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

char		demoplaying[MAX_OSPATH]; // woods for window title

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
	unsigned short	datasize;
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
} framevent_t;

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
	}				prev;
}					demo_rewind;

int demo_target_offset = -1; // woods -- target offset for seeking, -1 when not seeking
qboolean is_seeking = false; // woods -- flag to indicate seeking status
static qboolean initialized = false; // woods (iw) #democontrols

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
	int adjust, singleframe, i;

	if (key_dest != key_game)
	{
		cls.demospeed = cls.basedemospeed * !cls.demopaused;
		return;
	}

	const int dynamic_threshold = q_max(100, cls.demo_file_length * 0.03); // 0.1% of the demo file length, with a minimum of 100 for very small files
	const float seeking_speed = 256;

	if (is_seeking) 
	{
			if (cls.demo_offset_current < demo_target_offset)
				cls.demospeed = seeking_speed; // Move forward to target
			else if (cls.demo_offset_current >= demo_target_offset) // move to start vert fast
			{
				cls.demospeed = -1e9f;
				if (cls.basedemospeed)
					cls.demospeed *= cls.basedemospeed;

				for (i = 0; i < cl.maxclients; i++)
					cl.scores[i].frags = 0;
				is_seeking = false; // Stop seeking
				return;
			}

			if (abs(cls.demo_offset_current - demo_target_offset) < dynamic_threshold)
			{
				//cls.demo_offset_current = demo_target_offset; // Align with the target
				if (cls.basedemospeed)
					cls.demospeed *= cls.basedemospeed;
				is_seeking = false; // Stop seeking
			}
		return;
	}

	for (int key = '1'; key <= '9'; ++key)
	{
		if (keydown[key])
		{
			float targetPercentage = (key - '0') / 10.0f;
			demo_target_offset = cls.demo_offset_start + (int)(targetPercentage * (float)((cls.demo_file_length + cls.demo_offset_start) - cls.demo_offset_start)); // sometimes start is not 0
			is_seeking = true;
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
		for (i = 0; i < cl.maxclients; i++)
		{
			cl.scores[i].frags = 0;
			//memset(cl.scores[i].name, 0, sizeof(cl.scores[i].name));
		}
	}
	else if (keydown[K_END])
	{
		cls.demospeed = 1e9f;
		if (cls.basedemospeed)
			cls.demospeed *= cls.basedemospeed;
	}
	else
	{
		cls.demospeed = cls.basedemospeed * !cls.demopaused;
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
		}
		else
		{
			demoframe_t newframe;

			memset(&newframe, 0, sizeof(newframe));
			newframe.fileofs = ftell(cls.demofile);
			newframe.intermission = cl.intermission;
			//	newframe.forceunderwater = cl.forceunderwater;
			VEC_PUSH(demo_rewind.frames, newframe);

			// Take a snapshot of the tracked data at the beginning of this frame
			for (i = 0; i < MAX_LIGHTSTYLES; i++)
				q_strlcpy(demo_rewind.prev.lightstyles[i], cl_lightstyle[i].map, MAX_STYLESTRING);
			memcpy(&demo_rewind.prev.cshift, &cshift_empty, sizeof(cshift_empty));
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
			size_t begin = end - lastframe->datasize;

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
					Sys_Error("CL_NextDemoFrame: bad event type %d", datatype);
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
		CL_SetStati(STAT_VIEWHEIGHT, DEFAULT_VIEWHEIGHT); // woods
		memset(cl_dlights, 0, sizeof(cl_dlights)); // woods
		//memset(cl_temp_entities, 0, sizeof(cl_temp_entities));

		VEC_POP(demo_rewind.frames);
	}
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
		return;

	fclose (cls.demofile);
	cls.demoplayback = false;
	cls.demopaused = false;
	cls.demospeed = 1.f; // woods (iw) #democontrols
	cls.demofile = NULL;
	cls.demofilesize = 0; // woods (iw) #democontrols
	cls.demofilestart = 0; // woods (iw) #democontrols
	cls.demofilename[0] = '\0'; // woods (iw) #democontrols
	cls.state = ca_disconnected;

	VEC_CLEAR(demo_rewind.frames); // woods (iw) #democontrols
	VEC_CLEAR(demo_rewind.frame_events); // woods (iw) #democontrols
	VEC_CLEAR(demo_rewind.pending_sounds); // woods (iw) #democontrols
	demo_rewind.backstop = false; // woods (iw) #democontrols

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

	fread (&net_message.cursize, 4, 1, cls.demofile);
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
		for (i = 0; i < cl.maxclients; i++)
		{
			cl.scores[i].frags = 0;
			//memset(cl.scores[i].name, 0, sizeof(cl.scores[i].name));
		}
		demo_target_offset = -1;

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
			Con_Printf ("<-- server to client keepalive\n");
		else
			break;
	}

	if (cls.demorecording)
		CL_WriteDemoMessage ();

	return r;
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
		Con_Printf ("playdemo <demoname> : plays a demo\n");
		return;
	}

// disconnect from server
	CL_Disconnect ();

// open the demo file
	q_strlcpy (name, Cmd_Argv(1), sizeof(name));

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

