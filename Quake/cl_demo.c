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
#include <errno.h>
#include <limits.h>
#ifdef USE_ZLIB
#include <assert.h>
#include <stdint.h>
#include <zlib.h>
#endif
#include "quakedef.h"
#include "json.h"

static void CL_FinishTimeDemo (void);
entity_t *CL_EntityNum (int num);

char		demoplaying[MAX_OSPATH]; // woods for window title
char		last_demo[MAX_OSPATH]; // woods #lastdemo
static char	demo_record_raw_path[MAX_OSPATH];
static qboolean demo_record_to_dzip;
static unsigned int demo_record_serial;
#ifdef USE_ZLIB
static unsigned int demo_finalize_serial;
#endif

#define DEMOMARK_HISTORY_FILENAME "demomarks.json"
#define DEMOMARK_HISTORY_TIME_LENGTH 20

typedef struct demomark_history_entry_s
{
	char demo[MAX_OSPATH];
	char map[MAX_QPATH];
	char created_at[DEMOMARK_HISTORY_TIME_LENGTH];
	int frame;
	struct demomark_history_entry_s *next;
} demomark_history_entry_t;

static demomark_history_entry_t *demomark_pending_head;
static demomark_history_entry_t *demomark_pending_tail;
static void CL_DemoMarkHistory_ClearPending(void);
static qboolean demo_record_stop_in_progress;
static qboolean demo_record_stop_requested;

#define DEMO_WRITE_QUEUE_BYTES (8u * 1024u * 1024u)

typedef struct demo_write_chunk_s
{
	struct demo_write_chunk_s *next;
	size_t size;
	byte data[1];
} demo_write_chunk_t;

static struct
{
	SDL_mutex *mutex;
	SDL_cond *work_condition;
	SDL_cond *state_condition;
	SDL_Thread *thread;
	demo_write_chunk_t *pending_head;
	demo_write_chunk_t *pending_tail;
	size_t pending_bytes;
	FILE *file;
	qboolean active;
	qboolean closing;
	qboolean shutdown;
	qboolean write_failed;
} demo_writer;
static qboolean demo_writer_queue_error_reported;

#ifdef USE_ZLIB
#define DEMO_FINALIZE_MAX_OUTSTANDING 4

typedef struct demo_finalize_job_s
{
	struct demo_finalize_job_s *next;
	char raw_path[MAX_OSPATH];
	char archive_path[MAX_OSPATH];
	char temp_path[MAX_OSPATH];
	char entry_name[MAX_OSPATH];
	int frame_count;
	qboolean delete_short;
	qboolean ok;
	qboolean raw_remove_failed;
	demomark_history_entry_t *marks_head;
	demomark_history_entry_t *marks_tail;
} demo_finalize_job_t;

static struct
{
	SDL_mutex *mutex;
	SDL_cond *condition;
	SDL_Thread *thread;
	demo_finalize_job_t *pending_head;
	demo_finalize_job_t *pending_tail;
	demo_finalize_job_t *completed_head;
	demo_finalize_job_t *completed_tail;
	demo_finalize_job_t *active;
	int outstanding;
	qboolean shutdown;
} demo_finalizer;
#endif

#ifdef USE_ZLIB
static void CL_DZipCleanupTempDemo(void);
static qboolean CL_DZipOpenDemoArchive(const char *archive_path, FILE **out_demo, qofs_t *out_size, char *out_entry_name, size_t out_entry_name_size);
static qboolean CL_DZipExtractDemoArchiveOSPathToFile(const char *archive_path, FILE *out, qofs_t *out_size, char *out_entry_name, size_t out_entry_name_size);
static qboolean CL_DZipArchiveDemoFile(const char *src_dem_path, const char *archive_path, const char *entry_name);
static byte *CL_DZipLoadDemoBuffer(const char *archive_path, int *length_out);
static byte *CL_DZipLoadDemoBufferOSPath(const char *archive_path, int *length_out);
#endif

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
	size_t			datasize;
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
	DFE_STAT,
	DFE_STATSTR,
	DFE_ENTITY,
} framevent_t;

typedef struct
{
	int				int_value;
	float			float_value;
} demonumericstat_t;

typedef struct
{
	int				update_type;
	entity_state_t	netstate;
	double			msgtime;
	double			spawntime;
	vec3_t			msg_origins[2];
	vec3_t			msg_angles[2];
	vec3_t			origin;
	vec3_t			angles;
	int				frame;
	int				effects;
	int				skinnum;
	byte			lerpflags;
	qboolean		forcelink;
} demoentitystate_t;

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
		int			stats[MAX_CL_STATS];
		float		statsf[MAX_CL_STATS];
		char		*statss[MAX_CL_STATS];
		size_t		num_entities;
		demoentitystate_t entity_states[MAX_EDICTS];
	}				prev;
}					demo_rewind;

static void CL_DemoRewindFreePrevStatStrings(void)
{
	size_t i;

	for (i = 0; i < MAX_CL_STATS; i++)
	{
		free(demo_rewind.prev.statss[i]);
		demo_rewind.prev.statss[i] = NULL;
	}
}

static void CL_DemoRewindSetPrevStatString(size_t stat_idx, const char *value)
{
	if (demo_rewind.prev.statss[stat_idx] == value)
		return;

	if (demo_rewind.prev.statss[stat_idx] && value &&
		strcmp(demo_rewind.prev.statss[stat_idx], value) == 0)
		return;

	free(demo_rewind.prev.statss[stat_idx]);
	demo_rewind.prev.statss[stat_idx] = value ? strdup(value) : NULL;
}

static void CL_DemoRewindSnapshotEntity(demoentitystate_t *dst, const entity_t *src)
{
	memset(dst, 0, sizeof(*dst));
	dst->update_type = src->update_type;
	dst->netstate = src->netstate;
	dst->msgtime = src->msgtime;
	dst->spawntime = src->spawntime;
	VectorCopy(src->msg_origins[0], dst->msg_origins[0]);
	VectorCopy(src->msg_origins[1], dst->msg_origins[1]);
	VectorCopy(src->msg_angles[0], dst->msg_angles[0]);
	VectorCopy(src->msg_angles[1], dst->msg_angles[1]);
	VectorCopy(src->origin, dst->origin);
	VectorCopy(src->angles, dst->angles);
	dst->frame = src->frame;
	dst->effects = src->effects;
	dst->skinnum = src->skinnum;
	dst->lerpflags = src->lerpflags;
	dst->forcelink = src->forcelink;
}

static void CL_DemoRewindApplyEntity(const demoentitystate_t *src, entity_t *ent)
{
	ent->update_type = src->update_type;
	ent->netstate = src->netstate;
	ent->msgtime = src->msgtime;
	ent->spawntime = src->spawntime;
	VectorCopy(src->msg_origins[0], ent->msg_origins[0]);
	VectorCopy(src->msg_origins[1], ent->msg_origins[1]);
	VectorCopy(src->msg_angles[0], ent->msg_angles[0]);
	VectorCopy(src->msg_angles[1], ent->msg_angles[1]);
	VectorCopy(src->origin, ent->origin);
	VectorCopy(src->angles, ent->angles);
	ent->frame = src->frame;
	ent->effects = src->effects;
	ent->skinnum = src->skinnum;
	ent->lerpflags = src->lerpflags;
	ent->forcelink = src->forcelink;
	ent->alpha = ent->netstate.alpha;
	ent->eflags = ent->netstate.eflags;

	if (ent->update_type && ent->netstate.modelindex > 0 && ent->netstate.modelindex < MAX_MODELS)
		ent->model = cl.model_precache[ent->netstate.modelindex];
	else
		ent->model = NULL;

	InvalidateTraceLineCache();
}

static void CL_DemoRewindApplyNumericStat(int stat, int ival, float fval)
{
	if (stat < 0 || stat >= MAX_CL_STATS)
		return;

	if (stat == STAT_HEALTH)
	{
		if (cl.stats[STAT_HEALTH] > 0 && ival <= 0)
		{
			cl.cshifts[CSHIFT_DEAD].destcolor[0] = 70;
			cl.cshifts[CSHIFT_DEAD].destcolor[1] = 0;
			cl.cshifts[CSHIFT_DEAD].destcolor[2] = 0;
			cl.cshifts[CSHIFT_DEAD].percent = 0;
		}
		else if (cl.stats[STAT_HEALTH] <= 0 && ival > 0)
		{
			cl.cshifts[CSHIFT_DEAD].percent = 0;
		}
	}

	cl.stats[stat] = ival;
	cl.statsf[stat] = fval;

	if (stat == STAT_VIEWZOOM)
		vid.recalc_refdef = true;

	Sbar_Changed();
}

int demo_target_offset = -1; // woods -- target offset for seeking, -1 when not seeking
qboolean is_seeking = false; // woods -- flag to indicate seeking status
qboolean demo_seek_from_start = false; // woods -- rewind to start before seeking forward
static qboolean is_time_seeking = false; // woods -- flag to indicate time-based seek status
static qboolean is_frame_seeking = false; // woods -- flag to indicate frame-based seek status
static qboolean is_marker_seeking = false; // woods -- flag to indicate marker seek status
static qboolean demo_marker_found = false; // woods -- marker hit during marker seek
static float demo_time_seek_target = 0.f; // woods -- target demo time for J/L seeks
static int demo_time_seek_direction = 0; // woods -- final direction for active time seek (-1 or +1)
static int demo_frame_seek_target = 0; // woods -- target demo frame for jumpdemo
static float demo_marker_found_time = 0.f; // woods -- demo time when the current marker seek hit
static int demo_last_marker_offset = -1; // woods -- last marker location used by jumpdemo mark
static float demo_start_server_time = 0.f; // woods -- user-facing demo time 0:00 origin
static qboolean demo_start_server_time_valid = false; // woods -- true once first parsed demo frame is known
static qboolean demo_jump_back_was_down = false; // woods -- edge detector for J key
static qboolean demo_jump_forward_was_down = false; // woods -- edge detector for L key
static qboolean demo_jump_home_was_down = false; // woods -- edge detector for 0/Home
static qboolean demo_jump_end_was_down = false; // woods -- edge detector for End
static int demo_total_frame_count = 0; // woods #demoframes -- playable frames after signon
static qboolean initialized = false; // woods (iw) #democontrols
static qboolean demo_seek_frame_activity = false; // woods -- true if this host frame did seek-speed parsing
static qboolean demo_restart_pending = false; // woods -- 0/Home restart request, processed from the host-frame pump
static char demo_restart_name[MAX_OSPATH]; // woods -- logical demo name to reopen for a clean start jump
static qboolean demo_scrub_dragging = false;
static qboolean demo_scrub_saved_paused = false;
static qboolean demo_scrub_have_saved_paused = false;
static double demo_scrub_last_seek_time = 0.0;
static float demo_scrub_last_seek_pct = 0.f;
static float demo_scrub_pending_pct = 0.f;
static qboolean demo_scrub_have_total_seconds_est = false;
static float demo_scrub_total_seconds_est = 0.f;

static qboolean CL_DemoProcessRestartRequest(void);
static qboolean CL_DemoQueueRestart(void);
static qboolean CL_DemoSeekToPercent(float pct);

static void CL_ClearDemoFrags(void)
{
	int i;

	for (i = 0; i < cl.maxclients; i++)
		cl.scores[i].frags = 0;
}

static void CL_ResetDemoSeekState(void)
{
	demo_target_offset = -1;
	is_seeking = false;
	demo_seek_from_start = false;
	is_time_seeking = false;
	is_frame_seeking = false;
	is_marker_seeking = false;
	demo_marker_found = false;
	demo_time_seek_target = 0.f;
	demo_time_seek_direction = 0;
	demo_frame_seek_target = 0;
	demo_marker_found_time = 0.f;
	demo_seek_frame_activity = false;
}

static void CL_ClearDemoRestartRequest(void)
{
	demo_restart_pending = false;
	demo_restart_name[0] = '\0';
}

static void CL_ClearDemoMarkerHistory(void)
{
	demo_last_marker_offset = -1;
}

static qboolean CL_DemoMarkUseServerPath(void)
{
	if (!cls.demorecording || cls.demoplayback || cls.state != ca_connected)
		return false;

	return cl.modtype == 1;
}

static int CL_GetDemoSeekStartOffset(void);
static int CL_GetDemoSeekEndOffset(void);

static qboolean CL_DemoNameHasMarkerTag(const char *name)
{
	char base[MAX_OSPATH];
	char *tag;

	if (!name || !name[0])
		return false;

	COM_FileBase(COM_SkipPath(name), base, sizeof(base));
	for (tag = q_strcasestr(base, "_marks"); tag; tag = q_strcasestr(tag + 1, "_marks"))
	{
		const char *suffix = tag + 6;

		if (!*suffix)
			return true;

		while (*suffix >= '0' && *suffix <= '9')
			suffix++;

		if (!*suffix)
			return true;
	}

	for (tag = q_strcasestr(base, "_mark"); tag; tag = q_strcasestr(tag + 1, "_mark"))
	{
		const char *suffix = tag + 5;

		if (!*suffix)
			return true;

		while (*suffix >= '0' && *suffix <= '9')
			suffix++;

		if (!*suffix)
			return true;
	}

	return false;
}

static int CL_GetDemoSeekStartOffset(void)
{
	return (cls.demofilestart > 0) ? (int)cls.demofilestart : cls.demo_offset_start;
}

static int CL_GetDemoSeekEndOffset(void)
{
	return cls.demo_offset_start + cls.demo_file_length;
}

static float CL_GetCurrentDemoPercent(void)
{
	const int demo_seek_start = CL_GetDemoSeekStartOffset();
	const int demo_seek_end = CL_GetDemoSeekEndOffset();
	const int demo_seek_range = demo_seek_end - demo_seek_start;
	int current_offset = cls.demo_offset_current;

	if (demo_seek_range <= 0)
		return 0.f;

	if (current_offset < demo_seek_start)
		current_offset = demo_seek_start;
	else if (current_offset > demo_seek_end)
		current_offset = demo_seek_end;

	return (current_offset - demo_seek_start) / (float)demo_seek_range * 100.f;
}

static int CL_DemoSeekOffsetThreshold(void)
{
	return q_max(100, cls.demo_file_length * 0.03);
}

static qboolean CL_DemoScrubEstimateTotalSeconds(float *seconds)
{
	float current_pct;
	float current_elapsed;

	if (!seconds || !demo_start_server_time_valid)
		return false;

	current_pct = CL_GetCurrentDemoPercent();
	current_elapsed = cl.mtime[0] - demo_start_server_time;
	if (current_pct <= 0.1f || current_elapsed <= 0.f)
		return false;

	*seconds = current_elapsed * 100.f / current_pct;
	return true;
}

static qboolean CL_DemoSeekToPercent(float pct)
{
	const int demo_seek_start = CL_GetDemoSeekStartOffset();
	const int demo_seek_end = CL_GetDemoSeekEndOffset();
	const int dynamic_threshold = CL_DemoSeekOffsetThreshold();
	int target_offset;

	if (!cls.demoplayback || !initialized || !cls.demofile || cls.demo_file_length <= 0)
		return false;

	pct = CLAMP(0.f, pct, 100.f);

	// Match the original 1-9 seek behavior: percentage is over the demo file
	// span, then clamped past the text track header/sign-on seek start.
	target_offset = cls.demo_offset_start + (int)((pct / 100.f) * (float)cls.demo_file_length);
	if (target_offset < demo_seek_start)
		target_offset = demo_seek_start;
	else if (target_offset > demo_seek_end)
		target_offset = demo_seek_end;

	if (pct <= 0.f && target_offset <= demo_seek_start && cls.demo_offset_current > demo_seek_start &&
		CL_DemoQueueRestart())
		return true;

	CL_ClearDemoRestartRequest();
	CL_ClearDemoMarkerHistory();
	CL_ResetDemoSeekState();

	if (abs(cls.demo_offset_current - target_offset) < dynamic_threshold)
	{
		cls.demospeed = cls.basedemospeed * !cls.demopaused;
		return true;
	}

	demo_target_offset = target_offset;
	demo_seek_from_start = (cls.demo_offset_current > target_offset);
	is_seeking = true;
	if (demo_seek_from_start)
		CL_ClearDemoFrags();

	return true;
}

qboolean CL_DemoSeekPercent(float pct)
{
	return CL_DemoSeekToPercent(pct);
}

qboolean CL_DemoScrubActive(void)
{
	return demo_scrub_dragging;
}

qboolean CL_DemoScrub_Begin(float pct)
{
	pct = CLAMP(0.f, pct, 100.f);

	if (!cls.demoplayback || !initialized)
		return false;

	demo_scrub_saved_paused = cls.demopaused;
	demo_scrub_have_saved_paused = true;
	cls.demopaused = true;
	demo_scrub_dragging = true;
	demo_scrub_pending_pct = pct;
	demo_scrub_have_total_seconds_est = CL_DemoScrubEstimateTotalSeconds(&demo_scrub_total_seconds_est);

	if (!CL_DemoSeekToPercent(pct))
	{
		CL_DemoScrub_Cancel();
		return false;
	}

	demo_scrub_last_seek_time = realtime;
	demo_scrub_last_seek_pct = pct;
	IN_DemoScrubCapture(true);
	return true;
}

void CL_DemoScrub_Update(float pct)
{
	pct = CLAMP(0.f, pct, 100.f);

	if (!demo_scrub_dragging)
		return;

	demo_scrub_pending_pct = pct;

	if (realtime - demo_scrub_last_seek_time >= 0.05 ||
		fabsf(pct - demo_scrub_last_seek_pct) >= 0.5f)
	{
		if (CL_DemoSeekToPercent(pct))
		{
			demo_scrub_last_seek_time = realtime;
			demo_scrub_last_seek_pct = pct;
		}
	}
}

void CL_DemoScrub_End(float pct)
{
	pct = CLAMP(0.f, pct, 100.f);

	if (!demo_scrub_dragging)
		return;

	demo_scrub_pending_pct = pct;
	CL_DemoSeekToPercent(pct);

	if (demo_scrub_have_saved_paused)
		cls.demopaused = demo_scrub_saved_paused;
	demo_scrub_dragging = false;
	demo_scrub_have_saved_paused = false;
	demo_scrub_have_total_seconds_est = false;
	IN_DemoScrubCapture(false);
}

void CL_DemoScrub_Cancel(void)
{
	if (demo_scrub_have_saved_paused)
		cls.demopaused = demo_scrub_saved_paused;
	demo_scrub_dragging = false;
	demo_scrub_have_saved_paused = false;
	demo_scrub_have_total_seconds_est = false;
	IN_DemoScrubCapture(false);
}

qboolean CL_DemoScrub_GetDisplayPercent(float *pct)
{
	if (!pct || !cls.demoplayback || !initialized || !demo_scrub_dragging)
		return false;

	*pct = demo_scrub_pending_pct;
	return true;
}

qboolean CL_DemoScrub_GetDisplayTime(float *seconds)
{
	if (!seconds || !demo_scrub_dragging || !demo_scrub_have_total_seconds_est)
		return false;

	*seconds = demo_scrub_total_seconds_est * (demo_scrub_pending_pct / 100.f);
	if (*seconds < 0.f)
		*seconds = 0.f;
	return true;
}

static qboolean CL_ParseIntStrict(const char *str, int *value)
{
	char *end;
	long parsed;

	if (!str || !str[0] || !value)
		return false;

	errno = 0;
	parsed = strtol(str, &end, 10);
	if (errno || end == str || *end || parsed < INT_MIN || parsed > INT_MAX)
		return false;

	*value = (int)parsed;
	return true;
}

static qboolean CL_ParseFloatStrict(const char *str, float *value)
{
	char *end;
	float parsed;

	if (!str || !str[0] || !value)
		return false;

	errno = 0;
	parsed = strtof(str, &end);
	if (errno || end == str || *end || !isfinite(parsed))
		return false;

	*value = parsed;
	return true;
}

static qboolean CL_ParseTimeString(const char *str, float *seconds)
{
	int parts[3];
	int num_parts = 0;
	const char *p;

	if (!str || !str[0] || !seconds)
		return false;

	p = str;
	while (*p)
	{
		char *end;
		long part;

		if (num_parts == 3 || *p == ':')
			return false;

		errno = 0;
		part = strtol(p, &end, 10);
		if (errno || end == p || part < 0 || part > INT_MAX)
			return false;

		parts[num_parts++] = (int)part;
		p = end;

		if (!*p)
			break;
		if (*p != ':')
			return false;
		p++;
		if (!*p)
			return false;
	}

	if (num_parts == 2)
	{
		if (parts[1] > 59)
			return false;
		*seconds = parts[0] * 60.f + parts[1];
		return true;
	}

	if (num_parts == 3)
	{
		if (parts[1] > 59 || parts[2] > 59)
			return false;
		*seconds = parts[0] * 3600.f + parts[1] * 60.f + parts[2];
		return true;
	}

	return false;
}

static void CL_FinishExactDemoSeek(void)
{
	cls.demospeed = cls.basedemospeed * !cls.demopaused;
	cl.time = cl.mtime[0];
	CL_ResetDemoSeekState();
}

/*
===============
CL_GetDemoFrameCount -- woods #demoframes
===============
*/
int CL_GetDemoFrameCount(void)
{
	return (int)VEC_SIZE(demo_rewind.frames);
}

/*
===============
CL_GetDemoTotalFrameCount -- woods #demoframes
===============
*/
int CL_GetDemoTotalFrameCount(void)
{
	return demo_total_frame_count;
}

int CL_CountDemoFramesInBufferLimit(const byte *data, int length, int limit)
{
	int off = 0;
	int count = 0;

	if (!data || length <= 0)
		return -1;

	while (off < length && data[off] != '\n')
		off++;
	if (off < length)
		off++;

	while (off <= length - 16)
	{
		int msg_len;

		memcpy(&msg_len, data + off, sizeof(msg_len));
		msg_len = LittleLong(msg_len);
		off += 4;

		if (msg_len <= 0 || msg_len > MAX_MSGLEN || msg_len > length - off - 12)
			break;

		off += 12 + msg_len;
		count++;
		if (limit > 0 && count >= limit)
			break;
	}

	return count;
}

int CL_CountDemoFramesInBuffer(const byte *data, int length)
{
	return CL_CountDemoFramesInBufferLimit(data, length, 0);
}

static int CL_CountDemoFramesInStreamLimit(FILE *f, int limit)
{
	int c;
	int count = 0;
	int msg_len;
	long end_offset;
	long current_offset;

	if (!f)
		return -1;

	if (fseek(f, 0, SEEK_END) != 0)
		return -1;
	end_offset = ftell(f);
	if (end_offset < 0 || fseek(f, 0, SEEK_SET) != 0)
		return -1;

	// skip the cdtrack line
	while ((c = fgetc(f)) != EOF && c != '\n')
		;
	if (c == EOF)
		return 0;

	for (;;)
	{
		current_offset = ftell(f);
		if (current_offset < 0 || current_offset > end_offset - 16)
			break;

		if (fread(&msg_len, sizeof(msg_len), 1, f) != 1)
			break;
		msg_len = LittleLong(msg_len);
		if (msg_len <= 0 || msg_len > MAX_MSGLEN)
			break;

		current_offset = ftell(f);
		if (current_offset < 0 || msg_len > end_offset - current_offset - 12)
			break;

		if (fseek(f, 12 + msg_len, SEEK_CUR) != 0)
			break;
		count++;
		if (limit > 0 && count >= limit)
			break;
	}

	return count;
}

int CL_CountDemoFramesInFileLimit(const char *path, int limit)
{
	if (!path || !path[0])
		return -1;

	if (!q_strcasecmp(COM_FileGetExtension(path), "dz"))
	{
#ifdef USE_ZLIB
		FILE *out;
		int frames = -1;

		out = tmpfile();
		if (!out)
			return -1;

		if (CL_DZipExtractDemoArchiveOSPathToFile(path, out, NULL, NULL, 0))
			frames = CL_CountDemoFramesInStreamLimit(out, limit);

		fclose(out);
		return frames;
#else
		return -1;
#endif
	}
	else
	{
		FILE *f;
		int frames;

		f = fopen(path, "rb");
		if (!f)
			return -1;

		frames = CL_CountDemoFramesInStreamLimit(f, limit);
		fclose(f);
		return frames;
	}
}

int CL_CountDemoFramesInFile(const char *path)
{
	return CL_CountDemoFramesInFileLimit(path, 0);
}

int CL_DemoMinFramesThreshold(qboolean *auto_delete)
{
	float value = cl_demo_minframes.value;

	if (auto_delete)
		*auto_delete = value < 0.0f;

	if (value < 0.0f)
		value = -value;
	if (!(value > 0.0f))
		return 0;
	if (value > (float)INT_MAX)
		return INT_MAX;
	return (int)value;
}

qboolean CL_DemoSeekConsumeFrameActivity(void)
{
	qboolean activity = demo_seek_frame_activity;
	demo_seek_frame_activity = false;
	return activity;
}

/*
===============
CL_CountRemainingDemoFrames -- woods #demoframes
===============
*/
static int CL_CountRemainingDemoFrames(void)
{
	long saved_offset;
	int end_offset;
	int count = 0;

	if (!cls.demofile || cls.demo_file_length <= 0)
		return 0;

	saved_offset = ftell(cls.demofile);
	if (saved_offset < 0)
		return 0;

	end_offset = CL_GetDemoSeekEndOffset();
	while (1)
	{
		long current_offset = ftell(cls.demofile);
		int cursize;

		if (current_offset < 0 || current_offset >= end_offset)
			break;
		if (end_offset - current_offset < 16)
			break;
		if (fread(&cursize, 4, 1, cls.demofile) != 1)
			break;

		cursize = LittleLong(cursize);
		if (cursize <= 0 || cursize > MAX_MSGLEN)
			break;

		current_offset = ftell(cls.demofile);
		if (current_offset < 0 || current_offset + 12L + cursize > end_offset)
			break;
		if (fseek(cls.demofile, 12L + cursize, SEEK_CUR) != 0)
			break;

		count++;
	}

	fseek(cls.demofile, saved_offset, SEEK_SET);
	clearerr(cls.demofile);

	return count;
}

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
CL_DemoSeekForwardActive

True when forward parsing should advance to reach the seek target. Compares
current position to target directly (not demo_seek_from_start), so the J-key
backward time seek and the post-rewind forward catch-up both benefit.
===============
*/
static qboolean CL_DemoSeekForwardActive(void)
{
	if (!cls.demoplayback || cls.signon < SIGNONS)
		return false;
	if (is_marker_seeking)
		return !demo_marker_found;
	if (is_frame_seeking)
		return CL_GetDemoFrameCount() < demo_frame_seek_target;
	if (is_time_seeking)
		return cl.mtime[0] < demo_time_seek_target;
	if (is_seeking)
		return cls.demo_offset_current < demo_target_offset;
	return false;
}

qboolean CL_DemoSeekActive(void)
{
	if (!cls.demoplayback)
		return false;
	if (demo_restart_pending || is_marker_seeking || is_time_seeking || is_frame_seeking || is_seeking)
		return true;
	return fabsf(cls.demospeed) > 8.f;
}

/*
===============
CL_DemoSeekBackwardActive

True when rewind-via-snapshot-ring should pop frames to reach the seek
target. Like the forward predicate, this is position-vs-target rather than
flag-gated, so backward J-key time seeks (which don't set demo_seek_from_start)
also use the fast rewind.
===============
*/
static qboolean CL_DemoSeekBackwardActive(void)
{
	if (!cls.demoplayback || cls.signon < SIGNONS)
		return false;
	if (is_marker_seeking)
		return false; // marker seek only walks forward
	if (is_frame_seeking)
		return CL_GetDemoFrameCount() > demo_frame_seek_target;
	if (is_time_seeking)
		return cl.mtime[0] > demo_time_seek_target;
	if (is_seeking)
		return cls.demo_offset_current > demo_target_offset;
	return false;
}

