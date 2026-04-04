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

// This is enables a simple IP banning mechanism
#define BAN_TEST

#include "q_stdinc.h"
#include "arch_def.h"
#include "net_sys.h"
#include "quakedef.h"
#include "net_defs.h"
#include "ice/ice_quake.h"
#include "net_dgrm.h"

// these two macros are to make the code more readable
#define sfunc	net_landrivers[sock->landriver]
#define dfunc	net_landrivers[net_landriverlevel]

static int net_landriverlevel;

/* statistic counters */
static int packetsSent = 0;
static int packetsReSent = 0;
static int packetsReceived = 0;
static int receivedDuplicateCount = 0;
static int shortPacketCount = 0;
static int droppedDatagrams;

//cvars controlling dpmaster support:
//our servers might as well claim to be 'FTE-Quake' servers. this means FTE can see us, we can see FTE (when its pretending to be nq).
//we additionally look for 'DarkPlaces-Quake' servers too, because we can, but most of those servers will be using dpp7 and will (safely) not respond to our ccreq_server_info requests.
//we are not visible to DarkPlaces users - dp does not support fitz666 so that's not a viable option, at least by default, feel free to switch the order if you also change sv_protocol back to 15.
cvar_t sv_reportheartbeats = {"sv_reportheartbeats", "0"};
cvar_t sv_heartbeat_interval = {"sv_heartbeat_interval", "110"};
cvar_t sv_public = {"sv_public", NULL};
cvar_t com_protocolname = {"com_protocolname", "FTE-Quake DarkPlaces-Quake"};
cvar_t password = {"password", ""};	//this is super-lame and limited to numbers, so when not numeric we hash it and use that instead. there's no nonces though.
cvar_t cl_portpingprobe_enable = {"cl_portpingprobe_enable", "0", CVAR_ARCHIVE};
cvar_t cl_portpingprobe_probes = {"cl_portpingprobe_probes", "500", CVAR_ARCHIVE};
cvar_t cl_portpingprobe_delay = {"cl_portpingprobe_delay", "0", CVAR_ARCHIVE};
cvar_t net_masters[] = 
{
	{"net_master1", ""},
	{"net_master2", ""},
	{"net_master3", ""},
	{"net_master4", ""},
	{"net_masterextra1", "master.frag-net.com:27950"},
	{"net_masterextra2", "dpmaster.deathmask.net:27950"},
	{"net_masterextra3", "master.quakeone.com:27950"},
	{NULL}
};
cvar_t rcon_password = {"rcon_password", ""};
extern cvar_t net_messagetimeout;
extern cvar_t net_connecttimeout;
extern cvar_t net_connectattempts; // woods #connectretry

static struct
{
	unsigned int	length;
	unsigned int	sequence;
	byte	data[MAX_DATAGRAM];
} packetBuffer;

static int myDriverLevel;
static qboolean islistening;

extern qboolean m_return_onerror;
extern char m_return_reason[32];

static double heartbeat_time;	//when this is reached, send a heartbeat to all masters.
static struct heartbeatctx_s {	//thread context used to avoid stalls on dns lookups.
	qboolean working;	//don't really need a barrier, we'll use join to sync before reading the rest.
	void *thread;

	int nummasters;
	struct
	{
		int okay;
		char *name;
	} master[countof(net_masters)];

	size_t numresults;
	struct
	{
		int ldrv;
		char *name;
		struct qsockaddr addr;
	} result[countof(net_masters)*MAX_NET_DRIVERS];
} *heartbeatctx;

typedef struct portpingprobe_ctx_s
{
	SDL_Thread *thread;
	int num_probes;
	int landriver;
	char connect_addr[NET_NAMELEN];
	struct qsockaddr target_addr;
	byte serverinfo_packet[4 + 1 + sizeof("QUAKE") + 1];
	int best_port;
	double best_rtt;
} portpingprobe_ctx_t;

static const byte portpingprobe_getinfo_packet[] = {0xFF, 0xFF, 0xFF, 0xFF, 'g', 'e', 't', 'i', 'n', 'f', 'o', '\n'};

static portpingprobe_ctx_t *portpingprobe_ctx = NULL;
static SDL_atomic_t portpingprobe_status = {PORTPINGPROBE_IDLE};
static SDL_atomic_t portpingprobe_abort_requested = {0};
static SDL_atomic_t portpingprobe_worker_running = {0};
static SDL_atomic_t portpingprobe_progress = {0};
static int net_probe_clientport = 0;

static struct
{
	qboolean valid;
	qboolean has_target;
	char connect_addr[NET_NAMELEN];
	int landriver;
	struct qsockaddr target_addr;
	int best_port;
	double best_rtt;
} portpingprobe_result = {0};
static int portpingprobe_last_percent = -1;
static qboolean portpingprobe_console_inline = false;

static void cl_portpingprobe_enable_completion(cvar_t *var, const char *partial);
static void cl_portpingprobe_probes_completion(cvar_t *var, const char *partial);
static void cl_portpingprobe_delay_completion(cvar_t *var, const char *partial);
static void cl_portpingprobe_enable_changed(cvar_t *var);
static void cl_portpingprobe_probes_changed(cvar_t *var);
static void cl_portpingprobe_delay_changed(cvar_t *var);
static double NET_PortPingProbeSingle(const portpingprobe_ctx_t *ctx, int source_port);
static int NET_PortPingProbeWorker(void *data);
static void NET_PortPingProbe_ClearResult(void);
static void NET_PortPingProbe_Shutdown(void);


static char *StrAddr (struct qsockaddr *addr)
{
	static char buf[34];
	byte *p = (byte *)addr;
	int n;

	for (n = 0; n < 16; n++)
		sprintf (buf + n * 2, "%02x", *p++);
	return buf;
}

static const char *Datagram_SocketOwnerString(const qsocket_t *sock) // woods #droplog
{
	int i;

	if (sock && svs.clients && svs.maxclients)
	{
		for (i = 0; i < svs.maxclients; i++)
		{
			client_t *cl = &svs.clients[i];

			if (cl->netconnection == sock)
				return cl->name[0] ? cl->name : NET_QSocketGetTrueAddressString(sock);
		}
	}

	return sock ? NET_QSocketGetTrueAddressString(sock) : "unknown";
}

static void cl_portpingprobe_enable_completion(cvar_t *var, const char *partial)
{
	Con_AddToTabList("0", partial, "disabled", NULL);
	Con_AddToTabList("1", partial, "enabled", NULL);
}

static void cl_portpingprobe_probes_completion(cvar_t *var, const char *partial)
{
	Con_AddToTabList("50", partial, "fast test", NULL);
	Con_AddToTabList("100", partial, "light probe", NULL);
	Con_AddToTabList("200", partial, "balanced", NULL);
	Con_AddToTabList("500", partial, "default", NULL);
	Con_AddToTabList("1000", partial, "max", NULL);
}

static void cl_portpingprobe_delay_completion(cvar_t *var, const char *partial)
{
	Con_AddToTabList("0", partial, "no delay", NULL);
	Con_AddToTabList("1", partial, "1 ms", NULL);
	Con_AddToTabList("5", partial, "5 ms", NULL);
	Con_AddToTabList("10", partial, "10 ms", NULL);
	Con_AddToTabList("25", partial, "25 ms", NULL);
	Con_AddToTabList("50", partial, "50 ms", NULL);
	Con_AddToTabList("100", partial, "100 ms", NULL);
}

static void cl_portpingprobe_probes_changed(cvar_t *var)
{
	const int clamped = CLAMP(1, (int)var->value, 1000);

	if ((int)var->value == clamped)
		return;

	Con_Printf("cl_portpingprobe_probes must be between 1 and 1000\n");
	Cvar_SetValueQuick(var, (float)clamped);
}

static void cl_portpingprobe_delay_changed(cvar_t *var)
{
	if (var->value >= 0)
		return;

	Con_Printf("cl_portpingprobe_delay must be >= 0\n");
	Cvar_SetValueQuick(var, 0);
}

static void cl_portpingprobe_enable_changed(cvar_t *var)
{
	portpingprobe_status_t status;

	if (var->value != 0)
		return;

	NET_PortPingProbe_RequestAbort();
	status = NET_PortPingProbe_GetStatus();
	if (status == PORTPINGPROBE_COMPLETED || status == PORTPINGPROBE_IDLE)
	{
		NET_PortPingProbe_ClearResult();
		net_probe_clientport = 0;
		portpingprobe_last_percent = -1;
		portpingprobe_console_inline = false;
		SDL_AtomicSet(&portpingprobe_progress, 0);
		SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_IDLE);
	}
}

qboolean NET_PortPingProbe_IsEnabled(void)
{
	return cl_portpingprobe_enable.value != 0;
}

portpingprobe_status_t NET_PortPingProbe_GetStatus(void)
{
	return (portpingprobe_status_t)SDL_AtomicGet(&portpingprobe_status);
}

int NET_PortPingProbe_GetProgress(void)
{
	int progress_count;

	if (NET_PortPingProbe_GetStatus() == PORTPINGPROBE_COMPLETED)
		return 100;
	if (NET_PortPingProbe_GetStatus() != PORTPINGPROBE_PROBING || !portpingprobe_ctx || portpingprobe_ctx->num_probes <= 0)
		return 0;

	progress_count = SDL_AtomicGet(&portpingprobe_progress);
	return CLAMP(0, (progress_count * 100) / portpingprobe_ctx->num_probes, 100);
}

void NET_PortPingProbe_RequestAbort(void)
{
	portpingprobe_status_t status = NET_PortPingProbe_GetStatus();

	if (status == PORTPINGPROBE_IDLE || status == PORTPINGPROBE_COMPLETED)
		return;

	SDL_AtomicSet(&portpingprobe_abort_requested, 1);
	SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_ABORT);
}

static void NET_PortPingProbe_ClearResult(void)
{
	portpingprobe_result.valid = false;
	portpingprobe_result.has_target = false;
	portpingprobe_result.connect_addr[0] = '\0';
	portpingprobe_result.landriver = -1;
	memset(&portpingprobe_result.target_addr, 0, sizeof(portpingprobe_result.target_addr));
	portpingprobe_result.best_port = 0;
	portpingprobe_result.best_rtt = 0;
}

qboolean NET_PortPingProbe_ConsumeCompleted(const char *connect_addr)
{
	struct qsockaddr resolved_addr;
	qboolean equivalent_target = false;
	int i;

	if (NET_PortPingProbe_GetStatus() != PORTPINGPROBE_COMPLETED)
		return false;

	if (!connect_addr || !connect_addr[0] || !portpingprobe_result.valid)
	{
		NET_PortPingProbe_ClearResult();
		SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_IDLE);
		return false;
	}

	if (q_strcasecmp(connect_addr, portpingprobe_result.connect_addr))
	{
		if (portpingprobe_result.has_target)
		{
			if (portpingprobe_result.landriver >= 0 &&
				portpingprobe_result.landriver < net_numlandrivers &&
				net_landrivers[portpingprobe_result.landriver].initialized &&
				net_landrivers[portpingprobe_result.landriver].GetAddrFromName(connect_addr, &resolved_addr) != -1 &&
				net_landrivers[portpingprobe_result.landriver].AddrCompare(&resolved_addr, &portpingprobe_result.target_addr) != -1)
			{
				equivalent_target = true;
			}
			else
			{
				for (i = 0; i < net_numlandrivers; i++)
				{
					if (!net_landrivers[i].initialized)
						continue;
					if (net_landrivers[i].GetAddrFromName(connect_addr, &resolved_addr) == -1)
						continue;
					if (net_landrivers[i].AddrCompare(&resolved_addr, &portpingprobe_result.target_addr) != -1)
					{
						equivalent_target = true;
						break;
					}
				}
			}
		}

		if (!equivalent_target)
		{
			NET_PortPingProbe_ClearResult();
			SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_IDLE);
			return false;
		}
	}

	net_probe_clientport = portpingprobe_result.best_port > 0 ? portpingprobe_result.best_port : 0;
	NET_PortPingProbe_ClearResult();
	SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_IDLE);
	return true;
}

static double NET_PortPingProbeSingle(const portpingprobe_ctx_t *ctx, int source_port)
{
	net_landriver_t *ldrv;
	struct qsockaddr recvaddr;
	byte recvbuf[2048];
	double start_time;
	double elapsed;
	sys_socket_t sock;
	qboolean fallback_sent = false;
	int ret;

	if (!ctx)
		return -1;

	ldrv = &net_landrivers[ctx->landriver];
	sock = ldrv->Open_Socket(source_port);
	if (sock == INVALID_SOCKET)
		return -1;

	if (ldrv->Write(sock, (byte *)portpingprobe_getinfo_packet, sizeof(portpingprobe_getinfo_packet), (struct qsockaddr *)&ctx->target_addr) == -1)
	{
		ldrv->Close_Socket(sock);
		return -1;
	}

	start_time = Sys_DoubleTime();

	while ((elapsed = (Sys_DoubleTime() - start_time)) < 1.0)
	{
		if (SDL_AtomicGet(&portpingprobe_abort_requested))
			break;

		// Some active servers ignore connectionless getinfo but answer
		// CCREQ_SERVER_INFO. Try that as a fallback after a short delay.
		if (!fallback_sent && elapsed >= 0.25)
		{
			ldrv->Write(sock, (byte *)ctx->serverinfo_packet, sizeof(ctx->serverinfo_packet), (struct qsockaddr *)&ctx->target_addr);
			fallback_sent = true;
		}

		ret = ldrv->Read(sock, recvbuf, sizeof(recvbuf), &recvaddr);
		if (ret > 0)
		{
			// Accept replies from the same host even if source port differs.
			if (ldrv->AddrCompare(&recvaddr, (struct qsockaddr *)&ctx->target_addr) != -1)
			{
				ldrv->Close_Socket(sock);
				return Sys_DoubleTime() - start_time;
			}
		}
		else if (ret < 0)
			break;

		Sys_Sleep(1);
	}

	ldrv->Close_Socket(sock);
	return -1;
}

static int NET_PortPingProbeWorker(void *data)
{
	portpingprobe_ctx_t *ctx = data;
	unsigned int random_state;
	int i;

	if (!ctx)
		return 0;

	random_state = (unsigned int)(Sys_DoubleTime() * 1000000.0) ^ (unsigned int)(uintptr_t)SDL_ThreadID();

	for (i = 0; i < ctx->num_probes; i++)
	{
		double rtt;
		int source_port;

		if (SDL_AtomicGet(&portpingprobe_abort_requested))
			break;

		random_state = random_state * 1664525u + 1013904223u;
		source_port = 1024 + (int)(random_state % 64512u); // [1024..65535]
		rtt = NET_PortPingProbeSingle(ctx, source_port);

		if (rtt >= 0 && (ctx->best_rtt < 0 || rtt < ctx->best_rtt))
		{
			ctx->best_port = source_port;
			ctx->best_rtt = rtt;
		}

		SDL_AtomicSet(&portpingprobe_progress, i + 1);

		if (SDL_AtomicGet(&portpingprobe_abort_requested))
			break;

		if (cl_portpingprobe_delay.value > 0)
			Sys_Sleep((unsigned long)cl_portpingprobe_delay.value);
	}

	if (SDL_AtomicGet(&portpingprobe_abort_requested))
		SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_ABORT);
	else
		SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_COMPLETED);

	SDL_AtomicSet(&portpingprobe_worker_running, 0);
	return 0;
}

