/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
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

// screen.c -- master for refresh, status bar, console, chat, notify, etc

#include "time.h"
#include "quakedef.h"

/*

background clear
rendering
turtle/net/ram icons
sbar
centerprint / slow centerprint
notify lines
intermission / finale overlay
loading plaque
console
menu

required background clears
required update regions


syncronous draw mode or async
One off screen buffer, with updates either copied or xblited
Need to double buffer?


async draw will require the refresh area to be cleared, because it will be
xblited, but sync draw can just ignore it.

sync
draw

CenterPrint ()
SlowPrint ()
Screen_Update ();
Con_Printf ();

net
turn off messages option

the refresh is allways rendered, unless the console is full screen


console is:
	notify lines
	half
	full

*/


int			glx, gly, glwidth, glheight;

int ct; // woods connected map time #maptime
extern int	maptime; // woods connected map time #maptime
extern qboolean	sb_showscores; // woods
extern int	fragsort[MAX_SCOREBOARD]; // woods #scrping
extern int	scoreboardlines; // woods #scrping
char mute[2]; // woods for mute to memory #usermute
qboolean pausedprint; // woods #qssmhints
qboolean timerstarted; // woods #qssmhints

float		scr_con_current;
float		scr_conlines;		// lines of console to display

void Sbar_SortFrags(void); // woods #scrping
void Sbar_SortTeamFrags(void); // woods #matchhud
int	Sbar_ColorForMap(int m); // woods #matchhud
void Sbar_DrawCharacter(int x, int y, int num); // woods #matchhud
void Sbar_SortFrags_Obs(void); // woods #observerhud
void Sound_Toggle_Mute_On_f(void); // woods #usermute -- adapted from Fitzquake Mark V

Uint32 HintTimer_Callback(Uint32 interval, void* param); // woods #qssmhints
void Print_Hints_f(void); // woods #qssmhints
extern qboolean netquakeio; // woods

void TexturePointer_Draw(void); // woods #texturepointer

extern qpic_t* sb_nums[2][11]; // woods #varmatchclock
extern qpic_t* sb_colon; // woods #varmatchclock

//johnfitz -- new cvars
cvar_t		scr_menuscale = {"scr_menuscale", "1", CVAR_ARCHIVE};
cvar_t		scr_centerprintbg = {"scr_centerprintbg", "0", CVAR_ARCHIVE}; // 0 = off; 1 = text box; 2 = menu box; 3 = menu strip -- woods #centerprintbg (iw)
cvar_t		scr_sbarscale = {"scr_sbarscale", "1", CVAR_ARCHIVE};
cvar_t		scr_sbaralpha = {"scr_sbaralpha", "0.75", CVAR_ARCHIVE}; // woods #sbarstyles
cvar_t		scr_sbaralphaqwammo = {"scr_sbaralphaqwammo", "1", CVAR_ARCHIVE};
cvar_t		scr_sbarshowqeammo = {"scr_sbarshowqeammo", "1", CVAR_ARCHIVE}; // woods
cvar_t		scr_sbar = {"scr_sbar", "1", CVAR_ARCHIVE}; // woods #sbarstyles
cvar_t		scr_sbarfacecolor = {"scr_sbarfacecolor", "1", CVAR_ARCHIVE}; // woods #teamface
cvar_t		scr_conwidth = {"scr_conwidth", "0", CVAR_ARCHIVE};
cvar_t		scr_conscale = {"scr_conscale", "1", CVAR_ARCHIVE};
cvar_t		scr_consize = {"scr_consize", ".5", CVAR_ARCHIVE}; // woods #consize (joequake)
cvar_t		scr_crosshairscale = {"scr_crosshairscale", "1", CVAR_ARCHIVE};
cvar_t		scr_crosshaircolor = {"scr_crosshaircolor", "0xffffff", CVAR_ARCHIVE}; // woods #crosshair
cvar_t		scr_crosshairalpha = {"scr_crosshairalpha", "1", CVAR_ARCHIVE}; // woods #crosshair
cvar_t		scr_crosshaircshift = { "scr_crosshaircshift", "0xfc7303", CVAR_ARCHIVE}; // woods #crosshair
cvar_t		scr_crosshairoutline = { "scr_crosshairoutline", "1", CVAR_ARCHIVE }; // woods #crosshair
cvar_t		scr_showfps = {"scr_showfps", "0", CVAR_ARCHIVE};
cvar_t		scr_clock = {"scr_clock", "0", CVAR_ARCHIVE};
cvar_t		scr_ping = {"scr_ping", "1", CVAR_ARCHIVE};  // woods #scrping
cvar_t		scr_match_hud = {"scr_match_hud", "1", CVAR_ARCHIVE};  // woods #matchhud
cvar_t		scr_showspeed = {"scr_showspeed", "0",CVAR_ARCHIVE}; // woods #speed
cvar_t		scr_showspeed_y = {"scr_showspeed_y", "176", CVAR_ARCHIVE}; // woods - #speedometer
cvar_t		scr_movekeys = {"scr_movekeys", "0", CVAR_ARCHIVE}; // woods #movementkeys
cvar_t		scr_matchclock = {"scr_matchclock", "0",CVAR_ARCHIVE}; // woods #varmatchclock
cvar_t		scr_matchclock_y = {"scr_matchclock_y", "0",CVAR_ARCHIVE}; // woods #varmatchclock
cvar_t		scr_matchclock_x = {"scr_matchclock_x", "0",CVAR_ARCHIVE}; // woods #varmatchclock
cvar_t		scr_matchclockscale = {"scr_matchclockscale", "1",CVAR_ARCHIVE}; // woods #varmatchclock
cvar_t		scr_showscores = {"scr_showscores", "0",CVAR_ARCHIVE}; // woods #observerhud
cvar_t		scr_shownet = {"scr_shownet", "0",CVAR_ARCHIVE}; // woods #shownet scr_obscenterprint
cvar_t		scr_obscenterprint = {"scr_obscenterprint", "0",CVAR_ARCHIVE}; // woods
cvar_t		scr_hints = {"scr_hints", "1",CVAR_ARCHIVE}; // woods #qssmhints
//johnfitz
cvar_t		scr_usekfont = {"scr_usekfont", "0", CVAR_NONE}; // 2021 re-release
cvar_t		cl_predict = { "cl_predict", "0", CVAR_NONE }; // 2021 re-release

cvar_t		scr_demobar_timeout = {"scr_demobar_timeout", "1", CVAR_ARCHIVE}; // woods (iw) #democontrols
cvar_t		scr_viewsize = {"viewsize","100", CVAR_ARCHIVE};
cvar_t		scr_fov = {"fov","90",CVAR_ARCHIVE};	// 10 - 170
cvar_t		scr_fov_adapt = {"fov_adapt","1",CVAR_ARCHIVE};
cvar_t		scr_zoomfov = {"zoom_fov","30",CVAR_ARCHIVE};	// 10 - 170 // woods #zoom (ironwail)
cvar_t		scr_zoomspeed = {"zoom_speed","8",CVAR_ARCHIVE}; // woods #zoom (ironwail)
cvar_t		scr_conspeed = {"scr_conspeed","500",CVAR_ARCHIVE};
cvar_t		scr_centertime = {"scr_centertime","2",CVAR_NONE};
cvar_t		scr_showturtle = {"showturtle","0",CVAR_NONE};
cvar_t		scr_showpause = {"showpause","1",CVAR_NONE};
cvar_t		scr_printspeed = {"scr_printspeed","8",CVAR_NONE};
cvar_t		gl_triplebuffer = {"gl_triplebuffer", "1", CVAR_ARCHIVE};

cvar_t		cl_gun_fovscale = {"cl_gun_fovscale","1",CVAR_ARCHIVE}; // Qrack
cvar_t		cl_menucrosshair = { "cl_menucrosshair","0",CVAR_ARCHIVE}; // woods #menucrosshair

extern	cvar_t	crosshair;
extern	cvar_t	con_notifyfade; // woods #confade
extern	cvar_t	con_notifyfadetime; // woods #confade

qboolean	scr_initialized;		// ready to draw

qpic_t		*scr_net;
qpic_t		*scr_turtle;

void Sbar_DrawPicAlpha(int x, int y, qpic_t* pic, float alpha); // woods for loading #flagstatus alpha

int			clearconsole;
int			clearnotify;

vrect_t		scr_vrect;

qboolean	scr_disabled_for_loading;
qboolean	scr_drawloading;
float		scr_disabled_time;

int	scr_tileclear_updates = 0; //johnfitz

void SCR_ScreenShot_f (void);
void TP_DrawClosestLocText(void); // woods #locext

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

char		scr_centerstring[1024];
float		scr_centertime_start;	// for slow victory printing
float		scr_centertime_off;
int			scr_center_lines;
int			scr_center_maxcols; // woods #centerprintbg (iw)
int			scr_erase_lines;
int			scr_erase_center;
#define CPRINT_TYPEWRITER	(1u<<0)
#define CPRINT_PERSIST		(1u<<1)
#define CPRINT_TALIGN		(1u<<2)
unsigned int scr_centerprint_flags;

int paused = 0; // woods #showpaused
qboolean	countdown; // #clearcrxcountdown
qboolean	cameras; // woods #crxcamera
qboolean	qeintermission; // woods #qeintermission
qboolean draw; // woods #crxcamera #qeintermission
qboolean crxintermission; // woods #crxintermission

/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void SCR_CenterPrint (const char *str) //update centerprint data
{
	unsigned int flags = 0;

	countdown = false; // woods #clearcrxcountdown
	cameras = false; // woods #crxcamera
	qeintermission = false; // woods #qeintermission
	pausedprint = false; // woods #qssmhints

	if (strstr(str, "eyecam") || strstr(str, "chasecam")) // woods #crxcamera
		cameras = true;

	if (cl.modtype == 1) // woods #crxintermission
	{
		const char* val1;
		char buf6[5];
		val1 = Info_GetKey(cl.serverinfo, "timelimit", buf6, sizeof(buf6));

		const char* val2;
		char buf7[10];
		val2 = Info_GetKey(cl.serverinfo, "playmode", buf7, sizeof(buf7));

		const char* val3;
		char buf8[12];
		val3 = Info_GetKey(cl.serverinfo, "intermission", buf8, sizeof(buf8));

		if ((cl.time > atoi(val1) * 60 && (!strcmp(val2, "ffa") || !strcmp(val2, "pug"))) || !strcmp(val3, "on"))
			crxintermission = true;
	}

	if (cl.modtype == 4) // woods #qeintermission
	{ 
		char qfVote[5] = { 214, 239, 244, 229, '\0' }; // quake font red 'Vote'
		char qfTDM[4] = { 212, 196, 205, '\0' }; // quake font red 'TDM'
		char qfMatch[6] = { 205, 225, 244, 227, 232, '\0' }; // quake font red 'Match'
		char qfSummary[8] = { 211, 245, 237, 237, 225, 242, 249, '\0' }; // quake font red 'Summary'

		if (strstr(str, qfVote) || strstr(str, qfTDM) || (strstr(str, qfMatch) && strstr(str, qfSummary))) // woods #qeintermission (Vote For, TDM Stats, Match Summary)  
			qeintermission = true;
		else
			qeintermission = false;
	}

	char qfcountdown[11] = { 227, 239, 245, 238, 244, 228, 239, 247, 238, 186, '\0' }; // woods -- quake font red 'countdown:'

	if (strstr(str, qfcountdown)) // woods #clearcrxcountdown (countdown)
		countdown = true;
	else
		countdown = false;

	char qfPAUSED[7] = { 208, 193, 213, 211, 197, 196, '\0' }; // woods -- quake font red 'PAUSED'

	if ((strstr(str, qfPAUSED)) || (strstr(str, "PAUSED"))) // #showpaused
	{
		pausedprint = true; // woods #qssmhints
		return;
	}

	char qfrunequake[18] = { 247, 247, 247, 174, 242, 245, 238, 229, 241, 245, 225, 235, 229, 174, 227, 239, 237, '\0' }; // woods -- quake font red 'www.runequake.com' #autoid

	if ((strstr(str, qfrunequake)))
	{
		cl.modtype = 6;
		strncpy(cl.observer, "y", sizeof(cl.observer));
		return;
	}

// ===============================
// woods for center print filter  -> this is #flagstatus
// ===============================

 // begin woods for flagstatus parsing for legacy mods without infokeys

	const char* blueflag;
	char buf[10];
	blueflag = Info_GetKey(cl.serverinfo, "blue flag", buf, sizeof(buf));

	if (blueflag[0] == '\0' || cls.demoplayback) // we only use this if the server does NOT have a infokey for flag status
	{ 
		strncpy(cl.flagstatus, "n", sizeof(cl.flagstatus)); // null flag, reset all flag ... flags :)

		char qfleftbrnbigbrkt[2] = { 128, '\0' }; // quake font left bigger brown bracket

		char qfrbrnsep[3] = { 114, 158, '\0' }; // regular font 'r' + quake font brown spacer -- RED TAKEN
		char qfbrrtbrnbrkt[3] = { 98, 159, '\0' }; // regular font 'b' + quake font brown right smaller bracket -- BLUE TAKEN

		char qfredrbrnsep[3] = { 242, 158, '\0' }; // quake font red 'r' + quake font brown spacer -- RED ABANDONED
		char qfredbrrtbrnbrkt[3] = { 226, 159, '\0' }; // quake font red 'b' + quake font brown right smaller bracket -- BLUE ABANDONED

		if (!strpbrk(str, qfleftbrnbigbrkt)) // crmod MOD print
		{
			// RED

			if (strstr(str, qfrbrnsep) && !strstr(str, qfbrrtbrnbrkt) && !strstr(str, qfredbrrtbrnbrkt)) // red taken
				strncpy(cl.flagstatus, "r", sizeof(cl.flagstatus));

			if (strstr(str, qfredrbrnsep) && !strstr(str, qfbrrtbrnbrkt) && !strstr(str, qfredbrrtbrnbrkt)) // red abandoned
				strncpy(cl.flagstatus, "x", sizeof(cl.flagstatus));

		// BLUE

			if (strstr(str, qfbrrtbrnbrkt) && !strstr(str, qfrbrnsep) && !strstr(str, qfredrbrnsep)) // blue taken
				strncpy(cl.flagstatus, "b", sizeof(cl.flagstatus));

			if (strstr(str, qfredbrrtbrnbrkt) && !strstr(str, qfrbrnsep) && !strstr(str, qfredrbrnsep)) // blue abandoned
				strncpy(cl.flagstatus, "y", sizeof(cl.flagstatus));

		// RED & BLUE

			if ((strstr(str, qfbrrtbrnbrkt)) && (strstr(str, qfrbrnsep))) //  blue & red taken
				strncpy(cl.flagstatus, "p", sizeof(cl.flagstatus));

			if ((strstr(str, qfredbrrtbrnbrkt)) && (strstr(str, qfredrbrnsep))) // blue & red abandoned
				strncpy(cl.flagstatus, "z", sizeof(cl.flagstatus));

			if ((strstr(str, qfredbrrtbrnbrkt)) && (strstr(str, qfrbrnsep))) // blue abandoned, red taken
				strncpy(cl.flagstatus, "j", sizeof(cl.flagstatus));

			if ((strstr(str, qfbrrtbrnbrkt)) && (strstr(str, qfredrbrnsep))) // red abandoned, blue taken
				strncpy(cl.flagstatus, "k", sizeof(cl.flagstatus));
		}
	}

	// end woods for flagstatus parsing

	if (!strcmp(str, "You found a secret area!") && cl.gametype == GAME_DEATHMATCH)
		return;

	if (!strcmp(str, "Your team captured the flag!\n") ||
		!strcmp(str, "Your flag was captured!\n"))
		return;

	char qfflag[5] = { 230, 236, 225, 231, '\0' }; // woods  -- quake font red lowercase 'flag'
	char qfupFLAG[5] = { 198, 204, 193, 199, '\0' }; // woods -- quake font red uppercase 'FLAG'

	char fgmsgbuffer[42];

	q_snprintf(fgmsgbuffer, sizeof(fgmsgbuffer), "Enemy %s has been returned to base!", qfflag);
	if (!strcmp(str, fgmsgbuffer))
		return;

	q_snprintf(fgmsgbuffer, sizeof(fgmsgbuffer), "Your %s has been taken!", qfupFLAG);
	if (!strcmp(str, fgmsgbuffer))
		return;

	q_snprintf(fgmsgbuffer, sizeof(fgmsgbuffer), "Your team has the enemy %s!", qfupFLAG);
	if (!strcmp(str, fgmsgbuffer))
		return;

	q_snprintf(fgmsgbuffer, sizeof(fgmsgbuffer), "Your %s has been returned to base!", qfflag);
	if (!strcmp(str, fgmsgbuffer))
		return;

	if (*str != '/' && cl.intermission)
		flags |= CPRINT_TYPEWRITER | CPRINT_PERSIST | CPRINT_TALIGN;

	//check for centerprint prefixes/flags
	while (*str == '/')
	{
		if (str[1] == '.')
		{	//no more
			str+=2;
			break;
		}
		else if (str[1] == 'P')
			flags |= CPRINT_PERSIST;
		else if (str[1] == 'W')	//typewriter
			flags ^= CPRINT_TYPEWRITER;
		else if (str[1] == 'S')	//typewriter
			flags ^= CPRINT_PERSIST;
		else if (str[1] == 'M')	//masked background
			;
		else if (str[1] == 'O')	//obituary print (lower half)
			;
		else if (str[1] == 'B')	//bottom-align
			;
		else if (str[1] == 'B')	//top-align
			;
		else if (str[1] == 'L')	//left-align
			;
		else if (str[1] == 'R')	//right-align
			;
		else if (str[1] == 'F')	//alternative 'finale' control
		{
			str+=2;
			if (!cl.intermission)
				cl.completed_time = cl.time;
			switch(*str++)
			{
			case 0:
				str--;
				break;
			case 'R':	//remove intermission (no other method to do this)
				cl.intermission = 0;
				break;
			case 'I':	//regular intermission
			case 'S':	//show scoreboard
				cl.intermission = 1;
				break;
			case 'F':	//like svc_finale
				cl.intermission = 2;
				break;
			default:
				break;	//any other flag you want
			}
			vid.recalc_refdef = true;
			continue;
		}
		else if (str[1] == 'I')	//title image
		{
			const char *e;
			str+=2;
			e = strchr(str, ':');
			if (!e)
				e = strchr(str, ' ');	//probably an error
			if (!e)
				e = str+strlen(str)-1;	//error
			str = e+1;
			continue;
		}
		else
			break;
		str+=2;
	}

	int cols; // woods #centerprintbg (iw)
	q_strlcpy(scr_centerstring, str, sizeof(scr_centerstring)); // woods #centerprintbg (iw)
	if (!scr_centerstring[0]) // woods #centerprintbg (iw)
	{
		scr_center_lines = 0;
		scr_center_maxcols = 0;
		return;
	}

	scr_centertime_off = (flags&CPRINT_PERSIST)?999999:scr_centertime.value;
	scr_centertime_start = cl.time;

	if (!cl.intermission)
		scr_centertime_off += q_max(0.f, con_notifyfade.value * con_notifyfadetime.value); // woods #confade

	if (*scr_centerstring && !(flags&CPRINT_PERSIST))
		Con_LogCenterPrint (scr_centerstring);

// count the number of lines for centering
	scr_center_lines = 1;
	scr_center_maxcols = 0; // woods #centerprintbg (iw)
	str = scr_centerstring;
	cols = 0; // woods #centerprintbg (iw)
	while (*str)
	{
		if (*str == '\n')
		{
			scr_center_lines++;
			scr_center_maxcols = q_max(scr_center_maxcols, cols); // woods #centerprintbg (iw)
			cols = -1; // compensate the following ++
		}
		str++;
		cols++;
	}
	scr_center_maxcols = q_max(scr_center_maxcols, cols);
}

