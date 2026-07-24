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
#include "f_modified.h"
#include "cfgfile.h"
#include <setjmp.h>
#include "time.h" // woods #cfgbackup

#ifdef __APPLE__
#define Host_Setjmp(env) _setjmp(env)
#define Host_Longjmp(env, value) _longjmp(env, value)
#else
#define Host_Setjmp(env) setjmp(env)
#define Host_Longjmp(env, value) longjmp(env, value)
#endif

void URI_Init(void); // woods #uri
void URI_Shutdown(void); // woods #uri
void URI_Frame(void); // woods #uri

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
double		realtime;				// without any filtering or bounding
double		oldrealtime;			// last frame run

int		host_framecount;

int		host_hunklevel;

int		minimum_memory;

client_t	*host_client;			// current client

jmp_buf 	host_abortserver;

byte		*host_colormap;
float	host_netinterval = 1.0/72;
cvar_t	host_framerate = {"host_framerate","0",CVAR_NONE};	// set for slow motion
cvar_t	host_speeds = {"host_speeds","0",CVAR_NONE};			// set for running times
cvar_t	host_maxfps = {"host_maxfps", "250", CVAR_ARCHIVE}; //johnfitz
cvar_t	host_timescale = {"host_timescale", "0", CVAR_NONE}; //johnfitz
cvar_t	max_edicts = {"max_edicts", "15000", CVAR_NONE}; //johnfitz //ericw -- changed from 2048 to 8192, removed CVAR_ARCHIVE
cvar_t	cl_nocsqc = {"cl_nocsqc", "0", CVAR_NONE};	//spike -- blocks the loading of any csqc modules
cvar_t	sv_autosave = {"sv_autosave", "1", CVAR_ARCHIVE}; // woods #autosave (iw)
cvar_t	sv_autosave_interval = {"sv_autosave_interval", "30", CVAR_ARCHIVE}; // woods #autosave (iw)

cvar_t	sys_ticrate = {"sys_ticrate","0.05",CVAR_NOTIFY|CVAR_SERVERINFO}; // dedicated server -- woods
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
cvar_t	cl_migration_schema = {"cl_migration_schema","0",CVAR_ARCHIVE}; // woods #migration

devstats_t dev_stats, dev_peakstats;
overflowtimes_t dev_overflows; //this stores the last time overflow messages were displayed, not the last time overflows occured

extern cvar_t	sv_modvote;
extern cvar_t	pq_lag; // woods
extern char	lastmphost[NET_NAMELEN]; // woods - connected server address
extern char demoplaying[MAX_OSPATH]; // woods for window title

void SV_Next_Map_f(void); // woods #maprotation

#define NETFPS_PROBE_DEFAULT_SECONDS	10.0
#define NETFPS_PROBE_MIN_SECONDS		1.0
#define NETFPS_PROBE_MAX_SECONDS		30.0
#define NETFPS_PROBE_DEFAULT_TARGET		72.0
#define NETFPS_PROBE_MIN_TARGET			10.0
#define NETFPS_PROBE_MAX_TARGET			150.0
#define NETFPS_PROBE_MAX_SAMPLES		32768

typedef struct
{
	qboolean	active;
	qboolean	local_server;
	qboolean	remote_predinfo;
	qboolean	remote_pmove_seen;
	double		start_time;
	double		last_frame_time;
	double		duration;
	double		target_netfps;
	int			frames;
	int			intervals;
	double		sum_dt;
	double		min_dt;
	double		max_dt;
	int			clamped_intervals;
	int			sample_count;
	qboolean	sample_limit_hit;
	double		samples[NETFPS_PROBE_MAX_SAMPLES];
} netfps_probe_t;

typedef struct
{
	int			value;
	int			sends;
	double		effective_netfps;
	double		avg_send_dt;
	double		min_send_dt;
	double		max_send_dt;
	double		send_jitter;		// stddev of send spacing; low = smooth move cadence on the server
	double		min_window_netfps;	// worst ~1s window; how the value holds up during fps dips
	double		avg_late;
	double		max_late;
} netfps_probe_eval_t;

static netfps_probe_t netfps_probe;

static qboolean Host_NetfpsProbe_RemotePmove (void)
{
	/* A server emits a nonzero view-entity pmovetype only for its usingpmove
	 * path. Zero is inconclusive because MOVETYPE_NONE is also zero. */
	return !sv.active &&
		(cl.protocol_pext2 & PEXT2_PREDINFO) &&
		cl.entities && cl.viewentity > 0 && cl.viewentity < cl.num_entities &&
		cl.entities[cl.viewentity].netstate.pmovetype;
}

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
Host_NetfpsProbe

Samples rendered host-frame cadence and simulates the renderer/network
isolation accumulator to suggest a negative host_maxfps value.
================
*/
static void Host_NetfpsProbe_Usage (void)
{
	Con_Printf ("usage: netfps_probe [seconds] [target_netfps]\n");
	Con_Printf ("Defaults: %.0f seconds, %.0f target netfps.\n",
				NETFPS_PROBE_DEFAULT_SECONDS, NETFPS_PROBE_DEFAULT_TARGET);
	Con_Printf ("Run after connecting and entering a map, then move and fight normally.\n");
	Con_Printf ("Examples:\n");
	Con_Printf ("  netfps_probe          use the defaults\n");
	Con_Printf ("  netfps_probe 15       sample for 15 seconds\n");
	Con_Printf ("  netfps_probe 15 150   test a 150 netfps target for 15 seconds\n");
	Con_Printf ("  netfps_probe stop     stop an active probe\n");
}

static void Host_NetfpsProbe_Completion (const char *partial)
{
	if (Cmd_Argc () == 2)
	{
		Con_AddToTabList ("5", partial, "seconds", NULL);
		Con_AddToTabList ("10", partial, "seconds (default)", NULL);
		Con_AddToTabList ("15", partial, "seconds", NULL);
		Con_AddToTabList ("30", partial, "seconds (maximum)", NULL);
		Con_AddToTabList ("stop", partial, "stop active probe", NULL);
		Con_AddToTabList ("cancel", partial, "stop active probe", NULL);
		Con_AddToTabList ("help", partial, "show usage", NULL);
	}
	else if (Cmd_Argc () == 3)
	{
		Con_AddToTabList ("72", partial, "compatibility rate", NULL);
		Con_AddToTabList ("100", partial, "target netfps", NULL);
		Con_AddToTabList ("125", partial, "target netfps", NULL);
		Con_AddToTabList ("144", partial, "target netfps", NULL);
		Con_AddToTabList ("150", partial, "maximum target", NULL);
	}
}

static int Host_NetfpsProbe_CompareDouble (const void *a, const void *b)
{
	double da = *(const double *)a;
	double db = *(const double *)b;

	if (da < db)
		return -1;
	if (da > db)
		return 1;
	return 0;
}