qboolean NET_PortPingProbe_Start(const char *connect_addr)
{
	portpingprobe_ctx_t *ctx;
	struct qsockaddr resolved_addr;
	int landriver = -1;
	int num_probes;
	int control;
	int i;

	if (!connect_addr || !connect_addr[0] || !NET_PortPingProbe_IsEnabled())
		return false;

	if (NET_PortPingProbe_GetStatus() != PORTPINGPROBE_IDLE)
		return false;

	for (i = 0; i < net_numlandrivers; i++)
	{
		if (!net_landrivers[i].initialized)
			continue;

		if (net_landrivers[i].GetAddrFromName(connect_addr, &resolved_addr) != -1)
		{
			landriver = i;
			break;
		}
	}

	if (landriver < 0)
	{
		Con_SafePrintf("Could not resolve %s\n", connect_addr);
		return false;
	}

	num_probes = CLAMP(1, (int)cl_portpingprobe_probes.value, 1000);
	ctx = Z_Malloc(sizeof(*ctx));
	ctx->num_probes = num_probes;
	ctx->landriver = landriver;
	ctx->target_addr = resolved_addr;
	control = BigLong(NETFLAG_CTL | ((int)sizeof(ctx->serverinfo_packet) & NETFLAG_LENGTH_MASK));
	memcpy(ctx->serverinfo_packet, &control, sizeof(control));
	ctx->serverinfo_packet[4] = CCREQ_SERVER_INFO;
	memcpy(ctx->serverinfo_packet + 5, "QUAKE", sizeof("QUAKE"));
	ctx->serverinfo_packet[5 + sizeof("QUAKE")] = NET_PROTOCOL_VERSION;
	ctx->best_port = 0;
	ctx->best_rtt = -1;
	q_strlcpy(ctx->connect_addr, connect_addr, sizeof(ctx->connect_addr));
	ctx->thread = NULL;

	NET_PortPingProbe_ClearResult();
	SDL_AtomicSet(&portpingprobe_abort_requested, 0);
	SDL_AtomicSet(&portpingprobe_progress, 0);
	SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_PROBING);
	SDL_AtomicSet(&portpingprobe_worker_running, 1);
	portpingprobe_last_percent = -1;
	portpingprobe_console_inline = false;

	ctx->thread = SDL_CreateThread(NET_PortPingProbeWorker, "portpingprobe", ctx);
	if (!ctx->thread)
	{
		Con_Printf("NET_PortPingProbe_Start: failed to create worker thread\n");
		SDL_AtomicSet(&portpingprobe_worker_running, 0);
		SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_IDLE);
		Z_Free(ctx);
		return false;
	}

	portpingprobe_ctx = ctx;
	Con_Printf("Probing %s to find best source port (%d probes)\n", connect_addr, num_probes);
	return true;
}

void NET_PortPingProbe_Frame(void)
{
	portpingprobe_status_t status;
	portpingprobe_ctx_t *ctx = portpingprobe_ctx;
	int progress_percent;

	if (!ctx)
		return;

	if (SDL_AtomicGet(&portpingprobe_worker_running))
	{
		progress_percent = NET_PortPingProbe_GetProgress();
		if (progress_percent > 0 && progress_percent != portpingprobe_last_percent)
		{
			portpingprobe_last_percent = progress_percent;
			portpingprobe_console_inline = true;
			Con_SafePrintf("Port probe progress: %d%%\r", progress_percent);
		}
		return;
	}

	if (ctx->thread)
	{
		SDL_WaitThread(ctx->thread, NULL);
		ctx->thread = NULL;
	}

	// Take ownership here so teardown paths won't race this cleanup.
	portpingprobe_ctx = NULL;

	status = NET_PortPingProbe_GetStatus();
	if (portpingprobe_console_inline)
	{
		Con_SafePrintf("\n");
		portpingprobe_console_inline = false;
	}

	if (status == PORTPINGPROBE_COMPLETED)
	{
		portpingprobe_result.valid = true;
		portpingprobe_result.has_target = true;
		q_strlcpy(portpingprobe_result.connect_addr, ctx->connect_addr, sizeof(portpingprobe_result.connect_addr));
		portpingprobe_result.landriver = ctx->landriver;
		portpingprobe_result.target_addr = ctx->target_addr;
		portpingprobe_result.best_port = ctx->best_port;
		portpingprobe_result.best_rtt = ctx->best_rtt;

		if (ctx->best_port > 0)
			Con_Printf("Port probe completed: best source port %d (%.2f ms)\n", ctx->best_port, ctx->best_rtt * 1000.0);
		else
			Con_Printf("Port probe completed: no responsive source port found, falling back to OS-assigned source port\n");

		Cbuf_AddText(va("connect \"%s\"\n", ctx->connect_addr));
	}
	else
	{
		if (status == PORTPINGPROBE_ABORT)
			Con_Printf("Port ping probe aborted\n");

		NET_PortPingProbe_ClearResult();
		SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_IDLE);
	}

	Z_Free(ctx);
	SDL_AtomicSet(&portpingprobe_abort_requested, 0);
	SDL_AtomicSet(&portpingprobe_progress, 0);
	portpingprobe_last_percent = -1;
	portpingprobe_console_inline = false;
}

static void NET_PortPingProbe_Shutdown(void)
{
	portpingprobe_ctx_t *ctx = portpingprobe_ctx;

	// Called during teardown, after normal frame pumping has stopped.
	NET_PortPingProbe_RequestAbort();
	portpingprobe_ctx = NULL;

	if (ctx)
	{
		if (ctx->thread)
		{
			SDL_WaitThread(ctx->thread, NULL);
			ctx->thread = NULL;
		}

		Z_Free(ctx);
	}

	NET_PortPingProbe_ClearResult();
	net_probe_clientport = 0;
	SDL_AtomicSet(&portpingprobe_abort_requested, 0);
	SDL_AtomicSet(&portpingprobe_worker_running, 0);
	SDL_AtomicSet(&portpingprobe_progress, 0);
	SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_IDLE);
	portpingprobe_last_percent = -1;
	portpingprobe_console_inline = false;
}

#ifdef BAN_TEST

static struct in_addr	banAddr;
static struct in_addr	banMask;

static void NET_Ban_f (void)
{
	char	addrStr [32];
	char	maskStr [32];
	void	(*print_fn)(const char *fmt, ...) FUNCP_PRINTF(1,2);

	if (cmd_source != src_client)
	{
		if (!sv.active)
		{
			Cmd_ForwardToServer ();
			return;
		}
		print_fn = Con_Printf;
	}
	else
	{
		if (pr_global_struct->deathmatch)
			return;
		print_fn = SV_ClientPrintf;
	}

	switch (Cmd_Argc ())
	{
	case 1:
		if (banAddr.s_addr != INADDR_ANY)
		{
			Q_strcpy(addrStr, inet_ntoa(banAddr));
			Q_strcpy(maskStr, inet_ntoa(banMask));
			print_fn("Banning %s [%s]\n", addrStr, maskStr);
		}
		else
			print_fn("Banning not active\n");
		break;

	case 2:
		if (q_strcasecmp(Cmd_Argv(1), "off") == 0)
			banAddr.s_addr = INADDR_ANY;
		else
			banAddr.s_addr = inet_addr(Cmd_Argv(1));
		banMask.s_addr = INADDR_NONE;
		break;

	case 3:
		banAddr.s_addr = inet_addr(Cmd_Argv(1));
		banMask.s_addr = inet_addr(Cmd_Argv(2));
		break;

	default:
		print_fn("BAN ip_address [mask]\n");
		break;
	}
}
#endif	// BAN_TEST


int Datagram_SendMessage (qsocket_t *sock, sizebuf_t *data)
{
	unsigned int	packetLen;
	unsigned int	dataLen;
	unsigned int	eom;

#ifdef DEBUG
	if (data->cursize == 0)
		Sys_Error("Datagram_SendMessage: zero length message");

	if (data->cursize > NET_MAXMESSAGE)
		Sys_Error("Datagram_SendMessage: message too big: %u", data->cursize);

	if (sock->canSend == false)
		Sys_Error("SendMessage: called with canSend == false");
#endif

	Q_memcpy(sock->sendMessage, data->data, data->cursize);
	sock->sendMessageLength = data->cursize;

	sock->max_datagram = sock->pending_max_datagram;	//this can apply only at the start of a reliable, to avoid issues with acks if its resized later.

	if (data->cursize <= sock->max_datagram)
	{
		dataLen = data->cursize;
		eom = NETFLAG_EOM;
	}
	else
	{
		dataLen = sock->max_datagram;
		eom = 0;
	}
	packetLen = NET_HEADERSIZE + dataLen;

	packetBuffer.length = BigLong(packetLen | (NETFLAG_DATA | eom));
	packetBuffer.sequence = BigLong(sock->sendSequence++);
	Q_memcpy (packetBuffer.data, sock->sendMessage, dataLen);

	sock->canSend = false;

	if (sfunc.Write (sock->socket, (byte *)&packetBuffer, packetLen, &sock->addr) == -1)
		return -1;

	sock->lastSendTime = net_time;
	packetsSent++;
	return 1;
}


static int SendMessageNext (qsocket_t *sock)
{
	unsigned int	packetLen;
	unsigned int	dataLen;
	unsigned int	eom;

	if (sock->sendMessageLength <= sock->max_datagram)
	{
		dataLen = sock->sendMessageLength;
		eom = NETFLAG_EOM;
	}
	else
	{
		dataLen = sock->max_datagram;
		eom = 0;
	}
	packetLen = NET_HEADERSIZE + dataLen;

	packetBuffer.length = BigLong(packetLen | (NETFLAG_DATA | eom));
	packetBuffer.sequence = BigLong(sock->sendSequence++);
	Q_memcpy (packetBuffer.data, sock->sendMessage, dataLen);

	sock->sendNext = false;

	if (sfunc.Write (sock->socket, (byte *)&packetBuffer, packetLen, &sock->addr) == -1)
		return -1;

	sock->lastSendTime = net_time;
	packetsSent++;
	return 1;
}


static int ReSendMessage (qsocket_t *sock)
{
	unsigned int	packetLen;
	unsigned int	dataLen;
	unsigned int	eom;

	if (sock->sendMessageLength <= sock->max_datagram)
	{
		dataLen = sock->sendMessageLength;
		eom = NETFLAG_EOM;
	}
	else
	{
		dataLen = sock->max_datagram;
		eom = 0;
	}
	packetLen = NET_HEADERSIZE + dataLen;

	packetBuffer.length = BigLong(packetLen | (NETFLAG_DATA | eom));
	packetBuffer.sequence = BigLong(sock->sendSequence - 1);
	Q_memcpy (packetBuffer.data, sock->sendMessage, dataLen);

	sock->sendNext = false;

	if (sfunc.Write (sock->socket, (byte *)&packetBuffer, packetLen, &sock->addr) == -1)
		return -1;

	sock->lastSendTime = net_time;
	packetsReSent++;
	return 1;
}


qboolean Datagram_CanSendMessage (qsocket_t *sock)
{
	if (sock->sendNext)
		SendMessageNext (sock);

	return sock->canSend;
}


qboolean Datagram_CanSendUnreliableMessage (qsocket_t *sock)
{
	return true;
}


int Datagram_SendUnreliableMessage (qsocket_t *sock, sizebuf_t *data)
{
	int	packetLen;

#ifdef DEBUG
	if (data->cursize == 0)
		Sys_Error("Datagram_SendUnreliableMessage: zero length message");

	if (data->cursize > MAX_DATAGRAM)
		Sys_Error("Datagram_SendUnreliableMessage: message too big: %u", data->cursize);
#endif

	packetLen = NET_HEADERSIZE + data->cursize;

	packetBuffer.length = BigLong(packetLen | NETFLAG_UNRELIABLE);
	packetBuffer.sequence = BigLong(sock->unreliableSendSequence++);
	Q_memcpy (packetBuffer.data, data->data, data->cursize);

	if (sfunc.Write (sock->socket, (byte *)&packetBuffer, packetLen, &sock->addr) == -1)
		return -1;

	packetsSent++;
	return 1;
}

static void _Datagram_ServerControlPacket (sys_socket_t acceptsock, struct qsockaddr *clientaddr, byte *data, unsigned int length);
qboolean Datagram_ProcessPacket(unsigned int length, qsocket_t *sock)
{
	unsigned int	flags;
	unsigned int	sequence;
	unsigned int	count;

	if (length < NET_HEADERSIZE)
	{
		shortPacketCount++;
		return false;
	}

	length = BigLong(packetBuffer.length);
	flags = length & (~NETFLAG_LENGTH_MASK);
	length &= NETFLAG_LENGTH_MASK;

	if (flags & NETFLAG_CTL)
		return false;	//should only be for OOB packets.

	sequence = BigLong(packetBuffer.sequence);
	packetsReceived++;

	if (flags & NETFLAG_UNRELIABLE)
	{
		if (sequence < sock->unreliableReceiveSequence)
		{
			Con_DPrintf("Got a stale datagram\n");
			return false;
		}
		if (sequence != sock->unreliableReceiveSequence)
		{
			count = sequence - sock->unreliableReceiveSequence;
			droppedDatagrams += count;
			Con_DPrintf("Dropped %u datagram(s) for %s\n", count, Datagram_SocketOwnerString(sock)); // woods #droplog
		}
		sock->unreliableReceiveSequence = sequence + 1;

		length -= NET_HEADERSIZE;

		if (length > (unsigned int)net_message.maxsize)
		{	//is this even possible? maybe it will be in the future! either way, no sys_errors please.
			Con_Printf("Over-sized unreliable\n");
			return -1;
		}
		SZ_Clear (&net_message);
		SZ_Write (&net_message, packetBuffer.data, length);

		unreliableMessagesReceived++;
		return true;	//parse the unreliable
	}

	if (flags & NETFLAG_ACK)
	{
		if (sequence != (sock->sendSequence - 1))
		{
			Con_DPrintf("Stale ACK received\n");
			return false;
		}
		if (sequence == sock->ackSequence)
		{
			sock->ackSequence++;
			if (sock->ackSequence != sock->sendSequence)
				Con_DPrintf("ack sequencing error\n");
		}
		else
		{
			Con_DPrintf("Duplicate ACK received\n");
			return false;
		}
		sock->sendMessageLength -= sock->max_datagram;
		if (sock->sendMessageLength > 0)
		{
			memmove (sock->sendMessage, sock->sendMessage + sock->max_datagram, sock->sendMessageLength);
			sock->sendNext = true;
		}
		else
		{
			sock->sendMessageLength = 0;
			sock->canSend = true;
		}
		return false;
	}

	if (flags & NETFLAG_DATA)
	{
		packetBuffer.length = BigLong(NET_HEADERSIZE | NETFLAG_ACK);
		packetBuffer.sequence = BigLong(sequence);
		sfunc.Write (sock->socket, (byte *)&packetBuffer, NET_HEADERSIZE, &sock->addr);

		if (sequence != sock->receiveSequence)
		{
			receivedDuplicateCount++;
			return false;
		}
		sock->receiveSequence++;

		length -= NET_HEADERSIZE;

		if (flags & NETFLAG_EOM)
		{
			if (sock->receiveMessageLength + length > (unsigned int)net_message.maxsize)
			{
				Con_Printf("Over-sized reliable\n");
				return -1;
			}
			SZ_Clear(&net_message);
			SZ_Write(&net_message, sock->receiveMessage, sock->receiveMessageLength);
			SZ_Write(&net_message, packetBuffer.data, length);
			sock->receiveMessageLength = 0;

			messagesReceived++;
			return true;	//parse this reliable!
		}

		if (sock->receiveMessageLength + length > sizeof(sock->receiveMessage))
		{
			Con_Printf("Over-sized reliable\n");
			return -1;
		}
		Q_memcpy(sock->receiveMessage + sock->receiveMessageLength, packetBuffer.data, length);
		sock->receiveMessageLength += length;
		return false;	//still watiting for the eom
	}
	//unknown flags
	Con_DPrintf("Unknown packet flags\n");
	return false;
}

