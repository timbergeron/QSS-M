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
// view.c -- player eye positioning

#include "quakedef.h"

extern qboolean	premul_hud;
/*

The view is allowed to move slightly from it's true position for bobbing,
but if it exceeds 8 pixels linear distance (spherical, not box), the list of
entities sent from the server may not include everything in the pvs, especially
when crossing a water boudnary.

*/

cvar_t	scr_ofsx = {"scr_ofsx","0", CVAR_NONE};
cvar_t	scr_ofsy = {"scr_ofsy","0", CVAR_NONE};
cvar_t	scr_ofsz = {"scr_ofsz","0", CVAR_NONE};

cvar_t	cl_rollspeed = {"cl_rollspeed", "200", CVAR_ARCHIVE};
cvar_t	cl_rollangle = {"cl_rollangle", "2.0", CVAR_ARCHIVE};
cvar_t	cl_screenangle = { "cl_screenangle", "0", CVAR_ARCHIVE}; // woods #screenangle
cvar_t	cl_screenangle_speed = { "cl_screenangle_speed", "10", CVAR_ARCHIVE}; // woods #screenangle

cvar_t	cl_bob = {"cl_bob","0.02", CVAR_ARCHIVE};
cvar_t	cl_bobcycle = {"cl_bobcycle","0.6", CVAR_ARCHIVE};
cvar_t	cl_bobup = {"cl_bobup","0.5", CVAR_ARCHIVE};

cvar_t	v_kicktime = {"v_kicktime", "0.5", CVAR_ARCHIVE};
cvar_t	v_kickroll = {"v_kickroll", "0.6", CVAR_ARCHIVE};
cvar_t	v_kickpitch = {"v_kickpitch", "0.6", CVAR_ARCHIVE};
cvar_t	v_gunkick = {"v_gunkick", "1", CVAR_ARCHIVE}; //johnfitz

cvar_t	cl_gun_drift = {"cl_gun_drift", "0", CVAR_ARCHIVE}; // woods #gdrift

cvar_t	v_iyaw_cycle = {"v_iyaw_cycle", "2", CVAR_NONE};
cvar_t	v_iroll_cycle = {"v_iroll_cycle", "0.5", CVAR_NONE};
cvar_t	v_ipitch_cycle = {"v_ipitch_cycle", "1", CVAR_NONE};
cvar_t	v_iyaw_level = {"v_iyaw_level", "0.3", CVAR_NONE};
cvar_t	v_iroll_level = {"v_iroll_level", "0.1", CVAR_NONE};
cvar_t	v_ipitch_level = {"v_ipitch_level", "0.3", CVAR_NONE};

cvar_t	v_viewheight = {"v_viewheight", "0", CVAR_ARCHIVE};
cvar_t	v_idlescale = {"v_idlescale", "0", CVAR_NONE};

cvar_t	crosshair = {"crosshair", "0", CVAR_ARCHIVE};

cvar_t	gl_cshiftpercent = {"gl_cshiftpercent", "100", CVAR_ARCHIVE};
cvar_t	gl_cshiftpercent_contents = {"gl_cshiftpercent_contents", "100", CVAR_ARCHIVE}; // QuakeSpasm
cvar_t	gl_cshiftpercent_damage = {"gl_cshiftpercent_damage", "100", CVAR_ARCHIVE}; // QuakeSpasm
cvar_t	gl_cshiftpercent_bonus = {"gl_cshiftpercent_bonus", "100", CVAR_ARCHIVE}; // QuakeSpasm
cvar_t	gl_cshiftpercent_powerup = {"gl_cshiftpercent_powerup", "100", CVAR_ARCHIVE}; // QuakeSpasm
cvar_t	gl_cshiftpercent_dead = {"gl_cshiftpercent_dead", "0", CVAR_ARCHIVE}; // woods #cdead

cvar_t	r_viewmodel_quake = {"r_viewmodel_quake", "0", CVAR_ARCHIVE};
cvar_t	cl_demo_eyecam = {"cl_demo_eyecam", "0", CVAR_NONE}; // woods #demoeyecam

float	v_dmg_time, v_dmg_roll, v_dmg_pitch;

extern	int			in_forward, in_forward2, in_back;
extern	qboolean	qeintermission; // woods #qeintermission #cdead
extern	qboolean	crxintermission; // woods #crxintermission #cdead

static GLuint polyblend_vignette_texture; // woods #polylblend2
static int polyblend_vignette_size = 2048; // woods #polylblend2

// GLSL vignette path: computes the gradient analytically and adds zero-mean
// temporal alpha dither AFTER multiplying by uBlend.a, so the dither survives
// at low cshifts (where the texture path's baked dither gets compressed away).
static GLuint   polyblend_program;
static GLint    polyblend_blendLoc = -1; // -1 is GL's sentinel for unknown/inactive
static GLint    polyblend_frameLoc = -1;
// Latched on a failed create so we don't retry every frame. Without this guard,
// a uniform-lookup failure would leak a tracked GL program per frame until
// R_DeleteShaders, because GL_GetUniformLocation zeroes the program handle
// without untracking it.
static qboolean polyblend_shader_failed;

vec3_t	v_punchangles[2]; //johnfitz -- copied from cl.punchangle.  0 is current, 1 is previous value. never the same unless map just loaded
double	v_punchangles_times[2]; //spike -- times, to avoid assumptions...

void SCR_SetupAutoID(void); // woods #autoid
void SCR_DrawAutoID(void); // woods #autoid
void SCR_DrawStatusIndicators(void); // woods #autoid

static void V_Viewheight_Completion_f (cvar_t *cvar, const char *partial)
{
	int i;

	(void)cvar;

	for (i = -7; i <= 4; i++)
	{
		const char *type = NULL;
		char value[8];

		if (i == -7)
			type = "lowest";
		else if (i == -6)
			type = "aim height";
		else if (i == 0)
			type = "default";
		else if (i == 4)
			type = "highest";

		q_snprintf (value, sizeof(value), "%d", i);
		Con_AddToTabList (value, partial, type, NULL);
	}
}

/*
===============
V_CalcRoll

Used by view and sv_user
===============
*/
float V_CalcRoll (vec3_t angles, vec3_t velocity)
{
	vec3_t	forward, right, up;
	float	sign;
	float	side;
	float	value;

	AngleVectors (angles, forward, right, up);
	side = DotProduct (velocity, right);
	sign = side < 0 ? -1 : 1;
	side = fabs(side);

	value = cl_rollangle.value;
//	if (cl.inwater)
//		value *= 6;

	if (side < cl_rollspeed.value)
		side = side * value / cl_rollspeed.value;
	else
		side = value;

	return side*sign;
}


