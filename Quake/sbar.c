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
// sbar.c -- status bar code

#include "quakedef.h"

extern	qboolean premul_hud;
int		sb_updates;		// if >= vid.numpages, no update needed

extern int	maptime; // woods connected map time #maptime
extern double  mpservertime;	// woods #servertime
extern char mute[2];			// woods for mute to memory #usermute
int	fragsort[MAX_SCOREBOARD]; // woods #scrping
int	scoreboardlines; // woods #scrping
extern char	lastmphost[NET_NAMELEN]; // woods

#define STAT_MINUS		10	// num frame for '-' stats digit

qpic_t		*sb_nums[2][11];
qpic_t		*sb_colon, *sb_slash;
qpic_t		*sb_ibar;
qpic_t		*sb_sbar;
qpic_t		*sb_scorebar;

qpic_t		*sb_weapons[7][8];   // 0 is active, 1 is owned, 2-5 are flashes
qpic_t		*sb_ammo[4];
qpic_t		*sb_sigil[4];
qpic_t		*sb_armor[3];
qpic_t		*sb_items[32];

qpic_t		*sb_faces[7][2];		// 0 is gibbed, 1 is dead, 2-6 are alive
							// 0 is static, 1 is temporary animation
qpic_t		*sb_face_invis;
qpic_t		*sb_face_quad;
qpic_t		*sb_face_invuln;
qpic_t		*sb_face_invis_invuln;

qboolean	sb_showscores;

int		sb_lines;			// scan lines to draw

qpic_t		*rsb_invbar[2];
qpic_t		*rsb_weapons[5];
qpic_t		*rsb_items[2];
qpic_t		*rsb_ammo[3];
qpic_t		*rsb_teambord;		// PGM 01/19/97 - team color border

//MED 01/04/97 added two more weapons + 3 alternates for grenade launcher
qpic_t		*hsb_weapons[7][5];   // 0 is active, 1 is owned, 2-5 are flashes
//MED 01/04/97 added array to simplify weapon parsing
int		hipweapons[4] = {HIT_LASER_CANNON_BIT,HIT_MJOLNIR_BIT,4,HIT_PROXIMITY_GUN_BIT};
//MED 01/04/97 added hipnotic items array
qpic_t		*hsb_items[2];

//spike -- fix -game hipnotic by autodetecting hud types. the fte protocols will deal with the networking issue, other than demos, anyway
static int hudtype;
#define hipnotic (hudtype==1)
#define rogue (hudtype==2)

void Sbar_MiniDeathmatchOverlay (void);
void Sbar_DeathmatchOverlay (void);
void M_DrawPic (int x, int y, qpic_t *pic);
void Draw_SubPic_QW (int x, int y, qpic_t* pic, int ofsx, int ofsy, int w, int h); // woods #sbarstyles for qw hud
extern cvar_t scr_showspeed; // woods
extern cvar_t scr_sbarfacecolor; // #teamface

qboolean Sbar_CSQCCommand(void)
{
	qboolean ret = false;
	if (cl.qcvm.extfuncs.CSQC_ConsoleCommand)
	{
		PR_SwitchQCVM(&cl.qcvm);
		G_INT(OFS_PARM0) = PR_MakeTempString(Cmd_Argv(0));
		PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_ConsoleCommand);
		ret = G_FLOAT(OFS_RETURN);
		PR_SwitchQCVM(NULL);
	}
	return ret;
}

/*
===============
Sbar_ShowScores

Tab key down
===============
*/
void Sbar_ShowScores (void)
{
	Sbar_CSQCCommand();
	if (sb_showscores)
		return;
	sb_showscores = true;
	sb_updates = 0;
}

/*
===============
Sbar_DontShowScores

Tab key up
===============
*/
void Sbar_DontShowScores (void)
{
	Sbar_CSQCCommand();
	sb_showscores = false;
	sb_updates = 0;
}

/*
===============
Sbar_Changed
===============
*/
void Sbar_Changed (void)
{
	sb_updates = 0;	// update next frame
}


qpic_t *Sbar_CheckPicFromWad (const char *name)
{
	extern qpic_t *pic_nul;
	qpic_t *r;
	lumpinfo_t *info;
	if (!hudtype)
		return pic_nul;	//one already failed, don't waste cpu
	if (!W_GetLumpName(name, &info))
		r = pic_nul;
	else
		r = Draw_PicFromWad(name);
	if (r == pic_nul)
		hudtype = 0;
	return r;
}
/*
===============
Sbar_LoadPics -- johnfitz -- load all the sbar pics
===============
*/
void Sbar_LoadPics (void)
{
	int		i;

	for (i = 0; i < 10; i++)
	{
		sb_nums[0][i] = Draw_PicFromWad (va("num_%i",i));
		sb_nums[1][i] = Draw_PicFromWad (va("anum_%i",i));
	}

	sb_nums[0][10] = Draw_PicFromWad ("num_minus");
	sb_nums[1][10] = Draw_PicFromWad ("anum_minus");

	sb_colon = Draw_PicFromWad ("num_colon");
	sb_slash = Draw_PicFromWad ("num_slash");

	sb_weapons[0][0] = Draw_PicFromWad2("inv_shotgun", TEXPREF_NEAREST | TEXPREF_ALPHA);
	sb_weapons[0][1] = Draw_PicFromWad2("inv_sshotgun", TEXPREF_NEAREST | TEXPREF_ALPHA);
	sb_weapons[0][2] = Draw_PicFromWad2("inv_nailgun", TEXPREF_NEAREST | TEXPREF_ALPHA);
	sb_weapons[0][3] = Draw_PicFromWad2("inv_snailgun", TEXPREF_NEAREST | TEXPREF_ALPHA);
	sb_weapons[0][4] = Draw_PicFromWad2("inv_rlaunch", TEXPREF_NEAREST | TEXPREF_ALPHA);
	sb_weapons[0][5] = Draw_PicFromWad2("inv_srlaunch", TEXPREF_NEAREST | TEXPREF_ALPHA);
	sb_weapons[0][6] = Draw_PicFromWad2("inv_lightng", TEXPREF_NEAREST | TEXPREF_ALPHA);

	sb_weapons[1][0] = Draw_PicFromWad2("inv2_shotgun", TEXPREF_NEAREST | TEXPREF_ALPHA);
	sb_weapons[1][1] = Draw_PicFromWad2("inv2_sshotgun", TEXPREF_NEAREST | TEXPREF_ALPHA);
	sb_weapons[1][2] = Draw_PicFromWad2("inv2_nailgun", TEXPREF_NEAREST | TEXPREF_ALPHA);
	sb_weapons[1][3] = Draw_PicFromWad2("inv2_snailgun", TEXPREF_NEAREST | TEXPREF_ALPHA);
	sb_weapons[1][4] = Draw_PicFromWad2("inv2_rlaunch", TEXPREF_NEAREST | TEXPREF_ALPHA);
	sb_weapons[1][5] = Draw_PicFromWad2("inv2_srlaunch", TEXPREF_NEAREST | TEXPREF_ALPHA);
	sb_weapons[1][6] = Draw_PicFromWad2("inv2_lightng", TEXPREF_NEAREST| TEXPREF_ALPHA);

	for (i = 0; i < 5; i++)
	{
		sb_weapons[2+i][0] = Draw_PicFromWad (va("inva%i_shotgun",i+1));
		sb_weapons[2+i][1] = Draw_PicFromWad (va("inva%i_sshotgun",i+1));
		sb_weapons[2+i][2] = Draw_PicFromWad (va("inva%i_nailgun",i+1));
		sb_weapons[2+i][3] = Draw_PicFromWad (va("inva%i_snailgun",i+1));
		sb_weapons[2+i][4] = Draw_PicFromWad (va("inva%i_rlaunch",i+1));
		sb_weapons[2+i][5] = Draw_PicFromWad (va("inva%i_srlaunch",i+1));
		sb_weapons[2+i][6] = Draw_PicFromWad (va("inva%i_lightng",i+1));
	}

	sb_ammo[0] = Draw_PicFromWad ("sb_shells");
	sb_ammo[1] = Draw_PicFromWad ("sb_nails");
	sb_ammo[2] = Draw_PicFromWad ("sb_rocket");
	sb_ammo[3] = Draw_PicFromWad ("sb_cells");

	sb_armor[0] = Draw_PicFromWad ("sb_armor1");
	sb_armor[1] = Draw_PicFromWad ("sb_armor2");
	sb_armor[2] = Draw_PicFromWad ("sb_armor3");

	sb_items[0] = Draw_PicFromWad ("sb_key1");
	sb_items[1] = Draw_PicFromWad ("sb_key2");
	sb_items[2] = Draw_PicFromWad ("sb_invis");
	sb_items[3] = Draw_PicFromWad ("sb_invuln");
	sb_items[4] = Draw_PicFromWad ("sb_suit");
	sb_items[5] = Draw_PicFromWad ("sb_quad");

	sb_sigil[0] = Draw_PicFromWad ("sb_sigil1");
	sb_sigil[1] = Draw_PicFromWad ("sb_sigil2");
	sb_sigil[2] = Draw_PicFromWad ("sb_sigil3");
	sb_sigil[3] = Draw_PicFromWad ("sb_sigil4");

	sb_faces[4][0] = Draw_PicFromWad ("face1");
	sb_faces[4][1] = Draw_PicFromWad ("face_p1");
	sb_faces[3][0] = Draw_PicFromWad ("face2");
	sb_faces[3][1] = Draw_PicFromWad ("face_p2");
	sb_faces[2][0] = Draw_PicFromWad ("face3");
	sb_faces[2][1] = Draw_PicFromWad ("face_p3");
	sb_faces[1][0] = Draw_PicFromWad ("face4");
	sb_faces[1][1] = Draw_PicFromWad ("face_p4");
	sb_faces[0][0] = Draw_PicFromWad ("face5");
	sb_faces[0][1] = Draw_PicFromWad ("face_p5");

	sb_face_invis = Draw_PicFromWad ("face_invis");
	sb_face_invuln = Draw_PicFromWad ("face_invul2");
	sb_face_invis_invuln = Draw_PicFromWad ("face_inv2");
	sb_face_quad = Draw_PicFromWad ("face_quad");

	sb_sbar = Draw_PicFromWad2 ("sbar", TEXPREF_PAD|TEXPREF_NOPICMIP);
	sb_ibar = Draw_PicFromWad2 ("ibar", TEXPREF_PAD|TEXPREF_NOPICMIP);
	sb_scorebar = Draw_PicFromWad ("scorebar");

	hudtype = 0;

//MED 01/04/97 added new hipnotic weapons
	if (!hudtype)
	{
		hudtype = 1;
		hsb_weapons[0][0] = Sbar_CheckPicFromWad ("inv_laser");
		hsb_weapons[0][1] = Sbar_CheckPicFromWad ("inv_mjolnir");
		hsb_weapons[0][2] = Sbar_CheckPicFromWad ("inv_gren_prox");
		hsb_weapons[0][3] = Sbar_CheckPicFromWad ("inv_prox_gren");
		hsb_weapons[0][4] = Sbar_CheckPicFromWad ("inv_prox");

		hsb_weapons[1][0] = Sbar_CheckPicFromWad ("inv2_laser");
		hsb_weapons[1][1] = Sbar_CheckPicFromWad ("inv2_mjolnir");
		hsb_weapons[1][2] = Sbar_CheckPicFromWad ("inv2_gren_prox");
		hsb_weapons[1][3] = Sbar_CheckPicFromWad ("inv2_prox_gren");
		hsb_weapons[1][4] = Sbar_CheckPicFromWad ("inv2_prox");

		for (i = 0; i < 5; i++)
		{
			hsb_weapons[2+i][0] = Sbar_CheckPicFromWad (va("inva%i_laser",i+1));
			hsb_weapons[2+i][1] = Sbar_CheckPicFromWad (va("inva%i_mjolnir",i+1));
			hsb_weapons[2+i][2] = Sbar_CheckPicFromWad (va("inva%i_gren_prox",i+1));
			hsb_weapons[2+i][3] = Sbar_CheckPicFromWad (va("inva%i_prox_gren",i+1));
			hsb_weapons[2+i][4] = Sbar_CheckPicFromWad (va("inva%i_prox",i+1));
		}

		hsb_items[0] = Sbar_CheckPicFromWad ("sb_wsuit");
		hsb_items[1] = Sbar_CheckPicFromWad ("sb_eshld");
	}

	if (!hudtype)
	{
		hudtype = 2;
		rsb_invbar[0] = Sbar_CheckPicFromWad ("r_invbar1");
		rsb_invbar[1] = Sbar_CheckPicFromWad ("r_invbar2");

		rsb_weapons[0] = Sbar_CheckPicFromWad ("r_lava");
		rsb_weapons[1] = Sbar_CheckPicFromWad ("r_superlava");
		rsb_weapons[2] = Sbar_CheckPicFromWad ("r_gren");
		rsb_weapons[3] = Sbar_CheckPicFromWad ("r_multirock");
		rsb_weapons[4] = Sbar_CheckPicFromWad ("r_plasma");

		rsb_items[0] = Sbar_CheckPicFromWad ("r_shield1");
		rsb_items[1] = Sbar_CheckPicFromWad ("r_agrav1");

// PGM 01/19/97 - team color border
		rsb_teambord = Sbar_CheckPicFromWad ("r_teambord");
// PGM 01/19/97 - team color border

		rsb_ammo[0] = Sbar_CheckPicFromWad ("r_ammolava");
		rsb_ammo[1] = Sbar_CheckPicFromWad ("r_ammomulti");
		rsb_ammo[2] = Sbar_CheckPicFromWad ("r_ammoplasma");
	}
}

