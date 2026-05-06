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
// cl_tent.c -- client side temporary entities

#include "quakedef.h"

qboolean Q1BSP_RecursiveHullCheck (hull_t* hull, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t* trace); // woods added for truelightning #truelight

int			num_temp_entities;
entity_t	cl_temp_entities[MAX_TEMP_ENTITIES];
beam_t		cl_beams[MAX_BEAMS];

static	vec3_t	playerbeam_end; // woods #truelight
vec3_t	NULLVEC = { 0,0,0 }; // woods for #truelight
void vectoangles(vec3_t vec, vec3_t ang); // woods for #truelight
extern cvar_t v_viewheight;

qboolean CL_BeamTrailIsLightning(const char *trailname) // woods #beamspoly
{
	if (!trailname)
		return false;

	return (!strcmp(trailname, "TE_LIGHTNING1") ||
		!strcmp(trailname, "TE_LIGHTNING2") ||
		!strcmp(trailname, "TE_LIGHTNING3") ||
		!strcmp(trailname, "TE_LIGHTNING4"));
}

void CL_Beam_CalculatePositions(const beam_t *b, vec3_t start, vec3_t end) // woods #beamspoly
{
	if (!b)
	{
		start[0] = start[1] = start[2] = 0;
		end[0] = end[1] = end[2] = 0;
		return;
	}

	VectorCopy(b->start, start);
	VectorCopy(b->end, end);
}

static sfx_t			*cl_sfx_wizhit;
static sfx_t			*cl_sfx_knighthit;
static sfx_t			*cl_sfx_tink1;
static sfx_t			*cl_sfx_ric1;
static sfx_t			*cl_sfx_ric2;
static sfx_t			*cl_sfx_ric3;
static sfx_t			*cl_sfx_r_exp3;

static sfx_t *CL_TEntSound (sfx_t **cached_sfx, const char *name)
{
	/*
	 * S_ClearPrecache() wipes and reuses known_sfx slots on map/mod changes.
	 * Refresh these long-lived temp-entity pointers if their slot was cleared
	 * or recycled for some other sound.
	 */
	if (!*cached_sfx || strcmp((*cached_sfx)->name, name))
		*cached_sfx = S_PrecacheSound (name);

	return *cached_sfx;
}

/*
=================
CL_ParseTEnt
=================
*/
void CL_InitTEnts (void)
{
	cl_sfx_wizhit = CL_TEntSound (&cl_sfx_wizhit, "wizard/hit.wav");
	cl_sfx_knighthit = CL_TEntSound (&cl_sfx_knighthit, "hknight/hit.wav");
	cl_sfx_tink1 = CL_TEntSound (&cl_sfx_tink1, "weapons/tink1.wav");
	cl_sfx_ric1 = CL_TEntSound (&cl_sfx_ric1, "weapons/ric1.wav");
	cl_sfx_ric2 = CL_TEntSound (&cl_sfx_ric2, "weapons/ric2.wav");
	cl_sfx_ric3 = CL_TEntSound (&cl_sfx_ric3, "weapons/ric3.wav");
	cl_sfx_r_exp3 = CL_TEntSound (&cl_sfx_r_exp3, "weapons/r_exp3.wav");
}

