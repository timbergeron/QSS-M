/*
 * host_stress.c -- QSS-M stress-test command and script boundary.
 *
 * This module is compiled into the Debug QSS-M target with QSSM_STRESS.  The
 * release build gets no-op lifecycle functions, so the stress command surface
 * is not present in normal player binaries.
 *
 * The Python harness appends commands to a per-process script.  The engine
 * polls that file on the main thread, queues ordinary console commands, and
 * answers liveness probes through the normal console log.  Keeping all of
 * this on the host thread is important: key/menu/console commands are not safe
 * to execute from a file or SDL worker thread.
 */

#include "quakedef.h"

#ifdef QSSM_STRESS

#define STRESS_QUEUE_SIZE 65536
#define STRESS_LINE_SIZE  1024
#define STRESS_PROF_FRAMES 20000

typedef struct
{
	qboolean active;
	char scriptpath[MAX_OSPATH];
	char answerpath[MAX_OSPATH];
	long scriptofs;
	long answerofs;
	double nextpoll;
	char queue[STRESS_QUEUE_SIZE];
	size_t queuelen;
	char answerqueue[STRESS_LINE_SIZE * 4];
	size_t answerqueuelen;
	int seq_acked;
	int seq_exec;
	int parse_overreads;
	unsigned int coverage_total;
} host_stress_state_t;

static host_stress_state_t stress;

/* Frame-time profiler.  r_speeds only times R_RenderView and prints a line per
 * frame, so with -condebug the measurement writes a file every frame and
 * changes what it is measuring.  This samples the whole frame period once per
 * frame, buffers it, and reports percentiles once at the end. */
typedef struct
{
	qboolean active;
	double endtime;
	double lastframe;
	int count;
	int dropped;
	char label[64];
	float ms[STRESS_PROF_FRAMES];
#ifdef QSSM_RENDERSTATS
	/* Snapshots of the renderer's rs_* counters.  Those sit behind their own
	 * define because the counters are a separate, optional patch: this file
	 * must compile against a stock renderer, where only frame timing exists. */
	unsigned int builds, stylebuilds, uploads, blocks, uploadkb;
	double blockms, uploadms, skyscanms, workerms, lightmapms;
	unsigned int lmscanned, lmbuilt, stylejobs, stylebusy, visfat;
	unsigned int visedicts, edframes, aliaspolys, aliaspasses;
	unsigned int aents, akeyswitch, adistinctkey, adistinctmodel;
	unsigned int adcall[RS_ALIAS_NUMPASSES], atris[RS_ALIAS_NUMPASSES];
	double partms;
	unsigned int partframes, partcount;
	unsigned int scullA, scullD;
	unsigned int bsEnts, bsSurfs, bsVerts, bsBuf, bsIdx;
	double visms, queuems, drawms;
#endif
} host_stress_prof_t;

static host_stress_prof_t prof;

static void Host_StressKey_f (void);
static void Host_StressChar_f (void);
static void Host_StressStatus_f (void);
static void Host_StressCapabilities_f (void);
static void Host_StressInject_f (void);
static void Host_StressFrameProf_f (void);

static void Host_StressReadAppend (const char *path, long *offset,
					   char *buffer, size_t *length, size_t capacity)
{
	FILE *file;
	long end;
	size_t room, got;

	file = fopen (path, "rb");
	if (!file)
		return;
	if (fseek (file, 0, SEEK_END) != 0 || (end = ftell (file)) < 0)
	{
		fclose (file);
		return;
	}
	if (end < *offset)
		*offset = 0; /* The harness may recreate the script after a retry. */
	if (end == *offset)
	{
		fclose (file);
		return;
	}
	if (fseek (file, *offset, SEEK_SET) != 0)
	{
		fclose (file);
		return;
	}

	room = capacity - *length;
	if ((unsigned long)(end - *offset) > room)
	{
		Con_Printf ("STRESS_QUEUE overflow path=%s bytes=%ld\n", path,
				end - *offset);
		/* Drop the unread tail rather than corrupting the command queue. */
		*offset = end;
		fclose (file);
		return;
	}
	got = fread (buffer + *length, 1, (size_t)(end - *offset), file);
	*length += got;
	*offset += (long)got;
	fclose (file);
}

