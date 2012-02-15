/*
 *      Copyright (C) 2005-2008 Team XBMC
 *      Copyright (C) 2012 Mike Dyer <mike.dyer@md-soft.co.uk>
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */


#include "threads/SystemClock.h"
#include "system.h"
#include "signal.h"
#include "limits.h"
#include "threads/SingleLock.h"
#include "guilib/AudioContext.h"
#include "gstPlayer.h"
#include "windowing/WindowingFactory.h"
#include "dialogs/GUIDialogOK.h"
#include "guilib/GUIWindowManager.h"
#include "Application.h"
#include "filesystem/FileMusicDatabase.h"
#include "FileItem.h"
#include "utils/RegExp.h"
#include "utils/StringUtils.h"
#include "URL.h"
#include "utils/XMLUtils.h"
#include "utils/TimeUtils.h"
#include "utils/log.h"

#ifdef HAS_VIDEO_PLAYBACK
#include "cores/VideoRenderers/RenderManager.h"
#endif

#include <gst/gst.h>
#include <gst/interfaces/xoverlay.h>
#include <glib.h>

#if defined(HAS_LIRC)
#include "input/linux/LIRC.h"
#endif

CGstPlayer::CGstPlayer(IPlayerCallback& callback)
    : IPlayer(callback),
    CThread("CGstPlayer"),
    m_pipeline(NULL),
    m_is_playing(false)
{
  CLog::Log(LOGDEBUG, "CGstPlayer::CGstPlayer()");

  gst_init(NULL, NULL);

  m_pipeline = gst_element_factory_make("playbin2", "XBMC");
  m_loop = g_main_loop_new (NULL, FALSE);

  GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (m_pipeline));
  gst_bus_add_watch (bus, (GstBusFunc)BusMsg, this);
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler)BusSyncMsg, this);
  gst_object_unref (bus);
}

CGstPlayer::~CGstPlayer()
{
  CLog::Log(LOGDEBUG, "CGstPlayer::CloseFile()");
  gst_object_unref(m_pipeline);
  gst_deinit();
}

gboolean CGstPlayer::BusMsg (GstBus *bus, GstMessage *msg, CGstPlayer *me)
{
  switch (GST_MESSAGE_TYPE (msg)) {
  case GST_MESSAGE_EOS:
	  //m_callback.OnPlayBackEnded();
    break;
  default:
    break;
  }
  return TRUE;
}

GstBusSyncReply CGstPlayer::BusSyncMsg (GstBus * bus, GstMessage * message, CGstPlayer *me)
{
  // ignore anything but 'prepare-xwindow-id' element messages
  if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT)
    return GST_BUS_PASS;

  if (!gst_structure_has_name (message->structure, "prepare-xwindow-id"))
    return GST_BUS_PASS;

  CLog::Log(LOGDEBUG, "CGstPlayer::BusSyncMsg - setting x window id %lu", g_Windowing.GetWindow());

  gst_x_overlay_set_xwindow_id (GST_X_OVERLAY (GST_MESSAGE_SRC (message)), g_Windowing.GetWindow());

  return GST_BUS_DROP;
}

void CGstPlayer::Process()
{
  CLog::Log(LOGDEBUG, "CGstPlayer::Process - main loop starting");
  g_main_loop_run (m_loop);
  CLog::Log(LOGDEBUG, "CGstPlayer::Process - main loop finished");
}

bool CGstPlayer::IsPlaying() const
{
  return m_is_playing;
}

bool CGstPlayer::Initialize(TiXmlElement* pConfig)
{
  CLog::Log(LOGDEBUG, "CGstPlayer::Initialize");



  return true;
}

bool CGstPlayer::OpenFile(const CFileItem& file, const CPlayerOptions &options)
{
  m_is_playing = true;
  CStdString error = "No Error";
  CStdString uri;

  if (m_pipeline ==  NULL) {
    error = "No Pipeline";
    goto err;
  }

  /* Kill off any left overs */
  gst_element_set_state(m_pipeline, GST_STATE_NULL);

  /* Create the uri - we'll have to fix this for other 'protocols' */
  uri = "file://" + file.GetPath();
  g_object_set (G_OBJECT(m_pipeline), "uri", uri.c_str(), NULL);

  /* Change to ready, so we can check that all options are ok synchronously */
  if (gst_element_set_state(m_pipeline, GST_STATE_READY) == GST_STATE_CHANGE_FAILURE) {
    error = "Can't change state to READT";
    goto err;
  }

  /* Everything looks ok, go playing */
  gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
  Create();

  return true;

err:
  CLog::Log(LOGERROR, "CGstPlayer::OpenFile - %s", error.c_str());
  m_is_playing = false;
  return false;
}

bool CGstPlayer::CloseFile()
{
  CLog::Log(LOGDEBUG, "CGstPlayer::CloseFile()");
  if (m_pipeline) {
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    g_main_loop_quit (m_loop);
    WaitForThreadExit(INFINITE);
  }
  return true;
}

void CGstPlayer::Pause()
{
  if (IsPlaying())
    gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
}

bool CGstPlayer::IsPaused() const
{
  if (m_pipeline == NULL)
    return false;
  else {
    GstState state, pend;
    gst_element_get_state(m_pipeline, &state, &pend, 0);
    if (state == GST_STATE_PAUSED)
      return true;
    else
      return false;
  }
}

bool CGstPlayer::HasVideo() const
{
  return true;
}

bool CGstPlayer::HasAudio() const
{
  return false;
}

void CGstPlayer::SwitchToNextLanguage()
{
}

void CGstPlayer::ToggleSubtitles()
{
}

bool CGstPlayer::CanSeek()
{
  return false;
}

void CGstPlayer::Seek(bool bPlus, bool bLargeStep)
{
}

void CGstPlayer::GetAudioInfo(CStdString& strAudioInfo)
{
  strAudioInfo = "CGstPlayer:GetAudioInfo";
}

void CGstPlayer::GetVideoInfo(CStdString& strVideoInfo)
{
  strVideoInfo = "CGstPlayer:GetVideoInfo";
}

void CGstPlayer::GetGeneralInfo(CStdString& strGeneralInfo)
{
  strGeneralInfo = "CGstPlayer:GetGeneralInfo";
}

void CGstPlayer::SwitchToNextAudioLanguage()
{
}

void CGstPlayer::SeekPercentage(float iPercent)
{
}

float CGstPlayer::GetPercentage()
{
  return 0.0f;
}

void CGstPlayer::SetAVDelay(float fValue)
{
}

float CGstPlayer::GetAVDelay()
{
  return 0.0f;
}

void CGstPlayer::SetSubTitleDelay(float fValue)
{
}

float CGstPlayer::GetSubTitleDelay()
{
  return 0.0;
}

void CGstPlayer::SeekTime(__int64 iTime)
{
}

__int64 CGstPlayer::GetTime() // in millis
{

  return 0;
}

int CGstPlayer::GetTotalTime() // in seconds
{
  return 0;
}

void CGstPlayer::ToFFRW(int iSpeed)
{
}

void CGstPlayer::ShowOSD(bool bOnoff)
{
}

CStdString CGstPlayer::GetPlayerState()
{
  return "";
}

bool CGstPlayer::SetPlayerState(CStdString state)
{
  return true;
}
