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
static void SCR_DiagDump_f (void); // woods #scr_diag
static void SCR_DiagWriteReport (void); // woods #scr_diag

Uint32 HintTimer_Callback(Uint32 interval, void* param); // woods #qssmhints
void Print_Hints_f(void); // woods #qssmhints
extern qboolean netquakeio; // woods
extern const char *svc_strings[128]; // woods #scr_diag

void TexturePointer_Draw(void); // woods #texturepointer

extern cvar_t r_lerpmove; //johnfitz

extern qpic_t* sb_nums[2][11]; // woods #varmatchclock
extern qpic_t* sb_colon; // woods #varmatchclock

extern cvar_t scr_scoreboard_teamsort; // woods #teamscoreboard
void Sbar_SortFrags_TeamOrder(qboolean sort_ascending); // woods #teamscoreboard
qboolean Sbar_ShouldSortByTeam(void); // woods #teamscoreboard

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
cvar_t		scr_crosshair_x = {"scr_crosshair_x", "0", CVAR_ARCHIVE}; // woods #crosshair
cvar_t		scr_crosshair_y = {"scr_crosshair_y", "0", CVAR_ARCHIVE}; // woods #crosshair
cvar_t		scr_showfps = {"scr_showfps", "0", CVAR_ARCHIVE};
cvar_t		scr_diagnostics = {"scr_diagnostics", "0", CVAR_ARCHIVE}; // woods #scr_diag
cvar_t		scr_clock = {"scr_clock", "0", CVAR_ARCHIVE};
cvar_t		scr_showgrenadecounter = {"scr_showgrenadecounter", "0", CVAR_ARCHIVE}; // woods #nadecount
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
cvar_t		scr_obsitems = {"scr_obsitems", "1",CVAR_ARCHIVE}; // woods
cvar_t		scr_hints = {"scr_hints", "1",CVAR_ARCHIVE}; // woods #qssmhints
cvar_t		scr_customcursor = {"scr_customcursor", "1", CVAR_ARCHIVE}; // woods #customcursor
//johnfitz
cvar_t		scr_usekfont = {"scr_usekfont", "0", CVAR_NONE}; // 2021 re-release
cvar_t		cl_predict = { "cl_predict", "0", CVAR_NONE }; // 2021 re-release

cvar_t		scr_demobar_timeout = {"scr_demobar_timeout", "1", CVAR_ARCHIVE}; // woods (iw) #democontrols
cvar_t		scr_viewsize = {"viewsize","100", CVAR_ARCHIVE};
cvar_t		scr_fov = {"fov","90",CVAR_ARCHIVE};	// 10 - 170
cvar_t		scr_fov_adapt = {"fov_adapt","1",CVAR_ARCHIVE};
cvar_t		scr_zoomfov = {"zoom_fov","30",CVAR_ARCHIVE};	// 10 - 170 // woods #zoom (ironwail)
cvar_t		scr_zoomspeed = {"zoom_speed","8",CVAR_ARCHIVE}; // woods #zoom (ironwail)
cvar_t		scr_scopealpha = {"scr_scopealpha",".55",CVAR_ARCHIVE}; // woods #scope
cvar_t		scr_scoperadius = {"scr_scoperadius",".4",CVAR_ARCHIVE}; // woods #scope
cvar_t		scr_scopefadespeed = {"scr_scopefadespeed","70",CVAR_ARCHIVE}; // woods #scope
cvar_t		scr_conspeed = {"scr_conspeed","500",CVAR_ARCHIVE};
cvar_t		scr_centertime = {"scr_centertime","2",CVAR_NONE};
cvar_t		scr_showturtle = {"showturtle","0",CVAR_NONE};
cvar_t		scr_showpause = {"showpause","1",CVAR_NONE};
cvar_t		scr_printspeed = {"scr_printspeed","8",CVAR_NONE};
cvar_t		scr_autoid = {"scr_autoid","1",CVAR_ARCHIVE}; // woods #autoid
cvar_t		scr_scoreboard_teamsort = {"scr_scoreboard_teamsort","0",CVAR_ARCHIVE}; // woods #teamscoreboard
cvar_t		gl_triplebuffer = {"gl_triplebuffer", "1", CVAR_ARCHIVE};
extern cvar_t	r_clearcolor; // woods #fxaa

cvar_t		cl_gun_fovscale = {"cl_gun_fovscale","1",CVAR_ARCHIVE}; // Qrack
cvar_t		cl_menucrosshair = { "cl_menucrosshair","0",CVAR_ARCHIVE}; // woods #menucrosshair
cvar_t		cl_pong = {"cl_pong","2",CVAR_ARCHIVE}; // woods #pong -- 0 = disabled, >0 = enabled with speed multiplier

static const float GRENADE_EXPLOSION_TIME = 2.5f; // woods #nadecount
static const float GRENADE_TIMER_WIDTH = 24.0f; // woods #nadecount

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
extern qboolean WordFilter_Check(const char* text, char* dest_buffer, size_t buffer_size); // woods #contentfilter

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
		if (!cl.match_pause_time) // let client connecting during pause know by setting cl.match_pause_time
			cl.match_pause_time = cl.time;
		
		pausedprint = true; // woods #qssmhints
		return;
	}

	char qfrunequake[18] = { 247, 247, 247, 174, 242, 245, 238, 229, 241, 245, 225, 235, 229, 174, 227, 239, 237, '\0' }; // woods -- quake font red 'www.runequake.com' #autoid

	if ((strstr(str, qfrunequake)))
	{
		cl.modtype = 6;
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
	char buf2[15];
	const char* realobs;
	const char* star_realobs;
	realobs = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "observer", buf, sizeof(buf));
	star_realobs = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "*observer", buf2, sizeof(buf2));

	if (!scr_obscenterprint.value && !cameras && !countdown && !qeintermission && !crxintermission &&
		((cl.modtype == 1 || cl.modtype == 4) &&
			((!strcmp(realobs, "eyecam") || !strcmp(realobs, "chase")) ||
				(!strcmp(star_realobs, "eyecam") || !strcmp(star_realobs, "chase")))))
		return;

	if (!strcmp(cl.observer, "y") && (cl.modtype >= 2)) // woods #observer
		GL_SetCanvas(CANVAS_OBSERVER); //johnfitz //  center print moved down near weapon
	else
		GL_SetCanvas(CANVAS_MOD); //johnfitz // woods messages scale with console font size instead

	char filtered_centerstring[1024];
	if (cl_contentfilter.value == 2) // woods #contentfilter
	{
		if (WordFilter_Check(scr_centerstring, filtered_centerstring, sizeof(filtered_centerstring))) {
			// Ensure the filtered string is null-terminated
			filtered_centerstring[sizeof(filtered_centerstring) - 1] = '\0';
			start = filtered_centerstring;
		}
		else {
			start = scr_centerstring;
		}
	}
	else {
		start = scr_centerstring;
	}

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
===============
Clock_Completion_f
===============
*/
static void Clock_Completion_f(cvar_t* cvar, const char* partial)
{
	Con_AddToTabList("0", partial, "off", NULL);
	Con_AddToTabList("1", partial, "level time", NULL);
	Con_AddToTabList("2", partial, "12hr clock", NULL);
	Con_AddToTabList("3", partial, "24hr clock", NULL);
	Con_AddToTabList("4", partial, "date only", NULL);
	Con_AddToTabList("5", partial, "date + 12hr", NULL);
	Con_AddToTabList("6", partial, "date + 24hr", NULL);
	Con_AddToTabList("7", partial, "showscores date + 12hr", NULL);
	Con_AddToTabList("8", partial, "showscores date + 24hr", NULL);

	return;
}

/*
==================
SCR_CustomCursor_f -- woods #customcursor
Keep OS cursor assets in sync with scr_customcursor changes.
==================
*/
static void SCR_CustomCursor_f(cvar_t* cvar)
{
	LoadCustomCursorImage();
	Con_ReloadIBeamCursor();
	VID_UpdateCursor();
	IN_UpdateGrabs();
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
	Cvar_RegisterVariable (&scr_crosshair_x); // woods #crosshair
	Cvar_RegisterVariable (&scr_crosshair_y); // woods #crosshair
	Cvar_RegisterVariable (&scr_showfps);
	Cvar_SetCompletion (&scr_clock, &Clock_Completion_f); // woods #iwtabcomplete
	Cvar_RegisterVariable (&scr_clock);
	Cvar_RegisterVariable (&scr_showgrenadecounter); // woods #nadecount
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
	Cvar_RegisterVariable (&scr_diagnostics); // woods #scr_diag
	Cvar_RegisterVariable (&scr_obscenterprint); // woods
	Cvar_RegisterVariable (&scr_obsitems); // woods
	Cvar_RegisterVariable (&scr_hints); // woods #qssmhints
	Cvar_RegisterVariable (&scr_customcursor); // woods #customcursor
	Cvar_SetCallback (&scr_customcursor, SCR_CustomCursor_f); // woods #customcursor
	SCR_CustomCursor_f(&scr_customcursor); // woods #customcursor - load cursors with current setting
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
	Cvar_RegisterVariable (&scr_scopealpha); // woods #scope
	Cvar_RegisterVariable (&scr_scoperadius); // woods #scope
	Cvar_RegisterVariable (&scr_scopefadespeed); // woods #scope
	Cvar_RegisterVariable (&scr_viewsize);
	Cvar_SetCompletion (&scr_viewsize, CompleteViewsize_f); // woods #scrviewsize
	Cvar_RegisterVariable (&scr_conspeed);
	Cvar_RegisterVariable (&scr_showturtle);
	Cvar_RegisterVariable (&scr_showpause);
	Cvar_RegisterVariable (&scr_centertime);
	Cvar_RegisterVariable (&scr_printspeed);
	Cvar_RegisterVariable (&scr_autoid); // woods #autoid
	Cvar_RegisterVariable (&scr_scoreboard_teamsort); // woods #teamscoreboard
	Cvar_RegisterVariable (&gl_triplebuffer);
	Cvar_RegisterVariable (&cl_gun_fovscale);
	Cvar_RegisterVariable (&cl_menucrosshair); // woods #menucrosshair
	Cvar_RegisterVariable (&cl_pong); // woods #pong

	Cmd_AddCommand ("screenshot",SCR_ScreenShot_f);
	Cmd_AddCommand ("sizeup",SCR_SizeUp_f);
	Cmd_AddCommand ("sizedown",SCR_SizeDown_f);

	Cmd_AddCommand("togglezoom", SCR_ToggleZoom_f); // woods #zoom (ironwail)
	Cmd_AddCommand("+zoom", SCR_ZoomDown_f); // woods #zoom (ironwail)
	Cmd_AddCommand("-zoom", SCR_ZoomUp_f); // woods #zoom (ironwail)
	Cmd_AddCommand("hints", Print_Hints_f); // woods #hints
	Cmd_AddCommand("diagnosticsdump", SCR_DiagDump_f); // woods #scr_diag

	SCR_LoadPics (); //johnfitz

	scr_initialized = true;

	LoadCustomCursorImage (); // woods #customcursor
	Pong_Init (); // woods #pong
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

	cl.fps = (int)lastfps; // woods #f_config

	if (scr_showfps.value)
	{
		char	st[12];
		int	x, y;
		if (scr_showfps.value == 2)
			q_snprintf(st, sizeof(st), "%4.0f", lastfps);
		else
			q_snprintf(st, sizeof(st), "%4.0f fps", lastfps);

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

		if (scr_clock.value)
			y -= 12; //make room for clock // woods added padding

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

					q_snprintf(num, sizeof(num), "%i%s", s->ping,
						(scr_ping.value == 1 || scr_ping.value == 3) ? " ms" : "");

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

	if (!scr_ping.value)
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
				q_snprintf(num, sizeof(num), "%i%s", lastPL, scr_ping.value == 3 ? " pl" : "");
				M_Print(x, y, num);

			}
		}
		else
		{
			lastPL = 0;
		}
	}
}

/*
==================
SCR_DrawDiagnostics -- woods #scr_diag
==================
*/
#define DIAG_WINDOW_SECS 5.0
#define DIAG_SLOW_MS 33.0f
#define DIAG_FREEZE_MS 100.0f

typedef struct scr_diag_state_s
{
	qboolean	active;
	double		last_realtime;
	double		window_start;
	double		sum_frametime_ms;
	int			frame_count;
	int			window_seq;
	int			last_packets_sent;
	int			last_packets_received;
	int			last_packets_resent;
	int			last_dropped;
	unsigned long long	last_bytes_sent;
	unsigned long long	last_bytes_received;
	int			last_pltotal;
	int			pl_window;
	int			slow_count;
	int			freeze_count;
	float		max_frametime_ms;
	float		last_frametime_ms;
	double		diag_start_time;
	qboolean	net_bad;
	double		net_bad_start;
	double		net_bad_total;
	double		net_last_bad_duration;
	double		net_last_recovery_time;
	qboolean	frame_bad;
	double		frame_bad_start;
	double		frame_bad_total;
	double		frame_last_bad_duration;
	double		frame_last_recovery_time;
	qboolean	input_bad;
	double		input_bad_start;
	double		input_bad_total;
	double		input_last_bad_duration;
	double		input_last_recovery_time;
	// Server tick jitter tracking
	float		sv_dt_samples[64];
	int			sv_dt_head;
	int			sv_dt_count;
	double		sv_dt_last_mtime0;
	// Net message buffer peak
	int			net_msg_peak;
} scr_diag_state_t;

static scr_diag_state_t scr_diag_state;
static double scr_diag_render_ms;

typedef struct scr_diag_snapshot_s
{
	qboolean baselines_valid;
	double window_elapsed;
	int pl_window;
	int pl_total;
	int slow_count;
	int freeze_count;
	float max_ms;
	float avg_ms;
	float last_ms;
	float net_gap_ms;
	float sv_dt_ms;
	float render_ms;
	float input_age_ms;
	float pps_in;
	float pps_out;
	float kb_in;
	float kb_out;
	float loss_pct;
	float resend_pps;
	int pkt_sent;
	int pkt_recv;
	int pkt_resent;
	int pkt_dropped;
	unsigned long long bytes_sent;
	unsigned long long bytes_recv;
	// Connection diag (from NET_GetConnectionDiag)
	unsigned int seq_send, seq_recv, seq_ack;
	qboolean can_send;
	int reliable_queue_bytes;
	double connect_time;
	double last_msg_time;
	double last_send_time;
	// Expanded packet stats
	int pkt_duplicates;
	int pkt_short;
	// Computed metrics
	float sv_tick_hz;
	float sv_tick_jitter_ms;
	float lerp_frac;
	qboolean extrapolating;
	float timeout_remaining;
	int conn_uptime_secs;
	float avg_pkt_in;
	float avg_pkt_out;
	int net_msg_peak;
	// Move ACK gap
	int move_ack_gap;
	// Reliable vs unreliable message counts (cumulative)
	int msgs_reliable;
	int msgs_unreliable;
} scr_diag_snapshot_t;

typedef struct scr_diag_class_s
{
	int net;
	int frame;
	int input;
} scr_diag_class_t;

typedef struct scr_diag_summary_s
{
	int key;
	int window_seq;
	double last_update;
	int lines;
	char line1[192];
	char line2[192];
	char line3[192];
} scr_diag_summary_t;

static scr_diag_summary_t scr_diag_summary;

static scr_diag_class_t SCR_DiagClassify (const scr_diag_snapshot_t *snap)
{
	scr_diag_class_t c;

	c.net = 0;
	if (snap->loss_pct > 2.0f || snap->net_gap_ms > 250.0f || snap->pl_window >= 3)
		c.net = 2;
	else if (snap->loss_pct > 0.5f || snap->net_gap_ms > 100.0f || snap->pl_window > 0)
		c.net = 1;

	c.frame = 0;
	if (snap->freeze_count > 0 || snap->max_ms > 100.0f || snap->avg_ms > 33.0f)
		c.frame = 2;
	else if (snap->slow_count > 0 || snap->max_ms > 50.0f || snap->avg_ms > 20.0f)
		c.frame = 1;

	c.input = 0;
	if (snap->input_age_ms > 50.0f)
		c.input = 2;
	else if (snap->input_age_ms > 20.0f)
		c.input = 1;

	return c;
}

static void SCR_DiagUpdateIssue (qboolean now_bad, qboolean *was_bad, double *bad_start, double *bad_total, double *last_bad_duration, double *last_recovery_time)
{
	if (now_bad)
	{
		if (!*was_bad)
		{
			*was_bad = true;
			*bad_start = realtime;
		}
		return;
	}

	if (*was_bad)
	{
		double duration = realtime - *bad_start;
		if (duration < 0.0)
			duration = 0.0;
		*bad_total += duration;
		*last_bad_duration = duration;
		*last_recovery_time = realtime;
		*was_bad = false;
	}
}

static void SCR_DiagUpdateIssueHistory (const scr_diag_class_t *cls)
{
	if (!scr_diag_state.active)
		return;

	SCR_DiagUpdateIssue(cls->net > 0, &scr_diag_state.net_bad, &scr_diag_state.net_bad_start,
		&scr_diag_state.net_bad_total, &scr_diag_state.net_last_bad_duration, &scr_diag_state.net_last_recovery_time);
	SCR_DiagUpdateIssue(cls->frame > 0, &scr_diag_state.frame_bad, &scr_diag_state.frame_bad_start,
		&scr_diag_state.frame_bad_total, &scr_diag_state.frame_last_bad_duration, &scr_diag_state.frame_last_recovery_time);
	SCR_DiagUpdateIssue(cls->input > 0, &scr_diag_state.input_bad, &scr_diag_state.input_bad_start,
		&scr_diag_state.input_bad_total, &scr_diag_state.input_last_bad_duration, &scr_diag_state.input_last_recovery_time);
}

static void SCR_DiagGetSnapshot (scr_diag_snapshot_t *snap)
{
	int cur_sent = 0;
	int cur_recv = 0;
	int cur_resent = 0;
	int cur_dropped = 0;
	unsigned long long cur_bytes_sent = 0;
	unsigned long long cur_bytes_recv = 0;

	Q_memset(snap, 0, sizeof(*snap));

	snap->window_elapsed = realtime - scr_diag_state.window_start;
	if (snap->window_elapsed < 0.001)
		snap->window_elapsed = 0.001;

	snap->pl_window = scr_diag_state.pl_window;
	snap->pl_total = cl.pltotal;
	snap->slow_count = scr_diag_state.slow_count;
	snap->freeze_count = scr_diag_state.freeze_count;
	snap->max_ms = scr_diag_state.max_frametime_ms;
	snap->avg_ms = scr_diag_state.frame_count > 0 ? (float)(scr_diag_state.sum_frametime_ms / scr_diag_state.frame_count) : 0.0f;
	snap->last_ms = scr_diag_state.last_frametime_ms;
	snap->net_gap_ms = q_max(0.0f, (float)((realtime - cl.last_received_message) * 1000.0));
	snap->sv_dt_ms = (cl.mtime[0] >= cl.mtime[1]) ? (float)((cl.mtime[0] - cl.mtime[1]) * 1000.0) : 0.0f;
	snap->render_ms = (float)scr_diag_render_ms;
	snap->input_age_ms = q_max(0.0f, (float)((realtime - cl.last_input_time) * 1000.0));

	NET_GetPacketStats(&cur_sent, &cur_recv, &cur_resent, &cur_dropped, &cur_bytes_sent, &cur_bytes_recv,
		&snap->pkt_duplicates, &snap->pkt_short);

	snap->pkt_sent = cur_sent;
	snap->pkt_recv = cur_recv;
	snap->pkt_resent = cur_resent;
	snap->pkt_dropped = cur_dropped;
	snap->bytes_sent = cur_bytes_sent;
	snap->bytes_recv = cur_bytes_recv;

	// Connection diagnostics
	NET_GetConnectionDiag(cls.netcon,
		&snap->seq_send, &snap->seq_recv, &snap->seq_ack,
		NULL, NULL,
		&snap->can_send, &snap->reliable_queue_bytes,
		&snap->connect_time, &snap->last_msg_time, &snap->last_send_time,
		NULL);

	// Lerp fraction
	{
		double mtime_delta = cl.mtime[0] - cl.mtime[1];
		if (mtime_delta > 0.001)
		{
			snap->lerp_frac = (float)((cl.time - cl.mtime[1]) / mtime_delta);
			snap->extrapolating = (cl.time > cl.mtime[0]);
		}
		else
		{
			snap->lerp_frac = 0.0f;
			snap->extrapolating = false;
		}
	}

	// Server tick rate and jitter
	if (snap->sv_dt_ms > 0.001f)
		snap->sv_tick_hz = 1000.0f / snap->sv_dt_ms;

	// Jitter from ring buffer
	if (scr_diag_state.sv_dt_count > 1)
	{
		int n = q_min(scr_diag_state.sv_dt_count, 64);
		float sum = 0.0f, sum_sq = 0.0f, mean, variance;
		int j;
		for (j = 0; j < n; j++)
			sum += scr_diag_state.sv_dt_samples[j];
		mean = sum / n;
		for (j = 0; j < n; j++)
		{
			float d = scr_diag_state.sv_dt_samples[j] - mean;
			sum_sq += d * d;
		}
		variance = sum_sq / n;
		snap->sv_tick_jitter_ms = (float)sqrt((double)variance);
	}

	// Timeout remaining
	{
		extern cvar_t net_messagetimeout;
		if (snap->last_msg_time > 0.0)
			snap->timeout_remaining = (float)(net_messagetimeout.value - (net_time - snap->last_msg_time));
		else
			snap->timeout_remaining = (float)net_messagetimeout.value;
	}

	// Uptime
	if (snap->connect_time > 0.0)
		snap->conn_uptime_secs = (int)(net_time - snap->connect_time);

	// Average packet sizes
	if (cur_recv > 0 && cur_bytes_recv > 0)
		snap->avg_pkt_in = (float)((double)cur_bytes_recv / cur_recv);
	if (cur_sent > 0 && cur_bytes_sent > 0)
		snap->avg_pkt_out = (float)((double)cur_bytes_sent / cur_sent);

	// Net message peak
	snap->net_msg_peak = scr_diag_state.net_msg_peak;

	// Move ACK gap
	snap->move_ack_gap = cl.movemessages - cl.ackedmovemessages;
	if (snap->move_ack_gap < 0)
		snap->move_ack_gap = 0;

	// Reliable vs unreliable message counts
	{
		extern int messagesSent, unreliableMessagesSent;
		snap->msgs_reliable = messagesSent;
		snap->msgs_unreliable = unreliableMessagesSent;
	}

	if (!scr_diag_state.active)
	{
		snap->baselines_valid = false;
		return;
	}

	if (scr_diag_state.last_packets_sent == 0 && scr_diag_state.last_packets_received == 0)
	{
		scr_diag_state.last_packets_sent = cur_sent;
		scr_diag_state.last_packets_received = cur_recv;
		scr_diag_state.last_packets_resent = cur_resent;
		scr_diag_state.last_dropped = cur_dropped;
		scr_diag_state.last_bytes_sent = cur_bytes_sent;
		scr_diag_state.last_bytes_received = cur_bytes_recv;
		snap->baselines_valid = false;
		return;
	}

	if (cur_sent < scr_diag_state.last_packets_sent)
		scr_diag_state.last_packets_sent = cur_sent;
	if (cur_recv < scr_diag_state.last_packets_received)
		scr_diag_state.last_packets_received = cur_recv;
	if (cur_resent < scr_diag_state.last_packets_resent)
		scr_diag_state.last_packets_resent = cur_resent;
	if (cur_dropped < scr_diag_state.last_dropped)
		scr_diag_state.last_dropped = cur_dropped;
	if (cur_bytes_sent < scr_diag_state.last_bytes_sent)
		scr_diag_state.last_bytes_sent = cur_bytes_sent;
	if (cur_bytes_recv < scr_diag_state.last_bytes_received)
		scr_diag_state.last_bytes_received = cur_bytes_recv;

	{
		int delta_sent = cur_sent - scr_diag_state.last_packets_sent;
		int delta_recv = cur_recv - scr_diag_state.last_packets_received;
		int delta_resend = cur_resent - scr_diag_state.last_packets_resent;
		int delta_drop = cur_dropped - scr_diag_state.last_dropped;
		unsigned long long delta_bytes_sent = cur_bytes_sent - scr_diag_state.last_bytes_sent;
		unsigned long long delta_bytes_recv = cur_bytes_recv - scr_diag_state.last_bytes_received;
		int loss_total = delta_recv + delta_drop;

		snap->pps_out = (float)(delta_sent / snap->window_elapsed);
		snap->pps_in = (float)(delta_recv / snap->window_elapsed);
		snap->resend_pps = (float)(delta_resend / snap->window_elapsed);
		snap->kb_out = (float)((delta_bytes_sent / 1024.0) / snap->window_elapsed);
		snap->kb_in = (float)((delta_bytes_recv / 1024.0) / snap->window_elapsed);
		if (loss_total > 0)
			snap->loss_pct = (float)(delta_drop * 100.0 / loss_total);
	}

	snap->baselines_valid = true;
}