void Datagram_GetAnyMessages(void(*callback)(qsocket_t *))
{
	qsocket_t *s;
	struct qsockaddr addr;
	int length;
	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		sys_socket_t sock;
		if (!dfunc.initialized)
			continue;
		sock = dfunc.listeningSock;
		if (sock == INVALID_SOCKET)
			continue;

		while(1)
		{
			length = dfunc.Read(sock, (byte *)&packetBuffer, NET_DATAGRAMSIZE, &addr);
			if (length == -1 || !length)
			{
				//no more packets, move on to the next.
				break;
			}

			if (length < 4)
				continue;

			if (BigLong(packetBuffer.length) & NETFLAG_CTL)
			{
				_Datagram_ServerControlPacket(sock, &addr, (byte *)&packetBuffer, length);

				//rcon can mess some stuff up...
				sock = dfunc.listeningSock;
				if (sock == INVALID_SOCKET)
					break;
				continue;
			}

			//figure out which qsocket it was for
			for (s = net_activeSockets; s; s = s->next)
			{
				if (s->driver != net_driverlevel)
					continue;
				if (s->disconnected)
					continue;
				if (!s->isvirtual)
					continue;
				if (dfunc.AddrCompare(&addr, &s->addr) == 0)
				{
					//okay, looks like this is us. try to process it, and if there's new data
					if (Datagram_ProcessPacket(length, s))
					{
						s->lastMessageTime = net_time;
						callback(s);	//the server needs to parse that packet.
						break;
					}
				}
			}
			if (!s)
			{	//unmatched packet — try ICE (STUN/DTLS/SCTP)
				byte leadbyte = ((byte *)&packetBuffer)[0];
				if (leadbyte < 4 || (leadbyte >= 20 && leadbyte < 64))
					NQICE_ProcessPacket((byte *)&packetBuffer, length, &addr, callback);
			}
		}
	}
	for (s = net_activeSockets; s; s = s->next)
	{
		if (s->driver != net_driverlevel)
			continue;
		if (!s->isvirtual)
			continue;

		if (!s->canSend)
			if ((net_time - s->lastSendTime) > 1.0)
				ReSendMessage (s);
		if (s->sendNext)
			SendMessageNext (s);

		if (net_time - s->lastMessageTime > ((!s->ackSequence)?net_connecttimeout.value:net_messagetimeout.value))
		{	//timed out, kick them
			//FIXME: add a proper challenge rather than assuming spoofers won't fake acks
			int i;
			for (i = 0; i < svs.maxclients; i++)
			{
				if (svs.clients[i].netconnection == s)
				{
					host_client = &svs.clients[i];
					SV_DropClient(false);
					break;
				}
			}
		}
	}
}

int	Datagram_GetMessage (qsocket_t *sock)
{
	unsigned int	length;
	unsigned int	flags;
	int				ret = 0;
	struct qsockaddr readaddr;
	unsigned int	sequence;
	unsigned int	count;

	if (!sock->canSend)
		if ((net_time - sock->lastSendTime) > 1.0)
			ReSendMessage (sock);

	while (1)
	{
		length = (unsigned int) sfunc.Read(sock->socket, (byte *)&packetBuffer,
							NET_DATAGRAMSIZE, &readaddr);

	//	if ((rand() & 255) > 220)
	//		continue;

		if (length == 0)
			break;

		if (length == (unsigned int)-1)
		{
			Con_Printf("Read error\n");
			return -1;
		}

		if (sfunc.AddrCompare(&readaddr, &sock->addr) != 0)
		{
 /* woods  (R00k)
			Con_DPrintf("Stray/Forged packet received\n");
			Con_DPrintf("Expected: %s\n", sfunc.AddrToString(&sock->addr, false));
			Con_DPrintf("Received: %s\n", sfunc.AddrToString(&readaddr, false));
#endif*/
			continue;
		}

		if (length < NET_HEADERSIZE)
		{
			shortPacketCount++;
			continue;
		}

		length = BigLong(packetBuffer.length);
		if (length == 0xffffffff)
			continue;	//some kind of lingering QW or DP response?
		flags = length & (~NETFLAG_LENGTH_MASK);
		length &= NETFLAG_LENGTH_MASK;

		if (flags & NETFLAG_CTL)
		{
			SZ_Clear (&net_message);
			SZ_Write (&net_message, (byte*)&packetBuffer + 4, length-4);
			MSG_BeginReading();
			switch(MSG_ReadByte())
			{
			case CCREP_RCON:
				Con_Printf("%s\n", MSG_ReadString());
				break;
			}
			continue;
		}

		sequence = BigLong(packetBuffer.sequence);
		packetsReceived++;

		if (flags & NETFLAG_UNRELIABLE)
		{
			if (sequence < sock->unreliableReceiveSequence)
			{
				Con_DPrintf("Got a stale datagram\n");
				ret = 0;
				break;
			}
			if (sequence != sock->unreliableReceiveSequence)
			{
				count = sequence - sock->unreliableReceiveSequence;
				droppedDatagrams += count;
				Con_DPrintf("Dropped %u datagram(s) for %s\n", count, Datagram_SocketOwnerString(sock)); // woods #droplog
				cl.packetloss = count; // woods #scrpl
				cl.pltotal = droppedDatagrams; // woods #scrpl
			}
			sock->unreliableReceiveSequence = sequence + 1;

			length -= NET_HEADERSIZE;

			SZ_Clear (&net_message);
			SZ_Write (&net_message, packetBuffer.data, length);

			ret = 2;
			break;
		}

		if (flags & NETFLAG_ACK)
		{
			if (sequence != (sock->sendSequence - 1))
			{
				Con_DPrintf("Stale ACK received\n");
				continue;
			}
			if (sequence == sock->ackSequence)
			{
				sock->ackSequence++;
				if (sock->ackSequence != sock->sendSequence)
					Con_DPrintf("ack sequencing error\n");
			}
			else
			{
				Con_DPrintf("Duplicate ACK received\n");
				continue;
			}
			sock->sendMessageLength -= sock->max_datagram;
			if (sock->sendMessageLength > 0)
			{
				memmove (sock->sendMessage, sock->sendMessage + sock->max_datagram, sock->sendMessageLength);
				sock->sendNext = true;
			}
			else
			{
				sock->sendMessageLength = 0;
				sock->canSend = true;
			}
			continue;
		}

		if (flags & NETFLAG_DATA)
		{
			packetBuffer.length = BigLong(NET_HEADERSIZE | NETFLAG_ACK);
			packetBuffer.sequence = BigLong(sequence);
			sfunc.Write (sock->socket, (byte *)&packetBuffer, NET_HEADERSIZE, &readaddr);

			if (sequence != sock->receiveSequence)
			{
				receivedDuplicateCount++;
				continue;
			}
			sock->receiveSequence++;

			length -= NET_HEADERSIZE;

			if (flags & NETFLAG_EOM)
			{
				if (sock->receiveMessageLength + length > (unsigned int)net_message.maxsize)
				{
					Con_Printf("Over-sized reliable\n");
					return -1;
				}
				SZ_Clear(&net_message);
				SZ_Write(&net_message, sock->receiveMessage, sock->receiveMessageLength);
				SZ_Write(&net_message, packetBuffer.data, length);
				sock->receiveMessageLength = 0;

				ret = 1;
				break;
			}

			if (sock->receiveMessageLength + length > sizeof(sock->receiveMessage))
			{
				Con_Printf("Over-sized reliable\n");
				return -1;
			}
			Q_memcpy(sock->receiveMessage + sock->receiveMessageLength, packetBuffer.data, length);
			sock->receiveMessageLength += length;
			continue;
		}
	}

	if (sock->sendNext)
		SendMessageNext (sock);

	return ret;
}


static void PrintStats(qsocket_t *s)
{
	Con_Printf("canSend = %4u   \n", s->canSend);
	Con_Printf("sendSeq = %4u   ", s->sendSequence);
	Con_Printf("recvSeq = %4u   \n", s->receiveSequence);
	Con_Printf("\n");
}

static void NET_Stats_f (void)
{
	qsocket_t	*s;

	if (Cmd_Argc () == 1)
	{
		Con_Printf("unreliable messages sent   = %i\n", unreliableMessagesSent);
		Con_Printf("unreliable messages recv   = %i\n", unreliableMessagesReceived);
		Con_Printf("reliable messages sent     = %i\n", messagesSent);
		Con_Printf("reliable messages received = %i\n", messagesReceived);
		Con_Printf("packetsSent                = %i\n", packetsSent);
		Con_Printf("packetsReSent              = %i\n", packetsReSent);
		Con_Printf("packetsReceived            = %i\n", packetsReceived);
		Con_Printf("receivedDuplicateCount     = %i\n", receivedDuplicateCount);
		Con_Printf("shortPacketCount           = %i\n", shortPacketCount);
		Con_Printf("droppedDatagrams           = %i\n", droppedDatagrams);
	}
	else if (Q_strcmp(Cmd_Argv(1), "*") == 0)
	{
		for (s = net_activeSockets; s; s = s->next)
			PrintStats(s);
		for (s = net_freeSockets; s; s = s->next)
			PrintStats(s);
	}
	else
	{
		for (s = net_activeSockets; s; s = s->next)
		{
			if (q_strcasecmp(Cmd_Argv(1), s->trueaddress) == 0 || q_strcasecmp(Cmd_Argv(1), s->maskedaddress) == 0)
				break;
		}

		if (s == NULL)
		{
			for (s = net_freeSockets; s; s = s->next)
			{
				if (q_strcasecmp(Cmd_Argv(1), s->trueaddress) == 0 || q_strcasecmp(Cmd_Argv(1), s->maskedaddress) == 0)
					break;
			}
		}

		if (s == NULL)
			return;

		PrintStats(s);
	}
}

// recognize ip:port (based on ProQuake)
static const char *Strip_Port (const char *host)
{
	static char	noport[MAX_QPATH];
			/* array size as in Host_Connect_f() */
	char		*p;
	int		port;

	if (!host || !*host)
		return host;
	q_strlcpy (noport, host, sizeof(noport));
	if ((p = Q_strrchr(noport, ':')) == NULL)
		return host;
	if (strchr(p, ']'))
		return host;	//[::] should not be considered port 0
	*p++ = '\0';
	port = Q_atoi (p);
	if (port > 0 && port < 65536 && port != net_hostport)
	{
		net_hostport = port;
		Con_SafePrintf("Port set to %d\n", net_hostport);
	}
	return noport;
}


static qboolean testInProgress = false;
static int		testPollCount;
static int		testDriver;
static sys_socket_t	testSocket;

static void Test_Poll (void *);
static PollProcedure	testPollProcedure = {NULL, 0.0, Test_Poll};

static void Test_Poll (void *unused)
{
	struct qsockaddr clientaddr;
	int		control;
	int		len;
	char	name[32];
	char	address[64];
	int		colors;
	int		frags;
	int		connectTime;

	net_landriverlevel = testDriver;

	while (1)
	{
		len = dfunc.Read (testSocket, net_message.data, net_message.maxsize, &clientaddr);
		if (len < (int) sizeof(int))
			break;

		net_message.cursize = len;

		MSG_BeginReading ();
		control = BigLong(*((int *)net_message.data));
		MSG_ReadLong();
		if (control == -1)
			break;
		if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
			break;
		if ((control & NETFLAG_LENGTH_MASK) != len)
			break;

		if (MSG_ReadByte() != CCREP_PLAYER_INFO)
			Sys_Error("Unexpected response to Player Info request\n");

		MSG_ReadByte(); /* playerNumber */
		Q_strcpy(name, MSG_ReadString());
		colors = MSG_ReadLong();
		frags = MSG_ReadLong();
		connectTime = MSG_ReadLong();
		Q_strcpy(address, MSG_ReadString());

		Con_Printf("%s\n  frags:%3i  colors:%d %d  time:%d\n  %s\n", name, frags, colors >> 4, colors & 0x0f, connectTime / 60, address);
	}

	testPollCount--;
	if (testPollCount)
	{
		SchedulePollProcedure(&testPollProcedure, 0.1);
	}
	else
	{
		dfunc.Close_Socket(testSocket);
		testInProgress = false;
	}
}

static void Test_f (void)
{
	const char	*host;
	size_t		n;
	size_t		maxusers = MAX_SCOREBOARD;
	struct qsockaddr sendaddr;

	if (testInProgress)
		return;

	host = Strip_Port (Cmd_Argv(1));

	if (host && hostCacheCount)
	{
		for (n = 0; n < hostCacheCount; n++)
		{
			if (q_strcasecmp (host, hostcache[n].name) == 0)
			{
				if (hostcache[n].driver != myDriverLevel)
					continue;
				net_landriverlevel = hostcache[n].ldriver;
				maxusers = hostcache[n].maxusers;
				Q_memcpy(&sendaddr, &hostcache[n].addr, sizeof(struct qsockaddr));
				break;
			}
		}

		if (n < hostCacheCount)
			goto JustDoIt;
	}

	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (!net_landrivers[net_landriverlevel].initialized)
			continue;

		// see if we can resolve the host name
		if (dfunc.GetAddrFromName(host, &sendaddr) != -1)
			break;
	}

	if (net_landriverlevel == net_numlandrivers)
	{
		Con_Printf("Could not resolve %s\n", host);
		return;
	}

JustDoIt:
	testSocket = dfunc.Open_Socket(0);
	if (testSocket == INVALID_SOCKET)
		return;

	testInProgress = true;
	testPollCount = 20;
	testDriver = net_landriverlevel;

	for (n = 0; n < maxusers; n++)
	{
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREQ_PLAYER_INFO);
		MSG_WriteByte(&net_message, n);
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (testSocket, net_message.data, net_message.cursize, &sendaddr);
	}
	SZ_Clear(&net_message);
	SchedulePollProcedure(&testPollProcedure, 0.1);
}


static qboolean test2InProgress = false;
static int		test2Driver;
static sys_socket_t	test2Socket;

static void Test2_Poll (void *);
static PollProcedure	test2PollProcedure = {NULL, 0.0, Test2_Poll};

static void Test2_Poll (void *unused)
{
	struct qsockaddr clientaddr;
	int		control;
	int		len;
	char	name[256];
	char	value[256];

	net_landriverlevel = test2Driver;
	name[0] = 0;

	len = dfunc.Read (test2Socket, net_message.data, net_message.maxsize, &clientaddr);
	if (len < (int) sizeof(int))
		goto Reschedule;

	net_message.cursize = len;

	MSG_BeginReading ();
	control = BigLong(*((int *)net_message.data));
	MSG_ReadLong();
	if (control == -1)
		goto Error;
	if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
		goto Error;
	if ((control & NETFLAG_LENGTH_MASK) != len)
		goto Error;

	if (MSG_ReadByte() != CCREP_RULE_INFO)
		goto Error;

	Q_strcpy(name, MSG_ReadString());
	if (name[0] == 0)
		goto Done;
	Q_strcpy(value, MSG_ReadString());

	Con_Printf("%-16.16s  %-16.16s\n", name, value);

	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREQ_RULE_INFO);
	MSG_WriteString(&net_message, name);
	*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
	dfunc.Write (test2Socket, net_message.data, net_message.cursize, &clientaddr);
	SZ_Clear(&net_message);

