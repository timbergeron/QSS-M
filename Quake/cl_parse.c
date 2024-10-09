/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2016      Spike

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
// cl_parse.c  -- parse a message received from the server

#include "quakedef.h"
#include "bgmusic.h"
#include "arch_def.h" // woods for f_system
#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC) // woods for f_system
#include <sys/sysctl.h>
#include <stdio.h>
#endif

extern cvar_t scr_fov; // woods #f_config
extern cvar_t host_maxfps; // woods #f_config
extern cvar_t crosshair; // woods #f_config
extern cvar_t r_particledesc; // woods #f_config
extern cvar_t gl_picmip; // woods #f_config
extern cvar_t scr_showfps; // woods #f_config
extern cvar_t allow_download; // woods #ftehack

int VID_GetCurrentDPI(void);

extern cvar_t gl_overbright_models; // woods for f_config

int ogflagprecache = 0, swapflagprecache = 0, swapflagprecache2 = 0, swapflagprecache3 = 0; // woods #alternateflags
int grenadecache = -1, rocketcache = -1; // woods #r2g


extern int	maptime; // woods connected map time #maptime
extern char videosetg[50];	// woods #q_sysinfo (qrack)
extern char videoc[40];		// woods #q_sysinfo (qrack)
qboolean	endscoreprint = false; // woods pq_confilter+

extern Uint32 exec_dm_cfg (Uint32 interval, void* param); // woods #execdelay

const char *svc_strings[128] =
{
	"svc_bad",
	"svc_nop",
	"svc_disconnect",
	"svc_updatestat",
	"svc_version",		// [long] server version
	"svc_setview",		// [short] entity number
	"svc_sound",			// <see code>
	"svc_time",			// [float] server time
	"svc_print",			// [string] null terminated string
	"svc_stufftext",		// [string] stuffed into client's console buffer
						// the string should be \n terminated
	"svc_setangle",		// [vec3] set the view angle to this absolute value

	"svc_serverinfo",		// [long] version
						// [string] signon string
						// [string]..[0]model cache [string]...[0]sounds cache
						// [string]..[0]item cache
	"svc_lightstyle",		// [byte] [string]
	"svc_updatename",		// [byte] [string]
	"svc_updatefrags",	// [byte] [short]
	"svc_clientdata",		// <shortbits + data>
	"svc_stopsound",		// <see code>
	"svc_updatecolors",	// [byte] [byte]
	"svc_particle",		// [vec3] <variable>
	"svc_damage",			// [byte] impact [byte] blood [vec3] from

	"svc_spawnstatic",
	/*"OBSOLETE svc_spawnbinary"*/"21 svc_spawnstatic_fte",
	"svc_spawnbaseline",

	"svc_temp_entity",		// <variable>
	"svc_setpause",
	"svc_signonnum",
	"svc_centerprint",
	"svc_killedmonster",
	"svc_foundsecret",
	"svc_spawnstaticsound",
	"svc_intermission",
	"svc_finale",			// [string] music [string] text
	"svc_cdtrack",			// [byte] track [byte] looptrack
	"svc_sellscreen",
	"svc_cutscene",
//johnfitz -- new server messages
	"svc_showpic_dp",	// 35
	"svc_hidepic_dp",	// 36
	"svc_skybox_fitz", // 37					// [string] skyname
	"38/qe_botchat", // 38
	"39", // 39
	"svc_bf_fitz", // 40						// no data
	"svc_fog_fitz", // 41					// [byte] density [byte] red [byte] green [byte] blue [float] time
	"svc_spawnbaseline2_fitz", //42			// support for large modelindex, large framenum, alpha, using flags
	"svc_spawnstatic2_fitz", // 43			// support for large modelindex, large framenum, alpha, using flags
	"svc_spawnstaticsound2_fitz", //	44		// [coord3] [short] samp [byte] vol [byte] aten
	"45/qe_setviews", // 45
	"46/qe_updateping", // 46
	"47/qe_updatesocial", // 47
	"48/qe_updateplinfo", // 48
	"49/qe_rawprint", // 49
//johnfitz

//spike -- particle stuff, and padded to 128 to avoid possible crashes.
	"50 svc_downloaddata_dp/qe_servervars", // 50
	"51 svc_updatestatbyte/qe_seq", // 51
	"52 svc_effect_dp/qe_achievement", // 52
	"53 svc_effect2_dp/qe_chat", // 53
	"54 svc_precache/qe_levelcompleted", // 54	//[short] type+idx [string] name
	"55 svc_baseline2_dp/qe_backtolobby", // 55
	"56 svc_spawnstatic2_dp/qe_localsound", // 56
	"57 svc_entities_dp", // 57
	"58 svc_csqcentities", // 58
	"59 svc_spawnstaticsound2_dp", // 59
	"60 svc_trailparticles", // 60
	"61 svc_pointparticles", // 61
	"62 svc_pointparticles1", // 62
	"63 svc_particle2_fte", // 63
	"64 svc_particle3_fte", // 64
	"65 svc_particle4_fte", // 65
	"66 svc_spawnbaseline_fte", // 66
	"67 svc_customtempent_fte", // 67
	"68 svc_selectsplitscreen_fte", // 68
	"69 svc_showpic_fte", // 69
	"70 svc_hidepic_fte", // 70
	"71 svc_movepic_fte", // 71
	"72 svc_updatepic_fte", // 72
	"73", // 73
	"74", // 74
	"75", // 75
	"76 svc_csqcentities_fte", // 76
	"77", // 77
	"78 svc_updatestatstring_fte", // 78
	"79 svc_updatestatfloat_fte", // 79
	"80", // 80
	"81", // 81
	"82", // 82
	"83 svc_cgamepacket_fte", // 83
	"84 svc_voicechat_fte", // 84
	"85 svc_setangledelta_fte", // 85
	"86 svc_updateentities_fte", // 86
	"87 svc_brushedit_fte", // 87
	"88 svc_updateseats_fte", // 88
	"89", // 89
	"90", // 90
	"91", // 91
	"92", // 92
	"93", // 93
	"94", // 94
	"95", // 95
	"96", // 96
	"97", // 97
	"98", // 98
	"99", // 99
	"100", // 100
	"101", // 101
	"102", // 102
	"103", // 103
	"104", // 104
	"105", // 105
	"106", // 106
	"107", // 107
	"108", // 108
	"109", // 109
	"110", // 110
	"111", // 111
	"112", // 112
	"113", // 113
	"114", // 114
	"115", // 115
	"116", // 116
	"117", // 117
	"118", // 118
	"119", // 119
	"120", // 120
	"121", // 121
	"122", // 122
	"123", // 123
	"124", // 124
	"125", // 125
	"126", // 126
	"127", // 127
};
#define	NUM_SVC_STRINGS	(sizeof(svc_strings) / sizeof(svc_strings[0]))

qboolean warn_about_nehahra_protocol; //johnfitz

extern vec3_t	v_punchangles[2]; //johnfitz
extern double	v_punchangles_times[2]; //spike -- don't assume 10fps...

//=============================================================================

/*
===============
CL_EntityNum

This error checks and tracks the total number of entities
===============
*/
entity_t	*CL_EntityNum (int num)
{
	//johnfitz -- check minimum number too
	if (num < 0)
		Host_Error ("CL_EntityNum: %i is an invalid number",num);
	//john

	if (num >= cl.num_entities)
	{
		if (num >= cl.max_edicts) //johnfitz -- no more MAX_EDICTS
			Host_Error ("CL_EntityNum: %i is an invalid number",num);
		while (cl.num_entities<=num)
		{
			cl.entities[cl.num_entities].baseline = nullentitystate;
			cl.entities[cl.num_entities].lerpflags |= LERP_RESETMOVE|LERP_RESETANIM; //johnfitz
			cl.num_entities++;
		}
	}

	return &cl.entities[num];
}

static int MSG_ReadSize16 (sizebuf_t *sb)
{
	unsigned short ssolid = MSG_ReadShort();
	if (ssolid == ES_SOLID_BSP)
		return ssolid;
	else
	{
		int solid = (((ssolid>>7) & 0x1F8) - 32+32768)<<16;	/*up can be negative*/
		solid|= ((ssolid & 0x1F)<<3);
		solid|= ((ssolid & 0x3E0)<<6);
		return solid;
	}
}
static unsigned int CLFTE_ReadDelta(unsigned int entnum, entity_state_t *news, const entity_state_t *olds, const entity_state_t *baseline)
{
	unsigned int predbits = 0;
	unsigned int bits;
	
	bits = MSG_ReadByte();
	if (bits & UF_EXTEND1)
		bits |= MSG_ReadByte()<<8;
	if (bits & UF_EXTEND2)
		bits |= MSG_ReadByte()<<16;
	if (bits & UF_EXTEND3)
		bits |= MSG_ReadByte()<<24;

	if (cl_shownet.value >= 3)
		Con_SafePrintf("%3i:     Update %4i 0x%x\n", msg_readcount, entnum, bits);

	if (bits & UF_RESET)
	{
//		Con_Printf("%3i: Reset %i @ %i\n", msg_readcount, entnum, cls.netchan.incoming_sequence);
		*news = *baseline;
	}
	else if (!olds)
	{
		/*reset got lost, probably the data will be filled in later - FIXME: we should probably ignore this entity*/
		if (sv.active)
		{	//for extra debug info
			qcvm_t *old = qcvm;
			qcvm = NULL;
			PR_SwitchQCVM(&sv.qcvm);
			Con_DPrintf("New entity %i(%s / %s) without reset\n", entnum, PR_GetString(EDICT_NUM(entnum)->v.classname), PR_GetString(EDICT_NUM(entnum)->v.model));
			PR_SwitchQCVM(old);
		}
		else
			Con_DPrintf("New entity %i without reset\n", entnum);
		*news = nullentitystate;
	}
	else
		*news = *olds;
	
	if (bits & UF_FRAME)
	{
		if (bits & UF_16BIT)
			news->frame = MSG_ReadShort();
		else
			news->frame = MSG_ReadByte();
	}

	if (bits & UF_ORIGINXY)
	{
		news->origin[0] = MSG_ReadCoord(cl.protocolflags);
		news->origin[1] = MSG_ReadCoord(cl.protocolflags);
	}
	if (bits & UF_ORIGINZ)
		news->origin[2] = MSG_ReadCoord(cl.protocolflags);

	if ((bits & UF_PREDINFO) && !(cl.protocol_pext2 & PEXT2_PREDINFO))
	{
		//predicted stuff gets more precise angles
		if (bits & UF_ANGLESXZ)
		{
			news->angles[0] = MSG_ReadAngle16(cl.protocolflags);
			news->angles[2] = MSG_ReadAngle16(cl.protocolflags);
		}
		if (bits & UF_ANGLESY)
			news->angles[1] = MSG_ReadAngle16(cl.protocolflags);
	}
	else
	{
		if (bits & UF_ANGLESXZ)
		{
			news->angles[0] = MSG_ReadAngle(cl.protocolflags);
			news->angles[2] = MSG_ReadAngle(cl.protocolflags);
		}
		if (bits & UF_ANGLESY)
			news->angles[1] = MSG_ReadAngle(cl.protocolflags);
	}

	if ((bits & (UF_EFFECTS | UF_EFFECTS2)) == (UF_EFFECTS | UF_EFFECTS2))
		news->effects = MSG_ReadLong();
	else if (bits & UF_EFFECTS2)
		news->effects = (unsigned short)MSG_ReadShort();
	else if (bits & UF_EFFECTS)
		news->effects = MSG_ReadByte();

//	news->movement[0] = 0;
//	news->movement[1] = 0;
//	news->movement[2] = 0;
	news->velocity[0] = 0;
	news->velocity[1] = 0;
	news->velocity[2] = 0;
	if (bits & UF_PREDINFO)
	{
		predbits = MSG_ReadByte();

		if (predbits & UFP_FORWARD)
			/*news->movement[0] =*/ MSG_ReadShort();
		//else
		//	news->movement[0] = 0;
		if (predbits & UFP_SIDE)
			/*news->movement[1] =*/ MSG_ReadShort();
		//else
		//	news->movement[1] = 0;
		if (predbits & UFP_UP)
			/*news->movement[2] =*/ MSG_ReadShort();
		//else
		//	news->movement[2] = 0;
		if (predbits & UFP_MOVETYPE)
			news->pmovetype = MSG_ReadByte();
		if (predbits & UFP_VELOCITYXY)
		{
			news->velocity[0] = MSG_ReadShort();
			news->velocity[1] = MSG_ReadShort();
		}
		else
		{
			news->velocity[0] = 0;
			news->velocity[1] = 0;
		}
		if (predbits & UFP_VELOCITYZ)
			news->velocity[2] = MSG_ReadShort();
		else
			news->velocity[2] = 0;
		if (predbits & UFP_MSEC)	//the msec value is how old the update is (qw clients normally predict without the server running an update every frame)
			/*news->msec =*/ MSG_ReadByte();
		//else
		//	news->msec = 0;

		if (cl.protocol_pext2 & PEXT2_PREDINFO)
		{
			if (predbits & UFP_VIEWANGLE)
			{
				if (bits & UF_ANGLESXZ)
				{
					/*news->vangle[0] =*/ MSG_ReadShort();
					/*news->vangle[2] =*/ MSG_ReadShort();
				}
				if (bits & UF_ANGLESY)
					/*news->vangle[1] =*/ MSG_ReadShort();
			}
		}
		else
		{
			if (predbits & UFP_WEAPONFRAME_OLD)
			{
				int wframe;
				wframe = MSG_ReadByte();
				if (wframe & 0x80)
					wframe = (wframe & 127) | (MSG_ReadByte()<<7);
			}
		}
	}
	else
	{
		//news->msec = 0;
	}

	if (!(predbits & UFP_VIEWANGLE) || !(cl.protocol_pext2 & PEXT2_PREDINFO))
	{/*
		if (bits & UF_ANGLESXZ)
			news->vangle[0] = ANGLE2SHORT(news->angles[0] * ((bits & UF_PREDINFO)?-3:-1));
		if (bits & UF_ANGLESY)
			news->vangle[1] = ANGLE2SHORT(news->angles[1]);
		if (bits & UF_ANGLESXZ)
			news->vangle[2] = ANGLE2SHORT(news->angles[2]);
		*/
	}

	if (bits & UF_MODEL)
	{
		if (bits & UF_16BIT)
			news->modelindex = MSG_ReadShort();
		else
			news->modelindex = MSG_ReadByte();
	}
	if (bits & UF_SKIN)
	{
		if (bits & UF_16BIT)
			news->skin = MSG_ReadShort();
		else
			news->skin = MSG_ReadByte();
	}
	if (bits & UF_COLORMAP)
		news->colormap = MSG_ReadByte();

	if (bits & UF_SOLID)
	{	//knowing the size of an entity is important for prediction
		//without prediction, its a bit pointless.
		if (cl.protocol_pext2 & PEXT2_NEWSIZEENCODING)
		{
			byte enc = MSG_ReadByte();
			if (enc == 0)
				news->solidsize = ES_SOLID_NOT;
			else if (enc == 1)
				news->solidsize = ES_SOLID_BSP;
			else if (enc == 2)
				news->solidsize = ES_SOLID_HULL1;
			else if (enc == 3)
				news->solidsize = ES_SOLID_HULL2;
			else if (enc == 16)
				news->solidsize = MSG_ReadSize16(&net_message);
			else if (enc == 32)
				news->solidsize = MSG_ReadLong();
			else
				Sys_Error("Solid+Size encoding not known");
		}
		else
			news->solidsize = MSG_ReadSize16(&net_message);
	}

	if (bits & UF_FLAGS)
		news->eflags = MSG_ReadByte();

	if (bits & UF_ALPHA)
		news->alpha = (MSG_ReadByte()+1)&0xff;
	if (bits & UF_SCALE)
		news->scale = MSG_ReadByte();
	if (bits & UF_BONEDATA)
	{
		unsigned char fl = MSG_ReadByte();
		if (fl & 0x80)
		{
			//this is NOT finalized
			int i;
			int bonecount = MSG_ReadByte();
			//short *bonedata = AllocateBoneSpace(newp, bonecount, &news->boneoffset);
			for (i = 0; i < bonecount*7; i++)
				/*bonedata[i] =*/ MSG_ReadShort();
			//news->bonecount = bonecount;
		}
		//else
			//news->bonecount = 0;	//oo, it went away.
		if (fl & 0x40)
		{
			/*news->basebone =*/ MSG_ReadByte();
			/*news->baseframe =*/ MSG_ReadShort();
		}
		/*else
		{
			news->basebone = 0;
			news->baseframe = 0;
		}*/

		//fixme: basebone, baseframe, etc.
		if (fl & 0x3f)
			Host_EndGame("unsupported entity delta info\n");
	}
//	else if (news->bonecount)
//	{	//still has bone data from the previous frame.
//		short *bonedata = AllocateBoneSpace(newp, news->bonecount, &news->boneoffset);
//		memcpy(bonedata, oldp->bonedata+olds->boneoffset, sizeof(short)*7*news->bonecount);
//	}

	if (bits & UF_DRAWFLAGS)
	{
		int drawflags = MSG_ReadByte();
		if ((drawflags & /*MLS_MASK*/7) == /*MLS_ABSLIGHT*/7)
			/*news->abslight =*/ MSG_ReadByte();
		//else
		//	news->abslight = 0;
		//news->drawflags = drawflags;
	}
	if (bits & UF_TAGINFO)
	{
		news->tagentity = MSG_ReadEntity(cl.protocol_pext2);
		news->tagindex = MSG_ReadByte();
	}
	if (bits & UF_LIGHT)
	{
		/*news->light[0] =*/ MSG_ReadShort();
		/*news->light[1] =*/ MSG_ReadShort();
		/*news->light[2] =*/ MSG_ReadShort();
		/*news->light[3] =*/ MSG_ReadShort();
		/*news->lightstyle =*/ MSG_ReadByte();
		/*news->lightpflags =*/ MSG_ReadByte();
	}
	if (bits & UF_TRAILEFFECT)
	{
		unsigned short v = MSG_ReadShort();
		news->emiteffectnum = 0;
		news->traileffectnum = v & 0x3fff;
		if (v & 0x8000)
			news->emiteffectnum = MSG_ReadShort() & 0x3fff;
		if (news->traileffectnum >= MAX_PARTICLETYPES)
			news->traileffectnum = 0;
		if (news->emiteffectnum >= MAX_PARTICLETYPES)
			news->emiteffectnum = 0;
	}

	if (bits & UF_COLORMOD)
	{
		news->colormod[0] = MSG_ReadByte();
		news->colormod[1] = MSG_ReadByte();
		news->colormod[2] = MSG_ReadByte();
	}
	if (bits & UF_GLOW)
	{
		/*news->glowsize =*/ MSG_ReadByte();
		/*news->glowcolour =*/ MSG_ReadByte();
		news->glowmod[0] = MSG_ReadByte();
		news->glowmod[1] = MSG_ReadByte();
		news->glowmod[2] = MSG_ReadByte();
	}
	if (bits & UF_FATNESS)
		/*news->fatness =*/ MSG_ReadByte();
	if (bits & UF_MODELINDEX2)
	{
		if (bits & UF_16BIT)
			/*news->modelindex2 =*/ MSG_ReadShort();
		else
			/*news->modelindex2 =*/ MSG_ReadByte();
	}
	if (bits & UF_GRAVITYDIR)
	{
		/*news->gravitydir[0] =*/ MSG_ReadByte();
		/*news->gravitydir[1] =*/ MSG_ReadByte();
	}
	if (bits & UF_UNUSED2)
	{
		Host_EndGame("UF_UNUSED2 bit\n");
	}
	if (bits & UF_UNUSED1)
	{
		Host_EndGame("UF_UNUSED1 bit\n");
	}
	return bits;
}
static void CLFTE_ParseBaseline(entity_state_t *es)
{
	CLFTE_ReadDelta(0, es, &nullentitystate, &nullentitystate);
}

