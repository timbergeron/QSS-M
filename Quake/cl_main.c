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
// cl_main.c  -- client main loop

#include "quakedef.h"
#include "bgmusic.h"
#include "pmove.h"

#include "arch_def.h"
#ifdef PLATFORM_UNIX
//for unlink
#include <unistd.h>
#endif

#include <curl/curl.h> // woods #webdl
#include "cfgfile.h" // woods #webdl

// we need to declare some mouse variables here, because the menu system
// references them even when on a unix system.

// these two are not intended to be set directly
cvar_t	cl_name = {"name", "player", CVAR_ARCHIVE | CVAR_USERINFO};
cvar_t	cl_topcolor = {"topcolor", "", CVAR_ARCHIVE | CVAR_USERINFO};
cvar_t	cl_bottomcolor = {"bottomcolor", "", CVAR_ARCHIVE | CVAR_USERINFO};

cvar_t	cl_shownet = {"cl_shownet","0",CVAR_NONE};	// can be 0, 1, or 2
cvar_t	cl_nolerp = {"cl_nolerp","0",CVAR_NONE};
cvar_t	cl_nopred = {"cl_nopred", "0", CVAR_ARCHIVE};	//name comes from quakeworld.

cvar_t	cfg_unbindall = {"cfg_unbindall", "1", CVAR_ARCHIVE};

cvar_t	lookspring = {"lookspring","0", CVAR_ARCHIVE};
cvar_t	lookstrafe = {"lookstrafe","0", CVAR_ARCHIVE};
cvar_t	sensitivity = {"sensitivity","3", CVAR_ARCHIVE};

cvar_t	m_pitch = {"m_pitch","0.022", CVAR_ARCHIVE};
cvar_t	m_yaw = {"m_yaw","0.022", CVAR_ARCHIVE};
cvar_t	m_forward = {"m_forward","1", CVAR_ARCHIVE};
cvar_t	m_side = {"m_side","0.8", CVAR_ARCHIVE};

cvar_t	cl_maxpitch = {"cl_maxpitch", "90", CVAR_ARCHIVE}; //johnfitz -- variable pitch clamping
cvar_t	cl_minpitch = {"cl_minpitch", "-90", CVAR_ARCHIVE}; //johnfitz -- variable pitch clamping

cvar_t cl_recordingdemo = {"cl_recordingdemo", "", CVAR_ROM};	//the name of the currently-recording demo.
cvar_t	cl_demoreel = {"cl_demoreel", "0", CVAR_ARCHIVE};
cvar_t	cl_demospeed = { "cl_demospeed", "1", CVAR_NONE }; // woods #demotools

cvar_t	cl_truelightning = {"cl_truelightning", "0",CVAR_ARCHIVE}; // woods for #truelight
cvar_t	cl_say = {"cl_say","0", CVAR_ARCHIVE}; // woods #ezsay
cvar_t  cl_afk = {"cl_afk", "0", CVAR_ARCHIVE }; // woods #smartafk
cvar_t  cl_idle = {"cl_idle", "0", CVAR_NONE }; // woods #smartafk
cvar_t  cl_rocketlight = {"cl_rocketlight", "0", CVAR_ARCHIVE }; // woods #rocketlight
cvar_t  cl_muzzleflash = {"cl_muzzleflash", "0", CVAR_ARCHIVE}; // woods #muzzleflash
cvar_t  cl_deadbodyfilter = {"cl_deadbodyfilter", "1", CVAR_ARCHIVE}; // woods #deadbody
cvar_t	cl_r2g = {"cl_r2g","0",CVAR_ARCHIVE}; // woods #r2g

cvar_t  w_switch = {"w_switch", "0", CVAR_ARCHIVE | CVAR_USERINFO}; // woods #autoweapon
cvar_t  b_switch = {"b_switch", "0", CVAR_ARCHIVE | CVAR_USERINFO}; // woods #autoweapon
cvar_t  f_status = {"f_status", "on", CVAR_ARCHIVE | CVAR_USERINFO}; // woods #flagstatus

cvar_t  cl_ambient = {"cl_ambient", "1", CVAR_ARCHIVE}; // woods #stopsound
cvar_t  r_coloredpowerupglow = {"r_coloredpowerupglow", "1", CVAR_ARCHIVE}; // woods
cvar_t  cl_bobbing = {"cl_bobbing", "0", CVAR_ARCHIVE}; // woods (joequake #weaponbob)
cvar_t	cl_web_download_url = {"cl_web_download_url", "q1tools.github.io", CVAR_ARCHIVE}; // woods #webdl
cvar_t	cl_web_download_url2 = { "cl_web_download_url2", "maps.quakeworld.nu/all", CVAR_ARCHIVE }; // woods #webdl

client_static_t	cls;
client_state_t	cl;
// FIXME: put these on hunk?
lightstyle_t	cl_lightstyle[MAX_LIGHTSTYLES];
dlight_t		cl_dlights[MAX_DLIGHTS];

int				cl_numvisedicts;
int				cl_maxvisedicts;
entity_t		**cl_visedicts;

extern cvar_t	r_lerpmodels, r_lerpmove; //johnfitz
extern float	host_netinterval;	//Spike

extern cvar_t	allow_download; // woods #ftehack
extern cvar_t	pq_lag; // woods
extern qboolean	qeintermission; // woods #qeintermission

char			lastmphost[NET_NAMELEN]; // woods - connected server address
int				maptime;		// woods connected map time #maptime

void Log_Last_Server_f(void); // woods #connectlast (Qrack) -- write last server to file memory
void Host_ConnectToLastServer_f(void); // woods use #connectlast for smarter reconnect

extern char lastconnected[3]; // woods #identify+
extern qboolean netquakeio; // woods
extern int retry_counter; // woods #ms
extern int grenadecache, rocketcache; // woods #r2g
extern qboolean pausedprint; // woods

void FileList_Add (const char* name, const char* date, filelist_item_t** list); // woods

void CL_ClearTrailStates(void)
{
	int i;
	for (i = 0; i < cl.num_statics; i++)
	{
		PScript_DelinkTrailstate(&(cl.static_entities[i]->trailstate));
		PScript_DelinkTrailstate(&(cl.static_entities[i]->emitstate));
	}
	for (i = 0; i < cl.max_edicts; i++)
	{
		PScript_DelinkTrailstate(&(cl.entities[i].trailstate));
		PScript_DelinkTrailstate(&(cl.entities[i].emitstate));
	}
	for (i = 0; i < MAX_BEAMS; i++)
	{
		PScript_DelinkTrailstate(&(cl_beams[i].trailstate));
	}
}

void CL_FreeState(void)
{
	int i;
	for (i = 0; i < MAX_CL_STATS; i++)
		free(cl.statss[i]);
	CL_ClearTrailStates();
	PR_ClearProgs(&cl.qcvm);
	free(cl.static_entities);
	free(cl.ssqc_to_csqc);
	memset (&cl, 0, sizeof(cl));
}

/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState (void)
{
	if (cl.qcvm.extfuncs.CSQC_Shutdown)
	{
		PR_SwitchQCVM(&cl.qcvm);
		PR_ExecuteProgram(qcvm->extfuncs.CSQC_Shutdown);
		qcvm->extfuncs.CSQC_Shutdown = 0;
		PR_SwitchQCVM(NULL);
	}

	if (!sv.active)
		Host_ClearMemory ();

// wipe the entire cl structure
	CL_FreeState();

	SZ_Clear (&cls.message);

// clear other arrays
	memset (cl_dlights, 0, sizeof(cl_dlights));
	memset (cl_lightstyle, 0, sizeof(cl_lightstyle));
	memset (cl_temp_entities, 0, sizeof(cl_temp_entities));
	memset (cl_beams, 0, sizeof(cl_beams));

	//johnfitz -- cl_entities is now dynamically allocated
	cl.max_edicts = CLAMP (MIN_EDICTS,(int)max_edicts.value,MAX_EDICTS);
	cl.entities = (entity_t *) Hunk_AllocName (cl.max_edicts*sizeof(entity_t), "cl_entities");
	//johnfitz

	//Spike -- this stuff needs to get reset to defaults.
	cl.csqc_sensitivity = 1;

	cl.viewent.netstate = nullentitystate;
#ifdef PSET_SCRIPT
	PScript_Shutdown();
#endif

	if (!sv.active)
		Draw_ReloadTextures(false);
}

/*
=====================
CL_Disconnect

Sends a disconnect message to the server
This is also called on Host_Error, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect (void)
{
	if (key_dest == key_message)
		Key_EndChat ();	// don't get stuck in chat mode

// stop sounds (especially looping!)
	S_StopAllSounds (true);
	BGM_Stop();
	CDAudio_Stop();

// if running a local server, shut it down
	if (cls.demoplayback)
		CL_StopPlayback ();
	else if (cls.state == ca_connected)
	{
		if (cls.demorecording)
			CL_Stop_f ();

		Con_DPrintf ("Sending clc_disconnect\n");
		SZ_Clear (&cls.message);
		MSG_WriteByte (&cls.message, clc_disconnect);
		NET_SendUnreliableMessage (cls.netcon, &cls.message);
		SZ_Clear (&cls.message);
		NET_Close (cls.netcon);
		cls.netcon = NULL;

		cls.state = ca_disconnected;
		if (sv.active)
			Host_ShutdownServer(false);
	}

	cls.demoplayback = cls.timedemo = false;
	cls.demopaused = false;
	cls.signon = 0;
	cls.netcon = NULL;
	if (cls.download.file)
		fclose(cls.download.file);
	memset(&cls.download, 0, sizeof(cls.download));
	cl.intermission = 0;
	cl.worldmodel = NULL;
	cl.sendprespawn = false;
	memset(lastconnected, '\0', sizeof(lastconnected)); // woods #identify+
	cl.matchinp = 0; // woods
	netquakeio = false; // woods

	if (cl.modtype == 1 || cl.modtype == 4)
		Cbuf_AddText("setinfo observing off\n"); // woods
	pausedprint = false;  // woods
}

void CL_Disconnect_f (void)
{
	CL_Disconnect ();
	if (sv.active)
		Host_ShutdownServer (false);
}


/*
=====================
CL_EstablishConnection

Host should be either "local" or a net address to be passed on
=====================
*/
void CL_EstablishConnection (const char *host)
{
	static char lasthost[NET_NAMELEN];

	char addressip[70] = {'\0'}; // woods
	char local_verbose[64]; // woods

	int	numaddresses; // woods
	qhostaddr_t addresses[16]; // woods

	if (cls.state == ca_dedicated)
		return;

	if (cls.demoplayback)
		return;
	if (!host)
	{
		host = lasthost;
		if (!*host)
		{ 
			Host_ConnectToLastServer_f (); // woods use #connectlast for smarter reconnect
			Con_Printf("using server history\n"); // woods verbose connection info
			return;
		}
	}
	else
		q_strlcpy(lasthost, host, sizeof(lasthost));

	CL_Disconnect ();

	numaddresses = NET_ListAddresses(addresses, sizeof(addresses) / sizeof(addresses[0])); // woods

	if (numaddresses && !strstr(addresses[0], "[")) // woods, no [ for ipv6
		snprintf(addressip, sizeof(addressip), " -- %s", addresses[0]);

	if (!strcmp(host, "local") || !strcmp(host, "localhost")) // woods
		sprintf(local_verbose, "%s%s", host, addressip);
	else
		sprintf(local_verbose, "%s", host);

	if (!strstr(lasthost, ":"))
		Con_Printf("connecting to ^m%s:%i\n", local_verbose, net_hostport); // woods include port if not specified
	else
		Con_Printf("connecting to ^m%s\n", local_verbose); // woods verbose connection info

	cls.netcon = NET_Connect (host);
	if (!cls.netcon) // woods -  Baker 3.60 - Rook's Qrack port 26000 notification on failure
	{
		Con_Printf("\nsyntax: connect server:port (port is optional)\n");//r00k added
		if (net_hostport != 26000)
			Con_Printf("\nTry using port 26000\n");//r00k added
		Host_Error("connect failed");
	}
	Con_DPrintf ("CL_EstablishConnection: connected to %s\n", host);

	cls.demonum = -1;			// not in the demo loop now
	cls.state = ca_connected;
	cls.signon = 0;				// need all the signon messages before playing
	MSG_WriteByte (&cls.message, clc_nop);	// NAT Fix from ProQuake

	q_strlcpy(lastmphost, host, sizeof(lastmphost)); // woods - connected server address

	Log_Last_Server_f(); // woods #connectlast (Qrack) -- write last server to file memory
	Write_Log (host, SERVERLIST); // woods write server to log #serverlist
	ServerList_Rebuild(); // woods rebuild tab list live for connect +tab #serverlist
}