static void SCR_DrawCenterStringBG(int y, float alpha) // woods #centerprintbg (iw)
{
	const char* str;
	int i, len, lines, x;

	if (cl.intermission || q_min(scr_center_lines, scr_center_maxcols) <= 0 || alpha <= 0.f)
		return;

	// skip leading empty lines (might be there just to reposition the text)
	str = scr_centerstring;
	while (*str == '\n')
	{
		str++;
		y += CHARSIZE;
	}

	// skip trailing empty lines
	len = (int)strlen(str);
	while (len > 0 && str[len - 1] == '\n')
		--len;

	// count remaining lines
	for (i = 0, lines = 1; i < len; i++)
		if (str[i] == '\n')
			lines++;

	// draw the background
	switch ((int)scr_centerprintbg.value)
	{
	case 1:
		len = (scr_center_maxcols + 3) & ~1;
		x = (320 - len * 8) / 2;
		M_DrawTextBox_WithAlpha(x - 8, y - 12, len, lines + 1, alpha);
		break;

	case 2:
		len = scr_center_maxcols + 2;
		x = (320 - len * 8) / 2;
		Draw_FillPlayer(x, y - 4, len * 8, lines * 8 + 8, CL_PLColours_Parse("0x000000"), alpha-0.5f);

		break;

	case 3:
		Draw_FillPlayer(-(glwidth / 2), y - 4, glwidth, lines * 8 + 8, CL_PLColours_Parse("0x000000"), alpha - 0.2f);
			break;

	default:
		return;
	}
}

void SCR_DrawCenterString (void) //actually do the drawing
{
	char	*start;
	int		l;
	int		j;
	int		x, y;
	int		remaining;
	float	alpha; // woods #confade

	char buf[15];
	const char* realobs;
	realobs = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "observer", buf, sizeof(buf));

	if (!scr_obscenterprint.value && !cameras && !countdown && !qeintermission && ((cl.modtype == 1 || cl.modtype == 4) && (!strcmp(realobs, "eyecam") || (!strcmp(realobs, "chase")))))
		return;

	if (!strcmp(cl.observer, "y") && (cl.modtype >= 2)) // woods #observer
		GL_SetCanvas(CANVAS_OBSERVER); //johnfitz //  center print moved down near weapon
	else
		GL_SetCanvas(CANVAS_MOD); //johnfitz // woods messages scale with console font size instead

// the finale prints the characters one at a time
	if (cl.intermission)
	{
		remaining = scr_printspeed.value * (cl.time - scr_centertime_start);
		alpha = 1.f; // woods #confade
	}
	else
	{
		float fade = q_max(con_notifyfade.value * con_notifyfadetime.value, 0.f); // woods #confade
		remaining = 9999;
		alpha = fade ? q_min(scr_centertime_off / fade, 1.f) : 1.f; // woods #confade
	}

	scr_erase_center = 0;
	start = scr_centerstring;

	if (scr_center_lines <= 4)
		y = 200*0.35;	//johnfitz -- 320x200 coordinate system
	else
		y = 48;
	if (crosshair.value)
		y -= 8;

	SCR_DrawCenterStringBG(y, alpha);

	do
	{
	// scan the width of the line
		for (l=0 ; l<40 ; l++)
			if (start[l] == '\n' || !start[l])
				break;
		x = (320 - l*8)/2;	//johnfitz -- 320x200 coordinate system
		for (j=0 ; j<l ; j++, x+=8)
		{
			Draw_CharacterRGBA (x, y, start[j], CL_PLColours_Parse("0xffffff"), alpha);	//johnfitz -- stretch overlays
			if (!remaining--)
				return;
		}

		y += 8;

		while (*start && *start != '\n')
			start++;

		if (!*start)
			break;
		start++;		// skip the \n
	} while (1);
}

void SCR_CheckDrawCenterString (void)
{
	draw = false; // woods #crxcamera #qeintermission
	
	if (scr_center_lines > scr_erase_lines)
		scr_erase_lines = scr_center_lines;

	scr_centertime_off -= host_frametime;

	if (scr_centertime.value <= 0) // woods #confade
		scr_centertime_off = 0;

	if (scr_centertime_off <= 0 && !cl.intermission)
		return;
	if (key_dest != key_game)
		return;
	if (cl.paused) //johnfitz -- don't show centerprint during a pause
		return;

	if (cl.paused) //johnfitz -- don't show centerprint during a pause
		return;

	if (sb_showscores == true && (cl.gametype == GAME_DEATHMATCH)) // woods don't overlap centerprints with scoreboard
		return;

	draw = true; // woods #crxcamera #qeintermission

	SCR_DrawCenterString ();
}

// woods #zoom (ironwail) SCR_ToggleZoom_f, SCR_ZoomDown_f, SCR_ZoomUp_f, SCR_UpdateZoom

/*
====================
SCR_ToggleZoom_f
====================
*/
static void SCR_ToggleZoom_f(void)
{
	if (cl.zoomdir)
		cl.zoomdir = -cl.zoomdir;
	else
		cl.zoomdir = cl.zoom > 0.5f ? -1.f : 1.f;
}

/*
====================
SCR_ZoomDown_f
====================
*/
static void SCR_ZoomDown_f(void)
{
	cl.zoomdir = 1.f;
}

/*
====================
SCR_ZoomUp_f
====================
*/
static void SCR_ZoomUp_f(void)
{
	cl.zoomdir = -1.f;
}

/*
====================
SCR_UpdateZoom
====================
*/
void SCR_UpdateZoom(void)
{
	float speed = scr_zoomspeed.value > 0.f ? scr_zoomspeed.value : 1e6;
	float delta = cl.zoomdir * speed * (cl.time - cl.oldtime);
	if (!delta)
		return;
	cl.zoom += delta;
	if (cl.zoom >= 1.f)
	{
		cl.zoom = 1.f;
		cl.zoomdir = 0.f;
	}
	else if (cl.zoom <= 0.f)
	{
		cl.zoom = 0.f;
		cl.zoomdir = 0.f;
	}
	vid.recalc_refdef = 1;
}

//=============================================================================

/*
====================
AdaptFovx
Adapt a 4:3 horizontal FOV to the current screen size using the "Hor+" scaling:
2.0 * atan(width / height * 3.0 / 4.0 * tan(fov_x / 2.0))
====================
*/
float AdaptFovx (float fov_x, float width, float height)
{
	float	a, x;

	if (cl.statsf[STAT_VIEWZOOM])
		fov_x *= cl.statsf[STAT_VIEWZOOM]/255.0;
	if (fov_x < 1)
		fov_x = 1;
	if (fov_x > 179)
		fov_x = 179;

	if (!scr_fov_adapt.value)
		return fov_x;
	if ((x = height / width) == 0.75)
		return fov_x;
	a = atan(0.75 / x * tan(fov_x / 360 * M_PI));
	a = a * 360 / M_PI;
	return a;
}

/*
====================
CalcFovy
====================
*/
float CalcFovy (float fov_x, float width, float height)
{
	float	a, x;

	if (fov_x < 1 || fov_x > 179)
		Sys_Error ("Bad fov: %f", fov_x);

	x = width / tan(fov_x / 360 * M_PI);
	a = atan(height / x);
	a = a * 360 / M_PI;
	return a;
}

/*
=================
SCR_CalcRefdef

Must be called whenever vid changes
Internal use only
=================
*/
static void SCR_CalcRefdef (void)
{
	float		size, scale; //johnfitz -- scale
	float		zoom; // woods #zoom (ironwail)

// force the status bar to redraw
	Sbar_Changed ();

	scr_tileclear_updates = 0; //johnfitz

// bound viewsize
	if (scr_viewsize.value < 30)
		Cvar_SetQuick (&scr_viewsize, "30");
	if (scr_viewsize.value > 120)
		Cvar_SetQuick (&scr_viewsize, "130");

// bound fov
	if (scr_fov.value < 10)
		Cvar_SetQuick (&scr_fov, "10");
	if (scr_fov.value > 170)
		Cvar_SetQuick (&scr_fov, "170");
	if (scr_zoomfov.value < 10) // woods #zoom (ironwail)
		Cvar_SetQuick(&scr_zoomfov, "10");
	if (scr_zoomfov.value > 170)
		Cvar_SetQuick(&scr_zoomfov, "170"); // woods #zoom (ironwail)

	vid.recalc_refdef = 0;

	//johnfitz -- rewrote this section
	size = scr_viewsize.value;
	scale = CLAMP (1.0f, scr_sbarscale.value, (float)glwidth / 320.0f);

	if (size >= 120 || cl.intermission || (scr_sbaralpha.value < 1 || cl.qcvm.extfuncs.CSQC_DrawHud || cl.qcvm.extfuncs.CSQC_UpdateView)) //johnfitz -- scr_sbaralpha.value. Spike -- simple csqc assumes fullscreen video the same way.
		sb_lines = 0;
	else if (size >= 110)
		sb_lines = 24 * scale;
	else
		sb_lines = 48 * scale;

	size = q_min(scr_viewsize.value, 100.f) / 100;
	//johnfitz

	//johnfitz -- rewrote this section
	r_refdef.vrect.width = q_max(glwidth * size, 96.0f); //no smaller than 96, for icons
	r_refdef.vrect.height = q_min((int)(glheight * size), glheight - sb_lines); //make room for sbar
	r_refdef.vrect.x = (glwidth - r_refdef.vrect.width)/2;
	r_refdef.vrect.y = (glheight - sb_lines - r_refdef.vrect.height)/2;
	//johnfitz

	zoom = cl.zoom;
	zoom *= zoom * (3.f - 2.f * zoom); // smoothstep // woods #zoom (ironwail)
	r_refdef.basefov = LERP(scr_fov.value, scr_zoomfov.value, zoom); // woods #zoom (ironwail)
	r_refdef.fov_x = AdaptFovx(r_refdef.basefov, vid.width, vid.height); // woods #zoom (ironwail)
	r_refdef.fov_y = CalcFovy (r_refdef.fov_x, r_refdef.vrect.width, r_refdef.vrect.height); // woods #zoom (ironwail)

	scr_vrect = r_refdef.vrect;
}