/*
===============
Sbar_Init -- johnfitz -- rewritten
===============
*/
void Sbar_Init (void)
{
	Cmd_AddCommand ("+showscores", Sbar_ShowScores);
	Cmd_AddCommand ("-showscores", Sbar_DontShowScores);

	Sbar_LoadPics ();
}


//=============================================================================

// drawing routines are relative to the status bar location

/*
=============
Sbar_DrawPic -- johnfitz -- rewritten now that GL_SetCanvas is doing the work
=============
*/
void Sbar_DrawPic (int x, int y, qpic_t *pic)
{
	Draw_Pic (x, y + 24, pic);
}

/*
=============
Sbar_DrawSubPicAlpha -- // woods #sbarstyles
=============
*/
void Sbar_DrawSubPicAlpha(int x, int y, qpic_t* pic, int ofsx, int ofsy, int w, int h, float alpha)
{
	glDisable(GL_ALPHA_TEST);
	glEnable(GL_BLEND);
	glColor4f(1, 1, 1, alpha);
	Draw_SubPic_QW(x, y + 24, pic, ofsx, ofsy, w, h);
	glColor4f(1, 1, 1, 1); // ericw -- changed from glColor3f to work around intel 855 bug with "r_oldwater 0" and "scr_sbaralpha 0"
	glDisable(GL_BLEND);
	glEnable(GL_ALPHA_TEST);
}

/*
=============
Sbar_DrawPicAlpha -- johnfitz
=============
*/
void Sbar_DrawPicAlpha (int x, int y, qpic_t *pic, float alpha)
{
	if (premul_hud)
	{
		glColor4f(alpha,alpha,alpha,alpha);
		Draw_Pic (x, y + 24, pic);
		glColor4f(1,1,1,1);
	}
	else
	{
		glDisable (GL_ALPHA_TEST);
		glEnable (GL_BLEND);
		glColor4f(1,1,1,alpha);
		Draw_Pic (x, y + 24, pic);
		glColor4f(1,1,1,1); // ericw -- changed from glColor3f to work around intel 855 bug with "r_oldwater 0" and "scr_sbaralpha 0"
		glDisable (GL_BLEND);
		glEnable (GL_ALPHA_TEST);
	}
}

/*
================
Sbar_DrawCharacter -- johnfitz -- rewritten now that GL_SetCanvas is doing the work
================
*/
void Sbar_DrawCharacter (int x, int y, int num)
{
	Draw_Character (x, y + 24, num);
}

/*
================
Sbar_DrawString -- johnfitz -- rewritten now that GL_SetCanvas is doing the work
================
*/
void Sbar_DrawString (int x, int y, const char *str)
{
	Draw_String (x, y + 24, str);
}

/*
===============
Sbar_DrawScrollString -- johnfitz

scroll the string inside a glscissor region -- woods added 5 pixel margins, etc
===============
*/
void Sbar_DrawScrollString (int x, int y, int width, const char *str)
{
	float scale;
	int strLen, totalLen, ofs, left;

	scale = CLAMP (1.0f, scr_sbarscale.value, (float)glwidth / 320.0f);
	left = (x + 5) * scale;
	//if (cl.gametype != GAME_DEATHMATCH) // woods sbar now middle default
		left += (((float)glwidth - 320.0 * scale) / 2);

	glEnable (GL_SCISSOR_TEST);
	glScissor (left, 0, (width - 10) * scale, glheight);

	strLen = strlen(str) * 8;
	totalLen = strLen + 3 * 8;  // text width + " - " divider width

	ofs = ((int)(realtime * 30)) % totalLen;

	Sbar_DrawString (x - ofs + 5, y, str);
	Sbar_DrawString (x - ofs + 5 + strLen, y, " Ž ");
	Sbar_DrawString (x - ofs + 5 + totalLen, y, str);

	glDisable (GL_SCISSOR_TEST);
}

/*
=============
Sbar_itoa
=============
*/
int Sbar_itoa (int num, char *buf)
{
	char	*str;
	int	pow10;
	int	dig;

	str = buf;

	if (num < 0)
	{
		*str++ = '-';
		num = -num;
	}

	for (pow10 = 10 ; num >= pow10 ; pow10 *= 10)
		;

	do
	{
		pow10 /= 10;
		dig = num/pow10;
		*str++ = '0'+dig;
		num -= dig*pow10;
	} while (pow10 != 1);

	*str = 0;

	return str-buf;
}


/*
=============
Sbar_DrawNum
=============
*/
void Sbar_DrawNum (int x, int y, int num, int digits, int color)
{
	char	str[12];
	char	*ptr;
	int	l, frame;

	num = q_min(999,num); //johnfitz -- cap high values rather than truncating number

	l = Sbar_itoa (num, str);
	ptr = str;
	if (l > digits)
		ptr += (l-digits);
	if (l < digits)
		x += (digits-l)*24;

	while (*ptr)
	{
		if (*ptr == '-')
			frame = STAT_MINUS;
		else
			frame = *ptr -'0';

		Sbar_DrawPic (x,y,sb_nums[color][frame]); //johnfitz -- DrawTransPic is obsolete
		x += 24;
		ptr++;
	}
}

/*
===============
Sbar_SortFrags
===============
*/
void Sbar_SortFrags (qboolean ignorespecs)
{
	int		i, j, k;
	int		w1,w2;

// sort by frags
	scoreboardlines = 0;
	for (i = 0; i < cl.maxclients; i++)
	{
		if (cl.scores[i].name[0])
		{
			if (cl.scores[i].spectator==1 && ignorespecs)
				continue;
			fragsort[scoreboardlines] = i;
			scoreboardlines++;
		}
	}

	for (i = 0; i < scoreboardlines; i++)
	{
		for (j = 0; j < scoreboardlines - 1 - i; j++)
		{	//spike: adding this so spectators end up at the bottom.
			w1 = cl.scores[fragsort[j+0]].spectator!=1, w2 = cl.scores[fragsort[j+1]].spectator!=1;	//sort by spectatorness...
			if (w1==w2)
				w1 = cl.scores[fragsort[j+0]].frags, w2 = cl.scores[fragsort[j+1]].frags;		//then by frags
			if (w1 < w2)
			{
				k = fragsort[j];
				fragsort[j] = fragsort[j+1];
				fragsort[j+1] = k;
			}
		}
	}
}

/*
===============
Sbar_SortFrags_Obs -- woods for vertical upwards sorting #observerhud
===============
*/
void Sbar_SortFrags_Obs(void)
{
	int		i, j, k;

// sort by frags
	scoreboardlines = 0;
	for (i = 0; i < cl.maxclients; i++)
	{
		if (cl.scores[i].name[0])
		{
			fragsort[scoreboardlines] = i;
			scoreboardlines++;
		}
	}

	for (i = 0; i < scoreboardlines; i++)
	{
		for (j = 0; j < scoreboardlines - 1 - i; j++)
		{
			if (cl.scores[fragsort[j]].frags > cl.scores[fragsort[j+1]].frags) // woods '>'
			{
				k = fragsort[j];
				fragsort[j] = fragsort[j+1];
				fragsort[j+1] = k;
			}
		}
	}
}