//called with both fte+dp deltas
static void CL_EntitiesDeltaed(void)
{
	int			newnum;
	qmodel_t	*model;
	qboolean	forcelink;
	entity_t	*ent;

	for (newnum = 1; newnum < cl.num_entities; newnum++)
	{
		ent = CL_EntityNum(newnum);
		if (!ent->update_type)
			continue;	//not interested in this one

		if (ent->msgtime == cl.mtime[0])
			forcelink = false;	//update got fragmented, don't dirty anything.
		else
		{
			if (ent->msgtime != cl.mtime[1])
				forcelink = true;	// no previous frame to lerp from
			else
				forcelink = false;

			//johnfitz -- lerping
			if (ent->msgtime + 0.2 < cl.mtime[0]) //more than 0.2 seconds since the last message (most entities think every 0.1 sec)
				ent->lerpflags |= LERP_RESETANIM; //if we missed a think, we'd be lerping from the wrong frame

			ent->msgtime = cl.mtime[0];

			if (cl.viewent.model != cl.model_precache[cl.stats[STAT_WEAPON]]) // woods
			{
				cl.viewent.lerpflags |= LERP_WRESET; // woods #wplerp don't lerp animation across model changes
			}

		// shift the known values for interpolation
			VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
			VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);

			VectorCopy (ent->netstate.origin, ent->msg_origins[0]);
			VectorCopy (ent->netstate.angles, ent->msg_angles[0]);
		}
		ent->skinnum = ent->netstate.skin;
		ent->effects = ent->netstate.effects;

		//johnfitz -- lerping for movetype_step entities
		if (ent->netstate.eflags & EFLAGS_STEP)
		{
			ent->lerpflags |= LERP_MOVESTEP;
			ent->forcelink = true;
		}
		else
			ent->lerpflags &= ~LERP_MOVESTEP;

		ent->alpha = ent->netstate.alpha;
/*		if (bits & U_LERPFINISH)
		{
			ent->lerpfinish = ent->msgtime + ((float)(MSG_ReadByte()) / 255);
			ent->lerpflags |= LERP_FINISH;
		}
		else*/
			ent->lerpflags &= ~LERP_FINISH;

		model = cl.model_precache[ent->netstate.modelindex];
		if (model != ent->model)
		{
			ent->model = model;
		// automatic animation (torches, etc) can be either all together
		// or randomized
			if (model)
			{
				if (model->synctype == ST_FRAMETIME)
					ent->syncbase = -cl.time;
				else if (model->synctype == ST_RAND)
					ent->syncbase = (float)(rand()&0x7fff) / 0x7fff;
				else
					ent->syncbase = 0.0;
			}
			else
				forcelink = true;	// hack to make null model players work

			ent->lerpflags |= LERP_RESETANIM; //johnfitz -- don't lerp animation across model changes
		}
		else if (model && model->synctype == ST_FRAMETIME && ent->frame != ent->netstate.frame)
			ent->syncbase = -cl.time;
		ent->frame = ent->netstate.frame;

		if ( forcelink )
		{	// didn't have an update last message
			VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
			VectorCopy (ent->msg_origins[0], ent->origin);
			VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);
			VectorCopy (ent->msg_angles[0], ent->angles);
			ent->forcelink = true;
		}
	}
}

static void CLFTE_ParseEntitiesUpdate(void)
{
	int newnum;
	qboolean removeflag;
	entity_t *ent;
	float newtime;

	//so the server can know when we got it, and guess which frames we didn't get
	if (cls.netcon && cl.ackframes_count < sizeof(cl.ackframes)/sizeof(cl.ackframes[0]))
		cl.ackframes[cl.ackframes_count++] = NET_QSocketGetSequenceIn(cls.netcon);

	if (cl.protocol_pext2 & PEXT2_PREDINFO)
	{
		int seq = (cl.movemessages&0xffff0000) | (unsigned short)MSG_ReadShort();	//an ack from our input sequences. strictly ascending-or-equal
		if (seq > cl.movemessages)
			seq -= 0x10000;	//check for cl.movemessages overflowing the low 16 bits, and compensate.
		cl.ackedmovemessages = seq;

		if (cl.qcvm.extglobals.servercommandframe)
			*cl.qcvm.extglobals.servercommandframe = cl.ackedmovemessages;
	}

	newtime = MSG_ReadFloat ();
	if (newtime != cl.mtime[0])
	{	//don't mess up lerps if the server is splitting entities into multiple packets.
		cl.mtime[1] = cl.mtime[0];
		cl.mtime[0] = newtime;
	}

	for (;;)
	{
		newnum = (unsigned short)(short)MSG_ReadShort();
		removeflag = !!(newnum & 0x8000);
		if (newnum & 0x4000)
			newnum = (newnum & 0x3fff) | (MSG_ReadByte()<<14);
		else
			newnum &= ~0x8000;

		if ((!newnum && !removeflag) || msg_badread)
			break;

		ent = CL_EntityNum(newnum);

		if (removeflag)
		{	//removal.
			if (cl_shownet.value >= 3)
				Con_SafePrintf("%3i:     Remove %i\n", msg_readcount, newnum);

			if (!newnum)
			{
				/*removal of world - means forget all entities, aka a full reset*/
				if (cl_shownet.value >= 3)
					Con_SafePrintf("%3i:     Reset all\n", msg_readcount);
				for (newnum = 1; newnum < cl.num_entities; newnum++)
				{
					CL_EntityNum(newnum)->netstate.pmovetype = 0;
					CL_EntityNum(newnum)->model = NULL;
				}
				cl.requestresend = false;	//we got it.
				continue;
			}
			ent->update_type = false; //no longer valid
			ent->model = NULL;
			ent->spawntime = 0; // woods (iw) #democontrols
			continue;
		}
		else if (ent->update_type)
		{	//simple update
			CLFTE_ReadDelta(newnum, &ent->netstate, &ent->netstate, &ent->baseline);
		}
		else
		{	//we had no previous copy of this entity...
			ent->update_type = true;
			CLFTE_ReadDelta(newnum, &ent->netstate, NULL, &ent->baseline);

			ent->spawntime = cl.mtime[0]; // woods (iw) #democontrols

			//stupid interpolation junk.
			ent->lerpflags |= LERP_RESETMOVE|LERP_RESETANIM;
		}
	}

	CL_EntitiesDeltaed();

	if (cl.protocol_pext2 & PEXT2_PREDINFO)
	{	//stats should normally be sent before the entity data.
		extern cvar_t v_gunkick;
		VectorCopy (cl.mvelocity[0], cl.mvelocity[1]);
		ent = CL_EntityNum(cl.viewentity);
		cl.mvelocity[0][0] = ent->netstate.velocity[0]*(1/8.0);
		cl.mvelocity[0][1] = ent->netstate.velocity[1]*(1/8.0);
		cl.mvelocity[0][2] = ent->netstate.velocity[2]*(1/8.0);
		cl.onground = (ent->netstate.eflags & EFLAGS_ONGROUND)?true:false;


		if (v_gunkick.value == 1)
		{	//truncate away any extra precision, like vanilla/qs would.
			cl.punchangle[0] = cl.stats[STAT_PUNCHANGLE_X];
			cl.punchangle[1] = cl.stats[STAT_PUNCHANGLE_Y];
			cl.punchangle[2] = cl.stats[STAT_PUNCHANGLE_Z];
		}
		else
		{	//woo, more precision
			cl.punchangle[0] = cl.statsf[STAT_PUNCHANGLE_X];
			cl.punchangle[1] = cl.statsf[STAT_PUNCHANGLE_Y];
			cl.punchangle[2] = cl.statsf[STAT_PUNCHANGLE_Z];
		}
		if (v_punchangles[0][0] != cl.punchangle[0] || v_punchangles[0][1] != cl.punchangle[1] || v_punchangles[0][2] != cl.punchangle[2])
		{
			v_punchangles_times[1] = v_punchangles_times[0];
			v_punchangles_times[0] = newtime;

			VectorCopy (v_punchangles[0], v_punchangles[1]);
			VectorCopy (cl.punchangle, v_punchangles[0]);
		}
	}

	if (!cl.requestresend)
	{
		if (cls.signon == SIGNONS - 1)
		{	// first update is the final signon stage
			cls.signon = SIGNONS;
			CL_SignonReply ();
		}
	}
}

static void CSQC_ClearCsEdictForSSQC(size_t entnum)
{
	edict_t *ed;
	if (entnum >= cl.ssqc_to_csqc_max)
		return;	//invalid...

	ed = cl.ssqc_to_csqc[entnum];
	if (ed)
	{
		cl.ssqc_to_csqc[entnum] = NULL;

		//let the csqc know.
		pr_global_struct->self = EDICT_TO_PROG(ed);
		if (qcvm->extfuncs.CSQC_Ent_Remove)
			PR_ExecuteProgram(qcvm->extfuncs.CSQC_Ent_Remove);
		else
			ED_Free(ed);
	}
}
static void CSQC_UpdateCsEdictForSSQC(size_t entnum)
{
	edict_t *ed;
	eval_t *ev;
	qboolean isnew;
	if (entnum >= cl.ssqc_to_csqc_max)
	{
		size_t nc = q_min(MAX_EDICTS, entnum+64);
		void *nptr;
		if (entnum >= nc)
			Host_EndGame("entnum > MAX_EDICTS");
		nptr = realloc(cl.ssqc_to_csqc, nc * sizeof(*cl.ssqc_to_csqc));
		if (!nptr)
			Sys_Error("realloc failure");
		cl.ssqc_to_csqc = nptr;
		memset(cl.ssqc_to_csqc+cl.ssqc_to_csqc_max, 0, (nc-cl.ssqc_to_csqc_max)*sizeof(*cl.ssqc_to_csqc));
		cl.ssqc_to_csqc_max = nc;
	}

	ed = cl.ssqc_to_csqc[entnum];
	if (!ed)
	{
		//allocate our new ent.
		ed = cl.ssqc_to_csqc[entnum] = ED_Alloc();

		//fill its entnum field too.
		ev = GetEdictFieldValue(ed, qcvm->extfields.entnum);
		if (ev)
			ev->_float = entnum;
		isnew = true;
	}
	else
		isnew = false;

	G_FLOAT(OFS_PARM0) = isnew;
	pr_global_struct->self = EDICT_TO_PROG(ed);
	PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_Ent_Update);
}

