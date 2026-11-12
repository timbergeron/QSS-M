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
// chase.c -- chase camera code

#include "quakedef.h"

cvar_t	chase_back = {"chase_back", "90", CVAR_ARCHIVE};
cvar_t	chase_up = {"chase_up", "30", CVAR_ARCHIVE };
cvar_t	chase_right = {"chase_right", "0", CVAR_ARCHIVE };
cvar_t	chase_active = {"chase_active", "0", CVAR_NONE};
cvar_t	chase_modelangles = {"chase_modelangles", "0 0 0", CVAR_NONE};

static qboolean Chase_IsTopDownMode (int mode)
{
	return mode == 2 || mode == 3;
}

static qboolean Chase_IsNorthLockedMode (int mode)
{
	return mode == 3;
}

qboolean Chase_GetPlayerModelAngleOffset (entity_t *e, vec3_t out)
{
	int n;

	out[PITCH] = out[YAW] = out[ROLL] = 0;

	if (!chase_active.value)
		return false;
	if (!cl.entities)
		return false;
	if (cl.viewentity <= 0 || cl.viewentity >= cl.num_entities)
		return false;
	if (e != &cl.entities[cl.viewentity])
		return false;
	if (!chase_modelangles.string || !*chase_modelangles.string)
		return false;

	n = sscanf (chase_modelangles.string, "%f %f %f", &out[PITCH], &out[YAW], &out[ROLL]);
	if (n < 1)
		return false;

	return (out[PITCH] != 0.f || out[YAW] != 0.f || out[ROLL] != 0.f);
}

static void Chase_ModelAngles_Completion_f (cvar_t *cvar, const char *partial)
{
	(void)cvar;
	Con_AddToTabListMatched ("\"0 0 0\"",   partial, "default",  NULL, "0 0 0 default");
	Con_AddToTabListMatched ("\"0 90 0\"",  partial, "left",     NULL, "0 90 0 left");
	Con_AddToTabListMatched ("\"0 -90 0\"", partial, "right",    NULL, "0 -90 0 right");
	Con_AddToTabListMatched ("\"0 180 0\"", partial, "backward", NULL, "0 180 0 backward");
	Con_AddToTabListMatched ("\"0 0 90\"",  partial, "roll +",   NULL, "0 0 90 roll +");
	Con_AddToTabListMatched ("\"0 0 -90\"", partial, "roll -",   NULL, "0 0 -90 roll -");
	Con_AddToTabListMatched ("\"90 0 0\"",  partial, "pitch +",  NULL, "90 0 0 pitch +");
	Con_AddToTabListMatched ("\"-90 0 0\"", partial, "pitch -",  NULL, "-90 0 0 pitch -");
}

static void Chase_Active_Completion_f (cvar_t *cvar, const char *partial)
{
	(void)cvar;
	Con_AddToTabList ("0", partial, "off", NULL);
	Con_AddToTabList ("1", partial, "third-person", NULL);
	Con_AddToTabList ("2", partial, "top-down", NULL);
	Con_AddToTabList ("3", partial, "north-locked top-down", NULL);
}

/*
==============
Chase_Init
==============
*/
void Chase_Init (void)
{
	Cvar_RegisterVariable (&chase_back);
	Cvar_RegisterVariable (&chase_up);
	Cvar_RegisterVariable (&chase_right);
	Cvar_RegisterVariable (&chase_active);
	Cvar_SetCompletion (&chase_active, &Chase_Active_Completion_f);
	Cvar_RegisterVariable (&chase_modelangles);
	Cvar_SetCompletion (&chase_modelangles, &Chase_ModelAngles_Completion_f);
}

/*
==============
TraceLine

TODO: impact on bmodels, monsters
==============
*/
void TraceLine (vec3_t start, vec3_t end, float pushoff, vec3_t impact)
{
	trace_t	trace;

	memset (&trace, 0, sizeof(trace));
	trace.fraction = 1;
	trace.allsolid = true;
	VectorCopy (end, trace.endpos);
	SV_RecursiveHullCheck (cl.worldmodel->hulls, start, end, &trace, CONTENTMASK_ANYSOLID);

	VectorCopy (trace.endpos, impact);

	if (pushoff && trace.fraction < 1)	//push away from the impact plane by the distance specified, so our camera's near clip plane does not intersect the wall.
	{
		vec3_t dir;
		VectorSubtract(start, end, dir);
		pushoff = pushoff / DotProduct(dir, trace.plane.normal);	//distance needs to be bigger if the trace is co-planar to the surface
		VectorMA(impact, pushoff, dir, impact);
	}
}

/*
==============
Chase_UpdateForDrawing -- johnfitz -- orient camera based on client. called before drawing

TODO: stay at least 8 units away from all walls in this leaf
==============
*/
void Chase_UpdateForDrawing (void)
{
	int		i;
	int		mode = (int)chase_active.value;
	vec3_t	forward, up, right;
	vec3_t	ideal, crosshair, temp;
	vec3_t	lookangles;
	float	alpha = 1, alphadist = 1;
	float	absdist;

	VectorCopy (cl.lerpangles, lookangles); // woods added lerpangles for #smoothcam
	if (Chase_IsTopDownMode (mode))
		lookangles[PITCH] = 90;
	if (Chase_IsNorthLockedMode (mode))
		lookangles[YAW] = 0;

	AngleVectors (lookangles, forward, right, up);

	// calc ideal camera location before checking for walls
	for (i=0 ; i<3 ; i++)
		ideal[i] = r_refdef.vieworg[i]
		- forward[i]*chase_back.value
		+ right[i]*chase_right.value;
		//+ up[i]*chase_up.value;
	ideal[2] = r_refdef.vieworg[2] + chase_up.value;

	// make sure camera is not in or behind a wall
	TraceLine(r_refdef.vieworg, ideal, NEARCLIP, ideal);

	// find the spot the player is looking at
	VectorMA (r_refdef.vieworg, 1<<20, forward, temp);
	TraceLine (r_refdef.vieworg, temp, 0, crosshair);

	if (VectorLength(temp) != 0)
	{	alphadist = VecLength2(r_refdef.vieworg, ideal); // chase_transparent from Qrack
		absdist = fabsf(chase_back.value);
		alpha = bound(0, (alphadist / absdist), 1);


		if ((alpha < 1 && alpha > 0.6) || alpha < 0.09)
			alpha = (alpha < 0.09) ? 0 : 1;

		cl.entities[cl.viewentity].alpha = ENTALPHA_ENCODE((q_min(alpha, 1)));
	}
	
	// place camera
	VectorCopy (ideal, r_refdef.vieworg);

	// calculate camera angles to look at the same spot
	VectorSubtract (crosshair, r_refdef.vieworg, temp);
	VectorAngles (temp, NULL, r_refdef.viewangles);
	if (r_refdef.viewangles[PITCH] >= 89.9 || r_refdef.viewangles[PITCH] <= -89.9)
		r_refdef.viewangles[YAW] = Chase_IsNorthLockedMode (mode) ? 0 : cl.viewangles[YAW];
}

