/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
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
// host.c -- coordinates spawning and killing of local servers

#include "quakedef.h"
#include "bgmusic.h"
#include "pmove.h"
#include <setjmp.h>
#include "time.h" // woods #cfgbackup

/*

A server can allways be started, even if the system started out as a client
to a remote system.

A client can NOT be started if the system started as a dedicated server.

Memory is cleared / released when a server or client begins, not when they end.

*/

quakeparms_t *host_parms;

qboolean	host_initialized;		// true if into command execution

double		host_frametime;
double		host_time;              // woods #smoothcam
double		last_angle_time;		// JPG - for smooth chasecam (from Proquake)   // woods #smoothcam
double		realtime;				// without any filtering or bounding
double		oldrealtime;			// last frame run

int		host_framecount;

int		host_hunklevel;

int		minimum_memory;

client_t	*host_client;			// current client

jmp_buf 	host_abortserver;

byte		*host_colormap;
float	host_netinterval;
cvar_t	host_framerate = {"host_framerate","0",CVAR_NONE};	// set for slow motion
cvar_t	host_speeds = {"host_speeds","0",CVAR_NONE};			// set for running times
cvar_t	host_maxfps = {"host_maxfps", "250", CVAR_ARCHIVE}; //johnfitz
cvar_t	host_timescale = {"host_timescale", "0", CVAR_NONE}; //johnfitz
cvar_t	max_edicts = {"max_edicts", "15000", CVAR_NONE}; //johnfitz //ericw -- changed from 2048 to 8192, removed CVAR_ARCHIVE
cvar_t	cl_nocsqc = {"cl_nocsqc", "0", CVAR_NONE};	//spike -- blocks the loading of any csqc modules

cvar_t	sys_ticrate = {"sys_ticrate","0.05",CVAR_NONE}; // dedicated server
cvar_t	serverprofile = {"serverprofile","0",CVAR_NONE};

cvar_t	fraglimit = {"fraglimit","0",CVAR_NOTIFY|CVAR_SERVERINFO};
cvar_t	timelimit = {"timelimit","0",CVAR_NOTIFY|CVAR_SERVERINFO};
cvar_t	teamplay = {"teamplay","0",CVAR_NOTIFY|CVAR_SERVERINFO};
cvar_t	samelevel = {"samelevel","0",CVAR_SERVERINFO};
cvar_t	noexit = {"noexit","0",CVAR_NOTIFY|CVAR_SERVERINFO};
cvar_t	skill = {"skill","1",CVAR_SERVERINFO};			// 0 - 3
cvar_t	deathmatch = {"deathmatch","0",CVAR_SERVERINFO};	// 0, 1, or 2
cvar_t	coop = {"coop","0",CVAR_SERVERINFO};			// 0 or 1

cvar_t	pausable = {"pausable","1",CVAR_NONE};

cvar_t	developer = {"developer","0",CVAR_NONE};

static cvar_t	pr_engine = {"pr_engine", ENGINE_NAME_AND_VER, CVAR_NONE};
cvar_t	temp1 = {"temp1","0",CVAR_NONE};

cvar_t devstats = {"devstats","0",CVAR_NONE}; //johnfitz -- track developer statistics that vary every frame

cvar_t	campaign = {"campaign","0",CVAR_NONE}; // for the 2021 rerelease
cvar_t	horde = {"horde","0",CVAR_NONE}; // for the 2021 rerelease
cvar_t	sv_cheats = {"sv_cheats","0",CVAR_NONE}; // for the 2021 rerelease

cvar_t	cl_menuskip = { "cl_menuskip","0",CVAR_ARCHIVE}; // woods #menuskip

devstats_t dev_stats, dev_peakstats;
overflowtimes_t dev_overflows; //this stores the last time overflow messages were displayed, not the last time overflows occured

extern cvar_t	pq_lag; // woods
extern char	lastmphost[NET_NAMELEN]; // woods - connected server address
extern char demoplaying[MAX_OSPATH]; // woods for window title

void SV_Next_Map_f(void); // woods #maprotation

/*
================
Max_Edicts_f -- johnfitz
================
*/
static void Max_Edicts_f (cvar_t *var)
{
	//TODO: clamp it here?
	if (cls.state == ca_connected || sv.active)
		Con_Printf ("Changes to max_edicts will not take effect until the next time a map is loaded.\n");
}

/*
================
Max_Fps_f -- ericw
================
*/
static void Max_Fps_f (cvar_t *var)
{
	if (var->value < 0)
	{
		if (!host_netinterval)
			Con_Printf ("Using renderer/network isolation.\n");
		host_netinterval = 1/-var->value;
		if (host_netinterval > 1/10.f)	//don't let it get too jerky for other players
			host_netinterval = 1/10.f;
		if (host_netinterval < 1/150.f)	//don't let us spam servers too often. just abusive.
			host_netinterval = 1/150.f;
	}
	else if (var->value > 72 || var->value <= 0)
	{
		if (!host_netinterval)
			Con_Printf ("Using renderer/network isolation.\n");
		host_netinterval = 1.0/72;
	}
	else
	{
		if (host_netinterval)
			Con_Printf ("Disabling renderer/network isolation.\n");
		host_netinterval = 0;

		if (var->value > 72)
			Con_Warning ("host_maxfps above 72 breaks physics.\n");
	}
}

/*
================
Host_EndGame
================
*/
void Host_EndGame (const char *message, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr,message);
	q_vsnprintf (string, sizeof(string), message, argptr);
	va_end (argptr);
	Con_DPrintf ("Host_EndGame: %s\n",string);

	PR_SwitchQCVM(NULL);

	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.state == ca_dedicated)
		Sys_Error ("Host_EndGame: %s\n",string);	// dedicated servers exit

	if (cls.demonum != -1 && !cls.timedemo)
		CL_NextDemo ();
	else
		CL_Disconnect ();

	longjmp (host_abortserver, 1);
}

