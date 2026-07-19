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

#ifndef _QUAKE_MENU_H
#define _QUAKE_MENU_H

enum m_state_e {
	m_none,
	m_main,
	m_modmenu,
	m_singleplayer,
	m_load,
	m_save,
	m_maps, // woods #mapsmenu (iw)
	m_downloadmaps,
	m_skill, // woods #skillmenu (iw)
	m_multiplayer,
	m_setup,
	m_options,
	m_keys,
	m_mouse,
	m_controller,
	m_controller_test,
	m_weaponwheel,
	m_calibration,
	m_video,
	m_graphics,
	m_sky,
	m_skywind,
	m_sound,
	m_voip,
	m_game,
	m_playerxray,
	m_hud,
	m_crosshair,
	m_console,
	m_colorpicker,
	m_extras,
	m_saving,
	m_shortcuts,
	m_version,
	m_startup,
	m_demooptions,
	m_pakloading,
	m_modelviewer,
	m_audiobrowser,
	m_mods, // woods #modsmenu (iw)
	m_downloadmods,
	m_demos, // woods #demosmenu
	m_help,
	m_quit,
	m_lanconfig,
	m_gameoptions,
	m_search,
	m_slist,
	m_history,
	m_bookmarks, // woods #bookmarksmenu
	m_bookmarks_edit, // woods #bookmarksmenu
	m_namemaker, // woods #namemaker
	m_resetconfig
};

extern enum m_state_e m_state;
extern enum m_state_e m_return_state;

extern qboolean m_entersound;
extern qboolean crosshair_menu;

enum versiongithubstate_e {
	VERSIONGITHUB_IDLE,
	VERSIONGITHUB_LOADING,
	VERSIONGITHUB_READY,
	VERSIONGITHUB_ERROR
};

typedef struct
{
	int			state;
	int			comparison;
	char		version[64];
	char		detail[16];
	char		error[96];
} versionremoteinfo_t;

typedef enum
{
    VERSIONSECTION_APPLICATION,
    VERSIONSECTION_RENDERER,
    VERSIONSECTION_LIBRARIES,
    VERSIONSECTION_COUNT
} versionsection_t;

typedef void (*versionlocalcallback_t)(versionsection_t section,
    const char *label, const char *value, void *userdata);

//
// menus
//
void M_Init (void);
void M_Keydown (int key, qboolean repeat);
void M_Charinput (int key);
void M_Mousemove(int x, int y); // woods #mousemenu (iw)
qboolean M_TextEntry (void);
qboolean M_WantsIBeamCursor(void);
void M_MenuSearch_CloseForVideoRestart(void);
qboolean M_MenuSearch_UseGamepadBack(void);

#define VID_MENU_SEARCH_ITEMS 8
const char *VID_MenuSearch_GetItemText(int index);
cvar_t *VID_MenuSearch_GetItemCvar(int index);
const char *VID_MenuSearch_GetItemHintText(int index);
const char *VID_MenuSearch_GetValueText(int index);
qboolean VID_MenuSearch_ItemAvailable(int index);
void VID_MenuSearch_OpenItem(int index);
void VID_MenuSearch_LeaveMenu(void);

typedef struct
{
	char		*text;
	int			max_len;
	int			cursor;
	int			sel_start;
	qboolean	digits_only;
} menu_textfield_t;

void M_TextField_Init(menu_textfield_t *tf, char *buffer, int max_len, qboolean digits_only);
void M_TextField_ClampCursor(menu_textfield_t *tf);
void M_TextField_ClearSelection(menu_textfield_t *tf);
qboolean M_TextField_Key(menu_textfield_t *tf, int key);
qboolean M_TextField_Char(menu_textfield_t *tf, int key);
void M_TextField_MouseClick(menu_textfield_t *tf, int mouse_x, int text_x);
void M_TextField_MouseDrag(int mouse_x);
void M_TextField_CheckMouseRelease(void);
qboolean M_TextField_IsDraggingField(const menu_textfield_t *tf);
qboolean M_TextField_IsDraggingAny(void);
void M_TextField_DrawHighlight(menu_textfield_t *tf, int x, int y);
void M_TextField_DrawCursor(menu_textfield_t *tf, int x, int y);
void M_Version_StartGitHubFetch(void);
void M_Version_GetGitHubInfo(versionremoteinfo_t *release, versionremoteinfo_t *commit);
const char *M_Version_SectionName(versionsection_t section);
void M_Version_EnumerateLocal(versionlocalcallback_t callback, void *userdata);
void M_Version_FormatRemoteInfo(const versionremoteinfo_t *info, qboolean commit,
    const char *pending, char *out, size_t outsize);