/*
===============
V_CalcBob

===============
*/
float V_CalcBob (void)
{
	float	bob;
	float	cycle;

	if (!cl_bobcycle.value) /* Avoid divide-by-zero, don't bob */
		return 0.0f;

	cycle = cl.time - (int)(cl.time/cl_bobcycle.value)*cl_bobcycle.value;
	cycle /= cl_bobcycle.value;
	if (cycle < cl_bobup.value)
		cycle = M_PI * cycle / cl_bobup.value;
	else
		cycle = M_PI + M_PI*(cycle-cl_bobup.value)/(1.0 - cl_bobup.value);

// bob is proportional to velocity in the xy plane
// (don't count Z, or jumping messes it up)

	bob = sqrt(cl.velocity[0]*cl.velocity[0] + cl.velocity[1]*cl.velocity[1]) * cl_bob.value;
//Con_Printf ("speed: %5.1f\n", VectorLength(cl.velocity));
	bob = bob*0.3 + bob*0.7*sin(cycle);
	if (bob > 4)
		bob = 4;
	else if (bob < -7)
		bob = -7;
	return bob;
}


//=============================================================================


cvar_t	v_centermove = {"v_centermove", "0.15", CVAR_NONE};
cvar_t	v_centerspeed = {"v_centerspeed","500", CVAR_NONE};


void V_StartPitchDrift (void)
{
#if 1
	if (cl.laststop == cl.time)
	{
		return;		// something else is keeping it from drifting
	}
#endif
	cl.lastcenterstart = cl.time;
	if (cl.nodrift || !cl.pitchvel)
	{
		cl.pitchvel = v_centerspeed.value;
		cl.nodrift = false;
		cl.driftmove = 0;
	}
}

void V_StopPitchDrift (void)
{
	cl.laststop = cl.time;
	cl.nodrift = true;
	cl.pitchvel = 0;
}

/*
===============
V_DriftPitch

Moves the client pitch angle towards cl.idealpitch sent by the server.

If the user is adjusting pitch manually, either with lookup/lookdown,
mlook and mouse, or klook and keyboard, pitch drifting is constantly stopped.

Drifting is enabled when the center view key is hit, mlook is released and
lookspring is non 0, or when
===============
*/
void V_DriftPitch (void)
{
	float		delta, move;

	if (noclip_anglehack || !cl.onground || cls.demoplayback )
	//FIXME: noclip_anglehack is set on the server, so in a nonlocal game this won't work.
	{
		cl.driftmove = 0;
		cl.pitchvel = 0;
		return;
	}

// don't count small mouse motion
	if (cl.nodrift)
	{
		if ( fabs(cl.movecmds[(cl.movemessages-1)&MOVECMDS_MASK].forwardmove) < cl_forwardspeed.value)
			cl.driftmove = 0;
		else
			cl.driftmove += host_frametime;

		if ( cl.driftmove > v_centermove.value)
		{
			if (lookspring.value)
				V_StartPitchDrift ();
		}
		return;
	}

	delta = cl.statsf[STAT_IDEALPITCH] - cl.viewangles[PITCH];

	if (!delta)
	{
		cl.pitchvel = 0;
		return;
	}

	move = host_frametime * cl.pitchvel;
	cl.pitchvel += host_frametime * v_centerspeed.value;

//Con_Printf ("move: %f (%f)\n", move, host_frametime);

	if (delta > 0)
	{
		if (move > delta)
		{
			cl.pitchvel = 0;
			move = delta;
		}
		cl.viewangles[PITCH] += move;
	}
	else if (delta < 0)
	{
		if (move > -delta)
		{
			cl.pitchvel = 0;
			move = -delta;
		}
		cl.viewangles[PITCH] -= move;
	}
}

/*
==============================================================================

	VIEW BLENDING

==============================================================================
*/

cshift_t	cshift_empty = { {130,80,50}, 0 };
cshift_t	cshift_water = { {130,80,50}, 128 };
cshift_t	cshift_slime = { {0,25,5}, 150 };
cshift_t	cshift_lava = { {255,80,0}, 150 };

float		v_blend[4];		// rgba 0.0 - 1.0
static cshift_t	map_scoped_cshift_empty;
static qboolean	map_scoped_cshift_empty_saved;

//johnfitz -- deleted BuildGammaTable(), V_CheckGamma(), gammatable[], and ramps[][]

/*
============
V_ResetEffects
============
*/
void V_ResetEffects (void)
{
	cshift_empty.percent = 0;
	memset (v_blend, 0, sizeof (v_blend));
	v_dmg_time = 0.f;
	v_dmg_roll = 0.f;
	v_dmg_pitch = 0.f;
}

qboolean V_MapScoped_MarkServerStuffCmd (const char *cmd)
{
	if (!cmd || !*cmd)
		return false;
	if (q_strcasecmp (cmd, "v_cshift"))
		return false;

	if (!map_scoped_cshift_empty_saved)
	{
		map_scoped_cshift_empty = cshift_empty;
		map_scoped_cshift_empty_saved = true;
	}
	return true;
}

void V_MapScoped_RestoreServerStuff (void)
{
	if (!map_scoped_cshift_empty_saved)
		return;

	// v_cshift is not a cvar, so restore the saved server-stuffed state unconditionally.
	cshift_empty = map_scoped_cshift_empty;
	map_scoped_cshift_empty_saved = false;
}

/*
===============
V_ParseDamage
===============
*/
void V_ParseDamage (void)
{
	int		armor, blood;
	vec3_t	from;
	int		i;
	vec3_t	forward, right, up;
	entity_t	*ent;
	float	side;
	float	count;

	armor = MSG_ReadByte ();
	blood = MSG_ReadByte ();
	for (i=0 ; i<3 ; i++)
		from[i] = MSG_ReadCoord (cl.protocolflags);

	if (cl.qcvm.extfuncs.CSQC_Parse_Damage)
	{
		qboolean inhibit;
		PR_SwitchQCVM(&cl.qcvm);
		pr_global_struct->time = cl.time;
		G_FLOAT(OFS_PARM0) = armor;
		G_FLOAT(OFS_PARM1) = blood;
		G_VECTORSET(OFS_PARM2, from[0], from[1], from[2]);
		PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_Parse_Damage);
		inhibit = G_FLOAT(OFS_RETURN);
		PR_SwitchQCVM(NULL);
		if (inhibit)
			return;
	}

	count = blood*0.5 + armor*0.5;
	if (count < 10)
		count = 10;

	cl.faceanimtime = cl.time + 0.2;		// but sbar face into pain frame

	cl.cshifts[CSHIFT_DAMAGE].percent += 3*count;
	if (cl.cshifts[CSHIFT_DAMAGE].percent < 0)
		cl.cshifts[CSHIFT_DAMAGE].percent = 0;
	if (cl.cshifts[CSHIFT_DAMAGE].percent > 150)
		cl.cshifts[CSHIFT_DAMAGE].percent = 150;

	if (armor > blood)
	{
		cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 200;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 100;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 100;
	}
	else if (armor)
	{
		cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 220;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 50;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 50;
	}
	else
	{
		cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 255;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 0;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 0;
	}

