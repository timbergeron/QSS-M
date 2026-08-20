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
#ifdef _WIN32
#include "wsaerror.h"
#endif
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
static int malformedPacketCount = 0;
static int droppedDatagrams;

typedef enum
{
	MALFORMED_PACKET_LENGTH,
	MALFORMED_PACKET_CONTROL_CAPACITY,
	MALFORMED_PACKET_TERMINATOR_HEADROOM
} malformed_packet_reason_t;

typedef struct
{
	qboolean			valid;
	malformed_packet_reason_t	reason;
	unsigned int			first_value;
	unsigned int			second_value;
} malformed_packet_sample_t;

static malformed_packet_sample_t malformedPacketFirst;
static malformed_packet_sample_t malformedPacketLatest;

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
cvar_t cl_portpingprobe_port_probes = {"cl_portpingprobe_port_probes", "5", CVAR_ARCHIVE};
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

typedef struct
{
	unsigned int	length;
	unsigned int	sequence;
	byte	data[MAX_DATAGRAM];
	byte	terminator_spare;
} packet_buffer_t;

#define PACKET_BUFFER_RECV_SIZE offsetof(packet_buffer_t, terminator_spare)
#define PACKET_BUFFER_PARSE_CAPACITY (PACKET_BUFFER_RECV_SIZE + sizeof(((packet_buffer_t *)0)->terminator_spare))

COMPILE_TIME_ASSERT(packet_buffer_wire_size,
	PACKET_BUFFER_RECV_SIZE == NET_DATAGRAMSIZE);
COMPILE_TIME_ASSERT(packet_buffer_parse_capacity,
	PACKET_BUFFER_PARSE_CAPACITY == NET_DATAGRAMSIZE + 1);
COMPILE_TIME_ASSERT(packet_buffer_has_terminator_spare,
	sizeof(packet_buffer_t) > NET_DATAGRAMSIZE);

static packet_buffer_t packetBuffer;

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

#define PORTPINGPROBE_FINALISTS 8
#define PORTPINGPROBE_MAX_PORT_PROBES 5
#define PORTPINGPROBE_ENDPOINT_PROBES 3
#define PORTPINGPROBE_TIMEOUT 1.0
#define PORTPINGPROBE_SELECT_SLICE 0.05

typedef enum
{
	PORTPINGPROBE_QUERY_NONE = 0,
	PORTPINGPROBE_QUERY_GETINFO,
	PORTPINGPROBE_QUERY_SERVER_INFO
} portpingprobe_query_t;

typedef struct portpingprobe_candidate_s
{
	int port;
	double discovery_rtt;
	double samples[PORTPINGPROBE_MAX_PORT_PROBES];
	int replies;
	double median_rtt;
	double jitter;
	sys_socket_t socket;
} portpingprobe_candidate_t;

typedef struct portpingprobe_endpoint_s
{
	int landriver;
	struct qsockaddr addr;
	portpingprobe_query_t query;
	sys_socket_t socket;
	double samples[PORTPINGPROBE_ENDPOINT_PROBES];
	int replies;
	double median_rtt;
	double jitter;
} portpingprobe_endpoint_t;

typedef struct portpingprobe_ctx_s
{
	SDL_Thread *thread;
	int num_probes;
	int port_probes;
	int total_work;
	unsigned long delay_ms;
	int landriver;
	char connect_addr[NET_NAMELEN];
	struct qsockaddr target_addr;
	byte serverinfo_packet[4 + 1 + sizeof("QUAKE") + 1];
	portpingprobe_query_t query;
	portpingprobe_endpoint_t endpoints[MAX_NET_DRIVERS];
	int endpoint_count;
	portpingprobe_candidate_t *candidates;
	int responsive_ports;
	int finalist_count;
	int best_port;
	double best_rtt;
	double best_jitter;
	int best_replies;
	sys_socket_t best_socket;
} portpingprobe_ctx_t;

static const byte portpingprobe_getinfo_packet[] = {0xFF, 0xFF, 0xFF, 0xFF, 'g', 'e', 't', 'i', 'n', 'f', 'o', '\n'};

static portpingprobe_ctx_t *portpingprobe_ctx = NULL;
static SDL_atomic_t portpingprobe_status = {PORTPINGPROBE_IDLE};
static SDL_atomic_t portpingprobe_abort_requested = {0};
static SDL_atomic_t portpingprobe_worker_running = {0};
static SDL_atomic_t portpingprobe_progress = {0};
static qboolean portpingprobe_abort_quiet = false;
static int net_probe_clientport = 0;
static int net_probe_clientlandriver = -1;
static sys_socket_t net_probe_clientsocket = INVALID_SOCKET;
static qboolean net_probe_clienthas_target = false;
static struct qsockaddr net_probe_clienttarget;

static struct
{
	qboolean valid;
	qboolean has_target;
	char connect_addr[NET_NAMELEN];
	int landriver;
	struct qsockaddr target_addr;
	int best_port;
	double best_rtt;
	double best_jitter;
	int best_replies;
	int port_probes;
	int responsive_ports;
	qboolean has_socket;
	sys_socket_t best_socket;
} portpingprobe_result = {0};
static int portpingprobe_last_percent = -1;
static qboolean portpingprobe_console_inline = false;

static void cl_portpingprobe_enable_completion(cvar_t *var, const char *partial);
static void cl_portpingprobe_probes_completion(cvar_t *var, const char *partial);
static void cl_portpingprobe_port_probes_completion(cvar_t *var, const char *partial);
static void cl_portpingprobe_delay_completion(cvar_t *var, const char *partial);
static void cl_portpingprobe_enable_changed(cvar_t *var);
static void cl_portpingprobe_probes_changed(cvar_t *var);
static void cl_portpingprobe_port_probes_changed(cvar_t *var);
static void cl_portpingprobe_delay_changed(cvar_t *var);
static double NET_PortPingProbeSingle(const portpingprobe_ctx_t *ctx, int source_port, portpingprobe_query_t query);
static int NET_PortPingProbeWorker(void *data);
static void NET_PortPingProbe_ClearResult(void);
static void NET_PortPingProbe_ClearPendingSocket(void);
static void NET_PortPingProbe_FreeContext(portpingprobe_ctx_t *ctx);
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

static void Datagram_LogDroppedDatagrams(qsocket_t *sock, unsigned int count) // woods #droplog
{
	Con_DPrintf("Dropped %u datagram(s) for %s (map total: %u, connected total: %u)\n",
		count,
		NET_QSocketGetOwnerString(sock),
		NET_QSocketGetUnreliableReceiveMapDrops(sock),
		NET_QSocketGetUnreliableReceiveTotalDrops(sock));
}

static void cl_portpingprobe_enable_completion(cvar_t *var, const char *partial)
{
	Con_AddToTabList("0", partial, "disabled", NULL);
	Con_AddToTabList("1", partial, "connect, probe, reconnect", NULL);
	Con_AddToTabList("2", partial, "probe before connecting", NULL);
}

static void cl_portpingprobe_probes_completion(cvar_t *var, const char *partial)
{
	Con_AddToTabList("50", partial, "fast test", NULL);
	Con_AddToTabList("100", partial, "light probe", NULL);
	Con_AddToTabList("200", partial, "balanced", NULL);
	Con_AddToTabList("500", partial, "default", NULL);
	Con_AddToTabList("1000", partial, "max", NULL);
}

