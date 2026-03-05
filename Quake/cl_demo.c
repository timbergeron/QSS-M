/*
Copyright (C) 1996-2001 Id Software, Inc.
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

#include "time.h"
#include "quakedef.h"

static void CL_FinishTimeDemo (void);
entity_t *CL_EntityNum (int num);

char		demoplaying[MAX_OSPATH]; // woods for window title
char		last_demo[MAX_OSPATH]; // woods #lastdemo

/*
==============================================================================

DEMO CODE

When a demo is playing back, all NET_SendMessages are skipped, and
NET_GetMessages are read from the demo file.

Whenever cl.time gets past the last received message, another message is
read from the demo file.
==============================================================================
*/

// woods (iw) #democontrols
// Demo rewinding
typedef struct
{
	long			fileofs;
	size_t			datasize;
	byte			intermission;
	byte			forceunderwater;
} demoframe_t;

typedef struct
{
	sfx_t* sfx;
	int				ent;
	unsigned short	channel;
	byte			volume;
	byte			attenuation;
	vec3_t			pos;
} soundevent_t;

typedef enum
{
	DFE_LIGHTSTYLE,
	DFE_CSHIFT,
	DFE_SOUND,
	DFE_STAT,
	DFE_STATSTR,
	DFE_ENTITY,
} framevent_t;

typedef struct
{
	int				int_value;
	float			float_value;
} demonumericstat_t;

typedef struct
{
	int				update_type;
	entity_state_t	netstate;
	double			msgtime;
	double			spawntime;
	vec3_t			msg_origins[2];
	vec3_t			msg_angles[2];
	vec3_t			origin;
	vec3_t			angles;
	int				frame;
	int				effects;
	int				skinnum;
	byte			lerpflags;
	qboolean		forcelink;
} demoentitystate_t;

static struct
{
	demoframe_t* frames;
	byte* frame_events;
	soundevent_t* pending_sounds;
	qboolean		backstop;

	struct
	{
		cshift_t	cshift;
		char		lightstyles[MAX_LIGHTSTYLES][MAX_STYLESTRING];
		int			stats[MAX_CL_STATS];
		float		statsf[MAX_CL_STATS];
		char		*statss[MAX_CL_STATS];
		size_t		num_entities;
		demoentitystate_t entity_states[MAX_EDICTS];
	}				prev;
}					demo_rewind;

static void CL_DemoRewindFreePrevStatStrings(void)
{
	size_t i;

	for (i = 0; i < MAX_CL_STATS; i++)
	{
		free(demo_rewind.prev.statss[i]);
		demo_rewind.prev.statss[i] = NULL;
	}
}

static void CL_DemoRewindSetPrevStatString(size_t stat_idx, const char *value)
{
	if (demo_rewind.prev.statss[stat_idx] == value)
		return;

	if (demo_rewind.prev.statss[stat_idx] && value &&
		strcmp(demo_rewind.prev.statss[stat_idx], value) == 0)
		return;

	free(demo_rewind.prev.statss[stat_idx]);
	demo_rewind.prev.statss[stat_idx] = value ? strdup(value) : NULL;
}

static void CL_DemoRewindSnapshotEntity(demoentitystate_t *dst, const entity_t *src)
{
	memset(dst, 0, sizeof(*dst));
	dst->update_type = src->update_type;
	dst->netstate = src->netstate;
	dst->msgtime = src->msgtime;
	dst->spawntime = src->spawntime;
	VectorCopy(src->msg_origins[0], dst->msg_origins[0]);
	VectorCopy(src->msg_origins[1], dst->msg_origins[1]);
	VectorCopy(src->msg_angles[0], dst->msg_angles[0]);
	VectorCopy(src->msg_angles[1], dst->msg_angles[1]);
	VectorCopy(src->origin, dst->origin);
	VectorCopy(src->angles, dst->angles);
	dst->frame = src->frame;
	dst->effects = src->effects;
	dst->skinnum = src->skinnum;
	dst->lerpflags = src->lerpflags;
	dst->forcelink = src->forcelink;
}

static void CL_DemoRewindApplyEntity(const demoentitystate_t *src, entity_t *ent)
{
	ent->update_type = src->update_type;
	ent->netstate = src->netstate;
	ent->msgtime = src->msgtime;
	ent->spawntime = src->spawntime;
	VectorCopy(src->msg_origins[0], ent->msg_origins[0]);
	VectorCopy(src->msg_origins[1], ent->msg_origins[1]);
	VectorCopy(src->msg_angles[0], ent->msg_angles[0]);
	VectorCopy(src->msg_angles[1], ent->msg_angles[1]);
	VectorCopy(src->origin, ent->origin);
	VectorCopy(src->angles, ent->angles);
	ent->frame = src->frame;
	ent->effects = src->effects;
	ent->skinnum = src->skinnum;
	ent->lerpflags = src->lerpflags;
	ent->forcelink = src->forcelink;
	ent->alpha = ent->netstate.alpha;
	ent->eflags = ent->netstate.eflags;

	if (ent->update_type && ent->netstate.modelindex > 0 && ent->netstate.modelindex < MAX_MODELS)
		ent->model = cl.model_precache[ent->netstate.modelindex];
	else
		ent->model = NULL;
}

static void CL_DemoRewindApplyNumericStat(int stat, int ival, float fval)
{
	if (stat < 0 || stat >= MAX_CL_STATS)
		return;

	if (stat == STAT_HEALTH)
	{
		if (cl.stats[STAT_HEALTH] > 0 && ival <= 0)
		{
			cl.cshifts[CSHIFT_DEAD].destcolor[0] = 70;
			cl.cshifts[CSHIFT_DEAD].destcolor[1] = 0;
			cl.cshifts[CSHIFT_DEAD].destcolor[2] = 0;
			cl.cshifts[CSHIFT_DEAD].percent = 0;
		}
		else if (cl.stats[STAT_HEALTH] <= 0 && ival > 0)
		{
			cl.cshifts[CSHIFT_DEAD].percent = 0;
		}
	}

	cl.stats[stat] = ival;
	cl.statsf[stat] = fval;

	if (stat == STAT_VIEWZOOM)
		vid.recalc_refdef = true;

	Sbar_Changed();
}

int demo_target_offset = -1; // woods -- target offset for seeking, -1 when not seeking
qboolean is_seeking = false; // woods -- flag to indicate seeking status
qboolean demo_seek_from_start = false; // woods -- rewind to start before seeking forward
static qboolean initialized = false; // woods (iw) #democontrols

static void CL_ClearDemoFrags(void)
{
	int i;

	for (i = 0; i < cl.maxclients; i++)
		cl.scores[i].frags = 0;
}

