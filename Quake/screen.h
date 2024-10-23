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

#ifndef _QUAKE_SCREEN_H
#define _QUAKE_SCREEN_H

// screen.h

void SCR_Init (void);
void SCR_LoadPics (void);

void SCR_UpdateScreen (void);

void SCR_UpdateZoom(void); // woods #zoom (ironwail)

void SCR_SizeUp (void);
void SCR_SizeDown (void);
void SCR_BringDownConsole (void);
void SCR_CenterPrint (const char *str);

void SCR_BeginLoadingPlaque (void);
void SCR_EndLoadingPlaque (void);

int SCR_ModalMessage (const char *text, float timeout); //johnfitz -- added timeout

extern	float		scr_con_current;
extern	float		scr_conlines;		// lines of console to display

extern	int			sb_lines;

extern	int			clearnotify;	// set to 0 whenever notify text is drawn
extern	qboolean	scr_disabled_for_loading;
extern	qboolean	scr_skipupdate;

extern	cvar_t		scr_viewsize;

extern	cvar_t		scr_sbaralpha; //johnfitz
extern	cvar_t		scr_sbaralphaqwammo; // woods #sbarstyles
extern	cvar_t		scr_sbarshowqeammo; // woods #sbarstyles
extern	cvar_t		scr_sbar; // woods #sbarstyles

void SCR_UpdateWholeScreen (void);

//johnfitz -- stuff for 2d drawing control
typedef enum {
	CANVAS_NONE,
	CANVAS_DEFAULT,
	CANVAS_DEFAULT2, // woods
	CANVAS_AUTOID, // woods #autoid
	CANVAS_SCOREBOARD, // woods #scoreboard
	CANVAS_CONSOLE,
	CANVAS_MENU,
	CANVAS_MENU2,
	CANVAS_SBAR,
	CANVAS_SBAR2, // woods #speed
	CANVAS_IBAR_QW, // woods #sbarstyles
	CANVAS_CROSSHAIR,
	CANVAS_CROSSHAIR2, // woods #texturepointer
	CANVAS_MATCHCLOCK,
	CANVAS_BOTTOMLEFT,
	CANVAS_BOTTOMLEFT2, // woods #scrping
	CANVAS_SBARQE, // woods for QE hud, scr_sbar 3
	CANVAS_IBAR_QWQE, // woods for QE hud, scr_sbar 3
	CANVAS_BOTTOMLEFTQE, // woods for QE hud, scr_sbar 3
	CANVAS_BOTTOMLEFTQESMALL, // woods for QE hud, scr_sbar 3
	CANVAS_BOTTOMLEFTQESCORES, // woods for QE hud, scr_sbar 3
	CANVAS_BOTTOMRIGHTQE, // woods for QE hud, scr_sbar 3
	CANVAS_BOTTOMRIGHTQESMALL, // woods for QE hud, scr_sbar 3
	CANVAS_MOD, // woods #modprint
	CANVAS_HINT, // woods #qssmhints
	CANVAS_OBSERVER, // woods #observer
	CANVAS_SCORES, // woods #observerhud
	CANVAS_BOTTOMRIGHT,
	CANVAS_TOPRIGHT,
	CANVAS_TOPRIGHT2, // woods #matchhud
	CANVAS_TOPRIGHT3, // woods #matchhud
	CANVAS_TOPRIGHT4, // woods #hud_diff
	CANVAS_CSQC,
	CANVAS_MENUQC,
	CANVAS_INVALID = -1
} canvastype;
extern	cvar_t		scr_menuscale;
extern	cvar_t		scr_sbarscale;
extern	cvar_t		scr_conwidth;
extern	cvar_t		scr_conscale;
extern	cvar_t		scr_consize; // woods #consize (joequake)
extern	cvar_t		scr_scale;
extern	cvar_t		scr_crosshairscale;
extern	cvar_t		scr_crosshaircolor; // woods #crosshair
extern	cvar_t		scr_matchclockscale; // woods #varmatchclock
extern	cvar_t		scr_centerprintbg; // woods #centerprintbg (iw)
//johnfitz

extern int scr_tileclear_updates; //johnfitz

#endif	/* _QUAKE_SCREEN_H */