static void cl_portpingprobe_port_probes_completion(cvar_t *var, const char *partial)
{
	Con_AddToTabList("1", partial, "fastest", NULL);
	Con_AddToTabList("3", partial, "balanced", NULL);
	Con_AddToTabList("5", partial, "most reliable", NULL);
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

static void cl_portpingprobe_port_probes_changed(cvar_t *var)
{
	const int clamped = CLAMP(1, (int)var->value, PORTPINGPROBE_MAX_PORT_PROBES);

	if ((int)var->value == clamped)
		return;

	Con_Printf("cl_portpingprobe_port_probes must be between 1 and %d\n", PORTPINGPROBE_MAX_PORT_PROBES);
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
	const int clamped = CLAMP(0, (int)var->value, 2);

	if (var->value != (float)clamped)
	{
		Con_Printf("cl_portpingprobe_enable must be 0, 1, or 2\n");
		Cvar_SetValueQuick(var, (float)clamped);
		return;
	}

	if (var->value != 0)
		return;

	NET_PortPingProbe_RequestAbort();
	status = NET_PortPingProbe_GetStatus();
	if (status == PORTPINGPROBE_COMPLETED || status == PORTPINGPROBE_IDLE)
	{
		NET_PortPingProbe_ClearResult();
		NET_PortPingProbe_ClearPendingSocket();
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

int NET_PortPingProbe_GetMode(void)
{
	return CLAMP(0, (int)cl_portpingprobe_enable.value, 2);
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
	if (NET_PortPingProbe_GetStatus() != PORTPINGPROBE_PROBING || !portpingprobe_ctx || portpingprobe_ctx->total_work <= 0)
		return 0;

	progress_count = SDL_AtomicGet(&portpingprobe_progress);
	return CLAMP(0, (progress_count * 100) / portpingprobe_ctx->total_work, 100);
}

void NET_PortPingProbe_RequestAbort(void)
{
	portpingprobe_status_t status = NET_PortPingProbe_GetStatus();

	if (status == PORTPINGPROBE_IDLE || status == PORTPINGPROBE_COMPLETED)
		return;

	portpingprobe_abort_quiet = false;
	SDL_AtomicSet(&portpingprobe_abort_requested, 1);
	SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_ABORT);
}

void NET_PortPingProbe_RequestAbortQuietly(void)
{
	NET_PortPingProbe_RequestAbort();
	portpingprobe_abort_quiet = true;
}

static void NET_PortPingProbe_ClearResult(void)
{
	if (portpingprobe_result.has_socket &&
		portpingprobe_result.best_socket != INVALID_SOCKET &&
		portpingprobe_result.landriver >= 0 &&
		portpingprobe_result.landriver < net_numlandrivers)
	{
		net_landrivers[portpingprobe_result.landriver].Close_Socket(portpingprobe_result.best_socket);
	}

	portpingprobe_result.valid = false;
	portpingprobe_result.has_target = false;
	portpingprobe_result.has_socket = false;
	portpingprobe_result.connect_addr[0] = '\0';
	portpingprobe_result.landriver = -1;
	memset(&portpingprobe_result.target_addr, 0, sizeof(portpingprobe_result.target_addr));
	portpingprobe_result.best_port = 0;
	portpingprobe_result.best_rtt = 0;
	portpingprobe_result.best_jitter = 0;
	portpingprobe_result.best_replies = 0;
	portpingprobe_result.port_probes = 0;
	portpingprobe_result.responsive_ports = 0;
	portpingprobe_result.best_socket = INVALID_SOCKET;
}

static void NET_PortPingProbe_ClearPendingSocket(void)
{
	if (net_probe_clientsocket != INVALID_SOCKET &&
		net_probe_clientlandriver >= 0 &&
		net_probe_clientlandriver < net_numlandrivers)
	{
		net_landrivers[net_probe_clientlandriver].Close_Socket(net_probe_clientsocket);
	}

	net_probe_clientsocket = INVALID_SOCKET;
	net_probe_clientlandriver = -1;
	net_probe_clientport = 0;
	net_probe_clienthas_target = false;
	memset(&net_probe_clienttarget, 0, sizeof(net_probe_clienttarget));
}

static qboolean NET_PortPingProbe_GetPendingTarget(int *landriver, struct qsockaddr *target)
{
	if (!net_probe_clienthas_target || net_probe_clientlandriver < 0 ||
		net_probe_clientlandriver >= net_numlandrivers)
		return false;

	if (landriver)
		*landriver = net_probe_clientlandriver;
	if (target)
		*target = net_probe_clienttarget;
	return true;
}

static sys_socket_t NET_PortPingProbe_TakePendingSocket(int landriver, int *source_port)
{
	sys_socket_t sock = INVALID_SOCKET;

	if (source_port)
		*source_port = 0;
	if (landriver != net_probe_clientlandriver)
		return INVALID_SOCKET;

	if (source_port)
		*source_port = net_probe_clientport;
	sock = net_probe_clientsocket;
	net_probe_clientsocket = INVALID_SOCKET;
	net_probe_clientlandriver = -1;
	net_probe_clientport = 0;
	return sock;
}

static void NET_PortPingProbe_FreeContext(portpingprobe_ctx_t *ctx)
{
	if (!ctx)
		return;

	if (ctx->best_socket != INVALID_SOCKET &&
		ctx->landriver >= 0 && ctx->landriver < net_numlandrivers)
	{
		net_landrivers[ctx->landriver].Close_Socket(ctx->best_socket);
		ctx->best_socket = INVALID_SOCKET;
	}
	if (ctx->candidates)
	{
		Z_Free(ctx->candidates);
		ctx->candidates = NULL;
	}
	Z_Free(ctx);
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

	NET_PortPingProbe_ClearPendingSocket();
	net_probe_clientport = portpingprobe_result.best_port > 0 ? portpingprobe_result.best_port : 0;
	net_probe_clientlandriver = portpingprobe_result.landriver;
	net_probe_clienthas_target = portpingprobe_result.has_target;
	if (portpingprobe_result.has_target)
		net_probe_clienttarget = portpingprobe_result.target_addr;
	if (portpingprobe_result.has_socket)
	{
		net_probe_clientsocket = portpingprobe_result.best_socket;
		portpingprobe_result.has_socket = false;
		portpingprobe_result.best_socket = INVALID_SOCKET;
	}
	NET_PortPingProbe_ClearResult();
	SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_IDLE);
	return true;
}

static double NET_PortPingProbeTime(void)
{
	return SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

static qboolean NET_PortPingProbeReplyValid(const portpingprobe_ctx_t *ctx,
	const struct qsockaddr *recvaddr, const byte *data, int length, portpingprobe_query_t query)
{
	int control;

	if (!ctx || !recvaddr || !data || length < 5)
		return false;
	if (net_landrivers[ctx->landriver].AddrCompare((struct qsockaddr *)recvaddr,
		(struct qsockaddr *)&ctx->target_addr) != 0)
		return false;

	if (query == PORTPINGPROBE_QUERY_GETINFO)
	{
		static const byte header[] = {0xff, 0xff, 0xff, 0xff};
		static const char response[] = "infoResponse\n";

		return length >= (int)(sizeof(header) + sizeof(response) - 1) &&
			!memcmp(data, header, sizeof(header)) &&
			!memcmp(data + sizeof(header), response, sizeof(response) - 1);
	}

	if (query != PORTPINGPROBE_QUERY_SERVER_INFO)
		return false;

	memcpy(&control, data, sizeof(control));
	control = BigLong(control);
	return (control & (~NETFLAG_LENGTH_MASK)) == (int)NETFLAG_CTL &&
		(control & NETFLAG_LENGTH_MASK) == length && data[4] == CCREP_SERVER_INFO;
}

static double NET_PortPingProbeSocket(const portpingprobe_ctx_t *ctx, sys_socket_t sock,
	portpingprobe_query_t query)
{
	net_landriver_t *ldrv;
	struct qsockaddr recvaddr;
	byte recvbuf[2048];
	const byte *packet;
	int packet_size;
	double start_time;

	if (!ctx || sock == INVALID_SOCKET)
		return -1;

	ldrv = &net_landrivers[ctx->landriver];
	if (query == PORTPINGPROBE_QUERY_GETINFO)
	{
		packet = portpingprobe_getinfo_packet;
		packet_size = sizeof(portpingprobe_getinfo_packet);
	}
	else if (query == PORTPINGPROBE_QUERY_SERVER_INFO)
	{
		packet = ctx->serverinfo_packet;
		packet_size = sizeof(ctx->serverinfo_packet);
	}
	else
	{
		return -1;
	}

	/* Drain any delayed response before starting a new timed sample. */
	while (ldrv->Read(sock, recvbuf, sizeof(recvbuf), &recvaddr) > 0)
		;

	start_time = NET_PortPingProbeTime();
	if (ldrv->Write(sock, (byte *)packet, packet_size, (struct qsockaddr *)&ctx->target_addr) == -1)
		return -1;

	for (;;)
	{
		double now = NET_PortPingProbeTime();
		double remaining = PORTPINGPROBE_TIMEOUT - (now - start_time);
		double wait_time;
		fd_set readfds;
		struct timeval timeout;
		int selected;
		int ret;

		if (SDL_AtomicGet(&portpingprobe_abort_requested) || remaining <= 0)
			return -1;

		wait_time = q_min(remaining, PORTPINGPROBE_SELECT_SLICE);
		timeout.tv_sec = (long)wait_time;
		timeout.tv_usec = (long)((wait_time - timeout.tv_sec) * 1000000.0);
		if (timeout.tv_sec == 0 && timeout.tv_usec <= 0)
			timeout.tv_usec = 1;

		FD_ZERO(&readfds);
		FD_SET(sock, &readfds);
#ifdef _WIN32
		selected = selectsocket(0, &readfds, NULL, NULL, &timeout);
#else
		selected = selectsocket((int)(sock + 1), &readfds, NULL, NULL, &timeout);
#endif
		if (selected == SOCKET_ERROR)
		{
			int err = SOCKETERRNO;
#ifdef _WIN32
			if (err == WSAEINTR)
#else
			if (err == EINTR)
#endif
				continue;
			Con_DPrintf("Port ping probe select failed: %s\n", socketerror(err));
			return -1;
		}
		if (selected == 0 || !FD_ISSET(sock, &readfds))
			continue;

		ret = ldrv->Read(sock, recvbuf, sizeof(recvbuf), &recvaddr);
		if (ret < 0)
			return -1;
		if (ret > 0 && NET_PortPingProbeReplyValid(ctx, &recvaddr, recvbuf, ret, query))
			return NET_PortPingProbeTime() - start_time;
	}
}

static double NET_PortPingProbeSingle(const portpingprobe_ctx_t *ctx, int source_port,
	portpingprobe_query_t query)
{
	sys_socket_t sock;
	double rtt;

	if (!ctx)
		return -1;

	sock = net_landrivers[ctx->landriver].Open_Socket(source_port);
	if (sock == INVALID_SOCKET)
		return -1;
	rtt = NET_PortPingProbeSocket(ctx, sock, query);
	net_landrivers[ctx->landriver].Close_Socket(sock);
	return rtt;
}

static portpingprobe_query_t NET_PortPingProbeDetectQuery(const portpingprobe_ctx_t *ctx,
	sys_socket_t sock)
{
	if (NET_PortPingProbeSocket(ctx, sock, PORTPINGPROBE_QUERY_GETINFO) >= 0)
		return PORTPINGPROBE_QUERY_GETINFO;
	if (!SDL_AtomicGet(&portpingprobe_abort_requested) &&
		NET_PortPingProbeSocket(ctx, sock, PORTPINGPROBE_QUERY_SERVER_INFO) >= 0)
		return PORTPINGPROBE_QUERY_SERVER_INFO;
	return PORTPINGPROBE_QUERY_NONE;
}

static int NET_PortPingProbeCompareDouble(const void *left, const void *right)
{
	const double a = *(const double *)left;
	const double b = *(const double *)right;
	return a < b ? -1 : a > b ? 1 : 0;
}

static void NET_PortPingProbeCalculateSampleStats(const double *samples, int count,
	double *median_rtt, double *jitter)
{
	double sorted[PORTPINGPROBE_MAX_PORT_PROBES];
	double deviations[PORTPINGPROBE_MAX_PORT_PROBES];
	int middle;
	int i;

	if (!samples || count <= 0 || count > PORTPINGPROBE_MAX_PORT_PROBES ||
		!median_rtt || !jitter)
		return;

	memcpy(sorted, samples, count * sizeof(sorted[0]));
	qsort(sorted, count, sizeof(sorted[0]), NET_PortPingProbeCompareDouble);
	middle = count / 2;
	*median_rtt = (count & 1)
		? sorted[middle]
		: (sorted[middle - 1] + sorted[middle]) * 0.5;

	for (i = 0; i < count; i++)
		deviations[i] = fabs(samples[i] - *median_rtt);
	qsort(deviations, count, sizeof(deviations[0]), NET_PortPingProbeCompareDouble);
	*jitter = (count & 1)
		? deviations[middle]
		: (deviations[middle - 1] + deviations[middle]) * 0.5;
}

static void NET_PortPingProbeCalculateStats(portpingprobe_candidate_t *candidate)
{
	if (!candidate || candidate->replies <= 0)
		return;

	NET_PortPingProbeCalculateSampleStats(candidate->samples, candidate->replies,
		&candidate->median_rtt, &candidate->jitter);
}

static int NET_PortPingProbeWorker(void *data)
{
	portpingprobe_ctx_t *ctx = data;
	int finalists[PORTPINGPROBE_FINALISTS];
	int progress = 0;
	int best_endpoint = -1;
	int best_index = -1;
	double best_score = 0;
	int required_replies;
	int round;
	int i;

	if (!ctx)
		return 0;

	/* Select the destination first. Source-port results are meaningful only for
	 * the exact address family and route that will be used by the connection. */
	for (i = 0; i < ctx->endpoint_count; i++)
	{
		portpingprobe_endpoint_t *endpoint = &ctx->endpoints[i];
		int sample;

		ctx->landriver = endpoint->landriver;
		ctx->target_addr = endpoint->addr;
		endpoint->socket = net_landrivers[endpoint->landriver].Open_Socket(0);
		endpoint->query = NET_PortPingProbeDetectQuery(ctx, endpoint->socket);

		for (sample = 0; sample < PORTPINGPROBE_ENDPOINT_PROBES; sample++)
		{
			double rtt = -1;

			if (SDL_AtomicGet(&portpingprobe_abort_requested))
				goto finished;
			if (endpoint->query != PORTPINGPROBE_QUERY_NONE)
				rtt = NET_PortPingProbeSocket(ctx, endpoint->socket, endpoint->query);
			if (rtt >= 0)
				endpoint->samples[endpoint->replies++] = rtt;
			SDL_AtomicSet(&portpingprobe_progress, ++progress);

			if (ctx->delay_ms > 0)
				Sys_Sleep(ctx->delay_ms);
		}

		if (endpoint->replies >= 2)
		{
			double score;

			NET_PortPingProbeCalculateSampleStats(endpoint->samples, endpoint->replies,
				&endpoint->median_rtt, &endpoint->jitter);
			score = endpoint->median_rtt + endpoint->jitter;
			Con_DPrintf("Port probe endpoint %s via %s: median %.2f ms, jitter %.2f ms (%d/%d replies)\n",
				net_landrivers[endpoint->landriver].AddrToString(&endpoint->addr, false),
				net_landrivers[endpoint->landriver].name,
				endpoint->median_rtt * 1000.0, endpoint->jitter * 1000.0,
				endpoint->replies, PORTPINGPROBE_ENDPOINT_PROBES);
			if (best_endpoint < 0 ||
				endpoint->replies > ctx->endpoints[best_endpoint].replies ||
				(endpoint->replies == ctx->endpoints[best_endpoint].replies && score < best_score))
			{
				best_endpoint = i;
				best_score = score;
			}
		}
		else
		{
			Con_DPrintf("Port probe endpoint %s via %s rejected: %d/%d replies\n",
				net_landrivers[endpoint->landriver].AddrToString(&endpoint->addr, false),
				net_landrivers[endpoint->landriver].name,
				endpoint->replies, PORTPINGPROBE_ENDPOINT_PROBES);
		}

		if (endpoint->socket != INVALID_SOCKET)
		{
			net_landrivers[endpoint->landriver].Close_Socket(endpoint->socket);
			endpoint->socket = INVALID_SOCKET;
		}
	}

	if (best_endpoint < 0)
	{
		ctx->landriver = -1;
		memset(&ctx->target_addr, 0, sizeof(ctx->target_addr));
		ctx->query = PORTPINGPROBE_QUERY_NONE;
		goto finished;
	}

	ctx->landriver = ctx->endpoints[best_endpoint].landriver;
	ctx->target_addr = ctx->endpoints[best_endpoint].addr;
	ctx->query = ctx->endpoints[best_endpoint].query;
	Con_DPrintf("Port ping probe selected %s via %s using %s replies\n",
		net_landrivers[ctx->landriver].AddrToString(&ctx->target_addr, false),
		net_landrivers[ctx->landriver].name,
		ctx->query == PORTPINGPROBE_QUERY_GETINFO ? "getinfo" : "NetQuake server-info");

	for (i = 0; i < ctx->num_probes; i++)
	{
		portpingprobe_candidate_t *candidate = &ctx->candidates[i];
		int position;
		int move;

		if (SDL_AtomicGet(&portpingprobe_abort_requested))
			goto finished;

		candidate->discovery_rtt = NET_PortPingProbeSingle(ctx, candidate->port, ctx->query);
		SDL_AtomicSet(&portpingprobe_progress, ++progress);
		if (candidate->discovery_rtt >= 0)
		{
			ctx->responsive_ports++;
			for (position = 0; position < ctx->finalist_count; position++)
			{
				if (candidate->discovery_rtt < ctx->candidates[finalists[position]].discovery_rtt)
					break;
			}
			if (position < PORTPINGPROBE_FINALISTS)
			{
				if (ctx->finalist_count < PORTPINGPROBE_FINALISTS)
					ctx->finalist_count++;
				for (move = ctx->finalist_count - 1; move > position; move--)
					finalists[move] = finalists[move - 1];
				finalists[position] = i;
			}
		}

		if (ctx->delay_ms > 0)
			Sys_Sleep(ctx->delay_ms);
	}

	for (i = 0; i < ctx->finalist_count; i++)
	{
		portpingprobe_candidate_t *candidate = &ctx->candidates[finalists[i]];
		candidate->socket = net_landrivers[ctx->landriver].Open_Socket(candidate->port);
	}

	/* Rotate the starting candidate each round so temporal changes do not favor one port. */
	for (round = 0; round < ctx->port_probes; round++)
	{
		int offset;
		for (offset = 0; offset < ctx->finalist_count; offset++)
		{
			int slot = (offset + round) % ctx->finalist_count;
			portpingprobe_candidate_t *candidate = &ctx->candidates[finalists[slot]];
			double rtt = -1;

			if (SDL_AtomicGet(&portpingprobe_abort_requested))
				goto finished;
			if (candidate->socket != INVALID_SOCKET)
				rtt = NET_PortPingProbeSocket(ctx, candidate->socket, ctx->query);
			if (rtt >= 0 && candidate->replies < PORTPINGPROBE_MAX_PORT_PROBES)
				candidate->samples[candidate->replies++] = rtt;
			SDL_AtomicSet(&portpingprobe_progress, ++progress);

			if (ctx->delay_ms > 0)
				Sys_Sleep(ctx->delay_ms);
		}
	}

	required_replies = (ctx->port_probes * 4 + 4) / 5; /* at least 80 percent */
	for (i = 0; i < ctx->finalist_count; i++)
	{
		int index = finalists[i];
		portpingprobe_candidate_t *candidate = &ctx->candidates[index];
		double score;

		if (candidate->replies < required_replies)
		{
			Con_DPrintf("Port probe finalist %d rejected: %d/%d replies\n",
				candidate->port, candidate->replies, ctx->port_probes);
			continue;
		}
		NET_PortPingProbeCalculateStats(candidate);
		score = candidate->median_rtt + candidate->jitter;
		Con_DPrintf("Port probe finalist %d: discovery %.2f ms, median %.2f ms, jitter %.2f ms (%d/%d replies)\n",
			candidate->port, candidate->discovery_rtt * 1000.0,
			candidate->median_rtt * 1000.0, candidate->jitter * 1000.0,
			candidate->replies, ctx->port_probes);
		if (best_index < 0 || score < best_score ||
			(score == best_score && candidate->replies > ctx->candidates[best_index].replies))
		{
			best_index = index;
			best_score = score;
		}
	}

	if (best_index >= 0)
	{
		portpingprobe_candidate_t *best = &ctx->candidates[best_index];
		ctx->best_port = best->port;
		ctx->best_rtt = best->median_rtt;
		ctx->best_jitter = best->jitter;
		ctx->best_replies = best->replies;
		ctx->best_socket = best->socket;
		best->socket = INVALID_SOCKET;
	}

finished:
	for (i = 0; i < ctx->endpoint_count; i++)
	{
		portpingprobe_endpoint_t *endpoint = &ctx->endpoints[i];
		if (endpoint->socket != INVALID_SOCKET)
		{
			net_landrivers[endpoint->landriver].Close_Socket(endpoint->socket);
			endpoint->socket = INVALID_SOCKET;
		}
	}
	if (ctx->candidates)
	{
		for (i = 0; i < ctx->num_probes; i++)
		{
			if (ctx->candidates[i].socket != INVALID_SOCKET)
			{
				net_landrivers[ctx->landriver].Close_Socket(ctx->candidates[i].socket);
				ctx->candidates[i].socket = INVALID_SOCKET;
			}
		}
	}

	if (SDL_AtomicGet(&portpingprobe_abort_requested))
	{
		if (ctx->best_socket != INVALID_SOCKET)
		{
			net_landrivers[ctx->landriver].Close_Socket(ctx->best_socket);
			ctx->best_socket = INVALID_SOCKET;
		}
		SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_ABORT);
	}
	else
	{
		SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_COMPLETED);
	}

	SDL_AtomicSet(&portpingprobe_worker_running, 0);
	return 0;
}

