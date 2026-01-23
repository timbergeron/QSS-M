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

#ifndef __CONSOLE_H
#define __CONSOLE_H

//
// console
//
extern int con_totallines;
extern int con_backscroll;
extern int con_x;	//for testing if there's trailing junk that needs an \n
extern	qboolean con_forcedup;	// because no entities to refresh
extern qboolean con_initialized;
extern byte *con_chars;
extern	int	con_notifylines_; // woods from proquake 493 #notifylines

extern char con_lastcenterstring[]; //johnfitz

void Con_DrawCharacter (int cx, int line, int num);

void Con_CheckResize (void);
void Con_Init (void);
void Con_DrawConsole (int lines, qboolean drawinput);
void Con_Printf (const char *fmt, ...) FUNC_PRINTF(1,2);
void Con_DWarning (const char *fmt, ...) FUNC_PRINTF(1,2); //ericw
void Con_Warning (const char *fmt, ...) FUNC_PRINTF(1,2); //johnfitz
void Con_DPrintf (const char *fmt, ...) FUNC_PRINTF(1,2);
void Con_DPrintf2 (const char *fmt, ...) FUNC_PRINTF(1,2); //johnfitz
void Con_SafePrintf (const char *fmt, ...) FUNC_PRINTF(1,2);
void Con_DrawNotify (void);
void Con_ClearNotify (void);
void Con_ToggleConsole_f (void);
qboolean Con_IsRedirected(void);	//returns true if its redirected. this generally means that things are a little more verbose.
void Con_Redirect(void(*flush)(const char *text));

void Con_NotifyBox (const char *text);	// during startup for sound / cd warnings

typedef enum { // woods #iwtabcomplete
	TABCOMPLETE_AUTOHINT,
	TABCOMPLETE_USER,
} tabcomplete_t;

void Con_Show (void);
void Con_Hide (void);

const char *Con_Quakebar (int len);
void Con_TabComplete (tabcomplete_t mode); // woods #iwtabcomplete
void Con_DedicatedTabComplete(char* text, size_t buf_size, int* textlen, int* cursor_pos);
void Con_DedicatedResetTabState(void);
void Con_AddToTabList (const char* name, const char* partial, const char* type, const char* param); // woods #iwtabcomplete -- add arg #demolistsort
qboolean Con_Match (const char* partial, const char* str); // woods #iwtabcomplete
void Con_LogCenterPrint (const char *str);

// woods #conselection
void Con_ForceMouseMove (void);
void Con_SelectAll (void);
void Con_MoveSelection (int dir_x, int dir_y);
qboolean Con_CopySelectionToClipboard (void);
void Con_ReloadIBeamCursor (void); // woods #customcursor

//
// debuglog
//
void LOG_Init (quakeparms_t *parms);
void LOG_Close (void);
void Con_DebugLog (const char *msg);
void LOG_Maintenance (void);

#endif	/* __CONSOLE_H */