//
// calculate view angle kicks
//
	ent = &cl.entities[cl.viewentity];

	VectorSubtract (from, ent->origin, from);
	VectorNormalize (from);

	AngleVectors (ent->angles, forward, right, up);

	side = DotProduct (from, right);
	v_dmg_roll = count*side*v_kickroll.value;

	side = DotProduct (from, forward);
	v_dmg_pitch = count*side*v_kickpitch.value;

	v_dmg_time = v_kicktime.value;
}


/*
==================
V_cshift_f
==================
*/
void V_cshift_f (void)
{
	cshift_empty.destcolor[0] = atoi(Cmd_Argv(1));
	cshift_empty.destcolor[1] = atoi(Cmd_Argv(2));
	cshift_empty.destcolor[2] = atoi(Cmd_Argv(3));
	cshift_empty.percent = atoi(Cmd_Argv(4));
}


/*
==================
V_BonusFlash_f

When you run over an item, the server sends this command
==================
*/
void V_BonusFlash_f (void)
{
	if (Cmd_Argc() >= 5)
	{
		cl.cshifts[CSHIFT_BONUS].destcolor[0] = atof(Cmd_Argv(1))*255;
		cl.cshifts[CSHIFT_BONUS].destcolor[1] = atof(Cmd_Argv(2))*255;
		cl.cshifts[CSHIFT_BONUS].destcolor[2] = atof(Cmd_Argv(3))*255;
		cl.cshifts[CSHIFT_BONUS].percent = atof(Cmd_Argv(4))*255;
	}
	else
	{
		cl.cshifts[CSHIFT_BONUS].destcolor[0] = 215;
		cl.cshifts[CSHIFT_BONUS].destcolor[1] = 186;
		cl.cshifts[CSHIFT_BONUS].destcolor[2] = 69;
		cl.cshifts[CSHIFT_BONUS].percent = 50;
	}
}

/*
=============
CShift_ParseWorldspawn //infin -- woods tag

called at map load
=============
*/

void CShift_ParseWorldspawn(void)
{
	char key[128], value[4096];
	const char* data;

	// reset default cshift values
	cshift_water = (cshift_t){ {130,80,50}, 128 };
	cshift_slime = (cshift_t){ {0,25,5}, 150 };
	cshift_lava = (cshift_t){ {255,80,0}, 150 };

	data = COM_Parse(cl.worldmodel->entities);
	if (!data)
		return; // error
	if (com_token[0] != '{')
		return; // error
	while (1)
	{
		data = COM_Parse(data);
		if (!data)
			return; // error
		if (com_token[0] == '}')
			break; // end of worldspawn
		if (com_token[0] == '_')
			q_strlcpy(key, com_token + 1, sizeof(key));
		else
			q_strlcpy(key, com_token, sizeof(key));
		while (key[0] && key[strlen(key) - 1] == ' ') // remove trailing spaces
			key[strlen(key) - 1] = 0;
		data = COM_Parse(data);
		if (!data)
			return; // error
		q_strlcpy(value, com_token, sizeof(value));

		if (!strcmp("cshiftwater", key))
		{
			sscanf(value, "%d %d %d", &cshift_water.destcolor[0], &cshift_water.destcolor[1], &cshift_water.destcolor[2]);
			cshift_water.percent = 128;
		}

		if (!strcmp("cshiftslime", key))
		{
			sscanf(value, "%d %d %d", &cshift_slime.destcolor[0], &cshift_slime.destcolor[1], &cshift_slime.destcolor[2]);
			cshift_slime.percent = 150;
		}

		if (!strcmp("cshiftlava", key))
		{
			sscanf(value, "%d %d %d", &cshift_lava.destcolor[0], &cshift_lava.destcolor[1], &cshift_lava.destcolor[2]);
			cshift_lava.percent = 150;
		}
	}
}

/*
=============
V_SetContentsColor

Underwater, lava, etc each has a color shift
=============
*/
void V_SetContentsColor (int contents)
{
	switch (contents)
	{
	case CONTENTS_EMPTY:
	case CONTENTS_SOLID:
	case CONTENTS_SKY: //johnfitz -- no blend in sky
		cl.cshifts[CSHIFT_CONTENTS] = cshift_empty;
		break;
	case CONTENTS_LAVA:
		cl.cshifts[CSHIFT_CONTENTS] = cshift_lava;
		break;
	case CONTENTS_SLIME:
		cl.cshifts[CSHIFT_CONTENTS] = cshift_slime;
		break;
	default:
		cl.cshifts[CSHIFT_CONTENTS] = cshift_water;
	}
}

/*
=============
V_CalcPowerupCshift
=============
*/
void V_CalcPowerupCshift (void)
{
	if (cl.items & IT_QUAD)
	{
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 255;
		cl.cshifts[CSHIFT_POWERUP].percent = 30;
	}
	else if (cl.items & IT_SUIT)
	{
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 0;
		cl.cshifts[CSHIFT_POWERUP].percent = 20;
	}
	else if (cl.items & IT_INVISIBILITY)
	{
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 100;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 100;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 100;
		cl.cshifts[CSHIFT_POWERUP].percent = 100;
	}
	else if (cl.items & IT_INVULNERABILITY)
	{
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 0;
		cl.cshifts[CSHIFT_POWERUP].percent = 30;
	}
	else
		cl.cshifts[CSHIFT_POWERUP].percent = 0;
}

/*
=============
V_CalcBlend
=============
*/
void V_CalcBlend (void)
{
	float	r, g, b, a, a2;
	int		j;
	cvar_t	*cshiftpercent_cvars[NUM_CSHIFTS] = {
		&gl_cshiftpercent_contents,
		&gl_cshiftpercent_damage,
		&gl_cshiftpercent_bonus,
		&gl_cshiftpercent_powerup,
		&gl_cshiftpercent_dead // woods #cdead
	};

	r = 0;
	g = 0;
	b = 0;
	a = 0;

	for (j=0 ; j<NUM_CSHIFTS ; j++)
	{
		if (!gl_cshiftpercent.value)
			continue;

		//johnfitz -- only apply leaf contents color shifts during intermission
		if ((cl.intermission || qeintermission || crxintermission) && j != CSHIFT_CONTENTS) // woods #cdead
			continue;
		//johnfitz

		a2 = ((cl.cshifts[j].percent * gl_cshiftpercent.value) / 100.0) / 255.0;
		// QuakeSpasm -- also scale by the specific gl_cshiftpercent_* cvar
		a2 *= (cshiftpercent_cvars[j]->value / 100.0);
		// QuakeSpasm
		if (!a2)
			continue;
		a = a + a2*(1-a);
		a2 = a2/a;
		r = r*(1-a2) + cl.cshifts[j].destcolor[0]*a2;
		g = g*(1-a2) + cl.cshifts[j].destcolor[1]*a2;
		b = b*(1-a2) + cl.cshifts[j].destcolor[2]*a2;
	}

	v_blend[0] = r/255.0;
	v_blend[1] = g/255.0;
	v_blend[2] = b/255.0;
	v_blend[3] = a;
	if (v_blend[3] > 1)
		v_blend[3] = 1;
	if (v_blend[3] < 0)
		v_blend[3] = 0;
}