Reschedule:
	SchedulePollProcedure(&test2PollProcedure, 0.05);
	return;

Error:
	Con_Printf("Unexpected response to Rule Info request\n");
Done:
	dfunc.Close_Socket(test2Socket);
	test2InProgress = false;
	return;
}

static void Test2_f (void)
{
	const char	*host;
	size_t		n;
	struct qsockaddr sendaddr;

	if (test2InProgress)
		return;

	host = Strip_Port (Cmd_Argv(1));

	if (host && hostCacheCount)
	{
		for (n = 0; n < hostCacheCount; n++)
		{
			if (q_strcasecmp (host, hostcache[n].name) == 0)
			{
				if (hostcache[n].driver != myDriverLevel)
					continue;
				net_landriverlevel = hostcache[n].ldriver;
				Q_memcpy(&sendaddr, &hostcache[n].addr, sizeof(struct qsockaddr));
				break;
			}
		}

		if (n < hostCacheCount)
			goto JustDoIt;
	}

	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (!net_landrivers[net_landriverlevel].initialized)
			continue;

		// see if we can resolve the host name
		if (dfunc.GetAddrFromName(host, &sendaddr) != -1)
			break;
	}

	if (net_landriverlevel == net_numlandrivers)
	{
		Con_Printf("Could not resolve %s\n", host);
		return;
	}

JustDoIt:
	test2Socket = dfunc.Open_Socket(0);
	if (test2Socket == INVALID_SOCKET)
		return;

	test2InProgress = true;
	test2Driver = net_landriverlevel;

	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREQ_RULE_INFO);
	MSG_WriteString(&net_message, "");
	*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
	dfunc.Write (test2Socket, net_message.data, net_message.cursize, &sendaddr);
	SZ_Clear(&net_message);
	SchedulePollProcedure(&test2PollProcedure, 0.05);
}

void NET_Rcon_f(void)
{
	qsocket_t *sock = cls.netcon;

	if (!sock || !net_drivers[sock->driver].initialized || net_drivers[sock->driver].SendUnreliableMessage != Datagram_SendUnreliableMessage)
		return;	//not dgram. probably loopback. just use the proper command or something.

	SZ_Clear(&net_message);
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREQ_RCON);
	MSG_WriteString(&net_message, rcon_password.string);
	MSG_WriteString(&net_message, Cmd_Args());

	*(int*)net_message.data = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));

	if (sfunc.Write (sock->socket, net_message.data, net_message.cursize, &sock->addr) == -1)
		return;
}

int Datagram_Init (void)
{
	int	i, num_inited;
	sys_socket_t	csock;

#ifdef BAN_TEST
	banAddr.s_addr = INADDR_ANY;
	banMask.s_addr = INADDR_NONE;
#endif
	myDriverLevel = net_driverlevel;

	Cmd_AddCommand ("net_stats", NET_Stats_f);

	Cvar_RegisterVariable(&cl_portpingprobe_enable);
	Cvar_RegisterVariable(&cl_portpingprobe_probes);
	Cvar_RegisterVariable(&cl_portpingprobe_delay);
	Cvar_SetCompletion(&cl_portpingprobe_enable, cl_portpingprobe_enable_completion);
	Cvar_SetCompletion(&cl_portpingprobe_probes, cl_portpingprobe_probes_completion);
	Cvar_SetCompletion(&cl_portpingprobe_delay, cl_portpingprobe_delay_completion);
	Cvar_SetCallback(&cl_portpingprobe_enable, cl_portpingprobe_enable_changed);
	Cvar_SetCallback(&cl_portpingprobe_probes, cl_portpingprobe_probes_changed);
	Cvar_SetCallback(&cl_portpingprobe_delay, cl_portpingprobe_delay_changed);
	SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_IDLE);
	SDL_AtomicSet(&portpingprobe_abort_requested, 0);
	SDL_AtomicSet(&portpingprobe_worker_running, 0);
	SDL_AtomicSet(&portpingprobe_progress, 0);
	NET_PortPingProbe_ClearResult();
	net_probe_clientport = 0;

	if (safemode || COM_CheckParm("-nolan"))
		return -1;

	num_inited = 0;
	for (i = 0; i < net_numlandrivers; i++)
	{
		csock = net_landrivers[i].Init ();
		if (csock == INVALID_SOCKET)
			continue;
		net_landrivers[i].initialized = true;
		net_landrivers[i].controlSock = csock;
		net_landrivers[i].listeningSock = INVALID_SOCKET;
		num_inited++;
	}

	if (num_inited == 0)
		return -1;

#ifdef BAN_TEST
	Cmd_AddCommand_ClientCommand ("ban", NET_Ban_f);
#endif
	Cmd_AddCommand ("test", Test_f);
	Cmd_AddCommand ("test2", Test2_f);
	Cmd_AddCommand ("rcon", NET_Rcon_f);

	return 0;
}


void Datagram_Shutdown (void)
{
	int i;

	NET_DatagramConnectCancel();
	NET_PortPingProbe_Shutdown();
	Datagram_Listen(false);

//
// shutdown the lan drivers
//
	for (i = 0; i < net_numlandrivers; i++)
	{
		if (net_landrivers[i].initialized)
		{
			net_landrivers[i].Shutdown ();
			net_landrivers[i].initialized = false;
		}
	}
}


void Datagram_Close (qsocket_t *sock)
{
	if (sock->isvirtual)
	{
		sock->isvirtual = false;
		sock->socket = INVALID_SOCKET;
	}
	else
		sfunc.Close_Socket(sock->socket);
}


void Datagram_Listen (qboolean state)
{
	qsocket_t *s;
	int i;
	islistening = false;

	heartbeat_time = 0;	//reset it
	if (heartbeatctx)
	{	//clean up, might block oh well.
		SDL_WaitThread(heartbeatctx->thread, NULL);
		Z_Free(heartbeatctx);
		heartbeatctx = NULL;
	}

	NQICE_UnshareGameSockets();	//invalidate before sockets change

	for (i = 0; i < net_numlandrivers; i++)
	{
		if (net_landrivers[i].initialized)
		{
			net_landrivers[i].listeningSock = net_landrivers[i].Listen (state);
			if (net_landrivers[i].listeningSock != INVALID_SOCKET)
			{
				islistening = true;
				NQICE_ShareGameSocket(net_landrivers[i].listeningSock);
			}

			for (s = net_activeSockets; s; s = s->next)
			{
				if (s->isvirtual)
				{
					s->isvirtual = false;
					s->socket = INVALID_SOCKET;
				}
			}
		}
	}
	if (state && !islistening)
	{
		if (isDedicated)
			Sys_Error("Unable to open any listening sockets\n");
		Con_Warning("Unable to open any listening sockets\n");
	}
}

static struct qsockaddr rcon_response_address;
static sys_socket_t rcon_response_socket;
static sys_socket_t rcon_response_landriver;
void Datagram_Rcon_Flush(const char *text)
{
	sizebuf_t msg;
	byte buffer[8192];
	msg.data = buffer;
	msg.maxsize = sizeof(buffer);
	msg.allowoverflow = true;
	SZ_Clear(&msg);
	// save space for the header, filled in later
	MSG_WriteLong(&msg, 0);
	MSG_WriteByte(&msg, CCREP_RCON);
	MSG_WriteString(&msg, text);
	if (msg.overflowed)
		return;
	*((int *)msg.data) = BigLong(NETFLAG_CTL | (msg.cursize & NETFLAG_LENGTH_MASK));
	net_landrivers[rcon_response_landriver].Write (rcon_response_socket, msg.data, msg.cursize, &rcon_response_address);
}
void Datagram_GenerateGetInfoString(char *out, size_t outsize)
{
	const char *gamedir = COM_GetGameNames(false);
	size_t ofs = 0;
	int i;
	unsigned int numclients = 0, numbots = 0;

	for (i = 0; i < svs.maxclients; i++)
	{
		if (svs.clients[i].active)
		{
			numclients++;
			if (!svs.clients[i].netconnection)
				numbots++;
		}
	}

	*out = 0;

	COM_Parse(com_protocolname.string);
	if (*com_token)	//the master server needs this. This tells the master which game we should be listed as.
		{q_snprintf(out+ofs, outsize-ofs, "\\gamename\\%s", com_token); ofs += strlen(out+ofs);}

	q_snprintf(out+ofs, outsize-ofs, "\\protocol\\3n"); ofs += strlen(out+ofs);
		//w: quakeworld
		//n: netquake
		//d: darkplaces
		//x: remaster
		//r: qwfwd proxy ('prx' infokey)
		//t: qtv

	q_snprintf(out+ofs, outsize-ofs, "\\ver\\"ENGINE_NAME_AND_VER); ofs += strlen(out+ofs);
	q_snprintf(out+ofs, outsize-ofs, "\\nqprotocol\\%u", sv.protocol); ofs += strlen(out+ofs);	//silly nqness

	if (*gamedir)
		{q_snprintf(out+ofs, outsize-ofs, "\\modname\\%s", gamedir); ofs += strlen(out+ofs);}
	if (*sv.name)
		{q_snprintf(out+ofs, outsize-ofs, "\\mapname\\%s", sv.name); ofs += strlen(out+ofs);}
	if (*deathmatch.string)
		{q_snprintf(out+ofs, outsize-ofs, "\\deathmatch\\%s", deathmatch.string); ofs += strlen(out+ofs);}
	if (*teamplay.string)
		{q_snprintf(out+ofs, outsize-ofs, "\\teamplay\\%s", teamplay.string); ofs += strlen(out+ofs);}
	if (*hostname.string)
		{q_snprintf(out+ofs, outsize-ofs, "\\hostname\\%s", hostname.string); ofs += strlen(out+ofs);}
	q_snprintf(out+ofs, outsize-ofs, "\\clients\\%u", numclients); ofs += strlen(out+ofs);
	if (numbots)
		{q_snprintf(out+ofs, outsize-ofs, "\\bots\\%u", numbots); ofs += strlen(out+ofs);}
	q_snprintf(out+ofs, outsize-ofs, "\\sv_maxclients\\%i", svs.maxclients); ofs += strlen(out+ofs);
	if (*NQICE_GetWsAddr())
		{q_snprintf(out+ofs, outsize-ofs, "\\*wsaddr\\%s", NQICE_GetWsAddr()); ofs += strlen(out+ofs);}
	if (*NQICE_GetFingerprint())
		{q_snprintf(out+ofs, outsize-ofs, "\\*fp\\%s", NQICE_GetFingerprint()); ofs += strlen(out+ofs);}
}

//send context for ICE UDP signaling callback — set before calling SVC_ICE_Offer/Candidate
static sys_socket_t _ice_send_sock;
static struct qsockaddr *_ice_send_addr;
static void _Datagram_ICE_SendPacket(const void *data, int len)
{
	if (BrokerDTLS_IsAuthenticated())
	{	//response goes back encrypted through the DTLS session
		BrokerDTLS_Send(data, len);
		return;
	}
	dfunc.Write(_ice_send_sock, (byte *)data, len, _ice_send_addr);
}

//called by BrokerDTLS to process decrypted connectionless packets
void _Datagram_BrokerPacket(byte *data, unsigned int length, sys_socket_t sock, struct qsockaddr *addr)
{
	_Datagram_ServerControlPacket(sock, addr, data, length);
}