static void SCR_DiagBuildSummary (const scr_diag_snapshot_t *snap, const scr_diag_class_t *cls)
{
	int key;
	qboolean update;

	key = (cls->net << 4) | (cls->frame << 2) | cls->input;
	key |= (snap->pl_window > 0) ? (1 << 8) : 0;
	key |= (snap->freeze_count > 0) ? (1 << 9) : 0;
	key |= ((int)(snap->net_gap_ms / 100.0f) & 7) << 10;

	update = (key != scr_diag_summary.key) ||
		(scr_diag_summary.window_seq != scr_diag_state.window_seq) ||
		(realtime - scr_diag_summary.last_update > 1.0);

	if (!update)
		return;

	scr_diag_summary.key = key;
	scr_diag_summary.window_seq = scr_diag_state.window_seq;
	scr_diag_summary.last_update = realtime;
	scr_diag_summary.lines = 2;
	scr_diag_summary.line3[0] = '\0';

	if (cls->net == 0 && cls->frame == 0 && cls->input == 0)
	{
		q_snprintf(scr_diag_summary.line1, sizeof(scr_diag_summary.line1),
			"All clear. No notable loss or frame spikes in the last 5s.");
		q_snprintf(scr_diag_summary.line2, sizeof(scr_diag_summary.line2),
			"Render time and input sampling look stable.");
		return;
	}

	if (cls->net > 0 && cls->frame == 0)
	{
		q_snprintf(scr_diag_summary.line1, sizeof(scr_diag_summary.line1),
			"Network looks rough: gap %dms, loss %.1f%%, drops %d/5s.",
			(int)(snap->net_gap_ms + 0.5f), snap->loss_pct, snap->pl_window);
		q_snprintf(scr_diag_summary.line2, sizeof(scr_diag_summary.line2),
			"Frame times are stable; likely network-side jitter.");
	}
	else if (cls->frame > 0 && cls->net == 0)
	{
		q_snprintf(scr_diag_summary.line1, sizeof(scr_diag_summary.line1),
			"Frame pacing is the issue: %d freezes, max %dms, avg %dms.",
			snap->freeze_count, (int)(snap->max_ms + 0.5f), (int)(snap->avg_ms + 0.5f));
		q_snprintf(scr_diag_summary.line2, sizeof(scr_diag_summary.line2),
			"Network looks stable; focus on CPU/GPU load or drivers.");
	}
	else
	{
		q_snprintf(scr_diag_summary.line1, sizeof(scr_diag_summary.line1),
			"Both network and frame pacing show problems: gap %dms, loss %.1f%%, freezes %d.",
			(int)(snap->net_gap_ms + 0.5f), snap->loss_pct, snap->freeze_count);
		q_snprintf(scr_diag_summary.line2, sizeof(scr_diag_summary.line2),
			"Could be system load plus network jitter; try wired + reduce render load.");
	}

	if (cls->input > 0)
	{
		scr_diag_summary.lines = 3;
		q_snprintf(scr_diag_summary.line3, sizeof(scr_diag_summary.line3),
			"Input sampling age %dms suggests queueing; check frame pacing or vsync.",
			(int)(snap->input_age_ms + 0.5f));
	}
}

static char scr_diag_ext_summary[512];
static double scr_diag_ext_summary_time;
static qboolean scr_diag_ext_summary_warn;

static char scr_diag_conn_summary[512];
static double scr_diag_conn_summary_time;
static qboolean scr_diag_conn_summary_warn;

static void SCR_DiagBuildExtSummary (const scr_diag_snapshot_t *snap)
{
	char buf[512];

	/* throttle: only rebuild every 2 seconds */
	if (scr_diag_ext_summary[0] && realtime - scr_diag_ext_summary_time < 2.0)
		return;

	buf[0] = '\0';

	if (snap->loss_pct > 2.0f)
		q_strlcat(buf, va("Packet loss at %.0f%% is high; check for WiFi interference or congested route. ", snap->loss_pct), sizeof(buf));
	else if (snap->loss_pct > 0.5f)
		q_strlcat(buf, va("Minor packet loss (%.1f%%); usually not noticeable but worth watching. ", snap->loss_pct), sizeof(buf));

	if (snap->resend_pps > 2.0f)
		q_strlcat(buf, va("Resends at %.0f/s means the reliable channel is busy retransmitting. ", snap->resend_pps), sizeof(buf));

	if (snap->kb_in > 50.0f)
		q_strlcat(buf, va("Inbound bandwidth %.0f KB/s is heavy; complex scene or many players. ", snap->kb_in), sizeof(buf));
	else if (snap->kb_in < 1.0f && snap->baselines_valid)
		q_strlcat(buf, "Very low inbound traffic; server may be idle or connection stalled. ", sizeof(buf));

	scr_diag_ext_summary_warn = (buf[0] != '\0');
	if (buf[0] == '\0')
		q_strlcpy(buf, "Network rates and entity counts look normal. No concerns.", sizeof(buf));

	q_strlcpy(scr_diag_ext_summary, buf, sizeof(scr_diag_ext_summary));
	scr_diag_ext_summary_time = realtime;
}

static void SCR_DiagBuildConnSummary (const scr_diag_snapshot_t *snap)
{
	char buf[512];

	/* throttle: only rebuild every 2 seconds */
	if (scr_diag_conn_summary[0] && realtime - scr_diag_conn_summary_time < 2.0)
		return;

	buf[0] = '\0';

	if (snap->extrapolating)
		q_strlcat(buf, "Client is extrapolating past the last server frame; visible as jerky entity movement. ", sizeof(buf));
	else if (snap->lerp_frac > 0.9f)
		q_strlcat(buf, "Lerp fraction near 1.0 means the next server frame is due soon; tight but OK. ", sizeof(buf));

	if (snap->sv_tick_jitter_ms > 5.0f)
		q_strlcat(buf, va("Server tick jitter %.1fms is elevated; server may be under load or the route is uneven. ", snap->sv_tick_jitter_ms), sizeof(buf));

	if (snap->timeout_remaining < 30.0f)
		q_strlcat(buf, va("Only %.0fs until timeout disconnect; server hasn't sent data recently. ", snap->timeout_remaining), sizeof(buf));

	if (!snap->can_send && snap->seq_send > snap->seq_ack)
		q_strlcat(buf, "Reliable channel is blocked waiting for ACK; outbound messages are queued. ", sizeof(buf));

	if (snap->pkt_duplicates > 10)
		q_strlcat(buf, va("%d duplicate packets received; possibly a routing loop or server double-send. ", snap->pkt_duplicates), sizeof(buf));

	{
		float peak_pct = (float)snap->net_msg_peak / NET_MAXMESSAGE * 100.0f;
		if (peak_pct > 80.0f)
			q_strlcat(buf, va("Message buffer peaked at %.0f%% capacity; risk of overflow on busy frames. ", peak_pct), sizeof(buf));
		else if (peak_pct > 50.0f)
			q_strlcat(buf, va("Message buffer peaked at %.0f%%; moderate usage, watch on complex maps. ", peak_pct), sizeof(buf));
	}

	scr_diag_conn_summary_warn = (buf[0] != '\0');
	if (buf[0] == '\0')
		q_strlcpy(buf, "Connection looks healthy. Lerp, tick rate, and reliability all nominal.", sizeof(buf));

	q_strlcpy(scr_diag_conn_summary, buf, sizeof(scr_diag_conn_summary));
	scr_diag_conn_summary_time = realtime;
}

/*
 * SCR_DiagWriteReportTo -- write the full diagnostic report.
 * If f is non-NULL, write to file (strip ^m). If NULL, print to console (with ^m color).
 */
static void SCR_DiagOut (FILE *f, const char *fmt, ...) __attribute__((__format__(__printf__,2,3)));
static void SCR_DiagOut (FILE *f, const char *fmt, ...)
{
	va_list ap;
	char buf[1024];

	va_start(ap, fmt);
	q_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (!f)
	{
		Con_Printf("%s", buf);
	}
	else
	{
		/* strip ^m markers for plain text file output */
		const char *s = buf;
		while (*s)
		{
			if (s[0] == '^' && s[1] == 'm')
			{
				s += 2;
				continue;
			}
			fputc(*s, f);
			s++;
		}
	}
}
#define DIAG_OUT(...) SCR_DiagOut(f, __VA_ARGS__)
static void SCR_DiagWriteReportTo (FILE *f)
{
	scr_diag_snapshot_t snap;
	scr_diag_class_t cls;
	double active_time = 0.0;
	int finding = 0;
	int rec = 0;
	int severity = 0; /* 0=clean, 1=minor, 2=moderate, 3=critical */
	float peak_pct;
	double net_bad_pct = 0.0, frame_bad_pct = 0.0, input_bad_pct = 0.0;
	double net_total, frame_total, input_total;

	SCR_DiagGetSnapshot(&snap);
	cls = SCR_DiagClassify(&snap);
	(void)cls; /* used indirectly via severity */

	if (scr_diag_state.active)
	{
		active_time = realtime - scr_diag_state.diag_start_time;
		if (active_time < 0.0)
			active_time = 0.0;
	}

	/* compute bad-time percentages */
	net_total = scr_diag_state.net_bad_total;
	if (scr_diag_state.net_bad)
		net_total += realtime - scr_diag_state.net_bad_start;
	frame_total = scr_diag_state.frame_bad_total;
	if (scr_diag_state.frame_bad)
		frame_total += realtime - scr_diag_state.frame_bad_start;
	input_total = scr_diag_state.input_bad_total;
	if (scr_diag_state.input_bad)
		input_total += realtime - scr_diag_state.input_bad_start;
	if (active_time > 0.0)
	{
		net_bad_pct = net_total * 100.0 / active_time;
		frame_bad_pct = frame_total * 100.0 / active_time;
		input_bad_pct = input_total * 100.0 / active_time;
	}

	peak_pct = (float)snap.net_msg_peak / NET_MAXMESSAGE * 100.0f;

	DIAG_OUT("\n");
	DIAG_OUT("  ========================================\n");
	DIAG_OUT("   ^mQSS-M DIAGNOSTIC REPORT^m\n");
	if (active_time > 0.0)
	{
		int mins = (int)(active_time / 60.0);
		int secs = (int)active_time % 60;
		if (mins > 0)
			DIAG_OUT("   Session: ^m%dm %ds^m\n", mins, secs);
		else
			DIAG_OUT("   Session: ^m%ds^m\n", secs);
	}
	else
		DIAG_OUT("   Session: inactive (enable scr_diagnostics to record)\n");
	DIAG_OUT("  ========================================\n\n");

	/* PERFORMANCE */
	DIAG_OUT("  ^mPERFORMANCE^m\n");
	DIAG_OUT("  %-22s %.1f ms (%.0f fps)\n", "Avg frame time:",
		snap.avg_ms, (snap.avg_ms > 0.0f) ? (1000.0f / snap.avg_ms) : 0.0f);
	DIAG_OUT("  %-22s %.1f ms\n", "Worst frame:", snap.max_ms);
	DIAG_OUT("  %-22s %d          Freezes (>100ms): %d\n",
		"Slow frames (>16ms):", snap.slow_count, snap.freeze_count);
	DIAG_OUT("  %-22s %.1f ms\n", "Render time:", snap.render_ms);
	DIAG_OUT("  %-22s %.1f ms\n", "Input age:", snap.input_age_ms);
	DIAG_OUT("\n");

	/* NETWORK */
	DIAG_OUT("  ^mNETWORK^m\n");
	DIAG_OUT("  %-22s %.0f in/s, %.0f out/s\n", "Packet rates:", snap.pps_in, snap.pps_out);
	DIAG_OUT("  %-22s %.1f KB/s in, %.1f KB/s out\n", "Bandwidth:", snap.kb_in, snap.kb_out);
	DIAG_OUT("  %-22s %.1f%%       Resends: %.1f/s\n", "Packet loss:", snap.loss_pct, snap.resend_pps);
	{
		int total = snap.msgs_reliable + snap.msgs_unreliable;
		int rel_pct = (total > 0) ? (int)(snap.msgs_reliable * 100.0f / total) : 0;
		DIAG_OUT("  %-22s %d%% reliable, %d%% unreliable (%d / %d)\n",
			"Channel split:", rel_pct, 100 - rel_pct, snap.msgs_reliable, snap.msgs_unreliable);
	}
	DIAG_OUT("  %-22s %.0f b in, %.0f b out\n", "Avg packet size:", snap.avg_pkt_in, snap.avg_pkt_out);
	DIAG_OUT("  %-22s %d       Short: %d\n", "Duplicates:", snap.pkt_duplicates, snap.pkt_short);
	DIAG_OUT("\n");

	/* CONNECTION */
	DIAG_OUT("  ^mCONNECTION^m\n");
	DIAG_OUT("  %-22s %.0f hz (%.1f ms, jitter %.1f ms)\n", "Server tick:",
		snap.sv_tick_hz, snap.sv_dt_ms, snap.sv_tick_jitter_ms);
	DIAG_OUT("  %-22s snd %u  rcv %u  ack %u\n", "Sequences:",
		snap.seq_send, snap.seq_recv, snap.seq_ack);
	DIAG_OUT("  %-22s %u       Reliable queue: %d b    canSend: %s\n",
		"Inflight:", (snap.seq_send > snap.seq_ack) ? (snap.seq_send - snap.seq_ack) : 0,
		snap.reliable_queue_bytes, snap.can_send ? "Y" : "N");
	DIAG_OUT("  %-22s %d\n", "Move ACK gap:", snap.move_ack_gap);
	DIAG_OUT("  %-22s %.2f%s\n", "Lerp fraction:", snap.lerp_frac,
		snap.extrapolating ? " ^m(EXTRAPOLATING)^m" : "");
	DIAG_OUT("  %-22s %.0f s\n", "Timeout in:", snap.timeout_remaining);
	DIAG_OUT("  %-22s %dm %ds\n", "Uptime:", snap.conn_uptime_secs / 60, snap.conn_uptime_secs % 60);
	DIAG_OUT("  %-22s %d / %d (%.0f%%)\n", "Msg buffer peak:", snap.net_msg_peak, NET_MAXMESSAGE, peak_pct);
	DIAG_OUT("\n");

	/* STABILITY */
	if (active_time > 0.0)
	{
		DIAG_OUT("  ^mSTABILITY^m (over %.0fs)\n", active_time);
		DIAG_OUT("  %-22s %.1fs (%.1f%% of session)%s\n", "Net issues:",
			net_total, net_bad_pct,
			scr_diag_state.net_bad ? "  ^m** active now **^m" : "");
		DIAG_OUT("  %-22s %.1fs (%.1f%% of session)%s\n", "Frame issues:",
			frame_total, frame_bad_pct,
			scr_diag_state.frame_bad ? "  ^m** active now **^m" : "");
		DIAG_OUT("  %-22s %.1fs (%.1f%% of session)%s\n", "Input issues:",
			input_total, input_bad_pct,
			scr_diag_state.input_bad ? "  ^m** active now **^m" : "");
		DIAG_OUT("\n");
	}

	/* SVC PROFILE */
	{
		int svc_counts_d[128], svc_bytes_d[128], svc_fc, svc_fb;
		int sorted[129], sc = 0, j, k;
		CL_GetSVCProfile(svc_counts_d, svc_bytes_d, &svc_fc, &svc_fb);
		for (j = 0; j < 128; j++)
			if (svc_bytes_d[j] > 0)
				sorted[sc++] = j;
		if (svc_fb > 0)
			sorted[sc++] = 128;
		for (j = 1; j < sc; j++)
		{
			int tmp = sorted[j];
			int tb = (tmp == 128) ? svc_fb : svc_bytes_d[tmp];
			k = j - 1;
			while (k >= 0)
			{
				int kb = (sorted[k] == 128) ? svc_fb : svc_bytes_d[sorted[k]];
				if (kb >= tb) break;
				sorted[k + 1] = sorted[k]; k--;
			}
			sorted[k + 1] = tmp;
		}
		if (sc > 0)
		{
			DIAG_OUT("  ^mSVC PROFILE^m (top by bytes)\n");
			for (j = 0; j < sc && j < 10; j++)
			{
				int idx = sorted[j];
				const char *nm = (idx == 128) ? "fast_update" : ((idx < 128 && svc_strings[idx]) ? svc_strings[idx] : "unknown");
				int cnt = (idx == 128) ? svc_fc : svc_counts_d[idx];
				float kbb = (idx == 128) ? (svc_fb / 1024.0f) : (svc_bytes_d[idx] / 1024.0f);
				DIAG_OUT("  %-22s %6d  %7.1f K\n", nm, cnt, kbb);
			}
			DIAG_OUT("\n");
		}
	}

	/* FINDINGS */
	DIAG_OUT("  ^mFINDINGS^m\n");

	if (snap.freeze_count > 0)
	{
		DIAG_OUT("  %d. %d freeze(s) detected (>100ms). Worst frame: %.0fms.\n",
			++finding, snap.freeze_count, snap.max_ms);
		DIAG_OUT("     Severe frame stalls cause visible teleporting and input drops.\n");
		if (severity < 3) severity = 3;
	}
	else if (snap.slow_count > 3)
	{
		DIAG_OUT("  %d. %d slow frames (>16ms) in the sample window. Worst: %.0fms, avg: %.1fms.\n",
			++finding, snap.slow_count, snap.max_ms, snap.avg_ms);
		DIAG_OUT("     Frequent slow frames cause stuttery movement and inconsistent aim.\n");
		if (severity < 2) severity = 2;
	}
	else if (snap.slow_count > 0)
	{
		DIAG_OUT("  %d. %d slow frame(s) recorded, worst %.0fms. Likely transient.\n",
			++finding, snap.slow_count, snap.max_ms);
		if (severity < 1) severity = 1;
	}

	if (frame_bad_pct > 10.0 && active_time > 5.0)
	{
		DIAG_OUT("  %d. Frame issues present %.0f%% of session (%.1fs of %.0fs).\n",
			++finding, frame_bad_pct, frame_total, active_time);
		DIAG_OUT("     Sustained frame problems suggest a systemic bottleneck, not transient spikes.\n");
		if (severity < 2) severity = 2;
	}

	if (snap.input_age_ms > 16.0f)
	{
		DIAG_OUT("  %d. Input sampling age is %.0fms (>16ms). Input is being queued.\n",
			++finding, snap.input_age_ms);
		DIAG_OUT("     Mouse and keyboard actions are being processed later than they should be.\n");
		if (severity < 2) severity = 2;
	}

	if (snap.loss_pct > 5.0f)
	{
		DIAG_OUT("  %d. Packet loss at %.1f%% is severe.\n", ++finding, snap.loss_pct);
		DIAG_OUT("     Expect visible hitching, delayed damage registration, and ghost players.\n");
		if (severity < 3) severity = 3;
	}
	else if (snap.loss_pct > 2.0f)
	{
		DIAG_OUT("  %d. Packet loss at %.1f%% is elevated.\n", ++finding, snap.loss_pct);
		DIAG_OUT("     Noticeable as occasional position corrections and delayed hits.\n");
		if (severity < 2) severity = 2;
	}
	else if (snap.loss_pct > 0.5f)
	{
		DIAG_OUT("  %d. Minor packet loss (%.1f%%). Usually not perceptible but worth monitoring.\n",
			++finding, snap.loss_pct);
		if (severity < 1) severity = 1;
	}

	if (snap.net_gap_ms > 250.0f)
	{
		DIAG_OUT("  %d. Network gap is %.0fms. Server data is arriving in bursts.\n",
			++finding, snap.net_gap_ms);
		DIAG_OUT("     Entity movement will appear jerky regardless of client framerate.\n");
		if (severity < 3) severity = 3;
	}
	else if (snap.net_gap_ms > 100.0f)
	{
		DIAG_OUT("  %d. Network gap %.0fms is above ideal (<80ms). Possible route congestion.\n",
			++finding, snap.net_gap_ms);
		if (severity < 1) severity = 1;
	}

	if (snap.resend_pps > 5.0f)
	{
		DIAG_OUT("  %d. Reliable resends at %.0f/s. The reliable channel is struggling.\n",
			++finding, snap.resend_pps);
		DIAG_OUT("     Indicates repeated packet loss on reliable data (level changes, prints, etc).\n");
		if (severity < 2) severity = 2;
	}

	if (net_bad_pct > 10.0 && active_time > 5.0)
	{
		DIAG_OUT("  %d. Network issues present %.0f%% of session (%.1fs of %.0fs).\n",
			++finding, net_bad_pct, net_total, active_time);
		if (severity < 2) severity = 2;
	}

	if (snap.extrapolating)
	{
		DIAG_OUT("  %d. Client is extrapolating (lerp %.2f). No server frame to interpolate toward.\n",
			++finding, snap.lerp_frac);
		DIAG_OUT("     Entities will overshoot their positions until the next server update arrives.\n");
		if (severity < 2) severity = 2;
	}

	if (snap.sv_tick_jitter_ms > 8.0f)
	{
		DIAG_OUT("  %d. Server tick jitter is %.1fms (high). Server frame intervals are inconsistent.\n",
			++finding, snap.sv_tick_jitter_ms);
		DIAG_OUT("     Even with good ping, entity movement will appear uneven.\n");
		if (severity < 2) severity = 2;
	}
	else if (snap.sv_tick_jitter_ms > 4.0f)
	{
		DIAG_OUT("  %d. Server tick jitter at %.1fms is mildly elevated.\n",
			++finding, snap.sv_tick_jitter_ms);
		if (severity < 1) severity = 1;
	}

	if (snap.timeout_remaining < 10.0f && snap.timeout_remaining > 0.0f)
	{
		DIAG_OUT("  %d. Timeout imminent (%.0fs remaining). Connection may drop.\n",
			++finding, snap.timeout_remaining);
		if (severity < 3) severity = 3;
	}
	else if (snap.timeout_remaining < 30.0f && snap.timeout_remaining > 0.0f)
	{
		DIAG_OUT("  %d. Timeout is low (%.0fs). Server has not sent data recently.\n",
			++finding, snap.timeout_remaining);
		if (severity < 2) severity = 2;
	}

	if (snap.move_ack_gap > 8)
	{
		DIAG_OUT("  %d. Move ACK gap is %d. Server is far behind acknowledging client input.\n",
			++finding, snap.move_ack_gap);
		DIAG_OUT("     Movement prediction may feel disconnected from server state.\n");
		if (severity < 2) severity = 2;
	}
	else if (snap.move_ack_gap > 5)
	{
		DIAG_OUT("  %d. Move ACK gap at %d is mildly elevated.\n", ++finding, snap.move_ack_gap);
		if (severity < 1) severity = 1;
	}

	if (!snap.can_send && snap.reliable_queue_bytes > 0)
	{
		DIAG_OUT("  %d. Reliable channel blocked. %d bytes queued waiting for ACK.\n",
			++finding, snap.reliable_queue_bytes);
		DIAG_OUT("     Outbound reliable messages are stalled until the server acknowledges.\n");
		if (severity < 2) severity = 2;
	}

	if (peak_pct > 80.0f)
	{
		DIAG_OUT("  %d. Message buffer peaked at %.0f%% (%d/%d bytes). Risk of overflow.\n",
			++finding, peak_pct, snap.net_msg_peak, NET_MAXMESSAGE);
		DIAG_OUT("     A single large frame could exceed the buffer and crash the connection.\n");
		if (severity < 3) severity = 3;
	}
	else if (peak_pct > 50.0f)
	{
		DIAG_OUT("  %d. Message buffer peaked at %.0f%%. Moderate usage, watch on complex maps.\n",
			++finding, peak_pct);
		if (severity < 1) severity = 1;
	}

	if (snap.pkt_duplicates > 20)
	{
		DIAG_OUT("  %d. %d duplicate packets. Possible routing loop or ISP-level retransmission.\n",
			++finding, snap.pkt_duplicates);
		if (severity < 1) severity = 1;
	}

	if (finding == 0)
		DIAG_OUT("     No issues detected. All metrics within normal parameters.\n");

	DIAG_OUT("\n");

	/* RECOMMENDATIONS */
	DIAG_OUT("  ^mRECOMMENDATIONS^m\n");

	if (severity == 0)
	{
		DIAG_OUT("     No action needed. Connection and performance are healthy.\n");
		if (active_time < 30.0)
			DIAG_OUT("     Consider monitoring for a longer session to catch intermittent issues.\n");
	}
	else
	{
		if (snap.freeze_count > 0 || snap.slow_count > 3 || frame_bad_pct > 10.0)
		{
			DIAG_OUT("  %d. Frame pacing: ", ++rec);
			if (snap.freeze_count > 0)
				DIAG_OUT("Freezes suggest a major stall (disk I/O, GC, GPU sync).\n");
			else
				DIAG_OUT("Slow frames indicate CPU or GPU contention.\n");
			DIAG_OUT("     - Try ^mhost_maxfps^m at your monitor refresh rate to cap engine load.\n");
			DIAG_OUT("     - Disable vsync (^mvid_vsync 0^m) to rule out swap-chain stalls.\n");
			DIAG_OUT("     - Reduce visual load: ^mr_shadows 0^m, lower ^mr_waterquality^m.\n");
			DIAG_OUT("     - Check for background processes consuming CPU/GPU.\n");
		}

		if (snap.input_age_ms > 16.0f)
		{
			DIAG_OUT("  %d. Input latency: Input is queuing behind frame submission.\n", ++rec);
			DIAG_OUT("     - Ensure framerate is stable and above 60 fps.\n");
			DIAG_OUT("     - Disable vsync or use ^mvid_vsync 0^m to reduce input-to-display lag.\n");
		}

		if (snap.loss_pct > 2.0f || snap.net_gap_ms > 200.0f || snap.resend_pps > 5.0f)
		{
			DIAG_OUT("  %d. Network quality: ", ++rec);
			if (snap.loss_pct > 5.0f)
				DIAG_OUT("Severe loss usually indicates a bad route or WiFi interference.\n");
			else
				DIAG_OUT("Elevated loss/gap suggests congestion on the network path.\n");
			DIAG_OUT("     - Use a wired (ethernet) connection if currently on WiFi.\n");
			DIAG_OUT("     - Check for bandwidth-heavy applications (streaming, downloads).\n");
			DIAG_OUT("     - Try a different server to isolate local vs remote issues.\n");
			if (snap.resend_pps > 5.0f)
				DIAG_OUT("     - High resends indicate the reliable channel is congested.\n");
		}
		else if (snap.loss_pct > 0.5f)
		{
			DIAG_OUT("  %d. Minor packet loss: Likely not impacting gameplay, but monitor it.\n", ++rec);
			DIAG_OUT("     - If on WiFi, ensure good signal strength and no interference.\n");
		}

		if (snap.extrapolating)
		{
			DIAG_OUT("  %d. Extrapolation: Client has outrun the server's update stream.\n", ++rec);
			DIAG_OUT("     - Usually resolves when the next server tick arrives.\n");
			DIAG_OUT("     - If persistent, the server may be overloaded or the route is lossy.\n");
		}

		if (snap.sv_tick_jitter_ms > 4.0f)
		{
			DIAG_OUT("  %d. Server jitter: The server is not sending frames at a steady rate.\n", ++rec);
			DIAG_OUT("     - This is server-side; not fixable from the client.\n");
			DIAG_OUT("     - Try a different server or wait for server load to decrease.\n");
		}

		if (snap.timeout_remaining < 30.0f && snap.timeout_remaining > 0.0f)
		{
			DIAG_OUT("  %d. Timeout warning: Server has gone silent. Disconnect is imminent.\n", ++rec);
			DIAG_OUT("     - Check your internet connection.\n");
			DIAG_OUT("     - The server may have crashed or restarted.\n");
		}

		if (snap.move_ack_gap > 5)
		{
			DIAG_OUT("  %d. Move ACK gap: Server is behind on acknowledging client movement.\n", ++rec);
			DIAG_OUT("     - High latency (ping) is the usual cause.\n");
			DIAG_OUT("     - If persistent, server may be overloaded processing client commands.\n");
		}

		if (peak_pct > 50.0f)
		{
			DIAG_OUT("  %d. Buffer pressure: Message buffer usage is notable.\n", ++rec);
			DIAG_OUT("     - Complex maps with many entities produce larger server frames.\n");
			DIAG_OUT("     - If approaching 100%%, the connection will be dropped.\n");
		}

		if (rec == 0)
		{
			DIAG_OUT("     Minor issues only. No specific action required.\n");
			DIAG_OUT("     Continue monitoring to see if conditions worsen.\n");
		}
	}

	DIAG_OUT("\n");

	/* OVERALL STATUS */
	DIAG_OUT("  ^mSTATUS:^m ");
	switch (severity)
	{
		case 0: DIAG_OUT("^mCLEAN^m - No issues detected.\n"); break;
		case 1: DIAG_OUT("^mMINOR^m - Small anomalies, unlikely to affect gameplay.\n"); break;
		case 2: DIAG_OUT("^mMODERATE^m - Issues present that may affect gameplay quality.\n"); break;
		case 3: DIAG_OUT("^mCRITICAL^m - Serious problems detected. Action recommended.\n"); break;
	}

	if (!snap.baselines_valid)
		DIAG_OUT("  NOTE: Baselines not ready. Run scr_diagnostics for a few seconds before dumping.\n");

	DIAG_OUT("  ========================================\n\n");
}