/*
=============
V_UpdateBlend -- johnfitz -- V_UpdatePalette cleaned up and renamed
=============
*/
void V_UpdateBlend (void)
{
	int		i, j;
	float	frametime = fabs(cl.time - cl.oldtime); // time can go backwards when rewinding demos // woods (iw) #democontrols
	qboolean	blend_changed;

	V_CalcPowerupCshift ();

	blend_changed = false;

	for (i=0 ; i<NUM_CSHIFTS ; i++)
	{
		if (cl.cshifts[i].percent != cl.prev_cshifts[i].percent)
		{
			blend_changed = true;
			cl.prev_cshifts[i].percent = cl.cshifts[i].percent;
		}
		for (j=0 ; j<3 ; j++)
			if (cl.cshifts[i].destcolor[j] != cl.prev_cshifts[i].destcolor[j])
			{
				blend_changed = true;
				cl.prev_cshifts[i].destcolor[j] = cl.cshifts[i].destcolor[j];
			}
	}

// drop the damage value
	cl.cshifts[CSHIFT_DAMAGE].percent -= frametime*150; // woods iw
	if (cl.cshifts[CSHIFT_DAMAGE].percent <= 0)
		cl.cshifts[CSHIFT_DAMAGE].percent = 0;

// drop the bonus value
	cl.cshifts[CSHIFT_BONUS].percent -= frametime*100; // woods iw
	if (cl.cshifts[CSHIFT_BONUS].percent <= 0)
		cl.cshifts[CSHIFT_BONUS].percent = 0;

	// handle death cshift fade-in/fade-out -- woods #cdead
	if (cl.stats[STAT_HEALTH] <= 0)
	{
		if (cl.cshifts[CSHIFT_DEAD].percent < 150)
		{
			cl.cshifts[CSHIFT_DEAD].percent += frametime * 1500; // fade in over 0.1 seconds
			if (cl.cshifts[CSHIFT_DEAD].percent > 150)
				cl.cshifts[CSHIFT_DEAD].percent = 150;
			blend_changed = true;
		}
	}
	else
	{
		// immediately clear death cshift when alive
		if (cl.cshifts[CSHIFT_DEAD].percent > 0)
		{
			cl.cshifts[CSHIFT_DEAD].percent = 0;
			blend_changed = true;
		}
	}

	if (blend_changed)
		V_CalcBlend ();
}

/*
============
PolyBlend_CreateShader -- analytical vignette + alpha dither, post-multiply
============
*/
static void PolyBlend_CreateShader (void)
{
	const GLchar *vertSource = \
		"#version 110\n"
		"void main(void) {\n"
		"	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
		"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
		"}\n";

	// Vignette is computed from a centered [-1,1] coord. Dither is added in
	// 0..1 alpha space AFTER vignette*uBlend.a, so the framebuffer-LSB dither
	// amplitude is independent of uBlend.a. Temporal IGN seed comes from uFrame.
	const GLchar *fragSource = \
		"#version 110\n"
		"uniform vec4  uBlend;\n"
		"uniform float uFrame;\n"
		"void main(void) {\n"
		"	vec2 p = gl_TexCoord[0].xy * 2.0 - 1.0;\n"
		"	float dist = length(p);\n"
		"	float inner = 0.55;\n"
		"	float outer = 1.45;\n"
		"	float v = clamp((dist - inner) / (outer - inner), 0.0, 1.0);\n"
		"	v = v * v * (3.0 - 2.0 * v);\n"
		"	float a = v * uBlend.a;\n"
		"	if (v > 0.0 && v < 1.0) {\n"
		"		vec2 q = gl_FragCoord.xy + vec2(uFrame, uFrame * 1.6180339);\n"
		"		float ign = fract(52.9829189 * fract(0.06711056 * q.x + 0.00583715 * q.y));\n"
		"		a += (ign - 0.5) * (1.0 / 255.0);\n"
		"	}\n"
		"	gl_FragColor = vec4(uBlend.rgb, clamp(a, 0.0, uBlend.a));\n"
		"}\n";

	if (!gl_glsl_able)
		return;

	GLuint program = GL_CreateProgram (vertSource, fragSource, 0, NULL);
	if (!program)
	{
		polyblend_shader_failed = true;
		return;
	}

	// GL_GetUniformLocation zeroes its program-ptr arg on lookup failure but
	// leaves the program tracked, so keep the original handle to untrack.
	GLuint tracked = program;
	GLint blendLoc = GL_GetUniformLocation (&program, "uBlend");
	GLint frameLoc = GL_GetUniformLocation (&program, "uFrame");
	if (!program)
	{
		GL_DeleteProgramTracked (&tracked);
		polyblend_shader_failed = true;
		return;
	}

	polyblend_program  = program;
	polyblend_blendLoc = blendLoc;
	polyblend_frameLoc = frameLoc;
}

/*
============
PolyBlend_CreateVignetteTexture -- creates a radial gradient texture for smooth vignette -- woods #polylblend2
============
*/
static void PolyBlend_CreateVignetteTexture (void)
{
	int x, y;
	int size = polyblend_vignette_size;
	byte *data;
	float cx, cy;
	float inner_radius = 0.55f;  // where transparency starts
	float outer_radius = 1.45f; // where full opacity is reached
	
	if (gl_hardware_maxsize && size > gl_hardware_maxsize)
		size = gl_hardware_maxsize;
	
	cx = size / 2.0f;
	cy = size / 2.0f;

	data = (byte *)malloc (size * size * 2);
	if (!data)
	{
		Con_Warning ("PolyBlend_CreateVignetteTexture: failed to allocate %d bytes\n", size * size * 2);
		return;
	}
	
	for (y = 0; y < size; y++)
	{
		for (x = 0; x < size; x++)
		{
			float dx = (x - cx) / cx;
			float dy = (y - cy) / cy;
			float dist = sqrtf(dx * dx + dy * dy);
			float alpha;

			// Smoothstep-like falloff
			if (dist <= inner_radius)
				alpha = 0.0f;
			else if (dist >= outer_radius)
				alpha = 1.0f;
			else
			{
				float t = (dist - inner_radius) / (outer_radius - inner_radius);
				// Smooth hermite interpolation (smoothstep)
				alpha = t * t * (3.0f - 2.0f * t);
			}

			data[(y * size + x) * 2 + 0] = 255; // Luminance (White)
			data[(y * size + x) * 2 + 1] = (byte)(alpha * 255.0f); // Alpha
		}
	}
	
	glGenTextures (1, &polyblend_vignette_texture);
	glBindTexture (GL_TEXTURE_2D, polyblend_vignette_texture);
	// NOTE: GL_LUMINANCE_ALPHA is compatibility-profile only; core-profile would need GL_R8 + swizzle
	glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, size, size, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, data);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	free (data);

	Con_DPrintf ("Vignette texture created (%dx%d)\n", size, size);
}