/* JPG - added this for teamscores in default status bar // woods #pqteam
==================
Sbar_SortTeamFrags
==================
*/
void Sbar_SortTeamFrags(void)
{
	int		i, j, k;

	// sort by frags
	scoreboardlines = 0;
	for (i = 0; i < 14; i++)
	{
		if (cl.teamscores[i].colors)
		{
			fragsort[scoreboardlines] = i;
			scoreboardlines++;
		}
	}

	for (i = 0; i < scoreboardlines; i++)
		for (j = 0; j < scoreboardlines - 1 - i; j++)
			if (cl.teamscores[fragsort[j]].frags < cl.teamscores[fragsort[j + 1]].frags)
			{
				k = fragsort[j];
				fragsort[j] = fragsort[j + 1];
				fragsort[j + 1] = k;
			}
}

int	Sbar_ColorForMap (int m)
{
	return m < 128 ? m + 8 : m + 8;
}

/*
===============
Sbar_SoloScoreboard -- johnfitz -- new layout
===============
*/
void Sbar_SoloScoreboard (void)
{
	char	str[256];
	int minutes, seconds, tens, units;
	int	min, smin, cmin, ticks;
	int	len, ct, pl, st, mpc; // woods ct, pl

	if (cl.gametype != GAME_DEATHMATCH)  // woods only in singleplayer
	{
		sprintf(str, "Kills: %i/%i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);
		Sbar_DrawString(8, 12, str);

		sprintf(str, "Secrets: %i/%i", cl.stats[STAT_SECRETS], cl.stats[STAT_TOTALSECRETS]);
		Sbar_DrawString(312 - strlen(str) * 8, 12, str);
	}

	else // woods add various times + PL
	{

		ticks = SDL_GetTicks();
		ct = ticks - maptime; // map connected time
		st = ticks - mpservertime;
		mpc = ticks / 1000; // client open time
		pl = cl.pltotal;
		
		min = ct / 60000;
		smin = st / 60000;
		cmin = mpc / 60;

		sprintf(str, "Map %i  Server %i  QSSM %i  PL %i", min, smin, cmin, pl);

		len = strlen(str);
		if (len > 40)
			Sbar_DrawScrollString(0, 12, 320, str);
		else
			M_Print(160 - len * 4, 37, str); // woods lets make this colored
	}

	if (!fitzmode)
	{ /* QuakeSpasm customization: */
		if (cl.gametype != GAME_DEATHMATCH) // woods only in singleplayer
		{
			q_snprintf(str, sizeof(str), "skill %i", (int)(skill.value + 0.5));
			Sbar_DrawString(160 - strlen(str) * 4, 12, str);
		}

		if (cl.maxclients > 1)
		{
			char qfylwdot[2] = { 133, '\0' }; // woods  -- quake font yellow dot

			if (cl.levelname[0]) // if there's a level name
				q_snprintf(str, sizeof(str), "%s (%s) %s %s", cl.levelname, cl.mapname, qfylwdot, lastmphost);
			else
				q_snprintf(str, sizeof(str), "%s %s %s", cl.mapname, qfylwdot, lastmphost);
		}
		else
		{ 
			if (cl.levelname[0]) // if there's a level name
				q_snprintf (str, sizeof(str), "%s (%s)", cl.levelname, cl.mapname);
			else
				q_snprintf (str, sizeof(str), "%s", cl.mapname);
		}

		len = strlen (str);
		if (len > 40)
			Sbar_DrawScrollString (0, 4, 320, str);
		else
			Sbar_DrawString (160 - len*4, 4, str);
		return;
	}
	minutes = cl.time / 60;
	seconds = cl.time - 60*minutes;
	tens = seconds / 10;
	units = seconds - 10*tens;
	sprintf (str,"%i:%i%i", minutes, tens, units);
	Sbar_DrawString (160 - strlen(str)*4, 12, str);

	len = q_strlcpy (str, cl.levelname[0] ? cl.levelname : cl.mapname, sizeof(str)); // woods iw
	if (len > 40)
		Sbar_DrawScrollString (0, 4, 320, str); // woods iw
	else
		Sbar_DrawString (160 - len*4, 4, str); // woods iw
}

/*
===============
Sbar_DrawScoreboard
===============
*/
void Sbar_DrawScoreboard (void)
{
	Sbar_SoloScoreboard ();
	if (cl.gametype == GAME_DEATHMATCH || cl.maxclients > 1) // woods coop support (sleeper) -- joequake/qrack
		Sbar_DeathmatchOverlay ();
}

//=============================================================================

/*
===============
Sbar_DrawInventory
===============
*/
void Sbar_DrawInventory (void)
{
	int	i, val;
	char	num[6];
	float	time;
	int	flashon;

	if (rogue)
	{
		if ( cl.stats[STAT_ACTIVEWEAPON] >= RIT_LAVA_NAILGUN )
			Sbar_DrawPicAlpha (0, -24, rsb_invbar[0], scr_sbaralpha.value); //johnfitz -- scr_sbaralpha
		else
			Sbar_DrawPicAlpha (0, -24, rsb_invbar[1], scr_sbaralpha.value); //johnfitz -- scr_sbaralpha
	}
	else
	{
		Sbar_DrawPicAlpha (0, -24, sb_ibar, scr_sbaralpha.value); //johnfitz -- scr_sbaralpha
	}

// weapons
	for (i = 0; i < 7; i++)
	{
		if (cl.items & (IT_SHOTGUN<<i) )
		{
			time = cl.item_gettime[i];
			flashon = (int)((cl.time - time)*10);
			if (flashon < 0)
			{	//wait what? it happened in the future? no no no!
				time = 0;
				cl.item_gettime[i] = cl.time;
			}
			if (flashon >= 10)
			{
				if ( cl.stats[STAT_ACTIVEWEAPON] == (IT_SHOTGUN<<i)  )
					flashon = 1;
				else
					flashon = 0;
			}
			else
				flashon = (flashon%5) + 2;

			Sbar_DrawPic (i*24, -16, sb_weapons[flashon][i]);

			if (flashon > 1)
				sb_updates = 0;		// force update to remove flash
		}
	}

// MED 01/04/97
// hipnotic weapons
	if (hipnotic)
	{
		int grenadeflashing = 0;
		for (i = 0; i < 4; i++)
		{
			if (cl.items & (1<<hipweapons[i]))
			{
				time = cl.item_gettime[hipweapons[i]];
				flashon = (int)((cl.time - time)*10);
				if (flashon >= 10)
				{
					if (cl.stats[STAT_ACTIVEWEAPON] == (1<<hipweapons[i]))
						flashon = 1;
					else
						flashon = 0;
				}
				else
					flashon = (flashon%5) + 2;

				// check grenade launcher
				if (i == 2)
				{
					if (cl.items & HIT_PROXIMITY_GUN)
					{
						if (flashon)
						{
							grenadeflashing = 1;
							Sbar_DrawPic (96, -16, hsb_weapons[flashon][2]);
						}
					}
				}
				else if (i == 3)
				{
					if (cl.items & (IT_SHOTGUN<<4))
					{
						if (flashon && !grenadeflashing)
						{
							Sbar_DrawPic (96, -16, hsb_weapons[flashon][3]);
						}
						else if (!grenadeflashing)
						{
							Sbar_DrawPic (96, -16, hsb_weapons[0][3]);
						}
					}
					else
						Sbar_DrawPic (96, -16, hsb_weapons[flashon][4]);
				}
				else
					Sbar_DrawPic (176 + (i*24), -16, hsb_weapons[flashon][i]);

				if (flashon > 1)
					sb_updates = 0;	// force update to remove flash
			}
		}
	}

	if (rogue)
	{
    // check for powered up weapon.
		if ( cl.stats[STAT_ACTIVEWEAPON] >= RIT_LAVA_NAILGUN )
		{
			for (i=0;i<5;i++)
			{
				if (cl.stats[STAT_ACTIVEWEAPON] == (RIT_LAVA_NAILGUN << i))
				{
					Sbar_DrawPic ((i+2)*24, -16, rsb_weapons[i]);
				}
			}
		}
	}

// ammo counts
	for (i = 0; i < 4; i++)
	{
		val = cl.stats[STAT_SHELLS+i];
		val = (val < 0)? 0 : q_min(999,val);//johnfitz -- cap displayed value to 999
		sprintf (num, "%3i", val);
		if (num[0] != ' ')
			Sbar_DrawCharacter ( (6*i+1)*8 + 2, -24, 18 + num[0] - '0');
		if (num[1] != ' ')
			Sbar_DrawCharacter ( (6*i+2)*8 + 2, -24, 18 + num[1] - '0');
		if (num[2] != ' ')
			Sbar_DrawCharacter ( (6*i+3)*8 + 2, -24, 18 + num[2] - '0');
	}

	flashon = 0;
	// items
	for (i = 0; i < 6; i++)
	{
		if (cl.items & (1<<(17+i)))
		{
			time = cl.item_gettime[17+i];
			if (time && time > cl.time - 2 && flashon)
			{	// flash frame
				sb_updates = 0;
			}
			else
			{
				//MED 01/04/97 changed keys
				if (!hipnotic || (i > 1))
				{
					Sbar_DrawPic (192 + i*16, -16, sb_items[i]);
				}
			}
			if (time && time > cl.time - 2)
				sb_updates = 0;
		}
	}
	//MED 01/04/97 added hipnotic items
	// hipnotic items
	if (hipnotic)
	{
		for (i = 0; i < 2; i++)
		{
			if (cl.items & (1<<(24+i)))
			{
				time = cl.item_gettime[24+i];
				if (time && time > cl.time - 2 && flashon )
				{	// flash frame
					sb_updates = 0;
				}
				else
				{
					Sbar_DrawPic (288 + i*16, -16, hsb_items[i]);
				}
				if (time && time > cl.time - 2)
					sb_updates = 0;
			}
		}
	}

	if (rogue)
	{
	// new rogue items
		for (i = 0; i < 2; i++)
		{
			if (cl.items & (1<<(29+i)))
			{
				time = cl.item_gettime[29+i];
				if (time && time > cl.time - 2 && flashon)
				{	// flash frame
					sb_updates = 0;
				}
				else
				{
					Sbar_DrawPic (288 + i*16, -16, rsb_items[i]);
				}
				if (time && time > cl.time - 2)
					sb_updates = 0;
			}
		}
	}
	else
	{
	// sigils
		if (!cls.demoplayback) // woods #demopercent (Baker Fitzquake Mark V) -- make room for demo %
		for (i = 0; i < 4; i++)
		{
			if (cl.items & (1<<(28+i)))
			{
				time = cl.item_gettime[28+i];
				if (time && time > cl.time - 2 && flashon)
				{	// flash frame
					sb_updates = 0;
				}
				else
					Sbar_DrawPic (320-32 + i*8, -16, sb_sigil[i]);
				if (time && time > cl.time - 2)
					sb_updates = 0;
			}
		}
	}
}

/*
===============
Sbar_DrawInventory_QW -- github.com/bangstk/Quakespasm // woods #sbarstyles
===============
*/
void Sbar_DrawInventory_QW (void)
{
	int	i, val;
	char	num[6];
	float	time;
	int	flashon;
	int extraguns = 2 * hipnotic;
	int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);
	qpic_t* pic = NULL;

	if (clampedSbar == 3)
		GL_SetCanvas(CANVAS_IBAR_QWQE);
	else
		GL_SetCanvas(CANVAS_IBAR_QW);

	if (COM_FileExists("gfx/ibar2.lmp", NULL))
		pic = Draw_CachePic("gfx/ibar2.lmp");

	//for qw hud, ammo backgrounds
	for (i = 0; i < 4; i++)
	{
		if (rogue)
		{
			if (cl.stats[STAT_ACTIVEWEAPON] >= RIT_LAVA_NAILGUN)
				Sbar_DrawSubPicAlpha(0, 188 - 11 * (4 - i) - 24, rsb_invbar[0], 1 + (i * 48), 0, 44, 11, scr_sbaralpha.value); //johnfitz -- scr_sbaralpha
			else
				Sbar_DrawSubPicAlpha(0, 188 - 11 * (4 - i) - 24, rsb_invbar[1], 1 + (i * 48), 0, 44, 11, scr_sbaralpha.value); //johnfitz -- scr_sbaralpha
		}
		else
			if (!scr_sbaralphaqwammo.value) // woods #sbarstyles
			{
				if (pic != NULL)
					Sbar_DrawSubPicAlpha(2, 188 - 11 * (4 - i) - 24, pic, 3 + (i * 48), 0, 42, 11, 1);
				else
					Sbar_DrawSubPicAlpha(2, 188 - 11 * (4 - i) - 24, sb_ibar, 3 + (i * 48), 0, 42, 11, 0);
			}
			else
				Sbar_DrawSubPicAlpha(2, 188 - 11 * (4 - i) - 24, sb_ibar, 3 + (i * 48), 0, 42, 11, 1);
	}

	// weapons
	for (i = 0; i < 7; i++)
	{
		if (cl.items & (IT_SHOTGUN << i))
		{
			time = cl.item_gettime[i];
			flashon = (int)((cl.time - time) * 10);
			if (flashon >= 10)
			{
				if (cl.stats[STAT_ACTIVEWEAPON] == (IT_SHOTGUN << i))
					flashon = 1;
				else
					flashon = 0;
			}
			else
				flashon = (flashon % 5) + 2;

			Sbar_DrawPicAlpha(20, 32 + i * 16 - (16 * extraguns) - 24, sb_weapons[flashon][i], scr_sbaralpha.value);

			if (flashon > 1)
				sb_updates = 0;		// force update to remove flash
		}
	}

	// MED 01/04/97
	// hipnotic weapons
	if (hipnotic)
	{
		int grenadeflashing = 0;
		for (i = 0; i < 4; i++)
		{
			if (cl.items & (1 << hipweapons[i]))
			{
				time = cl.item_gettime[hipweapons[i]];
				flashon = (int)((cl.time - time) * 10);
				if (flashon >= 10)
				{
					if (cl.stats[STAT_ACTIVEWEAPON] == (1 << hipweapons[i]))
						flashon = 1;
					else
						flashon = 0;
				}
				else
					flashon = (flashon % 5) + 2;

				// check grenade launcher
				if (i == 2)
				{
					if (cl.items & HIT_PROXIMITY_GUN)
					{
						if (flashon)
						{
							grenadeflashing = 1;
							Sbar_DrawPicAlpha(20, 40, hsb_weapons[flashon][2], scr_sbaralpha.value);
						}
					}
				}
				else if (i == 3)
				{
					if (cl.items & (IT_SHOTGUN << 4))
					{
						if (flashon && !grenadeflashing)
						{
							Sbar_DrawPicAlpha(20, 40, hsb_weapons[flashon][3], scr_sbaralpha.value);
						}
						else if (!grenadeflashing)
						{
							Sbar_DrawPicAlpha(20, 40, hsb_weapons[0][3], scr_sbaralpha.value);
						}
					}
					else
						Sbar_DrawPicAlpha(20, 40, hsb_weapons[flashon][4], scr_sbaralpha.value);
				}
				else
					Sbar_DrawPicAlpha(20, (i + 7) * 16 - 24, hsb_weapons[flashon][i], scr_sbaralpha.value);

				if (flashon > 1)
					sb_updates = 0;	// force update to remove flash
			}
		}
	}

	if (rogue)
	{
		// check for powered up weapon.
		if (cl.stats[STAT_ACTIVEWEAPON] >= RIT_LAVA_NAILGUN)
		{
			for (i = 0;i < 5;i++)
			{
				if (cl.stats[STAT_ACTIVEWEAPON] == (RIT_LAVA_NAILGUN << i))
				{
					Sbar_DrawPicAlpha(20, (i + 2) * 16 - 24 + 32, rsb_weapons[i], scr_sbaralpha.value);
				}
			}
		}
	}
	
	// ammo counts
	for (i = 0; i < 4; i++)
	{
		int x;
		x = 0;
		if (!scr_sbaralphaqwammo.value && !COM_FileExists("gfx/ibar2.lmp", NULL)) // woods #sbarstyles
			x = 8;
		val = cl.stats[STAT_SHELLS + i];
		val = (val < 0) ? 0 : q_min(999, val);//johnfitz -- cap displayed value to 999
		sprintf(num, "%3i", val);
		if (num[0] != ' ')
			Sbar_DrawCharacter(9 + x, 188 - 11 * (4 - i) - 24, 18 + num[0] - '0');
		if (num[1] != ' ')
			Sbar_DrawCharacter(17 + x, 188 - 11 * (4 - i) - 24, 18 + num[1] - '0');
		if (num[2] != ' ')
			Sbar_DrawCharacter(25 + x, 188 - 11 * (4 - i) - 24, 18 + num[2] - '0');
	}

	GL_SetCanvas(CANVAS_SBAR);

	if (clampedSbar == 3)
		return;

	flashon = 0;
	// items
	for (i = 0; i < 6; i++)
	{
		if (cl.items & (1 << (17 + i)))
		{
			time = cl.item_gettime[17 + i];
			if (time && time > cl.time - 2 && flashon)
			{	// flash frame
				sb_updates = 0;
			}
			else
			{
				//MED 01/04/97 changed keys
				if (!hipnotic || (i > 1))
				{
					Sbar_DrawPic(192 + i * 16, -16, sb_items[i]);
				}
			}
			if (time && time > cl.time - 2)
				sb_updates = 0;
		}
	}
	//MED 01/04/97 added hipnotic items
	// hipnotic items
	if (hipnotic)
	{
		for (i = 0; i < 2; i++)
		{
			if (cl.items & (1 << (24 + i)))
			{
				time = cl.item_gettime[24 + i];
				if (time && time > cl.time - 2 && flashon)
				{	// flash frame
					sb_updates = 0;
				}
				else
				{
					Sbar_DrawPic(288 + i * 16, -16, hsb_items[i]);
				}
				if (time && time > cl.time - 2)
					sb_updates = 0;
			}
		}
	}

	if (rogue)
	{
		// new rogue items
		for (i = 0; i < 2; i++)
		{
			if (cl.items & (1 << (29 + i)))
			{
				time = cl.item_gettime[29 + i];
				if (time && time > cl.time - 2 && flashon)
				{	// flash frame
					sb_updates = 0;
				}
				else
				{
					Sbar_DrawPic(288 + i * 16, -16, rsb_items[i]);
				}
				if (time && time > cl.time - 2)
					sb_updates = 0;
			}
		}
	}
	else
	{
		// sigils
		if (!cls.demoplayback) // woods #demopercent (Baker Fitzquake Mark V) -- make room for demo %
			for (i = 0; i < 4; i++)
			{
				if (cl.items & (1 << (28 + i)))
				{
					time = cl.item_gettime[28 + i];
					if (time && time > cl.time - 2 && flashon)
					{	// flash frame
						sb_updates = 0;
					}
					else
						Sbar_DrawPic(320 - 32 + i * 8, -16, sb_sigil[i]);
					if (time && time > cl.time - 2)
						sb_updates = 0;
				}
			}
	}
	GL_SetCanvas(CANVAS_SBAR);
}