/*
================
Host_Error

This shuts down both the client and server
================
*/
void Host_Error (const char *error, ...)
{
	va_list		argptr;
	char		string[1024];
	static	qboolean inerror = false;

	if (inerror)
		Sys_Error ("Host_Error: recursively entered");
	inerror = true;

	if (cl.qcvm.progs)
		glDisable(GL_SCISSOR_TEST);	//equivelent to drawresetcliparea, to reset any damage if we crashed in csqc.
	if (qcvm == &cls.menu_qcvm)
		MQC_Shutdown();
	PR_SwitchQCVM(NULL);

	SCR_EndLoadingPlaque ();		// reenable screen updates

	va_start (argptr,error);
	q_vsnprintf (string, sizeof(string), error, argptr);
	va_end (argptr);
	Con_Printf ("Host_Error: %s\n",string);

	Con_Redirect(NULL);

	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.state == ca_dedicated)
		Sys_Error ("Host_Error: %s\n",string);	// dedicated servers exit

	CL_Disconnect ();
	cls.demonum = -1;
	cl.intermission = 0; //johnfitz -- for errors during intermissions (changelevel with no map found, etc.)

	inerror = false;

	longjmp (host_abortserver, 1);
}

/*
================
Host_FindMaxClients
================
*/
void	Host_FindMaxClients (void)
{
	int		i;

	svs.maxclients = 1;

	i = COM_CheckParm ("-dedicated");
	if (i)
	{
		cls.state = ca_dedicated;
		if (i != (com_argc - 1))
		{
			svs.maxclients = Q_atoi (com_argv[i+1]);
		}
		else
			svs.maxclients = 16;
	}
	else
		cls.state = ca_disconnected;

	i = COM_CheckParm ("-listen");
	if (i)
	{
		if (cls.state == ca_dedicated)
			Sys_Error ("Only one of -dedicated or -listen can be specified");
		if (i != (com_argc - 1))
			svs.maxclients = Q_atoi (com_argv[i+1]);
		else
			svs.maxclients = 16;
	}
	if (svs.maxclients < 1)
		svs.maxclients = 16;
	else if (svs.maxclients > MAX_SCOREBOARD)
		svs.maxclients = MAX_SCOREBOARD;

	svs.maxclientslimit = MAX_SCOREBOARD;
	svs.clients = (struct client_s *) Hunk_AllocName (svs.maxclientslimit*sizeof(client_t), "clients");

	if (svs.maxclients > 1)
		Cvar_SetQuick (&deathmatch, "1");
	else
		Cvar_SetQuick (&deathmatch, "0");
}

void Host_Version_f (void)
{
	SDL_version sdl_linked;
	SDL_GetVersion(&sdl_linked);

	Con_Printf ("\n");
	Con_Printf ("Quake                    %1.2f\n", VERSION);
	Con_Printf ("QuakeSpasm               " QUAKESPASM_VER_STRING "\n");
	Con_Printf ("QuakeSpasm-Spiked        " QSS_VER "\n"); // woods
	Con_Printf ("QSS-M                    " QSSM_VER_STRING "\n"); // woods
#ifdef QSS_VERSION
	Con_Printf ("QSS Git Description      " QS_STRINGIFY(QSS_VERSION) "\n");
#endif
#ifdef QSS_REVISION
	Con_Printf ("QSS Git Revision         " QS_STRINGIFY(QSS_REVISION) "\n");
#endif
#ifdef QSS_DATE
	Con_Printf ("QuakeSpasm-Spiked Build  " QS_STRINGIFY(QSS_DATE) "\n");
#else
	Con_Printf ("Exe                      " __TIME__ " " __DATE__ "\n");
#endif
	Con_Printf ("SDL                      " Q_SDL_COMPILED_VERSION_STRING " (compiled)\n"); // woods (iw)
	Con_Printf ("                         %d.%d.%d (linked)\n", sdl_linked.major, sdl_linked.minor, sdl_linked.patch); // woods (iw)
	Con_Printf ("OS                       %s %d-bit\n", SDL_GetPlatform(), (int)sizeof(void*) * 8); // woods (iw)
	Con_Printf ("\n");
}

/* cvar callback functions : */
void Host_Callback_Notify (cvar_t *var)
{
	if (sv.active)
		SV_BroadcastPrintf ("\"%s\" changed to \"%s\"\n", var->name, var->string);
}

char dequake[256];	// JPG 1.05 // woods for #iplog to work

/*
=======================
Host_InitDeQuake // woods for #iplog to work
======================
*/
void Host_InitDeQuake (void)
{
	int i;

	for (i = 1; i < 12; i++)
		dequake[i] = '#';
	dequake[9] = 9;
	dequake[10] = 10;
	dequake[13] = 13;
	dequake[12] = ' ';
	dequake[1] = dequake[5] = dequake[14] = dequake[15] = dequake[28] = '.';
	dequake[16] = '[';
	dequake[17] = ']';
	for (i = 0; i < 10; i++)
		dequake[18 + i] = '0' + i;
	dequake[29] = '<';
	dequake[30] = '-';
	dequake[31] = '>';
	for (i = 32; i < 128; i++)
		dequake[i] = i;
	for (i = 0; i < 128; i++)
		dequake[i + 128] = dequake[i];
	dequake[128] = '(';
	dequake[129] = '=';
	dequake[130] = ')';
	dequake[131] = '*';
	dequake[141] = '>';
}

/*
===============
Menu_Skip_f -- woods #menuskip
===============
*/
void Menu_Skip_f(void)
{
	if (cl_menuskip.value)
		Cbuf_AddText("toggleconsole\n");
	if (cl_menuskip.value)
		Cbuf_AddText("togglemenu\n");
}