/*
 * SCR_DiagWriteReport -- write the diagnostic report to diagnosticsdump.txt.
 * Called manually via 'diagnosticsdump' console command.
 */
static void SCR_DiagWriteReport (void)
{
	char name[MAX_OSPATH];
	FILE *f;

	q_snprintf(name, sizeof(name), "%s/diagnosticsdump.txt", com_gamedir);
	f = fopen(name, "w");
	if (!f)
	{
		Con_Printf("ERROR: couldn't write %s\n", name);
		return;
	}

	SCR_DiagWriteReportTo(f);
	fclose(f);
	Con_Printf("Diagnostic report written to %s\n", name);
}

static void SCR_DiagDump_f (void)
{
	SCR_DiagWriteReport();
}

static int SCR_DiagWrapText(const char *text, char out[][192], int max_lines, int max_chars)
{
	const char *p = text;
	int line = 0;
	int len = 0;

	for (line = 0; line < max_lines; line++)
		out[line][0] = '\0';

	line = 0;
	while (*p && line < max_lines)
	{
		const char *word;
		int wlen = 0;
		int needed;

		while (*p == ' ')
			p++;
		if (!*p)
			break;

		word = p;
		while (*p && *p != ' ')
		{
			p++;
			wlen++;
		}

		if (wlen > max_chars)
		{
			int pos = 0;
			if (len > 0)
			{
				line++;
				if (line >= max_lines)
					break;
				len = 0;
			}
			while (pos < wlen && line < max_lines)
			{
				int chunk = q_min(max_chars, wlen - pos);
				Q_memcpy(out[line], word + pos, chunk);
				out[line][chunk] = '\0';
				pos += chunk;
				if (pos < wlen)
				{
					line++;
					if (line >= max_lines)
						break;
				}
			}
			len = (line < max_lines) ? (int)strlen(out[line]) : 0;
			continue;
		}

		needed = (len > 0 ? 1 : 0) + wlen;
		if (len + needed > max_chars)
		{
			line++;
			if (line >= max_lines)
				break;
			len = 0;
		}

		if (len > 0)
		{
			out[line][len++] = ' ';
			out[line][len] = '\0';
		}

		Q_memcpy(out[line] + len, word, wlen);
		len += wlen;
		out[line][len] = '\0';
	}

	if (line >= max_lines)
		return max_lines;
	return (out[line][0] ? line + 1 : line);
}
static void SCR_DrawDiagnostics (void)
{
	static qboolean colors_inited = false;
	static plcolour_t col_bg;
	static plcolour_t col_header;
	static plcolour_t col_accent;
	static plcolour_t col_text;
	static plcolour_t col_green;
	static plcolour_t col_yellow;
	static plcolour_t col_red;
	char line[64];

	if (scr_diagnostics.value <= 0)
	{
		if (scr_diag_state.active)
		{
			SCR_DiagUpdateIssue(false, &scr_diag_state.net_bad, &scr_diag_state.net_bad_start,
				&scr_diag_state.net_bad_total, &scr_diag_state.net_last_bad_duration, &scr_diag_state.net_last_recovery_time);
			SCR_DiagUpdateIssue(false, &scr_diag_state.frame_bad, &scr_diag_state.frame_bad_start,
				&scr_diag_state.frame_bad_total, &scr_diag_state.frame_last_bad_duration, &scr_diag_state.frame_last_recovery_time);
			SCR_DiagUpdateIssue(false, &scr_diag_state.input_bad, &scr_diag_state.input_bad_start,
				&scr_diag_state.input_bad_total, &scr_diag_state.input_last_bad_duration, &scr_diag_state.input_last_recovery_time);
			SCR_DiagWriteReportTo(NULL); /* print to console on disable */
		}
		scr_diag_state.active = false;
		scr_diag_state.last_realtime = realtime;
		return;
	}

	if (!colors_inited)
	{
		colors_inited = true;
		col_bg = CL_PLColours_Parse("0x000000");
		col_header = CL_PLColours_Parse("0x1a1a1a");
		col_accent = CL_PLColours_Parse("0x2b6cb0");
		col_text = CL_PLColours_Parse("0xffffff");
		col_green = CL_PLColours_Parse("0x2ecc71");
		col_yellow = CL_PLColours_Parse("0xf1c40f");
		col_red = CL_PLColours_Parse("0xe74c3c");
	}

	if (!scr_diag_state.active)
	{
		scr_diag_state.active = true;
		scr_diag_state.window_start = realtime;
		scr_diag_state.last_realtime = realtime;
		scr_diag_state.sum_frametime_ms = 0.0;
		scr_diag_state.frame_count = 0;
		scr_diag_state.window_seq++;
		scr_diag_state.last_packets_sent = 0;
		scr_diag_state.last_packets_received = 0;
		scr_diag_state.last_packets_resent = 0;
		scr_diag_state.last_dropped = 0;
		scr_diag_state.last_bytes_sent = 0;
		scr_diag_state.last_bytes_received = 0;
		scr_diag_state.last_pltotal = cl.pltotal;
		scr_diag_state.pl_window = 0;
		scr_diag_state.slow_count = 0;
		scr_diag_state.freeze_count = 0;
		scr_diag_state.max_frametime_ms = 0.0f;
		scr_diag_state.last_frametime_ms = 0.0f;
		scr_diag_state.diag_start_time = realtime;
		scr_diag_state.net_bad = false;
		scr_diag_state.net_bad_start = 0.0;
		scr_diag_state.net_bad_total = 0.0;
		scr_diag_state.net_last_bad_duration = 0.0;
		scr_diag_state.net_last_recovery_time = 0.0;
		scr_diag_state.frame_bad = false;
		scr_diag_state.frame_bad_start = 0.0;
		scr_diag_state.frame_bad_total = 0.0;
		scr_diag_state.frame_last_bad_duration = 0.0;
		scr_diag_state.frame_last_recovery_time = 0.0;
		scr_diag_state.input_bad = false;
		scr_diag_state.input_bad_start = 0.0;
		scr_diag_state.input_bad_total = 0.0;
		scr_diag_state.input_last_bad_duration = 0.0;
		scr_diag_state.input_last_recovery_time = 0.0;
		scr_diag_state.sv_dt_head = 0;
		scr_diag_state.sv_dt_count = 0;
		scr_diag_state.sv_dt_last_mtime0 = 0.0;
		scr_diag_state.net_msg_peak = 0;
		scr_diag_ext_summary[0] = '\0';
		scr_diag_conn_summary[0] = '\0';
	}

	{
		double raw = realtime - scr_diag_state.last_realtime;
		if (raw < 0)
			raw = 0;
		scr_diag_state.last_realtime = realtime;
		scr_diag_state.last_frametime_ms = (float)(raw * 1000.0);
	}

	if (realtime - scr_diag_state.window_start >= DIAG_WINDOW_SECS)
	{
		scr_diag_state.window_start = realtime;
		scr_diag_state.sum_frametime_ms = 0.0;
		scr_diag_state.frame_count = 0;
		scr_diag_state.window_seq++;
		scr_diag_state.last_packets_sent = 0;
		scr_diag_state.last_packets_received = 0;
		scr_diag_state.last_packets_resent = 0;
		scr_diag_state.last_dropped = 0;
		scr_diag_state.last_bytes_sent = 0;
		scr_diag_state.last_bytes_received = 0;
		scr_diag_state.last_pltotal = cl.pltotal;
		scr_diag_state.pl_window = 0;
		scr_diag_state.slow_count = 0;
		scr_diag_state.freeze_count = 0;
		scr_diag_state.max_frametime_ms = 0.0f;
	}

	scr_diag_state.sum_frametime_ms += scr_diag_state.last_frametime_ms;
	scr_diag_state.frame_count++;

	if (scr_diag_state.last_frametime_ms > DIAG_FREEZE_MS)
		scr_diag_state.freeze_count++;
	else if (scr_diag_state.last_frametime_ms > DIAG_SLOW_MS)
		scr_diag_state.slow_count++;

	if (scr_diag_state.last_frametime_ms > scr_diag_state.max_frametime_ms)
		scr_diag_state.max_frametime_ms = scr_diag_state.last_frametime_ms;

	if (cl.pltotal < scr_diag_state.last_pltotal)
		scr_diag_state.last_pltotal = cl.pltotal;
	scr_diag_state.pl_window = cl.pltotal - scr_diag_state.last_pltotal;

	// Track sv_dt jitter ring buffer when mtime changes
	if (cl.mtime[0] != scr_diag_state.sv_dt_last_mtime0 && cl.mtime[0] > cl.mtime[1])
	{
		float dt = (float)((cl.mtime[0] - cl.mtime[1]) * 1000.0);
		scr_diag_state.sv_dt_samples[scr_diag_state.sv_dt_head] = dt;
		scr_diag_state.sv_dt_head = (scr_diag_state.sv_dt_head + 1) & 63;
		if (scr_diag_state.sv_dt_count < 64)
			scr_diag_state.sv_dt_count++;
		scr_diag_state.sv_dt_last_mtime0 = cl.mtime[0];
	}

	// Track net_message peak
	if (net_message.cursize > scr_diag_state.net_msg_peak)
		scr_diag_state.net_msg_peak = net_message.cursize;

	{
		plcolour_t net_color = col_green;
		plcolour_t frame_color = col_green;
		scr_diag_snapshot_t snap;
		scr_diag_class_t cls;
		SCR_DiagGetSnapshot(&snap);
		cls = SCR_DiagClassify(&snap);
		SCR_DiagUpdateIssueHistory(&cls);
		SCR_DiagBuildSummary(&snap, &cls);

		const int panel_x = 8;
		const int panel_w = 310;
		const int line_h = 8;
		const int pad = 4;
		const int data_lines = 4;
		const int net_lines = 14;
		const int svc_lines = 9;
		const int panel_h = pad * 2 + (data_lines + 1) * line_h;
		const int header_h = pad + line_h;
		const int indicator_size = 6;
		const int max_summary_chars = (panel_w - 16) / 8;
		const int margin = 72;
		float diag_scale = (scr_conscale.value > 1.0f) ? (scr_conscale.value - 1.0f) : 1.0f;
		float view_h;
		char summary_text[512];
		char wrapped[3][192];
		int summary_lines = 0;
		int summary_h = 0;
		int net_h = 0;
		int svc_h = 0;
		int net_summary_lines = 0;
		int net_summary_h = 0;
		char net_wrapped[3][192];
		int total_h;
		int panel_y;
		int text_x;
		int header_y;
		int line1_y;
		int line2_y;
		int line3_y;
		int line4_y;
		int indicator_x;
		int summary_y;

		if (snap.net_gap_ms > 250.0f || scr_diag_state.pl_window >= 3)
			net_color = col_red;
		else if (snap.net_gap_ms > 100.0f || scr_diag_state.pl_window > 0)
			net_color = col_yellow;

		if (scr_diag_state.freeze_count > 0 || scr_diag_state.last_frametime_ms > DIAG_FREEZE_MS)
			frame_color = col_red;
		else if (scr_diag_state.slow_count > 0 || scr_diag_state.last_frametime_ms > DIAG_SLOW_MS)
			frame_color = col_yellow;

		summary_text[0] = '\0';
		if (scr_diag_summary.lines >= 1 && scr_diag_summary.line1[0])
			q_strlcat(summary_text, scr_diag_summary.line1, sizeof(summary_text));
		if (scr_diag_summary.lines >= 2 && scr_diag_summary.line2[0])
		{
			if (summary_text[0])
				q_strlcat(summary_text, " ", sizeof(summary_text));
			q_strlcat(summary_text, scr_diag_summary.line2, sizeof(summary_text));
		}
		if (scr_diag_summary.lines >= 3 && scr_diag_summary.line3[0])
		{
			if (summary_text[0])
				q_strlcat(summary_text, " ", sizeof(summary_text));
			q_strlcat(summary_text, scr_diag_summary.line3, sizeof(summary_text));
		}

		if (summary_text[0])
			summary_lines = SCR_DiagWrapText(summary_text, wrapped, countof(wrapped), max_summary_chars);

		summary_h = summary_lines * line_h;

		if (scr_diagnostics.value >= 2)
		{
			net_h = pad * 2 + (net_lines + 1) * line_h;
			SCR_DiagBuildExtSummary(&snap);
			SCR_DiagBuildConnSummary(&snap);
			/* combine both summaries into one block */
			{
				char net_summary_buf[512];
				net_summary_buf[0] = '\0';
				if (scr_diag_ext_summary_warn && scr_diag_ext_summary[0])
				{
					q_strlcat(net_summary_buf, scr_diag_ext_summary, sizeof(net_summary_buf));
					if (scr_diag_conn_summary_warn && scr_diag_conn_summary[0])
					{
						q_strlcat(net_summary_buf, " ", sizeof(net_summary_buf));
						q_strlcat(net_summary_buf, scr_diag_conn_summary, sizeof(net_summary_buf));
					}
				}
				else if (scr_diag_conn_summary_warn && scr_diag_conn_summary[0])
					q_strlcat(net_summary_buf, scr_diag_conn_summary, sizeof(net_summary_buf));
				else if (scr_diag_ext_summary[0])
					q_strlcat(net_summary_buf, scr_diag_ext_summary, sizeof(net_summary_buf));
				else if (scr_diag_conn_summary[0])
					q_strlcat(net_summary_buf, scr_diag_conn_summary, sizeof(net_summary_buf));
				if (net_summary_buf[0])
					net_summary_lines = SCR_DiagWrapText(net_summary_buf, net_wrapped, countof(net_wrapped), max_summary_chars);
			}
			net_summary_h = net_summary_lines * line_h;
		}
		if (scr_diagnostics.value >= 3)
			svc_h = pad * 2 + (svc_lines + 1) * line_h;

		total_h = panel_h + 4 + summary_h;
		if (scr_diagnostics.value >= 2)
			total_h += 6 + net_h + 4 + net_summary_h;
		if (scr_diagnostics.value >= 3)
			total_h += 6 + svc_h;

		if (diag_scale <= 0.0f)
			diag_scale = 1.0f;
		view_h = (float)glheight / diag_scale;

		panel_y = (int)(view_h - total_h - margin);
		if (panel_y < 0)
			panel_y = 0;

		text_x = panel_x + 8;
		header_y = panel_y + 2;
		line1_y = panel_y + header_h;
		line2_y = line1_y + line_h;
		line3_y = line2_y + line_h;
		line4_y = line3_y + line_h;
		indicator_x = panel_x + panel_w - indicator_size - 6;
		summary_y = panel_y + panel_h + 4;

		GL_SetCanvas (CANVAS_DEFAULT);
		glPushMatrix();
		glScalef(diag_scale, diag_scale, 1.0f);

		Draw_FillPlayer(panel_x, panel_y, panel_w, panel_h, col_bg, 0.55f);
		Draw_FillPlayer(panel_x, panel_y, panel_w, header_h, col_header, 0.85f);
		Draw_FillPlayer(panel_x, panel_y, 3, panel_h, col_accent, 0.9f);
		Draw_FillPlayer(panel_x + 3, panel_y + header_h, panel_w - 3, 1, col_header, 0.25f);

		Draw_StringRGBA(text_x, header_y, (scr_diagnostics.value >= 4) ? "DIAGNOSTICS (GPU)" : "DIAGNOSTICS", col_text, 1.0f);

		q_snprintf(line, sizeof(line), "PL(5s): %d/%d  GAP: %dms",
			scr_diag_state.pl_window, cl.pltotal, (int)(snap.net_gap_ms + 0.5f));
		Draw_StringRGBA(text_x, line1_y, line, col_text, 0.95f);

		q_snprintf(line, sizeof(line), "SV: %dms  REN: %dms  IN: %dms",
			(int)(snap.sv_dt_ms + 0.5f), (int)(snap.render_ms + 0.5f), (int)(snap.input_age_ms + 0.5f));
		Draw_StringRGBA(text_x, line2_y, line, col_text, 0.95f);

		q_snprintf(line, sizeof(line), "SLOW: %d  FREEZE: %d", scr_diag_state.slow_count, scr_diag_state.freeze_count);
		Draw_StringRGBA(text_x, line3_y, line, col_text, 0.95f);

		q_snprintf(line, sizeof(line), "MAX: %dms  AVG: %dms  LAST: %dms",
			(int)(scr_diag_state.max_frametime_ms + 0.5f),
			(int)(snap.avg_ms + 0.5f),
			(int)(scr_diag_state.last_frametime_ms + 0.5f));
		Draw_StringRGBA(text_x, line4_y, line, col_text, 0.90f);

		Draw_FillPlayer(indicator_x, line1_y + 1, indicator_size, indicator_size, net_color, 0.9f);
		Draw_FillPlayer(indicator_x, line3_y + 1, indicator_size, indicator_size, frame_color, 0.9f);

		if (summary_lines > 0)
		{
			int i;
			plcolour_t summary_color = (cls.net || cls.frame || cls.input) ? col_yellow : col_text;
			for (i = 0; i < summary_lines; i++)
				Draw_StringRGBA(panel_x + 8, summary_y + i * line_h, wrapped[i], i == 0 ? summary_color : col_text, 0.85f);
		}

		// Level 2: NETWORK panel (merged details + connection)
		if (scr_diagnostics.value >= 2)
		{
			const int net_y = summary_y + summary_h + 6;
			const int net_header_h = pad + line_h;
			const int net_text_x = text_x;
			const int net_header_y = net_y + 2;
			int ny = net_y + net_header_h;
			const int net_ind_x = panel_x + panel_w - indicator_size - 6;
			float peak_pct;
			plcolour_t gap_ind, inflight_ind, lerp_ind, timeout_ind, peak_ind;
			int gap_ny, inflight_ny, lerp_ny, timeout_ny, peak_ny;
			qboolean net_summary_warn = (scr_diag_ext_summary_warn || scr_diag_conn_summary_warn);

			Draw_FillPlayer(panel_x, net_y, panel_w, net_h, col_bg, 0.55f);
			Draw_FillPlayer(panel_x, net_y, panel_w, net_header_h, col_header, 0.85f);
			Draw_FillPlayer(panel_x, net_y, 3, net_h, col_accent, 0.9f);
			Draw_FillPlayer(panel_x + 3, net_y + net_header_h, panel_w - 3, 1, col_header, 0.25f);

			Draw_StringRGBA(net_text_x, net_header_y, "NETWORK", col_text, 1.0f);

			// -- Rates --
			q_snprintf(line, sizeof(line), "NET IN : %5.1f pps %6.1f KB/s", snap.pps_in, snap.kb_in);
			Draw_StringRGBA(net_text_x, ny, line, col_text, 0.9f);
			ny += line_h;

			q_snprintf(line, sizeof(line), "NET OUT: %5.1f pps %6.1f KB/s", snap.pps_out, snap.kb_out);
			Draw_StringRGBA(net_text_x, ny, line, col_text, 0.9f);
			ny += line_h;

			q_snprintf(line, sizeof(line), "LOSS: %3.0f%%  RESEND: %5.1f pps", snap.loss_pct, snap.resend_pps);
			Draw_StringRGBA(net_text_x, ny, line, col_text, 0.9f);
			ny += line_h;

			{
				int total = snap.msgs_reliable + snap.msgs_unreliable;
				int rel_pct = (total > 0) ? (int)(snap.msgs_reliable * 100.0f / total) : 0;
				q_snprintf(line, sizeof(line), "CHAN: rel %d%%  unrel %d%%", rel_pct, 100 - rel_pct);
			}
			Draw_StringRGBA(net_text_x, ny, line, col_text, 0.9f);
			ny += line_h;

			// -- Connection --
			q_snprintf(line, sizeof(line), "SEQ: snd %u rcv %u ack %u",
				snap.seq_send, snap.seq_recv, snap.seq_ack);
			Draw_StringRGBA(net_text_x, ny, line, col_text, 0.9f);
			ny += line_h;

			// MOVE ACK GAP
			q_snprintf(line, sizeof(line), "MOVE ACK GAP: %d", snap.move_ack_gap);
			Draw_StringRGBA(net_text_x, ny, line, col_text, 0.9f);
			gap_ind = col_green;
			if (snap.move_ack_gap > 8) gap_ind = col_red;
			else if (snap.move_ack_gap > 5) gap_ind = col_yellow;
			gap_ny = ny;
			ny += line_h;

			// INFLIGHT / RQUEUE / canSend
			q_snprintf(line, sizeof(line), "INFLIGHT: %u  RQUEUE: %db  canSend: %c",
				(snap.seq_send > snap.seq_ack) ? (snap.seq_send - snap.seq_ack) : 0,
				snap.reliable_queue_bytes,
				snap.can_send ? 'Y' : 'N');
			Draw_StringRGBA(net_text_x, ny, line, col_text, 0.9f);
			inflight_ind = col_green;
			if (!snap.can_send && (snap.seq_send > snap.seq_ack)) inflight_ind = col_yellow;
			inflight_ny = ny;
			ny += line_h;

			// DUP / SHORT
			q_snprintf(line, sizeof(line), "DUP: %d  SHORT: %d",
				snap.pkt_duplicates, snap.pkt_short);
			Draw_StringRGBA(net_text_x, ny, line, col_text, 0.9f);
			ny += line_h;

			// LERP
			if (snap.extrapolating)
				q_snprintf(line, sizeof(line), "LERP: %.2f EXTRAP!", snap.lerp_frac);
			else
				q_snprintf(line, sizeof(line), "LERP: %.2f", snap.lerp_frac);
			Draw_StringRGBA(net_text_x, ny, line, col_text, 0.9f);
			lerp_ind = col_green;
			if (snap.extrapolating) lerp_ind = col_red;
			lerp_ny = ny;
			ny += line_h;

			// SV TICK
			q_snprintf(line, sizeof(line), "SV TICK: %.0fhz (%.1fms jitter %.1fms)",
				snap.sv_tick_hz, snap.sv_dt_ms, snap.sv_tick_jitter_ms);
			Draw_StringRGBA(net_text_x, ny, line, col_text, 0.9f);
			ny += line_h;

			// TIMEOUT
			q_snprintf(line, sizeof(line), "TIMEOUT: %.1fs", snap.timeout_remaining);
			Draw_StringRGBA(net_text_x, ny, line, col_text, 0.9f);
			timeout_ind = col_green;
			if (snap.timeout_remaining < 10.0f) timeout_ind = col_red;
			else if (snap.timeout_remaining < 30.0f) timeout_ind = col_yellow;
			timeout_ny = ny;
			ny += line_h;

			// UPTIME
			{
				int up_min = snap.conn_uptime_secs / 60;
				int up_sec = snap.conn_uptime_secs % 60;
				q_snprintf(line, sizeof(line), "UPTIME: %dm %ds", up_min, up_sec);
			}
			Draw_StringRGBA(net_text_x, ny, line, col_text, 0.9f);
			ny += line_h;

			// AVG PKT
			q_snprintf(line, sizeof(line), "AVG PKT: in %.0fb out %.0fb",
				snap.avg_pkt_in, snap.avg_pkt_out);
			Draw_StringRGBA(net_text_x, ny, line, col_text, 0.9f);
			ny += line_h;

			// MSG PEAK
			peak_pct = (float)snap.net_msg_peak / NET_MAXMESSAGE * 100.0f;
			q_snprintf(line, sizeof(line), "MSG PEAK: %d/%d (%.0f%%)",
				snap.net_msg_peak, NET_MAXMESSAGE, peak_pct);
			Draw_StringRGBA(net_text_x, ny, line, col_text, 0.9f);
			peak_ind = col_green;
			if (peak_pct > 80.0f) peak_ind = col_red;
			else if (peak_pct > 50.0f) peak_ind = col_yellow;
			peak_ny = ny;

			// Draw indicator squares
			Draw_FillPlayer(net_ind_x, gap_ny + 1, indicator_size, indicator_size, gap_ind, 0.9f);
			Draw_FillPlayer(net_ind_x, inflight_ny + 1, indicator_size, indicator_size, inflight_ind, 0.9f);
			Draw_FillPlayer(net_ind_x, lerp_ny + 1, indicator_size, indicator_size, lerp_ind, 0.9f);
			Draw_FillPlayer(net_ind_x, timeout_ny + 1, indicator_size, indicator_size, timeout_ind, 0.9f);
			Draw_FillPlayer(net_ind_x, peak_ny + 1, indicator_size, indicator_size, peak_ind, 0.9f);

			// NETWORK summary text
			if (net_summary_lines > 0)
			{
				int nsi;
				int net_summary_y = net_y + net_h + 4;
				for (nsi = 0; nsi < net_summary_lines; nsi++)
					Draw_StringRGBA(panel_x + 8, net_summary_y + nsi * line_h, net_wrapped[nsi], (nsi == 0 && net_summary_warn) ? col_yellow : col_text, 0.85f);
			}
		}

		// Level 3: SVC PROFILE panel
		if (scr_diagnostics.value >= 3)
		{
			int svc_y_base;
			const int svc_header_h = pad + line_h;
			const int svc_text_x = text_x;
			int sy;
			int svc_counts[128], svc_bytes_arr[128];
			int svc_fast_count, svc_fast_bytes;
			int sorted_idx[129]; // 128 svc + 1 for fast updates
			int sorted_count = 0;
			int j, k;

			// Calculate svc_y position (after NETWORK panel if present)
			if (scr_diagnostics.value >= 2)
				svc_y_base = summary_y + summary_h + 6 + net_h + 4 + net_summary_h + 6;
			else
				svc_y_base = summary_y + summary_h + 6;

			Draw_FillPlayer(panel_x, svc_y_base, panel_w, svc_h, col_bg, 0.55f);
			Draw_FillPlayer(panel_x, svc_y_base, panel_w, svc_header_h, col_header, 0.85f);
			Draw_FillPlayer(panel_x, svc_y_base, 3, svc_h, col_accent, 0.9f);
			Draw_FillPlayer(panel_x + 3, svc_y_base + svc_header_h, panel_w - 3, 1, col_header, 0.25f);

			Draw_StringRGBA(svc_text_x, svc_y_base + 2, "SVC PROFILE (top by bytes)", col_text, 1.0f);

			CL_GetSVCProfile(svc_counts, svc_bytes_arr, &svc_fast_count, &svc_fast_bytes);

			// Build sortable index: entries with nonzero bytes
			for (j = 0; j < 128; j++)
			{
				if (svc_bytes_arr[j] > 0)
					sorted_idx[sorted_count++] = j;
			}
			// Add fast updates as index 128
			if (svc_fast_bytes > 0)
				sorted_idx[sorted_count++] = 128;

			// Sort by bytes descending (simple insertion sort for small N)
			for (j = 1; j < sorted_count; j++)
			{
				int tmp = sorted_idx[j];
				int tmp_bytes = (tmp == 128) ? svc_fast_bytes : svc_bytes_arr[tmp];
				k = j - 1;
				while (k >= 0)
				{
					int kb = (sorted_idx[k] == 128) ? svc_fast_bytes : svc_bytes_arr[sorted_idx[k]];
					if (kb >= tmp_bytes)
						break;
					sorted_idx[k + 1] = sorted_idx[k];
					k--;
				}
				sorted_idx[k + 1] = tmp;
			}

			// Draw top 8
			sy = svc_y_base + svc_header_h;
			for (j = 0; j < 8 && j < sorted_count; j++)
			{
				int idx = sorted_idx[j];
				const char *name;
				int count;
				float kb;

				if (idx == 128)
				{
					name = "fast_update";
					count = svc_fast_count;
					kb = svc_fast_bytes / 1024.0f;
				}
				else
				{
					name = (idx < (int)countof(svc_strings) && svc_strings[idx]) ? svc_strings[idx] : "unknown";
					count = svc_counts[idx];
					kb = svc_bytes_arr[idx] / 1024.0f;
				}
				{
					char trunc_name[19];
					q_strlcpy(trunc_name, name, sizeof(trunc_name));
					q_snprintf(line, sizeof(line), "%-18s %5d %6.1fK", trunc_name, count, kb);
				}
				Draw_StringRGBA(svc_text_x, sy, line, col_text, 0.9f);
				sy += line_h;
			}
			// Fill remaining lines if fewer than 8
			for (; j < 8; j++)
				sy += line_h;
		}

		glPopMatrix();
	}
}

