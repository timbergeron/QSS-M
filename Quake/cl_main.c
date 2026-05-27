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
#include <errno.h>
#include <string.h>

#include <curl/curl.h> // woods #webdl
#include "cfgfile.h" // woods #webdl
#include "q_ctype.h" // woods #entcopy

void CL_RotateModel_OnChange(cvar_t* var); // woods #clmrotate
void CL_RotateModel_f(void); // woods #clmrotate
void CL_RotateModel_RebuildFromCvar(void); // woods #clmrotate
void CL_RotateModel_Cvar_Completion_f(cvar_t* cvar, const char* partial); // woods #clmrotate
void CL_DemoMark_f(void); // woods #demomark

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
cvar_t	cfg_save_aliases = {"cfg_save_aliases", "1", CVAR_ARCHIVE}; // woods #serveralias

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
cvar_t	cl_demo_format = {"cl_demo_format", "dem", CVAR_ARCHIVE};
cvar_t	cl_demo_minframes = {"cl_demo_minframes", "0", CVAR_ARCHIVE}; // abs(value) hides shorter demos; <0 also deletes just-finished short demos
cvar_t	cl_demoreel = {"cl_demoreel", "1", CVAR_ARCHIVE};

cvar_t	cl_beams_polygons = {"cl_beams_polygons", "0", CVAR_ARCHIVE}; // woods #beamspoly
cvar_t	cl_truelightning = {"cl_truelightning", "0",CVAR_ARCHIVE}; // woods for #truelight
cvar_t	cl_say = {"cl_say","0", CVAR_ARCHIVE}; // woods #ezsay
cvar_t  cl_afk = {"cl_afk", "0", CVAR_ARCHIVE }; // woods #smartafk
cvar_t  cl_idle = {"cl_idle", "0", CVAR_NONE }; // woods #smartafk
cvar_t  r_rocketlight = {"r_rocketlight", "0", CVAR_ARCHIVE }; // woods #rocketlight
cvar_t  r_explosionlight = {"r_explosionlight", "0", CVAR_ARCHIVE}; // woods #explosionlight
cvar_t  cl_muzzleflash = {"cl_muzzleflash", "0", CVAR_ARCHIVE}; // woods #muzzleflash
cvar_t  cl_deadbodyfilter = {"cl_deadbodyfilter", "1", CVAR_ARCHIVE}; // woods #deadbody
cvar_t	cl_r2g = {"cl_r2g","0",CVAR_ARCHIVE}; // woods #r2g
cvar_t	cl_demoeyes = {"cl_demoeyes", "0", CVAR_ARCHIVE}; // woods #demoeyes (value = alpha, 0 = disabled)
cvar_t	cl_ctf_pub_modelswap = {"cl_ctf_pub_modelswap", "0", CVAR_ARCHIVE}; // woods #ctfpubmodels
cvar_t	cl_rot = {"cl_rot", "", CVAR_ARCHIVE}; // woods #clmrotate

cvar_t  w_switch = {"w_switch", "0", CVAR_ARCHIVE | CVAR_USERINFO}; // woods #autoweapon
cvar_t  b_switch = {"b_switch", "0", CVAR_ARCHIVE | CVAR_USERINFO}; // woods #autoweapon
cvar_t  f_status = {"f_status", "on", CVAR_ARCHIVE | CVAR_USERINFO}; // woods #flagstatus

cvar_t  cl_ambient = {"cl_ambient", "1", CVAR_ARCHIVE}; // woods #stopsound
cvar_t  r_coloredpowerupglow = {"r_coloredpowerupglow", "1", CVAR_ARCHIVE}; // woods
cvar_t  cl_bobbing = {"cl_bobbing", "0", CVAR_ARCHIVE}; // woods (joequake #weaponbob)
cvar_t	cl_web_download_url = {"cl_web_download_url", "q1tools/q1tools.github.io", CVAR_ARCHIVE}; // woods #webdl
cvar_t	cl_web_download_url2 = { "cl_web_download_url2", "maps.quakeworld.nu", CVAR_ARCHIVE }; // woods #webdl
cvar_t	cl_autovote = {"cl_autovote", "0", CVAR_ARCHIVE}; // woods #autovote
cvar_t	cl_autovote_list = {"cl_autovote_list", "", CVAR_ARCHIVE}; // woods #autovote
cvar_t	cl_onload = {"cl_onload", "", CVAR_ARCHIVE}; // woods #onload
cvar_t	cl_contentfilter = {"cl_contentfilter", "0", CVAR_ARCHIVE}; // woods #contentfilter

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
extern cvar_t	sv_mapcrc; // woods #mapcrc
extern int		ctfpubflagprecache; // woods #ctfpubmodels
extern int		ctfpubhookprecache; // woods #ctfpubmodels
extern int		ctfpubhookchainprecache; // woods #ctfpubmodels
extern int		ctfpubhookweaponprecache; // woods #ctfpubmodels
extern qboolean	qeintermission; // woods #qeintermission
extern qboolean	crxintermission; // woods #crxintermission
extern qboolean m_return_onerror;
extern char m_return_reason[32];

char			lastmphost[NET_NAMELEN]; // woods - connected server address
int				maptime;		// woods connected map time #maptime

void Log_Last_Server_f(void); // woods #connectlast (Qrack) -- write last server to file memory
void Host_ConnectToLastServer_f(void); // woods use #connectlast for smarter reconnect
qboolean Host_GetLastServer(char *name, size_t namesize);

extern char lastconnected[3]; // woods #identify+
extern qboolean netquakeio; // woods
extern int retry_counter; // woods #ms
extern int grenadecache, rocketcache; // woods #r2g
extern qboolean pausedprint; // woods
extern SDL_TimerID chatTimerID; // woods #chatinfo
extern qboolean isChatTimerRunning; // woods #chatinfo
static qboolean prediction_msg_shown = false; // woods #prednotify

static void CL_ClearTypingState(void)
{
	int i;

	Info_SetKey(cls.userinfo, sizeof(cls.userinfo), "chat", "0");

	if (isChatTimerRunning)
	{
		SDL_RemoveTimer(chatTimerID);
		isChatTimerRunning = false;
		chatTimerID = 0;
	}

	if (!cl.scores || cl.maxclients <= 0)
		return;

	for (i = 0; i < cl.maxclients; i++)
		Info_SetKey(cl.scores[i].userinfo, sizeof(cl.scores[i].userinfo), "chat", "");
}

static const char cl_empty_userinfo[] = "";

const char *CL_GetSafeUserinfoForClientSlot(int playernum)
{
	if (!cl.scores || cl.maxclients <= 0 || playernum < 0 || playernum >= cl.maxclients)
		return cl_empty_userinfo;

	return cl.scores[playernum].userinfo;
}

const char *CL_GetSafeViewEntityUserinfo(void)
{
	return CL_GetSafeUserinfoForClientSlot(cl.viewentity - 1);
}

const char *CL_GetSafeRealViewEntityUserinfo(void)
{
	return CL_GetSafeUserinfoForClientSlot(cl.realviewentity - 1);
}