/*
=======================
Host_InitLocal
======================
*/
void Host_InitLocal (void)
{
	Cmd_AddCommand ("version", Host_Version_f);
	Cmd_AddCommand ("svnextmap", SV_Next_Map_f); // woods #maprotation
	Cmd_AddCommand ("menuskip", Menu_Skip_f); // woods #menuskip

	Host_InitCommands ();

	Cvar_RegisterVariable (&pr_engine);
	Cvar_RegisterVariable (&host_framerate);
	Cvar_RegisterVariable (&host_speeds);
	Cvar_RegisterVariable (&host_maxfps); //johnfitz
	Cvar_SetCallback (&host_maxfps, Max_Fps_f);
	Cvar_RegisterVariable (&host_timescale); //johnfitz

	Cvar_RegisterVariable (&cl_nocsqc);	//spike
	Cvar_RegisterVariable (&max_edicts); //johnfitz
	Cvar_SetCallback (&max_edicts, Max_Edicts_f);
	Cvar_RegisterVariable (&devstats); //johnfitz

	Cvar_RegisterVariable (&sys_ticrate);
	Cvar_RegisterVariable (&sys_throttle);
	Cvar_RegisterVariable (&serverprofile);

	Cvar_RegisterVariable (&fraglimit);
	Cvar_RegisterVariable (&timelimit);
	Cvar_RegisterVariable (&teamplay);
	Cvar_SetCallback (&fraglimit, Host_Callback_Notify);
	Cvar_SetCallback (&timelimit, Host_Callback_Notify);
	Cvar_SetCallback (&teamplay, Host_Callback_Notify);
	Cvar_RegisterVariable (&samelevel);
	Cvar_RegisterVariable (&noexit);
	Cvar_SetCallback (&noexit, Host_Callback_Notify);
	Cvar_RegisterVariable (&skill);
	Cvar_RegisterVariable (&developer);
	Cvar_RegisterVariable (&coop);
	Cvar_RegisterVariable (&deathmatch);

	Cvar_RegisterVariable (&campaign);
	Cvar_RegisterVariable (&horde);
	Cvar_RegisterVariable (&sv_cheats);

	Cvar_RegisterVariable (&cl_menuskip); // woods #menuskip

	Cvar_RegisterVariable (&pausable);

	Cvar_RegisterVariable (&temp1);

	Host_FindMaxClients ();

	host_time = 1.0;		// so a think at time 0 won't get called // woods #smoothcam
	last_angle_time = 0.0;  // JPG - smooth chasecam  // woods #smoothcam
	Host_InitDeQuake ();	// JPG 1.05 - initialize dequake array // for #iplog woods
}

/************************* PRINTING FUNCTIONS from ezquake *************************/ // woods #configprint

#define CONFIG_WIDTH 100

static void Config_PrintBorder(FILE* f)
{
	char buf[CONFIG_WIDTH + 1] = { 0 };

	if (!buf[0]) {
		memset(buf, '/', CONFIG_WIDTH);
		buf[CONFIG_WIDTH] = 0;
	}
	fprintf(f, "%s\n", buf);
}

static void Config_PrintLine(FILE* f, char* title, int width)
{
	char buf[CONFIG_WIDTH + 1] = { 0 };
	int title_len, i;

	width = bound(1, width, CONFIG_WIDTH << 3);

	for (i = 0; i < width; i++)
		buf[i] = buf[CONFIG_WIDTH - 1 - i] = '/';
	memset(buf + width, ' ', CONFIG_WIDTH - 2 * width);
	if (strlen(title) > CONFIG_WIDTH - (2 * width + 4))
		title = "Config_PrintLine : TITLE TOO BIG";
	title_len = strlen(title);
	memcpy(buf + width + ((CONFIG_WIDTH - title_len - 2 * width) >> 1), title, title_len);
	buf[CONFIG_WIDTH] = 0;
	fprintf(f, "%s\n", buf);
}

static void Config_PrintHeading(FILE* f, char* title)
{
	fprintf(f, "\n");
	Config_PrintBorder(f);
	Config_PrintLine(f, "", 2);
	Config_PrintLine(f, title, 2);
	Config_PrintLine(f, "", 2);
	Config_PrintBorder(f);
	fprintf(f, "\n");
}

static void Config_PrintPreamble(FILE* f)
{
	extern cvar_t cl_name;

	// woods added time
	char str[24];
	time_t systime = time(0);
	struct tm loct = *localtime(&systime);
	strftime(str, 24, "%m-%d-%Y-%H:%M", &loct);

	Config_PrintBorder(f);
	Config_PrintBorder(f);
	Config_PrintLine(f, "", 3);
	Config_PrintLine(f, "", 3);
	Config_PrintLine(f, "Q S S M   C O N F I G U R A T I O N", 3);
	Config_PrintLine(f, "https://qssm.quakeone.com", 3);
	Config_PrintLine(f, "", 3);
	Config_PrintLine(f, "", 3);
	Config_PrintBorder(f);
	Config_PrintBorder(f);
	fprintf(f, "\n// %s's config (%s)\n", cl_name.string, str);
	fprintf(f, "// "ENGINE_NAME_AND_VER"\n");

}

/*
===============
Host_WriteConfigurationToFile - woods - ironwail #writecfg

Writes key bindings and archived cvars to specified file
===============
*/
void Host_WriteConfigurationToFile (const char* name)
{
	FILE	*f;

// dedicated servers initialize the host but don't parse and set the
// config.cfg cvars
	if (host_initialized && !isDedicated && !host_parms->errstate)
	{
		f = fopen (va("%s/%s", com_gamedir, name), "w");
		if (!f)
		{
			Con_Printf ("Couldn't write %s.\n", name);
			return;
		}

		//VID_SyncCvars (); //johnfitz -- write actual current mode to config file, in case cvars were messed with

		Config_PrintPreamble(f);

		Config_PrintHeading(f, "K E Y   B I N D I N G S"); // woods #configprint
		Key_WriteBindings (f);
		Config_PrintHeading(f, "V A R I A B L E S"); // woods #configprint
		Cvar_WriteVariables (f);

		Config_PrintHeading(f, "M I S C E L L A N E O U S"); // woods #configprint
		//johnfitz -- extra commands to preserve state
		fprintf (f, "vid_restart\n");
		if (in_mlook.state & 1) fprintf (f, "+mlook\n");
		//johnfitz

		fclose (f);

		Con_Printf("Wrote %s.\n", name);
	}
}

/*
===============
Host_WriteConfiguration  - woods - ironwail #writecfg

Writes key bindings and archived cvars to engine config file
===============
*/
void Host_WriteConfiguration(void)
{
	Host_WriteConfigurationToFile("config.cfg");
}

/*
=======================
Host_WriteConfig_f  - woods - ironwail #writecfg
======================
*/
void Host_WriteConfig_f(void)
{
	char filename[MAX_QPATH];
	q_strlcpy(filename, Cmd_Argc() >= 2 ? Cmd_Argv(1) : "config.cfg", sizeof(filename));
	COM_AddExtension(filename, ".cfg", sizeof(filename));
	Host_WriteConfigurationToFile(filename);
}

