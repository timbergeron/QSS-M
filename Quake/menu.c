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
#include <sys/stat.h>

#ifdef _WIN32
#include <sys/types.h>
#else
#include <unistd.h>
#include <dirent.h>
#endif

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
		void M_Menu_Mouse_f (void);
		void M_Menu_Video_f (void);
		void M_Menu_Graphics_f (void);
		void M_Menu_Sound_f (void);
		void M_Menu_Game_f (void);
		void M_Menu_HUD_f (void);
			void M_Menu_Crosshair_f (void);
		void M_Menu_Console_f (void);
		void M_Menu_Extras_f (void);
		void M_Menu_ResetConfig_f(void); // woods #resetconfig
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
		void M_Mouse_Draw (void);
		void M_Video_Draw (void);
		void M_Graphics_Draw (void);
		void M_Sound_Draw (void);
		void M_Game_Draw (void);
		void M_HUD_Draw (void);
		void M_Extras_Draw (void);
		void M_ResetConfig_Draw(void); // woods #resetconfig
			void M_Crosshair_Draw (void);
		void M_Console_Draw (void);
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
		void M_Mouse_Key (int key);
		void M_Video_Key (int key);
		void M_Graphics_Key (int key);
		void M_Sound_Key (int key);
		void M_Game_Key (int key);
		void M_HUD_Key (int key);
		void M_Extras_Key (int key);
		void M_ResetConfig_Key(int key); // woods #resetconfig
			void M_Crosshair_Key (int key);
		void M_Console_Key (int key);
	void M_Mods_Key (int key);
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
		void M_Mouse_Mousemove (int cx, int cy);
		void M_Video_Mousemove (int cx, int cy);
		void M_Graphics_Mousemove (int cx, int cy);
		void M_Sound_Mousemove (int cx, int cy);
		void M_Game_Mousemove (int cx, int cy);
		void M_HUD_Mousemove (int cx, int cy);
			void M_Crosshair_Mousemove (int cx, int cy);
		void M_Console_Mousemove (int cx, int cy);
		void M_Extras_Mousemove(int cx, int cy);
		void M_ResetConfig_Mousemove(int cx, int cy); // woods #resetconfig
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

static qboolean has_custom_progs = false; // woods #botdetect
qboolean progs_check_done = false; // woods #botdetect

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

void M_PrintScroll2(int x, int y, int maxwidth, const char* str, const char* str2, double time)
{
	int maxchars = maxwidth / 8;
	int len_str = (int)strlen(str);

	// Determine effective name length based on scroll state
	int effective_len_str = (time != 0.0) ? len_str : q_min(len_str, 12);

	// Create masked version of name
	char masked_str[MAX_QPATH];
	for (int i = 0; i < effective_len_str; i++)
		masked_str[i] = (char)(str[i] ^ 128);
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

	// Scrolling display
	static const char gap[] = " /// ";
	int scroll_len = combined_len + 5;
	int ofs = ((int)(time * 4.0)) % scroll_len;
	if (ofs < 0)
		ofs += scroll_len;

	// Draw scrolling text
	for (int i = 0; i < maxchars; i++) {
		char c;
		if (ofs < combined_len)
			c = combined[ofs];
		else
			c = gap[ofs - combined_len];

		M_DrawCharacter(x + (i * 8), y, c);
		ofs = (ofs + 1) % scroll_len;
	}
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
	int scroll_len = combined_len + 5;
	int ofs = ((int)(time * 4.0)) % scroll_len;
	if (ofs < 0)
		ofs += scroll_len;

	for (int i = 0; i < maxchars; i++) {
		int pos = (ofs + i) % scroll_len;
		if (pos >= combined_len) {
			M_DrawCharacter(x + i * 8, y, ' ' | 128);
			continue;
		}

		char ch = combined[pos];
		qboolean is_highlighted = false;
		qboolean is_bronzed = false;

		if (pos < name_end) {
			if (pos < effective_len_str) {
				is_highlighted = (name_highlight_start != -1 &&
					pos >= name_highlight_start &&
					pos < name_highlight_end);
				is_bronzed = !is_highlighted;
			}
			else {
				is_bronzed = true;
			}
		}
		else {
			int desc_pos = pos - name_end;
			is_highlighted = (desc_highlight_start != -1 &&
				desc_pos >= desc_highlight_start &&
				desc_pos < desc_highlight_end);
		}

		M_DrawCharacter(x + i * 8, y, ch | (is_highlighted ? 0 : (is_bronzed ? 128 : 0)));
	}
}