static qboolean Host_StressNextLine (char *buffer, size_t *length,
						 char *line, size_t linecapacity)
{
	char *newline;
	size_t count;

	newline = (char *)memchr (buffer, '\n', *length);
	if (!newline)
		return false;
	count = (size_t)(newline - buffer);
	if (count && buffer[count - 1] == '\r')
		count--;
	if (count >= linecapacity)
		count = linecapacity - 1;
	memcpy (line, buffer, count);
	line[count] = 0;

	count = (size_t)(newline - buffer) + 1;
	*length -= count;
	if (*length)
		memmove (buffer, buffer + count, *length);
	return true;
}

static void Host_StressQueueCommand (const char *line)
{
	char command[STRESS_LINE_SIZE + 2];

	q_snprintf (command, sizeof(command), "%s\n", line);
	Cbuf_AddText (command);
	stress.seq_exec++;
}

static qboolean Host_StressBarrierMet (const char *condition)
{
	/* The current harness emits barriers only for replay metadata.  Basic
	 * lifecycle barriers are represented by the following status probe, so an
	 * unknown condition should not deadlock the command stream. */
	Q_UNUSED (condition);
	return true;
}

static void Host_StressDrainScript (void)
{
	char line[STRESS_LINE_SIZE];

	Host_StressReadAppend (stress.scriptpath, &stress.scriptofs,
					   stress.queue, &stress.queuelen, sizeof(stress.queue));
	while (Host_StressNextLine (stress.queue, &stress.queuelen,
						line, sizeof(line)))
	{
		if (!line[0])
			continue;
		if (!strncmp (line, "@seq ", 5))
		{
			stress.seq_acked = atoi (line + 5);
			continue;
		}
		if (!strncmp (line, "@barrier ", 9))
		{
			if (!Host_StressBarrierMet (line + 9))
				break;
			continue;
		}
		Host_StressQueueCommand (line);
	}
}

static void Host_StressDrainAnswer (void)
{
	char line[STRESS_LINE_SIZE];

	Host_StressReadAppend (stress.answerpath, &stress.answerofs,
					   stress.answerqueue, &stress.answerqueuelen,
					   sizeof(stress.answerqueue));
	while (Host_StressNextLine (stress.answerqueue, &stress.answerqueuelen,
						line, sizeof(line)))
	{
		if (line[0])
		{
			Con_Printf ("STRESS_MODAL_ANSWER %s\n", line);
			Cmd_ExecuteString (line, src_command);
		}
	}
}

void Host_StressInit (void)
{
	int parm;

	parm = COM_CheckParm ("-stress");
	if (!parm)
		return;
	if (parm + 1 >= com_argc || !com_argv[parm + 1] ||
		com_argv[parm + 1][0] == '-')
	{
		Con_Printf ("-stress requires a script path\n");
		return;
	}

	memset (&stress, 0, sizeof(stress));
	q_strlcpy (stress.scriptpath, com_argv[parm + 1],
			   sizeof(stress.scriptpath));
	q_snprintf (stress.answerpath, sizeof(stress.answerpath), "%s.answer",
			stress.scriptpath);
	stress.active = true;

	Cmd_AddCommand ("_stress_key", Host_StressKey_f);
	Cmd_AddCommand ("_stress_char", Host_StressChar_f);
	Cmd_AddCommand ("_stress_status", Host_StressStatus_f);
	Cmd_AddCommand ("_stress_capabilities", Host_StressCapabilities_f);
	Cmd_AddCommand ("_stress_inject", Host_StressInject_f);
	Cmd_AddCommand ("_stress_frameprof", Host_StressFrameProf_f);

	Con_Printf ("STRESS_READY %s\n", stress.scriptpath);
	fflush (stdout);
}

static int Host_StressProfCompare (const void *a, const void *b)
{
	float fa = *(const float *)a, fb = *(const float *)b;
	return (fa > fb) - (fa < fb);
}

static float Host_StressProfPercentile (float pct)
{
	int idx;

	if (!prof.count)
		return 0;
	idx = (int)(pct * (prof.count - 1) + 0.5f);
	if (idx < 0)
		idx = 0;
	else if (idx >= prof.count)
		idx = prof.count - 1;
	return prof.ms[idx];
}