/*
===============
Host_BackupConfiguration // woods #cfgbackup
===============
*/
void Host_BackupConfiguration(void)
{
	FILE* f;

	char	name[MAX_OSPATH];
	
	char str[24];
	time_t systime = time(0);
	struct tm loct = *localtime(&systime);

	// dedicated servers initialize the host but don't parse and set the
	// config.cfg cvars
	if (host_initialized && !isDedicated && !host_parms->errstate)
	{	
		strftime(str, 24, "config-%m-%d-%Y", &loct);
		
		q_snprintf(name, sizeof(name), "%s/id1", com_basedir); //  make an id1 folder if it doesnt exist already #smartafk
		Sys_mkdir(name);

		f = fopen(va("%s/%s", com_gamedir, "config.cfg"), "r");

		if (f)
		{
			q_snprintf(name, sizeof(name), "%s/backups", com_gamedir); //  create backups folder if not there
			Sys_mkdir(name);
		}
		
		f = fopen(va("%s/backups/%s.cfg", com_gamedir, str), "w");
		if (!f)
		{
			Con_Printf("Couldn't write backup config.cfg.\n");
			return;
		}

		//VID_SyncCvars (); //johnfitz -- write actual current mode to config file, in case cvars were messed with

		Config_PrintPreamble(f);

		Config_PrintHeading(f, "K E Y   B I N D I N G S"); // woods #configprint
		Key_WriteBindings(f);
		Config_PrintHeading(f, "V A R I A B L E S"); // woods #configprint
		Cvar_WriteVariables(f);

		Config_PrintHeading(f, "M I S C E L L A N E O U S"); // woods #configprint
		//johnfitz -- extra commands to preserve state
		fprintf(f, "vid_restart\n");
		if (in_mlook.state & 1) fprintf(f, "+mlook\n");
		//johnfitz

		fclose(f);
	}
}

/*
=================
SV_ClientPrintf

Sends text across to be displayed
FIXME: make this just a stuffed echo?
=================
*/
void SV_ClientPrintf (const char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr,fmt);
	q_vsnprintf (string, sizeof(string), fmt,argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_print);
	MSG_WriteString (&host_client->message, string);
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients
=================
*/
void SV_BroadcastPrintf (const char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];
	int			i;

	va_start (argptr,fmt);
	q_vsnprintf (string, sizeof(string), fmt, argptr);
	va_end (argptr);

	for (i = 0; i < svs.maxclients; i++)
	{
		if (svs.clients[i].active && svs.clients[i].spawned)
		{
			MSG_WriteByte (&svs.clients[i].message, svc_print);
			MSG_WriteString (&svs.clients[i].message, string);
		}
	}
}

/*
=================
Host_ClientCommands

Send text over to the client to be executed
=================
*/
void Host_ClientCommands (const char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr,fmt);
	q_vsnprintf (string, sizeof(string), fmt, argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_stufftext);
	MSG_WriteString (&host_client->message, string);
}