/*
===============
CL_DemoSeekFastRewind

Tight loop variant of the existing rewind path: pops snapshot frames and
applies their inverse deltas until the seek target is crossed (or the demo's
start is reached). Avoids the "rewind to start, then replay forward" cost
that the slow path incurs because it only knows how to stop at backstop.

When the target is crossed, flips demo_seek_from_start off so the next host
frame's CL_DemoSeekFastForward catches up to the exact target.
===============
*/
static int CL_DemoSeekFastRewind(double deadline)
{
	const int seek_start = CL_GetDemoSeekStartOffset();
	int msgs = 0;
	int ret;

	while (Sys_DoubleTime() < deadline)
	{
		if (demo_rewind.backstop)
			break;
		if (cls.demo_offset_current <= seek_start)
			break;
		if (!CL_DemoSeekBackwardActive())
			break;

		Cbuf_Execute();

		// Cbuf_Execute may have run a stuffcmd that disconnected us; if so,
		// CL_GetMessage would now route to the network path with a NULL or
		// stale netcon. Re-check before touching it.
		if (!cls.demoplayback || cls.state != ca_connected || !cls.demofile)
			break;

		// Force the rewind branch in CL_NextDemoFrame, and open the rewind
		// gate in CL_GetDemoMessage by dropping cl.time below cl.mtime[0]
		// (gate during rewind: "no message yet" while cl.time >= cl.mtime[0]).
		cls.demospeed = -1e9f;
		if (cls.basedemospeed)
			cls.demospeed *= cls.basedemospeed;
		cl.oldtime = cl.time;
		cl.time = cl.mtime[0] - 1.0;

		ret = CL_GetMessage();
		if (ret == -1)
			Host_Error("CL_DemoSeekFastRewind: lost server connection");
		if (!ret)
			break;

		cl.last_received_message = realtime;
		CL_ParseServerMessage();
		msgs++;

		if (!cls.demoplayback || cls.state != ca_connected || cls.signon < SIGNONS)
			break;
	}

	if (msgs > 0)
	{
		// Drain queued sounds but preserve static/ambient sounds tied to the
		// world (svc_spawnstaticsound entries don't get re-emitted on render
		// resume).
		S_StopAllSounds(true, true);
		cl.time = cl.mtime[0];

		// Each normal rewind iteration reads its frame (file ends past it) and
		// then undoes that frame's state via CL_FinishDemoFrame, so after any
		// partial batch the game state is pre-frame while stdio is naturally
		// post-frame. Keep the file position aligned with the current state in
		// case the seek is interrupted or changes direction before the next
		// rewind batch. The backstop frame is special: CL_FinishDemoFrame
		// intentionally leaves the first frame applied, so stdio's post-frame
		// position already matches the client state.
		if (!demo_rewind.backstop && cls.demofile)
			fseek(cls.demofile, cls.demo_offset_current, SEEK_SET);

		if (demo_rewind.backstop ||
			cls.demo_offset_current <= seek_start ||
			!CL_DemoSeekBackwardActive())
		{
			// Clear demo_seek_from_start so the slow-path UpdateDemoSpeed
			// doesn't keep firing the "rewind to start" branch, and let
			// CL_DemoSeekFastForward handle the final approach if we
			// overshot the target.
			demo_seek_from_start = false;
			cls.demospeed = cls.basedemospeed * !cls.demopaused;
		}
	}

	return msgs;
}

/*
===============
CL_DemoSeekFastForward

Tight loop that reads and parses demo messages without rendering, sound
mixing, or particle stepping. Bounded by deadline so the host loop can still
draw progress between batches.
===============
*/
static int CL_DemoSeekFastForward(double deadline)
{
	int msgs = 0;
	int ret;

	while (Sys_DoubleTime() < deadline)
	{
		if (!CL_DemoSeekForwardActive())
			break;

		// drain any svc_stufftext commands parsed in the previous iteration so
		// they take effect before we read another batch of messages
		Cbuf_Execute();

		// Cbuf_Execute may have run a stuffcmd that disconnected us; if so,
		// CL_GetMessage would now route to the network path with a NULL or
		// stale netcon. Re-check before touching it.
		if (!cls.demoplayback || cls.state != ca_connected || !cls.demofile)
			break;

		// Force positive demospeed so CL_GetDemoMessage's `!cls.demospeed`
		// early-out doesn't trip when seeking while paused (basedemospeed *
		// !demopaused = 0 leaves demospeed at 0 from the previous frame's
		// CL_UpdateDemoSpeed). Mirror what the rewind side already does.
		cls.demospeed = 1e9f;
		if (cls.basedemospeed)
			cls.demospeed *= cls.basedemospeed;

		// open the gate inside CL_GetDemoMessage: it only returns a message
		// when cl.time has advanced past the next frame's server timestamp
		cl.oldtime = cl.time;
		cl.time = cl.mtime[0] + 1.0;

		ret = CL_GetMessage();
		if (ret == -1)
			Host_Error("CL_DemoSeekFastForward: lost server connection");
		if (!ret)
			break;

		cl.last_received_message = realtime;
		CL_ParseServerMessage();
		msgs++;

		// bail out the moment anything wants attention from the full host loop:
		// map change, demo end, disconnect, or the seek state machine cleared
		// itself (e.g. exact frame target reached in CL_GetDemoMessage)
		if (!cls.demoplayback || cls.state != ca_connected || cls.signon < SIGNONS)
			break;
	}

	if (msgs > 0)
	{
		// Channels accumulate svc_sound/svc_temp_entity output during the pump
		// with no S_Update in between; drain them so the user doesn't hear a
		// burst of stale sounds when rendering resumes. Keep statics so
		// world-attached ambient sounds (svc_spawnstaticsound) survive.
		S_StopAllSounds(true, true);

		// Keep cl.time consistent with what we last parsed so the normal frame
		// that follows interpolates from a sane baseline.
		cl.time = cl.mtime[0];
	}

	return msgs;
}

/*
===============
CL_DemoSeekFastPump

Single host-loop hook for demo seeking. Processes queued clean restarts for
0/Home, then dispatches to the rewind or forward variant; it may run both in
sequence within one budget window when the rewind crosses its target and hands
off.
===============
*/
qboolean CL_DemoSeekFastPump(void)
{
	// Bigger budget = fewer host-frame round-trips for medium-to-large seeks.
	// The render skip in _Host_Frame means each pump iteration owns most of
	// the wall clock, so a 250ms cap lets common 1-3 minute jumps finish in
	// a single pump and feel instant; very large jumps still chunk across
	// multiple frames.
	const double pump_budget = 0.250;
	double	deadline;
	int		msgs = 0;

	deadline = Sys_DoubleTime() + pump_budget;

	if (CL_DemoProcessRestartRequest())
	{
		demo_seek_frame_activity = true;
		return true;
	}

	if (CL_DemoSeekBackwardActive())
		msgs += CL_DemoSeekFastRewind(deadline);
	if (CL_DemoSeekForwardActive())
		msgs += CL_DemoSeekFastForward(deadline);

	if (msgs > 0)
		demo_seek_frame_activity = true;

	return msgs > 0;
}

static qboolean CL_DemoSeekRewindFromStart(int demo_seek_start, float seeking_speed)
{
	if (!demo_seek_from_start)
		return false;

	if (demo_rewind.backstop ||
		cls.demo_offset_current <= demo_seek_start ||
		!CL_DemoSeekBackwardActive())
	{
		// Mirror CL_DemoSeekFastRewind's exit fseek: when the slow inner
		// loop overshoots the target (state pre-frame, stdio post-frame),
		// the next forward read would skip a frame. Backstop and seek_start
		// already have file/state aligned, so only fseek on target crossing.
		if (!demo_rewind.backstop && cls.demo_offset_current > demo_seek_start && cls.demofile)
			fseek(cls.demofile, cls.demo_offset_current, SEEK_SET);
		demo_seek_from_start = false;
		return false;
	}

	demo_seek_frame_activity = true;
	cls.demospeed = -seeking_speed;
	if (cls.basedemospeed)
		cls.demospeed *= cls.basedemospeed;
	return true;
}

static void CL_DemoSeekFinish(float normal_speed)
{
	cls.demospeed = normal_speed;
	if (cls.demospeed > 0.f)
		demo_rewind.backstop = false;
	CL_ResetDemoSeekState();
	SCR_ShowDemoBar (); // woods #democontrols -- ensure the bar reappears at the landing spot
}

static qboolean CL_DemoQueueRestart(void)
{
	const char *restart_name = cls.demofilename[0] ? cls.demofilename : cls.demoname;

	if (!cls.demoplayback || !restart_name[0])
		return false;

	CL_ClearDemoMarkerHistory();
	CL_ResetDemoSeekState();
	q_strlcpy(demo_restart_name, restart_name, sizeof(demo_restart_name));
	demo_restart_pending = true;
	cls.demospeed = 0.f;
	return true;
}

static qboolean CL_DemoSeekToTime(float absolute_time)
{
	const float seek_time_threshold = 0.05f;
	const float current_demo_time = cl.mtime[0];

	if (!cls.demoplayback || !initialized)
		return false;

	if (demo_start_server_time_valid && absolute_time < demo_start_server_time)
		absolute_time = demo_start_server_time;

	if (fabsf(absolute_time - current_demo_time) <= seek_time_threshold)
		return false;

	CL_ClearDemoRestartRequest();
	CL_ClearDemoMarkerHistory();
	CL_ResetDemoSeekState();

	if (demo_start_server_time_valid &&
		absolute_time <= demo_start_server_time + seek_time_threshold &&
		current_demo_time > demo_start_server_time + seek_time_threshold &&
		CL_DemoQueueRestart())
		return true;

	demo_time_seek_target = absolute_time;
	demo_time_seek_direction = (absolute_time >= current_demo_time) ? 1 : -1;
	is_time_seeking = true;
	if (absolute_time < current_demo_time)
	{
		demo_seek_from_start = true;
		CL_ClearDemoFrags();
	}
	return true;
}

qboolean CL_DemoSeekRelativeSeconds(float seconds)
{
	const float base_time = is_time_seeking ? demo_time_seek_target : cl.mtime[0];

	if (!cls.demoplayback || !initialized || !demo_start_server_time_valid)
		return false;

	return CL_DemoSeekToTime(base_time + seconds);
}

/*
===============
CL_UpdateDemoSpeed
===============
*/
static void CL_UpdateDemoSpeed(void)
{
	int adjust, singleframe;
	const float normal_speed = cls.basedemospeed * !cls.demopaused;
	const qboolean jump_back_down = keydown['j'];
	const qboolean jump_forward_down = keydown['l'];
	const qboolean jump_back_pressed = jump_back_down && !demo_jump_back_was_down;
	const qboolean jump_forward_pressed = jump_forward_down && !demo_jump_forward_was_down;
	const qboolean jump_home_down = keydown[K_HOME] || keydown['0'];
	const qboolean jump_end_down = keydown[K_END];
	const qboolean jump_home_pressed = jump_home_down && !demo_jump_home_was_down;
	const qboolean jump_end_pressed = jump_end_down && !demo_jump_end_was_down;

	demo_jump_back_was_down = jump_back_down;
	demo_jump_forward_was_down = jump_forward_down;
	demo_jump_home_was_down = jump_home_down;
	demo_jump_end_was_down = jump_end_down;

	const int dynamic_threshold = CL_DemoSeekOffsetThreshold(); // 3% of the demo file length, with a minimum of 100 for very small files
	const float seeking_speed = 256;
	const float seek_time_step = 10.f;
	const float seek_time_threshold = 0.05f;
	const int demo_seek_start = CL_GetDemoSeekStartOffset();
	const int demo_seek_end = CL_GetDemoSeekEndOffset();

	if (key_dest == key_game && jump_back_pressed != jump_forward_pressed)
	{
		const float jump_delta = jump_forward_pressed ? seek_time_step : -seek_time_step;
		const float current_demo_time = cl.mtime[0];
		const float base_time = is_time_seeking ? demo_time_seek_target : current_demo_time;

		CL_ClearDemoRestartRequest();
		CL_ClearDemoMarkerHistory();
		CL_ResetDemoSeekState();
		demo_time_seek_target = q_max(0.f, base_time + jump_delta);
		if (demo_start_server_time_valid && demo_time_seek_target < demo_start_server_time)
			demo_time_seek_target = demo_start_server_time;
		demo_time_seek_direction = (demo_time_seek_target >= current_demo_time) ? 1 : -1;

		if (fabsf(demo_time_seek_target - current_demo_time) > seek_time_threshold)
		{
			if (demo_start_server_time_valid &&
				demo_time_seek_target <= demo_start_server_time + seek_time_threshold &&
				current_demo_time > demo_start_server_time + seek_time_threshold &&
				CL_DemoQueueRestart())
				return;
			is_time_seeking = true;
			if (demo_time_seek_target < current_demo_time)
				CL_ClearDemoFrags();
		}
	}

	if (is_marker_seeking)
	{
		if (!demo_marker_found)
		{
			demo_seek_frame_activity = true;
			cls.demospeed = seeking_speed;
			demo_rewind.backstop = false;
			return;
		}

		is_marker_seeking = false;
		demo_marker_found = false;
		CL_DemoSeekFinish(normal_speed);
		return;
	}

	if (is_time_seeking)
	{
		if (CL_DemoSeekRewindFromStart(demo_seek_start, seeking_speed))
			return;

		const float remaining = demo_time_seek_target - cl.mtime[0];

		if (fabsf(remaining) <= seek_time_threshold ||
			(demo_time_seek_direction > 0 && remaining <= 0.f) ||
			(demo_time_seek_direction < 0 && remaining >= 0.f) ||
			(demo_rewind.backstop && remaining < 0.f))
		{
			CL_DemoSeekFinish(normal_speed);
			return;
		}

		demo_seek_frame_activity = true;
		cls.demospeed = (demo_time_seek_direction > 0) ? seeking_speed : -seeking_speed;
		if (cls.basedemospeed)
			cls.demospeed *= cls.basedemospeed;

		if (cls.demospeed > 0.f)
			demo_rewind.backstop = false;
		return;
	}

	if (is_frame_seeking)
	{
		if (CL_DemoSeekRewindFromStart(demo_seek_start, seeking_speed))
			return;

		if (CL_GetDemoFrameCount() >= demo_frame_seek_target)
		{
			CL_DemoSeekFinish(normal_speed);
			return;
		}

		demo_seek_frame_activity = true;
		cls.demospeed = seeking_speed;
		demo_rewind.backstop = false;
		return;
	}

	if (is_seeking) 
	{
		if (CL_DemoSeekRewindFromStart(demo_seek_start, seeking_speed))
			return;

		if (demo_target_offset < demo_seek_start)
			demo_target_offset = demo_seek_start;
		else if (demo_target_offset > demo_seek_end)
			demo_target_offset = demo_seek_end;

		if (abs(cls.demo_offset_current - demo_target_offset) < dynamic_threshold ||
			cls.demo_offset_current >= demo_target_offset)
		{
			CL_DemoSeekFinish(normal_speed);
			return;
		}

		demo_seek_frame_activity = true;
		cls.demospeed = seeking_speed;
		demo_rewind.backstop = false;
		return;
	}

	if (key_dest != key_game)
	{
		cls.demospeed = normal_speed;
		return;
	}

	// 0/Home restarts the current demo through the normal playdemo setup path.
	// Seeking to the first rewind frame is fragile because the first stored
	// frame intentionally has no inverse state to apply; FTEQW handles backward
	// start jumps by restarting the stream for the same reason.
	if (jump_home_pressed)
	{
		if (CL_DemoQueueRestart())
			return;
	}
	else if (jump_end_pressed)
	{
		CL_ClearDemoRestartRequest();
		CL_ClearDemoMarkerHistory();
		CL_ResetDemoSeekState();
		demo_target_offset = demo_seek_end;
		if (cls.demo_offset_current < demo_target_offset)
		{
			demo_seek_from_start = false;
			is_seeking = true;
			cls.demospeed = 0.f;
			return;
		}
	}

	adjust = (keydown[K_RIGHTARROW] || keydown[K_DPAD_RIGHT]) -
		(keydown[K_LEFTARROW] || keydown[K_DPAD_LEFT]);
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
	else
	{
		cls.demospeed = normal_speed;
	}

	if (Key_IsShortcutModifierDown())
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
			CL_DemoRewindFreePrevStatStrings();
			demo_rewind.prev.num_entities = 0;
		}
		else
		{
			demoframe_t newframe;
			size_t snap_num_entities;

			memset(&newframe, 0, sizeof(newframe));
			newframe.fileofs = ftell(cls.demofile);
			newframe.intermission = cl.intermission;
			//	newframe.forceunderwater = cl.forceunderwater;
			VEC_PUSH(demo_rewind.frames, newframe);

			// Take a snapshot of the tracked data at the beginning of this frame
			for (i = 0; i < MAX_LIGHTSTYLES; i++)
				q_strlcpy(demo_rewind.prev.lightstyles[i], cl_lightstyle[i].map, MAX_STYLESTRING);
			memcpy(&demo_rewind.prev.cshift, &cshift_empty, sizeof(cshift_empty));
			memcpy(demo_rewind.prev.stats, cl.stats, sizeof(cl.stats));
			memcpy(demo_rewind.prev.statsf, cl.statsf, sizeof(cl.statsf));
			for (i = 0; i < MAX_CL_STATS; i++)
				CL_DemoRewindSetPrevStatString(i, cl.statss[i]);

			snap_num_entities = (size_t)cl.num_entities;
			if (snap_num_entities > MAX_EDICTS)
				snap_num_entities = MAX_EDICTS;
			demo_rewind.prev.num_entities = snap_num_entities;

			for (i = 1; i < snap_num_entities; i++)
				CL_DemoRewindSnapshotEntity(&demo_rewind.prev.entity_states[i], &cl.entities[i]);
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
	if (cls.demospeed > 0.f && !demo_start_server_time_valid && numframes > 0)
	{
		demo_start_server_time = cl.mtime[0];
		demo_start_server_time_valid = true;
	}

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

		// Save the previous value for any changed numeric stats
		for (i = 0; i < MAX_CL_STATS; i++)
		{
			demonumericstat_t prevstat;

			if (demo_rewind.prev.stats[i] == cl.stats[i] &&
				demo_rewind.prev.statsf[i] == cl.statsf[i])
				continue;

			prevstat.int_value = demo_rewind.prev.stats[i];
			prevstat.float_value = demo_rewind.prev.statsf[i];

			VEC_PUSH(demo_rewind.frame_events, DFE_STAT);
			VEC_PUSH(demo_rewind.frame_events, (byte)i);
			Vec_Append((void**)&demo_rewind.frame_events, 1, &prevstat, sizeof(prevstat));
			lastframe->datasize += 1 + 1 + sizeof(prevstat);

			demo_rewind.prev.stats[i] = cl.stats[i];
			demo_rewind.prev.statsf[i] = cl.statsf[i];
		}

		// Save the previous value for any changed string stats
		for (i = 0; i < MAX_CL_STATS; i++)
		{
			const char *prevstr = demo_rewind.prev.statss[i];
			const char *curstr = cl.statss[i];
			size_t strlen_prev;
			uint32_t strlen_prev32;

			if ((!prevstr && !curstr) ||
				(prevstr && curstr && strcmp(prevstr, curstr) == 0))
				continue;

			strlen_prev = prevstr ? strlen(prevstr) : 0;
			if (strlen_prev > 0xFFFFFFFFu)
				Sys_Error("CL_FinishDemoFrame: stat string too large");
			strlen_prev32 = (uint32_t)strlen_prev;

			VEC_PUSH(demo_rewind.frame_events, DFE_STATSTR);
			VEC_PUSH(demo_rewind.frame_events, (byte)i);
			Vec_Append((void**)&demo_rewind.frame_events, 1, &strlen_prev32, sizeof(strlen_prev32));
			if (strlen_prev)
				Vec_Append((void**)&demo_rewind.frame_events, 1, prevstr, strlen_prev);
			lastframe->datasize += 1 + 1 + sizeof(strlen_prev32) + strlen_prev;

			CL_DemoRewindSetPrevStatString(i, curstr);
		}

		// Save previous state for any changed entities (including create/remove)
		{
			size_t cur_num_entities = (size_t)cl.num_entities;
			size_t max_num_entities;
			demoentitystate_t empty_state;

			if (cur_num_entities > MAX_EDICTS)
				cur_num_entities = MAX_EDICTS;
			max_num_entities = q_max(demo_rewind.prev.num_entities, cur_num_entities);
			memset(&empty_state, 0, sizeof(empty_state));

			for (i = 1; i < max_num_entities; i++)
			{
				const demoentitystate_t *prevstate;
				demoentitystate_t curstate;

				prevstate = (i < demo_rewind.prev.num_entities) ?
					&demo_rewind.prev.entity_states[i] : &empty_state;

				memset(&curstate, 0, sizeof(curstate));
				if (i < cur_num_entities)
					CL_DemoRewindSnapshotEntity(&curstate, &cl.entities[i]);

				if (memcmp(prevstate, &curstate, sizeof(curstate)) != 0)
				{
					unsigned short entnum = (unsigned short)i;
					VEC_PUSH(demo_rewind.frame_events, DFE_ENTITY);
					Vec_Append((void**)&demo_rewind.frame_events, 1, &entnum, sizeof(entnum));
					Vec_Append((void**)&demo_rewind.frame_events, 1, prevstate, sizeof(*prevstate));
					lastframe->datasize += 1 + sizeof(entnum) + sizeof(*prevstate);
				}
			}
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
			size_t begin;

			if (lastframe->datasize > end)
				Sys_Error("CL_FinishDemoFrame: invalid event span");
			begin = end - lastframe->datasize;

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

				case DFE_STAT:
				{
					int stat_idx = *data++;
					demonumericstat_t stat;

					memcpy(&stat, data, sizeof(stat));
					CL_DemoRewindApplyNumericStat(stat_idx, stat.int_value, stat.float_value);

					begin += 1 + sizeof(stat);
				}
				break;

				case DFE_STATSTR:
				{
					int stat_idx = *data++;
					uint32_t str_len32;
					size_t str_len;
					char *str = NULL;
					size_t remaining = end - begin;

					if (remaining < 1 + sizeof(str_len32))
						Sys_Error("CL_FinishDemoFrame: bad stat string event size");

					memcpy(&str_len32, data, sizeof(str_len32));
					str_len = (size_t)str_len32;
					data += sizeof(str_len32);
					if (str_len > remaining - (1 + sizeof(str_len32)))
						Sys_Error("CL_FinishDemoFrame: bad stat string length");
					if (str_len)
					{
						str = (char *)malloc(str_len + 1);
						if (!str)
							Sys_Error("CL_FinishDemoFrame: failed to alloc stat string");
						memcpy(str, data, str_len);
						str[str_len] = '\0';
					}

					if (stat_idx >= 0 && stat_idx < MAX_CL_STATS)
					{
						free(cl.statss[stat_idx]);
						cl.statss[stat_idx] = str;
						Sbar_Changed();
					}
					else
					{
						free(str);
					}

					begin += 1 + sizeof(str_len32) + str_len;
				}
				break;

				case DFE_ENTITY:
				{
					unsigned short entnum;
					demoentitystate_t entstate;

					memcpy(&entnum, data, sizeof(entnum));
					data += sizeof(entnum);
					memcpy(&entstate, data, sizeof(entstate));

					if (entnum >= 1 && entnum < cl.max_edicts)
					{
						entity_t *ent = CL_EntityNum(entnum);
						CL_DemoRewindApplyEntity(&entstate, ent);
					}
					begin += sizeof(entnum) + sizeof(entstate);
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
					Sys_Error("CL_FinishDemoFrame: bad event type %d", datatype);
					break;
				}
			}

			SDL_assert(begin == end);

			VEC_POP_N(demo_rewind.frame_events, lastframe->datasize);
			lastframe->datasize = 0;

			InvalidateTraceLineCache();
		}

		if (cl.intermission != lastframe->intermission && !lastframe->intermission)
			cl.completed_time = 0;
		cl.intermission = lastframe->intermission;
		//cl.forceunderwater = lastframe->forceunderwater;

		cl.faceanimtime = 0; // woods
		memset(cl_dlights, 0, sizeof(cl_dlights)); // woods
		//memset(cl_temp_entities, 0, sizeof(cl_temp_entities));

		// Reset viewmodel state to ensure it updates immediately
		cl.viewent.model = NULL;
		cl.viewent.frame = 0;
		cl.viewent.lerpflags |= LERP_RESETANIM | LERP_RESETMOVE;

		VEC_POP(demo_rewind.frames);
	}
}

void Log_Last_Demo_f (void) // woods #lastdemo
{
	FILE* f;
	char demodir[MAX_OSPATH];

	q_snprintf(demodir, sizeof(demodir), "%s/id1/backups", com_basedir);
	Sys_mkdir(demodir);

	f = fopen(va("%s/id1/backups/%s.txt", com_basedir, "lastdemo"), "w");

	if (!f)
	{
		Con_Printf("Couldn't write backup last demo\n");
		return;
	}

	fprintf(f, "%s", last_demo);

	fclose(f);
}

void Load_Last_Demo (void) // woods #lastdemo
{
	FILE* f;
	char demodir[MAX_OSPATH];

	q_snprintf(demodir, sizeof(demodir), "%s/id1/backups/lastdemo.txt", com_basedir);

	f = fopen(demodir, "r");
	if (!f)
		return;

	if (fgets(last_demo, sizeof(last_demo), f))
	{
		// Remove any trailing newline
		size_t len = strlen(last_demo);
		if (len > 0 && last_demo[len - 1] == '\n')
			last_demo[len - 1] = '\0';
	}

	fclose(f);
}

static void CL_DemoMarkHistory_ClearList(demomark_history_entry_t **head, demomark_history_entry_t **tail)
{
	demomark_history_entry_t *entry = head ? *head : NULL;

	while (entry)
	{
		demomark_history_entry_t *next = entry->next;
		free(entry);
		entry = next;
	}

	if (head)
		*head = NULL;
	if (tail)
		*tail = NULL;
}

static qboolean CL_DemoMarkHistory_Append(demomark_history_entry_t **head, demomark_history_entry_t **tail,
	const char *demo, const char *map, const char *created_at, int frame)
{
	demomark_history_entry_t *entry = (demomark_history_entry_t *)calloc(1, sizeof(*entry));

	if (!entry)
		return false;

	if (demo)
		q_strlcpy(entry->demo, demo, sizeof(entry->demo));
	if (map)
		q_strlcpy(entry->map, map, sizeof(entry->map));
	if (created_at)
		q_strlcpy(entry->created_at, created_at, sizeof(entry->created_at));
	entry->frame = (frame > 0) ? frame : 0;

	if (*tail)
		(*tail)->next = entry;
	else
		*head = entry;
	*tail = entry;

	return true;
}

static void CL_DemoMarkHistory_EnsureDir(void)
{
	char path[MAX_OSPATH];

	q_snprintf(path, sizeof(path), "%s/id1", com_basedir);
	Sys_mkdir(path);

	q_snprintf(path, sizeof(path), "%s/id1/backups", com_basedir);
	Sys_mkdir(path);
}

static void CL_DemoMarkHistory_GetPath(char *path, size_t path_size)
{
	q_snprintf(path, path_size, "%s/id1/backups/%s", com_basedir, DEMOMARK_HISTORY_FILENAME);
}

static qboolean CL_DemoMarkHistory_ClearStored(void)
{
	char path[MAX_OSPATH];

	CL_DemoMarkHistory_GetPath(path, sizeof(path));
	CL_DemoMarkHistory_ClearPending();

	if (remove(path) == 0 || errno == ENOENT)
		return true;

	return false;
}

static void CL_DemoMarkHistory_GetTimestamp(char *created_at, size_t created_at_size)
{
	time_t systime = time(NULL);
	struct tm *loct;

	if (!created_at_size)
		return;

	created_at[0] = '\0';
	loct = localtime(&systime);
	if (!loct)
	{
		q_strlcpy(created_at, "unknown", created_at_size);
		return;
	}

	strftime(created_at, created_at_size, "%Y-%m-%d %H:%M:%S", loct);
}

static demomark_history_entry_t *CL_DemoMarkHistory_Load(demomark_history_entry_t **tail_out)
{
	char path[MAX_OSPATH];
	FILE *file;
	long file_size;
	char *buffer;
	json_t *json;
	demomark_history_entry_t *head = NULL;
	demomark_history_entry_t *tail = NULL;

	if (tail_out)
		*tail_out = NULL;

	CL_DemoMarkHistory_EnsureDir();
	CL_DemoMarkHistory_GetPath(path, sizeof(path));

	file = fopen(path, "rb");
	if (!file)
		return NULL;

	fseek(file, 0, SEEK_END);
	file_size = ftell(file);
	rewind(file);

	if (file_size <= 0)
	{
		fclose(file);
		return NULL;
	}

	buffer = (char *)malloc((size_t)file_size + 1);
	if (!buffer)
	{
		fclose(file);
		return NULL;
	}

	if (fread(buffer, 1, (size_t)file_size, file) != (size_t)file_size)
	{
		free(buffer);
		fclose(file);
		return NULL;
	}

	buffer[file_size] = '\0';
	fclose(file);

	json = JSON_Parse(buffer);
	free(buffer);

	if (!json || !json->root || json->root->type != JSON_ARRAY)
	{
		if (json)
			JSON_Free(json);
		return NULL;
	}

	{
		const jsonentry_t *entry;

		for (entry = json->root->firstchild; entry; entry = entry->next)
		{
			const char *demo;
			const char *map;
			const char *created_at;
			const double *frame_value;
			int frame_num;

			if (!entry || entry->type != JSON_OBJECT)
				continue;

			demo = JSON_FindString(entry, "demo");
			map = JSON_FindString(entry, "map");
			created_at = JSON_FindString(entry, "created_at");
			frame_value = JSON_FindNumber(entry, "frame");
			frame_num = (frame_value && *frame_value > 0.0 && *frame_value <= (double)INT_MAX) ? (int)(*frame_value) : 0;

			if (!demo || !map || !created_at)
				continue;

			if (!CL_DemoMarkHistory_Append(&head, &tail, demo, map, created_at, frame_num))
			{
				Con_Printf("markdemo: failed to load full history\n");
				break;
			}
		}
	}

	JSON_Free(json);
	if (tail_out)
		*tail_out = tail;
	return head;
}