/*
===============
Sbar_DrawInventory_QE -- woods - keys and runes only #qehud
===============
*/
void Sbar_DrawInventory_QE (void)
{
	int	i, x, y;

	GL_SetCanvas(CANVAS_BOTTOMRIGHTQE);
	
	// keys

	x = 288;
	y = 120;

	if (!scr_sbarshowqeammo.value)
		y -= 24;

	for (i = 0; i < 2; i++)
	{
		if (cl.items & (IT_KEY1 << i))
		{
			Sbar_DrawPic(x, y, sb_items[i]);
			x -= sb_items[i]->width;
		}
	}

	// sigils

	if (!(cl.items & IT_KEY1) && !(cl.items & IT_KEY2)) // no keys
		x = 296;
	else if ((cl.items & IT_KEY1) && (cl.items & IT_KEY2)) // both keys
		x = 256;
	else
		x = 272; // one key

	for (i = 0; i < 4; i++)
	{
		if (cl.items & (IT_SIGIL1 << i))
		{
			Sbar_DrawPic(x, y, sb_sigil[i]);
			x -= sb_sigil[i]->width;
		}
	}
	GL_SetCanvas(CANVAS_SBAR);
}

//=============================================================================

/*===============
IsOneVsOneMatch -- detect if match is 1v1 match #detectmatch
============== */
qboolean IsOneVsOneMatch (void)
{
	const int TEAM_RED = 4;
	const int TEAM_BLUE = 13;

	int redTeamCount = 0;
	int blueTeamCount = 0;

	for (int i = 0; i < scoreboardlines; i++)
	{
		int playerIndex = fragsort[i];
		scoreboard_t* score = &cl.scores[playerIndex];

		if (score->name[0] == '\0') // Skip entries with empty names
			continue;

		if (score->pants.basic == TEAM_RED) // Count players based on their team
		{
			redTeamCount++;
		}
		else if (score->pants.basic == TEAM_BLUE)
		{
			blueTeamCount++;
		}
	}

	int totalTeamPlayers = redTeamCount + blueTeamCount;

	return (totalTeamPlayers <= 2); // accounts for scenarios where a player might leave
}