/*====================
SCR_DrawMatchClock    woods (Adapted from Sbar_DrawFrags from r00k) draw match clock upper right corner #matchhud
====================
*/

#define MATCHCLOCK_REF_W 1920.0f
#define MATCHCLOCK_REF_H 1080.0f

// Track current minute-digit count (1-3) for the numeric match clock (modes 1/2)
// Baseline alignment assumes 3 digits; other HUD elements can offset relative to this
static int scr_matchclock_minute_digits = 3;

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

				char buf2[10];
				const char* mode;
				mode = Info_GetKey(cl.serverinfo, "mode", buf2, sizeof(buf2));

				if (!q_strcasecmp(mode, "clanarena") || !q_strcasecmp(mode, "wipeout"))
					tl = 0;

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

		// Always update scr_matchclock_minute_digits based on the current clock string
		// so other HUD elements (like differentials) can align even when the clock
		// itself is hidden or drawn in a different mode.
		{
			char* p_clock = num;
			int minutes_for_digits = 0;

			while (*p_clock == ' ')
				p_clock++;

			if (*p_clock >= '0' && *p_clock <= '9')
			{
				minutes_for_digits = (*p_clock++ - '0');
				if (*p_clock >= '0' && *p_clock <= '9')
					minutes_for_digits = minutes_for_digits * 10 + (*p_clock++ - '0');
				if (*p_clock >= '0' && *p_clock <= '9')
					minutes_for_digits = minutes_for_digits * 10 + (*p_clock++ - '0');
			}

			scr_matchclock_minute_digits = (minutes_for_digits >= 100) ? 3 : ((minutes_for_digits >= 10) ? 2 : 1);
		}

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

			float  s       = CLAMP(0.05f, scr_matchclockscale.value, 8.0f);

			float  view_w  = scr_vrect.width  / s;
			float  view_h  = scr_vrect.height / s;

            float user_x_percent = CLAMP(0.0f, scr_matchclock_x.value, 100.0f);
            float user_y_percent = CLAMP(0.0f, scr_matchclock_y.value, 100.0f);

            float  base_x  = (user_x_percent / 100.0f) * view_w;
            float  base_y  = (user_y_percent / 100.0f) * view_h;

			if (sb_showscores == false && (cl.gametype == GAME_DEATHMATCH && cls.state == ca_connected)) // woods don't overlap crosshair with scoreboard
			{
				int scr_matchclock_int = (int)scr_matchclock.value; // get the integer part of scr_matchclock.value

				char buf3[10];
				const char* mode;
				mode = Info_GetKey(cl.serverinfo, "mode", buf3, sizeof(buf3));

				if (!q_strcasecmp(mode, "clanarena") || !q_strcasecmp(mode, "wipeout"))
					return;
				
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
						Draw_Pic_RGBA_Outline(base_x, base_y, sb_nums[color][hundreds], CL_PLColours_Parse("0xffffff"), 1.0f, border);
						x_offset = 24; // Move the offset for the tens place
					}
					if (minutes >= 10) {
						int tens = (minutes / 10) % 10;
						Draw_Pic_RGBA_Outline(base_x + x_offset, base_y, sb_nums[color][tens], CL_PLColours_Parse("0xffffff"), 1.0f, border);
						x_offset += 24; // Move the offset for the ones place
					}

					int ones_min = minutes % 10;
					Draw_Pic_RGBA_Outline(base_x + x_offset, base_y, sb_nums[color][ones_min], CL_PLColours_Parse("0xffffff"), 1.0f, border);

					qboolean red_colon = (scr_matchclock.value == 2 || ((minutes == 0 && seconds < 15 && seconds > 0) && teamscores));
					plcolour_t colon_color = CL_PLColours_Parse(red_colon ? "0xff0000" : "0xffffff");
					// Use base_x, base_y + offset
					Draw_Pic_RGBA_Outline(base_x + x_offset + 24, base_y, sb_colon, colon_color, 1.0f, border);

					int tens_sec = seconds / 10;
					int ones_sec = seconds % 10;

					Draw_Pic_RGBA_Outline(base_x + x_offset + 38, base_y, sb_nums[color][tens_sec], CL_PLColours_Parse("0xffffff"), 1.0f, border); // draw tens place of seconds
					Draw_Pic_RGBA_Outline(base_x + x_offset + 62, base_y, sb_nums[color][ones_sec], CL_PLColours_Parse("0xffffff"), 1.0f, border); // draw ones place of seconds
				}
				else if (scr_matchclock.value == 3)
				{
					if ((((minutes <= 0) && (seconds < 15) && (seconds > 0)) && teamscores) || cl.seconds >= 128) // color last 15 seconds or CRMOD condition
						M_Print(base_x, base_y, num);
					else
						Draw_String(base_x, base_y, num);
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
SCR_CalculateTeamScoresForDemo -- woods - calculate team scores for demo playback #matchhud
====================*/
void SCR_CalculateTeamScoresForDemo(int* team_colors, int* team_scores, int* num_teams, int* redteamplayers, int* blueteamplayers)
{
	int i, j, k, colors;
	scoreboard_t* s;

	// Initialize arrays
	memset(team_colors, 0, sizeof(int) * MAX_SCOREBOARD);
	memset(team_scores, 0, sizeof(int) * MAX_SCOREBOARD);
	*num_teams = 0;
	*redteamplayers = 0;
	*blueteamplayers = 0;

	Sbar_SortFrags();

	// First pass: identify all team colors
	for (i = 0; i < scoreboardlines; i++) {
		k = fragsort[i];
		s = &cl.scores[k];

		if (!s->name[0])
			continue;

		colors = s->pants.basic;

		// Skip white (color 0)
		if (colors == 0)
			continue;

		// Check if we've seen this team color before
		int found = 0;
		for (j = 0; j < *num_teams; j++) {
			if (team_colors[j] == colors) {
				found = 1;
				break;
			}
		}

		if (!found && *num_teams < MAX_SCOREBOARD) {
			team_colors[(*num_teams)++] = colors;
		}

		// Count players for each team color for CTF calculations
		if (colors == 4) // red team
			(*redteamplayers)++;
		else if (colors == 13) // blue team
			(*blueteamplayers)++;
	}

	// Second pass: sum up frags for each team
	for (i = 0; i < scoreboardlines; i++) {
		k = fragsort[i];
		s = &cl.scores[k];
		
		if (!s->name[0])
			continue;

		colors = s->pants.basic;

		// Skip white (color 0)
		if (colors == 0)
			continue;

		// Add this player's frags to their team's total
		for (j = 0; j < *num_teams; j++) {
			if (team_colors[j] == colors) {
				team_scores[j] += s->frags;
				break;
			}
		}
	}

	// Sort teams by score (bubble sort)
	for (i = 0; i < *num_teams - 1; i++) {
		for (j = 0; j < *num_teams - i - 1; j++) {
			if (team_scores[j] < team_scores[j + 1]) {
				// Swap scores
				int temp_score = team_scores[j];
				team_scores[j] = team_scores[j + 1];
				team_scores[j + 1] = temp_score;

				// Swap colors
				int temp_color = team_colors[j];
				team_colors[j] = team_colors[j + 1];
				team_colors[j + 1] = temp_color;
			}
		}
	}
}

/*====================
SCR_DrawTeamScores -- woods - draw the team scores in the upper right corner #matchhud
====================*/
int SCR_DrawTeamScores(int x, int y, int l, qboolean use_demo_calculation, int* team_colors, int* team_scores)
		{
	int i, k, colors, f;
	int top, bottom;
	char num[30];

	if (cl.teamcolor[0] || (use_demo_calculation && team_colors[0]))
				Draw_Fill(11, 1, 32, 18, 0, 0.3);  // rectangle for missing team

			for (i = 0; i < l; i++)
			{
		// Get team color and score based on calculation method
		if (use_demo_calculation) {
			colors = team_colors[i];
			f = team_scores[i];
		}
		else {
				k = fragsort[i];
					colors = cl.teamscores[k].colors;
					f = cl.teamscores[k].frags;
		}

		// Store for diff calculation
					cl.teamscore[i] = f;
					cl.teamcolor[i] = colors;

				// draw background
		if (use_demo_calculation) {
			top = colors << 4;
			bottom = colors << 4;
		}
		else {
					top = (colors & 15) << 4;
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

				y += 9;  // woods to position vertical
			}

	return y; // Return the updated y position
}

static void SCR_DrawPositiveDiffString(int x, int y, const char* str) // woods #goldtext
{
        int cx = x;
        const char* p;
        plcolour_t accent = Draw_GetConcharsAccentColor();

        for (p = str; *p; ++p)
        {
                int ch = *p;

                if (ch >= '0' && ch <= '9')
                        M_DrawCharacter(cx, y, ch - 30);
                else if (ch == ' ')
                        M_DrawCharacter(cx, y, ch);
                else
                        M_DrawCharacterRGBA(cx, y, ch, accent, 1.0f);

                cx += 8;
        }
}

static void SCR_DrawFFADifferential(void) // woods
{
	int player_index;
	scoreboard_t* player;
	int highest_score = 0;
	int second_highest = 0;
	int player_score = 0;
	qboolean found_highest = false;
	qboolean found_second = false;
	qboolean found_player = false;
	int leaders_at_highest = 0;
	char diff_str[16];

	if (cl.viewentity <= 0 || cl.viewentity > cl.maxclients)
		return;

	if (cl.intermission || qeintermission || crxintermission || scr_viewsize.value >= 120)
		return;

	player_index = cl.viewentity - 1;
	player = &cl.scores[player_index];

	if (!player->name[0] || player->spectator || player->frags == -99)
		return;

	Sbar_SortFrags();

	if (!scoreboardlines)
		return;

	for (int i = 0; i < scoreboardlines; ++i)
	{
		int idx = fragsort[i];
		scoreboard_t* entry = &cl.scores[idx];

		if (!entry->name[0])
			continue;

		if (entry->spectator || entry->frags == -99)
			continue;

		if (!found_highest) {
			highest_score = entry->frags;
			found_highest = true;
			leaders_at_highest = 1;
		}
		else {
			if (entry->frags == highest_score) {
				leaders_at_highest++;
			}
			else if (!found_second) {
				// first score strictly below the leader = runner-up
				second_highest = entry->frags;
				found_second = true;
			}
		}

		if (idx == player_index) {
			player_score = entry->frags;
			found_player = true;
			// don't break; we still need to count ties and find runner-up
		}
	}

	if (!found_highest)
		return;

	if (!found_player)
		player_score = player->frags;

	// If player is tied for the lead, show nothing
	if (player_score == highest_score && leaders_at_highest >= 2)
		return;

	// Sole leader: show +diff vs runner-up (if any)
	if (player_score == highest_score) {
		if (!found_second) // no runner-up exists (e.g., only one valid player)
			return;

		GL_SetCanvas(CANVAS_TOPRIGHT4);
		q_snprintf(diff_str, sizeof(diff_str), "+%d", player_score - second_highest);
		{
			int digit_adjust_px = (scr_matchclock_minute_digits - 1) * 24; // missing digits
			SCR_DrawPositiveDiffString(120 - (strlen(diff_str) << 3) - digit_adjust_px, 11, diff_str);
		}
		return;
}

	// Player is behind the leader: show negative diff vs the leader
	GL_SetCanvas(CANVAS_TOPRIGHT4);
	q_snprintf(diff_str, sizeof(diff_str), "%d", player_score - highest_score);
	{
		int digit_adjust_px = (scr_matchclock_minute_digits - 1) * 24; // missing digits
		M_Print(120 - (strlen(diff_str) << 3) - digit_adjust_px, 11, diff_str);
	}
}

/*====================
SCR_DrawTeamDifferential -- woods - draw the team differential display #capturediff #matchhud
====================*/
void SCR_DrawTeamDifferential(int y, qboolean use_demo_calculation, int redteamplayers, int blueteamplayers, int l)
			{
	int i, k, l2, ts1, ts2, tc1, tc2, diff;
	int totalteamplayers = 0, capturepoints = 0, capdiff = 0;
	char tcolor[12];
	char num[30];
	char buf[10];
	const char* val;
	qboolean is_ctf = false;
	scoreboard_t* s;

	if (cl.teamcolor[2]) // only for two colors
		return;

					ts1 = cl.teamscore[0]; // high score
					ts2 = cl.teamscore[1]; // low score
					diff = abs(ts1 - ts2); // +/= differential

					tc1 = cl.teamcolor[0]; // top score [color]
					tc2 = cl.teamcolor[1]; // bottom score [color]

	// Check if this is CTF mode
	is_ctf = (cl.modetype == 1);

	// Also check serverinfo for CTF by looking for the "red flag" key
	val = Info_GetKey(cl.serverinfo, "red flag", buf, sizeof(buf));
	if (val && *val) {  // If key exists and has any value
		is_ctf = true;
	}

	// lets get YOUR team color from scoreboard
	if (!use_demo_calculation) {
					Sbar_SortFrags();
					l2 = scoreboardlines;
		redteamplayers = 0;
		blueteamplayers = 0;

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
		}
	}
	else {
		// For demo playback, get the player's team color
		s = &cl.scores[cl.viewentity - 1];
		if (s->name[0]) {
			sprintf(tcolor, "%u", s->pants.basic);
		}
		// Note: redteamplayers and blueteamplayers were already counted above
	}

						if (redteamplayers == blueteamplayers) // equal teams? 3 vs 3 not 2 vs 1
							totalteamplayers = blueteamplayers;
						else
							totalteamplayers = 0;

					// woods, lets do some ctf math! #capturediff
					capturepoints = (totalteamplayers * 10) + 5; // capture 10 and +5 for cap
					if (diff != 0 && capturepoints != 0)
						capdiff = divide_round_up(diff, capturepoints); // how many ctf captures up or down

					GL_SetCanvas(CANVAS_TOPRIGHT4); // lets do some printing

					if ((ts1 == ts2) || (l < 2)) // don't show ties, l = # of teams
		return;

					else if ((atoi(tcolor) == tc1) || atoi(tcolor) == (tc1/17))// top score [color] is the same as your color
					{
		if (totalteamplayers && ((cl.modetype == 1) || is_ctf)) // equal teams and CTF
							snprintf(num, sizeof(num), "+%-i (%i)", diff, capdiff);
						else
							snprintf(num, sizeof(num), "+%-i", diff);

					SCR_DrawPositiveDiffString(120 - (strlen(num) << 3), 32, num);
					}

					else if ((atoi(tcolor) == tc2) || atoi(tcolor) == (tc2 / 17)) // bottom score [color] is the same as your color
					{
		if (totalteamplayers && ((cl.modetype == 1) || is_ctf)) // equal teams and CTF
							snprintf(num, sizeof(num), "-%-i (%i)", diff, capdiff);
						else
							snprintf(num, sizeof(num), "-%-i", diff);
						M_Print(120 - (strlen(num) << 3), 32 + 20, num);
					}				
				}

/*====================
SCR_DrawMatchScores   -- woods  (Adapted from Sbar_DrawFrags from r00k) -- draw match scores upper right corner #matchhud
====================*/
void SCR_DrawMatchScores(void)
{
	int team_colors[MAX_SCOREBOARD]; // For demo calculation
	int team_scores[MAX_SCOREBOARD]; // For demo calculation
	int num_teams = 0;               // For demo calculation
	int redteamplayers = 0, blueteamplayers = 0;
	int x, y, l;
	qboolean use_demo_calculation = false;
	int teamscores;
	char buf[10];
	char buf2[10];
	const char* uiplaymode;
	const char* siplaymode;

	if (scr_viewsize.value >= 130)
		return;

	// Check if we need to calculate team scores manually (for demos)
	if (cls.demoplayback && cl.teamscores && (!cl.teamscores[0].colors || !cl.teamscores[1].colors)) {
		use_demo_calculation = true;
			}

	uiplaymode = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "mode", buf, sizeof(buf)); // serverinfo
	siplaymode = Info_GetKey(cl.serverinfo, "playmode", buf2, sizeof(buf2)); // userinfo (qecrx)

	if (scr_match_hud.value && cl.gametype == GAME_DEATHMATCH)   // woods for console var off and on
	{
		if (!q_strcasecmp(uiplaymode, "ffa") || !q_strcasecmp(siplaymode, "ffa"))
		{
			SCR_DrawFFADifferential();
		}
	}

	// Only proceed if it's a team game
	teamscores = cl.teamgame;
	if (!teamscores)
		return;

	// For regular games, sort team frags
	if (!use_demo_calculation) {
		Sbar_SortTeamFrags();
		}
	// For demos, calculate team scores manually
	else {
		SCR_CalculateTeamScoresForDemo(team_colors, team_scores, &num_teams, &redteamplayers, &blueteamplayers);
	}

	// draw the text
	l = use_demo_calculation ?
		(num_teams <= 4 ? num_teams : 4) :
		(scoreboardlines <= 4 ? scoreboardlines : 4);

	x = 0;
	y = 0; // woods to position vertical

	if (cl.gametype == GAME_DEATHMATCH)
	{
		GL_SetCanvas(CANVAS_TOPRIGHT3);


		if (!q_strcasecmp(uiplaymode, "ffa"))
		return;

		if (cl.modetype == 3) // no teamscores for crx ra
			return;

		if (scr_match_hud.value)   // woods for console var off and on
		{
			y = SCR_DrawTeamScores(x, y, l, use_demo_calculation, team_colors, team_scores);
			SCR_DrawTeamDifferential(y, use_demo_calculation, redteamplayers, blueteamplayers, l);
		}
	}
}

// woods #eyemouse

static int obs_frags_x;
static int obs_frags_y;
static int obs_frags_height;
static qboolean obs_frags_active;  // Track if the frags list is currently being displayed	
void Sbar_DrawSubPicAlpha (int x, int y, qpic_t* pic, int ofsx, int ofsy, int w, int h, float alpha);

void getShortName(const char* fullName, char* shortName)
{
	// Copy up to the first 15 characters into shortName
	// Make sure to leave space for the null terminator
	size_t len = strlen(fullName);
	size_t copyLen = (len < 15) ? len : 15;

	memcpy(shortName, fullName, copyLen);
	shortName[copyLen] = '\0';  // Ensure null termination

	// Trim trailing spaces
	len = copyLen;
	while (len > 0 && isspace((unsigned char)shortName[len - 1])) {
		shortName[--len] = '\0';
	}
}

/*
=======================
SCR_ShowObsFrags -- added by woods #observerhud #eyemouse
=======================
*/

#define OBS_POWERUP_DURATION 30.5f