qboolean NET_PortPingProbe_Start(const char *connect_addr)
{
	portpingprobe_ctx_t *ctx;
	portpingprobe_endpoint_t endpoints[MAX_NET_DRIVERS];
	char display_addr[MAX_SERVER_ADDRESS_LEN * 2];
	unsigned int random_state;
	int endpoint_count = 0;
	int num_probes;
	int port_probes;
	int control;
	int i;

	if (!connect_addr || !connect_addr[0] || !NET_PortPingProbe_IsEnabled())
		return false;

	if (NET_PortPingProbe_GetStatus() != PORTPINGPROBE_IDLE)
		return false;

	memset(endpoints, 0, sizeof(endpoints));
	for (i = 0; i < net_numlandrivers; i++)
	{
		if (!net_landrivers[i].initialized)
			continue;

		if (endpoint_count < MAX_NET_DRIVERS &&
			net_landrivers[i].GetAddrFromName(connect_addr,
				&endpoints[endpoint_count].addr) != -1)
		{
			endpoints[endpoint_count].landriver = i;
			endpoints[endpoint_count].socket = INVALID_SOCKET;
			endpoint_count++;
		}
	}

	if (endpoint_count <= 0)
	{
		Con_SafePrintf("Could not resolve %s\n", connect_addr);
		return false;
	}

	num_probes = CLAMP(1, (int)cl_portpingprobe_probes.value, 1000);
	port_probes = CLAMP(1, (int)cl_portpingprobe_port_probes.value, PORTPINGPROBE_MAX_PORT_PROBES);
	ctx = Z_Malloc(sizeof(*ctx));
	ctx->num_probes = num_probes;
	ctx->port_probes = port_probes;
	ctx->total_work = endpoint_count * PORTPINGPROBE_ENDPOINT_PROBES + num_probes +
		q_min(num_probes, PORTPINGPROBE_FINALISTS) * port_probes;
	ctx->delay_ms = cl_portpingprobe_delay.value > 0 ? (unsigned long)cl_portpingprobe_delay.value : 0;
	ctx->landriver = -1;
	memset(&ctx->target_addr, 0, sizeof(ctx->target_addr));
	control = BigLong(NETFLAG_CTL | ((int)sizeof(ctx->serverinfo_packet) & NETFLAG_LENGTH_MASK));
	memcpy(ctx->serverinfo_packet, &control, sizeof(control));
	ctx->serverinfo_packet[4] = CCREQ_SERVER_INFO;
	memcpy(ctx->serverinfo_packet + 5, "QUAKE", sizeof("QUAKE"));
	ctx->serverinfo_packet[5 + sizeof("QUAKE")] = NET_PROTOCOL_VERSION;
	ctx->query = PORTPINGPROBE_QUERY_NONE;
	memcpy(ctx->endpoints, endpoints, endpoint_count * sizeof(endpoints[0]));
	ctx->endpoint_count = endpoint_count;
	ctx->candidates = Z_Malloc(num_probes * sizeof(*ctx->candidates));
	ctx->responsive_ports = 0;
	ctx->finalist_count = 0;
	ctx->best_port = 0;
	ctx->best_rtt = -1;
	ctx->best_jitter = 0;
	ctx->best_replies = 0;
	ctx->best_socket = INVALID_SOCKET;
	q_strlcpy(ctx->connect_addr, connect_addr, sizeof(ctx->connect_addr));
	ctx->thread = NULL;

	/* Generate without replacement so every discovery probe covers a new port. */
	random_state = (unsigned int)(NET_PortPingProbeTime() * 1000000.0) ^ (unsigned int)(uintptr_t)ctx;
	for (i = 0; i < num_probes; i++)
	{
		int candidate_port;
		int previous;
		do
		{
			random_state = random_state * 1664525u + 1013904223u;
			candidate_port = 1024 + (int)(random_state % 64512u);
			for (previous = 0; previous < i; previous++)
			{
				if (ctx->candidates[previous].port == candidate_port)
					break;
			}
		} while (previous != i);
		ctx->candidates[i].port = candidate_port;
		ctx->candidates[i].discovery_rtt = -1;
		ctx->candidates[i].socket = INVALID_SOCKET;
	}

	NET_PortPingProbe_ClearResult();
	NET_PortPingProbe_ClearPendingSocket();
	SDL_AtomicSet(&portpingprobe_abort_requested, 0);
	SDL_AtomicSet(&portpingprobe_progress, 0);
	SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_PROBING);
	SDL_AtomicSet(&portpingprobe_worker_running, 1);
	portpingprobe_abort_quiet = false;
	portpingprobe_last_percent = -1;
	portpingprobe_console_inline = false;

	ctx->thread = SDL_CreateThread(NET_PortPingProbeWorker, "portpingprobe", ctx);
	if (!ctx->thread)
	{
		Con_Printf("NET_PortPingProbe_Start: failed to create worker thread\n");
		SDL_AtomicSet(&portpingprobe_worker_running, 0);
		SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_IDLE);
		NET_PortPingProbe_FreeContext(ctx);
		return false;
	}

	portpingprobe_ctx = ctx;
	NET_HostnameCache_FormatDetailedDisplay(connect_addr, net_hostport,
		display_addr, sizeof(display_addr));
	Con_Printf("Probing %s across %d network endpoint%s to find best source port (%d unique ports, %d confirmation probes on up to %d finalists)\n",
		display_addr, endpoint_count, endpoint_count == 1 ? "" : "s",
		num_probes, port_probes, PORTPINGPROBE_FINALISTS);
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
			Con_SafePrintf("Port probe progress: ^g%d^g^m%%^m\r", progress_percent);
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
		char display_addr[MAX_SERVER_ADDRESS_LEN * 2];
		NET_HostnameCache_FormatDetailedDisplay(ctx->connect_addr, net_hostport,
			display_addr, sizeof(display_addr));
		portpingprobe_result.valid = true;
		portpingprobe_result.has_target = ctx->landriver >= 0 &&
			ctx->landriver < net_numlandrivers && ctx->query != PORTPINGPROBE_QUERY_NONE;
		q_strlcpy(portpingprobe_result.connect_addr, ctx->connect_addr, sizeof(portpingprobe_result.connect_addr));
		portpingprobe_result.landriver = ctx->landriver;
		portpingprobe_result.target_addr = ctx->target_addr;
		portpingprobe_result.best_port = ctx->best_port;
		portpingprobe_result.best_rtt = ctx->best_rtt;
		portpingprobe_result.best_jitter = ctx->best_jitter;
		portpingprobe_result.best_replies = ctx->best_replies;
		portpingprobe_result.port_probes = ctx->port_probes;
		portpingprobe_result.responsive_ports = ctx->responsive_ports;
		portpingprobe_result.has_socket = ctx->best_socket != INVALID_SOCKET;
		portpingprobe_result.best_socket = ctx->best_socket;
		ctx->best_socket = INVALID_SOCKET;

		if (!portpingprobe_result.has_target)
		{
			Con_Printf("Port probe completed: no stable network endpoint found; falling back to normal connection selection\n");
		}
		else if (ctx->best_port > 0)
		{
			Con_Printf("Port probe completed for %s via %s: source port %d, median %.2f ms, jitter %.2f ms (%d/%d replies, %d responsive ports)\n",
				display_addr,
				net_landrivers[ctx->landriver].name,
				ctx->best_port, ctx->best_rtt * 1000.0, ctx->best_jitter * 1000.0,
				ctx->best_replies, ctx->port_probes, ctx->responsive_ports);
		}
		else
		{
			Con_Printf("Port probe completed for %s via %s: no stable source port found (%d responsive), falling back to OS-assigned source port\n",
				display_addr,
				net_landrivers[ctx->landriver].name, ctx->responsive_ports);
		}

		Cbuf_AddText(va("connect \"%s\"\n", ctx->connect_addr));
	}
	else
	{
		if (status == PORTPINGPROBE_ABORT && !portpingprobe_abort_quiet)
			Con_Printf("Port ping probe aborted\n");

		NET_PortPingProbe_ClearResult();
		SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_IDLE);
	}

	NET_PortPingProbe_FreeContext(ctx);
	SDL_AtomicSet(&portpingprobe_abort_requested, 0);
	SDL_AtomicSet(&portpingprobe_progress, 0);
	portpingprobe_last_percent = -1;
	portpingprobe_console_inline = false;
	portpingprobe_abort_quiet = false;
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

		NET_PortPingProbe_FreeContext(ctx);
	}

	NET_PortPingProbe_ClearResult();
	NET_PortPingProbe_ClearPendingSocket();
	SDL_AtomicSet(&portpingprobe_abort_requested, 0);
	SDL_AtomicSet(&portpingprobe_worker_running, 0);
	SDL_AtomicSet(&portpingprobe_progress, 0);
	SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_IDLE);
	portpingprobe_last_percent = -1;
	portpingprobe_console_inline = false;
	portpingprobe_abort_quiet = false;
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