#ifdef QSSM_RENDERSTATS
/* per-frame average over the sample window */
#define PERFRAME(x) ((rs_scenecache_frames - prof.edframes) ? 	(double)(x) / (double)(rs_scenecache_frames - prof.edframes) : 0.0)
#endif

static void Host_StressProfReport (void)
{
	double total = 0;
	int i, over33 = 0, over16 = 0;
	double seconds;

	prof.active = false;
	if (!prof.count)
	{
		Con_Printf ("STRESS_PROF label=%s frames=0\n", prof.label);
		return;
	}

	for (i = 0; i < prof.count; i++)
	{
		total += prof.ms[i];
		if (prof.ms[i] > 33.4f)
			over33++;
		if (prof.ms[i] > 16.7f)
			over16++;
	}
	seconds = total / 1000.0;
	qsort (prof.ms, prof.count, sizeof(prof.ms[0]), Host_StressProfCompare);

	Con_Printf ("STRESS_PROF label=%s frames=%d dropped=%d secs=%.2f fps=%.1f "
			"min=%.2f p50=%.2f p90=%.2f p99=%.2f max=%.2f "
			"over16=%d over33=%d\n",
			prof.label, prof.count, prof.dropped, seconds,
			seconds > 0 ? prof.count / seconds : 0,
			Host_StressProfPercentile (0.0f),
			Host_StressProfPercentile (0.50f),
			Host_StressProfPercentile (0.90f),
			Host_StressProfPercentile (0.99f),
			Host_StressProfPercentile (1.0f),
			over16, over33);

#ifdef QSSM_RENDERSTATS
	Con_Printf ("STRESS_PROF_CACHE label=%s builds=%u stylebuilds=%u uploads=%u "
			"uploadkb=%u uploadms=%.1f blocks=%u blockms=%.1f "
			"skyscanms=%.1f workerwallms=%.1f lightmapwallms=%.1f "
			"lmscanned=%u lmbuilt=%u stylejobs=%u stylebusy=%u "
			"visms=%.1f queuems=%.1f drawms=%.1f visfat=%u "
			"edicts=%.0f aliastris=%.0f aliaspasses=%.0f "
			"aents=%.0f akeysw=%.0f adkey=%.0f adModel=%.0f "
			"dcMain=%.0f dcShadow=%.0f dcOutline=%.0f dcShell=%.0f "
			"trMain=%.0f trShadow=%.0f trOutline=%.0f trShell=%.0f "
			"partms=%.1f partcount=%.0f scullA=%.1f scullD=%.1f "
			"bsEnts=%.1f bsSurfs=%.0f bsVerts=%.0f bsBuf=%.1f bsIdx=%.0f\n",
			prof.label,
			rs_scenecache_builds - prof.builds,
			rs_scenecache_stylebuilds - prof.stylebuilds,
			rs_scenecache_uploads - prof.uploads,
			rs_scenecache_uploadkb - prof.uploadkb,
			rs_scenecache_uploadms - prof.uploadms,
			rs_scenecache_blocks - prof.blocks,
			rs_scenecache_blockms - prof.blockms,
			rs_scenecache_skyscanms - prof.skyscanms,
			rs_scenecache_workerms - prof.workerms,
			rs_scenecache_lightmapms - prof.lightmapms,
			rs_scenecache_lmscanned - prof.lmscanned,
			rs_scenecache_lmbuilt - prof.lmbuilt,
			rs_scenecache_stylejobs - prof.stylejobs,
			rs_scenecache_stylebusy - prof.stylebusy,
			rs_scenecache_visms - prof.visms,
			rs_scenecache_queuems - prof.queuems,
			rs_scenecache_drawms - prof.drawms,
			rs_scenecache_visfat - prof.visfat,
			(rs_scenecache_frames - prof.edframes) ?
				(double)(rs_scenecache_visedicts - prof.visedicts) /
				(double)(rs_scenecache_frames - prof.edframes) : 0.0,
			(rs_scenecache_frames - prof.edframes) ?
				(double)((unsigned int)rs_aliaspolys - prof.aliaspolys) /
				(double)(rs_scenecache_frames - prof.edframes) : 0.0,
			(rs_scenecache_frames - prof.edframes) ?
				(double)((unsigned int)rs_aliaspasses - prof.aliaspasses) /
				(double)(rs_scenecache_frames - prof.edframes) : 0.0,
			PERFRAME (rs_alias_entities - prof.aents),
			PERFRAME (rs_alias_keyswitch - prof.akeyswitch),
			PERFRAME (rs_alias_distinctkey - prof.adistinctkey),
			PERFRAME (rs_alias_distinctmodel - prof.adistinctmodel),
			PERFRAME (rs_alias_drawcalls[RS_ALIAS_MAIN] - prof.adcall[RS_ALIAS_MAIN]),
			PERFRAME (rs_alias_drawcalls[RS_ALIAS_SHADOW] - prof.adcall[RS_ALIAS_SHADOW]),
			PERFRAME (rs_alias_drawcalls[RS_ALIAS_OUTLINE] - prof.adcall[RS_ALIAS_OUTLINE]),
			PERFRAME (rs_alias_drawcalls[RS_ALIAS_SHELL] - prof.adcall[RS_ALIAS_SHELL]),
			PERFRAME (rs_alias_passtris[RS_ALIAS_MAIN] - prof.atris[RS_ALIAS_MAIN]),
			PERFRAME (rs_alias_passtris[RS_ALIAS_SHADOW] - prof.atris[RS_ALIAS_SHADOW]),
			PERFRAME (rs_alias_passtris[RS_ALIAS_OUTLINE] - prof.atris[RS_ALIAS_OUTLINE]),
			PERFRAME (rs_alias_passtris[RS_ALIAS_SHELL] - prof.atris[RS_ALIAS_SHELL]),
			rs_particle_ms - prof.partms,
			PERFRAME (rs_particle_count - prof.partcount),
			PERFRAME (rs_alias_shadowcull_alpha - prof.scullA),
			PERFRAME (rs_alias_shadowcull_dist - prof.scullD),
			PERFRAME (rs_bshadow_ents - prof.bsEnts),
			PERFRAME (rs_bshadow_surfs - prof.bsSurfs),
			PERFRAME (rs_bshadow_verts - prof.bsVerts),
			PERFRAME (rs_bshadow_buffered - prof.bsBuf),
			PERFRAME (rs_bshadow_indices - prof.bsIdx));

#endif
}