/*
=====================
SV_DropClient

Called when the player is getting totally kicked off the host
if (crash = true), don't bother sending signofs
=====================
*/
void SV_DropClient (qboolean crash)
{
	int		saveSelf;
	int		i;
	client_t *client;

	if (!crash)
	{
		// send any final messages (don't check for errors)
		if (NET_CanSendMessage (host_client->netconnection))
		{
			MSG_WriteByte (&host_client->message, svc_disconnect);
			NET_SendMessage (host_client->netconnection, &host_client->message);
		}

		if (host_client->edict && host_client->spawned)
		{
		// call the prog function for removing a client
		// this will set the body to a dead frame, among other things
			qcvm_t *oldvm = qcvm;
			PR_SwitchQCVM(NULL);
			PR_SwitchQCVM(&sv.qcvm);
			saveSelf = pr_global_struct->self;
			pr_global_struct->self = EDICT_TO_PROG(host_client->edict);
			PR_ExecuteProgram (pr_global_struct->ClientDisconnect);
			pr_global_struct->self = saveSelf;
			PR_SwitchQCVM(NULL);
			PR_SwitchQCVM(oldvm);
		}

		Sys_Printf ("Client %s removed\n",host_client->name);
	}

// break the net connection
	NET_Close (host_client->netconnection);
	host_client->netconnection = NULL;

	SVFTE_DestroyFrames(host_client);	//release any delta state

// free the client (the body stays around)
	host_client->active = false;
	host_client->name[0] = 0;
	host_client->old_frags = -999999;
	net_activeconnections--;

	if (host_client->download.file)
		fclose(host_client->download.file);
	memset(&host_client->download, 0, sizeof(host_client->download));

// send notification to all clients
	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
		if (!client->knowntoqc)
			continue;
		if ((host_client->protocol_pext1 & PEXT1_CSQC) || (host_client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
		{
			MSG_WriteByte (&client->message, svc_stufftext);
			MSG_WriteString (&client->message, va("//fui %u \"\"\n", (unsigned)(host_client - svs.clients)));
		}
		
		{
			MSG_WriteByte (&client->message, svc_updatename);
			MSG_WriteByte (&client->message, host_client - svs.clients);
			MSG_WriteString (&client->message, "");
			MSG_WriteByte (&client->message, svc_updatecolors);
			MSG_WriteByte (&client->message, host_client - svs.clients);
			MSG_WriteByte (&client->message, 0);
		}
		MSG_WriteByte (&client->message, svc_updatefrags);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteShort (&client->message, 0);
	}
}

/*
==================
Host_ShutdownServer

This only happens at the end of a game, not between levels
==================
*/
void Host_ShutdownServer(qboolean crash)
{
	int		i;
	int		count;
	sizebuf_t	buf;
	byte		message[4];
	double	start;

	if (!sv.active)
		return;

	sv.active = false;

// stop all client sounds immediately
	if (cls.state == ca_connected)
		CL_Disconnect ();

// flush any pending messages - like the score!!!
	start = Sys_DoubleTime();
	do
	{
		count = 0;
		NET_GetServerMessage();	//read packets to make sure we're receiving their acks. we're going to drop them all so we don't actually care to read the data, just the acks so we can flush our outgoing properly.
		for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
		{
			if (host_client->active && host_client->message.cursize && host_client->netconnection)
			{
				if (NET_CanSendMessage (host_client->netconnection))	//also sends pending data too.
				{
					NET_SendMessage(host_client->netconnection, &host_client->message);
					SZ_Clear (&host_client->message);
				}
				else
					count++;
			}
		}
		if ((Sys_DoubleTime() - start) > 3.0)
			break;
	}
	while (count);

// make sure all the clients know we're disconnecting
	buf.data = message;
	buf.maxsize = 4;
	buf.cursize = 0;
	MSG_WriteByte(&buf, svc_disconnect);
	count = NET_SendToAll(&buf, 5.0);
	if (count)
		Con_Printf("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);

	PR_SwitchQCVM(&sv.qcvm);
	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		if (host_client->active)
			SV_DropClient(crash);
	
	qcvm->worldmodel = NULL;
	PR_SwitchQCVM(NULL);

//
// clear structures
//
//	memset (&sv, 0, sizeof(sv)); // ServerSpawn already do this by Host_ClearMemory
	memset (svs.clients, 0, svs.maxclientslimit*sizeof(client_t));
}


/*
================
Host_ClearMemory

This clears all the memory used by both the client and server, but does
not reinitialize anything.
================
*/
void Host_ClearMemory (void)
{
	if (cl.qcvm.extfuncs.CSQC_Shutdown)
	{
		PR_SwitchQCVM(&cl.qcvm);
		PR_ExecuteProgram(qcvm->extfuncs.CSQC_Shutdown);
		qcvm->extfuncs.CSQC_Shutdown = 0;
		PR_SwitchQCVM(NULL);
	}

	Con_DPrintf ("Clearing memory\n");
	D_FlushCaches ();
	Mod_ClearAll ();
	Sky_ClearAll();
/* host_hunklevel MUST be set at this point */
	Hunk_FreeToLowMark (host_hunklevel);
	cls.signon = 0;
	PR_ClearProgs(&sv.qcvm);
	free(sv.static_entities);	//spike -- this is dynamic too, now
	free(sv.ambientsounds);
	memset (&sv, 0, sizeof(sv));

	CL_FreeState();
}


//==============================================================================
//
// Host Frame
//
//==============================================================================

/*
===================
Host_FilterTime

Returns false if the time is too short to run a frame
===================
*/


qboolean Host_FilterTime (float time)
{
	float maxfps; //johnfitz

	realtime += time;

	//johnfitz -- max fps cvar
	maxfps = CLAMP (10.f, host_maxfps.value, 5000.0); // woods higher max
	if (host_maxfps.value>0 && !cls.timedemo && realtime - oldrealtime < 1.0/maxfps)
		return false; // framerate is too high
	//johnfitz

	host_frametime = realtime - oldrealtime;
	oldrealtime = realtime;

	//johnfitz -- host_timescale is more intuitive than host_framerate
	if (host_timescale.value > 0)
		host_frametime *= host_timescale.value;
	//johnfitz
	else if (host_framerate.value > 0)
		host_frametime = host_framerate.value;
	else if (host_maxfps.value > 0)// don't allow really long or short frames
		host_frametime = CLAMP (0.0001, host_frametime, 0.1); //johnfitz -- use CLAMP

	return true;
}

/*
===================
Host_GetConsoleCommands

Add them exactly as if they had been typed at the console
===================
*/
void Host_GetConsoleCommands (void)
{
	const char	*cmd;

	if (!isDedicated)
		return;	// no stdin necessary in graphical mode

	while (1)
	{
		cmd = Sys_ConsoleInput ();
		if (!cmd)
			break;
		Cbuf_AddText (cmd);
	}
}

/*
==================
Host_ServerFrame
==================
*/
void Host_ServerFrame (void)
{
	int		i, active; //johnfitz
	edict_t	*ent; //johnfitz

// run the world state
	pr_global_struct->frametime = host_frametime;

// set the time and clear the general datagram
	SV_ClearDatagram ();

//respond to cvar changes
	PMSV_UpdateMovevars ();

// check for new clients
	SV_CheckForNewClients ();

// read client messages
	SV_RunClients ();

// move things around and think
// always pause in single player if in console or menus
	if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game) )
		SV_Physics (host_frametime);

//johnfitz -- devstats
	if (cls.signon == SIGNONS)
	{
		for (i=0, active=0; i<qcvm->num_edicts; i++)
		{
			ent = EDICT_NUM(i);
			if (!ent->free)
				active++;
		}
		if (active > 600 && dev_peakstats.edicts <= 600)
			Con_DWarning ("%i edicts exceeds standard limit of 600 (max = %d).\n", active, qcvm->max_edicts);
		dev_stats.edicts = active;
		dev_peakstats.edicts = q_max(active, dev_peakstats.edicts);
	}
//johnfitz

// send all messages to the clients
	SV_SendClientMessages ();
}

typedef struct summary_s {
	struct {
		int		skill;
		int		monsters;
		int		total_monsters;
		int		secrets;
		int		total_secrets;
	}			stats;
	char		map[countof(cl.mapname)];
} summary_t;

/*
==================
GetGameSummary - github.com/andrei-drexler/ironwail (Show game summary in window title)
==================
*/
static void GetGameSummary(summary_t* s)
{
	if (cls.state != ca_connected || cls.signon != SIGNONS)
	{
		s->map[0] = 0;
		memset(&s->stats, 0, sizeof(s->stats));
	}
	else
	{
		q_strlcpy(s->map, cl.mapname, countof(s->map));
		s->stats.skill = (int)skill.value;
		s->stats.monsters = cl.stats[STAT_MONSTERS];
		s->stats.total_monsters = cl.stats[STAT_TOTALMONSTERS];
		s->stats.secrets = cl.stats[STAT_SECRETS];
		s->stats.total_secrets = cl.stats[STAT_TOTALSECRETS];
	}
}

/*
==================
UpdateWindowTitle - github.com/andrei-drexler/ironwail (Show game summary in window title)
==================
*/
static void UpdateWindowTitle(void)
{
	static float timeleft = 0.f;
	static summary_t last;
	summary_t current;

	timeleft -= host_frametime;
	if (timeleft > 0.f)
		return;
	timeleft = 0.125f;

	GetGameSummary(&current);
	if (!cls.demoplayback || (cl.gametype != GAME_DEATHMATCH && cls.state != ca_connected)) // woods 
		if (!strcmp(current.map, last.map) && !memcmp(&current.stats, &last.stats, sizeof(current.stats)))
			return;
	last = current;

	if (current.map[0])
	{
		char title[1024];
		unsigned char* ch;
		char ln[128];

		strcpy(ln, cl.levelname); // woods dequake
		for (ch = (unsigned char*)ln; *ch; ch++)
		{
			*ch = dequake[*ch];
			if (*ch == 10 || *ch == 13)
				*ch = ' ';
		}

		if ((cl.gametype == GAME_DEATHMATCH) && (cls.state == ca_connected) && !cls.demoplayback) // woods added connected server
			q_snprintf(title, sizeof(title), "%s  |  %s (%s)  -  " ENGINE_NAME_AND_VER, lastmphost, ln, current.map);
		else if (cls.demoplayback) // woods added demofile
			q_snprintf(title, sizeof(title), "%s (%s)  |  %s  -  " ENGINE_NAME_AND_VER, ln, current.map, demoplaying);
		else
			q_snprintf(title, sizeof(title),
				"%s (%s)  |  skill %d  |  %d/%d kills  |  %d/%d secrets  -  " ENGINE_NAME_AND_VER,
				ln, current.map,
				current.stats.skill,
				current.stats.monsters, current.stats.total_monsters,
				current.stats.secrets, current.stats.total_secrets
			);
		VID_SetWindowTitle(title);
	}
	else
	{
		VID_SetWindowTitle(ENGINE_NAME_AND_VER);
	}
}