void CL_ClearTrailStates(void)
{
	int i;
	for (i = 0; i < cl.num_statics; i++)
	{
		PScript_DelinkTrailstate(&(cl.static_entities[i].ent->trailstate));
		PScript_DelinkTrailstate(&(cl.static_entities[i].ent->emitstate));
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

	Cvar_MapLock_RestoreAll ();
	V_MapScoped_RestoreServerStuff ();

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

	RSceneCache_Shutdown();

	if (!sv.active)
		Draw_ReloadTextures(false);
}

/*
=====================
CL_Connect Helpers
=====================
*/
typedef struct
{
	qboolean active;
	char host[NET_NAMELEN];
} cl_pending_connect_t;

static cl_pending_connect_t cl_pending_connect = {false, {0}};
static char cl_lasthost[NET_NAMELEN];
static qboolean cl_next_connect_from_menu = false;

static void CL_ClearConnectReturnState(void)
{
	m_return_onerror = false;
	m_return_reason[0] = '\0';
}

void CL_MarkNextConnectFromMenu(void)
{
	cl_next_connect_from_menu = true;
}

qboolean CL_ConsumeNextConnectFromMenu(void)
{
	qboolean from_menu = cl_next_connect_from_menu;
	cl_next_connect_from_menu = false;
	return from_menu;
}

static const char *CL_PrepareConnectHost(const char *host)
{
	if (!host)
	{
		host = cl_lasthost;
		if (!*host)
		{
			if (!Host_GetLastServer(cl_lasthost, sizeof(cl_lasthost)))
				return NULL;

			host = cl_lasthost;
			Con_Printf("using server history\n");
		}
	}
	else
	{
		q_strlcpy(cl_lasthost, host, sizeof(cl_lasthost));
	}

	return host;
}

static void CL_PrintConnectingMessage(const char *host)
{
	char addressip[70] = {'\0'};
	char local_verbose[NET_NAMELEN + sizeof(addressip)];
	int numaddresses;
	qhostaddr_t addresses[16];
	qboolean is_local;

	is_local = !strcmp(host, "local") || !strcmp(host, "localhost");
	if (is_local && sv.active && svs.maxclients == 1)
		return;

	numaddresses = NET_ListAddresses(addresses, sizeof(addresses) / sizeof(addresses[0]));
	if (numaddresses && !strstr(addresses[0], "["))
	{
		q_strlcpy(addressip, " -- ", sizeof(addressip));
		q_strlcat(addressip, addresses[0], sizeof(addressip));
	}

	if (is_local)
	{
		q_strlcpy(local_verbose, host, sizeof(local_verbose));
		q_strlcat(local_verbose, addressip, sizeof(local_verbose));
	}
	else
	{
		q_strlcpy(local_verbose, host, sizeof(local_verbose));
	}

	if (!strstr(host, ":"))
		Con_Printf("connecting to ^m%s:%i\n", local_verbose, net_hostport);
	else
		Con_Printf("connecting to ^m%s\n", local_verbose);
}

static void CL_PrintConnectFailureHints(void)
{
	Con_Printf("\nsyntax: connect server:port (port is optional)\n");
	if (net_hostport != 26000)
		Con_Printf("\nTry using port 26000\n");
}

void CL_PrintWrongGameDirWarning(void)
{
	const char *curgame;
	const char *servergame;

	if (!cl.wronggamedir)
		return;

	curgame = COM_GetGameNames(false);
	if (!*curgame)
		curgame = COM_GetGameNames(true);

	servergame = cl.server_gamedir;
	if (!*servergame)
		servergame = "(unknown)";

	Con_Warning("Server is using a different gamedir.\n");
	Con_Warning("Current: %s\n", curgame);
	Con_Warning("Server: %s\n", servergame);
	Con_Warning("You will probably want to switch gamedir to match the server.\n");
}

static void CL_MaybePrintLateWrongGameDirWarning(void)
{
	if (!cl.wronggamedir || cl.wronggamedir_latewarned)
		return;

	if (cl.suppressed_model_precache_warnings == 0 && cl.suppressed_sound_precache_warnings == 0)
		return;

	CL_PrintWrongGameDirWarning();
	if (cl.suppressed_model_precache_warnings && cl.suppressed_sound_precache_warnings)
	{
		Con_Warning("Suppressed %d missing model warnings and %d missing sound warnings caused by the gamedir mismatch.\n",
			cl.suppressed_model_precache_warnings, cl.suppressed_sound_precache_warnings);
	}
	else if (cl.suppressed_model_precache_warnings)
	{
		Con_Warning("Suppressed %d missing model warnings caused by the gamedir mismatch.\n",
			cl.suppressed_model_precache_warnings);
	}
	else
	{
		Con_Warning("Suppressed %d missing sound warnings caused by the gamedir mismatch.\n",
			cl.suppressed_sound_precache_warnings);
	}

	cl.wronggamedir_latewarned = true;
}

static void CL_FinalizeConnection(struct qsocket_s *netcon, const char *host)
{
	CL_ClearConnectReturnState();
	cls.netcon = netcon;
	Con_DPrintf("CL_EstablishConnection: connected to %s\n", host);

	cls.demonum = -1;
	cls.state = ca_connected;

	if ((cl_autodemo.value == 3 || cl_autodemo.value == 4) && cls.demorecording)
		Cbuf_AddText("stop\n");

	SCR_BeginLoadingPlaque();
	cl.protocol_dpdownload = false;
	cls.signon = 0;
	MSG_WriteByte(&cls.message, clc_nop);

	q_strlcpy(lastmphost, host, sizeof(lastmphost));
	Log_Last_Server_f();
	Write_Log(host, SERVERLIST);
	ServerList_Rebuild();
}

static void CL_CancelConnectInternal(qboolean clear_return_state)
{
	if (!cl_pending_connect.active && !NET_DatagramConnectPending())
		return;

	NET_DatagramConnectCancel();
	cl_pending_connect.active = false;
	cl_pending_connect.host[0] = '\0';

	if (clear_return_state)
		CL_ClearConnectReturnState();
}

void CL_CancelConnect(void)
{
	CL_CancelConnectInternal(true);
}

static qboolean CL_HandlePortPingProbe(const char *target)
{
	portpingprobe_status_t probe_status;
	qboolean is_local;

	if (!target || !*target)
		return false;

	if (!NET_PortPingProbe_IsEnabled())
	{
		if (NET_PortPingProbe_GetStatus() == PORTPINGPROBE_COMPLETED)
			NET_PortPingProbe_ConsumeCompleted(NULL);
		return false;
	}

	is_local = !q_strcasecmp(target, "local") || !q_strcasecmp(target, "localhost");
	if (is_local)
		return false;

	probe_status = NET_PortPingProbe_GetStatus();
	if (probe_status == PORTPINGPROBE_IDLE)
		return NET_PortPingProbe_Start(target);

	if (probe_status == PORTPINGPROBE_PROBING || probe_status == PORTPINGPROBE_ABORT)
	{
		NET_PortPingProbe_RequestAbort();
		Con_Printf("Port ping probe is still running; connect again in a moment\n");
		return true;
	}

	if (probe_status == PORTPINGPROBE_COMPLETED)
	{
		if (!NET_PortPingProbe_ConsumeCompleted(target))
			return NET_PortPingProbe_Start(target);
	}

	return false;
}

qboolean CL_BeginConnect(const char *host)
{
	const char *target;
	const char *connect_target;
	qboolean preserve_return_state;
	struct qsocket_s *immediate = NULL;

	if (cls.state == ca_dedicated || cls.demoplayback)
		return false;

	target = CL_PrepareConnectHost(host);
	if (!target)
		return false;

	if (CL_HandlePortPingProbe(target))
		return true;

	preserve_return_state = CL_ConsumeNextConnectFromMenu();
	connect_target = NET_ResolveCacheName(target);
	if (!preserve_return_state)
		CL_ClearConnectReturnState();

	CL_CancelConnectInternal(false);
	CL_Disconnect();
	CL_PrintConnectingMessage(target);

	immediate = NET_ConnectNoSlist(target, true);
	if (immediate)
	{
		CL_FinalizeConnection(immediate, target);
		return true;
	}

	if (!NET_DatagramConnectStart(connect_target))
	{
		CL_PrintConnectFailureHints();
		return false;
	}

	cl_pending_connect.active = true;
	q_strlcpy(cl_pending_connect.host, target, sizeof(cl_pending_connect.host));
	return true;
}

void CL_ConnectFrame(void)
{
	net_connect_result_t result;
	struct qsocket_s *netcon = NULL;
	const char *reason = NULL;

	if (!cl_pending_connect.active)
		return;

	result = NET_DatagramConnectFrame(&netcon, &reason);
	if (result == NET_CONNECT_PENDING)
		return;

	cl_pending_connect.active = false;

	if (result == NET_CONNECT_COMPLETE && netcon)
	{
		CL_FinalizeConnection(netcon, cl_pending_connect.host);
		cl_pending_connect.host[0] = '\0';
		return;
	}

	if (reason && *reason)
		Con_Printf("%s\n", reason);

	CL_PrintConnectFailureHints();
	cl_pending_connect.host[0] = '\0';
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
	NET_PortPingProbe_RequestAbort();
	CL_CancelConnect();
	CL_ClearTypingState();

	// Idempotent with CL_StopPlayback; needed for non-demo disconnect paths.
	Cvar_MapLock_RestoreAll ();
	V_MapScoped_RestoreServerStuff ();

	if (key_dest == key_message)
		Key_EndChat ();	// don't get stuck in chat mode

// stop sounds (especially looping!)
	S_StopAllSounds (true, false);
	BGM_Pause ();

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
	cls.download.percent = -1.0f;
	cl.intermission = 0;
	cl.worldmodel = NULL;
	cl.sendprespawn = false;
	memset(lastconnected, '\0', sizeof(lastconnected)); // woods #identify+
	cl.matchinp = 0; // woods
	cls.demo_had_overtime = false;
	cls.demo_marker_count = 0;
	cls.demo_record_frame_count = 0;
	netquakeio = false; // woods
	CL_ClearIgnoredChats();

	Info_SetKey(cls.userinfo, sizeof(cls.userinfo), "*mapmismatch", ""); // clear -- woods #mapcrc

	if (cl.modtype == 1 || cl.modtype == 4)
		Cbuf_AddText("setinfo observing off\n"); // woods
	pausedprint = false;  // woods
	cl.match_pause_time = 0; // woods
	prediction_msg_shown = false; // woods #prednotify
	V_ResetEffects ();
}

void CL_Disconnect_f (void)
{
	CL_Disconnect ();
	BGM_Stop ();
	CDAudio_Stop ();
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
	const char *target;
	struct qsocket_s *netcon;

	if (cls.state == ca_dedicated)
		return;

	if (cls.demoplayback)
		return;

	target = CL_PrepareConnectHost(host);
	if (!target)
		return;

	CL_CancelConnectInternal(false);
	CL_Disconnect ();
	CL_PrintConnectingMessage(target);

	netcon = NET_Connect(target);
	if (!netcon)
	{
		CL_PrintConnectFailureHints();
		Host_Error("connect failed");
	}
	CL_FinalizeConnection(netcon, target);
}

void CL_SendInitialUserinfo(void *ctx, const char *key, const char *val)
{
	if (*key == '*' && strcmp(key, "*ver"))
		return;	//servers don't like that sort of userinfo key

	if (!strcmp(key, "name"))
		return;	//already unconditionally sent earlier.
	MSG_WriteByte (&cls.message, clc_stringcmd);
	MSG_WriteString (&cls.message, va("setinfo \"%s\" \"%s\"\n", key, val));
}

Uint32 exec_connect_cfg (Uint32 interval, void* param) // woods #execdelay
{
	COM_ExecConfigFile("connect.cfg"); // exec some configs based on serverinfo, hybrid uses userinfo
	return 0; // only exec once
}

Uint32 exec_ctf_cfg (Uint32 interval, void* param) // woods #execdelay
{
	COM_ExecConfigFile("ctf.cfg"); // exec some configs based on serverinfo, hybrid uses userinfo
	return 0; // only exec once
}

Uint32 exec_dm_cfg (Uint32 interval, void* param) // woods #execdelay
{
	COM_ExecConfigFile("dm.cfg"); // exec some configs based on serverinfo, hybrid uses userinfo
	return 0; // only exec once
}

static qboolean CL_ServerinfoIsCTFPug (void)
{
	char mode[16];
	char playmode[16];

	Info_GetKey (cl.serverinfo, "mode", mode, sizeof(mode));
	Info_GetKey (cl.serverinfo, "playmode", playmode, sizeof(playmode));

	return (!q_strcasecmp(mode, "ctf") && !q_strcasecmp(playmode, "pug"));
}

static void CL_UpdatePlaymodeFromServerinfo (void)
{
	char playmode[16];
	const char *val;

	val = Info_GetKey (cl.serverinfo, "playmode", playmode, sizeof(playmode));
	cl.playmode = 0;

	if (!q_strcasecmp(val, "match"))
		cl.playmode = 1;
	else if (!q_strcasecmp(val, "ffa") || !q_strcasecmp(val, "pug") ||
		!q_strcasecmp(val, "normal") || !q_strcasecmp(val, "pub"))
		cl.playmode = 2;
	else if (!q_strcasecmp(val, "practice"))
		cl.playmode = 3;
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

		Info_SetKey(cls.userinfo, sizeof(cls.userinfo), "*ver", ENGINE_NAME_AND_VER); // woods, allow initial only #*ver

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
		cl.spawntime = cl.mtime[0];
		SCR_EndLoadingPlaque ();		// allow normal screen updates

		if (cl.gametype == GAME_DEATHMATCH && cls.state == ca_connected && !cl_ambient.value) // woods for no background sounds #stopsound
			Cmd_ExecuteString("stopsound\n", src_command);
		if (!cls.demoplayback && !cls.demorecording &&
			(cl_autodemo.value == 1 ||
				(cl_autodemo.value == 3 && cl.gametype == GAME_DEATHMATCH) ||
				cl_autodemo.value == 4))
		{
			Cbuf_AddText("record\n");
		}
		if (VID_HasMouseOrInputFocus())
			key_dest = key_game; // woods exit console on server connect
		maptime = SDL_GetTicks(); // woods connected map time #maptime

		if (registered.value == 0) // woods #pak0only
			Con_Printf("\n^mWarning:^m emulating shareware mode, install pak1.pak assets to enable all client features\n\n");

		qeintermission = false; // woods #qeintermission
		crxintermission = false; // woods #crxintermission
		pausedprint = false; // woods
		cl.match_pause_time = 0; // woods

		cl.realviewentity = cl.viewentity; // woods -- eyecam reports wrong viewentity, lets record real one

		strncpy(cl.observer, "n", sizeof(cl.observer));

		if (COM_ConfigFileExists("connect.cfg", NULL))
			SDL_AddTimer(900, exec_connect_cfg, NULL); // 2 sec delay after connect #execdelay

		const char* val;
		const char* val2;

		char buf[10]; // woods #modtype [crx/crmod server check]
		char bufn[16];
		val = Info_GetKey(cl.serverinfo, "mod", buf, sizeof(buf));
		val2 = Info_GetKey(cl.serverinfo, "modname", bufn, sizeof(bufn));
		if ((q_strcasestr(val, "crx") && val[0] != 'q') || q_strcasestr(val2, "crmod"))
		{
			cl.modtype = 1;
			strncpy(cl.observer, "n", sizeof(cl.observer));
		}

		char buf2[10]; // woods #modtype [FTE server check]
		val = Info_GetKey(cl.serverinfo, "*version", buf2, sizeof(buf2));
		if (strstr(val, "FTE"))
			cl.modtype = 5;

		char buf6[10]; // woods #servertype
		val = Info_GetKey(cl.serverinfo, "*version", buf6, sizeof(buf6));
		if (!strncmp(val, "QSS-M", 5))
			cl.server = 1;

		// woods lets detect the mode of the server for hybrid/nq crx

		char buf3[16];
		val = Info_GetKey(cl.serverinfo, "mode", buf3, sizeof(buf3));

		// woods lets 

		if (!q_strcasecmp(val, "ctf"))
		{
			cl.modetype = 1;
			if (COM_ConfigFileExists("ctf.cfg", NULL))
				SDL_AddTimer(1000, exec_ctf_cfg, NULL); // 2 sec delay after connect #execdelay
		}
		if (!strcmp(val, "dm") || !strcmp(val, "ffa"))
		{
			cl.modetype = 2;
			if (COM_ConfigFileExists("dm.cfg", NULL))
				SDL_AddTimer(1000, exec_dm_cfg, NULL); // 2 sec delay after connect #execdelay
		}
		if (!q_strcasecmp(val, "ra") || !q_strcasecmp(val, "rocketarena"))
			cl.modetype = 3;
		if (!q_strcasecmp(val, "ca") || !q_strcasecmp(val, "clanarena"))
			cl.modetype = 4;
		if (!q_strcasecmp(val, "airshot"))
			cl.modetype = 5;
		if (!q_strcasecmp(val, "wipeout"))
			cl.modetype = 6;
		if (!q_strcasecmp(val, "freezetag"))
			cl.modetype = 7;
		if (!q_strcasecmp(val, "headhunters"))
			cl.modetype = 8;

		// woods lets detect the playmode of the server for hybrid/nq crx
		CL_UpdatePlaymodeFromServerinfo ();

		const char* val3;
		char buf8[4];
		val3 = Info_GetKey(cl.serverinfo, "sv_fullpitch", buf8, sizeof(buf8)); // woods #pqfullpitch
		if (val3 && val3[0] != '\0')
		{
			if (strcmp(val3, "0") == 0)
				cl.fullpitch = 0;
			else
				cl.fullpitch = 1;
		}
		else
			cl.fullpitch = 1;

		retry_counter = 0; // woods #ms

		if (sv_mapcrc.value && !sv.active) // woods #mapcrc -- skip CRC validation for listen servers
		{
			// Validate map CRC now that we're fully connected and have complete serverinfo
			char crc_quick_str[64], crc_full_str[64];
			Con_DPrintf("Starting two-stage map CRC validation after full connection...\n");
			
			qboolean has_quick = Info_GetKey(cl.serverinfo, "*mapcrc_quick", crc_quick_str, sizeof(crc_quick_str)) && *crc_quick_str;
			qboolean has_full = Info_GetKey(cl.serverinfo, "*mapcrc_full", crc_full_str, sizeof(crc_full_str)) && *crc_full_str;
			
			if (has_quick && has_full)
			{
				cls.map_crc_quick_server = strtoul(crc_quick_str, NULL, 10);
				cls.map_crc_full_server = strtoul(crc_full_str, NULL, 10);
				Con_DPrintf("Server Quick CRC: %u, Full CRC: %u\n", cls.map_crc_quick_server, cls.map_crc_full_server);
				Con_DPrintf("Validating map: %s\n", cl.model_name[1]);
				
				// Validate map CRC - allow connection but track mismatch
				if (!CL_MapCRC_Validate(cl.model_name[1], cls.map_crc_quick_server, cls.map_crc_full_server))
				{
					// Set userinfo flag to indicate map mismatch
					Info_SetKey(cls.userinfo, sizeof(cls.userinfo), "*mapmismatch", "1");
					Con_Warning("Your map version differs from the server's version\n");
				}
				else
				{
					// Clear any previous mismatch flag
					Info_SetKey(cls.userinfo, sizeof(cls.userinfo), "*mapmismatch", "");
					Con_DPrintf("Map CRC validation passed - maps match\n");
				}
			}
			else
			{
				Con_DPrintf("Server did not provide complete CRC info (quick='%s', full='%s')\n", 
				          has_quick ? crc_quick_str : "(missing)", 
				          has_full ? crc_full_str : "(missing)");
				cls.map_crc_quick_server = 0;
				cls.map_crc_full_server = 0;
				// Clear mismatch flag when server doesn't provide complete CRC
				Info_SetKey(cls.userinfo, sizeof(cls.userinfo), "*mapmismatch", "");
			}
		}
		else if (sv_mapcrc.value)
		{
			Con_DPrintf("Skipping map CRC validation for listen server\n");
			cls.map_crc_quick_server = 0;
			cls.map_crc_full_server = 0;
			Info_SetKey(cls.userinfo, sizeof(cls.userinfo), "*mapmismatch", "");
		}

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
				dl->spawn = cl.mtime[0] - 0.001; // woods (iw) #democontrols
				return dl;
			}
		}
	}