void SCR_ShowObsFrags(void)
{
	int	i, k, x, y, f;
	char	num[12];
	scoreboard_t* s;
	char	shortname[16]; // woods for dynamic scoreboard during match, don't show ready
	char buf[15];
	char buf2[15];
	const char* obs;
	const char* star_obs;
	int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);
	static qpic_t* weapon_icons = NULL;
	static qpic_t* sb_quad = NULL;
	static qpic_t* sb_pent = NULL;
	static qpic_t* sb_ring = NULL;
	static qpic_t* sb_key1 = NULL;
	static qpic_t* sb_key2 = NULL;
	static qpic_t* sb_sigil[4] = { NULL, NULL, NULL, NULL };

	if (cl.intermission || qeintermission || crxintermission)
		return;

	if (scr_viewsize.value >= 120)
		return;

	if (scr_diagnostics.value > 0) // woods #scr_diag
	{
		obs_frags_active = false;
		return;
	}

	if ((!cl.notobserver && scr_obsitems.value) || (cls.demoplayback && scr_obsitems.value))
	{
		if (COM_FileExists("gfx/ibar2.lmp", NULL))
			weapon_icons = Draw_CachePic("gfx/ibar2.lmp");

		sb_quad = Draw_PicFromWad("sb_quad");
		sb_pent = Draw_PicFromWad("sb_invuln");
		sb_ring = Draw_PicFromWad("sb_invis");
		sb_key1 = Draw_PicFromWad("sb_key1");
		sb_key2 = Draw_PicFromWad("sb_key2");

		sb_sigil[0] = Draw_PicFromWad("sb_sigil1");
		sb_sigil[1] = Draw_PicFromWad("sb_sigil2");
		sb_sigil[2] = Draw_PicFromWad("sb_sigil3");
		sb_sigil[3] = Draw_PicFromWad("sb_sigil4");
	}

	obs_frags_active = true;

	if ((cl.gametype == GAME_DEATHMATCH) && (cls.state == ca_connected))
	{
		obs = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "observer", buf, sizeof(buf));
		star_obs = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "*observer", buf2, sizeof(buf2));

		if ((!strcmp(cl.observer, "y") && (cl.modtype >= 2)) ||
			scr_showscores.value ||
			!strcmp(obs, "eyecam") || !strcmp(obs, "chase") || !strcmp(obs, "fly") || !strcmp(obs, "walk") ||
			!strcmp(star_obs, "eyecam") || !strcmp(star_obs, "chase") || !strcmp(star_obs, "fly") || !strcmp(star_obs, "walk"))
		{
			if (scr_scoreboard_teamsort.value && Sbar_ShouldSortByTeam()) // woods #teamscoreboard
			{
				Sbar_SortFrags_TeamOrder(true);
			}
			else
			{
				Sbar_SortFrags_Obs();
			}

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

			// Store coordinates for click detection
			obs_frags_x = x;
			obs_frags_y = y;
			obs_frags_height = scoreboardlines * 8;

			char qflbracket[2] = { 144, '\0' }; // woods  -- quake font left bracket
			char qfrbracket[2] = { 145, '\0' }; // woods  -- quake font right bracket

			for (i = 0; i < scoreboardlines; i++, y += -8) //johnfitz -- change y init, test, inc woods (reverse drawing order from bottom to top)
			{
				k = fragsort[i];
				s = &cl.scores[k];
				if (!s->name[0])
					continue;

				// colors
				Draw_FillPlayer(x, y + 1, 40, 4, s->shirt, 1);
				Draw_FillPlayer(x, y + 5, 40, 3, s->pants, 1);

				if (k == cl.viewentity - 1)
				{
					Draw_StringRGBA(x - 2, y, qflbracket, CL_PLColours_Parse("0xffffff"), 1);
					Draw_StringRGBA(x + 33, y, qfrbracket, CL_PLColours_Parse("0xffffff"), 1);
				}

				// number
				f = s->frags;
				q_snprintf(num, sizeof(num), "%3i", f);
				Draw_Character(x + 8, y, num[0]);
				Draw_Character(x + 16, y, num[1]);
				Draw_Character(x + 24, y, num[2]);

				// name
				char filtered_name[32];
				qboolean was_filtered = false;

				if (cl_contentfilter.value == 2) // woods #contentfilter
				{
					was_filtered = WordFilter_Check(s->name, filtered_name, sizeof(filtered_name));
					filtered_name[sizeof(filtered_name) - 1] = '\0';
				}

				// Get the short name and respect content filter setting
				if (cl_contentfilter.value == 2 && was_filtered) // woods #contentfilter
					getShortName(filtered_name, shortname);
				else
				getShortName(s->name, shortname);

				M_PrintWhite(x + 50, y, shortname);

				if ((!cl.notobserver && scr_obsitems.value) || (cls.demoplayback && scr_obsitems.value))
				{
					// Calculate name width for icon placement
					int nameWidth = strlen(shortname) * 8;
					int iconX = x + 50 + nameWidth + 4; // 4 pixels padding after name
					int iconSpacing = 10;

					// Check if player info is recent enough (within 5 seconds)
					if (s->tinfo.time > cl.time)
					{
						// Check for weapons in items
						qboolean hasRL = (s->tinfo.items & IT_ROCKET_LAUNCHER) != 0;
						qboolean hasLG = (s->tinfo.items & IT_LIGHTNING) != 0;

						if (weapon_icons)
						{
							if (hasRL)
							{
								// Draw rocket icon (3rd ammo type in ibar2.lmp)
								Sbar_DrawSubPicAlpha(iconX - 34, y - 24, weapon_icons, 3 + (2 * 48), 0, 42, 11, 1);
								iconX += iconSpacing; // Move to next icon position
							}

							if (hasLG)
							{
								// Draw cell icon (4th ammo type in ibar2.lmp)
								Sbar_DrawSubPicAlpha(iconX - 34, y - 24, weapon_icons, 3 + (3 * 48), 0, 42, 11, 1);
								iconX += iconSpacing; // Move to next icon position
							}
							iconX -= 2;
						}

						qboolean hasQuad = (s->tinfo.items & IT_QUAD) != 0;
						qboolean hasPent = (s->tinfo.items & IT_INVULNERABILITY) != 0;
						qboolean hasRing = (s->tinfo.items & IT_INVISIBILITY) != 0;
						qboolean hasSilverKey = (s->tinfo.items & IT_KEY1) != 0;
						qboolean hasGoldKey = (s->tinfo.items & IT_KEY2) != 0;
						qboolean hasMega = (s->tinfo.items & IT_SUPERHEALTH) != 0;

						qboolean hasSigil1 = (s->tinfo.items & (1 << 23)) != 0;
						qboolean hasSigil2 = (s->tinfo.items & (1 << 24)) != 0;
						qboolean hasSigil3 = (s->tinfo.items & (1 << 25)) != 0;
						qboolean hasSigil4 = (s->tinfo.items & (1 << 26)) != 0;

						float scale = 0.65; // Draw powerup/rune icons with scaling

						float quadAlpha = 1.0f;
						float pentAlpha = 1.0f;
						float ringAlpha = 1.0f;

						if (hasQuad && s->tinfo.quad_time > 0)
						{
							float elapsed = cl.time - s->tinfo.quad_time;
							if (elapsed <= OBS_POWERUP_DURATION)
							{
								quadAlpha = 0.3f + (1.0f - (elapsed / OBS_POWERUP_DURATION)) * 0.7f;
								if (quadAlpha > 1.0f) quadAlpha = 1.0f;
							}
							else
							{
								quadAlpha = 0.3f;
							}
						}

						if (hasPent && s->tinfo.pent_time > 0)
						{
							float elapsed = cl.time - s->tinfo.pent_time;
							if (elapsed <= OBS_POWERUP_DURATION)
							{
								pentAlpha = 0.3f + (1.0f - (elapsed / OBS_POWERUP_DURATION)) * 0.7f;
								if (pentAlpha > 1.0f) pentAlpha = 1.0f;
							}
							else
							{
								pentAlpha = 0.3f;
							}
						}

						if (hasRing && s->tinfo.ring_time > 0)
						{
							float elapsed = cl.time - s->tinfo.ring_time;
							if (elapsed <= OBS_POWERUP_DURATION)
							{
								ringAlpha = 0.3f + (1.0f - (elapsed / OBS_POWERUP_DURATION)) * 0.7f;
								if (ringAlpha > 1.0f) ringAlpha = 1.0f;
							}
							else
							{
								ringAlpha = 0.3f;
							}
						}

						if (hasQuad && sb_quad)
						{
							Draw_ScaledPicAlpha(iconX, y, sb_quad, scale, quadAlpha);
							iconX += iconSpacing;
						}

						if (hasPent && sb_pent)
						{
							Draw_ScaledPicAlpha(iconX, y, sb_pent, scale, pentAlpha);
							iconX += iconSpacing;
						}

						if (hasRing && sb_ring)
						{
							Draw_ScaledPicAlpha(iconX, y, sb_ring, scale, ringAlpha);
							iconX += iconSpacing;
						}

						if (hasMega)
						{
							float megaAlpha = 0.3f + (s->tinfo.health - 101) / 99.0f * 0.7f;
							if (megaAlpha > 1.0f) megaAlpha = 1.0f;

							Draw_CharacterRGBA(iconX + 1, y + 1, '+' + 128, CL_PLColours_Parse("0xffffff"), megaAlpha);
							iconX += iconSpacing;
						}

						if (hasSilverKey && sb_key1)
						{
							Draw_ScaledPicAlpha(iconX, y, sb_key1, scale, 1);
							iconX += iconSpacing;
						}

						if (hasGoldKey && sb_key2)
						{
							Draw_ScaledPicAlpha(iconX, y, sb_key2, scale, 1);
							iconX += iconSpacing;
						}

						if (hasSigil1 || hasSigil2 || hasSigil3 || hasSigil4)
							iconX += 1;

						// Draw runes
						if (hasSigil1 && sb_sigil[0])
						{
							Draw_ScaledPicAlpha(iconX, y, sb_sigil[0], scale, 1);
							iconX += iconSpacing;
						}

						if (hasSigil2 && sb_sigil[1])
						{
							Draw_ScaledPicAlpha(iconX, y, sb_sigil[1], scale, 1);
							iconX += iconSpacing;
						}

						if (hasSigil3 && sb_sigil[2])
						{
							Draw_ScaledPicAlpha(iconX, y, sb_sigil[2], scale, 1);
							iconX += iconSpacing;
						}

						if (hasSigil4 && sb_sigil[3])
						{
							Draw_ScaledPicAlpha(iconX, y, sb_sigil[3], scale, 1);
						}
					}
				}
			}
			obs_frags_active = true;
		}
	}
}

void Draw_GetScoreboardTransform (vrect_t* bounds, vrect_t* viewport) // woods #eyemouse
{
	int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);

	if (clampedSbar == 3)
	{
		bounds->x = 0;
		bounds->y = 0;
		bounds->width = 320;
		bounds->height = 300;

		viewport->x = glx;
		viewport->y = gly;
		viewport->width = glwidth;
		viewport->height = glheight;
	}
	else
	{
		bounds->x = 0;
		bounds->y = 0;
		bounds->width = 320;
		bounds->height = 200;

		viewport->x = glx;
		viewport->y = gly;
		viewport->width = glwidth;
		viewport->height = glheight;
		}
}

void IN_ObsFragsClick(int x, int y) // woods #eyemouse
{
	// Early return if observer frags aren't active
	if (!obs_frags_active)
		return;

	vrect_t bounds, viewport;
	Draw_GetScoreboardTransform(&bounds, &viewport);
	int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);

	float scale;
	int row = -1;
	int itemheight = 8; // each entry is 8 units tall

	// Calculate row based on sbar mode
	if (clampedSbar == 3)
	{
		scale = CLAMP(1.0, scr_sbarscale.value / 1.2, (float)glwidth / 320.0);
		x = (int)(x / scale);
		y = (int)((viewport.height - y) / scale);
		y = 300 - y;
		row = (182 - y) / itemheight;
	}
	else
	{
		scale = (float)glwidth / vid.conwidth;
		x = (int)(x / scale);
		y = (int)((viewport.height - y) / scale);
		y += 19;
		row = (y - 151) / itemheight;
	}

	// Validate row bounds
	if (row < 0 || row >= scoreboardlines)
	{
		Con_DPrintf("ObsFragsClick: Row %d is out of bounds (valid range: 0 to %d)\n",
			row, scoreboardlines - 1);
		return;
	}

	// Get the player corresponding to the computed row
	int playernum = fragsort[row];
	scoreboard_t* s = &cl.scores[playernum];

	// Skip empty entries
	if (!s->name[0])
	{
		Con_DPrintf("ObsFragsClick: Empty player entry at row %d\n", row);
		return;
	}
	// Create a shortened name (max 15 characters) and trim trailing spaces
	char shortname[16];
	char trimmed_name[16];
	q_snprintf(shortname, sizeof(shortname), "%.15s", s->name);

	// Trim trailing spaces
	q_strlcpy(trimmed_name, shortname, sizeof(trimmed_name));
	int len = strlen(trimmed_name);
	while (len > 0 && trimmed_name[len - 1] == ' ')
	{
		trimmed_name[len - 1] = '\0';
		len--;
	}

	// Calculate click boundaries
	int startx = obs_frags_x;
	int name_offset = 50;
	int name_width = strlen(trimmed_name) * 8;
	int total_width = name_offset + name_width;

	// Validate horizontal click position
	if (x < startx || x >= startx + total_width)
	{
		Con_DPrintf("ObsFragsClick: Click outside valid region (x=%d, valid=%d-%d)\n",
			x, startx, startx + total_width);
		return;
	}

	// Issue the track command
	char cmd[64];
	q_snprintf(cmd, sizeof(cmd), "cmd eyecam %d\n", playernum + 1);
	Cbuf_AddText(cmd);
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

	if (scr_viewsize.value >= 130)
		return;

	// woods lets get some info from server infokeys

	static char cached_redflag[10] = "";
	static char cached_blueflag[10] = "";
	static Uint32 last_cache_time = 0;
	Uint32 current_time = SDL_GetTicks();

	// Cache the values for 100 milliseconds to prevent blinking

	if (current_time - last_cache_time > 100 || cached_redflag[0] == '\0' || cached_blueflag[0] == '\0')
	{
	char buf[10];
		const char* redflag = Info_GetKey(cl.serverinfo, "red flag", buf, sizeof(buf));
		Q_strncpy(cached_redflag, redflag, sizeof(cached_redflag) - 1);
		cached_redflag[sizeof(cached_redflag) - 1] = '\0';

	char buf2[10];
		const char* blueflag = Info_GetKey(cl.serverinfo, "blue flag", buf2, sizeof(buf2));
		Q_strncpy(cached_blueflag, blueflag, sizeof(cached_blueflag) - 1);
		cached_blueflag[sizeof(cached_blueflag) - 1] = '\0';

		last_cache_time = current_time;
	}

	if (cached_blueflag[0] != '\0' && cached_redflag[0] != '\0') // is there a key on the server (newer version of crx)
	{
		Q_strncpy(cl.flagstatus, "n", sizeof(cl.flagstatus) - 1);
		cl.flagstatus[sizeof(cl.flagstatus) - 1] = '\0';

		// RED

		if (!strcmp(cached_redflag, "carried")) {
			Q_strncpy(cl.flagstatus, "r", sizeof(cl.flagstatus) - 1);
			cl.flagstatus[sizeof(cl.flagstatus) - 1] = '\0';
		}

		if (!strcmp(cached_redflag, "dropped")) {
			Q_strncpy(cl.flagstatus, "x", sizeof(cl.flagstatus) - 1);
			cl.flagstatus[sizeof(cl.flagstatus) - 1] = '\0';
		}

		// BLUE

		if (!strcmp(cached_blueflag, "carried")) {
			Q_strncpy(cl.flagstatus, "b", sizeof(cl.flagstatus) - 1);
			cl.flagstatus[sizeof(cl.flagstatus) - 1] = '\0';
		}

		if (!strcmp(cached_blueflag, "dropped")) {
			Q_strncpy(cl.flagstatus, "y", sizeof(cl.flagstatus) - 1);
			cl.flagstatus[sizeof(cl.flagstatus) - 1] = '\0';
		}

		// RED & BLUE

		if (!strcmp(cached_blueflag, "carried") && !strcmp(cached_redflag, "carried")) {
			Q_strncpy(cl.flagstatus, "p", sizeof(cl.flagstatus) - 1);
			cl.flagstatus[sizeof(cl.flagstatus) - 1] = '\0';
		}

		if (!strcmp(cached_blueflag, "dropped") && !strcmp(cached_redflag, "dropped")) {
			Q_strncpy(cl.flagstatus, "z", sizeof(cl.flagstatus) - 1);
			cl.flagstatus[sizeof(cl.flagstatus) - 1] = '\0';
		}

		if (!strcmp(cached_blueflag, "dropped") && !strcmp(cached_redflag, "carried")) {
			Q_strncpy(cl.flagstatus, "j", sizeof(cl.flagstatus) - 1);
			cl.flagstatus[sizeof(cl.flagstatus) - 1] = '\0';
		}

		if (!strcmp(cached_blueflag, "carried") && !strcmp(cached_redflag, "dropped")) {
			Q_strncpy(cl.flagstatus, "k", sizeof(cl.flagstatus) - 1);
			cl.flagstatus[sizeof(cl.flagstatus) - 1] = '\0';
		}
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

// woods #speed #speedometer

#define SPEED_UPDATE_INTERVAL 0.167f
static float last_speed = 0;
static float current_speed = 0;
static float speed_update_time = 0;

static float GetLocalVelocity(void)
{
	vec3_t vel;

	vel[0] = cl.velocity[0];
	vel[1] = cl.velocity[1];
	vel[2] = 0;

	return VectorLength(vel);
}

static float GetSpeedValue(void)
{
	if (!cl.notobserver &&
		cl.viewentity - 1 >= 0 && cl.viewentity - 1 < MAX_SCOREBOARD)
	{
		int new_speed = (int)cl.scores[cl.viewentity - 1].tinfo.speed;

		if (new_speed != 0)  // Check if speed data exists
		{
			// If we got a new speed update
			if (new_speed != current_speed)
			{
				last_speed = current_speed;
				current_speed = new_speed;
				speed_update_time = cl.time;
			}

			// Interpolate between last and current speed
			float frac = (cl.time - speed_update_time) / SPEED_UPDATE_INTERVAL;
			frac = CLAMP(0, frac, 1);
			return last_speed + (current_speed - last_speed) * frac;
		}
	}
	return GetLocalVelocity();
}

/*
==============
SCR_Speedometer -- woods #speedometer (https://github.com/matthewearl/quakespasm f4411f0 from joequake)
==============
*/
void SCR_Speedometer(void)
{
	float speed;
	int bad_jump = 0;
	static float maxspeed = 0, display_speed = -1;
	static double lastrealtime = 0;

	int	x, bg_color;
	float speedunits;
	char st[8];
	float alpha = 0.5;
	int y = scr_showspeed_y.value;

	GL_SetCanvas(CANVAS_SBAR2);

	if (lastrealtime > realtime)
	{
		lastrealtime = 0;
		display_speed = -1;
		maxspeed = 0;
	}

	speed = GetSpeedValue();

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
	if (cl.intermission || qeintermission || crxintermission || scr_viewsize.value >= 120)
		return;
	
	if (scr_showspeed.value > 1)
	{
		SCR_Speedometer();
		return;
	}
	
	char			st[64];
	int				x, y;
	int				clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);
	float			speed = 0;

	if (scr_viewsize.value > 110 || scr_viewsize.value >= 130)
		return;

	if (clampedSbar == 3)
	{
		GL_SetCanvas(CANVAS_BOTTOMLEFTQE);
		y = 177;
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

	if (scr_showspeed.value == 1 && (!cl.intermission || !qeintermission || !crxintermission))
	{
		speed = GetSpeedValue();

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
}

/*
===============
SCR_DrawMovementKeys -- woods #movementkeys (soruced from: https://github.com/j0zzz/JoeQuake/commit/bc56fea)
===============
*/
void SCR_DrawMovementKeys(void)
{
	if (!scr_movekeys.value || cl.intermission || qeintermission || crxintermission || scr_viewsize.value > 110)
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
===============
SCR_DrawGrenadeTimer -- woods #nadecount
===============
*/
void SCR_DrawGrenadeTimer(void)
{
	static qboolean finish_state = false;
	static float last_time_remaining = -1.0f;
	static float smooth_time_frac = -1.0f;

	if (cls.state != ca_connected || !scr_showgrenadecounter.value)
	{
		finish_state = false;
		last_time_remaining = -1.0f;
		smooth_time_frac = -1.0f;
		return;
	}

	if (!sv.active)
	{
		finish_state = false;
		last_time_remaining = -1.0f;
		smooth_time_frac = -1.0f;
		return;
	}

	float time_remaining = 0.0f;
	qboolean should_draw = false;

	qcvm_t *oldvm = qcvm;
	qboolean switched_vm = false;

	if (qcvm != &sv.qcvm)
	{
		PR_SwitchQCVM(&sv.qcvm);
		switched_vm = true;
	}

	do
	{
		if (!qcvm)
		{
			finish_state = false;
			break;
		}

		if (cl.viewentity <= 0 || cl.viewentity >= qcvm->num_edicts)
		{
			finish_state = false;
			break;
		}

		edict_t *player = EDICT_NUM(cl.viewentity);
		float best_remaining = FLT_MAX;

		for (int i = 0; i < qcvm->num_edicts; ++i)
		{
			edict_t *ent = EDICT_NUM(i);

			if (ent->free)
				continue;

			if (!ent->v.classname)
				continue;

			if (strcmp(PR_GetString(ent->v.classname), "grenade"))
				continue;

			edict_t *owner = ent->v.owner ? PROG_TO_EDICT(ent->v.owner) : NULL;

			if (owner != player)
				continue;

			float remaining = !ent->v.touch ? 0.0f : (ent->v.nextthink - cl.time);

			if (remaining < 0.0f || remaining >= GRENADE_EXPLOSION_TIME)
				continue;

			if (remaining < best_remaining)
				best_remaining = remaining;
		}

		if (best_remaining == FLT_MAX)
		{
			finish_state = false;
			break;
		}

		time_remaining = best_remaining;

		if (time_remaining >= GRENADE_EXPLOSION_TIME || time_remaining < 0.0f)
		{
			finish_state = false;
			break;
		}

		if (time_remaining > 0.2f)
			finish_state = false;
		else if (time_remaining < 0.02f)
			finish_state = true;

		if (finish_state)
			time_remaining = 0.0f;

		should_draw = true;
	} while (0);

	if (switched_vm)
	{
		PR_SwitchQCVM(NULL);
		if (oldvm)
			PR_SwitchQCVM(oldvm);
	}

	if (!should_draw)
	{
		last_time_remaining = -1.0f;
		smooth_time_frac = -1.0f;
		return;
	}

	if (last_time_remaining >= 0.0f)
	{
		const float reset_epsilon = 0.05f; // detect new grenade

		if (time_remaining > last_time_remaining + reset_epsilon)
		{
			last_time_remaining = time_remaining; // new grenade, reset
			smooth_time_frac = -1.0f;
		}
		else
			time_remaining = q_min(time_remaining, last_time_remaining); // clamp to never increase
	}

	last_time_remaining = time_remaining;

	float scale = CLAMP(1.0f, scr_sbarscale.value, (float)glwidth / 320.0f);
	int size = q_max(1, (int)(8.0f * scale + 0.5f));
	int half_size = size / 2;

	GL_SetCanvas(CANVAS_DEFAULT);

	float center_x = scr_vrect.x + (scr_vrect.width * 0.5f);
	float center_y = scr_vrect.y + (scr_vrect.height * 0.4f);

	float half_width = (GRENADE_TIMER_WIDTH * size) * 0.5f;
	float left = center_x - half_width;
	float right = center_x + half_width;
	float border_thickness = 2.0f;

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);

	float inner_left = left + half_size;
	float inner_right = right - half_size;
	float line_thickness = q_max(1.0f, size / 2.0f);
	float line_top = center_y - line_thickness * 0.5f;
	float line_bottom = center_y + line_thickness * 0.5f;

	// 1-pixel black border around the white meter
	glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
	glBegin(GL_QUADS);
	// top border
	glVertex2f(inner_left, line_top);
	glVertex2f(inner_right, line_top);
	glVertex2f(inner_right, line_top + border_thickness);
	glVertex2f(inner_left, line_top + border_thickness);
	// bottom border
	glVertex2f(inner_left, line_bottom - border_thickness);
	glVertex2f(inner_right, line_bottom - border_thickness);
	glVertex2f(inner_right, line_bottom);
	glVertex2f(inner_left, line_bottom);
	// left border
	glVertex2f(inner_left, line_top + border_thickness);
	glVertex2f(inner_left + border_thickness, line_top + border_thickness);
	glVertex2f(inner_left + border_thickness, line_bottom - border_thickness);
	glVertex2f(inner_left, line_bottom - border_thickness);
	// right border
	glVertex2f(inner_right - border_thickness, line_top + border_thickness);
	glVertex2f(inner_right, line_top + border_thickness);
	glVertex2f(inner_right, line_bottom - border_thickness);
	glVertex2f(inner_right - border_thickness, line_bottom - border_thickness);
	glEnd();

	glColor4f(1.0f, 1.0f, 1.0f, 150.0f / 255.0f);
	glBegin(GL_QUADS);
	glVertex2f(inner_left, line_top);
	glVertex2f(inner_right, line_top);
	glVertex2f(inner_right, line_bottom);
	glVertex2f(inner_left, line_bottom);
	glEnd();

	float clamped = CLAMP(0.0f, time_remaining, GRENADE_EXPLOSION_TIME);
	float target_frac = (GRENADE_EXPLOSION_TIME > 0.0f) ? (clamped / GRENADE_EXPLOSION_TIME) : 0.0f;

	// Smooth pointer movement using host_frametime
	if (smooth_time_frac < 0.0f)
		smooth_time_frac = target_frac;
	else
	{
		const float lerp_speed = 10.0f; // higher = snappier, lower = smoother
		float alpha = host_frametime * lerp_speed;
		if (alpha > 1.0f)
			alpha = 1.0f;
		smooth_time_frac += (target_frac - smooth_time_frac) * alpha;
	}
	smooth_time_frac = CLAMP(0.0f, smooth_time_frac, 1.0f);

	float travel = inner_right - inner_left;
	float pos = travel * smooth_time_frac;
	float pointer_x = inner_left + pos;
	float pointer_half_width = q_max(1.0f, (size / 2.0f) * 0.5f);
	float pointer_top = center_y - half_size / 2.0f;
	float pointer_bottom = center_y + half_size / 2.0f;

	// Color ramp: cursor color 2 (gold) -> cursor color 1 (red) as time runs out
	float frac_elapsed = 1.0f - smooth_time_frac; // 0=start, 1=explode
	plcolour_t start_color = Draw_GetConcharsCursorColorByIndex(2); // gold
	plcolour_t end_color = Draw_GetConcharsCursorColorByIndex(1);   // red
	float r = (start_color.rgb[0] + (end_color.rgb[0] - start_color.rgb[0]) * frac_elapsed) / 255.0f;
	float g = (start_color.rgb[1] + (end_color.rgb[1] - start_color.rgb[1]) * frac_elapsed) / 255.0f;
	float b = (start_color.rgb[2] + (end_color.rgb[2] - start_color.rgb[2]) * frac_elapsed) / 255.0f;

	glColor4f(r, g, b, 1.0f);
	glBegin(GL_QUADS);
	glVertex2f(pointer_x - pointer_half_width, pointer_top);
	glVertex2f(pointer_x + pointer_half_width, pointer_top);
	glVertex2f(pointer_x + pointer_half_width, pointer_bottom);
	glVertex2f(pointer_x - pointer_half_width, pointer_bottom);
	glEnd();

	glDisable(GL_BLEND);
	glEnable(GL_ALPHA_TEST);
	glEnable(GL_TEXTURE_2D);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
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
		char original_observing_name[25]; // Buffer to store the original name from Info_GetKey
		char buf2[25];
		char buf3[25];
		char buf4[25];
		const char* obs;
		const char* star_obs;
		const char* observing_ptr;
		int color, y;
		obs = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "observer", buf2, sizeof(buf2));
		star_obs = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "*observer", buf4, sizeof(buf4));
		observing_ptr = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "observing", buf3, sizeof(buf3));

		Q_strncpy(original_observing_name, observing_ptr, sizeof(original_observing_name) - 1);
		original_observing_name[sizeof(original_observing_name) - 1] = '\0';

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

		if (cl.intermission || qeintermission || crxintermission)
			return;

		if (!strcmp(original_observing_name, "off")) // Check original name
			return;

		char name_to_display[25];
		if (cl_contentfilter.value == 2) // Assuming this is your condition to apply the filter
		{
			WordFilter_Check(original_observing_name, name_to_display, sizeof(name_to_display));
			name_to_display[sizeof(name_to_display) - 1] = '\0';
		}
		else
		{
			Q_strncpy(name_to_display, original_observing_name, sizeof(name_to_display) -1);
			name_to_display[sizeof(name_to_display) - 1] = '\0';
		}

		GL_SetCanvas(CANVAS_SBAR2);

		if (cl.modtype == 1 || cl.modtype == 4) // crx case
		{
			if (!strcmp(obs, "chase") || !strcmp(star_obs, "chase")) // chase
			{
				sprintf(printtxt, "%s", name_to_display);
				M_PrintWhite(166 - (strlen(name_to_display)*4), y, printtxt);
			}
			else if (!strcmp(obs, "eyecam") || !strcmp(star_obs, "eyecam")) // eyecam
			{
				if (r_drawviewmodel.value)
					Draw_Fill(152 - strlen(name_to_display)*4, y, (strlen(name_to_display)*8) + 15, 9, 0, .8); // show their color
				sprintf(printtxt, "%s", name_to_display); 
				M_PrintWhite(165-strlen(name_to_display)*4, y, printtxt);
				Draw_Fill(154 - (strlen(name_to_display)*4), y + 1, 7, 7, color, 1); // show their color
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
	scr_shownet.value = CLAMP(0, scr_shownet.value, 10);
	
	if (!scr_shownet.value)
		return;

	if (realtime - cl.last_received_message < scr_shownet.value)
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

	float cross_x = scr_crosshair_x.value;
	float cross_y = scr_crosshair_y.value;

	if (crosshair.value < 0) // Negative values select a character index from conchars (custom crosshair glyphs) -- iw, used in QBJ3
	{
		int custom_char = ((int)-crosshair.value) & 255;
		Draw_CharacterRGBA(-4 + cross_x, -4 + cross_y, custom_char, color, alpha); // 0,0 is center of viewport
	}

	if (crosshair.value == 1)
		Draw_CharacterRGBA (-4 + cross_x, -4 + cross_y, '+', color, alpha); //0,0 is center of viewport

	if (crosshair.value == 2) 
	{
		if (scr_crosshairoutline.value)
			Draw_FillPlayer (-2 + cross_x, -2 + cross_y, 4, 4, outline, alpha); // simple dot (black bg)
		Draw_FillPlayer (-1 + cross_x, -1 + cross_y, 2, 2, color, alpha); // simple dot
	}

	if (crosshair.value == 3)
	{
		if (scr_crosshairoutline.value) 
		{
			Draw_FillPlayer (-2 + cross_x, 5 + cross_y, 4, 12, outline, alpha); // SOUTH (black bg)
			Draw_FillPlayer (-17 + cross_x, -2 + cross_y, 12, 4, outline, alpha); // WEST (black bg)
			Draw_FillPlayer (5 + cross_x, -2 + cross_y, 12, 4, outline, alpha); // EAST (black bg)
			Draw_FillPlayer (-2 + cross_x, -17 + cross_y, 4, 12, outline, alpha); // NORTH (black bg)
		}
		Draw_FillPlayer (-1 + cross_x, 6 + cross_y, 2, 10, color, alpha); // SOUTH
		Draw_FillPlayer (-16 + cross_x, -1 + cross_y, 10, 2, color, alpha); // WEST
		Draw_FillPlayer (6 + cross_x, -1 + cross_y, 10, 2, color, alpha); // EAST
		Draw_FillPlayer (-1 + cross_x, -16 + cross_y, 2, 10, color, alpha); // NORTH
	}

	if (crosshair.value == 4)
	{
		if (scr_crosshairoutline.value)
		{
			Draw_FillPlayer (-2 + cross_x, -10 + cross_y, 4, 20, outline, alpha); // vertical (black bg)
			Draw_FillPlayer (-10 + cross_x, -2 + cross_y, 20, 4, outline, alpha); // horizontal (black bg)
		}
		Draw_FillPlayer (-1 + cross_x, -9 + cross_y, 2, 18, color, alpha); // vertical
		Draw_FillPlayer (-9 + cross_x, -1 + cross_y, 18, 2, color, alpha); // horizontal
	}

	if (crosshair.value == 5)
	{
		if (scr_crosshairoutline.value) 
		{
			Draw_FillPlayer (-3 + cross_x, -10 + cross_y, 6, 20, outline, 1); // vertical (black bg)
			Draw_FillPlayer (-10 + cross_x, -3 + cross_y, 20, 6, outline, 1); // horizontal (black bg)
		}
		Draw_FillPlayer (-2 + cross_x, -9 + cross_y, 4, 18, color, alpha); // vertical (thicker)
		Draw_FillPlayer (-9 + cross_x, -2 + cross_y, 18, 4, color, alpha); // horizontal (thicker)
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
			renderSmoothDot(0.0f + cross_x, 0.0f + cross_y, outlineSize); // Slightly larger dot for outline
		}

		glColor4f(r, g, b, alpha); // Set color for actual dot
		renderSmoothDot(0.0f + cross_x, 0.0f + cross_y, dotSize); // Actual dot size
	}

	if (crosshair.value == 7)
	{
		glColor4f(r, g, b, alpha / 12); // Set color with alpha
		renderCircle(0.0f + cross_x, 0.0f + cross_y, 10.0f, 200, scaledLineWidth); // Draw circle at center with radius 10, more segments for smoothness

		if (scr_crosshairoutline.value)
		{
			glColor4f(ro, go, bo, 1.0f); // Black color for outline
			renderSmoothDot(0.0f + cross_x, 0.0f + cross_y, outlineSize); // Slightly larger dot for outline
		}

		glColor4f(r, g, b, 1.0f);
		renderSmoothDot(0.0f + cross_x, 0.0f + cross_y, dotSize); // Actual dot size
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

			PScript_RunParticleEffectTypeString(wall, NULL, 1, "EF_LASERPOINT"); // particle cfg "r_part laserpoint" for dot on wall
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
		if (cls.timedemo)
			scr_con_current = scr_conlines;	// spoike -- turbocharge any console shrinkage, to make it more deterministic.
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
=================
SCR_AutoID -- woods #autoid
Prints the name of players in your line of sight in demos, coop, and deathmatch as an observer
=================
*/

extern float canvas_scaling;

#define ISDEAD(i) (((i) >= 41) && ((i) <= 102))

typedef struct player_autoid_s
{
	float		x, y;
	scoreboard_t* player;
} autoid_player_t;

static	autoid_player_t	autoids[MAX_SCOREBOARDNAME];
static	int		autoid_count;

typedef struct item_vis_s {
	vec3_t	vieworg;
	vec3_t	forward;
	vec3_t	right;
	vec3_t	up;
	vec3_t	entorg;
	float	radius;
	vec3_t	dir;
	float	dist;
} item_vis_t;

void TrimTrailingSpaces(char* str)
{
	if (!str) return;
	int length = strlen(str);

	while (length > 0 && isspace((unsigned char)str[length - 1]))
	{
		str[length - 1] = '\0';  // replace trailing space with null terminator
		length--;
	}
}

qboolean R_CullSphere(vec3_t org, float radius)
{
	//four frustrum planes all point inwards in an expanding 'cone'.
	int		i;
	float d;

	for (i = 0; i < 4; i++)
	{
		d = DotProduct(frustum[i].normal, org) - frustum[i].dist;
		if (d <= -radius)
			return true;
	}
	return false;
}

int qglProject(float objx, float objy, float objz, float* model, float* proj, int* view, float* winx, float* winy, float* winz) {
	int i, j;
	float in[4], out[4];

	in[0] = objx;
	in[1] = objy;
	in[2] = objz;
	in[3] = 1.0f;

	// First transform by the model matrix
	for (i = 0; i < 4; i++) {
		out[i] = 0.0f;
		for (j = 0; j < 4; j++)
			out[i] += model[j * 4 + i] * in[j]; // Column-major order
	}

	// Then by the projection matrix
	for (i = 0; i < 4; i++) {
		in[i] = 0.0f;
		for (j = 0; j < 4; j++)
			in[i] += proj[j * 4 + i] * out[j]; // Column-major order
	}

	if (fabs(in[3]) < 1e-7f) // Avoid division by zero
		return 0;

	// Perspective division
	for (i = 0; i < 3; i++)
		in[i] /= in[3];

	// Map to window coordinates
	*winx = view[0] + (1 + in[0]) * view[2] / 2.0f;
	*winy = view[1] + (1 + in[1]) * view[3] / 2.0f;
	*winz = (1 + in[2]) / 2.0f;

	return 1;
}

qboolean TP_IsItemVisible(item_vis_t* visitem)
{
	vec3_t impact, end, v;
	int i;

	TraceLine(visitem->vieworg, visitem->entorg, 0, impact); // trace from the viewer's origin to the target's position

	if (VecLength2(impact, visitem->entorg) <= visitem->radius) // did trace hit the target directly within the radius
		return true; // Target is visible

	if (visitem->dist <= visitem->radius) // Check if the distance to the target is within the radius
		return true;

	vec3_t offsets[] = {
		// { x, y, z } offsets relative to the target's origin
		{ 0, 0, 0 },                            // Center (already checked, can be omitted)
		{ -visitem->radius, 0, 0 },             // Left
		{ visitem->radius, 0, 0 },              // Right
		{ 0, 0, visitem->radius },              // Above
		{ 0, 0, -visitem->radius / 2 },         // Below (half radius)
		{ 0, visitem->radius, 0 },              // Forward
		{ 0, -visitem->radius, 0 }              // Backward
	};

	int num_offsets = sizeof(offsets) / sizeof(offsets[0]);

	for (i = 0; i < num_offsets; i++) // Loop through the offsets
	{
		VectorAdd(visitem->entorg, offsets[i], end); // Compute the end point trace by adding the offset to the target's origin

		VectorSubtract(end, visitem->vieworg, v); // Compute the direction vector from the viewer to the end point
		VectorNormalize(v);

		VectorMA(end, -visitem->radius, v, end); // Adjust the end point slightly towards the viewer to account for the target's radius

		TraceLine(visitem->vieworg, end, 0, impact); // Perform the trace from the viewer's origin to the adjusted end point

		if (VecLength2(impact, end) <= visitem->radius) // Check if the trace hit within the target's radius
			return true; // Target is visible
	}

	return false; // If none of the checks are successful, the item is not visible
}

qboolean TP_IsPlayerVisible(vec3_t origin)
{
	item_vis_t visitem;

	VectorCopy(vpn, visitem.forward);
	VectorCopy(vright, visitem.right);
	VectorCopy(vup, visitem.up);
	VectorCopy(r_origin, visitem.vieworg);

	VectorCopy(origin, visitem.entorg);
	visitem.entorg[2] += 27; // Adjust to player's head height
	VectorSubtract(visitem.entorg, visitem.vieworg, visitem.dir);
	visitem.dist = DotProduct(visitem.dir, visitem.forward);
	visitem.radius = 25; // Player's approximate radius

	return TP_IsItemVisible(&visitem);
}

static qboolean SCR_IsOutsideMapView(void)
{
	vec3_t end, impact;

	/*
	 * If a very long trace from the camera reaches the end point without
	 * hitting solid world geometry, treat it as a void/outside-map view.
	 */
	VectorMA(r_origin, 65536.0f, vpn, end);
	TraceLine(r_origin, end, 0, impact);

	return VecLength2(impact, end) <= 1.0f;
}

static void SCR_GetAutoIDOrigin(const entity_t *state, vec3_t out)
{
	if (r_lerpmove.value && state != &cl.viewent && (state->lerpflags & LERP_MOVESTEP))
	{
		float blend;
		vec3_t d;
		float s = (cls.demoplayback && cls.demospeed < 0.f) ? -1.f : 1.f;

		if (state->lerpflags & LERP_FINISH)
		{
			float denom = state->lerpfinish - state->movelerpstart;
			blend = (denom > 0.0f) ? CLAMP(0.0f, (float)(cl.time - state->movelerpstart) / denom, 1.0f) : 1.0f;
		}
		else
		{
			blend = CLAMP(0.0f, (float)(cl.time - state->movelerpstart) / 0.1f * s, 1.0f);
		}

		VectorSubtract(state->currentorigin, state->previousorigin, d);
		out[0] = state->previousorigin[0] + d[0] * blend;
		out[1] = state->previousorigin[1] + d[1] * blend;
		out[2] = state->previousorigin[2] + d[2] * blend;
	}
	else
	{
		VectorCopy(state->origin, out);
	}
}

static int SCR_FloorToInt(float x)
{
	return (int)floorf(x);
}

void SCR_SetupAutoID(void)
{
	int		i, view[4];
	float		model[16], project[16], winz;
	vec3_t origin, base_origin;
	entity_t* state;
	autoid_player_t* id;

	autoid_count = 0;

	char buf[15];
	const char* obs;

	char buf2[15];
	const char* playmode;

	char buf3[15];
	const char* mode;

	char buf4[15];
	const char* star_obs;

	qboolean skip_vis = false; // skip LOS only for fly observers looking into map void/outside

	//if (r_refdef.viewangles[ROLL] == 80) // dead, could rotate text?

	if (!scr_autoid.value || cls.state != ca_connected || cl.intermission || qeintermission || crxintermission)
		return;

	if ((cl.gametype == GAME_DEATHMATCH) && (cls.state == ca_connected) && !cls.demoplayback)
	{
		obs = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "observer", buf, sizeof(buf));
		star_obs = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "*observer", buf4, sizeof(buf4));
		playmode = Info_GetKey(cl.serverinfo, "playmode", buf2, sizeof(buf2));
		mode = Info_GetKey(cl.serverinfo, "mode", buf3, sizeof(buf3));

		if (cl.modtype == 1 || cl.modtype == 4) // mods with observer keys
		{
			if (
				(strcmp(obs, "eyecam") != 0 && strcmp(star_obs, "eyecam") != 0) && // allow in mp if an observer
				(strcmp(obs, "chase") != 0 && strcmp(star_obs, "chase") != 0) &&
				(strcmp(obs, "fly") != 0 && strcmp(star_obs, "fly") != 0) &&
				(strcmp(obs, "walk") != 0 && strcmp(star_obs, "walk") != 0) &&
				(
					(strcmp(playmode, "practice") != 0) || // allow in practice mode if value 2
					((int)scr_autoid.value != 2)
					) &&
				(
					((strcmp(playmode, "match") != 0 || (strcmp(mode, "dm") != 0 && strcmp(mode, "ctf") != 0))) || // allow in pre-match if value 2
					(cl.matchinp || (int)scr_autoid.value != 2)
					)
				) {
				return;
			}

			// Only skip LOS in fly mode when the view is in map void/outside area.
			if (!strcmp(obs, "fly") || !strcmp(star_obs, "fly"))
				skip_vis = SCR_IsOutsideMapView();
		}
		else if (!strcmp(cl.observer, "n")) // general observer flag for legacy mods/servers
			return;
	}

	glGetFloatv(GL_MODELVIEW_MATRIX, model);
	glGetFloatv(GL_PROJECTION_MATRIX, project);
	glGetIntegerv(GL_VIEWPORT, view);

	for (i = 0; i < cl.maxclients; i++)
	{
		state = &cl.entities[1 + i];

		if (state->model == NULL)
			continue;

		if ((1 + i) == cl.viewentity || (1 + i) == cl.realviewentity)
			continue;  // Skip our own entity / eyecam target

		if ((!strcmp(state->model->name, "progs/player.mdl") && ISDEAD(state->frame)) || !strcmp(state->model->name, "progs/h_player.mdl"))
			continue;

		SCR_GetAutoIDOrigin(state, base_origin);
		VectorCopy(base_origin, origin);
		origin[2] += 28;
		if (R_CullSphere(origin, 0))
			continue;

		if (!skip_vis && !TP_IsPlayerVisible(base_origin))
			continue;

		id = &autoids[autoid_count];
		id->player = &cl.scores[i];

		if (qglProject(origin[0], origin[1], origin[2], model, project, view, &id->x, &id->y, &winz))
			autoid_count++;
	}
}

