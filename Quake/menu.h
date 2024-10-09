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
	m_singleplayer,
	m_load,
	m_save,
	m_maps, // woods #mapsmenu (iw)
	m_skill, // woods #skillmenu (iw)
	m_multiplayer,
	m_setup,
	m_net,
	m_options,
	m_video,
	m_extras,
	m_keys,
	m_mods, // woods #modsmenu (iw)
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
	m_namemaker // woods #namemaker
};

extern enum m_state_e m_state;
extern enum m_state_e m_return_state;

extern qboolean m_entersound;

//
// menus
//
void M_Init (void);
void M_Keydown (int key);
void M_Charinput (int key);
void M_Mousemove(int x, int y); // woods #mousemenu (iw)
qboolean M_TextEntry (void);
#if defined(_WIN32) // woods #disablecaps via ironwail
qboolean M_KeyBinding(void);
#endif
void M_ToggleMenu (int mode);
void MQC_Shutdown(void);

void M_Menu_Main_f (void);
void M_Menu_Options_f (void);
void M_Menu_Quit_f (void);

void M_Print (int cx, int cy, const char *str);
void M_Print2 (int cx, int cy, const char* str); // woods #speed yellow numbers
void M_DrawCharacterRGBA (int cx, int line, int num, plcolour_t c, float alpha); // woods
void M_PrintRGBA (int cx, int cy, const char* str, plcolour_t c, float alpha); // woods
void M_PrintWhite (int cx, int cy, const char *str);

void M_Draw (void);
void M_DrawCharacter (int cx, int line, int num);

void M_DrawPic (int x, int y, qpic_t *pic);
void M_DrawSubpic (int x, int y, qpic_t* pic, int left, int top, int width, int height); // woods #modsmenu (iw)
void M_DrawTransPic (int x, int y, qpic_t *pic);
void M_DrawCheckbox (int x, int y, int on);
void M_DrawTextBox(int x, int y, int width, int lines); // woods (iw) #democontrols
void M_DrawTextBox_WithAlpha (int x, int y, int width, int lines, float alpha); // woods #centerprintbg (iw)

#endif	/* _QUAKE_MENU_H */