/*
=================
CL_ParseBeam
=================
*/
void CL_UpdateBeam (qmodel_t *m, const char *trailname, const char *impactname, int ent, float *start, float *end)
{
	beam_t	*b;
	int		i;
	qboolean	isLightning = CL_BeamTrailIsLightning(trailname); // woods #beamspoly

#ifdef PSET_SCRIPT
	{
		vec3_t normal, extra, impact;
		VectorSubtract(end, start, normal);
		VectorNormalize(normal);
		VectorMA(end, 4, normal, extra);	//extend the end-point by four
		if (CL_TraceLine(start, extra, impact, normal, NULL)<1)
			PScript_RunParticleEffectTypeString(impact, normal, 1, impactname);
	}
#endif

	if (ent == cl.viewentity)						// woods #truelight
		VectorCopy (end, playerbeam_end);	// for cl_truelightning  #truelight

// override any beam with the same entity
	for (i=0, b=cl_beams ; i< MAX_BEAMS ; i++, b++)
		if (b->entity == ent)
		{
			b->entity = ent;
			b->model = m;
			b->starttime = cl.time - 0.001; // woods (iw) #democontrols
			b->trailname = trailname;
			b->lightning = isLightning; // woods #beamspoly
			b->endtime = cl.time + 0.2;
			VectorCopy (start, b->start);
			VectorCopy (end, b->end);
			return;
		}

// find a free beam
	for (i=0, b=cl_beams ; i< MAX_BEAMS ; i++, b++)
	{
		if (!b->model || (b->starttime > cl.time && cls.demoplayback) || b->endtime < cl.time) // woods (iw) #democontrols
		{
			b->entity = ent;
			b->model = m;
			b->starttime = cl.time - 0.001; // woods (iw) #democontrols
			b->trailname = trailname;
			b->lightning = isLightning; // woods #beamspoly
			b->endtime = cl.time + 0.2;
			VectorCopy (start, b->start);
			VectorCopy (end, b->end);
			return;
		}
	}

	//johnfitz -- less spammy overflow message
	if (!dev_overflows.beams || dev_overflows.beams + CONSOLE_RESPAM_TIME < realtime )
	{
		Con_Printf ("Beam list overflow!\n");
		dev_overflows.beams = realtime;
	}
	//johnfitz
}

static void CL_ParseBeam (qmodel_t *m, const char *trailname, const char *impactname)
{
	int		ent;
	vec3_t	start, end;

	ent = MSG_ReadEntity (cl.protocol_pext2);

	start[0] = MSG_ReadCoord (cl.protocolflags);
	start[1] = MSG_ReadCoord (cl.protocolflags);
	start[2] = MSG_ReadCoord (cl.protocolflags);

	end[0] = MSG_ReadCoord (cl.protocolflags);
	end[1] = MSG_ReadCoord (cl.protocolflags);
	end[2] = MSG_ReadCoord (cl.protocolflags);

	CL_UpdateBeam (m, trailname, impactname, ent, start, end);
}

void CL_SpawnSpriteEffect(vec3_t org/*, vec3_t dir, vec3_t orientationup*/, qmodel_t *model, int startframe, int framecount, float framerate/*, float alpha, float scale, float randspin, float gravity, int traileffect, unsigned int renderflags, int skinnum*/)
{
	if (startframe < 0)
		startframe = framecount = 0;
	if (!framecount)
		framecount = model->numframes;
	Con_DPrintf("CL_SpawnSpriteEffect: not implemented\n");
}