static void CL_ResetDemoSeekState(void)
{
	demo_target_offset = -1;
	is_seeking = false;
	demo_seek_from_start = false;
}

/*
===============
CL_AddDemoRewindSound
===============
*/
void CL_AddDemoRewindSound(int entnum, int channel, sfx_t* sfx, vec3_t pos, int vol, float atten)
{
	soundevent_t sound;

	if (entnum <= 0 || channel <= 0)
		return;

	sound.sfx = sfx;
	sound.ent = entnum;
	sound.channel = channel;
	sound.volume = vol;
	sound.attenuation = (int)(atten + 0.5f) * 64.f;
	sound.pos[0] = pos[0];
	sound.pos[1] = pos[1];
	sound.pos[2] = pos[2];

	VEC_PUSH(demo_rewind.pending_sounds, sound);
}

/*
===============
CL_UpdateDemoSpeed
===============
*/
static void CL_UpdateDemoSpeed(void)
{
	extern qboolean keydown[256];
	int adjust, singleframe;
	const float normal_speed = cls.basedemospeed * !cls.demopaused;

	if (key_dest != key_game)
	{
		cls.demospeed = normal_speed;
		return;
	}

	const int dynamic_threshold = q_max(100, cls.demo_file_length * 0.03); // 3% of the demo file length, with a minimum of 100 for very small files
	const float seeking_speed = 256;
	const int demo_seek_start = (cls.demofilestart > 0) ? (int)cls.demofilestart : cls.demo_offset_start;
	const int demo_seek_end = cls.demo_offset_start + cls.demo_file_length;

	if (is_seeking) 
	{
		if (demo_seek_from_start)
		{
			cls.demospeed = -1e9f;
			if (cls.basedemospeed)
				cls.demospeed *= cls.basedemospeed;
			if (demo_rewind.backstop || cls.demo_offset_current <= demo_seek_start)
				demo_seek_from_start = false;
			return;
		}

		if (demo_target_offset < demo_seek_start)
			demo_target_offset = demo_seek_start;
		else if (demo_target_offset > demo_seek_end)
			demo_target_offset = demo_seek_end;

		if (abs(cls.demo_offset_current - demo_target_offset) < dynamic_threshold ||
			cls.demo_offset_current >= demo_target_offset)
		{
			cls.demospeed = normal_speed;
			CL_ResetDemoSeekState();
			return;
		}

		cls.demospeed = seeking_speed;
		demo_rewind.backstop = false;
		return;
	}

	for (int key = '1'; key <= '9'; ++key)
	{
		if (keydown[key])
		{
			float targetPercentage = (key - '0') / 10.0f;

			demo_target_offset = cls.demo_offset_start + (int)(targetPercentage * (float)((cls.demo_file_length + cls.demo_offset_start) - cls.demo_offset_start)); // sometimes start is not 0
			if (demo_target_offset < demo_seek_start)
				demo_target_offset = demo_seek_start;
			else if (demo_target_offset > demo_seek_end)
				demo_target_offset = demo_seek_end;

			if (abs(cls.demo_offset_current - demo_target_offset) < dynamic_threshold)
			{
				cls.demospeed = normal_speed;
				CL_ResetDemoSeekState();
				break;
			}

			demo_seek_from_start = (cls.demo_offset_current > demo_target_offset);
			is_seeking = true;
			if (demo_seek_from_start)
				CL_ClearDemoFrags();
			break;
		}
	}

	adjust = keydown[K_RIGHTARROW] - keydown[K_LEFTARROW];
	singleframe = keydown['.'] - keydown[','];

	if (adjust)
	{
		cls.demospeed = adjust * 60.f;
		if (cls.basedemospeed)
			cls.demospeed *= cls.basedemospeed;
	}
	else if (singleframe && cls.demopaused)
	{
		cls.demospeed = singleframe * 0.03215f;
		if (cls.basedemospeed)
			cls.demospeed *= cls.basedemospeed;
	}
	else if (keydown[K_HOME] || keydown['0'])
	{
		cls.demospeed = -1e9f;
		if (cls.basedemospeed)
			cls.demospeed *= cls.basedemospeed;
		CL_ClearDemoFrags();
		CL_ResetDemoSeekState();
	}
	else if (keydown[K_END])
	{
		cls.demospeed = 1e9f;
		if (cls.basedemospeed)
			cls.demospeed *= cls.basedemospeed;
	}
	else
	{
		cls.demospeed = normal_speed;
	}

	if (keydown[K_CTRL])
		cls.demospeed *= 0.25f;

	if (cls.demospeed > 0.f)
		demo_rewind.backstop = false;
}


/*
====================
CL_AdvanceTime
====================
*/
void CL_AdvanceTime(void)
{
	cl.oldtime = cl.time;

	if (cls.demoplayback)
	{
		CL_UpdateDemoSpeed();
		cl.time += cls.demospeed * host_frametime;
		if (demo_rewind.backstop)
			cl.time = cl.mtime[0];
	}
	else
	{
		cl.time += host_frametime;
	}
}


/*
====================
CL_NextDemoFrame
====================
*/
static qboolean CL_NextDemoFrame(void)
{
	size_t		i, framecount;
	demoframe_t* lastframe;

	VEC_CLEAR(demo_rewind.pending_sounds);

	// Forward playback
	if (cls.demospeed > 0.f)
	{
		if (cls.signon < SIGNONS)
		{
			VEC_CLEAR(demo_rewind.frames);
			VEC_CLEAR(demo_rewind.frame_events);
			CL_DemoRewindFreePrevStatStrings();
			demo_rewind.prev.num_entities = 0;
		}
		else
		{
			demoframe_t newframe;
			size_t snap_num_entities;

			memset(&newframe, 0, sizeof(newframe));
			newframe.fileofs = ftell(cls.demofile);
			newframe.intermission = cl.intermission;
			//	newframe.forceunderwater = cl.forceunderwater;
			VEC_PUSH(demo_rewind.frames, newframe);

			// Take a snapshot of the tracked data at the beginning of this frame
			for (i = 0; i < MAX_LIGHTSTYLES; i++)
				q_strlcpy(demo_rewind.prev.lightstyles[i], cl_lightstyle[i].map, MAX_STYLESTRING);
			memcpy(&demo_rewind.prev.cshift, &cshift_empty, sizeof(cshift_empty));
			memcpy(demo_rewind.prev.stats, cl.stats, sizeof(cl.stats));
			memcpy(demo_rewind.prev.statsf, cl.statsf, sizeof(cl.statsf));
			for (i = 0; i < MAX_CL_STATS; i++)
				CL_DemoRewindSetPrevStatString(i, cl.statss[i]);

			snap_num_entities = (size_t)cl.num_entities;
			if (snap_num_entities > MAX_EDICTS)
				snap_num_entities = MAX_EDICTS;
			demo_rewind.prev.num_entities = snap_num_entities;

			for (i = 1; i < snap_num_entities; i++)
				CL_DemoRewindSnapshotEntity(&demo_rewind.prev.entity_states[i], &cl.entities[i]);
		}
		return true;
	}

	// If we're rewinding we should always have at least one frame to go back to
	framecount = VEC_SIZE(demo_rewind.frames);
	if (!framecount)
		return false;

	lastframe = &demo_rewind.frames[framecount - 1];
	fseek(cls.demofile, lastframe->fileofs, SEEK_SET);

	if (framecount == 1)
		demo_rewind.backstop = true;

	return true;
}