/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
void SCR_SizeUp_f (void)
{
	Cvar_SetValueQuick (&scr_viewsize, scr_viewsize.value+10);
}


/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
void SCR_SizeDown_f (void)
{
	Cvar_SetValueQuick (&scr_viewsize, scr_viewsize.value-10);
}

static void SCR_Callback_refdef (cvar_t *var)
{
	vid.recalc_refdef = 1;
	if (key_dest == key_game && host_initialized && scr_viewsize.value != 140 && scr_viewsize.value != 20) // woods
		Con_Printf("screen size: ^m%i\n", (int)scr_viewsize.value);
}

/*
=================
CompleteViewsize_f -- woods #scrviewsize

For tab complete
=================
*/
static void CompleteViewsize_f (cvar_t* cvar, const char* partial)
{
	if (Cmd_Argc() != 2)
		return;

	const char* viewSizes[] = { "30", "40", "50", "60", "70", "80", "90", "100", "110", "120", "130" };
	int viewSizesCount = sizeof(viewSizes) / sizeof(viewSizes[0]);

	for (int i = 0; i < viewSizesCount; ++i) {
		Con_AddToTabList (viewSizes[i], partial, NULL, NULL);
	}
}

/*
==================
SCR_Conwidth_f -- johnfitz -- called when scr_conwidth or scr_conscale changes
==================
*/
void SCR_Conwidth_f (cvar_t *var)
{
	vid.recalc_refdef = 1;
	vid.conwidth = (scr_conwidth.value > 0) ? (int)scr_conwidth.value : (scr_conscale.value > 0) ? (int)(vid.width/scr_conscale.value) : vid.width;
	vid.conwidth = CLAMP (320, vid.conwidth, vid.width);
	vid.conwidth &= 0xFFFFFFF8;
	vid.conheight = vid.conwidth * vid.height / vid.width;
}

//============================================================================

/*
==================
SCR_LoadPics -- johnfitz
==================
*/
void SCR_LoadPics (void)
{
	scr_net = Draw_PicFromWad ("net");
	scr_turtle = Draw_PicFromWad ("turtle");
}

/*
===============
Crosshair_Color_Completion_f -- woods #iwtabcomplete
===============
*/
static void Crosshair_Color_Completion_f(cvar_t* cvar, const char* partial)
{
	Con_AddToTabList("0xffffff", partial, "white", NULL); // #demolistsort add arg
	Con_AddToTabList("0x00d11c", partial, "bright green", NULL); // #demolistsort add arg
	Con_AddToTabList("0xff0000", partial, "red", NULL); // #demolistsort add arg

	return;
}

/*
==================
SCR_Init
==================
*/
void SCR_Init (void)
{
	//johnfitz -- new cvars
	Cvar_RegisterVariable (&scr_menuscale);
	Cvar_RegisterVariable (&scr_centerprintbg); // woods #centerprintbg (iw)
	Cvar_RegisterVariable (&scr_sbarscale);
	Cvar_SetCallback (&scr_sbaralpha, SCR_Callback_refdef);
	Cvar_RegisterVariable (&scr_sbaralpha);
	Cvar_RegisterVariable (&scr_sbaralphaqwammo); // woods #sbarstyles
	Cvar_RegisterVariable (&scr_sbarshowqeammo); // woods #sbarstyles
	Cvar_RegisterVariable (&scr_sbar); // woods #sbarstyles
	Cvar_RegisterVariable (&scr_sbarfacecolor); // woods #teamface
	Cvar_SetCallback (&scr_conwidth, &SCR_Conwidth_f);
	Cvar_SetCallback (&scr_conscale, &SCR_Conwidth_f);
	Cvar_RegisterVariable (&scr_conwidth);
	Cvar_RegisterVariable (&scr_conscale);
	Cvar_RegisterVariable (&scr_consize); // woods #consize (joequake)
	Cvar_RegisterVariable (&scr_crosshairscale);
	Cvar_RegisterVariable (&scr_crosshaircolor); // woods #crosshair
	Cvar_SetCompletion (&scr_crosshaircolor, &Crosshair_Color_Completion_f); // woods #iwtabcomplete
	Cvar_RegisterVariable (&scr_crosshairalpha); // woods #crosshair
	Cvar_RegisterVariable (&scr_crosshaircshift); // woods #crosshair
	Cvar_RegisterVariable (&scr_crosshairoutline); // woods #crosshair
	Cvar_RegisterVariable (&scr_showfps);
	Cvar_RegisterVariable (&scr_clock);
	Cvar_RegisterVariable (&scr_ping); // woods #scrping
	Cvar_RegisterVariable(&scr_match_hud); // woods #matchhud
	Cvar_RegisterVariable (&scr_showspeed); // woods #speed
	Cvar_RegisterVariable (&scr_showspeed_y); // woods #speedometer
	Cvar_RegisterVariable (&scr_movekeys); // woods #movementkeys
	Cvar_RegisterVariable (&scr_matchclock); // woods #varmatchclock
	Cvar_RegisterVariable (&scr_matchclock_y); // woods #varmatchclock
	Cvar_RegisterVariable (&scr_matchclock_x); // woods #varmatchclock
	Cvar_RegisterVariable (&scr_matchclockscale); // woods #varmatchclock
	Cvar_RegisterVariable (&scr_showscores); // woods #observerhud
	Cvar_RegisterVariable (&scr_shownet); // woods #shownet
	Cvar_RegisterVariable (&scr_obscenterprint); // woods
	Cvar_RegisterVariable (&scr_hints); // woods #qssmhints
	//johnfitz
	Cvar_RegisterVariable(&scr_demobar_timeout); // woods (iw) #democontrols
	Cvar_RegisterVariable (&scr_usekfont); // 2021 re-release
	Cvar_RegisterVariable (&cl_predict); // 2021 re-release
	Cvar_SetCallback (&scr_fov, SCR_Callback_refdef);
	Cvar_SetCallback (&scr_fov_adapt, SCR_Callback_refdef); // woods #zoom (ironwail)
	Cvar_SetCallback (&scr_zoomfov, SCR_Callback_refdef);
	Cvar_SetCallback (&scr_viewsize, SCR_Callback_refdef);
	Cvar_RegisterVariable (&scr_fov);
	Cvar_RegisterVariable (&scr_fov_adapt); // woods #zoom (ironwail)
	Cvar_RegisterVariable(&scr_zoomfov); // woods #zoom (ironwail)
	Cvar_RegisterVariable(&scr_zoomspeed);
	Cvar_RegisterVariable (&scr_viewsize);
	Cvar_SetCompletion (&scr_viewsize, CompleteViewsize_f); // woods #scrviewsize
	Cvar_RegisterVariable (&scr_conspeed);
	Cvar_RegisterVariable (&scr_showturtle);
	Cvar_RegisterVariable (&scr_showpause);
	Cvar_RegisterVariable (&scr_centertime);
	Cvar_RegisterVariable (&scr_printspeed);
	Cvar_RegisterVariable (&gl_triplebuffer);
	Cvar_RegisterVariable (&cl_gun_fovscale);
	Cvar_RegisterVariable (&cl_menucrosshair); // woods #menucrosshair

	Cmd_AddCommand ("screenshot",SCR_ScreenShot_f);
	Cmd_AddCommand ("sizeup",SCR_SizeUp_f);
	Cmd_AddCommand ("sizedown",SCR_SizeDown_f);

	Cmd_AddCommand("togglezoom", SCR_ToggleZoom_f); // woods #zoom (ironwail)
	Cmd_AddCommand("+zoom", SCR_ZoomDown_f); // woods #zoom (ironwail)
	Cmd_AddCommand("-zoom", SCR_ZoomUp_f); // woods #zoom (ironwail)
	Cmd_AddCommand("hints", Print_Hints_f); // woods #hints

	SCR_LoadPics (); //johnfitz

	scr_initialized = true;

	LoadCustomCursorImage (); // woods #customcursor
}

//============================================================================

/*
==============
SCR_DrawFPS -- johnfitz
==============
*/
void SCR_DrawFPS (void)
{
	static double	oldtime = 0;
	static double	lastfps = 0;
	static int	oldframecount = 0;
	double	elapsed_time;
	int	frames;
	int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3); // woods

	elapsed_time = realtime - oldtime;
	frames = host_framecount - oldframecount;

	if (scr_viewsize.value >= 130)
		return;

	if (elapsed_time < 0 || frames < 0)
	{
		oldtime = realtime;
		oldframecount = host_framecount;
		return;
	}
	// update value every 3/4 second
	if (elapsed_time > 0.75)
	{
		lastfps = frames / elapsed_time;
		oldtime = realtime;
		oldframecount = host_framecount;
	}

	if (scr_showfps.value)
	{
		char	st[16];
		char	st2[16]; // woods #f_config
		int	x, y;
		sprintf (st, "%4.0f fps", lastfps);
		sprintf (st2, "%4.0f", lastfps); // woods #f_config
		cl.fps = atoi(st2); // woods #f_config
		x = 312 - (strlen(st)<<3); // woods added padding
		if (clampedSbar == 3 && scr_viewsize.value <= 110) // woods #qehud
		{
			GL_SetCanvas(CANVAS_BOTTOMRIGHTQESMALL);
			x = 301;
			y = 140;

			if (!scr_sbarshowqeammo.value)
				y += 36;

			if ((cl.items & IT_KEY1) || (cl.items & IT_KEY2) || (cl.items & IT_SIGIL1) || (cl.items & IT_SIGIL2) || (cl.items & IT_SIGIL3) || (cl.items & IT_SIGIL4))
			{
				if (scr_sbarshowqeammo.value)
					y -= 22;
				if (scr_viewsize.value >= 110 && scr_sbarshowqeammo.value)
					y += 22;
			}
		}
		else
		{
			GL_SetCanvas(CANVAS_BOTTOMRIGHT);
			x = 312;
			y = 186;
		}
		if (scr_clock.value) y -= 12; //make room for clock // woods added padding
			Draw_String (x - (strlen(st) << 3), y, st);

		scr_tileclear_updates = 0;
	}
}

// woods (iw) #democontrols

/*
==============
SCR_DrawDemoControls
==============
*/
void SCR_DrawDemoControls(void)
{
	static const int	TIMEBAR_CHARS = 38;
	static float		prevspeed = 1.0f;
	static float		prevbasespeed = 1.0f;
	static float		showtime = 1.0f;
	int					i, len, x, y, min, sec, canvasleft, canvasright, canvasbottom, canvastop, match_time;
	float				frac;
	const char* str;
	char				name[31]; // size chosen to avoid overlap with side text
	
	static float smoothedFrameTime = 0.002f;  // Initial frame time for 500 FPS
	const float smoothingFactor = 0.1f;

	smoothedFrameTime = (host_frametime * smoothingFactor) + (smoothedFrameTime * (1.0f - smoothingFactor));

	canvasleft = 0;
	canvasright = 320;
	canvastop = 0;
	canvasbottom = 240;

	if (!cls.demoplayback || scr_demobar_timeout.value < 0.f)
	{
		showtime = 0.f;
		return;
	}

	// Determine for how long the demo playback info should be displayed
	if (cls.demospeed != prevspeed || cls.basedemospeed != prevbasespeed ||			// speed/base speed changed
		fabs(cls.demospeed) > cls.basedemospeed ||									// fast forward/rewind
		!scr_demobar_timeout.value)													// controls always shown
	{
		prevspeed = cls.demospeed;
		prevbasespeed = cls.basedemospeed;
		showtime = scr_demobar_timeout.value > 0.f ? scr_demobar_timeout.value : 1.f;
	}
	else
	{
		showtime -= smoothedFrameTime; // woods
		if (showtime < 0.f)
		{
			showtime = 0.f;
			return;
		}
	}

	// Approximate the fraction of the demo that's already been played back
	// based on the current file offset and total demo size
	// Note: we need to take into account the starting offset for pak files
	frac = (ftell(cls.demofile) - cls.demofilestart) / (double)cls.demofilesize;
	frac = CLAMP(0.f, frac, 1.f);

	if (cl.intermission)
	{
		GL_SetCanvas(CANVAS_MENU);
		y = LERP(canvasbottom, canvastop, 0.125f) + 8;
	}
	else
	{
		GL_SetCanvas(CANVAS_SBAR2);
		y = canvasbottom - 68;
	}
	x = (canvasleft + canvasright) / 2 - TIMEBAR_CHARS / 2 * 8;

	// Draw status box background
	//GL_SetCanvasColor(1.f, 1.f, 1.f, scr_sbaralpha.value);
	M_DrawTextBox(x - 8, y - 8, TIMEBAR_CHARS, 1);
	//GL_SetCanvasColor(1.f, 1.f, 1.f, 1.f);

	// Print playback status on the left (paused/playing/fast-forward/rewind)
	// Note: character #13 works well as a forward symbol, but Alkaline 1.2 changes it to a disk.
	// If we have a custom conchars texture we switch to a safer alternative, the '>' character.
	if (!cls.demospeed)
		str = "II";
	else if (fabs(cls.demospeed) > 1.f)
		str = ">>";
	else
		str = custom_conchars ? ">" : "\xD";
	if (cls.demospeed >= 0.f)
		M_Print(x, y, str);
	else
	{
		str = "<<";
		M_Print(x, y, str);
	}

	// Print base playback speed on the right
	if (!cls.basedemospeed)
		str = "";
	else if (fabs(cls.basedemospeed) >= 1.f)
		str = va("%gx", fabs(cls.basedemospeed));
	else
		str = va("1/%gx", 1.f / fabs(cls.basedemospeed));
	M_Print(x + (TIMEBAR_CHARS - strlen(str)) * 8, y, str);

	// Print demo name in the center
	COM_StripExtension(COM_SkipPath(cls.demofilename), name, sizeof(name));
	x = (canvasleft + canvasright) / 2;
	M_Print(x - strlen(name) * 8 / 2, y, name);

	// Draw seek bar rail
	x = (canvasleft + canvasright) / 2 - TIMEBAR_CHARS / 2 * 8;
	y -= 8;
	Draw_Character(x - 8, y, 128);
	for (i = 0; i < TIMEBAR_CHARS; i++)
		Draw_Character(x + i * 8, y, 129);
	Draw_Character(x + i * 8, y, 130);

	// Define a margin for the cursor. Assuming the cursor width is 8 pixels, and we add a bit of padding
	int cursorMargin = 12; // Adjust this value as needed

	// Adjust the calculation of 'x' for the cursor position
	// The original line was: x += (TIMEBAR_CHARS - 1) * 8 * frac;
	// We subtract the margin from both ends (2 * cursorMargin) and adjust the calculation
	x += ((TIMEBAR_CHARS - 1) * 8 - (2 * cursorMargin)) * frac;

	// Adjust 'x' to include the margin at the start of the seek bar
	x += cursorMargin;

	// Now draw the seek bar cursor with the adjusted 'x' position
	Draw_Character(x, y, 131);

	// Print current time above the cursor
	y -= 11;
	sec = (int)cl.time;
	min = sec / 60;
	sec %= 60;

	if (cl.teamgame) // pq match time
	{
		if (cl.match_pause_time)
			match_time = ceil(60.0 * cl.minutes + cl.seconds - (cl.match_pause_time - cl.last_match_time));
		else
			match_time = ceil(60.0 * cl.minutes + cl.seconds - (cl.time - cl.last_match_time));
		min = match_time / 60;
		sec = match_time - 60 * min;

		if (min < 0) 
			min = 0;
		if (sec < 0) 
			sec = 0;
	}

	str = va("%i:%02i", min, sec);
	x -= (strchr(str, ':') - str) * 8; // align ':' with cursor
	len = strlen(str);
	// M_DrawTextBox effectively rounds width up to a multiple of 2,
	// so if our length is odd we pad by half a character on each side
	//GL_SetCanvasColor(1.f, 1.f, 1.f, scr_sbaralpha.value);
	M_DrawTextBox(x - 8 - (len & 1) * 8 / 2, y - 8, len + (len & 1), 1);
//	GL_SetCanvasColor(1.f, 1.f, 1.f, 1.f);
	Draw_String(x, y, str);
}