void CL_SendInitialUserinfo(void *ctx, const char *key, const char *val)
{
	//if (*key == '*')
	//	return;	//servers don't like that sort of userinfo key

	char* ver; // woods, allow initial only #*ver
	ver = va("%s", ENGINE_NAME_AND_VER); // woods, allow initial only #*ver
	Info_SetKey(cls.userinfo, sizeof(cls.userinfo), "*ver", ver); // woods, allow initial only #*ver

	if (!strcmp(key, "name"))
		return;	//already unconditionally sent earlier.
	MSG_WriteByte (&cls.message, clc_stringcmd);
	MSG_WriteString (&cls.message, va("setinfo \"%s\" \"%s\"\n", key, val));
}

Uint32 exec_connect_cfg (Uint32 interval, void* param) // woods #execdelay
{
	Cbuf_AddText("exec connect.cfg\n"); // exec some configs based on serverinfo, hybrid uses userinfo
	return 0; // only exec once
}

Uint32 exec_ctf_cfg (Uint32 interval, void* param) // woods #execdelay
{
	Cbuf_AddText("exec ctf.cfg\n"); // exec some configs based on serverinfo, hybrid uses userinfo
	return 0; // only exec once
}

Uint32 exec_dm_cfg (Uint32 interval, void* param) // woods #execdelay
{
	Cbuf_AddText("exec dm.cfg\n"); // exec some configs based on serverinfo, hybrid uses userinfo
	return 0; // only exec once
}

/*
=====================
CL_SignonReply

An svc_signonnum has been received, perform a client side setup
=====================
*/
void CL_SignonReply (void)
{
	char 	str[8192];

	Con_DPrintf ("CL_SignonReply: %i\n", cls.signon);

	switch (cls.signon)
	{
	case 1:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va("name \"%s\"\n", cl_name.string));

		cl.sendprespawn = true;
		break;

	case 2:

		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va("color %i %i\n", (int)cl_topcolor.value, (int)cl_bottomcolor.value));

		//if (*cl.serverinfo) // woods, for qe fte compat, fte doesnt send serverinfo in nq emulation?
			Info_Enumerate(cls.userinfo, CL_SendInitialUserinfo, NULL);

		MSG_WriteByte (&cls.message, clc_stringcmd);
		sprintf (str, "spawn %s", cls.spawnparms);
		MSG_WriteString (&cls.message, str);
		break;

	case 3:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "begin");
		Cache_Report ();		// print remaining memory
		break;

	case 4:
		SCR_EndLoadingPlaque ();		// allow normal screen updates

		if (cl.gametype == GAME_DEATHMATCH && cls.state == ca_connected && !cl_ambient.value) // woods for no background sounds #stopsound
			Cmd_ExecuteString("stopsound\n", src_command);
		if ((cl_autodemo.value == 1 || cl_autodemo.value == 4) && !cls.demoplayback && !cls.demorecording)   // woods for #autodemo
			Cmd_ExecuteString("record\n", src_command);
		if (cl_autodemo.value == 3 && !cls.demoplayback && !cls.demorecording && (cl.gametype == GAME_DEATHMATCH && cls.state == ca_connected))   // woods for #autodemo
			Cmd_ExecuteString("record\n", src_command);
		if (VID_HasMouseOrInputFocus())
			key_dest = key_game; // woods exit console on server connect
		maptime = SDL_GetTicks(); // woods connected map time #maptime

		if (registered.value == 0) // woods #pak0only
			Con_Printf("\n^mWarning:^m emulating shareware mode, install pak1.pak assets to enable all client features\n\n");

		qeintermission = false; // woods #qeintermission

		cl.realviewentity = cl.viewentity; // woods -- eyecam reports wrong viewentity, lets record real one

		if (COM_FileExists("connect.cfg", NULL))
			SDL_AddTimer(900, exec_connect_cfg, NULL); // 2 sec delay after connect #execdelay

		const char* val;
		const char* val2;

		char buf[10]; // woods #modtype [crx server check]
		val = Info_GetKey(cl.serverinfo, "mod", buf, sizeof(buf));
		if (q_strcasestr(val, "crx") && val[0] != 'q')
		{
			cl.modtype = 1;
			strncpy(cl.observer, "n", sizeof(cl.observer));
		}

		char buf2[10]; // woods #modtype [FTE server check]
		val = Info_GetKey(cl.serverinfo, "*version", buf2, sizeof(buf2));
		if (strstr(val, "FTE"))
			cl.modtype = 5;

		// woods lets detect the mode of the server for hybrid/nq crx

		char buf3[16];
		val = Info_GetKey(cl.serverinfo, "mode", buf3, sizeof(buf3));

		// woods lets 

		if (!q_strcasecmp(val, "ctf"))
		{
			cl.modetype = 1;
			if (COM_FileExists("ctf.cfg", NULL))
				SDL_AddTimer(1000, exec_ctf_cfg, NULL); // 2 sec delay after connect #execdelay
		}
		if (!strcmp(val, "dm") || !strcmp(val, "ffa"))
		{
			cl.modetype = 2;
			if (COM_FileExists("dm.cfg", NULL))
				SDL_AddTimer(1000, exec_dm_cfg, NULL); // 2 sec delay after connect #execdelay
		}
		if (!q_strcasecmp(val, "ra") || !q_strcasecmp(val, "rocketarena"))
			cl.modetype = 3;
		if (!q_strcasecmp(val, "ca"))
			cl.modetype = 4;
		if (!q_strcasecmp(val, "airshot"))
			cl.modetype = 5;
		if (!q_strcasecmp(val, "wipeout"))
			cl.modetype = 6;
		if (!q_strcasecmp(val, "freezetag"))
			cl.modetype = 7;

		// woods lets detect the playmode of the server for hybrid/nq crx

		char buf4[16];
		val2 = Info_GetKey(cl.serverinfo, "playmode", buf4, sizeof(buf4));

		if (!q_strcasecmp(val2, "match"))
			cl.playmode = 1;
		if (!q_strcasecmp(val2, "ffa") || !q_strcasecmp(val2, "pug") || !q_strcasecmp(val2, "normal"))
			cl.playmode = 2;
		if (!q_strcasecmp(val2, "practice"))
			cl.playmode = 3;

		retry_counter = 0; // woods #ms

		break;
	}
}

/*
=====================
CL_NextDemo

Called to play the next demo in the demo loop
=====================
*/
void CL_NextDemo (void)
{
	char	str[1024];

	if (cls.demonum == -1)
		return;		// don't play demos

	if (!cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS)
	{
		cls.demonum = 0;
		if (!cls.demos[cls.demonum][0])
		{
			Con_Printf ("No demos listed with startdemos\n");
			cls.demonum = -1;
			CL_Disconnect();
			return;
		}
	}

	SCR_BeginLoadingPlaque ();

	sprintf (str,"playdemo %s\n", cls.demos[cls.demonum]);
	Cbuf_InsertText (str);
	cls.demonum++;
}

/*
==============
CL_PrintEntities_f
==============
*/
void CL_PrintEntities_f (void)
{
	entity_t	*ent;
	int			i;

	if (cls.state != ca_connected)
		return;

	for (i=0,ent=cl.entities ; i<cl.num_entities ; i++,ent++)
	{
		Con_Printf ("%3i:",i);
		if (!ent->model)
		{
			Con_Printf ("EMPTY\n");
			continue;
		}
		Con_Printf ("%s:%2i  (%5.1f,%5.1f,%5.1f) [%5.1f %5.1f %5.1f]\n"
		,ent->model->name,ent->frame, ent->origin[0], ent->origin[1], ent->origin[2], ent->angles[0], ent->angles[1], ent->angles[2]);
	}
}

/*
===============
CL_AllocDlight

===============
*/
dlight_t *CL_AllocDlight (int key)
{
	int		i;
	dlight_t	*dl;

// first look for an exact key match
	if (key)
	{
		dl = cl_dlights;
		for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
		{
			if (dl->key == key)
			{
				memset (dl, 0, sizeof(*dl));
				dl->key = key;
				dl->color[0] = dl->color[1] = dl->color[2] = 1; //johnfitz -- lit support via lordhavoc
				return dl;
			}
		}
	}

// then look for anything else
	dl = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (dl->die < cl.time)
		{
			memset (dl, 0, sizeof(*dl));
			dl->key = key;
			dl->color[0] = dl->color[1] = dl->color[2] = 1; //johnfitz -- lit support via lordhavoc
			return dl;
		}
	}

	dl = &cl_dlights[0];
	memset (dl, 0, sizeof(*dl));
	dl->key = key;
	dl->color[0] = dl->color[1] = dl->color[2] = 1; //johnfitz -- lit support via lordhavoc
	return dl;
}


/*
===============
CL_DecayLights

===============
*/
void CL_DecayLights (void)
{
	int			i;
	dlight_t	*dl;
	float		time;

	time = fabs(cl.time - cl.oldtime); // To make sure it stays forward oriented time // woods #demorewind (Baker Fitzquake Mark V)
	//time = cl.time - cl.oldtime;
	if (time < 0)
		return;

	dl = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (dl->die < cl.time || !dl->radius)
			continue;

		dl->radius -= time*dl->decay;
		if (dl->radius < 0)
			dl->radius = 0;
	}
}


/*
===============
CL_LerpPoint

Determines the fraction between the last two messages that the objects
should be put at.
===============
*/
float	CL_LerpPoint (void)
{
	extern qboolean bumper_on; // woods #demorewind (Baker Fitzquake Mark V)
	float	f, frac;

	f = cl.mtime[0] - cl.mtime[1];

	if (!f || cls.timedemo || (sv.active && !host_netinterval))
	{
		cl.time = cl.ctime = cl.mtime[0]; // woods #demorewind (Baker Fitzquake Mark V)
	//	cl.time = cl.mtime[0];
		return 1;
	}

	if (f > 0.1) // dropped packet, or start of demo
	{
		cl.mtime[1] = cl.mtime[0] - 0.1;
		f = 0.1;
	}

	frac = (cl.ctime - cl.mtime[1]) / f;
	//frac = (cl.time - cl.mtime[1]) / f;

	if (frac < 0)
	{
		if (frac < -0.01)
		{
			if (bumper_on) // woods #demorewind (Baker Fitzquake Mark V)
			{
				cl.ctime = cl.mtime[1];
			}
			else cl.time = cl.ctime = cl.mtime[1];
			//cl.time = cl.mtime[1];
		frac = 0;
		}
	}
	else if (frac > 1)
	{
		if (frac > 1.01)
		{
			if (bumper_on) // woods #demorewind (Baker Fitzquake Mark V)
				cl.ctime = cl.mtime[0];
			else cl.time = cl.ctime = cl.mtime[0]; // Here is where we get foobar'd
		//	cl.time = cl.mtime[0];
			frac = 1;
		}
	}

	//johnfitz -- better nolerp behavior
	if (cl_nolerp.value)
		return 1;
	//johnfitz

	return frac;
}