void SCR_DrawAutoID(void)
{
	int	i;
	int xi, yi;
	float x, y, xfrac, yfrac;
	char formatted_name[16]; // 15 chars + null terminator
	int name_length;
	int y_offset = 12;
	char filtered_name[16]; // Buffer for filtered name
	qboolean was_filtered = false; // Flag if filtering occurred

	float alpha = scr_autoid.value;
	int integer_part = (int)alpha;
	float decimal_part = alpha - integer_part;

	if (decimal_part < 0.0f || decimal_part > 1.0f)
		decimal_part = 1.0f; // Default opacity

	if (!scr_autoid.value)
		return;

	GL_SetCanvas(CANVAS_AUTOID);

	const char* observing = "null";
	char buf[16];
	observing = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "observing", buf, sizeof(buf)); // userinfo

	for (i = 0; i < autoid_count; i++)
	{
		// Adjust coordinates according to the scaling factor
		x = autoids[i].x / canvas_scaling;
		y = (glheight - autoids[i].y) / canvas_scaling - y_offset;

		if (r_refdef.viewangles[ROLL] == 80) // dead, adjust text
		{
			x += 26;
			y -= y_offset;
		}

		if (cl_contentfilter.value == 2) // woods #contentfilter
		{
			was_filtered = WordFilter_Check(autoids[i].player->name, filtered_name, sizeof(filtered_name));
			filtered_name[sizeof(filtered_name) - 1] = '\0'; 
		}

		const char* name_to_use = (cl_contentfilter.value == 2 && was_filtered) ? filtered_name : autoids[i].player->name;

		q_snprintf(formatted_name, sizeof(formatted_name), "%.15s", name_to_use);

		TrimTrailingSpaces(formatted_name);

		name_length = strlen(formatted_name);

			/*
			 * Only suppress the tracked player's label while in eyecam.
			 * In fly/chase modes, "observing" can still be set and should not hide names.
			 */
			if (cl.eyecam && !strcmp(formatted_name, observing))
				continue;

		xi = SCR_FloorToInt(x);
		yi = SCR_FloorToInt(y);
		xfrac = x - (float)xi;
		yfrac = y - (float)yi;

		glPushMatrix();
		glTranslatef(xfrac, yfrac, 0.0f);

		Draw_FillPlayer(xi - 4 * name_length - 1, yi - 1, (name_length * 8) + 2, 11, CL_PLColours_Parse("0x000000"), decimal_part);
		Draw_String(xi - 4 * name_length, yi, formatted_name);

		glPopMatrix();
	}
}

void SCR_DrawStatusIndicators (void)
{
	int i;
	int xi, yi;
	float x, y, xfrac, yfrac;
	const int y_offset = 12;

	if (cls.state != ca_connected || cl.intermission || qeintermission || crxintermission)
		return;
	if (!autoid_count)
		return;

	GL_SetCanvas(CANVAS_AUTOID);

	for (i = 0; i < autoid_count; ++i)
	{
		const char* fullname = autoids[i].player->name;

		/* inline test that ignores the 0x80 red-mask bit */
		char clean[64];  int n = 0;
		for (const char* p = fullname; *p && n < (int)sizeof(clean) - 1; ++p)
			clean[n++] = (char)tolower(*p & 0x7F);   /* strip bit, lower-case */
		clean[n] = '\0';

		qboolean is_typing = q_strcasestr(clean, "typing") != NULL;

		/* Case sensitive AFK detection that only triggers after 15th character */
		qboolean is_afk = false;
		if (strlen(fullname) > 15) {
			/* Check for both regular "AFK" and Quake-encoded "AFK" (ÁÆË) */
			const char* afk_pos = strstr(fullname + 15, "AFK");
			if (!afk_pos) {
				/* Check for Quake-encoded AFK: ÁÆË (0xC1 0xC6 0xCB with possible 0x80 mask) */
				for (const char* p = fullname + 15; p[0] && p[1] && p[2]; ++p) {
					if ((p[0] & 0x7F) == 0x41 && /* A */
						(p[1] & 0x7F) == 0x46 && /* F */
						(p[2] & 0x7F) == 0x4B)   /* K */
					{
						afk_pos = p;
						break;
					}
				}
			}
			is_afk = (afk_pos != NULL);
		}

		if (!is_typing && !is_afk)
			continue;

		/* projected coords prepared by SCR_SetupAutoID() */
		x = autoids[i].x / canvas_scaling;
		y = (glheight - autoids[i].y) / canvas_scaling - y_offset;

		if (r_refdef.viewangles[ROLL] == 80) { x += 26; y -= y_offset; }
		if (scr_autoid.value > 0)             y -= y_offset;   /* above shown name */

		xi = SCR_FloorToInt(x);
		yi = SCR_FloorToInt(y);
		xfrac = x - (float)xi;
		yfrac = y - (float)yi;

		glPushMatrix();
		glTranslatef(xfrac, yfrac, 0.0f);

		if (is_afk) {
			/* Draw red "AFK" using 128 mask */
			char afk_text[4];
			afk_text[0] = 'A' | 128;  // Red A
			afk_text[1] = 'F' | 128;  // Red F
			afk_text[2] = 'K' | 128;  // Red K
			afk_text[3] = '\0';
			Draw_String(xi - 12, yi, afk_text);
		}
		else {
			/* Animated typing dots */
			Draw_StringAnimatedDots(xi - 12, yi, "...");
		}

		glPopMatrix();
	}
}

float last_pause_time = 0.0f; // woods #obstimers - Store pause start time
float pause_offset = 0.0f; // woods #obstimers - Accumulated pause offset