/*
============
PolyBlend_DeleteVignetteTexture -- cleanup texture on video mode change -- woods #polylblend2
============
*/
void PolyBlend_DeleteVignetteTexture (void)
{
	if (polyblend_vignette_texture)
	{
		glDeleteTextures (1, &polyblend_vignette_texture);
		polyblend_vignette_texture = 0;
	}
	polyblend_program = 0; // deleted in R_DeleteShaders
	polyblend_blendLoc = -1;
	polyblend_frameLoc = -1;
	polyblend_shader_failed = false;
}

/*
============
V_PolyBlend -- johnfitz -- moved here from gl_rmain.c, and rewritten to use glOrtho
============
*/
void V_PolyBlend (void)
{
	if (!gl_polyblend.value || !v_blend[3])
		return;

	if ((int)gl_polyblend.value != 2 && GLSLGamma_SoftEmuCanRemapBlend() &&
		(v_blend[0] > 0.001f || v_blend[1] > 0.001f || v_blend[2] > 0.001f))
		return;

	GL_DisableMultitexture();

	glDisable (GL_TEXTURE_2D);
	glDisable (GL_DEPTH_TEST);
	glEnable (GL_BLEND);
	if (premul_hud)
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	else
		glDisable (GL_ALPHA_TEST);

	// Mode 2: Vignette. GLSL path adds dither after the vignette*v_blend.a
	// multiply so it survives at low cshifts; the texture path is a fallback
	// for systems without GLSL or where shader creation failed.
	qboolean use_shader = ((int)gl_polyblend.value == 2 && gl_glsl_able && GL_UseProgramFunc && !polyblend_shader_failed);
	if (use_shader && !polyblend_program)
		PolyBlend_CreateShader ();
	if (use_shader && !polyblend_program)
		use_shader = false; // create just failed; polyblend_shader_failed is now set

	if (use_shader)
	{
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity ();
		glOrtho (0, 1, 1, 0, -99999, 99999);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity ();

		// Alpha test still applies to shader output in compat profile, so
		// disable it for the same reason the texture path does. The cleanup
		// block below restores it when !premul_hud.
		glDisable (GL_ALPHA_TEST);

		GL_UseProgramFunc (polyblend_program);
		GL_Uniform4fFunc (polyblend_blendLoc, v_blend[0], v_blend[1], v_blend[2], v_blend[3]);
		GL_Uniform1fFunc (polyblend_frameLoc, (float)(host_framecount & 1023));

		glBegin (GL_QUADS);
		glTexCoord2f (0, 0); glVertex2f (0, 0);
		glTexCoord2f (1, 0); glVertex2f (1, 0);
		glTexCoord2f (1, 1); glVertex2f (1, 1);
		glTexCoord2f (0, 1); glVertex2f (0, 1);
		glEnd ();

		GL_UseProgramFunc (0);
	}
	else if ((int)gl_polyblend.value == 2)
	{
		GLboolean alpha_test_was_enabled = glIsEnabled(GL_ALPHA_TEST);
		
		// Create texture on first use
		if (!polyblend_vignette_texture)
			PolyBlend_CreateVignetteTexture ();
		
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity ();
		glOrtho (0, 1, 1, 0, -99999, 99999);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity ();
		
		// Enable texturing for the vignette
		glEnable (GL_TEXTURE_2D);
		glDisable (GL_ALPHA_TEST); // Ensure smooth gradients aren't clipped
		glBindTexture (GL_TEXTURE_2D, polyblend_vignette_texture);
		
		// Save and set texture environment mode
		GLint prev_texenv_mode;
		glGetTexEnviv (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &prev_texenv_mode);
		glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		
		// Draw fullscreen quad with vignette texture
		glBegin (GL_QUADS);
		glColor4f (v_blend[0], v_blend[1], v_blend[2], v_blend[3]);
		glTexCoord2f (0, 0);
		glVertex2f (0, 0);
		glTexCoord2f (1, 0);
		glVertex2f (1, 0);
		glTexCoord2f (1, 1);
		glVertex2f (1, 1);
		glTexCoord2f (0, 1);
		glVertex2f (0, 1);
		glEnd ();
		
		glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, prev_texenv_mode);
		glDisable (GL_TEXTURE_2D);
		if (alpha_test_was_enabled)
			glEnable (GL_ALPHA_TEST);
	}
	else
	{
		// Mode 1: Original solid fullscreen blend
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity ();
		glOrtho (0, 1, 1, 0, -99999, 99999);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity ();

		glBegin (GL_QUADS);

		glColor4fv (v_blend); // inside glBegin / glEnd to workaround an AMD driver bug

		glVertex2f (0,0);
		glVertex2f (1, 0);
		glVertex2f (1, 1);
		glVertex2f (0, 1);
		glEnd ();
	}

	glEnable (GL_DEPTH_TEST);
	glEnable (GL_TEXTURE_2D);
	if (premul_hud)
		glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	else
	{
		glDisable (GL_BLEND);
		glEnable (GL_ALPHA_TEST);
	}
}

/*
==============================================================================

	VIEW RENDERING

==============================================================================
*/

/*
==================
V_CalcGunDrift -- woods #gdrift

Adds a delay/lag effect to the viewmodel based on view rotation.
Ported from Qrack (originally from OpenKatana by Eukos).
==================
*/
static void V_CalcGunDrift (vec3_t origin, vec3_t angles)
{
	int		i;
	float		speed, diff_length;
	float		side, vert;
	static vec3_t	lastfacing;
	static double	lasttime;
	vec3_t		forward, right, up, diff;

	if (host_frametime == 0.0f)
		return;

	AngleVectors (cl.lerpangles, forward, right, up);

	// Re-init on first use, time discontinuity (map change/demo restart), or enabling from 0
	if (lasttime == 0 || cl.time < lasttime || VectorLength(lastfacing) == 0)
	{
		VectorCopy (forward, lastfacing);
		lasttime = cl.time;
		return;
	}
	lasttime = cl.time;

	VectorSubtract (forward, lastfacing, diff);
	speed = 6.0f;
	diff_length = VectorLength (diff);

	if (cl_gun_drift.value > 0 && diff_length > cl_gun_drift.value)
		speed *= diff_length / cl_gun_drift.value;

	// Interpolate lastfacing toward forward
	for (i = 0; i < 3; i++)
		lastfacing[i] += diff[i] * (speed * host_frametime);
	VectorNormalize (lastfacing);

	// Skip visual offset if disabled
	if (cl_gun_drift.value <= 0)
		return;

	// Project world-space diff onto view axes to get view-relative offset
	side = DotProduct (diff, right);   // horizontal turn amount
	vert = DotProduct (diff, up);      // vertical turn amount

	// Apply offset in view-relative coordinates (gun lags opposite to turn)
	origin[1] += side * -5.0f;
	origin[2] += vert * -5.0f;

	// Roll based on horizontal turn
	angles[ROLL] += side * 3.0f;
}