/*
==============
SCR_DrawClock -- johnfitz
==============
*/
void SCR_DrawClock (void)
{
	char	str[30];
	int x,y;

	int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);

	if (scr_viewsize.value >= 130)
		return;

	time_t systime = time(0);
	struct tm* loct = localtime(&systime);

	if (loct == NULL)
		strcpy(str, "time error");
	else {
		switch ((int)scr_clock.value)
		{
		case 1:
		{
			int minutes = (int)cl.time / 60;
			int seconds = (int)cl.time % 60;
			sprintf(str, "%02i:%02i", minutes, seconds);
			break;
		}
		case 2:
				strftime(str, sizeof(str), "%I:%M %p", loct);
			break;
		case 3:
				strftime(str, sizeof(str), "%X", loct);
			break;
		case 4:
			strftime(str, sizeof(str), "%m/%d/%Y", loct);
			break;
		case 5:
			strftime(str, sizeof(str), "%m/%d/%Y %I:%M %p", loct);
			break;
		case 6:
			strftime(str, sizeof(str), "%m/%d/%Y %X", loct);
			break;
		case 7:
			if (sb_showscores)
				strftime(str, sizeof(str), "%m/%d/%y", loct);
			else
				strftime(str, sizeof(str), "%I:%M %p", loct);
			break;
		case 8:
			if (sb_showscores)
				strftime(str, sizeof(str), "%m/%d/%y", loct);
			else
				strftime(str, sizeof(str), "%X", loct);
			break;
		default:
			return;
		}
	}

	//draw it

	if (clampedSbar == 3 && scr_viewsize.value <= 110) // woods #qehud
	{
		GL_SetCanvas(CANVAS_BOTTOMRIGHTQESMALL);
		x = 301;
		y = 140;

		if (!scr_sbarshowqeammo.value)
			y += 36;

		if ((cl.items & IT_KEY1) || (cl.items & IT_KEY2) || (cl.items & IT_SIGIL1) || (cl.items & IT_SIGIL2) || (cl.items & IT_SIGIL3) || (cl.items & IT_SIGIL4))
		{
			if (scr_sbarshowqeammo.value)
				y -= 22;
			if (scr_viewsize.value >= 110 && scr_sbarshowqeammo.value)
				y += 22;
		}

	}
	else
	{ 
		GL_SetCanvas(CANVAS_BOTTOMRIGHT);
		x = 312;
		y = 186;
	}

	Draw_String(x - (strlen(str) << 3), y, str); // woods added padding
	scr_tileclear_updates = 0;
}

/*
==================
SCR_Show_Ping -- added by woods #scrping
==================
*/
void SCR_ShowPing(void)
{
	int	i, k, l;
	int	x, y;
	char	num[12];
	scoreboard_t* s;

	int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);

	if (scr_viewsize.value >= 130)
		return;

	ct = (SDL_GetTicks() - maptime) / 1000; // woods connected map time #maptime

	if (cl.gametype == GAME_DEATHMATCH && cls.state == ca_connected) {

		if (scr_ping.value) {

			GL_SetCanvas (CANVAS_BOTTOMLEFT2); //johnfitz woods 9/2/2021

			Sbar_SortFrags ();

			// draw the text
			l = scoreboardlines;

			x = 46; //johnfitz -- simplified becuase some positioning is handled elsewhere
			y = 20;

			if (clampedSbar == 3 && scr_viewsize.value <= 110) // #qehud
			{
				GL_SetCanvas(CANVAS_BOTTOMLEFTQESMALL);
				if (cl.stats[STAT_ARMOR] < 1)
					y = 140;
				else
					y = 114;
				x = 61;				
			}
			else
			{
				x = 46;
				y = 86;
			}

			for (i = 0; i < l; i++)
			{
				k = fragsort[i];
				s = &cl.scores[k];
				if (!s->name[0])
					continue;

				if (fragsort[i] == cl.realviewentity - 1) {

					sprintf (num, "%-4i", s->ping);

					if (ct > 5 && !scr_con_current) // dont update when console down or report ping 0
						M_PrintWhite (x - 8 * 5, y, num); //johnfitz -- was Draw_String, changed for stretched overlays 
				}
			}

			if (key_dest != key_console && (cls.signon >= SIGNONS)) // dont update when console down or not fully connected

				if (!cls.message.cursize && cl.expectingpingtimes < realtime)
				{
					cl.expectingpingtimes = realtime + 5;   // update frequency
					MSG_WriteByte(&cls.message, clc_stringcmd);
					MSG_WriteString(&cls.message, "ping");
				}
		}
	}

}

/*
==================
SCR_ShowPL -- added by woods #scrpl
==================
*/
void SCR_ShowPL(void)
{
	static int lastPL = 0;
	static int lastPLTime = 0;
	char			num[12];

	int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);

	if (scr_viewsize.value >= 130)
		return;

	ct = (SDL_GetTicks() - maptime) / 1000; // woods connected map time #maptime

	if (cl.gametype == GAME_DEATHMATCH && cls.state == ca_connected)
	{
		int currentPL = cl.packetloss; // directly use the integer value

		// If there is a new packet loss value, store it and reset scrpacketloss
		if (currentPL > 0) {
			lastPL += currentPL;
			lastPLTime = SDL_GetTicks(); // Update the time when the new value is received
			cl.packetloss = 0; // Reset scrpacketloss to 0
		}

		// Determine if the stored value should be displayed
		int elapsedTime = SDL_GetTicks() - lastPLTime;
		if (elapsedTime < 3000) { // Show for 1 second

			int	x, y;

			if (clampedSbar == 3) // #qehud
			{
				GL_SetCanvas(CANVAS_BOTTOMLEFTQESMALL);
				x = 20;
				if (cl.stats[STAT_ARMOR] < 1)
					y = 129;
				else
					y = 103;
				if (!scr_ping.value)
					y += 10;
			}
			else
			{
				GL_SetCanvas(CANVAS_BOTTOMLEFT2);
				x = 6;
				y = 77;
				if (!scr_ping.value)
					y += 10;
			}

			if (key_dest != key_console && ((ct != (int)cl.time) && (ct > 6)))
			{
				sprintf(num, "%-4i", lastPL);
				M_Print(x, y, num);

			}
		}
		else
		{
			lastPL = 0;
		}
	}
}

/*====================
SCR_DrawMatchClock    woods (Adapted from Sbar_DrawFrags from r00k) draw match clock upper right corner #matchhud
====================
*/
void SCR_DrawMatchClock(void)
{
	char			num[22] = "empty";
	int				teamscores, minutes, seconds;
	int				match_time, tl;

	match_time = ceil(60.0 * cl.minutes + cl.seconds - (cl.time - cl.last_match_time));
	minutes = match_time / 60;
	seconds = match_time - 60 * minutes;
	teamscores = cl.teamgame;

	if (scr_viewsize.value >= 130)
		return;

	GL_SetCanvas(CANVAS_TOPRIGHT2);

	if ((teamscores) && !(cl.minutes != 255)) // display 0.00 for pre match mode in DM
	{
		sprintf(num, "%3d:%02d", 0, 0);
		Draw_String(((314 - (strlen(num) << 3)) + 1), 195 - 8, num);
	}

	if ((cl.minutes != 255)) // hack for crmod 6.6
	{
		if (cl.playmode == 2 || (cl.modetype != 3 && cl.playmode == 2) || netquakeio || (!teamscores && cl.modtype == 3)) // display count up to timelimit in normal/ffa mode
		{
			minutes = cl.time / 60;
			seconds = cl.time - 60 * minutes;
			minutes = minutes & 511;

			if (crxintermission) // woods #crxintermission
				sprintf(num, "%3d:%02d", 0, 0);
			else
				sprintf(num, "%3d:%02d", minutes, seconds);
		}

		if (cl.teamcolor[0] && cl.modetype != 3) // display timelimit if we can get it if there is a team
		{
			if (cl.modtype == 1) // nq crx server check, if so parse serverinfo for timelimit
			{
				char buf[10];
				const char* simt;
				simt = Info_GetKey(cl.serverinfo, "matchtime", buf, sizeof(buf));
				tl = atoi(simt);
			}
			else if (cl.modtype == 4) // qecrx server check, if so parse userinfo for timelimit
			{
				char buf[10];
				const char* uimt;
				uimt = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "matchtime", buf, sizeof(buf)); // userinfo (qecrx)
				tl = atoi(uimt);
			}
			else
				tl = 0; // if no timelimit available, set clock to 0:00

			sprintf(num, "%3d:%02d", tl, 0);
		}

		if (cl.minutes || cl.seconds) // counter is rolling
		{
			if (cl.match_pause_time)
				match_time = ceil(60.0 * cl.minutes + cl.seconds - (cl.match_pause_time - cl.last_match_time));
			else
				match_time = ceil(60.0 * cl.minutes + cl.seconds - (cl.time - cl.last_match_time));
			minutes = q_max(0, floor(match_time / 60));
			seconds = q_max(0, match_time - 60 * floor(match_time / 60));
			sprintf(num, "%3d:%02d", minutes, seconds);
		}

		if (cl.seconds >= 128) // DM CRMOD 6.6 countdown, second count inaccurate in countdown, fix it
			sprintf(num, " 0:%02d", cl.seconds - 128);

		// now lets draw the clocks

		if (!strcmp(num, "empty"))
			return;

		if (qeintermission && !cl.teamcolor[0])
			return;

		if (scr_match_hud.value)
		{
			if ((((minutes <= 0) && (seconds < 15) && (seconds > 0)) && teamscores) || cl.seconds >= 128) // color last 15 seconds to draw attention cl.seconds >= 128 is for CRMOD
				M_Print(((314 - (strlen(num) << 3)) + 1), 195 - 8, num); // M_Print is colored text
			else
				Draw_String(((314 - (strlen(num) << 3)) + 1), 195 - 8, num);
		}

		if (crxintermission) // woods #crxintermission
			return;

		if (key_dest == key_menu) // woods #menuclear
			return;

		if (countdown && draw) // woods #clearcrxcountdown
			return;

		if (qeintermission && draw) // woods #qeintermission
			return;

		if (scr_matchclock.value) // woods #varmatchclock draw variable clock where players want based on their x, y cvar
		{
			GL_SetCanvas(CANVAS_MATCHCLOCK);

			if (sb_showscores == false && (cl.gametype == GAME_DEATHMATCH && cls.state == ca_connected)) // woods don't overlap crosshair with scoreboard
			{
				int scr_matchclock_int = (int)scr_matchclock.value; // get the integer part of scr_matchclock.value

				if (scr_matchclock_int == 1 || scr_matchclock_int == 2)
				{
					int color = 0; // Default to brown

					if (scr_matchclock.value == 2)
						color = 1; // red

					// calculate the border thickness based on the tenths place of scr_matchclock.value
					float border = fmod(scr_matchclock.value, 1.0f); // extract the decimal part for the border thickness
					if (border == 0.0f) border = 0.4f; // default border if no tenths place is provided

					char* p = num; // manually parse the `num` string to extract minutes and seconds

					while (*p == ' ') p++;  // skip leading spaces
					minutes = (*p++ - '0');  // get the first digit of minutes
					if (*p >= '0' && *p <= '9')
						minutes = minutes * 10 + (*p++ - '0');  // get the second digit of minutes if present
					if (*p >= '0' && *p <= '9')
						minutes = minutes * 10 + (*p++ - '0');  // get the third digit of minutes if present

					if (*p == ':') p++; // skip the colon

					seconds = (*p++ - '0') * 10;  // get the tens place of seconds
					seconds += (*p++ - '0');  // get the ones place of seconds

					if (scr_matchclock.value == 1 && (minutes == 0 && seconds < 15 && seconds > 0) && teamscores) // check if we are in the final 15 seconds
						color = 1;

					int x_offset = 0;

					if (minutes >= 100)
					{
						int hundreds = minutes / 100;
						Draw_Pic_RGBA_Outline(scr_matchclock_x.value, scr_matchclock_y.value, sb_nums[color][hundreds], CL_PLColours_Parse("0xffffff"), 1.0f, border); // draw hundreds place of minutes
						x_offset = 24; // Move the x position for the tens place
					}
					if (minutes >= 10) {
						int tens = (minutes / 10) % 10;
						Draw_Pic_RGBA_Outline(scr_matchclock_x.value + x_offset, scr_matchclock_y.value, sb_nums[color][tens], CL_PLColours_Parse("0xffffff"), 1.0f, border); // draw tens place of minutes
						x_offset += 24; // Move the x position for the ones place
					}

					int ones = minutes % 10;
					Draw_Pic_RGBA_Outline(scr_matchclock_x.value + x_offset, scr_matchclock_y.value, sb_nums[color][ones], CL_PLColours_Parse("0xffffff"), 1.0f, border); // draw ones place of minutes

					if (scr_matchclock.value == 2 || ((minutes == 0 && seconds < 15 && seconds > 0) && teamscores))
						Draw_Pic_RGBA_Outline(scr_matchclock_x.value + x_offset + 24, scr_matchclock_y.value, sb_colon, CL_PLColours_Parse("0xff0000"), 1.0f, border); // red, there is no red colon in wad
					else
						Draw_Pic_RGBA_Outline(scr_matchclock_x.value + x_offset + 24, scr_matchclock_y.value, sb_colon, CL_PLColours_Parse("0xffffff"), 1.0f, border);

					int tens = seconds / 10;
					ones = seconds % 10;

					Draw_Pic_RGBA_Outline(scr_matchclock_x.value + x_offset + 38, scr_matchclock_y.value, sb_nums[color][tens], CL_PLColours_Parse("0xffffff"), 1.0f, border); // draw tens place of seconds
					Draw_Pic_RGBA_Outline(scr_matchclock_x.value + x_offset + 62, scr_matchclock_y.value, sb_nums[color][ones], CL_PLColours_Parse("0xffffff"), 1.0f, border); // draw ones place of seconds
				}
				else if (scr_matchclock.value == 3)
				{
					if ((((minutes <= 0) && (seconds < 15) && (seconds > 0)) && teamscores) || cl.seconds >= 128) // color last 15 seconds or CRMOD condition
						M_Print(scr_matchclock_x.value, scr_matchclock_y.value, num);
					else
						Draw_String(scr_matchclock_x.value, scr_matchclock_y.value, num);
				}
			}
		}
	}
}