static void Host_NetfpsProbe_Evaluate (int value, netfps_probe_eval_t *eval)
{
	double interval = 1.0 / value;
	double accum = 0.0;
	double total = 0.0;
	double send_sum = 0.0;
	double send_sq_sum = 0.0;
	double send_elapsed = 0.0;
	double late_sum = 0.0;
	double window_time = 0.0;
	int window_sends = 0;
	int i;

	memset (eval, 0, sizeof(*eval));
	eval->value = value;
	eval->min_window_netfps = -1.0;

	for (i = 0; i < netfps_probe.sample_count; i++)
	{
		double wall_dt = netfps_probe.samples[i];
		// the engine clamps each frame's accumulator contribution to 0.2s
		double accum_dt = q_min (wall_dt, 0.2);

		// Keep wall time separate: a hitch still delays the next move even
		// though the isolation accumulator advances by at most 0.2 seconds.
		total += wall_dt;
		send_elapsed += wall_dt;
		accum += accum_dt;
		window_time += wall_dt;
		if (accum >= interval)
		{
			double late = accum - interval;

			eval->sends++;
			window_sends++;
			send_sum += send_elapsed;
			send_sq_sum += send_elapsed * send_elapsed;
			late_sum += late;
			if (!eval->min_send_dt || send_elapsed < eval->min_send_dt)
				eval->min_send_dt = send_elapsed;
			if (send_elapsed > eval->max_send_dt)
				eval->max_send_dt = send_elapsed;
			if (late > eval->max_late)
				eval->max_late = late;
			accum = 0.0;
			send_elapsed = 0.0;
		}
		if (window_time >= 1.0)
		{
			// A single interval spanning a full second necessarily contains
			// a one-second window with no sends, even if one occurs at its end.
			double rate = (wall_dt >= 1.0) ? 0.0 : window_sends / window_time;
			if (eval->min_window_netfps < 0.0 || rate < eval->min_window_netfps)
				eval->min_window_netfps = rate;
			window_time = 0.0;
			window_sends = 0;
		}
	}
	if (window_time >= 0.5)
	{
		double rate = window_sends / window_time;
		if (eval->min_window_netfps < 0.0 || rate < eval->min_window_netfps)
			eval->min_window_netfps = rate;
	}

	if (total > 0.0)
		eval->effective_netfps = eval->sends / total;
	if (eval->min_window_netfps < 0.0)
		eval->min_window_netfps = eval->effective_netfps;
	if (eval->sends > 0)
	{
		double variance;

		eval->avg_send_dt = send_sum / eval->sends;
		eval->avg_late = late_sum / eval->sends;
		variance = send_sq_sum / eval->sends - eval->avg_send_dt * eval->avg_send_dt;
		eval->send_jitter = (variance > 0.0) ? sqrt (variance) : 0.0;
	}
}

static qboolean Host_NetfpsProbe_EvalBetter (const netfps_probe_eval_t *candidate, const netfps_probe_eval_t *best)
{
	if (!candidate->sends)
		return false;
	if (!best->sends)
		return true;
	// rate the candidate holds through fps dips beats a higher but flaky average
	if (candidate->min_window_netfps > best->min_window_netfps + 0.5)
		return true;
	if (candidate->min_window_netfps < best->min_window_netfps - 0.5)
		return false;
	if (candidate->effective_netfps > best->effective_netfps + 0.25)
		return true;
	if (candidate->effective_netfps < best->effective_netfps - 0.25)
		return false;
	// steadier send pacing is easier on the server than a marginally higher rate
	if (candidate->send_jitter < best->send_jitter - 0.0001)
		return true;
	if (candidate->send_jitter > best->send_jitter + 0.0001)
		return false;
	if (candidate->max_late < best->max_late - 0.00025)
		return true;
	if (candidate->max_late > best->max_late + 0.00025)
		return false;
	if (candidate->avg_late < best->avg_late - 0.00025)
		return true;
	if (candidate->avg_late > best->avg_late + 0.00025)
		return false;
	// prefer the value whose nominal rate matches what it actually delivers
	return candidate->value < best->value;
}

static qboolean Host_NetfpsProbe_EvalCloser (const netfps_probe_eval_t *candidate, const netfps_probe_eval_t *best, double target)
{
	double candidate_diff, best_diff;

	if (!candidate->sends)
		return false;
	if (!best->sends)
		return true;

	candidate_diff = fabs (candidate->effective_netfps - target);
	best_diff = fabs (best->effective_netfps - target);

	if (candidate_diff < best_diff - 0.25)
		return true;
	if (candidate_diff > best_diff + 0.25)
		return false;
	if (candidate->send_jitter < best->send_jitter - 0.0001)
		return true;
	if (candidate->send_jitter > best->send_jitter + 0.0001)
		return false;
	if (candidate->max_late < best->max_late - 0.00025)
		return true;
	if (candidate->max_late > best->max_late + 0.00025)
		return false;
	return candidate->value < best->value;
}

