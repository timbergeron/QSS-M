/*
 * host_stress.c -- proposed extraction boundary for the QSS-M stress hooks.
 *
 * PORTABILITY DRAFT ONLY.  This file is intentionally not part of the QSS-M
 * build and is not an engine feature.  It documents where the current stress
 * block could move if we decide to maintain a dedicated stress target.
 *
 * ENTRY-POINT REGISTRY
 * --------------------
 * Host lifecycle crossings, currently called from Quake/host.c:
 *
 *   Host_StressInit()       one-time command registration
 *   Host_StressPoll()       append-only script pump, once per frame
 *   Host_StressPumpModal()  answer channel while a modal dialog owns input
 *
 * Parser/coverage crossings, currently called from parser code:
 *
 *   Host_StressNoteParse(readcount, cursize)  cl_parse.c invariant hook
 *   Host_StressCoverage(parser, id)           cl_parse.c/sv_user.c telemetry
 *
 * Commands registered by Host_StressInit():
 *
 *   _stress_key, _stress_char, _stress_status, _stress_inject
 *   _stress_parse_servermsg, _stress_parse_clientmsg
 *   _stress_replay_datagram, _stress_netstate, _stress_capabilities
 *
 * Network ownership boundary:
 *
 *   Datagram_StressReplayPacket() stays a small adapter in net_dgrm.c.  It
 *   must remain beside packetBuffer, Datagram_ProcessPacket(), and the
 *   connectionless control handler because those are private network internals.
 *   host_stress.c owns the command, callback, oracle, and coverage policy; it
 *   should not duplicate the netchan parser.
 *
 * BEHAVIORAL CONTRACTS
 * --------------------
 *
 *   Host_StressPumpModal() reads a dedicated <script>.answer channel and
 *   dispatches only _stress_key/_stress_char directly while SCR_ModalMessage
 *   owns the input grab.  It must not drain the ordinary script queue: the
 *   modal loop is a nested input loop and cannot wait for the normal frame
 *   pump to advance.
 *
 *   Port selection belongs to the harness/launcher, not this module.  The
 *   launcher passes -port before network initialization and may retry a busy
 *   port; stress hooks must not allocate sockets or assume the default port.
 *
 *   Oracle classification belongs to the harness.  In particular, a bare
 *   SZ_GetSpace: overflow is the handled allowoverflow path, while an
 *   unhandled or oversized write remains a failure.  This module should emit
 *   useful telemetry but should not classify console text on its own.
 *
 * FUTURE SPLIT CHECKLIST
 * ----------------------
 *   1. Move the STRESS TEST HOOKS block out of Quake/host.c.
 *   2. Make Host_StressInit/Host_StressPoll external host crossings.
 *   3. Add this module to a stress/debug Xcode target only.
 *   4. Keep QSSM_STRESS around every implementation and provide Release
 *      no-op declarations, so normal builds do not gain the commands.
 *   5. Leave the network adapter and parser call sites as narrow seams.
 *
 * Until that decision is made, the complete implementation remains in
 * Quake/host.c, Quake/net_dgrm.c, Quake/cl_parse.c, and Quake/sv_user.c.
 */

#include "quakedef.h"

/*
 * Public boundary used by the host loop and the two stress-specific call
 * sites.  Host_StressInit and Host_StressPoll replace the private functions
 * currently living in host.c; the other two already cross that boundary.
 */
void Host_StressInit (void);
void Host_StressPoll (void);
void Host_StressPumpModal (void);
void Host_StressNoteParse (int readcount, int cursize);
void Host_StressCoverage (const char *parser, unsigned int id);

/* Narrow network seam; implementation remains owned by net_dgrm.c. */
int Datagram_StressReplayPacket (const byte *data, size_t length,
                                 void (*callback)(qsocket_t *sock));

/*
 * REFERENCE-ONLY ADAPTER (not compiled)
 * -------------------------------------
 * This is the current net_dgrm.c implementation copied as line comments so
 * a future extraction has a concrete checklist.  Do not uncomment this in
 * host_stress.c: packetBuffer, Datagram_ProcessPacket(), and
 * _Datagram_ServerControlPacket() are private to net_dgrm.c.  Keeping this
 * reference beside the host-side entry-point registry makes the ownership
 * boundary obvious without creating a second network implementation.
 *
 * int Datagram_StressReplayPacket (const byte *data, size_t length,
 *                                  void (*callback)(qsocket_t *sock))
 * {
 *     client_t *client = NULL;
 *     qsocket_t *sock;
 *     struct qsockaddr clientaddr;
 *     byte control_data[PACKET_BUFFER_PARSE_CAPACITY];
 *     unsigned int control;
 *     int saved_landriverlevel;
 *     int i;
 *     int result;
 *
 *     if (!data || length < sizeof(control) ||
 *         length > PACKET_BUFFER_RECV_SIZE)
 *         return -1;
 *
 *     for (i = 0; i < svs.maxclients; i++)
 *         if (svs.clients[i].active && svs.clients[i].netconnection)
 *         {
 *             client = &svs.clients[i];
 *             break;
 *         }
 *     if (!client)
 *         return -2;
 *
 *     sock = client->netconnection;
 *     clientaddr = sock->addr;
 *     saved_landriverlevel = net_landriverlevel;
 *     net_landriverlevel = sock->landriver;
 *
 *     memcpy (&control, data, sizeof(control));
 *     control = BigLong(control);
 *     if ((control & (~NETFLAG_LENGTH_MASK)) == NETFLAG_CTL)
 *     {
 *         memcpy (control_data, data, length);
 *         control_data[length] = 0;
 *         _Datagram_ServerControlPacket (
 *             net_landrivers[net_landriverlevel].listeningSock,
 *             &clientaddr, control_data, (unsigned int)length,
 *             sizeof(control_data));
 *         result = (control == 0xffffffff ||
 *                   (control & NETFLAG_LENGTH_MASK) == length) ? 1 : 0;
 *     }
 *     else
 *     {
 *         memcpy (&packetBuffer, data, length);
 *         result = Datagram_ProcessPacket ((unsigned int)length, sock) ? 1 : 0;
 *         if (result && callback)
 *             callback (sock);
 *     }
 *
 *     net_landriverlevel = saved_landriverlevel;
 *     return result;
 * }
 */

