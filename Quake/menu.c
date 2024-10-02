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
#include <curl/curl.h> // woods #serversmenu
#include "json.h" // woods #serversmenu

void (*vid_menucmdfn)(void); //johnfitz
void (*vid_menudrawfn)(void);
void (*vid_menukeyfn)(int key);
void (*vid_menumousefn)(int cx, int cy); // woods #mousemenu

enum m_state_e m_state;
extern qboolean	keydown[256]; // woods #modsmenu (iw)
int m_mousex, m_mousey; // woods #mousemenu

const char* ResolveHostname(const char* hostname); // woods #serversmenu
extern qboolean Valid_IP(const char* ip_str); // woods #serversmenu
extern qboolean Valid_Domain(const char* domain_str); // woods #serversmenu

void M_Menu_Main_f (void);
	void M_Menu_SinglePlayer_f (void);
		void M_Menu_Load_f (void);
		void M_Menu_Save_f (void);
		void M_Menu_Maps_f(void);
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
		void M_Menu_Extras_f (void);
		void M_Menu_Video_f (void);
	void M_Menu_Mods_f(void); // woods #modsmenu (iw)
	void M_Menu_Demos_f (void); // woods #demosmenu
	void M_Menu_Help_f (void);
	void M_Menu_Quit_f (void);

void M_Main_Draw (void);
	void M_SinglePlayer_Draw (void);
		void M_Load_Draw (void);
		void M_Save_Draw (void);
		void M_Maps_Draw(void); // woods #modsmenu (iw)
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
		void M_Video_Draw (void);
	void M_Mods_Draw(void); // woods #modsmenu (iw)
	void M_Demos_Draw (void); // woods #demosmenu
	void M_Help_Draw (void);
	void M_Quit_Draw (void);

void M_Main_Key (int key);
	void M_SinglePlayer_Key (int key);
		void M_Load_Key (int key);
		void M_Save_Key (int key);
		void M_Maps_Key(int key);
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
	void M_Mods_Key(int key);
		void M_Video_Key (int key);
	void M_Help_Key (int key);
	void M_Quit_Key (int key);
	void M_NameMaker_Key(int key); // woods #namemaker

	// woods #mousemenu
	
	void M_Main_Mousemove(int cx, int cy);
	void M_SinglePlayer_Mousemove(int cx, int cy);
	void M_Load_Mousemove(int cx, int cy);
	void M_Save_Mousemove(int cx, int cy);
	void M_Maps_Mousemove(int cx, int cy);
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
	void M_Video_Mousemove (int cx, int cy);
	void M_Extras_Mousemove(int cx, int cy);
	//void M_Gamepad_Mousemove (int cx, int cy);
	void M_Mods_Mousemove(int cx, int cy);
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

void FileList_Subtract(const char* name, filelist_item_t** list); // woods #historymenu

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