/*
=================
CL_ParseTEnt
=================
*/
void CL_ParseTEnt (void)
{
	int		type;
	vec3_t	pos;
	dlight_t	*dl;
	int		rnd;
	int		colorStart, colorLength;

	if (cl.qcvm.extfuncs.CSQC_Parse_TempEntity && !cl.qcvm.nogameaccess)
	{	//this is likely to be misused, but I'm implementing it anyway because DP doesn't allow anything better and consistency is generally easier than simplicity. You should generally be using CSQC_Parse_Event instead, for custom messages.
		//FTE *REQUIRES* use of multicast for custom messages, otherwise its serverside protocol translation logic cannot guage sizes properly.
		qboolean ret;
		int start = msg_readcount;

		PR_SwitchQCVM(&cl.qcvm);
		PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_Parse_TempEntity);
		ret = G_FLOAT(OFS_RETURN);
		PR_SwitchQCVM(NULL);

		if (ret)
			return;	//it handled it okay.
		//rewind, so we can actually read the 'type' byte that the qc will no doubt have already read...
		msg_readcount = start;
	}

	type = MSG_ReadByte ();
	switch (type)
	{
	case TE_WIZSPIKE:			// spike hitting wall
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString(pos, NULL, 1, "TE_WIZSPIKE"))
			R_RunParticleEffect (pos, vec3_origin, 20, 30);
		S_StartSound (-1, 0, CL_TEntSound (&cl_sfx_wizhit, "wizard/hit.wav"), pos, 1, 1);
		break;

	case TE_KNIGHTSPIKE:			// spike hitting wall
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString(pos, NULL, 1, "TE_KNIGHTSPIKE"))
			R_RunParticleEffect (pos, vec3_origin, 226, 20);
		S_StartSound (-1, 0, CL_TEntSound (&cl_sfx_knighthit, "hknight/hit.wav"), pos, 1, 1);
		break;

	case TEDP_SPIKEQUAD:
	case TE_SPIKE:			// spike hitting wall
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString(pos, NULL, 1, (type==TEDP_SPIKEQUAD)?"TE_SPIKEQUAD":"TE_SPIKE"))
			R_RunParticleEffect (pos, vec3_origin, 0, 10);
		if ( rand() % 5 )
			S_StartSound (-1, 0, CL_TEntSound (&cl_sfx_tink1, "weapons/tink1.wav"), pos, 1, 1);
		else
		{
			rnd = rand() & 3;
			if (rnd == 1)
				S_StartSound (-1, 0, CL_TEntSound (&cl_sfx_ric1, "weapons/ric1.wav"), pos, 1, 1);
			else if (rnd == 2)
				S_StartSound (-1, 0, CL_TEntSound (&cl_sfx_ric2, "weapons/ric2.wav"), pos, 1, 1);
			else
				S_StartSound (-1, 0, CL_TEntSound (&cl_sfx_ric3, "weapons/ric3.wav"), pos, 1, 1);
		}
		break;
	case TEDP_SUPERSPIKEQUAD:
	case TE_SUPERSPIKE:			// super spike hitting wall
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString(pos, NULL, 1, (type==TEDP_SUPERSPIKEQUAD)?"TE_SUPERSPIKEQUAD":"TE_SUPERSPIKE"))
			R_RunParticleEffect (pos, vec3_origin, 0, 20);

		if ( rand() % 5 )
			S_StartSound (-1, 0, CL_TEntSound (&cl_sfx_tink1, "weapons/tink1.wav"), pos, 1, 1);
		else
		{
			rnd = rand() & 3;
			if (rnd == 1)
				S_StartSound (-1, 0, CL_TEntSound (&cl_sfx_ric1, "weapons/ric1.wav"), pos, 1, 1);
			else if (rnd == 2)
				S_StartSound (-1, 0, CL_TEntSound (&cl_sfx_ric2, "weapons/ric2.wav"), pos, 1, 1);
			else
				S_StartSound (-1, 0, CL_TEntSound (&cl_sfx_ric3, "weapons/ric3.wav"), pos, 1, 1);
		}
		break;

	case TEDP_GUNSHOTQUAD:
	case TEFTE_GUNSHOT_COUNT:	//for compat with qw mods
	case TE_GUNSHOT:			// bullet hitting wall
		rnd = 20;
		if (type == TEFTE_GUNSHOT_COUNT)
			rnd *= MSG_ReadByte();
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString(pos, NULL, rnd, (type==TEDP_GUNSHOTQUAD)?"TE_GUNSHOTQUAD":"TE_GUNSHOT"))
			R_RunParticleEffect (pos, vec3_origin, 0, rnd);
		break;

	case TEFTE_EXPLOSION_SPRITE://for compat with qw mods
	case TEDP_EXPLOSIONQUAD:
	case TENEH_EXPLOSION3:
	case TE_EXPLOSION:			// rocket explosion
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString(pos, NULL, 1, (type==TEDP_EXPLOSIONQUAD)?"TE_EXPLOSIONQUAD":"TE_EXPLOSION"))
			R_ParticleExplosion (pos);
		if (r_explosionlight.value) // woods #explosionlight
		{
			dl = CL_AllocDlight (0);
			VectorCopy (pos, dl->origin);
			dl->radius = 150 + 200 * bound(0, r_explosionlight.value, 1);
			dl->die = cl.time + 0.5;
			dl->decay = 300;
			if (type == TENEH_EXPLOSION3)
			{	//the *2 is to match dp's expectations, for some reason.
				dl->color[0] = MSG_ReadCoord(cl.protocolflags)*2.0;
				dl->color[1] = MSG_ReadCoord(cl.protocolflags)*2.0;
				dl->color[2] = MSG_ReadCoord(cl.protocolflags)*2.0;
			}
		}
		S_StartSound (-1, 0, CL_TEntSound (&cl_sfx_r_exp3, "weapons/r_exp3.wav"), pos, 1, 1);

		if (type==TEFTE_EXPLOSION_SPRITE)
		{
			qmodel_t *mod = Mod_ForName ("progs/s_explod.spr", false);
			CL_SpawnSpriteEffect(pos/*, NULL, NULL*/, mod, 0, 0, 10/*, mod->type==mod_sprite?-1:1, 1, 0, 0, P_INVALID, 0, 0*/);
		}
		break;

	case TE_TAREXPLOSION:			// tarbaby explosion
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString(pos, NULL, 1, "TE_TAREXPLOSION"))
			R_BlobExplosion (pos);

		S_StartSound (-1, 0, CL_TEntSound (&cl_sfx_r_exp3, "weapons/r_exp3.wav"), pos, 1, 1);
		break;

	case TE_LIGHTNING1:				// lightning bolts
		CL_ParseBeam (Mod_ForName("progs/bolt.mdl", true), "TE_LIGHTNING1", "TE_LIGHTNING1_END");
		break;

	case TE_LIGHTNING2:				// lightning bolts
		CL_ParseBeam (Mod_ForName("progs/bolt2.mdl", true), "TE_LIGHTNING2", "TE_LIGHTNING2_END");
		break;

	case TE_LIGHTNING3:				// lightning bolts
		CL_ParseBeam (Mod_ForName("progs/bolt3.mdl", true), "TE_LIGHTNING3", "TE_LIGHTNING3_END");
		break;