/*
==================
CalcGunAngle
==================
*/
void CalcGunAngle (void)
{
	cl.viewent.angles[ROLL] = -v_idlescale.value * sin(cl.time*v_iroll_cycle.value) * v_iroll_level.value - r_refdef.viewangles[ROLL];
	cl.viewent.angles[PITCH] = -v_idlescale.value * sin(cl.time*v_ipitch_cycle.value) * v_ipitch_level.value;
	cl.viewent.angles[YAW] = -v_idlescale.value * sin(cl.time*v_iyaw_cycle.value) * v_iyaw_level.value;
}

/*
==============
V_BoundOffsets
==============
*/
void V_BoundOffsets (void)
{
	entity_t	*ent;

	ent = &cl.entities[cl.viewentity];

// absolutely bound refresh reletive to entity clipping hull
// so the view can never be inside a solid wall

	if (r_refdef.vieworg[0] < ent->origin[0] - 14)
		r_refdef.vieworg[0] = ent->origin[0] - 14;
	else if (r_refdef.vieworg[0] > ent->origin[0] + 14)
		r_refdef.vieworg[0] = ent->origin[0] + 14;
	if (r_refdef.vieworg[1] < ent->origin[1] - 14)
		r_refdef.vieworg[1] = ent->origin[1] - 14;
	else if (r_refdef.vieworg[1] > ent->origin[1] + 14)
		r_refdef.vieworg[1] = ent->origin[1] + 14;
	if (r_refdef.vieworg[2] < ent->origin[2] - 22)
		r_refdef.vieworg[2] = ent->origin[2] - 22;
	else if (r_refdef.vieworg[2] > ent->origin[2] + 30)
		r_refdef.vieworg[2] = ent->origin[2] + 30;
}

/*
==============
V_AddIdle

Idle swaying
==============
*/
void V_AddIdle (void)
{
	r_refdef.viewangles[ROLL] += v_idlescale.value * sin(cl.time*v_iroll_cycle.value) * v_iroll_level.value;
	r_refdef.viewangles[PITCH] += v_idlescale.value * sin(cl.time*v_ipitch_cycle.value) * v_ipitch_level.value;
	r_refdef.viewangles[YAW] += v_idlescale.value * sin(cl.time*v_iyaw_cycle.value) * v_iyaw_level.value;
}

// woods #screenangle (kilomile -- bloodrails)

float lerp(float a, float b, float alpha)
{
	return a + alpha * (b - a);
}

float lerp_alpha = 0;
qboolean isTransitioning = false;
float previousRoll = 0;  // this will store the starting roll value for the lerp

void OnClScreenAngleChanged (void) // woods #screenangle
{
	lerp_alpha = 0;
	isTransitioning = true;
	previousRoll = r_refdef.viewangles[ROLL];
}

/*
====================
OnClScreenAngleChanged_f -- woods #screenangle
====================
*/
static void OnClScreenAngleChanged_f (cvar_t* var)
{
	OnClScreenAngleChanged();
}

/*
==============
V_CalcViewRoll

Roll is induced by movement and damage
==============
*/
void V_CalcViewRoll (void)
{
	float		side;

	side = V_CalcRoll (cl.entities[cl.viewentity].angles, cl.velocity);
	r_refdef.viewangles[ROLL] += side;

	if (v_dmg_time > 0)
	{
		r_refdef.viewangles[ROLL] += v_dmg_time/v_kicktime.value*v_dmg_roll;
		r_refdef.viewangles[PITCH] += v_dmg_time/v_kicktime.value*v_dmg_pitch;
		v_dmg_time -= fabs(cl.time - cl.oldtime); // woods (iw) #democontrols
	}

	if (cl.stats[STAT_HEALTH] <= 0)
	{
		r_refdef.viewangles[ROLL] = 80;	// dead view angle
		memcpy (cl.death_location, cl.entities[cl.viewentity].origin, sizeof(vec3_t)); // woods

		return;
	}

	if (isTransitioning) // woods #screenangle lerp transition
	{
		r_refdef.viewangles[ROLL] = lerp(previousRoll, cl_screenangle.value, lerp_alpha);
		lerp_alpha += host_frametime * cl_screenangle_speed.value;  // adjust this value to control the speed of the transition

		if (lerp_alpha >= 1) 
		{
			r_refdef.viewangles[ROLL] = cl_screenangle.value;  // ensure it's set to the exact target value
			isTransitioning = false;  // stop the transition
		}
	}
	else {
		r_refdef.viewangles[ROLL] = cl_screenangle.value; // state when not transitioning
	}
}

/*
==================
V_FindDemoEyecamTarget

Detect which player entity the observer is chasing during demo playback.
Uses proximity heuristics: chasecam view is usually close to the target.
==================
*/
static int V_FindDemoEyecamTarget (void)
{
	entity_t	*viewer;
	vec3_t		diff;
	float		dist, bestdist;
	int			i, best;

	if (!cls.demoplayback || !cl_demo_eyecam.value)
		return 0;

	if (!cl.entities || cl.viewentity < 1 || cl.viewentity >= cl.num_entities)
		return 0;

	if (cl.maxclients <= 0)
		return 0;

	viewer = &cl.entities[cl.viewentity];
	if (!viewer->model)
		return 0;

	best = 0;
	bestdist = 999999.0f;

	for (i = 1; i <= cl.maxclients; i++)
	{
		entity_t *target;

		if (i >= cl.num_entities || i == cl.viewentity)
			continue;

		target = &cl.entities[i];
		if (!target->model)
			continue;

		VectorSubtract (target->origin, viewer->origin, diff);
		dist = VectorLength (diff);

		// chasecam typically within 500 units
		if (dist > 600)
			continue;

		if (dist < bestdist)
		{
			bestdist = dist;
			best = i;
		}
	}

	return best;
}

/*
==================
V_ShouldApplyDemoEyecam

Gate for cl_demo_eyecam. Suppress the override when the recorder's
*observer userinfo key is "eyecam" (demo is already first-person), or
when the nearest player is too far away to plausibly be a chase target.
Observer chase mode locks the camera ~100 units behind the target, so a
small distance is a strong signal we're following them.
==================
*/
static qboolean V_ShouldApplyDemoEyecam (int target)
{
	char		 buf1[32], buf2[32];
	const char	*userinfo;
	const char	*obs;
	const char	*star_obs;
	entity_t	*viewer;
	entity_t	*targetent;
	vec3_t		 diff;

	if (target <= 0 || target > cl.maxclients || target >= cl.num_entities)
		return false;

	userinfo = CL_GetSafeViewEntityUserinfo ();
	obs = Info_GetKey (userinfo, "observer", buf1, sizeof(buf1));
	star_obs = Info_GetKey (userinfo, "*observer", buf2, sizeof(buf2));

	if (!strcmp(obs, "eyecam") || !strcmp(star_obs, "eyecam"))
		return false;

	if (!cl.entities || cl.viewentity < 1 || cl.viewentity >= cl.num_entities)
		return false;

	viewer = &cl.entities[cl.viewentity];
	targetent = &cl.entities[target];
	VectorSubtract (targetent->origin, viewer->origin, diff);
	if (VectorLength (diff) > 200.0f)
		return false;

	return true;
}