static void _Datagram_ServerControlPacket (sys_socket_t acceptsock, struct qsockaddr *clientaddr, byte *data, unsigned int length, size_t capacity);

#define MALFORMED_PACKET_LOG_LIMIT 8

//Keep enough detail to diagnose an interop problem without allowing
//unauthenticated UDP traffic to grow a -condebug log without bound.
static qboolean Datagram_RecordMalformedPacket (malformed_packet_reason_t reason, unsigned int first_value, unsigned int second_value)
{
	malformed_packet_sample_t sample;

	sample.valid = true;
	sample.reason = reason;
	sample.first_value = first_value;
	sample.second_value = second_value;

	if (!malformedPacketFirst.valid)
		malformedPacketFirst = sample;
	malformedPacketLatest = sample;

	if (malformedPacketCount < INT_MAX)
		malformedPacketCount++;

	if (malformedPacketCount <= MALFORMED_PACKET_LOG_LIMIT)
		return true;

	if (malformedPacketCount == MALFORMED_PACKET_LOG_LIMIT + 1)
		Con_DPrintf("Further malformed datagram messages suppressed; use net_stats for the total\n");

	return false;
}

//NETFLAG_LENGTH_MASK allows a declared length far larger than packetBuffer, so the
//header's length field must be reconciled against the datagram we actually read
//before anything indexes packetBuffer with it.
static qboolean Datagram_DecodePacketHeader (unsigned int received_length, unsigned int *packet_length, unsigned int *flags)
{
	unsigned int	header;
	unsigned int	declared_length;

	if (received_length < NET_HEADERSIZE)
	{
		shortPacketCount++;
		return false;
	}

	header = BigLong(packetBuffer.length);
	declared_length = header & NETFLAG_LENGTH_MASK;

	if (declared_length < NET_HEADERSIZE || declared_length != received_length)
	{
		if (Datagram_RecordMalformedPacket(MALFORMED_PACKET_LENGTH, declared_length, received_length))
			Con_DPrintf("Dropping datagram with invalid length (%u declared, %u received)\n",
				declared_length, received_length);
		return false;
	}

	*flags = header & (~NETFLAG_LENGTH_MASK);
	*packet_length = declared_length;
	return true;
}