/*===============
Sbar_DrawFrags -- for proquake, HEAVILY modified (draws match time, and teamscores) replace this entire function // woods #pqteam
============== */
void Sbar_DrawFrags(void)
{
	int				i, k, l;
	int				top, bottom;
	int				numscores;
	int				x, f;
	char			num[20];
	scoreboard_t* s;
	int				teamscores, colors, minutes, seconds, mask; // JPG - added these
	int				match_time; // JPG - added this

	extern qboolean crxintermission; // woods #crxintermission

	int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);
	
	if (clampedSbar > 1) // woods #sbarstyles
		return;
	
	// JPG - check to see if we should sort teamscores instead
	teamscores = cl.teamgame;

	if (teamscores)
		Sbar_SortTeamFrags();
	else
		Sbar_SortFrags (false);

	// draw the text
	l = scoreboardlines <= 4 ? scoreboardlines : 4;

	x = 23;

	// display match clock
	// 
	// JPG - check to see if we need to draw the timer
	if (cl.minutes != 255)
	{
		if (l > 2)
			l = 2;
		mask = 0;
		if (cl.minutes == 254)
		{
			strcpy(num, "    SD");
			mask = 128;
		}
		else if (cl.minutes || cl.seconds)
		{
			if (cl.seconds >= 128)
				sprintf(num, " -0:%02d", cl.seconds - 128);
			else
			{
				if (cl.match_pause_time)
					match_time = ceil(60.0 * cl.minutes + cl.seconds - (cl.match_pause_time - cl.last_match_time));
				else
					match_time = ceil(60.0 * cl.minutes + cl.seconds - (cl.time - cl.last_match_time));
				minutes = q_max(0, floor(match_time / 60));
				seconds = q_max(0, match_time - 60 * floor(match_time / 60));
				sprintf(num, "%3d:%02d", minutes, seconds);
				if (!minutes)
					mask = 128;
			}
		}
		else
		{
			minutes = cl.time / 60;
			seconds = cl.time - 60 * minutes;
			minutes = minutes & 511;

			if (crxintermission) // woods #crxintermission
				sprintf(num, "%3d:%02d", 0, 0);
			else
				sprintf(num, "%3d:%02d", minutes, seconds);
		}

		for (i = 0; i < 6; i++)
			Sbar_DrawCharacter (((x + 9 + i) * 8)+3, -24, num[i] + mask);
	}

	// display frag/team colors

	if (teamscores)
	{
		for (i = 0; i < l; i++)
		{
			k = fragsort[i];
			colors = cl.teamscores[k].colors;

			top = (colors & 15) << 4;
			bottom = (colors & 15) << 4;

			Draw_Fill((((x + 1) * 8) + 2), 1, 28, 4, top + 8, 1);
			Draw_Fill((((x + 1) * 8) + 2), 5, 28, 3, bottom + 8, 1);

			f = cl.teamscores[k].frags;

			// draw number
			sprintf (num, "%3i", f);
			Sbar_DrawCharacter (((x + 1) * 8) + 4, -24, num[0]);
			Sbar_DrawCharacter (((x + 2) * 8) + 4, -24, num[1]);
			Sbar_DrawCharacter (((x + 3) * 8) + 4, -24, num[2]);

			if ((teamscores && bottom == (cl.scores[cl.viewentity - 1].pants.basic & 15) << 4) || (!teamscores && (k == cl.viewentity - 1)))
			{
				Sbar_DrawCharacter((x) * 8 + 5, -24, 16);
				Sbar_DrawCharacter((x + 4) * 8 + 1, -24, 17);
			}

			x += 4;
		}
	}
	else
	{ 
		// draw the text
		numscores = q_min(scoreboardlines, 2); // woods only show 2 scores to make room for clock

		for (i = 0, x = 184; i < numscores; i++, x += 32)
		{
			s = &cl.scores[fragsort[i]];
			if (!s->name[0])
				continue;

			// top color
			Draw_FillPlayer (x + 10, 1, 28, 4, s->shirt, 1);

			// bottom color
			Draw_FillPlayer (x + 10, 5, 28, 3, s->pants, 1);

			// number
			sprintf(num, "%3i", s->frags);
			Sbar_DrawCharacter (x + 12, -24, num[0]);
			Sbar_DrawCharacter (x + 20, -24, num[1]);
			Sbar_DrawCharacter (x + 28, -24, num[2]);

			// brackets
			if (fragsort[i] == cl.viewentity - 1)
			{
				Sbar_DrawCharacter (x + 6, -24, 16);
				Sbar_DrawCharacter (x + 32, -24, 17);
			}
		}

	}
}

extern cvar_t scr_showfps; // woods #showrecord
extern cvar_t scr_clock; // woods #showrecord

/*
===============
Sbar_DrawRecord -- woods #showrecord
===============
*/

void Sbar_DrawRecord(void)
{
	int x,y;

	y = 0;
	x = 0;

	int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);

	if (scr_viewsize.value >= 130)
		return;

	if (clampedSbar == 1)
	{
		GL_SetCanvas(CANVAS_SBAR);

		if (scr_viewsize.value <= 100)
			y = 4;
		else
			return;
		Draw_Fill(315, y, 1, 1, 249, 1);
	}

	if (clampedSbar == 2)
	{
		if (scr_viewsize.value >= 110)
			return;

		GL_SetCanvas(CANVAS_BOTTOMRIGHTQE);

		x = 316;
		y = 156;
		Draw_Fill(x, y, 1, 1, 249, 1);
	}

	if (clampedSbar == 3)
	{
		GL_SetCanvas(CANVAS_BOTTOMRIGHTQE);

		if (scr_viewsize.value >= 110)
			return;

		x = 302;
		y = 119;



		Draw_Fill(x, y, 1, 1, 249, 1);
	}
}

/*
===============
Sbar_DrawFace
===============
*/
void Sbar_DrawFace (void)
{
	int	f, anim;
	plcolour_t color = CL_PLColours_Parse(cl_damagehuecolor.string);

// PGM 01/19/97 - team color drawing
// PGM 03/02/97 - fixed so color swatch only appears in CTF modes
	if (rogue && (cl.maxclients != 1) && (teamplay.value>3) && (teamplay.value<7))
	{
		int	xofs;
		char	num[12];
		scoreboard_t	*s;

		s = &cl.scores[cl.viewentity - 1];
		// draw background
		if (cl.gametype == GAME_DEATHMATCH)
			xofs = 113;
		else
			xofs = ((vid.width - 320)>>1) + 113;

		Sbar_DrawPic (112, 0, rsb_teambord);
		Draw_FillPlayer (xofs, /*vid.height-*/24+3, 22, 9, s->shirt, 1); //johnfitz -- sbar coords are now relative
		Draw_FillPlayer (xofs, /*vid.height-*/24+12, 22, 9, s->pants, 1); //johnfitz -- sbar coords are now relative

		// draw number
		f = s->frags;
		sprintf (num, "%3i",f);

		if (s->shirt.type == 1 && s->shirt.basic == 0) //white team. FIXME: vanilla says top, but I suspect it should be the lower colour, as that's the actual team nq sees.
		{
			if (num[0] != ' ')
				Sbar_DrawCharacter(113, 3, 18 + num[0] - '0');
			if (num[1] != ' ')
				Sbar_DrawCharacter(120, 3, 18 + num[1] - '0');
			if (num[2] != ' ')
				Sbar_DrawCharacter(127, 3, 18 + num[2] - '0');
		}
		else
		{
			Sbar_DrawCharacter (113, 3, num[0]);
			Sbar_DrawCharacter (120, 3, num[1]);
			Sbar_DrawCharacter (127, 3, num[2]);
		}

		return;
	}
// PGM 01/19/97 - team color drawing

	if ((cl.items & (IT_INVISIBILITY | IT_INVULNERABILITY))
			== (IT_INVISIBILITY | IT_INVULNERABILITY))
	{
		Sbar_DrawPic (112, 0, sb_face_invis_invuln);
		return;
	}
	if (cl.items & IT_QUAD)
	{
		Sbar_DrawPic (112, 0, sb_face_quad );
		return;
	}
	if (cl.items & IT_INVISIBILITY)
	{
		Sbar_DrawPic (112, 0, sb_face_invis );
		return;
	}
	if (cl.items & IT_INVULNERABILITY)
	{
		Sbar_DrawPic (112, 0, sb_face_invuln);
		return;
	}

	if (cl.stats[STAT_HEALTH] >= 100)
		f = 4;
	else
		f = cl.stats[STAT_HEALTH] / 20;
	if (f < 0)	// in case we ever decide to draw when health <= 0
		f = 0;

	if (cl.time <= cl.faceanimtime)
	{
		anim = 1;
		sb_updates = 0;		// make sure the anim gets drawn over
	}
	else
		anim = 0;
	Sbar_DrawPic (112, 0, sb_faces[f][anim]);

	if (cl.time <= cl.faceanimtime && cl_damagehue.value && cl_damagehuecolor.value) // woods for damagehue on sbar face
 		Draw_FillPlayer(112, 24, 24, 25, color, .2);
}

/*
===============
Sbar_DrawFace_Team -- woods to color face in sbar when on a match team #teamface
===============
*/

void Sbar_DrawFace_Team (void)
{
	if (!scr_sbarfacecolor.value)
		return;
	
	int color;

	color = cl.scores[cl.viewentity - 1].pants.basic; // get color 0-13
	color = Sbar_ColorForMap((color & 15) << 4); // translate to proper drawfill color

	int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);

	if (clampedSbar == 3 && scr_viewsize.value <= 110)
		{
			GL_SetCanvas(CANVAS_BOTTOMLEFTQE);
			Draw_Fill(18, 164, 23, 1, color, .7); // top
			Draw_Fill(18, 187, 23, 1, color, .7); // bottom

			Draw_Fill(18, 164, 1, 24, color, .7); // left
			Draw_Fill(41, 164, 1, 24, color, .7);  // right
		}

	if (sb_showscores == true)
		return;

	if (scr_viewsize.value <= 110 && clampedSbar != 3)
	{
		{
			GL_SetCanvas(CANVAS_SBAR);

			Draw_Fill(111, 24, 1, 25, color, .7); // left
			Draw_Fill(136, 24, 1, 25, color, .7);  // right
		}
	}
}