// then look for anything else
	dl = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (dl->die < cl.time || (dl->spawn > cl.time && cl.protocol_pext2 && cls.demoplayback)) // woods (iw) #democontrols
		{
			memset (dl, 0, sizeof(*dl));
			dl->key = key;
			dl->color[0] = dl->color[1] = dl->color[2] = 1; //johnfitz -- lit support via lordhavoc
			dl->spawn = cl.mtime[0] - 0.001; // woods (iw) #democontrols
			return dl;
		}
	}

	dl = &cl_dlights[0];
	memset (dl, 0, sizeof(*dl));
	dl->key = key;
	dl->color[0] = dl->color[1] = dl->color[2] = 1; //johnfitz -- lit support via lordhavoc
	dl->spawn = cl.mtime[0] - 0.001; // woods (iw) #democontrols
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

	time = cl.time - cl.oldtime;
	if (time < 0)
		return;

	dl = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (dl->die < cl.time || (dl->spawn > cl.mtime[0] && cls.demoplayback) || !dl->radius) // woods (iw) #democontrols
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
	float	f, frac;

	f = cl.mtime[0] - cl.mtime[1];

	if (!f || cls.timedemo || (sv.active && !host_netinterval))
	{
		cl.time = cl.mtime[0];
		return 1;
	}

	if (f > 0.1) // dropped packet, or start of demo
	{
		cl.mtime[1] = cl.mtime[0] - 0.1;
		f = 0.1;
	}

	frac = (cl.time - cl.mtime[1]) / f;

	if (frac < 0)
	{
		if (frac < -0.01)
		{
		cl.time = cl.mtime[1];
		frac = 0;
		}
	}
	else if (frac > 1)
	{
		if (frac > 1.01)
		{
			cl.time = cl.mtime[0];
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

	if (ent->netstate.pmovetype && ent-cl.entities==cl.viewentity && qcvm->worldmodel && !cl_nopred.value && cls.signon == SIGNONS && cl.stats[STAT_HEALTH] > 0 && cl.ackedmovemessages > 0)
	{	//note: V_CalcRefdef will copy from cl.entities[viewent] to get its origin, so doing it here is the proper place anyway.
		if (!prediction_msg_shown && !cls.demoplayback) // woods #prednotify
		{
			prediction_msg_shown = true;
			Con_Printf("Server movement prediction enabled\n");
		}

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


/* --------------------------------------------------*/
/*    Client-side coloured player glows -- woods     */
/* --------------------------------------------------*/

/* Match dlight key (full entnum or low 8 bits — supports common forks) */
static qboolean CPG_KeyMatch(int k, int entnum)
{
	return (k == entnum) || (k == (entnum & 0xFF));
}

/* Kill any active dlights tied to entnum */
static void CPG_KillDlights(int entnum)
{
	for (int i = 0; i < MAX_DLIGHTS; ++i) {
		dlight_t* dl = &cl_dlights[i];
		if (!dl->die || dl->die <= cl.time) continue;
		if (CPG_KeyMatch(dl->key, entnum)) {
			dl->die = cl.time;
			dl->radius = 0;
		}
	}
}

/* Retint active dlights tied to entnum (RGB 0..1) */
static void CPG_TintDlights(int entnum, float r, float g, float b)
{
	for (int i = 0; i < MAX_DLIGHTS; ++i) {
		dlight_t* dl = &cl_dlights[i];
		if (!dl->die || dl->die <= cl.time) continue;
		if (CPG_KeyMatch(dl->key, entnum)) {
			dl->color[0] = r;
			dl->color[1] = g;
			dl->color[2] = b;
		}
	}
}

/*
	Apply local-player powerup tinting.
	Call once per frame *before* EF_DIMLIGHT dlights are spawned if possible.
*/
static void CL_ClientsidePowerupColor(entity_t* ent, int entnum)
{
	if (!r_coloredpowerupglow.value)
		return;

	if (cl.gametype == GAME_DEATHMATCH || cl.maxclients > 1)
		return;

	const qboolean is_local = (entnum == cl.viewentity);

	/* Non-local ents: only known player models */
	if (!is_local) {
		if (!ent->model || !ent->model->name[0]) return;
		if (strcmp(ent->model->name, "progs/player.mdl"))
			return;
	}

	/* Snapshot local inventory */
	const int items = cl.items;
	const qboolean have_quad = (items & IT_QUAD) != 0;
	const qboolean have_pent = (items & IT_INVULNERABILITY) != 0;
	const qboolean have_ring = (items & IT_INVISIBILITY) != 0;

	/* No powerups → no glow */
	if (!have_quad && !have_pent && !have_ring) {
		ent->effects &= ~(EF_DIMLIGHT | EF_RED | EF_BLUE);
		CPG_KillDlights(entnum);
		return;
	}

	/* Ring only → no glow */
	if (have_ring && !have_quad && !have_pent) {
		ent->effects &= ~(EF_DIMLIGHT | EF_RED | EF_BLUE);
		CPG_KillDlights(entnum);
		return;
	}

	/* Quad and/or Pent (Ring may also be present) */
	if (!(ent->effects & EF_DIMLIGHT))
		ent->effects |= EF_DIMLIGHT;

	ent->effects &= ~(EF_RED | EF_BLUE);

	if (have_quad && have_pent) {
		ent->effects |= (EF_RED | EF_BLUE);    /* purple */
		CPG_TintDlights(entnum, 1, 0, 1);
	}
	else if (have_quad) {
		ent->effects |= EF_BLUE;               /* blue */
		CPG_TintDlights(entnum, 0, 0, 1);
	}
	else { /* have_pent */
		ent->effects |= EF_RED;                /* red */
		CPG_TintDlights(entnum, 1, 0, 0);
	}
}

/* ------------------------------------------------------*/
/*    woods #demoeyes show player model for eyes entity  */
/* ------------------------------------------------------*/

#define DEMOEYES_ANIM_FPS        10.0f
#define DEMOEYES_SOURCE_MODEL    "progs/eyes.mdl"
#define DEMOEYES_PLAYER_MODEL    "progs/player.mdl"

/* Player model animation frames (from player.qc) */
#define DEMOEYES_AXRUN_FIRST     0
#define DEMOEYES_AXRUN_COUNT     6
#define DEMOEYES_ROCKRUN_FIRST   6
#define DEMOEYES_ROCKRUN_COUNT   6
#define DEMOEYES_STAND_FIRST     12
#define DEMOEYES_STAND_COUNT     5
#define DEMOEYES_AXSTND_FIRST    17
#define DEMOEYES_AXSTND_COUNT    12
#define DEMOEYES_AXPAIN_FIRST    29
#define DEMOEYES_AXPAIN_COUNT    6
#define DEMOEYES_PAIN_FIRST      35
#define DEMOEYES_PAIN_COUNT      6
#define DEMOEYES_NAILATT_FIRST   103
#define DEMOEYES_NAILATT_COUNT   2
#define DEMOEYES_LIGHT_FIRST     105
#define DEMOEYES_LIGHT_COUNT     2
#define DEMOEYES_ROCKATT_FIRST   107
#define DEMOEYES_ROCKATT_COUNT   6
#define DEMOEYES_SHOTATT_FIRST   113
#define DEMOEYES_SHOTATT_COUNT   6
#define DEMOEYES_AXATT_FIRST     119
#define DEMOEYES_AXATT_COUNT     6

/* Animation state for viewentity */
typedef enum {
	DEMOEYES_ANIM_NONE = 0,
	DEMOEYES_ANIM_ATTACK,
	DEMOEYES_ANIM_PAIN
} demoeyes_anim_type_t;

static struct {
	demoeyes_anim_type_t type;
	double start_time;
	int last_health;
	int first_frame;
	int frame_count;
	qmodel_t *cached_player_model;
} cl_demoeyes_state = {DEMOEYES_ANIM_NONE, 0.0, -1, 0, 0, NULL};

static qmodel_t *CL_DemoEyesFindPlayerModel(void)
{
	if (cl_demoeyes_state.cached_player_model && 
	    cl_demoeyes_state.cached_player_model->name[0] &&
	    !strcmp(cl_demoeyes_state.cached_player_model->name, DEMOEYES_PLAYER_MODEL))
		return cl_demoeyes_state.cached_player_model;

	for (int i = 0; i < MAX_MODELS; ++i) {
		qmodel_t *candidate = cl.model_precache[i];
		if (!candidate || !candidate->name[0])
			continue;
		if (!strcmp(candidate->name, DEMOEYES_PLAYER_MODEL)) {
			cl_demoeyes_state.cached_player_model = candidate;
			return candidate;
		}
	}

	return NULL;
}

static qboolean CL_DemoEyesIsObserving(void)
{
	if (cl.realviewentity < 1 || cl.realviewentity > cl.maxclients)
		return false;
	
	char buf1[32], buf2[32];
	const char *userinfo = CL_GetSafeRealViewEntityUserinfo();
	const char *obs = Info_GetKey(userinfo, "observer", buf1, sizeof(buf1));
	const char *star_obs = Info_GetKey(userinfo, "*observer", buf2, sizeof(buf2));
	
	if (!strcmp(obs, "eyecam") || !strcmp(obs, "chase") || !strcmp(obs, "fly") || !strcmp(obs, "walk") ||
	    !strcmp(star_obs, "eyecam") || !strcmp(star_obs, "chase") || !strcmp(star_obs, "fly") || !strcmp(star_obs, "walk"))
		return true;
	
	return false;
}

static qboolean CL_DemoEyesIsAxe(qmodel_t *weapon_model)
{
	return (weapon_model && weapon_model->name[0] && 
	        !strcmp(weapon_model->name, "progs/v_axe.mdl"));
}

static void CL_DemoEyesGetAttackAnim(qmodel_t *weapon_model, int *first, int *count)
{
	if (!weapon_model || !weapon_model->name[0]) {
		*first = DEMOEYES_ROCKATT_FIRST;
		*count = DEMOEYES_ROCKATT_COUNT;
		return;
	}
	
	if (!strcmp(weapon_model->name, "progs/v_axe.mdl")) {
		*first = DEMOEYES_AXATT_FIRST; *count = DEMOEYES_AXATT_COUNT;
	}
	else if (!strcmp(weapon_model->name, "progs/v_shot.mdl") || 
	         !strcmp(weapon_model->name, "progs/v_shot2.mdl")) {
		*first = DEMOEYES_SHOTATT_FIRST; *count = DEMOEYES_SHOTATT_COUNT;
	}
	else if (!strcmp(weapon_model->name, "progs/v_nail.mdl") || 
	         !strcmp(weapon_model->name, "progs/v_nail2.mdl")) {
		*first = DEMOEYES_NAILATT_FIRST; *count = DEMOEYES_NAILATT_COUNT;
	}
	else if (!strcmp(weapon_model->name, "progs/v_rock.mdl") || 
	         !strcmp(weapon_model->name, "progs/v_rock2.mdl")) {
		*first = DEMOEYES_ROCKATT_FIRST; *count = DEMOEYES_ROCKATT_COUNT;
	}
	else if (!strcmp(weapon_model->name, "progs/v_light.mdl")) {
		*first = DEMOEYES_LIGHT_FIRST; *count = DEMOEYES_LIGHT_COUNT;
	}
	else {
		*first = DEMOEYES_ROCKATT_FIRST; *count = DEMOEYES_ROCKATT_COUNT;
	}
}

static void CL_DemoEyesMaybeAnimate(entity_t *ent, int entnum)
{
	qmodel_t *current_model = ent->model;
	if (!current_model || !current_model->name[0])
		return;
	
	if (strcmp(current_model->name, DEMOEYES_SOURCE_MODEL) != 0)
		return;
	
	const float alpha_value = cl_demoeyes.value;
	if (alpha_value <= 0.0f)
		return;
	
	if (!cls.demoplayback && !CL_DemoEyesIsObserving())
		return;

	qmodel_t *player_model = CL_DemoEyesFindPlayerModel();
	if (!player_model)
		return;

	ent->model = player_model;
	float clamped_alpha = (alpha_value > 1.0f) ? 1.0f : alpha_value;
	ent->alpha = ENTALPHA_ENCODE(clamped_alpha);

	double now = cl.time;
	int cycle = (int)(now * DEMOEYES_ANIM_FPS);
	if (cycle < 0) cycle = 0;
	
	/* Get weapon model for viewentity */
	qmodel_t *weapon_model = NULL;
	qboolean is_axe = false;
	if (entnum == cl.viewentity) {
		int weapon_index = cl.stats[STAT_WEAPON];
		if (weapon_index > 0 && weapon_index < MAX_MODELS)
			weapon_model = cl.model_precache[weapon_index];
		is_axe = CL_DemoEyesIsAxe(weapon_model);
		
		/* Check for attack animation trigger */
		if (cl.stats[STAT_WEAPONFRAME] != 0) {
			if (cl_demoeyes_state.type != DEMOEYES_ANIM_ATTACK) {
				cl_demoeyes_state.type = DEMOEYES_ANIM_ATTACK;
				cl_demoeyes_state.start_time = now;
				CL_DemoEyesGetAttackAnim(weapon_model, 
					&cl_demoeyes_state.first_frame, &cl_demoeyes_state.frame_count);
			}
		}
		
		/* Check for pain animation trigger (health dropped by 5+) */
		int health = cl.stats[STAT_HEALTH];
		if (cl_demoeyes_state.last_health >= 0 && 
		    health <= cl_demoeyes_state.last_health - 5 &&
		    cl_demoeyes_state.type != DEMOEYES_ANIM_PAIN) {
			cl_demoeyes_state.type = DEMOEYES_ANIM_PAIN;
			cl_demoeyes_state.start_time = now;
			cl_demoeyes_state.first_frame = is_axe ? DEMOEYES_AXPAIN_FIRST : DEMOEYES_PAIN_FIRST;
			cl_demoeyes_state.frame_count = is_axe ? DEMOEYES_AXPAIN_COUNT : DEMOEYES_PAIN_COUNT;
		}
		cl_demoeyes_state.last_health = health;
		
		/* Check if current animation has finished */
		if (cl_demoeyes_state.type != DEMOEYES_ANIM_NONE) {
			int elapsed_frames = (int)((now - cl_demoeyes_state.start_time) * DEMOEYES_ANIM_FPS) + 1;
			if (elapsed_frames > cl_demoeyes_state.frame_count) {
				cl_demoeyes_state.type = DEMOEYES_ANIM_NONE;
				cl_demoeyes_state.start_time = now;
				
				/* Restart attack if still firing */
				if (cl.stats[STAT_WEAPONFRAME] != 0) {
					cl_demoeyes_state.type = DEMOEYES_ANIM_ATTACK;
					CL_DemoEyesGetAttackAnim(weapon_model,
						&cl_demoeyes_state.first_frame, &cl_demoeyes_state.frame_count);
				}
			}
		}
		
		/* Play current animation */
		if (cl_demoeyes_state.type != DEMOEYES_ANIM_NONE) {
			int frame_num = (int)((now - cl_demoeyes_state.start_time) * DEMOEYES_ANIM_FPS);
			if (frame_num >= cl_demoeyes_state.frame_count)
				frame_num = cl_demoeyes_state.frame_count - 1;
			if (frame_num < 0) frame_num = 0;
			ent->frame = cl_demoeyes_state.first_frame + frame_num;
			return;
		}
	}
	
	/* Default: run/stand animation based on speed */
	float speed = 0.0f;
	int playernum = entnum - 1;
	if (playernum >= 0 && playernum < cl.maxclients && 
	    cl.scores[playernum].tinfo.time > cl.time) {
		speed = cl.scores[playernum].tinfo.speed;
	}
	else {
		vec3_t move;
		VectorSubtract(ent->origin, ent->msg_origins[1], move);
		speed = sqrt(move[0]*move[0] + move[1]*move[1]) / host_frametime;
	}
	
	if (speed > 20.0f) {
		int first = is_axe ? DEMOEYES_AXRUN_FIRST : DEMOEYES_ROCKRUN_FIRST;
		int count = is_axe ? DEMOEYES_AXRUN_COUNT : DEMOEYES_ROCKRUN_COUNT;
		ent->frame = first + (cycle % count);
	}
	else {
		int first = is_axe ? DEMOEYES_AXSTND_FIRST : DEMOEYES_STAND_FIRST;
		int count = is_axe ? DEMOEYES_AXSTND_COUNT : DEMOEYES_STAND_COUNT;
		ent->frame = first + (cycle % count);
	}
}

enum
{
	CTFPUB_FLAG_RED_SKIN = 0,
	CTFPUB_FLAG_BLUE_SKIN = 1
};

static qboolean CL_CTFPugModelSwapActive (void)
{
	return (cl_ctf_pub_modelswap.value && CL_ServerinfoIsCTFPug ());
}

static qmodel_t *CL_CTFPugPrecacheModel (int precache)
{
	if (!CL_CTFPugModelSwapActive())
		return NULL;

	if (precache <= 0 || precache >= MAX_MODELS)
		return NULL;

	return cl.model_precache[precache];
}

static qboolean CL_CTFPugIsSilverKeyModel (qmodel_t *model)
{
	return (model && model->name[0] &&
		(!strcmp(model->name, "progs/w_s_key.mdl") ||
		 !strcmp(model->name, "progs/m_s_key.mdl") ||
		 !strcmp(model->name, "progs/b_s_key.mdl")));
}

static qboolean CL_CTFPugIsGoldKeyModel (qmodel_t *model)
{
	return (model && model->name[0] &&
		(!strcmp(model->name, "progs/w_g_key.mdl") ||
		 !strcmp(model->name, "progs/m_g_key.mdl") ||
		 !strcmp(model->name, "progs/b_g_key.mdl")));
}

static qmodel_t *CL_CTFPugTranslateModelAndSkin (qmodel_t *model, int *skinnum)
{
	if (!model || !model->name[0])
		return model;

	if (!strcmp(model->name, "progs/v_spike.mdl"))
	{
		qmodel_t *star_model = CL_CTFPugPrecacheModel (ctfpubhookprecache);
		if (star_model)
			return star_model;
	}
	else if (!strcmp(model->name, "progs/s_spike.mdl"))
	{
		qmodel_t *chain_model = CL_CTFPugPrecacheModel (ctfpubhookchainprecache);
		if (chain_model)
			return chain_model;
	}
	else if (!strcmp(model->name, "progs/v_axe.mdl"))
	{
		qmodel_t *weapon_model = CL_CTFPugPrecacheModel (ctfpubhookweaponprecache);
		if (weapon_model)
			return weapon_model;
	}
	else if (CL_CTFPugIsSilverKeyModel(model))
	{
		qmodel_t *flag_model = CL_CTFPugPrecacheModel (ctfpubflagprecache);
		if (flag_model)
		{
			if (skinnum)
				*skinnum = CTFPUB_FLAG_BLUE_SKIN;
			return flag_model;
		}
	}
	else if (CL_CTFPugIsGoldKeyModel(model))
	{
		qmodel_t *flag_model = CL_CTFPugPrecacheModel (ctfpubflagprecache);
		if (flag_model)
		{
			if (skinnum)
				*skinnum = CTFPUB_FLAG_RED_SKIN;
			return flag_model;
		}
	}

	return model;
}

qmodel_t *CL_CTFPugTranslateModel (qmodel_t *model)
{
	return CL_CTFPugTranslateModelAndSkin (model, NULL);
}

static qboolean CL_CTFPugModelIsSwapTargetForSource (qmodel_t *source_model, qmodel_t *current_model)
{
	if (!source_model || !current_model || !source_model->name[0] || !current_model->name[0])
		return false;

	if (!strcmp(source_model->name, "progs/v_spike.mdl"))
		return !strcmp(current_model->name, "progs/star.mdl");
	if (!strcmp(source_model->name, "progs/s_spike.mdl"))
		return !strcmp(current_model->name, "progs/bit.mdl");
	if (!strcmp(source_model->name, "progs/v_axe.mdl"))
		return !strcmp(current_model->name, "progs/v_star.mdl");
	if (CL_CTFPugIsSilverKeyModel(source_model) || CL_CTFPugIsGoldKeyModel(source_model))
		return !strcmp(current_model->name, "progs/flag.mdl");

	return false;
}

qboolean CL_CTFPugSwapEntityModel (entity_t *ent)
{
	qmodel_t *base_model;
	qmodel_t *translated_model;
	int base_skin;
	int translated_skin;

	if (!ent->model)
		return false;

	base_model = ent->model;
	base_skin = ent->skinnum;
	if (ent->netstate.modelindex > 0 && ent->netstate.modelindex < MAX_MODELS &&
		cl.model_precache[ent->netstate.modelindex])
	{
		base_model = cl.model_precache[ent->netstate.modelindex];
		base_skin = ent->netstate.skin;
	}

	translated_skin = base_skin;
	translated_model = CL_CTFPugTranslateModelAndSkin (base_model, &translated_skin);
	if (translated_model != base_model)
	{
		qboolean changed = (ent->model != translated_model || ent->skinnum != translated_skin);

		ent->model = translated_model;
		ent->skinnum = translated_skin;

		if (!strcmp(ent->model->name, "progs/flag.mdl"))
		{
			ent->syncbase = 0;
			ent->effects |= EF_NOSHADOW;
			ent->lerpflags |= LERP_RESETANIM;
		}

		return changed;
	}

	if (base_model != ent->model && CL_CTFPugModelIsSwapTargetForSource(base_model, ent->model))
	{
		ent->model = base_model;
		ent->skinnum = base_skin;
		if (!(ent->netstate.effects & EF_NOSHADOW))
			ent->effects &= ~EF_NOSHADOW;
		return true;
	}

	return false;
}

static int CL_EntityModelFlags (const entity_t *ent)
{
	int modelflags;

	if (!ent)
		return 0;

	modelflags = (ent->effects >> 24) & 0xff;
	if (ent->model && !(ent->effects & EF_NOMODELFLAGS))
		modelflags |= ent->model->flags;

	return modelflags;
}

static qboolean CL_IsSinglePlayerCTFFallbackStaticModel (const entity_t *ent)
{
	const char *name;

	if (!ent || !ent->model || !ent->model->name[0])
		return false;

	name = ent->model->name;
	return (!strcmp(name, "progs/flag.mdl") ||
		!strcmp(name, "progs/ctfmodel.mdl") ||
		!strcmp(name, "progs/flag2.mdl") ||
		!strcmp(name, "progs/flag3.mdl") ||
		!strcmp(name, "progs/w_s_key.mdl") ||
		!strcmp(name, "progs/w_g_key.mdl") ||
		!strcmp(name, "progs/m_s_key.mdl") ||
		!strcmp(name, "progs/m_g_key.mdl") ||
		!strcmp(name, "progs/b_s_key.mdl") ||
		!strcmp(name, "progs/b_g_key.mdl"));
}

qboolean CL_ViewingQ3ItemBobbingItem (void)
{
	int i;

	if (cls.state != ca_connected || cls.signon != SIGNONS || !cl.worldmodel || !r_drawentities.value)
		return false;

	for (i = 0; i < cl_numvisedicts; i++)
	{
		entity_t *ent = cl_visedicts[i];

		if (!ent || !ent->model || ent->model->needload)
			continue;
		if (ent->eflags & EFLAGS_EXTERIORMODEL)
			continue;
		if (!(CL_EntityModelFlags (ent) & EF_ROTATE))
			continue;
		if (R_CullModelForEntity (ent))
			continue;

		return true;
	}

	return false;
}

/*
===============
CL_RelinkEntities
===============
*/
#define CL_CTF_FALLBACK_STATIC_DLIGHT_Z 24.f
#define CL_CTF_FALLBACK_STATIC_DLIGHT_RADIUS 256.f

static void CL_AddSinglePlayerCTFFallbackStaticLights (void)
{
	entity_t	*ent;
	dlight_t	*dl;
	unsigned int	effects;
	int		i;

	if (cl.maxclients != 1)
		return;

	// Static entities do not pass through the normal per-entity effect light path.
	for (i = 0; i < cl.num_statics; i++)
	{
		ent = cl.static_entities[i].ent;
		if (!ent || !ent->model)
			continue;

		effects = ent->baseline.effects & (EF_RED | EF_GREEN | EF_BLUE);
		if (!effects)
		{
			if (!CL_IsSinglePlayerCTFFallbackStaticModel (ent))
				continue;

			if (ent->baseline.colormap == STATIC_COLORMAP_CTF_RED)
				effects = EF_RED;
			else if (ent->baseline.colormap == STATIC_COLORMAP_CTF_BLUE)
				effects = EF_BLUE;
		}
		if (!effects)
			continue;

		dl = CL_AllocDlight (-(MAX_EDICTS + i + 1));
		VectorCopy (ent->origin, dl->origin);
		dl->origin[2] += CL_CTF_FALLBACK_STATIC_DLIGHT_Z;
		dl->radius = CL_CTF_FALLBACK_STATIC_DLIGHT_RADIUS;
		dl->die = cl.time + 0.1;
		if (r_coloredpowerupglow.value)
		{
			dl->color[0] = !!(effects & EF_RED);
			dl->color[1] = !!(effects & EF_GREEN);
			dl->color[2] = !!(effects & EF_BLUE);
		}
	}
}

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
	qboolean	hidden_viewentity;
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
			InvalidateTraceLineCache();
			continue;
		}

		if (ent->spawntime > cl.mtime[0] && (cls.demoplayback && cl.protocol_pext2)) // woods (iw) #democontrols
		{
			ent->model = NULL;
			ent->lerpflags |= LERP_RESETMOVE | LERP_RESETANIM;
			InvalidateTraceLineCache();

			continue;
		}

		hidden_viewentity = (i == cl.viewentity && !chase_active.value);
		VectorCopy (ent->origin, oldorg);

		CL_ClientsidePowerupColor(ent, i); // woods
		CL_DemoEyesMaybeAnimate(ent, i); // woods #demoeyes
		CL_CTFPugSwapEntityModel(ent); // woods #ctfpubmodels

		if (CL_LerpEntity(ent, ent->origin, ent->angles, frac))
			ent->lerpflags |= LERP_RESETMOVE;

		if (ent->netstate.tagentity)
		if (!CL_AttachEntity(ent, frac))
		{
			//can't draw it if we don't know where its parent is.
			continue;
		}

		modelflags = CL_EntityModelFlags (ent);

		if (hidden_viewentity && cl.stats[STAT_HEALTH] <= 0)
		{
			PScript_DelinkTrailstate(&ent->trailstate);
			PScript_DelinkTrailstate(&ent->emitstate);
			ent->forcelink = false;
			continue;
		}

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
				modelflags -= EF_ROCKET;
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
			if (r_rocketlight.value) // woods eliminate rocket light #rocketlight
			{
				dl = CL_AllocDlight (i);
				VectorCopy (ent->origin, dl->origin);
				dl->radius = 200 * (bound(0, r_rocketlight.value, 1));
				dl->die = cl.time + 0.01;
			}
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

		if (hidden_viewentity)
			continue;

		// woods #demoeyecam - hide chased player model when rendering demo eyecam
		if (cls.demoplayback && cl_demo_eyecam.value && cl.demo_eyecam_target > 0 && i == cl.demo_eyecam_target)
			continue;

		if (cl_numvisedicts < cl_maxvisedicts)
		{
			cl_visedicts[cl_numvisedicts] = ent;
			cl_numvisedicts++;
		}
	}

	CL_AddSinglePlayerCTFFallbackStaticLights ();

	// viewmodel. last, for transparency reasons.
	ent = &cl.viewent;

	// woods #demoeyecam - try to show weapon for demo eyecam target
	if (cls.demoplayback && cl_demo_eyecam.value && cl.demo_eyecam_target > 0)
	{
		int target = cl.demo_eyecam_target;
		int playernum = target - 1;
		qboolean have_weapon = false;
		const char *weapon_name = NULL;

		// Avoid carrying stale weapon models between target changes.
		ent->model = NULL;

		// Try to derive weapon from target player items (teaminfo feed).
		if (playernum >= 0 && playernum < cl.maxclients
			&& cl.scores[playernum].tinfo.time > cl.time - 1.0)
		{
			int items = cl.scores[playernum].tinfo.items;
			int j;

			// Pick best available weapon.
			if (items & IT_LIGHTNING)
				weapon_name = "progs/v_light.mdl";
			else if (items & IT_ROCKET_LAUNCHER)
				weapon_name = "progs/v_rock2.mdl";
			else if (items & IT_GRENADE_LAUNCHER)
				weapon_name = "progs/v_rock.mdl";
			else if (items & IT_SUPER_NAILGUN)
				weapon_name = "progs/v_nail2.mdl";
			else if (items & IT_NAILGUN)
				weapon_name = "progs/v_nail.mdl";
			else if (items & IT_SUPER_SHOTGUN)
				weapon_name = "progs/v_shot2.mdl";
			else if (items & IT_SHOTGUN)
				weapon_name = "progs/v_shot.mdl";
			else
				weapon_name = "progs/v_axe.mdl";

			// Resolve model from precache table.
			for (j = 1; j < MAX_MODELS; j++)
			{
				if (cl.model_precache[j] && !strcmp(cl.model_precache[j]->name, weapon_name))
				{
					ent->model = cl.model_precache[j];
					have_weapon = true;
					break;
				}
			}
		}

		// In eyecam, draw a weapon model when available (ignore observer health).
		if (r_drawviewmodel.value && !chase_active.value && have_weapon && scr_viewsize.value < 130)
		{
			if (cl_numvisedicts < cl_maxvisedicts)
			{
				cl_visedicts[cl_numvisedicts] = ent;
				cl_numvisedicts++;
			}
		}
	}
	else if (r_drawviewmodel.value
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

#define MAX_URLPATH 1024
#define BYTES_TO_KB(bytes) ((bytes) / 1024.0f)
#define BYTES_TO_MB(bytes) ((bytes) / (1024.0 * 1024.0))

typedef struct 
{
	char filename[MAX_OSPATH];
	char url[MAX_URLPATH];
        qboolean is_skybox;
        char display_name[64];
} DownloadData;

qboolean web2check = false;
qboolean webcheck = false;
static qboolean qwmaplist_webcheck = false;
static qboolean qwmaplist_webcheck_started = false;
qboolean stop_curl_download = false;
qboolean curl_download_active = false;
qboolean downloadedctf = false;

typedef struct {
	char* url;
	int web;
} ThreadData;

SDL_Thread* currentWebCheckThread = NULL;
SDL_Thread* currentWeb2CheckThread = NULL;

#define QW_MAPLIST_SOURCE_HOST "maps.quakeworld.nu"
#define QW_MAPLIST_SOURCE_URL "https://maps.quakeworld.nu/all/"
#define MENU_DOWNLOAD_REDRAW_INTERVAL_MS 50


qboolean IsGithubRepoPath(const char* s)
{
	if (!s)
		return false;

	if (strstr(s, "://"))
		return false;

	const char* first = strchr(s, '/');
	if (!first)
		return false;

	// Now accepts both user/repo and user/repo/branch patterns
	return true; // Must have at least 1 slash (user/repo)
}

/*
==============================================================================
* NormalizeGithubRepoPath
*     Ensures GitHub repo paths have "/main" appended if they only have user/repo
*     e.g., "q1tools/q1tools.github.io" becomes "q1tools/q1tools.github.io/main"
*     Returns false if the normalized value would be truncated.
==============================================================================
*/
static qboolean NormalizeGithubRepoPath(const char* input, char* output, size_t output_size)
{
	if (!input || !output || output_size == 0)
        return false;

	// Count slashes to determine if we need to append "/main"
	int slash_count = 0;
	for (const char* p = input; *p; ++p)
	{
		if (*p == '/')
			slash_count++;
	}

	// If we have exactly 1 slash (user/repo), append "/main"
	if (slash_count == 1)
	{
        return (size_t)q_snprintf(output, output_size, "%s/main", input) < output_size;
	}
	else
	{
		// Otherwise, use as-is
        return q_strlcpy(output, input, output_size) < output_size;
	}
}

static const char *CL_DownloadUrlPathSeparator(const char *base)
{
    size_t len = strlen(base);
    return (len > 0 && base[len - 1] == '/') ? "" : "/";
}

static inline const char* DL_DisplayTag(const char* base, char* buf, size_t bufsz)
{
	if (IsGithubRepoPath(base))
	{
		const char* s1 = strchr(base, '/');        /* after user/ */
		const char* s2 = strchr(s1 + 1, '/');      /* after repo/ */
		size_t len = s2 ? (size_t)(s2 - s1 - 1) : strlen(s1 + 1);
		if (len >= bufsz) len = bufsz - 1;
		memcpy(buf, s1 + 1, len);
		buf[len] = '\0';
		return buf;                                /* e.g. "q1tools.github.io" */
	}
	return base;                                   /* e.g. "maps.quakeworld.nu" */
}

static qboolean CL_DownloadUrlUsesQWMapListHost(const char *url)
{
	const char *host;
	const char *end;
	size_t host_len;

	if (!url || !*url)
		return false;

	host = url;
	if (!q_strncasecmp(host, "https://", 8))
		host += 8;
	else if (!q_strncasecmp(host, "http://", 7))
		host += 7;

	if (!*host || *host == '/')
		return false;

	end = host;
	while (*end && *end != '/' && *end != ':' && *end != '?' && *end != '#')
		++end;

	host_len = end - host;
	return host_len == strlen(QW_MAPLIST_SOURCE_HOST) &&
		!q_strncasecmp(host, QW_MAPLIST_SOURCE_HOST, host_len);
}

static const char *WebCheckLabel(int web)
{
	switch (web)
	{
	case 1:
		return "cl_web_download_url";
	case 2:
		return "cl_web_download_url2";
	case 3:
		return "qwmaplist source";
	default:
		return "web download URL";
	}
}

static void WebCheckSetResult(int web, qboolean active)
{
	switch (web)
	{
	case 1:
		webcheck = active;
		break;
	case 2:
		web2check = active;
		break;
	case 3:
		qwmaplist_webcheck = active;
		break;
	}
}

/*
==============================================================================
*  GithubExtractUserRepo
*     Splits "user/repo[/branch[/dir]]" into USER and REPO.
*     Returns false if the input does not match that pattern.
==============================================================================
*/
static qboolean GithubExtractUserRepo(const char *in,
                                      char *user, size_t usz,
                                      char *repo, size_t rsz)
{
    if (!IsGithubRepoPath(in))
        return false;

    /* 1st slash separates USER / REPO */
    const char *slash1 = strchr(in, '/');
    size_t ulen = slash1 - in;                 /* bytes before first '/' */

    /* REPO ends at next slash (branch) or end of string */
    const char *slash2 = strchr(slash1 + 1, '/');
    size_t rlen = slash2 ? (size_t)(slash2 - slash1 - 1)
                         : strlen(slash1 + 1);

    /* sanity-check lengths and buffers */
    if (ulen == 0 || rlen == 0 || ulen >= usz || rlen >= rsz)
        return false;

    memcpy(user, in, ulen);          user[ulen]  = '\0';
    memcpy(repo, slash1 + 1, rlen);  repo[rlen]  = '\0';
    return true;
}


int checkWebsite (void* ptr)  // ping the potential websites in advance
{
	ThreadData* data = (ThreadData*)ptr;

	if (data == NULL || data->url == NULL)
	{
		free(data);
		return -1;
	}

	for (const char* p = data->url; *p; ++p)
		if ((unsigned char)*p <= 32 || ((unsigned char)*p & 0x80))
		{
			WebCheckSetResult(data->web, false);
			free(data->url);
			free(data);
			return 0;
		}

	/* user/repo[/branch] names may only use A-Z a-z 0-9 . _ - and / */
	if (IsGithubRepoPath(data->url))
	{
		for (const char* p = data->url; *p; ++p)
			if (!isalnum((unsigned char)*p) &&
				*p != '/' && *p != '.' && *p != '-' && *p != '_')
			{
				WebCheckSetResult(data->web, false);
				free(data->url);
				free(data);
				return 0;
			}
	}

	CURL* curl = curl_easy_init();
	if (curl == NULL) {
		WebCheckSetResult(data->web, false);
		free(data->url);
		free(data);
		return -1;
	}

	char fullurl[MAX_URLPATH];
	char user[64], repo[64];

	// Normalize the URL to include /main if needed for GitHub repo paths
	char normalized_url[MAX_URLPATH];
	if (IsGithubRepoPath(data->url))
	{
		if (!NormalizeGithubRepoPath(data->url, normalized_url, sizeof(normalized_url)) ||
			!GithubExtractUserRepo(normalized_url, user, sizeof(user), repo, sizeof(repo)) ||
			(size_t)q_snprintf(fullurl, sizeof(fullurl), "https://github.com/%s/%s", user, repo) >= sizeof(fullurl))
			goto invalid_url;
	}
	else if (!strstr(data->url, "://"))
	{
		if ((size_t)q_snprintf(fullurl, sizeof(fullurl), "https://%s/", data->url) >= sizeof(fullurl))
			goto invalid_url;
	}
	else
	{
		if (q_strlcpy(fullurl, data->url, sizeof(fullurl)) >= sizeof(fullurl))
			goto invalid_url;
	}

	curl_easy_setopt(curl, CURLOPT_URL, fullurl);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HEAD request
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

	CURLcode res = curl_easy_perform(curl);
	if (res == CURLE_OK)
	{
		WebCheckSetResult(data->web, true);
	}
	else
	{
		Con_DPrintf("%s %s is not responsive: %s\n",
			WebCheckLabel(data->web), data->url, curl_easy_strerror(res));
		WebCheckSetResult(data->web, false);
	}

	curl_easy_cleanup(curl);

	free(data->url);
	free(data);

    return 0;

invalid_url:
    Con_DPrintf("%s %s is invalid or too long\n", WebCheckLabel(data->web), data->url);
    WebCheckSetResult(data->web, false);

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

static qboolean QWMapListSourceIsConfiguredWebDownload(void)
{
	return CL_DownloadUrlUsesQWMapListHost(cl_web_download_url.string) ||
		CL_DownloadUrlUsesQWMapListHost(cl_web_download_url2.string);
}

static qboolean CL_AnyWebDownloadServerActive(void)
{
	return (webcheck && cl_web_download_url.string != NULL && cl_web_download_url.string[0] != '\0') ||
		(web2check && cl_web_download_url2.string != NULL && cl_web_download_url2.string[0] != '\0');
}

static void QWMapListWebCheckStart(void)
{
	SDL_Thread *thread;

	if (cls.state == ca_dedicated)
		return;

	if (QWMapListSourceIsConfiguredWebDownload())
		return;

	if (qwmaplist_webcheck_started)
		return;

	qwmaplist_webcheck = false;
	qwmaplist_webcheck_started = true;
	thread = webDownloadCheck(QW_MAPLIST_SOURCE_URL, 3);
	if (thread != NULL)
		SDL_DetachThread(thread);
	else
		qwmaplist_webcheck_started = false;
}

static void QWMapListWebCheckReset(void)
{
	qwmaplist_webcheck = false;
	qwmaplist_webcheck_started = false;
}

void CL_QWMapListDownloadsRetry(void)
{
	if (cls.state == ca_dedicated)
		return;

	if (currentWebCheckThread != NULL)
	{
		SDL_WaitThread(currentWebCheckThread, NULL);
		currentWebCheckThread = NULL;
	}

	webcheck = false;
	if (cl_web_download_url.string != NULL && cl_web_download_url.string[0] != '\0')
		currentWebCheckThread = webDownloadCheck(cl_web_download_url.string, 1);

	if (currentWeb2CheckThread != NULL)
	{
		SDL_WaitThread(currentWeb2CheckThread, NULL);
		currentWeb2CheckThread = NULL;
	}

	web2check = false;
	if (cl_web_download_url2.string != NULL && cl_web_download_url2.string[0] != '\0')
		currentWeb2CheckThread = webDownloadCheck(cl_web_download_url2.string, 2);

	QWMapListWebCheckReset();
	QWMapListWebCheckStart();
}

qboolean CL_QWMapListDownloadsAvailable(void)
{
	qboolean url1_maps = CL_DownloadUrlUsesQWMapListHost(cl_web_download_url.string);
	qboolean url2_maps = CL_DownloadUrlUsesQWMapListHost(cl_web_download_url2.string);
	qboolean source_available;

	if (cls.state == ca_dedicated)
		return false;

	if (url1_maps || url2_maps)
		source_available = (url1_maps && webcheck) || (url2_maps && web2check);
	else
	{
		QWMapListWebCheckStart();
		source_available = qwmaplist_webcheck;
	}

	return source_available && CL_AnyWebDownloadServerActive();
}

void WebCheckCallback_f (cvar_t* var)
{
	if (currentWebCheckThread != NULL)
	{
		SDL_WaitThread(currentWebCheckThread, NULL); // Wait for the current thread to finish
		currentWebCheckThread = NULL; // Reset the pointer
	}

	webcheck = false;
	QWMapListWebCheckReset();

	if (cl_web_download_url.string != NULL && cl_web_download_url.string[0] != '\0')
	{
		currentWebCheckThread = webDownloadCheck(cl_web_download_url.string, 1);
	}

	QWMapListWebCheckStart();
}

void Web2CheckCallback_f (cvar_t* var)
{
	if (currentWeb2CheckThread != NULL) 	// Wait for the current thread to finish, if it exists
	{
		SDL_WaitThread(currentWeb2CheckThread, NULL); // Wait for the current thread to complete
		currentWeb2CheckThread = NULL; // Reset the pointer
	}

	web2check = false;
	QWMapListWebCheckReset();

	if (cl_web_download_url2.string != NULL && cl_web_download_url2.string[0] != '\0')
	{
		currentWeb2CheckThread = webDownloadCheck(cl_web_download_url2.string, 2);
	}

	QWMapListWebCheckStart();
}

void WebCheckInit (void) // runs at launch in CL_Init if default values
{
	char* webearly = NULL;
	char* web2early = NULL;
	qboolean checked_web = false;
	qboolean checked_web2 = false;

	if (CFG_OpenConfig("config.cfg") == 0) // get these early config values
	{
		webearly = CFG_ReadCvarValue("cl_web_download_url");
		web2early = CFG_ReadCvarValue("cl_web_download_url2");
		CFG_CloseConfig();
	}

	if (webearly != NULL && webearly[0] != '\0')
		if (!strcmp(webearly, cl_web_download_url.default_string))
		{
			WebCheckCallback_f(&cl_web_download_url);
			checked_web = true;
		}
	if (web2early != NULL && web2early[0] != '\0')
		if (!strcmp(web2early, cl_web_download_url2.default_string))
		{
			Web2CheckCallback_f(&cl_web_download_url2);
			checked_web2 = true;
		}

	if (!checked_web && webearly == NULL &&
		cl_web_download_url.string != NULL && cl_web_download_url.string[0] != '\0' &&
		!strcmp(cl_web_download_url.string, cl_web_download_url.default_string))
	{
		WebCheckCallback_f(&cl_web_download_url);
		checked_web = true;
	}

	if (!checked_web2 && web2early == NULL &&
		cl_web_download_url2.string != NULL && cl_web_download_url2.string[0] != '\0' &&
		!strcmp(cl_web_download_url2.string, cl_web_download_url2.default_string))
	{
		Web2CheckCallback_f(&cl_web_download_url2);
		checked_web2 = true;
	}

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

	QWMapListWebCheckStart();
}

void CL_InitWebDownloads(qboolean run_checks)
{
    if (!(cl_web_download_url.flags & CVAR_REGISTERED))
        Cvar_RegisterVariable (&cl_web_download_url); // woods #webdl
    if (!(cl_web_download_url2.flags & CVAR_REGISTERED))
        Cvar_RegisterVariable (&cl_web_download_url2); // woods #webdl

    if (!run_checks)
        return;

    Cvar_SetCallback (&cl_web_download_url, &WebCheckCallback_f); // woods #webdl
    Cvar_SetCallback (&cl_web_download_url2, &Web2CheckCallback_f); // woods #webdl

    WebCheckInit (); // woods -- check if the web download servers are live at launch (threaded) #webdl
}

static void CL_DownloadProgress_Begin(const char *filename)
{
	cls.download.active = true;
	cls.download.percent = -1.0f;
	cls.download.received = 0.0;
	cls.download.total = 0.0;
	cls.download.starttime = (float)realtime;
	if (filename)
		q_strlcpy(cls.download.current, filename, sizeof(cls.download.current));
}

static void CL_DownloadProgress_Update(double received, double total)
{
	cls.download.received = received > 0.0 ? received : 0.0;
	cls.download.total = total > 0.0 ? total : 0.0;

	if (cls.download.total > 0.0)
	{
		double percent = (cls.download.received * 100.0) / cls.download.total;
		if (percent < 0.0)
			percent = 0.0;
		else if (percent > 100.0)
			percent = 100.0;
		cls.download.percent = (float)percent;
	}
	else
		cls.download.percent = -1.0f;
}

static void CL_DownloadProgress_UpdateMenuScreen(void)
{
	static Uint32 last_update_time = 0;
	Uint32 now;

	if (isDedicated || scr_disabled_for_loading)
		return;

	SDL_PumpEvents();

	if (key_dest != key_menu)
		return;

	now = SDL_GetTicks();
	if (last_update_time && now - last_update_time < MENU_DOWNLOAD_REDRAW_INTERVAL_MS)
		return;

	last_update_time = now;
	SCR_UpdateScreen();
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

	CL_DownloadProgress_Update((double)dlnow, (double)dltotal);

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
			if (dataFromCurl->is_skybox && dataFromCurl->display_name[0] != '\0')
				Q_strncpy(urlLimited, dataFromCurl->display_name, 20);
			else
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
		}

		CL_DownloadProgress_UpdateMenuScreen();
	}
	return 0;
}

static qboolean CL_DownloadNameIsValid(const char *relative_path)
{
	return COM_DownloadNameOkay(relative_path) || COM_DownloadPackageNameOkay(relative_path);
}

static void CL_DownloadAddMapDesc(const char *relative_path)
{
	if (!q_strcasecmp(COM_FileGetExtension(relative_path), "bsp"))
	{
		char mapname[MAX_QPATH];
		COM_StripExtension(COM_SkipPath(relative_path), mapname, sizeof(mapname));
		FileList_Add_MapDesc(mapname); // #mapdescriptions
	}
}

static qboolean CL_FinalizeDownloadFile(const char *relative_path, const char *temp_path)
{
	char finalpath[MAX_OSPATH];
	const char *extension;
	qboolean is_package;
	qboolean renameokay;
	int rename_errno;

	if (!CL_DownloadNameIsValid(relative_path))
	{
		Con_Warning("Rejected downloaded filename \"%s\"\n", relative_path ? relative_path : "(null)");
		return false;
	}

    if ((size_t)q_snprintf(finalpath, sizeof(finalpath), "%s/%s", com_gamedir, relative_path) >= sizeof(finalpath))
    {
        Con_Warning("Download path too long for \"%s\"\n", relative_path);
        return false;
    }

	extension = COM_FileGetExtension(relative_path);
	is_package = COM_IsPackageExtension(extension) && COM_DownloadPackageNameOkay(relative_path);
	renameokay = false;
	rename_errno = 0;

	if (is_package)
		COM_RemoveDownloadedPackage(relative_path);

	if (rename(temp_path, finalpath) == 0)
		renameokay = true;
	else
	{
		unlink(finalpath);
		if (rename(temp_path, finalpath) == 0)
			renameokay = true;
		else
			rename_errno = errno;
	}

	if (!renameokay)
	{
		if (is_package && (Sys_FileType(finalpath) & FS_ENT_FILE))
			COM_AddDownloadedPackage(relative_path);
		Con_Warning("Failed to finalize download \"%s\" (%s)\n", finalpath, strerror(rename_errno));
		return false;
	}

	if (is_package)
		COM_AddDownloadedPackage(relative_path);

	CL_DownloadAddMapDesc(relative_path);
	return true;
}

qboolean Curl_DownloadFile (const char* url, const char* filename, const char* local_path, qboolean is_skybox, const char* display_name) // main curl function
{
	stop_curl_download = false;
	CL_DownloadProgress_Begin(filename);
	curl_download_active = true;

	if (url == NULL || url[0] == '\0')
	{
		cls.download.active = false;
		curl_download_active = false;
		return false;
	}

	char full_url[MAX_URLPATH];
	int full_url_len = -1;
	const char* skipped_path = COM_SkipPath(filename);


	if (IsGithubRepoPath(url))
	{
		/* Normalize the URL to include /main if needed */
		char normalized_url[MAX_URLPATH];
		if (!NormalizeGithubRepoPath(url, normalized_url, sizeof(normalized_url)))
			goto url_too_long;

		/* Build the common prefix once */
		char repo_base[MAX_URLPATH];
		if ((size_t)q_snprintf(repo_base, sizeof(repo_base),
			"https://raw.githubusercontent.com/%s/", normalized_url) >= sizeof(repo_base))
			goto url_too_long;

		/* 1.  Skyboxes -> skyboxes/<face>.(tga|png|...) */
		if (is_skybox && !strncmp(filename, "gfx/env/", 8))
		{
			/* skip the "gfx/env/" part */
			full_url_len = q_snprintf(full_url, sizeof(full_url),
				"%sgfx/env/%s", repo_base, filename + 8);
		}
		/* 2.  Maps -> maps/<A-Z or 0-9>/<basename>.bsp */
		else if (!strncmp(filename, "maps/", 5))
		{
			const char* base = COM_SkipPath(filename);   /* DM4.bsp */
			char directory[5];

			if (isdigit((unsigned char)base[0]))
				strcpy(directory, "0-9/");
			else {
				directory[0] = (char)toupper((unsigned char)base[0]);
				directory[1] = '/';
				directory[2] = '\0';
			}

            full_url_len = q_snprintf(full_url, sizeof(full_url),
				"%smaps/%s%s", repo_base, directory, base);
		}
		/* 3.  Everything else -> branch/<original path> */
		else
		{
            full_url_len = q_snprintf(full_url, sizeof(full_url),
				"%s%s", repo_base, filename);
		}
	}

    else if (strstr(url, "github.io") && !strncmp(filename, "maps/", 5)) // special case for github.io
	{
        const char *githubio_host = url;
		char directory[5];

        if (!strncmp(githubio_host, "https://", 8))
            githubio_host += 8;
        else if (!strncmp(githubio_host, "http://", 7))
            githubio_host += 7;

		if (isdigit((unsigned char)skipped_path[0]))
		{
			strcpy(directory, "0-9/"); // If the filename starts with a digit, use '#' directory
		}
		else {
			directory[0] = toupper(skipped_path[0]); // Extract the first letter and make it uppercase
			directory[1] = '/';
			directory[2] = '\0';
		}

		full_url_len = q_snprintf(full_url, sizeof(full_url), "https://%s%smaps/%s/%s",
			githubio_host, CL_DownloadUrlPathSeparator(githubio_host), directory, skipped_path); // Construct the URL with directory
	}

	else if (strstr(url, "maps.quakeworld.nu")) // special cases for maps.quakeworld.nu
	{
		if (!q_strcasecmp(COM_FileGetExtension(filename), "loc"))
			full_url_len = q_snprintf(full_url, sizeof(full_url), "https://%s/%s", "maps.quakeworld.nu", filename);
		else
			full_url_len = q_snprintf(full_url, sizeof(full_url), "https://%s/%s", "maps.quakeworld.nu/all", skipped_path); // use secure https and skip path
	}
	else if (strstr(url, "://"))
		full_url_len = q_snprintf(full_url, sizeof(full_url), "%s%s%s",
			url, CL_DownloadUrlPathSeparator(url), filename);
	else
		full_url_len = q_snprintf(full_url, sizeof(full_url), "https://%s%s%s",
			url, CL_DownloadUrlPathSeparator(url), filename); // use secure https

	if (full_url_len < 0 || (size_t)full_url_len >= sizeof(full_url))
		goto url_too_long;

	DownloadData dl_data;
	memset(&dl_data, 0, sizeof(dl_data)); // Reset dl_data
	Q_strncpy(dl_data.filename, filename, MAX_OSPATH);
	Q_strncpy(dl_data.url, url, MAX_URLPATH); // the server set in cl_web_download_url
        dl_data.is_skybox = is_skybox;
        if (display_name)
            Q_strncpy(dl_data.display_name, display_name, sizeof(dl_data.display_name));
        else
            dl_data.display_name[0] = '\0';
	q_strlcpy(cls.download.current, filename, sizeof(cls.download.current));

	CURL* curl = curl_easy_init();
	if (!curl)
    {
        cls.download.active = false;
        curl_download_active = false;
		return false;
    }

	char tmp_path[MAX_OSPATH];
    if ((size_t)q_snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", local_path) >= sizeof(tmp_path))
    {
        curl_easy_cleanup(curl);
        cls.download.active = false;
        curl_download_active = false;
        return false;
    }

	COM_CreatePath(tmp_path);

	FILE* fp = fopen(tmp_path, "wb");
	if (!fp)
	{
		curl_easy_cleanup(curl);
		cls.download.active = false;
		curl_download_active = false;
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

	if (!CL_FinalizeDownloadFile(filename, tmp_path))
	{
		unlink(tmp_path); // Also delete the temporary file in case renaming fails
		cls.download.active = false;
		curl_download_active = false;
		return false;
	}
	CL_DownloadProgress_Update((double)fileSizeBytes, (double)fileSizeBytes);
	cls.download.active = false;
	curl_download_active = false;

	char sizeStr[32];

	if (fileSizeMB >= 1.0f)
		q_snprintf(sizeStr, sizeof(sizeStr), "%.2f mb", fileSizeMB);
	else if (fileSizeKB >= 1.0f)
		q_snprintf(sizeStr, sizeof(sizeStr), "%ld kb", (long)fileSizeKB);

	else
		q_snprintf(sizeStr, sizeof(sizeStr), "%ld bytes", fileSizeBytes);

	char tagbuf[64];
	const char* src = (display_name && display_name[0])
		? display_name
		: DL_DisplayTag(url, tagbuf, sizeof(tagbuf));

	Con_Printf("Downloaded ^m%s^m (%s) from %s\n",
		COM_SkipPath(filename), sizeStr, src);

	return true; // File successfully downloaded

url_too_long:
    Con_DPrintf("Download URL is invalid or too long: %s\n", url ? url : "(null)");
    cls.download.active = false;
    curl_download_active = false;
    return false;
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
		unsigned int size;
		unsigned int hash;
		qboolean hashokay = false;

		if (Cmd_Argc() < 3)
		{
			Con_Warning("Download finished with insufficient arguments\n");
			goto cleanup;
		}

		size = strtoul(Cmd_Argv(1), NULL, 0);
		hash = strtoul(Cmd_Argv(2), NULL, 0);

		if (!CL_DownloadNameIsValid(cls.download.current))
		{
			Con_Warning("Rejected downloaded filename \"%s\"\n", cls.download.current);
			goto cleanup;
		}

		if (size == cls.download.size)
		{
			byte buf[16384];
			unsigned int remaining = size;
			unsigned short crc;

			if (fseek(cls.download.file, 0, SEEK_SET) != 0)
			{
				Con_Warning("Download hash verify seek failure\n");
			}
			else
			{
				CRC_Init(&crc);
				while (remaining)
				{
					size_t chunk = remaining > sizeof(buf) ? sizeof(buf) : remaining;
					size_t bytes_read = fread(buf, 1, chunk, cls.download.file);
					size_t i;

					if (bytes_read != chunk)
						break;

					for (i = 0; i < bytes_read; i++)
						CRC_ProcessByte(&crc, buf[i]);

					remaining -= (unsigned int)bytes_read;
				}

				hashokay = (!remaining && hash == (unsigned int)crc);
				if (!hashokay)
					Con_Warning("Download hash failure\n");
			}
		}
		else
		{
			Con_Warning("Download size mismatch\n");
		}

cleanup:
		fclose(cls.download.file);
		cls.download.file = NULL;
		if (hashokay)
		{
			if (CL_FinalizeDownloadFile(cls.download.current, cls.download.temp))
			{
				Con_SafePrintf("Downloaded %s: %u bytes\n", cls.download.current, cls.download.size);
			}
			else
			{
				Con_Warning("Download of %s failed\n", cls.download.current);
				unlink(cls.download.temp);
			}
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
	CL_DownloadProgress_Update(0.0, (double)cls.download.size);

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
	CL_DownloadProgress_Update((double)(start + size), (double)cls.download.size);

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
	if (cl.wronggamedir)
		return false;	//don't download them into the wrong place. this may be awkward for id1 content though (if such a thing logically exists... like custom maps).
	if (*cls.download.current && !strcmp(cls.download.current, filename))
		return false;	//if the previous download failed, don't endlessly retry.
	if (COM_FileExists(filename, NULL))
		return false;	//no need to download anything.
	if (cls.demoplayback)
		return false;

	// woods, lets try curl web download first #webdl (much faster) #webdl

	char local_path[MAX_OSPATH]; // Define the max path length	
	char modified_filename[MAX_OSPATH];

	if (!strcmp(filename, "progs/star.mdl") && downloadedctf == false) // since we don't download files inside a pak, lets download the pak for ctf
	{
		q_strlcpy(modified_filename, "paks/ctf.pak", sizeof(modified_filename));
		filename = modified_filename;
		Con_Printf("\nfull ctf installation not detected, downloading ctf pak...\n\n");
		downloadedctf = true;
	}

	if (COM_IsPackageExtension(COM_FileGetExtension(filename)))
	{
		if (!COM_DownloadPackageNameOkay(filename))
			return false;
	}
	else if (!COM_DownloadNameOkay(filename))
	{
		return false;
	}

	if (*cls.download.current && !strcmp(cls.download.current, filename))
		return false;
	if (COM_FileExists(filename, NULL))
		return false;

	if ((size_t)q_snprintf(local_path, sizeof(local_path), "%s/%s", com_gamedir, filename) >= sizeof(local_path))
		return false;

	if (webcheck && (cl_web_download_url.string != NULL && cl_web_download_url.string[0] != '\0')) // only run if server is verified
	{
		if (Curl_DownloadFile (cl_web_download_url.string, filename, local_path, false, NULL))
			return false;
	}

	if (web2check && (cl_web_download_url2.string != NULL && cl_web_download_url2.string[0] != '\0')) // only run if server is verified
	{
		if (Curl_DownloadFile (cl_web_download_url2.string, filename, local_path, false, NULL))
			return false;
	}

	// woods, if not available via web, try the server #webdl

	if (allow_download.value == 2) // woods #ftehack
	{
		if (!cl.protocol_dpdownload && cl.protocol != 666) // woods, allow downloads on qecrx (nq physics, FTE server) -- hack
			return false;	//can't download anyway
		if (netquakeio)
			return false;
	}
	else
	{
		if (!cl.protocol_dpdownload)
			return false;	//can't download anyway
	}

	CL_DownloadProgress_Begin(filename);
	q_snprintf (cls.download.temp, sizeof(cls.download.temp), "%s/%s.tmp", com_gamedir, filename);
	if (!strstr(filename, ".loc")) // woods, don't show attempt
		Con_Printf("Downloading %s...\r", filename);
	MSG_WriteByte (&cls.message, clc_stringcmd);
	MSG_WriteString (&cls.message, va("download \"%s\"\n", filename));
	return true;
}

/*
=============================================================================
 Sky_PeekSkyKeyFromBSP -- woods
-----------------------------------------------------------------------------
Looks for a worldspawn "sky" key in a map.
Order of search
   1. Inside the BSP's entity lump
   2. If not found, an external entity file  maps/<mapname>.ent

Returns true and puts the sky string in *outbuf  (truncated to outsz-1)
if a key is found; otherwise returns false and leaves *outbuf empty.
=============================================================================
*/
qboolean Sky_PeekSkyKeyFromBSP(const char* bspname,
	char* outbuf,
	size_t      outsz)
{
	dheader_t  hdr;
	lump_t* ent;
	FILE* f;
	qboolean   ok = false;

	/* ------------------ sanity ------------------ */
	if (!outsz)
		return false;
	*outbuf = 0;

	/* ------------------ open BSP ---------------- */
	if (COM_FOpenFile(bspname, &f, NULL) < (int)sizeof(hdr) || !f)
		goto try_external_ent;

	/* read + validate header */
	if (fread(&hdr, sizeof(hdr), 1, f) != 1)
		goto close_bsp;

	hdr.version = LittleLong(hdr.version);
	if (hdr.version != BSPVERSION &&
		hdr.version != BSP2VERSION_2PSB &&
		hdr.version != BSP2VERSION_BSP2 &&
		hdr.version != BSPVERSION_QUAKE64)
		goto close_bsp;

	for (int i = 1; i < (int)sizeof(hdr) / 4; i++)
		((int*)&hdr)[i] = LittleLong(((int*)&hdr)[i]);

	/* --------------- scan entity lump ---------- */
	ent = &hdr.lumps[LUMP_ENTITIES];
	if (ent->filelen > 0 && ent->filelen <= 32768)
	{
		char* buf = (char*)Z_Malloc(ent->filelen + 1);
		fseek(f, ent->fileofs, SEEK_SET);
		size_t readlen = fread(buf, 1, ent->filelen, f);
		if (readlen != (size_t)ent->filelen)
		{
			Z_Free(buf);
			goto close_bsp;
		}
		buf[ent->filelen] = 0;

		const char* p = COM_Parse(buf);
		if (p && com_token[0] == '{')
		{
			while ((p = COM_Parse(p)))
			{
				if (com_token[0] == '}')
					break;

				char key[64];
				if (com_token[0] == '_')
					q_strlcpy(key, com_token + 1, sizeof(key));
				else q_strlcpy(key, com_token, sizeof(key));

				p = COM_Parse(p);
				if (!p) break;

				if (!q_strcasecmp(key, "sky") ||
					!q_strcasecmp(key, "_sky") ||
					!q_strcasecmp(key, "skyname") ||
					!q_strcasecmp(key, "qlsky"))
				{
					q_strlcpy(outbuf, com_token, outsz);
					ok = true;
					break;
				}
			}
		}
		Z_Free(buf);
	}

close_bsp:
	fclose(f);
	if (ok)
		return true;

	/* ---------------- external .ent ------------- */
try_external_ent:
	{
		char mapname[MAX_QPATH];
		char entpath[MAX_QPATH];

		/* get file name w/o path or extension */
		q_strlcpy(mapname, COM_SkipPath(bspname), sizeof(mapname));
		COM_StripExtension(mapname, mapname, sizeof(mapname));

		q_snprintf(entpath, sizeof(entpath), "maps/%s.ent", mapname);

		char* ebuf = (char*)COM_LoadMallocFile(entpath, NULL);
		if (!ebuf)
			return false;

		const char* p = COM_Parse(ebuf);
		while (p)
		{
			if (com_token[0] != '{')
			{   /* not an entity start – skip line */
				p = COM_Parse(p);
				continue;
			}

			/* entity loop */
			while ((p = COM_Parse(p)))
			{
				if (com_token[0] == '}')
					break;

				char key[64];
				if (com_token[0] == '_')
					q_strlcpy(key, com_token + 1, sizeof(key));
				else q_strlcpy(key, com_token, sizeof(key));

				p = COM_Parse(p);
				if (!p) break;

				if (!q_strcasecmp(key, "sky") ||
					!q_strcasecmp(key, "_sky") ||
					!q_strcasecmp(key, "skyname") ||
					!q_strcasecmp(key, "qlsky"))
				{
					q_strlcpy(outbuf, com_token, outsz);
					ok = true;
					break;
				}
			}
			if (ok) break;

			/* continue with next entity */
			p = COM_Parse(p);
		}

		free(ebuf);
	}

	return ok;
}

extern qboolean Sky_DownloadsDisabled(void);
extern qboolean Sky_DownloadSkybox(const char* name);
extern void Sky_WarnMissingSkybox(const char* name);

//download+load models and sounds as needed, once complete let the server know we're ready for the next stage.
//returning false will trigger nops.
qboolean CL_CheckDownloads(void)
{
	int i;
	cl.suppress_precache_miss_warnings = false;
	if (cl.model_download == 0 && cl.model_count && cl.model_name[1][0]) // woods
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

	if (cl.skybox_download == 0                   /* run once              */
		&& cl.model_name[1][0]                    /* we know the BSP path  */
		&& COM_FileExists(cl.model_name[1], NULL)) /* BSP already here      */
	{
		char skyname[64] = { 0 };

		if (Sky_PeekSkyKeyFromBSP(cl.model_name[1], skyname, sizeof(skyname))
			&& skyname[0])
		{
			qboolean try_skybox_download = !Sky_DownloadsDisabled();

			/* Only try if any face is missing; Sky_DownloadSkybox itself
			   skips mirrors that aren't in user/repo/branch form       */
			if (try_skybox_download && !Sky_DownloadSkybox(skyname))
				Sky_WarnMissingSkybox(skyname);
			/* on failure we still fall through – we tried once */
		}

		cl.skybox_download++;      /* always advance so we never loop */
		return false;              /* block exactly like .loc stage   */
	}

	for (; cl.model_download < cl.model_count; )
	{
		if (cl.model_name[cl.model_download][0]) // woods
		{
			if (CL_CheckDownload(cl.model_name[cl.model_download]))
				return false;
			cl.suppress_precache_miss_warnings = cl.wronggamedir;
			cl.model_precache[cl.model_download] = Mod_ForName (cl.model_name[cl.model_download], false);
			cl.suppress_precache_miss_warnings = false;
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
			cl.suppress_precache_miss_warnings = cl.wronggamedir;
			cl.sound_precache[cl.sound_download] = S_PrecacheSound (cl.sound_name[cl.sound_download]);
			cl.suppress_precache_miss_warnings = false;
		}
		cl.sound_download++;
	}

	CL_MaybePrintLateWrongGameDirWarning();

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
		if (cl.static_entities[i].ent->model)
			continue;
		cl.static_entities[i].ent->model = cl.model_precache[cl.static_entities[i].ent->netstate.modelindex];
		InvalidateTraceLineCache();
		CL_LinkStaticEnt(&cl.static_entities[i]);
	}
	return true;
}

/*
====================
CL_ManualDownload_f -- woods #manualdownload
====================
*/
static qboolean CL_ManualDownloadNameHasPrefix(const char *filename, const char *prefix)
{
    return !q_strncasecmp(filename, prefix, strlen(prefix));
}

static void CL_ManualDownloadSetPrefixedName(const char *filename, const char *prefix, char *out, size_t outsize)
{
    if (CL_ManualDownloadNameHasPrefix(filename, prefix))
        q_snprintf(out, outsize, "%s%s", prefix, filename + strlen(prefix));
    else
        q_snprintf(out, outsize, "%s%s", prefix, filename);
}

static qboolean CL_ManualDownloadNormalizeName(const char *filename, char *out, size_t outsize)
{
    const char* extension;

    if (!filename || !filename[0])
        return false;

    if (!q_strcasecmp(filename, "ctf"))
	{
        q_strlcpy(out, "paks/ctf.pak", outsize);
        return true;
	}
    else if (!q_strcasecmp(filename, "ra"))
	{
        q_strlcpy(out, "paks/ra.pak", outsize);
        return true;
	}

    extension = COM_FileGetExtension(filename);
    if (!extension[0])
	{
		Con_Printf("Please use a filename with an extension (bsp, lit, loc, mdl, or wav)\n");
        return false;
	}

    if (!q_strcasecmp(extension, "bsp") || !q_strcasecmp(extension, "lit"))
	{
        CL_ManualDownloadSetPrefixedName(filename, "maps/", out, outsize);
        return true;
	}
    else if (!q_strcasecmp(extension, "wav"))
	{
        CL_ManualDownloadSetPrefixedName(filename, "sound/", out, outsize);
        return true;
	}
    else if (!q_strcasecmp(extension, "loc"))
	{
        CL_ManualDownloadSetPrefixedName(filename, "locs/", out, outsize);
        return true;
	}
    else if (!q_strcasecmp(extension, "mdl"))
	{
        CL_ManualDownloadSetPrefixedName(filename, "progs/", out, outsize);
        return true;
	}

    Con_Printf("Unsupported file extension. Use bsp, lit, loc, mdl, or wav extensions\n");
    return false;
}

static qboolean CL_ManualDownloadTryMirror(cvar_t *url, qboolean mirror_active, const char *filename, const char *local_path, qboolean require_active_check)
{
    if (!url->string || !url->string[0])
        return false;
    if (require_active_check && !mirror_active)
        return false;

    if (!require_active_check)
        Con_Printf("Trying %s...\n", url->string);

    return Curl_DownloadFile(url->string, filename, local_path, false, NULL);
}

static void CL_ManualDownloadClearCurrent(const char *filename)
{
    if (!strcmp(cls.download.current, filename))
        cls.download.current[0] = '\0';
}

void CL_ManualDownload_f (const char* filename)
{
    char prefixedArg[MAX_OSPATH];
    char local_path[MAX_OSPATH];
    qboolean require_active_check;
    qboolean isNeitherWebDownloadServerSet;

	if (Cmd_Argc() != 2)
	{
		Con_Printf("download <filename|ctf|ra> : filename with an extension (bsp, lit, loc, mdl, or wav)\n");
		return;
	}

	if (*filename == '*')
		return;    //don't download these...
	if (cls.download.active)
		return;    //block while we're already downloading something

	if (!CL_ManualDownloadNormalizeName(filename, prefixedArg, sizeof(prefixedArg)))
		return;

	if (!CL_DownloadNameIsValid(prefixedArg))
	{
		Con_Printf("Unsupported download path\n");
		return;
	}

    if (strlen(prefixedArg) >= sizeof(cls.download.current))
    {
        Con_Printf("Download path too long\n");
        return;
    }

	if (*cls.download.current && !strcmp(cls.download.current, prefixedArg))
		return;	//if the previous download failed, don't endlessly retry.

	if (COM_FileExists(prefixedArg, NULL))
	{
		Con_Printf("File already exists, download not attempted\n");
		return;
	}

    isNeitherWebDownloadServerSet = (cl_web_download_url.string == NULL || cl_web_download_url.string[0] == '\0') &&
		(cl_web_download_url2.string == NULL || cl_web_download_url2.string[0] == '\0');

	if (isNeitherWebDownloadServerSet)
	{
		Con_Printf("No web download servers are set\n");
		return;
	}

    require_active_check = (cls.state != ca_dedicated);

    if (require_active_check && !webcheck && !web2check)
	{
		Con_Printf("No web download servers are active\n");
		return;
	}

	Con_Printf("Attempting download, if found you will see progress below...\n");

	if ((size_t)q_snprintf(local_path, sizeof(local_path), "%s/%s", com_gamedir, prefixedArg) >= sizeof(local_path))
	{
		Con_Printf("Download path too long\n");
		return;
	}

	if (CL_ManualDownloadTryMirror(&cl_web_download_url, webcheck, prefixedArg, local_path, require_active_check))
	{
		CL_ManualDownloadClearCurrent(prefixedArg);
		return;
	}

	if (CL_ManualDownloadTryMirror(&cl_web_download_url2, web2check, prefixedArg, local_path, require_active_check))
	{
		CL_ManualDownloadClearCurrent(prefixedArg);
		return;
	}

	Con_Printf("Download failed: %s\n", prefixedArg);
	CL_ManualDownloadClearCurrent(prefixedArg);
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

	if (cls.demoplayback)
		CL_AdvanceTime(); // woods (iw) #democontrols
	else
	{
		cl.oldtime = cl.time;
		cl.time += host_frametime;
	}

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

//	if (cl_shownet.value)
//		Con_Printf ("\n");

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
	for (i = 0, b = cl_beams; i < MAX_BEAMS; i++, b++)
		if (b->model && (cls.demoplayback ? b->starttime <= cl.time : true) && b->endtime >= cl.time) // woods (iw) #democontrols
			num_beams++;
	if (num_beams > 24 && dev_peakstats.beams <= 24)
		Con_DWarning ("%i beams exceeded standard limit of 24 (max = %d).\n", num_beams, MAX_BEAMS);
	dev_stats.beams = num_beams;
	dev_peakstats.beams = q_max(num_beams, dev_peakstats.beams);

	//dlights
	for (i=0, l=cl_dlights ; i<MAX_DLIGHTS ; i++, l++)
		if (l->die >= cl.time && (cls.demoplayback ? l->spawn <= cl.mtime[0] : true) && l->radius) // woods (iw) #democontrols
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

vec3_t last_viewpos; // woods #setlast
vec3_t last_viewangles; // woods #setlast
qboolean has_last_viewpos; // woods #setlast

/*
=============
CL_Viewpos_f -- johnfitz

display client's position and angles
=============
*/
void CL_Viewpos_f (void)
{
	char buf[256];
	vec3_t laserpoint;
	if (cls.state != ca_connected)
		return;

	// player position
	q_snprintf (buf, sizeof (buf),
		"(%.0f %.0f %.0f) %.0f %.0f %.0f",
		cl.entities[cl.viewentity].origin[0],
		cl.entities[cl.viewentity].origin[1],
		cl.entities[cl.viewentity].origin[2],
		cl.viewangles[PITCH],
		cl.viewangles[YAW],
		cl.viewangles[ROLL]
	);

	VectorCopy(cl.entities[cl.viewentity].origin, last_viewpos); // woods #setlast
	VectorCopy(cl.viewangles, last_viewangles); // woods #setlast
	has_last_viewpos = true; // woods #setlast

	Con_SafePrintf ("Player pos: %s\n", buf);

	if (Cmd_Argc () >= 2 && !q_strcasecmp (Cmd_Argv (1), "copy"))
		if (SDL_SetClipboardText (buf) < 0)
			Con_SafePrintf ("Clipboard copy failed: %s\n", SDL_GetError ());

	if (SCR_GetLaserPoint (laserpoint))
		Con_SafePrintf ("Laserpoint pos: (%.0f %.0f %.0f)\n",
			laserpoint[0],
			laserpoint[1],
			laserpoint[2]
		);

	// camera position
	Con_SafePrintf ("Camera pos: (%.0f %.0f %.0f) %.0f %.0f %.0f\n",
		r_refdef.vieworg[0],
		r_refdef.vieworg[1],
		r_refdef.vieworg[2],
		r_refdef.viewangles[PITCH],
		r_refdef.viewangles[YAW],
		r_refdef.viewangles[ROLL]
	);

	// sun mangle
	Con_SafePrintf ("Sun mangle: %.0f %.0f %.0f\n",
		anglemod (r_refdef.viewangles[YAW] + 180.f),
		r_refdef.viewangles[PITCH],
		r_refdef.viewangles[ROLL]
	);
}

/*
===============
GetBspVersionString -- woods #entcopy -- get string representation of BSP version
===============
*/
static const char* GetBspVersionString(int version)
{
	switch (version)
	{
	case BSPVERSION:
		return "BSP29";
	case BSP2VERSION_2PSB:
		return "BSP2 (2PSB)";
	case BSP2VERSION_BSP2:
		return "BSP2";
	case BSPVERSION_QUAKE64:
		return "BSP64";
	default:
		return "Unknown";
	}
}

static qboolean Entdump_MakeUnique(char *out, size_t outsz, const char *map_lower) // woods #entcopy
{
    char candidate[MAX_OSPATH], full[MAX_OSPATH];

    /* 1) Try plain maps/<name>.ent */
    if ((size_t)q_snprintf(candidate, sizeof(candidate), "maps/%s.ent", map_lower) >= sizeof(candidate))
        return false;
    q_snprintf(full, sizeof(full), "%s/%s", com_gamedir, candidate);
    if (!(Sys_FileType(full) & FS_ENT_FILE)) { /* not present -> use it */
        q_strlcpy(out, candidate, outsz);
        return true;
    }

    /* 2) Try maps/<name> (n).ent, n = 1..99 */
    for (int i = 1; i < 100; ++i) {
        if ((size_t)q_snprintf(candidate, sizeof(candidate), "maps/%s(%d).ent", map_lower, i) >= sizeof(candidate))
            return false;
        q_snprintf(full, sizeof(full), "%s/%s", com_gamedir, candidate);
        if (!(Sys_FileType(full) & FS_ENT_FILE)) {
            q_strlcpy(out, candidate, outsz);
            return true;
        }
    }
    return false; /* too many duplicates */
}

/*
===============
CL_Entdump_f -- woods (source: github.com/alexey-lysiuk/quakespasm-exp) #entcopy
===============
*/
void CL_Entdump_f(void)
{
	if (Cmd_Argc() < 2) // Handle case when no argument is given - use loaded map
	{
		if (!cl.worldmodel)
		{
			Con_Printf("no map loaded, cannot save .ent\n");
			return;
		}

		if (!cl.worldmodel->entities)
		{
			Con_Printf("no entities in current map\n");
			return;
		}

		char entfilename[MAX_OSPATH];
		char map_lower[MAX_QPATH];
		char full[MAX_OSPATH];
		q_strlcpy(map_lower, cl.mapname, sizeof(map_lower));
		for (char *p = map_lower; *p; ++p)
			*p = q_tolower(*p);
		if (!Entdump_MakeUnique(entfilename, sizeof(entfilename), map_lower)) {
			Con_Printf("could not form unique .ent path\n");
			return;
		}
		q_snprintf(full, sizeof(full), "%s/%s", com_gamedir, entfilename);
		COM_CreatePath(full); /* ensure "<gamedir>/maps/" exists */
		COM_WriteFile(entfilename, cl.worldmodel->entities, strlen(cl.worldmodel->entities));
		Con_Printf("saved entities from maps/%s.bsp (%s) to ^m%s^m\n",
			cl.mapname, GetBspVersionString(cl.worldmodel->bspversion), entfilename);
		return;
	}

	// Handle case when map name is provided as argument
	const char* mapname = Cmd_Argv(1);
	char cleaned_mapname[MAX_QPATH];

	COM_StripExtension(mapname, cleaned_mapname, sizeof(cleaned_mapname));

	// Check if this is the currently loaded map
	qboolean matches_current;
	if (FS_IsCaseSensitive())
		matches_current = (cl.worldmodel && !strcmp(cleaned_mapname, cl.mapname));
	else
		matches_current = (cl.worldmodel && !q_strcasecmp(cleaned_mapname, cl.mapname));

	if (matches_current)
	{
		if (!cl.worldmodel->entities)
		{
			Con_SafePrintf("no entities in current map\n");
			return;
		}

		char entfilename[MAX_OSPATH];
		char map_lower[MAX_QPATH];
		char full[MAX_OSPATH];
		q_strlcpy(map_lower, cleaned_mapname, sizeof(map_lower));
		for (char *p = map_lower; *p; ++p)
			*p = q_tolower(*p);
		if (!Entdump_MakeUnique(entfilename, sizeof(entfilename), map_lower)) {
			Con_Printf("could not form unique .ent path\n");
			return;
		}
		q_snprintf(full, sizeof(full), "%s/%s", com_gamedir, entfilename);
		COM_CreatePath(full);
		COM_WriteFile(entfilename, cl.worldmodel->entities, strlen(cl.worldmodel->entities));
		Con_Printf("saved entities from maps/%s.bsp (%s) to ^m%s^m\n",
			cleaned_mapname, GetBspVersionString(cl.worldmodel->bspversion), entfilename);
		return;
	}

	char bspfilename[MAX_OSPATH];

	// Build full BSP path
	if ((size_t)q_snprintf(bspfilename, sizeof(bspfilename), "maps/%s.bsp", cleaned_mapname) >= sizeof(bspfilename))
	{
		Con_Printf("map name too long\n");
		return;
	}

	// Open and read BSP file
	FILE* f;
	unsigned path_id;
	int length = COM_FOpenFile(bspfilename, &f, &path_id);
	if (length <= 0)
	{
		if (f)
			fclose(f);
		Con_Printf("couldn't load %s\n", bspfilename);
		return;
	}

	byte* buffer = malloc(length);
	if (!buffer)
	{
		fclose(f);
		Con_Printf("out of memory for BSP file\n");
		return;
	}

	if (fread(buffer, 1, length, f) != (size_t)length)
	{
		free(buffer);
		fclose(f);
		Con_Printf("error reading BSP file\n");
		return;
	}
	fclose(f);

	dheader_t* header = (dheader_t*)buffer;
	header->version = LittleLong(header->version);

	const char* bspversion = GetBspVersionString(header->version);
	if (!strcmp(bspversion, "Unknown"))
	{
		int version = header->version; // woods
		free(buffer);
		Con_Printf("unsupported BSP version %d\n", version);
		return;
	}

	// Swap header integers to correct endianness
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		header->lumps[i].fileofs = LittleLong(header->lumps[i].fileofs);
		header->lumps[i].filelen = LittleLong(header->lumps[i].filelen);
	}

	// Get entities lump
	const lump_t* entlump = &header->lumps[LUMP_ENTITIES];
	if (entlump->filelen <= 0)
	{
		free(buffer);
		Con_Printf("no entities in %s\n", bspfilename);
		return;
	}

	// Validate entity lump position
	if (entlump->fileofs < 0 || entlump->fileofs + entlump->filelen >(unsigned int)length)
	{
		free(buffer);
		Con_Printf("invalid entity lump in %s\n", bspfilename);
		return;
	}

	// Point to the entities data in the buffer
	char* entities_data = (char*)(buffer + entlump->fileofs);

	char entfilename[MAX_OSPATH];
	char map_lower[MAX_QPATH];
	char full[MAX_OSPATH];
	q_strlcpy(map_lower, cleaned_mapname, sizeof(map_lower));
	for (char *p = map_lower; *p; ++p)
		*p = q_tolower(*p);
	if (!Entdump_MakeUnique(entfilename, sizeof(entfilename), map_lower)) {
		free(buffer);
		Con_Printf("could not form unique .ent path\n");
		return;
	}
	q_snprintf(full, sizeof(full), "%s/%s", com_gamedir, entfilename);
	COM_CreatePath(full);

	// Find the actual length of valid entity text
	size_t text_length = 0;
	while (text_length < entlump->filelen && entities_data[text_length])
		text_length++;

	// Basic validation - check for either '{' or '//' to indicate valid entity data
	qboolean valid_start = false;
	for (size_t i = 0; i < text_length - 1; i++) {
		if (entities_data[i] == '{' || (entities_data[i] == '/' && entities_data[i + 1] == '/')) {
			valid_start = true;
			break;
		}
		// Skip whitespace during validation
		if (entities_data[i] != ' ' && entities_data[i] != '\n' && entities_data[i] != '\r' && entities_data[i] != '\t')
			break;
	}

	if (!valid_start) {
		free(buffer);
		Con_Printf("invalid entity data in %s (no valid entity data found)\n", bspfilename);
		return;
	}

	COM_WriteFile(entfilename, entities_data, text_length);
	Con_Printf("saved entities from %s (%s) to ^m%s^m\n", bspfilename, bspversion, entfilename);

	free(buffer);
}

static void CL_ServerExtension_ItemTimer_f (void) // woods #obstimers (FTE)
{
	// it[cur / ]duration x y z radius 0xRRGGBB "timername" owningent

	if (Cmd_Argc() < 8)
	{
		Con_DPrintf2("Ignoring stufftext: %s\n", Cmd_Argv(0));
		return;
	}

	// Check deathmatch mode
	char buf[4];
	const char* val;
	val = Info_GetKey(cl.serverinfo, "deathmatch", buf, sizeof(buf));
	int deathmatch_mode = val ? atoi(val) : 0;

	float timeout;
	float start = cl.time;
	const char* e;
	timeout = strtod(Cmd_Argv(1), (char**)&e);
	if (*e == '/') 
	{
		start += timeout;
		timeout = atof(e + 1);
		start -= timeout;
	}

	// Parse remaining arguments
	vec3_t org =
	{
		atof(Cmd_Argv(2)),  // x
		atof(Cmd_Argv(3)),  // y
		atof(Cmd_Argv(4))   // z
	};
	float radius = atof(Cmd_Argv(5));
	const char* tint = Cmd_Argv(6);
	unsigned int rgb = strtoul(tint, NULL, 16);  // Convert tint string to RGB
	const char* timername = (Cmd_Argc() > 7) ? Cmd_Argv(7) : "";
	unsigned int entnum = (Cmd_Argc() > 8) ? strtoul(Cmd_Argv(8), NULL, 0) : 0;

	// Skip weapon timers in deathmatch 3
	if (deathmatch_mode == 3 &&
		(strcmp(timername, "lg") == 0 || strcmp(timername, "rl") == 0))
		return;

	// Set defaults if needed
	if (!timeout)
		timeout = FLT_MAX;
	if (!radius)
		radius = 32;

	// Find existing timer or create new one
	struct itemtimer_s* timer;
	for (timer = cl.itemtimers; timer; timer = timer->next)
	{
		if (entnum)
		{
			if (timer->entnum == entnum)
				break;
		}
		else if (VectorCompare(timer->origin, org))
			break;
	}
	if (!timer)
	{   //didn't find it.
		timer = Z_Malloc(sizeof(*timer));
		timer->next = cl.itemtimers;
		cl.itemtimers = timer;
	}

	extern cvar_t scr_obsitems;
	if (scr_obsitems.value)
	PScript_RunParticleEffectTypeString(org, NULL, 1, "EF_ITEMTIMER");

	// Update timer properties
	VectorCopy(org, timer->origin);
	timer->start = start;
	timer->duration = timeout;
	timer->radius = radius;
	timer->entnum = entnum;
	timer->end = start + timer->duration;
	timer->rgb[0] = ((rgb >> 16) & 0xff) / 255.0;
	timer->rgb[1] = ((rgb >> 8) & 0xff) / 255.0;
	timer->rgb[2] = ((rgb) & 0xff) / 255.0;

	// Properly handle the timername string
	if (timer->timername)
		Z_Free(timer->timername);
	timer->timername = Z_Strdup(timername);  // Allocate and copy the string
}

static void CL_ServerExtension_TeamInfo_f(void) // woods #teaminfo
{
	int pidx = atoi(Cmd_Argv(1));
	vec3_t org = {
		atof(Cmd_Argv(2)),
		atof(Cmd_Argv(3)),
		atof(Cmd_Argv(4))
	};
	float health = atof(Cmd_Argv(5));
	float armor = atof(Cmd_Argv(6));
	unsigned int items = strtoul(Cmd_Argv(7), NULL, 0);
	float speed = atof(Cmd_Argv(8));

	if (pidx < cl.maxclients)
	{
		scoreboard_t* player = &cl.scores[pidx];
		player->tinfo.time = cl.time + 5;
		player->tinfo.health = health;
		player->tinfo.armor = armor;
		player->tinfo.items = items;
		player->tinfo.speed = speed;
		VectorCopy(org, player->tinfo.origin);
	}
}

static void CL_ServerExtension_FullServerinfo_f(void)
{
	const char *newserverinfo = Cmd_Argv(1);
	Q_strncpy(cl.serverinfo, newserverinfo, sizeof(cl.serverinfo));	//just replace it

	CL_UpdatePlaymodeFromServerinfo ();
	PMCL_ServerinfoUpdated();
}
static void CL_ServerExtension_ServerinfoUpdate_f(void)
{
	const char *newserverkey = Cmd_Argv(1);
	const char *newservervalue = Cmd_Argv(2);
	Info_SetKey(cl.serverinfo, sizeof(cl.serverinfo), newserverkey, newservervalue);

	CL_UpdatePlaymodeFromServerinfo ();
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
		// Save preferred name before duplicate check modifies it
		q_strlcpy(client->desired_name, tmp, sizeof(client->desired_name));

		if (client->name[0] && strcmp(client->name, "unconnected") )
			Con_Printf ("%s renamed to %s\n", host_client->name, tmp);
		Q_strcpy (host_client->name, tmp);
		client->edict->v.netname = PR_SetEngineString(client->name);
		SV_CheckDuplicateNames(client); // woods #dupnames
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
		{
			SV_DecodeUserInfo(infoplayer);

			if (!strcmp(keyname, "name") && infoplayer->name[0]) // woods #dupnames
			{
				SV_CheckDuplicateNames(infoplayer);
				SV_ReapplyPreferredNames(infoplayer);
			}

			if (sv_mapcrc.value && !strcmp(keyname, "*mapmismatch") && !strcmp(value, "1")) // woods #mapcrc
			{
				// Notify all players about the map mismatch
				client_t *notify_cl;
				for (notify_cl = svs.clients; notify_cl < svs.clients+svs.maxclients; notify_cl++)
				{
					if (notify_cl->active && notify_cl != infoplayer)
					{
						MSG_WriteByte(&notify_cl->message, svc_print);
						MSG_WriteString(&notify_cl->message, va("^3%s connected with a different map version\n", infoplayer->name));
					}
				}
				Con_Printf("^3%s connected with a different map version\n", infoplayer->name);
			}
		}

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
===============
CL_Onload_Completion_f -- woods #onload
===============
*/
static void CL_Onload_Completion_f(cvar_t* cvar, const char* partial)
{
	Con_AddToTabList("\"\"", partial, NULL, NULL); // #demolistsort add arg
	Con_AddToTabList("bookmarks", partial, NULL, NULL); // #demolistsort add arg
	Con_AddToTabList("browser", partial, NULL, NULL); // #demolistsort add arg
	Con_AddToTabList("connect", partial, NULL, NULL); // #demolistsort add arg
	Con_AddToTabList("console", partial, NULL, NULL); // #demolistsort add arg
	Con_AddToTabList("demo", partial, NULL, NULL); // #demolistsort add arg
	Con_AddToTabList("exec", partial, NULL, NULL); // #demolistsort add arg
	Con_AddToTabList("history", partial, NULL, NULL); // #demolistsort add arg
	Con_AddToTabList("save", partial, NULL, NULL); // #demolistsort add arg

	return;
}

/*
===============
CL_ContentFilter_Completion_f -- woods #iwtabcomplete
===============
*/
static void CL_ContentFilter_Completion_f(cvar_t* cvar, const char* partial)
{
	(void)cvar;

	Con_AddToTabList("0", partial, "off", NULL);
	Con_AddToTabList("1", partial, "partial", NULL);
	Con_AddToTabList("2", partial, "full", NULL);
}

/*
===============
CL_Autovote_List_Completion_f -- woods #autovote
===============
*/
static void CL_Autovote_List_Completion_f(cvar_t* cvar, const char* partial)
{
	static const struct
	{
		const char* value;
		const char* type;
	} options[] =
	{
		{ "player", "name" },
		{ "powerzord", "name" },
		{ "sofdm3", "map" },
		{ "change level", "vote" },
		{ "next level", "vote" },
		{ "change map", "vote" },
		{ "change gametype", "vote" },
		{ "change mode", "vote" },
		{ "change frag limit", "vote" },
		{ "frag limit", "vote" },
		{ "change match length", "vote" },
		{ "match length", "vote" },
		{ "change overtime", "vote" },
		{ "overtime", "vote" },
		{ "weaponstay", "vote" },
		{ "grappling hook", "vote" },
		{ "entity set", "vote" },
		{ "alternative entity set", "vote" },
		{ "standard entity set", "vote" },
		{ "gibs", "vote" },
		{ "quad", "vote" },
		{ "pentagram", "vote" },
		{ "ring of shadows", "vote" },
		{ "obituaries", "vote" },
		{ "match autopause", "vote" },
		{ "prediction", "vote" },
		{ "runes", "vote" },
		{ "abort match", "vote" },
		{ "powerup dropping", "vote" },
		{ "pause the match", "vote" },
		{ "unpause the match", "vote" },
		{ "lock the match", "vote" },
		{ "allow new players to join", "vote" },
		{ "start the timer", "vote" },
		{ "randomly reshuffle the teams", "vote" },
		{ "qwsucks", "vote" },
		{ "q14ever", "vote" },
		{ "free for all", "gametype" },
		{ "team deathmatch", "gametype" },
		{ "deathmatch", "gametype" },
		{ "ctf", "mode" },
		{ "capture the flag", "gametype" },
		{ "clan arena", "gametype" },
		{ "rocket arena", "gametype" },
		{ "dm", "mode" },
		{ "duel", "gametype" },
		{ "airshot", "gametype" },
		{ "wipeout", "gametype" },
		{ "ctf duel", "gametype" },
		{ "timelimit", "vote" },
		{ "normal", "mode" },
		{ "practice", "mode" },
		{ "match", "mode" }
	};
	char candidate[MAXCMDLINE];
	const char* comma = strrchr(partial, ',');
	const char* semicolon = strrchr(partial, ';');
	const char* separator = comma;
	size_t prefix_len = 0;
	size_t i;

	if (Cmd_Argc() != 2)
		return;

	if (!separator || (semicolon && semicolon > separator))
		separator = semicolon;

	if (separator)
	{
		prefix_len = (size_t)((separator + 1) - partial);
		while (partial[prefix_len] && q_isspace((unsigned char)partial[prefix_len]))
			prefix_len++;
	}

	// offer mode keywords at first position
	if (!prefix_len)
	{
		Con_AddToTabList("exclude", partial, "mode", NULL);
		Con_AddToTabList("include", partial, "mode", NULL);
	}

	for (i = 0; i < sizeof(options) / sizeof(options[0]); ++i)
	{
		if (prefix_len)
			q_snprintf(candidate, sizeof(candidate), "%.*s%s", (int)prefix_len, partial, options[i].value);
		else
			q_strlcpy(candidate, options[i].value, sizeof(candidate));

		Con_AddToTabList(candidate, partial, options[i].type, NULL);
	}
}

/*
===============
CL_DemoFormat_Completion_f
===============
*/
static void CL_DemoFormat_Completion_f(cvar_t* cvar, const char* partial)
{
	(void)cvar;

	if (Cmd_Argc() != 2)
		return;

	Con_AddToTabList("dem", partial, "raw demo", NULL);
#ifdef USE_ZLIB
	Con_AddToTabList("dz", partial, "dzip archive", NULL);
#endif
}

/*
===============
CL_DemoMinFrames_Completion_f
===============
*/
static void CL_DemoMinFrames_Completion_f(cvar_t* cvar, const char* partial)
{
	static const struct
	{
		const char* value;
		const char* type;
	} thresholds[] =
	{
		{ "0",     "off" },
		{ "72",    "hide <1s" },
		{ "720",   "hide <10s" },
		{ "2160",  "hide <30s" },
		{ "4320",  "hide <1min" },
		{ "-72",   "delete <1s" },
		{ "-720",  "delete <10s" },
		{ "-2160", "delete <30s" },
		{ "-4320", "delete <1min" },
	};
	size_t i;

	(void)cvar;

	if (Cmd_Argc() != 2)
		return;

	for (i = 0; i < Q_COUNTOF(thresholds); i++)
		Con_AddToTabList(thresholds[i].value, partial, thresholds[i].type, NULL);
}

/*
===============
CL_PlayerColor_Completion_f -- woods #iwtabcomplete
===============
*/
static void CL_PlayerColor_Completion_f(cvar_t* cvar, const char* partial)
{
	static const struct
	{
		const char* value;
		const char* type;
	} colors[] =
	{
		{ "0", "white" },
		{ "1", "brown" },
		{ "2", "light blue" },
		{ "3", "green" },
		{ "4", "red" },
		{ "5", "orange" },
		{ "6", "gold" },
		{ "7", "peach" },
		{ "8", "purple" },
		{ "9", "magenta" },
		{ "10", "tan" },
		{ "11", "green" },
		{ "12", "yellow" },
		{ "13", "blue" },
		{ "0x66ff00", "bright green" },
		{ "0xff00cd", "bright magenta" },
		{ "0xffff00", "bright yellow" }
	};
	size_t i;

	(void)cvar;

	if (Cmd_Argc() != 2)
		return;

	for (i = 0; i < sizeof(colors) / sizeof(colors[0]); i++)
		Con_AddToTabList(colors[i].value, partial, colors[i].type, NULL);
}

/*
===============
CL_Name_Completion_f -- woods #namehistory

uses the shared name tab-complete helpers in console.c so the user can type
the ascii equivalent of a name that contains quake special chars
===============
*/
static void CL_Name_Completion_f (cvar_t *cvar, const char *partial)
{
	filelist_item_t *item;
	char unfun_partial[32];
	const char *match_partial;

	(void)cvar;

	if (Cmd_Argc() != 2)
		return;

	match_partial = Con_DequakePartial (partial, unfun_partial, sizeof(unfun_partial));

	for (item = namehistorylist; item; item = item->next)
		Con_AddNameToTabList (item->name, partial, match_partial);
}

/*
=================
CL_Init
=================
*/
void CL_Init (void)
{
	SZ_Alloc (&cls.message, 1024);
	cls.download.percent = -1.0f;

	CL_InitInput ();
	CL_InitTEnts ();

	Cvar_RegisterVariable (&cl_name);
	Cvar_RegisterAlias    (&cl_name, "_cl_name");	//spike -- for compat with configs now that 'name' is a cvar in its own right.
	Cvar_SetCompletion (&cl_name, &CL_Name_Completion_f); // woods #namehistory
	Cvar_RegisterVariable (&cl_topcolor);
	Cvar_RegisterVariable (&cl_bottomcolor);
	Cvar_SetCompletion (&cl_topcolor, &CL_PlayerColor_Completion_f); // woods #iwtabcomplete
	Cvar_SetCompletion (&cl_bottomcolor, &CL_PlayerColor_Completion_f); // woods #iwtabcomplete
	Cmd_AddCommand ("_cl_color", CL_LegacyColor_f);	//for loading vanilla configs (we have separate qw-style topcolor/bottomcolor userinfo cvars instead)
	Cvar_RegisterVariable (&cl_upspeed);
	Cvar_RegisterVariable (&cl_forwardspeed);
	Cvar_RegisterVariable (&cl_backspeed);
	Cvar_RegisterVariable (&cl_sidespeed);
	Cvar_RegisterVariable (&cl_noclip_speed);
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
	Cvar_RegisterVariable (&cfg_save_aliases); // woods #serveralias

	Cvar_RegisterVariable (&cl_maxpitch); //johnfitz -- variable pitch clamping
	Cvar_RegisterVariable (&cl_minpitch); //johnfitz -- variable pitch clamping
	Cvar_RegisterVariable (&cl_recordingdemo); //spike -- for mod hacks. combine with cvar_string or something
	Cvar_RegisterVariable (&cl_demo_format);
	Cvar_SetCompletion (&cl_demo_format, &CL_DemoFormat_Completion_f);
	Cvar_RegisterVariable (&cl_demo_minframes);
	Cvar_SetCompletion (&cl_demo_minframes, &CL_DemoMinFrames_Completion_f);
	Cvar_RegisterVariable (&cl_demoreel);

	Cvar_RegisterVariable (&cl_beams_polygons); // woods #beamspoly
	Cvar_RegisterVariable (&cl_truelightning); // woods for #truelight
	Cvar_RegisterVariable (&gl_lightning_alpha); // woods for lighting alpha #lightalpha
	Cvar_RegisterVariable (&cl_say); // woods for #ezsay
	Cvar_RegisterVariable (&cl_afk); // woods #smartafk
	Cvar_RegisterVariable (&cl_idle); // woods #smartafk
	Cvar_RegisterVariable (&r_rocketlight); // woods #rocketlight
	Cvar_RegisterVariable (&r_explosionlight); // woods #explosionlight
	Cvar_RegisterVariable (&cl_muzzleflash); // woods #muzzleflash
	Cvar_RegisterVariable (&cl_deadbodyfilter); // woods #deadbody
	Cvar_RegisterVariable (&cl_r2g); // woods #r2g
	Cvar_RegisterVariable (&cl_demoeyes); // woods #demoeyes
	Cvar_RegisterVariable (&cl_ctf_pub_modelswap); // woods #ctfpubmodels

	Cvar_RegisterVariable (&w_switch); // woods #autoweapon
	Cvar_RegisterVariable (&b_switch); // woods #autoweapon
	Cvar_RegisterVariable (&f_status); // woods #flagstatus

	Cvar_RegisterVariable (&cl_ambient); // woods #stopsound
	Cvar_RegisterVariable (&cl_smartspawn); // woods #spawntrainer
	Cvar_RegisterVariable (&r_coloredpowerupglow); // woods
	Cvar_RegisterVariable (&cl_bobbing); // woods (joequake #weaponbob)

    CL_InitWebDownloads(false);

	Cvar_RegisterVariable (&cl_autovote); // woods #autovote
	Cvar_RegisterVariable (&cl_autovote_list); // woods #autovote
	Cvar_SetCompletion (&cl_autovote_list, &CL_Autovote_List_Completion_f); // woods #autovote
	Cvar_RegisterVariable (&cl_onload); // woods #onload
	Cvar_SetCompletion (&cl_onload, &CL_Onload_Completion_f); // woods #onload
	Cvar_RegisterVariable (&cl_contentfilter); // woods #contentfilter
	Cvar_SetCompletion (&cl_contentfilter, &CL_ContentFilter_Completion_f); // woods #iwtabcomplete

	Cvar_RegisterVariable(&cl_rot); // woods #clmrotate
	Cvar_SetCallback(&cl_rot, CL_RotateModel_OnChange); // woods #clmrotate
	Cvar_SetCompletion(&cl_rot, CL_RotateModel_Cvar_Completion_f); // woods #clmrotate
	CL_RotateModel_RebuildFromCvar(); // woods #clmrotate
    CL_InitWebDownloads(true);

	Cmd_AddCommand ("entities", CL_PrintEntities_f);
	Cmd_AddCommand ("disconnect", CL_Disconnect_f);
	Cmd_AddCommand ("record", CL_Record_f);
	Cmd_AddCommand ("markdemo", CL_DemoMark_f);
	Cmd_AddCommand ("stop", CL_Stop_f);
	Cmd_AddCommand ("playdemo", CL_PlayDemo_f);
	Cmd_AddCommand ("timedemo", CL_TimeDemo_f);
	Cmd_AddCommand ("jumpdemo", CL_JumpDemo_f);

	Cmd_AddCommand ("tracepos", CL_Tracepos_f); //johnfitz
	Cmd_AddCommand ("viewpos", CL_Viewpos_f); //johnfitz

	Cmd_AddCommand("entdump", &CL_Entdump_f); // woods #entcopy
	Cmd_AddCommand("rotatemodel", CL_RotateModel_f); // woods #clmrotate

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
	Cmd_AddCommand_ServerCommand ("it", CL_ServerExtension_ItemTimer_f); //cspree item timers -- woods #obstimers
	Cmd_AddCommand_ServerCommand ("tinfo", CL_ServerExtension_TeamInfo_f); //ktx team info -- woods #teaminfo
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
	Cmd_AddCommand_ServerCommand("markdemo", CL_DemoMark_f); // woods #markdemo
	
	Cmd_AddCommand_ServerCommand ("cl_serverextension_download", CL_ServerExtension_Download_f); //spike
	Cmd_AddCommand_ServerCommand ("cl_downloadbegin", CL_Download_Begin_f); //spike
	Cmd_AddCommand_ServerCommand ("cl_downloadfinished", CL_Download_Finished_f); //spike
	Cmd_AddCommand ("stopdownload", CL_StopDownload_f); //spike
}