static void Host_StressProfSample (void)
{
	double now = Sys_DoubleTime ();

	if (!prof.active)
		return;
	if (prof.lastframe > 0)
	{
		if (prof.count < STRESS_PROF_FRAMES)
			prof.ms[prof.count++] = (float)((now - prof.lastframe) * 1000.0);
		else
			prof.dropped++;
	}
	prof.lastframe = now;
	if (now >= prof.endtime)
		Host_StressProfReport ();
}

static void Host_StressFrameProf_f (void)
{
	double seconds;

	if (Cmd_Argc () < 2)
	{
		Con_Printf ("usage: _stress_frameprof <seconds> [label]\n");
		return;
	}
	seconds = atof (Cmd_Argv (1));
	if (seconds <= 0)
	{
		prof.active = false;
		return;
	}

	memset (&prof, 0, sizeof(prof));
	q_strlcpy (prof.label, Cmd_Argc () > 2 ? Cmd_Argv (2) : "run",
			   sizeof(prof.label));
#ifdef QSSM_RENDERSTATS
	prof.builds = rs_scenecache_builds;
	prof.stylebuilds = rs_scenecache_stylebuilds;
	prof.uploads = rs_scenecache_uploads;
	prof.uploadkb = rs_scenecache_uploadkb;
	prof.blocks = rs_scenecache_blocks;
	prof.blockms = rs_scenecache_blockms;
	prof.uploadms = rs_scenecache_uploadms;
	prof.skyscanms = rs_scenecache_skyscanms;
	prof.workerms = rs_scenecache_workerms;
	prof.lightmapms = rs_scenecache_lightmapms;
	prof.lmscanned = rs_scenecache_lmscanned;
	prof.lmbuilt = rs_scenecache_lmbuilt;
	prof.stylejobs = rs_scenecache_stylejobs;
	prof.stylebusy = rs_scenecache_stylebusy;
	prof.visms = rs_scenecache_visms;
	prof.queuems = rs_scenecache_queuems;
	prof.drawms = rs_scenecache_drawms;
	prof.visfat = rs_scenecache_visfat;
	prof.visedicts = rs_scenecache_visedicts;
	prof.edframes = rs_scenecache_frames;
	prof.aliaspolys = (unsigned int)rs_aliaspolys;
	prof.aliaspasses = (unsigned int)rs_aliaspasses;
	prof.aents = rs_alias_entities;
	prof.akeyswitch = rs_alias_keyswitch;
	prof.adistinctkey = rs_alias_distinctkey;
	prof.adistinctmodel = rs_alias_distinctmodel;
	memcpy (prof.adcall, rs_alias_drawcalls, sizeof(prof.adcall));
	memcpy (prof.atris, rs_alias_passtris, sizeof(prof.atris));
	prof.partms = rs_particle_ms;
	prof.partframes = rs_particle_frames;
	prof.partcount = rs_particle_count;
	prof.scullA = rs_alias_shadowcull_alpha;
	prof.scullD = rs_alias_shadowcull_dist;
	prof.bsEnts = rs_bshadow_ents;
	prof.bsSurfs = rs_bshadow_surfs;
	prof.bsVerts = rs_bshadow_verts;
	prof.bsBuf = rs_bshadow_buffered;
	prof.bsIdx = rs_bshadow_indices;
#endif
	/* The first sample is discarded: it would span the command's own frame. */
	prof.lastframe = 0;
	prof.endtime = Sys_DoubleTime () + seconds;
	prof.active = true;
}