void M_PrintRGBA (int cx, int cy, const char* str, plcolour_t c, float alpha) // woods
{
	while (*str)
	{
		M_DrawCharacterRGBA (cx, cy, (*str), c, alpha);
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

// TODO: smooth scrolling
void M_PrintScroll(int x, int y, int maxwidth, const char* str, double time, qboolean color) // woods #modsmenu (iw)
{
	int maxchars = maxwidth / 8;
	int len = strlen(str);
	int i, ofs;
	char mask = color ? 128 : 0;

	if (len <= maxchars)
	{
		if (color)
			M_Print(x, y, str);
		else
			M_PrintWhite(x, y, str);
		return;
	}

	ofs = (int)floor(time * 4.0);
	ofs %= len + 5;
	if (ofs < 0)
		ofs += len + 5;

	for (i = 0; i < maxchars; i++)
	{
		char c = (ofs < len) ? str[ofs] : " /// "[ofs - len];
		M_DrawCharacter(x, y, c ^ mask);
		x += 8;
		if (++ofs >= len + 5)
			ofs = 0;
	}
}

void M_PrintScroll2(int x, int y, int maxwidth, const char* str, const char* str2, double time) // woods
{
	int maxchars = maxwidth / 8;
	char masked_str[13];
	int len_str = strlen(str);
	int effective_len_str = len_str > 12 ? 12: len_str; // Determine the effective length after potential truncation

	for (int i = 0; i < effective_len_str; ++i) {
		masked_str[i] = str[i] ^ 128;
	}
	masked_str[effective_len_str] = '\0';

	int padding_width = max_word_length + 1; // Add one space after 'str'
	if (padding_width > 13) 
		padding_width = 13;

	char combined[MAX_CHAT_SIZE_EX];

	snprintf(combined, sizeof(combined), "%-*s%s", padding_width, masked_str, str2);

	int combined_len = strlen(combined);

	if (combined_len <= maxchars) 
	{
		M_PrintWhite(x, y, combined);
		return;
	}

	int ofs = (int)floor(time * 4.0) % (combined_len + 5);
	if (ofs < 0)
		ofs += combined_len + 5;

	for (int i = 0; i < maxchars; i++) {
		char c = (ofs < combined_len) ? combined[ofs] : " /// "[ofs - combined_len];
		M_DrawCharacter(x + (i * 8), y, c);
		if (++ofs >= combined_len + 5)
			ofs = 0;
	}
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

//=============================================================================

// woods iw menu functions #modsmenu #skillmenu #mapsmenu #mousemenu

/* Listbox */

qboolean mapshint; // woods

typedef struct
{
	int			cursor;
	int			numitems;
	int			viewsize;
	int			scroll;
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

//=============================================================================

int m_save_demonum;


//=============================================================================
/* MAIN MENU */

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

void M_Main_Key (int key) // woods #modsmenu #demosmenu (iw)
{
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

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		do {
			if (++m_main_cursor >= MAIN_ITEMS)
				m_main_cursor = 0;
		} while ((m_main_cursor == MAIN_MODS && !m_main_mods) || (m_main_cursor == MAIN_DEMOS && !m_main_demos));
		break;

	case K_UPARROW:
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

//=============================================================================
/* SINGLE PLAYER MENU */

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


void M_SinglePlayer_Key (int key)
{
	switch (key)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		M_Menu_Main_f ();
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		if (++m_singleplayer_cursor >= SINGLEPLAYER_ITEMS)
			m_singleplayer_cursor = 0;
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		if (--m_singleplayer_cursor < 0)
			m_singleplayer_cursor = SINGLEPLAYER_ITEMS - 1;
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1: // woods #mousemenu
		m_entersound = true;

		switch (m_singleplayer_cursor)
		{
		case 0:
			if (sv.active)
				if (!SCR_ModalMessage("Are you sure you want to\nstart a new game?\n", 0.0f))
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
	}
}

void M_SinglePlayer_Mousemove(int cx, int cy) // woods #mousemenu
{
	M_UpdateCursor(cy, 32, 20, SINGLEPLAYER_ITEMS, &m_singleplayer_cursor);
}

//=============================================================================
/* LOAD/SAVE MENU */

int		load_cursor;		// 0 < load_cursor < MAX_SAVEGAMES

#define	MAX_SAVEGAMES		20	/* johnfitz -- increased from 12 */
char	m_filenames[MAX_SAVEGAMES][SAVEGAME_COMMENT_LENGTH+1];
int		loadable[MAX_SAVEGAMES];

void M_ScanSaves (void)
{
	int	i, j;
	char	name[MAX_OSPATH];
	FILE	*f;
	int	version;

	for (i = 0; i < MAX_SAVEGAMES; i++)
	{
		strcpy (m_filenames[i], "--- UNUSED SLOT ---");
		loadable[i] = false;
		q_snprintf (name, sizeof(name), "%s/s%i.sav", com_gamedir, i);
		f = fopen (name, "r");
		if (!f)
			continue;
		fscanf (f, "%i\n", &version);
		fscanf (f, "%79s\n", name);
		q_strlcpy (m_filenames[i], name, SAVEGAME_COMMENT_LENGTH+1);

	// change _ back to space
		for (j = 0; j < SAVEGAME_COMMENT_LENGTH; j++)
		{
			if (m_filenames[i][j] == '_')
				m_filenames[i][j] = ' ';
		}
		loadable[i] = true;
		fclose (f);
	}
}

void M_Menu_Load_f (void)
{
	m_entersound = true;
	m_state = m_load;

	key_dest = key_menu;
	M_ScanSaves ();

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
	M_ScanSaves ();
}


void M_Load_Draw (void)
{
	int		i;
	qpic_t	*p;

	p = Draw_CachePic ("gfx/p_load.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);

	for (i = 0; i < MAX_SAVEGAMES; i++)
		M_Print (16, 32 + 8*i, m_filenames[i]);

// line cursor
	M_DrawCharacter (8, 32 + load_cursor*8, 12+((int)(realtime*4)&1));
}


void M_Save_Draw (void)
{
	int		i;
	qpic_t	*p;

	p = Draw_CachePic ("gfx/p_save.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);

	for (i = 0; i < MAX_SAVEGAMES; i++)
		M_Print (16, 32 + 8*i, m_filenames[i]);

// line cursor
	M_DrawCharacter (8, 32 + load_cursor*8, 12+((int)(realtime*4)&1));
}


void M_Load_Key (int k)
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
		S_LocalSound ("misc/menu2.wav");
		if (!loadable[load_cursor])
			return;
		m_state = m_none;
		key_dest = key_game;
		IN_UpdateGrabs();

	// Host_Loadgame_f can't bring up the loading plaque because too much
	// stack space has been used, so do it now
		SCR_BeginLoadingPlaque ();

	// issue the load command
		Cbuf_AddText (va ("load s%i\n", load_cursor) );
		return;

	case K_UPARROW:
	case K_LEFTARROW:
		S_LocalSound ("misc/menu1.wav");
		load_cursor--;
		if (load_cursor < 0)
			load_cursor = MAX_SAVEGAMES-1;
		break;

	case K_DOWNARROW:
	case K_RIGHTARROW:
		S_LocalSound ("misc/menu1.wav");
		load_cursor++;
		if (load_cursor >= MAX_SAVEGAMES)
			load_cursor = 0;
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
		Cbuf_AddText (va("save s%i\n", load_cursor));
		return;

	case K_UPARROW:
	case K_LEFTARROW:
		S_LocalSound ("misc/menu1.wav");
		load_cursor--;
		if (load_cursor < 0)
			load_cursor = MAX_SAVEGAMES-1;
		break;

	case K_DOWNARROW:
	case K_RIGHTARROW:
		S_LocalSound ("misc/menu1.wav");
		load_cursor++;
		if (load_cursor >= MAX_SAVEGAMES)
			load_cursor = 0;
		break;
	}
}

void M_Load_Mousemove(int cx, int cy) // woods #mousemenu
{
	M_UpdateCursor(cy, 32, 8, MAX_SAVEGAMES, &load_cursor);
}

void M_Save_Mousemove(int cx, int cy) // woods #mousemenu
{
	M_UpdateCursor(cy, 32, 8, MAX_SAVEGAMES, &load_cursor);
}




// ============================================================================

// woods #mapsmenu (iw)

/* maps menu */

#define MAX_VIS_MAPS	17

typedef struct
{
	const char* name;
	const char* date;
	qboolean	active;
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
	mapitem_t			*items;
} mapsmenu;

static void M_Maps_Add(const char* name, const char* date)
{
	mapitem_t map;
	map.name = name;
	map.date = date; // Set the date

	// Ensure there's enough space for one more item
	VEC_PUSH(mapsmenu.items, map);

	mapsmenu.items[mapsmenu.list.numitems] = map;
	mapsmenu.list.numitems++;
}

static void M_Maps_Init(void)
{
	filelist_item_t* item;

	mapsmenu.scrollbar_grab = false;
	mapsmenu.list.viewsize = MAX_VIS_MAPS;
	mapsmenu.list.cursor = -1;
	mapsmenu.list.scroll = 0;
	mapsmenu.list.numitems = 0;
	mapsmenu.mapcount = 0;
	VEC_CLEAR(mapsmenu.items);

	M_Ticker_Init(&mapsmenu.ticker);

	if (!descriptionsParsed)
		ExtraMaps_ParseDescriptions();

	for (item = extralevels; item; item = item->next)
		M_Maps_Add(item->name, item->data);

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

	if (mapsmenu.prev_cursor != mapsmenu.list.cursor)
	{
		mapsmenu.prev_cursor = mapsmenu.list.cursor;
		M_Ticker_Init(&mapsmenu.ticker);
	}
	else
		M_Ticker_Update(&mapsmenu.ticker);

	Draw_String(x, y - 28, "Maps");
	M_DrawQuakeBar(x - 8, y - 16, cols + 2);

	M_List_GetVisibleRange(&mapsmenu.list, &firstvis, &numvis);
	for (i = 0; i < numvis; i++)
	{
		int idx = i + firstvis;
		qboolean selected = (idx == mapsmenu.list.cursor);

		M_PrintScroll2(x, y + i * 8, (cols - 2) * 8, mapsmenu.items[idx].name, mapsmenu.items[idx].date, selected ? mapsmenu.ticker.scroll_time : 0.0);

		if (selected)
			M_DrawCharacter(x - 8, y + i * 8, 12 + ((int)(realtime * 4) & 1));
	}

	if (M_List_GetOverflow(&mapsmenu.list) > 0)
	{
		M_List_DrawScrollbar(&mapsmenu.list, x + cols * 8 - 8, y);

		if (mapsmenu.list.scroll > 0)
			M_DrawEllipsisBar(x, y - 8, cols);
		if (mapsmenu.list.scroll + mapsmenu.list.viewsize < mapsmenu.list.numitems)
			M_DrawEllipsisBar(x, y + mapsmenu.list.viewsize * 8, cols);
	}

	if (!mapshint) // woods
		M_PrintRGBA(20, 180, "to search, maps 'x' in the console", CL_PLColours_Parse("0xffffff"), 0.5f); // woods'

}

qboolean M_Maps_Match(int index, char initial)
{
	return q_tolower(mapsmenu.items[index].name[0]) == initial;
}

void M_Maps_Key(int key)
{
	int x, y;
	
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
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		M_Menu_SinglePlayer_f();
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	enter:
		if (mapsmenu.items[mapsmenu.list.cursor].name[0])
		{
			M_SetSkillMenuMap(mapsmenu.items[mapsmenu.list.cursor].name);
			M_Menu_Skill_f();
		}
		else
			S_LocalSound ("misc/menu3.wav");
		break;

	case K_MOUSE1:
		x = m_mousex - mapsmenu.x - (mapsmenu.cols - 1) * 8;
		y = m_mousey - mapsmenu.y;
		if (x < -8 || !M_List_UseScrollbar(&mapsmenu.list, y))
			goto enter;
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

	M_List_Mousemove(&mapsmenu.list, cy);
}

//=============================================================================
/* SKILL MENU */ 
// woods #skillmenu (iw)

int				m_skill_cursor;
qboolean		m_skill_usegfx;
qboolean		m_skill_usecustomtitle;
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

void M_Menu_Skill_f(void)
{
	key_dest = key_menu;
	m_skill_prevmenu = m_state;
	m_state = m_skill;
	m_entersound = true;
	M_Ticker_Init(&m_skill_ticker);


		// Select current skill level initially if there's no autosave
		m_skill_cursor = (int)skill.value;
		m_skill_cursor = CLAMP(0, m_skill_cursor, 3);
	
	m_skill_numoptions = 4;
}

void M_Skill_Draw(void)
{
	int		x, y, f;
	qpic_t* p;

	M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
	p = Draw_CachePic(m_skill_usecustomtitle ? "gfx/p_skill.lmp" : "gfx/ttl_sgl.lmp");
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
}

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

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1:
		key_dest = key_game;
		if (sv.active)
			Cbuf_AddText("disconnect\n");
		// Fresh start
		Cbuf_AddText(va("skill %d\n", m_skill_cursor));
		Cbuf_AddText("maxplayers 1\n");
		Cbuf_AddText("deathmatch 0\n"); //johnfitz
		Cbuf_AddText("coop 0\n"); //johnfitz
		Cbuf_AddText(va("map \"%s\"\n", m_skill_mapname));
		break;
	}
}

void M_Skill_Mousemove(int cx, int cy)
{
	int ybase = 48;
	int itemheight = m_skill_usegfx ? 20 : 16;

	M_UpdateCursor(cy, ybase, itemheight, 4, &m_skill_cursor);

}

//=============================================================================
/* MULTIPLAYER MENU */

int	m_multiplayer_cursor;
#define	MULTIPLAYER_ITEMS	3


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

	M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp") );
	p = Draw_CachePic ("gfx/p_multi.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);
	M_DrawTransPic (72, 32, Draw_CachePic ("gfx/mp_menu.lmp") );

	f = (int)(realtime * 10)%6;
	i = 24;
	if (strlen(lastmphost) > i)
		i = (strlen(lastmphost));

	M_DrawTransPic (54, 32 + m_multiplayer_cursor * 20,Draw_CachePic( va("gfx/menudot%i.lmp", f+1 ) ) );

	
	if (cl.maxclients > 1 && cls.state == ca_connected && !cls.demoplayback) // woods, give some extra info in mp menu
	{
		f = (320 - 26 * 8) / 2;
		M_DrawTextBox(f, 96, i, 2);
		f += 8;
		M_Print(f, 104, "currently connected to:");
		M_PrintWhite(f, 112, lastmphost);
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

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		if (++m_multiplayer_cursor >= MULTIPLAYER_ITEMS)
			m_multiplayer_cursor = 0;
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		if (--m_multiplayer_cursor < 0)
			m_multiplayer_cursor = MULTIPLAYER_ITEMS - 1;
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
		}
	}
}

void M_MultiPlayer_Mousemove(int cx, int cy) // woods #mousemenu
{
	M_UpdateCursor(cy, 32, 20, MULTIPLAYER_ITEMS, &m_multiplayer_cursor);
}

//=============================================================================
/* SETUP MENU */

static int		setup_cursor = 5; // woods 4 to 5 #

static int		setup_cursor_table[] = {40, 56, 72, 88, 112, 150}; // woods add value, change position #namemaker #colorbar

char	namemaker_name[16]; // woods #namemaker
qboolean namemaker_shortcut = false; // woods #namemaker
qboolean from_namemaker = false; // woods #namemaker

static char	setup_hostname[16];
static char	setup_myname[16];
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

#define	NUM_SETUP_CMDS	6 // woods 5 to 6 #namemaker
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

	if (cls.state == ca_connected)
	{
		char buf[15];
		const char* obs;
		obs = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "observer", buf, sizeof(buf));

		if (!strcmp(obs, "fly")) // woods #3rdperson
			flyme = true;
		else
			flyme = false;
	}

	if (!chase_active.value && !cls.demoplayback && host_initialized && !flyme && cls.state == ca_connected) // woods #3rdperson
	{
		chasewasnotactive = true;
		Cbuf_AddText("chase_active 1\n");
	}

	M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp") );
	p = Draw_CachePic ("gfx/p_multi.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);

	M_Print (64, 40, "Hostname");
	M_DrawTextBox (160, 32, 16, 1);
	M_Print (168, 40, setup_hostname);

	M_Print (64, 56, "Your name");
	M_DrawTextBox (160, 48, 16, 1);
	M_PrintWhite (168, 56, setup_myname); // woods change to white #namemaker

	M_Print(64, 72, "Name Maker"); // woods #namemaker

	M_Print (64, 88, "Shirt -"); // woods 80 to 104 #namemaker #showcolornum
	M_PrintWhite (126, 88, CL_PLColours_ToString (setup_top)); // woods #showcolornum
	M_DrawColorBar_Top (64, 94, atoi(CL_PLColours_ToString (setup_top))); // woods #colorbar
	M_Print (64, 112, "Pants -"); // woods 104 to 128 #namemaker #showcolornum
	M_PrintWhite (126, 112, CL_PLColours_ToString (setup_bottom)); // woods #showcolornum
	M_DrawColorBar_Bot (64, 118, atoi(CL_PLColours_ToString (setup_bottom))); // woods #colorbar

	if (!rgbactive) // woods
		M_PrintRGBA (64, 170, "Shift For RGB Colors", CL_PLColours_Parse ("0xffffff"), 0.6f); // woods

	M_DrawTextBox (64, 142, 14, 1);  // woods 140 to 152 #namemaker
	M_Print (72, 150, "Accept Changes"); // woods #colorbar

	p = Draw_CachePic ("gfx/bigbox.lmp");
	M_DrawTransPic (196, 77, p); // woods #colorbar
	p = Draw_CachePic ("gfx/menuplyr.lmp");

	setup_top = CL_PLColours_Parse(CL_PLColours_ToString(setup_top)); // woods menu color fix
	setup_bottom = CL_PLColours_Parse(CL_PLColours_ToString(setup_bottom)); // woods menu color fix

	M_DrawTransPicTranslate (208, 85, p, setup_top, setup_bottom); // woods #colorbar

	M_DrawCharacter (56, setup_cursor_table [setup_cursor], 12+((int)(realtime*4)&1));

	if (setup_cursor == 0)
		M_DrawCharacter (168 + 8*strlen(setup_hostname), setup_cursor_table [setup_cursor], 10+((int)(realtime*4)&1));

	if (setup_cursor == 1)
		M_DrawCharacter (168 + 8*strlen(setup_myname), setup_cursor_table [setup_cursor], 10+((int)(realtime*4)&1));
}

char lastColorSelected[10]; // woods

void M_Setup_Key (int k)
{
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
		setup_cursor--;
		if (setup_cursor < 0)
			setup_cursor = NUM_SETUP_CMDS-1;
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		setup_cursor++;
		if (setup_cursor >= NUM_SETUP_CMDS)
			setup_cursor = 0;
		break;

	case K_MWHEELDOWN:
	case K_LEFTARROW:
		if (setup_cursor < 2)
			return;
		S_LocalSound ("misc/menu3.wav");
		if (setup_cursor == 3) // 2 to 3 woods #namemaker
		{
			M_AdjustColour(&setup_top, -1);
			strncpy(lastColorSelected, CL_PLColours_ToString(setup_top), sizeof((CL_PLColours_ToString(setup_top))));
			if (chase_active.value && !cls.demoplayback && host_initialized && !flyme) // woods #3rdperson
				if (!CL_PLColours_Equals(setup_top, setup_oldtop) || !CL_PLColours_Equals(setup_bottom, setup_oldbottom))
				{
					Cbuf_AddText(va("color %s %s\n", CL_PLColours_ToString(setup_top), CL_PLColours_ToString(setup_bottom)));
					colordelta = true;
				}
		}
		if (setup_cursor == 4) // 3 to 4 woods #namemaker
		{
			M_AdjustColour(&setup_bottom, -1);
			strncpy(lastColorSelected, CL_PLColours_ToString(setup_bottom), sizeof((CL_PLColours_ToString(setup_bottom))));
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
		if (setup_cursor == 3) // 2 to 3 woods #namemaker
		{
			M_AdjustColour(&setup_top, +1);
			strncpy (lastColorSelected, CL_PLColours_ToString (setup_top), sizeof ((CL_PLColours_ToString (setup_top))));
			if (chase_active.value && !cls.demoplayback && host_initialized && !flyme) // woods #3rdperson
				if (!CL_PLColours_Equals(setup_top, setup_oldtop) || !CL_PLColours_Equals(setup_bottom, setup_oldbottom))
				{
					Cbuf_AddText(va("color %s %s\n", CL_PLColours_ToString(setup_top), CL_PLColours_ToString(setup_bottom)));
					colordelta = true;
				}
		}
		if (setup_cursor == 4) // 3 to 4 woods #namemaker
		{
			M_AdjustColour(&setup_bottom, +1);
			strncpy (lastColorSelected, CL_PLColours_ToString (setup_bottom), sizeof ((CL_PLColours_ToString (setup_bottom))));
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
		if (setup_cursor == 0 || setup_cursor == 1)
			return;

		if (setup_cursor == 3 || setup_cursor == 4) // inc 1 both woods #namemaker
			goto forward;

		if (setup_cursor == 2) // woods #namemaker
		{
			m_entersound = true;
			M_Menu_NameMaker_f();
			break;
		}

		// setup_cursor == 4 (OK)
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

	case K_BACKSPACE:
		if (setup_cursor == 0)
		{
			if (strlen(setup_hostname))
				setup_hostname[strlen(setup_hostname)-1] = 0;
		}

		if (setup_cursor == 1)
		{
			if (strlen(setup_myname))
				setup_myname[strlen(setup_myname)-1] = 0;
		}
		break;

	case 'c': // woods, copy color
	case 'C':
		if (keydown[K_CTRL])
		{
			if (lastColorSelected[0] != '\0')
				SDL_SetClipboardText (lastColorSelected);
			else
				SDL_SetClipboardText (CL_PLColours_ToString (setup_bottom));
			const char* soundFile = COM_FileExists("sound/qssm/copy.wav", NULL) ? "qssm/copy.wav" : "player/tornoff2.wav";
			S_LocalSound(soundFile); // woods add sound to screenshot
		}
		break;
	}
}


void M_Setup_Char (int k)
{
	int l;

	switch (setup_cursor)
	{
	case 0:
		l = strlen(setup_hostname);
		if (l < 15)
		{
			setup_hostname[l+1] = 0;
			setup_hostname[l] = k;
		}
		break;
	case 1:
		l = strlen(setup_myname);
		if (l < 15)
		{
			setup_myname[l+1] = 0;
			setup_myname[l] = k;
		}
		break;
	}
}


qboolean M_Setup_TextEntry (void)
{
	return (setup_cursor == 0 || setup_cursor == 1);
}

void M_Setup_Mousemove(int cx, int cy) // woods #mousemenu
{
	M_UpdateCursorWithTable(cy, setup_cursor_table, NUM_SETUP_CMDS, &setup_cursor);
}

//=============================================================================
/* NAME MAKER MENU */ // woods #namemaker from joequake, qrack
//=============================================================================
int	namemaker_cursor_x, namemaker_cursor_y;
#define	NAMEMAKER_TABLE_SIZE	16
//extern int key_special_dest;

void M_Menu_NameMaker_f (void)
{
	key_dest = key_menu;
	//key_special_dest = 1;
	m_state = m_namemaker;
	m_entersound = true;
	q_strlcpy(namemaker_name, setup_myname, sizeof(namemaker_name));
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

	M_Print(48, 16, "Your name");
	M_DrawTextBox(120, 8, 16, 1);
	M_PrintWhite(128, 16, namemaker_name);

	for (y = 0; y < NAMEMAKER_TABLE_SIZE; y++)
		for (x = 0; x < NAMEMAKER_TABLE_SIZE; x++)
			M_DrawCharacter(32 + (16 * x), 40 + (8 * y), NAMEMAKER_TABLE_SIZE * y + x);

	if (namemaker_cursor_y == NAMEMAKER_TABLE_SIZE)
		M_DrawCharacter(128, 184, 12 + ((int)(realtime * 4) & 1));
	else
		M_DrawCharacter(24 + 16 * namemaker_cursor_x, 40 + 8 * namemaker_cursor_y, 12 + ((int)(realtime * 4) & 1));

	//	M_DrawTextBox (136, 176, 2, 1);
	//M_Print(56, 184, "press");
	//M_PrintWhite(103, 184, "ESC");
	//M_Print(133, 184, "to save changes");
}

void M_NameMaker_Key (int k)
{
	int	l;

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
		S_LocalSound("misc/menu1.wav");
		namemaker_cursor_y--;
		if (namemaker_cursor_y < 0)
			namemaker_cursor_y = NAMEMAKER_TABLE_SIZE - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		namemaker_cursor_y++;
		if (namemaker_cursor_y > NAMEMAKER_TABLE_SIZE - 1)
			namemaker_cursor_y = 0;
		break;

	case K_PGUP:
		S_LocalSound("misc/menu1.wav");
		namemaker_cursor_y = 0;
		break;

	case K_PGDN:
		S_LocalSound("misc/menu1.wav");
		namemaker_cursor_y = NAMEMAKER_TABLE_SIZE - 1;
		break;

	case K_LEFTARROW:
		S_LocalSound("misc/menu1.wav");
		namemaker_cursor_x--;
		if (namemaker_cursor_x < 0)
			namemaker_cursor_x = NAMEMAKER_TABLE_SIZE - 1;
		break;

	case K_RIGHTARROW:
		S_LocalSound("misc/menu1.wav");
		namemaker_cursor_x++;
		if (namemaker_cursor_x >= NAMEMAKER_TABLE_SIZE - 1)
			namemaker_cursor_x = 0;
		break;

	case K_HOME:
		S_LocalSound("misc/menu1.wav");
		namemaker_cursor_x = 0;
		break;

	case K_END:
		S_LocalSound("misc/menu1.wav");
		namemaker_cursor_x = NAMEMAKER_TABLE_SIZE - 1;
		break;

	case K_BACKSPACE:
		if ((l = strlen(namemaker_name)))
			namemaker_name[l - 1] = 0;
		break;

	// If we reached this point, we are simulating ENTER

	case K_SPACE:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1: // woods #mousemenu
		if (namemaker_cursor_y == NAMEMAKER_TABLE_SIZE)
		{
			q_strlcpy(setup_myname, namemaker_name, sizeof(setup_myname));
			M_Menu_Setup_f();
		}
		else
		{
			l = strlen(namemaker_name);
			if (l < 15)
			{
				namemaker_name[l] = NAMEMAKER_TABLE_SIZE * namemaker_cursor_y + namemaker_cursor_x;
				namemaker_name[l + 1] = 0;
			}
		}
		break;

	default:
		if (k < 32 || k > 127)
			break;

		Key_Extra (&k);

		l = strlen(namemaker_name);
		if (l < 15)
		{
			namemaker_name[l] = k;
			namemaker_name[l + 1] = 0;
		}
		break;
	}
}

void M_NameMaker_Mousemove(int cx, int cy) // woods #mousemenu
{
	M_UpdateCursorXY(cx -8, cy - 8, 20, 32, 16, 8, NAMEMAKER_TABLE_SIZE, &namemaker_cursor_x, &namemaker_cursor_y);
}

//=============================================================================
/* NET MENU */

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

//=============================================================================
/* OPTIONS MENU */

enum
{
	OPT_CUSTOMIZE = 0,
	OPT_EXTRAS, // 1
	OPT_VIDEO, // 2
	OPT_SPACE,
	OPT_CONSOLE,
	OPT_DEFAULTS,
	OPT_SCALE,
	OPT_SCRSIZE,
	OPT_GAMMA,
	OPT_CONTRAST,
	OPT_MOUSESPEED,
	OPT_SBALPHA,
	OPT_SNDVOL,
	OPT_MUSICVOL,
	OPT_MUSICEXT,
	OPT_ALWAYRUN,
	OPT_INVMOUSE,
#if 0 // woods
	OPT_ALWAYSMLOOK,
	OPT_LOOKSPRING,
	OPT_LOOKSTRAFE,
#endif
//#ifdef _WIN32
//	OPT_USEMOUSE,
//#endif
	OPTIONS_ITEMS
};

enum
{
	ALWAYSRUN_OFF = 0,
	ALWAYSRUN_VANILLA,
	ALWAYSRUN_QUAKESPASM,
	ALWAYSRUN_ITEMS
};

#define	SLIDER_RANGE	6

int		options_cursor;
qboolean slider_grab; // woods #mousemenu
float target_scale_frac; // woods #mousemenu

struct // woods #mousemenu
{
	menulist_t		list;
	int				y;
	int				first_item;
	int				options_cursor;
	int				video_cursor;
	int* last_cursor;
} optionsmenu;

void M_Menu_Options_f (void)
{
	key_dest = key_menu;
	m_state = m_options;
	m_entersound = true;
	slider_grab = false; // woods #mousemenu

	IN_UpdateGrabs();
}


void M_AdjustSliders (int dir)
{
	int	curr_alwaysrun, target_alwaysrun;
	float	f, l;

	S_LocalSound ("misc/menu3.wav");

	switch (options_cursor)
	{
	case OPT_SCALE:	// console and menu scale
		l = ((vid.width + 31) / 32) / 10.0;
		f = scr_conscale.value + dir * .1;
		if (f < 1)	f = 1;
		else if(f > l)	f = l;
		Cvar_SetValue ("scr_conscale", f);
		Cvar_SetValue ("scr_menuscale", f);
		Cvar_SetValue ("scr_sbarscale", f);
		Cvar_SetValue ("scr_crosshairscale", f);
		break;
	case OPT_SCRSIZE:	// screen size
		f = scr_viewsize.value + dir * 10;
		if (f > 130)	f = 130;
		else if(f < 30)	f = 30;
		Cvar_SetValue ("viewsize", f);
		break;
	case OPT_GAMMA:	// gamma
		f = vid_gamma.value - dir * 0.05;
		if (f < 0.5)	f = 0.5;
		else if (f > 1)	f = 1;
		Cvar_SetValue ("gamma", f);
		break;
	case OPT_CONTRAST:	// contrast
		f = vid_contrast.value + dir * 0.1;
		if (f < 1)	f = 1;
		else if (f > 2)	f = 2;
		Cvar_SetValue ("contrast", f);
		break;
	case OPT_MOUSESPEED:	// mouse speed
		f = sensitivity.value + dir * 0.5;
		if (f > 11)	f = 11;
		else if (f < 1)	f = 1;
		Cvar_SetValue ("sensitivity", f);
		break;
	case OPT_SBALPHA:	// statusbar alpha
		f = scr_sbaralpha.value - dir * 0.05;
		if (f < 0)	f = 0;
		else if (f > 1)	f = 1;
		Cvar_SetValue ("scr_sbaralpha", f);
		break;
	case OPT_MUSICVOL:	// music volume
		f = bgmvolume.value + dir * 0.05f;
		if (f < 0)	f = 0;
		else if (f > 1)	f = 1;
		Cvar_SetValue ("bgmvolume", f);
		break;
	case OPT_MUSICEXT:	// enable external music vs cdaudio
		Cvar_Set ("bgm_extmusic", bgm_extmusic.value ? "0" : "1");
		break;
	case OPT_SNDVOL:	// sfx volume
		f = sfxvolume.value + dir * 0.05f;
		if (f < 0)	f = 0;
		else if (f > 1)	f = 1;
		Cvar_SetValue ("volume", f);
		break;

	case OPT_ALWAYRUN:	// always run
		if (cl_alwaysrun.value)
			curr_alwaysrun = ALWAYSRUN_QUAKESPASM;
		else if (cl_forwardspeed.value > 200)
			curr_alwaysrun = ALWAYSRUN_VANILLA;
		else
			curr_alwaysrun = ALWAYSRUN_OFF;
			
		target_alwaysrun = (ALWAYSRUN_ITEMS + curr_alwaysrun + dir) % ALWAYSRUN_ITEMS;
			
		if (target_alwaysrun == ALWAYSRUN_VANILLA)
		{
			Cvar_SetValue ("cl_alwaysrun", 0);
			Cvar_SetValue ("cl_forwardspeed", 400);
			Cvar_SetValue ("cl_backspeed", 400);
		}
		else if (target_alwaysrun == ALWAYSRUN_QUAKESPASM)
		{
			Cvar_SetValue ("cl_alwaysrun", 1);
			Cvar_SetValue ("cl_forwardspeed", 200);
			Cvar_SetValue ("cl_backspeed", 200);
		}
		else // ALWAYSRUN_OFF
		{
			Cvar_SetValue ("cl_alwaysrun", 0);
			Cvar_SetValue ("cl_forwardspeed", 200);
			Cvar_SetValue ("cl_backspeed", 200);
		}
		break;

	case OPT_INVMOUSE:	// invert mouse
		Cvar_SetValue ("m_pitch", -m_pitch.value);
		break;
#if 0 
	case OPT_ALWAYSMLOOK:
		if (in_mlook.state & 1)
			Cbuf_AddText("-mlook");
		else
			Cbuf_AddText("+mlook");
		break;
// woods
	case OPT_LOOKSPRING:	// lookspring
		Cvar_Set ("lookspring", lookspring.value ? "0" : "1");
		break;

	case OPT_LOOKSTRAFE:	// lookstrafe
		Cvar_Set ("lookstrafe", lookstrafe.value ? "0" : "1");
		break;
#endif
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
	float l;
	f = CLAMP(0.f, f, 1.f);

	switch (option)
	{
	case OPT_SCALE:	// console and menu scale
		target_scale_frac = f;
		l = (vid.width / 320.0) - 1;
		f = l > 0 ? f * l + 1 : 0;
		// Delay the actual update until we release the mouse button
		// to keep the UI layout stable while adjusting the scale
		Cvar_SetValue("scr_conscale", f);
		Cvar_SetValue("scr_crosshairscale", f);
		Cvar_SetValue("scr_sbarscale", f);
		if (!slider_grab)
			Cvar_SetValue("scr_menuscale", f);
		return true;
	case OPT_SCRSIZE:	// screen size
		f = f * (130 - 30) + 30;
		if (f >= 100)
			f = floor(f / 10 + 0.5) * 10;
		Cvar_SetValue("viewsize", f);
		return true;
	case OPT_GAMMA:	// gamma
		f = 1.f - f * 0.5f;
		Cvar_SetValue("gamma", f);
		return true;
	case OPT_CONTRAST:	// contrast
		f += 1.f;
		Cvar_SetValue("contrast", f);
		return true;
	case OPT_MOUSESPEED:	// mouse speed
		f = f * 10.f + 1.f;
		Cvar_SetValue("sensitivity", f);
		return true;
	case OPT_SBALPHA:	// statusbar alpha
		f = 1.f - f;
		Cvar_SetValue("scr_sbaralpha", f);
		return true;
	case OPT_MUSICVOL:	// music volume
		Cvar_SetValue("bgmvolume", f);
		return true;
	case OPT_SNDVOL:	// sfx volume
		Cvar_SetValue("volume", f);
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
	slider_grab = false;
	S_LocalSound("misc/menu1.wav");
	if (options_cursor == OPT_SCALE)
		M_SetSliderValue(OPT_SCALE, target_scale_frac);
}

qboolean M_SliderClick(int cx, int cy) // woods #mousemenu
{
	cx -= 220;
	if (cx < -12 || cx > SLIDER_RANGE * 8 + 4)
		return false;
	// HACK: we set the flag to true before updating the slider
	// to avoid changing the UI scale and implicitly the layout
	if (options_cursor == OPT_SCALE)
		slider_grab = true;
	if (!M_SetSliderValue(options_cursor, M_MouseToSliderFraction(cx)))
		return false;
	slider_grab = true;
	S_LocalSound("misc/menu3.wav");
	return true;
}

void M_Options_Draw (void)
{
	float		r, l;
	qpic_t	*p;

	if (slider_grab && !keydown[K_MOUSE1]) // woods #mousemenu
		M_ReleaseSliderGrab();

	M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp") );
	p = Draw_CachePic ("gfx/p_option.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);

	// Draw the items in the order of the enum defined above:
	// OPT_CUSTOMIZE:
	M_Print (16, 32,			"              Controls   ..."); // woods add '...'

	// OPT_EXTRAS:
	M_Print(16, 32 + 8 * OPT_EXTRAS, "         Extra Options   ..."); // woods add '...'

	// OPT_VIDEO:
	if (vid_menudrawfn)
		M_Print(16, 32 + 8 * OPT_VIDEO, "         Video Options   ..."); // woods add '...'

	// OPT_CONSOLE:
	M_Print (16, 32 + 8*OPT_CONSOLE,	"          Goto console");
	// OPT_DEFAULTS:
	M_Print (16, 32 + 8*OPT_DEFAULTS,	"          Reset config");

	// OPT_SCALE:
	M_Print (16, 32 + 8*OPT_SCALE,		"                 Scale");
	l = (vid.width / 320.0) - 1;
	r = l > 0 ? (scr_conscale.value - 1) / l : 0;
	if (slider_grab && options_cursor == OPT_SCALE) // woods #mousemenu
		r = target_scale_frac;
	M_DrawSlider (220, 32 + 8*OPT_SCALE, r, scr_conscale.value, "%.1f");

	// OPT_SCRSIZE:
	M_Print (16, 32 + 8*OPT_SCRSIZE,	"           Screen size");
	r = (scr_viewsize.value - 30) / (130 - 30);
	M_DrawSlider (220, 32 + 8*OPT_SCRSIZE, r, scr_viewsize.value, "%.0f");

	// OPT_GAMMA:
	M_Print (16, 32 + 8*OPT_GAMMA,		"            Brightness");
	r = (1.0 - vid_gamma.value) / 0.5;
	M_DrawSlider (220, 32 + 8*OPT_GAMMA, r, 10.f * r, "%.0f");

	// OPT_CONTRAST:
	M_Print (16, 32 + 8*OPT_CONTRAST,	"              Contrast");
	r = vid_contrast.value - 1.0;
	M_DrawSlider (220, 32 + 8*OPT_CONTRAST, r, 10.f * r, "%.0f");
	
	// OPT_MOUSESPEED:
	M_Print (16, 32 + 8*OPT_MOUSESPEED,	"           Mouse Speed");
	r = (sensitivity.value - 1)/10;
	M_DrawSlider (220, 32 + 8*OPT_MOUSESPEED, r, sensitivity.value, "%.1f");

	// OPT_SBALPHA:
	M_Print (16, 32 + 8*OPT_SBALPHA,	"       Statusbar alpha");
	r = (1.0 - scr_sbaralpha.value) ; // scr_sbaralpha range is 1.0 to 0.0
	M_DrawSlider (220, 32 + 8*OPT_SBALPHA, r, 100.0f * r, "%.0f%%");

	// OPT_SNDVOL:
	M_Print (16, 32 + 8*OPT_SNDVOL,		"          Sound Volume");
	r = sfxvolume.value;
	M_DrawSlider (220, 32 + 8*OPT_SNDVOL, r, 100.f * sfxvolume.value, "%.0f%%");

	// OPT_MUSICVOL:
	M_Print (16, 32 + 8*OPT_MUSICVOL,	"          Music Volume");
	r = bgmvolume.value;
	M_DrawSlider (220, 32 + 8*OPT_MUSICVOL, r, 100.f * bgmvolume.value, "%.0f%%");

	// OPT_MUSICEXT:
	M_Print (16, 32 + 8*OPT_MUSICEXT,	"        External Music");
	M_DrawCheckbox (220, 32 + 8*OPT_MUSICEXT, bgm_extmusic.value);

	// OPT_ALWAYRUN:
	M_Print (16, 32 + 8*OPT_ALWAYRUN,	"            Always Run");
	if (cl_alwaysrun.value)
		M_Print (220, 32 + 8*OPT_ALWAYRUN, "qs/power hop"); // woods
	else if (cl_forwardspeed.value > 200.0)
		M_Print (220, 32 + 8*OPT_ALWAYRUN, "vanilla");
	else
		M_Print (220, 32 + 8*OPT_ALWAYRUN, "off");

	// OPT_INVMOUSE:
	M_Print (16, 32 + 8*OPT_INVMOUSE,	"          Invert Mouse");
	M_DrawCheckbox (220, 32 + 8*OPT_INVMOUSE, m_pitch.value < 0);
#if 0
	// OPT_ALWAYSMLOOK:
	M_Print (16, 32 + 8*OPT_ALWAYSMLOOK,	"            Mouse Look");
	M_DrawCheckbox (220, 32 + 8*OPT_ALWAYSMLOOK, in_mlook.state & 1);
 // woods
	// OPT_LOOKSPRING:
	M_Print (16, 32 + 8*OPT_LOOKSPRING,	"            Lookspring");
	M_DrawCheckbox (220, 32 + 8*OPT_LOOKSPRING, lookspring.value);

	// OPT_LOOKSTRAFE:
	M_Print (16, 32 + 8*OPT_LOOKSTRAFE,	"            Lookstrafe");
	M_DrawCheckbox (220, 32 + 8*OPT_LOOKSTRAFE, lookstrafe.value);
#endif

// cursor
	M_DrawCharacter (200, 32 + options_cursor*8, 12+((int)(realtime*4)&1));
}


void M_Options_Key (int k)
{
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
		case OPT_CONSOLE:
			m_state = m_none;
			Con_ToggleConsole_f ();
			break;
		case OPT_DEFAULTS:
			if (SCR_ModalMessage("This will reset all controls\n"
					"and stored cvars. Continue? (y/n)\n", 15.0f))
			{
				Cbuf_AddText ("resetcfg\n");
				Cbuf_AddText ("exec default.cfg\n");
			}
			break;
		case OPT_EXTRAS:
			M_Menu_Extras_f ();
			break;
		case OPT_VIDEO:
			M_Menu_Video_f ();
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
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		options_cursor++;
		if (options_cursor >= OPTIONS_ITEMS)
			options_cursor = 0;
		break;

	case K_LEFTARROW:
	case K_MWHEELDOWN: // woods #mousemenu
		M_AdjustSliders (-1);
		break;

	case K_RIGHTARROW:
	case K_MWHEELUP: // woods #mousemenu
		M_AdjustSliders (1);
		break;

	case K_MOUSE1: // woods #mousemenu
		if (!M_SliderClick(m_mousex, m_mousey))
			goto enter;
		break;
	}

	if (options_cursor == OPTIONS_ITEMS - 1 && vid_menudrawfn == NULL)
	{
		if (k == K_UPARROW)
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
		M_SetSliderValue(options_cursor, M_MouseToSliderFraction(cx - 220));
		return;
	}

	M_UpdateCursor(cy, 36, 8, OPTIONS_ITEMS, &options_cursor);
}

//=============================================================================
/* KEYS MENU */

const char *quakebindnames[][2] = // woods use iw quake bind names
{
	{"+forward",		"Move forward"},
	{"+back",			"Move backward"},
	{"+moveleft",		"Move left"},
	{"+moveright",		"Move right"},
	{"+jump",			"Jump / swim up"},
	{"+moveup",			"Swim up"},
	{"+movedown",		"Swim down"},
	{"+speed",			"Run"},
	{"+strafe",			"Sidestep"},
	{"+left",			"Turn left"},
	{"+right",			"Turn right"},
	{"+lookup",			"Look up"},
	{"+lookdown",		"Look down"},
	{"centerview",		"Center view"},
	{"zoom_in",			"Toggle zoom"},
	{"+zoom",			"Quick zoom"},
	{"+attack",			"Attack"},
	{"impulse 10",		"Next weapon"},
	{"impulse 12",		"Previous weapon"},
	{"impulse 1",		"Axe"},
	{"impulse 2",		"Shotgun"},
	{"impulse 3",		"Super Shotgun"},
	{"impulse 4",		"Nailgun"},
	{"impulse 5",		"Super Nailgun"},
	{"impulse 6",		"Grenade Launcher"},
	{"impulse 7",		"Rocket Launcher"},
	{"impulse 8",		"Thunderbolt"},
	{"impulse 225",		"Laser Cannon"},
	{"impulse 226",		"Mjolnir"},
};
#define	NUMQUAKECOMMANDS	(sizeof(quakebindnames)/sizeof(quakebindnames[0]))


#define MAX_VIS_KEYS	18 // woods #mousemenu

static struct
{
	menulist_t			list; // woods #mousemenu
} keysmenu;

typedef struct { // woods #mousemenu
	char* cmd;
	char* desc;
} bindname_t;

static bindname_t* bindnames = NULL; // woods #mousemenu
static int numbindnames = 0; // woods #mousemenu

qboolean	bind_grab;

void M_Keys_Populate(void) // woods #mousemenu -- modified 
{
	FILE* file;
	char line[1024];
	if (numbindnames) return;

	// Try to open the file
	if (COM_FOpenFile("bindlist.lst", &file, NULL) >= 0 && file) 
	{
		while (fgets(line, sizeof(line), file))
		{
			const char* cmd, * desc;
			Cmd_TokenizeString(line);
			cmd = Cmd_Argv(0);
			desc = Cmd_Argv(1);

			if (*cmd)
			{
				bindnames = (bindname_t*)Z_Realloc(bindnames, sizeof(bindname_t)*(numbindnames+1));
				bindnames[numbindnames].cmd = (char*)Z_Malloc(strlen(cmd)+1);
				strcpy(bindnames[numbindnames].cmd, cmd);
				bindnames[numbindnames].desc = (char*)Z_Malloc(strlen(desc)+1);
				strcpy(bindnames[numbindnames].desc, desc);
				numbindnames++;
			}
		}
		fclose(file);
	}

	// Fallback to default bindings if no bindings were loaded from the file
	if (!numbindnames)
	{
		bindnames = (bindname_t*)Z_Realloc(bindnames, sizeof(bindname_t)*NUMQUAKECOMMANDS);
		for (int i = 0; i < NUMQUAKECOMMANDS; i++)
		{
			bindnames[i].cmd = (char*)Z_Malloc(strlen(quakebindnames[i][0])+1);
			strcpy(bindnames[i].cmd, quakebindnames[i][0]);
			bindnames[i].desc = (char*)Z_Malloc(strlen(quakebindnames[i][1])+1);
			strcpy(bindnames[i].desc, quakebindnames[i][1]);
		}
		numbindnames = NUMQUAKECOMMANDS;
	}
}

void M_Menu_Keys_f (void)
{
	key_dest = key_menu;
	m_state = m_keys;
	m_entersound = true;

	M_Keys_Populate();

	keysmenu.list.viewsize = MAX_VIS_KEYS; // woods #mousemenu
	keysmenu.list.cursor = -1;
	keysmenu.list.scroll = 0;
	keysmenu.list.numitems = numbindnames;

	IN_UpdateGrabs();
}

void M_FindKeysForCommand (const char *command, int *threekeys)
{
	int		count;
	int		j;
	int		l;
	char	*b;
	int		bindmap = 0;

	threekeys[0] = threekeys[1] = threekeys[2] = -1;
	l = strlen(command);
	count = 0;

	for (j = 0; j < MAX_KEYS; j++)
	{
		b = keybindings[bindmap][j];
		if (!b)
			continue;
		if (!strncmp (b, command, l) )
		{
			threekeys[count] = j;
			count++;
			if (count == 3)
				break;
		}
	}
}

void M_UnbindCommand (const char *command)
{
	int		j;
	int		l;
	char	*b;
	int		bindmap = 0;

	l = strlen(command);

	for (j = 0; j < MAX_KEYS; j++)
	{
		b = keybindings[bindmap][j];
		if (!b)
			continue;
		if (!strncmp (b, command, l) )
			Key_SetBinding (j, NULL, bindmap);
	}
}

extern qpic_t	*pic_up, *pic_down;

void M_Keys_Draw (void)
{
	int		firstvis, numvis, i, x, y, cols; // woods #mousemenu
	qpic_t* p; // woods #mousemenu

	p = Draw_CachePic ("gfx/ttl_cstm.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);
	if (bind_grab)
		M_Print (12, 32, "Press a key or button for this action");
	else
		M_Print (18, 32, "Enter to change, backspace to clear");

	x = 0;
	y = 48;
	cols = 36;
	if (M_List_GetOverflow(&keysmenu.list) > 0) { // woods #mousemenu
		if (keysmenu.list.scroll > 0)
			M_DrawEllipsisBar(x, y - 8, cols);
		if (keysmenu.list.scroll + keysmenu.list.viewsize < keysmenu.list.numitems)
			M_DrawEllipsisBar(x, y + keysmenu.list.viewsize * 8, cols);
	}

	y+=2;

	M_List_GetVisibleRange(&keysmenu.list, &firstvis, &numvis); // woods #mousemenu
	for (i = firstvis; numvis-- > 0; i++) 
	{
		void (*print_fn)(int, int, const char*) = (i == keysmenu.list.cursor && bind_grab) ? M_PrintWhite : M_Print;
		print_fn(0, y, bindnames[i].desc);

		int keys[3];
		M_FindKeysForCommand(bindnames[i].cmd, keys);
		if (i == keysmenu.list.cursor && bind_grab && keys[2] != -1)
			keys[0] = -1;

		const char* keyStr = keys[0] == -1 ? "???" : Key_KeynumToString(keys[0]);
		print_fn(136, y, keyStr);

		for (int j = 1; j <= 2 && keys[j] != -1; j++) {
			print_fn(136 + strlen(keyStr) * 8 + 8, y, "or");
			keyStr = Key_KeynumToString(keys[j]);
			print_fn(136 + strlen(keyStr) * 8 + 32, y, keyStr);
		}

		if (i == keysmenu.list.cursor) 
		{
			M_DrawCharacter(128, y, bind_grab ? '=' : 12 + ((int)(realtime * 4) & 1));
		}
		y += 8;
	}
}

void M_Keys_Key (int k)
{
	char	cmd[80];
	if (bind_grab)
	{	// defining a key
		S_LocalSound ("misc/menu1.wav");
		if (k != K_ESCAPE && k != '`') // woods #mousemenu
		{
			int keys[3];
			M_FindKeysForCommand(bindnames[keysmenu.list.cursor].cmd, keys);
			if (keys[2] != -1)
				M_UnbindCommand(bindnames[keysmenu.list.cursor].cmd);
			sprintf (cmd, "bind \"%s\" \"%s\"\n", Key_KeynumToString (k), bindnames[keysmenu.list.cursor].cmd);
			Cbuf_InsertText (cmd);
		}
		bind_grab = false;
		IN_UpdateGrabs();
		return;
	}
	if (M_List_Key(&keysmenu.list, k)) // woods #mousemenu
		return;
	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		M_Menu_Options_f ();
		break;

	case K_ENTER:		// go into bind mode
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1:
		S_LocalSound("misc/menu2.wav");
		bind_grab = true;
		M_List_AutoScroll(&keysmenu.list); // woods #mousemenu
		IN_UpdateGrabs();
		break;

	case K_BACKSPACE:	// delete bindings
	case K_DEL:
		S_LocalSound ("misc/menu2.wav");
		M_UnbindCommand (bindnames[keysmenu.list.cursor].cmd); // woods #mousemenu
		break;
	}
}

void M_Keys_Mousemove(int cx, int cy) // woods #mousemenu
{
	M_List_Mousemove(&keysmenu.list, cy - 50);
}

//=============================================================================
/* QSS's EXTRAS MENU */

static enum extras_e
{
	EXTRAS_FILTERING,
	EXTRAS_EXTERNALTEX,
	EXTRAS_REPLACEMENTMODELS,
	EXTRAS_MODELLERP,
	EXTRAS_FPSCAP,
	EXTRAS_YIELD,
	EXTRAS_DEMOREEL,
	EXTRAS_RENDERSCALE,
	EXTRAS_NETEXTENSIONS,
	EXTRAS_QCEXTENSIONS,
	EXTRAS_CLASSICPARTICLES,
	EXTRAS_AUDIORATE,
	EXTRAS_PREDICTION,
	EXTRAS_ITEMS
} extras_cursor;

int numberOfExtrasItems = EXTRAS_ITEMS; // woods #mousemenu

void M_Menu_Extras_f (void)
{
	key_dest = key_menu;
	m_state = m_extras;
	m_entersound = true;

	IN_UpdateGrabs();
}


static void M_Extras_AdjustSliders (int dir)
{
	extern cvar_t pr_checkextension, r_replacemodels, gl_load24bit, cl_nopext, r_lerpmodels, r_lerpmove, host_maxfps, sys_throttle, r_particles, sv_nqplayerphysics, cl_nopred;
	int m;
	S_LocalSound ("misc/menu3.wav");

	switch (extras_cursor)
	{
	case EXTRAS_FILTERING:
		m = TexMgr_GetTextureMode() + dir;
		while (m == 3 || (m>4&&m<8) || (m>8&&m<16))
			m += dir;
		if (m < 0)
			m = 16;
		else if (m > 16)
			m = 0;
		if (m == 0)
		{
			Cvar_Set ("gl_texturemode", "nll");	//use linear minification filter to reduce distant noise without uglifying the visuals.
			Cvar_Set ("gl_texture_anisotropy", "1");
		}
		else
		{
			Cvar_Set ("gl_texturemode", "GL_LINEAR_MIPMAP_LINEAR");
			Cvar_SetValue ("gl_texture_anisotropy", m);
		}
		break;
	case EXTRAS_EXTERNALTEX:
		Cvar_SetValueQuick (&gl_load24bit, !gl_load24bit.value);
		Cbuf_AddText("flush\n");	//needs to be a vid_reload, but qs doesn't exactly do that nicely...
		break;
	case EXTRAS_REPLACEMENTMODELS:
		Cvar_SetQuick (&r_replacemodels, *r_replacemodels.string?"":"iqm md5mesh md3");
		Cbuf_AddText("flush\n");
		break;
	case EXTRAS_MODELLERP:
		if (r_lerpmodels.value || r_lerpmove.value)
		{
			Cvar_SetValueQuick(&r_lerpmodels, 0);
			Cvar_SetValueQuick(&r_lerpmove, 0);
		}
		else
		{
			Cvar_SetValueQuick(&r_lerpmodels, 1);
			Cvar_SetValueQuick(&r_lerpmove,  1);
		}
		break;
	case EXTRAS_FPSCAP:
		{
			static int caps[] = {30, 60, 72, 120, 144, 500, 0};
			int best = 0, bestdiff = INT_MAX, diff, i;
			for (i = 0; i < countof(caps); i++)
			{
				diff = abs((int)host_maxfps.value - caps[i]);
				if (diff < bestdiff)
				{
					bestdiff = diff;
					best = i;
				}
			}
			best += dir;
			best = CLAMP(0, best, countof(caps)-1);
			Cvar_SetValueQuick (&host_maxfps, caps[best]);
		}
		break;
	case EXTRAS_YIELD:
		Cvar_SetQuick (&sys_throttle, sys_throttle.value?"0":sys_throttle.default_string);
		break;
	case EXTRAS_DEMOREEL:
		m = cl_demoreel.value+dir;
		if (m < 0)
			m = 2;
		else if (m > 2)
			m = 0;
		Cvar_SetValueQuick (&cl_demoreel, m);
		break;
	case EXTRAS_RENDERSCALE:
		m = r_scale.value-dir;
		m = CLAMP(1, m, 4);
		Cvar_SetValueQuick(&r_scale, m);
		break;
	case EXTRAS_NETEXTENSIONS:
		Cvar_SetValueQuick (&cl_nopext, !cl_nopext.value);
		break;
	case EXTRAS_QCEXTENSIONS:
		Cvar_SetValueQuick (&pr_checkextension, !pr_checkextension.value);
		break;
	case EXTRAS_CLASSICPARTICLES:
		Cvar_SetValueQuick (&r_particles, (r_particles.value==1)?2:1);
		break;
	case EXTRAS_AUDIORATE:
		Cvar_SetValueQuick (&snd_mixspeed, (snd_mixspeed.value==48000)?44100:48000);
	//	Cbuf_AddText("\nsnd_restart\n");
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
	case EXTRAS_ITEMS:	//not a real option
		break;
	}
}

void M_Extras_Draw (void) // woods 
{
	extern cvar_t pr_checkextension, r_replacemodels, gl_load24bit, cl_nopext, r_lerpmodels, r_lerpmove, host_maxfps, sys_throttle, r_particles, sv_nqplayerphysics, cl_nopred;
	int m;
	qpic_t	*p;
	enum extras_e i;

	//M_DrawTransPic (0, 4, Draw_CachePic ("gfx/qplaque.lmp") ); // woods
	p = Draw_CachePic ("gfx/p_option.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);

	const char* title;
	title = "Extra Options";
	M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);

	for (i = 0; i < EXTRAS_ITEMS; i++)
	{
		int y = 48 + 8*i;
		switch(i)
		{
		case EXTRAS_FILTERING:
			M_Print (0, y,	" Texture Filtering");
			m = TexMgr_GetTextureMode();
			switch(m)
			{
			case 0:
				M_Print (176, y, "nearest");
				break;
			case 1:
				M_Print (176, y, "linear");
				break;
			default:
				M_Print (176, y, va("anisotropic %i", m));
				break;
			}
			break;
		case EXTRAS_EXTERNALTEX:
			M_Print (0, y,	"   Custom Textures");
			M_DrawCheckbox (176, y, !!gl_load24bit.value);
			break;
		case EXTRAS_REPLACEMENTMODELS:
			M_Print (0, y,	"     Custom Models");
			M_DrawCheckbox (176, y, !!*r_replacemodels.string);
			break;
		case EXTRAS_MODELLERP:
			M_Print (0, y,	"        Model Lerp");
			M_DrawCheckbox(176, y, !!r_lerpmodels.value && !!r_lerpmove.value);
			break;
		case EXTRAS_FPSCAP:
			if (host_maxfps.value < 0)
				M_Print (0, y,	"       Maximum PPS");
			else
				M_Print (0, y,	"       Maximum FPS");
			if (host_maxfps.value)
				M_Print (176, y, va("%g", fabs(host_maxfps.value)));
			else
				M_Print (176, y, "uncapped");
			break;
		case EXTRAS_YIELD:
			M_Print (0, y,	"       Frame Delay");
			if (sys_throttle.value)
				M_Print (176, y, "on");
			else
				M_Print (176, y, "off");
			break;
		case EXTRAS_DEMOREEL:
			M_Print (0, y,	"      Attract Mode");
			if (cl_demoreel.value>1)
				M_Print (176, y, "on");
			else if (cl_demoreel.value)
				M_Print (176, y, "startup only");
			else
				M_Print (176, y, "off");
			break;
		case EXTRAS_RENDERSCALE:
			M_Print (0, y,	"      Render Scale");
			if (r_scale.value==1)
				M_Print (176, y, "native");
			else
				M_Print (176, y, va("1/%g", r_scale.value));
			break;
		case EXTRAS_NETEXTENSIONS:
			M_Print (0, y,	"     Protocol Exts");
			M_Print (176, y, cl_nopext.value?"blocked":"enabled");
			break;
		case EXTRAS_QCEXTENSIONS:
			M_Print (0, y,	"     QC Extensions");
			M_Print (176, y, pr_checkextension.value?"enabled":"blocked");
			break;

		case EXTRAS_CLASSICPARTICLES:
			M_Print (0, y,	" Classic Particles");
			if (r_particles.value == 1)
				M_Print (176, y, "disabled");
			else if (r_particles.value == 1)
				M_Print (176, y, "round");
			else if (r_particles.value == 2)
				M_Print (176, y, "square");
			else
				M_Print (176, y, "?!?");
			break;

		case EXTRAS_AUDIORATE:
			M_Print (0, y,	"        Audio Rate");
			if (snd_mixspeed.value == 48000)
				M_Print (176, y, "48000 hz (DVD)");
			else if (r_particles.value == 1)
				M_Print (176, y, "44100 hz (CD)");
			else
				M_Print (176, y, va("%i hz", (int)snd_mixspeed.value));
			break;

		case EXTRAS_PREDICTION:
			M_Print (0, y,	"        Prediction");
			if (!cl_nopred.value && !sv_nqplayerphysics.value)
				M_Print (176, y, "on (override ssqc)");	//deathmatch! will break quirky mods like quakerally.
			else if (!cl_nopred.value && sv_nqplayerphysics.value)
				M_Print (176, y, "on (compat phys)");	//conservative / default setting.
			else if (cl_nopred.value && !sv_nqplayerphysics.value)
				M_Print (176, y, "off (override ssqc)"); //silly setting (skipped when changing in menu)
			else
				M_Print (178, y, "off");	//honest/oldskool setting.
			break;

		case EXTRAS_ITEMS:	//unreachable.
			break;
		}
	}

// cursor
	M_DrawCharacter (168, 48 + extras_cursor*8, 12+((int)(realtime*4)&1));
}


void M_Extras_Key (int k)
{
	switch (k)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		M_Menu_Options_f ();
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1: // woods #mousemenu
		m_entersound = true;
		switch (extras_cursor)
		{
		default:
			M_Extras_AdjustSliders (1);
			break;
		}
		return;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		if (extras_cursor <= 0)
			extras_cursor = EXTRAS_ITEMS-1;
		else
			extras_cursor--;
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		extras_cursor++;
		if (extras_cursor >= EXTRAS_ITEMS)
			extras_cursor = 0;
		break;

	case K_LEFTARROW:
	case K_MWHEELDOWN: // woods #mousemenu
		M_Extras_AdjustSliders (-1);
		break;

	case K_RIGHTARROW:
	case K_MWHEELUP: // woods #mousemenu
		M_Extras_AdjustSliders (1);
		break;
	}
}

void M_Extras_Mousemove(int cx, int cy) // woods #mousemenu
{
	int tempCursor = extras_cursor; // Store enum value in an int variable
	M_UpdateCursor(cy, 48, 8, numberOfExtrasItems, &tempCursor);
	extras_cursor = tempCursor; // Cast back to enum type if necessary
}

//=============================================================================
/* VIDEO MENU */

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

//=============================================================================
/* HELP MENU */

int		help_page;
#define	NUM_HELP_PAGES	6


void M_Menu_Help_f (void)
{
	key_dest = key_menu;
	m_state = m_help;
	m_entersound = true;
	help_page = 0;
	IN_UpdateGrabs();
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

//=============================================================================
/* QUIT MENU */

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
	if (key == K_ESCAPE)
	{
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

//=============================================================================
/* LAN CONFIG MENU */

int		lanConfig_cursor = -1;
int     lanConfig_cursor_table_newgame[] = { 76, 86, 104 }; // Updated cursor positions for "New Game"
int		lanConfig_cursor_table[] = { 76, 84, 92, 100, 108, 138 }; // woods #mousemenu #bookmarksmenu
int*	lanConfig_cursor_ptr = NULL; // Pointer to the current cursor table

int     NUM_LANCONFIG_CMDS;
#define NUM_LANCONFIG_CMDS_NEWGAME 3
#define NUM_LANCONFIG_CMDS_JOINGAME 6

int 	lanConfig_port;
char	lanConfig_portname[6];
char	lanConfig_joinname[22];
int     lanConfig_protocol_cursor = 0; // Track selected protocol

extern int sv_protocol;
extern unsigned int	sv_protocol_pext2;

void SetProtocol(int protocol_cursor)
{
	if (protocol_cursor < 3)
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
		switch (protocol_cursor - 3) // Adjust cursor for FTE+ options
		{
		case 0: Cbuf_AddText("sv_protocol FTE+15\n"); break; // PROTOCOL_NETQUAKE with FTE extensions
		case 1: Cbuf_AddText("sv_protocol FTE+666\n"); break; // PROTOCOL_FITZQUAKE with FTE extensions
		case 2: Cbuf_AddText("sv_protocol FTE+999\n"); break; // PROTOCOL_RMQ with FTE extensions
		}
	}
}

const char* GetProtocolDescription(int protocol_cursor)
{
	if (protocol_cursor < 3)
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
		switch (protocol_cursor - 3) // Adjust cursor for FTE+ options
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
	
	if (StartingGame)
	{
		// Use New Game configuration
		lanConfig_cursor_ptr = lanConfig_cursor_table_newgame;
		NUM_LANCONFIG_CMDS = NUM_LANCONFIG_CMDS_NEWGAME;
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

		// If FTE extensions are enabled, shift by 5 to reflect FTE+ versions
		if (sv_protocol_pext2)
		{
			lanConfig_protocol_cursor += 3; // Shift to FTE+ versions
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
		if (JoiningGame && TCPIPConfig)
			lanConfig_cursor = 2;
		else
			lanConfig_cursor = 1;
	}
	if (StartingGame && lanConfig_cursor >= 3)
		lanConfig_cursor = 1;
	lanConfig_port = DEFAULTnet_hostport;
	sprintf(lanConfig_portname, "%u", lanConfig_port);

	m_return_onerror = false;
	m_return_reason[0] = 0;
	IN_UpdateGrabs();
}


void M_LanConfig_Draw (void)
{
	qpic_t	*p;
	int		basex;
	int		y;
	int		numaddresses;
	qhostaddr_t addresses[16];
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
	/*if (IPXConfig) // woods #skipipx
		protocol = "IPX";
	else*/
	//protocol = "TCP/IP";
	M_PrintWhite (basex, 32, va ("%s", startJoin));
	basex += 8;

	y = 52;
	M_Print (basex, y, "Address:");
#if 1
	numaddresses = NET_ListAddresses(addresses, sizeof(addresses)/sizeof(addresses[0]));
	if (!numaddresses)
	{
		M_Print (basex+(9*8)+10, y, "NONE KNOWN");
		y += 8;
	}
	else
	{
		M_Print(basex + (9 * 8) + 10, y, va("local: %s", addresses[0]));
		y += 8;

		M_Print(basex + (9 * 8) + 10, y, va("ext:   %s", my_public_ip));
		y += 8;
	}
#else
	if (IPXConfig)
	{
		M_Print (basex+9*8, y, my_ipx_address);
		y+=8;
	}
	else
	{
		if (ipv4Available && ipv6Available)
		{
			M_Print (basex+9*8, y, my_ipv4_address);
			y+=8;
			M_Print (basex+9*8, y, my_ipv6_address);
			y+=8;
		}
		else
		{
			if (ipv4Available)
				M_Print (basex+9*8, y, my_ipv4_address);
			if (ipv6Available)
				M_Print (basex+9*8, y, my_ipv6_address);
			y+=8;
		}
	}
#endif

	y+=8;	//for the port's box
	M_Print (basex, y, "Port:");
	M_DrawTextBox (basex+8*10, y-8, 6, 1);
	M_Print (basex+9*10, y, lanConfig_portname);
	if (lanConfig_cursor == 0)
	{
		M_DrawCharacter (basex+9*10 + 8*strlen(lanConfig_portname), y, 10+((int)(realtime*4)&1));
		M_DrawCharacter (basex-10, y, 12+((int)(realtime*4)&1));
	}
	y += 8;

	if (StartingGame)
	{
		y += 8;
		M_Print(basex, y, "Protocol:");


		// Get the protocol description based on the current cursor value
		const char* protocolDescription = GetProtocolDescription(lanConfig_protocol_cursor);

		// Print the protocol description
		M_Print(basex + 9 * 9 + 1, y, protocolDescription);

		if (lanConfig_cursor == 1)
		{
			M_DrawCharacter(basex - 8, y, 12 + ((int)(realtime * 4) & 1));
		}

		y += 16;
	}

	if (JoiningGame)
	{
		M_Print (basex, y, "Search for local games...");
		if (lanConfig_cursor == 1)
			M_DrawCharacter (basex-8, y, 12+((int)(realtime*4)&1));
		y+=8;

		M_Print (basex, y, "Search for public games...");
		if (lanConfig_cursor == 2)
			M_DrawCharacter (basex-8, y, 12+((int)(realtime*4)&1));
		y+=8;

		M_Print(basex, y, "History"); // woods #historymenu
		if (lanConfig_cursor == 3)
			M_DrawCharacter(basex - 8, y, 12 + ((int)(realtime * 4) & 1));
		y += 8;

		M_Print(basex, y, "Bookmarks"); // woods #bookmarksmenu
		if (lanConfig_cursor == 4)
			M_DrawCharacter(basex - 8, y, 12 + ((int)(realtime * 4) & 1));
		y += 8;

		M_Print (basex, y, "Join game at:");
		y+=24;
		M_DrawTextBox (basex+8, y-8, 22, 1);
		M_Print (basex+16, y, lanConfig_joinname);
		if (lanConfig_cursor == 5) // woods #historymenu #bookmarksmenu
		{
			M_DrawCharacter (basex+16 + 8*strlen(lanConfig_joinname), y, 10+((int)(realtime*4)&1));
			M_DrawCharacter (basex-8, y, 12+((int)(realtime*4)&1));
		}
		y += 16;
	}
	else
	{
		M_DrawTextBox (basex, y-8, 2, 1);
		M_Print (basex+8, y, "OK");
		if (lanConfig_cursor == 2)
			M_DrawCharacter (basex-8, y, 12+((int)(realtime*4)&1));
		y += 16;
	}

	if (*m_return_reason)
		M_PrintWhite (basex, 148, m_return_reason);
}

void M_LanConfig_Key (int key)
{
	int		l;

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
		lanConfig_cursor--;

		if (StartingGame) {
			if (lanConfig_cursor < 0) {
				lanConfig_cursor = NUM_LANCONFIG_CMDS_NEWGAME - 1;
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
		lanConfig_cursor++;

		if (StartingGame) {
			if (lanConfig_cursor >= NUM_LANCONFIG_CMDS_NEWGAME) {
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
		if (StartingGame && lanConfig_cursor == 1)
		{
			S_LocalSound("misc/menu1.wav");
			lanConfig_protocol_cursor--;
			if (lanConfig_protocol_cursor < 0)
				lanConfig_protocol_cursor = 5; // Wrap around to the last protocol

			SetProtocol(lanConfig_protocol_cursor);
		}
		break;

	case K_MWHEELDOWN:
	case K_RIGHTARROW:
		if (StartingGame && lanConfig_cursor == 1)
		{
			S_LocalSound("misc/menu1.wav");
			lanConfig_protocol_cursor++;
			if (lanConfig_protocol_cursor > 5)
				lanConfig_protocol_cursor = 0; // Wrap around to the first protocol

			SetProtocol(lanConfig_protocol_cursor);
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1: // woods #mousemenu
		if (lanConfig_cursor == 0)
			break;

		m_entersound = true;

		M_ConfigureNetSubsystem ();

		if (StartingGame)
		{

			if (lanConfig_cursor == 1)
			{
				S_LocalSound("misc/menu1.wav");
				lanConfig_protocol_cursor++;
				if (lanConfig_protocol_cursor > 9)
					lanConfig_protocol_cursor = 0; // Wrap around to the first protocol

				SetProtocol(lanConfig_protocol_cursor);
			}
			if (lanConfig_cursor == 2)
				M_Menu_GameOptions_f();
		}
		else
		{
			if (lanConfig_cursor == 1)
				M_Menu_Search_f(SLIST_LAN);
			else if (lanConfig_cursor == 2)
				M_Menu_Search_f(SLIST_INTERNET);
			else if (lanConfig_cursor == 3) // woods #historymenu
				M_Menu_History_f ();
			else if (lanConfig_cursor == 4) // woods #bookmarksmenu
				M_Menu_Bookmarks_f();
			else if (lanConfig_cursor == 5)
			{
				m_return_state = m_state;
				m_return_onerror = true;
				key_dest = key_game;
				m_state = m_none;
				IN_UpdateGrabs();
				Cbuf_AddText ( va ("connect \"%s\"\n", lanConfig_joinname) );
			}
		}

		break;

	case K_BACKSPACE:
		if (lanConfig_cursor == 0)
		{
			if (strlen(lanConfig_portname))
				lanConfig_portname[strlen(lanConfig_portname)-1] = 0;
		}

		if (lanConfig_cursor == 5) // woods #historymenu #bookmarksmenu
		{
			if (strlen(lanConfig_joinname))
				lanConfig_joinname[strlen(lanConfig_joinname)-1] = 0;
		}
		break;
	}

	if (StartingGame && lanConfig_cursor >= 3)
	{
		if (key == K_UPARROW)
			lanConfig_cursor = 1;
		else
			lanConfig_cursor = 0;
	}

	l =  Q_atoi(lanConfig_portname);
	if (l > 65535)
		l = lanConfig_port;
	else
		lanConfig_port = l;
	sprintf(lanConfig_portname, "%u", lanConfig_port);
}


void M_LanConfig_Char (int key)
{
	int l;

	switch (lanConfig_cursor)
	{
	case 0:
		if (key < '0' || key > '9')
			return;
		l = strlen(lanConfig_portname);
		if (l < 5)
		{
			lanConfig_portname[l+1] = 0;
			lanConfig_portname[l] = key;
		}
		break;
	case 5: // woods #historymenu #bookmarksmenu
		l = strlen(lanConfig_joinname);
		if (l < 21)
		{
			lanConfig_joinname[l+1] = 0;
			lanConfig_joinname[l] = key;
		}
		break;
	}
}

//=============================================================================

// woods #historymenu

/* Connection History menu */

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

//=============================================================================

// woods #bookmarksmenu

/* Bookmarks menu */

#define MAX_VIS_BOOKMARKS	16

void FileList_Add(const char* name, const char* data, filelist_item_t** list);

static qboolean bookmarks_edit_new = false;
static qboolean bookmarks_edit_shortcut = false;

typedef struct
{
	const char* name;
	const char* alias;
	qboolean	active;
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

static void M_Bookmarks_Add(const char* name, const char* alias)
{
	if (!strcmp(alias, ""))
		return;

	bookmarksitem_t bookmarks;
	bookmarks.name = name;
	bookmarks.alias = alias;  // Set the alias
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
		m_return_state = m_state;
		m_return_onerror = true;
		key_dest = key_game;
		m_state = m_none;
		IN_UpdateGrabs();
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
			Write_List(bookmarkslist, BOOKMARKSLIST);
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

static int		bookmarks_edit_cursor = 2;
static int		bookmarks_edit_cursor_table[] = { 54, 86, 106 }; // woods add value, change position #namemaker #colorbar

static char temp_alias[45];
static char temp_name[45];

#define	NUM_BOOKMARKS_EDIT_CMDS	3

void M_Menu_Bookmarks_Edit_f (void)
{
	key_dest = key_menu;
	m_state = m_bookmarks_edit;
	m_entersound = true;
	IN_UpdateGrabs();

	bookmarks_edit_cursor = 2;

	if (bookmarks_edit_new)
	{
		if (cls.state == ca_connected)
			q_snprintf(temp_name, sizeof(temp_name), "%s", lastmphost);
		else
			temp_name[0] = 0;
		temp_alias[0] = 0;

	}
	else if (bookmarksmenu.list.cursor >= 0 && bookmarksmenu.list.cursor < bookmarksmenu.list.numitems)
	{
		strncpy(temp_alias, bookmarksmenu.items[bookmarksmenu.list.cursor].alias, sizeof(temp_alias) - 2);
		strncpy(temp_name, bookmarksmenu.items[bookmarksmenu.list.cursor].name, sizeof(temp_name) - 2);
	}
	else 
	{
		M_Menu_Bookmarks_f();  // Fall back to the bookmarks menu if the index is invalid
		return;
	}
}

void M_Shortcut_Bookmarks_Edit_f(void)
{
	bookmarks_edit_new = true;
	bookmarks_edit_shortcut = true;
	M_Menu_Bookmarks_Edit_f();
}


void M_Bookmarks_Edit_Draw(void)
{
	M_Print(10, 40, "Hostname/IP");
	M_DrawTextBox(6, 46, 38, 1);
	M_PrintWhite(14, 54, temp_name);

	M_Print(10, 72, "Bookmark Name");
	M_DrawTextBox(6, 78, 38, 1);
	M_PrintWhite(14, 86, temp_alias);

	M_DrawTextBox(6, 106 - 8, 14, 1);
	M_Print(15, 106, "Accept Changes");


	M_DrawCharacter(0, bookmarks_edit_cursor_table[bookmarks_edit_cursor], 12 + ((int)(realtime * 4) & 1));

	if (bookmarks_edit_cursor == 0)
		M_DrawCharacter(13 + 8 * strlen(temp_name), bookmarks_edit_cursor_table[bookmarks_edit_cursor], 10 + ((int)(realtime * 4) & 1));

	if (bookmarks_edit_cursor == 1)
		M_DrawCharacter(13 + 8 * strlen(temp_alias), bookmarks_edit_cursor_table[bookmarks_edit_cursor], 10 + ((int)(realtime * 4) & 1));
}

void M_Bookmarks_Edit_Key(int k)
{

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
		bookmarks_edit_cursor--;
		if (bookmarks_edit_cursor < 0)
			bookmarks_edit_cursor = NUM_BOOKMARKS_EDIT_CMDS - 1;
		break;

	case K_DOWNARROW:
	case K_TAB:
		S_LocalSound("misc/menu1.wav");
		bookmarks_edit_cursor++;
		if (bookmarks_edit_cursor >= NUM_BOOKMARKS_EDIT_CMDS)
			bookmarks_edit_cursor = 0;
		break;

	case K_MWHEELDOWN:
	case K_LEFTARROW:
		if (bookmarks_edit_cursor < 2)
			return;
		S_LocalSound("misc/menu3.wav");
		break;
	case K_MWHEELUP:
	case K_RIGHTARROW:
		if (bookmarks_edit_cursor < 2)
			return;
		S_LocalSound("misc/menu3.wav");
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1: // woods #mousemenu
		if (bookmarks_edit_cursor == 0 || bookmarks_edit_cursor == 1)
			return;

		// (Accept Changes)
		if (!bookmarks_edit_new) // edit + save
		{
			if ((Q_strcmp(bookmarksmenu.items[bookmarksmenu.list.cursor].alias, temp_alias) != 0 || Q_strcmp(bookmarksmenu.items[bookmarksmenu.list.cursor].name, temp_name) != 0)
				&& (strcmp(temp_alias, "") && (Valid_IP(temp_name) || Valid_Domain(temp_name))))
			{
				FileList_Subtract(bookmarksmenu.items[bookmarksmenu.list.cursor].name, &bookmarkslist);
				FileList_Add(temp_name, temp_alias, &bookmarkslist);
				Write_List(bookmarkslist, BOOKMARKSLIST);
				bookmarks_edit_new = false;

			}
		}
		
		if (bookmarks_edit_new && (strcmp(temp_alias, "") && (Valid_IP(temp_name) || Valid_Domain(temp_name)))) // new + save
			{
			FileList_Add(temp_name, temp_alias, &bookmarkslist);
			Write_List(bookmarkslist, BOOKMARKSLIST);
			bookmarks_edit_new = false;
		}

		m_entersound = true;

		M_Menu_Bookmarks_f();
		break;

	case K_BACKSPACE:
		if (bookmarks_edit_cursor == 0)
		{
			if (strlen(temp_name))
				temp_name[strlen(temp_name) - 1] = 0;
		}

		if (bookmarks_edit_cursor == 1)
		{
			if (strlen(temp_alias))
				temp_alias[strlen(temp_alias) - 1] = 0;
		}
		break;
	}
}

void M_Bookmarks_Edit_Char(int k)
{
	int l;

	switch (bookmarks_edit_cursor)
	{
	case 0:
		l = strlen(temp_name);
		if (l < 37)
		{
			temp_name[l + 1] = 0;
			temp_name[l] = k;
		}
		break;
	case 1:
		l = strlen(temp_alias);
		if (l < 37)
		{
			temp_alias[l + 1] = 0;
			temp_alias[l] = k;
		}
		break;
	}
}

qboolean M_Bookmarks_Edit_TextEntry(void)
{
	return (bookmarks_edit_cursor == 0 || bookmarks_edit_cursor == 1);
}

void M_Bookmarks_Edit_Mousemove(int cx, int cy) // woods #mousemenu
{
	M_UpdateCursorWithTable(cy, bookmarks_edit_cursor_table, NUM_BOOKMARKS_EDIT_CMDS, &bookmarks_edit_cursor);
}

qboolean M_LanConfig_TextEntry (void)
{
	return (lanConfig_cursor == 0 || lanConfig_cursor == 5); // woods #historymenu #bookmarksmenu
}

void M_LanConfig_Mousemove(int cx, int cy) // woods #mousemenu
{
	int numCommands = StartingGame ? NUM_LANCONFIG_CMDS_NEWGAME : NUM_LANCONFIG_CMDS_JOINGAME;
	M_UpdateCursorWithTable(cy, lanConfig_cursor_ptr, numCommands, &lanConfig_cursor);
}

//=============================================================================
/* GAME OPTIONS MENU */

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

void M_Menu_GameOptions_f (void)
{
	key_dest = key_menu;
	m_state = m_gameoptions;
	IN_UpdateGrabs();
	m_entersound = true;
	if (maxplayers == 0)
		maxplayers = svs.maxclients;
	if (maxplayers < 2)
		maxplayers = 16;
}


int gameoptions_cursor_table[] = {40, 56, 64, 72, 80, 88, 96, 104, 120, 128};
#define	NUM_GAMEOPTIONS	10
int		gameoptions_cursor;

void M_GameOptions_Draw (void)
{
	qpic_t	*p;
	int y = 40;

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

	M_Print (0, y, "        Teamplay");
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
	if (skill.value == 0)
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

	y+=8;

	M_Print (0, y, "         Episode");
	// MED 01/06/97 added hipnotic episodes
	if (hipnotic)
		M_Print (160, y, hipnoticepisodes[startepisode].description);
	// PGM 01/07/97 added rogue episodes
	else if (rogue)
		M_Print (160, y, rogueepisodes[startepisode].description);
	else
		M_Print (160, y, episodes[startepisode].description);
	y+=8;

	M_Print (0, y, "           Level");
	// MED 01/06/97 added hipnotic episodes
	if (hipnotic)
	{
		M_Print (160, y, hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel].description);
		M_Print (160, y+8, hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel].name);
	}
	// PGM 01/07/97 added rogue episodes
	else if (rogue)
	{
		M_Print (160, y, roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].description);
		M_Print (160, y+8, roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].name);
	}
	else
	{
		M_Print (160, y, levels[episodes[startepisode].firstLevel + startlevel].description);
		M_Print (160, y+8, levels[episodes[startepisode].firstLevel + startlevel].name);
	}
	y+=8;

// line cursor
	M_DrawCharacter (144, gameoptions_cursor_table[gameoptions_cursor], 12+((int)(realtime*4)&1));
}


void M_NetStart_Change (int dir)
{
	int count;
	float	f;

	switch (gameoptions_cursor)
	{
	case 1:
		maxplayers += dir;
		if (maxplayers > svs.maxclientslimit)
			maxplayers = svs.maxclientslimit;
		if (maxplayers < 2)
			maxplayers = 2;
		break;

	case 2:
		Cvar_SetQuick (&sv_public, sv_public.value ? "0" : "1");
		break;

	case 3:
		Cvar_Set ("coop", coop.value ? "0" : "1");
		break;

	case 4:
		count = (rogue) ? 6 : 2;
		f = teamplay.value + dir;
		if (f > count)	f = 0;
		else if (f < 0)	f = count;
		Cvar_SetValue ("teamplay", f);
		break;

	case 5:
		f = skill.value + dir;
		if (f > 3)	f = 0;
		else if (f < 0)	f = 3;
		Cvar_SetValue ("skill", f);
		break;

	case 6:
		f = fraglimit.value + dir * 10;
		if (f > 100)	f = 0;
		else if (f < 0)	f = 100;
		Cvar_SetValue ("fraglimit", f);
		break;

	case 7:
		f = timelimit.value + dir * 5;
		if (f > 60)	f = 0;
		else if (f < 0)	f = 60;
		Cvar_SetValue ("timelimit", f);
		break;

	case 8:
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

	case 9:
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
	}
}

void M_GameOptions_Key (int key)
{
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
		gameoptions_cursor--;
		if (gameoptions_cursor < 0)
			gameoptions_cursor = NUM_GAMEOPTIONS-1;
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		gameoptions_cursor++;
		if (gameoptions_cursor >= NUM_GAMEOPTIONS)
			gameoptions_cursor = 0;
		break;

	case K_LEFTARROW:
	case K_MWHEELDOWN:
	//case K_MOUSE2:
		if (gameoptions_cursor == 0)
			break;
		S_LocalSound ("misc/menu3.wav");
		M_NetStart_Change (-1);
		break;

	case K_RIGHTARROW:
	case K_MWHEELUP:
		if (gameoptions_cursor == 0)
			break;
		S_LocalSound ("misc/menu3.wav");
		M_NetStart_Change (1);
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1: // woods #mousemenu
		S_LocalSound ("misc/menu2.wav");
		if (gameoptions_cursor == 0)
		{
			if (sv.active)
				Cbuf_AddText ("disconnect\n");
			Cbuf_AddText ("listen 0\n");	// so host_netport will be re-examined
			Cbuf_AddText ( va ("maxplayers %u\n", maxplayers) );
			SCR_BeginLoadingPlaque ();

			if (hipnotic)
				Cbuf_AddText ( va ("map %s\n", hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel].name) );
			else if (rogue)
				Cbuf_AddText ( va ("map %s\n", roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].name) );
			else
				Cbuf_AddText ( va ("map %s\n", levels[episodes[startepisode].firstLevel + startlevel].name) );

			return;
		}

		M_NetStart_Change (1);
		break;
	}
}

void M_GameOptions_Mousemove(int cx, int cy) // woods #mousemenu
{
	M_UpdateCursorWithTable(cy, gameoptions_cursor_table, NUM_GAMEOPTIONS, &gameoptions_cursor);
}

//=============================================================================
/* SEARCH MENU */

qboolean	searchComplete = false;
double		searchCompleteTime;
enum slistScope_e searchLastScope = SLIST_LAN;
void ResetHostlist (void); // woods #resethostlist

void M_Menu_Search_f (enum slistScope_e scope)
{
	key_dest = key_menu;
	m_state = m_search;
	IN_UpdateGrabs();
	m_entersound = false;
	slistSilent = true;
	if (searchLastScope != scope) // woods #resethostlist
		ResetHostlist();
	slistScope = searchLastScope = scope;
	searchComplete = false;
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
	M_Print (x, 40, "Searching...");

	if(slistInProgress)
	{
		NET_Poll();
		return;
	}

	if (! searchComplete)
	{
		searchComplete = true;
		searchCompleteTime = realtime;
	}

	if (hostCacheCount)
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

//=============================================================================
/* SLIST MENU */
// woods #serversmenu #icmp

#define MAX_VIS_SERVERS 18
#define PING_COOLDOWN 2.0
#define MAX_PING_QUEUE 5

typedef struct {
	const char* name;
	const char* ip;
	int users;
	int maxusers;
	const char* map;
	int ping;
	qboolean active;
	double lastPingTime;
	qboolean isLoading;  // New flag to indicate loading state
} servertitem_t;

static struct {
	menulist_t list;
	enum m_state_e prev;
	int x, y, cols;
	int prev_cursor;
	menuticker_t ticker;
	qboolean scrollbar_grab;
	servertitem_t* items;
	int servercount;
	int slist_first;
	int sorted;
	SDL_Thread* pingThreads[2];
	qboolean initialPingComplete;
	int pingQueue[MAX_PING_QUEUE];
	int pingQueueSize;
	qboolean pingThreadRunning;
	SDL_Thread* pingThread;
} serversmenu;

//=============================================================================
// woods servers.quakeone.com support curl+json parsing #serversmenu
//=============================================================================

static volatile qboolean pingThreadsShouldExit = false;
SDL_mutex* pingMutex = NULL;

int ICMP_Ping_Host(const char* host);

void InitializePingMutex(void)
{
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
	if (index < 0 || index >= serversmenu.servercount)
		return;

	SDL_LockMutex(pingMutex);
	const char* serverIP = COM_StripPort(serversmenu.items[index].ip);
	int previousPing = serversmenu.items[index].ping;
	serversmenu.items[index].isLoading = true;  // Set loading flag
	SDL_UnlockMutex(pingMutex);

	int ping = ICMP_Ping_Host(serverIP);

	SDL_LockMutex(pingMutex);
	if (ping >= 0) {
		serversmenu.items[index].ping = ping;
	}
	else if (previousPing >= 0) {
		serversmenu.items[index].ping = previousPing;
	}
	else {
		serversmenu.items[index].ping = -1;  // -1 indicates "failed"
	}
	serversmenu.items[index].isLoading = false;  // Clear loading flag
	SDL_UnlockMutex(pingMutex);

	free((void*)serverIP);
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

void TriggerServerPing(int index)
{
	SDL_LockMutex(pingMutex);
	qboolean canPing = serversmenu.initialPingComplete;
	SDL_UnlockMutex(pingMutex);

	if (!canPing)
		return;

	if (index >= 0 && index < serversmenu.servercount)
	{
		double currentTime = Sys_DoubleTime();
		if (currentTime - serversmenu.items[index].lastPingTime >= PING_COOLDOWN)
		{
			SDL_LockMutex(pingMutex);
			if (serversmenu.pingQueueSize < MAX_PING_QUEUE)
			{
				serversmenu.pingQueue[serversmenu.pingQueueSize++] = index;
				serversmenu.items[index].lastPingTime = currentTime;
			}
			if (!serversmenu.pingThreadRunning)
			{
				serversmenu.pingThread = SDL_CreateThread(ProcessPingQueue, "PingQueueThread", NULL);
				if (serversmenu.pingThread == NULL)
					Con_DPrintf("SDL_CreateThread failed: %s\n", SDL_GetError());
				else
					serversmenu.pingThreadRunning = true;
			}
			SDL_UnlockMutex(pingMutex);
		}
	}
}

int PingServers(void* data)
{
	if (!data) {
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
		if (serversmenu.items && serversmenu.items[i].ip) {
			const char* serverIP = COM_StripPort(serversmenu.items[i].ip);
			SDL_UnlockMutex(pingMutex);

			int ping = ICMP_Ping_Host(serverIP);

			SDL_LockMutex(pingMutex);
			serversmenu.items[i].ping = (ping >= 0) ? ping : -1;
			SDL_UnlockMutex(pingMutex);

			free((void*)serverIP);
		}
		else {
			SDL_UnlockMutex(pingMutex);
			Con_DPrintf("Invalid server item or IP\n");
		}
	}

	free(data);

	// Check if this is the last thread to finish
	SDL_LockMutex(pingMutex);
	serversmenu.initialPingComplete = true;
	SDL_UnlockMutex(pingMutex);

	return 0;
}

void WaitForPingThreads(SDL_Thread* thread1, SDL_Thread* thread2)
{
	pingThreadsShouldExit = true; // Signal threads to exit

	if (thread1)
		SDL_WaitThread(thread1, NULL);
	if (thread2)
		SDL_WaitThread(thread2, NULL);

	pingThreadsShouldExit = false; // Reset the exit flag

	SDL_LockMutex(pingMutex);
	serversmenu.initialPingComplete = true;
	SDL_UnlockMutex(pingMutex);
}

void PingAllServers(void)
{
	int servercount = serversmenu.servercount;
	int mid = servercount / 2;

	int* range1 = (int*)malloc(2 * sizeof(int));
	int* range2 = (int*)malloc(2 * sizeof(int));

	if (!range1 || !range2) {
		Con_DPrintf("Memory allocation failed\n");
		if (range1) free(range1);
		if (range2) free(range2);
		return;
	}

	range1[0] = 0;
	range1[1] = mid;

	range2[0] = mid;
	range2[1] = servercount;

	serversmenu.pingThreads[0] = SDL_CreateThread(PingServers, "PingServersThread1", (void*)range1);
	serversmenu.pingThreads[1] = SDL_CreateThread(PingServers, "PingServersThread2", (void*)range2);

	if (serversmenu.pingThreads[0] == NULL)
		Con_DPrintf("SDL_CreateThread failed: %s\n", SDL_GetError());

	if (serversmenu.pingThreads[1] == NULL)
		Con_DPrintf("SDL_CreateThread failed: %s\n", SDL_GetError());
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

		const jsonentry_t* playersArray = JSON_Find(serverEntry, "players", JSON_ARRAY);

		int numPlayers = 0;
		if (playersArray)
		{
			const jsonentry_t* playerEntry;
			for (playerEntry = playersArray->firstchild; playerEntry; playerEntry = playerEntry->next) 
			{
				numPlayers++;
			}
		}

		qboolean status;
		setStatusFlagBasedOnTimestamp(timestamp, lastQuery, &status);

		if (!status || !name || !address || !port || !gameId || (*gameId != 0 && (*gameId != 5 || !parameters || !strstr(parameters, "fte")))) continue; // Skip if essential info is missing or server is down

		*items = realloc(*items, sizeof(servertitem_t) * (*actualServerCount + 1));
		if (!*items) {
			Con_DPrintf("Memory allocation failed.\n");
			break;
		}

		size_t addressLength = strlen(address) + 1 /* colon */ + 6 /* max length of port number */ + 1 /* null terminator */;
		char* addressWithPort = malloc(addressLength);
		if (!addressWithPort) {
			Con_DPrintf("Memory allocation for address with port failed.\n");
			break;
		}
		q_snprintf(addressWithPort, addressLength, "%s:%d", address, (int)*port);

		servertitem_t* newItem = &(*items)[*actualServerCount];
		newItem->name = strdup(name ? name : "Unknown");
		newItem->ip = strdup(addressWithPort);
		free(addressWithPort);
		newItem->users = numPlayers;
		newItem->maxusers = maxPlayers ? (int)*maxPlayers : 0;
		newItem->map = strdup(map ? map : "Unknown");
		newItem->active = true;
		newItem->ping = -1;

		(*actualServerCount)++;
	}

	JSON_Free(json);
}

void CurlServerList (servertitem_t** items, int* actualServerCount) 
{
	CURL* curl;
	CURLcode res;
	struct MemoryStruct chunk;

	chunk.memory = malloc(1);  // Initial allocation
	chunk.size = 0;    // No data at this point

	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();

	if (curl) 
	{
		curl_easy_setopt(curl, CURLOPT_URL, "https://servers.quakeone.com/api/servers/status");
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

		res = curl_easy_perform(curl);
		if (res != CURLE_OK)
			Con_DPrintf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
		else
			populateServersFromJSON(chunk.memory, items, actualServerCount);

		free(chunk.memory);
		curl_easy_cleanup(curl);
	}

	curl_global_cleanup();
}

int compareServerUsers (const void* a, const void* b) 
{
	const servertitem_t* serverA = (const servertitem_t*)a;
	const servertitem_t* serverB = (const servertitem_t*)b;

	int userDifference = serverB->users - serverA->users; // First, sort by user count in descending order
	if (userDifference != 0) 
		return userDifference;

	return q_strcasecmp(serverA->name, serverB->name);
	return q_strcasecmp(serverA->name, serverB->name);
}

void RemoveDuplicateServers (servertitem_t** items, int* actualServerCount) 
{
	int writeIndex = 0;
	for (int i = 0; i < *actualServerCount; i++) {
		qboolean isDuplicate = false;
		for (int j = 0; j < i; j++) {
			if (strcmp((*items)[i].ip, (*items)[j].ip) == 0) {
				isDuplicate = true;
				break;
			}
		}
		if (!isDuplicate) {
			if (writeIndex != i) {
				(*items)[writeIndex] = (*items)[i];
			}
			writeIndex++;
		}
	}
	*actualServerCount = writeIndex;
}

void FetchAndSortServers (void) 
{
	free(serversmenu.items);
	serversmenu.items = NULL;
	int actualServerCount = 0;

	for (int i = 0; i < HOSTCACHESIZE; i++) // Fetch and add servers from the dp list
	{
		const char* serverName = NET_SlistPrintServerInfo(i, SERVER_NAME);
		const char* serverIP = NET_SlistPrintServerInfo(i, SERVER_CNAME);
		int users = atoi(NET_SlistPrintServerInfo(i, SERVER_USERS));
		int maxusers = atoi(NET_SlistPrintServerInfo(i, SERVER_MAX_USERS));
		const char* map = NET_SlistPrintServerInfo(i, SERVER_MAP);

		if (serverName && serverName[0] != '\0') 
		{
			serversmenu.items = (servertitem_t*)realloc(serversmenu.items, sizeof(servertitem_t) * (actualServerCount + 1));

			serversmenu.items[actualServerCount].name = strdup(serverName);
			serversmenu.items[actualServerCount].ip = strdup(serverIP);
			serversmenu.items[actualServerCount].users = users;
			serversmenu.items[actualServerCount].maxusers = maxusers;
			serversmenu.items[actualServerCount].map = strdup(map);
			serversmenu.items[actualServerCount].active = true;
			serversmenu.items[actualServerCount].ping = -1;
			serversmenu.items[actualServerCount].isLoading = false;

			actualServerCount++;
		}
	}

	CurlServerList (&serversmenu.items, &actualServerCount);// fetch and add servers from the server.quakeone.com json API

	RemoveDuplicateServers(&serversmenu.items, &actualServerCount);

	// Sorting servers based on the number of users in descending order, if more than one server is present
	if (actualServerCount > 1)
		qsort(serversmenu.items, actualServerCount, sizeof(servertitem_t), compareServerUsers);

	serversmenu.servercount = actualServerCount;
	serversmenu.list.numitems = actualServerCount;

	if (serversmenu.list.cursor >= actualServerCount)
		serversmenu.list.cursor = actualServerCount > 0 ? actualServerCount - 1 : 0;
	if (serversmenu.slist_first > serversmenu.list.cursor)
		serversmenu.slist_first = serversmenu.list.cursor;
}

void M_Menu_ServerList_f (void)
{
	key_dest = key_menu;
	m_state = m_slist;
	IN_UpdateGrabs();
	m_entersound = true;

	serversmenu.list.cursor = -1;
	serversmenu.list.scroll = 0;
	serversmenu.list.numitems = 0;
	serversmenu.servercount = 0;
	serversmenu.scrollbar_grab = false;
	serversmenu.initialPingComplete = false;
	serversmenu.pingQueueSize = 0;
	serversmenu.pingThreadRunning = false;
	pingThreadsShouldExit = false;

	FetchAndSortServers();
	InitializePingMutex();
	PingAllServers();

	serversmenu.list.viewsize = MAX_VIS_SERVERS;

	M_Ticker_Init(&serversmenu.ticker);

	M_List_CenterCursor(&serversmenu.list);
}

void M_ServerList_Draw (void)
{
	int x, y, i, cols;
	int firstvis, numvis;

	x = 16;
	y = 28;
	cols = 36;

	serversmenu.x = x;
	serversmenu.y = y;
	serversmenu.cols = cols;

	if (!keydown[K_MOUSE1])
		serversmenu.scrollbar_grab = false;

	if (serversmenu.prev_cursor != serversmenu.list.cursor) {
		serversmenu.prev_cursor = serversmenu.list.cursor;
		M_Ticker_Init(&serversmenu.ticker);
	}
	else {
		M_Ticker_Update(&serversmenu.ticker);
	}

	Draw_String(x, y - 28, "Servers");
	M_DrawQuakeBar(x - 8, y - 16, cols + 2);

	M_List_GetVisibleRange(&serversmenu.list, &firstvis, &numvis);
	for (i = 0; i < numvis; i++) {
		int idx = i + firstvis;
		qboolean selected = (idx == serversmenu.list.cursor);

		servertitem_t server;
		server.active = false;

		if (cls.state == ca_connected) // highlight if connected to a server in the list
		{ 
			if (!strcmp(lastmphost, serversmenu.items[idx].ip))
				server.active = true;
			else if (Valid_Domain(lastmphost))
				server.active = !strcmp((ResolveHostname(lastmphost)), serversmenu.items[idx].ip);
			else if (Valid_IP(lastmphost))
				server.active = !strcmp(lastmphost, serversmenu.items[idx].ip);
		}
			else
				server.active = false;

		char serverStr[40];

		char pingStr[8];

		if (serversmenu.items[idx].ping == -1)
			pingStr[0] = '\0';
		else if (serversmenu.items[idx].isLoading)
			q_snprintf(pingStr, sizeof(pingStr), "%3i", serversmenu.items[idx].ping);	
		else 
			q_snprintf(pingStr, sizeof(pingStr), "%3i", serversmenu.items[idx].ping);

		q_snprintf(serverStr, sizeof(serverStr), "%-16.16s  %-6.6s %2u/%2u %s\n", serversmenu.items[idx].name, serversmenu.items[idx].map, serversmenu.items[idx].users, serversmenu.items[idx].maxusers, pingStr);

		if (server.active)
			M_PrintWhite(x, y + i * 8, serverStr);
		else
			M_Print(x, y + i * 8, serverStr);

		if (selected)
			M_DrawCharacter(x - 8, y + i * 8, 12 + ((int)(realtime * 4) & 1));

		q_snprintf(serverStr, sizeof(serverStr), "%-34.34s", serversmenu.items[idx].name);

		if (selected)
			M_PrintWhite(x, y + serversmenu.list.viewsize * 8 + 12, serverStr);

		q_snprintf(serverStr, sizeof(serverStr), "%-34.34s", serversmenu.items[idx].ip);

		if (selected)
			M_PrintWhite(x, y + serversmenu.list.viewsize * 8 + 20, serverStr);
	}

	if (M_List_GetOverflow(&serversmenu.list) > 0) {
		M_List_DrawScrollbar(&serversmenu.list, x + cols * 8 - 8, y);

		if (serversmenu.list.scroll > 0)
			M_DrawEllipsisBar(x, y - 8, cols);
		if (serversmenu.list.scroll + serversmenu.list.viewsize < serversmenu.list.numitems)
			M_DrawEllipsisBar(x, y + serversmenu.list.viewsize * 8, cols);
	}
}

qboolean M_Servers_Match(int index, char initial)
{
	return q_tolower(serversmenu.items[index].name[0]) == initial;
}

void CleanupPingThreads()
{
	WaitForPingThreads(serversmenu.pingThreads[0], serversmenu.pingThreads[1]);

	if (!serversmenu.pingThreadRunning)
	{
		pingThreadsShouldExit = true;
		if (serversmenu.pingThread)
			SDL_WaitThread(serversmenu.pingThread, NULL);
	}

	CleanupPingMutex();
}

void M_ServerList_Key(int key)
{

	int x, y; // woods #mousemenu
	int prev_cursor = serversmenu.list.cursor;

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

	if (M_List_Key(&serversmenu.list, key))
	{
		if (serversmenu.list.cursor != prev_cursor)
			TriggerServerPing(serversmenu.list.cursor);
	
		return;
	}

		if (M_List_CycleMatch(&serversmenu.list, key, M_Servers_Match))
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
		m_return_state = m_state;
		m_return_onerror = true;
		key_dest = key_game;
		m_state = m_none;
		IN_UpdateGrabs();
		Cbuf_AddText(va("connect \"%s\"\n", serversmenu.items[serversmenu.list.cursor].ip));
		CleanupPingThreads();
		break;

	case K_MOUSE1: // woods #mousemenu
		x = m_mousex - serversmenu.x - (serversmenu.cols - 1) * 8;
		y = m_mousey - serversmenu.y;
		if (x < -8 || !M_List_UseScrollbar(&serversmenu.list, y))
			goto enter;
		serversmenu.scrollbar_grab = true;
		M_Mods_Mousemove(m_mousex, m_mousey);

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
		M_List_UseScrollbar(&serversmenu.list, cy);
		// Note: no return, we also update the cursor
	}

	M_List_Mousemove(&serversmenu.list, cy);

	if (serversmenu.list.cursor != prev_cursor)
		TriggerServerPing(serversmenu.list.cursor);
}

//=============================================================================

// woods #modsmenu (iw)

/* Mods menu */

#define MAX_VIS_MODS	19

typedef struct
{
	const char* name;
	qboolean	active;
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

static void M_Mods_Add(const char* name)
{
	moditem_t mod;
	mod.name = name;
	mod.active = M_Mods_IsActive(name);
	if (mod.active && modsmenu.list.cursor == -1)
		modsmenu.list.cursor = modsmenu.list.numitems;
	
	// Ensure there's enough space for one more item
	VEC_PUSH(modsmenu.items, mod);

	modsmenu.items[modsmenu.list.numitems] = mod;
	modsmenu.list.numitems++;
}

static void M_Mods_Init(void)
{
	filelist_item_t* item;

	modsmenu.list.viewsize = MAX_VIS_MODS;
	modsmenu.list.cursor = -1;
	modsmenu.list.scroll = 0;
	modsmenu.list.numitems = 0;
	modsmenu.modcount = 0;
	modsmenu.scrollbar_grab = false;
	VEC_CLEAR(modsmenu.items);

	M_Ticker_Init(&modsmenu.ticker);

	for (item = modlist; item; item = item->next)
		M_Mods_Add(item->name);

	if (modsmenu.list.cursor == -1)
		modsmenu.list.cursor = 0;

	M_List_CenterCursor(&modsmenu.list);
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

	Draw_String(x, y - 28, "Mods");
	M_DrawQuakeBar(x - 8, y - 16, cols + 2);

	M_List_GetVisibleRange(&modsmenu.list, &firstvis, &numvis);
	for (i = 0; i < numvis; i++) 
	{
		int idx = i + firstvis;
		int color = modsmenu.items[idx].active ? 0 : 1;
		qboolean selected = (idx == modsmenu.list.cursor);

		M_PrintScroll(x, y + i * 8, (cols - 2) * 8, modsmenu.items[idx].name, selected ? modsmenu.ticker.scroll_time : 0.0, color);

		if (selected)
			M_DrawCharacter(x - 8, y + i * 8, 12 + ((int)(realtime * 4) & 1));
	}

	if (M_List_GetOverflow(&modsmenu.list) > 0)
	{
		M_List_DrawScrollbar(&modsmenu.list, x + cols * 8 - 8, y);

		if (modsmenu.list.scroll > 0)
			M_DrawEllipsisBar(x, y - 8, cols);
		if (modsmenu.list.scroll + modsmenu.list.viewsize < modsmenu.list.numitems)
			M_DrawEllipsisBar(x, y + modsmenu.list.viewsize * 8, cols);
	}
}

qboolean M_Mods_Match(int index, char initial)
{
	return q_tolower(modsmenu.items[index].name[0]) == initial;
}

void M_Mods_Key(int key)
{
	
	int x, y; // woods #mousemenu

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
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		if (modsmenu.prev == m_options)
			M_Menu_Options_f();
		else
			M_Menu_Main_f();
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	enter:
		Cbuf_AddText(va("game %s\n", modsmenu.items[modsmenu.list.cursor].name));
		M_Menu_Main_f();
		break;

	case K_MOUSE1: // woods #mousemenu
		x = m_mousex - modsmenu.x - (modsmenu.cols - 1) * 8;
		y = m_mousey - modsmenu.y;
		if (x < -8 || !M_List_UseScrollbar(&modsmenu.list, y))
			goto enter;
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

	M_List_Mousemove(&modsmenu.list, cy);
}

//=============================================================================

// woods #demosmenu

/* Demos menu */

#define MAX_VIS_DEMOS	19

typedef struct
{
	const char* name;
	const char* date;
	qboolean	active;
} demoitem_t;
	
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
} demosmenu;


static void M_Demos_Add (const char* name, const char* date)
{
	demoitem_t tempDemo;
	tempDemo.name = name;
	tempDemo.date = date;

	Vec_Grow ((void**)&demosmenu.items, sizeof(demoitem_t), demosmenu.list.numitems + 1);

	int insertPos = demosmenu.list.numitems;

	for (int i = 0; i < demosmenu.list.numitems; i++) // Find the correct position to insert the new demo based on the date
	{
		if (q_sortdemos(date, demosmenu.items[i].date) < 0) // Assuming q_sortdemos returns <0 if first date is earlier
		{
			insertPos = i;
			break;
		}
	}

	if (insertPos != demosmenu.list.numitems) // If necessary, shift items to make room for the new demo
		memmove(&demosmenu.items[insertPos + 1], &demosmenu.items[insertPos], sizeof(demoitem_t) * (demosmenu.list.numitems - insertPos));

	demosmenu.items[insertPos] = tempDemo; // Insert the new demo into the calculated position

	if (demosmenu.list.numitems == 0)
		demosmenu.list.cursor = 0;

	demosmenu.list.numitems++;
}

static void M_Demos_ReverseOrder (void)
{
	int i, j;
	demoitem_t temp;

	for (i = 0, j = demosmenu.list.numitems - 1; i < j; i++, j--)
	{
		temp = demosmenu.items[i];
		demosmenu.items[i] = demosmenu.items[j];
		demosmenu.items[j] = temp;
	}
}

static void M_Demos_Init (void)
{
	filelist_item_t* item;

	demosmenu.list.viewsize = MAX_VIS_DEMOS;
	demosmenu.list.cursor = -1;
	demosmenu.list.scroll = 0;
	demosmenu.list.numitems = 0;
	demosmenu.democount = 0;
	demosmenu.scrollbar_grab = false;
	VEC_CLEAR(demosmenu.items);

	M_Ticker_Init (&demosmenu.ticker);

	for (item = demolist; item; item = item->next)
		M_Demos_Add(item->name, item->data);

	if (demosmenu.list.cursor == -1)
		demosmenu.list.cursor = 0;

	M_List_CenterCursor(&demosmenu.list);
	M_Demos_ReverseOrder();
}

void M_Menu_Demos_f (void)
{
	key_dest = key_menu;
	demosmenu.prev = m_state;
	m_state = m_demos;
	m_entersound = true;
	M_Demos_Init();
}

void M_Demos_Draw (void)
{
	int x, y, i, cols;
	int firstvis, numvis;

	x = 16;
	y = 32;
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
		M_Ticker_Update(&demosmenu.ticker);

	Draw_String(x, y - 28, "Demos");
	M_DrawQuakeBar(x - 8, y - 16, cols + 2);

	M_List_GetVisibleRange(&demosmenu.list, &firstvis, &numvis);
	for (i = 0; i < numvis; i++) 
	{
		int idx = i + firstvis;
		qboolean selected = (idx == demosmenu.list.cursor);

		COM_StripExtension(cls.demofilename, demofilename, sizeof(demofilename));

		demosmenu.items[idx].active = !strcmp(demofilename, demosmenu.items[idx].name);

		int color = demosmenu.items[idx].active ? 0 : 1;

		M_PrintScroll(x, y + i * 8, (cols - 2) * 8, demosmenu.items[idx].name, selected ? demosmenu.ticker.scroll_time : 0.0, color);

		if (selected) 
			M_DrawCharacter(x - 8, y + i * 8, 12 + ((int)(realtime * 4) & 1));
	}

	if (M_List_GetOverflow(&demosmenu.list) > 0)
	{
		M_List_DrawScrollbar(&demosmenu.list, x + cols * 8 - 8, y);

		if (demosmenu.list.scroll > 0)
			M_DrawEllipsisBar(x, y - 8, cols);
		if (demosmenu.list.scroll + demosmenu.list.viewsize < demosmenu.list.numitems)
			M_DrawEllipsisBar(x, y + demosmenu.list.viewsize * 8, cols);
	}
}

qboolean M_Demos_Match (int index, char initial)
{
	return q_tolower(demosmenu.items[index].name[0]) == initial;
}

void M_Demos_Key (int key)
{
	int x, y; // woods #mousemenu

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
	case K_BBUTTON:
	case K_MOUSE4: // woods #mousemenu
	case K_MOUSE2:
		if (demosmenu.prev == m_options)
			M_Menu_Options_f();
		else
			M_Menu_Main_f();
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	enter: // woods #mousemenu
		Cbuf_AddText(va("playdemo %s\n", demosmenu.items[demosmenu.list.cursor].name));
		M_Menu_Main_f();
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

void M_Demos_Mousemove(int cx, int cy) // woods #mousemenu
{
	cy -= demosmenu.y;

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

//=============================================================================
/* Credits menu -- used by the 2021 re-release */

void M_Menu_Credits_f (void)
{
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
	{"menu_multiplayer", M_Menu_MultiPlayer_f},
	{"menu_setup", M_Menu_Setup_f},
	{"menu_options", M_Menu_Options_f},
	{"menu_keys", M_Menu_Keys_f},
	{"menu_extras", M_Menu_Extras_f},
	{"menu_video", M_Menu_Video_f},
	{"help", M_Menu_Help_f},
	{"menu_quit", M_Menu_Quit_f},
	{"menu_credits", M_Menu_Credits_f}, // needed by the 2021 re-release
	{"menu_namemaker", M_Menu_NameMaker_f}, // woods #namemaker
	{"namemaker", M_Shortcut_NameMaker_f}, // woods #namemaker
	{"menu_mods", M_Menu_Mods_f}, // woods
	{"menu_demos", M_Menu_Demos_f}, // woods
	{"menu_maps", M_Menu_Maps_f}, // woods
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
			M_Menu_Main_f ();
			return;
		}

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
	Cmd_AddCommand ("togglemenu", M_ToggleMenu_f);
	Cmd_AddCommand ("menu_cmd", MQC_Command_f);
	Cmd_AddCommand ("menu_restart", M_MenuRestart_f);	//qss still loads progs on hunk, so we can't do this safely.

	if (!MQC_Init())
		MQC_Shutdown();
}


void M_Draw (void)
{
	if (cls.menu_qcvm.extfuncs.m_draw)
	{	//Spike -- menuqc
		float s = q_min((float)glwidth / 320.0, (float)glheight / 200.0);
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
		return;

	if (!m_recursiveDraw)
	{
		if (scr_con_current)
		{
			Draw_ConsoleBackground ();
			S_ExtraUpdate ();
		}

		Draw_FadeScreen (); //johnfitz -- fade even if console fills screen
	}
	else
	{
		m_recursiveDraw = false;
	}

	GL_SetCanvas (CANVAS_MENU); //johnfitz

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

	case m_extras:
		M_Extras_Draw ();
		break;

	case m_video:
		M_Video_Draw ();
		break;

	case m_mods: // woods #modsmenu (iw)
		M_Mods_Draw();
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

	if (m_entersound)
	{
		S_LocalSound ("misc/menu2.wav");
		m_entersound = false;
	}

	S_ExtraUpdate ();
}


void M_Keydown (int key)
{
	if (cls.menu_qcvm.extfuncs.m_draw)	//don't get confused.
		return;

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

	case m_extras:
		M_Extras_Key (key);
		return;

	case m_video:
		M_Video_Key (key);
		return;

	case m_mods: // woods #modsmenu (iw)
		M_Mods_Key(key);
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

	case m_video:
		M_Video_Mousemove(x, y);
		return;

	case m_extras:
		M_Extras_Mousemove(x, y);
		return;

	case m_mods:
		M_Mods_Mousemove(x, y);
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
	case m_bookmarks_edit: // woods #bookmarksmenu
		M_Bookmarks_Edit_Char(key);
		return;
	case m_quit:
		M_Quit_Char (key);
		return;
	case m_lanconfig:
		M_LanConfig_Char (key);
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
	case m_bookmarks_edit: // woods #bookmarksmenu
		return M_Bookmarks_Edit_TextEntry();
	case m_quit:
		return M_Quit_TextEntry ();
	case m_lanconfig:
		return M_LanConfig_TextEntry ();
	default:
		return false;
	}
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