/*
===============
CL_FinishDemoFrame
===============
*/
void CL_FinishDemoFrame(void)
{
	size_t		i, len, numframes;
	demoframe_t* lastframe;

	if (!cls.demoplayback || !cls.demospeed)
		return;

	// Flush any pending stuffcmds (such as v_chifts)
	// so that they take effect this frame, not the next
	Cbuf_Execute();

	// We're not going to rewind before the first frame,
	// so we only track state changes from the second one onwards
	numframes = VEC_SIZE(demo_rewind.frames);
	if (numframes < 2)
		return;

	lastframe = &demo_rewind.frames[numframes - 1];

	if (cls.demospeed > 0.f) // forward playback
	{
		SDL_assert(lastframe->datasize == 0);

		// Save the previous cshift value if it changed this frame
		if (memcmp(&demo_rewind.prev.cshift, &cshift_empty, sizeof(cshift_t)) != 0)
		{
			VEC_PUSH(demo_rewind.frame_events, DFE_CSHIFT);
			Vec_Append((void**)&demo_rewind.frame_events, 1, &demo_rewind.prev.cshift, sizeof(cshift_t));
			lastframe->datasize += 1 + sizeof(cshift_t);
		}

		// Save the previous value for any changed lightstyle
		for (i = 0; i < MAX_LIGHTSTYLES; i++)
		{
			if (strcmp(demo_rewind.prev.lightstyles[i], cl_lightstyle[i].map) == 0)
				continue;
			len = strlen(demo_rewind.prev.lightstyles[i]);
			VEC_PUSH(demo_rewind.frame_events, DFE_LIGHTSTYLE);
			VEC_PUSH(demo_rewind.frame_events, (byte)i);
			VEC_PUSH(demo_rewind.frame_events, (byte)len);
			Vec_Append((void**)&demo_rewind.frame_events, 1, demo_rewind.prev.lightstyles[i], len);
			lastframe->datasize += 3 + len;
		}

		// Save the previous value for any changed numeric stats
		for (i = 0; i < MAX_CL_STATS; i++)
		{
			demonumericstat_t prevstat;

			if (demo_rewind.prev.stats[i] == cl.stats[i] &&
				demo_rewind.prev.statsf[i] == cl.statsf[i])
				continue;

			prevstat.int_value = demo_rewind.prev.stats[i];
			prevstat.float_value = demo_rewind.prev.statsf[i];

			VEC_PUSH(demo_rewind.frame_events, DFE_STAT);
			VEC_PUSH(demo_rewind.frame_events, (byte)i);
			Vec_Append((void**)&demo_rewind.frame_events, 1, &prevstat, sizeof(prevstat));
			lastframe->datasize += 1 + 1 + sizeof(prevstat);

			demo_rewind.prev.stats[i] = cl.stats[i];
			demo_rewind.prev.statsf[i] = cl.statsf[i];
		}

		// Save the previous value for any changed string stats
		for (i = 0; i < MAX_CL_STATS; i++)
		{
			const char *prevstr = demo_rewind.prev.statss[i];
			const char *curstr = cl.statss[i];
			size_t strlen_prev;
			uint32_t strlen_prev32;

			if ((!prevstr && !curstr) ||
				(prevstr && curstr && strcmp(prevstr, curstr) == 0))
				continue;

			strlen_prev = prevstr ? strlen(prevstr) : 0;
			if (strlen_prev > 0xFFFFFFFFu)
				Sys_Error("CL_FinishDemoFrame: stat string too large");
			strlen_prev32 = (uint32_t)strlen_prev;

			VEC_PUSH(demo_rewind.frame_events, DFE_STATSTR);
			VEC_PUSH(demo_rewind.frame_events, (byte)i);
			Vec_Append((void**)&demo_rewind.frame_events, 1, &strlen_prev32, sizeof(strlen_prev32));
			if (strlen_prev)
				Vec_Append((void**)&demo_rewind.frame_events, 1, prevstr, strlen_prev);
			lastframe->datasize += 1 + 1 + sizeof(strlen_prev32) + strlen_prev;

			CL_DemoRewindSetPrevStatString(i, curstr);
		}

		// Save previous state for any changed entities (including create/remove)
		{
			size_t cur_num_entities = (size_t)cl.num_entities;
			size_t max_num_entities;
			demoentitystate_t empty_state;

			if (cur_num_entities > MAX_EDICTS)
				cur_num_entities = MAX_EDICTS;
			max_num_entities = q_max(demo_rewind.prev.num_entities, cur_num_entities);
			memset(&empty_state, 0, sizeof(empty_state));

			for (i = 1; i < max_num_entities; i++)
			{
				const demoentitystate_t *prevstate;
				demoentitystate_t curstate;

				prevstate = (i < demo_rewind.prev.num_entities) ?
					&demo_rewind.prev.entity_states[i] : &empty_state;

				memset(&curstate, 0, sizeof(curstate));
				if (i < cur_num_entities)
					CL_DemoRewindSnapshotEntity(&curstate, &cl.entities[i]);

				if (memcmp(prevstate, &curstate, sizeof(curstate)) != 0)
				{
					unsigned short entnum = (unsigned short)i;
					VEC_PUSH(demo_rewind.frame_events, DFE_ENTITY);
					Vec_Append((void**)&demo_rewind.frame_events, 1, &entnum, sizeof(entnum));
					Vec_Append((void**)&demo_rewind.frame_events, 1, prevstate, sizeof(*prevstate));
					lastframe->datasize += 1 + sizeof(entnum) + sizeof(*prevstate);
				}
			}
		}

		// Play back pending sounds in reverse order
		len = VEC_SIZE(demo_rewind.pending_sounds);
		while (len > 0)
		{
			soundevent_t* snd = &demo_rewind.pending_sounds[--len];
			VEC_PUSH(demo_rewind.frame_events, DFE_SOUND);
			Vec_Append((void**)&demo_rewind.frame_events, 1, snd, sizeof(*snd));
			lastframe->datasize += 1 + sizeof(*snd);
		}
		VEC_CLEAR(demo_rewind.pending_sounds);
	}
	else // rewinding
	{
		// Revert tracked state changes in this frame
		if (lastframe->datasize > 0)
		{
			size_t end = VEC_SIZE(demo_rewind.frame_events);
			size_t begin;

			if (lastframe->datasize > end)
				Sys_Error("CL_FinishDemoFrame: invalid event span");
			begin = end - lastframe->datasize;

			while (begin < end)
			{
				byte* data = &demo_rewind.frame_events[begin++];
				byte	datatype = *data++;

				switch (datatype)
				{
				case DFE_LIGHTSTYLE:
				{
					char	str[MAX_STYLESTRING];
					byte	style;

					style = *data++;
					len = *data++;
					memcpy(str, data, len);
					str[len] = '\0';
					CL_UpdateLightstyle(style, str);

					begin += 2 + len;
				}
				break;

				case DFE_CSHIFT:
				{
					memcpy(&cshift_empty, data, sizeof(cshift_empty));
					begin += sizeof(cshift_empty);
				}
				break;

				case DFE_STAT:
				{
					int stat_idx = *data++;
					demonumericstat_t stat;

					memcpy(&stat, data, sizeof(stat));
					CL_DemoRewindApplyNumericStat(stat_idx, stat.int_value, stat.float_value);

					begin += 1 + sizeof(stat);
				}
				break;

				case DFE_STATSTR:
				{
					int stat_idx = *data++;
					uint32_t str_len32;
					size_t str_len;
					char *str = NULL;
					size_t remaining = end - begin;

					if (remaining < 1 + sizeof(str_len32))
						Sys_Error("CL_FinishDemoFrame: bad stat string event size");

					memcpy(&str_len32, data, sizeof(str_len32));
					str_len = (size_t)str_len32;
					data += sizeof(str_len32);
					if (str_len > remaining - (1 + sizeof(str_len32)))
						Sys_Error("CL_FinishDemoFrame: bad stat string length");
					if (str_len)
					{
						str = (char *)malloc(str_len + 1);
						if (!str)
							Sys_Error("CL_FinishDemoFrame: failed to alloc stat string");
						memcpy(str, data, str_len);
						str[str_len] = '\0';
					}

					if (stat_idx >= 0 && stat_idx < MAX_CL_STATS)
					{
						free(cl.statss[stat_idx]);
						cl.statss[stat_idx] = str;
						Sbar_Changed();
					}
					else
					{
						free(str);
					}

					begin += 1 + sizeof(str_len32) + str_len;
				}
				break;

				case DFE_ENTITY:
				{
					unsigned short entnum;
					demoentitystate_t entstate;

					memcpy(&entnum, data, sizeof(entnum));
					data += sizeof(entnum);
					memcpy(&entstate, data, sizeof(entstate));

					if (entnum >= 1 && entnum < cl.max_edicts)
					{
						entity_t *ent = CL_EntityNum(entnum);
						CL_DemoRewindApplyEntity(&entstate, ent);
					}
					begin += sizeof(entnum) + sizeof(entstate);
				}
				break;

				case DFE_SOUND:
				{
					soundevent_t snd;

					memcpy(&snd, data, sizeof(snd));
					if (snd.sfx)
						S_StartSound(snd.ent, snd.channel, snd.sfx, snd.pos, snd.volume / 255.0, snd.attenuation / 64.f);
					else
						S_StopSound(snd.ent, snd.channel);

					begin += sizeof(snd);
				}
				break;

				default:
					Sys_Error("CL_FinishDemoFrame: bad event type %d", datatype);
					break;
				}
			}

			SDL_assert(begin == end);

			VEC_POP_N(demo_rewind.frame_events, lastframe->datasize);
			lastframe->datasize = 0;
		}

		if (cl.intermission != lastframe->intermission && !lastframe->intermission)
			cl.completed_time = 0;
		cl.intermission = lastframe->intermission;
		//cl.forceunderwater = lastframe->forceunderwater;

		cl.faceanimtime = 0; // woods
		memset(cl_dlights, 0, sizeof(cl_dlights)); // woods
		//memset(cl_temp_entities, 0, sizeof(cl_temp_entities));

		// Reset viewmodel state to ensure it updates immediately
		cl.viewent.model = NULL;
		cl.viewent.frame = 0;
		cl.viewent.lerpflags |= LERP_RESETANIM | LERP_RESETMOVE;

		VEC_POP(demo_rewind.frames);
	}
}