int divide_round_up(int a, int b) // woods #capturediff
{
	return (a + b - 1) / b;
}

/*====================
SCR_DrawMatchScores   -- woods  (Adapted from Sbar_DrawFrags from r00k) -- draw match scores upper right corner #matchhud
====================++
*/
void SCR_DrawMatchScores(void)
{
	int				i, k, l;
	int				top, bottom;
	int				x, y, f;
	char			num[30];
	int				teamscores, colors;// JPG - added these
	int				ts1, ts2, tc1, tc2, diff, l2; // woods #hud_diff
	char			tcolor[12]; // woods #hud_diff
	scoreboard_t* s; // woods #hud_diff
	int				totalteamplayers, redteamplayers, blueteamplayers, capturepoints, capdiff; // woods #capturediff

	if (scr_viewsize.value >= 130)
		return;

	// JPG - check to see if we should sort teamscores instead
	teamscores = /*pq_teamscores.value && */cl.teamgame;

	if (teamscores)    // display frags if it's a teamgame match
		Sbar_SortTeamFrags();
	else
		return;

	// draw the text
	l = scoreboardlines <= 4 ? scoreboardlines : 4;

	x = 0;
	y = 0; // woods to position vertical
	redteamplayers = 0;
	blueteamplayers = 0;
	totalteamplayers = 0;
	capturepoints = 0;
	capdiff = 0;

	if (cl.gametype == GAME_DEATHMATCH)
	{
		GL_SetCanvas(CANVAS_TOPRIGHT3);

		char buf[10];

		const char* uiplaymode;
		uiplaymode = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "mode", buf, sizeof(buf)); // userinfo (qecrx)

		if (!q_strcasecmp(uiplaymode, "ffa"))
			return;

		if (cl.modetype == 3) // no teamscores for crx ra
			return;
		
		if (scr_match_hud.value)   // woods for console var off and on
		{
			if (cl.teamcolor[0])
				Draw_Fill(11, 1, 32, 18, 0, 0.3);  // rectangle for missing team

			for (i = 0; i < l; i++)
			{
				k = fragsort[i];

				// JPG - check for teamscores
				if (teamscores)
				{
					colors = cl.teamscores[k].colors;
					f = cl.teamscores[k].frags;
					cl.teamscore[i] = f;
					cl.teamcolor[i] = colors;
				}
				else
					return;

				// draw background
				if (teamscores)
				{
					top = (colors & 15) << 4;
					bottom = (colors & 15) << 4;
				}
				else
				{
					top = colors & 0xf0;
					bottom = (colors & 15) << 4;
				}
				top = Sbar_ColorForMap(top);
				bottom = Sbar_ColorForMap(bottom);

				GL_SetCanvas(CANVAS_TOPRIGHT3);

				Draw_Fill((((x + 1) * 8) + 3), y + 1, 32, 6, top, .6);
				Draw_Fill((((x + 1) * 8) + 3), y + 7, 32, 3.5, bottom, .6);

				// draw number
				sprintf(num, "%3i", f);

				Sbar_DrawCharacter(((x + 1) * 8) + 7, y - 23, num[0]);
				Sbar_DrawCharacter(((x + 2) * 8) + 7, y - 23, num[1]);
				Sbar_DrawCharacter(((x + 3) * 8) + 7, y - 23, num[2]);

				x += 0;
				y += 9;  // woods to position vertical
			}

			// woods #hud_diff display point differential

			if (!cl.teamcolor[2]) // only for two colors
			{
				for (i = 0; i < l; i++)
				{
					k = fragsort[i];

					ts1 = cl.teamscore[0]; // high score
					ts2 = cl.teamscore[1]; // low score
					diff = abs(ts1 - ts2); // +/= differential

					tc1 = cl.teamcolor[0]; // top score [color]
					tc2 = cl.teamcolor[1]; // bottom score [color]

					// lets get YOUR team color from scoreboard

					Sbar_SortFrags();
					l2 = scoreboardlines;

					for (i = 0; i < l2; i++)
					{
						k = fragsort[i];
						s = &cl.scores[k];
						if (!s->name[0])
							continue;

						if (fragsort[i] == cl.viewentity - 1) {
							sprintf(tcolor, "%u", s->pants.basic);
						}

						// woods, lets see how many players are on each team #capturediff

						if (s->pants.basic == 4) // count number of red players
							redteamplayers += 1;

						if (s->pants.basic == 13) // count number of blue players
							blueteamplayers += 1;

						if (redteamplayers == blueteamplayers) // equal teams? 3 vs 3 not 2 vs 1
							totalteamplayers = blueteamplayers;
						else
							totalteamplayers = 0;
					}

					// woods, lets do some ctf math! #capturediff

					capturepoints = (totalteamplayers * 10) + 5; // capture 10 and +5 for cap
					if (diff != 0 && capturepoints != 0)
						capdiff = divide_round_up(diff, capturepoints); // how many ctf captures up or down

					GL_SetCanvas(CANVAS_TOPRIGHT4); // lets do some printing

					if ((ts1 == ts2) || (l < 2)) // don't show ties, l = # of teams
						continue;

					else if ((atoi(tcolor) == tc1) || atoi(tcolor) == (tc1/17))// top score [color] is the same as your color
					{
						if (totalteamplayers && cl.modetype == 1) // equal teams and CTF
							snprintf(num, sizeof(num), "+%-i (%i)", diff, capdiff);
						else
							snprintf(num, sizeof(num), "+%-i", diff);

						M_Print(120 - (strlen(num) << 3), y, num);
					}

					else if ((atoi(tcolor) == tc2) || atoi(tcolor) == (tc2 / 17)) // bottom score [color] is the same as your color
					{
						if (totalteamplayers && cl.modetype == 1) // equal teams and CTF
							snprintf(num, sizeof(num), "-%-i (%i)", diff, capdiff);
						else
							snprintf(num, sizeof(num), "-%-i", diff);
						M_Print(120 - (strlen(num) << 3), y + 20, num);
					}				
				}
			}
		}
	}
	else
		return;
}

/*
=======================
SCR_ShowObsFrags -- added by woods #observerhud
=======================
*/

void SCR_ShowObsFrags(void)
{
	int	i, k, x, y, f;
	char	num[12];
	scoreboard_t* s;
	char	shortname[16]; // woods for dynamic scoreboard during match, don't show ready
	char buf[15];
	const char* obs;
	int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);

	if (cl.intermission)
		return;

	if (qeintermission)
		return;

	if (scr_viewsize.value >= 120)
		return;

	if ((cl.gametype == GAME_DEATHMATCH) && (cls.state == ca_connected))
	{
		obs = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "observer", buf, sizeof(buf));

		if ((!strcmp(cl.observer, "y") && (cl.modtype >= 2)) || scr_showscores.value || !strcmp(obs, "eyecam") || !strcmp(obs, "chase") || !strcmp(obs, "fly") || !strcmp(obs, "walk"))
		{
			Sbar_SortFrags_Obs ();

			if (clampedSbar == 3)
			{
				GL_SetCanvas(CANVAS_BOTTOMLEFTQESCORES);
				x = 24;
				y = 170;
			}
			else
			{ 
				GL_SetCanvas(CANVAS_SCORES);
				x = 10;
				y = 160;
			}
			for (i = 0; i < scoreboardlines; i++, y += -8) //johnfitz -- change y init, test, inc woods (reverse drawing order from bottom to top)
			{
				k = fragsort[i];
				s = &cl.scores[k];
				if (!s->name[0])
					continue;

				// colors
				Draw_FillPlayer(x, y + 1, 40, 4, s->shirt, 1);
				Draw_FillPlayer(x, y + 5, 40, 3, s->pants, 1);

				// number
				f = s->frags;
				sprintf(num, "%3i", f);
				Draw_Character(x + 8, y, num[0]);
				Draw_Character(x + 16, y, num[1]);
				Draw_Character(x + 24, y, num[2]);

				// name
				sprintf(shortname, "%.15s", s->name); // woods only show name, not 'ready' or 'afk' -- 15 characters
				M_PrintWhite(x + 50, y, shortname);
			}
		}
	}
}

/*
=======================
SCR_ShowFlagStatus -- added by woods #flagstatus
Grab the impulse 70-80 CRCTF flag and print to top right screen. Abadondoned flags have reduced transparency.
=======================
*/
void SCR_ShowFlagStatus(void)
{
	float z;
	int x, y, xx, yy;

	// woods lets get some info from server infokeys

	const char* redflag;
	char buf[10];
	redflag = Info_GetKey(cl.serverinfo, "red flag", buf, sizeof(buf));

	const char* blueflag;
	char buf2[10];
	blueflag = Info_GetKey(cl.serverinfo, "blue flag", buf2, sizeof(buf2));

	if ((blueflag[0] != '\0' && redflag[0] != '\0') && !cls.demoplayback) // is there a key on the server (newer version of crx)
	{
		strncpy(cl.flagstatus, "n", sizeof(cl.flagstatus)); // null flag, reset all flag ... flags :)

		// RED

		if (!strcmp(redflag, "carried"))
			strncpy(cl.flagstatus, "r", sizeof(cl.flagstatus)); // red taken

		if (!strcmp(redflag, "dropped"))
			strncpy(cl.flagstatus, "x", sizeof(cl.flagstatus)); // red abandoned

		// BLUE

		if (!strcmp(blueflag, "carried"))
			strncpy(cl.flagstatus, "b", sizeof(cl.flagstatus)); // blue abandoned

		if (!strcmp(blueflag, "dropped"))
			strncpy(cl.flagstatus, "y", sizeof(cl.flagstatus)); // blue abandoned

		// RED & BLUE

		if (!strcmp(blueflag, "carried") && !strcmp(redflag, "carried")) // blue & red taken
			strncpy(cl.flagstatus, "p", sizeof(cl.flagstatus));

		if (!strcmp(blueflag, "dropped") && !strcmp(redflag, "dropped")) // blue & red abandoned
			strncpy(cl.flagstatus, "z", sizeof(cl.flagstatus));

		if (!strcmp(blueflag, "dropped") && !strcmp(redflag, "carried")) // blue abandoned, red taken
			strncpy(cl.flagstatus, "j", sizeof(cl.flagstatus));

		if (!strcmp(blueflag, "carried") && !strcmp(redflag, "dropped")) // red abandoned, blue taken
			strncpy(cl.flagstatus, "k", sizeof(cl.flagstatus));
	}

	GL_SetCanvas(CANVAS_TOPRIGHT3);

	z = 0.20; // abandoned not at base flag (alpha)
	x = 0; xx = 0; 	y = 0; 	yy = 0; // initiate

	if (!cl.teamgame) // change position in ffa mode below the clock
	{  // xx and yy needed because drawalpha uses diff positioning
		x = 26;
		xx = 12;
		y = -1;
		yy = -25;
	}

	else // xx and yy needed because drawalpha uses diff positioning
	{
		x = 26;
		xx = 12;
		y = 19;
		yy = -5;
	}

	if (scr_match_hud.value == 1)

		if (cl.gametype == GAME_DEATHMATCH && cls.state == ca_connected)
		{
			if (!strcmp(cl.flagstatus, "r")) // red taken
				Draw_Pic (x, y, Draw_PicFromWad ("sb_key2"));

			if (!strcmp(cl.flagstatus, "x")) // red abandoned
			{
				glDisable (GL_ALPHA_TEST);
				glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

				Sbar_DrawPicAlpha (x, yy, Draw_PicFromWad2 ("sb_key2", TEXPREF_PAD | TEXPREF_NOPICMIP), z); // doesnt work
			}

			if (!strcmp(cl.flagstatus, "b")) // blue taken
				Draw_Pic (x, y, Draw_PicFromWad ("sb_key1"));

			if (!strcmp(cl.flagstatus, "y")) // blue abandoned
			{
				glDisable (GL_ALPHA_TEST);
				glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

				Sbar_DrawPicAlpha (x, yy, Draw_PicFromWad2 ("sb_key1", TEXPREF_PAD | TEXPREF_NOPICMIP), z);
			}

			if (!strcmp(cl.flagstatus, "p")) //  blue & red taken
			{
				Draw_Pic (x, y, Draw_PicFromWad ("sb_key1")); // blue
				Draw_Pic (xx, y, Draw_PicFromWad ("sb_key2")); // red
			}

			if (!strcmp(cl.flagstatus, "z")) // blue & red abandoned
			{
				glDisable (GL_ALPHA_TEST);
				glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

				Sbar_DrawPicAlpha (xx, yy, Draw_PicFromWad2 ("sb_key2", TEXPREF_PAD | TEXPREF_NOPICMIP), z);
				Sbar_DrawPicAlpha (x, yy, Draw_PicFromWad2 ("sb_key1", TEXPREF_PAD | TEXPREF_NOPICMIP), z);
			}

			if (!strcmp (cl.flagstatus, "j"))  // blue abandoned, red taken
			{
				Draw_Pic (xx, y, Draw_PicFromWad ("sb_key2")); // red

				glDisable (GL_ALPHA_TEST);
				glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

				Sbar_DrawPicAlpha (x, yy, Draw_PicFromWad2 ("sb_key1", TEXPREF_PAD | TEXPREF_NOPICMIP), z);
			}

			if (!strcmp(cl.flagstatus, "k")) // red abandoned, blue taken
			{
				glDisable (GL_ALPHA_TEST);
				glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

				Sbar_DrawPicAlpha (xx, yy, Draw_PicFromWad2("sb_key2", TEXPREF_PAD | TEXPREF_NOPICMIP), z); // red

				Draw_Pic (x, y, Draw_PicFromWad ("sb_key1")); // blue
			}
		}
}