typedef struct
{
	qboolean	active;
	char		scriptpath[MAX_OSPATH];
	char		answerpath[MAX_OSPATH];
	long		scriptofs;
	long		answerofs;
	double		nextpoll;

	/* Keep the queue private to this module; host.c should not know its size. */
	char		queue[65536];
	size_t		queuelen;
	int		seq_acked;
	int		seq_exec;
	int		parse_overreads;
	unsigned int	coverage_total;
} host_stress_state_t;

static host_stress_state_t stress;

/*
 * These helpers are the intended private implementation surface.  The
 * complete versions are currently in host.c and can be moved here without
 * changing their command protocol or the Python harness.
 */
static void Host_StressKey_f (void);
static void Host_StressChar_f (void);
static void Host_StressStatus_f (void);
static void Host_StressInject_f (void);
static char *Host_StressReadFile (const char *path, long *ofs);
static qboolean Host_StressBarrierMet (const char *condition);

void Host_StressInit (void)
{
	int i;

	/* Compile this module only into the stress/debug target. */
	i = COM_CheckParm ("-stress");
	if (!i)
		return;
	if (i + 1 >= com_argc || !com_argv[i + 1] || com_argv[i + 1][0] == '-')
	{
		Con_Printf ("-stress requires a script path\n");
		return;
	}

	q_strlcpy (stress.scriptpath, com_argv[i + 1],
		       sizeof (stress.scriptpath));
	q_snprintf (stress.answerpath, sizeof (stress.answerpath), "%s.answer",
		    stress.scriptpath);
	stress.scriptofs = 0;
	stress.answerofs = 0;
	stress.queuelen = 0;
	stress.seq_acked = 0;
	stress.seq_exec = 0;
	stress.parse_overreads = 0;
	stress.active = true;

	Cmd_AddCommand ("_stress_key", Host_StressKey_f);
	Cmd_AddCommand ("_stress_char", Host_StressChar_f);
	Cmd_AddCommand ("_stress_status", Host_StressStatus_f);
	Cmd_AddCommand ("_stress_inject", Host_StressInject_f);

	Con_Printf ("STRESS_READY %s\n", stress.scriptpath);
}

void Host_StressPoll (void)
{
	char *text;

	if (!stress.active)
		return;
	if (realtime < stress.nextpoll)
		return;
	stress.nextpoll = realtime + 0.02;

	/* Move the append-only reader and barrier-aware queue here. */
	text = Host_StressReadFile (stress.scriptpath, &stress.scriptofs);
	if (text)
	{
		/* The full implementation preserves whole lines and drains through
		 * Host_StressBarrierMet before appending commands to Cbuf. */
		Q_UNUSED (text);
	}
}

void Host_StressPumpModal (void)
{
	if (!stress.active)
		return;

	/* This is a priority answer channel, not the main script queue.  Preserve
	 * direct key/char dispatch while SCR_ModalMessage owns the input grab. */
	(void)Host_StressReadFile (stress.answerpath, &stress.answerofs);
}

void Host_StressNoteParse (int readcount, int cursize)
{
	if (!stress.active || readcount == cursize)
		return;

	stress.parse_overreads++;
	Con_Printf ("STRESS_PARSE %s readcount=%d cursize=%d\n",
		    readcount > cursize ? "overread" : "short",
		    readcount, cursize);
}

/* Private implementations moved from host.c in the eventual split. */
static void Host_StressKey_f (void) { /* move current implementation */ }
static void Host_StressChar_f (void) { /* move current implementation */ }
static void Host_StressStatus_f (void) { /* move current implementation */ }
static void Host_StressInject_f (void) { /* move current implementation */ }
static char *Host_StressReadFile (const char *path, long *ofs)
{
	Q_UNUSED (path);
	Q_UNUSED (ofs);
	return NULL; /* move current append-only reader */
}
static qboolean Host_StressBarrierMet (const char *condition)
{
	Q_UNUSED (condition);
	return true; /* move current barrier parser */
}