/*
==================
V_CalcIntermissionRefdef

==================
*/
void V_CalcIntermissionRefdef (void)
{
	entity_t	*ent, *view;
	float		old;

// ent is the player model (visible when out of body)
	ent = &cl.entities[cl.viewentity];
// view is the weapon model (only visible from inside body)
	view = &cl.viewent;

	VectorCopy (ent->origin, r_refdef.vieworg);
	VectorCopy (ent->angles, r_refdef.viewangles);
	view->model = NULL;
	InvalidateTraceLineCache();

// allways idle in intermission
	old = v_idlescale.value;
	v_idlescale.value = 1;
	V_AddIdle ();
	v_idlescale.value = old;
}

/*
==================
V_CalcRefdef
==================
*/
void V_CalcRefdef (void)
{
	entity_t	*ent, *view;
	int			i;
	vec3_t		forward, right, up;
	vec3_t		angles;
	float		bob, height_adjustment;
	static float oldz = 0;
	static vec3_t punch = {0,0,0}; //johnfitz -- v_gunkick
	float delta; //johnfitz -- v_gunkick

	V_DriftPitch ();

// ent is the player model (visible when out of body)
	ent = &cl.entities[cl.viewentity];
// view is the weapon model (only visible from inside body)
	view = &cl.viewent;


// transform the view offset by the model's matrix to get the offset from
// model origin for the view
	ent->angles[YAW] = cl.lerpangles[YAW];	// the model should face the view dir // woods to lerp #smoothcam
	ent->angles[PITCH] = -cl.lerpangles[PITCH];	// the model should face the view dir // woods to lerp #smoothcam

	bob = V_CalcBob ();
	if (v_viewheight.value)
	{
		height_adjustment = bound(-7, v_viewheight.value, 4);
		bob = 0;
	}
	else
		height_adjustment = bob;

// refresh position
	VectorCopy (ent->origin, r_refdef.vieworg);
	r_refdef.vieworg[2] += cl.stats[STAT_VIEWHEIGHT];

// never let it sit exactly on a node line, because a water plane can
// dissapear when viewed with the eye exactly on it.
// the server protocol only specifies to 1/16 pixel, so add 1/32 in each axis
	r_refdef.vieworg[0] += 1.0/32;
	r_refdef.vieworg[1] += 1.0/32;
	r_refdef.vieworg[2] += 1.0/32;

	VectorCopy (cl.lerpangles, r_refdef.viewangles); // woods to lerp #smoothcam
	V_CalcViewRoll ();
	V_AddIdle ();

	if (cl.stats[STAT_HEALTH] > 0)
	{
		// v_viewheight/bob only affects the view if the player is alive
		r_refdef.vieworg[2] += height_adjustment;
	}

// offsets
	angles[PITCH] = -ent->angles[PITCH]; // because entity pitches are actually backward
	angles[YAW] = ent->angles[YAW];
	angles[ROLL] = ent->angles[ROLL];

	AngleVectors (angles, forward, right, up);

	if (cl.maxclients <= 1) //johnfitz -- moved cheat-protection here from V_RenderView
		for (i=0 ; i<3 ; i++)
			r_refdef.vieworg[i] += scr_ofsx.value*forward[i] + scr_ofsy.value*right[i] + scr_ofsz.value*up[i];

	if (ent->model && ent->model->mins[2] >= -10 && ent->model->maxs[2] >= 24+32)
	{	//hack this hack...
		ent->origin[2] += 24;
		V_BoundOffsets ();
		ent->origin[2] -= 24;
	}
	else
		V_BoundOffsets ();

// set up gun stuff

	VectorCopy (cl.lerpangles, view->angles); // woods to lerp #smoothcam

	CalcGunAngle ();

	view->eflags = EFLAGS_VIEWMODEL;
	VectorScale(forward, 1.0/32, view->origin);	//bias it very slightly sideways (so it shifts slightly when turning to mimic the 1/32 bias that used to affect it before we changed how viewmodels work)
	view->origin[0] = bob*0.4;	//and bob it forwards

	if (cl.items & IT_INVISIBILITY) // woods #ringalpha
	{
		if (r_drawviewmodel.value == 1) 
			view->alpha = ENTALPHA_ENCODE(0.50);
		else
			view->alpha = ENTALPHA_ZERO;
	}
	else
		view->alpha = ENTALPHA_ENCODE(r_drawviewmodel.value);

	//johnfitz -- removed all gun position fudging code (was used to keep gun from getting covered by sbar)
	//MarkV -- restored this with r_viewmodel_quake cvar
	if (r_viewmodel_quake.value)
	{
		if (scr_viewsize.value == 110)
			view->origin[2] += 1;
		else if (scr_viewsize.value == 100)
			view->origin[2] += 2;
		else if (scr_viewsize.value == 90)
			view->origin[2] += 1;
		else if (scr_viewsize.value == 80)
			view->origin[2] += 0.5;
	}
	if (ent->lerpflags & LERP_FINISH)
	{
		view->lerpflags |= LERP_FINISH;
		view->lerpfinish = ent->lerpfinish;
	}
	else
		view->lerpflags &= ~LERP_FINISH;

	V_CalcGunDrift (view->origin, view->angles); // woods #gdrift

	view->model = CL_CTFPugTranslateModel (cl.model_precache[cl.stats[STAT_WEAPON]]);
	view->frame = cl.stats[STAT_WEAPONFRAME];
	view->netstate = nullentitystate;

//johnfitz -- v_gunkick
	if (v_gunkick.value == 1) //original quake kick
		VectorAdd (r_refdef.viewangles, cl.punchangle, r_refdef.viewangles);
	if (v_gunkick.value == 2) //lerped kick
	{
		for (i=0; i<3; i++)
			if (punch[i] != v_punchangles[0][i])
			{
				double interval = v_punchangles_times[0] - v_punchangles_times[1];
				if (interval > 0.1) interval = 0.1;

				//speed determined by how far we need to lerp in 1/10th of a second
				delta = (v_punchangles[0][i]-v_punchangles[1][i]) * host_frametime / interval;

				if (delta > 0)
					punch[i] = q_min(punch[i]+delta, v_punchangles[0][i]);
				else if (delta < 0)
					punch[i] = q_max(punch[i]+delta, v_punchangles[0][i]);
			}

		VectorAdd (r_refdef.viewangles, punch, r_refdef.viewangles);
	}
//johnfitz

// smooth out stair step ups / fast elevator rises
// joequake-style adaptive rate: small delta (stairs) uses the classic 80 u/s;
// large upward moves (>20 units) boost to 160 u/s with extra offset window.
	{
		static vec3_t oldorigin = {0,0,0};
		static float extracrouch = 0;
		static float crouchspeed = 80;
		float steptime;
		vec3_t odiff;

		steptime = cl.time - cl.oldtime;
		if (steptime < 0)
			//FIXME	I_Error ("steptime < 0");
			steptime = 0;

		// teleport / respawn detection: reset smoothing state
		VectorSubtract (ent->origin, oldorigin, odiff);
		if (DotProduct (odiff, odiff) > 48 * 48)
		{
			oldz = ent->origin[2];
			extracrouch = 0;
			crouchspeed = 80;
		}
		VectorCopy (ent->origin, oldorigin);

		if (!noclip_anglehack && cl.onground && ent->origin[2] - oldz > 0) //johnfitz -- added exception for noclip
		//FIXME: noclip_anglehack is set on the server, so in a nonlocal game this won't work.
		{
			if (ent->origin[2] - oldz > 20)
			{
				if (crouchspeed < 160)
				{
					extracrouch = ent->origin[2] - oldz - steptime * 200 - 15;
					extracrouch = q_min (extracrouch, 5);
				}
				crouchspeed = 160;
			}

			oldz += steptime * crouchspeed;
			if (oldz > ent->origin[2])
				oldz = ent->origin[2];
			if (ent->origin[2] - oldz > 12 + extracrouch)
				oldz = ent->origin[2] - 12 - extracrouch;

			extracrouch -= steptime * 200;
			if (extracrouch < 0)
				extracrouch = 0;

			r_refdef.vieworg[2] += oldz - ent->origin[2];
		}
		else
		{
			oldz = ent->origin[2];
			crouchspeed = 80;
			extracrouch = 0;
		}
	}

	// woods #demoeyecam - convert chasecam demo view into eyecam view
	if (cls.demoplayback && cl_demo_eyecam.value && cl.entities && cl.maxclients > 0)
	{
		int target = V_FindDemoEyecamTarget ();

		if (V_ShouldApplyDemoEyecam (target))
		{
			entity_t *targetent = &cl.entities[target];

			if (targetent->model)
			{
				cl.demo_eyecam_target = target;

				// Override camera to target player POV.
				VectorCopy (targetent->origin, r_refdef.vieworg);
				r_refdef.vieworg[2] += DEFAULT_VIEWHEIGHT + bound(-7, v_viewheight.value, 4);
				r_refdef.vieworg[0] += 1.0/32;
				r_refdef.vieworg[1] += 1.0/32;
				r_refdef.vieworg[2] += 1.0/32;

				// Entity pitch is inverted; remove roll in eyecam mode.
				r_refdef.viewangles[PITCH] = -targetent->angles[PITCH];
				r_refdef.viewangles[YAW] = targetent->angles[YAW];
				r_refdef.viewangles[ROLL] = 0;
			}
			else
			{
				cl.demo_eyecam_target = 0;
			}
		}
		else
		{
			cl.demo_eyecam_target = 0;
		}
	}
	else
	{
		cl.demo_eyecam_target = 0;
	}

	if (chase_active.value)
		Chase_UpdateForDrawing (); //johnfitz
}