//used for cl.qcvm.GetModel (so ssqc+csqc can share builtins)
qmodel_t *CL_ModelForIndex(int index)
{
	if (index < 0 || index >= MAX_MODELS)
		return NULL;
	return cl.model_precache[index];
}


static void CL_LoadCSProgs(void)
{
	qboolean fullcsqc = false;
	int i;
	PR_ClearProgs(&cl.qcvm);
	PR_SwitchQCVM(&cl.qcvm);
	if (pr_checkextension.value && !cl_nocsqc.value)
	{	//only try to use csqc if qc extensions are enabled.
		char versionedname[MAX_QPATH];
		unsigned int csqchash;
		size_t csqcsize;
		const char *val;
		val = Info_GetKey(cl.serverinfo, "*csprogs", versionedname, sizeof(versionedname));
		csqchash = (unsigned int)strtoul(val, NULL, 0);
		if (*val)
			snprintf(versionedname, MAX_QPATH, "csprogsvers/%x.dat", csqchash);
		else
			*versionedname = 0;
		csqcsize = strtoul(Info_GetKey(cl.serverinfo, "*csprogssize", versionedname, sizeof(versionedname)), NULL, 0);

		//try csprogs.dat first, then fall back on progs.dat in case someone tried merging the two.
		//we only care about it if it actually contains a CSQC_DrawHud, otherwise its either just a (misnamed) ssqc progs or a full csqc progs that would just crash us on 3d stuff.
		if ((*versionedname && PR_LoadProgs(versionedname, false, PROGHEADER_CRC, pr_csqcbuiltins, pr_csqcnumbuiltins) && (qcvm->extfuncs.CSQC_DrawHud||cl.qcvm.extfuncs.CSQC_UpdateView))||
			(PR_LoadProgs("csprogs.dat", false, PROGHEADER_CRC, pr_csqcbuiltins, pr_csqcnumbuiltins) && (qcvm->extfuncs.CSQC_DrawHud||qcvm->extfuncs.CSQC_DrawScores||cl.qcvm.extfuncs.CSQC_UpdateView))||
			(PR_LoadProgs("progs.dat",   false, PROGHEADER_CRC, pr_csqcbuiltins, pr_csqcnumbuiltins) && (qcvm->extfuncs.CSQC_DrawHud||cl.qcvm.extfuncs.CSQC_UpdateView)))
		{
			qcvm->max_edicts = CLAMP (MIN_EDICTS,(int)max_edicts.value,MAX_EDICTS);
			qcvm->edicts = (edict_t *) malloc (qcvm->max_edicts*qcvm->edict_size);
			qcvm->num_edicts = qcvm->reserved_edicts = 1;
			memset(qcvm->edicts, 0, qcvm->num_edicts*qcvm->edict_size);
			for (i = 0; i < qcvm->num_edicts; i++)
				EDICT_NUM(i)->baseline = nullentitystate;

			//in terms of exploit protection this is kinda pointless as someone can just strip out this check and compile themselves. oh well.
			if ((*versionedname && qcvm->progshash == csqchash && qcvm->progssize == csqcsize) || cls.demoplayback)
				fullcsqc = true;
			else
			{	//okay, it doesn't match. full csqc is disallowed to prevent cheats, but we still allow simplecsqc...
				if (!qcvm->extfuncs.CSQC_DrawHud)
				{	//no simplecsqc entry points... abort entirely!
					PR_ClearProgs(qcvm);
					PR_SwitchQCVM(NULL);
					return;
				}
				fullcsqc = false;
				qcvm->nogameaccess = true;

				qcvm->extfuncs.CSQC_Input_Frame = 0;		//prevent reading/writing input frames (no wallhacks please).
				qcvm->extfuncs.CSQC_UpdateView = 0;		//will probably bug out. block it.
				qcvm->extfuncs.CSQC_Ent_Update = 0;		//don't let the qc know where ents are... the server should prevent this, but make sure the user didn't cheese a 'cmd enablecsqc'
				qcvm->extfuncs.CSQC_Ent_Remove = 0;
				qcvm->extfuncs.CSQC_Parse_StuffCmd = 0;	//don't allow blocking stuffcmds... though we can't prevent cvar queries+sets, so this is probably futile...

				qcvm->extglobals.clientcommandframe = NULL;	//input frames are blocked, so don't bother to connect these either.
				qcvm->extglobals.servercommandframe = NULL;
			}

			qcvm->rotatingbmodel = true;	//csqc always assumes this is enabled.
			qcvm->GetModel = PR_CSQC_GetModel;
			//set a few globals, if they exist
			if (qcvm->extglobals.maxclients)
				*qcvm->extglobals.maxclients = cl.maxclients;
			pr_global_struct->time = qcvm->time = cl.time;
			pr_global_struct->mapname = PR_SetEngineString(cl.mapname);
			pr_global_struct->total_monsters = cl.statsf[STAT_TOTALMONSTERS];
			pr_global_struct->total_secrets = cl.statsf[STAT_TOTALSECRETS];
			pr_global_struct->deathmatch = cl.gametype;
			pr_global_struct->coop = (cl.gametype == GAME_COOP) && cl.maxclients != 1;
			if (qcvm->extglobals.player_localnum)
				*qcvm->extglobals.player_localnum = cl.viewentity-1;	//this is a guess, but is important for scoreboards.

			//set a few worldspawn fields too
			qcvm->edicts->v.solid = SOLID_BSP;
			qcvm->edicts->v.movetype = MOVETYPE_PUSH;
			qcvm->edicts->v.modelindex = 1;
			qcvm->edicts->v.model = PR_SetEngineString(cl.worldmodel->name);
			VectorCopy(cl.worldmodel->mins, qcvm->edicts->v.mins);
			VectorCopy(cl.worldmodel->maxs, qcvm->edicts->v.maxs);
			qcvm->edicts->v.message = PR_SetEngineString(cl.levelname);

			//and call the init function... if it exists.
			qcvm->worldmodel = cl.worldmodel;
			SV_ClearWorld();
			if (qcvm->extfuncs.CSQC_Init)
			{
				int maj = (int)QUAKESPASM_VERSION;
				int min = (QUAKESPASM_VERSION-maj) * 100;
				G_FLOAT(OFS_PARM0) = fullcsqc;
				G_INT(OFS_PARM1) = PR_SetEngineString("QuakeSpasm-Spiked");
				G_FLOAT(OFS_PARM2) = 10000*maj + 100*(min) + QUAKESPASM_VER_PATCH;
				PR_ExecuteProgram(qcvm->extfuncs.CSQC_Init);
			}
			qcvm->worldlocked = true;

			if (fullcsqc)
			{
				//let the server know.
				MSG_WriteByte (&cls.message, clc_stringcmd);
				MSG_WriteString (&cls.message, "enablecsqc");
			}
		}
		else
		{
			PR_ClearProgs(qcvm);
			qcvm->worldmodel = cl.worldmodel;
			SV_ClearWorld();
		}
	}
	else
	{	//always initialsing at least part of it, allowing us to share some state with prediction.
		qcvm->worldmodel = cl.worldmodel;
		SV_ClearWorld();
	}
	PR_SwitchQCVM(NULL);
}

