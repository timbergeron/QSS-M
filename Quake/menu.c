/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "quakedef.h"
#include "bgmusic.h"
#include "q_ctype.h" // woods #modsmenu (iw)
#include "q_hash.h"
#include <curl/curl.h> // woods #serversmenu
#include <zlib.h>
#include "json.h" // woods #serversmenu
#include "update.h"
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>

#ifdef USE_CODEC_FLAC
#include <FLAC/format.h>
#endif

#ifdef USE_CODEC_MIKMOD
#include <mikmod.h>
#endif

#ifdef USE_CODEC_OPUS
#include <opus/opus_defines.h>
#include <opus/opusfile.h>
#endif

#ifdef USE_CODEC_VORBIS
#include <vorbis/codec.h>
#endif

#ifdef USE_CODEC_XMP
#include <xmp.h>
#endif

#ifdef USE_CODEC_MP3
#include <mad.h>
#endif

#ifdef _WIN32
#include <sys/types.h>
#include <direct.h>
#else
#include <unistd.h>
#include <dirent.h>
#endif

void (*vid_menucmdfn)(void); //johnfitz
void (*vid_menudrawfn)(void);
void (*vid_menukeyfn)(int key);
void (*vid_menumousefn)(int cx, int cy); // woods #mousemenu

enum m_state_e m_state;
int m_mousex, m_mousey; // woods #mousemenu

const char* ResolveHostname(const char* hostname); // woods #serversmenu
extern qboolean Valid_IP(const char* ip_str); // woods #serversmenu
extern qboolean Valid_Domain(const char* domain_str); // woods #serversmenu
extern cvar_t net_master_ignore;

void M_Menu_Main_f (void);
	void M_Menu_SinglePlayer_f (void);
		void M_Menu_Load_f (void);
		void M_Menu_Save_f (void);
		void M_Menu_Maps_f(void);
		void M_Menu_DownloadMaps_f(void);
		void M_Menu_Skill_f(void);
	void M_Menu_MultiPlayer_f (void);
		void M_Menu_Setup_f (void);
		void M_Menu_NameMaker_f(void); // woods #namemaker
		void M_Menu_Net_f (void);
		void M_Menu_LanConfig_f (void);
		void M_Menu_GameOptions_f (void);
		void M_Menu_Search_f (enum slistScope_e scope);
		void M_Menu_ServerList_f (void);
		void M_Menu_History_f(void); // woods #historymenu
		void M_Menu_Bookmarks_f(void); // woods #bookmarksmenu
		void M_Menu_Bookmarks_Edit_f(void); // woods #bookmarksmenu
	void M_Menu_Options_f (void);
		void M_Menu_Keys_f (void);
		void M_Menu_Mouse_f (void);
		void M_Menu_Controller_f (void);
		void M_Menu_Controller_Test_f (void);
		void M_Menu_WeaponWheel_f (void);
		void M_Menu_Calibration_f (void);
		void M_Menu_Video_f (void);
	void M_Menu_Graphics_f (void);
		void M_Menu_Sky_f (void);
			void M_Menu_Skywind_f (void);
	void M_Menu_Sound_f (void);
		void M_Menu_Voip_f (void);
	void M_Menu_Game_f (void);
		void M_Menu_PlayerXray_f (void);
		void M_Menu_HUD_f (void);
			void M_Menu_Crosshair_f (void);
		void M_Menu_Console_f (void);
		void M_Menu_Startup_f (void);
		void M_Menu_DemoOptions_f (void);
		void M_Menu_PakLoading_f (void);
		void M_Menu_ModelViewer_f (void);
		void M_Menu_ColorPicker_f (void);
		void M_Menu_Extras_f (void);
		void M_Menu_Saving_f (void);
		void M_Menu_Shortcuts_f (void);
		void M_Menu_Version_f (void);
		void M_Menu_ResetConfig_f(void); // woods #resetconfig
	void M_Menu_Mods_f(void); // woods #modsmenu (iw)
	void M_Menu_DownloadMods_f(void);
	void M_Menu_Demos_f (void); // woods #demosmenu
	void M_Menu_Help_f (void);
	void M_Menu_Quit_f (void);

void M_Main_Draw (void);
	void M_SinglePlayer_Draw (void);
		void M_Load_Draw (void);
		void M_Save_Draw (void);
		void M_Maps_Draw(void); // woods #modsmenu (iw)
		void M_DownloadMaps_Draw(void);
		void M_Skill_Draw (void);
	void M_MultiPlayer_Draw (void);
		void M_Setup_Draw (void);
		void M_NameMaker_Draw(void); // woods #namemaker
		void M_Net_Draw (void);
		void M_LanConfig_Draw (void);
		void M_GameOptions_Draw (void);
		void M_Search_Draw (void);
		void M_ServerList_Draw (void);
		void M_History_Draw(void); // woods #historymenu
		void M_Bookmarks_Draw(void); // woods #bookmarksmenu
		void M_Bookmarks_Edit_Draw(void); // woods #bookmarksmenu
	void M_Options_Draw (void);
		void M_Keys_Draw (void);
		void M_Mouse_Draw (void);
		void M_Controller_Draw (void);
		void M_Controller_Test_Draw (void);
		void M_WeaponWheel_Draw (void);
		void M_Calibration_Draw (void);
		void M_Video_Draw (void);
	void M_Graphics_Draw (void);
		void M_Sky_Draw (void);
			void M_Skywind_Draw (void);
	void M_Sound_Draw (void);
		void M_Voip_Draw (void);
	void M_Game_Draw (void);
		void M_PlayerXray_Draw (void);
	void M_HUD_Draw (void);
		void M_Startup_Draw (void);
		void M_DemoOptions_Draw (void);
		void M_PakLoading_Draw (void);
		void M_ModelViewer_Draw (void);
		void M_ColorPicker_Draw (void);
		void M_Extras_Draw (void);
		void M_Saving_Draw (void);
		void M_Shortcuts_Draw (void);
		void M_Version_Draw (void);
		void M_ResetConfig_Draw(void); // woods #resetconfig
			void M_Crosshair_Draw (void);
		void M_Console_Draw (void);
	void M_Mods_Draw(void); // woods #modsmenu (iw)
	void M_DownloadMods_Draw(void);
	void M_Demos_Draw (void); // woods #demosmenu
	void M_Help_Draw (void);
	void M_Quit_Draw (void);

void M_Main_Key (int key);
	void M_SinglePlayer_Key (int key);
		void M_Load_Key (int key);
		void M_Save_Key (int key);
		void M_Maps_Key(int key);
		void M_DownloadMaps_Key(int key);
		void M_Skill_Key(int key);
	void M_MultiPlayer_Key (int key);
		void M_Setup_Key (int key);
		void M_Net_Key (int key);
		void M_LanConfig_Key (int key);
		void M_GameOptions_Key (int key);
		void M_Search_Key (int key);
		void M_ServerList_Key (int key);
		void M_History_Key(int key); // woods #historymenu
		void M_Bookmarks_Key(int key); // woods #bookmarksmenu
		void M_Bookmarks_Edit_Key(int key); // woods #bookmarksmenu
	void M_Options_Key (int key);
		void M_Keys_Key (int key);
		void M_Mouse_Key (int key);
		void M_Controller_Key (int key);
		void M_Controller_Test_Key (int key);
		void M_WeaponWheel_Key (int key);
		void M_Calibration_Key (int key);
		void M_Video_Key (int key);
	void M_Graphics_Key (int key);
		void M_Sky_Key (int key);
		void M_Sky_Char (int key);
		qboolean M_Sky_TextEntry (void);
			void M_Skywind_Key (int key);
	void M_Sound_Key (int key);
		void M_Voip_Key (int key);
	void M_Game_Key (int key);
		void M_PlayerXray_Key (int key);
	void M_HUD_Key (int key);
		void M_Startup_Key (int key);
		void M_DemoOptions_Key (int key);
		void M_PakLoading_Key (int key);
		void M_ModelViewer_Key (int key);
		void M_ColorPicker_Key (int key);
		void M_Extras_Key (int key);
		void M_Saving_Key (int key);
		void M_Shortcuts_Key (int key);
		void M_Version_Key (int key);
		void M_ResetConfig_Key(int key); // woods #resetconfig
			void M_Crosshair_Key (int key);
		void M_Console_Key (int key);
	void M_Mods_Key (int key);
	void M_DownloadMods_Key(int key);
	void M_Demos_Key (int key);
	void M_Demos_Char (int key);
	qboolean M_Demos_TextEntry (void);
	void M_Help_Key (int key);
	void M_Quit_Key (int key);
	void M_NameMaker_Key(int key); // woods #namemaker

	// woods #mousemenu
	
	void M_Main_Mousemove(int cx, int cy);
	void M_SinglePlayer_Mousemove(int cx, int cy);
		void M_Load_Mousemove(int cx, int cy);
		void M_Save_Mousemove(int cx, int cy);
		void M_Maps_Mousemove(int cx, int cy);
		void M_DownloadMaps_Mousemove(int cx, int cy);
			void M_Skill_Mousemove(int cx, int cy);
	void M_MultiPlayer_Mousemove(int cx, int cy);
		void M_Setup_Mousemove(int cx, int cy);
		void M_NameMaker_Mousemove(int cx, int cy);
		void M_Net_Mousemove(int cx, int cy);
		void M_LanConfig_Mousemove(int cx, int cy);
		void M_GameOptions_Mousemove(int cx, int cy);
		//void M_Search_Mousemove (int cx, int cy);
		void M_ServerList_Mousemove(int cx, int cy);
		void M_History_Mousemove(int cx, int cy); // woods #historymenu
		void M_Bookmarks_Mousemove(int cx, int cy); // woods #bookmarksmenu
		void M_Bookmarks_Edit_Mousemove(int cx, int cy); // woods #bookmarksmenu
	void M_Options_Mousemove(int cx, int cy);
		void M_Keys_Mousemove(int cx, int cy);
		void M_Mouse_Mousemove (int cx, int cy);
		void M_Controller_Mousemove (int cx, int cy);
		void M_WeaponWheel_Mousemove (int cx, int cy);
		void M_Video_Mousemove (int cx, int cy);
		void M_Graphics_Mousemove (int cx, int cy);
			void M_Sky_Mousemove (int cx, int cy);
				void M_Skywind_Mousemove (int cx, int cy);
		void M_Sound_Mousemove (int cx, int cy);
			void M_Voip_Mousemove (int cx, int cy);
		void M_Game_Mousemove (int cx, int cy);
		void M_PlayerXray_Mousemove (int cx, int cy);
		void M_HUD_Mousemove (int cx, int cy);
			void M_Crosshair_Mousemove (int cx, int cy);
		void M_Console_Mousemove (int cx, int cy);
		void M_Startup_Mousemove (int cx, int cy);
		void M_DemoOptions_Mousemove (int cx, int cy);
		void M_PakLoading_Mousemove (int cx, int cy);
		void M_ModelViewer_Mousemove(int cx, int cy);
		void M_ColorPicker_Mousemove(int cx, int cy);
		void M_Extras_Mousemove(int cx, int cy);
		void M_Saving_Mousemove(int cx, int cy);
		void M_Shortcuts_Mousemove(int cx, int cy);
		void M_Version_Mousemove(int cx, int cy);
		void M_ResetConfig_Mousemove(int cx, int cy); // woods #resetconfig
	//void M_Gamepad_Mousemove (int cx, int cy);
	void M_Mods_Mousemove(int cx, int cy);
	void M_DownloadMods_Mousemove(int cx, int cy);
	void M_Demos_Mousemove(int cx, int cy);
	//void M_Help_Mousemove (int cx, int cy);
	//void M_Quit_Mousemove (int cx, int cy);

qboolean	m_entersound;		// play after drawing a frame, so caching
								// won't disrupt the sound
qboolean	m_recursiveDraw;

enum m_state_e	m_return_state;
qboolean	m_return_onerror;
char		m_return_reason [32];

#define StartingGame	(m_multiplayer_cursor == 1)
#define JoiningGame		(m_multiplayer_cursor == 0)
//#define	IPXConfig		(m_net_cursor == 1) // woods #skipipx
#define	TCPIPConfig		(m_net_cursor == 0)

void M_ConfigureNetSubsystem(void);
void M_SetSkillMenuMap(const char* name); // woods #skillmenu (iw)
static void M_GameOptions_ClearTypedLevel(void);

void FileList_Subtract(const char* name, filelist_item_t** list); // woods #historymenu
void FileList_Add(const char* name, const char* data, filelist_item_t** list);

static qboolean has_custom_progs = false; // woods #botdetect
qboolean progs_check_done = false; // woods #botdetect

//=============================================================================
// Live Preview - ported from Ironwail's ui_live_preview.
// Option draw code reports the selected previewable item each frame, but the
// preview only starts when the option is actually changed. Console options
// additionally drop the console so their visual cvars can be seen live.
//=============================================================================

cvar_t ui_live_preview = {"ui_live_preview", "1", CVAR_ARCHIVE};
extern cvar_t gl_cshiftpercent;

typedef enum {
	LP_NONE = -1,
	LP_BRIGHTNESS,
	LP_CONTRAST,
	LP_FILTERING,
	LP_MODELLERP,
	LP_RENDERSCALE,
	LP_CLASSICPARTICLES,
	LP_GRAPHICS,
	LP_SKY,
	LP_HUDSCALE,
	LP_SCRSIZE,
	LP_SBALPHA,
	LP_SBARSTYLE,
	LP_FOV,
	LP_CSHIFT,
	LP_DAMAGETINT,
	LP_VIEWMODEL,
	LP_POWERUPSHELLS,
	LP_ITEMBOB,
	LP_PONG,
	LP_HINTS,
	LP_DEMOBAR,
	LP_CONFONT,
	LP_CONHEIGHT,
	LP_CONSPEED,
	LP_CONALPHA,
	LP_CONBACK,
	LP_CONCOLOR,
	LP_CONTYPING,
	LP_MATCHSCORES,
	LP_SHOWSPEED,
	LP_SHOWSCORES,
	LP_MOVEKEYS,
	LP_COUNT
} livepreview_id_t;

#define LP_FADEIN_TIME		0.125f
#define LP_FADEOUT_TIME		0.125f
#define LP_HOLD_TIME		1.25f
#define LP_LONG_HOLD_TIME	2.25f
#define LP_PONG_HOLD_TIME	4.0f
#define LP_HINTS_HOLD_TIME	4.0f
#define LP_DEMOBAR_HOLD_TIME	4.0f
#define LP_DEMOBAR_MARGIN_TIME	1.25f
#define LP_CONSPEED_HOLD_TIME	4.0f
#define LP_CONSPEED_CYCLE_TIME	0.85f

static struct {
	int		id;
	int		selection_id;
	int		selection_state;
	int		y;
	int		selection_y;
	float	frac;
	float	frac_target;
	float	hold_time;
	float	console_speed_cycle_start;
	qboolean	hold_paused;
} livepreview = { LP_NONE, LP_NONE, m_none, 0, 0, 0.f, 0.f, 0.f, 0.f, false };

static qboolean lp_user_pinned;
qboolean slider_grab; // woods #mousemenu
static qboolean graphics_slider_grab;
static qboolean game_slider_grab;
static qboolean hud_slider_grab;
static qboolean console_slider_grab;
static qboolean sky_slider_grab;
static qboolean skywind_slider_grab;
static qboolean demooptions_slider_grab;

static float M_LivePreview_HoldTimeForId (int id)
{
	switch (id) {
	case LP_CONSPEED:
		return LP_CONSPEED_HOLD_TIME;
	case LP_PONG:
		return LP_PONG_HOLD_TIME;
	case LP_HINTS:
		return LP_HINTS_HOLD_TIME;
	case LP_DEMOBAR:
		if (scr_demobar_timeout.value > 0.f)
			return scr_demobar_timeout.value + LP_DEMOBAR_MARGIN_TIME;
		return LP_DEMOBAR_HOLD_TIME;
	case LP_CONALPHA:
		return LP_LONG_HOLD_TIME;
	default:
		return LP_HOLD_TIME;
	}
}

static qboolean M_LivePreview_GameAvailable (void)
{
	return (cls.state == ca_connected && cls.signon == SIGNONS);
}

static qboolean M_LivePreview_HoldPaused (void)
{
	if (lp_user_pinned)
		return true;

	switch (m_state)
	{
	case m_options:
		return slider_grab;
	case m_graphics:
		return graphics_slider_grab;
	case m_game:
		return game_slider_grab;
	case m_hud:
		return hud_slider_grab;
	case m_console:
		return console_slider_grab;
	case m_sky:
		return sky_slider_grab;
	case m_skywind:
		return skywind_slider_grab;
	case m_demooptions:
		return demooptions_slider_grab;
	default:
		return false;
	}
}

static void M_LivePreview_ClearSelection (void)
{
	lp_user_pinned = false;
	livepreview.selection_id = LP_NONE;
	livepreview.selection_y = 0;
	livepreview.selection_state = m_state;
}

static void M_LivePreview_Stop (qboolean clear_selection)
{
	if (clear_selection)
		M_LivePreview_ClearSelection ();

	livepreview.frac_target = 0.f;
	livepreview.hold_time = 0.f;
}

static void M_LivePreview_StopImmediate (qboolean clear_selection)
{
	if (clear_selection)
		M_LivePreview_ClearSelection ();

	livepreview.id = LP_NONE;
	livepreview.y = 0;
	livepreview.frac = 0.f;
	livepreview.frac_target = 0.f;
	livepreview.hold_time = 0.f;
	livepreview.console_speed_cycle_start = 0.f;
	livepreview.hold_paused = false;
	lp_user_pinned = false;
}

static qboolean M_LivePreview_ValidateFrame (void)
{
	if (livepreview.selection_state != m_state)
	{
		M_LivePreview_StopImmediate (true);
		return false;
	}

	if (!ui_live_preview.value || key_dest != key_menu)
	{
		M_LivePreview_StopImmediate (true);
		return false;
	}

	if (livepreview.id != LP_NONE && !M_LivePreview_GameAvailable ())
	{
		M_LivePreview_StopImmediate (false);
		return false;
	}

	return true;
}

static void M_LivePreview_Want (int id)
{
	if (id < LP_NONE || id >= LP_COUNT)
		id = LP_NONE;

	if (livepreview.selection_id == id)
		return;

	livepreview.selection_id = id;
	if (livepreview.id != LP_NONE && livepreview.id != id)
		M_LivePreview_Stop (false);
}

static void M_LivePreview_WantAt (int id, int y)
{
	if (id < LP_NONE || id >= LP_COUNT)
		id = LP_NONE;

	if (livepreview.selection_state != m_state)
		M_LivePreview_ClearSelection ();

	if (id != LP_NONE)
	{
		if (livepreview.selection_id == id &&
			livepreview.selection_y != y &&
			livepreview.id == id &&
			livepreview.y != y)
		{
			livepreview.frac_target = 0.f;
			livepreview.hold_time = 0.f;
		}
		livepreview.selection_y = y;
	}
	else
	{
		livepreview.selection_y = 0;
	}

	M_LivePreview_Want (id);
}

static void M_LivePreview_WantAndKick (int id, int y)
{
	M_LivePreview_WantAt (id, y);
	M_LivePreview_Kick ();
}

static void M_LivePreview_UpdateUserPin (void)
{
	lp_user_pinned = (keydown[K_SHIFT] &&
					  livepreview.selection_id != LP_NONE &&
					  livepreview.selection_state == m_state &&
					  ui_live_preview.value &&
					  key_dest == key_menu &&
					  M_LivePreview_GameAvailable ());
	if (!lp_user_pinned)
		return;

	if (livepreview.id != livepreview.selection_id ||
		livepreview.y != livepreview.selection_y ||
		livepreview.frac_target == 0.f)
		M_LivePreview_Kick ();

	if (livepreview.id != LP_NONE)
	{
		livepreview.frac_target = 1.f;
		livepreview.hold_time = 0.f;
	}
}

void M_LivePreview_Kick (void)
{
	int id = livepreview.selection_id;
	qboolean new_id, restarting;

	if (!ui_live_preview.value || key_dest != key_menu)
		id = LP_NONE;

	// Ironwail only reveals the world for live preview while a level is active.
	if (id != LP_NONE && !M_LivePreview_GameAvailable ())
		id = LP_NONE;

	if (id == LP_NONE)
	{
		M_LivePreview_Stop (false);
		return;
	}

	new_id = (livepreview.id != id);
	restarting = (livepreview.frac_target == 0.f);
	livepreview.id = id;
	livepreview.y = livepreview.selection_y;
	if (id == LP_CONSPEED && (new_id || restarting))
		livepreview.console_speed_cycle_start = realtime;
	livepreview.hold_time = M_LivePreview_HoldTimeForId (livepreview.id);
	livepreview.frac_target = 1.f;
	if (id == LP_DEMOBAR)
		SCR_ShowDemoBarFor (livepreview.hold_time);
}

void M_LivePreview_Reset (void)
{
	livepreview.id = LP_NONE;
	livepreview.selection_id = LP_NONE;
	livepreview.selection_state = m_none;
	livepreview.y = 0;
	livepreview.selection_y = 0;
	livepreview.frac = 0.f;
	livepreview.frac_target = 0.f;
	livepreview.hold_time = 0.f;
	livepreview.console_speed_cycle_start = 0.f;
	livepreview.hold_paused = false;
	lp_user_pinned = false;
}

float M_LivePreview_Alpha (void)
{
	return livepreview.frac;
}

static qboolean M_LivePreview_IsConsoleId (int id)
{
	switch (id)
	{
	case LP_CONFONT:
	case LP_CONHEIGHT:
	case LP_CONSPEED:
	case LP_CONALPHA:
	case LP_CONBACK:
	case LP_CONCOLOR:
	case LP_CONTYPING:
		return true;
	default:
		return false;
	}
}

qboolean M_WantsConsole (float *alpha)
{
	qboolean want = (key_dest == key_menu &&
					 M_LivePreview_GameAvailable () &&
					 M_LivePreview_IsConsoleId (livepreview.id) &&
					 livepreview.frac > 0.f);
	if (alpha)
		*alpha = want ? livepreview.frac : 0.f;
	return want;
}

qboolean M_LivePreview_UseConsoleHeight (void)
{
	return M_WantsConsole (NULL);
}

qboolean M_LivePreview_UseConsoleSpeed (void)
{
	return (key_dest == key_menu &&
			M_LivePreview_GameAvailable () &&
			livepreview.id == LP_CONSPEED &&
			livepreview.frac > 0.f);
}

qboolean M_LivePreview_ConsoleSpeedOpen (void)
{
	float elapsed;
	int phase;

	if (!M_LivePreview_UseConsoleSpeed ())
		return true;

	elapsed = realtime - livepreview.console_speed_cycle_start;
	if (elapsed < 0.f)
		elapsed = 0.f;
	phase = (int)(elapsed / LP_CONSPEED_CYCLE_TIME);
	return (phase & 1) == 0;
}

qboolean M_LivePreview_UseDamageTint (void)
{
	return (key_dest == key_menu &&
			M_LivePreview_GameAvailable () &&
			livepreview.id == LP_DAMAGETINT &&
			livepreview.frac > 0.f);
}

qboolean M_LivePreview_UsePong (void)
{
	return (key_dest == key_menu &&
			M_LivePreview_GameAvailable () &&
			!cls.demoplayback &&
			livepreview.id == LP_PONG &&
			livepreview.frac > 0.f);
}

qboolean M_LivePreview_UsePowerupShells (void)
{
	return (key_dest == key_menu &&
			M_LivePreview_GameAvailable () &&
			livepreview.id == LP_POWERUPSHELLS &&
			livepreview.frac > 0.f);
}

qboolean M_LivePreview_UsePausedHints (void)
{
	return (key_dest == key_menu &&
			M_LivePreview_GameAvailable () &&
			!cls.demoplayback &&
			livepreview.id == LP_HINTS &&
			livepreview.frac > 0.f);
}

qboolean M_LivePreview_UseTypingStatus (void)
{
	return (key_dest == key_menu &&
			M_LivePreview_GameAvailable () &&
			livepreview.id == LP_CONTYPING &&
			livepreview.frac > 0.f);
}

qboolean M_LivePreview_UseMatchScores (void)
{
	return (key_dest == key_menu &&
			M_LivePreview_GameAvailable () &&
			livepreview.id == LP_MATCHSCORES &&
			livepreview.frac > 0.f);
}

qboolean M_LivePreview_UseSpeed (void)
{
	return (key_dest == key_menu &&
			M_LivePreview_GameAvailable () &&
			livepreview.id == LP_SHOWSPEED &&
			livepreview.frac > 0.f);
}

qboolean M_LivePreview_UseScores (void)
{
	return (key_dest == key_menu &&
			M_LivePreview_GameAvailable () &&
			livepreview.id == LP_SHOWSCORES &&
			livepreview.frac > 0.f);
}

qboolean M_LivePreview_UseMovementKeys (void)
{
	return (key_dest == key_menu &&
			M_LivePreview_GameAvailable () &&
			livepreview.id == LP_MOVEKEYS &&
			livepreview.frac > 0.f);
}

static qboolean M_LivePreview_IsolateY (int y)
{
	return (livepreview.id != LP_NONE &&
			livepreview.frac > 0.f &&
			livepreview.y == y);
}

static void M_LivePreview_DrawColorOverlay (float r, float g, float b, float alpha)
{
	alpha = CLAMP (0.f, alpha, 1.f);
	if (alpha <= 0.f)
		return;

	GL_SetCanvas (CANVAS_DEFAULT);
	glEnable (GL_BLEND);
	glDisable (GL_ALPHA_TEST);
	glDisable (GL_TEXTURE_2D);
	glColor4f (r, g, b, alpha);
	glBegin (GL_QUADS);
	glVertex2f (0, 0);
	glVertex2f (glwidth, 0);
	glVertex2f (glwidth, glheight);
	glVertex2f (0, glheight);
	glEnd ();
	glColor4f (1.f, 1.f, 1.f, 1.f);
	glEnable (GL_TEXTURE_2D);
	glEnable (GL_ALPHA_TEST);
	glDisable (GL_BLEND);
}

static void M_LivePreview_DrawEffects (void)
{
	float alpha;

	if (livepreview.id == LP_NONE || livepreview.frac <= 0.f)
		return;

	switch (livepreview.id)
	{
	case LP_CSHIFT:
		alpha = (150.f * CLAMP (0.f, gl_cshiftpercent.value, 100.f) / 100.f) / 255.f;
		M_LivePreview_DrawColorOverlay (1.f, 0.f, 0.f, alpha * livepreview.frac);
		break;
	default:
		break;
	}
}

// While the menu is faded for preview, the previewed line should stay at full
// alpha so the user can see which option produced the live change.
// Submenu draw fns bracket the previewed item's draws with these.
static int lp_isolate_depth = 0;
static float lp_isolate_prev_alpha = 1.f;

void M_LivePreview_BeginIsolate (void)
{
	if (lp_isolate_depth > 0)
	{
		lp_isolate_depth++;
		return;
	}

	if (gl_menu_alpha < 1.f)
	{
		lp_isolate_depth = 1;
		lp_isolate_prev_alpha = gl_menu_alpha;
		// gl_draw.c multiplies primitives by gl_menu_alpha during preview;
		// raising it here temporarily opts the isolated row back into normal
		// draw-state restoration while this bracket is active.
		gl_menu_alpha = 1.f;
		glColor4f (1.f, 1.f, 1.f, 1.f);
	}
}

void M_LivePreview_EndIsolate (void)
{
	if (lp_isolate_depth > 0 && --lp_isolate_depth == 0)
	{
		gl_menu_alpha = lp_isolate_prev_alpha;
		glEnable (GL_BLEND);
		glDisable (GL_ALPHA_TEST);
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glColor4f (1.f, 1.f, 1.f, gl_menu_alpha);
	}
}

static void M_LivePreview_Update (void)
{
	qboolean hold_paused;

	if (!M_LivePreview_ValidateFrame ())
		return;

	M_LivePreview_UpdateUserPin ();

	hold_paused = (livepreview.id >= 0 && livepreview.frac_target == 1.f) ?
		M_LivePreview_HoldPaused () : false;
	if (livepreview.hold_paused && !hold_paused &&
		livepreview.id >= 0 &&
		livepreview.frac_target == 1.f &&
		livepreview.frac > 0.f)
		livepreview.hold_time = M_LivePreview_HoldTimeForId (livepreview.id);
	livepreview.hold_paused = hold_paused;

	// Hold timer counts down only while we're fully faded in.
	if (livepreview.id >= 0 &&
		livepreview.frac >= livepreview.frac_target &&
		livepreview.frac_target == 1.f &&
		livepreview.hold_time > 0.f &&
		!hold_paused)
	{
		livepreview.hold_time -= host_frametime;
		if (livepreview.hold_time <= 0.f)
		{
			livepreview.hold_time = 0.f;
			livepreview.frac_target = 0.f;
		}
	}

	// Advance fade toward target.
	if (livepreview.frac < livepreview.frac_target)
	{
		livepreview.frac += host_frametime / LP_FADEIN_TIME;
		if (livepreview.frac > livepreview.frac_target)
			livepreview.frac = livepreview.frac_target;
	}
	else if (livepreview.frac > livepreview.frac_target)
	{
		livepreview.frac -= host_frametime / LP_FADEOUT_TIME;
		if (livepreview.frac < livepreview.frac_target)
			livepreview.frac = livepreview.frac_target;
		if (livepreview.frac == 0.f && livepreview.frac_target == 0.f)
			livepreview.id = LP_NONE;
	}
}

static float M_LivePreview_EaseInOut (float t)
{
	t = CLAMP (0.f, t, 1.f);
	return t * t * (3.f - 2.f * t);
}

static void M_LivePreview_DrawFadeScreen (void)
{
	vrect_t bounds, viewport;
	float center, frac, s, y0, y1;

	if (livepreview.id == LP_NONE || livepreview.frac <= 0.f)
		return;

	Draw_GetMenuTransform (&bounds, &viewport);
	// livepreview.y is in menu-canvas coordinates. Draw_GetMenuTransform gives
	// the active menu viewport in GL pixels, so scale from the 320x200-style
	// bounds into that viewport before drawing the default-canvas fade band.
	s = (float)viewport.height / (float)bounds.height;
	center = (viewport.y - gly) + (livepreview.y + CHARSIZE / 2) * s;
	frac = M_LivePreview_EaseInOut (livepreview.frac);
	y0 = LERP (0.f, center - CHARSIZE * s, frac);
	y1 = LERP ((float)glheight, center + CHARSIZE * s, frac);

	Draw_FadeScreen_Rect_Alpha (0.f, y0, (float)glwidth, y1, 0.5f);
}

/*
================
M_DrawCharacter

Draws one solid graphics character
================
*/
void M_DrawCharacter (int cx, int line, int num)
{
	Draw_Character (cx, line, num);
}

void M_DrawArrowCursor(int cx, int cy) // woods #skillmenu (iw)
{
	M_DrawCharacter(cx, cy, 12 + ((int)(realtime * 4) & 1));
}

void M_Print (int cx, int cy, const char *str)
{
	while (*str)
	{
		M_DrawCharacter (cx, cy, (*str)+128);
		str++;
		cx += 8;
	}
}

void M_DrawCharacterRGBA (int cx, int line, int num, plcolour_t c, float alpha) // woods
{
	Draw_CharacterRGBA (cx, line, num, c, alpha);
}

void M_PrintRGBA (int cx, int cy, const char* str, plcolour_t c, float alpha, qboolean mask) // woods
{
	while (*str)
	{
		if (mask)
			M_DrawCharacterRGBA(cx, cy, (*str) + 128, c, alpha);  // Add 128 for masked version
		else
			M_DrawCharacterRGBA(cx, cy, (*str), c, alpha);
		str++;
		cx += 8;
	}
}

void M_Print2 (int cx, int cy, const char* str) // woods #speed yellow/gold numbers
{
	while (*str)
	{
		M_DrawCharacter(cx, cy, (*str) -30);
		str++;
		cx += 8;
	}
}

void M_PrintWhite (int cx, int cy, const char *str)
{
	while (*str)
	{
		M_DrawCharacter (cx, cy, *str);
		str++;
		cx += 8;
	}
}

void M_DrawTransPic (int x, int y, qpic_t *pic)
{
	Draw_Pic (x, y, pic); //johnfitz -- simplified becuase centering is handled elsewhere
}

void M_DrawPic (int x, int y, qpic_t *pic)
{
	Draw_Pic (x, y, pic); //johnfitz -- simplified becuase centering is handled elsewhere
}

void M_DrawSubpic (int x, int y, qpic_t* pic, int left, int top, int width, int height) // woods #modsmenu (iw)
{
	float s1 = left / (float)pic->width;
	float t1 = top / (float)pic->height;
	float s2 = width / (float)pic->width;
	float t2 = height / (float)pic->height;
	Draw_SubPic (x, y, width, height, pic, s1, t1, s2, t2);
}

void M_DrawTransPicTranslate (int x, int y, qpic_t *pic, plcolour_t top, plcolour_t bottom) //johnfitz -- more parameters
{
	Draw_TransPicTranslate (x, y, pic, top, bottom); //johnfitz -- simplified becuase centering is handled elsewhere
}

void M_DrawTextBox (int x, int y, int width, int lines)
{
	qpic_t	*p;
	int		cx, cy;
	int		n;

	// draw left side
	cx = x;
	cy = y;
	p = Draw_CachePic ("gfx/box_tl.lmp");
	M_DrawTransPic (cx, cy, p);
	p = Draw_CachePic ("gfx/box_ml.lmp");
	for (n = 0; n < lines; n++)
	{
		cy += 8;
		M_DrawTransPic (cx, cy, p);
	}
	p = Draw_CachePic ("gfx/box_bl.lmp");
	M_DrawTransPic (cx, cy+8, p);

	// draw middle
	cx += 8;
	while (width > 0)
	{
		cy = y;
		p = Draw_CachePic ("gfx/box_tm.lmp");
		M_DrawTransPic (cx, cy, p);
		p = Draw_CachePic ("gfx/box_mm.lmp");
		for (n = 0; n < lines; n++)
		{
			cy += 8;
			if (n == 1)
				p = Draw_CachePic ("gfx/box_mm2.lmp");
			M_DrawTransPic (cx, cy, p);
		}
		p = Draw_CachePic ("gfx/box_bm.lmp");
		M_DrawTransPic (cx, cy+8, p);
		width -= 2;
		cx += 16;
	}

	// draw right side
	cy = y;
	p = Draw_CachePic ("gfx/box_tr.lmp");
	M_DrawTransPic (cx, cy, p);
	p = Draw_CachePic ("gfx/box_mr.lmp");
	for (n = 0; n < lines; n++)
	{
		cy += 8;
		M_DrawTransPic (cx, cy, p);
	}
	p = Draw_CachePic ("gfx/box_br.lmp");
	M_DrawTransPic (cx, cy+8, p);
}

void M_DrawTextBox_WithAlpha (int x, int y, int width, int lines, float alpha) // woods #centerprintbg (iw)
{
	qpic_t* p;
	int cx, cy;
	int n;
	float outlineThickness = 0.0f;
	plcolour_t imageColor = CL_PLColours_Parse("0xffffff");

	// draw left side
	cx = x;
	cy = y;
	p = Draw_CachePic("gfx/box_tl.lmp");
	Draw_Pic_RGBA_Outline(cx, cy, p, imageColor, alpha, outlineThickness);

	p = Draw_CachePic("gfx/box_ml.lmp");
	for (n = 0; n < lines; n++)
	{
		cy += 8;
		Draw_Pic_RGBA_Outline(cx, cy, p, imageColor, alpha, outlineThickness);
	}
	p = Draw_CachePic("gfx/box_bl.lmp");
	Draw_Pic_RGBA_Outline(cx, cy + 8, p, imageColor, alpha, outlineThickness);

	// draw middle
	cx += 8;
	while (width > 0)
	{
		cy = y;
		p = Draw_CachePic("gfx/box_tm.lmp");
		Draw_Pic_RGBA_Outline(cx, cy, p, imageColor, alpha, outlineThickness);

		p = Draw_CachePic("gfx/box_mm.lmp");
		for (n = 0; n < lines; n++)
		{
			cy += 8;
			if (n == 1)
				p = Draw_CachePic("gfx/box_mm2.lmp");
			Draw_Pic_RGBA_Outline(cx, cy, p, imageColor, alpha, outlineThickness);
		}
		p = Draw_CachePic("gfx/box_bm.lmp");
		Draw_Pic_RGBA_Outline(cx, cy + 8, p, imageColor, alpha, outlineThickness);
		width -= 2;
		cx += 16;
	}

	// draw right side
	cy = y;
	p = Draw_CachePic("gfx/box_tr.lmp");
	Draw_Pic_RGBA_Outline(cx, cy, p, imageColor, alpha, outlineThickness);

	p = Draw_CachePic("gfx/box_mr.lmp");
	for (n = 0; n < lines; n++)
	{
		cy += 8;
		Draw_Pic_RGBA_Outline(cx, cy, p, imageColor, alpha, outlineThickness);
	}
	p = Draw_CachePic("gfx/box_br.lmp");
	Draw_Pic_RGBA_Outline(cx, cy + 8, p, imageColor, alpha, outlineThickness);
}

void M_DrawQuakeCursor(int cx, int cy) // woods #skillmenu (iw)
{
	qpic_t* pic = Draw_CachePic(va("gfx/menudot%i.lmp", (int)(realtime * 10) % 6 + 1));
	M_DrawTransPic(cx, cy, pic);
}


void M_DrawQuakeBar(int x, int y, int cols) // woods #modsmenu (iw)
{
	M_DrawCharacter(x, y, '\35');
	x += 8;
	cols -= 2;
	while (cols-- > 0)
	{
		M_DrawCharacter(x, y, '\36');
		x += 8;
	}
	M_DrawCharacter(x, y, '\37');
}

static void M_DrawCountHeader(int x, int y, int cols, const char *title,
	int count, const char *singular, const char *plural)
{
	char count_text[64];

	q_snprintf(count_text, sizeof(count_text), "%d %s",
		count, count == 1 ? singular : plural);
	Draw_String(x, y, title);
	Draw_String(x + cols * 8 - (int)strlen(count_text) * 8, y, count_text);
}

void M_DrawEllipsisBar(int x, int y, int cols) // woods #modsmenu (iw)
{
	while (cols > 0)
	{
		M_DrawCharacter(x, y, '.' | 128);
		cols -= 2;
		x += 16;
	}
}

//=============================================================================
/* Scrolling ticker -- woods #modsmenu #demosmenu (iw)*/ 

typedef struct
{
	double			scroll_time;
	double			scroll_wait_time;
} menuticker_t;

static void M_Ticker_Init(menuticker_t* ticker)
{
	ticker->scroll_time = 0.0;
	ticker->scroll_wait_time = 1.0;
}

static void M_Ticker_Update(menuticker_t* ticker)
{
	if (ticker->scroll_wait_time <= 0.0)
		ticker->scroll_time += host_frametime;
	else
		ticker->scroll_wait_time = q_max(0.0, ticker->scroll_wait_time - host_frametime);
}

static qboolean M_Ticker_Key(menuticker_t* ticker, int key)
{
	switch (key)
	{
	case K_RIGHTARROW:
		ticker->scroll_time += 0.25;
		ticker->scroll_wait_time = 1.5;
		S_LocalSound("misc/menu3.wav");
		return true;

	case K_LEFTARROW:
		ticker->scroll_time -= 0.25;
		ticker->scroll_wait_time = 1.5;
		S_LocalSound("misc/menu3.wav");
		return true;

	default:
		return false;
	}
}

void M_PrintHighlight(int x, int y, const char* str, const char* search, int searchlen)
{
	if (!searchlen)
	{
		M_Print(x, y, str);
		return;
	}

	const char* match = q_strcasestr(str, search);
	if (!match)
	{
		M_Print(x, y, str);
		return;
	}

	// Print part before match
	int pos = match - str;
	int i;
	for (i = 0; i < pos; i++)
		M_DrawCharacter(x + i * 8, y, str[i] ^ 128);

	for (i = 0; i < searchlen && match[i]; i++) // Print matching part highlighted
		M_DrawCharacter(x + (pos + i) * 8, y, match[i]);

	for (i = 0; match[i + searchlen]; i++) // Print rest normally
		M_DrawCharacter(x + (pos + searchlen + i) * 8, y, match[i + searchlen] ^ 128);
}

static float M_ScrollPixelOffset(double time, int scrollspeed, int cycle_pixels, int *pixel_offset)
{
	double offset;

	if (cycle_pixels <= 0)
	{
		*pixel_offset = 0;
		return 0.0f;
	}

	offset = time * (double)scrollspeed;
	offset -= floor(offset / (double)cycle_pixels) * (double)cycle_pixels;

	*pixel_offset = (int)offset;
	return (float)(offset - *pixel_offset);
}

void M_PrintScroll(int x, int y, int maxwidth, const char* str, double time, qboolean color) // woods #modsmenu (iw)
{
	const int charwidth = 8;
	const int gap_len = 5;
	const int scrollspeed = 30; // pixels per second, matches Sbar_DrawScrollString
	int maxchars = maxwidth / charwidth;
	int len = strlen(str);
	char mask = color ? 128 : 0;
	float frac;

	if (len <= maxchars)
	{
		if (color)
			M_Print(x, y, str);
		else
			M_PrintWhite(x, y, str);
		return;
	}

	if (!len)
		return;

	int total_chars = len + gap_len;
	int cycle_pixels = total_chars * charwidth;
	int pixel_offset;
	frac = M_ScrollPixelOffset(time, scrollspeed, cycle_pixels, &pixel_offset);

	glPushMatrix();
	glTranslatef(-frac, 0.0f, 0.0f);
	for (int pass = 0; pass < 2; ++pass)
	{
		int base_x = x - pixel_offset + pass * cycle_pixels;
		for (int pos = 0; pos < total_chars; ++pos)
		{
			int char_x = base_x + pos * charwidth;

			if (char_x + charwidth <= x)
				continue;
			if (char_x >= x + maxwidth)
				break;

			int ch;
			if (pos < len)
				ch = (unsigned char)str[pos];
			else
				ch = (unsigned char)" /// "[pos - len];

			M_DrawCharacter(char_x, y, ch ^ mask);
		}
	}
	glPopMatrix();
}

void M_PrintScroll2(int x, int y, int maxwidth, const char* str, const char* str2, double time, qboolean name_red)
{
	int maxchars = maxwidth / 8;
	int len_str = (int)strlen(str);

	// Determine effective name length based on scroll state
	int effective_len_str = (time != 0.0) ? len_str : q_min(len_str, 12);

	// Create masked version of name
	char masked_str[MAX_QPATH];
	char mask = name_red ? 128 : 0;
	for (int i = 0; i < effective_len_str; i++)
		masked_str[i] = (char)(str[i] ^ mask);
	masked_str[effective_len_str] = '\0';

	// Calculate padding width (capped at 13)
	int padding_width = q_min(max_word_length + 1, 13);

	// Build combined string
	char combined[MAX_CHAT_SIZE_EX];
	if (time != 0.0 && len_str > 12)
		q_snprintf(combined, sizeof(combined), "%-*s %s", padding_width, masked_str, str2);
	else
		q_snprintf(combined, sizeof(combined), "%-*s%s", padding_width, masked_str, str2);

	int combined_len = (int)strlen(combined);

	// Non-scrolling display if text fits
	if (combined_len <= maxchars) {
		M_PrintWhite(x, y, combined);
		return;
	}

	const int charwidth = 8;
	const int gap_len = 5;
	const int scrollspeed = 30; // pixels per second, matches Sbar_DrawScrollString
	int total_chars = combined_len + gap_len;
	int cycle_pixels = total_chars * charwidth;
	int pixel_offset;
	float frac = M_ScrollPixelOffset(time, scrollspeed, cycle_pixels, &pixel_offset);

	glPushMatrix();
	glTranslatef(-frac, 0.0f, 0.0f);
	for (int pass = 0; pass < 2; ++pass)
	{
		int base_x = x - pixel_offset + pass * cycle_pixels;
		for (int pos = 0; pos < total_chars; ++pos)
		{
			int char_x = base_x + pos * charwidth;

			if (char_x + charwidth <= x)
				continue;
			if (char_x >= x + maxwidth)
				break;

			char c;
			if (pos < combined_len)
				c = combined[pos];
			else
				c = " /// "[pos - combined_len];

			M_DrawCharacter(char_x, y, c);
		}
	}
	glPopMatrix();
}

void M_PrintHighlightScroll2(int x, int y, int maxwidth,
	const char* str, const char* str2,
	const char* highlight, double time)
{
	// How many visible characters fit on one line
	int maxchars = maxwidth / 8;

	// Safe string handling for name portion
	char name_str[256];
	int len_str = (int)strlen(str);
	int effective_len_str = (time != 0.0) ? len_str : (len_str > 12 ? 12 : len_str);

	// Safely copy the name portion
	q_strlcpy(name_str, str, sizeof(name_str));
	if (effective_len_str < len_str)
		name_str[effective_len_str] = '\0';

	// Build the name portion with proper padding
	char name_portion[256];
	if (time != 0.0 && len_str > 12)
		q_snprintf(name_portion, sizeof(name_portion), "%s ", name_str);
	else {
		int padding_width = max_word_length + 1;
		if (padding_width > 13)
			padding_width = 13;
		q_snprintf(name_portion, sizeof(name_portion), "%-*s", padding_width, name_str);
	}

	// Build combined string
	char combined[1024];
	q_snprintf(combined, sizeof(combined), "%s%s", name_portion, str2);

	int actual_name_len = (int)strlen(name_portion);
	int combined_len = (int)strlen(combined);
	int name_end = actual_name_len;

	// Find highlight positions
	int name_highlight_start = -1, name_highlight_end = -1;
	if (highlight && highlight[0]) {
		const char* nm = q_strcasestr(name_str, highlight);
		if (nm) {
			name_highlight_start = (int)(nm - name_str);
			name_highlight_end = name_highlight_start + (int)strlen(highlight);
			if (name_highlight_end > effective_len_str)
				name_highlight_end = effective_len_str;
		}
	}

	int desc_highlight_start = -1, desc_highlight_end = -1;
	if (highlight && highlight[0]) {
		const char* dm = q_strcasestr(str2, highlight);
		if (dm) {
			desc_highlight_start = (int)(dm - str2);
			desc_highlight_end = desc_highlight_start + (int)strlen(highlight);
			if (desc_highlight_end > (int)strlen(str2))
				desc_highlight_end = (int)strlen(str2);
		}
	}

	// Non-scrolling display if text fits
	if (combined_len <= maxchars) {
		// Draw name portion
		for (int i = 0; i < actual_name_len; i++) {
			char ch = combined[i];
			qboolean is_highlighted = (i < effective_len_str &&
				name_highlight_start != -1 &&
				i >= name_highlight_start &&
				i < name_highlight_end);
			qboolean is_bronzed = (i < effective_len_str) || (time == 0.0);

			M_DrawCharacter(x + i * 8, y, ch | (is_highlighted ? 0 : (is_bronzed ? 128 : 0)));
		}

		// Draw description portion
		int desc_x = x + actual_name_len * 8;
		for (int i = 0; i < (int)strlen(str2); i++) {
			char ch = str2[i];
			qboolean is_highlighted = (desc_highlight_start != -1 &&
				i >= desc_highlight_start &&
				i < desc_highlight_end);

			M_DrawCharacter(desc_x + i * 8, y, ch | (is_highlighted ? 128 : 0));
		}
		return;
	}

	// Scrolling display
	const int charwidth = 8;
	const int gap_len = 5;
	const int scrollspeed = 30; // pixels per second, matches Sbar_DrawScrollString
	int total_chars = combined_len + gap_len;
	int cycle_pixels = total_chars * charwidth;
	int pixel_offset;
	float frac = M_ScrollPixelOffset(time, scrollspeed, cycle_pixels, &pixel_offset);

	glPushMatrix();
	glTranslatef(-frac, 0.0f, 0.0f);
	for (int pass = 0; pass < 2; ++pass)
	{
		int base_x = x - pixel_offset + pass * cycle_pixels;
		for (int pos = 0; pos < total_chars; ++pos)
		{
			int char_x = base_x + pos * charwidth;

			if (char_x + charwidth <= x)
				continue;
			if (char_x >= x + maxwidth)
				break;

			int drawch;
			if (pos < combined_len)
			{
				char ch = combined[pos];
				qboolean is_highlighted = false;
				qboolean is_bronzed = false;

				if (pos < name_end)
				{
					if (pos < effective_len_str)
					{
						is_highlighted = (name_highlight_start != -1 &&
							pos >= name_highlight_start &&
							pos < name_highlight_end);
						is_bronzed = !is_highlighted;
					}
					else
					{
						is_bronzed = true;
					}
				}
				else
				{
					int desc_pos = pos - name_end;
					is_highlighted = (desc_highlight_start != -1 &&
						desc_pos >= desc_highlight_start &&
						desc_pos < desc_highlight_end);
				}

				drawch = ch | (is_highlighted ? 0 : (is_bronzed ? 128 : 0));
			}
			else
			{
				drawch = ' ' | 128; // Gap
			}

			M_DrawCharacter(char_x, y, drawch);
		}
	}
	glPopMatrix();
}

void M_PrintHighlightScroll(int x, int y, int maxwidth, const char* str, const char* highlight, double time)
{
    int len_str = strlen(str);

    // Copy the original string without masking
    char name_str[MAX_CHAT_SIZE_EX];
    strncpy(name_str, str, sizeof(name_str) - 1);
    name_str[sizeof(name_str) - 1] = '\0';

    // Compute highlight positions in the name
    int name_highlight_start = -1, name_highlight_end = -1;
    if (highlight && highlight[0])
    {
        const char* name_match = q_strcasestr(name_str, highlight);
        if (name_match)
        {
            name_highlight_start = name_match - name_str;
            name_highlight_end = name_highlight_start + strlen(highlight);
            if (name_highlight_end > len_str)
                name_highlight_end = len_str;
        }
    }

	const int charwidth = 8;
	const int gap_len = 5;
	const int scrollspeed = 30; // pixels per second, matches Sbar_DrawScrollString
	int total_chars = len_str + gap_len;
	int cycle_pixels = total_chars * charwidth;
	int pixel_offset;
	float frac = M_ScrollPixelOffset(time, scrollspeed, cycle_pixels, &pixel_offset);

	glPushMatrix();
	glTranslatef(-frac, 0.0f, 0.0f);
	for (int pass = 0; pass < 2; ++pass)
	{
		int base_x = x - pixel_offset + pass * cycle_pixels;
		for (int pos = 0; pos < total_chars; ++pos)
		{
			int char_x = base_x + pos * charwidth;

			if (char_x + charwidth <= x)
				continue;
			if (char_x >= x + maxwidth)
				break;

			int drawch;
			if (pos < len_str)
			{
				char ch = name_str[pos];
				qboolean is_highlighted = (name_highlight_start != -1 &&
					pos >= name_highlight_start &&
					pos < name_highlight_end);

				if (is_highlighted)
					drawch = ch & 127; // Draw character in normal color (highlighted)
				else
					drawch = ch | 128; // Apply bronze effect for non-highlighted text
			}
			else
			{
				drawch = ' ' | 128; // Gap
			}

			M_DrawCharacter(char_x, y, drawch);
		}
	}
	glPopMatrix();
}

//=============================================================================
/* Mouse helpers */

// woods #mousemenu

void M_ForceMousemove(void)
{
	int x, y;
	SDL_GetMouseState(&x, &y);
	M_Mousemove(x, y);
}

void M_UpdateCursor(int mousey, int starty, int itemheight, int numitems, int* cursor)
{
	int pos = (mousey - starty) / itemheight;
	if (pos > numitems - 1)
		pos = numitems - 1;
	if (pos < 0)
		pos = 0;
	*cursor = pos;
}

void M_UpdateCursorXY(int mousex, int mousey, int startx, int starty, int itemwidth, int itemheight, int numitems, int* cursorX, int* cursorY)
{
	int posx = (mousex - startx) / itemwidth;
	int posy = (mousey - starty) / itemheight;

	// Calculate the total number of rows based on the number of items and columns
	//int numrows = (numitems + numcolumns - 1) / numcolumns; // Ceiling division to ensure full coverage of items

	// Clamping posx to the range [0, numcolumns - 1]
	if (posx > numitems - 1)
		posx = numitems - 1;
	if (posx < 0)
		posx = 0;

	// Clamping posy to the range [0, numrows - 1]
	if (posy > numitems - 1)
		posy = numitems - 1;
	if (posy < 0)
		posy = 0;

	// Updating the cursor position
	*cursorX = posx;
	*cursorY = posy;
}


void M_UpdateCursorWithTable(int mousey, const int* table, int numitems, int* cursor)
{
	int i, dy;
	for (i = 0; i < numitems; i++)
	{
		dy = mousey - table[i];
		if (dy >= 0 && dy < 8)
		{
			*cursor = i;
			break;
		}
	}
}


// woods iw menu functions #modsmenu #skillmenu #mapsmenu #mousemenu

/* Listbox */

qboolean mapshint; // woods
qboolean maps_from_gameoptions = false;

typedef struct
{
	int				len;
	int				maxlen;
	qboolean(*match_fn) (int index);
	double			timeout;
	double			errtimeout;
	double			backspacecooldown;
	char			text[256];
} listsearch_t;

typedef struct
{
	int			cursor;
	int			numitems;
	int			viewsize;
	int			scroll;
	listsearch_t search;
	qboolean(*isactive_fn) (int index);
} menulist_t;

void M_List_CheckIntegrity(const menulist_t* list)
{
	SDL_assert(list->numitems >= 0);
	SDL_assert(list->cursor >= 0);
	SDL_assert(list->cursor < list->numitems);
	SDL_assert(list->scroll >= 0);
	SDL_assert(list->scroll < list->numitems);
	SDL_assert(list->viewsize > 0);
}

static menu_textfield_t	*textfield_drag_field = NULL;
static int				textfield_drag_text_x = 0;
static qboolean		textfield_mouse_dragging = false;
static double			textfield_mouseclick_time = 0.0;
static int				textfield_mouseclicks = 0; /* 1: char, 2: word, >=3: whole field */
static menu_textfield_t	*textfield_click_field = NULL;
static int				textfield_click_pos = -1;
static const double		TEXTFIELD_DOUBLECLICK_TIME = 0.5;
extern qpic_t *pic_ins;

static qboolean M_TextField_HasShortcutModifier(void)
{
#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC)
	return keydown[K_COMMAND] || keydown[K_CTRL];
#else
	return keydown[K_CTRL];
#endif
}

static qboolean M_TextField_HasWordMoveModifier(void)
{
#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC)
	return keydown[K_COMMAND];
#else
	return keydown[K_CTRL];
#endif
}

static qboolean M_TextField_HasWordDeleteModifier(void)
{
	return keydown[K_CTRL];
}

void M_TextField_ClampCursor(menu_textfield_t *tf)
{
	int len = (int)strlen(tf->text);

	if (tf->cursor < 0)
		tf->cursor = 0;
	if (tf->cursor > len)
		tf->cursor = len;
	if (tf->cursor > tf->max_len)
		tf->cursor = tf->max_len;
	if (tf->sel_start > len)
		tf->sel_start = len;
	if (tf->sel_start > tf->max_len)
		tf->sel_start = tf->max_len;
}

void M_TextField_Init(menu_textfield_t *tf, char *buffer, int max_len, qboolean digits_only)
{
	tf->text = buffer;
	tf->max_len = max_len;
	tf->cursor = (int)strlen(buffer);
	tf->sel_start = -1;
	tf->digits_only = digits_only;
	M_TextField_ClampCursor(tf);
}

void M_TextField_ClearSelection(menu_textfield_t *tf)
{
	tf->sel_start = -1;
}

static qboolean M_TextField_GetSelection(const menu_textfield_t *tf, int *out_start, int *out_end)
{
	if (tf->sel_start < 0)
		return false;
	if (tf->sel_start <= tf->cursor)
	{
		*out_start = tf->sel_start;
		*out_end = tf->cursor;
	}
	else
	{
		*out_start = tf->cursor;
		*out_end = tf->sel_start;
	}
	return *out_start != *out_end;
}

static int M_TextField_IntSign(int value)
{
	return (value < 0) ? -1 : ((value > 0) ? 1 : 0);
}

static int M_TextField_TestWordBoundary(int pos, const char *text, int len)
{
	if (pos <= 0)
		return 1;
	if (pos >= len)
		return -1;
	return q_isspace((unsigned char)text[pos - 1]) - q_isspace((unsigned char)text[pos]);
}

static void M_TextField_ApplyMouseSelection(menu_textfield_t *tf)
{
	int len = (int)strlen(tf->text);
	int anchor;
	int caret;
	int begin;
	int end;

	anchor = CLAMP(0, tf->sel_start, len);
	caret = CLAMP(0, tf->cursor, len);

	if (textfield_mouseclicks <= 1)
	{
		tf->sel_start = anchor;
		tf->cursor = caret;
		return;
	}

	if (textfield_mouseclicks >= 3)
	{
		tf->sel_start = 0;
		tf->cursor = len;
		return;
	}

	/* Double-click mode: expand to whole-word boundaries like the console. */
	{
		int boundary = M_TextField_IntSign(M_TextField_TestWordBoundary(anchor, tf->text, len));
		int dir = M_TextField_IntSign(caret - anchor);
		if (boundary && boundary != dir)
			anchor += boundary;
	}

	begin = q_min(anchor, caret);
	end = q_max(anchor, caret);

	while (!M_TextField_TestWordBoundary(begin, tf->text, len))
		--begin;
	while (!M_TextField_TestWordBoundary(end, tf->text, len))
		++end;

	if (anchor <= caret)
	{
		tf->sel_start = begin;
		tf->cursor = end;
	}
	else
	{
		tf->sel_start = end;
		tf->cursor = begin;
	}
}

static int M_TextField_FindWordBoundary(const menu_textfield_t *tf, int dir)
{
	const char *text = tf->text;
	int len = (int)strlen(text);
	int pos = tf->cursor;

	if (dir < 0)
	{
		while (pos > 0 && q_isspace(text[pos - 1]))
			--pos;
		while (pos > 0 && !q_isspace(text[pos - 1]))
			--pos;
	}
	else
	{
		while (pos < len && q_isspace(text[pos]))
			++pos;
		while (pos < len && !q_isspace(text[pos]))
			++pos;
	}

	return pos;
}

static void M_TextField_MoveCursor(menu_textfield_t *tf, int cursor, qboolean extend_selection)
{
	if (cursor < 0)
		cursor = 0;
	if (cursor > tf->max_len)
		cursor = tf->max_len;

	if (extend_selection)
	{
		if (tf->sel_start < 0)
			tf->sel_start = tf->cursor;
	}
	else
	{
		tf->sel_start = -1;
	}

	tf->cursor = cursor;
	M_TextField_ClampCursor(tf);
}

static qboolean M_TextField_DeleteRange(menu_textfield_t *tf, int start, int end)
{
	int len = (int)strlen(tf->text);

	start = CLAMP(0, start, len);
	end = CLAMP(0, end, len);
	if (start >= end)
		return false;

	memmove(tf->text + start, tf->text + end, (size_t)(len - end + 1));
	tf->cursor = start;
	tf->sel_start = -1;
	return true;
}

static qboolean M_TextField_DeleteSelection(menu_textfield_t *tf)
{
	int sel_begin, sel_end;

	if (!M_TextField_GetSelection(tf, &sel_begin, &sel_end))
		return false;

	return M_TextField_DeleteRange(tf, sel_begin, sel_end);
}

static qboolean M_TextField_Insert(menu_textfield_t *tf, const char *src)
{
	int cur_len;
	int space;

	if (!src || !*src)
		return false;

	if (tf->sel_start >= 0)
		M_TextField_DeleteSelection(tf);

	cur_len = (int)strlen(tf->text);
	space = tf->max_len - cur_len;
	if (space <= 0)
		return false;

	if (tf->digits_only)
	{
		int i;
		int inserted = 0;
		for (i = 0; src[i] && space > 0; ++i)
		{
			if (src[i] >= '0' && src[i] <= '9')
			{
				memmove(tf->text + tf->cursor + 1, tf->text + tf->cursor, (size_t)(cur_len - tf->cursor + 1));
				tf->text[tf->cursor] = src[i];
				++tf->cursor;
				++cur_len;
				--space;
				++inserted;
			}
		}
		tf->sel_start = -1;
		return inserted > 0;
	}
	else
	{
		int i;
		int inserted = 0;
		for (i = 0; src[i] && space > 0; ++i)
		{
			unsigned char c = (unsigned char)src[i];
			if (c < 32 || c > 126)
				continue;

			memmove(tf->text + tf->cursor + 1, tf->text + tf->cursor, (size_t)(cur_len - tf->cursor + 1));
			tf->text[tf->cursor] = (char)c;
			++tf->cursor;
			++cur_len;
			--space;
			++inserted;
		}
		tf->sel_start = -1;
		return inserted > 0;
	}
}

static void M_TextField_PlayCopySound(void)
{
	const char* sound_file = COM_FileExists("sound/qssm/copy.wav", NULL) ? "qssm/copy.wav" : "player/tornoff2.wav";
	S_LocalSound(sound_file);
}

static qboolean M_TextField_CopySelection(menu_textfield_t *tf)
{
	int sel_begin, sel_end;
	int copy_len;
	char *copy;

	if (!M_TextField_GetSelection(tf, &sel_begin, &sel_end))
		return false;

	copy_len = sel_end - sel_begin;
	copy = (char *)SDL_malloc((size_t)copy_len + 1);
	if (!copy)
		return false;

	memcpy(copy, tf->text + sel_begin, (size_t)copy_len);
	copy[copy_len] = 0;
	SDL_SetClipboardText(copy);
	SDL_free(copy);

	M_TextField_PlayCopySound();
	return true;
}

qboolean M_TextField_Key(menu_textfield_t *tf, int key)
{
	int len = (int)strlen(tf->text);
	int target;

	switch (key)
	{
	case K_LEFTARROW:
	case K_KP_LEFTARROW:
		target = tf->cursor;
		if (M_TextField_HasWordMoveModifier())
			target = M_TextField_FindWordBoundary(tf, -1);
		else if (target > 0)
			--target;
		M_TextField_MoveCursor(tf, target, keydown[K_SHIFT]);
		return true;

	case K_RIGHTARROW:
	case K_KP_RIGHTARROW:
		target = tf->cursor;
		if (M_TextField_HasWordMoveModifier())
			target = M_TextField_FindWordBoundary(tf, +1);
		else if (target < len)
			++target;
		M_TextField_MoveCursor(tf, target, keydown[K_SHIFT]);
		return true;

	case K_HOME:
		M_TextField_MoveCursor(tf, 0, keydown[K_SHIFT]);
		return true;

	case K_END:
		M_TextField_MoveCursor(tf, len, keydown[K_SHIFT]);
		return true;

	case K_BACKSPACE:
		if (M_TextField_DeleteSelection(tf))
			return true;
		if (M_TextField_HasWordDeleteModifier())
			return M_TextField_DeleteRange(tf, M_TextField_FindWordBoundary(tf, -1), tf->cursor);
		return M_TextField_DeleteRange(tf, tf->cursor - 1, tf->cursor);

	case K_DEL:
		if (M_TextField_DeleteSelection(tf))
			return true;
		if (M_TextField_HasWordDeleteModifier())
			return M_TextField_DeleteRange(tf, tf->cursor, M_TextField_FindWordBoundary(tf, +1));
		return M_TextField_DeleteRange(tf, tf->cursor, tf->cursor + 1);

	case 'a':
	case 'A':
		if (M_TextField_HasShortcutModifier())
		{
			tf->sel_start = 0;
			tf->cursor = len;
			return true;
		}
		break;

	case 'c':
	case 'C':
		if (M_TextField_HasShortcutModifier())
			return M_TextField_CopySelection(tf);
		break;

	case 'x':
	case 'X':
		if (M_TextField_HasShortcutModifier())
		{
			if (!M_TextField_CopySelection(tf))
				return false;
			M_TextField_DeleteSelection(tf);
			return true;
		}
		break;

	case 'v':
	case 'V':
		if (M_TextField_HasShortcutModifier())
		{
			char *clipboard = SDL_GetClipboardText();
			if (clipboard)
			{
				M_TextField_Insert(tf, clipboard);
				SDL_free(clipboard);
			}
			return true;
		}
		break;

	case 'u':
	case 'U':
		if (M_TextField_HasShortcutModifier())
		{
			tf->text[0] = 0;
			tf->cursor = 0;
			tf->sel_start = -1;
			return true;
		}
		break;
	}

	return false;
}

qboolean M_TextField_Char(menu_textfield_t *tf, int key)
{
	char text[2];
	char c;

	if (key < 32 || key > 126)
		return false;

	c = (char)key;
	if (tf->digits_only && (c < '0' || c > '9'))
		return false;

	text[0] = c;
	text[1] = 0;
	return M_TextField_Insert(tf, text);
}

static int M_TextField_MouseToCursor(menu_textfield_t *tf, int mouse_x, int text_x)
{
	int len = (int)strlen(tf->text);
	int pos;

	if (mouse_x < text_x)
		return 0;

	pos = (mouse_x - text_x + 4) / 8;
	if (pos < 0)
		pos = 0;
	if (pos > len)
		pos = len;
	if (pos > tf->max_len)
		pos = tf->max_len;
	return pos;
}

void M_TextField_DrawHighlight(menu_textfield_t *tf, int x, int y)
{
	int sel_begin, sel_end;

	if (M_TextField_GetSelection(tf, &sel_begin, &sel_end))
		Draw_Fill(x + sel_begin * 8, y, (sel_end - sel_begin) * 8, 8, 170, 0.4f);
}

void M_TextField_DrawCursor(menu_textfield_t *tf, int x, int y)
{
	if (((int)(realtime * 4) & 1))
		return;

	if (pic_ins)
		Draw_PicRGBA(x + 8 * tf->cursor, y, pic_ins, Draw_GetConcharsCursorColorByIndex(0), 1.0f);
	else
		M_DrawCharacter(x + 8 * tf->cursor, y, 10 + ((int)(realtime * 4) & 1));
}

void M_TextField_MouseClick(menu_textfield_t *tf, int mouse_x, int text_x)
{
	int cursor = M_TextField_MouseToCursor(tf, mouse_x, text_x);

	if (keydown[K_SHIFT])
	{
		textfield_mouseclicks = 1;
	}
	else
	{
		if (textfield_click_field != tf ||
			textfield_click_pos != cursor ||
			(realtime - textfield_mouseclick_time) >= TEXTFIELD_DOUBLECLICK_TIME)
		{
			textfield_mouseclicks = 1;
		}
		else
		{
			++textfield_mouseclicks;
		}

		textfield_click_field = tf;
		textfield_click_pos = cursor;
		textfield_mouseclick_time = realtime;
	}

	if (keydown[K_SHIFT])
	{
		if (tf->sel_start < 0)
			tf->sel_start = tf->cursor;
	}
	else
	{
		/* Anchor selection for drag; click-without-drag gets cleared on release. */
		tf->sel_start = cursor;
	}

	tf->cursor = cursor;
	M_TextField_ApplyMouseSelection(tf);
	textfield_drag_field = tf;
	textfield_drag_text_x = text_x;
	textfield_mouse_dragging = true;
}

void M_TextField_MouseDrag(int mouse_x)
{
	if (textfield_mouse_dragging && textfield_drag_field)
	{
		int cursor = M_TextField_MouseToCursor(textfield_drag_field, mouse_x, textfield_drag_text_x);
		textfield_drag_field->cursor = cursor;
		M_TextField_ApplyMouseSelection(textfield_drag_field);
	}
}

void M_TextField_CheckMouseRelease(void)
{
	if (textfield_mouse_dragging && !keydown[K_MOUSE1])
	{
		if (textfield_drag_field && textfield_drag_field->sel_start == textfield_drag_field->cursor)
			textfield_drag_field->sel_start = -1;
		textfield_drag_field = NULL;
		textfield_mouse_dragging = false;
	}
}

static qboolean M_TextField_MouseInRow(int mouse_y, int row_y)
{
	return (mouse_y >= row_y - 4 && mouse_y <= row_y + 12);
}

qboolean M_TextField_IsDraggingField(const menu_textfield_t *tf)
{
	return textfield_mouse_dragging && textfield_drag_field == tf;
}

qboolean M_TextField_IsDraggingAny(void)
{
	return textfield_mouse_dragging;
}

void M_List_AutoScroll(menulist_t* list)
{
	if (list->numitems <= list->viewsize)
		return;
	if (list->cursor < list->scroll)
	{
		list->scroll = list->cursor;
		if (list->isactive_fn)
		{
			while (list->scroll > 0 &&
				list->scroll > list->cursor - list->viewsize + 1 &&
				!list->isactive_fn(list->scroll - 1))
			{
				--list->scroll;
			}
		}
	}
	else if (list->cursor >= list->scroll + list->viewsize)
		list->scroll = list->cursor - list->viewsize + 1;
}

void M_List_CenterCursor(menulist_t* list)
{
	if (list->cursor >= list->viewsize)
	{
		if (list->cursor + list->viewsize >= list->numitems)
			list->scroll = list->numitems - list->viewsize; // last page, scroll to the end
		else
			list->scroll = list->cursor - list->viewsize / 2; // keep centered
		list->scroll = CLAMP(0, list->scroll, list->numitems - list->viewsize);
	}
	else
		list->scroll = 0;
}

int M_List_GetOverflow(const menulist_t* list)
{
	return list->numitems - list->viewsize;
}

// Note: y is in pixels, height is in chars!
qboolean M_List_GetScrollbar(const menulist_t* list, int* y, int* height)
{
	if (list->numitems <= list->viewsize)
	{
		*y = *height = 0;
		return false;
	}

	*height = (int)(list->viewsize * list->viewsize / (float)list->numitems + 0.5f);
	*height = q_max(*height, 2);
	*y = (int)(list->scroll * 8 / (float)(list->numitems - list->viewsize) * (list->viewsize - *height) + 0.5f);

	return true;
}

void M_List_DrawScrollbar(const menulist_t* list, int cx, int cy)
{
	int y, h;
	if (!M_List_GetScrollbar(list, &y, &h))
		return;
	M_DrawTextBox(cx - 4, cy + y - 4, 0, h - 1);
}

qboolean M_List_UseScrollbar(menulist_t* list, int yrel)
{
	int scrolly, scrollh, range;
	if (!M_List_GetScrollbar(list, &scrolly, &scrollh))
		return false;

	yrel -= scrollh * 4; // half the thumb height, in pixels
	range = (list->viewsize - scrollh) * 8;
	list->scroll = (int)(yrel * (float)(list->numitems - list->viewsize) / range + 0.5f);

	if (list->scroll > list->numitems - list->viewsize)
		list->scroll = list->numitems - list->viewsize;
	if (list->scroll < 0)
		list->scroll = 0;

	return true;
}

void M_List_GetVisibleRange(const menulist_t* list, int* first, int* count)
{
	*first = list->scroll;
	*count = q_min(list->scroll + list->viewsize, list->numitems) - list->scroll;
}

qboolean M_List_IsItemVisible(const menulist_t* list, int i)
{
	int first, count;
	M_List_GetVisibleRange(list, &first, &count);
	return (unsigned)(i - first) < (unsigned)count;
}

void M_List_Rescroll(menulist_t* list)
{
	int overflow = M_List_GetOverflow(list);
	if (overflow < 0)
		overflow = 0;
	if (list->scroll > overflow)
		list->scroll = overflow;
	if (list->cursor >= 0 && list->cursor < list->numitems && !M_List_IsItemVisible(list, list->cursor))
		M_List_AutoScroll(list);
}

qboolean M_List_SelectNextMatch(menulist_t* list, qboolean(*match_fn) (int idx), int start, int dir, qboolean wrap)
{
	int i, j;

	if (list->numitems <= 0)
		return false;

	if (!wrap)
		start = CLAMP(0, start, list->numitems - 1);

	for (i = 0, j = start; i < list->numitems; i++, j += dir)
	{
		if (j < 0)
		{
			if (!wrap)
				return false;
			j = list->numitems - 1;
		}
		else if (j >= list->numitems)
		{
			if (!wrap)
				return false;
			j = 0;
		}
		if (!match_fn || match_fn(j))
		{
			list->cursor = j;
			M_List_AutoScroll(list);
			return true;
		}
	}

	return false;
}

qboolean M_List_SelectNextActive(menulist_t* list, int start, int dir, qboolean wrap)
{
	return M_List_SelectNextMatch(list, list->isactive_fn, start, dir, wrap);
}

void M_List_UpdateMouseSelection(menulist_t* list)
{
	M_ForceMousemove();
	if (list->cursor < list->scroll)
		M_List_SelectNextActive(list, list->scroll, 1, false);
	else if (list->cursor >= list->scroll + list->viewsize)
		M_List_SelectNextActive(list, list->scroll + list->viewsize, -1, false);
}


qboolean M_List_Key(menulist_t* list, int key)
{
	switch (key)
	{
	case K_HOME:
	case K_KP_HOME:
		S_LocalSound("misc/menu1.wav");
		list->cursor = 0;
		M_List_AutoScroll(list);
		return true;

	case K_END:
	case K_KP_END:
		S_LocalSound("misc/menu1.wav");
		list->cursor = list->numitems - 1;
		M_List_AutoScroll(list);
		return true;

	case K_PGDN:
	case K_KP_PGDN:
		S_LocalSound("misc/menu1.wav");
		if (list->cursor - list->scroll < list->viewsize - 1)
			list->cursor = list->scroll + list->viewsize - 1;
		else
			list->cursor += list->viewsize - 1;
		list->cursor = q_min(list->cursor, list->numitems - 1);
		M_List_AutoScroll(list);
		return true;

	case K_PGUP:
	case K_KP_PGUP:
		S_LocalSound("misc/menu1.wav");
		if (list->cursor > list->scroll)
			list->cursor = list->scroll;
		else
			list->cursor -= list->viewsize - 1;
		list->cursor = q_max(list->cursor, 0);
		M_List_AutoScroll(list);
		return true;

	case K_UPARROW:
	case K_KP_UPARROW:
		if (m_maps)
			mapshint = true; // woods
		S_LocalSound("misc/menu1.wav");
		if (--list->cursor < 0)
			list->cursor = list->numitems - 1;
		M_List_AutoScroll(list);
		return true;


	case K_MWHEELUP:
		list->scroll -= 3;
		if (list->scroll < 0)
			list->scroll = 0;
		M_List_UpdateMouseSelection(list);
		return true;

	case K_MWHEELDOWN:
		list->scroll += 3;
		if (list->scroll > list->numitems - list->viewsize)
			list->scroll = list->numitems - list->viewsize;
		if (list->scroll < 0)
			list->scroll = 0;
		M_List_UpdateMouseSelection(list);
		return true;

	case K_DOWNARROW:
	case K_KP_DOWNARROW:
		if (m_maps)
			mapshint = true; // woods
		S_LocalSound("misc/menu1.wav");
		if (++list->cursor >= list->numitems)
			list->cursor = 0;
		M_List_AutoScroll(list);
		return true;

	default:
		return false;
	}
}

qboolean M_List_CycleMatch(menulist_t* list, int key, qboolean(*match_fn) (int idx, char c))
{
	int i, j, dir;

	if (!(key >= 'a' && key <= 'z') &&
		!(key >= 'A' && key <= 'Z') &&
		!(key >= '0' && key <= '9'))
		return false;

	if (list->numitems <= 0)
		return false;

	S_LocalSound("misc/menu1.wav");

	key = q_tolower(key);
	dir = keydown[K_SHIFT] ? -1 : 1;

	for (i = 1, j = list->cursor + dir; i < list->numitems; i++, j += dir)
	{
		j = (j + list->numitems) % list->numitems; // avoid negative mod
		if (match_fn(j, (char)key))
		{
			list->cursor = j;
			M_List_AutoScroll(list);
			break;
		}
	}

	return true;
}

void M_List_Mousemove(menulist_t* list, int yrel)
{
	int i, firstvis, numvis;

	M_List_GetVisibleRange(list, &firstvis, &numvis);
	if (!numvis || yrel < 0)
		return;
	i = yrel / 8;
	if (i >= numvis)
		return;

	i += firstvis;
	if (list->cursor == i)
		return;

	if (list->isactive_fn && !list->isactive_fn(i))
	{
		int before, after;
		yrel += firstvis * 8;

		for (before = i - 1; before >= firstvis; before--)
			if (list->isactive_fn(before))
				break;
		for (after = i + 1; after < firstvis + numvis; after++)
			if (list->isactive_fn(after))
				break;

		if (before >= firstvis && after < firstvis + numvis)
		{
			int distbefore = yrel - 4 - before * 8;
			int distafter = after * 8 + 4 - yrel;
			i = distbefore < distafter ? before : after;
		}
		else if (before >= firstvis)
			i = before;
		else if (after < firstvis + numvis)
			i = after;
		else
			return;

		if (list->cursor == i)
			return;
	}

	list->cursor = i;

	//M_MouseSound("misc/menu1.wav");
}

void M_DeletePrevWord(listsearch_t* search)
{
	int pos = search->len;

	/* 1.  skip any trailing spaces */
	while (pos > 0 && q_isspace(search->text[pos - 1]))
		--pos;

	/* 2.  walk backwards until we hit the previous space */
	while (pos > 0 && !q_isspace(search->text[pos - 1]))
		--pos;

	/* 3.  shrink the string */
	search->len = pos;
	search->text[pos] = '\0';
}

//=============================================================================

int m_save_demonum;


/*
==================
Main Menu
==================
*/

int	m_main_cursor;
int m_main_mods; // woods #modsmenu (iw)
int m_main_demos; // woods #modsmenu #demosmenu (iw)

enum // woods #modsmenu (iw)
{
	MAIN_SINGLEPLAYER,
	MAIN_MULTIPLAYER,
	MAIN_OPTIONS,
	MAIN_MODS,
	MAIN_DEMOS, // woods #demosmenu
	MAIN_HELP,
	MAIN_QUIT,

	MAIN_ITEMS,
};


void M_Menu_Main_f (void)
{
	if (key_dest != key_menu)
	{
		m_save_demonum = cls.demonum;
		cls.demonum = -1;
	}
	key_dest = key_menu;
	m_state = m_main;
	m_entersound = true;

	progs_check_done = false; // woods #botdetect

	// woods #modsmenu (iw)

	// When switching to a mod with a custom UI the 'Mods' option
// is no longer available in the main menu, so we move the cursor
// to 'Options' to nudge the player toward the secondary location.
// TODO (maybe): inform the user about the missing option
// and its alternative location?
	if (!m_main_mods && m_main_cursor == MAIN_MODS)
	{
		extern int options_cursor;
		m_main_cursor = MAIN_OPTIONS;
		options_cursor = 3; // OPT_MODS
	}

	IN_UpdateGrabs();
}

void M_Main_Draw (void) // woods #modsmenu #demosmenu (iw)
{
	int cursor, f;
	qpic_t* p;

	M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
	p = Draw_CachePic("gfx/ttl_main.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	p = Draw_CachePic("gfx/mainmenu.lmp");
	int split = 60;
	int offset = 0;

	if (m_main_mods && m_main_demos) // both mods and demos
	{
		M_DrawSubpic(72, 32, p, 0, 0, p->width, split);
		M_DrawTransPic(72, 32 + split, Draw_CachePic("gfx/menumods.lmp"));
		M_DrawTransPic(72, 52 + split, Draw_CachePic("gfx/menudemos.lmp"));
		M_DrawSubpic(72, 72 + split, p, 0, split, p->width, p->height - split);
	}
	
	else if (m_main_mods && !m_main_demos) // only mods
	{
		M_DrawSubpic(72, 32 + offset, p, 0, 0, p->width, split);
		M_DrawTransPic(72, 32 + offset + split, Draw_CachePic("gfx/menumods.lmp"));
		M_DrawSubpic(72, 32 + offset + split + 20, p, 0, split, p->width, p->height - split);
		offset += split + 20; // Adjust offset if needed for further items
	}

	else if (m_main_demos && !m_main_mods) // only demos
	{
		M_DrawSubpic(72, 32 + offset, p, 0, 0, p->width, split);
		M_DrawTransPic(72, 32 + offset + split, Draw_CachePic("gfx/menudemos.lmp"));
		M_DrawSubpic(72, 32 + offset + split + 20, p, 0, split, p->width, p->height - split);
		offset += split + 20; // Adjust offset if needed for further items
	}

	else
		M_DrawTransPic(72, 32, Draw_CachePic("gfx/mainmenu.lmp")); // neither mods nor demos

	f = (int)(realtime * 10) % 6;
	cursor = m_main_cursor;

	// Adjust cursor position based on mods and demos activation
	if (!m_main_mods && cursor > MAIN_MODS) cursor--;
	if (!m_main_demos && cursor >= MAIN_DEMOS) cursor--;

	M_DrawTransPic(54, 32 + cursor * 20, Draw_CachePic(va("gfx/menudot%i.lmp", f + 1)));
}

static double m_lastkey_time;
static qboolean m_key_was_m;

void M_Main_Key (int key) // woods #modsmenu #demosmenu (iw)
{
	double time_since_m;

	switch (key)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		key_dest = key_game;
		m_state = m_none;
		cls.demonum = m_save_demonum;
		IN_UpdateGrabs();
		if (!cl_demoreel.value)	/* QuakeSpasm customization: */
			break;
		if (cl_demoreel.value >= 2 && cls.demonum == -1)
			cls.demonum = 0;
		if (cls.demonum != -1 && !cls.demoplayback && cls.state != ca_connected)
			CL_NextDemo ();
		break;

	case 'm':
	case 'M':
		m_key_was_m = true;
		m_lastkey_time = realtime;
		// Just toggle between multiplayer and mods when only 'm' is pressed
		if (m_main_mods && m_main_cursor == MAIN_MULTIPLAYER)
			m_main_cursor = MAIN_MODS;
		else
			m_main_cursor = MAIN_MULTIPLAYER;
		S_LocalSound("misc/menu1.wav");
		break;

	case 'o':
	case 'O':
		time_since_m = realtime - m_lastkey_time;
		if (m_key_was_m && time_since_m < 0.5 && m_main_mods)  // 500ms window to type 'mo'
		{
			m_main_cursor = MAIN_MODS;  // Always go to mods when 'mo' is typed
			S_LocalSound("misc/menu1.wav");
		}
		else
		{
			m_main_cursor = MAIN_OPTIONS;
			S_LocalSound("misc/menu1.wav");
		}
		m_key_was_m = false;  // Reset the flag
		break;

	case 'u':
	case 'U':
		time_since_m = realtime - m_lastkey_time;
		if (m_key_was_m && time_since_m < 0.5)  // 500ms window to type 'mu'
		{
			m_main_cursor = MAIN_MULTIPLAYER;  // Always go to multiplayer when 'mu' is typed
			S_LocalSound("misc/menu1.wav");
		}
		m_key_was_m = false;  // Reset the flag
		break;
	case 's':
	case 'S':
		m_key_was_m = false;  // Reset m flag when other keys are pressed
		m_main_cursor = MAIN_SINGLEPLAYER;
		S_LocalSound("misc/menu1.wav");
		break;

	case 'd':
	case 'D':
		m_key_was_m = false;
		if (m_main_demos)
		{
			m_main_cursor = MAIN_DEMOS;
			S_LocalSound("misc/menu1.wav");
		}
		break;

	case 'h':
	case 'H':
		m_key_was_m = false;
		m_main_cursor = MAIN_HELP;
		S_LocalSound("misc/menu1.wav");
		break;

	case 'q':
	case 'Q':
		m_key_was_m = false;
		m_main_cursor = MAIN_QUIT;
		S_LocalSound("misc/menu1.wav");
		break;

	case K_DOWNARROW:
		m_key_was_m = false;  // Reset m flag when using arrows
		S_LocalSound("misc/menu1.wav");
		do {
			if (++m_main_cursor >= MAIN_ITEMS)
				m_main_cursor = 0;
		} while ((m_main_cursor == MAIN_MODS && !m_main_mods) || (m_main_cursor == MAIN_DEMOS && !m_main_demos));
		break;

	case K_UPARROW:
		m_key_was_m = false;  // Reset m flag when using arrows
		S_LocalSound("misc/menu1.wav");
		do {
			if (--m_main_cursor < 0)
				m_main_cursor = MAIN_ITEMS - 1;
		} while ((m_main_cursor == MAIN_MODS && !m_main_mods) || (m_main_cursor == MAIN_DEMOS && !m_main_demos));
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1: // woods #mousemenu
		m_key_was_m = false;
		m_entersound = true;

		switch (m_main_cursor)
		{
		case MAIN_SINGLEPLAYER:
			M_Menu_SinglePlayer_f ();
			break;

		case MAIN_MULTIPLAYER:
			M_Menu_MultiPlayer_f ();
			break;

		case MAIN_OPTIONS:
			M_Menu_Options_f ();
			break;

		case MAIN_HELP:
			M_Menu_Help_f ();
			break;

		case MAIN_MODS:
			M_Menu_Mods_f();
			break;

		case MAIN_DEMOS: // woods #demosmenu
			M_Menu_Demos_f ();
			break;

		case MAIN_QUIT:
			M_Menu_Quit_f ();
			break;
		}
	}
}

void M_Main_Mousemove(int cx, int cy) // woods #mousemenu
{
	M_UpdateCursor(cy, 32, 20, MAIN_ITEMS - !m_main_mods - !m_main_demos, &m_main_cursor);
	if (m_main_cursor >= MAIN_MODS && !m_main_mods)
		++m_main_cursor;
	if (m_main_cursor >= MAIN_DEMOS && !m_main_demos)
		++m_main_cursor;
}

/*
==================
Singleplayer Menu
==================
*/

qboolean m_singleplayer_showlevels;
int	m_singleplayer_cursor;
#define	SINGLEPLAYER_ITEMS	(3 + m_singleplayer_showlevels)


void M_Menu_SinglePlayer_f (void)
{
	if (m_singleplayer_cursor >= SINGLEPLAYER_ITEMS)
		m_singleplayer_cursor = 0;
	
	key_dest = key_menu;
	m_state = m_singleplayer;
	m_entersound = true;

	IN_UpdateGrabs();
}


void M_SinglePlayer_Draw (void)
{
	int		f;
	qpic_t	*p;

	M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp") );
	p = Draw_CachePic ("gfx/ttl_sgl.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);
	M_DrawTransPic (72, 32, Draw_CachePic ("gfx/sp_menu.lmp") );
	if (m_singleplayer_showlevels)
		M_DrawTransPic(72, 92, Draw_CachePic("gfx/sp_maps.lmp"));

	f = (int)(realtime * 10)%6;

	M_DrawTransPic (54, 32 + m_singleplayer_cursor * 20,Draw_CachePic( va("gfx/menudot%i.lmp", f+1 ) ) );
}

static double sp_lastkey_time;  // For single player menu
static qboolean sp_key_was_l;   // For "le"/"lo" detection

void M_SinglePlayer_Key (int key)
{
	double time_since_l;

	switch (key)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		M_Menu_Main_f ();
		break;

	case 'n':
	case 'N':
		sp_key_was_l = false;
		m_singleplayer_cursor = 0;  // New Game
		S_LocalSound("misc/menu1.wav");
		break;

	case 'l':
	case 'L':
		if (m_singleplayer_cursor == 1)  // If already on Load
		{
			if (m_singleplayer_showlevels)
			{
				m_singleplayer_cursor = 3;  // Go to Levels
				S_LocalSound("misc/menu1.wav");
			}
		}
		else
		{
			sp_lastkey_time = realtime;
			sp_key_was_l = true;
			m_singleplayer_cursor = 1;  // Load Game
			S_LocalSound("misc/menu1.wav");
		}
		break;

	case 'o':
	case 'O':
		time_since_l = realtime - sp_lastkey_time;
		if (sp_key_was_l && time_since_l < 0.5)  // 500ms window to type 'lo'
		{
			m_singleplayer_cursor = 1;  // Always go to Load when 'lo' is typed
			S_LocalSound("misc/menu1.wav");
		}
		sp_key_was_l = false;  // Reset the flag
		break;

	case 'e':
	case 'E':
		time_since_l = realtime - sp_lastkey_time;
		if (sp_key_was_l && time_since_l < 0.5 && m_singleplayer_showlevels)  // 500ms window to type 'le'
		{
			m_singleplayer_cursor = 3;  // Always go to Levels when 'le' is typed
			S_LocalSound("misc/menu1.wav");
		}
		sp_key_was_l = false;  // Reset the flag
		break;
	case 's':
	case 'S':
		sp_key_was_l = false;
		m_singleplayer_cursor = 2;  // Save Game
		S_LocalSound("misc/menu1.wav");
		break;

	case K_DOWNARROW:
		sp_key_was_l = false;
		S_LocalSound ("misc/menu1.wav");
		if (++m_singleplayer_cursor >= SINGLEPLAYER_ITEMS)
			m_singleplayer_cursor = 0;
		break;

	case K_UPARROW:
		sp_key_was_l = false;
		S_LocalSound ("misc/menu1.wav");
		if (--m_singleplayer_cursor < 0)
			m_singleplayer_cursor = SINGLEPLAYER_ITEMS - 1;
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1: // woods #mousemenu
		sp_key_was_l = false;
		m_entersound = true;

		switch (m_singleplayer_cursor)
		{
		case 0:
			if (sv.active)
				if (!SCR_ModalMessage("Are you sure you want to\nstart a new game?\n (^mn^m/^my^m)\n", 0.0f))
					break;
			key_dest = key_game;
			IN_UpdateGrabs();
			if (sv.active)
				Cbuf_AddText ("disconnect\n");
			Cbuf_AddText ("maxplayers 1\n");
			Cbuf_AddText ("samelevel 0\n"); //spike -- you'd be amazed how many qw players have this setting breaking their singleplayer experience...
			Cbuf_AddText ("deathmatch 0\n"); //johnfitz
			Cbuf_AddText ("coop 0\n"); //johnfitz
			Cbuf_AddText ("startmap_sp\n");
			break;

		case 1:
			M_Menu_Load_f ();
			break;

		case 2:
			M_Menu_Save_f ();
			break;
		case 3:
			Cbuf_AddText("menu_maps\n");
			break;
		}
		break;
	}
}

void M_SinglePlayer_Mousemove(int cx, int cy) // woods #mousemenu
{
	M_UpdateCursor(cy, 32, 20, SINGLEPLAYER_ITEMS, &m_singleplayer_cursor);
}

/*
==================
Load/Save Menu
==================
*/

int		load_cursor;		// 0 < load_cursor < MAX_SAVEGAMES

#define	MAX_SAVEGAMES		20	/* johnfitz -- increased from 12 */
#define	SAVEGAME_KILLS_COLUMN		22	/* Host_SavegameComment puts "kills:" at this column. */
#define	SAVEGAME_MENU_COMMENT_CHARS	79
#define	SAVEGAME_MENU_COMMENT_BUFFER	(SAVEGAME_MENU_COMMENT_CHARS + 1)
#define	SAVEGAME_MENU_COMMENT_SCAN	"%" QS_STRINGIFY(SAVEGAME_MENU_COMMENT_CHARS) "s\n"
#define	SAVEGAME_MENU_SLOTS			(MAX_SAVEGAMES - 1)
#define	MAX_AUTOSAVE_MENU_ENTRIES	64
#define	MAX_SAVEGAME_MENU_ENTRIES	(SAVEGAME_MENU_SLOTS + MAX_AUTOSAVE_MENU_ENTRIES)
#define	LOADGAME_SEPARATOR_INDEX		-1
#define	LOADGAME_LIST_X				16
#define	LOADGAME_LIST_Y				32
#define	LOADGAME_LIST_COLS			36
#define	LOADGAME_LIST_ROWS			15
#define	LOADGAME_INFO_Y				164
#define	LOADGAME_SEARCH_BOX_Y		176
#define	LOADGAME_SEARCH_TEXT_Y		184
char	m_filenames[MAX_SAVEGAMES][SAVEGAME_COMMENT_LENGTH+1];
int		loadable[MAX_SAVEGAMES];

typedef struct {
	char name[SAVEGAME_COMMENT_LENGTH + 1];
	char loadname[MAX_OSPATH];
	char date[32];
	char mapname[MAX_QPATH];
	time_t timestamp;
	qboolean loadable;
	qboolean autosave;
	int original_index;
} save_entry_t;

static save_entry_t save_entries[MAX_SAVEGAME_MENU_ENTRIES];
static int save_entries_count;

static struct
{
	menulist_t	list;
	int			filtered_indices[MAX_SAVEGAME_MENU_ENTRIES + 1];
	int			prev_cursor;
	int			x, y, cols;
	qboolean	scrollbar_grab;
	menuticker_t ticker;
} loadmenu;

static int save_compare(const void* a, const void* b) // Comparison function for qsort
{
	const save_entry_t* sa = (const save_entry_t*)a;
	const save_entry_t* sb = (const save_entry_t*)b;

	// Sort loadable saves first, then by timestamp (newest first)
	if (sa->loadable != sb->loadable)
		return sb->loadable - sa->loadable;
	if (sa->autosave != sb->autosave)
		return sb->autosave - sa->autosave;
	if (sa->timestamp < sb->timestamp)
		return 1;
	if (sa->timestamp > sb->timestamp)
		return -1;
	return 0;
}

static void M_ClearSaveEntry (save_entry_t *entry, int original_index)
{
	memset(entry, 0, sizeof(*entry));
	strcpy(entry->name, "--- UNUSED SLOT ---");
	entry->original_index = original_index;
	if (original_index >= 0)
		q_snprintf(entry->loadname, sizeof(entry->loadname), "s%i", original_index);
}

static void M_NormalizeSavegameComment (char display[SAVEGAME_COMMENT_LENGTH + 1], const char *comment)
{
	char	text[SAVEGAME_MENU_COMMENT_BUFFER];
	char	*kills;
	char	*slash;
	char	*title_end;
	size_t	title_len;
	int	j;

	q_strlcpy(text, comment, sizeof(text));
	for (j = 0; text[j]; j++)
	{
		if (text[j] == '_')
			text[j] = ' ';
	}

	memset(display, ' ', SAVEGAME_COMMENT_LENGTH);
	display[SAVEGAME_COMMENT_LENGTH] = '\0';

	kills = strstr(text, "kills:");
	if (!kills)
	{
		q_strlcpy(display, text, SAVEGAME_COMMENT_LENGTH + 1);
		return;
	}

	title_end = kills;
	while (title_end > text && title_end[-1] == ' ')
		title_end--;

	title_len = (size_t)(title_end - text);
	if (title_len > SAVEGAME_KILLS_COLUMN)
		title_len = SAVEGAME_KILLS_COLUMN;
	memcpy(display, text, title_len);
	q_strlcpy(display + SAVEGAME_KILLS_COLUMN, kills, SAVEGAME_COMMENT_LENGTH - SAVEGAME_KILLS_COLUMN + 1);

	// Fix the kills pattern - handle both single and double spaces after slash.
	slash = strchr(display + SAVEGAME_KILLS_COLUMN, '/');
	if (slash && slash[1] == ' ')
	{
		if (slash[2] == ' ')
			memmove(slash + 1, slash + 3, strlen(slash + 3) + 1);
		else
			memmove(slash + 1, slash + 2, strlen(slash + 2) + 1);
	}
}

static qboolean M_ReadSaveEntry (save_entry_t *entry, const char *path, const char *loadname,
	int original_index, qboolean autosave)
{
	int	j;
	char	comment[SAVEGAME_MENU_COMMENT_BUFFER];
	FILE	*f;
	int	version;
	float time;
	char mapname[MAX_QPATH];
	char saved_gamedir[MAX_QPATH];
	qboolean remaster_save;
#ifdef _WIN32
	struct _stat st;
#else
	struct stat st;
#endif

	M_ClearSaveEntry(entry, original_index);
	q_strlcpy(entry->loadname, loadname, sizeof(entry->loadname));
	entry->autosave = autosave;

	f = fopen (path, "r");
	if (!f)
		return false;

	// Get file modification time
#ifdef _WIN32
	if (_stat(path, &st) == 0)
#else
	if (stat(path, &st) == 0)
#endif
	{
		struct tm* timeinfo = localtime(&st.st_mtime);
		if (timeinfo)
		{
			strftime(entry->date, sizeof(entry->date),
				"%Y-%m-%d %H:%M", timeinfo);
			entry->timestamp = st.st_mtime;
		}
	}

	// Read version and comment. Kex rerelease saves include a gamedir line after the version.
	saved_gamedir[0] = '\0';
	if (fscanf (f, "%i\n", &version) != 1 ||
		(version != SAVEGAME_VERSION && version != SAVEGAME_VERSION_KEX))
	{
		fclose(f);
		return false;
	}
	remaster_save = (version == SAVEGAME_VERSION_KEX);
	if ((remaster_save && fscanf (f, "%63s\n", saved_gamedir) != 1) ||
		fscanf (f, SAVEGAME_MENU_COMMENT_SCAN, comment) != 1)
	{
		fclose(f);
		return false;
	}

	if (remaster_save && !COM_GameDirMatches(saved_gamedir))
	{
		q_snprintf(entry->name, sizeof(entry->name),
			"wrong gamedir: %s", saved_gamedir[0] ? saved_gamedir : "(unknown)");
		fclose(f);
		return true;
	}

	// Read spawn parms (skip them)
	for (j = 0; j < NUM_BASIC_SPAWN_PARMS; j++)
	{
		if (fscanf(f, "%f\n", &time) != 1)
			break;
	}
	if (j < NUM_BASIC_SPAWN_PARMS)
	{
		fclose(f);
		return false;
	}

	// Read skill
	if (fscanf(f, "%f\n", &time) != 1)
	{
		fclose(f);
		return false;
	}

	// Read map name
	if (fscanf(f, "%63s\n", mapname) == 1)
		q_strlcpy (entry->mapname, mapname, sizeof(entry->mapname));

	M_NormalizeSavegameComment(entry->name, comment);

	entry->loadable = true;
	fclose(f);
	return true;
}

typedef struct {
	int count;
} autosave_scan_t;

static qboolean M_AddAutosaveEntry (void *ctx, const char *fname)
{
	autosave_scan_t *scan = (autosave_scan_t *)ctx;
	save_entry_t *entry;
	char path[MAX_OSPATH];
	char base[MAX_OSPATH];
	char loadname[MAX_OSPATH];

	if (scan->count >= MAX_SAVEGAME_MENU_ENTRIES)
		return false;
	if (!fname[0] || fname[0] == '.' || strchr(fname, '/') || strchr(fname, '\\'))
		return true;

	COM_StripExtension(fname, base, sizeof(base));
	q_snprintf(path, sizeof(path), "%s/saves/autosave/%s", com_gamedir, fname);
	q_snprintf(loadname, sizeof(loadname), "autosave/%s", base);

	entry = &save_entries[scan->count];
	if (M_ReadSaveEntry(entry, path, loadname, -1, true))
		scan->count++;

	return true;
}

void M_ScanSaves (qboolean include_autosaves)
{
	int	i;
	char	name[MAX_OSPATH];
	char	loadname[MAX_OSPATH];
	autosave_scan_t scan;

	for (i = 0; i < MAX_SAVEGAME_MENU_ENTRIES; i++)
		M_ClearSaveEntry(&save_entries[i], -1);

	scan.count = 0;
	for (i = 0; i < SAVEGAME_MENU_SLOTS; i++)
	{
		save_entry_t numbered_entry;

		q_snprintf(loadname, sizeof(loadname), "s%i", i);
		q_snprintf(name, sizeof(name), "%s/saves/%s.sav", com_gamedir, loadname);
		if (!M_ReadSaveEntry(&numbered_entry, name, loadname, i, false))
		{
			q_snprintf(name, sizeof(name), "%s/%s.sav", com_gamedir, loadname); // legacy
			if (!M_ReadSaveEntry(&numbered_entry, name, loadname, i, false))
			{
				if (!include_autosaves && scan.count < SAVEGAME_MENU_SLOTS)
					M_ClearSaveEntry(&save_entries[scan.count++], i);
				continue;
			}
		}

		if (scan.count < MAX_SAVEGAME_MENU_ENTRIES)
			save_entries[scan.count++] = numbered_entry;
	}

	if (include_autosaves)
	{
		q_snprintf(name, sizeof(name), "%s/saves/autosave", com_gamedir);
		COM_ListSystemFiles(&scan, name, "sav", M_AddAutosaveEntry);
	}

	// Sort the entries
	qsort(save_entries, scan.count, sizeof(save_entry_t), save_compare);
	save_entries_count = scan.count;

	while (scan.count < SAVEGAME_MENU_SLOTS)
		M_ClearSaveEntry(&save_entries[scan.count++], -1);

}

static qboolean M_Load_EntryIsPresent(int index)
{
	return index >= 0 && index < save_entries_count &&
		strcmp(save_entries[index].name, "--- UNUSED SLOT ---");
}

static qboolean M_Load_EntryMatchesSearch(int index)
{
	save_entry_t *entry;
	const char *search = loadmenu.list.search.text;

	if (!M_Load_EntryIsPresent(index))
		return false;

	if (loadmenu.list.search.len <= 0)
		return true;

	entry = &save_entries[index];
	return q_strcasestr(entry->name, search) ||
		q_strcasestr(entry->mapname, search) ||
		q_strcasestr(entry->date, search) ||
		q_strcasestr(entry->loadname, search) ||
		(entry->autosave && q_strcasestr("auto autosave auto save", search));
}

static qboolean M_Load_IsSelectableDisplayIndex(int index)
{
	int entry_index;

	if (index < 0 || index >= loadmenu.list.numitems)
		return false;

	entry_index = loadmenu.filtered_indices[index];
	if (entry_index == LOADGAME_SEPARATOR_INDEX)
		return false;

	return save_entries[entry_index].loadable;
}

static save_entry_t* M_Load_SelectedEntry(void)
{
	int entry_index;

	if (loadmenu.list.numitems <= 0 ||
		loadmenu.list.cursor < 0 ||
		loadmenu.list.cursor >= loadmenu.list.numitems)
		return NULL;

	entry_index = loadmenu.filtered_indices[loadmenu.list.cursor];
	if (entry_index == LOADGAME_SEPARATOR_INDEX)
		return NULL;

	if (!save_entries[entry_index].loadable)
		return NULL;

	return &save_entries[entry_index];
}

static qboolean M_Load_EnsureSelectableCursor(int dir)
{
	if (loadmenu.list.numitems <= 0)
		return false;

	if (M_Load_IsSelectableDisplayIndex(loadmenu.list.cursor))
		return true;

	if (M_List_SelectNextActive(&loadmenu.list, loadmenu.list.cursor, dir, true))
		return true;

	loadmenu.list.cursor = 0;
	loadmenu.list.scroll = 0;
	return false;
}

static void M_Load_EnsureSelectableCursorForKey(int key)
{
	switch (key)
	{
	case K_UPARROW:
	case K_KP_UPARROW:
	case K_LEFTARROW:
	case K_KP_LEFTARROW:
	case K_PGUP:
	case K_KP_PGUP:
	case K_END:
	case K_KP_END:
		M_Load_EnsureSelectableCursor(-1);
		break;

	default:
		M_Load_EnsureSelectableCursor(1);
		break;
	}
}

static void M_Load_Refilter(void)
{
	int i, count = 0;
	qboolean have_autosaves = false;
	qboolean have_regular_saves = false;

	for (i = 0; i < save_entries_count; i++)
	{
		if (!M_Load_EntryMatchesSearch(i))
			continue;
		if (save_entries[i].autosave)
			have_autosaves = true;
		else
			have_regular_saves = true;
	}

	for (i = 0; i < save_entries_count; i++)
	{
		if (save_entries[i].autosave && M_Load_EntryMatchesSearch(i))
			loadmenu.filtered_indices[count++] = i;
	}

	if (have_autosaves && have_regular_saves)
		loadmenu.filtered_indices[count++] = LOADGAME_SEPARATOR_INDEX;

	for (i = 0; i < save_entries_count; i++)
	{
		if (!save_entries[i].autosave && M_Load_EntryMatchesSearch(i))
			loadmenu.filtered_indices[count++] = i;
	}

	loadmenu.list.numitems = count;
	if (loadmenu.list.numitems <= 0)
	{
		loadmenu.list.cursor = 0;
		loadmenu.list.scroll = 0;
		return;
	}

	if (loadmenu.list.cursor >= loadmenu.list.numitems)
		loadmenu.list.cursor = loadmenu.list.numitems - 1;
	if (loadmenu.list.cursor < 0)
		loadmenu.list.cursor = 0;

	M_Load_EnsureSelectableCursor(1);
	M_List_CenterCursor(&loadmenu.list);
}

static void M_Load_MoveCursor(int dir)
{
	if (loadmenu.list.numitems <= 0)
		return;

	S_LocalSound("misc/menu1.wav");
	M_List_SelectNextActive(&loadmenu.list, loadmenu.list.cursor + dir, dir, true);
}

static void M_Load_Init(void)
{
	loadmenu.list.cursor = 0;
	loadmenu.list.scroll = 0;
	loadmenu.list.viewsize = LOADGAME_LIST_ROWS;
	loadmenu.list.numitems = 0;
	loadmenu.list.isactive_fn = M_Load_IsSelectableDisplayIndex;
	memset(&loadmenu.list.search, 0, sizeof(loadmenu.list.search));
	loadmenu.list.search.maxlen = 32;
	loadmenu.prev_cursor = -1;
	loadmenu.x = LOADGAME_LIST_X;
	loadmenu.y = LOADGAME_LIST_Y;
	loadmenu.cols = LOADGAME_LIST_COLS;
	loadmenu.scrollbar_grab = false;
	M_Ticker_Init(&loadmenu.ticker);

	M_Load_Refilter();
}

void M_Menu_Load_f (void)
{
	m_entersound = true;
	m_state = m_load;

	key_dest = key_menu;
	Host_WaitForSaveThread ();
	M_ScanSaves (true);
	M_Load_Init();

	IN_UpdateGrabs();
}


void M_Menu_Save_f (void)
{
	if (!sv.active)
		return;
	if (cl.intermission)
		return;
	if (svs.maxclients != 1)
		return;
	m_entersound = true;
	m_state = m_save;

	key_dest = key_menu;
	IN_UpdateGrabs();
	Host_WaitForSaveThread ();
	M_ScanSaves (false);
}


static void M_DrawSaveSlots (const char* title_pic)
{
	qpic_t* p = Draw_CachePic(title_pic);
	M_DrawPic((320 - p->width) / 2, 4, p);

	for (int i = 0; i < SAVEGAME_MENU_SLOTS; i++)
	{
		if (save_entries[i].autosave)
			M_PrintWhite(16, 32 + 8 * i, save_entries[i].name);
		else
			M_Print(16, 32 + 8 * i, save_entries[i].name);
	}

	// Draw date info in last slot position with white text
	if (save_entries[load_cursor].loadable)
	{
		char info[128];
		M_Print(16, 32 + 8 * SAVEGAME_MENU_SLOTS + 4,
			save_entries[load_cursor].autosave ? "auto save:" : "last save:");
		q_snprintf(info, sizeof(info), "%s (%s)",
			save_entries[load_cursor].date,
			save_entries[load_cursor].mapname);
		M_PrintWhite(100, 32 + 8 * SAVEGAME_MENU_SLOTS + 4, info);
	}

// line cursor
	if (load_cursor < SAVEGAME_MENU_SLOTS)
		M_DrawCharacter(8, 32 + load_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

void M_Load_Draw (void)
{
	qpic_t* p;
	int firstvis, numvis, i;
	save_entry_t *selected;

	loadmenu.x = LOADGAME_LIST_X;
	loadmenu.y = LOADGAME_LIST_Y;
	loadmenu.cols = LOADGAME_LIST_COLS;

	if (!keydown[K_MOUSE1])
		loadmenu.scrollbar_grab = false;

	if (loadmenu.prev_cursor != loadmenu.list.cursor)
	{
		loadmenu.prev_cursor = loadmenu.list.cursor;
		M_Ticker_Init(&loadmenu.ticker);
	}
	else
	{
		M_Ticker_Update(&loadmenu.ticker);
	}

	p = Draw_CachePic("gfx/p_load.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	if (loadmenu.list.numitems > 0)
	{
		M_List_GetVisibleRange(&loadmenu.list, &firstvis, &numvis);
		for (i = 0; i < numvis; i++)
		{
			const int draw_idx = i + firstvis;
			const int entry_idx = loadmenu.filtered_indices[draw_idx];
			const int item_y = loadmenu.y + i * 8;
			const int maxchars = loadmenu.cols - 2;
			const int maxwidth = maxchars * 8;
			const qboolean selected_row = (draw_idx == loadmenu.list.cursor &&
				M_Load_IsSelectableDisplayIndex(draw_idx));
			qboolean matched, needs_scroll;
			save_entry_t *entry;

			if (entry_idx == LOADGAME_SEPARATOR_INDEX)
				continue;

			entry = &save_entries[entry_idx];
			matched = (loadmenu.list.search.len > 0 &&
				q_strcasestr(entry->name, loadmenu.list.search.text) != NULL);
			needs_scroll = ((int)strlen(entry->name) > maxchars);

			if (entry->autosave)
			{
				if (needs_scroll)
					M_PrintScroll(loadmenu.x, item_y, maxwidth, entry->name,
						selected_row ? loadmenu.ticker.scroll_time : 0.0, false);
				else
					M_PrintWhite(loadmenu.x, item_y, entry->name);
			}
			else if (matched)
			{
				if (needs_scroll)
					M_PrintHighlightScroll(loadmenu.x, item_y, maxwidth,
						entry->name, loadmenu.list.search.text,
						selected_row ? loadmenu.ticker.scroll_time : 0.0);
				else
					M_PrintHighlight(loadmenu.x, item_y, entry->name,
						loadmenu.list.search.text,
						loadmenu.list.search.len);
			}
			else if (needs_scroll)
			{
				M_PrintScroll(loadmenu.x, item_y, maxwidth, entry->name,
					selected_row ? loadmenu.ticker.scroll_time : 0.0, true);
			}
			else
			{
				M_Print(loadmenu.x, item_y, entry->name);
			}

			if (selected_row)
				M_DrawCharacter(loadmenu.x - 8, item_y, 12 + ((int)(realtime * 4) & 1));
		}
	}
	else
	{
		M_PrintWhite(loadmenu.x, loadmenu.y,
			save_entries_count > 0 ? "No matching saves" : "No saved games");
	}

	if (M_List_GetOverflow(&loadmenu.list) > 0)
	{
		M_List_DrawScrollbar(&loadmenu.list, loadmenu.x + loadmenu.cols * 8 - 8, loadmenu.y);

		if (loadmenu.list.scroll > 0)
			M_DrawEllipsisBar(loadmenu.x, loadmenu.y - 8, loadmenu.cols);
		if (loadmenu.list.scroll + loadmenu.list.viewsize < loadmenu.list.numitems)
			M_DrawEllipsisBar(loadmenu.x, loadmenu.y + loadmenu.list.viewsize * 8, loadmenu.cols);
	}

	selected = M_Load_SelectedEntry();
	if (selected)
	{
		char info[128];
		M_Print(16, LOADGAME_INFO_Y, selected->autosave ? "auto save:" : "last save:");
		q_snprintf(info, sizeof(info), "%s (%s)", selected->date, selected->mapname);
		M_PrintWhite(100, LOADGAME_INFO_Y, info);
	}

	if (loadmenu.list.search.len > 0)
	{
		int cursor_x = 24 + 8 * loadmenu.list.search.len;
		M_DrawTextBox(16, LOADGAME_SEARCH_BOX_Y, 32, 1);
		M_PrintHighlight(24, LOADGAME_SEARCH_TEXT_Y, loadmenu.list.search.text,
			loadmenu.list.search.text,
			loadmenu.list.search.len);
		if (loadmenu.list.numitems == 0)
			M_DrawCharacter(cursor_x, LOADGAME_SEARCH_TEXT_Y, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, LOADGAME_SEARCH_TEXT_Y, 10 + ((int)(realtime * 4) & 1));
	}
}

void M_Save_Draw (void)
{
	M_DrawSaveSlots ("gfx/p_save.lmp");
}


void M_Load_Key (int k)
{
	save_entry_t *selected;

	if (keydown[K_CTRL])
	{
		if ((k == 'u' || k == 'U') && loadmenu.list.search.len > 0)
		{
			loadmenu.list.search.len = 0;
			loadmenu.list.search.text[0] = 0;
			loadmenu.list.cursor = 0;
			loadmenu.list.scroll = 0;
			M_Load_Refilter();
			return;
		}
		else if (k == K_BACKSPACE && loadmenu.list.search.len > 0)
		{
			M_DeletePrevWord(&loadmenu.list.search);
			loadmenu.list.cursor = 0;
			loadmenu.list.scroll = 0;
			M_Load_Refilter();
			return;
		}
	}

	if (k >= 32 && k < 127)
	{
		if (loadmenu.list.search.len < loadmenu.list.search.maxlen)
		{
			loadmenu.list.search.text[loadmenu.list.search.len++] = k;
			loadmenu.list.search.text[loadmenu.list.search.len] = 0;
			loadmenu.list.cursor = 0;
			loadmenu.list.scroll = 0;
			M_Load_Refilter();
		}
		return;
	}

	if (k == K_BACKSPACE && loadmenu.list.search.len > 0)
	{
		loadmenu.list.search.text[--loadmenu.list.search.len] = 0;
		loadmenu.list.cursor = 0;
		loadmenu.list.scroll = 0;
		M_Load_Refilter();
		return;
	}

	if (loadmenu.scrollbar_grab)
	{
		switch (k)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			loadmenu.scrollbar_grab = false;
			break;
		}
		return;
	}

	if (loadmenu.list.numitems > 0 && M_List_Key(&loadmenu.list, k))
	{
		M_Load_EnsureSelectableCursorForKey(k);
		return;
	}

	switch (k)
	{
	case K_ESCAPE:
		if (loadmenu.list.search.len > 0)
		{
			loadmenu.list.search.len = 0;
			loadmenu.list.search.text[0] = 0;
			loadmenu.list.cursor = 0;
			loadmenu.list.scroll = 0;
			M_Load_Refilter();
			return;
		}
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		M_Menu_SinglePlayer_f ();
		break;

	case K_LEFTARROW:
	case K_KP_LEFTARROW:
		M_Load_MoveCursor(-1);
		break;

	case K_RIGHTARROW:
	case K_KP_RIGHTARROW:
		M_Load_MoveCursor(1);
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	enter:
		selected = M_Load_SelectedEntry();
		S_LocalSound ("misc/menu2.wav");
		if (!selected)
			return;
		m_state = m_none;
		key_dest = key_game;
		IN_UpdateGrabs();

	// Host_Loadgame_f can't bring up the loading plaque because too much
	// stack space has been used, so do it now
		SCR_BeginLoadingPlaque ();

	// issue the load command
		Cbuf_AddText (va ("load \"%s\"\n", selected->loadname));
		return;

	case K_MOUSE1: // woods #mousemenu
		if (loadmenu.list.numitems > 0)
		{
			int x = m_mousex - loadmenu.x - (loadmenu.cols - 1) * 8;
			int y = m_mousey - loadmenu.y;
			if (x >= -8 && M_List_UseScrollbar(&loadmenu.list, y))
			{
				loadmenu.scrollbar_grab = true;
				M_Load_Mousemove(m_mousex, m_mousey);
				break;
			}
		}
		goto enter;

	default:
		break;
	}
}


void M_Save_Key (int k)
{
	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		M_Menu_SinglePlayer_f ();
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1: // woods #mousemenu
		m_state = m_none;
		key_dest = key_game;
		IN_UpdateGrabs();
		Cbuf_AddText (va("save s%i\n", save_entries[load_cursor].original_index));
		return;

	case K_UPARROW:
	case K_LEFTARROW:
		S_LocalSound ("misc/menu1.wav");
		load_cursor--;
		if (load_cursor < 0)
			load_cursor = SAVEGAME_MENU_SLOTS - 1;
		break;

	case K_DOWNARROW:
	case K_RIGHTARROW:
		S_LocalSound ("misc/menu1.wav");
		load_cursor++;
		if (load_cursor >= SAVEGAME_MENU_SLOTS)
			load_cursor = 0;
		break;
	}
}

void M_Load_Mousemove(int cx, int cy) // woods #mousemenu
{
	cy -= loadmenu.y;
	(void)cx;

	if (loadmenu.scrollbar_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			loadmenu.scrollbar_grab = false;
			return;
		}
		M_List_UseScrollbar(&loadmenu.list, cy);
	}

	if (m_mousey >= LOADGAME_INFO_Y)
		return;

	if (loadmenu.list.numitems > 0)
		M_List_Mousemove(&loadmenu.list, cy);
}

void M_Save_Mousemove(int cx, int cy) // woods #mousemenu
{
	M_UpdateCursor(cy, 32, 8, SAVEGAME_MENU_SLOTS, &load_cursor);
}

/*
==================
Maps Menu (iw)
==================
*/

#define MAX_VIS_MAPS			17
#define DOWNLOAD_MAPS_LABEL		"Download"

typedef struct
{
	const char* name;
	const char* date;
	qboolean	active;
	qboolean	download_menu;
} mapitem_t;

static struct
{
	menulist_t			list;
	enum m_state_e		prev;
	int					prev_cursor;
	qboolean			scrollbar_grab;
	menuticker_t		ticker;
	int					mapcount;
	int					x, y, cols;
	qboolean			download_available;
	mapitem_t			*items;
	int*                filtered_indices;
} mapsmenu;

static void M_Maps_Add(const char* name, const char* date)
{
	mapitem_t map;

	map.name = name;
	map.date = date ? date : "";
	map.active = true;
	map.download_menu = false;

	VEC_PUSH(mapsmenu.items, map);
	mapsmenu.mapcount = (int)VEC_SIZE(mapsmenu.items);
}

static void M_Maps_AddDownloadMenu(void)
{
	mapitem_t map;

	map.name = DOWNLOAD_MAPS_LABEL;
	map.date = "...";
	map.active = true;
	map.download_menu = true;

	VEC_PUSH(mapsmenu.items, map);
	mapsmenu.mapcount = (int)VEC_SIZE(mapsmenu.items);
}

static int M_Maps_ContentCount(void)
{
	int i, count = 0;

	for (i = 0; i < mapsmenu.mapcount; i++)
		if (!mapsmenu.items[i].download_menu)
			count++;
	return count;
}

static int M_Maps_DescriptionX(int x)
{
	return x + q_min(max_word_length + 1, 13) * 8;
}

static void M_Maps_DrawDownloadMenuPrompt(int x, int y, const char *highlight, int highlight_len)
{
	if (highlight_len > 0)
		M_PrintHighlight(x, y, DOWNLOAD_MAPS_LABEL, highlight, highlight_len);
	else
		M_Print(x, y, DOWNLOAD_MAPS_LABEL);

	M_PrintWhite(M_Maps_DescriptionX(x), y, "...");
}

static void M_Maps_UpdateViewsize(void)
{
	mapsmenu.list.viewsize = (mapsmenu.download_available && mapsmenu.list.search.len == 0) ?
		MAX_VIS_MAPS - 1 : MAX_VIS_MAPS;
}

static void M_Maps_Refilter(void)
{
	int i;

	M_Maps_UpdateViewsize();
	VEC_CLEAR(mapsmenu.filtered_indices);

	for (i = 0; i < mapsmenu.mapcount; i++)
	{
		if (mapsmenu.list.search.len == 0 ||
			q_strcasestr(mapsmenu.items[i].name, mapsmenu.list.search.text) ||
			(mapsmenu.items[i].date && q_strcasestr(mapsmenu.items[i].date, mapsmenu.list.search.text)))
		{
			VEC_PUSH(mapsmenu.filtered_indices, i);
		}
	}

	mapsmenu.list.numitems = (int)VEC_SIZE(mapsmenu.filtered_indices);

	if (mapsmenu.list.cursor >= mapsmenu.list.numitems)
		mapsmenu.list.cursor = mapsmenu.list.numitems - 1;

	if (mapsmenu.list.cursor < 0 && mapsmenu.list.numitems > 0)
		mapsmenu.list.cursor = 0;

	M_List_CenterCursor(&mapsmenu.list);
}

static void M_Maps_Init(void)
{
	filelist_item_t* item;

	mapsmenu.scrollbar_grab = false;
	mapsmenu.download_available = CL_QWMapListDownloadsAvailable();
	mapsmenu.prev_cursor = -2;
	mapsmenu.list.cursor = -1;
	mapsmenu.list.scroll = 0;
	mapsmenu.list.numitems = 0;
	mapsmenu.mapcount = 0;
	VEC_CLEAR(mapsmenu.items);
	VEC_CLEAR(mapsmenu.filtered_indices);

	memset(&mapsmenu.list.search, 0, sizeof(mapsmenu.list.search));
	mapsmenu.list.search.maxlen = 32;
	M_Maps_UpdateViewsize();

	M_Ticker_Init(&mapsmenu.ticker);

	if (mapsmenu.download_available)
		M_Maps_AddDownloadMenu();

	if (!descriptionsParsed)
		ExtraMaps_ParseDescriptions();

	for (item = extralevels; item; item = item->next)
		M_Maps_Add(item->name, item->data);

	M_Maps_Refilter();

	if (mapsmenu.list.cursor == -1)
		mapsmenu.list.cursor = 0;

	M_List_CenterCursor(&mapsmenu.list);
}

void M_Menu_Maps_f(void)
{
	key_dest = key_menu;
	mapsmenu.prev = m_state;
	m_state = m_maps;
	m_entersound = true;
	M_Maps_Init();
}

static qboolean M_DownloadMaps_ActiveMapName(char *display_name, size_t display_size)
{
	const char *current;

	if (display_size > 0)
		display_name[0] = '\0';

	if (!cls.download.active || !cls.download.current[0])
		return false;

	if (q_strcasecmp(COM_FileGetExtension(cls.download.current), "bsp"))
		return false;

	current = COM_SkipPath(cls.download.current);
	COM_StripExtension(current, display_name, display_size);
	return display_name[0] != '\0';
}

static qboolean M_DownloadMaps_NameIsActive(const char *name)
{
	char active_name[MAX_QPATH];
	char display_name[MAX_QPATH];

	if (!name || !M_DownloadMaps_ActiveMapName(active_name, sizeof(active_name)))
		return false;

	COM_StripExtension(COM_SkipPath(name), display_name, sizeof(display_name));
	return !q_strcasecmp(active_name, display_name);
}

static void M_DownloadMaps_DrawProgressPercent(int x, int y, int progress)
{
	char digits[8];

	q_snprintf(digits, sizeof(digits), "%d", progress);
	M_Print2(x, y, digits);
	M_Print(x + (int)strlen(digits) * 8, y, "%");
}

static void M_DownloadMaps_DrawActiveDownload(int x, int y, int maxwidth, const char *display_name)
{
	char visible_name[MAX_QPATH];
	int maxchars, namechars, progress = -1, progresschars = 0;

	if (!display_name || !display_name[0])
		return;

	if (cls.download.percent >= 0.0f)
	{
		progress = (int)(cls.download.percent + 0.5f);
		if (progress < 0)
			progress = 0;
		else if (progress > 100)
			progress = 100;
		progresschars = (progress >= 100) ? 4 : (progress >= 10 ? 3 : 2);
	}

	maxchars = maxwidth / 8;
	if (maxchars <= 0)
		return;

	namechars = maxchars;
	if (progress >= 0 && maxchars > progresschars + 1)
		namechars = maxchars - progresschars - 1;

	q_strlcpy(visible_name, display_name, sizeof(visible_name));
	if (namechars < (int)sizeof(visible_name))
		visible_name[namechars] = '\0';

	Draw_StringGradientSweep(x, y, visible_name, 96.0f, 48.0f, 1.0f, true);

	if (progress >= 0)
	{
		int namelen = (int)strlen(visible_name);
		int progress_x = x + namelen * 8 + (namelen > 0 ? 8 : 0);

		if (progress_x + progresschars * 8 <= x + maxwidth)
			M_DownloadMaps_DrawProgressPercent(progress_x, y, progress);
	}
}

static void M_DownloadMaps_DrawInstalledMap(int x, int y, int maxwidth, const char *display_name, double time)
{
	const int charwidth = 8;
	const int gap_len = 5;
	const int scrollspeed = 30;
	plcolour_t white = CL_PLColours_Parse("0xffffff");
	int maxchars, len, total_chars, cycle_pixels, pixel_offset, pass;
	float frac;

	if (!display_name || !display_name[0])
		return;

	maxchars = maxwidth / charwidth;
	len = (int)strlen(display_name);

	if (len <= maxchars)
	{
		int i;
		for (i = 0; i < len; i++)
			M_DrawCharacterRGBA(x + i * charwidth, y, (unsigned char)display_name[i], white, 0.5f);
		return;
	}

	total_chars = len + gap_len;
	cycle_pixels = total_chars * charwidth;
	frac = M_ScrollPixelOffset(time, scrollspeed, cycle_pixels, &pixel_offset);

	glPushMatrix();
	glTranslatef(-frac, 0.0f, 0.0f);
	for (pass = 0; pass < 2; pass++)
	{
		int base_x = x - pixel_offset + pass * cycle_pixels;
		int pos;
		for (pos = 0; pos < total_chars; pos++)
		{
			int char_x = base_x + pos * charwidth;
			int ch;

			if (char_x + charwidth <= x)
				continue;
			if (char_x >= x + maxwidth)
				break;

			ch = (pos < len) ? (unsigned char)display_name[pos] : (unsigned char)" /// "[pos - len];
			M_DrawCharacterRGBA(char_x, y, ch, white, 0.5f);
		}
	}
	glPopMatrix();
}

static qboolean M_Maps_HasDownloadGap(void)
{
	return mapsmenu.list.search.len == 0 &&
		mapsmenu.list.scroll == 0 &&
		mapsmenu.list.numitems > 1 &&
		mapsmenu.items[mapsmenu.filtered_indices[0]].download_menu;
}

static qboolean M_Maps_MouseYInDownloadGap(int yrel)
{
	return M_Maps_HasDownloadGap() && yrel >= 8 && yrel < 16;
}

void M_Maps_Draw(void)
{
	int x, y, i, cols;
	int firstvis, numvis;

	x = 16;
	y = 32;
	cols = 36;

	mapsmenu.x = x;
	mapsmenu.y = y;
	mapsmenu.cols = cols;

	if (!keydown[K_MOUSE1])
		mapsmenu.scrollbar_grab = false;

	if (mapsmenu.download_available != CL_QWMapListDownloadsAvailable())
	{
		M_Maps_Init();
		return;
	}

	if (mapsmenu.prev_cursor != mapsmenu.list.cursor)
	{
		mapsmenu.prev_cursor = mapsmenu.list.cursor;
		M_Ticker_Init(&mapsmenu.ticker);
	}
	else
		M_Ticker_Update(&mapsmenu.ticker);

	M_DrawCountHeader(x, y - 28, cols, "Levels",
		M_Maps_ContentCount(), "level", "levels");
	M_DrawQuakeBar(x - 8, y - 16, cols + 2);

	M_List_GetVisibleRange(&mapsmenu.list, &firstvis, &numvis);
	for (i = 0; i < numvis; i++)
	{
		int idx = i + firstvis;
		int map_idx = mapsmenu.filtered_indices[idx];
		mapitem_t* map_item = &mapsmenu.items[map_idx];
		qboolean selected = (idx == mapsmenu.list.cursor);
		int row = i;
		int item_y;

		if (M_Maps_HasDownloadGap() && i > 0)
			row++;
		item_y = y + row * 8;

		if (mapsmenu.list.search.len > 0)
		{
			if (map_item->download_menu)
			{
				char active_download[MAX_QPATH];
				if (M_DownloadMaps_ActiveMapName(active_download, sizeof(active_download)))
					M_DownloadMaps_DrawActiveDownload(x, item_y, (cols - 2) * 8, active_download);
				else
					M_Maps_DrawDownloadMenuPrompt(x, item_y,
						mapsmenu.list.search.text,
						mapsmenu.list.search.len);
			}
			else
			{
				M_PrintHighlightScroll2(x, item_y, (cols - 2) * 8,
					map_item->name,
					map_item->date,
					mapsmenu.list.search.text,
					selected ? mapsmenu.ticker.scroll_time : 0.0);
			}
		}
		else
		{
			if (map_item->download_menu)
			{
				char active_download[MAX_QPATH];
				if (M_DownloadMaps_ActiveMapName(active_download, sizeof(active_download)))
					M_DownloadMaps_DrawActiveDownload(x, item_y, (cols - 2) * 8, active_download);
				else
					M_Maps_DrawDownloadMenuPrompt(x, item_y, NULL, 0);
			}
			else
				M_PrintScroll2(x, item_y, (cols - 2) * 8,
					map_item->name,
					map_item->date,
					selected ? mapsmenu.ticker.scroll_time : 0.0,
					true);
		}

		if (selected)
			M_DrawCharacter(x - 8, item_y, 12 + ((int)(realtime * 4) & 1));
	}

	if (M_List_GetOverflow(&mapsmenu.list) > 0)
	{
		M_List_DrawScrollbar(&mapsmenu.list, x + cols * 8 - 8, y);

		if (mapsmenu.list.scroll > 0)
			M_DrawEllipsisBar(x, y - 8, cols);
		if (mapsmenu.list.scroll + mapsmenu.list.viewsize < mapsmenu.list.numitems)
			M_DrawEllipsisBar(x, y + (mapsmenu.list.viewsize + (M_Maps_HasDownloadGap() ? 1 : 0)) * 8, cols);
	}

	if (mapsmenu.list.search.len > 0) // Draw search box if search is active
	{
		int cursor_x = 24 + 8 * mapsmenu.list.search.len; // Start position + character width * text length

		M_DrawTextBox(16, 176, 32, 1);
		M_PrintHighlight(24, 184, mapsmenu.list.search.text,
			mapsmenu.list.search.text,
			mapsmenu.list.search.len);
		if (mapsmenu.list.numitems == 0)
			M_DrawCharacter(cursor_x, 184, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, 184, 10 + ((int)(realtime * 4) & 1));
	}
}

qboolean M_Maps_Match(int index, char initial)
{
	int map_idx = mapsmenu.filtered_indices[index];

	return q_tolower(mapsmenu.items[map_idx].name[0]) == initial;
}

void M_Maps_Key(int key)
{
	int x, y;

	if (keydown[K_CTRL])
	{
		if ((key == 'u' || key == 'U') && mapsmenu.list.search.len > 0)
		{
			mapsmenu.list.search.len = 0;
			mapsmenu.list.search.text[0] = 0;
			M_Maps_Refilter();
			return;
		}
		else if (key == K_BACKSPACE && mapsmenu.list.search.len > 0)
		{
			M_DeletePrevWord(&mapsmenu.list.search);
			M_Maps_Refilter();
			return;
		}
	}
	if (key >= 32 && key < 127) // Handle search input first, printable characters
	{
		if (mapsmenu.list.search.len < mapsmenu.list.search.maxlen)
		{
			mapsmenu.list.search.text[mapsmenu.list.search.len++] = key;
			mapsmenu.list.search.text[mapsmenu.list.search.len] = 0;
			M_Maps_Refilter();
			return;
		}
	}

	if (mapsmenu.scrollbar_grab)
	{
		switch (key)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			mapsmenu.scrollbar_grab = false;
			break;
		}
		return;
	}

	if (M_List_Key(&mapsmenu.list, key))
		return;

	if (M_List_CycleMatch(&mapsmenu.list, key, M_Maps_Match))
		return;

	if (M_Ticker_Key(&mapsmenu.ticker, key))
		return;

	switch (key)
	{
	case K_ESCAPE:
		if (mapsmenu.list.search.len > 0) // Clear search but stay in menu
		{
			mapsmenu.list.search.len = 0;
			mapsmenu.list.search.text[0] = 0;
			M_Maps_Refilter();
			return;
		}
		/* fall through */
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		if (maps_from_gameoptions)
		{
			maps_from_gameoptions = false;
			M_Menu_GameOptions_f();
		}
		else
		{
			M_Menu_SinglePlayer_f();
		}
		break;

	case K_BACKSPACE:
		if (mapsmenu.list.search.len > 0)
		{
			mapsmenu.list.search.text[--mapsmenu.list.search.len] = 0;
			M_Maps_Refilter();
			return;
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	enter:
		if (mapsmenu.list.numitems > 0 && mapsmenu.items[mapsmenu.filtered_indices[mapsmenu.list.cursor]].name[0])
		{
			mapitem_t *map_item = &mapsmenu.items[mapsmenu.filtered_indices[mapsmenu.list.cursor]];
			if (map_item->download_menu)
			{
				M_Menu_DownloadMaps_f();
				break;
			}

			if (maps_from_gameoptions)
			{
				// Set the map and return to game options
				M_SetSkillMenuMap(map_item->name);
				maps_from_gameoptions = false;
				M_GameOptions_ClearTypedLevel();
				M_Menu_GameOptions_f();
			}
			else
			{
				// Original behavior - go to skill menu
				M_SetSkillMenuMap(map_item->name);
				M_Menu_Skill_f();
			}
		}
		else
			S_LocalSound ("misc/menu3.wav");
		break;

	case K_MOUSE1:
		x = m_mousex - mapsmenu.x - (mapsmenu.cols - 1) * 8;
		y = m_mousey - mapsmenu.y;
		if (x < -8 || !M_List_UseScrollbar(&mapsmenu.list, y))
		{
			if (M_Maps_MouseYInDownloadGap(y))
			{
				S_LocalSound ("misc/menu3.wav");
				break;
			}
			goto enter;
		}
		mapsmenu.scrollbar_grab = true;
		M_Maps_Mousemove(m_mousex, m_mousey);
		break;

	default:
		break;
	}
}


void M_Maps_Mousemove(int cx, int cy)
{
	cy -= mapsmenu.y;

	if (mapsmenu.scrollbar_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			mapsmenu.scrollbar_grab = false;
			return;
		}
		M_List_UseScrollbar(&mapsmenu.list, cy);
		// Note: no return, we also update the cursor
	}

	if (M_Maps_MouseYInDownloadGap(cy))
		return;
	if (M_Maps_HasDownloadGap() && cy >= 16)
		cy -= 8;

	M_List_Mousemove(&mapsmenu.list, cy);
}

/*
==================
Download Maps Menu
==================
*/

typedef struct
{
	char name[MAX_QPATH];
} downloadmaps_recent_t;

static struct
{
	menulist_t			list;
	enum m_state_e		prev;
	int					prev_cursor;
	qboolean			scrollbar_grab;
	menuticker_t		ticker;
	int					mapcount;
	int					x, y, cols;
	char				message[64];
	double				message_time;
	char				pending_download[MAX_QPATH];
	downloadmaps_recent_t *recent_downloads;
	int*				filtered_indices;
} downloadmapsmenu;

static const char *M_DownloadMaps_SelectedName(void);

static void M_DownloadMaps_SetMessage(const char *message)
{
	q_strlcpy(downloadmapsmenu.message, message, sizeof(downloadmapsmenu.message));
	downloadmapsmenu.message_time = realtime;
}

static const char *M_DownloadMaps_DisplayName(const char *name, char *buffer, size_t buffer_size)
{
	if (!name)
	{
		if (buffer_size > 0)
			buffer[0] = '\0';
		return buffer;
	}

	COM_StripExtension(name, buffer, buffer_size);
	return buffer;
}

static qboolean M_DownloadMaps_LocalPath(const char *name, char *path, size_t path_size)
{
	if (!name || !*name || path_size == 0)
		return false;

	if (!q_strncasecmp(name, "maps/", 5))
		return q_strlcpy(path, name, path_size) < path_size;

	return (size_t)q_snprintf(path, path_size, "maps/%s", name) < path_size;
}

static qboolean M_DownloadMaps_AlreadyHave(const char *name)
{
	char path[MAX_QPATH];

	if (!M_DownloadMaps_LocalPath(name, path, sizeof(path)))
		return false;

	return COM_FileExists(path, NULL);
}

static qboolean M_DownloadMaps_NameMatches(const char *a, const char *b)
{
	return a && b && !q_strcasecmp(a, b);
}

static qboolean M_DownloadMaps_RecentDownloadActive(const char *name)
{
	int i;

	if (!name)
		return false;

	for (i = 0; i < (int)VEC_SIZE(downloadmapsmenu.recent_downloads); i++)
		if (M_DownloadMaps_NameMatches(name, downloadmapsmenu.recent_downloads[i].name))
			return true;

	return false;
}

static void M_DownloadMaps_AddRecentDownload(const char *name)
{
	downloadmaps_recent_t recent;

	if (!name || !*name || M_DownloadMaps_RecentDownloadActive(name))
		return;

	q_strlcpy(recent.name, name, sizeof(recent.name));
	VEC_PUSH(downloadmapsmenu.recent_downloads, recent);
}

static void M_DownloadMaps_UpdateCompletionState(void)
{
	if (!downloadmapsmenu.pending_download[0])
		return;

	if (M_DownloadMaps_NameIsActive(downloadmapsmenu.pending_download))
		return;

	if (!M_DownloadMaps_AlreadyHave(downloadmapsmenu.pending_download))
		return;

	M_DownloadMaps_AddRecentDownload(downloadmapsmenu.pending_download);
	downloadmapsmenu.pending_download[0] = '\0';
	downloadmapsmenu.message[0] = '\0';
}

static void M_DownloadMaps_Refilter(void)
{
	int i;

	VEC_CLEAR(downloadmapsmenu.filtered_indices);

	for (i = 0; i < downloadmapsmenu.mapcount; i++)
	{
		const char *name = QWMapList_NameAt(i);
		char display_name[MAX_QPATH];

		if (!name)
			continue;

		M_DownloadMaps_DisplayName(name, display_name, sizeof(display_name));

		if (downloadmapsmenu.list.search.len == 0 ||
			q_strcasestr(display_name, downloadmapsmenu.list.search.text))
		{
			VEC_PUSH(downloadmapsmenu.filtered_indices, i);
		}
	}

	downloadmapsmenu.list.numitems = (int)VEC_SIZE(downloadmapsmenu.filtered_indices);

	if (downloadmapsmenu.list.cursor >= downloadmapsmenu.list.numitems)
		downloadmapsmenu.list.cursor = downloadmapsmenu.list.numitems - 1;

	if (downloadmapsmenu.list.cursor < 0 && downloadmapsmenu.list.numitems > 0)
		downloadmapsmenu.list.cursor = 0;

	M_List_CenterCursor(&downloadmapsmenu.list);
}

static void M_DownloadMaps_Init(void)
{
	downloadmapsmenu.scrollbar_grab = false;
	downloadmapsmenu.prev_cursor = -2;
	downloadmapsmenu.list.viewsize = MAX_VIS_MAPS;
	downloadmapsmenu.list.cursor = -1;
	downloadmapsmenu.list.scroll = 0;
	downloadmapsmenu.list.numitems = 0;
	downloadmapsmenu.mapcount = 0;
	downloadmapsmenu.message[0] = '\0';
	downloadmapsmenu.message_time = 0.0;
	downloadmapsmenu.pending_download[0] = '\0';
	VEC_CLEAR(downloadmapsmenu.recent_downloads);
	VEC_CLEAR(downloadmapsmenu.filtered_indices);

	memset(&downloadmapsmenu.list.search, 0, sizeof(downloadmapsmenu.list.search));
	downloadmapsmenu.list.search.maxlen = 32;

	M_Ticker_Init(&downloadmapsmenu.ticker);

	if (QWMapList_LoadOnce())
		downloadmapsmenu.mapcount = QWMapList_Count();

	M_DownloadMaps_Refilter();

	if (downloadmapsmenu.list.cursor == -1)
		downloadmapsmenu.list.cursor = 0;

	M_List_CenterCursor(&downloadmapsmenu.list);
}

void M_Menu_DownloadMaps_f(void)
{
	if (!CL_QWMapListDownloadsAvailable())
	{
		S_LocalSound ("misc/menu3.wav");
		return;
	}

	key_dest = key_menu;
	downloadmapsmenu.prev = m_state;
	m_state = m_downloadmaps;
	m_entersound = true;
	M_DownloadMaps_Init();
}

void M_DownloadMaps_Draw(void)
{
	int x, y, i, cols;
	int firstvis, numvis;
	const char *selected_name;
	qboolean selected_already_have = false;
	qboolean message_active = false;

	if (!CL_QWMapListDownloadsAvailable())
	{
		M_Menu_Maps_f();
		return;
	}

	x = 16;
	y = 32;
	cols = 36;

	downloadmapsmenu.x = x;
	downloadmapsmenu.y = y;
	downloadmapsmenu.cols = cols;

	if (!keydown[K_MOUSE1])
		downloadmapsmenu.scrollbar_grab = false;

	if (downloadmapsmenu.mapcount <= 0 && QWMapList_LoadOnce())
	{
		int count = QWMapList_Count();
		if (count > 0)
		{
			downloadmapsmenu.mapcount = count;
			M_DownloadMaps_Refilter();
			if (downloadmapsmenu.list.cursor < 0)
				downloadmapsmenu.list.cursor = 0;
			M_List_CenterCursor(&downloadmapsmenu.list);
		}
	}

	M_DownloadMaps_UpdateCompletionState();
	selected_name = M_DownloadMaps_SelectedName();
	selected_already_have = M_DownloadMaps_AlreadyHave(selected_name);

	if (downloadmapsmenu.prev_cursor != downloadmapsmenu.list.cursor)
	{
		downloadmapsmenu.prev_cursor = downloadmapsmenu.list.cursor;
		M_Ticker_Init(&downloadmapsmenu.ticker);
	}
	else
		M_Ticker_Update(&downloadmapsmenu.ticker);

	M_DrawCountHeader(x, y - 28, cols, "Map Downloads",
		downloadmapsmenu.mapcount, "map", "maps");
	if (downloadmapsmenu.message[0])
	{
		if (realtime - downloadmapsmenu.message_time < 2.5)
			message_active = true;
		else
			downloadmapsmenu.message[0] = '\0';
	}
	M_DrawQuakeBar(x - 8, y - 16, cols + 2);

	if (downloadmapsmenu.mapcount <= 0)
	{
		if (QWMapList_IsRefreshing())
		{
			char buf[64];
			int dots = (int)(realtime * 2) % 4;
			q_snprintf(buf, sizeof(buf), "Fetching map list%.*s", dots, "...");
			M_Print(x, y, buf);
		}
		else if (QWMapList_State() == QW_MAPLIST_FAILED)
			M_Print(x, y, "qw_maps.txt not available");
		else
			M_Print(x, y, "No download maps");
	}

	M_List_GetVisibleRange(&downloadmapsmenu.list, &firstvis, &numvis);
	for (i = 0; i < numvis; i++)
	{
		int idx = i + firstvis;
		int map_idx = downloadmapsmenu.filtered_indices[idx];
		const char *name = QWMapList_NameAt(map_idx);
		char display_name[MAX_QPATH];
		qboolean selected = (idx == downloadmapsmenu.list.cursor);
		qboolean already_have;

		if (!name)
			continue;

		M_DownloadMaps_DisplayName(name, display_name, sizeof(display_name));
		already_have = M_DownloadMaps_AlreadyHave(name);

		if (M_DownloadMaps_NameIsActive(name))
		{
			M_DownloadMaps_DrawActiveDownload(x, y + i * 8, (cols - 2) * 8, display_name);
		}
		else if (already_have)
		{
			M_DownloadMaps_DrawInstalledMap(x, y + i * 8, (cols - 2) * 8,
				display_name,
				selected ? downloadmapsmenu.ticker.scroll_time : 0.0);
		}
		else if (downloadmapsmenu.list.search.len > 0)
		{
			M_PrintHighlightScroll(x, y + i * 8, (cols - 2) * 8,
				display_name,
				downloadmapsmenu.list.search.text,
				selected ? downloadmapsmenu.ticker.scroll_time : 0.0);
		}
		else
		{
			M_PrintScroll(x, y + i * 8, (cols - 2) * 8,
				display_name,
				selected ? downloadmapsmenu.ticker.scroll_time : 0.0,
				true);
		}

		if (selected)
			M_DrawCharacter(x - 8, y + i * 8, 12 + ((int)(realtime * 4) & 1));
	}

	if (M_List_GetOverflow(&downloadmapsmenu.list) > 0)
	{
		M_List_DrawScrollbar(&downloadmapsmenu.list, x + cols * 8 - 8, y);

		if (downloadmapsmenu.list.scroll > 0)
			M_DrawEllipsisBar(x, y - 8, cols);
		if (downloadmapsmenu.list.scroll + downloadmapsmenu.list.viewsize < downloadmapsmenu.list.numitems)
			M_DrawEllipsisBar(x, y + downloadmapsmenu.list.viewsize * 8, cols);
	}

	if (downloadmapsmenu.list.search.len == 0)
	{
		const char *tooltip = NULL;

		if (M_DownloadMaps_RecentDownloadActive(selected_name))
			tooltip = "successfully downloaded";
		else if (selected_already_have)
			tooltip = "already installed";
		else if (message_active)
			tooltip = downloadmapsmenu.message;

		if (tooltip)
			M_PrintWhite(x, y + downloadmapsmenu.list.viewsize * 8 + 16, tooltip);
	}

	if (downloadmapsmenu.list.search.len > 0)
	{
		M_DrawTextBox(16, 176, 32, 1);
		M_PrintHighlight(24, 184, downloadmapsmenu.list.search.text,
			downloadmapsmenu.list.search.text,
			downloadmapsmenu.list.search.len);
		{
			int cursor_x = 24 + 8 * downloadmapsmenu.list.search.len;
			if (downloadmapsmenu.list.numitems == 0)
				M_DrawCharacter(cursor_x, 184, 11 ^ 128);
			else
				M_DrawCharacter(cursor_x, 184, 10 + ((int)(realtime * 4) & 1));
		}
	}
}

qboolean M_DownloadMaps_Match(int index, char initial)
{
	int map_idx = downloadmapsmenu.filtered_indices[index];
	const char *name = QWMapList_NameAt(map_idx);
	char display_name[MAX_QPATH];

	if (!name)
		return false;

	M_DownloadMaps_DisplayName(name, display_name, sizeof(display_name));
	return display_name[0] && q_tolower(display_name[0]) == initial;
}

static const char *M_DownloadMaps_SelectedName(void)
{
	if (downloadmapsmenu.list.numitems <= 0 ||
		downloadmapsmenu.list.cursor < 0 ||
		downloadmapsmenu.list.cursor >= downloadmapsmenu.list.numitems)
		return NULL;

	return QWMapList_NameAt(downloadmapsmenu.filtered_indices[downloadmapsmenu.list.cursor]);
}

void M_DownloadMaps_Key(int key)
{
	int x, y;

	if (keydown[K_CTRL])
	{
		if ((key == 'u' || key == 'U') && downloadmapsmenu.list.search.len > 0)
		{
			downloadmapsmenu.list.search.len = 0;
			downloadmapsmenu.list.search.text[0] = 0;
			M_DownloadMaps_Refilter();
			return;
		}
		else if (key == K_BACKSPACE && downloadmapsmenu.list.search.len > 0)
		{
			M_DeletePrevWord(&downloadmapsmenu.list.search);
			M_DownloadMaps_Refilter();
			return;
		}
	}

	if (key >= 32 && key < 127)
	{
		if (downloadmapsmenu.list.search.len < downloadmapsmenu.list.search.maxlen)
		{
			downloadmapsmenu.list.search.text[downloadmapsmenu.list.search.len++] = key;
			downloadmapsmenu.list.search.text[downloadmapsmenu.list.search.len] = 0;
			M_DownloadMaps_Refilter();
			return;
		}
	}

	if (downloadmapsmenu.scrollbar_grab)
	{
		switch (key)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			downloadmapsmenu.scrollbar_grab = false;
			break;
		}
		return;
	}

	if (M_List_Key(&downloadmapsmenu.list, key))
		return;

	if (M_List_CycleMatch(&downloadmapsmenu.list, key, M_DownloadMaps_Match))
		return;

	if (M_Ticker_Key(&downloadmapsmenu.ticker, key))
		return;

	switch (key)
	{
	case K_ESCAPE:
		if (downloadmapsmenu.list.search.len > 0)
		{
			downloadmapsmenu.list.search.len = 0;
			downloadmapsmenu.list.search.text[0] = 0;
			M_DownloadMaps_Refilter();
			return;
		}
		/* fall through */
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		M_Menu_Maps_f();
		break;

	case K_BACKSPACE:
		if (downloadmapsmenu.list.search.len > 0)
		{
			downloadmapsmenu.list.search.text[--downloadmapsmenu.list.search.len] = 0;
			M_DownloadMaps_Refilter();
			return;
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	enter:
		{
			const char *name = M_DownloadMaps_SelectedName();
			if (!CL_QWMapListDownloadsAvailable())
			{
				S_LocalSound("misc/menu3.wav");
				M_Menu_Maps_f();
			}
			else if (name && *name)
			{
				if (M_DownloadMaps_AlreadyHave(name))
				{
					char display_name[MAX_QPATH];
					M_DownloadMaps_DisplayName(name, display_name, sizeof(display_name));
					S_LocalSound("misc/menu3.wav");
					downloadmapsmenu.message[0] = '\0';
					Con_Printf("Map already downloaded: %s\n", display_name);
				}
				else
				{
					q_strlcpy(downloadmapsmenu.pending_download, name,
						sizeof(downloadmapsmenu.pending_download));
					Cbuf_AddText(va("download \"%s\"\n", name));
					M_DownloadMaps_SetMessage("Downloading...");
				}
			}
			else
			{
				S_LocalSound("misc/menu3.wav");
			}
		}
		break;

	case K_MOUSE1:
		x = m_mousex - downloadmapsmenu.x - (downloadmapsmenu.cols - 1) * 8;
		y = m_mousey - downloadmapsmenu.y;
		if (x < -8 || !M_List_UseScrollbar(&downloadmapsmenu.list, y))
			goto enter;
		downloadmapsmenu.scrollbar_grab = true;
		M_DownloadMaps_Mousemove(m_mousex, m_mousey);
		break;

	default:
		break;
	}
}

void M_DownloadMaps_Mousemove(int cx, int cy)
{
	cy -= downloadmapsmenu.y;

	if (downloadmapsmenu.scrollbar_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			downloadmapsmenu.scrollbar_grab = false;
			return;
		}
		M_List_UseScrollbar(&downloadmapsmenu.list, cy);
	}

	M_List_Mousemove(&downloadmapsmenu.list, cy);
}

/*
==================
Skill Menu (iw)
==================
*/

int				m_skill_cursor;
qboolean		m_skill_usegfx;
qboolean		m_skill_usecustomtitle;
qboolean		m_skill_canresume;
time_t			m_skill_lastplayed;
int				m_skill_numoptions;
char			m_skill_mapname[MAX_QPATH];
char			m_skill_maptitle[1024];
menuticker_t	m_skill_ticker;

enum m_state_e m_skill_prevmenu;

void M_SetSkillMenuMap(const char* name)
{
	q_strlcpy(m_skill_mapname, name, sizeof(m_skill_mapname));
	if (!Mod_LoadMapDescription(m_skill_maptitle, sizeof(m_skill_maptitle), name) || !m_skill_maptitle[0])
		q_strlcpy(m_skill_maptitle, name, sizeof(m_skill_maptitle));
}

static void M_DescribeDuration(char *out, size_t outsize, double seconds)
{
	const double minute = 60.0;
	const double hour = 60.0 * minute;
	const double day = 24.0 * hour;
	const double week = 7.0 * day;
	const double month = 30.436875 * day;
	const double year = 365.2425 * day;
	const char	*unit;
	int			count;

	if (seconds < 0.0)
		seconds = -seconds;

	if (seconds < 1.0)
	{
		q_strlcpy(out, "moments", outsize);
		return;
	}
	else if (seconds < minute)
	{
		count = (int)seconds;
		unit = "second";
	}
	else if (seconds < 90.0 * minute)
	{
		count = (int)(seconds / minute);
		unit = "minute";
	}
	else if (seconds < day)
	{
		count = (int)(seconds / hour);
		unit = "hour";
	}
	else if (seconds < week)
	{
		count = (int)(seconds / day);
		unit = "day";
	}
	else if (seconds < month)
	{
		count = (int)(seconds / week);
		unit = "week";
	}
	else if (seconds < year)
	{
		count = (int)(seconds / month);
		unit = "month";
	}
	else
	{
		count = (int)(seconds / year);
		unit = "year";
	}

	q_snprintf(out, outsize, "%i %s%s", count, unit, count == 1 ? "" : "s");
}

void M_Menu_Skill_f(void)
{
	char autosave[MAX_OSPATH];

	m_skill_canresume = false;
	m_skill_lastplayed = 0;
	Host_WaitForSaveThread ();
	q_snprintf(autosave, sizeof(autosave), "%s/saves/autosave/%s.sav", com_gamedir, m_skill_mapname);
	if (Sys_FileType(autosave) & FS_ENT_FILE)
	{
		time_t now, lastplayed;
		m_skill_canresume = true;
		time(&now);
		if (Sys_GetFileTime(autosave, &lastplayed) && lastplayed <= now)
			m_skill_lastplayed = lastplayed;
	}

	key_dest = key_menu;
	m_skill_prevmenu = m_state;
	m_state = m_skill;
	m_entersound = true;
	M_Ticker_Init(&m_skill_ticker);

	if (m_skill_canresume)
	{
		m_skill_cursor = 4;
	}
	else
	{
		// Select current skill level initially if there's no autosave
		m_skill_cursor = (int)skill.value;
		m_skill_cursor = CLAMP(0, m_skill_cursor, 3);
	}
	
	m_skill_numoptions = 4 + m_skill_canresume;
}

void M_Skill_Draw(void)
{
	int		x, y, f;
	qpic_t* p;

	M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
	p = Draw_CachePic(m_skill_usecustomtitle && !m_skill_canresume ? "gfx/p_skill.lmp" : "gfx/ttl_sgl.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	x = 72;
	y = 32;

	M_Ticker_Update(&m_skill_ticker);
	M_PrintScroll(x, 32, 30 * 8, m_skill_maptitle, m_skill_ticker.scroll_time, false);

	y += 16;

	if (m_skill_usegfx)
	{
		M_DrawTransPic(x, y, Draw_CachePic("gfx/skillmenu.lmp"));
		if (m_skill_cursor < 4)
			M_DrawQuakeCursor(x - 18, y + m_skill_cursor * 20);
		y += 4 * 20;
	}
	else
	{
		static const char* const skills[] =
		{
			"EASY",
			"NORMAL",
			"HARD",
			"NIGHTMARE",
		};

		for (f = 0; f < 4; f++)
			M_Print(x, y + f * 16 + 2, skills[f]);
		if (m_skill_cursor < 4)
			M_DrawArrowCursor(x - 16, y + m_skill_cursor * 16 + 4);
		y += 4 * 16;
	}

	if (m_skill_canresume)
	{
		y += 8;
		M_Print(x, y, "Resume last game");
		if (m_skill_lastplayed)
		{
			char	duration[32];
			time_t	now;

			time(&now);
			M_DescribeDuration(duration, sizeof(duration), difftime(now, m_skill_lastplayed));
			M_Print(x, y + 8, va("from %s ago", duration));
		}
		if (m_skill_cursor == 4)
			M_DrawArrowCursor(x - 16, y);
	}
}

static double skill_last_key_time = 0.0; // Tracks last key time for 'ni' combo
static qboolean skill_was_n = false;    // Tracks if the last key was 'n'

void M_Skill_Key(int key)
{
	if (M_Ticker_Key(&m_skill_ticker, key))
		return;

	switch (key)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		m_state = m_skill_prevmenu;
		m_entersound = true;
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		if (++m_skill_cursor > m_skill_numoptions - 1)
			m_skill_cursor = 0;
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		if (--m_skill_cursor < 0)
			m_skill_cursor = m_skill_numoptions - 1;
		break;

	case 'e': // Shortcut for Easy
	case 'E':
		m_skill_cursor = 0;
		S_LocalSound("misc/menu1.wav");
		skill_was_n = false; // Reset the flag
		break;

	case 'n': // Shortcut for Normal and cycling behavior
	case 'N':
		if (m_skill_cursor == 1) // Already on Normal
		{
			skill_last_key_time = 0.0; // Reset time to avoid combo with 'i'
			skill_was_n = false;
			m_skill_cursor = 3; // Move to Nightmare
			S_LocalSound("misc/menu1.wav");
		}
		else
		{
			skill_last_key_time = realtime; // Record time for 'ni' combo
			skill_was_n = true;
			m_skill_cursor = 1; // Move to Normal
			S_LocalSound("misc/menu1.wav");
		}
		break;

	case 'h': // Shortcut for Hard
	case 'H':
		m_skill_cursor = 2;
		S_LocalSound("misc/menu1.wav");
		skill_was_n = false; // Reset the flag
		break;

	case 'i': // Shortcut for Nightmare (only if preceded by 'n')
	case 'I':
		if (skill_was_n && (realtime - skill_last_key_time) < 0.5) // 500ms window for 'ni'
		{
			m_skill_cursor = 3; // Nightmare
			S_LocalSound("misc/menu1.wav");
		}
		skill_was_n = false; // Reset the flag
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1:
		key_dest = key_game;
		if (sv.active)
			Cbuf_AddText("disconnect\n");
		if (m_skill_cursor == 4)
		{
			Cbuf_AddText(va("load \"autosave/%s\"\n", m_skill_mapname));
		}
		else
		{
			// Fresh start
			Cbuf_AddText(va("skill %d\n", m_skill_cursor));
			Cbuf_AddText("maxplayers 1\n");
			Cbuf_AddText("deathmatch 0\n"); //johnfitz
			Cbuf_AddText("coop 0\n"); //johnfitz
			Cbuf_AddText(va("map \"%s\"\n", m_skill_mapname));
		}
		break;
	}
}

void M_Skill_Mousemove(int cx, int cy)
{
	int ybase = 48;
	int itemheight = m_skill_usegfx ? 20 : 16;

	if (m_skill_numoptions > 4 && cy > ybase + 4 * itemheight + 8 / 2)
		m_skill_cursor = 4;
	else
		M_UpdateCursor(cy, ybase, itemheight, 4, &m_skill_cursor);

}

/*
==================
Multiplayer Menu
==================
*/

int	m_multiplayer_cursor;
#define	MULTIPLAYER_BASE_ITEMS	3
#define	MAX_PINNED_BOOKMARKS	5
#define	MULTIPLAYER_PINNED_OFFSET_Y	6
#define	MULTIPLAYER_PINNED_SPACING	10
extern cvar_t scr_shownet; // woods

#define	BOOKMARK_ALIAS_LENGTH	BOOKMARK_DATA_LENGTH

typedef struct pinnedbookmark_s {
	char	name[MAX_QPATH];
	char	alias[BOOKMARK_ALIAS_LENGTH];
} pinnedbookmark_t;

// Forward declarations for pinned bookmark helpers
static int M_Bookmarks_CountPinned(void);
static int M_Bookmarks_GetPinned(pinnedbookmark_t* out, int max_pins);
static int M_MultiPlayer_TotalItems(void);

static int M_Bookmarks_CountPinned(void)
{
	int count = 0;
	for (filelist_item_t* item = bookmarkslist; item; item = item->next)
	{
		char alias[BOOKMARK_ALIAS_LENGTH];
		qboolean pinned = false;
		BookmarkData_Parse(item->data, alias, sizeof(alias), &pinned);
		if (pinned && alias[0])
			count++;
	}
	return count;
}

static int PinnedBookmarkCompare(const void* a, const void* b)
{
	const pinnedbookmark_t* itemA = (const pinnedbookmark_t*)a;
	const pinnedbookmark_t* itemB = (const pinnedbookmark_t*)b;
	return q_strcasecmp(itemA->alias, itemB->alias);
}

static int M_Bookmarks_GetPinned(pinnedbookmark_t* out, int max_pins)
{
	int count = 0;

	if (!out || max_pins <= 0)
		return 0;

	for (filelist_item_t* item = bookmarkslist; item && count < max_pins; item = item->next)
	{
		pinnedbookmark_t entry;
		qboolean pinned = false;

		BookmarkData_Parse(item->data, entry.alias, sizeof(entry.alias), &pinned);
		if (!pinned || !entry.alias[0])
			continue;

		q_strlcpy(entry.name, item->name, sizeof(entry.name));
		out[count++] = entry;
	}

	if (count > 1)
		qsort(out, count, sizeof(*out), PinnedBookmarkCompare);

	return count;
}

static int M_MultiPlayer_TotalItems(void)
{
	int total = MULTIPLAYER_BASE_ITEMS;
	pinnedbookmark_t pinned[MAX_PINNED_BOOKMARKS];
	int pinned_count = M_Bookmarks_GetPinned(pinned, MAX_PINNED_BOOKMARKS);

	return total + pinned_count;
}

static int M_MultiPlayer_FirstPinnedY(void)
{
	return 32 + MULTIPLAYER_BASE_ITEMS * 20 + MULTIPLAYER_PINNED_OFFSET_Y;
}

void M_Menu_MultiPlayer_f (void)
{
	key_dest = key_menu;
	m_state = m_multiplayer;
	m_entersound = true;
	IN_UpdateGrabs();
}

extern char	lastmphost[NET_NAMELEN]; // woods - connected server address

void M_MultiPlayer_Draw (void)
{
	int		f, i; // woods
	qpic_t	*p;
	pinnedbookmark_t pinned[MAX_PINNED_BOOKMARKS];
	int pinned_count = M_Bookmarks_GetPinned(pinned, MAX_PINNED_BOOKMARKS);
	int total_items = M_MultiPlayer_TotalItems();

	if (total_items <= 0)
		total_items = MULTIPLAYER_BASE_ITEMS;

	if (m_multiplayer_cursor >= total_items)
		m_multiplayer_cursor = total_items - 1;

	M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp") );
	p = Draw_CachePic ("gfx/p_multi.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);
	M_DrawTransPic (72, 32, Draw_CachePic ("gfx/mp_menu.lmp") );

	f = (int)(realtime * 10)%6;
	i = 24;
	if (strlen(lastmphost) > i)
		i = (strlen(lastmphost));

	// Draw cursor - use rotating Q for base items, rotated arrow for pinned items
	if (m_multiplayer_cursor < MULTIPLAYER_BASE_ITEMS)
		M_DrawTransPic (54, 32 + m_multiplayer_cursor * 20,Draw_CachePic( va("gfx/menudot%i.lmp", f+1 ) ) );

	// Draw pinned bookmarks below base items
	for (i = 0; i < pinned_count && i < MAX_PINNED_BOOKMARKS; ++i)
	{
		int row = MULTIPLAYER_BASE_ITEMS + i;
		int y = M_MultiPlayer_FirstPinnedY() + i * MULTIPLAYER_PINNED_SPACING;
		qboolean selected = (m_multiplayer_cursor == row);

		// Show arrow at 0  when selected (pointing right), 90  when not (pointing down)
		Draw_Character_Rotation(80, y, 141, selected ? 0 : 90);
		if (selected)
			M_PrintWhite(96, y, pinned[i].alias);
		else
			M_Print(96, y, pinned[i].alias);
	}

        // Draw "currently connected to" below pinned bookmarks
        if (cl.maxclients > 1 && cls.state == ca_connected && !cls.demoplayback)
        {
                int conn_y = M_MultiPlayer_FirstPinnedY() + pinned_count * MULTIPLAYER_PINNED_SPACING + MULTIPLAYER_PINNED_OFFSET_Y;
                int box_width = strlen(lastmphost);
                if (box_width < 24)
                        box_width = 24;
                f = (320 - 26 * 8) / 2;
                M_DrawTextBox(f, conn_y, box_width, 2);
                f += 8;
                M_Print(f, conn_y + 8, "currently connected to:");

                if (realtime - cl.last_received_message > 5.0)
                        M_PrintRGBA(f, conn_y + 16, lastmphost, CL_PLColours_Parse("0xffffff"), 0.2f, false);
                else
                        M_PrintWhite(f, conn_y + 16, lastmphost);
        }

	if (ipxAvailable || ipv4Available || ipv6Available)
		return;
	M_PrintWhite ((320/2) - ((27*8)/2), 148, "No Communications Available");
}


void M_MultiPlayer_Key (int key)
{
	switch (key)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2: // woods #mousemenu
		M_Menu_Main_f ();
		break;

	case 'j':
	case 'J':
		m_multiplayer_cursor = 0;  // Join Game
		S_LocalSound ("misc/menu1.wav");
		break;

	case 'n':
	case 'N':
		m_multiplayer_cursor = 1;  // New Game
		S_LocalSound ("misc/menu1.wav");
		break;

	case 's':
	case 'S':
		m_multiplayer_cursor = 2;  // Setup
		S_LocalSound ("misc/menu1.wav");
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		if (++m_multiplayer_cursor >= M_MultiPlayer_TotalItems())
			m_multiplayer_cursor = 0;
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		if (--m_multiplayer_cursor < 0)
			m_multiplayer_cursor = M_MultiPlayer_TotalItems() - 1;
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1: // woods #mousemenu
		m_entersound = true;
		switch (m_multiplayer_cursor)
		{
		case 0:
			if (ipxAvailable || ipv4Available || ipv6Available)
				M_Menu_LanConfig_f (); // woods #skipipx
			break;

		case 1:
			if (ipxAvailable || ipv4Available || ipv6Available)
				M_Menu_LanConfig_f (); // woods #skipipx
			break;

		case 2:
			M_Menu_Setup_f ();
			break;

		default:
			// Handle pinned bookmarks
			if (m_multiplayer_cursor >= MULTIPLAYER_BASE_ITEMS)
			{
				int index = m_multiplayer_cursor - MULTIPLAYER_BASE_ITEMS;
				pinnedbookmark_t pinned[MAX_PINNED_BOOKMARKS];
				int count = M_Bookmarks_GetPinned(pinned, MAX_PINNED_BOOKMARKS);
				if (index < count)
				{
						m_return_state = m_state;
						m_return_onerror = true;
						key_dest = key_game;
						m_state = m_none;
						IN_UpdateGrabs();
						CL_MarkNextConnectFromMenu();
						Cbuf_AddText(va("connect \"%s\"\n", pinned[index].name));
					}
				}
				break;
		}
	}
}

void M_MultiPlayer_Mousemove(int cx, int cy) // woods #mousemenu
{
	pinnedbookmark_t pinned[MAX_PINNED_BOOKMARKS];
	int pinned_count = M_Bookmarks_GetPinned(pinned, MAX_PINNED_BOOKMARKS);
	int first_pinned_y = M_MultiPlayer_FirstPinnedY();

	(void)cx;

	if (pinned_count > 0 && cy >= first_pinned_y)
	{
		M_UpdateCursor(cy, first_pinned_y, MULTIPLAYER_PINNED_SPACING, pinned_count, &m_multiplayer_cursor);
		m_multiplayer_cursor += MULTIPLAYER_BASE_ITEMS;
		return;
	}

	M_UpdateCursor(cy, 32, 20, MULTIPLAYER_BASE_ITEMS, &m_multiplayer_cursor);
}

/*
==================
Setup Menu
==================
*/

static qboolean M_Menu_TabCompleteNameHistory(menu_textfield_t *field,
	char *buffer, size_t buffer_size,
	char *tab_partial, size_t tab_partial_size); // woods #namehistory

static int		setup_cursor = 6; // woods 4 to 5 #

static int		setup_cursor_table[] = {40, 56, 72, 88, 104, 128, 158}; // woods add value, change position #namemaker #colorbar

static void (*colorpicker_return_fn)(void);

char	namemaker_name[16]; // woods #namemaker
qboolean namemaker_shortcut = false; // woods #namemaker
qboolean from_namemaker = false; // woods #namemaker

static menu_textfield_t namemaker_name_field;
static qboolean namemaker_edit_active = false;
static char	namemaker_name_tabpartial[16]; // woods #namehistory
static char	namemaker_name_hint[16]; // woods #namehistory
static char	setup_hostname[16];
static char	setup_myname[16];
static menu_textfield_t setup_hostname_field;
static menu_textfield_t setup_myname_field;
static char	setup_myname_tabpartial[16]; // woods #namehistory
static char	setup_myname_hint[16]; // woods #namehistory
static plcolour_t	setup_oldtop;
static plcolour_t	setup_oldbottom;
static plcolour_t	setup_top;
static plcolour_t	setup_bottom;
extern qboolean	keydown[];


//http://axonflux.com/handy-rgb-to-hsl-and-rgb-to-hsv-color-model-c
static void rgbtohsv(byte *rgb, vec3_t result)
{	//helper for the setup menu
	int r = rgb[0], g = rgb[1], b = rgb[2];
	float maxc = q_max(r, q_max(g, b)), minc = q_min(r, q_min(g, b));
    float h, s, l = (maxc + minc) / 2;

	float d = maxc - minc;
	if (maxc)
		s = d / maxc;
	else
		s = 0;

	if(maxc == minc)
	{
		h = 0; // achromatic
	}
	else
	{
		if (maxc == r)
			h = (g - b) / d + ((g < b) ? 6 : 0);
		else if (maxc == g)
			h = (b - r) / d + 2;
		else
			h = (r - g) / d + 4;
		h /= 6;
    }

	result[0] = h;
	result[1] = s;
	result[2] = l;
};
//http://axonflux.com/handy-rgb-to-hsl-and-rgb-to-hsv-color-model-c
static void hsvtorgb(float inh, float s, float v, byte *out)
{	//helper for the setup menu
	int r, g, b;
	float h = inh - (int)floor(inh);
	int i = h * 6;
	float f = h * 6 - i;
	float p = v * (1 - s);
	float q = v * (1 - f * s);
	float t = v * (1 - (1 - f) * s);
	switch(i)
	{
	default:
	case 0: r = v*0xff, g = t*0xff, b = p*0xff; break;
	case 1: r = q*0xff, g = v*0xff, b = p*0xff; break;
	case 2: r = p*0xff, g = v*0xff, b = t*0xff; break;
	case 3: r = p*0xff, g = q*0xff, b = v*0xff; break;
	case 4: r = t*0xff, g = p*0xff, b = v*0xff; break;
	case 5: r = v*0xff, g = p*0xff, b = q*0xff; break;
	}

	out[0] = r;
	out[1] = g;
	out[2] = b;
};

qboolean rgbactive; // woods
qboolean colordelta; // woods

void M_AdjustColour(plcolour_t *tr, int dir)
{
	if (keydown[K_SHIFT])
	{
		rgbactive = true; // woods
		vec3_t hsv;
		rgbtohsv(CL_PLColours_ToRGB(tr), hsv);

		hsv[0] += dir/128.0;
		hsv[1] = 1;
		hsv[2] = 1;	//make these consistent and not inherited from any legacy colours. we're persisting in rgb with small hue changes so we can't actually handle greys, so whack the saturation and brightness right up.
		tr->type = 2;	//rgb...
		tr->basic = 0;	//no longer relevant.
		hsvtorgb(hsv[0], hsv[1], hsv[2], tr->rgb);
	}
	else
	{
		tr->type = 1;
		if (tr->basic+dir < 0)
			tr->basic = 13;
		else if (tr->basic+dir > 13)
			tr->basic = 0;
		else
			tr->basic += dir;
	}
}

static menu_textfield_t *M_Setup_GetFieldForCursor(void)
{
	if (setup_cursor == 0)
		return &setup_hostname_field;
	if (setup_cursor == 1)
		return &setup_myname_field;
	return NULL;
}

static void M_Setup_ClearTextSelections(void)
{
	M_TextField_ClearSelection(&setup_hostname_field);
	M_TextField_ClearSelection(&setup_myname_field);
}

static void M_Menu_UpdateNameHistoryHint(const char *name, char *hint, size_t hint_size) // woods #namehistory
{
	extern char unfun[129];
	filelist_item_t *item;
	int len = (int)strlen(name);
	char unfun_prefix[MAXCMDLINE];
	char unfun_name[32];
	int i;

	if (!hint_size)
		return;

	hint[0] = '\0';

	if (len <= 0)
		return;

	for (i = 0; i < len && i < (int)sizeof(unfun_prefix) - 1; i++)
		unfun_prefix[i] = unfun[name[i] & 127];
	unfun_prefix[i] = '\0';

	for (item = namehistorylist; item; item = item->next)
	{
		for (i = 0; item->name[i] && i < (int)sizeof(unfun_name) - 1; i++)
			unfun_name[i] = unfun[item->name[i] & 127];
		unfun_name[i] = '\0';

		if (!q_strncasecmp(unfun_name, unfun_prefix, len))
		{
			q_strlcpy(hint, item->name + len, hint_size);
			return;
		}
	}
}

static void M_Setup_UpdateNameHint(void) // woods #namehistory
{
	M_Menu_UpdateNameHistoryHint(setup_myname, setup_myname_hint, sizeof(setup_myname_hint));
}

#define	NUM_SETUP_CMDS	7 // woods 5 to 6 #namemaker
void M_Menu_Setup_f (void)
{
	key_dest = key_menu;
	m_state = m_setup;
	m_entersound = true;
	if (from_namemaker) // woods #namemaker
		from_namemaker = !from_namemaker;
	else
		Q_strcpy(setup_myname, cl_name.string);
	Q_strcpy(setup_hostname, hostname.string);
	setup_top = setup_oldtop = CL_PLColours_Parse(cl_topcolor.string);
	setup_bottom = setup_oldbottom = CL_PLColours_Parse(cl_bottomcolor.string);
	M_TextField_Init(&setup_hostname_field, setup_hostname, 15, false);
	M_TextField_Init(&setup_myname_field, setup_myname, 15, false);
	setup_myname_tabpartial[0] = '\0'; // woods #namehistory
	M_Setup_UpdateNameHint(); // woods #namehistory

	IN_UpdateGrabs();
}

qboolean chasewasnotactive; // woods #3rdperson
qboolean flyme; // woods #3rdperson

void M_DrawColorBar_Top (int x, int y, int highlight) // woods #colorbar -- mh
{
	int i;
	int intense = highlight * 16 + (highlight < 8 ? 11 : 4);

	if (setup_top.type == 2)
	{
		Draw_FillPlayer (x, y + 4, 8, 8, setup_top, 1);
	}
	else
	{
		// position correctly
		x = 64;

		for (i = 0; i < 14; i++)
		{
			// take the approximate midpoint colour (handle backward ranges)
			int c = i * 16 + (i < 8 ? 8 : 7);

			// braw baseline colour (offset downwards a little so that it fits correctly
			Draw_Fill(x + i * 8, y + 4, 8, 8, c, 1);
		}

		// draw the highlight rectangle
		Draw_Fill(x - 1 + highlight * 8, y + 3, 10, 10, 15, 1);

		// redraw the highlighted color at brighter intensity
		Draw_Fill(x + highlight * 8, y + 4, 8, 8, intense, 1);
	}
}

void M_DrawColorBar_Bot (int x, int y, int highlight) // woods #colorbar -- mh
{
	int i;
	int intense = highlight * 16 + (highlight < 8 ? 11 : 4);

	if (setup_bottom.type == 2)
	{
		Draw_FillPlayer (x, y + 4, 8, 8, setup_bottom, 1);
	}
	else
	{
		// position correctly
		x = 64;

		for (i = 0; i < 14; i++)
		{
			// take the approximate midpoint colour (handle backward ranges)
			int c = i * 16 + (i < 8 ? 8 : 7);

			// braw baseline colour (offset downwards a little so that it fits correctly
			Draw_Fill(x + i * 8, y + 4, 8, 8, c, 1);
		}

		// draw the highlight rectangle
		Draw_Fill(x - 1 + highlight * 8, y + 3, 10, 10, 15, 1);

		// redraw the highlighted color at brighter intensity
		Draw_Fill(x + highlight * 8, y + 4, 8, 8, intense, 1);
	}
}

void M_Setup_Draw (void)
{
	qpic_t	*p;

	M_TextField_CheckMouseRelease();

	if (cls.state == ca_connected)
	{
		char buf[15];
		char buf2[15];
		const char* obs;
		const char* star_obs;
		const char *userinfo = CL_GetSafeRealViewEntityUserinfo();
		obs = Info_GetKey(userinfo, "observer", buf, sizeof(buf));
		star_obs = Info_GetKey(userinfo, "*observer", buf2, sizeof(buf2));

		if (!strcmp(obs, "fly") || !strcmp(star_obs, "fly")) // woods #3rdperson
			flyme = true;
		else
			flyme = false;
	}

	if (!chase_active.value && !cls.demoplayback&& host_initialized && !flyme && cls.state == ca_connected && cl.modtype != 6) // woods #3rdperson
	{
		chasewasnotactive = true;
		Cbuf_AddText("chase_active 1\n");
	}

	M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp") );
	p = Draw_CachePic ("gfx/p_multi.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);

	M_Print (64, 40, "Hostname");
	M_DrawTextBox (160, 32, 16, 1);
	M_TextField_DrawHighlight(&setup_hostname_field, 168, 40);
	M_Print (168, 40, setup_hostname);

	M_Print (64, 56, "Your name");
	M_DrawTextBox (160, 48, 16, 1);
	M_TextField_DrawHighlight(&setup_myname_field, 168, 56);
	M_PrintWhite (168, 56, setup_myname); // woods change to white #namemaker
	if (setup_cursor == 1 && // woods #namehistory
		setup_myname_hint[0] &&
		setup_myname_field.cursor == (int)strlen(setup_myname))
	{
		int hint_x = 168 + (int)strlen(setup_myname) * 8;
		M_PrintRGBA(hint_x, 56, setup_myname_hint, CL_PLColours_Parse("0xffffff"), 0.5f, false);
	}

	M_Print(64, 72, "Name Maker"); // woods #namemaker

	M_Print(64, 88, "Color Picker");

	M_Print (64, 104, "Shirt -"); // woods 80 to 104 #namemaker #showcolornum
	M_PrintWhite (126, 104, CL_PLColours_ToString (setup_top)); // woods #showcolornum
	M_DrawColorBar_Top (64, 110, atoi(CL_PLColours_ToString (setup_top))); // woods #colorbar
	M_Print (64, 128, "Pants -"); // woods 104 to 128 #namemaker #showcolornum
	M_PrintWhite (126, 128, CL_PLColours_ToString (setup_bottom)); // woods #showcolornum
	M_DrawColorBar_Bot (64, 134, atoi(CL_PLColours_ToString (setup_bottom))); // woods #colorbar

	if (!rgbactive && (setup_cursor == 4 || setup_cursor == 5)) // woods
		M_PrintRGBA (64, 178, "+shift for RGB colors", CL_PLColours_Parse ("0xffffff"), 0.6f, false); // woods

	M_DrawTextBox (64, 150, 14, 1);  // woods 140 to 152 #namemaker
	M_Print (72, 158, "Accept Changes"); // woods #colorbar

	p = Draw_CachePic ("gfx/bigbox.lmp");
	M_DrawTransPic (196, 77, p); // woods #colorbar

	// woods #spinnymodel

	qpic_t* menup = Draw_CachePic("gfx/menuplyr.lmp");

	// Normalize setup colors to a canonical form
	setup_top = CL_PLColours_Parse(CL_PLColours_ToString(setup_top));
	setup_bottom = CL_PLColours_Parse(CL_PLColours_ToString(setup_bottom));

	// If RGB colours are used, provide true RGB to preview; else use legacy indices
	if (setup_top.type == 2 || setup_bottom.type == 2)
	{
		PR_SetMenuPreviewRGBColors(
			setup_top.rgb[0], setup_top.rgb[1], setup_top.rgb[2],
			setup_bottom.rgb[0], setup_bottom.rgb[1], setup_bottom.rgb[2]);
	}
	else
	{
		int top_legacy = setup_top.basic;
		int bot_legacy = setup_bottom.basic;
		PR_SetMenuPreviewLegacyColors(top_legacy, bot_legacy);
	}

	// Draw spinning player model aligned to CANVAS_MENU pixels
	int boxw = menup ? menup->width : 96;
	int boxh = menup ? menup->height : 96;
	vrect_t bounds, vp;
	Draw_GetMenuTransform(&bounds, &vp);
	// Convert menu virtual coords (640x200) to absolute pixels via viewport
	float s = (float)vp.width / (float)bounds.width;
	float px = vp.x + 208 * s;
	float py = vp.y + 80 * s;
	float pw = boxw * s;
	float ph = boxh * s;
	DrawSpinningModelToMenuPixels("progs/player.mdl",
		px, py, pw, ph,
		25.0f,
		0.0f,
		0, 0, 0, 0);

	// Restore menu 2D canvas to keep coordinates/cursor aligned
	GL_SetCanvas(CANVAS_MENU);
	glDisable(GL_BLEND);
	glEnable(GL_ALPHA_TEST);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

	// Clear the overrides so other draws are unaffected
	PR_SetMenuPreviewLegacyColors(-1, -1);
	PR_SetMenuPreviewRGBColors(-1, -1, -1, -1, -1, -1);

	M_DrawCharacter (56, setup_cursor_table [setup_cursor], 12+((int)(realtime*4)&1));

	if (setup_cursor == 0)
		M_TextField_DrawCursor(&setup_hostname_field, 168, setup_cursor_table[setup_cursor]);

	if (setup_cursor == 1)
		M_TextField_DrawCursor(&setup_myname_field, 168, setup_cursor_table[setup_cursor]);
}

char lastColorSelected[10]; // woods

void M_Setup_Key (int k)
{
	menu_textfield_t *active_field = M_Setup_GetFieldForCursor();
	if (active_field && M_TextField_Key(active_field, k))
	{
		if (active_field == &setup_myname_field)
		{
			setup_myname_tabpartial[0] = '\0'; // woods #namehistory
			M_Setup_UpdateNameHint();
		}
		return;
	}

	if (k == K_TAB && setup_cursor == 1) // woods #namehistory
	{
		if (M_Menu_TabCompleteNameHistory(&setup_myname_field, setup_myname,
			sizeof(setup_myname), setup_myname_tabpartial, sizeof(setup_myname_tabpartial)))
			S_LocalSound("misc/menu2.wav");
		M_Setup_UpdateNameHint();
		return;
	}

	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2: // woods #mousemenu
		if (chasewasnotactive && !cls.demoplayback && host_initialized && !flyme) // woods #3rdperson
		{
			chasewasnotactive = false;
			Cbuf_AddText("chase_active 0\n");
		}
		if (colordelta)
		{
			colordelta = false;
			Cbuf_AddText(va("color %s %s\n", CL_PLColours_ToString(setup_oldtop), CL_PLColours_ToString(setup_oldbottom)));
		}
		M_Menu_MultiPlayer_f ();
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		M_Setup_ClearTextSelections();
		setup_myname_tabpartial[0] = '\0'; // woods #namehistory
		setup_cursor--;
		if (setup_cursor < 0)
			setup_cursor = NUM_SETUP_CMDS-1;
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		M_Setup_ClearTextSelections();
		setup_myname_tabpartial[0] = '\0'; // woods #namehistory
		setup_cursor++;
		if (setup_cursor >= NUM_SETUP_CMDS)
			setup_cursor = 0;
		break;

	case K_MWHEELDOWN:
	case K_LEFTARROW:
		if (setup_cursor < 2)
			return;
		S_LocalSound ("misc/menu3.wav");
		if (setup_cursor == 4) // 2 to 3 woods #namemaker
		{
			M_AdjustColour(&setup_top, -1);
			q_strlcpy (lastColorSelected, CL_PLColours_ToString(setup_top), sizeof(lastColorSelected));
			if (chase_active.value && !cls.demoplayback && host_initialized && !flyme) // woods #3rdperson
				if (!CL_PLColours_Equals(setup_top, setup_oldtop) || !CL_PLColours_Equals(setup_bottom, setup_oldbottom))
				{
					Cbuf_AddText(va("color %s %s\n", CL_PLColours_ToString(setup_top), CL_PLColours_ToString(setup_bottom)));
					colordelta = true;
				}
		}
		if (setup_cursor == 5) // 3 to 4 woods #namemaker
		{
			M_AdjustColour(&setup_bottom, -1);
			q_strlcpy (lastColorSelected, CL_PLColours_ToString(setup_bottom), sizeof(lastColorSelected));
			if (chase_active.value && !cls.demoplayback && host_initialized && !flyme) // woods #3rdperson
				if (!CL_PLColours_Equals(setup_top, setup_oldtop) || !CL_PLColours_Equals(setup_bottom, setup_oldbottom))
				{
					Cbuf_AddText(va("color %s %s\n", CL_PLColours_ToString(setup_top), CL_PLColours_ToString(setup_bottom)));
					colordelta = true;
				}
		}
		break;
	case K_MWHEELUP:
	case K_RIGHTARROW:
		if (setup_cursor < 2)
			return;
forward:
		S_LocalSound ("misc/menu3.wav");
		if (setup_cursor == 4) // 2 to 3 woods #namemaker
		{
			M_AdjustColour(&setup_top, +1);
			q_strlcpy (lastColorSelected, CL_PLColours_ToString(setup_top), sizeof(lastColorSelected));
			if (chase_active.value && !cls.demoplayback && host_initialized && !flyme) // woods #3rdperson
				if (!CL_PLColours_Equals(setup_top, setup_oldtop) || !CL_PLColours_Equals(setup_bottom, setup_oldbottom))
				{
					Cbuf_AddText(va("color %s %s\n", CL_PLColours_ToString(setup_top), CL_PLColours_ToString(setup_bottom)));
					colordelta = true;
				}
		}
		if (setup_cursor == 5) // 3 to 4 woods #namemaker
		{
			M_AdjustColour(&setup_bottom, +1);
			q_strlcpy (lastColorSelected, CL_PLColours_ToString(setup_bottom), sizeof(lastColorSelected));
			if (chase_active.value && !cls.demoplayback && host_initialized && !flyme) // woods #3rdperson
				if (!CL_PLColours_Equals(setup_top, setup_oldtop) || !CL_PLColours_Equals(setup_bottom, setup_oldbottom))
				{
					Cbuf_AddText(va("color %s %s\n", CL_PLColours_ToString(setup_top), CL_PLColours_ToString(setup_bottom)));
					colordelta = true;
				}
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1: // woods #mousemenu
		if (k == K_MOUSE1 && setup_cursor == 0)
		{
			if (M_TextField_MouseInRow(m_mousey, setup_cursor_table[0]))
				M_TextField_MouseClick(&setup_hostname_field, m_mousex, 168);
			return;
		}

		if (k == K_MOUSE1 && setup_cursor == 1)
		{
			if (M_TextField_MouseInRow(m_mousey, setup_cursor_table[1]))
			{
				setup_myname_tabpartial[0] = '\0'; // woods #namehistory
				M_TextField_MouseClick(&setup_myname_field, m_mousex, 168);
			}
			return;
		}

		if (setup_cursor == 0 || setup_cursor == 1)
			return;

		if (setup_cursor == 4 || setup_cursor == 5) // inc 1 both woods #namemaker
		{
			// Handle direct click on color bar boxes
			// Color bar is drawn at y+4 offset: shirt at 114, pants at 138 (8 pixels tall each)
			// Color bar starts at x=64, each box is 8 pixels wide, 14 colors (0-13)
			if (k == K_MOUSE1)
			{
				int colorbar_x = 70; // Adjusted for visual offset (boxes appear ~6px right of mouse coords)
				int colorbar_y = (setup_cursor == 4) ? 110 : 134; // Menu item Y position
				
				// Check if click is within the color bar area
				if (m_mousex >= colorbar_x && m_mousex < colorbar_x + 14 * 8 &&
					m_mousey >= colorbar_y && m_mousey < colorbar_y + 20)
				{
					int clicked_color = (m_mousex - colorbar_x) / 8;
					if (clicked_color >= 0 && clicked_color <= 13)
					{
						plcolour_t *target = (setup_cursor == 4) ? &setup_top : &setup_bottom;
						target->type = 1;
						target->basic = clicked_color;
						S_LocalSound("misc/menu3.wav");
						q_strlcpy(lastColorSelected, CL_PLColours_ToString(*target), sizeof(lastColorSelected));
						if (chase_active.value && !cls.demoplayback && host_initialized && !flyme)
						{
							if (!CL_PLColours_Equals(setup_top, setup_oldtop) || !CL_PLColours_Equals(setup_bottom, setup_oldbottom))
							{
								Cbuf_AddText(va("color %s %s\n", CL_PLColours_ToString(setup_top), CL_PLColours_ToString(setup_bottom)));
								colordelta = true;
							}
						}
						return;
					}
				}
			}
			// If click was outside color bar, fall through to cycle behavior
			goto forward;
		}



		if (setup_cursor == 3)
		{
			m_entersound = true;
			colorpicker_return_fn = M_Menu_Setup_f;
			M_Menu_ColorPicker_f();
			break;
		}

		if (setup_cursor == 2) // woods #namemaker
		{
			m_entersound = true;
			M_Menu_NameMaker_f();
			break;
		}

		// setup_cursor == 6 (OK)
		if (Q_strcmp(cl_name.string, setup_myname) != 0)
			Cbuf_AddText ( va ("name \"%s\"\n", setup_myname) );
		if (Q_strcmp(hostname.string, setup_hostname) != 0)
			Cvar_Set("hostname", setup_hostname);
		if (!CL_PLColours_Equals(setup_top, setup_oldtop) || !CL_PLColours_Equals(setup_bottom, setup_oldbottom))
			Cbuf_AddText( va ("color %s %s\n", CL_PLColours_ToString(setup_top), CL_PLColours_ToString(setup_bottom)) );
		m_entersound = true;

		if (chasewasnotactive && !cls.demoplayback && host_initialized && !flyme) // woods #3rdperson
		{
			chasewasnotactive = false;
			Cbuf_AddText("chase_active 0\n");
		}

			M_Menu_MultiPlayer_f ();
			break;

	case 'c': // woods, copy color
	case 'C':
		if (M_TextField_HasShortcutModifier())
		{
			if (lastColorSelected[0] != '\0')
				SDL_SetClipboardText (lastColorSelected);
			else
				SDL_SetClipboardText (CL_PLColours_ToString (setup_bottom));
			M_TextField_PlayCopySound();
		}
		break;
	}
}


void M_Setup_Char (int k)
{
	menu_textfield_t *active_field = M_Setup_GetFieldForCursor();
	if (active_field)
	{
		if (M_TextField_Char(active_field, k) && active_field == &setup_myname_field)
		{
			setup_myname_tabpartial[0] = '\0'; // woods #namehistory
			M_Setup_UpdateNameHint();
		}
	}
}


qboolean M_Setup_TextEntry (void)
{
	return (setup_cursor == 0 || setup_cursor == 1);
}

void M_Setup_Mousemove(int cx, int cy) // woods #mousemenu
{
	int old_cursor;

	if (textfield_mouse_dragging &&
		(textfield_drag_field == &setup_hostname_field || textfield_drag_field == &setup_myname_field))
	{
		M_TextField_MouseDrag(cx);
		return;
	}

	old_cursor = setup_cursor;
	M_UpdateCursorWithTable(cy, setup_cursor_table, NUM_SETUP_CMDS, &setup_cursor);
	if (setup_cursor != old_cursor)
	{
		M_Setup_ClearTextSelections();
		setup_myname_tabpartial[0] = '\0'; // woods #namehistory
	}
}

/*
=============================================================
Name Maker Menu #namemaker from joequake, qrack
=============================================================
*/

int	namemaker_cursor_x, namemaker_cursor_y;
#define	NAMEMAKER_TABLE_SIZE	16
#define NAMEMAKER_TOTAL_ROWS (NAMEMAKER_TABLE_SIZE + 1) // Added to include the new row

//extern int key_special_dest;

static void M_NameMaker_UpdateNameHint(void) // woods #namehistory
{
	M_Menu_UpdateNameHistoryHint(namemaker_name, namemaker_name_hint, sizeof(namemaker_name_hint));
}

static void M_NameMaker_NameChanged(void) // woods #namehistory
{
	namemaker_name_tabpartial[0] = '\0';
	M_NameMaker_UpdateNameHint();
}

static qboolean M_NameMaker_TextFieldKey(int k)
{
	if (M_TextField_Key(&namemaker_name_field, k))
	{
		M_NameMaker_NameChanged();
		return true;
	}

	if (M_TextField_HasShortcutModifier())
	{
		switch (k)
		{
		case 'a':
		case 'A':
		case 'c':
		case 'C':
		case 'x':
		case 'X':
		case 'v':
		case 'V':
		case 'u':
		case 'U':
			return true;
		}
	}

	return (k == K_BACKSPACE || k == K_DEL);
}

void M_Menu_NameMaker_f (void)
{
	key_dest = key_menu;
	//key_special_dest = 1;
	m_state = m_namemaker;
	m_entersound = true;
	q_strlcpy(namemaker_name, setup_myname, sizeof(namemaker_name));
	M_TextField_Init(&namemaker_name_field, namemaker_name, 15, false);
	namemaker_name_tabpartial[0] = '\0'; // woods #namehistory
	M_NameMaker_UpdateNameHint(); // woods #namehistory
	namemaker_edit_active = true;
}

void M_Shortcut_NameMaker_f (void)
{
	// Baker: our little shortcut into the name maker
	namemaker_shortcut = true;
	q_strlcpy(setup_myname, cl_name.string, sizeof(setup_myname));//R00k
	namemaker_cursor_x = 0;
	namemaker_cursor_y = 0;
	M_Menu_NameMaker_f();
}

void M_NameMaker_Draw (void)
{
	int	x, y;

	M_TextField_CheckMouseRelease();

	M_Print(48, 16, "Your name");
	M_DrawTextBox(120, 8, 16, 1);
	M_TextField_DrawHighlight(&namemaker_name_field, 128, 16);
	M_PrintWhite(128, 16, namemaker_name);
	if (namemaker_edit_active && // woods #namehistory
		namemaker_name_hint[0] &&
		namemaker_name_field.cursor == (int)strlen(namemaker_name))
	{
		int hint_x = 128 + (int)strlen(namemaker_name) * 8;
		M_PrintRGBA(hint_x, 16, namemaker_name_hint, CL_PLColours_Parse("0xffffff"), 0.5f, false);
	}
	if (namemaker_edit_active)
		M_TextField_DrawCursor(&namemaker_name_field, 128, 16);

	for (y = 0; y < NAMEMAKER_TABLE_SIZE; y++)
		for (x = 0; x < NAMEMAKER_TABLE_SIZE; x++)
			M_DrawCharacter(32 + (16 * x), 40 + (8 * y), NAMEMAKER_TABLE_SIZE * y + x);

	M_PrintWhite(32, 48 + 8 * NAMEMAKER_TABLE_SIZE, "Web Name Maker");

	if (namemaker_cursor_y == NAMEMAKER_TABLE_SIZE)
		M_DrawCharacter(24, 48 + 8 * NAMEMAKER_TABLE_SIZE, 12 + ((int)(realtime * 4) & 1));
	else // Cursor within the character table
		M_DrawCharacter(24 + 16 * namemaker_cursor_x, 40 + 8 * namemaker_cursor_y, 12 + ((int)(realtime * 4) & 1));

	//	M_DrawTextBox (136, 176, 2, 1);
	//M_Print(56, 184, "press");
	//M_PrintWhite(103, 184, "ESC");
	//M_Print(133, 184, "to save changes");
}

void M_NameMaker_Key (int k)
{
	int	l;

	if (k == K_TAB) // woods #namehistory
	{
		if (M_Menu_TabCompleteNameHistory(&namemaker_name_field, namemaker_name,
			sizeof(namemaker_name), namemaker_name_tabpartial, sizeof(namemaker_name_tabpartial)))
			S_LocalSound("misc/menu2.wav");
		M_NameMaker_UpdateNameHint();
		namemaker_edit_active = true;
		return;
	}

	if (namemaker_edit_active && M_NameMaker_TextFieldKey(k))
		return;

	if (namemaker_edit_active && k >= 32 && k <= 127)
	{
		if (M_TextField_HasShortcutModifier())
			return;

		Key_Extra(&k);
		if (M_TextField_Char(&namemaker_name_field, k))
			M_NameMaker_NameChanged();
		return;
	}

	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		//key_special_dest = false;

		if (namemaker_shortcut)
		{// Allow quick exit for namemaker command
			key_dest = key_game;
			m_state = m_none;

			//Save the name
			if (strcmp(namemaker_name, cl_name.string))
			{
				Cbuf_AddText(va("name \"%s\"\n", namemaker_name));
				Con_Printf("name changed to %s\n", namemaker_name);
			}
			namemaker_shortcut = false;
			from_namemaker = false;
		}
		else
		{
			from_namemaker = true;
			q_strlcpy(setup_myname, namemaker_name, sizeof(setup_myname));//R00k
			M_Menu_Setup_f();
		}

		break;

	case K_UPARROW:
	case K_KP_UPARROW:
		S_LocalSound("misc/menu1.wav");
		namemaker_edit_active = false;
		namemaker_cursor_y--;
		if (namemaker_cursor_y < 0)
			namemaker_cursor_y = NAMEMAKER_TOTAL_ROWS - 1;
		break;

	case K_DOWNARROW:
	case K_KP_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		namemaker_edit_active = false;
		namemaker_cursor_y++;
		if (namemaker_cursor_y >= NAMEMAKER_TOTAL_ROWS)
			namemaker_cursor_y = 0;
		break;

	case K_PGUP:
		S_LocalSound("misc/menu1.wav");
		namemaker_edit_active = false;
		namemaker_cursor_y = 0;
		break;

	case K_PGDN:
		S_LocalSound("misc/menu1.wav");
		namemaker_edit_active = false;
		namemaker_cursor_y = NAMEMAKER_TABLE_SIZE - 1;
		break;

	case K_LEFTARROW:
	case K_KP_LEFTARROW:
		if (namemaker_cursor_y < NAMEMAKER_TABLE_SIZE) // Only move left if within table
		{
			S_LocalSound("misc/menu1.wav");
			namemaker_cursor_x--;
			if (namemaker_cursor_x < 0)
				namemaker_cursor_x = NAMEMAKER_TABLE_SIZE - 1;
		}
		break;

	case K_RIGHTARROW:
	case K_KP_RIGHTARROW:
		if (namemaker_cursor_y < NAMEMAKER_TABLE_SIZE) // Only move right if within table
		{
			S_LocalSound("misc/menu1.wav");
			namemaker_cursor_x++;
			if (namemaker_cursor_x >= NAMEMAKER_TABLE_SIZE)
				namemaker_cursor_x = 0;
		}
		break;

	case K_HOME:
		S_LocalSound("misc/menu1.wav");
		namemaker_edit_active = false;
		namemaker_cursor_x = 0;
		break;

	case K_END:
		S_LocalSound("misc/menu1.wav");
		namemaker_edit_active = false;
		namemaker_cursor_x = NAMEMAKER_TABLE_SIZE - 1;
		break;

	case K_BACKSPACE:
		if (keydown[K_CTRL])
		{
			listsearch_t temp;
			temp.len = strlen(namemaker_name);
			Q_strcpy(temp.text, namemaker_name);
			M_DeletePrevWord(&temp);
			Q_strcpy(namemaker_name, temp.text);
		}
		else if ((l = strlen(namemaker_name)))
		{
			namemaker_name[l - 1] = 0;
		}
		M_TextField_ClampCursor(&namemaker_name_field);
		M_TextField_ClearSelection(&namemaker_name_field);
		M_NameMaker_NameChanged(); // woods #namehistory
		break;

	case 'u':
	case 'U':
		if (keydown[K_CTRL])
		{
			namemaker_name[0] = 0;
			M_TextField_ClampCursor(&namemaker_name_field);
			M_TextField_ClearSelection(&namemaker_name_field);
			M_NameMaker_NameChanged(); // woods #namehistory
		}
		break;

	// If we reached this point, we are simulating ENTER

	case K_SPACE:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1: // woods #mousemenu
		if (k == K_MOUSE1 &&
			m_mousex >= 120 && m_mousex <= 120 + 18 * 8 &&
			M_TextField_MouseInRow(m_mousey, 16))
		{
			M_TextField_MouseClick(&namemaker_name_field, m_mousex, 128);
			M_NameMaker_NameChanged(); // woods #namehistory
			namemaker_edit_active = true;
			return;
		}

		namemaker_edit_active = false;
		if (namemaker_cursor_y < NAMEMAKER_TABLE_SIZE)
		{
			unsigned char grid_ch = (unsigned char)(NAMEMAKER_TABLE_SIZE * namemaker_cursor_y + namemaker_cursor_x);
			M_TextField_ClampCursor(&namemaker_name_field);
			if (namemaker_name_field.sel_start >= 0 && namemaker_name_field.sel_start != namemaker_name_field.cursor)
				M_TextField_DeleteSelection(&namemaker_name_field);

			l = strlen(namemaker_name);
			if (l < 15)
			{
				memmove(namemaker_name + namemaker_name_field.cursor + 1,
				        namemaker_name + namemaker_name_field.cursor,
				        l - namemaker_name_field.cursor + 1);
				namemaker_name[namemaker_name_field.cursor] = grid_ch;
				namemaker_name_field.cursor++;
				M_TextField_ClampCursor(&namemaker_name_field);
				M_TextField_ClearSelection(&namemaker_name_field);
				M_NameMaker_NameChanged(); // woods #namehistory
			}
		}
		else if (namemaker_cursor_y == NAMEMAKER_TABLE_SIZE)
		{
			// Open the web name maker
			SCR_ModalMessage("web name maker webpage has been opened\nin your ^mweb browser^m\n\nminimize QSS-M to view", 3.5f); // woods
			SDL_OpenURL("https://q1tools.github.io/namemaker/");
		}
		break;

	default:
		if (k < 32 || k > 127)
			break;

		Key_Extra (&k);
		if (M_TextField_Char(&namemaker_name_field, k))
		{
			M_NameMaker_NameChanged(); // woods #namehistory
			namemaker_edit_active = true;
		}
		break;
	}
}

qboolean M_NameMaker_TextEntry(void)
{
	return namemaker_edit_active;
}

void M_NameMaker_Mousemove(int cx, int cy) // woods #mousemenu
{
	int x_origin = 28;
	int y_origin = 36;
	int x_spacing = 16;
	int y_spacing = 8;
	int num_rows = NAMEMAKER_TOTAL_ROWS;
	int max_columns;
	int temp_cursor_x, temp_cursor_y;

	if (textfield_mouse_dragging && textfield_drag_field == &namemaker_name_field)
	{
		M_TextField_MouseDrag(cx);
		return;
	}

	temp_cursor_x = (cx - 8 - x_origin + x_spacing / 2) / x_spacing; // Calculate tentative cursor positions
	temp_cursor_y = (cy - 8 - y_origin + y_spacing / 2) / y_spacing;

	if (temp_cursor_y < 0) // Clamp cursor_y between 0 and num_rows - 1
		temp_cursor_y = 0;
	if (temp_cursor_y >= num_rows)
		temp_cursor_y = num_rows - 1;

	if (temp_cursor_y < NAMEMAKER_TABLE_SIZE) // Determine the number of columns in the current row
		max_columns = NAMEMAKER_TABLE_SIZE; // Regular character table rows
	else
		max_columns = 1; // Last row with "Web Name Maker"

	if (temp_cursor_x < 0) // Clamp cursor_x between 0 and max_columns - 1
		temp_cursor_x = 0;
	if (temp_cursor_x >= max_columns)
		temp_cursor_x = max_columns - 1;

	namemaker_cursor_x = temp_cursor_x; // Update cursor positions
	namemaker_cursor_y = temp_cursor_y;
}

/*
==================
Net Menu
==================
*/

int	m_net_cursor;
int m_net_items;

const char *net_helpMessage [] =
{
/* .........1.........2.... */
  " Novell network LANs    ",
  " or Windows 95 DOS-box. ",
  "                        ",
  "(LAN=Local Area Network)",

  " Commonly used to play  ",
  " over the Internet, but ",
  " also used on a Local   ",
  " Area Network.          "
};

void M_Menu_Net_f (void)
{
	key_dest = key_menu;
	m_state = m_net;
	m_entersound = true;
	m_net_items = 2;

	IN_UpdateGrabs();

	if (m_net_cursor >= m_net_items)
		m_net_cursor = 0;
	m_net_cursor--;
	M_Net_Key (K_DOWNARROW);
}


void M_Net_Draw (void)
{
	int		f;
	qpic_t	*p;

	M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp") );
	p = Draw_CachePic ("gfx/p_multi.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);

	f = 32;

	/*if (ipxAvailable)   // woods this is not needed
		p = Draw_CachePic ("gfx/netmen3.lmp");
	else
		p = Draw_CachePic ("gfx/dim_ipx.lmp");
	M_DrawTransPic (72, f, p);*/

	f += 19;
	if (ipv4Available || ipv6Available)
		p = Draw_CachePic ("gfx/netmen4.lmp");
	else
		p = Draw_CachePic ("gfx/dim_tcp.lmp");
	M_DrawTransPic (72, f, p);

	f = (320-26*8)/2;
	M_DrawTextBox (f, 96, 24, 4);
	f += 8;
	M_Print (f, 104, net_helpMessage[m_net_cursor*4+0]);
	M_Print (f, 112, net_helpMessage[m_net_cursor*4+1]);
	M_Print (f, 120, net_helpMessage[m_net_cursor*4+2]);
	M_Print (f, 128, net_helpMessage[m_net_cursor*4+3]);

	f = (int)(realtime * 10)%6;
	M_DrawTransPic (54, 32 + m_net_cursor * 20,Draw_CachePic( va("gfx/menudot%i.lmp", f+1 ) ) );
}


void M_Net_Key (int k)
{
again:
	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		M_Menu_MultiPlayer_f ();
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		if (++m_net_cursor >= m_net_items)
			m_net_cursor = 0;
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		if (--m_net_cursor < 0)
			m_net_cursor = m_net_items - 1;
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1: // woods #mousemenu
		m_entersound = true;
		M_Menu_LanConfig_f ();
		break;
	}

	if (m_net_cursor == 0 && !ipxAvailable)
		goto again;
	if (m_net_cursor == 1 && !(ipv4Available || ipv6Available))
		goto again;
}

void M_Net_Mousemove(int cx, int cy) // woods #mousemenu
{
	M_UpdateCursor(cy, 32, 20, m_net_items, &m_net_cursor);
	if (m_net_cursor == 0 && !ipxAvailable)
		m_net_cursor = 1;
	if (m_net_cursor == 1 && !(ipv4Available || ipv6Available))
		m_net_cursor = 0;
}

/*
==================
Options Menu
==================
*/

extern cvar_t scr_menuscale;

enum
{
	OPT_CUSTOMIZE = 0,
	OPT_MOUSE,
	OPT_CONTROLLER,
	OPT_WEAPONWHEEL,
	OPT_VIDEO,
	OPT_GRAPHICS,
	OPT_SOUND,
	OPT_GAME,
	OPT_HUD,
	OPT_CONSOLEM,    // Moved up, before OPT_EXTRAS
	OPT_STARTUP,
	OPT_DEMOOPTIONS,
	OPT_EXTRAS,
	OPT_SPACE,       // Spacer
	OPT_MENUSCALE,
	OPT_CONSOLE,
	OPT_RESETCONFIG,
	OPTIONS_ITEMS
};

#define	SLIDER_RANGE	6

int		options_cursor;
static float pending_scale_value;

#define OPTIONS_WHEEL_REPEAT_TIME 0.12

struct // woods #mousemenu
{
	menulist_t		list;
	int				y;
	int				first_item;
	int				options_cursor;
	int				video_cursor;
	int* last_cursor;
	qboolean        scrollbar_grab;
	double          wheel_time;
	int             wheel_dir;
} optionsmenu;

static void M_Options_Init(void)
{
	optionsmenu.list.viewsize = OPTIONS_ITEMS;
	optionsmenu.list.cursor = 0;
	optionsmenu.list.scroll = 0;
	optionsmenu.list.numitems = OPTIONS_ITEMS;
	optionsmenu.scrollbar_grab = false;
	optionsmenu.wheel_time = -OPTIONS_WHEEL_REPEAT_TIME;
	optionsmenu.wheel_dir = 0;

	// Initialize search
	memset(&optionsmenu.list.search, 0, sizeof(optionsmenu.list.search));
	optionsmenu.list.search.maxlen = 32;
}

static qboolean M_Options_AcceptWheelMove(int dir)
{
	if (optionsmenu.wheel_dir != dir ||
		realtime - optionsmenu.wheel_time >= OPTIONS_WHEEL_REPEAT_TIME)
	{
		optionsmenu.wheel_dir = dir;
		optionsmenu.wheel_time = realtime;
		return true;
	}

	return false;
}

static int M_Options_RowY(int item)
{
	return 32 + item * 8;
}

static int M_Options_LivePreviewId(void)
{
	return LP_NONE;
}

static int M_Options_MenuScaleMax(void)
{
	int max_scale = q_min(glwidth / 320, glheight / 200);
	return q_max(1, max_scale);
}

static float M_Options_MenuScaleFraction(float value)
{
	int max_scale = M_Options_MenuScaleMax();

	if (max_scale <= 1)
		return 0.0f;

	return CLAMP(0.0f, (value - 1.0f) / (float)(max_scale - 1), 1.0f);
}

static int M_Options_MenuScaleFromFraction(float fraction)
{
	int max_scale = M_Options_MenuScaleMax();

	fraction = CLAMP(0.0f, fraction, 1.0f);
	return (int)(fraction * (max_scale - 1) + 1.5f);
}

static int M_ConsoleScaleMax(void)
{
	return q_max(1, vid.width / 320);
}

static float M_ConsoleScaleFraction(float value)
{
	int max_scale = M_ConsoleScaleMax();

	if (max_scale <= 1)
		return 0.0f;

	return CLAMP(0.0f, (value - 1.0f) / (float)(max_scale - 1), 1.0f);
}

static float M_ConsoleScaleFromFraction(float fraction)
{
	int max_scale = M_ConsoleScaleMax();

	fraction = CLAMP(0.0f, fraction, 1.0f);
	return fraction * (max_scale - 1) + 1.0f;
}

void M_Menu_Options_f (void)
{
	key_dest = key_menu;
	m_state = m_options;
	m_entersound = true;
	slider_grab = false; // woods #mousemenu
	M_LivePreview_Reset();
	M_Options_Init();

	IN_UpdateGrabs();
}


void M_AdjustSliders (int dir)
{
	float	f;

	switch (options_cursor)
	{
	case OPT_MENUSCALE:
		S_LocalSound ("misc/menu3.wav");
		f = scr_menuscale.value + dir;
		if (f > M_Options_MenuScaleMax()) f = M_Options_MenuScaleMax();
		else if (f < 1) f = 1;
		Cvar_SetValue("scr_menuscale", f);
		break;
	}
}

void M_DrawSlider (int x, int y, float range, float value, const char* format)
{
	int	i;
	char	buffer[6];

	if (range < 0)
		range = 0;
	if (range > 1)
		range = 1;
	M_DrawCharacter (x-8, y, 128);
	for (i = 0; i < SLIDER_RANGE; i++)
		M_DrawCharacter (x + i*8, y, 129);
	M_DrawCharacter (x+i*8, y, 130);
	M_DrawCharacter (x + (SLIDER_RANGE-1)*8 * range, y, 131);

	q_snprintf(buffer, sizeof(buffer), format, value);
	i = x + (SLIDER_RANGE + 2) * 8;
	M_Print(i, y, buffer);
}

#define MENU_CHECKBOX_ROW_OFFSET	4
#define MENU_CHECKBOX_BOX_SCALE		1.375f
#define MENU_CHECKBOX_X_SCALE		0.625f

void M_DrawCheckboxBox (int x, int y, int on)
{
	glPushMatrix();
	glTranslatef(x, y - MENU_CHECKBOX_ROW_OFFSET - 2, 0);
	glScalef(MENU_CHECKBOX_BOX_SCALE, MENU_CHECKBOX_BOX_SCALE, 1.0f);
	M_DrawTextBox(0, 0, 0, 0);
	glPopMatrix();

	if (on)
	{
		glPushMatrix();
		glTranslatef(x + 8, y + 2, 0);
		glScalef(MENU_CHECKBOX_X_SCALE, MENU_CHECKBOX_X_SCALE, 1.0f);
		M_PrintWhite(0, 0, "X");
		glPopMatrix();
	}
}

void M_DrawCheckbox (int x, int y, int on)
{
#if 0
	if (on)
		M_DrawCharacter (x, y, 131);
	else
		M_DrawCharacter (x, y, 129);
#endif
	if (on)
		M_Print (x, y, "on");
	else
		M_Print (x, y, "off");
}

qboolean M_SetSliderValue(int option, float f) // woods #mousemenu
{
	f = CLAMP(0.f, f, 1.f);

	switch (option)
	{
	case OPT_MENUSCALE:
		f = M_Options_MenuScaleFromFraction(f);
		Cvar_SetValue("scr_menuscale", f);
		return true;
	default:
		return false;
	}
}

float M_MouseToSliderFraction(int cx) // woods #mousemenu
{
	float f;
	f = (cx - 4) / (float)((SLIDER_RANGE - 1) * 8);
	return CLAMP(0.f, f, 1.f);
}

void M_ReleaseSliderGrab(void) // woods #mousemenu
{
	if (!slider_grab)
		return;

	if (options_cursor == OPT_MENUSCALE)
	{
		Cvar_SetValue("scr_menuscale", pending_scale_value);
	}

	slider_grab = false;
	S_LocalSound("misc/menu1.wav");
}

qboolean M_SliderClick(int cx, int cy) // woods #mousemenu
{
	cx -= 220;
	if (cx < -12 || cx > SLIDER_RANGE * 8 + 4)
		return false;

	if (options_cursor == OPT_MENUSCALE)
	{
		float f = M_MouseToSliderFraction(cx);
		f = M_Options_MenuScaleFromFraction(f);
		pending_scale_value = f;  // Store initial value
		slider_grab = true;
		S_LocalSound("misc/menu3.wav");
		return true;
	}

	slider_grab = true;
	S_LocalSound("misc/menu3.wav");
	return true;
}

void M_Options_Draw (void)
{
	float		r;
	qpic_t  *p;

	if (slider_grab && !keydown[K_MOUSE1]) // woods #mousemenu
		M_ReleaseSliderGrab();

	M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp") );
	p = Draw_CachePic ("gfx/p_option.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);

	M_LivePreview_WantAt (M_Options_LivePreviewId (), M_Options_RowY (options_cursor));

	// Draw menu items with search highlighting if active
	for (int i = 0; i < OPTIONS_ITEMS; i++)
	{
		const char* text = NULL;
		int y = M_Options_RowY (i);
		qboolean isolated = M_LivePreview_IsolateY (y);

		if (isolated)
			M_LivePreview_BeginIsolate ();

		// Get menu item text based on index
		switch (i) {
		case OPT_CUSTOMIZE:
			text = "      Key/Button Setup   ...";
			break;
		case OPT_MOUSE:
			text = "                 Mouse   ...";
			break;
		case OPT_CONTROLLER:
			text = "            Controller   ...";
			break;
		case OPT_WEAPONWHEEL:
			text = "          Weapon Wheel   ...";
			break;
		case OPT_VIDEO:
			if (vid_menudrawfn)
			text = "               Display   ...";
			break;
		case OPT_GRAPHICS:
			if (vid_menudrawfn)
			text = "              Graphics   ...";
			break;
		case OPT_SOUND:
			text = "                 Sound   ...";
			break;
		case OPT_GAME:
			text = "                  Game   ...";
			break;
		case OPT_HUD:
			text = "                   HUD   ...";
			break;
		case OPT_CONSOLEM:
			text = "               Console   ...";
			break;
		case OPT_STARTUP:
			text = "               Startup   ...";
			break;
		case OPT_DEMOOPTIONS:
			text = "                 Demos   ...";
			break;
		case OPT_EXTRAS:
			text = "                  Misc   ...";
			break;
		case OPT_MENUSCALE:
			text = "            Menu Scale";
			if (slider_grab && options_cursor == OPT_MENUSCALE)
			{
				r = M_Options_MenuScaleFraction(pending_scale_value);
				M_DrawSlider(220, y, r, pending_scale_value, "%.0f");
			}
			else
			{
				r = M_Options_MenuScaleFraction(scr_menuscale.value);
				M_DrawSlider(220, y, r, scr_menuscale.value, "%.0f");
			}
			break;
		case OPT_CONSOLE:
			text = "          Goto Console";
			break;
		case OPT_RESETCONFIG:
			text = "          Reset Config   ...";
			break;
		}

		if (text) // If search is active and text matches search term
		{
			if (optionsmenu.list.search.len > 0 &&
				q_strcasestr(text, optionsmenu.list.search.text))
			{
				M_PrintHighlight(16, y, text,
					optionsmenu.list.search.text,
					optionsmenu.list.search.len);
			}
			else
			{
				M_Print(16, y, text);
			}
		}

		if (isolated)
			M_LivePreview_EndIsolate ();
	}
	// Draw cursor
	{
		int y = M_Options_RowY (options_cursor);
		qboolean isolated = M_LivePreview_IsolateY (y);
		if (isolated)
			M_LivePreview_BeginIsolate ();
		M_DrawCharacter(200, y, 12 + ((int)(realtime * 4) & 1));
		if (isolated)
			M_LivePreview_EndIsolate ();
	}

	if (optionsmenu.list.search.len > 0) // Draw search box if search is active
	{
		M_DrawTextBox(16, 170, 32, 1);
		M_PrintHighlight(24, 178, optionsmenu.list.search.text,
			optionsmenu.list.search.text,
			optionsmenu.list.search.len);
		int cursor_x = 24 + 8 * optionsmenu.list.search.len; // Start position + character width * text length
		if (optionsmenu.list.numitems == 0)
			M_DrawCharacter(cursor_x, 178, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, 178, 10 + ((int)(realtime * 4) & 1));
	}
}

static const char* M_Options_GetItemText(int index)
{
	switch (index)
	{
	case OPT_CUSTOMIZE:
		return "      Key/Button Setup   ...";
	case OPT_MOUSE:
		return "                 Mouse   ...";
	case OPT_CONTROLLER:
		return "            Controller   ...";
	case OPT_WEAPONWHEEL:
		return "          Weapon Wheel   ...";
	case OPT_VIDEO:
		return "               Display   ...";
	case OPT_GRAPHICS:
		return "              Graphics   ...";
	case OPT_SOUND:
		return "                 Sound   ...";
	case OPT_GAME:
		return "                  Game   ...";
	case OPT_HUD:
		return "                   HUD   ...";
	case OPT_CONSOLEM:
		return "               Console   ...";
	case OPT_STARTUP:
		return "               Startup   ...";
	case OPT_DEMOOPTIONS:
		return "                 Demos   ...";
	case OPT_EXTRAS:
		return "                  Misc   ...";
	case OPT_MENUSCALE:
		return "            Menu Scale";
	case OPT_CONSOLE:
		return "          Goto Console";
	case OPT_RESETCONFIG:
		return "         Reset Config   ...";

	default:
		return "";
	}
}

void M_Options_Key (int k)
{
	// Handle search functionality first
	if (k == K_ESCAPE)
	{
		if (optionsmenu.list.search.len > 0)
		{
			// Clear search but stay in menu
			optionsmenu.list.search.len = 0;
			optionsmenu.list.search.text[0] = 0;
			return;
		}
		// If no search active, proceed with normal menu exit
		if (M_LivePreview_Alpha() > 0.f)
		{
			M_LivePreview_Reset();
			return;
		}
		M_Menu_Main_f();
		return;
	}
	else if (k == K_BACKSPACE)
	{
		if (optionsmenu.list.search.len > 0)
		{
			optionsmenu.list.search.text[--optionsmenu.list.search.len] = 0;
			return;
		}
	}
	else if (k >= 32 && k < 127) // Printable characters
	{
		if (optionsmenu.list.search.len < sizeof(optionsmenu.list.search.text) - 1)
		{
			optionsmenu.list.search.text[optionsmenu.list.search.len++] = k;
			optionsmenu.list.search.text[optionsmenu.list.search.len] = 0;

			// Reset item count
			optionsmenu.list.numitems = 0;

			// Search for matching items and count them
			for (int i = 0; i < OPTIONS_ITEMS; i++)
			{
				const char* itemtext = M_Options_GetItemText(i);
				if (q_strcasestr(itemtext, optionsmenu.list.search.text))
				{
					optionsmenu.list.numitems++;
					// Move cursor to the first matching item
					if (optionsmenu.list.numitems == 1)
						options_cursor = i;
				}
			}
			return;
		}
	}

	if (!keydown[K_MOUSE1]) // woods #mousemenu
		M_ReleaseSliderGrab();

	if (slider_grab) // woods #mousemenu
	{
		switch (k)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			M_ReleaseSliderGrab();
			break;
		}
		return;
	}
	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		if (M_LivePreview_Alpha() > 0.f)
		{
			M_LivePreview_Reset();
			return;
		}
		M_Menu_Main_f ();
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	enter:
		m_entersound = true;
		switch (options_cursor)
		{
		case OPT_CUSTOMIZE:
			M_Menu_Keys_f ();
			break;
		case OPT_MOUSE:
			M_Menu_Mouse_f();
			break;
		case OPT_CONTROLLER:
			M_Menu_Controller_f();
			break;
		case OPT_WEAPONWHEEL:
			M_Menu_WeaponWheel_f();
			break;
		case OPT_VIDEO:
			M_Menu_Video_f();
			break;
		case OPT_GRAPHICS:
			M_Menu_Graphics_f();
			break;
		case OPT_SOUND:
			M_Menu_Sound_f();
			break;
		case OPT_GAME:
			M_Menu_Game_f();
			break;
		case OPT_HUD:
			M_Menu_HUD_f();
			break;
		case OPT_CONSOLEM:
			M_Menu_Console_f();
			break;
		case OPT_STARTUP:
			M_Menu_Startup_f();
			break;
		case OPT_DEMOOPTIONS:
			M_Menu_DemoOptions_f();
			break;
		case OPT_EXTRAS:
			M_Menu_Extras_f();
			break;
		case OPT_CONSOLE:
			m_state = m_none;
			Con_ToggleConsole_f ();
			break;
		case OPT_RESETCONFIG:
			M_Menu_ResetConfig_f();
			break;
		default:
			M_AdjustSliders (1);
			break;
		}
		return;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		options_cursor--;
		if (options_cursor < 0)
			options_cursor = OPTIONS_ITEMS-1;
		if (options_cursor == OPT_SPACE)  // Skip space when going up
			options_cursor--;
		break;

	case K_MWHEELUP:
		if (!M_Options_AcceptWheelMove(-1))
			return;
		S_LocalSound ("misc/menu1.wav");
		options_cursor--;
		if (options_cursor < 0)
			options_cursor = OPTIONS_ITEMS-1;
		if (options_cursor == OPT_SPACE)  // Skip space when going up
			options_cursor--;
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		options_cursor++;
		if (options_cursor >= OPTIONS_ITEMS)
			options_cursor = 0;
		if (options_cursor == OPT_SPACE)  // Skip space when going down
			options_cursor++;
		break;

	case K_MWHEELDOWN:
		if (!M_Options_AcceptWheelMove(1))
			return;
		S_LocalSound ("misc/menu1.wav");
		options_cursor++;
		if (options_cursor >= OPTIONS_ITEMS)
			options_cursor = 0;
		if (options_cursor == OPT_SPACE)  // Skip space when going down
			options_cursor++;
		break;

	case K_LEFTARROW:
		M_AdjustSliders (-1);
		break;

	case K_RIGHTARROW:
		M_AdjustSliders (1);
		break;

	case K_MOUSE1: // woods #mousemenu
		if (options_cursor == OPT_MENUSCALE && m_mousex >= 220 && m_mousex <= 220 + SLIDER_RANGE * 8)
		{
			if (!M_SliderClick(m_mousex, m_mousey))
				goto enter;
		}
		else
		{
			goto enter;
		}
	}

	if (options_cursor == OPTIONS_ITEMS - 1 && vid_menudrawfn == NULL)
	{
		if (k == K_UPARROW || k == K_MWHEELUP)
			options_cursor = OPTIONS_ITEMS - 2;
		else
			options_cursor = 0;
	}
}

void M_Options_Mousemove(int cx, int cy) // woods #mousemenu
{
	if (slider_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			M_ReleaseSliderGrab();
			return;
		}

		if (options_cursor == OPT_MENUSCALE)
		{
			float f = M_MouseToSliderFraction(cx - 220);
			f = M_Options_MenuScaleFromFraction(f);
			pending_scale_value = f;  // Store the value but don't apply it yet
			return;
		}

		M_SetSliderValue(options_cursor, M_MouseToSliderFraction(cx - 220));
		return;
	}

	int old_cursor = options_cursor;

	M_UpdateCursor(cy, 36, 8, OPTIONS_ITEMS, &options_cursor);

	if (options_cursor == OPT_SPACE)
	{
		// If moving down
		if (old_cursor < OPT_SPACE)
			options_cursor++;
		// If moving up
		else if (old_cursor > OPT_SPACE)
			options_cursor--;
	}
}

/*
==================
Weapon Wheel Menu
==================
*/

#define WEAPONWHEEL_MENU_MAX	32
#define WEAPONWHEEL_HEADER_X	16
#define WEAPONWHEEL_HEADER_Y	32
#define WEAPONWHEEL_HEADER_COLS	36
#define WEAPONWHEEL_LIST_Y	32

static struct
{
	int	items[WEAPONWHEEL_MENU_MAX];
	int	visible_count;
	int	total_count;
	int	cursor;
	qboolean mouse_down;
	int	mouse_item;
	qboolean mouse_dragged;
	qboolean preview;
	int	preview_pick;
} weaponwheelmenu;

static int M_WeaponWheel_PreviewIndex(void)
{
	return weaponwheelmenu.total_count;
}

static int M_WeaponWheel_MenuCount(void)
{
	return weaponwheelmenu.total_count > 0 ? weaponwheelmenu.total_count + 1 : 0;
}

static qboolean M_WeaponWheel_IsPreviewItem(int item)
{
	return weaponwheelmenu.total_count > 0 && item == M_WeaponWheel_PreviewIndex();
}

static void M_WeaponWheel_ClampCursor(void)
{
	int count = M_WeaponWheel_MenuCount();

	if (count <= 0)
	{
		weaponwheelmenu.cursor = 0;
		return;
	}
	if (weaponwheelmenu.cursor < 0)
		weaponwheelmenu.cursor = count - 1;
	else if (weaponwheelmenu.cursor >= count)
		weaponwheelmenu.cursor = 0;
}

static qboolean M_WeaponWheel_HasHidden(void)
{
	return weaponwheelmenu.visible_count < weaponwheelmenu.total_count;
}

static int M_WeaponWheel_RowY(int item)
{
	int y = WEAPONWHEEL_LIST_Y + item * 8;

	if (M_WeaponWheel_HasHidden() && item >= weaponwheelmenu.visible_count)
		y += 8;
	return y;
}

static int M_WeaponWheel_PreviewY(void)
{
	if (weaponwheelmenu.total_count <= 0)
		return WEAPONWHEEL_LIST_Y;
	return M_WeaponWheel_RowY(weaponwheelmenu.total_count - 1) + 16;
}

static int M_WeaponWheel_CursorY(int item)
{
	if (M_WeaponWheel_IsPreviewItem(item))
		return M_WeaponWheel_PreviewY();
	return M_WeaponWheel_RowY(item);
}

static int M_WeaponWheel_ItemAtY(int y)
{
	int rel = y - WEAPONWHEEL_LIST_Y;
	int item;

	if (rel < 0)
		return -1;
	if (M_WeaponWheel_HasHidden() && rel >= weaponwheelmenu.visible_count * 8)
	{
		if (rel < (weaponwheelmenu.visible_count + 1) * 8)
			return -1;
		rel -= 8;
	}

	item = rel / 8;
	if (item < 0 || item >= weaponwheelmenu.total_count)
	{
		int preview_y = M_WeaponWheel_PreviewY();
		if (weaponwheelmenu.total_count > 0 && y >= preview_y - 8 && y < preview_y + 16)
			return M_WeaponWheel_PreviewIndex();
		return -1;
	}
	return item;
}

static void M_WeaponWheel_LoadOrder(void)
{
	int visible[WEAPONWHEEL_MENU_MAX];
	int hidden[WEAPONWHEEL_MENU_MAX];
	int visible_count = 0, hidden_count = 0;
	int i;

	Wheel_MenuBuildOrder(visible, &visible_count, hidden, &hidden_count, WEAPONWHEEL_MENU_MAX);

	weaponwheelmenu.visible_count = visible_count;
	weaponwheelmenu.total_count = 0;
	for (i = 0; i < visible_count && weaponwheelmenu.total_count < WEAPONWHEEL_MENU_MAX; i++)
		weaponwheelmenu.items[weaponwheelmenu.total_count++] = visible[i];
	for (i = 0; i < hidden_count && weaponwheelmenu.total_count < WEAPONWHEEL_MENU_MAX; i++)
		weaponwheelmenu.items[weaponwheelmenu.total_count++] = hidden[i];

	M_WeaponWheel_ClampCursor();
}

static void M_WeaponWheel_Save(void)
{
	Wheel_MenuSetOrder(weaponwheelmenu.items, weaponwheelmenu.visible_count);
}

static void M_WeaponWheel_ClearMouseDrag(void)
{
	weaponwheelmenu.mouse_down = false;
	weaponwheelmenu.mouse_item = -1;
	weaponwheelmenu.mouse_dragged = false;
}

static void M_WeaponWheel_ResetDefaults(void)
{
	Wheel_MenuResetOrder();
	weaponwheelmenu.cursor = 0;
	weaponwheelmenu.preview = false;
	weaponwheelmenu.preview_pick = 0;
	M_WeaponWheel_ClearMouseDrag();
	M_WeaponWheel_LoadOrder();
	S_LocalSound("misc/menu3.wav");
}

void M_Menu_WeaponWheel_f(void)
{
	key_dest = key_menu;
	m_state = m_weaponwheel;
	m_entersound = true;

	weaponwheelmenu.cursor = 0;
	weaponwheelmenu.preview = false;
	weaponwheelmenu.preview_pick = 0;
	M_WeaponWheel_ClearMouseDrag();
	M_WeaponWheel_LoadOrder();
	IN_UpdateGrabs();
}

static void M_WeaponWheel_MoveCursor(int dir)
{
	if (M_WeaponWheel_MenuCount() <= 0)
		return;
	S_LocalSound("misc/menu1.wav");
	weaponwheelmenu.cursor += dir;
	M_WeaponWheel_ClampCursor();
}

static void M_WeaponWheel_MoveSelected(int dir)
{
	int target, tmp;

	if (weaponwheelmenu.cursor < 0 || weaponwheelmenu.cursor >= weaponwheelmenu.visible_count)
	{
		S_LocalSound("misc/menu2.wav");
		return;
	}

	target = weaponwheelmenu.cursor + dir;
	if (target < 0 || target >= weaponwheelmenu.visible_count)
	{
		S_LocalSound("misc/menu2.wav");
		return;
	}

	tmp = weaponwheelmenu.items[weaponwheelmenu.cursor];
	weaponwheelmenu.items[weaponwheelmenu.cursor] = weaponwheelmenu.items[target];
	weaponwheelmenu.items[target] = tmp;
	weaponwheelmenu.cursor = target;

	S_LocalSound("misc/menu3.wav");
	M_WeaponWheel_Save();
}

static void M_WeaponWheel_MoveItem(int from, int to)
{
	int weapon_index;

	if (from < 0 || from >= weaponwheelmenu.visible_count ||
		to < 0 || to >= weaponwheelmenu.visible_count ||
		from == to)
		return;

	weapon_index = weaponwheelmenu.items[from];
	if (from < to)
		memmove(&weaponwheelmenu.items[from],
			&weaponwheelmenu.items[from + 1],
			(to - from) * sizeof(weaponwheelmenu.items[0]));
	else
		memmove(&weaponwheelmenu.items[to + 1],
			&weaponwheelmenu.items[to],
			(from - to) * sizeof(weaponwheelmenu.items[0]));

	weaponwheelmenu.items[to] = weapon_index;
	weaponwheelmenu.cursor = to;
	weaponwheelmenu.mouse_item = to;
	weaponwheelmenu.mouse_dragged = true;

	S_LocalSound("misc/menu3.wav");
	M_WeaponWheel_Save();
}

static void M_WeaponWheel_ToggleSelected(void)
{
	int weapon_index;

	if (M_WeaponWheel_IsPreviewItem(weaponwheelmenu.cursor))
	{
		M_WeaponWheel_Save();
		weaponwheelmenu.preview = true;
		weaponwheelmenu.preview_pick = Wheel_MenuPreviewStart();
		M_WeaponWheel_ClearMouseDrag();
		S_LocalSound("misc/menu3.wav");
		return;
	}

	if (weaponwheelmenu.cursor < 0 || weaponwheelmenu.cursor >= weaponwheelmenu.total_count)
		return;

	if (weaponwheelmenu.cursor < weaponwheelmenu.visible_count)
	{
		if (weaponwheelmenu.visible_count <= 1)
			return;

		weapon_index = weaponwheelmenu.items[weaponwheelmenu.cursor];
		memmove(&weaponwheelmenu.items[weaponwheelmenu.cursor],
			&weaponwheelmenu.items[weaponwheelmenu.cursor + 1],
			(weaponwheelmenu.total_count - weaponwheelmenu.cursor - 1) * sizeof(weaponwheelmenu.items[0]));
		weaponwheelmenu.items[weaponwheelmenu.total_count - 1] = weapon_index;
		weaponwheelmenu.visible_count--;
	}
	else
	{
		int insert = weaponwheelmenu.visible_count;

		weapon_index = weaponwheelmenu.items[weaponwheelmenu.cursor];
		memmove(&weaponwheelmenu.items[weaponwheelmenu.cursor],
			&weaponwheelmenu.items[weaponwheelmenu.cursor + 1],
			(weaponwheelmenu.total_count - weaponwheelmenu.cursor - 1) * sizeof(weaponwheelmenu.items[0]));
		memmove(&weaponwheelmenu.items[insert + 1],
			&weaponwheelmenu.items[insert],
			(weaponwheelmenu.total_count - insert - 1) * sizeof(weaponwheelmenu.items[0]));
		weaponwheelmenu.items[insert] = weapon_index;
		weaponwheelmenu.visible_count++;
		weaponwheelmenu.cursor = insert;
	}

	S_LocalSound("misc/menu3.wav");
	M_WeaponWheel_Save();
	M_WeaponWheel_ClampCursor();
}

static void M_WeaponWheel_FinishMouseClick(void)
{
	int item;

	if (!weaponwheelmenu.mouse_down)
		return;

	item = M_WeaponWheel_ItemAtY(m_mousey);
	if (!weaponwheelmenu.mouse_dragged && item >= 0)
	{
		weaponwheelmenu.cursor = item;
		m_entersound = true;
		M_WeaponWheel_ToggleSelected();
	}

	M_WeaponWheel_ClearMouseDrag();
}

static void M_WeaponWheel_PrintLegendSegment(float *x, int y, const char *text, qboolean white, float scale)
{
	glPushMatrix();
	glTranslatef(*x, y, 0);
	glScalef(scale, scale, 1.0f);
	if (white)
		M_PrintWhite(0, 0, text);
	else
		M_Print(0, 0, text);
	glPopMatrix();

	*x += (float)strlen(text) * 8.0f * scale;
}

static void M_WeaponWheel_DrawTextBoxExact(int x, int y, int width, int lines)
{
	qpic_t *p;
	int cx, cy;
	int n;

	cx = x;
	cy = y;
	p = Draw_CachePic("gfx/box_tl.lmp");
	M_DrawTransPic(cx, cy, p);
	p = Draw_CachePic("gfx/box_ml.lmp");
	for (n = 0; n < lines; n++)
	{
		cy += 8;
		M_DrawTransPic(cx, cy, p);
	}
	p = Draw_CachePic("gfx/box_bl.lmp");
	M_DrawTransPic(cx, cy + 8, p);

	cx += 8;
	while (width >= 2)
	{
		cy = y;
		p = Draw_CachePic("gfx/box_tm.lmp");
		M_DrawTransPic(cx, cy, p);
		p = Draw_CachePic("gfx/box_mm.lmp");
		for (n = 0; n < lines; n++)
		{
			cy += 8;
			if (n == 1)
				p = Draw_CachePic("gfx/box_mm2.lmp");
			M_DrawTransPic(cx, cy, p);
		}
		p = Draw_CachePic("gfx/box_bm.lmp");
		M_DrawTransPic(cx, cy + 8, p);
		width -= 2;
		cx += 16;
	}
	if (width > 0)
	{
		cy = y;
		p = Draw_CachePic("gfx/box_tm.lmp");
		M_DrawSubpic(cx, cy, p, 0, 0, 8, p->height);
		p = Draw_CachePic("gfx/box_mm.lmp");
		for (n = 0; n < lines; n++)
		{
			cy += 8;
			if (n == 1)
				p = Draw_CachePic("gfx/box_mm2.lmp");
			M_DrawSubpic(cx, cy, p, 0, 0, 8, p->height);
		}
		p = Draw_CachePic("gfx/box_bm.lmp");
		M_DrawSubpic(cx, cy + 8, p, 0, 0, 8, p->height);
		cx += 8;
	}

	cy = y;
	p = Draw_CachePic("gfx/box_tr.lmp");
	M_DrawTransPic(cx, cy, p);
	p = Draw_CachePic("gfx/box_mr.lmp");
	for (n = 0; n < lines; n++)
	{
		cy += 8;
		M_DrawTransPic(cx, cy, p);
	}
	p = Draw_CachePic("gfx/box_br.lmp");
	M_DrawTransPic(cx, cy + 8, p);
}

void M_WeaponWheel_Draw(void)
{
	plcolour_t white = CL_PLColours_Parse("0xffffff");
	float legend_x;
	float legend_width;
	const float legend_scale = 0.75f;
	int preview_y;
	int i;

	if (weaponwheelmenu.mouse_down && !keydown[K_MOUSE1])
		M_WeaponWheel_FinishMouseClick();

	Draw_String(WEAPONWHEEL_HEADER_X, WEAPONWHEEL_HEADER_Y - 28, "Weapon Wheel");
	M_DrawQuakeBar(WEAPONWHEEL_HEADER_X - 8, WEAPONWHEEL_HEADER_Y - 16, WEAPONWHEEL_HEADER_COLS + 2);

	if (weaponwheelmenu.preview)
	{
		Wheel_MenuDrawPreview(weaponwheelmenu.preview_pick);
		return;
	}

	for (i = 0; i < weaponwheelmenu.total_count; i++)
	{
		int weapon_index = weaponwheelmenu.items[i];
		int y = M_WeaponWheel_RowY(i);
		qboolean included = i < weaponwheelmenu.visible_count;
		qboolean available = Wheel_MenuWeaponAvailable(weapon_index);

		if (included && available)
		{
			M_Print(32, y, "[ ]");
			M_PrintRGBA(40, y, "x", white, 1.0f, false);
			M_Print(64, y, Wheel_MenuWeaponName(weapon_index));
		}
		else
		{
			float alpha = included ? 0.55f : 0.40f;

			M_PrintRGBA(32, y, "[ ]", white, alpha, true);
			if (included)
				M_PrintRGBA(40, y, "x", white, alpha, false);
			M_PrintRGBA(64, y, Wheel_MenuWeaponName(weapon_index), white, alpha, true);
		}
	}

	if (weaponwheelmenu.total_count > 0)
	{
		preview_y = M_WeaponWheel_PreviewY();
		M_WeaponWheel_DrawTextBoxExact(24, preview_y - 8, 7, 1);
		M_PrintWhite(32, preview_y, "Preview");
	}
	else
	{
		M_Print(32, WEAPONWHEEL_LIST_Y, "no weapons available");
	}

	if (M_WeaponWheel_MenuCount() > 0)
		M_DrawCharacter(16, M_WeaponWheel_CursorY(weaponwheelmenu.cursor), 12 + ((int)(realtime * 4) & 1));

	legend_width = (float)(strlen("enter:") + strlen("toggle  ") + strlen("left/right:") +
		strlen("reorder  ") + strlen("ctrl+r:") + strlen("defaults")) * 8.0f * legend_scale;
	legend_x = (320.0f - legend_width) * 0.5f;
	M_WeaponWheel_PrintLegendSegment(&legend_x, 190, "enter:", false, legend_scale);
	M_WeaponWheel_PrintLegendSegment(&legend_x, 190, "toggle  ", true, legend_scale);
	M_WeaponWheel_PrintLegendSegment(&legend_x, 190, "left/right:", false, legend_scale);
	M_WeaponWheel_PrintLegendSegment(&legend_x, 190, "reorder  ", true, legend_scale);
	M_WeaponWheel_PrintLegendSegment(&legend_x, 190, "ctrl+r:", false, legend_scale);
	M_WeaponWheel_PrintLegendSegment(&legend_x, 190, "defaults", true, legend_scale);
}

void M_WeaponWheel_Key(int key)
{
	if (weaponwheelmenu.preview)
	{
		switch (key)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
		case K_ENTER:
		case K_KP_ENTER:
		case K_ABUTTON:
			weaponwheelmenu.preview = false;
			M_WeaponWheel_ClearMouseDrag();
			S_LocalSound("misc/menu3.wav");
			return;
		case K_MOUSE1:
			weaponwheelmenu.preview_pick = Wheel_MenuPreviewPickFromPoint(weaponwheelmenu.preview_pick,
				(float)m_mousex, (float)m_mousey);
			S_LocalSound("misc/menu1.wav");
			return;
		case K_MWHEELUP:
		case K_UPARROW:
		case K_LEFTARROW:
		case '[':
			weaponwheelmenu.preview_pick = Wheel_MenuPreviewScroll(weaponwheelmenu.preview_pick, -1);
			S_LocalSound("misc/menu1.wav");
			return;
		case K_MWHEELDOWN:
		case K_DOWNARROW:
		case K_RIGHTARROW:
		case ']':
			weaponwheelmenu.preview_pick = Wheel_MenuPreviewScroll(weaponwheelmenu.preview_pick, 1);
			S_LocalSound("misc/menu1.wav");
			return;
		default:
			return;
		}
	}

	if (keydown[K_CTRL] && (key == 'r' || key == 'R'))
	{
		M_WeaponWheel_ResetDefaults();
		return;
	}

	switch (key)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		M_WeaponWheel_Save();
		M_Menu_Options_f();
		break;

	case K_MOUSE1:
	{
		int item = M_WeaponWheel_ItemAtY(m_mousey);
		if (item >= 0)
		{
			weaponwheelmenu.cursor = item;
			weaponwheelmenu.mouse_down = true;
			weaponwheelmenu.mouse_item = item;
			weaponwheelmenu.mouse_dragged = false;
		}
		else
			M_WeaponWheel_ClearMouseDrag();
		break;
	}

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		M_WeaponWheel_ToggleSelected();
		break;

	case K_UPARROW:
		if (keydown[K_SHIFT])
			M_WeaponWheel_MoveSelected(-1);
		else
			M_WeaponWheel_MoveCursor(-1);
		break;

	case K_DOWNARROW:
		if (keydown[K_SHIFT])
			M_WeaponWheel_MoveSelected(1);
		else
			M_WeaponWheel_MoveCursor(1);
		break;

	case K_MWHEELUP:
		M_WeaponWheel_MoveCursor(-1);
		break;

	case K_MWHEELDOWN:
		M_WeaponWheel_MoveCursor(1);
		break;

	case K_LEFTARROW:
	case '[':
		M_WeaponWheel_MoveSelected(-1);
		break;

	case K_RIGHTARROW:
	case ']':
		M_WeaponWheel_MoveSelected(1);
		break;
	}
}

void M_WeaponWheel_Mousemove(int cx, int cy)
{
	int item = M_WeaponWheel_ItemAtY(cy);

	if (weaponwheelmenu.preview)
	{
		weaponwheelmenu.preview_pick = Wheel_MenuPreviewPickFromPoint(weaponwheelmenu.preview_pick,
			(float)cx, (float)cy);
		return;
	}

	if (weaponwheelmenu.mouse_down)
	{
		if (!keydown[K_MOUSE1])
		{
			M_WeaponWheel_FinishMouseClick();
			return;
		}

		if (item != weaponwheelmenu.mouse_item)
			weaponwheelmenu.mouse_dragged = true;

		if (weaponwheelmenu.mouse_item >= 0 &&
			weaponwheelmenu.mouse_item < weaponwheelmenu.visible_count &&
			item >= 0 && item < weaponwheelmenu.visible_count)
		{
			M_WeaponWheel_MoveItem(weaponwheelmenu.mouse_item, item);
		}
		return;
	}

	if (item >= 0)
		weaponwheelmenu.cursor = item;
}

/*
==================
Keys Menu
==================
*/

typedef struct
{
	const char *cmd;
	const char *desc;
	keydevicemask_t devicemask;
} defaultbind_t;

#define KEYBIND_CUSTOM_MARKER	"*"
#define QUICKSAVE	"echo Quicksaving...; wait; save quick"
#define QUICKLOAD	"echo Quickloading...; wait; load quick"

// The marker pair brackets the gameplay/weapon section where bindlist.lst
// entries may override default labels instead of being suppressed as duplicates.
static const defaultbind_t quakebindnames[] = // woods use iw quake bind names
{
	{"+forward",		"Move forward",			KDM_KEYBOARD_AND_MOUSE},
	{"+back",			"Move backward",		KDM_KEYBOARD_AND_MOUSE},
	{"+moveleft",		"Move left",			KDM_KEYBOARD_AND_MOUSE},
	{"+moveright",		"Move right",			KDM_KEYBOARD_AND_MOUSE},
	{"+jump",			"Jump / swim up",		KDM_ANY},
	{"+moveup",			"Swim up",				KDM_ANY},
	{"+movedown",		"Swim down",			KDM_ANY},
	{"+speed",			"Run",					KDM_KEYBOARD_AND_MOUSE},
	{"+strafe",			"Sidestep",				KDM_KEYBOARD_AND_MOUSE},
	{"+left",			"Turn left",			KDM_KEYBOARD_AND_MOUSE},
	{"+right",			"Turn right",			KDM_KEYBOARD_AND_MOUSE},
	{"+lookup",			"Look up",				KDM_KEYBOARD_AND_MOUSE},
	{"+lookdown",		"Look down",			KDM_KEYBOARD_AND_MOUSE},
	{"centerview",		"Center view",			KDM_ANY},
	{"levelview",		"Level view",			KDM_ANY},
	{"zoom_in",			"Toggle zoom",			KDM_ANY},
	{"+zoom",			"Quick zoom",			KDM_ANY},
	{"+gyroaction",		"Gyro switch",			KDM_GAMEPAD},
	{"+altmodifier",	"Alt modifier",			KDM_GAMEPAD},
	{KEYBIND_CUSTOM_MARKER, "",		KDM_NONE},
	{"+attack",			"Attack",				KDM_ANY},
	{"+weaponwheel",	"Weapon wheel",			KDM_ANY},
	{"impulse 10",		"Next weapon",			KDM_ANY},
	{"impulse 12",		"Previous weapon",		KDM_ANY},
	{"impulse 1",		"Axe",					KDM_ANY},
	{"impulse 2",		"Shotgun",				KDM_ANY},
	{"impulse 3",		"Super Shotgun",		KDM_ANY},
	{"impulse 4",		"Nailgun",				KDM_ANY},
	{"impulse 5",		"Super Nailgun",		KDM_ANY},
	{"impulse 6",		"Grenade Launcher",		KDM_ANY},
	{"impulse 7",		"Rocket Launcher",		KDM_ANY},
	{"impulse 8",		"Thunderbolt",			KDM_ANY},
	{"impulse 225",		"Laser Cannon",			KDM_ANY},
	{"impulse 226",		"Mjolnir",				KDM_ANY},
	{KEYBIND_CUSTOM_MARKER, "",		KDM_NONE},
	{QUICKSAVE,			"Quick save",			KDM_ANY},
	{QUICKLOAD,			"Quick load",			KDM_ANY},
	{"menu_load",		"Load menu",			KDM_ANY},
	{"menu_save",		"Save menu",			KDM_ANY},
	{"menu_multiplayer",	"Multiplayer menu",		KDM_ANY},
	{"menu_options",	"Options menu",			KDM_ANY},
	{"screenshot",		"Screenshot",			KDM_ANY},
	{"+showscores",		"Show score",			KDM_ANY},
	{"messagemode",		"Text chat",			KDM_KEYBOARD_AND_MOUSE},
};
#define	NUMQUAKECOMMANDS	(sizeof(quakebindnames)/sizeof(quakebindnames[0]))


#define MAX_VIS_KEYS	14 // woods #mousemenu
#define KEYS_TAB_Y		40
#define KEYS_LIST_Y		56

static struct
{
	menulist_t           list;
	struct {
		char text[32];
		int len;
		int maxlen;
	} search;
	keydevicemask_t devicemask;
	int* filtered_indices;
	int num_filtered;
	qboolean scrollbar_grab;  // Add this
	int x, y, cols;          // Add these for scrollbar positioning
} keysmenu;

typedef struct { // woods #mousemenu
	char* cmd;
	char* desc;
	keydevicemask_t devicemask;
} bindname_t;

static bindname_t* bindnames = NULL; // woods #mousemenu
static int numbindnames = 0; // woods #mousemenu

qboolean	bind_grab;

static void M_Keys_UpdateFilter(void);

static void M_Keys_SetDeviceMask(keydevicemask_t devicemask)
{
	if (devicemask == KDM_NONE || keysmenu.devicemask == devicemask)
		return;

	keysmenu.devicemask = devicemask;
	M_Keys_UpdateFilter();
}

static keydevicemask_t M_Keys_GetTabAtPoint(int x, int y)
{
	if (y < KEYS_TAB_Y || y >= KEYS_TAB_Y + 8)
		return KDM_NONE;

	return x < 160 ? KDM_KEYBOARD_AND_MOUSE : KDM_GAMEPAD;
}

static void M_Keys_CopyBindName (bindname_t *bindname, const char *cmd, const char *desc, keydevicemask_t devicemask)
{
	bindname->cmd = (char *)Z_Malloc(strlen(cmd) + 1);
	strcpy(bindname->cmd, cmd);
	bindname->desc = (char *)Z_Malloc(strlen(desc) + 1);
	strcpy(bindname->desc, desc);
	bindname->devicemask = devicemask;
}

static void M_Keys_AddBindName (const char *cmd, const char *desc, keydevicemask_t devicemask)
{
	bindnames = (bindname_t *)Z_Realloc(bindnames, sizeof(bindname_t) * (numbindnames + 1));
	M_Keys_CopyBindName(&bindnames[numbindnames], cmd, desc, devicemask);
	numbindnames++;
}

static void M_Keys_ClearBindNames (void)
{
	int i;

	for (i = 0; i < numbindnames; i++)
	{
		Z_Free(bindnames[i].cmd);
		Z_Free(bindnames[i].desc);
	}
	Z_Free(bindnames);
	bindnames = NULL;
	numbindnames = 0;
}

static qboolean M_Keys_IsCustomMarker (const char *cmd)
{
	return cmd[0] == KEYBIND_CUSTOM_MARKER[0] && cmd[1] == '\0';
}

static qboolean M_Keys_IsHipnoticOnlyCommand (const char *cmd)
{
	return !strcmp(cmd, "impulse 225") || !strcmp(cmd, "impulse 226");
}

static const defaultbind_t *M_Keys_FindDefaultBind (const char *cmd)
{
	int i;

	for (i = 0; i < NUMQUAKECOMMANDS; i++)
	{
		if (M_Keys_IsCustomMarker(quakebindnames[i].cmd))
			continue;
		if (!strcmp(quakebindnames[i].cmd, cmd))
			return &quakebindnames[i];
	}

	return NULL;
}

static qboolean M_Keys_HasCommand (const char *cmd)
{
	int i;

	for (i = 0; i < numbindnames; i++)
	{
		if (!strcmp(bindnames[i].cmd, cmd))
			return true;
	}

	return false;
}

static qboolean M_Keys_ShouldSuppressCustomBind (const char *cmd)
{
	int i;
	qboolean filter_enabled = true;

	if (!cmd[0])
		return true;

	for (i = 0; i < NUMQUAKECOMMANDS; i++)
	{
		const defaultbind_t *defaultbind = &quakebindnames[i];

		if (M_Keys_IsCustomMarker(defaultbind->cmd))
		{
			filter_enabled = !filter_enabled;
			continue;
		}

		if (!filter_enabled)
			continue;
		if (!strcmp(defaultbind->cmd, cmd))
			return true;
	}

	return false;
}

static qboolean M_Keys_IsDeprecatedBindCommand (const char *cmd)
{
	static const char *const deprecated[] =
	{
		"+klook",
		"+mlook",
	};
	int i;

	for (i = 0; i < Q_COUNTOF(deprecated); i++)
	{
		if (!strcmp(deprecated[i], cmd))
			return true;
	}

	return false;
}

static qboolean M_Keys_CustomCommandSupported (const char *cmd, const char *desc)
{
	if (!cmd[0])
		return false;

	COM_Parse(cmd);
	if (!Cmd_Exists(com_token) && !Cmd_AliasExists(com_token) && !Cvar_FindVar(com_token))
	{
		Con_DPrintf("Skipping unsupported key binding: \"%s\" = \"%s\"\n", desc, cmd);
		return false;
	}

	if (M_Keys_IsDeprecatedBindCommand(cmd))
	{
		Con_DPrintf("Skipping deprecated key binding: \"%s\" = \"%s\"\n", desc, cmd);
		return false;
	}

	return true;
}

static void M_Keys_AddBindNameUnique (const char *cmd, const char *desc, keydevicemask_t devicemask)
{
	if (!cmd[0] || M_Keys_HasCommand(cmd))
		return;

	M_Keys_AddBindName(cmd, desc, devicemask);
}

static void M_Keys_AddDefaultBind (const defaultbind_t *defaultbind)
{
	if (!hipnotic && M_Keys_IsHipnoticOnlyCommand(defaultbind->cmd))
		return;

	M_Keys_AddBindNameUnique(defaultbind->cmd, defaultbind->desc, defaultbind->devicemask);
}

static void M_Keys_LoadCustomBindList (bindname_t **custombinds, int *numcustombinds)
{
	FILE* file;
	char line[1024];

	*custombinds = NULL;
	*numcustombinds = 0;

	if (COM_FOpenFile("bindlist.lst", &file, NULL) < 0 || !file)
		return;

	while (fgets(line, sizeof(line), file))
	{
		const char* cmd, * desc;
		const defaultbind_t *defaultbind;

		Cmd_TokenizeString(line);
		cmd = Cmd_Argv(0);
		desc = Cmd_Argv(1);

		if (!cmd[0] || !desc[0] || (cmd[0] == '-' && cmd[1] == '\0'))
			continue;
		if (M_Keys_ShouldSuppressCustomBind(cmd))
			continue;
		if (!M_Keys_CustomCommandSupported(cmd, desc))
			continue;

		defaultbind = M_Keys_FindDefaultBind(cmd);
		*custombinds = (bindname_t *)Z_Realloc(*custombinds, sizeof(bindname_t) * (*numcustombinds + 1));
		M_Keys_CopyBindName(&(*custombinds)[*numcustombinds], cmd, desc, defaultbind ? defaultbind->devicemask : KDM_ANY);
		(*numcustombinds)++;
	}

	fclose(file);
}

static void M_Keys_FreeCustomBindList (bindname_t *custombinds, int numcustombinds)
{
	int i;

	for (i = 0; i < numcustombinds; i++)
	{
		Z_Free(custombinds[i].cmd);
		Z_Free(custombinds[i].desc);
	}
	Z_Free(custombinds);
}

static void M_Keys_AddCustomBindList (bindname_t *custombinds, int numcustombinds)
{
	int i;

	for (i = 0; i < numcustombinds; i++)
		M_Keys_AddBindNameUnique(custombinds[i].cmd, custombinds[i].desc, custombinds[i].devicemask);
}

static void M_Keys_Populate(void) // woods #mousemenu -- modified
{
	bindname_t *custombinds;
	int numcustombinds;
	qboolean added_custom_entries = false;
	int i;

	M_Keys_ClearBindNames();
	M_Keys_LoadCustomBindList(&custombinds, &numcustombinds);

	for (i = 0; i < NUMQUAKECOMMANDS; i++)
	{
		const defaultbind_t *defaultbind = &quakebindnames[i];

		if (M_Keys_IsCustomMarker(defaultbind->cmd))
		{
			if (!added_custom_entries)
			{
				M_Keys_AddCustomBindList(custombinds, numcustombinds);
				added_custom_entries = true;
			}
			continue;
		}

		M_Keys_AddDefaultBind(defaultbind);
	}

	M_Keys_FreeCustomBindList(custombinds, numcustombinds);
}

void M_Keys_UpdateFilter(void)
{
	keysmenu.num_filtered = 0;
	keysmenu.list.scroll = 0;  // Reset scroll position when filtering

	// First pass: count matches
	for (int i = 0; i < numbindnames; i++)
	{
		if (!(bindnames[i].devicemask & keysmenu.devicemask))
			continue;

		if (keysmenu.search.len == 0)
		{
			keysmenu.num_filtered++;
			continue;
		}
		else
		{
			const char* desc = bindnames[i].desc;
			const char* cmd = bindnames[i].cmd;
			const char* search = keysmenu.search.text;

			char desc_lower[128] = { 0 };
			char cmd_lower[128] = { 0 };
			char search_lower[32] = { 0 };

			Q_strncpy(desc_lower, desc, sizeof(desc_lower) - 1);
			Q_strncpy(cmd_lower, cmd, sizeof(cmd_lower) - 1);
			Q_strncpy(search_lower, search, sizeof(search_lower) - 1);

			// Convert to lowercase
			for (char* p = desc_lower; *p; p++) *p = q_tolower(*p);
			for (char* p = cmd_lower; *p; p++) *p = q_tolower(*p);
			for (char* p = search_lower; *p; p++) *p = q_tolower(*p);

			if (strstr(desc_lower, search_lower) || strstr(cmd_lower, search_lower))
			{
				keysmenu.num_filtered++;
			}
		}
	}

	// Allocate or reallocate filtered indices array
	if (keysmenu.filtered_indices)
		Z_Free(keysmenu.filtered_indices);
	keysmenu.filtered_indices = (int*)Z_Malloc(keysmenu.num_filtered * sizeof(int));

	// Second pass: fill indices
	if (keysmenu.search.len == 0)
	{
		// No search, just copy all indices
		int filter_idx = 0;
		for (int i = 0; i < numbindnames; i++)
		{
			if (bindnames[i].devicemask & keysmenu.devicemask)
				keysmenu.filtered_indices[filter_idx++] = i;
		}
	}
	else
	{
		// Fill with matching indices
		int filter_idx = 0;
		for (int i = 0; i < numbindnames; i++)
		{
			if (!(bindnames[i].devicemask & keysmenu.devicemask))
				continue;

			const char* desc = bindnames[i].desc;
			const char* cmd = bindnames[i].cmd;
			const char* search = keysmenu.search.text;

			char desc_lower[128] = { 0 };
			char cmd_lower[128] = { 0 };
			char search_lower[32] = { 0 };

			Q_strncpy(desc_lower, desc, sizeof(desc_lower) - 1);
			Q_strncpy(cmd_lower, cmd, sizeof(cmd_lower) - 1);
			Q_strncpy(search_lower, search, sizeof(search_lower) - 1);

			// Convert to lowercase
			for (char* p = desc_lower; *p; p++) *p = q_tolower(*p);
			for (char* p = cmd_lower; *p; p++) *p = q_tolower(*p);
			for (char* p = search_lower; *p; p++) *p = q_tolower(*p);

			if (strstr(desc_lower, search_lower) || strstr(cmd_lower, search_lower))
			{
				keysmenu.filtered_indices[filter_idx++] = i;
			}
		}
	}

	// Update menu list state
	keysmenu.list.numitems = keysmenu.num_filtered;
	if (keysmenu.list.cursor >= keysmenu.num_filtered)
		keysmenu.list.cursor = keysmenu.num_filtered - 1;
	if (keysmenu.list.cursor < 0)
		keysmenu.list.cursor = 0;
}

void M_Menu_Keys_f(void)
{
	key_dest = key_menu;
	m_state = m_keys;
	m_entersound = true;

	M_Keys_Populate();

	keysmenu.list.viewsize = MAX_VIS_KEYS;
	keysmenu.list.cursor = 0;
	keysmenu.list.scroll = 0;
	keysmenu.list.numitems = numbindnames;

	keysmenu.search.len = 0;
	keysmenu.search.text[0] = 0;
	keysmenu.search.maxlen = sizeof(keysmenu.search.text) - 1;
	keysmenu.devicemask = (IN_GetLastActiveDeviceType() == KD_GAMEPAD) ? KDM_GAMEPAD : KDM_KEYBOARD_AND_MOUSE;

	keysmenu.scrollbar_grab = false;
	keysmenu.x = 0;
	keysmenu.y = KEYS_LIST_Y;
	keysmenu.cols = 36;

	// Initialize filtered indices array
	if (keysmenu.filtered_indices)
		Z_Free(keysmenu.filtered_indices);
	keysmenu.filtered_indices = (int*)Z_Malloc(numbindnames * sizeof(int));
	keysmenu.num_filtered = 0;
	M_Keys_UpdateFilter();

	IN_UpdateGrabs();
}

qboolean IsCompleteCommand(const char* binding, const char* command)
{
	// Check if commands are exactly equal
	if (!strcmp(binding, command))
		return true;

	// For impulse commands, ensure we're matching complete numbers
	if (strstr(command, "impulse ") == command)
	{
		// If binding also starts with "impulse "
		if (strstr(binding, "impulse ") == binding)
		{
			// Compare the numbers after "impulse "
			const char* bind_num = binding + 8;
			const char* cmd_num = command + 8;

			// Check if the numbers match exactly
			char* bind_end;
			char* cmd_end;
			int bind_val = strtol(bind_num, &bind_end, 10);
			int cmd_val = strtol(cmd_num, &cmd_end, 10);

			// Make sure we consumed all digits and the numbers match
			return (*bind_end == '\0' && *cmd_end == '\0' && bind_val == cmd_val);
		}
	}
	return false;
}

void M_FindKeysForCommand (const char *command, int *threekeys)
{
	Key_GetKeysForCommand(command, threekeys, 3, keysmenu.devicemask);
}

static int M_Keys_GetPrimaryBindmap (void)
{
	if (key_bindmap[0] >= 0 && key_bindmap[0] < MAX_BINDMAPS)
		return key_bindmap[0];
	return 0;
}

void M_UnbindCommand (const char *command)
{
	int active_bindmaps[2];
	int i, j;

	active_bindmaps[0] = M_Keys_GetPrimaryBindmap();
	active_bindmaps[1] = (key_bindmap[1] >= 0 && key_bindmap[1] < MAX_BINDMAPS) ? key_bindmap[1] : -1;

	for (i = 0; i < 2; i++)
	{
		int bindmap = active_bindmaps[i];

		if (bindmap < 0 || (i > 0 && bindmap == active_bindmaps[0]))
			continue;

		for (j = 0; j < MAX_KEYS; j++)
		{
			char *b = keybindings[bindmap][j];

			if (!b)
				continue;
			if (IsCompleteCommand(b, command) && (Key_GetDeviceMaskForKeynum(j) & keysmenu.devicemask))
				Key_SetBinding(j, NULL, bindmap);
		}
	}
}

extern qpic_t	*pic_up, *pic_down;

void M_Keys_Draw(void)
{
	int firstvis, numvis, x, y, cols;
	qpic_t* p;
	const char* hint = NULL;

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	{
		qboolean gamepad_connected = IN_HasGamepad();
		qboolean gamepad_active = (keysmenu.devicemask == KDM_GAMEPAD);

		if (gamepad_active)
			M_Print(16, KEYS_TAB_Y, "Keyboard & Mouse");
		else
			M_PrintWhite(16, KEYS_TAB_Y, "Keyboard & Mouse");

		if (!gamepad_connected)
			M_PrintRGBA(184, KEYS_TAB_Y, "Gamepad", CL_PLColours_Parse("0xffffff"), 0.375f, !gamepad_active);
		else if (gamepad_active)
			M_PrintWhite(184, KEYS_TAB_Y, "Gamepad");
		else
			M_Print(184, KEYS_TAB_Y, "Gamepad");
	}

	x = 0;
	y = keysmenu.y;
	cols = 36;

	// Get visible range
	M_List_GetVisibleRange(&keysmenu.list, &firstvis, &numvis);

	// Draw scroll indicators
	if (keysmenu.list.scroll > 0)
		M_DrawEllipsisBar(x, y - 8, cols);
	if (keysmenu.list.scroll + keysmenu.list.viewsize < keysmenu.num_filtered)
		M_DrawEllipsisBar(x, y + keysmenu.list.viewsize * 8, cols);

	if (M_List_GetOverflow(&keysmenu.list) > 0)
	{
		M_List_DrawScrollbar(&keysmenu.list, keysmenu.x + keysmenu.cols * 8 - 8, keysmenu.y);
	}

	// Draw visible items
	for (int i = 0; i < numvis; i++)
	{
		int list_index = firstvis + i;
		if (list_index >= keysmenu.num_filtered)
			break;

		int actual_idx = keysmenu.filtered_indices[list_index];
		qboolean is_selected = (list_index == keysmenu.list.cursor && bind_grab);

		void (*print_fn)(int, int, const char*) = is_selected ? M_PrintWhite : M_Print;
		print_fn(0, y, bindnames[actual_idx].desc);

		int keys[3];
		M_FindKeysForCommand(bindnames[actual_idx].cmd, keys);
		if (list_index == keysmenu.list.cursor && bind_grab && keys[2] != -1)
			keys[0] = -1;

		int x_pos = 136;
		if (keys[0] != -1)
		{
			const char* keyStr = Key_KeynumToFriendlyString(keys[0]);
			print_fn(x_pos, y, keyStr);
			x_pos += (strlen(keyStr) * 8);

			for (int j = 1; j < 3 && keys[j] != -1; j++)
			{
				qboolean masked = !is_selected;
				float alpha = 0.5f;
				M_PrintRGBA(x_pos, y, ",", CL_PLColours_Parse("0xffffff"), alpha, masked);
				x_pos += 8;  // Comma width
				M_PrintRGBA(x_pos, y, " ", CL_PLColours_Parse("0xffffff"), alpha, masked);
				x_pos += 8;  // Space width
				keyStr = Key_KeynumToFriendlyString(keys[j]);
				print_fn(x_pos, y, keyStr);
				x_pos += (strlen(keyStr) * 8);
			}
		}
		else
		{
			qboolean masked = !is_selected;
			float alpha = masked ? 0.5f : 1.0f;
			M_PrintRGBA(x_pos, y,
				(bind_grab && list_index == keysmenu.list.cursor && Key_GetGamepadAltModifierState()) ? "Alt-???" : "???",
				CL_PLColours_Parse("0xffffff"), alpha, masked);
		}

		if (list_index == keysmenu.list.cursor)
		{
			M_DrawCharacter(128, y, bind_grab ? '=' : 12 + ((int)(realtime * 4) & 1));
		}
		y += 8;
	}

	// Draw search box
	if (keysmenu.search.len > 0)
	{
		M_DrawTextBox(16, 174, 32, 1);
		M_PrintHighlight(24, 182, keysmenu.search.text,
			keysmenu.search.text,
			keysmenu.search.len);
		int cursor_x = 24 + 8 * keysmenu.search.len;
		if (keysmenu.num_filtered == 0)
			M_DrawCharacter(cursor_x, 182, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, 182, 10 + ((int)(realtime * 4) & 1));
	}
	else
	{
		if (bind_grab)
		{
			hint = keysmenu.devicemask == KDM_GAMEPAD ?
				va("Press new button, or %s to cancel", Key_KeynumToFriendlyString(K_START)) :
				va("Press new key, or %s to cancel", Key_KeynumToFriendlyString(K_ESCAPE));
		}
		else if (keysmenu.devicemask == KDM_GAMEPAD)
		{
			hint = va("%s = change, %s = clear", Key_KeynumToFriendlyString(K_ABUTTON), Key_KeynumToFriendlyString(K_YBUTTON));
		}
		else
		{
			hint = va("%s = change, %s = clear", Key_KeynumToFriendlyString(K_ENTER), Key_KeynumToFriendlyString(K_BACKSPACE));
		}

		M_PrintWhite((320 - 8 * strlen(hint)) / 2, 182, hint);
	}
}

void M_Keys_Key(int k)
{
	int x, y;
	keydevicemask_t clickedmask = KDM_NONE;

	if (keysmenu.scrollbar_grab)
	{
		switch (k)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			keysmenu.scrollbar_grab = false;
			break;
		}
		return;
	}

	if (k == K_MOUSE1)
	{
		clickedmask = M_Keys_GetTabAtPoint(m_mousex, m_mousey);
		if (clickedmask != KDM_NONE)
		{
			if (clickedmask == KDM_GAMEPAD && !IN_HasGamepad())
				return;

			if (bind_grab)
			{
				bind_grab = false;
				IN_UpdateGrabs();
			}

			if (clickedmask != keysmenu.devicemask)
			{
				S_LocalSound("misc/menu1.wav");
				M_Keys_SetDeviceMask(clickedmask);
			}
			return;
		}
	}
	
	char    cmd[80];
	if (bind_grab)
	{   // defining a key
		S_LocalSound("misc/menu1.wav");
		if (k != K_ESCAPE && k != K_BBUTTON && k != '`')
		{
			int actual_idx = keysmenu.filtered_indices[keysmenu.list.cursor];
			const char *command = bindnames[actual_idx].cmd;
			int keys[3];

			if (!(Key_GetDeviceMaskForKeynum(k) & keysmenu.devicemask))
				return;
			if (strcmp(command, "+altmodifier"))
			{
				if (Key_IsKeyGamepadAltModifier(k))
					return;
				if (Key_GetGamepadAltModifierState() && k >= K_LTHUMB && k <= K_TOUCHPAD)
					k += K_LTHUMB_ALT - K_LTHUMB;
			}

			M_FindKeysForCommand(command, keys);
			if (keys[2] != -1)
				M_UnbindCommand(command);
			sprintf(cmd, "in_bind %i \"%s\" \"%s\"\n", M_Keys_GetPrimaryBindmap(), Key_KeynumToString(k), command);
			Cbuf_InsertText(cmd);
		}
		bind_grab = false;
		IN_UpdateGrabs();
		return;
	}

	if (k == K_TAB || k == K_LSHOULDER || k == K_RSHOULDER)
	{
		if (keysmenu.devicemask != KDM_GAMEPAD && !IN_HasGamepad())
			return;
		S_LocalSound("misc/menu1.wav");
		M_Keys_SetDeviceMask((keysmenu.devicemask == KDM_GAMEPAD) ? KDM_KEYBOARD_AND_MOUSE : KDM_GAMEPAD);
		return;
	}

	if (keydown[K_CTRL])
	{
		if ((k == 'u' || k == 'U') && keysmenu.search.len > 0)
		{
			// Clear entire search with Ctrl+U
			keysmenu.search.len = 0;
			keysmenu.search.text[0] = 0;
			M_Keys_UpdateFilter();
			return;
		}
		else if (k == K_BACKSPACE && keysmenu.search.len > 0)
		{
			// Delete previous word with Ctrl+Backspace
			listsearch_t temp;
			temp.len = keysmenu.search.len;
			Q_strcpy(temp.text, keysmenu.search.text);
			M_DeletePrevWord(&temp);
			Q_strcpy(keysmenu.search.text, temp.text);
			keysmenu.search.len = temp.len;
			M_Keys_UpdateFilter();
			return;
		}
	}

	// Handle search functionality first
	if (k >= 32 && k < 127) // Printable characters
	{
		if (keysmenu.search.len < keysmenu.search.maxlen)
		{
			keysmenu.search.text[keysmenu.search.len++] = k;
			keysmenu.search.text[keysmenu.search.len] = 0;
			M_Keys_UpdateFilter();
			return;
		}
	}

	if (k == K_BACKSPACE)
	{
		if (keysmenu.search.len > 0)
		{
			if (keydown[K_CTRL])
			{
				// Delete previous word instead of just one character
				listsearch_t temp;
				temp.len = keysmenu.search.len;
				Q_strcpy(temp.text, keysmenu.search.text);
				M_DeletePrevWord(&temp);
				Q_strcpy(keysmenu.search.text, temp.text);
				keysmenu.search.len = temp.len;
			}
			else
			{
				// Delete one character
			keysmenu.search.text[--keysmenu.search.len] = 0;
			}
			M_Keys_UpdateFilter();
			return;
		}
	}

	if (M_List_Key(&keysmenu.list, k))
		return;

	switch (k)
	{
	case K_ESCAPE:
		if (keysmenu.search.len > 0)
		{
			// Clear search but stay in menu
			keysmenu.search.len = 0;
			keysmenu.search.text[0] = 0;
			M_Keys_UpdateFilter();
			return;
		}
		// Fall through to exit menu if search is already empty
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		M_Menu_Options_f();
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1:
	{
		if (keysmenu.num_filtered <= 0)
			break;
		x = m_mousex - keysmenu.x - (keysmenu.cols - 1) * 8;
		y = m_mousey - keysmenu.y;
		if (x < -8 || !M_List_UseScrollbar(&keysmenu.list, y))
		{
			// Handle normal click
			S_LocalSound("misc/menu2.wav");
			bind_grab = true;
			M_List_AutoScroll(&keysmenu.list);
			IN_UpdateGrabs();
		}
		else
		{
			keysmenu.scrollbar_grab = true;
			M_Keys_Mousemove(m_mousex, m_mousey);
		}
		break;
	}

	case K_BACKSPACE:
	case K_DEL:
	case K_YBUTTON:
		if (!keysmenu.search.len && keysmenu.num_filtered > 0)  // Only delete binding if not searching
		{
			S_LocalSound("misc/menu2.wav");
			int actual_idx = keysmenu.filtered_indices[keysmenu.list.cursor];
			M_UnbindCommand(bindnames[actual_idx].cmd);
		}
		break;
	}
}

void M_Keys_Mousemove(int cx, int cy)
{
	cy -= keysmenu.y;

	if (keysmenu.scrollbar_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			keysmenu.scrollbar_grab = false;
			return;
		}
		M_List_UseScrollbar(&keysmenu.list, cy);
		// Note: no return, we also update the cursor
	}

	M_List_Mousemove(&keysmenu.list, cy);
}

/*
==================
Mouse Menu
==================
*/

typedef const char* (*menu_search_gettext_fn)(int index);

static int M_Menu_ClampCursorValue(int cursor, int total_items)
{
	if (total_items <= 0)
		return 0;

	if (cursor < 0 || cursor >= total_items)
	{
		cursor %= total_items;
		if (cursor < 0)
			cursor += total_items;
	}

	return cursor;
}

static qboolean M_Menu_SearchItemMatches(menu_search_gettext_fn get_item_text,
	const char* search_text, int index)
{
	const char* itemtext = get_item_text(index);

	return itemtext && q_strcasestr(itemtext, search_text);
}

static int M_Menu_UpdateSearchCursor(int total_items, int cursor,
	int* filtered_count, menu_search_gettext_fn get_item_text,
	const char* search_text, int search_len)
{
	int i;
	int first_match = -1;
	const qboolean has_search = search_len > 0;
	qboolean current_matches;

	cursor = M_Menu_ClampCursorValue(cursor, total_items);
	current_matches = has_search &&
		M_Menu_SearchItemMatches(get_item_text, search_text, cursor);

	if (!has_search)
	{
		*filtered_count = total_items;
		return cursor;
	}

	*filtered_count = 0;
	for (i = 0; i < total_items; i++)
	{
		if (M_Menu_SearchItemMatches(get_item_text, search_text, i))
		{
			if (first_match < 0)
				first_match = i;
			(*filtered_count)++;
		}
	}

	if (*filtered_count > 0 && !current_matches)
		cursor = first_match;

	return cursor;
}

static int M_Menu_MoveSearchCursor(int total_items, int filtered_count,
	int cursor, int delta, menu_search_gettext_fn get_item_text,
	const char* search_text, int search_len)
{
	int i;

	cursor = M_Menu_ClampCursorValue(cursor, total_items);

	if (search_len <= 0)
	{
		cursor += delta;
		cursor %= total_items;
		if (cursor < 0)
			cursor += total_items;
		return cursor;
	}

	if (filtered_count <= 0)
		return cursor;

	for (i = 0; i < total_items; i++)
	{
		cursor += delta;
		cursor %= total_items;
		if (cursor < 0)
			cursor += total_items;

		if (M_Menu_SearchItemMatches(get_item_text, search_text, cursor))
			return cursor;
	}

	return cursor;
}

static size_t M_Menu_CommonPrefixLength(const char *a, const char *b)
{
	size_t i = 0;

	while (a[i] && b[i] && q_tolower((unsigned char)a[i]) == q_tolower((unsigned char)b[i]))
		++i;

	return i;
}

static qboolean M_Menu_TabCompleteFileList(menu_textfield_t *field,
	char *buffer, size_t buffer_size, filelist_item_t *list,
	char *tab_partial, size_t tab_partial_size)
{
	const filelist_item_t *item;
	const filelist_item_t *first_match = NULL;
	const filelist_item_t *last_match = NULL;
	const filelist_item_t *prev_match = NULL;
	const filelist_item_t *current_match = NULL;
	const filelist_item_t *next_match = NULL;
	char prefix[MAXCMDLINE];
	char completed[MAXCMDLINE];
	const char *replacement = NULL;
	size_t prefix_len, partial_len, common_len = 0;
	int match_count = 0;
	qboolean first_cycle = !tab_partial[0];

	if (!buffer_size)
		return false;

	prefix_len = (size_t)CLAMP(0, field->cursor, (int)strlen(buffer));
	if (prefix_len >= sizeof(prefix))
		prefix_len = sizeof(prefix) - 1;

	memcpy(prefix, buffer, prefix_len);
	prefix[prefix_len] = '\0';

	if (first_cycle)
		q_strlcpy(tab_partial, prefix, tab_partial_size);

	partial_len = strlen(tab_partial);

	for (item = list; item; item = item->next)
	{
		if (partial_len && q_strncasecmp(item->name, tab_partial, partial_len) != 0)
			continue;

		if (!first_match)
		{
			first_match = item;
			common_len = strlen(item->name);
		}
		else
		{
			common_len = q_min(common_len, M_Menu_CommonPrefixLength(first_match->name, item->name));
		}

		if (current_match && !next_match)
			next_match = item;

		if (!q_strcasecmp(item->name, prefix))
			current_match = item;
		else if (!current_match)
			prev_match = item;

		last_match = item;
		++match_count;
	}

	if (!first_match)
		return false;

	if (first_cycle)
	{
		if (match_count == 1)
		{
			replacement = first_match->name;
		}
		else if (common_len > partial_len)
		{
			memcpy(completed, first_match->name, common_len);
			completed[common_len] = '\0';
			replacement = completed;
		}
		else
		{
			replacement = keydown[K_SHIFT] ? last_match->name : first_match->name;
		}
	}
	else if (current_match)
	{
		replacement = keydown[K_SHIFT]
			? (prev_match ? prev_match->name : last_match->name)
			: (next_match ? next_match->name : first_match->name);
	}
	else
	{
		replacement = keydown[K_SHIFT] ? last_match->name : first_match->name;
	}

	q_strlcpy(completed, replacement, sizeof(completed));
	q_strlcat(completed, buffer + prefix_len, sizeof(completed));

	if (!strcmp(buffer, completed) && field->cursor == (int)strlen(replacement))
		return false;

	q_strlcpy(buffer, completed, buffer_size);
	field->cursor = (int)strlen(replacement);
	field->sel_start = -1;
	M_TextField_ClampCursor(field);
	return true;
}

/*
=================
M_Menu_TabCompleteNameHistory -- woods #namehistory

Like M_Menu_TabCompleteFileList but walks namehistorylist and matches against
the dequaked (plain ascii) form of each stored name via substring search
(matching Con_Match / console behaviour), so the user can type any portion of
a saved name — including one that contains quake special chars — and have the
original name inserted with its colours preserved.
=================
*/
static qboolean M_Menu_TabCompleteNameHistory(menu_textfield_t *field,
	char *buffer, size_t buffer_size,
	char *tab_partial, size_t tab_partial_size)
{
	extern char unfun[129];
	const filelist_item_t *item;
	const filelist_item_t *first_match = NULL;
	const filelist_item_t *last_match = NULL;
	const filelist_item_t *prev_match = NULL;
	const filelist_item_t *current_match = NULL;
	const filelist_item_t *next_match = NULL;
	char prefix[MAXCMDLINE];
	char unfun_prefix[MAXCMDLINE];
	char unfun_partial[MAXCMDLINE];
	char unfun_name[MAXCMDLINE];
	const char *replacement = NULL;
	size_t prefix_len;
	size_t i;
	qboolean first_cycle = !tab_partial[0];

	if (!buffer_size)
		return false;

	prefix_len = (size_t)CLAMP(0, field->cursor, (int)strlen(buffer));
	if (prefix_len >= sizeof(prefix))
		prefix_len = sizeof(prefix) - 1;
	memcpy(prefix, buffer, prefix_len);
	prefix[prefix_len] = '\0';

	if (first_cycle)
		q_strlcpy(tab_partial, prefix, tab_partial_size);

	/* dequake the saved partial for substring matching */
	for (i = 0; tab_partial[i] && i < sizeof(unfun_partial) - 1; i++)
		unfun_partial[i] = unfun[tab_partial[i] & 127];
	unfun_partial[i] = '\0';

	/* dequake the current buffer for "is this the current match?" check */
	for (i = 0; prefix[i] && i < sizeof(unfun_prefix) - 1; i++)
		unfun_prefix[i] = unfun[prefix[i] & 127];
	unfun_prefix[i] = '\0';

	for (item = namehistorylist; item; item = item->next)
	{
		for (i = 0; item->name[i] && i < sizeof(unfun_name) - 1; i++)
			unfun_name[i] = unfun[item->name[i] & 127];
		unfun_name[i] = '\0';

		/* substring match -- matches Con_Match / console behaviour */
		if (unfun_partial[0] && !q_strcasestr(unfun_name, unfun_partial))
			continue;

		if (!first_match)
			first_match = item;

		if (current_match && !next_match)
			next_match = item;

		if (!q_strcasecmp(unfun_name, unfun_prefix))
			current_match = item;
		else if (!current_match)
			prev_match = item;

		last_match = item;
	}

	if (!first_match)
		return false;

	/* always cycle through full names (no common-prefix shortcut —
	   substring matches may appear at different offsets) */
	if (first_cycle)
	{
		replacement = keydown[K_SHIFT] ? last_match->name : first_match->name;
	}
	else if (current_match)
	{
		replacement = keydown[K_SHIFT]
			? (prev_match ? prev_match->name : last_match->name)
			: (next_match ? next_match->name : first_match->name);
	}
	else
	{
		replacement = keydown[K_SHIFT] ? last_match->name : first_match->name;
	}

	if (!strcmp(buffer, replacement))
		return false;

	q_strlcpy(buffer, replacement, buffer_size);
	field->cursor = (int)strlen(buffer);
	field->sel_start = -1;
	M_TextField_ClampCursor(field);
	return true;
}

extern cvar_t cl_minpitch, cl_maxpitch;
extern cvar_t scr_customcursor;

#ifdef __APPLE__
#define MACOS_X_ACCELERATION_HACK
#endif

#ifdef MACOS_X_ACCELERATION_HACK
extern cvar_t in_disablemacosxmouseaccel;
#endif

static enum mouse_e
{
	MOUSE_SPEED,
	MOUSE_INVERT,
	MOUSE_ALWAYSMLOOK,
	MOUSE_PITCHMODE,
	MOUSE_CUSTOMCURSOR,
#ifdef MACOS_X_ACCELERATION_HACK
	MOUSE_ACCELERATION,
#endif
	MOUSE_COUNT
} mouse_cursor;

#define MOUSE_ITEMS (MOUSE_COUNT)
int numberOfMouseItems = MOUSE_ITEMS;


static void M_Mouse_SetPitchMode(qboolean netquake)
{
	if (netquake)
	{
		Cvar_SetValue("cl_minpitch", -69.99);
		Cvar_SetValue("cl_maxpitch", 79.99);
	}
	else
	{
		Cvar_SetValue("cl_minpitch", -90);
		Cvar_SetValue("cl_maxpitch", 90);
	}
}

static struct
{
	int cursor;
	struct {
		char text[32];
		int len;
	} search;
} mousemenu;

static qboolean mouse_slider_grab;

static const char* M_Mouse_GetItemText(int index)
{
	static char buffer[64];

	switch (index)
	{
	case MOUSE_SPEED:
		return "Mouse Speed";
	case MOUSE_INVERT:
		return "Invert Mouse";
	case MOUSE_ALWAYSMLOOK:
		return "Mouse Look";
	case MOUSE_PITCHMODE:
		return "Pitch Mode";
	case MOUSE_CUSTOMCURSOR:
		return "Custom Cursor";
#ifdef MACOS_X_ACCELERATION_HACK
	case MOUSE_ACCELERATION:
		return "Acceleration";
#endif
	default:
		q_snprintf(buffer, sizeof(buffer), "Unknown Item %d", index);
		return buffer;
	}
}

static void M_Mouse_UpdateSearch(void)
{
	mouse_cursor = (enum mouse_e)M_Menu_UpdateSearchCursor(
		MOUSE_ITEMS, (int)mouse_cursor, &numberOfMouseItems,
		M_Mouse_GetItemText, mousemenu.search.text, mousemenu.search.len);
}

static void M_Mouse_MoveCursor(int delta)
{
	mouse_cursor = (enum mouse_e)M_Menu_MoveSearchCursor(
		MOUSE_ITEMS, numberOfMouseItems, (int)mouse_cursor, delta,
		M_Mouse_GetItemText, mousemenu.search.text, mousemenu.search.len);
}

void M_Menu_Mouse_f(void)
{
	key_dest = key_menu;
	m_state = m_mouse;
	m_entersound = true;
	mouse_cursor = 0;
	mousemenu.cursor = 0;
	mousemenu.search.len = 0;
	mousemenu.search.text[0] = 0;
	numberOfMouseItems = MOUSE_ITEMS;

	IN_UpdateGrabs();
}

static void M_Mouse_AdjustSliders(int dir)
{
	float f;
	S_LocalSound("misc/menu3.wav");

	switch (mouse_cursor)
	{
	case MOUSE_SPEED:
		f = sensitivity.value + dir * 0.1;
		if (f > 20) f = 20;
		else if (f < 1) f = 1;
		Cvar_SetValue("sensitivity", f);
		break;

	case MOUSE_INVERT:
		Cvar_SetValue("m_pitch", -m_pitch.value);
		break;

	case MOUSE_ALWAYSMLOOK:
		if (in_mlook.state & 1)
			Cbuf_AddText("-mlook");
		else
			Cbuf_AddText("+mlook");
		break;

	case MOUSE_PITCHMODE:
		// Toggle between NetQuake and Quakespasm pitch modes
		if (cl_maxpitch.value >= 89)  // If currently Quakespasm mode
			M_Mouse_SetPitchMode(true);  // Switch to NetQuake
		else
			M_Mouse_SetPitchMode(false); // Switch to Quakespasm
		break;
	case MOUSE_CUSTOMCURSOR:
		Cvar_SetValue("scr_customcursor", !scr_customcursor.value);
		break;
#ifdef MACOS_X_ACCELERATION_HACK
	case MOUSE_ACCELERATION:
		Cvar_SetValue("in_disablemacosxmouseaccel", !in_disablemacosxmouseaccel.value);
		break;
#endif
	default:
		break;
	}
}

void M_Mouse_Draw(void)
{
	qpic_t* p;
	float r;
	enum mouse_e i;

	mouse_cursor = (enum mouse_e)M_Menu_ClampCursorValue((int)mouse_cursor, MOUSE_ITEMS);

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	const char* title = "Mouse Options";
	M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);

	for (i = 0; i < MOUSE_ITEMS; i++)
	{
		int y = 48 + 8 * i;
		const char* text = NULL;
		const char* value = NULL;

		switch (i)
		{
		case MOUSE_SPEED:
			text = "     Sensitivity";
			r = (sensitivity.value - 1) / 19;
			M_DrawSlider(186, y, r, sensitivity.value, "%.1f");
			break;

		case MOUSE_INVERT:
			text = "    Invert Mouse";
			M_DrawCheckbox(178, y, m_pitch.value < 0);
			break;

		case MOUSE_ALWAYSMLOOK:
			text = "      Mouse Look";
			M_DrawCheckbox(178, y, in_mlook.state & 1);
			break;

		case MOUSE_PITCHMODE:
			text = "      Pitch Mode";
			// Check current pitch settings to determine mode
			if (cl_maxpitch.value >= 89)
				value = "qs (straight up/down)";
			else
				value = "traditional ";
			M_Print(178, y, value);
			break;
		case MOUSE_CUSTOMCURSOR:
			text = "   Custom Cursor";
			M_DrawCheckbox(178, y, scr_customcursor.value);
			break;
#ifdef MACOS_X_ACCELERATION_HACK
		case MOUSE_ACCELERATION:
			text = "    Acceleration";
			M_DrawCheckbox(178, y, !in_disablemacosxmouseaccel.value);
			break;
#endif
		default:
			break;
		}

		if (text)
		{
			if (mousemenu.search.len > 0 &&
				q_strcasestr(text, mousemenu.search.text))
			{
				M_PrintHighlight(16, y, text,
					mousemenu.search.text,
					mousemenu.search.len);
			}
			else
			{
				M_Print(16, y, text);
			}
		}
	}

	// Draw cursor
	M_DrawCharacter(168, 48 + mouse_cursor * 8, 12 + ((int)(realtime * 4) & 1));

	// Draw search box if search is active
	if (mousemenu.search.len > 0)
	{
		M_DrawTextBox(16, 170, 32, 1);
		M_PrintHighlight(24, 178, mousemenu.search.text,
			mousemenu.search.text,
			mousemenu.search.len);
		int cursor_x = 24 + 8 * mousemenu.search.len;
		if (numberOfMouseItems == 0)
			M_DrawCharacter(cursor_x, 178, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, 178, 10 + ((int)(realtime * 4) & 1));
	}
}

void M_Mouse_Key(int k)
{
	// Handle slider grab release
	if (!keydown[K_MOUSE1])
		mouse_slider_grab = false;

	if (mouse_slider_grab)
	{
		switch (k)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			mouse_slider_grab = false;
			break;
		}
		return;
	}

	// Handle search functionality first
	if (k == K_ESCAPE)
	{
		if (mousemenu.search.len > 0)
		{
			mousemenu.search.len = 0;
			mousemenu.search.text[0] = 0;
			M_Mouse_UpdateSearch();
			return;
		}
		if (M_LivePreview_Alpha() > 0.f)
		{
			M_LivePreview_Reset();
			return;
		}
		M_Menu_Options_f();
		return;
	}
	else if (k == K_BACKSPACE)
	{
		if (mousemenu.search.len > 0)
		{
			if (keydown[K_CTRL])
			{
				// Delete previous word
				listsearch_t temp;
				temp.len = mousemenu.search.len;
				Q_strcpy(temp.text, mousemenu.search.text);
				M_DeletePrevWord(&temp);
				Q_strcpy(mousemenu.search.text, temp.text);
				mousemenu.search.len = temp.len;
			}
			else
			{
				// Delete one character
				mousemenu.search.text[--mousemenu.search.len] = 0;
			}

			M_Mouse_UpdateSearch();
			return;
		}
	}
	else if (k == 'u' || k == 'U')
	{
		if (keydown[K_CTRL] && mousemenu.search.len > 0)
		{
			mousemenu.search.len = 0;
			mousemenu.search.text[0] = 0;
			M_Mouse_UpdateSearch();
			return;
		}
	}
	else if (k >= 32 && k < 127)
	{
		if (mousemenu.search.len < sizeof(mousemenu.search.text) - 1)
		{
			mousemenu.search.text[mousemenu.search.len++] = k;
			mousemenu.search.text[mousemenu.search.len] = 0;
			M_Mouse_UpdateSearch();
			return;
		}
	}

	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		if (M_LivePreview_Alpha() > 0.f)
		{
			M_LivePreview_Reset();
			return;
		}
		M_Menu_Options_f();
		break;

	case K_MOUSE1:
		m_entersound = true;

		// Check if click is in search box area
		if (mousemenu.search.len > 0 && m_mousey >= 170)
			break;

		// Check if click is in valid menu area
		if (m_mousey >= 48 && m_mousey < 48 + (MOUSE_ITEMS * 8))
		{
			mouse_cursor = (m_mousey - 48) / 8;

			if (mouse_cursor == MOUSE_SPEED)
			{
				mouse_slider_grab = true;
			}
			else
			{
				M_Mouse_AdjustSliders(1);
			}
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		M_Mouse_AdjustSliders(1);
		break;

	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		M_Mouse_MoveCursor(-1);
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		M_Mouse_MoveCursor(1);
		break;

	case K_LEFTARROW:
	case K_MWHEELDOWN:
		M_Mouse_AdjustSliders(-1);
		break;

	case K_RIGHTARROW:
	case K_MWHEELUP:
		M_Mouse_AdjustSliders(1);
		break;
	}
}

void M_Mouse_Mousemove(int cx, int cy)
{
	if (mouse_slider_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			mouse_slider_grab = false;
			return;
		}

		float f;
		switch (mouse_cursor)
		{
		case MOUSE_SPEED:
			f = 1.f + M_MouseToSliderFraction(cx - 187) * 19.f;
			Cvar_SetValue("sensitivity", f);
			break;
		case MOUSE_INVERT:
		case MOUSE_ALWAYSMLOOK:
		case MOUSE_PITCHMODE:
		case MOUSE_COUNT:
			break;
		default:
			break;
		}
		return;
	}

	// Don't process mouse movement if it's in the search box area
	if (mousemenu.search.len > 0 && cy >= 170)
		return;

	// Calculate which menu item the mouse is over
	int item = (cy - 48) / 8;

	// Make sure the item is within valid range
	if (item >= 0 && item < MOUSE_ITEMS)
	{
		// Update the cursor position
		mouse_cursor = item;
	}
}

/*
==================
Controller Menu
==================
*/

extern cvar_t joy_device;
extern cvar_t joy_deadzone_look, joy_deadzone_move, joy_deadzone_trigger;
extern cvar_t joy_sensitivity_yaw, joy_sensitivity_pitch;
extern cvar_t joy_invert, joy_exponent, joy_exponent_move, joy_swapmovelook;
extern cvar_t joy_flick, joy_rumble;
extern cvar_t gyro_enable, gyro_mode, gyro_turning_axis;
extern cvar_t gyro_yawsensitivity, gyro_pitchsensitivity, gyro_noise_thresh;

#define CONTROLLER_SLIDER_X		186
#define CONTROLLER_CURSOR_X		168
#define CONTROLLER_VALUE_X		178
#define CONTROLLER_TITLE_Y		32
#define CONTROLLER_TOP_Y		48
#define CONTROLLER_MAX_VISIBLE	18
#define MIN_JOY_SENS			60.f
#define MAX_JOY_SENS			720.f
#define MIN_JOY_EXPONENT		1.f
#define MAX_JOY_EXPONENT		5.f
#define MIN_STICK_DEADZONE		0.f
#define MAX_STICK_DEADZONE		0.75f
#define MIN_TRIGGER_DEADZONE	0.f
#define MAX_TRIGGER_DEADZONE	0.75f
#define MIN_RUMBLE				0.f
#define MAX_RUMBLE				1.f
#define MIN_GYRO_SENS			0.1f
#define MAX_GYRO_SENS			20.f
#define MIN_GYRO_NOISE_THRESH	0.f
#define MAX_GYRO_NOISE_THRESH	5.f

static enum controller_e
{
	CONTROLLER_DEVICE,
	CONTROLLER_SPACE_DEVICE,
	CONTROLLER_SENSX,
	CONTROLLER_SENSY,
	CONTROLLER_INVERT,
	CONTROLLER_LOOK_STICK,
	CONTROLLER_SPACE_LOOK_STICK,
	CONTROLLER_EXPONENT_LOOK,
	CONTROLLER_EXPONENT_MOVE,
	CONTROLLER_DEADZONE_LOOK,
	CONTROLLER_DEADZONE_MOVE,
	CONTROLLER_TRIGGER_THRESH,
	CONTROLLER_SPACE_BEFORE_RUMBLE,
	CONTROLLER_RUMBLE,
	CONTROLLER_SPACE_AFTER_RUMBLE,
	CONTROLLER_GYRO_ENABLE,
	CONTROLLER_FLICK_STICK,
	CONTROLLER_GYRO_MODE,
	CONTROLLER_GYRO_AXIS,
	CONTROLLER_GYRO_SENSX,
	CONTROLLER_GYRO_SENSY,
	CONTROLLER_GYRO_NOISE,
	CONTROLLER_CALIBRATE,
	CONTROLLER_TEST,
	CONTROLLER_COUNT
} controller_cursor;

#define CONTROLLER_ITEMS (CONTROLLER_COUNT)

static qboolean controller_slider_grab;
static int controller_scroll;

static const char *M_Controller_GetItemText(int index)
{
	static char buffer[64];

	switch (index)
	{
	case CONTROLLER_SPACE_DEVICE:
	case CONTROLLER_SPACE_LOOK_STICK:
	case CONTROLLER_SPACE_BEFORE_RUMBLE:
	case CONTROLLER_SPACE_AFTER_RUMBLE:
		return "";
	case CONTROLLER_DEVICE:
		return "          Device";
	case CONTROLLER_SENSX:
		return "       Yaw Speed";
	case CONTROLLER_SENSY:
		return "     Pitch Speed";
	case CONTROLLER_INVERT:
		return "    Invert Pitch";
	case CONTROLLER_LOOK_STICK:
		return "      Look Stick";
	case CONTROLLER_EXPONENT_LOOK:
		return "      Look Accel";
	case CONTROLLER_EXPONENT_MOVE:
		return "      Move Accel";
	case CONTROLLER_DEADZONE_LOOK:
		return "   Look Deadzone";
	case CONTROLLER_DEADZONE_MOVE:
		return "   Move Deadzone";
	case CONTROLLER_TRIGGER_THRESH:
		return "  Trigger Thresh";
	case CONTROLLER_TEST:
		return " Controller Test";
	case CONTROLLER_RUMBLE:
		return "       Vibration";
	case CONTROLLER_GYRO_ENABLE:
		return "            Gyro";
	case CONTROLLER_FLICK_STICK:
		return "     Flick Stick";
	case CONTROLLER_GYRO_MODE:
		return "     Gyro Button";
	case CONTROLLER_GYRO_AXIS:
		return "       Gyro Axis";
	case CONTROLLER_GYRO_SENSX:
		return "  Gyro Yaw Speed";
	case CONTROLLER_GYRO_SENSY:
		return "Gyro Pitch Speed";
	case CONTROLLER_GYRO_NOISE:
		return "      Gyro Noise";
	case CONTROLLER_CALIBRATE:
		return "       Calibrate";
	default:
		q_snprintf(buffer, sizeof(buffer), "Unknown Item %d", index);
		return buffer;
	}
}

static qboolean M_Controller_IsSlider(int option)
{
	switch (option)
	{
	case CONTROLLER_SENSX:
	case CONTROLLER_SENSY:
	case CONTROLLER_EXPONENT_LOOK:
	case CONTROLLER_EXPONENT_MOVE:
	case CONTROLLER_DEADZONE_LOOK:
	case CONTROLLER_DEADZONE_MOVE:
	case CONTROLLER_TRIGGER_THRESH:
	case CONTROLLER_RUMBLE:
	case CONTROLLER_GYRO_SENSX:
	case CONTROLLER_GYRO_SENSY:
	case CONTROLLER_GYRO_NOISE:
		return true;
	default:
		return false;
	}
}

static qboolean M_Controller_IsOptionVisible(int option)
{
	switch (option)
	{
	case CONTROLLER_CALIBRATE:
		return IN_HasGyro();

	default:
		return true;
	}
}

#define CONTROLLER_DIM_ALPHA	0.375f

static int M_Controller_GetVisibleItemCount(void);
static int M_Controller_CursorToOption(int cursor);

static qboolean M_Controller_IsOptionEnabled(int option)
{
	if (option == CONTROLLER_SPACE_DEVICE || option == CONTROLLER_SPACE_LOOK_STICK
		|| option == CONTROLLER_SPACE_BEFORE_RUMBLE || option == CONTROLLER_SPACE_AFTER_RUMBLE)
		return false;
	if (option == CONTROLLER_DEVICE)
		return true;
	if (!IN_HasGamepad())
		return false;

	switch (option)
	{
	case CONTROLLER_RUMBLE:
		return IN_HasRumble();

	case CONTROLLER_GYRO_ENABLE:
	case CONTROLLER_FLICK_STICK:
	case CONTROLLER_GYRO_MODE:
	case CONTROLLER_GYRO_AXIS:
	case CONTROLLER_GYRO_SENSX:
	case CONTROLLER_GYRO_SENSY:
	case CONTROLLER_GYRO_NOISE:
		return IN_HasGyro();

	default:
		return true;
	}
}

static void M_Controller_PrintMaybeDim(int x, int y, const char *str, qboolean enabled)
{
	if (enabled)
		M_Print(x, y, str);
	else
		M_PrintRGBA(x, y, str, CL_PLColours_Parse("0xffffff"), CONTROLLER_DIM_ALPHA, true);
}

static void M_Controller_DrawSliderMaybeDim(int x, int y, float range, float value, const char *format, qboolean enabled)
{
	int i;
	char buffer[6];
	plcolour_t c;

	if (enabled)
	{
		M_DrawSlider(x, y, range, value, format);
		return;
	}

	c = CL_PLColours_Parse("0xffffff");
	if (range < 0) range = 0;
	if (range > 1) range = 1;
	M_DrawCharacterRGBA(x - 8, y, 128, c, CONTROLLER_DIM_ALPHA);
	for (i = 0; i < SLIDER_RANGE; i++)
		M_DrawCharacterRGBA(x + i * 8, y, 129, c, CONTROLLER_DIM_ALPHA);
	M_DrawCharacterRGBA(x + i * 8, y, 130, c, CONTROLLER_DIM_ALPHA);
	M_DrawCharacterRGBA(x + (SLIDER_RANGE - 1) * 8 * range, y, 131, c, CONTROLLER_DIM_ALPHA);

	q_snprintf(buffer, sizeof(buffer), format, value);
	M_PrintRGBA(x + (SLIDER_RANGE + 2) * 8, y, buffer, c, CONTROLLER_DIM_ALPHA, true);
}

static void M_Controller_DrawCheckboxMaybeDim(int x, int y, int on, qboolean enabled)
{
	if (enabled)
		M_DrawCheckbox(x, y, on);
	else
		M_PrintRGBA(x, y, on ? "on" : "off", CL_PLColours_Parse("0xffffff"), CONTROLLER_DIM_ALPHA, true);
}

static int M_Controller_StepCursorToEnabled(int start, int dir)
{
	int visible_count = M_Controller_GetVisibleItemCount();
	int cursor = start;
	int steps;

	if (visible_count <= 0)
		return 0;

	for (steps = 0; steps < visible_count; steps++)
	{
		int option = M_Controller_CursorToOption(cursor);
		if (M_Controller_IsOptionEnabled(option))
			return cursor;
		cursor += dir;
		if (cursor < 0)
			cursor = visible_count - 1;
		else if (cursor >= visible_count)
			cursor = 0;
	}

	return start;
}

static int M_Controller_GetVisibleItemCount(void)
{
	int count = 0;
	int option;

	for (option = 0; option < CONTROLLER_ITEMS; option++)
	{
		if (M_Controller_IsOptionVisible(option))
			count++;
	}

	return count;
}

static int M_Controller_CursorToOption(int cursor)
{
	int option;
	int visible_index = 0;

	for (option = 0; option < CONTROLLER_ITEMS; option++)
	{
		if (!M_Controller_IsOptionVisible(option))
			continue;
		if (visible_index == cursor)
			return option;
		visible_index++;
	}

	return CONTROLLER_DEVICE;
}

static void M_Controller_ClampCursor(void)
{
	int visible_count = M_Controller_GetVisibleItemCount();
	int max_visible = q_min(visible_count, CONTROLLER_MAX_VISIBLE);
	int option;

	if (visible_count <= 0)
	{
		controller_cursor = 0;
		controller_scroll = 0;
		return;
	}

	if (controller_cursor < 0)
		controller_cursor = 0;
	else if (controller_cursor >= visible_count)
		controller_cursor = visible_count - 1;

	option = M_Controller_CursorToOption(controller_cursor);
	if (!M_Controller_IsOptionEnabled(option))
		controller_cursor = M_Controller_StepCursorToEnabled(controller_cursor, 1);

	if (controller_scroll < 0)
		controller_scroll = 0;
	else if (controller_scroll > visible_count - max_visible)
		controller_scroll = visible_count - max_visible;
}

static void M_Controller_ScrollToCursor(void)
{
	int visible_count = M_Controller_GetVisibleItemCount();
	int max_visible = q_min(visible_count, CONTROLLER_MAX_VISIBLE);

	if (visible_count <= 0)
		return;

	if (controller_cursor < controller_scroll)
		controller_scroll = controller_cursor;
	else if (controller_cursor >= controller_scroll + max_visible)
		controller_scroll = controller_cursor - max_visible + 1;
}

static void M_Controller_Ellipsize(char *dst, size_t dstsize, const char *src, int maxchars)
{
	if (!dstsize)
		return;

	if (!src)
	{
		dst[0] = '\0';
		return;
	}

	if (maxchars < 4)
	{
		q_strlcpy(dst, src, dstsize);
		if ((size_t)maxchars < dstsize)
			dst[maxchars] = '\0';
		return;
	}

	if ((size_t)(maxchars + 1) > dstsize)
		maxchars = (int)dstsize - 1;

	if ((int)strlen(src) <= maxchars)
	{
		q_strlcpy(dst, src, dstsize);
		return;
	}

	memcpy(dst, src, maxchars - 3);
	dst[maxchars - 3] = '.';
	dst[maxchars - 2] = '.';
	dst[maxchars - 1] = '.';
	dst[maxchars] = '\0';
}

static const char *M_Controller_GetDeviceLabel(void)
{
	static char label[20];
	const char *name = NULL;
	int device = (int)joy_device.value;

#if defined(USE_SDL2)
	if (device < 0)
		return "Disabled";

	if (device < SDL_NumJoysticks())
	{
		if (!SDL_IsGameController(device))
			return "Unsupported";

		name = SDL_GameControllerNameForIndex(device);
	}
	else
	{
		name = IN_GetGamepadName();
		if (!name)
			return "Not connected";
	}
#else
	name = IN_GetGamepadName();
	if (!name)
		return "Unavailable";
#endif

	if (!name || !*name)
		name = "[Unknown gamepad]";

	M_Controller_Ellipsize(label, sizeof(label), name, 16);
	return label;
}

static const char *M_Controller_GetGyroModeLabel(void)
{
	switch ((int)gyro_mode.value)
	{
	case GYRO_BUTTON_ENABLES:
		return "Hold To Use";
	case GYRO_BUTTON_DISABLES:
		return "Hold To Pause";
	case GYRO_BUTTON_INVERTS_DIR:
		return "Hold To Invert";
	default:
		return "Always On";
	}
}

static const char *M_Controller_GetGyroAxisLabel(void)
{
	return gyro_turning_axis.value ? "Roll" : "Yaw";
}

static void M_Controller_CycleDevice(int dir)
{
#if defined(USE_SDL2)
	int i, count, current, effective_current, first, last, next, prev, target;

	count = SDL_NumJoysticks();
	current = (int)joy_device.value;
	effective_current = (current >= 0 && current < count && SDL_IsGameController(current)) ? current : -1;
	first = last = next = prev = -1;
	target = current;

	for (i = 0; i < count; i++)
	{
		if (!SDL_IsGameController(i))
			continue;

		if (first == -1)
			first = i;
		last = i;

		if (i < effective_current)
			prev = i;
		else if (i > effective_current && next == -1)
			next = i;
	}

	if (first == -1)
		return;

	if (effective_current < 0)
		target = dir > 0 ? first : last;
	else if (dir > 0)
		target = next != -1 ? next : -1;
	else
		target = prev != -1 ? prev : -1;

	if (target != current)
		Cvar_SetValueQuick(&joy_device, target);
#else
	(void)dir;
#endif
}

static void M_Controller_AdjustSliders(int dir)
{
	int option;

	M_Controller_ClampCursor();
	option = M_Controller_CursorToOption(controller_cursor);

	if (!M_Controller_IsOptionEnabled(option))
		return;

	S_LocalSound("misc/menu3.wav");

	switch (option)
	{
	case CONTROLLER_DEVICE:
		M_Controller_CycleDevice(dir);
		break;

	case CONTROLLER_SENSX:
		Cvar_SetValueQuick(&joy_sensitivity_yaw, CLAMP(MIN_JOY_SENS, joy_sensitivity_yaw.value + dir * 10.f, MAX_JOY_SENS));
		break;

	case CONTROLLER_SENSY:
		Cvar_SetValueQuick(&joy_sensitivity_pitch, CLAMP(MIN_JOY_SENS, joy_sensitivity_pitch.value + dir * 10.f, MAX_JOY_SENS));
		break;

	case CONTROLLER_INVERT:
		Cvar_SetValueQuick(&joy_invert, !joy_invert.value);
		break;

	case CONTROLLER_LOOK_STICK:
		Cvar_SetValueQuick(&joy_swapmovelook, !joy_swapmovelook.value);
		break;

	case CONTROLLER_EXPONENT_LOOK:
		Cvar_SetValueQuick(&joy_exponent, CLAMP(MIN_JOY_EXPONENT, joy_exponent.value + dir * 0.5f, MAX_JOY_EXPONENT));
		break;

	case CONTROLLER_EXPONENT_MOVE:
		Cvar_SetValueQuick(&joy_exponent_move, CLAMP(MIN_JOY_EXPONENT, joy_exponent_move.value + dir * 0.5f, MAX_JOY_EXPONENT));
		break;

	case CONTROLLER_DEADZONE_LOOK:
		Cvar_SetValueQuick(&joy_deadzone_look, CLAMP(MIN_STICK_DEADZONE, joy_deadzone_look.value + dir * 0.05f, MAX_STICK_DEADZONE));
		break;

	case CONTROLLER_DEADZONE_MOVE:
		Cvar_SetValueQuick(&joy_deadzone_move, CLAMP(MIN_STICK_DEADZONE, joy_deadzone_move.value + dir * 0.05f, MAX_STICK_DEADZONE));
		break;

	case CONTROLLER_TRIGGER_THRESH:
		Cvar_SetValueQuick(&joy_deadzone_trigger, CLAMP(MIN_TRIGGER_DEADZONE, joy_deadzone_trigger.value + dir * 0.05f, MAX_TRIGGER_DEADZONE));
		break;

	case CONTROLLER_TEST:
		M_Menu_Controller_Test_f();
		break;

	case CONTROLLER_RUMBLE:
		Cvar_SetValueQuick(&joy_rumble, CLAMP(MIN_RUMBLE, joy_rumble.value + dir * 0.05f, MAX_RUMBLE));
		break;

	case CONTROLLER_GYRO_ENABLE:
		Cvar_SetValueQuick(&gyro_enable, !gyro_enable.value);
		break;

	case CONTROLLER_FLICK_STICK:
		Cvar_SetValueQuick(&joy_flick, !joy_flick.value);
		break;

	case CONTROLLER_GYRO_MODE:
		Cvar_SetValueQuick(&gyro_mode, ((int)gyro_mode.value + GYRO_MODE_COUNT + dir) % GYRO_MODE_COUNT);
		break;

	case CONTROLLER_GYRO_AXIS:
		Cvar_SetValueQuick(&gyro_turning_axis, !gyro_turning_axis.value);
		break;

	case CONTROLLER_GYRO_SENSX:
		Cvar_SetValueQuick(&gyro_yawsensitivity, CLAMP(MIN_GYRO_SENS, gyro_yawsensitivity.value + dir * 0.1f, MAX_GYRO_SENS));
		break;

	case CONTROLLER_GYRO_SENSY:
		Cvar_SetValueQuick(&gyro_pitchsensitivity, CLAMP(MIN_GYRO_SENS, gyro_pitchsensitivity.value + dir * 0.1f, MAX_GYRO_SENS));
		break;

	case CONTROLLER_GYRO_NOISE:
		Cvar_SetValueQuick(&gyro_noise_thresh, CLAMP(MIN_GYRO_NOISE_THRESH, gyro_noise_thresh.value + dir * 0.1f, MAX_GYRO_NOISE_THRESH));
		break;

	case CONTROLLER_CALIBRATE:
		if (IN_HasGyro())
			M_Menu_Calibration_f();
		break;

	default:
		break;
	}
}

static qboolean M_Controller_SetSliderValue(int option, float frac)
{
	frac = CLAMP(0.f, frac, 1.f);

	switch (option)
	{
	case CONTROLLER_SENSX:
		Cvar_SetValueQuick(&joy_sensitivity_yaw, MIN_JOY_SENS + frac * (MAX_JOY_SENS - MIN_JOY_SENS));
		return true;

	case CONTROLLER_SENSY:
		Cvar_SetValueQuick(&joy_sensitivity_pitch, MIN_JOY_SENS + frac * (MAX_JOY_SENS - MIN_JOY_SENS));
		return true;

	case CONTROLLER_EXPONENT_LOOK:
		Cvar_SetValueQuick(&joy_exponent, MIN_JOY_EXPONENT + frac * (MAX_JOY_EXPONENT - MIN_JOY_EXPONENT));
		return true;

	case CONTROLLER_EXPONENT_MOVE:
		Cvar_SetValueQuick(&joy_exponent_move, MIN_JOY_EXPONENT + frac * (MAX_JOY_EXPONENT - MIN_JOY_EXPONENT));
		return true;

	case CONTROLLER_DEADZONE_LOOK:
		Cvar_SetValueQuick(&joy_deadzone_look, MIN_STICK_DEADZONE + frac * (MAX_STICK_DEADZONE - MIN_STICK_DEADZONE));
		return true;

	case CONTROLLER_DEADZONE_MOVE:
		Cvar_SetValueQuick(&joy_deadzone_move, MIN_STICK_DEADZONE + frac * (MAX_STICK_DEADZONE - MIN_STICK_DEADZONE));
		return true;

	case CONTROLLER_TRIGGER_THRESH:
		Cvar_SetValueQuick(&joy_deadzone_trigger, MIN_TRIGGER_DEADZONE + frac * (MAX_TRIGGER_DEADZONE - MIN_TRIGGER_DEADZONE));
		return true;

	case CONTROLLER_RUMBLE:
		Cvar_SetValueQuick(&joy_rumble, MIN_RUMBLE + frac * (MAX_RUMBLE - MIN_RUMBLE));
		return true;

	case CONTROLLER_GYRO_SENSX:
		Cvar_SetValueQuick(&gyro_yawsensitivity, MIN_GYRO_SENS + frac * (MAX_GYRO_SENS - MIN_GYRO_SENS));
		return true;

	case CONTROLLER_GYRO_SENSY:
		Cvar_SetValueQuick(&gyro_pitchsensitivity, MIN_GYRO_SENS + frac * (MAX_GYRO_SENS - MIN_GYRO_SENS));
		return true;

	case CONTROLLER_GYRO_NOISE:
		Cvar_SetValueQuick(&gyro_noise_thresh, MIN_GYRO_NOISE_THRESH + frac * (MAX_GYRO_NOISE_THRESH - MIN_GYRO_NOISE_THRESH));
		return true;

	default:
		return false;
	}
}

static enum calibration_e
{
	CALIBRATION_INTRO_TEXT,
	CALIBRATION_IN_PROGRESS,
	CALIBRATION_FINISHED,
} calibration_state;

static double calibration_finished_delay;

static void M_Calibration_Update(void)
{
	switch (calibration_state)
	{
	case CALIBRATION_IN_PROGRESS:
		if (!IN_IsCalibratingGyro())
			calibration_state = CALIBRATION_FINISHED;
		break;

	case CALIBRATION_FINISHED:
		calibration_finished_delay -= host_frametime;
		if (calibration_finished_delay < 0.0)
			M_Menu_Controller_f();
		break;

	default:
		break;
	}
}

void M_Menu_Calibration_f(void)
{
	key_dest = key_menu;
	m_state = m_calibration;
	m_entersound = true;
	calibration_state = CALIBRATION_INTRO_TEXT;
	calibration_finished_delay = 1.0;

	IN_UpdateGrabs();
}

void M_Calibration_Draw(void)
{
	char anim[16];
	int i, progress;
	int y = 72;
	qpic_t *p;

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);
	M_PrintWhite((320 - 8 * strlen("Gyro Calibration")) / 2, 32, "Gyro Calibration");

	switch (calibration_state)
	{
	case CALIBRATION_INTRO_TEXT:
		M_PrintWhite((320 - 8 * strlen("Place the controller flat")) / 2, y - 8, "Place the controller flat");
		M_PrintWhite((320 - 8 * strlen("on a stable surface")) / 2, y, "on a stable surface");
		M_DrawTextBox(160 - 5 * 8, y + 24, 8, 1);
		M_DrawArrowCursor(160 - 6 * 8, y + 32);
		M_PrintWhite((320 - 8 * strlen("Continue")) / 2, y + 32, "Continue");
		break;

	case CALIBRATION_IN_PROGRESS:
		progress = (int)(IN_GetGyroCalibrationProgress() * (Q_COUNTOF(anim) - 1) + 0.5f);
		for (i = 0; i < (int)Q_COUNTOF(anim) - 1; i++)
			anim[i] = i < progress ? (char)('.' | 128) : '.';
		anim[i] = '\0';
		M_PrintWhite((320 - 8 * strlen("Calibrating, please wait...")) / 2, y, "Calibrating, please wait...");
		M_PrintWhite((320 - 8 * strlen(anim)) / 2, y + 16, anim);
		break;

	case CALIBRATION_FINISHED:
		M_PrintWhite((320 - 8 * strlen("Calibration complete!")) / 2, y, "Calibration complete!");
		break;
	}
}

void M_Calibration_Key(int key)
{
	if (calibration_state != CALIBRATION_INTRO_TEXT)
		return;

	switch (key)
	{
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1:
		calibration_state = CALIBRATION_IN_PROGRESS;
		S_LocalSound("misc/menu2.wav");
		IN_StartGyroCalibration();
		break;

	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		M_Menu_Controller_f();
		break;
	}
}

void M_Menu_Controller_f(void)
{
	key_dest = key_menu;
	m_state = m_controller;
	m_entersound = true;
	controller_cursor = 0;
	controller_scroll = 0;
	controller_slider_grab = false;
	M_Controller_ClampCursor();

	IN_UpdateGrabs();
}

void M_Controller_Draw(void)
{
	qpic_t *p;
	float r;
	int i;
	int visible_index;
	int max_visible;
	int visible_count;
	const char *title = "Controller Options";

	M_Controller_ClampCursor();
	visible_count = M_Controller_GetVisibleItemCount();
	max_visible = q_min(visible_count, CONTROLLER_MAX_VISIBLE);

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);
	M_PrintWhite((320 - 8 * strlen(title)) / 2, CONTROLLER_TITLE_Y, title);

	visible_index = 0;
	for (i = 0; i < CONTROLLER_ITEMS; i++)
	{
		int y;
		int display_index;
		qboolean enabled;

		if (!M_Controller_IsOptionVisible(i))
			continue;

		display_index = visible_index - controller_scroll;
		visible_index++;
		if (display_index < 0 || display_index >= CONTROLLER_MAX_VISIBLE)
			continue;

		y = CONTROLLER_TOP_Y + 8 * display_index;
		enabled = M_Controller_IsOptionEnabled(i);

		M_Controller_PrintMaybeDim(16, y, M_Controller_GetItemText(i), enabled);

		switch (i)
		{
		case CONTROLLER_DEVICE:
			M_Controller_PrintMaybeDim(CONTROLLER_VALUE_X, y, M_Controller_GetDeviceLabel(), enabled);
			break;

		case CONTROLLER_SENSX:
			r = (joy_sensitivity_yaw.value - MIN_JOY_SENS) / (MAX_JOY_SENS - MIN_JOY_SENS);
			M_Controller_DrawSliderMaybeDim(CONTROLLER_SLIDER_X, y, r, joy_sensitivity_yaw.value, "%.0f", enabled);
			break;

		case CONTROLLER_SENSY:
			r = (joy_sensitivity_pitch.value - MIN_JOY_SENS) / (MAX_JOY_SENS - MIN_JOY_SENS);
			M_Controller_DrawSliderMaybeDim(CONTROLLER_SLIDER_X, y, r, joy_sensitivity_pitch.value, "%.0f", enabled);
			break;

		case CONTROLLER_INVERT:
			M_Controller_DrawCheckboxMaybeDim(CONTROLLER_VALUE_X, y, joy_invert.value, enabled);
			break;

		case CONTROLLER_LOOK_STICK:
			M_Controller_PrintMaybeDim(CONTROLLER_VALUE_X, y, joy_swapmovelook.value ? "Left" : "Right", enabled);
			break;

		case CONTROLLER_EXPONENT_LOOK:
			r = (joy_exponent.value - MIN_JOY_EXPONENT) / (MAX_JOY_EXPONENT - MIN_JOY_EXPONENT);
			M_Controller_DrawSliderMaybeDim(CONTROLLER_SLIDER_X, y, r, joy_exponent.value, "%.1f", enabled);
			break;

		case CONTROLLER_EXPONENT_MOVE:
			r = (joy_exponent_move.value - MIN_JOY_EXPONENT) / (MAX_JOY_EXPONENT - MIN_JOY_EXPONENT);
			M_Controller_DrawSliderMaybeDim(CONTROLLER_SLIDER_X, y, r, joy_exponent_move.value, "%.1f", enabled);
			break;

		case CONTROLLER_DEADZONE_LOOK:
			r = (joy_deadzone_look.value - MIN_STICK_DEADZONE) / (MAX_STICK_DEADZONE - MIN_STICK_DEADZONE);
			M_Controller_DrawSliderMaybeDim(CONTROLLER_SLIDER_X, y, r, joy_deadzone_look.value * 100.f, "%.0f%%", enabled);
			break;

		case CONTROLLER_DEADZONE_MOVE:
			r = (joy_deadzone_move.value - MIN_STICK_DEADZONE) / (MAX_STICK_DEADZONE - MIN_STICK_DEADZONE);
			M_Controller_DrawSliderMaybeDim(CONTROLLER_SLIDER_X, y, r, joy_deadzone_move.value * 100.f, "%.0f%%", enabled);
			break;

		case CONTROLLER_TRIGGER_THRESH:
			r = (joy_deadzone_trigger.value - MIN_TRIGGER_DEADZONE) / (MAX_TRIGGER_DEADZONE - MIN_TRIGGER_DEADZONE);
			M_Controller_DrawSliderMaybeDim(CONTROLLER_SLIDER_X, y, r, joy_deadzone_trigger.value * 100.f, "%.0f%%", enabled);
			break;

		case CONTROLLER_TEST:
			M_Controller_PrintMaybeDim(CONTROLLER_VALUE_X, y, "...", enabled);
			break;

		case CONTROLLER_RUMBLE:
			if (!IN_HasRumble())
			{
				M_Controller_PrintMaybeDim(CONTROLLER_VALUE_X, y, "Unavailable", false);
			}
			else
			{
				r = (joy_rumble.value - MIN_RUMBLE) / (MAX_RUMBLE - MIN_RUMBLE);
				M_Controller_DrawSliderMaybeDim(CONTROLLER_SLIDER_X, y, r, joy_rumble.value * 100.f, "%.0f%%", enabled);
			}
			break;

		case CONTROLLER_GYRO_ENABLE:
			if (!IN_HasGyro())
				M_Controller_PrintMaybeDim(CONTROLLER_VALUE_X, y, "Unavailable", false);
			else
				M_Controller_DrawCheckboxMaybeDim(CONTROLLER_VALUE_X, y, gyro_enable.value, enabled);
			break;

		case CONTROLLER_FLICK_STICK:
			M_Controller_DrawCheckboxMaybeDim(CONTROLLER_VALUE_X, y, joy_flick.value, enabled);
			break;

		case CONTROLLER_GYRO_MODE:
			M_Controller_PrintMaybeDim(CONTROLLER_VALUE_X, y, M_Controller_GetGyroModeLabel(), enabled);
			break;

		case CONTROLLER_GYRO_AXIS:
			M_Controller_PrintMaybeDim(CONTROLLER_VALUE_X, y, M_Controller_GetGyroAxisLabel(), enabled);
			break;

		case CONTROLLER_GYRO_SENSX:
			r = (gyro_yawsensitivity.value - MIN_GYRO_SENS) / (MAX_GYRO_SENS - MIN_GYRO_SENS);
			M_Controller_DrawSliderMaybeDim(CONTROLLER_SLIDER_X, y, r, gyro_yawsensitivity.value, "%.1f", enabled);
			break;

		case CONTROLLER_GYRO_SENSY:
			r = (gyro_pitchsensitivity.value - MIN_GYRO_SENS) / (MAX_GYRO_SENS - MIN_GYRO_SENS);
			M_Controller_DrawSliderMaybeDim(CONTROLLER_SLIDER_X, y, r, gyro_pitchsensitivity.value, "%.1f", enabled);
			break;

		case CONTROLLER_GYRO_NOISE:
			r = (gyro_noise_thresh.value - MIN_GYRO_NOISE_THRESH) / (MAX_GYRO_NOISE_THRESH - MIN_GYRO_NOISE_THRESH);
			M_Controller_DrawSliderMaybeDim(CONTROLLER_SLIDER_X, y, r, gyro_noise_thresh.value, "%.1f", enabled);
			break;

		case CONTROLLER_CALIBRATE:
			M_Controller_PrintMaybeDim(CONTROLLER_VALUE_X, y, IN_HasGyro() ? "Start" : "Unavailable", enabled);
			break;

		default:
			break;
		}
	}

	if (visible_count > max_visible)
	{
		if (controller_scroll > 0)
			M_DrawEllipsisBar(16, CONTROLLER_TOP_Y - 8, 36);
		if (controller_scroll + max_visible < visible_count)
			M_DrawEllipsisBar(16, CONTROLLER_TOP_Y + max_visible * 8, 36);
	}

	if (controller_cursor >= controller_scroll && controller_cursor < controller_scroll + max_visible)
		M_DrawCharacter(CONTROLLER_CURSOR_X, CONTROLLER_TOP_Y + (controller_cursor - controller_scroll) * 8, 12 + ((int)(realtime * 4) & 1));
}

void M_Controller_Key(int k)
{
	int option;
	int visible_count;

	M_Controller_ClampCursor();

	if (!keydown[K_MOUSE1])
		controller_slider_grab = false;

	if (controller_slider_grab)
	{
		switch (k)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			controller_slider_grab = false;
			break;
		}
		return;
	}

	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		M_Menu_Options_f();
		break;

	case K_MOUSE1:
		m_entersound = true;
		visible_count = M_Controller_GetVisibleItemCount();
		visible_count = q_min(visible_count, CONTROLLER_MAX_VISIBLE);
		if (m_mousey >= CONTROLLER_TOP_Y && m_mousey < CONTROLLER_TOP_Y + visible_count * 8)
		{
			int clicked = controller_scroll + (m_mousey - CONTROLLER_TOP_Y) / 8;
			int clicked_option;

			if (clicked < 0)
				clicked = 0;
			else if (clicked >= M_Controller_GetVisibleItemCount())
				clicked = M_Controller_GetVisibleItemCount() - 1;

			clicked_option = M_Controller_CursorToOption(clicked);
			if (!M_Controller_IsOptionEnabled(clicked_option))
				break;

			controller_cursor = clicked;
			M_Controller_ClampCursor();
			option = M_Controller_CursorToOption(controller_cursor);

			if (M_Controller_IsSlider(option) &&
				m_mousex >= CONTROLLER_SLIDER_X &&
				m_mousex <= CONTROLLER_SLIDER_X + SLIDER_RANGE * 8)
			{
				controller_slider_grab = true;
				M_Controller_SetSliderValue(option, M_MouseToSliderFraction(m_mousex - CONTROLLER_SLIDER_X));
				S_LocalSound("misc/menu3.wav");
			}
			else
			{
				M_Controller_AdjustSliders(1);
			}
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		M_Controller_AdjustSliders(1);
		break;

	case K_UPARROW:
		visible_count = M_Controller_GetVisibleItemCount();
		S_LocalSound("misc/menu1.wav");
		controller_cursor--;
		if (controller_cursor < 0)
			controller_cursor = visible_count - 1;
		controller_cursor = M_Controller_StepCursorToEnabled(controller_cursor, -1);
		M_Controller_ClampCursor();
		M_Controller_ScrollToCursor();
		break;

	case K_DOWNARROW:
		visible_count = M_Controller_GetVisibleItemCount();
		S_LocalSound("misc/menu1.wav");
		controller_cursor++;
		if (controller_cursor >= visible_count)
			controller_cursor = 0;
		controller_cursor = M_Controller_StepCursorToEnabled(controller_cursor, 1);
		M_Controller_ClampCursor();
		M_Controller_ScrollToCursor();
		break;

	case K_LEFTARROW:
		M_Controller_AdjustSliders(-1);
		break;

	case K_MWHEELUP:
		option = M_Controller_CursorToOption(controller_cursor);
		if (option == CONTROLLER_TEST)
			break;
		visible_count = M_Controller_GetVisibleItemCount();
		if (visible_count > CONTROLLER_MAX_VISIBLE)
		{
			if (controller_scroll > 0)
			{
				S_LocalSound("misc/menu1.wav");
				controller_scroll--;
			}
		}
		else
		{
			M_Controller_AdjustSliders(1);
		}
		break;

	case K_MWHEELDOWN:
		option = M_Controller_CursorToOption(controller_cursor);
		if (option == CONTROLLER_TEST)
			break;
		visible_count = M_Controller_GetVisibleItemCount();
		if (visible_count > CONTROLLER_MAX_VISIBLE)
		{
			int max_visible = q_min(visible_count, CONTROLLER_MAX_VISIBLE);
			if (controller_scroll + max_visible < visible_count)
			{
				S_LocalSound("misc/menu1.wav");
				controller_scroll++;
			}
		}
		else
		{
			M_Controller_AdjustSliders(-1);
		}
		break;

	case K_RIGHTARROW:
		M_Controller_AdjustSliders(1);
		break;

	case 't':
	case 'T':
		if (IN_HasGamepad())
			M_Menu_Controller_Test_f();
		break;
	}
}

void M_Controller_Mousemove(int cx, int cy)
{
	int visible_count;

	M_Controller_ClampCursor();

	if (controller_slider_grab)
	{
		int option = M_Controller_CursorToOption(controller_cursor);

		if (!keydown[K_MOUSE1])
		{
			controller_slider_grab = false;
			return;
		}

		M_Controller_SetSliderValue(option, M_MouseToSliderFraction(cx - CONTROLLER_SLIDER_X));
		return;
	}

	visible_count = M_Controller_GetVisibleItemCount();
	visible_count = q_min(visible_count, CONTROLLER_MAX_VISIBLE);
	if (cy >= CONTROLLER_TOP_Y && cy < CONTROLLER_TOP_Y + visible_count * 8)
	{
		int hovered = controller_scroll + (cy - CONTROLLER_TOP_Y) / 8;
		int total = M_Controller_GetVisibleItemCount();
		int hovered_option;

		if (hovered < 0)
			hovered = 0;
		else if (hovered >= total)
			hovered = total - 1;

		hovered_option = M_Controller_CursorToOption(hovered);
		if (M_Controller_IsOptionEnabled(hovered_option))
		{
			controller_cursor = hovered;
			M_Controller_ClampCursor();
		}
	}
}

/*
==================
Controller Test Menu
==================
*/

#define M_CONTROLLER_TEST_PLACEHOLDER_ALPHA	0.55f
#define M_CONTROLLER_TEST_DIM_ALPHA		0.45f

static enum m_state_e controller_test_prev = m_controller;

static int M_Controller_Test_AxisPixel(float value, float scale)
{
	value = CLAMP(-1.f, value, 1.f);
	return (int)(value * scale + (value < 0.f ? -0.5f : 0.5f));
}

static void M_Controller_Test_DimPrint(int x, int y, const char *s, qboolean dim)
{
	if (dim)
		M_PrintRGBA(x, y, s, CL_PLColours_Parse("0xffffff"), M_CONTROLLER_TEST_DIM_ALPHA, true);
	else
		M_Print(x, y, s);
}

static void M_Controller_Test_DrawStickBox(int x, int y, const char *label, float ax, float ay, float deadzone, qboolean dim)
{
	int marker_x, marker_y;
	qboolean active;
	plcolour_t white = CL_PLColours_Parse("0xffffff");

	M_Controller_Test_DimPrint(x + 16, y - 8, label, dim);

	if (dim)
		M_DrawTextBox_WithAlpha(x, y, 5, 4, M_CONTROLLER_TEST_DIM_ALPHA);
	else
		M_DrawTextBox(x, y, 5, 4);

	marker_x = x + 32 + M_Controller_Test_AxisPixel(ax, 16.f);
	marker_y = y + 24 + M_Controller_Test_AxisPixel(ay, 16.f);
	active = !dim && (ax * ax + ay * ay) > (deadzone * deadzone);

	if (active)
		M_PrintWhite(marker_x, marker_y, "+");
	else
		M_PrintRGBA(marker_x, marker_y, "+", white, dim ? 0.25f : 0.35f, true);
}

static const char *M_Controller_Test_GamepadTypeLabel(void)
{
	switch (IN_GetGamepadType())
	{
	case GAMEPAD_XBOX:
		return "Xbox";
	case GAMEPAD_PLAYSTATION:
		return "PlayStation";
	case GAMEPAD_NINTENDO:
		return "Nintendo";
	default:
		return "Unknown";
	}
}

static void M_Controller_Test_AppendHeldKey(char *dst, size_t dstsize, int keynum, const char *label)
{
	const char *name;

	if (!keydown[keynum])
		return;

	name = label ? label : Key_KeynumToFriendlyString(keynum);
	if (!name || !*name)
		return;

	if (strlen(dst) + strlen(name) + 2 >= dstsize)
	{
		if (!strstr(dst, "..."))
			q_strlcat(dst, " ...", dstsize);
		return;
	}

	q_strlcat(dst, " ", dstsize);
	q_strlcat(dst, name, dstsize);
}

static void M_Controller_Test_DrawHeldButtons(int y, qboolean dim)
{
	char face[38];
	char other[38];

	q_strlcpy(face, "Face:", sizeof(face));
	M_Controller_Test_AppendHeldKey(face, sizeof(face), K_ABUTTON, NULL);
	M_Controller_Test_AppendHeldKey(face, sizeof(face), K_BBUTTON, NULL);
	M_Controller_Test_AppendHeldKey(face, sizeof(face), K_XBUTTON, NULL);
	M_Controller_Test_AppendHeldKey(face, sizeof(face), K_YBUTTON, NULL);
	M_Controller_Test_AppendHeldKey(face, sizeof(face), K_ABUTTON_ALT, NULL);
	M_Controller_Test_AppendHeldKey(face, sizeof(face), K_BBUTTON_ALT, NULL);
	M_Controller_Test_AppendHeldKey(face, sizeof(face), K_XBUTTON_ALT, NULL);
	M_Controller_Test_AppendHeldKey(face, sizeof(face), K_YBUTTON_ALT, NULL);
	if (!strcmp(face, "Face:"))
		q_strlcat(face, " -", sizeof(face));

	q_strlcpy(other, "Other:", sizeof(other));
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_DPAD_UP, "Up");
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_DPAD_DOWN, "Down");
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_DPAD_LEFT, "Left");
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_DPAD_RIGHT, "Right");
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_LSHOULDER, NULL);
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_RSHOULDER, NULL);
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_LTRIGGER, NULL);
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_RTRIGGER, NULL);
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_LTHUMB, NULL);
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_RTHUMB, NULL);
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_ESCAPE, Key_KeynumToFriendlyString(K_START));
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_TAB, Key_KeynumToFriendlyString(K_BACK));
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_MISC1, NULL);
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_PADDLE1, NULL);
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_PADDLE2, NULL);
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_PADDLE3, NULL);
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_PADDLE4, NULL);
	M_Controller_Test_AppendHeldKey(other, sizeof(other), K_TOUCHPAD, NULL);
	if (!strcmp(other, "Other:"))
		q_strlcat(other, " -", sizeof(other));

	M_Controller_Test_DimPrint(16, y, face, dim);
	M_Controller_Test_DimPrint(16, y + 8, other, dim);
}

static void M_Controller_Test_DrawHintLetter(int x, int y, const char *s, qboolean dim)
{
	if (dim)
		M_PrintRGBA(x, y, s, CL_PLColours_Parse("0xffffff"), M_CONTROLLER_TEST_DIM_ALPHA, true);
	else
		M_PrintWhite(x, y, s);
}

static void M_Controller_Test_DrawHints(int y, qboolean dim)
{
	qboolean show_rumble, show_gyro;
	int w, x;

	if (dim)
	{
		show_rumble = true;
		show_gyro = true;
	}
	else
	{
		show_rumble = IN_HasRumble();
		show_gyro = IN_HasGyro();
	}

	w = 0;
	if (show_rumble) w += 8;			// "A Rumble"
	if (show_gyro) w += (show_rumble ? 2 : 0) + 11;	// "  X Calibrate"
	if (!w) return;

	x = (320 - 8 * w) / 2;
	if (show_rumble)
	{
		M_Controller_Test_DrawHintLetter(x, y, "A", dim);
		M_Controller_Test_DimPrint(x + 8, y, " Rumble", dim);
		x += 8 * 8;
	}
	if (show_gyro)
	{
		if (show_rumble)
			x += 16;
		M_Controller_Test_DrawHintLetter(x, y, "X", dim);
		M_Controller_Test_DimPrint(x + 8, y, " Calibrate", dim);
	}
}

static void M_Controller_Test_DrawStatusValue(int x, int y, const char *value, qboolean present, qboolean warn)
{
	if (!present)
		M_PrintRGBA(x, y, value, CL_PLColours_Parse("0xffffff"), M_CONTROLLER_TEST_PLACEHOLDER_ALPHA, true);
	else if (warn)
		M_PrintRGBA(x, y, value, CL_PLColours_Parse("0xffcc44"), 1.f, true);
	else
		M_PrintWhite(x, y, value);
}

static void M_Controller_Test_DrawStatusValueRA(int end_x, int y, const char *value, qboolean present, qboolean warn)
{
	int x = end_x - 8 * (int)strlen(value);
	M_Controller_Test_DrawStatusValue(x, y, value, present, warn);
}

void M_Menu_Controller_Test_f(void)
{
	if (m_state != m_controller_test && m_state != m_none)
		controller_test_prev = m_state;
	else
		controller_test_prev = m_controller;

	key_dest = key_menu;
	m_state = m_controller_test;
	m_entersound = true;

	IN_UpdateGrabs();
}

static void M_Controller_Test_Back(void)
{
	if (controller_test_prev == m_options)
		M_Menu_Options_f();
	else
		M_Menu_Controller_f();
}

void M_Controller_Test_Draw(void)
{
	qpic_t *p;
	float movex, movey, lookx, looky, trigleft, trigright;
	float gyro, r;
	char value[32];
	const char *title = "Controller Test";
	qboolean has_pad = IN_HasGamepad();
	qboolean dim = !has_pad;

	IN_GetRawMoveAxis(&movex, &movey);
	IN_GetRawLookAxis(&lookx, &looky);
	IN_GetRawTriggerAxis(&trigleft, &trigright);

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);
	M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);

	{
		const char *device_label = M_Controller_GetDeviceLabel();

		M_Print(16, 48, "Device");
		M_Controller_Test_DrawStatusValue(72, 48, device_label, true, false);

		M_Print(16, 56, "Type");
		M_Controller_Test_DrawStatusValueRA(152, 56, has_pad ? M_Controller_Test_GamepadTypeLabel() : "--", has_pad, false);

		M_Print(160, 56, "Rumble");
		M_Controller_Test_DrawStatusValueRA(240, 56, has_pad ? (IN_HasRumble() ? "Yes" : "No") : "--", has_pad && IN_HasRumble(), false);

		M_Print(248, 56, "Gyro");
		M_Controller_Test_DrawStatusValueRA(312, 56, has_pad ? (IN_HasGyro() ? "Yes" : "No") : "--", has_pad && IN_HasGyro(), false);
	}

	M_Controller_Test_DrawStickBox(24, 72, "Move", movex, movey, joy_deadzone_move.value, dim);
	M_Controller_Test_DrawStickBox(112, 72, "Look", lookx, looky, joy_deadzone_look.value, dim);

	q_snprintf(value, sizeof(value), "X%+.1f Y%+.1f", movex, movey);
	M_Controller_Test_DimPrint(56 - 4 * (int)strlen(value), 120, value, dim);
	q_snprintf(value, sizeof(value), "X%+.1f Y%+.1f", lookx, looky);
	M_Controller_Test_DimPrint(144 - 4 * (int)strlen(value), 120, value, dim);

	{
		qboolean lt_active = !dim && trigleft  > joy_deadzone_trigger.value;
		qboolean rt_active = !dim && trigright > joy_deadzone_trigger.value;

		if (lt_active) M_PrintWhite(40, 136, "Left Trigger");
		else           M_Controller_Test_DimPrint(40, 136, "Left Trigger", dim);
		M_Controller_DrawSliderMaybeDim(176, 136, trigleft,  trigleft  * 100.f, "%.0f%%", !dim);

		if (rt_active) M_PrintWhite(40, 144, "Right Trigger");
		else           M_Controller_Test_DimPrint(40, 144, "Right Trigger", dim);
		M_Controller_DrawSliderMaybeDim(176, 144, trigright, trigright * 100.f, "%.0f%%", !dim);
	}

	if (IN_HasGyro())
	{
		qboolean gyro_active;
		gyro = IN_GetRawGyroMagnitude();
		r = CLAMP(0.f, gyro / 180.f, 1.f);
		gyro_active = gyro > gyro_noise_thresh.value;
		if (gyro_active) M_PrintWhite(40, 152, "Gyro");
		else             M_Controller_Test_DimPrint(40, 152, "Gyro", dim);
		M_Controller_DrawSliderMaybeDim(176, 152, r, gyro, "%.0f", !dim);
	}
	else
	{
		M_Controller_Test_DimPrint(40, 152, "Gyro", dim);
		M_Controller_Test_DimPrint(176, 152, "Unavailable", dim);
	}

	M_Controller_Test_DrawHeldButtons(168, dim);
	M_Controller_Test_DrawHints(184, dim);
}

void M_Controller_Test_Key(int key)
{
	switch (key)
	{
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		if (!IN_HasGamepad())
		{
			S_LocalSound("misc/menu2.wav");
			M_Menu_Controller_f();
		}
		else if (IN_HasRumble())
		{
			S_LocalSound("misc/menu3.wav");
			IN_TestRumble();
		}
		else
		{
			S_LocalSound("misc/menu1.wav");
		}
		break;

	case K_XBUTTON:
	case 'c':
	case 'C':
		if (IN_HasGyro())
		{
			M_Menu_Calibration_f();
		}
		else
		{
			S_LocalSound("misc/menu1.wav");
		}
		break;

	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		M_Controller_Test_Back();
		break;
	}
}

/*
==================
Graphics Menu
==================
*/

extern cvar_t r_particles, gl_load24bit, r_replacemodels, r_lerpmodels, r_lerpmove, r_scale, r_outline, r_waterwarp,
vid_gamma, vid_contrast, vid_fsaa, r_particledesc, gl_loadlitfiles, r_rocketlight, r_explosionlight,
gl_powerupshells, gl_caustics, gl_cshift_contents_auto;

static enum graphics_e
{
	GRAPHICS_BRIGHTNESS,
	GRAPHICS_CONTRAST,
	GRAPHICS_FILTERING,
	GRAPHICS_ANTIALIASING,
	GRAPHICS_SOFTEMU,
	GRAPHICS_EXTERNALTEX,
	GRAPHICS_REPLACEMENTMODELS,
	GRAPHICS_ROCKETLIGHT,     // Added
	GRAPHICS_EXPLOSIONLIGHT,  // Added
	GRAPHICS_MODELLERP,
	GRAPHICS_RENDERSCALE,
	GRAPHICS_CLASSICPARTICLES,
	GRAPHICS_CUSTOMPARTICLES,    // Added
	GRAPHICS_COLOREDLIGHTING,    // Added
	GRAPHICS_ALIASSHADOW,
	GRAPHICS_BRUSHSHADOW,
	GRAPHICS_MODELOUTLINES,
	GRAPHICS_POWERUPSHELLS,
	GRAPHICS_WATERCAUSTICS,
	GRAPHICS_WATERWARP,
	GRAPHICS_WATERALPHA,
	GRAPHICS_CSHIFTAUTO,
	GRAPHICS_SKY,
	GRAPHICS_COUNT
} graphics_cursor;

#define GRAPHICS_ITEMS (GRAPHICS_COUNT)
#define GRAPHICS_ROW_HEIGHT 8
#define GRAPHICS_DEFAULT_Y 20
#define GRAPHICS_MIN_Y 12
#define GRAPHICS_BOTTOM_Y 188
int numberOfGraphicsItems = GRAPHICS_ITEMS;

static struct
{
	int cursor;
	struct {
		char text[32];
		int len;
	} search;
} graphicsmenu;

static int M_Graphics_FirstY(void)
{
	int y = GRAPHICS_BOTTOM_Y - (GRAPHICS_ITEMS - 1) * GRAPHICS_ROW_HEIGHT;

	if (y > GRAPHICS_DEFAULT_Y)
		return GRAPHICS_DEFAULT_Y;
	if (y < GRAPHICS_MIN_Y)
		return GRAPHICS_MIN_Y;
	return y;
}

static int M_Graphics_ItemY(int index)
{
	return M_Graphics_FirstY() + GRAPHICS_ROW_HEIGHT * index;
}

static const char* M_Graphics_GetItemText(int index)
{
	static char buffer[64];

	switch (index)
	{
	case GRAPHICS_BRIGHTNESS:
		return "Brightness";
	case GRAPHICS_CONTRAST:
		return "Contrast";
	case GRAPHICS_FILTERING:
		return "Texture Filtering";
	case GRAPHICS_ANTIALIASING:
		return "Screen Anti-Aliasing";
	case GRAPHICS_SOFTEMU:
		return "Software Emulation";
	case GRAPHICS_EXTERNALTEX:
		return "External Textures";
	case GRAPHICS_REPLACEMENTMODELS:
		return "Custom Models";
	case GRAPHICS_ROCKETLIGHT:
		return "Rocket Light";
	case GRAPHICS_EXPLOSIONLIGHT:
		return "Explosion Light";
	case GRAPHICS_MODELLERP:
		return "Smooth Model Anims";
	case GRAPHICS_RENDERSCALE:
		return "Render Scale";
	case GRAPHICS_CLASSICPARTICLES:
		return "Classic Particles";
	case GRAPHICS_ALIASSHADOW:
		return "Shadows";
	case GRAPHICS_BRUSHSHADOW:
		return "Brush Shadows";
	case GRAPHICS_CUSTOMPARTICLES:
		return "Custom Particles";
	case GRAPHICS_COLOREDLIGHTING:
		return "Colored Lighting";
	case GRAPHICS_MODELOUTLINES:
		return "Model Outlines";
	case GRAPHICS_POWERUPSHELLS:
		return "Powerup Shells";
	case GRAPHICS_WATERCAUSTICS:
		return "Water Caustics";
	case GRAPHICS_WATERWARP:
		return "Underwater FX";
	case GRAPHICS_WATERALPHA:
		return "Liquid Alpha";
	case GRAPHICS_CSHIFTAUTO:
		return "Auto Liquid Tint";
	case GRAPHICS_SKY:
		return "Sky";
	default:
		q_snprintf(buffer, sizeof(buffer), "Unknown Item %d", index);
		return buffer;
	}
}

static void M_Graphics_ClampCursor(void)
{
	int cursor = (int)graphics_cursor;

	if (cursor < 0 || cursor >= GRAPHICS_ITEMS)
	{
		cursor %= GRAPHICS_ITEMS;
		if (cursor < 0)
			cursor += GRAPHICS_ITEMS;
		graphics_cursor = (enum graphics_e)cursor;
	}
}

static qboolean M_Graphics_ItemMatchesSearch(int index)
{
	const char* itemtext = M_Graphics_GetItemText(index);

	return itemtext && q_strcasestr(itemtext, graphicsmenu.search.text);
}

static void M_Graphics_UpdateSearch(void)
{
	int i;
	int first_match = -1;
	const qboolean has_search = graphicsmenu.search.len > 0;
	const qboolean current_matches = has_search &&
		M_Graphics_ItemMatchesSearch((int)graphics_cursor);

	M_Graphics_ClampCursor();

	if (!has_search)
	{
		numberOfGraphicsItems = GRAPHICS_ITEMS;
		return;
	}

	numberOfGraphicsItems = 0;
	for (i = 0; i < GRAPHICS_ITEMS; i++)
	{
		if (M_Graphics_ItemMatchesSearch(i))
		{
			if (first_match < 0)
				first_match = i;
			numberOfGraphicsItems++;
		}
	}

	if (numberOfGraphicsItems > 0 && !current_matches)
		graphics_cursor = (enum graphics_e)first_match;
}

static void M_Graphics_MoveCursor(int delta)
{
	int cursor;
	int i;

	M_Graphics_ClampCursor();

	if (graphicsmenu.search.len <= 0)
	{
		cursor = (int)graphics_cursor + delta;
		cursor %= GRAPHICS_ITEMS;
		if (cursor < 0)
			cursor += GRAPHICS_ITEMS;
		graphics_cursor = (enum graphics_e)cursor;
		return;
	}

	if (numberOfGraphicsItems <= 0)
		return;

	cursor = (int)graphics_cursor;
	for (i = 0; i < GRAPHICS_ITEMS; i++)
	{
		cursor += delta;
		cursor %= GRAPHICS_ITEMS;
		if (cursor < 0)
			cursor += GRAPHICS_ITEMS;

		if (M_Graphics_ItemMatchesSearch(cursor))
		{
			graphics_cursor = (enum graphics_e)cursor;
			return;
		}
	}
}

static int M_Graphics_LivePreviewId(void)
{
	switch (graphics_cursor)
	{
	case GRAPHICS_BRIGHTNESS:		return LP_BRIGHTNESS;
	case GRAPHICS_CONTRAST:			return LP_CONTRAST;
	case GRAPHICS_FILTERING:		return LP_FILTERING;
	case GRAPHICS_MODELLERP:		return LP_MODELLERP;
	case GRAPHICS_RENDERSCALE:		return LP_RENDERSCALE;
	case GRAPHICS_CLASSICPARTICLES:	return LP_CLASSICPARTICLES;
	case GRAPHICS_POWERUPSHELLS:	return chase_active.value ? LP_NONE : LP_POWERUPSHELLS;
	case GRAPHICS_SOFTEMU:
	case GRAPHICS_CUSTOMPARTICLES:
	case GRAPHICS_ALIASSHADOW:
	case GRAPHICS_BRUSHSHADOW:
	case GRAPHICS_MODELOUTLINES:
		return LP_GRAPHICS;
	case GRAPHICS_WATERCAUSTICS:
	case GRAPHICS_WATERWARP:
	case GRAPHICS_WATERALPHA:
	case GRAPHICS_CSHIFTAUTO:
		return (cl.inwater ||
				(r_viewleaf &&
				 (r_viewleaf->contents == CONTENTS_WATER ||
				  r_viewleaf->contents == CONTENTS_SLIME ||
				  r_viewleaf->contents == CONTENTS_LAVA))) ? LP_GRAPHICS : LP_NONE;
	default:						return LP_NONE;
	}
}

void M_Menu_Graphics_f(void)
{
	key_dest = key_menu;
	m_state = m_graphics;
	m_entersound = true;
	graphics_cursor = 0;
	graphicsmenu.cursor = 0;
	graphicsmenu.search.len = 0;
	graphicsmenu.search.text[0] = 0;
	numberOfGraphicsItems = GRAPHICS_ITEMS;
	M_LivePreview_Reset();

	IN_UpdateGrabs();
}

// Set all liquid alphas together so the slider controls water/lava/slime/teleporters
// uniformly. lava/slime/tele default to 0 (follow r_wateralpha); we set them explicitly
// so the chosen value applies even on maps where they were tuned separately.
static void M_Graphics_SetLiquidAlpha(float f)
{
	Cvar_SetValueQuick(&r_wateralpha, f);
	Cvar_SetValueQuick(&r_lavaalpha, f);
	Cvar_SetValueQuick(&r_slimealpha, f);
	Cvar_SetValueQuick(&r_telealpha, f);
}

static void M_Graphics_AdjustSliders(int dir)
{
	int m;
	float f;
	S_LocalSound("misc/menu3.wav");

	M_LivePreview_WantAndKick (M_Graphics_LivePreviewId (), M_Graphics_ItemY(graphics_cursor));

	switch (graphics_cursor)
	{
		case GRAPHICS_BRIGHTNESS:
			f = vid_gamma.value - dir * 0.05f;
			if (f < 0.5)    f = 0.5;
			else if (f > 1) f = 1;
			Cvar_SetValue("gamma", f);
			break;

		case GRAPHICS_CONTRAST:
			f = vid_contrast.value + dir * 0.1f;
			if (f < 1)    f = 1;
			else if (f > 2) f = 2;
			Cvar_SetValue("contrast", f);
			break;

		case GRAPHICS_FILTERING:
			m = TexMgr_GetTextureMode() + dir;
			while (m == 3 || (m > 4 && m < 8) || (m > 8 && m < 16))
				m += dir;
			if (m < 0)
				m = 16;
			else if (m > 16)
				m = 0;
			if (m == 0)
			{
				Cvar_Set("gl_texturemode", "nll");
				Cvar_Set("gl_texture_anisotropy", "1");
			}
			else
			{
				Cvar_Set("gl_texturemode", "GL_LINEAR_MIPMAP_LINEAR");
				Cvar_SetValue("gl_texture_anisotropy", m);
			}
			break;

		case GRAPHICS_ANTIALIASING:
		{
			static const int aa_values[] = { 0, 2, 4, 6, 8, 16 };
			int current = vid_fsaa.value;
			int current_index = 0;

			// Find current index
			for (int i = 0; i < 6; i++) {
				if (aa_values[i] == current) {
					current_index = i;
					break;
				}
			}

			// Adjust index
			current_index += dir;
			if (current_index < 0) current_index = 5;
			if (current_index > 5) current_index = 0;

			Cvar_SetValue("vid_fsaa", aa_values[current_index]);
		}
		break;

		case GRAPHICS_SOFTEMU:
		{
			// Shader only distinguishes off / dithered (1,2) / banded (3),
			// so cycle the three visually-distinct states.
			static const int modes[] = {0, 1, 3};
			int cur = (int)r_softemu.value;
			int idx = (cur >= 3) ? 2 : (cur >= 1) ? 1 : 0;
			idx += (dir > 0) ? 1 : -1;
			if (idx < 0) idx = 2;
			else if (idx > 2) idx = 0;
			Cvar_SetValueQuick(&r_softemu, modes[idx]);
		}
		break;

		default:
			break;

	case GRAPHICS_EXTERNALTEX:
		Cvar_SetValueQuick(&gl_load24bit, !gl_load24bit.value);
		Cbuf_AddText("flush\n");
		break;

	case GRAPHICS_REPLACEMENTMODELS:
		Cvar_SetQuick(&r_replacemodels, *r_replacemodels.string ? "" : "iqm md5mesh md3");
		Cbuf_AddText("flush\n");
		break;

	case GRAPHICS_ROCKETLIGHT:
	{
		float f = r_rocketlight.value + dir;
		f = CLAMP(0, f, 100);
		Cvar_SetValue("r_rocketlight", f);
	}
	break;

	case GRAPHICS_EXPLOSIONLIGHT:
	{
		float f = r_explosionlight.value + dir;
		f = CLAMP(0, f, 100);
		Cvar_SetValue("r_explosionlight", f);
	}
	break;

	case GRAPHICS_MODELLERP:
		if (r_lerpmodels.value || r_lerpmove.value)
		{
			Cvar_SetValueQuick(&r_lerpmodels, 0);
			Cvar_SetValueQuick(&r_lerpmove, 0);
		}
		else
		{
			Cvar_SetValueQuick(&r_lerpmodels, 1);
			Cvar_SetValueQuick(&r_lerpmove, 1);
		}
		break;

	case GRAPHICS_RENDERSCALE:
		if (dir > 0) {
			m = r_scale.value + 1;
			if (m > 4) m = 1;
		}
		else {
			m = r_scale.value - 1;
			if (m < 1) m = 4;
		}
		Cvar_SetValueQuick(&r_scale, m);
		break;

	case GRAPHICS_CLASSICPARTICLES:
		Cvar_SetValueQuick(&r_particles, (r_particles.value == 1) ? 2 : 1);
		break;

	case GRAPHICS_ALIASSHADOW:
		f = r_shadows.value + dir * 0.1f;
		f = CLAMP(0, f, 1);
		Cvar_SetValue("r_shadows", f);
		break;

	case GRAPHICS_BRUSHSHADOW:
		Cvar_SetValue("r_shadows_bmodels", !r_shadows_bmodels.value);
		break;

	case GRAPHICS_CUSTOMPARTICLES:
		if (Q_strcmp(r_particledesc.string, "qssm") == 0)
			Cvar_Set("r_particledesc", "qssmc");
		else if (Q_strcmp(r_particledesc.string, "qssmc") == 0)
			Cvar_Set("r_particledesc", "classic");
		else
			Cvar_Set("r_particledesc", "qssm");
		break;

	case GRAPHICS_COLOREDLIGHTING:
		Cvar_SetValue("gl_loadlitfiles", !gl_loadlitfiles.value);
		break;

	case GRAPHICS_MODELOUTLINES:
		f = r_outline.value + dir;
		f = CLAMP(0, f, 5);
		Cvar_SetValue("r_outline", f);
		break;

	case GRAPHICS_POWERUPSHELLS:
	{
		int value = gl_powerupshells.value + dir;
		if (value < 0) value = 2;
		if (value > 2) value = 0;
		Cvar_SetValue("gl_powerupshells", value);
		break;
	}

	case GRAPHICS_WATERCAUSTICS:
	{
		float f_wc = gl_caustics.value + dir * 0.1f; // Slider 0-100% maps to value 0-10
		f_wc = CLAMP(0, f_wc, 10);
		Cvar_SetValue("gl_caustics", f_wc);
		break;
	}
	break;

	case GRAPHICS_WATERWARP:
		m = CLAMP(0, (int)r_waterwarp.value, 2);
		m += (dir > 0) ? 1 : -1;
		if (m < 0) m = 2;
		else if (m > 2) m = 0;
		Cvar_SetValueQuick(&r_waterwarp, m);
		break;

	case GRAPHICS_WATERALPHA:
		f = r_wateralpha.value + dir * 0.05f;
		f = CLAMP(0, f, 1);
		M_Graphics_SetLiquidAlpha(f);
		break;

	case GRAPHICS_CSHIFTAUTO:
		Cvar_SetValueQuick(&gl_cshift_contents_auto, gl_cshift_contents_auto.value ? 0 : 1);
		break;

	case GRAPHICS_SKY:
		M_Menu_Sky_f();
		break;
	}
}

void M_Graphics_Draw(void)
{
	enum graphics_e i;
	float r;
	int m;

	M_Graphics_ClampCursor();

	M_LivePreview_WantAt (M_Graphics_LivePreviewId (), M_Graphics_ItemY(graphics_cursor));

	const char* title = "Graphics Options";
	M_PrintWhite((320 - 8 * strlen(title)) / 2, 4, title);

	for (i = 0; i < GRAPHICS_ITEMS; i++)
	{
		int y = M_Graphics_ItemY(i);
		qboolean isolated = M_LivePreview_IsolateY (y);
		const char* text = NULL;
		const char* value = NULL;

		if (isolated)
			M_LivePreview_BeginIsolate ();

		switch (i)
		{
		case GRAPHICS_BRIGHTNESS:
			text = "        Brightness";
			r = (1.0 - vid_gamma.value) / 0.5;
			M_DrawSlider(186, y, r, 10.f * r, "%.0f");
			break;

		case GRAPHICS_CONTRAST:
			text = "          Contrast";
			r = vid_contrast.value - 1.0;
			M_DrawSlider(186, y, r, 10.f * r, "%.0f");
			break;
		
		case GRAPHICS_FILTERING:
			text = " Texture Filtering";
			m = TexMgr_GetTextureMode();
			switch (m)
			{
			case 0: value = "nearest"; break;
			case 1: value = "linear"; break;
			default: value = va("aniso %i", m); break;
			}
			M_Print(178, y, value);
			break;

		case GRAPHICS_ANTIALIASING:
			text = "     Anti-Aliasing";
			if (vid_fsaa.value == 0)
				value = "off";
			else
				value = va("%ix", (int)vid_fsaa.value);
			M_Print(178, y, value);
			break;

		case GRAPHICS_SOFTEMU:
			text = "Software Emulation";
			if ((int)r_softemu.value <= 0)
				value = "off";
			else if ((int)r_softemu.value >= 3)
				value = "8-bit (banded)";
			else
				value = "8-bit (dithered)";
			M_Print(178, y, value);
			break;

		case GRAPHICS_EXTERNALTEX:
			text = " External Textures";
			M_DrawCheckbox(178, y, !!gl_load24bit.value);
			break;

		case GRAPHICS_REPLACEMENTMODELS:
			text = "     Custom Models";
			M_DrawCheckbox(178, y, !!*r_replacemodels.string);
			break;

		case GRAPHICS_ROCKETLIGHT:
			text = "      Rocket Light";
			r = r_rocketlight.value / 100.0;
			M_DrawSlider(186, y, r, r_rocketlight.value, "%.0f%%");
			break;

		case GRAPHICS_EXPLOSIONLIGHT:
			text = "   Explosion Light";
			r = r_explosionlight.value / 100.0;
			M_DrawSlider(186, y, r, r_explosionlight.value, "%.0f%%");
			break;

		case GRAPHICS_MODELLERP:
			text = "Smooth Model Anims";
			M_DrawCheckbox(178, y, !!r_lerpmodels.value && !!r_lerpmove.value);
			break;

		case GRAPHICS_RENDERSCALE:
			text = "      Render Scale";
			if (r_scale.value == 1)
				M_Print(178, y, "native (1/1)");
			else if (r_scale.value == 2)
				M_Print(178, y, "half (1/2)");
			else if (r_scale.value == 3)
				M_Print(178, y, "third (1/3)");
			else if (r_scale.value == 4)
				M_Print(178, y, "quarter (1/4)");
			else
				M_Print(178, y, "unknown");
			break;

		case GRAPHICS_CLASSICPARTICLES:
			text = " Classic Particles";
			value = r_particles.value == 1 ? "round (winquake)" : "square (glquake)";
			M_Print(178, y, value);
			break;

		case GRAPHICS_ALIASSHADOW:
			text = "           Shadows";
			r = r_shadows.value;
			M_DrawSlider(186, y, r, r_shadows.value, "%.1f");
			break;

		case GRAPHICS_BRUSHSHADOW:
			text = "     Brush Shadows";
			M_DrawCheckbox(178, y, r_shadows_bmodels.value != 0);
			break;

		case GRAPHICS_CUSTOMPARTICLES:
			text = "  Custom Particles";
			if (Q_strcmp(r_particledesc.string, "qssm") == 0)
				value = "qssm";
			else if (Q_strcmp(r_particledesc.string, "qssmc") == 0)
				value = "classic+qssm";
			else
				value = "off (classic)";
			M_Print(178, y, value);
			break;

		case GRAPHICS_COLOREDLIGHTING:
			text = "  Colored Lighting";
			M_DrawCheckbox(178, y, gl_loadlitfiles.value != 0);
			break;

		case GRAPHICS_MODELOUTLINES:
			text = "    Model Outlines";
			r = r_outline.value / 5.0; // Normalize to 0-1 range for slider (max value is 5)
			M_DrawSlider(186, y, r, r_outline.value, "%.0f");
			break;

		case GRAPHICS_POWERUPSHELLS:
			text = "    Powerup Shells";
			if (gl_powerupshells.value == 0)
				value = "off";
			else if (gl_powerupshells.value == 1)
				value = "shell+effects";
			else
				value = "shell+items";
			M_Print(178, y, value);
			break;

		case GRAPHICS_WATERCAUSTICS:
			text = "    Water Caustics"; // Adjust spacing as needed
			r = gl_caustics.value / 10.0f; // Normalize 0-10 value to 0-1 for slider
			M_DrawSlider(186, y, r, gl_caustics.value * 10.0f, "%.0f%%"); // Display as 0-100%
			break;

		case GRAPHICS_WATERWARP:
			text = "    Underwater FX";
			if ((int)r_waterwarp.value <= 0)
				value = "off";
			else if ((int)r_waterwarp.value == 1)
				value = "classic";
			else
				value = "glQuake";
			M_Print(178, y, value);
			break;

		case GRAPHICS_WATERALPHA:
			text = "      Liquid Alpha";
			r = CLAMP(0, r_wateralpha.value, 1);
			M_DrawSlider(186, y, r, 100.f * r, "%.0f%%");
			break;

		case GRAPHICS_CSHIFTAUTO:
			text = "  Auto Liquid Tint";
			M_DrawCheckbox(178, y, gl_cshift_contents_auto.value != 0);
			break;

		case GRAPHICS_SKY:
			text = "               Sky";
			M_Print(178, y, "...");
			break;

		default:
			break;
		}

		if (text)
		{
			if (graphicsmenu.search.len > 0 &&
				q_strcasestr(text, graphicsmenu.search.text))
			{
				M_PrintHighlight(0, y, text,
					graphicsmenu.search.text,
					graphicsmenu.search.len);
			}
			else
			{
				M_Print(0, y, text);
			}

			if (value)
				M_Print(178, y, value);
		}

		if (isolated)
			M_LivePreview_EndIsolate ();
	}

	// Draw search box if search is active
	if (graphicsmenu.search.len > 0)
	{
		M_DrawTextBox(16, 170, 32, 1);
		M_PrintHighlight(24, 178, graphicsmenu.search.text,
			graphicsmenu.search.text,
			graphicsmenu.search.len);
		int cursor_x = 24 + 8 * graphicsmenu.search.len;
		if (numberOfGraphicsItems == 0)
			M_DrawCharacter(cursor_x, 178, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, 178, 10 + ((int)(realtime * 4) & 1));
	}

	// cursor
	{
		int y = M_Graphics_ItemY(graphics_cursor);
		qboolean isolated = M_LivePreview_IsolateY (y);
		if (isolated)
			M_LivePreview_BeginIsolate ();
		M_DrawCharacter(168, y, 12 + ((int)(realtime * 4) & 1));
		if (isolated)
			M_LivePreview_EndIsolate ();
	}
}

void M_Graphics_Key(int k)
{
	// Handle slider grab release
	if (!keydown[K_MOUSE1])
		graphics_slider_grab = false;

	if (graphics_slider_grab)
	{
		switch (k)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			graphics_slider_grab = false;
			break;
		}
		return;
	}

	// Handle search functionality first
	if (k == K_ESCAPE)
	{
		if (graphicsmenu.search.len > 0)
		{
			graphicsmenu.search.len = 0;
			graphicsmenu.search.text[0] = 0;
			M_Graphics_UpdateSearch();
			return;
		}
		if (M_LivePreview_Alpha() > 0.f)
		{
			M_LivePreview_Reset();
			return;
		}
		M_Menu_Options_f();
		return;
	}
	else if (keydown[K_CTRL])
	{
		if ((k == 'u' || k == 'U') && graphicsmenu.search.len > 0)
		{
			// Clear entire search with Ctrl+U
			graphicsmenu.search.len = 0;
			graphicsmenu.search.text[0] = 0;
			M_Graphics_UpdateSearch();
			return;
		}
		else if (k == K_BACKSPACE && graphicsmenu.search.len > 0)
		{
			// Delete previous word with Ctrl+Backspace
			listsearch_t temp;
			temp.len = graphicsmenu.search.len;
			Q_strcpy(temp.text, graphicsmenu.search.text);
			M_DeletePrevWord(&temp);
			Q_strcpy(graphicsmenu.search.text, temp.text);
			graphicsmenu.search.len = temp.len;
			M_Graphics_UpdateSearch();
			return;
	}
	}
	else if (k == K_BACKSPACE)
	{
		if (graphicsmenu.search.len > 0)
		{
			graphicsmenu.search.text[--graphicsmenu.search.len] = 0;
			M_Graphics_UpdateSearch();
			return;
		}
	}
	else if (k >= 32 && k < 127)
	{
		if (graphicsmenu.search.len < sizeof(graphicsmenu.search.text) - 1)
		{
			graphicsmenu.search.text[graphicsmenu.search.len++] = k;
			graphicsmenu.search.text[graphicsmenu.search.len] = 0;
			M_Graphics_UpdateSearch();
			return;
		}
	}

	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		if (M_LivePreview_Alpha() > 0.f)
		{
			M_LivePreview_Reset();
			return;
		}
		M_Menu_Options_f();
		break;

	case K_MOUSE1:
		m_entersound = true;

		// Check if click is in search box area
		if (graphicsmenu.search.len > 0 && m_mousey >= 170)
			break;

		// Check if click is in valid menu area
		{
			int first_y = M_Graphics_FirstY();
			if (m_mousey < first_y || m_mousey >= first_y + (GRAPHICS_ITEMS * GRAPHICS_ROW_HEIGHT))
				break;

			graphics_cursor = (m_mousey - first_y) / GRAPHICS_ROW_HEIGHT;

			if (graphics_cursor == GRAPHICS_BRIGHTNESS ||
				graphics_cursor == GRAPHICS_CONTRAST ||
				graphics_cursor == GRAPHICS_ALIASSHADOW ||
				graphics_cursor == GRAPHICS_ROCKETLIGHT ||
				graphics_cursor == GRAPHICS_EXPLOSIONLIGHT ||
				graphics_cursor == GRAPHICS_MODELOUTLINES ||
				graphics_cursor == GRAPHICS_WATERCAUSTICS ||
				graphics_cursor == GRAPHICS_WATERALPHA)
			{
				graphics_slider_grab = true;
				M_LivePreview_WantAndKick (M_Graphics_LivePreviewId (), M_Graphics_ItemY(graphics_cursor));
			}
			else
			{
				M_Graphics_AdjustSliders(1);
			}
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		M_Graphics_AdjustSliders(1);
		break;

	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		M_Graphics_MoveCursor(-1);
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		M_Graphics_MoveCursor(1);
		break;

	case K_LEFTARROW:
		M_Graphics_AdjustSliders(-1);
		break;

	case K_MWHEELDOWN:
		if (graphics_cursor != GRAPHICS_SKY)
			M_Graphics_AdjustSliders(-1);
		break;

	case K_RIGHTARROW:
		M_Graphics_AdjustSliders(1);
		break;

	case K_MWHEELUP:
		if (graphics_cursor != GRAPHICS_SKY)
			M_Graphics_AdjustSliders(1);
		break;
	}
}

void M_Graphics_Mousemove(int cx, int cy)
{
	if (graphics_slider_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			graphics_slider_grab = false;
			return;
		}

		M_LivePreview_WantAndKick (M_Graphics_LivePreviewId (), M_Graphics_ItemY(graphics_cursor));

		float f;
		switch (graphics_cursor)
		{
		case GRAPHICS_BRIGHTNESS:
			f = 1.f - M_MouseToSliderFraction(cx - 187) * 0.5f;
			Cvar_SetValue("gamma", f);
			break;

		case GRAPHICS_CONTRAST:
			f = M_MouseToSliderFraction(cx - 187) + 1.f;
			Cvar_SetValue("contrast", f);
			break;

		case GRAPHICS_ALIASSHADOW:
			f = M_MouseToSliderFraction(cx - 187);
			f = CLAMP(0, f, 1);
			Cvar_SetValue("r_shadows", f);
			break;

		case GRAPHICS_ROCKETLIGHT:
			f = M_MouseToSliderFraction(cx - 187) * 100;
			Cvar_SetValue("r_rocketlight", CLAMP(0, f, 100));
			break;

		case GRAPHICS_EXPLOSIONLIGHT:
			f = M_MouseToSliderFraction(cx - 187) * 100;
			Cvar_SetValue("r_explosionlight", CLAMP(0, f, 100));
			break;

		case GRAPHICS_MODELOUTLINES:
			f = M_MouseToSliderFraction(cx - 187) * 5;
			Cvar_SetValue("r_outline", CLAMP(0, f, 5));
			break;

		case GRAPHICS_WATERCAUSTICS: // Added
			f = M_MouseToSliderFraction(cx - 187) * 10.0f; // Convert slider fraction (0-1) to value (0-10)
			Cvar_SetValue("gl_caustics", CLAMP(0, f, 10));
			break;

		case GRAPHICS_WATERALPHA:
			f = M_MouseToSliderFraction(cx - 187);
			M_Graphics_SetLiquidAlpha(CLAMP(0, f, 1));
			break;

			// Add empty cases for all other enum values to suppress warnings
		case GRAPHICS_FILTERING:
		case GRAPHICS_ANTIALIASING:
		case GRAPHICS_SOFTEMU:
		case GRAPHICS_EXTERNALTEX:
		case GRAPHICS_REPLACEMENTMODELS:
		case GRAPHICS_MODELLERP:
		case GRAPHICS_RENDERSCALE:
		case GRAPHICS_CLASSICPARTICLES:
		case GRAPHICS_CUSTOMPARTICLES:
		case GRAPHICS_COLOREDLIGHTING:
		case GRAPHICS_BRUSHSHADOW:
		case GRAPHICS_POWERUPSHELLS:
		case GRAPHICS_WATERWARP:
		case GRAPHICS_CSHIFTAUTO:
		case GRAPHICS_SKY:
		case GRAPHICS_COUNT:
			break;

		default:
			break;
		}
		return;
	}

	// Don't process mouse movement if it's in the search box area
	if (graphicsmenu.search.len > 0 && cy >= 170)
		return;

	// Calculate which menu item the mouse is over
	int item = (cy - M_Graphics_FirstY()) / GRAPHICS_ROW_HEIGHT;

	// Make sure the item is within valid range
	if (item >= 0 && item < GRAPHICS_ITEMS)
	{
		// Update the cursor position
		graphics_cursor = item;
	}
}


/*
==================
Sky Menu
==================
*/

extern cvar_t r_fastsky, r_fastskycolor, r_skyalpha, r_skyfog, r_skyspeed;
extern cvar_t r_skywind, r_globalsky, allow_download_sky;
// r_sky_quality is file-static in gl_sky.c; we access it by name via Cvar_FindVar.

void Sky_GetWindParams(float *dist, float *yaw, float *period, float *pitch);
void Sky_SetWindParams(float dist, float yaw, float period, float pitch);

static qboolean sky_rgb_active;

#define SKY_GLOBALSKY_BOX_X     178
#define SKY_GLOBALSKY_BOX_WIDTH 14
#define SKY_GLOBALSKY_TEXT_X    (SKY_GLOBALSKY_BOX_X + 8)

static enum sky_e
{
	SKY_FASTSKY,
	SKY_FASTSKY_COLOR,
	SKY_QUALITY,
	SKY_ALPHA,
	SKY_FOG,
	SKY_SPEED,
	SKY_ALLOW_DOWNLOAD,
	SKY_GLOBALSKY,
	SKY_WIND,
	SKY_COUNT
} sky_cursor;

#define SKY_ITEMS (SKY_COUNT)

static qboolean sky_field_editing;
static menu_textfield_t sky_globalsky_field;
static char sky_globalsky_buffer[MAX_QPATH];
static char sky_globalsky_hint[MAX_QPATH];
static char sky_globalsky_tabpartial[MAX_QPATH];

static qboolean M_Sky_PreviewAvailable(void)
{
	return R_WorldSkyVisible();
}

static int M_Sky_GetItemY(int index)
{
	int y = 48 + index * 8;
	if (index >= SKY_GLOBALSKY)
		y += 8;
	if (index > SKY_GLOBALSKY)
		y += 8;
	return y;
}

static int M_Sky_GetItemAtY(int cy)
{
	int i;
	for (i = 0; i < SKY_ITEMS; i++)
	{
		int y = M_Sky_GetItemY(i);
		int top = (i == SKY_GLOBALSKY) ? y - 8 : y;
		int bottom = y + 8;
		if (cy >= top && cy < bottom)
			return i;
	}
	return -1;
}

static int M_Sky_GlobalskyViewStart(const menu_textfield_t *field)
{
	int len = (int)strlen(field->text);
	if (len <= SKY_GLOBALSKY_BOX_WIDTH)
		return 0;
	return CLAMP(0, field->cursor - SKY_GLOBALSKY_BOX_WIDTH, len - SKY_GLOBALSKY_BOX_WIDTH);
}

static void M_Sky_UpdateGlobalskyHint(void)
{
	filelist_item_t *item;
	int len = (int)strlen(sky_globalsky_buffer);

	sky_globalsky_hint[0] = '\0';

	if (len <= 0)
		return;

	for (item = skylist; item; item = item->next)
	{
		if (!q_strncasecmp(item->name, sky_globalsky_buffer, len))
		{
			q_strlcpy(sky_globalsky_hint, item->name + len, sizeof(sky_globalsky_hint));
			return;
		}
	}
}

static void M_Sky_BeginFieldEdit(void)
{
	q_strlcpy(sky_globalsky_buffer, r_globalsky.string, sizeof(sky_globalsky_buffer));
	M_TextField_Init(&sky_globalsky_field, sky_globalsky_buffer,
		sizeof(sky_globalsky_buffer) - 1, false);
	sky_globalsky_field.cursor = (int)strlen(sky_globalsky_field.text);
	sky_globalsky_field.sel_start = -1;
	sky_field_editing = true;
	sky_globalsky_tabpartial[0] = '\0';
	M_Sky_UpdateGlobalskyHint();
}

static void M_Sky_EndFieldEdit(qboolean apply_changes)
{
	if (apply_changes)
		Cvar_Set("r_globalsky", sky_globalsky_buffer);
	else
		q_strlcpy(sky_globalsky_buffer, r_globalsky.string, sizeof(sky_globalsky_buffer));

	sky_globalsky_field.cursor = (int)strlen(sky_globalsky_field.text);
	sky_globalsky_field.sel_start = -1;
	M_TextField_ClampCursor(&sky_globalsky_field);
	sky_field_editing = false;
	sky_globalsky_hint[0] = '\0';
	sky_globalsky_tabpartial[0] = '\0';

	if (apply_changes && M_Sky_PreviewAvailable())
		M_LivePreview_WantAndKick (LP_SKY, M_Sky_GetItemY (SKY_GLOBALSKY));
}

static cvar_t *M_Sky_QualityCvar(void)
{
	static cvar_t *cached = NULL;
	if (!cached)
		cached = Cvar_FindVar("r_sky_quality");
	return cached;
}

static void M_Sky_ClampCursor(void)
{
	sky_cursor = (enum sky_e)M_Menu_ClampCursorValue((int)sky_cursor, SKY_ITEMS);
}

static void M_Sky_MoveCursor(int delta)
{
	sky_cursor = (enum sky_e)M_Menu_ClampCursorValue((int)sky_cursor + delta, SKY_ITEMS);
}

static int M_Sky_LivePreviewId(void)
{
	if (!M_Sky_PreviewAvailable())
		return LP_NONE;

	switch (sky_cursor)
	{
	case SKY_FASTSKY:
	case SKY_FASTSKY_COLOR:
	case SKY_QUALITY:
	case SKY_ALPHA:
	case SKY_FOG:
	case SKY_SPEED:
	case SKY_GLOBALSKY:
		return LP_SKY;
	default:
		return LP_NONE;
	}
}

static void M_Sky_KickLivePreview(void)
{
	M_LivePreview_WantAndKick (M_Sky_LivePreviewId (), M_Sky_GetItemY (sky_cursor));
}

void M_Menu_Sky_f(void)
{
	key_dest = key_menu;
	m_state = m_sky;
	m_entersound = true;
	sky_cursor = 0;
	sky_slider_grab = false;
	sky_field_editing = false;
	sky_globalsky_hint[0] = '\0';
	sky_globalsky_tabpartial[0] = '\0';

	q_strlcpy(sky_globalsky_buffer, r_globalsky.string, sizeof(sky_globalsky_buffer));
	M_TextField_Init(&sky_globalsky_field, sky_globalsky_buffer,
		sizeof(sky_globalsky_buffer) - 1, false);
	M_LivePreview_Reset();

	IN_UpdateGrabs();
}

static void M_Sky_AdjustColor(int dir)
{
	const char *current = r_fastskycolor.string;

	if (keydown[K_SHIFT])
	{
		plcolour_t color = CL_PLColours_Parse(current);
		vec3_t hsv;

		sky_rgb_active = true;

		if (color.type != 2)
		{
			byte *pal = (byte *)&d_8to24table[(color.basic << 4) + 8];
			color.rgb[0] = pal[0];
			color.rgb[1] = pal[1];
			color.rgb[2] = pal[2];
		}

		rgbtohsv(color.rgb, hsv);
		hsv[0] += dir / 128.0;
		hsv[1] = 1;
		hsv[2] = 1;
		color.type = 2;
		color.basic = 0;
		hsvtorgb(hsv[0], hsv[1], hsv[2], color.rgb);

		Cvar_Set("r_fastskycolor", CL_PLColours_ToString(color));
		return;
	}

	sky_rgb_active = false;

	if (!current[0])
	{
		if (dir > 0)
		{
			plcolour_t color;
			color.type = 1;
			color.basic = 0;
			Cvar_Set("r_fastskycolor", CL_PLColours_ToString(color));
		}
		return;
	}

	{
		plcolour_t color = CL_PLColours_Parse(current);
		int newBasic;

		color.type = 1;
		newBasic = color.basic + dir;

		if (newBasic < 0)
		{
			Cvar_Set("r_fastskycolor", "");
			return;
		}
		if (newBasic > 13)
			newBasic = 0;
		color.basic = newBasic;
		Cvar_Set("r_fastskycolor", CL_PLColours_ToString(color));
	}
}

static void M_Sky_AdjustSliders(int dir)
{
	cvar_t *q;
	float f;
	int mode;

	S_LocalSound("misc/menu3.wav");

	switch (sky_cursor)
	{
	case SKY_FASTSKY:
		M_Sky_KickLivePreview();
		mode = (int)r_fastsky.value;
		mode = (mode + (dir > 0 ? 1 : 2)) % 3; // cycle 0->1->2
		Cvar_SetValue("r_fastsky", (float)mode);
		break;

	case SKY_FASTSKY_COLOR:
		M_Sky_KickLivePreview();
		M_Sky_AdjustColor(dir);
		break;

	case SKY_QUALITY:
		q = M_Sky_QualityCvar();
		if (q)
		{
			M_Sky_KickLivePreview();
			f = q->value + dir;
			f = CLAMP(4, f, 32);
			Cvar_SetValue("r_sky_quality", f);
		}
		break;

	case SKY_ALPHA:
		M_Sky_KickLivePreview();
		f = r_skyalpha.value + dir * 0.1f;
		f = CLAMP(0, f, 1);
		Cvar_SetValue("r_skyalpha", f);
		break;

	case SKY_FOG:
		M_Sky_KickLivePreview();
		f = r_skyfog.value + dir * 0.05f;
		f = CLAMP(0, f, 1);
		Cvar_SetValue("r_skyfog", f);
		break;

	case SKY_SPEED:
		M_Sky_KickLivePreview();
		f = r_skyspeed.value + dir * 0.25f;
		f = CLAMP(0, f, 10);
		Cvar_SetValue("r_skyspeed", f);
		break;

	case SKY_ALLOW_DOWNLOAD:
		Cvar_SetValue("allow_download_sky", !allow_download_sky.value);
		break;

	case SKY_GLOBALSKY:
		M_Sky_BeginFieldEdit();
		break;

	case SKY_WIND:
		M_Menu_Skywind_f();
		break;

	case SKY_COUNT:
	default:
		break;
	}
}

void M_Sky_Draw(void)
{
	qpic_t *p;
	cvar_t *q;
	float r;
	int i;

	M_TextField_CheckMouseRelease();
	M_Sky_ClampCursor();

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	{
		const char *title = "Sky";
		M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);
	}

	M_LivePreview_WantAt (M_Sky_LivePreviewId (), M_Sky_GetItemY (sky_cursor));

	for (i = 0; i < SKY_ITEMS; i++)
	{
		int y = M_Sky_GetItemY(i);
		qboolean isolated = M_LivePreview_IsolateY (y);
		const char *text = NULL;

		if (isolated)
			M_LivePreview_BeginIsolate ();

		switch (i)
		{
		case SKY_FASTSKY:
		{
			static const char *labels[3] = {"off", "flat", "auto"};
			int mode = (int)r_fastsky.value;
			text = "          Fast Sky";
			if (mode < 0) mode = 0;
			if (mode > 2) mode = 2;
			M_Print(178, y, labels[mode]);
			break;
		}

		case SKY_FASTSKY_COLOR:
		{
			const char *val = r_fastskycolor.string;
			const char *display;
			text = "    Fast Sky Color";
			if (!val[0])
			{
				display = "off";
				M_Print(178, y, display);
			}
			else if (sky_rgb_active)
			{
				display = val;
				M_Print(178, y, display);
			}
			else
			{
				plcolour_t color = CL_PLColours_Parse(val);
				display = (color.type == 2) ? val : va("%d", color.basic);
				M_Print(178, y, display);
			}
			if (val[0])
				Draw_FillPlayer(178 + (strlen(display) * 8) + 4, y + 2, 6, 6,
					CL_PLColours_Parse(val), 1.0);
			break;
		}

		case SKY_QUALITY:
			text = "       Sky Quality";
			q = M_Sky_QualityCvar();
			if (q)
			{
				float v = CLAMP(4, q->value, 32);
				r = (v - 4) / (32 - 4);
				M_DrawSlider(186, y, r, v, "%.0f");
			}
			else
			{
				M_Print(178, y, "n/a");
			}
			break;

		case SKY_ALPHA:
			text = "         Sky Alpha";
			r = r_skyalpha.value;
			M_DrawSlider(186, y, r, r * 100.0f, "%.0f%%");
			break;

		case SKY_FOG:
			text = "           Sky Fog";
			r = r_skyfog.value;
			M_DrawSlider(186, y, r, r * 100.0f, "%.0f%%");
			break;

		case SKY_SPEED:
			text = "         Sky Speed";
			r = r_skyspeed.value / 10.0f;
			M_DrawSlider(186, y, r, r_skyspeed.value, "%.2f");
			break;

		case SKY_ALLOW_DOWNLOAD:
			text = "  Skybox Downloads";
			M_DrawCheckbox(178, y, allow_download_sky.value != 0);
			break;

		case SKY_GLOBALSKY:
		{
			menu_textfield_t *field = &sky_globalsky_field;
			int view_start = M_Sky_GlobalskyViewStart(field);
			int sel_begin, sel_end;

			text = "        Global Sky";
			M_DrawTextBox(SKY_GLOBALSKY_BOX_X, y - 8, SKY_GLOBALSKY_BOX_WIDTH, 1);

			if (M_TextField_GetSelection(field, &sel_begin, &sel_end))
			{
				int visible_begin = CLAMP(view_start, sel_begin, view_start + SKY_GLOBALSKY_BOX_WIDTH);
				int visible_end   = CLAMP(view_start, sel_end,   view_start + SKY_GLOBALSKY_BOX_WIDTH);
				if (visible_begin < visible_end)
				{
					Draw_Fill(SKY_GLOBALSKY_TEXT_X + (visible_begin - view_start) * 8, y,
						(visible_end - visible_begin) * 8, 8, 170, 0.4f);
				}
			}

			if (field->text[0])
			{
				char visible_text[SKY_GLOBALSKY_BOX_WIDTH + 1];
				q_strlcpy(visible_text, field->text + view_start, sizeof(visible_text));
				M_PrintWhite(SKY_GLOBALSKY_TEXT_X, y, visible_text);

				if (sky_field_editing &&
					sky_globalsky_hint[0] &&
					field->cursor == (int)strlen(field->text))
				{
					int hint_col = field->cursor - view_start;
					int max_hint_len = SKY_GLOBALSKY_BOX_WIDTH - hint_col;

					if (hint_col >= 0 && max_hint_len > 0)
					{
						char visible_hint[SKY_GLOBALSKY_BOX_WIDTH + 1];
						q_strlcpy(visible_hint, sky_globalsky_hint, (size_t)max_hint_len + 1);
						M_PrintRGBA(SKY_GLOBALSKY_TEXT_X + hint_col * 8, y, visible_hint,
							CL_PLColours_Parse("0xffffff"), 0.5f, true);
					}
				}
			}
			else if (!sky_field_editing)
			{
				M_PrintRGBA(SKY_GLOBALSKY_TEXT_X, y, "none",
					CL_PLColours_Parse("0xffffff"), 0.5f, false);
			}

			if (sky_field_editing)
			{
				menu_textfield_t visible_field = *field;
				visible_field.cursor = CLAMP(0, field->cursor - view_start, SKY_GLOBALSKY_BOX_WIDTH);
				M_TextField_DrawCursor(&visible_field, SKY_GLOBALSKY_TEXT_X, y);
			}
			break;
		}

		case SKY_WIND:
			text = "           Skywind";
			M_Print(178, y, "...");
			break;

		default:
			break;
		}

		if (text)
			M_Print(0, y, text);

		if (isolated)
			M_LivePreview_EndIsolate ();
	}

	{
		int y = M_Sky_GetItemY(sky_cursor);
		qboolean isolated = M_LivePreview_IsolateY (y);
		if (isolated)
			M_LivePreview_BeginIsolate ();
		M_DrawCharacter(168, y, 12 + ((int)(realtime * 4) & 1));
		if (isolated)
			M_LivePreview_EndIsolate ();
	}

	if (sky_field_editing)
	{
		const char *hint = "Tab completes, Enter applies, Esc cancels";
		M_PrintRGBA((320 - (int)strlen(hint) * 8) / 2, 160, hint,
			CL_PLColours_Parse("0xffffff"), 0.5f, false);
	}
}

void M_Sky_Key(int k)
{
	if (!keydown[K_MOUSE1])
		sky_slider_grab = false;

	if (sky_slider_grab)
	{
		switch (k)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			sky_slider_grab = false;
			break;
		}
		return;
	}

	if (sky_field_editing)
	{
		if (k == K_TAB)
		{
			if (M_Menu_TabCompleteFileList(&sky_globalsky_field, sky_globalsky_buffer,
				sizeof(sky_globalsky_buffer), skylist,
				sky_globalsky_tabpartial, sizeof(sky_globalsky_tabpartial)))
			{
				M_Sky_UpdateGlobalskyHint();
				S_LocalSound("misc/menu2.wav");
			}
			return;
		}

		if (M_TextField_Key(&sky_globalsky_field, k))
		{
			sky_globalsky_tabpartial[0] = '\0';
			M_Sky_UpdateGlobalskyHint();
			return;
		}

		switch (k)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			M_Sky_EndFieldEdit(false);
			return;

		case K_ENTER:
		case K_KP_ENTER:
		case K_ABUTTON:
			S_LocalSound("misc/menu3.wav");
			M_Sky_EndFieldEdit(true);
			return;

		case K_UPARROW:
			M_Sky_EndFieldEdit(true);
			S_LocalSound("misc/menu1.wav");
			M_Sky_MoveCursor(-1);
			return;

		case K_DOWNARROW:
			M_Sky_EndFieldEdit(true);
			S_LocalSound("misc/menu1.wav");
			M_Sky_MoveCursor(1);
			return;

		case K_MOUSE1:
			if (M_TextField_MouseInRow(m_mousey, M_Sky_GetItemY(SKY_GLOBALSKY)))
			{
				int view_start = M_Sky_GlobalskyViewStart(&sky_globalsky_field);
				sky_globalsky_tabpartial[0] = '\0';
				M_TextField_MouseClick(&sky_globalsky_field, m_mousex,
					SKY_GLOBALSKY_TEXT_X - view_start * 8);
				return;
			}
			M_Sky_EndFieldEdit(true);
			break;

		default:
			break;
		}
	}

	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		if (M_LivePreview_Alpha() > 0.f)
		{
			M_LivePreview_Reset();
			return;
		}
		M_Menu_Graphics_f();
		graphics_cursor = GRAPHICS_SKY;
		break;

	case K_MOUSE1:
		m_entersound = true;
		{
			int item = M_Sky_GetItemAtY(m_mousey);
			if (item >= 0)
			{
				sky_cursor = item;
				if (sky_cursor == SKY_QUALITY || sky_cursor == SKY_ALPHA ||
					sky_cursor == SKY_FOG || sky_cursor == SKY_SPEED)
				{
					sky_slider_grab = true;
					M_Sky_KickLivePreview();
				}
				else
					M_Sky_AdjustSliders(1);
			}
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		M_Sky_AdjustSliders(1);
		break;

	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		M_Sky_MoveCursor(-1);
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		M_Sky_MoveCursor(1);
		break;

	case K_LEFTARROW:
		M_Sky_AdjustSliders(-1);
		break;

	case K_MWHEELDOWN:
		if (sky_cursor != SKY_WIND)
			M_Sky_AdjustSliders(-1);
		break;

	case K_RIGHTARROW:
		M_Sky_AdjustSliders(1);
		break;

	case K_MWHEELUP:
		if (sky_cursor != SKY_WIND)
			M_Sky_AdjustSliders(1);
		break;
	}
}

void M_Sky_Char(int k)
{
	if (!sky_field_editing)
		return;
	if (M_TextField_Char(&sky_globalsky_field, k))
	{
		sky_globalsky_tabpartial[0] = '\0';
		M_Sky_UpdateGlobalskyHint();
	}
}

qboolean M_Sky_TextEntry(void)
{
	return sky_field_editing;
}

void M_Sky_Mousemove(int cx, int cy)
{
	if (M_TextField_IsDraggingField(&sky_globalsky_field))
	{
		M_TextField_MouseDrag(cx);
		return;
	}

	if (sky_slider_grab)
	{
		cvar_t *q;
		float f;

		if (!keydown[K_MOUSE1])
		{
			sky_slider_grab = false;
			return;
		}

		M_Sky_KickLivePreview();

		switch (sky_cursor)
		{
		case SKY_QUALITY:
			q = M_Sky_QualityCvar();
			if (q)
			{
				f = 4.0f + M_MouseToSliderFraction(cx - 187) * (32 - 4);
				Cvar_SetValue("r_sky_quality", floorf(f + 0.5f));
			}
			break;

		case SKY_ALPHA:
			f = CLAMP(0.0f, M_MouseToSliderFraction(cx - 187), 1.0f);
			Cvar_SetValue("r_skyalpha", f);
			break;

		case SKY_FOG:
			f = CLAMP(0.0f, M_MouseToSliderFraction(cx - 187), 1.0f);
			Cvar_SetValue("r_skyfog", f);
			break;

		case SKY_SPEED:
			f = CLAMP(0.0f, M_MouseToSliderFraction(cx - 187), 1.0f) * 10.0f;
			Cvar_SetValue("r_skyspeed", f);
			break;

		default:
			break;
		}
		return;
	}

	if (sky_field_editing)
	{
		if (M_TextField_MouseInRow(cy, M_Sky_GetItemY(SKY_GLOBALSKY)))
			return;
		M_Sky_EndFieldEdit(true);
	}

	{
		int item = M_Sky_GetItemAtY(cy);
		if (item >= 0)
			sky_cursor = item;
	}
}


/*
==================
Skywind Menu
==================
*/

static enum skywind_e
{
	SKYWIND_STRENGTH,
	SKYWIND_DIRECTION,
	SKYWIND_PITCH,
	SKYWIND_PERIOD,
	SKYWIND_COUNT
} skywind_cursor;

#define SKYWIND_ITEMS (SKYWIND_COUNT)

static void M_Skywind_ClampCursor(void)
{
	skywind_cursor = (enum skywind_e)M_Menu_ClampCursorValue((int)skywind_cursor, SKYWIND_ITEMS);
}

static void M_Skywind_MoveCursor(int delta)
{
	skywind_cursor = (enum skywind_e)M_Menu_ClampCursorValue((int)skywind_cursor + delta, SKYWIND_ITEMS);
}

static int M_Skywind_LivePreviewId(void)
{
	return M_Sky_PreviewAvailable() ? LP_SKY : LP_NONE;
}

static void M_Skywind_KickLivePreview(void)
{
	M_LivePreview_WantAndKick (M_Skywind_LivePreviewId (), 48 + skywind_cursor * 8);
}

static void M_Skywind_Adjust(int dir)
{
	float dist, yaw, period, pitch;

	Sky_GetWindParams(&dist, &yaw, &period, &pitch);
	S_LocalSound("misc/menu3.wav");
	M_Skywind_KickLivePreview();

	switch (skywind_cursor)
	{
	case SKYWIND_STRENGTH:
		dist = CLAMP(-2.0f, dist + dir * 0.1f, 2.0f);
		break;

	case SKYWIND_DIRECTION:
		yaw += dir * 15.0f;
		while (yaw < 0.0f)   yaw += 360.0f;
		while (yaw >= 360.0f) yaw -= 360.0f;
		break;

	case SKYWIND_PITCH:
		pitch = CLAMP(-90.0f, pitch + dir * 5.0f, 90.0f);
		break;

	case SKYWIND_PERIOD:
		period = CLAMP(1.0f, period + dir * 1.0f, 120.0f);
		break;

	case SKYWIND_COUNT:
	default:
		return;
	}

	Sky_SetWindParams(dist, yaw, period, pitch);
}

void M_Menu_Skywind_f(void)
{
	key_dest = key_menu;
	m_state = m_skywind;
	m_entersound = true;
	skywind_cursor = 0;
	skywind_slider_grab = false;
	M_LivePreview_Reset();

	IN_UpdateGrabs();
}

void M_Skywind_Draw(void)
{
	qpic_t *p;
	float dist, yaw, period, pitch;
	float r;
	int i;

	M_Skywind_ClampCursor();

	Sky_GetWindParams(&dist, &yaw, &period, &pitch);

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	{
		const char *title = "Skywind";
		M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);
	}

	M_LivePreview_WantAt (M_Skywind_LivePreviewId (), 48 + skywind_cursor * 8);

	for (i = 0; i < SKYWIND_ITEMS; i++)
	{
		int y = 48 + 8 * i;
		qboolean isolated = M_LivePreview_IsolateY (y);
		const char *text = NULL;

		if (isolated)
			M_LivePreview_BeginIsolate ();

		switch (i)
		{
		case SKYWIND_STRENGTH:
			text = "          Strength";
			r = (dist + 2.0f) / 4.0f; // map -2..2 to 0..1
			M_DrawSlider(186, y, r, dist, "%.2f");
			break;

		case SKYWIND_DIRECTION:
			text = "         Direction";
			r = yaw / 360.0f;
			M_DrawSlider(186, y, r, yaw, "%.0f\xf8"); // degree sign glyph
			break;

		case SKYWIND_PITCH:
			text = "             Pitch";
			r = (pitch + 90.0f) / 180.0f;
			M_DrawSlider(186, y, r, pitch, "%.0f\xf8");
			break;

		case SKYWIND_PERIOD:
			text = "            Period";
			r = (period - 1.0f) / (120.0f - 1.0f);
			M_DrawSlider(186, y, r, period, "%.0fs");
			break;

		default:
			break;
		}

		if (text)
			M_Print(0, y, text);

		if (isolated)
			M_LivePreview_EndIsolate ();
	}

	{
		int y = 48 + skywind_cursor * 8;
		qboolean isolated = M_LivePreview_IsolateY (y);
		if (isolated)
			M_LivePreview_BeginIsolate ();
		M_DrawCharacter(168, y, 12 + ((int)(realtime * 4) & 1));
		if (isolated)
			M_LivePreview_EndIsolate ();
	}

	if (dist == 0.0f)
		M_PrintRGBA(80, 144, "strength 0 = disabled",
			CL_PLColours_Parse("0xffffff"), 0.6f, false);
}

void M_Skywind_Key(int k)
{
	if (!keydown[K_MOUSE1])
		skywind_slider_grab = false;

	if (skywind_slider_grab)
	{
		switch (k)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			skywind_slider_grab = false;
			break;
		}
		return;
	}

	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		if (M_LivePreview_Alpha() > 0.f)
		{
			M_LivePreview_Reset();
			return;
		}
		M_Menu_Sky_f();
		sky_cursor = SKY_WIND;
		break;

	case K_MOUSE1:
		m_entersound = true;
		if (m_mousey >= 48 && m_mousey < 48 + (SKYWIND_ITEMS * 8))
		{
			skywind_cursor = (m_mousey - 48) / 8;
			skywind_slider_grab = true;
			M_Skywind_KickLivePreview();
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		M_Skywind_Adjust(1);
		break;

	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		M_Skywind_MoveCursor(-1);
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		M_Skywind_MoveCursor(1);
		break;

	case K_LEFTARROW:
	case K_MWHEELDOWN:
		M_Skywind_Adjust(-1);
		break;

	case K_RIGHTARROW:
	case K_MWHEELUP:
		M_Skywind_Adjust(1);
		break;
	}
}

void M_Skywind_Mousemove(int cx, int cy)
{
	if (skywind_slider_grab)
	{
		float dist, yaw, period, pitch;
		float frac;

		if (!keydown[K_MOUSE1])
		{
			skywind_slider_grab = false;
			return;
		}

		Sky_GetWindParams(&dist, &yaw, &period, &pitch);
		frac = CLAMP(0.0f, M_MouseToSliderFraction(cx - 187), 1.0f);
		M_Skywind_KickLivePreview();

		switch (skywind_cursor)
		{
		case SKYWIND_STRENGTH:
			dist = frac * 4.0f - 2.0f;
			break;
		case SKYWIND_DIRECTION:
			yaw = floorf((frac * 360.0f) / 5.0f + 0.5f) * 5.0f;
			if (yaw >= 360.0f) yaw -= 360.0f;
			break;
		case SKYWIND_PITCH:
			pitch = frac * 180.0f - 90.0f;
			break;
		case SKYWIND_PERIOD:
			period = 1.0f + frac * (120.0f - 1.0f);
			break;
		default:
			return;
		}

		Sky_SetWindParams(dist, yaw, period, pitch);
		return;
	}

	{
		int item = (cy - 48) / 8;
		if (item >= 0 && item < SKYWIND_ITEMS)
			skywind_cursor = item;
	}
}


/*
==================
Sound Menu
==================
*/

extern cvar_t cl_ambient, ambient_level, snd_waterfx;
extern char mute[2];

static enum sound_e
{
	SOUND_VOLUME,
	SOUND_MUSICVOL,
	SOUND_MUSICEXT,
	SOUND_AUDIORATE,
	SOUND_SURROUND,
	SOUND_WATERFX,
	SOUND_AMBIENTLEVEL,
	SOUND_STOPSOUND,
	SOUND_MUTE,
	SOUND_VOIP,
	SOUND_COUNT
} sound_cursor;

#define SOUND_ITEMS (SOUND_COUNT)
int numberOfSoundItems = SOUND_ITEMS;

static struct
{
	int cursor;
	struct {
		char text[32];
		int len;
	} search;
} soundmenu;

static const char* M_Sound_GetItemText(int index)
{
	static char buffer[64];

	switch (index)
	{
	case SOUND_VOLUME:
		return "Sound Volume";
	case SOUND_MUSICVOL:
		return "Music Volume";
	case SOUND_MUSICEXT:
		return "External Music";
	case SOUND_AUDIORATE:
		return "Audio Rate";
	case SOUND_SURROUND:
		return "Surround Sound";
	case SOUND_WATERFX:
		return "Water FX";
	case SOUND_AMBIENTLEVEL:
		return "Ambient Level";
	case SOUND_STOPSOUND:
		return "Stop Sound";
	case SOUND_MUTE:
		return "Mute";
	case SOUND_VOIP:
		return "VoIP";
	default:
		q_snprintf(buffer, sizeof(buffer), "Unknown Item %d", index);
		return buffer;
	}
}

static void M_Sound_UpdateSearch(void)
{
	sound_cursor = (enum sound_e)M_Menu_UpdateSearchCursor(
		SOUND_ITEMS, (int)sound_cursor, &numberOfSoundItems,
		M_Sound_GetItemText, soundmenu.search.text, soundmenu.search.len);
}

static void M_Sound_MoveCursor(int delta)
{
	sound_cursor = (enum sound_e)M_Menu_MoveSearchCursor(
		SOUND_ITEMS, numberOfSoundItems, (int)sound_cursor, delta,
		M_Sound_GetItemText, soundmenu.search.text, soundmenu.search.len);
}

void M_Menu_Sound_f(void)
{
	key_dest = key_menu;
	m_state = m_sound;
	m_entersound = true;
	sound_cursor = 0;
	soundmenu.cursor = 0;
	soundmenu.search.len = 0;
	soundmenu.search.text[0] = 0;
	numberOfSoundItems = SOUND_ITEMS;

	IN_UpdateGrabs();
}

static void M_Sound_AdjustSliders(int dir)
{
	float f;
	S_LocalSound("misc/menu3.wav");

	switch (sound_cursor)
	{
	case SOUND_VOLUME:
		f = sfxvolume.value + dir * 0.05f;
		if (f < 0) f = 0;
		else if (f > 1) f = 1;
		Cvar_SetValue("volume", f);
		break;

	case SOUND_MUSICVOL:
		f = bgmvolume.value + dir * 0.05f;
		if (f < 0) f = 0;
		else if (f > 1) f = 1;
		Cvar_SetValue("bgmvolume", f);
		break;

	case SOUND_MUSICEXT:
		Cvar_Set("bgm_extmusic", bgm_extmusic.value ? "0" : "1");
		break;

	case SOUND_AUDIORATE:
		if (dir > 0) {
			// Going up: 11025->22050->44100->48000->11025
			if (snd_mixspeed.value == 11025)
				Cvar_SetValueQuick(&snd_mixspeed, 22050);
			else if (snd_mixspeed.value == 22050)
				Cvar_SetValueQuick(&snd_mixspeed, 44100);
			else if (snd_mixspeed.value == 44100)
				Cvar_SetValueQuick(&snd_mixspeed, 48000);
			else
				Cvar_SetValueQuick(&snd_mixspeed, 11025);
			Cbuf_AddText("\nsnd_restart\n");
		}
		else {
			// Going down: 11025<-22050<-44100<-48000<-11025
			if (snd_mixspeed.value == 48000)
				Cvar_SetValueQuick(&snd_mixspeed, 44100);
			else if (snd_mixspeed.value == 44100)
				Cvar_SetValueQuick(&snd_mixspeed, 22050);
			else if (snd_mixspeed.value == 22050)
				Cvar_SetValueQuick(&snd_mixspeed, 11025);
			else
				Cvar_SetValueQuick(&snd_mixspeed, 48000);
		}
		break;

	case SOUND_SURROUND:
		Cvar_SetValueQuick(&snd_surround, snd_surround.value > 0 ? 0 : 1);
		Cbuf_AddText("\nsnd_restart\n");
		break;

	case SOUND_WATERFX:
		f = snd_waterfx.value + dir * 0.05f;
		if (f < 0) f = 0;
		else if (f > 1) f = 1;
		Cvar_SetValue("snd_waterfx", f);
		break;

	case SOUND_AMBIENTLEVEL:
		f = ambient_level.value + dir * 0.05f;
		if (f < 0) f = 0;
		else if (f > 1) f = 1;
		Cvar_SetValue("ambient_level", f);
		break;

	case SOUND_STOPSOUND:
		Cvar_Set("cl_ambient", cl_ambient.value ? "0" : "1");
		break;

	case SOUND_MUTE:
		if (mute[0] == 'n')  // If currently not muted (showing "on")
			q_snprintf(mute, sizeof(mute), "y");  // Set to muted (will show "off")
		else
			q_snprintf(mute, sizeof(mute), "n");  // Set to not muted (will show "on")
		break;

	case SOUND_VOIP:
		M_Menu_Voip_f();
		break;

	default:
		break;
	}
}


void M_Sound_Draw(void)
{
	qpic_t* p;
	enum sound_e i;

	sound_cursor = (enum sound_e)M_Menu_ClampCursorValue((int)sound_cursor, SOUND_ITEMS);

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	const char* title = "Sound Options";
	M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);

	for (i = 0; i < SOUND_ITEMS; i++)
	{
		int y = 48 + 8 * i;
		const char* text = NULL;
		const char* value = NULL;
		float r;

		switch (i)
		{
		case SOUND_VOLUME:
			text = "      Sound Volume";
			r = sfxvolume.value;
			M_DrawSlider(186, y, r, 100.f * sfxvolume.value, "%.0f%%");
			break;

		case SOUND_MUSICVOL:
			text = "      Music Volume";
			r = bgmvolume.value;
			M_DrawSlider(186, y, r, 100.f * bgmvolume.value, "%.0f%%");
			break;

		case SOUND_MUSICEXT:
			text = "    External Music";
			M_DrawCheckbox(178, y, bgm_extmusic.value);
			break;

		case SOUND_AUDIORATE:
			text = "        Audio Rate";
			if (snd_mixspeed.value == 48000)
				value = "48000 hz (DVD)";
			else if (snd_mixspeed.value == 44100)
				value = "44100 hz (CD)";
			else if (snd_mixspeed.value == 22050)
				value = "22050 hz (Midrange)";
			else if (snd_mixspeed.value == 11025)
				value = "11025 hz (WinQuake)";
			else
				value = va("%i hz", (int)snd_mixspeed.value);
			if (value)
				M_Print(178, y, value);
			break;

		case SOUND_SURROUND:
			text = "    Surround Sound";
			M_DrawCheckbox(178, y, snd_surround.value > 0);
			break;

		case SOUND_WATERFX:
			text = "          Water FX";
			r = snd_waterfx.value;
			M_DrawSlider(186, y, r, 100.f * snd_waterfx.value, "%.0f%%");
			break;

		case SOUND_AMBIENTLEVEL:
			text = "     Ambient Level";
			r = ambient_level.value;
			M_DrawSlider(186, y, r, 100.f * ambient_level.value, "%.0f%%");
			break;

		case SOUND_STOPSOUND:
			text = "        Stop Sound";
			M_DrawCheckbox(178, y, cl_ambient.value);
			break;
		case SOUND_MUTE:
		{
			text = "              Mute";
			// If mute is 'y', sound is off. If 'n' or anything else, sound is on
			if (mute[0] == 'y')
				M_Print(178, y, "on");
			else
				M_Print(178, y, "off");
		}
		break;

		case SOUND_VOIP:
			text = "              VoIP";
			M_Print(178, y, "...");
			break;

		default:
			break;
		}

		if (text)
		{
			if (soundmenu.search.len > 0 &&
				q_strcasestr(text, soundmenu.search.text))
			{
				M_PrintHighlight(0, y, text,
					soundmenu.search.text,
					soundmenu.search.len);
			}
			else
			{
				M_Print(0, y, text);
			}
		}
	}

	// Draw search box if search is active
	if (soundmenu.search.len > 0)
	{
		M_DrawTextBox(16, 170, 32, 1);
		M_PrintHighlight(24, 178, soundmenu.search.text,
			soundmenu.search.text,
			soundmenu.search.len);
		int cursor_x = 24 + 8 * soundmenu.search.len;
		if (numberOfSoundItems == 0)
			M_DrawCharacter(cursor_x, 178, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, 178, 10 + ((int)(realtime * 4) & 1));
	}

	// cursor
	M_DrawCharacter(168, 48 + sound_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

static qboolean sound_slider_grab; // For slider dragging


void M_Sound_Key(int k)
{
	// Handle slider grab release
	if (!keydown[K_MOUSE1])
		sound_slider_grab = false;

	if (sound_slider_grab)
	{
		switch (k)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			sound_slider_grab = false;
			break;
		}
		return;
	}

	// Handle search functionality first
	if (k == K_ESCAPE)
	{
		if (soundmenu.search.len > 0)
		{
			soundmenu.search.len = 0;
			soundmenu.search.text[0] = 0;
			M_Sound_UpdateSearch();
			return;
		}
		M_Menu_Options_f();
		return;
	}
	else if (keydown[K_CTRL])
	{
		if ((k == 'u' || k == 'U') && soundmenu.search.len > 0)
		{
			// Clear entire search with Ctrl+U
			soundmenu.search.len = 0;
			soundmenu.search.text[0] = 0;
			M_Sound_UpdateSearch();
			return;
		}
		else if (k == K_BACKSPACE && soundmenu.search.len > 0)
		{
			// Delete previous word with Ctrl+Backspace
			listsearch_t temp;
			temp.len = soundmenu.search.len;
			Q_strcpy(temp.text, soundmenu.search.text);
			M_DeletePrevWord(&temp);
			Q_strcpy(soundmenu.search.text, temp.text);
			soundmenu.search.len = temp.len;
			M_Sound_UpdateSearch();
			return;
		}
	}
	else if (k == K_BACKSPACE)
	{
		if (soundmenu.search.len > 0)
		{
			soundmenu.search.text[--soundmenu.search.len] = 0;
			M_Sound_UpdateSearch();
			return;
		}
	}
	else if (k >= 32 && k < 127)
	{
		if (soundmenu.search.len < sizeof(soundmenu.search.text) - 1)
		{
			soundmenu.search.text[soundmenu.search.len++] = k;
			soundmenu.search.text[soundmenu.search.len] = 0;
			M_Sound_UpdateSearch();
			return;
		}
	}

	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		M_Menu_Options_f();
		break;

	case K_MOUSE1:
		m_entersound = true;

		// Check if click is in search box area
		if (soundmenu.search.len > 0 && m_mousey >= 170)
			break;

		// Check if click is in valid menu area
		if (m_mousey >= 48 && m_mousey < 48 + (SOUND_ITEMS * 8))
		{
			sound_cursor = (m_mousey - 48) / 8;

			if (sound_cursor == SOUND_VOLUME ||
				sound_cursor == SOUND_MUSICVOL ||
				sound_cursor == SOUND_WATERFX ||
				sound_cursor == SOUND_AMBIENTLEVEL)
			{
				sound_slider_grab = true;
			}
			else
			{
				M_Sound_AdjustSliders(1);
			}
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		M_Sound_AdjustSliders(1);
		break;

	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		M_Sound_MoveCursor(-1);
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		M_Sound_MoveCursor(1);
		break;

	case K_LEFTARROW:
		M_Sound_AdjustSliders(-1);
		break;

	case K_MWHEELDOWN:
		if (sound_cursor != SOUND_VOIP)
			M_Sound_AdjustSliders(-1);
		break;

	case K_RIGHTARROW:
		M_Sound_AdjustSliders(1);
		break;

	case K_MWHEELUP:
		if (sound_cursor != SOUND_VOIP)
			M_Sound_AdjustSliders(1);
		break;
	}
}

void M_Sound_Mousemove(int cx, int cy)
{
	if (sound_slider_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			sound_slider_grab = false;
			return;
		}

		float f;
		switch (sound_cursor)
		{
		case SOUND_VOLUME:
			f = M_MouseToSliderFraction(cx - 187);
			f = CLAMP(0, f, 1);
			Cvar_SetValue("volume", f);
			break;

		case SOUND_MUSICVOL:
			f = M_MouseToSliderFraction(cx - 187);
			f = CLAMP(0, f, 1);
			Cvar_SetValue("bgmvolume", f);
			break;

		case SOUND_WATERFX:
			f = M_MouseToSliderFraction(cx - 187);
			f = CLAMP(0, f, 1);
			Cvar_SetValue("snd_waterfx", f);
			break;

		case SOUND_AMBIENTLEVEL:
			f = M_MouseToSliderFraction(cx - 187);
			f = CLAMP(0, f, 1);
			Cvar_SetValue("ambient_level", f);
			break;

			// Add cases for unhandled enumerations to suppress warnings
		case SOUND_MUSICEXT:
		case SOUND_AUDIORATE:
		case SOUND_SURROUND:
		case SOUND_STOPSOUND:
		case SOUND_MUTE:
		case SOUND_VOIP:
		case SOUND_COUNT:
			// No action needed for these cases in mouse movement
			break;

		default:
			// Handle unexpected cases gracefully
			break;
		}
		return;
	}

	// Don't process mouse movement if it's in the search box area
	if (soundmenu.search.len > 0 && cy >= 170)
		return;

	// Calculate which menu item the mouse is over
	int item = (cy - 48) / 8;

	// Make sure the item is within valid range
	if (item >= 0 && item < SOUND_ITEMS)
	{
		// Update the cursor position
		sound_cursor = item;
	}
}


/*
==================
VoIP Menu
==================
*/

extern cvar_t cl_voip_send, cl_voip_play, cl_voip_capturingvol, cl_voip_micamp,
	cl_voip_vad_threshhold, cl_voip_vad_delay, cl_voip_ducking, cl_voip_noisefilter,
	cl_voip_autogain, cl_voip_showmeter, cl_voip_bitrate, cl_voip_test;

static enum voip_e
{
	VOIP_SEND,
	VOIP_PLAYVOL,
	VOIP_MICVOL,
	VOIP_MICAMP,
	VOIP_VADTHRESH,
	VOIP_VADDELAY,
	VOIP_DUCKING,
	VOIP_NOISEFILTER,
	VOIP_AUTOGAIN,
	VOIP_SHOWMETER,
	VOIP_BITRATE,
	VOIP_TEST,
	VOIP_COUNT
} voip_cursor;

#define VOIP_ITEMS (VOIP_COUNT)

static qboolean voip_slider_grab;

void M_Menu_Voip_f(void)
{
	key_dest = key_menu;
	m_state = m_voip;
	m_entersound = true;
	voip_cursor = 0;
	voip_slider_grab = false;

	IN_UpdateGrabs();
}

static void M_Voip_AdjustSetting(int dir)
{
	float f;
	int v;

	S_LocalSound("misc/menu3.wav");

	switch (voip_cursor)
	{
	case VOIP_SEND:
		v = (int)cl_voip_send.value + dir;
		if (v < 0) v = 2;
		else if (v > 2) v = 0;
		Cvar_SetValue("cl_voip_send", v);
		break;

	case VOIP_PLAYVOL:
		f = cl_voip_play.value + dir * 0.05f;
		f = CLAMP(0.f, f, 1.f);
		Cvar_SetValue("cl_voip_play", f);
		break;

	case VOIP_MICVOL:
		f = cl_voip_capturingvol.value + dir * 0.05f;
		f = CLAMP(0.f, f, 1.f);
		Cvar_SetValue("cl_voip_capturingvol", f);
		break;

	case VOIP_MICAMP:
		f = cl_voip_micamp.value + dir * 0.25f;
		f = CLAMP(0.f, f, 8.f);
		Cvar_SetValue("cl_voip_micamp", f);
		break;

	case VOIP_VADTHRESH:
		f = cl_voip_vad_threshhold.value + dir * 5.f;
		f = CLAMP(0.f, f, 100.f);
		Cvar_SetValue("cl_voip_vad_threshhold", f);
		break;

	case VOIP_VADDELAY:
		f = cl_voip_vad_delay.value + dir * 0.1f;
		f = CLAMP(0.f, f, 5.f);
		Cvar_SetValue("cl_voip_vad_delay", f);
		break;

	case VOIP_DUCKING:
		f = cl_voip_ducking.value + dir * 0.05f;
		f = CLAMP(0.f, f, 1.f);
		Cvar_SetValue("cl_voip_ducking", f);
		break;

	case VOIP_NOISEFILTER:
		Cvar_SetValue("cl_voip_noisefilter", cl_voip_noisefilter.value ? 0 : 1);
		break;

	case VOIP_AUTOGAIN:
		Cvar_SetValue("cl_voip_autogain", cl_voip_autogain.value ? 0 : 1);
		break;

	case VOIP_SHOWMETER:
		v = (int)cl_voip_showmeter.value + dir;
		if (v < 0) v = 2;
		else if (v > 2) v = 0;
		Cvar_SetValue("cl_voip_showmeter", v);
		break;

	case VOIP_BITRATE:
	{
		static const int rates[] = { 1500, 2000, 3000, 4000, 6000, 8000, 12000, 16000, 24000, 32000 };
		int n = (int)(sizeof(rates) / sizeof(rates[0]));
		int cur = 0, i;
		int best = 0;
		int bestdiff = 0x7fffffff;
		for (i = 0; i < n; i++)
		{
			int diff = (int)cl_voip_bitrate.value - rates[i];
			if (diff < 0) diff = -diff;
			if (diff < bestdiff) { bestdiff = diff; best = i; }
		}
		cur = best + dir;
		if (cur < 0) cur = n - 1;
		else if (cur >= n) cur = 0;
		Cvar_SetValue("cl_voip_bitrate", rates[cur]);
		break;
	}

	case VOIP_TEST:
		Cvar_SetValue("cl_voip_test", cl_voip_test.value ? 0 : 1);
		break;

	default:
		break;
	}
}

void M_Voip_Draw(void)
{
	qpic_t *p;
	enum voip_e i;
	const char *sendlabels[3] = { "off", "voice activation", "continuous" };
	const char *meterlabels[3] = { "off", "on", "verbose" };

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	const char *title = "VoIP Options";
	M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);

	for (i = 0; i < VOIP_ITEMS; i++)
	{
		int y = 48 + 8 * i;
		const char *text = NULL;
		float r;
		int idx;

		switch (i)
		{
		case VOIP_SEND:
			text = "              Mode";
			idx = (int)cl_voip_send.value;
			M_Print(178, y, (idx >= 0 && idx < 3) ? sendlabels[idx] : va("%d", idx));
			break;

		case VOIP_PLAYVOL:
			text = "       Play Volume";
			r = cl_voip_play.value;
			M_DrawSlider(186, y, r, 100.f * cl_voip_play.value, "%.0f%%");
			break;

		case VOIP_MICVOL:
			text = "        Mic Volume";
			r = cl_voip_capturingvol.value;
			M_DrawSlider(186, y, r, 100.f * cl_voip_capturingvol.value, "%.0f%%");
			break;

		case VOIP_MICAMP:
			text = "       Mic Amplify";
			r = cl_voip_micamp.value / 8.f;
			M_DrawSlider(186, y, r, cl_voip_micamp.value, "%.2fx");
			break;

		case VOIP_VADTHRESH:
			text = "     VAD Threshold";
			r = cl_voip_vad_threshhold.value / 100.f;
			M_DrawSlider(186, y, r, cl_voip_vad_threshhold.value, "%.0f");
			break;

		case VOIP_VADDELAY:
			text = "         VAD Delay";
			r = cl_voip_vad_delay.value / 5.f;
			M_DrawSlider(186, y, r, cl_voip_vad_delay.value, "%.2fs");
			break;

		case VOIP_DUCKING:
			text = "           Ducking";
			r = cl_voip_ducking.value;
			M_DrawSlider(186, y, r, 100.f * cl_voip_ducking.value, "%.0f%%");
			break;

		case VOIP_NOISEFILTER:
			text = "      Noise Filter";
			M_DrawCheckbox(178, y, cl_voip_noisefilter.value);
			break;

		case VOIP_AUTOGAIN:
			text = "         Auto Gain";
			M_DrawCheckbox(178, y, cl_voip_autogain.value);
			break;

		case VOIP_SHOWMETER:
			text = "        Show Meter";
			idx = (int)cl_voip_showmeter.value;
			M_Print(178, y, (idx >= 0 && idx < 3) ? meterlabels[idx] : va("%d", idx));
			break;

		case VOIP_BITRATE:
			text = "           Bitrate";
			M_Print(178, y, va("%d bps", (int)cl_voip_bitrate.value));
			break;

		case VOIP_TEST:
			text = "          Mic Test";
			M_DrawCheckbox(178, y, cl_voip_test.value);
			break;

		default:
			break;
		}

		if (text)
			M_Print(0, y, text);
	}

	M_DrawCharacter(168, 48 + voip_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

void M_Voip_Key(int k)
{
	if (!keydown[K_MOUSE1])
		voip_slider_grab = false;

	if (voip_slider_grab)
	{
		switch (k)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			voip_slider_grab = false;
			break;
		}
		return;
	}

	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		M_Menu_Sound_f();
		sound_cursor = SOUND_VOIP;
		break;

	case K_MOUSE1:
		m_entersound = true;
		if (m_mousey >= 48 && m_mousey < 48 + (VOIP_ITEMS * 8))
		{
			voip_cursor = (m_mousey - 48) / 8;
			if (voip_cursor == VOIP_PLAYVOL ||
				voip_cursor == VOIP_MICVOL ||
				voip_cursor == VOIP_MICAMP ||
				voip_cursor == VOIP_VADTHRESH ||
				voip_cursor == VOIP_VADDELAY ||
				voip_cursor == VOIP_DUCKING)
			{
				voip_slider_grab = true;
			}
			else
			{
				M_Voip_AdjustSetting(1);
			}
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		M_Voip_AdjustSetting(1);
		break;

	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		if (voip_cursor == 0)
			voip_cursor = VOIP_ITEMS - 1;
		else
			voip_cursor--;
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		voip_cursor++;
		if (voip_cursor >= VOIP_ITEMS)
			voip_cursor = 0;
		break;

	case K_LEFTARROW:
	case K_MWHEELDOWN:
		M_Voip_AdjustSetting(-1);
		break;

	case K_RIGHTARROW:
	case K_MWHEELUP:
		M_Voip_AdjustSetting(1);
		break;
	}
}

void M_Voip_Mousemove(int cx, int cy)
{
	if (voip_slider_grab)
	{
		float f;

		if (!keydown[K_MOUSE1])
		{
			voip_slider_grab = false;
			return;
		}

		f = CLAMP(0.f, M_MouseToSliderFraction(cx - 187), 1.f);

		switch (voip_cursor)
		{
		case VOIP_PLAYVOL:
			Cvar_SetValue("cl_voip_play", f);
			break;
		case VOIP_MICVOL:
			Cvar_SetValue("cl_voip_capturingvol", f);
			break;
		case VOIP_MICAMP:
			Cvar_SetValue("cl_voip_micamp", f * 8.f);
			break;
		case VOIP_VADTHRESH:
			Cvar_SetValue("cl_voip_vad_threshhold", f * 100.f);
			break;
		case VOIP_VADDELAY:
			Cvar_SetValue("cl_voip_vad_delay", f * 5.f);
			break;
		case VOIP_DUCKING:
			Cvar_SetValue("cl_voip_ducking", f);
			break;
		default:
			break;
		}
		return;
	}

	int item = (cy - 48) / 8;
	if (item >= 0 && item < VOIP_ITEMS)
		voip_cursor = item;
}


/*
==================
Game Menu
==================
*/

extern cvar_t cl_rollangle, scr_fov, gl_cshiftpercent, cl_bob, v_kicktime, v_kickroll, v_kickpitch, r_drawviewmodel,
cl_damagehue, w_switch, b_switch, cl_say, cl_r2g, cl_truelightning, cl_beams_polygons, cl_deadbodyfilter, con_mm1mute,
gl_max_size, gl_load24bit, r_player_xray;

enum
{
	ALWAYSRUN_OFF = 0,
	ALWAYSRUN_VANILLA,
	ALWAYSRUN_QUAKESPASM,
	ALWAYSRUN_ITEMS
};

static enum game_e
{
	GAME_ALWAYSRUN,
	GAME_ROLLANGLE,
	GAME_FOV,
	GAME_FLASHES,
	GAME_WEAPONBOB,
	GAME_DAMAGEKICK,
	GAME_DAMAGETINT,     // Added
	GAME_AUTOSWITCH,     // Added
	GAME_CONSOLECHAT,    // Added
	GAME_SWAPROCKETS,    // Added
	GAME_TRUELIGHTNING,  // Added
	GAME_STRAIGHTSHAFT,
	GAME_DEADBODYFILTER, // Added
	GAME_MM1MUTE,        // Added
	GAME_VIEWMODEL,      // Added
	GAME_TEAMCOLOR,  // Added
	GAME_ENEMYCOLOR, // Added
	GAME_CTFMODELSWAP,
	GAME_PLAYERXRAY,
	GAME_TEXTURELESS,
	GAME_COUNT
} game_cursor;

#define GAME_ITEMS (GAME_COUNT)
int numberOfGameItems = GAME_ITEMS;

static struct
{
	int cursor;
	struct {
		char text[32];
		int len;
	} search;
} gamemenu;

static qboolean team_rgb_active;
static qboolean enemy_rgb_active;
static char last_team_color[10];
static char last_enemy_color[10];

enum
{
	PLAYERXRAY_MENU_OFF = 0,
	PLAYERXRAY_MENU_BOTH,
	PLAYERXRAY_MENU_ENEMY,
	PLAYERXRAY_MENU_TEAM
};

enum
{
	PLAYERXRAY_TARGET_BOTH = 0,
	PLAYERXRAY_TARGET_ENEMY,
	PLAYERXRAY_TARGET_TEAM
};

enum
{
	PLAYERXRAY_COLOR_SPLIT = 0,
	PLAYERXRAY_COLOR_MATCH
};

enum
{
	PLAYERXRAY_RENDER_FILL = 0,
	PLAYERXRAY_RENDER_OUTLINE
};

typedef struct
{
	float alpha;
	float distance;
	int target_mode;
	int color_mode;
	int render_mode;
	int max_match_size;
	plcolour_t enemy_color;
	plcolour_t team_color;
} playerxray_settings_t;

static enum playerxray_e
{
	PLAYERXRAY_TARGETS,
	PLAYERXRAY_STYLE,
	PLAYERXRAY_ALPHA,
	PLAYERXRAY_DISTANCE,
	PLAYERXRAY_COLORMODE,
	PLAYERXRAY_ENEMYCOLOR,
	PLAYERXRAY_TEAMCOLOR,
	PLAYERXRAY_MATCHSIZE,
	PLAYERXRAY_COUNT
} playerxray_cursor;

#define PLAYERXRAY_ITEMS (PLAYERXRAY_COUNT)

static qboolean playerxray_slider_grab;
static qboolean playerxray_enemy_rgb_active;
static qboolean playerxray_team_rgb_active;

static plcolour_t M_PlayerXray_ColorFromRGB(byte r, byte g, byte b)
{
	plcolour_t color;

	color.type = 2;
	color.basic = 0;
	color.rgb[0] = r;
	color.rgb[1] = g;
	color.rgb[2] = b;
	return color;
}

static void M_PlayerXray_DefaultColor(qboolean isTeam, plcolour_t *out)
{
	const char *source = isTeam ? gl_teamcolor.string : gl_enemycolor.string;

	if (source && source[0])
	{
		*out = CL_PLColours_Parse(source);
		return;
	}

	*out = isTeam
		? M_PlayerXray_ColorFromRGB(0x00, 0xB7, 0xFF)
		: M_PlayerXray_ColorFromRGB(0xFF, 0x00, 0x00);
}

static qboolean M_PlayerXray_ParseHexColorToken(const char *token, plcolour_t *out)
{
	unsigned int rgb;

	if (q_strncasecmp(token, "0x", 2))
		return false;
	if (sscanf(token + 2, "%x", &rgb) != 1)
		return false;

	*out = M_PlayerXray_ColorFromRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
	return true;
}

static qboolean M_PlayerXray_ParseNamedColorToken(const char *token, const char *key, plcolour_t *out)
{
	const char *eq = strchr(token, '=');
	size_t keylen;

	if (!eq || !eq[1])
		return false;

	keylen = (size_t)(eq - token);
	if (strlen(key) != keylen || q_strncasecmp(token, key, keylen))
		return false;

	return M_PlayerXray_ParseHexColorToken(eq + 1, out);
}

static int M_PlayerXray_ParseTargetToken(const char *token)
{
	const char *value = token;
	const char *eq = strchr(token, '=');
	size_t keylen;

	if (eq && eq[1])
	{
		keylen = (size_t)(eq - token);
		if (!((keylen == 6 && !q_strncasecmp(token, "target", keylen)) ||
			(keylen == 7 && !q_strncasecmp(token, "targets", keylen))))
			return -1;
		value = eq + 1;
	}

	if (!q_strcasecmp(value, "both") ||
		!q_strcasecmp(value, "all") ||
		!q_strcasecmp(value, "players"))
		return PLAYERXRAY_TARGET_BOTH;

	if (!q_strcasecmp(value, "enemy") ||
		!q_strcasecmp(value, "enemies"))
		return PLAYERXRAY_TARGET_ENEMY;

	if (!q_strcasecmp(value, "team") ||
		!q_strcasecmp(value, "teammates") ||
		!q_strcasecmp(value, "ally") ||
		!q_strcasecmp(value, "allies"))
		return PLAYERXRAY_TARGET_TEAM;

	return -1;
}

static int M_PlayerXray_ParseColorModeToken(const char *token)
{
	const char *value = token;
	const char *eq = strchr(token, '=');
	size_t keylen;

	if (eq && eq[1])
	{
		keylen = (size_t)(eq - token);
		if (!((keylen == 5 && !q_strncasecmp(token, "color", keylen)) ||
			(keylen == 6 && !q_strncasecmp(token, "colors", keylen)) ||
			(keylen == 9 && !q_strncasecmp(token, "colormode", keylen))))
			return -1;
		value = eq + 1;
	}

	if (!q_strcasecmp(value, "pcolor") ||
		!q_strcasecmp(value, "pcolors") ||
		!q_strcasecmp(value, "player") ||
		!q_strcasecmp(value, "playercolor") ||
		!q_strcasecmp(value, "playercolors"))
		return PLAYERXRAY_COLOR_MATCH;

	return -1;
}

static int M_PlayerXray_ParseRenderModeToken(const char *token)
{
	const char *value = token;
	const char *eq = strchr(token, '=');
	size_t keylen;

	if (eq && eq[1])
	{
		keylen = (size_t)(eq - token);
		if (!((keylen == 4 && !q_strncasecmp(token, "mode", keylen)) ||
			(keylen == 5 && !q_strncasecmp(token, "style", keylen)) ||
			(keylen == 6 && !q_strncasecmp(token, "render", keylen))))
			return -1;
		value = eq + 1;
	}

	if (!q_strcasecmp(value, "outline") ||
		!q_strcasecmp(value, "outlines") ||
		!q_strcasecmp(value, "ring"))
		return PLAYERXRAY_RENDER_OUTLINE;

	if (!q_strcasecmp(value, "fill") ||
		!q_strcasecmp(value, "filled") ||
		!q_strcasecmp(value, "body") ||
		!q_strcasecmp(value, "solid"))
		return PLAYERXRAY_RENDER_FILL;

	return -1;
}

static int M_PlayerXray_ParseMatchSizeToken(const char *token)
{
	const char *value = token;
	const char *eq = strchr(token, '=');
	size_t keylen;
	qboolean keyed = false;
	char *endptr;
	long parsed;

	if (eq && eq[1])
	{
		keylen = (size_t)(eq - token);
		if (!(keylen == 8 && !q_strncasecmp(token, "gametype", keylen)))
			return -1;
		value = eq + 1;
		keyed = true;
	}

	if (!q_strcasecmp(value, "1v1") || !q_strcasecmp(value, "1on1"))
		return 1;
	if (!q_strcasecmp(value, "2v2") || !q_strcasecmp(value, "2on2"))
		return 2;
	if (!q_strcasecmp(value, "3v3") || !q_strcasecmp(value, "3on3"))
		return 3;
	if (!q_strcasecmp(value, "4v4") || !q_strcasecmp(value, "4on4"))
		return 4;
	if (!q_strcasecmp(value, "5v5") || !q_strcasecmp(value, "5on5"))
		return 5;

	if (!keyed)
		return -1;

	parsed = strtol(value, &endptr, 10);
	if (endptr == value || *endptr != '\0' || parsed < 1 || parsed > 5)
		return -1;

	return (int)parsed;
}

static void M_PlayerXray_GetSettings(playerxray_settings_t *settings)
{
	const char *text = r_player_xray.string;
	qboolean saw_enemy_color = false;
	qboolean saw_team_color = false;
	qboolean saw_base_color = false;
	qboolean saw_alpha = false;
	qboolean saw_distance = false;
	char token[64];
	int consumed = 0;

	memset(settings, 0, sizeof(*settings));
	settings->alpha = 1.0f;
	settings->target_mode = PLAYERXRAY_TARGET_BOTH;
	settings->color_mode = PLAYERXRAY_COLOR_SPLIT;
	settings->render_mode = PLAYERXRAY_RENDER_FILL;
	M_PlayerXray_DefaultColor(false, &settings->enemy_color);
	M_PlayerXray_DefaultColor(true, &settings->team_color);

	if (!text || !text[0])
		return;

	while (sscanf(text, " %63s%n", token, &consumed) == 1)
	{
		int parsed_mode;
		char *endptr;
		float value;
		plcolour_t parsed_color;

		text += consumed;

		if (M_PlayerXray_ParseNamedColorToken(token, "enemycolor", &settings->enemy_color))
		{
			saw_enemy_color = true;
			continue;
		}
		if (M_PlayerXray_ParseNamedColorToken(token, "teamcolor", &settings->team_color))
		{
			saw_team_color = true;
			continue;
		}

		parsed_mode = M_PlayerXray_ParseTargetToken(token);
		if (parsed_mode >= 0)
		{
			settings->target_mode = parsed_mode;
			continue;
		}

		parsed_mode = M_PlayerXray_ParseColorModeToken(token);
		if (parsed_mode >= 0)
		{
			settings->color_mode = parsed_mode;
			continue;
		}

		parsed_mode = M_PlayerXray_ParseRenderModeToken(token);
		if (parsed_mode >= 0)
		{
			settings->render_mode = parsed_mode;
			continue;
		}

		parsed_mode = M_PlayerXray_ParseMatchSizeToken(token);
		if (parsed_mode >= 0)
		{
			settings->max_match_size = parsed_mode;
			continue;
		}

		if (M_PlayerXray_ParseHexColorToken(token, &parsed_color))
		{
			saw_base_color = true;
			if (!saw_enemy_color)
				settings->enemy_color = parsed_color;
			if (!saw_team_color)
				settings->team_color = parsed_color;
			continue;
		}

		value = (float)strtod(token, &endptr);
		if (endptr == token || *endptr != '\0')
			continue;

		if (!saw_alpha && value >= 0.0f && value <= 1.0f)
		{
			settings->alpha = value;
			saw_alpha = true;
		}
		else
		{
			settings->distance = q_max(0.0f, value);
			saw_distance = true;
		}
	}

	settings->alpha = CLAMP(0.0f, settings->alpha, 1.0f);

	if (!saw_distance && (saw_base_color || (saw_alpha && settings->alpha > 0.0f)))
		settings->distance = 4096.0f;

	if (settings->distance <= 0.0f && settings->alpha <= 0.0f)
		settings->alpha = 1.0f;
}

static void M_PlayerXray_ColorToHex(const plcolour_t *color, char *buffer, size_t buffer_size)
{
	plcolour_t temp = *color;
	byte *rgb = CL_PLColours_ToRGB(&temp);

	if (!rgb)
	{
		q_strlcpy(buffer, "0xFF0000", buffer_size);
		return;
	}

	q_snprintf(buffer, buffer_size, "0x%02X%02X%02X", rgb[0], rgb[1], rgb[2]);
}

static int M_PlayerXray_ColorToBasicIndex(const plcolour_t *color)
{
	plcolour_t temp = *color;
	byte *rgb = CL_PLColours_ToRGB(&temp);
	int i;

	if (!rgb)
		return -1;

	for (i = 0; i <= 13; ++i)
	{
		byte *pal = (byte *)&d_8to24table[(i << 4) + 8];
		if (pal[0] == rgb[0] && pal[1] == rgb[1] && pal[2] == rgb[2])
			return i;
	}

	return -1;
}

static const char *M_PlayerXray_ColorValue(const plcolour_t *color, qboolean rgb_active)
{
	static char value[16];
	int basic = M_PlayerXray_ColorToBasicIndex(color);

	if (!rgb_active && basic >= 0)
	{
		q_snprintf(value, sizeof(value), "%d", basic);
		return value;
	}

	M_PlayerXray_ColorToHex(color, value, sizeof(value));
	return value;
}

static int M_PlayerXray_GetMenuTarget(const playerxray_settings_t *settings)
{
	if (settings->distance <= 0.0f)
		return PLAYERXRAY_MENU_OFF;

	switch (settings->target_mode)
	{
	case PLAYERXRAY_TARGET_ENEMY:
		return PLAYERXRAY_MENU_ENEMY;
	case PLAYERXRAY_TARGET_TEAM:
		return PLAYERXRAY_MENU_TEAM;
	case PLAYERXRAY_TARGET_BOTH:
	default:
		return PLAYERXRAY_MENU_BOTH;
	}
}

static const char *M_PlayerXray_TargetLabel(int menu_target)
{
	switch (menu_target)
	{
	case PLAYERXRAY_MENU_OFF:
		return "off";
	case PLAYERXRAY_MENU_ENEMY:
		return "enemy";
	case PLAYERXRAY_MENU_TEAM:
		return "team";
	case PLAYERXRAY_MENU_BOTH:
	default:
		return "both";
	}
}

static const char *M_PlayerXray_ColorModeLabel(int color_mode)
{
	return (color_mode == PLAYERXRAY_COLOR_MATCH) ? "player colors" : "split";
}

static const char *M_PlayerXray_RenderModeLabel(int render_mode)
{
	return (render_mode == PLAYERXRAY_RENDER_OUTLINE) ? "outline" : "body";
}

static const char *M_PlayerXray_MatchSizeLabel(int max_match_size)
{
	switch (max_match_size)
	{
	case 1:
		return "1v1";
	case 2:
		return "up to 2v2";
	case 3:
		return "up to 3v3";
	case 4:
		return "up to 4v4";
	case 5:
		return "5v5+";
	default:
		return "any";
	}
}

static void M_PlayerXray_SetMenuTarget(playerxray_settings_t *settings, int menu_target)
{
	switch (menu_target)
	{
	case PLAYERXRAY_MENU_OFF:
		settings->distance = 0.0f;
		break;

	case PLAYERXRAY_MENU_ENEMY:
		settings->target_mode = PLAYERXRAY_TARGET_ENEMY;
		if (settings->distance <= 0.0f)
			settings->distance = 4096.0f;
		break;

	case PLAYERXRAY_MENU_TEAM:
		settings->target_mode = PLAYERXRAY_TARGET_TEAM;
		if (settings->distance <= 0.0f)
			settings->distance = 4096.0f;
		break;

	case PLAYERXRAY_MENU_BOTH:
	default:
		settings->target_mode = PLAYERXRAY_TARGET_BOTH;
		if (settings->distance <= 0.0f)
			settings->distance = 4096.0f;
		break;
	}
}

static void M_PlayerXray_SetSettings(const playerxray_settings_t *settings)
{
	char value[160];
	char enemy_hex[16];
	char team_hex[16];
	char match_token[24] = "";
	const char *style_token;
	const char *target_token;

	M_PlayerXray_ColorToHex(&settings->enemy_color, enemy_hex, sizeof(enemy_hex));
	M_PlayerXray_ColorToHex(&settings->team_color, team_hex, sizeof(team_hex));

	switch (settings->target_mode)
	{
	case PLAYERXRAY_TARGET_ENEMY:
		target_token = "enemy";
		break;
	case PLAYERXRAY_TARGET_TEAM:
		target_token = "team";
		break;
	case PLAYERXRAY_TARGET_BOTH:
	default:
		target_token = "both";
		break;
	}

	if (settings->max_match_size > 0)
		q_snprintf(match_token, sizeof(match_token), " gametype=%d", settings->max_match_size);

	style_token = (settings->render_mode == PLAYERXRAY_RENDER_OUTLINE) ? " outline" : "";

	q_snprintf(value, sizeof(value), "%.2f %.0f %s%s%s%s enemycolor=%s teamcolor=%s",
		CLAMP(0.0f, settings->alpha, 1.0f),
		q_max(0.0f, settings->distance),
		target_token,
		style_token,
		(settings->color_mode == PLAYERXRAY_COLOR_MATCH) ? " pcolor" : "",
		match_token,
		enemy_hex,
		team_hex);

	Cvar_Set("r_player_xray", value);
}

static void M_PlayerXray_AdjustColor(plcolour_t *color, qboolean *rgb_active, int dir)
{
	if (keydown[K_SHIFT])
	{
		vec3_t hsv;
		plcolour_t temp = *color;

		*rgb_active = true;
		rgbtohsv(CL_PLColours_ToRGB(&temp), hsv);
		hsv[0] += dir / 128.0f;
		hsv[1] = 1.0f;
		hsv[2] = 1.0f;
		*color = M_PlayerXray_ColorFromRGB(0, 0, 0);
		hsvtorgb(hsv[0], hsv[1], hsv[2], color->rgb);
		return;
	}

	*rgb_active = false;
	{
		int basic = M_PlayerXray_ColorToBasicIndex(color);
		if (basic < 0)
			basic = 0;

		basic += dir;
		if (basic < 0)
			basic = 13;
		else if (basic > 13)
			basic = 0;

		color->type = 1;
		color->basic = basic;
	}
}

static const char *M_PlayerXray_SummaryValue(void)
{
	playerxray_settings_t settings;

	M_PlayerXray_GetSettings(&settings);
	return M_PlayerXray_TargetLabel(M_PlayerXray_GetMenuTarget(&settings));
}

static void M_Game_AdjustColor(int dir, qboolean isTeam)
{
	const char* current = isTeam ? gl_teamcolor.string : gl_enemycolor.string;

	// If shift is held, handle RGB color mode
	if (keydown[K_SHIFT])
	{
		if (isTeam)
			team_rgb_active = true;
		else
			enemy_rgb_active = true;

		plcolour_t color = CL_PLColours_Parse(current);
		vec3_t hsv;
		rgbtohsv(color.rgb, hsv);

		hsv[0] += dir / 128.0;
		hsv[1] = 1;
		hsv[2] = 1;
		color.type = 2;
		color.basic = 0;
		hsvtorgb(hsv[0], hsv[1], hsv[2], color.rgb);

		const char* colorStr = CL_PLColours_ToString(color);
		if (isTeam)
		{
			Cvar_Set("gl_teamcolor", colorStr);
			snprintf(last_team_color, sizeof(last_team_color), "%s", colorStr);
		}
		else
		{
			Cvar_Set("gl_enemycolor", colorStr);
			snprintf(last_enemy_color, sizeof(last_enemy_color), "%s", colorStr);
		}
		return;
	}

	// Not in RGB mode
	if (isTeam)
		team_rgb_active = false;
	else
		enemy_rgb_active = false;

	// Handle empty string ("off") case
	if (strcmp(current, "") == 0)
	{
		if (dir > 0)  // Going right from "off" -> 0
		{
			plcolour_t color;
			color.type = 1;
			color.basic = 0;
			const char* colorStr = CL_PLColours_ToString(color);
			if (isTeam)
			{
				Cvar_Set("gl_teamcolor", colorStr);
				snprintf(last_team_color, sizeof(last_team_color), "%s", colorStr);
			}
			else
			{
				Cvar_Set("gl_enemycolor", colorStr);
				snprintf(last_enemy_color, sizeof(last_enemy_color), "%s", colorStr);
			}
		}
		return;
	}

	// Handle numeric colors
	plcolour_t color = CL_PLColours_Parse(current);
	color.type = 1;

	// Calculate new basic color value
	int newBasic = color.basic + dir;

	// Handle cycling
	if (newBasic < 0)  // Going left from 0 -> "off"
	{
		if (isTeam)
			Cvar_Set("gl_teamcolor", "");
		else
			Cvar_Set("gl_enemycolor", "");
		return;
	}
	else if (newBasic > 13)  // Going right from 13 -> 0
	{
		color.basic = 0;
	}
	else  // Normal case
	{
		color.basic = newBasic;
	}

	const char* colorStr = CL_PLColours_ToString(color);
	if (isTeam)
	{
		Cvar_Set("gl_teamcolor", colorStr);
		snprintf(last_team_color, sizeof(last_team_color), "%s", colorStr);
	}
	else
	{
		Cvar_Set("gl_enemycolor", colorStr);
		snprintf(last_enemy_color, sizeof(last_enemy_color), "%s", colorStr);
	}
}

static const char* M_Game_GetItemText(int index)
{
	static char buffer[64];

	switch (index)
	{
	case GAME_ALWAYSRUN:
		return "Always Run";
	case GAME_ROLLANGLE:
		return "Strafe Angle Tilt";
	case GAME_FOV:
		return "Field of View";
	case GAME_FLASHES:
		return "Screen Flashes";
	case GAME_WEAPONBOB:
		return "Weapon Bob";
	case GAME_DAMAGEKICK:
		return "Damage Kick";
	case GAME_DAMAGETINT:
		return "Gun Damage Tint";
	case GAME_AUTOSWITCH:
		return "Gun Auto Switch";
	case GAME_CONSOLECHAT:
		return "Console Chat";
	case GAME_SWAPROCKETS:
		return "R2G Swap Rockets";
	case GAME_TRUELIGHTNING:
		return "True Lightning";
	case GAME_STRAIGHTSHAFT:
		return "Straight Shaft";
	case GAME_DEADBODYFILTER:
		return "Deadbody Filter";
	case GAME_MM1MUTE:
		return "Mute MM1 Chat";
	case GAME_VIEWMODEL:
		return "View Model";
	case GAME_TEAMCOLOR:
		return "Force Team Color";
	case GAME_ENEMYCOLOR:
		return "Force Enemy Color";
	case GAME_CTFMODELSWAP:
		return "3Wave CTF Models";
	case GAME_PLAYERXRAY:
		return "Player Xray";
	case GAME_TEXTURELESS:
		return "Textureless";
	default:
		q_snprintf(buffer, sizeof(buffer), "Unknown Item %d", index);
		return buffer;
	}
}

static void M_Game_UpdateSearch(void)
{
	game_cursor = (enum game_e)M_Menu_UpdateSearchCursor(
		GAME_ITEMS, (int)game_cursor, &numberOfGameItems,
		M_Game_GetItemText, gamemenu.search.text, gamemenu.search.len);
}

static void M_Game_MoveCursor(int delta)
{
	game_cursor = (enum game_e)M_Menu_MoveSearchCursor(
		GAME_ITEMS, numberOfGameItems, (int)game_cursor, delta,
		M_Game_GetItemText, gamemenu.search.text, gamemenu.search.len);
}

static int M_Game_LivePreviewId(void)
{
	switch (game_cursor)
	{
	case GAME_FOV:
		return LP_FOV;
	case GAME_FLASHES:
		return LP_CSHIFT;
	case GAME_DAMAGETINT:
		return LP_DAMAGETINT;
	case GAME_VIEWMODEL:
		return LP_VIEWMODEL;
	default:
		return LP_NONE;
	}
}

void M_Menu_Game_f(void)
{
	key_dest = key_menu;
	m_state = m_game;
	m_entersound = true;
	game_cursor = 0;
	gamemenu.cursor = 0;
	gamemenu.search.len = 0;
	gamemenu.search.text[0] = 0;
	numberOfGameItems = GAME_ITEMS;
	M_LivePreview_Reset();

	IN_UpdateGrabs();
}

static void M_Game_AdjustSliders(int dir)
{
	int curr_alwaysrun, target_alwaysrun;
	float f;
	S_LocalSound("misc/menu3.wav");

	M_LivePreview_WantAndKick (M_Game_LivePreviewId (), 20 + game_cursor * 8);

	switch (game_cursor)
	{

	case GAME_ALWAYSRUN:
		if (cl_alwaysrun.value)
			curr_alwaysrun = ALWAYSRUN_QUAKESPASM;
		else if (cl_forwardspeed.value > 200)
			curr_alwaysrun = ALWAYSRUN_VANILLA;
		else
			curr_alwaysrun = ALWAYSRUN_OFF;

		target_alwaysrun = (ALWAYSRUN_ITEMS + curr_alwaysrun + dir) % ALWAYSRUN_ITEMS;

		if (target_alwaysrun == ALWAYSRUN_VANILLA)
		{
			Cvar_SetValue("cl_alwaysrun", 0);
			Cvar_SetValue("cl_forwardspeed", 400);
			Cvar_SetValue("cl_backspeed", 400);
		}
		else if (target_alwaysrun == ALWAYSRUN_QUAKESPASM)
		{
			Cvar_SetValue("cl_alwaysrun", 1);
			Cvar_SetValue("cl_forwardspeed", 200);
			Cvar_SetValue("cl_backspeed", 200);
		}
		else // ALWAYSRUN_OFF
		{
			Cvar_SetValue("cl_alwaysrun", 0);
			Cvar_SetValue("cl_forwardspeed", 200);
			Cvar_SetValue("cl_backspeed", 200);
		}
		break;

	case GAME_ROLLANGLE:
		Cvar_SetValue("cl_rollangle", !cl_rollangle.value);
		break;

	case GAME_FOV:
		f = scr_fov.value + dir;  // Changed from dir * 5 to just dir
		f = CLAMP(60, f, 130);
		Cvar_SetValue("fov", f);
		break;

	case GAME_FLASHES:
		f = gl_cshiftpercent.value + dir;  // Changed from dir * 10 to just dir
		f = CLAMP(0, f, 100);
		Cvar_SetValue("gl_cshiftpercent", f);
		break;

	case GAME_WEAPONBOB:
		Cvar_SetValue("cl_bob", !cl_bob.value * 0.02);
		break;

	case GAME_DAMAGEKICK:
		if (v_kickroll.value == 0 || v_kickpitch.value == 0) // If off, turn on with defaults
		{
			Cvar_SetValue("v_kicktime", 0.5);
			Cvar_SetValue("v_kickroll", 0.6);
			Cvar_SetValue("v_kickpitch", 0.6);
		}
		else // Turn off
		{
			Cvar_SetValue("v_kicktime", 0);
			Cvar_SetValue("v_kickroll", 0);
			Cvar_SetValue("v_kickpitch", 0);
		}
		break;

	case GAME_DAMAGETINT:
	{
		int current = cl_damagehue.value;
		current = (current + 3 + dir) % 3;  // Cycle through 0,1,2
		Cvar_SetValue("cl_damagehue", current);
	}
	break;

	case GAME_AUTOSWITCH:
	{
		int newval = (w_switch.value == 0) ? 2 : 0;
		Cvar_SetValue("w_switch", newval);
		Cvar_SetValue("b_switch", newval);
	}
	break;

	case GAME_CONSOLECHAT:
	{
		int current = cl_say.value;
		current = (current + 3 + dir) % 3;  // Cycle through 0,1,2
		Cvar_SetValue("cl_say", current);
	}
	break;

	case GAME_SWAPROCKETS:
		Cvar_SetValue("cl_r2g", !cl_r2g.value);
		break;

	case GAME_TRUELIGHTNING:
		f = cl_truelightning.value + dir;
		f = CLAMP(0, f, 100);
		Cvar_SetValue("cl_truelightning", f);
		break;

	case GAME_STRAIGHTSHAFT:
		f = cl_beams_polygons.value + dir * 0.5f;
		f = CLAMP(0.0f, f, 10.0f);
		Cvar_SetValue("cl_beams_polygons", f);
		break;

	case GAME_DEADBODYFILTER:
		Cvar_SetValue("cl_deadbodyfilter", !cl_deadbodyfilter.value);
		break;

	case GAME_MM1MUTE:
		Cvar_SetValue("con_mm1mute", !con_mm1mute.value);
		break;

	case GAME_VIEWMODEL:
		f = r_drawviewmodel.value + dir * 0.1f;  // Change to 0.1 increments
		f = CLAMP(0, f, 1);  // Clamp between 0 and 1
		Cvar_SetValue("r_drawviewmodel", f);
		break;

	case GAME_TEAMCOLOR:
		M_Game_AdjustColor(dir, true);
		break;
	case GAME_ENEMYCOLOR:
		M_Game_AdjustColor(dir, false);
		break;

	case GAME_CTFMODELSWAP:
		Cvar_SetValueQuick(&cl_ctf_pub_modelswap, cl_ctf_pub_modelswap.value ? 0 : 1);
		break;

	case GAME_PLAYERXRAY:
		M_Menu_PlayerXray_f();
		break;

	case GAME_TEXTURELESS:
	{
		qboolean textureless_is_currently_on = (gl_max_size.value == 1.0f);
		float original_load24bit_value = gl_load24bit.value;

		if (textureless_is_currently_on) // Currently ON, user wants to turn OFF
		{
			Cvar_SetValue("gl_max_size", 0.0f); // Primary action: turn Textureless OFF

			if (original_load24bit_value == 2.0f)
			{
				Cvar_SetValue("gl_load24bit", 1.0f);
			}
			// If original_load24bit_value was 0, it remains 0 as per rule.
			// If original_load24bit_value was 1, it remains 1 (no specific rule to change it on OFF).
		}
		else // Currently OFF, user wants to turn ON
		{
			Cvar_SetValue("gl_max_size", 1.0f); // Primary action: turn Textureless ON

			if (original_load24bit_value == 1.0f)
			{
				Cvar_SetValue("gl_load24bit", 2.0f);
			}
			// If original_load24bit_value was 0, it remains 0 as per rule.
			// If original_load24bit_value was 2, it remains 2 (no specific rule to change it on ON if not 0 or 1).
		}
		break;
	}

	case GAME_COUNT:
		break;

	default:
		break;
	}
}

void M_Game_Draw(void)
{
	//qpic_t* p;
	float r;
	enum game_e i;

	game_cursor = (enum game_e)M_Menu_ClampCursorValue((int)game_cursor, GAME_ITEMS);

	//p = Draw_CachePic("gfx/p_option.lmp");
	//M_DrawPic((320 - p->width) / 2, 4, p);

	const char* title = "Game Options";
	M_PrintWhite((320 - 8 * strlen(title)) / 2, 4, title);

	M_LivePreview_WantAt (M_Game_LivePreviewId (), 20 + game_cursor * 8);

	for (i = 0; i < GAME_ITEMS; i++)
	{
		int y = 20 + 8 * i;
		qboolean isolated = M_LivePreview_IsolateY (y);
		const char* text = NULL;
		const char* value = NULL;

		if (isolated)
			M_LivePreview_BeginIsolate ();

		switch (i)
		{

		case GAME_ALWAYSRUN:
			text = "        Always Run";
			if (cl_alwaysrun.value)
				value = "qs/power bunnyhop";
			else if (cl_forwardspeed.value > 200.0)
				value = "traditional";
			else
				value = "off (slow)";
			M_Print(178, y, value);
			break;

		case GAME_ROLLANGLE:
			text = " Strafe Angle Tilt";
			M_DrawCheckbox(178, y, cl_rollangle.value != 0);
			break;

		case GAME_FOV:
			text = "     Field of View";
			r = (scr_fov.value - 60) / 70.0;  // 70 is range (130-60)
			M_DrawSlider(186, y, r, scr_fov.value, "%.0f");
			break;

		case GAME_FLASHES:
			text = "    Screen Flashes";
			r = gl_cshiftpercent.value / 100.0;
			M_DrawSlider(186, y, r, gl_cshiftpercent.value, "%.0f%%");
			break;

		case GAME_WEAPONBOB:
			text = "        Weapon Bob";
			M_DrawCheckbox(178, y, cl_bob.value != 0);
			break;

		case GAME_DAMAGEKICK:
			text = "       Damage Kick";
			M_DrawCheckbox(178, y, (v_kickroll.value != 0 || v_kickpitch.value != 0));
			break;

		case GAME_DAMAGETINT:
			text = "   Damage Gun Tint";
			if (cl_damagehue.value == 0)
				value = "off";
			else if (cl_damagehue.value == 1)
				value = "weapon";
			else
				value = "weapon+crosshair";
			M_Print(178, y, value);
			break;

		case GAME_AUTOSWITCH:
			text = "   Gun Auto Switch";
			M_DrawCheckbox(178, y, w_switch.value != 0);
			break;

		case GAME_CONSOLECHAT:
			text = "      Console Chat";
			if (cl_say.value == 0)
				value = "off";
			else if (cl_say.value == 1)
				value = "console";
			else
				value = "console+space";
			M_Print(178, y, value);
			break;

		case GAME_SWAPROCKETS:
			text = "  R2G Swap Rockets";
			M_DrawCheckbox(178, y, cl_r2g.value != 0);
			break;

		case GAME_TRUELIGHTNING:
			text = "    True Lightning";
			r = cl_truelightning.value / 100.0;
			M_DrawSlider(186, y, r, cl_truelightning.value, "%.0f%%");
			break;

		case GAME_STRAIGHTSHAFT:
			text = "    Straight Shaft";
			r = CLAMP(0.0f, cl_beams_polygons.value, 10.0f) / 10.0f;
			M_DrawSlider(186, y, r, CLAMP(0.0f, cl_beams_polygons.value, 10.0f), "%.1f");
			break;

		case GAME_DEADBODYFILTER:
			text = "   Deadbody Filter";
			M_DrawCheckbox(178, y, cl_deadbodyfilter.value != 0);
			break;
		case GAME_MM1MUTE:
			text = "     Mute MM1 Chat";
			M_DrawCheckbox(178, y, con_mm1mute.value != 0);
			break;

		case GAME_VIEWMODEL:
			text = " Visible Gun Model";
			r = r_drawviewmodel.value;  // Already 0-1, no need to divide
			M_DrawSlider(186, y, r, r_drawviewmodel.value * 100, "%.0f%%");  // Multiply by 100 just for display
			break;

		case GAME_TEAMCOLOR:
			text = "  Force Team Color";
			if (strcmp(gl_teamcolor.string, "") == 0)
				value = "off";
			else if (team_rgb_active)
				value = va("%s", gl_teamcolor.string);
			else
			{
				plcolour_t color = CL_PLColours_Parse(gl_teamcolor.string);
				value = (color.type == 2) ? va("%s", gl_teamcolor.string) : va("%d", color.basic);
			}
			M_Print(178, y, value);
			if (strcmp(gl_teamcolor.string, "") != 0)
				Draw_FillPlayer(178 + (strlen(value) * 8) + 4, y + 2, 6, 6, CL_PLColours_Parse(gl_teamcolor.string), 1.0);
			break;

		case GAME_ENEMYCOLOR:
			text = " Force Enemy Color";
			if (strcmp(gl_enemycolor.string, "") == 0)
				value = "off";
			else if (enemy_rgb_active)
				value = va("%s", gl_enemycolor.string);
			else
			{
				plcolour_t color = CL_PLColours_Parse(gl_enemycolor.string);
				value = (color.type == 2) ? va("%s", gl_enemycolor.string) : va("%d", color.basic);
			}
			M_Print(178, y, value);
			if (strcmp(gl_enemycolor.string, "") != 0)
				Draw_FillPlayer(178 + (strlen(value) * 8) + 4, y + 2, 6, 6, CL_PLColours_Parse(gl_enemycolor.string), 1.0);
			break;

		case GAME_CTFMODELSWAP:
			text = "  3Wave CTF Models";
			M_DrawCheckbox(178, y, cl_ctf_pub_modelswap.value != 0);
			break;

		case GAME_PLAYERXRAY:
			text = "       Player Xray";
			value = M_PlayerXray_SummaryValue();
			M_Print(178, y, value);
			break;

		case GAME_TEXTURELESS:
			text = "       Textureless"; // Adjusted spacing
			M_DrawCheckbox(178, y, gl_max_size.value == 1.0f);
			break;

		default:
			break;
		}

		if (text)
		{
			if (gamemenu.search.len > 0 &&
				q_strcasestr(text, gamemenu.search.text))
			{
				M_PrintHighlight(0, y, text,
					gamemenu.search.text,
					gamemenu.search.len);
			}
			else
			{
				M_Print(0, y, text);
			}
		}

		if (isolated)
			M_LivePreview_EndIsolate ();
	}

	// Draw cursor
	{
		int y = 20 + game_cursor * 8;
		qboolean isolated = M_LivePreview_IsolateY (y);
		if (isolated)
			M_LivePreview_BeginIsolate ();
		M_DrawCharacter(168, y, 12 + ((int)(realtime * 4) & 1));
		if (isolated)
			M_LivePreview_EndIsolate ();
	}

	if (game_cursor == GAME_TEAMCOLOR || game_cursor == GAME_ENEMYCOLOR)
		M_PrintRGBA(74, 176, "+shift for RGB colors", CL_PLColours_Parse("0xffffff"), 0.6f, false);

	// Draw search box if search is active
	if (gamemenu.search.len > 0)
	{
		M_DrawTextBox(16, 170, 32, 1);
		M_PrintHighlight(24, 178, gamemenu.search.text,
			gamemenu.search.text,
			gamemenu.search.len);
		int cursor_x = 24 + 8 * gamemenu.search.len;
		if (numberOfGameItems == 0)
			M_DrawCharacter(cursor_x, 178, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, 178, 10 + ((int)(realtime * 4) & 1));
	}
}

void M_Game_Key(int k)
{
	// Handle slider grab release
	if (!keydown[K_MOUSE1])
		game_slider_grab = false;

	if (game_slider_grab)
	{
		switch (k)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			game_slider_grab = false;
			break;
		}
		return;
	}

	// Handle search functionality first
	if (k == K_ESCAPE)
	{
		if (gamemenu.search.len > 0)
		{
			gamemenu.search.len = 0;
			gamemenu.search.text[0] = 0;
			M_Game_UpdateSearch();
			return;
		}
		if (M_LivePreview_Alpha() > 0.f)
		{
			M_LivePreview_Reset();
			return;
		}
		M_Menu_Options_f();
		return;
	}
	else if (keydown[K_CTRL])
	{
		if ((k == 'u' || k == 'U') && gamemenu.search.len > 0)
		{
			// Clear entire search with Ctrl+U
			gamemenu.search.len = 0;
			gamemenu.search.text[0] = 0;
			M_Game_UpdateSearch();
			return;
		}
		else if (k == K_BACKSPACE && gamemenu.search.len > 0)
		{
			// Delete previous word with Ctrl+Backspace
			listsearch_t temp;
			temp.len = gamemenu.search.len;
			Q_strcpy(temp.text, gamemenu.search.text);
			M_DeletePrevWord(&temp);
			Q_strcpy(gamemenu.search.text, temp.text);
			gamemenu.search.len = temp.len;
			M_Game_UpdateSearch();
			return;
	}
	}
	else if (k == K_BACKSPACE)
	{
		if (gamemenu.search.len > 0)
		{
			gamemenu.search.text[--gamemenu.search.len] = 0;
			M_Game_UpdateSearch();
			return;
		}
	}
	else if (k >= 32 && k < 127)
	{
		if (gamemenu.search.len < sizeof(gamemenu.search.text) - 1)
		{
			gamemenu.search.text[gamemenu.search.len++] = k;
			gamemenu.search.text[gamemenu.search.len] = 0;
			M_Game_UpdateSearch();
			return;
		}
	}

	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		if (M_LivePreview_Alpha() > 0.f)
		{
			M_LivePreview_Reset();
			return;
		}
		M_Menu_Options_f();
		break;

	case K_MOUSE1:
		m_entersound = true;

		// Check if click is in search box area
		if (gamemenu.search.len > 0 && m_mousey >= 170)
			break;

		// Check if click is in valid menu area
		if (m_mousey >= 20 && m_mousey < 20 + (GAME_ITEMS * 8))  // Changed from 48 to 20
		{
			game_cursor = (m_mousey - 20) / 8;  // Changed from 48 to 20

			if (game_cursor == GAME_FOV ||
				game_cursor == GAME_FLASHES ||
				game_cursor == GAME_TRUELIGHTNING ||
				game_cursor == GAME_STRAIGHTSHAFT ||
				game_cursor == GAME_VIEWMODEL)
			{
				game_slider_grab = true;
				M_LivePreview_WantAndKick (M_Game_LivePreviewId (), 20 + game_cursor * 8);
			}
			else
			{
				M_Game_AdjustSliders(1);
			}
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		M_Game_AdjustSliders(1);
		break;

	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		M_Game_MoveCursor(-1);
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		M_Game_MoveCursor(1);
		break;

	case K_LEFTARROW:
		M_Game_AdjustSliders(-1);
		break;

	case K_MWHEELDOWN:
		if (game_cursor != GAME_PLAYERXRAY)
			M_Game_AdjustSliders(-1);
		break;

	case K_RIGHTARROW:
		M_Game_AdjustSliders(1);
		break;

	case K_MWHEELUP:
		if (game_cursor != GAME_PLAYERXRAY)
			M_Game_AdjustSliders(1);
		break;
	}
}

void M_Game_Mousemove(int cx, int cy)
{
	if (game_slider_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			game_slider_grab = false;
			return;
		}

		M_LivePreview_WantAndKick (M_Game_LivePreviewId (), 20 + game_cursor * 8);

		float f;
		switch (game_cursor)
		{
		case GAME_FOV:
			f = 60 + M_MouseToSliderFraction(cx - 187) * 70;  // 70 is range (130-60)
			Cvar_SetValue("fov", CLAMP(60, (int)f, 130));
			break;

		case GAME_FLASHES:
			f = M_MouseToSliderFraction(cx - 187) * 100;
			Cvar_SetValue("gl_cshiftpercent", CLAMP(0, (int)f, 100));
			break;

		case GAME_TRUELIGHTNING:
			f = M_MouseToSliderFraction(cx - 187) * 100;
			Cvar_SetValue("cl_truelightning", CLAMP(0, (int)f, 100));
			break;

		case GAME_STRAIGHTSHAFT:
			f = M_MouseToSliderFraction(cx - 187) * 10.0f;
			f = (int)(f * 10.0f + 0.5f) / 10.0f;
			Cvar_SetValue("cl_beams_polygons", CLAMP(0.0f, f, 10.0f));
			break;

		case GAME_VIEWMODEL:
			f = M_MouseToSliderFraction(cx - 187);  // Already 0-1
			Cvar_SetValue("r_drawviewmodel", CLAMP(0, f, 1));
			break;

			// Add cases for unhandled enumerations
		case GAME_ALWAYSRUN:
		case GAME_ROLLANGLE:
		case GAME_WEAPONBOB:
		case GAME_DAMAGEKICK:
		case GAME_DAMAGETINT:
		case GAME_AUTOSWITCH:
		case GAME_CONSOLECHAT:
		case GAME_SWAPROCKETS:
		case GAME_DEADBODYFILTER:
		case GAME_MM1MUTE:
		case GAME_CTFMODELSWAP:
		case GAME_PLAYERXRAY:
		case GAME_COUNT:
			// No action needed for these cases in mouse movement
			break;

		default:
			// Handle unexpected cases gracefully
			break;
		}
		return;
	}

	// Don't process mouse movement if it's in the search box area
	if (gamemenu.search.len > 0 && cy >= 170)
		return;

	// Calculate which menu item the mouse is over
	int item = (cy - 20) / 8;

	// Make sure the item is within valid range
	if (item >= 0 && item < GAME_ITEMS)
	{
		// Update the cursor position
		game_cursor = item;
	}
}

static void M_PlayerXray_AdjustSetting(int dir)
{
	playerxray_settings_t settings;
	int target;

	M_PlayerXray_GetSettings(&settings);
	S_LocalSound("misc/menu3.wav");

	switch (playerxray_cursor)
	{
	case PLAYERXRAY_TARGETS:
		target = M_PlayerXray_GetMenuTarget(&settings) + dir;
		if (target < PLAYERXRAY_MENU_OFF)
			target = PLAYERXRAY_MENU_TEAM;
		else if (target > PLAYERXRAY_MENU_TEAM)
			target = PLAYERXRAY_MENU_OFF;
		M_PlayerXray_SetMenuTarget(&settings, target);
		break;

	case PLAYERXRAY_STYLE:
		settings.render_mode = (settings.render_mode == PLAYERXRAY_RENDER_OUTLINE)
			? PLAYERXRAY_RENDER_FILL
			: PLAYERXRAY_RENDER_OUTLINE;
		break;

	case PLAYERXRAY_ALPHA:
		settings.alpha = CLAMP(0.0f, settings.alpha + dir * 0.05f, 1.0f);
		break;

	case PLAYERXRAY_DISTANCE:
		settings.distance = CLAMP(0.0f, settings.distance + dir * 256.0f, 8192.0f);
		break;

	case PLAYERXRAY_COLORMODE:
		settings.color_mode = (settings.color_mode == PLAYERXRAY_COLOR_MATCH)
			? PLAYERXRAY_COLOR_SPLIT
			: PLAYERXRAY_COLOR_MATCH;
		break;

	case PLAYERXRAY_ENEMYCOLOR:
		M_PlayerXray_AdjustColor(&settings.enemy_color, &playerxray_enemy_rgb_active, dir);
		break;

	case PLAYERXRAY_TEAMCOLOR:
		M_PlayerXray_AdjustColor(&settings.team_color, &playerxray_team_rgb_active, dir);
		break;

	case PLAYERXRAY_MATCHSIZE:
		settings.max_match_size += dir;
		if (settings.max_match_size < 0)
			settings.max_match_size = 5;
		else if (settings.max_match_size > 5)
			settings.max_match_size = 0;
		break;

	case PLAYERXRAY_COUNT:
	default:
		break;
	}

	M_PlayerXray_SetSettings(&settings);
}

void M_Menu_PlayerXray_f(void)
{
	key_dest = key_menu;
	m_state = m_playerxray;
	m_entersound = true;
	playerxray_cursor = 0;
	playerxray_slider_grab = false;
	playerxray_enemy_rgb_active = false;
	playerxray_team_rgb_active = false;

	IN_UpdateGrabs();
}

void M_PlayerXray_Draw(void)
{
	qpic_t *p;
	playerxray_settings_t settings;
	float r;
	int i;

	M_PlayerXray_GetSettings(&settings);

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	{
		const char *title = "Player Xray";
		M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);
	}

	for (i = 0; i < PLAYERXRAY_ITEMS; ++i)
	{
		int y = 48 + 8 * i;
		const char *text = NULL;
		const char *value = NULL;

		switch (i)
		{
		case PLAYERXRAY_TARGETS:
			text = "         Targets";
			value = M_PlayerXray_TargetLabel(M_PlayerXray_GetMenuTarget(&settings));
			M_Print(178, y, value);
			break;

		case PLAYERXRAY_STYLE:
			text = "           Style";
			M_Print(178, y, M_PlayerXray_RenderModeLabel(settings.render_mode));
			break;

		case PLAYERXRAY_ALPHA:
			text = "         Opacity";
			r = settings.alpha;
			M_DrawSlider(186, y, r, settings.alpha * 100.0f, "%.0f%%");
			break;

		case PLAYERXRAY_DISTANCE:
			text = "           Range";
			r = settings.distance / 8192.0f;
			M_DrawSlider(186, y, r, settings.distance, "%.0f");
			break;

		case PLAYERXRAY_COLORMODE:
			text = "      Color Mode";
			M_Print(178, y, M_PlayerXray_ColorModeLabel(settings.color_mode));
			break;

		case PLAYERXRAY_ENEMYCOLOR:
			text = "     Enemy Color";
			value = M_PlayerXray_ColorValue(&settings.enemy_color, playerxray_enemy_rgb_active);
			M_Print(178, y, value);
			Draw_FillPlayer(178 + (strlen(value) * 8) + 4, y + 2, 6, 6, settings.enemy_color, 1.0f);
			break;

		case PLAYERXRAY_TEAMCOLOR:
			text = "      Team Color";
			value = M_PlayerXray_ColorValue(&settings.team_color, playerxray_team_rgb_active);
			M_Print(178, y, value);
			Draw_FillPlayer(178 + (strlen(value) * 8) + 4, y + 2, 6, 6, settings.team_color, 1.0f);
			break;

		case PLAYERXRAY_MATCHSIZE:
			text = "      Match Size";
			M_Print(178, y, M_PlayerXray_MatchSizeLabel(settings.max_match_size));
			break;

		default:
			break;
		}

		if (text)
			M_Print(16, y, text);
	}

	M_DrawCharacter(168, 48 + playerxray_cursor * 8, 12 + ((int)(realtime * 4) & 1));

	if (playerxray_cursor == PLAYERXRAY_ENEMYCOLOR || playerxray_cursor == PLAYERXRAY_TEAMCOLOR)
		M_PrintRGBA(74, 128, "+shift for RGB colors", CL_PLColours_Parse("0xffffff"), 0.6f, false);
}

void M_PlayerXray_Key(int k)
{
	if (!keydown[K_MOUSE1])
		playerxray_slider_grab = false;

	if (playerxray_slider_grab)
	{
		switch (k)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			playerxray_slider_grab = false;
			break;
		}
		return;
	}

	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		M_Menu_Game_f();
		game_cursor = GAME_PLAYERXRAY;
		break;

	case K_MOUSE1:
		m_entersound = true;
		if (m_mousey >= 48 && m_mousey < 48 + (PLAYERXRAY_ITEMS * 8))
		{
			playerxray_cursor = (m_mousey - 48) / 8;
			if (playerxray_cursor == PLAYERXRAY_ALPHA ||
				playerxray_cursor == PLAYERXRAY_DISTANCE)
			{
				playerxray_slider_grab = true;
			}
			else
			{
				M_PlayerXray_AdjustSetting(1);
			}
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		M_PlayerXray_AdjustSetting(1);
		break;

	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		playerxray_cursor--;
		if (playerxray_cursor < 0)
			playerxray_cursor = PLAYERXRAY_ITEMS - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		playerxray_cursor++;
		if (playerxray_cursor >= PLAYERXRAY_ITEMS)
			playerxray_cursor = 0;
		break;

	case K_LEFTARROW:
	case K_MWHEELDOWN:
		M_PlayerXray_AdjustSetting(-1);
		break;

	case K_RIGHTARROW:
	case K_MWHEELUP:
		M_PlayerXray_AdjustSetting(1);
		break;
	}
}

void M_PlayerXray_Mousemove(int cx, int cy)
{
	if (playerxray_slider_grab)
	{
		playerxray_settings_t settings;
		float f;

		if (!keydown[K_MOUSE1])
		{
			playerxray_slider_grab = false;
			return;
		}

		M_PlayerXray_GetSettings(&settings);

		switch (playerxray_cursor)
		{
		case PLAYERXRAY_ALPHA:
			settings.alpha = CLAMP(0.0f, M_MouseToSliderFraction(cx - 187), 1.0f);
			break;

		case PLAYERXRAY_DISTANCE:
			f = CLAMP(0.0f, M_MouseToSliderFraction(cx - 187), 1.0f) * 8192.0f;
			settings.distance = floorf((f / 128.0f) + 0.5f) * 128.0f;
			break;

		default:
			break;
		}

		M_PlayerXray_SetSettings(&settings);
		return;
	}

	{
		int item = (cy - 48) / 8;
		if (item >= 0 && item < PLAYERXRAY_ITEMS)
			playerxray_cursor = item;
	}
}


/*
==================
HUD Menu
==================
*/

extern cvar_t scr_sbar, scr_showfps, scr_match_hud, scr_matchclock, scr_ping, scr_clock, 
scr_showspeed, scr_sbarfacecolor, scr_showscores, scr_autoid, scr_movekeys, scr_conscale, 
scr_sbaralphaqwammo, scr_obsitems, scr_scoreboard_teamsort;

static enum hud_e
{
	HUD_CROSSHAIR,
	HUD_SCALE,
	HUD_SCRSIZE,
	HUD_SBALPHA,
	HUD_SBARSTYLE,
	HUD_SHOWFPS,
	HUD_MATCHSCORES,
	HUD_MATCHCLOCK,
	HUD_SHOWPING,
	HUD_SHOWCLOCK,
	HUD_SHOWSPEED,
	HUD_SHOWSCORES,
	HUD_AUTOID,
	HUD_MOVEKEYS,
	HUD_CONSOLEFONT,
	HUD_OBSITEMS,
	HUD_SCOREBOARD_SORT,
	HUD_COUNT
} hud_cursor;

#define HUD_ITEMS (HUD_COUNT)
int numberOfHUDItems = HUD_ITEMS;

static struct
{
	int cursor;
	struct {
		char text[32];
		int len;
	} search;
} hudmenu;

float target_hud_scale_frac;

static const char* M_HUD_GetItemText(int index)
{
	static char buffer[64];

	switch (index)
	{
	case HUD_CROSSHAIR:
		return "Crosshair";
	case HUD_SCALE:
		return "HUD Scale";
	case HUD_SCRSIZE:
		return "Screen Size";
	case HUD_SBALPHA:
		return "Statusbar Alpha";
	case HUD_SBARSTYLE:
		return "Status Bar Style";
	case HUD_SHOWFPS:
		return "Show FPS";
	case HUD_MATCHSCORES:
		return "Show Match Scores";
	case HUD_MATCHCLOCK:
		return "Match Clock";
	case HUD_SHOWPING:
		return "Show Ping";
	case HUD_SHOWCLOCK:
		return "Show Clock";
	case HUD_SHOWSPEED:
		return "Show Speed";
	case HUD_SHOWSCORES:
		return "Show Scores";
	case HUD_AUTOID:
		return "Player Auto ID";
	case HUD_MOVEKEYS:
		return "Movement Keys";
	case HUD_CONSOLEFONT:
		return "Console Font Size";
	case HUD_OBSITEMS:
		return "Observer Items";
	case HUD_SCOREBOARD_SORT:
		return "Scoreboard Sort";

	default:
		q_snprintf(buffer, sizeof(buffer), "Unknown Item %d", index);
		return buffer;
	}
}

static int M_HUD_LivePreviewId(void)
{
	switch (hud_cursor)
	{
	case HUD_SCALE:		return LP_HUDSCALE;
	case HUD_SCRSIZE:	return LP_SCRSIZE;
	case HUD_SBALPHA:	return LP_SBALPHA;
	case HUD_SBARSTYLE:	return LP_SBARSTYLE;
	case HUD_MATCHSCORES:	return LP_MATCHSCORES;
	case HUD_SHOWSPEED:	return LP_SHOWSPEED;
	case HUD_SHOWSCORES:	return LP_SHOWSCORES;
	case HUD_MOVEKEYS:	return LP_MOVEKEYS;
	case HUD_CONSOLEFONT:	return LP_CONFONT;
	default:			return LP_NONE;
	}
}

void M_Menu_HUD_f(void)
{
	key_dest = key_menu;
	m_state = m_hud;
	m_entersound = true;
	hud_cursor = 0;
	hudmenu.cursor = 0;
	hudmenu.search.len = 0;
	hudmenu.search.text[0] = 0;
	numberOfHUDItems = HUD_ITEMS;
	hud_slider_grab = false;
	M_LivePreview_Reset();

	IN_UpdateGrabs();
}

static void M_HUD_AdjustSliders(int dir)
{
	float f, l;
	int value;
	S_LocalSound("misc/menu3.wav");

	M_LivePreview_WantAndKick (M_HUD_LivePreviewId (), 48 + hud_cursor * 8);

	switch (hud_cursor)
	{
	case HUD_SCALE:
		l = ((vid.width + 31) / 32) / 10.0;
		f = scr_sbarscale.value + dir * .1;
		if (f < 1) f = 1;
		else if (f > l) f = l;
		Cvar_SetValue("scr_sbarscale", f);  // Only adjust sbar scale
		break;

	case HUD_SCRSIZE:
		f = scr_viewsize.value + dir * 10;
		if (f > 130) f = 130;
		else if (f < 30) f = 30;
		Cvar_SetValue("viewsize", f);
		break;

	case HUD_SBALPHA:
		f = scr_sbaralpha.value + dir * 0.05;
		if (f < 0) f = 0;
		else if (f > 1) f = 1;
		Cvar_SetValue("scr_sbaralpha", f);
		break;
	case HUD_SBARSTYLE:
		value = scr_sbar.value + dir;
		if (value > 3) value = 1;
		if (value < 1) value = 3;
		Cvar_SetValue("scr_sbar", value);
		break;

	case HUD_SHOWFPS:
		Cvar_SetValue("scr_showfps", !scr_showfps.value);
		break;

	case HUD_MATCHSCORES:
		Cvar_SetValue("scr_match_hud", !scr_match_hud.value);
		break;

	case HUD_MATCHCLOCK:
		Cvar_SetValue("scr_matchclock", !scr_matchclock.value);
		break;

	case HUD_SHOWPING:
		Cvar_SetValue("scr_ping", !scr_ping.value);
		break;

	case HUD_SHOWCLOCK:
		value = scr_clock.value + dir;
		if (value > 8) value = 0;  // Changed from 4 to 8
		if (value < 0) value = 8;  // Changed from 4 to 8
		Cvar_SetValue("scr_clock", value);
		break;

	case HUD_SHOWSPEED:
		value = scr_showspeed.value + dir;
		if (value > 2) value = 0;
		if (value < 0) value = 2;
		Cvar_SetValue("scr_showspeed", value);
		break;

	case HUD_SHOWSCORES:
		Cvar_SetValue("scr_showscores", !scr_showscores.value);
		break;

	case HUD_AUTOID:
		value = scr_autoid.value + dir;
		if (value > 2) value = 0;
		if (value < 0) value = 2;
		Cvar_SetValue("scr_autoid", value);
		break;

	case HUD_MOVEKEYS:
		Cvar_SetValue("scr_movekeys", !scr_movekeys.value);
		break;

	case HUD_CONSOLEFONT:
		f = scr_conscale.value + dir * 0.5;
		if (f < 1) f = 1;
		else if (f > M_ConsoleScaleMax()) f = M_ConsoleScaleMax();
		Cvar_SetValue("scr_conscale", f);
		break;

	case HUD_OBSITEMS:
		Cvar_SetValue("scr_obsitems", !scr_obsitems.value);
		break;

	case HUD_SCOREBOARD_SORT:
		value = scr_scoreboard_teamsort.value + dir;
		if (value > 1) value = 0;
		if (value < 0) value = 1;
		Cvar_SetValue("scr_scoreboard_teamsort", value);
		break;

	default:
		break;
	}
}

void M_HUD_Draw(void)
{
	qpic_t* p;
	float r, l;
	const char* value;

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	const char* title = "HUD Options";
	M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);

	M_LivePreview_WantAt (M_HUD_LivePreviewId (), 48 + hud_cursor * 8);

	for (int i = 0; i < HUD_ITEMS; i++)
	{
		int y = 48 + 8 * i;
		qboolean isolated = M_LivePreview_IsolateY (y);
		const char* text = NULL;

		if (isolated)
			M_LivePreview_BeginIsolate ();

		switch (i)
		{
		case HUD_CROSSHAIR:
			text = "         Crosshair";
			M_Print(178, y-2, "...");
			break;
		case HUD_SCALE:
			text = "         HUD Scale";
			l = (vid.width / 320.0) - 1;
			r = l > 0 ? (scr_sbarscale.value - 1) / l : 0;  // Changed from conscale to sbarscale
			if (hud_slider_grab && hud_cursor == HUD_SCALE)
				r = target_hud_scale_frac;
			M_DrawSlider(186, y, r, scr_sbarscale.value, "%.1f");  // Changed from conscale to sbarscale
			break;

		case HUD_SCRSIZE:
			text = "       Screen Size";
			r = (scr_viewsize.value - 30) / (130 - 30);
			M_DrawSlider(186, y, r, scr_viewsize.value, "%.0f");
			break;

		case HUD_SBALPHA:
			text = "   Statusbar Alpha";
			r = scr_sbaralpha.value;
			M_DrawSlider(186, y, r, 100.0f * r, "%.0f%%");
			break;

		case HUD_SBARSTYLE:
			text = "  Status Bar Style";
			switch ((int)scr_sbar.value)
			{
			case 1: value = "classic"; break;
			case 2: value = "quakeworld"; break;
			case 3: value = "modern/remaster"; break;
			default: value = "unknown"; break;
			}
			M_Print(178, y, value);
			break;

		case HUD_SHOWFPS:
			text = "          Show FPS";
			M_DrawCheckbox(178, y, scr_showfps.value);
			break;

		case HUD_MATCHSCORES:
			text = " Show Match Scores";
			M_DrawCheckbox(178, y, scr_match_hud.value);
			break;

		case HUD_MATCHCLOCK:
			text = "       Match Clock";
			M_DrawCheckbox(178, y, scr_matchclock.value);
			break;

		case HUD_SHOWPING:
			text = "         Show Ping";
			M_DrawCheckbox(178, y, scr_ping.value);
			break;

		case HUD_SHOWCLOCK:
			text = "        Show Clock";
			switch ((int)scr_clock.value)
			{
			case 0: value = "off"; break;
			case 1: value = "level time"; break;
			case 2: value = "12hr clock"; break;
			case 3: value = "24hr clock"; break;
			case 4: value = "date only"; break;
			case 5: value = "date + 12hr"; break;
			case 6: value = "date + 24hr"; break;
			case 7: value = "score/12hr"; break;
			case 8: value = "score/24hr"; break;
			default: value = "unknown"; break;
			}
			M_Print(178, y, value);
			break;

		case HUD_SHOWSPEED:
			text = "        Show Speed";
			switch ((int)scr_showspeed.value)
			{
			case 0: value = "off"; break;
			case 1: value = "numbers"; break;
			case 2: value = "visual meter"; break;
			default: value = "unknown"; break;
			}
			M_Print(178, y, value);
			break;

		case HUD_SHOWSCORES:
			text = "       Show Scores";
			M_DrawCheckbox(178, y, scr_showscores.value);
			break;

		case HUD_AUTOID:
			text = "    Player Auto ID";
			switch ((int)scr_autoid.value)
			{
			case 0: value = "off"; break;
			case 1: value = "on"; break;
			case 2: value = "on+prewar+pmode"; break;
			default: value = "Unknown"; break;
			}
			M_Print(178, y, value);
			break;

		case HUD_MOVEKEYS:
			text = "     Movement Keys";
			M_DrawCheckbox(178, y, scr_movekeys.value);
			break;

		case HUD_CONSOLEFONT:
			text = " Console Font Size";
			r = M_ConsoleScaleFraction(scr_conscale.value);
			M_DrawSlider(186, y, r, scr_conscale.value, "%.1f");
			break;

		case HUD_OBSITEMS:
			text = "    Observer Items";
			M_DrawCheckbox(178, y, scr_obsitems.value);
			break;

		case HUD_SCOREBOARD_SORT:
			text = "   Scoreboard Sort";
			switch ((int)scr_scoreboard_teamsort.value)
			{
			case 0: value = "frag totals"; break;
			case 1: value = "teams totals"; break;
			default: value = "frag totals"; break;
			}
			M_Print(178, y, value);
			break;

		}

		if (text)
		{
			if (hudmenu.search.len > 0 &&
				q_strcasestr(text, hudmenu.search.text))
			{
				M_PrintHighlight(0, y, text,
					hudmenu.search.text,
					hudmenu.search.len);
			}
			else
			{
				M_Print(0, y, text);
			}
		}

		if (isolated)
			M_LivePreview_EndIsolate ();
	}

	// Draw search box if active
	if (hudmenu.search.len > 0)
	{
		M_DrawTextBox(16, 174, 32, 1);
		M_PrintHighlight(24, 182, hudmenu.search.text,
			hudmenu.search.text,
			hudmenu.search.len);
		int cursor_x = 24 + 8 * hudmenu.search.len;
		if (numberOfHUDItems == 0)
			M_DrawCharacter(cursor_x, 182, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, 182, 10 + ((int)(realtime * 4) & 1));
	}

	// Draw cursor
	{
		int y = 48 + hud_cursor * 8;
		qboolean isolated = M_LivePreview_IsolateY (y);
		if (isolated)
			M_LivePreview_BeginIsolate ();
		M_DrawCharacter(168, y, 12 + ((int)(realtime * 4) & 1));
		if (isolated)
			M_LivePreview_EndIsolate ();
	}
}

void M_HUD_Key(int k)
{
	// Handle slider grab release
	if (!keydown[K_MOUSE1])
		hud_slider_grab = false;

	if (hud_slider_grab)
	{
		switch (k)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			hud_slider_grab = false;
			break;
		}
		return;
	}

	// Handle search functionality first
	if (k == K_ESCAPE)
	{
		if (hudmenu.search.len > 0)
		{
			hudmenu.search.len = 0;
			hudmenu.search.text[0] = 0;
			numberOfHUDItems = HUD_ITEMS;
			return;
		}
		if (M_LivePreview_Alpha() > 0.f)
		{
			M_LivePreview_Reset();
			return;
		}
		M_Menu_Options_f();
		return;
	}
	else if (keydown[K_CTRL])
	{
		if ((k == 'u' || k == 'U') && hudmenu.search.len > 0)
		{
			// Clear entire search with Ctrl+U
			hudmenu.search.len = 0;
			hudmenu.search.text[0] = 0;
			numberOfHUDItems = HUD_ITEMS;
			return;
		}
		else if (k == K_BACKSPACE && hudmenu.search.len > 0)
		{
			// Delete previous word with Ctrl+Backspace
			listsearch_t temp;
			temp.len = hudmenu.search.len;
			Q_strcpy(temp.text, hudmenu.search.text);
			M_DeletePrevWord(&temp);
			Q_strcpy(hudmenu.search.text, temp.text);
			hudmenu.search.len = temp.len;

			// Update filtering based on new search text
			if (hudmenu.search.len > 0)
			{
				numberOfHUDItems = 0;
				for (int i = 0; i < HUD_ITEMS; i++)
				{
					const char* itemtext = M_HUD_GetItemText(i);
					if (itemtext && q_strcasestr(itemtext, hudmenu.search.text))
					{
						numberOfHUDItems++;
						if (numberOfHUDItems == 1)
							hud_cursor = i;
					}
				}
			}
			else
			{
				numberOfHUDItems = HUD_ITEMS;
			}
			return;
		}
	}
	else if (k == K_BACKSPACE)
	{
		if (hudmenu.search.len > 0)
		{
			hudmenu.search.text[--hudmenu.search.len] = 0;
			if (hudmenu.search.len > 0)
			{
				numberOfHUDItems = 0;
				for (int i = 0; i < HUD_ITEMS; i++)
				{
					const char* itemtext = M_HUD_GetItemText(i);
					if (itemtext && q_strcasestr(itemtext, hudmenu.search.text))
					{
						numberOfHUDItems++;
						if (numberOfHUDItems == 1)
							hud_cursor = i;
					}
				}
			}
			else
			{
				numberOfHUDItems = HUD_ITEMS;
			}
			return;
		}
	}
	else if (k >= 32 && k < 127)
	{
		if (hudmenu.search.len < sizeof(hudmenu.search.text) - 1)
		{
			hudmenu.search.text[hudmenu.search.len++] = k;
			hudmenu.search.text[hudmenu.search.len] = 0;

			numberOfHUDItems = 0;
			for (int i = 0; i < HUD_ITEMS; i++)
			{
				const char* itemtext = M_HUD_GetItemText(i);
				if (itemtext && q_strcasestr(itemtext, hudmenu.search.text))
				{
					numberOfHUDItems++;
					if (numberOfHUDItems == 1)
						hud_cursor = i;
				}
			}
			return;
		}
	}

	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		if (M_LivePreview_Alpha() > 0.f)
		{
			M_LivePreview_Reset();
			return;
		}
		M_Menu_Options_f();
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		switch (hud_cursor)
		{
		case HUD_CROSSHAIR:
			M_Menu_Crosshair_f();
			break;
		case HUD_SBARSTYLE:
			M_HUD_AdjustSliders(1);
			break;
		case HUD_SHOWFPS:
			Cvar_SetValue("scr_showfps", !scr_showfps.value);
			break;
		case HUD_MATCHSCORES:
			M_LivePreview_WantAndKick (M_HUD_LivePreviewId (), 48 + hud_cursor * 8);
			Cvar_SetValue("scr_match_hud", !scr_match_hud.value);
			break;
		case HUD_MATCHCLOCK:
			Cvar_SetValue("scr_matchclock", !scr_matchclock.value);
			break;
		case HUD_SHOWPING:
			Cvar_SetValue("scr_ping", !scr_ping.value);
			break;
		case HUD_SHOWCLOCK:
		{
			int value = scr_clock.value + 1;
			if (value > 8) value = 0;
			Cvar_SetValue("scr_clock", value);
			break;
		}
		case HUD_OBSITEMS:
			Cvar_SetValue("scr_obsitems", !scr_obsitems.value);
			break;
		case HUD_SHOWSCORES:
			M_LivePreview_WantAndKick (M_HUD_LivePreviewId (), 48 + hud_cursor * 8);
			Cvar_SetValue("scr_showscores", !scr_showscores.value);
			break;
		case HUD_SCOREBOARD_SORT:
		{
			int val = scr_scoreboard_teamsort.value + 1;
			if (val > 1) val = 0;
			Cvar_SetValue("scr_scoreboard_teamsort", val);
		}
		break;
		default:
			M_HUD_AdjustSliders(1);
			break;
		}
		break;

	case K_MOUSE1:
		m_entersound = true;

		// Check if click is in search box area
		if (hudmenu.search.len > 0 && m_mousey >= 170)
			break;

		// Check if click is in valid menu area
		if (m_mousey >= 48 && m_mousey < 48 + (HUD_ITEMS * 8))
		{
			hud_cursor = (m_mousey - 48) / 8;

			if (hud_cursor == HUD_CROSSHAIR)
			{
				M_Menu_Crosshair_f();
				break;
			}

			if (hud_cursor == HUD_SCALE ||
				hud_cursor == HUD_SCRSIZE ||
				hud_cursor == HUD_SBALPHA ||
				hud_cursor == HUD_CONSOLEFONT)
			{
				hud_slider_grab = true;
				M_LivePreview_WantAndKick (M_HUD_LivePreviewId (), 48 + hud_cursor * 8);
			}
			else if (hud_cursor == HUD_SBARSTYLE)
			{
				M_HUD_AdjustSliders(1);
			}
			else if (hud_cursor == HUD_SHOWFPS)
			{
				Cvar_SetValue("scr_showfps", !scr_showfps.value);
			}
			else if (hud_cursor == HUD_MATCHSCORES)
			{
				M_LivePreview_WantAndKick (M_HUD_LivePreviewId (), 48 + hud_cursor * 8);
				Cvar_SetValue("scr_match_hud", !scr_match_hud.value);
			}
			else if (hud_cursor == HUD_MATCHCLOCK)
			{
				Cvar_SetValue("scr_matchclock", !scr_matchclock.value);
			}
			else if (hud_cursor == HUD_SHOWPING)
			{
				Cvar_SetValue("scr_ping", !scr_ping.value);
			}
			else if (hud_cursor == HUD_SHOWCLOCK)
			{
				int value = scr_clock.value + 1;
				if (value > 8) value = 0;
				Cvar_SetValue("scr_clock", value);
			}
			else if (hud_cursor == HUD_OBSITEMS)
			{
				Cvar_SetValue("scr_obsitems", !scr_obsitems.value);
			}
			else if (hud_cursor == HUD_SCOREBOARD_SORT)
			{
				int val = scr_scoreboard_teamsort.value + 1;
				if (val > 1) val = 0;
				Cvar_SetValue("scr_scoreboard_teamsort", val);
			}
			else if (hud_cursor == HUD_SHOWSPEED)
			{
				int value = scr_showspeed.value + 1;
				M_LivePreview_WantAndKick (M_HUD_LivePreviewId (), 48 + hud_cursor * 8);
				if (value > 2) value = 0;
				Cvar_SetValue("scr_showspeed", value);
			}
			else if (hud_cursor == HUD_SHOWSCORES)
			{
				M_LivePreview_WantAndKick (M_HUD_LivePreviewId (), 48 + hud_cursor * 8);
				Cvar_SetValue("scr_showscores", !scr_showscores.value);
			}
			else if (hud_cursor == HUD_AUTOID)
			{
				int value = scr_autoid.value + 1;
				if (value > 2) value = 0;
				Cvar_SetValue("scr_autoid", value);
			}
			else if (hud_cursor == HUD_MOVEKEYS)
			{
				M_LivePreview_WantAndKick (M_HUD_LivePreviewId (), 48 + hud_cursor * 8);
				Cvar_SetValue("scr_movekeys", !scr_movekeys.value);
			}
			else
			{
				M_HUD_AdjustSliders(1);
			}
		}
		break;

	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		if (hud_cursor <= 0)
			hud_cursor = numberOfHUDItems - 1;
		else
			hud_cursor--;
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		hud_cursor++;
		if (hud_cursor >= numberOfHUDItems)
			hud_cursor = 0;
		break;

	case K_LEFTARROW:
		M_HUD_AdjustSliders(-1);
		break;

	case K_MWHEELDOWN:
		if (hud_cursor != HUD_CROSSHAIR)
			M_HUD_AdjustSliders(-1);
		break;

	case K_RIGHTARROW:
		M_HUD_AdjustSliders(1);
		break;

	case K_MWHEELUP:
		if (hud_cursor != HUD_CROSSHAIR)
			M_HUD_AdjustSliders(1);
		break;
	}
}

void M_HUD_Mousemove(int cx, int cy)
{
	if (hud_slider_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			hud_slider_grab = false;
			return;
		}

		M_LivePreview_WantAndKick (M_HUD_LivePreviewId (), 48 + hud_cursor * 8);

		float f, l;
		switch (hud_cursor)
		{
		case HUD_SCALE:
			target_hud_scale_frac = M_MouseToSliderFraction(cx - 187);
			l = (vid.width / 320.0) - 1;
			f = l > 0 ? target_hud_scale_frac * l + 1 : 1;
			Cvar_SetValue("scr_sbarscale", f);
			break;

		case HUD_SCRSIZE:
			f = M_MouseToSliderFraction(cx - 187);
			f = f * (130 - 30) + 30;
			if (f >= 100)
				f = floor(f / 10 + 0.5) * 10;
			Cvar_SetValue("viewsize", f);
			break;

		case HUD_SBALPHA:
			f = M_MouseToSliderFraction(cx - 187);
			Cvar_SetValue("scr_sbaralpha", f);
			break;

		case HUD_CONSOLEFONT:
			f = M_MouseToSliderFraction(cx - 187);
			f = M_ConsoleScaleFromFraction(f);
			Cvar_SetValue("scr_conscale", f);
			break;

			// Add cases for unhandled enumerations
		case HUD_SBARSTYLE:
		case HUD_SHOWFPS:
		case HUD_MATCHSCORES:
		case HUD_MATCHCLOCK:
		case HUD_SHOWPING:
		case HUD_SHOWCLOCK:
		case HUD_SHOWSPEED:
		case HUD_SHOWSCORES:
		case HUD_AUTOID:
		case HUD_MOVEKEYS:
		case HUD_OBSITEMS:
		case HUD_SCOREBOARD_SORT:
		case HUD_COUNT:
			// No action needed for these cases in mouse movement
			break;

		default:
			// Handle unexpected cases gracefully
			break;
		}
		return;
	}

	// Don't process mouse movement if it's in the search box area
	if (hudmenu.search.len > 0 && cy >= 170)
		return;

	// Calculate which menu item the mouse is over
	int item = (cy - 48) / 8;

	// Make sure the item is within valid range
	if (item >= 0 && item < HUD_ITEMS)
	{
		// Update the cursor position
		hud_cursor = item;
	}
}

/*
==================
Crosshair Menu
==================
*/

qboolean crosshair_menu;

extern cvar_t scr_crosshairalpha, scr_crosshaircolor, scr_crosshairoutline, scr_crosshairscale, crosshair,
scr_crosshair_x, scr_crosshair_y;

static enum crosshair_e
{
	CROSSHAIR_TOGGLE,
	CROSSHAIR_ALPHA,
	CROSSHAIR_COLOR,
	CROSSHAIR_COLOR_PICKER,
	CROSSHAIR_OUTLINE,
	CROSSHAIR_SCALE,
	CROSSHAIR_X,
	CROSSHAIR_Y,
	CROSSHAIR_COUNT
} crosshair_cursor;

#define CROSSHAIR_ITEMS (CROSSHAIR_COUNT)
int numberOfCrosshairItems = CROSSHAIR_ITEMS;

static struct
{
	int cursor;
	struct {
		char text[32];
		int len;
	} search;
} crosshairmenu;


void renderCircle(float cx, float cy, float r, int num_segments, float line_width);
void renderSmoothDot(float cx, float cy, float size);

void M_DrawMenuCrosshair(int x, int y)
{
	float base_scale = CLAMP(1.0f, scr_crosshairscale.value, 10.0f);
	float menu_scale = q_min((float)glwidth / 320.0f, (float)glheight / 200.0f);
	menu_scale = CLAMP(1.0f, scr_menuscale.value, menu_scale);

	// Adjust scale to match viewport
	float s = (base_scale / menu_scale) / 1.0f;

	plcolour_t color;
	if (strcmp(scr_crosshaircolor.string, "") == 0)
		color = CL_PLColours_Parse("0xffffff");
	else
		color = CL_PLColours_Parse(scr_crosshaircolor.string);

	plcolour_t outline = CL_PLColours_Parse("0x000000");
	float alpha = scr_crosshairalpha.value;

	// Save current GL state
	glPushAttrib(GL_ALL_ATTRIB_BITS);

	// Set up scaling matrix for all crosshairs
	glPushMatrix();
	glTranslatef(x, y, 0);
	glScalef(s, s, 1.0);

	// Regular crosshairs 1-5

	if (crosshair.value == 1)
		Draw_CharacterRGBA(-4, -4, '+', color, alpha);

	if (crosshair.value == 2)
	{
		if (scr_crosshairoutline.value)
			Draw_FillPlayer(-2, -2, 4, 4, outline, alpha);
		Draw_FillPlayer(-1, -1, 2, 2, color, alpha);
	}

	if (crosshair.value == 3)
	{
		if (scr_crosshairoutline.value)
		{
			Draw_FillPlayer(-2, 5, 4, 12, outline, alpha);
			Draw_FillPlayer(-17, -2, 12, 4, outline, alpha);
			Draw_FillPlayer(5, -2, 12, 4, outline, alpha);
			Draw_FillPlayer(-2, -17, 4, 12, outline, alpha);
		}
		Draw_FillPlayer(-1, 6, 2, 10, color, alpha);
		Draw_FillPlayer(-16, -1, 10, 2, color, alpha);
		Draw_FillPlayer(6, -1, 10, 2, color, alpha);
		Draw_FillPlayer(-1, -16, 2, 10, color, alpha);
	}

	if (crosshair.value == 4)
	{
		if (scr_crosshairoutline.value)
		{
			Draw_FillPlayer(-2, -10, 4, 20, outline, alpha);
			Draw_FillPlayer(-10, -2, 20, 4, outline, alpha);
		}
		Draw_FillPlayer(-1, -9, 2, 18, color, alpha);
		Draw_FillPlayer(-9, -1, 18, 2, color, alpha);
	}

	if (crosshair.value == 5)
	{
		if (scr_crosshairoutline.value)
		{
			Draw_FillPlayer(-3, -10, 6, 20, outline, alpha);
			Draw_FillPlayer(-10, -3, 20, 6, outline, alpha);
		}
		Draw_FillPlayer(-2, -9, 4, 18, color, alpha);
		Draw_FillPlayer(-9, -2, 18, 4, color, alpha);
	}

	if (crosshair.value >= 6)
	{
		glDisable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);        // for alpha
		glDisable(GL_ALPHA_TEST);  // for alpha

		float r, g, b;
		float ro, go, bo;

		// --- Handle 'color' (main crosshair color), respecting type like in Draw_FillPlayer() ---
		if (color.type == 2)
		{
			// Already an RGB color
			r = color.rgb[0] / 255.0f;
			g = color.rgb[1] / 255.0f;
			b = color.rgb[2] / 255.0f;
		}
		else
		{
			// Basic color index
			byte* pal = (byte*)&d_8to24table[(color.basic << 4) + 8];
			r = pal[0] / 255.0f;
			g = pal[1] / 255.0f;
			b = pal[2] / 255.0f;
		}

		// --- Handle 'outline' color, same approach ---
		if (outline.type == 2)
		{
			ro = outline.rgb[0] / 255.0f;
			go = outline.rgb[1] / 255.0f;
			bo = outline.rgb[2] / 255.0f;
		}
		else
		{
			byte* pal = (byte*)&d_8to24table[(outline.basic << 4) + 8];
			ro = pal[0] / 255.0f;
			go = pal[1] / 255.0f;
			bo = pal[2] / 255.0f;
		}

		float dotSize = 3.0f * (s * 4);
		float outlineWidth = 4.0f;
		float outlineSize = dotSize + outlineWidth;
		float scaledLineWidth = s * 4 * 1.9f;

		// Crosshair #6: a smooth circle "dot"
		if (crosshair.value == 6)
		{
			if (scr_crosshairoutline.value)
			{
				// Outline first
				glColor4f(ro, go, bo, alpha);
				renderSmoothDot(0.0f, 0.0f, outlineSize);
			}
			// Main fill
			glColor4f(r, g, b, alpha);
			renderSmoothDot(0.0f, 0.0f, dotSize);
		}
		// Crosshair #7: a circle ring plus center dot
		else if (crosshair.value == 7)
		{
			// The circle ring is translucent
			glColor4f(r, g, b, alpha / 12);
			renderCircle(0.0f, 0.0f, 10.0f, 200, scaledLineWidth);

			if (scr_crosshairoutline.value)
			{
				// Outline first
				glColor4f(ro, go, bo, 1.0f);
				renderSmoothDot(0.0f, 0.0f, outlineSize);
			}
			// Main fill
			glColor4f(r, g, b, 1.0f);
			renderSmoothDot(0.0f, 0.0f, dotSize);
		}

		glDisable(GL_BLEND);
		glEnable(GL_ALPHA_TEST);
		glEnable(GL_TEXTURE_2D);
	}

	// Restore matrix and GL state
	glPopMatrix();
	glPopAttrib();
}

static plcolour_t Tools_ColorFromRGB(byte r, byte g, byte b);
static void Tools_SetColorFromRGB(byte r, byte g, byte b);

static qboolean crosshair_rgb_active;
static char last_crosshair_color[10];

static void M_Crosshair_AdjustColor(int dir)
{
	if (keydown[K_SHIFT])
	{
		crosshair_rgb_active = true;
		plcolour_t color = CL_PLColours_Parse(scr_crosshaircolor.string);
		vec3_t hsv;
		rgbtohsv(color.rgb, hsv);  // Remove ToRGB call, use rgb directly

		hsv[0] += dir / 128.0;
		hsv[1] = 1;
		hsv[2] = 1;
		color.type = 2;
		color.basic = 0;
		hsvtorgb(hsv[0], hsv[1], hsv[2], color.rgb);

		const char* colorStr = CL_PLColours_ToString(color);  // Pass color directly, not pointer
		Cvar_Set("scr_crosshaircolor", colorStr);
		snprintf(last_crosshair_color, sizeof(last_crosshair_color), "%s", colorStr); // Safely copy
	}
	else
	{
		crosshair_rgb_active = false;
		plcolour_t color = CL_PLColours_Parse(scr_crosshaircolor.string);
		color.type = 1;

		if (color.basic + dir < 0)
			color.basic = 13;
		else if (color.basic + dir > 13)
			color.basic = 0;
		else
			color.basic += dir;

		const char* colorStr = CL_PLColours_ToString(color);  // Pass color directly, not pointer
		Cvar_Set("scr_crosshaircolor", colorStr);
		snprintf(last_crosshair_color, sizeof(last_crosshair_color), "%s", colorStr); // Safely copy
	}
}

static qboolean crosshair_slider_grab;

static const char* M_Crosshair_GetItemText(int index)
{
	static char buffer[64];

	switch (index)
	{
	case CROSSHAIR_TOGGLE:
		return "Use Crosshair";
	case CROSSHAIR_ALPHA:
		return "Crosshair Alpha";
	case CROSSHAIR_OUTLINE:
		return "Crosshair Outline";
	case CROSSHAIR_SCALE:
		return "Crosshair Scale";
	case CROSSHAIR_X:
		return "Horizontal (X) Adjustment";
	case CROSSHAIR_Y:
		return "Vertical (Y) Adjustment";
	default:
		q_snprintf(buffer, sizeof(buffer), "Unknown Item %d", index);
		return buffer;
	}
}

void M_Menu_Crosshair_f(void)
{
	key_dest = key_menu;
	m_state = m_crosshair;
	m_entersound = true;
	crosshair_cursor = 0;
	crosshairmenu.cursor = 0;
	crosshairmenu.search.len = 0;
	crosshairmenu.search.text[0] = 0;
	numberOfCrosshairItems = CROSSHAIR_ITEMS;
	crosshair_menu = true;

	IN_UpdateGrabs();
}

static void M_Crosshair_AdjustSliders(int dir)
{
	float f;
	S_LocalSound("misc/menu3.wav");

	switch (crosshair_cursor)
	{
	case CROSSHAIR_TOGGLE:
		if (dir > 0)
		{
			// Cycle through crosshair styles 0-7
			f = crosshair.value + 1;
			if (f > 7) f = 0;
		}
		else
		{
			f = crosshair.value - 1;
			if (f < 0) f = 7;
		}
		Cvar_SetValue("crosshair", f);
		break;
	case CROSSHAIR_ALPHA:
		f = scr_crosshairalpha.value + dir * 0.1;
		if (f > 1) f = 1;
		else if (f < 0) f = 0;
		Cvar_SetValue("scr_crosshairalpha", f);
		break;

case CROSSHAIR_COLOR:
	M_Crosshair_AdjustColor(dir);
	break;
case CROSSHAIR_COLOR_PICKER:
	colorpicker_return_fn = M_Menu_Crosshair_f;
	/* seed picker with current crosshair color */
	{
		plcolour_t c = CL_PLColours_Parse(scr_crosshaircolor.string);
		byte rgb[3];
		byte* pal;
		if (c.type == 2)
		{
			rgb[0] = c.rgb[0];
			rgb[1] = c.rgb[1];
			rgb[2] = c.rgb[2];
		}
		else
		{
			pal = (byte*)&d_8to24table[(c.basic << 4) + 8];
			rgb[0] = pal[0];
			rgb[1] = pal[1];
			rgb[2] = pal[2];
		}
		Tools_SetColorFromRGB(rgb[0], rgb[1], rgb[2]);
	}
	M_Menu_ColorPicker_f();
	break;

	case CROSSHAIR_OUTLINE:
		Cvar_SetValue("scr_crosshairoutline", !scr_crosshairoutline.value);
		break;

	case CROSSHAIR_SCALE:
		f = scr_crosshairscale.value + dir * 0.1;
		if (f > 10) f = 10;
		else if (f < 1) f = 1;
		Cvar_SetValue("scr_crosshairscale", f);
		break;

	case CROSSHAIR_X:
		f = scr_crosshair_x.value + dir * 1.0;
		if (f > 10) f = 10;
		else if (f < -10) f = -10;
		Cvar_SetValue("scr_crosshair_x", f);
		break;
	case CROSSHAIR_Y:
		f = scr_crosshair_y.value + dir * 1.0;
		if (f > 10) f = 10;
		else if (f < -10) f = -10;
		Cvar_SetValue("scr_crosshair_y", f);
		break;

	default:
		break;
	}
}

void M_Crosshair_Draw(void)
{
	qpic_t* p;
	float r;
	enum crosshair_e i;

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	const char* title = "Crosshair Options";
	M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);

	for (i = 0; i < CROSSHAIR_ITEMS; i++)
	{
		int y = 48 + 8 * i;
		const char* text = NULL;
		const char* value = NULL;

		switch (i)
		{
		case CROSSHAIR_TOGGLE:
			text = "       Crosshair";
			if (crosshair.value == 0)
				value = "Off";
			else
				value = va("Style %d", (int)crosshair.value);
			M_Print(178, y, value);
			break;
		case CROSSHAIR_ALPHA:
			text = "           Alpha";
			r = scr_crosshairalpha.value;
			M_DrawSlider(186, y, r, scr_crosshairalpha.value, "%.1f");
			break;

		case CROSSHAIR_COLOR:
			text = "           Color";
			if (crosshair_rgb_active)
			{
				value = va("%s", scr_crosshaircolor.string);
			}
			else
			{
				plcolour_t color = CL_PLColours_Parse(scr_crosshaircolor.string);
				if (color.type == 2)  // RGB color
					value = va("%s", scr_crosshaircolor.string);
				else  // Basic color
					value = va("%d", color.basic);
			}
			M_Print(178, y, value);
			break;
		case CROSSHAIR_COLOR_PICKER:
			text = "    Color Picker";
			{
				/* rainbow swatch like the picker hue bar */
				int swatch_x = 179;
				int swatch_y = y + 1;
				int swatch_w = 48;
				int swatch_h = 6;

				for (int xx = 0; xx < swatch_w; xx += 1)
				{
					float hue = (float)xx / (float)(swatch_w - 1);
					byte rgb[3];
					hsvtorgb(hue, 1.0f, 1.0f, rgb);
					Draw_FillPlayer(swatch_x + xx, swatch_y, 1, swatch_h,
						Tools_ColorFromRGB(rgb[0], rgb[1], rgb[2]), 1.0f);
				}

				plcolour_t border = Tools_ColorFromRGB(40, 40, 40);
				Draw_FillPlayer(swatch_x - 1, swatch_y - 1, swatch_w + 2, 1, border, 1.0f);
				Draw_FillPlayer(swatch_x - 1, swatch_y + swatch_h, swatch_w + 2, 1, border, 1.0f);
				Draw_FillPlayer(swatch_x - 1, swatch_y, 1, swatch_h, border, 1.0f);
				Draw_FillPlayer(swatch_x + swatch_w, swatch_y, 1, swatch_h, border, 1.0f);
			}
			break;

		case CROSSHAIR_OUTLINE:
			text = "         Outline";
			M_DrawCheckbox(178, y, scr_crosshairoutline.value);
			break;

		case CROSSHAIR_SCALE:
			text = "           Scale";
			r = (scr_crosshairscale.value - 1.0f) / 9.0f;  // Map 1-10 to 0-1 for slider
			M_DrawSlider(186, y, r, scr_crosshairscale.value, "%.1f");
			break;

		case CROSSHAIR_X:
			text = "        X-Adjust";
			{ // Use a block to scope temp variables
				float current_x_val = scr_crosshair_x.value;
				float display_x_val = roundf(current_x_val);
				if (display_x_val == -0.0f) { // Check for negative zero
					display_x_val = 0.0f;     // Convert to positive zero
				}
				r = (current_x_val + 10.0f) / 20.0f; // Slider position based on actual cvar value
				M_DrawSlider(186, y, r, display_x_val, "%.0f"); // Display corrected value
			}
			break;
		case CROSSHAIR_Y:
			text = "        Y-Adjust";
			{ // Use a block to scope temp variables
				float current_y_val = scr_crosshair_y.value;
				float display_y_val = roundf(current_y_val);
				if (display_y_val == -0.0f) { // Check for negative zero
					display_y_val = 0.0f;     // Convert to positive zero
				}
				r = (current_y_val + 10.0f) / 20.0f; // Slider position based on actual cvar value
				M_DrawSlider(186, y, r, display_y_val, "%.0f"); // Display corrected value
			}
			break;

		default:
			break;
		}

		if (text)
		{
			if (crosshairmenu.search.len > 0 &&
				q_strcasestr(text, crosshairmenu.search.text))
			{
				M_PrintHighlight(16, y, text,
					crosshairmenu.search.text,
					crosshairmenu.search.len);
			}
			else
			{
				M_Print(16, y, text);
			}
		}

	}

	if (crosshair.value > 0)
		M_DrawMenuCrosshair(160 + (int)scr_crosshair_x.value, 100 + (int)scr_crosshair_y.value);

	// Draw cursor
	M_DrawCharacter(168, 48 + crosshair_cursor * 8, 12 + ((int)(realtime * 4) & 1));

	if (crosshair_cursor == CROSSHAIR_COLOR)
		M_PrintRGBA(74, 120, "+shift for RGB colors", CL_PLColours_Parse("0xffffff"), 0.6f, false);

	// Draw search box if search is active
	if (crosshairmenu.search.len > 0)
	{
		M_DrawTextBox(16, 170, 32, 1);
		M_PrintHighlight(24, 178, crosshairmenu.search.text,
			crosshairmenu.search.text,
			crosshairmenu.search.len);
		int cursor_x = 24 + 8 * crosshairmenu.search.len;
		if (numberOfCrosshairItems == 0)
			M_DrawCharacter(cursor_x, 178, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, 178, 10 + ((int)(realtime * 4) & 1));
	}
}

void M_Crosshair_Key(int k)
{
	// Handle slider grab release
	if (!keydown[K_MOUSE1])
		crosshair_slider_grab = false;

	if (crosshair_slider_grab)
	{
		switch (k)
		{
		case 'c':
		case 'C':
			if (keydown[K_CTRL])
			{
				if (last_crosshair_color[0] != '\0')
					SDL_SetClipboardText(last_crosshair_color);
				const char* soundFile = COM_FileExists("sound/qssm/copy.wav", NULL) ? "qssm/copy.wav" : "player/tornoff2.wav";
				S_LocalSound(soundFile);
			}
			break;
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			crosshair_slider_grab = false;
			break;
		}
		return;
	}

	// Handle search functionality first
	if (k == K_ESCAPE)
	{
		if (crosshairmenu.search.len > 0)
		{
			crosshairmenu.search.len = 0;
			crosshairmenu.search.text[0] = 0;
			numberOfCrosshairItems = CROSSHAIR_ITEMS;
			return;
		}
		crosshair_menu = false;
		M_Menu_Options_f();
		return;
	}
	else if (k == K_BACKSPACE)
	{
		if (crosshairmenu.search.len > 0)
		{
			crosshairmenu.search.text[--crosshairmenu.search.len] = 0;
			if (crosshairmenu.search.len > 0)
			{
				numberOfCrosshairItems = 0;
				for (int i = 0; i < CROSSHAIR_ITEMS; i++)
				{
					const char* itemtext = M_Crosshair_GetItemText(i);
					if (itemtext && q_strcasestr(itemtext, crosshairmenu.search.text))
					{
						numberOfCrosshairItems++;
						if (numberOfCrosshairItems == 1)
							crosshair_cursor = i;
					}
				}
			}
			else
			{
				numberOfCrosshairItems = CROSSHAIR_ITEMS;
			}
			return;
		}
	}

	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		M_Menu_Options_f();
		break;

	case K_MOUSE1:
		m_entersound = true;

		// Check if click is in search box area
		if (crosshairmenu.search.len > 0 && m_mousey >= 170)
			break;

		// Check if click is in valid menu area
		if (m_mousey >= 48 && m_mousey < 48 + (CROSSHAIR_ITEMS * 8))
		{
			crosshair_cursor = (m_mousey - 48) / 8;

			if (crosshair_cursor == CROSSHAIR_ALPHA || crosshair_cursor == CROSSHAIR_SCALE || crosshair_cursor == CROSSHAIR_X || crosshair_cursor == CROSSHAIR_Y)
			{
				crosshair_slider_grab = true;
			}
			else
			{
				M_Crosshair_AdjustSliders(1);
			}
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		M_Crosshair_AdjustSliders(1);
		break;

	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		crosshair_cursor--;
		if (crosshair_cursor < 0)
			crosshair_cursor = numberOfCrosshairItems - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		crosshair_cursor++;
		if (crosshair_cursor >= numberOfCrosshairItems)
			crosshair_cursor = 0;
		break;

	case K_LEFTARROW:
	case K_MWHEELDOWN:
		M_Crosshair_AdjustSliders(-1);
		break;

	case K_RIGHTARROW:
	case K_MWHEELUP:
		M_Crosshair_AdjustSliders(1);
		break;
	}
}

void M_Crosshair_Mousemove(int cx, int cy)
{
	if (crosshair_slider_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			crosshair_slider_grab = false;
			return;
		}

		float f;
		switch (crosshair_cursor)
		{
		case CROSSHAIR_ALPHA:
			f = M_MouseToSliderFraction(cx - 187);
			Cvar_SetValue("scr_crosshairalpha", f);
			break;
		case CROSSHAIR_SCALE:
			f = 1.0f + M_MouseToSliderFraction(cx - 187) * 9.0f;
			Cvar_SetValue("scr_crosshairscale", f);
			break;
		case CROSSHAIR_X:
			f = -10.0f + M_MouseToSliderFraction(cx - 187) * 20.0f;
			Cvar_SetValue("scr_crosshair_x", f);
			break;
		case CROSSHAIR_Y:
			f = -10.0f + M_MouseToSliderFraction(cx - 187) * 20.0f;
			Cvar_SetValue("scr_crosshair_y", f);
			break;
		default:
			break;
		}
		return;
	}

	// Don't process mouse movement if it's in the search box area
	if (crosshairmenu.search.len > 0 && cy >= 170)
		return;

	// Calculate which menu item the mouse is over
	int item = (cy - 48) / 8;

	// Make sure the item is within valid range
	if (item >= 0 && item < CROSSHAIR_ITEMS)
	{
		// Update the cursor position
		crosshair_cursor = item;
	}
}

/*
==================
Console Menu
==================
*/

extern cvar_t scr_conscale, scr_consize, scr_conspeed, scr_conalpha, cl_contentfilter, con_typing, scr_conback, scr_concolor;

static enum console_e
{
	CONSOLE_FONTSIZE,
	CONSOLE_HEIGHT,
	CONSOLE_SPEED,
	CONSOLE_TRANSPARENCY,
	CONSOLE_CONBACK,
	CONSOLE_CONCOLOR,
	CONSOLE_CONTENTFILTER,
	CONSOLE_TYPING,
	CONSOLE_SAVE_HISTORY,
	CONSOLE_CLEAR_CONSOLE,
	CONSOLE_CLEAR_HISTORY,
	CONSOLE_COUNT
} console_cursor;

#define CONSOLE_ITEMS (CONSOLE_COUNT)
#define CONSOLE_CONBACK_BOX_X 178
#define CONSOLE_CONBACK_BOX_WIDTH 14
#define CONSOLE_CONBACK_TEXT_X (CONSOLE_CONBACK_BOX_X + 8)
int numberOfConsoleItems = CONSOLE_ITEMS;

static struct
{
	int cursor;
	struct {
		char text[32];
		int len;
	} search;
} consolemenu;

static qboolean console_field_editing;
static menu_textfield_t console_conback_field;
static char console_conback_buffer[MAX_QPATH];
static qboolean console_rgb_active;
static const char* M_Console_GetItemText(int index);
static void M_Console_UpdateSearchResults(void);
static int M_Console_LivePreviewId(void);

static menu_textfield_t *M_Console_GetFieldForCursor(void)
{
	switch (console_cursor)
	{
	case CONSOLE_CONBACK:
		return &console_conback_field;
	default:
		return NULL;
	}
}

static void M_Console_ClearTextSelections(void)
{
	M_TextField_ClearSelection(&console_conback_field);
}

static void M_Console_InitTextFields(void)
{
	q_strlcpy(console_conback_buffer, scr_conback.string, sizeof(console_conback_buffer));
	M_TextField_Init(&console_conback_field, console_conback_buffer, sizeof(console_conback_buffer) - 1, false);
	console_field_editing = false;
	console_rgb_active = false;
}

static void M_Console_ClearSearch(void)
{
	consolemenu.search.len = 0;
	consolemenu.search.text[0] = 0;
	M_Console_UpdateSearchResults();
}

static void M_Console_UpdateSearchResults(void)
{
	console_cursor = (enum console_e)M_Menu_UpdateSearchCursor(
		CONSOLE_ITEMS, (int)console_cursor, &numberOfConsoleItems,
		M_Console_GetItemText, consolemenu.search.text, consolemenu.search.len);
}

static int M_Console_GetItemY(int index)
{
	int y = 48 + index * 8;

	if (index >= CONSOLE_CONBACK)
		y += 8;
	if (index >= CONSOLE_CONCOLOR)
		y += 8;
	if (index >= CONSOLE_CLEAR_CONSOLE)
		y += 8;

	return y;
}

static int M_Console_GetItemAtY(int cy)
{
	for (int i = 0; i < CONSOLE_ITEMS; ++i)
	{
		int y = M_Console_GetItemY(i);
		int top = (i == CONSOLE_CONBACK) ? y - 8 : y;
		int bottom = y + 8;

		if (cy >= top && cy < bottom)
			return i;
	}

	return -1;
}

static void M_Console_BeginFieldEdit(void)
{
	menu_textfield_t* field = M_Console_GetFieldForCursor();

	if (!field)
		return;

	M_Console_ClearSearch();

	q_strlcpy(console_conback_buffer, scr_conback.string, sizeof(console_conback_buffer));
	M_TextField_Init(&console_conback_field, console_conback_buffer, sizeof(console_conback_buffer) - 1, false);
	field = &console_conback_field;

	field->cursor = (int)strlen(field->text);
	field->sel_start = -1;
	console_field_editing = true;
}

static void M_Console_EndFieldEdit(qboolean apply_changes)
{
	menu_textfield_t* field = M_Console_GetFieldForCursor();

	if (!field)
	{
		console_field_editing = false;
		return;
	}

	if (apply_changes)
	{
		Cvar_Set("scr_conback", console_conback_buffer);
		M_LivePreview_WantAndKick (M_Console_LivePreviewId (), M_Console_GetItemY (console_cursor));
	}
	else
	{
		q_strlcpy(console_conback_buffer, scr_conback.string, sizeof(console_conback_buffer));
	}

	field->cursor = (int)strlen(field->text);
	field->sel_start = -1;
	M_TextField_ClampCursor(field);
	console_field_editing = false;
}

static int M_Console_GetFieldViewStart(const menu_textfield_t* field)
{
	int len = (int)strlen(field->text);

	if (len <= CONSOLE_CONBACK_BOX_WIDTH)
		return 0;

	return CLAMP(0, field->cursor - CONSOLE_CONBACK_BOX_WIDTH, len - CONSOLE_CONBACK_BOX_WIDTH);
}

static void M_Console_MouseClickField(menu_textfield_t* field, int mouse_x)
{
	int view_start = M_Console_GetFieldViewStart(field);

	M_TextField_MouseClick(field, mouse_x, CONSOLE_CONBACK_TEXT_X - view_start * 8);
}

static void M_Console_DrawField(int y, menu_textfield_t* field, const char* placeholder)
{
	int view_start = M_Console_GetFieldViewStart(field);
	int sel_begin, sel_end;

	M_DrawTextBox(CONSOLE_CONBACK_BOX_X, y - 8, CONSOLE_CONBACK_BOX_WIDTH, 1);

	if (M_TextField_GetSelection(field, &sel_begin, &sel_end))
	{
		int visible_begin = CLAMP(view_start, sel_begin, view_start + CONSOLE_CONBACK_BOX_WIDTH);
		int visible_end = CLAMP(view_start, sel_end, view_start + CONSOLE_CONBACK_BOX_WIDTH);

		if (visible_begin < visible_end)
		{
			Draw_Fill(CONSOLE_CONBACK_TEXT_X + (visible_begin - view_start) * 8, y,
				(visible_end - visible_begin) * 8, 8, 170, 0.4f);
		}
	}

	if (field->text[0])
	{
		char visible_text[CONSOLE_CONBACK_BOX_WIDTH + 1];

		q_strlcpy(visible_text, field->text + view_start, sizeof(visible_text));
		M_PrintWhite(CONSOLE_CONBACK_TEXT_X, y, visible_text);
	}
	else if (!(console_field_editing && field == M_Console_GetFieldForCursor()))
		M_PrintRGBA(CONSOLE_CONBACK_TEXT_X, y, placeholder, CL_PLColours_Parse("0xffffff"), 0.5f, false);

	if (console_field_editing && field == M_Console_GetFieldForCursor())
	{
		menu_textfield_t visible_field = *field;

		visible_field.cursor = CLAMP(0, field->cursor - view_start, CONSOLE_CONBACK_BOX_WIDTH);
		M_TextField_DrawCursor(&visible_field, CONSOLE_CONBACK_TEXT_X, y);
	}
}

static void M_Console_AdjustColor(int dir)
{
	const char* current = scr_concolor.string;

	if (keydown[K_SHIFT])
	{
		plcolour_t color;
		vec3_t hsv;

		console_rgb_active = true;
		color = CL_PLColours_Parse(current[0] ? current : "0xffffff");
		rgbtohsv(color.rgb, hsv);

		hsv[0] += dir / 128.0f;
		hsv[1] = 1.0f;
		hsv[2] = 1.0f;
		color.type = 2;
		color.basic = 0;
		hsvtorgb(hsv[0], hsv[1], hsv[2], color.rgb);
		Cvar_Set("scr_concolor", CL_PLColours_ToString(color));
		return;
	}

	console_rgb_active = false;

	if (strcmp(current, "") == 0)
	{
		if (dir > 0)
		{
			plcolour_t color;
			color.type = 1;
			color.basic = 0;
			Cvar_Set("scr_concolor", CL_PLColours_ToString(color));
		}
		else if (dir < 0)
		{
			plcolour_t color;
			color.type = 1;
			color.basic = 13;
			Cvar_Set("scr_concolor", CL_PLColours_ToString(color));
		}
		return;
	}

	{
		plcolour_t color = CL_PLColours_Parse(current);
		int newBasic;

		color.type = 1;
		newBasic = color.basic + dir;

		if (newBasic < 0)
		{
			Cvar_Set("scr_concolor", "");
			return;
		}
		else if (newBasic > 13)
		{
			Cvar_Set("scr_concolor", "");
			return;
		}
		else
			color.basic = newBasic;

		Cvar_Set("scr_concolor", CL_PLColours_ToString(color));
	}
}

static const char* M_Console_GetItemText(int index)
{
	static char buffer[64];

	switch (index)
	{
	case CONSOLE_FONTSIZE:
		return "Font Size";
	case CONSOLE_HEIGHT:
		return "Console Height";
	case CONSOLE_SPEED:
		return "Down/Up Speed";
	case CONSOLE_TRANSPARENCY:
		return "Transparency";
	case CONSOLE_CONBACK:
		return "Background Image";
	case CONSOLE_CONCOLOR:
		return "Background Color";
	case CONSOLE_CONTENTFILTER:
		return "Content Filter";
	case CONSOLE_TYPING:
		return "Typing Status";
	case CONSOLE_SAVE_HISTORY:
		return "Save History";
	case CONSOLE_CLEAR_CONSOLE:
		return "Clear Console";
	case CONSOLE_CLEAR_HISTORY:
		return "Clear History";
	default:
		q_snprintf(buffer, sizeof(buffer), "Unknown Item %d", index);
		return buffer;
	}
}

static int M_Console_LivePreviewId(void)
{
	switch (console_cursor)
	{
	case CONSOLE_FONTSIZE:
		return LP_CONFONT;
	case CONSOLE_HEIGHT:
		return LP_CONHEIGHT;
	case CONSOLE_SPEED:
		return LP_CONSPEED;
	case CONSOLE_TRANSPARENCY:
		return LP_CONALPHA;
	case CONSOLE_CONBACK:
		return LP_CONBACK;
	case CONSOLE_CONCOLOR:
		return LP_CONCOLOR;
	case CONSOLE_TYPING:
		return LP_CONTYPING;
	default:
		return LP_NONE;
	}
}

static qboolean M_Console_IsSliderItem (void)
{
	return (console_cursor == CONSOLE_FONTSIZE ||
			console_cursor == CONSOLE_HEIGHT ||
			console_cursor == CONSOLE_SPEED ||
			console_cursor == CONSOLE_TRANSPARENCY);
}

static void M_Console_StartSliderGrab (void)
{
	console_slider_grab = true;
	M_LivePreview_WantAndKick (M_Console_LivePreviewId (), M_Console_GetItemY (console_cursor));
}

void M_Menu_Console_f(void)
{
	key_dest = key_menu;
	m_state = m_console;
	m_entersound = true;
	console_cursor = 0;
	consolemenu.cursor = 0;
	consolemenu.search.len = 0;
	consolemenu.search.text[0] = 0;
	numberOfConsoleItems = CONSOLE_ITEMS;
	M_Console_InitTextFields();
	M_LivePreview_Reset();

	IN_UpdateGrabs();
}

static void M_Console_AdjustSliders(int dir)
{
	float f;
	int val;

	if (console_cursor == CONSOLE_CONBACK ||
		console_cursor == CONSOLE_CLEAR_CONSOLE ||
		console_cursor == CONSOLE_CLEAR_HISTORY)
		return;

	S_LocalSound("misc/menu3.wav");

	M_LivePreview_WantAndKick (M_Console_LivePreviewId (), M_Console_GetItemY (console_cursor));

	switch (console_cursor)
	{
	case CONSOLE_FONTSIZE:
		f = scr_conscale.value + dir;
		if (f > M_ConsoleScaleMax()) f = M_ConsoleScaleMax();
		else if (f < 1) f = 1;
		Cvar_SetValue("scr_conscale", f);
		break;

	case CONSOLE_HEIGHT:
		f = scr_consize.value + dir * 0.1;
		if (f > 1) f = 1;
		else if (f < 0) f = 0;
		Cvar_SetValue("scr_consize", f);
		break;

	case CONSOLE_SPEED:
		f = scr_conspeed.value + dir * 100;
		if (f > 10000) f = 10000;
		else if (f < 100) f = 100;
		Cvar_SetValue("scr_conspeed", f);
		break;

	case CONSOLE_TRANSPARENCY:
		f = scr_conalpha.value + dir * 0.1;
		if (f > 1) f = 1;
		else if (f < 0) f = 0;
		Cvar_SetValue("scr_conalpha", f);
		break;

	case CONSOLE_CONCOLOR:
		M_Console_AdjustColor(dir);
		break;

	case CONSOLE_CONTENTFILTER:
		val = (int)cl_contentfilter.value + dir;
		if (val > 2) val = 0;
		else if (val < 0) val = 2;
		Cvar_SetValue("cl_contentfilter", val);
		break;

	case CONSOLE_TYPING:
		Cvar_SetValue("con_typing", !con_typing.value);
		break;
	case CONSOLE_SAVE_HISTORY:
		Cvar_SetValue("con_savehistory", !Cvar_VariableValue("con_savehistory"));
		break;
	default:
		break;
	}
}

static void M_Console_ActivateItem(void)
{
	if (M_Console_GetFieldForCursor())
		M_Console_BeginFieldEdit();
	else if (M_Console_IsSliderItem())
		M_Console_StartSliderGrab();
	else
	{
		switch (console_cursor)
		{
		case CONSOLE_CLEAR_CONSOLE:
			Cbuf_AddText("clear\n");
			break;
		case CONSOLE_CLEAR_HISTORY:
			Cbuf_AddText("clearhistory\n");
			break;
		default:
			M_Console_AdjustSliders(1);
			break;
		}
	}
}

void M_Console_Draw(void)
{
	qpic_t* p;
	float r;
	enum console_e i;
	const char* filter_text;
	const char* value;

	M_TextField_CheckMouseRelease();
	console_cursor = (enum console_e)M_Menu_ClampCursorValue((int)console_cursor, CONSOLE_ITEMS);

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	const char* title = "Console Options";
	M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);

	M_LivePreview_WantAt (M_Console_LivePreviewId (), M_Console_GetItemY (console_cursor));

	for (i = 0; i < CONSOLE_ITEMS; i++)
	{
		int y = M_Console_GetItemY(i);
		qboolean isolated = M_LivePreview_IsolateY (y);
		const char* text = NULL;

		if (isolated)
			M_LivePreview_BeginIsolate ();

		switch (i)
		{
		case CONSOLE_FONTSIZE:
			text = "       Font Size";
			r = M_ConsoleScaleFraction(scr_conscale.value);
			M_DrawSlider(186, y, r, scr_conscale.value, "%.0f");
			break;

		case CONSOLE_HEIGHT:
			text = "          Height";
			r = scr_consize.value;
			M_DrawSlider(186, y, r, scr_consize.value * 100, "%.0f%%");
			break;

		case CONSOLE_SPEED:
			text = "           Speed";
			r = (scr_conspeed.value - 100) / 9900;  // Simplified calculation
			M_DrawSlider(186, y, r, scr_conspeed.value, "%.0f");
			break;

		case CONSOLE_TRANSPARENCY:
			text = "    Transparency";
			r = scr_conalpha.value;
			M_DrawSlider(186, y, r, scr_conalpha.value * 100, "%.0f%%");
			break;

		case CONSOLE_CONBACK:
			text = "Background Image";
			M_Console_DrawField(y, &console_conback_field, "default");
			break;

		case CONSOLE_CONCOLOR:
			text = "Background Color";
			if (strcmp(scr_concolor.string, "") == 0)
				value = "default";
			else if (console_rgb_active)
				value = va("%s", scr_concolor.string);
			else
			{
				plcolour_t color = CL_PLColours_Parse(scr_concolor.string);
				value = (color.type == 2) ? va("%s", scr_concolor.string) : va("%d", color.basic);
			}
			M_Print(178, y, value);
			if (strcmp(scr_concolor.string, "") != 0)
				Draw_FillPlayer(178 + (strlen(value) * 8) + 4, y + 2, 6, 6, CL_PLColours_Parse(scr_concolor.string), 1.0f);
			break;

		case CONSOLE_CONTENTFILTER:
			text = "  Content Filter";
			switch ((int)cl_contentfilter.value)
			{
			case 0: filter_text = "off"; break;
			case 1: filter_text = "partial"; break;
			case 2: filter_text = "full"; break;
			default: filter_text = "unknown"; break;
			}
			M_Print(178, y, filter_text);
			break;

		case CONSOLE_TYPING:
			text = "   Typing Status";
			M_DrawCheckbox(178, y, con_typing.value != 0);
			break;

		case CONSOLE_SAVE_HISTORY:
			text = "    Save History";
			M_DrawCheckbox(178, y, Cvar_VariableValue("con_savehistory") != 0);
			break;

		case CONSOLE_CLEAR_CONSOLE:
			text = "   Clear Console";
			M_Print(178, y, "clear");
			break;

		case CONSOLE_CLEAR_HISTORY:
			text = "   Clear History";
			M_Print(178, y, "clearhistory");
			break;

		default:
			break;
		}

		if (text)
		{
			if (consolemenu.search.len > 0 &&
				q_strcasestr(text, consolemenu.search.text))
			{
				M_PrintHighlight(16, y, text,
					consolemenu.search.text,
					consolemenu.search.len);
			}
			else
			{
				M_Print(16, y, text);
			}
		}

		if (isolated)
			M_LivePreview_EndIsolate ();
	}

	// Draw cursor
	{
		int y = M_Console_GetItemY(console_cursor);
		qboolean isolated = M_LivePreview_IsolateY (y);
		if (isolated)
			M_LivePreview_BeginIsolate ();
		M_DrawCharacter(168, y, 12 + ((int)(realtime * 4) & 1));
		if (isolated)
			M_LivePreview_EndIsolate ();
	}

	// Position the hint below the last menu item so it doesn't overlap
	int hint_y = M_Console_GetItemY(CONSOLE_CLEAR_HISTORY) + 18;
	if (console_field_editing)
	{
		const char* hint = "Enter applies, Esc cancels";
		M_PrintRGBA((320 - (int)strlen(hint) * 8) / 2, hint_y, hint, CL_PLColours_Parse("0xffffff"), 0.5f, false);
	}
	else if (console_cursor == CONSOLE_CONCOLOR)
		M_PrintRGBA(74, hint_y, "+shift for RGB colors", CL_PLColours_Parse("0xffffff"), 0.6f, false);

	// Draw search box if search is active
	if (consolemenu.search.len > 0)
	{
		M_DrawTextBox(16, 170, 32, 1);
		M_PrintHighlight(24, 178, consolemenu.search.text,
			consolemenu.search.text,
			consolemenu.search.len);
		int cursor_x = 24 + 8 * consolemenu.search.len;
		if (numberOfConsoleItems == 0)
			M_DrawCharacter(cursor_x, 178, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, 178, 10 + ((int)(realtime * 4) & 1));
	}
}

void M_Console_Key(int k)
{
	// Handle slider grab release
	if (!keydown[K_MOUSE1])
		console_slider_grab = false;

	if (console_slider_grab)
	{
		switch (k)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			console_slider_grab = false;
			break;
		}
		return;
	}

	if (console_field_editing)
	{
		menu_textfield_t* active_field = M_Console_GetFieldForCursor();

		if (active_field && M_TextField_Key(active_field, k))
			return;
		if (k >= 32 && k < 127)
			return;

		switch (k)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			M_Console_EndFieldEdit(false);
			return;

		case K_ENTER:
		case K_KP_ENTER:
		case K_ABUTTON:
			S_LocalSound("misc/menu3.wav");
			M_Console_EndFieldEdit(true);
			return;

		case K_UPARROW:
			M_Console_EndFieldEdit(true);
			S_LocalSound("misc/menu1.wav");
			console_cursor--;
			if (console_cursor < 0)
				console_cursor = CONSOLE_ITEMS - 1;
			return;

		case K_DOWNARROW:
		case K_TAB:
			M_Console_EndFieldEdit(true);
			S_LocalSound("misc/menu1.wav");
			console_cursor++;
			if (console_cursor >= CONSOLE_ITEMS)
				console_cursor = 0;
			return;

			case K_MOUSE1:
				if (active_field && M_TextField_MouseInRow(m_mousey, M_Console_GetItemY(CONSOLE_CONBACK)))
				{
					M_Console_MouseClickField(active_field, m_mousex);
					return;
				}
			M_Console_EndFieldEdit(true);
			break;

		default:
			break;
		}
	}

	// Handle search functionality first
	if (k == K_ESCAPE)
	{
		if (consolemenu.search.len > 0)
		{
			M_Console_ClearSearch();
			return;
		}
		if (M_LivePreview_Alpha() > 0.f)
		{
			M_LivePreview_Reset();
			return;
		}
		M_Menu_Options_f();
		return;
	}
	else if (keydown[K_CTRL])
	{
		if ((k == 'u' || k == 'U') && consolemenu.search.len > 0)
		{
			// Clear entire search with Ctrl+U
			M_Console_ClearSearch();
			return;
		}
		else if (k == K_BACKSPACE && consolemenu.search.len > 0)
		{
			// Delete previous word with Ctrl+Backspace
			listsearch_t temp;
			temp.len = consolemenu.search.len;
			Q_strcpy(temp.text, consolemenu.search.text);
			M_DeletePrevWord(&temp);
			Q_strcpy(consolemenu.search.text, temp.text);
			consolemenu.search.len = temp.len;
			M_Console_UpdateSearchResults();
			return;
		}
	}
	else if (k == K_BACKSPACE)
	{
		if (consolemenu.search.len > 0)
		{
			consolemenu.search.text[--consolemenu.search.len] = 0;
			M_Console_UpdateSearchResults();
			return;
		}
	}
	else if (k >= 32 && k < 127)
	{
		if (consolemenu.search.len < sizeof(consolemenu.search.text) - 1)
		{
			consolemenu.search.text[consolemenu.search.len++] = k;
			consolemenu.search.text[consolemenu.search.len] = 0;
			M_Console_UpdateSearchResults();
			return;
		}
	}

	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		if (M_LivePreview_Alpha() > 0.f)
		{
			M_LivePreview_Reset();
			return;
		}
		M_Menu_Options_f();
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		M_Console_ActivateItem();
		break;

	case K_MOUSE1:
		m_entersound = true;

		if (consolemenu.search.len > 0 && m_mousey >= 170)
			break;

		{
			int item = M_Console_GetItemAtY(m_mousey);

			if (item >= 0)
		{
				console_cursor = item;

				if (M_Console_GetFieldForCursor())
				{
						menu_textfield_t* field;
						M_Console_BeginFieldEdit();
						field = M_Console_GetFieldForCursor();
						if (field)
							M_Console_MouseClickField(field, m_mousex);
					}
				else
					M_Console_ActivateItem();
			}
		}
		break;

	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		console_cursor = (enum console_e)M_Menu_MoveSearchCursor(
			CONSOLE_ITEMS, numberOfConsoleItems, (int)console_cursor, -1,
			M_Console_GetItemText, consolemenu.search.text, consolemenu.search.len);
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		console_cursor = (enum console_e)M_Menu_MoveSearchCursor(
			CONSOLE_ITEMS, numberOfConsoleItems, (int)console_cursor, 1,
			M_Console_GetItemText, consolemenu.search.text, consolemenu.search.len);
		break;

	case K_LEFTARROW:
	case K_MWHEELDOWN:
		M_Console_AdjustSliders(-1);
		break;

	case K_RIGHTARROW:
	case K_MWHEELUP:
		M_Console_AdjustSliders(1);
		break;
	}
}

void M_Console_Char(int k)
{
	menu_textfield_t* active_field;

	if (!console_field_editing)
		return;

	active_field = M_Console_GetFieldForCursor();
	if (active_field)
		M_TextField_Char(active_field, k);
}

qboolean M_Console_TextEntry(void)
{
	return console_field_editing && M_Console_GetFieldForCursor() != NULL;
}

void M_Console_Mousemove(int cx, int cy)
{
	if (textfield_mouse_dragging &&
		textfield_drag_field == &console_conback_field)
	{
		M_TextField_MouseDrag(cx);
		return;
	}

	if (console_field_editing)
		return;

	if (console_slider_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			console_slider_grab = false;
			return;
		}

		M_LivePreview_WantAndKick (M_Console_LivePreviewId (), M_Console_GetItemY (console_cursor));

		float f;
		switch (console_cursor)
		{
		case CONSOLE_FONTSIZE:
			f = M_ConsoleScaleFromFraction(M_MouseToSliderFraction(cx - 187));
			f = (int)f;  // Round to nearest integer
			Cvar_SetValue("scr_conscale", CLAMP(1, f, M_ConsoleScaleMax()));
			break;

		case CONSOLE_HEIGHT:
			f = M_MouseToSliderFraction(cx - 187);
			Cvar_SetValue("scr_consize", CLAMP(0, f, 1));
			break;

		case CONSOLE_SPEED:
			f = 100.f + M_MouseToSliderFraction(cx - 187) * 9900.f;
			f = floor(f / 100) * 100;  // Round down to nearest 100
			Cvar_SetValue("scr_conspeed", CLAMP(100, f, 10000));
			break;

		case CONSOLE_TRANSPARENCY:
			f = M_MouseToSliderFraction(cx - 187);
			Cvar_SetValue("scr_conalpha", CLAMP(0, f, 1));
			break;

		default:
			break;
		}
		return;
	}

	// Don't process mouse movement if it's in the search box area
	if (consolemenu.search.len > 0 && cy >= 170)
		return;

	// Calculate which menu item the mouse is over
	int item = M_Console_GetItemAtY(cy);

	// Make sure the item is within valid range
	if (item >= 0)
	{
		int old_cursor = console_cursor;
		console_cursor = item;
		if (console_cursor != old_cursor)
			M_Console_ClearTextSelections();
	}
}

#define TOOLS_BACKGROUND_TOP 16
#define TOOLS_BACKGROUND_HEIGHT 184

#define TOOLS_PICKER_WIDTH 192
#define TOOLS_PICKER_HEIGHT 96
#define TOOLS_SLIDER_HEIGHT 12
#define TOOLS_SLIDER_GAP 12
#define TOOLS_INFO_TOP_GAP 12
#define TOOLS_HINT_GAP 8

#define TOOLS_PICKER_Y 24
#define TOOLS_SLIDER_Y (TOOLS_PICKER_Y + TOOLS_PICKER_HEIGHT + TOOLS_SLIDER_GAP)
#define TOOLS_INFO_Y (TOOLS_SLIDER_Y + TOOLS_SLIDER_HEIGHT + TOOLS_INFO_TOP_GAP)
#define TOOLS_HINT_Y (TOOLS_INFO_Y + Tools_TextBoxPixelHeight(1) + TOOLS_HINT_GAP)

#define TOOLS_HEX_BOX_WIDTH 11
#define TOOLS_RGB_BOX_WIDTH 17
#define TOOLS_SWATCH_WIDTH 32
#define TOOLS_SWATCH_GAP 8
#define TOOLS_INFO_GAP 16

#define TOOLS_SAT_STEP (1.0f / (TOOLS_PICKER_WIDTH - 1))
#define TOOLS_VAL_STEP (1.0f / (TOOLS_PICKER_HEIGHT - 1))
#define TOOLS_HUE_STEP (1.0f / (TOOLS_PICKER_WIDTH - 1))

enum
{
	TOOLS_FOCUS_AREA,
	TOOLS_FOCUS_HUE
};

static struct
{
	float hue;
	float saturation;
	float value;
	int focus;
	qboolean dragging_area;
	qboolean dragging_hue;
	qboolean initialized;
} toolsmenu;
static double toolsmenu_hex_flash_until;
static double toolsmenu_rgb_flash_until;

typedef struct
{
	qboolean attempted;
	qboolean ready;
	GLuint program;
	GLint u_ring_color;
	GLint u_fill_color;
	GLint u_radius;
	GLint u_ring_width;
	GLint u_aa_width;
} tools_circle_shader_t;

static tools_circle_shader_t tools_circle_shader;

static void ColorPicker_ReturnToParent(void)
{
	void (*return_fn)(void) = colorpicker_return_fn;
	colorpicker_return_fn = NULL;

	/* If we came from the crosshair menu, commit the current picker color to the crosshair color cvar */
	if (return_fn == M_Menu_Crosshair_f)
	{
		byte rgb[3];
		hsvtorgb(toolsmenu.hue, toolsmenu.saturation, toolsmenu.value, rgb);
		plcolour_t c = Tools_ColorFromRGB(rgb[0], rgb[1], rgb[2]);
		Cvar_Set("scr_crosshaircolor", CL_PLColours_ToString(c));
		q_strlcpy(last_crosshair_color, CL_PLColours_ToString(c), sizeof(last_crosshair_color));
	}

	if (return_fn)
		return_fn();
	else
		M_Menu_Options_f();
}

static float Tools_Clamp01(float value)
{
	if (value < 0.0f)
		return 0.0f;
	if (value > 1.0f)
		return 1.0f;
	return value;
}

static float Tools_WrapHue(float value)
{
	while (value < 0.0f)
		value += 1.0f;
	while (value >= 1.0f)
		value -= 1.0f;
	return value;
}

static int Tools_TextBoxPixelWidth(int width)
{
	return (width + 2) * 8;
}

static int Tools_TextBoxPixelHeight(int lines)
{
	return (lines + 2) * 8;
}

static int Tools_GetLayoutOriginX(void)
{
	int total_width = TOOLS_SWATCH_WIDTH + TOOLS_SWATCH_GAP + TOOLS_PICKER_WIDTH;
	return (320 - total_width) / 2;
}

static int Tools_GetSwatchX(void)
{
	return Tools_GetLayoutOriginX();
}

static int Tools_GetPickerX(void)
{
	return Tools_GetLayoutOriginX() + TOOLS_SWATCH_WIDTH + TOOLS_SWATCH_GAP;
}

static int Tools_GetPickerY(void)
{
	return TOOLS_PICKER_Y;
}

static int Tools_GetSliderX(void)
{
	return Tools_GetPickerX();
}

static int Tools_GetSliderY(void)
{
	return TOOLS_SLIDER_Y;
}

static int Tools_GetHexBoxX(void)
{
	int hex_width = Tools_TextBoxPixelWidth(TOOLS_HEX_BOX_WIDTH);
	int rgb_width = Tools_TextBoxPixelWidth(TOOLS_RGB_BOX_WIDTH);
	int total_width = hex_width + TOOLS_INFO_GAP + rgb_width;
	return (320 - total_width) / 2;
}

static int Tools_GetRgbBoxX(void)
{
	return Tools_GetHexBoxX()
		+ Tools_TextBoxPixelWidth(TOOLS_HEX_BOX_WIDTH)
		+ TOOLS_INFO_GAP;
}

static int Tools_GetInfoRowY(void)
{
	return TOOLS_INFO_Y;
}

static int Tools_GetHintY(void)
{
	return TOOLS_HINT_Y;
}

static plcolour_t Tools_ColorFromRGB(byte r, byte g, byte b)
{
	plcolour_t c;
	c.type = 2;
	c.rgb[0] = r;
	c.rgb[1] = g;
	c.rgb[2] = b;
	c.basic = 0;
	return c;
}

static qboolean Tools_PointInRect(int mx, int my, int x, int y, int w, int h)
{
	return (mx >= x && mx < x + w && my >= y && my < y + h);
}

static void Tools_DrawBackground(void)
{
	/* no background tint for color picker */
}

static qboolean Tools_MouseOverPicker(void)
{
	return Tools_PointInRect(m_mousex, m_mousey,
		Tools_GetPickerX(), Tools_GetPickerY(),
		TOOLS_PICKER_WIDTH, TOOLS_PICKER_HEIGHT);
}

static qboolean Tools_MouseOverSlider(void)
{
	return Tools_PointInRect(m_mousex, m_mousey,
		Tools_GetSliderX(), Tools_GetSliderY(),
		TOOLS_PICKER_WIDTH, TOOLS_SLIDER_HEIGHT);
}

static qboolean Tools_MouseOverHexBox(void)
{
	return Tools_PointInRect(m_mousex, m_mousey,
		Tools_GetHexBoxX(), Tools_GetInfoRowY(),
		Tools_TextBoxPixelWidth(TOOLS_HEX_BOX_WIDTH),
		Tools_TextBoxPixelHeight(1));
}

static qboolean Tools_MouseOverRgbBox(void)
{
	return Tools_PointInRect(m_mousex, m_mousey,
		Tools_GetRgbBoxX(), Tools_GetInfoRowY(),
		Tools_TextBoxPixelWidth(TOOLS_RGB_BOX_WIDTH),
		Tools_TextBoxPixelHeight(1));
}

static void Tools_RGBToHSV(byte r, byte g, byte b, float* h, float* s, float* v)
{
	float rf = r / 255.0f;
	float gf = g / 255.0f;
	float bf = b / 255.0f;
	float maxc = q_max(rf, q_max(gf, bf));
	float minc = q_min(rf, q_min(gf, bf));
	float delta = maxc - minc;

	*v = maxc;

	if (delta < 0.00001f)
	{
		*s = 0.0f;
		*h = 0.0f;
		return;
	}

	if (maxc > 0.0f)
		*s = delta / maxc;
	else
		*s = 0.0f;

	if (rf >= maxc)
		*h = (gf - bf) / delta;
	else if (gf >= maxc)
		*h = 2.0f + (bf - rf) / delta;
	else
		*h = 4.0f + (rf - gf) / delta;

	*h /= 6.0f;
	if (*h < 0.0f)
		*h += 1.0f;
}

static void Tools_SetColorFromRGB(byte r, byte g, byte b)
{
	float h, s, v;
	Tools_RGBToHSV(r, g, b, &h, &s, &v);
	toolsmenu.hue = Tools_WrapHue(h);
	toolsmenu.saturation = Tools_Clamp01(s);
	toolsmenu.value = Tools_Clamp01(v);
}

static void Tools_EnsureInitialized(void)
{
	if (toolsmenu.initialized)
		return;

	Tools_SetColorFromRGB(0x66, 0x23, 0x23);
	toolsmenu.focus = TOOLS_FOCUS_AREA;
	toolsmenu.initialized = true;
}

static void Tools_UpdateAreaFromMouse(int mx, int my)
{
	float sx = (mx - Tools_GetPickerX()) / (float)(TOOLS_PICKER_WIDTH - 1);
	float vy = 1.0f - (my - Tools_GetPickerY()) / (float)(TOOLS_PICKER_HEIGHT - 1);
	toolsmenu.saturation = Tools_Clamp01(sx);
	toolsmenu.value = Tools_Clamp01(vy);
}

static void Tools_UpdateHueFromMouse(int mx)
{
	float hue = (mx - Tools_GetSliderX()) / (float)(TOOLS_PICKER_WIDTH - 1);
	toolsmenu.hue = Tools_Clamp01(hue);
}

static void Tools_ColorToFloat4(plcolour_t color, float alpha, float out_rgba[4])
{
	if (color.type == 2)
	{
		out_rgba[0] = color.rgb[0] / 255.0f;
		out_rgba[1] = color.rgb[1] / 255.0f;
		out_rgba[2] = color.rgb[2] / 255.0f;
	}
	else
	{
		byte* pal = (byte*)&d_8to24table[(color.basic << 4) + 8];
		out_rgba[0] = pal[0] / 255.0f;
		out_rgba[1] = pal[1] / 255.0f;
		out_rgba[2] = pal[2] / 255.0f;
	}

	out_rgba[3] = alpha;
}

static void Tools_SetGLColour(plcolour_t color, float alpha)
{
	float r, g, b;

	if (color.type == 2)
	{
		r = color.rgb[0] / 255.0f;
		g = color.rgb[1] / 255.0f;
		b = color.rgb[2] / 255.0f;
	}
	else
	{
		byte* pal = (byte*)&d_8to24table[(color.basic << 4) + 8];
		r = pal[0] / 255.0f;
		g = pal[1] / 255.0f;
		b = pal[2] / 255.0f;
	}

	glColor4f(r, g, b, alpha);
}

static qboolean Tools_InitCircleShader(void)
{
	if (tools_circle_shader.attempted)
		return tools_circle_shader.ready;

	tools_circle_shader.attempted = true;

	if (!gl_glsl_able || !GL_CreateShaderFunc || !GL_ShaderSourceFunc
		|| !GL_CompileShaderFunc || !GL_GetShaderivFunc || !GL_CreateProgramFunc
		|| !GL_AttachShaderFunc || !GL_LinkProgramFunc || !GL_GetProgramivFunc
		|| !GL_DeleteShaderFunc || !GL_DeleteProgramFunc || !GL_UseProgramFunc
		|| !GL_GetUniformLocationFunc || !GL_Uniform4fFunc || !GL_Uniform1fFunc)
	{
		return false;
	}

	const GLchar* vert_source =
		"#version 110\n"
		"varying vec2 v_offset;\n"
		"void main()\n"
		"{\n"
		"\tgl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
		"\tv_offset = gl_MultiTexCoord0.xy;\n"
		"}\n";

	const GLchar* frag_source =
		"#version 110\n"
		"varying vec2 v_offset;\n"
		"uniform vec4 u_ring_color;\n"
		"uniform vec4 u_fill_color;\n"
		"uniform float u_radius;\n"
		"uniform float u_ring_width;\n"
		"uniform float u_aa_width;\n"
		"\n"
		"float band(float dist, float start, float end, float aa)\n"
		"{\n"
		"\tfloat a = smoothstep(start - aa, start + aa, dist);\n"
		"\tfloat b = smoothstep(end - aa, end + aa, dist);\n"
		"\treturn clamp(a - b, 0.0, 1.0);\n"
		"}\n"
		"\n"
		"void main()\n"
		"{\n"
		"\tfloat dist = length(v_offset);\n"
		"\tfloat ring_start = max(u_radius - u_ring_width, 0.0);\n"
		"\tfloat fill_radius = max(ring_start, 0.0);\n"
		"\n"
		"\tfloat ring_mask = band(dist, ring_start, u_radius, u_aa_width);\n"
		"\tfloat fill_mask = clamp(1.0 - smoothstep(fill_radius - u_aa_width,\n"
		"\t\tfill_radius + u_aa_width, dist), 0.0, 1.0);\n"
		"\n"
		"\tfloat total = ring_mask + fill_mask;\n"
		"\tif (total <= 0.0)\n"
		"\t\tdiscard;\n"
		"\n"
		"\tvec4 color = vec4(0.0);\n"
		"\tcolor += u_ring_color * ring_mask;\n"
		"\tcolor += u_fill_color * fill_mask;\n"
		"\n"
		"\tcolor.rgb /= max(total, 1e-5);\n"
		"\tcolor.a = clamp(total, 0.0, 1.0);\n"
		"\tgl_FragColor = color;\n"
		"}\n";

	GLuint vert = GL_CreateShaderFunc(GL_VERTEX_SHADER);
	GL_ShaderSourceFunc(vert, 1, &vert_source, NULL);
	GL_CompileShaderFunc(vert);

	GLint compiled = GL_FALSE;
	GL_GetShaderivFunc(vert, GL_COMPILE_STATUS, &compiled);
	if (!compiled)
	{
		GL_DeleteShaderFunc(vert);
		return false;
	}

	GLuint frag = GL_CreateShaderFunc(GL_FRAGMENT_SHADER);
	GL_ShaderSourceFunc(frag, 1, &frag_source, NULL);
	GL_CompileShaderFunc(frag);
	GL_GetShaderivFunc(frag, GL_COMPILE_STATUS, &compiled);
	if (!compiled)
	{
		GL_DeleteShaderFunc(vert);
		GL_DeleteShaderFunc(frag);
		return false;
	}

	GLuint program = GL_CreateProgramFunc();
	GL_AttachShaderFunc(program, vert);
	GL_AttachShaderFunc(program, frag);
	GL_LinkProgramFunc(program);

	GLint linked = GL_FALSE;
	GL_GetProgramivFunc(program, GL_LINK_STATUS, &linked);
	if (!linked)
	{
		GL_DeleteShaderFunc(vert);
		GL_DeleteShaderFunc(frag);
		GL_DeleteProgramFunc(program);
		return false;
	}

	GL_DeleteShaderFunc(vert);
	GL_DeleteShaderFunc(frag);

	tools_circle_shader.program = program;
	tools_circle_shader.u_ring_color = GL_GetUniformLocationFunc(program, "u_ring_color");
	tools_circle_shader.u_fill_color = GL_GetUniformLocationFunc(program, "u_fill_color");
	tools_circle_shader.u_radius = GL_GetUniformLocationFunc(program, "u_radius");
	tools_circle_shader.u_ring_width = GL_GetUniformLocationFunc(program, "u_ring_width");
	tools_circle_shader.u_aa_width = GL_GetUniformLocationFunc(program, "u_aa_width");

	tools_circle_shader.ready = tools_circle_shader.u_ring_color >= 0
		&& tools_circle_shader.u_fill_color >= 0
		&& tools_circle_shader.u_radius >= 0
		&& tools_circle_shader.u_ring_width >= 0
		&& tools_circle_shader.u_aa_width >= 0;

	if (!tools_circle_shader.ready)
	{
		GL_DeleteProgramFunc(program);
		memset(&tools_circle_shader, 0, sizeof(tools_circle_shader));
		tools_circle_shader.attempted = true;
		tools_circle_shader.ready = false;
		return false;
	}

	return true;
}

static qboolean Tools_DrawPickerMarkerWithShader(float center_x, float center_y, int radius,
	plcolour_t ring, plcolour_t fill)
{
	if (!tools_circle_shader.ready && !Tools_InitCircleShader())
		return false;

	float ring_rgba[4];
	float fill_rgba[4];
	Tools_ColorToFloat4(ring, 1.0f, ring_rgba);
	Tools_ColorToFloat4(fill, 1.0f, fill_rgba);

	float radius_f = (float)radius;
	float ring_width = q_max(0.5f, radius_f * 0.10f);
	float aa_width = 0.25f;
	float extent = radius_f + ring_width + aa_width;

	glPushAttrib(GL_ENABLE_BIT | GL_LINE_BIT | GL_POINT_BIT | GL_COLOR_BUFFER_BIT | GL_HINT_BIT);

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_ALPHA_TEST);

	GL_UseProgramFunc(tools_circle_shader.program);
	GL_Uniform4fFunc(tools_circle_shader.u_ring_color,
		ring_rgba[0], ring_rgba[1], ring_rgba[2], ring_rgba[3]);
	GL_Uniform4fFunc(tools_circle_shader.u_fill_color,
		fill_rgba[0], fill_rgba[1], fill_rgba[2], fill_rgba[3]);
	GL_Uniform1fFunc(tools_circle_shader.u_radius, radius_f);
	GL_Uniform1fFunc(tools_circle_shader.u_ring_width, ring_width);
	GL_Uniform1fFunc(tools_circle_shader.u_aa_width, aa_width);

	glBegin(GL_TRIANGLE_STRIP);

	glTexCoord2f(-extent, -extent);
	glVertex2f(center_x - extent, center_y - extent);

	glTexCoord2f(extent, -extent);
	glVertex2f(center_x + extent, center_y - extent);

	glTexCoord2f(-extent, extent);
	glVertex2f(center_x - extent, center_y + extent);

	glTexCoord2f(extent, extent);
	glVertex2f(center_x + extent, center_y + extent);

	glEnd();

	GL_UseProgramFunc(0);

	glPopAttrib();
	return true;
}

static void Tools_DrawPickerMarker(float center_x, float center_y, int radius,
	plcolour_t ring, plcolour_t fill)
{
	if (radius <= 0)
		return;

	if (Tools_DrawPickerMarkerWithShader(center_x, center_y, radius, ring, fill))
		return;

	glPushAttrib(GL_ENABLE_BIT | GL_LINE_BIT | GL_POINT_BIT | GL_COLOR_BUFFER_BIT | GL_HINT_BIT);

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_ALPHA_TEST);
	Tools_SetGLColour(ring, 1.0f);
	renderCircle(center_x, center_y, (float)radius, 128, 1.0f);

	float fill_size = q_max(1.0f, (float)(radius - 1) * 1.0f);
	Tools_SetGLColour(fill, 1.0f);
	renderSmoothDot(center_x, center_y, fill_size);

	glPopAttrib();
}

static void Tools_DrawPicker(const byte current_rgb[3])
{
	int picker_x = Tools_GetPickerX();
	int picker_y = Tools_GetPickerY();

	for (int y = 0; y < TOOLS_PICKER_HEIGHT; y += 2)
	{
		float value = 1.0f - (float)y / (float)(TOOLS_PICKER_HEIGHT - 1);
		int draw_h = q_min(2, TOOLS_PICKER_HEIGHT - y);

		for (int x = 0; x < TOOLS_PICKER_WIDTH; x += 2)
		{
			float saturation = (float)x / (float)(TOOLS_PICKER_WIDTH - 1);
			byte rgb[3];
			hsvtorgb(toolsmenu.hue, saturation, value, rgb);

			Draw_FillPlayer(picker_x + x,
				picker_y + y,
				q_min(2, TOOLS_PICKER_WIDTH - x),
				draw_h,
				Tools_ColorFromRGB(rgb[0], rgb[1], rgb[2]),
				1);
		}
	}

	int marker_x = picker_x + (int)(toolsmenu.saturation * (TOOLS_PICKER_WIDTH - 1) + 0.5f);
	int marker_y = picker_y + (int)((1.0f - toolsmenu.value) * (TOOLS_PICKER_HEIGHT - 1) + 0.5f);
	marker_x = q_max(picker_x, q_min(marker_x, picker_x + TOOLS_PICKER_WIDTH - 1));
	marker_y = q_max(picker_y, q_min(marker_y, picker_y + TOOLS_PICKER_HEIGHT - 1));

	int brightness = (current_rgb[0] + current_rgb[1] + current_rgb[2]) / 3;
	plcolour_t ring = brightness > 140
		? Tools_ColorFromRGB(0, 0, 0)
		: Tools_ColorFromRGB(255, 255, 255);

	int marker_radius = 4;
	plcolour_t fill = Tools_ColorFromRGB(current_rgb[0], current_rgb[1], current_rgb[2]);
	Tools_DrawPickerMarker(marker_x + 0.5f, marker_y + 0.5f, marker_radius,
		ring, fill);
}

static void Tools_DrawHueSlider(void)
{
	int slider_x = Tools_GetSliderX();
	int slider_y = Tools_GetSliderY();

	for (int x = 0; x < TOOLS_PICKER_WIDTH; x += 2)
	{
		float hue = (float)x / (float)(TOOLS_PICKER_WIDTH - 1);
		byte rgb[3];
		hsvtorgb(hue, 1.0f, 1.0f, rgb);

		Draw_FillPlayer(slider_x + x,
			slider_y,
			q_min(2, TOOLS_PICKER_WIDTH - x),
			TOOLS_SLIDER_HEIGHT,
			Tools_ColorFromRGB(rgb[0], rgb[1], rgb[2]),
			1);
	}

	int hue_x = slider_x + (int)(toolsmenu.hue * (TOOLS_PICKER_WIDTH - 1) + 0.5f);
	hue_x = q_max(slider_x, q_min(hue_x, slider_x + TOOLS_PICKER_WIDTH - 1));
	{
		byte hrgb[3];
		hsvtorgb(toolsmenu.hue, 1.0f, 1.0f, hrgb);
		plcolour_t fill = Tools_ColorFromRGB(hrgb[0], hrgb[1], hrgb[2]);
		int brightness = (hrgb[0] + hrgb[1] + hrgb[2]) / 3;
		plcolour_t ring = brightness > 140
			? Tools_ColorFromRGB(0, 0, 0)
			: Tools_ColorFromRGB(255, 255, 255);

		float center_x = hue_x + 0.5f;
		float center_y = slider_y + (TOOLS_SLIDER_HEIGHT / 2.0f);
		Tools_DrawPickerMarker(center_x, center_y, 4, ring, fill);
	}
}

static void Tools_DrawSwatch(const byte rgb[3])
{
	int swatch_x = Tools_GetSwatchX();
	int swatch_y = Tools_GetPickerY();
	int swatch_h = TOOLS_PICKER_HEIGHT;

	Draw_FillPlayer(swatch_x, swatch_y, TOOLS_SWATCH_WIDTH, swatch_h,
		Tools_ColorFromRGB(rgb[0], rgb[1], rgb[2]), 1);
}

static void Tools_DrawInfo(const byte rgb[3])
{
	char hex_line[32];
	char rgb_line[32];

	q_snprintf(hex_line, sizeof(hex_line), "HEX #%02X%02X%02X", rgb[0], rgb[1], rgb[2]);
	q_snprintf(rgb_line, sizeof(rgb_line), "RGB %d, %d, %d", rgb[0], rgb[1], rgb[2]);

	int info_y = Tools_GetInfoRowY();
	int hex_box_x = Tools_GetHexBoxX();
	int rgb_box_x = Tools_GetRgbBoxX();

	M_DrawTextBox(hex_box_x, info_y, TOOLS_HEX_BOX_WIDTH, 1);
	if (realtime < toolsmenu_hex_flash_until)
		M_PrintWhite(hex_box_x + 8, info_y + 8, hex_line);
	else
		M_Print(hex_box_x + 8, info_y + 8, hex_line);

	M_DrawTextBox(rgb_box_x, info_y, TOOLS_RGB_BOX_WIDTH, 1);
	if (realtime < toolsmenu_rgb_flash_until)
		M_PrintWhite(rgb_box_x + 8, info_y + 8, rgb_line);
	else
		M_Print(rgb_box_x + 8, info_y + 8, rgb_line);

	const char* hint = "Click boxes above to copy";
	int hint_x = (320 - (int)strlen(hint) * 8) / 2;
	M_Print(hint_x, Tools_GetHintY(), hint);
}

static void Tools_CopyHexToClipboard(void)
{
	byte rgb[3];
	hsvtorgb(toolsmenu.hue, toolsmenu.saturation, toolsmenu.value, rgb);
	char hex_line[16];
	q_snprintf(hex_line, sizeof(hex_line), "#%02X%02X%02X", rgb[0], rgb[1], rgb[2]);
	SDL_SetClipboardText(hex_line);
	toolsmenu_hex_flash_until = realtime + 1.0;
	{
		const char* soundFile = COM_FileExists("sound/qssm/copy.wav", NULL) ? "qssm/copy.wav" : "player/tornoff2.wav";
		S_LocalSound(soundFile);
	}
}

static void Tools_CopyRgbToClipboard(void)
{
	byte rgb[3];
	hsvtorgb(toolsmenu.hue, toolsmenu.saturation, toolsmenu.value, rgb);
	char buf[32];
	q_snprintf(buf, sizeof(buf), "%d,%d,%d", rgb[0], rgb[1], rgb[2]);
	SDL_SetClipboardText(buf);
	toolsmenu_rgb_flash_until = realtime + 1.0;
	{
		const char* soundFile = COM_FileExists("sound/qssm/copy.wav", NULL) ? "qssm/copy.wav" : "player/tornoff2.wav";
		S_LocalSound(soundFile);
	}
}

void M_Menu_ColorPicker_f(void)
{
	key_dest = key_menu;
	m_state = m_colorpicker;
	m_entersound = true;

	if (!colorpicker_return_fn)
		colorpicker_return_fn = M_Menu_Options_f;

	Tools_EnsureInitialized();
	/* Non-crosshair callers start from a fresh random hue */
	if (colorpicker_return_fn != M_Menu_Crosshair_f)
	{
		toolsmenu.hue = (float)rand() / (float)RAND_MAX;
		toolsmenu.saturation = 1.0f;
		toolsmenu.value = 1.0f;
	}
	/* If opened from setup or other menus, keep current hue/sat/val; if opened from crosshair, it was seeded before entry */
	toolsmenu.dragging_area = false;
	toolsmenu.dragging_hue = false;
	toolsmenu_hex_flash_until = 0;
	toolsmenu_rgb_flash_until = 0;

	IN_UpdateGrabs();
}

void M_ColorPicker_Draw(void)
{
	Tools_DrawBackground();

	byte rgb[3];
	hsvtorgb(toolsmenu.hue, toolsmenu.saturation, toolsmenu.value, rgb);

	Tools_DrawSwatch(rgb);
	Tools_DrawPicker(rgb);
	Tools_DrawHueSlider();
	Tools_DrawInfo(rgb);
}

void M_ColorPicker_Key(int k)
{
	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		ColorPicker_ReturnToParent();
		break;

	case K_LEFTARROW:
		if (toolsmenu.focus == TOOLS_FOCUS_AREA)
			toolsmenu.saturation = Tools_Clamp01(toolsmenu.saturation - TOOLS_SAT_STEP);
		else
			toolsmenu.hue = Tools_WrapHue(toolsmenu.hue - TOOLS_HUE_STEP);
		S_LocalSound("misc/menu3.wav");
		break;

	case K_RIGHTARROW:
		if (toolsmenu.focus == TOOLS_FOCUS_AREA)
			toolsmenu.saturation = Tools_Clamp01(toolsmenu.saturation + TOOLS_SAT_STEP);
		else
			toolsmenu.hue = Tools_WrapHue(toolsmenu.hue + TOOLS_HUE_STEP);
		S_LocalSound("misc/menu3.wav");
		break;

	case K_MWHEELDOWN:
		if (Tools_MouseOverPicker())
		{
			toolsmenu.focus = TOOLS_FOCUS_AREA;
			toolsmenu.saturation = Tools_Clamp01(toolsmenu.saturation - TOOLS_SAT_STEP);
			S_LocalSound("misc/menu3.wav");
		}
		else if (Tools_MouseOverSlider())
		{
			toolsmenu.focus = TOOLS_FOCUS_HUE;
			toolsmenu.hue = Tools_WrapHue(toolsmenu.hue - TOOLS_HUE_STEP);
			S_LocalSound("misc/menu3.wav");
		}
		break;

	case K_MWHEELUP:
		if (Tools_MouseOverPicker())
		{
			toolsmenu.focus = TOOLS_FOCUS_AREA;
			toolsmenu.saturation = Tools_Clamp01(toolsmenu.saturation + TOOLS_SAT_STEP);
			S_LocalSound("misc/menu3.wav");
		}
		else if (Tools_MouseOverSlider())
		{
			toolsmenu.focus = TOOLS_FOCUS_HUE;
			toolsmenu.hue = Tools_WrapHue(toolsmenu.hue + TOOLS_HUE_STEP);
			S_LocalSound("misc/menu3.wav");
		}
		break;

	case K_UPARROW:
		if (toolsmenu.focus == TOOLS_FOCUS_AREA)
			toolsmenu.value = Tools_Clamp01(toolsmenu.value + TOOLS_VAL_STEP);
		else
			toolsmenu.hue = Tools_WrapHue(toolsmenu.hue + TOOLS_HUE_STEP);
		S_LocalSound("misc/menu3.wav");
		break;

	case K_DOWNARROW:
		if (toolsmenu.focus == TOOLS_FOCUS_AREA)
			toolsmenu.value = Tools_Clamp01(toolsmenu.value - TOOLS_VAL_STEP);
		else
			toolsmenu.hue = Tools_WrapHue(toolsmenu.hue - TOOLS_HUE_STEP);
		S_LocalSound("misc/menu3.wav");
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		Tools_CopyHexToClipboard();
		S_LocalSound("misc/menu1.wav");
		break;

	case K_MOUSE1:
		if (Tools_MouseOverPicker())
		{
			Tools_UpdateAreaFromMouse(m_mousex, m_mousey);
			toolsmenu.dragging_area = true;
			toolsmenu.focus = TOOLS_FOCUS_AREA;
			S_LocalSound("misc/menu3.wav");
		}
		else if (Tools_MouseOverSlider())
		{
			Tools_UpdateHueFromMouse(m_mousex);
			toolsmenu.dragging_hue = true;
			toolsmenu.focus = TOOLS_FOCUS_HUE;
			S_LocalSound("misc/menu3.wav");
		}
		else if (Tools_MouseOverHexBox())
		{
			Tools_CopyHexToClipboard();
		}
		else if (Tools_MouseOverRgbBox())
			Tools_CopyRgbToClipboard();
		break;
	}
}

void M_ColorPicker_Mousemove(int cx, int cy)
{
	if (toolsmenu.dragging_area)
	{
		if (!keydown[K_MOUSE1])
		{
			toolsmenu.dragging_area = false;
			return;
		}

		Tools_UpdateAreaFromMouse(cx, cy);
		return;
	}

	if (toolsmenu.dragging_hue)
	{
		if (!keydown[K_MOUSE1])
		{
			toolsmenu.dragging_hue = false;
			return;
		}

		Tools_UpdateHueFromMouse(cx);
	}
}

/*
==================
Misc Menu
==================
*/

extern cvar_t pr_checkextension, r_replacemodels, gl_load24bit, cl_nopext, r_lerpmodels, r_lerpmove,
sys_throttle, r_particles, sv_nqplayerphysics, cl_nopred, cl_autodemo, cl_smartspawn, cl_bobbing, cl_onload,
cl_pong, scr_hints, cl_portpingprobe_enable, sv_autoload, sv_autosave, sv_autosave_interval;

static enum extras_e
{
	EXTRAS_YIELD,
	EXTRAS_NETEXTENSIONS,
	EXTRAS_QCEXTENSIONS,
	EXTRAS_PREDICTION,
	EXTRAS_AUTODEMO,
	EXTRAS_PORTPINGPROBE,
	EXTRAS_SPAWNTRAINER,
	EXTRAS_ITEMBOB,
	EXTRAS_RESETCONFIG,
	EXTRAS_PONG,
	EXTRAS_HINTS,
	EXTRAS_LIVEPREVIEW,
	EXTRAS_MODELVIEWER,
	EXTRAS_SAVING,
	EXTRAS_SHORTCUTS,
	EXTRAS_VERSION,
	EXTRAS_COUNT
} extras_cursor;

#define EXTRAS_ITEMS (EXTRAS_COUNT)

int numberOfExtrasItems = EXTRAS_ITEMS; // woods #mousemenu

static struct
{
	int cursor;
	struct {
		char text[32];
		int len;
	} search;
} extrasmenu;

static const char* M_Extras_GetItemText(int index) // Add this helper function
{
	static char buffer[64];

	switch (index)
	{
	case EXTRAS_YIELD:
		return "System Throttle";
	case EXTRAS_NETEXTENSIONS:
		return "Protocol Exts";
	case EXTRAS_QCEXTENSIONS:
		return "QC Extensions";
	case EXTRAS_PREDICTION:
		return "Prediction";
	case EXTRAS_AUTODEMO:
		return "Auto Demo";
	case EXTRAS_PORTPINGPROBE:
		return "Port Ping Probe";
	case EXTRAS_SPAWNTRAINER:
		return "Spawn Trainer";
	case EXTRAS_ITEMBOB:
		return "Q3 Item Bobbing";
	case EXTRAS_RESETCONFIG:
		return "Reset Config";
	case EXTRAS_PONG:
		return "Quake Pong";
	case EXTRAS_HINTS:
		return "Paused Hints";
	case EXTRAS_LIVEPREVIEW:
		return "Live Preview";
	case EXTRAS_MODELVIEWER:
		return "Model Viewer";
	case EXTRAS_SAVING:
		return "Saving";
	case EXTRAS_SHORTCUTS:
		return "Keyboard Shortcuts";
	case EXTRAS_VERSION:
		return "Version Info";
	default:
		q_snprintf(buffer, sizeof(buffer), "Unknown Item %d", index);
		return buffer;
	}
}

static void M_Extras_UpdateSearch(void)
{
	extras_cursor = (enum extras_e)M_Menu_UpdateSearchCursor(
		EXTRAS_ITEMS, (int)extras_cursor, &numberOfExtrasItems,
		M_Extras_GetItemText, extrasmenu.search.text, extrasmenu.search.len);
}

static void M_Extras_MoveCursor(int delta)
{
	extras_cursor = (enum extras_e)M_Menu_MoveSearchCursor(
		EXTRAS_ITEMS, numberOfExtrasItems, (int)extras_cursor, delta,
		M_Extras_GetItemText, extrasmenu.search.text, extrasmenu.search.len);
}

static int M_Extras_RowY(int item)
{
	return 48 + item * 8;
}

#define EXTRAS_SEARCH_BOX_Y	176
#define EXTRAS_SEARCH_TEXT_Y	184

static int M_Extras_LivePreviewId(void)
{
	switch (extras_cursor)
	{
	case EXTRAS_ITEMBOB:
		return CL_ViewingQ3ItemBobbingItem () ? LP_ITEMBOB : LP_NONE;
	case EXTRAS_PONG:
		return cls.demoplayback ? LP_NONE : LP_PONG;
	case EXTRAS_HINTS:
		return cls.demoplayback ? LP_NONE : LP_HINTS;
	default:
		return LP_NONE;
	}
}

void M_Menu_Extras_f(void)
{
	key_dest = key_menu;
	m_state = m_extras;
	m_entersound = true;
	extras_cursor = 0;
	extrasmenu.cursor = 0;
	extrasmenu.search.len = 0;
	extrasmenu.search.text[0] = 0;
	numberOfExtrasItems = EXTRAS_ITEMS;
	M_LivePreview_Reset();

	IN_UpdateGrabs();
}

static void M_Extras_AdjustSliders (int dir)
{
	int m;
	S_LocalSound ("misc/menu3.wav");

	switch (extras_cursor)
	{
	case EXTRAS_YIELD:
		if (fabs(sys_throttle.value - 0.02) < 0.001)      // Check if close to 0.02
			Cvar_SetValue("sys_throttle", -1);
		else if (sys_throttle.value < -0.9)               // Check if it's -1
			Cvar_SetValue("sys_throttle", 0);
		else
			Cvar_SetValue("sys_throttle", 0.02);
		break;
	case EXTRAS_NETEXTENSIONS:
		Cvar_SetValueQuick (&cl_nopext, !cl_nopext.value);
		break;
	case EXTRAS_QCEXTENSIONS:
		Cvar_SetValueQuick (&pr_checkextension, !pr_checkextension.value);
		break;
	case EXTRAS_PREDICTION:
		m = ((!!cl_nopred.value)<<1)|(!!sv_nqplayerphysics.value);
		m += dir;
		if ((m&3)==2)
			m += dir; //boo! don't like that combo. skip it
		m &= 3;
		Cvar_SetValueQuick (&cl_nopred, (m>>1)&1);
		Cvar_SetValueQuick (&sv_nqplayerphysics, (m>>0)&1);
		break;
	case EXTRAS_AUTODEMO:
		m = cl_autodemo.value + dir;
		if (m < 0) m = 4;
		if (m > 4) m = 0;
		Cvar_SetValue("cl_autodemo", m);
		break;
	case EXTRAS_PORTPINGPROBE:
		Cvar_SetValueQuick(&cl_portpingprobe_enable, !cl_portpingprobe_enable.value);
		break;
	case EXTRAS_SPAWNTRAINER:
		Cvar_SetValue("cl_smartspawn", !cl_smartspawn.value);
		break;
	case EXTRAS_ITEMBOB:
		M_LivePreview_WantAndKick (M_Extras_LivePreviewId (), M_Extras_RowY (extras_cursor));
		Cvar_SetValue("cl_bobbing", !cl_bobbing.value);
		break;
	case EXTRAS_RESETCONFIG:
		if (!SCR_ModalMessage("Are you sure you want to\nreset your configuration?\n (^mn^m/^my^m)\n", 0.0f))
			break;
		// Execute config reset commands
		Cbuf_AddText("resetcfg\n");              // Reset archived cvars to defaults
		Cbuf_AddText("writeconfig config.cfg\n"); // Persist reset config to disk
		M_Menu_Options_f();           // Return to Options menu
		break;
	case EXTRAS_PONG: // Added Quake Pong toggle
		Cvar_SetValueQuick(&cl_pong, !cl_pong.value);
		if (cl_pong.value)
			M_LivePreview_WantAndKick (M_Extras_LivePreviewId (), M_Extras_RowY (extras_cursor));
		else
			M_LivePreview_Reset();
		break;
	case EXTRAS_HINTS: // Added Paused Hints toggle
		Cvar_SetValueQuick(&scr_hints, !scr_hints.value);
		if (scr_hints.value)
			M_LivePreview_WantAndKick (M_Extras_LivePreviewId (), M_Extras_RowY (extras_cursor));
		else
			M_LivePreview_Reset();
		break;
	case EXTRAS_LIVEPREVIEW:
		Cvar_SetValueQuick(&ui_live_preview, !ui_live_preview.value);
		if (!ui_live_preview.value)
			M_LivePreview_Reset();
		break;
	case EXTRAS_MODELVIEWER:
		M_Menu_ModelViewer_f();
		break;
	case EXTRAS_SAVING:
		M_Menu_Saving_f();
		break;
	case EXTRAS_SHORTCUTS:
		M_Menu_Shortcuts_f();
		break;
	case EXTRAS_VERSION:
		M_Menu_Version_f();
		break;
	case EXTRAS_ITEMS:	//not a real option
		break;
	}
}

void M_Extras_Draw(void)
{
	qpic_t* p;
	enum extras_e i;

	extras_cursor = (enum extras_e)M_Menu_ClampCursorValue((int)extras_cursor, EXTRAS_ITEMS);

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	const char* title = "Miscellaneous Options";
	M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);

	M_LivePreview_WantAt (M_Extras_LivePreviewId (), M_Extras_RowY (extras_cursor));

	for (i = 0; i < EXTRAS_ITEMS; i++)
	{
		int y = M_Extras_RowY (i);
		qboolean isolated = M_LivePreview_IsolateY (y);
		const char* text = NULL;
		const char* value = NULL;

		if (isolated)
			M_LivePreview_BeginIsolate ();

		switch (i)
		{
		case EXTRAS_YIELD:
			text = "   System Throttle";
			if (fabs(sys_throttle.value - 0.02) < 0.001)
				value = "on";
			else if (sys_throttle.value == 0)
				value = "when idle";
			else if (sys_throttle.value < -0.9)
				value = "off+when minimized";
			else
				value = "unknown";
			break;


		case EXTRAS_NETEXTENSIONS:
			text = "     Protocol Exts";
			value = cl_nopext.value ? "blocked" : "enabled";
			break;

		case EXTRAS_QCEXTENSIONS:
			text = "     QC Extensions";
			value = pr_checkextension.value ? "enabled" : "blocked";
			break;

		case EXTRAS_PREDICTION:
			text = "        Prediction";
			if (!cl_nopred.value && !sv_nqplayerphysics.value)
				value = "on (override ssqc)";
			else if (!cl_nopred.value && sv_nqplayerphysics.value)
				value = "on (compat phys)";
			else if (cl_nopred.value && !sv_nqplayerphysics.value)
				value = "off (override ssqc)";
			else
				value = "off";
			break;

		case EXTRAS_AUTODEMO:
			text = "         Auto Demo";
			switch ((int)cl_autodemo.value)
			{
			case 0: value = "off"; break;
			case 1: value = "all maps"; break;
			case 2: value = "crmod matches only"; break;
			case 3: value = "all maps (online)"; break;
			case 4: value = "all maps (split)"; break;
			default: value = "unknown"; break;
			}
			break;

		case EXTRAS_PORTPINGPROBE:
			text = "   Port Ping Probe";
			value = cl_portpingprobe_enable.value ? "on" : "off";
			break;

		case EXTRAS_SPAWNTRAINER:
			text = "     Spawn Trainer";
			value = cl_smartspawn.value ? "on (jump only)" : "off (jump or fire)";
			break;

		case EXTRAS_ITEMBOB:
			text = "   Q3 Item Bobbing";
			value = cl_bobbing.value ? "on" : "off";
			break;

		case EXTRAS_RESETCONFIG:
			text = "      Reset Config";
			value = "confirm";
			break;


		case EXTRAS_PONG: // Added Quake Pong display
			text = "        Quake Pong";
			value = cl_pong.value ? "on" : "off";
			break;

		case EXTRAS_HINTS: // Added Paused Hints display
			text = "      Paused Hints";
			value = scr_hints.value ? "on" : "off";
			break;

		case EXTRAS_LIVEPREVIEW:
			text = "      Live Preview";
			value = ui_live_preview.value ? "on" : "off";
			break;

		case EXTRAS_MODELVIEWER:
			text = "      Model Viewer";
			value = "...";
			break;

		case EXTRAS_SAVING:
			text = "            Saving";
			value = "...";
			break;

		case EXTRAS_SHORTCUTS:
			text = "Keyboard Shortcuts";
			value = "...";
			break;

		case EXTRAS_VERSION:
			text = "      Version Info";
			value = "...";
			break;

		default:
			break;
		}

		if (text)
		{
			if (extrasmenu.search.len > 0 &&
				q_strcasestr(text, extrasmenu.search.text))
			{
				M_PrintHighlight(8, y, text,
					extrasmenu.search.text,
					extrasmenu.search.len);
			}
			else
			{
				M_Print(8, y, text);
			}

			M_Print(168, y, value);
		}

		if (isolated)
			M_LivePreview_EndIsolate ();
	}

	// Draw cursor
	{
		int y = M_Extras_RowY (extras_cursor);
		qboolean isolated = M_LivePreview_IsolateY (y);
		if (isolated)
			M_LivePreview_BeginIsolate ();
		M_DrawCharacter(160, y, 12 + ((int)(realtime * 4) & 1));
		if (isolated)
			M_LivePreview_EndIsolate ();
	}

	// Draw search box if search is active
	if (extrasmenu.search.len > 0)
	{
		M_DrawTextBox(16, EXTRAS_SEARCH_BOX_Y, 32, 1);
		M_PrintHighlight(24, EXTRAS_SEARCH_TEXT_Y, extrasmenu.search.text,
			extrasmenu.search.text,
			extrasmenu.search.len);
		int cursor_x = 24 + 8 * extrasmenu.search.len;
		if (numberOfExtrasItems == 0)
			M_DrawCharacter(cursor_x, EXTRAS_SEARCH_TEXT_Y, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, EXTRAS_SEARCH_TEXT_Y, 10 + ((int)(realtime * 4) & 1));
	}
}

void M_Extras_Key(int k)
{
	if (k == K_ESCAPE)
	{
		if (extrasmenu.search.len > 0)
		{
			extrasmenu.search.len = 0;
			extrasmenu.search.text[0] = 0;
			M_Extras_UpdateSearch();
			return;
		}
		if (M_LivePreview_Alpha() > 0.f)
		{
			M_LivePreview_Reset();
			return;
		}
		M_Menu_Options_f();
		return;
	}
	else if (keydown[K_CTRL])
	{
		if ((k == 'u' || k == 'U') && extrasmenu.search.len > 0)
		{
			// Clear entire search with Ctrl+U
			extrasmenu.search.len = 0;
			extrasmenu.search.text[0] = 0;
			M_Extras_UpdateSearch();
			return;
		}
		else if (k == K_BACKSPACE && extrasmenu.search.len > 0)
		{
			// Delete previous word with Ctrl+Backspace
			listsearch_t temp;
			temp.len = extrasmenu.search.len;
			Q_strcpy(temp.text, extrasmenu.search.text);
			M_DeletePrevWord(&temp);
			Q_strcpy(extrasmenu.search.text, temp.text);
			extrasmenu.search.len = temp.len;
			M_Extras_UpdateSearch();
			return;
		}
	}
	else if (k == K_BACKSPACE)
	{
		if (extrasmenu.search.len > 0)
		{
			extrasmenu.search.text[--extrasmenu.search.len] = 0;
			M_Extras_UpdateSearch();
			return;
		}
	}
	else if (k >= 32 && k < 127)
	{
		if (extrasmenu.search.len < sizeof(extrasmenu.search.text) - 1)
		{
			extrasmenu.search.text[extrasmenu.search.len++] = k;
			extrasmenu.search.text[extrasmenu.search.len] = 0;
			M_Extras_UpdateSearch();
			return;
		}
	}

	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		if (M_LivePreview_Alpha() > 0.f)
		{
			M_LivePreview_Reset();
			return;
		}
		M_Menu_Options_f();
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1:
		m_entersound = true;
		M_Extras_AdjustSliders(1);
		break;

	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		M_Extras_MoveCursor(-1);
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		M_Extras_MoveCursor(1);
		break;

	case K_LEFTARROW:
		M_Extras_AdjustSliders(-1);
		break;

	case K_MWHEELDOWN:
		if (extras_cursor != EXTRAS_MODELVIEWER && extras_cursor != EXTRAS_SAVING &&
			extras_cursor != EXTRAS_SHORTCUTS && extras_cursor != EXTRAS_VERSION)
			M_Extras_AdjustSliders(-1);
		break;

	case K_RIGHTARROW:
		M_Extras_AdjustSliders(1);
		break;

	case K_MWHEELUP:
		if (extras_cursor != EXTRAS_MODELVIEWER && extras_cursor != EXTRAS_SAVING &&
			extras_cursor != EXTRAS_SHORTCUTS && extras_cursor != EXTRAS_VERSION)
			M_Extras_AdjustSliders(1);
		break;
	}
}

void M_Extras_Mousemove(int cx, int cy)
{
	// Don't process mouse movement if it's in the search box area
	if (extrasmenu.search.len > 0 && cy >= EXTRAS_SEARCH_BOX_Y)
		return;

	// Calculate which menu item the mouse is over
	int item = (cy - 48) / 8;

	// Make sure the item is within valid range and mouse is in the menu area
	if (item >= 0 && item < EXTRAS_ITEMS && cy >= 48 && cy < 48 + (EXTRAS_ITEMS * 8))
	{
		// Update cursor position regardless of search state
		extras_cursor = item;
	}
}

/*
==================
Saving Menu
==================
*/

enum saving_e
{
	SAVING_AUTOLOAD,
	SAVING_AUTOSAVE,
	SAVING_AUTOSAVE_INTERVAL,
	SAVING_INDICATOR,
	SAVING_ITEMS
} saving_cursor;

#define SAVING_ROW_Y(item) (48 + (item) * 8)
#define SAVING_AUTOSAVE_INTERVAL_DEFAULT	30
#define SAVING_AUTOSAVE_INTERVAL_STEP		5

static int M_Saving_AutoloadMode(void)
{
	if (sv_autoload.value <= 0.f)
		return 0;
	if (sv_autoload.value < 2.f)
		return 1;
	if (sv_autoload.value < 3.f)
		return 2;
	return 3;
}

static const char* M_Saving_AutoloadText(void)
{
	switch (M_Saving_AutoloadMode())
	{
	case 0:
		return "off";
	case 1:
		return "prompt";
	case 2:
		return "death only";
	case 3:
		return "always";
	default:
		return "unknown";
	}
}

static const char* M_Saving_AutosaveText(void)
{
	if (!sv_autosave.value)
		return "off";
	if (fabsf(sv_autosave_interval.value) <= 0.f)
		return "on, interval off";
	return "on";
}

static float M_Saving_IntervalValue(void)
{
	return fabsf(sv_autosave_interval.value);
}

static qboolean M_Saving_IndicatorEnabled(void)
{
	return sv_autosave_interval.value < 0.f;
}

static void M_Saving_SetIntervalValue(float interval)
{
	if (interval > 0.f && M_Saving_IndicatorEnabled())
		interval = -interval;
	Cvar_SetValueQuick(&sv_autosave_interval, interval);
}

static void M_Saving_SetIndicator(qboolean enabled)
{
	float interval = M_Saving_IntervalValue();

	if (interval <= 0.f)
		interval = SAVING_AUTOSAVE_INTERVAL_DEFAULT;

	Cvar_SetValueQuick(&sv_autosave_interval, enabled ? -interval : interval);
}

static const char* M_Saving_IntervalText(void)
{
	float interval = M_Saving_IntervalValue();
	int seconds;

	if (interval <= 0.f)
		return "off";

	seconds = Q_rint(interval);
	if ((float)seconds == interval)
		return va("%d sec", seconds);

	return va("%.1f sec", interval);
}

static void M_Saving_MoveCursor(int delta)
{
	saving_cursor = (enum saving_e)M_Menu_ClampCursorValue((int)saving_cursor + delta, SAVING_ITEMS);
}

void M_Menu_Saving_f(void)
{
	key_dest = key_menu;
	m_state = m_saving;
	m_entersound = true;
	saving_cursor = 0;

	IN_UpdateGrabs();
}

static void M_Saving_AdjustSliders(int dir)
{
	float interval;
	int m;

	S_LocalSound("misc/menu3.wav");

	switch (saving_cursor)
	{
	case SAVING_AUTOLOAD:
		m = M_Saving_AutoloadMode() + dir;
		if (m < 0)
			m = 3;
		else if (m > 3)
			m = 0;
		Cvar_SetValueQuick(&sv_autoload, m);
		break;

	case SAVING_AUTOSAVE:
		if (sv_autosave.value)
			Cvar_SetValueQuick(&sv_autosave, 0);
		else
		{
			Cvar_SetValueQuick(&sv_autosave, 1);
			if (M_Saving_IntervalValue() <= 0.f)
				M_Saving_SetIntervalValue(SAVING_AUTOSAVE_INTERVAL_DEFAULT);
		}
		break;

	case SAVING_AUTOSAVE_INTERVAL:
		interval = M_Saving_IntervalValue();
		if (interval <= 0.f)
			m = (dir > 0) ? SAVING_AUTOSAVE_INTERVAL_STEP : 0;
		else
		{
			m = Q_rint(interval) + dir * SAVING_AUTOSAVE_INTERVAL_STEP;
			if (m < SAVING_AUTOSAVE_INTERVAL_STEP)
				m = 0;
		}
		M_Saving_SetIntervalValue(m);
		break;

	case SAVING_INDICATOR:
		M_Saving_SetIndicator(!M_Saving_IndicatorEnabled());
		break;

	default:
		break;
	}
}

void M_Saving_Draw(void)
{
	qpic_t* p;
	int i;

	saving_cursor = (enum saving_e)M_Menu_ClampCursorValue((int)saving_cursor, SAVING_ITEMS);

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	{
		const char* title = "Saving";
		M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);
	}

	for (i = 0; i < SAVING_ITEMS; i++)
	{
		int y = SAVING_ROW_Y(i);
		const char* text = NULL;
		const char* value = NULL;

		switch (i)
		{
		case SAVING_AUTOLOAD:
			text = "         Auto Load";
			value = M_Saving_AutoloadText();
			break;

		case SAVING_AUTOSAVE:
			text = "         Auto Save";
			value = M_Saving_AutosaveText();
			break;

		case SAVING_AUTOSAVE_INTERVAL:
			text = "Auto Save Interval";
			value = M_Saving_IntervalText();
			break;

		case SAVING_INDICATOR:
			text = "  Saving Indicator";
			value = M_Saving_IndicatorEnabled() ? "on" : "off";
			break;

		default:
			break;
		}

		if (text)
			M_Print(0, y, text);

		if (value)
			M_Print(178, y, value);
	}

	M_DrawCharacter(168, SAVING_ROW_Y(saving_cursor), 12 + ((int)(realtime * 4) & 1));
}

void M_Saving_Key(int k)
{
	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		M_Menu_Extras_f();
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1:
		m_entersound = true;
		M_Saving_AdjustSliders(1);
		break;

	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		M_Saving_MoveCursor(-1);
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		M_Saving_MoveCursor(1);
		break;

	case K_LEFTARROW:
	case K_MWHEELDOWN:
		M_Saving_AdjustSliders(-1);
		break;

	case K_RIGHTARROW:
	case K_MWHEELUP:
		M_Saving_AdjustSliders(1);
		break;

	default:
		break;
	}
}

void M_Saving_Mousemove(int cx, int cy)
{
	int item = (cy - SAVING_ROW_Y(0)) / 8;
	(void)cx;

	if (item >= 0 && item < SAVING_ITEMS &&
		cy >= SAVING_ROW_Y(0) && cy < SAVING_ROW_Y(SAVING_ITEMS))
	{
		saving_cursor = (enum saving_e)item;
	}
}

/*
==================
Keyboard Shortcuts Menu
==================
*/

#define MAX_VIS_SHORTCUTS	17

typedef struct
{
	char		text[160];
	char		keys[96];
	char		search[256];
	qboolean	is_header;
} shortcutline_t;

static struct
{
	menulist_t		list;
	int				x, y, cols;
	int				prev_cursor;
	qboolean		scrollbar_grab;
	menuticker_t	ticker;
	shortcutline_t	*lines;
	int				*filtered_indices;
	char			status_message[64];
	char			current_category[64];
	double			status_time;
} shortcutmenu;

static void M_Shortcuts_Refilter(void);
static qboolean M_Shortcuts_IsSelectableDisplayIndex(int index);

static void M_Shortcuts_AddLine(const char* text, qboolean is_header)
{
	shortcutline_t line;

	q_strlcpy(line.text, text, sizeof(line.text));
	line.keys[0] = '\0';
	q_strlcpy(line.search, text, sizeof(line.search));
	line.is_header = is_header;
	VEC_PUSH(shortcutmenu.lines, line);
}

static void M_Shortcuts_AddHeader(const char* text)
{
	if (VEC_SIZE(shortcutmenu.lines) > 0)
		M_Shortcuts_AddLine("", false);

	q_strlcpy(shortcutmenu.current_category, text, sizeof(shortcutmenu.current_category));
	M_Shortcuts_AddLine(text, true);
	M_Shortcuts_AddLine("", false);
}

static void M_Shortcuts_AddShortcut(const char* action, const char* keys)
{
	shortcutline_t line;

	q_strlcpy(line.text, action, sizeof(line.text));
	q_strlcpy(line.keys, keys, sizeof(line.keys));
	q_snprintf(line.search, sizeof(line.search), "%s %s %s",
		shortcutmenu.current_category, action, keys);
	line.is_header = false;
	VEC_PUSH(shortcutmenu.lines, line);
}

static qboolean M_Shortcuts_IsSelectableDisplayIndex(int index)
{
	int line_idx;

	if (index < 0 || index >= VEC_SIZE(shortcutmenu.filtered_indices))
		return false;

	line_idx = shortcutmenu.filtered_indices[index];
	if (line_idx < 0 || line_idx >= VEC_SIZE(shortcutmenu.lines))
		return false;

	return shortcutmenu.lines[line_idx].keys[0] != '\0';
}

static const shortcutline_t* M_Shortcuts_SelectedLine(void)
{
	int line_idx;

	if (shortcutmenu.list.numitems <= 0 ||
		shortcutmenu.list.cursor < 0 ||
		shortcutmenu.list.cursor >= shortcutmenu.list.numitems)
		return NULL;

	line_idx = shortcutmenu.filtered_indices[shortcutmenu.list.cursor];
	if (line_idx < 0 || line_idx >= VEC_SIZE(shortcutmenu.lines))
		return NULL;

	return &shortcutmenu.lines[line_idx];
}

static void M_Shortcuts_EnsureSelectableCursor(int dir)
{
	if (shortcutmenu.list.numitems <= 0)
		return;

	if (M_Shortcuts_IsSelectableDisplayIndex(shortcutmenu.list.cursor))
		return;

	if (!M_List_SelectNextActive(&shortcutmenu.list, shortcutmenu.list.cursor, dir, true))
	{
		shortcutmenu.list.cursor = 0;
		shortcutmenu.list.scroll = 0;
	}
}

static void M_Shortcuts_EnsureSelectableCursorForKey(int key)
{
	switch (key)
	{
	case K_UPARROW:
	case K_KP_UPARROW:
		M_Shortcuts_EnsureSelectableCursor(-1);
		break;

	case K_END:
	case K_KP_END:
		M_Shortcuts_EnsureSelectableCursor(-1);
		break;

	default:
		M_Shortcuts_EnsureSelectableCursor(1);
		break;
	}
}

static void M_Shortcuts_Refilter(void)
{
	int i;

	VEC_CLEAR(shortcutmenu.filtered_indices);

	for (i = 0; i < VEC_SIZE(shortcutmenu.lines); i++)
	{
		if (shortcutmenu.list.search.len == 0 ||
			q_strcasestr(shortcutmenu.lines[i].search, shortcutmenu.list.search.text))
		{
			VEC_PUSH(shortcutmenu.filtered_indices, i);
		}
	}

	shortcutmenu.list.numitems = VEC_SIZE(shortcutmenu.filtered_indices);

	if (shortcutmenu.list.numitems <= 0)
	{
		shortcutmenu.list.cursor = 0;
		shortcutmenu.list.scroll = 0;
		return;
	}

	if (shortcutmenu.list.cursor >= shortcutmenu.list.numitems)
		shortcutmenu.list.cursor = shortcutmenu.list.numitems - 1;

	if (shortcutmenu.list.cursor < 0)
		shortcutmenu.list.cursor = 0;

	M_Shortcuts_EnsureSelectableCursor(1);
	M_List_CenterCursor(&shortcutmenu.list);
}

static void M_Shortcuts_CopyToClipboard(void)
{
	size_t total = 1;
	int i;
	char* copy;

	if (VEC_SIZE(shortcutmenu.lines) <= 0)
		return;

	for (i = 0; i < VEC_SIZE(shortcutmenu.lines); i++)
		total += strlen(shortcutmenu.lines[i].text) + strlen(shortcutmenu.lines[i].keys) + 3;

	copy = (char*)SDL_malloc(total);
	if (!copy)
		return;

	copy[0] = '\0';
	for (i = 0; i < VEC_SIZE(shortcutmenu.lines); i++)
	{
		q_strlcat(copy, shortcutmenu.lines[i].text, total);
		if (shortcutmenu.lines[i].keys[0])
		{
			q_strlcat(copy, "\t", total);
			q_strlcat(copy, shortcutmenu.lines[i].keys, total);
		}
		q_strlcat(copy, "\n", total);
	}

	if (SDL_SetClipboardText(copy) < 0)
		q_strlcpy(shortcutmenu.status_message, "Clipboard copy failed", sizeof(shortcutmenu.status_message));
	else
	{
		q_strlcpy(shortcutmenu.status_message, "Copied keyboard shortcuts", sizeof(shortcutmenu.status_message));
		M_TextField_PlayCopySound();
	}

	shortcutmenu.status_time = realtime;
	SDL_free(copy);
}

static void M_Shortcuts_Init(void)
{
	shortcutmenu.list.cursor = 0;
	shortcutmenu.list.scroll = 0;
	shortcutmenu.list.viewsize = MAX_VIS_SHORTCUTS;
	shortcutmenu.list.numitems = 0;
	shortcutmenu.list.isactive_fn = M_Shortcuts_IsSelectableDisplayIndex;
	memset(&shortcutmenu.list.search, 0, sizeof(shortcutmenu.list.search));
	shortcutmenu.list.search.maxlen = 32;

	shortcutmenu.prev_cursor = -1;
	shortcutmenu.scrollbar_grab = false;
	shortcutmenu.status_message[0] = '\0';
	shortcutmenu.current_category[0] = '\0';
	shortcutmenu.status_time = 0.0;

	VEC_CLEAR(shortcutmenu.lines);
	VEC_CLEAR(shortcutmenu.filtered_indices);
	M_Ticker_Init(&shortcutmenu.ticker);

	M_Shortcuts_AddHeader("App and System");
#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC)
	M_Shortcuts_AddShortcut("Show keyboard shortcuts", "Cmd+/");
#endif
	M_Shortcuts_AddShortcut("Toggle fullscreen", "Option+Enter");
	M_Shortcuts_AddShortcut("Minimize from fullscreen", "Cmd+Tab");
	M_Shortcuts_AddShortcut("Show command history", "Cmd+Y / Ctrl+H");
	M_Shortcuts_AddShortcut("Stop active download", "Cmd+. / Ctrl+.");
	M_Shortcuts_AddShortcut("Paste clipboard file", "Cmd+V / Ctrl+V");
	M_Shortcuts_AddShortcut("Mute or unmute sound", "Cmd+M / Ctrl+M");
	M_Shortcuts_AddShortcut("Increase UI scale", "Cmd+Shift+Wheel Up / Ctrl+Shift+Wheel Up");
	M_Shortcuts_AddShortcut("Decrease UI scale", "Cmd+Shift+Wheel Down / Ctrl+Shift+Wheel Down");
	M_Shortcuts_AddShortcut("Increase volume", "Option+Shift+Wheel Up");
	M_Shortcuts_AddShortcut("Decrease volume", "Option+Shift+Wheel Down");

	M_Shortcuts_AddHeader("Movement");
	M_Shortcuts_AddShortcut("Move forward", "W / Up Arrow");
	M_Shortcuts_AddShortcut("Move backward", "S / Down Arrow");
	M_Shortcuts_AddShortcut("Move left", "A / ,");
	M_Shortcuts_AddShortcut("Move right", "D / .");
	M_Shortcuts_AddShortcut("Turn left", "Left Arrow");
	M_Shortcuts_AddShortcut("Turn right", "Right Arrow");
	M_Shortcuts_AddShortcut("Strafe", "Alt/Option");
	M_Shortcuts_AddShortcut("Run", "Shift");
	M_Shortcuts_AddShortcut("Jump / swim up", "Space / Mouse2 / Left Trigger");
	M_Shortcuts_AddShortcut("Swim up", "E");
	M_Shortcuts_AddShortcut("Swim down", "C");
	M_Shortcuts_AddShortcut("Look up", "Page Down");
	M_Shortcuts_AddShortcut("Look down", "Delete");
	M_Shortcuts_AddShortcut("Center view", "End");
	M_Shortcuts_AddShortcut("Mouse look", "Backslash");
	M_Shortcuts_AddShortcut("Keyboard look", "Insert");

	M_Shortcuts_AddHeader("Combat and Weapons");
	M_Shortcuts_AddShortcut("Attack", "Ctrl / Mouse1 / Right Trigger");
	M_Shortcuts_AddShortcut("Next weapon", "Slash / Wheel Down / Right Shoulder");
	M_Shortcuts_AddShortcut("Previous weapon", "Wheel Up / Left Shoulder");
	M_Shortcuts_AddShortcut("Axe", "1");
	M_Shortcuts_AddShortcut("Shotgun", "2");
	M_Shortcuts_AddShortcut("Super Shotgun", "3");
	M_Shortcuts_AddShortcut("Nailgun", "4");
	M_Shortcuts_AddShortcut("Super Nailgun", "5");
	M_Shortcuts_AddShortcut("Grenade Launcher", "6");
	M_Shortcuts_AddShortcut("Rocket Launcher", "7");
	M_Shortcuts_AddShortcut("Thunderbolt", "8");
	M_Shortcuts_AddShortcut("Impulse 0", "0");
	M_Shortcuts_AddShortcut("Weapon wheel", "Y Button");

	M_Shortcuts_AddHeader("Menus and HUD");
	M_Shortcuts_AddShortcut("Show scores", "Tab");
	M_Shortcuts_AddShortcut("Help", "F1");
	M_Shortcuts_AddShortcut("Save menu", "F2");
	M_Shortcuts_AddShortcut("Load menu", "F3");
	M_Shortcuts_AddShortcut("Options menu", "F4");
	M_Shortcuts_AddShortcut("Multiplayer menu", "F5");
	M_Shortcuts_AddShortcut("Quicksave", "F6");
	M_Shortcuts_AddShortcut("Quickload", "F9");
	M_Shortcuts_AddShortcut("Quit prompt", "F10");
	M_Shortcuts_AddShortcut("Screenshot", "F12 / Print Screen");
	M_Shortcuts_AddShortcut("Toggle zoom", "F11");
	M_Shortcuts_AddShortcut("Pause", "Pause");
	M_Shortcuts_AddShortcut("Main menu", "Esc");
	M_Shortcuts_AddShortcut("Larger view", "+ / =");
	M_Shortcuts_AddShortcut("Smaller view", "-");

	M_Shortcuts_AddHeader("Console");
	M_Shortcuts_AddShortcut("Toggle console", "` / ~");
	M_Shortcuts_AddShortcut("Force console", "Shift+Esc");
	M_Shortcuts_AddShortcut("Autocomplete", "Tab");
	M_Shortcuts_AddShortcut("Previous or next command", "Up Arrow / Down Arrow");
	M_Shortcuts_AddShortcut("Scroll console", "Page Up / Page Down / Wheel");
	M_Shortcuts_AddShortcut("Page console scroll", "Ctrl+Page Up / Ctrl+Page Down");
	M_Shortcuts_AddShortcut("Jump to top or bottom", "Ctrl+Home / Ctrl+End");
	M_Shortcuts_AddShortcut("Move cursor by word", "Cmd+Left / Cmd+Right");
	M_Shortcuts_AddShortcut("Adjust console height", "Cmd+Up / Cmd+Down");
	M_Shortcuts_AddShortcut("Extend selection", "Shift+Arrow");
	M_Shortcuts_AddShortcut("Delete previous or next word", "Ctrl+Backspace / Ctrl+Delete");
	M_Shortcuts_AddShortcut("Clear line", "Cmd+U / Ctrl+U");
	M_Shortcuts_AddShortcut("Paste text", "Cmd+V / Ctrl+V / Shift+Insert");
	M_Shortcuts_AddShortcut("Select all", "Cmd+A");
	M_Shortcuts_AddShortcut("Copy console", "Cmd+C / Ctrl+C");
	M_Shortcuts_AddShortcut("Abort line", "Ctrl+D");

	M_Shortcuts_AddHeader("Chat");
	M_Shortcuts_AddShortcut("Open chat", "T");
	M_Shortcuts_AddShortcut("Send or cancel chat", "Enter / Esc");
	M_Shortcuts_AddShortcut("Send chat as team chat", "Ctrl+Enter");
	M_Shortcuts_AddShortcut("Delete previous word", "Ctrl+Backspace");
	M_Shortcuts_AddShortcut("Clear message", "Ctrl+U");
	M_Shortcuts_AddShortcut("Paste message", "Ctrl+V");

	M_Shortcuts_AddHeader("Demo Playback");
	M_Shortcuts_AddShortcut("Pause or resume", "Space");
	M_Shortcuts_AddShortcut("Increase speed", "Up Arrow / Shift+.");
	M_Shortcuts_AddShortcut("Decrease speed", "Down Arrow / Shift+,");
	M_Shortcuts_AddShortcut("Rewind or fast-forward", "Left Arrow / Right Arrow");
	M_Shortcuts_AddShortcut("Fine rewind or fast-forward", "Ctrl+Left / Ctrl+Right");
	M_Shortcuts_AddShortcut("Single-frame step while paused", ", / .");
	M_Shortcuts_AddShortcut("Seek to 10-90 percent", "1-9");
	M_Shortcuts_AddShortcut("Restart demo", "0 / Home");
	M_Shortcuts_AddShortcut("Jump to end", "End");
	M_Shortcuts_AddShortcut("Jump backward or forward 10 seconds", "J / L");

	M_Shortcuts_Refilter();
}

void M_Menu_Shortcuts_f(void)
{
	key_dest = key_menu;
	m_state = m_shortcuts;
	m_entersound = true;

	M_Shortcuts_Init();
	IN_UpdateGrabs();
}

void M_Shortcuts_Draw(void)
{
	int x, y, cols;
	int firstvis, numvis, i;
	int saved_viewsize;
	qboolean show_status;

	x = 16;
	y = 32;
	cols = 36;

	shortcutmenu.x = x;
	shortcutmenu.y = y;
	shortcutmenu.cols = cols;

	if (!keydown[K_MOUSE1])
		shortcutmenu.scrollbar_grab = false;

	if (shortcutmenu.prev_cursor != shortcutmenu.list.cursor)
	{
		shortcutmenu.prev_cursor = shortcutmenu.list.cursor;
		M_Ticker_Init(&shortcutmenu.ticker);
	}
	else
	{
		M_Ticker_Update(&shortcutmenu.ticker);
	}

	Draw_String(x, y - 28, "Keyboard Shortcuts");
	M_DrawQuakeBar(x - 8, y - 16, cols + 2);

	saved_viewsize = shortcutmenu.list.viewsize;
	if (shortcutmenu.list.search.len > 0 && shortcutmenu.list.viewsize > 14)
	{
		shortcutmenu.list.viewsize = 14;
		M_List_Rescroll(&shortcutmenu.list);
	}

	if (shortcutmenu.list.numitems > 0)
	{
		M_List_GetVisibleRange(&shortcutmenu.list, &firstvis, &numvis);
		for (i = 0; i < numvis; i++)
		{
			const int draw_idx = i + firstvis;
			const int line_idx = shortcutmenu.filtered_indices[draw_idx];
			shortcutline_t* line = &shortcutmenu.lines[line_idx];
			const int item_y = y + i * 8;
			const int maxchars = cols - 2;
			const int maxwidth = maxchars * 8;
			const qboolean selected = (draw_idx == shortcutmenu.list.cursor);
			const qboolean selectable = line->keys[0] != '\0';
			const qboolean matched = (shortcutmenu.list.search.len > 0 &&
				q_strcasestr(line->text, shortcutmenu.list.search.text) != NULL);
			const qboolean needs_scroll = ((int)strlen(line->text) > maxchars);

			if (line->is_header)
			{
				M_PrintWhite(x, item_y, line->text);
			}
			else if (matched)
			{
				if (needs_scroll)
					M_PrintHighlightScroll(x, item_y, maxwidth, line->text,
						shortcutmenu.list.search.text,
						selected ? shortcutmenu.ticker.scroll_time : 0.0);
				else
					M_PrintHighlight(x, item_y, line->text,
						shortcutmenu.list.search.text,
						shortcutmenu.list.search.len);
			}
			else if (needs_scroll)
			{
				M_PrintScroll(x, item_y, maxwidth, line->text,
					selected ? shortcutmenu.ticker.scroll_time : 0.0, true);
			}
			else
			{
				M_Print(x, item_y, line->text);
			}

			if (selected && selectable)
				M_DrawCharacter(x - 8, item_y, 12 + ((int)(realtime * 4) & 1));
		}
	}
	else
	{
		M_PrintWhite(x, y, "No matching lines");
	}

	if (M_List_GetOverflow(&shortcutmenu.list) > 0)
	{
		M_List_DrawScrollbar(&shortcutmenu.list, x + cols * 8 - 8, y);

		if (shortcutmenu.list.scroll > 0)
			M_DrawEllipsisBar(x, y - 8, cols);
		if (shortcutmenu.list.scroll + shortcutmenu.list.viewsize < shortcutmenu.list.numitems)
			M_DrawEllipsisBar(x, y + shortcutmenu.list.viewsize * 8, cols);
	}

	show_status = shortcutmenu.status_message[0] && (realtime - shortcutmenu.status_time) < 2.0;
	if (!show_status)
	{
		const shortcutline_t* selected_line = M_Shortcuts_SelectedLine();
		if (selected_line && selected_line->keys[0])
			M_PrintScroll(x, y + shortcutmenu.list.viewsize * 8 + 12,
				cols * 8, selected_line->keys,
				shortcutmenu.ticker.scroll_time, false);
	}

	if (shortcutmenu.list.search.len > 0)
	{
		int cursor_x = 24 + 8 * shortcutmenu.list.search.len;
		M_DrawTextBox(16, 176, 32, 1);
		M_PrintHighlight(24, 184, shortcutmenu.list.search.text,
			shortcutmenu.list.search.text,
			shortcutmenu.list.search.len);
		if (shortcutmenu.list.numitems == 0)
			M_DrawCharacter(cursor_x, 184, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, 184, 10);
	}

	if (show_status)
		M_PrintWhite(x, shortcutmenu.list.search.len > 0 ? 200 : 184, shortcutmenu.status_message);

	shortcutmenu.list.viewsize = saved_viewsize;
}

void M_Shortcuts_Key(int key)
{
	if (M_TextField_HasShortcutModifier() && (key == 'c' || key == 'C'))
	{
		M_Shortcuts_CopyToClipboard();
		return;
	}

	if (keydown[K_CTRL])
	{
		if ((key == 'u' || key == 'U') && shortcutmenu.list.search.len > 0)
		{
			shortcutmenu.list.search.len = 0;
			shortcutmenu.list.search.text[0] = 0;
			shortcutmenu.list.cursor = 0;
			shortcutmenu.list.scroll = 0;
			M_Shortcuts_Refilter();
			return;
		}
		else if (key == K_BACKSPACE && shortcutmenu.list.search.len > 0)
		{
			M_DeletePrevWord(&shortcutmenu.list.search);
			shortcutmenu.list.cursor = 0;
			shortcutmenu.list.scroll = 0;
			M_Shortcuts_Refilter();
			return;
		}
	}

	if (key >= 32 && key < 127)
	{
		if (shortcutmenu.list.search.len < shortcutmenu.list.search.maxlen)
		{
			shortcutmenu.list.search.text[shortcutmenu.list.search.len++] = key;
			shortcutmenu.list.search.text[shortcutmenu.list.search.len] = 0;
			shortcutmenu.list.cursor = 0;
			shortcutmenu.list.scroll = 0;
			M_Shortcuts_Refilter();
		}
		return;
	}

	if (key == K_BACKSPACE && shortcutmenu.list.search.len > 0)
	{
		shortcutmenu.list.search.text[--shortcutmenu.list.search.len] = 0;
		shortcutmenu.list.cursor = 0;
		shortcutmenu.list.scroll = 0;
		M_Shortcuts_Refilter();
		return;
	}

	if (shortcutmenu.scrollbar_grab)
	{
		switch (key)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			shortcutmenu.scrollbar_grab = false;
			break;
		}
		return;
	}

	if (shortcutmenu.list.numitems > 0 && M_List_Key(&shortcutmenu.list, key))
	{
		M_Shortcuts_EnsureSelectableCursorForKey(key);
		return;
	}

	if (M_Ticker_Key(&shortcutmenu.ticker, key))
		return;

	switch (key)
	{
	case K_ESCAPE:
		if (shortcutmenu.list.search.len > 0)
		{
			shortcutmenu.list.search.len = 0;
			shortcutmenu.list.search.text[0] = 0;
			shortcutmenu.list.cursor = 0;
			shortcutmenu.list.scroll = 0;
			M_Shortcuts_Refilter();
			return;
		}
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		M_Menu_Extras_f();
		break;

	case K_MOUSE1:
		if (shortcutmenu.list.numitems > 0)
		{
			int x = m_mousex - shortcutmenu.x - (shortcutmenu.cols - 1) * 8;
			int y = m_mousey - shortcutmenu.y;
			if (x >= -8 && M_List_UseScrollbar(&shortcutmenu.list, y))
			{
				shortcutmenu.scrollbar_grab = true;
				M_Shortcuts_Mousemove(m_mousex, m_mousey);
			}
		}
		break;

	default:
		break;
	}
}

void M_Shortcuts_Mousemove(int cx, int cy)
{
	cy -= shortcutmenu.y;

	if (shortcutmenu.scrollbar_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			shortcutmenu.scrollbar_grab = false;
			return;
		}
		M_List_UseScrollbar(&shortcutmenu.list, cy);
	}

	if (shortcutmenu.list.numitems > 0)
		M_List_Mousemove(&shortcutmenu.list, cy);
}

/*
==================
Model Viewer Menu
==================
*/

#define MAX_VIS_MODELVIEWER	16
#define MODELVIEWER_LIST_X	16
#define MODELVIEWER_LIST_Y	12
#define MODELVIEWER_LIST_COLS	14
#define MODELVIEWER_PREVIEW_X	136
#define MODELVIEWER_PREVIEW_Y	12
#define MODELVIEWER_PREVIEW_W	168
#define MODELVIEWER_PREVIEW_H	136
#define MODELVIEWER_PREVIEW_BOX_COLS	22
#define MODELVIEWER_PREVIEW_BOX_LINES	17
#define MODELVIEWER_DETAIL_X	16
#define MODELVIEWER_DETAIL_Y	160
#define MODELVIEWER_DETAIL_W	288
#define MODELVIEWER_SEARCH_BOX_X	16
#define MODELVIEWER_SEARCH_BOX_Y	176
#define MODELVIEWER_ORBIT_SENSITIVITY	2.0f

typedef struct
{
	char name[MAX_QPATH];
	char source[50];
} modelvieweritem_t;

static struct
{
	menulist_t			list;
	modelvieweritem_t	*items;
	int					*filtered_indices;
	int					x, y, cols;
	int					prev_cursor;
	menuticker_t		ticker;
	qboolean			scrollbar_grab;
	qboolean			orbit_grab;
	int					orbit_last_x, orbit_last_y;
	float				orbit_yaw, orbit_pitch;
	float				orbit_distance_scale;
} modelviewermenu;

static qboolean M_ModelViewer_IsModelFile(const char *name)
{
	const char *ext = COM_FileGetExtension(name);

	return !q_strcasecmp(ext, "mdl") ||
		!q_strcasecmp(ext, "spr") ||
		!q_strcasecmp(ext, "md3") ||
		!q_strcasecmp(ext, "md5") ||
		!q_strcasecmp(ext, "md5mesh") ||
		!q_strcasecmp(ext, "iqm");
}

static qboolean M_ModelViewer_JoinRelative(char *out, size_t outsize, const char *prefix, const char *name)
{
	size_t len = strlen(name);

	if (prefix && *prefix)
		len += strlen(prefix) + 1;
	if (len >= outsize)
		return false;

	if (prefix && *prefix)
		q_snprintf(out, outsize, "%s/%s", prefix, name);
	else
		q_strlcpy(out, name, outsize);

	return true;
}

static void M_ModelViewer_JoinFullPath(char *out, size_t outsize, const char *base, const char *relpath)
{
	if (relpath && *relpath)
		q_snprintf(out, outsize, "%s/%s", base, relpath);
	else
		q_strlcpy(out, base, outsize);
}

static void M_ModelViewer_AddCandidate(filelist_item_t **models, const char *name, const char *source)
{
	if (!name || !*name || strlen(name) >= MAX_QPATH)
		return;
	if (!M_ModelViewer_IsModelFile(name))
		return;

	FileList_Add(name, source, models);
}

static qboolean M_ModelViewer_IsMountedLoosePackage(searchpath_t *search)
{
	const char *base;
	const char *ext;

	if (!search || search->pack)
		return false;

	base = COM_SkipPath(search->purename);
	ext = COM_FileGetExtension(base);

	return base[0] == '#' ||
		!q_strcasecmp(ext, "pak") ||
		!q_strcasecmp(ext, "pk3") ||
		!q_strcasecmp(ext, "pk4") ||
		!q_strcasecmp(ext, "zip") ||
		!q_strcasecmp(ext, "apk") ||
		!q_strcasecmp(ext, "kpf");
}

static qboolean M_ModelViewer_IsLooseModelRoot(const char *name)
{
	return !q_strcasecmp(name, "progs") ||
		!q_strcasecmp(name, "models");
}

static qboolean M_ModelViewer_ShouldScanLooseDir(const char *relpath, const char *name, qboolean mounted_package)
{
	if (mounted_package)
		return true;
	if (relpath && *relpath)
		return true;
	if (!name || !*name || name[0] == '#')
		return false;

	return M_ModelViewer_IsLooseModelRoot(name);
}

#ifdef _WIN32
static void M_ModelViewer_ScanLooseDir(filelist_item_t **models, const char *base, const char *relpath, const char *source, int depth, qboolean mounted_package)
{
	WIN32_FIND_DATA fdat;
	HANDLE fhnd;
	char dirpath[MAX_OSPATH];
	char searchpath[MAX_OSPATH];
	char childrel[MAX_QPATH];

	if (depth > 16)
		return;

	M_ModelViewer_JoinFullPath(dirpath, sizeof(dirpath), base, relpath);
	q_snprintf(searchpath, sizeof(searchpath), "%s/*", dirpath);

	fhnd = FindFirstFile(searchpath, &fdat);
	if (fhnd == INVALID_HANDLE_VALUE)
		return;

	do
	{
		if (!strcmp(fdat.cFileName, ".") || !strcmp(fdat.cFileName, ".."))
			continue;
		if (!M_ModelViewer_JoinRelative(childrel, sizeof(childrel), relpath, fdat.cFileName))
			continue;

		if (fdat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (M_ModelViewer_ShouldScanLooseDir(relpath, fdat.cFileName, mounted_package))
				M_ModelViewer_ScanLooseDir(models, base, childrel, source, depth + 1, mounted_package);
		}
		else
			M_ModelViewer_AddCandidate(models, childrel, source);
	} while (FindNextFile(fhnd, &fdat));

	FindClose(fhnd);
}
#else
static void M_ModelViewer_ScanLooseDir(filelist_item_t **models, const char *base, const char *relpath, const char *source, int depth, qboolean mounted_package)
{
	DIR *dir_p;
	struct dirent *dir_t;
	char dirpath[MAX_OSPATH];
	char fullpath[MAX_OSPATH];
	char childrel[MAX_QPATH];
	struct stat st;

	if (depth > 16)
		return;

	M_ModelViewer_JoinFullPath(dirpath, sizeof(dirpath), base, relpath);
	dir_p = opendir(dirpath);
	if (!dir_p)
		return;

	while ((dir_t = readdir(dir_p)) != NULL)
	{
		if (dir_t->d_name[0] == '.')
			continue;
		if (!M_ModelViewer_JoinRelative(childrel, sizeof(childrel), relpath, dir_t->d_name))
			continue;

		M_ModelViewer_JoinFullPath(fullpath, sizeof(fullpath), base, childrel);
		if (stat(fullpath, &st) < 0)
			continue;

		if (S_ISDIR(st.st_mode))
		{
			if (M_ModelViewer_ShouldScanLooseDir(relpath, dir_t->d_name, mounted_package))
				M_ModelViewer_ScanLooseDir(models, base, childrel, source, depth + 1, mounted_package);
		}
		else if (S_ISREG(st.st_mode))
			M_ModelViewer_AddCandidate(models, childrel, source);
	}

	closedir(dir_p);
}
#endif

static void M_ModelViewer_SourceLabel(searchpath_t *search, char *out, size_t outsize)
{
	const char *source;

	if (search->pack)
	{
		source = search->purename[0] ? search->purename : search->pack->filename;
		q_strlcpy(out, COM_SkipPath(source), outsize);
	}
	else
	{
		source = search->purename[0] ? search->purename : search->filename;
		q_strlcpy(out, COM_SkipPath(source), outsize);
	}
}

static void M_ModelViewer_ClearFileList(filelist_item_t **list)
{
	filelist_item_t *next;

	while (*list)
	{
		next = (*list)->next;
		Z_Free(*list);
		*list = next;
	}
}

static void M_ModelViewer_BuildItems(void)
{
	searchpath_t *search;
	filelist_item_t *models = NULL;
	filelist_item_t *item;
	char source[50];

	VEC_CLEAR(modelviewermenu.items);
	VEC_CLEAR(modelviewermenu.filtered_indices);

	for (search = com_searchpaths; search; search = search->next)
	{
		M_ModelViewer_SourceLabel(search, source, sizeof(source));

		if (search->pack)
		{
			pack_t *pak = search->pack;
			int i;
			for (i = 0; i < pak->numfiles; i++)
				M_ModelViewer_AddCandidate(&models, pak->files[i].name, source);
		}
		else
		{
			M_ModelViewer_ScanLooseDir(&models, search->filename, "", source, 0,
				M_ModelViewer_IsMountedLoosePackage(search));
		}
	}

	for (item = models; item; item = item->next)
	{
		modelvieweritem_t viewer_item;
		q_strlcpy(viewer_item.name, item->name, sizeof(viewer_item.name));
		q_strlcpy(viewer_item.source, item->data[0] ? item->data : "unknown", sizeof(viewer_item.source));
		VEC_PUSH(modelviewermenu.items, viewer_item);
	}

	M_ModelViewer_ClearFileList(&models);
}

static void M_ModelViewer_Refilter(void)
{
	int i;
	int item_count = (int)VEC_SIZE(modelviewermenu.items);

	VEC_CLEAR(modelviewermenu.filtered_indices);

	for (i = 0; i < item_count; i++)
	{
		if (modelviewermenu.list.search.len == 0 ||
			q_strcasestr(modelviewermenu.items[i].name, modelviewermenu.list.search.text) ||
			q_strcasestr(modelviewermenu.items[i].source, modelviewermenu.list.search.text))
		{
			VEC_PUSH(modelviewermenu.filtered_indices, i);
		}
	}

	modelviewermenu.list.numitems = (int)VEC_SIZE(modelviewermenu.filtered_indices);

	if (modelviewermenu.list.numitems <= 0)
	{
		modelviewermenu.list.cursor = 0;
		modelviewermenu.list.scroll = 0;
		return;
	}

	if (modelviewermenu.list.cursor >= modelviewermenu.list.numitems)
		modelviewermenu.list.cursor = modelviewermenu.list.numitems - 1;
	if (modelviewermenu.list.cursor < 0)
		modelviewermenu.list.cursor = 0;

	M_List_CenterCursor(&modelviewermenu.list);
}

static modelvieweritem_t *M_ModelViewer_SelectedItem(void)
{
	int item_index;

	if (modelviewermenu.list.numitems <= 0 ||
		modelviewermenu.list.cursor < 0 ||
		modelviewermenu.list.cursor >= modelviewermenu.list.numitems)
	{
		return NULL;
	}

	item_index = modelviewermenu.filtered_indices[modelviewermenu.list.cursor];
	return &modelviewermenu.items[item_index];
}

static void M_ModelViewer_MenuRectToPixels(float x, float y, float w, float h,
	float *px, float *py, float *pw, float *ph)
{
	vrect_t bounds, vp;
	float sx, sy;

	Draw_GetMenuTransform(&bounds, &vp);
	sx = (float)vp.width / (float)bounds.width;
	sy = (float)vp.height / (float)bounds.height;

	*px = vp.x + (x - bounds.x) * sx;
	*py = vp.y + (y - bounds.y) * sy;
	*pw = w * sx;
	*ph = h * sy;
}

static qboolean M_ModelViewer_PointInPreview(int x, int y)
{
	return x >= MODELVIEWER_PREVIEW_X &&
		x < MODELVIEWER_PREVIEW_X + MODELVIEWER_PREVIEW_W &&
		y >= MODELVIEWER_PREVIEW_Y &&
		y < MODELVIEWER_PREVIEW_Y + MODELVIEWER_PREVIEW_H;
}

static void M_ModelViewer_ResetOrbit(void)
{
	modelviewermenu.orbit_yaw = 0.0f;
	modelviewermenu.orbit_pitch = 0.0f;
	modelviewermenu.orbit_distance_scale = 1.0f;
}

static void M_ModelViewer_Zoom(float factor)
{
	modelviewermenu.orbit_distance_scale = CLAMP(0.35f,
		modelviewermenu.orbit_distance_scale * factor,
		3.0f);
}

static void M_ModelViewer_DrawPreview(modelvieweritem_t *item)
{
	qmodel_t *mod;
	float px, py, pw, ph;

	M_DrawTextBox(MODELVIEWER_PREVIEW_X - 8, MODELVIEWER_PREVIEW_Y - 8,
		MODELVIEWER_PREVIEW_BOX_COLS, MODELVIEWER_PREVIEW_BOX_LINES);

	if (!item)
		return;

	mod = Mod_ForName(item->name, false);
	if (!mod || (mod->type != mod_alias && mod->type != mod_sprite))
	{
		const char *status = (mod && mod->type != mod_ext_invalid) ? "not renderable" : "load failed";
		M_PrintWhite(MODELVIEWER_PREVIEW_X + 40, MODELVIEWER_PREVIEW_Y + 60, status);
		return;
	}

	M_ModelViewer_MenuRectToPixels(MODELVIEWER_PREVIEW_X, MODELVIEWER_PREVIEW_Y,
		MODELVIEWER_PREVIEW_W, MODELVIEWER_PREVIEW_H,
		&px, &py, &pw, &ph);

	DrawOrbitModelToMenuPixelsFit(item->name,
		px, py, pw, ph,
		25.0f,
		0.0f,
		0, 0, 0, 0,
		modelviewermenu.orbit_yaw,
		modelviewermenu.orbit_pitch,
		modelviewermenu.orbit_distance_scale);

	GL_SetCanvas(CANVAS_MENU);
	glDisable(GL_BLEND);
	glEnable(GL_ALPHA_TEST);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

static void M_ModelViewer_MoveCursor(int delta)
{
	if (modelviewermenu.list.numitems <= 0)
		return;

	S_LocalSound("misc/menu1.wav");
	modelviewermenu.list.cursor += delta;
	modelviewermenu.list.cursor %= modelviewermenu.list.numitems;
	if (modelviewermenu.list.cursor < 0)
		modelviewermenu.list.cursor += modelviewermenu.list.numitems;
	M_List_AutoScroll(&modelviewermenu.list);
}

static void M_ModelViewer_Close(void)
{
	VEC_CLEAR(modelviewermenu.items);
	VEC_CLEAR(modelviewermenu.filtered_indices);
	M_Menu_Extras_f();
}

static void M_ModelViewer_Init(void)
{
	modelviewermenu.list.cursor = 0;
	modelviewermenu.list.scroll = 0;
	modelviewermenu.list.viewsize = MAX_VIS_MODELVIEWER;
	modelviewermenu.list.numitems = 0;
	modelviewermenu.list.isactive_fn = NULL;
	memset(&modelviewermenu.list.search, 0, sizeof(modelviewermenu.list.search));
	modelviewermenu.list.search.maxlen = 32;
	modelviewermenu.x = MODELVIEWER_LIST_X;
	modelviewermenu.y = MODELVIEWER_LIST_Y;
	modelviewermenu.cols = MODELVIEWER_LIST_COLS;
	modelviewermenu.prev_cursor = -1;
	modelviewermenu.scrollbar_grab = false;
	modelviewermenu.orbit_grab = false;
	modelviewermenu.orbit_last_x = 0;
	modelviewermenu.orbit_last_y = 0;
	M_ModelViewer_ResetOrbit();

	M_Ticker_Init(&modelviewermenu.ticker);
	M_ModelViewer_BuildItems();
	M_ModelViewer_Refilter();
}

void M_Menu_ModelViewer_f(void)
{
	key_dest = key_menu;
	m_state = m_modelviewer;
	m_entersound = true;

	M_ModelViewer_Init();
	IN_UpdateGrabs();
}

void M_ModelViewer_Draw(void)
{
	int firstvis, numvis, i;
	modelvieweritem_t *selected;

	modelviewermenu.x = MODELVIEWER_LIST_X;
	modelviewermenu.y = MODELVIEWER_LIST_Y;
	modelviewermenu.cols = MODELVIEWER_LIST_COLS;

	if (!keydown[K_MOUSE1])
	{
		modelviewermenu.scrollbar_grab = false;
		modelviewermenu.orbit_grab = false;
	}

	if (modelviewermenu.prev_cursor != modelviewermenu.list.cursor)
	{
		modelviewermenu.prev_cursor = modelviewermenu.list.cursor;
		M_Ticker_Init(&modelviewermenu.ticker);
	}
	else
	{
		M_Ticker_Update(&modelviewermenu.ticker);
	}

	if (modelviewermenu.list.numitems > 0)
	{
		M_List_GetVisibleRange(&modelviewermenu.list, &firstvis, &numvis);
		for (i = 0; i < numvis; i++)
		{
			const int draw_idx = i + firstvis;
			const int item_idx = modelviewermenu.filtered_indices[draw_idx];
			modelvieweritem_t *item = &modelviewermenu.items[item_idx];
			char display_name[MAX_QPATH];
			const int item_y = MODELVIEWER_LIST_Y + i * 8;
			const int maxchars = MODELVIEWER_LIST_COLS - 2;
			const int maxwidth = maxchars * 8;
			const qboolean selected_row = (draw_idx == modelviewermenu.list.cursor);
			qboolean matched;
			qboolean needs_scroll;

			COM_StripExtension(COM_SkipPath(item->name), display_name, sizeof(display_name));
			matched = (modelviewermenu.list.search.len > 0 &&
				q_strcasestr(display_name, modelviewermenu.list.search.text) != NULL);
			needs_scroll = ((int)strlen(display_name) > maxchars);

			if (matched)
			{
				if (needs_scroll)
					M_PrintHighlightScroll(MODELVIEWER_LIST_X, item_y, maxwidth,
						display_name, modelviewermenu.list.search.text,
						selected_row ? modelviewermenu.ticker.scroll_time : 0.0);
				else
					M_PrintHighlight(MODELVIEWER_LIST_X, item_y, display_name,
						modelviewermenu.list.search.text,
						modelviewermenu.list.search.len);
			}
			else if (needs_scroll)
			{
				M_PrintScroll(MODELVIEWER_LIST_X, item_y, maxwidth, display_name,
					selected_row ? modelviewermenu.ticker.scroll_time : 0.0, true);
			}
			else
			{
				M_Print(MODELVIEWER_LIST_X, item_y, display_name);
			}

			if (selected_row)
				M_DrawCharacter(MODELVIEWER_LIST_X - 8, item_y, 12 + ((int)(realtime * 4) & 1));
		}
	}
	else if (VEC_SIZE(modelviewermenu.items) > 0)
	{
		M_PrintWhite(MODELVIEWER_LIST_X, MODELVIEWER_LIST_Y, "No matches");
	}
	else
	{
		M_PrintWhite(MODELVIEWER_LIST_X, MODELVIEWER_LIST_Y, "No models");
	}

	if (M_List_GetOverflow(&modelviewermenu.list) > 0)
	{
		M_List_DrawScrollbar(&modelviewermenu.list,
			MODELVIEWER_LIST_X + MODELVIEWER_LIST_COLS * 8 - 8,
			MODELVIEWER_LIST_Y);

		if (modelviewermenu.list.scroll > 0)
			M_DrawEllipsisBar(MODELVIEWER_LIST_X, MODELVIEWER_LIST_Y - 8, MODELVIEWER_LIST_COLS);
		if (modelviewermenu.list.scroll + modelviewermenu.list.viewsize < modelviewermenu.list.numitems)
			M_DrawEllipsisBar(MODELVIEWER_LIST_X,
				MODELVIEWER_LIST_Y + modelviewermenu.list.viewsize * 8,
				MODELVIEWER_LIST_COLS);
	}

	selected = M_ModelViewer_SelectedItem();
	M_ModelViewer_DrawPreview(selected);

	if (selected)
	{
		M_PrintScroll(MODELVIEWER_DETAIL_X, MODELVIEWER_DETAIL_Y,
			MODELVIEWER_DETAIL_W,
			selected->name, modelviewermenu.ticker.scroll_time, false);
		M_PrintScroll(MODELVIEWER_DETAIL_X, MODELVIEWER_DETAIL_Y + 8,
			MODELVIEWER_DETAIL_W, selected->source,
			modelviewermenu.ticker.scroll_time, true);
	}

	if (modelviewermenu.list.search.len > 0)
	{
		int cursor_x = MODELVIEWER_SEARCH_BOX_X + 8 + 8 * modelviewermenu.list.search.len;
		M_DrawTextBox(MODELVIEWER_SEARCH_BOX_X, MODELVIEWER_SEARCH_BOX_Y, 32, 1);
		M_PrintHighlight(MODELVIEWER_SEARCH_BOX_X + 8, MODELVIEWER_SEARCH_BOX_Y + 8, modelviewermenu.list.search.text,
			modelviewermenu.list.search.text,
			modelviewermenu.list.search.len);
		if (modelviewermenu.list.numitems == 0)
			M_DrawCharacter(cursor_x, MODELVIEWER_SEARCH_BOX_Y + 8, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, MODELVIEWER_SEARCH_BOX_Y + 8, 10 + ((int)(realtime * 4) & 1));
	}
}

void M_ModelViewer_Key(int key)
{
	if (keydown[K_CTRL])
	{
		if ((key == 'u' || key == 'U') && modelviewermenu.list.search.len > 0)
		{
			modelviewermenu.list.search.len = 0;
			modelviewermenu.list.search.text[0] = 0;
			modelviewermenu.list.cursor = 0;
			modelviewermenu.list.scroll = 0;
			M_ModelViewer_Refilter();
			return;
		}
		else if (key == K_BACKSPACE && modelviewermenu.list.search.len > 0)
		{
			M_DeletePrevWord(&modelviewermenu.list.search);
			modelviewermenu.list.cursor = 0;
			modelviewermenu.list.scroll = 0;
			M_ModelViewer_Refilter();
			return;
		}
	}

	if (key >= 32 && key < 127)
	{
		if (modelviewermenu.list.search.len < modelviewermenu.list.search.maxlen)
		{
			modelviewermenu.list.search.text[modelviewermenu.list.search.len++] = key;
			modelviewermenu.list.search.text[modelviewermenu.list.search.len] = 0;
			modelviewermenu.list.cursor = 0;
			modelviewermenu.list.scroll = 0;
			M_ModelViewer_Refilter();
		}
		return;
	}

	if (key == K_BACKSPACE && modelviewermenu.list.search.len > 0)
	{
		modelviewermenu.list.search.text[--modelviewermenu.list.search.len] = 0;
		modelviewermenu.list.cursor = 0;
		modelviewermenu.list.scroll = 0;
		M_ModelViewer_Refilter();
		return;
	}

	if (modelviewermenu.scrollbar_grab)
	{
		switch (key)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			modelviewermenu.scrollbar_grab = false;
			break;
		}
		return;
	}

	switch (key)
	{
	case K_ESCAPE:
		if (modelviewermenu.list.search.len > 0)
		{
			modelviewermenu.list.search.len = 0;
			modelviewermenu.list.search.text[0] = 0;
			modelviewermenu.list.cursor = 0;
			modelviewermenu.list.scroll = 0;
			M_ModelViewer_Refilter();
			return;
		}
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		M_ModelViewer_Close();
		return;

	case K_LEFTARROW:
	case K_KP_LEFTARROW:
		M_ModelViewer_MoveCursor(-1);
		return;

	case K_RIGHTARROW:
	case K_KP_RIGHTARROW:
		M_ModelViewer_MoveCursor(1);
		return;

	case K_MWHEELUP:
		if (M_ModelViewer_PointInPreview(m_mousex, m_mousey))
			M_ModelViewer_Zoom(0.9f);
		else
			M_ModelViewer_MoveCursor(-1);
		return;

	case K_MWHEELDOWN:
		if (M_ModelViewer_PointInPreview(m_mousex, m_mousey))
			M_ModelViewer_Zoom(1.1f);
		else
			M_ModelViewer_MoveCursor(1);
		return;

	case K_MOUSE1:
		if (M_ModelViewer_PointInPreview(m_mousex, m_mousey) && M_ModelViewer_SelectedItem())
		{
			modelviewermenu.orbit_grab = true;
			modelviewermenu.orbit_last_x = m_mousex;
			modelviewermenu.orbit_last_y = m_mousey;
			return;
		}

		if (modelviewermenu.list.numitems > 0)
		{
			int x = m_mousex - modelviewermenu.x - (modelviewermenu.cols - 1) * 8;
			int y = m_mousey - modelviewermenu.y;
			if (x >= -8 && M_List_UseScrollbar(&modelviewermenu.list, y))
			{
				modelviewermenu.scrollbar_grab = true;
				M_ModelViewer_Mousemove(m_mousex, m_mousey);
			}
		}
		return;

	default:
		break;
	}

	if (modelviewermenu.list.numitems > 0 && M_List_Key(&modelviewermenu.list, key))
		return;

	M_Ticker_Key(&modelviewermenu.ticker, key);
}

void M_ModelViewer_Mousemove(int cx, int cy)
{
	int list_y = cy - modelviewermenu.y;

	if (modelviewermenu.orbit_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			modelviewermenu.orbit_grab = false;
			return;
		}

		modelviewermenu.orbit_yaw += (float)(cx - modelviewermenu.orbit_last_x) * MODELVIEWER_ORBIT_SENSITIVITY;
		while (modelviewermenu.orbit_yaw >= 360.0f)
			modelviewermenu.orbit_yaw -= 360.0f;
		while (modelviewermenu.orbit_yaw < 0.0f)
			modelviewermenu.orbit_yaw += 360.0f;
		modelviewermenu.orbit_pitch = CLAMP(-80.0f,
			modelviewermenu.orbit_pitch + (float)(cy - modelviewermenu.orbit_last_y) * MODELVIEWER_ORBIT_SENSITIVITY,
			80.0f);
		modelviewermenu.orbit_last_x = cx;
		modelviewermenu.orbit_last_y = cy;
		return;
	}

	if (modelviewermenu.scrollbar_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			modelviewermenu.scrollbar_grab = false;
			return;
		}
		M_List_UseScrollbar(&modelviewermenu.list, list_y);
	}

	if (modelviewermenu.list.numitems > 0 &&
		cx >= modelviewermenu.x - 8 &&
		cx < modelviewermenu.x + modelviewermenu.cols * 8)
	{
		M_List_Mousemove(&modelviewermenu.list, list_y);
	}
}

/*
==================
Version Menu
==================
*/

#define MAX_VIS_VERSION	17
#define VERSION_GITHUB_URL VERSION_GITHUB_RELEASE_URL

typedef struct
{
	char		text[160];
	qboolean	is_header;
} versionline_t;

static struct
{
	SDL_mutex	*mutex;
	versionremoteinfo_t release;
	versionremoteinfo_t commit;
} versiongithub;

static struct
{
	menulist_t		list;
	int				x, y, cols;
	int				prev_cursor;
	qboolean		scrollbar_grab;
	menuticker_t	ticker;
	versionline_t	*lines;
	int				*filtered_indices;
	char			status_message[64];
	double			status_time;
	int				github_release_line_index;
	int				github_commit_line_index;
} versionmenu;

static void M_Version_Refilter(void);

#define VERSION_GITHUB_COMMIT_URL "https://api.github.com/repos/timbergeron/QSS-M/commits?path=Quake/quakedef.h&per_page=1"
#define VERSION_GITHUB_QUAKEDEF_URL_FMT "https://raw.githubusercontent.com/timbergeron/QSS-M/%s/Quake/quakedef.h"

static void M_Version_GitHubEnsureMutex(void)
{
	if (!versiongithub.mutex)
		versiongithub.mutex = SDL_CreateMutex();
}

static void M_Version_RemoteInfo_Init(versionremoteinfo_t* info, int state)
{
	info->state = state;
	info->comparison = 2;
	info->version[0] = '\0';
	info->detail[0] = '\0';
	info->error[0] = '\0';
}

static qboolean M_Version_ParseQuakedefInt(const char* text, const char* macro, int* out)
{
	char pattern[64];
	const char* pos;

	q_snprintf(pattern, sizeof(pattern), "#define %s", macro);
	pos = strstr(text, pattern);
	if (!pos)
		return false;

	return sscanf(pos + strlen(pattern), "%d", out) == 1;
}

static void M_Version_ParseQuakedefSuffix(const char* text, char* out, size_t outsz)
{
	char pattern[64];
	const char* pos;
	const char* begin;
	const char* end;
	size_t len;

	out[0] = '\0';
	q_snprintf(pattern, sizeof(pattern), "#define %s", "QSSM_VER_SUFFIX");
	pos = strstr(text, pattern);
	if (!pos)
		return;

	begin = strchr(pos + strlen(pattern), '"');
	if (!begin)
		return;
	end = strchr(begin + 1, '"');
	if (!end)
		return;

	len = (size_t)(end - begin - 1);
	if (len >= outsz)
		len = outsz - 1;
	memcpy(out, begin + 1, len);
	out[len] = '\0';
}

static qboolean M_Version_ParseQuakedefVersion(const char* text, char* out, size_t outsz, int* comparison)
{
	int major, minor, patch;
	char suffix[32];

	if (!M_Version_ParseQuakedefInt(text, "QSSM_VER_MAJOR", &major) ||
		!M_Version_ParseQuakedefInt(text, "QSSM_VER_MINOR", &minor) ||
		!M_Version_ParseQuakedefInt(text, "QSSM_VER_PATCH", &patch))
	{
		return false;
	}

	M_Version_ParseQuakedefSuffix(text, suffix, sizeof(suffix));
	q_snprintf(out, outsz, "%d.%d.%d%s", major, minor, patch, suffix);
	*comparison = M_Version_CompareToCurrent(major, minor, patch, suffix,
		false);
	return true;
}

static void M_Version_GitHubFetchRelease(versionremoteinfo_t* info)
{
	versionhttpmem_t mem = {0};

	M_Version_RemoteInfo_Init(info, VERSIONGITHUB_ERROR);
	if (!M_Version_GitHubHttpGet(VERSION_GITHUB_RELEASE_URL, &mem,
		info->error, sizeof(info->error), VERSION_GITHUB_MAX_RESPONSE_BYTES))
		return;

	{
		json_t* json = JSON_Parse(mem.memory);
		if (!json || !json->root || json->root->type != JSON_OBJECT)
		{
			if (json)
				JSON_Free(json);
			free(mem.memory);
			q_strlcpy(info->error, "invalid JSON", sizeof(info->error));
			return;
		}

		{
			const char* latest_tag = JSON_FindString(json->root, "tag_name");
			const qboolean* prerelease = JSON_FindBoolean(json->root,
				"prerelease");
			if (!latest_tag || !latest_tag[0])
			{
				JSON_Free(json);
				free(mem.memory);
				q_strlcpy(info->error, "missing tag_name", sizeof(info->error));
				return;
			}

			q_strlcpy(info->version, latest_tag, sizeof(info->version));

			info->comparison = M_Version_CompareTagToCurrent(latest_tag,
				prerelease ? *prerelease : false);
		}

		info->state = VERSIONGITHUB_READY;
		JSON_Free(json);
	}

	free(mem.memory);
}

static void M_Version_GitHubFetchCommit(versionremoteinfo_t* info)
{
	versionhttpmem_t mem = {0};
	versionhttpmem_t filemem = {0};
	char rawurl[256];

	M_Version_RemoteInfo_Init(info, VERSIONGITHUB_ERROR);
	if (!M_Version_GitHubHttpGet(VERSION_GITHUB_COMMIT_URL, &mem,
		info->error, sizeof(info->error), VERSION_GITHUB_MAX_RESPONSE_BYTES))
		return;

	{
		json_t* json = JSON_Parse(mem.memory);
		if (!json || !json->root || json->root->type != JSON_ARRAY || !json->root->firstchild)
		{
			if (json)
				JSON_Free(json);
			free(mem.memory);
			q_strlcpy(info->error, "invalid commit JSON", sizeof(info->error));
			return;
		}

		{
			const char* sha = JSON_FindString(json->root->firstchild, "sha");
			if (!sha || !sha[0])
			{
				JSON_Free(json);
				free(mem.memory);
				q_strlcpy(info->error, "missing sha", sizeof(info->error));
				return;
			}

			q_strlcpy(info->detail, sha, 8);
			q_snprintf(rawurl, sizeof(rawurl), VERSION_GITHUB_QUAKEDEF_URL_FMT, sha);
		}

		JSON_Free(json);
	}

	free(mem.memory);

	if (!M_Version_GitHubHttpGet(rawurl, &filemem,
		info->error, sizeof(info->error), VERSION_GITHUB_MAX_RESPONSE_BYTES))
		return;

	if (!M_Version_ParseQuakedefVersion(filemem.memory, info->version, sizeof(info->version), &info->comparison))
	{
		free(filemem.memory);
		q_strlcpy(info->error, "missing QSSM version", sizeof(info->error));
		return;
	}

	free(filemem.memory);
	info->state = VERSIONGITHUB_READY;
}

static int M_Version_GitHubThread(void* unused)
{
	versionremoteinfo_t release;
	versionremoteinfo_t commit;

	(void)unused;

	M_Version_GitHubFetchRelease(&release);
	M_Version_GitHubFetchCommit(&commit);

	M_Version_GitHubEnsureMutex();
	if (!versiongithub.mutex)
		return 0;

	SDL_LockMutex(versiongithub.mutex);
	versiongithub.release = release;
	versiongithub.commit = commit;
	SDL_UnlockMutex(versiongithub.mutex);

	return 0;
}

void M_Version_GetGitHubInfo(versionremoteinfo_t* release, versionremoteinfo_t* commit)
{
	M_Version_GitHubEnsureMutex();

	if (release)
		M_Version_RemoteInfo_Init(release, VERSIONGITHUB_IDLE);
	if (commit)
		M_Version_RemoteInfo_Init(commit, VERSIONGITHUB_IDLE);

	if (!versiongithub.mutex)
		return;

	SDL_LockMutex(versiongithub.mutex);
	if (release)
		*release = versiongithub.release;
	if (commit)
		*commit = versiongithub.commit;
	SDL_UnlockMutex(versiongithub.mutex);
}

qboolean M_Version_WaitForGitHubInfo(versionremoteinfo_t* release, versionremoteinfo_t* commit, Uint32 timeout_ms)
{
	Uint32 deadline;

	M_Version_StartGitHubFetch();
	deadline = SDL_GetTicks() + timeout_ms;

	for (;;)
	{
		M_Version_GetGitHubInfo(release, commit);

		if ((!release || (release->state != VERSIONGITHUB_IDLE && release->state != VERSIONGITHUB_LOADING)) &&
			(!commit || (commit->state != VERSIONGITHUB_IDLE && commit->state != VERSIONGITHUB_LOADING)))
		{
			return true;
		}

		if (!timeout_ms || SDL_TICKS_PASSED(SDL_GetTicks(), deadline))
			return false;

		SDL_Delay(10);
	}
}

void M_Version_StartGitHubFetch(void)
{
	SDL_Thread* thread;

	M_Version_GitHubEnsureMutex();
	if (!versiongithub.mutex)
		return;

	SDL_LockMutex(versiongithub.mutex);
	if (versiongithub.release.state == VERSIONGITHUB_LOADING ||
		versiongithub.commit.state == VERSIONGITHUB_LOADING)
	{
		SDL_UnlockMutex(versiongithub.mutex);
		return;
	}
	if (versiongithub.release.state == VERSIONGITHUB_READY &&
		versiongithub.commit.state == VERSIONGITHUB_READY)
	{
		SDL_UnlockMutex(versiongithub.mutex);
		return;
	}

	M_Version_RemoteInfo_Init(&versiongithub.release, VERSIONGITHUB_LOADING);
	M_Version_RemoteInfo_Init(&versiongithub.commit, VERSIONGITHUB_LOADING);
	SDL_UnlockMutex(versiongithub.mutex);

	thread = SDL_CreateThread(M_Version_GitHubThread, "VersionGitHubThread", NULL);
	if (!thread)
	{
		SDL_LockMutex(versiongithub.mutex);
		M_Version_RemoteInfo_Init(&versiongithub.release, VERSIONGITHUB_ERROR);
		M_Version_RemoteInfo_Init(&versiongithub.commit, VERSIONGITHUB_ERROR);
		q_strlcpy(versiongithub.release.error, "thread create failed", sizeof(versiongithub.release.error));
		q_strlcpy(versiongithub.commit.error, "thread create failed", sizeof(versiongithub.commit.error));
		SDL_UnlockMutex(versiongithub.mutex);
		return;
	}

	SDL_DetachThread(thread);
}

static void M_Version_UpdateGitHubLines(void)
{
	char release_text[sizeof(versionmenu.lines[0].text)];
	char commit_text[sizeof(versionmenu.lines[0].text)];
	versionremoteinfo_t release;
	versionremoteinfo_t commit;
	qboolean changed = false;

	if (versionmenu.github_release_line_index < 0 || versionmenu.github_release_line_index >= VEC_SIZE(versionmenu.lines))
		return;
	if (versionmenu.github_commit_line_index < 0 || versionmenu.github_commit_line_index >= VEC_SIZE(versionmenu.lines))
		return;

	M_Version_GetGitHubInfo(&release, &commit);

	if (release.state == VERSIONGITHUB_LOADING || release.state == VERSIONGITHUB_IDLE)
	{
		q_strlcpy(release_text, "  Latest release  checking...", sizeof(release_text));
	}
	else if (release.state == VERSIONGITHUB_READY)
	{
		if (release.comparison == 0)
			q_snprintf(release_text, sizeof(release_text), "  Latest release  %s (you have this)", release.version);
		else if (release.comparison == 2)
			q_snprintf(release_text, sizeof(release_text), "  Latest release  %s (unknown channel)", release.version);
		else if (release.comparison > 0)
			q_snprintf(release_text, sizeof(release_text), "  Latest release  %s (you have newer)", release.version);
		else if (release.comparison < 0)
			q_snprintf(release_text, sizeof(release_text), "  Latest release  %s (update available)", release.version);
		else
			q_snprintf(release_text, sizeof(release_text), "  Latest release  %s", release.version);
	}
	else
	{
		q_snprintf(release_text, sizeof(release_text), "  Latest release  error (%s)",
			release.error[0] ? release.error : "unavailable");
	}

	if (commit.state == VERSIONGITHUB_LOADING || commit.state == VERSIONGITHUB_IDLE)
	{
		q_strlcpy(commit_text, "  Latest commit   checking...", sizeof(commit_text));
	}
	else if (commit.state == VERSIONGITHUB_READY)
	{
		if (commit.comparison == 0)
			q_snprintf(commit_text, sizeof(commit_text), "  Latest commit   %s @ %s (you have this)",
				commit.version, commit.detail[0] ? commit.detail : "unknown");
		else if (commit.comparison == 2)
			q_snprintf(commit_text, sizeof(commit_text), "  Latest commit   %s @ %s (unknown channel)",
				commit.version, commit.detail[0] ? commit.detail : "unknown");
		else if (commit.comparison > 0)
			q_snprintf(commit_text, sizeof(commit_text), "  Latest commit   %s @ %s (you have newer)",
				commit.version, commit.detail[0] ? commit.detail : "unknown");
		else if (commit.comparison < 0)
			q_snprintf(commit_text, sizeof(commit_text), "  Latest commit   %s @ %s (update available)",
				commit.version, commit.detail[0] ? commit.detail : "unknown");
		else
			q_snprintf(commit_text, sizeof(commit_text), "  Latest commit   %s @ %s",
				commit.version, commit.detail[0] ? commit.detail : "unknown");
	}
	else
	{
		q_snprintf(commit_text, sizeof(commit_text), "  Latest commit   error (%s)",
			commit.error[0] ? commit.error : "unavailable");
	}

	if (strcmp(versionmenu.lines[versionmenu.github_release_line_index].text, release_text))
	{
		q_strlcpy(versionmenu.lines[versionmenu.github_release_line_index].text,
			release_text, sizeof(versionmenu.lines[versionmenu.github_release_line_index].text));
		changed = true;
	}

	if (strcmp(versionmenu.lines[versionmenu.github_commit_line_index].text, commit_text))
	{
		q_strlcpy(versionmenu.lines[versionmenu.github_commit_line_index].text,
			commit_text, sizeof(versionmenu.lines[versionmenu.github_commit_line_index].text));
		changed = true;
	}

	if (changed && versionmenu.list.search.len > 0)
		M_Version_Refilter();
}

static const char* M_Version_GetGLString(GLenum name)
{
	const char* value = (const char*)glGetString(name);
	return value ? value : "unavailable";
}

static void M_Version_AddLine(const char* text, qboolean is_header)
{
	versionline_t line;

	q_strlcpy(line.text, text, sizeof(line.text));
	line.is_header = is_header;
	VEC_PUSH(versionmenu.lines, line);
}

static void M_Version_Refilter(void)
{
	int i;

	VEC_CLEAR(versionmenu.filtered_indices);

	for (i = 0; i < VEC_SIZE(versionmenu.lines); i++)
	{
		if (versionmenu.list.search.len == 0 ||
			q_strcasestr(versionmenu.lines[i].text, versionmenu.list.search.text))
		{
			VEC_PUSH(versionmenu.filtered_indices, i);
		}
	}

	versionmenu.list.numitems = VEC_SIZE(versionmenu.filtered_indices);

	if (versionmenu.list.numitems <= 0)
	{
		versionmenu.list.cursor = 0;
		versionmenu.list.scroll = 0;
		return;
	}

	if (versionmenu.list.cursor >= versionmenu.list.numitems)
		versionmenu.list.cursor = versionmenu.list.numitems - 1;

	if (versionmenu.list.cursor < 0)
		versionmenu.list.cursor = 0;

	M_List_CenterCursor(&versionmenu.list);
}

static void M_Version_CopyToClipboard(void)
{
	size_t total = 1;
	int i;
	char* copy;

	if (VEC_SIZE(versionmenu.lines) <= 0)
		return;

	for (i = 0; i < VEC_SIZE(versionmenu.lines); i++)
		total += strlen(versionmenu.lines[i].text) + 1;

	copy = (char*)SDL_malloc(total);
	if (!copy)
		return;

	copy[0] = '\0';
	for (i = 0; i < VEC_SIZE(versionmenu.lines); i++)
	{
		q_strlcat(copy, versionmenu.lines[i].text, total);
		q_strlcat(copy, "\n", total);
	}

	if (SDL_SetClipboardText(copy) < 0)
		q_strlcpy(versionmenu.status_message, "Clipboard copy failed", sizeof(versionmenu.status_message));
	else
	{
		q_strlcpy(versionmenu.status_message, "Copied version info", sizeof(versionmenu.status_message));
		M_TextField_PlayCopySound();
	}

	versionmenu.status_time = realtime;
	SDL_free(copy);
}

static void M_Version_Init(void)
{
	SDL_version sdl_linked;

	versionmenu.list.cursor = 0;
	versionmenu.list.scroll = 0;
	versionmenu.list.viewsize = MAX_VIS_VERSION;
	versionmenu.list.numitems = 0;
	versionmenu.list.isactive_fn = NULL;
	memset(&versionmenu.list.search, 0, sizeof(versionmenu.list.search));
	versionmenu.list.search.maxlen = 32;

	versionmenu.prev_cursor = -1;
	versionmenu.scrollbar_grab = false;
	versionmenu.status_message[0] = '\0';
	versionmenu.status_time = 0.0;
	versionmenu.github_release_line_index = -1;
	versionmenu.github_commit_line_index = -1;

	VEC_CLEAR(versionmenu.lines);
	VEC_CLEAR(versionmenu.filtered_indices);

	M_Ticker_Init(&versionmenu.ticker);
	SDL_GetVersion(&sdl_linked);

	M_Version_AddLine("Application Information", true);
	M_Version_AddLine(va("  Quake          %1.2f", VERSION), false);
	M_Version_AddLine(va("  QuakeSpasm     %s", QUAKESPASM_VER_STRING), false);
	M_Version_AddLine(va("  QSS            %s", QSS_VER), false);
	M_Version_AddLine(va("  QSS-M          %s", QSSM_VER_STRING), false);

#ifdef QSS_VERSION
	M_Version_AddLine(va("  QSS Git Desc   %s", QS_STRINGIFY(QSS_VERSION)), false);
#endif
#ifdef QSS_REVISION
	M_Version_AddLine(va("  QSS Git Rev    %s", QS_STRINGIFY(QSS_REVISION)), false);
#endif
#ifdef QSS_DATE
	M_Version_AddLine(va("  Build Date     %s", QS_STRINGIFY(QSS_DATE)), false);
#else
	M_Version_AddLine(va("  Build Date     %s %s", __DATE__, __TIME__), false);
#endif

	M_Version_AddLine(va("  Platform       %s %d-bit", SDL_GetPlatform(), (int)sizeof(void*) * 8), false);

	M_Version_AddLine("", false);
	M_Version_AddLine("Renderer Information", true);
	M_Version_AddLine(va("  Vendor         %s", M_Version_GetGLString(GL_VENDOR)), false);
	M_Version_AddLine(va("  Renderer       %s", M_Version_GetGLString(GL_RENDERER)), false);
	M_Version_AddLine(va("  Version        %s", M_Version_GetGLString(GL_VERSION)), false);

	M_Version_AddLine("", false);
	M_Version_AddLine("Library Versions", true);
	M_Version_AddLine(va("  SDL compiled   %s", Q_SDL_COMPILED_VERSION_STRING), false);
	M_Version_AddLine(va("  SDL linked     %d.%d.%d", sdl_linked.major, sdl_linked.minor, sdl_linked.patch), false);
	M_Version_AddLine(va("  zlib           %s", zlibVersion()), false);
#ifdef LIBCURL_VERSION
	M_Version_AddLine(va("  libcurl        %s", LIBCURL_VERSION), false);
#endif
#ifdef USE_CODEC_FLAC
	M_Version_AddLine(va("  libFLAC        %s", FLAC__VERSION_STRING), false);
#endif
#ifdef USE_CODEC_OPUS
	{
		const char* opus_ver = opus_get_version_string();
		const char* version = strstr(opus_ver, "libopus ");
		M_Version_AddLine(va("  libopus        %s", version ? version + 8 : opus_ver), false);
	}
#endif
#ifdef USE_CODEC_VORBIS
	{
		const char* vorbis_ver = vorbis_version_string();
		const char* version = strstr(vorbis_ver, "libVorbis ");
		M_Version_AddLine(va("  libvorbis      %s", version ? version + 10 : vorbis_ver), false);
	}
#endif
#ifdef USE_CODEC_MIKMOD
	M_Version_AddLine(va("  libmikmod      %ld.%ld.%ld",
		LIBMIKMOD_VERSION_MAJOR,
		LIBMIKMOD_VERSION_MINOR,
		LIBMIKMOD_REVISION), false);
#endif
#ifdef USE_CODEC_XMP
	M_Version_AddLine(va("  libxmp         %s", XMP_VERSION), false);
#endif
#ifdef USE_CODEC_MP3
	M_Version_AddLine(va("  libmad         %d.%d.%d%s",
		MAD_VERSION_MAJOR,
		MAD_VERSION_MINOR,
		MAD_VERSION_PATCH,
		MAD_VERSION_EXTRA), false);
#endif

	M_Version_AddLine("", false);
	M_Version_AddLine("GitHub QSS-M Versions", true);
	versionmenu.github_release_line_index = VEC_SIZE(versionmenu.lines);
	M_Version_AddLine("  Latest release  checking...", false);
	versionmenu.github_commit_line_index = VEC_SIZE(versionmenu.lines);
	M_Version_AddLine("  Latest commit   checking...", false);

	M_Version_Refilter();
	M_Version_StartGitHubFetch();
	M_Version_UpdateGitHubLines();
}

void M_Menu_Version_f(void)
{
	key_dest = key_menu;
	m_state = m_version;
	m_entersound = true;

	M_Version_Init();
	IN_UpdateGrabs();
}

void M_Version_Draw(void)
{
	int x, y, cols;
	int firstvis, numvis, i;

	x = 16;
	y = 32;
	cols = 36;

	versionmenu.x = x;
	versionmenu.y = y;
	versionmenu.cols = cols;

	if (!keydown[K_MOUSE1])
		versionmenu.scrollbar_grab = false;

	if (versionmenu.prev_cursor != versionmenu.list.cursor)
	{
		versionmenu.prev_cursor = versionmenu.list.cursor;
		M_Ticker_Init(&versionmenu.ticker);
	}
	else
	{
		M_Ticker_Update(&versionmenu.ticker);
	}

	M_Version_UpdateGitHubLines();

	Draw_String(x, y - 28, "Version Information");
	M_DrawQuakeBar(x - 8, y - 16, cols + 2);

	if (versionmenu.list.numitems > 0)
	{
		M_List_GetVisibleRange(&versionmenu.list, &firstvis, &numvis);
		for (i = 0; i < numvis; i++)
		{
			const int draw_idx = i + firstvis;
			const int line_idx = versionmenu.filtered_indices[draw_idx];
			versionline_t* line = &versionmenu.lines[line_idx];
			const int item_y = y + i * 8;
			const int maxchars = cols - 2;
			const int maxwidth = maxchars * 8;
			const qboolean selected = (draw_idx == versionmenu.list.cursor);
			const qboolean matched = (versionmenu.list.search.len > 0 &&
				q_strcasestr(line->text, versionmenu.list.search.text) != NULL);
			const qboolean needs_scroll = ((int)strlen(line->text) > maxchars);

			if (line->is_header)
			{
				M_PrintWhite(x, item_y, line->text);
			}
			else if (matched)
			{
				if (needs_scroll)
					M_PrintHighlightScroll(x, item_y, maxwidth, line->text,
						versionmenu.list.search.text,
						selected ? versionmenu.ticker.scroll_time : 0.0);
				else
					M_PrintHighlight(x, item_y, line->text,
						versionmenu.list.search.text,
						versionmenu.list.search.len);
			}
			else if (needs_scroll)
			{
				M_PrintScroll(x, item_y, maxwidth, line->text,
					selected ? versionmenu.ticker.scroll_time : 0.0, true);
			}
			else
			{
				M_Print(x, item_y, line->text);
			}

			if (selected)
				M_DrawCharacter(x - 8, item_y, 12);
		}
	}
	else
	{
		M_PrintWhite(x, y, "No matching lines");
	}

	if (M_List_GetOverflow(&versionmenu.list) > 0)
	{
		M_List_DrawScrollbar(&versionmenu.list, x + cols * 8 - 8, y);

		if (versionmenu.list.scroll > 0)
			M_DrawEllipsisBar(x, y - 8, cols);
		if (versionmenu.list.scroll + versionmenu.list.viewsize < versionmenu.list.numitems)
			M_DrawEllipsisBar(x, y + versionmenu.list.viewsize * 8, cols);
	}

	if (versionmenu.list.search.len > 0)
	{
		int cursor_x = 24 + 8 * versionmenu.list.search.len;
		M_DrawTextBox(16, 176, 32, 1);
		M_PrintHighlight(24, 184, versionmenu.list.search.text,
			versionmenu.list.search.text,
			versionmenu.list.search.len);
		if (versionmenu.list.numitems == 0)
			M_DrawCharacter(cursor_x, 184, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, 184, 10);
	}

	if (versionmenu.status_message[0] && (realtime - versionmenu.status_time) < 2.0)
		M_PrintWhite(x, versionmenu.list.search.len > 0 ? 200 : 184, versionmenu.status_message);
}

void M_Version_Key(int key)
{
	if (M_TextField_HasShortcutModifier() && (key == 'c' || key == 'C'))
	{
		M_Version_CopyToClipboard();
		return;
	}

	if (keydown[K_CTRL])
	{
		if ((key == 'u' || key == 'U') && versionmenu.list.search.len > 0)
		{
			versionmenu.list.search.len = 0;
			versionmenu.list.search.text[0] = 0;
			versionmenu.list.cursor = 0;
			versionmenu.list.scroll = 0;
			M_Version_Refilter();
			return;
		}
		else if (key == K_BACKSPACE && versionmenu.list.search.len > 0)
		{
			M_DeletePrevWord(&versionmenu.list.search);
			versionmenu.list.cursor = 0;
			versionmenu.list.scroll = 0;
			M_Version_Refilter();
			return;
		}
	}

	if (key >= 32 && key < 127)
	{
		if (versionmenu.list.search.len < versionmenu.list.search.maxlen)
		{
			versionmenu.list.search.text[versionmenu.list.search.len++] = key;
			versionmenu.list.search.text[versionmenu.list.search.len] = 0;
			versionmenu.list.cursor = 0;
			versionmenu.list.scroll = 0;
			M_Version_Refilter();
		}
		return;
	}

	if (key == K_BACKSPACE && versionmenu.list.search.len > 0)
	{
		versionmenu.list.search.text[--versionmenu.list.search.len] = 0;
		versionmenu.list.cursor = 0;
		versionmenu.list.scroll = 0;
		M_Version_Refilter();
		return;
	}

	if (versionmenu.scrollbar_grab)
	{
		switch (key)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			versionmenu.scrollbar_grab = false;
			break;
		}
		return;
	}

	if (versionmenu.list.numitems > 0 && M_List_Key(&versionmenu.list, key))
		return;

	if (M_Ticker_Key(&versionmenu.ticker, key))
		return;

	switch (key)
	{
	case K_ESCAPE:
		if (versionmenu.list.search.len > 0)
		{
			versionmenu.list.search.len = 0;
			versionmenu.list.search.text[0] = 0;
			versionmenu.list.cursor = 0;
			versionmenu.list.scroll = 0;
			M_Version_Refilter();
			return;
		}
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		M_Menu_Extras_f();
		break;

	case K_MOUSE1:
		if (versionmenu.list.numitems > 0)
		{
			int x = m_mousex - versionmenu.x - (versionmenu.cols - 1) * 8;
			int y = m_mousey - versionmenu.y;
			if (x >= -8 && M_List_UseScrollbar(&versionmenu.list, y))
			{
				versionmenu.scrollbar_grab = true;
				M_Version_Mousemove(m_mousex, m_mousey);
			}
		}
		break;

	default:
		break;
	}
}

void M_Version_Mousemove(int cx, int cy)
{
	cy -= versionmenu.y;

	if (versionmenu.scrollbar_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			versionmenu.scrollbar_grab = false;
			return;
		}
		M_List_UseScrollbar(&versionmenu.list, cy);
	}

	if (versionmenu.list.numitems > 0)
		M_List_Mousemove(&versionmenu.list, cy);
}

/*
==================
Reset Config Menu
==================
*/

#define MAX_VIS_RESETCONFIG	17
#define RESETCONFIG_SEARCH_BOX_X	16
#define RESETCONFIG_SEARCH_BOX_Y	180
#define RESETCONFIG_SEARCH_BOX_COLS	32
#define RESETCONFIG_SEARCH_TEXT_X	(RESETCONFIG_SEARCH_BOX_X + 8)
#define RESETCONFIG_SEARCH_TEXT_Y	(RESETCONFIG_SEARCH_BOX_Y + 8)

typedef struct
{
	char name[64];
	char date[32];
	qboolean active;
} resetconfigitem_t;

static struct
{
	menulist_t			list;
	enum m_state_e		prev;
	int					x, y, cols;
	int					prev_cursor;
	menuticker_t		ticker;
	resetconfigitem_t* items;
	qboolean			scrollbar_grab;
	int* filtered_indices;
	char				status_message[128]; // Add status message
	double				status_time; // Time when status was set
} resetconfigmenu;
static menu_textfield_t resetconfig_search_field;

static qboolean M_ResetConfig_ShowSearchBox(void)
{
	return resetconfigmenu.list.search.len > 0;
}

static qboolean M_ResetConfig_MouseInSearchBox(void)
{
	return M_ResetConfig_ShowSearchBox() &&
		m_mousex >= RESETCONFIG_SEARCH_BOX_X &&
		m_mousex <= RESETCONFIG_SEARCH_BOX_X + (RESETCONFIG_SEARCH_BOX_COLS + 2) * 8 &&
		m_mousey >= RESETCONFIG_SEARCH_BOX_Y &&
		m_mousey <= RESETCONFIG_SEARCH_BOX_Y + 16;
}

static void M_ResetConfig_Add(const char* name, const char* date)
{
	resetconfigitem_t tempConfig;
	q_strlcpy(tempConfig.name, name, sizeof(tempConfig.name));
	q_strlcpy(tempConfig.date, date, sizeof(tempConfig.date));
	tempConfig.active = false;

	// Find insertion position for date sorting (newest first)
	int insertPos = 0;
	int currentCount = VEC_SIZE(resetconfigmenu.items);

	for (int i = 0; i < currentCount; i++)
	{
		if (q_sortdemos(date, resetconfigmenu.items[i].date) > 0) // If new date is newer
		{
			insertPos = i;
			break;
		}
		insertPos = i + 1;
	}

	// Add the item using vector push
	VEC_PUSH(resetconfigmenu.items, tempConfig);

	// If we need to insert in the middle, shift items
	if (insertPos < currentCount)
	{
		// Move the newly added item from the end to the correct position
		resetconfigitem_t newItem = resetconfigmenu.items[currentCount]; // The item we just pushed

		// Shift items to make room
		for (int i = currentCount; i > insertPos; i--)
		{
			resetconfigmenu.items[i] = resetconfigmenu.items[i - 1];
		}

		// Insert at correct position
		resetconfigmenu.items[insertPos] = newItem;
	}
}

static void M_ResetConfig_Refilter(void)
{
	int i;
	int itemCount = VEC_SIZE(resetconfigmenu.items);
	VEC_CLEAR(resetconfigmenu.filtered_indices);

	for (i = 0; i < itemCount; i++)
	{
		if (resetconfigmenu.list.search.len == 0 ||
			q_strcasestr(resetconfigmenu.items[i].name, resetconfigmenu.list.search.text) ||
			q_strcasestr(resetconfigmenu.items[i].date, resetconfigmenu.list.search.text))
		{
			VEC_PUSH(resetconfigmenu.filtered_indices, i);
		}
	}

	resetconfigmenu.list.numitems = VEC_SIZE(resetconfigmenu.filtered_indices);

	if (resetconfigmenu.list.cursor >= resetconfigmenu.list.numitems)
		resetconfigmenu.list.cursor = resetconfigmenu.list.numitems - 1;

	if (resetconfigmenu.list.cursor < 0 && resetconfigmenu.list.numitems > 0)
		resetconfigmenu.list.cursor = 0;

	M_List_CenterCursor(&resetconfigmenu.list);
}

static void M_ResetConfig_SyncSearchField(void)
{
	resetconfigmenu.list.search.len = (int)strlen(resetconfigmenu.list.search.text);
	if (resetconfigmenu.list.search.len >= resetconfigmenu.list.search.maxlen)
	{
		resetconfigmenu.list.search.len = resetconfigmenu.list.search.maxlen - 1;
		resetconfigmenu.list.search.text[resetconfigmenu.list.search.len] = 0;
	}
	M_TextField_ClampCursor(&resetconfig_search_field);
}

static void M_ResetConfig_Init(void)
{
#ifdef _WIN32
	WIN32_FIND_DATA fdat;
	HANDLE fhnd;
	char filestring[MAX_OSPATH];
	char configname[64];
	char configdate[32];
	char sortdate[32]; // For converted date format

	resetconfigmenu.list.viewsize = MAX_VIS_RESETCONFIG;
	resetconfigmenu.list.cursor = -1;
	resetconfigmenu.list.scroll = 0;
	resetconfigmenu.scrollbar_grab = false;

	// Clear vectors
	VEC_CLEAR(resetconfigmenu.items);
	VEC_CLEAR(resetconfigmenu.filtered_indices);

	memset(&resetconfigmenu.list.search, 0, sizeof(resetconfigmenu.list.search));
	resetconfigmenu.list.search.maxlen = 32;

	// Clear status message
	resetconfigmenu.status_message[0] = '\0';
	resetconfigmenu.status_time = 0;

	M_Ticker_Init(&resetconfigmenu.ticker);

	// Add default config as first item (always at top)
	M_ResetConfig_Add("default config", "9999-12-31 23:59:59"); // Future date to ensure it stays at top

	// Search in backups folder for config-*.cfg files
	q_snprintf(filestring, sizeof(filestring), "%s/backups/config-*.cfg", com_gamedir);
	fhnd = FindFirstFile(filestring, &fdat);
	if (fhnd != INVALID_HANDLE_VALUE)
	{
		do
		{
			q_strlcpy(configname, fdat.cFileName, sizeof(configname));

			// Extract date from filename (config-MM-DD-YYYY.cfg)
			if (strncmp(configname, "config-", 7) == 0)
			{
				char* datepart = configname + 7; // Skip "config-"
				char* dotpos = strrchr(datepart, '.');
				if (dotpos) *dotpos = '\0'; // Remove .cfg extension

				q_strlcpy(configdate, datepart, sizeof(configdate));

				// Convert MM-DD-YYYY to YYYY-MM-DD 00:00:00 for sorting
				if (strlen(configdate) == 10) // MM-DD-YYYY format
				{
					char month[3], day[3], year[5];
					if (sscanf(configdate, "%2s-%2s-%4s", month, day, year) == 3)
					{
						q_snprintf(sortdate, sizeof(sortdate), "%s-%s-%s 00:00:00", year, month, day);
					}
					else
					{
						q_strlcpy(sortdate, configdate, sizeof(sortdate));
					}
				}
				else
				{
					q_strlcpy(sortdate, configdate, sizeof(sortdate));
				}

				M_ResetConfig_Add(configname, sortdate);
			}
		} while (FindNextFile(fhnd, &fdat));
		FindClose(fhnd);
	}
#else
	DIR* dir_p;
	struct dirent* dir_t;
	char filestring[MAX_OSPATH];
	char configname[64];
	char configdate[32];
	char sortdate[32]; // For converted date format

	resetconfigmenu.list.viewsize = MAX_VIS_RESETCONFIG;
	resetconfigmenu.list.cursor = -1;
	resetconfigmenu.list.scroll = 0;
	resetconfigmenu.scrollbar_grab = false;

	// Clear vectors
	VEC_CLEAR(resetconfigmenu.items);
	VEC_CLEAR(resetconfigmenu.filtered_indices);

	memset(&resetconfigmenu.list.search, 0, sizeof(resetconfigmenu.list.search));
	resetconfigmenu.list.search.maxlen = 32;

	// Clear status message
	resetconfigmenu.status_message[0] = '\0';
	resetconfigmenu.status_time = 0;

	M_Ticker_Init(&resetconfigmenu.ticker);

	// Add default config as first item (always at top)
	M_ResetConfig_Add("default config", "9999-12-31 23:59:59"); // Future date to ensure it stays at top

	// Search in backups folder for config-*.cfg files
	q_snprintf(filestring, sizeof(filestring), "%s/backups", com_gamedir);
	dir_p = opendir(filestring);
	if (dir_p != NULL)
	{
		while ((dir_t = readdir(dir_p)) != NULL)
		{
			if (q_strcasecmp(COM_FileGetExtension(dir_t->d_name), "cfg") != 0)
				continue;

			if (strncmp(dir_t->d_name, "config-", 7) != 0)
				continue;

			q_strlcpy(configname, dir_t->d_name, sizeof(configname));

			// Extract date from filename (config-MM-DD-YYYY.cfg)
			char* datepart = configname + 7; // Skip "config-"
			char* dotpos = strrchr(datepart, '.');
			if (dotpos) *dotpos = '\0'; // Remove .cfg extension

			q_strlcpy(configdate, datepart, sizeof(configdate));

			// Convert MM-DD-YYYY to YYYY-MM-DD 00:00:00 for sorting
			if (strlen(configdate) == 10) // MM-DD-YYYY format
			{
				char month[3], day[3], year[5];
				if (sscanf(configdate, "%2s-%2s-%4s", month, day, year) == 3)
				{
					q_snprintf(sortdate, sizeof(sortdate), "%s-%s-%s 00:00:00", year, month, day);
				}
				else
				{
					q_strlcpy(sortdate, configdate, sizeof(sortdate));
				}
			}
			else
			{
				q_strlcpy(sortdate, configdate, sizeof(sortdate));
			}

			M_ResetConfig_Add(configname, sortdate);
		}
		closedir(dir_p);
	}
#endif

	M_ResetConfig_Refilter();
	M_TextField_Init(&resetconfig_search_field,
		resetconfigmenu.list.search.text,
		resetconfigmenu.list.search.maxlen - 1,
		false);

	if (resetconfigmenu.list.cursor == -1 && resetconfigmenu.list.numitems > 0)
		resetconfigmenu.list.cursor = 0;

	M_List_CenterCursor(&resetconfigmenu.list);
}

void M_Menu_ResetConfig_f(void)
{
	key_dest = key_menu;
	resetconfigmenu.prev = m_state;
	m_state = m_resetconfig;
	m_entersound = true;
	M_ResetConfig_Init();
}

void M_ResetConfig_Draw(void)
{
	int x, y, i, cols;
	int firstvis, numvis;

	M_TextField_CheckMouseRelease();

	x = 16;
	y = 32;
	cols = 36;

	resetconfigmenu.x = x;
	resetconfigmenu.y = y;
	resetconfigmenu.cols = cols;

	if (!keydown[K_MOUSE1]) // woods #mousemenu
		resetconfigmenu.scrollbar_grab = false;

	if (resetconfigmenu.prev_cursor != resetconfigmenu.list.cursor)
	{
		resetconfigmenu.prev_cursor = resetconfigmenu.list.cursor;
		M_Ticker_Init(&resetconfigmenu.ticker);
	}
	else
		M_Ticker_Update(&resetconfigmenu.ticker);

	Draw_String(x, y - 28, "Reset Config");
	M_DrawQuakeBar(x - 8, y - 16, cols + 2);

	M_List_GetVisibleRange(&resetconfigmenu.list, &firstvis, &numvis);
	for (i = 0; i < numvis; i++)
	{
		int idx = i + firstvis;
		int config_idx = resetconfigmenu.filtered_indices[idx];
		resetconfigitem_t* config_item = &resetconfigmenu.items[config_idx];
		qboolean selected = (idx == resetconfigmenu.list.cursor);

		int color = config_item->active ? 0 : 1;
		int len = strlen(config_item->name);
		int maxchars = (cols - 2);

		if (resetconfigmenu.list.search.len > 0)
		{
			if (len <= maxchars)
			{
				// No scrolling needed, display with highlighting
				M_PrintHighlight(x, y + i * 8, config_item->name, resetconfigmenu.list.search.text, resetconfigmenu.list.search.len);
			}
			else
			{
				// Scrolling needed, display with scrolling and highlighting
				M_PrintHighlightScroll(x, y + i * 8, (cols - 2) * 8,
					config_item->name, resetconfigmenu.list.search.text,
					selected ? resetconfigmenu.ticker.scroll_time : 0.0);
			}
		}
		else
		{
			if (len <= maxchars)
			{
				// No scrolling needed
				if (color)
					M_Print(x, y + i * 8, config_item->name);
				else
					M_PrintWhite(x, y + i * 8, config_item->name);
			}
			else
			{
				// Scrolling needed
				M_PrintScroll(x, y + i * 8, (cols - 2) * 8,
					config_item->name,
					selected ? resetconfigmenu.ticker.scroll_time : 0.0,
					color);
			}
		}

		if (selected)
			M_DrawCharacter(x - 8, y + i * 8, 12 + ((int)(realtime * 4) & 1));
	}

	if (M_List_GetOverflow(&resetconfigmenu.list) > 0)
	{
		M_List_DrawScrollbar(&resetconfigmenu.list, x + cols * 8 - 8, y);

		if (resetconfigmenu.list.scroll > 0)
			M_DrawEllipsisBar(x, y - 8, cols);
		if (resetconfigmenu.list.scroll + resetconfigmenu.list.viewsize < resetconfigmenu.list.numitems)
			M_DrawEllipsisBar(x, y + resetconfigmenu.list.viewsize * 8, cols);
	}

	if (M_ResetConfig_ShowSearchBox())
	{
		M_DrawTextBox(RESETCONFIG_SEARCH_BOX_X, RESETCONFIG_SEARCH_BOX_Y, RESETCONFIG_SEARCH_BOX_COLS, 1);
		M_TextField_DrawHighlight(&resetconfig_search_field, RESETCONFIG_SEARCH_TEXT_X, RESETCONFIG_SEARCH_TEXT_Y);
		M_PrintHighlight(RESETCONFIG_SEARCH_TEXT_X, RESETCONFIG_SEARCH_TEXT_Y, resetconfigmenu.list.search.text,
			resetconfigmenu.list.search.text,
			resetconfigmenu.list.search.len);
		int cursor_x = RESETCONFIG_SEARCH_TEXT_X + 8 * resetconfig_search_field.cursor;
		if (resetconfigmenu.list.numitems == 0)
			M_DrawCharacter(cursor_x, RESETCONFIG_SEARCH_TEXT_Y, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, RESETCONFIG_SEARCH_TEXT_Y, 10 + ((int)(realtime * 4) & 1));
	}

	// Display status message if recent (show for 3 seconds)
	if (resetconfigmenu.status_message[0] && (realtime - resetconfigmenu.status_time) < 3.0)
	{
		int status_y = y + resetconfigmenu.list.viewsize * 8 + 16;
		M_PrintWhite(x, status_y, resetconfigmenu.status_message);
	}
}

qboolean M_ResetConfig_Match(int index, char initial)
{
	int config_idx = resetconfigmenu.filtered_indices[index];
	return q_tolower(resetconfigmenu.items[config_idx].name[0]) == initial;
}

void M_ResetConfig_Key(int key)
{
	int x, y; // woods #mousemenu

	if (resetconfigmenu.scrollbar_grab)
	{
		switch (key)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			resetconfigmenu.scrollbar_grab = false;
			break;
		}
		return;
	}

	if (M_TextField_Key(&resetconfig_search_field, key))
	{
		M_ResetConfig_SyncSearchField();
		M_ResetConfig_Refilter();
		return;
	}

	if (M_List_Key(&resetconfigmenu.list, key))
		return;

	if (M_List_CycleMatch(&resetconfigmenu.list, key, M_ResetConfig_Match))
		return;

	if (M_Ticker_Key(&resetconfigmenu.ticker, key))
		return;

	switch (key)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		if (resetconfigmenu.prev == m_options)
			M_Menu_Options_f();
		else
			M_Menu_Main_f();
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	enter:
		if (resetconfigmenu.list.numitems > 0 && resetconfigmenu.list.cursor >= 0)
		{
			int config_idx = resetconfigmenu.filtered_indices[resetconfigmenu.list.cursor];
			resetconfigitem_t* config_item = &resetconfigmenu.items[config_idx];

			// Check if this is the default config option
			if (!strcmp(config_item->name, "default config"))
			{
				if (SCR_ModalMessage("This will reset all controls\n"
					"and stored vars. Continue? (^my^m/^mn^m)\n", 15.0f))
				{
					Cbuf_AddText("resetcfg\n");
					Cbuf_AddText("exec default.cfg\n");

					// Set status message
					q_strlcpy(resetconfigmenu.status_message, "reset to default config", sizeof(resetconfigmenu.status_message));
					resetconfigmenu.status_time = realtime;
				}
			}
			else
			{
				// Execute the config file
				char exec_cmd[256];
				q_snprintf(exec_cmd, sizeof(exec_cmd), "exec backups/%s.cfg\n", config_item->name);
				Cbuf_AddText(exec_cmd);

				// Set status message
				q_snprintf(resetconfigmenu.status_message, sizeof(resetconfigmenu.status_message),
					"loaded config %s", config_item->name);
				resetconfigmenu.status_time = realtime;
			}
		}
		break;

	case K_MOUSE1: // woods #mousemenu
		if (M_ResetConfig_MouseInSearchBox())
		{
			M_TextField_MouseClick(&resetconfig_search_field, m_mousex, RESETCONFIG_SEARCH_TEXT_X);
			return;
		}

		x = m_mousex - resetconfigmenu.x - (resetconfigmenu.cols - 1) * 8;
		y = m_mousey - resetconfigmenu.y;
		if (x < -8 || !M_List_UseScrollbar(&resetconfigmenu.list, y))
			goto enter;
		resetconfigmenu.scrollbar_grab = true;
		M_ResetConfig_Mousemove(m_mousex, m_mousey);
		break;

	default:
		break;
	}
}
void M_ResetConfig_Char(int key)
{
	if (M_TextField_Char(&resetconfig_search_field, key))
	{
		M_ResetConfig_SyncSearchField();
		M_ResetConfig_Refilter();
	}
}

qboolean M_ResetConfig_TextEntry(void)
{
	return true; // Always allow text entry for search
}


void M_ResetConfig_Mousemove(int cx, int cy) // woods #mousemenu
{
	if (textfield_mouse_dragging && textfield_drag_field == &resetconfig_search_field)
	{
		M_TextField_MouseDrag(cx);
		return;
	}

	cy -= resetconfigmenu.y;

	if (resetconfigmenu.scrollbar_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			resetconfigmenu.scrollbar_grab = false;
			return;
		}
		M_List_UseScrollbar(&resetconfigmenu.list, cy);
		// Note: no return, we also update the cursor
	}

	M_List_Mousemove(&resetconfigmenu.list, cy);
}

/*
==================
Video Menu
==================
*/

void M_Menu_Video_f (void)
{
	(*vid_menucmdfn) (); //johnfitz
}


void M_Video_Draw (void)
{
	(*vid_menudrawfn) ();
}


void M_Video_Key (int key)
{
	(*vid_menukeyfn) (key);
}

void M_Video_Mousemove(int cx, int cy) // woods #mousemenu
{
	(*vid_menumousefn) (cx, cy);
}

/*
==================
Help Menu
==================
*/

int		help_page;
#define	NUM_HELP_PAGES	6


void M_Menu_Help_f (void)
{
	key_dest = key_menu;
	m_state = m_help;
	m_entersound = true;
	help_page = 0;
	IN_UpdateGrabs();
	SCR_ModalMessage("The QSS-M webpage has been opened\nin your ^mweb browser^m\n\nMinimize QSS-M for further assistance", 3.5f); // woods
	SDL_OpenURL("https://qssm.quakeone.com");
}



void M_Help_Draw (void)
{
	M_DrawPic (0, 0, Draw_CachePic ( va("gfx/help%i.lmp", help_page)) );
}


void M_Help_Key (int key)
{
	switch (key)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		M_Menu_Main_f ();
		break;

	case K_UPARROW:
	case K_RIGHTARROW:
	case K_MWHEELDOWN: // woods #mousemenu
	case K_MOUSE1:
		m_entersound = true;
		if (++help_page >= NUM_HELP_PAGES)
			help_page = 0;
		break;

	case K_DOWNARROW:
	case K_LEFTARROW:
	case K_MWHEELUP: // woods #mousemenu
		//case K_MOUSE2:
		m_entersound = true;
		if (--help_page < 0)
			help_page = NUM_HELP_PAGES-1;
		break;
	}

}

/*
==================
Quit Menu
==================
*/

int		msgNumber;
enum m_state_e	m_quit_prevstate;
qboolean	wasInMenus;

void M_Menu_Quit_f (void)
{
	if (m_state == m_quit)
		return;
	wasInMenus = (key_dest == key_menu);
	key_dest = key_menu;
	m_quit_prevstate = m_state;
	m_state = m_quit;
	m_entersound = true;
	msgNumber = rand()&7;

	IN_UpdateGrabs();
}


void M_Quit_Key (int key)
{
	switch (key)
	{
	case K_ESCAPE:
	case K_BBUTTON:
		if (wasInMenus)
		{
			m_state = m_quit_prevstate;
			m_entersound = true;
		}
		else
		{
			key_dest = key_game;
			m_state = m_none;
			IN_UpdateGrabs();
		}
		break;

	case K_ABUTTON:
		key_dest = key_console;
		Host_Quit_f ();
		break;

	default:
		break;
	}
}


void M_Quit_Char (int key)
{
	switch (key)
	{
	case 'n':
	case 'N':
		if (wasInMenus)
		{
			m_state = m_quit_prevstate;
			m_entersound = true;
		}
		else
		{
			key_dest = key_game;
			m_state = m_none;
			IN_UpdateGrabs();
		}
		break;

	case 'y':
	case 'Y':
		key_dest = key_console;
		Host_Quit_f ();
		IN_UpdateGrabs();
		break;

	default:
		break;
	}

}


qboolean M_Quit_TextEntry (void)
{
	return true;
}


void M_Quit_Draw (void) //johnfitz -- modified for new quit message -- woods modified for match quit warning #matchquit
{
	char	msg1[] = "you are currently a match participant";
	char	msg2[] = "quiting will disrupt the match"; /* msg2/msg3 are [38] at most */
	char	msg3[] = "press y to quit";
	int		boxlen;

	if (wasInMenus)
	{
		m_state = m_quit_prevstate;
		m_recursiveDraw = true;
		M_Draw ();
		m_state = m_quit;
	}

	//okay, this is kind of fucked up.  M_DrawTextBox will always act as if
	//width is even. Also, the width and lines values are for the interior of the box,
	//but the x and y values include the border.
	boxlen = (q_max(sizeof(msg1), q_max(sizeof(msg2),sizeof(msg3))) + 1) & ~1;
	M_DrawTextBox	(160-4*(boxlen+2), 76, boxlen, 4);

	//now do the text
	M_Print			(160-4*(sizeof(msg1)-1), 88, msg1);
	M_Print			(160-4*(sizeof(msg2)-1), 96, msg2);
	M_PrintWhite		(160-4*(sizeof(msg3)-1), 104, msg3);
}

/*
==================
LAN Config Menu
==================
*/

int		lanConfig_cursor = -1;
int     lanConfig_cursor_table_newgame[] = { 76, 86, 104 };
int     lanConfig_cursor_table_newgame_ice[] = { 76, 92, 108, 124 };
int		lanConfig_cursor_table[] = { 76, 94, 102, 108, 116, 124 }; // woods #mousemenu #bookmarksmenu
int*	lanConfig_cursor_ptr = NULL; // Pointer to the current cursor table

int     NUM_LANCONFIG_CMDS;
#define NUM_LANCONFIG_CMDS_NEWGAME 4
#define NUM_LANCONFIG_CMDS_JOINGAME 6
#define LANCONFIG_CURSOR_PORT 0
#define LANCONFIG_CURSOR_NEWGAME_ROOM 1
#define LANCONFIG_CURSOR_NEWGAME_PROTOCOL 2
#define LANCONFIG_CURSOR_NEWGAME_OK 3
#define LANCONFIG_CURSOR_JOINGAME_SEARCH_LAN 1
#define LANCONFIG_CURSOR_JOINGAME_SEARCH_WEB 2
#define LANCONFIG_CURSOR_JOINGAME_HISTORY 3
#define LANCONFIG_CURSOR_JOINGAME_BOOKMARKS 4
#define LANCONFIG_CURSOR_JOINGAME_JOIN 5
#define LANCONFIG_PROTOCOL_BASE_COUNT 3
#define LANCONFIG_PROTOCOL_COUNT 6

int 	lanConfig_port;
char	lanConfig_portname[6];
char	lanConfig_roomname[13];
char	lanConfig_joinname[22];
int     lanConfig_protocol_cursor = 0; // Track selected protocol
static menu_textfield_t lanConfig_port_field;
static menu_textfield_t lanConfig_room_field;
static menu_textfield_t lanConfig_join_field;
static char lanConfig_porthint[6];
static char lanConfig_joinhint[22];
static char lanConfig_join_tabpartial[22];

extern int sv_protocol;
extern unsigned int	sv_protocol_pext2;
extern cvar_t sv_port_rtc;

typedef struct {
	int x;
	int y;
	int width;
	char text[128];
	int label_x;
	int label_width;
} clickable_text_t;

static clickable_text_t ip_clickables[2];  // For local and external IPs
static float copy_message_time = 0;
static char last_copied_ip[128] = "";

static qboolean addresses_cached = false;
static qhostaddr_t cached_addresses[16];
static int cached_numaddresses = 0;

static qboolean M_LanConfig_HasIce(void)
{
	return !safemode && COM_CheckParm("-useice") && !COM_CheckParm("-noice");
}

static qboolean M_LanConfig_ShowRoomField(void)
{
	return StartingGame && M_LanConfig_HasIce();
}

static int M_LanConfig_NewGameNumCommands(void)
{
	return M_LanConfig_ShowRoomField() ? NUM_LANCONFIG_CMDS_NEWGAME : NUM_LANCONFIG_CMDS_NEWGAME - 1;
}

static int M_LanConfig_NewGameProtocolCursor(void)
{
	return M_LanConfig_ShowRoomField() ? LANCONFIG_CURSOR_NEWGAME_PROTOCOL : LANCONFIG_CURSOR_NEWGAME_ROOM;
}

static int M_LanConfig_NewGameOkCursor(void)
{
	return M_LanConfig_ShowRoomField() ? LANCONFIG_CURSOR_NEWGAME_OK : LANCONFIG_CURSOR_NEWGAME_PROTOCOL;
}

static menu_textfield_t *M_LanConfig_GetFieldForCursor(void)
{
	if (lanConfig_cursor == LANCONFIG_CURSOR_PORT)
		return &lanConfig_port_field;
	if (M_LanConfig_ShowRoomField() && lanConfig_cursor == LANCONFIG_CURSOR_NEWGAME_ROOM)
		return &lanConfig_room_field;
	if (JoiningGame && lanConfig_cursor == LANCONFIG_CURSOR_JOINGAME_JOIN)
		return &lanConfig_join_field;
	return NULL;
}

static void M_LanConfig_ClearTextSelections(void)
{
	M_TextField_ClearSelection(&lanConfig_port_field);
	M_TextField_ClearSelection(&lanConfig_room_field);
	M_TextField_ClearSelection(&lanConfig_join_field);
}

static void M_LanConfig_UpdatePortHint(void)
{
	static const char default_port[] = "26000";
	int len = (int)strlen(lanConfig_portname);

	lanConfig_porthint[0] = '\0';

	if (!q_strncasecmp(default_port, lanConfig_portname, len))
		q_strlcpy(lanConfig_porthint, default_port + len, sizeof(lanConfig_porthint));
}

static void M_LanConfig_UpdateJoinHint(void)
{
	filelist_item_t *item;
	int len = (int)strlen(lanConfig_joinname);

	lanConfig_joinhint[0] = '\0';

	if (len <= 0)
		return;

	for (item = serverlist; item; item = item->next)
	{
		if (!q_strncasecmp(item->name, lanConfig_joinname, len))
		{
			q_strlcpy(lanConfig_joinhint, item->name + len, sizeof(lanConfig_joinhint));
			return;
		}
	}
}

static qboolean M_LanConfig_AcceptPortHint(void)
{
	if (!lanConfig_porthint[0])
		return false;

	if (lanConfig_port_field.cursor != (int)strlen(lanConfig_portname))
		return false;

	q_strlcat(lanConfig_portname, lanConfig_porthint, sizeof(lanConfig_portname));
	lanConfig_port_field.cursor = (int)strlen(lanConfig_portname);
	lanConfig_port_field.sel_start = -1;
	M_TextField_ClampCursor(&lanConfig_port_field);
	M_LanConfig_UpdatePortHint();
	return true;
}

static void M_LanConfig_UpdateHints(void)
{
	M_LanConfig_UpdatePortHint();
	M_LanConfig_UpdateJoinHint();
}

static void M_LanConfig_NormalizeRoomField(void)
{
	int len;

	if (!lanConfig_roomname[0] || lanConfig_roomname[0] == '/')
		return;

	len = (int)strlen(lanConfig_roomname);
	if (len >= (int)sizeof(lanConfig_roomname) - 1)
		len = (int)sizeof(lanConfig_roomname) - 2;

	memmove(lanConfig_roomname + 1, lanConfig_roomname, len + 1);
	lanConfig_roomname[0] = '/';
	lanConfig_roomname[len + 1] = '\0';

	lanConfig_room_field.cursor++;
	if (lanConfig_room_field.sel_start >= 0)
		lanConfig_room_field.sel_start++;
	M_TextField_ClampCursor(&lanConfig_room_field);
}

static void M_LanConfig_SyncRoomField(void)
{
	if (!M_LanConfig_HasIce())
		return;

	Cvar_Set(sv_port_rtc.name, lanConfig_roomname);
}

void SetProtocol(int protocol_cursor)
{
	if (protocol_cursor < LANCONFIG_PROTOCOL_BASE_COUNT)
	{
		// Set base protocols (no FTE extensions)
		switch (protocol_cursor)
		{
		case 0: Cbuf_AddText("sv_protocol Base-15\n"); break; // PROTOCOL_NETQUAKE
		case 1: Cbuf_AddText("sv_protocol Base-666\n"); break; // PROTOCOL_FITZQUAKE
		case 2: Cbuf_AddText("sv_protocol Base-999\n"); break; // PROTOCOL_RMQ
		}
	}
	else
	{
		// Set FTE+ protocols (with extensions)
		switch (protocol_cursor - LANCONFIG_PROTOCOL_BASE_COUNT) // Adjust cursor for FTE+ options
		{
		case 0: Cbuf_AddText("sv_protocol FTE+15\n"); break; // PROTOCOL_NETQUAKE with FTE extensions
		case 1: Cbuf_AddText("sv_protocol FTE+666\n"); break; // PROTOCOL_FITZQUAKE with FTE extensions
		case 2: Cbuf_AddText("sv_protocol FTE+999\n"); break; // PROTOCOL_RMQ with FTE extensions
		}
	}
}

const char* GetProtocolDescription(int protocol_cursor)
{
	if (protocol_cursor < LANCONFIG_PROTOCOL_BASE_COUNT)
	{
		// Base protocols (no FTE extensions)
		switch (protocol_cursor)
		{
		case 0: return "15 (netquake)";
		case 1: return "666 (fitzquake)";
		case 2: return "999 (rmq)";
		default: return "Unknown";
		}
	}
	else
	{
		// FTE+ protocols (with extensions)
		switch (protocol_cursor - LANCONFIG_PROTOCOL_BASE_COUNT) // Adjust cursor for FTE+ options
		{
		case 0: return "FTE+15 (netquake+pext)";
		case 1: return "FTE+666 (fitzquake+pext)";
		case 2: return "FTE+999 (rmq+pext)";
		default: return "Unknown";
		}
	}
}

void M_Menu_LanConfig_f (void)
{
	key_dest = key_menu;
	m_state = m_lanconfig;
	m_entersound = true;
	
	addresses_cached = false;

	if (StartingGame)
	{
		// Use New Game configuration
		lanConfig_cursor_ptr = M_LanConfig_ShowRoomField() ? lanConfig_cursor_table_newgame_ice : lanConfig_cursor_table_newgame;
		NUM_LANCONFIG_CMDS = M_LanConfig_NewGameNumCommands();
		// Map sv_protocol to corresponding protocol cursor
		switch (sv_protocol)
		{
		case 15:
			lanConfig_protocol_cursor = 0; // PROTOCOL_NETQUAKE
			break;
		case 666:
			lanConfig_protocol_cursor = 1; // PROTOCOL_FITZQUAKE
			break;
		case 999:
			lanConfig_protocol_cursor = 2; // PROTOCOL_RMQ
			break;
		default:
			lanConfig_protocol_cursor = 0; // Default to base protocol if unknown
			break;
		}

		// If FTE extensions are enabled, shift to the FTE+ protocol entries.
		if (sv_protocol_pext2)
		{
			lanConfig_protocol_cursor += LANCONFIG_PROTOCOL_BASE_COUNT; // Shift to FTE+ versions
		}
	}
	else
	{
		// Use Join Game configuration
		lanConfig_cursor_ptr = lanConfig_cursor_table;
		NUM_LANCONFIG_CMDS = NUM_LANCONFIG_CMDS_JOINGAME;
	}

	if (lanConfig_cursor == -1)
	{
		if (StartingGame)
			lanConfig_cursor = M_LanConfig_NewGameProtocolCursor();
		else if (JoiningGame && TCPIPConfig)
			lanConfig_cursor = LANCONFIG_CURSOR_JOINGAME_SEARCH_WEB;
		else
			lanConfig_cursor = LANCONFIG_CURSOR_JOINGAME_SEARCH_LAN;
	}
	if (StartingGame && lanConfig_cursor >= NUM_LANCONFIG_CMDS)
		lanConfig_cursor = M_LanConfig_NewGameProtocolCursor();
	lanConfig_port = DEFAULTnet_hostport;
	sprintf(lanConfig_portname, "%u", lanConfig_port);
	q_strlcpy(lanConfig_roomname, sv_port_rtc.string, sizeof(lanConfig_roomname));
	M_TextField_Init(&lanConfig_port_field, lanConfig_portname, 5, true);
	M_TextField_Init(&lanConfig_room_field, lanConfig_roomname, sizeof(lanConfig_roomname) - 1, false);
	M_TextField_Init(&lanConfig_join_field, lanConfig_joinname, 21, false);
	lanConfig_join_tabpartial[0] = '\0';
	M_LanConfig_NormalizeRoomField();
	M_LanConfig_SyncRoomField();
	M_LanConfig_UpdateHints();

	m_return_onerror = false;
	m_return_reason[0] = 0;
	IN_UpdateGrabs();
}

static qboolean ip_temporarily_visible[2] = { false, false };
static float ip_visibility_timeout[2] = { 0, 0 };
#define IP_VISIBILITY_DURATION 3.0f  // Show IP for 3 seconds after clicking

void M_LanConfig_CheckTimeouts(void)
{
	// Check for timeout on temporarily visible IPs
	for (int i = 0; i < 2; i++) {
		if (ip_temporarily_visible[i] && realtime > ip_visibility_timeout[i]) {
			ip_temporarily_visible[i] = false;
		}
	}
}

void M_LanConfig_Draw (void)
{
	M_LanConfig_CheckTimeouts(); // woods #contentfilter
	M_TextField_CheckMouseRelease();
	
	qpic_t	*p;
	int		basex;
	int		y;
	const char	*startJoin;
	//const char	*protocol;

	M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp") );
	p = Draw_CachePic ("gfx/p_multi.lmp");
	basex = (320-p->width)/2;
	M_DrawPic (basex, 4, p);

	basex = 72; /* Arcane Dimensions has an oversized gfx/p_multi.lmp */

	if (StartingGame)
		startJoin = "New Game";
	else
		startJoin = "Join Game";

	M_PrintWhite (basex, 32, va ("%s", startJoin));
	basex += 0;

	y = 52;
	M_Print(basex, y, "Address:");
	const int address_x = basex + (9 * 8);

	if (!addresses_cached) {
		cached_numaddresses = NET_ListAddresses(cached_addresses, sizeof(cached_addresses) / sizeof(cached_addresses[0]));
		addresses_cached = true;
	}

	if (!cached_numaddresses)
	{
		M_Print(address_x, y, "NONE KNOWN");
		y += 8;
	}
	else
	{
		// Store clickable areas for IPs and their labels
		// Local IP
		ip_clickables[0].label_x = address_x;
		ip_clickables[0].label_width = 7 * 8;
		ip_clickables[0].x = address_x + 7 * 8;
		ip_clickables[0].y = y;
		ip_clickables[0].width = strlen(cached_addresses[0]) * 8;
		strncpy(ip_clickables[0].text, cached_addresses[0], sizeof(ip_clickables[0].text));

		if (cl_contentfilter.value && !ip_temporarily_visible[0]) // woods #contentfilter
		{
			M_Print(address_x, y, "local: click to view");
			ip_clickables[0].width = 13 * 8; // Width of "click to view" text
		}
		else 
		{
		M_Print(address_x, y, va("local: %s", cached_addresses[0]));
		}
		y += 8;

		// External IP
		ip_clickables[1].label_x = address_x;
		ip_clickables[1].label_width = 7 * 8;
		ip_clickables[1].x = address_x + 7 * 8;
		ip_clickables[1].y = y;
		ip_clickables[1].width = strlen(my_public_ip) * 8;
		strncpy(ip_clickables[1].text, my_public_ip, sizeof(ip_clickables[1].text));

		if (cl_contentfilter.value && !ip_temporarily_visible[1]) // woods #contentfilter
		{
			M_Print(address_x, y, "ext:   click to view");
			ip_clickables[1].width = 13 * 8; // Width of "click to view" text
		}
		else 
		{
		M_Print(address_x, y, va("ext:   %s", my_public_ip));
		}
		y += 8;
	}

	y+=8;	//for the port's box
	M_Print (basex, y, "Port:");
	M_DrawTextBox (basex+8*10, y-8, 6, 1);
	M_TextField_DrawHighlight(&lanConfig_port_field, basex + 9 * 10, y);
	M_Print (basex+9*10, y, lanConfig_portname);
	if (lanConfig_cursor == LANCONFIG_CURSOR_PORT &&
		lanConfig_porthint[0] &&
		lanConfig_port_field.cursor == (int)strlen(lanConfig_portname))
	{
		int hint_x = basex + 9 * 10 + (int)strlen(lanConfig_portname) * 8;
		M_PrintRGBA(hint_x, y, lanConfig_porthint, CL_PLColours_Parse("0xffffff"), 0.5f, true);
	}
	if (lanConfig_cursor == LANCONFIG_CURSOR_PORT)
	{
		M_TextField_DrawCursor(&lanConfig_port_field, basex + 9 * 10, y);
		M_DrawCharacter (basex-10, y, 12+((int)(realtime*4)&1));
	}
	y += 8;

	if (StartingGame)
	{
		y += 8;
		if (M_LanConfig_ShowRoomField())
		{
			M_Print(basex, y, "Room:");
			M_DrawTextBox(basex + 8 * 10, y - 8, 14, 1);
			if (lanConfig_cursor == LANCONFIG_CURSOR_NEWGAME_ROOM)
			{
				M_TextField_DrawHighlight(&lanConfig_room_field, basex + 9 * 10, y);
				M_Print(basex + 9 * 10, y, lanConfig_roomname);
				M_TextField_DrawCursor(&lanConfig_room_field, basex + 9 * 10, y);
				M_DrawCharacter(basex - 8, y, 12 + ((int)(realtime * 4) & 1));
			}
			else if (!lanConfig_roomname[0])
			{
				M_PrintRGBA(basex + 9 * 10, y, "<DISABLED>", CL_PLColours_Parse("0xffffff"), 0.5f, true);
			}
			else if (!strcmp(lanConfig_roomname, "/"))
			{
				M_PrintRGBA(basex + 9 * 10, y, "<AUTO>", CL_PLColours_Parse("0xffffff"), 0.5f, true);
			}
			else
			{
				M_Print(basex + 9 * 10, y, lanConfig_roomname);
			}

			y += 16;
		}

		M_Print(basex, y, "Protocol:");


		// Get the protocol description based on the current cursor value
		const char* protocolDescription = GetProtocolDescription(lanConfig_protocol_cursor);

		// Print the protocol description
		M_Print(basex + 9 * 9 + 1, y, protocolDescription);

		if (lanConfig_cursor == M_LanConfig_NewGameProtocolCursor())
		{
			M_DrawCharacter(basex - 8, y, 12 + ((int)(realtime * 4) & 1));
		}

		y += 16;
	}

	if (JoiningGame)
	{
		y += 8;
		
		M_Print (basex, y, "Search for local games...");
		if (lanConfig_cursor == LANCONFIG_CURSOR_JOINGAME_SEARCH_LAN)
			M_DrawCharacter (basex-8, y, 12+((int)(realtime*4)&1));
		y+=8;

		M_Print (basex, y, "Search for public games...");
		if (lanConfig_cursor == LANCONFIG_CURSOR_JOINGAME_SEARCH_WEB)
			M_DrawCharacter (basex-8, y, 12+((int)(realtime*4)&1));
		y+=8;

		M_Print(basex, y, "History"); // woods #historymenu
		if (lanConfig_cursor == LANCONFIG_CURSOR_JOINGAME_HISTORY)
			M_DrawCharacter(basex - 8, y, 12 + ((int)(realtime * 4) & 1));
		y += 8;

		M_Print(basex, y, "Bookmarks"); // woods #bookmarksmenu
		if (lanConfig_cursor == LANCONFIG_CURSOR_JOINGAME_BOOKMARKS)
			M_DrawCharacter(basex - 8, y, 12 + ((int)(realtime * 4) & 1));
		y += 8;

		M_Print (basex, y, "Join game at:");
		y+=24;
			M_DrawTextBox (basex+8, y-8, 22, 1);
			M_TextField_DrawHighlight(&lanConfig_join_field, basex + 16, y);
			M_Print (basex+16, y, lanConfig_joinname);
			if (lanConfig_cursor == LANCONFIG_CURSOR_JOINGAME_JOIN &&
				lanConfig_joinhint[0] &&
				lanConfig_join_field.cursor == (int)strlen(lanConfig_joinname))
			{
				int hint_x = basex + 16 + (int)strlen(lanConfig_joinname) * 8;
				M_PrintRGBA(hint_x, y, lanConfig_joinhint, CL_PLColours_Parse("0xffffff"), 0.5f, true);
			}
			if (lanConfig_cursor == LANCONFIG_CURSOR_JOINGAME_JOIN) // woods #historymenu #bookmarksmenu
			{
				M_TextField_DrawCursor(&lanConfig_join_field, basex + 16, y);
				M_DrawCharacter (basex-8, y, 12+((int)(realtime*4)&1));
			}
			y += 16;
	}
	else
	{
		M_DrawTextBox (basex, y-8, 2, 1);
		M_Print (basex+8, y, "OK");
		if (lanConfig_cursor == M_LanConfig_NewGameOkCursor())
			M_DrawCharacter (basex-8, y, 12+((int)(realtime*4)&1));
		y += 16;
	}

	if (realtime < copy_message_time)
	{
		char copy_message[256];
		snprintf(copy_message, sizeof(copy_message), "copied %s", last_copied_ip);
		M_PrintRGBA(basex, y + 8, copy_message, CL_PLColours_Parse("0xffffff"), 0.5f, false);
	}

	if (*m_return_reason)
		M_PrintWhite(basex, y, m_return_reason);
}

void M_LanConfig_Key (int key)
{
	int		l;
	menu_textfield_t *active_field;

	if (key == K_MOUSE1)
	{
		// Check if click was on either IP address or their labels
		for (int i = 0; i < 2; i++)
		{
			if ((m_mousex >= ip_clickables[i].x &&
				m_mousex <= ip_clickables[i].x + ip_clickables[i].width &&
				m_mousey >= ip_clickables[i].y &&
				m_mousey <= ip_clickables[i].y + 8) ||
				(m_mousex >= ip_clickables[i].label_x &&
					m_mousex <= ip_clickables[i].label_x + ip_clickables[i].label_width &&
					m_mousey >= ip_clickables[i].y &&
					m_mousey <= ip_clickables[i].y + 8))
			{
				if (cl_contentfilter.value)
				{
					if (!ip_temporarily_visible[i])
					{
						// If IP is not visible, make it visible
						ip_temporarily_visible[i] = true;
						ip_visibility_timeout[i] = realtime + IP_VISIBILITY_DURATION;
						S_LocalSound("misc/menu1.wav");
					}
					else 
					{
				SDL_SetClipboardText(ip_clickables[i].text);
				strcpy(last_copied_ip, ip_clickables[i].text);
				copy_message_time = realtime + 1.0;
				const char* soundFile = COM_FileExists("sound/qssm/copy.wav", NULL) ? "qssm/copy.wav" : "player/tornoff2.wav";
				S_LocalSound(soundFile);
					}
				}
				else 
				{
					SDL_SetClipboardText(ip_clickables[i].text);
					strcpy(last_copied_ip, ip_clickables[i].text);
					copy_message_time = realtime + 1.0;
					const char* soundFile = COM_FileExists("sound/qssm/copy.wav", NULL) ? "qssm/copy.wav" : "player/tornoff2.wav";
					S_LocalSound(soundFile);
				}
				return;
			}
		}
	}

	active_field = M_LanConfig_GetFieldForCursor();
	if (active_field && M_TextField_Key(active_field, key))
	{
		if (active_field == &lanConfig_join_field)
			lanConfig_join_tabpartial[0] = '\0';
		goto finish;
	}

	switch (key)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		M_Menu_MultiPlayer_f (); // woods #skipipx
		break;

	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		M_LanConfig_ClearTextSelections();
		lanConfig_cursor--;

		if (StartingGame) {
			if (lanConfig_cursor < 0) {
				lanConfig_cursor = NUM_LANCONFIG_CMDS - 1;
			}
		}
		else {
			if (lanConfig_cursor < 0) {
				lanConfig_cursor = NUM_LANCONFIG_CMDS_JOINGAME - 1;
			}
		}
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		M_LanConfig_ClearTextSelections();
		lanConfig_cursor++;

		if (StartingGame) {
			if (lanConfig_cursor >= NUM_LANCONFIG_CMDS) {
				lanConfig_cursor = 0;
			}
		}
		else {
			if (lanConfig_cursor >= NUM_LANCONFIG_CMDS_JOINGAME) {
				lanConfig_cursor = 0;
			}
		}
		break;

	case K_MWHEELUP:
	case K_LEFTARROW:
		if (StartingGame && lanConfig_cursor == M_LanConfig_NewGameProtocolCursor())
		{
			S_LocalSound("misc/menu1.wav");
			lanConfig_protocol_cursor--;
			if (lanConfig_protocol_cursor < 0)
				lanConfig_protocol_cursor = LANCONFIG_PROTOCOL_COUNT - 1; // Wrap around to the last protocol

			SetProtocol(lanConfig_protocol_cursor);
		}
		break;

	case K_MWHEELDOWN:
	case K_RIGHTARROW:
		if (StartingGame && lanConfig_cursor == M_LanConfig_NewGameProtocolCursor())
		{
			S_LocalSound("misc/menu1.wav");
			lanConfig_protocol_cursor++;
			if (lanConfig_protocol_cursor >= LANCONFIG_PROTOCOL_COUNT)
				lanConfig_protocol_cursor = 0; // Wrap around to the first protocol

			SetProtocol(lanConfig_protocol_cursor);
		}
		break;

	case K_TAB:
		if (lanConfig_cursor == LANCONFIG_CURSOR_PORT)
		{
			if (M_LanConfig_AcceptPortHint())
				S_LocalSound("misc/menu2.wav");
			goto finish;
		}
		if (JoiningGame && lanConfig_cursor == LANCONFIG_CURSOR_JOINGAME_JOIN)
		{
			if (M_Menu_TabCompleteFileList(&lanConfig_join_field, lanConfig_joinname,
				sizeof(lanConfig_joinname), serverlist,
				lanConfig_join_tabpartial, sizeof(lanConfig_join_tabpartial)))
				S_LocalSound("misc/menu2.wav");
			goto finish;
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1: // woods #mousemenu
		if (key == K_MOUSE1 && lanConfig_cursor == LANCONFIG_CURSOR_PORT)
		{
			M_TextField_MouseClick(&lanConfig_port_field, m_mousex, 170);
			goto finish;
		}
		if (key == K_MOUSE1 && M_LanConfig_ShowRoomField() && lanConfig_cursor == LANCONFIG_CURSOR_NEWGAME_ROOM)
		{
			M_TextField_MouseClick(&lanConfig_room_field, m_mousex, 170);
			goto finish;
		}
		if (key == K_MOUSE1 && JoiningGame && lanConfig_cursor == LANCONFIG_CURSOR_JOINGAME_JOIN)
		{
			lanConfig_join_tabpartial[0] = '\0';
			M_TextField_MouseClick(&lanConfig_join_field, m_mousex, 96);
			goto finish;
		}

		if (lanConfig_cursor == LANCONFIG_CURSOR_PORT ||
			(M_LanConfig_ShowRoomField() && lanConfig_cursor == LANCONFIG_CURSOR_NEWGAME_ROOM))
			break;

		m_entersound = true;

		M_ConfigureNetSubsystem ();

		if (StartingGame)
		{

			if (lanConfig_cursor == M_LanConfig_NewGameProtocolCursor())
			{
				S_LocalSound("misc/menu1.wav");
				lanConfig_protocol_cursor++;
				if (lanConfig_protocol_cursor >= LANCONFIG_PROTOCOL_COUNT)
					lanConfig_protocol_cursor = 0; // Wrap around to the first protocol

				SetProtocol(lanConfig_protocol_cursor);
			}
			if (lanConfig_cursor == M_LanConfig_NewGameOkCursor())
				M_Menu_GameOptions_f();
		}
		else
		{
			if (lanConfig_cursor == LANCONFIG_CURSOR_JOINGAME_SEARCH_LAN)
				M_Menu_Search_f(SLIST_LAN); // woods #localmpfix
			else if (lanConfig_cursor == LANCONFIG_CURSOR_JOINGAME_SEARCH_WEB)
				M_Menu_Search_f(SLIST_INTERNET);
			else if (lanConfig_cursor == LANCONFIG_CURSOR_JOINGAME_HISTORY) // woods #historymenu
				M_Menu_History_f ();
			else if (lanConfig_cursor == LANCONFIG_CURSOR_JOINGAME_BOOKMARKS) // woods #bookmarksmenu
				M_Menu_Bookmarks_f();
			else if (lanConfig_cursor == LANCONFIG_CURSOR_JOINGAME_JOIN)
			{
					m_return_state = m_state;
					m_return_onerror = true;
					key_dest = key_game;
					m_state = m_none;
					IN_UpdateGrabs();
					CL_MarkNextConnectFromMenu();
					Cbuf_AddText ( va ("connect \"%s\"\n", lanConfig_joinname) );
				}
			}

			break;
	}

finish:
	M_LanConfig_NormalizeRoomField();
	M_LanConfig_SyncRoomField();

	if (!(JoiningGame && lanConfig_cursor == LANCONFIG_CURSOR_JOINGAME_JOIN))
		lanConfig_join_tabpartial[0] = '\0';

	if (StartingGame && lanConfig_cursor >= NUM_LANCONFIG_CMDS)
	{
		if (key == K_UPARROW)
			lanConfig_cursor = M_LanConfig_NewGameProtocolCursor();
		else
			lanConfig_cursor = LANCONFIG_CURSOR_PORT;
	}

	l =  Q_atoi(lanConfig_portname);
	if (lanConfig_portname[0])
	{
		if (l <= 65535)
			lanConfig_port = l;
		else if (lanConfig_cursor != LANCONFIG_CURSOR_PORT)
			sprintf(lanConfig_portname, "%u", lanConfig_port);
	}
	M_TextField_ClampCursor(&lanConfig_port_field);
	M_TextField_ClampCursor(&lanConfig_room_field);
	M_TextField_ClampCursor(&lanConfig_join_field);
	M_LanConfig_UpdateHints();
}


void M_LanConfig_Char (int key)
{
	menu_textfield_t *active_field = M_LanConfig_GetFieldForCursor();
	if (active_field)
	{
		if (M_TextField_Char(active_field, key))
		{
			if (active_field == &lanConfig_port_field)
				M_LanConfig_UpdatePortHint();
			else if (active_field == &lanConfig_room_field)
			{
				M_LanConfig_NormalizeRoomField();
				M_LanConfig_SyncRoomField();
			}
			else if (active_field == &lanConfig_join_field)
			{
				lanConfig_join_tabpartial[0] = '\0';
				M_LanConfig_UpdateJoinHint();
			}
		}
	}
}

/*
==================
History Menu
==================
*/

#define MAX_VIS_HISTORY	17

typedef struct
{
	const char* name;
	qboolean	active;
} historyitem_t;

static struct
{
	menulist_t			list;
	enum m_state_e		prev;
	int					x, y, cols;
	int					democount;
	int					prev_cursor;
	menuticker_t		ticker;
	historyitem_t* items;
	qboolean			scrollbar_grab;
} historymenu;

static qboolean M_History_IsActive(const char* server)
{
	return cls.state == ca_connected && cls.signon == SIGNONS && !strcmp(lastmphost, server);
}

static void M_History_Add(const char* name)
{
	historyitem_t history;
		history.name = name;
		history.active = M_History_IsActive(name);

		if (history.active && historymenu.list.cursor == -1)
			historymenu.list.cursor = historymenu.list.numitems;

		// Ensure there's enough space for one more item
		VEC_PUSH(historymenu.items, history);

		historymenu.items[historymenu.list.numitems] = history;
		historymenu.list.numitems++;
}

static void M_History_Init(void)
{
	filelist_item_t* item;

	historymenu.list.viewsize = MAX_VIS_HISTORY;
	historymenu.list.cursor = -1;
	historymenu.list.scroll = 0;
	historymenu.list.numitems = 0;
	historymenu.democount = 0;
	historymenu.scrollbar_grab = false;
	VEC_CLEAR(historymenu.items);

	M_Ticker_Init(&historymenu.ticker);

	for (item = serverlist; item; item = item->next)
		M_History_Add(item->name);

	if (historymenu.list.cursor == -1)
		historymenu.list.cursor = 0;

	M_List_CenterCursor(&historymenu.list);
}

void M_Menu_History_f(void)
{
	key_dest = key_menu;
	historymenu.prev = m_state;
	m_state = m_history;
	m_entersound = true;
	M_History_Init();
}

void M_History_Draw(void)
{
	int x, y, i, cols;
	int firstvis, numvis;

	x = 16;
	y = 32;
	cols = 36;

	historymenu.x = x;
	historymenu.y = y;
	historymenu.cols = cols;

	if (!keydown[K_MOUSE1]) // woods #mousemenu
		historymenu.scrollbar_grab = false;

	if (historymenu.prev_cursor != historymenu.list.cursor)
	{
		historymenu.prev_cursor = historymenu.list.cursor;
		M_Ticker_Init(&historymenu.ticker);
	}
	else
		M_Ticker_Update(&historymenu.ticker);

	Draw_String(x, y - 28, "History");
	M_DrawQuakeBar(x - 8, y - 16, cols + 2);

	M_List_GetVisibleRange(&historymenu.list, &firstvis, &numvis);
	for (i = 0; i < numvis; i++)
	{
		int idx = i + firstvis;
		qboolean selected = (idx == historymenu.list.cursor);

		historyitem_t history;
		history.active = false;

		const char* lastmphostWithoutPort = COM_StripPort(lastmphost);
		const char* HistoryEntryWithoutPort = COM_StripPort(historymenu.items[idx].name);
		const char* ResolvedLastmphostWithoutPort = COM_StripPort(ResolveHostname(lastmphost));

		char portStr[10];
		q_snprintf(portStr, sizeof(portStr), "%d", DEFAULTnet_hostport);

		if (cls.state == ca_connected && lanConfig_port == DEFAULTnet_hostport) // highlight if connected to a server in the list
		{
			qboolean hasNonStandardPort = (strstr(lastmphost, ":") && !strstr(lastmphost, portStr)) ||
				(strstr(historymenu.items[idx].name, ":") && !strstr(historymenu.items[idx].name, portStr));
			
			if (hasNonStandardPort) // ports > 26000
			{
				if (!strcmp(historymenu.items[idx].name, lastmphost)) // exact match
					history.active = true;

				if (!strcmp(historymenu.items[idx].name, ResolveHostname(lastmphost))) // exact match but convert name to ip
					history.active = true;
			}
			else
			{
				if (!strcmp(HistoryEntryWithoutPort, lastmphostWithoutPort)) // treat 26000 and blank portthe same
					history.active = true;

				if (!strcmp(HistoryEntryWithoutPort, ResolvedLastmphostWithoutPort)) // convert name to ip
					history.active = true;
			}
		}
		else
			history.active = false;

		M_PrintScroll(x, y + i * 8, (cols - 2) * 8, historymenu.items[idx].name, selected ? historymenu.ticker.scroll_time : 0.0, !history.active);

		if (selected)
			M_DrawCharacter(x - 8, y + i * 8, 12 + ((int)(realtime * 4) & 1));

		if (lastmphostWithoutPort) free((void*)lastmphostWithoutPort);
		if (HistoryEntryWithoutPort) free((void*)HistoryEntryWithoutPort);
		if (ResolvedLastmphostWithoutPort) free((void*)ResolvedLastmphostWithoutPort);

	}

	if (M_List_GetOverflow(&historymenu.list) > 0)
	{
		M_List_DrawScrollbar(&historymenu.list, x + cols * 8 - 8, y);

		if (historymenu.list.scroll > 0)
			M_DrawEllipsisBar(x, y - 8, cols);
		if (historymenu.list.scroll + historymenu.list.viewsize < historymenu.list.numitems)
			M_DrawEllipsisBar(x, y + historymenu.list.viewsize * 8, cols);
	}
	M_PrintWhite(x, y + 2 + historymenu.list.viewsize * 8 + 10, "ctrl+backspace: delete");
}

qboolean M_History_Match(int index, char initial)
{
	return q_tolower(historymenu.items[index].name[0]) == initial;
}

void M_History_Key(int key)
{
	int x, y; // woods #mousemenu

	if (historymenu.scrollbar_grab)
	{
		switch (key)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			historymenu.scrollbar_grab = false;
			break;
		}
		return;
	}

	if (M_List_Key(&historymenu.list, key))
		return;

	if (M_List_CycleMatch(&historymenu.list, key, M_History_Match))
		return;

	if (M_Ticker_Key(&historymenu.ticker, key))
		return;

	switch (key)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		M_Menu_LanConfig_f();
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	enter:
		m_return_state = m_state;
		m_return_onerror = true;
		key_dest = key_game;
		m_state = m_none;
		IN_UpdateGrabs();
		CL_MarkNextConnectFromMenu();
		Cbuf_AddText(va("connect \"%s\"\n", historymenu.items[historymenu.list.cursor].name));
		break;

	case K_MOUSE1: // woods #mousemenu
		x = m_mousex - historymenu.x - (historymenu.cols - 1) * 8;
		y = m_mousey - historymenu.y;
		if (x < -8 || !M_List_UseScrollbar(&historymenu.list, y))
			goto enter;
		historymenu.scrollbar_grab = true;
		M_History_Mousemove(m_mousex, m_mousey);
		break;

	case K_BACKSPACE:
		if (historymenu.items != NULL && keydown[K_CTRL])
		{
			FileList_Subtract(historymenu.items[historymenu.list.cursor].name, &serverlist);
			Write_List(serverlist, SERVERLIST);
			M_Menu_History_f();
		}
		break;

	default:
		break;
	}
}

void M_History_Mousemove(int cx, int cy) // woods #mousemenu
{
	cy -= historymenu.y;

	if (historymenu.scrollbar_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			historymenu.scrollbar_grab = false;
			return;
		}
		M_List_UseScrollbar(&historymenu.list, cy);
		// Note: no return, we also update the cursor
	}

	M_List_Mousemove(&historymenu.list, cy);
}

/*
==================
Bookmarks Menu
==================
*/

#define MAX_VIS_BOOKMARKS	16

void FileList_Add(const char* name, const char* data, filelist_item_t** list);

static qboolean bookmarks_edit_new = false;
static qboolean bookmarks_edit_shortcut = false;

typedef struct
{
	const char* name;
	char		alias[BOOKMARK_ALIAS_LENGTH];
	qboolean	active;
	qboolean	pinned;
} bookmarksitem_t;

static struct
{
	menulist_t			list;
	enum m_state_e		prev;
	int					x, y, cols;
	int					democount;
	int					prev_cursor;
	menuticker_t		ticker;
	bookmarksitem_t* items;
	qboolean			scrollbar_grab;
} bookmarksmenu;

static qboolean M_Bookmarks_IsActive(const char* server)
{
	return cls.state == ca_connected && cls.signon == SIGNONS && !strcmp(lastmphost, server);
}

static void M_Bookmarks_Add(const char* name, const char* data)
{
	bookmarksitem_t bookmarks;
	bookmarks.name = name;
	BookmarkData_Parse(data, bookmarks.alias, sizeof(bookmarks.alias), &bookmarks.pinned);

	if (!bookmarks.alias[0])
		return;

	bookmarks.active = M_Bookmarks_IsActive(name);

	if (bookmarks.active && bookmarksmenu.list.cursor == -1)
		bookmarksmenu.list.cursor = bookmarksmenu.list.numitems;

	// Ensure there's enough space for one more item
	VEC_PUSH(bookmarksmenu.items, bookmarks);

	bookmarksmenu.items[bookmarksmenu.list.numitems] = bookmarks;
	bookmarksmenu.list.numitems++;
}

int BookmarkCompare(const void* a, const void* b)
{
	const bookmarksitem_t* itemA = (const bookmarksitem_t*)a;
	const bookmarksitem_t* itemB = (const bookmarksitem_t*)b;
	return strcmp(itemA->alias, itemB->alias);
}

static void M_Bookmarks_Init(void)
{
	filelist_item_t* item;

	bookmarksmenu.list.viewsize = MAX_VIS_BOOKMARKS;
	bookmarksmenu.list.cursor = -1;
	bookmarksmenu.list.scroll = 0;
	bookmarksmenu.list.numitems = 0;
	bookmarksmenu.democount = 0;
	bookmarksmenu.scrollbar_grab = false;
	VEC_CLEAR(bookmarksmenu.items);

	M_Ticker_Init(&bookmarksmenu.ticker);

	for (item = bookmarkslist; item; item = item->next)
		M_Bookmarks_Add(item->name, item->data);

	qsort(bookmarksmenu.items, bookmarksmenu.list.numitems, sizeof(bookmarksitem_t), BookmarkCompare);

	if (bookmarksmenu.list.cursor == -1)
		bookmarksmenu.list.cursor = 0;

	M_List_CenterCursor(&bookmarksmenu.list);
}

void M_Menu_Bookmarks_f(void)
{
	key_dest = key_menu;
	bookmarksmenu.prev = m_state;
	m_state = m_bookmarks;
	m_entersound = true;
	M_Bookmarks_Init();
}

void M_Bookmarks_Draw(void)
{
	int x, y, i, cols;
	int firstvis, numvis;

	x = 16;
	y = 32;
	cols = 36;

	bookmarksmenu.x = x;
	bookmarksmenu.y = y;
	bookmarksmenu.cols = cols;

	if (!keydown[K_MOUSE1]) // woods #mousemenu
		bookmarksmenu.scrollbar_grab = false;

	if (bookmarksmenu.prev_cursor != bookmarksmenu.list.cursor)
	{
		bookmarksmenu.prev_cursor = bookmarksmenu.list.cursor;
		M_Ticker_Init(&bookmarksmenu.ticker);
	}
	else
		M_Ticker_Update(&bookmarksmenu.ticker);

	Draw_String(x, y - 28, "Bookmarks");
	M_DrawQuakeBar(x - 8, y - 16, cols + 2);

	M_List_GetVisibleRange(&bookmarksmenu.list, &firstvis, &numvis);
	for (i = 0; i < numvis; i++)
	{
		int idx = i + firstvis;
		qboolean selected = (idx == bookmarksmenu.list.cursor);

		bookmarksitem_t bookmarks;
		bookmarks.active = false;

		const char* lastmphostWithoutPort = COM_StripPort(lastmphost);
		const char* HistoryEntryWithoutPort = COM_StripPort(bookmarksmenu.items[idx].name);
		const char* ResolvedLastmphostWithoutPort = COM_StripPort(ResolveHostname(lastmphost));

		char portStr[10];
		q_snprintf(portStr, sizeof(portStr), "%d", DEFAULTnet_hostport);

		if (cls.state == ca_connected && lanConfig_port == DEFAULTnet_hostport) // highlight if connected to a server in the list
		{
			qboolean hasNonStandardPort = (strstr(lastmphost, ":") && !strstr(lastmphost, portStr)) ||
				(strstr(bookmarksmenu.items[idx].name, ":") && !strstr(bookmarksmenu.items[idx].name, portStr));

			if (hasNonStandardPort) // ports > 26000
			{
				if (!strcmp(bookmarksmenu.items[idx].name, lastmphost)) // exact match
					bookmarks.active = true;

				if (!strcmp(bookmarksmenu.items[idx].name, ResolveHostname(lastmphost))) // exact match but convert name to ip
					bookmarks.active = true;
			}
			else
			{
				if (!strcmp(HistoryEntryWithoutPort, lastmphostWithoutPort)) // treat 26000 and blank portthe same
					bookmarks.active = true;

				if (!strcmp(HistoryEntryWithoutPort, ResolvedLastmphostWithoutPort)) // convert name to ip
					bookmarks.active = true;
			}
		}
		else
			bookmarks.active = false;

		M_PrintScroll(x, y + i * 8, (cols - 2) * 8, bookmarksmenu.items[idx].alias, selected ? bookmarksmenu.ticker.scroll_time : 0.0, !bookmarks.active);

		// Show pin indicator for pinned bookmarks
		if (bookmarksmenu.items[idx].pinned)
		{
			int alias_len = strlen(bookmarksmenu.items[idx].alias);
			if (alias_len < cols - 3)
				Draw_Character_Rotation(x + (alias_len + 1) * 8, y + i * 8, 141, 90); // Rotated arrow, same as multiplayer menu
		}

		if (selected)
			M_DrawCharacter(x - 8, y + i * 8, 12 + ((int)(realtime * 4) & 1));

		char serverStr[40];
		q_snprintf(serverStr, sizeof(serverStr), "%-34.34s", bookmarksmenu.items[idx].name);

		if (selected)
			M_PrintWhite(x, y + bookmarksmenu.list.viewsize * 8 + 12, serverStr);

		if (lastmphostWithoutPort) free((void*)lastmphostWithoutPort);
		if (HistoryEntryWithoutPort) free((void*)HistoryEntryWithoutPort);
		if (ResolvedLastmphostWithoutPort) free((void*)ResolvedLastmphostWithoutPort);

	}

	if (M_List_GetOverflow(&bookmarksmenu.list) > 0)
	{
		M_List_DrawScrollbar(&bookmarksmenu.list, x + cols * 8 - 8, y);

		if (bookmarksmenu.list.scroll > 0)
			M_DrawEllipsisBar(x, y - 8, cols);
		if (bookmarksmenu.list.scroll + bookmarksmenu.list.viewsize < bookmarksmenu.list.numitems)
			M_DrawEllipsisBar(x, y + bookmarksmenu.list.viewsize * 8, cols);
	}

	M_Print(x, y + 2 + bookmarksmenu.list.viewsize * 8 + 20, "ctrl+  a:add  e:edit  backspace:delete");
}

qboolean M_Bookmarks_Match(int index, char initial)
{
	return q_tolower(bookmarksmenu.items[index].alias[0]) == initial;
}

void M_Bookmarks_Key(int key)
{
	int x, y; // woods #mousemenu

	if (bookmarksmenu.scrollbar_grab)
	{
		switch (key)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			bookmarksmenu.scrollbar_grab = false;
			break;
		}
		return;
	}

	if (M_List_Key(&bookmarksmenu.list, key))
		return;

	if (M_List_CycleMatch(&bookmarksmenu.list, key, M_Bookmarks_Match) && !keydown[K_CTRL])
		return;

	if (M_Ticker_Key(&bookmarksmenu.ticker, key))
		return;

	switch (key)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		M_Menu_LanConfig_f();
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	enter:
		if (!bookmarksmenu.items || bookmarksmenu.list.numitems <= 0)
			break;	// nothing to connect to (empty bookmark list) -- avoid items[cursor] NULL deref
		m_return_state = m_state;
		m_return_onerror = true;
		key_dest = key_game;
		m_state = m_none;
		IN_UpdateGrabs();
		CL_MarkNextConnectFromMenu();
		Cbuf_AddText(va("connect \"%s\"\n", bookmarksmenu.items[bookmarksmenu.list.cursor].name));
		break;

	case K_MOUSE1: // woods #mousemenu
		x = m_mousex - bookmarksmenu.x - (bookmarksmenu.cols - 1) * 8;
		y = m_mousey - bookmarksmenu.y;
		if (x < -8 || !M_List_UseScrollbar(&bookmarksmenu.list, y))
			goto enter;
		bookmarksmenu.scrollbar_grab = true;
		M_Bookmarks_Mousemove(m_mousex, m_mousey);
		break;

	case 'a':
	case 'A':
		if (keydown[K_CTRL])
		{
			bookmarks_edit_new = true;
			M_Menu_Bookmarks_Edit_f();
		}
		break;

	case 'e':
	case 'E':
		if (keydown[K_CTRL])
		{
			if (bookmarksmenu.items != NULL)
				M_Menu_Bookmarks_Edit_f();
		}
		break;

	case K_BACKSPACE:
		if (bookmarksmenu.items != NULL && keydown[K_CTRL])
		{ 
			FileList_Subtract(bookmarksmenu.items[bookmarksmenu.list.cursor].name, &bookmarkslist);
			BookmarksList_Write();
			M_Menu_Bookmarks_f();
		}
		break;

	default:
		break;
	}
}

void M_Bookmarks_Mousemove(int cx, int cy) // woods #mousemenu
{
	cy -= bookmarksmenu.y;

	if (bookmarksmenu.scrollbar_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			bookmarksmenu.scrollbar_grab = false;
			return;
		}
		M_List_UseScrollbar(&bookmarksmenu.list, cy);
		// Note: no return, we also update the cursor
	}

	M_List_Mousemove(&bookmarksmenu.list, cy);
}

/* Bookmarks Edit menu */

static int		bookmarks_edit_cursor = 3;
static int		bookmarks_edit_cursor_table[] = { 54, 86, 114, 138 };

static char temp_alias[45];
static char temp_name[45];
static menu_textfield_t bookmarks_edit_name_field;
static menu_textfield_t bookmarks_edit_alias_field;
static qboolean	temp_pinned;
static qboolean	bookmarks_edit_original_pinned;
static char		bookmarks_edit_status[64];
static double	bookmarks_edit_status_until;

#define	NUM_BOOKMARKS_EDIT_CMDS	4

static void M_Bookmarks_Edit_ClearStatus(void)
{
	bookmarks_edit_status[0] = '\0';
	bookmarks_edit_status_until = 0;
}

static void M_Bookmarks_Edit_ShowStatus(const char* message)
{
	if (!message)
	{
		M_Bookmarks_Edit_ClearStatus();
		return;
	}

	q_strlcpy(bookmarks_edit_status, message, sizeof(bookmarks_edit_status));
	bookmarks_edit_status_until = realtime + 3.0;
}

static qboolean M_Bookmarks_Edit_SetPinned(qboolean pinned)
{
	if (temp_pinned == pinned)
	{
		if (!pinned)
			M_Bookmarks_Edit_ClearStatus();
		return true;
	}

	if (pinned)
	{
		int pinned_count = M_Bookmarks_CountPinned();
		if (!bookmarks_edit_new && bookmarks_edit_original_pinned && pinned_count > 0)
			pinned_count--;

		if (pinned_count >= MAX_PINNED_BOOKMARKS)
		{
			M_Bookmarks_Edit_ShowStatus(va("Max %d pins reached", MAX_PINNED_BOOKMARKS));
			S_LocalSound("misc/menu2.wav");
			return false;
		}
	}

	temp_pinned = pinned;
	M_Bookmarks_Edit_ClearStatus();
	S_LocalSound("misc/menu3.wav");
	return true;
}

static void M_Bookmarks_ListAdd(const char* name, const char* alias, qboolean pinned)
{
	char data[BOOKMARK_DATA_LENGTH];
	BookmarkData_Format(data, sizeof(data), alias, pinned);
	FileList_Add(name, data, &bookmarkslist);
}

static menu_textfield_t *M_Bookmarks_Edit_GetFieldForCursor(void)
{
	if (bookmarks_edit_cursor == 0)
		return &bookmarks_edit_name_field;
	if (bookmarks_edit_cursor == 1)
		return &bookmarks_edit_alias_field;
	return NULL;
}

static void M_Bookmarks_Edit_ClearTextSelections(void)
{
	M_TextField_ClearSelection(&bookmarks_edit_name_field);
	M_TextField_ClearSelection(&bookmarks_edit_alias_field);
}

void M_Menu_Bookmarks_Edit_f (void)
{
	key_dest = key_menu;
	m_state = m_bookmarks_edit;
	m_entersound = true;
	IN_UpdateGrabs();

	bookmarks_edit_cursor = 3;
	M_Bookmarks_Edit_ClearStatus();

	if (bookmarks_edit_new)
	{
		if (cls.state == ca_connected)
			q_snprintf(temp_name, sizeof(temp_name), "%s", lastmphost);
		else
			temp_name[0] = 0;
		temp_alias[0] = 0;
		temp_pinned = false;
		bookmarks_edit_original_pinned = false;
	}
	else if (bookmarksmenu.list.cursor >= 0 && bookmarksmenu.list.cursor < bookmarksmenu.list.numitems)
	{
		strncpy(temp_alias, bookmarksmenu.items[bookmarksmenu.list.cursor].alias, sizeof(temp_alias) - 1);
		temp_alias[sizeof(temp_alias) - 1] = 0;
		strncpy(temp_name, bookmarksmenu.items[bookmarksmenu.list.cursor].name, sizeof(temp_name) - 1);
		temp_name[sizeof(temp_name) - 1] = 0;
		temp_pinned = bookmarksmenu.items[bookmarksmenu.list.cursor].pinned;
		bookmarks_edit_original_pinned = temp_pinned;
	}
	else
	{
		M_Menu_Bookmarks_f();  // Fall back to the bookmarks menu if the index is invalid
		temp_pinned = false;
		bookmarks_edit_original_pinned = false;
		return;
	}

	M_TextField_Init(&bookmarks_edit_name_field, temp_name, 37, false);
	M_TextField_Init(&bookmarks_edit_alias_field, temp_alias, 37, false);
}

void M_Shortcut_Bookmarks_Edit_f(void)
{
	bookmarks_edit_new = true;
	bookmarks_edit_shortcut = true;
	M_Menu_Bookmarks_Edit_f();
}


void M_Bookmarks_Edit_Draw(void)
{
	M_TextField_CheckMouseRelease();

	M_Print(10, 40, "Hostname/IP");
	M_DrawTextBox(6, 46, 38, 1);
	M_TextField_DrawHighlight(&bookmarks_edit_name_field, 14, 54);
	M_PrintWhite(14, 54, temp_name);

	M_Print(10, 72, "Bookmark Name");
	M_DrawTextBox(6, 78, 38, 1);
	M_TextField_DrawHighlight(&bookmarks_edit_alias_field, 14, 86);
	M_PrintWhite(14, 86, temp_alias);

	M_Print(10, 114, "Pin");
	M_DrawCheckboxBox(36, 114, temp_pinned);

	M_DrawTextBox(6, 138 - 8, 14, 1);
	M_Print(15, 138, "Accept Changes");

	M_DrawCharacter(0, bookmarks_edit_cursor_table[bookmarks_edit_cursor], 12 + ((int)(realtime * 4) & 1));

	if (bookmarks_edit_cursor == 0)
		M_TextField_DrawCursor(&bookmarks_edit_name_field, 13, bookmarks_edit_cursor_table[bookmarks_edit_cursor]);

	if (bookmarks_edit_cursor == 1)
		M_TextField_DrawCursor(&bookmarks_edit_alias_field, 13, bookmarks_edit_cursor_table[bookmarks_edit_cursor]);

	if (bookmarks_edit_status[0] && realtime < bookmarks_edit_status_until)
	{
		int box_width = (int)strlen(bookmarks_edit_status) + 2;
		if (box_width & 1)
			box_width++;
		M_DrawTextBox(6, 160, box_width, 1);
		M_PrintWhite(14, 168, bookmarks_edit_status);
	}
}

void M_Bookmarks_Edit_Key(int k)
{
	menu_textfield_t *active_field = M_Bookmarks_Edit_GetFieldForCursor();
	if (active_field && M_TextField_Key(active_field, k))
		return;

	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2: // woods #mousemenu
		if (bookmarks_edit_shortcut)
		{
			key_dest = key_game;
			m_state = m_none;
			bookmarks_edit_shortcut = false;
			bookmarks_edit_new = false;
		}
		else
		{
			M_Menu_Bookmarks_f();
			bookmarks_edit_new = false;
		}
		break;

	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		M_Bookmarks_Edit_ClearTextSelections();
		bookmarks_edit_cursor--;
		if (bookmarks_edit_cursor < 0)
			bookmarks_edit_cursor = NUM_BOOKMARKS_EDIT_CMDS - 1;
		break;

	case K_DOWNARROW:
	case K_TAB:
		S_LocalSound("misc/menu1.wav");
		M_Bookmarks_Edit_ClearTextSelections();
		bookmarks_edit_cursor++;
		if (bookmarks_edit_cursor >= NUM_BOOKMARKS_EDIT_CMDS)
			bookmarks_edit_cursor = 0;
		break;

	case K_MWHEELDOWN:
	case K_LEFTARROW:
		if (bookmarks_edit_cursor == 2)
		{
			M_Bookmarks_Edit_SetPinned(!temp_pinned);
			return;
		}
		if (bookmarks_edit_cursor < 2)
			return;
		S_LocalSound("misc/menu3.wav");
		break;
	case K_MWHEELUP:
	case K_RIGHTARROW:
		if (bookmarks_edit_cursor == 2)
		{
			M_Bookmarks_Edit_SetPinned(!temp_pinned);
			return;
		}
		if (bookmarks_edit_cursor < 2)
			return;
		S_LocalSound("misc/menu3.wav");
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1: // woods #mousemenu
		if (k == K_MOUSE1 && bookmarks_edit_cursor == 0)
		{
			if (M_TextField_MouseInRow(m_mousey, bookmarks_edit_cursor_table[0]))
				M_TextField_MouseClick(&bookmarks_edit_name_field, m_mousex, 14);
			return;
		}
		if (k == K_MOUSE1 && bookmarks_edit_cursor == 1)
		{
			if (M_TextField_MouseInRow(m_mousey, bookmarks_edit_cursor_table[1]))
				M_TextField_MouseClick(&bookmarks_edit_alias_field, m_mousex, 14);
			return;
		}

		if (bookmarks_edit_cursor == 0 || bookmarks_edit_cursor == 1)
			return;

		if (bookmarks_edit_cursor == 2)
		{
			M_Bookmarks_Edit_SetPinned(!temp_pinned);
			return;
		}

		// (Accept Changes)
		if (!bookmarks_edit_new) // edit + save
		{
			if ((Q_strcmp(bookmarksmenu.items[bookmarksmenu.list.cursor].alias, temp_alias) != 0 ||
			     Q_strcmp(bookmarksmenu.items[bookmarksmenu.list.cursor].name, temp_name) != 0 ||
			     bookmarksmenu.items[bookmarksmenu.list.cursor].pinned != temp_pinned)
				&& (strcmp(temp_alias, "") && (Valid_IP(temp_name) || Valid_Domain(temp_name))))
			{
				FileList_Subtract(bookmarksmenu.items[bookmarksmenu.list.cursor].name, &bookmarkslist);
				M_Bookmarks_ListAdd(temp_name, temp_alias, temp_pinned);
				BookmarksList_Write();
				bookmarks_edit_new = false;
			}
		}
		
		if (bookmarks_edit_new && (strcmp(temp_alias, "") && (Valid_IP(temp_name) || Valid_Domain(temp_name)))) // new + save
		{
			M_Bookmarks_ListAdd(temp_name, temp_alias, temp_pinned);
			BookmarksList_Write();
			bookmarks_edit_new = false;
		}

			m_entersound = true;

			M_Menu_Bookmarks_f();
			break;
	}
}

void M_Bookmarks_Edit_Char(int k)
{
	menu_textfield_t *active_field = M_Bookmarks_Edit_GetFieldForCursor();
	if (active_field)
		M_TextField_Char(active_field, k);
}

qboolean M_Bookmarks_Edit_TextEntry(void)
{
	return (bookmarks_edit_cursor == 0 || bookmarks_edit_cursor == 1);
}

void M_Bookmarks_Edit_Mousemove(int cx, int cy) // woods #mousemenu
{
	int old_cursor;

	if (textfield_mouse_dragging &&
		(textfield_drag_field == &bookmarks_edit_name_field || textfield_drag_field == &bookmarks_edit_alias_field))
	{
		M_TextField_MouseDrag(cx);
		return;
	}

	old_cursor = bookmarks_edit_cursor;
	M_UpdateCursorWithTable(cy, bookmarks_edit_cursor_table, NUM_BOOKMARKS_EDIT_CMDS, &bookmarks_edit_cursor);
	if (bookmarks_edit_cursor != old_cursor)
		M_Bookmarks_Edit_ClearTextSelections();
}

qboolean M_LanConfig_TextEntry (void)
{
	return (lanConfig_cursor == LANCONFIG_CURSOR_PORT ||
		(M_LanConfig_ShowRoomField() && lanConfig_cursor == LANCONFIG_CURSOR_NEWGAME_ROOM) ||
		lanConfig_cursor == LANCONFIG_CURSOR_JOINGAME_JOIN); // woods #historymenu #bookmarksmenu
}

void M_LanConfig_Mousemove(int cx, int cy)
{
	int numCommands;
	int old_cursor;

	if (textfield_mouse_dragging &&
		(textfield_drag_field == &lanConfig_port_field ||
		textfield_drag_field == &lanConfig_room_field ||
		textfield_drag_field == &lanConfig_join_field))
	{
		M_TextField_MouseDrag(cx);
		return;
	}

	// First check if mouse is over IP addresses
	for (int i = 0; i < 2; i++)
	{
		if (cx >= ip_clickables[i].x &&
			cx <= ip_clickables[i].x + ip_clickables[i].width &&
			cy >= ip_clickables[i].y &&
			cy <= ip_clickables[i].y + 8)
		{
			// Mouse is over an IP address - could add visual feedback here
			return; // Don't update menu cursor when over IPs
		}
	}

	// If not over IPs, handle regular menu cursor movement
	numCommands = NUM_LANCONFIG_CMDS;
	old_cursor = lanConfig_cursor;
	M_UpdateCursorWithTable(cy, lanConfig_cursor_ptr, numCommands, &lanConfig_cursor);
	if (lanConfig_cursor != old_cursor)
	{
		if (!(JoiningGame && lanConfig_cursor == LANCONFIG_CURSOR_JOINGAME_JOIN))
			lanConfig_join_tabpartial[0] = '\0';
		M_LanConfig_ClearTextSelections();
	}
}

/*
==================
New Game Options Menu
==================
*/

typedef struct
{
	const char	*name;
	const char	*description;
} level_t;

level_t		levels[] =
{
	{"start", "Entrance"},	// 0

	{"e1m1", "Slipgate Complex"},				// 1
	{"e1m2", "Castle of the Damned"},
	{"e1m3", "The Necropolis"},
	{"e1m4", "The Grisly Grotto"},
	{"e1m5", "Gloom Keep"},
	{"e1m6", "The Door To Chthon"},
	{"e1m7", "The House of Chthon"},
	{"e1m8", "Ziggurat Vertigo"},

	{"e2m1", "The Installation"},				// 9
	{"e2m2", "Ogre Citadel"},
	{"e2m3", "Crypt of Decay"},
	{"e2m4", "The Ebon Fortress"},
	{"e2m5", "The Wizard's Manse"},
	{"e2m6", "The Dismal Oubliette"},
	{"e2m7", "Underearth"},

	{"e3m1", "Termination Central"},			// 16
	{"e3m2", "The Vaults of Zin"},
	{"e3m3", "The Tomb of Terror"},
	{"e3m4", "Satan's Dark Delight"},
	{"e3m5", "Wind Tunnels"},
	{"e3m6", "Chambers of Torment"},
	{"e3m7", "The Haunted Halls"},

	{"e4m1", "The Sewage System"},				// 23
	{"e4m2", "The Tower of Despair"},
	{"e4m3", "The Elder God Shrine"},
	{"e4m4", "The Palace of Hate"},
	{"e4m5", "Hell's Atrium"},
	{"e4m6", "The Pain Maze"},
	{"e4m7", "Azure Agony"},
	{"e4m8", "The Nameless City"},

	{"end", "Shub-Niggurath's Pit"},			// 31

	{"dm1", "Place of Two Deaths"},				// 32
	{"dm2", "Claustrophobopolis"},
	{"dm3", "The Abandoned Base"},
	{"dm4", "The Bad Place"},
	{"dm5", "The Cistern"},
	{"dm6", "The Dark Zone"}
};

//MED 01/06/97 added hipnotic levels
level_t     hipnoticlevels[] =
{
	{"start", "Command HQ"},	// 0

	{"hip1m1", "The Pumping Station"},			// 1
	{"hip1m2", "Storage Facility"},
	{"hip1m3", "The Lost Mine"},
	{"hip1m4", "Research Facility"},
	{"hip1m5", "Military Complex"},

	{"hip2m1", "Ancient Realms"},				// 6
	{"hip2m2", "The Black Cathedral"},
	{"hip2m3", "The Catacombs"},
	{"hip2m4", "The Crypt"},
	{"hip2m5", "Mortum's Keep"},
	{"hip2m6", "The Gremlin's Domain"},

	{"hip3m1", "Tur Torment"},				// 12
	{"hip3m2", "Pandemonium"},
	{"hip3m3", "Limbo"},
	{"hip3m4", "The Gauntlet"},

	{"hipend", "Armagon's Lair"},				// 16

	{"hipdm1", "The Edge of Oblivion"}			// 17
};

//PGM 01/07/97 added rogue levels
//PGM 03/02/97 added dmatch level
level_t		roguelevels[] =
{
	{"start",	"Split Decision"},
	{"r1m1",	"Deviant's Domain"},
	{"r1m2",	"Dread Portal"},
	{"r1m3",	"Judgement Call"},
	{"r1m4",	"Cave of Death"},
	{"r1m5",	"Towers of Wrath"},
	{"r1m6",	"Temple of Pain"},
	{"r1m7",	"Tomb of the Overlord"},
	{"r2m1",	"Tempus Fugit"},
	{"r2m2",	"Elemental Fury I"},
	{"r2m3",	"Elemental Fury II"},
	{"r2m4",	"Curse of Osiris"},
	{"r2m5",	"Wizard's Keep"},
	{"r2m6",	"Blood Sacrifice"},
	{"r2m7",	"Last Bastion"},
	{"r2m8",	"Source of Evil"},
	{"ctf1",    "Division of Change"}
};

typedef struct
{
	const char	*description;
	int		firstLevel;
	int		levels;
} episode_t;

episode_t	episodes[] =
{
	{"Welcome to Quake", 0, 1},
	{"Doomed Dimension", 1, 8},
	{"Realm of Black Magic", 9, 7},
	{"Netherworld", 16, 7},
	{"The Elder World", 23, 8},
	{"Final Level", 31, 1},
	{"Deathmatch Arena", 32, 6}
};

//MED 01/06/97  added hipnotic episodes
episode_t   hipnoticepisodes[] =
{
	{"Scourge of Armagon", 0, 1},
	{"Fortress of the Dead", 1, 5},
	{"Dominion of Darkness", 6, 6},
	{"The Rift", 12, 4},
	{"Final Level", 16, 1},
	{"Deathmatch Arena", 17, 1}
};

//PGM 01/07/97 added rogue episodes
//PGM 03/02/97 added dmatch episode
episode_t	rogueepisodes[] =
{
	{"Introduction", 0, 1},
	{"Hell's Fortress", 1, 7},
	{"Corridors of Time", 8, 8},
	{"Deathmatch Arena", 16, 1}
};

extern cvar_t sv_public;

int	startepisode;
int	startlevel;
int maxplayers;
enum
{
	GAMEOPTIONS_BEGIN,
	GAMEOPTIONS_MAXPLAYERS,
	GAMEOPTIONS_PUBLIC,
	GAMEOPTIONS_GAMETYPE,
	GAMEOPTIONS_TEAMPLAY,
	GAMEOPTIONS_SKILL,
	GAMEOPTIONS_FRAGLIMIT,
	GAMEOPTIONS_TIMELIMIT,
	GAMEOPTIONS_MOD,
	GAMEOPTIONS_EPISODE,
	GAMEOPTIONS_LEVEL,
	GAMEOPTIONS_SELECT_LEVEL,
	GAMEOPTIONS_ENTER_LEVEL,
	NUM_GAMEOPTIONS
};
static int gameoptions_cursor_table[] = {40, 56, 64, 72, 80, 88, 96, 104, 112, 128, 136, 160, 176};
#define GAMEOPTIONS_LEVEL_FIELD_BOX_X	152
#define GAMEOPTIONS_LEVEL_FIELD_TEXT_X	160
#define GAMEOPTIONS_LEVEL_FIELD_BOX_WIDTH	18
int		gameoptions_cursor;
static int gameoptions_mod_index;
typedef struct
{
	char name[MAX_QPATH];
} gameoptions_mod_t;
static gameoptions_mod_t *gameoptions_mods;

static char goptions_levelname[MAX_QPATH];
static char goptions_levelhint[MAX_QPATH];
static qboolean goptions_levelvalid;
static menu_textfield_t goptions_level_field;

static void M_GameOptions_UpdateLevelHint(void);
static const char *M_GameOptions_SelectedMod(void);
static qboolean M_GameOptions_UsingDefaultMod(void);
static void M_GameOptions_ClearTypedLevel(void);
static void M_GameOptions_ClampCursor(void);
static void M_GameOptions_RebuildMods(void);
static void M_GameOptions_LoadHistory(void);
static void M_GameOptions_SaveHistory(const char *selected_map);

#define SHISTORY_FILENAME          "shistory.json"
#define SHISTORY_MAX_FILE_SIZE     (64 * 1024)

static qboolean gameoptions_history_loaded = false;

static void M_GameOptions_HistoryPath(char *path, size_t size)
{
	q_snprintf(path, size, "%s/id1/backups/%s", com_basedir, SHISTORY_FILENAME);
}

static int M_GameOptions_EpisodeCount(void)
{
	if (hipnotic)
		return (int)(sizeof(hipnoticepisodes) / sizeof(hipnoticepisodes[0]));
	if (rogue)
		return (int)(sizeof(rogueepisodes) / sizeof(rogueepisodes[0]));
	if (registered.value)
		return (int)(sizeof(episodes) / sizeof(episodes[0]));
	return 2;
}

static int M_GameOptions_LevelCount(int episode_idx)
{
	int ep_count = M_GameOptions_EpisodeCount();

	if (episode_idx < 0 || episode_idx >= ep_count)
		episode_idx = 0;

	if (hipnotic)
		return hipnoticepisodes[episode_idx].levels;
	if (rogue)
		return rogueepisodes[episode_idx].levels;
	return episodes[episode_idx].levels;
}

static void M_GameOptions_ClampEpisodeLevel(void)
{
	int ep_count = M_GameOptions_EpisodeCount();
	int lvl_count;

	if (startepisode < 0 || startepisode >= ep_count)
		startepisode = 0;

	lvl_count = M_GameOptions_LevelCount(startepisode);
	if (startlevel < 0 || startlevel >= lvl_count)
		startlevel = 0;
}

static qboolean M_GameOptions_ReadHistoryInt(const jsonentry_t *entry, const char *name, int minval, int maxval, int *out)
{
	const double *d;

	if (maxval < minval)
		return false;

	d = JSON_FindNumber(entry, name);
	if (!d || !isfinite(*d))
		return false;

	if (*d <= minval)
		*out = minval;
	else if (*d >= maxval)
		*out = maxval;
	else
		*out = (int)*d;

	return true;
}

static void M_GameOptions_LoadHistory(void)
{
	char path[MAX_OSPATH];
	FILE *file;
	long file_size;
	char *text;
	json_t *json;
	const char *s;
	int n;

	M_GameOptions_HistoryPath(path, sizeof(path));
	file = fopen(path, "rb");
	if (!file)
		return;

	if (fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		return;
	}
	file_size = ftell(file);
	rewind(file);
	if (file_size <= 0 || file_size > SHISTORY_MAX_FILE_SIZE)
	{
		fclose(file);
		return;
	}

	text = (char *)malloc((size_t)file_size + 1);
	if (!text)
	{
		fclose(file);
		return;
	}
	if (fread(text, 1, (size_t)file_size, file) != (size_t)file_size)
	{
		free(text);
		fclose(file);
		return;
	}
	text[file_size] = '\0';
	fclose(file);

	json = JSON_Parse(text);
	free(text);
	if (!json || !json->root || json->root->type != JSON_OBJECT)
	{
		if (json)
			JSON_Free(json);
		return;
	}

	n = svs.maxclientslimit;
	if (n < 2)
		n = 2;
	if (M_GameOptions_ReadHistoryInt(json->root, "maxplayers", 2, n, &n))
		maxplayers = n;

	if (M_GameOptions_ReadHistoryInt(json->root, "sv_public", 0, 1, &n))
		Cvar_SetValue("sv_public", (float)n);
	if (M_GameOptions_ReadHistoryInt(json->root, "coop", 0, 1, &n))
		Cvar_SetValue("coop", (float)n);
	if (M_GameOptions_ReadHistoryInt(json->root, "teamplay", 0, rogue ? 6 : 2, &n))
		Cvar_SetValue("teamplay", (float)n);
	if (M_GameOptions_ReadHistoryInt(json->root, "skill", 0, 3, &n))
		Cvar_SetValue("skill", (float)n);
	if (M_GameOptions_ReadHistoryInt(json->root, "fraglimit", 0, 100, &n))
		Cvar_SetValue("fraglimit", (float)n);
	if (M_GameOptions_ReadHistoryInt(json->root, "timelimit", 0, 60, &n))
		Cvar_SetValue("timelimit", (float)n);

	if (M_GameOptions_ReadHistoryInt(json->root, "episode", 0, M_GameOptions_EpisodeCount() - 1, &n))
	{
		startepisode = n;
		if (M_GameOptions_ReadHistoryInt(json->root, "level", 0, M_GameOptions_LevelCount(startepisode) - 1, &n))
			startlevel = n;
	}
	else if (M_GameOptions_ReadHistoryInt(json->root, "level", 0, M_GameOptions_LevelCount(startepisode) - 1, &n))
		startlevel = n;
	M_GameOptions_ClampEpisodeLevel();

	gameoptions_mod_index = 0;
	s = JSON_FindString(json->root, "mod");
	if (s && s[0])
	{
		size_t i, count = VEC_SIZE(gameoptions_mods);
		for (i = 0; i < count; i++)
		{
			if (!q_strcasecmp(gameoptions_mods[i].name, s))
			{
				gameoptions_mod_index = (int)(i + 1);
				break;
			}
		}
	}

	m_skill_mapname[0] = '\0';
	M_GameOptions_ClearTypedLevel();
	s = JSON_FindString(json->root, "map");
	if (s && s[0])
	{
		q_strlcpy(goptions_levelname, s, sizeof(goptions_levelname));
		goptions_level_field.cursor = (int)strlen(goptions_levelname);
		goptions_level_field.sel_start = -1;
		M_TextField_ClampCursor(&goptions_level_field);
	}

	JSON_Free(json);
}

static void M_GameOptions_SaveHistory(const char *selected_map)
{
	char path[MAX_OSPATH];
	char dir[MAX_OSPATH];
	FILE *file;
	const char *selected_mod = M_GameOptions_SelectedMod();
	char *escaped_mod;
	char *escaped_map;
	qboolean write_failed;

	escaped_mod = JSON_EscapeString(selected_mod ? selected_mod : "");
	escaped_map = JSON_EscapeString(selected_map ? selected_map : "");
	if (!escaped_mod || !escaped_map)
	{
		Con_DPrintf("Failed to encode %s for writing\n", SHISTORY_FILENAME);
		free(escaped_mod);
		free(escaped_map);
		return;
	}

	q_snprintf(dir, sizeof(dir), "%s/id1/backups", com_basedir);
	Sys_mkdir(dir);
	M_GameOptions_HistoryPath(path, sizeof(path));

	file = fopen(path, "w");
	if (!file)
	{
		Con_DPrintf("Failed to open %s for writing\n", SHISTORY_FILENAME);
		free(escaped_mod);
		free(escaped_map);
		return;
	}

	write_failed =
		fprintf(file, "{\n") < 0 ||
		fprintf(file, "  \"maxplayers\": %d,\n", maxplayers) < 0 ||
		fprintf(file, "  \"sv_public\": %d,\n", sv_public.value ? 1 : 0) < 0 ||
		fprintf(file, "  \"coop\": %d,\n", (int)coop.value) < 0 ||
		fprintf(file, "  \"teamplay\": %d,\n", (int)teamplay.value) < 0 ||
		fprintf(file, "  \"skill\": %d,\n", (int)skill.value) < 0 ||
		fprintf(file, "  \"fraglimit\": %d,\n", (int)fraglimit.value) < 0 ||
		fprintf(file, "  \"timelimit\": %d,\n", (int)timelimit.value) < 0 ||
		fprintf(file, "  \"mod\": \"%s\",\n", escaped_mod) < 0 ||
		fprintf(file, "  \"episode\": %d,\n", startepisode) < 0 ||
		fprintf(file, "  \"level\": %d,\n", startlevel) < 0 ||
		fprintf(file, "  \"map\": \"%s\"\n", escaped_map) < 0 ||
		fprintf(file, "}\n") < 0;
	if (fclose(file) != 0)
		write_failed = true;

	if (write_failed)
		Con_DPrintf("Failed to write %s\n", SHISTORY_FILENAME);

	free(escaped_mod);
	free(escaped_map);
}

void M_Menu_GameOptions_f (void)
{
	key_dest = key_menu;
	m_state = m_gameoptions;
	IN_UpdateGrabs();
	m_entersound = true;
	M_TextField_Init(&goptions_level_field, goptions_levelname, MAX_QPATH - 1, false);
	M_GameOptions_RebuildMods();
	if (!gameoptions_history_loaded && !sv.active)
	{
		M_GameOptions_LoadHistory();
		gameoptions_history_loaded = true;
	}
	if (maxplayers == 0)
		maxplayers = svs.maxclients;
	if (maxplayers < 2)
		maxplayers = 16;
	M_GameOptions_UpdateLevelHint();
	M_GameOptions_ClampCursor();
}


static menu_textfield_t *M_GameOptions_GetFieldForCursor(void)
{
	if (gameoptions_cursor == GAMEOPTIONS_ENTER_LEVEL)
		return &goptions_level_field;
	return NULL;
}

static void M_GameOptions_ClearTextSelections(void)
{
	M_TextField_ClearSelection(&goptions_level_field);
}

static void M_GameOptions_ClearTypedLevel(void)
{
	goptions_levelname[0] = '\0';
	goptions_levelhint[0] = '\0';
	goptions_levelvalid = false;
	goptions_level_field.cursor = 0;
	goptions_level_field.sel_start = -1;
}

/*
============
M_GameOptions_UpdateLevelHint

Scans extralevels for a map matching the typed input and updates hint text.
============
*/
static void M_GameOptions_UpdateLevelHint(void)
{
	filelist_item_t *level;
	int len = (int)strlen(goptions_levelname);

	goptions_levelhint[0] = '\0';
	goptions_levelvalid = false;

	if (len <= 0)
		return;

	for (level = extralevels; level; level = level->next)
	{
		if (!q_strncasecmp(level->name, goptions_levelname, len))
		{
			if (!goptions_levelhint[0])
				q_strlcpy(goptions_levelhint, level->name + len, sizeof(goptions_levelhint));

			if (!q_strcasecmp(level->name, goptions_levelname))
			{
				/* Exact map name typed: valid and no hint needed. */
				goptions_levelvalid = true;
				goptions_levelhint[0] = '\0';
				return;
			}
		}
	}
}

/*
============
M_GameOptions_IsValidLevel

Returns true if goptions_levelname exactly matches a map in extralevels.
============
*/
static qboolean M_GameOptions_IsValidLevel(void)
{
	return goptions_levelvalid;
}

static void M_GameOptions_AcceptLevelHint(void)
{
	if (!goptions_levelhint[0])
		return;

	/* Only complete when the caret is at the end of the field text. */
	if (goptions_level_field.cursor != (int)strlen(goptions_levelname))
		return;

	q_strlcat(goptions_levelname, goptions_levelhint, sizeof(goptions_levelname));
	goptions_levelhint[0] = '\0';
	goptions_level_field.cursor = (int)strlen(goptions_levelname);
	goptions_level_field.sel_start = -1;
	M_TextField_ClampCursor(&goptions_level_field);
	M_GameOptions_UpdateLevelHint();
}

/*
============
M_GameOptions_CheckLeave

Called when leaving Enter Level; validates and resolves priority with Select Level.
============
*/
static void M_GameOptions_CheckLeave(void)
{
	if (!goptions_levelname[0])
	{
		goptions_levelhint[0] = '\0';
		M_TextField_ClearSelection(&goptions_level_field);
		return;
	}

	M_GameOptions_AcceptLevelHint();

	if (M_GameOptions_IsValidLevel())
	{
		/* Valid typed map takes priority over Select Level. */
		m_skill_mapname[0] = '\0';
	}
	else
	{
		/* Invalid input is cleared when leaving the field. */
		M_GameOptions_ClearTypedLevel();
	}

	M_TextField_ClearSelection(&goptions_level_field);
	M_GameOptions_UpdateLevelHint();
}

static qboolean M_GameOptions_ModIsSelectable(const char *name)
{
	char check_path[MAX_OSPATH];
	char game_dir[MAX_OSPATH];
	char game_path[MAX_OSPATH];
	FILE *check_file;
	int pak_num;

	if (!name || !*name || !q_strcasecmp(name, GAMENAME))
		return false;

	if (!COM_ResolveGameDir(name, game_dir, sizeof(game_dir)))
		return false;

	q_snprintf(game_path, sizeof(game_path), "%s/%s", com_basedir, game_dir);
	q_snprintf(check_path, sizeof(check_path), "%s/progs.dat", game_path);
	check_file = fopen(check_path, "rb");
	if (check_file)
	{
		fclose(check_file);
		return true;
	}

	for (pak_num = 0; pak_num < 10; pak_num++)
	{
		q_snprintf(check_path, sizeof(check_path), "%s/pak%d.pak", game_path, pak_num);
		check_file = fopen(check_path, "rb");
		if (check_file)
		{
			fclose(check_file);
			return true;
		}
	}
	for (pak_num = 0; pak_num < 10; pak_num++)
	{
		q_snprintf(check_path, sizeof(check_path), "%s/paks/pak%d.pak", game_path, pak_num);
		check_file = fopen(check_path, "rb");
		if (check_file)
		{
			fclose(check_file);
			return true;
		}
	}

	return false;
}

static void M_GameOptions_RebuildMods(void)
{
	filelist_item_t *mod;
	gameoptions_mod_t item;

	VEC_CLEAR(gameoptions_mods);
	for (mod = modlist; mod; mod = mod->next)
	{
		if (M_GameOptions_ModIsSelectable(mod->name))
		{
			q_strlcpy(item.name, mod->name, sizeof(item.name));
			VEC_PUSH(gameoptions_mods, item);
		}
	}

	if (gameoptions_mod_index > (int)VEC_SIZE(gameoptions_mods))
		gameoptions_mod_index = 0;
}

static int M_GameOptions_ModCount(void)
{
	return (int)VEC_SIZE(gameoptions_mods) + 1;
}

static const char *M_GameOptions_ModNameForIndex(int index)
{
	if (index <= 0)
		return NULL;

	if (index > (int)VEC_SIZE(gameoptions_mods))
		return NULL;

	return gameoptions_mods[index - 1].name;
}

static const char *M_GameOptions_SelectedMod(void)
{
	const char *mod = M_GameOptions_ModNameForIndex(gameoptions_mod_index);

	if (!mod)
		gameoptions_mod_index = 0;

	return mod;
}

static qboolean M_GameOptions_UsingDefaultMod(void)
{
	return M_GameOptions_SelectedMod() == NULL;
}

static int M_GameOptions_NumItems(void)
{
	return NUM_GAMEOPTIONS;
}

static void M_GameOptions_ClampCursor(void)
{
	int count = M_GameOptions_NumItems();

	if (gameoptions_cursor >= count)
		gameoptions_cursor = count - 1;
	if (gameoptions_cursor < 0)
		gameoptions_cursor = 0;
}

static void M_GameOptions_PrintMaybeDim(int x, int y, const char *str, qboolean dim, qboolean white)
{
	if (dim)
		M_PrintRGBA(x, y, str, CL_PLColours_Parse("0xffffff"), 0.5f, !white);
	else if (white)
		M_PrintWhite(x, y, str);
	else
		M_Print(x, y, str);
}

qboolean HasBots(void) // woods -- check if deathmatch needs difficulty #botdetect
{
	if (!progs_check_done)
	{
		FILE* file;
		byte* buffer;
		long size;
		unsigned short crc;

		const unsigned short valid_crcs[] = { 32913, 10067, 51593 }; // shareware/steam/regisrted pak0, rogue, hipnotic
		const size_t num_valid_crcs = sizeof(valid_crcs) / sizeof(valid_crcs[0]);

		const char* custom_marker = "crx"; // custom progs without bots
		size_t custom_marker_len = strlen(custom_marker);

		if (COM_FOpenFile("progs.dat", &file, NULL) < 0 || !file)
		{
			progs_check_done = true;
			return false;
		}

		fseek(file, 0, SEEK_END);
		size = ftell(file);
		fseek(file, 0, SEEK_SET);

		buffer = (byte*)malloc(size);
		if (!buffer)
		{
			fclose(file);
			progs_check_done = true;
			return false;
		}

		if (fread(buffer, 1, size, file) != (size_t)size)
		{
			free(buffer);
			fclose(file);
			progs_check_done = true;
			return false;
		}
		fclose(file);

		crc = CRC_Block(buffer, size);

		qboolean is_valid_crc = false;
		for (size_t i = 0; i < num_valid_crcs; i++)
		{
			if (crc == valid_crcs[i]) {
				is_valid_crc = true;
				break;
			}
		}

		if (is_valid_crc) 
			has_custom_progs = false;
		else if (q_memmem(buffer, size, custom_marker, custom_marker_len) != NULL)
			has_custom_progs = false;
		else 
			has_custom_progs = true;

		free(buffer);
		progs_check_done = true;
	}
	return has_custom_progs;
}

void M_GameOptions_Draw (void)
{
	qpic_t	*p;
	int y = 40;
	const char *selected_mod;
	qboolean mod_selected;
	qboolean dim_builtin_level;

	M_TextField_CheckMouseRelease();
	M_GameOptions_ClampCursor();

	M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp") );
	p = Draw_CachePic ("gfx/p_multi.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);

	M_DrawTextBox (152, y-8, 10, 1);
	M_Print (160, y, "begin game");
	y+=16;

	M_Print (0, y, "      Max players");
	M_Print (160, y, va("%i", maxplayers) );
	y+=8;

	M_Print (0, y, "           Public");
	if (sv_public.value)
		M_Print (160, y, "Yes");
	else
		M_Print (160, y, "No");
	y+=8;

	M_Print (0, y, "        Game Type");
	if (coop.value)
		M_Print (160, y, "Cooperative");
	else
		M_Print (160, y, "Deathmatch");
	y+=8;

	M_Print (0, y, "         Teamplay");
	if (rogue)
	{
		const char *msg;

		switch((int)teamplay.value)
		{
			case 1: msg = "No Friendly Fire"; break;
			case 2: msg = "Friendly Fire"; break;
			case 3: msg = "Tag"; break;
			case 4: msg = "Capture the Flag"; break;
			case 5: msg = "One Flag CTF"; break;
			case 6: msg = "Three Team CTF"; break;
			default: msg = "Off"; break;
		}
		M_Print (160, y, msg);
	}
	else
	{
		const char *msg;

		switch((int)teamplay.value)
		{
			case 1: msg = "No Friendly Fire"; break;
			case 2: msg = "Friendly Fire"; break;
			default: msg = "Off"; break;
		}
		M_Print (160, y, msg);
	}
	y+=8;

	M_Print (0, y, "            Skill");
	if (!coop.value && !HasBots()) // woods #botdetect
	{ 
		M_PrintRGBA(160, y, "Normal difficulty", CL_PLColours_Parse("0xffffff"), 0.5f, true);
	}
	else if (skill.value == 0)
		M_Print (160, y, "Easy difficulty");
	else if (skill.value == 1)
		M_Print (160, y, "Normal difficulty");
	else if (skill.value == 2)
		M_Print (160, y, "Hard difficulty");
	else
		M_Print (160, y, "Nightmare difficulty");
	y+=8;

	M_Print (0, y, "       Frag Limit");
	if (fraglimit.value == 0)
		M_Print (160, y, "none");
	else
		M_Print (160, y, va("%i frags", (int)fraglimit.value));
	y+=8;

	M_Print (0, y, "       Time Limit");
	if (timelimit.value == 0)
		M_Print (160, y, "none");
	else
		M_Print (160, y, va("%i minutes", (int)timelimit.value));
	y+=8;

	M_Print (0, y, "             Mod");
	selected_mod = M_GameOptions_SelectedMod();
	if (selected_mod)
		M_PrintScroll(160, y, 19 * 8, selected_mod, gameoptions_cursor == GAMEOPTIONS_MOD ? realtime : 0.0, false);
	else
	{
		M_Print (160, y, "Default (");
		M_PrintWhite (232, y, "id1");
		M_Print (256, y, ")");
	}
	y+=8;

	y+=8;

	mod_selected = selected_mod != NULL;

	M_GameOptions_PrintMaybeDim(0, y, "          Episode", mod_selected, false);
	// MED 01/06/97 added hipnotic episodes
	if (hipnotic)
		M_GameOptions_PrintMaybeDim(160, y, hipnoticepisodes[startepisode].description, mod_selected, false);
	// PGM 01/07/97 added rogue episodes
	else if (rogue)
		M_GameOptions_PrintMaybeDim(160, y, rogueepisodes[startepisode].description, mod_selected, false);
	else
		M_GameOptions_PrintMaybeDim(160, y, episodes[startepisode].description, mod_selected, false);
	y+=8;

	M_GameOptions_PrintMaybeDim(0, y, "            Level", mod_selected, false);
	dim_builtin_level = mod_selected || m_skill_mapname[0] || (goptions_levelname[0] && M_GameOptions_IsValidLevel());
	// MED 01/06/97 added hipnotic episodes
	if (hipnotic)
	{
		M_GameOptions_PrintMaybeDim(160, y, hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel].description, mod_selected, false);
		if (dim_builtin_level)  // Custom map/mod selected - show faded level name
			M_PrintRGBA(160, y + 8, hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel].name,
				CL_PLColours_Parse("0xffffff"), 0.5, false);
		else  // No custom map - show normal level name
			M_PrintWhite (160, y+8, hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel].name);
	}
	// PGM 01/07/97 added rogue episodes
	else if (rogue)
	{
		M_GameOptions_PrintMaybeDim(160, y, roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].description, mod_selected, false);
		if (dim_builtin_level)  // Custom map/mod selected - show faded level name
			M_PrintRGBA(160, y+8, roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].name,
				CL_PLColours_Parse("0xffffff"), 0.5, false);
		else  // No custom map - show normal level name
			M_PrintWhite(160, y+8, roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].name);
	}
	else
	{
		M_GameOptions_PrintMaybeDim(160, y, levels[episodes[startepisode].firstLevel + startlevel].description, mod_selected, false);
		if (dim_builtin_level)  // Custom map/mod selected - show faded level name
			M_PrintRGBA(160, y+8, levels[episodes[startepisode].firstLevel + startlevel].name,
				CL_PLColours_Parse("0xffffff"), 0.5, false);
		else  // No custom map - show normal level name
			M_PrintWhite(160, y+8, levels[episodes[startepisode].firstLevel + startlevel].name);
	}
	y +=24;
	M_Print(0, y, "    Select Level");
	if (m_skill_mapname[0])
		M_PrintWhite(160, y, m_skill_mapname);
	else
		M_Print(160, y, "...");
	y += 16;

	M_Print(0, y, "     Enter Level");
	M_DrawTextBox(GAMEOPTIONS_LEVEL_FIELD_BOX_X, y - 8, GAMEOPTIONS_LEVEL_FIELD_BOX_WIDTH, 1);
	M_TextField_DrawHighlight(&goptions_level_field, GAMEOPTIONS_LEVEL_FIELD_TEXT_X, y);
	if (goptions_levelname[0])
		M_PrintWhite(GAMEOPTIONS_LEVEL_FIELD_TEXT_X, y, goptions_levelname);

	if (goptions_levelhint[0] &&
		gameoptions_cursor == GAMEOPTIONS_ENTER_LEVEL &&
		goptions_level_field.cursor == (int)strlen(goptions_levelname))
	{
		int hint_x = GAMEOPTIONS_LEVEL_FIELD_TEXT_X + (int)strlen(goptions_levelname) * 8;
		M_PrintRGBA(hint_x, y, goptions_levelhint, CL_PLColours_Parse("0xffffff"), 0.5f, true);
	}

	if (gameoptions_cursor == GAMEOPTIONS_ENTER_LEVEL)
		M_TextField_DrawCursor(&goptions_level_field, GAMEOPTIONS_LEVEL_FIELD_TEXT_X, y);
	y += 8;

// line cursor
	M_DrawCharacter (144, gameoptions_cursor_table[gameoptions_cursor], 12+((int)(realtime*4)&1));
}


void M_NetStart_Change (int dir)
{
	int count;
	float	f;

	switch (gameoptions_cursor)
	{
	case GAMEOPTIONS_MAXPLAYERS:
		maxplayers += dir;
		if (maxplayers > svs.maxclientslimit)
			maxplayers = svs.maxclientslimit;
		if (maxplayers < 2)
			maxplayers = 2;
		break;

	case GAMEOPTIONS_PUBLIC:
		Cvar_SetQuick (&sv_public, sv_public.value ? "0" : "1");
		break;

	case GAMEOPTIONS_GAMETYPE:
		Cvar_Set ("coop", coop.value ? "0" : "1");
		break;

	case GAMEOPTIONS_TEAMPLAY:
		count = (rogue) ? 6 : 2;
		f = teamplay.value + dir;
		if (f > count)	f = 0;
		else if (f < 0)	f = count;
		Cvar_SetValue ("teamplay", f);
		break;

	case GAMEOPTIONS_SKILL:
		f = skill.value + dir;
		if (f > 3)	f = 0;
		else if (f < 0)	f = 3;
		Cvar_SetValue ("skill", f);
		break;

	case GAMEOPTIONS_FRAGLIMIT:
		f = fraglimit.value + dir * 10;
		if (f > 100)	f = 0;
		else if (f < 0)	f = 100;
		Cvar_SetValue ("fraglimit", f);
		break;

	case GAMEOPTIONS_TIMELIMIT:
		f = timelimit.value + dir * 5;
		if (f > 60)	f = 0;
		else if (f < 0)	f = 60;
		Cvar_SetValue ("timelimit", f);
		break;

	case GAMEOPTIONS_MOD:
		count = M_GameOptions_ModCount();
		gameoptions_mod_index += dir;
		if (gameoptions_mod_index < 0)
			gameoptions_mod_index = count - 1;
		if (gameoptions_mod_index >= count)
			gameoptions_mod_index = 0;
		M_GameOptions_ClampCursor();
		break;

	case GAMEOPTIONS_EPISODE:
		if (!M_GameOptions_UsingDefaultMod())
			break;
		m_skill_mapname[0] = 0;
		M_GameOptions_ClearTypedLevel();
		startepisode += dir;
	//MED 01/06/97 added hipnotic count
		if (hipnotic)
			count = 6;
	//PGM 01/07/97 added rogue count
	//PGM 03/02/97 added 1 for dmatch episode
		else if (rogue)
			count = 4;
		else if (registered.value)
			count = 7;
		else
			count = 2;

		if (startepisode < 0)
			startepisode = count - 1;

		if (startepisode >= count)
			startepisode = 0;

		startlevel = 0;
		break;

	case GAMEOPTIONS_LEVEL:
		if (!M_GameOptions_UsingDefaultMod())
			break;
		m_skill_mapname[0] = 0;
		M_GameOptions_ClearTypedLevel();
		startlevel += dir;
	//MED 01/06/97 added hipnotic episodes
		if (hipnotic)
			count = hipnoticepisodes[startepisode].levels;
	//PGM 01/06/97 added hipnotic episodes
		else if (rogue)
			count = rogueepisodes[startepisode].levels;
		else
			count = episodes[startepisode].levels;

		if (startlevel < 0)
			startlevel = count - 1;

		if (startlevel >= count)
			startlevel = 0;
		break;

	case GAMEOPTIONS_SELECT_LEVEL: // Select Level option - open maps menu
		maps_from_gameoptions = true;
		M_Menu_Maps_f();
		break;

	case GAMEOPTIONS_ENTER_LEVEL: // Enter Level field - left/right handled by textfield
		break;
	}
}

void M_GameOptions_Key (int key)
{
	menu_textfield_t *active_field = M_GameOptions_GetFieldForCursor();
	int option_count;
	if (active_field && M_TextField_Key(active_field, key))
	{
		M_GameOptions_UpdateLevelHint();
		return;
	}

	M_GameOptions_ClampCursor();
	option_count = M_GameOptions_NumItems();

	switch (key)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		M_Menu_MultiPlayer_f (); // woods #skipipx
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		if (gameoptions_cursor == GAMEOPTIONS_ENTER_LEVEL && goptions_levelname[0])
			M_GameOptions_CheckLeave();
		M_GameOptions_ClearTextSelections();
		gameoptions_cursor--;
		if (gameoptions_cursor < 0)
			gameoptions_cursor = option_count - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		if (gameoptions_cursor == GAMEOPTIONS_ENTER_LEVEL && goptions_levelname[0])
			M_GameOptions_CheckLeave();
		M_GameOptions_ClearTextSelections();
		gameoptions_cursor++;
		if (gameoptions_cursor >= option_count)
			gameoptions_cursor = 0;
		break;

	case K_LEFTARROW:
	//case K_MOUSE2:
		if (gameoptions_cursor == GAMEOPTIONS_BEGIN || gameoptions_cursor == GAMEOPTIONS_ENTER_LEVEL)
			break;
		S_LocalSound ("misc/menu3.wav");
		M_NetStart_Change (-1);
		break;

	case K_MWHEELDOWN:
		if (gameoptions_cursor == GAMEOPTIONS_BEGIN || gameoptions_cursor == GAMEOPTIONS_SELECT_LEVEL || gameoptions_cursor == GAMEOPTIONS_ENTER_LEVEL)
			break;
		S_LocalSound ("misc/menu3.wav");
		M_NetStart_Change (-1);
		break;

	case K_RIGHTARROW:
		if (gameoptions_cursor == GAMEOPTIONS_BEGIN || gameoptions_cursor == GAMEOPTIONS_ENTER_LEVEL)
			break;
		S_LocalSound ("misc/menu3.wav");
		M_NetStart_Change (1);
		break;

	case K_MWHEELUP:
		if (gameoptions_cursor == GAMEOPTIONS_BEGIN || gameoptions_cursor == GAMEOPTIONS_SELECT_LEVEL || gameoptions_cursor == GAMEOPTIONS_ENTER_LEVEL)
			break;
		S_LocalSound ("misc/menu3.wav");
		M_NetStart_Change (1);
		break;

	case K_BACKSPACE:
	case K_DEL:
		if (gameoptions_cursor == GAMEOPTIONS_SELECT_LEVEL)
		{
			m_skill_mapname[0] = 0;
		}
		else if (gameoptions_cursor == GAMEOPTIONS_ENTER_LEVEL)
		{
			M_GameOptions_UpdateLevelHint();
		}
		break;

	case K_TAB:
		if (gameoptions_cursor == GAMEOPTIONS_ENTER_LEVEL && goptions_levelhint[0])
		{
			M_GameOptions_AcceptLevelHint();
			m_skill_mapname[0] = '\0';
			S_LocalSound("misc/menu2.wav");
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1: // woods #mousemenu
		if (key == K_MOUSE1 && gameoptions_cursor == GAMEOPTIONS_ENTER_LEVEL)
		{
			if (M_TextField_MouseInRow(m_mousey, gameoptions_cursor_table[GAMEOPTIONS_ENTER_LEVEL]))
				M_TextField_MouseClick(&goptions_level_field, m_mousex, GAMEOPTIONS_LEVEL_FIELD_TEXT_X);
			return;
		}

		if (gameoptions_cursor == GAMEOPTIONS_ENTER_LEVEL)
		{
			M_GameOptions_CheckLeave();
			if (goptions_levelname[0] && M_GameOptions_IsValidLevel())
				S_LocalSound("misc/menu2.wav");
			else
				S_LocalSound("misc/menu3.wav");
			return;
		}

		S_LocalSound ("misc/menu2.wav");
		if (gameoptions_cursor == GAMEOPTIONS_BEGIN)
		{
			const char *selected_mod = M_GameOptions_SelectedMod();
			char selected_map[MAX_QPATH];

			selected_map[0] = '\0';

			if (sv.active)
				Cbuf_AddText ("disconnect\n");
			Cbuf_AddText ("listen 0\n");	// so host_netport will be re-examined
			Cbuf_AddText ( va ("maxplayers %u\n", maxplayers) );
			SCR_BeginLoadingPlaque ();

			if (goptions_levelname[0])
				M_GameOptions_CheckLeave();

			if (goptions_levelname[0] && M_GameOptions_IsValidLevel())  // Enter Level has priority
				q_strlcpy(selected_map, goptions_levelname, sizeof(selected_map));
			else if (m_skill_mapname[0])  // Fallback to Select Level
				q_strlcpy(selected_map, m_skill_mapname, sizeof(selected_map));

			M_GameOptions_SaveHistory(selected_map);

			if (selected_mod)
			{
				if (COM_GameDirMatches(selected_mod))
				{
					if (selected_map[0])
						Cbuf_AddText(va("map %s\n", selected_map));
					else
						Cbuf_AddText("modvote_startmap\n");
				}
				else
				{
					if (selected_map[0])
						Cvar_Set("sv_defaultmap", selected_map);
					COM_SetModvoteAutostart();
					Cbuf_AddText(va("game \"%s\"\n", selected_mod));
				}
				return;
			}

			if (selected_map[0])
			{
				Cbuf_AddText(va("map %s\n", selected_map));
			}
			else  // Use regular episode/level selection
			{
				if (hipnotic)
					Cbuf_AddText ( va ("map %s\n", hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel].name) );
				else if (rogue)
					Cbuf_AddText ( va ("map %s\n", roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].name) );
				else
					Cbuf_AddText ( va ("map %s\n", levels[episodes[startepisode].firstLevel + startlevel].name) );
			}

			return;
		}

		M_NetStart_Change (1);
		break;
	}
}

void M_GameOptions_Mousemove(int cx, int cy) // woods #mousemenu
{
	int old_cursor;

	if (textfield_mouse_dragging && textfield_drag_field == &goptions_level_field)
	{
		M_TextField_MouseDrag(cx);
		return;
	}

	M_GameOptions_ClampCursor();
	old_cursor = gameoptions_cursor;
	M_UpdateCursorWithTable(cy, gameoptions_cursor_table, M_GameOptions_NumItems(), &gameoptions_cursor);
	if (gameoptions_cursor != old_cursor)
	{
		if (old_cursor == GAMEOPTIONS_ENTER_LEVEL && goptions_levelname[0])
			M_GameOptions_CheckLeave();
		M_GameOptions_ClearTextSelections();
	}
}

void M_GameOptions_Char(int key)
{
	menu_textfield_t *active_field = M_GameOptions_GetFieldForCursor();
	if (active_field && M_TextField_Char(active_field, key))
		M_GameOptions_UpdateLevelHint();
}

qboolean M_GameOptions_TextEntry(void)
{
	return (gameoptions_cursor == GAMEOPTIONS_ENTER_LEVEL);
}

/*
==================
Server Search Menu
==================
*/

qboolean	searchComplete = false;
double		searchCompleteTime;
enum slistScope_e searchLastScope = SLIST_LAN;
void ResetHostlist (void); // woods #resethostlist
static void ServerList_StartApiFetch(void);
static qboolean ServerList_ApiFetchHasPendingOrResults(void);
static qboolean ServerList_ApiFetchIsLoading(void);
static void ServerList_ApplyApiResults(void);

void M_Menu_Search_f (enum slistScope_e scope)
{
	key_dest = key_menu;
	m_state = m_search;
	IN_UpdateGrabs();
	m_entersound = false;
	slistSilent = true;
	ResetHostlist(); // woods #resethostlist
	slistScope = searchLastScope = scope;
	searchComplete = false;
	if (scope == SLIST_INTERNET)
		ServerList_StartApiFetch();
	NET_Slist_f();

}


void M_Search_Draw (void)
{
	qpic_t	*p;
	int x;

	p = Draw_CachePic ("gfx/p_multi.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);
	x = (320/2) - ((12*8)/2) + 4;
	M_DrawTextBox (x-8, 32, 12, 1);
	Draw_StringGradientSweep (x, 40, "Searching...", 96.0f, 48.0f, 1.0f, true); // woods

	if(slistInProgress)
	{
		NET_Poll();
		if (searchLastScope == SLIST_INTERNET && ServerList_ApiFetchHasPendingOrResults())
			M_Menu_ServerList_f ();
		return;
	}

	if (! searchComplete)
	{
		searchComplete = true;
		searchCompleteTime = realtime;
	}

	if (hostCacheCount ||
		(searchLastScope == SLIST_INTERNET &&
			(M_Bookmarks_CountPinned() > 0 || ServerList_ApiFetchHasPendingOrResults())))
	{
		M_Menu_ServerList_f ();
		return;
	}

	M_PrintWhite ((320/2) - ((22*8)/2), 64, "No Quake servers found");
	if ((realtime - searchCompleteTime) < 3.0)
		return;

	M_Menu_LanConfig_f ();
}


void M_Search_Key (int key)
{
}

/*
==================
Server List Menu
==================
*/

#define MAX_VIS_SERVERS 17
#define PING_COOLDOWN 2.0
#define MAX_PING_QUEUE 5
#define SERVERLIST_UNAVAILABLE_ALPHA 0.35f

// number of worker threads used for the initial server ping sweep
#ifndef MAX_PING_THREADS
#define MAX_PING_THREADS 4
#endif

typedef struct {
	const char* name;
	const char* ip;
	int users;
	int maxusers;
	const char* map;
	const char* players;  // comma-separated player names (NULL if unavailable)
	int ping;
	qboolean active;
	double lastPingTime;
	qboolean isLoading;  // New flag to indicate loading state
	qboolean pinned_bookmark;
	int pinned_rank;
	qboolean known_available;
	qboolean ping_tested;
} servertitem_t;

static qboolean ServerList_IsUnavailableBookmark(const servertitem_t* server)
{
	return server && server->pinned_bookmark && server->ping_tested &&
		server->ping < 0 && !server->known_available;
}

typedef struct {
	char name[256];
	char ip[256];
	char map[64];
	char players[512];
	int users;
	int maxusers;
	int ping;
	qboolean has_players;
	qboolean unavailable_bookmark;
} servertitem_snapshot_t;

static struct {
	menulist_t list;
	enum m_state_e prev;
	int x, y, cols;
	int prev_cursor;
	menuticker_t ticker;
	qboolean scrollbar_grab;
    servertitem_t* items;
    int* order;
    int* filtered_indices;
    int servercount;
	size_t hostcache_copied;
	int pinged_count;
    int slist_first;
    qboolean pingSortDirty;
	SDL_Thread* pingThreads[MAX_PING_THREADS];
	qboolean initialPingComplete;
	int initialPingThreadsRemaining;
	int pingQueue[MAX_PING_QUEUE];
	int pingQueueSize;
	qboolean pingThreadRunning;
	SDL_Thread* pingThread;
	int sort_mode;
	qboolean sort_descending;
} serversmenu;

enum {
	SORT_NAME,
	SORT_MAP,
	SORT_USERS,
	SORT_PING
};

//=============================================================================
// woods servers.quakeone.com support curl+json parsing #serversmenu
//=============================================================================

static volatile qboolean pingThreadsShouldExit = false;
SDL_mutex* pingMutex = NULL;

static qboolean ServerList_SnapshotItem(int actualIndex, servertitem_snapshot_t *snapshot)
{
	const servertitem_t *server;
	qboolean locked = false;

	if (!snapshot)
		return false;
	memset(snapshot, 0, sizeof(*snapshot));

	if (pingMutex)
	{
		SDL_LockMutex(pingMutex);
		locked = true;
	}

	if (!serversmenu.items || actualIndex < 0 || actualIndex >= serversmenu.servercount)
	{
		if (locked)
			SDL_UnlockMutex(pingMutex);
		return false;
	}

	server = &serversmenu.items[actualIndex];
	q_strlcpy(snapshot->name, server->name ? server->name : "", sizeof(snapshot->name));
	q_strlcpy(snapshot->ip, server->ip ? server->ip : "", sizeof(snapshot->ip));
	q_strlcpy(snapshot->map, server->map ? server->map : "", sizeof(snapshot->map));
	if (server->players && server->players[0])
	{
		q_strlcpy(snapshot->players, server->players, sizeof(snapshot->players));
		snapshot->has_players = true;
	}
	snapshot->users = server->users;
	snapshot->maxusers = server->maxusers;
	snapshot->ping = server->ping;
	snapshot->unavailable_bookmark = ServerList_IsUnavailableBookmark(server);

	if (locked)
		SDL_UnlockMutex(pingMutex);

	return true;
}

typedef enum
{
	SERVERLIST_API_IDLE,
	SERVERLIST_API_LOADING,
	SERVERLIST_API_READY,
	SERVERLIST_API_ERROR
} serverlistapistate_t;

static struct
{
	SDL_mutex* mutex;
	SDL_Thread* thread;
	serverlistapistate_t state;
	servertitem_t* items;
	int count;
} serverlistapi;

int UDP_Ping_Host(const char* host);
char *UDP_QueryPlayers(const char *host, int maxslots);

static int ServersMenu_ResolveIndex(int displayIndex);
static void SortServers(qboolean lockMutex);
static void SortServersWithSelection(qboolean lockMutex, int selectedActual);
static void M_ServerList_Refilter(void);
static void M_ServerList_RefilterWithSelection(int selectedActual);
qboolean M_Servers_Match(int index, char initial);
static void ServerList_FreeItem(servertitem_t* item);
static void ServerList_Rescroll(void);
static void ServerList_CenterCursor(void);
static int ServerList_FindDuplicateItem(const servertitem_t* items, int count, const servertitem_t* candidate);
static void ServerList_AppendHostCacheResults(void);
static void CleanupPingThreads(void);

static void ServerList_CopyTrimmedToken(const char* start, size_t len, char* out, size_t outsize)
{
	const char* end = start + len;

	while (start < end && (*start == ' ' || *start == '\t'))
		start++;
	while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
		end--;

	len = end - start;
	if (!outsize)
		return;
	if (len >= outsize)
		len = outsize - 1;

	memcpy(out, start, len);
	out[len] = 0;
}

static int ServerList_HexValue(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c + (10 - 'a');
	if (c >= 'A' && c <= 'F')
		return c + (10 - 'A');
	return -1;
}

static qboolean ServerList_ReadUtf8CodePoint(const unsigned char* src, uint32_t* codepoint, int* bytes)
{
	uint32_t c;

	if (!src || !*src || !codepoint || !bytes)
		return false;

	if (src[0] < 0x80)
	{
		*codepoint = src[0];
		*bytes = 1;
		return true;
	}

	if ((src[0] & 0xe0) == 0xc0 && src[1] &&
		(src[1] & 0xc0) == 0x80)
	{
		c = ((uint32_t)(src[0] & 0x1f) << 6) |
			(uint32_t)(src[1] & 0x3f);
		if (c >= 0x80)
		{
			*codepoint = c;
			*bytes = 2;
			return true;
		}
	}

	if ((src[0] & 0xf0) == 0xe0 && src[1] && src[2] &&
		(src[1] & 0xc0) == 0x80 &&
		(src[2] & 0xc0) == 0x80)
	{
		c = ((uint32_t)(src[0] & 0x0f) << 12) |
			((uint32_t)(src[1] & 0x3f) << 6) |
			(uint32_t)(src[2] & 0x3f);
		if (c >= 0x800)
		{
			*codepoint = c;
			*bytes = 3;
			return true;
		}
	}

	if ((src[0] & 0xf8) == 0xf0 && src[1] && src[2] && src[3] &&
		(src[1] & 0xc0) == 0x80 &&
		(src[2] & 0xc0) == 0x80 &&
		(src[3] & 0xc0) == 0x80)
	{
		c = ((uint32_t)(src[0] & 0x07) << 18) |
			((uint32_t)(src[1] & 0x3f) << 12) |
			((uint32_t)(src[2] & 0x3f) << 6) |
			(uint32_t)(src[3] & 0x3f);
		if (c >= 0x10000 && c < 0x110000)
		{
			*codepoint = c;
			*bytes = 4;
			return true;
		}
	}

	return false;
}

static void ServerList_AppendPlainQuakeChar(char** out, char* end, int ch)
{
	unsigned char plain;

	if (*out >= end)
		return;

	if (ch == '\t' || ch == '\n' || ch == '\r')
		ch = ' ';

	plain = (unsigned char)dequake[(unsigned char)ch];
	if (!q_isprint(plain))
		plain = ' ';

	*(*out)++ = (char)plain;
}

static void ServerList_AppendPlainCodePoint(char** out, char* end, uint32_t codepoint)
{
	if (codepoint >= 0xe000 && codepoint <= 0xe0ff)
		ServerList_AppendPlainQuakeChar(out, end, codepoint & 0xff);
	else if (codepoint >= 0x20 && codepoint <= 0x7e)
		ServerList_AppendPlainQuakeChar(out, end, (int)codepoint);
	else if (codepoint == '\t' || codepoint == '\n' || codepoint == '\r')
		ServerList_AppendPlainQuakeChar(out, end, ' ');
	else
		ServerList_AppendPlainQuakeChar(out, end, '?');
}

static qboolean ServerList_ParseCaretCodePoint(const unsigned char** src, uint32_t* codepoint)
{
	const unsigned char* s = *src;
	int c;
	int ndigits;

	if (s[1] == 'U' &&
		q_isxdigit(s[2]) && q_isxdigit(s[3]) &&
		q_isxdigit(s[4]) && q_isxdigit(s[5]))
	{
		*codepoint = (ServerList_HexValue(s[2]) << 12) |
			(ServerList_HexValue(s[3]) << 8) |
			(ServerList_HexValue(s[4]) << 4) |
			ServerList_HexValue(s[5]);
		*src += 6;
		return true;
	}

	if (s[1] != '{')
		return false;

	s += 2;
	c = 0;
	ndigits = 0;
	while (*s && *s != '}')
	{
		int hex = ServerList_HexValue(*s);
		if (hex < 0 || ++ndigits > 6) // bound digit count to avoid signed shift overflow
			return false;
		c = (c << 4) | hex;
		s++;
	}

	if (*s != '}' || !ndigits || c > 0x10FFFF) // require >=1 digit, reject out-of-range codepoints
		return false;

	*codepoint = (uint32_t)c;
	*src = s + 1;
	return true;
}

static void ServerList_CopyDisplayText(const char* src, char* dst, size_t dstsize)
{
	const unsigned char* in = (const unsigned char*)src;
	char* out = dst;
	char* end;

	if (!dstsize)
		return;
	dst[0] = 0;
	if (!src)
		return;

	end = dst + dstsize - 1;
	while (*in && out < end)
	{
		uint32_t codepoint;
		int bytes;

		if (in[0] == '^' && in[1])
		{
			const unsigned char* caret = in;

			if (in[1] == '^')
			{
				ServerList_AppendPlainQuakeChar(&out, end, '^');
				in += 2;
				continue;
			}

			if ((in[1] >= '0' && in[1] <= '9') ||
				in[1] == 'h' || in[1] == 'b' ||
				in[1] == 'd' || in[1] == 's' ||
				in[1] == 'r' || in[1] == 'a' ||
				in[1] == 'm' || in[1] == 'g')
			{
				in += 2;
				continue;
			}

			if (in[1] == 'x' &&
				q_isxdigit(in[2]) && q_isxdigit(in[3]) && q_isxdigit(in[4]))
			{
				in += 5;
				continue;
			}

			if (in[1] == '&' &&
				(q_isxdigit(in[2]) || in[2] == '-') &&
				(q_isxdigit(in[3]) || in[3] == '-'))
			{
				in += 4;
				continue;
			}

			if (ServerList_ParseCaretCodePoint(&caret, &codepoint))
			{
				ServerList_AppendPlainCodePoint(&out, end, codepoint);
				in = caret;
				continue;
			}
		}

		if (ServerList_ReadUtf8CodePoint(in, &codepoint, &bytes))
		{
			ServerList_AppendPlainCodePoint(&out, end, codepoint);
			in += bytes;
		}
		else
		{
			ServerList_AppendPlainQuakeChar(&out, end, *in);
			in++;
		}
	}

	*out = 0;
}

static void ServerList_GetBaseName(const char* name, char* out, size_t outsize)
{
	if (!name)
		name = "";
	if (*name == '*')
		name++;
	q_strlcpy(out, name, outsize);
}

static void ServerList_GetHostOnly(const char* address, char* out, size_t outsize)
{
	const char* start;
	const char* end;

	if (!outsize)
		return;
	out[0] = 0;

	if (!address || !*address)
		return;

	if (address[0] == '[')
	{
		start = address + 1;
		end = strchr(start, ']');
		if (!end)
			end = address + strlen(address);
	}
	else
	{
		const char* last_colon = strrchr(address, ':');
		start = address;
		end = last_colon ? last_colon : address + strlen(address);
	}

	ServerList_CopyTrimmedToken(start, end - start, out, outsize);
}

typedef struct
{
	char host[256];
	int port;
} serverlistaddress_t;

static qboolean ServerList_ParsePortText(const char* port, const char* end, int* out)
{
	long value = 0;

	if (!port || !*port)
		return false;

	if (!end)
		end = port + strlen(port);

	if (port >= end)
		return false;

	while (port < end)
	{
		if (!q_isdigit((unsigned char)*port))
			return false;
		value = value * 10 + (*port - '0');
		if (value > 65535)
			return false;
		port++;
	}

	if (out)
		*out = (int)value;
	return true;
}

static qboolean ServerList_ParseAddress(const char* address, serverlistaddress_t* parsed)
{
	const char* host_start;
	const char* host_end;
	const char* address_end;
	const char* port_start = NULL;
	const char* port_end = NULL;
	const char* last_colon;
	const char* first_colon;
	size_t host_len;

	if (!parsed)
		return false;

	memset(parsed, 0, sizeof(*parsed));
	parsed->port = DEFAULTnet_hostport;

	if (!address || !*address)
		return false;

	while (*address == ' ' || *address == '\t')
		address++;

	host_start = address;
	host_end = address + strlen(address);
	while (host_end > host_start &&
		(host_end[-1] == ' ' || host_end[-1] == '\t'))
		host_end--;
	address_end = host_end;

	if (host_start >= host_end)
		return false;

	if (*host_start == '[')
	{
		const char* bracket = memchr(host_start, ']', host_end - host_start);
		const char* after;

		if (!bracket || bracket == host_start + 1)
			return false;

		after = bracket + 1;
		host_start++;
		host_end = bracket;
		if (after < address_end)
		{
			if (*after != ':')
				return false;
			port_start = after + 1;
			port_end = address_end;
		}
	}
	else
	{
		first_colon = memchr(host_start, ':', host_end - host_start);
		last_colon = first_colon ? memchr(first_colon + 1, ':', host_end - first_colon - 1) : NULL;
		if (first_colon && !last_colon)
		{
			port_start = first_colon + 1;
			port_end = address_end;
			host_end = first_colon;
		}
	}

	host_len = host_end - host_start;
	if (host_len == 0)
		return false;
	if (host_len >= sizeof(parsed->host))
		return false;

	memcpy(parsed->host, host_start, host_len);
	parsed->host[host_len] = '\0';

	if (port_start && !ServerList_ParsePortText(port_start, port_end, &parsed->port))
		return false;

	return parsed->host[0] != '\0';
}

static qboolean ServerList_AddressMatches(const char* a, const char* b)
{
	serverlistaddress_t parsed_a;
	serverlistaddress_t parsed_b;

	if (!a || !b || !*a || !*b)
		return false;

	if (!q_strcasecmp(a, b))
		return true;

	if (!ServerList_ParseAddress(a, &parsed_a) ||
		!ServerList_ParseAddress(b, &parsed_b))
		return false;

	return !q_strcasecmp(parsed_a.host, parsed_b.host) &&
		parsed_a.port == parsed_b.port;
}

static qboolean ServerList_IsIgnored(const char* name, const char* ip)
{
	const char* list = net_master_ignore.string;
	char token[256];
	char baseName[256];
	char hostOnly[NET_NAMELEN];

	if (!list || !*list)
		return false;

	ServerList_GetBaseName(name, baseName, sizeof(baseName));
	ServerList_GetHostOnly(ip, hostOnly, sizeof(hostOnly));

	while (*list)
	{
		const char* comma = strchr(list, ',');
		size_t len = comma ? (size_t)(comma - list) : strlen(list);

		ServerList_CopyTrimmedToken(list, len, token, sizeof(token));
		if (*token)
		{
			qboolean tokenLooksLikeAddress = strchr(token, '.') || strchr(token, ':') || strchr(token, '[') || strchr(token, ']');

			if ((name && !q_strcasecmp(token, name)) ||
				(baseName[0] && !q_strcasecmp(token, baseName)) ||
				(ip && !q_strcasecmp(token, ip)) ||
				(hostOnly[0] && !q_strcasecmp(token, hostOnly)) ||
				(tokenLooksLikeAddress && ((name && q_strcasestr(name, token)) ||
				(baseName[0] && q_strcasestr(baseName, token)))))
				return true;
		}

		if (!comma)
			break;
		list = comma + 1;
	}

	return false;
}

static int ServerList_PinnedRank(const servertitem_t* server)
{
	if (!server || !server->pinned_bookmark)
		return -1;

	return server->pinned_rank;
}

static int ServerList_SortGroup(const servertitem_t* server)
{
	if (!server || !server->pinned_bookmark)
		return 1;
	return 0;
}

static qboolean ServerList_CreatePinnedBookmarkItem(const pinnedbookmark_t* bookmark, int rank, servertitem_t* item)
{
	if (!bookmark || !item || !bookmark->name[0] || !bookmark->alias[0])
		return false;

	memset(item, 0, sizeof(*item));

	item->name = strdup(bookmark->alias);
	item->ip = strdup(bookmark->name);
	item->map = strdup("");
	item->players = NULL;
	item->users = 0;
	item->maxusers = 0;
	item->active = true;
	item->ping = -1;
	item->lastPingTime = 0;
	item->isLoading = false;
	item->pinned_bookmark = true;
	item->pinned_rank = rank;
	item->known_available = (cls.state == ca_connected &&
		ServerList_AddressMatches(bookmark->name, lastmphost));
	item->ping_tested = false;

	if (!item->name || !item->ip || !item->map)
	{
		ServerList_FreeItem(item);
		return false;
	}

	return true;
}

static void ServerList_FreeItem(servertitem_t* item)
{
	if (!item)
		return;

	free((void *)item->name);
	free((void *)item->ip);
	free((void *)item->map);
	free((void *)item->players);

	item->name = NULL;
	item->ip = NULL;
	item->map = NULL;
	item->players = NULL;
}

static void ServerList_FreeItems(servertitem_t* items, int count)
{
	int i;

	if (!items)
		return;

	for (i = 0; i < count; ++i)
		ServerList_FreeItem(&items[i]);

	free(items);
}

static void ServerList_MoveItem(servertitem_t* dst, servertitem_t* src)
{
	if (!dst || !src || dst == src)
		return;

	*dst = *src;
	memset(src, 0, sizeof(*src));
}

static qboolean ServerList_AppendMovedItem(servertitem_t** items, int* count, servertitem_t* src)
{
	servertitem_t* resizedItems;

	if (!items || !count || !src)
		return false;

	resizedItems = (servertitem_t*)realloc(*items, sizeof(servertitem_t) * (*count + 1));
	if (!resizedItems)
	{
		Con_DPrintf("Memory allocation failed.\n");
		return false;
	}

	*items = resizedItems;
	ServerList_MoveItem(&(*items)[*count], src);
	(*count)++;
	return true;
}

static int ServerList_FindDuplicateItem(const servertitem_t* items, int count, const servertitem_t* candidate)
{
	const char* candidateName;
	const char* candidateBaseName;
	qboolean candidateStarred;

	if (!candidate)
		return -1;

	candidateName = candidate->name ? candidate->name : "";
	candidateBaseName = candidate->name && candidate->name[0] == '*' ? candidate->name + 1 : candidateName;
	candidateStarred = candidate->name && candidate->name[0] == '*';

	for (int i = 0; i < count; i++)
	{
		const char* existingName = items[i].name ? items[i].name : "";
		const char* existingBaseName = items[i].name && items[i].name[0] == '*' ? items[i].name + 1 : existingName;
		qboolean existingStarred = items[i].name && items[i].name[0] == '*';

		if (candidate->ip && items[i].ip &&
			ServerList_AddressMatches(candidate->ip, items[i].ip))
			return i;

		if ((candidateStarred || existingStarred) &&
			!q_strcasecmp(candidateBaseName, existingBaseName) &&
			candidate->map && items[i].map &&
			!q_strcasecmp(candidate->map, items[i].map) &&
			candidate->users == items[i].users &&
			candidate->maxusers == items[i].maxusers)
			return i;
	}

	return -1;
}

static qboolean ServerList_ShouldReplaceDuplicate(const servertitem_t* existing, const servertitem_t* candidate)
{
	qboolean existingStarred = existing && existing->name && existing->name[0] == '*';
	qboolean candidateStarred = candidate && candidate->name && candidate->name[0] == '*';

	return existingStarred && !candidateStarred;
}

static void ServerList_MergeLiveDetailsIntoPinned(servertitem_t* pinned, servertitem_t* live)
{
	if (!pinned || !live || !pinned->pinned_bookmark)
		return;

	free((void *)pinned->map);
	free((void *)pinned->players);

	pinned->map = live->map;
	pinned->players = live->players;
	live->map = NULL;
	live->players = NULL;

	pinned->users = live->users;
	pinned->maxusers = live->maxusers;
	if (live->ping >= 0)
		pinned->ping = live->ping;
	pinned->active = live->active;
	pinned->isLoading = live->isLoading;
	pinned->known_available = pinned->known_available || live->known_available;
	if (live->lastPingTime > pinned->lastPingTime)
		pinned->lastPingTime = live->lastPingTime;
}

static qboolean ServerList_AppendOrReplaceMovedItem(servertitem_t** items, int* count, servertitem_t* src, int* changedIndex)
{
	int duplicateIndex;
	int appendIndex;

	if (changedIndex)
		*changedIndex = -1;

	if (!items || !count || !src)
		return false;

	duplicateIndex = ServerList_FindDuplicateItem(*items, *count, src);
	if (duplicateIndex >= 0)
	{
		if ((*items)[duplicateIndex].pinned_bookmark && !src->pinned_bookmark)
		{
			ServerList_MergeLiveDetailsIntoPinned(&(*items)[duplicateIndex], src);
			ServerList_FreeItem(src);
			if (changedIndex)
				*changedIndex = duplicateIndex;
			return true;
		}
		if (ServerList_ShouldReplaceDuplicate(&(*items)[duplicateIndex], src))
		{
			ServerList_FreeItem(&(*items)[duplicateIndex]);
			ServerList_MoveItem(&(*items)[duplicateIndex], src);
			if (changedIndex)
				*changedIndex = duplicateIndex;
			return true;
		}
		return false;
	}

	appendIndex = *count;
	if (!ServerList_AppendMovedItem(items, count, src))
		return false;

	if (changedIndex)
		*changedIndex = appendIndex;
	return true;
}

static void ServerList_RebuildOrderAndFilter(void)
{
	int selectedActual = ServersMenu_ResolveIndex(serversmenu.list.cursor);

	free(serversmenu.order);
	serversmenu.order = NULL;
	serversmenu.list.numitems = serversmenu.servercount;

	if (serversmenu.servercount > 0)
	{
		serversmenu.order = (int*)malloc(sizeof(int) * serversmenu.servercount);
		if (serversmenu.order)
		{
			for (int i = 0; i < serversmenu.servercount; ++i)
				serversmenu.order[i] = i;
		}
	}

	if (serversmenu.servercount > 0)
	{
		if (serversmenu.list.cursor < 0)
			serversmenu.list.cursor = 0;
		else if (serversmenu.list.cursor >= serversmenu.servercount)
			serversmenu.list.cursor = serversmenu.servercount - 1;
	}
	else
	{
		serversmenu.list.cursor = 0;
		serversmenu.list.scroll = 0;
	}

	serversmenu.pingSortDirty = false;
	SortServersWithSelection(false, selectedActual);

	if (serversmenu.list.search.len > 0)
		M_ServerList_RefilterWithSelection(selectedActual);
	else if (serversmenu.list.viewsize > 0)
		ServerList_Rescroll();
}

static void ServerList_ApiEnsureMutex(void)
{
	if (!serverlistapi.mutex)
		serverlistapi.mutex = SDL_CreateMutex();
}

static void ServerList_ApiReapThread(qboolean wait)
{
	SDL_Thread* thread = NULL;

	if (!serverlistapi.mutex)
		return;

	SDL_LockMutex(serverlistapi.mutex);
	if (serverlistapi.thread &&
		(wait || serverlistapi.state != SERVERLIST_API_LOADING))
	{
		thread = serverlistapi.thread;
		serverlistapi.thread = NULL;
	}
	SDL_UnlockMutex(serverlistapi.mutex);

	if (thread)
		SDL_WaitThread(thread, NULL);
}

void M_ServerList_ShutdownApiFetch(void)
{
	SDL_mutex* mutex;

	ServerList_ApiReapThread(true);

	mutex = serverlistapi.mutex;
	if (!mutex)
		return;

	SDL_LockMutex(mutex);
	ServerList_FreeItems(serverlistapi.items, serverlistapi.count);
	serverlistapi.items = NULL;
	serverlistapi.count = 0;
	serverlistapi.state = SERVERLIST_API_IDLE;
	serverlistapi.thread = NULL;
	SDL_UnlockMutex(mutex);

	SDL_DestroyMutex(mutex);
	serverlistapi.mutex = NULL;
}

static qboolean ServerList_CreateHostCacheItem(size_t index, servertitem_t* item)
{
	const char* serverName;
	const char* serverIP;
	const char* map;
	char serverNameBuf[64];

	if (!item)
		return false;

	memset(item, 0, sizeof(*item));

	serverName = NET_SlistPrintServerInfo(index, SERVER_NAME);
	serverIP = NET_SlistPrintServerInfo(index, SERVER_CNAME);
	map = NET_SlistPrintServerInfo(index, SERVER_MAP);

	if (!serverName)
		serverName = "";
	ServerList_CopyDisplayText(serverName, serverNameBuf, sizeof(serverNameBuf));

	if (!serverNameBuf[0] || ServerList_IsIgnored(serverNameBuf, serverIP))
		return false;

	item->name = strdup(serverNameBuf);
	item->ip = strdup(serverIP);
	item->users = atoi(NET_SlistPrintServerInfo(index, SERVER_USERS));
	item->maxusers = atoi(NET_SlistPrintServerInfo(index, SERVER_MAX_USERS));
	item->map = strdup(map ? map : "");
	item->players = NULL;
	item->active = true;
	item->ping = -1;
	item->lastPingTime = 0;
	item->isLoading = false;
	item->pinned_bookmark = false;
	item->pinned_rank = -1;
	item->known_available = true;
	item->ping_tested = false;

	if (!item->name || !item->ip || !item->map)
	{
		ServerList_FreeItem(item);
		return false;
	}

	return true;
}

void InitializePingMutex(void)
{
	if (pingMutex != NULL)
		return;

	pingMutex = SDL_CreateMutex();
	if (pingMutex == NULL) {
		Con_DPrintf("SDL_CreateMutex failed: %s\n", SDL_GetError());
	}
}

void CleanupPingMutex(void)
{
	if (pingMutex != NULL) {
		SDL_DestroyMutex(pingMutex);
		pingMutex = NULL;
	}
}

void PingSingleServer(int index)
{
	qboolean same_server;
	qboolean was_unavailable;

	if (index < 0 || index >= serversmenu.servercount)
		return;

	char serverAddress[256];
	int  previousPing;
	int  users;

	SDL_LockMutex(pingMutex);
	if (!serversmenu.items || index >= serversmenu.servercount || !serversmenu.items[index].ip)
	{
		SDL_UnlockMutex(pingMutex);
		return;
	}
	q_strlcpy(serverAddress, serversmenu.items[index].ip, sizeof(serverAddress));
	previousPing = serversmenu.items[index].ping;
	users = serversmenu.items[index].users;
	was_unavailable = ServerList_IsUnavailableBookmark(&serversmenu.items[index]);
	serversmenu.items[index].isLoading = true;  // Set loading flag
	SDL_UnlockMutex(pingMutex);

	int ping = UDP_Ping_Host(serverAddress);

	SDL_LockMutex(pingMutex);
	same_server = (serversmenu.items && index < serversmenu.servercount &&
		serversmenu.items[index].ip && !strcmp(serversmenu.items[index].ip, serverAddress));
	if (same_server && ping >= 0)
	{
		serversmenu.items[index].ping = ping;
	}
	else if (same_server && previousPing >= 0)
	{
		serversmenu.items[index].ping = previousPing;
	}
	else if (same_server)
	{
		serversmenu.items[index].ping = -1;  // -1 indicates "failed"
	}
	if (same_server)
	{
		serversmenu.items[index].ping_tested = true;
		serversmenu.items[index].isLoading = false;  // Clear loading flag
		if (was_unavailable != ServerList_IsUnavailableBookmark(&serversmenu.items[index]))
			serversmenu.pingSortDirty = true;
	}
	SDL_UnlockMutex(pingMutex);

	/* refresh player names on re-ping if server has players */
	if (same_server && ping >= 0 && users > 0)
	{
		char *playernames = UDP_QueryPlayers(serverAddress, users);
		if (playernames)
		{
			qboolean stored_players = false;

			SDL_LockMutex(pingMutex);
			if (serversmenu.items && index < serversmenu.servercount &&
				serversmenu.items[index].ip && !strcmp(serversmenu.items[index].ip, serverAddress))
			{
				free((void *)serversmenu.items[index].players);
				serversmenu.items[index].players = playernames;
				stored_players = true;
			}
			SDL_UnlockMutex(pingMutex);

			if (!stored_players)
				free(playernames);
		}
	}
}

int ProcessPingQueue(void* data)
{
	while (!pingThreadsShouldExit)
	{
		int serverIndex = -1;

		SDL_LockMutex(pingMutex);
		if (serversmenu.pingQueueSize > 0)
		{
			serverIndex = serversmenu.pingQueue[0];
			for (int i = 0; i < serversmenu.pingQueueSize - 1; i++)
				serversmenu.pingQueue[i] = serversmenu.pingQueue[i + 1];
			serversmenu.pingQueueSize--;
		}
		SDL_UnlockMutex(pingMutex);

		if (serverIndex != -1)
			PingSingleServer(serverIndex);
		else
			SDL_Delay(10);  // Short delay to prevent busy-waiting
	}

	SDL_LockMutex(pingMutex);
	serversmenu.pingThreadRunning = false;
	SDL_UnlockMutex(pingMutex);

	return 0;
}

int PingSingleServerThread(void* data)
{
	int index = (int)(intptr_t)data;
	PingSingleServer(index);
	return 0;
}

static void PingSweepThreadFinished(void)
{
	SDL_LockMutex(pingMutex);
	if (serversmenu.initialPingThreadsRemaining > 0)
		serversmenu.initialPingThreadsRemaining--;
	if (serversmenu.initialPingThreadsRemaining <= 0)
	{
		serversmenu.initialPingThreadsRemaining = 0;
		serversmenu.initialPingComplete = true;
		serversmenu.pingSortDirty = true;
	}
	SDL_UnlockMutex(pingMutex);
}

void TriggerServerPing(int index)
{
	int actualIndex;
	double currentTime;

	if (!pingMutex)
		return;
	SDL_LockMutex(pingMutex);
	if (!serversmenu.initialPingComplete)
	{
		SDL_UnlockMutex(pingMutex);
		return;
	}
	actualIndex = ServersMenu_ResolveIndex(index);
	if (!serversmenu.items || actualIndex < 0 || actualIndex >= serversmenu.servercount)
	{
		SDL_UnlockMutex(pingMutex);
		return;
	}

	currentTime = Sys_DoubleTime();
	if ((currentTime - serversmenu.items[actualIndex].lastPingTime) >= PING_COOLDOWN)
	{
		if (serversmenu.pingQueueSize < MAX_PING_QUEUE)
		{
			serversmenu.pingQueue[serversmenu.pingQueueSize++] = actualIndex;
			serversmenu.items[actualIndex].lastPingTime = currentTime;
		}
	}
	if (serversmenu.pingQueueSize > 0 && !serversmenu.pingThreadRunning)
	{
		serversmenu.pingThread = SDL_CreateThread(ProcessPingQueue, "PingQueueThread", NULL);
		if (serversmenu.pingThread == NULL)
			Con_DPrintf("SDL_CreateThread failed: %s\n", SDL_GetError());
		else
			serversmenu.pingThreadRunning = true;
	}
	SDL_UnlockMutex(pingMutex);
}

int PingServers(void* data)
{
	if (!data)
	{
		Con_DPrintf("PingServers received a null pointer\n");
		return -1; // Return an error if data is null
	}

	int start = ((int*)data)[0];
	int end = ((int*)data)[1];

	for (int i = start; i < end; i++)
	{
		if (pingThreadsShouldExit)
			break;

		SDL_LockMutex(pingMutex);
		if (serversmenu.items && i < serversmenu.servercount && serversmenu.items[i].ip)
		{
			char serverAddress[256];
			int users;
			qboolean has_players_already;
			qboolean same_server;
			qboolean was_unavailable;
			q_strlcpy(serverAddress, serversmenu.items[i].ip, sizeof(serverAddress));
			users = serversmenu.items[i].users;
			has_players_already = (serversmenu.items[i].players != NULL);
			was_unavailable = ServerList_IsUnavailableBookmark(&serversmenu.items[i]);
			SDL_UnlockMutex(pingMutex);

			int ping = UDP_Ping_Host(serverAddress);

			SDL_LockMutex(pingMutex);
			same_server = (serversmenu.items && i < serversmenu.servercount &&
				serversmenu.items[i].ip && !strcmp(serversmenu.items[i].ip, serverAddress));
			if (same_server)
			{
				serversmenu.items[i].ping = (ping >= 0) ? ping : -1;
				serversmenu.items[i].ping_tested = true;
				if (was_unavailable != ServerList_IsUnavailableBookmark(&serversmenu.items[i]))
					serversmenu.pingSortDirty = true;
			}
			SDL_UnlockMutex(pingMutex);

			/* query player names if server responded to ping and has players */
			if (same_server && ping >= 0 && users > 0 && !has_players_already)
			{
				char *playernames = UDP_QueryPlayers(serverAddress, users);
				if (playernames)
				{
					qboolean stored_players = false;

					SDL_LockMutex(pingMutex);
					if (serversmenu.items && i < serversmenu.servercount &&
						serversmenu.items[i].ip && !strcmp(serversmenu.items[i].ip, serverAddress) &&
						!serversmenu.items[i].players)
					{
						serversmenu.items[i].players = playernames;
						stored_players = true;
					}
					SDL_UnlockMutex(pingMutex);

					if (!stored_players)
						free(playernames);
				}
			}
		}
		else
		{
			SDL_UnlockMutex(pingMutex);
			Con_DPrintf("Invalid server item or IP\n");
		}
	}

	free(data);
	PingSweepThreadFinished();

	return 0;
}

void WaitForPingThreads(void)
{
	if (!pingMutex)
	{
		pingThreadsShouldExit = false;
		return;
	}

	pingThreadsShouldExit = true; // Signal threads to exit

	for (int i = 0; i < MAX_PING_THREADS; ++i)
	{
		SDL_Thread* t = serversmenu.pingThreads[i];
		if (t)
		{
			SDL_WaitThread(t, NULL);
			serversmenu.pingThreads[i] = NULL; // Set to NULL after joining
		}
	}

	SDL_LockMutex(pingMutex);
	serversmenu.initialPingThreadsRemaining = 0;
	serversmenu.initialPingComplete = true;
	SDL_UnlockMutex(pingMutex);
}

static void JoinFinishedPingSweep(void)
{
	for (int i = 0; i < MAX_PING_THREADS; ++i)
	{
		SDL_Thread* t = serversmenu.pingThreads[i];
		if (t)
		{
			SDL_WaitThread(t, NULL);
			serversmenu.pingThreads[i] = NULL;
		}
	}
}

static void PingServerRange(int rangeStart, int rangeEnd)
{
	int servercount;
	int desiredThreads;
	int base;
	int rem;
	int start;

	if (rangeStart < 0)
		rangeStart = 0;
	if (rangeEnd > serversmenu.servercount)
		rangeEnd = serversmenu.servercount;
	if (rangeEnd < rangeStart)
		rangeEnd = rangeStart;

	servercount = rangeEnd - rangeStart;

	for (int i = 0; i < MAX_PING_THREADS; ++i)
		serversmenu.pingThreads[i] = NULL;

	if (servercount <= 0)
	{
		SDL_LockMutex(pingMutex);
		serversmenu.initialPingThreadsRemaining = 0;
		serversmenu.initialPingComplete = true;
		serversmenu.pingSortDirty = true;
		SDL_UnlockMutex(pingMutex);
		return;
	}

	desiredThreads = MAX_PING_THREADS;
	if (desiredThreads > servercount)
		desiredThreads = servercount; // don't spawn more threads than servers

	base = servercount / desiredThreads;
	rem  = servercount % desiredThreads;

	SDL_LockMutex(pingMutex);
	serversmenu.initialPingThreadsRemaining = desiredThreads;
	serversmenu.initialPingComplete = false;
	SDL_UnlockMutex(pingMutex);

	start = rangeStart;
	for (int i = 0; i < desiredThreads; ++i)
	{
		int count = base + (i < rem ? 1 : 0);
		int end = start + count;

		int* range = (int*)malloc(2 * sizeof(int));
		if (!range)
		{
			Con_DPrintf("Memory allocation failed\n");
			PingSweepThreadFinished();
			continue;
		}
		range[0] = start;
		range[1] = end;

		char namebuf[32];
		q_snprintf(namebuf, sizeof(namebuf), "PingServersThread%d", i + 1);
		serversmenu.pingThreads[i] = SDL_CreateThread(PingServers, namebuf, (void*)range);
		if (serversmenu.pingThreads[i] == NULL)
		{
			Con_DPrintf("SDL_CreateThread failed: %s\n", SDL_GetError());
			free(range);
			PingSweepThreadFinished();
		}

		start = end;
	}
}

static void ServerList_StartPendingPingSweep(void)
{
	qboolean complete;
	int start;
	int end;

	if (!pingMutex)
		return;

	SDL_LockMutex(pingMutex);
	complete = serversmenu.initialPingComplete;
	start = serversmenu.pinged_count;
	end = serversmenu.servercount;
	SDL_UnlockMutex(pingMutex);

	if (!complete || start >= end)
		return;

	JoinFinishedPingSweep();
	PingServerRange(start, end);

	SDL_LockMutex(pingMutex);
	if (serversmenu.pinged_count < end)
		serversmenu.pinged_count = end;
	SDL_UnlockMutex(pingMutex);
}

void PingAllServers(void)
{
	int servercount = serversmenu.servercount;

	PingServerRange(0, servercount);

	SDL_LockMutex(pingMutex);
	serversmenu.pinged_count = servercount;
	SDL_UnlockMutex(pingMutex);
}

struct MemoryStruct
{
	char* memory;
	size_t size;
};

static size_t WriteMemoryCallback (void* contents, size_t size, size_t nmemb, void* userp)
{
	size_t realSize = size * nmemb;
	struct MemoryStruct* mem = (struct MemoryStruct*)userp;

	char* ptr = realloc(mem->memory, mem->size + realSize + 1);
	if (!ptr) {
		Con_DPrintf("not enough memory (realloc returned NULL)\n");
		return 0;
	}

	mem->memory = ptr;
	memcpy(&(mem->memory[mem->size]), contents, realSize);
	mem->size += realSize;
	mem->memory[mem->size] = 0;

	return realSize;
}

void setStatusFlagBasedOnTimestamp (const char* timestamp, const char* lastQuery, qboolean* status)
{
	char bufTimestamp[20], bufLastQuery[20]; // Extract time components up to seconds

	if (!timestamp || !lastQuery) // guard malformed/incomplete JSON entries
	{
		*status = false;
		return;
	}

	Q_strncpy(bufTimestamp, timestamp, 19);
	bufTimestamp[19] = '\0';
	Q_strncpy(bufLastQuery, lastQuery, 19);
	bufLastQuery[19] = '\0';

	if (Q_strcmp(bufTimestamp, bufLastQuery) == 0) // Compare the timestamp and lastQuery to the second
		*status = true;
	else
		*status = false;
}

void populateServersFromJSON (const char* jsonText, servertitem_t** items, int* actualServerCount)
{
	json_t* json = JSON_Parse(jsonText);
	if (!json || !json->root || json->root->type != JSON_ARRAY) 
	{
		Con_DPrintf("Failed to parse JSON or JSON is not an array.\n");
		if (json) JSON_Free(json);
		return;
	}

	const jsonentry_t* serverEntry;
	for (serverEntry = json->root->firstchild; serverEntry; serverEntry = serverEntry->next)
	{
		const char* name = JSON_FindString(serverEntry, "hostname");
		const char* address = JSON_FindString(serverEntry, "address");
		const double* maxPlayers = JSON_FindNumber(serverEntry, "maxPlayers");
		const char* map = JSON_FindString(serverEntry, "map");
		const char* parameters = JSON_FindString(serverEntry, "parameters");
		const double* gameId = JSON_FindNumber(serverEntry, "gameId");
		const double* port = JSON_FindNumber(serverEntry, "port");
		const char* timestamp = JSON_FindString(serverEntry, "timestamp");
		const char* lastQuery = JSON_FindString(serverEntry, "lastQuery");
		char displayName[256];
		char displayMap[64];

		const jsonentry_t* playersArray = JSON_Find(serverEntry, "players", JSON_ARRAY);

		int numPlayers = 0;
		char playerNames[512] = "";
		int playerNamesLen = 0;
		if (playersArray)
		{
			const jsonentry_t* playerEntry;
			for (playerEntry = playersArray->firstchild; playerEntry; playerEntry = playerEntry->next)
			{
				const char* pname = JSON_FindString(playerEntry, "name");
				if (pname && pname[0])
				{
					char playerName[128];
					// trim trailing spaces
					int plen;

					ServerList_CopyDisplayText(pname, playerName, sizeof(playerName));
					plen = (int)strlen(playerName);
					while (plen > 0 && playerName[plen - 1] == ' ')
						plen--;
					if (plen > 0 && playerNamesLen + plen + 2 < (int)sizeof(playerNames))
					{
						if (playerNamesLen > 0)
						{
							playerNames[playerNamesLen++] = ',';
							playerNames[playerNamesLen++] = ' ';
						}
						memcpy(playerNames + playerNamesLen, playerName, plen);
						playerNamesLen += plen;
						playerNames[playerNamesLen] = '\0';
					}
				}
				numPlayers++;
			}
		}

		qboolean status;
		setStatusFlagBasedOnTimestamp(timestamp, lastQuery, &status);

		if (!status || !name || !address || !port || !gameId || (*gameId != 0 && (*gameId != 5 || !parameters || !strstr(parameters, "fte")))) continue; // Skip if essential info is missing or server is down
		ServerList_CopyDisplayText(name, displayName, sizeof(displayName));
		ServerList_CopyDisplayText(map ? map : "Unknown", displayMap, sizeof(displayMap));
		if (!displayName[0])
			q_strlcpy(displayName, "Unknown", sizeof(displayName));
		if (!displayMap[0])
			q_strlcpy(displayMap, "Unknown", sizeof(displayMap));

		servertitem_t* resizedItems = realloc(*items, sizeof(servertitem_t) * (*actualServerCount + 1));
		if (!resizedItems) {
			Con_DPrintf("Memory allocation failed.\n");
			break;
		}
		*items = resizedItems;

                size_t address_len = strlen(address);
                qboolean needs_brackets = false;
                if (address_len > 0)
                {
                        const char* colon = strchr(address, ':');
                        const char* dot = strchr(address, '.');

                        if (address[0] == '[')
                                needs_brackets = false; // already bracketed
                        else if (colon && (!dot || dot > colon))
                                needs_brackets = true; // treat colon-only hosts as IPv6 literals
                }

                size_t addressLength = address_len + 1 /* colon */ + 6 /* max length of port number */ + 1 /* null terminator */;
                if (needs_brackets)
                        addressLength += 2; /* enclosing [] */

                char* addressWithPort = malloc(addressLength);
                if (!addressWithPort) {
                        Con_DPrintf("Memory allocation for address with port failed.\n");
                        break;
                }
		if (needs_brackets)
			q_snprintf(addressWithPort, addressLength, "[%s]:%d", address, (int)*port);
		else
			q_snprintf(addressWithPort, addressLength, "%s:%d", address, (int)*port);

		if (ServerList_IsIgnored(displayName, addressWithPort))
		{
			free(addressWithPort);
			continue;
		}

		servertitem_t* newItem = &(*items)[*actualServerCount];
		memset(newItem, 0, sizeof(*newItem));
		newItem->name = strdup(displayName);
                newItem->ip = strdup(addressWithPort);
                free(addressWithPort);
		newItem->users = numPlayers;
		newItem->maxusers = maxPlayers ? (int)*maxPlayers : 0;
		newItem->map = strdup(displayMap);
		newItem->players = playerNamesLen > 0 ? strdup(playerNames) : NULL;
		newItem->active = true;
		newItem->ping = -1;
		newItem->lastPingTime = 0;
		newItem->isLoading = false;
		newItem->pinned_bookmark = false;
		newItem->pinned_rank = -1;
		newItem->known_available = true;
		newItem->ping_tested = false;

		if (!newItem->name || !newItem->ip || !newItem->map ||
			(playerNamesLen > 0 && !newItem->players))
		{
			ServerList_FreeItem(newItem);
			Con_DPrintf("server list API: memory allocation failed\n");
			continue;
		}

		(*actualServerCount)++;
	}

	JSON_Free(json);
}

static qboolean CurlServerList (servertitem_t** items, int* actualServerCount)
{
	CURL* curl;
	CURLcode res;
	long http_code = 0;
	struct MemoryStruct chunk;

	chunk.memory = malloc(1);  // Initial allocation
	chunk.size = 0;    // No data at this point
	if (!chunk.memory)
	{
		Con_DPrintf("server list API: memory allocation failed\n");
		return false;
	}
	chunk.memory[0] = '\0';

	curl = curl_easy_init();

	if (curl) 
	{
		curl_easy_setopt(curl, CURLOPT_URL, "https://servers.quakeone.com/api/servers/status");
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 2L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 10L);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
		curl_easy_setopt(curl, CURLOPT_USERAGENT, ENGINE_NAME_AND_VER);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
#if CURL_AT_LEAST_VERSION(7, 85, 0)
		curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
		curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
		curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
		curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif

		res = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
		if (res == CURLE_OK && http_code >= 200 && http_code < 300)
			populateServersFromJSON(chunk.memory, items, actualServerCount);
		else if (res != CURLE_OK)
			Con_DPrintf("server list API: %s\n", curl_easy_strerror(res));
		else
			Con_DPrintf("server list API: HTTP %ld\n", http_code);

		free(chunk.memory);
		curl_easy_cleanup(curl);
		return (res == CURLE_OK && http_code >= 200 && http_code < 300);
	}

	free(chunk.memory);
	Con_DPrintf("server list API: curl init failed\n");
	return false;
}

static int ServerList_ApiFetchThread(void* unused)
{
	servertitem_t* items = NULL;
	int count = 0;
	qboolean ok;

	(void)unused;

	ok = CurlServerList(&items, &count);

	ServerList_ApiEnsureMutex();
	if (!serverlistapi.mutex)
	{
		ServerList_FreeItems(items, count);
		return 0;
	}

	SDL_LockMutex(serverlistapi.mutex);
	ServerList_FreeItems(serverlistapi.items, serverlistapi.count);
	if (ok)
	{
		serverlistapi.items = items;
		serverlistapi.count = count;
		serverlistapi.state = SERVERLIST_API_READY;
	}
	else
	{
		ServerList_FreeItems(items, count);
		serverlistapi.items = NULL;
		serverlistapi.count = 0;
		serverlistapi.state = SERVERLIST_API_ERROR;
	}
	SDL_UnlockMutex(serverlistapi.mutex);

	return 0;
}

static void ServerList_StartApiFetch(void)
{
	SDL_Thread* thread;

	ServerList_ApiReapThread(false);
	ServerList_ApiEnsureMutex();
	if (!serverlistapi.mutex)
		return;

	SDL_LockMutex(serverlistapi.mutex);
	if (serverlistapi.state == SERVERLIST_API_LOADING)
	{
		SDL_UnlockMutex(serverlistapi.mutex);
		return;
	}

	ServerList_FreeItems(serverlistapi.items, serverlistapi.count);
	serverlistapi.items = NULL;
	serverlistapi.count = 0;
	serverlistapi.state = SERVERLIST_API_LOADING;
	SDL_UnlockMutex(serverlistapi.mutex);

	thread = SDL_CreateThread(ServerList_ApiFetchThread, "ServerListApiThread", NULL);
	if (!thread)
	{
		ServerList_ApiEnsureMutex();
		SDL_LockMutex(serverlistapi.mutex);
		serverlistapi.state = SERVERLIST_API_ERROR;
		SDL_UnlockMutex(serverlistapi.mutex);
		Con_DPrintf("server list API: failed to create fetch thread: %s\n", SDL_GetError());
		return;
	}

	SDL_LockMutex(serverlistapi.mutex);
	serverlistapi.thread = thread;
	SDL_UnlockMutex(serverlistapi.mutex);
}

static qboolean ServerList_TakeApiResults(servertitem_t** items, int* count)
{
	if (items)
		*items = NULL;
	if (count)
		*count = 0;

	ServerList_ApiEnsureMutex();
	if (!serverlistapi.mutex)
		return false;

	SDL_LockMutex(serverlistapi.mutex);
	if (serverlistapi.state != SERVERLIST_API_READY)
	{
		SDL_UnlockMutex(serverlistapi.mutex);
		return false;
	}

	if (items)
		*items = serverlistapi.items;
	if (count)
		*count = serverlistapi.count;
	serverlistapi.items = NULL;
	serverlistapi.count = 0;
	serverlistapi.state = SERVERLIST_API_IDLE;
	SDL_UnlockMutex(serverlistapi.mutex);
	ServerList_ApiReapThread(false);

	return true;
}

static qboolean ServerList_ApiFetchHasPendingOrResults(void)
{
	qboolean result = false;

	ServerList_ApiEnsureMutex();
	if (!serverlistapi.mutex)
		return false;

	SDL_LockMutex(serverlistapi.mutex);
	result = (serverlistapi.state == SERVERLIST_API_LOADING ||
		(serverlistapi.state == SERVERLIST_API_READY && serverlistapi.count > 0));
	SDL_UnlockMutex(serverlistapi.mutex);

	return result;
}

static qboolean ServerList_ApiFetchIsLoading(void)
{
	qboolean result = false;

	ServerList_ApiEnsureMutex();
	if (!serverlistapi.mutex)
		return false;

	SDL_LockMutex(serverlistapi.mutex);
	result = (serverlistapi.state == SERVERLIST_API_LOADING);
	SDL_UnlockMutex(serverlistapi.mutex);

	return result;
}

static void ServerList_ApplyApiResults(void)
{
	servertitem_t* apiItems = NULL;
	int apiCount = 0;
	int added = 0;
	qboolean locked = false;

	if (searchLastScope != SLIST_INTERNET ||
		!ServerList_TakeApiResults(&apiItems, &apiCount))
		return;

	if (apiCount <= 0)
	{
		ServerList_FreeItems(apiItems, apiCount);
		return;
	}

	if (pingMutex)
	{
		SDL_LockMutex(pingMutex);
		locked = true;
	}

	for (int i = 0; i < apiCount; i++)
	{
		int changedIndex;

		if (ServerList_AppendOrReplaceMovedItem(&serversmenu.items, &serversmenu.servercount, &apiItems[i], &changedIndex))
		{
			if (changedIndex >= 0 && changedIndex < serversmenu.pinged_count)
				serversmenu.pinged_count = changedIndex;
			added++;
		}
	}

	if (added > 0)
		ServerList_RebuildOrderAndFilter();

	if (locked)
		SDL_UnlockMutex(pingMutex);

	if (added > 0)
		TriggerServerPing(serversmenu.list.cursor);

	ServerList_FreeItems(apiItems, apiCount);
}

static int CompareServers(const void* a, const void* b)
{
        int indexA = *(const int*)a;
        int indexB = *(const int*)b;
        const servertitem_t* serverA = &serversmenu.items[indexA];
        const servertitem_t* serverB = &serversmenu.items[indexB];
	int pinA = ServerList_PinnedRank(serverA);
	int pinB = ServerList_PinnedRank(serverB);
	int groupA = ServerList_SortGroup(serverA);
	int groupB = ServerList_SortGroup(serverB);
	int res = 0;

	if (groupA != groupB)
		return groupA - groupB;

	if (pinA >= 0 && pinB >= 0)
	{
		qboolean unavailableA = ServerList_IsUnavailableBookmark(serverA);
		qboolean unavailableB = ServerList_IsUnavailableBookmark(serverB);

		if (unavailableA != unavailableB)
			return unavailableA ? 1 : -1;
	}

	if (pinA >= 0 && pinB >= 0 && pinA != pinB)
		return pinA - pinB;

	switch (serversmenu.sort_mode) {
	case SORT_NAME:
		res = q_strcasecmp(serverA->name, serverB->name);
		break;
	case SORT_MAP:
		res = q_strcasecmp(serverA->map, serverB->map);
		break;
	case SORT_USERS:
		res = serverA->users - serverB->users;
		break;
	case SORT_PING:
		{
        int pingA = (serverA->ping >= 0) ? serverA->ping : INT_MAX;
        int pingB = (serverB->ping >= 0) ? serverB->ping : INT_MAX;
			res = pingA - pingB;
		}
		break;
	}

	if (res == 0) {
		if (serversmenu.sort_mode != SORT_PING) {
			int pingA = (serverA->ping >= 0) ? serverA->ping : INT_MAX;
			int pingB = (serverB->ping >= 0) ? serverB->ping : INT_MAX;
        if (pingA != pingB)
                return pingA - pingB;
		}
        return q_strcasecmp(serverA->name, serverB->name);
}

	return serversmenu.sort_descending ? -res : res;
}

static int ServerList_DisplayPinnedCount(void)
{
	int count = 0;

	for (int i = 0; i < serversmenu.list.numitems; ++i)
	{
		int actualIndex = ServersMenu_ResolveIndex(i);
		if (actualIndex < 0 || actualIndex >= serversmenu.servercount ||
			ServerList_PinnedRank(&serversmenu.items[actualIndex]) < 0)
			break;
		count++;
	}

	return count;
}

static qboolean ServerList_HasPinnedSeparator(int pinnedCount)
{
	return pinnedCount > 0 && pinnedCount < serversmenu.list.numitems;
}

static int ServerList_RowCapacity(void)
{
	int rows = serversmenu.list.viewsize;

	if (serversmenu.list.search.len > 0 && rows > 13)
		rows = 13;

	return q_max(rows, 1);
}

static void ServerList_GetVisibleLayout(int* rows, int* itemViewsize,
	int* pinnedCount, qboolean* separatorVisible)
{
	int row_capacity = ServerList_RowCapacity();
	int pinned_count = ServerList_DisplayPinnedCount();
	qboolean show_separator;

	show_separator = ServerList_HasPinnedSeparator(pinned_count) &&
		serversmenu.list.scroll < pinned_count &&
		pinned_count < serversmenu.list.scroll + row_capacity;

	if (rows)
		*rows = row_capacity;
	if (itemViewsize)
		*itemViewsize = q_max(row_capacity - (show_separator ? 1 : 0), 1);
	if (pinnedCount)
		*pinnedCount = pinned_count;
	if (separatorVisible)
		*separatorVisible = show_separator;
}

static void ServerList_ClampScroll(void)
{
	/* The separator consumes a row only while it is visible. A scrollbar drag
	 * can cross that boundary, so clamp once for each possible layout state. */
	for (int pass = 0; pass < 2; ++pass)
	{
		int item_viewsize;
		int max_scroll;

		ServerList_GetVisibleLayout(NULL, &item_viewsize, NULL, NULL);
		max_scroll = q_max(serversmenu.list.numitems - item_viewsize, 0);
		serversmenu.list.scroll = CLAMP(0, serversmenu.list.scroll, max_scroll);
	}
}

static void ServerList_Rescroll(void)
{
	int item_viewsize;
	int saved_viewsize = serversmenu.list.viewsize;

	ServerList_GetVisibleLayout(NULL, &item_viewsize, NULL, NULL);
	serversmenu.list.viewsize = item_viewsize;
	M_List_Rescroll(&serversmenu.list);
	serversmenu.list.viewsize = saved_viewsize;
	ServerList_ClampScroll();
}

static void ServerList_CenterCursor(void)
{
	int pinned_count = ServerList_DisplayPinnedCount();
	int item_viewsize = ServerList_RowCapacity();
	int saved_viewsize = serversmenu.list.viewsize;

	/* Use the conservative capacity so centering cannot leave the cursor in
	 * the row occupied by the pinned/unpinned separator. */
	if (ServerList_HasPinnedSeparator(pinned_count))
		item_viewsize = q_max(item_viewsize - 1, 1);

	serversmenu.list.viewsize = item_viewsize;
	M_List_CenterCursor(&serversmenu.list);
	serversmenu.list.viewsize = saved_viewsize;
	ServerList_ClampScroll();
}

static qboolean ServerList_MouseOverPinnedSeparator(int yrel,
	int rows, int pinnedCount, qboolean separatorVisible)
{
	int separator_row;

	if (!separatorVisible || yrel < 0 || yrel >= rows * 8)
		return false;

	separator_row = pinnedCount - serversmenu.list.scroll;
	return yrel >= separator_row * 8 && yrel < (separator_row + 1) * 8;
}

static qboolean ServerList_ListKey(int key)
{
	int item_viewsize;
	int saved_viewsize = serversmenu.list.viewsize;
	qboolean handled;

	ServerList_GetVisibleLayout(NULL, &item_viewsize, NULL, NULL);

	serversmenu.list.viewsize = item_viewsize;
	handled = M_List_Key(&serversmenu.list, key);
	serversmenu.list.viewsize = saved_viewsize;
	ServerList_ClampScroll();

	return handled;
}

static qboolean ServerList_ListCycleMatch(int key)
{
	int item_viewsize;
	int saved_viewsize = serversmenu.list.viewsize;
	qboolean handled;

	ServerList_GetVisibleLayout(NULL, &item_viewsize, NULL, NULL);

	serversmenu.list.viewsize = item_viewsize;
	handled = M_List_CycleMatch(&serversmenu.list, key, M_Servers_Match);
	serversmenu.list.viewsize = saved_viewsize;
	ServerList_ClampScroll();

	return handled;
}

static qboolean ServerList_UseScrollbar(int yrel)
{
	int item_viewsize;
	int saved_viewsize = serversmenu.list.viewsize;
	qboolean used;

	ServerList_GetVisibleLayout(NULL, &item_viewsize, NULL, NULL);

	serversmenu.list.viewsize = item_viewsize;
	used = M_List_UseScrollbar(&serversmenu.list, yrel);
	serversmenu.list.viewsize = saved_viewsize;
	ServerList_ClampScroll();

	return used;
}

static void ServerList_MousemoveList(int yrel)
{
	int rows, item_viewsize, pinned_count;
	qboolean separator_visible;
	int saved_viewsize = serversmenu.list.viewsize;

	ServerList_GetVisibleLayout(&rows, &item_viewsize, &pinned_count, &separator_visible);

	if (ServerList_MouseOverPinnedSeparator(yrel, rows, pinned_count, separator_visible))
		return;

	if (separator_visible)
	{
		int separator_row = pinned_count - serversmenu.list.scroll;
		if (yrel >= (separator_row + 1) * 8)
			yrel -= 8;
	}

	serversmenu.list.viewsize = item_viewsize;
	M_List_Mousemove(&serversmenu.list, yrel);
	serversmenu.list.viewsize = saved_viewsize;
}

static int ServersMenu_ResolveIndex(int displayIndex)
{
	// When search is active, use filtered_indices
	if (serversmenu.list.search.len > 0 && serversmenu.filtered_indices)
	{
		if (displayIndex < 0 || displayIndex >= VEC_SIZE(serversmenu.filtered_indices))
			return -1;
		int sortedIndex = serversmenu.filtered_indices[displayIndex];
		if (!serversmenu.order)
			return sortedIndex;
		return serversmenu.order[sortedIndex];
	}

        if (displayIndex < 0 || displayIndex >= serversmenu.servercount)
                return -1;

        if (!serversmenu.order)
                return displayIndex;

        return serversmenu.order[displayIndex];
}

static void M_ServerList_RefilterWithSelection(int selectedActual)
{
    int i;
    int selectedDisplay = -1;

    VEC_CLEAR(serversmenu.filtered_indices);

    for (i = 0; i < serversmenu.servercount; i++)
    {
        int actual_idx = serversmenu.order ? serversmenu.order[i] : i;
        if (actual_idx < 0 || actual_idx >= serversmenu.servercount)
            continue;

        const servertitem_t* server = &serversmenu.items[actual_idx];
        
        if (serversmenu.list.search.len == 0 ||
            q_strcasestr(server->name, serversmenu.list.search.text) ||
            q_strcasestr(server->map, serversmenu.list.search.text) ||
            q_strcasestr(server->ip, serversmenu.list.search.text))
        {
            if (actual_idx == selectedActual)
                selectedDisplay = (int)VEC_SIZE(serversmenu.filtered_indices);
            VEC_PUSH(serversmenu.filtered_indices, i);
        }
    }

    serversmenu.list.numitems = (int)VEC_SIZE(serversmenu.filtered_indices);

    if (selectedDisplay >= 0)
        serversmenu.list.cursor = selectedDisplay;
    else if (serversmenu.list.cursor >= serversmenu.list.numitems)
        serversmenu.list.cursor = serversmenu.list.numitems - 1;

    if (serversmenu.list.cursor < 0 && serversmenu.list.numitems > 0)
        serversmenu.list.cursor = 0;

    ServerList_CenterCursor();
}

static void M_ServerList_Refilter(void)
{
    M_ServerList_RefilterWithSelection(ServersMenu_ResolveIndex(serversmenu.list.cursor));
}

static void SortServersWithSelection(qboolean lockMutex, int selectedActual)
{
        qboolean locked = false;

        if (lockMutex && pingMutex)
        {
                SDL_LockMutex(pingMutex);
                locked = true;
        }

        if (!serversmenu.items || !serversmenu.order)
        {
                serversmenu.pingSortDirty = false;
                if (locked)
                        SDL_UnlockMutex(pingMutex);
                return;
        }

        if (serversmenu.servercount >= 2)
		qsort(serversmenu.order, serversmenu.servercount, sizeof(serversmenu.order[0]), CompareServers);

        if (selectedActual >= 0)
        {
                for (int i = 0; i < serversmenu.servercount; ++i)
                {
                        if (serversmenu.order[i] == selectedActual)
                        {
                                serversmenu.list.cursor = i;
                                break;
                        }
                }
        }
        else if (serversmenu.servercount > 0 &&
                (serversmenu.list.cursor < 0 || serversmenu.list.cursor >= serversmenu.servercount))
        {
                serversmenu.list.cursor = CLAMP(0, serversmenu.list.cursor, serversmenu.servercount - 1);
        }

        if (serversmenu.list.search.len > 0)
                M_ServerList_RefilterWithSelection(selectedActual);
        else if (serversmenu.list.viewsize > 0)
                ServerList_Rescroll();

        serversmenu.pingSortDirty = false;

        if (locked)
                SDL_UnlockMutex(pingMutex);
}

static void SortServers(qboolean lockMutex)
{
        SortServersWithSelection(lockMutex, ServersMenu_ResolveIndex(serversmenu.list.cursor));
}

void RemoveDuplicateServers (servertitem_t** items, int* actualServerCount) 
{
	int writeIndex = 0;
	for (int i = 0; i < *actualServerCount; i++)
	{
		int duplicateIndex = ServerList_FindDuplicateItem(*items, writeIndex, &(*items)[i]);

		if (duplicateIndex >= 0)
		{
			if ((*items)[duplicateIndex].pinned_bookmark && !(*items)[i].pinned_bookmark)
			{
				ServerList_MergeLiveDetailsIntoPinned(&(*items)[duplicateIndex], &(*items)[i]);
				ServerList_FreeItem(&(*items)[i]);
			}
			else if (ServerList_ShouldReplaceDuplicate(&(*items)[duplicateIndex], &(*items)[i]))
			{
				ServerList_FreeItem(&(*items)[duplicateIndex]);
				ServerList_MoveItem(&(*items)[duplicateIndex], &(*items)[i]);
			}
			else
			{
				ServerList_FreeItem(&(*items)[i]);
			}
			continue;
		}

		if (writeIndex != i)
		{
			ServerList_FreeItem(&(*items)[writeIndex]);
			ServerList_MoveItem(&(*items)[writeIndex], &(*items)[i]);
		}
		writeIndex++;
	}
	*actualServerCount = writeIndex;
}

void FetchAndSortServers (void) 
{
        servertitem_t* apiItems = NULL;
        int apiCount = 0;
        ServerList_FreeItems(serversmenu.items, serversmenu.servercount);
        serversmenu.items = NULL;
        free(serversmenu.order);
        serversmenu.order = NULL;
        int actualServerCount = 0;
	pinnedbookmark_t pinned[MAX_PINNED_BOOKMARKS];
	int pinned_count = (searchLastScope == SLIST_INTERNET) ?
		M_Bookmarks_GetPinned(pinned, MAX_PINNED_BOOKMARKS) : 0;

	for (int i = 0; i < pinned_count; ++i)
	{
		servertitem_t item;

		if (ServerList_CreatePinnedBookmarkItem(&pinned[i], i, &item) &&
			!ServerList_AppendMovedItem(&serversmenu.items, &actualServerCount, &item))
			ServerList_FreeItem(&item);
	}

	for (size_t i = 0; i < hostCacheCount; i++) // Fetch and add servers from the dp list
	{
		servertitem_t item;

		if (ServerList_CreateHostCacheItem(i, &item) &&
			!ServerList_AppendOrReplaceMovedItem(&serversmenu.items, &actualServerCount, &item, NULL))
			ServerList_FreeItem(&item);
	}
	serversmenu.hostcache_copied = hostCacheCount;

	if (searchLastScope == SLIST_INTERNET && ServerList_TakeApiResults(&apiItems, &apiCount))
	{
		for (int i = 0; i < apiCount; i++)
			ServerList_AppendOrReplaceMovedItem(&serversmenu.items, &actualServerCount, &apiItems[i], NULL);
		ServerList_FreeItems(apiItems, apiCount);
	}

	RemoveDuplicateServers(&serversmenu.items, &actualServerCount);

        serversmenu.servercount = actualServerCount;
	ServerList_RebuildOrderAndFilter();

        if (serversmenu.list.cursor >= actualServerCount)
                serversmenu.list.cursor = actualServerCount > 0 ? actualServerCount - 1 : 0;
	if (serversmenu.slist_first > serversmenu.list.cursor)
		serversmenu.slist_first = serversmenu.list.cursor;
}

static void ServerList_AppendHostCacheResults(void)
{
	int added = 0;
	qboolean locked = false;

	if (!hostCacheCount)
		return;
	if (serversmenu.hostcache_copied > hostCacheCount)
		serversmenu.hostcache_copied = 0;
	if (serversmenu.hostcache_copied == hostCacheCount)
		return;

	if (pingMutex)
	{
		SDL_LockMutex(pingMutex);
		locked = true;
	}

	for (size_t i = serversmenu.hostcache_copied; i < hostCacheCount; i++)
	{
		servertitem_t item;
		int changedIndex;

		if (!ServerList_CreateHostCacheItem(i, &item))
			continue;

		if (!ServerList_AppendOrReplaceMovedItem(&serversmenu.items, &serversmenu.servercount, &item, &changedIndex))
		{
			ServerList_FreeItem(&item);
			continue;
		}

		if (changedIndex >= 0 && changedIndex < serversmenu.pinged_count)
			serversmenu.pinged_count = changedIndex;
		added++;
	}
	serversmenu.hostcache_copied = hostCacheCount;

	if (added > 0)
		ServerList_RebuildOrderAndFilter();

	if (locked)
		SDL_UnlockMutex(pingMutex);

	if (added > 0)
		TriggerServerPing(serversmenu.list.cursor);
}

void M_Menu_ServerList_f (void)
{
	CleanupPingThreads();

	key_dest = key_menu;
	m_state = m_slist;
	IN_UpdateGrabs();
	m_entersound = true;

	serversmenu.list.cursor = -1;
	serversmenu.list.scroll = 0;
	serversmenu.list.numitems = 0;
	serversmenu.scrollbar_grab = false;
	serversmenu.initialPingComplete = false;
	serversmenu.initialPingThreadsRemaining = 0;
	serversmenu.pingQueueSize = 0;
	serversmenu.pingThreadRunning = false;
	pingThreadsShouldExit = false;
	serversmenu.list.viewsize = MAX_VIS_SERVERS;
	memset(&serversmenu.list.search, 0, sizeof(serversmenu.list.search));
	serversmenu.list.search.maxlen = 32;
	VEC_CLEAR(serversmenu.filtered_indices);

	FetchAndSortServers();
	InitializePingMutex();
	PingAllServers();

	M_Ticker_Init(&serversmenu.ticker);

	ServerList_CenterCursor();
}

void M_ServerList_Draw (void)
{
	int x, y, i, cols;
	int firstvis, numvis;
	int list_rows, item_viewsize, pinned_count;
	qboolean separator_visible;
	const char* title;
	qboolean loading;

	x = 16;
	y = 36;
	cols = 36;
	loading = (searchLastScope == SLIST_INTERNET &&
		(slistInProgress || ServerList_ApiFetchIsLoading()));

	switch (searchLastScope)
	{
	case SLIST_INTERNET:
		title = "Public Servers";
		break;
	case SLIST_LAN:
		title = "Local Servers";
		break;
	default:
		title = "Servers";
		break;
	}

        serversmenu.x = x;
        serversmenu.y = y;
        serversmenu.cols = cols;

	if (searchLastScope == SLIST_INTERNET)
		ServerList_AppendHostCacheResults();
	ServerList_ApplyApiResults();
	ServerList_StartPendingPingSweep();

	if (serversmenu.pingSortDirty)
		SortServers(true);
	ServerList_ClampScroll();

        if (!keydown[K_MOUSE1])
                serversmenu.scrollbar_grab = false;

	if (serversmenu.prev_cursor != serversmenu.list.cursor) {
		serversmenu.prev_cursor = serversmenu.list.cursor;
		M_Ticker_Init(&serversmenu.ticker);
	}
	else {
		M_Ticker_Update(&serversmenu.ticker);
	}

	M_DrawCountHeader(x, y - 36, cols, title,
		serversmenu.servercount, "server", "servers");
	M_DrawQuakeBar(x - 8, y - 24, cols + 2);
	// Header drawing
	int header_y = y - 16;
	const char *hdr_name = "Name";
	const char *hdr_map = "Map";
	const char *hdr_users = "Plys";
	const char *hdr_ping = "Ping";
	
	if (serversmenu.sort_mode == SORT_NAME) M_PrintWhite(x, header_y, hdr_name);
	else M_Print(x, header_y, hdr_name);

	if (serversmenu.sort_mode == SORT_MAP) M_PrintWhite(x + 18 * 8, header_y, hdr_map);
	else M_Print(x + 18 * 8, header_y, hdr_map);
	
	if (serversmenu.sort_mode == SORT_USERS) M_PrintWhite(x + 25 * 8, header_y, hdr_users);
	else M_Print(x + 25 * 8, header_y, hdr_users);
	
	if (serversmenu.sort_mode == SORT_PING) M_PrintWhite(x + 31 * 8, header_y, hdr_ping);
	else M_Print(x + 31 * 8, header_y, hdr_ping);

        int saved_viewsize = serversmenu.list.viewsize;
	ServerList_GetVisibleLayout(&list_rows, &item_viewsize, &pinned_count, &separator_visible);
	serversmenu.list.viewsize = item_viewsize;

        M_List_GetVisibleRange(&serversmenu.list, &firstvis, &numvis);
	if (numvis <= 0)
	{
		if (loading)
			M_PrintWhite(x, y, "Loading public servers...");
		else
			M_PrintWhite(x, y, "No Quake servers found");
	}
	        for (i = 0; i < numvis; i++) {
	                int idx = i + firstvis;
			int row = i;
	                qboolean selected = (idx == serversmenu.list.cursor);
	                int actualIndex = ServersMenu_ResolveIndex(idx);
	                servertitem_snapshot_t server;

	                if (!ServerList_SnapshotItem(actualIndex, &server))
	                        continue;

	                qboolean isActive = false;

	                if (cls.state == ca_connected) // highlight if connected to a server in the list
	                {
	                        if (ServerList_AddressMatches(lastmphost, server.ip))
	                                isActive = true;
	                        else if (Valid_Domain(lastmphost))
	                                isActive = ServerList_AddressMatches(ResolveHostname(lastmphost), server.ip);
	                        else if (Valid_IP(lastmphost))
	                                isActive = ServerList_AddressMatches(lastmphost, server.ip);
	                }

	                char pingStrBuffer[8];
	                char* pingStrToPrint = pingStrBuffer;

	                if (server.ping == -1) {
	                        pingStrBuffer[0] = '\0';
	                }
	                else {
	                        q_snprintf(pingStrBuffer, sizeof(pingStrBuffer), "%3i", server.ping);
	                        while (*pingStrToPrint == ' ' && *pingStrToPrint != '\0') {
	                                pingStrToPrint++;
	                        }
	                }

			char plysStr[16];
			if (server.unavailable_bookmark)
				plysStr[0] = '\0';  // unreachable bookmark has no live player count
			else
				q_snprintf(plysStr, sizeof(plysStr), "%d/%d", server.users, server.maxusers);

	                char linePrefixStr[32];
			q_snprintf(linePrefixStr, sizeof(linePrefixStr), "%-16.16s  %-6.6s %-5s ",
	                        server.name,
	                        server.map,
				plysStr);

		if (separator_visible && idx >= pinned_count)
			row++;

                int current_y_pos = y + row * 8;
                int current_x_pos = x;

                if (server.unavailable_bookmark) {
			M_PrintRGBA(current_x_pos, current_y_pos, linePrefixStr,
				CL_PLColours_Parse("0xffffff"), SERVERLIST_UNAVAILABLE_ALPHA, true);
                }
                else if (serversmenu.list.search.len > 0) {
                        M_PrintHighlight(current_x_pos, current_y_pos, linePrefixStr,
                                serversmenu.list.search.text, serversmenu.list.search.len);
                }
                else if (isActive) {
                        M_PrintWhite(current_x_pos, current_y_pos, linePrefixStr);
                }
                else {
                        M_Print(current_x_pos, current_y_pos, linePrefixStr);
                }

	                int ping_display_x = current_x_pos + ((int)strlen(linePrefixStr) * 8);

	                if (pingStrToPrint[0] != '\0') {
	                        int current_ping = server.ping;
	                        if (current_ping <= 60) {
	                                M_PrintWhite(ping_display_x, current_y_pos, pingStrToPrint); // Green for pings <= 60
	                        }
                        else if (current_ping <= 120) {
                                M_Print2(ping_display_x, current_y_pos, pingStrToPrint); // White for pings 61-120
                        }
                        else { // Pings > 120
                                if (isActive) { // Active servers with high ping remain white
                                        M_PrintWhite(ping_display_x, current_y_pos, pingStrToPrint);
                                }
                                else { // Inactive servers with high ping use default M_Print color
                                        M_Print(ping_display_x, current_y_pos, pingStrToPrint);
                                }
                        }
                }

                if (selected)
                        M_DrawCharacter(x - 8, current_y_pos, 12 + ((int)(realtime * 4) & 1));

                if (selected)
                {
                        int info_y = y + list_rows * 8 + 12;
                        int plys_text_x = x + 25 * 8;
                        int plys_text_w = (int)strlen(plysStr) * 8;
                        qboolean hover_plys = (m_mousex >= plys_text_x &&
                                m_mousex < plys_text_x + plys_text_w &&
                                m_mousey >= current_y_pos &&
                                m_mousey < current_y_pos + 8);
	                        qboolean tab_held = keydown[K_TAB];

	                        if ((hover_plys || tab_held) && server.has_players)
	                        {
	                                // Display player names with word-wrapping (like demos menu)
	                                char players_copy[512];
	                                q_strlcpy(players_copy, server.players, sizeof(players_copy));

                                char *pos = players_copy;
                                char line_buffer[64];
                                int line_pos = 0;
                                int line_count = 0;
                                int current_y = info_y;

                                while (*pos)
                                {
                                        char *next_comma = strchr(pos, ',');
                                        int name_len;

                                        if (next_comma)
                                        {
                                                name_len = (int)(next_comma - pos);
                                                while (next_comma[1] == ' ') next_comma++;
                                        }
                                        else
                                        {
                                                name_len = (int)strlen(pos);
                                        }

                                        if (name_len > 40)
                                                name_len = 40;

                                        int comma_space = next_comma ? 2 : 0;
                                        int needed_space = name_len + comma_space;

                                        if (line_pos > 0 && line_pos + needed_space > 40)
                                        {
                                                if (line_count >= 2)
                                                {
                                                        if (line_pos + 4 <= 40)
                                                        {
                                                                line_buffer[line_pos++] = ' ';
                                                                line_buffer[line_pos++] = '.';
                                                                line_buffer[line_pos++] = '.';
                                                                line_buffer[line_pos++] = '.';
                                                        }
                                                        line_buffer[line_pos] = '\0';
                                                        M_PrintWhite(x, current_y, line_buffer);
                                                        break;
                                                }

                                                line_buffer[line_pos] = '\0';
                                                M_PrintWhite(x, current_y, line_buffer);
                                                current_y += 8;
                                                line_count++;
                                                line_pos = 0;
                                        }

                                        memcpy(line_buffer + line_pos, pos, name_len);
                                        line_pos += name_len;

                                        if (next_comma)
                                        {
                                                line_buffer[line_pos++] = ',';
                                                line_buffer[line_pos++] = ' ';
                                                pos = next_comma + 1;
                                        }
                                        else
                                                break;
                                }

                                if (line_pos > 0 && line_count < 3)
                                {
                                        line_buffer[line_pos] = '\0';
                                        M_PrintWhite(x, current_y, line_buffer);
                                }
                        }
                        else
	                        {
	                                char infoStr[40];
	                                q_snprintf(infoStr, sizeof(infoStr), "%-34.34s", server.name);
					if (server.unavailable_bookmark)
						M_PrintRGBA(x, info_y, infoStr, CL_PLColours_Parse("0xffffff"),
							SERVERLIST_UNAVAILABLE_ALPHA, false);
					else
						M_PrintWhite(x, info_y, infoStr);
	                                q_snprintf(infoStr, sizeof(infoStr), "%-34.34s", server.ip);
					if (server.unavailable_bookmark)
						M_PrintRGBA(x, info_y + 8, infoStr, CL_PLColours_Parse("0xffffff"),
							SERVERLIST_UNAVAILABLE_ALPHA, false);
					else
						M_PrintWhite(x, info_y + 8, infoStr);
	                        }
                }
        }

	if (M_List_GetOverflow(&serversmenu.list) > 0) {
		M_List_DrawScrollbar(&serversmenu.list, x + cols * 8 - 8, y);

		if (serversmenu.list.scroll > 0)
			M_DrawEllipsisBar(x, y - 8, cols);
		if (serversmenu.list.scroll + serversmenu.list.viewsize < serversmenu.list.numitems)
			M_DrawEllipsisBar(x, y + list_rows * 8, cols);
	}

	// Restore viewsize
	serversmenu.list.viewsize = saved_viewsize;

	// Draw search box
	if (serversmenu.list.search.len > 0)
	{
		M_DrawTextBox(16, 180, 32, 1);
		M_PrintHighlight(24, 188, serversmenu.list.search.text,
			serversmenu.list.search.text,
			serversmenu.list.search.len);
		int cursor_x = 24 + 8 * serversmenu.list.search.len;
		if (serversmenu.list.numitems == 0)
			M_DrawCharacter(cursor_x, 188, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, 188, 10 + ((int)(realtime * 4) & 1));
	}
}

qboolean M_Servers_Match(int index, char initial)
{
        int actualIndex = ServersMenu_ResolveIndex(index);
        if (actualIndex < 0)
                return false;

        const char* name = serversmenu.items[actualIndex].name;
        if (!name || !name[0])
                return false;

        return q_tolower(name[0]) == initial;
}

static void CleanupPingThreads(void)
{
	SDL_Thread* queueThread = NULL;

	if (!pingMutex)
	{
		pingThreadsShouldExit = false;
		serversmenu.pingThread = NULL;
		serversmenu.pingThreadRunning = false;
		serversmenu.pingQueueSize = 0;
		return;
	}

	WaitForPingThreads();

	pingThreadsShouldExit = true;
	if (serversmenu.pingThread)
	{
		queueThread = serversmenu.pingThread;
		serversmenu.pingThread = NULL;
	}
	if (queueThread)
		SDL_WaitThread(queueThread, NULL);

	SDL_LockMutex(pingMutex);
	serversmenu.pingQueueSize = 0;
	serversmenu.pingThreadRunning = false;
	SDL_UnlockMutex(pingMutex);

	CleanupPingMutex();
	pingThreadsShouldExit = false;
}

void M_ServerList_ShutdownPingThreads(void)
{
	CleanupPingThreads();
}

void M_ServerList_Key(int key)
{

	int x, y; // woods #mousemenu
	int prev_cursor = serversmenu.list.cursor;

	
	// Handle Ctrl+U or Ctrl+Backspace first
	if (keydown[K_CTRL])
	{
		if ((key == 'u' || key == 'U') && serversmenu.list.search.len > 0)
		{
			serversmenu.list.search.len = 0;
			serversmenu.list.search.text[0] = 0;
			M_ServerList_Refilter();
			return;
		}
		else if (key == K_BACKSPACE && serversmenu.list.search.len > 0)
		{
			M_DeletePrevWord(&serversmenu.list.search);
			M_ServerList_Refilter();
			return;
		}
	}

	// Handle search input - printable characters
	if (key >= 32 && key < 127)
	{
		if (serversmenu.list.search.len < serversmenu.list.search.maxlen)
		{
			serversmenu.list.search.text[serversmenu.list.search.len++] = key;
			serversmenu.list.search.text[serversmenu.list.search.len] = 0;
			M_ServerList_Refilter();
			return;
		}
	}

	// Handle backspace for search
	if (key == K_BACKSPACE)
	{
		if (serversmenu.list.search.len > 0)
		{
			serversmenu.list.search.text[--serversmenu.list.search.len] = 0;
			M_ServerList_Refilter();
			return;
		}
	}

	if (serversmenu.scrollbar_grab)
	{
		switch (key)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			serversmenu.scrollbar_grab = false;
			break;
		}
		return;
	}

	if (serversmenu.list.numitems > 0 && ServerList_ListKey(key))
	{
		if (serversmenu.list.cursor != prev_cursor)
			TriggerServerPing(serversmenu.list.cursor);
	
		return;
	}

		if (serversmenu.list.numitems > 0 && ServerList_ListCycleMatch(key))
		{
			if (serversmenu.list.cursor != prev_cursor)
				TriggerServerPing(serversmenu.list.cursor);
			return;
		}

	if (M_Ticker_Key(&serversmenu.ticker, key))
		return;

	switch (key)
	{
	case K_ESCAPE:
		if (serversmenu.list.search.len > 0)
		{
			serversmenu.list.search.len = 0;
			serversmenu.list.search.text[0] = 0;
			M_ServerList_Refilter();
			return;
		}
		// Fall through to exit menu if search is already empty
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		CleanupPingThreads();
		M_Menu_LanConfig_f();
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	enter:
	{
		int actualIndex = ServersMenu_ResolveIndex(serversmenu.list.cursor);
		servertitem_snapshot_t server;

		if (actualIndex < 0 || actualIndex >= serversmenu.servercount ||
			!ServerList_SnapshotItem(actualIndex, &server) ||
			server.unavailable_bookmark)
		{
			break;
		}

		m_return_state = m_state;
		m_return_onerror = true;
		key_dest = key_game;
		m_state = m_none;
		IN_UpdateGrabs();
		CL_MarkNextConnectFromMenu();
		Cbuf_AddText(va("connect \"%s\"\n", serversmenu.items[actualIndex].ip));
		CleanupPingThreads();
	}
	break;

	case K_MOUSE1: // woods #mousemenu
{
		// Check header click
		int hx = m_mousex - serversmenu.x;
		int hy = m_mousey - (serversmenu.y - 16);
		if (hy >= 0 && hy < 8) {
			int new_sort = -1;
			if (hx >= 0 && hx < 16 * 8) new_sort = SORT_NAME;
			else if (hx >= 18 * 8 && hx < (18 + 6) * 8) new_sort = SORT_MAP;
			else if (hx >= 25 * 8 && hx < (25 + 5) * 8) new_sort = SORT_USERS;
			else if (hx >= 32 * 8 && hx < (32 + 4) * 8) new_sort = SORT_PING;
			
			if (new_sort != -1) {
				if (serversmenu.sort_mode == new_sort) {
					serversmenu.sort_descending = !serversmenu.sort_descending;
				} else {
					serversmenu.sort_mode = new_sort;
                    // Default sort directions
                    if (new_sort == SORT_USERS) serversmenu.sort_descending = true;
                    else serversmenu.sort_descending = false;
				}
				SortServers(true);
				S_LocalSound("misc/menu2.wav");
				return;
			}
		}
		x = m_mousex - serversmenu.x - (serversmenu.cols - 1) * 8;
		y = m_mousey - serversmenu.y;
		{
			int list_rows, pinned_count;
			qboolean separator_visible;

			ServerList_GetVisibleLayout(&list_rows, NULL, &pinned_count, &separator_visible);

			if (ServerList_MouseOverPinnedSeparator(y, list_rows, pinned_count, separator_visible))
				return;
		}
		if (x < -8 || !ServerList_UseScrollbar(y))
			goto enter;
		serversmenu.scrollbar_grab = true;
		M_ServerList_Mousemove(m_mousex, m_mousey);

}
	default:
		break;
	}
}

void M_ServerList_Mousemove(int cx, int cy) // woods
{
	int prev_cursor = serversmenu.list.cursor;
	cy -= serversmenu.y;

	if (serversmenu.scrollbar_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			serversmenu.scrollbar_grab = false;
			return;
		}
		ServerList_UseScrollbar(cy);
		// Note: no return, we also update the cursor
	}

	ServerList_MousemoveList(cy);

	if (serversmenu.list.cursor != prev_cursor)
		TriggerServerPing(serversmenu.list.cursor);
}

/*
==================
Mods Menu (iw)
==================
*/

#define MAX_VIS_MODS	19
#define DOWNLOAD_MODS_LABEL	"Download"

typedef struct
{
	const char* name;
	char		description[64];
	qboolean	active;
	qboolean	download_menu;
} moditem_t;

static struct
{
	menulist_t			list;
	enum m_state_e		prev;
	int					x, y, cols;
	int					modcount;
	int					prev_cursor;
	menuticker_t		ticker;
	qboolean			scrollbar_grab;
	moditem_t			*items;
	int*				filtered_indices;
} modsmenu;

static qboolean M_Mods_IsActive(const char* game)
{
	extern char com_gamenames[];
	const char* list, * end, * p;

	if (!q_strcasecmp(game, GAMENAME))
		return !*com_gamenames;

	list = com_gamenames;
	while (*list)
	{
		end = list;
		while (*end && *end != ';')
			end++;

		p = game;
		while (*p && list != end)
			if (q_tolower(*p) == q_tolower(*list))
				p++, list++;
			else
				break;

		if (!*p && list == end)
			return true;

		list = end;
		if (*list)
			list++;
	}

	return false;
}

static int M_Mods_DescriptionX(int x)
{
	return x + q_min(max_word_length + 1, 13) * 8;
}

static void M_Mods_DrawDownloadMenuPrompt(int x, int y, const char *highlight, int highlight_len)
{
	if (highlight_len > 0)
		M_PrintHighlight(x, y, DOWNLOAD_MODS_LABEL, highlight, highlight_len);
	else
		M_Print(x, y, DOWNLOAD_MODS_LABEL);

	M_PrintWhite(M_Mods_DescriptionX(x), y, "...");
}

static void M_Mods_AddDownloadMenu(void)
{
	moditem_t mod;

	mod.name = DOWNLOAD_MODS_LABEL;
	q_strlcpy(mod.description, "...", sizeof(mod.description));
	mod.active = true;
	mod.download_menu = true;

	VEC_PUSH(modsmenu.items, mod);
	modsmenu.modcount = (int)VEC_SIZE(modsmenu.items);
}

static int M_Mods_ContentCount(void)
{
	int i, count = 0;

	for (i = 0; i < modsmenu.modcount; i++)
		if (!modsmenu.items[i].download_menu)
			count++;
	return count;
}

static void M_Mods_Add(const char* name)
{
	char check_path[MAX_OSPATH];
	char game_dir[MAX_OSPATH];
	char game_path[MAX_OSPATH];
	FILE *check_file;
	qboolean has_progs = false;
	qboolean has_pak = false;
	int pak_num;

	if (!COM_ResolveGameDir(name, game_dir, sizeof(game_dir)))
		return;
	q_snprintf(game_path, sizeof(game_path), "%s/%s", com_basedir, game_dir);
	
	// Check for progs.dat
	q_snprintf(check_path, sizeof(check_path), "%s/progs.dat", game_path);
	check_file = fopen(check_path, "rb");
	if (check_file)
	{
		has_progs = true;
		fclose(check_file);
	}
	
	// Check for pak files (pak0.pak, pak1.pak, etc)
	if (!has_progs)
	{
		for (pak_num = 0; pak_num < 10 && !has_pak; pak_num++)
		{
			q_snprintf(check_path, sizeof(check_path), "%s/pak%d.pak", game_path, pak_num);
			check_file = fopen(check_path, "rb");
			if (check_file)
			{
				has_pak = true;
				fclose(check_file);
			}
		}
		for (pak_num = 0; pak_num < 10 && !has_pak; pak_num++)
		{
			q_snprintf(check_path, sizeof(check_path), "%s/paks/pak%d.pak", game_path, pak_num);
			check_file = fopen(check_path, "rb");
			if (check_file)
			{
				has_pak = true;
				fclose(check_file);
			}
		}
	}
	
	// Only add if it has progs.dat or pak files
	if (!has_progs && !has_pak)
		return;
	
	
	moditem_t mod;
	mod.name = name;
	mod.download_menu = false;
	
	// Special case: Auto-detect known mods by scanning PAK files
	{
		char pakpath[MAX_OSPATH];
		qboolean found_ad_signature = false;
		qboolean found_hip_demo1 = false;
		qboolean found_hip_demo2 = false;
		qboolean found_hip_demo3 = false;
		qboolean found_hip_demo4 = false;
		qboolean found_rog_end1 = false;
		qboolean found_rog_end2 = false;
		qboolean found_rog_r1m1 = false;
		qboolean found_rog_r1m2 = false;
		qboolean found_rog_r1m3 = false;
		qboolean found_rog_r1m4 = false;
		qboolean found_mg_hub = false;
		qboolean found_mg_mgend = false;
		qboolean found_mg_mge1m1 = false;
		qboolean found_mg_horde1 = false;
		int pak_dir;
		const char *pak_dirs[] = {"", "paks/"};
		
		// PAK file structures (local definitions)
		#pragma pack(push, 1)
		typedef struct { char name[56]; int filepos; int filelen; } pak_entry_t;
		typedef struct { char id[4]; int dirofs; int dirlen; } pak_header_t;
		#pragma pack(pop)
		
		// Scan pak0.pak through pak9.pak in the game dir and optional paks/ folder.
		for (pak_dir = 0; pak_dir < 2; pak_dir++)
		{
			for (pak_num = 0; pak_num < 10; pak_num++)
			{
				FILE *pakfile;
				pak_header_t header;
				long pak_size;
				unsigned int numfiles, j;

				q_snprintf(pakpath, sizeof(pakpath), "%s/%spak%d.pak", game_path, pak_dirs[pak_dir], pak_num);
				pakfile = fopen(pakpath, "rb");
				if (!pakfile)
					continue;

				// Read PAK header
				if (fread(&header, 1, sizeof(header), pakfile) != sizeof(header))
				{
					fclose(pakfile);
					continue;
				}

				// Verify PAK signature
				if (header.id[0] != 'P' || header.id[1] != 'A' || header.id[2] != 'C' || header.id[3] != 'K')
				{
					fclose(pakfile);
					continue;
				}

				header.dirofs = LittleLong(header.dirofs);
				header.dirlen = LittleLong(header.dirlen);

				if (fseek(pakfile, 0, SEEK_END) != 0 ||
					(pak_size = ftell(pakfile)) < (long)sizeof(header))
				{
					fclose(pakfile);
					continue;
				}

				if (header.dirlen < 0 || header.dirofs < 0 ||
					header.dirlen % (int)sizeof(pak_entry_t) != 0 ||
					(long)header.dirofs > pak_size ||
					(long)header.dirlen > pak_size - (long)header.dirofs)
				{
					fclose(pakfile);
					continue;
				}

				numfiles = (unsigned int)(header.dirlen / (int)sizeof(pak_entry_t));
				if (numfiles > 4096)
				{
					fclose(pakfile);
					continue;
				}

				// Seek to directory
				if (fseek(pakfile, header.dirofs, SEEK_SET) != 0)
				{
					fclose(pakfile);
					continue;
				}

				// Read and scan entries one at a time
				for (j = 0; j < numfiles; j++)
				{
					pak_entry_t entry;
					if (fread(&entry, sizeof(entry), 1, pakfile) != 1)
						break;

					// Check for Arcane Dimensions signature
					if (!strcmp(entry.name, "maps/ad_chapters.bsp"))
						found_ad_signature = true;

					// Check for Hipnotic (Mission Pack 1) demos
					else if (!strcmp(entry.name, "hipdemo1.dem"))
						found_hip_demo1 = true;
					else if (!strcmp(entry.name, "hipdemo2.dem"))
						found_hip_demo2 = true;
					else if (!strcmp(entry.name, "hipdemo3.dem"))
						found_hip_demo3 = true;
					else if (!strcmp(entry.name, "hipdemo4.dem"))
						found_hip_demo4 = true;

					// Check for Rogue (Mission Pack 2) files
					else if (!strcmp(entry.name, "end1.bin"))
						found_rog_end1 = true;
					else if (!strcmp(entry.name, "end2.bin"))
						found_rog_end2 = true;
					else if (!strcmp(entry.name, "maps/r1m1.bsp"))
						found_rog_r1m1 = true;
					else if (!strcmp(entry.name, "maps/r1m2.bsp"))
						found_rog_r1m2 = true;
					else if (!strcmp(entry.name, "maps/r1m3.bsp"))
						found_rog_r1m3 = true;
					else if (!strcmp(entry.name, "maps/r1m4.bsp"))
						found_rog_r1m4 = true;

					// Check for Dimension of the Machine (MachineGames) files
					else if (!strcmp(entry.name, "maps/hub.bsp"))
						found_mg_hub = true;
					else if (!strcmp(entry.name, "maps/mgend.bsp"))
						found_mg_mgend = true;
					else if (!strcmp(entry.name, "maps/mge1m1.bsp"))
						found_mg_mge1m1 = true;
					else if (!strcmp(entry.name, "maps/horde1.bsp"))
						found_mg_horde1 = true;
				}

				fclose(pakfile);
			}
		}

		// Set description based on detected mod
		if (found_ad_signature)
		{
			q_strlcpy(mod.description, "Arcane Dimensions", sizeof(mod.description));
		}
		else if (found_hip_demo1 && found_hip_demo2 && found_hip_demo3 && found_hip_demo4)
		{
			q_strlcpy(mod.description, "Mission Pack 1: Scourge of Armagon - Hipnotic", sizeof(mod.description));
		}
		else if (found_rog_end1 && found_rog_end2 && found_rog_r1m1 && found_rog_r1m2 && found_rog_r1m3 && found_rog_r1m4)
		{
			q_strlcpy(mod.description, "Mission Pack 2: Dissolution of Eternity - Rogue", sizeof(mod.description));
		}
		else if (found_mg_hub && found_mg_mgend && found_mg_mge1m1 && found_mg_horde1)
		{
			q_strlcpy(mod.description, "Dimension of the Machine - MachineGames", sizeof(mod.description));
		}
		else
		{
			mod.description[0] = '\0'; // No description yet
		}
	}

	// Read description from descript.ion file
	{
		char desc_path[MAX_OSPATH];
		FILE *f;
		q_snprintf(desc_path, sizeof(desc_path), "%s/descript.ion", game_path);
		f = fopen(desc_path, "r");
		if (f)
		{
			if (fgets(mod.description, sizeof(mod.description), f))
			{
				// Remove trailing newline if present
				size_t len = strlen(mod.description);
				if (len > 0 && mod.description[len-1] == '\n')
					mod.description[len-1] = '\0';
			}
			fclose(f);
		}
		else
		{
			// mod.description[0] = '\0';  // Don't clear - preserve auto-detected description
		}
	}

	mod.active = M_Mods_IsActive(name);
	if (mod.active && modsmenu.list.cursor == -1)
		modsmenu.list.cursor = modsmenu.modcount;

	// Ensure there's enough space for one more item
	VEC_PUSH(modsmenu.items, mod);

	modsmenu.items[modsmenu.modcount] = mod;
	modsmenu.modcount++;
}

static void M_Mods_UpdateViewsize(void)
{
	modsmenu.list.viewsize = (modsmenu.list.search.len == 0) ?
		MAX_VIS_MODS - 1 : MAX_VIS_MODS;
}

static void M_Mods_Refilter(void)
{
	int i;

	M_Mods_UpdateViewsize();
	VEC_CLEAR(modsmenu.filtered_indices);

	for (i = 0; i < modsmenu.modcount; i++)
	{
		if (modsmenu.list.search.len == 0 ||
			q_strcasestr(modsmenu.items[i].name, modsmenu.list.search.text) ||
			q_strcasestr(modsmenu.items[i].description, modsmenu.list.search.text))
		{
			VEC_PUSH(modsmenu.filtered_indices, i);
		}
	}

	modsmenu.list.numitems = VEC_SIZE(modsmenu.filtered_indices);

	if (modsmenu.list.cursor >= modsmenu.list.numitems)
		modsmenu.list.cursor = modsmenu.list.numitems - 1;

	if (modsmenu.list.cursor < 0 && modsmenu.list.numitems > 0)
		modsmenu.list.cursor = 0;

	M_List_CenterCursor(&modsmenu.list);
}

static void M_Mods_Init(void)
{
	filelist_item_t* item;

	modsmenu.list.cursor = -1;
	modsmenu.list.scroll = 0;
	modsmenu.list.numitems = 0;
	modsmenu.modcount = 0;
	modsmenu.scrollbar_grab = false;
	VEC_CLEAR(modsmenu.items);
	VEC_CLEAR(modsmenu.filtered_indices);

	memset(&modsmenu.list.search, 0, sizeof(modsmenu.list.search));
	modsmenu.list.search.maxlen = 32;
	M_Mods_UpdateViewsize();

	M_Ticker_Init(&modsmenu.ticker);

	if (CL_ModDownloadsAvailable())
		M_Mods_AddDownloadMenu();

	for (item = modlist; item; item = item->next)
		M_Mods_Add(item->name);

	// Force 12 chars for consistent column alignment as requested
	max_word_length = 12;

	M_Mods_Refilter();

	if (modsmenu.list.cursor == -1)
		modsmenu.list.cursor = 0;

	M_List_CenterCursor(&modsmenu.list);
}

static qboolean M_Mods_HasDownloadGap(void)
{
	return modsmenu.list.search.len == 0 &&
		modsmenu.list.scroll == 0 &&
		modsmenu.list.numitems > 1 &&
		modsmenu.items[modsmenu.filtered_indices[0]].download_menu;
}

static qboolean M_Mods_MouseYInDownloadGap(int yrel)
{
	return M_Mods_HasDownloadGap() && yrel >= 8 && yrel < 16;
}

void M_Menu_Mods_f(void)
{
	key_dest = key_menu;
	modsmenu.prev = m_state;
	m_state = m_mods;
	m_entersound = true;
	M_Mods_Init();
}

void M_Mods_Draw(void)
{
	int x, y, i, cols;
	int firstvis, numvis;

	x = 16;
	y = 32;
	cols = 36;

	modsmenu.x = x;
	modsmenu.y = y;
	modsmenu.cols = cols;

	if (!keydown[K_MOUSE1])
		modsmenu.scrollbar_grab = false;

	if (modsmenu.prev_cursor != modsmenu.list.cursor)
	{
		modsmenu.prev_cursor = modsmenu.list.cursor;
		M_Ticker_Init(&modsmenu.ticker);
	}
	else
		M_Ticker_Update(&modsmenu.ticker);

	M_DrawCountHeader(x, y - 28, cols, "Mods",
		M_Mods_ContentCount(), "mod", "mods");
	M_DrawQuakeBar(x - 8, y - 16, cols + 2);

	M_List_GetVisibleRange(&modsmenu.list, &firstvis, &numvis);
	for (i = 0; i < numvis; i++)
	{
		int idx = i + firstvis;
		int mod_idx = modsmenu.filtered_indices[idx];
		qboolean selected = (idx == modsmenu.list.cursor);
		int row = i;
		int item_y;

		if (M_Mods_HasDownloadGap() && i > 0)
			row++;
		item_y = y + row * 8;

		if (modsmenu.list.search.len > 0)
		{
			if (modsmenu.items[mod_idx].download_menu)
			{
				M_Mods_DrawDownloadMenuPrompt(x, item_y,
					modsmenu.list.search.text,
					modsmenu.list.search.len);
			}
			else
			{
				M_PrintHighlightScroll2(x, item_y, (cols - 2) * 8,
					modsmenu.items[mod_idx].name,
					modsmenu.items[mod_idx].description,
					modsmenu.list.search.text,
					selected ? modsmenu.ticker.scroll_time : 0.0);
			}
		}
		else
		{
			if (modsmenu.items[mod_idx].download_menu)
			{
				M_Mods_DrawDownloadMenuPrompt(x, item_y, NULL, 0);
			}
			else
			{
				M_PrintScroll2(x, item_y, (cols - 2) * 8,
					modsmenu.items[mod_idx].name,
					modsmenu.items[mod_idx].description,
					selected ? modsmenu.ticker.scroll_time : 0.0,
					!modsmenu.items[mod_idx].active);
			}
		}

		if (selected)
			M_DrawCharacter(x - 8, item_y, 12 + ((int)(realtime * 4) & 1));
	}

	if (M_List_GetOverflow(&modsmenu.list) > 0)
	{
		M_List_DrawScrollbar(&modsmenu.list, x + cols * 8 - 8, y);

		if (modsmenu.list.scroll > 0)
			M_DrawEllipsisBar(x, y - 8, cols);
		if (modsmenu.list.scroll + modsmenu.list.viewsize < modsmenu.list.numitems)
			M_DrawEllipsisBar(x, y + (modsmenu.list.viewsize + (M_Mods_HasDownloadGap() ? 1 : 0)) * 8, cols);
	}

	if (modsmenu.list.search.len > 0) // Draw search box if search is active
	{
		M_DrawTextBox(16, 176, 32, 1);
		M_PrintHighlight(24, 184, modsmenu.list.search.text,
			modsmenu.list.search.text,
			modsmenu.list.search.len);
		int cursor_x = 24 + 8 * modsmenu.list.search.len;
		if (modsmenu.list.numitems == 0)
			M_DrawCharacter(cursor_x, 184, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, 184, 10 + ((int)(realtime * 4) & 1));
	}
}

qboolean M_Mods_Match(int index, char initial)
{
	int mod_idx = modsmenu.filtered_indices[index];
	return q_tolower(modsmenu.items[mod_idx].name[0]) == initial;
}

void M_Mods_Key(int key)
{

	int x, y; // woods #mousemenu

	if (keydown[K_CTRL])
	{
		if ((key == 'u' || key == 'U') && modsmenu.list.search.len > 0)
		{
			modsmenu.list.search.len = 0;
			modsmenu.list.search.text[0] = 0;
			M_Mods_Refilter();
			return;
		}
		else if (key == K_BACKSPACE && modsmenu.list.search.len > 0)
		{
			M_DeletePrevWord(&modsmenu.list.search);
			M_Mods_Refilter();
			return;
		}
	}

	if (key >= 32 && key < 127) // Handle search input first, printable characters
	{
		if (modsmenu.list.search.len < modsmenu.list.search.maxlen)
		{
			modsmenu.list.search.text[modsmenu.list.search.len++] = key;
			modsmenu.list.search.text[modsmenu.list.search.len] = 0;
			M_Mods_Refilter();
			return;
		}
	}

	if (modsmenu.scrollbar_grab)
	{
		switch (key)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			modsmenu.scrollbar_grab = false;
			break;
		}
		return;
	}

	if (M_List_Key(&modsmenu.list, key))
		return;

	if (M_List_CycleMatch(&modsmenu.list, key, M_Mods_Match))
		return;

	if (M_Ticker_Key(&modsmenu.ticker, key))
		return;

	switch (key)
	{
	case K_ESCAPE:
		if (modsmenu.list.search.len > 0) // Clear search but stay in menu
		{
			modsmenu.list.search.len = 0;
			modsmenu.list.search.text[0] = 0;
			M_Mods_Refilter();
			return;
		}
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		if (modsmenu.prev == m_options)
			M_Menu_Options_f();
		else
			M_Menu_Main_f();
		break;
	case K_BACKSPACE:
		if (modsmenu.list.search.len > 0)
		{
			modsmenu.list.search.text[--modsmenu.list.search.len] = 0;
			M_Mods_Refilter();
			return;
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	enter:
		if (modsmenu.list.numitems > 0)
		{
			int mod_idx = modsmenu.filtered_indices[modsmenu.list.cursor];
			if (modsmenu.items[mod_idx].download_menu)
			{
				M_Menu_DownloadMods_f();
				break;
			}
			Cbuf_AddText(va("game \"%s\"\n", modsmenu.items[mod_idx].name));
			M_Menu_Main_f();
		}
		break;

	case K_MOUSE1: // woods #mousemenu
		x = m_mousex - modsmenu.x - (modsmenu.cols - 1) * 8;
		y = m_mousey - modsmenu.y;
		if (x < -8 || !M_List_UseScrollbar(&modsmenu.list, y))
		{
			if (M_Mods_MouseYInDownloadGap(y))
			{
				S_LocalSound ("misc/menu3.wav");
				break;
			}
			goto enter;
		}
		modsmenu.scrollbar_grab = true;
		M_Mods_Mousemove(m_mousex, m_mousey);

	default:
		break;
	}
}

void M_Mods_Mousemove(int cx, int cy) // woods #mousemenu
{
		cy -= modsmenu.y;

	if (modsmenu.scrollbar_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			modsmenu.scrollbar_grab = false;
			return;
		}
		M_List_UseScrollbar(&modsmenu.list, cy);
		// Note: no return, we also update the cursor
	}

	if (M_Mods_MouseYInDownloadGap(cy))
		return;
	if (M_Mods_HasDownloadGap() && cy >= 16)
		cy -= 8;

	M_List_Mousemove(&modsmenu.list, cy);
}

/*
==================
Download Mods Menu
==================
*/

#define MAX_VIS_DOWNLOAD_MODS	17
#define DOWNLOAD_MODS_LIST_X	16
#define DOWNLOAD_MODS_LIST_Y	32
/* Match the local mods menu's width: cols 36 with the last 2 columns reserved
 * for the scrollbar, giving a 34-char content budget (name + 1 gap + detail) so
 * neither column draws under the scrollbar when the list overflows. */
#define DOWNLOAD_MODS_LIST_COLS	36
#define DOWNLOAD_MODS_SCROLLBAR_COLS	2
#define DOWNLOAD_MODS_NAME_CHARS	24
#define DOWNLOAD_MODS_DETAIL_CHARS \
	(DOWNLOAD_MODS_LIST_COLS - DOWNLOAD_MODS_SCROLLBAR_COLS \
		- DOWNLOAD_MODS_NAME_CHARS - 1)
#define DOWNLOAD_MODS_SEPARATOR_INDEX	-1
#define DOWNLOAD_MODS_MAX_URL	1024
#define DOWNLOAD_MODS_MAX_MANIFEST_BYTES	((curl_off_t)1024 * 1024)
#define DOWNLOAD_MODS_MAX_SPLIT_PART_BYTES	((curl_off_t)2 * 1024 * 1024 * 1024)
#define DOWNLOAD_MODS_MAX_ARCHIVE_BYTES		((curl_off_t)3 * 1024 * 1024 * 1024)
#define DOWNLOAD_MODS_MAX_PARTS				16
/* Throttle repeated menu opens for the same source in one engine session without
 * repeatedly downloading the static manifest when the user reopens the menu. */
#define DOWNLOAD_MODS_FETCH_THROTTLE_SECONDS	(2 * 60)
/* Repo-relative path of the curated manifest; the host/owner/branch come from
 * the configured cl_web_download_url repo (see M_DownloadMods_ManifestUrl). */
#define DOWNLOAD_MODS_MANIFEST_PATH				"mods/moddownloads.json"

typedef enum
{
	DOWNLOADMOD_SPLIT_ZIP,
	DOWNLOADMOD_SPLIT_MANIFEST,
	DOWNLOADMOD_SINGLE_ZIP
} downloadmodtype_t;

typedef struct
{
	downloadmodtype_t type;
	char		id[32];
	char		name[64];
	char		version[32];
	char		size[32];
	char		description[96];
	char		install_dir[MAX_QPATH];
	char		url[DOWNLOAD_MODS_MAX_URL];
	char		sha256[65];
	qboolean	installed;
	/* Split archives store ordered part URLs here (part_count > 0);
	 * single-ZIP entries leave part_count at 0 and use url/sha256 above. */
	int			part_count;
	char		part_url[DOWNLOAD_MODS_MAX_PARTS][DOWNLOAD_MODS_MAX_URL];
	char		part_sha256[DOWNLOAD_MODS_MAX_PARTS][65];
} downloadmoditem_t;

typedef struct
{
	downloadmodtype_t	type;
	const char			*id;
	const char			*name;
	const char			*size;
	const char			*install_dir;
	const char			*url;
	const char			*sha256;
	int					part_count;
	const char			*part_url[DOWNLOAD_MODS_MAX_PARTS];
	const char			*part_sha256[DOWNLOAD_MODS_MAX_PARTS];
} downloadmodseed_t;

/* Last-resort bootstrap list. The static manifest replaces this as soon as it
 * is available, but users still get a usable menu if q1tools.github.io is
 * temporarily unreachable. */
static const downloadmodseed_t downloadmods_builtin[] =
{
	{ DOWNLOADMOD_SINGLE_ZIP, "agjam", "Antigravity Jam", "126 MB", "agjam",
		"https://github.com/q1tools/q1tools.github.io/releases/download/agjam-1.2/antigravityjam1.2.zip",
		"685c20c5e9d82615afbe3f1828d521823fb1e2f5df3a5de09f069ec99a1aea90" },
	{ DOWNLOADMOD_SINGLE_ZIP, "ad", "Arcane Dimensions 1.80 + Patch 1", "306 MB", "ad",
		"https://github.com/q1tools/q1tools.github.io/releases/download/ad-v1.80p1/ad_v1_80p1final.zip",
		"7ad993da6c760c446ca31fad71e9f5b6c9eee99b6354f161aa746b572fe70a7d" },
	{ DOWNLOADMOD_SINGLE_ZIP, "bonk", "Bonk Jam", "358 MB", "bonk",
		"https://github.com/q1tools/q1tools.github.io/releases/download/bonk-1.0/bonkjam.zip",
		"5157acb20be50e453ae38335c5cbc60cff2a09457855f153b44dbff6b6c35ced" },
	{ DOWNLOADMOD_SINGLE_ZIP, "dwell", "Dwell", "367 MB", "dwell",
		"https://github.com/q1tools/q1tools.github.io/releases/download/dwell-2p2/dwellv2p2.zip",
		"15bbc5dc0eb3fee5763835cea1aa62ab263545ca8c04aaa019982d45d1e8d11d" },
	{ DOWNLOADMOD_SINGLE_ZIP, "limjam", "Liminal Spaces Jam", "487 MB", "limjam",
		"https://github.com/q1tools/q1tools.github.io/releases/download/limjam-3/limjam.zip",
		"b936d086fece844fbf88dc0681011b80513f92aa3542474e3d8d2b6540cec409" },
	{ DOWNLOADMOD_SINGLE_ZIP, "peril3.0", "Peril", "1.7 GB", "peril3.0",
		"https://github.com/q1tools/q1tools.github.io/releases/download/peril3.0-c/peril3.0c.zip",
		"4e7a4ebe590974771d066c54bc7196e4205e3a783de7254f5b165de992a402c1" },
	{ DOWNLOADMOD_SINGLE_ZIP, "mc_q30th_jam", "Quake 30th Anniversary Jam", "144 MB", "mc_q30th_jam",
		"https://github.com/q1tools/q1tools.github.io/releases/download/mc_q30th_jam-67/mc_q30th_jam.zip",
		"195a2fe426b99d6642866d4e6fbca06c0e0ad9175e9d7eb936eff2a62bc4e5b6" },
	{ DOWNLOADMOD_SINGLE_ZIP, "qbj1", "Quake Brutalist Jam I 1.05", "562 MB", "qbj1",
		"https://github.com/q1tools/q1tools.github.io/releases/download/qbj1-1.0.5/qbj_1.05.zip",
		"7a5521754973bcdb4bb8f4568a464cbb3ef5df70d28e365d04d841005d7245f3" },
	{ DOWNLOADMOD_SINGLE_ZIP, "qbj2", "Quake Brutalist Jam II 1.2", "554 MB", "qbj2",
		"https://github.com/q1tools/q1tools.github.io/releases/download/qbj2-1.2/qbj2_1.2.zip",
		"f2b77ee6fc001e0d7e9ab8dbe7642ee3b85e9a8c9162c2f480e3f1cc0ad157dd" },
	{ DOWNLOADMOD_SPLIT_ZIP, "qbj3", "Quake Brutalist Jam III 1.3", "2.5 GB", "qbj3",
		"", "", 2,
		{
			"https://github.com/q1tools/q1tools.github.io/releases/download/qbj3-1.3/qbj3_1.3.zip.part-aaa",
			"https://github.com/q1tools/q1tools.github.io/releases/download/qbj3-1.3/qbj3_1.3.zip.part-aab"
		},
		{
			"8b442fe7da4d4ab426bc43e4eb5e72985bef9f36b221c0d65fc13e1d48ed2915",
			"934a78404654431b43ea78a45d2d6ecb5152ebf77bab581103b4b484edc9b26c"
		} },
	{ DOWNLOADMOD_SINGLE_ZIP, "rk", "Raven Keep 1.0.1", "150 MB", "rk",
		"https://github.com/q1tools/q1tools.github.io/releases/download/rk-1.0.1/ravenkeep.zip",
		"cf64625b4cd222a440a4fbb53766b6f1efc7e7b3fce38ca0b61e6835b9b55977" },
	{ DOWNLOADMOD_SINGLE_ZIP, "rm1.2", "Re:Mobilize", "580 MB", "rm1.2",
		"https://github.com/q1tools/q1tools.github.io/releases/download/rm1.2-b/rm1.2.zip",
		"1346db6e598a0bc2b5acdf4873dc4caf94940d52509357e6e1197880158d3a9d" },
	{ DOWNLOADMOD_SINGLE_ZIP, "sj2", "Sewer Jam 2", "232 MB", "sj2",
		"https://github.com/q1tools/q1tools.github.io/releases/download/sj2/sewerjam2.zip",
		"ff4a1016f152ecaa2ab0007e164de4cb8fda4e60f36acd9bca16e6581a180f8c" },
	{ DOWNLOADMOD_SINGLE_ZIP, "enyo", "Slave Zero X: Episode Enyo", "133 MB", "enyo",
		"https://github.com/q1tools/q1tools.github.io/releases/download/enyo-20240115/enyo_hd_20240115.zip",
		"45ddd187e07af6b3d43a5f9f5597172e43aae7cebe73cdd2a5057ad6a36f570e" },
	{ DOWNLOADMOD_SINGLE_ZIP, "sp", "Snack Pack 3", "74.9 MB", "sp",
		"https://github.com/q1tools/q1tools.github.io/releases/download/sp-3/snack3.zip",
		"c92c3a22d0df88190c81e645bd23f3e065106c6895d34af92b02aed54aace6d4" },
	{ DOWNLOADMOD_SINGLE_ZIP, "immortal", "The Immortal Lock", "357 MB", "immortal",
		"https://github.com/q1tools/q1tools.github.io/releases/download/immortal-1.9/immortal_v1_9.zip",
		"a04aee22a146df6dd97db37932c7c96028307dab4f6f96e15944bd0e5006bd5b" },
	{ DOWNLOADMOD_SINGLE_ZIP, "tcj2022", "Twisted Christmas Jam 2022", "260 MB", "tcj2022",
		"https://github.com/q1tools/q1tools.github.io/releases/download/tcj2022-3/tcj_r3.zip",
		"0fc8f530297f27b1360f05ae59d8facd3a9aedf153ac4d02dc7af882f0b78e1e" },
	{ DOWNLOADMOD_SINGLE_ZIP, "xmasjam2020", "Xmas Jam 2020", "386 MB", "xmasjam2020",
		"https://github.com/q1tools/q1tools.github.io/releases/download/xmasjam2020-1/xmasjam2020.zip",
		"6755b682db4d2fcd3e92dee61b7975468a2807abc2a93f29051835e7e33aec69" },
	{ DOWNLOADMOD_SINGLE_ZIP, "mc_xmasjam_2025", "Xmas Scraps Jam 2025", "145 MB", "mc_xmasjam_2025",
		"https://github.com/q1tools/q1tools.github.io/releases/download/mc_xmasjam_2025-1/mc_xmasjam_2025.zip",
		"01869daddb52357b86aba0e145d5c905eb06f6168e5f33af0ee65b959c824449" }
};

static struct
{
	menulist_t			list;
	enum m_state_e		prev;
	int					x, y, cols;
	int					itemcount;
	int					prev_cursor;
	int					exit_prompt_cursor;
	menuticker_t		ticker;
	qboolean			scrollbar_grab;
	qboolean			exit_prompt;
	char				message[96];
	double				message_time;
	downloadmoditem_t	*items;
	int					*filtered_indices;
	char				source_manifest_url[DOWNLOAD_MODS_MAX_URL];
} downloadmodsmenu;

/* Background fetch of the static moddownloads manifest. The worker thread only
 * performs the network GET and hands raw JSON text to the main thread, which
 * validates and applies it. Source URLs are resolved before the worker starts
 * and captured here so the thread reads no cvars. */
static struct
{
	SDL_Thread			*thread;
	SDL_mutex			*mutex;
	qboolean			active;		/* a fetch has been started */
	qboolean			done;		/* worker finished, result not yet consumed */
	qboolean			success;
	time_t				last_fetch_time; /* process-local rate limit */
	char				last_manifest_url[DOWNLOAD_MODS_MAX_URL];
	char				*json;		/* manifest response text, owned by main thread once done */
	char				manifest_url[DOWNLOAD_MODS_MAX_URL];
	char				error[128];
} downloadmodsfetch;

static void M_DownloadMods_StartFetch(qboolean force);
static void M_DownloadMods_PollFetch(void);
static void M_DownloadMods_ShutdownFetch(void);
static qboolean M_DownloadMods_RepoUrls(char *api_url, size_t api_sz,
	char *asset_prefix, size_t prefix_sz);
static qboolean M_DownloadMods_ManifestUrl(char *out, size_t outsize);

typedef enum
{
	DOWNLOADMOD_EXIT_BACKGROUND,
	DOWNLOADMOD_EXIT_CANCEL,
	DOWNLOADMOD_EXIT_COUNT
} downloadmodexitchoice_t;

static void M_DownloadMods_SetMessage(const char *message)
{
	q_strlcpy(downloadmodsmenu.message, message, sizeof(downloadmodsmenu.message));
	downloadmodsmenu.message_time = realtime;
}

static void M_DownloadMods_Add(const downloadmoditem_t *item)
{
	if (!item)
		return;

	VEC_PUSH(downloadmodsmenu.items, *item);
	downloadmodsmenu.itemcount = (int)VEC_SIZE(downloadmodsmenu.items);
}

static qboolean M_DownloadMods_InstallDirNameOkay(const char *name);
static qboolean M_DownloadMods_PathLooksInstalled(const char *path);

static void M_DownloadMods_SeedBuiltins(void)
{
	size_t i;
	int p;

	for (i = 0; i < sizeof(downloadmods_builtin) / sizeof(downloadmods_builtin[0]); i++)
	{
		const downloadmodseed_t *seed = &downloadmods_builtin[i];
		downloadmoditem_t item;

		if (!seed->id || !seed->name || !seed->install_dir ||
			!M_DownloadMods_InstallDirNameOkay(seed->id) ||
			!M_DownloadMods_InstallDirNameOkay(seed->install_dir))
			continue;

		memset(&item, 0, sizeof(item));
		item.type = seed->type;
		q_strlcpy(item.id, seed->id, sizeof(item.id));
		q_strlcpy(item.name, seed->name, sizeof(item.name));
		q_strlcpy(item.size, seed->size ? seed->size : "", sizeof(item.size));
		q_strlcpy(item.description, "Built-in release", sizeof(item.description));
		q_strlcpy(item.install_dir, seed->install_dir, sizeof(item.install_dir));
		q_strlcpy(item.url, seed->url ? seed->url : "", sizeof(item.url));
		q_strlcpy(item.sha256, seed->sha256 ? seed->sha256 : "", sizeof(item.sha256));

		item.part_count = seed->part_count;
		if (item.part_count < 0)
			item.part_count = 0;
		if (item.part_count > DOWNLOAD_MODS_MAX_PARTS)
			item.part_count = DOWNLOAD_MODS_MAX_PARTS;
		for (p = 0; p < item.part_count; p++)
		{
			q_strlcpy(item.part_url[p],
				seed->part_url[p] ? seed->part_url[p] : "",
				sizeof(item.part_url[p]));
			q_strlcpy(item.part_sha256[p],
				seed->part_sha256[p] ? seed->part_sha256[p] : "",
				sizeof(item.part_sha256[p]));
		}

		M_DownloadMods_Add(&item);
	}
}

static void M_DownloadMods_ClearItemsForSource(const char *manifest_url,
	qboolean seed_builtins)
{
	downloadmodsmenu.itemcount = 0;
	VEC_CLEAR(downloadmodsmenu.items);
	q_strlcpy(downloadmodsmenu.source_manifest_url,
		manifest_url ? manifest_url : "",
		sizeof(downloadmodsmenu.source_manifest_url));

	if (seed_builtins)
		M_DownloadMods_SeedBuiltins();
}

static qboolean M_DownloadMods_ProbeInstalledAt(const downloadmoditem_t *item,
	const char *root, const char *prefix)
{
	char game_path[MAX_OSPATH];

	if (!root || !*root)
		return false;

	if (prefix && *prefix)
	{
		if ((size_t)q_snprintf(game_path, sizeof(game_path), "%s/%s/%s",
			root, prefix, item->install_dir) >= sizeof(game_path))
			return false;
	}
	else if ((size_t)q_snprintf(game_path, sizeof(game_path), "%s/%s",
		root, item->install_dir) >= sizeof(game_path))
		return false;

	return M_DownloadMods_PathLooksInstalled(game_path);
}

static qboolean M_DownloadMods_ProbeInstalled(const downloadmoditem_t *item)
{
	static const char *prefixes[] = { "", "games", "mods" };
	size_t i;

	if (!item || !M_DownloadMods_InstallDirNameOkay(item->install_dir))
		return false;

	for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++)
	{
		if (M_DownloadMods_ProbeInstalledAt(item, com_basedir, prefixes[i]))
			return true;
		if (host_parms && host_parms->userdir != host_parms->basedir &&
			M_DownloadMods_ProbeInstalledAt(item, host_parms->userdir, prefixes[i]))
			return true;
	}

	return false;
}

static void M_DownloadMods_RefreshInstalledCache(void)
{
	int i;

	for (i = 0; i < downloadmodsmenu.itemcount; i++)
		downloadmodsmenu.items[i].installed =
			M_DownloadMods_ProbeInstalled(&downloadmodsmenu.items[i]);
}

static qboolean M_DownloadMods_IsInstalled(const downloadmoditem_t *item)
{
	return item && item->installed;
}

static qboolean M_DownloadMods_ItemMatchesSearch(const downloadmoditem_t *item)
{
	if (!item)
		return false;

	if (downloadmodsmenu.list.search.len == 0)
		return true;

	return q_strcasestr(item->name, downloadmodsmenu.list.search.text) ||
		q_strcasestr(item->version, downloadmodsmenu.list.search.text) ||
		q_strcasestr(item->description, downloadmodsmenu.list.search.text) ||
		q_strcasestr(item->id, downloadmodsmenu.list.search.text);
}

static int M_DownloadMods_CompareItems(const void *a, const void *b)
{
	int ia = *(const int *)a;
	int ib = *(const int *)b;
	const downloadmoditem_t *item_a = &downloadmodsmenu.items[ia];
	const downloadmoditem_t *item_b = &downloadmodsmenu.items[ib];
	int result;

	result = q_strcasecmp(item_a->name, item_b->name);
	if (result)
		return result;

	result = q_strcasecmp(item_a->version, item_b->version);
	if (result)
		return result;

	return q_strcasecmp(item_a->id, item_b->id);
}

static qboolean M_DownloadMods_IsSelectableDisplayIndex(int index)
{
	int item_idx;

	if (index < 0 || index >= downloadmodsmenu.list.numitems)
		return false;

	item_idx = downloadmodsmenu.filtered_indices[index];
	return item_idx != DOWNLOAD_MODS_SEPARATOR_INDEX;
}

static void M_DownloadMods_CopySelectedId(char *out, size_t outsize)
{
	int item_idx;

	if (!out || outsize == 0)
		return;
	out[0] = '\0';

	if (!downloadmodsmenu.filtered_indices ||
		downloadmodsmenu.list.cursor < 0 ||
		downloadmodsmenu.list.cursor >= downloadmodsmenu.list.numitems)
		return;

	item_idx = downloadmodsmenu.filtered_indices[downloadmodsmenu.list.cursor];
	if (item_idx == DOWNLOAD_MODS_SEPARATOR_INDEX ||
		item_idx < 0 ||
		item_idx >= downloadmodsmenu.itemcount)
		return;

	q_strlcpy(out, downloadmodsmenu.items[item_idx].id, outsize);
}

static qboolean M_DownloadMods_SelectId(const char *id)
{
	int i;

	if (!id || !*id)
		return false;

	for (i = 0; i < downloadmodsmenu.list.numitems; i++)
	{
		int item_idx = downloadmodsmenu.filtered_indices[i];

		if (item_idx == DOWNLOAD_MODS_SEPARATOR_INDEX)
			continue;
		if (item_idx < 0 || item_idx >= downloadmodsmenu.itemcount)
			continue;
		if (!q_strcasecmp(downloadmodsmenu.items[item_idx].id, id))
		{
			downloadmodsmenu.list.cursor = i;
			M_List_CenterCursor(&downloadmodsmenu.list);
			return true;
		}
	}

	return false;
}

static qboolean M_DownloadMods_EnsureSelectableCursor(int dir)
{
	if (downloadmodsmenu.list.numitems <= 0)
		return false;

	if (M_DownloadMods_IsSelectableDisplayIndex(downloadmodsmenu.list.cursor))
		return true;

	if (M_List_SelectNextActive(&downloadmodsmenu.list,
		downloadmodsmenu.list.cursor, dir, true))
		return true;

	downloadmodsmenu.list.cursor = 0;
	downloadmodsmenu.list.scroll = 0;
	return false;
}

static void M_DownloadMods_EnsureSelectableCursorForKey(int key)
{
	switch (key)
	{
	case K_UPARROW:
	case K_KP_UPARROW:
	case K_PGUP:
	case K_KP_PGUP:
	case K_END:
	case K_KP_END:
		M_DownloadMods_EnsureSelectableCursor(-1);
		break;

	default:
		M_DownloadMods_EnsureSelectableCursor(1);
		break;
	}
}

static void M_DownloadMods_Refilter(void)
{
	int i;
	char selected_id[sizeof(downloadmodsmenu.items[0].id)] = "";
	int *download_indices = NULL;
	int *installed_indices = NULL;

	if (downloadmodsmenu.filtered_indices &&
		downloadmodsmenu.list.cursor >= 0 &&
		downloadmodsmenu.list.cursor < downloadmodsmenu.list.numitems)
	{
		int selected_idx =
			downloadmodsmenu.filtered_indices[downloadmodsmenu.list.cursor];
		if (selected_idx != DOWNLOAD_MODS_SEPARATOR_INDEX &&
			selected_idx >= 0 &&
			selected_idx < downloadmodsmenu.itemcount)
		{
			q_strlcpy(selected_id, downloadmodsmenu.items[selected_idx].id,
				sizeof(selected_id));
		}
	}

	VEC_CLEAR(downloadmodsmenu.filtered_indices);

	for (i = 0; i < downloadmodsmenu.itemcount; i++)
	{
		downloadmoditem_t *item = &downloadmodsmenu.items[i];

		if (!M_DownloadMods_ItemMatchesSearch(item))
			continue;

		if (M_DownloadMods_IsInstalled(item))
			VEC_PUSH(installed_indices, i);
		else
			VEC_PUSH(download_indices, i);
	}

	if (VEC_SIZE(download_indices) > 1)
		qsort(download_indices, VEC_SIZE(download_indices),
			sizeof(download_indices[0]), M_DownloadMods_CompareItems);
	if (VEC_SIZE(installed_indices) > 1)
		qsort(installed_indices, VEC_SIZE(installed_indices),
			sizeof(installed_indices[0]), M_DownloadMods_CompareItems);

	for (i = 0; i < (int)VEC_SIZE(download_indices); i++)
		VEC_PUSH(downloadmodsmenu.filtered_indices, download_indices[i]);

	if (VEC_SIZE(download_indices) > 0 && VEC_SIZE(installed_indices) > 0)
		VEC_PUSH(downloadmodsmenu.filtered_indices, DOWNLOAD_MODS_SEPARATOR_INDEX);

	for (i = 0; i < (int)VEC_SIZE(installed_indices); i++)
		VEC_PUSH(downloadmodsmenu.filtered_indices, installed_indices[i]);

	VEC_FREE(download_indices);
	VEC_FREE(installed_indices);

	downloadmodsmenu.list.numitems = (int)VEC_SIZE(downloadmodsmenu.filtered_indices);

	if (selected_id[0])
	{
		for (i = 0; i < downloadmodsmenu.list.numitems; i++)
		{
			int item_idx = downloadmodsmenu.filtered_indices[i];
			if (item_idx == DOWNLOAD_MODS_SEPARATOR_INDEX)
				continue;
			if (!q_strcasecmp(downloadmodsmenu.items[item_idx].id, selected_id))
			{
				downloadmodsmenu.list.cursor = i;
				break;
			}
		}
	}

	if (downloadmodsmenu.list.numitems <= 0)
	{
		downloadmodsmenu.list.cursor = -1;
		downloadmodsmenu.list.scroll = 0;
		return;
	}

	if (downloadmodsmenu.list.cursor >= downloadmodsmenu.list.numitems)
		downloadmodsmenu.list.cursor = downloadmodsmenu.list.numitems - 1;
	if (downloadmodsmenu.list.cursor < 0)
		downloadmodsmenu.list.cursor = 0;

	M_DownloadMods_EnsureSelectableCursor(1);
	M_List_CenterCursor(&downloadmodsmenu.list);
}

static void M_DownloadMods_Init(void)
{
	char manifest_url[DOWNLOAD_MODS_MAX_URL];
	qboolean source_ok;
	qboolean keep_items;

	source_ok = M_DownloadMods_ManifestUrl(manifest_url, sizeof(manifest_url));
	if (!source_ok)
		manifest_url[0] = '\0';

	keep_items = source_ok && downloadmodsmenu.itemcount > 0 &&
		!q_strcasecmp(downloadmodsmenu.source_manifest_url, manifest_url);

	downloadmodsmenu.scrollbar_grab = false;
	downloadmodsmenu.exit_prompt = false;
	downloadmodsmenu.exit_prompt_cursor = DOWNLOADMOD_EXIT_BACKGROUND;
	downloadmodsmenu.prev_cursor = -2;
	downloadmodsmenu.list.viewsize = MAX_VIS_DOWNLOAD_MODS;
	downloadmodsmenu.list.cursor = -1;
	downloadmodsmenu.list.scroll = 0;
	downloadmodsmenu.list.numitems = 0;
	downloadmodsmenu.list.isactive_fn = M_DownloadMods_IsSelectableDisplayIndex;
	downloadmodsmenu.message[0] = '\0';
	downloadmodsmenu.message_time = 0.0;
	VEC_CLEAR(downloadmodsmenu.filtered_indices);

	if (!keep_items)
	{
		M_DownloadMods_ClearItemsForSource(manifest_url,
			source_ok && CL_DownloadRepoIsQ1Tools());
	}

	memset(&downloadmodsmenu.list.search, 0, sizeof(downloadmodsmenu.list.search));
	downloadmodsmenu.list.search.maxlen = 32;

	M_Ticker_Init(&downloadmodsmenu.ticker);

	M_DownloadMods_StartFetch(!keep_items);

	M_DownloadMods_RefreshInstalledCache();
	M_DownloadMods_Refilter();

	if (downloadmodsmenu.list.cursor == -1)
		downloadmodsmenu.list.cursor = 0;

	M_DownloadMods_EnsureSelectableCursor(1);
	M_List_CenterCursor(&downloadmodsmenu.list);
}

void M_Menu_DownloadMods_f(void)
{
	key_dest = key_menu;
	downloadmodsmenu.prev = m_state;
	m_state = m_downloadmods;
	m_entersound = true;
	M_DownloadMods_Init();
}

static downloadmoditem_t *M_DownloadMods_SelectedItem(void)
{
	if (downloadmodsmenu.list.numitems <= 0 ||
		downloadmodsmenu.list.cursor < 0 ||
		downloadmodsmenu.list.cursor >= downloadmodsmenu.list.numitems)
		return NULL;

	{
		int item_idx = downloadmodsmenu.filtered_indices[downloadmodsmenu.list.cursor];
		if (item_idx == DOWNLOAD_MODS_SEPARATOR_INDEX)
			return NULL;
		return &downloadmodsmenu.items[item_idx];
	}
}

typedef enum
{
	DOWNLOADMOD_INSTALL_NONE,
	DOWNLOADMOD_INSTALL_MANIFEST,
	DOWNLOADMOD_INSTALL_ARCHIVE
} downloadmodinstallstage_t;

typedef struct
{
	char	url[DOWNLOAD_MODS_MAX_URL];
	char	sha256[65];
	char	filename[MAX_QPATH];
	char	temp_path[MAX_OSPATH];
} downloadmodpart_t;

typedef struct
{
	qboolean	active;
	qboolean	downloading;
	qboolean	done;
	qboolean	success;
	qboolean	aborted;
	SDL_Thread	*thread;
	char		url[DOWNLOAD_MODS_MAX_URL];
	char		temp_path[MAX_OSPATH];
	char		display_name[64];
	char		error[128];
	double		received;
	double		total;
	curl_off_t	max_bytes;
	qofs_t		file_size;
	long		response_code;
	CURLcode	curl_result;
} downloadmodtransfer_t;

static struct
{
	qboolean					active;
	downloadmodinstallstage_t	stage;
	downloadmoditem_t			item;
	downloadmodpart_t			*parts;
	int							part_index;
	char						stage_dir[MAX_OSPATH];
	char						joined_zip[MAX_OSPATH];
	char						joined_name[MAX_QPATH];
	char						status[96];
	double						last_progress_print;
	SDL_mutex					*mutex;
	SDL_atomic_t				abort_requested;
	SDL_atomic_t				size_exceeded;
	downloadmodtransfer_t		transfer;
} downloadmodinstall;

extern qboolean stop_curl_download;
extern qboolean curl_download_active;

static qboolean M_DownloadMods_InstallDirNameOkay(const char *name)
{
	if (!name || !*name)
		return false;
	if (!strcmp(name, ".") || strstr(name, "..") ||
		strchr(name, '/') || strchr(name, '\\') || strchr(name, ':'))
		return false;
	return true;
}

static void M_DownloadMods_SetInstallStatus(const char *status)
{
	if (downloadmodinstall.mutex)
		SDL_LockMutex(downloadmodinstall.mutex);
	q_strlcpy(downloadmodinstall.status, status ? status : "",
		sizeof(downloadmodinstall.status));
	if (downloadmodinstall.mutex)
		SDL_UnlockMutex(downloadmodinstall.mutex);
	if (status && *status)
		M_DownloadMods_SetMessage(status);
}

static void M_DownloadMods_SetWorkerStatus(const char *status)
{
	if (downloadmodinstall.mutex)
		SDL_LockMutex(downloadmodinstall.mutex);
	q_strlcpy(downloadmodinstall.status, status ? status : "",
		sizeof(downloadmodinstall.status));
	if (downloadmodinstall.mutex)
		SDL_UnlockMutex(downloadmodinstall.mutex);
}

static void M_DownloadMods_SetWorkerStage(downloadmodinstallstage_t stage)
{
	if (downloadmodinstall.mutex)
		SDL_LockMutex(downloadmodinstall.mutex);
	downloadmodinstall.stage = stage;
	if (downloadmodinstall.mutex)
		SDL_UnlockMutex(downloadmodinstall.mutex);
}

static void M_DownloadMods_SetWorkerPartIndex(int part_index)
{
	if (downloadmodinstall.mutex)
		SDL_LockMutex(downloadmodinstall.mutex);
	downloadmodinstall.part_index = part_index;
	if (downloadmodinstall.mutex)
		SDL_UnlockMutex(downloadmodinstall.mutex);
}

static int M_DownloadMods_CopyPartIndex(void)
{
	int part_index;

	if (downloadmodinstall.mutex)
		SDL_LockMutex(downloadmodinstall.mutex);
	part_index = downloadmodinstall.part_index;
	if (downloadmodinstall.mutex)
		SDL_UnlockMutex(downloadmodinstall.mutex);

	return part_index;
}

static qboolean M_DownloadMods_CopyInstallStateForItem(const downloadmoditem_t *item,
	downloadmodinstallstage_t *stage, char *status, size_t status_size,
	qboolean *downloading)
{
	qboolean active = false;

	if (status && status_size)
		status[0] = '\0';
	if (downloading)
		*downloading = false;
	if (!item)
		return false;

	if (downloadmodinstall.mutex)
		SDL_LockMutex(downloadmodinstall.mutex);
	active = downloadmodinstall.active &&
		!q_strcasecmp(downloadmodinstall.item.id, item->id);
	if (active)
	{
		if (stage)
			*stage = downloadmodinstall.stage;
		if (status && status_size)
			q_strlcpy(status, downloadmodinstall.status, status_size);
		if (downloading)
			*downloading = downloadmodinstall.transfer.downloading;
	}
	if (downloadmodinstall.mutex)
		SDL_UnlockMutex(downloadmodinstall.mutex);

	return active;
}

static void M_DownloadMods_FormatSize(double bytes, char *out, size_t outsize)
{
	double kb, mb;

	if (bytes < 0.0)
		bytes = 0.0;
	kb = bytes / 1024.0;
	mb = bytes / (1024.0 * 1024.0);

	if (mb >= 1.0)
		q_snprintf(out, outsize, "%.2f mb", mb);
	else if (kb >= 1.0)
		q_snprintf(out, outsize, "%.0f kb", kb);
	else
		q_snprintf(out, outsize, "%.0f bytes", bytes);
}

/* Resolve the GitHub Releases asset-download prefix for the configured download
 * repo (cl_web_download_url[2]). Either output may be NULL. Returns false when
 * neither web-download slot is a GitHub repo path, meaning the menu has no
 * source for release assets. */
static qboolean M_DownloadMods_RepoUrls(char *api_url, size_t api_sz,
	char *asset_prefix, size_t prefix_sz)
{
	char repo[DOWNLOAD_MODS_MAX_URL];

	if (!CL_ModDownloadRepo(repo, sizeof(repo)))
		return false;

	return CL_GithubReleasesUrls(repo,
		api_url, api_sz, asset_prefix, prefix_sz);
}

static qboolean M_DownloadMods_ManifestUrl(char *out, size_t outsize)
{
	char repo[DOWNLOAD_MODS_MAX_URL];

	if (!CL_ModDownloadRepo(repo, sizeof(repo)))
		return false;

	return CL_GithubRawFileUrl(repo, DOWNLOAD_MODS_MANIFEST_PATH, out, outsize);
}

static qboolean M_DownloadMods_UrlAllowed(const char *url)
{
	return url && !q_strncasecmp(url, "https://", 8);
}

/* Accept only HTTPS downloads that belong to the configured repo: either a
 * release asset (github.com/<repo>/releases/download/) or a tree asset
 * (raw.githubusercontent.com/<repo>/), used for the mods/ directory. */
static qboolean M_DownloadMods_RepoAssetUrlAllowed(const char *url)
{
	char repo[DOWNLOAD_MODS_MAX_URL];
	char prefix[DOWNLOAD_MODS_MAX_URL];

	if (!M_DownloadMods_UrlAllowed(url))
		return false;

	if (!CL_ModDownloadRepo(repo, sizeof(repo)))
		return false;

	if (M_DownloadMods_RepoUrls(NULL, 0, prefix, sizeof(prefix)))
	{
		size_t prefix_len = strlen(prefix);
		if (!q_strncasecmp(url, prefix, prefix_len) && url[prefix_len])
			return true;
	}

	if (CL_GithubRawPrefix(repo, prefix, sizeof(prefix)))
	{
		size_t prefix_len = strlen(prefix);
		if (!q_strncasecmp(url, prefix, prefix_len) && url[prefix_len])
			return true;
	}

	return false;
}

static void M_DownloadMods_FileNameFromUrl(const char *url, char *out, size_t outsize)
{
	const char *base, *end;
	size_t len;

	if (!url || !*url)
	{
		q_strlcpy(out, "download.zip", outsize);
		return;
	}

	base = COM_SkipPath(url);
	end = base;
	while (*end && *end != '?' && *end != '#')
		end++;
	len = (size_t)(end - base);
	if (len <= 0)
		q_strlcpy(out, "download.zip", outsize);
	else
	{
		if (len >= outsize)
			len = outsize - 1;
		memcpy(out, base, len);
		out[len] = '\0';
	}
}

static qboolean M_DownloadMods_BuildTempPath(const char *suffix, char *out, size_t outsize)
{
	char safe_suffix[MAX_QPATH];
	char path_copy[MAX_OSPATH];
	const char *s;
	char *d;
	size_t len;
	int part_index;

	for (s = suffix, d = safe_suffix; s && *s && d < safe_suffix + sizeof(safe_suffix) - 1; s++)
	{
		if (*s == '/' || *s == '\\' || *s == ':' || *s == '?' || *s == '#')
			*d++ = '_';
		else
			*d++ = *s;
	}
	*d = '\0';
	if (!safe_suffix[0])
		q_strlcpy(safe_suffix, "download.tmp", sizeof(safe_suffix));

	part_index = M_DownloadMods_CopyPartIndex();
	len = q_snprintf(out, outsize, "%s/qssm-downloads/%s-%d-%d-%s",
		com_basedir, downloadmodinstall.item.id, host_framecount,
		part_index, safe_suffix);
	if (len >= outsize)
		return false;

	q_strlcpy(path_copy, out, sizeof(path_copy));
	COM_CreatePath(path_copy);
	return true;
}

static qboolean M_DownloadMods_BuildStageDir(void)
{
	size_t len;

	len = q_snprintf(downloadmodinstall.stage_dir,
		sizeof(downloadmodinstall.stage_dir),
		"%s/qssm-downloads/%s-stage-%d",
		com_basedir, downloadmodinstall.item.id, host_framecount);
	if (len >= sizeof(downloadmodinstall.stage_dir))
	{
		downloadmodinstall.stage_dir[0] = '\0';
		return false;
	}
	return true;
}

static qboolean M_DownloadMods_EnsureMutex(void)
{
	if (downloadmodinstall.mutex)
		return true;

	downloadmodinstall.mutex = SDL_CreateMutex();
	if (!downloadmodinstall.mutex)
	{
		Con_Printf("Unable to initialize mod download mutex: %s\n", SDL_GetError());
		return false;
	}
	return true;
}

static size_t M_DownloadMods_WriteData(void *ptr, size_t size, size_t nmemb, void *stream)
{
	return fwrite(ptr, size, nmemb, (FILE *)stream) * size;
}

static int M_DownloadMods_ProgressCallback(void *clientp, curl_off_t dltotal,
	curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	curl_off_t max_bytes = 0;

	(void)clientp;
	(void)ultotal;
	(void)ulnow;

	if (stop_curl_download || SDL_AtomicGet(&downloadmodinstall.abort_requested))
		return 1;

	if (downloadmodinstall.mutex)
	{
		SDL_LockMutex(downloadmodinstall.mutex);
		max_bytes = downloadmodinstall.transfer.max_bytes;
		downloadmodinstall.transfer.received = (double)dlnow;
		downloadmodinstall.transfer.total = (double)dltotal;
		SDL_UnlockMutex(downloadmodinstall.mutex);
	}

	if (max_bytes > 0 &&
		((dltotal > 0 && dltotal > max_bytes) || dlnow > max_bytes))
	{
		SDL_AtomicSet(&downloadmodinstall.size_exceeded, 1);
		return 1;
	}

	return 0;
}

static qboolean M_DownloadMods_CancelRequested(void)
{
	return stop_curl_download || SDL_AtomicGet(&downloadmodinstall.abort_requested);
}

static void M_DownloadMods_BeginSharedDownloadProgress(const char *display_name);

static qboolean M_DownloadMods_RunTransfer(const char *url, const char *temp_path,
	const char *display_name, curl_off_t max_bytes, qofs_t *file_size,
	qboolean *aborted, char *error, size_t error_size)
{
	qboolean success = false;
	long response_code = 0;
	qofs_t local_file_size = 0;
	CURLcode result = CURLE_OK;
	FILE *fp;
	CURL *curl;
	qboolean write_failed;

	if (file_size)
		*file_size = 0;
	if (aborted)
		*aborted = false;
	if (error && error_size > 0)
		error[0] = '\0';

	if (!M_DownloadMods_UrlAllowed(url))
	{
		q_snprintf(error, error_size, "Invalid download URL");
		return false;
	}

	SDL_LockMutex(downloadmodinstall.mutex);
	downloadmodinstall.transfer.downloading = true;
	downloadmodinstall.transfer.received = 0.0;
	downloadmodinstall.transfer.total = 0.0;
	downloadmodinstall.transfer.file_size = 0;
	downloadmodinstall.transfer.response_code = 0;
	downloadmodinstall.transfer.curl_result = CURLE_OK;
	downloadmodinstall.transfer.max_bytes = max_bytes;
	q_strlcpy(downloadmodinstall.transfer.url, url, sizeof(downloadmodinstall.transfer.url));
	q_strlcpy(downloadmodinstall.transfer.temp_path, temp_path,
		sizeof(downloadmodinstall.transfer.temp_path));
	q_strlcpy(downloadmodinstall.transfer.display_name, display_name,
		sizeof(downloadmodinstall.transfer.display_name));
	downloadmodinstall.transfer.error[0] = '\0';
	SDL_AtomicSet(&downloadmodinstall.size_exceeded, 0);
	SDL_UnlockMutex(downloadmodinstall.mutex);

	/* cls.download (read by the renderer) is owned by the main thread; the
	 * Frame mirror initializes it when it observes this transfer start. */

	fp = fopen(temp_path, "wb");
	if (!fp)
	{
		q_snprintf(error, error_size, "Unable to open temp file: %s", strerror(errno));
		goto done;
	}

	curl = curl_easy_init();
	if (!curl)
	{
		fclose(fp);
		q_strlcpy(error, "Unable to initialize curl", error_size);
		goto done;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 1024L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, M_DownloadMods_WriteData);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, M_DownloadMods_ProgressCallback);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 0L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 500L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);
	if (max_bytes > 0)
		curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, max_bytes);
	M_Update_CurlOptions(curl);

	result = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	write_failed = (fflush(fp) != 0 || ferror(fp));
	if (fseek(fp, 0, SEEK_END) == 0)
		local_file_size = ftell(fp);
	if (local_file_size < 0)
		local_file_size = 0;

	fclose(fp);
	curl_easy_cleanup(curl);

	if (write_failed && result == CURLE_OK)
		result = CURLE_WRITE_ERROR;

	if (SDL_AtomicGet(&downloadmodinstall.size_exceeded) ||
		result == CURLE_FILESIZE_EXCEEDED)
	{
		char sizeStr[32];
		M_DownloadMods_FormatSize(max_bytes, sizeStr, sizeof(sizeStr));
		q_snprintf(error, error_size, "Download exceeded %s limit", sizeStr);
		unlink(temp_path);
	}
	else if (M_DownloadMods_CancelRequested() ||
		result == CURLE_ABORTED_BY_CALLBACK)
	{
		if (aborted)
			*aborted = true;
		q_strlcpy(error, "Download cancelled", error_size);
		unlink(temp_path);
	}
	else if (result == CURLE_OK && response_code >= 200 && response_code < 300)
	{
		success = true;
	}
	else if (result != CURLE_OK)
	{
		q_snprintf(error, error_size, "%s", curl_easy_strerror(result));
		unlink(temp_path);
	}
	else
	{
		q_snprintf(error, error_size, "HTTP %ld", response_code);
		unlink(temp_path);
	}

done:
	SDL_LockMutex(downloadmodinstall.mutex);
	downloadmodinstall.transfer.downloading = false;
	downloadmodinstall.transfer.response_code = response_code;
	downloadmodinstall.transfer.file_size = local_file_size;
	downloadmodinstall.transfer.curl_result = result;
	if (error)
		q_strlcpy(downloadmodinstall.transfer.error, error,
			sizeof(downloadmodinstall.transfer.error));
	SDL_UnlockMutex(downloadmodinstall.mutex);

	if (file_size)
		*file_size = local_file_size;
	return success;
}

static void M_DownloadMods_BeginSharedDownloadProgress(const char *display_name)
{
	cls.download.active = true;
	cls.download.chunked = false;
	cls.download.completedbytes = 0;
	cls.download.ratebytes = 0;
	cls.download.rate = 0;
	cls.download.ratetime = realtime;
	cls.download.chunkedstaleuntil = 0;
	cls.download.percent = -1.0f;
	cls.download.received = 0.0;
	cls.download.total = 0.0;
	cls.download.starttime = (float)realtime;
	cls.download.current[0] = '\0';
	q_strlcpy(cls.download.current, display_name, sizeof(cls.download.current));
}

static int M_DownloadMods_InstallThread(void *unused);

static qboolean M_DownloadMods_StartWorker(void)
{
	SDL_Thread *thread;

	if (cls.download.active || curl_download_active)
	{
		M_DownloadMods_SetInstallStatus("another download is active");
		Con_Printf("A download is already active\n");
		return false;
	}

	if (!M_DownloadMods_EnsureMutex())
		return false;

	SDL_LockMutex(downloadmodinstall.mutex);
	memset(&downloadmodinstall.transfer, 0, sizeof(downloadmodinstall.transfer));
	downloadmodinstall.transfer.active = true;
	SDL_AtomicSet(&downloadmodinstall.abort_requested, 0);
	SDL_AtomicSet(&downloadmodinstall.size_exceeded, 0);
	SDL_UnlockMutex(downloadmodinstall.mutex);

	stop_curl_download = false;
	curl_download_active = true;

	thread = SDL_CreateThread(M_DownloadMods_InstallThread, "ModInstall", NULL);
	if (!thread)
	{
		SDL_LockMutex(downloadmodinstall.mutex);
		downloadmodinstall.transfer.active = false;
		SDL_UnlockMutex(downloadmodinstall.mutex);
		cls.download.active = false;
		curl_download_active = false;
		q_snprintf(downloadmodinstall.status, sizeof(downloadmodinstall.status),
			"install thread failed");
		Con_Printf("Unable to start mod install worker: %s\n", SDL_GetError());
		return false;
	}

	SDL_LockMutex(downloadmodinstall.mutex);
	downloadmodinstall.transfer.thread = thread;
	SDL_UnlockMutex(downloadmodinstall.mutex);
	return true;
}

static qboolean M_DownloadMods_SHA256StringOkay(const char *sha)
{
	int i;

	if (!sha || !*sha)
		return true;
	if (strlen(sha) != 64)
		return false;
	for (i = 0; i < 64; i++)
		if (!q_isxdigit((unsigned char)sha[i]))
			return false;
	return true;
}

static qboolean M_DownloadMods_VerifySHA256(const char *path, const char *expected)
{
	if (!M_DownloadMods_SHA256StringOkay(expected))
		return false;

	return M_VerifySHA256File(path, expected, true,
		M_DownloadMods_CancelRequested);
}

static qboolean M_DownloadMods_PathLooksInstalled(const char *path)
{
	char check[MAX_OSPATH];
	int pak_num;

	if ((size_t)q_snprintf(check, sizeof(check), "%s/progs.dat", path) <
		sizeof(check) && (Sys_FileType(check) & FS_ENT_FILE))
		return true;

	/* A maps/ directory marks a playable mappack even without progs/paks. */
	if ((size_t)q_snprintf(check, sizeof(check), "%s/maps", path) <
		sizeof(check) && (Sys_FileType(check) & FS_ENT_DIRECTORY))
		return true;

	for (pak_num = 0; pak_num < 10; pak_num++)
	{
		if ((size_t)q_snprintf(check, sizeof(check), "%s/pak%d.pak",
			path, pak_num) < sizeof(check) &&
			(Sys_FileType(check) & FS_ENT_FILE))
			return true;
		if ((size_t)q_snprintf(check, sizeof(check), "%s/pak%d.pk3",
			path, pak_num) < sizeof(check) &&
			(Sys_FileType(check) & FS_ENT_FILE))
			return true;
	}

	for (pak_num = 0; pak_num < 10; pak_num++)
	{
		if ((size_t)q_snprintf(check, sizeof(check), "%s/paks/pak%d.pak",
			path, pak_num) < sizeof(check) &&
			(Sys_FileType(check) & FS_ENT_FILE))
			return true;
		if ((size_t)q_snprintf(check, sizeof(check), "%s/paks/pak%d.pk3",
			path, pak_num) < sizeof(check) &&
			(Sys_FileType(check) & FS_ENT_FILE))
			return true;
	}

	return false;
}

static qboolean M_DownloadMods_ResolvedUnderMods(const char *resolved)
{
	return resolved &&
		(!q_strncasecmp(resolved, "mods/", 5) ||
		 !q_strncasecmp(resolved, "mods\\", 5));
}

static qboolean M_DownloadMods_ModListNameCounts(const char *name)
{
	return name && *name &&
		q_strcasecmp(name, GAMENAME) &&
		q_strcasecmp(name, "id1");
}

static qboolean M_DownloadMods_PreferModsFolder(void)
{
	filelist_item_t *item;
	int mods_count = 0;
	int other_count = 0;

	/* Keep installs predictable on mixed layouts: prefer mods/ only when
	 * most existing non-id game dirs already resolve there. */
	for (item = modlist; item; item = item->next)
	{
		char resolved[MAX_OSPATH];

		if (!M_DownloadMods_ModListNameCounts(item->name))
			continue;
		if (!COM_ResolveGameDir(item->name, resolved, sizeof(resolved)))
			continue;

		if (M_DownloadMods_ResolvedUnderMods(resolved))
			mods_count++;
		else
			other_count++;
	}

	return mods_count > other_count;
}

static qboolean M_DownloadMods_BuildInstallTargetForItem(
	const downloadmoditem_t *item, char *target, size_t target_size)
{
	if (!item || !M_DownloadMods_InstallDirNameOkay(item->install_dir))
		return false;

	if (M_DownloadMods_PreferModsFolder())
	{
		char mods_dir[MAX_OSPATH];

		if ((size_t)q_snprintf(mods_dir, sizeof(mods_dir),
			"%s/mods", com_basedir) >= sizeof(mods_dir))
			return false;
		Sys_mkdir(mods_dir);
		if (!(Sys_FileType(mods_dir) & FS_ENT_DIRECTORY))
			return false;

		return (size_t)q_snprintf(target, target_size, "%s/%s",
			mods_dir, item->install_dir) < target_size;
	}

	return (size_t)q_snprintf(target, target_size, "%s/%s",
		com_basedir, item->install_dir) < target_size;
}

static qboolean M_DownloadMods_BuildInstallTarget(char *target, size_t target_size)
{
	return M_DownloadMods_BuildInstallTargetForItem(&downloadmodinstall.item,
		target, target_size);
}

static qboolean M_DownloadMods_TargetExists(const downloadmoditem_t *item,
	qboolean *looks_installed)
{
	char target[MAX_OSPATH];
	int target_type;

	if (looks_installed)
		*looks_installed = false;
	if (!M_DownloadMods_BuildInstallTargetForItem(item, target, sizeof(target)))
		return false;

	target_type = Sys_FileType(target);
	if (!(target_type & (FS_ENT_FILE | FS_ENT_DIRECTORY)))
		return false;

	if (looks_installed && (target_type & FS_ENT_DIRECTORY))
		*looks_installed = M_DownloadMods_PathLooksInstalled(target);
	return true;
}

static qboolean M_DownloadMods_Rmdir(const char *path)
{
#ifdef _WIN32
	return _rmdir(path) == 0 || errno == ENOENT;
#else
	return rmdir(path) == 0 || errno == ENOENT;
#endif
}

static qboolean M_DownloadMods_PathInStagingRoot(const char *path)
{
	char staging_root[MAX_OSPATH];
	size_t root_len;

	if (!path || !*path)
		return false;

	if ((size_t)q_snprintf(staging_root, sizeof(staging_root),
		"%s/qssm-downloads/", com_basedir) >= sizeof(staging_root))
		return false;

	root_len = strlen(staging_root);
#ifdef _WIN32
	return !q_strncasecmp(path, staging_root, root_len);
#else
	return !strncmp(path, staging_root, root_len);
#endif
}

static qboolean M_DownloadMods_RemoveTree(const char *path)
{
#ifdef _WIN32
	int type;
#else
	struct stat st;
#endif

	if (!M_DownloadMods_PathInStagingRoot(path))
		return false;

#ifdef _WIN32
	type = Sys_FileType(path);
	if (type & FS_ENT_FILE)
		return remove(path) == 0 || errno == ENOENT;
	if (!(type & FS_ENT_DIRECTORY))
		return true;
#else
	if (lstat(path, &st) != 0)
		return errno == ENOENT;
	if (!S_ISDIR(st.st_mode))
		return remove(path) == 0 || errno == ENOENT;
#endif

#ifdef _WIN32
	{
		WIN32_FIND_DATA fdat;
		HANDLE fhnd;
		char searchpath[MAX_OSPATH];

		if ((size_t)q_snprintf(searchpath, sizeof(searchpath), "%s/*", path) >=
			sizeof(searchpath))
			return false;

		fhnd = FindFirstFile(searchpath, &fdat);
		if (fhnd != INVALID_HANDLE_VALUE)
		{
			do
			{
				char child[MAX_OSPATH];

				if (!strcmp(fdat.cFileName, ".") || !strcmp(fdat.cFileName, ".."))
					continue;
				if ((size_t)q_snprintf(child, sizeof(child), "%s/%s",
					path, fdat.cFileName) >= sizeof(child))
				{
					FindClose(fhnd);
					return false;
				}
				if ((fdat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
					!(fdat.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
				{
					if (!M_DownloadMods_RemoveTree(child))
					{
						FindClose(fhnd);
						return false;
					}
				}
				else if (remove(child) != 0 && errno != ENOENT)
				{
					FindClose(fhnd);
					return false;
				}
			} while (FindNextFile(fhnd, &fdat));

			FindClose(fhnd);
		}
	}
#else
	{
		DIR *dir_p;
		struct dirent *dir_t;

		dir_p = opendir(path);
		if (dir_p)
		{
			while ((dir_t = readdir(dir_p)) != NULL)
			{
				char child[MAX_OSPATH];

				if (!strcmp(dir_t->d_name, ".") || !strcmp(dir_t->d_name, ".."))
					continue;
				if ((size_t)q_snprintf(child, sizeof(child), "%s/%s",
					path, dir_t->d_name) >= sizeof(child))
				{
					closedir(dir_p);
					return false;
				}
				if (!M_DownloadMods_RemoveTree(child))
				{
					closedir(dir_p);
					return false;
				}
			}
			closedir(dir_p);
		}
	}
#endif

	return M_DownloadMods_Rmdir(path);
}

static qboolean M_DownloadMods_FindNestedInstallDir(char *out, size_t outsize)
{
#ifdef _WIN32
	WIN32_FIND_DATA fdat;
	HANDLE fhnd;
	char searchpath[MAX_OSPATH];

	if ((size_t)q_snprintf(searchpath, sizeof(searchpath), "%s/*",
		downloadmodinstall.stage_dir) >= sizeof(searchpath))
		return false;
	fhnd = FindFirstFile(searchpath, &fdat);
	if (fhnd == INVALID_HANDLE_VALUE)
		return false;

	do
	{
		char candidate[MAX_OSPATH];

		if (!(fdat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;
		if (!strcmp(fdat.cFileName, ".") || !strcmp(fdat.cFileName, ".."))
			continue;

		if ((size_t)q_snprintf(candidate, sizeof(candidate), "%s/%s/%s",
			downloadmodinstall.stage_dir, fdat.cFileName,
			downloadmodinstall.item.install_dir) >= sizeof(candidate))
			continue;
		if (Sys_FileType(candidate) & FS_ENT_DIRECTORY)
		{
			FindClose(fhnd);
			q_strlcpy(out, candidate, outsize);
			return true;
		}
	} while (FindNextFile(fhnd, &fdat));

	FindClose(fhnd);
#else
	DIR *dir_p;
	struct dirent *dir_t;

	dir_p = opendir(downloadmodinstall.stage_dir);
	if (!dir_p)
		return false;

	while ((dir_t = readdir(dir_p)) != NULL)
	{
		char child[MAX_OSPATH];
		char candidate[MAX_OSPATH];
		struct stat st;

		if (!strcmp(dir_t->d_name, ".") || !strcmp(dir_t->d_name, ".."))
			continue;

		if ((size_t)q_snprintf(child, sizeof(child), "%s/%s",
			downloadmodinstall.stage_dir, dir_t->d_name) >= sizeof(child))
			continue;
		if (stat(child, &st) != 0 || !S_ISDIR(st.st_mode))
			continue;

		if ((size_t)q_snprintf(candidate, sizeof(candidate), "%s/%s",
			child, downloadmodinstall.item.install_dir) >= sizeof(candidate))
			continue;
		if (Sys_FileType(candidate) & FS_ENT_DIRECTORY)
		{
			closedir(dir_p);
			q_strlcpy(out, candidate, outsize);
			return true;
		}
	}

	closedir(dir_p);
#endif
	return false;
}

static qboolean M_DownloadMods_FindSingleInstalledDir(char *out, size_t outsize)
{
	char found[MAX_OSPATH] = "";
	int matches = 0;
#ifdef _WIN32
	WIN32_FIND_DATA fdat;
	HANDLE fhnd;
	char searchpath[MAX_OSPATH];

	if ((size_t)q_snprintf(searchpath, sizeof(searchpath), "%s/*",
		downloadmodinstall.stage_dir) >= sizeof(searchpath))
		return false;
	fhnd = FindFirstFile(searchpath, &fdat);
	if (fhnd == INVALID_HANDLE_VALUE)
		return false;

	do
	{
		char child[MAX_OSPATH];

		if (!(fdat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
			(fdat.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
			continue;
		if (!strcmp(fdat.cFileName, ".") || !strcmp(fdat.cFileName, ".."))
			continue;

		if ((size_t)q_snprintf(child, sizeof(child), "%s/%s",
			downloadmodinstall.stage_dir, fdat.cFileName) >= sizeof(child))
		{
			FindClose(fhnd);
			return false;
		}
		if (M_DownloadMods_PathLooksInstalled(child))
		{
			if (++matches == 1)
				q_strlcpy(found, child, sizeof(found));
			else
				break;
		}
	} while (FindNextFile(fhnd, &fdat));

	FindClose(fhnd);
#else
	DIR *dir_p;
	struct dirent *dir_t;

	dir_p = opendir(downloadmodinstall.stage_dir);
	if (!dir_p)
		return false;

	while ((dir_t = readdir(dir_p)) != NULL)
	{
		char child[MAX_OSPATH];
		struct stat st;

		if (!strcmp(dir_t->d_name, ".") || !strcmp(dir_t->d_name, ".."))
			continue;

		if ((size_t)q_snprintf(child, sizeof(child), "%s/%s",
			downloadmodinstall.stage_dir, dir_t->d_name) >= sizeof(child))
		{
			closedir(dir_p);
			return false;
		}
		if (stat(child, &st) != 0 || !S_ISDIR(st.st_mode))
			continue;

		if (M_DownloadMods_PathLooksInstalled(child))
		{
			if (++matches == 1)
				q_strlcpy(found, child, sizeof(found));
			else
				break;
		}
	}

	closedir(dir_p);
#endif

	if (matches == 1)
	{
		q_strlcpy(out, found, outsize);
		return true;
	}

	return false;
}

static void M_DownloadMods_RemoveTempFiles(void)
{
	size_t i;

	for (i = 0; i < VEC_SIZE(downloadmodinstall.parts); i++)
	{
		if (downloadmodinstall.parts[i].temp_path[0] &&
			M_DownloadMods_PathInStagingRoot(downloadmodinstall.parts[i].temp_path))
		{
			unlink(downloadmodinstall.parts[i].temp_path);
			downloadmodinstall.parts[i].temp_path[0] = '\0';
		}
	}

	if (downloadmodinstall.joined_zip[0] &&
		M_DownloadMods_PathInStagingRoot(downloadmodinstall.joined_zip))
	{
		unlink(downloadmodinstall.joined_zip);
		downloadmodinstall.joined_zip[0] = '\0';
	}
}

static void M_DownloadMods_FinishInstall(qboolean success, const char *message)
{
	if (success)
		S_LocalSound("misc/menu2.wav");
	else
	{
		if (downloadmodinstall.stage_dir[0])
			M_DownloadMods_RemoveTree(downloadmodinstall.stage_dir);
		S_LocalSound("misc/menu3.wav");
	}

	M_DownloadMods_RemoveTempFiles();
	M_DownloadMods_SetInstallStatus(message);
	downloadmodinstall.active = false;
	M_DownloadMods_SetWorkerStage(DOWNLOADMOD_INSTALL_NONE);
	M_DownloadMods_SetWorkerPartIndex(0);
	VEC_CLEAR(downloadmodinstall.parts);
	if (success)
		M_DownloadMods_RefreshInstalledCache();
	M_DownloadMods_Refilter();
}

static qboolean M_DownloadMods_FinalizeStagedInstall(void)
{
	char target[MAX_OSPATH];
	char candidate[MAX_OSPATH];
	char source[MAX_OSPATH];
	qboolean source_is_stage = false;
	int target_type;

	if (!M_DownloadMods_InstallDirNameOkay(downloadmodinstall.item.install_dir))
	{
		Con_Printf("Invalid install directory for %s: %s\n",
			downloadmodinstall.item.name, downloadmodinstall.item.install_dir);
		return false;
	}

	if (!M_DownloadMods_BuildInstallTarget(target, sizeof(target)))
		return false;

	target_type = Sys_FileType(target);
	if (target_type & (FS_ENT_FILE | FS_ENT_DIRECTORY))
	{
		Con_Printf("Install target already exists: %s\n", target);
		return false;
	}

	if ((size_t)q_snprintf(candidate, sizeof(candidate), "%s/%s",
		downloadmodinstall.stage_dir,
		downloadmodinstall.item.install_dir) >= sizeof(candidate))
		return false;
	if (Sys_FileType(candidate) & FS_ENT_DIRECTORY)
		q_strlcpy(source, candidate, sizeof(source));
	else if (M_DownloadMods_FindNestedInstallDir(source, sizeof(source)))
	{
		/* source was filled by helper */
	}
	else if (M_DownloadMods_FindSingleInstalledDir(source, sizeof(source)))
	{
		/* source was filled by helper */
	}
	else if (M_DownloadMods_PathLooksInstalled(downloadmodinstall.stage_dir))
	{
		q_strlcpy(source, downloadmodinstall.stage_dir, sizeof(source));
		source_is_stage = true;
	}
	else
	{
		Con_Printf("Archive for %s did not contain expected mod directory \"%s\"\n",
			downloadmodinstall.item.name, downloadmodinstall.item.install_dir);
		return false;
	}

	if (rename(source, target) != 0)
	{
		Con_Printf("Failed to move installed mod to %s: %s\n",
			target, strerror(errno));
		return false;
	}

	if (!source_is_stage)
		M_DownloadMods_RemoveTree(downloadmodinstall.stage_dir);

	FileList_Add(downloadmodinstall.item.install_dir, NULL, &modlist);
	Con_Printf("Installed %s to %s\n", downloadmodinstall.item.name, target);
	return true;
}

static qboolean M_DownloadMods_AddPart(const char *url,
	const char *sha256, const char *filename)
{
	downloadmodpart_t part;

	if (!url || !*url || !M_DownloadMods_UrlAllowed(url))
		return false;
	/* A hash is optional (auto-discovered releases have none); but if one is
	 * supplied it must be well formed. */
	if (sha256 && *sha256 && !M_DownloadMods_SHA256StringOkay(sha256))
	{
		return false;
	}

	memset(&part, 0, sizeof(part));
	if (q_strlcpy(part.url, url, sizeof(part.url)) >= sizeof(part.url))
		return false;
	if (sha256)
		q_strlcpy(part.sha256, sha256, sizeof(part.sha256));
	if (filename && *filename)
		q_strlcpy(part.filename, filename, sizeof(part.filename));
	else
		M_DownloadMods_FileNameFromUrl(url, part.filename, sizeof(part.filename));

	VEC_PUSH(downloadmodinstall.parts, part);
	return true;
}

static qboolean M_DownloadMods_AddManifestPart(const char *url,
	const char *sha256, const char *filename)
{
	if (!url || !*url || !M_DownloadMods_RepoAssetUrlAllowed(url))
		return false;

	return M_DownloadMods_AddPart(url, sha256, filename);
}

static qboolean M_DownloadMods_ParseManifestArray(const jsonentry_t *array,
	const char *base_url)
{
	const jsonentry_t *entry;
	qboolean added = false;
	qboolean rejected = false;

	if (!array || array->type != JSON_ARRAY)
		return false;

	for (entry = array->firstchild; entry; entry = entry->next)
	{
		const char *url, *sha, *filename;
		char joined_url[DOWNLOAD_MODS_MAX_URL];

		if (entry->type != JSON_OBJECT)
		{
			rejected = true;
			continue;
		}

		url = JSON_FindString(entry, "url");
		if (!url)
			url = JSON_FindString(entry, "download_url");
		if (!url)
			url = JSON_FindString(entry, "browser_download_url");
		sha = JSON_FindString(entry, "sha256");
		if (!sha)
			sha = JSON_FindString(entry, "sha-256");
		filename = JSON_FindString(entry, "filename");
		if (!filename)
			filename = JSON_FindString(entry, "name");

		if ((!url || !*url) && base_url && *base_url && filename && *filename)
		{
			if ((size_t)q_snprintf(joined_url, sizeof(joined_url), "%s%s%s",
				base_url, (base_url[strlen(base_url) - 1] == '/') ? "" : "/",
				filename) >= sizeof(joined_url))
			{
				rejected = true;
				continue;
			}
			url = joined_url;
		}

		if (M_DownloadMods_AddManifestPart(url, sha, filename))
			added = true;
		else
			rejected = true;
	}

	if (rejected)
	{
		VEC_CLEAR(downloadmodinstall.parts);
		return false;
	}

	return added;
}

static qboolean M_DownloadMods_ParseManifest(const char *path)
{
	FILE *f;
	long len;
	char *text;
	json_t *json;
	const jsonentry_t *array;
	const char *base_url;
	qboolean ok = false;

	f = fopen(path, "rb");
	if (!f)
		return false;
	if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 ||
		fseek(f, 0, SEEK_SET) != 0)
	{
		fclose(f);
		return false;
	}
	if (len > 1024 * 1024)
	{
		fclose(f);
		return false;
	}

	text = (char *)malloc((size_t)len + 1);
	if (!text)
	{
		fclose(f);
		return false;
	}
	if (fread(text, 1, (size_t)len, f) != (size_t)len)
	{
		free(text);
		fclose(f);
		return false;
	}
	text[len] = '\0';
	fclose(f);

	json = JSON_Parse(text);
	free(text);
	if (!json || !json->root || json->root->type != JSON_OBJECT)
		goto cleanup;

	base_url = JSON_FindString(json->root, "base_url");
	if (!base_url)
		base_url = JSON_FindString(json->root, "url_base");

	array = JSON_Find(json->root, "parts", JSON_ARRAY);
	ok = M_DownloadMods_ParseManifestArray(array, base_url);
	if (!ok)
	{
		array = JSON_Find(json->root, "files", JSON_ARRAY);
		ok = M_DownloadMods_ParseManifestArray(array, base_url);
	}
	if (!ok)
	{
		array = JSON_Find(json->root, "archives", JSON_ARRAY);
		ok = M_DownloadMods_ParseManifestArray(array, base_url);
	}
	if (!ok)
	{
		array = JSON_Find(json->root, "downloads", JSON_ARRAY);
		ok = M_DownloadMods_ParseManifestArray(array, base_url);
	}

cleanup:
	if (json)
		JSON_Free(json);
	return ok;
}

static qboolean M_DownloadMods_JoinSplitParts(const char *joined_path)
{
	byte buffer[65536];
	FILE *out;
	size_t i;

	out = fopen(joined_path, "wb");
	if (!out)
	{
		return false;
	}

	for (i = 0; i < VEC_SIZE(downloadmodinstall.parts); i++)
	{
		FILE *in;
		size_t readbytes;

		if (!downloadmodinstall.parts[i].temp_path[0])
		{
			fclose(out);
			unlink(joined_path);
			return false;
		}

		in = fopen(downloadmodinstall.parts[i].temp_path, "rb");
		if (!in)
		{
			fclose(out);
			unlink(joined_path);
			return false;
		}

		while ((readbytes = fread(buffer, 1, sizeof(buffer), in)) > 0)
			{
				if (M_DownloadMods_CancelRequested())
				{
					fclose(in);
					fclose(out);
					unlink(joined_path);
					return false;
				}
				if (fwrite(buffer, 1, readbytes, out) != readbytes)
				{
					fclose(in);
					fclose(out);
					unlink(joined_path);
					return false;
				}
			}

		if (ferror(in))
		{
			fclose(in);
			fclose(out);
			unlink(joined_path);
			return false;
		}

		fclose(in);
	}

	if (fflush(out) != 0 || ferror(out))
	{
		fclose(out);
		unlink(joined_path);
		return false;
	}

	fclose(out);
	return true;
}

static qboolean M_DownloadMods_WorkerCancelled(char *error, size_t error_size,
	qboolean *aborted)
{
	if (!M_DownloadMods_CancelRequested())
		return false;

	if (aborted)
		*aborted = true;
	q_strlcpy(error, "Download cancelled", error_size);
	return true;
}

static void M_DownloadMods_CopyExtractError(char *error, size_t error_size)
{
	const char *zip_error = ZIP_ExtractError();

	if (zip_error && zip_error[0])
		q_strlcpy(error, zip_error, error_size);
	else
		q_strlcpy(error, "extract failed", error_size);
}

static qboolean M_DownloadMods_RunArchivePart(downloadmodpart_t *part,
	char *error, size_t error_size, qboolean *aborted)
{
	char temp_path[MAX_OSPATH];
	char status[96];
	curl_off_t max_bytes;
	qofs_t file_size = 0;
	int part_index;

	if (!part)
	{
		q_strlcpy(error, "install state error", error_size);
		return false;
	}

	if (!M_DownloadMods_BuildTempPath(part->filename, temp_path, sizeof(temp_path)))
	{
		q_strlcpy(error, "download path too long", error_size);
		return false;
	}
	q_strlcpy(part->temp_path, temp_path, sizeof(part->temp_path));

	part_index = M_DownloadMods_CopyPartIndex();
	q_snprintf(status, sizeof(status), "downloading %d/%d",
		part_index + 1, (int)VEC_SIZE(downloadmodinstall.parts));
	M_DownloadMods_SetWorkerStatus(status);
	M_DownloadMods_SetWorkerStage(DOWNLOADMOD_INSTALL_ARCHIVE);
	max_bytes = (downloadmodinstall.item.type == DOWNLOADMOD_SPLIT_ZIP) ?
		DOWNLOAD_MODS_MAX_SPLIT_PART_BYTES : DOWNLOAD_MODS_MAX_ARCHIVE_BYTES;

	if (!M_DownloadMods_RunTransfer(part->url, temp_path, part->filename,
		max_bytes, &file_size, aborted, error, error_size))
		return false;

	if (M_DownloadMods_WorkerCancelled(error, error_size, aborted))
		return false;

	if (part->sha256[0])
	{
		M_DownloadMods_SetWorkerStatus("verify");
		if (!M_DownloadMods_VerifySHA256(temp_path, part->sha256))
		{
			unlink(temp_path);
			if (M_DownloadMods_WorkerCancelled(error, error_size, aborted))
				return false;
			q_strlcpy(error, "SHA-256 failed", error_size);
			return false;
		}
	}

	if (downloadmodinstall.item.type == DOWNLOADMOD_SPLIT_ZIP)
		return true;

	if (M_DownloadMods_WorkerCancelled(error, error_size, aborted))
	{
		unlink(temp_path);
		return false;
	}

	M_DownloadMods_SetWorkerStatus("extract");
	if (!ZIP_ExtractQuiet(temp_path, downloadmodinstall.stage_dir))
	{
		unlink(temp_path);
		if (M_DownloadMods_WorkerCancelled(error, error_size, aborted))
			return false;
		M_DownloadMods_CopyExtractError(error, error_size);
		return false;
	}

	if (M_DownloadMods_WorkerCancelled(error, error_size, aborted))
	{
		unlink(temp_path);
		return false;
	}

	unlink(temp_path);
	return true;
}

static qboolean M_DownloadMods_FinalizeSplitZipWorker(char *error,
	size_t error_size, qboolean *aborted)
{
	char joined_path[MAX_OSPATH];
	const char *joined_name = downloadmodinstall.joined_name[0] ?
		downloadmodinstall.joined_name : "download.zip";

	if (!M_DownloadMods_BuildTempPath(joined_name,
		joined_path, sizeof(joined_path)))
	{
		q_strlcpy(error, "joined ZIP path too long", error_size);
		return false;
	}
	q_strlcpy(downloadmodinstall.joined_zip, joined_path,
		sizeof(downloadmodinstall.joined_zip));

	if (M_DownloadMods_WorkerCancelled(error, error_size, aborted))
		return false;

	M_DownloadMods_SetWorkerStatus("joining");
	if (!M_DownloadMods_JoinSplitParts(joined_path))
	{
		if (M_DownloadMods_WorkerCancelled(error, error_size, aborted))
			return false;
		q_strlcpy(error, "join failed", error_size);
		return false;
	}

	if (downloadmodinstall.item.sha256[0])
	{
		M_DownloadMods_SetWorkerStatus("verify");
		if (!M_DownloadMods_VerifySHA256(joined_path, downloadmodinstall.item.sha256))
		{
			if (M_DownloadMods_WorkerCancelled(error, error_size, aborted))
				return false;
			q_strlcpy(error, "ZIP SHA-256 failed", error_size);
			return false;
		}
	}

	if (M_DownloadMods_WorkerCancelled(error, error_size, aborted))
		return false;

	M_DownloadMods_SetWorkerStatus("extract");
	if (!ZIP_ExtractQuiet(joined_path, downloadmodinstall.stage_dir))
	{
		if (M_DownloadMods_WorkerCancelled(error, error_size, aborted))
			return false;
		M_DownloadMods_CopyExtractError(error, error_size);
		return false;
	}

	if (M_DownloadMods_WorkerCancelled(error, error_size, aborted))
		return false;

	return true;
}

static qboolean M_DownloadMods_RunManifestWorker(char *error, size_t error_size,
	qboolean *aborted)
{
	char temp_path[MAX_OSPATH];
	char filename[MAX_QPATH];
	qofs_t file_size = 0;

	M_DownloadMods_FileNameFromUrl(downloadmodinstall.item.url, filename, sizeof(filename));
	if (!M_DownloadMods_BuildTempPath(filename, temp_path, sizeof(temp_path)))
	{
		q_strlcpy(error, "manifest path too long", error_size);
		return false;
	}

	M_DownloadMods_SetWorkerStatus("manifest");
	M_DownloadMods_SetWorkerStage(DOWNLOADMOD_INSTALL_MANIFEST);
	if (!M_DownloadMods_RunTransfer(downloadmodinstall.item.url, temp_path, filename,
		DOWNLOAD_MODS_MAX_MANIFEST_BYTES, &file_size, aborted, error, error_size))
		return false;

	if (M_DownloadMods_WorkerCancelled(error, error_size, aborted))
	{
		unlink(temp_path);
		return false;
	}

	VEC_CLEAR(downloadmodinstall.parts);
	if (!M_DownloadMods_ParseManifest(temp_path) ||
		VEC_SIZE(downloadmodinstall.parts) <= 0)
	{
		unlink(temp_path);
		q_strlcpy(error, "manifest parse failed", error_size);
		return false;
	}

	unlink(temp_path);
	return true;
}

static qboolean M_DownloadMods_RunInstallWorker(char *error, size_t error_size,
	qboolean *aborted)
{
	int i;

	if (downloadmodinstall.item.type == DOWNLOADMOD_SPLIT_MANIFEST &&
		!M_DownloadMods_RunManifestWorker(error, error_size, aborted))
		return false;

	if (VEC_SIZE(downloadmodinstall.parts) <= 0)
	{
		q_strlcpy(error, "install state error", error_size);
		return false;
	}

	for (i = 0; i < (int)VEC_SIZE(downloadmodinstall.parts); i++)
	{
		M_DownloadMods_SetWorkerPartIndex(i);
		if (M_DownloadMods_WorkerCancelled(error, error_size, aborted))
			return false;
		if (!M_DownloadMods_RunArchivePart(&downloadmodinstall.parts[i],
			error, error_size, aborted))
			return false;
	}

	M_DownloadMods_SetWorkerPartIndex((int)VEC_SIZE(downloadmodinstall.parts));
	if (downloadmodinstall.item.type == DOWNLOADMOD_SPLIT_ZIP)
		return M_DownloadMods_FinalizeSplitZipWorker(error, error_size, aborted);

	return true;
}

static void M_DownloadMods_SetWorkerDone(qboolean success, qboolean aborted,
	const char *message)
{
	SDL_LockMutex(downloadmodinstall.mutex);
	downloadmodinstall.transfer.downloading = false;
	downloadmodinstall.transfer.done = true;
	downloadmodinstall.transfer.success = success;
	downloadmodinstall.transfer.aborted = aborted;
	q_strlcpy(downloadmodinstall.transfer.error, message ? message : "",
		sizeof(downloadmodinstall.transfer.error));
	SDL_UnlockMutex(downloadmodinstall.mutex);
}

static int M_DownloadMods_InstallThread(void *unused)
{
	char message[128] = "";
	qboolean aborted = false;
	qboolean success;

	(void)unused;

	success = M_DownloadMods_RunInstallWorker(message, sizeof(message), &aborted);
	if (success)
		M_DownloadMods_SetWorkerStatus("final");
	M_DownloadMods_SetWorkerDone(success, aborted,
		message[0] ? message : (success ? "" : "install failed"));
	return 0;
}

void M_DownloadMods_Frame(void)
{
	qboolean active, downloading, done, success, aborted;
	char error[128];
	char display_name[64];
	double received, total;
	qofs_t file_size;
	SDL_Thread *thread = NULL;

	if (!downloadmodinstall.active || !downloadmodinstall.mutex)
		return;

	SDL_LockMutex(downloadmodinstall.mutex);
	active = downloadmodinstall.transfer.active;
	downloading = downloadmodinstall.transfer.downloading;
	done = downloadmodinstall.transfer.done;
	success = downloadmodinstall.transfer.success;
	aborted = downloadmodinstall.transfer.aborted;
	received = downloadmodinstall.transfer.received;
	total = downloadmodinstall.transfer.total;
	file_size = downloadmodinstall.transfer.file_size;
	q_strlcpy(error, downloadmodinstall.transfer.error, sizeof(error));
	q_strlcpy(display_name, downloadmodinstall.transfer.display_name, sizeof(display_name));
	if (done)
	{
		thread = downloadmodinstall.transfer.thread;
		downloadmodinstall.transfer.thread = NULL;
	}
	SDL_UnlockMutex(downloadmodinstall.mutex);

	if (!active)
		return;

	if (!done)
	{
		if (!downloading)
		{
			cls.download.percent = -1.0f;
			return;
		}

		if (!cls.download.active || strcmp(cls.download.current, display_name))
			M_DownloadMods_BeginSharedDownloadProgress(display_name);

		cls.download.received = received > 0.0 ? received : 0.0;
		cls.download.total = total > 0.0 ? total : 0.0;
		if (cls.download.total > 0.0)
			cls.download.percent = (float)((cls.download.received * 100.0) /
				cls.download.total);
		else
			cls.download.percent = -1.0f;

		if (received > 10000.0 && realtime - downloadmodinstall.last_progress_print >= 0.5)
		{
			downloadmodinstall.last_progress_print = realtime;
			if (total > 0.0)
			{
				char sizeStr[32];
				int progress = (int)((received / total) * 100.0);
				if (progress < 0)
					progress = 0;
				else if (progress > 100)
					progress = 100;
				M_DownloadMods_FormatSize(total, sizeStr, sizeof(sizeStr));
				Con_Printf("DL %s (%s) %s ^m%d%%\r",
					display_name, downloadmodinstall.item.name, sizeStr, progress);
			}
			else
			{
				Con_Printf("DL %s (%s) %.0f kb\r",
					display_name, downloadmodinstall.item.name, received / 1024.0);
			}
		}
		return;
	}

	if (thread)
		SDL_WaitThread(thread, NULL);

	SDL_LockMutex(downloadmodinstall.mutex);
	downloadmodinstall.transfer.active = false;
	downloadmodinstall.transfer.downloading = false;
	downloadmodinstall.transfer.done = false;
	SDL_UnlockMutex(downloadmodinstall.mutex);

	cls.download.active = false;
	cls.download.chunked = false;
	cls.download.current[0] = '\0';
	cls.download.percent = -1.0f;
	cls.download.received = 0.0;
	cls.download.total = 0.0;
	curl_download_active = false;
	stop_curl_download = false;

	if (!success)
	{
		if (aborted)
			M_DownloadMods_FinishInstall(false, "download cancelled");
		else if (error[0])
			M_DownloadMods_FinishInstall(false, error);
		else
			M_DownloadMods_FinishInstall(false, "install failed");
		return;
	}

	{
		char sizeStr[32];
		M_DownloadMods_FormatSize(file_size, sizeStr, sizeof(sizeStr));
		if (display_name[0] && file_size > 0)
			Con_Printf("Downloaded ^m%s^m (%s)\n", display_name, sizeStr);
	}

	if (M_DownloadMods_FinalizeStagedInstall())
	{
		char status[96];
		q_snprintf(status, sizeof(status), "installed %s",
			downloadmodinstall.item.install_dir);
		M_DownloadMods_FinishInstall(true, status);
	}
	else
		M_DownloadMods_FinishInstall(false, "install failed");
}

void M_DownloadMods_Shutdown(void)
{
	SDL_mutex *mutex = downloadmodinstall.mutex;
	SDL_Thread *thread = NULL;
	qboolean active;

	if (!mutex)
	{
		M_DownloadMods_ShutdownFetch();
		return;
	}

	SDL_LockMutex(mutex);
	active = downloadmodinstall.active || downloadmodinstall.transfer.active;
	if (active)
	{
		SDL_AtomicSet(&downloadmodinstall.abort_requested, 1);
		thread = downloadmodinstall.transfer.thread;
		downloadmodinstall.transfer.thread = NULL;
	}
	SDL_UnlockMutex(mutex);

	if (active)
		stop_curl_download = true;
	if (thread)
		SDL_WaitThread(thread, NULL);

	if (active && downloadmodinstall.stage_dir[0])
		M_DownloadMods_RemoveTree(downloadmodinstall.stage_dir);
	M_DownloadMods_RemoveTempFiles();
	VEC_FREE(downloadmodinstall.parts);

	cls.download.active = false;
	cls.download.chunked = false;
	cls.download.current[0] = '\0';
	cls.download.percent = -1.0f;
	cls.download.received = 0.0;
	cls.download.total = 0.0;
	curl_download_active = false;
	stop_curl_download = false;

	SDL_DestroyMutex(mutex);
	memset(&downloadmodinstall, 0, sizeof(downloadmodinstall));

	M_DownloadMods_ShutdownFetch();
}

static const char *M_DownloadMods_InstallDetail(const downloadmoditem_t *item,
	char *buffer, size_t buffer_size)
{
	char status[sizeof(downloadmodinstall.status)];
	downloadmodinstallstage_t stage = DOWNLOADMOD_INSTALL_NONE;
	qboolean downloading = false;

	if (!M_DownloadMods_CopyInstallStateForItem(item, &stage,
		status, sizeof(status), &downloading))
		return NULL;

	if (stage == DOWNLOADMOD_INSTALL_ARCHIVE && downloading && cls.download.active)
	{
		/* Show a percentage for the whole transfer; during the connect phase
		 * (before a total is known) percent is -1, so clamp it to 0% rather
		 * than briefly flashing the "downloading..." status text. Once the
		 * transfer is done, show worker statuses such as verifying/extracting. */
		float percent = cls.download.percent;
		if (percent < 0.0f)
			percent = 0.0f;
		q_snprintf(buffer, buffer_size, "%d%%", (int)(percent + 0.5f));
		return buffer;
	}

	if (status[0])
	{
		q_strlcpy(buffer, status, buffer_size);
		return buffer;
	}

	/* Active install with no status yet (worker thread still spinning up just
	 * after the click): show 0%% so the row never flashes a placeholder before
	 * the download begins. */
	q_strlcpy(buffer, "0%", buffer_size);
	return buffer;
}

static qboolean M_DownloadMods_StartInstall(downloadmoditem_t *item)
{
	SDL_mutex *mutex;
	downloadmodpart_t *parts;
	downloadmodpart_t part;
	char target[MAX_OSPATH];
	int target_type;

	if (!item)
		return false;

	if (downloadmodinstall.active)
	{
		M_DownloadMods_SetMessage("mod install already active");
		S_LocalSound("misc/menu3.wav");
		return false;
	}

	if (!M_DownloadMods_InstallDirNameOkay(item->install_dir))
	{
		M_DownloadMods_SetMessage("invalid install directory");
		S_LocalSound("misc/menu3.wav");
		return false;
	}

	if (!M_DownloadMods_InstallDirNameOkay(item->id))
	{
		M_DownloadMods_SetMessage("invalid download id");
		S_LocalSound("misc/menu3.wav");
		return false;
	}

	mutex = downloadmodinstall.mutex;
	parts = downloadmodinstall.parts;
	VEC_CLEAR(parts);
	memset(&downloadmodinstall, 0, sizeof(downloadmodinstall));
	downloadmodinstall.mutex = mutex;
	downloadmodinstall.parts = parts;
	downloadmodinstall.active = true;
	downloadmodinstall.item = *item;
	M_DownloadMods_SetWorkerStage(DOWNLOADMOD_INSTALL_NONE);
	downloadmodinstall.last_progress_print = 0.0;

	if (!M_DownloadMods_BuildInstallTarget(target, sizeof(target)))
	{
		M_DownloadMods_FinishInstall(false, "target path too long");
		return false;
	}

	target_type = Sys_FileType(target);
	if (target_type & (FS_ENT_FILE | FS_ENT_DIRECTORY))
	{
		Con_Printf("Install target already exists: %s\n", target);
		M_DownloadMods_FinishInstall(false, "target exists");
		return false;
	}

	if (!M_DownloadMods_BuildStageDir())
	{
		M_DownloadMods_FinishInstall(false, "stage path too long");
		return false;
	}

	if (item->type == DOWNLOADMOD_SPLIT_MANIFEST)
	{
		if (!M_DownloadMods_RepoAssetUrlAllowed(item->url))
		{
			M_DownloadMods_FinishInstall(false, "invalid download URL");
			return false;
		}

		Con_Printf("Downloading manifest for %s %s:\n%s\n",
			item->name, item->version, item->url);
		if (!M_DownloadMods_StartWorker())
		{
			M_DownloadMods_FinishInstall(false,
				downloadmodinstall.status[0] ? downloadmodinstall.status : "download failed");
			return false;
		}
		return true;
	}

	if (item->type == DOWNLOADMOD_SPLIT_ZIP)
	{
		/* A malformed hash is fatal; an empty one (auto-discovered release)
		 * just skips verification. */
		if (item->sha256[0] && !M_DownloadMods_SHA256StringOkay(item->sha256))
		{
			M_DownloadMods_FinishInstall(false, "invalid ZIP SHA-256");
			return false;
		}

		if (item->part_count <= 0)
		{
			M_DownloadMods_FinishInstall(false, "split ZIP has no parts");
			return false;
		}

		/* Build parts from the release's asset URLs and derive the joined ZIP
		 * name from the first part filename. */
		{
			int p;
			char *cut;

			M_DownloadMods_FileNameFromUrl(item->part_url[0],
				downloadmodinstall.joined_name, sizeof(downloadmodinstall.joined_name));
			cut = strstr(downloadmodinstall.joined_name, ".part-");
			if (cut)
				*cut = '\0';
			if (!downloadmodinstall.joined_name[0])
				q_strlcpy(downloadmodinstall.joined_name, "download.zip",
					sizeof(downloadmodinstall.joined_name));

			for (p = 0; p < item->part_count; p++)
			{
				if (!M_DownloadMods_AddManifestPart(item->part_url[p],
					item->part_sha256[p], NULL))
				{
					M_DownloadMods_FinishInstall(false, "split ZIP setup failed");
					return false;
				}
				Con_Printf("Downloading split part for %s %s:\n%s\n",
					item->name, item->version, item->part_url[p]);
			}
		}

		M_DownloadMods_SetWorkerPartIndex(0);
		if (item->sha256[0])
			Con_Printf("Expected final ZIP SHA-256: %s\n", item->sha256);

		if (!M_DownloadMods_StartWorker())
		{
			M_DownloadMods_FinishInstall(false,
				downloadmodinstall.status[0] ? downloadmodinstall.status : "download failed");
			return false;
		}
		return true;
	}

	if (!M_DownloadMods_RepoAssetUrlAllowed(item->url))
	{
		M_DownloadMods_FinishInstall(false, "invalid download URL");
		return false;
	}

	memset(&part, 0, sizeof(part));
	/* Empty hash means an auto-discovered release (no verification); a present
	 * but malformed hash is still rejected. */
	if (item->sha256[0] && !M_DownloadMods_SHA256StringOkay(item->sha256))
	{
		M_DownloadMods_FinishInstall(false, "invalid SHA-256");
		return false;
	}
	if (q_strlcpy(part.url, item->url, sizeof(part.url)) >= sizeof(part.url))
	{
		M_DownloadMods_FinishInstall(false, "download URL too long");
		return false;
	}
	q_strlcpy(part.sha256, item->sha256, sizeof(part.sha256));
	M_DownloadMods_FileNameFromUrl(item->url, part.filename, sizeof(part.filename));
	VEC_PUSH(downloadmodinstall.parts, part);
	M_DownloadMods_SetWorkerPartIndex(0);

	Con_Printf("Downloading ZIP for %s %s:\n%s\n",
		item->name, item->version, item->url);
	if (item->sha256[0])
		Con_Printf("Expected SHA-256: %s\n", item->sha256);

	if (!M_DownloadMods_StartWorker())
	{
		M_DownloadMods_FinishInstall(false,
			downloadmodinstall.status[0] ? downloadmodinstall.status : "download failed");
		return false;
	}
	return true;
}

static void M_DownloadMods_StartSelected(void)
{
	downloadmoditem_t *item = M_DownloadMods_SelectedItem();
	qboolean was_installed;

	if (!item)
	{
		S_LocalSound("misc/menu3.wav");
		return;
	}

	was_installed = item->installed;
	item->installed = M_DownloadMods_ProbeInstalled(item);
	if (item->installed != was_installed)
		M_DownloadMods_Refilter();
	if (M_DownloadMods_IsInstalled(item))
	{
		Cbuf_AddText(va("game \"%s\"\n", item->install_dir));
		M_Menu_Main_f();
		return;
	}
	if (M_DownloadMods_TargetExists(item, NULL))
	{
		M_DownloadMods_SetMessage("target exists");
		S_LocalSound("misc/menu3.wav");
		return;
	}

	M_DownloadMods_StartInstall(item);
}

static qboolean M_DownloadMods_InstallBusy(void)
{
	qboolean active = false;
	qboolean done = false;

	if (!downloadmodinstall.active)
		return false;
	if (!downloadmodinstall.mutex)
		return true;

	SDL_LockMutex(downloadmodinstall.mutex);
	active = downloadmodinstall.transfer.active;
	done = downloadmodinstall.transfer.done;
	SDL_UnlockMutex(downloadmodinstall.mutex);
	return active && !done;
}

static void M_DownloadMods_RequestCancel(void)
{
	if (!downloadmodinstall.active)
		return;

	SDL_AtomicSet(&downloadmodinstall.abort_requested, 1);
	stop_curl_download = true;
	M_DownloadMods_SetInstallStatus("cancel");
}

static void M_DownloadMods_LeaveToMods(void)
{
	downloadmodsmenu.exit_prompt = false;
	M_Menu_Mods_f();
}

static void M_DownloadMods_ApplyExitPrompt(void)
{
	if (downloadmodsmenu.exit_prompt_cursor == DOWNLOADMOD_EXIT_CANCEL)
		M_DownloadMods_RequestCancel();
	M_DownloadMods_LeaveToMods();
}

static void M_DownloadMods_DrawExitPrompt(void)
{
	int box_x = 24;
	int box_y = 68;
	int text_x = 48;
	int option_x = 64;
	int option_y = box_y + 36;
	int cursor_y = option_y + downloadmodsmenu.exit_prompt_cursor * 8;

	M_DrawTextBox(box_x, box_y, 32, 6);
	M_PrintWhite(text_x, box_y + 12, "download is active");
	M_Print(text_x, box_y + 20, "leave menu or cancel it?");
	M_Print(option_x, option_y, "download in background");
	M_Print(option_x, option_y + 8, "cancel download");
	M_DrawCharacter(option_x - 8, cursor_y,
		12 + ((int)(realtime * 4) & 1));
}

static qboolean M_DownloadMods_ExitPromptKey(int key)
{
	if (!downloadmodsmenu.exit_prompt)
		return false;

	switch (key)
	{
	case K_UPARROW:
	case K_LEFTARROW:
	case K_MWHEELUP:
		downloadmodsmenu.exit_prompt_cursor =
			(downloadmodsmenu.exit_prompt_cursor + DOWNLOADMOD_EXIT_COUNT - 1) %
			DOWNLOADMOD_EXIT_COUNT;
		S_LocalSound("misc/menu1.wav");
		return true;

	case K_DOWNARROW:
	case K_RIGHTARROW:
	case K_MWHEELDOWN:
		downloadmodsmenu.exit_prompt_cursor =
			(downloadmodsmenu.exit_prompt_cursor + 1) % DOWNLOADMOD_EXIT_COUNT;
		S_LocalSound("misc/menu1.wav");
		return true;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		M_DownloadMods_ApplyExitPrompt();
		return true;

	case K_MOUSE1:
		if (m_mousex >= 56 && m_mousex < 240 &&
			m_mousey >= 104 && m_mousey < 120)
			downloadmodsmenu.exit_prompt_cursor = (m_mousey - 104) / 8;
		M_DownloadMods_ApplyExitPrompt();
		return true;

	case 'c':
	case 'C':
		downloadmodsmenu.exit_prompt_cursor = DOWNLOADMOD_EXIT_CANCEL;
		M_DownloadMods_ApplyExitPrompt();
		return true;

	case 'b':
	case 'B':
		downloadmodsmenu.exit_prompt_cursor = DOWNLOADMOD_EXIT_BACKGROUND;
		M_DownloadMods_ApplyExitPrompt();
		return true;

	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		downloadmodsmenu.exit_prompt_cursor = DOWNLOADMOD_EXIT_BACKGROUND;
		M_DownloadMods_ApplyExitPrompt();
		return true;

	default:
		return true;
	}
}

typedef enum
{
	DOWNLOADMOD_DETAIL_NORMAL,
	DOWNLOADMOD_DETAIL_SIZE,
	DOWNLOADMOD_DETAIL_INSTALLED,
	DOWNLOADMOD_DETAIL_PROGRESS
} downloadmoddetailstyle_t;

static plcolour_t M_DownloadMods_Color(byte r, byte g, byte b)
{
	plcolour_t c;

	c.type = 2;
	c.rgb[0] = r;
	c.rgb[1] = g;
	c.rgb[2] = b;
	c.basic = 0;
	return c;
}

static void M_DownloadMods_PrintRGBAScroll(int x, int y, int maxwidth,
	const char *str, double time, plcolour_t color, float alpha, qboolean mask)
{
	const int charwidth = 8;
	const int gap_len = 5;
	const int scrollspeed = 30;
	int maxchars = maxwidth / charwidth;
	int len = (int)strlen(str);
	int mask_offset = mask ? 128 : 0;
	int total_chars, cycle_pixels, pixel_offset, pass;
	float frac;

	if (len <= maxchars)
	{
		M_PrintRGBA(x, y, str, color, alpha, mask);
		return;
	}

	if (!len)
		return;

	total_chars = len + gap_len;
	cycle_pixels = total_chars * charwidth;
	frac = M_ScrollPixelOffset(time, scrollspeed, cycle_pixels, &pixel_offset);

	glPushMatrix();
	glTranslatef(-frac, 0.0f, 0.0f);
	for (pass = 0; pass < 2; pass++)
	{
		int base_x = x - pixel_offset + pass * cycle_pixels;
		int pos;

		for (pos = 0; pos < total_chars; pos++)
		{
			int char_x = base_x + pos * charwidth;
			int ch;

			if (char_x + charwidth <= x)
				continue;
			if (char_x >= x + maxwidth)
				break;

			if (pos < len)
				ch = (unsigned char)str[pos];
			else
				ch = (unsigned char)" /// "[pos - len];

			M_DrawCharacterRGBA(char_x, y, ch + mask_offset, color, alpha);
		}
	}
	glPopMatrix();
}

static void M_DownloadMods_PrintMaskedAlpha(int x, int y, const char *str, float alpha)
{
	plcolour_t white;

	if (alpha >= 1.0f)
	{
		M_Print(x, y, str);
		return;
	}

	white = M_DownloadMods_Color(255, 255, 255);
	M_PrintRGBA(x, y, str, white, alpha, true);
}

static void M_DownloadMods_PrintGoldProgress(int x, int y, const char *str, float alpha)
{
	plcolour_t white = M_DownloadMods_Color(255, 255, 255);

	while (*str)
	{
		int ch = (unsigned char)*str++;
		int glyph = (ch >= '0' && ch <= '9') ? ch - 30 : ch + 128;

		if (alpha >= 1.0f)
			M_DrawCharacter(x, y, glyph);
		else
			M_DrawCharacterRGBA(x, y, glyph, white, alpha);
		x += 8;
	}
}

static void M_DownloadMods_DrawDetail(int x, int y, const char *detail,
	downloadmoddetailstyle_t style, qboolean dim, qboolean search_active)
{
	plcolour_t white = M_DownloadMods_Color(255, 255, 255);
	float alpha = dim ? 0.5f : 1.0f;

	if (style == DOWNLOADMOD_DETAIL_PROGRESS)
	{
		M_DownloadMods_PrintGoldProgress(x, y, detail, alpha);
		return;
	}

	if (style == DOWNLOADMOD_DETAIL_SIZE ||
		style == DOWNLOADMOD_DETAIL_INSTALLED)
	{
		M_DownloadMods_PrintMaskedAlpha(x, y, detail, alpha);
		return;
	}

	if (dim)
	{
		M_PrintRGBA(x, y, detail, white, alpha, false);
		return;
	}

	if (search_active && q_strcasestr(detail, downloadmodsmenu.list.search.text))
		M_PrintHighlight(x, y, detail, downloadmodsmenu.list.search.text,
			downloadmodsmenu.list.search.len);
	else
		M_PrintWhite(x, y, detail);
}

static void M_DownloadMods_DrawRow(int x, int y, const downloadmoditem_t *item,
	const char *detail, downloadmoddetailstyle_t detail_style,
	qboolean installed, qboolean selected, qboolean search_active)
{
	int name_width = DOWNLOAD_MODS_NAME_CHARS * 8;
	int detail_x = x + (DOWNLOAD_MODS_NAME_CHARS + 1) * 8;
	char name_text[sizeof(item->name) + sizeof(item->version) + 2];
	char detail_text[DOWNLOAD_MODS_DETAIL_CHARS + 1];
	plcolour_t white = M_DownloadMods_Color(255, 255, 255);

	if (item->version[0])
		q_snprintf(name_text, sizeof(name_text), "%s %s",
			item->name, item->version);
	else
		q_strlcpy(name_text, item->name, sizeof(name_text));
	q_strlcpy(detail_text, detail, sizeof(detail_text));

	if (installed)
	{
		M_DownloadMods_PrintRGBAScroll(x, y, name_width, name_text,
			selected ? downloadmodsmenu.ticker.scroll_time : 0.0,
			white, 0.5f, false);
	}
	else if (search_active)
	{
		if ((int)strlen(name_text) <= DOWNLOAD_MODS_NAME_CHARS)
		{
			M_PrintHighlight(x, y, name_text,
				downloadmodsmenu.list.search.text,
				downloadmodsmenu.list.search.len);
		}
		else
		{
			M_PrintHighlightScroll(x, y, name_width, name_text,
				downloadmodsmenu.list.search.text,
				selected ? downloadmodsmenu.ticker.scroll_time : 0.0);
		}

		M_DownloadMods_DrawDetail(detail_x, y, detail_text, detail_style,
			installed, search_active);
	}
	else
	{
		M_PrintScroll(x, y, name_width, name_text,
			selected ? downloadmodsmenu.ticker.scroll_time : 0.0, false);
	}

	if (!search_active || installed)
	{
		M_DownloadMods_DrawDetail(detail_x, y, detail_text, detail_style,
			installed, search_active);
	}
}

/*
================================================================================

Static mod downloads manifest

Builds the live mod list from the curated moddownloads.json manifest at the
configured repo (see M_DownloadMods_ManifestUrl / DOWNLOAD_MODS_MANIFEST_PATH)
on a background thread. The manifest replaces the bootstrap list wholesale when
valid, so publishing/removing a mod is a data update on the repo rather than a
GitHub API discovery race. Download URLs remain allow-listed to the configured
repo's GitHub release/raw asset roots.

================================================================================
*/

static qboolean M_DownloadMods_SHA256FromDigest(const char *digest,
	char *out, size_t outsize)
{
	if (out && outsize)
		out[0] = '\0';
	if (!digest || !out || outsize < 65)
		return false;
	if (q_strncasecmp(digest, "sha256:", 7))
		return false;
	digest += 7;
	if (!*digest || !M_DownloadMods_SHA256StringOkay(digest))
		return false;
	q_strlcpy(out, digest, outsize);
	return true;
}

static void M_DownloadMods_FormatSizeShort(double bytes, char *out, size_t outsize)
{
	double gb = bytes / (1024.0 * 1024.0 * 1024.0);
	double mb = bytes / (1024.0 * 1024.0);
	double kb = bytes / 1024.0;

	if (gb >= 1.0)
		q_snprintf(out, outsize, "%.1f GB", gb);
	else if (mb >= 1.0)
		q_snprintf(out, outsize, "%.0f MB", mb);
	else
		q_snprintf(out, outsize, "%.0f KB", kb);
}

/* Derive a stable id (install-directory name + dedupe key) from a release tag.
 * Tag convention: "<folder>-<notes>". Everything before the first '-' is the
 * install-directory name (and dedupe key); anything after it is free-form
 * release notes and ignored. A tag with no '-' is used as-is. So "qbj3-1.3" ->
 * "qbj3", "ad-v1.80p1" -> "ad", "rm1.2-b" -> "rm1.2", "sewerjam" -> "sewerjam".
 * Folder names therefore must not contain a '-'. */
static void M_DownloadMods_IdFromTag(const char *tag, char *out, size_t outsize)
{
	size_t len;
	const char *dash;

	if (!out || outsize == 0)
		return;
	out[0] = '\0';
	if (!tag || !*tag)
		return;

	dash = strchr(tag, '-');
	len = dash ? (size_t)(dash - tag) : strlen(tag);
	if (len > outsize - 1)
		len = outsize - 1;
	memcpy(out, tag, len);
	out[len] = '\0';
}

typedef struct
{
	char key[DOWNLOAD_MODS_MAX_URL];
	char url[DOWNLOAD_MODS_MAX_URL];
	char sha256[65];
	double bytes;
} downloadmodmanifestpart_t;

static const char *M_DownloadMods_ManifestString(const jsonentry_t *entry,
	const char *name1, const char *name2, const char *name3)
{
	const char *s = NULL;

	if (!entry)
		return NULL;
	if (name1)
		s = JSON_FindString(entry, name1);
	if (!s && name2)
		s = JSON_FindString(entry, name2);
	if (!s && name3)
		s = JSON_FindString(entry, name3);
	return s;
}

static const double *M_DownloadMods_ManifestNumber(const jsonentry_t *entry,
	const char *name1, const char *name2, const char *name3)
{
	const double *n = NULL;

	if (!entry)
		return NULL;
	if (name1)
		n = JSON_FindNumber(entry, name1);
	if (!n && name2)
		n = JSON_FindNumber(entry, name2);
	if (!n && name3)
		n = JSON_FindNumber(entry, name3);
	return n;
}

static qboolean M_DownloadMods_CopyManifestSHA256(const jsonentry_t *entry,
	char *out, size_t outsize)
{
	const char *sha;

	if (!out || outsize == 0)
		return false;
	out[0] = '\0';

	sha = M_DownloadMods_ManifestString(entry, "sha256", "sha-256", "sha");
	if (!sha)
	{
		sha = JSON_FindString(entry, "digest");
		if (sha && *sha)
			return M_DownloadMods_SHA256FromDigest(sha, out, outsize);
		return true;
	}

	if (!q_strncasecmp(sha, "sha256:", 7))
		return M_DownloadMods_SHA256FromDigest(sha, out, outsize);
	if (!M_DownloadMods_SHA256StringOkay(sha))
		return false;

	return q_strlcpy(out, sha, outsize) < outsize;
}

static const char *M_DownloadMods_ManifestEntryUrl(const jsonentry_t *entry)
{
	return M_DownloadMods_ManifestString(entry, "url", "download_url",
		"browser_download_url");
}

static void M_DownloadMods_SortManifestParts(
	downloadmodmanifestpart_t *parts, int count)
{
	int i, j;

	for (i = 0; i < count - 1; i++)
		for (j = i + 1; j < count; j++)
			if (strcmp(parts[j].key, parts[i].key) < 0)
			{
				downloadmodmanifestpart_t temp = parts[i];
				parts[i] = parts[j];
				parts[j] = temp;
			}
}

static qboolean M_DownloadMods_ParseManifestParts(const jsonentry_t *parts_array,
	downloadmoditem_t *out, double *bytes_out)
{
	downloadmodmanifestpart_t parts[DOWNLOAD_MODS_MAX_PARTS];
	const jsonentry_t *entry;
	int count = 0;
	int i;
	double bytes = 0.0;

	if (bytes_out)
		*bytes_out = 0.0;
	if (!parts_array)
		return false;
	if (parts_array->type != JSON_ARRAY)
		return false;

	for (entry = parts_array->firstchild; entry; entry = entry->next)
	{
		const char *url, *key;
		const double *size;

		if (entry->type != JSON_OBJECT)
			return false;
		if (count >= DOWNLOAD_MODS_MAX_PARTS)
			return false;

		url = M_DownloadMods_ManifestEntryUrl(entry);
		if (!url || !M_DownloadMods_RepoAssetUrlAllowed(url))
			return false;
		if (!M_DownloadMods_CopyManifestSHA256(entry, parts[count].sha256,
			sizeof(parts[count].sha256)))
			return false;

		key = M_DownloadMods_ManifestString(entry, "filename", "name", NULL);
		if (q_strlcpy(parts[count].key, (key && *key) ? key : url,
			sizeof(parts[count].key)) >= sizeof(parts[count].key))
			return false;
		if (q_strlcpy(parts[count].url, url, sizeof(parts[count].url)) >=
			sizeof(parts[count].url))
			return false;
		size = M_DownloadMods_ManifestNumber(entry, "size_bytes", "bytes",
			"size");
		if (size && *size > 0.0)
		{
			parts[count].bytes = *size;
			bytes += *size;
		}
		else
			parts[count].bytes = 0.0;
		count++;
	}

	if (count <= 1)
		return false;

	M_DownloadMods_SortManifestParts(parts, count);
	out->type = DOWNLOADMOD_SPLIT_ZIP;
	out->part_count = count;
	for (i = 0; i < count; i++)
	{
		q_strlcpy(out->part_url[i], parts[i].url, sizeof(out->part_url[i]));
		q_strlcpy(out->part_sha256[i], parts[i].sha256,
			sizeof(out->part_sha256[i]));
	}
	if (bytes_out)
		*bytes_out = bytes;
	return true;
}

static qboolean M_DownloadMods_ParseManifestAsset(const jsonentry_t *entry,
	downloadmoditem_t *out, double *bytes_out)
{
	const jsonentry_t *asset;
	const double *size;
	const char *url;

	if (bytes_out)
		*bytes_out = 0.0;

	asset = JSON_Find(entry, "asset", JSON_OBJECT);
	if (!asset)
		asset = entry;

	url = M_DownloadMods_ManifestEntryUrl(asset);
	if (!url || !M_DownloadMods_RepoAssetUrlAllowed(url))
		return false;
	if (!M_DownloadMods_CopyManifestSHA256(asset, out->sha256,
		sizeof(out->sha256)))
		return false;

	out->type = DOWNLOADMOD_SINGLE_ZIP;
	if (q_strlcpy(out->url, url, sizeof(out->url)) >= sizeof(out->url))
		return false;
	size = M_DownloadMods_ManifestNumber(asset, "size_bytes", "bytes", "size");
	if (size && *size > 0.0 && bytes_out)
		*bytes_out = *size;
	return true;
}

static qboolean M_DownloadMods_BuildItemFromManifestEntry(
	const jsonentry_t *entry, downloadmoditem_t *out)
{
	const char *id, *tag, *name, *version, *description, *install_dir;
	const char *size_label;
	const double *size_bytes;
	const jsonentry_t *parts_array;
	double archive_bytes = 0.0;

	if (!entry || entry->type != JSON_OBJECT || !out)
		return false;

	memset(out, 0, sizeof(*out));

	id = JSON_FindString(entry, "id");
	tag = JSON_FindString(entry, "tag");
	if ((!id || !*id) && tag && *tag)
	{
		M_DownloadMods_IdFromTag(tag, out->id, sizeof(out->id));
		id = out->id;
	}
	if (!id || !*id)
		return false;
	if (q_strlcpy(out->id, id, sizeof(out->id)) >= sizeof(out->id))
		return false;
	if (!M_DownloadMods_InstallDirNameOkay(out->id))
		return false;

	install_dir = M_DownloadMods_ManifestString(entry, "install_dir",
		"folder", "directory");
	if (!install_dir || !*install_dir)
		install_dir = out->id;
	if (q_strlcpy(out->install_dir, install_dir, sizeof(out->install_dir)) >=
		sizeof(out->install_dir))
		return false;
	if (!M_DownloadMods_InstallDirNameOkay(out->install_dir))
		return false;

	name = M_DownloadMods_ManifestString(entry, "title", "name", NULL);
	if (!name || !*name)
		name = out->id;
	q_strlcpy(out->name, name, sizeof(out->name));

	version = JSON_FindString(entry, "version");
	if (version)
		q_strlcpy(out->version, version, sizeof(out->version));
	description = JSON_FindString(entry, "description");
	q_strlcpy(out->description,
		(description && *description) ? description : "Mod download",
		sizeof(out->description));

	parts_array = JSON_Find(entry, "parts", JSON_ARRAY);
	if (parts_array)
	{
		if (!M_DownloadMods_ParseManifestParts(parts_array, out, &archive_bytes))
			return false;
		if (!M_DownloadMods_CopyManifestSHA256(entry, out->sha256,
			sizeof(out->sha256)))
			return false;
	}
	else if (!M_DownloadMods_ParseManifestAsset(entry, out, &archive_bytes))
		return false;

	size_label = JSON_FindString(entry, "size");
	if (size_label && *size_label)
		q_strlcpy(out->size, size_label, sizeof(out->size));
	else
	{
		size_bytes = M_DownloadMods_ManifestNumber(entry, "size_bytes",
			"bytes", NULL);
		if (size_bytes && *size_bytes > 0.0)
			archive_bytes = *size_bytes;
		if (archive_bytes > 0.0)
			M_DownloadMods_FormatSizeShort(archive_bytes, out->size,
				sizeof(out->size));
	}

	return true;
}

static qboolean M_DownloadMods_BuildManifestItemsArray(const jsonentry_t *array,
	downloadmoditem_t **items_out, int *count_out)
{
	downloadmoditem_t *items = NULL;
	const jsonentry_t *entry;
	int installable = 0;

	if (items_out)
		*items_out = NULL;
	if (count_out)
		*count_out = 0;
	if (!array || array->type != JSON_ARRAY)
		return false;

	for (entry = array->firstchild; entry; entry = entry->next)
	{
		downloadmoditem_t item;
		int i;

		if (!M_DownloadMods_BuildItemFromManifestEntry(entry, &item))
			continue;
		installable++;

		for (i = 0; i < (int)VEC_SIZE(items); i++)
			if (!q_strcasecmp(items[i].id, item.id))
				break;

		if (i < (int)VEC_SIZE(items))
			items[i] = item; /* manifest order is authoritative; last id wins */
		else
			VEC_PUSH(items, item);
	}

	if (installable <= 0 || VEC_SIZE(items) <= 0)
	{
		VEC_FREE(items);
		return false;
	}

	if (items_out)
		*items_out = items;
	if (count_out)
		*count_out = (int)VEC_SIZE(items);
	return true;
}

static qboolean M_DownloadMods_ApplyManifestJson(const char *text,
	int *added_out)
{
	json_t *json;
	const jsonentry_t *array;
	downloadmoditem_t *items = NULL;
	int count = 0;
	qboolean valid = false;

	if (added_out)
		*added_out = 0;
	if (!text || !*text)
		return false;

	json = JSON_Parse(text);
	if (!json || !json->root)
		goto done;

	if (json->root->type == JSON_ARRAY)
		array = json->root;
	else if (json->root->type == JSON_OBJECT)
	{
		array = JSON_Find(json->root, "mods", JSON_ARRAY);
		if (!array)
			array = JSON_Find(json->root, "downloads", JSON_ARRAY);
	}
	else
		array = NULL;

	if (!M_DownloadMods_BuildManifestItemsArray(array, &items, &count))
		goto done;

	VEC_FREE(downloadmodsmenu.items);
	downloadmodsmenu.items = items;
	downloadmodsmenu.itemcount = count;
	items = NULL;
	if (added_out)
		*added_out = count;
	valid = true;

done:
	VEC_FREE(items);
	if (json)
		JSON_Free(json);
	return valid;
}

/* GET a URL and return a malloc'd copy of the response body, or NULL. */
static char *M_DownloadMods_FetchUrl(const char *url, char *error,
	size_t error_size)
{
	versionhttpmem_t mem;
	char *out = NULL;

	memset(&mem, 0, sizeof(mem));
	if (M_Version_GitHubHttpGet(url, &mem, error, error_size,
		(size_t)DOWNLOAD_MODS_MAX_MANIFEST_BYTES) && mem.memory)
	{
		size_t len = strlen(mem.memory);
		out = (char *)malloc(len + 1);
		if (out)
			memcpy(out, mem.memory, len + 1);
		else
			q_strlcpy(error, "out of memory", error_size);
	}

	if (mem.memory)
		free(mem.memory);
	return out;
}

static int M_DownloadMods_FetchThread(void *unused)
{
	char error[sizeof(downloadmodsfetch.error)];
	char *json = NULL;

	(void)unused;
	error[0] = '\0';

	json = M_DownloadMods_FetchUrl(downloadmodsfetch.manifest_url,
		error, sizeof(error));

	SDL_LockMutex(downloadmodsfetch.mutex);
	downloadmodsfetch.json = json;
	downloadmodsfetch.success = (json != NULL);
	q_strlcpy(downloadmodsfetch.error, json ? "" : error, sizeof(downloadmodsfetch.error));
	downloadmodsfetch.done = true;
	SDL_UnlockMutex(downloadmodsfetch.mutex);
	return 0;
}

static void M_DownloadMods_StartFetch(qboolean force)
{
	time_t now;
	double age;

	if (downloadmodsfetch.active)
		return; /* a fetch is already running or its result is pending */

	/* Resolve the static manifest URL up front so the worker thread touches no
	 * cvars. If neither web-download slot is a GitHub repo path there is no
	 * source to fetch from. */
	if (!M_DownloadMods_ManifestUrl(downloadmodsfetch.manifest_url,
		sizeof(downloadmodsfetch.manifest_url)))
	{
		q_strlcpy(downloadmodsfetch.error,
			"set cl_web_download_url to a GitHub user/repo",
			sizeof(downloadmodsfetch.error));
		M_DownloadMods_SetMessage(downloadmodsfetch.error);
		return;
	}

	now = time(NULL);
	if (!force &&
		downloadmodsfetch.last_fetch_time != (time_t)0 &&
		now != (time_t)-1 &&
		!q_strcasecmp(downloadmodsfetch.last_manifest_url,
			downloadmodsfetch.manifest_url))
	{
		age = difftime(now, downloadmodsfetch.last_fetch_time);
		if (age >= 0.0 && age < DOWNLOAD_MODS_FETCH_THROTTLE_SECONDS)
			return;
	}

	if (!downloadmodsfetch.mutex)
		downloadmodsfetch.mutex = SDL_CreateMutex();
	if (!downloadmodsfetch.mutex)
		return;

	downloadmodsfetch.active = true;
	downloadmodsfetch.done = false;
	downloadmodsfetch.success = false;
	downloadmodsfetch.json = NULL;
	downloadmodsfetch.error[0] = '\0';

	downloadmodsfetch.thread =
		SDL_CreateThread(M_DownloadMods_FetchThread, "ModDownloadsFetch", NULL);
	if (!downloadmodsfetch.thread)
		downloadmodsfetch.active = false;
	else if (now != (time_t)-1)
	{
		downloadmodsfetch.last_fetch_time = now;
		q_strlcpy(downloadmodsfetch.last_manifest_url,
			downloadmodsfetch.manifest_url,
			sizeof(downloadmodsfetch.last_manifest_url));
	}
}

static void M_DownloadMods_RebuildVisibleList(void)
{
	M_DownloadMods_RefreshInstalledCache();
	M_DownloadMods_Refilter();
	if (downloadmodsmenu.list.cursor == -1)
		downloadmodsmenu.list.cursor = 0;
	M_DownloadMods_EnsureSelectableCursor(1);
	M_List_CenterCursor(&downloadmodsmenu.list);
}

static qboolean M_DownloadMods_ResetForManifestUrl(const char *manifest_url,
	qboolean source_ok)
{
	const char *source_url = manifest_url ? manifest_url : "";

	if (!q_strcasecmp(downloadmodsmenu.source_manifest_url, source_url))
		return false;

	M_DownloadMods_ClearItemsForSource(source_url,
		source_ok && CL_DownloadRepoIsQ1Tools());
	M_DownloadMods_RebuildVisibleList();
	return true;
}

static qboolean M_DownloadMods_CheckSourceChanged(void)
{
	char current_manifest_url[DOWNLOAD_MODS_MAX_URL];
	qboolean current_source_ok;

	current_source_ok = M_DownloadMods_ManifestUrl(current_manifest_url,
		sizeof(current_manifest_url));
	if (!current_source_ok)
		current_manifest_url[0] = '\0';

	if (!M_DownloadMods_ResetForManifestUrl(current_manifest_url,
		current_source_ok))
		return false;

	M_DownloadMods_StartFetch(true);
	return true;
}

static void M_DownloadMods_PollFetch(void)
{
	char *json;
	char selected_id[sizeof(downloadmodsmenu.items[0].id)];
	char fetched_manifest_url[DOWNLOAD_MODS_MAX_URL];
	char current_manifest_url[DOWNLOAD_MODS_MAX_URL];
	int added = 0;
	qboolean done, success;
	qboolean current_source_ok;
	qboolean source_matches;

	if (!downloadmodsfetch.active || !downloadmodsfetch.mutex)
		return;

	SDL_LockMutex(downloadmodsfetch.mutex);
	done = downloadmodsfetch.done;
	success = downloadmodsfetch.success;
	json = downloadmodsfetch.json;
	q_strlcpy(fetched_manifest_url, downloadmodsfetch.manifest_url,
		sizeof(fetched_manifest_url));
	SDL_UnlockMutex(downloadmodsfetch.mutex);

	if (!done)
		return;

	M_DownloadMods_CopySelectedId(selected_id, sizeof(selected_id));

	if (downloadmodsfetch.thread)
	{
		SDL_WaitThread(downloadmodsfetch.thread, NULL);
		downloadmodsfetch.thread = NULL;
	}

	current_source_ok = M_DownloadMods_ManifestUrl(current_manifest_url,
		sizeof(current_manifest_url));
	if (!current_source_ok)
		current_manifest_url[0] = '\0';

	source_matches = current_source_ok &&
		!q_strcasecmp(fetched_manifest_url, current_manifest_url);

	if (!source_matches)
	{
		Con_DPrintf("Discarding stale mod downloads manifest: %s\n",
			fetched_manifest_url);
	}
	else if (success && json)
	{
		if (!M_DownloadMods_ApplyManifestJson(json, &added))
			Con_DPrintf("Mod downloads manifest returned no valid entries\n");
	}
	else if (!success && downloadmodsfetch.error[0])
		Con_DPrintf("Mod downloads manifest fetch failed: %s\n",
			downloadmodsfetch.error);

	if (json)
		free(json);

	downloadmodsfetch.json = NULL;
	downloadmodsfetch.active = false;

	if (!source_matches)
	{
		M_DownloadMods_ResetForManifestUrl(current_manifest_url,
			current_source_ok);
		M_DownloadMods_StartFetch(true);
	}

	if (added > 0)
	{
		M_DownloadMods_RebuildVisibleList();
		if (!M_DownloadMods_SelectId(selected_id))
			M_DownloadMods_EnsureSelectableCursor(1);
	}
}

static void M_DownloadMods_ShutdownFetch(void)
{
	SDL_mutex *mutex = downloadmodsfetch.mutex;
	SDL_Thread *thread = NULL;
	char *json = NULL;

	if (!mutex)
		return;

	SDL_LockMutex(mutex);
	thread = downloadmodsfetch.thread;
	downloadmodsfetch.thread = NULL;
	SDL_UnlockMutex(mutex);

	if (thread)
		SDL_WaitThread(thread, NULL);

	SDL_LockMutex(mutex);
	json = downloadmodsfetch.json;
	downloadmodsfetch.json = NULL;
	downloadmodsfetch.active = false;
	downloadmodsfetch.done = false;
	downloadmodsfetch.success = false;
	downloadmodsfetch.error[0] = '\0';
	SDL_UnlockMutex(mutex);

	if (json)
		free(json);

	SDL_DestroyMutex(mutex);
	memset(&downloadmodsfetch, 0, sizeof(downloadmodsfetch));
}

void M_DownloadMods_Draw(void)
{
	int x, y, i, cols;
	int firstvis, numvis;
	qboolean message_active = false;
	downloadmoditem_t *selected_item;

	x = DOWNLOAD_MODS_LIST_X;
	y = DOWNLOAD_MODS_LIST_Y;
	cols = DOWNLOAD_MODS_LIST_COLS;

	downloadmodsmenu.x = x;
	downloadmodsmenu.y = y;
	downloadmodsmenu.cols = cols;

	if (!keydown[K_MOUSE1])
		downloadmodsmenu.scrollbar_grab = false;

	M_DownloadMods_CheckSourceChanged();
	M_DownloadMods_PollFetch();

	if (downloadmodsmenu.prev_cursor != downloadmodsmenu.list.cursor)
	{
		downloadmodsmenu.prev_cursor = downloadmodsmenu.list.cursor;
		M_Ticker_Init(&downloadmodsmenu.ticker);
	}
	else
		M_Ticker_Update(&downloadmodsmenu.ticker);

	M_DrawCountHeader(x, y - 28, cols, "Mod Downloads",
		downloadmodsmenu.itemcount, "mod", "mods");
	M_DrawQuakeBar(x - 8, y - 16, cols + 2);

	if (downloadmodsmenu.message[0])
	{
		if (realtime - downloadmodsmenu.message_time < 3.0)
			message_active = true;
		else
			downloadmodsmenu.message[0] = '\0';
	}

	if (downloadmodsmenu.itemcount <= 0)
	{
		if (downloadmodsfetch.active)
			M_Print(x, y, "Fetching mod list...");
		else if (downloadmodsfetch.error[0])
			M_Print(x, y, va("Mod list unavailable: %s", downloadmodsfetch.error));
		else
			M_Print(x, y, "No downloads available");
	}

	M_List_GetVisibleRange(&downloadmodsmenu.list, &firstvis, &numvis);
	for (i = 0; i < numvis; i++)
	{
		int idx = i + firstvis;
		int item_idx = downloadmodsmenu.filtered_indices[idx];
		downloadmoditem_t *item;
		qboolean selected;
		qboolean installed;
		qboolean target_exists;
		char active_detail[DOWNLOAD_MODS_DETAIL_CHARS + 1];
		const char *detail;
		downloadmoddetailstyle_t detail_style = DOWNLOADMOD_DETAIL_NORMAL;

		if (item_idx == DOWNLOAD_MODS_SEPARATOR_INDEX)
			continue;

		item = &downloadmodsmenu.items[item_idx];
		selected = (idx == downloadmodsmenu.list.cursor);
		installed = M_DownloadMods_IsInstalled(item);
		detail = M_DownloadMods_InstallDetail(item,
			active_detail, sizeof(active_detail));
		target_exists = !installed && !detail &&
			M_DownloadMods_TargetExists(item, NULL);

		if (detail && detail[0] && detail[strlen(detail) - 1] == '%')
			detail_style = DOWNLOADMOD_DETAIL_PROGRESS;
		else if (installed)
			detail_style = DOWNLOADMOD_DETAIL_INSTALLED;
		else if (target_exists)
			detail_style = DOWNLOADMOD_DETAIL_INSTALLED;
		else if (!detail)
			detail_style = DOWNLOADMOD_DETAIL_SIZE;

		M_DownloadMods_DrawRow(x, y + i * 8, item,
			detail ? detail :
				(installed ? "installed" :
					target_exists ? "target exists" : item->size),
			detail_style, installed, selected,
			downloadmodsmenu.list.search.len > 0);

		if (selected && M_DownloadMods_IsSelectableDisplayIndex(idx))
			M_DrawCharacter(x - 8, y + i * 8, 12 + ((int)(realtime * 4) & 1));
	}

	if (M_List_GetOverflow(&downloadmodsmenu.list) > 0)
	{
		M_List_DrawScrollbar(&downloadmodsmenu.list, x + cols * 8 - 8, y);

		if (downloadmodsmenu.list.scroll > 0)
			M_DrawEllipsisBar(x, y - 8, cols);
		if (downloadmodsmenu.list.scroll + downloadmodsmenu.list.viewsize < downloadmodsmenu.list.numitems)
			M_DrawEllipsisBar(x, y + downloadmodsmenu.list.viewsize * 8, cols);
	}

	selected_item = M_DownloadMods_SelectedItem();
	if (downloadmodsfetch.active)
	{
		Draw_StringGradientSweep(x, y + downloadmodsmenu.list.viewsize * 8 + 16,
			"refreshing mods list", 96.0f, 48.0f, 1.0f, true);
	}
	else if (downloadmodsmenu.list.search.len == 0 && selected_item)
	{
		char tooltip_status[sizeof(downloadmodinstall.status)];
		const char *tooltip = NULL;

		/* Transient messages take priority, then a state-driven action hint. The
		 * live progress percentage is drawn on the row itself, so the hint here
		 * is just the relevant key: cancel while this item is downloading/
		 * installing, play when installed, otherwise download. No descriptive
		 * tooltip (e.g. "GitHub release") or "target exists" on plain selection. */
		if (message_active)
			tooltip = downloadmodsmenu.message;
		else if (M_DownloadMods_CopyInstallStateForItem(selected_item, NULL,
			tooltip_status, sizeof(tooltip_status), NULL))
			tooltip = "escape to cancel";
		else if (M_DownloadMods_IsInstalled(selected_item))
			tooltip = "installed - enter to play";
		else
			tooltip = "enter to download";

		if (tooltip)
			M_PrintWhite(x, y + downloadmodsmenu.list.viewsize * 8 + 16, tooltip);
	}

	if (downloadmodsmenu.list.search.len > 0)
	{
		M_DrawTextBox(16, 176, 32, 1);
		M_PrintHighlight(24, 184, downloadmodsmenu.list.search.text,
			downloadmodsmenu.list.search.text,
			downloadmodsmenu.list.search.len);
		{
			int cursor_x = 24 + 8 * downloadmodsmenu.list.search.len;
			if (downloadmodsmenu.list.numitems == 0)
				M_DrawCharacter(cursor_x, 184, 11 ^ 128);
			else
				M_DrawCharacter(cursor_x, 184, 10 + ((int)(realtime * 4) & 1));
		}
	}

	if (downloadmodsmenu.exit_prompt)
		M_DownloadMods_DrawExitPrompt();
}

qboolean M_DownloadMods_Match(int index, char initial)
{
	int item_idx = downloadmodsmenu.filtered_indices[index];

	if (item_idx == DOWNLOAD_MODS_SEPARATOR_INDEX)
		return false;

	return q_tolower(downloadmodsmenu.items[item_idx].name[0]) == initial;
}

void M_DownloadMods_Key(int key)
{
	int x, y;

	if (M_DownloadMods_ExitPromptKey(key))
		return;

	if (keydown[K_CTRL])
	{
		if ((key == 'u' || key == 'U') && downloadmodsmenu.list.search.len > 0)
		{
			downloadmodsmenu.list.search.len = 0;
			downloadmodsmenu.list.search.text[0] = 0;
			M_DownloadMods_Refilter();
			return;
		}
		else if (key == K_BACKSPACE && downloadmodsmenu.list.search.len > 0)
		{
			M_DeletePrevWord(&downloadmodsmenu.list.search);
			M_DownloadMods_Refilter();
			return;
		}
	}

	if (key >= 32 && key < 127)
	{
		if (downloadmodsmenu.list.search.len < downloadmodsmenu.list.search.maxlen)
		{
			downloadmodsmenu.list.search.text[downloadmodsmenu.list.search.len++] = key;
			downloadmodsmenu.list.search.text[downloadmodsmenu.list.search.len] = 0;
			M_DownloadMods_Refilter();
			return;
		}
	}

	if (downloadmodsmenu.scrollbar_grab)
	{
		switch (key)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			downloadmodsmenu.scrollbar_grab = false;
			break;
		}
		return;
	}

	if (M_List_Key(&downloadmodsmenu.list, key))
	{
		M_DownloadMods_EnsureSelectableCursorForKey(key);
		return;
	}

	if (M_List_CycleMatch(&downloadmodsmenu.list, key, M_DownloadMods_Match))
		return;

	if (M_Ticker_Key(&downloadmodsmenu.ticker, key))
		return;

	switch (key)
	{
	case K_ESCAPE:
		if (downloadmodsmenu.list.search.len > 0)
		{
			downloadmodsmenu.list.search.len = 0;
			downloadmodsmenu.list.search.text[0] = 0;
			M_DownloadMods_Refilter();
			return;
		}
		/* fall through */
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		if (M_DownloadMods_InstallBusy())
		{
			downloadmodsmenu.exit_prompt = true;
			downloadmodsmenu.exit_prompt_cursor = DOWNLOADMOD_EXIT_BACKGROUND;
			S_LocalSound("misc/menu1.wav");
			return;
		}
		M_Menu_Mods_f();
		break;

	case K_BACKSPACE:
		if (downloadmodsmenu.list.search.len > 0)
		{
			downloadmodsmenu.list.search.text[--downloadmodsmenu.list.search.len] = 0;
			M_DownloadMods_Refilter();
			return;
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	enter:
		M_DownloadMods_StartSelected();
		break;

	case K_MOUSE1:
		x = m_mousex - downloadmodsmenu.x - (downloadmodsmenu.cols - 1) * 8;
		y = m_mousey - downloadmodsmenu.y;
		if (x < -8 || !M_List_UseScrollbar(&downloadmodsmenu.list, y))
		{
			M_List_Mousemove(&downloadmodsmenu.list, y);
			goto enter;
		}
		downloadmodsmenu.scrollbar_grab = true;
		M_DownloadMods_Mousemove(m_mousex, m_mousey);
		break;

	default:
		break;
	}
}

void M_DownloadMods_Mousemove(int cx, int cy)
{
	cy -= downloadmodsmenu.y;

	if (downloadmodsmenu.scrollbar_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			downloadmodsmenu.scrollbar_grab = false;
			return;
		}
		M_List_UseScrollbar(&downloadmodsmenu.list, cy);
	}

	M_List_Mousemove(&downloadmodsmenu.list, cy);
}

/*
==================
Demos Menu
==================
*/

#define MAX_VIS_DEMOS	11
#define DEMOS_PATH_ROW_Y	32
#define DEMOS_PATH_LABEL_X	16
#define DEMOS_PATH_BOX_X	56
#define DEMOS_PATH_TEXT_X	64
#define DEMOS_PATH_BOX_CHARS	30
#define DEMOS_PATH_MAX_DEPTH	8
#define DEMOS_ID1_ROW_Y	44
#define DEMOS_ID1_TEXT_X	(DEMOS_PATH_BOX_X + 8)
#define DEMOS_ID1_TEXT_SCALE	0.8125f

typedef enum
{
	DEMO_MINFRAMES_UNKNOWN,
	DEMO_MINFRAMES_PASS,
	DEMO_MINFRAMES_FAIL
} demo_minframes_state_t;

typedef struct
{
	char        name[MAX_QPATH];
	char        display[MAX_QPATH + 8];
	char        date[32];
	char        map[64];
	char        players[256];
	char        stats[64];
	char        duration[16];
	char        filesize[16];
	qboolean    active;
	qboolean    parsed;
	qboolean    from_id1;
	time_t      mtime;
	size_t      fsize;
	searchpath_t *source_searchpath;
	char        cache_key[MAX_OSPATH];
	demo_minframes_state_t minframes_state;
	int         frame_count;
} demoitem_t;

typedef struct
{
	char        map[64];
	char        players[256];
	float       duration;
	float       filesize_mb;
	qboolean    singleplayer;
	int         kills;
	int         total_kills;
	int         secrets;
	int         total_secrets;
	int         skill;          /* 0-3, or -1 if not present in the demo */
	int         frame_count;
} demoinfo_t;

typedef struct demo_metadata_cache_entry_s demo_metadata_cache_entry_t;

static qboolean M_Demos_BuildCacheKey(char *key, size_t key_size, const char *fname, searchpath_t *spath);
static void M_Demos_MetadataCache_Load(void);
static void M_Demos_MetadataCache_SaveIfDirty(void);
static void M_Demos_MetadataCache_MaybeFlush(void);
static qboolean M_Demos_MetadataCache_ApplyToItem(demoitem_t *di);
static const char *M_Demos_SkipExplicitRootPrefix(const char *name);

static qboolean M_Demos_BlockSoundForIO(void)
{
	return S_BlockSound();
}

static void M_Demos_UnblockSoundForIO(qboolean blocked_sound)
{
	if (blocked_sound)
		S_UnblockSound();
}

static void FormatDuration(float secs, char *out, size_t outlen)
{
	int m = (int)(secs / 60);
	int s = (int)(secs + 0.5f) % 60;
	q_snprintf(out, outlen, "%d:%02d", m, s);
}

static qboolean M_IsTimestampStart(const char *s)
{
	return q_isdigit((unsigned char)s[0]) && q_isdigit((unsigned char)s[1]) &&
		s[2] == '-' &&
		q_isdigit((unsigned char)s[3]) && q_isdigit((unsigned char)s[4]) &&
		s[5] == '-' &&
		q_isdigit((unsigned char)s[6]) && q_isdigit((unsigned char)s[7]) &&
		q_isdigit((unsigned char)s[8]) && q_isdigit((unsigned char)s[9]) &&
		s[10] == '-';
}

static qboolean M_IsDatePrefix(const char *s)
{
	return q_isdigit((unsigned char)s[0]) &&
		q_isdigit((unsigned char)s[1]) &&
		q_isdigit((unsigned char)s[2]) &&
		q_isdigit((unsigned char)s[3]) &&
		s[4] == '-' &&
		q_isdigit((unsigned char)s[5]) &&
		q_isdigit((unsigned char)s[6]) &&
		s[7] == '-' &&
		q_isdigit((unsigned char)s[8]) &&
		q_isdigit((unsigned char)s[9]) &&
		(s[10] == '-' || s[10] == '_');
}

static void M_InferDemoMapName(const char *name, char *out, size_t outlen)
{
	char base[MAX_QPATH];
	size_t i;

	COM_FileBase(COM_SkipPath(name), base, sizeof(base));
	if (!base[0])
	{
		out[0] = '\0';
		return;
	}

	if (M_IsDatePrefix(base))
		memmove(base, base + 11, strlen(base + 11) + 1);

	for (i = 0; base[i]; ++i)
	{
		if ((base[i] == '_' || base[i] == '-') && M_IsTimestampStart(base + i + 1))
		{
			base[i] = '\0';
			break;
		}
	}

	if (!strncmp(base, "start_", 6) || !strncmp(base, "start-", 6))
		base[0] = '\0';

	q_strlcpy(out, base, outlen);
}

static qboolean M_IsDemoMapNameChar(int c)
{
	return q_isalnum((unsigned char)c) || c == '_' || c == '-' || c == '+';
}

static qboolean M_FindDemoMapNameInData(const byte *data, int start, int length,
	char *out, size_t outlen)
{
	int scan_limit;
	int i;

	scan_limit = q_min(length, 512 * 1024);
	for (i = start; i <= scan_limit - 9; ++i)
	{
		int name_start;
		int j;

		if (memcmp(data + i, "maps/", 5))
			continue;

		name_start = i + 5;
		for (j = name_start; j <= scan_limit - 4; ++j)
		{
			int name_len;

			if (!memcmp(data + j, ".bsp", 4))
			{
				name_len = j - name_start;
				if (name_len <= 0)
					break;
				if (name_len >= (int)outlen)
					name_len = (int)outlen - 1;
				memcpy(out, data + name_start, name_len);
				out[name_len] = '\0';
				return true;
			}

			if (!M_IsDemoMapNameChar(data[j]))
				break;
		}
	}

	return false;
}

static inline int SkipCStringOffset(const byte* base, int off, int limit)
{
	const void* p = memchr(base + off, 0, (size_t)(limit - off));
	return p ? (int)((const byte*)p - base) + 1 : limit;
}

static int CompareFrags(const void* a, const void* b)
{
	const struct { char name[MAX_QPATH]; int frags; } *pa = a, * pb = b;
	return pb->frags - pa->frags;
}

static byte *M_LoadDemoInfoData(const char *name, searchpath_t *spath, int *length_out)
{
	char path[MAX_OSPATH];

	if (spath && !spath->pack)
	{
		if (!M_Demos_BuildCacheKey(path, sizeof(path), name, spath))
			return NULL;
		return CL_LoadDemoBufferFromFile(path, length_out);
	}

	return CL_LoadDemoBuffer(name, length_out);
}

/* ------------------------------------------------------------------------
   Bounds-checked demo message reader. The demo is a sequence of
   length-prefixed blocks, so a mis-parse inside one block is contained:
   we set ->error, abandon that block, and the next block's length prefix
   re-syncs us. The walker only needs to reach the per-frame stats, which
   the server always writes before the (protocol-dependent) entity data,
   so we can stop a block at the first entity/sound/unhandled command.
   ------------------------------------------------------------------------ */
typedef struct
{
	const byte	*data;
	int			pos;
	int			end;
	qboolean	error;
} demoreader_t;

static int DR_Byte(demoreader_t *r)
{
	if (r->error || r->pos + 1 > r->end) { r->error = true; return 0; }
	return r->data[r->pos++];
}
static int DR_Short(demoreader_t *r)
{
	short v;
	if (r->error || r->pos + 2 > r->end) { r->error = true; return 0; }
	memcpy(&v, r->data + r->pos, 2); r->pos += 2;
	return (short)LittleShort(v);
}
static int DR_Long(demoreader_t *r)
{
	int v;
	if (r->error || r->pos + 4 > r->end) { r->error = true; return 0; }
	memcpy(&v, r->data + r->pos, 4); r->pos += 4;
	return LittleLong(v);
}
static float DR_Float(demoreader_t *r)
{
	float v;
	if (r->error || r->pos + 4 > r->end) { r->error = true; return 0.0f; }
	memcpy(&v, r->data + r->pos, 4); r->pos += 4;
	return LittleFloat(v);
}
static void DR_Skip(demoreader_t *r, int n)
{
	if (r->error || n < 0 || r->pos + n > r->end) { r->error = true; return; }
	r->pos += n;
}
static void DR_SkipString(demoreader_t *r)
{
	while (!r->error && r->pos < r->end && r->data[r->pos] != 0) r->pos++;
	if (r->pos >= r->end) r->error = true; else r->pos++;
}

static int DR_CoordSize(unsigned int flags)
{
	if (flags & (PRFL_FLOATCOORD | PRFL_INT32COORD)) return 4;
	if (flags & PRFL_24BITCOORD) return 3;
	return 2;
}
static int DR_AngleSize(unsigned int flags)
{
	if (flags & PRFL_FLOATANGLE) return 4;
	if (flags & PRFL_SHORTANGLE) return 2;
	return 1;
}

static void DR_RecordStat(demoinfo_t *info, int *stat_kills, int *stat_secrets, int stat, int val)
{
	switch (stat)
	{
	case STAT_TOTALMONSTERS: info->total_kills = val; break;
	case STAT_TOTALSECRETS:  info->total_secrets = val; break;
	case STAT_MONSTERS:      if (val > *stat_kills)   *stat_kills = val; break;
	case STAT_SECRETS:       if (val > *stat_secrets) *stat_secrets = val; break;
	}
}

/* Parse svc_clientdata far enough to land on whatever follows it (the
   per-frame stats live just after). Mirrors CL_ParseClientdata for the
   NetQuake/FitzQuake/RMQ field layout; bit-driven, so no coords and no
   protocolflags dependence. (DP7/BJP3 size a couple of fields differently,
   but those are rare and a mis-size only abandons this one block.) */
static void DR_SkipClientdata(demoreader_t *r)
{
	unsigned int bits = (unsigned short)DR_Short(r);
	int i;

	if (bits & SU_EXTEND1) bits |= (unsigned int)DR_Byte(r) << 16;
	if (bits & SU_EXTEND2) bits |= (unsigned int)DR_Byte(r) << 24;

	if (bits & SU_VIEWHEIGHT) DR_Byte(r);
	if (bits & SU_IDEALPITCH) DR_Byte(r);
	for (i = 0; i < 3; i++)
	{
		if (bits & (SU_PUNCH1 << i)) DR_Byte(r);
		if (bits & (SU_VELOCITY1 << i)) DR_Byte(r);
	}
	DR_Long(r);				/* items (SU_ITEMS is forced on, so always sent) */
	if (bits & SU_WEAPONFRAME) DR_Byte(r);
	if (bits & SU_ARMOR) DR_Byte(r);
	if (bits & SU_WEAPON) DR_Byte(r);
	DR_Short(r);			/* health */
	DR_Skip(r, 6);			/* ammo, shells, nails, rockets, cells, activeweapon */
	if (bits & SU_WEAPON2) DR_Byte(r);
	if (bits & SU_ARMOR2) DR_Byte(r);
	if (bits & SU_AMMO2) DR_Byte(r);
	if (bits & SU_SHELLS2) DR_Byte(r);
	if (bits & SU_NAILS2) DR_Byte(r);
	if (bits & SU_ROCKETS2) DR_Byte(r);
	if (bits & SU_CELLS2) DR_Byte(r);
	if (bits & SU_WEAPONFRAME2) DR_Byte(r);
	if (bits & SU_WEAPONALPHA) DR_Byte(r);
}

static qboolean Parse_DemoInfo(const char* name, searchpath_t *spath, demoinfo_t* info)
{
	int length;
	byte* data = M_LoadDemoInfoData(name, spath, &length);
	if (!data) return false;

	if (length <= 0) { free(data); return false; }

	info->map[0] = info->players[0] = '\0';
	info->duration = 0.0f;
	info->filesize_mb = length / (1024.0f * 1024.0f);
	info->singleplayer = false;
	info->kills = info->total_kills = info->secrets = info->total_secrets = 0;
	info->skill = -1;
	info->frame_count = 0;

	int maxclients = 16;
	qboolean maxclients_found = false;

	/* single-player stat tracking: totals come from svc_updatestat during
	   signon; current counts come either from svc_updatestat or from the
	   svc_killedmonster/svc_foundsecret events that bump them client-side */
	int stat_kills = 0, stat_secrets = 0;
	int killed_events = 0, secret_events = 0;

	int  player_peak_frags[32];
	memset(player_peak_frags, 0x9D, sizeof(player_peak_frags));   /* -99 */
	qboolean player_has_name[32] = { 0 };
	char player_names[32][MAX_QPATH];
	memset(player_names, 0, sizeof(player_names));

	/* skip header line */
	int off = 0;
	while (off < length && data[off] != '\n') off++;
	if (off < length) off++;

	M_FindDemoMapNameInData(data, off, length, info->map, sizeof(info->map));
	float last_time = 0.0f;
	float first_time = -1.0f;	/* server clock is absolute uptime, not demo-relative */
	int frame_count = 0;

	int protocol = PROTOCOL_NETQUAKE;
	unsigned int protocolflags = 0;
	unsigned int pext2 = 0;
	qboolean predinfo = false;
	int coordsize = 2, anglesize = 1;
	qboolean done = false;

	while (!done && off <= length - 16)
	{
		int msg_len, msg_end;
		demoreader_t r;

		memcpy(&msg_len, data + off, sizeof(msg_len));
		msg_len = LittleLong(msg_len);
		off += 4;

		if (msg_len <= 0 || msg_len > MAX_MSGLEN || off + 12 + msg_len > length)
			break;

		off += 12;                    /* skip view angles */
		msg_end = off + msg_len;

		r.data = data; r.pos = off; r.end = msg_end; r.error = false;

		while (r.pos < r.end && !r.error)
		{
			int cmd = DR_Byte(&r);
			if (r.error) break;

			/* An entity update (high bit set) means we've passed the
			   per-frame stats the server writes earlier in the packet;
			   the entity format is protocol/flag-dependent (and FTE-delta
			   on modern demos), so stop here and re-sync on the next block. */
			if (cmd & U_SIGNAL)
				break;

			switch (cmd)
			{
			case svc_bad:
			case svc_nop:
				break;

			case svc_disconnect:
				r.pos = r.end;
				break;

			case svc_time:
				last_time = DR_Float(&r);
				if (predinfo) DR_Skip(&r, 2);	/* PEXT2_PREDINFO move-ack short */
				if (!r.error && first_time < 0.0f) first_time = last_time;
				frame_count++;
				break;

			case svc_version:
				DR_Long(&r);
				break;

			case svc_setview:
				DR_Short(&r);
				break;

			case svc_print:
				DR_SkipString(&r);
				break;

			case svc_stufftext:
			{
				int str_start = r.pos;
				DR_SkipString(&r);

				/* QSS forwards serverinfo cvars (incl. CVAR_SERVERINFO "skill")
				   as a //fullserverinfo "\key\value..." stuffcmd; older demos
				   simply lack this, leaving skill unknown. */
				if (!r.error && info->skill < 0)
				{
					const char *s = (const char *)(data + str_start);
					const char *pre = "//fullserverinfo ";
					int prelen = (int)strlen(pre);
					int avail = r.pos - 1 - str_start;	/* exclude null terminator */
					if (avail > prelen && !strncmp(s, pre, prelen))
					{
						const char *q1 = memchr(s + prelen, '"', avail - prelen);
						const char *q2 = q1 ? memchr(q1 + 1, '"', (s + avail) - (q1 + 1)) : NULL;
						if (q1 && q2)
						{
							char infostr[1024], skillbuf[16];
							int ilen = (int)(q2 - (q1 + 1));
							if (ilen >= (int)sizeof(infostr)) ilen = sizeof(infostr) - 1;
							memcpy(infostr, q1 + 1, ilen);
							infostr[ilen] = '\0';
							skillbuf[0] = '\0';
							Info_GetKey(infostr, "skill", skillbuf, sizeof(skillbuf));
							if (skillbuf[0])
								info->skill = atoi(skillbuf);
						}
					}
				}
				break;
			}

			case svc_setangle:
				DR_Skip(&r, 3 * anglesize);
				break;

			case svc_serverinfo:
			{
				int p = PROTOCOL_NETQUAKE;
				for (;;)
				{
					p = DR_Long(&r);
					if (r.error) break;
					if (p == PROTOCOL_FTE_PEXT1) { DR_Long(&r); continue; }
					if (p == PROTOCOL_FTE_PEXT2) { pext2 = (unsigned int)DR_Long(&r); continue; }
					break;
				}
				if (!r.error)
				{
					protocol = p;
					if (protocol == PROTOCOL_RMQ)
						protocolflags = (unsigned int)DR_Long(&r);
					else if (protocol == PROTOCOL_VERSION_DP7)
						protocolflags = PRFL_SHORTANGLE | PRFL_FLOATCOORD;
					else
						protocolflags = 0;
					predinfo = (pext2 & PEXT2_PREDINFO) != 0;
					coordsize = DR_CoordSize(protocolflags);
					anglesize = DR_AngleSize(protocolflags);
					if (predinfo)
						DR_SkipString(&r);	/* server gamedir */
					{
						int mc = DR_Byte(&r);
						if (!r.error)
						{
							if (mc < 1) mc = 1;
							if (mc > 32) mc = 32;
							maxclients = mc;
							maxclients_found = true;
						}
					}
				}
				/* the remainder is gametype + mapname + precache lists; the
				   map name is recovered by the global scan above */
				r.pos = r.end;
				break;
			}

			case svc_lightstyle:
				DR_Byte(&r);
				DR_SkipString(&r);
				break;

			case svc_updatename:
			{
				int pl = DR_Byte(&r);
				int start = r.pos;
				DR_SkipString(&r);
				if (!r.error && pl >= 0 && pl < maxclients)
				{
					int nlen = r.pos - 1 - start;
					if (nlen > 0 && nlen < MAX_QPATH)
					{
						char tmp[MAX_QPATH], clean[16];
						size_t len;
						memcpy(tmp, data + start, nlen); tmp[nlen] = '\0';
						q_strlcpy(clean, tmp, sizeof(clean));
						len = strlen(clean);
						while (len && isspace((unsigned char)clean[len - 1])) clean[--len] = '\0';
						if (len && strcmp(clean, "unconnected") &&
							strncmp(clean, "Player ", 7))
						{
							q_strlcpy(player_names[pl], clean, sizeof(player_names[pl]));
							player_has_name[pl] = true;
						}
					}
				}
				break;
			}

			case svc_updatefrags:
			{
				int pl = DR_Byte(&r);
				int fr = DR_Short(&r);
				if (!r.error && pl >= 0 && pl < maxclients && fr > player_peak_frags[pl])
					player_peak_frags[pl] = fr;
				break;
			}

			case svc_clientdata:
				DR_SkipClientdata(&r);
				break;

			case svc_stopsound:
				DR_Short(&r);
				break;

			case svc_updatecolors:
				DR_Skip(&r, 2);
				break;

			case svc_damage:
				DR_Skip(&r, 2);					/* armor, blood */
				DR_Skip(&r, 3 * coordsize);		/* hit origin */
				break;

			case svc_signonnum:
				DR_Byte(&r);
				break;

			case svc_setpause:
				DR_Byte(&r);
				break;

			case svc_centerprint:
				DR_SkipString(&r);
				break;

			case svc_finale:
				DR_SkipString(&r);
				done = true;	/* end of run; stats are final */
				break;

			case svc_cdtrack:
				DR_Skip(&r, 2);
				break;

			case svc_intermission:
				done = true;	/* no payload; stats are final */
				break;

			case svc_updatestat:
			{
				int stat = DR_Byte(&r);
				int val = DR_Long(&r);
				if (!r.error) DR_RecordStat(info, &stat_kills, &stat_secrets, stat, val);
				break;
			}

			case svcdp_updatestatbyte:
			{
				int stat = DR_Byte(&r);
				int val = DR_Byte(&r);
				if (!r.error) DR_RecordStat(info, &stat_kills, &stat_secrets, stat, val);
				break;
			}

			case svcfte_updatestatfloat:
			{
				int stat = DR_Byte(&r);
				float val = DR_Float(&r);
				if (!r.error) DR_RecordStat(info, &stat_kills, &stat_secrets, stat, (int)val);
				break;
			}

			case svcfte_updatestatstring:
				DR_Byte(&r);
				DR_SkipString(&r);
				break;

			case svcfte_updateentities:
				/* FTE replacement-deltas carry no svc_time; the server clock is
				   embedded at the head of this message, ahead of the entity
				   deltas (which we don't decode). */
				if (predinfo) DR_Skip(&r, 2);	/* input-sequence ack short */
				last_time = DR_Float(&r);
				if (!r.error && first_time < 0.0f) first_time = last_time;
				frame_count++;
				r.pos = r.end;
				break;

			case svc_killedmonster:
				killed_events++;
				break;

			case svc_foundsecret:
				secret_events++;
				break;

			default:
				/* sound/particle/baseline/temp-entity/fte-entities/etc. — these
				   carry protocol-dependent data and always follow the stats, so
				   abandon this block and re-sync on the next length-prefix. */
				r.pos = r.end;
				break;
			}

			if (done) break;
		}

		off = msg_end;	/* block framing always advances, so the loop terminates */
	}

{
	struct { char name[MAX_QPATH]; int frags; } list[32];
	int cnt = 0;

	for (int p = 0; p < maxclients; p++)
		if (player_has_name[p] && cnt < 32)
		{
			qboolean dup = false;
			for (int i = 0; i < cnt; i++)
				if (!strcmp(list[i].name, player_names[p]))
				{
					if (player_peak_frags[p] > list[i].frags)
						list[i].frags = player_peak_frags[p];
					dup = true;
					break;
				}
			if (!dup)
			{
				q_strlcpy(list[cnt].name, player_names[p],
					sizeof(list[cnt].name));
				list[cnt].frags = player_peak_frags[p];
				cnt++;
			}
		}

	/* Older/odd demos may not yield a maxclients from serverinfo; in that case
	   fall back to the number of distinct named players we saw — a lone (or no)
	   named client means single player. */
	if (maxclients_found)
		info->singleplayer = (maxclients == 1);
	else
		info->singleplayer = (cnt <= 1);

	info->kills = q_max(stat_kills, killed_events);
	info->secrets = q_max(stat_secrets, secret_events);

	if (!info->singleplayer)
	{
		qsort(list, cnt, sizeof(list[0]), CompareFrags);

		for (int i = 0; i < cnt; i++)
		{
			if (i) q_strlcat(info->players, ", ", sizeof(info->players));
			char buf[MAX_QPATH];
			q_snprintf(buf, sizeof(buf), "%s (%d)",
				list[i].name, list[i].frags);
			q_strlcat(info->players, buf, sizeof(info->players));
		}
	}

	/* The server clock is absolute uptime (it can start at any value when
	   connecting to a long-running server), so the playable length is the
	   span from the first frame to the last. Fall back to a frame-count
	   estimate only if the stream yielded no time at all. */
	if (first_time >= 0.0f && last_time > first_time)
		info->duration = last_time - first_time;
	else if (frame_count > 0)
		info->duration = frame_count / 72.0f;
	else
		info->duration = 0.0f;
	if (!info->map[0])
		M_InferDemoMapName(name, info->map, sizeof(info->map));
	info->frame_count = frame_count;
}

free(data);
return true;
}

static struct
{
	menulist_t			list;
	enum m_state_e		prev;
	int					x, y, cols;
	int					democount;
	int					prev_cursor;
	menuticker_t		ticker;
	demoitem_t			*items;
	qboolean			scrollbar_grab;
	int*                filtered_indices;
	menu_textfield_t	path_field;
	char				path_suffix[MAX_QPATH];
	char				remembered_path_suffix[MAX_QPATH];
	char				path_hint[MAX_QPATH];
	char				path_tabpartial[MAX_QPATH];
	filelist_item_t		*path_folders;
	qboolean			path_editing;
	qboolean			path_valid;
	qboolean			show_id1;
	int					bg_parse_cursor;       // next index to background-parse for searchable metadata
	int					bg_parse_refilter_count; // parsed metadata entries since last search refilter
	int					minframes_threshold;
	int					minframes_check_cursor;
	int					minframes_hidden_since_refilter;
} demosmenu;

static const char *M_Demos_CurrentGameName(void)
{
	const char *gamedir = COM_SkipPath(com_gamedir);

	if (!gamedir || !gamedir[0])
		gamedir = GAMENAME;

	return gamedir;
}

static qboolean M_Demos_CurrentGameIsId1(void)
{
	return !q_strcasecmp(M_Demos_CurrentGameName(), GAMENAME);
}

/* The attract-mode demos shipped in the official paks: id1 and Rogue both
   use demo1..demo3 (disambiguated by gamedir / from_id1), Hipnotic uses
   hipdemo1..hipdemo4. Returns the caption to show, or NULL. */
static const char *M_Demos_OriginalIntroDemoLabel(const char *name, qboolean from_id1)
{
	const char *base = COM_SkipPath(name);
	const char *game = M_Demos_CurrentGameName();
	qboolean is_demo123 =
		!q_strcasecmp(base, "demo1.dem") ||
		!q_strcasecmp(base, "demo2.dem") ||
		!q_strcasecmp(base, "demo3.dem");

	/* id1 demos surfaced while playing a mod (the show-id1 toggle) */
	if (from_id1)
		return is_demo123 ? "original Quake intro demo" : NULL;

	if (!q_strcasecmp(game, "hipnotic") &&
		(!q_strcasecmp(base, "hipdemo1.dem") || !q_strcasecmp(base, "hipdemo2.dem") ||
		 !q_strcasecmp(base, "hipdemo3.dem") || !q_strcasecmp(base, "hipdemo4.dem")))
		return "original Hipnotic intro demo";

	if (is_demo123)
	{
		if (M_Demos_CurrentGameIsId1())
			return "original Quake intro demo";
		if (!q_strcasecmp(game, "rogue"))
			return "original Rogue intro demo";
	}

	return NULL;
}

static const char *M_Demos_PathBase(void)
{
	static char base[MAX_QPATH];
	const char *gamedir = M_Demos_CurrentGameName();

	q_snprintf(base, sizeof(base), "/%s/demos", gamedir);
	return base;
}

static const char *M_Demos_SkipPathBasePrefix(const char *path)
{
	char game_base[MAX_QPATH];
	const char *gamedir = M_Demos_CurrentGameName();
	size_t len;

	while (*path == '/')
		++path;

	q_snprintf(game_base, sizeof(game_base), "%s/demos", gamedir);
	len = strlen(game_base);
	if (!q_strncasecmp(path, game_base, len) && (path[len] == '\0' || path[len] == '/'))
	{
		path += len;
		while (*path == '/')
			++path;
		return path;
	}

	if (!q_strncasecmp(path, "id1/demos", 9) && (path[9] == '\0' || path[9] == '/'))
	{
		path += 9;
		while (*path == '/')
			++path;
		return path;
	}

	if (!q_strncasecmp(path, "demos", 5) && (path[5] == '\0' || path[5] == '/'))
	{
		path += 5;
		while (*path == '/')
			++path;
	}

	return path;
}

static qboolean M_Demos_SearchPathMatchesGame(searchpath_t *search, const char *gamedir)
{
	size_t len;

	if (!search || !gamedir || !gamedir[0])
		return false;

	len = strlen(gamedir);
	return !q_strncasecmp(search->purename, gamedir, len) &&
		(search->purename[len] == '\0' ||
		 search->purename[len] == '/' ||
		 search->purename[len] == '\\');
}

static qboolean M_Demos_SearchPathAllowed(searchpath_t *search, qboolean *from_id1)
{
	qboolean is_id1;

	if (from_id1)
		*from_id1 = false;

	if (M_Demos_SearchPathMatchesGame(search, M_Demos_CurrentGameName()))
		return true;

	is_id1 = M_Demos_SearchPathMatchesGame(search, GAMENAME);
	if (!M_Demos_CurrentGameIsId1() && demosmenu.show_id1 && is_id1)
	{
		if (from_id1)
			*from_id1 = true;
		return true;
	}

	return false;
}

static qboolean M_Demos_HasId1SearchPath(void)
{
	searchpath_t *search;

	for (search = com_searchpaths; search; search = search->next)
	{
		if (M_Demos_SearchPathMatchesGame(search, GAMENAME))
			return true;
	}

	return false;
}

static qboolean M_Demos_ShowId1Toggle(void)
{
	return !M_Demos_CurrentGameIsId1() && M_Demos_HasId1SearchPath();
}

static void M_Demos_ClearFileList(filelist_item_t **list)
{
	filelist_item_t *next;

	while (*list)
	{
		next = (*list)->next;
		Z_Free(*list);
		*list = next;
	}
}

static void M_Demos_FreeItems(void)
{
	M_Demos_MetadataCache_SaveIfDirty();

	if (demosmenu.items)
	{
		Vec_Free((void**)&demosmenu.items);
		demosmenu.items = NULL;
	}
	if (demosmenu.filtered_indices)
	{
		Vec_Free((void**)&demosmenu.filtered_indices);
		demosmenu.filtered_indices = NULL;
	}
	demosmenu.democount = 0;
	demosmenu.bg_parse_cursor = 0;
	demosmenu.bg_parse_refilter_count = 0;
	demosmenu.minframes_check_cursor = 0;
	demosmenu.minframes_hidden_since_refilter = 0;
}

static void M_Demos_AddEx(const char* name, const char* date, const char *display,
	qboolean from_id1, time_t mtime, size_t fsize, searchpath_t *spath)
{
    demoitem_t tempDemo;
	char display_with_source[MAX_QPATH + 8];
	int i;

	memset(&tempDemo, 0, sizeof(tempDemo));

	if (!date)
		date = "Unknown Date";

	for (i = 0; i < demosmenu.democount; i++)
	{
		if (!q_strcasecmp(name, demosmenu.items[i].name))
			return;
	}

	q_strlcpy(tempDemo.name, name, sizeof(tempDemo.name));
	if (from_id1 && !M_Demos_CurrentGameIsId1())
	{
		q_snprintf(display_with_source, sizeof(display_with_source), "%s [id1]",
			display && display[0] ? display : name);
		q_strlcpy(tempDemo.display, display_with_source, sizeof(tempDemo.display));
	}
	else
		q_strlcpy(tempDemo.display, display && display[0] ? display : name, sizeof(tempDemo.display));
	q_strlcpy(tempDemo.date, date, sizeof(tempDemo.date));
	M_InferDemoMapName(name, tempDemo.map, sizeof(tempDemo.map));
	tempDemo.players[0] = '\0';
	tempDemo.stats[0] = '\0';
	tempDemo.duration[0] = '\0';
	tempDemo.filesize[0] = '\0';
    tempDemo.active = false;
	tempDemo.parsed = false;
	tempDemo.from_id1 = from_id1;
	tempDemo.mtime = mtime;
	tempDemo.fsize = fsize;
	tempDemo.source_searchpath = spath;
	tempDemo.minframes_state = demosmenu.minframes_threshold > 0 ?
		DEMO_MINFRAMES_UNKNOWN : DEMO_MINFRAMES_PASS;
	tempDemo.frame_count = -1;
	// Key off tempDemo.name, not the raw name param: di->name is truncated to
	// MAX_QPATH and that truncated form is what CountFrames/ParseItem feed back
	// into the cache, so the key must be derived from the same string.
	M_Demos_BuildCacheKey(tempDemo.cache_key, sizeof(tempDemo.cache_key), tempDemo.name, spath);
	M_Demos_MetadataCache_ApplyToItem(&tempDemo);

    int insertPos = demosmenu.democount;

    for (int i = 0; i < demosmenu.democount; i++)
    {
        if (q_sortdemos(date, demosmenu.items[i].date) > 0) // If new date is newer
        {
            insertPos = i;
            break;
        }
    }

    // Increase the size of demosmenu.items by one
    Vec_Grow((void**)&demosmenu.items, sizeof(demoitem_t), demosmenu.democount + 1);

    if (insertPos != demosmenu.democount)
    {
        // Shift items to make room for the new demo
        memmove(&demosmenu.items[insertPos + 1], &demosmenu.items[insertPos], sizeof(demoitem_t) * (demosmenu.democount - insertPos));
    }

    // Insert the new demo
    demosmenu.items[insertPos] = tempDemo;

    demosmenu.democount++;
}

static void M_Demos_AddFolderAncestors(const char *relpath)
{
	char folder[MAX_QPATH];
	char *slash;

	if (!relpath || !relpath[0])
		return;

	q_strlcpy(folder, relpath, sizeof(folder));
	for (slash = folder; *slash; ++slash)
	{
		if (*slash == '/' || *slash == '\\')
		{
			*slash = '\0';
			if (folder[0])
				FileList_Add(folder, NULL, &demosmenu.path_folders);
			*slash = '/';
		}
	}

	FileList_Add(folder, NULL, &demosmenu.path_folders);
}

static void M_Demos_AddFolderFromDemoPath(const char *path)
{
	const char *rel;
	const char *slash;
	char folder[MAX_QPATH];
	size_t len;

	if (q_strncasecmp(path, "demos/", 6))
		return;

	rel = path + 6;
	slash = strrchr(rel, '/');
	if (!slash)
		return;

	len = (size_t)(slash - rel);
	if (len <= 0)
		return;
	if (len >= sizeof(folder))
		len = sizeof(folder) - 1;

	memcpy(folder, rel, len);
	folder[len] = '\0';
	M_Demos_AddFolderAncestors(folder);
}

static void M_Demos_ScanPakFolders(void)
{
	searchpath_t *search;
	pack_t *pak;
	int i;

	for (search = com_searchpaths; search; search = search->next)
	{
		if (!search->pack)
			continue;
		if (!M_Demos_SearchPathAllowed(search, NULL))
			continue;

		pak = search->pack;
		for (i = 0; i < pak->numfiles; i++)
		{
			const char *ext = COM_FileGetExtension(pak->files[i].name);
			if (q_strcasecmp(ext, "dem") && q_strcasecmp(ext, "dz"))
				continue;
			M_Demos_AddFolderFromDemoPath(pak->files[i].name);
		}
	}
}

static void M_Demos_ScanPhysicalFolders(const char *basepath, const char *relpath, int depth)
{
	char path[MAX_OSPATH];

	if (depth >= DEMOS_PATH_MAX_DEPTH)
		return;

	if (relpath && relpath[0])
		q_snprintf(path, sizeof(path), "%s/%s", basepath, relpath);
	else
		q_strlcpy(path, basepath, sizeof(path));

#ifdef _WIN32
	{
		char searchpath[MAX_OSPATH];
		WIN32_FIND_DATA fdat;
		HANDLE fhnd;

		q_snprintf(searchpath, sizeof(searchpath), "%s/*", path);
		fhnd = FindFirstFile(searchpath, &fdat);
		if (fhnd == INVALID_HANDLE_VALUE)
			return;

		do
		{
			char child_rel[MAX_QPATH];

			if (!(fdat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				continue;
			if (fdat.cFileName[0] == '.')
				continue;

			if (relpath && relpath[0])
				q_snprintf(child_rel, sizeof(child_rel), "%s/%s", relpath, fdat.cFileName);
			else
				q_strlcpy(child_rel, fdat.cFileName, sizeof(child_rel));

			FileList_Add(child_rel, NULL, &demosmenu.path_folders);
			M_Demos_ScanPhysicalFolders(basepath, child_rel, depth + 1);
		} while (FindNextFile(fhnd, &fdat));

		FindClose(fhnd);
	}
#else
	{
		DIR *dir_p;
		struct dirent *dir_t;

		dir_p = opendir(path);
		if (!dir_p)
			return;

		while ((dir_t = readdir(dir_p)) != NULL)
		{
			char fullpath[MAX_OSPATH];
			char child_rel[MAX_QPATH];
			struct stat st;

			if (dir_t->d_name[0] == '.')
				continue;

			q_snprintf(fullpath, sizeof(fullpath), "%s/%s", path, dir_t->d_name);
			if (stat(fullpath, &st) < 0 || !S_ISDIR(st.st_mode))
				continue;

			if (relpath && relpath[0])
				q_snprintf(child_rel, sizeof(child_rel), "%s/%s", relpath, dir_t->d_name);
			else
				q_strlcpy(child_rel, dir_t->d_name, sizeof(child_rel));

			FileList_Add(child_rel, NULL, &demosmenu.path_folders);
			M_Demos_ScanPhysicalFolders(basepath, child_rel, depth + 1);
		}

		closedir(dir_p);
	}
#endif
}

static void M_Demos_RebuildFolderList(void)
{
	searchpath_t *search;
	char demos_path[MAX_OSPATH];
	qboolean blocked_sound;

	blocked_sound = M_Demos_BlockSoundForIO();
	M_Demos_ClearFileList(&demosmenu.path_folders);

	for (search = com_searchpaths; search; search = search->next)
	{
		if (search->pack)
			continue;
		if (!M_Demos_SearchPathAllowed(search, NULL))
			continue;

		q_snprintf(demos_path, sizeof(demos_path), "%s/demos", search->filename);
		M_Demos_ScanPhysicalFolders(demos_path, NULL, 0);
	}

	M_Demos_ScanPakFolders();
	M_Demos_UnblockSoundForIO(blocked_sound);
}

static void M_Demos_FormatFileDate(time_t mtime, char *out, size_t outlen)
{
	struct tm *tm;

	if (!mtime)
	{
		q_strlcpy(out, "Unknown Date", outlen);
		return;
	}

	tm = localtime(&mtime);
	if (!tm)
	{
		q_strlcpy(out, "Unknown Date", outlen);
		return;
	}

	q_snprintf(out, outlen, "%04d-%02d-%02d %02d:%02d:%02d",
		tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);
}

typedef struct
{
	qboolean	explicit_root;
} demos_list_ctx_t;

#define DEMOS_METADATA_CACHE_FILENAME			"demos_metadata_cache.json"
#define DEMOS_METADATA_CACHE_SCHEMA_VERSION		1
#define DEMOS_METADATA_CACHE_PARSER_VERSION		1
#define DEMOS_METADATA_CACHE_MAX_FILE_SIZE		(16 * 1024 * 1024)
#define DEMOS_METADATA_CACHE_HASH_BUCKETS		4096
#define DEMOS_METADATA_CACHE_FLUSH_DIRTY		200

struct demo_metadata_cache_entry_s
{
	char key[MAX_OSPATH];
	time_t mtime;
	size_t fsize;
	qboolean parse_known;
	qboolean parse_ok;
	demoinfo_t info;
	qboolean frame_count_valid;
	int frame_count;
	qboolean frame_exact;
	demo_metadata_cache_entry_t *next;
	demo_metadata_cache_entry_t *hash_next;
};

static demo_metadata_cache_entry_t *demo_metadata_cache_entries;
static demo_metadata_cache_entry_t *demo_metadata_cache_hash[DEMOS_METADATA_CACHE_HASH_BUCKETS];
static qboolean demo_metadata_cache_loaded;
static qboolean demo_metadata_cache_dirty;
static int demo_metadata_cache_dirty_count;

static const char *M_Demos_JSONBool(qboolean value)
{
	return value ? "true" : "false";
}

static qboolean M_Demos_MetadataCachePath(char *path, size_t path_size)
{
	int result = q_snprintf(path, path_size, "%s/id1/backups/%s", com_basedir, DEMOS_METADATA_CACHE_FILENAME);

	if (result < 0 || (size_t)result >= path_size)
	{
		if (path_size)
			path[0] = '\0';
		return false;
	}

	return true;
}

static qboolean M_Demos_MetadataCacheEnsureDir(void)
{
	char path[MAX_OSPATH];
	int result;

	result = q_snprintf(path, sizeof(path), "%s/id1", com_basedir);
	if (result < 0 || (size_t)result >= sizeof(path))
		return false;
	Sys_mkdir(path);

	result = q_snprintf(path, sizeof(path), "%s/id1/backups", com_basedir);
	if (result < 0 || (size_t)result >= sizeof(path))
		return false;
	Sys_mkdir(path);
	return true;
}

static qboolean M_Demos_BuildCacheKey(char *key, size_t key_size, const char *fname, searchpath_t *spath)
{
	const char *disk_name;
	int result;

	if (!key_size)
		return false;
	if (!fname)
	{
		key[0] = '\0';
		return false;
	}

	disk_name = M_Demos_SkipExplicitRootPrefix(fname);

	if (spath && spath->pack)
		result = q_snprintf(key, key_size, "%s|%s", spath->filename, fname);
	else if (spath)
		result = q_snprintf(key, key_size, "%s/%s", spath->filename, disk_name);
	else if (q_strlcpy(key, disk_name, key_size) >= key_size)
	{
		key[0] = '\0';
		return false;
	}
	else
		return true;

	if (result < 0 || (size_t)result >= key_size)
	{
		key[0] = '\0';
		return false;
	}

	return true;
}

static demo_metadata_cache_entry_t *M_Demos_MetadataCacheFind(const char *key)
{
	demo_metadata_cache_entry_t *entry;
	unsigned bucket;

	if (!key || !key[0])
		return NULL;

	bucket = COM_HashString(key) % DEMOS_METADATA_CACHE_HASH_BUCKETS;
	for (entry = demo_metadata_cache_hash[bucket]; entry; entry = entry->hash_next)
	{
		if (!strcmp(entry->key, key))
			return entry;
	}

	return NULL;
}

static demo_metadata_cache_entry_t *M_Demos_MetadataCacheAlloc(const char *key)
{
	demo_metadata_cache_entry_t *entry;
	unsigned bucket;

	entry = (demo_metadata_cache_entry_t *)calloc(1, sizeof(*entry));
	if (!entry)
		return NULL;

	q_strlcpy(entry->key, key, sizeof(entry->key));
	entry->info.skill = -1;
	entry->info.frame_count = 0;
	entry->frame_count = -1;

	bucket = COM_HashString(entry->key) % DEMOS_METADATA_CACHE_HASH_BUCKETS;
	entry->hash_next = demo_metadata_cache_hash[bucket];
	demo_metadata_cache_hash[bucket] = entry;

	entry->next = demo_metadata_cache_entries;
	demo_metadata_cache_entries = entry;
	return entry;
}

static demo_metadata_cache_entry_t *M_Demos_MetadataCacheUpsert(const char *key)
{
	demo_metadata_cache_entry_t *entry = M_Demos_MetadataCacheFind(key);

	if (entry)
		return entry;

	return M_Demos_MetadataCacheAlloc(key);
}

static qboolean M_Demos_MetadataCacheEntryMatches(const demo_metadata_cache_entry_t *entry,
	time_t mtime, size_t fsize)
{
	return entry && entry->mtime == mtime && entry->fsize == fsize;
}

static void M_Demos_MetadataCacheMarkDirty(void)
{
	demo_metadata_cache_dirty = true;
	demo_metadata_cache_dirty_count++;
}

static qboolean M_Demos_JSONReadBool(const jsonentry_t *entry, const char *name, qboolean *out)
{
	const qboolean *value = JSON_FindBoolean(entry, name);

	if (!value)
		return false;

	*out = *value;
	return true;
}

static qboolean M_Demos_JSONReadInt(const jsonentry_t *entry, const char *name,
	int min_value, int max_value, int *out)
{
	const double *value = JSON_FindNumber(entry, name);

	if (!value || !isfinite(*value) || *value < min_value || *value > max_value)
		return false;

	*out = (int)*value;
	return true;
}

static qboolean M_Demos_JSONReadFloat(const jsonentry_t *entry, const char *name, float *out)
{
	const double *value = JSON_FindNumber(entry, name);

	if (!value || !isfinite(*value))
		return false;

	*out = (float)*value;
	return true;
}

static qboolean M_Demos_JSONReadULLString(const jsonentry_t *entry, const char *name,
	unsigned long long *out)
{
	const char *str = JSON_FindString(entry, name);
	char *end;
	unsigned long long value;

	if (!str || !str[0])
		return false;

	errno = 0;
	value = strtoull(str, &end, 10);
	if (errno != 0 || end == str || *end != '\0')
		return false;

	*out = value;
	return true;
}

static int M_Demos_HexValue(char c)
{
	if ((unsigned int)(c - '0') < 10)
		return c - '0';
	if ((unsigned int)(c - 'a') < 6)
		return c + (10 - 'a');
	if ((unsigned int)(c - 'A') < 6)
		return c + (10 - 'A');
	return -1;
}

static void M_Demos_EncodeHexString(const char *src, char *hex, size_t hex_size)
{
	static const char digits[] = "0123456789abcdef";
	size_t i, j;

	for (i = 0, j = 0; src[i] && j + 2 < hex_size; i++)
	{
		unsigned char c = (unsigned char)src[i];
		hex[j++] = digits[c >> 4];
		hex[j++] = digits[c & 0x0f];
	}

	hex[j] = '\0';
}

static qboolean M_Demos_DecodeHexString(const char *hex, char *dst, size_t dst_size)
{
	size_t i, j;

	if (!hex || !dst || !dst_size)
		return false;

	for (i = 0, j = 0; hex[i]; i += 2)
	{
		int hi, lo;
		char c;

		if (!hex[i + 1] || j + 1 >= dst_size)
			return false;

		hi = M_Demos_HexValue(hex[i]);
		lo = M_Demos_HexValue(hex[i + 1]);
		if (hi < 0 || lo < 0)
			return false;

		c = (char)((hi << 4) | lo);
		if (!c)
			return false;
		dst[j++] = c;
	}

	dst[j] = '\0';
	return true;
}

static qboolean M_Demos_DecodeLegacyJSONStringBytes(const char *src, char *dst, size_t dst_size)
{
	const unsigned char *in;
	size_t out;

	if (!src || !dst || !dst_size)
		return false;

	in = (const unsigned char *)src;
	out = 0;

	while (*in)
	{
		if (out + 1 >= dst_size)
			return false;

		if ((in[0] & 0xe0) == 0xc0 && (in[1] & 0xc0) == 0x80)
		{
			unsigned int codepoint = ((in[0] & 0x1f) << 6) | (in[1] & 0x3f);
			if (codepoint >= 0x80 && codepoint <= 0xff)
			{
				dst[out++] = (char)codepoint;
				in += 2;
				continue;
			}
		}

		dst[out++] = (char)*in++;
	}

	dst[out] = '\0';
	return true;
}

static qboolean M_Demos_MetadataCacheReadIdentity(const jsonentry_t *entry,
	time_t *mtime, size_t *fsize)
{
	unsigned long long mtime64;
	unsigned long long fsize64;
	time_t mt;
	size_t fs;

	if (!M_Demos_JSONReadULLString(entry, "mtime", &mtime64) ||
		!M_Demos_JSONReadULLString(entry, "fsize", &fsize64))
		return false;

	mt = (time_t)mtime64;
	fs = (size_t)fsize64;
	if ((unsigned long long)mt != mtime64 || (unsigned long long)fs != fsize64)
		return false;

	*mtime = mt;
	*fsize = fs;
	return true;
}

static qboolean M_Demos_MetadataCacheLoadEntry(const jsonentry_t *json_entry)
{
	const char *key;
	const char *map;
	const char *players_hex;
	const char *legacy_players;
	demo_metadata_cache_entry_t *entry;
	demoinfo_t info;
	time_t mtime;
	size_t fsize;
	qboolean parsed;
	qboolean parse_ok = false;
	qboolean rewrite = false;
	qboolean frame_valid;
	qboolean frame_exact = false;
	int frame_count = -1;

	if (!json_entry || json_entry->type != JSON_OBJECT)
		return false;

	key = JSON_FindString(json_entry, "key");
	if (!key || !key[0] || strlen(key) >= MAX_OSPATH)
		return false;

	if (!M_Demos_MetadataCacheReadIdentity(json_entry, &mtime, &fsize) ||
		!M_Demos_JSONReadBool(json_entry, "parsed", &parsed) ||
		!M_Demos_JSONReadBool(json_entry, "frame_count_valid", &frame_valid))
		return false;

	memset(&info, 0, sizeof(info));
	info.skill = -1;
	info.frame_count = 0;

	if (parsed)
	{
		if (!M_Demos_JSONReadBool(json_entry, "parse_ok", &parse_ok))
			return false;

		if (!parse_ok)
			parsed = false;
		else
		{
			map = JSON_FindString(json_entry, "map");
			if (!map)
				return false;

			q_strlcpy(info.map, map, sizeof(info.map));
			players_hex = JSON_FindString(json_entry, "players_hex");
			legacy_players = JSON_FindString(json_entry, "players");
			if (players_hex)
			{
				if (!M_Demos_DecodeHexString(players_hex, info.players, sizeof(info.players)))
					return false;
			}
			else if (legacy_players)
			{
				if (!M_Demos_DecodeLegacyJSONStringBytes(legacy_players, info.players, sizeof(info.players)))
					return false;
				rewrite = true;
			}
			else
				return false;

			if (!M_Demos_JSONReadFloat(json_entry, "duration", &info.duration) ||
				!M_Demos_JSONReadFloat(json_entry, "filesize_mb", &info.filesize_mb) ||
				!M_Demos_JSONReadBool(json_entry, "singleplayer", &info.singleplayer) ||
				!M_Demos_JSONReadInt(json_entry, "kills", 0, INT_MAX, &info.kills) ||
				!M_Demos_JSONReadInt(json_entry, "total_kills", 0, INT_MAX, &info.total_kills) ||
				!M_Demos_JSONReadInt(json_entry, "secrets", 0, INT_MAX, &info.secrets) ||
				!M_Demos_JSONReadInt(json_entry, "total_secrets", 0, INT_MAX, &info.total_secrets) ||
				!M_Demos_JSONReadInt(json_entry, "skill", -1, INT_MAX, &info.skill) ||
				!M_Demos_JSONReadInt(json_entry, "parser_frame_count", 0, INT_MAX, &info.frame_count))
				return false;
		}
	}

	if (frame_valid)
	{
		if (!M_Demos_JSONReadInt(json_entry, "frame_count", -1, INT_MAX, &frame_count) ||
			!M_Demos_JSONReadBool(json_entry, "frame_exact", &frame_exact))
			return false;
	}

	if (!parsed && !frame_valid)
		return false;

	entry = M_Demos_MetadataCacheUpsert(key);
	if (!entry)
		return false;

	entry->mtime = mtime;
	entry->fsize = fsize;
	entry->parse_known = parsed;
	entry->parse_ok = parsed && parse_ok;
	entry->info = info;
	entry->frame_count_valid = frame_valid;
	entry->frame_count = frame_valid ? frame_count : -1;
	entry->frame_exact = frame_valid && frame_exact;
	if (rewrite)
		M_Demos_MetadataCacheMarkDirty();

	return true;
}

static void M_Demos_MetadataCacheLoadFromText(const char *text)
{
	json_t *json;
	const jsonentry_t *entries;
	const jsonentry_t *entry;
	int schema_version;
	int parser_version;

	json = JSON_Parse(text);
	if (!json || !json->root || json->root->type != JSON_OBJECT)
	{
		if (json)
			JSON_Free(json);
		return;
	}

	if (!M_Demos_JSONReadInt(json->root, "schema_version", 1, INT_MAX, &schema_version) ||
		!M_Demos_JSONReadInt(json->root, "parser_version", 1, INT_MAX, &parser_version) ||
		schema_version != DEMOS_METADATA_CACHE_SCHEMA_VERSION ||
		parser_version != DEMOS_METADATA_CACHE_PARSER_VERSION)
	{
		JSON_Free(json);
		return;
	}

	entries = JSON_Find(json->root, "entries", JSON_ARRAY);
	if (!entries)
	{
		JSON_Free(json);
		return;
	}

	for (entry = entries->firstchild; entry; entry = entry->next)
		M_Demos_MetadataCacheLoadEntry(entry);

	JSON_Free(json);
}

static void M_Demos_MetadataCache_Load(void)
{
	char path[MAX_OSPATH];
	FILE *file;
	long file_size;
	char *text;

	if (demo_metadata_cache_loaded)
		return;

	demo_metadata_cache_loaded = true;
	if (!M_Demos_MetadataCachePath(path, sizeof(path)))
		return;

	file = fopen(path, "rb");
	if (!file)
		return;

	if (fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		return;
	}
	file_size = ftell(file);
	rewind(file);

	if (file_size <= 0 || file_size > DEMOS_METADATA_CACHE_MAX_FILE_SIZE)
	{
		fclose(file);
		return;
	}

	text = (char *)malloc((size_t)file_size + 1);
	if (!text)
	{
		fclose(file);
		return;
	}

	if (fread(text, 1, (size_t)file_size, file) != (size_t)file_size)
	{
		free(text);
		fclose(file);
		return;
	}

	text[file_size] = '\0';
	fclose(file);

	M_Demos_MetadataCacheLoadFromText(text);
	free(text);
}

static void M_Demos_MetadataCache_SaveIfDirty(void)
{
	char path[MAX_OSPATH];
	char tmp_path[MAX_OSPATH];
	FILE *file;
	demo_metadata_cache_entry_t *entry;
	qboolean ok = true;
	qboolean first = true;
	int result;

	if (!demo_metadata_cache_loaded || !demo_metadata_cache_dirty)
		return;

	if (!M_Demos_MetadataCacheEnsureDir())
		return;
	if (!M_Demos_MetadataCachePath(path, sizeof(path)))
		return;
	result = q_snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
	if (result < 0 || (size_t)result >= sizeof(tmp_path))
		return;

	file = fopen(tmp_path, "w");
	if (!file)
		return;

	if (fprintf(file, "{\n") < 0 ||
		fprintf(file, "  \"schema_version\": %d,\n", DEMOS_METADATA_CACHE_SCHEMA_VERSION) < 0 ||
		fprintf(file, "  \"parser_version\": %d,\n", DEMOS_METADATA_CACHE_PARSER_VERSION) < 0 ||
		fprintf(file, "  \"entries\": [\n") < 0)
		ok = false;

	for (entry = demo_metadata_cache_entries; ok && entry; entry = entry->next)
	{
		char *escaped_key;
		char *escaped_map;
		char players_hex[sizeof(entry->info.players) * 2 + 1];
		const char *map = entry->parse_ok ? entry->info.map : "";
		const char *players = entry->parse_ok ? entry->info.players : "";

		if (!entry->parse_known && !entry->frame_count_valid)
			continue;

		escaped_key = JSON_EscapeString(entry->key);
		escaped_map = JSON_EscapeString(map);
		M_Demos_EncodeHexString(players, players_hex, sizeof(players_hex));
		if (!escaped_key || !escaped_map)
		{
			free(escaped_key);
			free(escaped_map);
			ok = false;
			break;
		}

		if (!first && fprintf(file, ",\n") < 0)
			ok = false;
		first = false;

		if (ok)
		{
			if (fprintf(file, "    {\n") < 0 ||
				fprintf(file, "      \"key\": \"%s\",\n", escaped_key) < 0 ||
				fprintf(file, "      \"mtime\": \"%lld\",\n", (long long)entry->mtime) < 0 ||
				fprintf(file, "      \"fsize\": \"%llu\",\n", (unsigned long long)entry->fsize) < 0 ||
				fprintf(file, "      \"parsed\": %s,\n", M_Demos_JSONBool(entry->parse_known)) < 0 ||
				fprintf(file, "      \"parse_ok\": %s,\n", M_Demos_JSONBool(entry->parse_ok)) < 0 ||
				fprintf(file, "      \"map\": \"%s\",\n", escaped_map) < 0 ||
				fprintf(file, "      \"players_hex\": \"%s\",\n", players_hex) < 0 ||
				fprintf(file, "      \"duration\": %.9g,\n", entry->parse_ok ? (double)entry->info.duration : 0.0) < 0 ||
				fprintf(file, "      \"filesize_mb\": %.9g,\n", entry->parse_ok ? (double)entry->info.filesize_mb : 0.0) < 0 ||
				fprintf(file, "      \"singleplayer\": %s,\n", M_Demos_JSONBool(entry->parse_ok && entry->info.singleplayer)) < 0 ||
				fprintf(file, "      \"kills\": %d,\n", entry->parse_ok ? entry->info.kills : 0) < 0 ||
				fprintf(file, "      \"total_kills\": %d,\n", entry->parse_ok ? entry->info.total_kills : 0) < 0 ||
				fprintf(file, "      \"secrets\": %d,\n", entry->parse_ok ? entry->info.secrets : 0) < 0 ||
				fprintf(file, "      \"total_secrets\": %d,\n", entry->parse_ok ? entry->info.total_secrets : 0) < 0 ||
				fprintf(file, "      \"skill\": %d,\n", entry->parse_ok ? entry->info.skill : -1) < 0 ||
				fprintf(file, "      \"parser_frame_count\": %d,\n", entry->parse_ok ? entry->info.frame_count : 0) < 0 ||
				fprintf(file, "      \"frame_count_valid\": %s,\n", M_Demos_JSONBool(entry->frame_count_valid)) < 0 ||
				fprintf(file, "      \"frame_count\": %d,\n", entry->frame_count_valid ? entry->frame_count : -1) < 0 ||
				fprintf(file, "      \"frame_exact\": %s\n", M_Demos_JSONBool(entry->frame_count_valid && entry->frame_exact)) < 0 ||
				fprintf(file, "    }") < 0)
				ok = false;
		}

		free(escaped_key);
		free(escaped_map);
	}

	if (ok)
	{
		if (!first && fprintf(file, "\n") < 0)
			ok = false;
		if (fprintf(file, "  ]\n}\n") < 0)
			ok = false;
	}

	if (fclose(file) != 0)
		ok = false;

	if (!ok)
	{
		remove(tmp_path);
		return;
	}

#ifdef _WIN32
	remove(path);
#endif
	if (rename(tmp_path, path) != 0)
	{
		remove(tmp_path);
		return;
	}

	demo_metadata_cache_dirty = false;
	demo_metadata_cache_dirty_count = 0;
}

static void M_Demos_MetadataCache_MaybeFlush(void)
{
	if (demo_metadata_cache_dirty_count >= DEMOS_METADATA_CACHE_FLUSH_DIRTY)
		M_Demos_MetadataCache_SaveIfDirty();
}

static demo_metadata_cache_entry_t *M_Demos_MetadataCacheFindValid(const char *key,
	time_t mtime, size_t fsize)
{
	demo_metadata_cache_entry_t *entry = M_Demos_MetadataCacheFind(key);

	if (!M_Demos_MetadataCacheEntryMatches(entry, mtime, fsize))
		return NULL;

	return entry;
}

static qboolean M_Demos_MetadataCacheLookupFrames(const char *key, time_t mtime,
	size_t fsize, int min_frames, int *frames_out)
{
	demo_metadata_cache_entry_t *entry = M_Demos_MetadataCacheFindValid(key, mtime, fsize);

	if (!entry || !entry->frame_count_valid)
		return false;

	if (!entry->frame_exact &&
		(min_frames <= 0 || entry->frame_count < min_frames))
		return false;

	*frames_out = entry->frame_count;
	return true;
}

static void M_Demos_MetadataCacheUpdateFrame(const char *key, time_t mtime, size_t fsize,
	int frames, qboolean exact)
{
	demo_metadata_cache_entry_t *entry;
	qboolean identity_changed;

	if (!key || !key[0])
		return;

	entry = M_Demos_MetadataCacheUpsert(key);
	if (!entry)
		return;

	identity_changed = !M_Demos_MetadataCacheEntryMatches(entry, mtime, fsize);
	if (identity_changed)
	{
		entry->parse_known = false;
		entry->parse_ok = false;
		memset(&entry->info, 0, sizeof(entry->info));
		entry->info.skill = -1;
		entry->info.frame_count = 0;
	}

	entry->mtime = mtime;
	entry->fsize = fsize;
	entry->frame_count_valid = true;
	entry->frame_count = frames;
	entry->frame_exact = exact;
	M_Demos_MetadataCacheMarkDirty();
	M_Demos_MetadataCache_MaybeFlush();
}

static void M_Demos_MetadataCacheUpdateParse(const demoitem_t *di, qboolean parsed,
	const demoinfo_t *info)
{
	demo_metadata_cache_entry_t *entry;
	qboolean identity_changed;

	if (!di || !di->cache_key[0])
		return;
	if (!parsed || !info)
		return;

	entry = M_Demos_MetadataCacheUpsert(di->cache_key);
	if (!entry)
		return;

	identity_changed = !M_Demos_MetadataCacheEntryMatches(entry, di->mtime, di->fsize);
	if (identity_changed)
	{
		entry->frame_count_valid = false;
		entry->frame_count = -1;
		entry->frame_exact = false;
	}

	entry->mtime = di->mtime;
	entry->fsize = di->fsize;
	entry->parse_known = true;
	entry->parse_ok = true;
	entry->info = *info;
	if (info->frame_count >= 0)
	{
		entry->frame_count_valid = true;
		entry->frame_count = info->frame_count;
		entry->frame_exact = true;
	}

	M_Demos_MetadataCacheMarkDirty();
	M_Demos_MetadataCache_MaybeFlush();
}

static const char *M_Demos_SkipExplicitRootPrefix(const char *name)
{
	if (name && name[0] == '.' && (name[1] == '/' || name[1] == '\\'))
		return name + 2;
	return name;
}

static qboolean M_Demos_ListedFileIsRegular(const char *fname, searchpath_t *spath)
{
	char path[MAX_OSPATH];

	if (!spath || spath->pack)
		return true;

	q_snprintf(path, sizeof(path), "%s/%s", spath->filename,
		M_Demos_SkipExplicitRootPrefix(fname));
	return (Sys_FileType(path) & FS_ENT_FILE) != 0;
}

static int M_Demos_CountFrames(const char *fname, time_t mtime, size_t fsize,
	searchpath_t *spath, int min_frames)
{
	char key[MAX_OSPATH];
	int frames;
	qboolean exact;

	if (!M_Demos_BuildCacheKey(key, sizeof(key), fname, spath))
		return -1;

	if (M_Demos_MetadataCacheLookupFrames(key, mtime, fsize, min_frames, &frames))
		return frames;

	if (spath && !spath->pack)
		frames = CL_CountDemoFramesInFileLimit(key, min_frames);
	else
	{
		byte *data;
		int length;

		data = CL_LoadDemoBuffer(fname, &length);
		if (!data)
			frames = -1;
		else
		{
			frames = CL_CountDemoFramesInBufferLimit(data, length, min_frames);
			free(data);
		}
	}

	exact = (min_frames <= 0 || frames < min_frames);
	M_Demos_MetadataCacheUpdateFrame(key, mtime, fsize, frames, exact);
	return frames;
}

static qboolean M_Demos_CheckMinFrames(demoitem_t *di)
{
	int min_frames = demosmenu.minframes_threshold;

	if (min_frames <= 0)
	{
		di->minframes_state = DEMO_MINFRAMES_PASS;
		return false;
	}

	if (di->minframes_state != DEMO_MINFRAMES_UNKNOWN)
		return di->minframes_state == DEMO_MINFRAMES_FAIL;

	di->frame_count = M_Demos_CountFrames(di->name, di->mtime, di->fsize,
		di->source_searchpath, min_frames);
	di->minframes_state = (di->frame_count >= 0 && di->frame_count < min_frames) ?
		DEMO_MINFRAMES_FAIL : DEMO_MINFRAMES_PASS;

	return di->minframes_state == DEMO_MINFRAMES_FAIL;
}

#define DEMOS_MINFRAME_FILTER_BUDGET	1
#define DEMOS_MINFRAME_REFILTER_BATCH	8

static void M_Demos_ResetMinFramesFilter(void)
{
	int i;

	demosmenu.minframes_threshold = CL_DemoMinFramesThreshold(NULL);
	demosmenu.minframes_check_cursor = 0;
	demosmenu.minframes_hidden_since_refilter = 0;
	demosmenu.bg_parse_cursor = 0;
	demosmenu.bg_parse_refilter_count = 0;

	for (i = 0; i < demosmenu.democount; i++)
	{
		demosmenu.items[i].minframes_state = demosmenu.minframes_threshold > 0 ?
			DEMO_MINFRAMES_UNKNOWN : DEMO_MINFRAMES_PASS;
		demosmenu.items[i].frame_count = -1;
	}
}

static qboolean M_Demos_SyncMinFramesThreshold(void)
{
	int current = CL_DemoMinFramesThreshold(NULL);

	if (current == demosmenu.minframes_threshold)
		return false;

	M_Demos_ResetMinFramesFilter();
	return true;
}

static qboolean M_Demos_TickMinFrameFilter(void)
{
	int processed = 0;
	qboolean changed = false;

	if (demosmenu.minframes_threshold <= 0)
		return false;

	while (processed < DEMOS_MINFRAME_FILTER_BUDGET &&
		demosmenu.minframes_check_cursor < demosmenu.democount)
	{
		demoitem_t *di = &demosmenu.items[demosmenu.minframes_check_cursor++];

		if (di->minframes_state != DEMO_MINFRAMES_UNKNOWN)
			continue;

		processed++;
		if (M_Demos_CheckMinFrames(di))
		{
			changed = true;
			demosmenu.minframes_hidden_since_refilter++;
		}
	}

	return changed;
}

static qboolean M_Demos_MinFrameFilterDone(void)
{
	return demosmenu.minframes_threshold <= 0 ||
		demosmenu.minframes_check_cursor >= demosmenu.democount;
}

static qboolean M_Demos_AddListedFile(void *ctx, const char *fname, time_t mtime, size_t fsize, searchpath_t *spath)
{
	demos_list_ctx_t *list_ctx = (demos_list_ctx_t *)ctx;
	char logical_name[MAX_QPATH];
	char display_name[MAX_QPATH];
	char date[32];
	qboolean from_id1;
	qboolean explicit_root = list_ctx && list_ctx->explicit_root &&
		!strchr(fname, '/') && !strchr(fname, '\\');

	if (!M_Demos_SearchPathAllowed(spath, &from_id1))
		return true;

	if (explicit_root)
	{
		q_snprintf(logical_name, sizeof(logical_name), "./%s", fname);
		q_snprintf(display_name, sizeof(display_name), "../%s", fname);
	}
	else
	{
		q_strlcpy(logical_name, fname, sizeof(logical_name));
		q_strlcpy(display_name, COM_SkipPath(fname), sizeof(display_name));
	}

	if (!M_Demos_ListedFileIsRegular(logical_name, spath))
		return true;

	M_Demos_FormatFileDate(mtime, date, sizeof(date));
	M_Demos_AddEx(logical_name, date, display_name, from_id1, mtime, fsize, spath);
	return true;
}

static void M_Demos_CopyLookupSuffix(const char *src, char *out, size_t outlen)
{
	size_t len;

	q_strlcpy(out, src, outlen);
	len = strlen(out);
	while (len > 0 && out[len - 1] == '/')
		out[--len] = '\0';
}

static qboolean M_Demos_FindFolder(const char *suffix, char *actual, size_t actual_size)
{
	filelist_item_t *folder;
	char lookup[MAX_QPATH];

	M_Demos_CopyLookupSuffix(suffix, lookup, sizeof(lookup));
	if (!lookup[0])
	{
		if (actual_size)
			actual[0] = '\0';
		return true;
	}

	for (folder = demosmenu.path_folders; folder; folder = folder->next)
	{
		if (!q_strcasecmp(folder->name, lookup))
		{
			q_strlcpy(actual, folder->name, actual_size);
			return true;
		}
	}

	return false;
}

static void M_Demos_CleanPathSuffix(void)
{
	char src[MAX_QPATH];
	char clean[MAX_QPATH];
	const char *p;
	char *w;
	size_t out = 0;
	qboolean last_slash = false;

	q_strlcpy(src, demosmenu.path_suffix, sizeof(src));
	for (w = src; *w; ++w)
	{
		if (*w == '\\')
			*w = '/';
	}

	p = M_Demos_SkipPathBasePrefix(src);

	clean[0] = '\0';
	while (*p && out < sizeof(clean) - 1)
	{
		char c = *p++;

		if (c == ':')
			continue;
		if (c == '/')
		{
			if (out == 0 || last_slash)
				continue;
			clean[out++] = '/';
			last_slash = true;
			continue;
		}

		clean[out++] = c;
		last_slash = false;
	}
	clean[out] = '\0';

	if (strstr(clean, "../") || strstr(clean, "/..") || !strcmp(clean, "..") ||
		strstr(clean, "./") || strstr(clean, "/.") || !strcmp(clean, "."))
	{
		char safe[MAX_QPATH];
		size_t i = 0, j = 0;

		while (clean[i] && j < sizeof(safe) - 1)
		{
			size_t start = i;
			size_t len;

			while (clean[i] && clean[i] != '/')
				++i;
			len = i - start;

			if (!(len == 1 && clean[start] == '.') &&
				!(len == 2 && clean[start] == '.' && clean[start + 1] == '.'))
			{
				if (j > 0 && safe[j - 1] != '/' && j < sizeof(safe) - 1)
					safe[j++] = '/';
				while (len-- && j < sizeof(safe) - 1)
					safe[j++] = clean[start++];
			}

			while (clean[i] == '/')
				++i;
		}

		if (last_slash && j > 0 && safe[j - 1] != '/' && j < sizeof(safe) - 1)
			safe[j++] = '/';
		safe[j] = '\0';
		q_strlcpy(clean, safe, sizeof(clean));
	}

	if (strcmp(demosmenu.path_suffix, clean))
	{
		q_strlcpy(demosmenu.path_suffix, clean, sizeof(demosmenu.path_suffix));
		demosmenu.path_field.cursor = (int)strlen(demosmenu.path_suffix);
		demosmenu.path_field.sel_start = -1;
		M_TextField_ClampCursor(&demosmenu.path_field);
	}
}

static void M_Demos_UpdatePathHint(void)
{
	filelist_item_t *folder;
	int len = (int)strlen(demosmenu.path_suffix);

	demosmenu.path_hint[0] = '\0';

	if (!demosmenu.path_editing || len <= 0 ||
		demosmenu.path_field.cursor != len)
		return;

	for (folder = demosmenu.path_folders; folder; folder = folder->next)
	{
		if (q_strncasecmp(folder->name, demosmenu.path_suffix, len))
			continue;
		if ((int)strlen(folder->name) <= len)
			continue;

		q_strlcpy(demosmenu.path_hint, folder->name + len, sizeof(demosmenu.path_hint));
		return;
	}
}

extern int unfun_match(const char *s1, char *s2);  // host_cmd.c — Quake-special-aware substring match (used by name/identify/tell)
extern filelist_item_t *FindLevelInList(filelist_item_t *list, const char *name);  // host_cmd.c #mapdescriptions

static void M_Demos_ApplyInfoToItem(demoitem_t *di, const demoinfo_t *info)
{
	q_strlcpy(di->map, info->map, sizeof(di->map));
	q_strlcpy(di->players, info->players, sizeof(di->players));
	FormatDuration(info->duration, di->duration, sizeof(di->duration));
	q_snprintf(di->filesize, sizeof(di->filesize), "%.1f mb", info->filesize_mb);

	di->stats[0] = '\0';
	if (info->singleplayer)
	{
		static const char *skill_names[4] = { "Easy", "Normal", "Hard", "Nightmare" };
		char buf[64];
		buf[0] = '\0';
		if (info->total_kills > 0 || info->kills > 0)
			q_snprintf(buf, sizeof(buf), "Kills: %d/%d", info->kills, info->total_kills);
		if (info->total_secrets > 0 || info->secrets > 0)
		{
			char sec[32];
			q_snprintf(sec, sizeof(sec), "Secrets: %d/%d", info->secrets, info->total_secrets);
			if (buf[0]) q_strlcat(buf, "  ", sizeof(buf));
			q_strlcat(buf, sec, sizeof(buf));
		}
		if (info->skill >= 0)
		{
			char skl[24];
			int s = info->skill < 4 ? info->skill : 3;
			q_snprintf(skl, sizeof(skl), "Skill: %s", skill_names[s]);
			if (buf[0]) q_strlcat(buf, "  ", sizeof(buf));
			q_strlcat(buf, skl, sizeof(buf));
		}
		q_strlcpy(di->stats, buf[0] ? buf : "single player", sizeof(di->stats));
	}
}

static void M_Demos_ApplyParseFailureToItem(demoitem_t *di)
{
	q_strlcpy(di->map, "unknown", sizeof(di->map));
	q_strlcpy(di->players, "n/a", sizeof(di->players));
	q_strlcpy(di->duration, "n/a", sizeof(di->duration));
	q_strlcpy(di->filesize, "n/a", sizeof(di->filesize));
	di->stats[0] = '\0';
}

static void M_Demos_ApplyCachedFrameCount(demoitem_t *di, const demo_metadata_cache_entry_t *entry)
{
	int min_frames = demosmenu.minframes_threshold;

	if (!entry || !entry->frame_count_valid)
		return;

	if (!entry->frame_exact && (min_frames <= 0 || entry->frame_count < min_frames))
		return;

	di->frame_count = entry->frame_count;
	if (min_frames <= 0)
		di->minframes_state = DEMO_MINFRAMES_PASS;
	else if (entry->frame_count >= 0 && entry->frame_count < min_frames)
		di->minframes_state = DEMO_MINFRAMES_FAIL;
	else
		di->minframes_state = DEMO_MINFRAMES_PASS;
}

static qboolean M_Demos_MetadataCache_ApplyToItem(demoitem_t *di)
{
	demo_metadata_cache_entry_t *entry;

	if (!di || !di->cache_key[0])
		return false;

	entry = M_Demos_MetadataCacheFindValid(di->cache_key, di->mtime, di->fsize);
	if (!entry)
		return false;

	M_Demos_ApplyCachedFrameCount(di, entry);

	if (!entry->parse_known)
		return false;

	if (entry->parse_ok)
		M_Demos_ApplyInfoToItem(di, &entry->info);
	else
		M_Demos_ApplyParseFailureToItem(di);

	di->parsed = true;
	return true;
}

static void M_Demos_ParseItem(demoitem_t *di)
{
	demoinfo_t info;
	qboolean parsed = Parse_DemoInfo(di->name, di->source_searchpath, &info);

	if (parsed)
		M_Demos_ApplyInfoToItem(di, &info);
	else
		M_Demos_ApplyParseFailureToItem(di);

	di->parsed = true;
	M_Demos_MetadataCacheUpdateParse(di, parsed, parsed ? &info : NULL);
}

// Parse a small number of demos per frame so typed searches don't block.
// Returns true if anything was parsed this call (so caller can refresh
// dependent state like the filtered list).
typedef enum
{
	DEMO_BACKGROUND_PARSE_NONE,
	DEMO_BACKGROUND_PARSE_CHECKED,
	DEMO_BACKGROUND_PARSE_PARSED
} demo_background_parse_result_t;

static demo_background_parse_result_t M_Demos_TryBackgroundParseDemo(int demo_idx,
	qboolean allow_minframes_check)
{
	demoitem_t *di;

	if (demo_idx < 0 || demo_idx >= demosmenu.democount)
		return DEMO_BACKGROUND_PARSE_NONE;

	di = &demosmenu.items[demo_idx];
	if (di->parsed || di->minframes_state == DEMO_MINFRAMES_FAIL)
		return DEMO_BACKGROUND_PARSE_NONE;

	if (demosmenu.minframes_threshold > 0 &&
		di->minframes_state == DEMO_MINFRAMES_UNKNOWN)
	{
		if (!allow_minframes_check)
			return DEMO_BACKGROUND_PARSE_NONE;

		if (M_Demos_CheckMinFrames(di))
		{
			demosmenu.minframes_hidden_since_refilter++;
			return DEMO_BACKGROUND_PARSE_CHECKED;
		}

		return DEMO_BACKGROUND_PARSE_CHECKED;
	}

	if (di->minframes_state == DEMO_MINFRAMES_FAIL || di->parsed)
		return DEMO_BACKGROUND_PARSE_NONE;

	M_Demos_ParseItem(di);
	return DEMO_BACKGROUND_PARSE_PARSED;
}

static demo_background_parse_result_t M_Demos_TryBackgroundParseDisplay(int display_idx)
{
	if (display_idx < 0 || display_idx >= demosmenu.list.numitems ||
		!demosmenu.filtered_indices)
		return DEMO_BACKGROUND_PARSE_NONE;

	return M_Demos_TryBackgroundParseDemo(demosmenu.filtered_indices[display_idx], true);
}

static demo_background_parse_result_t M_Demos_TickPriorityBackgroundParse(void)
{
	demo_background_parse_result_t result;
	int firstvis, numvis;
	int i;

	if (demosmenu.list.numitems <= 0)
		return DEMO_BACKGROUND_PARSE_NONE;

	M_List_GetVisibleRange(&demosmenu.list, &firstvis, &numvis);

	if (M_List_IsItemVisible(&demosmenu.list, demosmenu.list.cursor))
	{
		result = M_Demos_TryBackgroundParseDisplay(demosmenu.list.cursor);
		if (result != DEMO_BACKGROUND_PARSE_NONE)
			return result;
	}

	for (i = 0; i < numvis; i++)
	{
		int display_idx = firstvis + i;

		if (display_idx == demosmenu.list.cursor)
			continue;

		result = M_Demos_TryBackgroundParseDisplay(display_idx);
		if (result != DEMO_BACKGROUND_PARSE_NONE)
			return result;
	}

	return M_Demos_TryBackgroundParseDisplay(demosmenu.list.cursor);
}

static demo_background_parse_result_t M_Demos_TickSequentialBackgroundParse(void)
{
	while (demosmenu.bg_parse_cursor < demosmenu.democount)
	{
		demoitem_t *di = &demosmenu.items[demosmenu.bg_parse_cursor];
		demo_background_parse_result_t result;
		int demo_idx;

		if (demosmenu.minframes_threshold > 0 &&
			di->minframes_state == DEMO_MINFRAMES_UNKNOWN)
			break;

		demo_idx = demosmenu.bg_parse_cursor++;
		result = M_Demos_TryBackgroundParseDemo(demo_idx, false);
		if (result != DEMO_BACKGROUND_PARSE_NONE)
			return result;
	}

	return DEMO_BACKGROUND_PARSE_NONE;
}

static qboolean M_Demos_TickBackgroundParse(void)
{
	const int BUDGET = 1;  // demos per frame; each Parse_DemoInfo can be I/O heavy
	int parsed = 0;

	while (parsed < BUDGET)
	{
		demo_background_parse_result_t result;

		result = M_Demos_TickPriorityBackgroundParse();
		if (result == DEMO_BACKGROUND_PARSE_NONE)
			result = M_Demos_TickSequentialBackgroundParse();

		if (result == DEMO_BACKGROUND_PARSE_NONE)
			break;

		if (result == DEMO_BACKGROUND_PARSE_CHECKED)
			break;

		parsed++;
		demosmenu.bg_parse_refilter_count++;
	}

	if (parsed > 0)
	{
		if (demosmenu.bg_parse_cursor >= demosmenu.democount)
			M_Demos_MetadataCache_SaveIfDirty();
		else
			M_Demos_MetadataCache_MaybeFlush();
	}

	return parsed > 0;
}

#define DEMOS_SEARCH_MAX_TERMS	8
#define DEMOS_SEARCH_TERM_SIZE	32

typedef struct
{
	char text[DEMOS_SEARCH_TERM_SIZE];
} demosearchterm_t;

static int M_Demos_ParseSearchTerms(const char *search, demosearchterm_t *terms, int max_terms)
{
	int count = 0;
	const char *p = search;

	while (*p && count < max_terms)
	{
		char *out;
		size_t outlen = 0;
		size_t outsize;
		qboolean quoted = false;

		while (q_isspace((unsigned char)*p))
			++p;
		if (!*p)
			break;

		if (*p == '"')
		{
			quoted = true;
			++p;
		}

		out = terms[count].text;
		outsize = sizeof(terms[count].text);
		while (*p && ((!quoted && !q_isspace((unsigned char)*p)) || (quoted && *p != '"')))
		{
			if (outlen < outsize - 1)
				out[outlen++] = *p;
			++p;
		}
		out[outlen] = '\0';

		if (quoted && *p == '"')
			++p;

		if (outlen > 0)
			++count;
	}

	return count;
}

static qboolean M_Demos_FieldMatchesTerm(const char *field, const char *term)
{
	return field && field[0] && q_strcasestr(field, term);
}

static qboolean M_Demos_ItemMatchesTerm(demoitem_t *di, const char *term, const char *map_desc)
{
	if (M_Demos_FieldMatchesTerm(di->name, term) ||
		M_Demos_FieldMatchesTerm(di->display, term) ||
		M_Demos_FieldMatchesTerm(di->date, term) ||
		M_Demos_FieldMatchesTerm(di->map, term) ||
		M_Demos_FieldMatchesTerm(map_desc, term))
		return true;

	if (di->parsed &&
		((di->players[0] && unfun_match(term, di->players)) ||
		 M_Demos_FieldMatchesTerm(di->stats, term) ||
		 M_Demos_FieldMatchesTerm(di->duration, term) ||
		 M_Demos_FieldMatchesTerm(di->filesize, term)))
		return true;

	return false;
}

static qboolean M_Demos_ItemMatchesSearch(demoitem_t *di, const demosearchterm_t *terms,
	int term_count, const char *map_desc)
{
	int i;

	for (i = 0; i < term_count; ++i)
	{
		if (!M_Demos_ItemMatchesTerm(di, terms[i].text, map_desc))
			return false;
	}

	return true;
}

// preserve_view=true: keep the user's scroll position when possible.  Used by
// the background-parse tick so newly-matched items can stream into the list
// without scroll-jumping under the user.  Keystroke callers pass false and
// get the legacy center-on-cursor behavior.
static void M_Demos_RefilterEx(qboolean preserve_view)
{
    int i;
    qboolean has_search = demosmenu.list.search.len > 0;
    demosearchterm_t terms[DEMOS_SEARCH_MAX_TERMS];
    int term_count = has_search ? M_Demos_ParseSearchTerms(demosmenu.list.search.text,
		terms, (int)Q_COUNTOF(terms)) : 0;
    int prev_demo_idx = -1;
    int prev_scroll = demosmenu.list.scroll;

    // Remember which demo the cursor was on so we can keep it selected even
    // when the filter set grows or items shift index.
    if (demosmenu.list.cursor >= 0 &&
        demosmenu.list.cursor < (int)VEC_SIZE(demosmenu.filtered_indices))
        prev_demo_idx = demosmenu.filtered_indices[demosmenu.list.cursor];

    VEC_CLEAR(demosmenu.filtered_indices);

    for (i = 0; i < demosmenu.democount; i++)
    {
        demoitem_t *di = &demosmenu.items[i];

		if (di->minframes_state == DEMO_MINFRAMES_FAIL)
			continue;

        // Tooltip metadata (players/duration/filesize) is filled in by the
        // background parser in M_Demos_Draw.  Map starts with a filename
        // inference so common searches can match immediately, then the parser
        // overwrites it with authoritative demo metadata when available.
        //
        // For the players field we use unfun_match so typed ASCII matches
        // names that contain Quake's gold/colored chars (same matcher used
        // by name/identify/tell tab-completion).  Other fields stay on
        // q_strcasestr because the unfun table folds ASCII digits 0-9 into
        // letters (intended for gold-digit player names), which would
        // garble searches against map ("e1m1") or duration ("1:23").
        // Also match against the worldspawn description from mapdesc.json
        // (e.g. typing "necropolis" finds demos on e1m3).  Descriptions live
        // on extralevels[].data once ExtraMaps_ParseDescriptions has run;
        // M_Demos_Init kicks that off so this lookup is usually populated.
        const char *map_desc = NULL;
        if (term_count > 0 && di->map[0] && descriptionsParsed)
        {
            filelist_item_t *level = FindLevelInList(extralevels, di->map);
            if (level && level->data[0])
                map_desc = level->data;
        }

        if (term_count <= 0 || M_Demos_ItemMatchesSearch(di, terms, term_count, map_desc))
        {
            VEC_PUSH(demosmenu.filtered_indices, i);
        }
    }

    demosmenu.bg_parse_refilter_count = 0;
    demosmenu.list.numitems = VEC_SIZE(demosmenu.filtered_indices);

    // Try to relocate the previously-selected demo in the new filter set so
    // identity (not position) is preserved across the refilter.
    int new_cursor = -1;
    if (prev_demo_idx >= 0)
    {
        for (i = 0; i < demosmenu.list.numitems; i++)
        {
            if (demosmenu.filtered_indices[i] == prev_demo_idx)
            {
                new_cursor = i;
                break;
            }
        }
    }

    if (new_cursor >= 0)
    {
        demosmenu.list.cursor = new_cursor;

        if (preserve_view)
        {
            int max_scroll = demosmenu.list.numitems - demosmenu.list.viewsize;
            if (max_scroll < 0)
                max_scroll = 0;
            demosmenu.list.scroll = CLAMP(0, prev_scroll, max_scroll);
            // Only re-center if the preserved cursor scrolled out of view.
            if (new_cursor < demosmenu.list.scroll ||
                new_cursor >= demosmenu.list.scroll + demosmenu.list.viewsize)
                M_List_CenterCursor(&demosmenu.list);
        }
        else
        {
            M_List_CenterCursor(&demosmenu.list);
        }
        return;
    }

    // Previous selection no longer matches — fall back to clamping the
    // existing cursor index and re-centering.
    if (demosmenu.list.cursor >= demosmenu.list.numitems)
        demosmenu.list.cursor = demosmenu.list.numitems - 1;
    if (demosmenu.list.cursor < 0 && demosmenu.list.numitems > 0)
        demosmenu.list.cursor = 0;

    M_List_CenterCursor(&demosmenu.list);
}

static void M_Demos_Refilter(void)
{
    M_Demos_RefilterEx(false);
}

static const char *M_Demos_CommandName(const char *name);

static qboolean M_Demos_AllowPlayByMinFrames(demoitem_t *di)
{
	qboolean below_minframes;

	M_Demos_SyncMinFramesThreshold();

	if (demosmenu.minframes_threshold <= 0)
		return true;

	if (di->minframes_state == DEMO_MINFRAMES_UNKNOWN)
	{
		qboolean blocked_sound = M_Demos_BlockSoundForIO();

		below_minframes = M_Demos_CheckMinFrames(di);
		M_Demos_UnblockSoundForIO(blocked_sound);
	}
	else
		below_minframes = M_Demos_CheckMinFrames(di);

	if (!below_minframes)
		return true;

	S_LocalSound("misc/menu3.wav");
	Con_Printf("demo ^m%s^m is below cl_demo_minframes (%d/%d frames)\n",
		M_Demos_CommandName(di->name), di->frame_count,
		demosmenu.minframes_threshold);
	M_Demos_RefilterEx(true);
	demosmenu.minframes_hidden_since_refilter = 0;
	return false;
}

static void M_Demos_RebuildForCurrentPath(void)
{
	demos_list_ctx_t root_ctx = { true };
	char actual_folder[MAX_QPATH];
	qboolean blocked_sound;

	blocked_sound = M_Demos_BlockSoundForIO();
	M_Demos_FreeItems();
	demosmenu.list.cursor = -1;
	demosmenu.list.scroll = 0;
	demosmenu.democount = 0;
	demosmenu.bg_parse_cursor = 0;
	demosmenu.bg_parse_refilter_count = 0;
	VEC_CLEAR(demosmenu.items);
	VEC_CLEAR(demosmenu.filtered_indices);
	M_Demos_ResetMinFramesFilter();

	demosmenu.path_valid = M_Demos_FindFolder(demosmenu.path_suffix,
		actual_folder, sizeof(actual_folder));

	if (demosmenu.path_valid)
	{
		if (!actual_folder[0])
		{
			COM_ListAllFiles(&root_ctx, "*.dem", M_Demos_AddListedFile, 0, NULL);
			COM_ListAllFiles(&root_ctx, "*.dz", M_Demos_AddListedFile, 0, NULL);
			COM_ListAllFiles(NULL, "demos/*.dem", M_Demos_AddListedFile, 0, NULL);
			COM_ListAllFiles(NULL, "demos/*.dz", M_Demos_AddListedFile, 0, NULL);
		}
		else
		{
			char pattern[MAX_OSPATH];

			q_snprintf(pattern, sizeof(pattern), "demos/%s/*.dem", actual_folder);
			COM_ListAllFiles(NULL, pattern, M_Demos_AddListedFile, 0, NULL);

			q_snprintf(pattern, sizeof(pattern), "demos/%s/*.dz", actual_folder);
			COM_ListAllFiles(NULL, pattern, M_Demos_AddListedFile, 0, NULL);
		}
	}

	M_Demos_Refilter();

	if (demosmenu.list.cursor == -1 && demosmenu.list.numitems > 0)
		demosmenu.list.cursor = 0;

	M_List_CenterCursor(&demosmenu.list);
	M_Demos_UpdatePathHint();
	M_Demos_UnblockSoundForIO(blocked_sound);
}

static void M_Demos_ClearRememberedPath(void)
{
	demosmenu.remembered_path_suffix[0] = '\0';
}

static void M_Demos_PathChanged(void)
{
	M_Demos_CleanPathSuffix();
	if (!demosmenu.path_suffix[0])
		M_Demos_ClearRememberedPath();
	demosmenu.path_tabpartial[0] = '\0';
	M_Demos_RebuildForCurrentPath();
}

static void M_Demos_ToggleShowId1(void)
{
	if (!M_Demos_ShowId1Toggle())
		return;

	demosmenu.show_id1 = !demosmenu.show_id1;
	demosmenu.path_tabpartial[0] = '\0';
	M_Demos_RebuildFolderList();
	M_Demos_RebuildForCurrentPath();
	S_LocalSound("misc/menu1.wav");
}

static void M_Demos_RememberCurrentPath(void)
{
	q_strlcpy(demosmenu.remembered_path_suffix, demosmenu.path_suffix,
		sizeof(demosmenu.remembered_path_suffix));
}

static void M_Demos_ResetPathToRoot(void)
{
	demosmenu.path_suffix[0] = '\0';
	demosmenu.path_field.cursor = 0;
	demosmenu.path_field.sel_start = -1;
	demosmenu.path_tabpartial[0] = '\0';
	demosmenu.path_hint[0] = '\0';
	M_TextField_ClampCursor(&demosmenu.path_field);
}

static void M_Demos_Init(void)
{
	qboolean blocked_sound = M_Demos_BlockSoundForIO();

	M_Demos_MetadataCache_Load();

	demosmenu.list.viewsize = MAX_VIS_DEMOS;
	demosmenu.list.cursor = -1;
	demosmenu.list.scroll = 0;
	demosmenu.democount = 0;
	demosmenu.scrollbar_grab = false;
	VEC_CLEAR(demosmenu.items);
	VEC_CLEAR(demosmenu.filtered_indices);
	demosmenu.path_editing = false;
	demosmenu.path_valid = true;
	q_strlcpy(demosmenu.path_suffix, demosmenu.remembered_path_suffix,
		sizeof(demosmenu.path_suffix));
	demosmenu.path_hint[0] = '\0';
	demosmenu.path_tabpartial[0] = '\0';
	M_TextField_Init(&demosmenu.path_field, demosmenu.path_suffix, sizeof(demosmenu.path_suffix) - 1, false);

	memset(&demosmenu.list.search, 0, sizeof(demosmenu.list.search));
	demosmenu.list.search.maxlen = 32;

	M_Ticker_Init (&demosmenu.ticker);

	// Populate extralevels[].data so the search filter can match demos by
	// their map's worldspawn description (e.g. typing "necropolis" → e1m3).
	if (!descriptionsParsed)
		ExtraMaps_ParseDescriptions();

	M_Demos_RebuildFolderList();
	M_Demos_RebuildForCurrentPath();
	if (demosmenu.remembered_path_suffix[0] && !demosmenu.path_valid)
	{
		M_Demos_ClearRememberedPath();
		M_Demos_ResetPathToRoot();
		M_Demos_RebuildForCurrentPath();
	}
	M_Demos_UnblockSoundForIO(blocked_sound);
}

void M_Menu_Demos_f (void)
{
	key_dest = key_menu;
	demosmenu.prev = m_state;
	m_state = m_demos;
	m_entersound = true;
	M_Demos_Init();
}

static int M_Demos_PathSuffixVisibleChars(void)
{
	return q_max(0, DEMOS_PATH_BOX_CHARS - (int)strlen(M_Demos_PathBase()) - 1);
}

static int M_Demos_PathViewStart(void)
{
	int suffix_chars = M_Demos_PathSuffixVisibleChars();
	int len = (int)strlen(demosmenu.path_suffix);

	if (suffix_chars <= 0 || len <= suffix_chars)
		return 0;

	return CLAMP(0, demosmenu.path_field.cursor - suffix_chars, len - suffix_chars);
}

static qboolean M_Demos_ShowPathOptions(void)
{
	return demosmenu.path_editing &&
		(!demosmenu.path_suffix[0] || !demosmenu.path_valid);
}

static int M_Demos_ListY(void)
{
	return M_Demos_ShowId1Toggle() ? 64 : 56;
}

static qboolean M_Demos_MouseInShowId1Toggle(void)
{
	return M_Demos_ShowId1Toggle() &&
		M_TextField_MouseInRow(m_mousey, DEMOS_ID1_ROW_Y) &&
		m_mousex >= DEMOS_ID1_TEXT_X &&
		m_mousex < DEMOS_ID1_TEXT_X + (int)(22 * 8 * DEMOS_ID1_TEXT_SCALE);
}

static qboolean M_Demos_MouseInPathOptionsArea(void)
{
	int width = (demosmenu.cols - 2) * 8;
	int height = demosmenu.list.viewsize * 8;
	int left = demosmenu.x - 8;

	return M_Demos_ShowPathOptions() &&
		m_mousex >= left &&
		m_mousex < demosmenu.x + width &&
		m_mousey >= demosmenu.y &&
		m_mousey < demosmenu.y + height;
}

static filelist_item_t *M_Demos_GetPathOptionAtRow(int row)
{
	filelist_item_t *folder;
	const char *partial = demosmenu.path_tabpartial[0] ?
		demosmenu.path_tabpartial : demosmenu.path_suffix;
	int partial_len = (int)strlen(partial);
	int shown = 0;

	if (row < 0 || row >= demosmenu.list.viewsize)
		return NULL;

	for (folder = demosmenu.path_folders; folder; folder = folder->next)
	{
		if (partial_len && q_strncasecmp(folder->name, partial, partial_len))
			continue;

		if (shown == row)
			return folder;

		++shown;
		if (shown >= demosmenu.list.viewsize)
			break;
	}

	return NULL;
}

static filelist_item_t *M_Demos_GetHoveredPathOption(void)
{
	int row;

	if (!M_Demos_MouseInPathOptionsArea())
		return NULL;

	row = (m_mousey - demosmenu.y) / 8;
	return M_Demos_GetPathOptionAtRow(row);
}

static void M_Demos_EndPathEdit(void);

static void M_Demos_SelectPathOption(filelist_item_t *folder)
{
	if (!folder)
		return;

	q_strlcpy(demosmenu.path_suffix, folder->name, sizeof(demosmenu.path_suffix));
	demosmenu.path_field.cursor = (int)strlen(demosmenu.path_suffix);
	demosmenu.path_field.sel_start = -1;
	demosmenu.path_tabpartial[0] = '\0';
	M_Demos_EndPathEdit();
	M_Demos_RebuildForCurrentPath();
}

static const char *M_Demos_CommandName(const char *name)
{
	if (!q_strncasecmp(name, "demos/", 6) || !q_strncasecmp(name, "demos\\", 6))
		return name + 6;
	return name;
}

static qboolean M_Demos_SameDemoName(const char *a, const char *b)
{
	return !q_strcasecmp(M_Demos_CommandName(a), M_Demos_CommandName(b));
}

static qboolean M_Demos_QueuePlayDemo(const char *name)
{
	const char *demo_name = name;

	if (strchr(demo_name, '"') || strchr(demo_name, '\n') || strchr(demo_name, '\r'))
	{
		Con_Printf("cannot play demo with unsupported characters in path: %s\n", demo_name);
		S_LocalSound("misc/menu3.wav");
		return false;
	}

	Cbuf_AddText(va("playdemo \"%s\"\n", demo_name));
	return true;
}

static void M_Demos_DrawPathField(void)
{
	int row_y = DEMOS_PATH_ROW_Y;
	const char *base = M_Demos_PathBase();
	int base_len = (int)strlen(base);
	int slash_x = DEMOS_PATH_TEXT_X + base_len * 8;
	int suffix_x = slash_x + 8;
	int suffix_chars = M_Demos_PathSuffixVisibleChars();
	int view_start = M_Demos_PathViewStart();
	qboolean show_separator = demosmenu.path_editing || demosmenu.path_suffix[0];
	char visible_suffix[MAX_QPATH];

	M_Print(DEMOS_PATH_LABEL_X, row_y, "path:");
	M_DrawTextBox(DEMOS_PATH_BOX_X, row_y - 8, DEMOS_PATH_BOX_CHARS, 1);
	M_Print(DEMOS_PATH_TEXT_X, row_y, base);

	if (show_separator)
		M_Print(slash_x, row_y, "/");

	visible_suffix[0] = '\0';
	if (show_separator && suffix_chars > 0)
	{
		q_strlcpy(visible_suffix, demosmenu.path_suffix + view_start, sizeof(visible_suffix));
		visible_suffix[suffix_chars] = '\0';

		if (demosmenu.path_editing)
		{
			menu_textfield_t visible_field = demosmenu.path_field;
			visible_field.text = demosmenu.path_suffix + view_start;
			visible_field.cursor = CLAMP(0, demosmenu.path_field.cursor - view_start, suffix_chars);
			visible_field.max_len = suffix_chars;
			if (demosmenu.path_field.sel_start >= 0)
				visible_field.sel_start = CLAMP(0, demosmenu.path_field.sel_start - view_start, suffix_chars);
			M_TextField_DrawHighlight(&visible_field, suffix_x, row_y);
		}

		M_Print(suffix_x, row_y, visible_suffix);
	}

	if (demosmenu.path_editing &&
		demosmenu.path_hint[0] &&
		demosmenu.path_field.cursor == (int)strlen(demosmenu.path_suffix))
	{
		int hint_col = (int)strlen(demosmenu.path_suffix) - view_start;
		if (hint_col >= 0 && hint_col < suffix_chars)
		{
			int hint_x = suffix_x + hint_col * 8;
			M_PrintRGBA(hint_x, row_y, demosmenu.path_hint,
				CL_PLColours_Parse("0xffffff"), 0.5f, true);
		}
	}

	if (demosmenu.path_editing)
	{
		menu_textfield_t visible_field = demosmenu.path_field;
		visible_field.text = demosmenu.path_suffix + view_start;
		visible_field.cursor = CLAMP(0, demosmenu.path_field.cursor - view_start, suffix_chars);
		visible_field.max_len = suffix_chars;
		visible_field.sel_start = -1;
		M_TextField_DrawCursor(&visible_field, suffix_x, row_y);
	}
}

static void M_Demos_DrawShowId1Toggle(void)
{
	const int value_x = DEMOS_ID1_TEXT_X + (int)(16 * 8 * DEMOS_ID1_TEXT_SCALE);

	if (!M_Demos_ShowId1Toggle())
		return;

	glPushMatrix();
	glTranslatef(DEMOS_ID1_TEXT_X, DEMOS_ID1_ROW_Y + 1, 0);
	glScalef(DEMOS_ID1_TEXT_SCALE, DEMOS_ID1_TEXT_SCALE, 1.0f);
	M_Print(0, 0, "show id1 demos:");
	glPopMatrix();

	glPushMatrix();
	glTranslatef(value_x, DEMOS_ID1_ROW_Y + 1, 0);
	glScalef(DEMOS_ID1_TEXT_SCALE, DEMOS_ID1_TEXT_SCALE, 1.0f);
	M_PrintWhite(0, 0, demosmenu.show_id1 ? "on" : "off");
	glPopMatrix();
}

static void M_Demos_DrawPathOptions(int x, int y, int cols)
{
	filelist_item_t *folder;
	filelist_item_t *hovered = M_Demos_GetHoveredPathOption();
	const char *partial = demosmenu.path_tabpartial[0] ?
		demosmenu.path_tabpartial : demosmenu.path_suffix;
	int partial_len = (int)strlen(partial);
	int shown = 0;
	int matches = 0;

	for (folder = demosmenu.path_folders; folder; folder = folder->next)
	{
		char label[MAX_QPATH + 2];
		int len;

		if (partial_len && q_strncasecmp(folder->name, partial, partial_len))
			continue;

		++matches;
		if (shown >= demosmenu.list.viewsize)
			continue;

		q_snprintf(label, sizeof(label), "%s/", folder->name);
		len = (int)strlen(label);

		if (folder == hovered)
			M_DrawCharacter(x - 8, y + shown * 8, 12 + ((int)(realtime * 4) & 1));

		if (partial_len > 0 && len <= cols - 2)
			M_PrintHighlight(x, y + shown * 8, label, partial, partial_len);
		else if (len <= cols - 2)
			M_Print(x, y + shown * 8, label);
		else
			M_PrintScroll(x, y + shown * 8, (cols - 2) * 8, label, 0.0, 1);

		++shown;
	}

	if (!matches)
	{
		if (!demosmenu.path_folders)
			M_PrintRGBA(x, y, va("no folders under %s", M_Demos_PathBase()),
				CL_PLColours_Parse("0xffffff"), 0.5f, true);
		else
			M_PrintRGBA(x, y, "no matching folders",
				CL_PLColours_Parse("0xffffff"), 0.5f, true);
		return;
	}

	if (matches > shown)
		M_DrawEllipsisBar(x, y + shown * 8, cols);
}

void M_Demos_Draw (void)
{
    int x, y, i, cols;
    int firstvis, numvis;

    x = 16;
    y = M_Demos_ListY();
    cols = 36;

    char demofilename[MAX_OSPATH];

    demosmenu.x = x;
    demosmenu.y = y;
    demosmenu.cols = cols;

    if (!keydown[K_MOUSE1]) // woods #mousemenu
        demosmenu.scrollbar_grab = false;

    if (demosmenu.prev_cursor != demosmenu.list.cursor)
    {
        demosmenu.prev_cursor = demosmenu.list.cursor;
        M_Ticker_Init(&demosmenu.ticker);
    }
    else
    {
        M_Ticker_Update(&demosmenu.ticker);
    }

	if (M_Demos_SyncMinFramesThreshold())
		M_Demos_RefilterEx(true);

	{
		M_Demos_TickMinFrameFilter();
		qboolean done = M_Demos_MinFrameFilterDone();
		if (demosmenu.minframes_hidden_since_refilter > 0 &&
			(demosmenu.minframes_hidden_since_refilter >= DEMOS_MINFRAME_REFILTER_BATCH || done))
		{
			M_Demos_RefilterEx(true);
			demosmenu.minframes_hidden_since_refilter = 0;
		}
	}

    // Background-parse a few demos per frame so metadata fills the cache without
    // blocking one frame. Search refilters are coalesced because they sweep the
    // full visible demo set.
    {
        qboolean parsed = M_Demos_TickBackgroundParse();
        if (demosmenu.list.search.len > 0 && parsed)
        {
            const int REFILTER_BATCH = 8;
            qboolean done = (demosmenu.bg_parse_cursor >= demosmenu.democount);
            if (demosmenu.bg_parse_refilter_count >= REFILTER_BATCH || done)
                M_Demos_RefilterEx(true);
        }
    }

	M_TextField_CheckMouseRelease();

	M_DrawCountHeader(x, 4, cols, "Demos",
		demosmenu.democount, "demo", "demos");
    M_DrawQuakeBar(x - 8, 16, cols + 2);
	M_Demos_DrawPathField();
	M_Demos_DrawShowId1Toggle();

	if (M_Demos_ShowPathOptions())
	{
		M_Demos_DrawPathOptions(x, y, cols);
		return;
	}

	if (!demosmenu.path_valid)
	{
		M_PrintRGBA(x, y, "invalid demos path",
			CL_PLColours_Parse("0xffffff"), 0.5f, true);
		return;
	}

    M_List_GetVisibleRange(&demosmenu.list, &firstvis, &numvis);
    for (i = 0; i < numvis; i++)
    {
        int idx = i + firstvis;
        int demo_idx = demosmenu.filtered_indices[idx];
        demoitem_t* demo_item = &demosmenu.items[demo_idx];
        qboolean selected = (idx == demosmenu.list.cursor);

        q_strlcpy(demofilename, cls.demofilename, sizeof(demofilename));

        demosmenu.items[demo_idx].active = M_Demos_SameDemoName(demo_item->name, demofilename);

        int color = demosmenu.items[demo_idx].active ? 0 : 1;
        int len = strlen(demo_item->display);
        int maxchars = (cols - 2);

        if (demosmenu.list.search.len > 0)
        {
            if (len <= maxchars)
            {
                // No scrolling needed, display with highlighting
                M_PrintHighlight(x, y + i * 8, demo_item->display, demosmenu.list.search.text, demosmenu.list.search.len);
            }
            else
            {
                // Scrolling needed, display with scrolling and highlighting
                M_PrintHighlightScroll(x, y + i * 8, (cols - 2) * 8,
				demo_item->display, demosmenu.list.search.text,
				selected ? demosmenu.ticker.scroll_time : 0.0);
            }
        }
        else
        {
            if (len <= maxchars)
            {
                // No scrolling needed
                if (color)
                    M_Print(x, y + i * 8, demo_item->display);
                else
                    M_PrintWhite(x, y + i * 8, demo_item->display);
            }
            else
            {
                // Scrolling needed
                M_PrintScroll(x, y + i * 8, (cols - 2) * 8,
                    demo_item->display,
                    selected ? demosmenu.ticker.scroll_time : 0.0,
                    color);
            }
        }

        if (selected)
            M_DrawCharacter(x - 8, y + i * 8, 12 + ((int)(realtime * 4) & 1));
    }

	if (demosmenu.list.numitems == 0)
		M_PrintRGBA(x, y, "no demos in this path",
			CL_PLColours_Parse("0xffffff"), 0.5f, true);

    if (M_List_GetOverflow(&demosmenu.list) > 0)
    {
        M_List_DrawScrollbar(&demosmenu.list, x + cols * 8 - 8, y);

        if (demosmenu.list.scroll > 0)
            M_DrawEllipsisBar(x, y - 8, cols);
        if (demosmenu.list.scroll + demosmenu.list.viewsize < demosmenu.list.numitems)
            M_DrawEllipsisBar(x, y + demosmenu.list.viewsize * 8, cols);
    }

    if (demosmenu.list.cursor >= 0 && demosmenu.list.cursor < demosmenu.list.numitems)
    {
        int demo_idx = demosmenu.filtered_indices[demosmenu.list.cursor];
        demoitem_t* di = &demosmenu.items[demo_idx];
        int info_y = y + demosmenu.list.viewsize * 8 + 4;
        qboolean at_bottom = (demosmenu.list.scroll + demosmenu.list.viewsize >= demosmenu.list.numitems);
        if (!at_bottom && M_List_GetOverflow(&demosmenu.list) > 0)
            info_y += 8;
        
        int current_y = info_y;
        
        if (di->map[0] && di->duration[0] && di->filesize[0])
        {
            M_Print(x, current_y, va("%s (%s) - %s", di->map, di->duration, di->filesize));
            current_y += 8;
        }
        else if (di->map[0] && di->duration[0])
        {
            M_Print(x, current_y, va("%s (%s)", di->map, di->duration));
            current_y += 8;
        }
        else if (di->map[0])
        {
            M_Print(x, current_y, di->map);
            current_y += 8;
        }
        else if (di->duration[0])
        {
            M_Print(x, current_y, va("Duration: %s", di->duration));
            current_y += 8;
        }

        if (di->stats[0])
        {
            M_PrintWhite(x, current_y, di->stats);
            current_y += 8;
        }

        {
            const char *introlabel = M_Demos_OriginalIntroDemoLabel(di->name, di->from_id1);
            if (introlabel)
            {
                M_Print(x, current_y, introlabel);
                current_y += 8;
            }
        }

        if (di->players[0])
        {
            // Handle player display with 40-character line wrapping
            char players_copy[256];
            q_strlcpy(players_copy, di->players, sizeof(players_copy));
            
            char* pos = players_copy;
            char line_buffer[64];
            int line_pos = 0;
            qboolean first_line = true;
            int line_count = 0;
            
            while (*pos)
            {
                // Find next comma or end of string
                char* next_comma = strchr(pos, ',');
                int name_len;
                
                if (next_comma)
                {
                    name_len = next_comma - pos;
                    // Skip comma and space after it
                    while (next_comma[1] == ' ') next_comma++;
                }
                else
                {
                    name_len = strlen(pos);
                }
                
                // Check if this name fits on current line (including comma and space if there are more names)
                int comma_space = next_comma ? 2 : 0; // ", " if there are more names
                int needed_space = name_len + comma_space;
                
                if (line_pos > 0 && line_pos + needed_space > 40)
                {
                    // Check if we've reached the 3-line limit
                    if (line_count >= 2) // 0, 1, 2 = 3 lines total
                    {
                        // Add ellipsis to indicate more players
                        if (line_pos + 4 <= 40) // Space for " ..."
                        {
                            line_buffer[line_pos++] = ' ';
                            line_buffer[line_pos++] = '.';
                            line_buffer[line_pos++] = '.';
                            line_buffer[line_pos++] = '.';
                        }
                        line_buffer[line_pos] = '\0';
                        M_PrintWhite(x, current_y, line_buffer);
                        break; // Stop processing more players
                    }
                    
                    // Print current line and start new one
                    line_buffer[line_pos] = '\0';
                    M_PrintWhite(x, current_y, line_buffer); // Remove indent - all lines align to same x position
                    if (first_line)
                        first_line = false;
                    current_y += 8;
                    line_count++;
                    line_pos = 0;
                }
                
                // Copy the name
				memcpy(line_buffer + line_pos, pos, name_len);
                line_pos += name_len;
                
                // Add comma and space after name if there are more names coming
                if (next_comma)
                {
                    line_buffer[line_pos++] = ',';
                    line_buffer[line_pos++] = ' ';
                }
                
                // Move to next name
                if (next_comma)
                    pos = next_comma + 1;
                else
                    break;
            }
            
            // Print final line if any content (and we haven't exceeded 3 lines)
            if (line_pos > 0 && line_count < 3)
            {
                line_buffer[line_pos] = '\0';
                M_PrintWhite(x, current_y, line_buffer); // Remove indent - all lines align to same x position
                current_y += 8;
            }
        }
    }

    if (demosmenu.list.search.len > 0)
    {
        M_DrawTextBox(16, 180, 32, 1);
        M_PrintHighlight(24, 188, demosmenu.list.search.text,
            demosmenu.list.search.text,
            demosmenu.list.search.len);
        int cursor_x = 24 + 8 * demosmenu.list.search.len;
		if (demosmenu.list.numitems == 0)
			M_DrawCharacter(cursor_x, 188, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, 188, 10 + ((int)(realtime * 4) & 1));
    }
}



qboolean M_Demos_Match(int index, char initial)
{
    int demo_idx = demosmenu.filtered_indices[index];
    return q_tolower(demosmenu.items[demo_idx].display[0]) == initial;
}

static int M_Demos_PathSuffixTextX(void)
{
	return DEMOS_PATH_TEXT_X + ((int)strlen(M_Demos_PathBase()) + 1) * 8;
}

static qboolean M_Demos_MouseInPathField(void)
{
	int box_w = (DEMOS_PATH_BOX_CHARS + 2) * 8;

	return M_TextField_MouseInRow(m_mousey, DEMOS_PATH_ROW_Y) &&
		m_mousex >= DEMOS_PATH_BOX_X &&
		m_mousex <= DEMOS_PATH_BOX_X + box_w;
}

static void M_Demos_BeginPathEdit(void)
{
	demosmenu.path_editing = true;
	M_TextField_ClampCursor(&demosmenu.path_field);
	M_Demos_UpdatePathHint();
}

static void M_Demos_EndPathEdit(void)
{
	demosmenu.path_editing = false;
	demosmenu.path_tabpartial[0] = '\0';
	demosmenu.path_hint[0] = '\0';
	M_TextField_ClearSelection(&demosmenu.path_field);
}

static void M_Demos_MouseClickPathField(void)
{
	int view_start = M_Demos_PathViewStart();
	int suffix_x = M_Demos_PathSuffixTextX();

	M_Demos_BeginPathEdit();
	demosmenu.path_tabpartial[0] = '\0';
	M_TextField_MouseClick(&demosmenu.path_field, m_mousex,
		suffix_x - view_start * 8);
	M_Demos_UpdatePathHint();
}

void M_Demos_Key(int key)
{
    int x, y; // woods #mousemenu

	if (demosmenu.path_editing)
	{
		char old_suffix[MAX_QPATH];

		q_strlcpy(old_suffix, demosmenu.path_suffix, sizeof(old_suffix));
		if (M_TextField_Key(&demosmenu.path_field, key))
		{
			if (strcmp(old_suffix, demosmenu.path_suffix))
				M_Demos_PathChanged();
			else
				M_Demos_UpdatePathHint();
			return;
		}

		switch (key)
		{
		case K_TAB:
			if (M_Menu_TabCompleteFileList(&demosmenu.path_field, demosmenu.path_suffix,
				sizeof(demosmenu.path_suffix), demosmenu.path_folders,
				demosmenu.path_tabpartial, sizeof(demosmenu.path_tabpartial)))
			{
				M_Demos_CleanPathSuffix();
				M_Demos_RebuildForCurrentPath();
				S_LocalSound("misc/menu2.wav");
			}
			else
				M_Demos_UpdatePathHint();
			return;

		case K_ESCAPE:
		case K_BBUTTON:
			M_Demos_EndPathEdit();
			return;

		case K_MOUSE4:
		case K_MOUSE2:
			M_Demos_EndPathEdit();
			S_LocalSound("misc/menu1.wav");
			return;

		case K_ENTER:
		case K_KP_ENTER:
		case K_ABUTTON:
			if (demosmenu.path_valid)
			{
				M_Demos_EndPathEdit();
				S_LocalSound("misc/menu1.wav");
			}
			else
				S_LocalSound("misc/menu3.wav");
			return;

		case K_MOUSE1:
			{
				filelist_item_t *folder = M_Demos_GetHoveredPathOption();

				if (folder)
				{
					M_Demos_SelectPathOption(folder);
					S_LocalSound("misc/menu2.wav");
				}
				else if (M_Demos_MouseInShowId1Toggle())
				{
					M_Demos_EndPathEdit();
					M_Demos_ToggleShowId1();
				}
				else if (M_Demos_MouseInPathField())
					M_Demos_MouseClickPathField();
				else
					M_Demos_EndPathEdit();
			}
			return;

		case K_UPARROW:
		case K_DOWNARROW:
		case K_MWHEELUP:
		case K_MWHEELDOWN:
			if (M_Demos_ShowPathOptions())
				return;
			break;

		case K_BACKSPACE:
		case K_DEL:
			return;

		default:
			if (key >= 32 && key < 127)
				return;
			break;
		}
	}

	if (key == K_MOUSE1 && M_Demos_MouseInShowId1Toggle())
	{
		M_Demos_ToggleShowId1();
		return;
	}

	if (key == K_MOUSE1 && M_Demos_MouseInPathField())
	{
		M_Demos_MouseClickPathField();
		return;
	}

	// Handle Ctrl+U or Ctrl+Backspace first
	if (keydown[K_CTRL])
	{
		if ((key == 'u' || key == 'U') && demosmenu.list.search.len > 0)
		{
			demosmenu.list.search.len = 0;
			demosmenu.list.search.text[0] = 0;
			M_Demos_Refilter();
			return;
		}
		else if (key == K_BACKSPACE && demosmenu.list.search.len > 0)
		{
			M_DeletePrevWord(&demosmenu.list.search);
			M_Demos_Refilter();
			return;
		}
		else if (key == K_BACKSPACE && demosmenu.list.numitems > 0)
		{
			// Delete the currently selected demo file
			int demo_idx = demosmenu.filtered_indices[demosmenu.list.cursor];
			demoitem_t *demo = &demosmenu.items[demo_idx];

			// Copy demo name to local buffer BEFORE freeing memory
			char demo_name[MAX_QPATH];
			q_strlcpy(demo_name, demo->name, sizeof(demo_name));

			if (demo->from_id1)
			{
				S_LocalSound("misc/menu3.wav");
				Con_Printf("cannot delete inherited id1 demo ^m%s^m from this menu\n", M_Demos_CommandName(demo_name));
				return;
			}

			char demo_path[MAX_OSPATH];

			// Construct the full path to the demo file
			if (strchr(demo_name, '/') || strchr(demo_name, '\\'))
				q_snprintf(demo_path, sizeof(demo_path), "%s/%s", com_gamedir, demo_name);
			else
				q_snprintf(demo_path, sizeof(demo_path), "%s/demos/%s", com_gamedir, demo_name);

			// Try to delete the file
			if (remove(demo_path) == 0)
			{
				// Successfully deleted file, now remove from demo list
				FileList_Subtract(demo_name, &demolist);

				// Store current cursor position
				int old_cursor = demosmenu.list.cursor;

				M_Demos_RebuildForCurrentPath();

				// Restore cursor position (adjust if necessary)
				if (old_cursor >= demosmenu.list.numitems && demosmenu.list.numitems > 0)
					demosmenu.list.cursor = demosmenu.list.numitems - 1;
				else if (demosmenu.list.numitems > 0)
					demosmenu.list.cursor = old_cursor;
				else
					demosmenu.list.cursor = -1;

				// Play confirmation sound
				S_LocalSound("misc/menu1.wav");

				Con_Printf("demo ^m%s^m deleted\n", demo_name);
			}
			else
			{
				// Failed to delete file
				S_LocalSound("misc/menu3.wav");
				Con_Printf("failed to delete demo ^m%s^m\n", demo_name);
			}
			return;
		}
	}
	
    if (key >= 32 && key < 127) // Handle search input first, printable characters
    {
        if (demosmenu.list.search.len < demosmenu.list.search.maxlen)
        {
            demosmenu.list.search.text[demosmenu.list.search.len++] = key;
            demosmenu.list.search.text[demosmenu.list.search.len] = 0;
            M_Demos_Refilter();
            return;
        }
    }

    if (key == K_BACKSPACE)
    {
        if (demosmenu.list.search.len > 0)
        {
            demosmenu.list.search.text[--demosmenu.list.search.len] = 0;
            M_Demos_Refilter();
            return;
        }
    }

    if (demosmenu.scrollbar_grab)
    {
        switch (key)
        {
        case K_ESCAPE:
        case K_BBUTTON:
        case K_MOUSE4:
        case K_MOUSE2:
            demosmenu.scrollbar_grab = false;
            break;
        }
        return;
    }

    if (M_List_Key(&demosmenu.list, key))
        return;

    if (M_List_CycleMatch(&demosmenu.list, key, M_Demos_Match))
        return;

    if (M_Ticker_Key(&demosmenu.ticker, key))
        return;

    switch (key)
    {
    case K_ESCAPE:
        if (demosmenu.list.search.len > 0)
        {
            demosmenu.list.search.len = 0;
            demosmenu.list.search.text[0] = 0;
            M_Demos_Refilter();
            return;
        }
        // Fall through to exit menu if search is already empty
    case K_BBUTTON:
    case K_MOUSE4: // woods #mousemenu
    case K_MOUSE2:
        M_Demos_MetadataCache_SaveIfDirty();
        if (demosmenu.prev == m_options)
            M_Menu_Options_f();
        else
            M_Menu_Main_f();
        break;

    case K_ENTER:
    case K_KP_ENTER:
    case K_ABUTTON:
    enter: // woods #mousemenu
        if (demosmenu.list.numitems > 0)
        {
			demoitem_t *demo = &demosmenu.items[demosmenu.filtered_indices[demosmenu.list.cursor]];

			if (M_Demos_AllowPlayByMinFrames(demo) &&
				M_Demos_QueuePlayDemo(demo->name))
			{
				M_Demos_RememberCurrentPath();
				M_Demos_MetadataCache_SaveIfDirty();
				M_Menu_Main_f();
			}
        }
        else
            S_LocalSound("misc/menu3.wav");
        break;

    case K_MOUSE1: // woods #mousemenu
        x = m_mousex - demosmenu.x - (demosmenu.cols - 1) * 8;
        y = m_mousey - demosmenu.y;
        if (x < -8 || !M_List_UseScrollbar(&demosmenu.list, y))
            goto enter;
        demosmenu.scrollbar_grab = true;
        M_Demos_Mousemove(m_mousex, m_mousey);
        break;

    default:
        break;
    }
}

void M_Demos_Char(int key)
{
	if (!demosmenu.path_editing)
		return;

	if (M_TextField_Char(&demosmenu.path_field, key))
		M_Demos_PathChanged();
}

qboolean M_Demos_TextEntry(void)
{
	return demosmenu.path_editing;
}

void M_Demos_Mousemove(int cx, int cy) // woods #mousemenu
{
	if (textfield_mouse_dragging && textfield_drag_field == &demosmenu.path_field)
	{
		M_TextField_MouseDrag(cx);
		M_Demos_UpdatePathHint();
		return;
	}

	cy -= demosmenu.y;

	if (M_Demos_ShowPathOptions())
		return;

	if (demosmenu.scrollbar_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			demosmenu.scrollbar_grab = false;
			return;
		}
		M_List_UseScrollbar(&demosmenu.list, cy);
		// Note: no return, we also update the cursor
	}

	M_List_Mousemove(&demosmenu.list, cy);
}

/*
=========================================
Credit Menu - used by the 2021 re-release
=========================================
*/

void M_Menu_Credits_f (void)
{
}

void M_Menu_SearchInternet_f (void) // woods
{
	M_Menu_Search_f(SLIST_INTERNET);
}

static struct
{
	const char *name;
	xcommand_t function;
	cmd_function_t *cmd;
} menucommands[] =
{
	{"menu_main", M_Menu_Main_f},
	{"menu_singleplayer", M_Menu_SinglePlayer_f},
	{"menu_load", M_Menu_Load_f},
	{"menu_save", M_Menu_Save_f},
	{"menu_skill", M_Menu_Skill_f},
	{"menu_multiplayer", M_Menu_MultiPlayer_f},
	{"menu_slist", M_Menu_SearchInternet_f},
	{"menu_setup", M_Menu_Setup_f},
	{"menu_options", M_Menu_Options_f},
	{"menu_keys", M_Menu_Keys_f},
	{"menu_mouse", M_Menu_Mouse_f},
	{"menu_controller", M_Menu_Controller_f},
	{"menu_controller_test", M_Menu_Controller_Test_f},
	{"menu_weaponwheel", M_Menu_WeaponWheel_f},
	{"menu_sound", M_Menu_Sound_f},
	{"menu_voip", M_Menu_Voip_f},
	{"menu_game", M_Menu_Game_f},
	{"menu_hud", M_Menu_HUD_f},
	{"menu_crosshair", M_Menu_Crosshair_f},
	{"menu_console", M_Menu_Console_f},
	{"menu_colorpicker", M_Menu_ColorPicker_f},
	{"menu_startup", M_Menu_Startup_f},
	{"menu_demooptions", M_Menu_DemoOptions_f},
	{"menu_pakloading", M_Menu_PakLoading_f},
	{"menu_modelviewer", M_Menu_ModelViewer_f},
	{"menu_saving", M_Menu_Saving_f},
	{"menu_misc", M_Menu_Extras_f},
	{"menu_shortcuts", M_Menu_Shortcuts_f},
	{"menu_version", M_Menu_Version_f},
	{"menu_config", M_Menu_ResetConfig_f},
	{"menu_video", M_Menu_Video_f},
	{"menu_graphics", M_Menu_Graphics_f},
	{"help", M_Menu_Help_f},
	{"menu_quit", M_Menu_Quit_f},
	{"menu_credits", M_Menu_Credits_f}, // needed by the 2021 re-release
	{"menu_namemaker", M_Menu_NameMaker_f}, // woods #namemaker
	{"namemaker", M_Shortcut_NameMaker_f}, // woods #namemaker
	{"menu_mods", M_Menu_Mods_f}, // woods
	{"menu_downloadmods", M_Menu_DownloadMods_f},
	{"menu_demos", M_Menu_Demos_f}, // woods
	{"menu_maps", M_Menu_Maps_f}, // woods
	{"menu_downloadmaps", M_Menu_DownloadMaps_f},
	{"menu_bookmarks", M_Menu_Bookmarks_f}, // woods #bookmarksmenu
	{"bookmark", M_Shortcut_Bookmarks_Edit_f}, // woods #bookmarksmenu
	{"menu_history", M_Menu_History_f}, // woods #historymenu
};

//=============================================================================
/* MenuQC Subsystem */
#define MENUQC_PROGHEADER_CRC 10020
void MQC_End(void)
{
	PR_SwitchQCVM(NULL);
}
void MQC_Begin(void)
{
	PR_SwitchQCVM(&cls.menu_qcvm);
	pr_global_struct = NULL;
}
static qboolean MQC_Init(void)
{
	size_t i;
	qboolean success;
	PR_SwitchQCVM(&cls.menu_qcvm);
	if (COM_CheckParm("-qmenu") || fitzmode || !pr_checkextension.value)
		success = false;
	else
		success = PR_LoadProgs("menu.dat", false, MENUQC_PROGHEADER_CRC, pr_menubuiltins, pr_menunumbuiltins);
	if (success && qcvm->extfuncs.m_draw)
	{
		for (i = 0; i < sizeof(menucommands)/sizeof(menucommands[0]); i++)
			if (menucommands[i].cmd)
			{
				Cmd_RemoveCommand (menucommands[i].cmd);
				menucommands[i].cmd = NULL;
			}


		qcvm->max_edicts = CLAMP (MIN_EDICTS,(int)max_edicts.value,MAX_EDICTS);
		qcvm->edicts = (edict_t *) malloc (qcvm->max_edicts*qcvm->edict_size);
		qcvm->num_edicts = qcvm->reserved_edicts = 1;
		memset(qcvm->edicts, 0, qcvm->num_edicts*qcvm->edict_size);

		if (qcvm->extfuncs.m_init)
			PR_ExecuteProgram(qcvm->extfuncs.m_init);
	}
	PR_SwitchQCVM(NULL);
	return success;
}

void MQC_Shutdown(void)
{
	size_t i;
	if (key_dest == key_menu)
		key_dest = key_console;
	PR_ClearProgs(&cls.menu_qcvm);					//nuke it from orbit

	// Clean up menu memory
	M_Demos_FreeItems();
	M_Demos_ClearFileList(&demosmenu.path_folders);

	for (i = 0; i < sizeof(menucommands)/sizeof(menucommands[0]); i++)
		if (!menucommands[i].cmd)
			menucommands[i].cmd = Cmd_AddCommand (menucommands[i].name, menucommands[i].function);
}

static void MQC_Command_f(void)
{
	if (cls.menu_qcvm.extfuncs.GameCommand)
	{
		MQC_Begin();
		G_INT(OFS_PARM0) = PR_MakeTempString(Cmd_Args());
		PR_ExecuteProgram(qcvm->extfuncs.GameCommand);
		MQC_End();
	}
	else
		Con_Printf("menu_cmd: no menuqc GameCommand function available\n");
}

//=============================================================================
/* Menu Subsystem */

/*
================
M_ToggleMenu_f
================
*/
void M_ToggleMenu (int mode)
{
	if (cls.menu_qcvm.extfuncs.m_toggle)
	{
		MQC_Begin();
		G_FLOAT(OFS_PARM0) = mode;
		PR_ExecuteProgram(qcvm->extfuncs.m_toggle);
		MQC_End();
		return;
	}

	m_entersound = true;

	if (key_dest == key_menu)
	{
		if (mode != 0 && m_state != m_main)
		{
			if (m_state == m_slist)
				CleanupPingThreads();
			M_Menu_Main_f ();
			return;
		}

		if (m_state == m_slist)
			CleanupPingThreads();
		key_dest = key_game;
		m_state = m_none;

		IN_UpdateGrabs();
		return;
	}
	else if (mode == 0)
		return;
	if (mode == -1 && key_dest == key_console)
	{
		Con_ToggleConsole_f ();
	}
	else
	{
		M_Menu_Main_f ();
	}
}
static void M_ToggleMenu_f (void)
{
	M_ToggleMenu((Cmd_Argc() < 2) ? -1 : atoi(Cmd_Argv(1)));
}

static void M_MenuRestart_f (void)
{
	qboolean off = !strcmp(Cmd_Argv(1), "off");
	if (off || !MQC_Init())
		MQC_Shutdown();
}

void M_Init (void)
{
	cmd_function_t *update_cmd;

	Cmd_AddCommand ("togglemenu", M_ToggleMenu_f);
	Cmd_AddCommand ("menu_cmd", MQC_Command_f);
	Cmd_AddCommand ("menu_restart", M_MenuRestart_f);	//qss still loads progs on hunk, so we can't do this safely.
	Cmd_AddCommand ("update", M_Update_f);
	update_cmd = Cmd_FindCommand("update");
	if (update_cmd)
		update_cmd->completion = M_Update_Completion_f;

	Cvar_RegisterVariable (&ui_live_preview);

	if (!MQC_Init())
		MQC_Shutdown();
}


void M_Draw (void)
{
	if (cls.menu_qcvm.extfuncs.m_draw)
	{	//Spike -- menuqc
		float s = q_min((float)glwidth / 320.0, (float)glheight / 200.0);
		M_LivePreview_Reset ();
		s = CLAMP (1.0, scr_menuscale.value, s);
		if (!host_initialized)
			return;
		MQC_Begin();

		if (scr_con_current && key_dest == key_menu)
		{	//make sure we don't have the console getting drawn in the background making the menu unreadable.
			//FIXME: rework console to show over the top of menuqc.
			Draw_ConsoleBackground ();
			S_ExtraUpdate ();
		}

		GL_SetCanvas (CANVAS_MENUQC);
		glEnable (GL_BLEND);	//in the finest tradition of glquake, we litter gl state calls all over the place. yay state trackers.
		glDisable (GL_ALPHA_TEST);	//in the finest tradition of glquake, we litter gl state calls all over the place. yay state trackers.
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

		if (qcvm->extglobals.time)
			*qcvm->extglobals.time = realtime;
		if (qcvm->extglobals.frametime)
			*qcvm->extglobals.frametime = host_frametime;
		G_FLOAT(OFS_PARM0+0) = vid.width/s;
		G_FLOAT(OFS_PARM0+1) = vid.height/s;
		G_FLOAT(OFS_PARM0+2) = 0;
		PR_ExecuteProgram(qcvm->extfuncs.m_draw);

		glDisable (GL_BLEND);
		glEnable (GL_ALPHA_TEST);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);	//back to ignoring vertex colours.
		glDisable(GL_SCISSOR_TEST);
		glColor3f (1,1,1);

		MQC_End();
		return;
	}

	if (m_state == m_none || key_dest != key_menu)
	{
		M_LivePreview_Update ();
		return;
	}

	M_LivePreview_Update ();

	if (!m_recursiveDraw)
	{
		qboolean live_world_menu = (m_state == m_skywind && cl.worldmodel);
		float lp_frac = M_LivePreview_Alpha ();

		if (scr_con_current && !M_WantsConsole (NULL))
		{
			Draw_ConsoleBackground ();
			S_ExtraUpdate ();
		}

		if (m_state != m_crosshair && !live_world_menu)
		{
			if (lp_frac > 0.f)
			{
				M_LivePreview_DrawEffects ();
				M_LivePreview_DrawFadeScreen ();
			}
			else if (!scr_con_current)
				Draw_FadeScreen ();
		}
	}
	else
	{
		m_recursiveDraw = false;
	}

	GL_SetCanvas (CANVAS_MENU); //johnfitz

	{
		float lp_frac = M_LivePreview_Alpha ();
		if (lp_frac > 0.f)
		{
			// Fade the menu itself (Ironwail-style). gl_menu_alpha is the
			// single source of truth: draw primitives that reset glColor at
			// the end honor it (see gl_draw.c) so the bracket survives across
			// nested RGBA draws. MODULATE texenv lets pics scale by glColor's
			// alpha; BLEND on + ALPHA_TEST off avoids the usual masked-pic
			// logic discarding our faded pixels. Squaring (alpha *= alpha)
			// matches Ironwail's fade curve: fast initial dim, slow tail.
			gl_menu_alpha = 1.f - lp_frac;
			gl_menu_alpha *= gl_menu_alpha;
			glEnable (GL_BLEND);
			glDisable (GL_ALPHA_TEST);
			glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glColor4f (1.f, 1.f, 1.f, gl_menu_alpha);
		}
	}

	switch (m_state)
	{
	case m_none:
		break;

	case m_main:
		M_Main_Draw ();
		break;

	case m_singleplayer:
		M_SinglePlayer_Draw ();
		break;

	case m_load:
		M_Load_Draw ();
		break;

	case m_save:
		M_Save_Draw ();
		break;

	case m_maps: // woods #mapsmenu (iw)
		M_Maps_Draw();
		break;

	case m_downloadmaps:
		M_DownloadMaps_Draw();
		break;

	case m_skill: // woods #skillmenu (iw)
		M_Skill_Draw();
		break;

	case m_multiplayer:
		M_MultiPlayer_Draw ();
		break;

	case m_history: // woods #historymenu
		M_History_Draw();
		break;

	case m_bookmarks: // woods #bookmarksmenu
		M_Bookmarks_Draw();
		break;

	case m_bookmarks_edit: // woods #bookmarksmenu
		M_Bookmarks_Edit_Draw();
		break;

	case m_setup:
		M_Setup_Draw ();
		break;

	case m_namemaker: // woods #namemaker
		M_NameMaker_Draw();
		break;

	case m_net:
		M_Net_Draw ();
		break;

	case m_options:
		M_Options_Draw ();
		break;

	case m_keys:
		M_Keys_Draw ();
		break;

	case m_mouse:
		M_Mouse_Draw();
		break;

	case m_controller:
		M_Controller_Draw();
		break;

	case m_controller_test:
		M_Controller_Test_Draw();
		break;

	case m_weaponwheel:
		M_WeaponWheel_Draw();
		break;

	case m_calibration:
		M_Calibration_Update();
		if (m_state == m_calibration)
			M_Calibration_Draw();
		break;

	case m_colorpicker:
		M_ColorPicker_Draw();
		break;

	case m_extras:
		M_Extras_Draw ();
		break;

	case m_saving:
		M_Saving_Draw();
		break;

	case m_shortcuts:
		M_Shortcuts_Draw();
		break;

	case m_version:
		M_Version_Draw();
		break;

	case m_resetconfig: // woods #resetconfig
		M_ResetConfig_Draw();
		break;

	case m_startup:
		M_Startup_Draw();
		break;

	case m_demooptions:
		M_DemoOptions_Draw();
		break;

	case m_pakloading:
		M_PakLoading_Draw();
		break;

	case m_modelviewer:
		M_ModelViewer_Draw();
		break;

	case m_video:
		M_Video_Draw ();
		break;

	case m_graphics:
		M_Graphics_Draw();
		break;

	case m_sky:
		M_Sky_Draw();
		break;

	case m_skywind:
		M_Skywind_Draw();
		break;

	case m_sound:
		M_Sound_Draw ();
		break;

	case m_voip:
		M_Voip_Draw ();
		break;

	case m_game:
		M_Game_Draw();
		break;

	case m_playerxray:
		M_PlayerXray_Draw();
		break;

	case m_hud:
		M_HUD_Draw();
		break;

	case m_crosshair:
		M_Crosshair_Draw();
		break;

	case m_console:
		M_Console_Draw();
		break;

	case m_mods: // woods #modsmenu (iw)
		M_Mods_Draw();
		break;

	case m_downloadmods:
		M_DownloadMods_Draw();
		break;

	case m_demos: // woods #demosmenu
		M_Demos_Draw ();
		break;

	case m_help:
		M_Help_Draw ();
		break;

	case m_quit:
		if (/*!fitzmode || */(cl.matchinp != 1 || cl.notobserver != 1 || cl.teamcolor[0] == 0) || cls.demoplayback) // woods #matchquit
		{ /* QuakeSpasm customization: */
			/* Quit now! S.A. */
			key_dest = key_console;
			Host_Quit_f ();
		}
		M_Quit_Draw ();
		break;

	case m_lanconfig:
		M_LanConfig_Draw ();
		break;

	case m_gameoptions:
		M_GameOptions_Draw ();
		break;

	case m_search:
		M_Search_Draw ();
		break;

	case m_slist:
		M_ServerList_Draw ();
		break;
	}

	if (M_LivePreview_Alpha () > 0.f)
	{
		gl_menu_alpha = 1.f;
		glColor4f (1.f, 1.f, 1.f, 1.f);
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glEnable (GL_ALPHA_TEST);
		glDisable (GL_BLEND);
	}

	if (m_entersound)
	{
		S_LocalSound ("misc/menu2.wav");
		m_entersound = false;
	}

	S_ExtraUpdate ();
}


// menus whose key handler treats printable input as a list-search filter
// and K_BACKSPACE as deleting a character from that search
static qboolean M_HasSearchField (void)
{
	switch (m_state)
	{
	case m_options:
	case m_load:
	case m_maps:
	case m_downloadmaps:
	case m_keys:
	case m_mouse:
	case m_video:
	case m_graphics:
	case m_sound:
	case m_game:
	case m_hud:
	case m_console:
	case m_extras:
	case m_shortcuts:
	case m_version:
	case m_startup:
	case m_demooptions:
	case m_pakloading:
	case m_modelviewer:
	case m_slist:
	case m_mods:
	case m_downloadmods:
	case m_demos:
		return true;
	default:
		return false;
	}
}

static int M_ShiftedPrintableKey (int key)
{
	if (key < 32 || key >= 127)
		return key;
	if (!keydown[K_SHIFT])
		return key;

	if (key >= 'a' && key <= 'z')
		return key - ('a' - 'A');

	switch (key)
	{
	case '`': return '~';
	case '1': return '!';
	case '2': return '@';
	case '3': return '#';
	case '4': return '$';
	case '5': return '%';
	case '6': return '^';
	case '7': return '&';
	case '8': return '*';
	case '9': return '(';
	case '0': return ')';
	case '-': return '_';
	case '=': return '+';
	case '[': return '{';
	case ']': return '}';
	case '\\': return '|';
	case ';': return ':';
	case '\'': return '"';
	case ',': return '<';
	case '.': return '>';
	case '/': return '?';
	default: return key;
	}
}


void M_Keydown (int key, qboolean repeat)
{
	qboolean has_search;

	if (cls.menu_qcvm.extfuncs.m_draw)	//don't get confused.
		return;

	if (!bind_grab)
	{
		switch (key)
		{
		case K_DPAD_UP:		key = K_UPARROW; break;
		case K_DPAD_DOWN:	key = K_DOWNARROW; break;
		case K_DPAD_LEFT:	key = K_LEFTARROW; break;
		case K_DPAD_RIGHT:	key = K_RIGHTARROW; break;
		case K_ABUTTON:		key = K_ENTER; break;
		case K_BBUTTON:		key = K_ESCAPE; break;
		default:
			break;
		}
	}

	// only allow repeat events for a few navigational keys
	// this reduces sound spam and, for gamepads, rumble spam
	has_search = M_HasSearchField();
	if (repeat)
	{
		switch (key)
		{
		case K_UPARROW:
		case K_DOWNARROW:
		case K_LEFTARROW:
		case K_RIGHTARROW:
		case K_KP_UPARROW:
		case K_KP_DOWNARROW:
		case K_KP_LEFTARROW:
		case K_KP_RIGHTARROW:
		case K_ESCAPE:
			break;
		case K_BACKSPACE:
		case K_DEL:
			if (M_TextEntry() || has_search)
				break;
			return;
		default:
			// also let printable characters repeat in list-search menus so
			// holding a letter keeps filtering, matching the typing behavior
			if (has_search && key >= 32 && key < 127)
				break;
			return;
		}
	}

	if (key == K_SHIFT)
		M_LivePreview_UpdateUserPin ();

	if (!bind_grab && has_search)
		key = M_ShiftedPrintableKey(key);

	switch (m_state)
	{
	case m_none:
		return;

	case m_main:
		M_Main_Key (key);
		return;

	case m_singleplayer:
		M_SinglePlayer_Key (key);
		return;

	case m_load:
		M_Load_Key (key);
		return;

	case m_save:
		M_Save_Key (key);
		return;

	case m_maps: // woods #demosmenu
		M_Maps_Key(key);
		return;

	case m_downloadmaps:
		M_DownloadMaps_Key(key);
		return;

	case m_skill: // woods #skillmenu (iw)
		M_Skill_Key(key);
		return;

	case m_multiplayer:
		M_MultiPlayer_Key (key);
		return;

	case m_history: // woods #historymenu
		M_History_Key(key);
		return;

	case m_bookmarks: // woods #bookmarksmenu
		M_Bookmarks_Key(key);
		return;

	case m_bookmarks_edit: // woods #bookmarksmenu
		M_Bookmarks_Edit_Key(key);
		return;

	case m_setup:
		M_Setup_Key (key);
		return;

	case m_namemaker: // woods #namemaker
		M_NameMaker_Key(key);
		return;

	case m_net:
		M_Net_Key (key);
		return;

	case m_options:
		M_Options_Key (key);
		return;

	case m_keys:
		M_Keys_Key (key);
		return;

	case m_mouse:
		M_Mouse_Key(key);
		return;

	case m_controller:
		M_Controller_Key(key);
		return;

	case m_controller_test:
		M_Controller_Test_Key(key);
		return;

	case m_weaponwheel:
		M_WeaponWheel_Key(key);
		return;

	case m_calibration:
		M_Calibration_Key(key);
		return;

	case m_colorpicker:
		M_ColorPicker_Key(key);
		return;

	case m_extras:
		M_Extras_Key (key);
		return;

	case m_saving:
		M_Saving_Key(key);
		return;

	case m_shortcuts:
		M_Shortcuts_Key(key);
		return;

	case m_version:
		M_Version_Key(key);
		return;

	case m_resetconfig: // woods #resetconfig
		M_ResetConfig_Key(key);
		return;

	case m_startup:
		M_Startup_Key(key);
		return;

	case m_demooptions:
		M_DemoOptions_Key(key);
		return;

	case m_pakloading:
		M_PakLoading_Key(key);
		return;

	case m_modelviewer:
		M_ModelViewer_Key(key);
		return;

	case m_video:
		M_Video_Key (key);
		return;

	case m_graphics:
		M_Graphics_Key(key);
		return;

	case m_sky:
		M_Sky_Key(key);
		return;

	case m_skywind:
		M_Skywind_Key(key);
		return;

	case m_sound:
		M_Sound_Key (key);
		break;

	case m_voip:
		M_Voip_Key (key);
		break;

	case m_game:
		M_Game_Key(key);
		break;

	case m_playerxray:
		M_PlayerXray_Key(key);
		break;

	case m_hud:
		M_HUD_Key(key);
		break;

	case m_crosshair:
		M_Crosshair_Key(key);
		break;

	case m_console:
		M_Console_Key(key);
		break;

	case m_mods: // woods #modsmenu (iw)
		M_Mods_Key(key);
		return;

	case m_downloadmods:
		M_DownloadMods_Key(key);
		return;

	case m_demos: // woods #demosmenu
		M_Demos_Key (key);
		return;

	case m_help:
		M_Help_Key (key);
		return;

	case m_quit:
		M_Quit_Key (key);
		return;

	case m_lanconfig:
		M_LanConfig_Key (key);
		return;

	case m_gameoptions:
		M_GameOptions_Key (key);
		return;

	case m_search:
		M_Search_Key (key);
		break;

	case m_slist:
		M_ServerList_Key (key);
		return;
	}
}

void M_Mousemove(int x, int y) // woods #mousemenu
{
	if (bind_grab)
		return;
	
	vrect_t bounds, viewport;

	Draw_GetMenuTransform(&bounds, &viewport);

	m_mousex = x = bounds.x + (int)((x - viewport.x) * bounds.width / (float)viewport.width + 0.5f);
	m_mousey = y = bounds.y + (int)((y - viewport.y) * bounds.height / (float)viewport.height + 0.5f);

	switch (m_state)
	{
	default:
		return;

	case m_none:
		return;

	case m_main:
		M_Main_Mousemove(x, y);
		return;

	case m_singleplayer:
		M_SinglePlayer_Mousemove(x, y);
		return;

	case m_load:
		M_Load_Mousemove(x, y);
		return;

	case m_save:
		M_Save_Mousemove(x, y);
		return;

	case m_maps:
		M_Maps_Mousemove(x, y);
		return;

	case m_downloadmaps:
		M_DownloadMaps_Mousemove(x, y);
		return;

	case m_skill:
		M_Skill_Mousemove(x, y);
		return;

	case m_multiplayer:
		M_MultiPlayer_Mousemove(x, y);
		return;

	case m_history: // woods #historymenu
		M_History_Mousemove(x, y);
		return;

	case m_bookmarks: // woods#bookmarksmenu
		M_Bookmarks_Mousemove(x, y);
		return;

	case m_bookmarks_edit: // woods #bookmarksmenu
		M_Bookmarks_Edit_Mousemove(x, y);
		return;

	case m_setup:
		M_Setup_Mousemove(x, y);
		return;

	case m_namemaker:
		M_NameMaker_Mousemove(x, y);
		return;

	case m_net:
		M_Net_Mousemove(x, y);
		return;

	case m_options:
		M_Options_Mousemove(x, y);
		return;

	case m_keys:
		M_Keys_Mousemove(x, y);
		return;

	case m_mouse:
		M_Mouse_Mousemove(x, y);
		return;

	case m_controller:
		M_Controller_Mousemove(x, y);
		return;

	case m_controller_test:
		return;

	case m_weaponwheel:
		M_WeaponWheel_Mousemove(x, y);
		return;

	case m_colorpicker:
		M_ColorPicker_Mousemove(x, y);
		return;

	case m_video:
		M_Video_Mousemove(x, y);
		return;

	case m_graphics:
		M_Graphics_Mousemove(x, y);
		return;

	case m_sky:
		M_Sky_Mousemove(x, y);
		return;

	case m_skywind:
		M_Skywind_Mousemove(x, y);
		return;

	case m_sound:
		M_Sound_Mousemove(x, y);
		return;

	case m_voip:
		M_Voip_Mousemove(x, y);
		return;

	case m_game:
		M_Game_Mousemove(x, y);
		return;

	case m_playerxray:
		M_PlayerXray_Mousemove(x, y);
		return;

	case m_hud:
		M_HUD_Mousemove(x, y);
		return;

	case m_crosshair:
		M_Crosshair_Mousemove(x, y);
		return;

	case m_console:
		M_Console_Mousemove(x, y);
		return;

	case m_extras:
		M_Extras_Mousemove(x, y);
		return;

	case m_saving:
		M_Saving_Mousemove(x, y);
		return;

	case m_shortcuts:
		M_Shortcuts_Mousemove(x, y);
		return;

	case m_version:
		M_Version_Mousemove(x, y);
		return;

	case m_resetconfig: // woods #resetconfig
		M_ResetConfig_Mousemove(x, y);
		break;

	case m_startup:
		M_Startup_Mousemove(x, y);
		return;

	case m_demooptions:
		M_DemoOptions_Mousemove(x, y);
		return;

	case m_pakloading:
		M_PakLoading_Mousemove(x, y);
		return;

	case m_modelviewer:
		M_ModelViewer_Mousemove(x, y);
		return;

	case m_mods:
		M_Mods_Mousemove(x, y);
		return;

	case m_downloadmods:
		M_DownloadMods_Mousemove(x, y);
		return;

	case m_demos:
		M_Demos_Mousemove(x, y);
		return;

		//case m_help:
		//	M_Help_Mousemove (x, y);
		//	return;

		//case m_quit:
		//	M_Quit_Mousemove (x, y);
		//	return;

	case m_lanconfig:
		M_LanConfig_Mousemove(x, y);
		return;

	case m_gameoptions:
		M_GameOptions_Mousemove(x, y);
		return;

		//case m_search:
		//	M_Search_Mousemove (x, y);
		//	break;

	case m_slist:
		M_ServerList_Mousemove(x, y);
		return;
	}
}


void M_Charinput (int key)
{
	if (cls.menu_qcvm.extfuncs.m_draw)	//don't get confused.
		return;

	switch (m_state)
	{
	case m_setup:
		M_Setup_Char (key);
		return;
	case m_console:
		M_Console_Char(key);
		return;
	case m_sky:
		M_Sky_Char(key);
		return;
	case m_bookmarks_edit: // woods #bookmarksmenu
		M_Bookmarks_Edit_Char(key);
		return;
	case m_quit:
		M_Quit_Char (key);
		return;
	case m_lanconfig:
		M_LanConfig_Char (key);
		return;
	case m_demos:
		M_Demos_Char(key);
		return;
	case m_resetconfig:
		M_ResetConfig_Char(key);
		return;
	case m_gameoptions:
		M_GameOptions_Char(key);
		return;
	default:
		return;
	}
}


qboolean M_TextEntry (void)
{
	switch (m_state)
	{
	case m_setup:
		return M_Setup_TextEntry ();
	case m_namemaker:
		return M_NameMaker_TextEntry();
	case m_console:
		return M_Console_TextEntry();
	case m_sky:
		return M_Sky_TextEntry();
	case m_bookmarks_edit: // woods #bookmarksmenu
		return M_Bookmarks_Edit_TextEntry();
	case m_quit:
		return M_Quit_TextEntry ();
	case m_lanconfig:
		return M_LanConfig_TextEntry ();
	case m_demos:
		return M_Demos_TextEntry();
	case m_resetconfig:
		return M_ResetConfig_TextEntry();
	case m_gameoptions:
		return M_GameOptions_TextEntry();
	default:
		return false;
	}
}

qboolean M_WantsIBeamCursor(void)
{
	if (key_dest != key_menu)
		return false;

	if (M_TextField_IsDraggingAny())
		return true;

	if (m_state == m_namemaker)
		return namemaker_edit_active;

	if (m_state == m_demos)
		return M_Demos_TextEntry() &&
			!M_Demos_MouseInPathOptionsArea() &&
			!M_Demos_MouseInShowId1Toggle();

	if (m_state == m_resetconfig)
		return M_ResetConfig_MouseInSearchBox();

	return M_TextEntry();
}

#if defined(_WIN32) // woods #disablecaps via ironwail
qboolean M_KeyBinding(void)
{
	return key_dest == key_menu && m_state == m_keys && bind_grab;
}
#endif

void M_ConfigureNetSubsystem(void)
{
// enable/disable net systems to match desired config
	Cbuf_AddText ("stopdemo\n");

	if (/*IPXConfig || */TCPIPConfig) // woods #skipipx
		net_hostport = lanConfig_port;
}

//=============================================================================

static qboolean M_CheckCustomGfx(const char* custompath, const char* basepath, int knownlength, const unsigned int* hashes, int numhashes) // woods (iw)
{
	unsigned int id_custom, id_base;
	int h, length;
	qboolean ret = false;

	if (!COM_FileExists(custompath, &id_custom))
		return false;

	length = COM_OpenFile(basepath, &h, &id_base);
	if (id_custom >= id_base)
		ret = true;
	else if (length == knownlength)
	{
		int mark = Hunk_LowMark();
		byte* data = (byte*)Hunk_Alloc(length);
		if (length == Sys_FileRead(h, data, length))
		{
			unsigned int hash = COM_HashBlock(data, length);
			while (numhashes-- > 0 && !ret)
				if (hash == *hashes++)
					ret = true;
		}
		Hunk_FreeToLowMark(mark);
	}

	COM_CloseFile(h);

	return ret;
}

void M_CheckMods(void) // woods #modsmenu (iw)
{
	const unsigned int
		main_hashes[] = { 0x136bc7fd, 0x90555cb4 },
		sp_hashes[] = { 0x86a6f086 },
		sgl_hashes[] = { 0x7bba813d }
	;

	m_main_mods = M_CheckCustomGfx("gfx/menumods.lmp",
		"gfx/mainmenu.lmp", 26888, main_hashes, countof(main_hashes));

	m_main_demos = M_CheckCustomGfx("gfx/menudemos.lmp", // woods #demosmenu
		"gfx/mainmenu.lmp", 26888, main_hashes, countof(main_hashes));

	m_singleplayer_showlevels = M_CheckCustomGfx("gfx/sp_maps.lmp",
		"gfx/sp_menu.lmp", 14856, sp_hashes, countof(sp_hashes));

	m_skill_usegfx = M_CheckCustomGfx("gfx/skillmenu.lmp",
		"gfx/sp_menu.lmp", 14856, sp_hashes, countof(sp_hashes));

	m_skill_usecustomtitle = M_CheckCustomGfx("gfx/p_skill.lmp",
		"gfx/ttl_sgl.lmp", 6728, sgl_hashes, countof(sgl_hashes));
}

/*
==================
 Startup Menu
==================
*/

enum startup_e
{
	STARTUP_PAK_TOGGLE,
	STARTUP_PAK_LOADING,
	STARTUP_SCREEN,
	STARTUP_DEMO_ATTRACT,
	STARTUP_ITEMS
} startup_cursor;

static struct
{
	int cursor;
	struct {
		char text[32];
		int len;
	} search;
} startupmenu;

int numberOfStartupItems = STARTUP_ITEMS;

#define MAX_PAKS 256
#define MAX_PAK_NAME 64

typedef struct
{
	char name[MAX_PAK_NAME];
	qboolean enabled;
	qboolean readonly;  // true for id1 paks (not editable/reorderable)
	int source;         // 0 = base (id1), 1 = mod (sorted base first)
} menu_pak_t;

static struct
{
	menu_pak_t paks[MAX_PAKS];
	int num_paks;
	int cursor;
	int scroll;
	qboolean dragging;
	struct {
		char text[32];
		int len;
	} search;
} pakmenu;
static qboolean paklist_exists = false;
static qboolean pak_reorder_enabled = false;

// Forward declarations
void M_Menu_PakLoading_f(void);
void M_PakLoading_Draw(void);
void M_PakLoading_Key(int k);
void M_PakLoading_Mousemove(int cx, int cy);
void M_Startup_AdjustSliders(int dir);
void M_Startup_Mousemove(int cx, int cy);
void M_Startup_Draw(void);
void M_Startup_Key(int k);
static void M_Pak_SaveList(void);
static void M_Pak_DeleteList(void);
static void M_Pak_BuildList(void);

static const char* M_Startup_GetItemText(int index)
{
	static char buffer[64];

	switch (index)
	{
	case STARTUP_SCREEN:
		return "Start-up Screen";
	case STARTUP_DEMO_ATTRACT:
		return "Start Demo Attract";
	case STARTUP_PAK_LOADING:
		return "PAK Loading   ...";
	case STARTUP_PAK_TOGGLE:
		return "Use PAK Re-Ordering";
	default:
		q_snprintf(buffer, sizeof(buffer), "Unknown Item %d", index);
		return buffer;
	}
}

static void M_Startup_ClampCursor(void)
{
	int cursor = (int)startup_cursor;

	if (cursor < 0 || cursor >= STARTUP_ITEMS)
	{
		cursor %= STARTUP_ITEMS;
		if (cursor < 0)
			cursor += STARTUP_ITEMS;
		startup_cursor = (enum startup_e)cursor;
	}
}

static void M_Startup_MoveCursor(int delta)
{
	int cursor = (int)startup_cursor + delta;

	cursor %= STARTUP_ITEMS;
	if (cursor < 0)
		cursor += STARTUP_ITEMS;

	startup_cursor = (enum startup_e)cursor;
}

static void M_Startup_SearchUpdate(void)
{
	int i;
	if (startupmenu.search.len <= 0)
		return;

	for (i = 0; i < STARTUP_ITEMS; i++)
	{
		const char* text = M_Startup_GetItemText(i);
		if (q_strcasestr(text, startupmenu.search.text))
		{
			startup_cursor = (enum startup_e)i;
			M_Startup_ClampCursor();
			return;
		}
	}
}

// Startup Functions
void M_Menu_Startup_f(void)
{
	key_dest = key_menu;
	m_state = m_startup;
	m_entersound = true;
	startup_cursor = 0;
	startupmenu.cursor = 0;
	startupmenu.search.len = 0;
	startupmenu.search.text[0] = 0;
	numberOfStartupItems = STARTUP_ITEMS;

	/* Check current pak.lst presence to seed toggle state */
	{
		char listpath[MAX_OSPATH];
		FILE* f;
		q_snprintf(listpath, sizeof(listpath), "%s/pak.lst", com_gamedir);
		f = fopen(listpath, "rb");
		paklist_exists = (f != NULL);
		if (f) fclose(f);
		if (!pak_reorder_enabled)
			pak_reorder_enabled = paklist_exists;
	}

	M_Startup_ClampCursor();
	IN_UpdateGrabs();
}

void M_Startup_AdjustSliders(int dir)
{
	int m;
	S_LocalSound("misc/menu3.wav");

	switch (startup_cursor)
	{
	case STARTUP_PAK_TOGGLE:
		if (pak_reorder_enabled)
		{
			if (!SCR_ModalMessage("Disabling PAK re-ordering will\ndelete saved ordering (pak.lst)\n\nContinue? (^mn^m/^my^m)\n", 0.0f))
				break;
			pak_reorder_enabled = false;
			M_Pak_DeleteList();
		}
		else
		{
			pak_reorder_enabled = true;
		}
		break;
	case STARTUP_SCREEN:
		if (dir > 0) {
			if (!strcmp(cl_onload.string, "") || !strcmp(cl_onload.string, "menu"))
				Cvar_Set("cl_onload", "browser");
			else if (!strcmp(cl_onload.string, "browser"))
				Cvar_Set("cl_onload", "bookmarks");
			else if (!strcmp(cl_onload.string, "bookmarks"))
				Cvar_Set("cl_onload", "save");
			else if (!strcmp(cl_onload.string, "save"))
				Cvar_Set("cl_onload", "history");
			else if (!strcmp(cl_onload.string, "history"))
				Cvar_Set("cl_onload", "console");
			else if (!strcmp(cl_onload.string, "console"))
				Cvar_Set("cl_onload", "demo");
			else if (!strcmp(cl_onload.string, "demo"))
				Cvar_Set("cl_onload", "menu");
			else  
				Cvar_Set("cl_onload", "menu");
		}
		else {
			if (!strcmp(cl_onload.string, "") || !strcmp(cl_onload.string, "menu"))
				Cvar_Set("cl_onload", "demo");
			else if (!strcmp(cl_onload.string, "demo"))
				Cvar_Set("cl_onload", "console");
			else if (!strcmp(cl_onload.string, "console"))
				Cvar_Set("cl_onload", "history");
			else if (!strcmp(cl_onload.string, "history"))
				Cvar_Set("cl_onload", "save");
			else if (!strcmp(cl_onload.string, "save"))
				Cvar_Set("cl_onload", "bookmarks");
			else if (!strcmp(cl_onload.string, "bookmarks"))
				Cvar_Set("cl_onload", "browser");
			else if (!strcmp(cl_onload.string, "browser"))
				Cvar_Set("cl_onload", "menu");
			else  
				Cvar_Set("cl_onload", "menu");
		}
		break;
	case STARTUP_DEMO_ATTRACT:
		m = cl_demoreel.value + dir;
		if (m < 0)
			m = 2;
		else if (m > 2)
			m = 0;
		Cvar_SetValueQuick(&cl_demoreel, m);
		break;
	case STARTUP_PAK_LOADING:
		M_Menu_PakLoading_f();
		break;
	default:
		break;
	}
}

void M_Startup_Draw(void)
{
	qpic_t* p;
	enum startup_e i;

	M_Startup_ClampCursor();

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	const char* title = "Startup Options";
	M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);

	for (i = 0; i < STARTUP_ITEMS; i++)
	{
		int y = 48 + 8 * i;
		const char* text = NULL;
		const char* value = NULL;

		switch (i)
		{
		case STARTUP_PAK_TOGGLE:
			text = "   PAK Re-Ordering";
			value = pak_reorder_enabled ? "on" : "off";
			break;
		case STARTUP_PAK_LOADING:
			text = " PAK Loading Order     ...";
			break;
		case STARTUP_SCREEN:
			text = "   Start-up Screen";
			if (!strcmp(cl_onload.string, "") || !strcmp(cl_onload.string, "menu"))
				value = "main menu";
			else if (!strcmp(cl_onload.string, "console"))
				value = "console";
			else if (!strcmp(cl_onload.string, "demo"))
				value = "demos";
			else if (!strcmp(cl_onload.string, "browser"))
				value = "server browser";
			else if (!strcmp(cl_onload.string, "bookmarks"))
				value = "bookmarks";
			else if (!strcmp(cl_onload.string, "save"))
				value = "load game";
			else if (!strcmp(cl_onload.string, "history"))
				value = "history";
			else
				value = "custom";
			break;
		case STARTUP_DEMO_ATTRACT:
			text = "Start Demo Attract";
			if (cl_demoreel.value > 1)
				value = "on";
			else if (cl_demoreel.value)
				value = "startup only";
			else
				value = "off";
			break;
		default:
			break;
		}

		if (text)
		{
			if (startupmenu.search.len > 0 &&
				q_strcasestr(text, startupmenu.search.text))
			{
				M_PrintHighlight(0, y, text,
					startupmenu.search.text,
					startupmenu.search.len);
			}
			else
			{
				M_Print(0, y, text);
			}
		}
		if (value)
			M_Print(183, y, value);
	}

	M_DrawCharacter(172, 48 + startup_cursor * 8, 12 + ((int)(realtime * 4) & 1));

	if (startupmenu.search.len > 0)
	{
		int box_x = 20;
		int box_y = 180;
		int cursor_x = box_x + 8 * startupmenu.search.len;
		M_DrawTextBox(box_x - 8, box_y - 8, 32, 1);
		M_PrintHighlight(box_x, box_y, startupmenu.search.text,
			startupmenu.search.text, startupmenu.search.len);

		{
			qboolean match = false;
			for (i = 0; i < STARTUP_ITEMS; i++)
			{
				const char* text = M_Startup_GetItemText(i);
				if (text && q_strcasestr(text, startupmenu.search.text))
				{
					match = true;
					break;
				}
			}
			if (match)
				M_DrawCharacter(cursor_x, box_y, 10 + ((int)(realtime * 4) & 1));
			else
				M_DrawCharacter(cursor_x, box_y, 11 ^ 128);
		}
	}
}

void M_Startup_Key(int k)
{
	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		M_Menu_Options_f();
		break;
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1:
		m_entersound = true;
		if (startup_cursor == STARTUP_PAK_LOADING)
		{
			M_Menu_PakLoading_f();
		}
		else
		{
			M_Startup_AdjustSliders(1);
		}
		break;
	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		M_Startup_MoveCursor(-1);
		break;
	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		M_Startup_MoveCursor(1);
		break;
	case K_LEFTARROW:
		M_Startup_AdjustSliders(-1);
		break;

	case K_MWHEELDOWN:
		if (startup_cursor != STARTUP_PAK_LOADING)
			M_Startup_AdjustSliders(-1);
		break;

	case K_RIGHTARROW:
		M_Startup_AdjustSliders(1);
		break;

	case K_MWHEELUP:
		if (startup_cursor != STARTUP_PAK_LOADING)
			M_Startup_AdjustSliders(1);
		break;
	case K_BACKSPACE:
		if (startupmenu.search.len > 0)
		{
			startupmenu.search.text[--startupmenu.search.len] = 0;
			M_Startup_SearchUpdate();
		}
		break;
	default:
		if (k >= 32 && k < 127 && startupmenu.search.len < (int)sizeof(startupmenu.search.text) - 1)
		{
			startupmenu.search.text[startupmenu.search.len++] = k;
			startupmenu.search.text[startupmenu.search.len] = 0;
			M_Startup_SearchUpdate();
		}
		break;
	}
}

void M_Startup_Mousemove(int cx, int cy)
{
	int item = (cy - 48) / 8;
	if (item >= 0 && item < STARTUP_ITEMS)
	{
		startup_cursor = item;
	}
	else
	{
		M_Startup_ClampCursor();
	}
}

/*
==================
Demo Options Menu
==================
*/

enum demooptions_e
{
	DEMOOPTIONS_DEMOEYES,
	DEMOOPTIONS_FORMAT,
	DEMOOPTIONS_AUTODEMO,
	DEMOOPTIONS_DEMOREEL,
	DEMOOPTIONS_EYECAM,
	DEMOOPTIONS_BAR_TIMEOUT,
	DEMOOPTIONS_MINFRAMES,
	DEMOOPTIONS_MINFRAMES_DELETE,
	DEMOOPTIONS_ITEMS
} demooptions_cursor;

static const int demoptions_minframes_presets[] = {0, 50, 100, 250, 500, 1000};

static int M_DemoOptions_MinFramesAbs(void)
{
	int cur = (int)cl_demo_minframes.value;
	return (cur < 0) ? -cur : cur;
}

static struct
{
	int cursor;
	struct {
		char text[32];
		int len;
	} search;
} demooptionsmenu;

static const char* M_DemoOptions_GetItemText(int index)
{
	static char buffer[64];

	switch (index)
	{
	case DEMOOPTIONS_DEMOEYES:
		return "Demo Eyes";
	case DEMOOPTIONS_FORMAT:
		return "Demo Format";
	case DEMOOPTIONS_AUTODEMO:
		return "Auto-record";
	case DEMOOPTIONS_DEMOREEL:
		return "Demo Reel";
	case DEMOOPTIONS_EYECAM:
		return "Demo Eyecam";
	case DEMOOPTIONS_BAR_TIMEOUT:
		return "Demo Bar Timeout";
	case DEMOOPTIONS_MINFRAMES:
		return "Demo Min Frames";
	case DEMOOPTIONS_MINFRAMES_DELETE:
		return "Hide Short Demos";
	default:
		q_snprintf(buffer, sizeof(buffer), "Unknown Item %d", index);
		return buffer;
	}
}

static const char* M_DemoOptions_GetValueText(int index)
{
	static char buffer[64];

	switch (index)
	{
	case DEMOOPTIONS_FORMAT:
		if (!cl_demo_format.string[0] || !q_strcasecmp(cl_demo_format.string, "dem"))
			return "dem (original)";
		if (!q_strcasecmp(cl_demo_format.string, "dz"))
			return "dz (dzip)";
		return cl_demo_format.string;
	case DEMOOPTIONS_AUTODEMO:
		switch ((int)cl_autodemo.value)
		{
		case 0: return "off";
		case 1: return "every map";
		case 2: return "crmod/crctf";
		case 3: return "online only";
		case 4: return "split by map";
		default:
			q_snprintf(buffer, sizeof(buffer), "%d", (int)cl_autodemo.value);
			return buffer;
		}
	case DEMOOPTIONS_DEMOREEL:
		if (cl_demoreel.value > 1)
			return "on";
		if (cl_demoreel.value > 0)
			return "startup only";
		return "off";
	case DEMOOPTIONS_EYECAM:
		return cl_demo_eyecam.value ? "on" : "off";
	case DEMOOPTIONS_BAR_TIMEOUT:
		if (scr_demobar_timeout.value < 0.0f)
			q_snprintf(buffer, sizeof(buffer), "hidden (%.1f)", scr_demobar_timeout.value);
		else if (scr_demobar_timeout.value == 0.0f)
			q_snprintf(buffer, sizeof(buffer), "always (%.1f)", scr_demobar_timeout.value);
		else if ((float)Q_rint(scr_demobar_timeout.value) == scr_demobar_timeout.value)
			q_snprintf(buffer, sizeof(buffer), "%d (%s)",
				Q_rint(scr_demobar_timeout.value),
				(Q_rint(scr_demobar_timeout.value) == 1) ? "second" : "seconds");
		else
			q_snprintf(buffer, sizeof(buffer), "%.1f (seconds)", scr_demobar_timeout.value);
		return buffer;
	case DEMOOPTIONS_MINFRAMES:
	{
		int cur = M_DemoOptions_MinFramesAbs();
		if (cur <= 0)
			return "off (show all)";
		q_snprintf(buffer, sizeof(buffer), "%d frames", cur);
		return buffer;
	}
	case DEMOOPTIONS_MINFRAMES_DELETE:
		if (M_DemoOptions_MinFramesAbs() <= 0)
			return "n/a";
		return (cl_demo_minframes.value < 0.0f) ? "hide & delete" : "hide only";
	default:
		return "";
	}
}

static void M_DemoOptions_ClampCursor(void)
{
	int cursor = (int)demooptions_cursor;

	if (cursor < 0 || cursor >= DEMOOPTIONS_ITEMS)
	{
		cursor %= DEMOOPTIONS_ITEMS;
		if (cursor < 0)
			cursor += DEMOOPTIONS_ITEMS;
		demooptions_cursor = (enum demooptions_e)cursor;
	}
}

static void M_DemoOptions_MoveCursor(int delta)
{
	int cursor = (int)demooptions_cursor + delta;

	cursor %= DEMOOPTIONS_ITEMS;
	if (cursor < 0)
		cursor += DEMOOPTIONS_ITEMS;

	demooptions_cursor = (enum demooptions_e)cursor;
}

static void M_DemoOptions_SearchUpdate(void)
{
	int i;

	if (demooptionsmenu.search.len <= 0)
		return;

	for (i = 0; i < DEMOOPTIONS_ITEMS; i++)
	{
		const char* text = M_DemoOptions_GetItemText(i);
		if (q_strcasestr(text, demooptionsmenu.search.text))
		{
			demooptions_cursor = (enum demooptions_e)i;
			M_DemoOptions_ClampCursor();
			return;
		}
	}
}

static int M_DemoOptions_RowY(int item)
{
	return 48 + item * 8;
}

static int M_DemoOptions_LivePreviewId(void)
{
	switch (demooptions_cursor)
	{
	case DEMOOPTIONS_BAR_TIMEOUT:
		return cls.demoplayback ? LP_DEMOBAR : LP_NONE;
	default:
		return LP_NONE;
	}
}

static void M_DemoOptions_CycleDemoFormat(int dir)
{
#ifdef USE_ZLIB
	static const char* formats[] = {"dem", "dz"};
	const int count = (int)(sizeof(formats) / sizeof(formats[0]));
	int index = 0;
	int i;

	for (i = 0; i < count; i++)
	{
		if (!q_strcasecmp(cl_demo_format.string, formats[i]))
		{
			index = i;
			break;
		}
	}

	index += (dir > 0) ? 1 : -1;
	if (index < 0)
		index = count - 1;
	else if (index >= count)
		index = 0;

	Cvar_Set("cl_demo_format", formats[index]);
#else
	(void)dir;
	Cvar_Set("cl_demo_format", "dem");
#endif
}

void M_Menu_DemoOptions_f(void)
{
	key_dest = key_menu;
	m_state = m_demooptions;
	m_entersound = true;
	demooptions_cursor = 0;
	demooptionsmenu.cursor = 0;
	demooptionsmenu.search.len = 0;
	demooptionsmenu.search.text[0] = 0;
	demooptions_slider_grab = false;
	M_LivePreview_Reset();

	M_DemoOptions_ClampCursor();
	IN_UpdateGrabs();
}

static void M_DemoOptions_AdjustSliders(int dir)
{
	float f;
	int m;

	S_LocalSound("misc/menu3.wav");

	switch (demooptions_cursor)
	{
	case DEMOOPTIONS_DEMOEYES:
		f = CLAMP(0.0f, cl_demoeyes.value + dir * 0.1f, 1.0f);
		f = Q_rint(f * 10.0f) / 10.0f;
		if (f < 0.05f)
			f = 0.0f;
		Cvar_SetValueQuick(&cl_demoeyes, f);
		break;
	case DEMOOPTIONS_FORMAT:
		M_DemoOptions_CycleDemoFormat(dir);
		break;
	case DEMOOPTIONS_AUTODEMO:
		m = (int)cl_autodemo.value + dir;
		if (m < 0)
			m = 4;
		else if (m > 4)
			m = 0;
		Cvar_SetValue("cl_autodemo", m);
		break;
	case DEMOOPTIONS_DEMOREEL:
		m = (int)cl_demoreel.value + dir;
		if (m < 0)
			m = 2;
		else if (m > 2)
			m = 0;
		Cvar_SetValueQuick(&cl_demoreel, m);
		break;
	case DEMOOPTIONS_EYECAM:
		Cvar_SetValueQuick(&cl_demo_eyecam, cl_demo_eyecam.value ? 0 : 1);
		break;
	case DEMOOPTIONS_BAR_TIMEOUT:
		if (scr_demobar_timeout.value < 0.0f)
			f = (dir > 0) ? 0.0f : 10.0f;
		else if (dir > 0)
			f = (scr_demobar_timeout.value >= 10.0f) ? -1.0f : (float)Q_rint(scr_demobar_timeout.value) + 1.0f;
		else if (scr_demobar_timeout.value <= 0.0f)
			f = -1.0f;
		else
			f = (scr_demobar_timeout.value <= 1.0f) ? 0.0f : (float)Q_rint(scr_demobar_timeout.value) - 1.0f;
		Cvar_SetValueQuick(&scr_demobar_timeout, f);
		M_LivePreview_WantAndKick (M_DemoOptions_LivePreviewId (), M_DemoOptions_RowY (demooptions_cursor));
		break;
	case DEMOOPTIONS_MINFRAMES:
	{
		const int count = (int)(sizeof(demoptions_minframes_presets) / sizeof(demoptions_minframes_presets[0]));
		qboolean del = cl_demo_minframes.value < 0.0f;
		int cur = M_DemoOptions_MinFramesAbs();
		int idx, mag;

		for (idx = 0; idx < count; idx++)
			if (demoptions_minframes_presets[idx] >= cur)
				break;
		if (idx >= count)
			idx = count - 1;

		idx += (dir > 0) ? 1 : -1;
		if (idx < 0)
			idx = count - 1;
		else if (idx >= count)
			idx = 0;

		mag = demoptions_minframes_presets[idx];
		Cvar_SetValueQuick(&cl_demo_minframes, (del && mag > 0) ? -mag : mag);
		break;
	}
	case DEMOOPTIONS_MINFRAMES_DELETE:
		if (M_DemoOptions_MinFramesAbs() > 0)
			Cvar_SetValueQuick(&cl_demo_minframes, -cl_demo_minframes.value);
		break;
	default:
		break;
	}
}

void M_DemoOptions_Draw(void)
{
	qpic_t* p;
	float r;
	enum demooptions_e i;

	M_DemoOptions_ClampCursor();

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	{
		const char* title = "Demo Options";
		M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);
	}

	M_LivePreview_WantAt (M_DemoOptions_LivePreviewId (), M_DemoOptions_RowY (demooptions_cursor));

	for (i = 0; i < DEMOOPTIONS_ITEMS; i++)
	{
		int y = M_DemoOptions_RowY (i);
		qboolean isolated = M_LivePreview_IsolateY (y);
		const char* text = NULL;
		const char* value = NULL;

		if (isolated)
			M_LivePreview_BeginIsolate ();

		switch (i)
		{
		case DEMOOPTIONS_DEMOEYES:
			text = "         Demo Eyes";
			r = CLAMP(0.0f, cl_demoeyes.value, 1.0f);
			M_DrawSlider(186, y, r, r, "%.1f");
			break;
		case DEMOOPTIONS_FORMAT:
			text = "       Demo Format";
			value = M_DemoOptions_GetValueText(i);
			break;
		case DEMOOPTIONS_AUTODEMO:
			text = "       Auto-record";
			value = M_DemoOptions_GetValueText(i);
			break;
		case DEMOOPTIONS_DEMOREEL:
			text = "         Demo Reel";
			value = M_DemoOptions_GetValueText(i);
			break;
		case DEMOOPTIONS_EYECAM:
			text = "       Demo Eyecam";
			value = M_DemoOptions_GetValueText(i);
			break;
		case DEMOOPTIONS_BAR_TIMEOUT:
			text = "  Demo Bar Timeout";
			value = M_DemoOptions_GetValueText(i);
			break;
		case DEMOOPTIONS_MINFRAMES:
			text = "   Demo Min Frames";
			value = M_DemoOptions_GetValueText(i);
			break;
		case DEMOOPTIONS_MINFRAMES_DELETE:
			text = "  Hide Short Demos";
			value = M_DemoOptions_GetValueText(i);
			break;
		default:
			break;
		}

		if (text)
		{
			if (demooptionsmenu.search.len > 0 &&
				q_strcasestr(text, demooptionsmenu.search.text))
			{
				M_PrintHighlight(0, y, text,
					demooptionsmenu.search.text,
					demooptionsmenu.search.len);
			}
			else
			{
				M_Print(0, y, text);
			}
		}
		if (value)
			M_Print(183, y, value);

		if (isolated)
			M_LivePreview_EndIsolate ();
	}

	{
		int y = M_DemoOptions_RowY (demooptions_cursor);
		qboolean isolated = M_LivePreview_IsolateY (y);
		if (isolated)
			M_LivePreview_BeginIsolate ();
		M_DrawCharacter(172, y, 12 + ((int)(realtime * 4) & 1));
		if (isolated)
			M_LivePreview_EndIsolate ();
	}

	if (demooptionsmenu.search.len > 0)
	{
		int box_x = 20;
		int box_y = 180;
		int cursor_x = box_x + 8 * demooptionsmenu.search.len;
		M_DrawTextBox(box_x - 8, box_y - 8, 32, 1);
		M_PrintHighlight(box_x, box_y, demooptionsmenu.search.text,
			demooptionsmenu.search.text, demooptionsmenu.search.len);

		{
			qboolean match = false;
			for (i = 0; i < DEMOOPTIONS_ITEMS; i++)
			{
				const char* text = M_DemoOptions_GetItemText(i);
				if (text && q_strcasestr(text, demooptionsmenu.search.text))
				{
					match = true;
					break;
				}
			}
			if (match)
				M_DrawCharacter(cursor_x, box_y, 10 + ((int)(realtime * 4) & 1));
			else
				M_DrawCharacter(cursor_x, box_y, 11 ^ 128);
		}
	}
}

void M_DemoOptions_Key(int k)
{
	if (!keydown[K_MOUSE1])
		demooptions_slider_grab = false;

	if (demooptions_slider_grab)
	{
		switch (k)
		{
		case K_ESCAPE:
		case K_BBUTTON:
		case K_MOUSE4:
		case K_MOUSE2:
			demooptions_slider_grab = false;
			break;
		}
		return;
	}

	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		if (M_LivePreview_Alpha() > 0.f)
		{
			M_LivePreview_Reset();
			return;
		}
		M_Menu_Options_f();
		break;
	case K_MOUSE1:
		m_entersound = true;
		if (m_mousey >= M_DemoOptions_RowY (0) && m_mousey < M_DemoOptions_RowY (DEMOOPTIONS_ITEMS))
		{
			demooptions_cursor = (m_mousey - M_DemoOptions_RowY (0)) / 8;
			if (demooptions_cursor == DEMOOPTIONS_DEMOEYES)
				demooptions_slider_grab = true;
			else
				M_DemoOptions_AdjustSliders(1);
		}
		break;
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		M_DemoOptions_AdjustSliders(1);
		break;
	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		M_DemoOptions_MoveCursor(-1);
		break;
	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		M_DemoOptions_MoveCursor(1);
		break;
	case K_LEFTARROW:
	case K_MWHEELDOWN:
		M_DemoOptions_AdjustSliders(-1);
		break;
	case K_RIGHTARROW:
	case K_MWHEELUP:
		M_DemoOptions_AdjustSliders(1);
		break;
	case K_BACKSPACE:
		if (demooptionsmenu.search.len > 0)
		{
			demooptionsmenu.search.text[--demooptionsmenu.search.len] = 0;
			M_DemoOptions_SearchUpdate();
		}
		break;
	default:
		if (k >= 32 && k < 127 && demooptionsmenu.search.len < (int)sizeof(demooptionsmenu.search.text) - 1)
		{
			demooptionsmenu.search.text[demooptionsmenu.search.len++] = k;
			demooptionsmenu.search.text[demooptionsmenu.search.len] = 0;
			M_DemoOptions_SearchUpdate();
		}
		break;
	}
}

void M_DemoOptions_Mousemove(int cx, int cy)
{
	if (demooptions_slider_grab)
	{
		float f;

		if (!keydown[K_MOUSE1])
		{
			demooptions_slider_grab = false;
			return;
		}

		switch (demooptions_cursor)
		{
		case DEMOOPTIONS_DEMOEYES:
			f = CLAMP(0.0f, M_MouseToSliderFraction(cx - 187), 1.0f);
			f = Q_rint(f * 10.0f) / 10.0f;
			if (f < 0.05f)
				f = 0.0f;
			Cvar_SetValue("cl_demoeyes", f);
			break;
		default:
			break;
		}
		return;
	}

	int item = (cy - M_DemoOptions_RowY (0)) / 8;
	if (item >= 0 && item < DEMOOPTIONS_ITEMS)
	{
		demooptions_cursor = item;
	}
	else
	{
		M_DemoOptions_ClampCursor();
	}
}

// Helper to add unique pak
static void M_Pak_Add(const char* name, qboolean readonly, int source)
{
	int i;
	if (pakmenu.num_paks >= MAX_PAKS)
		return;

	// Check duplicates
	for (i = 0; i < pakmenu.num_paks; i++)
	{
		if (!q_strcasecmp(pakmenu.paks[i].name, name))
			return;
	}

	// Add new
	q_strlcpy(pakmenu.paks[pakmenu.num_paks].name, name, MAX_PAK_NAME);
	pakmenu.paks[pakmenu.num_paks].enabled = true;
	pakmenu.paks[pakmenu.num_paks].readonly = readonly;
	pakmenu.paks[pakmenu.num_paks].source = source;
	pakmenu.num_paks++;
}

// Callback for COM_ListAllFiles
// Callback for COM_ListAllFiles
static qboolean M_Pak_ScanCallback(void *ctx, const char *fname, time_t mtime, size_t fsize, searchpath_t *spath)
{
	char id1path[MAX_OSPATH];
	qboolean is_id1;
	qboolean in_id1_gamedir;
	const char *gamedir_name;
	const char *ext = COM_FileGetExtension(fname);
	const char *pakname = fname;
	if (!ext) return true;
	
	if (q_strcasecmp(ext, "pak") && q_strcasecmp(ext, "pk3"))
		return true;

	if (!spath)
		return true;

	// Build the id1 path for comparison
	q_snprintf(id1path, sizeof(id1path), "%s/id1", com_basedir);
	
	// Determine if this pak is from id1
	is_id1 = (q_strcasecmp(spath->filename, id1path) == 0);
	
	// Check if we're running in id1 gamedir (compare just the directory name)
	gamedir_name = COM_SkipPath(com_gamedir);
	in_id1_gamedir = (q_strcasecmp(gamedir_name, "id1") == 0);

	if (!q_strncasecmp(fname, "paks/", 5) || !q_strncasecmp(fname, "paks\\", 5))
		pakname = fname + 5;
	
	// If in id1 gamedir, only show paks from id1 (no base/mod distinction)
	if (in_id1_gamedir)
	{
		// Running id1 only - only accept id1 paks, and filter engine paks
		if (!is_id1)
			return true;
		
		// Filter pak0/pak1 and engine paks (pinned at top) - ALWAYS in id1
		if (!q_strncasecmp(pakname, "pak0.", 5) ||
			!q_strncasecmp(pakname, "pak1.", 5) ||
			!q_strncasecmp(pakname, "quakespasm.", 11) ||
			!q_strncasecmp(pakname, "qssm.", 5))
			return true;
		
		M_Pak_Add(pakname, false, 1); // Not readonly when in id1
	}
	else
	{
		// Running in a mod - show both id1 and mod paks
		if (is_id1)
		{
			// id1 pak - filter pak0/pak1 (pinned at top), but add others as readonly
			if (!q_strncasecmp(pakname, "pak0.", 5) ||
				!q_strncasecmp(pakname, "pak1.", 5) ||
				!q_strncasecmp(pakname, "quakespasm.", 11) ||
				!q_strncasecmp(pakname, "qssm.", 5))
				return true;
			
			M_Pak_Add(pakname, true, 0); // Base (id1) = source 0, readonly
		}
		else if (strcmp(spath->filename, com_gamedir) == 0)
		{
			// Mod pak - filter engine paks only, allow pak0/pak1/pak2 etc
			if (!q_strncasecmp(pakname, "quakespasm.", 11) ||
				!q_strncasecmp(pakname, "qssm.", 5))
				return true;
			
			M_Pak_Add(pakname, false, 1); // Mod = source 1, editable
		}
		// Ignore paks from other directories
	}
	
	return true;
}
static int M_Pak_Compare(const void* a, const void* b)
{
	const menu_pak_t* pa = (const menu_pak_t*)a;
	const menu_pak_t* pb = (const menu_pak_t*)b;
	// Sort by source first (base=0 before mod=1), then by name
	if (pa->source != pb->source)
		return pa->source - pb->source;
	return q_strcasecmp(pa->name, pb->name);
}
static void M_Pak_BuildList(void)
{
	char listpath[MAX_OSPATH];
	FILE *f;
	long len;
	char *buffer;
	const char *data;

	pakmenu.num_paks = 0;
	pakmenu.dragging = false;
	paklist_exists = false;

	q_snprintf(listpath, sizeof(listpath), "%s/pak.lst", com_gamedir);
	f = fopen(listpath, "rb");
	if (f)
	{
		paklist_exists = true;
		fseek(f, 0, SEEK_END);
		len = ftell(f);
		fseek(f, 0, SEEK_SET);
		buffer = (char*)Z_Malloc(len + 1);
		if (fread(buffer, 1, len, f) == (size_t)len)
		{
			fclose(f);
			buffer[len] = 0;

			data = buffer;
			while ((data = COM_Parse(data)))
			{
				if (!*com_token) continue;
				M_Pak_Add(com_token, false, 1);
			}
			Z_Free(buffer);
			{
				int i, count = 0;
				menu_pak_t temp[MAX_PAKS];

				COM_ListAllFiles(NULL, "*.pak", M_Pak_ScanCallback, 0, NULL);
				COM_ListAllFiles(NULL, "*.pk3", M_Pak_ScanCallback, 0, NULL);
				COM_ListAllFiles(NULL, "paks/*.pak", M_Pak_ScanCallback, 0, NULL);
				COM_ListAllFiles(NULL, "paks/*.pk3", M_Pak_ScanCallback, 0, NULL);

				/* Sort logic:
				   1. Base paks (source 0) - sorted by name
				   2. Mod paks (source 1) - preserve loaded order (from pak.lst), append new ones
				*/
				
				/* 1. Extract and sort base paks */
				for (i = 0; i < pakmenu.num_paks; i++)
					if (pakmenu.paks[i].source == 0)
						temp[count++] = pakmenu.paks[i];
				
				if (count > 0)
					qsort(temp, count, sizeof(menu_pak_t), M_Pak_Compare); /* source is same, sorts by name */
				
				/* 2. Append mod paks (preserve order) */
				for (i = 0; i < pakmenu.num_paks; i++)
					if (pakmenu.paks[i].source == 1)
						temp[count++] = pakmenu.paks[i];
				
				/* Apply back to main array */
				memcpy(pakmenu.paks, temp, count * sizeof(menu_pak_t));
				pakmenu.num_paks = count;
			}
			pak_reorder_enabled = true;
			pakmenu.search.len = 0;
			pakmenu.search.text[0] = 0;
			return;
		}
		Z_Free(buffer);
		fclose(f);
		paklist_exists = false;
	}

	// Scan for all paks in current gamedir (callback filters by gamedir)
	COM_ListAllFiles(NULL, "*.pak", M_Pak_ScanCallback, 0, NULL);
	COM_ListAllFiles(NULL, "*.pk3", M_Pak_ScanCallback, 0, NULL);
	COM_ListAllFiles(NULL, "paks/*.pak", M_Pak_ScanCallback, 0, NULL);
	COM_ListAllFiles(NULL, "paks/*.pk3", M_Pak_ScanCallback, 0, NULL);
	if (pakmenu.num_paks > 1)
		qsort(pakmenu.paks, pakmenu.num_paks, sizeof(menu_pak_t), M_Pak_Compare);

	pakmenu.search.len = 0;
	pakmenu.search.text[0] = 0;
}

static void M_Pak_DeleteList(void)
{
	char listpath[MAX_OSPATH];

	q_snprintf(listpath, sizeof(listpath), "%s/pak.lst", com_gamedir);
	if (!remove(listpath))
		Con_Printf("pak.lst deleted. Using default load order.\n");
	paklist_exists = false;
	pak_reorder_enabled = false;
}

static void M_Pak_SaveList(void)
{
	if (!pak_reorder_enabled)
		return;

	char listpath[MAX_OSPATH];
	FILE *f;
	int i;

	q_snprintf(listpath, sizeof(listpath), "%s/pak.lst", com_gamedir);
	f = fopen(listpath, "w");
	if (!f) return;

	fprintf(f, "// Generated by PAK Loading Menu\n");
	for (i = 0; i < pakmenu.num_paks; i++)
	{
		/* Only save mod paks, skip readonly (id1) paks */
		if (!pakmenu.paks[i].readonly)
			fprintf(f, "%s\n", pakmenu.paks[i].name);
	}
	fclose(f);
	paklist_exists = true;
	
	Con_Printf("pak.lst saved. Restart game to apply changes.\n");
}

void M_PakLoading_Draw(void)
{
	int i;
	int x = 16;
	int y = 32;
	const int visible_items = 13;
	const int cols = 36;
	static const char* enginepacknames[] = { "quakespasm.pak", "qssm.pak" };
	const char* pinned[] = {
		"pak0.pak, pak1.pak (always loaded)",
		enginepacknames[0],
		enginepacknames[1]
	};
	const int pinned_count = (int)countof(pinned);
	const int pinned_offset = pinned_count + 1; /* spacer line after pinned items */
	const int list_y = y + pinned_offset * 8;
	plcolour_t white = CL_PLColours_Parse("0xffffff");
	int overflow_line_y = list_y - 8;

	{
		const char *gdir = COM_SkipPath(com_gamedir);
		int title_width = strlen("PAK Loading Order (") * 8;
		Draw_String(x, y - 28, "PAK Loading Order (");
		M_Print(x + title_width, y - 28, gdir);
		Draw_String(x + title_width + strlen(gdir) * 8, y - 28, ")");
	}
	M_DrawQuakeBar(x - 8, y - 16, cols + 2);

	if (pakmenu.cursor < pakmenu.scroll) pakmenu.scroll = pakmenu.cursor;
	if (pakmenu.cursor >= pakmenu.scroll + visible_items) pakmenu.scroll = pakmenu.cursor - visible_items + 1;

	if (pakmenu.scroll < 0) pakmenu.scroll = 0;
	if (pakmenu.scroll > q_max(0, pakmenu.num_paks - visible_items))
		pakmenu.scroll = q_max(0, pakmenu.num_paks - visible_items);

	/* Always show base/engine paks at top (not reorderable) */
	for (i = 0; i < pinned_count; i++)
		M_PrintRGBA(x, y + i * 8, pinned[i], white, 0.5f, false);
	/* spacer line after pinned */
	M_Print(x, y + pinned_count * 8, " ");

	for (i = 0; i < visible_items; i++)
	{
		int idx = pakmenu.scroll + i;
		if (idx >= pakmenu.num_paks) break;

		if (idx == pakmenu.cursor)
		{
			if (pakmenu.dragging)
				M_DrawCharacter(x - 8, list_y + i * 8, 141); 
			else
				M_DrawCharacter(x - 8, list_y + i * 8, 12 + ((int)(realtime * 4) & 1));
		}
		
			if (pakmenu.dragging && idx == pakmenu.cursor)
			{
				M_PrintWhite(x, list_y + i * 8, ">>");
				/* draw name after marker with optional highlight */
				if (pakmenu.search.len > 0 &&
					q_strcasestr(pakmenu.paks[idx].name, pakmenu.search.text))
				{
					M_PrintHighlight(x + 16, list_y + i * 8,
						pakmenu.paks[idx].name,
						pakmenu.search.text,
						pakmenu.search.len);
				}
				else if (pakmenu.paks[idx].readonly)
				{
					M_PrintRGBA(x + 16, list_y + i * 8, pakmenu.paks[idx].name, white, 0.5f, false);
				}
				else
				{
					M_Print(x + 16, list_y + i * 8,
						pakmenu.paks[idx].name);
				}
			}
			else
			{
				if (pakmenu.search.len > 0 &&
					q_strcasestr(pakmenu.paks[idx].name, pakmenu.search.text))
				{
					M_PrintHighlight(x, list_y + i * 8,
						pakmenu.paks[idx].name,
						pakmenu.search.text,
						pakmenu.search.len);
				}
				else if (pakmenu.paks[idx].readonly)
				{
					M_PrintRGBA(x, list_y + i * 8, pakmenu.paks[idx].name, white, 0.5f, false);
				}
				else
				{
					M_Print(x, list_y + i * 8, pakmenu.paks[idx].name);
				}
			}
	}
	
	if (pakmenu.num_paks > visible_items)
	{
		if (pakmenu.scroll > 0)
			M_DrawEllipsisBar(x, overflow_line_y, cols);
		if (pakmenu.scroll + visible_items < pakmenu.num_paks)
			M_DrawEllipsisBar(x, list_y + visible_items * 8, cols);
	}

	if (pakmenu.search.len > 0)
	{
		int box_x = x;
		int box_y = list_y + visible_items * 8 + 16;
		int cursor_x = box_x + 8 * pakmenu.search.len;
		M_DrawTextBox(box_x - 8, box_y - 8, 32, 1);
		M_PrintWhite(box_x, box_y, pakmenu.search.text);

		/* cursor color matches other menus: blink if match exists, solid inverted if no match */
		{
			qboolean match = false;
			for (i = 0; i < pakmenu.num_paks; i++)
			{
				if (q_strcasestr(pakmenu.paks[i].name, pakmenu.search.text))
				{
					match = true;
					break;
				}
			}
			if (match)
				M_DrawCharacter(cursor_x, box_y, 10 + ((int)(realtime * 4) & 1));
			else
				M_DrawCharacter(cursor_x, box_y, 11 ^ 128);
		}
	}

	if (pakmenu.search.len == 0)
	{
		if (!pak_reorder_enabled)
			M_PrintWhite(16, 180, "Re-ordering disabled");
		else if (pakmenu.dragging)
			M_PrintWhite(16, 180, "Arrows: Move  Enter/Space: Drop");
		else
			M_PrintWhite(16, 180, "Enter/Space: Grab  Arrows: Move");
	}
}

void M_PakLoading_Key(int k)
{
	qboolean disabled = !pak_reorder_enabled;

	if (k == K_ESCAPE || k == K_MOUSE2 || k == K_MOUSE4 || k == K_BBUTTON)
	{
		if (pakmenu.dragging)
		{
			pakmenu.dragging = false;
			S_LocalSound("misc/menu1.wav");
			return;
		}
		M_Pak_SaveList();
		M_Menu_Startup_f(); 
		return;
	}

	if (k == K_BACKSPACE)
	{
		if (pakmenu.search.len > 0)
		{
			pakmenu.search.text[--pakmenu.search.len] = 0;
			if (pakmenu.cursor >= pakmenu.num_paks)
				pakmenu.cursor = pakmenu.num_paks - 1;
			if (pakmenu.cursor < pakmenu.scroll)
				pakmenu.scroll = pakmenu.cursor;
		}
		return;
	}

	if (k >= 32 && k < 127 && pakmenu.search.len < (int)sizeof(pakmenu.search.text) - 1)
	{
		int i;
		pakmenu.search.text[pakmenu.search.len++] = k;
		pakmenu.search.text[pakmenu.search.len] = 0;

		for (i = 0; i < pakmenu.num_paks; i++)
		{
			if (q_strcasestr(pakmenu.paks[i].name, pakmenu.search.text))
			{
				pakmenu.cursor = i;
				if (pakmenu.cursor < pakmenu.scroll)
					pakmenu.scroll = pakmenu.cursor;
				break;
			}
		}
		return;
	}

	if (disabled)
	{
		if (k == K_UPARROW || k == K_MWHEELUP)
		{
			if (pakmenu.cursor > 0)
			{
				pakmenu.cursor--;
				S_LocalSound("misc/menu1.wav");
			}
		}
		else if (k == K_DOWNARROW || k == K_MWHEELDOWN)
		{
			if (pakmenu.cursor < pakmenu.num_paks - 1)
			{
				pakmenu.cursor++;
				S_LocalSound("misc/menu1.wav");
			}
		}
		return;
	}

	if (k == K_ENTER || k == K_KP_ENTER || k == K_SPACE || k == K_CTRL || k == K_SHIFT || k == K_MOUSE1)
	{
		/* Cannot drag readonly items */
		if (pakmenu.paks[pakmenu.cursor].readonly)
			return;
		pakmenu.dragging = !pakmenu.dragging;
		S_LocalSound("misc/menu2.wav");
		return;
	}

	if (k == K_UPARROW || k == K_MWHEELUP)
	{
		if (pakmenu.cursor > 0)
		{
			if (pakmenu.dragging)
			{
				/* Cannot swap with readonly items */
				if (pakmenu.paks[pakmenu.cursor - 1].readonly)
					return;
				menu_pak_t tmp = pakmenu.paks[pakmenu.cursor];
				pakmenu.paks[pakmenu.cursor] = pakmenu.paks[pakmenu.cursor - 1];
				pakmenu.paks[pakmenu.cursor - 1] = tmp;
				pakmenu.cursor--;
				S_LocalSound("misc/menu2.wav");
			}
			else
			{
				pakmenu.cursor--;
				S_LocalSound("misc/menu1.wav");
			}
		}
	}
	else if (k == K_DOWNARROW || k == K_MWHEELDOWN)
	{
		if (pakmenu.cursor < pakmenu.num_paks - 1)
		{
			if (pakmenu.dragging)
			{
				/* Cannot swap with readonly items */
				if (pakmenu.paks[pakmenu.cursor + 1].readonly)
					return;
				menu_pak_t tmp = pakmenu.paks[pakmenu.cursor];
				pakmenu.paks[pakmenu.cursor] = pakmenu.paks[pakmenu.cursor + 1];
				pakmenu.paks[pakmenu.cursor + 1] = tmp;
				pakmenu.cursor++;
				S_LocalSound("misc/menu2.wav");
			}
			else
			{
				pakmenu.cursor++;
				S_LocalSound("misc/menu1.wav");
			}
		}
	}
}

void M_PakLoading_Mousemove(int cx, int cy)
{
	const int pinned_count = 3; /* pak0+pak1, engine packs */
	const int pinned_offset = pinned_count + 1; /* spacer line */
	const int y_start = 32 + pinned_offset * 8;
	const int item_h = 8;
	const int visible_items = 13;
	
	int item_idx;
	
	if (cy < y_start) return;
	
	item_idx = (cy - y_start) / item_h;
	
	if (item_idx >= 0 && item_idx < visible_items)
	{
		int target_cursor = pakmenu.scroll + item_idx;
		if (target_cursor < pakmenu.num_paks)
		{
			pakmenu.cursor = target_cursor;
		}
	}
}

void M_Menu_PakLoading_f(void)
{
	key_dest = key_menu;
	m_state = m_pakloading;
	m_entersound = true;

	M_Pak_BuildList();
	pakmenu.cursor = 0;
	pakmenu.scroll = 0;
	pakmenu.dragging = false;
	pakmenu.search.len = 0;
	pakmenu.search.text[0] = 0;

	IN_UpdateGrabs();
}