void Log_Last_Demo_f (void) // woods #lastdemo
{
	FILE* f;
	char demodir[MAX_OSPATH];

	q_snprintf(demodir, sizeof(demodir), "%s/id1/backups", com_basedir);
	Sys_mkdir(demodir);

	f = fopen(va("%s/id1/backups/%s.txt", com_basedir, "lastdemo"), "w");

	if (!f)
	{
		Con_Printf("Couldn't write backup last demo\n");
		return;
	}

	fprintf(f, "%s", last_demo);

	fclose(f);
}

void Load_Last_Demo (void) // woods #lastdemo
{
	FILE* f;
	char demodir[MAX_OSPATH];

	q_snprintf(demodir, sizeof(demodir), "%s/id1/backups/lastdemo.txt", com_basedir);

	f = fopen(demodir, "r");
	if (!f)
		return;

	if (fgets(last_demo, sizeof(last_demo), f))
	{
		// Remove any trailing newline
		size_t len = strlen(last_demo);
		if (len > 0 && last_demo[len - 1] == '\n')
			last_demo[len - 1] = '\0';
	}

	fclose(f);
}

/*
==============
CL_StopPlayback

Called when a demo file runs out, or the user starts a game
==============
*/
void CL_StopPlayback (void)
{
	if (!cls.demoplayback)
	{
		CL_ResetDemoSeekState();
		return;
	}

	fclose (cls.demofile);
	cls.demoplayback = false;
	cls.demopaused = false;
	cls.demospeed = 1.f; // woods (iw) #democontrols
	cls.demofile = NULL;
	cls.demofilesize = 0; // woods (iw) #democontrols
	cls.demofilestart = 0; // woods (iw) #democontrols
	cls.state = ca_disconnected;

	VEC_CLEAR(demo_rewind.frames); // woods (iw) #democontrols
	VEC_CLEAR(demo_rewind.frame_events); // woods (iw) #democontrols
	VEC_CLEAR(demo_rewind.pending_sounds); // woods (iw) #democontrols
	demo_rewind.backstop = false; // woods (iw) #democontrols
	CL_DemoRewindFreePrevStatStrings();
	demo_rewind.prev.num_entities = 0;
	CL_ResetDemoSeekState();

	if (cls.demofilename[0]) // woods #lastdemo
	{
		const char* demoname = COM_SkipPath(cls.demofilename);
		if (demoname[0])
		{
			q_strlcpy(last_demo, demoname, sizeof(last_demo));
			Log_Last_Demo_f();
		}
	}
	cls.demofilename[0] = '\0'; // woods (iw) #democontrols

	if (cls.timedemo)
		CL_FinishTimeDemo ();

	initialized = false; // woods (iw) #democontrols
}