/*
==================
V_RestoreAngles

Resets the viewentity angles to the last values received from the server
(undoing the manual adjustments performed by V_CalcRefdef)
==================
*/
void V_RestoreAngles (void)
{
	entity_t *ent = &cl.entities[cl.viewentity];
	VectorCopy (ent->msg_angles[0], ent->angles);
}

/*
==================
V_RenderView

The player's clipping box goes from (-16 -16 -24) to (16 16 32) from
the entity origin, so any view position inside that will be valid
==================
*/
extern vrect_t	scr_vrect;

void V_RenderView (void)
{
	if (con_forcedup)
		return;

	if (cl.intermission)
		V_CalcIntermissionRefdef ();
	else if (!cl.paused /* && (cl.maxclients > 1 || key_dest == key_game) */)
		V_CalcRefdef ();

	//johnfitz -- removed lcd code

	R_RenderView ();

	SCR_SetupAutoID(); // woods #autoid

	SCR_DrawAutoID(); // woods #autoid

	SCR_DrawStatusIndicators(); // woods #autoid

	V_PolyBlend (); //johnfitz -- moved here from R_Renderview ();
}

/*
==============================================================================

	INIT

==============================================================================
*/

/*
=============
V_Init
=============
*/
void V_Init (void)
{
	Cmd_AddCommand ("v_cshift", V_cshift_f);
	Cmd_AddCommand ("bf", V_BonusFlash_f);
	Cmd_AddCommand ("centerview", V_StartPitchDrift);

	Cvar_RegisterVariable (&v_centermove);
	Cvar_RegisterVariable (&v_centerspeed);

	Cvar_RegisterVariable (&v_iyaw_cycle);
	Cvar_RegisterVariable (&v_iroll_cycle);
	Cvar_RegisterVariable (&v_ipitch_cycle);
	Cvar_RegisterVariable (&v_iyaw_level);
	Cvar_RegisterVariable (&v_iroll_level);
	Cvar_RegisterVariable (&v_ipitch_level);

	Cvar_RegisterVariable (&v_viewheight);
	Cvar_SetCompletion (&v_viewheight, &V_Viewheight_Completion_f);
	Cvar_RegisterVariable (&v_idlescale);
	Cvar_RegisterVariable (&crosshair);
	Cvar_RegisterVariable (&gl_cshiftpercent);
	Cvar_RegisterVariable (&gl_cshiftpercent_contents); // QuakeSpasm
	Cvar_RegisterVariable (&gl_cshiftpercent_damage); // QuakeSpasm
	Cvar_RegisterVariable (&gl_cshiftpercent_bonus); // QuakeSpasm
	Cvar_RegisterVariable (&gl_cshiftpercent_powerup); // QuakeSpasm
	Cvar_RegisterVariable (&gl_cshiftpercent_dead); // woods #cdead

	Cvar_RegisterVariable (&scr_ofsx);
	Cvar_RegisterVariable (&scr_ofsy);
	Cvar_RegisterVariable (&scr_ofsz);
	Cvar_RegisterVariable (&cl_rollspeed);
	Cvar_RegisterVariable (&cl_rollangle);
	Cvar_RegisterVariable (&cl_screenangle); // woods #screenangle
	Cvar_SetCallback (&cl_screenangle, OnClScreenAngleChanged_f); // woods #screenangle
	Cvar_RegisterVariable (&cl_screenangle_speed); // woods #screenangle
	Cvar_RegisterVariable (&cl_bob);
	Cvar_RegisterVariable (&cl_bobcycle);
	Cvar_RegisterVariable (&cl_bobup);

	Cvar_RegisterVariable (&v_kicktime);
	Cvar_RegisterVariable (&v_kickroll);
	Cvar_RegisterVariable (&v_kickpitch);
	Cvar_RegisterVariable (&v_gunkick); //johnfitz
	Cvar_RegisterVariable (&cl_gun_drift); // woods #gdrift

	Cvar_RegisterVariable (&r_viewmodel_quake); //MarkV
	Cvar_RegisterVariable (&cl_demo_eyecam); // woods #demoeyecam
}