// PGM 01/21/97
	case TE_BEAM:				// grappling hook beam
		CL_ParseBeam (Mod_ForName("progs/beam.mdl", true), "TE_BEAM", "TE_BEAM_END");
		break;
// PGM 01/21/97

	case TE_LAVASPLASH:
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString(pos, NULL, 1, "TE_LAVASPLASH"))
			R_LavaSplash (pos);
		break;

	case TE_TELEPORT:
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString(pos, NULL, 1, "TE_TELEPORT"))
			R_TeleportSplash (pos);
		break;

	case TE_EXPLOSION2:				// color mapped explosion
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		colorStart = MSG_ReadByte ();
		colorLength = MSG_ReadByte ();
		if (PScript_RunParticleEffectTypeString(pos, NULL, 1, va("TE_EXPLOSION2_%i_%i", colorStart, colorLength)))
			R_ParticleExplosion2 (pos, colorStart, colorLength);
		dl = CL_AllocDlight (0);
		VectorCopy (pos, dl->origin);
		dl->radius = 350;
		dl->die = cl.time + 0.5;
		dl->decay = 300;
		S_StartSound (-1, 0, CL_TEntSound (&cl_sfx_r_exp3, "weapons/r_exp3.wav"), pos, 1, 1);
		break;

	case TENEH_LIGHTNING4:
		{
			const char *beamname = MSG_ReadString();
			CL_ParseBeam (Mod_ForName(beamname, true), "TE_LIGHTNING4", "TE_LIGHTNING4_END");
		}
		break;

	case TEDP_CUSTOMFLASH:
		dl = CL_AllocDlight (0);
		dl->origin[0] = MSG_ReadCoord(cl.protocolflags);
		dl->origin[1] = MSG_ReadCoord(cl.protocolflags);
		dl->origin[2] = MSG_ReadCoord(cl.protocolflags);
		dl->radius = 8*MSG_ReadByte();
		dl->die = (MSG_ReadByte()+1)*(1/256.0);
		dl->decay = dl->radius / dl->die;
		dl->die += cl.time;
		dl->color[0] = MSG_ReadByte()*(1/127.0);
		dl->color[1] = MSG_ReadByte()*(1/127.0);
		dl->color[2] = MSG_ReadByte()*(1/127.0);
		break;

	case TEDP_PARTICLERAIN:
	case TEDP_PARTICLESNOW:
		{
			vec3_t dir, pos2;
			int cnt, colour;

			//min
			pos[0] = MSG_ReadCoord(cl.protocolflags);
			pos[1] = MSG_ReadCoord(cl.protocolflags);
			pos[2] = MSG_ReadCoord(cl.protocolflags);

			//max
			pos2[0] = MSG_ReadCoord(cl.protocolflags);
			pos2[1] = MSG_ReadCoord(cl.protocolflags);
			pos2[2] = MSG_ReadCoord(cl.protocolflags);

			//dir
			dir[0] = MSG_ReadCoord(cl.protocolflags);
			dir[1] = MSG_ReadCoord(cl.protocolflags);
			dir[2] = MSG_ReadCoord(cl.protocolflags);

			cnt = (unsigned short)MSG_ReadShort();	//count
			colour = MSG_ReadByte ();	//colour

			PScript_RunParticleWeather(pos, pos2, dir, cnt, colour, ((type==TEDP_PARTICLESNOW)?"snow":"rain"));
		}
		break;

	case TEDP_BLOOD:
		{
			vec3_t dir;
			int cnt;
			pos[0] = MSG_ReadCoord(cl.protocolflags);
			pos[1] = MSG_ReadCoord(cl.protocolflags);
			pos[2] = MSG_ReadCoord(cl.protocolflags);
			dir[0] = MSG_ReadChar();
			dir[1] = MSG_ReadChar();
			dir[2] = MSG_ReadChar();
			cnt = MSG_ReadByte();
			if (PScript_RunParticleEffectTypeString(pos, dir, cnt, "TE_BLOOD"))
				Con_Printf ("CL_ParseTEnt: TEDP_BLOOD unavailable\n");
		}
		break;

	case TEDP_SPARK:
		{
			vec3_t dir;
			int cnt;
			pos[0] = MSG_ReadCoord(cl.protocolflags);
			pos[1] = MSG_ReadCoord(cl.protocolflags);
			pos[2] = MSG_ReadCoord(cl.protocolflags);
			dir[0] = MSG_ReadChar();
			dir[1] = MSG_ReadChar();
			dir[2] = MSG_ReadChar();
			cnt = MSG_ReadByte();
			if (PScript_RunParticleEffectTypeString(pos, dir, cnt, "TE_SPARK"))
				Con_Printf ("CL_ParseTEnt: TEDP_SPARK unavailable\n");
		}
		break;
	case TEDP_SMALLFLASH: // [vector] origin
		pos[0] = MSG_ReadCoord(cl.protocolflags);
		pos[1] = MSG_ReadCoord(cl.protocolflags);
		pos[2] = MSG_ReadCoord(cl.protocolflags);
		if (PScript_RunParticleEffectTypeString(pos, NULL, 1, "TE_SMALLFLASH"))
			Con_Printf ("CL_ParseTEnt: TEDP_SMALLFLASH unavailable\n");
		break;

	//spike: these are all kinda useless once the ssqc has access to pointparticles...
	//I'm too lazy to bother implementing them properly.
	case TEDP_BLOODSHOWER:
		/*mins[0] =*/ MSG_ReadCoord(cl.protocolflags);
		/*mins[1] =*/ MSG_ReadCoord(cl.protocolflags);
		/*mins[2] =*/ MSG_ReadCoord(cl.protocolflags);
		/*maxs[0] =*/ MSG_ReadCoord(cl.protocolflags);
		/*maxs[1] =*/ MSG_ReadCoord(cl.protocolflags);
		/*maxs[2] =*/ MSG_ReadCoord(cl.protocolflags);
		/*velspeed =*/ MSG_ReadCoord(cl.protocolflags);
		/*count =*/ MSG_ReadShort();
		Con_Printf ("CL_ParseTEnt: TEDP_BLOODSHOWER unsupported\n");
		break;
	case TEDP_EXPLOSIONRGB:
		/*pos[0] =*/ MSG_ReadCoord(cl.protocolflags);
		/*pos[1] =*/ MSG_ReadCoord(cl.protocolflags);
		/*pos[2] =*/ MSG_ReadCoord(cl.protocolflags);
		/*col[0] =*/ MSG_ReadByte();
		/*col[1] =*/ MSG_ReadByte();
		/*col[2] =*/ MSG_ReadByte();
		Con_Printf ("CL_ParseTEnt: TEDP_EXPLOSIONRGB unsupported\n");
		break;
	case TEDP_PARTICLECUBE:
		/*mins[0] =*/ MSG_ReadCoord(cl.protocolflags);
		/*mins[1] =*/ MSG_ReadCoord(cl.protocolflags);
		/*mins[2] =*/ MSG_ReadCoord(cl.protocolflags);
		/*maxs[0] =*/ MSG_ReadCoord(cl.protocolflags);
		/*maxs[1] =*/ MSG_ReadCoord(cl.protocolflags);
		/*maxs[2] =*/ MSG_ReadCoord(cl.protocolflags);
		/*dir[0] =*/ MSG_ReadCoord(cl.protocolflags);
		/*dir[1] =*/ MSG_ReadCoord(cl.protocolflags);
		/*dir[2] =*/ MSG_ReadCoord(cl.protocolflags);
		/*count =*/ MSG_ReadShort();
		/*pal_start =*/ MSG_ReadByte();
		/*pal_rand =*/ MSG_ReadByte();
		/*velspeed =*/ MSG_ReadCoord(cl.protocolflags);
		Con_Printf ("CL_ParseTEnt: TEDP_PARTICLECUBE unsupported\n");
		break;
	case TEDP_FLAMEJET:
		{
			vec3_t dir;
			int cnt;
			pos[0] = MSG_ReadCoord(cl.protocolflags);
			pos[1] = MSG_ReadCoord(cl.protocolflags);
			pos[2] = MSG_ReadCoord(cl.protocolflags);
			dir[0] = MSG_ReadCoord(cl.protocolflags);
			dir[1] = MSG_ReadCoord(cl.protocolflags);
			dir[2] = MSG_ReadCoord(cl.protocolflags);
			cnt = MSG_ReadByte();
			if (PScript_RunParticleEffectTypeString(pos, dir, cnt, "TE_FLAMEJET"))
				Con_Printf ("CL_ParseTEnt: TEDP_FLAMEJET unavailable\n");
		}
		break;
	case TEDP_PLASMABURN:
		pos[0] = MSG_ReadCoord(cl.protocolflags);
		pos[1] = MSG_ReadCoord(cl.protocolflags);
		pos[2] = MSG_ReadCoord(cl.protocolflags);
		if (PScript_RunParticleEffectTypeString(pos, NULL, 1, "TE_PLASMABURN"))
			Con_Printf ("CL_ParseTEnt: TEDP_PLASMABURN unavailable\n");
		break;
	case TEDP_TEI_G3:
		Host_Error ("CL_ParseTEnt: TEDP_TEI_G3 unsupported");
	case TEDP_SMOKE:
		Host_Error ("CL_ParseTEnt: TEDP_SMOKE unsupported");
	case TEDP_TEI_BIGEXPLOSION:
		Host_Error ("CL_ParseTEnt: TEDP_TEI_BIGEXPLOSION unsupported");
	case TEDP_TEI_PLASMAHIT:
		Host_Error ("CL_ParseTEnt: TEDP_TEI_PLASMAHIT unsupported");
	default:
		Host_Error ("CL_ParseTEnt: unsupported tempentity type %i", type);
	}
}