/*
====================
CL_WriteDemoMessage

Dumps the current net message, prefixed by the length and view angles
====================
*/
static void CL_WriteDemoMessage (void)
{
	int	len;
	int	i;
	float	f;

	len = LittleLong (net_message.cursize);
	fwrite (&len, 4, 1, cls.demofile);
	for (i = 0; i < 3; i++)
	{
		f = LittleFloat (cl.viewangles[i]);
		fwrite (&f, 4, 1, cls.demofile);
	}
	fwrite (net_message.data, net_message.cursize, 1, cls.demofile);
	fflush (cls.demofile);
}

static int CL_GetDemoMessage (void)
{
	int	r, i;
	float	f;

	if (!cls.demospeed || demo_rewind.backstop) // woods (iw) #democontrols
		return 0;

	// decide if it is time to grab the next message
	if (cls.signon == SIGNONS)	// always grab until fully connected
	{
		if (cls.timedemo)
		{
			if (host_framecount == cls.td_lastframe)
				return 0;	// already read this frame's message
			cls.td_lastframe = host_framecount;
		// if this is the second frame, grab the real td_starttime
		// so the bogus time on the first frame doesn't count
			if (host_framecount == cls.td_startframe + 1)
				cls.td_starttime = realtime;
		}
		else if (/* cl.time > 0 && */ cls.demospeed > 0.f ? cl.time <= cl.mtime[0] : cl.time >= cl.mtime[0]) // woods(iw) #democontrols
		{
			return 0;	// don't need another message yet
		}
	}

// get the next message
	if (!CL_NextDemoFrame()) // woods (iw) #democontrols
		return 0;

	cls.demo_offset_current = ftell(cls.demofile); // woods #demopercent (Baker Fitzquake Mark V)

	if (fread (&net_message.cursize, 4, 1, cls.demofile) != 1) // woods
	{
		CL_StopPlayback();
		return 0;
	}
	VectorCopy (cl.mviewangles[0], cl.mviewangles[1]);
	for (i = 0 ; i < 3 ; i++)
	{
		r = fread (&f, 4, 1, cls.demofile);
		cl.mviewangles[0][i] = LittleFloat (f);
	}

	net_message.cursize = LittleLong (net_message.cursize);
	if (net_message.cursize > MAX_MSGLEN)
		Sys_Error ("Demo message > MAX_MSGLEN");
	r = fread (net_message.data, net_message.cursize, 1, cls.demofile);
	if (r != 1)
	{
		CL_StopPlayback ();
		return 0;
	}

	if (cls.signon == SIGNONS && !initialized) // woods (iw) #democontrols
	{
		CL_ClearDemoFrags();
		CL_ResetDemoSeekState();

		initialized = true; // Prevent reinitialization
	}

	return 1;
}

/*
====================
CL_GetMessage

Handles recording and playback of demos, on top of NET_ code
====================
*/
int CL_GetMessage (void)
{
	int	r;

	if (cls.demoplayback)
		return CL_GetDemoMessage ();

	while (1)
	{
		r = NET_GetMessage (cls.netcon);

		if (r != 1 && r != 2)
			return r;

	// discard nop keepalive message
		if (net_message.cursize == 1 && net_message.data[0] == svc_nop)
		{
			if (cls.download.active) // woods -- silence during dl
				Con_DPrintf ("<-- server to client keepalive\n");
			else
				Con_Printf ("<-- server to client keepalive\n");
		}
		else
			break;
	}

	if (cls.demorecording)
		CL_WriteDemoMessage ();

	return r;
}

static qboolean CL_DemoFilenameExists(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;

	fclose(f);
	return true;
}

static void CL_GetDemoModeTag(char *mode_tag, size_t mode_tag_size)
{
	char mode_buf[32];
	const char *mode;

	if (!mode_tag_size)
		return;
	mode_tag[0] = '\0';

	if (!cl.serverinfo[0])
		return;

	mode = Info_GetKey(cl.serverinfo, "mode", mode_buf, sizeof(mode_buf));
	if (!mode || !mode[0])
		return;

	if (!q_strcasecmp(mode, "ctf"))
		q_strlcpy(mode_tag, "CTF", mode_tag_size);
	else if (!q_strcasecmp(mode, "dm") || !q_strcasecmp(mode, "ffa"))
		q_strlcpy(mode_tag, "DM", mode_tag_size);
	else if (!q_strcasecmp(mode, "ra") || !q_strcasecmp(mode, "rocketarena"))
		q_strlcpy(mode_tag, "RA", mode_tag_size);
	else if (!q_strcasecmp(mode, "ca"))
		q_strlcpy(mode_tag, "CA", mode_tag_size);
	else if (!q_strcasecmp(mode, "airshot"))
		q_strlcpy(mode_tag, "AIRSHOT", mode_tag_size);
	else if (!q_strcasecmp(mode, "wipeout"))
		q_strlcpy(mode_tag, "WIPEOUT", mode_tag_size);
	else if (!q_strcasecmp(mode, "freezetag"))
		q_strlcpy(mode_tag, "FREEZETAG", mode_tag_size);
}

static void CL_RenameDemoWithMatchSuffixes(void)
{
	char base[MAX_OSPATH];
	char renamed[MAX_OSPATH];
	char current_noext[MAX_OSPATH];
	char suffix[32];
	char mode_tag[16];
	size_t current_len;
	size_t suffix_len;
	int attempt;

	if (!cls.demofilename[0])
		return;

	CL_GetDemoModeTag(mode_tag, sizeof(mode_tag));

	suffix[0] = '\0';
	if (mode_tag[0])
		q_snprintf(suffix, sizeof(suffix), "_%s", mode_tag);
	if (cls.demo_had_overtime)
		q_strlcat(suffix, "_OT", sizeof(suffix));

	if (!suffix[0])
		return;

	COM_StripExtension(cls.demofilename, current_noext, sizeof(current_noext));
	current_len = strlen(current_noext);
	suffix_len = strlen(suffix);
	if (current_len > suffix_len && !q_strcasecmp(current_noext + current_len - suffix_len, suffix))
		return;

	COM_StripExtension(cls.demofilename, base, sizeof(base));

	for (attempt = 0; attempt < 1000; ++attempt)
	{
		if (attempt == 0)
			q_snprintf(renamed, sizeof(renamed), "%s%s.dem", base, suffix);
		else
			q_snprintf(renamed, sizeof(renamed), "%s%s%d.dem", base, suffix, attempt + 1);

		if (CL_DemoFilenameExists(renamed))
			continue;

		if (rename(cls.demofilename, renamed) == 0)
		{
			q_strlcpy(cls.demofilename, renamed, sizeof(cls.demofilename));
			Con_Printf("renamed demo to %s\n", COM_SkipPath(renamed));
		}
		else
		{
			Con_Printf("WARNING: could not rename demo to %s\n", COM_SkipPath(renamed));
		}
		return;
	}

	Con_Printf("WARNING: could not find available demo name for %s\n", COM_SkipPath(cls.demofilename));
}