qboolean Datagram_ProcessPacket(unsigned int received_length, qsocket_t *sock)
{
	unsigned int	flags;
	unsigned int	length;
	unsigned int	sequence;
	unsigned int	count;

	if (!Datagram_DecodePacketHeader(received_length, &length, &flags))
		return false;

	if (flags & NETFLAG_CTL)
		return false;	//should only be for OOB packets.

	sequence = BigLong(packetBuffer.sequence);
	packetsReceived++;

	if (flags & NETFLAG_UNRELIABLE)
	{
		count = 0;
		if (sequence < sock->unreliableReceiveSequence)
		{
			Con_DPrintf("Got a stale datagram\n");
			return false;
		}
		if (sequence != sock->unreliableReceiveSequence)
		{
			count = sequence - sock->unreliableReceiveSequence;
			droppedDatagrams += count;
		}
		NET_QSocketRecordUnreliableReceive(sock, count);
		if (count)
			Datagram_LogDroppedDatagrams(sock, count);
		sock->unreliableReceiveSequence = sequence + 1;

		length -= NET_HEADERSIZE;

		if (length > (unsigned int)net_message.maxsize)
		{	//is this even possible? maybe it will be in the future! either way, no sys_errors please.
			//NB: must be false, not -1 - the caller treats any non-zero return as
			//"net_message holds a packet to parse", and we never filled it.
			Con_Printf("Over-sized unreliable\n");
			return false;
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
			{	//false, not -1: net_message was not filled, so there is nothing to parse.
				Con_Printf("Over-sized reliable\n");
				return false;
			}
			SZ_Clear(&net_message);
			SZ_Write(&net_message, sock->receiveMessage, sock->receiveMessageLength);
			SZ_Write(&net_message, packetBuffer.data, length);
			sock->receiveMessageLength = 0;

			messagesReceived++;
			return true;	//parse this reliable!
		}

		if (sock->receiveMessageLength + length > sizeof(sock->receiveMessage))
		{	//false, not -1: net_message was not filled, so there is nothing to parse.
			Con_Printf("Over-sized reliable\n");
			return false;
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
			length = dfunc.Read(sock, (byte *)&packetBuffer, PACKET_BUFFER_RECV_SIZE, &addr);
			if (length == -1 || !length)
			{
				//no more packets, move on to the next.
				break;
			}

			if (length < 4)
				continue;

			if (BigLong(packetBuffer.length) & NETFLAG_CTL)
			{
				_Datagram_ServerControlPacket(sock, &addr, (byte *)&packetBuffer, length, PACKET_BUFFER_PARSE_CAPACITY);

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
					}
					//the address matched an active client either way — acks, stale
					//datagrams and duplicate reliables are already fully handled by
					//Datagram_ProcessPacket and must not leak into the ICE stack below.
					break;
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

		if (s->sendNext)
			SendMessageNext (s);
		if (!s->canSend)
			if ((net_time - s->lastSendTime) > 1.0)
				ReSendMessage (s);

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
	unsigned int	packetlength;
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
							PACKET_BUFFER_RECV_SIZE, &readaddr);

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

		if (BigLong(packetBuffer.length) == 0xffffffff)
			continue;	//some kind of lingering QW or DP response?

		if (!Datagram_DecodePacketHeader(length, &packetlength, &flags))
			continue;
		length = packetlength;

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
			count = 0;
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
				Con_DPrintf("Dropped %u datagram(s) for %s\n", count, NET_QSocketGetOwnerString(sock)); // woods #droplog
				cl.packetloss = count; // woods #scrpl
				cl.pltotal = droppedDatagrams; // woods #scrpl
			}
			NET_QSocketRecordUnreliableReceive(sock, count);
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

static void Datagram_PrintMalformedPacketSample (const char *label, const malformed_packet_sample_t *sample)
{
	switch (sample->reason)
	{
	case MALFORMED_PACKET_LENGTH:
		Con_Printf("%s = length mismatch (%u declared, %u received)\n",
			label, sample->first_value, sample->second_value);
		break;
	case MALFORMED_PACKET_CONTROL_CAPACITY:
		Con_Printf("%s = control capacity (%u length, %u capacity)\n",
			label, sample->first_value, sample->second_value);
		break;
	case MALFORMED_PACKET_TERMINATOR_HEADROOM:
		Con_Printf("%s = terminator headroom (%u length, %u capacity)\n",
			label, sample->first_value, sample->second_value);
		break;
	}
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
		Con_Printf("malformedPacketCount       = %i\n", malformedPacketCount);
		if (malformedPacketFirst.valid)
		{
			Datagram_PrintMalformedPacketSample("malformedPacketFirst      ", &malformedPacketFirst);
			Datagram_PrintMalformedPacketSample("malformedPacketLatest     ", &malformedPacketLatest);
		}
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
	static char	noport[MAX_SERVER_ADDRESS_LEN];
	net_endpoint_t endpoint;

	if (!host || !*host)
		return host;
	if (!NET_ParseEndpoint(host, net_hostport, &endpoint))
		return host;
	if (endpoint.port_explicit && endpoint.port != net_hostport)
	{
		net_hostport = endpoint.port;
		Con_SafePrintf("Port set to %d\n", net_hostport);
	}
	q_strlcpy(noport, endpoint.host, sizeof(noport));
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
	Cvar_RegisterVariable(&cl_portpingprobe_port_probes);
	Cvar_RegisterVariable(&cl_portpingprobe_delay);
	Cvar_SetCompletion(&cl_portpingprobe_enable, cl_portpingprobe_enable_completion);
	Cvar_SetCompletion(&cl_portpingprobe_probes, cl_portpingprobe_probes_completion);
	Cvar_SetCompletion(&cl_portpingprobe_port_probes, cl_portpingprobe_port_probes_completion);
	Cvar_SetCompletion(&cl_portpingprobe_delay, cl_portpingprobe_delay_completion);
	Cvar_SetCallback(&cl_portpingprobe_enable, cl_portpingprobe_enable_changed);
	Cvar_SetCallback(&cl_portpingprobe_probes, cl_portpingprobe_probes_changed);
	Cvar_SetCallback(&cl_portpingprobe_port_probes, cl_portpingprobe_port_probes_changed);
	Cvar_SetCallback(&cl_portpingprobe_delay, cl_portpingprobe_delay_changed);
	SDL_AtomicSet(&portpingprobe_status, PORTPINGPROBE_IDLE);
	SDL_AtomicSet(&portpingprobe_abort_requested, 0);
	SDL_AtomicSet(&portpingprobe_worker_running, 0);
	SDL_AtomicSet(&portpingprobe_progress, 0);
	NET_PortPingProbe_ClearResult();
	NET_PortPingProbe_ClearPendingSocket();

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
	// The redirect buffer can contain 8191 text bytes. Reserve the control
	// header, response opcode, and trailing NUL in addition to that payload.
	byte buffer[CON_REDIRECT_BUFFER_SIZE + sizeof(int) + 2];
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

	// append QC-set serverinfo keys (e.g. "serverinfo mod crmod7"),
	// skipping any that are already present from the hardcoded fields above.
	{
		const char *p = svs.serverinfo;
		while (*p == '\\')
		{
			const char *keystart, *keyend, *valstart, *valend;
			char keybuf[64], tmp[64];

			if (ofs >= outsize)
				break;

			p++;
			keystart = p;
			while (*p && *p != '\\') p++;
			keyend = p;
			if (*p == '\\') p++;
			valstart = p;
			while (*p && *p != '\\') p++;
			valend = p;

			// only append if this key isn't already in the output
			{
				size_t kl = keyend - keystart;
				if (kl >= sizeof(keybuf)) kl = sizeof(keybuf) - 1;
				memcpy(keybuf, keystart, kl); keybuf[kl] = 0;
			}
			if (!*Info_GetKey(out, keybuf, tmp, sizeof(tmp)))
			{
				size_t vl = valend - valstart;
				q_snprintf(out+ofs, outsize-ofs, "\\%s\\", keybuf); ofs += strlen(out+ofs);
				if (ofs >= outsize)
					break;
				if (vl > outsize-ofs-1) vl = outsize-ofs-1;
				memcpy(out+ofs, valstart, vl); ofs += vl; out[ofs] = 0;
			}
		}
	}
}

/*find the next key/value pair in a \key\value info string after prevkey.
  pass "" for prevkey to get the first pair. returns false when no more keys.*/
qboolean Info_FindNextKey(const char *info, const char *prevkey, char *outkey, size_t outkeysize, char *outval, size_t outvalsize)
{
	const char *p = info;
	qboolean found_prev = (!*prevkey);	/*empty prevkey = start from beginning*/

	*outkey = 0;
	*outval = 0;

	while (*p == '\\')
	{
		const char *keystart, *keyend, *valstart, *valend;

		p++;	/*skip leading backslash*/
		keystart = p;
		while (*p && *p != '\\')
			p++;
		keyend = p;
		if (*p == '\\')
			p++;
		valstart = p;
		while (*p && *p != '\\')
			p++;
		valend = p;

		if (found_prev)
		{
			size_t kl = keyend - keystart;
			size_t vl = valend - valstart;
			if (kl >= outkeysize) kl = outkeysize - 1;
			if (vl >= outvalsize) vl = outvalsize - 1;
			memcpy(outkey, keystart, kl); outkey[kl] = 0;
			memcpy(outval, valstart, vl); outval[vl] = 0;
			return true;
		}

		if ((size_t)(keyend - keystart) == strlen(prevkey) && !strncmp(keystart, prevkey, keyend - keystart))
			found_prev = true;
	}
	return false;
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
void _Datagram_BrokerPacket(byte *data, unsigned int length, size_t capacity, sys_socket_t sock, struct qsockaddr *addr)
{
	_Datagram_ServerControlPacket(sock, addr, data, length, capacity);
}

// capacity includes caller-owned terminator headroom. Text packets are dropped
// unless data[length] is inside that allocation; they are never truncated.
static void _Datagram_ServerControlPacket (sys_socket_t acceptsock, struct qsockaddr *clientaddr, byte *data, unsigned int length, size_t capacity)
{
	struct qsockaddr newaddr;
	qsocket_t	*sock;
	qsocket_t	*s;
	int			command;
	int			control;
	int			ret;
	int plnum;
	int mod, /*mod_ver, mod_flags,*/ mod_passwd;	//proquake extensions

	if (length < sizeof(control))
		return;
	if ((size_t)length > capacity)
	{
		if (Datagram_RecordMalformedPacket(MALFORMED_PACKET_CONTROL_CAPACITY, length, (unsigned int)capacity))
			Con_DPrintf("Dropping control packet larger than its input buffer\n");
		return;
	}

	memcpy(&control, data, sizeof(control));	//data may be unaligned (dtls scratch buffer)
	control = BigLong(control);
	if (control == -1)
	{
		if (!sv_public.value)
			return;
		if ((size_t)length >= capacity)
		{
			if (Datagram_RecordMalformedPacket(MALFORMED_PACKET_TERMINATOR_HEADROOM, length, (unsigned int)capacity))
				Con_DPrintf("Dropping connectionless packet without terminator headroom\n");
			return;
		}
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
		const char	*prevKey;
		char		info[2048], key[64], val[256];

		prevKey = MSG_ReadString();

		// build the full info string and iterate it — includes both
		// CVAR_SERVERINFO cvars and svs.serverinfo keys (set by QC mods)
		Datagram_GenerateGetInfoString(info, sizeof(info));

		// send the response
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_RULE_INFO);
		if (Info_FindNextKey(info, prevKey, key, sizeof(key), val, sizeof(val)))
		{
			MSG_WriteString(&net_message, key);
			MSG_WriteString(&net_message, val);
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

/*
============
_Datagram_SweepLoopback		// woods #localscan

Queries the preferred loopback family across a port range so that servers on
non-default ports can answer. IPv4 is preferred by landriver order, with IPv6
as the fallback. Returns false if this landriver cannot form a loopback address.
============
*/
static qboolean _Datagram_SweepLoopback (void)
{
	struct qsockaddr base, addr;
	char		str[64];
	int			count = NET_LOCALSCAN_PORTS;
	int			i, port;
	qboolean	sent = false;

	if (!dfunc.GetAddrFromName || !dfunc.SetSocketPort)
		return false;

	// resolve the loopback address once, then just re-port it: this landriver
	// may only speak one family
	q_snprintf(str, sizeof(str), "127.0.0.1:%d", DEFAULTnet_hostport);
	if (dfunc.GetAddrFromName(str, &base) != 0)
	{
		q_snprintf(str, sizeof(str), "[::1]:%d", DEFAULTnet_hostport);
		if (dfunc.GetAddrFromName(str, &base) != 0)
			return false;
	}

	for (i = 0; i < count; i++)
	{
		port = DEFAULTnet_hostport + i;
		if (port > 65535)
			break;
		addr = base;
		if (dfunc.SetSocketPort(&addr, port) != 0)
			continue;
		_Datagram_SendServerQuery(&addr, false);
		sent = true;
	}

	// a non-default net_hostport is worth probing even when it sits outside the sweep
	port = net_hostport;
	if (port > 0 && port <= 65535 &&
		(port < DEFAULTnet_hostport || port >= DEFAULTnet_hostport + count))
	{
		addr = base;
		if (dfunc.SetSocketPort(&addr, port) == 0)
		{
			_Datagram_SendServerQuery(&addr, false);
			sent = true;
		}
	}

	return sent;
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

static qboolean Datagram_IsSupportedNetQuakeProtocol(int protocol)
{
	return protocol == PROTOCOL_NETQUAKE ||
		protocol == PROTOCOL_FITZQUAKE ||
		protocol == PROTOCOL_RMQ ||
		protocol == PROTOCOL_VERSION_BJP3 ||
		protocol == PROTOCOL_VERSION_DP7;
}

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

	/* "protocol" is the control protocol; "nqprotocol" is the game protocol. */
	supported = ((connectprotocol == NET_PROTOCOL_VERSION ||
		((t & PT_NETQUAKE) && Datagram_IsSupportedNetQuakeProtocol(connectprotocol))) &&
		(t & (PT_NETQUAKE | PT_DARKPLACES | PT_QUAKETV)));

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
		hostcache[n].addr = *readaddr;
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

/*
============
_Datagram_IsOwnServerReply	// woods #localmpfix

The listen socket uses net_hostport while discovery uses an ephemeral control
port, so comparing the complete endpoints never recognized our own reply.
Ignore only our address on our active server port; separate local servers on
other ports remain discoverable.
============
*/
static qboolean _Datagram_IsOwnServerReply (struct qsockaddr *readaddr,
	struct qsockaddr *myaddr)
{
	struct qsockaddr readhost, myhost;

	if (!sv.active || !dfunc.GetSocketPort || !dfunc.SetSocketPort || !dfunc.AddrCompare)
		return false;
	if (dfunc.GetSocketPort(readaddr) != net_hostport)
		return false;

	readhost = *readaddr;
	myhost = *myaddr;
	if (dfunc.SetSocketPort(&readhost, 0) != 0 ||
		dfunc.SetSocketPort(&myhost, 0) != 0)
		return false;

	return dfunc.AddrCompare(&readhost, &myhost) == 0;
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
		if (slistScope != SLIST_LOCAL) // woods #localscan -- a loopback sweep must not drag in known internet hosts
		{
			for (i = 0; i < hostlist_count; i++)
				hostlist[i].requery = true;
		}

		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREQ_SERVER_INFO);
		MSG_WriteString(&net_message, "QUAKE");
		MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));

		// woods #landedupe -- prefer IPv4 for LAN discovery. If IPv4 was
		// disabled or failed to initialize, the IPv6 landriver remains the fallback.
		if (slistScope != SLIST_INTERNET && slistScope != SLIST_LOCAL &&
			!(slistScope == SLIST_LAN && ipv4Available && myaddr.qsa_family == AF_INET6))
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

		// The loop driver already supplies the active listen server during a LAN search.
		if (_Datagram_IsOwnServerReply(&readaddr, &myaddr)) // woods #localmpfix
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

	if (!xmit && slistScope != SLIST_LOCAL) // woods #localscan -- keep stale requeries out of a loopback sweep
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
	qboolean sweep = (xmit && slistScope == SLIST_LOCAL); // woods #localscan
	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (hostCacheCount == HOSTCACHESIZE)
			break;
		if (net_landrivers[net_landriverlevel].initialized)
		{
			if (sweep && _Datagram_SweepLoopback())
			{	// one loopback family is enough; a second would only duplicate hits
				sweep = false;
				ret = true;
			}
			ret |= _Datagram_SearchForHosts (xmit);
		}
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

	newsock = NET_PortPingProbe_TakePendingSocket(net_landriverlevel, &probe_port_override);
	if (newsock != INVALID_SOCKET)
		Con_DPrintf("Port ping probe: reusing confirmed source port %d socket\n", probe_port_override);

	if (newsock == INVALID_SOCKET && probe_port_override > 0)
	{
		newsock = dfunc.Open_Socket(probe_port_override);
		if (newsock == INVALID_SOCKET)
			Con_DPrintf("Port ping probe: source port %d unavailable, falling back to OS-assigned source port\n", probe_port_override);
	}

	if (newsock == INVALID_SOCKET)
		newsock = dfunc.Open_Socket(0);

	if (newsock == INVALID_SOCKET)
		return NULL;

	sock = NET_NewQSocket ();
	if (sock == NULL)
		goto ErrorReturn2;
	sock->socket = newsock;
	sock->landriver = net_landriverlevel;
	q_strlcpy(sock->resolvedaddress, dfunc.AddrToString(serveraddr, false),
		sizeof(sock->resolvedaddress));

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
	qboolean probe_target_attempted;
	qboolean probe_fallback_started;
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
	NET_PortPingProbe_ClearPendingSocket();

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
	if (datagram_connect_ctx.probe_target_attempted &&
		!datagram_connect_ctx.probe_fallback_started)
	{
		/* The measured endpoint gets first choice, but a failed handshake must
		 * not suppress the ordinary IPv4/IPv6 fallback sequence. */
		datagram_connect_ctx.landriver = 0;
		datagram_connect_ctx.probe_fallback_started = true;
	}
	else
	{
		datagram_connect_ctx.landriver++;
	}
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
			if (NET_PortPingProbe_GetPendingTarget(&datagram_connect_ctx.landriver,
				&datagram_connect_ctx.serveraddr) &&
				net_landrivers[datagram_connect_ctx.landriver].initialized)
			{
				datagram_connect_ctx.probe_target_attempted = true;
				Con_DPrintf("Port ping probe: connecting to selected endpoint %s via %s\n",
					net_landrivers[datagram_connect_ctx.landriver].AddrToString(
						&datagram_connect_ctx.serveraddr, false),
					net_landrivers[datagram_connect_ctx.landriver].name);
				datagram_connect_ctx.phase = DATAGRAM_CONNECT_PHASE_OPEN_SOCKET;
				break;
			}
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
			datagram_connect_ctx.newsock = NET_PortPingProbe_TakePendingSocket(
				datagram_connect_ctx.landriver, &probe_port_override);
			if (datagram_connect_ctx.newsock != INVALID_SOCKET)
				Con_DPrintf("Port ping probe: reusing confirmed source port %d socket\n", probe_port_override);

			if (datagram_connect_ctx.newsock == INVALID_SOCKET && probe_port_override > 0)
			{
				datagram_connect_ctx.newsock = dfunc.Open_Socket(probe_port_override);
				if (datagram_connect_ctx.newsock == INVALID_SOCKET)
					Con_DPrintf("Port ping probe: source port %d unavailable, falling back to OS-assigned source port\n", probe_port_override);
			}

			if (datagram_connect_ctx.newsock == INVALID_SOCKET)
				datagram_connect_ctx.newsock = dfunc.Open_Socket(0);

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
			q_strlcpy(datagram_connect_ctx.sock->resolvedaddress,
				dfunc.AddrToString(&datagram_connect_ctx.serveraddr, false),
				sizeof(datagram_connect_ctx.sock->resolvedaddress));

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
				NET_PortPingProbe_ClearPendingSocket();
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
	int selected_landriver;

	NET_DatagramConnectCancel();

	host = Strip_Port (host);
	if (NET_PortPingProbe_GetPendingTarget(&selected_landriver, &addr) &&
		net_landrivers[selected_landriver].initialized)
	{
		net_landriverlevel = selected_landriver;
		resolved = true;
		Con_DPrintf("Port ping probe: connecting to selected endpoint %s via %s\n",
			dfunc.AddrToString(&addr, false), dfunc.name);
		ret = _Datagram_Connect(&addr);
	}

	if (ret)
	{
		NET_PortPingProbe_ClearPendingSocket();
		return ret;
	}

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
	NET_PortPingProbe_ClearPendingSocket();
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