static void _Datagram_ServerControlPacket (sys_socket_t acceptsock, struct qsockaddr *clientaddr, byte *data, unsigned int length)
{
	struct qsockaddr newaddr;
	qsocket_t	*sock;
	qsocket_t	*s;
	int			command;
	int			control;
	int			ret;
	int plnum;
	int mod, /*mod_ver, mod_flags,*/ mod_passwd;	//proquake extensions

	control = BigLong(*((int *)data));
	if (control == -1)
	{
		if (!sv_public.value)
			return;
		data[length] = 0;
		Cmd_TokenizeString((char*)data+4);
		if (!strcmp(Cmd_Argv(0), "getinfo") || !strcmp(Cmd_Argv(0), "getstatus"))
		{	//master, as well as other clients, may send us one of these two packets to get our serverinfo data
			//masters only really need gamename and player counts. actual clients might want player names too.
			qboolean full = !strcmp(Cmd_Argv(0), "getstatus");
			char cookie[128];
			const char *s = Cmd_Args();
			int i;
			size_t j;
			if (!s) s = "";
			q_strlcpy(cookie, s, sizeof(cookie));

			SZ_Clear(&net_message);
			MSG_WriteLong(&net_message, -1);
			MSG_WriteString(&net_message, full?"statusResponse\n":"infoResponse\n");net_message.cursize--;

			//kinda evil, but oh well, just write it directly.
			Datagram_GenerateGetInfoString((char*)net_message.data+net_message.cursize, net_message.maxsize - net_message.cursize);
			net_message.cursize += strlen((char*)net_message.data+net_message.cursize);

			if (*cookie)
				{MSG_WriteString(&net_message, va("\\challenge\\%s", cookie));net_message.cursize--;}

			if (full)
			{
				for (i = 0; i < svs.maxclients; i++)
				{
					if (svs.clients[i].active)
					{
						float total = 0;
						for (j = 0; j < NUM_PING_TIMES; j++)
							total+=svs.clients[i].ping_times[j];
						total /= NUM_PING_TIMES;
						total *= 1000;	//put it in ms

						MSG_WriteString(&net_message, va("\n%i %i %i_%i \"%s\"",
							svs.clients[i].old_frags, (int)total, svs.clients[i].colors&15, svs.clients[i].colors>>4, svs.clients[i].name
						));net_message.cursize--;
					}
				}
			}

			dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
			SZ_Clear(&net_message);
		}
		else if (!strcmp(Cmd_Argv(0), "ice_offer") || !strcmp(Cmd_Argv(0), "ice_ccand"))
		{	//broker-to-server ICE signaling for /udp/IP:Port browser connections
			//broker format: "command <args>\n<payload>" — separated by \n, not \0
			//parse manually because Cmd_TokenizeString mangles some token values
			const char *line = (const char *)data+4;
			const char *nl = strchr(line, '\n');
			const char *payload = (nl && nl < (const char *)data + length) ? nl + 1 : "";
			char header[256];
			char *args[8];
			int nargs = 0;
			char *p;

			//copy header line for safe tokenization
			{	size_t hlen = nl ? (size_t)(nl - line) : strlen(line);
				if (hlen >= sizeof(header)) hlen = sizeof(header)-1;
				memcpy(header, line, hlen);
				header[hlen] = 0;
			}

			//split header by spaces
			p = header;
			while (*p && nargs < 8)
			{
				while (*p == ' ') p++;
				if (!*p) break;
				args[nargs++] = p;
				while (*p && *p != ' ') p++;
				if (*p) *p++ = 0;
			}

			if (nargs >= 1)
			{
				//capture send context for the callback
				_ice_send_sock = acceptsock;
				_ice_send_addr = clientaddr;

				if (!strcmp(args[0], "ice_offer") && nargs >= 3)
					SVC_ICE_Offer(args[1], args[2], payload, dfunc.AddrToString(clientaddr, false), _Datagram_ICE_SendPacket);
				else if (!strcmp(args[0], "ice_ccand") && nargs >= 4)
					SVC_ICE_Candidate(args[1], args[2], args[3], payload, _Datagram_ICE_SendPacket);
			}
		}
		return;
	}
	if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
		return;
	if ((control & NETFLAG_LENGTH_MASK) != length)
		return;

	//sigh... FIXME: potentially abusive memcpy
	SZ_Clear(&net_message);
	SZ_Write(&net_message, data, length);

	MSG_BeginReading ();
	MSG_ReadLong();

	command = MSG_ReadByte();
	if (command == CCREQ_SERVER_INFO)
	{
		if (Q_strcmp(MSG_ReadString(), "QUAKE") != 0)
			return;

		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_SERVER_INFO);
		dfunc.GetSocketAddr(acceptsock, &newaddr);
		MSG_WriteString(&net_message, dfunc.AddrToString(&newaddr, false));
		MSG_WriteString(&net_message, hostname.string);
		MSG_WriteString(&net_message, sv.name);
		MSG_WriteByte(&net_message, net_activeconnections);
		MSG_WriteByte(&net_message, svs.maxclients);
		MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
		SZ_Clear(&net_message);
		return;
	}

	if (command == CCREQ_PLAYER_INFO)
	{
		int			playerNumber;
		int			activeNumber;
		int			clientNumber;
		client_t	*client;

		playerNumber = MSG_ReadByte();
		activeNumber = -1;

		for (clientNumber = 0, client = svs.clients; clientNumber < svs.maxclients; clientNumber++, client++)
		{
			if (client->active)
			{
				activeNumber++;
				if (activeNumber == playerNumber)
					break;
			}
		}

		if (clientNumber == svs.maxclients)
			return;

		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_PLAYER_INFO);
		MSG_WriteByte(&net_message, playerNumber);
		MSG_WriteString(&net_message, client->name);
		MSG_WriteLong(&net_message, client->colors);
		MSG_WriteLong(&net_message, (int)client->edict->v.frags);
		if (!client->netconnection)
		{
			MSG_WriteLong(&net_message, 0);
			MSG_WriteString(&net_message, "Bot");
		}
		else
		{
			MSG_WriteLong(&net_message, (int)(net_time - client->netconnection->connecttime));
			MSG_WriteString(&net_message, "private"); // woods (r00k)
		}
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
		SZ_Clear(&net_message);

		return;
	}

	if (command == CCREQ_RULE_INFO)
	{
		const char	*prevCvarName;
		cvar_t			*var;

		// find the search start location
		prevCvarName = MSG_ReadString();
		var = Cvar_FindVarAfter (prevCvarName, CVAR_SERVERINFO);

		// send the response
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_RULE_INFO);
		if (var)
		{
			MSG_WriteString(&net_message, var->name);
			MSG_WriteString(&net_message, var->string);
		}
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
		SZ_Clear(&net_message);

		return;
	}

	if (command == CCREQ_RCON)
	{
		const char *password = MSG_ReadString();	//FIXME: this really needs crypto
		const char *response;

		rcon_response_address = *clientaddr;
		rcon_response_socket = acceptsock;
		rcon_response_landriver = net_landriverlevel;

		if (!*rcon_password.string)
			response = "rcon is not enabled on this server";
		else if (!strcmp(password, rcon_password.string))
		{
			qcvm_t *oldvm = qcvm;
			Con_Redirect(Datagram_Rcon_Flush);
			PR_SwitchQCVM(NULL);
			Cmd_ExecuteString(MSG_ReadString(), src_command);
			PR_SwitchQCVM(oldvm);
			Con_Redirect(NULL);
			net_landriverlevel = rcon_response_landriver;
			if (!sv.active)
				Host_EndGame("Server shut down from rcon.");	//stuff got cleaned up that parent functions care about... like the socket.
			return;
		}
		else if (!strcmp(password, "password"))
			response = "What, you really thought that would work? Seriously?";
		else if (!strcmp(password, "thebackdoor"))
			response = "Oh look! You found the backdoor. Don't let it slam you in the face on your way out.";
		else
			response = "Your password is just WRONG dude.";

		Datagram_Rcon_Flush(response);
		return;
	}

	if (command != CCREQ_CONNECT)
		return;

	if (Q_strcmp(MSG_ReadString(), "QUAKE") != 0)
		return;

	if (MSG_ReadByte() != NET_PROTOCOL_VERSION)
	{
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_REJECT);
		MSG_WriteString(&net_message, "Incompatible version.\n");
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
		SZ_Clear(&net_message);
		return;
	}

	//read proquake extensions
	mod = MSG_ReadByte();
	if (msg_badread) mod = 0;
	/*mod_ver = */MSG_ReadByte();
	/*if (msg_badread) mod_ver = 0;
	mod_flags = */MSG_ReadByte();
	/*if (msg_badread) mod_flags = 0;*/
	mod_passwd = MSG_ReadLong();
	if (msg_badread) mod_passwd = 0;

	if (*password.string && strcmp(password.string, "none"))
	{	//FIXME: if this protocol is ever updated, this needs a nonce (eg based on client's IP+time, but requires round-trips to find that out, and confines of proquake's protocol makes it awkward)
		char *e;
		int pwd = strtol(password.string, &e, 0);
		if (*e)
			pwd = Com_BlockChecksum(password.string, strlen(password.string));
		if (mod_passwd != pwd)
		{
			//FIXME: add a short ban so they can't just keep trying
			//FIXME: CCREP_REJECT really needs to be a helper...
			SZ_Clear(&net_message);
			// save space for the header, filled in later
			MSG_WriteLong(&net_message, 0);
			MSG_WriteByte(&net_message, CCREP_REJECT);
			MSG_WriteString(&net_message, "bad/missing password.\n");
			*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
			dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
			SZ_Clear(&net_message);
			return;
		}
	}
	//else if (mod_passwd) thank you for telling me your password for some other server. I'm sure I'll put it to good use...

#ifdef BAN_TEST
	// check for a ban
	//fixme: no ipv6
	//fixme: only a single address? someone seriously underestimates tor.
	if (((struct sockaddr*)clientaddr)->sa_family == AF_INET)
	{
		in_addr_t	testAddr;
		testAddr = ((struct sockaddr_in *)clientaddr)->sin_addr.s_addr;
		if ((testAddr & banMask.s_addr) == banAddr.s_addr)
		{
			SZ_Clear(&net_message);
			// save space for the header, filled in later
			MSG_WriteLong(&net_message, 0);
			MSG_WriteByte(&net_message, CCREP_REJECT);
			MSG_WriteString(&net_message, "You have been banned.\n");
			*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
			dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
			SZ_Clear(&net_message);
			return;
		}
	}
#endif

	// see if this guy is already connected
	for (s = net_activeSockets; s; s = s->next)
	{
		if (s->driver != net_driverlevel)
			continue;
		if (s->disconnected)
			continue;
		ret = dfunc.AddrCompare(clientaddr, &s->addr);
		if (ret == 0)
		{
			int i;

			// is this a duplicate connection reqeust?
			if (ret == 0 && net_time - s->connecttime < 2.0)
			{
				// yes, so send a duplicate reply
				SZ_Clear(&net_message);
				// save space for the header, filled in later
				MSG_WriteLong(&net_message, 0);
				MSG_WriteByte(&net_message, CCREP_ACCEPT);
				dfunc.GetSocketAddr(s->socket, &newaddr);
				MSG_WriteLong(&net_message, dfunc.GetSocketPort(&newaddr));
				if (s->proquake_angle_hack)
				{
					MSG_WriteByte(&net_message, 1);	//proquake
					MSG_WriteByte(&net_message, 30);//ver 30 should be safe. 34 screws with our single-server-socket stuff.
					MSG_WriteByte(&net_message, PQF_IGNOREPORT);	//flags: 0x80==ignore port
				}
				*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
				dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
				SZ_Clear(&net_message);
				return;
			}
			// it's somebody coming back in from a crash/disconnect
			// so close the old qsocket and let their retry get them back in
//			NET_Close(s);
//			return;

			//FIXME: ideally we would just switch the connection over and restart it with a serverinfo packet.
			//warning: there might be packets in-flight which might mess up unreliable sequences.
			//so we attempt to ignore the request, and let the user restart.
			//FIXME: if this is an issue, it should be possible to reuse the previous connection's outgoing unreliable sequence. reliables should be less of an issue as stray ones will be ignored anyway.
			//FIXME: needs challenges, so that other clients can't determine ip's and spoof a reconnect.
			for (i = 0; i < svs.maxclients; i++)
			{
				if (svs.clients[i].netconnection == s)
				{
					NET_Close(s);	//close early, to avoid svc_disconnects confusing things.
					host_client = &svs.clients[i];
					SV_DropClient(false);
					break;
				}
			}
			return;
		}
	}

	//find a free player slot
	for (plnum=0 ; plnum<svs.maxclients ; plnum++)
		if (!svs.clients[plnum].active)
			break;
	if (plnum < svs.maxclients)
		sock = NET_NewQSocket ();
	else
		sock = NULL;	//can happen due to botclients.

	if (sock == NULL)
	{
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_REJECT);
		MSG_WriteString(&net_message, "Server is full.\n");
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
		SZ_Clear(&net_message);
		return;
	}

	sock->proquake_angle_hack = (mod == 1);

	// everything is allocated, just fill in the details
	sock->isvirtual = true;
	sock->socket = acceptsock;
	sock->landriver = net_landriverlevel;
	sock->addr = *clientaddr;
	Q_strcpy(sock->trueaddress, dfunc.AddrToString(clientaddr, false));
	Q_strcpy(sock->maskedaddress, dfunc.AddrToString(clientaddr, true));

	// send him back the info about the server connection he has been allocated
	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREP_ACCEPT);
	dfunc.GetSocketAddr(sock->socket, &newaddr);
	MSG_WriteLong(&net_message, dfunc.GetSocketPort(&newaddr));
//	MSG_WriteString(&net_message, dfunc.AddrToString(&newaddr));
	if (sock->proquake_angle_hack)
	{
		MSG_WriteByte(&net_message, 1);	//proquake
		MSG_WriteByte(&net_message, 30);//ver 30 should be safe. 34 screws with our single-server-socket stuff.
		MSG_WriteByte(&net_message, PQF_IGNOREPORT);
	}
	*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
	dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
	SZ_Clear(&net_message);

	//spawn the client.
	//FIXME: come up with some challenge mechanism so that we don't go to the expense of spamming serverinfos+modellists+etc until we know that its an actual connection attempt.
	svs.clients[plnum].netconnection = sock;
	SV_ConnectClient (plnum);
}

static int DNSLookupThread(void *vctx)
{
	struct heartbeatctx_s *ctx = vctx;
	size_t d, k;
	for (k = 0; k < ctx->nummasters; k++)
	{
		ctx->master[k].okay = false;
		for (d = 0; d < net_numlandrivers; d++)
		{
			if (net_landrivers[d].initialized && net_landrivers[d].listeningSock != INVALID_SOCKET)
			{
				if (net_landrivers[d].GetAddrFromName(ctx->master[k].name, &ctx->result[ctx->numresults].addr) >= 0)
				{
					ctx->result[ctx->numresults].ldrv = d;
					ctx->result[ctx->numresults].name = ctx->master[k].name;
					ctx->master[k].okay = true;
					ctx->numresults++;
				}
			}
		}
	}

	ctx->working = false;
	return true;
}

qsocket_t *Datagram_CheckNewConnections (void)
{
	struct heartbeatctx_s *ctx = heartbeatctx;
	//only needs to do master stuff now
	if (sv_public.value > 0)
	{
		if (ctx)
		{
			if (!ctx->working)
			{
				static char *str = "\377\377\377\377heartbeat DarkPlaces\n";
				size_t k, d;
				SDL_WaitThread(ctx->thread, NULL);

				if (sv_reportheartbeats.value)
					for (k = 0; k < ctx->nummasters; k++)
						if (!ctx->master[k].okay)
							Con_Warning("Unable to resolve master %s\n", ctx->master[k].name);
				for (k = 0; k < ctx->numresults; k++)
				{
					d = ctx->result[k].ldrv;
					if (sv_reportheartbeats.value)
						Con_Printf("Sending heartbeat to %s (%s)\n", ctx->result[k].name, net_landrivers[d].AddrToString(&ctx->result[k].addr, false));
					net_landrivers[d].Write(net_landrivers[d].listeningSock, (byte*)str, strlen(str), &ctx->result[k].addr);
				}

				Z_Free(ctx); //don't need it no more
				heartbeatctx = ctx = NULL;
			}
		}
		else if (islistening && Sys_DoubleTime() > heartbeat_time)
		{
			//darkplaces here refers to the master server protocol, rather than the game protocol
			//(specifies that the server responds to infoRequest packets from the master)
			size_t k, l = 0;
			heartbeat_time = Sys_DoubleTime() + q_max(30,sv_heartbeat_interval.value);

			for (k = 0; net_masters[k].string; k++)
				l += strlen(net_masters[k].string)+1;
			heartbeatctx = ctx = Z_Malloc(sizeof(*ctx) + l);
			for (k = 0, l = 0; net_masters[k].string; k++)
			{
				if (*net_masters[k].string)
				{
					strcpy(    (ctx->master[ctx->nummasters].name = (char*)(ctx+1)+l), net_masters[k].string);	//copy the names over, just in case there's races
					l += strlen(ctx->master[ctx->nummasters].name)+1;
					ctx->nummasters++;
				}
			}
			ctx->working = true;
			ctx->thread = SDL_CreateThread(DNSLookupThread, "heartbeatdns", ctx);
			if (!ctx->thread)	//bum...
				ctx->working = false;	//just clean it up later.
		}
	}

	return NULL;
}

static void _Datagram_SendServerQuery(struct qsockaddr *addr, qboolean master)
{
	SZ_Clear(&net_message);
	if (master) //assume false if you want only the protocol 15 servers.
	{
		MSG_WriteLong(&net_message, ~0);
		MSG_WriteString(&net_message, "getinfo");
	}
	else
	{
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREQ_SERVER_INFO);
		MSG_WriteString(&net_message, "QUAKE");
		MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
	}
	dfunc.Write(dfunc.controlSock, net_message.data, net_message.cursize, addr);
	SZ_Clear(&net_message);
}
static struct
{
	int driver;
	qboolean requery;
	qboolean master;
	struct qsockaddr addr;
} *hostlist;
size_t hostlist_count;
size_t hostlist_max;
static void _Datagram_AddPossibleHost(struct qsockaddr *addr, qboolean master)
{
	size_t u;
	for (u = 0; u < hostlist_count; u++)
	{
		if (!memcmp(&hostlist[u].addr, addr, sizeof(struct qsockaddr)) && hostlist[u].driver == net_landriverlevel)
		{	//we already know about it. it must have come from some other master. don't respam.
			return;
		}
	}
	if (hostlist_count == hostlist_max)
	{
		hostlist_max = hostlist_count + 16;
		hostlist = Z_Realloc(hostlist, sizeof(*hostlist)*hostlist_max);
	}
	hostlist[hostlist_count].addr = *addr;
	hostlist[hostlist_count].requery = true;
	hostlist[hostlist_count].master = master;
	hostlist[hostlist_count].driver = net_landriverlevel;
	hostlist_count++;
}