/*
====================
CL_Stop_f

stop recording a demo
====================
*/
void CL_Stop_f (void)
{
	if (cmd_source != src_command)
		return;

	if (!cls.demorecording)
	{
		Con_Printf ("Not recording a demo.\n");
		return;
	}

// write a disconnect message to the demo file
	SZ_Clear (&net_message);
	MSG_WriteByte (&net_message, svc_disconnect);
	CL_WriteDemoMessage ();

// finish up
	fclose (cls.demofile);
	cls.demofile = NULL;
	cls.demorecording = false;

	CL_RenameDemoWithMatchSuffixes();
	cls.demo_had_overtime = false;

	Con_Printf ("completed demo\n");

	Cvar_SetROM(cl_recordingdemo.name, "");
	
// ericw -- update demo tab-completion list
	DemoList_Rebuild ();
}

static void CL_Record_Serverdata(void)
{
	size_t i;
	MSG_WriteByte(&net_message, svc_serverinfo);
	if (cl.protocol_pext2)
	{
		MSG_WriteLong (&net_message, PROTOCOL_FTE_PEXT2);
		MSG_WriteLong (&net_message, cl.protocol_pext2);
	}
	MSG_WriteLong (&net_message, cl.protocol);
	if (cl.protocol == PROTOCOL_RMQ)
		MSG_WriteLong (&net_message, cl.protocolflags);
	if (cl.protocol_pext2 & PEXT2_PREDINFO)
		MSG_WriteString(&net_message, COM_SkipPath(com_gamedir));
	MSG_WriteByte (&net_message, cl.maxclients);
	MSG_WriteByte (&net_message, cl.gametype);
	MSG_WriteString (&net_message, cl.levelname);
	for (i=1; cl.model_precache[i]; i++)
		MSG_WriteString (&net_message, cl.model_precache[i]->name);
	MSG_WriteByte (&net_message, 0);
	for (i=1; cl.sound_precache[i]; i++)	//FIXME: might not send any if nosound is set
		MSG_WriteString (&net_message, cl.sound_precache[i]->name);
	MSG_WriteByte (&net_message, 0);
	//FIXME: cd track (current rather than initial?)
	//FIXME: initial view entity (for clients that don't want to mess up scoreboards)
	MSG_WriteByte (&net_message, svc_signonnum);
	MSG_WriteByte (&net_message, 1);
	CL_WriteDemoMessage();
	SZ_Clear (&net_message);
}

//spins out a baseline(idx>=0) or static entity(idx<0) into net_message
void CL_Record_Prespawn(void)
{
	int idx, i;

	//baselines
	for (idx = 0; idx < cl.num_entities; idx++)
	{
		entity_state_t *state = &cl.entities[idx].baseline;
		if (!memcmp(state, &nullentitystate, sizeof(entity_state_t)))
			continue;	//no need
		MSG_WriteStaticOrBaseLine(&net_message, idx, state, cl.protocol_pext2, cl.protocol, cl.protocolflags);

		if (net_message.cursize > 4096)
		{	//periodically flush so that large maps don't need larger than vanilla limits
			CL_WriteDemoMessage();
			SZ_Clear (&net_message);
		}
	}

	//static ents
	for (idx = 1; idx < cl.num_statics; idx++)
	{
		MSG_WriteStaticOrBaseLine(&net_message, -1, &cl.static_entities[idx].ent->baseline, cl.protocol_pext2, cl.protocol, cl.protocolflags);

		if (net_message.cursize > 4096)
		{	//periodically flush so that large maps don't need larger than vanilla limits
			CL_WriteDemoMessage();
			SZ_Clear (&net_message);
		}
	}

	//static sounds
	for (i = NUM_AMBIENTS; i < total_channels; i++)
	{
		channel_t	*ss = &snd_channels[i];
		sfxcache_t		*sc;

		if (!ss->sfx)
			continue;
		if (ss->entnum || ss->entchannel)
			continue;	//can't have been a static sound
		sc = S_LoadSound(ss->sfx);
		if (!sc || sc->loopstart == -1)
			continue;	//can't have been a (valid) static sound

		for (idx = 1; idx < MAX_SOUNDS && cl.sound_precache[idx]; idx++)
			if (cl.sound_precache[idx] == ss->sfx)
				break;
		if (idx == MAX_SOUNDS)
			continue;	//can't figure out which sound it was

		MSG_WriteByte(&net_message, (idx > 255)?svc_spawnstaticsound2:svc_spawnstaticsound);
		MSG_WriteCoord(&net_message, ss->origin[0], cl.protocolflags);
		MSG_WriteCoord(&net_message, ss->origin[1], cl.protocolflags);
		MSG_WriteCoord(&net_message, ss->origin[2], cl.protocolflags);
		if (idx > 255)
			MSG_WriteShort(&net_message, idx);
		else
			MSG_WriteByte(&net_message, idx);
		MSG_WriteByte(&net_message, ss->master_vol);
		MSG_WriteByte(&net_message, ss->dist_mult*1000*64);

		if (net_message.cursize > 4096)
		{	//periodically flush so that large maps don't need larger than vanilla limits
			CL_WriteDemoMessage();
			SZ_Clear (&net_message);
		}
	}

#ifdef PSET_SCRIPT
	//particleindexes
	for (idx = 0; idx < MAX_PARTICLETYPES; idx++)
	{
		if (!cl.particle_precache[idx].name)
			continue;
		MSG_WriteByte(&net_message, svcdp_precache);
		MSG_WriteShort(&net_message, 0x4000 | idx);
		MSG_WriteString(&net_message, cl.particle_precache[idx].name);

		if (net_message.cursize > 4096)
		{	//periodically flush so that large maps don't need larger than vanilla limits
			CL_WriteDemoMessage();
			SZ_Clear (&net_message);
		}
	}
#endif

	MSG_WriteByte (&net_message, svc_signonnum);
	MSG_WriteByte (&net_message, 2);
	CL_WriteDemoMessage();
	SZ_Clear (&net_message);
}