/*
==============
SCR_Speedometer -- woods #speedometer (https://github.com/matthewearl/quakespasm f4411f0 from joequake)
==============
*/
void SCR_Speedometer(void)
{
	float speed;
	vec3_t vel;
	int bad_jump = 0;
	static float maxspeed = 0, display_speed = -1;
	static double lastrealtime = 0;

	int	x, bg_color;
	float speedunits;
	char st[8];
	float alpha = 0.5;
	int y = scr_showspeed_y.value;

	if (cl.intermission || qeintermission || scr_viewsize.value >= 120)
		return;

	GL_SetCanvas(CANVAS_SBAR2);

	if (lastrealtime > realtime)
	{
		lastrealtime = 0;
		display_speed = -1;
		maxspeed = 0;
	}

	VectorCopy(cl.velocity, vel);
	vel[2] = 0;
	speed = VectorLength(vel);

	if (speed > maxspeed)
		maxspeed = speed;

	if (sv.active && speed_info.speed >= 0)
	{
		display_speed = speed_info.speed;
		bad_jump = speed_info.jump_fmove == 0 || speed_info.jump_smove == 0;
	}

	// draw

	if (display_speed >= 0)
	{
		sprintf(st, "%3d", (int)display_speed);

		x = 80;

		bg_color = (bad_jump && scr_showspeed.value > 2) ? 251 : 10;
		Draw_Fill(x, y - 1, 160, 1, bg_color, alpha);
		Draw_Fill(x, y + 9, 160, 1, bg_color, alpha);
		Draw_Fill(x, y, 160, 9, 52, 0.9);

		speedunits = display_speed;
		if (display_speed <= 500)
		{
			Draw_Fill(x, y, (int)(display_speed / 3.125), 9, 100, alpha);
		}
		else
		{
			while (speedunits > 500)
				speedunits -= 500;
			Draw_Fill(x, y, (int)(speedunits / 3.125), 9, 68, alpha);
		}
		Draw_String(x + 36 - (strlen(st) * 8), y, st);
	}

	if (realtime - lastrealtime >= 0.0)
	{
		lastrealtime = realtime;
		display_speed = maxspeed;
		maxspeed = 0;
	}
}

/*
==============
SCR_DrawSpeed -- woods #speed
==============
*/
void SCR_DrawSpeed (void)
{
	char			st[64];
	int				x, y;
	int				clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);

	if (scr_viewsize.value > 110 || scr_viewsize.value >= 130)
		return;

	if (clampedSbar == 3)
	{
		GL_SetCanvas(CANVAS_BOTTOMLEFTQE);
		y = 175;
		x = 134;
	}
	else
	{
		GL_SetCanvas(CANVAS_SBAR2);
		x = 0;
		y = 0;

		if (scr_viewsize.value <= 100)
			y = 208;
		else if (scr_viewsize.value == 110)
			y = 233;
		if (clampedSbar == 2)
			y = 233;
	}

	if (scr_showspeed.value == 1 && !cl.intermission) {
		vec3_t	vel = { cl.velocity[0], cl.velocity[1], 0 };
		float	speed = VectorLength(vel);

	sprintf(st, "%-3.0f", speed);
	if (scr_viewsize.value <= 110)
		{
			if (speed > 400 && !(speed > 600)) // red
				M_Print(x, y, st);
			else if (speed > 600)
					M_Print2(x, y, st); // yellow/gold
			else
					M_PrintWhite(x, y, st);  // white
		}
	}

	if (scr_showspeed.value > 1)
		SCR_Speedometer (); // woods #speedometer

}

/*
===============
SCR_DrawMovementKeys -- woods #movementkeys (soruced from: https://github.com/j0zzz/JoeQuake/commit/bc56fea)
===============
*/
void SCR_DrawMovementKeys(void)
{
	if (!scr_movekeys.value || cl.intermission || qeintermission || scr_viewsize.value > 110)
		return;

	extern kbutton_t in_moveleft, in_moveright, in_forward, in_back, in_jump, in_up;

	int x, y, size = 8;
	int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);

	switch (clampedSbar)
	{
	case 1:
		x = 10;
		y = (scr_showspeed.value == 1) ? 186 : 198;
		if (scr_viewsize.value == 110)
			y += 26;
		GL_SetCanvas(CANVAS_SBAR2);
		break;
	case 2:
		x = 10;
		y = (scr_showspeed.value == 1 || !strcmp(mute, "y")) ? 210 : 224;
		GL_SetCanvas(CANVAS_SBAR2);
		break;
	case 3: // #qehud
		x = (scr_showspeed.value == 1) ? 172 : 174;
		y = (scr_showspeed.value == 1) ? 148 : 166;
		GL_SetCanvas(CANVAS_BOTTOMLEFTQESMALL);
		break;
	default:
		return; // Invalid clampedSbar value
	}

	// Draw movement keys
	if (in_forward.state & 1)
		Draw_Character_Rotation(x, y - size, '^', 0);
	if (in_back.state & 1)
		Draw_Character_Rotation(x, y + size, '^', 180);
	if (in_moveleft.state & 1)
		Draw_Character_Rotation(x - size, y, '^', 270);
	if (in_moveright.state & 1)
		Draw_Character_Rotation(x + size, y, '^', 90);
	if (in_jump.state & 1)
		M_Print(x, y - 1, "j");
	else if (in_up.state & 1)
		M_Print(x, y -1, "s");
}

/*
==============
SCR_DrawMute -- woods #usermute
==============
*/
void SCR_Mute(void)
{
	int				x, y;

	if (cl.intermission)
		return;

	if (scr_viewsize.value > 110)
		return;

	int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);

	if (!strcmp(mute, "y"))
	{

		if (clampedSbar == 3) // #qehud
		{
			y = 176;
			x = 184;
			GL_SetCanvas(CANVAS_BOTTOMRIGHTQESMALL);
			

			if (cl.stats[STAT_AMMO] > 9) // two digits
				x -= 20;
			if (cl.stats[STAT_AMMO] > 99) // three digits
				x -= 32;

			if (cls.demoplayback)
				x -= 34;

			if (!scr_sbarshowqeammo.value)
				x = 184;

			M_PrintWhite(x, y, "mute");
		}
		else
		{ 
			GL_SetCanvas(CANVAS_SBAR2);

			x = 288;
			y = 0;

			if (scr_viewsize.value <= 100)
				y = 208;
			else if (scr_viewsize.value == 110)
				y = 233;
			else
				return;
			if (clampedSbar == 2)
			{ 
				y = 233;

				if (!scr_showspeed.value || !cls.demoplayback)
					x = 0;
				if (scr_showspeed.value == 1)
					x = 40;
			}

			M_PrintWhite(x, y, "mute");
		}
	}
}

/*
==============
SCR_Mute_Switch -- woods
==============
*/
void SCR_Mute_Switch(void)
{
	if ((!strcmp(mute, "y")) != true)
		strncpy(mute, "y", sizeof(mute));
	else
		strncpy(mute, "n", sizeof(mute));
}

/*
==============
SCR_Observing -- woods -- detect if client is observing and print for crx. eyecam pulls keys from other persons viewentity, chase doesnt
do not use for: fly or walk
==============
*/
void SCR_Observing(void)
{
	if ((cl.gametype == GAME_DEATHMATCH) && (cls.state == ca_connected))
	{
		char printtxt[25];
		char buf2[25];
		char buf3[25];
		const char* obs;
		const char* observing;
		int color, y;
		obs = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "observer", buf2, sizeof(buf2));
		observing = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "observing", buf3, sizeof(buf3));
		color = cl.scores[cl.viewentity - 1].pants.basic; // get color 0-13
		color = Sbar_ColorForMap((color & 15) << 4); // translate to proper drawfill color
		int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);

		y = 190;

		if (scr_viewsize.value > 110 || (clampedSbar == 3 && sb_showscores))
			return;
		
		if (scr_viewsize.value >= 110 || clampedSbar > 1)
			y += 24;

		if (clampedSbar == 3)
			y += 34;

		if (cl.intermission)
			return;

		if (qeintermission)
			return;

		if (!strcmp(observing, "off"))
			return;

		GL_SetCanvas(CANVAS_SBAR2);

		if (cl.modtype == 1 || cl.modtype == 4) // crx case
		{
			if (!strcmp(obs, "chase")) // chase
			{
				sprintf(printtxt, "%s", observing); // print who you are observering
				M_PrintWhite(166 - (strlen(observing)*4), y, printtxt);
			}
			else if (!strcmp(obs, "eyecam"))// eyecam
			{
				if (r_drawviewmodel.value)
					Draw_Fill(152 - strlen(observing)*4, y, (strlen(observing)*8) + 15, 9, 0, .8); // show their color
				sprintf(printtxt, "%s", observing); // // print self (name), viewentity hack (eyecam thinks your are them)
				M_PrintWhite(165-strlen(observing)*4, y, printtxt);
				Draw_Fill(154 - (strlen(observing)*4), y + 1, 7, 7, color, 1); // show their color
			}		
		}
	}
}

/*
==============
SCR_DrawDevStats
==============
*/
void SCR_DrawDevStats (void)
{
	char	str[40];
	int		y = 25-9; //9=number of lines to print
	int		x = 0; //margin

	if (!devstats.value)
		return;

	GL_SetCanvas (CANVAS_BOTTOMLEFT);

	Draw_Fill (x, y*8, 19*8, 9*8, 0, 0.5); //dark rectangle

	sprintf (str, "devstats |Curr Peak");
	Draw_String (x, (y++)*8-x, str);

	sprintf (str, "---------+---------");
	Draw_String (x, (y++)*8-x, str);

	sprintf (str, "Edicts   |%4i %4i", dev_stats.edicts, dev_peakstats.edicts);
	Draw_String (x, (y++)*8-x, str);

	sprintf (str, "Packet   |%4i %4i", dev_stats.packetsize, dev_peakstats.packetsize);
	Draw_String (x, (y++)*8-x, str);

	sprintf (str, "Visedicts|%4i %4i", dev_stats.visedicts, dev_peakstats.visedicts);
	Draw_String (x, (y++)*8-x, str);

	sprintf (str, "Efrags   |%4i %4i", dev_stats.efrags, dev_peakstats.efrags);
	Draw_String (x, (y++)*8-x, str);

	sprintf (str, "Dlights  |%4i %4i", dev_stats.dlights, dev_peakstats.dlights);
	Draw_String (x, (y++)*8-x, str);

	sprintf (str, "Beams    |%4i %4i", dev_stats.beams, dev_peakstats.beams);
	Draw_String (x, (y++)*8-x, str);

	sprintf (str, "Tempents |%4i %4i", dev_stats.tempents, dev_peakstats.tempents);
	Draw_String (x, (y++)*8-x, str);
}

/*
==============
SCR_DrawTurtle
==============
*/
void SCR_DrawTurtle (void)
{
	static int	count;

	if (!scr_showturtle.value)
		return;

	if (host_frametime < 0.1)
	{
		count = 0;
		return;
	}

	count++;
	if (count < 3)
		return;

	GL_SetCanvas (CANVAS_DEFAULT); //johnfitz

	Draw_Pic (scr_vrect.x, scr_vrect.y, scr_turtle);
}

/*
==============
SCR_DrawNet
==============
*/
void SCR_DrawNet (void)
{
	if (!scr_shownet.value)
		return;
	
	if (realtime - cl.last_received_message < 0.3)
		return;
	if (cls.demoplayback)
		return;

	GL_SetCanvas (CANVAS_DEFAULT2); // woods

	Draw_Pic (scr_vrect.x+64, scr_vrect.y, scr_net);
}

/*
==============
DrawPause
==============
*/
void SCR_DrawPause (void)
{
	qpic_t	*pic;

	if (!cl.paused)
		return;

	if (!scr_showpause.value)		// turn off for screenshots
		return;

	GL_SetCanvas (CANVAS_MENU); //johnfitz

	pic = Draw_CachePic ("gfx/pause.lmp");
	Draw_Pic ( (320 - pic->width)/2, (240 - 48 - pic->height)/2, pic); //johnfitz -- stretched menus

	scr_tileclear_updates = 0; //johnfitz
}

/*
==============
QSS-M Hints -- woods #qssmhints
==============
*/

char* hints[] = {
	"typing anything into the console searches for commands",
	"typing help into the console opens qss-m webpage in browser",
	"alt-enter swtiches between windowed and fullscreen mode",
	"pressing tab in console will auto-complete commands",
	"dragging a demo into windowed mode plays the demo",
	"arrow keys, scroll wheel, and pgup/pgdn adjust demo speed",
	"ctrl-u clears the console line",
	"cl_say allows you to tyle chat into the console",
	"connect last or reconnect will connect to last server",
	"connect + tab will autocomplete server history",
	"send a direct message by typing tell + first letters of name",
	"alt+shift+scrollwheel adjust game volume",
	"ctrl-m or mute will mute volume",
	"clear command will clear all console history",
	"type exec + tab to exec cfgs, or restore cfg backup",
	"uparrow and downarrow in the console for history",
	"add nickname to con_notifylist to flash client attentiion",
	"ctrl-enter in console will use messagemode2 (team)",
	"uparrow and downarrow in the console for history",
	"ctrl-home and ctrl-end jump to top/bottom of console",
	"type identify to identify the last person connected",
	"type lastid to see your last recorded ghost code",
	"type hints to print all hints to the console",
	"typing 'maps chamber' will search maps for chamber",
	"anything in end.cfg will be executed at match end",
	"anything in ctf.cfg will be executed if mode is ctf",
	"anything in dm.cfg will be executed if mode is dm",
	"type namemaker to make custom names with quake chars",
	"chat f_config, f_system, f_version, f_random for player info",
	"cl_smartspawn 1 can help train spawning with spacebar",
	"say_rand0 will randomly chat a line from say_rand0.txt",
	"typing open id1 or open screenshots etc will open folder",
	"w_switch values > 0 will disable all auto weapon switches",
	"use cl_enemycolor and cl_teamcolor to force colors"
};

char* random_hint;
int num_hints = sizeof(hints) / sizeof(hints[0]);

Uint32 HintTimer_Callback (Uint32 interval, void* param)
{
	int index = rand() % num_hints;
	random_hint = hints[index];
	return interval;
}

/*
==============
Print_Hints_f -- woods #qssmhints
==============
*/

void Print_Hints_f (void)
{
	Con_Printf("\n");
	for (int i = 0; i < num_hints; i++) 
	{
		Con_Printf("%s\n", hints[i]);
	}
	Con_Printf("\n");
}