static void Sbar_Voice(int y)
{
	cvar_t snd_voip_showmeter;
	int loudness;
	snd_voip_showmeter.value = 1;
	if (!snd_voip_showmeter.value)
		return;
	loudness = S_Voip_Loudness(snd_voip_showmeter.value>=2);
	if (loudness >= 0)
	{
		int cw = 8;
		int w;
		int x=160;
		int s, i;
		float range = loudness/100.0f;
		w = (5+16+1)*cw;
		x -= w/2;
		Draw_Character (x, y, 'M');		x+=cw;
		Draw_Character (x, y, 'i');		x+=cw;
		Draw_Character (x, y, 'c');		x+=cw;
										x+=cw;
		Draw_Character (x, y, 0xe080);	x+=cw;
		for (s=x,i=0 ; i<16 ; i++, x+=cw)
			Draw_Character(x, y, 0xe081);
		Draw_Character (x, y, 0xe082);
		Draw_Character (s + (x-s) * range - cw/2, y, 0xe083);
	}
}

/*
===============
Sbar_FacePic - woods fpr qe sbar #qehud
===============
*/
static qpic_t* Sbar_FacePic(void)
{
	int f, anim;

	if ((cl.items & (IT_INVISIBILITY | IT_INVULNERABILITY))
		== (IT_INVISIBILITY | IT_INVULNERABILITY))
		return sb_face_invis_invuln;

	if (cl.items & IT_QUAD)
		return sb_face_quad;

	if (cl.items & IT_INVISIBILITY)
		return sb_face_invis;

	if (cl.items & IT_INVULNERABILITY)
		return sb_face_invuln;

	if (cl.stats[STAT_HEALTH] >= 100)
		f = 4;
	else
		f = cl.stats[STAT_HEALTH] / 20;
	if (f < 0)	// in case we ever decide to draw when health <= 0
		f = 0;

	if (cl.time <= cl.faceanimtime)
	{
		anim = 1;
		sb_updates = 0;		// make sure the anim gets drawn over
	}
	else
		anim = 0;

	return sb_faces[f][anim];
}

/*
===============
Sbar_Draw
===============
*/
void Sbar_Draw (void)
{
	float w; //johnfitz
	int armor, invuln;

	qboolean mpdemo = false; // woods #obspent
	qboolean observer = false; // woods #obspent
	char buf[15]; // woods #obspent
	const char* obs = NULL; // woods #obspent

	if (cls.demoplayback && cl.maxclients > 1) // woods #obspent
		mpdemo = true;

	if ((cl.gametype == GAME_DEATHMATCH) && (cls.state == ca_connected)) // woods #obspent
		obs = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "observer", buf, sizeof(buf));

	if (obs) // woods #obspent
	{
		if (cl.modtype == 1 || cl.modtype == 4) 
		{
			if (strcmp(obs, "") == 0) 
				observer = false;
			else 
				observer = (strcmp(obs, "off") != 0);
		}
		else 
			observer = (strcmp(obs, "n") == 0);
	}

	int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3); // woods

	if (scr_con_current == vid.height)
		return;		// console is full screen

	if (cl.qcvm.extfuncs.CSQC_DrawHud && !qcvm)
	{
		qboolean deathmatchoverlay = false;
		float s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		sb_updates++;
		GL_SetCanvas (CANVAS_CSQC); //johnfitz
		glEnable (GL_BLEND);	//in the finest tradition of glquake, we litter gl state calls all over the place. yay state trackers.
		glDisable (GL_ALPHA_TEST);	//in the finest tradition of glquake, we litter gl state calls all over the place. yay state trackers.
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		PR_SwitchQCVM(&cl.qcvm);
		pr_global_struct->time = qcvm->time;
		pr_global_struct->frametime = qcvm->frametime;
		if (qcvm->extglobals.cltime)
			*qcvm->extglobals.cltime = realtime;
		if (qcvm->extglobals.clframetime)
			*qcvm->extglobals.clframetime = host_frametime;
		if (qcvm->extglobals.player_localentnum)
			*qcvm->extglobals.player_localentnum = cl.viewentity;
		Sbar_SortFrags (false);
		G_VECTORSET(OFS_PARM0, vid.width/s, vid.height/s, 0);
		G_FLOAT(OFS_PARM1) = sb_showscores;
		PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_DrawHud);
		if (cl.qcvm.extfuncs.CSQC_DrawScores)
		{
			G_VECTORSET(OFS_PARM0, vid.width/s, vid.height/s, 0);
			G_FLOAT(OFS_PARM1) = sb_showscores;
			if (key_dest != key_menu)
				PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_DrawScores);
		}
		else
			deathmatchoverlay = (sb_showscores || cl.stats[STAT_HEALTH] <= 0);
		PR_SwitchQCVM(NULL);
		glDisable (GL_BLEND);
		glEnable (GL_ALPHA_TEST);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);	//back to ignoring vertex colours.
		glDisable(GL_SCISSOR_TEST);
		glColor3f (1,1,1);

		if (deathmatchoverlay && cl.gametype == GAME_DEATHMATCH)
		{
			GL_SetCanvas (CANVAS_SBAR);
			Sbar_DeathmatchOverlay ();
		}
		return;
	}

	if (cl.intermission)
		return; //johnfitz -- never draw sbar during intermission

	if (sb_updates >= vid.numpages && !gl_clear.value && scr_sbaralpha.value >= 1 //johnfitz -- gl_clear, scr_sbaralpha
        && !(gl_glsl_gamma_able && vid_gamma.value != 1))                         //ericw -- must draw sbar every frame if doing glsl gamma
		return;

	sb_updates++;

	GL_SetCanvas (CANVAS_DEFAULT); //johnfitz

	if (sb_lines > 24)
		Sbar_Voice(-32);
	else if (sb_lines > 0)
		Sbar_Voice(-8);
	else
		Sbar_Voice(16);

	//johnfitz -- don't waste fillrate by clearing the area behind the sbar // woods edited for #sbarstyles
	w = CLAMP (320.0f, scr_sbarscale.value * 320.0f, (float)glwidth);
	if (sb_lines && glwidth > w)
		Draw_TileClear (0, glheight - sb_lines, glwidth, sb_lines);

	GL_SetCanvas (CANVAS_SBAR); //johnfitz

	if (scr_viewsize.value < 110) //johnfitz -- check viewsize instead of sb_lines
	{
		if (clampedSbar == 1)
			Sbar_DrawInventory ();
		if (clampedSbar == 2)
			Sbar_DrawInventory_QW ();
		if (clampedSbar == 3)
		{
			GL_SetCanvas(CANVAS_BOTTOMRIGHTQE); //johnfitz
			Sbar_DrawInventory_QE();
			Sbar_DrawInventory_QW();
		}
		if (cl.maxclients != 1)
			Sbar_DrawFrags ();
	}

	if (clampedSbar == 3 && scr_viewsize.value <= 110) // qe hud does not use 'traditional sbar' #qehud
	{
		plcolour_t color = CL_PLColours_Parse(cl_damagehuecolor.string);
		
		GL_SetCanvas(CANVAS_BOTTOMLEFTQE);

		// armor

		invuln = (cl.items & IT_INVULNERABILITY) != 0;
		armor = (invuln && !observer && !mpdemo) ? 666 : cl.stats[STAT_ARMOR]; // woods #obspent

		if (armor > 0) // only draw if you have armor
		{
			Sbar_DrawSubPicAlpha(18, 116, sb_sbar, 0, 0, 24, 24, 1); // armor sbar background

			if (cl.items & IT_INVULNERABILITY)
			{
				if (invuln && !observer && !mpdemo) // woods #obspent
					Sbar_DrawNum(50, 116, 666, 3, 1);
				else
					Sbar_DrawNum(50, 115, cl.stats[STAT_ARMOR], 3
						, cl.stats[STAT_ARMOR] <= 25);
				Sbar_DrawPic(18, 116, draw_disc);
			}
			else
			{
				if (rogue)
				{
					Sbar_DrawNum(50, 115, cl.stats[STAT_ARMOR], 3,
						cl.stats[STAT_ARMOR] <= 25);
					if (cl.items & RIT_ARMOR3)
						Sbar_DrawPic(18, 115, sb_armor[2]);
					else if (cl.items & RIT_ARMOR2)
						Sbar_DrawPic(18, 115, sb_armor[1]);
					else if (cl.items & RIT_ARMOR1)
						Sbar_DrawPic(18, 115, sb_armor[0]);
				}
				else
				{
					Sbar_DrawNum(50, 115, cl.stats[STAT_ARMOR], 3
						, cl.stats[STAT_ARMOR] <= 25);
					if (cl.items & IT_ARMOR3)
						Sbar_DrawPic(18, 115, sb_armor[2]);
					else if (cl.items & IT_ARMOR2)
						Sbar_DrawPic(18, 115, sb_armor[1]);
					else if (cl.items & IT_ARMOR1)
						Sbar_DrawPic(18, 115, sb_armor[0]);
				}
			}
		}

		// face
		Sbar_DrawPic(18, 140, Sbar_FacePic());

		if (cl.time <= cl.faceanimtime && cl_damagehue.value && cl_damagehuecolor.value) // woods for damagehue on sbar face
			Draw_FillPlayer (18, 163, 24, 25, color, .2);

		// health

		Sbar_DrawNum(50, 139, cl.stats[STAT_HEALTH], 3, cl.stats[STAT_HEALTH] <= 25);

		GL_SetCanvas(CANVAS_BOTTOMRIGHTQE);

	//	if (cl.stats[STAT_AMMO] > 0)
		if (scr_sbarshowqeammo.value)
			Sbar_DrawSubPicAlpha(280, 140, sb_sbar, 0, 0, 24, 24, 1); // ammo sbar background

		// ammo icon
		if (scr_sbarshowqeammo.value)
		{
			if (rogue)
			{
				if (cl.items & RIT_SHELLS)
					Sbar_DrawPic(280, 140, sb_ammo[0]);
				else if (cl.items & RIT_NAILS)
					Sbar_DrawPic(280, 140, sb_ammo[1]);
				else if (cl.items & RIT_ROCKETS)
					Sbar_DrawPic(280, 140, sb_ammo[2]);
				else if (cl.items & RIT_CELLS)
					Sbar_DrawPic(280, 140, sb_ammo[3]);
				else if (cl.items & RIT_LAVA_NAILS)
					Sbar_DrawPic(280, 140, rsb_ammo[0]);
				else if (cl.items & RIT_PLASMA_AMMO)
					Sbar_DrawPic(280, 140, rsb_ammo[1]);
				else if (cl.items & RIT_MULTI_ROCKETS)
					Sbar_DrawPic(280, 140, rsb_ammo[2]);
			}
			else
			{
				if (cl.items & IT_SHELLS)
					Sbar_DrawPic(280, 140, sb_ammo[0]);
				else if (cl.items & IT_NAILS)
					Sbar_DrawPic(280, 140, sb_ammo[1]);
				else if (cl.items & IT_ROCKETS)
					Sbar_DrawPic(280, 140, sb_ammo[2]);
				else if (cl.items & IT_CELLS)
					Sbar_DrawPic(280, 140, sb_ammo[3]);
			}
		}

		if (scr_sbarshowqeammo.value) //draw ammo if you have it
			Sbar_DrawNum(198, 140, cl.stats[STAT_AMMO], 3,
			cl.stats[STAT_AMMO] <= 10);
	}
	else // end qe hud, use traditional sbare
	{
		if (scr_viewsize.value < 120) //johnfitz -- check viewsize instead of sb_lines
		{
			Sbar_DrawPicAlpha (0, 0, sb_sbar, scr_sbaralpha.value); //johnfitz -- scr_sbaralpha

	   // keys (hipnotic only)
			//MED 01/04/97 moved keys here so they would not be overwritten
			if (hipnotic)
			{
				if (cl.items & IT_KEY1)
					Sbar_DrawPic (209, 3, sb_items[0]);
				if (cl.items & IT_KEY2)
					Sbar_DrawPic (209, 12, sb_items[1]);
			}
			if (sb_showscores == false)
			{
				// armor
				if (cl.items & IT_INVULNERABILITY)
				{
					if (!observer && !mpdemo) // woods #obspent
						Sbar_DrawNum (24, 0, 666, 3, 1);
					else
						Sbar_DrawNum(24, 0, cl.stats[STAT_ARMOR], 3
							, cl.stats[STAT_ARMOR] <= 25);
					Sbar_DrawPic (0, 0, draw_disc);
				}
				else
				{
					if (rogue)
					{
						Sbar_DrawNum (24, 0, cl.stats[STAT_ARMOR], 3,
							cl.stats[STAT_ARMOR] <= 25);
						if (cl.items & RIT_ARMOR3)
							Sbar_DrawPic (0, 0, sb_armor[2]);
						else if (cl.items & RIT_ARMOR2)
							Sbar_DrawPic (0, 0, sb_armor[1]);
						else if (cl.items & RIT_ARMOR1)
							Sbar_DrawPic (0, 0, sb_armor[0]);
					}
					else
					{
						Sbar_DrawNum (24, 0, cl.stats[STAT_ARMOR], 3
							, cl.stats[STAT_ARMOR] <= 25);
						if (cl.items & IT_ARMOR3)
							Sbar_DrawPic (0, 0, sb_armor[2]);
						else if (cl.items & IT_ARMOR2)
							Sbar_DrawPic (0, 0, sb_armor[1]);
						else if (cl.items & IT_ARMOR1)
							Sbar_DrawPic (0, 0, sb_armor[0]);
					}
				}

				// face
				Sbar_DrawFace ();

				// health
				Sbar_DrawNum (136, 0, cl.stats[STAT_HEALTH], 3
					, cl.stats[STAT_HEALTH] <= 25);

				// ammo icon
				if (rogue)
				{
					if (cl.items & RIT_SHELLS)
						Sbar_DrawPic (224, 0, sb_ammo[0]);
					else if (cl.items & RIT_NAILS)
						Sbar_DrawPic (224, 0, sb_ammo[1]);
					else if (cl.items & RIT_ROCKETS)
						Sbar_DrawPic (224, 0, sb_ammo[2]);
					else if (cl.items & RIT_CELLS)
						Sbar_DrawPic (224, 0, sb_ammo[3]);
					else if (cl.items & RIT_LAVA_NAILS)
						Sbar_DrawPic (224, 0, rsb_ammo[0]);
					else if (cl.items & RIT_PLASMA_AMMO)
						Sbar_DrawPic (224, 0, rsb_ammo[1]);
					else if (cl.items & RIT_MULTI_ROCKETS)
						Sbar_DrawPic (224, 0, rsb_ammo[2]);
				}
				else
				{
					if (cl.items & IT_SHELLS)
						Sbar_DrawPic (224, 0, sb_ammo[0]);
					else if (cl.items & IT_NAILS)
						Sbar_DrawPic (224, 0, sb_ammo[1]);
					else if (cl.items & IT_ROCKETS)
						Sbar_DrawPic (224, 0, sb_ammo[2]);
					else if (cl.items & IT_CELLS)
						Sbar_DrawPic (224, 0, sb_ammo[3]);
				}

				Sbar_DrawNum (248, 0, cl.stats[STAT_AMMO], 3,
					cl.stats[STAT_AMMO] <= 10);
			}
		}

	}

	if (clampedSbar == 3)
		GL_SetCanvas(CANVAS_SBARQE);
	else
		GL_SetCanvas(CANVAS_SBAR); //johnfitz

	if (sb_showscores/* || cl.stats[STAT_HEALTH] <= 0*/) // woods mimic qrack not showing scores on death
	{
		Sbar_DrawPicAlpha(0, 0, sb_scorebar, scr_sbaralpha.value); //johnfitz -- scr_sbaralpha
		Sbar_DrawScoreboard();
		sb_updates = 0;
	}

	//johnfitz -- removed the vid.width > 320 check here
	if (cl.gametype == GAME_DEATHMATCH)
		Sbar_MiniDeathmatchOverlay();

	if (cls.demorecording) // woods #showrecord
		Sbar_DrawRecord();

	Sbar_DrawFace_Team(); // woods #teamface

	if (cls.demoplayback && scr_viewsize.value <= 110) // woods #demopercent (Baker Fitzquake Mark V)
	{
		float completed_amount_0_to_1 = (cls.demo_offset_current - cls.demo_offset_start) / (float)cls.demo_file_length;
		int complete_pct_int = 100 - (int)(100 * completed_amount_0_to_1 + 0.5);
		char* tempstring = va("%i%%", complete_pct_int);
		int len = strlen(tempstring), i;
		int x = 0, y = 0;

		if (clampedSbar == 3) // #qehud
		{
			y = 152;
			x = 216;
			GL_SetCanvas(CANVAS_BOTTOMRIGHTQESMALL);


			if (cl.stats[STAT_AMMO] > 9) // two digits
				x -= 20;
			if (cl.stats[STAT_AMMO] > 99) // three digits
				x -= 32;

			if (scr_viewsize.value > 110)
				return;

		}
		if (clampedSbar == 2)
		{
			GL_SetCanvas(CANVAS_SBAR2);

			y = 209;

			if (!scr_showspeed.value && strcmp(mute, "y")) // by itself
					x = 24;
			if (scr_showspeed.value && strcmp(mute, "y"))
				x = 60;
			if (scr_showspeed.value && !strcmp(mute, "y")) // both
				x = 104;
			if (!scr_showspeed.value && !strcmp(mute, "y"))
				x = 62;

			if (complete_pct_int < 10)
				x -= 7;
			if (complete_pct_int > 99)
				x += 7;
		}
		if (clampedSbar == 1)
		{
			GL_SetCanvas(CANVAS_SBAR2);

			if (!strcmp(mute, "y"))
				x = 280;
			else
				x = 320;

			if (scr_viewsize.value <= 100)
				y = 184;
			else if (scr_viewsize.value == 110)
				y = 209;
			else
				return;
		}
	//	if ((!strcmp(mute, "y") && (scr_sbar.value == 2)) || (scr_viewsize.value == 110 && (!strcmp(mute, "y")))) // woods #sbarstyles
		//	x = 280;

		// Bronze it
		for (i = 0; i < len; i++)
			tempstring[i] |= 128;

		Sbar_DrawString(x - len * 8, y, tempstring);
	}

}