static qboolean CL_LerpEntity(entity_t *ent, vec3_t org, vec3_t ang, float frac)
{
	float f, d;
	int j;
	vec3_t delta;
	qboolean teleported = false;

	if (ent->netstate.pmovetype && ent-cl.entities==cl.viewentity && qcvm->worldmodel && !cl_nopred.value)
	{	//note: V_CalcRefdef will copy from cl.entities[viewent] to get its origin, so doing it here is the proper place anyway.
		static struct
		{
			int seq;
			float waterjumptime;
		} propagate[countof(cl.movecmds)];
		vec3_t bounds[2];

//		memset(&pmove, 0xff, sizeof(pmove));
#ifdef VALGRIND_MAKE_MEM_UNDEFINED
		VALGRIND_MAKE_MEM_UNDEFINED(&pmove, sizeof(pmove));
#endif
		PMCL_SetMoveVars();

		if (ent->netstate.solidsize)
		{
			pmove.player_maxs[0] = pmove.player_maxs[1] = ent->netstate.solidsize & 255;
			pmove.player_mins[0] = pmove.player_mins[1] = -pmove.player_maxs[0];
			pmove.player_mins[2] = -(int)((ent->netstate.solidsize >>8) & 255);
			pmove.player_maxs[2] = (int)((ent->netstate.solidsize>>16) & 65535) - 32768;
		}
		else
		{
			VectorClear(pmove.player_mins);
			VectorClear(pmove.player_maxs);
		}
		pmove.safeorigin_known = false;
		VectorCopy(ent->msg_origins[0], pmove.origin);
		for (j = 0; j < 3; j++)
		{
			pmove.velocity[j] = ent->netstate.velocity[j]*1.0/8;
			bounds[0][j] = pmove.origin[j] + pmove.player_mins[j] - 256;
			bounds[1][j] = pmove.origin[j] + pmove.player_maxs[j] + 256;
		}
		VectorClear(pmove.gravitydir);

		pmove.waterjumptime = 0;//FIXME: needs propagation. (e->v.teleport_time>qcvm->time)?e->v.teleport_time - qcvm->time:0;
		pmove.jump_held = !!(ent->netstate.pmovetype&0x40);
		pmove.onladder = false;//!!(fl&PMF_LADDER);
		pmove.jump_secs = 0;	//has been 0 since Z_EXT_PM_TYPE instead of imposing a delay on rejumps.
		pmove.onground = !!(ent->netstate.pmovetype&0x80); //in case we're using pm_pground

		switch(ent->netstate.pmovetype&63)
		{
		case MOVETYPE_WALK:		pmove.pm_type = PM_NORMAL;		break;
		case MOVETYPE_TOSS:		//pmove.pm_type = PM_DEAD;		break;
		case MOVETYPE_BOUNCE:	pmove.pm_type = PM_DEAD;		break;
		case MOVETYPE_FLY:		pmove.pm_type = PM_FLY;			break;
		case MOVETYPE_NOCLIP:	pmove.pm_type = PM_SPECTATOR;	break;

		case MOVETYPE_NONE:
		case MOVETYPE_STEP:
		case MOVETYPE_PUSH:
		case MOVETYPE_FLYMISSILE:
		case MOVETYPE_EXT_BOUNCEMISSILE:
		case MOVETYPE_EXT_FOLLOW:
		default:				pmove.pm_type = PM_NONE;		break;
		}

		pmove.skipent = -(int)(ent-cl.entities);
		World_AddEntsToPmove(NULL, bounds);

		j = cl.ackedmovemessages+1;
		if (j < cl.movemessages-countof(cl.movecmds))
			j = cl.movemessages-countof(cl.movecmds);	//don't corrupt things, lost is lost.

		if (propagate[j%countof(cl.movecmds)].seq == j)
		{	//some things can only be known thanks to propagation.
			pmove.waterjumptime = propagate[j%countof(cl.movecmds)].waterjumptime;
		}
//		else	 Con_Printf("propagation not available\n");	//just do without

		for (; j < cl.movemessages; j++)
		{
			pmove.cmd = cl.movecmds[j%countof(cl.movecmds)];
			PM_PlayerMove(1);

			propagate[(j+1)%countof(cl.movecmds)].seq = j+1;
			propagate[(j+1)%countof(cl.movecmds)].waterjumptime = pmove.waterjumptime;
		}

		//and run the partial too, to keep things smooth
		pmove.cmd = cl.pendingcmd;
		PM_PlayerMove(1);

		VectorCopy (pmove.origin, org);
		VectorCopy (pmove.cmd.viewangles, ang);
		ang[0] *= -1.0/3;	//FIXME: STUPID STUPID BUG

		//for bob+calcrefdef stuff, mostly.
		VectorCopy (pmove.velocity, cl.velocity);
		cl.onground = pmove.onground;
		cl.inwater = pmove.waterlevel>=2;

		//FIXME: add stair-smoothing support
		//FIXME: add error correction

		return true;	//if we're predicting, don't let its old position linger as interpolation. should be less laggy that way, or something.
	}

	//figure out the pos+angles of the parent
	if (ent->forcelink)
	{	// the entity was not updated in the last message
		// so move to the final spot
		VectorCopy (ent->msg_origins[0], org);
		VectorCopy (ent->msg_angles[0], ang);
	}
	else
	{	// if the delta is large, assume a teleport and don't lerp
		f = frac;
		for (j=0 ; j<3 ; j++)
		{
			delta[j] = ent->msg_origins[0][j] - ent->msg_origins[1][j];
			if (delta[j] > 100 || delta[j] < -100)
			{
				f = 1;		// assume a teleportation, not a motion
				teleported = true;	//johnfitz -- don't lerp teleports
			}
		}

		//johnfitz -- don't cl_lerp entities that will be r_lerped
		if (r_lerpmove.value && (ent->lerpflags & LERP_MOVESTEP))
			f = 1;
		//johnfitz

	// interpolate the origin and angles
		for (j=0 ; j<3 ; j++)
		{
			org[j] = ent->msg_origins[1][j] + f*delta[j];

			d = ent->msg_angles[0][j] - ent->msg_angles[1][j];
			if (d > 180)
				d -= 360;
			else if (d < -180)
				d += 360;
			ang[j] = ent->msg_angles[1][j] + f*d;
		}
	}
	return teleported;
}

static qboolean CL_AttachEntity(entity_t *ent, float frac)
{
	entity_t *parent;
	vec3_t porg, pang;
	vec3_t paxis[3];
	vec3_t tmp, fwd, up;
	unsigned int tagent = ent->netstate.tagentity;
	int runaway = 0;

	while(1)
	{
		if (!tagent)
			return true;	//nothing to do.
		if (runaway++==10 || tagent >= (unsigned int)cl.num_entities)
			return false;	//parent isn't valid
		parent = &cl.entities[tagent];

		if (tagent == cl.viewentity)
			ent->eflags |= EFLAGS_EXTERIORMODEL;

		if (!parent->model)
			return false;
		if (0)//tagent < ent-cl_entities)
		{
			tagent = parent->netstate.tagentity;
			VectorCopy(parent->origin, porg);
			VectorCopy(parent->angles, pang);
		}
		else
		{
			tagent = parent->netstate.tagentity;
			CL_LerpEntity(parent, porg, pang, frac);
		}

		//FIXME: this code needs to know the exact lerp info of the underlaying model.
		//however for some idiotic reason, someone decided to figure out what should be displayed somewhere far removed from the code that deals with timing
		//so we have absolutely no way to get a reliable origin
		//in the meantime, r_lerpmove 0; r_lerpmodels 0
		//you might be able to work around it by setting the attached entity to movetype_step to match the attachee, and to avoid EF_MUZZLEFLASH.
		//personally I'm just going to call it a quakespasm bug that I cba to fix.

		//FIXME: update porg+pang according to the tag index (we don't support md3s/iqms, so we don't need to do anything here yet)

		if (parent->model && parent->model->type == mod_alias)
			pang[0] *= -1;
		AngleVectors(pang, paxis[0], paxis[1], paxis[2]);

		if (ent->model && ent->model->type == mod_alias)
			ent->angles[0] *= -1;
		AngleVectors(ent->angles, fwd, tmp, up);

		//transform the origin
		VectorMA(parent->origin, ent->origin[0], paxis[0], tmp);
		VectorMA(tmp, -ent->origin[1], paxis[1], tmp);
		VectorMA(tmp, ent->origin[2], paxis[2], ent->origin);

		//transform the forward vector
		VectorMA(vec3_origin, fwd[0], paxis[0], tmp);
		VectorMA(tmp, -fwd[1], paxis[1], tmp);
		VectorMA(tmp, fwd[2], paxis[2], fwd);
		//transform the up vector
		VectorMA(vec3_origin, up[0], paxis[0], tmp);
		VectorMA(tmp, -up[1], paxis[1], tmp);
		VectorMA(tmp, up[2], paxis[2], up);
		//regenerate the new angles.
		VectorAngles(fwd, up, ent->angles);
		if (ent->model && ent->model->type == mod_alias)
			ent->angles[0] *= -1;

		ent->eflags |= parent->netstate.eflags & (EFLAGS_VIEWMODEL|EFLAGS_EXTERIORMODEL);
	}
}

/*
===============
CL_RocketTrail - woods (ironwail) #pemission
Rate-limiting wrapper over R_RocketTrail
===============
*/
static void CL_RocketTrail(entity_t* ent, int type)
{
	if (!(ent->lerpflags & LERP_RESETMOVE) && !ent->forcelink)
	{
		ent->traildelay -= cl.time - cl.oldtime;
		if (ent->traildelay > 0.f)
			return;
		R_RocketTrail(ent->trailorg, ent->origin, type);
	}

	ent->traildelay = 1.f / 72.f;
	VectorCopy(ent->origin, ent->trailorg);
}