/*
==============
DrawPause2 -- woods #showpaused #qssmhints
==============
*/
void SCR_DrawPause2(void)
{
	qpic_t* pic;
	char hint[80];
	static SDL_TimerID hint_timer_id = 0;

	GL_SetCanvas(CANVAS_MENU2); //johnfitz

	pic = Draw_CachePic("gfx/pause.lmp");

	if ((cl.match_pause_time > 0 && !cls.demoplayback) || pausedprint)
		Draw_Pic((320 - pic->width) / 2, (240 - 48 - pic->height) / 2, pic); //johnfitz -- stretched menus

	if (((cl.match_pause_time > 0 && !cls.demoplayback) || pausedprint) && scr_hints.value)
	{
		GL_SetCanvas(CANVAS_HINT);

		if (!timerstarted) // only start timer once
		{ 
			random_hint = hints[rand() % num_hints];
			hint_timer_id = SDL_AddTimer(6000, HintTimer_Callback, NULL);
			timerstarted = true;
		}

		snprintf(hint, sizeof(hint), "%s", random_hint);
		M_Print(360, 300, "QSS-M Hint");
		M_PrintWhite(400 - (strlen(hint) * 4), 320, hint);
	}
	else // remove timer when not paused, if it was started
	{ 
		if (timerstarted)
		{
			SDL_RemoveTimer(hint_timer_id);
			hint_timer_id = 0;
		}
		timerstarted = false;
	}

	scr_tileclear_updates = 0; //johnfitz
}

/*
==============
SCR_DrawLoading
==============
*/
void SCR_DrawLoading (void)
{
	qpic_t	*pic;

	if (!scr_drawloading)
		return;

	GL_SetCanvas (CANVAS_MENU2); //johnfitz

	pic = Draw_CachePic ("gfx/loading.lmp");
	Draw_Pic ( (320 - pic->width)/2, (240 - 48 - pic->height)/2, pic); //johnfitz -- stretched menus

	scr_tileclear_updates = 0; //johnfitz
}

void renderCircle (float cx, float cy, float r, int num_segments, float line_width) // woods #crosshair
{
	glLineWidth(line_width);

	glBegin(GL_LINE_LOOP);
	for (int i = 0; i < num_segments; i++) {
		float theta = 2.0f * M_PI * (float)i / (float)num_segments; // get the current angle
		float x = r * cosf(theta);
		float y = r * sinf(theta);

		glVertex2f(x + cx, y + cy);
	}
	glEnd();

	glLineWidth(1.0f);
}

void renderSmoothDot (float cx, float cy, float size) // woods #crosshair
{
	glEnable(GL_POINT_SMOOTH);
	glPointSize(size);
	glBegin(GL_POINTS);
	glVertex2f(cx, cy);
	glEnd();
	glDisable(GL_POINT_SMOOTH);
}

/*
==============
SCR_DrawCrosshair -- johnfitz -- woods major change #crosshair
==============
*/
void SCR_DrawCrosshair (void)
{
	
	if (key_dest == key_menu && !cl_menucrosshair.value)
		return;

	if (scr_viewsize.value >= 130)
		return;

	if (countdown && draw) // woods #clearcrxcountdown
		return;

	if (qeintermission && draw) // woods #qeintermission
		return;

	if (crxintermission) // woods #crxintermission
		return;

	plcolour_t color;
	if (strcmp(scr_crosshaircolor.string, "") == 0)
		color = CL_PLColours_Parse("0xffffff");
	else
		color = CL_PLColours_Parse(scr_crosshaircolor.string);

	plcolour_t outline = CL_PLColours_Parse("0x000000");
	plcolour_t damage = CL_PLColours_Parse(scr_crosshaircshift.string);
	float alpha = scr_crosshairalpha.value;

	if (cl.time <= cl.faceanimtime && cl_damagehue.value == 2)
	{ 
		color = damage;
		alpha = 1;
	}

	GL_SetCanvas (CANVAS_CROSSHAIR);

	if (crosshair.value == 1)
		Draw_CharacterRGBA (-4, -4, '+', color, alpha); //0,0 is center of viewport

	if (crosshair.value == 2) 
	{
		if (scr_crosshairoutline.value)
			Draw_FillPlayer (-2, -2, 4, 4, outline, alpha); // simple dot (black bg)
		Draw_FillPlayer (-1, -1, 2, 2, color, alpha); // simple dot
	}

	if (crosshair.value == 3)
	{
		if (scr_crosshairoutline.value) 
		{
			Draw_FillPlayer (-2, 5, 4, 12, outline, alpha); // SOUTH (black bg)
			Draw_FillPlayer (-17, -2, 12, 4, outline, alpha); // WEST (black bg)
			Draw_FillPlayer (5, -2, 12, 4, outline, alpha); // EAST (black bg)
			Draw_FillPlayer (-2, -17, 4, 12, outline, alpha); // NORTH (black bg)
		}
		Draw_FillPlayer (-1, 6, 2, 10, color, alpha); // SOUTH
		Draw_FillPlayer (-16, -1, 10, 2, color, alpha); // WEST
		Draw_FillPlayer (6, -1, 10, 2, color, alpha); // EAST
		Draw_FillPlayer (-1, -16, 2, 10, color, alpha); // NORTH
	}

	if (crosshair.value == 4)
	{
		if (scr_crosshairoutline.value)
		{
			Draw_FillPlayer (-2, -10, 4, 20, outline, alpha); // vertical (black bg)
			Draw_FillPlayer (-10, -2, 20, 4, outline, alpha); // horizontal (black bg)
		}
		Draw_FillPlayer (-1, -9, 2, 18, color, alpha); // vertical
		Draw_FillPlayer (-9, -1, 18, 2, color, alpha); // horizontal
	}

	if (crosshair.value == 5)
	{
		if (scr_crosshairoutline.value) 
		{
			Draw_FillPlayer (-3, -10, 6, 20, outline, 1); // vertical (black bg)
			Draw_FillPlayer (-10, -3, 20, 6, outline, 1); // horizontal (black bg)
		}
		Draw_FillPlayer (-2, -9, 4, 18, color, alpha); // vertical (thicker)
		Draw_FillPlayer (-9, -2, 18, 4, color, alpha); // horizontal (thicker)
	}

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_ALPHA_TEST);
	glEnable(GL_MULTISAMPLE);
	glEnable(GL_LINE_SMOOTH);
	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
	glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
	glEnable(GL_POLYGON_SMOOTH);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	float r = color.rgb[0] / 255.0f;
	float g = color.rgb[1] / 255.0f;
	float b = color.rgb[2] / 255.0f;
	float ro = outline.rgb[0] / 255.0f;
	float go = outline.rgb[1] / 255.0f;
	float bo = outline.rgb[2] / 255.0f;

	float dotSize = 3.0f * scr_crosshairscale.value;
	float outlineWidth = 4.0f;
	float outlineSize = dotSize + outlineWidth;
	float scaledLineWidth = scr_crosshairscale.value * 1.9f;

	if (crosshair.value == 6)
	{
		if (scr_crosshairoutline.value)
		{
			glColor4f(ro, go, bo, alpha); // Black color for outline
			renderSmoothDot(0.0f, 0.0f, outlineSize); // Slightly larger dot for outline
		}

		glColor4f(r, g, b, alpha); // Set color for actual dot
		renderSmoothDot(0.0f, 0.0f, dotSize); // Actual dot size
	}

	if (crosshair.value == 7)
	{
		glColor4f(r, g, b, alpha / 12); // Set color with alpha
		renderCircle(0.0f, 0.0f, 10.0f, 200, scaledLineWidth); // Draw circle at center with radius 10, more segments for smoothness

		if (scr_crosshairoutline.value)
		{
			glColor4f(ro, go, bo, 1.0f); // Black color for outline
			renderSmoothDot(0.0f, 0.0f, outlineSize); // Slightly larger dot for outline
		}

		glColor4f(r, g, b, 1.0f);
		renderSmoothDot(0.0f, 0.0f, dotSize); // Actual dot size
	}

	glColor4f(1, 1, 1, 1);
	glDisable(GL_MULTISAMPLE);
	glDisable(GL_LINE_SMOOTH);
	glDisable(GL_POLYGON_SMOOTH);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	GL_PolygonOffset(OFFSET_NONE);
	glDisable(GL_BLEND);
	glEnable(GL_ALPHA_TEST);
	glEnable(GL_TEXTURE_2D);
}

/*
================
LaserSight - port from quakespasm-shalrathy / qrack --  woods #laser
================
*/
void LaserSight (void)
{
	if (!cl.viewent.model || cl.viewent.model->name[0] == '\0' || cl.intermission || qeintermission || crxintermission || scr_viewsize.value >= 130 ||
		(countdown && draw) || (qeintermission && draw) || cl.stats[STAT_HEALTH] <= 0 || 
		!strcmp(cl.viewent.model->name, "progs/v_axe.mdl") || chase_active.value) //R00k: dont show laserpoint when observer!
	{
		return;
	}

	vec3_t	start, forward, right, up, crosshair, wall, origin;

	// copy origin to start, offset it correctly

	AngleVectors(r_refdef.viewangles, forward, right, up);
	VectorCopy(cl.entities[cl.viewentity].origin, start);
	VectorCopy(cl.entities[cl.viewentity].origin, origin);
	start[2] += 16;//QuakeC uses + '0 0 16' for gun aim.

	// find the spot the player is looking at
	VectorMA(start, 4096, forward, crosshair);
	TraceLine(start, crosshair, 0, wall);

	glDisable(GL_DEPTH_TEST);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	GL_PolygonOffset(OFFSET_SHOWTRIS);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);

	glColor4f(0.0, 1.0, 0.0, gl_laserpoint_alpha.value); // draw green line

	if (gl_laserpoint.value) 
	{
		glLineWidth(3.0f);
		glBegin(GL_LINES);
		if (gl_laserpoint.value == 2) // straight line
			glVertex3f(crosshair[0], crosshair[1], crosshair[2]);
		else // straight line, variable crosshair
			glVertex3f(wall[0], wall[1], wall[2]);
		glVertex3f(origin[0], origin[1], origin[2]);
		glEnd();
		glLineWidth(1.0f);

		if (gl_laserpoint.value == 1)
		{
			glEnable(GL_POINT_SMOOTH);
			glPointSize(12.0f); // set the size of the point
			glBegin(GL_POINTS);
			glVertex3f(wall[0], wall[1], wall[2]);
			glEnd();
			glDisable(GL_POINT_SMOOTH);
			glPointSize(1.0f);

			PScript_RunParticleEffectTypeString(wall, NULL, 1, "laserpoint"); // particle cfg "r_part laserpoint" for dot on wall
		}
	}

	glColor4f(1, 1, 1, 1);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_CULL_FACE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	GL_PolygonOffset(OFFSET_NONE);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);		
}

//=============================================================================


/*
==================
SCR_SetUpToDrawConsole
==================
*/
void SCR_SetUpToDrawConsole (void)
{
	//johnfitz -- let's hack away the problem of slow console when host_timescale is <0
	extern cvar_t host_timescale;
	float timescale, conspeed;
	//johnfitz

	Con_CheckResize ();

	if (scr_drawloading)
		return;		// never a console with loading plaque

// decide on the height of the console
	con_forcedup = !cl.worldmodel || cls.signon != SIGNONS;

	if (con_forcedup)
	{
		scr_conlines = glheight; //full screen //johnfitz -- glheight instead of vid.height
		scr_con_current = scr_conlines;
	}
	else if (key_dest == key_console)
	{
		scr_conlines = glheight * scr_consize.value; //johnfitz -- glheight instead of vid.height // woods #consize (joequake)
		if (scr_conlines < 50)
			scr_conlines = 50;
		if (scr_conlines > glheight - 50)
			scr_conlines = glheight - 50;
	}
	else
		scr_conlines = 0; //none visible

	timescale = (host_timescale.value > 0) ? host_timescale.value : 1; //johnfitz -- timescale
	conspeed = (scr_conspeed.value > 0 && !cls.timedemo) ? scr_conspeed.value : 1e6f;

	if (scr_conlines < scr_con_current)
	{
		// ericw -- (glheight/600.0) factor makes conspeed resolution independent, using 800x600 as a baseline
		scr_con_current -= conspeed*(glheight/600.0)*host_frametime/timescale; //johnfitz -- timescale
		if (scr_conlines > scr_con_current)
			scr_con_current = scr_conlines;
	}
	else if (scr_conlines > scr_con_current)
	{
		// ericw -- (glheight/600.0)
		scr_con_current += conspeed*(glheight/600.0)*host_frametime/timescale; //johnfitz -- timescale
		if (scr_conlines < scr_con_current)
			scr_con_current = scr_conlines;
	}

	if (clearconsole++ < vid.numpages)
		Sbar_Changed ();
	else
		con_notifylines_ = 0; // woods from proquake 493 #notifylines

	if (!con_forcedup && scr_con_current)
		scr_tileclear_updates = 0; //johnfitz
}

/*
==================
SCR_DrawConsole
==================
*/
void SCR_DrawConsole (void)
{
	if (scr_con_current)
	{
		Con_DrawConsole (scr_con_current, true);
		clearconsole = 0;
	}
	else
	{
		if (key_dest == key_game || key_dest == key_message)
			Con_DrawNotify ();	// only draw notify in game
	}
}


/*
==============================================================================

SCREEN SHOTS

==============================================================================
*/

//======================================================
// woods #screenshotcopy from fitzquake markvr9
//======================================================

#if defined(_WIN32)
static void FlipBuffer(byte* buffer, const int columns, const int rows, const int BytesPerPixel)	// Flip the image because of GL's up = positive-y
{
	int		bufsize = columns * BytesPerPixel; // bufsize=widthBytes;

	byte* tb1 = malloc(bufsize);
	byte* tb2 = malloc(bufsize);
	int		i, offset1, offset2;

	for (i = 0; i < (rows + 1) / 2;i++)
	{
		offset1 = i * bufsize;
		offset2 = ((rows - 1) - i) * bufsize;

		memcpy(tb1, buffer + offset1, bufsize);
		memcpy(tb2, buffer + offset2, bufsize);
		memcpy(buffer + offset1, tb2, bufsize);
		memcpy(buffer + offset2, tb1, bufsize);
	}

	free(tb1);
	free(tb2);
	return;
}

void SCR_ScreenShot_Clipboard_f(void)
{
	int		buffersize = glwidth * glheight * 4; // 4 bytes per pixel
	byte* buffer = malloc(buffersize);

	//get data
	glReadPixels(glx, gly, glwidth, glheight, GL_BGRA_EXT, GL_UNSIGNED_BYTE, buffer);

	// We are upside down flip it
	FlipBuffer(buffer, glwidth, glheight, 4 /* bytes per pixel */);

	// FIXME: No gamma correction of screenshots in Fitz?
	Sys_Image_BGRA_To_Clipboard(buffer, glwidth, glheight, buffersize);

	//Con_Printf("\nscreenshot copied to clipboard\n");

	free(buffer);
}
#endif

#ifdef __APPLE__
static void FlipBuffer(byte* buffer, const int columns, const int rows, const int BytesPerPixel) {
	int bufsize = columns * BytesPerPixel;
	byte* temp = malloc(bufsize);
	if (!temp) return;  // Handle allocation failure

	for (int i = 0; i < rows / 2; i++) {
		byte* row1 = buffer + i * bufsize;
		byte* row2 = buffer + (rows - 1 - i) * bufsize;

		memcpy(temp, row1, bufsize);
		memcpy(row1, row2, bufsize);
		memcpy(row2, temp, bufsize);
	}

	free(temp);
}