void CL_Record_Spawn(void)
{
	const char *cmd;
	int i, c, s ,p;

	// player names, colors, and frag counts
	for (i = 0; i < cl.maxclients; i++)
	{
		MSG_WriteByte (&net_message, svc_updatename);
		MSG_WriteByte (&net_message, i);
		MSG_WriteString (&net_message, cl.scores[i].name);
		MSG_WriteByte (&net_message, svc_updatefrags);
		MSG_WriteByte (&net_message, i);
		MSG_WriteShort (&net_message, cl.scores[i].frags);
		MSG_WriteByte (&net_message, svc_updatecolors);
		MSG_WriteByte (&net_message, i);
		c = 0;
		s = 0; p = 0;
		if ((cl.scores[i].shirt.type == 1) && (cl.scores[i].pants.type == 1)) //woods type; //0 for none, 1 for legacy colours, 2 for rgb.
		{
			s = (cl.scores[i].shirt.basic);
			p = (cl.scores[i].pants.basic);
			c = 17 * s + (p - s);
		}
		MSG_WriteByte (&net_message, c);
	}

	// send all current light styles
	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		if (*cl_lightstyle[i].map)
		{
			MSG_WriteByte (&net_message, svc_lightstyle);
			MSG_WriteByte (&net_message, i);
			MSG_WriteString (&net_message, cl_lightstyle[i].map);
		}

		if (net_message.cursize > 4096)
		{	//periodically flush so that large maps don't need larger than vanilla limits
			CL_WriteDemoMessage();
			SZ_Clear (&net_message);
		}
	}

	// what about the current CD track... future consideration.

	//if this mod is using dynamic fog, make sure we start with the right values.
	cmd = Fog_GetFogCommand();
	if (cmd)
	{
		MSG_WriteByte (&net_message, svc_stufftext);
		MSG_WriteString (&net_message, cmd);
	}

	//stats
	for (i = 0; i < MAX_CL_STATS; i++)
	{
		if (!cl.stats[i] && !cl.statsf[i])
			continue;

		if (net_message.cursize > 4096)
		{	//periodically flush so that large maps don't need larger than vanilla limits
			CL_WriteDemoMessage();
			SZ_Clear (&net_message);
		}

		if ((double)cl.stats[i] != cl.statsf[i] && (unsigned int)cl.stats[i] <= 0x00ffffff)
		{	//if the float representation seems to have more precision then use that, unless its getting huge in which case we're probably getting fpu truncation, so go back to more compatible ints
			MSG_WriteByte (&net_message, svcfte_updatestatfloat);
			MSG_WriteByte (&net_message, i);
			MSG_WriteFloat (&net_message, cl.statsf[i]);
		}
		else if (cl.stats[i] >= 0 && cl.stats[i] <= 255 && (cl.protocol_pext2 & PEXT2_PREDINFO))
		{
			MSG_WriteByte (&net_message, svcdp_updatestatbyte);
			MSG_WriteByte (&net_message, i);
			MSG_WriteByte (&net_message, cl.stats[i]);
		}
		else
		{
			MSG_WriteByte (&net_message, svc_updatestat);
			MSG_WriteByte (&net_message, i);
			MSG_WriteLong (&net_message, cl.stats[i]);
		}
	}

	// view entity
	MSG_WriteByte (&net_message, svc_setview);
	MSG_WriteShort (&net_message, cl.viewentity);

	// signon
	MSG_WriteByte (&net_message, svc_signonnum);
	MSG_WriteByte (&net_message, 3);

	CL_WriteDemoMessage();
	SZ_Clear (&net_message);

	//ask the server to reset entity deltas. yes this means playback will wait a couple of frames before it actually starts playing but oh well.
	if (cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
	{
		cl.ackframes_count = 0;
		cl.ackframes[cl.ackframes_count++] = -1;
	}
}

/*
====================
CL_Record_f -- -- woods changed alot: removed cd track, and map selections (just use autodemo) | sourced from Qrack #autodemo

record <demoname> <map> [cd track]
====================
*/
void CL_Record_f (void)
{
	int		c;
	char	name[MAX_OSPATH];
	int		track;

	if (cmd_source != src_command)
		return;

	if (cls.demoplayback)
	{
		Con_Printf ("Can't record during demo playback\n");
		return;
	}

	c = Cmd_Argc();
	if (c > 3)
	{
		Con_Printf("record or record <demoname> [<map>]\n");
		return;
	}

	if (c == 1 || c == 2)
	{
		if (c == 1)
		{
			// woods added time for demo output
			char str[24];
			time_t systime = time(0);
			struct tm loct =*localtime(&systime);

			q_snprintf(name, sizeof(name), "%s/demos", com_gamedir); //  create demos folder if not there
			Sys_mkdir(name); 

			strftime(str, 24, "%m-%d-%Y-%H%M%S", &loct);
			q_snprintf(name, sizeof(name), "%s/demos/%s_%s", com_gamedir, cl.mapname, str);  // woods added demos folder, added args for demo output info
		}
		else if (c == 2)
		{
			if (strstr(Cmd_Argv(1), ".."))
			{
				Con_Printf("Relative pathnames are not allowed.\n");
				return;
			}

			if (c == 2 && cls.state == ca_connected)
			{
#if 0
				Con_Printf("Can not record - already connected to server\nClient demo recording must be started before connecting\n");
				return;
#endif
				if (cls.signon < 2)
				{
					Con_Printf("Can't record - try again when connected\n");
					return;
				}
				switch (cl.protocol)
				{
				case PROTOCOL_NETQUAKE:
				case PROTOCOL_FITZQUAKE:
				case PROTOCOL_RMQ:
				case PROTOCOL_VERSION_BJP3:
					break;
					//case PROTOCOL_VERSION_NEHD:
					//case PROTOCOL_VERSION_DP5:
					//case PROTOCOL_VERSION_DP6:
				case PROTOCOL_VERSION_DP7:
					//case PROTOCOL_VERSION_BJP1:
					//case PROTOCOL_VERSION_BJP2:
				default:
					Con_Printf("Can not record - protocol not supported for recording mid-map\nClient demo recording must be started before connecting\n");
					return;
				}
			}

		}
	}

	if (cls.demorecording)
		CL_Stop_f();

	// write the forced cd track number, or -1
	if (c == 4)
	{
		track = atoi(Cmd_Argv(3));
		Con_Printf("Forcing CD track to %i\n", cls.forcetrack);
	}
	else
	{
		track = -1;
	}

	if (c == 2)
	{
		q_snprintf(name, sizeof(name), "%s/demos", com_gamedir); //  create demos folder if not there
		Sys_mkdir(name);
		q_snprintf(name, sizeof(name), "%s/demos/%s", com_gamedir, Cmd_Argv(1));  // added demos

	}

	// start the map up
	if (c > 2)
	{
		//Cmd_ExecuteString(va("map %s", Cmd_Argv(2)), src_command);
		//if (cls.state != ca_connected)
			//return;

		Con_Printf("enable autodemo to record at map start\n");
		return;

	}

// open the demo file
	COM_AddExtension (name, ".dem", sizeof(name));

	Cvar_SetROM(cl_recordingdemo.name, name);

	Con_Printf ("demo recording\n");
	cls.demofile = fopen (name, "wb");
	if (!cls.demofile)
	{
		Con_Printf ("ERROR: couldn't create %s\n", name);
		Cvar_SetROM(cl_recordingdemo.name, "");
		return;
	}

	cls.forcetrack = track;
	fprintf (cls.demofile, "%i\n", cls.forcetrack);
	q_strlcpy(cls.demofilename, name, sizeof(cls.demofilename)); // woods (iw) #democontrols

	cls.demo_had_overtime = false;
	cls.demorecording = true;

	// from ProQuake: initialize the demo file if we're already connected
	if (c < 3 && cls.state == ca_connected)
	{
		byte *data = net_message.data;
		int cursize = net_message.cursize;
		byte weirdaltbufferthatprobablyisntneeded[NET_MAXMESSAGE];

		net_message.data = weirdaltbufferthatprobablyisntneeded;
		SZ_Clear (&net_message);

		CL_Record_Serverdata();
		CL_Record_Prespawn();
		CL_Record_Spawn();

		// restore net_message
		net_message.data = data;
		net_message.cursize = cursize;
	}
}