void CL_ParseEffect (qboolean big)
{
	vec3_t org;
	int modelindex;
	int startframe;
	int framecount;
	int framerate;
	qmodel_t *mod;

	org[0] = MSG_ReadCoord(cl.protocolflags);
	org[1] = MSG_ReadCoord(cl.protocolflags);
	org[2] = MSG_ReadCoord(cl.protocolflags);

	if (big)
		modelindex = MSG_ReadShort();
	else
		modelindex = MSG_ReadByte();

	if (big)
		startframe = MSG_ReadShort();
	else
		startframe = MSG_ReadByte();

	framecount = MSG_ReadByte();
	framerate = MSG_ReadByte();

	mod = cl.model_precache[modelindex];
	CL_SpawnSpriteEffect(org/*, NULL, NULL*/, mod, startframe, framecount, framerate/*, mod->type==mod_sprite?-1:1, 1, 0, 0, P_INVALID, 0, 0*/);
}

/*
=================
CL_NewTempEntity
=================
*/
entity_t *CL_NewTempEntity (void)
{
	entity_t	*ent;

	if (cl_numvisedicts == cl_maxvisedicts)
		return NULL;
	if (num_temp_entities == MAX_TEMP_ENTITIES)
		return NULL;
	ent = &cl_temp_entities[num_temp_entities];
	memset (ent, 0, sizeof(*ent));
	num_temp_entities++;
	cl_visedicts[cl_numvisedicts] = ent;
	cl_numvisedicts++;

	ent->netstate = nullentitystate;
	return ent;
}