void M_ServerList_ShutdownPingThreads(void);
void M_ServerList_ShutdownApiFetch(void);
qboolean M_Version_WaitForGitHubInfo(versionremoteinfo_t *release, versionremoteinfo_t *commit, Uint32 timeout_ms);
#if defined(_WIN32) // woods #disablecaps via ironwail
qboolean M_KeyBinding(void);
#endif
void M_ToggleMenu (int mode);
void MQC_Shutdown(void);

void M_Menu_Main_f (void);
void M_Menu_Options_f (void);
void M_Menu_Quit_f (void);
void M_Menu_Extras_f (void);
void M_Menu_AudioBrowser_f (void);
void M_AudioBrowser_Close (void);
void M_AudioBrowser_CloseIfInactive (void);
void M_AudioBrowser_Draw (void);
void M_AudioBrowser_Key (int key);
void M_AudioBrowser_Mousemove (int cx, int cy);
void M_AudioBrowser_MouseClick (int x, int y);

void M_Print (int cx, int cy, const char *str);
void M_Print2 (int cx, int cy, const char* str); // woods #speed yellow numbers
void M_DrawCharacterRGBA (int cx, int line, int num, plcolour_t c, float alpha); // woods
void M_PrintRGBA (int cx, int cy, const char* str, plcolour_t c, float alpha, qboolean mask); // woods
void M_PrintWhite (int cx, int cy, const char *str);
void M_PrintScroll(int x, int y, int maxwidth, const char* str, double time, qboolean color);

void M_Draw (void);
void M_DrawCharacter (int cx, int line, int num);
void M_DrawQuakeBar(int x, int y, int cols);

// Live preview (ported from Ironwail ui_live_preview).
// Previewing submenus track their selected previewable item internally, then
// call M_LivePreview_Kick() when that option changes. During the fade, M_Draw
// owns the gl_menu_alpha bracket; Draw_* helpers that reset GL color must
// restore to gl_menu_alpha rather than plain opaque white so the faded menu
// state survives across nested draws.
void M_LivePreview_Kick (void);
void M_LivePreview_Reset (void);
float M_LivePreview_Alpha (void);
qboolean M_WantsConsole (float *alpha);
qboolean M_LivePreview_UseConsoleHeight (void);
qboolean M_LivePreview_UseConsoleSpeed (void);
qboolean M_LivePreview_ConsoleSpeedOpen (void);
qboolean M_LivePreview_UseDamageTint (void);
qboolean M_LivePreview_UsePong (void);
qboolean M_LivePreview_UsePowerupShells (void);
qboolean M_LivePreview_UsePausedHints (void);
qboolean M_LivePreview_UseTypingStatus (void);
qboolean M_LivePreview_UseMatchScores (void);
qboolean M_LivePreview_UseSpeed (void);
qboolean M_LivePreview_UseScores (void);
qboolean M_LivePreview_UseMovementKeys (void);
// Wrap the previewed line's draws with these so it stays at full alpha
// while the rest of the menu fades during a live preview.
void M_LivePreview_BeginIsolate (void);
void M_LivePreview_EndIsolate (void);

void M_DrawPic (int x, int y, qpic_t *pic);
void M_DrawSubpic (int x, int y, qpic_t* pic, int left, int top, int width, int height); // woods #modsmenu (iw)
void M_DrawTransPic (int x, int y, qpic_t *pic);
void M_DrawCheckbox (int x, int y, int on);
void M_DrawTextBox(int x, int y, int width, int lines); // woods (iw) #democontrols
void M_DrawTextBox_WithAlpha (int x, int y, int width, int lines, float alpha); // woods #centerprintbg (iw)
void M_PrintHighlight(int x, int y, const char* str, const char* search, int searchlen); // woods #centerprintbg (iw)

#endif	/* _QUAKE_MENU_H */