//csqc entities protocol, payload is identical in both fte+dp. just the svcs differ.
void CLFTE_ParseCSQCEntitiesUpdate(void)
{
	if (qcvm->extfuncs.CSQC_Ent_Update)
	{
		unsigned int entnum;
		qboolean removeflag;
		for(;;)
		{
			//replacement deltas now also includes 22bit entity num indicies.
			if (cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
			{
				entnum = (unsigned short)MSG_ReadShort();
				removeflag = !!(entnum & 0x8000);
				if (entnum & 0x4000)
					entnum = (entnum & 0x3fff) | (MSG_ReadByte()<<14);
				else
					entnum &= ~0x8000;
			}
			else
			{	//otherwise just a 16bit value, with the high bit used as a 'remove' flag
				entnum = (unsigned short)MSG_ReadShort();
				removeflag = !!(entnum & 0x8000);
				entnum &= ~0x8000;
			}
			if ((!entnum && !removeflag) || msg_badread)
				break;	//end of svc

			if (removeflag)
			{
				if (cl_shownet.value >= 3)
					Con_SafePrintf("%3i:     Remove %i\n", msg_readcount, entnum);
				CSQC_ClearCsEdictForSSQC(entnum);
			}
			else
			{
/*				if (sized)
				{
					packetsize = MSG_ReadShort();
					if (cl_shownet.value >= 3)
						Con_SafePrintf("%3i - %3i:     Update %i\n", msg_readcount, msg_readcount+packetsize-1, entnum);
				}
				else
*/				{
					if (cl_shownet.value >= 3)
						Con_SafePrintf("%3i:     Update %i\n", msg_readcount, entnum);
				}

				CSQC_UpdateCsEdictForSSQC(entnum);
//				if (sized)
//					;//TODO make sure we read the right size...
			}
		}
	}
	else
		Host_Error ("Received svc_csqcentities but unable to parse");
}


//darkplaces protocols 5 to 7 use these
#define E5_FULLUPDATE (1<<0)
#define E5_ORIGIN (1<<1)
#define E5_ANGLES (1<<2)
#define E5_MODEL (1<<3)

#define E5_FRAME (1<<4)
#define E5_SKIN (1<<5)
#define E5_EFFECTS (1<<6)
#define E5_EXTEND1 (1<<7)

#define E5_FLAGS (1<<8)
#define E5_ALPHA (1<<9)
#define E5_SCALE (1<<10)
#define E5_ORIGIN32 (1<<11)

#define E5_ANGLES16 (1<<12)
#define E5_MODEL16 (1<<13)
#define E5_COLORMAP (1<<14)
#define E5_EXTEND2 (1<<15)

#define E5_ATTACHMENT (1<<16)
#define E5_LIGHT (1<<17)
#define E5_GLOW (1<<18)
#define E5_EFFECTS16 (1<<19)

#define E5_EFFECTS32 (1<<20)
#define E5_FRAME16 (1<<21)
#define E5_COLORMOD (1<<22)
#define E5_EXTEND3 (1<<23)

#define E5_GLOWMOD (1<<24)
#define E5_COMPLEXANIMATION (1<<25)
#define E5_TRAILEFFECTNUM (1<<26)
#define E5_UNUSED27 (1<<27)
#define E5_UNUSED28 (1<<28)
#define E5_UNUSED29 (1<<29)
#define E5_UNUSED30 (1<<30)
#define E5_EXTEND4 (1<<31)

#define E5_ALLUNUSED (E5_UNUSED27|E5_UNUSED28|E5_UNUSED29|E5_UNUSED30|E5_EXTEND4)
#define E5_ALLUNSUPPORTED (E5_LIGHT|E5_GLOW|E5_GLOWMOD|E5_COMPLEXANIMATION)

static void CLDP_ReadDelta(unsigned int entnum, entity_state_t *s, const entity_state_t *olds, const entity_state_t *baseline)
{
	unsigned int bits = MSG_ReadByte();
	if (bits & E5_EXTEND1)
	{
		bits |= MSG_ReadByte() << 8;
		if (bits & E5_EXTEND2)
		{
			bits |= MSG_ReadByte() << 16;
			if (bits & E5_EXTEND3)
				bits |= MSG_ReadByte() << 24;
		}
	}
	if (bits & (E5_ALLUNSUPPORTED|E5_ALLUNUSED))
	{
		if (bits & E5_ALLUNUSED)
			Host_Error ("E5 update contains unknown bits %x", bits & E5_ALLUNUSED);
		else
			Con_DPrintf ("E5 update contains unsupported bits %x", bits & E5_ALLUNSUPPORTED);
	}

	if (bits & E5_FULLUPDATE)
	{
//		Con_Printf("%3i: Reset %i @ %i\n", msg_readcount, entnum, cls.netchan.incoming_sequence);
		*s = *baseline;
	}
	else if (!olds)
	{
		/*reset got lost, probably the data will be filled in later - FIXME: we should probably ignore this entity*/
		Con_DPrintf("New entity %i without reset\n", entnum);
		*s = nullentitystate;
	}
	else
		*s = *olds;

	if (bits & E5_FLAGS)
	{
		int i = MSG_ReadByte();
		s->eflags = i;
	}
	if (bits & E5_ORIGIN)
	{
		if (bits & E5_ORIGIN32)
		{
			s->origin[0] = MSG_ReadFloat();
			s->origin[1] = MSG_ReadFloat();
			s->origin[2] = MSG_ReadFloat();
		}
		else
		{
			s->origin[0] = MSG_ReadShort()*(1/8.0f);
			s->origin[1] = MSG_ReadShort()*(1/8.0f);
			s->origin[2] = MSG_ReadShort()*(1/8.0f);
		}
	}
	if (bits & E5_ANGLES)
	{
		if (bits & E5_ANGLES16)
		{
			s->angles[0] = MSG_ReadAngle(PRFL_SHORTANGLE);
			s->angles[1] = MSG_ReadAngle(PRFL_SHORTANGLE);
			s->angles[2] = MSG_ReadAngle(PRFL_SHORTANGLE);
		}
		else
		{
			s->angles[0] = MSG_ReadChar() * (360.0/256);
			s->angles[1] = MSG_ReadChar() * (360.0/256);
			s->angles[2] = MSG_ReadChar() * (360.0/256);
		}
	}
	if (bits & E5_MODEL)
	{
		if (bits & E5_MODEL16)
			s->modelindex = (unsigned short) MSG_ReadShort();
		else
			s->modelindex = MSG_ReadByte();
	}
	if (bits & E5_FRAME)
	{
		if (bits & E5_FRAME16)
			s->frame = (unsigned short) MSG_ReadShort();
		else
			s->frame = MSG_ReadByte();
	}
	if (bits & E5_SKIN)
		s->skin = MSG_ReadByte();
	if (bits & E5_EFFECTS)
	{
		if (bits & E5_EFFECTS32)
			s->effects = (unsigned int) MSG_ReadLong();
		else if (bits & E5_EFFECTS16)
			s->effects = (unsigned short) MSG_ReadShort();
		else
			s->effects = MSG_ReadByte();
	}
	if (bits & E5_ALPHA)
		s->alpha = (MSG_ReadByte()+1)&0xff;
	if (bits & E5_SCALE)
		s->scale = MSG_ReadByte();
	if (bits & E5_COLORMAP)
		s->colormap = MSG_ReadByte();
	if (bits & E5_ATTACHMENT)
	{
		s->tagentity = MSG_ReadEntity(cl.protocol_pext2);
		s->tagindex = MSG_ReadByte();
	}
	if (bits & E5_LIGHT)
	{
		/*s->light[0] =*/ MSG_ReadShort();
		/*s->light[1] =*/ MSG_ReadShort();
		/*s->light[2] =*/ MSG_ReadShort();
		/*s->light[3] =*/ MSG_ReadShort();
		/*s->lightstyle =*/ MSG_ReadByte();
		/*s->lightpflags =*/ MSG_ReadByte();
	}
	if (bits & E5_GLOW)
	{
		/*s->glowsize =*/ MSG_ReadByte();
		/*s->glowcolour =*/ MSG_ReadByte();
	}
	if (bits & E5_COLORMOD)
	{
		s->colormod[0] = MSG_ReadByte();
		s->colormod[1] = MSG_ReadByte();
		s->colormod[2] = MSG_ReadByte();
	}
	if (bits & E5_GLOWMOD)
	{
		s->glowmod[0] = MSG_ReadByte();
		s->glowmod[1] = MSG_ReadByte();
		s->glowmod[2] = MSG_ReadByte();
	}
	if (bits & E5_COMPLEXANIMATION)
	{
		int type = MSG_ReadByte();
		int i, numbones;
		if (type == 4)
		{
			/*modelindex = */MSG_ReadShort();
			numbones = MSG_ReadByte();
			for (i = 0; i < numbones*7; i++)
				/*bonedata[i] =*/ MSG_ReadShort();
		}
		else if (type < 4)
		{	//n-way blends
			type++;
			for (i = 0; i < type; i++)
				/*frame = */MSG_ReadShort();
			for (i = 0; i < type; i++)
				/*age = */MSG_ReadShort();
			for (i = 0; i < type; i++)
				/*frac = */(type==1)?255:MSG_ReadByte();
		}
		else
			Host_Error("E5_COMPLEXANIMATION: Parse error - unknown type %i\n", type);
	}
	if (bits & E5_TRAILEFFECTNUM)
		s->traileffectnum = MSG_ReadShort();
}

//dpp5-7 compat
static void CLDP_ParseEntitiesUpdate(void)
{
	entity_t *ent;
	unsigned short id;
	int ack;
	ack = MSG_ReadLong();	//delta sequence number (must be acked)
	if (cl.ackframes_count < sizeof(cl.ackframes)/sizeof(cl.ackframes[0]))
		cl.ackframes[cl.ackframes_count++] = ack;
	cl.ackedmovemessages = MSG_ReadLong();	//input sequence ack
	if (cl.qcvm.extglobals.servercommandframe)
		*cl.qcvm.extglobals.servercommandframe = cl.ackedmovemessages;

	for(;;)
	{
		id = MSG_ReadShort();
		if (msg_badread)
			break;
		if (id & 0x8000)
		{
			id &= ~0x8000;
			if (!id)
				break;	//no more
			ent = CL_EntityNum(id);
			ent->update_type = false;
			ent->model = NULL;
		}
		else
		{
			ent = CL_EntityNum(id);
			CLDP_ReadDelta(id, &ent->netstate, ent->update_type?&ent->netstate:NULL, &ent->baseline);
			ent->update_type = true;
		}
	}

	if (cls.signon == SIGNONS - 1)
	{	// first update is the final signon stage
		cls.signon = SIGNONS;
		CL_SignonReply ();
	}
}


/*
==================
CL_ParseStartSoundPacket
==================
*/
static void CL_ParseStartSoundPacket(void)
{
	vec3_t	pos;
	int	channel, ent;
	int	sound_num;
	int	volume;
	int	field_mask;
	float	attenuation;
	int	i;

	field_mask = MSG_ReadByte();

	if (cl.protocol == PROTOCOL_VERSION_BJP3)
		field_mask |= SND_LARGESOUND;

	//spike -- extra flags
	if (field_mask & SND_FTE_MOREFLAGS)
		field_mask |= MSG_ReadUInt64()<<8;

	if (field_mask & SND_VOLUME)
		volume = MSG_ReadByte ();
	else
		volume = DEFAULT_SOUND_PACKET_VOLUME;

	if (field_mask & SND_ATTENUATION)
		attenuation = MSG_ReadByte () / 64.0;
	else
		attenuation = DEFAULT_SOUND_PACKET_ATTENUATION;

	//fte's sound extensions
	if (cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
	{
		//spike -- our mixer can't deal with these, so just parse and ignore
		if (field_mask & SND_FTE_PITCHADJ)
			MSG_ReadByte();	//percentage
		if (field_mask & SND_FTE_TIMEOFS)
			MSG_ReadShort(); //in ms
		if (field_mask & SND_FTE_VELOCITY)
		{
			MSG_ReadShort(); //1/8th
			MSG_ReadShort(); //1/8th
			MSG_ReadShort(); //1/8th
		}
	}
	else if (field_mask & (SND_FTE_MOREFLAGS|SND_FTE_PITCHADJ|SND_FTE_TIMEOFS))
		Con_Warning("Unknown meaning for sound flags\n");
	//dp's sound extension
	if (cl.protocol == PROTOCOL_VERSION_DP7 || (cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
	{
		if (field_mask & SND_DP_PITCH)
			MSG_ReadShort();
	}
	else if (field_mask & SND_DP_PITCH)
		Con_Warning("Unknown meaning for sound flags\n");

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (field_mask & SND_LARGEENTITY)
	{
		ent = (unsigned short) MSG_ReadShort ();
		channel = MSG_ReadByte ();
	}
	else
	{
		channel = (unsigned short) MSG_ReadShort ();
		ent = channel >> 3;
		channel &= 7;
	}

	if (field_mask & SND_LARGESOUND)
		sound_num = (unsigned short) MSG_ReadShort ();
	else
		sound_num = MSG_ReadByte ();
	//johnfitz

	//johnfitz -- check soundnum
	if (sound_num >= MAX_SOUNDS)
		Host_Error ("CL_ParseStartSoundPacket: %i > MAX_SOUNDS", sound_num);
	//johnfitz

	if (ent > cl.max_edicts) //johnfitz -- no more MAX_EDICTS
		Host_Error ("CL_ParseStartSoundPacket: ent = %i", ent);

	for (i = 0; i < 3; i++)
		pos[i] = MSG_ReadCoord (cl.protocolflags);



	if (cl.qcvm.extfuncs.CSQC_Event_Sound && cl.sound_precache[sound_num] && !cl.qcvm.nogameaccess)
	{	//blocked with csqc, too easy to do dead-reckoning.
		qboolean ret = false;
		PR_SwitchQCVM(&cl.qcvm);

		if (qcvm->extglobals.player_localentnum)
			*qcvm->extglobals.player_localentnum = cl.viewentity;

		G_FLOAT(OFS_PARM0) = ent;
		G_FLOAT(OFS_PARM1) = channel;
		G_INT(OFS_PARM2) = PR_MakeTempString(cl.sound_precache[sound_num]->name);
		G_FLOAT(OFS_PARM3) = volume;
		G_FLOAT(OFS_PARM4) = attenuation;
		VectorCopy(pos, G_VECTOR(OFS_PARM5));
		G_FLOAT(OFS_PARM6) = 100;
		G_FLOAT(OFS_PARM7) = field_mask>>8;
		PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_Event_Sound);
		ret = G_FLOAT(OFS_RETURN);
		PR_SwitchQCVM(NULL);
		if (ret)
			return;
	}

	S_StartSound (ent, channel, cl.sound_precache[sound_num], pos, volume/255.0, attenuation);
}

#if 0
/*
==================
CL_ParseLocalSound - for 2021 rerelease
==================
*/
void CL_ParseLocalSound(void)
{
	int field_mask, sound_num;

	field_mask = MSG_ReadByte();
	sound_num = (field_mask&SND_LARGESOUND) ? MSG_ReadShort() : MSG_ReadByte();
	if (sound_num >= MAX_SOUNDS)
		Host_Error ("CL_ParseLocalSound: %i > MAX_SOUNDS", sound_num);

	S_LocalSound (cl.sound_precache[sound_num]->name);
}

/*
==================
CL_KeepaliveMessage

When the client is taking a long time to load stuff, send keepalive messages
so the server doesn't disconnect.
==================
*/
static byte	net_olddata[NET_MAXMESSAGE];
static void CL_KeepaliveMessage (void)
{
	float	time;
	static float lastmsg;
	int		ret;
	sizebuf_t	old;
	byte	*olddata;

	if (sv.active)
		return;		// no need if server is local
	if (cls.demoplayback)
		return;

// read messages from server, should just be nops
	olddata = net_olddata;
	old = net_message;
	memcpy (olddata, net_message.data, net_message.cursize);

	do
	{
		ret = CL_GetMessage ();
		switch (ret)
		{
		default:
			Host_Error ("CL_KeepaliveMessage: CL_GetMessage failed");
		case 0:
			break;	// nothing waiting
		case 1:
			Host_Error ("CL_KeepaliveMessage: received a message");
			break;
		case 2:
			if (MSG_ReadByte() != svc_nop)
				Host_Error ("CL_KeepaliveMessage: datagram wasn't a nop");
			break;
		}
	} while (ret);

	net_message = old;
	memcpy (net_message.data, olddata, net_message.cursize);

// check time
	time = Sys_DoubleTime ();
	if (time - lastmsg < 5)
		return;
	lastmsg = time;

// write out a nop
	Con_Printf ("--> client to server keepalive\n");

	MSG_WriteByte (&cls.message, clc_nop);
	NET_SendMessage (cls.netcon, &cls.message);
	SZ_Clear (&cls.message);
}
#endif

/*
==================
CL_ParseServerInfo
==================
*/
static void CL_ParseServerInfo (void)
{
	const char	*str;
	int		i;
	qboolean	gamedirswitchwarning = false;
	char gamedir[1024];
	char protname[64];

	Con_DPrintf ("Serverinfo packet received.\n");

// ericw -- bring up loading plaque for map changes within a demo.
//          it will be hidden in CL_SignonReply.
	if (cls.demoplayback)
		SCR_BeginLoadingPlaque();

//
// wipe the client_state_t struct
//
	i = cl.protocol_dpdownload;	//for some absurd reason, this is sent just before the serverinfo, which just confuses everything.
	CL_ClearState ();
	cl.protocol_dpdownload = i;

// parse protocol version number
	for(;;)
	{
		i = MSG_ReadLong ();
		if (i == PROTOCOL_FTE_PEXT1)
		{
			cl.protocol_pext1 = MSG_ReadLong();
			if (cl.protocol_pext1& ~PEXT1_ACCEPTED_CLIENT)
				Host_Error ("Server returned FTE1 protocol extensions that are not supported (%#x)", cl.protocol_pext1 & ~PEXT1_SUPPORTED_CLIENT);
			continue;
		}
		if (i == PROTOCOL_FTE_PEXT2)
		{
			cl.protocol_pext2 = MSG_ReadLong();
			if (cl.protocol_pext2 & ~PEXT2_ACCEPTED_CLIENT)
				Host_Error ("Server returned FTE2 protocol extensions that are not supported (%#x)", cl.protocol_pext2 & ~PEXT2_SUPPORTED_CLIENT);
			continue;
		}
		break;
	}

	//johnfitz -- support multiple protocols
	if (i != PROTOCOL_NETQUAKE && i != PROTOCOL_FITZQUAKE && i != PROTOCOL_RMQ && i != PROTOCOL_VERSION_BJP3 && i != PROTOCOL_VERSION_DP7) {
		Con_Printf ("\n"); //because there's no newline after serverinfo print
		Host_Error ("Server returned version %i, not %i or %i or %i", i, PROTOCOL_NETQUAKE, PROTOCOL_FITZQUAKE, PROTOCOL_RMQ);
	}
	cl.protocol = i;
	//johnfitz

	if (cl.protocol == PROTOCOL_RMQ)
	{
		const unsigned int supportedflags = (PRFL_SHORTANGLE | PRFL_FLOATANGLE | PRFL_24BITCOORD | PRFL_FLOATCOORD | PRFL_EDICTSCALE | PRFL_INT32COORD);
		
		// mh - read protocol flags from server so that we know what protocol features to expect
		cl.protocolflags = (unsigned int) MSG_ReadLong ();
		
		if (0 != (cl.protocolflags & (~supportedflags)))
		{
			Con_Warning("PROTOCOL_RMQ protocolflags %i contains unsupported flags\n", cl.protocolflags);
		}
	}
	else if (cl.protocol == PROTOCOL_VERSION_DP7)
		cl.protocolflags = PRFL_SHORTANGLE|PRFL_FLOATCOORD;
	else cl.protocolflags = 0;

	*gamedir = 0;
	if (cl.protocol_pext2 & PEXT2_PREDINFO)
	{
		q_strlcpy(gamedir, MSG_ReadString(), sizeof(gamedir));
		if (!COM_GameDirMatches(gamedir))
		{
			cl.wronggamedir = true;
		}
	}

// parse maxclients
	cl.maxclients = MSG_ReadByte ();
	if (cl.maxclients < 1 || cl.maxclients > MAX_SCOREBOARD)
	{
		Host_Error ("Bad maxclients (%u) from server", cl.maxclients);
	}
	cl.scores = (scoreboard_t *) Hunk_AllocName (cl.maxclients*sizeof(*cl.scores), "scores");

	cl.teamscores = Hunk_AllocName (14 * sizeof(*cl.teamscores), "teamscores"); // JPG - for teamscore status bar  rook / woods #pqteam

// parse gametype
	cl.gametype = MSG_ReadByte ();

// parse signon message
	str = MSG_ReadString ();
	q_strlcpy (cl.levelname, str, sizeof(cl.levelname));

//johnfitz -- tell user which protocol this is
	if (developer.value)
	{
		//spike: be a little more verbose about it
		switch(cl.protocol)
		{
		case PROTOCOL_VERSION_DP7:
			q_snprintf(protname, sizeof(protname), "%i(dpp7)", cl.protocol);
			break;
		case PROTOCOL_VERSION_BJP3:
			q_snprintf(protname, sizeof(protname), "%i(bjp3)", cl.protocol);
			break;
		case PROTOCOL_RMQ:
			q_snprintf(protname, sizeof(protname), "%i(rmq)", cl.protocol);
			break;
		case PROTOCOL_FITZQUAKE:
			q_snprintf(protname, sizeof(protname), "%i(fitz)", cl.protocol);
			break;
		case PROTOCOL_NETQUAKE:
			if (NET_QSocketGetProQuakeAngleHack(cls.netcon))
				q_snprintf(protname, sizeof(protname), "%i(proquake)", cl.protocol);
			else
				q_snprintf(protname, sizeof(protname), "%i(vanilla)", cl.protocol);
			break;
		default:
			q_snprintf(protname, sizeof(protname), "%i", cl.protocol);
			break;
		}
		if (cl.protocol_pext2)
		{
			if (cl.protocol == PROTOCOL_NETQUAKE)
				*protname = 0;
			else
				q_strlcat(protname, "+", sizeof(protname));
			q_strlcat(protname, va("fte2(%#x)", cl.protocol_pext2), sizeof(protname));
		}
	}
	else if (cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
		q_snprintf(protname, sizeof(protname), "fte%i", cl.protocol);
	else
		q_snprintf(protname, sizeof(protname), "%i", cl.protocol);
	if (con_x)	//some servers try to save spam by skipping the \n. don't make it ugly.
		Con_Printf ("\n");
	Con_Printf ("Using protocol %s\n", protname);

	// seperate the printfs so the server message can have a color
	Con_Printf ("\n%s\n", Con_Quakebar(40)); //johnfitz
	Con_Printf ("%c%s\n", 2, str);

// first we go through and touch all of the precache data that still
// happens to be in the cache, so precaching something else doesn't
// needlessly purge it

// precache models
	memset (cl.model_precache, 0, sizeof(cl.model_precache));
	for (cl.model_count = 1 ; ; cl.model_count++)
	{
		str = MSG_ReadString ();
		if (!str[0])
			break;
		if (cl.model_count == MAX_MODELS)
		{
			Host_Error ("Server sent too many model precaches");
		}
		q_strlcpy (cl.model_name[cl.model_count], str, MAX_QPATH);

		Mod_TouchModel (str);

		if (!strcmp(str, "progs/flag.mdl")) // find the precache number #alternateflags
			ogflagprecache = cl.model_count;

		if (!strcmp(str, "progs/grenade.mdl")) // woods #r2g
			grenadecache = cl.model_count;

		if (!strcmp(str, "progs/missile.mdl")) // woods #r2g
			rocketcache = cl.model_count;
	}

	if (COM_FileExists("progs/ctfmodel.mdl", NULL)) // woods -> does client have alternate flag model? Quake Mission Pack 2: Dissolution of Eternity (Rogue) -- official #alternateflags
	{ 
		const char* ss = "progs/ctfmodel.mdl";
		q_strlcpy(cl.model_name[cl.model_count++], ss, MAX_QPATH);
		swapflagprecache = cl.model_count-1;
		Mod_TouchModel(ss);
	}

	if (COM_FileExists("progs/flag2.mdl", NULL)) // woods -> does client have alternate flag model #alternateflags
	{
		const char* ss2 = "progs/flag2.mdl";
		q_strlcpy(cl.model_name[cl.model_count++], ss2, MAX_QPATH);
		swapflagprecache2 = cl.model_count - 1;
		Mod_TouchModel(ss2);
	}

	if (COM_FileExists("progs/flag3.mdl", NULL)) // woods -> does client have alternate flag model 2 #alternateflags
	{
		const char* ss3 = "progs/flag3.mdl";
		q_strlcpy(cl.model_name[cl.model_count++], ss3, MAX_QPATH);
		swapflagprecache3 = cl.model_count - 1;
		Mod_TouchModel(ss3);
	}

	//johnfitz -- check for excessive models
	if (cl.model_count >= 256)
		Con_DWarning ("%i models exceeds standard limit of 256 (max = %d).\n", cl.model_count, MAX_MODELS);
	//johnfitz

// precache sounds
	memset (cl.sound_precache, 0, sizeof(cl.sound_precache));
	for (cl.sound_count = 1 ; ; cl.sound_count++)
	{
		str = MSG_ReadString ();
		if (!str[0])
			break;
		if (cl.sound_count == MAX_SOUNDS)
		{
			Host_Error ("Server sent too many sound precaches");
		}
		q_strlcpy (cl.sound_name[cl.sound_count], str, MAX_QPATH);
		S_TouchSound (str);
	}

	//johnfitz -- check for excessive sounds
	if (cl.sound_count >= 256)
		Con_DWarning ("%i sounds exceeds standard limit of 256 (max = %d).\n", cl.sound_count, MAX_SOUNDS);

//
// now we try to load everything else until a cache allocation fails
//

	// copy the naked name of the map file to the cl structure -- O.S
	COM_StripExtension (COM_SkipPath(cl.model_name[1]), cl.mapname, sizeof(cl.mapname));

	//johnfitz -- clear out string; we don't consider identical
	//messages to be duplicates if the map has changed in between
	con_lastcenterstring[0] = 0;
	//johnfitz

	Hunk_Check ();		// make sure nothing is hurt

	noclip_anglehack = false;		// noclip is turned off at start

	warn_about_nehahra_protocol = true; //johnfitz -- warn about nehahra protocol hack once per server connection

//johnfitz -- reset developer stats
	memset(&dev_stats, 0, sizeof(dev_stats));
	memset(&dev_peakstats, 0, sizeof(dev_peakstats));
	memset(&dev_overflows, 0, sizeof(dev_overflows));

	cl.requestresend = true;
	cl.ackframes_count = 0;
	if (cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
		cl.ackframes[cl.ackframes_count++] = -1;

	//this is here, to try to make sure its a little more obvious that its there.
	if (cl.wronggamedir)
	{
		const char *curgame = COM_GetGameNames(false);
		if (!*curgame)
			curgame = COM_GetGameNames(true);
		Con_Warning("Server is using a different gamedir.\n");
		Con_Warning("Current: %s\n", curgame);
		Con_Warning("Server: %s\n", gamedir);
		Con_Warning("You will probably want to switch gamedir to match the server.\n");
	}

	S_Voip_MapChange();
}

/*
==================
CL_ParseUpdate

Parse an entity update message from the server
If an entities model or origin changes from frame to frame, it must be
relinked.  Other attributes can change without relinking.
==================
*/
static void CL_ParseUpdate (int bits)
{
	int		i;
	qmodel_t	*model;
	unsigned int	modnum;
	qboolean	forcelink;
	entity_t	*ent;
	int		num;

	if (cls.signon == SIGNONS - 1)
	{	// first update is the final signon stage
		cls.signon = SIGNONS;
		CL_SignonReply ();
	}

	if (bits & U_MOREBITS)
	{
		i = MSG_ReadByte ();
		bits |= (i<<8);
	}

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (cl.protocol == PROTOCOL_FITZQUAKE || cl.protocol == PROTOCOL_RMQ)
	{
		if (bits & U_EXTEND1)
			bits |= MSG_ReadByte() << 16;
		if (bits & U_EXTEND2)
			bits |= MSG_ReadByte() << 24;
	}
	//johnfitz

	if (bits & U_LONGENTITY)
		num = MSG_ReadShort ();
	else
		num = MSG_ReadByte ();

	ent = CL_EntityNum (num);

	if (ent->msgtime != cl.mtime[1])
		forcelink = true;	// no previous frame to lerp from
	else
		forcelink = false;

	//johnfitz -- lerping
	if (ent->msgtime + 0.2 < cl.mtime[0]) //more than 0.2 seconds since the last message (most entities think every 0.1 sec)
		ent->lerpflags |= LERP_RESETANIM; //if we missed a think, we'd be lerping from the wrong frame
	//johnfitz

	ent->msgtime = cl.mtime[0];

	//copy the baseline into the netstate for the rest of the code to use.
	//do NOT copy the origin/angles values, so we don't forget them when hipnotic sends a random pointless fastupdate[playerent] at us with its angles. this way our deltas won't forget it.
	//we don't worry too much about extension stuff going stale, because mods tend not to know about that stuff anyway.
#define netstate_start offsetof(entity_state_t, scale)
	memcpy((char*)&ent->netstate + offsetof(entity_state_t, modelindex), (const char*)&ent->baseline + offsetof(entity_state_t, modelindex), sizeof(ent->baseline) - offsetof(entity_state_t, modelindex));

	if (bits & U_MODEL)
	{
		if (cl.protocol == PROTOCOL_VERSION_BJP3)
			modnum = MSG_ReadShort ();
		else
			modnum = MSG_ReadByte ();
		if (modnum >= MAX_MODELS)
			Host_Error ("CL_ParseModel: bad modnum");
	}
	else
		modnum = ent->baseline.modelindex;

	if (bits & U_FRAME)
		ent->frame = MSG_ReadByte ();
	else
		ent->frame = ent->baseline.frame;

	if (bits & U_COLORMAP)
		ent->netstate.colormap = MSG_ReadByte();
	if (bits & U_SKIN)
		ent->skinnum = MSG_ReadByte();
	else
		ent->skinnum = ent->baseline.skin;
	if (bits & U_EFFECTS)
		ent->effects = MSG_ReadByte();
	else
		ent->effects = ent->baseline.effects;

// shift the known values for interpolation
	VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
	VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);

	if (bits & U_ORIGIN1)
		ent->msg_origins[0][0] = MSG_ReadCoord (cl.protocolflags);
	else
		ent->msg_origins[0][0] = ent->baseline.origin[0];
	if (bits & U_ANGLE1)
		ent->msg_angles[0][0] = MSG_ReadAngle(cl.protocolflags);
	else
		ent->msg_angles[0][0] = ent->baseline.angles[0];

	if (bits & U_ORIGIN2)
		ent->msg_origins[0][1] = MSG_ReadCoord (cl.protocolflags);
	else
		ent->msg_origins[0][1] = ent->baseline.origin[1];
	if (bits & U_ANGLE2)
		ent->msg_angles[0][1] = MSG_ReadAngle(cl.protocolflags);
	else
		ent->msg_angles[0][1] = ent->baseline.angles[1];

	if (bits & U_ORIGIN3)
		ent->msg_origins[0][2] = MSG_ReadCoord (cl.protocolflags);
	else
		ent->msg_origins[0][2] = ent->baseline.origin[2];
	if (bits & U_ANGLE3)
		ent->msg_angles[0][2] = MSG_ReadAngle(cl.protocolflags);
	else
		ent->msg_angles[0][2] = ent->baseline.angles[2];

	//johnfitz -- lerping for movetype_step entities
	if (bits & U_STEP)
	{
		ent->lerpflags |= LERP_MOVESTEP;
		ent->forcelink = true;
	}
	else
		ent->lerpflags &= ~LERP_MOVESTEP;
	//johnfitz

	//johnfitz -- PROTOCOL_FITZQUAKE and PROTOCOL_NEHAHRA
	if (cl.protocol == PROTOCOL_FITZQUAKE || cl.protocol == PROTOCOL_RMQ)
	{
		if (bits & U_ALPHA)
			ent->alpha = MSG_ReadByte();
		else
			ent->alpha = ent->baseline.alpha;
		if (bits & U_SCALE)
			ent->netstate.scale = MSG_ReadByte(); // PROTOCOL_RMQ
		if (bits & U_FRAME2)
			ent->frame = (ent->frame & 0x00FF) | (MSG_ReadByte() << 8);
		if (bits & U_MODEL2)
		{
			modnum = (modnum & 0x00FF) | (MSG_ReadByte() << 8);
			if (modnum >= MAX_MODELS)
				Host_Error ("CL_ParseModel: bad modnum");
		}
		if (bits & U_LERPFINISH)
		{
			ent->lerpfinish = ent->msgtime + ((float)(MSG_ReadByte()) / 255);
			ent->lerpflags |= LERP_FINISH;
		}
		else
			ent->lerpflags &= ~LERP_FINISH;
	}
	else if (cl.protocol == PROTOCOL_NETQUAKE || cl.protocol == PROTOCOL_VERSION_BJP3)
	{
		//HACK: if this bit is set, assume this is PROTOCOL_NEHAHRA instead of PROTOCOL_NETQUAKE
		if (bits & U_TRANS)
		{
			float a, b;

			if (cl.protocol == PROTOCOL_NETQUAKE && warn_about_nehahra_protocol)
			{
				Con_Warning ("nonstandard update bit, assuming Nehahra protocol\n");
				warn_about_nehahra_protocol = false;
			}

			a = MSG_ReadFloat();
			b = MSG_ReadFloat(); //alpha
			if (a == 2)
			{
				if (MSG_ReadFloat() >= 0.5) //parse fullbright, even if we don't use it yet.
					ent->effects |= EF_FULLBRIGHT;
			}
			ent->alpha = ENTALPHA_ENCODE(b);
		}
		else
			ent->alpha = ent->baseline.alpha;
	}
	else
		ent->alpha = ent->baseline.alpha;
	//johnfitz
	
	//johnfitz -- moved here from above
	model = cl.model_precache[modnum];
	if (model != ent->model)
	{
		ent->model = model;
	// automatic animation (torches, etc) can be either all together
	// or randomized
		if (model)
		{
			if (model->synctype == ST_RAND)
				ent->syncbase = (float)(rand()&0x7fff) / 0x7fff;
			else
				ent->syncbase = 0.0;
		}
		else
			forcelink = true;	// hack to make null model players work

		ent->lerpflags |= LERP_RESETANIM; //johnfitz -- don't lerp animation across model changes
	}
	//johnfitz

	if ( forcelink )
	{	// didn't have an update last message
		VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
		VectorCopy (ent->msg_origins[0], ent->origin);
		VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);
		VectorCopy (ent->msg_angles[0], ent->angles);
		ent->forcelink = true;
	}
}

/*
==================
CL_ParseBaseline
==================
*/
static void CL_ParseBaseline (entity_t *ent, int version) //johnfitz -- added argument
{
	int	i;
	int bits, unknownbits;

	if (version == 6)
	{
		CLFTE_ParseBaseline(&ent->baseline);
		return;
	}

	ent->baseline = nullentitystate;

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (cl.protocol == PROTOCOL_VERSION_BJP3 && version == 1)
		bits = B_LARGEMODEL;
	else if (version == 7)
		bits = B_LARGEMODEL|B_LARGEFRAME;	//dpp7's spawnstatic2
	else
		bits = (version == 2) ? MSG_ReadByte() : 0;
	ent->baseline.modelindex = (bits & B_LARGEMODEL) ? MSG_ReadShort() : MSG_ReadByte();
	ent->baseline.frame = (bits & B_LARGEFRAME) ? MSG_ReadShort() : MSG_ReadByte();
	//johnfitz

	ent->baseline.colormap = MSG_ReadByte();
	ent->baseline.skin = MSG_ReadByte();
	for (i = 0; i < 3; i++)
	{
		ent->baseline.origin[i] = MSG_ReadCoord (cl.protocolflags);
		ent->baseline.angles[i] = MSG_ReadAngle (cl.protocolflags);
	}

	if (bits & B_ALPHA)
		ent->baseline.alpha = MSG_ReadByte();
	if (bits & B_SCALE)	//not actually valid in 666, but reading anyway for servers that don't distinguish properly. The warning will have to suffice.
		ent->baseline.scale = MSG_ReadByte();

	if (cl.protocol == PROTOCOL_RMQ)
		unknownbits = ~(B_LARGEMODEL|B_LARGEFRAME|B_ALPHA|B_SCALE);
	else
		unknownbits = ~(B_LARGEMODEL|B_LARGEFRAME|B_ALPHA);
	if (bits & unknownbits)
		Con_Warning("CL_ParseBaseline: Unknown bits %#x\n", bits & unknownbits);
}



/*
==================
CL_ParseClientdata

Server information pertaining to this client only

Spike -- tweaked this function to ensure float stats get set as well as int ones (so csqc can't get confused).
==================
*/
static void CL_ParseClientdata (void)
{
	int		i;
	int		bits; //johnfitz

	bits = (unsigned short)MSG_ReadShort (); //johnfitz -- read bits here isntead of in CL_ParseServerMessage()

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (bits & SU_EXTEND1)
		bits |= (MSG_ReadByte() << 16);
	if (bits & SU_EXTEND2)
		bits |= (MSG_ReadByte() << 24);
	//johnfitz

	if (cl.protocol != PROTOCOL_VERSION_DP7)
		bits |= SU_ITEMS;

	if (bits & SU_VIEWHEIGHT)
		CL_SetStati(STAT_VIEWHEIGHT, MSG_ReadChar ());
	else if (cl.protocol != PROTOCOL_VERSION_DP7)
		CL_SetStati(STAT_VIEWHEIGHT, DEFAULT_VIEWHEIGHT);

	if (bits & SU_IDEALPITCH)
		CL_SetStati(STAT_IDEALPITCH, MSG_ReadChar ());
	else
		CL_SetStati(STAT_IDEALPITCH, 0);

	VectorCopy (cl.mvelocity[0], cl.mvelocity[1]);
	for (i = 0; i < 3; i++)
	{
		if (bits & (SU_PUNCH1<<i) )
			cl.punchangle[i] = (cl.protocol == PROTOCOL_VERSION_DP7)?MSG_ReadAngle(PRFL_SHORTANGLE):MSG_ReadChar();
		else
			cl.punchangle[i] = 0;

		if (cl.protocol == PROTOCOL_VERSION_DP7)
			if (bits & (DPSU_PUNCHVEC1<<i) )
				/*cl.punchvector[i] = */MSG_ReadCoord(cl.protocolflags);

		if (bits & (SU_VELOCITY1<<i) )
			cl.mvelocity[0][i] = (cl.protocol == PROTOCOL_VERSION_DP7)?MSG_ReadFloat():(MSG_ReadChar()*16);
		else
			cl.mvelocity[0][i] = 0;
	}

	//johnfitz -- update v_punchangles
	if (v_punchangles[0][0] != cl.punchangle[0] || v_punchangles[0][1] != cl.punchangle[1] || v_punchangles[0][2] != cl.punchangle[2])
	{
		v_punchangles_times[1] = v_punchangles_times[0];
		v_punchangles_times[0] = cl.mtime[0];
		VectorCopy (v_punchangles[0], v_punchangles[1]);
		VectorCopy (cl.punchangle, v_punchangles[0]);
	}
	//johnfitz

	if (bits & SU_ITEMS)
		CL_SetStati(STAT_ITEMS, MSG_ReadLong ());

	cl.onground = (bits & SU_ONGROUND) != 0;
	cl.inwater = (bits & SU_INWATER) != 0;

	if (cl.protocol == PROTOCOL_VERSION_DP7)
	{	//dpp7 doesn't really send much here, instead using deltas.
		cl.viewent.alpha = ENTALPHA_DEFAULT;
	}
	else
	{
		unsigned short weaponframe = 0;
		unsigned short armourval = 0;
		unsigned short weaponmodel = 0;
		unsigned int activeweapon;
		short health;
		unsigned short ammo;
		unsigned short ammovals[4];

		if (bits & SU_WEAPONFRAME)
			weaponframe = MSG_ReadByte ();
		if (bits & SU_ARMOR)
			armourval = MSG_ReadByte ();
		if (bits & SU_WEAPON)
		{
			if (cl.protocol == PROTOCOL_VERSION_BJP3)
				weaponmodel = MSG_ReadShort();
			else
				weaponmodel = MSG_ReadByte ();
		}
		health = MSG_ReadShort ();
		ammo = MSG_ReadByte ();
		for (i = 0; i < 4; i++)
			ammovals[i] = MSG_ReadByte ();
		activeweapon = MSG_ReadByte ();
		if (!standard_quake)
			activeweapon = 1u<<activeweapon;

		//johnfitz -- PROTOCOL_FITZQUAKE
		if (bits & SU_WEAPON2)
			weaponmodel |= (MSG_ReadByte() << 8);
		if (bits & SU_ARMOR2)
			armourval |= (MSG_ReadByte() << 8);
		if (bits & SU_AMMO2)
			ammo |= (MSG_ReadByte() << 8);
		if (bits & SU_SHELLS2)
			ammovals[0] |= (MSG_ReadByte() << 8);
		if (bits & SU_NAILS2)
			ammovals[1] |= (MSG_ReadByte() << 8);
		if (bits & SU_ROCKETS2)
			ammovals[2] |= (MSG_ReadByte() << 8);
		if (bits & SU_CELLS2)
			ammovals[3] |= (MSG_ReadByte() << 8);
		if (bits & SU_WEAPONFRAME2)
			weaponframe |= (MSG_ReadByte() << 8);
		if (bits & SU_WEAPONALPHA)
			cl.viewent.alpha = MSG_ReadByte();
		else
			cl.viewent.alpha = ENTALPHA_DEFAULT;
		//johnfitz

		CL_SetHudStat(STAT_WEAPONFRAME, weaponframe);
		CL_SetHudStat(STAT_ARMOR, armourval);
		CL_SetHudStat(STAT_WEAPON, weaponmodel);
		CL_SetHudStat(STAT_ACTIVEWEAPON, activeweapon);
		CL_SetHudStat(STAT_HEALTH, health);
		CL_SetHudStat(STAT_AMMO, ammo);
		CL_SetHudStat(STAT_SHELLS, ammovals[0]);
		CL_SetHudStat(STAT_NAILS, ammovals[1]);
		CL_SetHudStat(STAT_ROCKETS, ammovals[2]);
		CL_SetHudStat(STAT_CELLS, ammovals[3]);
	}

	//johnfitz -- lerping
	//ericw -- this was done before the upper 8 bits of cl.stats[STAT_WEAPON] were filled in, breaking on large maps like zendar.bsp
	if (cl.viewent.model != cl.model_precache[cl.stats[STAT_WEAPON]])
	{
		cl.viewent.lerpflags |= LERP_RESETANIM; //don't lerp animation across model changes
	}
	//johnfitz
}

/*
=====================
CL_NewTranslation
=====================
*/
static void CL_NewTranslation (int slot, int vanillacolour)
{
	if (slot > cl.maxclients)
		Sys_Error ("CL_NewTranslation: slot > cl.maxclients");

	cl.scores[slot].shirt = CL_PLColours_FromLegacy((vanillacolour>>4)&0xf);
	cl.scores[slot].pants = CL_PLColours_FromLegacy((vanillacolour>>0)&0xf);
}

/*
=====================
CL_ParseStatic
=====================
*/
static void CL_ParseStatic (int version) //johnfitz -- added a parameter
{
	entity_t *ent;
	int		i;

	i = cl.num_statics;
	if (i >= cl.max_static_entities)
	{
		int ec = 64;
		struct cl_static_entities_s *newstatics = realloc(cl.static_entities, sizeof(*newstatics) * (cl.max_static_entities+ec));
		entity_t *newents = Hunk_Alloc(sizeof(*newents) * ec);
		if (!newstatics || !newents)
			Host_Error ("Too many static entities");
		cl.static_entities = newstatics;
		while (ec--)
			cl.static_entities[cl.max_static_entities++].ent = newents++;
	}

	ent = cl.static_entities[i].ent;
	cl.num_statics++;
	CL_ParseBaseline (ent, version); //johnfitz -- added second parameter

// copy it to the current state

	ent->netstate = ent->baseline;
	ent->eflags = ent->netstate.eflags; //spike -- annoying and probably not used anyway, but w/e

	ent->trailstate = NULL;
	ent->emitstate = NULL;
	ent->model = cl.model_precache[ent->baseline.modelindex];
	ent->lerpflags |= LERP_RESETANIM; //johnfitz -- lerping
	ent->frame = ent->baseline.frame;

	ent->skinnum = ent->baseline.skin;
	ent->effects = ent->baseline.effects;
	ent->alpha = ent->baseline.alpha; //johnfitz -- alpha
	VectorCopy (ent->baseline.origin, ent->origin);
	VectorCopy (ent->baseline.angles, ent->angles);
	CL_LinkStaticEnt(&cl.static_entities[i]);
}

/*
===================
CL_ParseStaticSound
===================
*/
static void CL_ParseStaticSound (int version) //johnfitz -- added argument
{
	vec3_t		org;
	int			sound_num, vol, atten;
	int			i;

	for (i = 0; i < 3; i++)
		org[i] = MSG_ReadCoord (cl.protocolflags);

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (version == 2)
		sound_num = MSG_ReadShort ();
	else
		sound_num = MSG_ReadByte ();
	//johnfitz

	vol = MSG_ReadByte ();
	atten = MSG_ReadByte ();

	S_StaticSound (cl.sound_precache[sound_num], org, vol, atten);
}

/*
CL_ParsePrecache

spike -- added this mostly for particle effects, but its also used for models+sounds (if needed)
*/
static void CL_ParsePrecache(void)
{
	unsigned short code = MSG_ReadShort();
	unsigned int index = code&0x3fff;
	const char *name = MSG_ReadString();
	switch((code>>14) & 0x3)
	{
	case 0:	//models
		if (index < MAX_MODELS)
		{
			if (cl.model_count == index)
				cl.model_count = index+1;
			q_strlcpy (cl.model_name[index], name, MAX_QPATH);
			Mod_TouchModel (name);
			if (!cl.sendprespawn)
			{
				cl.model_precache[index] = Mod_ForName (name, (index==1)?true:false);
				//FIXME: update static entities with that modelindex
				if (cl.model_precache[index] && cl.model_precache[index]->type == mod_brush)
					lightmaps_latecached=true;
			}
		}
		break;
#ifdef PSET_SCRIPT
	case 1:	//particles
		if (index < MAX_PARTICLETYPES)
		{
			if (*name)
			{
				cl.particle_precache[index].name = strcpy(Hunk_Alloc(strlen(name)+1), name);
				cl.particle_precache[index].index = PScript_FindParticleType(cl.particle_precache[index].name);
			}
			else
			{
				cl.particle_precache[index].name = NULL;
				cl.particle_precache[index].index = -1;
			}
		}
		break;
#endif
	case 2:	//sounds
		if (index < MAX_SOUNDS)
		{
			if (cl.sound_count == index)
				cl.sound_count = index+1;
			q_strlcpy (cl.sound_name[index], name, MAX_QPATH);
			cl.sound_precache[index] = S_PrecacheSound (name);
		}
		break;
//	case 3:	//unused
	default:
		Con_Warning("CL_ParsePrecache: unsupported precache type\n");
		break;
	}
}
#ifdef PSET_SCRIPT
int CL_GenerateRandomParticlePrecache(const char *pname);
//small function for simpler reuse
static void CL_ForceProtocolParticles(void)
{
	cl.protocol_particles = true;
	PScript_FindParticleType("effectinfo.");	//make sure this is implicitly loaded.
	COM_Effectinfo_Enumerate(CL_GenerateRandomParticlePrecache);
	Con_Warning("Received svcdp_pointparticles1 but extension not active");
}

/*
CL_RegisterParticles
called when the particle system has changed, and any cached indexes are now probably stale.
*/
void CL_RegisterParticles(void)
{
	int i;

	if (cl.protocol == PROTOCOL_VERSION_DP7)	//dpp7 sucks.
		PScript_FindParticleType("effectinfo.");	//make sure this is implicitly loaded.

	//make sure the precaches know the right effects
	for (i = 0; i < MAX_PARTICLETYPES; i++)
	{
		if (cl.particle_precache[i].name)
			cl.particle_precache[i].index = PScript_FindParticleType(cl.particle_precache[i].name);
		else
			cl.particle_precache[i].index = -1;
	}

	//and make sure models get the right effects+trails etc too
	Mod_ForEachModel(PScript_UpdateModelEffects);
}

/*
CL_ParseParticles

spike -- this handles the various ssqc builtins (the ones that were based on csqc)
*/
static void CL_ParseParticles(int type)
{
	vec3_t org, vel;
	if (type < 0)
	{	//trail
		entity_t *ent;
		int entity = MSG_ReadShort();
		int efnum = MSG_ReadShort();
		org[0] = MSG_ReadCoord(cl.protocolflags);
		org[1] = MSG_ReadCoord(cl.protocolflags);
		org[2] = MSG_ReadCoord(cl.protocolflags);
		vel[0] = MSG_ReadCoord(cl.protocolflags);
		vel[1] = MSG_ReadCoord(cl.protocolflags);
		vel[2] = MSG_ReadCoord(cl.protocolflags);

		ent = CL_EntityNum(entity);

		if (efnum < MAX_PARTICLETYPES && cl.particle_precache[efnum].name)
			PScript_ParticleTrail(org, vel, cl.particle_precache[efnum].index, 1, 0, NULL, &ent->trailstate);
	}
	else
	{	//point
		int efnum = MSG_ReadShort();
		int count;
		org[0] = MSG_ReadCoord(cl.protocolflags);
		org[1] = MSG_ReadCoord(cl.protocolflags);
		org[2] = MSG_ReadCoord(cl.protocolflags);
		if (type)
		{
			vel[0] = vel[1] = vel[2] = 0;
			count = 1;
		}
		else
		{
			vel[0] = MSG_ReadCoord(cl.protocolflags);
			vel[1] = MSG_ReadCoord(cl.protocolflags);
			vel[2] = MSG_ReadCoord(cl.protocolflags);
			count = MSG_ReadShort();
		}
		if (efnum < MAX_PARTICLETYPES && cl.particle_precache[efnum].name)
		{
			PScript_RunParticleEffectState (org, vel, count, cl.particle_precache[efnum].index, NULL);
		}
	}
}
#endif

qboolean cl_mm2; // woods #con_mm1mute

/* 
=======================
CL_ParseProQuakeMessage -- // begin rook / woods #pqteam JPG - added this function for ProQuake messages
=======================
*/
// JPG - added this
int MSG_ReadBytePQ(void)
{
	return MSG_ReadByte() * 16 + MSG_ReadByte() - 272;
}

// JPG - added this
int MSG_ReadShortPQ(void)
{
	return (MSG_ReadBytePQ() * 256 + MSG_ReadBytePQ());
}

int MSG_PeekByte(void)// JPG - need this to check for ProQuake messages
{
	if (msg_readcount + 1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	return (unsigned char)net_message.data[msg_readcount];
}

void CL_ParseProQuakeMessage(void)
{
	int cmd, i;
	int team, shirt, frags;// , ping;

	MSG_ReadByte();//advance after the 'peek' MOD_PROQUAKE(0x01) byte

	cmd = MSG_ReadByte();

	switch (cmd)
	{
	case pqc_new_team:
		Sbar_Changed();
		team = MSG_ReadByte() - 16;
		if (team < 0 || team > 15)
			Host_Error("CL_ParseProQuakeMessage: pqc_new_team invalid team");
		shirt = MSG_ReadByte() - 16;
		cl.teamgame = true;
		cl.teamscores[team].colors = 16 * shirt + team;
		cl.teamscores[team].frags = 0;
		//Con_Printf("pqc_new_team %d %d\n", team, shirt);
		break;

	case pqc_erase_team:
		Sbar_Changed();
		team = MSG_ReadByte() - 16;
		if (team < 0 || team > 15)
			Host_Error("CL_ParseProQuakeMessage: pqc_erase_team invalid team");
		cl.teamscores[team].colors = 0;
		cl.teamscores[team].frags = 0;		// JPG 3.20 - added this
		//Con_Printf("pqc_erase_team %d\n", team);
		break;

	case pqc_team_frags:
		Sbar_Changed();
		cl.teamgame = true;
		team = MSG_ReadByte() - 16;
		if (team < 0 || team > 15)
			Host_Error("CL_ParseProQuakeMessage: pqc_team_frags invalid team");
		frags = MSG_ReadShortPQ();
		if (frags & 32768)
			frags = (frags - 65536);
		cl.teamscores[team].frags = frags;
		//Con_DPrintf (1,"pqc_team_frags %d %d\n", team, frags);
		break;

	case pqc_match_time:
		Sbar_Changed();
		cl.teamgame = true;
		cl.minutes = MSG_ReadBytePQ();
		cl.seconds = MSG_ReadBytePQ();
		cl.last_match_time = cl.time;//todo: fix for demo-rewind
		//Con_Printf("pqc_match_time %d %d\n", cl.minutes, cl.seconds);
		break;

	case pqc_match_reset:
		Sbar_Changed();
		cl.teamgame = true;
		for (i = 0; i < 14; i++)
		{
			cl.teamscores[i].colors = 0;
			cl.teamscores[i].frags = 0;		// JPG 3.20 - added this
		}
		//Con_Printf("pqc_match_reset\n");
		break;
	}
}

/* 
=======================
CL_ParseProQuakeString -- // begin rook / woods #pqteam
=======================
*/
void CL_ParseProQuakeString(char* string) // #pqteam
{
	static int checkping = -1;
	int i;
	int a, b, c; // woods #iplog
	const char* s;//R00k
	// JPG 1.05 - for ip logging woods #iplog
	static int remove_status = 0;
	static int begin_status = 0;
	static int playercount = 0;
	static int checkip = -1;	// player whose IP address we're expecting
	// JPG 3.02 - made this more robust.. try to eliminate screwups due to "unconnected" and '\n'
	s = string;
	const char* observer = "null";
	const char* observing = "null";
	const char* mode = "null";

	if ((cl.gametype == GAME_DEATHMATCH) && (cls.state == ca_connected))
	{// am I colored up?

		char buf[10];
		char buf2[10];
		char buf3[10];
		observer = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "observer", buf, sizeof(buf)); // userinfo
		observing = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "observing", buf2, sizeof(buf2)); // userinfo
		mode = Info_GetKey(cl.scores[cl.realviewentity - 1].userinfo, "mode", buf3, sizeof(buf3)); // userinfo

	}

	if (!q_strcasecmp(observer, "off") && !q_strcasecmp(observing, "off")) // use info keys to detect
		cl.notobserver = 1;
	else
		cl.notobserver = 0;

	if (((cl.seconds > 0 && cl.seconds != 255) || (cl.minutes > 0 && cl.minutes != 255)) && cl.match_pause_time == 0 && q_strcasecmp(mode, "ffa")) // is there a match in progress?
		cl.matchinp = 1;
	else
		cl.matchinp = 0;

	if ((strstr(string, "Match Starting") || strstr(string, "Match begins")) && !cls.demoplayback && cl.teamgame) // try get my attention if match beginning IF on team
	{
		if (cl.notobserver) // am I on a team?
		{
			if (!VID_HasMouseOrInputFocus())
				SDL_FlashWindow((SDL_Window*)VID_GetWindow(), SDL_FLASH_BRIEFLY);
		}
	}

	// woods #con_mm1mute (qrack)

	if (string[1] == '(')
	{
		cl_mm2 = true;
	}
	else
	{
		cl_mm2 = false;
	}

	if (!strncmp(string, "The match has begun!", 20)) // woods #con_mm1mute + other use
	{
		endscoreprint = false; // woods pq_confilter +
		cl.matchinp = 1;
	}

	// check for match time
	if (!strncmp(string, "Match ends in ", 14))
	{
		s = string + 14;

		if ((*s != 'T') && strchr(s, 'm'))
		{
			sscanf(s, "%d", &cl.minutes);
			cl.seconds = 0;
			cl.last_match_time = cl.time;
		}
	}
	else
	{	
		if (!strcmp(string, "Match paused\n"))
			//TODO:R00k add a pause for demo if recording...
		{
			cl.match_pause_time = cl.time;
			cl.matchinp = 0;
		}
		else
		{
			if (!strcmp(string, "Match unpaused\n"))
			{
				cl.last_match_time += (cl.time - cl.match_pause_time);
				cl.match_pause_time = 0;
				cl.matchinp = 1;
			}
			else
			{
				if (!strcmp(string, "The match is over\n"))
				{
					endscoreprint = true; // woods pq_confilter +
					cl.matchinp = 0;
					cl.match_pause_time = 0;
					cl.minutes = 255;					
					if ((cl_autodemo.value == 2) && (cls.demorecording)) // woods #autodemo
						Cbuf_AddText("stop\n");
					
					if (VID_HasMouseOrInputFocus() && !cls.demoplayback)
					{
						if (COM_FileExists("end.cfg", NULL))
							Cbuf_AddText("exec end.cfg\n"); // exec some configs based on serverinfo, hybrid uses userinfo
					}
				}
				if ((cl_autodemo.value == 2) && ((!cls.demoplayback) && (!cls.demorecording))) // intiate autodemo 2 // woods #autodemo
					if ((!strncmp(string, "The match has begun!", 20)) || (!strncmp(string, "minutes remaining", 17)))//crmod doesnt say "begun" so catch the 1st instance of minutes remain, makes the demos miss initial spawn though :(
					{
						Cmd_ExecuteString("record\n", src_command);
					}
				if (strstr(string, "welcome to CRx"))  // woods differemt cfgs per mod #modcfg
				{
					cl.modtype = 4; // woods #modtype [qecrx server check]
					strncpy(cl.observer, "n", sizeof(cl.observer)); // woods #observer set to no on join #observerhud
				}
				if (!strcmp(string, "Sending ClanRing CRCTF v3.5 bindings\n"))  // woods differemt cfgs per mod #modcfg
				{
					cl.modtype = 2; // woods #modtype [crctf server check]
					if (COM_FileExists("ctf.cfg", NULL))
						Cbuf_AddText("exec ctf.cfg\n"); // exec some configs based on serverinfo, hybrid uses userinfo
				}
				if (!strcmp(string, "ClanRing CRCTF v3.5\n"))  // woods #observerhud
				{
					strncpy(cl.observer, "n", sizeof(cl.observer)); // woods #observer set to no on join #observerhud
				}

				char qfmatchlength[13] = { 237, 225, 244, 227, 232, 32, 236, 229, 238, 231, 244, 232,'\0' }; // woods -- quake font red 'match length'

				if ((strstr(string, qfmatchlength) || (strstr(string, "match length"))))  // woods vote match length auto vote yes
				{
					Cbuf_AddText("impulse 115\n");
				}

				char qfClanRing[9] = { 195, 236, 225, 238, 210, 233, 238, 231, '\0' }; // woods -- quake font red 'ClanRing'
				char qfCRMod[6] = { 195, 210, 205, 239, 228, '\0' }; // woods -- quake font red 'CRMod'

				if (!strncmp(string, qfClanRing, 8) && strstr(string, qfCRMod)) // crmod wierd chars // woods differemt cfgs per mod #modcfg
				{
					cl.modtype = 3; // woods #modtype [crmod server check]
					if (COM_FileExists("dm.cfg", NULL))
						SDL_AddTimer(2000, exec_dm_cfg, NULL); // 2 sec delay after connect #execdelay
					strncpy(cl.observer, "n", sizeof(cl.observer)); // woods #observer set to no on join
				}
				if ((!strcmp(string, "classic mode\n")) || (!strcmp(string, "FFA mode\n")))  // woods
				{
					cl.playmode = 2;
				}
				else
				{
					{
						
						{
							if (checkping < 0)
							{
								s = string;
								i = 0;
								while (*s >= '0' && *s <= '9')
									i = 10 * i + *s++ - '0';
								if (!strcmp(s, " minutes remaining\n"))
								{
									cl.minutes = i;
									cl.seconds = 0;
									cl.last_match_time = cl.time;
								}
							}
						}
					}
				}
			}
		}
	}

	// JPG 1.05 check for IP information  // woods for #iplog
	if (iplog_size)
	{
		if (!strncmp(string, "host:    ", 9))
		{
			begin_status = 1;
			if (!cl.console_status)
				remove_status = 1;
		}
		if (begin_status && !strncmp(string, "players: ", 9))
		{
			begin_status = 0;
			remove_status = 0;
			if (sscanf(string + 9, "%d", &playercount))
			{
				if (!cl.console_status)
					*string = 0;
			}
			else
				playercount = 0;
		}
		else if (playercount && string[0] == '#')
		{
			if (!sscanf(string, "#%d", &checkip) || --checkip < 0 || checkip >= cl.maxclients)
				checkip = -1;
			if (!cl.console_status)
				*string = 0;
			remove_status = 0;
		}
		else if (checkip != -1)
		{
			if (sscanf(string, "   %d.%d.%d", &a, &b, &c) == 3)
			{
				cl.scores[checkip].addr = (a << 16) | (b << 8) | c;
				IPLog_Add(cl.scores[checkip].addr, cl.scores[checkip].name);
			}
			checkip = -1;
			if (!cl.console_status)
				*string = 0;
			remove_status = 0;

			if (!--playercount)
				cl.console_status = 0;
		}
		else
		{
			playercount = 0;
			if (remove_status)
				*string = 0;
		}
	}
}

#if 0	/* for debugging. from fteqw. */
static void CL_DumpPacket (void)
{
	int			i, pos;
	unsigned char	*packet = net_message.data;

	Con_Printf("CL_DumpPacket, BEGIN:\n");
	pos = 0;
	while (pos < net_message.cursize)
	{
		Con_Printf("%5i ", pos);
		for (i = 0; i < 16; i++)
		{
			if (pos >= net_message.cursize)
				Con_Printf(" X ");
			else	Con_Printf("%2x ", packet[pos]);
			pos++;
		}
		pos -= 16;
		for (i = 0; i < 16; i++)
		{
			if (pos >= net_message.cursize)
				Con_Printf("X");
			else if (packet[pos] == 0)
				Con_Printf(".");
			else	Con_Printf("%c", packet[pos]);
			pos++;
		}
		Con_Printf("\n");
	}

	Con_Printf("CL_DumpPacket, --- END ---\n");
}
#endif	/* CL_DumpPacket */

#define SHOWNET(x) if(cl_shownet.value==2)Con_Printf ("%3i:%s\n", msg_readcount-1, x);

static void CL_ParseStatNumeric(int stat, int ival, float fval)
{
	if (stat < 0 || stat >= MAX_CL_STATS)
	{
		Con_DWarning ("svc_updatestat: %i is invalid\n", stat);
		return;
	}
	cl.stats[stat] = ival;
	cl.statsf[stat] = fval;
	if (stat == STAT_VIEWZOOM)
		vid.recalc_refdef = true;
	//just assume that they all affect the hud
	Sbar_Changed ();
}
static void CL_ParseStatFloat(int stat, float fval)
{
	CL_ParseStatNumeric(stat,fval,fval);
}
static void CL_ParseStatInt(int stat, int ival)
{
	CL_ParseStatNumeric(stat,ival,ival);
}
static void CL_ParseStatString(int stat, const char *str)
{
	if (stat < 0 || stat >= MAX_CL_STATS)
	{
		Con_DWarning ("svc_updatestat: %i is invalid\n", stat);
		return;
	}
	free(cl.statss[stat]);
	cl.statss[stat] = strdup(str);
	//hud doesn't know/care about any of these strings so don't bother invalidating anything.
}

//mods and servers might not send the \n instantly.
//some mods bug out and omit the \n entirely, this function helps prevent the damage from spreading too much.
//some servers or mods use //prefixed commands as extensions to avoid spam about unrecognised commands.
//proquake has its own extension coding thing.
static void CL_ParseStuffText(const char *msg)
{
	char *str;
	q_strlcat(cl.stuffcmdbuf, msg, sizeof(cl.stuffcmdbuf));
	for (; (str = strchr(cl.stuffcmdbuf, '\n')); memmove(cl.stuffcmdbuf, str, Q_strlen(str)+1))
	{
		qboolean handled = false;
		/* woods comment this out #pqteam 
		if (*cl.stuffcmdbuf == 0x01 && cl.protocol == PROTOCOL_NETQUAKE) //proquake message, just strip this and try again (doesn't necessarily have a trailing \n straight away)
		{
			for (str = cl.stuffcmdbuf+1; *str >= 0x01 && *str <= 0x1f; str++)
				;//FIXME: parse properly
			continue;
		}*/

		*str++ = 0;//skip past the \n

		//handle special commands
		if (cl.stuffcmdbuf[0] == '/' && cl.stuffcmdbuf[1] == '/')
		{
			handled = Cmd_ExecuteString(cl.stuffcmdbuf+2, src_server);
			if (!handled)
				Con_DPrintf("Server sent unknown command %s\n", Cmd_Argv(0));
		}
		else
			handled = Cmd_ExecuteString(cl.stuffcmdbuf, src_server);

		//give the csqc a chance to handle them
		if (!handled && cl.qcvm.extfuncs.CSQC_Parse_StuffCmd && str-cl.stuffcmdbuf<STRINGTEMP_LENGTH)
		{
			char *tmp;
			PR_SwitchQCVM(&cl.qcvm);
			tmp = PR_GetTempString();
			memcpy(tmp, cl.stuffcmdbuf, str-cl.stuffcmdbuf);
			tmp[str-cl.stuffcmdbuf-1] = '\n';	//put the terminator back, for lazy localcmds.
			tmp[str-cl.stuffcmdbuf] = 0;	//null terminate it.
			G_INT(OFS_PARM0) = PR_SetEngineString(tmp);
			PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_Parse_StuffCmd);
			handled = true;	//unfortunately the mod is expected to localcmd unknown things.
			PR_SwitchQCVM(NULL);
		}

		//let the server exec general user commands (massive security hole)
		if (!handled)
		{
			Cbuf_AddTextLen(cl.stuffcmdbuf, str-cl.stuffcmdbuf);
			Cbuf_AddTextLen("\n", 1);
		}
	}
}

//warning: this text might not even be a complete line.
//we're screwed if someone names themselves something that triggers this or some such
//however, that's what has become standard for nq clients
static qboolean CL_ParseSpecialPrints(const char *printtext)
{
	const char *e = printtext+strlen(printtext);
	if (cl.printtype == PRINT_PINGS)
	{
		//players are expected to be listed in slot order.
		//names might need to be a little fuzzy due to some mods toying with svc_updatename.
		//'unconnected' players might cause issues as they might be listed without being in .
		const char *t = printtext;
		char *n;
		int ping;
		while(*t == ' ')
			t++;
		ping = strtol(t, &n, 10);
		if (t != n && *n == ' ' && e[-1] == '\n')
		{
			int i;
			n++;
			e--;
			//warning: some servers might have set svc_playernames with extra text on the end that isn't known to the server itself, and thus won't appear here
			//that text should at least be aligned such that the name part is padded to 15 chars

			if (!strcmp(n, "unconnected\n"))
				return true;	//just ignore unconnecteds, too many dupes etc

			for (i = cl.printplayer; i < MAX_SCOREBOARD; i++)
			{
				if (!*cl.scores[i].name)
					continue; //player slot is empty
				if (strncmp(cl.scores[i].name, n, e-n))
					continue; //reported name is screwy

				cl.scores[i++].ping = ping;
				cl.printplayer = i;
				return true;
			}

			if (!strstr(printtext, "seconds"))
				return true; // woods to get rid of random ping prints

		}
		cl.printtype = PRINT_NONE;
	}

if (!strcmp(printtext, "Client ping times:\n") && (cl.expectingpingtimes > realtime || cls.demoplayback || cls.signon < SIGNONS))
	{
		cl.printtype = PRINT_PINGS;
		cl.printplayer = 0;
		return true;
	}

	/*if (!strncmp(printtext, "host:    ", 9) && cl.expectingstatus > Sys_DoubleTime())
	{
		//host:    *\n
		// *:*\n
		//players: \n\n
		//#%i name frags time
		//   ipaddress
		return true;
	}*/

	//woods #f_random
	if (!cls.demoplayback && *printtext == 1 && e - printtext > 13 && (!strcmp(e - 11, ": f_random\n")))
	{
		if (realtime > cl.printrandom)
		{
			char coin[6];
			char color[5];
			char rps[10];
			int v1 = rand() % 2; // head / tails
			int v2 = rand() % 100 + 1; // 1-100
			int v3 = rand() % 3 + 1; // rock, paper, scissors
			int v4 = rand() % 2; // red or blue
			int v5 = rand() % 21 + 1; // blackjack

			if (v1 == 1)
				sprintf(coin, "heads");
			else
				sprintf(coin, "tails");

			if (v3 == 1)
				sprintf(rps, "rock");
			else if (v3 == 2)
				sprintf(rps, "paper");
			else
				sprintf(rps, "scissors");

			if (v4 == 1)
				sprintf(color, "red");
			else
				sprintf(color, "blue");

			MSG_WriteByte(&cls.message, clc_stringcmd);
			MSG_WriteString(&cls.message, va("say %s, %s, %s, blackjack: %d, 1-100: %d", coin, rps, color, v5, v2));
			cl.printrandom = realtime + 20;
		}
	}

	//woods #f_config check for chat messages of the form 'name: f_config'
	if (!cls.demoplayback && *printtext == 1 && e - printtext > 13 && (!strcmp(e - 11, ": f_config\n")))
	{
		if (realtime > cl.printconfig)
		{
			char key[2];
			char particles[15];
			char textures[4];
			char hud[3];
			char lfps[20];
			char ecolor[10];
			int clampedSbar = CLAMP(1, (int)scr_sbar.value, 3);
			
			if (!strcmp(r_particledesc.string, ""))
				sprintf(particles, "classic");
			else
				sprintf(particles, "%s", r_particledesc.string);

			if (r_lightmap.value == 1 || gl_picmip.value >= 2)
				sprintf(textures, "%s", "OFF");
			else
				sprintf(textures, "%s", "ON");

			if (clampedSbar == 2)
				sprintf(hud, "%s", "qw");
			else if (clampedSbar == 3)
				sprintf(hud, "%s", "qe");
			else
				sprintf(hud, "%s", "nq");

			if (!strcmp(gl_enemycolor.string, ""))
				sprintf(ecolor, "%s", "off");
			else
				sprintf(ecolor, "%.8s", gl_enemycolor.string);

			// for movement key
			int	i, count;
			int bindmap = 0;

			count = 0;
			for (i = 0; i < MAX_KEYS; i++)
			{
				if (keybindings[bindmap][i] && *keybindings[bindmap][i])
				{
					if (!q_strcasecmp(keybindings[bindmap][i], "+forward"))
					{
							if (strlen(Key_KeynumToString(i)) == 1) // could be UPARROW?
							{
								sprintf(key, "%s", Key_KeynumToString(i));
								break;
							}
					}
					else
					{ 
						sprintf(key, "%s", "?");
					}
					count++;
				}
			}

			// for fps
			if (scr_showfps.value)
			{ 
				if (host_maxfps.value == 0)
					sprintf(lfps, "fps (0) %d", cl.fps);
				else
					sprintf(lfps, "fps %d/%s", cl.fps, host_maxfps.string);
			}
			else
				sprintf(lfps, "fpsmax %s", host_maxfps.string);
			
			MSG_WriteByte(&cls.message, clc_stringcmd);
			MSG_WriteString(&cls.message, va("say fov %s, sens %s, fshaft %s, fbmodels %s, %s", scr_fov.string, sensitivity.string, cl_truelightning.string, gl_overbright_models.string, lfps));
			MSG_WriteByte(&cls.message, clc_stringcmd);
			MSG_WriteString(&cls.message, va("say cross %s, vmodel %s, hud %s, particles %s", crosshair.string, r_drawviewmodel.string, hud, particles));
			MSG_WriteByte(&cls.message, clc_stringcmd);
			MSG_WriteString(&cls.message, va("say textures %s, +forward %s, chat %s, ecolor %s", textures, key, cl_say.string, ecolor));
			cl.printconfig = realtime + 20;
		}
	}

	const int bit = sizeof(void*) * 8; // woods add bit, adapted from ironwail
	const char* platform = SDL_GetPlatform(); // woods #q_sysinfo (qrack)

	//check for chat messages of the form 'name: q_version'
	
	if (!cls.demoplayback && *printtext == 1 && e-printtext > 13 && (!strcmp(e-12, ": f_version\n") || !strcmp(e-12, ": q_version\n")))
	{
		if (realtime > cl.printversionresponse)
		{
			MSG_WriteByte (&cls.message, clc_stringcmd);
			MSG_WriteString(&cls.message,va("say %s %s %d-bit", ENGINE_NAME_AND_VER, platform, bit)); // woods add bit, adapted from ironwail
			cl.printversionresponse = realtime+20;
		}
	}

	if (!cls.demoplayback && *printtext == 1 && e - printtext > 13 && (!strcmp(e - 12, ": q_sysinfo\n") || !strcmp(e - 11, ": f_system\n"))) // woods #q_sysinfo (qrack)
	{
		if (realtime > cl.printqsys)
		{

			const char* sound = SDL_GetAudioDeviceName(0, SDL_FALSE); // woods #q_sysinfo (qrack)
			const int sdlRam = SDL_GetSystemRAM(); // woods #q_sysinfo (qrack)
			const int num_cpus = SDL_GetCPUCount(); // woods #q_sysinfo (qrack)
			const int dpi_num = VID_GetCurrentDPI(); // woods #q_sysinfo

#if defined(_WIN32) // use windows registry to get some more detailed info that SDL2 can't, adapted from ezquake
			char* SYSINFO_processor_description = NULL;
			char* SYSINFO_windows_version = NULL;
			int	 SYSINFO_MHz = 0;
			HKEY hKey;

			// find/set registry location
			long ret = RegOpenKey(
				HKEY_LOCAL_MACHINE,
				"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
				&hKey);

			// get processor mhz

			if (ret == ERROR_SUCCESS) {
				DWORD type;
				byte  data[1024];
				DWORD datasize;

				datasize = 1024;
				ret = RegQueryValueEx(
					hKey,
					"~MHz",
					NULL,
					&type,
					data,
					&datasize);

				if (ret == ERROR_SUCCESS && datasize > 0 && type == REG_DWORD)
					SYSINFO_MHz = *((DWORD*)data);

				// get processor name and description

				datasize = 1024;
				ret = RegQueryValueEx(
					hKey,
					"ProcessorNameString",
					NULL,
					&type,
					data,
					&datasize);

				if (ret == ERROR_SUCCESS && datasize > 0 && type == REG_SZ)
					SYSINFO_processor_description = strdup((char*)data);

				// find/set registry location
				long ret = RegOpenKey(
					HKEY_LOCAL_MACHINE,
					"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
					&hKey);

				// get windows version

				datasize = 1024;
				ret = RegQueryValueEx(
					hKey,
					"ProductName",
					NULL,
					&type,
					data,
					&datasize);

				if (ret == ERROR_SUCCESS && datasize > 0 && type == REG_SZ)
					SYSINFO_windows_version = strdup((char*)data);

				RegCloseKey(hKey);
			}

			platform = SYSINFO_windows_version;
	
			MSG_WriteByte(&cls.message, clc_stringcmd);
			MSG_WriteString(&cls.message, va("say %1.1fGHz %s", (float)SYSINFO_MHz/1000, SYSINFO_processor_description));
#endif

#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC) // woods -- use mac terminal to get some more detailed info that SDL2 can't

			char* SYSINFO_processor_description = NULL;
			char* os_codename = NULL;
			char* com_modelname = NULL;

			char buf[75];
			char buf2[16];
			char buf2a[75];
			char os_unknown[30];
			size_t buflen = 75;
			size_t buflen2 = 16;
			size_t buflen2a = 75;

			// get prcoessor info: brand, name, ghz

			if (sysctlbyname("machdep.cpu.brand_string", &buf, &buflen, NULL, 0) == -1)
				SYSINFO_processor_description = "Could Not Find Processor Info";
			else
				SYSINFO_processor_description = buf;

			// get os version: apple codename, release year, and number

			if (sysctlbyname("kern.osproductversion", &buf2, &buflen2, NULL, 0) == -1)
			{
				os_codename = "Unknown OS Name/Version";
			}
			else
			{
				if (!strncmp(buf2, "15.", 3))
					os_codename = "macOS Sequoia (2024)";
				else if (!strncmp(buf2, "14.", 3))
					os_codename = "macOS Sonoma (2023)";
				else if (!strncmp(buf2, "13.", 3))
					os_codename = "macOS Ventura (2022)";
				else if (!strncmp(buf2, "12.", 3))
					os_codename = "macOS Monterey (2021)";
				else if (!strncmp(buf2, "11", 2))
					os_codename = "macOS Big Sur (2020)";
				else if (!strncmp(buf2, "10.15", 4))
					os_codename = "macOS Catalina (2019)";
				else if (!strncmp(buf2, "10.14", 4))
					os_codename = "macOS Mojave (2018)";
				else if (!strncmp(buf2, "10.13", 4))
					os_codename = "macOS High Sierra (2017)";
				else if (!strncmp(buf2, "10.12", 4))
					os_codename = "macOS Sierra (2016)";
				else if (!strncmp(buf2, "10.11", 4))
					os_codename = "Mac OS X El Capitan (2015)";
				else if (!strncmp(buf2, "10.10", 4))
					os_codename = "Mac OS X Yosemite (2014)";
				else if (!strncmp(buf2, "10.9", 3))
					os_codename = "Mac OS X Mavericks (2013)";
				else if (!strncmp(buf2, "10.8", 3))
					os_codename = "Mac OS X Mountain Lion (2012)";
				else if (!strncmp(buf2, "10.7", 3))
					os_codename = "Mac OS X Lion (2011)";
				else if (!strncmp(buf2, "10.6", 3))
					os_codename = "Mac OS X Snow Leopard (2009)";
				else if (!strncmp(buf2, "10.5", 3))
					os_codename = "Mac OS X Leopard (2007)";
				else
				{ 
					sprintf(os_unknown, "macOS %s", buf2);
					os_codename = os_unknown;
				}
			}

			platform = os_codename;

			// get the specific model name and release year

			if (sysctlbyname("hw.optional.arm64", &buf2a, &buflen2a, NULL, 0) == 0) // m1 mac, simplest case

			{
				FILE* fmodelname = popen("/usr/libexec/PlistBuddy -c 'print 0:product-name' /dev/stdin <<< \"$(/usr/sbin/ioreg -ar -d1 -k product-name)\"", "r");

				char buf10[75];
				while (fgets(buf10, sizeof(buf10), fmodelname) != 0)
					pclose(fmodelname);

				if (strstr(buf10, "Mac")) // did it find a clean mac name, if so set var
					com_modelname = buf10;
			}

			else // if not a m1 mac, (About My Mac needs to have run ONCE by user to fill plist, or run it)

			{

				FILE* fmodelname = popen("/usr/libexec/PlistBuddy -c \"print :'CPU Names':$(system_profiler SPHardwareDataType | awk '/Serial/ {print $4}' | cut -c 9-)-en-US_US\" ~/Library/Preferences/com.apple.SystemProfiler.plist", "r");

				char buf3[75];
				while (fgets(buf3, sizeof(buf3), fmodelname) != 0)
					pclose(fmodelname);

				if (strstr(buf3, "Mac"))

					com_modelname = buf3;

				else // did it find a clean mac name, if so set var, else run About My Mac

				{
					FILE* fmodelname1 = popen("/usr/bin/open '/System/Library/CoreServices/Applications/About This Mac.app'; /bin/sleep 2", "r");

					char buf4[75];
					while (fgets(buf4, sizeof(buf4), fmodelname1) != 0)
						pclose(fmodelname1);

					FILE* fmodelname2 = popen("/usr/bin/pkill -ail 'System Information'; /bin/sleep 2", "r");

					char buf5[75];
					while (fgets(buf5, sizeof(buf5), fmodelname2) != 0)
						pclose(fmodelname2);

					FILE* fmodelname3 = popen("/usr/bin/killall cfprefsd; /bin/sleep 2", "r");

					char buf6[75];
					while (fgets(buf6, sizeof(buf6), fmodelname3) != 0)
						pclose(fmodelname3);

					FILE* fmodelname = popen("/usr/libexec/PlistBuddy -c \"print :'CPU Names':$(system_profiler SPHardwareDataType | awk '/Serial/ {print $4}' | cut -c 9-)-en-US_US\" ~/Library/Preferences/com.apple.SystemProfiler.plist", "r");

					char buf3[75];
					while (fgets(buf3, sizeof(buf3), fmodelname) != 0)
						pclose(fmodelname);

					if (strstr(buf3, "Mac"))
						com_modelname = buf3;

					else // did it find a clean mac name, if so set var, else give generic simple name
					{
						char buf7[75];

						if (sysctlbyname("hw.model", &buf7, &buflen, NULL, 0) == -1)
							com_modelname = "Cannot Find Mac Model Name"; // final fall back
						else
							com_modelname = buf7; // second to last fall back
					}
				}
			}

			com_modelname = strtok(com_modelname, "\n");

			MSG_WriteByte(&cls.message, clc_stringcmd);
			MSG_WriteString(&cls.message, va("say %s", com_modelname));
			MSG_WriteByte(&cls.message, clc_stringcmd);
			MSG_WriteString(&cls.message, va("say %s", SYSINFO_processor_description));
#endif
			MSG_WriteByte(&cls.message, clc_stringcmd);
			MSG_WriteString(&cls.message, va("say %s, %d l-cores, %dgb ram", platform, num_cpus, sdlRam / 1000));
			MSG_WriteByte(&cls.message, clc_stringcmd);
			MSG_WriteString(&cls.message, va("say Video: %s", videoc));
			MSG_WriteByte(&cls.message, clc_stringcmd);
			MSG_WriteString(&cls.message, va("say %s %d ppi", videosetg, dpi_num));
			MSG_WriteByte(&cls.message, clc_stringcmd);
			MSG_WriteString(&cls.message, va("say Audio: %s", sound));
		
			cl.printqsys = realtime + 20;
		}
	}

	return false;
}

static void CL_ParsePrint(const char *msg)
{
	const char *str;
	char *tmp;
	if (CL_ParseSpecialPrints(msg))
		return;

	if (cl.qcvm.extfuncs.CSQC_Parse_Print)
	{
		q_strlcat(cl.printbuffer, msg, sizeof(cl.printbuffer));
		for (; *cl.printbuffer; memmove(cl.printbuffer, str, Q_strlen(str)+1))
		{
			for (str = cl.printbuffer; str < cl.printbuffer+STRINGTEMP_LENGTH-1; str++)
			{
				if (*str == '\r' || *str == '\n')
				{
					str++;
					break;
				}
				if (!*str)
					return;
			}
			PR_SwitchQCVM(&cl.qcvm);
			tmp = PR_GetTempString();
			memcpy(tmp, cl.printbuffer, str-cl.printbuffer);
			tmp[str-cl.printbuffer] = 0;
			G_INT(OFS_PARM0) = PR_SetEngineString(tmp);
			G_FLOAT(OFS_PARM1) = ((*tmp=='\1')?3:2);	//guess at the print level. we don't really have them in NQ.
			PR_ExecuteProgram(qcvm->extfuncs.CSQC_Parse_Print);
			PR_SwitchQCVM(NULL);
		}
	}
	else
	{
		if (*cl.printbuffer)
		{
			Con_Printf ("%s", cl.printbuffer);
			*cl.printbuffer = 0;
		}
		Con_Printf ("%s", msg);
	}
}

static void CL_ParseCenterPrint(const char *msg)
{
	char *tmp;
	if (cl.qcvm.extfuncs.CSQC_Parse_CenterPrint)
	{	//let the csqc do it.
		PR_SwitchQCVM(&cl.qcvm);
		tmp = PR_GetTempString();
		q_strlcpy(tmp, msg, STRINGTEMP_LENGTH);
		G_INT(OFS_PARM0) = PR_SetEngineString(tmp);
		PR_ExecuteProgram(qcvm->extfuncs.CSQC_Parse_CenterPrint);
		//qc calls cprint if it wants the legacy behaviour...
		PR_SwitchQCVM(NULL);
	}
	else
		SCR_CenterPrint(msg);
}

/*
=====================
CL_ParseServerMessage
=====================
*/
void CL_ParseServerMessage (void)
{
	int			cmd;
	int			i;
	const char		*str; //johnfitz
	int			lastcmd; //johnfitz
	char*		s;	// woods #pqteam
//
// if recording demos, copy the message out
//
	if (cl_shownet.value == 1)
		Con_Printf ("%i ",net_message.cursize);
	else if (cl_shownet.value == 2)
		Con_Printf ("------------------\n");

//	cl.onground = false;	// unless the server says otherwise

//
// parse the message
//
	MSG_BeginReading ();

	lastcmd = 0;
	while (1)
	{
		if (msg_badread)
			Host_Error ("CL_ParseServerMessage: Bad server message");

		cmd = MSG_ReadByte ();

		if (cmd == -1)
		{
			SHOWNET("END OF MESSAGE");

			if (cl.items != cl.stats[STAT_ITEMS])
			{
				for (i = 0; i < 32; i++)
					if ( (cl.stats[STAT_ITEMS] & (1<<i)) && !(cl.items & (1<<i)))
						cl.item_gettime[i] = cl.time;
				cl.items = cl.stats[STAT_ITEMS];
			}

			if (cl.protocol == PROTOCOL_VERSION_DP7)
				CL_EntitiesDeltaed();
			if (*cl.stuffcmdbuf && net_message.cursize < 512)
				CL_ParseStuffText("\n");	//there's a few mods that forget to write \ns, that then fuck up other things too. So make sure it gets flushed to the cbuf. the cursize check is to reduce backbuffer overflows that would give a false positive.
			if (cls.demoplayback)
				CL_FinishDemoFrame(); // woods (iw) #democontrols
			return;		// end of message
		}

	// if the high bit of the command byte is set, it is a fast update
		if (cmd & U_SIGNAL) //johnfitz -- was 128, changed for clarity
		{
			SHOWNET("fast update");
			CL_ParseUpdate (cmd&127);
			continue;
		}

		if (cmd < (int)NUM_SVC_STRINGS) {
			SHOWNET(svc_strings[cmd]);
		}

	// other commands
		switch (cmd)
		{
		default:
			Host_Error ("Illegible server message %s, previous was %s", svc_strings[cmd], svc_strings[lastcmd]); //johnfitz -- added svc_strings[lastcmd]
		//	CL_DumpPacket ();
			break;

		case svc_nop:
		//	Con_Printf ("svc_nop\n");
			break;

		case svc_time:
			cl.mtime[1] = cl.mtime[0];
			cl.mtime[0] = MSG_ReadFloat ();
			if (cl.protocol_pext2 & PEXT2_PREDINFO)
				MSG_ReadShort();	//input sequence ack.
			break;

		case svc_clientdata:
			CL_ParseClientdata (); //johnfitz -- removed bits parameter, we will read this inside CL_ParseClientdata()
			break;

		case svc_version:
			i = MSG_ReadLong ();
			//johnfitz -- support multiple protocols
			if (i != PROTOCOL_NETQUAKE && i != PROTOCOL_FITZQUAKE && i != PROTOCOL_RMQ)
				Host_Error ("Server returned version %i, not %i or %i or %i", i, PROTOCOL_NETQUAKE, PROTOCOL_FITZQUAKE, PROTOCOL_RMQ);
			cl.protocol = i;
			//johnfitz
			break;

		case svc_disconnect:
			Host_EndGame ("Server disconnected\n");

		case svc_print:
 			s = MSG_ReadString();           //   woods pq string #pqteam
			CL_ParseProQuakeString(s);      //   woods pq string #pqteam
			CL_ParsePrint(s);				//   woods pq string #pqteam
			break;

		case svc_centerprint:
			//johnfitz -- log centerprints to console
			CL_ParseCenterPrint (MSG_ReadString());
			//johnfitz
			break;

		case svc_stufftext:
			// JPG - check for ProQuake message // woods #pqteam
			if (MSG_PeekByte() == 0x01)
				CL_ParseProQuakeMessage();
			// Still want to add text, even on ProQuake messages.  This guarantees compatibility;
			// unrecognized messages will essentially be ignored but there will be no parse errors
			//CL_ParseStuffText(MSG_ReadString());

			str = MSG_ReadString(); // woods Handle userinfo updates -- Fixes empty scoreboard on QSS demos / servers (vkQuake / temx)
			// handle special commands
			if (strlen(str) > 2 && str[0] == '/' && str[1] == '/')
			{
				if (!Cmd_ExecuteString(str + 2, src_server))
					Con_DPrintf("Server sent unknown command %s\n", Cmd_Argv(0));
			}
			else
				CL_ParseStuffText(str);

			break;

		case svc_damage:
			V_ParseDamage ();
			break;

		case svc_serverinfo:
			CL_ParseServerInfo ();
			vid.recalc_refdef = true;	// leave intermission full screen
			break;

		case svc_setangle:
			for (i=0 ; i<3 ; i++)
				cl.viewangles[i] = MSG_ReadAngle (cl.protocolflags);

			if (!cls.demoplayback) // woods #smoothcam
			{
				VectorCopy (cl.mviewangles[0], cl.mviewangles[1]);

				// JPG - hack with last_angle_time to autodetect continuous svc_setangles   // woods #smoothcam
				if (last_angle_time > host_time - 0.3)
					last_angle_time = host_time + 0.3;
				else if (last_angle_time > host_time - 0.6)
					last_angle_time = host_time;
				else
					last_angle_time = host_time - 0.3;

				for (i = 0; i < 3; i++)
					cl.mviewangles[0][i] = cl.viewangles[i];
			}

			break;
		case svcfte_setangledelta:
			for (i=0 ; i<3 ; i++)
				cl.viewangles[i] += MSG_ReadAngle16 (cl.protocolflags);
			break;

		case svc_setview:
			cl.viewentity = MSG_ReadShort ();
			break;

		case svc_lightstyle:
			i = MSG_ReadByte ();
			str = MSG_ReadString();
			CL_UpdateLightstyle(i, str);
			break;

		case svc_sound:
			CL_ParseStartSoundPacket();
			break;

		case svc_stopsound:
			i = MSG_ReadShort();
			S_StopSound(i>>3, i&7);
			break;

		case svc_updatename:
			Sbar_Changed ();
			i = MSG_ReadByte ();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updatename (%u) > MAX_SCOREBOARD (%u)", i, cl.maxclients); // woods - temporary? fix for connection issue
			q_strlcpy (cl.scores[i].name, MSG_ReadString(), MAX_SCOREBOARDNAME);
			Info_SetKey(cl.scores[i].userinfo, sizeof(cl.scores[i].userinfo), "name", cl.scores[i].name);
			break;

		case svc_updatefrags:
			Sbar_Changed ();
			i = MSG_ReadByte ();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updatefrags > MAX_SCOREBOARD");
			cl.scores[i].frags = MSG_ReadShort ();
			break;

		case svc_updatecolors:
			Sbar_Changed ();
			i = MSG_ReadByte ();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updatecolors > MAX_SCOREBOARD");
			CL_NewTranslation (i, MSG_ReadByte());
			Info_SetKey(cl.scores[i].userinfo, sizeof(cl.scores[i].userinfo), "topcolor", va("%d", cl.scores[i].shirt.basic));
			Info_SetKey(cl.scores[i].userinfo, sizeof(cl.scores[i].userinfo), "bottomcolor", va("%d", cl.scores[i].pants.basic));
			break;

		case svc_particle:
			R_ParseParticleEffect ();
			break;

		case svc_spawnbaseline:
			i = MSG_ReadShort ();
			// must use CL_EntityNum() to force cl.num_entities up
			CL_ParseBaseline (CL_EntityNum(i), 1); // johnfitz -- added second parameter
			break;

		case svc_spawnstatic:
			CL_ParseStatic (1); //johnfitz -- added parameter
			break;

		case svc_temp_entity:
			CL_ParseTEnt ();
			break;

		case svc_setpause:
			cl.paused = MSG_ReadByte ();
			if (cl.paused)
			{
				CDAudio_Pause ();
				BGM_Pause ();
			}
			else
			{
				CDAudio_Resume ();
				BGM_Resume ();
			}
			break;

		case svc_signonnum:
			i = MSG_ReadByte ();
			if (i <= cls.signon)
				Host_Error ("Received signon %i when at %i", i, cls.signon);
			cls.signon = i;
			//johnfitz -- if signonnum==2, signon packet has been fully parsed, so check for excessive static ents and efrags
			if (i == 2)
			{
				if (cl.num_statics > 128)
					Con_DWarning ("%i static entities exceeds standard limit of 128.\n", cl.num_statics);
				R_CheckEfrags ();
			}
			//johnfitz
			CL_SignonReply ();
			break;

		case svc_killedmonster:
			cl.stats[STAT_MONSTERS]++;
			cl.statsf[STAT_MONSTERS] = cl.stats[STAT_MONSTERS];
			break;

		case svc_foundsecret:
			cl.stats[STAT_SECRETS]++;
			cl.statsf[STAT_SECRETS] = cl.stats[STAT_SECRETS];
			break;

		case svc_updatestat:
			i = MSG_ReadByte ();
			CL_ParseStatInt(i, MSG_ReadLong());
			break;

		case svc_spawnstaticsound:
			CL_ParseStaticSound (1); //johnfitz -- added parameter
			break;

		case svc_cdtrack:
			cl.cdtrack = MSG_ReadByte ();
			cl.looptrack = MSG_ReadByte ();
			if ( (cls.demoplayback || cls.demorecording) && (cls.forcetrack != -1) )
				BGM_PlayCDtrack ((byte)cls.forcetrack, true);
			else
				BGM_PlayCDtrack ((byte)cl.cdtrack, true);
			break;

		case svc_intermission:
			cl.intermission = 1;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true;	// go to full screen
			V_RestoreAngles ();
			break;

		case svc_finale:
			cl.intermission = 2;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true;	// go to full screen
			//johnfitz -- log centerprints to console
			CL_ParseCenterPrint (MSG_ReadString());
			//johnfitz
			V_RestoreAngles ();
			break;

		case svc_cutscene:
			cl.intermission = 3;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true;	// go to full screen
			//johnfitz -- log centerprints to console
			CL_ParseCenterPrint (MSG_ReadString ());
			//johnfitz
			V_RestoreAngles ();
			break;

		case svc_sellscreen:
			Cmd_ExecuteString ("help", src_command);
			break;

		//johnfitz -- new svc types
		case svc_skybox:
			Sky_LoadSkyBox (MSG_ReadString());
			break;

		case svc_bf:
			Cmd_ExecuteString ("bf", src_command);
			break;

		case svc_fog:
			Fog_ParseServerMessage ();
			break;

		case svc_spawnbaseline2: //PROTOCOL_FITZQUAKE
			i = MSG_ReadShort ();
			// must use CL_EntityNum() to force cl.num_entities up
			CL_ParseBaseline (CL_EntityNum(i), 2);
			break;

		case svc_spawnstatic2: //PROTOCOL_FITZQUAKE
			CL_ParseStatic (2);
			break;

		case svc_spawnstaticsound2: //PROTOCOL_FITZQUAKE
			CL_ParseStaticSound (2);
			break;
		//johnfitz

		//spike -- for particles more than anything else
		case svcdp_precache:
			if (cl.protocol != PROTOCOL_VERSION_DP7 && !cl.protocol_pext2)
				Host_Error ("Received svcdp_precache but extension not active");
			CL_ParsePrecache();
			break;
#ifdef PSET_SCRIPT
		case svcdp_trailparticles:
			if (!cl.protocol_particles)
				CL_ForceProtocolParticles();
			CL_ParseParticles(-1);
			break;
		case svcdp_pointparticles:
			if (!cl.protocol_particles)
				CL_ForceProtocolParticles();
			CL_ParseParticles(0);
			break;
		case svcdp_pointparticles1:
			if (!cl.protocol_particles)
				CL_ForceProtocolParticles();
			CL_ParseParticles(1);
			break;
#endif

		//these two are used by nehahra. we ignore them, parsing only to avoid crashing.
		case svcdp_showpic:
			/*slotname = */MSG_ReadString();
			/*imagename = */MSG_ReadString();
			/*x = */MSG_ReadByte();	//FIXME: nehahra uses bytes, but DP uses shorts for other games. just use csqc instead.
			/*y = */MSG_ReadByte();
			Con_DPrintf("Ignoring svcdp_showpic\n");
			break;
		case svcdp_hidepic:
			/*slotname = */MSG_ReadString();
			Con_DPrintf("Ignoring svcdp_hidepic\n");
			break;

		case 52:
			if (cl.protocol == PROTOCOL_VERSION_DP7)
			{	//svcdp_effect
				CL_ParseEffect(false);
			}
			else
			{	//2021 release: svc_achievement
				str = MSG_ReadString();
				Con_DPrintf("Ignoring svc_achievement (%s)\n", str);
			}
			break;
		case svcdp_effect2:	//these are kinda pointless when the particle system can do it
			if (cl.protocol != PROTOCOL_VERSION_DP7)
				Host_Error ("Received svcdp_effect2 but extension not active");
			CL_ParseEffect(true);
			break;
		case svcdp_csqcentities:	//FTE uses DP's svc number for nq, because compat (despite fte's svc being first). same payload either way.
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS) && cl.protocol != PROTOCOL_VERSION_DP7)
				Host_Error ("Received svcdp_csqcentities but extension not active");
			PR_SwitchQCVM(&cl.qcvm);
			CLFTE_ParseCSQCEntitiesUpdate();
			PR_SwitchQCVM(NULL);
			break;
		case svcdp_spawnbaseline2:	//limited to a handful of extra properties.
			if (cl.protocol != PROTOCOL_VERSION_DP7)
				Host_Error ("Received svcdp_spawnbaseline2 but extension not active");
			i = MSG_ReadShort ();
			CL_ParseBaseline (CL_EntityNum(i), 7);
			break;
		case svcdp_spawnstaticsound2:	//many different ways to use 16bit sounds... no other advantage
			if (cl.protocol != PROTOCOL_VERSION_DP7)
				Host_Error ("Received svcdp_spawnstaticsound2 but extension not active");
			CL_ParseStaticSound (2);
			break;
		case svcdp_spawnstatic2:	//16bit model and frame. no alpha or anything fun.
			if (cl.protocol != PROTOCOL_VERSION_DP7)
				Host_Error ("Received svcdp_spawnstatic2 but extension not active");
			CL_ParseStatic (7);
			break;
		case svcdp_entities:
			if (cl.protocol != PROTOCOL_VERSION_DP7)
				Host_Error ("Received svcdp_entities but extension not active");
			CLDP_ParseEntitiesUpdate();
			break;

		case svcdp_downloaddata:
			if (allow_download.value == 2) // woods #ftehack
			{
				if (cl.protocol != PROTOCOL_VERSION_DP7 && !cl.protocol_dpdownload && cl.protocol != 666) // woods, allow downloads on qecrx (nq physics, FTE server) -- hack
					Host_Error("Received svcdp_downloaddata but extension not active");
			}
			else
			{ 
				if (cl.protocol != PROTOCOL_VERSION_DP7 && !cl.protocol_dpdownload)
					Host_Error ("Received svcdp_downloaddata but extension not active");
			}
			CL_Download_Data();
			break;

		//spike -- new deltas (including new fields etc)
		//stats also changed, and are sent unreliably using the same ack mechanism (which means they're not blocked until the reliables are acked, preventing the need to spam them in every packet).
		case svcdp_updatestatbyte:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS) && cl.protocol != PROTOCOL_VERSION_DP7)
				Host_Error ("Received svcdp_updatestatbyte but extension not active");
			i = MSG_ReadByte ();
			CL_ParseStatInt(i, MSG_ReadByte());
			break;
		case svcfte_updatestatstring:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcfte_updatestatstring but extension not active");
			i = MSG_ReadByte ();
			CL_ParseStatString(i, MSG_ReadString());
			break;
		case svcfte_updatestatfloat:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcfte_updatestatfloat but extension not active");
			i = MSG_ReadByte ();
			CL_ParseStatFloat(i, MSG_ReadFloat());
			break;
		//static ents get all the new fields too, even if the client will probably ignore most of them, the option is at least there to fix it without updating protocols separately.
		case svcfte_spawnstatic2:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcfte_spawnstatic2 but extension not active");
			CL_ParseStatic (6);
			break;
		//baselines have all fields. hurrah for the same delta mechanism
		case svcfte_spawnbaseline2:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcfte_spawnbaseline2 but extension not active");
			i = MSG_ReadEntity (cl.protocol_pext2);
			// must use CL_EntityNum() to force cl.num_entities up
			CL_ParseBaseline (CL_EntityNum(i), 6);
			break;
		//ent updates replace svc_time too
		case svcfte_updateentities:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcfte_updateentities but extension not active");
			CLFTE_ParseEntitiesUpdate();
			break;

		case svcfte_cgamepacket:
			if (!(cl.protocol_pext1 & PEXT1_CSQC))
				Host_Error ("Received svcfte_cgamepacket but extension not active");
			if (cl.qcvm.extfuncs.CSQC_Parse_Event)
			{
				PR_SwitchQCVM(&cl.qcvm);
				PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_Parse_Event);
				PR_SwitchQCVM(NULL);
			}
			else
				Host_Error ("CSQC_Parse_Event: Missing or incompatible CSQC\n");
			break;

		//voicechat, because we can. why reduce packet sizes if you're not going to use that extra space?!?
		case svcfte_voicechat:
			if (!(cl.protocol_pext2 & PEXT2_VOICECHAT))
				Host_Error ("Received svcfte_voicechat but extension not active");
			S_Voip_Parse();
			break;
		}

		lastcmd = cmd; //johnfitz
	}
}