void Datagram_AddHostCacheInfo(struct qsockaddr *readaddr, const char *cname, const char *info)
{
	char tmp[1024], *e;
	char hostnamebuf[sizeof(hostcache[0].name)];
	char mapbuf[sizeof(hostcache[0].map)];
	char gamedirbuf[sizeof(hostcache[0].gamedir)];
	int p, connectprotocol;
	int users, maxusers;
	qboolean brokerentry = (readaddr == NULL);
	qboolean supported;
	enum
	{
		PT_NONE			= 0,
		PT_NETQUAKE		= 1<<0,	//accepts standard(+extended) netquake clients, presumably 666+ if its responding to queries like this.
		PT_DARKPLACES	= 1<<1,	//different handshakes, different protocol expectations.
		PT_QUAKEWORLD	= 1<<2,	//accepts standard(+extended) quakeworld clients.
		PT_REMASTER		= 1<<3,	//the rerelease engine ('QuakeEx' aka kex) has its own protocol
		PT_QUAKETV		= 1<<4,	//for watching qtv/mvd streams.
		PT_QWRELAY		= 1<<5,	//quakeworld-like, has a 'prx' userinfo key to say where to relay to.
	} t = 0;
	size_t n, i;

	if (!cname)
		cname = dfunc.AddrToString(readaddr, false);
	else
	{	//hack:
		//fte's brpwser port needed this at one point. now we just consider old versions of ftemaster to be buggy for any server where the broker won't be able to ask for details.
		//if the server is reported as having an *fp key then we expect the broker to be willing to punch a hole, allowing us to get some actual security out of it, as well as potentially getting past dodgy routers on the server in question.
		//FIXME: get eukara to update his ftemaster instance, so we have fewer buggy addresses...
		if (!strncmp(cname, "rtc:///udp/", 11))
			if (!*Info_GetKey(info, "*fp", tmp, sizeof(tmp)))
				cname += 11;
	}

	//match by cname instead of address...
	for (n = 0; n < hostCacheCount; n++)
	{
		if (!strcmp(hostcache[n].cname, cname))
			return;
	}

	// is it already there?
	if (n == hostCacheCount)
	{
		if (n == HOSTCACHESIZE)
			return; //can't add.
	}

	Info_GetKey(info, "hostname", hostnamebuf, sizeof(hostnamebuf));
	Info_GetKey(info, "mapname", mapbuf, sizeof(mapbuf));
	Info_GetKey(info, "modname", gamedirbuf, sizeof(gamedirbuf));

	Info_GetKey(info, "clients", tmp, sizeof(tmp));
	users = atoi(tmp);
	Info_GetKey(info, "sv_maxclients", tmp, sizeof(tmp));
	maxusers = atoi(tmp);
	Info_GetKey(info, "protocol", tmp, sizeof(tmp));
	p = strtol(tmp, &e, 10);
	if (*e)	while(*e)switch(*e++)
	{
	case 'n':	t|=PT_NETQUAKE; break;	//netquake, okay
	case 'd':	t|=PT_DARKPLACES; break;	//darkplaces, we can cope.
	case 'w':	t|=PT_QUAKEWORLD; break;	//quakeworld, no support
	case 'x':	t|=PT_REMASTER; break;	//remaster, requires dtls+twiddles.
	case 't':	t|=PT_QUAKETV; break;	//qtv, has basic qw->nq translation so we should be okay.
	case 'r':	t|=PT_QWRELAY; break;	//qwfwd-like, no support
	}
	else
		t = PT_DARKPLACES; //assume the worst for outdated servers.

	connectprotocol = p;
	if (t & PT_NETQUAKE)
	{
		Info_GetKey(info, "nqprotocol", tmp, sizeof(tmp));
		if (*tmp)
			connectprotocol = atoi(tmp);
	}

	supported = (connectprotocol == NET_PROTOCOL_VERSION && (t&(PT_NETQUAKE|PT_DARKPLACES|PT_QUAKETV)));

	// Broker-fed entries should already contain a complete public listing.
	// Ignore malformed/unsupported summaries instead of filling the browser with junk rows.
	if (brokerentry && (!*mapbuf || maxusers <= 0 || !supported))
		return;

	// is it already there?
	if (n == hostCacheCount)
		hostCacheCount++;	//its new.

	q_strlcpy(hostcache[n].name, hostnamebuf, sizeof(hostcache[n].name));
	if (!*hostcache[n].name)
		q_strlcpy(hostcache[n].name, "UNNAMED", sizeof(hostcache[n].name));
	q_strlcpy(hostcache[n].map, mapbuf, sizeof(hostcache[n].map));
	q_strlcpy(hostcache[n].gamedir, gamedirbuf, sizeof(hostcache[n].gamedir));
	hostcache[n].users = users;
	hostcache[n].maxusers = maxusers;

	if (!supported)
	{	//server is unsupported. give it a star.
		Q_strcpy(hostcache[n].cname, hostcache[n].name);
		Q_strcpy(hostcache[n].name, "*");
		Q_strcat(hostcache[n].name, hostcache[n].cname);
	}
	if (readaddr)
	{
		Q_memcpy(&hostcache[n].addr, &readaddr, sizeof(struct qsockaddr));
		hostcache[n].ldriver = net_landriverlevel;
	}
	else
	{
		Q_memset(&hostcache[n].addr, 0, sizeof(struct qsockaddr));
		hostcache[n].ldriver = -1;
	}
	hostcache[n].driver = net_driverlevel;
	q_strlcpy(hostcache[n].cname, cname, sizeof(hostcache[n].cname));

	// check for a name conflict
	for (i = 0; i < hostCacheCount; i++)
	{
		if (i == n)
			continue;
		if (q_strcasecmp (hostcache[n].cname, hostcache[i].cname) == 0)
		{	//this is a dupe.
			hostCacheCount--;
			break;
		}
		if (q_strcasecmp (hostcache[n].name, hostcache[i].name) == 0)
		{
			i = Q_strlen(hostcache[n].name);
			if (i < sizeof(hostcache[n].name)-1 && hostcache[n].name[i-1] > '8')
			{
				hostcache[n].name[i] = '0';
				hostcache[n].name[i+1] = 0;
			}
			else
				hostcache[n].name[i-1]++;

			i = -1;
		}
	}
}

void ResetHostlist (void) // woods #resethostlist
{
	memset(hostlist, 0, sizeof(hostlist[0]) * hostlist_max);
	hostlist_count = 0;
}

static qboolean _Datagram_SearchForHosts (qboolean xmit)
{	
	int		ret;
	size_t	n;
	size_t	i;
	struct qsockaddr readaddr;
	struct qsockaddr myaddr;
	int		control;
	qboolean sentsomething = false;
	const char *cname;

	dfunc.GetSocketAddr (dfunc.controlSock, &myaddr);
	if (xmit)
	{
		for (i = 0; i < hostlist_count; i++)
			hostlist[i].requery = true;

		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREQ_SERVER_INFO);
		MSG_WriteString(&net_message, "QUAKE");
		MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));

		if (slistScope != SLIST_INTERNET) // woods
			dfunc.Broadcast(dfunc.controlSock, net_message.data, net_message.cursize);

		SZ_Clear(&net_message);

		if (slistScope == SLIST_INTERNET)
		{
			struct qsockaddr masteraddr;
			char *str;
			size_t m;
			for (m = 0; net_masters[m].string; m++)
			{
				if (!*net_masters[m].string)
					continue;
				if (dfunc.GetAddrFromName(net_masters[m].string, &masteraddr) >= 0)
				{
					const char *prot = com_protocolname.string;
					while (*prot)
					{	//send a request for each protocol
						prot = COM_Parse(prot);
						if (!prot)
							break;
						if (*com_token)
						{
							if (((struct sockaddr*)&masteraddr)->sa_family == AF_INET6)
								str = va("%c%c%c%cgetserversExt %s %u empty full ipv6"/*\x0A\n"*/, 255, 255, 255, 255, com_token, NET_PROTOCOL_VERSION);
							else
								str = va("%c%c%c%cgetservers %s %u empty full"/*\x0A\n"*/, 255, 255, 255, 255, com_token, NET_PROTOCOL_VERSION);
							dfunc.Write(dfunc.controlSock, (byte*)str, strlen(str), &masteraddr);
						}
					}
				}
			}
		}
		sentsomething = true;
	}

	while ((ret = dfunc.Read (dfunc.controlSock, net_message.data, net_message.maxsize, &readaddr)) > 0)
	{
		if (ret < (int) sizeof(int))
			continue;
		net_message.cursize = ret;

		// don't answer our own query
		//Note: this doesn't really work too well if we're multi-homed.
		//we should probably just refuse to respond to serverinfo requests while we're scanning (chances are our server is going to die anyway).
		if (dfunc.AddrCompare(&readaddr, &myaddr) == 0) // woods #localmpfix
			continue;

		// is the cache full?
		if (hostCacheCount == HOSTCACHESIZE)
			continue;

		MSG_BeginReading ();
		control = BigLong(*((int *)net_message.data));
		MSG_ReadLong();
		if (control == -1)
		{
			if (msg_readcount+19 <= net_message.cursize && !strncmp((char*)net_message.data+msg_readcount, "getserversResponse", 18))
			{
				struct qsockaddr addr;
				int i;
				msg_readcount += 18;
				for(;;)
				{
					switch(MSG_ReadByte())
					{
					case '\\':
						memset(&addr, 0, sizeof(addr));
						((struct sockaddr_in*)&addr)->sin_family = AF_INET;
						for (i = 0; i < 4; i++)
							((byte*)&((struct sockaddr_in*)&addr)->sin_addr)[i] = MSG_ReadByte();
						((byte*)&((struct sockaddr_in*)&addr)->sin_port)[0] = MSG_ReadByte();
						((byte*)&((struct sockaddr_in*)&addr)->sin_port)[1] = MSG_ReadByte();
						if (!((struct sockaddr_in*)&addr)->sin_port)
							msg_badread = true;
						break;
					case '/':
						memset(&addr, 0, sizeof(addr));
						((struct sockaddr_in6*)&addr)->sin6_family = AF_INET6;
						for (i = 0; i < 16; i++)
							((byte*)&((struct sockaddr_in6*)&addr)->sin6_addr)[i] = MSG_ReadByte();
						((byte*)&((struct sockaddr_in6*)&addr)->sin6_port)[0] = MSG_ReadByte();
						((byte*)&((struct sockaddr_in6*)&addr)->sin6_port)[1] = MSG_ReadByte();
						if (!((struct sockaddr_in6*)&addr)->sin6_port)
							msg_badread = true;
						break;
					default:
						memset(&addr, 0, sizeof(addr));
						msg_badread = true;
						break;
					}
					if (msg_badread)
						break;
					_Datagram_AddPossibleHost(&addr, true);
					sentsomething = true;
				}
			}
			else if (msg_readcount+13 <= net_message.cursize && !strncmp((char*)net_message.data+msg_readcount, "infoResponse\n", 13))
			{	//response from a dpp7 server (or possibly 15, no idea really)
				const char *info = MSG_ReadString()+13;
				Datagram_AddHostCacheInfo(&readaddr, NULL, info);
			}
			continue;
		}
		if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
			continue;
		if ((control & NETFLAG_LENGTH_MASK) != ret)
			continue;

		if (MSG_ReadByte() != CCREP_SERVER_INFO)
			continue;

		MSG_ReadString();
		//dfunc.GetAddrFromName(MSG_ReadString(), &peeraddr);
		/*if (dfunc.AddrCompare(&readaddr, &peeraddr) != 0)
		{
			char read[NET_NAMELEN];
			char peer[NET_NAMELEN];
			q_strlcpy(read, dfunc.AddrToString(&readaddr), sizeof(read));
			q_strlcpy(peer, dfunc.AddrToString(&peeraddr), sizeof(peer));
			Con_SafePrintf("Server at %s claimed to be at %s\n", read, peer);
		}*/

		cname = dfunc.AddrToString(&readaddr, false);

		// search the cache for this server
		for (n = 0; n < hostCacheCount; n++)
		{
			if (!strcmp(hostcache[n].cname, cname))
				break;
		}

		// is it already there?
		if (n < hostCacheCount)
		{
			if (*hostcache[n].cname)
				continue;
		}
		else
		{
			// add it
			hostCacheCount++;
		}
		q_strlcpy(hostcache[n].name, MSG_ReadString(), sizeof(hostcache[n].name));
		if (!*hostcache[n].name)
			q_strlcpy(hostcache[n].name, "UNNAMED", sizeof(hostcache[n].name));
		q_strlcpy(hostcache[n].map, MSG_ReadString(), sizeof(hostcache[n].map));
		hostcache[n].users = MSG_ReadByte();
		hostcache[n].maxusers = MSG_ReadByte();
		if (MSG_ReadByte() != NET_PROTOCOL_VERSION)
		{
			Q_strcpy(hostcache[n].cname, hostcache[n].name);
			hostcache[n].cname[14] = 0;
			Q_strcpy(hostcache[n].name, "*");
			Q_strcat(hostcache[n].name, hostcache[n].cname);
		}
		Q_memcpy(&hostcache[n].addr, &readaddr, sizeof(struct qsockaddr));
		hostcache[n].driver = net_driverlevel;
		hostcache[n].ldriver = net_landriverlevel;
		q_strlcpy(hostcache[n].cname, cname, sizeof(hostcache[n].cname));

		// check for a name conflict
		for (i = 0; i < hostCacheCount; i++)
		{
			if (i == n)
				continue;
			if (q_strcasecmp (hostcache[n].cname, hostcache[i].cname) == 0)
			{	//this is a dupe.
				hostCacheCount--;
				break;
			}
			if (q_strcasecmp (hostcache[n].name, hostcache[i].name) == 0)
			{
				i = Q_strlen(hostcache[n].name);
				if (i < sizeof(hostcache[n].name)-1 && hostcache[n].name[i-1] > '8')
				{
					hostcache[n].name[i] = '0';
					hostcache[n].name[i+1] = 0;
				}
				else
					hostcache[n].name[i-1]++;

				i = -1;
			}
		}
	}

	if (!xmit)
	{
		n = 4; //should be time-based. meh.
		for (i = 0; i < hostlist_count; i++)
		{
			if (hostlist[i].requery && hostlist[i].driver == net_landriverlevel)
			{
				hostlist[i].requery = false;
				_Datagram_SendServerQuery(&hostlist[i].addr, hostlist[i].master);
				sentsomething = true;
				n--;
				if (!n)
					break;
			}
		}
	}
	return sentsomething;
}

qboolean Datagram_SearchForHosts (qboolean xmit)
{
	qboolean ret = false;
	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (hostCacheCount == HOSTCACHESIZE)
			break;
		if (net_landrivers[net_landriverlevel].initialized)
			ret |= _Datagram_SearchForHosts (xmit);
	}
	return ret;
}

extern char	lastcattempt[NET_NAMELEN]; // woods verbose connection info