/*
==============
SCR_DrawObsTimers -- woods #obstimers
==============
*/
void SCR_DrawObsTimers (void)
{
	char playmode_buf[32]; // Buffer for the playmode string
	const char* playmode_val;
	qboolean is_playmode_match;

	// Get the playmode from server info
	playmode_val = Info_GetKey(cl.serverinfo, "playmode", playmode_buf, sizeof(playmode_buf));
	is_playmode_match = (strcmp(playmode_val, "match") == 0);
	
	if (!scr_obsitems.value || !cl.itemtimers ||
		cl.intermission ||
		qeintermission ||
		crxintermission ||
		scr_viewsize.value >= 130 ||
		(is_playmode_match && !cl.matchinp))
		return;

#define MAX_VISIBLE_TIMERS 32
#define COUNTDOWN_TIME 5
#define TIMER_SPACING 10

// Position setup
	int base_y = 0;
	int x = 0;
	int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);

	// Set canvas and calculate positions based on HUD type
	if (clampedSbar == 3 && scr_viewsize.value <= 110) 
	{
		GL_SetCanvas(CANVAS_BOTTOMLEFTQE);
		base_y = 177;
		x = (scr_showspeed.value != 1 && !scr_movekeys.value) ? 134 : 172;

		if ((scr_sbarscale.value > 3 && (scr_showspeed.value == 1 || scr_movekeys.value))
			|| scr_sbarscale.value >= 4)
			base_y -= 12;
	}
	else 
	{
		GL_SetCanvas(CANVAS_BOTTOMLEFT);
		base_y = 186;
		x = 6;
		if (scr_ping.value || key_dest != key_console)
			base_y -= 23;
	}

	if (cl.match_pause_time > 0) 
	{
		// During pause: calculate current pause duration
		float pause_duration = cl.time - cl.match_pause_time;
		last_pause_time = cl.match_pause_time;
		pause_offset = pause_duration;
	}
	else if (last_pause_time > 0) 
	{
		// Pause just ended: keep the final offset
		last_pause_time = 0;
	}

	// Timer collection structure
	typedef struct {
		struct itemtimer_s* timer;
		float time_left;
	} visible_timer_t;
	visible_timer_t visible_timers[MAX_VISIBLE_TIMERS];
	int num_visible = 0;

	// Collect visible timers
	for (struct itemtimer_s* timer = cl.itemtimers; timer; timer = timer->next)
	{
		float time_left = timer->end - cl.time;

		// Add pause offset to time_left if we're paused or have a stored offset
		if (cl.match_pause_time > 0 || pause_offset > 0)
		{
			time_left += pause_offset;
		}

		if (time_left <= COUNTDOWN_TIME && time_left > -1.0 &&
			num_visible < MAX_VISIBLE_TIMERS) {
			visible_timers[num_visible].timer = timer;
			visible_timers[num_visible].time_left = time_left;
			num_visible++;
		}
	}

	// Sort timers (spawn first, then by remaining time)
	for (int i = 0; i < num_visible - 1; i++) 
	{
		for (int j = 0; j < num_visible - i - 1; j++) 
		{
			qboolean should_swap = false;
			float time1 = visible_timers[j].time_left;
			float time2 = visible_timers[j + 1].time_left;

			if (time1 > 0 && time2 <= 0)
				should_swap = true;
			else if (time1 > 0 && time2 > 0 && time1 > time2)
				should_swap = true;

			if (should_swap) {
				visible_timer_t temp = visible_timers[j];
				visible_timers[j] = visible_timers[j + 1];
				visible_timers[j + 1] = temp;
			}
		}
	}

	// Draw timers
	char str[32];
	for (int i = 0; i < num_visible; i++) 
	{
		struct itemtimer_s* timer = visible_timers[i].timer;
		float time_left = visible_timers[i].time_left;
		int y = base_y - (i * TIMER_SPACING);
		int name_width = strlen(timer->timername) * 8;

		Draw_String(x, y, timer->timername);

		if (time_left <= 0)
			M_Print(x + name_width + 8, y, "spawn");
		else {
			sprintf(str, "%d", (int)ceil(time_left));
			M_Print(x + name_width + 8, y, str);
		}
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
		if (cl_contentfilter.value) // woods #contentfilter
		{
			char filtered_path[MAX_OSPATH];
			const char* gamedir_name = COM_SkipPath(com_gamedir);
			q_snprintf(filtered_path, sizeof(filtered_path), "%s/screenshots/%s",
				gamedir_name, COM_SkipPath(imagename));
			Con_Printf("Wrote %s\n", filtered_path);
		}
		else
			Con_Printf("Wrote %s\n", checkname);

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

void SCR_DrawNotifyString (void) // woods add ^m support
{
	const char	*start;
	int		l;
	int		x, y;
	int mask = 0;       // Masking state
	int last_char = 0;  // Previous character

	GL_SetCanvas (CANVAS_MENU); //johnfitz

	start = scr_notifystring;

	y = 200 * 0.35; //johnfitz -- stretched overlays

	while (*start)
	{
		// First pass: calculate visible length (excluding control sequences)
		int visible_length = 0;
		for (l = 0; l < 40; l++)
		{
			if (start[l] == '\n' || !start[l])
				break;

			// Skip ^m sequences when calculating length
			if (start[l] == '^' && l + 1 < 40 && start[l + 1] == 'm')
			{
				l++; // Skip both ^ and m
				continue;
			}
			// Skip standalone ^ if it's not part of a valid sequence
			if (start[l] == '^' && l + 1 < 40 && start[l + 1] != 'm')
			{
				continue;
			}
			visible_length++;
		}

		// Calculate starting x position based on visible length
		x = (320 - visible_length * 8) / 2;

		// Second pass: actual drawing
		for (int j = 0; j < l;)
		{
			char c = start[j];

			// Handle masking sequences
			if (last_char == '^' && c == 'm')
			{
				mask ^= 128;  // Toggle mask
				last_char = 0;
				j++;
				continue;
			}

			if (c == '^')
			{
				last_char = '^';
				j++;
				continue;
			}

			if (last_char == '^' && c != 'm')
			{
				last_char = 0;
				// Continue to draw the current character
			}
			else
			{
				last_char = 0;
			}

			// Apply mask if enabled
			int num = c;
			if (mask)
				num = (num & 127) | 128;
			else
				num &= 127;

			if (num == 32)
			{
				x += 8;
				j++;
				continue;
			}

			Draw_CharacterRGBA(x, y, num, CL_PLColours_Parse("0xffffff"), 1);
			x += 8;
			j++;
		}

		y += 8;

		start += l;

		while (*start && *start != '\n')
			start++;

		if (*start == '\n')
			start++;
	}
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
		 lastchar != '`' &&
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

/*
=================
Pong -- woods #pong
=================

A simple Pong mini-game that runs whenever the game is paused (#pong).
Controlled by the cl_pong cvar, it features resolution-independent scaling,
an adjustable speed multiplier, basic AI paddle logic, and standard
Quake engine rendering and input handling for a retro diversion.
*/

extern cvar_t gl_load24bit;
extern qpic_t* sb_nums[2][11];
extern qboolean windowhasfocus;

#define BASE_W          1920.0f
#define BASE_H          1080.0f
#define MAX_BALL_SPEED  900.f
#define AI_OFFSET_TIME  0.25f

static inline float GetScale(void)
{
	float sx = (float)glwidth / BASE_W;
	float sy = (float)glheight / BASE_H;
	return q_min(sx, sy);
}
static inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

typedef struct { float x, y, w, h, speed; } paddle_t;
typedef struct {
	float x, y, size, dx, dy, speed;
	qmodel_t* model;
	struct gltexture_s* texture;
	mspriteframe_t* frame;
} ball_t;

static paddle_t player, ai;
static ball_t   ball;
static int      scr_w, scr_h;
static int      paddle_w = 15, paddle_h = 80, ball_sz = 10;
static int      ply_score = 0, ai_score = 0;
static float pong_last_maptime_init = 0; // Store maptime when pong last init'd
static qpic_t* pong_pause_pic = NULL; // Cache the pause picture pointer


static qboolean game_init = false;
static qboolean sprite_checked = false;

static float    last_upd_time = 0;
static float    player_paddle_flash_time = 0.0f; // Time when the flash should end
static qboolean pong_user_frozen = false;

void Pong_ToggleFreeze(void)
{
	pong_user_frozen = !pong_user_frozen;
}

/* ----------------------------------------------------------------------
   Return the PAUSE‑banner rectangle in "virtual" coordinates.
   ---------------------------------------------------------------------- */
static qboolean Pong_GetPauseRect(float sc,
	float* x, float* y, float* w, float* h)
{
	if (!pong_pause_pic)                       /* texture not in memory   */
		return false;

	/* 1. Base menu-scale used by the *actual* canvas ------------------- */
	float menuscale =
		cl.match_pause_time ? scr_menuscale.value - 1.f   // CANVAS_MENU2
		: scr_menuscale.value;        // CANVAS_MENU
	menuscale = Clamp(menuscale, 1.f, menuscale);             // keep ≥ 1

	const float msc = Clamp(q_min((float)glwidth / 320.f,
		(float)glheight / 200.f),
		1.f, menuscale);


	/* -----------------------------------------------------------------
	   Choose the picture width/height we’re going to *centre*.
	   ----------------------------------------------------------------- */
	float pic_w = pong_pause_pic->width;
	float pic_h = pong_pause_pic->height;

	/* QUICK HACK: if external 24‑bit textures are OFF, assume the stock
	   128×24 LMP so the X‑position lines up with what’s drawn.          */
	if (!gl_load24bit.value)
		pic_w = 128;                          /* << the only new line   */

	/* Square sprite safety: fix broken 24×24 headers */
	if (pic_w == pic_h && pic_h > 0)
		pic_w = pic_h * (128.0f / 24.0f);     /* ≈ 5.333× wider */

	/* 2. Pixel‑space rectangle (matches SCR_DrawPause) ---------------- */
	float px = ((320 - pic_w) * 0.5f) * msc +
		(glwidth - 320.f * msc) * 0.5f;
	float py = ((240 - 48 - pic_h) * 0.5f) * msc +
		(glheight - 200.f * msc) * 0.5f;
	float pw = pic_w * msc;
	float ph = pic_h * msc;

	/* DEBUG overlay – keep or remove as you wish */
	//Draw_Fill(px, py, pw, ph, 236, 0.3f);

	/* 3. Convert back to our 1920×1080 “virtual” space ---------------- */
	*x = px / sc;  *y = py / sc;
	*w = pw / sc;  *h = ph / sc;
	return true;
}


void Pong_Init(void)
{
	memset(&ball, 0, sizeof(ball));
	sprite_checked = false;

	// Cache pause pic on init
	if (!pong_pause_pic)
		pong_pause_pic = Draw_CachePic("gfx/pause.lmp");
}

static void Pong_Reset(void)
{
	const float sc = GetScale();
	pong_user_frozen = false;
	scr_w = vid.width;  scr_h = vid.height;

	player.w = ai.w = paddle_w;
	player.h = ai.h = paddle_h;
	ball.size = ball_sz;

	if (!game_init) {
		int mx, my; SDL_GetMouseState(&mx, &my);
		player.y = Clamp((float)my / sc - player.h * 0.5f, 0, (scr_h / sc) - player.h);
		player.x = (scr_w / sc) - paddle_w - 20;
		player.speed = 500.f;
		ply_score = ai_score = 0; // Reset score only on first init
	}
	else {
		player.x = (scr_w / sc) - paddle_w - 20;
		player.y = Clamp(player.y, 0, (scr_h / sc) - player.h);
	}

	ai.x = 20; ai.y = ((scr_h / sc) - paddle_h) * 0.5f; ai.speed = 300.f;

	float prx, pry, prw, prh;
	if (!Pong_GetPauseRect(sc, &prx, &pry, &prw, &prh))
	    return;                   /* nothing to bounce off - bail */

	ball.x = (scr_w / sc) * 0.5f;
	ball.y = Clamp(pry - ball.size * 8, ball.size * 4, (scr_h / sc) * 0.25f);
	float ang = ((rand() % 60) - 30) * (M_PI / 180.f);
	float dir = (rand() % 2) ? 1.f : -1.f;
	ball.dx = dir * cosf(ang); ball.dy = sinf(ang);
	float m = sqrtf(ball.dx * ball.dx + ball.dy * ball.dy);
	ball.dx /= m; ball.dy /= m;
	// Use pong_enable value as speed multiplier (will be 0 if disabled)
	ball.speed = 300.f * cl_pong.value;

	game_init = true;
	last_upd_time = realtime;
}

static inline void Pong_HandlePaddleCollision(paddle_t* P)
{
	if (ball.x + ball.size * 0.5f > P->x && ball.x - ball.size * 0.5f < P->x + P->w &&
		ball.y + ball.size * 0.5f > P->y && ball.y - ball.size * 0.5f < P->y + P->h)
	{
		ball.x = (ball.dx > 0 ? P->x - ball.size * 0.5f : P->x + P->w + ball.size * 0.5f);
		ball.dx = -ball.dx;
		S_LocalSound("buttons/switch21.wav");
		float rel = (P->y + P->h * 0.5f) - ball.y;
		ball.dy = -0.75f * (rel / (P->h * 0.5f));
		if (fabsf(ball.dy) < 0.1f) ball.dy = (ball.dy > 0 ? 0.1f : -0.1f);
		float m = sqrtf(ball.dx * ball.dx + ball.dy * ball.dy);
		if (m != 0) { // Avoid division by zero
			ball.dx /= m;
			ball.dy /= m;
		}
		ball.speed = q_min(ball.speed * 1.05f, MAX_BALL_SPEED);
	}
}

void Pong_Update(void)
{
	if (cl_pong.value <= 0 || (!cl.paused && !cl.match_pause_time) || cls.demoplayback) return;

	if (game_init && (vid.width != scr_w || vid.height != scr_h)) // Check if the window was resized while paused
	{
		Pong_Reset(); // Re-initialize with new dimensions
	}

	if (maptime > pong_last_maptime_init) // Check if the map has changed since the last time Pong was active
	{
		game_init = false;      // Force re-initialization
		sprite_checked = false; // Force sprite lookup
		ball.model = NULL;      // Clear potentially stale pointers
		ball.frame = NULL;
		ball.texture = NULL;
		pong_last_maptime_init = maptime; // Update our stored maptime
	}

	qboolean frozen = (pong_user_frozen ||
		key_dest != key_game ||
		!windowhasfocus);

	// Initialize the game on the first *active* frame if needed
	// We also need last_upd_time to be set correctly before calculating dt
	if (!game_init) {
		if (!frozen) {
			Pong_Reset(); // This sets game_init and last_upd_time
		}
		else {
			return; // Don't initialize or update if frozen
		}
	}

	// If frozen, do not update game state or last_upd_time
	if (frozen) {
		return;
	}

	// --- Game logic proceeds only if not frozen ---

	const float sc = GetScale();
	const float sw = scr_w / sc, sh = scr_h / sc;

	// Calculate dt based on last *active* update time
	float dt = realtime - last_upd_time;
	// Update last_upd_time *only when* logic runs
	last_upd_time = realtime;

	// Handle potential time issues (e.g., after regaining focus if dt wasn't clamped)
	if (dt < 0) dt = 0; // Prevent issues if time goes backwards slightly
	// Clamp large delta time - might still happen if focus is lost/regained rapidly
	// between the frozen check and here, though unlikely. A clamp is safe.
	if (dt > 0.1f) dt = 0.1f;

	/* move ball --------------------------------------------------------- */
	ball.x += ball.dx * ball.speed * dt;
	ball.y += ball.dy * ball.speed * dt;
	if (ball.y - ball.size * 0.5f < 0) { ball.y = ball.size * 0.5f; ball.dy = -ball.dy; }
	if (ball.y + ball.size * 0.5f > sh) { ball.y = sh - ball.size * 0.5f;ball.dy = -ball.dy; }

	/* bounce off pause pic --------------------------------------------- */
	float prx, pry, prw, prh;
	if (Pong_GetPauseRect(sc, &prx, &pry, &prw, &prh) &&
	    ball.x + ball.size * .5f > prx && ball.x - ball.size * .5f < prx + prw &&
	    ball.y + ball.size * .5f > pry && ball.y - ball.size * .5f < pry + prh)
	{
	    /* choose the nearest side and reflect */
	    float d[4] = { ball.x - prx, (prx + prw) - ball.x,
	                   ball.y - pry, (pry + prh) - ball.y };
	    int side = 0;
	    for (int i = 1; i < 4; ++i) if (d[i] < d[side]) side = i;

	    switch (side) {
	        case 0: ball.x = prx - ball.size * .5f;   ball.dx = -fabsf(ball.dx); break;
	        case 1: ball.x = prx + prw + ball.size * .5f; ball.dx =  fabsf(ball.dx); break;
	        case 2: ball.y = pry - ball.size * .5f;   ball.dy = -fabsf(ball.dy); break;
	        case 3: ball.y = pry + prh + ball.size * .5f; ball.dy =  fabsf(ball.dy); break;
	    }
	}

	/* paddle collision macro ------------------------------------------- */
	if (ball.dx > 0) { Pong_HandlePaddleCollision(&player); }
	else { Pong_HandlePaddleCollision(&ai); }

	/* AI movement -------------------------------------------------------- */
	// AI uses dt, so it freezes automatically when update logic stops
	static float offset = 0, last_off = 0; // Keep these static within the function
	static qboolean ball_right = true;     // Keep this static

	qboolean ball_left = ball.x < sw * 0.5f;
	if (ball.dx < 0 && (ball_right || realtime - last_off > AI_OFFSET_TIME)) {
		float diff = ai_score - ply_score;
		float t = Clamp(fabsf(diff) / 5.f, 0, 1);
		float eff = (diff <= 0) ? (0.6f + 0.15f * t) : (0.6f - 0.35f * t);
		// Calculate offset relative to scaled paddle height
		float random_fraction = (rand() / (float)RAND_MAX) * 2.0f - 1.0f; // Random float between -1 and +1
		float max_offset = ai.h * 0.5f * sc; // Max offset is half the paddle height (using ai.h which is already scaled)
		offset = (1.f - eff) * random_fraction * max_offset; // Apply effectiveness factor
		last_off = realtime;
	}
	ball_right = !ball_left; // Update ball_right state

	float ai_tgt = ball.y - ai.h * 0.5f + offset;
	if (ball.dx < 0) { // Only move AI paddle if ball is coming towards it
		if (ai.y < ai_tgt) { ai.y += ai.speed * dt;if (ai.y > ai_tgt)ai.y = ai_tgt; }
		else if (ai.y > ai_tgt) { ai.y -= ai.speed * dt;if (ai.y < ai_tgt)ai.y = ai_tgt; }
	}
	ai.y = Clamp(ai.y, 0, sh - ai.h);
	// Player paddle position is updated by Pong_MouseMove

	/* scoring ------------------------------------------------------------ */
	if (ball.x < 0) { ++ply_score;S_LocalSound("zombie/z_miss.wav");Pong_Reset(); }
	else if (ball.x > sw) {
		++ai_score;S_LocalSound("zombie/z_miss.wav");
		player_paddle_flash_time = realtime + 0.1f;
		Pong_Reset();
	}
}

static void Quad(float x, float y, float w, float h)
{
	glBegin(GL_QUADS);
	glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
	glEnd();
}

void Pong_Draw(void)
{
	// Skip drawing during demo playback
	if (cls.demoplayback) return;

	// Only draw if enabled and game is paused
	if (cl_pong.value <= 0 || (!cl.paused && !cl.match_pause_time)) return;

	GL_SetCanvas(CANVAS_DEFAULT);

	// Ensure game is initialized before drawing. Call Pong_Reset if needed.
	// This handles the case where Draw runs before Update initializes the game.
	if (!game_init) {
		Pong_Reset();
		// If Pong_Reset failed (e.g. sprite not loaded yet), game_init might still be false.
		if (!game_init) return;
	}

	if (!sprite_checked) {
		if (!ball.model || !ball.frame || !ball.texture ||
			!ball.frame->gltexture ||
			strcmp(ball.texture->name, "progs/s_light.spr:frame0"))
		{
			ball.model = Mod_ForName("progs/s_light.spr", false);
			if (ball.model && ball.model->type == mod_sprite) {
				msprite_t* sp = (msprite_t*)ball.model->cache.data;
				if (sp && sp->numframes && sp->frames[0].type == SPR_SINGLE) {
					ball.frame = sp->frames[0].frameptr;
					ball.texture = ball.frame->gltexture;
				}
			}
		}
		else ball.texture = ball.frame->gltexture;
		sprite_checked = true;
	}
	if (!ball.texture) return;

	const float sc = GetScale();

	/* scores ----------------------------------------------------------- */
	float prx, pry, prw, prh;
	if (!Pong_GetPauseRect(sc, &prx, &pry, &prw, &prh))
	    return;        /* still loading */

	int psx = prx * sc, psy = pry * sc;      /* back to pixels for drawing */
	int pw  = prw * sc, ph  = prh * sc;

	glEnable(GL_TEXTURE_2D); glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // Set standard blend func
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); // Use modulate for scores/pics
	glColor4f(1, 1, 1, 0.9f);

	float num_sc = sc * 1.2f;
	int sy = psy + (ph - 24 * num_sc) / 2, pad = 20 * sc,
		x = psx - pad - (ai_score >= 10 ? 2 : 1) * 24 * num_sc;

	if (ai_score >= 10) { Draw_ScaledPicAlpha(x, sy, sb_nums[0][ai_score / 10], num_sc, 1);x += 24 * num_sc; }
	Draw_ScaledPicAlpha(x, sy, sb_nums[0][ai_score % 10], num_sc, 1);

	x = psx + pw + pad;
	if (ply_score >= 10) { Draw_ScaledPicAlpha(x, sy, sb_nums[0][ply_score / 10], num_sc, 1);x += 24 * num_sc; }
	Draw_ScaledPicAlpha(x, sy, sb_nums[0][ply_score % 10], num_sc, 1);
	glDisable(GL_TEXTURE_2D); // Disable texture for paddles (solid color quads)

	/* paddles ----------------------------------------------------------- */
	float pwid = paddle_w * sc, phgt = paddle_h * sc;
	glColor4f(0, 0, 0, 0.9f); // Black outline
	Quad(player.x * sc - sc, player.y * sc - sc, pwid + 2 * sc, phgt + 2 * sc);
	Quad(ai.x * sc - sc, ai.y * sc - sc, pwid + 2 * sc, phgt + 2 * sc);

	// Determine player paddle color (flash or default)
	byte paddle_rgb[3];
	float paddle_alpha = 0.9f; // Default alpha
	static byte white_rgb[3] = { 255, 255, 255 };
	byte* default_col = (cl.viewentity > 0 && (unsigned)(cl.viewentity - 1) < (unsigned)cl.maxclients) ?
		CL_PLColours_ToRGB(&cl.scores[cl.viewentity - 1].pants) :
		white_rgb;
	paddle_rgb[0] = default_col[0];
	paddle_rgb[1] = default_col[1];
	paddle_rgb[2] = default_col[2];

	// Draw the base player paddle
	glColor4f(paddle_rgb[0] / 255.f, paddle_rgb[1] / 255.f, paddle_rgb[2] / 255.f, paddle_alpha);
	Quad(player.x * sc, player.y * sc, pwid, phgt);

	// Overlay damage color if flashing
	if (cl_damagehue.value != 0 && realtime < player_paddle_flash_time)
	{
		// Use damage hue color
		plcolour_t damage_color = CL_PLColours_Parse(cl_damagehuecolor.string);
		paddle_rgb[0] = damage_color.rgb[0];
		paddle_rgb[1] = damage_color.rgb[1];
		paddle_rgb[2] = damage_color.rgb[2];
		float flash_alpha = 0.7f; // Opacity for the flash overlay

		// Draw the flash overlay
		glColor4f(paddle_rgb[0] / 255.f, paddle_rgb[1] / 255.f, paddle_rgb[2] / 255.f, flash_alpha);
		Quad(player.x * sc, player.y * sc, pwid, phgt);
	}
	// No 'else' needed, base paddle is already drawn

	glColor4f(0.5f, 0.5f, 0.5f, 0.9f); // AI paddle color
	Quad(ai.x * sc, ai.y * sc, pwid, phgt);

	/* ball -------------------------------------------------------------- */
	GL_DisableMultitexture(); // Ensure multitexture is off
	GL_ClearBindings(); // Clear previous texture bindings
	glEnable(GL_TEXTURE_2D); // Enable texture for ball sprite
	glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.1f); // Use alpha test for sprite transparency
	GL_Bind(ball.texture); glColor4f(1, 1, 1, 0.9f); // White color for sprite

	float bs = ball.size * sc * 1.5f,
		smax = ball.frame ? ball.frame->smax : 1.f,
		tmax = ball.frame ? ball.frame->tmax : 1.f;
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0);        glVertex2f(ball.x * sc - bs, ball.y * sc + bs);
	glTexCoord2f(smax, 0);     glVertex2f(ball.x * sc + bs, ball.y * sc + bs);
	glTexCoord2f(smax, tmax);  glVertex2f(ball.x * sc + bs, ball.y * sc - bs);
	glTexCoord2f(0, tmax);     glVertex2f(ball.x * sc - bs, ball.y * sc - bs);
	glEnd();

	/* ---------- restore “classic console” GL state ------------------ */
	glEnable(GL_ALPHA_TEST);                                   /* hard mask   */
	glAlphaFunc(GL_GREATER, 0.666f);                            /* typical cut-off */
	glDisable(GL_BLEND);                                        /* opaque glyphs   */
	glEnable(GL_TEXTURE_2D);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE); /* ignore colour   */
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);                          /* white           */
	GL_ClearBindings();                                         /* unbind sprites  */
}

void Pong_MouseMove(int x, int y)
{
	if (cl_pong.value <= 0 || (!cl.paused && !cl.match_pause_time) || !game_init) return;
	float sc = GetScale();
	player.y = Clamp((float)y / sc - player.h * 0.5f, 0.f, (scr_h / sc) - player.h);
}

/*
=================
Scope Overlay — woods #scope
=================

A circular vignette for zoom, using the stencil buffer to darken the screen outside
the reticle. It features a smooth fade-in/out after 50% zoom, an adaptive anti-aliased ring
*/

static void DrawScopeFadeQuad (float alpha)
{
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_ALPHA_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glColor4f(0.f, 0.f, 0.f, alpha);          // black with desired strength
	glBegin(GL_QUADS);
	glVertex2f(0.f, 0.f);
	glVertex2f(glwidth, 0.f);
	glVertex2f(glwidth, glheight);
	glVertex2f(0.f, glheight);
	glEnd();

	glColor4f(1.f, 1.f, 1.f, 1.f);            // restore white
}

static void DrawFilledCircle (float cx, float cy, float r, int segs)
{
	glBegin(GL_TRIANGLE_FAN);
	glVertex2f(cx, cy);           // centre
	for (int i = 0; i <= segs; ++i) {
		float a = (float)i / (float)segs * 2.f * (float)M_PI;
		glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
	}
	glEnd();
}

static int Scope_SegmentsForRadius (float r)
{
	/* circumference ≈ 2πr  →  segment count ≈ circumference / 2.5  */
	int segs = (int)(2.f * M_PI * r / 2.5f);

	/* keep it sane */
	if (segs < 64)   segs = 64;     /* never less than before   */
	if (segs > 512)  segs = 512;    /* don't blow the CPU/GPU   */
	return segs;
}

static void DrawScopeRing (float cx, float cy, float r, float thick)
{
	int   segs = Scope_SegmentsForRadius(r);
	float r_in = r - thick;
	float r_out = r;

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(0.f, 0.f, 0.f, 1.f);

	glBegin(GL_TRIANGLE_STRIP);
	for (int i = 0; i <= segs; ++i) {
		float a = (float)i / segs * 2.f * (float)M_PI;
		float ca = cosf(a), sa = sinf(a);

		glVertex2f(cx + ca * r_out, cy + sa * r_out);   /* outer edge */
		glVertex2f(cx + ca * r_in, cy + sa * r_in);   /* inner edge */
	}
	glEnd();
}


static void SCR_DrawScopeOverlay (void)
{
	GL_SetCanvas(CANVAS_DEFAULT);

	float r_frac = CLAMP(scr_scoperadius.value, 0.05f, 0.49f);
	float radius = r_frac * q_min((float)glwidth, (float)glheight);
	float cx = 0.5f * (float)glwidth;
	float cy = 0.5f * (float)glheight;

	static float fade_alpha = 0.f;          /* carries over frame-to-frame */

	float z = CLAMP(cl.zoom, 0.f, 1.f);     /* 0 → 1, already eases in engine */

	/* progress: 0 until zoom > 0.5, then maps linearly 0 → 1 */
	float prog = (z > 0.5f) ? (z - 0.5f) * 2.0f : 0.0f;   /* multiply by 2 = (z-0.5)/(1-0.5) */

	/* final target opacity (keep your existing scr_scopealpha cap) */
	float target_alpha = scr_scopealpha.value * prog;

	/* smoothing – chases target based on scr_scopefadespeed */
	float k_unclamped = CLAMP(0.1f, scr_scopefadespeed.value, 80.f) * host_frametime; // Corrected order
	float k = CLAMP(0.0f, k_unclamped, 0.99f); // Corrected order
	fade_alpha += (target_alpha - fade_alpha) * k;

	if (fade_alpha <= 0.004f && target_alpha <= 0.004f)
		return;                             /* nothing to draw */


	GLint bits;  glGetIntegerv(GL_STENCIL_BITS, &bits);
	if (!bits) { Con_Printf("No stencil buffer; scope fade disabled\n"); return; }

	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT |
		GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
		GL_TEXTURE_BIT);

	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);

	/*── PASS 1 : write 1s inside the circle ─────────────────────────────*/
	glEnable(GL_STENCIL_TEST);
	glStencilMask(0xFF);
	glClear(GL_STENCIL_BUFFER_BIT);

	glStencilFunc(GL_ALWAYS, 1, 0xFF);
	glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);

	/* make sure every fragment makes it to the stencil buffer */
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);

	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	DrawFilledCircle(cx, cy, radius, Scope_SegmentsForRadius(radius)); // Use adaptive segments for stencil

	/*── PASS 2 : darken where stencil == 0 (outside the glass) ─────────*/
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glStencilMask(0x00);                 /* read-only */
	glStencilFunc(GL_EQUAL, 0, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

	DrawScopeFadeQuad(fade_alpha); // Use fade_alpha here

	/*── PASS 3 : thin black ring (optional) ────────────────────────────*/
	glDisable(GL_STENCIL_TEST);
	// Draw the ring only when the fade effect is active and has reached its target opacity
	if (target_alpha > 0.01f && fabsf(target_alpha - fade_alpha) < 0.01f)
		DrawScopeRing(cx, cy, radius, 2.f);

	glPopAttrib();
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
	double diag_render_start = 0;
	qboolean diag_timing;
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

	diag_timing = (scr_diagnostics.value > 0); // woods #scr_diag


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

		if (diag_timing) // woods #scr_diag
		{
			if (scr_diagnostics.value >= 4)
				glFinish();
			diag_render_start = Sys_DoubleTime();
		}

		if (vid_fxaa.value > 0) // woods #fxaa #scr_diag
			FXAA_BeginFrame();

		if (cls.signon == SIGNONS||!cl.qcvm.extfuncs.CSQC_UpdateViewLoading)
			PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_UpdateView);
		else
			PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_UpdateViewLoading);

		if (vid_fxaa.value > 0) // woods #fxaa
			FXAA_EndFrame();

		if (diag_timing) // woods #scr_diag
		{
			if (scr_diagnostics.value >= 4)
				glFinish();
			scr_diag_render_ms = (Sys_DoubleTime() - diag_render_start) * 1000.0; // woods #scr_diag
		}

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

		if (diag_timing) // woods #scr_diag
		{
			if (scr_diagnostics.value >= 4)
				glFinish();
			diag_render_start = Sys_DoubleTime();
		}

		if (vid_fxaa.value > 0) // woods #fxaa begin FXAA frame for 3D rendering
			FXAA_BeginFrame();

		V_RenderView ();

		if (vid_fxaa.value > 0) // woods #fxaa end FXAA frame for 3D rendering
			FXAA_EndFrame();

		if (diag_timing) // woods #scr_diag
		{
			if (scr_diagnostics.value >= 4)
				glFinish();
			scr_diag_render_ms = (Sys_DoubleTime() - diag_render_start) * 1000.0; // woods #scr_diag
		}

		GL_Set2D (); // woods #scr_diag

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
		Pong_Update (); // woods #pong
		Pong_Draw (); // woods #pong
		SCR_CheckDrawCenterString ();
		SCR_DrawDevStats (); //johnfitz
		SCR_DrawFPS (); //johnfitz
		SCR_DrawClock (); //johnfitz
		SCR_DrawDemoControls(); // woods (iw) #democontrols
		SCR_ShowPing (); // woods #scrping
		SCR_ShowPL (); // woods #scrpl
		SCR_DrawDiagnostics (); // woods #scr_diag
		SCR_DrawMatchClock (); // woods #matchhud
		SCR_DrawMatchScores (); // woods #matchhud
		SCR_ShowFlagStatus (); // woods #matchhud #flagstatus
		SCR_ShowObsFrags (); // woods #observerhud
		SCR_DrawSpeed (); // woods #speed
		SCR_DrawMovementKeys (); // woods #movementkeys
		SCR_DrawGrenadeTimer(); // woods #nadecount
		TP_DrawClosestLocText (); // woods #locext
		SCR_DrawObsTimers (); // woods #obstimers
		SCR_Mute (); // woods #usermute
		SCR_Observing (); // woods
		TexturePointer_Draw (); // woods #texturepointer
		SCR_DrawScopeOverlay (); // woods #scope
		SCR_DrawConsole ();
		M_Draw ();
	}

	V_UpdateBlend (); //johnfitz -- V_UpdatePalette cleaned up and renamed

	GLSLGamma_GammaCorrect ();

	GL_EndRendering ();
}