/*
=================
CL_UpdateTEnts
=================
*/
void CL_UpdateTEnts (void)
{
	int			i, j; //johnfitz -- use j instead of using i twice, so we don't corrupt memory
	beam_t		*b;
	vec3_t		dist, org;
	float		d;
	entity_t	*ent;
	float		yaw, pitch;
	float		forward;

	num_temp_entities = 0;

	if (cl.paused)
		srand ((int) (cl.time * 1000)); //johnfitz -- freeze beams when paused

// update lightning
	for (i=0, b=cl_beams ; i< MAX_BEAMS ; i++, b++)
	{
		if (!b->model || (b->starttime > cl.mtime[0] && cls.demoplayback) || b->endtime < cl.time) // woods (iw) #democontrols
			continue;

	// if coming from the player, update the start position
		if (b->entity == cl.viewentity && cl.entities)
		{
			VectorCopy (cl.entities[cl.viewentity].origin, b->start);
			if (!chase_active.value)
				b->start[2] += bound(-7, v_viewheight.value, 4);

			// begin woods for truelightning #truelight

			if ((cl_truelightning.value) && (!cls.demoplayback))  // remove not demo recording
			{
				vec3_t	forward, v, org, ang;
				float	f, delta;
				trace_t	trace;

				f = q_max(0, q_min(1, cl_truelightning.value));

				VectorSubtract(playerbeam_end, cl.entities[cl.viewentity].origin, v);
				v[2] -= 22;		// adjust for view height

				vectoangles(v, ang);

				// lerp pitch
				ang[0] = -ang[0];
				if (ang[0] < -180)
					ang[0] += 360;
				ang[0] += (cl.viewangles[0] - ang[0]) * f;

				// lerp yaw
				delta = cl.viewangles[1] - ang[1];
				if (delta > 180)
					delta -= 360;
				if (delta < -180)
					delta += 360;
				ang[1] += delta * f;
				ang[2] = 0;

				AngleVectors(ang, forward, NULLVEC, NULLVEC);
				VectorScale(forward, 600, forward);

				VectorCopy(cl.entities[cl.viewentity].origin, org);

				org[2] += 16;

				VectorAdd(org, forward, b->end);

				memset(&trace, 0, sizeof(trace_t));

				if (!Q1BSP_RecursiveHullCheck(cl.worldmodel->hulls, 0, 0, 1, org, b->end, &trace)) // woods use q1bsp
				{
					//Con_DPrintf (1,"CL_UpdateTEnts : SV_RecursiveHullCheck hit world model\n");
					VectorCopy(trace.endpos, b->end);
				}
			}

			// end woods for truelightning
		}

		if (b->lightning) // woods #beamspoly
		{
			if (cl_beams_polygons.value > 0)
			{
				PScript_DelinkTrailstate(&b->trailstate);
			}
			else
			{
				if (!PScript_ParticleTrail(b->start, b->end, PScript_FindParticleType(b->trailname), host_frametime, b->entity, NULL, &b->trailstate))
					continue;
			}
		}
		else
		{
			if (!PScript_ParticleTrail(b->start, b->end, PScript_FindParticleType(b->trailname), host_frametime, b->entity, NULL, &b->trailstate))
				continue;
		}

	// calculate pitch and yaw
		VectorSubtract (b->end, b->start, dist);

		if (dist[1] == 0 && dist[0] == 0)
		{
			yaw = 0;
			if (dist[2] > 0)
				pitch = 90;
			else
				pitch = 270;
		}
		else
		{
			yaw = (int) (atan2(dist[1], dist[0]) * 180 / M_PI);
			if (yaw < 0)
				yaw += 360;

			forward = sqrt (dist[0]*dist[0] + dist[1]*dist[1]);
			pitch = (int) (atan2(dist[2], forward) * 180 / M_PI);
			if (pitch < 0)
				pitch += 360;
		}

		// add new entities for the lightning
		VectorCopy (b->start, org);
		d = VectorNormalize(dist);
		if (!(cl_beams_polygons.value > 0 && b->lightning)) // woods #beamspoly
		{
			while (d > 0)
			{
				ent = CL_NewTempEntity ();
				if (!ent)
					return;
				VectorCopy (org, ent->origin);
				ent->model = b->model;
				ent->angles[0] = pitch;
				ent->angles[1] = yaw;
				ent->angles[2] = rand()%360;

				//johnfitz -- use j instead of using i twice, so we don't corrupt memory
				for (j=0 ; j<3 ; j++)
					org[j] += dist[j]*30;
				d -= 30;
			}
		}
	}
}