/*
===============
CL_RelinkEntities
===============
*/
void CL_RelinkEntities (void)
{
	entity_t	*ent;
	int			i, j;
	float		frac, d;
	float		bobjrotate;
	vec3_t		oldorg;
	dlight_t	*dl;
	float		frametime;
	int			modelflags;
	qmodel_t	*model; // woods #r2g

// determine partial update time
	frac = CL_LerpPoint ();

	frametime = cl.time - cl.oldtime;
	if (frametime < 0)
		frametime = 0;
	if (frametime > 0.1)
		frametime = 0.1;

	if (cl_numvisedicts + 64 > cl_maxvisedicts)
	{
		cl_maxvisedicts = cl_maxvisedicts+64;
		cl_visedicts = realloc(cl_visedicts, sizeof(*cl_visedicts)*cl_maxvisedicts);
	}
	cl_numvisedicts = 0;

//
// interpolate player info
//
	for (i=0 ; i<3 ; i++)
		cl.velocity[i] = cl.mvelocity[1][i] +
			frac * (cl.mvelocity[0][i] - cl.mvelocity[1][i]);

	SCR_UpdateZoom(); // woods #zoom (ironwail)

	if ((cls.demoplayback || (last_angle_time > host_time && !(in_attack.state & 3)))) // woods JPG - check for last_angle_time for smooth chasecam!  #smoothcam
	{
	// interpolate the angles
		for (j=0 ; j<3 ; j++)
		{
			d = cl.mviewangles[0][j] - cl.mviewangles[1][j];
			if (d > 180)
				d -= 360;
			else if (d < -180)
				d += 360;
			// JPG - I can't set cl.viewangles anymore since that messes up the demorecording.  So instead, #smoothcam
			// I'll set lerpangles (new variable), and view.c will use that instead.
			cl.lerpangles[j] = cl.mviewangles[1][j] + frac*d; // #smoothcam
		}
	}
	else
		VectorCopy(cl.viewangles, cl.lerpangles);

	bobjrotate = anglemod(100*cl.time);

// start on the entity after the world
	for (i=1,ent=cl.entities+1 ; i<cl.num_entities ; i++,ent++)
	{
		if (!ent->model)
		{	// empty slot, ish.
			
			// ericw -- efrags are only used for static entities in GLQuake
			// ent can't be static, so this is a no-op.
			//if (ent->forcelink)
			//	R_RemoveEfrags (ent);	// just became empty
			continue;
		}
		ent->eflags = ent->netstate.eflags;

// if the object wasn't included in the last packet, remove it
		if (ent->msgtime != cl.mtime[0])
		{
			ent->model = NULL;
			ent->lerpflags |= LERP_RESETMOVE|LERP_RESETANIM; //johnfitz -- next time this entity slot is reused, the lerp will need to be reset
			continue;
		}

		VectorCopy (ent->origin, oldorg);

		if (CL_LerpEntity(ent, ent->origin, ent->angles, frac))
			ent->lerpflags |= LERP_RESETMOVE;

		if (ent->netstate.tagentity)
		if (!CL_AttachEntity(ent, frac))
		{
			//can't draw it if we don't know where its parent is.
			continue;
		}

		modelflags = (ent->effects>>24)&0xff;
		if (!(ent->effects & EF_NOMODELFLAGS))
			modelflags |= ent->model->flags;

// rotate binary objects locally
		if (modelflags & EF_ROTATE)
		{ 
			ent->angles[1] = bobjrotate;
			if (cl_bobbing.value) // woods (joequake #weaponbob)
				ent->origin[2] += sin(bobjrotate / 90 * M_PI) * 5 + 5;
		}

		if (ent->effects & EF_BRIGHTFIELD) // woods add ef_brightfield support
			if (PScript_RunParticleEffectTypeString(oldorg, NULL, frametime, "EF_BRIGHTFIELD"))
				R_EntityParticles (ent); // R_EntityParticles aka Classic_BrightField

		if (ent->effects & EF_MUZZLEFLASH)
		{
			if (cl_muzzleflash.value) // woods #muzzleflash
			{ 
				vec3_t		fv, rv, uv;

				dl = CL_AllocDlight (i);
				VectorCopy (ent->origin,  dl->origin);
				dl->origin[2] += 16;
				AngleVectors (ent->angles, fv, rv, uv);

				VectorMA (dl->origin, 18, fv, dl->origin);
				dl->radius = 200 + (rand() & 31);
				dl->minlight = 32;
				dl->die = cl.time + 0.1;
			}

			//johnfitz -- assume muzzle flash accompanied by muzzle flare, which looks bad when lerped
			if (r_lerpmodels.value < 2) // woods #lerp3
			{
				if (ent == &cl.entities[cl.viewentity])
					cl.viewent.lerpflags |= LERP_RESETANIM|LERP_RESETANIM2; //no lerping for two frames
				else
					ent->lerpflags |= LERP_RESETANIM|LERP_RESETANIM2; //no lerping for two frames
			}

			if (r_lerpmodels.value == 3) // woods #lerp3 adjusted lerping for smoother non overlapped frames
			{
				if (cl.viewent.model)

					// allow lerpmodels 2 on these, but we're gonna skip frame 1
					if (strcmp(cl.viewent.model->name, "progs/v_shot2.mdl") // ssg
						&& strcmp(cl.viewent.model->name, "progs/v_nail2.mdl")  // sng
						&& strcmp(cl.viewent.model->name, "progs/v_rock2.mdl")  // rl
						&& strcmp(cl.viewent.model->name, "progs/v_light.mdl")) // lg
					{
						if (ent == &cl.entities[cl.viewentity])
							cl.viewent.lerpflags |= LERP_RESETANIM | LERP_RESETANIM2; //no lerping for two frames
						else
							ent->lerpflags |= LERP_RESETANIM | LERP_RESETANIM2; //no lerping for two frames
					}

				if (cl.viewent.frame < 1)
					cl.viewent.lerpflags |= LERP_RESETANIM;
			}
			//johnfitz
		}

		// woods deadbodyfilter default #deadbody

		if (((ent->model->type == mod_alias) && cl.gametype == GAME_DEATHMATCH) && cl_deadbodyfilter.value)
			if (ent->frame == 49 || ent->frame == 60 || ent->frame == 69 || ent->frame == 84 || ent->frame == 93 || ent->frame == 102)
				continue;

		if (cl_r2g.value && (ent->netstate.modelindex == rocketcache) && rocketcache != 1 && grenadecache != 1) // woods #r2g
		{
			if (grenadecache >= 0 && grenadecache < sizeof(cl.model_precache) / sizeof(cl.model_precache[0]) && cl.model_precache[grenadecache])
			{
				model = cl.model_precache[grenadecache];
				cl.model_precache[grenadecache]->fromrl = 1;
				ent->model = model;
			}
		}
		else
		{
			if (grenadecache >= 0 && grenadecache < sizeof(cl.model_precache) / sizeof(cl.model_precache[0]) && cl.model_precache[grenadecache])
			{
				cl.model_precache[grenadecache]->fromrl = 0;
			}
		}

		if (ent->effects & EF_BRIGHTLIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->origin[2] += 16;
			dl->radius = 416;// +(rand() & 31); // woods no light flicker
			dl->die = cl.time + 0.1; //R00k was .001
		}
		if (ent->effects & (EF_DIMLIGHT|EF_RED|EF_BLUE|EF_GREEN))
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->radius = 216;// +(rand() & 31); // woods no light flicker
			dl->die = cl.time + 0.1; //R00k was .001

			if (((ent->effects & (EF_RED|EF_BLUE|EF_GREEN)) && r_coloredpowerupglow.value)) // woods
			{
				dl->color[0] = !!(ent->effects&EF_RED);
				dl->color[1] = !!(ent->effects&EF_GREEN);
				dl->color[2] = !!(ent->effects&EF_BLUE);
			}
		}

#ifdef PSET_SCRIPT
		if (cl.paused)
			;
		else if (ent->netstate.traileffectnum > 0 && ent->netstate.traileffectnum < MAX_PARTICLETYPES)
		{
			vec3_t axis[3];
			AngleVectors(ent->angles, axis[0], axis[1], axis[2]);
			PScript_ParticleTrail(oldorg, ent->origin, cl.particle_precache[ent->netstate.traileffectnum].index, frametime, i, axis, &ent->trailstate);
		}
		else if (ent->model->traileffect >= 0)
		{
			vec3_t axis[3];
			AngleVectors(ent->angles, axis[0], axis[1], axis[2]);
			PScript_ParticleTrail(oldorg, ent->origin, ent->model->traileffect, frametime, i, axis, &ent->trailstate);
		}
		else
#endif
			if (modelflags & EF_GIB)
		{
			if (PScript_EntParticleTrail(oldorg, ent, "TR_BLOOD"))
				CL_RocketTrail(ent, 2); // woods(ironwail) #pemission
		}
		else if (modelflags & EF_ZOMGIB)
		{
			if (PScript_EntParticleTrail(oldorg, ent, "TR_SLIGHTBLOOD"))
				CL_RocketTrail(ent, 4); // woods(ironwail) #pemission
		}
		else if (modelflags & EF_TRACER)
		{
			if (PScript_EntParticleTrail(oldorg, ent, "TR_WIZSPIKE"))
				CL_RocketTrail(ent, 3); // woods(ironwail) #pemission
		}
		else if (modelflags & EF_TRACER2)
		{
			if (PScript_EntParticleTrail(oldorg, ent, "TR_KNIGHTSPIKE"))
				CL_RocketTrail(ent, 5); // woods(ironwail) #pemission
		}
		else if (modelflags & EF_ROCKET)
		{
			if (PScript_EntParticleTrail(oldorg, ent, "TR_ROCKET"))
				CL_RocketTrail(ent, 0); // woods(ironwail) #pemission
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			if (!cl_rocketlight.value)
				dl->radius = 0;		// woods eliminate rocket light #rocketlight
			else
				dl->radius = 200;
			dl->die = cl.time + 0.01;
		}
		else if (modelflags & EF_GRENADE)
		{
			if (PScript_EntParticleTrail(oldorg, ent, "TR_GRENADE"))
				CL_RocketTrail(ent, 1); // woods(ironwail) #pemission
		}
		else if (modelflags & EF_TRACER3)
		{
			if (PScript_EntParticleTrail(oldorg, ent, "TR_VORESPIKE"))
				CL_RocketTrail(ent, 6); // woods(ironwail) #pemission
		}

		ent->forcelink = false;

#ifdef PSET_SCRIPT
		if (ent->netstate.emiteffectnum > 0)
		{
			vec3_t axis[3];
			AngleVectors(ent->angles, axis[0], axis[1], axis[2]);
			if (ent->model->type == mod_alias)
				axis[0][2] *= -1;	//stupid vanilla bug
			PScript_RunParticleEffectState(ent->origin, axis[0], frametime, cl.particle_precache[ent->netstate.emiteffectnum].index, &ent->emitstate);
		}
		else if (ent->model->emiteffect >= 0)
		{
			vec3_t axis[3];
			AngleVectors(ent->angles, axis[0], axis[1], axis[2]);
			if (ent->model->flags & MOD_EMITFORWARDS)
			{
				if (ent->model->type == mod_alias)
					axis[0][2] *= -1;	//stupid vanilla bug
			}
			else
				VectorScale(axis[2], -1, axis[0]);
			PScript_RunParticleEffectState(ent->origin, axis[0], frametime, ent->model->emiteffect, &ent->emitstate);
			if (ent->model->flags & MOD_EMITREPLACE)
				continue;
		}
#endif

		if (i == cl.viewentity && !chase_active.value)
			continue;

		if (cl_numvisedicts < cl_maxvisedicts)
		{
			cl_visedicts[cl_numvisedicts] = ent;
			cl_numvisedicts++;
		}
	}


	// viewmodel. last, for transparency reasons.
	ent = &cl.viewent;
	if (r_drawviewmodel.value
		&& !chase_active.value
		&& cl.stats[STAT_HEALTH] > 0
		/* && !(cl.items & IT_INVISIBILITY)*/ // woods #ringalpha
		&& ent->model
		&& scr_viewsize.value < 130) // woods
	{
		if (cl_numvisedicts < cl_maxvisedicts)
		{
			cl_visedicts[cl_numvisedicts] = ent;
			cl_numvisedicts++;
		}
	}
}

#ifdef PSET_SCRIPT
int CL_GenerateRandomParticlePrecache(const char *pname)
{	//for dpp7 compat
	size_t i;
	pname = va("%s", pname);
	for (i = 1; i < MAX_PARTICLETYPES; i++)
	{
		if (!cl.particle_precache[i].name)
		{
			cl.particle_precache[i].name = strcpy(Hunk_Alloc(strlen(pname)+1), pname);
			cl.particle_precache[i].index = PScript_FindParticleType(cl.particle_precache[i].name);
			return i;
		}
		if (!strcmp(cl.particle_precache[i].name, pname))
			return i;
	}
	return 0;
}
#endif

/*
=============================================
Libcurl Web/HTTP Downloads -- woods #webdl

- Implements faster, libcurl-based file downloading.
- Downloads game assets from a map repository, not directly from the server.
- Enhances download speeds and reduces server load.

Usage: Activated during map loading for external resource downloads.
Note: Ensure correct configuration of the map repository URL.

=============================================
*/

#define MAX_URLPATH 200
#define BYTES_TO_KB(bytes) ((bytes) / 1024.0f)
#define BYTES_TO_MB(bytes) ((bytes) / (1024.0 * 1024.0))

typedef struct 
{
	char filename[MAX_OSPATH];
	char url[MAX_URLPATH];
} DownloadData;

qboolean web2check = false;
qboolean webcheck = false;
qboolean stop_curl_download = false;
qboolean curl_download_active = false;

typedef struct {
	char* url;
	int web;
} ThreadData;

SDL_Thread* currentWebCheckThread = NULL;
SDL_Thread* currentWeb2CheckThread = NULL;

int checkWebsite (void* ptr)  // ping the potential websites in advance
{
	ThreadData* data = (ThreadData*)ptr;

	if (data == NULL || data->url == NULL)
	{
		free(data);
		return -1;
	}

	CURL* curl = curl_easy_init();
	if (curl == NULL) {
		free(data->url);
		free(data);
		return -1;
	}

	curl_easy_setopt(curl, CURLOPT_URL, data->url);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1); // HEAD request
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);

	CURLcode res = curl_easy_perform(curl);
	if (res == CURLE_OK)
	{
		switch (data->web)
		{
		case 1:
			webcheck = true;
			break;
		case 2:
			web2check = true;
			break;
		}
	}
	else
	{
		Con_DPrintf("cl_web_download_url %s is not responsive", data->url);

		switch (data->web)
		{
		case 1:
			webcheck = false;
			break;
		case 2:
			web2check = false;
			break;
		}
	}

	curl_easy_cleanup(curl);

	free(data->url);
	free(data);

	return 0;
}

