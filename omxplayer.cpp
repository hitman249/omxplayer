/*
 * 
 *      Copyright (C) 2012 Edgar Hucek
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <termios.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <getopt.h>
#include <string.h>

#include <fstream>

#define AV_NOWARN_DEPRECATED

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
};

#include "OMXStreamInfo.h"

#include "utils/log.h"

#include "DllAvUtil.h"
#include "DllAvFormat.h"
#include "DllAvFilter.h"
#include "DllAvCodec.h"
#include "linux/RBP.h"

#include "OMXVideo.h"
#include "OMXAudioCodecOMX.h"
#include "utils/PCMRemap.h"
#include "OMXClock.h"
#include "OMXAudio.h"
#include "OMXReader.h"
#include "OMXPlayerVideo.h"
#include "OMXPlayerAudio.h"
#include "OMXPlayerSubtitles.h"
#include "OMXControl.h"
#include "DllOMX.h"
#include "Srt.h"
#include "KeyConfig.h"
#include "utils/Strprintf.h"
#include "Keyboard.h"

#include <iostream>
#include <sstream>
#include <vector>

#include <string>
#include <utility>

#include "version.h"

// when we repeatedly seek, rather than play continuously
#define TRICKPLAY(speed) (speed < 0 || speed > 4 * DVD_PLAYSPEED_NORMAL)

#define DISPLAY_TEXT(text, ms) if(m_osd) m_player_subtitles.DisplayText(text, ms)

#define DISPLAY_TEXT_SHORT(text) DISPLAY_TEXT(text, 1000)
#define DISPLAY_TEXT_LONG(text) DISPLAY_TEXT(text, 2000)

typedef enum {CONF_FLAGS_FORMAT_NONE, CONF_FLAGS_FORMAT_SBS, CONF_FLAGS_FORMAT_TB } FORMAT_3D_T;
enum PCMChannels  *m_pChannelMap        = NULL;
volatile sig_atomic_t g_abort           = false;
bool              m_passthrough         = false;
long              m_Volume              = 0;
long              m_Amplification       = 0;
bool              m_Deinterlace         = false;
bool              m_NoDeinterlace       = false;
bool              m_HWDecode            = false;
std::string       deviceString          = "";
int               m_use_hw_audio        = false;
bool              m_osd                 = true;
bool              m_no_keys             = false;
std::string       m_external_subtitles_path;
bool              m_has_external_subtitles = false;
std::string       m_font_path           = "/usr/share/fonts/truetype/freefont/FreeSans.ttf";
std::string       m_italic_font_path    = "/usr/share/fonts/truetype/freefont/FreeSansOblique.ttf";
std::string       m_dbus_name           = "org.mpris.MediaPlayer2.omxplayer";
bool              m_asked_for_font      = false;
bool              m_asked_for_italic_font = false;
float             m_font_size           = 0.055f;
bool              m_centered            = false;
bool              m_ghost_box           = true;
unsigned int      m_subtitle_lines      = 3;
bool              m_Pause               = false;
OMXReader         m_omx_reader;
int               m_audio_index_use     = -1;
bool              m_thread_player       = false;
OMXClock          *m_av_clock           = NULL;
OMXControl        m_omxcontrol;
Keyboard          *m_keyboard           = NULL;
COMXStreamInfo    m_hints_audio;
COMXStreamInfo    m_hints_video;
OMXPacket         *m_omx_pkt            = NULL;
bool              m_hdmi_clock_sync     = false;
bool              m_no_hdmi_clock_sync  = false;
bool              m_stop                = false;
int               m_subtitle_index      = -1;
DllBcmHost        m_BcmHost;
OMXPlayerVideo    m_player_video;
OMXPlayerAudio    m_player_audio;
OMXPlayerSubtitles  m_player_subtitles;
int               m_tv_show_info        = 0;
bool              m_has_video           = false;
bool              m_has_audio           = false;
bool              m_has_subtitle        = false;
float             m_display_aspect      = 0.0f;
bool              m_boost_on_downmix    = true;
bool              m_gen_log             = false;
bool              m_loop                = false;
int               m_layer               = 0;
bool              set_resolution        = false;
enum{ERROR=-1,SUCCESS,ONEBYTE};




/////////// Addon to Alex Dorohin hitman2491@gmail.com






std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while(std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}


std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    return split(s, delim, elems);
}

int get_day() {
    
    //char * weekday[] = { "Sun", "Mon", "Tue", "We", "Thu", "Fri", "Sat"};
    int day;

    // текущее время в UTC:
    
    time_t currentTime( ::time( NULL ) );


    // Определение дня недели и дня года:

    struct tm * ptm;

    ptm = ( localtime( &currentTime ) );
    day = ptm->tm_wday;
    if (day == 0) {
        day = 7;
    }
    // Вывод
    //std::cout << day << std::endl;
    return day;
}

std::string get_path_playlist(int day) {
    std::stringstream ss;
    ss << "/work/`PL0"<< day << ".m3u";
    // для вывода это преобразование не нужно, но мало чего там нужно будет
    //std::string mystr = ss.str();
    //std::cout << mystr << endl;
    return ss.str();
}

std::string get_path_file(std::string file) {
    std::stringstream ss;
    ss << "/work/" << file;
    // для вывода это преобразование не нужно, но мало чего там нужно будет
    //std::string mystr = ss.str();
    //std::cout << mystr << endl;
    return ss.str();
}

std::vector<std::string> get_playlist(int day) {

    std::vector<std::string> playlist;

    ifstream playlist_file(get_path_playlist(day), ios_base::binary);
    
    //std::cout << std::endl << "============>" << std::endl;
    
    std::string str;
    
    //while (!playlist_file.eof()) {
    while (std::getline(playlist_file, str)) {

        str.erase(str.find_last_not_of(" \n\r")+1);

        std::vector<std::string> temp = split(str, '.');
        std::string exe = temp[temp.size() - 1];
        
        
        if (exe == std::string("mp4") || exe == std::string("avi")) {

            int vector_size = playlist.size();

            if (vector_size > 0) {

                bool is = false;

                for (int i = 0; i < vector_size; i++) {
                    if (playlist[i] == str) {
                        is = true;
                        break;
                    }
                }

                if (!is) {
                    playlist.push_back(str);
                    //std::cout << str << std::endl;
                }

            } else {
                playlist.push_back(str);
                //std::cout << str << std::endl;
            }

        }
    }

    return playlist;
}

void print_playlist() {
    
    std::cout << get_path_playlist(get_day()) << std::endl;
    
    std::vector<std::string> playlist = get_playlist(get_day());
    int playlist_i = (int) playlist.size();
    for (int i = 0; i < playlist_i; ++i) {
        std::cout << playlist[i] << endl;
    }
}





/////////// End Addon










void sig_handler(int s)
{
  if (s==SIGINT && !g_abort)
  {
     signal(SIGINT, SIG_DFL);
     g_abort = true;
     return;
  }
  signal(SIGABRT, SIG_DFL);
  signal(SIGSEGV, SIG_DFL);
  signal(SIGFPE, SIG_DFL);
  if (NULL != m_keyboard)
  {
     m_keyboard->Close();
  }
  abort();
}

void print_usage()
{
  printf("Usage: omxplayer [OPTIONS] [FILE]\n");
  printf("Options :\n");
  printf("         -h / --help                    print this help\n");
  printf("         -v / --version                 print version info\n");
  printf("         -m / --playlist               print playlist\n");
  printf("         -k / --keys                    print key bindings\n");
//  printf("         -a / --alang language          audio language        : e.g. ger\n");
  printf("         -n / --aidx  index             audio stream index    : e.g. 1\n");
  printf("         -o / --adev  device            audio out device      : e.g. hdmi/local/both\n");
  printf("         -i / --info                    dump stream format and exit\n");
  printf("         -s / --stats                   pts and buffer stats\n");
  printf("         -p / --passthrough             audio passthrough\n");
  printf("         -d / --deinterlace             force deinterlacing\n");
  printf("              --nodeinterlace           force no deinterlacing\n");
  printf("         -w / --hw                      hw audio decoding\n");
  printf("         -3 / --3d mode                 switch tv into 3d mode (e.g. SBS/TB)\n");
  printf("         -y / --hdmiclocksync           adjust display refresh rate to match video (default)\n");
  printf("         -z / --nohdmiclocksync         do not adjust display refresh rate to match video\n");
  printf("         -t / --sid index               show subtitle with index\n");
  printf("         -r / --refresh                 adjust framerate/resolution to video\n");
  printf("         -g / --genlog                  generate log file\n");
  printf("         -l / --pos n                   start position (hh:mm:ss)\n");
  printf("         -b / --blank                   set background to black\n");
  printf("              --loop                    loop file. Ignored if file is not seekable, start position applied if given\n");
  printf("              --no-boost-on-downmix     don't boost volume when downmixing\n");
  printf("              --vol n                   Set initial volume in millibels (default 0)\n");
  printf("              --amp n                   Set initial amplification in millibels (default 0)\n");
  printf("              --no-osd                  do not display status information on screen\n");
  printf("              --no-keys                 disable keyboard input (useful to prevent hangs for certain TTYs)\n");
  printf("              --subtitles path          external subtitles in UTF-8 srt format\n");
  printf("              --font path               subtitle font\n");
  printf("                                        (default: /usr/share/fonts/truetype/freefont/FreeSans.ttf)\n");
  printf("              --italic-font path        (default: /usr/share/fonts/truetype/freefont/FreeSansOblique.ttf)\n");
  printf("              --font-size size          font size as thousandths of screen height\n");
  printf("                                        (default: 55)\n");
  printf("              --align left/center       subtitle alignment (default: left)\n");
  printf("              --no-ghost-box            no semitransparent boxes behind subtitles\n");
  printf("              --lines n                 number of lines to accommodate in the subtitle buffer\n");
  printf("                                        (default: 3)\n");
  printf("              --win \"x1 y1 x2 y2\"       Set position of video window\n");
  printf("              --audio_fifo  n           Size of audio output fifo in seconds\n");
  printf("              --video_fifo  n           Size of video output fifo in MB\n");
  printf("              --audio_queue n           Size of audio input queue in MB\n");
  printf("              --video_queue n           Size of video input queue in MB\n");
  printf("              --threshold   n           Amount of buffered data required to come out of buffering in seconds\n");
  printf("              --timeout     n           Amount of time a file/network operation can stall for before timing out (default 10s)\n");
  printf("              --orientation n           Set orientation of video (0, 90, 180 or 270)\n");
  printf("              --fps n                   Set fps of video where timestamps are not present\n");
  printf("              --live                    Set for live tv or vod type stream\n");
  printf("              --layout                  Set output speaker layout (e.g. 5.1)\n");
  printf("              --dbus_name name          Set D-Bus bus name\n");
  printf("                                        (default: org.mpris.MediaPlayer2.omxplayer)\n");
  printf("              --key-config <file>       Uses key bindings specified in <file> instead of the default\n");
  printf("              --layer n                 Set the video render layer number (higher numbers are on top)\n");
}

void print_keybindings()
{
  printf("Key bindings :\n");
  printf("        1                  decrease speed\n");
  printf("        2                  increase speed\n");
  printf("        <                  rewind\n");
  printf("        >                  fast forward\n");
  printf("        z                  show info\n");
  printf("        j                  previous audio stream\n");
  printf("        k                  next audio stream\n");
  printf("        i                  previous chapter\n");
  printf("        o                  next chapter\n");
  printf("        n                  previous subtitle stream\n");
  printf("        m                  next subtitle stream\n");
  printf("        s                  toggle subtitles\n");
  printf("        d                  decrease subtitle delay (- 250 ms)\n");
  printf("        f                  increase subtitle delay (+ 250 ms)\n");
  printf("        q                  exit omxplayer\n");
  printf("        p / space          pause/resume\n");
  printf("        -                  decrease volume\n");
  printf("        + / =              increase volume\n");
  printf("        left arrow         seek -30 seconds\n");
  printf("        right arrow        seek +30 seconds\n");
  printf("        down arrow         seek -600 seconds\n");
  printf("        up arrow           seek +600 seconds\n");
}

void print_version()
{  
  printf("omxplayer - Commandline multimedia player for the Raspberry Pi\n");
  printf("        Build date: %s\n", VERSION_DATE);
  printf("        Version   : %s [%s]\n", VERSION_HASH, VERSION_BRANCH);
  printf("        Repository: %s\n", VERSION_REPO);
}

static void PrintSubtitleInfo()
{
  auto count = m_omx_reader.SubtitleStreamCount();
  size_t index = 0;

  if(m_has_external_subtitles)
  {
    ++count;
    if(!m_player_subtitles.GetUseExternalSubtitles())
      index = m_player_subtitles.GetActiveStream() + 1;
  }
  else if(m_has_subtitle)
  {
      index = m_player_subtitles.GetActiveStream();
  }

//  printf("Subtitle count: %d, state: %s, index: %d, delay: %d\n",
//         count,
//         m_has_subtitle && m_player_subtitles.GetVisible() ? " on" : "off",
//         index+1,
//         m_has_subtitle ? m_player_subtitles.GetDelay() : 0);
}

static void FlushStreams(double pts);

static void SetSpeed(int iSpeed)
{
  if(!m_av_clock)
    return;

  m_omx_reader.SetSpeed(iSpeed);

  // flush when in trickplay mode
  if (TRICKPLAY(iSpeed) || TRICKPLAY(m_av_clock->OMXPlaySpeed()))
    FlushStreams(DVD_NOPTS_VALUE);

  m_av_clock->OMXSetSpeed(iSpeed);
}

static float get_display_aspect_ratio(HDMI_ASPECT_T aspect)
{
  float display_aspect;
  switch (aspect) {
    case HDMI_ASPECT_4_3:   display_aspect = 4.0/3.0;   break;
    case HDMI_ASPECT_14_9:  display_aspect = 14.0/9.0;  break;
    case HDMI_ASPECT_16_9:  display_aspect = 16.0/9.0;  break;
    case HDMI_ASPECT_5_4:   display_aspect = 5.0/4.0;   break;
    case HDMI_ASPECT_16_10: display_aspect = 16.0/10.0; break;
    case HDMI_ASPECT_15_9:  display_aspect = 15.0/9.0;  break;
    case HDMI_ASPECT_64_27: display_aspect = 64.0/27.0; break;
    default:                display_aspect = 16.0/9.0;  break;
  }
  return display_aspect;
}

static float get_display_aspect_ratio(SDTV_ASPECT_T aspect)
{
  float display_aspect;
  switch (aspect) {
    case SDTV_ASPECT_4_3:  display_aspect = 4.0/3.0;  break;
    case SDTV_ASPECT_14_9: display_aspect = 14.0/9.0; break;
    case SDTV_ASPECT_16_9: display_aspect = 16.0/9.0; break;
    default:               display_aspect = 4.0/3.0;  break;
  }
  return display_aspect;
}

static void FlushStreams(double pts)
{
  m_av_clock->OMXStop();
  m_av_clock->OMXPause();

  if(m_has_video)
    m_player_video.Flush();

  if(m_has_audio)
    m_player_audio.Flush();

  if(pts != DVD_NOPTS_VALUE)
    m_av_clock->OMXMediaTime(0.0);

  if(m_has_subtitle)
    m_player_subtitles.Flush();

  if(m_omx_pkt)
  {
    m_omx_reader.FreePacket(m_omx_pkt);
    m_omx_pkt = NULL;
  }
}

static void CallbackTvServiceCallback(void *userdata, uint32_t reason, uint32_t param1, uint32_t param2)
{
  sem_t *tv_synced = (sem_t *)userdata;
  switch(reason)
  {
  case VC_HDMI_UNPLUGGED:
    break;
  case VC_HDMI_STANDBY:
    break;
  case VC_SDTV_NTSC:
  case VC_SDTV_PAL:
  case VC_HDMI_HDMI:
  case VC_HDMI_DVI:
    // Signal we are ready now
    sem_post(tv_synced);
    break;
  default:
     break;
  }
}

void SetVideoMode(int width, int height, int fpsrate, int fpsscale, FORMAT_3D_T is3d)
{
  int32_t num_modes = 0;
  int i;
  HDMI_RES_GROUP_T prefer_group;
  HDMI_RES_GROUP_T group = HDMI_RES_GROUP_CEA;
  float fps = 60.0f; // better to force to higher rate if no information is known
  uint32_t prefer_mode;

  if (fpsrate && fpsscale)
    fps = DVD_TIME_BASE / OMXReader::NormalizeFrameduration((double)DVD_TIME_BASE * fpsscale / fpsrate);

  //Supported HDMI CEA/DMT resolutions, preferred resolution will be returned
  TV_SUPPORTED_MODE_NEW_T *supported_modes = NULL;
  // query the number of modes first
  int max_supported_modes = m_BcmHost.vc_tv_hdmi_get_supported_modes_new(group, NULL, 0, &prefer_group, &prefer_mode);
 
  if (max_supported_modes > 0)
    supported_modes = new TV_SUPPORTED_MODE_NEW_T[max_supported_modes];
 
  if (supported_modes)
  {
    num_modes = m_BcmHost.vc_tv_hdmi_get_supported_modes_new(group,
        supported_modes, max_supported_modes, &prefer_group, &prefer_mode);

    if(m_gen_log) {
    CLog::Log(LOGDEBUG, "EGL get supported modes (%d) = %d, prefer_group=%x, prefer_mode=%x\n",
        group, num_modes, prefer_group, prefer_mode);
    }
  }

  TV_SUPPORTED_MODE_NEW_T *tv_found = NULL;

  if (num_modes > 0 && prefer_group != HDMI_RES_GROUP_INVALID)
  {
    uint32_t best_score = 1<<30;
    uint32_t scan_mode = 0;

    for (i=0; i<num_modes; i++)
    {
      TV_SUPPORTED_MODE_NEW_T *tv = supported_modes + i;
      uint32_t score = 0;
      uint32_t w = tv->width;
      uint32_t h = tv->height;
      uint32_t r = tv->frame_rate;

      /* Check if frame rate match (equal or exact multiple) */
      if(fabs(r - 1.0f*fps) / fps < 0.002f)
  score += 0;
      else if(fabs(r - 2.0f*fps) / fps < 0.002f)
  score += 1<<8;
      else 
  score += (1<<28)/r; // bad - but prefer higher framerate

      /* Check size too, only choose, bigger resolutions */
      if(width && height) 
      {
        /* cost of too small a resolution is high */
        score += max((int)(width -w), 0) * (1<<16);
        score += max((int)(height-h), 0) * (1<<16);
        /* cost of too high a resolution is lower */
        score += max((int)(w-width ), 0) * (1<<4);
        score += max((int)(h-height), 0) * (1<<4);
      } 

      // native is good
      if (!tv->native) 
        score += 1<<16;

      // interlace is bad
      if (scan_mode != tv->scan_mode) 
        score += (1<<16);

      // wanting 3D but not getting it is a negative
      if (is3d == CONF_FLAGS_FORMAT_SBS && !(tv->struct_3d_mask & HDMI_3D_STRUCT_SIDE_BY_SIDE_HALF_HORIZONTAL))
        score += 1<<18;
      if (is3d == CONF_FLAGS_FORMAT_TB  && !(tv->struct_3d_mask & HDMI_3D_STRUCT_TOP_AND_BOTTOM))
        score += 1<<18;

      // prefer square pixels modes
      float par = get_display_aspect_ratio((HDMI_ASPECT_T)tv->aspect_ratio)*(float)tv->height/(float)tv->width;
      score += fabs(par - 1.0f) * (1<<12);

      /*printf("mode %dx%d@%d %s%s:%x par=%.2f score=%d\n", tv->width, tv->height, 
             tv->frame_rate, tv->native?"N":"", tv->scan_mode?"I":"", tv->code, par, score);*/

      if (score < best_score) 
      {
        tv_found = tv;
        best_score = score;
      }
    }
  }

  if(tv_found)
  {
    printf("Output mode %d: %dx%d@%d %s%s:%x\n", tv_found->code, tv_found->width, tv_found->height, 
           tv_found->frame_rate, tv_found->native?"N":"", tv_found->scan_mode?"I":"", tv_found->code);
    // if we are closer to ntsc version of framerate, let gpu know
    int ifps = (int)(fps+0.5f);
    bool ntsc_freq = fabs(fps*1001.0f/1000.0f - ifps) < fabs(fps-ifps);
    char response[80];
    vc_gencmd(response, sizeof response, "hdmi_ntsc_freqs %d", ntsc_freq);

    /* inform TV of any 3D settings. Note this property just applies to next hdmi mode change, so no need to call for 2D modes */
    HDMI_PROPERTY_PARAM_T property;
    property.property = HDMI_PROPERTY_3D_STRUCTURE;
    property.param1 = HDMI_3D_FORMAT_NONE;
    property.param2 = 0;
    if (is3d != CONF_FLAGS_FORMAT_NONE)
    {
      if (is3d == CONF_FLAGS_FORMAT_SBS && tv_found->struct_3d_mask & HDMI_3D_STRUCT_SIDE_BY_SIDE_HALF_HORIZONTAL)
        property.param1 = HDMI_3D_FORMAT_SBS_HALF;
      else if (is3d == CONF_FLAGS_FORMAT_TB && tv_found->struct_3d_mask & HDMI_3D_STRUCT_TOP_AND_BOTTOM)
        property.param1 = HDMI_3D_FORMAT_TB_HALF;
      m_BcmHost.vc_tv_hdmi_set_property(&property);
    }

    printf("ntsc_freq:%d %s%s\n", ntsc_freq, property.param1 == HDMI_3D_FORMAT_SBS_HALF ? "3DSBS":"", property.param1 == HDMI_3D_FORMAT_TB_HALF ? "3DTB":"");
    sem_t tv_synced;
    sem_init(&tv_synced, 0, 0);
    m_BcmHost.vc_tv_register_callback(CallbackTvServiceCallback, &tv_synced);
    int success = m_BcmHost.vc_tv_hdmi_power_on_explicit_new(HDMI_MODE_HDMI, (HDMI_RES_GROUP_T)group, tv_found->code);
    if (success == 0)
      sem_wait(&tv_synced);
    m_BcmHost.vc_tv_unregister_callback(CallbackTvServiceCallback);
    sem_destroy(&tv_synced);
  }
  if (supported_modes)
    delete[] supported_modes;
}