static void CL_DemoMarkHistory_Write(const demomark_history_entry_t *head)
{
	char path[MAX_OSPATH];
	char tmp_path[MAX_OSPATH];
	FILE *file;
	qboolean ok = true;
	qboolean first = true;
	const demomark_history_entry_t *entry;

	CL_DemoMarkHistory_EnsureDir();
	CL_DemoMarkHistory_GetPath(path, sizeof(path));
	q_snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

	file = fopen(tmp_path, "w");
	if (!file)
	{
		Con_Printf("markdemo: unable to open %s for writing\n", DEMOMARK_HISTORY_FILENAME);
		return;
	}

	fprintf(file, "[\n");

	for (entry = head; entry; entry = entry->next)
	{
		char *escaped_demo = JSON_EscapeString(entry->demo);
		char *escaped_map = JSON_EscapeString(entry->map);
		char *escaped_created_at = JSON_EscapeString(entry->created_at);

		if (!escaped_demo || !escaped_map || !escaped_created_at)
		{
			free(escaped_demo);
			free(escaped_map);
			free(escaped_created_at);
			ok = false;
			break;
		}

		if (!first)
			fprintf(file, ",\n");
		first = false;

		fprintf(file, "  {\n");
		fprintf(file, "    \"demo\": \"%s\",\n", escaped_demo);
		fprintf(file, "    \"map\": \"%s\",\n", escaped_map);
		fprintf(file, "    \"frame\": %d,\n", entry->frame);
		fprintf(file, "    \"created_at\": \"%s\"\n", escaped_created_at);
		fprintf(file, "  }");

		free(escaped_demo);
		free(escaped_map);
		free(escaped_created_at);
	}

	if (ok)
	{
		if (!first)
			fprintf(file, "\n");
		fprintf(file, "]\n");
	}

	if (fclose(file) != 0)
		ok = false;

	if (!ok)
	{
		Con_Printf("markdemo: failed to flush %s, preserving existing file\n", DEMOMARK_HISTORY_FILENAME);
		remove(tmp_path);
		return;
	}

	remove(path);
	if (rename(tmp_path, path) != 0)
	{
		Con_Printf("markdemo: unable to replace %s\n", DEMOMARK_HISTORY_FILENAME);
		remove(tmp_path);
	}
}

static void CL_DemoMarkHistory_ClearPending(void)
{
	CL_DemoMarkHistory_ClearList(&demomark_pending_head, &demomark_pending_tail);
}

static void CL_DemoMarkHistory_QueuePending(const char *map, int frame)
{
	char created_at[DEMOMARK_HISTORY_TIME_LENGTH];
	const char *safe_map = (map && map[0]) ? map : "unknown";

	CL_DemoMarkHistory_GetTimestamp(created_at, sizeof(created_at));
	if (!CL_DemoMarkHistory_Append(&demomark_pending_head, &demomark_pending_tail, "", safe_map, created_at, frame))
		Con_Printf("markdemo: failed to queue history entry\n");
}

static void CL_DemoMarkHistory_FlushList(demomark_history_entry_t **pending_head,
	demomark_history_entry_t **pending_tail, const char *demo_path)
{
	demomark_history_entry_t *head = NULL;
	demomark_history_entry_t *tail = NULL;
	demomark_history_entry_t *pending;
	const char *demo_name;

	if (!pending_head || !*pending_head)
		return;

	head = CL_DemoMarkHistory_Load(&tail);
	demo_name = (demo_path && demo_path[0]) ? COM_SkipPath(demo_path) : "unknown.dem";

	for (pending = *pending_head; pending; pending = pending->next)
	{
		if (!CL_DemoMarkHistory_Append(&head, &tail, demo_name, pending->map, pending->created_at, pending->frame))
		{
			Con_Printf("markdemo: failed to save full history\n");
			break;
		}
	}

	CL_DemoMarkHistory_Write(head);
	CL_DemoMarkHistory_ClearList(&head, &tail);
	CL_DemoMarkHistory_ClearList(pending_head, pending_tail);
}

static void CL_DemoMarkHistory_FlushPending(const char *demo_path)
{
	CL_DemoMarkHistory_FlushList(&demomark_pending_head, &demomark_pending_tail, demo_path);
}

static void CL_DemoMarkHistory_Print(const char *map_filter)
{
	demomark_history_entry_t *head;
	demomark_history_entry_t *tail = NULL;
	demomark_history_entry_t *entry;
	const demomark_history_entry_t *match;
	const demomark_history_entry_t **matches = NULL;
	const qboolean filtered = map_filter && map_filter[0];
	size_t match_count = 0;
	size_t match_index = 0;
	size_t i;

	head = CL_DemoMarkHistory_Load(&tail);
	if (!head)
	{
		if (filtered)
			Con_Printf("| no markdemo history for %s\n", map_filter);
		else
			Con_Printf("| no markdemo history yet\n");
		return;
	}

	for (entry = head; entry; entry = entry->next)
	{
		if (filtered && q_strcasecmp(entry->map, map_filter))
			continue;
		match_count++;
	}

	if (!match_count)
	{
		if (filtered)
			Con_Printf("| no markdemo history for %s\n", map_filter);
		else
			Con_Printf("| no markdemo history yet\n");
		CL_DemoMarkHistory_ClearList(&head, &tail);
		return;
	}

	matches = (const demomark_history_entry_t **)calloc(match_count, sizeof(*matches));
	if (!matches)
	{
		Con_Printf("markdemo: failed to format history\n");
		CL_DemoMarkHistory_ClearList(&head, &tail);
		return;
	}

	for (entry = head; entry; entry = entry->next)
	{
		if (filtered && q_strcasecmp(entry->map, map_filter))
			continue;
		matches[match_index++] = entry;
	}

	Con_Printf("\n");
	Con_Printf("^mtime^m                 ^mframe^m  ^mmap^m       ^mdemo^m\n");

	for (i = 0; i < match_count; i++)
	{
		char frame_text[16];

		match = matches[i];
		if (match->frame > 0)
			q_snprintf(frame_text, sizeof(frame_text), "%d", match->frame);
		else
			q_strlcpy(frame_text, "-", sizeof(frame_text));

		Con_Printf("%-19s  %-5s  %-8s  %s\n",
			match->created_at[0] ? match->created_at : "unknown",
			frame_text,
			match->map[0] ? match->map : "-",
			match->demo[0] ? match->demo : "-");
	}
	Con_Printf("\n");

	free(matches);
	CL_DemoMarkHistory_ClearList(&head, &tail);
}