static void Host_NetfpsProbe_Report (void)
{
	double avg_dt, avgfps, minfps, maxfps, p95_dt = 0.0, p95fps = 0.0;
	double target;
	int target_value, safe_target, i;
	const netfps_probe_eval_t *recommendation;
	netfps_probe_eval_t requested, safe, closest, candidate;

	if (netfps_probe.intervals < 10 || netfps_probe.sum_dt <= 0.0 || netfps_probe.sample_count < 10)
	{
		Con_Printf ("netfps_probe: not enough frame samples; try again while the game is rendering normally.\n");
		return;
	}

	avg_dt = netfps_probe.sum_dt / netfps_probe.intervals;
	avgfps = netfps_probe.intervals / netfps_probe.sum_dt;
	minfps = (netfps_probe.max_dt > 0.0) ? 1.0 / netfps_probe.max_dt : 0.0;
	maxfps = (netfps_probe.min_dt > 0.0) ? 1.0 / netfps_probe.min_dt : 0.0;
	target = CLAMP (NETFPS_PROBE_MIN_TARGET, netfps_probe.target_netfps, NETFPS_PROBE_MAX_TARGET);
	target_value = CLAMP ((int)NETFPS_PROBE_MIN_TARGET, Q_rint(target), (int)NETFPS_PROBE_MAX_TARGET);
	safe_target = q_min (target_value, 72);

	Host_NetfpsProbe_Evaluate (target_value, &requested);
	memset (&safe, 0, sizeof(safe));
	for (i = (int)NETFPS_PROBE_MIN_TARGET; i <= safe_target; i++)
	{
		Host_NetfpsProbe_Evaluate (i, &candidate);
		if (Host_NetfpsProbe_EvalBetter (&candidate, &safe))
			safe = candidate;
	}
	memset (&closest, 0, sizeof(closest));
	for (i = (int)NETFPS_PROBE_MIN_TARGET; i <= (int)NETFPS_PROBE_MAX_TARGET; i++)
	{
		Host_NetfpsProbe_Evaluate (i, &candidate);
		if (Host_NetfpsProbe_EvalCloser (&candidate, &closest, target))
			closest = candidate;
	}

	qsort (netfps_probe.samples, netfps_probe.sample_count, sizeof(netfps_probe.samples[0]), Host_NetfpsProbe_CompareDouble);
	p95_dt = netfps_probe.samples[(int)(0.95 * (netfps_probe.sample_count - 1))];
	if (p95_dt > 0.0)
		p95fps = 1.0 / p95_dt;

	Con_Printf ("\nnetfps_probe result\n");
	Con_Printf ("netfps_probe: sampled %.1f seconds, %d frames.\n",
				netfps_probe.sum_dt, netfps_probe.frames);
	Con_Printf ("netfps_probe: render avg %.1f fps (%.2f ms).\n",
				avgfps, avg_dt * 1000.0);
	Con_Printf ("netfps_probe: render floor %.1f fps 95%%, worst %.1f fps, peak %.1f fps.\n",
				p95fps, minfps, maxfps);
	if (netfps_probe.local_server)
		Con_Printf ("netfps_probe: mode: local/listen server; netfps is also the server physics rate.\n");
	else if (netfps_probe.remote_pmove_seen)
		Con_Printf ("netfps_probe: mode: remote server with per-move player processing confirmed.\n");
	else if (netfps_probe.remote_predinfo)
	{
		Con_Printf ("netfps_probe: mode: remote server with prediction protocol, but per-move processing was not observed.\n");
		Con_Printf ("netfps_probe: note: probe while actively spawned if you expected per-move processing.\n");
	}
	else
		Con_Printf ("netfps_probe: mode: remote server; per-move processing could not be confirmed.\n");

	Con_Printf ("\nnetfps_probe: requested host_maxfps -%d estimates %.1f netfps (%.1f in worst second).\n",
				target_value, requested.effective_netfps, requested.min_window_netfps);
	Con_Printf ("netfps_probe: requested avg send %.2f ms, jitter %.2f ms, max late %.2f ms.\n",
				requested.avg_send_dt * 1000.0, requested.send_jitter * 1000.0, requested.max_late * 1000.0);
	if (requested.effective_netfps < target - 0.5)
		Con_Printf ("netfps_probe: note: the sampled render cadence cannot sustain %.1f netfps; host_maxfps -%d is estimated at %.1f netfps.\n",
				target, target_value, requested.effective_netfps);

	Con_Printf ("\n");
	Con_Printf ("netfps_probe: conservative command: host_maxfps -%d\n", safe.value);
	Con_Printf ("netfps_probe: conservative estimate: %.1f netfps (%.1f in worst second).\n",
				safe.effective_netfps, safe.min_window_netfps);
	Con_Printf ("netfps_probe: conservative avg send %.2f ms, jitter %.2f ms, max late %.2f ms.\n",
				safe.avg_send_dt * 1000.0, safe.send_jitter * 1000.0, safe.max_late * 1000.0);
	if (closest.value != safe.value)
	{
		Con_Printf ("\n");
		Con_Printf ("netfps_probe: closest-to-target command: host_maxfps -%d\n", closest.value);
		Con_Printf ("netfps_probe: closest estimate: %.1f netfps (%.1f in worst second).\n",
					closest.effective_netfps, closest.min_window_netfps);
		Con_Printf ("netfps_probe: closest avg send %.2f ms, jitter %.2f ms, max late %.2f ms.\n",
					closest.avg_send_dt * 1000.0, closest.send_jitter * 1000.0, closest.max_late * 1000.0);
		if (closest.value > 72)
		{
			if (netfps_probe.local_server)
				Con_Printf ("netfps_probe: note: above 72 changes local server physics; use conservative for compatibility.\n");
			else if (netfps_probe.remote_pmove_seen)
				Con_Printf ("netfps_probe: note: the server reports per-move processing; a higher rate does not change local physics, but increases packet traffic and may be limited by server policy.\n");
			else
				Con_Printf ("netfps_probe: note: without confirmed per-move processing, a quick button tap can be overwritten when multiple moves arrive before one physics tick; use conservative for compatibility.\n");
		}
	}
	if (safe.effective_netfps < target - 0.5)
	{
		Con_Printf ("\n");
		Con_Printf ("netfps_probe: note: values up to 72 cannot cleanly reach %.1f netfps with this render cadence.\n", target);
	}
	if (netfps_probe.sample_limit_hit)
		Con_Printf ("netfps_probe: note: sample buffer filled; result uses the first %d intervals.\n", NETFPS_PROBE_MAX_SAMPLES);
	if (netfps_probe.clamped_intervals)
		Con_Printf ("netfps_probe: note: %d frame %s exceeded 200 ms; the recommendation includes %s.\n",
					netfps_probe.clamped_intervals,
					netfps_probe.clamped_intervals == 1 ? "interval" : "intervals",
					netfps_probe.clamped_intervals == 1 ? "it" : "them");
	recommendation = (!netfps_probe.local_server && netfps_probe.remote_pmove_seen) ? &closest : &safe;
	Con_Printf ("\nnetfps_probe: final recommendation: ^mhost_maxfps^m -^g%d^d%s\n",
				recommendation->value,
				recommendation == &closest && closest.value > 72 ? " (remote low-latency mode)" : "");
	Con_Printf ("netfps_probe: final estimate: %.1f netfps (%.1f in worst second).\n",
				recommendation->effective_netfps, recommendation->min_window_netfps);
	Con_Printf ("\n");
}

static void Host_NetfpsProbe_Frame (void)
{
	double now, dt;

	if (!netfps_probe.active)
		return;
	if (cls.state != ca_connected || cls.signon != SIGNONS || cls.demoplayback || cls.timedemo)
	{
		netfps_probe.active = false;
		Con_Printf ("netfps_probe: canceled because the connection or map changed.\n");
		return;
	}

	now = realtime;
	if (Host_NetfpsProbe_RemotePmove ())
		netfps_probe.remote_pmove_seen = true;
	if (netfps_probe.last_frame_time > 0.0)
	{
		dt = now - netfps_probe.last_frame_time;
		if (dt > 0.0)
		{
			netfps_probe.intervals++;
			netfps_probe.sum_dt += dt;
			netfps_probe.samples[netfps_probe.sample_count++] = dt;
			if (dt > 0.2)
				netfps_probe.clamped_intervals++;
			if (!netfps_probe.min_dt || dt < netfps_probe.min_dt)
				netfps_probe.min_dt = dt;
			if (dt > netfps_probe.max_dt)
				netfps_probe.max_dt = dt;
		}
	}
	netfps_probe.last_frame_time = now;
	netfps_probe.frames++;

	if (netfps_probe.sample_count >= NETFPS_PROBE_MAX_SAMPLES ||
		now - netfps_probe.start_time >= netfps_probe.duration)
	{
		netfps_probe.sample_limit_hit = netfps_probe.sample_count >= NETFPS_PROBE_MAX_SAMPLES;
		netfps_probe.active = false;
		Host_NetfpsProbe_Report ();
	}
}