SDL_Thread* webDownloadCheck (const char* url, int webId)
{
	SDL_Thread* thread;
	ThreadData* data = (ThreadData*)malloc(sizeof(ThreadData));

	if (data == NULL)
	{
		Con_DPrintf("Error: Memory allocation failure in webDownloadCheck\n");
		return NULL;
	}

	data->url = strdup(url);
	if (data->url == NULL)
	{
		Con_DPrintf("Error: URL duplication failure in webDownloadCheck\n");
		free(data);
		return NULL;
	}

	data->web = webId;

	thread = SDL_CreateThread(checkWebsite, "CheckWebsiteThread", (void*)data);
	if (thread == NULL) 
	{
		Con_DPrintf("Error: SDL_CreateThread failed in webDownloadCheck\n");
		free(data->url);
		free(data);
		return NULL;
	}

	Con_DPrintf("CheckWebsiteThread created in webDownloadCheck\n");

	return thread;
}

void WebCheckCallback_f (cvar_t* var)
{
	if (currentWebCheckThread != NULL)
	{
		SDL_WaitThread(currentWebCheckThread, NULL); // Wait for the current thread to finish
		currentWebCheckThread = NULL; // Reset the pointer
	}

	if (cl_web_download_url.string != NULL && cl_web_download_url.string[0] != '\0')
	{
		currentWebCheckThread = webDownloadCheck(cl_web_download_url.string, 1);
	}
}

void Web2CheckCallback_f (cvar_t* var)
{
	if (currentWeb2CheckThread != NULL) 	// Wait for the current thread to finish, if it exists
	{
		SDL_WaitThread(currentWeb2CheckThread, NULL); // Wait for the current thread to complete
		currentWeb2CheckThread = NULL; // Reset the pointer
	}

	if (cl_web_download_url2.string != NULL && cl_web_download_url2.string[0] != '\0')
	{
		currentWeb2CheckThread = webDownloadCheck(cl_web_download_url2.string, 2);
	}
}

void WebCheckInit (void) // runs at launch in CL_Init if default values
{
	char* webearly = NULL;
	char* web2early = NULL;

	if (CFG_OpenConfig("config.cfg") == 0) // get these early config values
	{
		webearly = CFG_ReadCvarValue("cl_web_download_url");
		web2early = CFG_ReadCvarValue("cl_web_download_url2");
		CFG_CloseConfig();
	}

	if (webearly != NULL && webearly[0] != '\0')
		if (!strcmp(webearly, cl_web_download_url.default_string))
			WebCheckCallback_f(&cl_web_download_url);
	if (web2early != NULL && web2early[0] != '\0')
		if (!strcmp(web2early, cl_web_download_url2.default_string))
			Web2CheckCallback_f(&cl_web_download_url2);

	if (webearly != NULL)
	{
		free(webearly);
		webearly = NULL;
	}

	if (web2early != NULL)
	{
		free(web2early);
		web2early = NULL;
	}
}

size_t Write_Data (void* ptr, size_t size, size_t nmemb, FILE* stream)
{
	return fwrite (ptr, size, nmemb, stream);
}

int Progress_Callback (void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	if (stop_curl_download)
		return 1;

	DownloadData* dataFromCurl = (DownloadData*)clientp;

	if (dataFromCurl == NULL)
		return 0;

	static int dotCount = 0;
	static int callbackInvocationCount = 0;
	const int callbackThreshold = 300; // Adjust this value to control the speed


	if (dlnow > 10000 && strcmp(COM_FileGetExtension(dataFromCurl->filename), "loc"))
	{
		if (dltotal == 0)
		{
			if (callbackInvocationCount >= callbackThreshold) // Check if threshold is reached
			{
				char dots[32];
				int numDots = dotCount % (sizeof(dots) - 1);
				memset(dots, '.', numDots);
				dots[numDots] = '\0';

				Con_Printf("DL %s %s\r", COM_SkipPath(dataFromCurl->filename), dots);

				dotCount++;
				callbackInvocationCount = 0;
			}
			else
			{
				callbackInvocationCount++;
			}
		}
		else
		{
			int progress = 0; // Initialize progress to 0%

			if (dlnow > 0 && dltotal > 0)
				progress = (int)((double)dlnow / (double)dltotal * 100.0);

			char urlLimited[21];
			Q_strncpy(urlLimited, dataFromCurl->url, 20);
			urlLimited[20] = '\0';

			char sizeStr[32];

			float dltotalKB = BYTES_TO_KB(dltotal);
			float dltotalMB = BYTES_TO_MB(dltotal);

			if (dltotalMB >= 1.0f)
				q_snprintf(sizeStr, sizeof(sizeStr), "%.2f mb", dltotalMB);
			else if (dltotalKB >= 1.0f)
				q_snprintf(sizeStr, sizeof(sizeStr), "%ld kb", (long)dltotalKB);
			else
				q_snprintf(sizeStr, sizeof(sizeStr), "%ld bytes", (long)dltotal);

			Con_Printf("DL %s (%s) %s ^m%d%%\r",
				COM_SkipPath(dataFromCurl->filename),
				urlLimited,
				sizeStr,
				progress);

			if (scr_disabled_for_loading != true)
			{
				static double now, oldtime, newtime;
				newtime = Sys_DoubleTime();
				now = newtime - oldtime;
				Host_Frame(now);
				oldtime = newtime;
			}
		}
	}
	return 0;
}

qboolean Curl_DownloadFile (const char* url, const char* filename, const char* local_path) // main curl function
{
	stop_curl_download = false;
	cls.download.active = true;
	curl_download_active = true;

	if (url == NULL || url[0] == '\0')
		return false;

	char full_url[MAX_URLPATH];
	const char* skipped_path = COM_SkipPath(filename);

	if (strstr(url, "github.io") && strstr(filename, "maps")) // special case for github.io
	{
		char directory[5];

		if (isdigit((unsigned char)skipped_path[0]))
		{
			strcpy(directory, "0-9/"); // If the filename starts with a digit, use '#' directory
		}
		else {
			directory[0] = toupper(skipped_path[0]); // Extract the first letter and make it uppercase
			directory[1] = '/';
			directory[2] = '\0';
		}

		q_snprintf(full_url, sizeof(full_url), "https://%s/maps/%s/%s", url, directory, skipped_path); // Construct the URL with directory
	}

	else if (strstr(url, "maps.quakeworld.nu")) // special case for maps.quakeworld.nu
	{
		
		q_snprintf(full_url, sizeof(full_url), "https://%s/%s", url, skipped_path); // use secure https and skip path
	}
	else 
		q_snprintf(full_url, sizeof(full_url), "https://%s/%s", url, filename); // use secure https

	DownloadData dl_data;
	memset(&dl_data, 0, sizeof(dl_data)); // Reset dl_data
	Q_strncpy(dl_data.filename, filename, MAX_OSPATH);
	Q_strncpy(dl_data.url, url, MAX_URLPATH); // the server set in cl_web_download_url
	q_strlcpy(cls.download.current, filename, sizeof(cls.download.current));

	CURL* curl = curl_easy_init();
	if (!curl)
		return false;

	char tmp_path[MAX_OSPATH];
	q_snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", local_path);

	COM_CreatePath(tmp_path);

	FILE* fp = fopen(tmp_path, "wb");
	if (!fp) 
	{
		curl_easy_cleanup(curl);
		return false;
	}

	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &dl_data); // Pass the struct to the callback
	curl_easy_setopt(curl, CURLOPT_URL, full_url); // Use full_url here
	curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 1024L); // Use a smaller buffer size for more frequent progress updates
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Write_Data);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Progress_Callback);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 0L); // Timeout after x milliseconds, 1000 = 1 sec, 0L - not limit
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 500L); // Set minimum bytes per second (e.g., 500 bytes/sec)
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L); // Set time in seconds (e.g., 10 seconds)

	CURLcode res = curl_easy_perform(curl);
	long response_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

	// Get file size in bytes
	fseek(fp, 0, SEEK_END); // Seek to end of file
	long fileSizeBytes = ftell(fp); // Get current file pointer position, which is the size
	float fileSizeKB = BYTES_TO_KB(fileSizeBytes);
	float fileSizeMB = BYTES_TO_MB(fileSizeBytes);

	fclose(fp);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK || response_code != 200) 
	{
		unlink(tmp_path); // Delete the temporary file in case of an error
		cls.download.active = false;
		curl_download_active = false;

		if (res != CURLE_OK) 
			Con_DPrintf("Error downloading file: CURL error %s\n", curl_easy_strerror(res));
		else
			Con_DPrintf("Error downloading file: Server responded with HTTP status %ld\n", response_code);

		return false;
	}

	if (rename(tmp_path, local_path) != 0) 
	{
		unlink(tmp_path); // Also delete the temporary file in case renaming fails
		cls.download.active = false;
		curl_download_active = false;
		return false;
	}
	cls.download.active = false;
	curl_download_active = false;

	char sizeStr[32];

	if (fileSizeMB >= 1.0f)
		q_snprintf(sizeStr, sizeof(sizeStr), "%.2f mb", fileSizeMB);
	else if (fileSizeKB >= 1.0f)
		q_snprintf(sizeStr, sizeof(sizeStr), "%ld kb", (long)fileSizeKB);

	else
		q_snprintf(sizeStr, sizeof(sizeStr), "%ld bytes", fileSizeBytes);

	if (strstr(filename, ".bsp")) // woods, add mapname to extralevels tab completion
	{
		char mapname[MAX_QPATH];
		COM_StripExtension(COM_SkipPath(filename), mapname, sizeof(mapname));
		FileList_Add (mapname, NULL, &extralevels);

	}

	Con_Printf("Downloaded ^m%s^m (%s) from %s\n", COM_SkipPath(filename), sizeStr, url);

	return true; // File successfully downloaded
}

//sent by the server to let us know that dp downloads can be used
void CL_ServerExtension_Download_f(void)
{
	if (Cmd_Argc() == 2)
		cl.protocol_dpdownload = atoi(Cmd_Argv(1));
}

//sent by the server to let us know when its finished sending the entire file
void CL_Download_Finished_f(void)
{
	if (cls.download.file)
	{
		char finalpath[MAX_OSPATH];
		unsigned int size = strtoul(Cmd_Argv(1), NULL, 0);
		unsigned int hash = strtoul(Cmd_Argv(2), NULL, 0);
		//const char *fname = Cmd_Argv(3);
		qboolean hashokay = false;
		if (size == cls.download.size)
		{
			byte *tmp = malloc(size);
			if (tmp)
			{
				fseek(cls.download.file, 0, SEEK_SET);
				fread(tmp, 1, size, cls.download.file);
				hashokay = (hash == CRC_Block(tmp, size));
				free(tmp);

				if (!hashokay) Con_Warning("Download hash failure\n");
			}
			else Con_Warning("Download size too large\n");
		}
		else Con_Warning("Download size mismatch\n");

		fclose(cls.download.file);
		cls.download.file = NULL;
		if (hashokay)
		{
			q_snprintf (finalpath, sizeof(finalpath), "%s/%s", com_gamedir, cls.download.current);
			rename(cls.download.temp, finalpath);
			
			if (strstr(cls.download.current, ".bsp")) // woods, add mapname to extralevels tab completion
			{
				char mapname[MAX_QPATH];
				COM_StripExtension(COM_SkipPath(cls.download.current), mapname, sizeof(mapname));
				FileList_Add (mapname, NULL, &extralevels);
			}

			Con_SafePrintf("Downloaded %s: %u bytes\n", cls.download.current, cls.download.size);
		}
		else
		{
			Con_Warning("Download of %s failed\n", cls.download.current);
			unlink(cls.download.temp);	//kill the temp
		}
	}

	cls.download.active = false;
}
//sent by the server (or issued by the user) to stop the current download for any reason.
void CL_StopDownload_f(void)
{
	if (!curl_download_active) // woods, add support for stopping curl downloads #webdl
	{
		if (cls.download.file)
		{
			fclose(cls.download.file);
			cls.download.file = NULL;
			unlink(cls.download.temp);

			//		Con_SafePrintf("Download cancelled\n", cl.download_current, cl.download_size);
		}
		cls.download.active = false;

	}
	else
		stop_curl_download = true;
}
//sent by the server to let us know that its going to start spamming us now.
void CL_Download_Begin_f(void)
{
	if (!cls.download.active)
		return;

	if (cls.download.file)
		CL_StopDownload_f();

	//cl_downloadbegin size "name"
	cls.download.size = strtoul(Cmd_Argv(1), NULL, 0);

	COM_CreatePath(cls.download.temp);
	cls.download.file = fopen(cls.download.temp, "wb+");	//+ so we can read the data back to validate it

	if ((double)cls.download.size > 5 * 1024 * 1024) // woods anything over 5mb suggest alternative download
	Con_Printf("\nwarning: large file size items usually have additional assets, recommended download outside of QSSM\n\n");

	MSG_WriteByte (&cls.message, clc_stringcmd);
	MSG_WriteString (&cls.message, "sv_startdownload\n");
}