void SCR_ScreenShot_Clipboard_f(void) {
	int width = glwidth;    // Replace with your OpenGL viewport width
	int height = glheight;  // Replace with your OpenGL viewport height
	int buffersize = width * height * 4; // 4 bytes per pixel

	byte* buffer = malloc(buffersize);
	if (!buffer) {
		// Handle allocation failure
		fprintf(stderr, "Failed to allocate memory for screenshot buffer.\n");
		return;
	}

	// Get data from OpenGL buffer
	glReadPixels(glx, gly, width, height, GL_BGRA, GL_UNSIGNED_BYTE, buffer);

	// Flip the image vertically
	FlipBuffer(buffer, width, height, 4 /* bytes per pixel */);

	// Copy the image buffer to the clipboard
	Sys_Image_BGRA_To_Clipboard(buffer, width, height, buffersize);

	//Con_Printf("\nscreenshot copied to clipboard\n");

	free(buffer);
}
#endif

static void SCR_ScreenShot_Usage (void)
{
	Con_Printf ("usage: screenshot <format> <quality>\n");
	Con_Printf ("   format must be \"png\" or \"tga\" or \"jpg\"\n");
	Con_Printf ("   quality must be 1-100\n");
	return;
}

/*
==================
SCR_ScreenShot_f -- johnfitz -- rewritten to use Image_WriteTGA
==================
*/
void SCR_ScreenShot_f (void)
{
	byte	*buffer;
	char	ext[4];
	char	imagename[MAX_OSPATH];  //johnfitz -- was [80] // woods #screenshots was 16
	char	checkname[MAX_OSPATH];
	int	quality;
	qboolean	ok;

	// woods added time for demo output // woods #screenshots
	char str[24];
	time_t systime = time(0);
	struct tm loct = *localtime(&systime);

	strftime(str, 24, "%m-%d-%Y-%H%M%S", &loct); // time and date support

	q_snprintf(checkname, sizeof(checkname), "%s/screenshots", com_gamedir); // woods #screenshots
	Sys_mkdir(com_gamedir); //  woods create gamedir if not there #screenshots
	Sys_mkdir(checkname); //  woods create screenshots if not there #screenshots
	
	Q_strncpy (ext, "png", sizeof(ext));

	if (Cmd_Argc () >= 2)
	{
		const char	*requested_ext = Cmd_Argv (1);

		if (!q_strcasecmp ("png", requested_ext)
		    || !q_strcasecmp ("tga", requested_ext)
		    || !q_strcasecmp ("jpg", requested_ext))
			Q_strncpy (ext, requested_ext, sizeof(ext));
		else
		{
			SCR_ScreenShot_Usage ();
			return;
		}
	}

// read quality as the 3rd param (only used for JPG)
	quality = 90;
	if (Cmd_Argc () >= 3)
		quality = Q_atoi (Cmd_Argv(2));
	if (quality < 1 || quality > 100)
	{
		SCR_ScreenShot_Usage ();
		return;
	}
	
	if (cl.mapname[0] == '\0' || cls.state == ca_disconnected)
		q_snprintf(imagename, sizeof(imagename), "screenshots/qssm_%s.%s", str, ext); // woods #screenshots time and date support
	else
		q_snprintf(imagename, sizeof(imagename), "screenshots/qssm_%s_%s.%s", cl.mapname, str, ext);

	q_snprintf(checkname, sizeof(checkname), "%s/%s", com_gamedir, imagename);

//get data
	if (!(buffer = (byte *) malloc(glwidth*glheight*3)))
	{
		Con_Printf ("SCR_ScreenShot_f: Couldn't allocate memory\n");
		return;
	}

	glPixelStorei (GL_PACK_ALIGNMENT, 1);/* for widths that aren't a multiple of 4 */
	glReadPixels (glx, gly, glwidth, glheight, GL_RGB, GL_UNSIGNED_BYTE, buffer);

// now write the file
	if (!q_strncasecmp (ext, "png", sizeof(ext)))
		ok = Image_WritePNG (imagename, buffer, glwidth, glheight, 24, false);
	else if (!q_strncasecmp (ext, "tga", sizeof(ext)))
		ok = Image_WriteTGA (imagename, buffer, glwidth, glheight, 24, false);
	else if (!q_strncasecmp (ext, "jpg", sizeof(ext)))
		ok = Image_WriteJPG (imagename, buffer, glwidth, glheight, 24, quality, false);
	else
		ok = false;

	if (ok)
	{ 
		Con_Printf ("Wrote %s\n", checkname);

		const char* soundFile = COM_FileExists("sound/qssm/copy.wav", NULL) ? "qssm/copy.wav" : "player/tornoff2.wav";
		S_LocalSound(soundFile); // woods add sound to screenshot
	}
	else
		Con_Printf ("SCR_ScreenShot_f: Couldn't create %s\n", imagename);

#if defined(_WIN32) || defined(__APPLE__)
	SCR_ScreenShot_Clipboard_f();	// woods #screenshotcopy
#endif

	free (buffer);
}


//=============================================================================


/*
===============
SCR_BeginLoadingPlaque

================
*/
void SCR_BeginLoadingPlaque (void)
{
	S_StopAllSounds (true);

	if (cls.state != ca_connected)
		return;
	if (cls.signon != SIGNONS)
		return;

// redraw with no console and the loading plaque
	Con_ClearNotify ();
	scr_centertime_off = 0;
	scr_con_current = 0;

	scr_drawloading = true;
	Sbar_Changed ();
	SCR_UpdateScreen ();
	scr_drawloading = false;

	scr_disabled_for_loading = true;
	scr_disabled_time = realtime;
}

/*
===============
SCR_EndLoadingPlaque

================
*/
void SCR_EndLoadingPlaque (void)
{
	scr_disabled_for_loading = false;
	Con_ClearNotify ();
}

//=============================================================================

const char	*scr_notifystring;
qboolean	scr_drawdialog;

void SCR_DrawNotifyString (void)
{
	const char	*start;
	int		l;
	int		j;
	int		x, y;

	GL_SetCanvas (CANVAS_MENU); //johnfitz

	start = scr_notifystring;

	y = 200 * 0.35; //johnfitz -- stretched overlays

	do
	{
	// scan the width of the line
		for (l=0 ; l<40 ; l++)
			if (start[l] == '\n' || !start[l])
				break;
		x = (320 - l*8)/2; //johnfitz -- stretched overlays
		for (j=0 ; j<l ; j++, x+=8)
			Draw_Character (x, y, start[j]);

		y += 8;

		while (*start && *start != '\n')
			start++;

		if (!*start)
			break;
		start++;		// skip the \n
	} while (1);
}

/*
==================
SCR_ModalMessage

Displays a text string in the center of the screen and waits for a Y or N
keypress.
==================
*/
int SCR_ModalMessage (const char *text, float timeout) //johnfitz -- timeout
{
	double time1, time2; //johnfitz -- timeout
	int lastkey, lastchar;

	if (cls.state == ca_dedicated)
		return true;

	scr_notifystring = text;

// draw a fresh screen
	scr_drawdialog = true;
	SCR_UpdateScreen ();
	scr_drawdialog = false;

	S_ClearBuffer ();		// so dma doesn't loop current sound

	time1 = Sys_DoubleTime () + timeout; //johnfitz -- timeout
	time2 = 0.0f; //johnfitz -- timeout

	Key_BeginInputGrab ();
	do
	{
		Sys_SendKeyEvents ();
		Key_GetGrabbedInput (&lastkey, &lastchar);
		Sys_Sleep (16);
		if (timeout) time2 = Sys_DoubleTime (); //johnfitz -- zero timeout means wait forever.
	} while (lastchar != 'y' && lastchar != 'Y' &&
		 lastchar != 'n' && lastchar != 'N' &&
		 lastkey != K_ESCAPE &&
		 lastkey != K_ABUTTON &&
		 lastkey != K_BBUTTON &&
		 lastkey != K_MOUSE2 &&  // woods #mousemenu (iw)
		 lastkey != K_MOUSE4 &&
		 time2 <= time1);
	Key_EndInputGrab ();

//	SCR_UpdateScreen (); //johnfitz -- commented out

	//johnfitz -- timeout
	if (time2 > time1)
		return false;
	//johnfitz

	return (lastchar == 'y' || lastchar == 'Y' || lastkey == K_ABUTTON);
}


//=============================================================================

//johnfitz -- deleted SCR_BringDownConsole


/*
==================
SCR_TileClear
johnfitz -- modified to use glwidth/glheight instead of vid.width/vid.height
	    also fixed the dimentions of right and top panels
	    also added scr_tileclear_updates
==================
*/
void SCR_TileClear (void)
{
	//ericw -- added check for glsl gamma. TODO: remove this ugly optimization?
	if (scr_tileclear_updates >= vid.numpages && !gl_clear.value && !(gl_glsl_gamma_able && vid_gamma.value != 1))
		return;
	scr_tileclear_updates++;

	if (r_refdef.vrect.x > 0)
	{
		// left
		Draw_TileClear (0,
						0,
						r_refdef.vrect.x,
						glheight - sb_lines);
		// right
		Draw_TileClear (r_refdef.vrect.x + r_refdef.vrect.width,
						0,
						glwidth - r_refdef.vrect.x - r_refdef.vrect.width,
						glheight - sb_lines);
	}

	if (r_refdef.vrect.y > 0)
	{
		// top
		Draw_TileClear (r_refdef.vrect.x,
						0,
						r_refdef.vrect.width,
						r_refdef.vrect.y);
		// bottom
		Draw_TileClear (r_refdef.vrect.x,
						r_refdef.vrect.y + r_refdef.vrect.height,
						r_refdef.vrect.width,
						glheight - r_refdef.vrect.y - r_refdef.vrect.height - sb_lines);
	}
}

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.

WARNING: be very careful calling this from elsewhere, because the refresh
needs almost the entire 256k of stack space!
==================
*/
void SCR_UpdateScreen (void)
{
	vid.numpages = (gl_triplebuffer.value) ? 3 : 2;

	if (scr_disabled_for_loading)
	{
		if (realtime - scr_disabled_time > 60)
		{
			scr_disabled_for_loading = false;
			Con_Printf ("load failed.\n");
		}
		else
			return;
	}

	if (!scr_initialized || !con_initialized)
		return;				// not initialized yet


	GL_BeginRendering (&glx, &gly, &glwidth, &glheight);

	if (cl.worldmodel && cl.qcvm.worldmodel && cl.qcvm.extfuncs.CSQC_UpdateView)
	{
		float s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		SCR_SetUpToDrawConsole ();
		GL_SetCanvas (CANVAS_CSQC);

		PR_SwitchQCVM(&cl.qcvm);

		if (qcvm->extglobals.cltime)
			*qcvm->extglobals.cltime = realtime;
		if (qcvm->extglobals.clframetime)
			*qcvm->extglobals.clframetime = host_frametime;
		if (qcvm->extglobals.player_localentnum)
			*qcvm->extglobals.player_localentnum = cl.viewentity;
		if (qcvm->extglobals.intermission)
			*qcvm->extglobals.intermission = cl.intermission;
		if (qcvm->extglobals.intermission_time)
			*qcvm->extglobals.intermission_time = cl.completed_time;
		if (qcvm->extglobals.view_angles)
			VectorCopy(cl.viewangles, qcvm->extglobals.view_angles);
		if (qcvm->extglobals.clientcommandframe)
			*qcvm->extglobals.clientcommandframe = cl.movemessages;
		if (qcvm->extglobals.servercommandframe)
			*qcvm->extglobals.servercommandframe = cl.ackedmovemessages;
//		Sbar_SortFrags ();

		pr_global_struct->time = qcvm->time;
		pr_global_struct->frametime = qcvm->frametime;
		G_FLOAT(OFS_PARM0) = glwidth/s;
		G_FLOAT(OFS_PARM1) = glheight/s;
		G_FLOAT(OFS_PARM2) = true;
		if (cls.signon == SIGNONS||!cl.qcvm.extfuncs.CSQC_UpdateViewLoading)
			PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_UpdateView);
		else
			PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_UpdateViewLoading);
		PR_SwitchQCVM(NULL);

		GL_Set2D ();
	}
	else
	{
		//
		// determine size of refresh window
		//
		r_refdef.drawworld = true;
		if (vid.recalc_refdef)
			SCR_CalcRefdef ();

//
// do 3D refresh drawing, and then update the screen
//
		SCR_SetUpToDrawConsole ();

		V_RenderView ();

		GL_Set2D ();

		//FIXME: only call this when needed
		SCR_TileClear ();

		if (!cl.intermission)
		{
			Sbar_Draw ();
			if (!scr_drawloading && !con_forcedup)
				SCR_DrawCrosshair (); //johnfitz
		}
	}

	if (scr_drawdialog) //new game confirm
	{
		if (con_forcedup)
			Draw_ConsoleBackground ();
		Draw_FadeScreen ();
		SCR_DrawNotifyString ();
	}
	else if (scr_drawloading) //loading
	{
		SCR_DrawLoading ();
	}
	else if (cl.intermission == 1 && key_dest == key_game) //end of level
	{
		Sbar_IntermissionOverlay ();
		SCR_DrawDemoControls(); // woods (iw) #democontrols
	}
	else if (cl.intermission == 2 && key_dest == key_game) //end of episode
	{
		Sbar_FinaleOverlay ();
		SCR_CheckDrawCenterString ();
		SCR_DrawDemoControls(); // woods (iw) #democontrols
	}
	else
	{
		SCR_DrawNet ();
		SCR_DrawTurtle ();
		SCR_DrawPause ();
		SCR_DrawPause2 (); // woods #showpaused
		SCR_CheckDrawCenterString ();
		SCR_DrawDevStats (); //johnfitz
		SCR_DrawFPS (); //johnfitz
		SCR_DrawClock (); //johnfitz
		SCR_DrawDemoControls(); // woods (iw) #democontrols
		SCR_ShowPing (); // woods #scrping
		SCR_ShowPL (); // woods #scrpl
		SCR_DrawMatchClock (); // woods #matchhud
		SCR_DrawMatchScores (); // woods #matchhud
		SCR_ShowFlagStatus (); // woods #matchhud #flagstatus
		SCR_ShowObsFrags (); // woods #observerhud
		SCR_DrawSpeed (); // woods #speed
		SCR_DrawMovementKeys (); // woods #movementkeys
		TP_DrawClosestLocText (); // woods #locext
		SCR_Mute (); // woods #usermute
		SCR_Observing (); // woods
		TexturePointer_Draw (); // woods #texturepointer
		SCR_DrawConsole ();
		M_Draw ();
	}

	V_UpdateBlend (); //johnfitz -- V_UpdatePalette cleaned up and renamed

	GLSLGamma_GammaCorrect ();

	GL_EndRendering ();
}