//=============================================================================

/*
==================
Sbar_IntermissionNumber

==================
*/
void Sbar_IntermissionNumber (int x, int y, int num, int digits, int color)
{
	char	str[12];
	char	*ptr;
	int	l, frame;

	l = Sbar_itoa (num, str);
	ptr = str;
	if (l > digits)
		ptr += (l-digits);
	if (l < digits)
		x += (digits-l)*24;

	while (*ptr)
	{
		if (*ptr == '-')
			frame = STAT_MINUS;
		else
			frame = *ptr -'0';

		Draw_Pic (x,y,sb_nums[color][frame]); //johnfitz -- stretched menus
		x += 24;
		ptr++;
	}
}

qboolean flash () // woods #smartstatus
{
	unsigned int ticks = SDL_GetTicks();
	unsigned int half_seconds = ticks / 750;
	return half_seconds % 2 == 0;
}

/*
==================
Sbar_DeathmatchOverlay

==================
*/
void Sbar_DeathmatchOverlay (void)
{
	//qpic_t	*pic; // woods disabled
	int	i, k, l;
	int	x, y, f;
	int w, w2, y2; // woods for dynamic scoreboard
	int	xofs, yofs; // woods #scoreboard
	char	num[12];
	//char	shortname[16]; // woods for dynamic scoreboard during match, don't show ready
	scoreboard_t	*s;
	int ct = (SDL_GetTicks() - maptime)/1000; // woods connected map time #maptime
	qboolean notready = false; // woods #smartstatus
	qboolean oneready = false; // woods #smartstatus

	// JPG 1.05 - check to see if we should update IP status  // woods for #iplog
	if (iplog_size && (cl.time - cl.last_status_time > 5))
	{
		MSG_WriteByte(&cls.message, clc_stringcmd);
		SZ_Print(&cls.message, "status\n");
		cl.last_status_time = cl.time;
	}

	GL_SetCanvas (CANVAS_SCOREBOARD); //johnfitz  // woods #scoreboard

	/*if (cl.matchinp && cl.modtype != 4) // woods -- match running 0 for CRCTF, 255 for CDMOD
		w = -64;
	else*/
	w = 0;
		/*if (cl.matchinp && cl.modtype != 4) // woods -- match running 0 for CRCTF, 255 for CDMOD
		w2 = 32;
	else*/
	w2 = 0;

	xofs = (vid.conwidth - 320) >> 1; // woods #scoreboard
	yofs = (vid.conheight - 200) >> 1; // woods #scoreboard

	x = xofs + 64 + w2; // woods #scoreboard
	y2 = y = yofs - 20; // woods #smartstatus

	//pic = Draw_CachePic ("gfx/ranking.lmp"); //woods #scoreboard (remove rankings logo)
	//M_DrawPic ((320-pic->width)/2, 8, pic); woods #scoreboard

// scores
	Sbar_SortFrags (false);

// draw the text
	l = scoreboardlines;

	//x = 80; //johnfitz -- simplified becuase some positioning is handled elsewhere woods #scoreboard
	//y = 40; woods #scoreboard

	// woods for qrack +scoresbg #scoreboard

	Draw_Fill (x - 64, y - 11, 328 + w, 10, 16, 1);		//inside
	Draw_Fill (x - 64, y - 12, 329 + w, 1, 0, 1);		//Border - Top
	Draw_Fill (x - 64, y - 12, 1, 11, 0, 1);			//Border - Left
	Draw_Fill (x + 264 + w, y - 12, 1, 11, 0, 1);		//Border - Right
	Draw_Fill (x - 64, y - 1, 329 + w, 1, 0, 1);		//Border - Bottom
	Draw_Fill (x - 64, y - 1, 329 + w, 1, 0, 1);		//Border - Top

	/*if (cl.matchinp) // woods -- match running 0 for CRCTF, 255 for CDMOD
		Draw_String(x - 64, y - 10, "  ping  frags   name"); // woods
	else*/

	oneready = false; // woods #smartstatus

	for (i = 0; i < l; i++)
	{
		k = fragsort[i];
		s = &cl.scores[k];
		if (!s->name[0])
			continue;

		char qfReady[6] = { 210, 229, 225, 228, 249, '\0' }; // quake font red 'Ready'

		if (cl.modtype == 1 || cl.modtype == 4) // woods -- dynamic status flash scoreboard label if not ready #smartstatus
		{
			if (!cl.teamgame)
				notready = false;
			
			if (strstr(s->name, qfReady) || strstr(s->name, "Ready"))
				oneready = true;
			
			if ((k == cl.realviewentity - 1) && cl.teamgame && !cl.matchinp && cl.notobserver && (!strstr(s->name, qfReady) || !strstr(s->name, "Ready")))
				notready = true;

			if ((k == cl.realviewentity - 1) && cl.teamgame && !cl.matchinp && cl.notobserver && (strstr(s->name, qfReady) || strstr(s->name, "Ready")))
				notready = false;
		}

		if (k == cl.viewentity - 1) // #scoreboard
		{
			Draw_Fill(x - 63, y, 328 + w, 10, 20, .8);  // woods
		}
		else
		{
			Draw_Fill(x - 63, y, 328 + w, 10, 18, .8);  // woods
		}

		Draw_Fill(x - 64, y, 1, 10, 0, 1);	//Border - Left // woods #scoreboard
		Draw_Fill(x + 264 + w, y, 1, 10, 0, 1);	//Border - Right // woods #scoreboard

	// draw background
		if (S_Voip_Speaking(k))	//spike -- display an underlay for people who are speaking
			Draw_Fill ( x, y, 320-x*2, 8, ((k+1)==cl.viewentity)?75:73, 1);
		//else
		//	Draw_Fill ( x, y, 320-x*2, 8, 0, 0.3);	//or darken it for readability. noisy backgrounds make text hard to read.

		if (s->spectator == 1)	//2 is 'spectator-with-scores' (temporarily inactive players).
		{
			M_PrintWhite (x, y, "spect");
		}
		else
		{
			Draw_FillPlayer ( x, y, 40, 4, s->shirt, 1); //johnfitz -- stretched overlays
			Draw_FillPlayer ( x, y+4, 40, 4, s->pants, 1); //johnfitz -- stretched overlays

		// draw number
			f = s->frags;
			sprintf (num, "%3i",f);

			Draw_Character ( x+8 , y, num[0]); //johnfitz -- stretched overlays
			Draw_Character ( x+16 , y, num[1]); //johnfitz -- stretched overlays
			Draw_Character ( x+24 , y, num[2]); //johnfitz -- stretched overlays

			if (k == cl.viewentity - 1)
				Draw_Character ( x - 8, y, 12); //johnfitz -- stretched overlays
		}

#if 0
{
	int				total;
	int				n, minutes, tens, units;

	// draw time
		total = cl.completed_time - s->entertime;
		minutes = (int)total/60;
		n = total - minutes*60;
		tens = n/10;
		units = n%10;

		sprintf (num, "%3i:%i%i", minutes, tens, units);

		M_Print ( x+48 , y, num); //johnfitz -- was Draw_String, changed for stretched overlays
}
#endif

		sprintf (num, "%4i", s->ping);
		if (ct > 5) // woods don't print 0 print on connect
		M_PrintWhite ((x-8*4)-22, y, num); //johnfitz -- was Draw_String, changed for stretched overlays // woods centered ping #scoreboard

	// draw name
		/*if (cl.matchinp) // match running 0 for CRCTF, 255 for CDMOD
		{
			sprintf (shortname, "%.15s", s->name); // woods only show name, not 'ready' or 'afk' -- 15 characters
			M_PrintWhite (x + 64, y, shortname); //johnfitz -- was Draw_String, changed for stretched overlays // woods changed to white #scoreboard
		}
		else*/
		M_PrintWhite (x + 64, y, s->name); //johnfitz -- was Draw_String, changed for stretched overlays // woods changed to white #scoreboard
		
		y += 10;
	}

	Draw_String(x - 64, y2 - 10, "  ping  frags   name"); // woods #smartstatus

	if (flash() && notready && oneready) // on odd second, if im not ready AND someone else is ready
		M_Print(x + 192, y2 - 10, "status");
	else
		Draw_String
		(x + 192, y2 - 10, "status");

	Draw_Fill(x - 64, y, 329 + w, 1, 0, 1);	//Border - Bottom // woods #scoreboard

	GL_SetCanvas (CANVAS_SBAR); //johnfitz

	if ((!cls.message.cursize && cl.expectingpingtimes < realtime) && (cls.signon >= SIGNONS))
	{
		cl.expectingpingtimes = realtime + 5;
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString(&cls.message, "ping");
	}
}