void CL_Download_Data(void)
{
	byte *data;
	unsigned int start, size;
	start = MSG_ReadLong();
	size = (unsigned short)MSG_ReadShort();
	data = MSG_ReadData(size);
	if (msg_badread)
		return;
	if (!cls.download.file)
		return;	//demo started mid-record? something weird anyway

	fseek(cls.download.file, start, SEEK_SET);
	fwrite(data, 1, size, cls.download.file);

	Con_SafePrintf("Downloading %s (%.2f MB): %g%%\r", cls.download.current, (double)cls.download.size / (1024 * 1024), 100 * (start + size) / (double)cls.download.size); // woods add file size info

	//should maybe use unreliables, but whatever, shouldn't matter too much, it'll still complete
	MSG_WriteByte(&cls.message, clcdp_ackdownloaddata);
	MSG_WriteLong(&cls.message, start);
	MSG_WriteShort(&cls.message, size);
}

//returns true if we should block waiting for a download, false if there's no point.
qboolean CL_CheckDownload(const char *filename)
{
	if (sv.active)
		return false;	//no point downloading if we're the server...
	if (*filename == '*')
		return false;	//don't download these...
	if (cls.download.active)
		return true;	//block while we're already downloading something
	if (*cls.download.current && !strcmp(cls.download.current, filename))
		return false;	//if the previous download failed, don't endlessly retry.
	if (COM_FileExists(filename, NULL))
		return false;	//no need to download anything.
	if (!COM_DownloadNameOkay(filename))
		return false;	//diediedie

	// woods, lets try curl web download first #webdl (much faster) #webdl

	char local_path[MAX_OSPATH]; // Define the max path length	

	q_snprintf(local_path, sizeof(local_path), "%s/%s", com_gamedir, filename);

	if (webcheck && (cl_web_download_url.string != NULL && cl_web_download_url.string[0] != '\0')) // only run if server is verified
		if (Curl_DownloadFile (cl_web_download_url.string, filename, local_path))
			return false;

	if (web2check && (cl_web_download_url2.string != NULL && cl_web_download_url2.string[0] != '\0')) // only run if server is verified
		if (Curl_DownloadFile (cl_web_download_url2.string, filename, local_path))
			return false;

	// woods, if not available via web, try the server #webdl

	if (allow_download.value == 2) // woods #ftehack
	{
		if (!cl.protocol_dpdownload && cl.protocol != 666) // woods, allow downloads on qecrx (nq physics, FTE server) -- hack
			return false;	//can't download anyway
		if (netquakeio)
			return false;
		if (cls.demoplayback)
			return false;
	}
	else
	{
		if (!cl.protocol_dpdownload)
			return false;	//can't download anyway
	}

	cls.download.active = true;
	q_strlcpy(cls.download.current, filename, sizeof(cls.download.current));
	q_snprintf (cls.download.temp, sizeof(cls.download.temp), "%s/%s.tmp", com_gamedir, filename);
	if (!strstr(filename, ".loc")) // woods, don't show attempt
		Con_Printf("Downloading %s...\r", filename);
	MSG_WriteByte (&cls.message, clc_stringcmd);
	MSG_WriteString (&cls.message, va("download \"%s\"\n", filename));
	return true;
}

//download+load models and sounds as needed, once complete let the server know we're ready for the next stage.
//returning false will trigger nops.
qboolean CL_CheckDownloads(void)
{
	int i;
	if (cl.model_download == 0 && cl.model_count && cl.model_name[1])
	{	//haxors, download the lit first, but only if we don't already have the bsp
		//this ensures that we don't keep requesting the lit for maps that just don't have one (although may be problematic if the first server we find deleted them all, but oh well)
		char litname[MAX_QPATH];
		char *ext;
		q_strlcpy(litname, cl.model_name[1], sizeof(litname));
		ext = (char*)COM_FileGetExtension(litname);
		if (!q_strcasecmp(ext, "bsp"))
		{
			if (!COM_FileExists(litname, NULL))
			{
				strcpy(ext, "lit");
				if (CL_CheckDownload(litname))
					return false;
			}
		}
		cl.model_download++;
	}

	// woods #locdownloads

	if (cl.loc_download == 0 && !cls.demoplayback)
	{ 
		char locname[MAX_QPATH];
		char locname2[80];
		COM_FileBase(cl.model_name[1], locname, sizeof(locname));
		sprintf(locname2, "locs/%s.loc", locname);

		if (!COM_FileExists(locname2, NULL))
		{
			if (CL_CheckDownload(locname2))
				return false;
		}
		cl.loc_download++;
	}

	for (; cl.model_download < cl.model_count; )
	{
		if (*cl.model_name[cl.model_download])
		{
			if (CL_CheckDownload(cl.model_name[cl.model_download]))
				return false;
			cl.model_precache[cl.model_download] = Mod_ForName (cl.model_name[cl.model_download], false);
			if (cl.model_precache[cl.model_download] == NULL)
			{
				Host_Error ("Model %s not found", cl.model_name[cl.model_download]);
			}
		}
		cl.model_download++;
	}

	for (; cl.sound_download < cl.sound_count; )
	{
		if (*cl.sound_name[cl.sound_download])
		{
			if (CL_CheckDownload(va("sound/%s", cl.sound_name[cl.sound_download])))
				return false;
			cl.sound_precache[cl.sound_download] = S_PrecacheSound (cl.sound_name[cl.sound_download]);
		}
		cl.sound_download++;
	}

	if (!cl.worldmodel && cl.model_count >= 2)
	{
	// local state
		cl.entities[0].model = cl.worldmodel = cl.model_precache[1];
		if (cl.worldmodel->type != mod_brush)
		{
			if (cl.worldmodel->type == mod_ext_invalid)
				Host_Error ("Worldmodel %s was not loaded", cl.model_name[1]);
			else
				Host_Error ("Worldmodel %s is not a brushmodel", cl.model_name[1]);
		}

		//fixme: deal with skybox somehow

		R_NewMap ();

#ifdef PSET_SCRIPT
		//the protocol changing depending upon files found on the client's computer is of course a really shit way to design things
		//especially when users have a nasty habit of changing config files.
		if (cl.protocol == PROTOCOL_VERSION_DP7)
		{
			PScript_FindParticleType("effectinfo.");	//make sure this is implicitly loaded.
			COM_Effectinfo_Enumerate(CL_GenerateRandomParticlePrecache);
			cl.protocol_particles = true;
		}
		else if (cl.protocol_pext2 || (cl.protocol_pext1&PEXT1_CSQC))
			cl.protocol_particles = true;	//doesn't have a pext flag of its own, but at least we know what it is.
#endif
	}

	//make sure ents have the correct models, now that they're actually loaded.
	for (i = 0; i < cl.num_statics; i++)
	{
		if (cl.static_entities[i]->model)
			continue;
		cl.static_entities[i]->model = cl.model_precache[cl.static_entities[i]->netstate.modelindex];
		R_AddEfrags (cl.static_entities[i]);
	}
	return true;
}

/*
====================
CL_ManualDownload_f -- woods #manualdownload
====================
*/
void CL_ManualDownload_f (const char* filename)
{

	if (Cmd_Argc() != 2)
	{
		Con_Printf("download <filename> : filename with an extension (bsp, lit, wav, loc)\n");
		return;
	}

	if (*filename == '*')
		return;	//don't download these...
	if (cls.download.active)
		return;	//block while we're already downloading something
	if (*cls.download.current && !strcmp(cls.download.current, filename))
		return;	//if the previous download failed, don't endlessly retry.

	const char* extension = COM_FileGetExtension(Cmd_Argv(1));

	if (strlen(extension) == 0)
	{
		Con_Printf("Please use a filename with an extension (bsp, lit, wav, loc)\n");
		return;
	}

	if (strcmp(extension, "bsp") != 0 && strcmp(extension, "wav") != 0 && strcmp(extension, "loc") != 0)
	{
		Con_Printf("Unsupported file extension. Use bsp, lit, wav, loc extensions\n");
		return;
	}

	char prefixedArg[MAX_OSPATH];

	if (strcmp(extension, "bsp") == 0 || strcmp(extension, "lit") == 0)
	{
		snprintf(prefixedArg, sizeof(prefixedArg), "maps/%s", Cmd_Argv(1));
	}
	else if (strcmp(extension, "wav") == 0)
	{
		snprintf(prefixedArg, sizeof(prefixedArg), "sound/%s", Cmd_Argv(1));
	}
	else if (strcmp(extension, "loc") == 0)
	{
		snprintf(prefixedArg, sizeof(prefixedArg), "locs/%s", Cmd_Argv(1));
	}
	else
	{
		strncpy(prefixedArg, Cmd_Argv(1), sizeof(prefixedArg));
		prefixedArg[sizeof(prefixedArg) - 1] = '\0';
	}

	if (COM_FileExists(prefixedArg, NULL))
	{
		Con_Printf("File already exists, download not attempted\n");
		return;
	}

	qboolean isNeitherWebDownloadServerSet = (cl_web_download_url.string == NULL || cl_web_download_url.string[0] == '\0') &&
		(cl_web_download_url2.string == NULL || cl_web_download_url2.string[0] == '\0');

	if (isNeitherWebDownloadServerSet)
	{
		Con_Printf("No web download servers are set\n");
		return;
	}

	if (!webcheck && !web2check)
	{
		Con_Printf("No web download servers are active\n");
		return;
	}

	Con_Printf("Attempting download, if found you will see progress below...\n");

	char local_path[MAX_OSPATH]; // Define the max path length	

	q_snprintf(local_path, sizeof(local_path), "%s/%s", com_gamedir, prefixedArg);

	if (webcheck && (cl_web_download_url.string != NULL && cl_web_download_url.string[0] != '\0')) // only run if server is verified
		if (Curl_DownloadFile(cl_web_download_url.string, prefixedArg, local_path))
			return;

	if (web2check && (cl_web_download_url2.string != NULL && cl_web_download_url2.string[0] != '\0')) // only run if server is verified
		if (Curl_DownloadFile(cl_web_download_url2.string, prefixedArg, local_path))
			return;
}