static qsocket_t *_Datagram_Connect (struct qsockaddr *serveraddr)
{
	struct qsockaddr readaddr;
	qsocket_t	*sock;
	sys_socket_t		newsock;
	int			ret;
	int			reps;
	double		start_time;
	int			control;
	const char		*reason;
	int port;
	int probe_port_override;
	qboolean allowgetchallenge = true;

	probe_port_override = net_probe_clientport;
	newsock = INVALID_SOCKET;

	if (probe_port_override > 0)
	{
		newsock = dfunc.Open_Socket(probe_port_override);
		if (newsock == INVALID_SOCKET)
			Con_DPrintf("Port ping probe: source port %d unavailable, falling back to OS-assigned source port\n", probe_port_override);
	}

	if (newsock == INVALID_SOCKET)
		newsock = dfunc.Open_Socket(0);

	net_probe_clientport = 0;
	if (newsock == INVALID_SOCKET)
		return NULL;

	sock = NET_NewQSocket ();
	if (sock == NULL)
		goto ErrorReturn2;
	sock->socket = newsock;
	sock->landriver = net_landriverlevel;

	// connect to the host
	if (dfunc.Connect (newsock, serveraddr) == -1)
		goto ErrorReturn;

	sock->proquake_angle_hack = true;

	// send the connection request
	SCR_UpdateScreen ();
	start_time = net_time;

	const int totalAttempts = q_max(1, (int)net_connectattempts.value); // woods, minimum 1 attempt #connectretry

	for (reps = 0; reps < totalAttempts; reps++) // woods
	{
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREQ_CONNECT);
		MSG_WriteString(&net_message, "QUAKE");
		MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
		if (sock->proquake_angle_hack)
		{	/*Spike -- proquake compat. if both engines claim to be using mod==1 then 16bit client->server angles can be used. server->client angles remain 16bit*/
			char *e;
			int pwd;
			if (!*password.string || !strcmp(password.string, "none"))
				pwd = 0;	//no password specified, assume none.
			else
			{
				pwd = strtol(password.string, &e, 0);
				if (*e)	//something trailing = not a numer = hash it and send that.
					pwd = Com_BlockChecksum(password.string, strlen(password.string));
			}

			Con_DWarning("Attempting to use ProQuake angle hack\n");
			MSG_WriteByte(&net_message, 1); /*'mod', 1=proquake*/
			MSG_WriteByte(&net_message, 35); /*'mod' version*/  // woods for proquake version number on login, changed to 5 from 4
			MSG_WriteByte(&net_message, 0); /*flags*/
			MSG_WriteLong(&net_message, pwd); /*password*/

			//FTE adds a 'getchallenge' hint here for a challenge response instead of nq protocols. QW or DP would expect only a getchallenge.
			//by sending a getchallenge for fte servers, we can get the server to use dp-style handshakes, bypassing any serverside need for smurf pretection and thus the downsides of 'sv_listen_nq 1'
			if (allowgetchallenge)
			{
				if (!strchr(com_protocolname.string, '\"'))
					MSG_WriteString(&net_message, va("getchallenge %i \"%s\" qw=0 nq=1", 0, com_protocolname.string));
			}
		}
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));

		if (allowgetchallenge)
		{
			//for dp compat. DP sends these in addition to the above packet.
			//if the (DP) server is running using vanilla protocols, it replies to the above, otherwise to the following, requiring both to be sent.
			//(challenges hinder a DOS issue known as smurfing, in that the client must prove that it owns the IP that it might be spoofing before any serious resources are used)
			//FTE servers will ignore the following ccreq if they received a challenge recently. this allows using the challenge's protocol info instead (but only if the server has sv_listen_dp set).
			//NOTE: QW clients include a \n here, DP does not. we don't support qw protocols, so we don't want to get mistaken for a qw client.
			#define DPGETCHALLENGE "\xff\xff\xff\xffgetchallenge"
			dfunc.Write (newsock, (byte*)DPGETCHALLENGE, strlen(DPGETCHALLENGE), serveraddr);
		}

		//and the regular ccreq_connect.
		dfunc.Write (newsock, net_message.data, net_message.cursize, serveraddr);
		SZ_Clear(&net_message);

		do
		{
			ret = dfunc.Read (newsock, net_message.data, net_message.maxsize, &readaddr);
			// if we got something, validate it
			if (ret > 0)
			{
				// is it from the right place?
				if (dfunc.AddrCompare(&readaddr, serveraddr) != 0)
				{
					Con_SafePrintf("wrong reply address\n");
					Con_SafePrintf("Expected: %s | %s\n", dfunc.AddrToString (serveraddr, false), StrAddr(serveraddr));
					Con_SafePrintf("Received: %s | %s\n", dfunc.AddrToString (&readaddr, false), StrAddr(&readaddr));
					SCR_UpdateScreen ();
					ret = 0;
					continue;
				}

				if (ret < (int) sizeof(int))
				{
					ret = 0;
					continue;
				}

				net_message.cursize = ret;
				MSG_BeginReading ();

				control = BigLong(*((int *)net_message.data));
				MSG_ReadLong();
				if (control == -1)
				{
					char *e;
					const char *s = MSG_ReadString();
					if (!strncmp(s, "challenge ", 10))
					{	//either a q2 or dp server...
						char buf[1024];
						s+=10;

						if (*password.string && !strchr(password.string, '\\') && !strchr(password.string, '\n'))
							s = va("%s\\password\\%s", s, password.string);	//needs a password. note: this is NOT encrypted! plus the cvar may still be set from a different server/config, leaking that to an untrusted one!
						if (*cl_name.string && !strchr(cl_name.string, '\\') && !strchr(cl_name.string, '\n'))
							s = va("%s\\name\\%s", s, cl_name.string);	//give a name, so we don't get called 'unconnected'
						q_snprintf(buf, sizeof(buf), "%c%c%c%cconnect\\protocol\\darkplaces 3\\protocols\\RMQ FITZ DP7 NEHAHRABJP3 QUAKE\\challenge\\%s", 255, 255, 255, 255, s);
						dfunc.Write (newsock, (byte*)buf, strlen(buf), serveraddr);
						ret = 0;
						continue;
					}
					else if (!strncmp(s, "c", 1) && strtol(s+1, &e, 0) && !*e)
					{	//qw server... but we were trying to look like dp cos we don't do qw...
						//stop trying to look like dp so we don't confuse servers that prefer to send qw challenges inseead of nq ones.
						allowgetchallenge = false;
						ret = 0;
						continue;
					}
					else if (!strcmp(s, "accept"))
					{
						Q_memcpy(&sock->addr, serveraddr, sizeof(struct qsockaddr));
						sock->proquake_angle_hack = false;
						port = 0;	//don't force it.
						goto dpserveraccepted;
					}
					else if (!strncmp(s, "reject ", 7))
					{	//single line message, mostly.
						allowgetchallenge = false;
						ret = 0;
						continue;
						/*reason = s+7;
						Con_Printf("%s\n", reason);
						q_strlcpy(m_return_reason, reason, sizeof(m_return_reason));
						goto ErrorReturn;*/
					}
					else if (!strncmp(s, "print\n", 6))
					{	//qw rejections. just in case.
						ret = 0;
						continue;
						/*
						reason = s+6;
						Con_Printf("%s\n", reason);
						q_strlcpy(m_return_reason, reason, sizeof(m_return_reason));
						goto ErrorReturn;*/
					}

					ret = 0;
					continue;
				}
				if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
				{
					ret = 0;
					continue;
				}
				if ((control & NETFLAG_LENGTH_MASK) != ret)
				{
					ret = 0;
					continue;
				}
			}
		}
		while (ret == 0 && (SetNetTime() - start_time) < 2.5);

		if (ret)
			break;

		int attemptsLeft = totalAttempts - reps - 1;
		if (attemptsLeft > 0)
		{
			Con_SafePrintf("still trying... (%d attempt%s left)\n",attemptsLeft, attemptsLeft == 1 ? "" : "s");
			SCR_UpdateScreen ();
		}
		start_time = SetNetTime();
	}

	if (ret == 0)
	{
		if (!allowgetchallenge)
			reason = "QuakeWorld server";
		else
			reason = "No Response";
		Con_Printf("%s\n", reason);
		Q_strcpy(m_return_reason, reason);
		goto ErrorReturn;
	}

	if (ret == -1)
	{
		reason = "Network Error";
		Con_Printf("%s\n", reason);
		Q_strcpy(m_return_reason, reason);
		goto ErrorReturn;
	}

	ret = MSG_ReadByte();
	if (ret == CCREP_REJECT)
	{
		reason = MSG_ReadString();
		Con_Printf("%s\n", reason);
		q_strlcpy(m_return_reason, reason, sizeof(m_return_reason));
		goto ErrorReturn;
	}

	if (ret == CCREP_ACCEPT)
	{
		Q_memcpy(&sock->addr, serveraddr, sizeof(struct qsockaddr));
		port = MSG_ReadLong();
		if (msg_badread)
			port = 0;	//QE omits the port number, for good reason. not that we're likely to see it, but oh well.
	}
	else
	{
		reason = "Bad Response";
		Con_Printf("%s\n", reason);
		Q_strcpy(m_return_reason, reason);
		goto ErrorReturn;
	}

	if (sock->proquake_angle_hack)
	{
		byte mod = (msg_readcount<net_message.cursize)?MSG_ReadByte():0;
		byte ver = (msg_readcount<net_message.cursize)?MSG_ReadByte():0;
		byte flags = (msg_readcount<net_message.cursize)?MSG_ReadByte():0;
		(void)ver;

		if (mod == MOD_PROQUAKE)
		{
			if (flags & PQF_CHEATFREE)
			{
				reason = "Server is incompatible";
				Con_Printf("%s\n", reason);
				Q_strcpy(m_return_reason, reason);
				goto ErrorReturn;
			}
			if (flags & PQF_IGNOREPORT)
				port = 0; //don't switch it, for non-identity port forwarding.
			sock->proquake_angle_hack = true;
		}
		else
			sock->proquake_angle_hack = false;
	}

dpserveraccepted:
	if (port)	//spike --- don't change the remote port if the server doesn't want us to. this allows servers to use port forwarding with less issues, assuming the server uses the same port for all clients.
		dfunc.SetSocketPort (&sock->addr, port);

	dfunc.GetNameFromAddr (serveraddr, sock->trueaddress);
	dfunc.GetNameFromAddr (serveraddr, sock->maskedaddress);

	Con_Printf ("Connection accepted\n");
	sock->lastMessageTime = SetNetTime();

	// switch the connection to the specified address
	if (dfunc.Connect (newsock, &sock->addr) == -1)
	{
		reason = "Connect to Game failed";
		Con_Printf("%s\n", reason);
		Q_strcpy(m_return_reason, reason);
		goto ErrorReturn;
	}

	/*Spike's rant about NATs:
	We sent a packet to the server's control port.
	The server replied from that control port. all is well so far.
	The server is now about(or already did, yay resends) to send us a packet from its data port to our address.
	The nat will (correctly) see a packet from a different remote address:port.
	The local nat has two options here. 1) assume that the wrong port is fine. 2) drop it. Dropping it is far more likely.
	The NQ code will not send any unreliables until we have received the serverinfo. There are no reliables that need to be sent either.
	Normally we won't send ANYTHING until we get that packet.
	Which will never happen because the NAT will never let it through.
	So, if we want to get away without fixing everyone else's server (which is also quite messy),
		the easy way around this dilema is to just send some (small) useless packet to what we believe to be the server's data port.
	A single unreliable clc_nop should do it. There's unlikely to be much packetloss on our local lan (so long as our host buffers outgoing packets on a per-socket basis or something),
		so we don't normally need to resend. We don't really care if the server can even read it properly, but its best to avoid warning prints.
	With that small outgoing packet, our local nat will think we initiated the request.
	HOPEFULLY it'll reuse the same public port+address. Most home routers will, but not all, most hole-punching techniques depend upon such behaviour.
	Note that proquake 3.4+ will actually wait for a packet from the new client, which solves that (but makes the nop mandatory, so needs to be reliable).

	the nop is actually sent inside CL_EstablishConnection where it has cleaner access to the client's pending reliable message.

	Note that we do fix our own server. This means that we can easily run on a home nat. the heartbeats to the master will open up a public port with most routers.
	And if that doesn't work, then its easy enough to port-forward a single known port instead of having to DMZ the entire network.
	I don't really expect that many people will use this, but it'll be nice for the occasional coop game.
	(note that this makes the nop redundant, but that's a different can of worms)
	*/

	m_return_onerror = false;
	return sock;

ErrorReturn:
	NET_FreeQSocket(sock);
ErrorReturn2:
	dfunc.Close_Socket(newsock);
	if (m_return_onerror)
	{
		key_dest = key_menu;
		m_state = m_return_state;
		m_return_onerror = false;

		IN_UpdateGrabs();
	}
	return NULL;
}

typedef enum
{
	DATAGRAM_CONNECT_PHASE_IDLE = 0,
	DATAGRAM_CONNECT_PHASE_RESOLVE,
	DATAGRAM_CONNECT_PHASE_OPEN_SOCKET,
	DATAGRAM_CONNECT_PHASE_SEND_REQUEST,
	DATAGRAM_CONNECT_PHASE_WAIT_RESPONSE
} datagram_connect_phase_t;

typedef struct
{
	qboolean active;
	datagram_connect_phase_t phase;
	char host[NET_NAMELEN];
	int landriver;
	struct qsockaddr serveraddr;
	sys_socket_t newsock;
	qsocket_t *sock;
	int total_attempts;
	int attempt;
	double attempt_start_time;
	char reason[64];
} datagram_connect_ctx_t;

static datagram_connect_ctx_t datagram_connect_ctx = {false, DATAGRAM_CONNECT_PHASE_IDLE, {0}, -1, {0}, INVALID_SOCKET, NULL, 0, 0, 0.0, {0}};

static void Datagram_ConnectAsyncSetReason(const char *reason)
{
	if (!reason || !*reason)
		reason = "connect failed";
	q_strlcpy(datagram_connect_ctx.reason, reason, sizeof(datagram_connect_ctx.reason));
}

static void Datagram_ConnectAsyncReleaseSocket(void)
{
	if (datagram_connect_ctx.sock)
	{
		NET_FreeQSocket(datagram_connect_ctx.sock);
		datagram_connect_ctx.sock = NULL;
	}

	if (datagram_connect_ctx.newsock != INVALID_SOCKET)
	{
		if (datagram_connect_ctx.landriver >= 0 &&
			datagram_connect_ctx.landriver < net_numlandrivers &&
			net_landrivers[datagram_connect_ctx.landriver].Close_Socket)
		{
			net_landrivers[datagram_connect_ctx.landriver].Close_Socket(datagram_connect_ctx.newsock);
		}
		datagram_connect_ctx.newsock = INVALID_SOCKET;
	}
}

static void Datagram_ConnectAsyncFinalizeFailure(void)
{
	if (*datagram_connect_ctx.reason)
		q_strlcpy(m_return_reason, datagram_connect_ctx.reason, sizeof(m_return_reason));
	else
		m_return_reason[0] = 0;

	if (m_return_onerror)
	{
		key_dest = key_menu;
		m_state = m_return_state;
		m_return_onerror = false;
		IN_UpdateGrabs();
	}
}

static void Datagram_ConnectAsyncStepToNextDriver(void)
{
	Datagram_ConnectAsyncReleaseSocket();
	datagram_connect_ctx.landriver++;
	datagram_connect_ctx.phase = DATAGRAM_CONNECT_PHASE_RESOLVE;
	datagram_connect_ctx.attempt = 0;
	datagram_connect_ctx.attempt_start_time = 0;
}

