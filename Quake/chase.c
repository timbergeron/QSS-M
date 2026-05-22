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
cvar_t	chase_modelangles = {"chase_modelangles", "0 0 0", CVAR_ARCHIVE};

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

extern int SV_HullPointContents(hull_t* hull, int num, vec3_t p); // woods(Qrack) #betterchase

qboolean SV_RecursiveHullCheck2(hull_t* hull, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t* trace) // woods (Qrack) #betterchase
{
	mclipnode_t* node; //johnfitz -- was dclipnode_t
	mplane_t* plane;
	float		t1, t2;
	float		frac;
	int			i;
	vec3_t		mid;
	int			side;
	float		midf;

	// LordHavoc: a goto!  everyone flee in terror... :)
loc0:
	// check for empty
	if (num < 0)
	{
		if (num != CONTENTS_SOLID)
		{
			trace->allsolid = false;
			if (num == CONTENTS_EMPTY)
				trace->inopen = true;
			else
				trace->inwater = true;
		}
		else
			trace->startsolid = true;
		return true;		// empty
	}

	// LordHavoc: this can be eliminated by validating in the loader...  but Mercury told me not to bother
	if (num < hull->firstclipnode || num > hull->lastclipnode)
		Sys_Error("SV_RecursiveHullCheck: bad node number");

	// find the point distances
	node = hull->clipnodes + num;
	plane = hull->planes + node->planenum;

	if (plane->type < 3)
	{
		t1 = p1[plane->type] - plane->dist;
		t2 = p2[plane->type] - plane->dist;
	}
	else
	{
		t1 = DotProduct(plane->normal, p1) - plane->dist;
		t2 = DotProduct(plane->normal, p2) - plane->dist;
	}

	// LordHavoc: recursion optimization
	if (t1 >= 0 && t2 >= 0)
	{
		num = node->children[0];
		goto loc0;
	}
	if (t1 < 0 && t2 < 0)
	{
		num = node->children[1];
		goto loc0;
	}

	// put the crosspoint DIST_EPSILON pixels on the near side
	side = (t1 < 0);
	if (side)
		frac = bound(0, (t1 + DIST_EPSILON) / (t1 - t2), 1);
	else
		frac = bound(0, (t1 - DIST_EPSILON) / (t1 - t2), 1);

	midf = p1f + (p2f - p1f) * frac;
	for (i = 0; i < 3; i++)
		mid[i] = p1[i] + frac * (p2[i] - p1[i]);

	// move up to the node
	if (!SV_RecursiveHullCheck2(hull, node->children[side], p1f, midf, p1, mid, trace))
		return false;
	/*
#ifdef PARANOID
	if (SV_HullPointContents(pm_hullmodel, mid, node->children[side]) == CONTENTS_SOLID)
	{
		Con_Printf("mid PointInHullSolid\n");
		return false;
	}
#endif
	*/

	// LordHavoc: warning to the clumsy, this recursion can not be optimized because mid would need to be duplicated on a stack
	if (SV_HullPointContents(hull, node->children[side ^ 1], mid) != CONTENTS_SOLID)
		// go past the node
		return SV_RecursiveHullCheck2(hull, node->children[side ^ 1], midf, p2f, mid, p2, trace);

	if (trace->allsolid)
		return false;		// never got out of the solid area

//==================
// the other side of the node is solid, this is the impact point
//==================
	if (!side)
	{
		VectorCopy(plane->normal, trace->plane.normal);
		trace->plane.dist = plane->dist;
	}
	else
	{
		// LordHavoc: vec3_origin is evil; the compiler can not rely on it being '0 0 0'
		// VectorSubtract (vec3_origin, plane->normal, trace->plane.normal);
		trace->plane.normal[0] = -plane->normal[0];
		trace->plane.normal[1] = -plane->normal[1];
		trace->plane.normal[2] = -plane->normal[2];
		trace->plane.dist = -plane->dist;
	}

	while (SV_HullPointContents(hull, hull->firstclipnode, mid) == CONTENTS_SOLID)
	{ // shouldn't really happen, but does occasionally
		frac -= 0.1;
		if (frac < 0)
		{
			trace->fraction = midf;
			VectorCopy(mid, trace->endpos);
			return false;
		}
		midf = p1f + (p2f - p1f) * frac;
		for (i = 0; i < 3; i++)
			mid[i] = p1[i] + frac * (p2[i] - p1[i]);
	}

	trace->fraction = midf;
	VectorCopy(mid, trace->endpos);

	return false;
}

void TraceLine2(vec3_t start, vec3_t end, vec3_t impact) // woods (Qrack) #betterchase
{
	trace_t	trace;
	qboolean result;

	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1;

	//result is true if end is empty...
	result = SV_RecursiveHullCheck2(cl.worldmodel->hulls, 0, 0, 1, start, end, &trace);

	if (!result)//hit something
	{
		VectorCopy(trace.endpos, impact);
	}
	else
		VectorCopy(end, impact);
}

void LerpVector(const vec3_t from, const vec3_t to, float frac, vec3_t out) // woods (Qrack) #betterchase
{
	out[0] = from[0] + frac * (to[0] - from[0]);
	out[1] = from[1] + frac * (to[1] - from[1]);
	out[2] = from[2] + frac * (to[2] - from[2]);
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
Chase_UpdateForClient -- johnfitz -- orient client based on camera. called after input
==============
*/
void Chase_UpdateForClient (void)
{
	//place camera

	//assign client angles to camera

	//see where camera points

	//adjust client angles to point at the same place
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
	// TraceLine2(r_refdef.vieworg, ideal, temp); // woods (Qrack) #betterchase - change to 2

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