/*
====================
CL_PlayDemo_f

play [demoname]
====================
*/
void CL_PlayDemo_f (void)
{
	char	name[MAX_OSPATH], name2[MAX_OSPATH]; // woods #demosfolder

	if (cmd_source != src_command)
		return;

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("\nplaydemo <demoname> : plays a demo\n");
		Con_Printf ("playdemo -l         : plays the most recently played demo\n\n"); // woods #lastdemo
		return;
	}

// disconnect from server
	CL_Disconnect ();

	if (!q_strcasecmp(Cmd_Argv(1), "-l")) // woods #lastdemo
	{
		if (!last_demo[0])
		{
			// Try to load last demo from file if not already loaded
			Load_Last_Demo();
			if (!last_demo[0])
			{
				Con_Printf("no last demo available\n");
				return;
			}
		}
		q_strlcpy(name, last_demo, sizeof(name));
	}
	else
	{
		q_strlcpy(name, Cmd_Argv(1), sizeof(name));
		q_strlcpy(last_demo, name, sizeof(last_demo));
		Log_Last_Demo_f();
	}

	if (!FS_IsCaseSensitive()) // woods #filesystemsens
		q_strlwr (name);

	q_strlcpy(demoplaying, Cmd_Argv(1), sizeof(demoplaying)); // store for window title
	COM_AddExtension (name, ".dem", sizeof(name));

	q_snprintf(name2, sizeof(name2), "demos/%s", name); // woods #demosfolder

	Con_Printf ("Playing demo from %s.\n", name2); // woods #demosfolder

	COM_FOpenFile (name2, &cls.demofile, NULL); // check demos folder

	if (!cls.demofile)
		COM_FOpenFile(name, &cls.demofile, NULL); // check gamedir too

	if (!cls.demofile)
	{
		Con_Printf ("ERROR: couldn't open %s\n", name); // woods #demosfolder
		cls.demonum = -1;	// stop demo loop
		return;
	}

	// woods #demopercent (Baker Fitzquake Mark V)

	strcpy(cls.demoname, name); 
	cls.demo_offset_start = ftell(cls.demofile);	// qfs_lastload.offset instead?
	cls.demo_file_length = com_filesize;
	cls.demo_hosttime_start = cls.demo_hosttime_elapsed = 0; // Fill this in ... host_time;
	cls.demo_cltime_start = cls.demo_cltime_elapsed = 0; // Fill this in

	// end #demopercent (Baker Fitzquake Mark V)
	// 
// ZOID, fscanf is evil
// O.S.: if a space character e.g. 0x20 (' ') follows '\n',
// fscanf skips that byte too and screws up further reads.
//	fscanf (cls.demofile, "%i\n", &cls.forcetrack);
	if (fscanf (cls.demofile, "%i", &cls.forcetrack) != 1 || fgetc (cls.demofile) != '\n')
	{
		fclose (cls.demofile);
		cls.demofile = NULL;
		cls.demonum = -1;	// stop demo loop
		Con_Printf ("ERROR: demo \"%s\" is invalid\n", name);
		return;
	}

	cls.demoplayback = true;
	cls.demopaused = false;
	cls.demospeed = 1.f; // woods (iw) #democontrols
	CL_ResetDemoSeekState();
	// Only change basedemospeed if it hasn't been initialized,
	// otherwise preserve the existing value
	//if (!cls.basedemospeed) // woods (iw) #democontrols
	cls.basedemospeed = 1.f; // woods (iw) #democontrols
	q_strlcpy(cls.demofilename, name, sizeof(cls.demofilename)); // woods (iw) #democontrols
	cls.state = ca_connected;
	cls.demofilestart = ftell(cls.demofile); // woods(iw) #democontrols
	cls.demofilesize = com_filesize; // woods (iw) #democontrols

// get rid of the menu and/or console
	key_dest = key_game;

	memset(cl_dlights, 0, sizeof(cl_dlights)); // woods (iw) #democontrols
}

/*
====================
CL_FinishTimeDemo

====================
*/
static void CL_FinishTimeDemo (void)
{
	int	frames;
	float	time;

	cls.timedemo = false;

// the first frame didn't count
	frames = (host_framecount - cls.td_startframe) - 1;
	time = realtime - cls.td_starttime;
	if (!time)
		time = 1;
	Con_Printf ("%i frames %5.1f seconds %5.1f fps\n", frames, time, frames/time);
}

/*
====================
CL_TimeDemo_f

timedemo [demoname]
====================
*/
void CL_TimeDemo_f (void)
{
	if (cmd_source != src_command)
		return;

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("timedemo <demoname> : gets demo speeds\n");
		return;
	}

	CL_PlayDemo_f ();
	if (!cls.demofile)
		return;

// cls.td_starttime will be grabbed at the second frame of the demo, so
// all the loading time doesn't get counted

	cls.timedemo = true;
	cls.td_startframe = host_framecount;
	cls.td_lastframe = -1;	// get a new message this frame
}