static void Datagram_ConnectAsyncSendRequest(void)
{
	char *e;
	int pwd;

	net_landriverlevel = datagram_connect_ctx.landriver;
	SZ_Clear(&net_message);
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREQ_CONNECT);
	MSG_WriteString(&net_message, "QUAKE");
	MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
	if (datagram_connect_ctx.sock->proquake_angle_hack)
	{
		if (!*password.string || !strcmp(password.string, "none"))
			pwd = 0;
		else
		{
			pwd = strtol(password.string, &e, 0);
			if (*e)
				pwd = Com_BlockChecksum(password.string, strlen(password.string));
		}

		Con_DWarning("Attempting to use ProQuake angle hack\n");
		MSG_WriteByte(&net_message, 1);
		MSG_WriteByte(&net_message, 35);
		MSG_WriteByte(&net_message, 0);
		MSG_WriteLong(&net_message, pwd);
	}

	*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
	dfunc.Write(datagram_connect_ctx.newsock, net_message.data, net_message.cursize, &datagram_connect_ctx.serveraddr);
	SZ_Clear(&net_message);
	dfunc.Write(datagram_connect_ctx.newsock, (byte*)"\xff\xff\xff\xffgetchallenge\n", strlen("\xff\xff\xff\xffgetchallenge\n"), &datagram_connect_ctx.serveraddr);
	datagram_connect_ctx.attempt_start_time = SetNetTime();
}

qboolean NET_DatagramConnectPending(void)
{
	return datagram_connect_ctx.active;
}

void NET_DatagramConnectCancel(void)
{
	if (!datagram_connect_ctx.active)
		return;

	Datagram_ConnectAsyncReleaseSocket();
	datagram_connect_ctx.active = false;
	datagram_connect_ctx.phase = DATAGRAM_CONNECT_PHASE_IDLE;
	datagram_connect_ctx.landriver = -1;
}

qboolean NET_DatagramConnectStart(const char *host)
{
	NET_DatagramConnectCancel();

	if (!host || !*host)
		return false;

	host = Strip_Port(host);

	memset(&datagram_connect_ctx, 0, sizeof(datagram_connect_ctx));
	datagram_connect_ctx.active = true;
	datagram_connect_ctx.phase = DATAGRAM_CONNECT_PHASE_RESOLVE;
	datagram_connect_ctx.landriver = 0;
	datagram_connect_ctx.newsock = INVALID_SOCKET;
	datagram_connect_ctx.total_attempts = q_max(1, (int)net_connectattempts.value);
	q_strlcpy(datagram_connect_ctx.host, host, sizeof(datagram_connect_ctx.host));

	return true;
}

net_connect_result_t NET_DatagramConnectFrame(qsocket_t **outsock, const char **outreason)
{
	struct qsockaddr readaddr;
	int ret;
	int control;
	int port;
	const char *reason = NULL;
	qboolean try_next_driver = false;

	if (outsock)
		*outsock = NULL;
	if (outreason)
		*outreason = NULL;

	if (!datagram_connect_ctx.active)
		return NET_CONNECT_FAILED;

	SetNetTime();

	while (datagram_connect_ctx.active)
	{
		switch (datagram_connect_ctx.phase)
		{
		case DATAGRAM_CONNECT_PHASE_RESOLVE:
			/* v1 note: name resolution is still synchronous and may briefly stall for hostnames. */
			for (; datagram_connect_ctx.landriver < net_numlandrivers; datagram_connect_ctx.landriver++)
			{
				if (!net_landrivers[datagram_connect_ctx.landriver].initialized)
					continue;

				net_landriverlevel = datagram_connect_ctx.landriver;
				if (dfunc.GetAddrFromName(datagram_connect_ctx.host, &datagram_connect_ctx.serveraddr) != -1)
				{
					datagram_connect_ctx.phase = DATAGRAM_CONNECT_PHASE_OPEN_SOCKET;
					break;
				}
			}

			if (datagram_connect_ctx.phase != DATAGRAM_CONNECT_PHASE_OPEN_SOCKET)
			{
				if (!*datagram_connect_ctx.reason)
					Datagram_ConnectAsyncSetReason("Could not resolve");
				Datagram_ConnectAsyncFinalizeFailure();
				datagram_connect_ctx.active = false;
				datagram_connect_ctx.phase = DATAGRAM_CONNECT_PHASE_IDLE;
				if (outreason)
					*outreason = datagram_connect_ctx.reason;
				return NET_CONNECT_FAILED;
			}
			break;

		case DATAGRAM_CONNECT_PHASE_OPEN_SOCKET:
		{
			int probe_port_override;

			net_landriverlevel = datagram_connect_ctx.landriver;
			probe_port_override = net_probe_clientport;
			datagram_connect_ctx.newsock = INVALID_SOCKET;

			if (probe_port_override > 0)
			{
				datagram_connect_ctx.newsock = dfunc.Open_Socket(probe_port_override);
				if (datagram_connect_ctx.newsock == INVALID_SOCKET)
					Con_DPrintf("Port ping probe: source port %d unavailable, falling back to OS-assigned source port\n", probe_port_override);
			}

			if (datagram_connect_ctx.newsock == INVALID_SOCKET)
				datagram_connect_ctx.newsock = dfunc.Open_Socket(0);

			net_probe_clientport = 0;
			if (datagram_connect_ctx.newsock == INVALID_SOCKET)
			{
				Datagram_ConnectAsyncSetReason("Open socket failed");
				Datagram_ConnectAsyncStepToNextDriver();
				break;
			}

			net_driverlevel = myDriverLevel;
			datagram_connect_ctx.sock = NET_NewQSocket();
			if (!datagram_connect_ctx.sock)
			{
				Datagram_ConnectAsyncSetReason("No qsocket available");
				Datagram_ConnectAsyncStepToNextDriver();
				break;
			}

			datagram_connect_ctx.sock->driver = myDriverLevel;
			datagram_connect_ctx.sock->socket = datagram_connect_ctx.newsock;
			datagram_connect_ctx.sock->landriver = datagram_connect_ctx.landriver;
			datagram_connect_ctx.sock->proquake_angle_hack = true;

			if (dfunc.Connect(datagram_connect_ctx.newsock, &datagram_connect_ctx.serveraddr) == -1)
			{
				Datagram_ConnectAsyncSetReason("Connect request failed");
				Datagram_ConnectAsyncStepToNextDriver();
				break;
			}

			datagram_connect_ctx.attempt = 0;
			datagram_connect_ctx.phase = DATAGRAM_CONNECT_PHASE_SEND_REQUEST;
			break;
		}

		case DATAGRAM_CONNECT_PHASE_SEND_REQUEST:
			Datagram_ConnectAsyncSendRequest();
			datagram_connect_ctx.phase = DATAGRAM_CONNECT_PHASE_WAIT_RESPONSE;
			return NET_CONNECT_PENDING;

		case DATAGRAM_CONNECT_PHASE_WAIT_RESPONSE:
			net_landriverlevel = datagram_connect_ctx.landriver;
			while ((ret = dfunc.Read(datagram_connect_ctx.newsock, net_message.data, net_message.maxsize, &readaddr)) > 0)
			{
				if (dfunc.AddrCompare(&readaddr, &datagram_connect_ctx.serveraddr) != 0)
				{
					Con_SafePrintf("wrong reply address\n");
					Con_SafePrintf("Expected: %s | %s\n", dfunc.AddrToString(&datagram_connect_ctx.serveraddr, false), StrAddr(&datagram_connect_ctx.serveraddr));
					Con_SafePrintf("Received: %s | %s\n", dfunc.AddrToString(&readaddr, false), StrAddr(&readaddr));
					continue;
				}

				if (ret < (int)sizeof(int))
					continue;

				net_message.cursize = ret;
				MSG_BeginReading();
				control = BigLong(*((int *)net_message.data));
				MSG_ReadLong();

				if (control == -1)
				{
					const char *s = MSG_ReadString();
					if (!strncmp(s, "challenge ", 10))
					{
						char buf[1024];
						q_snprintf(buf, sizeof(buf), "%c%c%c%cconnect\\protocol\\darkplaces 3\\protocols\\RMQ FITZ DP7 NEHAHRABJP3 QUAKE\\challenge\\%s", 255, 255, 255, 255, s+10);
						dfunc.Write(datagram_connect_ctx.newsock, (byte*)buf, strlen(buf), &datagram_connect_ctx.serveraddr);
						continue;
					}
					if (!strcmp(s, "accept"))
					{
						Q_memcpy(&datagram_connect_ctx.sock->addr, &datagram_connect_ctx.serveraddr, sizeof(struct qsockaddr));
						datagram_connect_ctx.sock->proquake_angle_hack = false;
						port = 0;
						goto datagram_async_accept;
					}
					continue;
				}

				if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
					continue;
				if ((control & NETFLAG_LENGTH_MASK) != ret)
					continue;

				ret = MSG_ReadByte();
				if (ret == CCREP_REJECT)
				{
					reason = MSG_ReadString();
					Datagram_ConnectAsyncSetReason(reason);
					try_next_driver = true;
					break;
				}
				if (ret != CCREP_ACCEPT)
				{
					Datagram_ConnectAsyncSetReason("Bad Response");
					try_next_driver = true;
					break;
				}

				Q_memcpy(&datagram_connect_ctx.sock->addr, &datagram_connect_ctx.serveraddr, sizeof(struct qsockaddr));
				port = MSG_ReadLong();
				if (msg_badread)
					port = 0;

				if (datagram_connect_ctx.sock->proquake_angle_hack)
				{
					byte mod = (msg_readcount < net_message.cursize) ? MSG_ReadByte() : 0;
					byte ver = (msg_readcount < net_message.cursize) ? MSG_ReadByte() : 0;
					byte flags = (msg_readcount < net_message.cursize) ? MSG_ReadByte() : 0;
					(void)ver;

					if (mod == MOD_PROQUAKE)
					{
						if (flags & PQF_CHEATFREE)
						{
							Datagram_ConnectAsyncSetReason("Server is incompatible");
							try_next_driver = true;
							break;
						}
						if (flags & PQF_IGNOREPORT)
							port = 0;
						datagram_connect_ctx.sock->proquake_angle_hack = true;
					}
					else
					{
						datagram_connect_ctx.sock->proquake_angle_hack = false;
					}
				}

datagram_async_accept:
				if (port)
					dfunc.SetSocketPort(&datagram_connect_ctx.sock->addr, port);

				dfunc.GetNameFromAddr(&datagram_connect_ctx.serveraddr, datagram_connect_ctx.sock->trueaddress);
				dfunc.GetNameFromAddr(&datagram_connect_ctx.serveraddr, datagram_connect_ctx.sock->maskedaddress);
				datagram_connect_ctx.sock->lastMessageTime = SetNetTime();

				if (dfunc.Connect(datagram_connect_ctx.newsock, &datagram_connect_ctx.sock->addr) == -1)
				{
					Datagram_ConnectAsyncSetReason("Connect to Game failed");
					try_next_driver = true;
					break;
				}

				Con_Printf("Connection accepted\n");
				m_return_onerror = false;
				datagram_connect_ctx.active = false;
				datagram_connect_ctx.phase = DATAGRAM_CONNECT_PHASE_IDLE;
				datagram_connect_ctx.newsock = INVALID_SOCKET;
				if (outsock)
				{
					*outsock = datagram_connect_ctx.sock;
					datagram_connect_ctx.sock = NULL;
				}
				return NET_CONNECT_COMPLETE;
			}

			if (try_next_driver || ret == -1)
			{
				if (ret == -1 && !try_next_driver)
					Datagram_ConnectAsyncSetReason("Network Error");
				try_next_driver = false;
				Datagram_ConnectAsyncStepToNextDriver();
				break;
			}

			if ((SetNetTime() - datagram_connect_ctx.attempt_start_time) >= 2.5)
			{
				int attempts_left = datagram_connect_ctx.total_attempts - datagram_connect_ctx.attempt - 1;
				if (attempts_left > 0)
				{
					Con_SafePrintf("still trying... (%d attempt%s left)\n", attempts_left, attempts_left == 1 ? "" : "s");
					datagram_connect_ctx.attempt++;
					datagram_connect_ctx.phase = DATAGRAM_CONNECT_PHASE_SEND_REQUEST;
					return NET_CONNECT_PENDING;
				}
				Datagram_ConnectAsyncSetReason("No Response");
				Datagram_ConnectAsyncStepToNextDriver();
				break;
			}

			return NET_CONNECT_PENDING;

		default:
			Datagram_ConnectAsyncSetReason("connect failed");
			Datagram_ConnectAsyncFinalizeFailure();
			datagram_connect_ctx.active = false;
			datagram_connect_ctx.phase = DATAGRAM_CONNECT_PHASE_IDLE;
			if (outreason)
				*outreason = datagram_connect_ctx.reason;
			return NET_CONNECT_FAILED;
		}
	}

	if (!*datagram_connect_ctx.reason)
		Datagram_ConnectAsyncSetReason("connect failed");
	Datagram_ConnectAsyncFinalizeFailure();
	datagram_connect_ctx.active = false;
	datagram_connect_ctx.phase = DATAGRAM_CONNECT_PHASE_IDLE;
	if (outreason)
		*outreason = datagram_connect_ctx.reason;
	return NET_CONNECT_FAILED;
}

qsocket_t *Datagram_Connect (const char *host)
{
	qsocket_t *ret = NULL;
	qboolean resolved = false;
	struct qsockaddr addr;

	NET_DatagramConnectCancel();

	host = Strip_Port (host);
	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (net_landrivers[net_landriverlevel].initialized)
		{
			// see if we can resolve the host name
			// Spike -- moved name resolution to here to avoid extraneous 'could not resolves' when using other address families
			if (dfunc.GetAddrFromName(host, &addr) != -1)
			{
				resolved = true;
				if ((ret = _Datagram_Connect (&addr)) != NULL)
					break;
			}
		}
	}
	if (!resolved)
		Con_SafePrintf("Could not resolve %s\n", host);
	return ret;
}

/*
Spike: added this to list more than one ipv4 address (many people are still multi-homed)
*/
int Datagram_QueryAddresses(qhostaddr_t *addresses, int maxaddresses)
{
	int result = 0;
	int save_landriverlevel = net_landriverlevel;
	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (!net_landrivers[net_landriverlevel].initialized)
			continue;
		if (result == maxaddresses)
			break;
		if (net_landrivers[net_landriverlevel].QueryAddresses)
			result += net_landrivers[net_landriverlevel].QueryAddresses(addresses+result, maxaddresses-result);
	}
	net_landriverlevel = save_landriverlevel;
	return result;
}

extern qboolean Valid_Domain(const char* domain_str);

const char* ResolveHostname (const char* hostname) // woods #serversmenu
{
	
	if (!Valid_Domain(hostname))
	{
		Con_DPrintf("Invalid domain: %s\n", hostname);
		return hostname;
	}
	
	static char resolvedIP[NET_NAMELEN] = { 0 }; // Buffer to store the resolved IP address as a string
	struct qsockaddr sendaddr;
	int resolved = 0;

	// Attempt to resolve the hostname with each initialized network driver
	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++) {
		if (!net_landrivers[net_landriverlevel].initialized)
			continue;

		if (dfunc.GetAddrFromName(hostname, &sendaddr) != -1) 
		{
			resolved = 1; // Mark as resolved
			strncpy(resolvedIP, dfunc.AddrToString(&sendaddr, false), sizeof(resolvedIP) - 1);
			resolvedIP[sizeof(resolvedIP) - 1] = '\0'; // Ensure null-termination
			break; // Stop iterating once resolved
		}
	}

	if (!resolved) 
	{
		Con_DPrintf("Could not resolve %s\n", hostname);
		return hostname;
	}

	return resolvedIP;
}