//============================================================================
//
// FXAA IMPLEMENTATION -- woods #fxaa
//
//============================================================================

// FXAA quality presets (copied from gl_vidsdl.c)
typedef struct {
    float subpix;
    float edge;
} fxaa_quality_t;

/* 0 = off, 1-3 = presets */
static const fxaa_quality_t fxaa_presets[4] = {
    /* off   */ {0.00f, 0.00f},
    /* low   */ {0.25f, 0.333f},
    /* medium*/ {0.40f, 0.166f},   /* ← old defaults */
    /* high  */ {0.75f, 0.063f}
};

// FXAA structures and variables
typedef struct {
    GLuint framebuffer;
    GLuint color_texture;
    GLuint depth_renderbuffer;
    
    /* simple 5-tap shader (low / med) */
    GLuint program_simple;
    GLint u_tex_simple;
    GLint u_rcpFrame_simple;
    GLint u_subpix;
    GLint u_edge;
    
    /* FTE directional-search shader (high) */
    GLuint program_fte;
    GLint u_tex_fte;
    GLint u_rcpFrame_fte;
    
    int width, height;
    qboolean initialized;
    fxaa_quality_t current; // Current quality preset
} fxaa_t;

static fxaa_t fxaa;

// FXAA function declarations
void FXAA_Init(void);
void FXAA_Shutdown(void);
static GLuint FXAA_CreateShader_Simple(void);
static GLuint FXAA_CreateShader_FTE(void);
static qboolean FXAA_CreateFramebuffer(int width, int height);
void FXAA_VidFxaaChanged(cvar_t *v);

/*
===============
FXAA_VidFxaaChanged
===============
*/
void FXAA_VidFxaaChanged(cvar_t *v)
{
    /* clamp to [0‥3] so "vid_fxaa 99" doesn't explode */
    int lvl = (int)CLAMP(0, v->value, 3);
    if (lvl != v->value)
        Cvar_SetValueQuick(v, (float)lvl);

    /* turn resources on/off just like before */
    // If FXAA was disabled, clean up resources
    if (lvl == 0 && fxaa.initialized) {
        if (fxaa.framebuffer) {
            GL_DeleteFramebuffersFunc(1, &fxaa.framebuffer);
            fxaa.framebuffer = 0;
        }
        
        if (fxaa.color_texture) {
            glDeleteTextures(1, &fxaa.color_texture);
            fxaa.color_texture = 0;
        }
        
        if (fxaa.depth_renderbuffer) {
            GL_DeleteRenderbuffersFunc(1, &fxaa.depth_renderbuffer);
            fxaa.depth_renderbuffer = 0;
        }
        
        fxaa.width = fxaa.height = 0;
    }

    /* store current preset in the fxaa struct for fast access */
    fxaa.current = fxaa_presets[lvl];
}

/*
===============
FXAA_CvarClamp01
===============
*/
void FXAA_CvarClamp01(cvar_t *v)
{
    if (v->value < 0.0f)  Cvar_SetValueQuick(v, 0.0f);
    if (v->value > 1.0f)  Cvar_SetValueQuick(v, 1.0f);
}

/*
===============
FXAA_CvarChanged
===============
*/
void FXAA_CvarChanged(cvar_t *v)
{
    // If FXAA was disabled, clean up resources
    if (v->value <= 0.0f && fxaa.initialized) {
        if (fxaa.framebuffer) {
            GL_DeleteFramebuffersFunc(1, &fxaa.framebuffer);
            fxaa.framebuffer = 0;
        }
        
        if (fxaa.color_texture) {
            glDeleteTextures(1, &fxaa.color_texture);
            fxaa.color_texture = 0;
        }
        
        if (fxaa.depth_renderbuffer) {
            GL_DeleteRenderbuffersFunc(1, &fxaa.depth_renderbuffer);
            fxaa.depth_renderbuffer = 0;
        }
        
        fxaa.width = fxaa.height = 0;
    }
}

/*
===============
FXAA_CreateShader_Simple
===============
*/
static GLuint FXAA_CreateShader_Simple(void)
{
    const GLchar *vertSource = 
        "#version 110\n"
        "void main()\n"
        "{\n"
        "    gl_TexCoord[0] = gl_MultiTexCoord0;\n"
        "    gl_Position = gl_Vertex;\n"
        "}\n";
        
	const GLchar* fragSource =
		"#version 110\n"
		"uniform sampler2D u_tex;\n"
		"uniform vec2 u_rcpFrame;\n"
		"uniform float u_subpix;\n"
		"uniform float u_edge;\n"
		"\n"
		"void main()\n"
		"{\n"
		"    vec2 pos = gl_TexCoord[0].xy;\n"
		"    vec4 color = texture2D(u_tex, pos);\n"
	"    float alpha = color.a;\n"
		"    \n"
		"    // Enhanced FXAA implementation with quality parameters\n"
		"    vec4 nw = texture2D(u_tex, pos + vec2(-1.0, -1.0) * u_rcpFrame);\n"
		"    vec4 ne = texture2D(u_tex, pos + vec2(1.0, 1.0) * u_rcpFrame);\n"
		"    vec4 sw = texture2D(u_tex, pos + vec2(-1.0, 1.0) * u_rcpFrame);\n"
		"    vec4 se = texture2D(u_tex, pos + vec2(1.0, 1.0) * u_rcpFrame);\n"
		"    \n"
	"    vec4 blur = (nw + ne + sw + se + color) * 0.2;\n"
		"    \n"
		"    float luma_nw = dot(nw.rgb, vec3(0.299, 0.587, 0.114));\n"
		"    float luma_ne = dot(ne.rgb, vec3(0.299, 0.587, 0.114));\n"
		"    float luma_sw = dot(sw.rgb, vec3(0.299, 0.587, 0.114));\n"
		"    float luma_se = dot(se.rgb, vec3(0.299, 0.587, 0.114));\n"
	"    float luma_m = dot(color.rgb, vec3(0.299, 0.587, 0.114));\n"
		"    \n"
		"    float luma_min = min(luma_m, min(min(luma_nw, luma_ne), min(luma_sw, luma_se)));\n"
		"    float luma_max = max(luma_m, max(max(luma_nw, luma_ne), max(luma_sw, luma_se)));\n"
		"    \n"
		"    float contrast = luma_max - luma_min;\n"
		"    float threshold = max(0.0833, contrast * u_edge);\n"
		"    \n"
		"    if (contrast < threshold)\n"
		"        gl_FragColor = color;\n"
	"    else {\n"
	"        vec3 blended = mix(color.rgb, blur.rgb, u_subpix);\n"
	"        gl_FragColor = vec4(blended, alpha);\n"
	"    }\n"
		"}\n";
    
    if (!gl_glsl_able || !GL_CreateShaderFunc)
        return 0;
        
    GLuint vertShader = GL_CreateShaderFunc(GL_VERTEX_SHADER);
    GL_ShaderSourceFunc(vertShader, 1, &vertSource, NULL);
    GL_CompileShaderFunc(vertShader);
    
    GLint compiled;
    GL_GetShaderivFunc(vertShader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        Con_Printf("FXAA vertex shader compilation failed\n");
        GL_DeleteShaderFunc(vertShader);
        return 0;
    }
    
    GLuint fragShader = GL_CreateShaderFunc(GL_FRAGMENT_SHADER);
    GL_ShaderSourceFunc(fragShader, 1, &fragSource, NULL);
    GL_CompileShaderFunc(fragShader);
    
    GL_GetShaderivFunc(fragShader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        Con_Printf("FXAA fragment shader compilation failed\n");
        GL_DeleteShaderFunc(vertShader);
        GL_DeleteShaderFunc(fragShader);
        return 0;
    }
    
    GLuint program = GL_CreateProgramFunc();
    GL_AttachShaderFunc(program, vertShader);
    GL_AttachShaderFunc(program, fragShader);
    GL_LinkProgramFunc(program);
    
    GLint linked;
    GL_GetProgramivFunc(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        Con_Printf("FXAA shader linking failed\n");
        GL_DeleteShaderFunc(vertShader);
        GL_DeleteShaderFunc(fragShader);
        GL_DeleteProgramFunc(program);
        return 0;
    }
    
    GL_DeleteShaderFunc(vertShader);
    GL_DeleteShaderFunc(fragShader);
    
    return program;
}

/*
===============
FXAA_CreateShader_FTE
===============
*/
static GLuint FXAA_CreateShader_FTE(void)
{
    const GLchar *vertSource = 
        "#version 110\n"
        "void main()\n"
        "{\n"
        "    gl_TexCoord[0] = gl_MultiTexCoord0;\n"
        "    gl_Position = gl_Vertex;\n"
        "}\n";
        
    /* NOTE: uniform name matches the old code so we can reuse C-side plumbing */
    const GLchar *fragSource =
        "#version 110\n"
        "uniform sampler2D u_tex;\n"
        "uniform vec2      u_rcpFrame;\n"
        "\n"
        "void main(){\n"
        "  vec3 luma = vec3(0.299,0.587,0.114);\n"
        "  vec3 rgbNW = texture2D(u_tex, gl_TexCoord[0].xy + vec2(-1.0,-1.0)*u_rcpFrame).rgb;\n"
        "  vec3 rgbNE = texture2D(u_tex, gl_TexCoord[0].xy + vec2( 1.0,-1.0)*u_rcpFrame).rgb;\n"
        "  vec3 rgbSW = texture2D(u_tex, gl_TexCoord[0].xy + vec2(-1.0, 1.0)*u_rcpFrame).rgb;\n"
        "  vec3 rgbSE = texture2D(u_tex, gl_TexCoord[0].xy + vec2( 1.0, 1.0)*u_rcpFrame).rgb;\n"
    "  vec4 rgbaM = texture2D(u_tex, gl_TexCoord[0].xy);\n"
    "  vec3 rgbM  = rgbaM.rgb;\n"
    "  float alpha = rgbaM.a;\n"
        "\n"
        "  float lumaNW=dot(rgbNW,luma), lumaNE=dot(rgbNE,luma);\n"
        "  float lumaSW=dot(rgbSW,luma), lumaSE=dot(rgbSE,luma);\n"
        "  float lumaM =dot(rgbM ,luma);\n"
        "\n"
        "  float lumaMin=min(lumaM,min(min(lumaNW,lumaNE),min(lumaSW,lumaSE)));\n"
        "  float lumaMax=max(lumaM,max(max(lumaNW,lumaNE),max(lumaSW,lumaSE)));\n"
        "\n"
        "  vec2 dir;\n"
        "  dir.x = -((lumaNW+lumaNE) - (lumaSW+lumaSE));\n"
        "  dir.y =  ((lumaNW+lumaSW) - (lumaNE+lumaSE));\n"
        "\n"
        "  float FXAA_REDUCE_MUL = 1.0/8.0;\n"
        "  float FXAA_REDUCE_MIN = 1.0/128.0;\n"
        "  float FXAA_SPAN_MAX   = 8.0;\n"
        "\n"
        "  float dirReduce = max((lumaNW+lumaNE+lumaSW+lumaSE)* (0.25*FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);\n"
        "  float rcpDirMin = 1.0/(min(abs(dir.x),abs(dir.y))+dirReduce);\n"
        "  dir = clamp(dir*rcpDirMin, vec2(-FXAA_SPAN_MAX), vec2(FXAA_SPAN_MAX))*u_rcpFrame;\n"
        "\n"
        "  vec3 rgbA = 0.5*(texture2D(u_tex, gl_TexCoord[0].xy + dir*(1.0/3.0-0.5)).rgb +\n"
        "                   texture2D(u_tex, gl_TexCoord[0].xy + dir*(2.0/3.0-0.5)).rgb);\n"
        "  vec3 rgbB = 0.25*(texture2D(u_tex, gl_TexCoord[0].xy + dir*(-0.5)).rgb +\n"
        "                    texture2D(u_tex, gl_TexCoord[0].xy + dir*( 0.5)).rgb) + 0.5*rgbA;\n"
        "  float lumaB = dot(rgbB,luma);\n"
    "  vec3 rgbOut = ((lumaB < lumaMin)||(lumaB > lumaMax)) ? rgbA : rgbB;\n"
    "  gl_FragColor = vec4(rgbOut, alpha);\n"
        "}\n";
    
    if (!gl_glsl_able || !GL_CreateShaderFunc)
        return 0;
        
    GLuint vertShader = GL_CreateShaderFunc(GL_VERTEX_SHADER);
    GL_ShaderSourceFunc(vertShader, 1, &vertSource, NULL);
    GL_CompileShaderFunc(vertShader);
    
    GLint compiled;
    GL_GetShaderivFunc(vertShader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        Con_Printf("FXAA FTE vertex shader compilation failed\n");
        GL_DeleteShaderFunc(vertShader);
        return 0;
    }
    
    GLuint fragShader = GL_CreateShaderFunc(GL_FRAGMENT_SHADER);
    GL_ShaderSourceFunc(fragShader, 1, &fragSource, NULL);
    GL_CompileShaderFunc(fragShader);
    
    GL_GetShaderivFunc(fragShader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        Con_Printf("FXAA FTE fragment shader compilation failed\n");
        GL_DeleteShaderFunc(vertShader);
        GL_DeleteShaderFunc(fragShader);
        return 0;
    }
    
    GLuint program = GL_CreateProgramFunc();
    GL_AttachShaderFunc(program, vertShader);
    GL_AttachShaderFunc(program, fragShader);
    GL_LinkProgramFunc(program);
    
    GLint linked;
    GL_GetProgramivFunc(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        Con_Printf("FXAA FTE shader linking failed\n");
        GL_DeleteShaderFunc(vertShader);
        GL_DeleteShaderFunc(fragShader);
        GL_DeleteProgramFunc(program);
        return 0;
    }
    
    GL_DeleteShaderFunc(vertShader);
    GL_DeleteShaderFunc(fragShader);
    
    return program;
}

/*
===============
FXAA_Init
===============
*/
void FXAA_Init(void)
{
    if (!gl_fbo_able || !gl_glsl_able || fxaa.initialized)
        return;
        
    memset(&fxaa, 0, sizeof(fxaa));
    
    fxaa.program_simple = FXAA_CreateShader_Simple();
    if (!fxaa.program_simple) {
        Con_DPrintf("FXAA: Failed to create simple shader\n");
        return;
    }
    
    fxaa.program_fte = FXAA_CreateShader_FTE();
    if (!fxaa.program_fte) {
        Con_Printf("FXAA: Failed to create FTE shader\n");
        return;
    }
    
    GL_UseProgramFunc(fxaa.program_simple);
    fxaa.u_tex_simple = GL_GetUniformLocationFunc(fxaa.program_simple, "u_tex");
    fxaa.u_rcpFrame_simple = GL_GetUniformLocationFunc(fxaa.program_simple, "u_rcpFrame");
    fxaa.u_subpix = GL_GetUniformLocationFunc(fxaa.program_simple, "u_subpix");
    fxaa.u_edge = GL_GetUniformLocationFunc(fxaa.program_simple, "u_edge");
    GL_UseProgramFunc(0);
    
    GL_UseProgramFunc(fxaa.program_fte);
    fxaa.u_tex_fte = GL_GetUniformLocationFunc(fxaa.program_fte, "u_tex");
    fxaa.u_rcpFrame_fte = GL_GetUniformLocationFunc(fxaa.program_fte, "u_rcpFrame");
    GL_UseProgramFunc(0);
    
    fxaa.initialized = true;
    Con_DPrintf("FXAA initialized\n");
    
    // Disable MSAA if both are enabled
    if (vid_fsaa.value > 0 && vid_fxaa.value > 0) {
        Con_DPrintf("FXAA: Disabling MSAA (vid_fsaa) as FXAA is more efficient\n");
        Cvar_SetQuick(&vid_fsaa, "0");
    }
}

/*
===============
FXAA_Shutdown
===============
*/
void FXAA_Shutdown(void)
{
    if (!fxaa.initialized)
        return;
        
    if (fxaa.framebuffer) {
        GL_DeleteFramebuffersFunc(1, &fxaa.framebuffer);
        fxaa.framebuffer = 0;
    }
    
    if (fxaa.color_texture) {
        glDeleteTextures(1, &fxaa.color_texture);
        fxaa.color_texture = 0;
    }
    
    if (fxaa.depth_renderbuffer) {
        GL_DeleteRenderbuffersFunc(1, &fxaa.depth_renderbuffer);
        fxaa.depth_renderbuffer = 0;
    }
    
    if (fxaa.program_simple) {
        GL_DeleteProgramFunc(fxaa.program_simple);
        fxaa.program_simple = 0;
    }
    
    if (fxaa.program_fte) {
        GL_DeleteProgramFunc(fxaa.program_fte);
        fxaa.program_fte = 0;
    }
    
    fxaa.initialized = false;
}

/*
===============
FXAA_CreateFramebuffer
===============
*/
static qboolean FXAA_CreateFramebuffer(int width, int height)
{
    if (!fxaa.initialized)
        return false;
        
    // Clean up old framebuffer if size changed
    if (fxaa.width != width || fxaa.height != height) {
        if (fxaa.framebuffer) {
            GL_DeleteFramebuffersFunc(1, &fxaa.framebuffer);
            fxaa.framebuffer = 0;
        }
        
        if (fxaa.color_texture) {
            glDeleteTextures(1, &fxaa.color_texture);
            fxaa.color_texture = 0;
        }
        
        if (fxaa.depth_renderbuffer) {
            GL_DeleteRenderbuffersFunc(1, &fxaa.depth_renderbuffer);
            fxaa.depth_renderbuffer = 0;
        }
    }
    
    if (!fxaa.framebuffer) {
        // Create color texture
        glGenTextures(1, &fxaa.color_texture);
        glBindTexture(GL_TEXTURE_2D, fxaa.color_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        // Create depth-stencil renderbuffer (combined for outline/shell effects)
        GL_GenRenderbuffersFunc(1, &fxaa.depth_renderbuffer);
        GL_BindRenderbufferFunc(GL_RENDERBUFFER, fxaa.depth_renderbuffer);
        GL_RenderbufferStorageFunc(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        
        // Create framebuffer
        GL_GenFramebuffersFunc(1, &fxaa.framebuffer);
        GL_BindFramebufferFunc(GL_FRAMEBUFFER, fxaa.framebuffer);
        GL_FramebufferTexture2DFunc(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fxaa.color_texture, 0);
        GL_FramebufferRenderbufferFunc(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fxaa.depth_renderbuffer);
        GL_FramebufferRenderbufferFunc(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fxaa.depth_renderbuffer);
        
        GLenum status = GL_CheckFramebufferStatusFunc(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            Con_DPrintf("FXAA: Framebuffer incomplete (status: 0x%x) - disabling\n", status);
            SCR_CenterPrint("FXAA unavailable (driver bug)");
            Cvar_SetQuick(&vid_fxaa, "0");
            GL_BindFramebufferFunc(GL_FRAMEBUFFER, 0);
            return false;
        }
        
        GL_BindFramebufferFunc(GL_FRAMEBUFFER, 0);
        
        fxaa.width = width;
        fxaa.height = height;
    }
    
    return true;
}

/*
===============
FXAA_BeginFrame
===============
*/
void FXAA_BeginFrame(void)
{
    if (!fxaa.initialized || vid_fxaa.value <= 0 || !gl_fbo_able || !gl_glsl_able)
        return;
        
    if (!FXAA_CreateFramebuffer(glwidth, glheight))
        return;
        
    GL_BindFramebufferFunc(GL_FRAMEBUFFER, fxaa.framebuffer);
    glViewport(0, 0, glwidth, glheight);
    
    // Clear with alpha = 0 to support transparency detection
    {
        byte *rgb;
        int s = (int)r_clearcolor.value & 0xFF;
        rgb = (byte *)(d_8to24table + s);
        glClearColor(rgb[0]/255.0f, rgb[1]/255.0f, rgb[2]/255.0f, 0.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

/*
===============
FXAA_EndFrame
===============
*/
void FXAA_EndFrame(void)
{
    if (!fxaa.initialized || vid_fxaa.value <= 0 || !fxaa.framebuffer)
        return;
        
    // Save current state
    GLboolean wasBlend = glIsEnabled(GL_BLEND);
    GLboolean wasAlphaTest = glIsEnabled(GL_ALPHA_TEST);
    GLboolean wasDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean wasCullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean wasScissorTest = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean wasTexture2D = glIsEnabled(GL_TEXTURE_2D);
    GLint polygonMode[2];
    glGetIntegerv(GL_POLYGON_MODE, polygonMode);
    GLint activeTexture = GL_TEXTURE0;
    if (GL_SelectTextureFunc)
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    GLint boundTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture);
        
    // Bind back buffer
    GL_BindFramebufferFunc(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, glwidth, glheight);
    
    // Clear and set up for fullscreen quad
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    if (!wasTexture2D)
        glEnable(GL_TEXTURE_2D);
    glDisable(GL_SCISSOR_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    
    // Choose shader based on vid_fxaa value
    GLuint prog = (vid_fxaa.value == 3) ? fxaa.program_fte : fxaa.program_simple;
    GL_UseProgramFunc(prog);
    
    // Bind texture and set uniforms
    if (GL_SelectTextureFunc)
    GL_SelectTextureFunc(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fxaa.color_texture);
    
    if (prog == fxaa.program_fte) {
        GL_Uniform1iFunc(fxaa.u_tex_fte, 0);
        GL_Uniform2fFunc(fxaa.u_rcpFrame_fte, 1.0f / (float)glwidth, 1.0f / (float)glheight);
    } else {
        GL_Uniform1iFunc(fxaa.u_tex_simple, 0);
        GL_Uniform2fFunc(fxaa.u_rcpFrame_simple, 1.0f / (float)glwidth, 1.0f / (float)glheight);
        GL_Uniform1fFunc(fxaa.u_subpix, fxaa.current.subpix);
        GL_Uniform1fFunc(fxaa.u_edge, fxaa.current.edge);
    }

    // Draw fullscreen quad
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, 1.0f);
    glEnd();
    
    // Cleanup
    GL_UseProgramFunc(0);
    if (GL_SelectTextureFunc) {
        if (activeTexture != GL_TEXTURE0)
            GL_SelectTextureFunc((GLenum)activeTexture);
        glBindTexture(GL_TEXTURE_2D, (GLuint)boundTexture);
        if (activeTexture != GL_TEXTURE0)
            GL_SelectTextureFunc(GL_TEXTURE0);
    } else {
        glBindTexture(GL_TEXTURE_2D, (GLuint)boundTexture);
    }
    
    // Restore state
    if (wasDepthTest) glEnable(GL_DEPTH_TEST);
    if (wasCullFace) glEnable(GL_CULL_FACE);
    if (wasBlend) glEnable(GL_BLEND);
    if (wasAlphaTest) glEnable(GL_ALPHA_TEST);
    if (wasScissorTest) glEnable(GL_SCISSOR_TEST);
    if (!wasTexture2D)
        glDisable(GL_TEXTURE_2D);
    if (GL_SelectTextureFunc && activeTexture != GL_TEXTURE0)
        GL_SelectTextureFunc((GLenum)activeTexture);
    glPolygonMode(GL_FRONT, polygonMode[0]);
    glPolygonMode(GL_BACK, polygonMode[1]);
}