/*
==================
Host_Frame

Runs all active servers
==================
*/
void _Host_Frame (double time)
{
	static double	accumtime = 0;
	static double		time1 = 0;
	static double		time2 = 0;
	static double		time3 = 0;
	int			pass1, pass2, pass3;

	if (setjmp (host_abortserver) )
		return;			// something bad happened, or the server disconnected

// keep the random time dependent
	rand ();

// decide the simulation time
	accumtime += host_netinterval?CLAMP(0, time, 0.2):0;	//for renderer/server isolation
	if (!Host_FilterTime (time))
	{
		// JPG - if we're not doing a frame, still check for lagged moves to send // woods #pqlag
		if (pq_lag.value)
		{ 
			if (!sv.active && (cl.movemessages > 2))
				CL_SendLagMove ();
		}
		return;			// don't run too fast, or packets will flood out
	}
	if (!isDedicated) // woods  -- don't call the input functions in dedicated servers (vkquake)
	{ 
		// get new key events
		Key_UpdateForDest ();
		IN_UpdateInputMode ();
		Sys_SendKeyEvents ();

		// allow mice or other external controllers to add commands
		IN_Commands ();
	}

//check the stdin for commands (dedicated servers)
	Host_GetConsoleCommands ();

// process console commands
	Cbuf_Execute ();

	NET_Poll();

	if (cl.sendprespawn)
	{
		if (CL_CheckDownloads())
		{
			CL_LoadCSProgs();

			cl.sendprespawn = false;
			MSG_WriteByte (&cls.message, clc_stringcmd);
			MSG_WriteString (&cls.message, "prespawn");
			vid.recalc_refdef = true;
		}
		else if (!cls.message.cursize)
			MSG_WriteByte (&cls.message, clc_nop);
	}

	CL_AccumulateCmd ();

	//Run the server+networking (client->server->client), at a different rate from everything else
	if (accumtime >= host_netinterval)
	{
		float realframetime = host_frametime;
		if (host_netinterval)
		{
			host_frametime = q_max(accumtime, host_netinterval);
			accumtime -= host_frametime;
			if (host_timescale.value > 0)
				host_frametime *= host_timescale.value;
			else if (host_framerate.value)
				host_frametime = host_framerate.value;
		}
		else
			accumtime -= host_netinterval;
		CL_SendCmd ();
		if (sv.active)
		{
			PR_SwitchQCVM(&sv.qcvm);
			Host_ServerFrame ();
			PR_SwitchQCVM(NULL);
		}
		host_frametime = realframetime;
		Cbuf_Waited();
	}

	host_time += host_frametime; // woods smoothcam  #smoothcam

	if (cl.qcvm.progs)
	{
		PR_SwitchQCVM(&cl.qcvm);
		SV_Physics(cl.time - qcvm->time);
		pr_global_struct->time = cl.time;
		PR_SwitchQCVM(NULL);
	}

// fetch results from server
	if (cls.state == ca_connected)
		CL_ReadFromServer ();

// update video
	if (host_speeds.value)
		time1 = Sys_DoubleTime ();

	SCR_UpdateScreen ();

	CL_RunParticles (); //johnfitz -- seperated from rendering

	if (host_speeds.value)
		time2 = Sys_DoubleTime ();

// update audio
	BGM_Update();	// adds music raw samples and/or advances midi driver
	if (cl.listener_defined)
	{
		cl.listener_defined = false;
		S_Update (cl.listener_origin, cl.listener_axis[0], cl.listener_axis[1], cl.listener_axis[2]);
	}
	else if (cls.signon == SIGNONS)
		S_Update (r_origin, vpn, vright, vup);
	else
		S_Update (vec3_origin, vec3_origin, vec3_origin, vec3_origin);
	CL_DecayLights ();

	CDAudio_Update();
	UpdateWindowTitle(); // github.com/andrei-drexler/ironwail (Show game summary in window title)

	if (host_speeds.value)
	{
		pass1 = (time1 - time3)*1000;
		time3 = Sys_DoubleTime ();
		pass2 = (time2 - time1)*1000;
		pass3 = (time3 - time2)*1000;
		Con_Printf ("%3i tot %3i server %3i gfx %3i snd\n",
					pass1+pass2+pass3, pass1, pass2, pass3);
	}

	host_framecount++;

}

void Host_Frame (double time)
{
	double	time1, time2;
	static double	timetotal;
	static int		timecount;
	int		i, c, m;

	if (!serverprofile.value)
	{
		_Host_Frame (time);
		return;
	}

	time1 = Sys_DoubleTime ();
	_Host_Frame (time);
	time2 = Sys_DoubleTime ();

	timetotal += time2 - time1;
	timecount++;

	if (timecount < 1000)
		return;

	m = timetotal*1000/timecount;
	timecount = 0;
	timetotal = 0;
	c = 0;
	for (i = 0; i < svs.maxclients; i++)
	{
		if (svs.clients[i].active)
			c++;
	}

	Con_Printf ("serverprofile: %2i clients %2i msec\n",  c,  m);
}