/*
==============
CL_StopPlayback

Called when a demo file runs out, or the user starts a game.

Abnormal demo reads must terminate through Host_EndGame after clearing the
playlist and net message state; callers must not return to the host frame.
==============
*/
void CL_StopPlayback (void)
{
	CL_DemoScrub_Cancel();

	if (!cls.demoplayback)
	{
		cls.demoreelplayback = false;
#ifdef USE_ZLIB
		CL_DZipCleanupTempDemo();
#endif
		CL_ResetDemoSeekState();
		CL_ClearDemoMarkerHistory();
		CL_ClearDemoRestartRequest();
		demo_start_server_time = 0.f;
		demo_start_server_time_valid = false;
		demo_total_frame_count = 0;
		return;
	}

	// Also reached from CL_Disconnect; restore is idempotent.
	Cvar_MapLock_RestoreAll ();
	V_MapScoped_RestoreServerStuff ();

	fclose (cls.demofile);
#ifdef USE_ZLIB
	CL_DZipCleanupTempDemo();
#endif
	cls.demoplayback = false;
	cls.demoreelplayback = false;
	cls.demopaused = false;
	cls.demospeed = 1.f; // woods (iw) #democontrols
	cls.demofile = NULL;
	cls.demofilesize = 0; // woods (iw) #democontrols
	cls.demofilestart = 0; // woods (iw) #democontrols
	cls.state = ca_disconnected;

	VEC_CLEAR(demo_rewind.frames); // woods (iw) #democontrols
	VEC_CLEAR(demo_rewind.frame_events); // woods (iw) #democontrols
	VEC_CLEAR(demo_rewind.pending_sounds); // woods (iw) #democontrols
	demo_rewind.backstop = false; // woods (iw) #democontrols
	CL_DemoRewindFreePrevStatStrings();
	demo_rewind.prev.num_entities = 0;
	CL_ResetDemoSeekState();
	CL_ClearDemoMarkerHistory();
	CL_ClearDemoRestartRequest();
	demo_start_server_time = 0.f;
	demo_start_server_time_valid = false;
	demo_total_frame_count = 0;

	if (cls.demofilename[0]) // woods #lastdemo
	{
		const char* demoname = cls.demofilename;
		if (demoname[0])
		{
			q_strlcpy(last_demo, demoname, sizeof(last_demo));
			Log_Last_Demo_f();
		}
	}
	cls.demofilename[0] = '\0'; // woods (iw) #democontrols

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
static int CL_DemoWriterThread(void *unused)
{
	(void)unused;

	for (;;)
	{
		demo_write_chunk_t *chunk;
		FILE *file;
		qboolean failed;

		SDL_LockMutex(demo_writer.mutex);
		while (!demo_writer.pending_head && !demo_writer.closing && !demo_writer.shutdown)
			SDL_CondWait(demo_writer.work_condition, demo_writer.mutex);

		chunk = demo_writer.pending_head;
		if (chunk)
		{
			demo_writer.pending_head = chunk->next;
			if (!demo_writer.pending_head)
				demo_writer.pending_tail = NULL;
			file = demo_writer.file;
			failed = demo_writer.write_failed;
			SDL_UnlockMutex(demo_writer.mutex);

			if (!failed && (!file || fwrite(chunk->data, 1, chunk->size, file) != chunk->size || fflush(file) != 0))
				failed = true;

			SDL_LockMutex(demo_writer.mutex);
			if (failed)
				demo_writer.write_failed = true;
			demo_writer.pending_bytes -= chunk->size;
			SDL_CondBroadcast(demo_writer.state_condition);
			SDL_UnlockMutex(demo_writer.mutex);
			free(chunk);
			continue;
		}

		if (demo_writer.active && (demo_writer.closing || demo_writer.shutdown))
		{
			file = demo_writer.file;
			demo_writer.file = NULL;
			SDL_UnlockMutex(demo_writer.mutex);

			failed = false;
			if (!file)
				failed = true;
			else
			{
				if (fflush(file) != 0)
					failed = true;
				if (fclose(file) != 0)
					failed = true;
			}

			SDL_LockMutex(demo_writer.mutex);
			if (failed)
				demo_writer.write_failed = true;
			demo_writer.active = false;
			demo_writer.closing = false;
			SDL_CondBroadcast(demo_writer.state_condition);
			SDL_UnlockMutex(demo_writer.mutex);
			continue;
		}

		if (demo_writer.shutdown)
		{
			SDL_UnlockMutex(demo_writer.mutex);
			break;
		}

		SDL_UnlockMutex(demo_writer.mutex);
	}

	return 0;
}

static void CL_DemoWriterInit(void)
{
	if (demo_writer.thread || demo_writer.mutex)
		return;

	demo_writer.mutex = SDL_CreateMutex();
	demo_writer.work_condition = SDL_CreateCond();
	demo_writer.state_condition = SDL_CreateCond();
	if (!demo_writer.mutex || !demo_writer.work_condition || !demo_writer.state_condition)
		goto fail;

	demo_writer.thread = SDL_CreateThread(CL_DemoWriterThread, "DemoWriter", NULL);
	if (!demo_writer.thread)
		goto fail;
	return;

fail:
	Con_DPrintf("Unable to start demo writer: %s\n", SDL_GetError());
	if (demo_writer.state_condition)
		SDL_DestroyCond(demo_writer.state_condition);
	if (demo_writer.work_condition)
		SDL_DestroyCond(demo_writer.work_condition);
	if (demo_writer.mutex)
		SDL_DestroyMutex(demo_writer.mutex);
	memset(&demo_writer, 0, sizeof(demo_writer));
}

static qboolean CL_DemoWriterStart(FILE *file)
{
	demo_writer_queue_error_reported = false;
	demo_record_stop_requested = false;

	if (!demo_writer.thread || !demo_writer.mutex)
		return true;

	SDL_LockMutex(demo_writer.mutex);
	if (demo_writer.active || demo_writer.shutdown)
	{
		SDL_UnlockMutex(demo_writer.mutex);
		return false;
	}

	demo_writer.file = file;
	demo_writer.active = true;
	demo_writer.closing = false;
	demo_writer.write_failed = false;
	SDL_CondSignal(demo_writer.work_condition);
	SDL_UnlockMutex(demo_writer.mutex);
	return true;
}

static qboolean CL_DemoWriterQueueParts(const byte *prefix, size_t prefix_size,
	const byte *payload, size_t payload_size, const vec3_t viewangles)
{
	demo_write_chunk_t *chunk;
	byte *dst;
	size_t data_size;
	size_t record_size;
	int little_size;
	int i;
	float little_angle;

	if ((!prefix && prefix_size) || (!payload && payload_size) ||
		prefix_size > INT_MAX || payload_size > (size_t)INT_MAX - prefix_size)
		return false;
	data_size = prefix_size + payload_size;
	record_size = 16 + data_size;
	if (record_size > DEMO_WRITE_QUEUE_BYTES)
		return false;

	chunk = (demo_write_chunk_t *)malloc(sizeof(*chunk) - sizeof(chunk->data) + record_size);
	if (!chunk)
		return false;

	chunk->next = NULL;
	chunk->size = record_size;
	dst = chunk->data;
	little_size = LittleLong((int)data_size);
	memcpy(dst, &little_size, sizeof(little_size));
	dst += sizeof(little_size);
	for (i = 0; i < 3; ++i)
	{
		little_angle = LittleFloat(viewangles[i]);
		memcpy(dst, &little_angle, sizeof(little_angle));
		dst += sizeof(little_angle);
	}
	if (prefix_size)
	{
		memcpy(dst, prefix, prefix_size);
		dst += prefix_size;
	}
	if (payload_size)
		memcpy(dst, payload, payload_size);

	if (!demo_writer.thread || !demo_writer.mutex)
	{
		qboolean ok = cls.demofile &&
			fwrite(chunk->data, 1, chunk->size, cls.demofile) == chunk->size &&
			fflush(cls.demofile) == 0;
		free(chunk);
		return ok;
	}

	SDL_LockMutex(demo_writer.mutex);
	while (demo_writer.active && !demo_writer.closing && !demo_writer.shutdown &&
		!demo_writer.write_failed &&
		demo_writer.pending_bytes + chunk->size > DEMO_WRITE_QUEUE_BYTES)
	{
		SDL_CondWait(demo_writer.state_condition, demo_writer.mutex);
	}

	if (!demo_writer.active || demo_writer.closing || demo_writer.shutdown || demo_writer.write_failed)
	{
		SDL_UnlockMutex(demo_writer.mutex);
		free(chunk);
		return false;
	}

	if (demo_writer.pending_tail)
		demo_writer.pending_tail->next = chunk;
	else
		demo_writer.pending_head = chunk;
	demo_writer.pending_tail = chunk;
	demo_writer.pending_bytes += chunk->size;
	SDL_CondSignal(demo_writer.work_condition);
	SDL_UnlockMutex(demo_writer.mutex);
	return true;
}

static qboolean CL_DemoWriterStop(void)
{
	qboolean ok;

	if (!demo_writer.thread || !demo_writer.mutex)
	{
		if (!cls.demofile)
			return false;
		ok = fflush(cls.demofile) == 0;
		if (fclose(cls.demofile) != 0)
			ok = false;
		return ok;
	}

	SDL_LockMutex(demo_writer.mutex);
	if (!demo_writer.active)
	{
		ok = !demo_writer.write_failed;
		SDL_UnlockMutex(demo_writer.mutex);
		return ok;
	}

	demo_writer.closing = true;
	SDL_CondSignal(demo_writer.work_condition);
	while (demo_writer.active)
		SDL_CondWait(demo_writer.state_condition, demo_writer.mutex);
	ok = !demo_writer.write_failed;
	SDL_UnlockMutex(demo_writer.mutex);
	return ok;
}

static void CL_DemoWriterShutdown(void)
{
	demo_write_chunk_t *chunk;

	if (!demo_writer.mutex)
	{
		if (cls.demorecording && cls.demofile)
			CL_DemoWriterStop();
		return;
	}

	SDL_LockMutex(demo_writer.mutex);
	demo_writer.shutdown = true;
	demo_writer.closing = demo_writer.active;
	SDL_CondBroadcast(demo_writer.work_condition);
	SDL_CondBroadcast(demo_writer.state_condition);
	SDL_UnlockMutex(demo_writer.mutex);

	if (demo_writer.thread)
		SDL_WaitThread(demo_writer.thread, NULL);

	while ((chunk = demo_writer.pending_head) != NULL)
	{
		demo_writer.pending_head = chunk->next;
		free(chunk);
	}

	SDL_DestroyCond(demo_writer.state_condition);
	SDL_DestroyCond(demo_writer.work_condition);
	SDL_DestroyMutex(demo_writer.mutex);
	memset(&demo_writer, 0, sizeof(demo_writer));
}

static void CL_WriteDemoMessageData(const byte *data, int cursize, const vec3_t viewangles)
{
	if (cursize >= 0 && CL_DemoWriterQueueParts(NULL, 0, data, (size_t)cursize, viewangles))
	{
		cls.demo_record_frame_count++;
		return;
	}

	if (!demo_writer_queue_error_reported)
	{
		demo_writer_queue_error_reported = true;
		Con_SafePrintf("WARNING: failed to write demo data; stopping recording\n");
	}
	if (cls.demorecording && !demo_record_stop_in_progress)
		demo_record_stop_requested = true;
}

void CL_WriteDemoVoiceMessage(byte command, byte codec_generation, byte sequence,
	const byte *data, size_t data_size)
{
	byte prefix[5];
	unsigned short little_size;

	if (!cls.demorecording || !cls.demofile || data_size > USHRT_MAX)
		return;

	prefix[0] = command;
	prefix[1] = codec_generation;
	prefix[2] = sequence;
	little_size = (unsigned short)LittleShort((short)data_size);
	memcpy(prefix + 3, &little_size, sizeof(little_size));

	if (CL_DemoWriterQueueParts(prefix, sizeof(prefix), data, data_size, cl.viewangles))
		return;

	if (!demo_writer_queue_error_reported)
	{
		demo_writer_queue_error_reported = true;
		Con_SafePrintf("WARNING: failed to write demo voice data; stopping recording\n");
	}
	if (cls.demorecording && !demo_record_stop_in_progress)
		demo_record_stop_requested = true;
}

static void CL_WriteDemoMessage (void)
{
	CL_WriteDemoMessageData(net_message.data, net_message.cursize, cl.viewangles);
}

static void CL_WriteRecordedDemoMarker(void)
{
	sizebuf_t msg;
	byte data[64];

	if (!cls.demorecording || !cls.demofile)
		return;

	memset(&msg, 0, sizeof(msg));
	msg.data = data;
	msg.maxsize = sizeof(data);
	MSG_WriteByte(&msg, svc_stufftext);
	MSG_WriteString(&msg, "//demomark\n");

	CL_WriteDemoMessageData(msg.data, msg.cursize, cl.viewangles);
}

static FUNC_NORETURN void CL_EndDemoReadFailure(const char *reason)
{
	net_message.cursize = 0;
	Con_Printf("%s\n", reason);

	/* A malformed/unterminated attract demo must not be queued again by
	   Host_EndGame. Valid playlist demos terminate through svc_disconnect. */
	cls.demonum = -1;

	Host_EndGame("%s", reason);
}

static int CL_GetDemoMessage (void)
{
	int	r, i;
	int	msglen;
	float	f;

	if (!cls.demospeed || demo_rewind.backstop) // woods (iw) #democontrols
		return 0;

	if (is_frame_seeking && !demo_seek_from_start &&
		CL_GetDemoFrameCount() >= demo_frame_seek_target)
	{
		CL_FinishExactDemoSeek();
		return 0;
	}

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

	// read the length into a local and validate it before it ever reaches
	// net_message.cursize, so a corrupt demo cannot leave a bogus size on the
	// global buffer for anything else to act on
	r = (int)fread (&msglen, 1, 4, cls.demofile);
	if (r != 4) // woods
	{
		CL_EndDemoReadFailure(r == 0 && feof (cls.demofile) ?
			"End of demo" : "Demo read failed: truncated message length");
	}
	VectorCopy (cl.mviewangles[0], cl.mviewangles[1]);
	for (i = 0 ; i < 3 ; i++)
	{
		if (fread (&f, 4, 1, cls.demofile) != 1)
		{
			CL_EndDemoReadFailure("Demo read failed: truncated view angles");
		}
		cl.mviewangles[0][i] = LittleFloat (f);
	}

	// cursize is signed: a negative length used to slip past the upper-bound-only
	// check and then convert to a huge size_t in fread, overrunning
	// net_message.data with the rest of the file.  A bad length just means a bad
	// demo -- stop playing it rather than killing the whole engine.
	msglen = LittleLong (msglen);
	if (msglen <= 0 || msglen > MAX_MSGLEN)
	{
		Con_Printf ("Corrupt demo: message length %i out of range\n", msglen);
		CL_EndDemoReadFailure("Demo read failed: invalid message length");
	}
	// only publish the size once the payload is actually in the buffer, so a
	// short read never leaves cursize describing bytes we did not get
	r = fread (net_message.data, msglen, 1, cls.demofile);
	if (r != 1)
	{
		CL_EndDemoReadFailure("Demo read failed: truncated message payload");
	}
	net_message.cursize = msglen;

	if (cls.signon == SIGNONS && !initialized) // woods (iw) #democontrols
	{
		demo_total_frame_count = CL_GetDemoFrameCount() + 1 + CL_CountRemainingDemoFrames();
		CL_ClearDemoFrags();
		CL_ResetDemoSeekState();

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
		{
			if (cls.download.active) // woods -- silence during dl
				Con_DPrintf ("<-- server to client keepalive\n");
			else
				Con_Printf ("<-- server to client keepalive\n");
		}
		else
			break;
	}

	if (cls.demorecording)
		CL_WriteDemoMessage ();

	return r;
}

static qboolean CL_DemoFilenameExists(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;

	fclose(f);
	return true;
}

static qboolean CL_DemoFormatWantsDZip(void)
{
#ifdef USE_ZLIB
	return !q_strcasecmp(cl_demo_format.string, "dz");
#else
	return false;
#endif
}

#ifdef USE_ZLIB
static void CL_DemoBuildArchiveEntryName(const char *archive_path, char *out,
	size_t outsize)
{
	COM_FileBase(COM_SkipPath(archive_path), out, outsize);
	COM_AddExtension(out, ".dem", outsize);
}
#endif

static const char *CL_DemoSkipExplicitRootPrefix(const char *name)
{
	if (name && name[0] == '.' && (name[1] == '/' || name[1] == '\\'))
		return name + 2;
	return name;
}

static qboolean CL_DemoPathHasUnsafeComponent(const char *name)
{
	const char *start;
	size_t len;

	if (!name)
		return true;

	while (*name)
	{
		while (*name == '/' || *name == '\\')
			++name;

		start = name;
		while (*name && *name != '/' && *name != '\\')
			++name;
		len = (size_t)(name - start);

		if ((len == 1 && start[0] == '.') ||
			(len == 2 && start[0] == '.' && start[1] == '.'))
			return true;
	}

	return false;
}

static qboolean CL_DemoTryOpenPath(const char *logical_name, const char *search_path, FILE **out_file, qofs_t *out_size, char *out_name, size_t out_name_size)
{
	const char *ext;
	const char *open_path;

	*out_file = NULL;
	if (out_size)
		*out_size = -1;

	open_path = CL_DemoSkipExplicitRootPrefix(search_path);
	if (CL_DemoPathHasUnsafeComponent(open_path))
		return false;

	ext = COM_FileGetExtension(open_path);
	if (!q_strcasecmp(ext, "dz"))
	{
#ifdef USE_ZLIB
		if (CL_DZipOpenDemoArchive(open_path, out_file, out_size, NULL, 0))
		{
			q_strlcpy(out_name, logical_name, out_name_size);
			return true;
		}
#endif
		return false;
	}

	if (COM_FOpenFile(open_path, out_file, NULL) >= 0 && *out_file)
	{
		if (out_size)
			*out_size = com_filesize;
		q_strlcpy(out_name, logical_name, out_name_size);
		return true;
	}

	return false;
}

static qboolean CL_DemoShouldTryDemosPrefix(const char *name)
{
	if (!name || !name[0])
		return false;
	if (name[0] == '/' || name[0] == '\\' || strchr(name, ':'))
		return false;
	if (name[0] == '.' && (name[1] == '/' || name[1] == '\\'))
		return false;
	if (!q_strncasecmp(name, "demos/", 6) || !q_strncasecmp(name, "demos\\", 6))
		return false;
	return strchr(name, '/') != NULL || strchr(name, '\\') != NULL;
}

byte *CL_LoadDemoBuffer(const char *name, int *length_out)
{
	byte *data;
	char path[MAX_OSPATH];
	const char *ext;
	const char *direct_name;
	qboolean has_path;

	if (length_out)
		*length_out = -1;

	if (!name || !name[0])
		return NULL;

	ext = COM_FileGetExtension(name);
	direct_name = CL_DemoSkipExplicitRootPrefix(name);
	has_path = strchr(name, '/') != NULL || strchr(name, '\\') != NULL;

	if (CL_DemoPathHasUnsafeComponent(direct_name))
		return NULL;

	if (!q_strcasecmp(ext, "dz"))
	{
#ifdef USE_ZLIB
		if (!has_path || CL_DemoShouldTryDemosPrefix(name))
		{
			q_snprintf(path, sizeof(path), "demos/%s", name);
			data = CL_DZipLoadDemoBuffer(path, length_out);
			if (data)
				return data;
		}

		return CL_DZipLoadDemoBuffer(direct_name, length_out);
#else
		return NULL;
#endif
	}

	if (has_path)
	{
		if (CL_DemoShouldTryDemosPrefix(name))
		{
			q_snprintf(path, sizeof(path), "demos/%s", name);
			data = COM_LoadMallocFile(path, NULL);
			if (data)
			{
				if (length_out)
					*length_out = (int)com_filesize;
				return data;
			}
		}

		data = COM_LoadMallocFile(direct_name, NULL);
		if (data && length_out)
			*length_out = (int)com_filesize;
		return data;
	}

	q_snprintf(path, sizeof(path), "demos/%s", name);
	data = COM_LoadMallocFile(path, NULL);
	if (data)
	{
		if (length_out)
			*length_out = (int)com_filesize;
		return data;
	}

	data = COM_LoadMallocFile(direct_name, NULL);
	if (data && length_out)
		*length_out = (int)com_filesize;
	return data;
}

byte *CL_LoadDemoBufferFromFile(const char *path, int *length_out)
{
	const char *ext;
	FILE *file;
	long file_size;
	byte *data;

	if (length_out)
		*length_out = -1;

	if (!path || !path[0])
		return NULL;

	ext = COM_FileGetExtension(path);
	if (!q_strcasecmp(ext, "dz"))
	{
#ifdef USE_ZLIB
		return CL_DZipLoadDemoBufferOSPath(path, length_out);
#else
		return NULL;
#endif
	}

	file = fopen(path, "rb");
	if (!file)
		return NULL;

	if (fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		return NULL;
	}
	file_size = ftell(file);
	if (file_size <= 0 || file_size > INT_MAX || fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return NULL;
	}

	data = (byte *)malloc((size_t)file_size);
	if (!data)
	{
		fclose(file);
		return NULL;
	}

	if (fread(data, 1, (size_t)file_size, file) != (size_t)file_size)
	{
		free(data);
		fclose(file);
		return NULL;
	}

	fclose(file);
	if (length_out)
		*length_out = (int)file_size;
	return data;
}

static qboolean CL_DemoResolvePlayback(const char *requested, FILE **out_file, qofs_t *out_size, char *out_name, size_t out_name_size)
{
	char logical[MAX_OSPATH];
	char path[MAX_OSPATH];
	char with_ext[MAX_OSPATH];
	const char *ext;
	qboolean has_path;

	q_strlcpy(logical, requested, sizeof(logical));
	ext = COM_FileGetExtension(logical);
	has_path = strchr(logical, '/') != NULL || strchr(logical, '\\') != NULL;

	if (ext[0])
	{
		if (!has_path || CL_DemoShouldTryDemosPrefix(logical))
		{
			q_snprintf(path, sizeof(path), "demos/%s", logical);
			if (CL_DemoTryOpenPath(logical, path, out_file, out_size, out_name, out_name_size))
				return true;
		}

		return CL_DemoTryOpenPath(logical, logical, out_file, out_size, out_name, out_name_size);
	}

	q_strlcpy(with_ext, logical, sizeof(with_ext));
	COM_AddExtension(with_ext, ".dem", sizeof(with_ext));
	if (!has_path || CL_DemoShouldTryDemosPrefix(with_ext))
	{
		q_snprintf(path, sizeof(path), "demos/%s", with_ext);
		if (CL_DemoTryOpenPath(with_ext, path, out_file, out_size, out_name, out_name_size))
			return true;
	}
	if (CL_DemoTryOpenPath(with_ext, with_ext, out_file, out_size, out_name, out_name_size))
		return true;

	q_strlcpy(with_ext, logical, sizeof(with_ext));
	COM_AddExtension(with_ext, ".dz", sizeof(with_ext));
	if (!has_path || CL_DemoShouldTryDemosPrefix(with_ext))
	{
		q_snprintf(path, sizeof(path), "demos/%s", with_ext);
		if (CL_DemoTryOpenPath(with_ext, path, out_file, out_size, out_name, out_name_size))
			return true;
	}
	return CL_DemoTryOpenPath(with_ext, with_ext, out_file, out_size, out_name, out_name_size);
}

static qboolean CL_StartDemoPlayback(FILE *demofile, qofs_t demo_size, const char *opened_name, qboolean restarting, qboolean demoreelplayback)
{
	cls.demofile = demofile;
	q_strlcpy(demoplaying, opened_name, sizeof(demoplaying)); // store the resolved demo name for window title
	Con_Printf("%s demo from %s.\n", restarting ? "Restarting" : "Playing", opened_name);

	// woods #demopercent (Baker Fitzquake Mark V)
	com_filesize = demo_size;
	q_strlcpy(cls.demoname, opened_name, sizeof(cls.demoname));
	cls.demo_offset_start = ftell(cls.demofile);	// qfs_lastload.offset instead?
	cls.demo_file_length = demo_size;
	cls.demo_hosttime_start = cls.demo_hosttime_elapsed = 0; // Fill this in ... host_time;
	cls.demo_cltime_start = cls.demo_cltime_elapsed = 0; // Fill this in
	// end #demopercent (Baker Fitzquake Mark V)

// ZOID, fscanf is evil
// O.S.: if a space character e.g. 0x20 (' ') follows '\n',
// fscanf skips that byte too and screws up further reads.
//	fscanf (cls.demofile, "%i\n", &cls.forcetrack);
	if (fscanf (cls.demofile, "%i", &cls.forcetrack) != 1 || fgetc (cls.demofile) != '\n')
	{
		fclose (cls.demofile);
		cls.demofile = NULL;
#ifdef USE_ZLIB
		CL_DZipCleanupTempDemo();
#endif
		cls.demonum = -1;	// stop demo loop
		Con_Printf ("ERROR: demo \"%s\" is invalid\n", opened_name);
		return false;
	}

	cls.demoplayback = true;
	cls.demoreelplayback = demoreelplayback;
	cls.demopaused = false;
	cls.demospeed = 1.f; // woods (iw) #democontrols
	CL_ResetDemoSeekState();
	CL_ClearDemoMarkerHistory();
	CL_ClearDemoRestartRequest();
	demo_rewind.backstop = false;
	demo_start_server_time = 0.f;
	demo_start_server_time_valid = false;
	demo_total_frame_count = 0;
	initialized = false;
	// Only change basedemospeed if it hasn't been initialized,
	// otherwise preserve the existing value
	//if (!cls.basedemospeed) // woods (iw) #democontrols
	cls.basedemospeed = 1.f; // woods (iw) #democontrols
	q_strlcpy(cls.demofilename, opened_name, sizeof(cls.demofilename)); // woods (iw) #democontrols
	cls.state = ca_connected;
	cls.demofilestart = ftell(cls.demofile); // woods(iw) #democontrols
	cls.demofilesize = demo_size; // woods (iw) #democontrols
	q_strlcpy(last_demo, opened_name, sizeof(last_demo));
	Log_Last_Demo_f();

// get rid of the menu and/or console
	key_dest = key_game;

	memset(cl_dlights, 0, sizeof(cl_dlights)); // woods (iw) #democontrols
	return true;
}

static qboolean CL_DemoRestartPlayback(const char *requested)
{
	char requested_name[MAX_OSPATH];
	char opened_name[MAX_OSPATH];
	FILE *demofile = NULL;
	qofs_t demo_size = -1;
	const float saved_basedemospeed = cls.basedemospeed ? cls.basedemospeed : 1.f;
	const qboolean saved_demoreelplayback = cls.demoreelplayback;

	q_strlcpy(requested_name, requested, sizeof(requested_name));
	if (!requested_name[0])
		return false;

	// Close the current playback before opening a replacement; this keeps dzip
	// temp-file ownership and CL_StopPlayback cleanup in the normal order.
	CL_Disconnect();

	if (!FS_IsCaseSensitive()) // woods #filesystemsens
		q_strlwr(requested_name);

	if (!CL_DemoResolvePlayback(requested_name, &demofile, &demo_size, opened_name, sizeof(opened_name)))
	{
		Con_Printf("ERROR: couldn't restart demo %s\n", requested_name);
		cls.demonum = -1;	// stop demo loop
		return false;
	}

	if (!CL_StartDemoPlayback(demofile, demo_size, opened_name, true, saved_demoreelplayback))
		return false;

	cls.basedemospeed = saved_basedemospeed;
	cls.demospeed = cls.basedemospeed;
	return true;
}

static qboolean CL_DemoProcessRestartRequest(void)
{
	char requested_name[MAX_OSPATH];

	if (!demo_restart_pending)
		return false;

	q_strlcpy(requested_name, demo_restart_name, sizeof(requested_name));
	CL_ClearDemoRestartRequest();
	CL_ResetDemoSeekState();
	CL_ClearDemoMarkerHistory();

	if (requested_name[0])
		CL_DemoRestartPlayback(requested_name);

	return true;
}

static void CL_ResetDemoRecordingPaths(void)
{
	demo_record_raw_path[0] = '\0';
	demo_record_to_dzip = false;
}

static qboolean CL_BuildRecordingRawPath(const char *final_path, char *raw_path,
	size_t raw_path_size)
{
	char base[MAX_OSPATH];
	int attempt;

	COM_StripExtension(final_path, base, sizeof(base));
	for (attempt = 0; attempt < 10000; ++attempt)
	{
		int length;

		++demo_record_serial;
		length = q_snprintf(raw_path, raw_path_size, "%s.__recording_%u.dem",
			base, demo_record_serial);
		if (length <= 0 || (size_t)length >= raw_path_size)
			break;
		if (!CL_DemoFilenameExists(raw_path))
			return true;
	}

	raw_path[0] = '\0';
	return false;
}

static qboolean CL_DemoShouldAutoDelete(int frames)
{
	qboolean auto_delete;
	int min_frames = CL_DemoMinFramesThreshold(&auto_delete);

	return auto_delete && min_frames > 0 && frames >= 0 && frames < min_frames;
}

static qboolean CL_AutoDeleteShortDemo(const char *path, int frames)
{
	if (!path || !path[0])
		return false;

	if (!CL_DemoShouldAutoDelete(frames))
		return false;

	if (remove(path) != 0)
	{
		Con_Printf("WARNING: could not delete short demo %s\n", COM_SkipPath(path));
		return false;
	}

	Con_Printf("deleted short demo %s (%d frames)\n", COM_SkipPath(path), frames);
	return true;
}

#ifdef USE_ZLIB
static qboolean CL_DeleteShortDemoRequested(const char *path, int frames,
	qboolean requested)
{
	if (!requested || !path || !path[0])
		return false;

	if (remove(path) != 0)
	{
		Con_Printf("WARNING: could not delete short demo %s\n", COM_SkipPath(path));
		return false;
	}

	Con_Printf("deleted short demo %s (%d frames)\n", COM_SkipPath(path), frames);
	return true;
}
#endif

static void CL_GetDemoModeTag(char *mode_tag, size_t mode_tag_size)
{
	char mode_buf[32];
	const char *mode;

	if (!mode_tag_size)
		return;
	mode_tag[0] = '\0';

	if (!cl.serverinfo[0])
		return;

	mode = Info_GetKey(cl.serverinfo, "mode", mode_buf, sizeof(mode_buf));
	if (!mode || !mode[0])
		return;

	if (!q_strcasecmp(mode, "ctf"))
		q_strlcpy(mode_tag, "ctf", mode_tag_size);
	else if (!q_strcasecmp(mode, "dm") || !q_strcasecmp(mode, "ffa"))
		q_strlcpy(mode_tag, "dm", mode_tag_size);
	else if (!q_strcasecmp(mode, "ra") || !q_strcasecmp(mode, "rocketarena"))
		q_strlcpy(mode_tag, "ra", mode_tag_size);
	else if (!q_strcasecmp(mode, "ca") || !q_strcasecmp(mode, "clanarena"))
		q_strlcpy(mode_tag, "ca", mode_tag_size);
	else if (!q_strcasecmp(mode, "airshot"))
		q_strlcpy(mode_tag, "airshot", mode_tag_size);
	else if (!q_strcasecmp(mode, "wipeout"))
		q_strlcpy(mode_tag, "wipeout", mode_tag_size);
	else if (!q_strcasecmp(mode, "freezetag"))
		q_strlcpy(mode_tag, "freezetag", mode_tag_size);
}

static qboolean CL_BuildDemoPathWithMatchSuffixes(const char *input_path, char *output_path, size_t output_size)
{
	char base[MAX_OSPATH];
	char current_noext[MAX_OSPATH];
	char suffix[32];
	char mode_tag[16];
	char extdot[16];
	size_t current_len;
	size_t suffix_len;
	int attempt;

	if (!input_path[0] || !output_size)
		return false;

	CL_GetDemoModeTag(mode_tag, sizeof(mode_tag));

	suffix[0] = '\0';
	if (mode_tag[0])
		q_snprintf(suffix, sizeof(suffix), "_%s", mode_tag);
	if (cls.demo_had_overtime)
		q_strlcat(suffix, "_ot", sizeof(suffix));
	if (cls.demo_marker_count > 1)
		q_strlcat(suffix, "_marks", sizeof(suffix));
	else if (cls.demo_marker_count == 1)
		q_strlcat(suffix, "_mark", sizeof(suffix));

	if (!suffix[0])
	{
		q_strlcpy(output_path, input_path, output_size);
		return true;
	}

	COM_StripExtension(input_path, current_noext, sizeof(current_noext));
	current_len = strlen(current_noext);
	suffix_len = strlen(suffix);
	if (current_len > suffix_len && !q_strcasecmp(current_noext + current_len - suffix_len, suffix))
	{
		q_strlcpy(output_path, input_path, output_size);
		return true;
	}

	COM_StripExtension(input_path, base, sizeof(base));
	q_snprintf(extdot, sizeof(extdot), ".%s", COM_FileGetExtension(input_path));

	for (attempt = 0; attempt < 1000; ++attempt)
	{
		if (attempt == 0)
			q_snprintf(output_path, output_size, "%s%s%s", base, suffix, extdot);
		else
			q_snprintf(output_path, output_size, "%s%s%d%s", base, suffix, attempt + 1, extdot);

		if (CL_DemoFilenameExists(output_path))
			continue;

		return true;
	}

	return false;
}

static void CL_RenameDemoWithMatchSuffixes(void)
{
	char renamed[MAX_OSPATH];

	if (!cls.demofilename[0])
		return;

	if (!CL_BuildDemoPathWithMatchSuffixes(cls.demofilename, renamed, sizeof(renamed)))
	{
		Con_Printf("WARNING: could not find available demo name for %s\n", COM_SkipPath(cls.demofilename));
		return;
	}

	if (!q_strcasecmp(renamed, cls.demofilename))
		return;

	if (rename(cls.demofilename, renamed) == 0)
	{
		q_strlcpy(cls.demofilename, renamed, sizeof(cls.demofilename));
		Con_SafePrintf("renamed demo to ");
		Con_LinkPrintf(renamed, "%s", COM_SkipPath(renamed));
		Con_SafePrintf("\n");
	}
	else
	{
		Con_Printf("WARNING: could not rename demo to %s\n", COM_SkipPath(renamed));
	}
}

#ifdef USE_ZLIB
static qboolean CL_DemoFinalizePathReserved(const char *path)
{
	demo_finalize_job_t *job;
	qboolean reserved = false;

	if (!demo_finalizer.mutex || !path || !path[0])
		return false;

	SDL_LockMutex(demo_finalizer.mutex);
	if (demo_finalizer.active && !q_strcasecmp(demo_finalizer.active->archive_path, path))
		reserved = true;
	for (job = demo_finalizer.pending_head; !reserved && job; job = job->next)
		if (!q_strcasecmp(job->archive_path, path))
			reserved = true;
	for (job = demo_finalizer.completed_head; !reserved && job; job = job->next)
		if (!q_strcasecmp(job->archive_path, path))
			reserved = true;
	SDL_UnlockMutex(demo_finalizer.mutex);
	return reserved;
}

static qboolean CL_DemoFinalizeChoosePath(const char *preferred, char *path, size_t path_size)
{
	char base[MAX_OSPATH];
	char extension[16];
	int attempt;

	if (!CL_DemoFilenameExists(preferred) && !CL_DemoFinalizePathReserved(preferred))
	{
		q_strlcpy(path, preferred, path_size);
		return true;
	}

	COM_StripExtension(preferred, base, sizeof(base));
	q_snprintf(extension, sizeof(extension), ".%s", COM_FileGetExtension(preferred));
	for (attempt = 2; attempt <= 1000; ++attempt)
	{
		q_snprintf(path, path_size, "%s_%d%s", base, attempt, extension);
		if (!CL_DemoFilenameExists(path) && !CL_DemoFinalizePathReserved(path))
			return true;
	}

	return false;
}

static qboolean CL_DemoFinalizeBuildTempPath(const char *archive_path,
	char *temp_path, size_t temp_path_size)
{
	int attempt;

	for (attempt = 0; attempt < 10000; ++attempt)
	{
		int length;

		++demo_finalize_serial;
		length = q_snprintf(temp_path, temp_path_size, "%s.__finalizing_%u",
			archive_path, demo_finalize_serial);
		if (length <= 0 || (size_t)length >= temp_path_size)
			break;
		if (!CL_DemoFilenameExists(temp_path))
			return true;
	}

	temp_path[0] = '\0';
	return false;
}

static qboolean CL_DemoFinalizeArchive(const char *raw_path, const char *archive_path,
	const char *temp_path, const char *entry_name, qboolean *raw_remove_failed)
{
	if (raw_remove_failed)
		*raw_remove_failed = false;
	if (!temp_path[0] || !CL_DZipArchiveDemoFile(raw_path, temp_path, entry_name))
		return false;

	if (rename(temp_path, archive_path) != 0)
	{
		remove(temp_path);
		return false;
	}

	if (remove(raw_path) != 0 && raw_remove_failed)
		*raw_remove_failed = true;
	return true;
}

static int CL_DemoFinalizeThread(void *unused)
{
	(void)unused;

	for (;;)
	{
		demo_finalize_job_t *job;

		SDL_LockMutex(demo_finalizer.mutex);
		while (!demo_finalizer.pending_head && !demo_finalizer.shutdown)
			SDL_CondWait(demo_finalizer.condition, demo_finalizer.mutex);
		if (!demo_finalizer.pending_head && demo_finalizer.shutdown)
		{
			SDL_UnlockMutex(demo_finalizer.mutex);
			break;
		}

		job = demo_finalizer.pending_head;
		demo_finalizer.pending_head = job->next;
		if (!demo_finalizer.pending_head)
			demo_finalizer.pending_tail = NULL;
		job->next = NULL;
		demo_finalizer.active = job;
		SDL_UnlockMutex(demo_finalizer.mutex);

		job->ok = CL_DemoFinalizeArchive(job->raw_path, job->archive_path,
			job->temp_path, job->entry_name, &job->raw_remove_failed);

		SDL_LockMutex(demo_finalizer.mutex);
		demo_finalizer.active = NULL;
		if (demo_finalizer.completed_tail)
			demo_finalizer.completed_tail->next = job;
		else
			demo_finalizer.completed_head = job;
		demo_finalizer.completed_tail = job;
		SDL_UnlockMutex(demo_finalizer.mutex);
	}

	return 0;
}

static void CL_DemoFinalizeInit(void)
{
	if (demo_finalizer.thread || demo_finalizer.mutex)
		return;

	demo_finalizer.mutex = SDL_CreateMutex();
	demo_finalizer.condition = SDL_CreateCond();
	if (!demo_finalizer.mutex || !demo_finalizer.condition)
		goto fail;

	demo_finalizer.thread = SDL_CreateThread(CL_DemoFinalizeThread, "DemoFinalize", NULL);
	if (!demo_finalizer.thread)
		goto fail;
	return;

fail:
	Con_DPrintf("Unable to start demo finalizer: %s\n", SDL_GetError());
	if (demo_finalizer.condition)
		SDL_DestroyCond(demo_finalizer.condition);
	if (demo_finalizer.mutex)
		SDL_DestroyMutex(demo_finalizer.mutex);
	memset(&demo_finalizer, 0, sizeof(demo_finalizer));
}

static demo_finalize_job_t *CL_DemoFinalizeJobCreate(const char *raw_path,
	const char *archive_path, const char *entry_name, int frame_count)
{
	demo_finalize_job_t *job = (demo_finalize_job_t *)calloc(1, sizeof(*job));

	if (!job)
		return NULL;

	q_strlcpy(job->raw_path, raw_path, sizeof(job->raw_path));
	q_strlcpy(job->archive_path, archive_path, sizeof(job->archive_path));
	q_strlcpy(job->entry_name, entry_name, sizeof(job->entry_name));
	if (!CL_DemoFinalizeBuildTempPath(archive_path, job->temp_path,
		sizeof(job->temp_path)))
	{
		free(job);
		return NULL;
	}
	job->frame_count = frame_count;
	job->delete_short = CL_DemoShouldAutoDelete(frame_count);
	return job;
}

static qboolean CL_DemoFinalizeQueue(demo_finalize_job_t *job)
{
	if (!job || !demo_finalizer.thread || !demo_finalizer.mutex)
		return false;

	SDL_LockMutex(demo_finalizer.mutex);
	if (demo_finalizer.shutdown || demo_finalizer.outstanding >= DEMO_FINALIZE_MAX_OUTSTANDING)
	{
		SDL_UnlockMutex(demo_finalizer.mutex);
		return false;
	}

	job->next = NULL;
	if (demo_finalizer.pending_tail)
		demo_finalizer.pending_tail->next = job;
	else
		demo_finalizer.pending_head = job;
	demo_finalizer.pending_tail = job;
	demo_finalizer.outstanding++;
	SDL_CondSignal(demo_finalizer.condition);
	SDL_UnlockMutex(demo_finalizer.mutex);
	return true;
}

static void CL_DemoFinalizeConsumeCompleted(void)
{
	demo_finalize_job_t *jobs;
	qboolean rebuild = false;

	if (!demo_finalizer.mutex)
		return;

	SDL_LockMutex(demo_finalizer.mutex);
	jobs = demo_finalizer.completed_head;
	demo_finalizer.completed_head = NULL;
	demo_finalizer.completed_tail = NULL;
	SDL_UnlockMutex(demo_finalizer.mutex);

	while (jobs)
	{
		demo_finalize_job_t *job = jobs;
		const char *final_path;
		qboolean deleted_short = false;

		jobs = jobs->next;
		final_path = job->ok ? job->archive_path : job->raw_path;

		if (!job->ok)
		{
			Con_SafePrintf("WARNING: failed to write %s, raw demo kept at ",
				COM_SkipPath(job->archive_path));
			Con_LinkPrintf(job->raw_path, "%s", job->raw_path);
			Con_SafePrintf("\n");
		}
		else
		{
			deleted_short = CL_DeleteShortDemoRequested(final_path,
				job->frame_count, job->delete_short);
			if (job->raw_remove_failed)
				Con_SafePrintf("WARNING: archived demo but could not remove temporary file %s\n",
					COM_SkipPath(job->raw_path));
		}

		if (deleted_short)
			CL_DemoMarkHistory_ClearList(&job->marks_head, &job->marks_tail);
		else
			CL_DemoMarkHistory_FlushList(&job->marks_head, &job->marks_tail, final_path);

		if (job->ok && !deleted_short)
		{
			Con_SafePrintf("completed demo ");
			Con_LinkPrintf(final_path, "%s", COM_SkipPath(final_path));
			Con_SafePrintf("\n");
		}

		SDL_LockMutex(demo_finalizer.mutex);
		demo_finalizer.outstanding--;
		SDL_UnlockMutex(demo_finalizer.mutex);
		free(job);
		rebuild = true;
	}

	if (rebuild)
		DemoList_Rebuild();
}

static void CL_DemoFinalizeShutdown(void)
{
	qboolean finishing;

	if (!demo_finalizer.mutex)
		return;

	SDL_LockMutex(demo_finalizer.mutex);
	finishing = demo_finalizer.active != NULL || demo_finalizer.pending_head != NULL;
	demo_finalizer.shutdown = true;
	SDL_CondSignal(demo_finalizer.condition);
	SDL_UnlockMutex(demo_finalizer.mutex);

	if (demo_finalizer.thread)
	{
		if (finishing)
			Sys_Printf("Finishing pending demo archives before shutdown...\n");
		SDL_WaitThread(demo_finalizer.thread, NULL);
	}
	demo_finalizer.thread = NULL;
	CL_DemoFinalizeConsumeCompleted();

	SDL_DestroyCond(demo_finalizer.condition);
	SDL_DestroyMutex(demo_finalizer.mutex);
	memset(&demo_finalizer, 0, sizeof(demo_finalizer));
}
#endif

void CL_Demo_Init(void)
{
	if (cls.state == ca_dedicated)
		return;

	CL_DemoWriterInit();
#ifdef USE_ZLIB
	CL_DemoFinalizeInit();
#endif
}

void CL_Demo_Frame(void)
{
	if (demo_record_stop_requested)
	{
		demo_record_stop_requested = false;
		if (cls.demorecording && !demo_record_stop_in_progress)
			CL_StopRecording();
	}

#ifdef USE_ZLIB
	CL_DemoFinalizeConsumeCompleted();
#endif
}

void CL_Demo_Shutdown(void)
{
	if (cls.demorecording)
		CL_StopRecording();
	CL_DemoWriterShutdown();
#ifdef USE_ZLIB
	CL_DemoFinalizeShutdown();
#endif
}


/*
====================
CL_Stop_f

stop recording a demo
====================
*/
void CL_StopRecording(void)
{
	qboolean completed = true;
	qboolean deleted_short = false;
	qboolean writer_ok;
#ifdef USE_ZLIB
	qboolean finalize_queued = false;
#endif

	if (!cls.demorecording || demo_record_stop_in_progress)
		return;
	demo_record_stop_in_progress = true;
	demo_record_stop_requested = false;

// write a disconnect message to the demo file
	SZ_Clear (&net_message);
	MSG_WriteByte (&net_message, svc_disconnect);
	CL_WriteDemoMessage ();

// finish up
	writer_ok = CL_DemoWriterStop();
	cls.demofile = NULL;
	cls.demorecording = false;
	if (!writer_ok)
	{
		Con_SafePrintf("WARNING: failed to finish writing demo; partial demo kept at ");
		Con_LinkPrintf(demo_record_to_dzip ? demo_record_raw_path : cls.demofilename,
			"%s", demo_record_to_dzip ? demo_record_raw_path : cls.demofilename);
		Con_SafePrintf("\n");
		completed = false;
	}

	if (demo_record_to_dzip && writer_ok)
	{
#ifdef USE_ZLIB
		char archived_path[MAX_OSPATH];
		char available_path[MAX_OSPATH];
		char temp_path[MAX_OSPATH] = "";
		char entry_name[MAX_OSPATH];
		demo_finalize_job_t *job = NULL;
		qboolean raw_remove_failed = false;

		if (!CL_BuildDemoPathWithMatchSuffixes(cls.demofilename, archived_path, sizeof(archived_path)))
		{
			Con_Printf("WARNING: could not build archive path for %s\n", COM_SkipPath(cls.demofilename));
			completed = false;
		}
		else if (!CL_DemoFinalizeChoosePath(archived_path, available_path, sizeof(available_path)))
		{
			Con_Printf("WARNING: could not reserve archive path for %s\n", COM_SkipPath(cls.demofilename));
			completed = false;
		}
		else
		{
			q_strlcpy(archived_path, available_path, sizeof(archived_path));
			CL_DemoBuildArchiveEntryName(archived_path, entry_name, sizeof(entry_name));
			job = CL_DemoFinalizeJobCreate(demo_record_raw_path, archived_path,
				entry_name, cls.demo_record_frame_count);
			if (job)
			{
				job->marks_head = demomark_pending_head;
				job->marks_tail = demomark_pending_tail;
				demomark_pending_head = NULL;
				demomark_pending_tail = NULL;
				if (CL_DemoFinalizeQueue(job))
				{
					finalize_queued = true;
					q_strlcpy(cls.demofilename, archived_path, sizeof(cls.demofilename));
					Con_SafePrintf("finishing demo ");
					Con_LinkPrintf(archived_path, "%s", COM_SkipPath(archived_path));
					Con_SafePrintf(" in the background\n");
				}
				else
				{
					q_strlcpy(temp_path, job->temp_path, sizeof(temp_path));
					demomark_pending_head = job->marks_head;
					demomark_pending_tail = job->marks_tail;
					free(job);
					job = NULL;
				}
			}

			if (!finalize_queued && !temp_path[0])
				CL_DemoFinalizeBuildTempPath(archived_path, temp_path, sizeof(temp_path));

			if (!finalize_queued && CL_DemoFinalizeArchive(demo_record_raw_path,
				archived_path, temp_path, entry_name, &raw_remove_failed))
			{
				if (raw_remove_failed)
					Con_Printf("WARNING: could not remove temporary demo %s\n",
						COM_SkipPath(demo_record_raw_path));
				q_strlcpy(cls.demofilename, archived_path, sizeof(cls.demofilename));
			}
			else if (!finalize_queued)
			{
				Con_Printf("WARNING: failed to write %s, raw demo kept at ",
					COM_SkipPath(archived_path));
				Con_LinkPrintf(demo_record_raw_path, "%s", demo_record_raw_path);
				Con_Printf("\n");
				q_strlcpy(cls.demofilename, demo_record_raw_path, sizeof(cls.demofilename));
				completed = false;
			}
		}
#else
		completed = false;
#endif
	}
	else if (!demo_record_to_dzip)
	{
		CL_RenameDemoWithMatchSuffixes();
	}
	else
	{
		q_strlcpy(cls.demofilename, demo_record_raw_path, sizeof(cls.demofilename));
	}

#ifdef USE_ZLIB
	if (demo_record_to_dzip && !finalize_queued && !completed)
		q_strlcpy(cls.demofilename, demo_record_raw_path, sizeof(cls.demofilename));

	if (finalize_queued)
	{
		cls.demo_had_overtime = false;
		cls.demo_marker_count = 0;
		cls.demo_record_frame_count = 0;
		Cvar_SetROM(cl_recordingdemo.name, "");
		CL_ResetDemoRecordingPaths();
		demo_record_stop_in_progress = false;
		return;
	}
#endif

	if (completed)
		deleted_short = CL_AutoDeleteShortDemo(cls.demofilename, cls.demo_record_frame_count);

	if (deleted_short)
		CL_DemoMarkHistory_ClearPending();
	else
		CL_DemoMarkHistory_FlushPending(cls.demofilename);

	cls.demo_had_overtime = false;
	cls.demo_marker_count = 0;
	cls.demo_record_frame_count = 0;

	if (completed && !deleted_short)
	{
		Con_SafePrintf ("completed demo ");
		Con_LinkPrintf (cls.demofilename, "%s", COM_SkipPath(cls.demofilename));
		Con_SafePrintf ("\n");
	}

	Cvar_SetROM(cl_recordingdemo.name, "");
	CL_ResetDemoRecordingPaths();
	demo_record_stop_in_progress = false;
	
// ericw -- update demo tab-completion list
	DemoList_Rebuild ();
}

void CL_Stop_f(void)
{
	if (cmd_source != src_command)
		return;

	if (!cls.demorecording)
	{
		Con_Printf("Not recording a demo.\n");
		return;
	}

	CL_StopRecording();
}

void CL_DemoMark_f(void)
{
	if (cmd_source == src_server)
	{
		if (cls.demorecording && !cls.demoplayback)
		{
			cls.demo_marker_count++;
			if (Cmd_Argc() > 1 && !q_strcasecmp(Cmd_Argv(1), "local"))
			{
				const int mark_frame = cls.demo_record_frame_count > 0 ? cls.demo_record_frame_count : 1;
				CL_DemoMarkHistory_QueuePending(cl.mapname, mark_frame);
				{
					const char *soundFile = COM_FileExists("sound/qssm/copy.wav", NULL) ? "qssm/copy.wav" : "player/tornoff2.wav";
					S_LocalSound(soundFile);
				}
				Con_Printf("demo marker added at ^mframe^d ^1%d^d\n", mark_frame);
			}
		}

		if (is_marker_seeking)
		{
			if (demo_last_marker_offset >= 0 && cls.demo_offset_current <= demo_last_marker_offset)
				return;

			demo_marker_found = true;
			demo_marker_found_time = cl.mtime[0];
			demo_last_marker_offset = cls.demo_offset_current;
			return;
		}

		Con_DPrintf("Demo marker hit\n");
		return;
	}

	if (cmd_source != src_command)
		return;

	if (Cmd_Argc() > 2)
	{
		Con_Printf("markdemo          : add a marker while recording, or print saved mark history when not recording\n");
		Con_Printf("markdemo clear    : clear saved mark history\n");
		Con_Printf("markdemo <map>    : print saved mark history filtered by map\n");
		return;
	}

	if (!cls.demorecording)
	{
		if (Cmd_Argc() == 2 && !q_strcasecmp(Cmd_Argv(1), "clear"))
		{
			if (CL_DemoMarkHistory_ClearStored())
				Con_Printf("markdemo: cleared saved history\n");
			else
				Con_Printf("markdemo: failed to clear saved history\n");
			return;
		}

		CL_DemoMarkHistory_Print(Cmd_Argc() == 2 ? Cmd_Argv(1) : NULL);
		return;
	}

	if (Cmd_Argc() != 1)
	{
		Con_Printf("markdemo: while recording, use markdemo with no arguments\n");
		return;
	}

	if (CL_DemoMarkUseServerPath())
	{
		Cmd_ForwardToServer();
		return;
	}

	{
		const int mark_frame = cls.demo_record_frame_count + 1;

		cls.demo_marker_count++;
		CL_DemoMarkHistory_QueuePending(cl.mapname, mark_frame);
		CL_WriteRecordedDemoMarker();
		{
			const char *soundFile = COM_FileExists("sound/qssm/copy.wav", NULL) ? "qssm/copy.wav" : "player/tornoff2.wav";
			S_LocalSound(soundFile);
		}
		Con_Printf("demo marker added at ^mframe^d ^1%d^d\n", mark_frame);
	}
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
		// Client-only map decorations are rebuilt from the BSP entity lump.
		// They have no server model index and must not enter a demo signon.
		if (!cl.static_entities[idx].ent->baseline.modelindex)
			continue;
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
	COM_StripExtension(name, name, sizeof(name));
	COM_AddExtension(name, CL_DemoFormatWantsDZip() ? ".dz" : ".dem", sizeof(name));

	CL_ResetDemoRecordingPaths();
	demo_record_to_dzip = CL_DemoFormatWantsDZip();
	if (demo_record_to_dzip &&
		!CL_BuildRecordingRawPath(name, demo_record_raw_path, sizeof(demo_record_raw_path)))
	{
		Con_Printf("ERROR: couldn't reserve a temporary demo path for %s\n", name);
		CL_ResetDemoRecordingPaths();
		return;
	}

	Cvar_SetROM(cl_recordingdemo.name, name);
	q_strlcpy(cls.demofilename, name, sizeof(cls.demofilename)); // user-visible final target

	Con_SafePrintf("Recording to ");
	Con_LinkPrintf(name, "%s", name);
	Con_SafePrintf(".\n");
	cls.demofile = fopen (demo_record_to_dzip ? demo_record_raw_path : name, "wb");
	if (!cls.demofile)
	{
		Con_Printf ("ERROR: couldn't create %s\n", demo_record_to_dzip ? demo_record_raw_path : name);
		Cvar_SetROM(cl_recordingdemo.name, "");
		CL_ResetDemoRecordingPaths();
		return;
	}

	cls.forcetrack = track;
	if (fprintf(cls.demofile, "%i\n", cls.forcetrack) < 0 ||
		!CL_DemoWriterStart(cls.demofile))
	{
		const char *record_path = demo_record_to_dzip ? demo_record_raw_path : name;

		fclose(cls.demofile);
		cls.demofile = NULL;
		if (remove(record_path) != 0)
			Con_Printf("WARNING: couldn't remove incomplete demo %s\n", record_path);
		Con_Printf("ERROR: couldn't start demo writer for %s\n",
			record_path);
		Cvar_SetROM(cl_recordingdemo.name, "");
		CL_ResetDemoRecordingPaths();
		return;
	}

	cls.demo_had_overtime = false;
	cls.demo_marker_count = 0;
	cls.demo_record_frame_count = 0;
	CL_DemoMarkHistory_ClearPending();
	cls.demorecording = true;

	// from ProQuake: initialize the demo file if we're already connected
	if (c < 3 && cls.state == ca_connected)
	{
		byte *data = net_message.data;
		int cursize = net_message.cursize;
		int maxsize = net_message.maxsize;
		byte weirdaltbufferthatprobablyisntneeded[NET_MAXMESSAGE];

		net_message.data = weirdaltbufferthatprobablyisntneeded;
		net_message.maxsize = sizeof(weirdaltbufferthatprobablyisntneeded);
		SZ_Clear (&net_message);

		CL_Record_Serverdata();
		CL_Record_Prespawn();
		CL_Record_Spawn();

		// restore net_message
		net_message.data = data;
		net_message.cursize = cursize;
		net_message.maxsize = maxsize;
	}
}


/*
====================
CL_JumpDemo_f
====================
*/
void CL_JumpDemo_f(void)
{
	const char *arg;
	const char *value;
	int len;
	int value_len;
	qboolean is_relative;
	float sign;
	char token[MAXCMDLINE];

	if (cmd_source != src_command)
		return;

	if (!cls.demoplayback)
	{
		Con_Printf("jumpdemo: not playing a demo\n");
		return;
	}

	if (Cmd_Argc() != 2)
	{
		Con_Printf("jumpdemo <frame | percent%% | M:SS | H:MM:SS | Ns | mark> : seek (prefix +/- for relative)\n");
		return;
	}

	if (!initialized)
	{
		Con_Printf("jumpdemo: demo not ready yet\n");
		return;
	}

	if (CL_DemoScrubActive())
		CL_DemoScrub_Cancel();

	arg = Cmd_Argv(1);
	len = (int)strlen(arg);
	if (!len)
		return;

	if (!q_strcasecmp(arg, "mark"))
	{
		const qboolean demo_has_marker_tag = CL_DemoNameHasMarkerTag(cls.demoname);

		if (!demo_has_marker_tag)
		{
			Con_Printf("jumpdemo %s: demo has no _mark/_marks tag\n", arg);
			return;
		}

		CL_ClearDemoRestartRequest();
		CL_ResetDemoSeekState();
		is_marker_seeking = true;
		Con_Printf("seeking next demo marker...\n");
		return;
	}

	is_relative = (arg[0] == '+' || arg[0] == '-');
	sign = (arg[0] == '-') ? -1.f : 1.f;
	value = is_relative ? arg + 1 : arg;
	value_len = is_relative ? len - 1 : len;

	if (!value_len)
	{
		Con_Printf("jumpdemo: missing value after +/-\n");
		return;
	}

	if (value_len >= (int)sizeof(token))
	{
		Con_Printf("jumpdemo: value too long\n");
		return;
	}

	memcpy(token, value, value_len);
	token[value_len] = '\0';
	if (token[0] == '+' || token[0] == '-')
	{
		Con_Printf("jumpdemo: invalid value\n");
		return;
	}

	if (value_len > 1 && token[value_len - 1] == '%')
	{
		float pct;

		token[value_len - 1] = '\0';
		if (!CL_ParseFloatStrict(token, &pct) || pct < 0.f)
		{
			Con_Printf("jumpdemo: invalid percentage value\n");
			return;
		}

		if (is_relative)
			pct = CL_GetCurrentDemoPercent() + sign * pct;

		if (!CL_DemoSeekToPercent(pct))
			Con_Printf("jumpdemo: percentage seeking not ready yet\n");
		return;
	}

	if (value_len > 1 && (token[value_len - 1] == 's' || token[value_len - 1] == 'S'))
	{
		float seconds;
		float absolute_time;

		token[value_len - 1] = '\0';
		if (!CL_ParseFloatStrict(token, &seconds) || seconds < 0.f)
		{
			Con_Printf("jumpdemo: invalid time value\n");
			return;
		}

		if (is_relative)
			absolute_time = cl.mtime[0] + sign * seconds;
		else
		{
			if (!demo_start_server_time_valid)
			{
				Con_Printf("jumpdemo: time seeking not ready yet\n");
				return;
			}
			absolute_time = demo_start_server_time + seconds;
		}

		CL_DemoSeekToTime(absolute_time);
		return;
	}

	if (strchr(token, ':'))
	{
		float seconds;
		float absolute_time;

		if (!CL_ParseTimeString(token, &seconds))
		{
			Con_Printf("jumpdemo: invalid time format (use M:SS or H:MM:SS)\n");
			return;
		}

		if (is_relative)
			absolute_time = cl.mtime[0] + sign * seconds;
		else
		{
			if (!demo_start_server_time_valid)
			{
				Con_Printf("jumpdemo: time seeking not ready yet\n");
				return;
			}
			absolute_time = demo_start_server_time + seconds;
		}

		CL_DemoSeekToTime(absolute_time);
		return;
	}

	{
		int amount;
		int target;
		const int current_frame = CL_GetDemoFrameCount();

		if (!CL_ParseIntStrict(token, &amount))
		{
			Con_Printf("jumpdemo: invalid frame value\n");
			return;
		}

		if (is_relative)
		{
			target = current_frame + (int)(sign * amount);
			if (target < 1)
				target = 1;
		}
		else
		{
			target = amount;
			if (target < 1)
			{
				Con_Printf("jumpdemo: frame must be >= 1\n");
				return;
			}
		}

		CL_ClearDemoRestartRequest();
		CL_ClearDemoMarkerHistory();
		CL_ResetDemoSeekState();
		demo_frame_seek_target = target;
		if (target <= 1 && current_frame > 1 && CL_DemoQueueRestart())
			return;

		is_frame_seeking = true;
		if (target < current_frame)
		{
			demo_seek_from_start = true;
			CL_ClearDemoFrags();
		}
	}
}

/*
====================
CL_PlayDemo

play [demoname]
====================
*/
void CL_PlayDemo (const char *name, qboolean demoreelplayback)
{
	char	requested[MAX_OSPATH];
	char	opened_name[MAX_OSPATH];
	FILE	*demofile = NULL;
	qofs_t	demo_size = -1;
	qboolean use_last_demo;
	qboolean allow_last_fallback;

// disconnect from server
	CL_Disconnect ();

	use_last_demo = !q_strcasecmp(name, "-l");
	allow_last_fallback = !q_strcasecmp(name, "last");

	if (use_last_demo) // woods #lastdemo
	{
		if (!last_demo[0])
		{
			// Try to load last demo from file if not already loaded
			Load_Last_Demo();
			if (!last_demo[0])
			{
				Con_Printf("no last demo available\n");
				return;
			}
		}
		q_strlcpy(requested, last_demo, sizeof(requested));
	}
	else
	{
		q_strlcpy(requested, name, sizeof(requested));
	}

	if (!FS_IsCaseSensitive()) // woods #filesystemsens
		q_strlwr (requested);

	if (!CL_DemoResolvePlayback(requested, &demofile, &demo_size, opened_name, sizeof(opened_name)) && allow_last_fallback)
	{
		if (!last_demo[0])
			Load_Last_Demo();
		if (!last_demo[0])
		{
			Con_Printf("no last demo available\n");
			return;
		}

		q_strlcpy(requested, last_demo, sizeof(requested));
		if (!FS_IsCaseSensitive())
			q_strlwr(requested);
	}

	if (!demofile && !CL_DemoResolvePlayback(requested, &demofile, &demo_size, opened_name, sizeof(opened_name)))
	{
		Con_Printf ("ERROR: couldn't open %s\n", requested); // woods #demosfolder
		cls.demonum = -1;	// stop demo loop
		return;
	}

	if (!CL_StartDemoPlayback(demofile, demo_size, opened_name, false, demoreelplayback))
		return;
}

/*
====================
CL_PlayDemo_f
====================
*/
void CL_PlayDemo_f (void)
{
	if (cmd_source != src_command)
		return;

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("\nplaydemo <demoname> : plays a demo\n");
		Con_Printf ("playdemo last       : plays last.dem or last.dz if present, otherwise the most recently played demo\n");
		Con_Printf ("playdemo -l         : plays the most recently played demo\n\n"); // woods #lastdemo
		return;
	}

	CL_PlayDemo (Cmd_Argv(1), false);
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

#ifdef USE_ZLIB
/*
==================
DZip Support
==================
*/

#define CL_DZIP_MAX_ENT			32768
#define CL_DZIP_MAX_ENT_OLD		1024
#define CL_DZIP_MAJOR_VERSION		3
#define CL_DZIP_MINOR_VERSION		2
#define CL_DZIP_INITCRC			0xffffffffU
#define CL_DZIP_P_BLOCKSIZE		131072
#define CL_DZIP_Z_BUFFER_SIZE		16384
#define CL_DZIP_DIR_DISK_SIZE		32

#define CL_DZIP_SU_PUNCH0		SU_PUNCH1
#define CL_DZIP_SU_PUNCH1		SU_PUNCH2
#define CL_DZIP_SU_PUNCH2		SU_PUNCH3
#define CL_DZIP_SU_VELOCITY0		SU_VELOCITY1
#define CL_DZIP_SU_VELOCITY1		SU_VELOCITY2
#define CL_DZIP_SU_VELOCITY2		SU_VELOCITY3

#define CL_DZIP_U_ORIGIN0		U_ORIGIN1
#define CL_DZIP_U_ORIGIN1		U_ORIGIN2
#define CL_DZIP_U_ORIGIN2		U_ORIGIN3
#define CL_DZIP_U_ANGLE0		U_ANGLE1
#define CL_DZIP_U_ANGLE1		U_ANGLE2
#define CL_DZIP_U_ANGLE2		U_ANGLE3
#define CL_DZIP_U_NOLERP		U_STEP

enum
{
	CL_DZIP_TYPE_NORMAL,
	CL_DZIP_TYPE_DEMV1,
	CL_DZIP_TYPE_TXT,
	CL_DZIP_TYPE_PAK,
	CL_DZIP_TYPE_DZ,
	CL_DZIP_TYPE_DEM,
	CL_DZIP_TYPE_NEHAHRA,
	CL_DZIP_TYPE_DIR,
	CL_DZIP_TYPE_STORE,
	CL_DZIP_TYPE_LAST
};

enum
{
	CL_DZIP_DEM_bad,
	CL_DZIP_DEM_nop,
	CL_DZIP_DEM_disconnect,
	CL_DZIP_DEM_updatestat,
	CL_DZIP_DEM_version,
	CL_DZIP_DEM_setview,
	CL_DZIP_DEM_sound,
	CL_DZIP_DEM_time,
	CL_DZIP_DEM_print,
	CL_DZIP_DEM_stufftext,
	CL_DZIP_DEM_setangle,
	CL_DZIP_DEM_serverinfo,
	CL_DZIP_DEM_lightstyle,
	CL_DZIP_DEM_updatename,
	CL_DZIP_DEM_updatefrags,
	CL_DZIP_DEM_clientdata,
	CL_DZIP_DEM_stopsound,
	CL_DZIP_DEM_updatecolors,
	CL_DZIP_DEM_particle,
	CL_DZIP_DEM_damage,
	CL_DZIP_DEM_spawnstatic,
	CL_DZIP_DEM_spawnbinary,
	CL_DZIP_DEM_spawnbaseline,
	CL_DZIP_DEM_temp_entity,
	CL_DZIP_DEM_setpause,
	CL_DZIP_DEM_signonnum,
	CL_DZIP_DEM_centerprint,
	CL_DZIP_DEM_killedmonster,
	CL_DZIP_DEM_foundsecret,
	CL_DZIP_DEM_spawnstaticsound,
	CL_DZIP_DEM_intermission,
	CL_DZIP_DEM_finale,
	CL_DZIP_DEM_cdtrack,
	CL_DZIP_DEM_sellscreen,
	CL_DZIP_DEM_cutscene,
	CL_DZIP_DZ_longtime,
	CL_DZIP_DEM_showlmp = 35,
	CL_DZIP_DEM_hidelmp,
	CL_DZIP_DEM_skybox,
	CL_DZIP_DZ_showlmp,
	CL_DZIP_DEM_bf = 40,
	CL_DZIP_DEM_fog,
	CL_DZIP_DEM_spawnbaseline2,
	CL_DZIP_DEM_spawnstatic2,
	CL_DZIP_DEM_spawnstaticsound2
};

#define CL_DZIP_IDENTIFIER_CLIENTDATA_FORCE	(0x40 | 0x10)
#define CL_DZIP_IDENTIFIER_CLIENTDATA_DIFF	(0x40)
#define CL_DZIP_IDENTIFIER_UPDATEENTITY_FORCE	(0x20 | 0x10 | 0x01)
#define CL_DZIP_IDENTIFIER_UPDATEENTITY_DIFF	(0x20 | 0x10)
#define CL_DZIP_IDENTIFIER_SOUND		(0x20 | 0x10 | 0x08)
#define CL_DZIP_IDENTIFIER_UPDATEENTITY2_FORCE	(0x20 | 0x10 | 0x02)
#define CL_DZIP_IDENTIFIER_SOUND_MOREBITS	(0x40 | 0x20)

#define CL_DZIP_CD_VELOCITY0_FORCE	0x0001
#define CL_DZIP_CD_VELOCITY1_FORCE	0x0002
#define CL_DZIP_CD_VELOCITY2_FORCE	0x0004
#define CL_DZIP_CD_MOREBITS_FORCE	0x0008
#define CL_DZIP_CD_PUNCH0_FORCE		0x0100
#define CL_DZIP_CD_PUNCH1_FORCE		0x0200
#define CL_DZIP_CD_PUNCH2_FORCE		0x0400
#define CL_DZIP_CD_VIEWHEIGHT_FORCE	0x0800
#define CL_DZIP_CD_IDEALPITCH_FORCE	0x1000
#define CL_DZIP_CD_WEAPONFRAME_FORCE	0x2000
#define CL_DZIP_CD_ARMOR_FORCE		0x4000
#define CL_DZIP_CD_WEAPON_FORCE		0x8000

#define CL_DZIP_CD_VELOCITY2_DIFF	0x0000000001ULL
#define CL_DZIP_CD_VELOCITY0_DIFF	0x0000000002ULL
#define CL_DZIP_CD_VELOCITY1_DIFF	0x0000000004ULL
#define CL_DZIP_CD_MOREBITS_DIFF	0x0000000008ULL
#define CL_DZIP_CD_WEAPONFRAME_DIFF	0x0000000100ULL
#define CL_DZIP_CD_ONGROUND_DIFF	0x0000000200ULL
#define CL_DZIP_CD_PUNCH0_DIFF		0x0000000400ULL
#define CL_DZIP_CD_AMMO_DIFF		0x0000000800ULL
#define CL_DZIP_CD_HEALTH_DIFF		0x0000001000ULL
#define CL_DZIP_CD_ITEMS_DIFF		0x0000002000ULL
#define CL_DZIP_CD_ARMOR_DIFF		0x0000004000ULL
#define CL_DZIP_CD_MOREBITS1_DIFF	0x0000008000ULL
#define CL_DZIP_CD_IDEALPITCH_DIFF	0x0000010000ULL
#define CL_DZIP_CD_SHELLS_DIFF		0x0000020000ULL
#define CL_DZIP_CD_NAILS_DIFF		0x0000040000ULL
#define CL_DZIP_CD_ROCKETS_DIFF		0x0000080000ULL
#define CL_DZIP_CD_WEAPON_DIFF		0x0000100000ULL
#define CL_DZIP_CD_WEAPONINDEX_DIFF	0x0000200000ULL
#define CL_DZIP_CD_INWATER_DIFF		0x0000400000ULL
#define CL_DZIP_CD_MOREBITS2_DIFF	0x0000800000ULL
#define CL_DZIP_CD_VIEWHEIGHT_DIFF	0x0001000000ULL
#define CL_DZIP_CD_CELLS_DIFF		0x0002000000ULL
#define CL_DZIP_CD_PUNCH1_DIFF		0x0004000000ULL
#define CL_DZIP_CD_PUNCH2_DIFF		0x0008000000ULL
#define CL_DZIP_CD_INVBIT_DIFF		0x0010000000ULL
#define CL_DZIP_CD_WEAPONFRAME2_DIFF	0x0020000000ULL
#define CL_DZIP_CD_AMMO2_DIFF		0x0040000000ULL
#define CL_DZIP_CD_MOREBITS3_DIFF	0x0080000000ULL
#define CL_DZIP_CD_ARMOR2_DIFF		0x0100000000ULL
#define CL_DZIP_CD_SHELLS2_DIFF		0x0200000000ULL
#define CL_DZIP_CD_NAILS2_DIFF		0x0400000000ULL
#define CL_DZIP_CD_ROCKETS2_DIFF	0x0800000000ULL
#define CL_DZIP_CD_WEAPON2_DIFF		0x1000000000ULL
#define CL_DZIP_CD_CELLS2_DIFF		0x2000000000ULL
#define CL_DZIP_CD_WEAPONALPHA_DIFF	0x4000000000ULL

#define CL_DZIP_SB_MOREBITS		0x0001
#define CL_DZIP_SB_FRAME		0x0004
#define CL_DZIP_SB_COLORMAP		0x0008
#define CL_DZIP_SB_SKIN			0x0010
#define CL_DZIP_SB_ORIGIN		0x0020
#define CL_DZIP_SB_ANGLE1		0x0040
#define CL_DZIP_SB_ANGLE0AND2		0x0080
#define CL_DZIP_SB_LARGEENTITY		0x0100
#define CL_DZIP_SB_LARGEMODEL		0x0200
#define CL_DZIP_SB_LARGEFRAME		0x0400
#define CL_DZIP_SB_ALPHA		0x0800

#define CL_DZIP_UE_ORIGIN1_FORCE	0x00000400ULL
#define CL_DZIP_UE_ANGLE0_FORCE		0x00000800ULL
#define CL_DZIP_UE_ANGLE1_FORCE		0x00001000ULL
#define CL_DZIP_UE_ANGLE2_FORCE		0x00002000ULL
#define CL_DZIP_UE_FRAME_FORCE		0x00004000ULL
#define CL_DZIP_UE_MOREBITS_FORCE	0x00008000ULL
#define CL_DZIP_UE_ORIGIN0_FORCE	0x00010000ULL
#define CL_DZIP_UE_ORIGIN2_FORCE	0x00020000ULL
#define CL_DZIP_UE_MODEL_FORCE		0x00040000ULL
#define CL_DZIP_UE_COLORMAP_FORCE	0x00080000ULL
#define CL_DZIP_UE_SKIN_FORCE		0x00100000ULL
#define CL_DZIP_UE_EFFECTS_FORCE	0x00200000ULL
#define CL_DZIP_UE_LONGENTITY_FORCE	0x00400000ULL
#define CL_DZIP_UE_TRANS_FORCE		0x00800000ULL
#define CL_DZIP_UE_MOREBITS2_FORCE	0x00800000ULL
#define CL_DZIP_UE_ALPHA_FORCE		0x01000000ULL
#define CL_DZIP_UE_SCALE_FORCE		0x02000000ULL
#define CL_DZIP_UE_LERPFINISH_FORCE	0x04000000ULL
#define CL_DZIP_UE_MODEL2_FORCE		0x08000000ULL
#define CL_DZIP_UE_FRAME2_FORCE		0x10000000ULL

#define CL_DZIP_UE_ORIGIN2_DIFF		0x000001ULL
#define CL_DZIP_UE_ORIGIN1_DIFF		0x000002ULL
#define CL_DZIP_UE_ORIGIN0_DIFF		0x000004ULL
#define CL_DZIP_UE_ANGLE0_DIFF		0x000008ULL
#define CL_DZIP_UE_ANGLE1_DIFF		0x000010ULL
#define CL_DZIP_UE_ANGLE2_DIFF		0x000020ULL
#define CL_DZIP_UE_FRAME_SINGLE_DIFF	0x000040ULL
#define CL_DZIP_UE_MOREBITS_DIFF	0x000080ULL
#define CL_DZIP_UE_FRAME_NORMAL_DIFF	0x000100ULL
#define CL_DZIP_UE_ORIGIN0_MOREBITS_DIFF	0x000200ULL
#define CL_DZIP_UE_ORIGIN1_MOREBITS_DIFF	0x000400ULL
#define CL_DZIP_UE_ORIGIN2_MOREBITS_DIFF	0x000800ULL
#define CL_DZIP_UE_EFFECTS_DIFF		0x001000ULL
#define CL_DZIP_UE_MODEL_DIFF		0x002000ULL
#define CL_DZIP_UE_NOLERP_DIFF		0x004000ULL
#define CL_DZIP_UE_MOREBITS2_DIFF	0x008000ULL
#define CL_DZIP_UE_COLORMAP_DIFF	0x010000ULL
#define CL_DZIP_UE_SKIN_DIFF		0x020000ULL
#define CL_DZIP_UE_NEHAHRA_ALPHA_DIFF	0x040000ULL
#define CL_DZIP_UE_NEHAHRA_FULLBRIGHT_DIFF	0x080000ULL
#define CL_DZIP_UE_ALPHA_DIFF		0x040000ULL
#define CL_DZIP_UE_SCALE_DIFF		0x080000ULL
#define CL_DZIP_UE_LERPFINISH_DIFF	0x100000ULL
#define CL_DZIP_UE_MODEL2_DIFF		0x200000ULL
#define CL_DZIP_UE_FRAME2_DIFF		0x400000ULL

typedef struct
{
	byte voz, pax;
	byte ang0, ang1, ang2;
	byte vel0, vel1, vel2;
	int32_t items;
	byte uk10, uk11, invbit;
	int16_t wpf, av, wpm;
	int health;
	int16_t am, sh, nl, rk, ce;
	byte wp;
	int force;
	byte weaponalpha;
} cl_dzip_cdata_t;

typedef struct
{
	uint32_t ptr;
	uint32_t size;
	uint32_t real;
	uint16_t len;
	uint16_t pak;
	uint32_t crc;
	uint32_t type;
	uint32_t date;
	uint32_t inter;
	char *name;
} cl_dzip_direntry_t;

typedef struct
{
	int16_t modelindex, frame;
	byte colormap, skin;
	byte effects;
	byte ang0, ang1, ang2;
	byte newbit, present, active;
	byte fullbright;
	int org0, org1, org2;
	int od0, od1, od2;
	int force;
	float alpha;
	byte transparency;
	byte scale;
	byte lerpfinish;
} cl_dzip_ent_t;

static const byte cl_dzip_te_size[] =
{
	8, 8, 8, 8, 8, 16, 16, 8, 8, 16, 8, 8, 10, 16, 8, 8, 14
};

static byte *cl_dzip_inblk;
static byte *cl_dzip_outblk;
static byte *cl_dzip_inptr;
static byte *cl_dzip_workbuf;
static byte *cl_dzip_zbuf;
static long cl_dzip_outlen;
static unsigned int cl_dzip_totalsize;
static cl_dzip_cdata_t cl_dzip_oldcd, cl_dzip_newcd;
static cl_dzip_ent_t cl_dzip_base[CL_DZIP_MAX_ENT];
static cl_dzip_ent_t cl_dzip_oldent[CL_DZIP_MAX_ENT];
static cl_dzip_ent_t cl_dzip_newent[CL_DZIP_MAX_ENT];
static int cl_dzip_entlink[CL_DZIP_MAX_ENT];
static byte cl_dzip_dem_updateframe;
static byte cl_dzip_copybaseline;
static long cl_dzip_dem_gametime;
static int cl_dzip_maxent;
static int cl_dzip_lastent;
static int cl_dzip_sble;
static unsigned long cl_dzip_cam0, cl_dzip_cam1, cl_dzip_cam2;
static int cl_dzip_protocol = PROTOCOL_NETQUAKE;
static unsigned int cl_dzip_protocolflags;
static int cl_dzip_decode_type;
static unsigned long cl_dzip_crctable[256];
static unsigned long cl_dzip_crcval;
static cl_dzip_direntry_t *cl_dzip_directory;
static int cl_dzip_numfiles;
static int cl_dzip_maj_ver, cl_dzip_min_ver;
static unsigned int cl_dzip_directory_offset;
static FILE *cl_dzip_archive_file;
static FILE *cl_dzip_output_file;
static long cl_dzip_archive_base;
static qofs_t cl_dzip_archive_size;
static z_stream cl_dzip_zs;
static unsigned int cl_dzip_ztotal;
static qboolean cl_dzip_crc_enabled;
static char cl_dzip_playback_temp_path[MAX_OSPATH];
static unsigned int cl_dzip_temp_serial;

#define CL_DZipDiscardMsg(x) (cl_dzip_inptr += (x))

static uint16_t CL_DZipReadLE16(const byte *buf)
{
	return (uint16_t)(buf[0] | (buf[1] << 8));
}

static uint32_t CL_DZipReadLE32(const byte *buf)
{
	return (uint32_t)buf[0]
		| ((uint32_t)buf[1] << 8)
		| ((uint32_t)buf[2] << 16)
		| ((uint32_t)buf[3] << 24);
}

static void CL_DZipWriteLE16(byte *buf, uint16_t value)
{
	buf[0] = (byte)(value & 0xff);
	buf[1] = (byte)((value >> 8) & 0xff);
}

static void CL_DZipWriteLE32(byte *buf, uint32_t value)
{
	buf[0] = (byte)(value & 0xff);
	buf[1] = (byte)((value >> 8) & 0xff);
	buf[2] = (byte)((value >> 16) & 0xff);
	buf[3] = (byte)((value >> 24) & 0xff);
}

static int16_t CL_DZipGetShort(const byte *buf)
{
	return (int16_t)CL_DZipReadLE16(buf);
}

static int32_t CL_DZipGetLong(const byte *buf)
{
	return (int32_t)CL_DZipReadLE32(buf);
}

static float CL_DZipGetFloat(const byte *buf)
{
	float f;
	uint32_t tmp = CL_DZipReadLE32(buf);
	memcpy(&f, &tmp, sizeof(f));
	return f;
}

static unsigned long CL_DZipCRCReflect(unsigned long x, int bits)
{
	int i;
	unsigned long value = 0;
	unsigned long bit = 1UL << (bits - 1);

	for (i = 0; i < bits; ++i)
	{
		if (x & 1)
			value += bit;
		x >>= 1;
		bit >>= 1;
	}

	return value;
}

static void CL_DZipCRCBuildTable(unsigned long table[256])
{
	unsigned long crcpol = 0x04c11db7;
	unsigned long i, j, k;

	for (i = 0; i < 256; ++i)
	{
		k = CL_DZipCRCReflect(i, 8) << 24;
		for (j = 0; j < 8; ++j)
			k = (k << 1) ^ ((k & 0x80000000UL) ? crcpol : 0);
		table[i] = CL_DZipCRCReflect(k, 32);
	}
}

static void CL_DZipCRCInit(void)
{
	CL_DZipCRCBuildTable(cl_dzip_crctable);
}

static void CL_DZipUpdateCRC(const unsigned long table[256], unsigned long *crc,
	const byte *ptr, int len)
{
	while (len-- > 0)
		*crc = (*crc >> 8) ^ table[(*crc & 0xff) ^ *ptr++];
}

static void CL_DZipMakeCRC(const byte *ptr, int len)
{
	CL_DZipUpdateCRC(cl_dzip_crctable, &cl_dzip_crcval, ptr, len);
}

static qboolean CL_DZipArchiveRead(void *buf, unsigned int num)
{
	if (!buf || !cl_dzip_archive_file)
		return false;

	return fread(buf, 1, num, cl_dzip_archive_file) == num;
}

static qboolean CL_DZipArchiveSeek(unsigned int pos)
{
	if (!cl_dzip_archive_file)
		return false;

	return fseek(cl_dzip_archive_file, cl_dzip_archive_base + (long)pos, SEEK_SET) == 0;
}

static qboolean CL_DZipOutputWrite(const void *buf, unsigned int num)
{
	if (num && cl_dzip_output_file && fwrite(buf, 1, num, cl_dzip_output_file) != num)
		return false;

	if (num && cl_dzip_crc_enabled)
		CL_DZipMakeCRC((const byte *)buf, (int)num);

	return true;
}

static void CL_DZipCopyMsg(unsigned int num)
{
	memcpy(cl_dzip_outblk + cl_dzip_outlen, cl_dzip_inptr, num);
	cl_dzip_outlen += num;
	cl_dzip_inptr += num;
}

static void CL_DZipInsertMsg(const void *msg, unsigned int num)
{
	memcpy(cl_dzip_outblk + cl_dzip_outlen, msg, num);
	cl_dzip_outlen += num;
}

static int CL_DZipBPlus(int x, int y)
{
	if (x >= 128)
		x -= 256;
	return y + x;
}

static void CL_DZipFreeDirectory(void)
{
	int i;

	if (!cl_dzip_directory)
		return;

	for (i = 0; i < cl_dzip_numfiles; ++i)
		free(cl_dzip_directory[i].name);

	free(cl_dzip_directory);
	cl_dzip_directory = NULL;
	cl_dzip_numfiles = 0;
}

static int CL_DZipZRead(int inlen)
{
	int toread, bsize, ret;

	toread = CL_DZIP_P_BLOCKSIZE - inlen;
	cl_dzip_zs.next_out = cl_dzip_inblk + inlen;
	cl_dzip_zs.avail_out = toread;
	cl_dzip_totalsize += (unsigned int)toread;

	while (cl_dzip_zs.avail_out)
	{
		if (!cl_dzip_zs.avail_in && cl_dzip_ztotal)
		{
			bsize = (cl_dzip_ztotal > CL_DZIP_Z_BUFFER_SIZE) ? CL_DZIP_Z_BUFFER_SIZE : (int)cl_dzip_ztotal;
			cl_dzip_ztotal -= bsize;
			if (!CL_DZipArchiveRead(cl_dzip_zbuf, bsize))
				return 0;
			cl_dzip_zs.next_in = cl_dzip_zbuf;
			cl_dzip_zs.avail_in = bsize;
		}

		ret = inflate(&cl_dzip_zs, Z_NO_FLUSH);
		if (ret == Z_STREAM_END)
			return 1;
		if (ret != Z_OK)
			return 0;
	}

	return 1;
}

static qboolean CL_DZipReadDirectory(const char *archive_name)
{
	byte header[12];
	byte entrybuf[CL_DZIP_DIR_DISK_SIZE];
	unsigned int offset;
	unsigned int i;
	unsigned int archive_bytes;
	unsigned int dir_disk_size;

	archive_bytes = (unsigned int)cl_dzip_archive_size;
	if (archive_bytes < sizeof(header))
	{
		Con_Printf("DZip: %s is not a valid dz file\n", archive_name);
		return false;
	}

	if (!CL_DZipArchiveSeek(0) || !CL_DZipArchiveRead(header, sizeof(header)))
		return false;

	if ((CL_DZipReadLE32(header) & 0xffff) != ('D' + ('Z' << 8)))
	{
		Con_Printf("DZip: %s is not a valid dz file\n", archive_name);
		return false;
	}

	cl_dzip_maj_ver = (CL_DZipReadLE32(header) >> 16) & 0xff;
	cl_dzip_min_ver = (CL_DZipReadLE32(header) >> 24) & 0xff;
	offset = CL_DZipReadLE32(header + 4);
	cl_dzip_directory_offset = offset;
	cl_dzip_numfiles = (int)CL_DZipReadLE32(header + 8);
	dir_disk_size = (cl_dzip_maj_ver == 1) ? (CL_DZIP_DIR_DISK_SIZE - 8) : CL_DZIP_DIR_DISK_SIZE;

	if (cl_dzip_maj_ver > CL_DZIP_MAJOR_VERSION)
	{
		Con_Printf("DZip: %s was compressed with version %u.%u\n", archive_name, cl_dzip_maj_ver, cl_dzip_min_ver);
		return false;
	}

	if (cl_dzip_numfiles <= 0 || offset >= archive_bytes)
	{
		Con_Printf("DZip: %s has a corrupt directory\n", archive_name);
		return false;
	}

	if ((unsigned int)cl_dzip_numfiles > archive_bytes / dir_disk_size)
	{
		Con_Printf("DZip: %s has too many directory entries\n", archive_name);
		return false;
	}

	cl_dzip_directory = (cl_dzip_direntry_t *)calloc((size_t)cl_dzip_numfiles, sizeof(*cl_dzip_directory));
	if (!cl_dzip_directory)
	{
		Con_Printf("DZip: out of memory reading %s\n", archive_name);
		return false;
	}

	if (!CL_DZipArchiveSeek(offset))
		return false;

	for (i = 0; i < (unsigned int)cl_dzip_numfiles; ++i)
	{
		cl_dzip_direntry_t *de = &cl_dzip_directory[i];
		unsigned int j;

		memset(entrybuf, 0, sizeof(entrybuf));
		if (!CL_DZipArchiveRead(entrybuf, dir_disk_size))
		{
			Con_Printf("DZip: %s has a truncated directory\n", archive_name);
			return false;
		}

		de->ptr = CL_DZipReadLE32(entrybuf + 0);
		de->size = CL_DZipReadLE32(entrybuf + 4);
		de->real = CL_DZipReadLE32(entrybuf + 8);
		de->len = CL_DZipReadLE16(entrybuf + 12);
		de->pak = CL_DZipReadLE16(entrybuf + 14);
		de->crc = CL_DZipReadLE32(entrybuf + 16);
		de->type = CL_DZipReadLE32(entrybuf + 20);
		de->date = CL_DZipReadLE32(entrybuf + 24);
		de->inter = CL_DZipReadLE32(entrybuf + 28);

		if (de->type == CL_DZIP_TYPE_DEMV1 && cl_dzip_maj_ver > 1)
		{
			Con_Printf("DZip: %s has an invalid legacy demo entry\n", archive_name);
			return false;
		}

		if (de->len == 0 || de->len > MAX_OSPATH)
		{
			Con_Printf("DZip: %s has an invalid entry name length\n", archive_name);
			return false;
		}

		if (cl_dzip_maj_ver > 1 && (de->ptr > archive_bytes || de->size > archive_bytes - de->ptr))
		{
			Con_Printf("DZip: %s has a corrupt directory entry\n", archive_name);
			return false;
		}

		de->name = (char *)malloc(de->len);
		if (!de->name)
		{
			Con_Printf("DZip: out of memory reading %s\n", archive_name);
			return false;
		}

		if (!CL_DZipArchiveRead(de->name, de->len))
		{
			Con_Printf("DZip: %s has a truncated directory entry\n", archive_name);
			return false;
		}

		de->name[de->len - 1] = '\0';
		for (j = 0; j + 1 < de->len; ++j)
			if (de->name[j] == '\\')
				de->name[j] = '/';
	}

	return true;
}

static qboolean CL_DZipEntryLooksPlayableDemo(const cl_dzip_direntry_t *de)
{
	const char *ext;

	if (!de || !de->name || !de->name[0])
		return false;

	ext = COM_FileGetExtension(de->name);
	if (q_strcasecmp(ext, "dem"))
		return false;

	switch (de->type)
	{
	case CL_DZIP_TYPE_DEMV1:
	case CL_DZIP_TYPE_DEM:
	case CL_DZIP_TYPE_NORMAL:
	case CL_DZIP_TYPE_STORE:
		return true;
	default:
		return false;
	}
}

static cl_dzip_direntry_t *CL_DZipFindDemoEntry(const char *archive_name)
{
	char wanted[MAX_QPATH];
	int i;
	cl_dzip_direntry_t *fallback = NULL;

	COM_FileBase(COM_SkipPath(archive_name), wanted, sizeof(wanted));

	for (i = 0; i < cl_dzip_numfiles; ++i)
	{
		cl_dzip_direntry_t *de = &cl_dzip_directory[i];
		char basename[MAX_QPATH];

		if (!CL_DZipEntryLooksPlayableDemo(de))
			continue;

		if (!fallback)
			fallback = de;

		COM_FileBase(COM_SkipPath(de->name), basename, sizeof(basename));
		if (!q_strcasecmp(basename, wanted))
			return de;
	}

	return fallback;
}

static qboolean CL_DZipMakePlaybackTempPath(char *out, size_t outsize)
{
	char demodir[MAX_OSPATH];

	q_snprintf(demodir, sizeof(demodir), "%s/demos", com_gamedir);
	Sys_mkdir(demodir);

	++cl_dzip_temp_serial;
	q_snprintf(out, outsize, "%s/demos/__dzip_playback_%u.dem", com_gamedir, cl_dzip_temp_serial);
	return true;
}

static void CL_DZipCleanupTempDemo(void)
{
	if (cl_dzip_playback_temp_path[0])
	{
		remove(cl_dzip_playback_temp_path);
		cl_dzip_playback_temp_path[0] = '\0';
	}
}

static qboolean CL_DZipAllocDecodeBuffers(void)
{
	if (cl_dzip_workbuf)
		return true;

	cl_dzip_workbuf = (byte *)malloc(CL_DZIP_P_BLOCKSIZE * 2 + CL_DZIP_Z_BUFFER_SIZE);
	if (!cl_dzip_workbuf)
		return false;

	cl_dzip_inblk = cl_dzip_workbuf;
	cl_dzip_outblk = cl_dzip_inblk + CL_DZIP_P_BLOCKSIZE;
	cl_dzip_zbuf = cl_dzip_outblk + CL_DZIP_P_BLOCKSIZE;
	return true;
}

static void CL_DZipFreeDecodeBuffers(void)
{
	free(cl_dzip_workbuf);
	cl_dzip_workbuf = NULL;
	cl_dzip_inblk = NULL;
	cl_dzip_outblk = NULL;
	cl_dzip_zbuf = NULL;
}

static void CL_DZipCreateClientdataMsg(void)
{
	byte buf[64];
	byte *ptr = buf + 3;
	int mask = cl_dzip_newcd.invbit ? 0 : SU_ITEMS;
	uint32_t tmp;

	buf[0] = CL_DZIP_DEM_clientdata;

#define CL_DZIP_CMADD(x, def, bit, forcebit) \
	if ((cl_dzip_newcd.x & 0xff) != (def) || (cl_dzip_newcd.force & (forcebit))) \
	{ \
		mask |= (bit); \
		*ptr++ = cl_dzip_newcd.x & 0xff; \
	}
#define CL_DZIP_CMADD_2HI(x, def, bit) \
	if ((cl_dzip_newcd.x >> 8) != (def)) \
	{ \
		mask |= (bit); \
		*ptr++ = (cl_dzip_newcd.x >> 8); \
	}

	CL_DZIP_CMADD(voz, 22, SU_VIEWHEIGHT, CL_DZIP_CD_VIEWHEIGHT_FORCE);
	CL_DZIP_CMADD(pax, 0, SU_IDEALPITCH, CL_DZIP_CD_IDEALPITCH_FORCE);
	CL_DZIP_CMADD(ang0, 0, CL_DZIP_SU_PUNCH0, CL_DZIP_CD_PUNCH0_FORCE);
	CL_DZIP_CMADD(vel0, 0, CL_DZIP_SU_VELOCITY0, CL_DZIP_CD_VELOCITY0_FORCE);
	CL_DZIP_CMADD(ang1, 0, CL_DZIP_SU_PUNCH1, CL_DZIP_CD_PUNCH1_FORCE);
	CL_DZIP_CMADD(vel1, 0, CL_DZIP_SU_VELOCITY1, CL_DZIP_CD_VELOCITY1_FORCE);
	CL_DZIP_CMADD(ang2, 0, CL_DZIP_SU_PUNCH2, CL_DZIP_CD_PUNCH2_FORCE);
	CL_DZIP_CMADD(vel2, 0, CL_DZIP_SU_VELOCITY2, CL_DZIP_CD_VELOCITY2_FORCE);

	tmp = LittleLong((uint32_t)cl_dzip_newcd.items);
	memcpy(ptr, &tmp, 4);
	ptr += 4;

	if (cl_dzip_newcd.uk10)
		mask |= SU_ONGROUND;
	if (cl_dzip_newcd.uk11)
		mask |= SU_INWATER;

	CL_DZIP_CMADD(wpf, 0, SU_WEAPONFRAME, CL_DZIP_CD_WEAPONFRAME_FORCE);
	CL_DZIP_CMADD(av, 0, SU_ARMOR, CL_DZIP_CD_ARMOR_FORCE);
	CL_DZIP_CMADD(wpm, 0, SU_WEAPON, CL_DZIP_CD_WEAPON_FORCE);

	CL_DZipWriteLE16(ptr, (uint16_t)cl_dzip_newcd.health);
	ptr += 2;
	*ptr++ = cl_dzip_newcd.am;
	*ptr++ = cl_dzip_newcd.sh;
	*ptr++ = cl_dzip_newcd.nl;
	*ptr++ = cl_dzip_newcd.rk;
	*ptr++ = cl_dzip_newcd.ce;
	*ptr++ = cl_dzip_newcd.wp;

	CL_DZIP_CMADD_2HI(wpm, 0, SU_WEAPON2);
	CL_DZIP_CMADD_2HI(av, 0, SU_ARMOR2);
	CL_DZIP_CMADD_2HI(am, 0, SU_AMMO2);
	CL_DZIP_CMADD_2HI(sh, 0, SU_SHELLS2);
	CL_DZIP_CMADD_2HI(nl, 0, SU_NAILS2);
	CL_DZIP_CMADD_2HI(rk, 0, SU_ROCKETS2);
	CL_DZIP_CMADD_2HI(ce, 0, SU_CELLS2);
	CL_DZIP_CMADD_2HI(wpf, 0, SU_WEAPONFRAME2);

	if (cl_dzip_newcd.weaponalpha != cl_dzip_oldcd.weaponalpha)
	{
		mask |= SU_WEAPONALPHA;
		*ptr++ = cl_dzip_newcd.weaponalpha;
	}

	if (mask & 0xff000000)
	{
		memmove(buf + 5, buf + 3, (size_t)(ptr - (buf + 3)));
		ptr += 2;
	}
	else if (mask & 0xff0000)
	{
		memmove(buf + 4, buf + 3, (size_t)(ptr - (buf + 3)));
		ptr += 1;
	}

	if (mask & 0xff000000)
	{
		buf[4] = (mask >> 24) & 0xff;
		mask |= SU_EXTEND2;
	}
	if (mask & 0xffff0000)
	{
		buf[3] = (mask >> 16) & 0xff;
		mask |= SU_EXTEND1;
	}

	CL_DZipWriteLE16(buf + 1, (uint16_t)(mask & 0xffff));
	CL_DZipInsertMsg(buf, (unsigned int)(ptr - buf));

	cl_dzip_oldcd = cl_dzip_newcd;

#undef CL_DZIP_CMADD
#undef CL_DZIP_CMADD_2HI
}

static void CL_DZipDemv1Clientdata(void)
{
	byte *ptr = cl_dzip_inptr;
	int mask = *ptr++;

	if (mask & 0x01) mask += *ptr++ << 8;
	if (mask & 0x0100) mask += *ptr++ << 16;
	if (mask & 0x010000) mask += *ptr++ << 24;

#define CL_DZIP_DEMV1_CPLUS(x, bit) \
	if (mask & (bit)) cl_dzip_newcd.x = CL_DZipBPlus(*ptr++, cl_dzip_oldcd.x)

	CL_DZIP_DEMV1_CPLUS(voz, 0x01000000);
	CL_DZIP_DEMV1_CPLUS(pax, 0x00100000);
	CL_DZIP_DEMV1_CPLUS(ang0, 0x08000000);
	CL_DZIP_DEMV1_CPLUS(ang1, 0x04000000);
	CL_DZIP_DEMV1_CPLUS(ang2, 0x02000000);
	CL_DZIP_DEMV1_CPLUS(vel0, 0x00000008);
	CL_DZIP_DEMV1_CPLUS(vel1, 0x00000004);
	CL_DZIP_DEMV1_CPLUS(vel2, 0x00000002);
	if (mask & 0x00008000) cl_dzip_newcd.uk10 = !cl_dzip_oldcd.uk10;
	if (mask & 0x00400000) cl_dzip_newcd.uk11 = !cl_dzip_oldcd.uk11;
	if (mask & 0x10000000) cl_dzip_newcd.invbit = !cl_dzip_oldcd.invbit;
	if (mask & 0x00200000)
	{
		cl_dzip_newcd.items += CL_DZipGetLong(ptr);
		ptr += 4;
	}
	CL_DZIP_DEMV1_CPLUS(wpf, 0x00004000);
	CL_DZIP_DEMV1_CPLUS(av, 0x00080000);
	CL_DZIP_DEMV1_CPLUS(wpm, 0x00020000);
	if (mask & 0x00040000)
	{
		cl_dzip_newcd.health += CL_DZipGetShort(ptr);
		ptr += 2;
	}
	CL_DZIP_DEMV1_CPLUS(am, 0x00002000);
	CL_DZIP_DEMV1_CPLUS(sh, 0x00001000);
	CL_DZIP_DEMV1_CPLUS(nl, 0x00000800);
	CL_DZIP_DEMV1_CPLUS(rk, 0x00000400);
	CL_DZIP_DEMV1_CPLUS(ce, 0x00800000);
	CL_DZIP_DEMV1_CPLUS(wp, 0x00000200);

	CL_DZipDiscardMsg((int)(ptr - cl_dzip_inptr));

	if ((*ptr & 0xf0) == 0xe0)
	{
		mask = *ptr++;
		if (mask & 0x08) mask |= *ptr++ << 8;
		cl_dzip_newcd.force ^= mask & 0xff07;
		CL_DZipDiscardMsg((int)(ptr - cl_dzip_inptr));
	}

	CL_DZipCreateClientdataMsg();

#undef CL_DZIP_DEMV1_CPLUS
}

static void CL_DZipDemv1Updateentity(void)
{
	byte *ptr = cl_dzip_inptr + 1;
	byte code = *cl_dzip_inptr;
	int mask, entity;
	cl_dzip_ent_t n, o;

	cl_dzip_dem_updateframe = 1;

	if (code == 0x82)
	{
		CL_DZipDiscardMsg(1);
		return;
	}

	if (code == 0x83)
	{
		while ((entity = CL_DZipGetShort(ptr)))
		{
			ptr += 2;
			memcpy(cl_dzip_newent + entity, cl_dzip_base + entity, sizeof(cl_dzip_ent_t));
			memcpy(cl_dzip_oldent + entity, cl_dzip_base + entity, sizeof(cl_dzip_ent_t));
		}
		CL_DZipDiscardMsg((int)(ptr - cl_dzip_inptr + 2));
		return;
	}

	if (code == 0x84)
	{
		while ((mask = CL_DZipGetShort(ptr)))
		{
			ptr += 2;
			mask &= 0xffff;
			if (mask & CL_DZIP_UE_MOREBITS_FORCE) mask |= *ptr++ << 16;
			entity = mask & 0x3ff;
			if (entity > cl_dzip_maxent) cl_dzip_maxent = entity;
			cl_dzip_newent[entity].force ^= mask & 0xfffc00;
		}
		CL_DZipDiscardMsg((int)(ptr - cl_dzip_inptr + 2));
		return;
	}

	for (;;)
	{
		if (code == 0x81)
		{
			mask = (*ptr++ << 8) + 1;
			code = 0x80;
		}
		else
		{
			mask = CL_DZipGetShort(ptr) & 0xffff;
			ptr += 2;
		}

		if (mask & 0x8000) mask += (*ptr++) << 16;
		if (mask & 0x800000) mask += (*ptr++) << 24;

		entity = mask & 0x1ff;
		if (mask & 0x08000000) entity += 0x200;
		if (entity > cl_dzip_maxent) cl_dzip_maxent = entity;
		if (!entity) break;

		n = cl_dzip_newent[entity];
		o = cl_dzip_oldent[entity];
		n.present = 1;
		if (mask & 0x010000) n.modelindex = *ptr++;
		if (mask & 0x0200) n.frame = o.frame + 1;
		if (mask & 0x080000) n.frame = CL_DZipBPlus(*ptr++, o.frame);
		if (mask & 0x01000000) n.colormap = *ptr++;
		if (mask & 0x02000000) n.skin = *ptr++;
		if (mask & 0x04000000) n.effects = *ptr++;
		if (mask & 0x0400) n.org0 = CL_DZipBPlus(*ptr++, o.org0);
		if (mask & 0x100000) { n.org0 = CL_DZipGetShort(ptr); ptr += 2; }
		if (mask & 0x2000) n.ang0 = CL_DZipBPlus(*ptr++, o.ang0);
		if (mask & 0x0800) n.org1 = CL_DZipBPlus(*ptr++, o.org1);
		if (mask & 0x200000) { n.org1 = CL_DZipGetShort(ptr); ptr += 2; }
		if (mask & 0x4000) n.ang1 = CL_DZipBPlus(*ptr++, o.ang1);
		if (mask & 0x1000) n.org2 = CL_DZipBPlus(*ptr++, o.org2);
		if (mask & 0x400000) { n.org2 = CL_DZipGetShort(ptr); ptr += 2; }
		if (mask & 0x020000) n.ang2 = CL_DZipBPlus(*ptr++, o.ang2);
		if (mask & 0x040000) n.newbit = !o.newbit;
		cl_dzip_newent[entity] = n;
	}

	CL_DZipDiscardMsg((int)(ptr - cl_dzip_inptr));
}

static void CL_DZipDemv1Dxentities(void)
{
	byte buf[32];
	byte *ptr;
	int i, mask;

	for (i = 1; i <= cl_dzip_maxent; ++i)
	{
		cl_dzip_ent_t n = cl_dzip_newent[i];
		cl_dzip_ent_t b = cl_dzip_base[i];

		if (!n.present)
			continue;

		ptr = buf + 2;
		mask = U_SIGNAL;

		if (i > 0xff || (n.force & CL_DZIP_UE_LONGENTITY_FORCE))
		{
			CL_DZipWriteLE16(ptr, (uint16_t)i);
			ptr += 2;
			mask |= U_LONGENTITY;
		}
		else
		{
			*ptr++ = i;
		}

#define CL_DZIP_DEMV1_BDIFF(field, bit, forcebit) \
		if ((n.field) != (b.field) || (n.force & (forcebit))) \
		{ \
			*ptr++ = (n.field); \
			mask |= (bit); \
		}

		CL_DZIP_DEMV1_BDIFF(modelindex, U_MODEL, CL_DZIP_UE_MODEL_FORCE);
		CL_DZIP_DEMV1_BDIFF(frame, U_FRAME, CL_DZIP_UE_FRAME_FORCE);
		CL_DZIP_DEMV1_BDIFF(colormap, U_COLORMAP, CL_DZIP_UE_COLORMAP_FORCE);
		CL_DZIP_DEMV1_BDIFF(skin, U_SKIN, CL_DZIP_UE_SKIN_FORCE);
		CL_DZIP_DEMV1_BDIFF(effects, U_EFFECTS, CL_DZIP_UE_EFFECTS_FORCE);
		if (n.org0 != b.org0 || (n.force & CL_DZIP_UE_ORIGIN0_FORCE)) { mask |= CL_DZIP_U_ORIGIN0; CL_DZipWriteLE16(ptr, (uint16_t)n.org0); ptr += 2; }
		CL_DZIP_DEMV1_BDIFF(ang0, CL_DZIP_U_ANGLE0, CL_DZIP_UE_ANGLE0_FORCE);
		if (n.org1 != b.org1 || (n.force & CL_DZIP_UE_ORIGIN1_FORCE)) { mask |= CL_DZIP_U_ORIGIN1; CL_DZipWriteLE16(ptr, (uint16_t)n.org1); ptr += 2; }
		CL_DZIP_DEMV1_BDIFF(ang1, CL_DZIP_U_ANGLE1, CL_DZIP_UE_ANGLE1_FORCE);
		if (n.org2 != b.org2 || (n.force & CL_DZIP_UE_ORIGIN2_FORCE)) { mask |= CL_DZIP_U_ORIGIN2; CL_DZipWriteLE16(ptr, (uint16_t)n.org2); ptr += 2; }
		CL_DZIP_DEMV1_BDIFF(ang2, CL_DZIP_U_ANGLE2, CL_DZIP_UE_ANGLE2_FORCE);
		if (n.newbit) mask |= CL_DZIP_U_NOLERP;

		if (mask & 0xff00) mask |= U_MOREBITS;
		buf[0] = mask & 0xff;
		buf[1] = (mask & 0xff00) >> 8;
		if (!(mask & U_MOREBITS))
		{
			memmove(buf + 1, buf + 2, (size_t)(ptr - buf - 2));
			--ptr;
		}

		CL_DZipInsertMsg(buf, (unsigned int)(ptr - buf));
		memcpy(cl_dzip_oldent + i, cl_dzip_newent + i, sizeof(cl_dzip_ent_t));

#undef CL_DZIP_DEMV1_BDIFF
	}
}

static void CL_DZipDemxNop(void) { CL_DZipCopyMsg(1); }
static void CL_DZipDemxDisconnect(void) { CL_DZipCopyMsg(1); }
static void CL_DZipDemxUpdatestat(void) { CL_DZipCopyMsg(6); }
static void CL_DZipDemxVersion(void) { CL_DZipCopyMsg(5); }
static void CL_DZipDemxSetview(void) { CL_DZipCopyMsg(3); }
static void CL_DZipDemxSetangle(void) { CL_DZipCopyMsg(4); }
static void CL_DZipDemxStopsound(void) { CL_DZipCopyMsg(3); }
static void CL_DZipDemxUpdatecolors(void) { CL_DZipCopyMsg(3); }
static void CL_DZipDemxParticle(void) { CL_DZipCopyMsg(12); }
static void CL_DZipDemxDamage(void) { CL_DZipCopyMsg(9); }
static void CL_DZipDemxSpawnstatic(void) { CL_DZipCopyMsg(14); }
static void CL_DZipDemxSpawnbinary(void) { CL_DZipCopyMsg(1); }
static void CL_DZipDemxSetpause(void) { CL_DZipCopyMsg(2); }
static void CL_DZipDemxSignonnum(void) { CL_DZipCopyMsg(2); }
static void CL_DZipDemxKilledmonster(void) { CL_DZipCopyMsg(1); }
static void CL_DZipDemxFoundsecret(void) { CL_DZipCopyMsg(1); }
static void CL_DZipDemxSpawnstaticsound(void) { CL_DZipCopyMsg(10); }
static void CL_DZipDemxIntermission(void) { CL_DZipCopyMsg(1); }
static void CL_DZipDemxCdtrack(void) { CL_DZipCopyMsg(3); }
static void CL_DZipDemxSellscreen(void) { CL_DZipCopyMsg(1); }
static void CL_DZipDemxUpdatefrags(void) { CL_DZipCopyMsg(4); }
static void CL_DZipDemxFog(void) { CL_DZipCopyMsg(6); }
static void CL_DZipDemxSpawnstaticsound2(void) { CL_DZipCopyMsg(11); }

static void CL_DZipDemxString(void)
{
	byte *ptr = cl_dzip_inptr + 1;
	while (*ptr++)
		;
	CL_DZipCopyMsg((unsigned int)(ptr - cl_dzip_inptr));
}

static void CL_DZipDemxSound(void)
{
	int c;
	int len;
	unsigned int entity;
	byte mask;
	byte channel;
	byte *ptr = cl_dzip_inptr + 1;

	if (*cl_dzip_inptr > CL_DZIP_DEM_sound)
	{
		len = 10;
		mask = *cl_dzip_inptr & 0x07;
		if (!(*cl_dzip_inptr & SND_LARGEENTITY))
			mask |= SND_LARGEENTITY;
		if (!(*cl_dzip_inptr & SND_LARGESOUND))
			mask |= SND_LARGESOUND;
	}
	else
	{
		len = 11;
		mask = *ptr++;
		++cl_dzip_inptr;
	}

	if (mask & SND_VOLUME) { ++len; ++ptr; }
	if (mask & SND_ATTENUATION) { ++len; ++ptr; }

	if (cl_dzip_decode_type == CL_DZIP_TYPE_DEMV1)
	{
		CL_DZipCopyMsg((unsigned int)len);
		return;
	}

	*cl_dzip_inptr = CL_DZIP_DEM_sound;
	CL_DZipInsertMsg(cl_dzip_inptr, 1);

	*cl_dzip_inptr = mask;
	if (mask & SND_LARGEENTITY)
	{
		entity = (unsigned int)(uint16_t)CL_DZipGetShort(ptr);
		ptr += 2;
		channel = *ptr++;
	}
	else
	{
		channel = *ptr & 7;
		*ptr = (*ptr & 0xf8) + ((2 - channel) & 7);
		entity = (unsigned int)((uint16_t)CL_DZipGetShort(ptr) >> 3);
		ptr += 2;
	}

	if (mask & SND_LARGESOUND)
		ptr += 2;
	else
		++ptr;

	if (entity < CL_DZIP_MAX_ENT)
	{
		c = CL_DZipGetShort(ptr) + cl_dzip_newent[entity].org0;
		CL_DZipWriteLE16(ptr, (uint16_t)c);
		ptr += 2;
		c = CL_DZipGetShort(ptr) + cl_dzip_newent[entity].org1;
		CL_DZipWriteLE16(ptr, (uint16_t)c);
		ptr += 2;
		c = CL_DZipGetShort(ptr) + cl_dzip_newent[entity].org2;
		CL_DZipWriteLE16(ptr, (uint16_t)c);
		ptr += 2;
	}

	CL_DZipCopyMsg((unsigned int)(ptr - cl_dzip_inptr));
}

static void CL_DZipDemxLongtime(void)
{
	int32_t tmp = CL_DZipGetLong(cl_dzip_inptr + 1);
	cl_dzip_dem_gametime += tmp;
	*cl_dzip_inptr = CL_DZIP_DEM_time;
	CL_DZipWriteLE32(cl_dzip_inptr + 1, (uint32_t)cl_dzip_dem_gametime);
	CL_DZipCopyMsg(5);
}

static void CL_DZipDemxTime(void)
{
	byte buf[5];
	uint32_t tmp = (uint32_t)(uint16_t)CL_DZipGetShort(cl_dzip_inptr + 1);

	if (cl_dzip_decode_type == CL_DZIP_TYPE_DEMV1)
	{
		CL_DZipDemxLongtime();
		return;
	}

	cl_dzip_dem_gametime += (long)tmp;
	buf[0] = CL_DZIP_DEM_time;
	CL_DZipWriteLE32(buf + 1, (uint32_t)cl_dzip_dem_gametime);
	CL_DZipInsertMsg(buf, sizeof(buf));
	CL_DZipDiscardMsg(3);
}

static qboolean CL_DZipDemxServerinfo(void)
{
	byte *ptr = cl_dzip_inptr + 1;
	byte *start_ptr;

	cl_dzip_protocol = CL_DZipGetLong(ptr);
	ptr += sizeof(uint32_t);

	if (cl_dzip_protocol == PROTOCOL_RMQ)
	{
		const unsigned int unsupported_flags = PRFL_SHORTANGLE | PRFL_FLOATANGLE |
			PRFL_24BITCOORD | PRFL_FLOATCOORD | PRFL_EDICTSCALE |
			PRFL_INT32COORD | PRFL_MOREFLAGS;
		const unsigned int known_flags = unsupported_flags | PRFL_ALPHASANITY;

		cl_dzip_protocolflags = (unsigned int)CL_DZipGetLong(ptr);
		ptr += sizeof(uint32_t);

		if (cl_dzip_protocolflags & unsupported_flags)
		{
			Con_Printf("DZip: PROTOCOL_RMQ demos with protocolflags %#x are not supported in .dz archives\n",
				cl_dzip_protocolflags);
			return false;
		}

		if (cl_dzip_protocolflags & ~known_flags)
		{
			Con_Printf("DZip: PROTOCOL_RMQ demo uses unknown protocolflags %#x\n",
				cl_dzip_protocolflags);
			return false;
		}
	}
	else
	{
		cl_dzip_protocolflags = 0;
	}

	if (cl_dzip_protocol != PROTOCOL_NETQUAKE &&
		cl_dzip_protocol != PROTOCOL_FITZQUAKE &&
		cl_dzip_protocol != PROTOCOL_RMQ)
	{
		Con_Printf("DZip: unknown demo protocol %u\n", (unsigned)cl_dzip_protocol);
		return false;
	}

	++ptr;
	++ptr;

	while (*ptr++)
		;
	do
	{
		start_ptr = ptr;
		while (*ptr++)
			;
	} while (ptr - start_ptr > 1);
	do
	{
		start_ptr = ptr;
		while (*ptr++)
			;
	} while (ptr - start_ptr > 1);

	CL_DZipCopyMsg((unsigned int)(ptr - cl_dzip_inptr));
	cl_dzip_sble = 0;
	return true;
}

static void CL_DZipDemxLightstyle(void)
{
	byte *ptr = cl_dzip_inptr + 2;
	while (*ptr++)
		;
	CL_DZipCopyMsg((unsigned int)(ptr - cl_dzip_inptr));
}

static void CL_DZipDemxUpdatename(void)
{
	byte *ptr = cl_dzip_inptr + 2;
	while (*ptr++)
		;
	CL_DZipCopyMsg((unsigned int)(ptr - cl_dzip_inptr));
}

static void CL_DZipDemxClientdata(void)
{
	byte *ptr = cl_dzip_inptr;
	uint64_t mask = *ptr++;

	cl_dzip_newcd = cl_dzip_oldcd;

	if (cl_dzip_decode_type == CL_DZIP_TYPE_DEMV1)
	{
		CL_DZipDemv1Clientdata();
		return;
	}

	if (mask & CL_DZIP_CD_MOREBITS_DIFF) mask |= (uint64_t)(*ptr++) << 8;
	if (mask & CL_DZIP_CD_MOREBITS1_DIFF) mask |= (uint64_t)(*ptr++) << 16;
	if (mask & CL_DZIP_CD_MOREBITS2_DIFF) mask |= (uint64_t)(*ptr++) << 24;
	if (mask & CL_DZIP_CD_MOREBITS3_DIFF) mask |= (uint64_t)(*ptr++) << 32;

#define CL_DZIP_CPLUS(x, bit) \
	if (mask & (bit)) cl_dzip_newcd.x = (byte)CL_DZipBPlus(*ptr++, cl_dzip_oldcd.x)
#define CL_DZIP_CPLUS_2(x, bit, bit2) \
	if (mask & (bit2)) \
	{ \
		cl_dzip_newcd.x = CL_DZipGetShort(ptr); \
		ptr += 2; \
	} \
	else if (mask & (bit)) \
	{ \
		if (cl_dzip_oldcd.x & 0xff00) \
			cl_dzip_newcd.x = CL_DZipBPlus(*ptr++, cl_dzip_oldcd.x); \
		else \
			cl_dzip_newcd.x = (byte)CL_DZipBPlus(*ptr++, cl_dzip_oldcd.x); \
	}

	CL_DZIP_CPLUS(vel2, CL_DZIP_CD_VELOCITY2_DIFF);
	CL_DZIP_CPLUS(vel0, CL_DZIP_CD_VELOCITY0_DIFF);
	CL_DZIP_CPLUS(vel1, CL_DZIP_CD_VELOCITY1_DIFF);
	CL_DZIP_CPLUS_2(wpf, CL_DZIP_CD_WEAPONFRAME_DIFF, CL_DZIP_CD_WEAPONFRAME2_DIFF);
	if (mask & CL_DZIP_CD_ONGROUND_DIFF) cl_dzip_newcd.uk10 = !cl_dzip_oldcd.uk10;
	CL_DZIP_CPLUS(ang0, CL_DZIP_CD_PUNCH0_DIFF);
	CL_DZIP_CPLUS_2(am, CL_DZIP_CD_AMMO_DIFF, CL_DZIP_CD_AMMO2_DIFF);
	if (mask & CL_DZIP_CD_HEALTH_DIFF) { cl_dzip_newcd.health += CL_DZipGetShort(ptr); ptr += 2; }
	if (mask & CL_DZIP_CD_ITEMS_DIFF) { cl_dzip_newcd.items ^= CL_DZipGetLong(ptr); ptr += 4; }
	CL_DZIP_CPLUS_2(av, CL_DZIP_CD_ARMOR_DIFF, CL_DZIP_CD_ARMOR2_DIFF);
	CL_DZIP_CPLUS(pax, CL_DZIP_CD_IDEALPITCH_DIFF);
	CL_DZIP_CPLUS_2(sh, CL_DZIP_CD_SHELLS_DIFF, CL_DZIP_CD_SHELLS2_DIFF);
	CL_DZIP_CPLUS_2(nl, CL_DZIP_CD_NAILS_DIFF, CL_DZIP_CD_NAILS2_DIFF);
	CL_DZIP_CPLUS_2(rk, CL_DZIP_CD_ROCKETS_DIFF, CL_DZIP_CD_ROCKETS2_DIFF);
	CL_DZIP_CPLUS_2(wpm, CL_DZIP_CD_WEAPON_DIFF, CL_DZIP_CD_WEAPON2_DIFF);
	CL_DZIP_CPLUS(wp, CL_DZIP_CD_WEAPONINDEX_DIFF);
	if (mask & CL_DZIP_CD_INWATER_DIFF) cl_dzip_newcd.uk11 = !cl_dzip_oldcd.uk11;
	CL_DZIP_CPLUS(voz, CL_DZIP_CD_VIEWHEIGHT_DIFF);
	CL_DZIP_CPLUS_2(ce, CL_DZIP_CD_CELLS_DIFF, CL_DZIP_CD_CELLS2_DIFF);
	CL_DZIP_CPLUS(ang1, CL_DZIP_CD_PUNCH1_DIFF);
	CL_DZIP_CPLUS(ang2, CL_DZIP_CD_PUNCH2_DIFF);
	if (mask & CL_DZIP_CD_INVBIT_DIFF) cl_dzip_newcd.invbit = !cl_dzip_oldcd.invbit;
	CL_DZIP_CPLUS(weaponalpha, CL_DZIP_CD_WEAPONALPHA_DIFF);

	CL_DZipDiscardMsg((int)(ptr - cl_dzip_inptr));

	if ((*ptr & 0xf0) == CL_DZIP_IDENTIFIER_CLIENTDATA_FORCE)
	{
		mask = *ptr++;
		if (mask & CL_DZIP_CD_MOREBITS_FORCE)
			mask |= (uint64_t)(*ptr++) << 8;
		cl_dzip_newcd.force ^= (int)(mask & 0xff07);
		CL_DZipDiscardMsg((int)(ptr - cl_dzip_inptr));
	}

	CL_DZipCreateClientdataMsg();

#undef CL_DZIP_CPLUS
#undef CL_DZIP_CPLUS_2
}

static void CL_DZipDemxSpawnbaseline(void)
{
	byte buf[32];
	byte *ptr = cl_dzip_inptr + 3;
	cl_dzip_ent_t ent;
	int mask;
	byte bits = 0;
	byte version = *cl_dzip_inptr;
	int16_t index;

	if (version == CL_DZIP_DEM_spawnbaseline2)
	{
		index = CL_DZipGetShort(cl_dzip_inptr + 1);
		mask = *ptr++;
		if (mask & CL_DZIP_SB_MOREBITS)
			mask |= *ptr++ << 8;
		if (mask & CL_DZIP_SB_LARGEMODEL)
			bits |= B_LARGEMODEL;
		if (mask & CL_DZIP_SB_LARGEFRAME)
			bits |= B_LARGEFRAME;
		if (mask & CL_DZIP_SB_ALPHA)
			bits |= B_ALPHA;
	}
	else
	{
		mask = CL_DZipGetShort(cl_dzip_inptr + 1);
		cl_dzip_sble = (cl_dzip_sble + (mask & (CL_DZIP_MAX_ENT_OLD - 1))) % CL_DZIP_MAX_ENT_OLD;
		index = (int16_t)cl_dzip_sble;
		mask >>= 8;
	}

	memset(&ent, 0, sizeof(ent));
	if (bits & B_LARGEMODEL) { ent.modelindex = CL_DZipGetShort(ptr); ptr += 2; } else { ent.modelindex = *ptr++; }
	if (mask & CL_DZIP_SB_FRAME)
	{
		if (bits & B_LARGEFRAME) { ent.frame = CL_DZipGetShort(ptr); ptr += 2; } else { ent.frame = *ptr++; }
	}
	if (mask & CL_DZIP_SB_COLORMAP) ent.colormap = *ptr++;
	if (mask & CL_DZIP_SB_SKIN) ent.skin = *ptr++;
	if (mask & CL_DZIP_SB_ORIGIN)
	{
		ent.org0 = CL_DZipGetShort(ptr); ptr += 2;
		ent.org1 = CL_DZipGetShort(ptr); ptr += 2;
		ent.org2 = CL_DZipGetShort(ptr); ptr += 2;
	}
	if (mask & CL_DZIP_SB_ANGLE1) ent.ang1 = *ptr++;
	if (mask & CL_DZIP_SB_ANGLE0AND2) { ent.ang0 = *ptr++; ent.ang2 = *ptr++; }
	if (bits & B_ALPHA) ent.transparency = *ptr++;

	CL_DZipDiscardMsg((int)(ptr - cl_dzip_inptr));

	if (mask & CL_DZIP_SB_LARGEENTITY)
		version = CL_DZIP_DEM_spawnbaseline;

	ptr = buf;
	*ptr++ = version;
	CL_DZipWriteLE16(ptr, (uint16_t)index); ptr += 2;
	if (version == CL_DZIP_DEM_spawnbaseline2)
		*ptr++ = bits;

	if (bits & B_LARGEMODEL) { CL_DZipWriteLE16(ptr, (uint16_t)ent.modelindex); ptr += 2; } else { *ptr++ = ent.modelindex; }
	if (bits & B_LARGEFRAME) { CL_DZipWriteLE16(ptr, (uint16_t)ent.frame); ptr += 2; } else { *ptr++ = ent.frame; }
	*ptr++ = ent.colormap;
	*ptr++ = ent.skin;
	CL_DZipWriteLE16(ptr, (uint16_t)ent.org0); ptr += 2;
	*ptr++ = ent.ang0;
	CL_DZipWriteLE16(ptr, (uint16_t)ent.org1); ptr += 2;
	*ptr++ = ent.ang1;
	CL_DZipWriteLE16(ptr, (uint16_t)ent.org2); ptr += 2;
	*ptr++ = ent.ang2;
	if (bits & B_ALPHA) *ptr++ = ent.transparency;

	CL_DZipInsertMsg(buf, (unsigned int)(ptr - buf));

	cl_dzip_base[(unsigned short)index] = ent;
	cl_dzip_copybaseline = 1;
}

static qboolean CL_DZipDemxTempEntity(void)
{
	if (cl_dzip_inptr[1] == 17)
	{
		CL_DZipCopyMsg((unsigned int)(strlen((char *)cl_dzip_inptr + 2) + 17));
		return true;
	}

	if (cl_dzip_inptr[1] >= (int)sizeof(cl_dzip_te_size))
		return false;

	CL_DZipCopyMsg(cl_dzip_te_size[cl_dzip_inptr[1]]);
	return true;
}

static void CL_DZipDemxSpawnstatic2(void)
{
	byte bits = cl_dzip_inptr[1];
	int msgsize = 15;

	if (bits & B_LARGEMODEL) ++msgsize;
	if (bits & B_LARGEFRAME) ++msgsize;
	if (bits & B_ALPHA) ++msgsize;

	CL_DZipCopyMsg(msgsize);
}

static qboolean CL_DZipDemCopyUE(void)
{
	unsigned int mask = cl_dzip_inptr[0] & 0x7f;
	int len = 1;

	if (mask & U_MOREBITS)
	{
		mask |= cl_dzip_inptr[len++] << 8;
		if (cl_dzip_protocol == PROTOCOL_NETQUAKE && (mask & U_TRANS))
			return false;
	}
	if (cl_dzip_protocol != PROTOCOL_NETQUAKE && (mask & U_EXTEND1))
		mask |= cl_dzip_inptr[len++] << 16;
	if (mask & U_EXTEND2)
		return false;

	if (mask & U_LONGENTITY) ++len;
	if (mask & U_MODEL) ++len;
	if (mask & U_FRAME) ++len;
	if (mask & U_COLORMAP) ++len;
	if (mask & U_SKIN) ++len;
	if (mask & U_EFFECTS) ++len;
	if (mask & CL_DZIP_U_ORIGIN0) len += 2;
	if (mask & CL_DZIP_U_ANGLE0) ++len;
	if (mask & CL_DZIP_U_ORIGIN1) len += 2;
	if (mask & CL_DZIP_U_ANGLE1) ++len;
	if (mask & CL_DZIP_U_ORIGIN2) len += 2;
	if (mask & CL_DZIP_U_ANGLE2) ++len;
	if (mask & U_ALPHA) ++len;
	if (mask & U_SCALE) ++len;
	if (mask & U_FRAME2) ++len;
	if (mask & U_MODEL2) ++len;
	if (mask & U_LERPFINISH) ++len;

	CL_DZipCopyMsg((unsigned int)(len + 1));
	return true;
}

static qboolean CL_DZipDemxUpdateentity(void)
{
	byte buf[32];
	byte *ptr;
	uint64_t mask;
	int i, entity;
	int baseval = 0, prev;
	cl_dzip_ent_t n, o;

	if (cl_dzip_decode_type == CL_DZIP_TYPE_DEMV1)
	{
		CL_DZipDemv1Updateentity();
		return true;
	}

	cl_dzip_lastent = 0;
	for (ptr = cl_dzip_inptr + 1; *ptr; ++ptr)
	{
		if (*ptr == 0xff)
		{
			baseval += 0xfe;
			continue;
		}

		entity = baseval + *ptr;
		cl_dzip_newent[entity].active = 1;
		while (cl_dzip_entlink[cl_dzip_lastent] <= entity)
			cl_dzip_lastent = cl_dzip_entlink[cl_dzip_lastent];
		if (cl_dzip_lastent < entity)
		{
			cl_dzip_entlink[entity] = cl_dzip_entlink[cl_dzip_lastent];
			cl_dzip_entlink[cl_dzip_lastent] = entity;
		}
	}

	for (prev = 0, i = cl_dzip_entlink[0], ++ptr; i < CL_DZIP_MAX_ENT; i = cl_dzip_entlink[i])
	{
		cl_dzip_newent[i].org0 += cl_dzip_newent[i].od0;
		cl_dzip_newent[i].org1 += cl_dzip_newent[i].od1;
		cl_dzip_newent[i].org2 += cl_dzip_newent[i].od2;

		if (!cl_dzip_newent[i].active)
		{
			prev = i;
			continue;
		}

		mask = *ptr++;
		if (mask == CL_DZIP_UE_MOREBITS_DIFF)
		{
			cl_dzip_oldent[i] = cl_dzip_newent[i] = cl_dzip_base[i];
			cl_dzip_entlink[prev] = cl_dzip_entlink[i];
			continue;
		}

		prev = i;
		if (mask == 0x00)
		{
			cl_dzip_newent[i].active = 0;
			continue;
		}

		if (mask & CL_DZIP_UE_MOREBITS_DIFF) mask |= (uint64_t)(*ptr++) << 8;
		if (mask & CL_DZIP_UE_MOREBITS2_DIFF) mask |= (uint64_t)(*ptr++) << 16;

		n = cl_dzip_newent[i];
		o = cl_dzip_oldent[i];

		if (mask & CL_DZIP_UE_ORIGIN2_DIFF) { n.od2 = CL_DZipBPlus(*ptr++, o.od2); n.org2 = o.org2 + n.od2; }
		if (mask & CL_DZIP_UE_ORIGIN2_MOREBITS_DIFF) { n.org2 = CL_DZipGetShort(ptr); ptr += 2; n.od2 = n.org2 - o.org2; }
		if (mask & CL_DZIP_UE_ORIGIN1_DIFF) { n.od1 = CL_DZipBPlus(*ptr++, o.od1); n.org1 = o.org1 + n.od1; }
		if (mask & CL_DZIP_UE_ORIGIN1_MOREBITS_DIFF) { n.org1 = CL_DZipGetShort(ptr); ptr += 2; n.od1 = n.org1 - o.org1; }
		if (mask & CL_DZIP_UE_ORIGIN0_DIFF) { n.od0 = CL_DZipBPlus(*ptr++, o.od0); n.org0 = o.org0 + n.od0; }
		if (mask & CL_DZIP_UE_ORIGIN0_MOREBITS_DIFF) { n.org0 = CL_DZipGetShort(ptr); ptr += 2; n.od0 = n.org0 - o.org0; }
		if (mask & CL_DZIP_UE_ANGLE0_DIFF) n.ang0 = (byte)CL_DZipBPlus(*ptr++, o.ang0);
		if (mask & CL_DZIP_UE_ANGLE1_DIFF) n.ang1 = (byte)CL_DZipBPlus(*ptr++, o.ang1);
		if (mask & CL_DZIP_UE_ANGLE2_DIFF) n.ang2 = (byte)CL_DZipBPlus(*ptr++, o.ang2);
		if (mask & CL_DZIP_UE_FRAME_SINGLE_DIFF) n.frame = o.frame + 1;
		if (mask & CL_DZIP_UE_FRAME_NORMAL_DIFF)
		{
			if (o.frame & 0xff00) n.frame = CL_DZipBPlus(*ptr++, o.frame);
			else n.frame = (byte)CL_DZipBPlus(*ptr++, o.frame);
		}
		if (mask & CL_DZIP_UE_FRAME2_DIFF) { n.frame = CL_DZipGetShort(ptr); ptr += 2; }
		if (mask & CL_DZIP_UE_EFFECTS_DIFF) n.effects = *ptr++;
		if (mask & CL_DZIP_UE_MODEL_DIFF) { n.modelindex &= 0xff00; n.modelindex |= *ptr++; }
		if (mask & CL_DZIP_UE_MODEL2_DIFF) { n.modelindex &= 0x00ff; n.modelindex |= (*ptr++ << 8); }
		if (mask & CL_DZIP_UE_NOLERP_DIFF) n.newbit = !o.newbit;
		if (mask & CL_DZIP_UE_COLORMAP_DIFF) n.colormap = *ptr++;
		if (mask & CL_DZIP_UE_SKIN_DIFF) n.skin = *ptr++;
		if (mask & CL_DZIP_UE_NEHAHRA_ALPHA_DIFF && cl_dzip_protocol == PROTOCOL_NETQUAKE) { n.alpha = CL_DZipGetFloat(ptr); ptr += 4; }
		if (mask & CL_DZIP_UE_NEHAHRA_FULLBRIGHT_DIFF && cl_dzip_protocol == PROTOCOL_NETQUAKE) n.fullbright = *ptr++;
		if (mask & CL_DZIP_UE_ALPHA_DIFF && cl_dzip_protocol != PROTOCOL_NETQUAKE) n.transparency = *ptr++;
		if (mask & CL_DZIP_UE_SCALE_DIFF && cl_dzip_protocol != PROTOCOL_NETQUAKE) n.scale = *ptr++;
		if (mask & CL_DZIP_UE_LERPFINISH_DIFF) n.lerpfinish = *ptr++;

		cl_dzip_newent[i] = n;
	}

	if (*ptr == CL_DZIP_IDENTIFIER_UPDATEENTITY_FORCE)
	{
		++ptr;
		while ((mask = CL_DZipGetShort(ptr)))
		{
			ptr += 2;
			mask &= 0xffff;
			if (mask & CL_DZIP_UE_MOREBITS_FORCE) mask |= (uint64_t)(*ptr++) << 16;
			if (cl_dzip_protocol != PROTOCOL_NETQUAKE && (mask & CL_DZIP_UE_MOREBITS2_FORCE)) mask |= (uint64_t)(*ptr++) << 24;
			entity = (int)(mask & 0x3ff);
			cl_dzip_newent[entity].force ^= (int)(mask & 0xfffffc00ULL);
		}
		ptr += 2;
	}

	if (*ptr == CL_DZIP_IDENTIFIER_UPDATEENTITY2_FORCE)
	{
		++ptr;
		while ((mask = CL_DZipGetShort(ptr)))
		{
			ptr += 2;
			mask &= 0xffff;
			mask |= (uint64_t)(*ptr++) << 16;
			if (mask & (CL_DZIP_UE_MOREBITS_FORCE << 8)) mask |= (uint64_t)(*ptr++) << 24;
			if (cl_dzip_protocol != PROTOCOL_NETQUAKE && (mask & (CL_DZIP_UE_MOREBITS2_FORCE << 8))) mask |= (uint64_t)(*ptr++) << 32;
			entity = (int)(mask & (CL_DZIP_MAX_ENT - 1));
			cl_dzip_newent[entity].force ^= (int)((mask & ~(uint64_t)(CL_DZIP_MAX_ENT - 1)) >> 8);
		}
		ptr += 2;
	}

	CL_DZipDiscardMsg((int)(ptr - cl_dzip_inptr));

	for (i = cl_dzip_entlink[0]; i < CL_DZIP_MAX_ENT; i = cl_dzip_entlink[i])
	{
		cl_dzip_ent_t b = cl_dzip_base[i];

		n = cl_dzip_newent[i];
		ptr = buf + 3;
		mask = U_SIGNAL;

		if (i > 0xff || (n.force & CL_DZIP_UE_LONGENTITY_FORCE))
		{
			CL_DZipWriteLE16(ptr, (uint16_t)i);
			ptr += 2;
			mask |= U_LONGENTITY;
		}
		else
		{
			*ptr++ = i;
		}

#define CL_DZIP_BDIFF(field, bit, forcebit) \
	if ((n.field) != (b.field) || (n.force & (forcebit))) \
	{ \
		*ptr++ = (n.field); \
		mask |= (bit); \
	}
#define CL_DZIP_BDIFF_DEF(expr, baseexpr, defval, bit, forcebit) \
	if ((((expr) != (baseexpr) && (expr) != (defval))) || (n.force & (forcebit))) \
	{ \
		*ptr++ = (expr); \
		mask |= (bit); \
	}

		CL_DZIP_BDIFF(modelindex & 0x00ff, U_MODEL, CL_DZIP_UE_MODEL_FORCE);
		CL_DZIP_BDIFF(frame & 0x00ff, U_FRAME, CL_DZIP_UE_FRAME_FORCE);
		CL_DZIP_BDIFF(colormap, U_COLORMAP, CL_DZIP_UE_COLORMAP_FORCE);
		CL_DZIP_BDIFF(skin, U_SKIN, CL_DZIP_UE_SKIN_FORCE);
		CL_DZIP_BDIFF(effects, U_EFFECTS, CL_DZIP_UE_EFFECTS_FORCE);
		if (n.org0 != b.org0 || (n.force & CL_DZIP_UE_ORIGIN0_FORCE)) { mask |= CL_DZIP_U_ORIGIN0; CL_DZipWriteLE16(ptr, (uint16_t)n.org0); ptr += 2; }
		CL_DZIP_BDIFF(ang0, CL_DZIP_U_ANGLE0, CL_DZIP_UE_ANGLE0_FORCE);
		if (n.org1 != b.org1 || (n.force & CL_DZIP_UE_ORIGIN1_FORCE)) { mask |= CL_DZIP_U_ORIGIN1; CL_DZipWriteLE16(ptr, (uint16_t)n.org1); ptr += 2; }
		CL_DZIP_BDIFF(ang1, CL_DZIP_U_ANGLE1, CL_DZIP_UE_ANGLE1_FORCE);
		if (n.org2 != b.org2 || (n.force & CL_DZIP_UE_ORIGIN2_FORCE)) { mask |= CL_DZIP_U_ORIGIN2; CL_DZipWriteLE16(ptr, (uint16_t)n.org2); ptr += 2; }
		CL_DZIP_BDIFF(ang2, CL_DZIP_U_ANGLE2, CL_DZIP_UE_ANGLE2_FORCE);
		if (cl_dzip_protocol != PROTOCOL_NETQUAKE) CL_DZIP_BDIFF(transparency, U_ALPHA, CL_DZIP_UE_ALPHA_FORCE);
		CL_DZIP_BDIFF(scale, U_SCALE, CL_DZIP_UE_SCALE_FORCE);
		CL_DZIP_BDIFF_DEF(n.frame >> 8, b.frame >> 8, 0, U_FRAME2, CL_DZIP_UE_FRAME2_FORCE);
		CL_DZIP_BDIFF_DEF(n.modelindex >> 8, b.modelindex >> 8, 0, U_MODEL2, CL_DZIP_UE_MODEL2_FORCE);
		CL_DZIP_BDIFF(lerpfinish, U_LERPFINISH, CL_DZIP_UE_LERPFINISH_FORCE);

		if (n.newbit)
			mask |= CL_DZIP_U_NOLERP;

		if (mask & 0xffff00) mask |= U_MOREBITS;
		if (mask & 0xff0000) mask |= U_EXTEND1;

		buf[0] = (byte)(mask & 0xff);
		buf[1] = (byte)((mask >> 8) & 0xff);
		buf[2] = (byte)((mask >> 16) & 0xff);
		if (!(mask & U_EXTEND1))
		{
			if (!(mask & U_MOREBITS))
			{
				memmove(buf + 1, buf + 3, (size_t)(ptr - (buf + 3)));
				ptr -= 2;
			}
			else
			{
				memmove(buf + 2, buf + 3, (size_t)(ptr - (buf + 3)));
				ptr -= 1;
			}
		}

		CL_DZipInsertMsg(buf, (unsigned int)(ptr - buf));
		cl_dzip_oldent[i] = cl_dzip_newent[i];

#undef CL_DZIP_BDIFF
#undef CL_DZIP_BDIFF_DEF
	}

	return true;
}

static qboolean CL_DZipDemUncompressBlock(void)
{
	int32_t a1;
	int uemask = (cl_dzip_decode_type == CL_DZIP_TYPE_DEMV1) ? 0x80 : CL_DZIP_IDENTIFIER_UPDATEENTITY_DIFF;
	int cdmask = (cl_dzip_decode_type == CL_DZIP_TYPE_DEMV1) ? 0xf0 : CL_DZIP_IDENTIFIER_CLIENTDATA_DIFF;
	byte cfields;

	cfields = *cl_dzip_inptr++;

	if (cfields & 1) { cl_dzip_cam0 += CL_DZipGetLong(cl_dzip_inptr); cl_dzip_inptr += 4; }
	if (cfields & 2) { cl_dzip_cam1 += CL_DZipGetLong(cl_dzip_inptr); cl_dzip_inptr += 4; }
	if (cfields & 4) { cl_dzip_cam2 += CL_DZipGetLong(cl_dzip_inptr); cl_dzip_inptr += 4; }

	cl_dzip_outlen = 0;
	a1 = 0;
	CL_DZipInsertMsg(&a1, 4);
	a1 = LittleLong((uint32_t)cl_dzip_cam0); CL_DZipInsertMsg(&a1, 4);
	a1 = LittleLong((uint32_t)cl_dzip_cam1); CL_DZipInsertMsg(&a1, 4);
	a1 = LittleLong((uint32_t)cl_dzip_cam2); CL_DZipInsertMsg(&a1, 4);

	cl_dzip_dem_updateframe = 0;
	while (*cl_dzip_inptr)
	{
		if ((*cl_dzip_inptr & 0xf8) == uemask)
		{
			if (!CL_DZipDemxUpdateentity())
				return false;
		}
		else
		{
			if (cl_dzip_decode_type == CL_DZIP_TYPE_DEMV1 && cl_dzip_dem_updateframe)
			{
				CL_DZipDemv1Dxentities();
				cl_dzip_dem_updateframe = 0;
			}

			if ((*cl_dzip_inptr & 0xf0) == cdmask)
			{
				CL_DZipDemxClientdata();
			}
			else if ((*cl_dzip_inptr & 0xf8) == CL_DZIP_IDENTIFIER_SOUND || (*cl_dzip_inptr & 0xe0) == CL_DZIP_IDENTIFIER_SOUND_MOREBITS)
			{
				CL_DZipDemxSound();
			}
			else if (*cl_dzip_inptr >= 0x80)
			{
				if (!CL_DZipDemCopyUE())
					return false;
			}
			else switch (*cl_dzip_inptr)
			{
			case CL_DZIP_DEM_nop: CL_DZipDemxNop(); break;
			case CL_DZIP_DEM_disconnect: CL_DZipDemxDisconnect(); break;
			case CL_DZIP_DEM_updatestat: CL_DZipDemxUpdatestat(); break;
			case CL_DZIP_DEM_version: CL_DZipDemxVersion(); break;
			case CL_DZIP_DEM_setview: CL_DZipDemxSetview(); break;
			case CL_DZIP_DEM_time: CL_DZipDemxTime(); break;
			case CL_DZIP_DEM_print:
			case CL_DZIP_DEM_stufftext:
			case CL_DZIP_DEM_centerprint:
			case CL_DZIP_DEM_finale:
			case CL_DZIP_DEM_cutscene:
				CL_DZipDemxString();
				break;
			case CL_DZIP_DEM_setangle: CL_DZipDemxSetangle(); break;
			case CL_DZIP_DEM_serverinfo:
				if (!CL_DZipDemxServerinfo())
					return false;
				break;
			case CL_DZIP_DEM_lightstyle: CL_DZipDemxLightstyle(); break;
			case CL_DZIP_DEM_updatename: CL_DZipDemxUpdatename(); break;
			case CL_DZIP_DEM_updatefrags: CL_DZipDemxUpdatefrags(); break;
			case CL_DZIP_DEM_stopsound: CL_DZipDemxStopsound(); break;
			case CL_DZIP_DEM_updatecolors: CL_DZipDemxUpdatecolors(); break;
			case CL_DZIP_DEM_particle: CL_DZipDemxParticle(); break;
			case CL_DZIP_DEM_damage: CL_DZipDemxDamage(); break;
			case CL_DZIP_DEM_spawnstatic: CL_DZipDemxSpawnstatic(); break;
			case CL_DZIP_DEM_spawnbinary: CL_DZipDemxSpawnbinary(); break;
			case CL_DZIP_DEM_spawnbaseline:
			case CL_DZIP_DEM_spawnbaseline2:
				CL_DZipDemxSpawnbaseline();
				break;
			case CL_DZIP_DEM_temp_entity:
				if (!CL_DZipDemxTempEntity())
					return false;
				break;
			case CL_DZIP_DEM_setpause: CL_DZipDemxSetpause(); break;
			case CL_DZIP_DEM_signonnum: CL_DZipDemxSignonnum(); break;
			case CL_DZIP_DEM_killedmonster: CL_DZipDemxKilledmonster(); break;
			case CL_DZIP_DEM_foundsecret: CL_DZipDemxFoundsecret(); break;
			case CL_DZIP_DEM_spawnstaticsound: CL_DZipDemxSpawnstaticsound(); break;
			case CL_DZIP_DEM_intermission: CL_DZipDemxIntermission(); break;
			case CL_DZIP_DEM_cdtrack: CL_DZipDemxCdtrack(); break;
			case CL_DZIP_DEM_sellscreen: CL_DZipDemxSellscreen(); break;
			case CL_DZIP_DZ_longtime: CL_DZipDemxLongtime(); break;
			case CL_DZIP_DEM_fog: CL_DZipDemxFog(); break;
			case CL_DZIP_DEM_spawnstatic2: CL_DZipDemxSpawnstatic2(); break;
			case CL_DZIP_DEM_spawnstaticsound2: CL_DZipDemxSpawnstaticsound2(); break;
			default:
				return false;
			}
		}
	}

	if (cl_dzip_decode_type == CL_DZIP_TYPE_DEMV1 && cl_dzip_dem_updateframe)
		CL_DZipDemv1Dxentities();

	cl_dzip_outlen -= 16;
	a1 = LittleLong((uint32_t)cl_dzip_outlen);
	memcpy(cl_dzip_outblk, &a1, 4);
	if (!CL_DZipOutputWrite(cl_dzip_outblk, (unsigned int)cl_dzip_outlen + 16))
		return false;

	if (cl_dzip_copybaseline)
	{
		cl_dzip_copybaseline = 0;
		memcpy(cl_dzip_oldent, cl_dzip_base, sizeof(cl_dzip_base));
		memcpy(cl_dzip_newent, cl_dzip_base, sizeof(cl_dzip_base));
	}

	return true;
}

static void CL_DZipDemUncompressInit(int type)
{
	cl_dzip_decode_type = -type;
	memset(cl_dzip_base, 0, sizeof(cl_dzip_base));
	memset(cl_dzip_oldent, 0, sizeof(cl_dzip_oldent));
	memset(cl_dzip_newent, 0, sizeof(cl_dzip_newent));
	memset(cl_dzip_entlink, 0, sizeof(cl_dzip_entlink));
	memset(&cl_dzip_oldcd, 0, sizeof(cl_dzip_oldcd));
	cl_dzip_oldcd.voz = 22;
	cl_dzip_oldcd.items = 0x4001;
	cl_dzip_entlink[0] = CL_DZIP_MAX_ENT;
	cl_dzip_cam0 = cl_dzip_cam1 = cl_dzip_cam2 = 0;
	cl_dzip_copybaseline = 0;
	cl_dzip_dem_gametime = 0;
	cl_dzip_maxent = 0;
	cl_dzip_sble = 0;
	cl_dzip_protocol = PROTOCOL_NETQUAKE;
	cl_dzip_protocolflags = 0;
}

static unsigned int CL_DZipDemUncompress(unsigned int maxsize)
{
	unsigned int blocksize = 0;

	cl_dzip_inptr = cl_dzip_inblk;
	if (cl_dzip_decode_type < 0)
	{
		cl_dzip_decode_type = -cl_dzip_decode_type;
		while (blocksize < 12 && cl_dzip_inblk[blocksize] != '\n')
			++blocksize;

		if (blocksize == 12)
			return 0;

		if (!CL_DZipOutputWrite(cl_dzip_inblk, ++blocksize))
			return 0;
		cl_dzip_inptr += blocksize;
	}

	while (blocksize < 16000 && blocksize < maxsize)
	{
		if (*cl_dzip_inptr == 0xff)
		{
			unsigned int len = (unsigned int)CL_DZipGetLong(cl_dzip_inptr + 1);
			if (CL_DZIP_P_BLOCKSIZE - blocksize - 5 < len)
				return blocksize;
			if (!CL_DZipOutputWrite(cl_dzip_inptr + 5, len))
				return 0;
			blocksize = (unsigned int)(cl_dzip_inptr - cl_dzip_inblk) + len + 5;
		}
		else
		{
			if (!CL_DZipDemUncompressBlock())
				return 0;
			blocksize = (unsigned int)(cl_dzip_inptr - cl_dzip_inblk + 1);
		}

		if (!blocksize)
			return 0;
		++cl_dzip_inptr;
	}

	return blocksize;
}

static qboolean CL_DZipExtractStoredFile(const cl_dzip_direntry_t *de)
{
	byte buffer[65536];
	unsigned int remaining = de->size;

	if (!CL_DZipArchiveSeek(de->ptr))
		return false;

	while (remaining)
	{
		unsigned int chunk = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
		if (!CL_DZipArchiveRead(buffer, chunk))
			return false;
		if (!CL_DZipOutputWrite(buffer, chunk))
			return false;
		remaining -= chunk;
	}

	return true;
}

static qboolean CL_DZipExtractNormalFile(const cl_dzip_direntry_t *de)
{
	byte inbuf[65536];
	byte outbuf[65536];
	z_stream zs;
	unsigned int remaining = de->size;
	int ret = Z_OK;

	memset(&zs, 0, sizeof(zs));
	if (inflateInit(&zs) != Z_OK)
		return false;

	if (!CL_DZipArchiveSeek(de->ptr))
	{
		inflateEnd(&zs);
		return false;
	}

	do
	{
		if (zs.avail_in == 0 && remaining)
		{
			unsigned int chunk = remaining > sizeof(inbuf) ? sizeof(inbuf) : remaining;
			if (!CL_DZipArchiveRead(inbuf, chunk))
			{
				inflateEnd(&zs);
				return false;
			}
			zs.next_in = inbuf;
			zs.avail_in = chunk;
			remaining -= chunk;
		}

		zs.next_out = outbuf;
		zs.avail_out = sizeof(outbuf);
		ret = inflate(&zs, Z_NO_FLUSH);
		if (ret != Z_OK && ret != Z_STREAM_END)
		{
			inflateEnd(&zs);
			return false;
		}

		if (sizeof(outbuf) - zs.avail_out)
		{
			if (!CL_DZipOutputWrite(outbuf, (unsigned int)(sizeof(outbuf) - zs.avail_out)))
			{
				inflateEnd(&zs);
				return false;
			}
		}
	} while (ret != Z_STREAM_END);

	inflateEnd(&zs);
	return true;
}

static qboolean CL_DZipExtractDemoEntry(const cl_dzip_direntry_t *de)
{
	unsigned int readptr = 0;
	unsigned int inlen = 0;

	if (!CL_DZipArchiveSeek(de->ptr))
		return false;

	memset(&cl_dzip_zs, 0, sizeof(cl_dzip_zs));
	if (inflateInit(&cl_dzip_zs) != Z_OK)
		return false;

	cl_dzip_ztotal = de->size;
	cl_dzip_zs.avail_in = 0;
	CL_DZipDemUncompressInit((int)de->type);

	while (readptr < de->inter)
	{
		unsigned int blocksize;

		if (!CL_DZipZRead(inlen))
		{
			inflateEnd(&cl_dzip_zs);
			return false;
		}

		blocksize = CL_DZipDemUncompress(de->inter - readptr);
		if (!blocksize)
		{
			inflateEnd(&cl_dzip_zs);
			return false;
		}

		if (blocksize != CL_DZIP_P_BLOCKSIZE)
			memmove(cl_dzip_inblk, cl_dzip_inblk + blocksize, CL_DZIP_P_BLOCKSIZE - blocksize);

		readptr += blocksize;
		inlen = CL_DZIP_P_BLOCKSIZE - blocksize;
	}

	inflateEnd(&cl_dzip_zs);
	return true;
}

static qboolean CL_DZipExtractLegacyEntry(const cl_dzip_direntry_t *target_de)
{
	FILE *target_output = cl_dzip_output_file;
	unsigned int readptr = 12;
	unsigned int inlen = 0;
	unsigned int i;
	qboolean extracted = false;

	if (!CL_DZipArchiveSeek(12))
		return false;

	memset(&cl_dzip_zs, 0, sizeof(cl_dzip_zs));
	if (inflateInit(&cl_dzip_zs) != Z_OK)
		return false;

	cl_dzip_ztotal = cl_dzip_directory_offset - 12;
	cl_dzip_totalsize = 12;
	cl_dzip_zs.avail_in = 0;

	for (i = 0; i < (unsigned int)cl_dzip_numfiles; ++i)
	{
		const cl_dzip_direntry_t *de = &cl_dzip_directory[i];
		unsigned int eofptr = de->ptr + de->size;
		qboolean demomode;
		qboolean want_output = (de == target_de);

		if (de->type == CL_DZIP_TYPE_DEMV1)
			demomode = true;
		else if (de->type == CL_DZIP_TYPE_NORMAL || de->type == CL_DZIP_TYPE_TXT)
			demomode = false;
		else
		{
			inflateEnd(&cl_dzip_zs);
			cl_dzip_output_file = target_output;
			cl_dzip_crc_enabled = true;
			return false;
		}

		cl_dzip_output_file = want_output ? target_output : NULL;
		cl_dzip_crc_enabled = want_output;
		if (want_output)
			cl_dzip_crcval = CL_DZIP_INITCRC;

		if (demomode)
			CL_DZipDemUncompressInit((int)de->type);

		while (readptr < eofptr)
		{
			unsigned int blocksize;

			if (!CL_DZipZRead(inlen))
			{
				inflateEnd(&cl_dzip_zs);
				cl_dzip_output_file = target_output;
				cl_dzip_crc_enabled = true;
				return false;
			}

			if (demomode)
			{
				blocksize = CL_DZipDemUncompress(eofptr - readptr);
				if (!blocksize)
				{
					inflateEnd(&cl_dzip_zs);
					cl_dzip_output_file = target_output;
					cl_dzip_crc_enabled = true;
					return false;
				}
			}
			else
			{
				blocksize = cl_dzip_totalsize - readptr;
				if (cl_dzip_totalsize >= eofptr)
					blocksize = eofptr - readptr;
				if (want_output && !CL_DZipOutputWrite(cl_dzip_inblk, blocksize))
				{
					inflateEnd(&cl_dzip_zs);
					cl_dzip_output_file = target_output;
					cl_dzip_crc_enabled = true;
					return false;
				}
			}

			if (blocksize != CL_DZIP_P_BLOCKSIZE)
				memmove(cl_dzip_inblk, cl_dzip_inblk + blocksize, CL_DZIP_P_BLOCKSIZE - blocksize);

			readptr += blocksize;
			inlen = CL_DZIP_P_BLOCKSIZE - blocksize;
		}

		if (want_output)
		{
			extracted = true;
			break;
		}
	}

	inflateEnd(&cl_dzip_zs);
	cl_dzip_output_file = target_output;
	cl_dzip_crc_enabled = true;
	return extracted;
}

static qboolean CL_DZipExtractOpenedDemoArchiveToFile(const char *archive_path, FILE *archive, long archive_base, qofs_t archive_size, FILE *out, qofs_t *out_size, char *out_entry_name, size_t out_entry_name_size)
{
	cl_dzip_direntry_t *de;
	qboolean ok = false;

	if (!archive)
		return false;
	if (!out)
	{
		fclose(archive);
		return false;
	}

	if (out_size)
		*out_size = -1;
	if (out_entry_name && out_entry_name_size)
		out_entry_name[0] = '\0';

	if (archive_base < 0 || archive_size <= 0 || archive_size > UINT_MAX)
		goto done;

	cl_dzip_archive_file = archive;
	cl_dzip_archive_base = archive_base;
	cl_dzip_archive_size = archive_size;
	cl_dzip_output_file = out;
	cl_dzip_crc_enabled = true;
	CL_DZipCRCInit();

	CL_DZipFreeDirectory();
	if (!CL_DZipReadDirectory(archive_path))
		goto done;

	de = CL_DZipFindDemoEntry(archive_path);
	if (!de)
	{
		Con_Printf("DZip: %s does not contain a supported demo entry\n", archive_path);
		goto done;
	}

	cl_dzip_crcval = CL_DZIP_INITCRC;
	if ((cl_dzip_maj_ver == 1 || de->type == CL_DZIP_TYPE_DEM || de->type == CL_DZIP_TYPE_DEMV1) && !CL_DZipAllocDecodeBuffers())
	{
		Con_Printf("DZip: out of memory extracting %s\n", archive_path);
		goto done;
	}

	if (cl_dzip_maj_ver == 1)
	{
		ok = CL_DZipExtractLegacyEntry(de);
	}
	else switch (de->type)
	{
		case CL_DZIP_TYPE_STORE:
			ok = CL_DZipExtractStoredFile(de);
			break;
		case CL_DZIP_TYPE_NORMAL:
			ok = CL_DZipExtractNormalFile(de);
			break;
		case CL_DZIP_TYPE_DEM:
			ok = CL_DZipExtractDemoEntry(de);
			break;
		default:
			ok = false;
			break;
	}

	if (!ok)
	{
		Con_Printf("DZip: failed to extract %s from %s\n", de->name, archive_path);
		goto done;
	}

	if (cl_dzip_crcval != de->crc)
	{
		Con_Printf("DZip: warning: CRC mismatch extracting %s from %s\n", de->name, archive_path);
	}

	if (fflush(out) != 0)
		goto done;
	if (fseek(out, 0, SEEK_END) != 0)
		goto done;

	if (out_size)
		*out_size = ftell(out);

	rewind(out);

	if (out_entry_name && out_entry_name_size)
		q_strlcpy(out_entry_name, COM_SkipPath(de->name), out_entry_name_size);
	ok = true;

done:
	CL_DZipFreeDirectory();
	CL_DZipFreeDecodeBuffers();
	if (archive)
		fclose(archive);
	cl_dzip_archive_file = NULL;
	cl_dzip_output_file = NULL;
	cl_dzip_archive_base = 0;
	cl_dzip_archive_size = 0;
	cl_dzip_crc_enabled = false;
	return ok;
}

static qboolean CL_DZipExtractDemoArchiveToFile(const char *archive_path, FILE *out, qofs_t *out_size, char *out_entry_name, size_t out_entry_name_size)
{
	FILE *archive = NULL;
	long archive_base;

	if (COM_FOpenFile(archive_path, &archive, NULL) < 0 || !archive)
		return false;

	archive_base = ftell(archive);
	if (archive_base < 0)
	{
		fclose(archive);
		return false;
	}

	return CL_DZipExtractOpenedDemoArchiveToFile(archive_path, archive, archive_base,
		com_filesize, out, out_size, out_entry_name, out_entry_name_size);
}

static qboolean CL_DZipExtractDemoArchiveOSPathToFile(const char *archive_path, FILE *out, qofs_t *out_size, char *out_entry_name, size_t out_entry_name_size)
{
	FILE *archive;
	long archive_size;

	if (!archive_path || !archive_path[0])
		return false;

	archive = fopen(archive_path, "rb");
	if (!archive)
		return false;

	if (fseek(archive, 0, SEEK_END) != 0)
	{
		fclose(archive);
		return false;
	}
	archive_size = ftell(archive);
	if (archive_size < 0)
	{
		fclose(archive);
		return false;
	}

	return CL_DZipExtractOpenedDemoArchiveToFile(archive_path, archive, 0,
		(qofs_t)archive_size, out, out_size, out_entry_name, out_entry_name_size);
}

static qboolean CL_DZipOpenDemoArchive(const char *archive_path, FILE **out_demo, qofs_t *out_size, char *out_entry_name, size_t out_entry_name_size)
{
	FILE *out = NULL;
	char temp_path[MAX_OSPATH];
	qboolean ok = false;

	*out_demo = NULL;
	if (out_size)
		*out_size = -1;
	if (out_entry_name && out_entry_name_size)
		out_entry_name[0] = '\0';

	CL_DZipCleanupTempDemo();
	CL_DZipMakePlaybackTempPath(temp_path, sizeof(temp_path));
	out = fopen(temp_path, "wb+");
	if (!out)
	{
		Con_Printf("DZip: could not create temp demo %s\n", temp_path);
		goto done;
	}

	if (CL_DZipExtractDemoArchiveToFile(archive_path, out, out_size, out_entry_name, out_entry_name_size))
	{
		q_strlcpy(cl_dzip_playback_temp_path, temp_path, sizeof(cl_dzip_playback_temp_path));
		*out_demo = out;
		out = NULL;
		ok = true;
	}

done:
	if (out)
	{
		fclose(out);
		remove(temp_path);
	}
	return ok;
}

static byte *CL_DZipLoadDemoBufferEx(const char *archive_path, int *length_out, qboolean os_path)
{
	FILE *out;
	byte *data = NULL;
	qofs_t size = -1;
	qboolean extracted;

	if (length_out)
		*length_out = -1;

	out = tmpfile();
	if (!out)
		return NULL;

	if (os_path)
		extracted = CL_DZipExtractDemoArchiveOSPathToFile(archive_path, out, &size, NULL, 0);
	else
		extracted = CL_DZipExtractDemoArchiveToFile(archive_path, out, &size, NULL, 0);

	if (!extracted)
	{
		fclose(out);
		return NULL;
	}

	if (size <= 0 || size > INT_MAX)
	{
		fclose(out);
		return NULL;
	}

	data = (byte *)malloc((size_t)size + 1);
	if (!data)
	{
		fclose(out);
		return NULL;
	}

	if (fread(data, 1, (size_t)size, out) != (size_t)size)
	{
		fclose(out);
		free(data);
		return NULL;
	}

	data[size] = '\0';
	fclose(out);

	if (length_out)
		*length_out = (int)size;
	return data;
}

static byte *CL_DZipLoadDemoBuffer(const char *archive_path, int *length_out)
{
	return CL_DZipLoadDemoBufferEx(archive_path, length_out, false);
}

static byte *CL_DZipLoadDemoBufferOSPath(const char *archive_path, int *length_out)
{
	return CL_DZipLoadDemoBufferEx(archive_path, length_out, true);
}

static qboolean CL_DZipArchiveDemoFile(const char *src_dem_path, const char *archive_path, const char *entry_name)
{
	byte header[12];
	byte dirbuf[CL_DZIP_DIR_DISK_SIZE];
	byte inbuf[65536];
	byte outbuf[65536];
	unsigned long crc_table[256];
	unsigned long crc_value = CL_DZIP_INITCRC;
	FILE *in = NULL;
	FILE *out = NULL;
	z_stream zs;
	long real_size;
	uLong compressed_size;
	int ret;
	size_t entry_len;
	qboolean ok = false;

	entry_len = strlen(entry_name) + 1;
	if (entry_len == 0 || entry_len > MAX_OSPATH)
		return false;

	in = fopen(src_dem_path, "rb");
	if (!in)
		return false;

	if (fseek(in, 0, SEEK_END) != 0)
		goto done;
	real_size = ftell(in);
	if (real_size < 0 || (unsigned long)real_size > UINT32_MAX ||
		fseek(in, 0, SEEK_SET) != 0)
		goto done;

	out = fopen(archive_path, "wb");
	if (!out)
		goto done;

	CL_DZipCRCBuildTable(crc_table);

	CL_DZipWriteLE32(header + 0, 'D' + ('Z' << 8) + (CL_DZIP_MAJOR_VERSION << 16) + (CL_DZIP_MINOR_VERSION << 24));
	CL_DZipWriteLE32(header + 4, 0);
	CL_DZipWriteLE32(header + 8, 1);
	if (fwrite(header, 1, sizeof(header), out) != sizeof(header))
		goto done;

	memset(&zs, 0, sizeof(zs));
	if (deflateInit(&zs, Z_DEFAULT_COMPRESSION) != Z_OK)
		goto done;

	do
	{
		size_t read = fread(inbuf, 1, sizeof(inbuf), in);
		if (read)
		{
			CL_DZipUpdateCRC(crc_table, &crc_value, inbuf, (int)read);
			zs.next_in = inbuf;
			zs.avail_in = (uInt)read;
			while (zs.avail_in)
			{
				zs.next_out = outbuf;
				zs.avail_out = sizeof(outbuf);
				if (deflate(&zs, Z_NO_FLUSH) != Z_OK)
				{
					deflateEnd(&zs);
					goto done;
				}
				if (sizeof(outbuf) - zs.avail_out)
				{
					if (fwrite(outbuf, 1, sizeof(outbuf) - zs.avail_out, out) != sizeof(outbuf) - zs.avail_out)
					{
						deflateEnd(&zs);
						goto done;
					}
				}
			}
		}

		if (ferror(in))
		{
			deflateEnd(&zs);
			goto done;
		}

		if (feof(in))
			break;
	} while (1);

	do
	{
		zs.next_out = outbuf;
		zs.avail_out = sizeof(outbuf);
		ret = deflate(&zs, Z_FINISH);
		if (ret != Z_OK && ret != Z_STREAM_END)
		{
			deflateEnd(&zs);
			goto done;
		}
		if (sizeof(outbuf) - zs.avail_out)
		{
			if (fwrite(outbuf, 1, sizeof(outbuf) - zs.avail_out, out) != sizeof(outbuf) - zs.avail_out)
			{
				deflateEnd(&zs);
				goto done;
			}
		}
	} while (ret != Z_STREAM_END);

	compressed_size = zs.total_out;
	deflateEnd(&zs);
	if (compressed_size > UINT32_MAX - 12u)
		goto done;

	CL_DZipWriteLE32(dirbuf + 0, 12);
	CL_DZipWriteLE32(dirbuf + 4, (uint32_t)compressed_size);
	CL_DZipWriteLE32(dirbuf + 8, (uint32_t)real_size);
	CL_DZipWriteLE16(dirbuf + 12, (uint16_t)entry_len);
	CL_DZipWriteLE16(dirbuf + 14, 0);
	CL_DZipWriteLE32(dirbuf + 16, (uint32_t)crc_value);
	CL_DZipWriteLE32(dirbuf + 20, CL_DZIP_TYPE_NORMAL);
	CL_DZipWriteLE32(dirbuf + 24, 0);
	CL_DZipWriteLE32(dirbuf + 28, (uint32_t)real_size);

	if (fwrite(dirbuf, 1, sizeof(dirbuf), out) != sizeof(dirbuf))
		goto done;
	if (fwrite(entry_name, 1, entry_len, out) != entry_len)
		goto done;

	CL_DZipWriteLE32(header + 4, (uint32_t)(12 + compressed_size));
	if (fseek(out, 0, SEEK_SET) != 0)
		goto done;
	if (fwrite(header, 1, sizeof(header), out) != sizeof(header))
		goto done;

	ok = true;

done:
	if (out)
	{
		if (fflush(out) != 0)
			ok = false;
		if (fclose(out) != 0)
			ok = false;
	}
	if (in)
		fclose(in);
	if (!ok)
		remove(archive_path);
	return ok;
}

#endif