bool Exists(const std::string& path)
{
  struct stat buf;
  auto error = stat(path.c_str(), &buf);
  return !error || errno != ENOENT;
}

bool IsURL(const std::string& str)
{
  auto result = str.find("://");
  if(result == std::string::npos || result == 0)
    return false;

  for(size_t i = 0; i < result; ++i)
  {
    if(!isalpha(str[i]))
      return false;
  }
  return true;
}

bool IsPipe(const std::string& str)
{
  if (str.compare(0, 5, "pipe:") == 0)
    return true;
  return false;
}

static int get_mem_gpu(void)
{
   char response[80] = "";
   int gpu_mem = 0;
   if (vc_gencmd(response, sizeof response, "get_mem gpu") == 0)
      vc_gencmd_number_property(response, "gpu", &gpu_mem);
   return gpu_mem;
}

static void blank_background(bool enable)
{
  if (!enable)
    return;
  // we create a 1x1 black pixel image that is added to display just behind video
  DISPMANX_DISPLAY_HANDLE_T   display;
  DISPMANX_UPDATE_HANDLE_T    update;
  DISPMANX_RESOURCE_HANDLE_T  resource;
  DISPMANX_ELEMENT_HANDLE_T   element;
  int             ret;
  uint32_t vc_image_ptr;
  VC_IMAGE_TYPE_T type = VC_IMAGE_RGB565;
  uint16_t image = 0x0000; // black
  int             layer = m_layer - 1;

  VC_RECT_T dst_rect, src_rect;

  display = vc_dispmanx_display_open(0);
  assert(display);

  resource = vc_dispmanx_resource_create( type, 1 /*width*/, 1 /*height*/, &vc_image_ptr );
  assert( resource );

  vc_dispmanx_rect_set( &dst_rect, 0, 0, 1, 1);

  ret = vc_dispmanx_resource_write_data( resource, type, sizeof(image), &image, &dst_rect );
  assert(ret == 0);

  vc_dispmanx_rect_set( &src_rect, 0, 0, 1<<16, 1<<16);
  vc_dispmanx_rect_set( &dst_rect, 0, 0, 0, 0);

  update = vc_dispmanx_update_start(0);
  assert(update);

  element = vc_dispmanx_element_add(update, display, layer, &dst_rect, resource, &src_rect,
                                    DISPMANX_PROTECTION_NONE, NULL, NULL, (DISPMANX_TRANSFORM_T)0 );
  assert(element);

  ret = vc_dispmanx_update_submit_sync( update );
  assert( ret == 0 );
}