/*
====================
Host_Init
====================
*/
void Host_Init (void)
{
	extern void LOC_PQ_Init (void);    // rook / woods #pqteam (added PQ to name)

	if (standard_quake)
		minimum_memory = MINIMUM_MEMORY;
	else	minimum_memory = MINIMUM_MEMORY_LEVELPAK;

	if (COM_CheckParm ("-minmemory"))
		host_parms->memsize = minimum_memory;

	if (host_parms->memsize < minimum_memory)
		Sys_Error ("Only %4.1f megs of memory available, can't execute game", host_parms->memsize / (float)0x100000);

	com_argc = host_parms->argc;
	com_argv = host_parms->argv;

	Memory_Init (host_parms->membase, host_parms->memsize);
	Cbuf_Init ();
	Cmd_Init ();
	LOG_Init (host_parms);
	Cvar_Init (); //johnfitz
	COM_Init ();
	COM_InitFilesystem ();
	Host_InitLocal ();
	W_LoadWadFile (); //johnfitz -- filename is now hard-coded for honesty
	if (cls.state != ca_dedicated)
	{
		Key_Init ();
		Con_Init ();
	}
	PR_Init ();
	Mod_Init ();
	NET_Init ();
	SV_Init ();

	LOC_PQ_Init (); // rook / woods #pqteam (added PQ to name)
	if (cls.state != ca_dedicated)
		IPLog_Init ();		// JPG 1.05 - ip address logging // woods #iplog

#ifdef QSS_DATE	//avoid non-determinism.
	Con_Printf ("Exe: " ENGINE_NAME_AND_VER "\n");
#else
	Con_Printf ("Exe: " __TIME__ " " __DATE__ "\n");
#endif
	Con_Printf ("%4.1f megabyte heap\n", host_parms->memsize/ (1024*1024.0));

	if (cls.state != ca_dedicated)
	{
		host_colormap = (byte *)COM_LoadHunkFile ("gfx/colormap.lmp", NULL);
		if (!host_colormap)
			Sys_Error ("Couldn't load gfx/colormap.lmp");

		V_Init ();
		Chase_Init ();
		ExtraMaps_Init (); //johnfitz
		Modlist_Init (); //johnfitz
		DemoList_Init (); //ericw
		SkyList_Init (); // woods #skylist
		ExecList_Init(); // woods #execlist
		TextList_Init(); // woods #textlist
		ParticleList_Init (); // woods #particlelist
		ServerList_Init(); // woods #serverlist
		BookmarksList_Init (); // woods #bookmarksmenu
		FolderList_Init(); // woods #folderlist
		MusicList_Init (); // woods #musiclist
		M_CheckMods (); // woods #modsmenu (iw)
		VID_Init ();
		IN_Init ();
		TexMgr_Init (); //johnfitz
		Draw_Init ();
		SCR_Init ();
		R_Init ();
		S_Init ();
		CDAudio_Init ();
		BGM_Init();
		Sbar_Init ();
		CL_Init ();
		M_Init(); // woods move this up for tab complete system #iwtabcomplete
	}

	LOC_Init (); // for 2021 rerelease support.

	Hunk_AllocName (0, "-HOST_HUNKLEVEL-");
	host_hunklevel = Hunk_LowMark ();

	host_initialized = true;
	Con_Printf ("\n========= Quake Initialized =========\n\n");

	if (setjmp (host_abortserver) )
		return;			// something bad happened		
	//okay... now we can do stuff that's allowed to Host_Error

	//spike -- create these aliases, because they're useful.
	Cbuf_AddText ("alias startmap_sp \"map start\"\n");
	Cbuf_AddText ("alias startmap_dm \"map start\"\n");

	if (setjmp (host_abortserver) )
		return;			// don't do the above twice if the following Cbuf_Execute does bad things.

	if (cls.state != ca_dedicated)
	{
		Cbuf_AddText ("cl_warncmd 0\n");
		Cbuf_InsertText ("exec quake.rc\n");
		Cbuf_AddText ("cl_warncmd 1\n");
	// johnfitz -- in case the vid mode was locked during vid_init, we can unlock it now.
		// note: two leading newlines because the command buffer swallows one of them.
		Cbuf_AddText ("\n\nvid_unlock\n");
		Cbuf_AddText("menuskip\n"); // woods #menuskip
		Cbuf_AddText("namebk\n"); // woods #smartafk lets run a backup name check for AFK leftovers (crash/force quit)
	}

	if (cls.state == ca_dedicated)
	{
		Cbuf_AddText ("cl_warncmd 0\n");
		Cbuf_AddText ("exec default.cfg\n");	//spike -- someone decided that quake.rc shouldn't be execed on dedicated servers, but that means you'll get bad defaults
		Cbuf_AddText ("cl_warncmd 1\n");
		Cbuf_AddText ("exec server.cfg\n");		//spike -- for people who want things explicit.
		Cbuf_AddText ("exec autoexec.cfg\n");
		Cbuf_AddText ("stuffcmds\n");
		Cbuf_Execute ();
		if (!sv.active)
			Cbuf_AddText ("startmap_dm\n");
	}
}


/*
===============
Host_Shutdown

FIXME: this is a callback from Sys_Quit and Sys_Error.  It would be better
to run quit through here before the final handoff to the sys code.
===============
*/
void Host_Shutdown(void)
{
	static qboolean isdown = false;

	if (isdown)
	{
		printf ("recursive shutdown\n");
		return;
	}
	isdown = true;

// keep Con_Printf from trying to update the screen
	scr_disabled_for_loading = true;

	Host_WriteConfiguration ();

	Host_BackupConfiguration (); // woods #cfgbackup

	IPLog_WriteLog ();	// JPG 1.05 - ip loggging  // woods #iplog

	NET_Shutdown ();

	if (cls.state != ca_dedicated)
	{
		if (con_initialized)
			History_Shutdown ();
		BGM_Shutdown();
		CDAudio_Shutdown ();
		S_Shutdown ();
		IN_Shutdown ();
		VID_Shutdown();
	}

	LOG_Close ();

	LOC_Shutdown ();
}