/*
==================
Sbar_MiniDeathmatchOverlay
==================
*/
void Sbar_MiniDeathmatchOverlay (void)
{
	int	i, k, x, y, f, numlines;
	char	num[12];
	float	scale; //johnfitz
	scoreboard_t	*s;

	scale = CLAMP (1.0f, scr_sbarscale.value, (float)glwidth / 320.0f); //johnfitz

	//MAX_SCOREBOARDNAME = 32, so total width for this overlay plus sbar is 632, but we can cut off some i guess
	if (glwidth/scale < 512 || scr_viewsize.value >= 120) //johnfitz -- test should consider scr_sbarscale
		return;

// scores
	Sbar_SortFrags (true);

// draw the text
	numlines = (scr_viewsize.value >= 110) ? 3 : 6; //johnfitz

	//find us
	for (i = 0; i < scoreboardlines; i++)
		if (fragsort[i] == cl.viewentity - 1)
			break;
	if (i == scoreboardlines) // we're not there
		i = 0;
	else // figure out start
		i = i - numlines/2;
	if (i > scoreboardlines - numlines)
		i = scoreboardlines - numlines;
	if (i < 0)
		i = 0;

	x = 324;
	y = (scr_viewsize.value >= 110) ? 24 : 0; //johnfitz -- start at the right place
	for ( ; i < scoreboardlines && y <= 48; i++, y+=8) //johnfitz -- change y init, test, inc
	{
		k = fragsort[i];
		s = &cl.scores[k];
		if (!s->name[0])
			continue;

	// colors
		Draw_FillPlayer (x, y+1, 40, 4, s->shirt, 1);
		Draw_FillPlayer (x, y+5, 40, 3, s->pants, 1);

	// number
		f = s->frags;
		sprintf (num, "%3i",f);
		Draw_Character (x+ 8, y, num[0]);
		Draw_Character (x+16, y, num[1]);
		Draw_Character (x+24, y, num[2]);

	// brackets
		if (k == cl.viewentity - 1)
		{
			Draw_Character (x, y, 16);
			Draw_Character (x+32, y, 17);
		}

	// name
		Draw_String (x+48, y, s->name);
	}
}

/*
==================
Sbar_IntermissionOverlay
==================
*/
void Sbar_IntermissionOverlay (void)
{
	qpic_t	*pic;
	int	dig;
	int	num;

	if (cl.qcvm.extfuncs.CSQC_DrawScores && !qcvm)
	{
		float s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		GL_SetCanvas (CANVAS_CSQC);
		glEnable (GL_BLEND);
		glDisable (GL_ALPHA_TEST);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
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
		pr_global_struct->time = cl.time;
		pr_global_struct->frametime = host_frametime;
		Sbar_SortFrags (false);
		G_VECTORSET(OFS_PARM0, vid.width/s, vid.height/s, 0);
		G_FLOAT(OFS_PARM1) = sb_showscores;
		PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_DrawScores);
		PR_SwitchQCVM(NULL);
		glDisable (GL_BLEND);
		glEnable (GL_ALPHA_TEST);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glDisable(GL_SCISSOR_TEST);
		glColor3f (1,1,1);
		return;
	}

	if (cl.gametype == GAME_DEATHMATCH)
	{
		Sbar_DeathmatchOverlay ();
		return;
	}

	GL_SetCanvas (CANVAS_MENU); //johnfitz

	pic = Draw_CachePic ("gfx/complete.lmp");
	Draw_Pic (64, 24, pic);

	pic = Draw_CachePic ("gfx/inter.lmp");
	Draw_Pic (0, 56, pic);

	dig = cl.completed_time/60;
	Sbar_IntermissionNumber (152, 64, dig, 3, 0); //johnfitz -- was 160
	num = cl.completed_time - dig*60;
	Draw_Pic (224,64,sb_colon); //johnfitz -- was 234
	Draw_Pic (240,64,sb_nums[0][num/10]); //johnfitz -- was 246
	Draw_Pic (264,64,sb_nums[0][num%10]); //johnfitz -- was 266

	Sbar_IntermissionNumber (152, 104, cl.stats[STAT_SECRETS], 3, 0); //johnfitz -- was 160
	Draw_Pic (224,104,sb_slash); //johnfitz -- was 232
	Sbar_IntermissionNumber (240, 104, cl.stats[STAT_TOTALSECRETS], 3, 0); //johnfitz -- was 248

	Sbar_IntermissionNumber (152, 144, cl.stats[STAT_MONSTERS], 3, 0); //johnfitz -- was 160
	Draw_Pic (224,144,sb_slash); //johnfitz -- was 232
	Sbar_IntermissionNumber (240, 144, cl.stats[STAT_TOTALMONSTERS], 3, 0); //johnfitz -- was 248
}


/*
==================
Sbar_FinaleOverlay
==================
*/
void Sbar_FinaleOverlay (void)
{
	qpic_t	*pic;

	GL_SetCanvas (CANVAS_MOD); //johnfitz -- woods, maintain alignment

	pic = Draw_CachePic ("gfx/finale.lmp");
	Draw_Pic ( (320 - pic->width)/2, 16, pic); //johnfitz -- stretched menus
}