static void Host_NetfpsProbe_f (void)
{
	int argc = Cmd_Argc ();
	double duration = NETFPS_PROBE_DEFAULT_SECONDS;
	double target = NETFPS_PROBE_DEFAULT_TARGET;

	if (argc >= 2 &&
		(!q_strcasecmp (Cmd_Argv(1), "stop") ||
		 !q_strcasecmp (Cmd_Argv(1), "cancel")))
	{
		if (netfps_probe.active)
		{
			netfps_probe.active = false;
			Con_Printf ("netfps_probe: stopped.\n");
		}
		else
			Con_Printf ("netfps_probe: not running.\n");
		return;
	}

	if (argc > 3 ||
		(argc >= 2 && (!q_strcasecmp (Cmd_Argv(1), "?") ||
					   !q_strcasecmp (Cmd_Argv(1), "help"))))
	{
		Host_NetfpsProbe_Usage ();
		return;
	}

	if (cls.state != ca_connected || cls.signon != SIGNONS || cls.demoplayback || cls.timedemo)
	{
		Con_Printf ("netfps_probe: run this after connecting and entering a map.\n");
		if (cls.demoplayback || cls.timedemo)
			Con_Printf ("netfps_probe: demo playback/timedemo timing is not useful for netfps calibration.\n");
		Host_NetfpsProbe_Usage ();
		return;
	}

	if (argc >= 2)
		duration = Q_atof (Cmd_Argv(1));
	if (argc >= 3)
		target = Q_atof (Cmd_Argv(2));

	if (!isfinite (duration) || !isfinite (target) || duration <= 0.0 || target <= 0.0)
	{
		Host_NetfpsProbe_Usage ();
		return;
	}

	memset (&netfps_probe, 0, sizeof(netfps_probe));
	netfps_probe.active = true;
	netfps_probe.local_server = sv.active;
	netfps_probe.remote_predinfo = !sv.active && (cl.protocol_pext2 & PEXT2_PREDINFO);
	netfps_probe.remote_pmove_seen = Host_NetfpsProbe_RemotePmove ();
	netfps_probe.start_time = realtime;
	netfps_probe.duration = CLAMP (NETFPS_PROBE_MIN_SECONDS, duration, NETFPS_PROBE_MAX_SECONDS);
	netfps_probe.target_netfps = CLAMP (NETFPS_PROBE_MIN_TARGET, target, NETFPS_PROBE_MAX_TARGET);

	Con_Printf ("netfps_probe: sampling %.1f seconds for %.1f target netfps\n",
				netfps_probe.duration, netfps_probe.target_netfps);
	Con_Printf ("netfps_probe: move and fight normally while sampling; an idle probe overestimates what your fps can sustain.\n");
	if (host_maxfps.value > 0)
		Con_Printf ("netfps_probe: host_maxfps is currently capping render fps; this will be included in the measurement.\n");
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

	Host_Longjmp (host_abortserver, 1);
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
	cl.qcvm.extfuncs.CSQC_UpdateView = 0; //its going down. don't let it incercept any of the dumb prints etc here.
	cl.qcvm.extfuncs.CSQC_Shutdown = 0;	//also a common cause of recursive errors. don't give it a chance.
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

	CL_ClearState ();	//spike: stuff died. clean it up. mostly doing this to strip away any csqc still execing.

	inerror = false;

	Host_Longjmp (host_abortserver, 1);
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

#define HOST_VERSION_GITHUB_TIMEOUT_MS 2000

typedef struct
{
    int section;
} host_version_print_t;

static void Host_Version_PrintLocal(versionsection_t section, const char* label,
    const char* value, void* userdata)
{
    host_version_print_t* print = (host_version_print_t*)userdata;

    if (print->section != section)
    {
        Con_Printf("\n^m%s^m\n\n", M_Version_SectionName(section));
        print->section = section;
    }

    Con_Printf("%-24s %s\n", label, value);
}

void Host_Version_f(void)
{
	versionremoteinfo_t release;
	versionremoteinfo_t commit;
	qboolean github_complete;
    host_version_print_t print = {-1};
    char release_text[160];
    char commit_text[160];

	github_complete = M_Version_WaitForGitHubInfo(&release, &commit, HOST_VERSION_GITHUB_TIMEOUT_MS);
    M_Version_EnumerateLocal(Host_Version_PrintLocal, &print);

	Con_Printf("\n^mGitHub QSS-M Versions^m\n\n");
    M_Version_FormatRemoteInfo(&release, false,
        github_complete ? "checking..." : "timeout", release_text, sizeof(release_text));
    M_Version_FormatRemoteInfo(&commit, true,
        github_complete ? "checking..." : "timeout", commit_text, sizeof(commit_text));
    Con_Printf("%-24s %s\n", "Latest release", release_text);
    Con_Printf("%-24s %s\n", "Latest commit", commit_text);

	Con_Printf("\n");
}

/* cvar callback functions : */
void Host_Callback_Notify (cvar_t *var)
{
	if (sv.active)
		SV_BroadcastPrintf ("\"%s\" changed to \"%s\"\n", var->name, var->string);
}

static void Host_Modvote_GateChanged(cvar_t *var)
{
	if (sv_modvote.value < 1.0f || coop.value < 1.0f)
		Host_Modvote_Reset();
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
Startup_Place -- woods #onload (inspired by ezquake)

Customize the initial behavior of the game client based on user
preferences stored in cl_onload
===============
*/
void Startup_Place (void)
{
	extern cvar_t cl_onload;
	extern cvar_t cl_demoreel;
	const char* cmd = cl_onload.string;

	// Early return for empty or default "menu" command
	if (!cmd[0] || !q_strcasecmp(cmd, "menu"))
		return;

	// Define blocked commands that could cause loops or crashes
	const char* blocked_cmds[] = {
		"quit",
		"startup",
		NULL
	};

	// Check for blocked commands
	for (int i = 0; blocked_cmds[i]; i++) {
		if (!q_strcasecmp(cmd, blocked_cmds[i])) {
			Con_DPrintf("cl_onload: command '%s' is not allowed\n", blocked_cmds[i]);
			return;
		}
	}

	// Define command mappings
	struct {
		const char* name;
		const char* command;
	} command_map[] = {
		{"browser", "menu_slist"},
		{"bookmarks", "menu_bookmarks"},
		{"save", "menu_load"},
		{"history", "menu_history"},
		{NULL, NULL}
	};

	// Check for special commands first
	if (!q_strcasecmp(cmd, "console")) {
		key_dest = key_console;
		return;
	}
	if (!q_strcasecmp(cmd, "demo")) {
		key_dest = (cl_demoreel.value) ? key_game : key_menu;
		return;
	}

	// Look up command in mapping table
	for (int i = 0; command_map[i].name != NULL; i++) {
		if (!q_strcasecmp(cmd, command_map[i].name)) {
			Cbuf_AddText(va("%s\n", command_map[i].command));
			return;
		}
	}

	// Handle command with potential arguments
	const char* space = strchr(cmd, ' ');
	if (space) {
		// We have a command with arguments
		char command[128];
		int cmdlen = space - cmd;

		if (cmdlen < sizeof(command)) {
			memcpy(command, cmd, cmdlen);
			command[cmdlen] = '\0';

			// Check if the first word is a valid command
			if (Cmd_Exists(command)) {
				Cbuf_AddText(va("%s\n", cmd));  // Use full command string with args
				return;
			}
			key_dest = key_console;
			Con_DPrintf("cl_onload command does not exist: %s\n", command);
			return;
		}
	}

	// Handle single word command
	if (Cmd_Exists(cmd)) {
		Cbuf_AddText(va("%s\n", cmd));
		return;
	}

	// Command not found
	key_dest = key_console;
	Con_DPrintf("cl_onload command does not exist: %s\n", cmd);
}

/*
===============
Host_Startup_f -- woods #onload
===============
*/
void Host_Startup_f (void)
{
	Startup_Place ();
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
	Cmd_AddCommand ("startup", Host_Startup_f); // woods #onload
	Cmd_AddCommand ("host_cvar_migrate", Host_RunCvarMigrations); // woods #migration
	Cmd_AddCommand ("netfps_probe", Host_NetfpsProbe_f);
	{
		cmd_function_t *netfps_probe_cmd = Cmd_FindCommand ("netfps_probe");
		if (netfps_probe_cmd)
			netfps_probe_cmd->completion = Host_NetfpsProbe_Completion;
	}

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
	Cvar_SetCallback (&sys_ticrate, Host_Callback_Notify); // woods
	Cvar_RegisterVariable (&sys_throttle);
	Cvar_RegisterVariable (&sys_dedmouse_capture);
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
	Cvar_RegisterVariable (&sv_modvote);
	Cvar_SetCallback (&coop, Host_Modvote_GateChanged);
	Cvar_SetCallback (&sv_modvote, Host_Modvote_GateChanged);

	Cvar_RegisterVariable (&campaign);
	Cvar_RegisterVariable (&horde);
	Cvar_RegisterVariable (&sv_cheats);
	Cvar_RegisterVariable (&cl_migration_schema); // woods #migration

	Cvar_RegisterVariable (&pausable);

	Cvar_RegisterVariable (&temp1);

	Host_FindMaxClients ();

	host_time = 1.0;		// so a think at time 0 won't get called // woods #smoothcam
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
	char	write_name[MAX_QPATH];
	char	config_dir[MAX_OSPATH];
	char	fullpath[MAX_OSPATH];

// dedicated servers initialize the host but don't parse and set the
// config.cfg cvars
	if (host_initialized && !isDedicated && !host_parms->errstate)
	{
		COM_ConfigFileEffectivePath(name, write_name, sizeof(write_name));
		if (!q_strncasecmp(write_name, "configs/", 8))
		{
			q_snprintf(config_dir, sizeof(config_dir), "%s/configs", com_gamedir);
			Sys_mkdir(config_dir);
		}

		q_snprintf(fullpath, sizeof(fullpath), "%s/%s", com_gamedir, write_name);
		f = fopen (fullpath, "w");
		if (!f)
		{
			Con_Printf ("Couldn't write %s.\n", write_name);
			return;
		}

		//VID_SyncCvars (); //johnfitz -- write actual current mode to config file, in case cvars were messed with

		Config_PrintPreamble(f);

		if (cfg_save_aliases.value) // woods #serveralias
		{ 
			Config_PrintHeading(f, "A L I A S E S");
			Alias_WriteAliases(f);
		}
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

		Con_SafePrintf("Wrote ");
		Con_LinkPrintf(fullpath, "%s", write_name);
		Con_SafePrintf(".\n");
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
	char	config_name[MAX_QPATH];
	
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

		COM_ConfigFileEffectivePath("config.cfg", config_name, sizeof(config_name));
		f = fopen(va("%s/%s", com_gamedir, config_name), "r");

		if (f)
		{
			fclose(f);
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

		if (cfg_save_aliases.value) // woods #serveralias
		{
			Config_PrintHeading(f, "A L I A S E S");
			Alias_WriteAliases(f);
		}

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
	int		client_index;
	client_t *client;

	client_index = (int)(host_client - svs.clients);
	Host_Modvote_RemoveClientVote(client_index);

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
	host_client->desired_name[0] = 0; // woods #dupnames - clear preferred name
	if (sv.active)
		SV_ReapplyPreferredNames(host_client); // woods #dupnames - let others reclaim names
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
		NET_GetServerMessages(NULL);	//read packets to make sure we're receiving their acks. we're going to drop them all so we don't actually care to read the data, just the acks so we can flush our outgoing properly.
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
	extern edict_t *bbox_focus;
	extern void SCR_ClearShowFieldsTracks(void);

	CL_AsyncDownload_Cancel();
	Host_WaitForSaveThread();

	if (cl.qcvm.extfuncs.CSQC_Shutdown)
	{
		PR_SwitchQCVM(&cl.qcvm);
		PR_ExecuteProgram(qcvm->extfuncs.CSQC_Shutdown);
		qcvm->extfuncs.CSQC_Shutdown = 0;
		PR_SwitchQCVM(NULL);
	}

	bbox_focus = NULL;
	SCR_ClearShowFieldsTracks();

	Con_DPrintf ("Clearing memory\n");
	D_FlushCaches ();
	Mod_ClearAll ();
	Sky_NewMapClear(); // keeps the loaded skybox for reuse; full purge happens on game change
	S_ClearPrecache ();
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
	realtime += time;

	//johnfitz -- max fps cvar
	if ((host_maxfps.value>0 || cls.state == ca_disconnected) && !cls.timedemo)
	{
		float maxfps;
		if (cls.state == ca_disconnected)
		{
			maxfps = vid.refreshrate ? vid.refreshrate : 60.f;
			if (host_maxfps.value>0)
				maxfps = q_min (maxfps, host_maxfps.value);
			maxfps = CLAMP (10.f, maxfps, 5000.0); // woods higher max
		}
		else
		{
			maxfps = CLAMP (10.f, host_maxfps.value, 5000.0); // woods higher max
		}

		if (realtime - oldrealtime < 1.0/maxfps)
			return false; // framerate is too high
	}
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
		Cbuf_AddText ("\n");
	}
}

/*
==================
Host_CheckAutosave
==================
*/
static void Host_CheckAutosave (void)
{
	float	health_change, speed, elapsed, interval, score;

	interval = fabsf(sv_autosave_interval.value);
	if (!sv_autosave.value || interval <= 0.f || svs.maxclients != 1 || !svs.clients ||
		!svs.clients[0].active || !svs.clients[0].spawned || !sv_player || sv_player->free ||
		sv_player->v.health <= 0.f || cl.intermission)
		return;

	// woods -- don't autosave on deathmatch maps (no monsters/secrets, or known CTF maps);
	// same singleplayer-content test the solo scoreboard uses to show skill/kills/secrets
	if (!Host_MapHasLevelStats (cl.mapname, cl.stats[STAT_TOTALMONSTERS], cl.stats[STAT_TOTALSECRETS]))
		return;

	if (sv.nomonsters)
	{
		if (!sv.autosave.nomonsters_warned)
		{
			Con_Printf ("Can't save when using \"nomonsters\".\n");
			sv.autosave.nomonsters_warned = true;
		}
		return;
	}

	if (cls.signon == SIGNONS)
	{
		if (pr_global_struct->found_secrets != sv.autosave.prev_secrets)
		{
			sv.autosave.prev_secrets = pr_global_struct->found_secrets;
			sv.autosave.secret_boost = 1.f;
		}
		else
			sv.autosave.secret_boost = q_max (0.f, sv.autosave.secret_boost - host_frametime / 1.5f);
	}

	if (!sv.autosave.prev_health)
		sv.autosave.prev_health = sv_player->v.health;
	health_change = sv_player->v.health - sv.autosave.prev_health;
	if (health_change < 0.f)
		if (health_change < -3.f || sv_player->v.health < 100.f || sv_player->v.watertype == CONTENTS_SLIME || sv_player->v.watertype == CONTENTS_LAVA)
			sv.autosave.hurt_time = qcvm->time;
	sv.autosave.prev_health = sv_player->v.health;

	if (sv_player->v.button0)
		sv.autosave.shoot_time = qcvm->time;

	if (sv_player->v.movetype == MOVETYPE_NOCLIP || (int)sv_player->v.flags & (FL_GODMODE|FL_NOTARGET))
	{
		sv.autosave.cheat += host_frametime;
		return;
	}

	if (qcvm->time - sv.autosave.hurt_time < 3.f)
		return;

	if (qcvm->time - sv.autosave.shoot_time < 3.f)
		return;

	speed = VectorLength (sv_player->v.velocity);
	if (speed > 100.f)
		return;

	if ((int)sv_player->v.movetype == MOVETYPE_NONE)
		return;

	elapsed = qcvm->time - sv.autosave.time - sv.autosave.cheat;
	if (elapsed < 3.f)
		return;

	score = elapsed / interval;
	score *= q_min (100.f, (sv_player->v.health + sv_player->v.armortype * sv_player->v.armorvalue)) / 100.f;
	score += q_max (0.f, health_change) / 100.f;
	score -= (speed / 100.f) * 0.25f;
	score += sv.autosave.secret_boost * 0.25f;
	score += CLAMP (0.f, 1.f - (qcvm->time - sv_player->v.teleport_time) / 1.5f, 1.f) * 0.5f;

	if (score < 1.f)
		return;

	sv.autosave.time = qcvm->time;
	sv.autosave.cheat = 0;
	Cbuf_AddText (va ("save \"autosave/%s\" 0\n", sv.name));
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

	if (sv.active) // woods #svtimer
		SV_ProcessTimerExecution();

// set the time and clear the general datagram
	SV_ClearDatagram ();

//respond to cvar changes
	PMSV_UpdateMovevars ();

// check for new clients
	SV_CheckForNewClients ();

// read client messages
	SV_RunClients ();
	Host_Modvote_UpdateJoinMotd();

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

	Host_CheckAutosave ();
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
	char		server[MAX_SERVER_ADDRESS_LEN];
	int			players;	// woods connected player count for (x/y) window title
} summary_t;

/*
==================
GetGameSummary - github.com/andrei-drexler/ironwail (Show game summary in window title)
==================
*/
static void GetGameSummary(summary_t* s)
{
	s->players = 0; // woods
	if (cls.state != ca_connected || cls.signon != SIGNONS)
	{
		s->map[0] = 0;
		s->server[0] = 0;
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
		if (cl.gametype == GAME_DEATHMATCH && !cls.demoplayback)
		{
			int i;
			NET_HostnameCache_FormatDisplay(lastmphost, net_hostport,
				s->server, sizeof(s->server));
			for (i = 0; i < cl.maxclients; i++) // woods count connected players
				if (cl.scores && cl.scores[i].name[0])
					s->players++;
		}
		else
			s->server[0] = 0;
	}
}

static qboolean Summary_IsKnownCTFMap (const char *mapname)
{
	if (!mapname)
		return false;

	if (!q_strncasecmp (mapname, "ctf", 3) &&
		mapname[3] >= '1' && mapname[3] <= '8' &&
		mapname[4] == '\0')
		return true;

	return !q_strncasecmp (mapname, "ctf2m", 5) &&
		mapname[5] >= '1' && mapname[5] <= '8' &&
		mapname[6] == '\0';
}

qboolean Host_MapHasLevelStats (const char *mapname, int total_monsters, int total_secrets)
{
	// Known CTF maps are multiplayer maps, so don't show single-player stats
	// even when the BSP contains monsters or secrets.
	if (Summary_IsKnownCTFMap (mapname))
		return false;

	return total_monsters > 0 || total_secrets > 0;
}

static qboolean Summary_HasLevelStats (const summary_t *s)
{
	return Host_MapHasLevelStats (s->map, s->stats.total_monsters, s->stats.total_secrets);
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
		if (!strcmp(current.map, last.map) && !strcmp(current.server, last.server) &&
			current.players == last.players && // woods refresh on player join/leave
			!memcmp(&current.stats, &last.stats, sizeof(current.stats)))
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
{
    if (ln[0] != '\0' && Q_strcmp(ln, current.map) != 0)
        q_snprintf(title, sizeof(title), "%s (%d/%d)  |  %s (%s)  -  " ENGINE_NAME_AND_VER, current.server, current.players, cl.maxclients, ln, current.map);
    else
        q_snprintf(title, sizeof(title), "%s (%d/%d)  |  %s  -  " ENGINE_NAME_AND_VER, current.server, current.players, cl.maxclients, current.map);
}
else if (cls.demoplayback) // woods added demofile
{
    if (ln[0] != '\0' && Q_strcmp(ln, current.map) != 0)
        q_snprintf(title, sizeof(title), "%s (%s)  |  %s  -  " ENGINE_NAME_AND_VER, ln, current.map, demoplaying);
    else
        q_snprintf(title, sizeof(title), "%s  |  %s  -  " ENGINE_NAME_AND_VER, current.map, demoplaying);
}
else
{
    if (Summary_HasLevelStats(&current))
    {
        if (ln[0] != '\0')
            q_snprintf(title, sizeof(title),
                "%s (%s)  |  skill %d  |  %d/%d kills  |  %d/%d secrets  -  " ENGINE_NAME_AND_VER,
                ln, current.map,
                current.stats.skill,
                current.stats.monsters, current.stats.total_monsters,
                current.stats.secrets, current.stats.total_secrets
            );
        else
            q_snprintf(title, sizeof(title),
                "%s  |  skill %d  |  %d/%d kills  |  %d/%d secrets  -  " ENGINE_NAME_AND_VER,
                current.map,
                current.stats.skill,
                current.stats.monsters, current.stats.total_monsters,
                current.stats.secrets, current.stats.total_secrets
            );
    }
    else if (ln[0] != '\0' && Q_strcmp(ln, current.map) != 0)
        q_snprintf(title, sizeof(title),
            "%s (%s)  -  " ENGINE_NAME_AND_VER,
            ln, current.map
        );
    else
        q_snprintf(title, sizeof(title),
            "%s  -  " ENGINE_NAME_AND_VER,
            current.map
        );
}
VID_SetWindowTitle(title);
	}
	else
	{
		VID_SetWindowTitle(ENGINE_NAME_AND_VER);
	}
}

/*
==================
Host_UpdateDockBadge - woods

Reflect download and port-ping-probe activity in the platform shell UI: a
Chrome-style ring on the macOS Dock icon, or the native Windows taskbar
progress strip. Downloads take priority if both are active. Clears once the
activity finishes. No-op on platforms without support.
==================
*/
static void Host_UpdateDockBadge(void)
{
	extern qboolean curl_download_active;	// cl_main.c #webdl
	static int last = -1;	// last percent sent (-1 = hidden / not downloading)
	static qboolean last_port_probe = false;
	qboolean port_probe = false;
	int pct;

	if (cls.download.active || curl_download_active)
	{
		float p = cls.download.percent;
		pct = (p >= 0.f && p <= 100.f) ? (int)(p + 0.5f) : 0;	// 0 = active, size unknown
	}
	else if (NET_PortPingProbe_GetStatus() == PORTPINGPROBE_PROBING)
	{
		pct = NET_PortPingProbe_GetProgress();	// 0 = active, no probe completed yet
		port_probe = true;
	}
	else
		pct = -1;	// no shell-visible background activity

	if (pct > 100)
		pct = 100;

	if (pct != last || port_probe != last_port_probe)	// only touch the dock tile when the display changes
	{
		last = pct;
		last_port_probe = port_probe;
		Sys_SetDockProgress(pct < 0 ? -1.f : pct / 100.f, port_probe);
	}
}

//used for cl.qcvm.GetModel (so ssqc+csqc can share builtins)
qmodel_t *CL_ModelForIndex(int index)
{
	if (index < 0 || index >= MAX_MODELS)
		return NULL;
	return cl.model_precache[index];
}

static qboolean CL_QueueClientStringCommand(const char *command)
{
	size_t needed;

	if (cls.state != ca_connected)
		return false;
	needed = 1 + strlen(command) + 1;
	if (cls.message.cursize > cls.message.maxsize)
		return false;
	if (needed > (size_t)(cls.message.maxsize - cls.message.cursize))
		return false;

	MSG_WriteByte(&cls.message, clc_stringcmd);
	MSG_WriteString(&cls.message, command);
	return true;
}

static qboolean CL_LoadCSProgs(void)
{
	qboolean fullcsqc = false;
	int i;
	PR_ClearProgs(&cl.qcvm);
	PR_SwitchQCVM(&cl.qcvm);
	if (pr_checkextension.value && !cl_nocsqc.value)
	{	//only try to use csqc if qc extensions are enabled.
		char versionedname[MAX_QPATH];
		char specifiedname[MAX_QPATH];
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

		val = Info_GetKey(cl.serverinfo, "*csprogsname", specifiedname, sizeof(specifiedname));

		//try csprogs.dat first, then fall back on progs.dat in case someone tried merging the two.
		//we only care about it if it actually contains a CSQC_DrawHud, otherwise its either just a (misnamed) ssqc progs or a full csqc progs that would just crash us on 3d stuff.
		if ((*versionedname &&	PR_LoadProgs(versionedname, false, PROGHEADER_CRC, pr_csqcbuiltins, pr_csqcnumbuiltins) && (qcvm->extfuncs.CSQC_DrawHud||cl.qcvm.extfuncs.CSQC_UpdateView))||
			(*val &&			PR_LoadProgs(val,			false, PROGHEADER_CRC, pr_csqcbuiltins, pr_csqcnumbuiltins) && (qcvm->extfuncs.CSQC_DrawHud||cl.qcvm.extfuncs.CSQC_UpdateView))||
			(					PR_LoadProgs("csprogs.dat", false, PROGHEADER_CRC, pr_csqcbuiltins, pr_csqcnumbuiltins) && (qcvm->extfuncs.CSQC_DrawHud||qcvm->extfuncs.CSQC_DrawScores||cl.qcvm.extfuncs.CSQC_UpdateView))||
			(					PR_LoadProgs("progs.dat",   false, PROGHEADER_CRC, pr_csqcbuiltins, pr_csqcnumbuiltins) && (qcvm->extfuncs.CSQC_DrawHud||cl.qcvm.extfuncs.CSQC_UpdateView)))
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
					return true;
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
				if (!CL_QueueClientStringCommand("enablecsqc"))
				{
					PR_SwitchQCVM(NULL);
					return false;
				}
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
	return true;
}

void Host_RunCvarMigrations (void) // woods #migration
{
	cvar_t	*cv;
	int	schema = (int)cl_migration_schema.value;
	int	original_schema = schema;

	if (schema < 1)
	{
		cv = Cvar_FindVar ("cl_web_download_url");
		if (cv && !q_strcasecmp (cv->string, "q1tools.github.io"))
		{
			Cvar_Set ("cl_web_download_url", "q1tools/q1tools.github.io");
			Con_Printf ("Migrated cl_web_download_url to q1tools/q1tools.github.io\n");
		}

		cv = Cvar_FindVar ("cl_afk");
		if (cv && !q_strcasecmp (cv->string, "1"))
		{
			Cvar_Set ("cl_afk", "0");
			Con_Printf ("Migrated cl_afk to 0\n");
		}

		schema = 1;
	}

	if (schema < 2)
	{
#ifdef MACBOOK_ARM_HACK // woods #collinear
		cv = Cvar_FindVar ("r_remove_collinear_vertices");
		if (cv && !q_strcasecmp (cv->string, "0"))
		{
			Cvar_Set ("r_remove_collinear_vertices", "1");
			Con_Printf ("Migrated r_remove_collinear_vertices to 1\n");
		}
#endif

		schema = 2;
	}

	if (schema < 3)
	{
		// woods #routline -- pick up the clientside candle models added to
		// the r_nooutline_list/r_noshadow_list defaults without clobbering
		// user customizations.
		static const char *const candles[] = {
			"progs/candle.mdl", "progs/candle1.mdl", "progs/candle2.mdl",
			"progs/candle3.mdl", "progs/misc_candle1.mdl",
			"progs/misc_candle2.mdl", "progs/misc_candle3.mdl",
		};
		static const char *const lists[] = { "r_nooutline_list", "r_noshadow_list" };
		extern qboolean nameInList (const char *list, const char *name);
		int	l;

		for (l = 0; l < (int)Q_COUNTOF(lists); l++)
		{
			cv = Cvar_FindVar (lists[l]);
			if (cv)
			{
				char	*list;
				size_t	len, needed;
				int	i;
				qboolean added = false;

				needed = strlen (cv->string);
				for (i = 0; i < (int)Q_COUNTOF(candles); i++)
				{
					size_t candle_len, extra;

					if (nameInList (cv->string, candles[i]))
						continue;
					candle_len = strlen (candles[i]);
					extra = candle_len + (needed ? 1 : 0);
					if (extra < candle_len || needed > SIZE_MAX - extra - 1)
						Sys_Error ("Host_RunCvarMigrations: cvar value too long");
					needed += extra;
					added = true;
				}
				if (added)
				{
					list = (char *) Q_malloc (needed + 1);
					len = strlen (cv->string);
					memcpy (list, cv->string, len);
					for (i = 0; i < (int)Q_COUNTOF(candles); i++)
					{
						size_t candle_len;

						if (nameInList (cv->string, candles[i]))
							continue;
						if (len)
							list[len++] = ',';
						candle_len = strlen (candles[i]);
						memcpy (list + len, candles[i], candle_len);
						len += candle_len;
					}
					list[len] = 0;
					Cvar_Set (lists[l], list);
					Con_Printf ("Migrated %s: added candle models\n", lists[l]);
					free (list);
				}
			}
		}

		schema = 3;
	}

	if (schema < 4)
	{
		char	*legacy_chatmode = NULL;
		char	*current_chatmode = NULL;

		// woods #migration -- cl_say is no longer registered, so read its old
		// archived value directly. Prefer an explicit cl_chatmode setting when a
		// config contains both names.
		if (CFG_OpenConfig ("config.cfg") == 0)
		{
			legacy_chatmode = CFG_ReadCvarValue ("cl_say");
			current_chatmode = CFG_ReadCvarValue ("cl_chatmode");
			CFG_CloseConfig ();
		}

		if (legacy_chatmode && !current_chatmode)
		{
			Cvar_Set ("cl_chatmode", legacy_chatmode);
			Con_Printf ("Migrated cl_say to cl_chatmode\n");
		}

		// "seta cl_say" creates a temporary user cvar while the old config is
		// executing. Do not archive that placeholder back into the new config.
		cv = Cvar_FindVar ("cl_say");
		if (cv && (cv->flags & CVAR_USERDEFINED))
			cv->flags &= ~(CVAR_ARCHIVE | CVAR_SETA);

		free (legacy_chatmode);
		free (current_chatmode);
		schema = 4;
	}

	if (schema != original_schema)
	{
		Cvar_SetValue ("cl_migration_schema", (float)schema);
		Con_DPrintf ("Cvar migrations applied: %d -> %d\n", original_schema, schema);
	}
}

/*
==================
Main-thread deferred calls

Replaces SDL_AddTimer for callbacks that touch engine state (Cbuf, console,
client globals). SDL timer callbacks run on a separate thread, so doing that
work there races the main thread and can crash (e.g. Con_Printf -> SCR_UpdateScreen
-> OpenGL off-thread). These run from _Host_Frame on the main thread instead.

Host_DeferCall returns a handle (0 if the table is full). Cancellation is by
handle and is ABA-safe: a stale handle never matches a reused slot, so it just
no-ops. The callback owns its param (free it in the callback); cancellation does
not free it.
==================
*/
#define HOST_MAX_DEFERS	32
typedef struct {
	qboolean	active;
	int		id;
	double		due;
	void		(*fn)(void *);
	void		*param;
} hostdefer_t;
static hostdefer_t	host_defers[HOST_MAX_DEFERS];
static int		host_defer_nextid = 1;

int Host_DeferCall (double delay_seconds, void (*fn)(void *), void *param)
{
	int i;
	for (i = 0; i < HOST_MAX_DEFERS; i++)
		if (!host_defers[i].active)
		{
			host_defers[i].active = true;
			host_defers[i].id = host_defer_nextid++;
			if (host_defer_nextid <= 0)
				host_defer_nextid = 1;
			host_defers[i].due = realtime + delay_seconds;
			host_defers[i].fn = fn;
			host_defers[i].param = param;
			return host_defers[i].id;
		}
	return 0;	// table full -- drop rather than risk corruption
}

void Host_CancelDeferredCall (int handle)
{
	int i;
	if (handle <= 0)
		return;
	for (i = 0; i < HOST_MAX_DEFERS; i++)
		if (host_defers[i].active && host_defers[i].id == handle)
		{
			host_defers[i].active = false;
			return;
		}
}

static void Host_RunDeferredCalls (void)	// main thread, once per frame
{
	int i;
	for (i = 0; i < HOST_MAX_DEFERS; i++)
		if (host_defers[i].active && realtime >= host_defers[i].due)
		{
			void (*fn)(void *) = host_defers[i].fn;
			void *param = host_defers[i].param;
			host_defers[i].active = false;	// clear before calling (callback may re-arm)
			fn (param);
		}
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

	if (Host_Setjmp (host_abortserver) )
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
	Host_NetfpsProbe_Frame ();
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
	/* Audio-browser isolation must not depend on the menu draw path running.
	 * Run after commands so scripted menu transitions release it immediately. */
	M_AudioBrowser_CloseIfInactive();
	CL_ConnectFrame();

	Con_UpdateCenterPrint ();	// woods #centerlog -- flush deferred centerprint (main thread)
	Host_RunDeferredCalls ();	// run main-thread deferred calls (replaces unsafe SDL_AddTimer work)

	NET_Poll();
	NET_PortPingProbe_Frame();
	URI_Frame(); // woods #uri
	CL_AsyncDownload_Frame();
	M_DownloadMods_Frame();

	if (cl.sendprespawn)
	{
		if (CL_CheckDownloads())
		{
			if (CL_LoadCSProgs() &&
				CL_QueueClientStringCommand("prespawn"))
			{
				cl.sendprespawn = false;
				vid.recalc_refdef = true;
				CL_ConnectTimingMark("prespawn sent (precache done)");
			}
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
	{
		qboolean demo_seek_activity;

		CL_DemoSeekConsumeFrameActivity();

			// Burn most of this frame parsing demo messages headlessly when a
			// demo seek is active (restart, rewind, forward, marker, frame,
			// time, byte-offset); renders one progress frame after.
		demo_seek_activity = CL_DemoSeekFastPump ();
		if (cls.state == ca_connected)
			CL_ReadFromServer ();
		demo_seek_activity |= CL_DemoSeekConsumeFrameActivity();

		// Seek work can advance cl.time by many seconds in a single host
		// frame. Resync CSQC's qcvm clock only after seek-speed parsing;
		// normal demo playback needs the next frame to see the usual delta.
		if (demo_seek_activity && cls.state == ca_connected && cls.demoplayback && cl.qcvm.progs)
			cl.qcvm.time = cl.time;
	}

// update video
	if (host_speeds.value)
		time1 = Sys_DoubleTime ();

	// Skip the renderer (and particle stepping) while a demo seek is active so
	// the pump can use the full host frame for parsing instead of yielding ~10ms
	// to SCR_UpdateScreen per iteration. Last rendered frame stays on screen
	// until the seek completes, then the next host frame draws the result.
	if (!CL_DemoSeekActive ())
	{
		SCR_UpdateScreen ();
		CL_RunParticles (); //johnfitz -- seperated from rendering
		Sbar_PreloadFrame (); // spread 24-bit HUD decoding across post-launch/menu/connection frames
	}

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
	Host_UpdateDockBadge(); // woods -- show download progress in the platform shell UI
	DiscordPresence_Frame();

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

	srand((unsigned)time(NULL)); // woods -- initialization to for randomization

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
	else // woods #serverhistory
	{
		History_Init();
	}
	PR_Init ();
	Mod_Init ();
	NET_Init ();
	if (cls.state != ca_dedicated)
		NET_HostnameCache_Init();
	SV_Init ();
	URI_Init(); // woods #uri

	LOC_PQ_Init (); // rook / woods #pqteam (added PQ to name)
	if (cls.state != ca_dedicated)
		IPLog_Init ();		// JPG 1.05 - ip address logging // woods #iplog
	if (cls.state != ca_dedicated)
		FMod_Init ();

#ifdef QSS_DATE	//avoid non-determinism.
	Con_Printf ("Exe: " ENGINE_NAME_AND_VER "\n");
#else
	Con_Printf ("Exe: " __TIME__ " " __DATE__ "\n");
#endif
	Con_Printf ("%4.1f megabyte heap\n", host_parms->memsize/ (1024*1024.0));

	if (cls.state != ca_dedicated)
	{
		size_t colormap_len;

		host_colormap = (byte *)COM_LoadHunkFile ("gfx/colormap.lmp", NULL);
		colormap_len = (com_filesize > 0) ? (size_t)com_filesize : 0;
		if (!host_colormap)
			Sys_Error ("Couldn't load gfx/colormap.lmp");
		FMod_CheckModel ("gfx/colormap.lmp", host_colormap, colormap_len);

		V_Init ();
		Chase_Init ();
		// Filesystem-backed content lists are built lazily by their first consumer.
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
		M_CheckMods (); // woods #modsmenu (iw)
	}
	else // woods -- initialize lists for dedicated server argument completion
	{
		ExtraMaps_Init();
		Modlist_Init();
        CL_InitWebDownloads(false);
	}

	LOC_Init (); // for 2021 rerelease support.

	Hunk_AllocName (0, "-HOST_HUNKLEVEL-");
	host_hunklevel = Hunk_LowMark ();

	host_initialized = true;
	Con_Printf ("\n========= Quake Initialized =========\n\n");

	if (Host_Setjmp (host_abortserver) )
		return;			// something bad happened		
	//okay... now we can do stuff that's allowed to Host_Error

	//spike -- create these aliases, because they're useful.
	Cbuf_AddText ("alias startmap_sp \"map start\"\n");
	Cbuf_AddText ("alias startmap_dm \"map start\"\n");

	if (Host_Setjmp (host_abortserver) )
		return;			// don't do the above twice if the following Cbuf_Execute does bad things.

	if (cls.state != ca_dedicated)
	{
		Cbuf_AddText ("cl_warncmd 0\n");
		if (COM_FileExists("quake.rc", NULL))
			Cbuf_InsertText ("exec quake.rc\nhost_cvar_migrate\n");
//		else if (COM_FileExists("hexen.rc", NULL))
//			Cbuf_InsertText ("exec hexen.rc\n");	//includes a `menu_main` command which screws with quakespasm's normal startup behaviours. just ignore it and do the q2like thing.
		else
			Cbuf_InsertText ("exec default.cfg\nexec config.cfg\nexec autoexec.cfg\nstuffcmds\n");
		Cbuf_AddText ("cl_warncmd 1\n");
	// johnfitz -- in case the vid mode was locked during vid_init, we can unlock it now.
		// note: two leading newlines because the command buffer swallows one of them.
		Cbuf_AddText ("\n\nvid_unlock\n");
		Cbuf_AddText("namebk\n"); // woods #smartafk lets run a backup name check for AFK leftovers (crash/force quit)
		Cbuf_AddText("startup\n"); // woods #onload
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
	DiscordPresence_Shutdown();

	SV_CleanupTimer(); // woods #svtimer

	Host_WriteConfiguration ();

	M_DownloadMods_Shutdown();

	CL_AsyncDownload_Shutdown();

	URI_Shutdown(); // woods #uri -- shutdown async URI subsystem early to stop worker before network teardown

	Host_ShutdownSave ();

	Host_BackupConfiguration (); // woods #cfgbackup

	IPLog_WriteLog ();	// JPG 1.05 - ip loggging  // woods #iplog

	COM_RemoveDownloadTempFiles();

	NET_HostnameCache_Shutdown();
	NET_Shutdown ();

	if (cls.state != ca_dedicated)
	{
		TexMgr_Shutdown();
		if (con_initialized)
			History_Shutdown ();
		BGM_Shutdown();
		CDAudio_Shutdown ();
		S_Shutdown ();
		IN_Shutdown ();
		R_GrassShutdown();
		VID_Shutdown();
	}
	else // woods #serverhistory
	{
		History_Shutdown();
	}

	LOG_Close ();

	LOC_Shutdown ();
}