void M_PrintHighlightScroll(int x, int y, int maxwidth, const char* str, const char* highlight, double time)
{
    int maxchars = maxwidth / 8;
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

    int scroll_len = len_str + 5;  // Handle scrolling text, extra spaces for scrolling
    int ofs = ((int)(time * 4.0)) % scroll_len;
    if (ofs < 0)
        ofs += scroll_len;

    for (int i = 0; i < maxchars; i++)
    {
        int pos_in_str = (ofs + i) % scroll_len;
        char ch;
        qboolean is_highlighted = false;

        if (pos_in_str < len_str)
        {
            ch = name_str[pos_in_str];

            // Check if this character is within the highlight in the name
            if (name_highlight_start != -1 &&
                pos_in_str >= name_highlight_start && pos_in_str < name_highlight_end)
            {
                is_highlighted = true;
            }
        }
        else
        {
            ch = ' '; // Scrolling spaces after the text
        }

        if (is_highlighted)
            M_DrawCharacter(x + i * 8, y, ch & 127); // Draw character in normal color (highlighted)
        else
            M_DrawCharacter(x + i * 8, y, ch | 128); // Apply bronze effect for non-highlighted text
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
char	m_filenames[MAX_SAVEGAMES][SAVEGAME_COMMENT_LENGTH+1];
int		loadable[MAX_SAVEGAMES];

typedef struct {
	char name[SAVEGAME_COMMENT_LENGTH + 1];
	char date[32];
	char mapname[MAX_QPATH];
	time_t timestamp;
	qboolean loadable;
	int original_index;
} save_entry_t;

static save_entry_t save_entries[MAX_SAVEGAMES];

static int save_compare(const void* a, const void* b) // Comparison function for qsort
{
	const save_entry_t* sa = (const save_entry_t*)a;
	const save_entry_t* sb = (const save_entry_t*)b;

	// Sort loadable saves first, then by timestamp (newest first)
	if (sa->loadable != sb->loadable)
		return sb->loadable - sa->loadable;
	return (sb->timestamp - sa->timestamp);
}

void M_ScanSaves (void)
{
	int	i, j;
	char	name[MAX_OSPATH];
	FILE	*f;
	int	version;
	float time;
	char mapname[MAX_QPATH];
#ifdef _WIN32
	struct _stat st;
#else
	struct stat st;
#endif

	for (i = 0; i < MAX_SAVEGAMES; i++)
	{
		save_entries[i].name[0] = 0;
		save_entries[i].date[0] = 0;
		save_entries[i].timestamp = 0;
		save_entries[i].loadable = false;
		save_entries[i].original_index = i;
		q_strlcpy(save_entries[i].mapname, "", sizeof(save_entries[i].mapname));

		q_snprintf (name, sizeof(name), "%s/saves/s%i.sav", com_gamedir, i);
		f = fopen (name, "r");
		if (!f)
		{
			q_snprintf(name, sizeof(name), "%s/s%i.sav", com_gamedir, i); // legacy
			f = fopen(name, "r");
		}

		if (!f)
		{
			strcpy (save_entries[i].name, "--- UNUSED SLOT ---");
			continue;
		}

		// Get file modification time
#ifdef _WIN32
		if (_stat(name, &st) == 0)
#else
		if (stat(name, &st) == 0)
#endif
		{
			struct tm* timeinfo = localtime(&st.st_mtime);
			if (timeinfo)
			{
				strftime(save_entries[i].date, sizeof(save_entries[i].date),
					"%Y-%m-%d %H:%M", timeinfo);
				save_entries[i].timestamp = st.st_mtime;
			}
		}

		// Read version and name
		if (fscanf (f, "%i\n", &version) != 1 || // woods
			fscanf (f, "%79s\n", name) != 1)
		{
			fclose(f);
			continue;
		}

		// Read spawn parms (skip them)
		for (j = 0; j < NUM_BASIC_SPAWN_PARMS; j++)
		{
			if (fscanf(f, "%f\n", &time) != 1)
			{
				fclose(f);
				continue;
			}
		}

		// Read skill
		if (fscanf(f, "%f\n", &time) != 1)
		{
			fclose(f);
			continue;
		}

		// Read map name
		if (fscanf(f, "%63s\n", mapname) == 1)
		{
			q_strlcpy (save_entries[i].mapname, mapname, sizeof(save_entries[i].mapname));
		}

		q_strlcpy (save_entries[i].name, name, SAVEGAME_COMMENT_LENGTH + 1);

	// change _ back to space
		for (j = 0; j < SAVEGAME_COMMENT_LENGTH; j++)
		{
			if (save_entries[i].name[j] == '_')
				save_entries[i].name[j] = ' ';
		}

		// Fix the kills pattern - handle both single and double spaces after slash
		char* kills = strstr(save_entries[i].name, "kills:");
		if (kills)
		{
			char* slash = strchr(kills, '/');
			if (slash && slash[1] == ' ')
			{
				if (slash[2] == ' ')  // Double space case
					memmove(slash + 1, slash + 3, strlen(slash + 3) + 1);
				else  // Single space case
					memmove(slash + 1, slash + 2, strlen(slash + 2) + 1);
	}
}

		save_entries[i].loadable = true;
		fclose(f);
	}

	// Sort the entries
	qsort(save_entries, MAX_SAVEGAMES - 1, sizeof(save_entry_t), save_compare);
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


static void M_DrawSaveSlots (const char* title_pic)
{
	qpic_t* p = Draw_CachePic(title_pic);
	M_DrawPic((320 - p->width) / 2, 4, p);

	for (int i = 0; i < MAX_SAVEGAMES - 1; i++)
	{
		M_Print(16, 32 + 8 * i, save_entries[i].name);
	}

	// Draw date info in last slot position with white text
	if (save_entries[load_cursor].loadable)
	{
		char info[128];
		M_Print(16, 32 + 8 * (MAX_SAVEGAMES - 1) + 4, "last save:");
		q_snprintf(info, sizeof(info), "%s (%s)",
			save_entries[load_cursor].date,
			save_entries[load_cursor].mapname);
		M_PrintWhite(100, 32 + 8 * (MAX_SAVEGAMES - 1) + 4, info);
	}

// line cursor
	if (load_cursor < MAX_SAVEGAMES - 1)
		M_DrawCharacter(8, 32 + load_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

void M_Load_Draw (void)
{
	M_DrawSaveSlots ("gfx/p_load.lmp");
}

void M_Save_Draw (void)
{
	M_DrawSaveSlots ("gfx/p_save.lmp");
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
		if (!save_entries[load_cursor].loadable)
			return;
		m_state = m_none;
		key_dest = key_game;
		IN_UpdateGrabs();

	// Host_Loadgame_f can't bring up the loading plaque because too much
	// stack space has been used, so do it now
		SCR_BeginLoadingPlaque ();

	// issue the load command
		Cbuf_AddText (va ("load s%i\n", save_entries[load_cursor].original_index));
		return;

	case K_DOWNARROW:
	case K_RIGHTARROW:
		S_LocalSound("misc/menu1.wav");
		load_cursor++;
		if (load_cursor >= MAX_SAVEGAMES - 1)
			load_cursor = 0;
		break;

	case K_UPARROW:
	case K_LEFTARROW:
		S_LocalSound ("misc/menu1.wav");
		load_cursor--;
		if (load_cursor < 0)
			load_cursor = MAX_SAVEGAMES - 2;
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
			load_cursor = MAX_SAVEGAMES - 2;
		break;

	case K_DOWNARROW:
	case K_RIGHTARROW:
		S_LocalSound ("misc/menu1.wav");
		load_cursor++;
		if (load_cursor >= MAX_SAVEGAMES - 1)
			load_cursor = 0;
		break;
	}
}

void M_Load_Mousemove(int cx, int cy) // woods #mousemenu
{
	M_UpdateCursor(cy, 32, 8, MAX_SAVEGAMES-1, &load_cursor);
}

void M_Save_Mousemove(int cx, int cy) // woods #mousemenu
{
	M_UpdateCursor(cy, 32, 8, MAX_SAVEGAMES-1, &load_cursor);
}

/*
==================
Maps Menu (iw)
==================
*/

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
	int*                filtered_indices;
} mapsmenu;

static void M_Maps_Add(const char* name, const char* date)
{
    mapitem_t map;
    map.name = name;
    map.date = date; // Set the date

    VEC_PUSH(mapsmenu.items, map);
    mapsmenu.mapcount = VEC_SIZE(mapsmenu.items);
}

static void M_Maps_Refilter(void)
{
    int i;
    VEC_CLEAR(mapsmenu.filtered_indices);

    for (i = 0; i < mapsmenu.mapcount; i++)
    {
        if (mapsmenu.list.search.len == 0 || q_strcasestr(mapsmenu.items[i].name, mapsmenu.list.search.text) || q_strcasestr(mapsmenu.items[i].date, mapsmenu.list.search.text))
        {
            VEC_PUSH(mapsmenu.filtered_indices, i);
        }
    }

    mapsmenu.list.numitems = VEC_SIZE(mapsmenu.filtered_indices);

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
    mapsmenu.list.viewsize = MAX_VIS_MAPS;
    mapsmenu.list.cursor = -1;
    mapsmenu.list.scroll = 0;
    mapsmenu.list.numitems = 0;
    mapsmenu.mapcount = 0;
    VEC_CLEAR(mapsmenu.items);
    VEC_CLEAR(mapsmenu.filtered_indices);

    memset(&mapsmenu.list.search, 0, sizeof(mapsmenu.list.search));
    mapsmenu.list.search.maxlen = 32;

    M_Ticker_Init(&mapsmenu.ticker);

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
        int map_idx = mapsmenu.filtered_indices[idx];
        mapitem_t* map_item = &mapsmenu.items[map_idx];
        qboolean selected = (idx == mapsmenu.list.cursor);

        if (mapsmenu.list.search.len > 0)
        {
            M_PrintHighlightScroll2(x, y + i * 8, (cols - 2) * 8,
                map_item->name,
                map_item->date,
                mapsmenu.list.search.text,
                selected ? mapsmenu.ticker.scroll_time : 0.0);
        }
        else
        {
            M_PrintScroll2(x, y + i * 8, (cols - 2) * 8,
                map_item->name,
                map_item->date,
                selected ? mapsmenu.ticker.scroll_time : 0.0);
        }

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

    if (mapsmenu.list.search.len > 0) // Draw search box if search is active
    {
        M_DrawTextBox(16, 176, 32, 1);
        M_PrintHighlight(24, 184, mapsmenu.list.search.text,
            mapsmenu.list.search.text,
            mapsmenu.list.search.len);
        int cursor_x = 24 + 8 * mapsmenu.list.search.len; // Start position + character width * text length
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
			if (maps_from_gameoptions)
			{
				// Set the map and return to game options
				M_SetSkillMenuMap(mapsmenu.items[mapsmenu.filtered_indices[mapsmenu.list.cursor]].name);
				maps_from_gameoptions = false;
				M_Menu_GameOptions_f();
			}
			else
			{
				// Original behavior - go to skill menu
				M_SetSkillMenuMap(mapsmenu.items[mapsmenu.filtered_indices[mapsmenu.list.cursor]].name);
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

/*
==================
Skill Menu (iw)
==================
*/

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

/*
==================
Multiplayer Menu
==================
*/

int	m_multiplayer_cursor;
#define	MULTIPLAYER_ITEMS	3
extern cvar_t scr_shownet; // woods


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

		if (realtime - cl.last_received_message > scr_shownet.value)
			M_PrintRGBA(f, 112, lastmphost, CL_PLColours_Parse("0xffffff"), 0.2f, false);
		else
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

/*
==================
Setup Menu
==================
*/

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
		char buf2[15];
		const char* obs;
		const char* star_obs;
		obs = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "observer", buf, sizeof(buf));
		star_obs = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "*observer", buf2, sizeof(buf2));

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

	if (!rgbactive && (setup_cursor == 3 || setup_cursor == 4)) // woods
		M_PrintRGBA (64, 170, "+shift for RGB colors", CL_PLColours_Parse ("0xffffff"), 0.6f, false); // woods

	M_DrawTextBox (64, 142, 14, 1);  // woods 140 to 152 #namemaker
	M_Print (72, 150, "Accept Changes"); // woods #colorbar

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
			q_strlcpy (lastColorSelected, CL_PLColours_ToString(setup_top), sizeof(lastColorSelected));
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
		if (setup_cursor == 3) // 2 to 3 woods #namemaker
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
		if (setup_cursor == 4) // 3 to 4 woods #namemaker
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
			{
				if (keydown[K_CTRL])
				{
					listsearch_t temp = { 0 };
					temp.len = strlen(setup_hostname);
					Q_strcpy(temp.text, setup_hostname);
					M_DeletePrevWord(&temp);
					Q_strcpy(setup_hostname, temp.text);
		}
				else
					setup_hostname[strlen(setup_hostname) - 1] = 0;
			}
		}

		if (setup_cursor == 1)
		{
			if (strlen(setup_myname))
			{
				if (keydown[K_CTRL])
				{
					listsearch_t temp = { 0 };
					temp.len = strlen(setup_myname);
					Q_strcpy(temp.text, setup_myname);
					M_DeletePrevWord(&temp);
					Q_strcpy(setup_myname, temp.text);
		}
				else
					setup_myname[strlen(setup_myname) - 1] = 0;
			}
		}
		break;

	case 'u':
	case 'U':
		if (keydown[K_CTRL])
		{
			if (setup_cursor == 0)
				setup_hostname[0] = 0;
			else if (setup_cursor == 1)
				setup_myname[0] = 0;
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

/*
=============================================================
Name Maker Menu #namemaker from joequake, qrack
=============================================================
*/

int	namemaker_cursor_x, namemaker_cursor_y;
#define	NAMEMAKER_TABLE_SIZE	16
#define NAMEMAKER_TOTAL_ROWS (NAMEMAKER_TABLE_SIZE + 1) // Added to include the new row

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
			namemaker_cursor_y = NAMEMAKER_TOTAL_ROWS - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		namemaker_cursor_y++;
		if (namemaker_cursor_y >= NAMEMAKER_TOTAL_ROWS)
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
		if (namemaker_cursor_y < NAMEMAKER_TABLE_SIZE) // Only move left if within table
		{
			S_LocalSound("misc/menu1.wav");
			namemaker_cursor_x--;
			if (namemaker_cursor_x < 0)
				namemaker_cursor_x = NAMEMAKER_TABLE_SIZE - 1;
		}
		break;

	case K_RIGHTARROW:
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
		namemaker_cursor_x = 0;
		break;

	case K_END:
		S_LocalSound("misc/menu1.wav");
		namemaker_cursor_x = NAMEMAKER_TABLE_SIZE - 1;
		break;

	case K_BACKSPACE:
		if ((l = strlen(namemaker_name)))
			namemaker_name[l - 1] = 0;

		if (keydown[K_CTRL])
		{
			listsearch_t temp;
			temp.len = strlen(namemaker_name);
			Q_strcpy(temp.text, namemaker_name);
			M_DeletePrevWord(&temp);
			Q_strcpy(namemaker_name, temp.text);
		}
		break;

	case 'u':
	case 'U':
		if (keydown[K_CTRL])
		{
			namemaker_name[0] = 0;
		}
		break;

	// If we reached this point, we are simulating ENTER

	case K_SPACE:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1: // woods #mousemenu
		if (namemaker_cursor_y < NAMEMAKER_TABLE_SIZE)
		{
			l = strlen(namemaker_name);
			if (l < 15)
			{
				namemaker_name[l] = NAMEMAKER_TABLE_SIZE * namemaker_cursor_y + namemaker_cursor_x;
				namemaker_name[l + 1] = 0;
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
	int x_origin = 28;
	int y_origin = 36;
	int x_spacing = 16;
	int y_spacing = 8;
	int num_rows = NAMEMAKER_TOTAL_ROWS;
	int max_columns;
	int temp_cursor_x, temp_cursor_y;

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
	OPT_VIDEO,
	OPT_GRAPHICS,
	OPT_SOUND,
	OPT_GAME,
	OPT_HUD,
	OPT_CONSOLEM,    // Moved up, before OPT_EXTRAS
	OPT_EXTRAS,
	OPT_SPACE,       // Spacer
	OPT_MENUSCALE,
	OPT_CONSOLE,
	OPT_RESETCONFIG,
	OPTIONS_ITEMS
};

#define	SLIDER_RANGE	6

int		options_cursor;
qboolean slider_grab; // woods #mousemenu
static float pending_scale_value;

struct // woods #mousemenu
{
	menulist_t		list;
	int				y;
	int				first_item;
	int				options_cursor;
	int				video_cursor;
	int* last_cursor;
	qboolean        scrollbar_grab;
} optionsmenu;

static void M_Options_Init(void)
{
	optionsmenu.list.viewsize = OPTIONS_ITEMS;
	optionsmenu.list.cursor = 0;
	optionsmenu.list.scroll = 0;
	optionsmenu.list.numitems = OPTIONS_ITEMS;
	optionsmenu.scrollbar_grab = false;

	// Initialize search
	memset(&optionsmenu.list.search, 0, sizeof(optionsmenu.list.search));
	optionsmenu.list.search.maxlen = 32;
}

void M_Menu_Options_f (void)
{
	key_dest = key_menu;
	m_state = m_options;
	m_entersound = true;
	slider_grab = false; // woods #mousemenu
	M_Options_Init();

	IN_UpdateGrabs();
}


void M_AdjustSliders (int dir)
{
	float	f;

	S_LocalSound ("misc/menu3.wav");

	switch (options_cursor)
	{
	case OPT_MENUSCALE:
		f = scr_menuscale.value + dir;
		if (f > 6) f = 6;
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
		f = f * 5.0f + 1.0f;  // Convert 0-1 range to 1-6
		f = (int)f;  // Round to nearest integer
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
		f = f * 5.0f + 1.0f;
		f = (int)f;
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

	// Draw menu items with search highlighting if active
	for (int i = 0; i < OPTIONS_ITEMS; i++)
	{
		const char* text = NULL;
		int y = 32 + 8 * i;

		// Get menu item text based on index
		switch (i) {
		case OPT_CUSTOMIZE:
			text = "             Key Setup   ...";
			break;
		case OPT_MOUSE:
			text = "                 Mouse   ...";
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
		case OPT_EXTRAS:
			text = "                  Misc   ...";
			break;
		case OPT_MENUSCALE:
			text = "            Menu Scale";
			if (slider_grab && options_cursor == OPT_MENUSCALE)
			{
				r = (pending_scale_value - 1) / 5;
				M_DrawSlider(220, y, r, pending_scale_value, "%.0f");
			}
			else
			{
				r = (scr_menuscale.value - 1) / 5;
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

		// Draw the values/sliders
		switch (i) {

	}
}
	// Draw cursor
	M_DrawCharacter(200, 32 + options_cursor * 8, 12 + ((int)(realtime * 4) & 1));

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
		return "             Key Setup   ...";
	case OPT_MOUSE:
		return "                 Mouse   ...";
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

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		options_cursor++;
		if (options_cursor >= OPTIONS_ITEMS)
			options_cursor = 0;
		if (options_cursor == OPT_SPACE)  // Skip space when going down
			options_cursor++;
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

		if (options_cursor == OPT_MENUSCALE)
		{
			float f = M_MouseToSliderFraction(cx - 220);
			f = f * 5.0f + 1.0f;  // Convert 0-1 range to 1-6
			f = (int)f;  // Round to nearest integer
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
Keys Menu
==================
*/

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


#define MAX_VIS_KEYS	15 // woods #mousemenu

static struct
{
	menulist_t           list;
	struct {
		char text[32];
		int len;
		int maxlen;
	} search;
	int* filtered_indices;
	int num_filtered;
	qboolean scrollbar_grab;  // Add this
	int x, y, cols;          // Add these for scrollbar positioning
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

void M_Keys_UpdateFilter(void)
{
	keysmenu.num_filtered = 0;
	keysmenu.list.scroll = 0;  // Reset scroll position when filtering

	// First pass: count matches
	for (int i = 0; i < numbindnames; i++)
	{
		if (keysmenu.search.len == 0)
		{
			keysmenu.num_filtered = numbindnames;
			break;
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
		for (int i = 0; i < numbindnames; i++)
			keysmenu.filtered_indices[i] = i;
	}
	else
	{
		// Fill with matching indices
		int filter_idx = 0;
		for (int i = 0; i < numbindnames; i++)
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

	keysmenu.scrollbar_grab = false;
	keysmenu.x = 0;
	keysmenu.y = 48;
	keysmenu.cols = 36;

	// Initialize filtered indices array
	if (keysmenu.filtered_indices)
		Z_Free(keysmenu.filtered_indices);
	keysmenu.filtered_indices = (int*)Z_Malloc(numbindnames * sizeof(int));
	keysmenu.num_filtered = numbindnames;
	for (int i = 0; i < numbindnames; i++)
		keysmenu.filtered_indices[i] = i;

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
	int		count;
	int		j;
	char	*b;
	int		bindmap = 0;

	threekeys[0] = threekeys[1] = threekeys[2] = -1;
	count = 0;

	for (j = 0; j < MAX_KEYS; j++)
	{
		b = keybindings[bindmap][j];
		if (!b)
			continue;
		if (IsCompleteCommand(b, command))
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
	char	*b;
	int		bindmap = 0;

	for (j = 0; j < MAX_KEYS; j++)
	{
		b = keybindings[bindmap][j];
		if (!b)
			continue;
		if (IsCompleteCommand(b, command))
			Key_SetBinding(j, NULL, bindmap);
	}
}

extern qpic_t	*pic_up, *pic_down;

void M_Keys_Draw(void)
{
	int firstvis, numvis, x, y, cols;
	qpic_t* p;

	p = Draw_CachePic("gfx/ttl_cstm.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);
	if (bind_grab)
		M_Print(12, 32, "press a key or button for this action");
	else
		M_Print(18, 32, "enter to change, backspace to clear");

	x = 0;
	y = 48;
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

	y += 2;

	// Draw visible items
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
			const char* keyStr = Key_KeynumToString(keys[0]);
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
				keyStr = Key_KeynumToString(keys[j]);
				print_fn(x_pos, y, keyStr);
				x_pos += (strlen(keyStr) * 8);
			}
		}
		else
		{
			qboolean masked = !is_selected;
			float alpha = masked ? 0.5f : 1.0f;
			M_PrintRGBA(x_pos, y, "???", CL_PLColours_Parse("0xffffff"), alpha, masked);
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
}

void M_Keys_Key(int k)
{
	int x, y;

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
	
	char    cmd[80];
	if (bind_grab)
	{   // defining a key
		S_LocalSound("misc/menu1.wav");
		if (k != K_ESCAPE && k != '`')
		{
			int actual_idx = keysmenu.filtered_indices[keysmenu.list.cursor];
			int keys[3];
			M_FindKeysForCommand(bindnames[actual_idx].cmd, keys);
			if (keys[2] != -1)
				M_UnbindCommand(bindnames[actual_idx].cmd);
			sprintf(cmd, "bind \"%s\" \"%s\"\n", Key_KeynumToString(k), bindnames[actual_idx].cmd);
			Cbuf_InsertText(cmd);
		}
		bind_grab = false;
		IN_UpdateGrabs();
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

	case K_BACKSPACE:
	case K_DEL:
		if (!keysmenu.search.len)  // Only delete binding if not searching
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

extern cvar_t cl_minpitch, cl_maxpitch;

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
#ifdef MACOS_X_ACCELERATION_HACK
	case MOUSE_ACCELERATION:
		return "Acceleration";
#endif
	default:
		q_snprintf(buffer, sizeof(buffer), "Unknown Item %d", index);
		return buffer;
	}
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
			numberOfMouseItems = MOUSE_ITEMS;
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

			// Update filtering
			if (mousemenu.search.len > 0)
			{
				numberOfMouseItems = 0;
				for (int i = 0; i < MOUSE_ITEMS; i++)
				{
					const char* itemtext = M_Mouse_GetItemText(i);
					if (itemtext && q_strcasestr(itemtext, mousemenu.search.text))
					{
						numberOfMouseItems++;
						if (numberOfMouseItems == 1)
							mouse_cursor = i;
					}
				}
			}
			else
			{
				numberOfMouseItems = MOUSE_ITEMS;
			}
			return;
		}
	}
	else if (k == 'u' || k == 'U')
	{
		if (keydown[K_CTRL] && mousemenu.search.len > 0)
		{
			mousemenu.search.len = 0;
			mousemenu.search.text[0] = 0;
			numberOfMouseItems = MOUSE_ITEMS;
			return;
		}
	}
	else if (k >= 32 && k < 127)
	{
		if (mousemenu.search.len < sizeof(mousemenu.search.text) - 1)
		{
			mousemenu.search.text[mousemenu.search.len++] = k;
			mousemenu.search.text[mousemenu.search.len] = 0;

			numberOfMouseItems = 0;
			for (int i = 0; i < MOUSE_ITEMS; i++)
			{
				const char* itemtext = M_Mouse_GetItemText(i);
				if (itemtext && q_strcasestr(itemtext, mousemenu.search.text))
				{
					numberOfMouseItems++;
					if (numberOfMouseItems == 1)
						mouse_cursor = i;
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
		mouse_cursor--;
		if (mouse_cursor < 0)
			mouse_cursor = numberOfMouseItems - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		mouse_cursor++;
		if (mouse_cursor >= numberOfMouseItems)
			mouse_cursor = 0;
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
Graphics Menu
==================
*/

extern cvar_t r_particles, gl_load24bit, r_replacemodels, r_lerpmodels, r_lerpmove, r_scale, r_outline,
vid_gamma, vid_contrast, vid_fsaa, r_particledesc, gl_loadlitfiles, r_rocketlight, r_explosionlight,
gl_powerupshells, gl_caustics;

static enum graphics_e
{
	GRAPHICS_BRIGHTNESS,
	GRAPHICS_CONTRAST,
	GRAPHICS_FILTERING,
	GRAPHICS_ANTIALIASING,
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
	GRAPHICS_COUNT
} graphics_cursor;

#define GRAPHICS_ITEMS (GRAPHICS_COUNT)
int numberOfGraphicsItems = GRAPHICS_ITEMS;

static struct
{
	int cursor;
	struct {
		char text[32];
		int len;
	} search;
} graphicsmenu;

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
	default:
		q_snprintf(buffer, sizeof(buffer), "Unknown Item %d", index);
		return buffer;
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

	IN_UpdateGrabs();
}

static void M_Graphics_AdjustSliders(int dir)
{
	int m;
	float f;
	S_LocalSound("misc/menu3.wav");

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
	}
}

void M_Graphics_Draw(void)
{
	qpic_t* p;
	enum graphics_e i;
	float r;
	int m;

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	const char* title = "Graphics Options";
	M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);

	for (i = 0; i < GRAPHICS_ITEMS; i++)
	{
		int y = 48 + 8 * i;
		const char* text = NULL;
		const char* value = NULL;

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
				value = "light only";
			M_Print(178, y, value);
			break;

		case GRAPHICS_WATERCAUSTICS:
			text = "    Water Caustics"; // Adjust spacing as needed
			r = gl_caustics.value / 10.0f; // Normalize 0-10 value to 0-1 for slider
			M_DrawSlider(186, y, r, gl_caustics.value * 10.0f, "%.0f%%"); // Display as 0-100%
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
	M_DrawCharacter(168, 48 + graphics_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

static qboolean graphics_slider_grab;

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
			numberOfGraphicsItems = GRAPHICS_ITEMS;
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
			numberOfGraphicsItems = GRAPHICS_ITEMS;
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

			// Update filtering based on new search text
			if (graphicsmenu.search.len > 0)
			{
				numberOfGraphicsItems = 0;
				for (int i = 0; i < GRAPHICS_ITEMS; i++)
				{
					const char* itemtext = M_Graphics_GetItemText(i);
					if (itemtext && q_strcasestr(itemtext, graphicsmenu.search.text))
					{
						numberOfGraphicsItems++;
						if (numberOfGraphicsItems == 1)
							graphics_cursor = i;
					}
				}
			}
			else
			{
				numberOfGraphicsItems = GRAPHICS_ITEMS;
			}
			return;
	}
	}
	else if (k == K_BACKSPACE)
	{
		if (graphicsmenu.search.len > 0)
		{
			graphicsmenu.search.text[--graphicsmenu.search.len] = 0;
			if (graphicsmenu.search.len > 0)
			{
				numberOfGraphicsItems = 0;
				for (int i = 0; i < GRAPHICS_ITEMS; i++)
				{
					const char* itemtext = M_Graphics_GetItemText(i);
					if (itemtext && q_strcasestr(itemtext, graphicsmenu.search.text))
					{
						numberOfGraphicsItems++;
						if (numberOfGraphicsItems == 1)
							graphics_cursor = i;
					}
				}
			}
			else
			{
				numberOfGraphicsItems = GRAPHICS_ITEMS;
			}
			return;
		}
	}
	else if (k >= 32 && k < 127)
	{
		if (graphicsmenu.search.len < sizeof(graphicsmenu.search.text) - 1)
		{
			graphicsmenu.search.text[graphicsmenu.search.len++] = k;
			graphicsmenu.search.text[graphicsmenu.search.len] = 0;

			numberOfGraphicsItems = 0;
			for (int i = 0; i < GRAPHICS_ITEMS; i++)
			{
				const char* itemtext = M_Graphics_GetItemText(i);
				if (itemtext && q_strcasestr(itemtext, graphicsmenu.search.text))
				{
					numberOfGraphicsItems++;
					if (numberOfGraphicsItems == 1)
						graphics_cursor = i;
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
		M_Menu_Options_f();
		break;

	case K_MOUSE1:
		m_entersound = true;

		// Check if click is in search box area
		if (graphicsmenu.search.len > 0 && m_mousey >= 170)
			break;

		// Check if click is in valid menu area
		if (m_mousey >= 48 && m_mousey < 48 + (GRAPHICS_ITEMS * 8))
		{
			graphics_cursor = (m_mousey - 48) / 8;

			if (graphics_cursor == GRAPHICS_BRIGHTNESS ||
				graphics_cursor == GRAPHICS_CONTRAST ||
				graphics_cursor == GRAPHICS_ALIASSHADOW ||
				graphics_cursor == GRAPHICS_ROCKETLIGHT ||
				graphics_cursor == GRAPHICS_EXPLOSIONLIGHT ||
				graphics_cursor == GRAPHICS_MODELOUTLINES ||
				graphics_cursor == GRAPHICS_WATERCAUSTICS)
			{
				graphics_slider_grab = true;
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
		graphics_cursor--;
		if (graphics_cursor < 0)
			graphics_cursor = numberOfGraphicsItems - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		graphics_cursor++;
		if (graphics_cursor >= numberOfGraphicsItems)
			graphics_cursor = 0;
		break;

	case K_LEFTARROW:
	case K_MWHEELDOWN:
		M_Graphics_AdjustSliders(-1);
		break;

	case K_RIGHTARROW:
	case K_MWHEELUP:
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

			// Add empty cases for all other enum values to suppress warnings
		case GRAPHICS_FILTERING:
		case GRAPHICS_ANTIALIASING:
		case GRAPHICS_EXTERNALTEX:
		case GRAPHICS_REPLACEMENTMODELS:
		case GRAPHICS_MODELLERP:
		case GRAPHICS_RENDERSCALE:
		case GRAPHICS_CLASSICPARTICLES:
		case GRAPHICS_CUSTOMPARTICLES:
		case GRAPHICS_COLOREDLIGHTING:
		case GRAPHICS_BRUSHSHADOW:
		case GRAPHICS_POWERUPSHELLS:
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
	int item = (cy - 48) / 8;

	// Make sure the item is within valid range
	if (item >= 0 && item < GRAPHICS_ITEMS)
	{
		// Update the cursor position
		graphics_cursor = item;
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
	SOUND_WATERFX,
	SOUND_AMBIENTLEVEL,
	SOUND_STOPSOUND,
	SOUND_MUTE,
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
	case SOUND_WATERFX:
		return "Water FX";
	case SOUND_AMBIENTLEVEL:
		return "Ambient Level";
	case SOUND_STOPSOUND:
		return "Stop Sound";
	case SOUND_MUTE:
		return "Mute";
	default:
		q_snprintf(buffer, sizeof(buffer), "Unknown Item %d", index);
		return buffer;
	}
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

	default:
		break;
	}
}


void M_Sound_Draw(void)
{
	qpic_t* p;
	enum sound_e i;

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
			numberOfSoundItems = SOUND_ITEMS;
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
			numberOfSoundItems = SOUND_ITEMS;
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

			// Update filtering based on new search text
			if (soundmenu.search.len > 0)
			{
				numberOfSoundItems = 0;
				for (int i = 0; i < SOUND_ITEMS; i++)
				{
					const char* itemtext = M_Sound_GetItemText(i);
					if (itemtext && q_strcasestr(itemtext, soundmenu.search.text))
					{
						numberOfSoundItems++;
						if (numberOfSoundItems == 1)
							sound_cursor = i;
					}
				}
			}
			else
			{
				numberOfSoundItems = SOUND_ITEMS;
			}
			return;
		}
	}
	else if (k == K_BACKSPACE)
	{
		if (soundmenu.search.len > 0)
		{
			soundmenu.search.text[--soundmenu.search.len] = 0;
			if (soundmenu.search.len > 0)
			{
				numberOfSoundItems = 0;
				for (int i = 0; i < SOUND_ITEMS; i++)
				{
					const char* itemtext = M_Sound_GetItemText(i);
					if (itemtext && q_strcasestr(itemtext, soundmenu.search.text))
					{
						numberOfSoundItems++;
						if (numberOfSoundItems == 1)
							sound_cursor = i;
					}
				}
			}
			else
			{
				numberOfSoundItems = SOUND_ITEMS;
			}
			return;
		}
	}
	else if (k >= 32 && k < 127)
	{
		if (soundmenu.search.len < sizeof(soundmenu.search.text) - 1)
		{
			soundmenu.search.text[soundmenu.search.len++] = k;
			soundmenu.search.text[soundmenu.search.len] = 0;

			numberOfSoundItems = 0;
			for (int i = 0; i < SOUND_ITEMS; i++)
			{
				const char* itemtext = M_Sound_GetItemText(i);
				if (itemtext && q_strcasestr(itemtext, soundmenu.search.text))
				{
					numberOfSoundItems++;
					if (numberOfSoundItems == 1)
						sound_cursor = i;
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
		sound_cursor--;
		if (sound_cursor < 0)
			sound_cursor = numberOfSoundItems - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		sound_cursor++;
		if (sound_cursor >= numberOfSoundItems)
			sound_cursor = 0;
		break;

	case K_LEFTARROW:
	case K_MWHEELDOWN:
		M_Sound_AdjustSliders(-1);
		break;

	case K_RIGHTARROW:
	case K_MWHEELUP:
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
		case SOUND_STOPSOUND:
		case SOUND_MUTE:
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
Game Menu
==================
*/

extern cvar_t cl_rollangle, scr_fov, gl_cshiftpercent, cl_bob, v_kicktime, v_kickroll, v_kickpitch, r_drawviewmodel,
cl_damagehue, w_switch, b_switch, cl_say, cl_r2g, cl_truelightning, cl_deadbodyfilter, con_mm1mute,
gl_max_size, gl_load24bit;

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
	GAME_DEADBODYFILTER, // Added
	GAME_MM1MUTE,        // Added
	GAME_VIEWMODEL,      // Added
	GAME_TEAMCOLOR,  // Added
	GAME_ENEMYCOLOR, // Added
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

static qboolean game_slider_grab;
static qboolean team_rgb_active;
static qboolean enemy_rgb_active;
static char last_team_color[10];
static char last_enemy_color[10];

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
	case GAME_TEXTURELESS:
		return "Textureless";
	default:
		q_snprintf(buffer, sizeof(buffer), "Unknown Item %d", index);
		return buffer;
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

	IN_UpdateGrabs();
}

static void M_Game_AdjustSliders(int dir)
{
	int curr_alwaysrun, target_alwaysrun;
	float f;
	S_LocalSound("misc/menu3.wav");

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

	//p = Draw_CachePic("gfx/p_option.lmp");
	//M_DrawPic((320 - p->width) / 2, 4, p);

	const char* title = "Game Options";
	M_PrintWhite((320 - 8 * strlen(title)) / 2, 4, title);

	for (i = 0; i < GAME_ITEMS; i++)
	{
		int y = 20 + 8 * i;
		const char* text = NULL;
		const char* value = NULL;

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
	}

	// Draw cursor
	M_DrawCharacter(168, 20 + game_cursor * 8, 12 + ((int)(realtime * 4) & 1));

	if (game_cursor == GAME_TEAMCOLOR || game_cursor == GAME_ENEMYCOLOR)
		M_PrintRGBA(74, 160, "+shift for RGB colors", CL_PLColours_Parse("0xffffff"), 0.6f, false);

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
			numberOfGameItems = GAME_ITEMS;
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
			numberOfGameItems = GAME_ITEMS;
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

			// Update filtering based on new search text
			if (gamemenu.search.len > 0)
			{
				numberOfGameItems = 0;
				for (int i = 0; i < GAME_ITEMS; i++)
				{
					const char* itemtext = M_Game_GetItemText(i);
					if (itemtext && q_strcasestr(itemtext, gamemenu.search.text))
					{
						numberOfGameItems++;
						if (numberOfGameItems == 1)
							game_cursor = i;
					}
				}
			}
			else
			{
				numberOfGameItems = GAME_ITEMS;
			}
			return;
	}
	}
	else if (k == K_BACKSPACE)
	{
		if (gamemenu.search.len > 0)
		{
			gamemenu.search.text[--gamemenu.search.len] = 0;
			if (gamemenu.search.len > 0)
			{
				numberOfGameItems = 0;
				for (int i = 0; i < GAME_ITEMS; i++)
				{
					const char* itemtext = M_Game_GetItemText(i);
					if (itemtext && q_strcasestr(itemtext, gamemenu.search.text))
					{
						numberOfGameItems++;
						if (numberOfGameItems == 1)
							game_cursor = i;
					}
				}
			}
			else
			{
				numberOfGameItems = GAME_ITEMS;
			}
			return;
		}
	}
	else if (k >= 32 && k < 127)
	{
		if (gamemenu.search.len < sizeof(gamemenu.search.text) - 1)
		{
			gamemenu.search.text[gamemenu.search.len++] = k;
			gamemenu.search.text[gamemenu.search.len] = 0;

			numberOfGameItems = 0;
			for (int i = 0; i < GAME_ITEMS; i++)
			{
				const char* itemtext = M_Game_GetItemText(i);
				if (itemtext && q_strcasestr(itemtext, gamemenu.search.text))
				{
					numberOfGameItems++;
					if (numberOfGameItems == 1)
						game_cursor = i;
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
				game_cursor == GAME_VIEWMODEL)
			{
				game_slider_grab = true;
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
		game_cursor--;
		if (game_cursor < 0)
			game_cursor = numberOfGameItems - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		game_cursor++;
		if (game_cursor >= numberOfGameItems)
			game_cursor = 0;
		break;

	case K_LEFTARROW:
	case K_MWHEELDOWN:
		M_Game_AdjustSliders(-1);
		break;

	case K_RIGHTARROW:
	case K_MWHEELUP:
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

static qboolean hud_slider_grab;
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

	IN_UpdateGrabs();
}

static void M_HUD_AdjustSliders(int dir)
{
	float f, l;
	int value;
	S_LocalSound("misc/menu3.wav");

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
		f = scr_sbaralpha.value - dir * 0.05;
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
		else if (f > 6) f = 6;
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

	for (int i = 0; i < HUD_ITEMS; i++)
	{
		int y = 48 + 8 * i;
		const char* text = NULL;

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
			r = (1.0 - scr_sbaralpha.value);
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
			r = (scr_conscale.value - 1) / 5.0; // Scale to 1-6 range
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
	M_DrawCharacter(168, 48 + hud_cursor * 8, 12 + ((int)(realtime * 4) & 1));
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
		{
			int value = scr_sbar.value + 1;
			if (value > 3) value = 1;
			Cvar_SetValue("scr_sbar", value);
			break;
		}
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
		{
			int value = scr_clock.value + 1;
			if (value > 8) value = 0;
			Cvar_SetValue("scr_clock", value);
			break;
		}
		case HUD_OBSITEMS:
			Cvar_SetValue("scr_obsitems", !scr_obsitems.value);
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
			}
			else if (hud_cursor == HUD_SBARSTYLE)
			{
				int value = scr_sbar.value + 1;
				if (value > 3) value = 1;
				Cvar_SetValue("scr_sbar", value);
			}
			else if (hud_cursor == HUD_SHOWFPS)
			{
				Cvar_SetValue("scr_showfps", !scr_showfps.value);
			}
			else if (hud_cursor == HUD_MATCHSCORES)
			{
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
				if (value > 2) value = 0;
				Cvar_SetValue("scr_showspeed", value);
			}
			else if (hud_cursor == HUD_SHOWSCORES)
			{
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
	case K_MWHEELDOWN:
		M_HUD_AdjustSliders(-1);
		break;

	case K_RIGHTARROW:
	case K_MWHEELUP:
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
			f = 1.0 - M_MouseToSliderFraction(cx - 187);
			Cvar_SetValue("scr_sbaralpha", f);
			break;

		case HUD_CONSOLEFONT:
			f = M_MouseToSliderFraction(cx - 187);
			f = f * 5.0 + 1.0;
			f = CLAMP(1.0, f, 6.0);
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
	case CROSSHAIR_COLOR:
		return "Crosshair Color";
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

extern cvar_t scr_conscale, scr_consize, scr_conspeed, scr_conalpha, cl_contentfilter;

static enum console_e
{
	CONSOLE_FONTSIZE,
	CONSOLE_HEIGHT,
	CONSOLE_SPEED,
	CONSOLE_TRANSPARENCY,
	CONSOLE_CONTENTFILTER,
	CONSOLE_COUNT
} console_cursor;

#define CONSOLE_ITEMS (CONSOLE_COUNT)
int numberOfConsoleItems = CONSOLE_ITEMS;

static struct
{
	int cursor;
	struct {
		char text[32];
		int len;
	} search;
} consolemenu;

static qboolean console_slider_grab;

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
	case CONSOLE_CONTENTFILTER:
		return "Content Filter";
	default:
		q_snprintf(buffer, sizeof(buffer), "Unknown Item %d", index);
		return buffer;
	}
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

	IN_UpdateGrabs();
}

static void M_Console_AdjustSliders(int dir)
{
	float f;
	int val;
	S_LocalSound("misc/menu3.wav");

	switch (console_cursor)
	{
	case CONSOLE_FONTSIZE:
		f = scr_conscale.value + dir;
		if (f > 6) f = 6;
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

	case CONSOLE_CONTENTFILTER:
		val = (int)cl_contentfilter.value + dir;
		if (val > 2) val = 0;
		else if (val < 0) val = 2;
		Cvar_SetValue("cl_contentfilter", val);
		break;
	default:
		break;
	}
}

void M_Console_Draw(void)
{
	qpic_t* p;
	float r;
	enum console_e i;
	const char* filter_text;

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	const char* title = "Console Options";
	M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);

	for (i = 0; i < CONSOLE_ITEMS; i++)
	{
		int y = 48 + 8 * i;
		const char* text = NULL;

		switch (i)
		{
		case CONSOLE_FONTSIZE:
			text = "       Font Size";
			r = (scr_conscale.value - 1) / 5;
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

		case CONSOLE_CONTENTFILTER:
			text = "  Content Filter";
			switch ((int)cl_contentfilter.value)
			{
			case 0: filter_text = "off"; break;
			case 1: filter_text = "partial"; break;
			case 2: filter_text = "full"; break;
			default: filter_text = "unknown"; break;
			}
			M_Print(180, y, filter_text);
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
	}

	// Draw cursor
	M_DrawCharacter(168, 48 + console_cursor * 8, 12 + ((int)(realtime * 4) & 1));

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

	// Handle search functionality first
	if (k == K_ESCAPE)
	{
		if (consolemenu.search.len > 0)
		{
			consolemenu.search.len = 0;
			consolemenu.search.text[0] = 0;
			numberOfConsoleItems = CONSOLE_ITEMS;
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
			consolemenu.search.len = 0;
			consolemenu.search.text[0] = 0;
			numberOfConsoleItems = CONSOLE_ITEMS;
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

			// Update filtering based on new search text
			if (consolemenu.search.len > 0)
			{
				numberOfConsoleItems = 0;
				for (int i = 0; i < CONSOLE_ITEMS; i++)
				{
					const char* itemtext = M_Console_GetItemText(i);
					if (itemtext && q_strcasestr(itemtext, consolemenu.search.text))
					{
						numberOfConsoleItems++;
						if (numberOfConsoleItems == 1)
							console_cursor = i;
					}
				}
			}
			else
			{
				numberOfConsoleItems = CONSOLE_ITEMS;
			}
			return;
		}
	}
	else if (k == K_BACKSPACE)
	{
		if (consolemenu.search.len > 0)
		{
			consolemenu.search.text[--consolemenu.search.len] = 0;
			if (consolemenu.search.len > 0)
			{
				numberOfConsoleItems = 0;
				for (int i = 0; i < CONSOLE_ITEMS; i++)
				{
					const char* itemtext = M_Console_GetItemText(i);
					if (itemtext && q_strcasestr(itemtext, consolemenu.search.text))
					{
						numberOfConsoleItems++;
						if (numberOfConsoleItems == 1)
							console_cursor = i;
					}
				}
			}
			else
			{
				numberOfConsoleItems = CONSOLE_ITEMS;
			}
			return;
		}
	}
	else if (k >= 32 && k < 127)
	{
		if (consolemenu.search.len < sizeof(consolemenu.search.text) - 1)
		{
			consolemenu.search.text[consolemenu.search.len++] = k;
			consolemenu.search.text[consolemenu.search.len] = 0;

			numberOfConsoleItems = 0;
			for (int i = 0; i < CONSOLE_ITEMS; i++)
			{
				const char* itemtext = M_Console_GetItemText(i);
				if (itemtext && q_strcasestr(itemtext, consolemenu.search.text))
				{
					numberOfConsoleItems++;
					if (numberOfConsoleItems == 1)
						console_cursor = i;
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
		M_Menu_Options_f();
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1:
		m_entersound = true;
		if (console_cursor == CONSOLE_FONTSIZE ||
			console_cursor == CONSOLE_HEIGHT ||
			console_cursor == CONSOLE_SPEED ||
			console_cursor == CONSOLE_TRANSPARENCY)
		{
			console_slider_grab = true;
		}
		else
		{
			M_Console_AdjustSliders(1);
		}
		break;

	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		console_cursor--;
		if (console_cursor < 0)
			console_cursor = numberOfConsoleItems - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		console_cursor++;
		if (console_cursor >= numberOfConsoleItems)
			console_cursor = 0;
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

void M_Console_Mousemove(int cx, int cy)
{
	if (console_slider_grab)
	{
		if (!keydown[K_MOUSE1])
		{
			console_slider_grab = false;
			return;
		}

		float f;
		switch (console_cursor)
		{
		case CONSOLE_FONTSIZE:
			f = 1.f + M_MouseToSliderFraction(cx - 187) * 5.f;
			f = (int)f;  // Round to nearest integer
			Cvar_SetValue("scr_conscale", CLAMP(1, f, 6));
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
	int item = (cy - 48) / 8;

	// Make sure the item is within valid range
	if (item >= 0 && item < CONSOLE_ITEMS)
	{
		// Update the cursor position
		console_cursor = item;
	}
}

/*
==================
Misc Menu
==================
*/

extern cvar_t pr_checkextension, r_replacemodels, gl_load24bit, cl_nopext, r_lerpmodels, r_lerpmove, 
sys_throttle, r_particles, sv_nqplayerphysics, cl_nopred, cl_autodemo, cl_smartspawn, cl_bobbing, cl_onload,
cl_pong, scr_hints;

static enum extras_e
{
	EXTRAS_YIELD,
	EXTRAS_DEMOREEL,
	EXTRAS_NETEXTENSIONS,
	EXTRAS_QCEXTENSIONS,
	EXTRAS_PREDICTION,
	EXTRAS_AUTODEMO,
	EXTRAS_SPAWNTRAINER,
	EXTRAS_ITEMBOB,
	EXTRAS_RESETCONFIG,
	EXTRAS_STARTUP,
	EXTRAS_PONG,
	EXTRAS_HINTS,
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
	case EXTRAS_DEMOREEL:
		return "Start Demo Attract";
	case EXTRAS_NETEXTENSIONS:
		return "Protocol Exts";
	case EXTRAS_QCEXTENSIONS:
		return "QC Extensions";
	case EXTRAS_PREDICTION:
		return "Prediction";
	case EXTRAS_AUTODEMO:
		return "Auto Demo";
	case EXTRAS_SPAWNTRAINER:
		return "Spawn Trainer";
	case EXTRAS_ITEMBOB:
		return "Q3 Item Bobbing";
	case EXTRAS_RESETCONFIG:
		return "Reset Config";
	case EXTRAS_STARTUP:
		return "Start-up Screen";
	case EXTRAS_PONG:
		return "Quake Pong";
	case EXTRAS_HINTS:
		return "Paused Hints";
	default:
		q_snprintf(buffer, sizeof(buffer), "Unknown Item %d", index);
		return buffer;
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
	case EXTRAS_DEMOREEL:
		m = cl_demoreel.value+dir;
		if (m < 0)
			m = 2;
		else if (m > 2)
			m = 0;
		Cvar_SetValueQuick (&cl_demoreel, m);
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
	case EXTRAS_SPAWNTRAINER:
		Cvar_SetValue("cl_smartspawn", !cl_smartspawn.value);
		break;
	case EXTRAS_ITEMBOB:
		Cvar_SetValue("cl_bobbing", !cl_bobbing.value);
		break;
	case EXTRAS_RESETCONFIG:
		if (!SCR_ModalMessage("Are you sure you want to\nreset your configuration?\n (^mn^m/^my^m)\n", 0.0f))
			break;
		// Execute config reset commands
		Cbuf_AddText("cfg_reset\n");  // Reset to default config
		Cbuf_AddText("cfg_save\n");   // Save the reset config
		M_Menu_Options_f();           // Return to Options menu
		break;
	case EXTRAS_STARTUP:
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
			else  // If it's a custom command, cycle back to menu
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
			else  // If it's a custom command, cycle back to menu
				Cvar_Set("cl_onload", "menu");
		}
		break;
	case EXTRAS_PONG: // Added Quake Pong toggle
		Cvar_SetValueQuick(&cl_pong, !cl_pong.value);
		break;
	case EXTRAS_HINTS: // Added Paused Hints toggle
		Cvar_SetValueQuick(&scr_hints, !scr_hints.value);
		break;
	case EXTRAS_ITEMS:	//not a real option
		break;
	}
}

void M_Extras_Draw(void)
{
	qpic_t* p;
	enum extras_e i;

	p = Draw_CachePic("gfx/p_option.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	const char* title = "Miscellaneous Options";
	M_PrintWhite((320 - 8 * strlen(title)) / 2, 32, title);

	for (i = 0; i < EXTRAS_ITEMS; i++)
	{
		int y = 48 + 8 * i;
		const char* text = NULL;
		const char* value = NULL;

		switch (i)
		{
		case EXTRAS_YIELD:
			text = "   System Throttle";
			if (fabs(sys_throttle.value - 0.02) < 0.001)
				value = "on";
			else if (sys_throttle.value == 0)
				value = "off";
			else if (sys_throttle.value < -0.9)
				value = "off+when minimized";
			else
				value = "unknown";
			break;

		case EXTRAS_DEMOREEL:
			text = "Start Demo Attract";
			if (cl_demoreel.value > 1)
				value = "on";
			else if (cl_demoreel.value)
				value = "startup only";
			else
				value = "off";
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
			case 2: value = "crx matches only"; break;
			case 3: value = "all maps (online)"; break;
			case 4: value = "all maps (split)"; break;
			default: value = "unknown"; break;
			}
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

		case EXTRAS_STARTUP:
			text = "   Start-up Screen";
			if (!strcmp(cl_onload.string, "") || !strcmp(cl_onload.string, "menu"))
				value = "menu (default)";
			else if (!strcmp(cl_onload.string, "browser"))
				value = "server browser";
			else if (!strcmp(cl_onload.string, "bookmarks"))
				value = "bookmarks";
			else if (!strcmp(cl_onload.string, "save"))
				value = "save menu";
			else if (!strcmp(cl_onload.string, "history"))
				value = "server history";
			else if (!strcmp(cl_onload.string, "console"))
				value = "console";
			else if (!strcmp(cl_onload.string, "demo"))
				value = "demo playback";
			else if (!strncmp(cl_onload.string, "connect ", 8))
				value = va("connect: %s", cl_onload.string + 8);
			else if (!strncmp(cl_onload.string, "exec ", 5))
				value = va("exec: %s", cl_onload.string + 5);
			else if (strchr(cl_onload.string, ' '))
				value = va("cmd: %s", cl_onload.string);
			else
				value = cl_onload.string;
			break;

		case EXTRAS_PONG: // Added Quake Pong display
			text = "        Quake Pong";
			value = cl_pong.value ? "on" : "off";
			break;

		case EXTRAS_HINTS: // Added Paused Hints display
			text = "      Paused Hints";
			value = scr_hints.value ? "on" : "off";
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
	}

	// Draw cursor
	M_DrawCharacter(160, 48 + extras_cursor * 8, 12 + ((int)(realtime * 4) & 1));

	// Draw search box if search is active
	if (extrasmenu.search.len > 0)
	{
		M_DrawTextBox(16, 170, 32, 1);
		M_PrintHighlight(24, 178, extrasmenu.search.text,
			extrasmenu.search.text,
			extrasmenu.search.len);
		int cursor_x = 24 + 8 * extrasmenu.search.len;
		if (numberOfExtrasItems == 0)
			M_DrawCharacter(cursor_x, 178, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, 178, 10 + ((int)(realtime * 4) & 1));
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
			numberOfExtrasItems = EXTRAS_ITEMS;
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
			numberOfExtrasItems = EXTRAS_ITEMS;
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

			// Update filtering based on new search text
			if (extrasmenu.search.len > 0)
			{
				numberOfExtrasItems = 0;
				for (int i = 0; i < EXTRAS_ITEMS; i++)
				{
					const char* itemtext = M_Extras_GetItemText(i);
					if (itemtext && q_strcasestr(itemtext, extrasmenu.search.text))
					{
						numberOfExtrasItems++;
						if (numberOfExtrasItems == 1)
							extras_cursor = i;
					}
				}
			}
			else
			{
				numberOfExtrasItems = EXTRAS_ITEMS;
			}
			return;
		}
	}
	else if (k == K_BACKSPACE)
	{
		if (extrasmenu.search.len > 0)
		{
			extrasmenu.search.text[--extrasmenu.search.len] = 0;
			if (extrasmenu.search.len > 0)
			{
				numberOfExtrasItems = 0;
				for (int i = 0; i < EXTRAS_ITEMS; i++)
				{
					const char* itemtext = M_Extras_GetItemText(i);
					if (itemtext && q_strcasestr(itemtext, extrasmenu.search.text))
					{
						numberOfExtrasItems++;
						if (numberOfExtrasItems == 1)
							extras_cursor = i;
					}
				}
			}
			else
			{
				numberOfExtrasItems = EXTRAS_ITEMS;
			}
			return;
		}
	}
	else if (k >= 32 && k < 127)
	{
		if (extrasmenu.search.len < sizeof(extrasmenu.search.text) - 1)
		{
			extrasmenu.search.text[extrasmenu.search.len++] = k;
			extrasmenu.search.text[extrasmenu.search.len] = 0;

			numberOfExtrasItems = 0;
			for (int i = 0; i < EXTRAS_ITEMS; i++)
			{
				const char* itemtext = M_Extras_GetItemText(i);
				if (itemtext && q_strcasestr(itemtext, extrasmenu.search.text))
				{
					numberOfExtrasItems++;
					if (numberOfExtrasItems == 1)
						extras_cursor = i;
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
		extras_cursor--;
		if (extras_cursor < 0)
			extras_cursor = EXTRAS_ITEMS - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		extras_cursor++;
		if (extras_cursor >= EXTRAS_ITEMS)
			extras_cursor = 0;
		break;

	case K_LEFTARROW:
	case K_MWHEELDOWN:
		M_Extras_AdjustSliders(-1);
		break;

	case K_RIGHTARROW:
	case K_MWHEELUP:
		M_Extras_AdjustSliders(1);
		break;
	}
}

void M_Extras_Mousemove(int cx, int cy)
{
	// Don't process mouse movement if it's in the search box area
	if (extrasmenu.search.len > 0 && cy >= 170)
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
Reset Config Menu
==================
*/

#define MAX_VIS_RESETCONFIG	17

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

	if (resetconfigmenu.list.search.len > 0)
	{
		M_DrawTextBox(16, 180, 32, 1);
		M_PrintHighlight(24, 188, resetconfigmenu.list.search.text,
			resetconfigmenu.list.search.text,
			resetconfigmenu.list.search.len);
		int cursor_x = 24 + 8 * resetconfigmenu.list.search.len;
		if (resetconfigmenu.list.numitems == 0)
			M_DrawCharacter(cursor_x, 188, 11 ^ 128);
		else
			M_DrawCharacter(cursor_x, 188, 10 + ((int)(realtime * 4) & 1));
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

	// Handle Ctrl+U or Ctrl+Backspace first
	if (keydown[K_CTRL])
	{
		if ((key == 'u' || key == 'U') && resetconfigmenu.list.search.len > 0)
		{
			resetconfigmenu.list.search.len = 0;
			resetconfigmenu.list.search.text[0] = 0;
			M_ResetConfig_Refilter();
			return;
		}
		else if (key == K_BACKSPACE && resetconfigmenu.list.search.len > 0)
		{
			M_DeletePrevWord(&resetconfigmenu.list.search);
			M_ResetConfig_Refilter();
			return;
		}
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
		x = m_mousex - resetconfigmenu.x - (resetconfigmenu.cols - 1) * 8;
		y = m_mousey - resetconfigmenu.y;
		if (x < -8 || !M_List_UseScrollbar(&resetconfigmenu.list, y))
			goto enter;
		resetconfigmenu.scrollbar_grab = true;
		M_ResetConfig_Mousemove(m_mousex, m_mousey);

	case K_BACKSPACE:
		if (resetconfigmenu.list.search.len > 0)
		{
			resetconfigmenu.list.search.text[--resetconfigmenu.list.search.len] = 0;
			M_ResetConfig_Refilter();
		}
		break;

	default:
		break;
	}
}
void M_ResetConfig_Char(int key)
{
	if (resetconfigmenu.list.search.len < resetconfigmenu.list.search.maxlen - 1 && key >= 32 && key < 127)
	{
		resetconfigmenu.list.search.text[resetconfigmenu.list.search.len++] = key;
		resetconfigmenu.list.search.text[resetconfigmenu.list.search.len] = 0;
		M_ResetConfig_Refilter();
	}
}

qboolean M_ResetConfig_TextEntry(void)
{
	return true; // Always allow text entry for search
}


void M_ResetConfig_Mousemove(int cx, int cy) // woods #mousemenu
{
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

/*
==================
LAN Config Menu
==================
*/

int		lanConfig_cursor = -1;
int     lanConfig_cursor_table_newgame[] = { 76, 86, 104 }; // Updated cursor positions for "New Game"
int		lanConfig_cursor_table[] = { 76, 94, 102, 108, 116, 124 }; // woods #mousemenu #bookmarksmenu
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
	
	addresses_cached = false;

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
	basex += 8;

	y = 52;
	M_Print(basex, y, "Address:");

	if (!addresses_cached) {
		cached_numaddresses = NET_ListAddresses(cached_addresses, sizeof(cached_addresses) / sizeof(cached_addresses[0]));
		addresses_cached = true;
	}

	if (!cached_numaddresses)
	{
		M_Print(basex + (9 * 8) + 10, y, "NONE KNOWN");
		y += 8;
	}
	else
	{
		// Store clickable areas for IPs and their labels
		// Local IP
		ip_clickables[0].label_x = basex + (9 * 8) + 10;
		ip_clickables[0].label_width = 7 * 8;
		ip_clickables[0].x = basex + (9 * 8) + 10 + 7 * 8;
		ip_clickables[0].y = y;
		ip_clickables[0].width = strlen(cached_addresses[0]) * 8;
		strncpy(ip_clickables[0].text, cached_addresses[0], sizeof(ip_clickables[0].text));

		if (cl_contentfilter.value && !ip_temporarily_visible[0]) // woods #contentfilter
		{
			M_Print(basex + (9 * 8) + 10, y, "local: click to view");
			ip_clickables[0].width = 13 * 8; // Width of "click to view" text
		}
		else 
		{
		M_Print(basex + (9 * 8) + 10, y, va("local: %s", cached_addresses[0]));
		}
		y += 8;

		// External IP
		ip_clickables[1].label_x = basex + (9 * 8) + 10;
		ip_clickables[1].label_width = 7 * 8;
		ip_clickables[1].x = basex + (9 * 8) + 10 + 7 * 8;
		ip_clickables[1].y = y;
		ip_clickables[1].width = strlen(my_public_ip) * 8;
		strncpy(ip_clickables[1].text, my_public_ip, sizeof(ip_clickables[1].text));

		if (cl_contentfilter.value && !ip_temporarily_visible[1]) // woods #contentfilter
		{
			M_Print(basex + (9 * 8) + 10, y, "ext:   click to view");
			ip_clickables[1].width = 13 * 8; // Width of "click to view" text
		}
		else 
		{
		M_Print(basex + (9 * 8) + 10, y, va("ext:   %s", my_public_ip));
		}
		y += 8;
	}

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
		y += 8;
		
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

	if (realtime < copy_message_time)
	{
		char copy_message[256];
		snprintf(copy_message, sizeof(copy_message), "copied %s", last_copied_ip);
		M_PrintRGBA(basex, y + 8, copy_message, CL_PLColours_Parse("0xffffff"), 0.5f, false);
	}

	if (*m_return_reason)
		M_PrintWhite(basex, 148, m_return_reason);
}

void M_LanConfig_Key (int key)
{
	int		l;

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
				M_Menu_Search_f(SLIST_LAN); // woods #localmpfix
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

		if (keydown[K_CTRL])
		{
			if (lanConfig_cursor == 0)
			{
				listsearch_t temp;
				temp.len = strlen(lanConfig_portname);
				Q_strcpy(temp.text, lanConfig_portname);
				M_DeletePrevWord(&temp);
				Q_strcpy(lanConfig_portname, temp.text);
			}
			else if (lanConfig_cursor == 5)
			{
				listsearch_t temp;
				temp.len = strlen(lanConfig_joinname);
				Q_strcpy(temp.text, lanConfig_joinname);
				M_DeletePrevWord(&temp);
				Q_strcpy(lanConfig_joinname, temp.text);
			}
		}
		break;

	case 'u':
	case 'U':
		if (keydown[K_CTRL])
		{
			if (lanConfig_cursor == 0)
				lanConfig_portname[0] = 0;
			else if (lanConfig_cursor == 5)
				lanConfig_joinname[0] = 0;
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

		if (keydown[K_CTRL])
		{
			if (bookmarks_edit_cursor == 0)
			{
				listsearch_t temp;
				temp.len = strlen(temp_name);
				Q_strcpy(temp.text, temp_name);
				M_DeletePrevWord(&temp);
				Q_strcpy(temp_name, temp.text);
			}
			else if (bookmarks_edit_cursor == 1)
			{
				listsearch_t temp;
				temp.len = strlen(temp_alias);
				Q_strcpy(temp.text, temp_alias);
				M_DeletePrevWord(&temp);
				Q_strcpy(temp_alias, temp.text);
			}
		}
		break;

	case 'u':
	case 'U':
		if (keydown[K_CTRL])
		{
			if (bookmarks_edit_cursor == 0)
				temp_name[0] = 0;
			else if (bookmarks_edit_cursor == 1)
				temp_alias[0] = 0;
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

void M_LanConfig_Mousemove(int cx, int cy)
{
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
	int numCommands = StartingGame ? NUM_LANCONFIG_CMDS_NEWGAME : NUM_LANCONFIG_CMDS_JOINGAME;
	M_UpdateCursorWithTable(cy, lanConfig_cursor_ptr, numCommands, &lanConfig_cursor);
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


int gameoptions_cursor_table[] = {40, 56, 64, 72, 80, 88, 96, 104, 120, 128, 152};
#define	NUM_GAMEOPTIONS	11
int		gameoptions_cursor;

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

	y+=8;

	M_Print (0, y, "          Episode");
	// MED 01/06/97 added hipnotic episodes
	if (hipnotic)
		M_Print (160, y, hipnoticepisodes[startepisode].description);
	// PGM 01/07/97 added rogue episodes
	else if (rogue)
		M_Print (160, y, rogueepisodes[startepisode].description);
	else
		M_Print (160, y, episodes[startepisode].description);
	y+=8;

	M_Print (0, y, "            Level");
	// MED 01/06/97 added hipnotic episodes
	if (hipnotic)
	{
		M_Print (160, y, hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel].description);
		if (m_skill_mapname[0])  // Custom map selected - show faded level name
			M_PrintRGBA(160, y + 8, hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel].name,
				CL_PLColours_Parse("0xffffff"), 0.5, false);
		else  // No custom map - show normal level name
			M_PrintWhite (160, y+8, hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel].name);
	}
	// PGM 01/07/97 added rogue episodes
	else if (rogue)
	{
		M_Print (160, y, roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].description);
		if (m_skill_mapname[0])  // Custom map selected - show faded level name
			M_PrintRGBA(160, y+8, roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].name,
				CL_PLColours_Parse("0xffffff"), 0.5, false);
		else  // No custom map - show normal level name
			M_PrintWhite(160, y+8, roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].name);
	}
	else
	{
		M_Print (160, y, levels[episodes[startepisode].firstLevel + startlevel].description);
		if (m_skill_mapname[0])  // Custom map selected - show faded level name
			M_PrintRGBA(160, y+8, levels[episodes[startepisode].firstLevel + startlevel].name,
				CL_PLColours_Parse("0xffffff"), 0.5, false);
		else  // No custom map - show normal level name
			M_PrintWhite(160, y+8, levels[episodes[startepisode].firstLevel + startlevel].name);
	}
	y +=24;
	// Add new option
	M_Print(0, y, "     Custom Level");
	if (m_skill_mapname[0])
		M_PrintWhite(160, y, m_skill_mapname);
	else
		M_Print(160, y, "...");
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
		m_skill_mapname[0] = 0;
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
		m_skill_mapname[0] = 0;
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

	case 10: // Use Custom Map option
		maps_from_gameoptions = true;
		M_Menu_Maps_f();
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

	case K_BACKSPACE:
	case K_DEL:
		if (gameoptions_cursor == 10)
		{
			m_skill_mapname[0] = 0;
		}
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

			if (m_skill_mapname[0])  // If custom map is selected
			{
				Cbuf_AddText(va("map %s\n", m_skill_mapname));
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
	M_UpdateCursorWithTable(cy, gameoptions_cursor_table, NUM_GAMEOPTIONS, &gameoptions_cursor);
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

/*
==================
Server List Menu
==================
*/

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
	{
		SDL_WaitThread(thread1, NULL);
		thread1 = NULL; // Set to NULL after joining
	}
	if (thread2)
	{
		SDL_WaitThread(thread2, NULL);
		thread2 = NULL; // Set to NULL after joining
	}

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

		unsigned char* ch; // woods dequake
		for (ch = (unsigned char*)serverName; *ch; ch++)
			*ch = dequake[*ch];

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

	if (searchLastScope == SLIST_INTERNET)
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

		char pingStrBuffer[8];
		char* pingStrToPrint = pingStrBuffer;

		if (serversmenu.items[idx].ping == -1) {
			pingStrBuffer[0] = '\0';
		}
		else {
			q_snprintf(pingStrBuffer, sizeof(pingStrBuffer), "%3i", serversmenu.items[idx].ping);
			while (*pingStrToPrint == ' ' && *pingStrToPrint != '\0') {
				pingStrToPrint++;
			}
		}

		char linePrefixStr[32];
		q_snprintf(linePrefixStr, sizeof(linePrefixStr), "%-16.16s  %-6.6s %2u/%2u ",
			serversmenu.items[idx].name,
			serversmenu.items[idx].map,
			serversmenu.items[idx].users,
			serversmenu.items[idx].maxusers);

		int current_y_pos = y + i * 8;
		int current_x_pos = x;

		if (server.active) {
			M_PrintWhite(current_x_pos, current_y_pos, linePrefixStr);
		}
		else {
			M_Print(current_x_pos, current_y_pos, linePrefixStr);
		}

		int ping_display_x = current_x_pos + ((int)strlen(linePrefixStr) * 8);

		if (pingStrToPrint[0] != '\0') {
			int current_ping = serversmenu.items[idx].ping;
			if (current_ping <= 60) {
				M_PrintWhite(ping_display_x, current_y_pos, pingStrToPrint); // Green for pings <= 60
			}
			else if (current_ping <= 120) {
				M_Print2(ping_display_x, current_y_pos, pingStrToPrint); // White for pings 61-120
			}
			else { // Pings > 120
				if (server.active) { // Active servers with high ping remain white
					M_PrintWhite(ping_display_x, current_y_pos, pingStrToPrint);
				}
				else { // Inactive servers with high ping use default M_Print color
					M_Print(ping_display_x, current_y_pos, pingStrToPrint);
				}
			}
		}

		if (selected)
			M_DrawCharacter(x - 8, current_y_pos, 12 + ((int)(realtime * 4) & 1));

		char infoStr[40];

		q_snprintf(infoStr, sizeof(infoStr), "%-34.34s", serversmenu.items[idx].name);
		if (selected)
			M_PrintWhite(x, y + serversmenu.list.viewsize * 8 + 12, infoStr);

		q_snprintf(infoStr, sizeof(infoStr), "%-34.34s", serversmenu.items[idx].ip);
		if (selected)
			M_PrintWhite(x, y + serversmenu.list.viewsize * 8 + 20, infoStr);
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

	if (serversmenu.pingThreadRunning)
	{
		pingThreadsShouldExit = true;
		if (serversmenu.pingThread)
		{
			SDL_WaitThread(serversmenu.pingThread, NULL);
			serversmenu.pingThread = NULL; // Set to NULL after joining
		}
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

/*
==================
Mods Menu (iw)
==================
*/

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

/*
==================
Demos Menu
==================
*/

#define MAX_VIS_DEMOS	14

typedef struct
{
	char        name[64];
	char        date[32];
	char        map[64];
	char        players[256];
	char        duration[16];
	char        filesize[16];
	qboolean    active;
	qboolean    parsed;
} demoitem_t;
	
typedef struct
{
	char        map[64];
	char        players[256];
	float       duration;
	float       filesize_mb;
} demoinfo_t;

static void FormatDuration(float secs, char *out, size_t outlen)
{
	int m = (int)(secs / 60);
	int s = (int)(secs + 0.5f) % 60;
	q_snprintf(out, outlen, "%d:%02d", m, s);
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

static qboolean Parse_DemoInfo(const char* name, demoinfo_t* info)
{
	byte* data = COM_LoadMallocFile(va("demos/%s.dem", name), NULL);
	if (!data) return false;

	int length = com_filesize;
	if (length <= 0) { free(data); return false; }

	info->map[0] = info->players[0] = '\0';
	info->duration = 0.0f;
	info->filesize_mb = length / (1024.0f * 1024.0f);

	qboolean map_found = false;
	int maxclients = 16;

	int  player_peak_frags[32];
	memset(player_peak_frags, 0x9D, sizeof(player_peak_frags));   /* -99 */
	qboolean player_has_name[32] = { 0 };
	char player_names[32][MAX_QPATH];
	memset(player_names, 0, sizeof(player_names));

	/* skip header line */
	int off = 0;
	while (off < length && data[off] != '\n') off++;
	if (off < length) off++;

	float last_time = 0.0f;
	int frame_count = 0;

	while (off < length - 16)
	{
		if (off + 4 > length) break;

		int msg_len = LittleLong(*(int*)(data + off));
		off += 4;

		if (msg_len <= 0 || msg_len > 32768 || off + 12 + msg_len > length)
			break;

		off += 12;                    /* skip view angles */
		int msg_end = off + msg_len;  /* inner loop bound */

		while (off < msg_end)
		{
			byte cmd = data[off++];

			switch (cmd)
			{
			case svc_print:
			case svc_stufftext:
				off = SkipCStringOffset(data, off, msg_end);
				break;

			case svc_serverinfo:
			{
				int pos = off;
				while (pos + 4 < msg_end)
				{
					int proto = LittleLong(*(int*)(data + pos));
					pos += 4;

					/* skip extension blocks -------------------------- */
					if (proto == 0x80000001 || proto == 0x80000002      // QuakeSpasm/QSS
						|| proto == 0x31544546 || proto == 0x32455446)    // 'FTE1' / 'FTE2'
					{
						pos += 4;           // skip associated flags/size field
						continue;
					}

					/* FITZQUAKE (666) carries an extra flags int */
					if (proto == 666)
					{
						int fitzFlags = LittleLong(*(int*)(data + pos));
						Con_DPrintf("\n%i", fitzFlags);
						pos += 4;           // …you might want to store this if you care
					}

					/* now the next byte really *is* maxclients */
					if (pos < msg_end)
					{
						maxclients = data[pos];
						if (maxclients > 32) maxclients = 32;
					}
					break;
				}

				if (!map_found)
				{
					for (int i = off; i < msg_end - 5; i++)
					{
						if (!memcmp(data + i, "maps/", 5))
						{
							int j = i + 5;
							while (j < msg_end && data[j] && data[j] != '/') j++;
							int end = j;
							for (; end < msg_end - 4; end++)
								if (!memcmp(data + end, ".bsp", 4)) break;
							if (end < msg_end - 4)
							{
								int nlen = end - (i + 5);
								if (nlen >= (int)sizeof(info->map)) nlen = sizeof(info->map) - 1;
								memcpy(info->map, data + i + 5, nlen);
								info->map[nlen] = '\0';
								char* ext = strstr(info->map, ".bsp");
								if (ext) *ext = '\0';
								map_found = true;
								off = msg_end;           /* early skip */
								goto msg_end_reached;
							}
							break;
						}
					}
				}
				off = msg_end;         /* skip rest */
				goto msg_end_reached;
			}

			case svc_updatename:
				if (off < msg_end)
				{
					int pl = data[off++];
					if (pl >= 0 && pl < maxclients)
					{
						int start = off;
						off = SkipCStringOffset(data, off, msg_end);
						int nlen = off - start - 1;
						if (nlen > 0 && nlen < MAX_QPATH)
						{
							char tmp[MAX_QPATH];
							memcpy(tmp, data + start, nlen); tmp[nlen] = '\0';

							char clean[16]; q_strlcpy(clean, tmp, sizeof(clean));
							size_t len = strlen(clean);
							while (len && isspace((unsigned char)clean[len - 1])) clean[--len] = '\0';

							if (len && strcmp(clean, "unconnected") &&
								strncmp(clean, "Player ", 7))
							{
								q_strlcpy(player_names[pl], clean, sizeof(player_names[pl]));
								player_has_name[pl] = true;
							}
						}
					}
					else off = SkipCStringOffset(data, off, msg_end);
				}
				break;

			case svc_updatefrags:
				if (off + 3 <= msg_end)
				{
					int pl = data[off++];
					int fr = LittleShort(*(int16_t*)(data + off));
					off += 2;
					if (pl >= 0 && pl < maxclients && fr > player_peak_frags[pl])
						player_peak_frags[pl] = fr;
				}
				else goto msg_end_reached;
				break;

			case svc_updatecolors:
				off += (off + 2 <= msg_end) ? 2 : 0;
				break;

			case svc_signonnum:
				off += (off + 1 <= msg_end) ? 1 : 0;
				break;

			case svc_intermission:
				if (map_found) goto build_result;
				break;

			default:
				off = msg_end; /* unknown – skip rest */
				break;
			}
		}
	msg_end_reached:
		off = msg_end;
		if (++frame_count > 50000) break;
	}

build_result:
{
	/* first statement after label is “{” (a block), so declarations are legal */
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

	qsort(list, cnt, sizeof(list[0]), CompareFrags);

	for (int i = 0; i < cnt; i++)
	{
		if (i) q_strlcat(info->players, ", ", sizeof(info->players));
		char buf[MAX_QPATH];
		q_snprintf(buf, sizeof(buf), "%s (%d)",
			list[i].name, list[i].frags);
		q_strlcat(info->players, buf, sizeof(info->players));
	}

	info->duration = last_time;
	if (frame_count > 1000 && info->duration < 60.0f)
		info->duration = frame_count / 40.0f;
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
} demosmenu;

static void M_Demos_FreeItems(void)
{
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
}

static void M_Demos_Add (const char* name, const char* date)
{
    demoitem_t tempDemo;
	
	q_strlcpy(tempDemo.name, name, sizeof(tempDemo.name));
	q_strlcpy(tempDemo.date, date, sizeof(tempDemo.date));
	tempDemo.map[0] = '\0';
	tempDemo.players[0] = '\0';
	tempDemo.duration[0] = '\0';
	tempDemo.filesize[0] = '\0';
    tempDemo.active = false;
	tempDemo.parsed = false;

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

static void M_Demos_Refilter(void)
{
    int i;
    VEC_CLEAR(demosmenu.filtered_indices);

    for (i = 0; i < demosmenu.democount; i++)
    {
        if (demosmenu.list.search.len == 0 ||
            q_strcasestr(demosmenu.items[i].name, demosmenu.list.search.text) ||
            q_strcasestr(demosmenu.items[i].date, demosmenu.list.search.text) ||
            q_strcasestr(demosmenu.items[i].map, demosmenu.list.search.text) ||
            q_strcasestr(demosmenu.items[i].players, demosmenu.list.search.text))
        {
            VEC_PUSH(demosmenu.filtered_indices, i);
        }
    }

    demosmenu.list.numitems = VEC_SIZE(demosmenu.filtered_indices);

    if (demosmenu.list.cursor >= demosmenu.list.numitems)
        demosmenu.list.cursor = demosmenu.list.numitems - 1;

    if (demosmenu.list.cursor < 0 && demosmenu.list.numitems > 0)
        demosmenu.list.cursor = 0;

    M_List_CenterCursor(&demosmenu.list);
}

static void M_Demos_Init(void)
{
    filelist_item_t* item;

    M_Demos_FreeItems();

    demosmenu.list.viewsize = MAX_VIS_DEMOS;
    demosmenu.list.cursor = -1;
    demosmenu.list.scroll = 0;
    demosmenu.democount = 0;
    demosmenu.scrollbar_grab = false;
    VEC_CLEAR(demosmenu.items);
    VEC_CLEAR(demosmenu.filtered_indices);

    memset(&demosmenu.list.search, 0, sizeof(demosmenu.list.search));
    demosmenu.list.search.maxlen = 32;

    M_Ticker_Init (&demosmenu.ticker);

    for (item = demolist; item; item = item->next)
        M_Demos_Add(item->name, item->data);

    M_Demos_Refilter();

    if (demosmenu.list.cursor == -1)
        demosmenu.list.cursor = 0;

    M_List_CenterCursor(&demosmenu.list);
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
        int demo_idx = demosmenu.filtered_indices[idx];
        demoitem_t* demo_item = &demosmenu.items[demo_idx];
        qboolean selected = (idx == demosmenu.list.cursor);

        COM_StripExtension(cls.demofilename, demofilename, sizeof(demofilename));

        demosmenu.items[demo_idx].active = !strcmp(demofilename, demo_item->name);

        int color = demosmenu.items[demo_idx].active ? 0 : 1;
        int len = strlen(demo_item->name);
        int maxchars = (cols - 2);

        if (demosmenu.list.search.len > 0)
        {
            if (len <= maxchars)
            {
                // No scrolling needed, display with highlighting
                M_PrintHighlight(x, y + i * 8, demo_item->name, demosmenu.list.search.text, demosmenu.list.search.len);
            }
            else
            {
                // Scrolling needed, display with scrolling and highlighting
                M_PrintHighlightScroll(x, y + i * 8, (cols - 2) * 8,
				demo_item->name, demosmenu.list.search.text,
				selected ? demosmenu.ticker.scroll_time : 0.0);
            }
        }
        else
        {
            if (len <= maxchars)
            {
                // No scrolling needed
                if (color)
                    M_Print(x, y + i * 8, demo_item->name);
                else
                    M_PrintWhite(x, y + i * 8, demo_item->name);
            }
            else
            {
                // Scrolling needed
                M_PrintScroll(x, y + i * 8, (cols - 2) * 8,
                    demo_item->name,
                    selected ? demosmenu.ticker.scroll_time : 0.0,
                    color);
            }
        }

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

    if (demosmenu.list.cursor >= 0 && demosmenu.list.cursor < demosmenu.list.numitems)
    {
        int demo_idx = demosmenu.filtered_indices[demosmenu.list.cursor];
        demoitem_t* di = &demosmenu.items[demo_idx];
        
        // Lazy parsing: only parse when item is selected for the first time
        if (!di->parsed)
        {
            demoinfo_t info;
            if (Parse_DemoInfo(di->name, &info))
            {
                q_strlcpy(di->map, info.map, sizeof(di->map));
                q_strlcpy(di->players, info.players, sizeof(di->players));
                FormatDuration(info.duration, di->duration, sizeof(di->duration));
                q_snprintf(di->filesize, sizeof(di->filesize), "%.1f mb", info.filesize_mb);
            }
            else
            {
                q_strlcpy(di->map, "unknown", sizeof(di->map));
                q_strlcpy(di->players, "n/a", sizeof(di->players));
                q_strlcpy(di->duration, "n/a", sizeof(di->duration));
                q_strlcpy(di->filesize, "n/a", sizeof(di->filesize));
            }
            di->parsed = true;
        }
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
    return q_tolower(demosmenu.items[demo_idx].name[0]) == initial;
}

void M_Demos_Key(int key)
{
    int x, y; // woods #mousemenu

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

			// Copy demo name to local buffer BEFORE freeing memory
			char demo_name[64];
			q_strlcpy(demo_name, demosmenu.items[demo_idx].name, sizeof(demo_name));

			char demo_path[MAX_OSPATH];

			// Construct the full path to the demo file
			q_snprintf(demo_path, sizeof(demo_path), "%s/demos/%s.dem", com_gamedir, demo_name);

			// Try to delete the file
			if (remove(demo_path) == 0)
			{
				// Successfully deleted file, now remove from demo list
				FileList_Subtract(demo_name, &demolist);

				// Store current cursor position
				int old_cursor = demosmenu.list.cursor;

				// Use the existing function to properly free items
				M_Demos_FreeItems();

				// Rebuild items from updated demolist
				filelist_item_t* item;
				for (item = demolist; item; item = item->next)
					M_Demos_Add(item->name, item->data);

				// Refilter to update the filtered_indices array
				M_Demos_Refilter();

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
            Cbuf_AddText(va("playdemo %s\n", demosmenu.items[demosmenu.filtered_indices[demosmenu.list.cursor]].name));
            M_Menu_Main_f();
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

/*
=========================================
Credit Menu - used by the 2021 re-release
========================================+
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
	{"menu_sound", M_Menu_Sound_f},
	{"menu_game", M_Menu_Game_f},
	{"menu_hud", M_Menu_HUD_f},
	{"menu_crosshair", M_Menu_Crosshair_f},
	{"menu_console", M_Menu_HUD_f},
	{"menu_misc", M_Menu_Extras_f},
	{"menu_config", M_Menu_ResetConfig_f},
	{"menu_video", M_Menu_Video_f},
	{"menu_graphics", M_Menu_Graphics_f},
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

	// Clean up menu memory
	M_Demos_FreeItems();

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

		if (m_state != m_crosshair && !scr_con_current)
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

	case m_mouse:
		M_Mouse_Draw();
		break;

	case m_extras:
		M_Extras_Draw ();
		break;

	case m_resetconfig: // woods #resetconfig
		M_ResetConfig_Draw();
		break;

	case m_video:
		M_Video_Draw ();
		break;

	case m_graphics:
		M_Graphics_Draw();
		break;

	case m_sound:
		M_Sound_Draw ();
		break;

	case m_game:
		M_Game_Draw();
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

	case m_mouse:
		M_Mouse_Key(key);
		return;

	case m_extras:
		M_Extras_Key (key);
		return;

	case m_resetconfig: // woods #resetconfig
		M_ResetConfig_Key(key);
		return;

	case m_video:
		M_Video_Key (key);
		return;

	case m_graphics:
		M_Graphics_Key(key);
		return;

	case m_sound:
		M_Sound_Key (key);
		break;

	case m_game:
		M_Game_Key(key);
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

	case m_mouse:
		M_Mouse_Mousemove(x, y);
		return;

	case m_video:
		M_Video_Mousemove(x, y);
		return;

	case m_graphics:
		M_Graphics_Mousemove(x, y);
		return;

	case m_sound:
		M_Sound_Mousemove(x, y);
		return;

	case m_game:
		M_Game_Mousemove(x, y);
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

	case m_resetconfig: // woods #resetconfig
		M_ResetConfig_Mousemove(x, y);
		break;

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
	case m_resetconfig:
		M_ResetConfig_Char(key);
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
	case m_resetconfig:
		return M_ResetConfig_TextEntry();
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