void Host_StressPoll (void)
{
	Host_StressProfSample ();

	if (!stress.active || realtime < stress.nextpoll)
		return;
	stress.nextpoll = realtime + 0.02;
	Host_StressDrainAnswer ();
	Host_StressDrainScript ();
	fflush (stdout);
}

void Host_StressPumpModal (void)
{
	if (stress.active)
		Host_StressDrainAnswer ();
}

void Host_StressNoteParse (int readcount, int cursize)
{
	if (!stress.active || readcount == cursize)
		return;
	stress.parse_overreads++;
	Con_Printf ("STRESS_PARSE %s readcount=%d cursize=%d\n",
			readcount > cursize ? "overread" : "short", readcount, cursize);
}

void Host_StressCoverage (const char *parser, unsigned int id)
{
	if (!stress.active)
		return;
	stress.coverage_total++;
	Con_Printf ("STRESS_COVERAGE parser=%s id=%u\n", parser, id);
}

static void Host_StressKey_f (void)
{
	int key;

	if (Cmd_Argc () < 2)
		return;
	key = atoi (Cmd_Argv (1));
	Key_Event (key, true);
	Key_Event (key, false);
}

static void Host_StressChar_f (void)
{
	const char *text;

	if (Cmd_Argc () < 2)
		return;
	text = Cmd_Argv (1);
	while (*text)
		Char_Event ((unsigned char)*text++);
}

static void Host_StressStatus_f (void)
{
	const char *seq = Cmd_Argc () > 1 ? Cmd_Argv (1) : "0";

	Con_Printf ("STRESS_STATUS seq=%s acked=%d exec=%d state=%d signon=%d "
			"sv=%d keydest=%d parses=%d coverage=%u\n",
			seq, stress.seq_acked, stress.seq_exec, (int)cls.state, cls.signon,
			sv.active ? 1 : 0, (int)key_dest, stress.parse_overreads,
			stress.coverage_total);
}

static void Host_StressCapabilities_f (void)
{
	/* The lifecycle/input boundary is real.  Parser and datagram entry points
	 * remain explicitly unavailable until their owning modules are wired. */
	Con_Printf ("STRESS_CAPS qssm_stress=1 basic=1 wire_inject=0 "
			"exact_servermsg=0 exact_clientmsg=0 raw_datagram=0\n");
}

static void Host_StressInject_f (void)
{
	/* Keep the command registered so old journals fail explicitly and safely.
	 * The parser-specific lanes are skipped by capability checks until a real
	 * network adapter is added beside net_dgrm.c. */
	Con_Printf ("STRESS_UNSUPPORTED _stress_inject\n");
}

#else

void Host_StressInit (void) {}
void Host_StressPoll (void) {}
void Host_StressPumpModal (void) {}
void Host_StressNoteParse (int readcount, int cursize)
{
	Q_UNUSED (readcount);
	Q_UNUSED (cursize);
}
void Host_StressCoverage (const char *parser, unsigned int id)
{
	Q_UNUSED (parser);
	Q_UNUSED (id);
}

#endif
