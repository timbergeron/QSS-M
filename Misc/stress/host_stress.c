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

static void Host_StressKey_f (void);
static void Host_StressChar_f (void);
static void Host_StressStatus_f (void);
static void Host_StressCapabilities_f (void);
static void Host_StressInject_f (void);

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

	Con_Printf ("STRESS_READY %s\n", stress.scriptpath);
}

void Host_StressPoll (void)
{
	if (!stress.active || realtime < stress.nextpoll)
		return;
	stress.nextpoll = realtime + 0.02;
	Host_StressDrainAnswer ();
	Host_StressDrainScript ();
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