int main(int argc, char *argv[])
{
  signal(SIGSEGV, sig_handler);
  signal(SIGABRT, sig_handler);
  signal(SIGFPE, sig_handler);
  signal(SIGINT, sig_handler);

  bool                  m_send_eos            = false;
  bool                  m_packet_after_seek   = false;
  bool                  m_seek_flush          = false;
  bool                  m_new_win_pos         = false;
  std::string           m_filename;
  double                m_incr                = 0;
  double                m_loop_from           = 0;
  CRBP                  g_RBP;
  COMXCore              g_OMX;
  bool                  m_stats               = false;
  bool                  m_dump_format         = false;
  FORMAT_3D_T           m_3d                  = CONF_FLAGS_FORMAT_NONE;
  bool                  m_refresh             = false;
  double                startpts              = 0;
  CRect                 DestRect              = {0,0,0,0};
  bool                  m_blank_background    = false;
  bool sentStarted = false;
  float audio_fifo_size = 0.0; // zero means use default
  float video_fifo_size = 0.0;
  float audio_queue_size = 0.0;
  float video_queue_size = 0.0;
  float m_threshold      = -1.0f; // amount of audio/video required to come out of buffering
  float m_timeout        = 10.0f; // amount of time file/network operation can stall for before timing out
  int m_orientation      = -1; // unset
  float m_fps            = 0.0f; // unset
  bool m_live            = false; // set to true for live tv or vod for low buffering
  enum PCMLayout m_layout = PCM_LAYOUT_2_0;
  TV_DISPLAY_STATE_T   tv_state;
  double last_seek_pos = 0;
  bool idle = false;

  const int font_opt        = 0x100;
  const int italic_font_opt = 0x201;
  const int font_size_opt   = 0x101;
  const int align_opt       = 0x102;
  const int no_ghost_box_opt = 0x203;
  const int subtitles_opt   = 0x103;
  const int lines_opt       = 0x104;
  const int pos_opt         = 0x105;
  const int vol_opt         = 0x106;
  const int audio_fifo_opt  = 0x107;
  const int video_fifo_opt  = 0x108;
  const int audio_queue_opt = 0x109;
  const int video_queue_opt = 0x10a;
  const int no_deinterlace_opt = 0x10b;
  const int threshold_opt   = 0x10c;
  const int timeout_opt     = 0x10f;
  const int boost_on_downmix_opt = 0x200;
  const int no_boost_on_downmix_opt = 0x207;
  const int key_config_opt  = 0x10d;
  const int amp_opt         = 0x10e;
  const int no_osd_opt      = 0x202;
  const int orientation_opt = 0x204;
  const int fps_opt         = 0x208;
  const int live_opt        = 0x205;
  const int layout_opt      = 0x206;
  const int dbus_name_opt   = 0x209;
  const int loop_opt        = 0x20a;
  const int layer_opt       = 0x20b;
  const int no_keys_opt     = 0x20c;

  struct option longopts[] = {
    { "info",         no_argument,        NULL,          'i' },
    { "help",         no_argument,        NULL,          'h' },
    { "version",      no_argument,        NULL,          'v' },
    { "playlist",     no_argument,        NULL,          'm' },
    { "keys",         no_argument,        NULL,          'k' },
    { "aidx",         required_argument,  NULL,          'n' },
    { "adev",         required_argument,  NULL,          'o' },
    { "stats",        no_argument,        NULL,          's' },
    { "passthrough",  no_argument,        NULL,          'p' },
    { "vol",          required_argument,  NULL,          vol_opt },
    { "amp",          required_argument,  NULL,          amp_opt },
    { "deinterlace",  no_argument,        NULL,          'd' },
    { "nodeinterlace",no_argument,        NULL,          no_deinterlace_opt },
    { "hw",           no_argument,        NULL,          'w' },
    { "3d",           required_argument,  NULL,          '3' },
    { "hdmiclocksync", no_argument,       NULL,          'y' },
    { "nohdmiclocksync", no_argument,     NULL,          'z' },
    { "refresh",      no_argument,        NULL,          'r' },
    { "genlog",       no_argument,        NULL,          'g' },
    { "sid",          required_argument,  NULL,          't' },
    { "pos",          required_argument,  NULL,          'l' },    
    { "blank",        no_argument,        NULL,          'b' },
    { "font",         required_argument,  NULL,          font_opt },
    { "italic-font",  required_argument,  NULL,          italic_font_opt },
    { "font-size",    required_argument,  NULL,          font_size_opt },
    { "align",        required_argument,  NULL,          align_opt },
    { "no-ghost-box", no_argument,        NULL,          no_ghost_box_opt },
    { "subtitles",    required_argument,  NULL,          subtitles_opt },
    { "lines",        required_argument,  NULL,          lines_opt },
    { "win",          required_argument,  NULL,          pos_opt },
    { "audio_fifo",   required_argument,  NULL,          audio_fifo_opt },
    { "video_fifo",   required_argument,  NULL,          video_fifo_opt },
    { "audio_queue",  required_argument,  NULL,          audio_queue_opt },
    { "video_queue",  required_argument,  NULL,          video_queue_opt },
    { "threshold",    required_argument,  NULL,          threshold_opt },
    { "timeout",      required_argument,  NULL,          timeout_opt },
    { "boost-on-downmix", no_argument,    NULL,          boost_on_downmix_opt },
    { "no-boost-on-downmix", no_argument, NULL,          no_boost_on_downmix_opt },
    { "key-config",   required_argument,  NULL,          key_config_opt },
    { "no-osd",       no_argument,        NULL,          no_osd_opt },
    { "no-keys",      no_argument,        NULL,          no_keys_opt },
    { "orientation",  required_argument,  NULL,          orientation_opt },
    { "fps",          required_argument,  NULL,          fps_opt },
    { "live",         no_argument,        NULL,          live_opt },
    { "layout",       required_argument,  NULL,          layout_opt },
    { "dbus_name",    required_argument,  NULL,          dbus_name_opt },
    { "loop",         no_argument,        NULL,          loop_opt },
    { "layer",        required_argument,  NULL,          layer_opt },
    { 0, 0, 0, 0 }
  };

  #define S(x) (int)(DVD_PLAYSPEED_NORMAL*(x))
  int playspeeds[] = {S(0), S(1/16.0), S(1/8.0), S(1/4.0), S(1/2.0), S(0.975), S(1.0), S(1.125), S(-32.0), S(-16.0), S(-8.0), S(-4), S(-2), S(-1), S(1), S(2.0), S(4.0), S(8.0), S(16.0), S(32.0)};
  const int playspeed_slow_min = 0, playspeed_slow_max = 7, playspeed_rew_max = 8, playspeed_rew_min = 13, playspeed_normal = 14, playspeed_ff_min = 15, playspeed_ff_max = 19;
  int playspeed_current = playspeed_normal;
  double m_last_check_time = 0.0;
  float m_latency = 0.0f;
  int c;
  std::string mode;

  //Build default keymap just in case the --key-config option isn't used
  map<int,int> keymap = KeyConfig::buildDefaultKeymap();

  while ((c = getopt_long(argc, argv, "wihvkn:l:o:cslbpd3:yzt:rg", longopts, NULL)) != -1)
  {
    switch (c) 
    {
      case 'r':
        m_refresh = true;
        break;
      case 'g':
        m_gen_log = true;
        break;
      case 'y':
        m_hdmi_clock_sync = true;
        break;
      case 'z':
        m_no_hdmi_clock_sync = true;
        break;
      case '3':
        mode = optarg;
        if(mode != "SBS" && mode != "TB")
        {
          print_usage();
          return 0;
        }
        if(mode == "TB")
          m_3d = CONF_FLAGS_FORMAT_TB;
        else
          m_3d = CONF_FLAGS_FORMAT_SBS;
        break;
      case 'd':
        m_Deinterlace = true;
        break;
      case no_deinterlace_opt:
        m_NoDeinterlace = true;
        break;
      case 'w':
        m_use_hw_audio = true;
        break;
      case 'p':
        m_passthrough = true;
        break;
      case 's':
        m_stats = true;
        break;
      case 'o':
        deviceString = optarg;
        if(deviceString != "local" && deviceString != "hdmi" && deviceString != "both")
        {
          print_usage();
          return 0;
        }
        deviceString = "omx:" + deviceString;
        break;
      case 'i':
        m_dump_format = true;
        break;
      case 't':
        m_subtitle_index = atoi(optarg) - 1;
        if(m_subtitle_index < 0)
          m_subtitle_index = 0;
        break;
      case 'n':
        m_audio_index_use = atoi(optarg) - 1;
        if(m_audio_index_use < 0)
          m_audio_index_use = 0;
        break;
      case 'l':
        {
          if(strchr(optarg, ':'))
          {
            unsigned int h, m, s;
            if(sscanf(optarg, "%u:%u:%u", &h, &m, &s) == 3)
              m_incr = h*3600 + m*60 + s;
          }
          else
          {
            m_incr = atof(optarg);
          }
          if(m_loop)
            m_loop_from = m_incr;
        }
        break;
      case no_osd_opt:
        m_osd = false;
        break;
      case no_keys_opt:
        m_no_keys = true;
        break;
      case font_opt:
        m_font_path = optarg;
        m_asked_for_font = true;
        break;
      case italic_font_opt:
        m_italic_font_path = optarg;
        m_asked_for_italic_font = true;
        break;
      case font_size_opt:
        {
          const int thousands = atoi(optarg);
          if (thousands > 0)
            m_font_size = thousands*0.001f;
        }
        break;
      case align_opt:
        m_centered = !strcmp(optarg, "center");
        break;
      case no_ghost_box_opt:
        m_ghost_box = false;
        break;
      case subtitles_opt:
        m_external_subtitles_path = optarg;
        m_has_external_subtitles = true;
        break;
      case lines_opt:
        m_subtitle_lines = std::max(atoi(optarg), 1);
        break;
      case pos_opt:
  sscanf(optarg, "%f %f %f %f", &DestRect.x1, &DestRect.y1, &DestRect.x2, &DestRect.y2);
        break;
      case vol_opt:
	m_Volume = atoi(optarg);
        break;
      case amp_opt:
	m_Amplification = atoi(optarg);
        break;
      case boost_on_downmix_opt:
        m_boost_on_downmix = true;
        break;
      case no_boost_on_downmix_opt:
        m_boost_on_downmix = false;
        break;
      case audio_fifo_opt:
  audio_fifo_size = atof(optarg);
        break;
      case video_fifo_opt:
  video_fifo_size = atof(optarg);
        break;
      case audio_queue_opt:
  audio_queue_size = atof(optarg);
        break;
      case video_queue_opt:
  video_queue_size = atof(optarg);
        break;
      case threshold_opt:
  m_threshold = atof(optarg);
        break;
      case timeout_opt:
        m_timeout = atof(optarg);
        break;
      case orientation_opt:
        m_orientation = atoi(optarg);
        break;
      case fps_opt:
        m_fps = atof(optarg);
        break;
      case live_opt:
        m_live = true;
        break;
      case layout_opt:
      {
        const char *layouts[] = {"2.0", "2.1", "3.0", "3.1", "4.0", "4.1", "5.0", "5.1", "7.0", "7.1"};
        unsigned i;
        for (i=0; i<sizeof layouts/sizeof *layouts; i++)
          if (strcmp(optarg, layouts[i]) == 0)
          {
            m_layout = (enum PCMLayout)i;
            break;
          }
        if (i == sizeof layouts/sizeof *layouts)
        {
          print_usage();
          return 0;
        }
        break;
      }
      case dbus_name_opt:
        m_dbus_name = optarg;
        break;
      case loop_opt:
        if(m_incr != 0)
            m_loop_from = m_incr;
        m_loop = true;
        break;
      case 'b':
        m_blank_background = true;
        break;
      case key_config_opt:
        keymap = KeyConfig::parseConfigFile(optarg);
        break;
      case layer_opt:
        m_layer = atoi(optarg);
        break;
      case 0:
        break;
      case 'h':
        print_usage();
        return 0;
        break;
      case 'v':
        print_version();
        return 0;
        break;
      case 'm':
        print_playlist();
        return 0;
        break;
      case 'k':
        print_keybindings();
        return 0;
        break;
      case ':':
        return 0;
        break;
      default:
        return 0;
        break;
    }
  }

  if (optind >= argc) {
    print_usage();
    return 0;
  }
  
  std::vector<std::string> media_file = get_playlist(get_day());
  signed int media_file_index = -1;
  
  do_next:
          if ((int) media_file.size()-1 > media_file_index) {
              media_file_index++;
          } else {
              media_file_index = 0;
              media_file = get_playlist(get_day());
          }
  //m_filename = argv[optind];
          
  m_filename = get_path_file(media_file[media_file_index]);

//  if (false == m_no_keys)
//  {
//      m_keyboard = new Keyboard();
  //}

  auto PrintFileNotFound = [](const std::string& path)
  {
    printf("File \"%s\" not found.\n", path.c_str());
  };

  bool filename_is_URL = IsURL(m_filename);

  if(!filename_is_URL && !IsPipe(m_filename) && !Exists(m_filename))
  {
    PrintFileNotFound(m_filename);
    goto do_next;
    return 0;
  }

  if(m_asked_for_font && !Exists(m_font_path))
  {
    PrintFileNotFound(m_font_path);
    goto do_next;
    return 0;
  }

  if(m_asked_for_italic_font && !Exists(m_italic_font_path))
  {
    PrintFileNotFound(m_italic_font_path);
    goto do_next;
    return 0;
  }

  if(m_has_external_subtitles && !Exists(m_external_subtitles_path))
  {
    PrintFileNotFound(m_external_subtitles_path);
    goto do_next;
    return 0;
  }

  if(!m_has_external_subtitles && !filename_is_URL)
  {
    auto subtitles_path = m_filename.substr(0, m_filename.find_last_of(".")) +
                          ".srt";

    if(Exists(subtitles_path))
    {
      m_external_subtitles_path = subtitles_path;
      m_has_external_subtitles = true;
    }
  }
    
  bool m_audio_extension = false;
  const CStdString m_musicExtensions = ".nsv|.m4a|.flac|.aac|.strm|.pls|.rm|.rma|.mpa|.wav|.wma|.ogg|.mp3|.mp2|.m3u|.mod|.amf|.669|.dmf|.dsm|.far|.gdm|"
                 ".imf|.it|.m15|.med|.okt|.s3m|.stm|.sfx|.ult|.uni|.xm|.sid|.ac3|.dts|.cue|.aif|.aiff|.wpl|.ape|.mac|.mpc|.mp+|.mpp|.shn|.zip|.rar|"
                 ".wv|.nsf|.spc|.gym|.adx|.dsp|.adp|.ymf|.ast|.afc|.hps|.xsp|.xwav|.waa|.wvs|.wam|.gcm|.idsp|.mpdsp|.mss|.spt|.rsd|.mid|.kar|.sap|"
                 ".cmc|.cmr|.dmc|.mpt|.mpd|.rmt|.tmc|.tm8|.tm2|.oga|.url|.pxml|.tta|.rss|.cm3|.cms|.dlt|.brstm|.wtv|.mka";
  if (m_filename.find_last_of(".") != string::npos)
  {
    CStdString extension = m_filename.substr(m_filename.find_last_of("."));
    if (!extension.IsEmpty() && m_musicExtensions.Find(extension.ToLower()) != -1)
      m_audio_extension = true;
  }
  if(m_gen_log) {
    CLog::SetLogLevel(LOG_LEVEL_DEBUG);
    CLog::Init("./");
  } else {
    CLog::SetLogLevel(LOG_LEVEL_NONE);
  }
  
  g_RBP.Initialize();
  g_OMX.Initialize();

  blank_background(m_blank_background);

  int gpu_mem = get_mem_gpu();
  int min_gpu_mem = 64;
  if (gpu_mem > 0 && gpu_mem < min_gpu_mem)
    printf("Only %dM of gpu_mem is configured. Try running \"sudo raspi-config\" and ensure that \"memory_split\" has a value of %d or greater\n", gpu_mem, min_gpu_mem);

  m_av_clock = new OMXClock();
  //m_omxcontrol.init(m_av_clock, &m_player_audio, &m_player_subtitles, &m_omx_reader, m_dbus_name);
  //if (NULL != m_keyboard)
  //{
//    m_keyboard->setKeymap(keymap);
//    m_keyboard->setDbusName(m_dbus_name);
//  }

  m_thread_player = true;

  if(!m_omx_reader.Open(m_filename.c_str(), m_dump_format, m_live, m_timeout))
    goto do_exit;

  if(m_dump_format)
    goto do_exit;

  m_has_video     = m_omx_reader.VideoStreamCount();
  m_has_audio     = m_omx_reader.AudioStreamCount();
  m_has_subtitle  = m_has_external_subtitles ||
                    m_omx_reader.SubtitleStreamCount();
  m_loop          = m_loop && m_omx_reader.CanSeek();

  if (m_audio_extension)
  {
    CLog::Log(LOGWARNING, "%s - Ignoring video in audio filetype:%s", __FUNCTION__, m_filename.c_str());
    m_has_video = false;
  }

  if(m_filename.find("3DSBS") != string::npos || m_filename.find("HSBS") != string::npos)
    m_3d = CONF_FLAGS_FORMAT_SBS;
  else if(m_filename.find("3DTAB") != string::npos || m_filename.find("HTAB") != string::npos)
    m_3d = CONF_FLAGS_FORMAT_TB;

  // 3d modes don't work without switch hdmi mode
  if (m_3d != CONF_FLAGS_FORMAT_NONE)
    m_refresh = true;

  // you really don't want want to match refresh rate without hdmi clock sync
  if (m_refresh && !m_no_hdmi_clock_sync)
    m_hdmi_clock_sync = true;

  if(!m_av_clock->OMXInitialize())
    goto do_exit;

  if(m_hdmi_clock_sync && !m_av_clock->HDMIClockSync())
    goto do_exit;

  m_av_clock->OMXStateIdle();
  m_av_clock->OMXStop();
  m_av_clock->OMXPause();

  m_omx_reader.GetHints(OMXSTREAM_AUDIO, m_hints_audio);
  m_omx_reader.GetHints(OMXSTREAM_VIDEO, m_hints_video);

  if (m_fps > 0.0f)
    m_hints_video.fpsrate = m_fps * DVD_TIME_BASE, m_hints_video.fpsscale = DVD_TIME_BASE;

  if(m_audio_index_use != -1)
    m_omx_reader.SetActiveStream(OMXSTREAM_AUDIO, m_audio_index_use);
          
  if(m_has_video && m_refresh)
  {
    memset(&tv_state, 0, sizeof(TV_DISPLAY_STATE_T));
    m_BcmHost.vc_tv_get_display_state(&tv_state);
    if (!set_resolution){
        set_resolution = true;
        //SetVideoMode(m_hints_video.width, m_hints_video.height, m_hints_video.fpsrate, m_hints_video.fpsscale, m_3d);
        SetVideoMode((int)1280, (int)720, m_hints_video.fpsrate, m_hints_video.fpsscale, m_3d);
    }
   
   }
  // get display aspect
  TV_DISPLAY_STATE_T current_tv_state;
  memset(&current_tv_state, 0, sizeof(TV_DISPLAY_STATE_T));
  m_BcmHost.vc_tv_get_display_state(&current_tv_state);
  if(current_tv_state.state & ( VC_HDMI_HDMI | VC_HDMI_DVI )) {
    //HDMI or DVI on
    m_display_aspect = get_display_aspect_ratio((HDMI_ASPECT_T)current_tv_state.display.hdmi.aspect_ratio);
  } else {
    //composite on
    m_display_aspect = get_display_aspect_ratio((SDTV_ASPECT_T)current_tv_state.display.sdtv.display_options.aspect);
  }
  m_display_aspect *= (float)current_tv_state.display.hdmi.height/(float)current_tv_state.display.hdmi.width;

  if (m_orientation >= 0)
    m_hints_video.orientation = m_orientation;
  if(m_has_video && !m_player_video.Open(m_hints_video, m_av_clock, DestRect, m_Deinterlace ? VS_DEINTERLACEMODE_FORCE:m_NoDeinterlace ? VS_DEINTERLACEMODE_OFF:VS_DEINTERLACEMODE_AUTO,
                                         m_hdmi_clock_sync, m_thread_player, m_display_aspect, m_layer, video_queue_size, video_fifo_size))
    goto do_exit;

  if(m_has_subtitle || m_osd)
  {
    std::vector<Subtitle> external_subtitles;
    if(m_has_external_subtitles &&
       !ReadSrt(m_external_subtitles_path, external_subtitles))
    {
       puts("Unable to read the subtitle file.");
       goto do_exit;
    }

    if(!m_player_subtitles.Open(m_omx_reader.SubtitleStreamCount(),
                                std::move(external_subtitles),
                                m_font_path,
                                m_italic_font_path,
                                m_font_size,
                                m_centered,
                                m_ghost_box,
                                m_subtitle_lines,
                                m_layer + 1,
                                m_av_clock))
      goto do_exit;
  }

  if(m_has_subtitle)
  {
    if(!m_has_external_subtitles)
    {
      if(m_subtitle_index != -1)
      {
        m_player_subtitles.SetActiveStream(
          std::min(m_subtitle_index, m_omx_reader.SubtitleStreamCount()-1));
      }
      m_player_subtitles.SetUseExternalSubtitles(false);
    }

    if(m_subtitle_index == -1 && !m_has_external_subtitles)
      m_player_subtitles.SetVisible(false);
  }

  m_omx_reader.GetHints(OMXSTREAM_AUDIO, m_hints_audio);

  if (deviceString == "")
  {
    if (m_BcmHost.vc_tv_hdmi_audio_supported(EDID_AudioFormat_ePCM, 2, EDID_AudioSampleRate_e44KHz, EDID_AudioSampleSize_16bit ) == 0)
      deviceString = "omx:hdmi";
    else
      deviceString = "omx:local";
  }

  if ((m_hints_audio.codec == CODEC_ID_AC3 || m_hints_audio.codec == CODEC_ID_EAC3) &&
      m_BcmHost.vc_tv_hdmi_audio_supported(EDID_AudioFormat_eAC3, 2, EDID_AudioSampleRate_e44KHz, EDID_AudioSampleSize_16bit ) != 0)
    m_passthrough = false;
  if (m_hints_audio.codec == CODEC_ID_DTS &&
      m_BcmHost.vc_tv_hdmi_audio_supported(EDID_AudioFormat_eDTS, 2, EDID_AudioSampleRate_e44KHz, EDID_AudioSampleSize_16bit ) != 0)
    m_passthrough = false;

  if(m_has_audio && !m_player_audio.Open(m_hints_audio, m_av_clock, &m_omx_reader, deviceString, 
                                         m_passthrough, m_use_hw_audio,
                                         m_boost_on_downmix, m_thread_player, m_live, m_layout, audio_queue_size, audio_fifo_size))
    goto do_exit;

  if(m_has_audio)
  {
    m_player_audio.SetVolume(pow(10, m_Volume / 2000.0));
    if (m_Amplification)
      m_player_audio.SetDynamicRangeCompression(m_Amplification);
  }

  if (m_threshold < 0.0f)
    m_threshold = m_live ? 0.7f : 0.2f;

  PrintSubtitleInfo();

  m_av_clock->OMXReset(m_has_video, m_has_audio);
  m_av_clock->OMXStateExecute();
  sentStarted = true;

  while(!m_stop)
  {
    if(g_abort)
      goto do_exit;

    double now = m_av_clock->GetAbsoluteClock();
    bool update = false;
    if (m_last_check_time == 0.0 || m_last_check_time + DVD_MSEC_TO_TIME(20) <= now) 
    {
      update = true;
      m_last_check_time = now;
    }

/*     if (update) {
       OMXControlResult result = m_omxcontrol.getEvent();
       double oldPos, newPos;

    switch(result.getKey())
    {
      case KeyConfig::ACTION_SHOW_INFO:
        m_tv_show_info = !m_tv_show_info;
        vc_tv_show_info(m_tv_show_info);
        break;
      case KeyConfig::ACTION_DECREASE_SPEED:
        if (playspeed_current < playspeed_slow_min || playspeed_current > playspeed_slow_max)
          playspeed_current = playspeed_slow_max-1;
        playspeed_current = std::max(playspeed_current-1, playspeed_slow_min);
        SetSpeed(playspeeds[playspeed_current]);
        DISPLAY_TEXT_SHORT(
          strprintf("Playspeed: %.3f", playspeeds[playspeed_current]/1000.0f));
        printf("Playspeed %.3f\n", playspeeds[playspeed_current]/1000.0f);
        m_Pause = false;
        break;
      case KeyConfig::ACTION_INCREASE_SPEED:
        if (playspeed_current < playspeed_slow_min || playspeed_current > playspeed_slow_max)
          playspeed_current = playspeed_slow_max-1;
        playspeed_current = std::min(playspeed_current+1, playspeed_slow_max);
        SetSpeed(playspeeds[playspeed_current]);
        DISPLAY_TEXT_SHORT(
          strprintf("Playspeed: %.3f", playspeeds[playspeed_current]/1000.0f));
        printf("Playspeed %.3f\n", playspeeds[playspeed_current]/1000.0f);
        m_Pause = false;
        break;
      case KeyConfig::ACTION_REWIND:
        if (playspeed_current >= playspeed_ff_min && playspeed_current <= playspeed_ff_max)
        {
          playspeed_current = playspeed_normal;
          m_seek_flush = true;
        }
        else if (playspeed_current < playspeed_rew_max || playspeed_current > playspeed_rew_min)
          playspeed_current = playspeed_rew_min;
        else
          playspeed_current = std::max(playspeed_current-1, playspeed_rew_max);
        SetSpeed(playspeeds[playspeed_current]);
        DISPLAY_TEXT_SHORT(
          strprintf("Playspeed: %.3f", playspeeds[playspeed_current]/1000.0f));
        printf("Playspeed %.3f\n", playspeeds[playspeed_current]/1000.0f);
        m_Pause = false;
        break;
      case KeyConfig::ACTION_FAST_FORWARD:
        if (playspeed_current >= playspeed_rew_max && playspeed_current <= playspeed_rew_min)
        {
          playspeed_current = playspeed_normal;
          m_seek_flush = true;
        }
        else if (playspeed_current < playspeed_ff_min || playspeed_current > playspeed_ff_max)
          playspeed_current = playspeed_ff_min;
        else
          playspeed_current = std::min(playspeed_current+1, playspeed_ff_max);
        SetSpeed(playspeeds[playspeed_current]);
        DISPLAY_TEXT_SHORT(
          strprintf("Playspeed: %.3f", playspeeds[playspeed_current]/1000.0f));
        printf("Playspeed %.3f\n", playspeeds[playspeed_current]/1000.0f);
        m_Pause = false;
        break;
      case KeyConfig::ACTION_STEP:
        m_av_clock->OMXStep();
        printf("Step\n");
        {
          auto t = (unsigned) (m_av_clock->OMXMediaTime()*1e-3);
          auto dur = m_omx_reader.GetStreamLength() / 1000;
          DISPLAY_TEXT_SHORT(
            strprintf("Step\n%02d:%02d:%02d.%03d / %02d:%02d:%02d",
              (t/3600000), (t/60000)%60, (t/1000)%60, t%1000,
              (dur/3600), (dur/60)%60, dur%60));
        }
        break;
      case KeyConfig::ACTION_PREVIOUS_AUDIO:
        if(m_has_audio)
        {
          int new_index = m_omx_reader.GetAudioIndex() - 1;
          if(new_index >= 0)
          {
            m_omx_reader.SetActiveStream(OMXSTREAM_AUDIO, new_index);
            DISPLAY_TEXT_SHORT(
              strprintf("Audio stream: %d", m_omx_reader.GetAudioIndex() + 1));
          }
        }
        break;
      case KeyConfig::ACTION_NEXT_AUDIO:
        if(m_has_audio)
        {
          m_omx_reader.SetActiveStream(OMXSTREAM_AUDIO, m_omx_reader.GetAudioIndex() + 1);
          DISPLAY_TEXT_SHORT(
            strprintf("Audio stream: %d", m_omx_reader.GetAudioIndex() + 1));
        }
        break;
      case KeyConfig::ACTION_PREVIOUS_CHAPTER:
        if(m_omx_reader.GetChapterCount() > 0)
        {
          m_omx_reader.SeekChapter(m_omx_reader.GetChapter() - 1, &startpts);
          DISPLAY_TEXT_LONG(strprintf("Chapter %d", m_omx_reader.GetChapter()));
          FlushStreams(startpts);
        }
        else
        {
          m_incr = -600.0;
        }
        break;
      case KeyConfig::ACTION_NEXT_CHAPTER:
        if(m_omx_reader.GetChapterCount() > 0)
        {
          m_omx_reader.SeekChapter(m_omx_reader.GetChapter() + 1, &startpts);
          DISPLAY_TEXT_LONG(strprintf("Chapter %d", m_omx_reader.GetChapter()));
          FlushStreams(startpts);
        }
        else
        {
          m_incr = 600.0;
        }
        break;
      case KeyConfig::ACTION_PREVIOUS_SUBTITLE:
        if(m_has_subtitle)
        {
          if(!m_player_subtitles.GetUseExternalSubtitles())
          {
            if (m_player_subtitles.GetActiveStream() == 0)
            {
              if(m_has_external_subtitles)
              {
                DISPLAY_TEXT_SHORT("Subtitle file:\n" + m_external_subtitles_path);
                m_player_subtitles.SetUseExternalSubtitles(true);
              }
            }
            else
            {
              auto new_index = m_player_subtitles.GetActiveStream()-1;
              DISPLAY_TEXT_SHORT(strprintf("Subtitle stream: %d", new_index+1));
              m_player_subtitles.SetActiveStream(new_index);
            }
          }

          m_player_subtitles.SetVisible(true);
          PrintSubtitleInfo();
        }
        break;
      case KeyConfig::ACTION_NEXT_SUBTITLE:
        if(m_has_subtitle)
        {
          if(m_player_subtitles.GetUseExternalSubtitles())
          {
            if(m_omx_reader.SubtitleStreamCount())
            {
              assert(m_player_subtitles.GetActiveStream() == 0);
              DISPLAY_TEXT_SHORT("Subtitle stream: 1");
              m_player_subtitles.SetUseExternalSubtitles(false);
            }
          }
          else
          {
            auto new_index = m_player_subtitles.GetActiveStream()+1;
            if(new_index < (size_t) m_omx_reader.SubtitleStreamCount())
            {
              DISPLAY_TEXT_SHORT(strprintf("Subtitle stream: %d", new_index+1));
              m_player_subtitles.SetActiveStream(new_index);
            }
          }

          m_player_subtitles.SetVisible(true);
          PrintSubtitleInfo();
        }
        break;
      case KeyConfig::ACTION_TOGGLE_SUBTITLE:
        if(m_has_subtitle)
        {
          m_player_subtitles.SetVisible(!m_player_subtitles.GetVisible());
          PrintSubtitleInfo();
        }
        break;
      case KeyConfig::ACTION_DECREASE_SUBTITLE_DELAY:
        if(m_has_subtitle && m_player_subtitles.GetVisible())
        {
          auto new_delay = m_player_subtitles.GetDelay() - 250;
          DISPLAY_TEXT_SHORT(strprintf("Subtitle delay: %d ms", new_delay));
          m_player_subtitles.SetDelay(new_delay);
          PrintSubtitleInfo();
        }
        break;
      case KeyConfig::ACTION_INCREASE_SUBTITLE_DELAY:
        if(m_has_subtitle && m_player_subtitles.GetVisible())
        {
          auto new_delay = m_player_subtitles.GetDelay() + 250;
          DISPLAY_TEXT_SHORT(strprintf("Subtitle delay: %d ms", new_delay));
          m_player_subtitles.SetDelay(new_delay);
          PrintSubtitleInfo();
        }
        break;
      case KeyConfig::ACTION_EXIT:
        m_stop = true;
        goto do_exit;
        break;
      case KeyConfig::ACTION_SEEK_BACK_SMALL:
        if(m_omx_reader.CanSeek()) m_incr = -30.0;
        break;
      case KeyConfig::ACTION_SEEK_FORWARD_SMALL:
        if(m_omx_reader.CanSeek()) m_incr = 30.0;
        break;
      case KeyConfig::ACTION_SEEK_FORWARD_LARGE:
        if(m_omx_reader.CanSeek()) m_incr = 600.0;
        break;
      case KeyConfig::ACTION_SEEK_BACK_LARGE:
        if(m_omx_reader.CanSeek()) m_incr = -600.0;
        break;
      case KeyConfig::ACTION_SEEK_RELATIVE:
          m_incr = result.getArg() * 1e-6;
          break;
      case KeyConfig::ACTION_SEEK_ABSOLUTE:
          newPos = result.getArg() * 1e-6;
          oldPos = m_av_clock->OMXMediaTime()*1e-6;
          m_incr = newPos - oldPos;
          break;
      case KeyConfig::ACTION_PAUSE:
        m_Pause = !m_Pause;
        if (m_av_clock->OMXPlaySpeed() != DVD_PLAYSPEED_NORMAL && m_av_clock->OMXPlaySpeed() != DVD_PLAYSPEED_PAUSE)
        {
          printf("resume\n");
          playspeed_current = playspeed_normal;
          SetSpeed(playspeeds[playspeed_current]);
          m_seek_flush = true;
        }
        if(m_Pause)
        {
          if(m_has_subtitle)
            m_player_subtitles.Pause();

          auto t = (unsigned) (m_av_clock->OMXMediaTime()*1e-6);
          auto dur = m_omx_reader.GetStreamLength() / 1000;
          DISPLAY_TEXT_LONG(strprintf("Pause\n%02d:%02d:%02d / %02d:%02d:%02d",
            (t/3600), (t/60)%60, t%60, (dur/3600), (dur/60)%60, dur%60));
        }
        else
        {
          if(m_has_subtitle)
            m_player_subtitles.Resume();

          auto t = (unsigned) (m_av_clock->OMXMediaTime()*1e-6);
          auto dur = m_omx_reader.GetStreamLength() / 1000;
          DISPLAY_TEXT_SHORT(strprintf("Play\n%02d:%02d:%02d / %02d:%02d:%02d",
            (t/3600), (t/60)%60, t%60, (dur/3600), (dur/60)%60, dur%60));
        }
        break;
      case KeyConfig::ACTION_MOVE_VIDEO:
        sscanf(result.getWinArg(), "%f %f %f %f", &DestRect.x1, &DestRect.y1, &DestRect.x2, &DestRect.y2);
        m_has_video = true;
        m_new_win_pos = true;
        m_seek_flush = true;
        break;
      case KeyConfig::ACTION_HIDE_VIDEO:
        m_has_video = false;
        m_player_video.Close();
        if (m_live)
        {
          m_omx_reader.Close();
          idle = true;
        }
        break;
      case KeyConfig::ACTION_UNHIDE_VIDEO:
        m_has_video = true;
        if (m_live)
        {
          idle = false;
          if(!m_omx_reader.Open(m_filename.c_str(), m_dump_format, true))
            goto do_exit;
        }
        m_new_win_pos = true;
        m_seek_flush = true;
        break;
      case KeyConfig::ACTION_DECREASE_VOLUME:
        m_Volume -= 300;
        m_player_audio.SetVolume(pow(10, m_Volume / 2000.0));
        DISPLAY_TEXT_SHORT(strprintf("Volume: %.2f dB",
          m_Volume / 100.0f));
        printf("Current Volume: %.2fdB\n", m_Volume / 100.0f);
        break;
      case KeyConfig::ACTION_INCREASE_VOLUME:
        m_Volume += 300;
        m_player_audio.SetVolume(pow(10, m_Volume / 2000.0));
        DISPLAY_TEXT_SHORT(strprintf("Volume: %.2f dB",
          m_Volume / 100.0f));
        printf("Current Volume: %.2fdB\n", m_Volume / 100.0f);
        break;
      default:
        break;
    }
    }
*/
    if (idle)
    {
      usleep(10000);
      continue;
    }

    if(m_seek_flush || m_incr != 0)
    {
      double seek_pos     = 0;
      double pts          = 0;

      if(m_has_subtitle)
        m_player_subtitles.Pause();

      pts = m_av_clock->OMXMediaTime();

      seek_pos = (pts ? pts / DVD_TIME_BASE : last_seek_pos) + m_incr;
      last_seek_pos = seek_pos;

      seek_pos *= 1000.0;

      m_incr = 0;

      if(m_omx_reader.SeekTime((int)seek_pos, m_incr < 0.0f, &startpts))
      {
        unsigned t = (unsigned)(startpts*1e-6);
        auto dur = m_omx_reader.GetStreamLength() / 1000;

        if (!m_new_win_pos)
        {
          DISPLAY_TEXT_LONG(strprintf("Seek\n%02d:%02d:%02d / %02d:%02d:%02d",
              (t/3600), (t/60)%60, t%60, (dur/3600), (dur/60)%60, dur%60));
          printf("Seek to: %02d:%02d:%02d\n", (t/3600), (t/60)%60, t%60);
        }
        else
        {
          m_new_win_pos = false;
        }

        FlushStreams(startpts);
      }

      m_player_video.Close();

      sentStarted = false;

      if(m_has_video && !m_player_video.Open(m_hints_video, m_av_clock, DestRect, m_Deinterlace ? VS_DEINTERLACEMODE_FORCE:m_NoDeinterlace ? VS_DEINTERLACEMODE_OFF:VS_DEINTERLACEMODE_AUTO,
                                         m_hdmi_clock_sync, m_thread_player, m_display_aspect, m_layer, video_queue_size, video_fifo_size))
        goto do_exit;

      CLog::Log(LOGDEBUG, "Seeked %.0f %.0f %.0f\n", DVD_MSEC_TO_TIME(seek_pos), startpts, m_av_clock->OMXMediaTime());

      m_av_clock->OMXPause();

      if(m_has_subtitle)
        m_player_subtitles.Resume();
      m_packet_after_seek = false;
      m_seek_flush = false;
    }
    else if(m_packet_after_seek && TRICKPLAY(m_av_clock->OMXPlaySpeed()))
    {
      double seek_pos     = 0;
      double pts          = 0;

      pts = m_av_clock->OMXMediaTime();
      seek_pos = (pts / DVD_TIME_BASE);

      seek_pos *= 1000.0;

      if(m_omx_reader.SeekTime((int)seek_pos, m_av_clock->OMXPlaySpeed() < 0, &startpts))
        ; //FlushStreams(DVD_NOPTS_VALUE);

      CLog::Log(LOGDEBUG, "Seeked %.0f %.0f %.0f\n", DVD_MSEC_TO_TIME(seek_pos), startpts, m_av_clock->OMXMediaTime());

      //unsigned t = (unsigned)(startpts*1e-6);
      unsigned t = (unsigned)(pts*1e-6);
      printf("Seek to: %02d:%02d:%02d\n", (t/3600), (t/60)%60, t%60);
      m_packet_after_seek = false;
    }

    /* player got in an error state */
    if(m_player_audio.Error())
    {
      printf("audio player error. emergency exit!!!\n");
      goto do_exit;
    }

    if (update)
    {
      /* when the video/audio fifos are low, we pause clock, when high we resume */
      double stamp = m_av_clock->OMXMediaTime();
      double audio_pts = m_player_audio.GetCurrentPTS();
      double video_pts = m_player_video.GetCurrentPTS();

      if (0 && m_av_clock->OMXIsPaused())
      {
        double old_stamp = stamp;
        if (audio_pts != DVD_NOPTS_VALUE && (stamp == 0 || audio_pts < stamp))
          stamp = audio_pts;
        if (video_pts != DVD_NOPTS_VALUE && (stamp == 0 || video_pts < stamp))
          stamp = video_pts;
        if (old_stamp != stamp)
        {
          m_av_clock->OMXMediaTime(stamp);
          stamp = m_av_clock->OMXMediaTime();
        }
      }

      float audio_fifo = audio_pts == DVD_NOPTS_VALUE ? 0.0f : audio_pts / DVD_TIME_BASE - stamp * 1e-6;
      float video_fifo = video_pts == DVD_NOPTS_VALUE ? 0.0f : video_pts / DVD_TIME_BASE - stamp * 1e-6;
      float threshold = std::min(0.1f, (float)m_player_audio.GetCacheTotal() * 0.1f);
      bool audio_fifo_low = false, video_fifo_low = false, audio_fifo_high = false, video_fifo_high = false;

      if(m_stats)
      {
        static int count;
        if ((count++ & 7) == 0)
           printf("M:%8.0f V:%6.2fs %6dk/%6dk A:%6.2f %6.02fs/%6.02fs Cv:%6dk Ca:%6dk                            \r", stamp,
               video_fifo, (m_player_video.GetDecoderBufferSize()-m_player_video.GetDecoderFreeSpace())>>10, m_player_video.GetDecoderBufferSize()>>10,
               audio_fifo, m_player_audio.GetDelay(), m_player_audio.GetCacheTotal(),
               m_player_video.GetCached()>>10, m_player_audio.GetCached()>>10);
      }

      if(m_tv_show_info)
      {
        static unsigned count;
        if ((count++ & 7) == 0)
        {
          char response[80];
          if (m_player_video.GetDecoderBufferSize() && m_player_audio.GetCacheTotal())
            vc_gencmd(response, sizeof response, "render_bar 4 video_fifo %d %d %d %d",
                (int)(100.0*m_player_video.GetDecoderBufferSize()-m_player_video.GetDecoderFreeSpace())/m_player_video.GetDecoderBufferSize(),
                (int)(100.0*video_fifo/m_player_audio.GetCacheTotal()),
                0, 100);
          if (m_player_audio.GetCacheTotal())
            vc_gencmd(response, sizeof response, "render_bar 5 audio_fifo %d %d %d %d",
                (int)(100.0*audio_fifo/m_player_audio.GetCacheTotal()),
                (int)(100.0*m_player_audio.GetDelay()/m_player_audio.GetCacheTotal()),
                0, 100);
          vc_gencmd(response, sizeof response, "render_bar 6 video_queue %d %d %d %d",
                m_player_video.GetLevel(), 0, 0, 100);
          vc_gencmd(response, sizeof response, "render_bar 7 audio_queue %d %d %d %d",
                m_player_audio.GetLevel(), 0, 0, 100);
        }
      }

      if (audio_pts != DVD_NOPTS_VALUE)
      {
        audio_fifo_low = m_has_audio && audio_fifo < threshold;
        audio_fifo_high = !m_has_audio || (audio_pts != DVD_NOPTS_VALUE && audio_fifo > m_threshold);
      }
      if (video_pts != DVD_NOPTS_VALUE)
      {
        video_fifo_low = m_has_video && video_fifo < threshold;
        video_fifo_high = !m_has_video || (video_pts != DVD_NOPTS_VALUE && video_fifo > m_threshold);
      }
      CLog::Log(LOGDEBUG, "Normal M:%.0f (A:%.0f V:%.0f) P:%d A:%.2f V:%.2f/T:%.2f (%d,%d,%d,%d) A:%d%% V:%d%% (%.2f,%.2f)\n", stamp, audio_pts, video_pts, m_av_clock->OMXIsPaused(), 
        audio_pts == DVD_NOPTS_VALUE ? 0.0:audio_fifo, video_pts == DVD_NOPTS_VALUE ? 0.0:video_fifo, m_threshold, audio_fifo_low, video_fifo_low, audio_fifo_high, video_fifo_high,
        m_player_audio.GetLevel(), m_player_video.GetLevel(), m_player_audio.GetDelay(), (float)m_player_audio.GetCacheTotal());

      // keep latency under control by adjusting clock (and so resampling audio)
      if (m_live)
      {
        float latency = DVD_NOPTS_VALUE;
        if (m_has_audio && audio_pts != DVD_NOPTS_VALUE)
          latency = audio_fifo;
        else if (!m_has_audio && m_has_video && video_pts != DVD_NOPTS_VALUE)
          latency = video_fifo;
        if (!m_Pause && latency != DVD_NOPTS_VALUE)
        {
          if (m_av_clock->OMXIsPaused())
          {
            if (latency > m_threshold)
            {
              CLog::Log(LOGDEBUG, "Resume %.2f,%.2f (%d,%d,%d,%d) EOF:%d PKT:%p\n", audio_fifo, video_fifo, audio_fifo_low, video_fifo_low, audio_fifo_high, video_fifo_high, m_omx_reader.IsEof(), m_omx_pkt);
              m_av_clock->OMXResume();
              m_latency = latency;
            }
          }
          else
          {
            m_latency = m_latency*0.99f + latency*0.01f;
            float speed = 1.0f;
            if (m_latency < 0.5f*m_threshold)
              speed = 0.990f;
            else if (m_latency < 0.9f*m_threshold)
              speed = 0.999f;
            else if (m_latency > 2.0f*m_threshold)
              speed = 1.010f;
            else if (m_latency > 1.1f*m_threshold)
              speed = 1.001f;

            m_av_clock->OMXSetSpeed(S(speed));
            m_av_clock->OMXSetSpeed(S(speed), true, true);
            CLog::Log(LOGDEBUG, "Live: %.2f (%.2f) S:%.3f T:%.2f\n", m_latency, latency, speed, m_threshold);
          }
        }
      }
      else if(!m_Pause && (m_omx_reader.IsEof() || m_omx_pkt || TRICKPLAY(m_av_clock->OMXPlaySpeed()) || (audio_fifo_high && video_fifo_high)))
      {
        if (m_av_clock->OMXIsPaused())
        {
          CLog::Log(LOGDEBUG, "Resume %.2f,%.2f (%d,%d,%d,%d) EOF:%d PKT:%p\n", audio_fifo, video_fifo, audio_fifo_low, video_fifo_low, audio_fifo_high, video_fifo_high, m_omx_reader.IsEof(), m_omx_pkt);
          m_av_clock->OMXResume();
        }
      }
      else if (m_Pause || audio_fifo_low || video_fifo_low)
      {
        if (!m_av_clock->OMXIsPaused())
        {
          if (!m_Pause)
            m_threshold = std::min(2.0f*m_threshold, 16.0f);
          CLog::Log(LOGDEBUG, "Pause %.2f,%.2f (%d,%d,%d,%d) %.2f\n", audio_fifo, video_fifo, audio_fifo_low, video_fifo_low, audio_fifo_high, video_fifo_high, m_threshold);
          m_av_clock->OMXPause();
        }
      }
    }
    if (!sentStarted)
    {
      CLog::Log(LOGDEBUG, "COMXPlayer::HandleMessages - player started RESET");
      m_av_clock->OMXReset(m_has_video, m_has_audio);
      sentStarted = true;
    }

    if(!m_omx_pkt)
      m_omx_pkt = m_omx_reader.Read();

    if(m_omx_pkt)
      m_send_eos = false;

    if(m_omx_reader.IsEof() && !m_omx_pkt)
    {
      // demuxer EOF, but may have not played out data yet
      if ( (m_has_video && m_player_video.GetCached()) ||
           (m_has_audio && m_player_audio.GetCached()) )
      {
        OMXClock::OMXSleep(10);
        continue;
      }
      if (m_loop)
      {
        m_incr = m_loop_from - (m_av_clock->OMXMediaTime() ? m_av_clock->OMXMediaTime() / DVD_TIME_BASE : last_seek_pos);
        continue;
      }
      if (!m_send_eos && m_has_video)
        m_player_video.SubmitEOS();
      if (!m_send_eos && m_has_audio)
        m_player_audio.SubmitEOS();
      m_send_eos = true;
      if ( (m_has_video && !m_player_video.IsEOS()) ||
           (m_has_audio && !m_player_audio.IsEOS()) )
      {
        OMXClock::OMXSleep(10);
        continue;
      }
      break;
    }

    if(m_has_video && m_omx_pkt && m_omx_reader.IsActive(OMXSTREAM_VIDEO, m_omx_pkt->stream_index))
    {
      if (TRICKPLAY(m_av_clock->OMXPlaySpeed()))
      {
         m_packet_after_seek = true;
      }
      if(m_player_video.AddPacket(m_omx_pkt))
        m_omx_pkt = NULL;
      else
        OMXClock::OMXSleep(10);
    }
    else if(m_has_audio && m_omx_pkt && !TRICKPLAY(m_av_clock->OMXPlaySpeed()) && m_omx_pkt->codec_type == AVMEDIA_TYPE_AUDIO)
    {
      if(m_player_audio.AddPacket(m_omx_pkt))
        m_omx_pkt = NULL;
      else
        OMXClock::OMXSleep(10);
    }
    else if(m_has_subtitle && m_omx_pkt && !TRICKPLAY(m_av_clock->OMXPlaySpeed()) &&
            m_omx_pkt->codec_type == AVMEDIA_TYPE_SUBTITLE)
    {
      auto result = m_player_subtitles.AddPacket(m_omx_pkt,
                      m_omx_reader.GetRelativeIndex(m_omx_pkt->stream_index));
      if (result)
        m_omx_pkt = NULL;
      else
        OMXClock::OMXSleep(10);
    }
    else
    {
      if(m_omx_pkt)
      {
        m_omx_reader.FreePacket(m_omx_pkt);
        m_omx_pkt = NULL;
      }
      else
        OMXClock::OMXSleep(10);
    }
  }

do_exit:
  if (m_stats)
    printf("\n");

  if (m_stop)
  {
    unsigned t = (unsigned)(m_av_clock->OMXMediaTime()*1e-6);
    printf("Stopped at: %02d:%02d:%02d\n", (t/3600), (t/60)%60, t%60);
  }

  if(m_has_video && m_refresh && tv_state.display.hdmi.group && tv_state.display.hdmi.mode)
  {
    m_BcmHost.vc_tv_hdmi_power_on_explicit_new(HDMI_MODE_HDMI, (HDMI_RES_GROUP_T)tv_state.display.hdmi.group, tv_state.display.hdmi.mode);
  }

  m_av_clock->OMXStop();
  m_av_clock->OMXStateIdle();

  m_player_subtitles.Close();
  m_player_video.Close();
  m_player_audio.Close();
  //if (NULL != m_keyboard)
  //{
//    m_keyboard->Close();
//  }

  if(m_omx_pkt)
  {
    m_omx_reader.FreePacket(m_omx_pkt);
    m_omx_pkt = NULL;
  }

  m_omx_reader.Close();

  m_av_clock->OMXDeinitialize();
  if (m_av_clock)
    delete m_av_clock;

  vc_tv_show_info(0);

  g_OMX.Deinitialize();
  g_RBP.Deinitialize();
  
  goto do_next;

  printf("have a nice day ;)\n");
  return 1;
}