/*
===============
CL_ReadFromServer

Read all incoming data from the server
===============
*/
int CL_ReadFromServer (void)
{
	int			ret;
	extern int	num_temp_entities; //johnfitz
	int			num_beams = 0; //johnfitz
	int			num_dlights = 0; //johnfitz
	beam_t		*b; //johnfitz
	dlight_t	*l; //johnfitz
	int			i; //johnfitz


	cl.oldtime = cl.time;
	cl.time += host_frametime;

	if (!cls.demorewind || !cls.demoplayback)	// by joe // woods #demorewind (Baker Fitzquake Mark V)
		cl.ctime += host_frametime;
	else
		cl.ctime -= host_frametime;

	do
	{
		ret = CL_GetMessage ();
		if (ret == -1)
			Host_Error ("CL_ReadFromServer: lost server connection");
		if (!ret)
			break;

		cl.last_received_message = realtime;
		CL_ParseServerMessage ();
	} while (ret && cls.state == ca_connected);

	if (cl_shownet.value)
		Con_Printf ("\n");

	PR_SwitchQCVM(&cl.qcvm);
	CL_RelinkEntities ();
	CL_UpdateTEnts ();
	PR_SwitchQCVM(NULL);

//johnfitz -- devstats

	//visedicts
	if (cl_numvisedicts > 256 && dev_peakstats.visedicts <= 256)
		Con_DWarning ("%i visedicts exceeds standard limit of 256.\n", cl_numvisedicts);
	dev_stats.visedicts = cl_numvisedicts;
	dev_peakstats.visedicts = q_max(cl_numvisedicts, dev_peakstats.visedicts);

	//temp entities
	if (num_temp_entities > 64 && dev_peakstats.tempents <= 64)
		Con_DWarning ("%i tempentities exceeds standard limit of 64 (max = %d).\n", num_temp_entities, MAX_TEMP_ENTITIES);
	dev_stats.tempents = num_temp_entities;
	dev_peakstats.tempents = q_max(num_temp_entities, dev_peakstats.tempents);

	//beams
	for (i=0, b=cl_beams ; i< MAX_BEAMS ; i++, b++)
		if (b->model && b->endtime >= cl.time)
			num_beams++;
	if (num_beams > 24 && dev_peakstats.beams <= 24)
		Con_DWarning ("%i beams exceeded standard limit of 24 (max = %d).\n", num_beams, MAX_BEAMS);
	dev_stats.beams = num_beams;
	dev_peakstats.beams = q_max(num_beams, dev_peakstats.beams);

	//dlights
	for (i=0, l=cl_dlights ; i<MAX_DLIGHTS ; i++, l++)
		if (l->die >= cl.time && l->radius)
			num_dlights++;
	if (num_dlights > 32 && dev_peakstats.dlights <= 32)
		Con_DWarning ("%i dlights exceeded standard limit of 32 (max = %d).\n", num_dlights, MAX_DLIGHTS);
	dev_stats.dlights = num_dlights;
	dev_peakstats.dlights = q_max(num_dlights, dev_peakstats.dlights);

//johnfitz

//
// bring the links up to date
//
	return 0;
}

/*
=================
CL_UpdateViewAngles

Spike: split from CL_SendCmd, to do clientside viewangle changes separately from outgoing packets.
=================
*/
void CL_AccumulateCmd (void)
{
	if (cls.signon == SIGNONS)
	{
		//basic keyboard looking
		CL_AdjustAngles ();

		//accumulate movement from other devices
		CL_BaseMove (&cl.pendingcmd, false);
		IN_Move (&cl.pendingcmd);
		CL_FinishMove(&cl.pendingcmd, false);
	}
	else
		cl.lastcmdtime = cl.mtime[0];
}

/*
=================
CL_SendCmd
=================
*/
void CL_SendCmd (void)
{
	usercmd_t		cmd;

	if (cls.state != ca_connected)
		return;

	// get basic movement from keyboard
	CL_BaseMove (&cmd, true);
	IN_Move (&cmd);
	CL_FinishMove(&cmd, true);

	if (cl.qcvm.extfuncs.CSQC_Input_Frame && !cl.qcvm.nogameaccess)
	{
		PR_SwitchQCVM(&cl.qcvm);
		PR_GetSetInputs(&cmd, true);
		PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_Input_Frame);
		PR_GetSetInputs(&cmd, false);
		PR_SwitchQCVM(NULL);
	}

	if (cls.signon == SIGNONS)
	{
		if (pq_lag.value) // woods #pqlag
			CL_SendMove2(&cmd);	// send the unreliable message
		else
			CL_SendMove(&cmd);	// send the unreliable message
	}
	else
	{
		if (pq_lag.value) // woods #pqlag
			CL_SendMove2(NULL);
		else
			CL_SendMove(NULL);
		cmd.seconds = 0;	//not sent, don't predict it either.
	}
	cl.pendingcmd.seconds = 0;
	cl.lastcmdtime = cmd.servertime;

	if (cls.demoplayback)
	{
		SZ_Clear (&cls.message);
		return;
	}

// send the reliable message
	if (!cls.message.cursize)
		return;		// no message at all

	if (!NET_CanSendMessage (cls.netcon))
	{
		Con_DPrintf ("CL_SendCmd: can't send\n");
		return;
	}

	if (NET_SendMessage (cls.netcon, &cls.message) == -1)
		Host_Error ("CL_SendCmd: lost server connection");

	SZ_Clear (&cls.message);
}

/*
=============
CL_Tracepos_f -- johnfitz

display impact point of trace along VPN
=============
*/
void CL_Tracepos_f (void)
{
	vec3_t	v, w;

	if (cls.state != ca_connected)
		return;

	VectorMA(r_refdef.vieworg, 8192.0, vpn, v);
	TraceLine(r_refdef.vieworg, v, 0, w);

	if (VectorLength(w) == 0)
		Con_Printf ("Tracepos: trace didn't hit anything\n");
	else
		Con_Printf ("Tracepos: (%i %i %i)\n", (int)w[0], (int)w[1], (int)w[2]);
}

/*
=============
CL_Viewpos_f -- johnfitz

display client's position and angles
=============
*/
void CL_Viewpos_f (void)
{
	char buf[256];
	if (cls.state != ca_connected)
		return;
#if 0
	//camera position
	q_snprintf (buf, sizeof (buf),
		"(%i %i %i) %i %i %i",
		(int)r_refdef.vieworg[0],
		(int)r_refdef.vieworg[1],
		(int)r_refdef.vieworg[2],
		(int)r_refdef.viewangles[PITCH],
		(int)r_refdef.viewangles[YAW],
		(int)r_refdef.viewangles[ROLL]);
#else
	//player position
	q_snprintf (buf, sizeof (buf),
		"(%i %i %i) %i %i %i",
		(int)cl.entities[cl.viewentity].origin[0],
		(int)cl.entities[cl.viewentity].origin[1],
		(int)cl.entities[cl.viewentity].origin[2],
		(int)cl.viewangles[PITCH],
		(int)cl.viewangles[YAW],
		(int)cl.viewangles[ROLL]
	);
#endif
	Con_Printf ("Viewpos: %s\n", buf);

	if (Cmd_Argc () >= 2 && !q_strcasecmp (Cmd_Argv (1), "copy"))
		if (SDL_SetClipboardText (buf) < 0)
			Con_Printf ("Clipboard copy failed: %s\n", SDL_GetError ());
}

/*
===============
CL_Entdump_f -- woods (source: github.com/alexey-lysiuk/quakespasm-exp) #entcopy
===============
*/
void CL_Entdump_f(void)
{
	char entfilename[MAX_QPATH];
	size_t entlen;

	if (!cl.worldmodel)
	{
		Con_SafePrintf("no map loaded, cannot save .ent\n");
		return;
	}

	entlen = strlen(cl.worldmodel->entities);

	if (Cmd_Argc() < 2)
		q_snprintf(entfilename, sizeof entfilename, "%s.ent", cl.mapname);
	else
	{
		strncpy(entfilename, Cmd_Argv(1), sizeof entfilename - 1);
		entfilename[sizeof entfilename - 1] = '\0';
	}

	COM_WriteFile(entfilename, cl.worldmodel->entities, entlen);
	Con_Printf("saved %s.ent to game directory\n", cl.mapname);
}

static void CL_ServerExtension_FullServerinfo_f(void)
{
	const char *newserverinfo = Cmd_Argv(1);
	Q_strncpy(cl.serverinfo, newserverinfo, sizeof(cl.serverinfo));	//just replace it

	PMCL_ServerinfoUpdated();
}
static void CL_ServerExtension_ServerinfoUpdate_f(void)
{
	const char *newserverkey = Cmd_Argv(1);
	const char *newservervalue = Cmd_Argv(2);
	Info_SetKey(cl.serverinfo, sizeof(cl.serverinfo), newserverkey, newservervalue);

	PMCL_ServerinfoUpdated();
}

int	Sbar_ColorForMap (int m);
byte *CL_PLColours_ToRGB(plcolour_t *c)
{
	if (c->type == 2)
		return c->rgb;
	else if (c->type == 1)
		return (byte *)(d_8to24table + (c->basic<<4)+8);
	else
		return (byte*)&d_8to24table[15];
}
char *CL_PLColours_ToString(plcolour_t c)
{
	if (c.type == 2)
		return va("0x%02x%02x%02x", c.rgb[0], c.rgb[1], c.rgb[2]);
	else if (c.type == 1)
		return va("%i", c.basic);
	return "0";
}

plcolour_t CL_PLColours_FromLegacy(int val)
{
	plcolour_t c;
	val&=0xf;
	c.type = 1;
	c.basic = val;
	c.rgb[0] = c.rgb[1] = c.rgb[2] = val; //fixme... store proper palette?

	return c;
}

plcolour_t CL_PLColours_Parse(const char *s)
{
	plcolour_t c;
	unsigned int v = strtoul(s, NULL, 0);
	if (!strncmp(s, "0x", 2))
	{
		c.type = 2;
		c.basic = 0;	//find nearest?
		c.rgb[0] = 0xff&(v>>16);
		c.rgb[1] = 0xff&(v>>8);
		c.rgb[2] = 0xff&(v>>0);
	}
	else if (*s)
		return CL_PLColours_FromLegacy(v);
	else
	{
		c.type = 0;
		c.basic = 0;
		c.rgb[0] = c.rgb[1] = c.rgb[2] = 0xff;
	}
	return c;
}
static void CL_UserinfoChanged(scoreboard_t *sb)
{
	char tmp[64];
	Info_GetKey(sb->userinfo, "name", sb->name, sizeof(sb->name));
	Info_GetKey(sb->userinfo, "topcolor", tmp, sizeof(tmp));
	sb->shirt = CL_PLColours_Parse(*tmp?tmp:"0");
	Info_GetKey(sb->userinfo, "bottomcolor", tmp, sizeof(tmp));
	sb->pants = CL_PLColours_Parse(*tmp?tmp:"0");

	//for qw compat. remember that keys with an asterisk are blocked from setinfo (still changable via ssqc though).
	sb->spectator = atoi(Info_GetKey(sb->userinfo, "*spectator", tmp, sizeof(tmp)));	//0=regular player, 1=spectator, 2=spec-with-scores aka waiting their turn to (re)spawn.
	//Info_GetKey(sb->userinfo, "team", sb->team, sizeof(sb->team));
	//Info_GetKey(sb->userinfo, "skin", sb->skin, sizeof(sb->skin));
}
static void CL_ServerExtension_FullUserinfo_f(void)
{
	size_t slot = atoi(Cmd_Argv(1));
	const char *newserverinfo = Cmd_Argv(2);
	if (slot < cl.maxclients)
	{
		scoreboard_t *sb = &cl.scores[slot];
		Q_strncpy(sb->userinfo, newserverinfo, sizeof(sb->userinfo));	//just replace it
		CL_UserinfoChanged(sb);
	}
}
static void CL_ServerExtension_UserinfoUpdate_f(void)
{
	size_t slot = atoi(Cmd_Argv(1));
	const char *newserverkey = Cmd_Argv(2);
	const char *newservervalue = Cmd_Argv(3);
	if (slot < cl.maxclients)
	{
		scoreboard_t *sb = &cl.scores[slot];
		Info_SetKey(sb->userinfo, sizeof(sb->userinfo), newserverkey, newservervalue);
		CL_UserinfoChanged(sb);
	}
}
static void SV_DecodeUserInfo(client_t *client)
{
	char tmp[64];
	int top, bot;

	//figure out the player's colours
	Info_GetKey(client->userinfo, "topcolor", tmp, sizeof(tmp));
	top = atoi(tmp)&15;
	if (top > 13)
		top = 13;
	Info_GetKey(client->userinfo, "bottomcolor", tmp, sizeof(tmp));
	bot = atoi(tmp)&15;
	if (bot > 13)
		bot = 13;
	//update their entity
	client->edict->v.team = bot+1;
	client->colors = (top<<4) | bot;

	//pick out a name and try to clean it up a little.
	Info_GetKey(client->userinfo, "name", tmp, sizeof(tmp));
	if (!*tmp)
		q_strlcpy(tmp, "unnamed", sizeof(tmp));

	if (Q_strcmp(client->name, tmp) != 0)
	{	//name changed.
		if (client->name[0] && strcmp(client->name, "unconnected") )
			Con_Printf ("%s renamed to %s\n", host_client->name, tmp);
		Q_strcpy (host_client->name, tmp);
		client->edict->v.netname = PR_SetEngineString(client->name);
	}
}
void SV_UpdateInfo(int edict, const char *keyname, const char *value)
{
	char oldvalue[1024];

	char *info;
	size_t infosize;
	const char *pre;
	client_t *cl;
	client_t *infoplayer = NULL;
	char prestr[64];

	if (!edict)
	{
		cvar_t *var = Cvar_FindVar(keyname);
		if (var && var->flags & CVAR_SERVERINFO)
		{
			Cvar_Set(var->name, value);
			return;
		}
		info = svs.serverinfo;
		infosize = sizeof(svs.serverinfo);
		pre = "//svi";
	}
	else if (edict <= svs.maxclients)
	{
		edict-=1;
		infoplayer = &svs.clients[edict];
		info = infoplayer->userinfo;
		infosize = sizeof(infoplayer->userinfo);
		q_snprintf(prestr, sizeof(prestr), "//ui %i", edict);
		pre = prestr;
	}
	else
		return;

	Info_GetKey(info, keyname, oldvalue, sizeof(oldvalue));
	if (strcmp(value, oldvalue))
	{	//its changed. actually broadcast it.
		Info_SetKey(info, infosize, keyname, value);
		if (infoplayer)
			SV_DecodeUserInfo(infoplayer);

		if (*keyname == '_' || !sv.active)
			return;	//underscore means private (user) keys. these are not networked to clients.

		Info_GetKey(info, keyname, oldvalue, sizeof(oldvalue));
		value = oldvalue;

		for (cl = svs.clients; cl < svs.clients+svs.maxclients; cl++)
		{
			if (cl->active)
			{
				if (cl->protocol_pext2 & PEXT2_PREDINFO)
				{
					MSG_WriteByte (&cl->message, svc_stufftext);
					MSG_WriteString (&cl->message, va("%s \"%s\" \"%s\"\n", pre, keyname, value));
				}
				else if (infoplayer && !strcmp(keyname, "name"))
				{
					MSG_WriteByte (&cl->message, svc_updatename);
					MSG_WriteByte (&cl->message, edict);
					MSG_WriteString (&cl->message, value);
				}
				else if (infoplayer && (!strcmp(keyname, "topcolor") || !strcmp(keyname, "bottomcolor")))
				{
					MSG_WriteByte (&cl->message, svc_updatecolors);
					MSG_WriteByte (&cl->message, edict);
					MSG_WriteByte (&cl->message, infoplayer->colors);
				}
			}
		}
	}
}
static void CL_ServerExtension_Ignore_f(void)
{
	Con_DPrintf2("Ignoring stufftext: %s\n", Cmd_Argv(0));
}
static void CL_LegacyColor_f(void)
{	//spike -- code to handle the legacy _cl_color cvar (we now use separate qw-style topcolor/bottomcolor userinfo cvars)
	int col = atoi(Cmd_Argv(1));
	Cvar_SetValue("topcolor",		(col>>4)&0xf);
	Cvar_SetValue("bottomcolor",	(col>>0)&0xf);
}

/*
=================
CL_Init
=================
*/
void CL_Init (void)
{
	SZ_Alloc (&cls.message, 1024);

	CL_InitInput ();
	CL_InitTEnts ();

	Cvar_RegisterVariable (&cl_name);
	Cvar_RegisterAlias    (&cl_name, "_cl_name");	//spike -- for compat with configs now that 'name' is a cvar in its own right.
	Cvar_RegisterVariable (&cl_topcolor);
	Cvar_RegisterVariable (&cl_bottomcolor);
	Cmd_AddCommand ("_cl_color", CL_LegacyColor_f);	//for loading vanilla configs (we have separate qw-style topcolor/bottomcolor userinfo cvars instead)
	Cvar_RegisterVariable (&cl_upspeed);
	Cvar_RegisterVariable (&cl_forwardspeed);
	Cvar_RegisterVariable (&cl_backspeed);
	Cvar_RegisterVariable (&cl_sidespeed);
	Cvar_RegisterVariable (&cl_movespeedkey);
	Cvar_RegisterVariable (&cl_yawspeed);
	Cvar_RegisterVariable (&cl_pitchspeed);
	Cvar_RegisterVariable (&cl_anglespeedkey);
	Cvar_RegisterVariable (&cl_shownet);
	Cvar_RegisterVariable (&cl_nolerp);
	Cvar_RegisterVariable (&cl_nopred);
	Cvar_RegisterVariable (&lookspring);
	Cvar_RegisterVariable (&lookstrafe);
	Cvar_RegisterVariable (&sensitivity);
	
	Cvar_RegisterVariable (&cl_alwaysrun);

	Cvar_RegisterVariable (&m_pitch);
	Cvar_RegisterVariable (&m_yaw);
	Cvar_RegisterVariable (&m_forward);
	Cvar_RegisterVariable (&m_side);

	Cvar_RegisterVariable (&cfg_unbindall);

	Cvar_RegisterVariable (&cl_maxpitch); //johnfitz -- variable pitch clamping
	Cvar_RegisterVariable (&cl_minpitch); //johnfitz -- variable pitch clamping
	Cvar_RegisterVariable (&cl_recordingdemo); //spike -- for mod hacks. combine with cvar_string or something
	Cvar_RegisterVariable (&cl_demoreel);
	Cvar_RegisterVariable (&cl_demospeed); // woods

	Cvar_RegisterVariable (&cl_truelightning); // woods for #truelight
	Cvar_RegisterVariable (&gl_lightning_alpha); // woods for lighting alpha #lightalpha
	Cvar_RegisterVariable (&cl_say); // woods for #ezsay
	Cvar_RegisterVariable (&cl_afk); // woods #smartafk
	Cvar_RegisterVariable (&cl_idle); // woods #smartafk
	Cvar_RegisterVariable (&cl_rocketlight); // woods #rocketlight
	Cvar_RegisterVariable (&cl_muzzleflash); // woods #muzzleflash
	Cvar_RegisterVariable (&cl_deadbodyfilter); // woods #deadbody
	Cvar_RegisterVariable (&cl_r2g); // woods #r2g

	Cvar_RegisterVariable (&w_switch); // woods #autoweapon
	Cvar_RegisterVariable (&b_switch); // woods #autoweapon
	Cvar_RegisterVariable (&f_status); // woods #flagstatus

	Cvar_RegisterVariable (&cl_ambient); // woods #stopsound
	Cvar_RegisterVariable (&cl_smartspawn); // woods #spawntrainer
	Cvar_RegisterVariable (&r_coloredpowerupglow); // woods
	Cvar_RegisterVariable (&cl_bobbing); // woods (joequake #weaponbob)

	Cvar_RegisterVariable (&cl_web_download_url); // woods #webdl
	Cvar_RegisterVariable (&cl_web_download_url2); // woods #webdl

	Cvar_SetCallback (&cl_web_download_url, &WebCheckCallback_f); // woods #webdl
	Cvar_SetCallback (&cl_web_download_url2, &Web2CheckCallback_f); // woods #webdl

	WebCheckInit (); // woods -- check if the web downloads servers are live at launch (threaded) #webdl

	Cmd_AddCommand ("entities", CL_PrintEntities_f);
	Cmd_AddCommand ("disconnect", CL_Disconnect_f);
	Cmd_AddCommand ("record", CL_Record_f);
	Cmd_AddCommand ("stop", CL_Stop_f);
	Cmd_AddCommand ("playdemo", CL_PlayDemo_f);
	Cmd_AddCommand ("timedemo", CL_TimeDemo_f);

	Cmd_AddCommand ("tracepos", CL_Tracepos_f); //johnfitz
	Cmd_AddCommand ("viewpos", CL_Viewpos_f); //johnfitz

	Cmd_AddCommand("entdump", &CL_Entdump_f); // woods #entcopy

	//spike -- serverinfo stuff
	Cmd_AddCommand_ServerCommand ("fullserverinfo", CL_ServerExtension_FullServerinfo_f);
	Cmd_AddCommand_ServerCommand ("svi", CL_ServerExtension_ServerinfoUpdate_f);

	//spike -- userinfo stuff
	Cmd_AddCommand_ServerCommand ("fui", CL_ServerExtension_FullUserinfo_f);
	Cmd_AddCommand_ServerCommand ("ui", CL_ServerExtension_UserinfoUpdate_f);

	//spike -- add stubs to mute various invalid stuffcmds
	Cmd_AddCommand_ServerCommand ("paknames", CL_ServerExtension_Ignore_f); //package names in use by the server (including gamedir+extension)
	Cmd_AddCommand_ServerCommand ("paks", CL_ServerExtension_Ignore_f); //provides hashes to go with the paknames list
	//Cmd_AddCommand_ServerCommand ("vwep", CL_ServerExtension_Ignore_f); //invalid for nq, provides an alternative list of model precaches for vweps.
	//Cmd_AddCommand_ServerCommand ("at", CL_ServerExtension_Ignore_f); //invalid for nq, autotrack info for mvds
	Cmd_AddCommand_ServerCommand ("wps", CL_ServerExtension_Ignore_f); //ktx/cspree weapon stats
	Cmd_AddCommand_ServerCommand ("it", CL_ServerExtension_Ignore_f); //cspree item timers
	Cmd_AddCommand_ServerCommand ("tinfo", CL_ServerExtension_Ignore_f); //ktx team info
	Cmd_AddCommand_ServerCommand ("exectrigger", CL_ServerExtension_Ignore_f); //spike
	Cmd_AddCommand_ServerCommand ("csqc_progname", CL_ServerExtension_Ignore_f); //spike
	Cmd_AddCommand_ServerCommand ("csqc_progsize", CL_ServerExtension_Ignore_f); //spike
	Cmd_AddCommand_ServerCommand ("csqc_progcrc", CL_ServerExtension_Ignore_f); //spike
	Cmd_AddCommand_ServerCommand ("cl_fullpitch", CL_ServerExtension_Ignore_f); //spike
	Cmd_AddCommand_ServerCommand ("pq_fullpitch", CL_ServerExtension_Ignore_f); //spike

	Cmd_AddCommand_ServerCommand("ignorethis", CL_ServerExtension_Ignore_f); // woods crx
	Cmd_AddCommand_ServerCommand("crx_ignorethis", CL_ServerExtension_Ignore_f); // woods crx
	Cmd_AddCommand_ServerCommand("ignorethis_crx", CL_ServerExtension_Ignore_f); // woods crx
	Cmd_AddCommand_ServerCommand("init", CL_ServerExtension_Ignore_f); // woods runequake
	Cmd_AddCommand_ServerCommand("r_ambient", CL_ServerExtension_Ignore_f); // woods crmod66 legacy
	
	Cmd_AddCommand_ServerCommand ("cl_serverextension_download", CL_ServerExtension_Download_f); //spike
	Cmd_AddCommand_ServerCommand ("cl_downloadbegin", CL_Download_Begin_f); //spike
	Cmd_AddCommand_ServerCommand ("cl_downloadfinished", CL_Download_Finished_f); //spike
	Cmd_AddCommand ("stopdownload", CL_StopDownload_f); //spike
}

