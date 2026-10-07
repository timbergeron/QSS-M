/*
Copyright (C) 2026 QSS-M contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/

/*
netgraph.h -- network and frame diagnostics  // woods #netgraph

Two parts: the collector core's types and API (pure C, usable without the
engine; Misc/netdiag_test builds it with NETGRAPH_STANDALONE), then the hook
points engine code calls. Design notes: network-graph.plan.md.
*/

#ifndef NETGRAPH_H
#define NETGRAPH_H

#include <stdio.h>
#include <stdint.h>

/*
=============================================================================
Collector core
=============================================================================
*/

#define ND_BUCKET_SEC		0.02
#define ND_RING				1024	// 20.48 s of live history
#define ND_WINDOW_BUCKETS	250		// 5 s summary window
#define ND_RATE_BUCKETS		50		// 1 s rate window
#define ND_HITCH_PRE		300		// 6 s before the trigger
#define ND_HITCH_POST		200		// 4 s after
#define ND_HITCH_BUCKETS	(ND_HITCH_PRE + ND_HITCH_POST)
#define ND_HITCH_SLOTS		4
#define ND_HIST_BINS		1002	// 1 ms bins 0..1000, then overflow
#define ND_SVC_TYPES		129		// 0..127 svc ids, 128 = fast entity update
#define ND_SVC_FAST			128
#define ND_TOP_SVC			3
#define ND_CMD_RING			2048	// indexed by sequence: ~2 s of moves even at 1000 fps

enum { ND_IN, ND_OUT, ND_DIRS };
enum { ND_UNREL, ND_REL, ND_ACK, ND_OTHER, ND_CLASSES };

// what the connection is; set by the glue on every frame
typedef enum
{
	ND_SRC_NONE,		// not connected
	ND_SRC_NET,			// datagram transport: full data
	ND_SRC_OTHER,		// ICE or another transport: frames + messages only
	ND_SRC_LOCAL,		// loopback (single player, listen-server host)
	ND_SRC_DEMO
} nd_source_t;

// per-frame conditions that suspend counting/detection
#define ND_COND_LOADING		1
#define ND_COND_PAUSED		2
#define ND_COND_UNFOCUSED	4

// bucket flags
#define ND_BF_DISCONT		0x0001	// time jump, clock reversal, segment start
#define ND_BF_LOADING		0x0002
#define ND_BF_PAUSED		0x0004
#define ND_BF_UNFOCUSED		0x0008
#define ND_BF_MARK			0x0010
#define ND_BF_GAPRESET		0x0020	// implausible sequence jump, excluded from loss
#define ND_BF_WRITEFAIL		0x0040
#define ND_BF_HITCH			0x0080	// hitch trigger bucket
#define ND_BF_UNMEASURED	0x0100	// recording was off: no data, not zero; excluded from coverage and rates
#define ND_BF_SUSPEND		(ND_BF_LOADING | ND_BF_PAUSED | ND_BF_UNFOCUSED | ND_BF_DISCONT | ND_BF_UNMEASURED)

typedef struct nd_bucket_s
{
	uint32_t	index;					// absolute bucket number; detects stale ring slots
	uint16_t	flags;
	uint16_t	maxpkt[ND_DIRS];
	uint32_t	bytes[ND_DIRS][ND_CLASSES];
	uint16_t	pkts[ND_DIRS][ND_CLASSES];

	uint16_t	frames;
	uint16_t	starved_frames;
	float		frame_sum_ms;
	float		frame_max_ms;
	float		starved_ms;

	uint16_t	arrivals;
	uint16_t	lates;
	float		gap_sum_ms, gap_sumsq, gap_max_ms;
	float		late_sum_ms, late_sumsq, late_max_ms;	// raw lateness (includes the client's read delay); exported as raw_*
	float		late_net_sum_ms, late_net_sumsq;	// LATE: lateness beyond the client's read delay, clamped at 0
	float		late_net_max_ms;		// largest lateness beyond the client's own read delay (signed)
	float		svgap_sum_ms;			// server time between received snapshots (the update rate is lates / this)
	uint32_t	svc_total;				// all message bytes parsed in this bucket (top_svc keeps only three)

	uint16_t	accepted;				// accepted unique unreliable datagrams
	uint16_t	missing;				// sequence positions skipped
	uint16_t	gaps;					// gap events
	uint16_t	dup, stale, shortp;
	uint16_t	resent;
	uint16_t	backlog_max;

	uint8_t		top_svc[ND_TOP_SVC];
	uint16_t	top_svc_bytes[ND_TOP_SVC];

	float		ping_ms;				// last server report in this bucket, <0 none
	float		cmdrtt_ms;				// last command ack sample in this bucket, <0 none
} nd_bucket_t;

typedef enum
{
	ND_HITCH_NONE,
	ND_HITCH_FRAME,
	ND_HITCH_LATE,
	ND_HITCH_BOTH,
	ND_HITCH_LOSS,
	ND_HITCH_STARVED,
	ND_HITCH_MARK
} nd_hitch_kind_t;

typedef struct nd_hitch_meta_s
{
	int			used;
	int			kind;
	uint32_t	trigger;			// absolute bucket index of the trigger
	double		session_time;		// seconds into the recording
	float		severity;
	float		frame_ms, late_ms, starved_ms, normal_frame_ms;	// late_ms < 0: no lateness samples
	int			missing;
	char		sentence[160];
} nd_hitch_meta_t;

typedef struct nd_hitch_s
{
	nd_hitch_meta_t	m;
	nd_bucket_t		window[ND_HITCH_BUCKETS];	// window[ND_HITCH_PRE] is the trigger bucket
} nd_hitch_t;

typedef struct nd_session_s
{
	double		rec_time;			// seconds recorded: wall time while recording a connection, gaps excluded
	uint32_t	frames;
	uint32_t	frames_over_50;
	uint32_t	frames_over_100;
	double		starved_ms;
	uint64_t	bytes[ND_DIRS][ND_CLASSES];
	uint64_t	pkts[ND_DIRS][ND_CLASSES];
	uint32_t	maxpkt[ND_DIRS];
	uint32_t	gaps, missing, accepted, resets;
	uint32_t	dup, stale, shortp, resent, writefail;
	uint32_t	backlog_peak;
	uint32_t	msg_peak;
	double		late_sum, late_sumsq;	// raw signed lateness (includes client read delay)
	double		lnet_sum, lnet_sumsq;	// LATE: beyond read delay, clamped at 0; JITTER and thresholds use this
	uint32_t	lates;
	uint32_t	ping_reports;
	double		ping_sum, ping_sumsq, ping_min, ping_max;
	uint32_t	cmdrtt_samples;
	double		cmdrtt_sum;
	uint32_t	frame_hist[ND_HIST_BINS];
	uint32_t	late_hist[ND_HIST_BINS];	// raw lateness; <=0 lands in bin 0
	uint32_t	lnet_hist[ND_HIST_BINS];	// LATE, the headline lateness
	uint64_t	svc_bytes[ND_SVC_TYPES];
	uint32_t	svc_count[ND_SVC_TYPES];
	uint32_t	hitches_detected;
} nd_session_t;

typedef struct nd_cmd_s
{
	int			seq;
	double		time;
} nd_cmd_t;

typedef struct netdiag_s
{
	// time base
	int		started;
	double		t0;					// time of bucket 0
	double		last_now;
	uint32_t	cur;				// absolute index of the bucket being filled
	uint32_t	first;				// first bucket with coverage this session
	nd_bucket_t	ring[ND_RING];

	// connection-scoped running state
	int			source;
	int			cond;				// ND_COND_* for the current frame
	double		last_frame_time;	// <0 = no baseline
	int		frame_starved;
	double		last_arrival;		// <0 = no baseline
	double		last_sv_read, last_sv_time;	// <0 = no baseline
	double		cur_read_time;		// read time of the message being parsed, <0 none
	double		cur_read_delay;		// how long that datagram may have waited for our read pass
	double		last_update_time;	// last accepted snapshot datagram, <0 none (chat and other reliables don't count)
	uint32_t	unmeasured_until;	// buckets before this index were not recorded
	double		pass_time, pass_prev;	// start of this and the previous socket read pass, <0 none
	float		last_ping_ms;
	double		last_ping_time;		// <0 = never
	int			last_moveloss;		// server-reported movement loss %, <0 = not reported
	float		last_cmdrtt_ms;
	double		last_cmdrtt_time;
	nd_cmd_t	cmds[ND_CMD_RING];	// cmds[seq & (ND_CMD_RING - 1)]
	int			last_sent_seq;
	int			last_acked;
	int			svc_cur;			// svc id being attributed, <0 none
	int			svc_pos;
	uint32_t	svc_accum[ND_SVC_TYPES];	// current bucket
	uint8_t		svc_touched[ND_SVC_TYPES];
	int			svc_ntouched;
	uint8_t		svc_touched_list[ND_SVC_TYPES];
	int			movemsg_gap;
	int		silence_known;

	// cached session medians (refreshed once per second of buckets)
	uint32_t	median_at;
	float		median_frame_ms;
	float		jitter_ms;

	// hitch detection
	uint32_t	settle_until;		// no detection before this bucket (joining, loads, pauses)
	uint32_t	last_gap_bucket;
	int		have_last_gap;
	int		pending;
	nd_hitch_meta_t	pend;			// window is copied at capture time
	int			pend_slot;			// detected slot to replace, -1 = pick
	int		pend_mark;
	int		mark_pending;
	uint32_t	mark_trigger;
	nd_hitch_t	hitches[ND_HITCH_SLOTS];
	nd_hitch_t	marks[ND_HITCH_SLOTS];
	int			mark_next;

	nd_session_t s;
} netdiag_t;

// context the glue provides for status/report text
typedef struct nd_info_s
{
	const char	*engine;		// version string
	const char	*protocol;		// e.g. "666"
	const char	*extensions;	// e.g. "predinfo" or "-"
	const char	*map;
	const char	*hostname;
	int		recording;
	int		connected;		// false after a disconnect, while kept data can still be reported
	int		scr_ping_pl;	// scoreboard PL already shown by scr_ping
	double		silence_s;		// seconds since last update, <0 unknown
	double		timeout_left_s;	// <0 unknown
	int			backlog_now;
	int			movemsg_gap;
} nd_info_t;

// lifecycle
void	ND_Reset (netdiag_t *nd);
void	ND_SetSource (netdiag_t *nd, int source);
void	ND_Break (netdiag_t *nd, double now);	// recording resumes: the gap is unmeasured, nothing spans it
void	ND_Finish (netdiag_t *nd);	// session ending: save pending captures with whatever context exists
void	ND_Advance (netdiag_t *nd, double now);

// events (each advances time first)
void	ND_Frame (netdiag_t *nd, double now, int cond);
void	ND_ReadPass (netdiag_t *nd, double now);
void	ND_Starved (netdiag_t *nd);
void	ND_Packet (netdiag_t *nd, double now, int dir, int klass, int bytes);
void	ND_WriteFail (netdiag_t *nd, double now);
void	ND_Resent (netdiag_t *nd, double now);
void	ND_Arrival (netdiag_t *nd, double now, int missing);
void	ND_Dup (netdiag_t *nd, double now);
void	ND_Stale (netdiag_t *nd, double now);
void	ND_Short (netdiag_t *nd, double now);
void	ND_ServerTime (netdiag_t *nd, double svtime);
void	ND_MessageDone (netdiag_t *nd);
void	ND_Svc (netdiag_t *nd, int svc, int pos);
void	ND_Ping (netdiag_t *nd, double now, float ms, int moveloss);	// moveloss < 0: not reported
void	ND_CmdSent (netdiag_t *nd, int seq, double now);
void	ND_CmdAck (netdiag_t *nd, int seq, double now);
void	ND_Backlog (netdiag_t *nd, int bytes);
void	ND_MsgSize (netdiag_t *nd, int bytes);
void	ND_Mark (netdiag_t *nd, double now);
void	ND_LoadBuckets (netdiag_t *nd, const nd_bucket_t *b, int n);	// replace history with saved buckets (replay)
float	ND_FrameThresholdOf (const netdiag_t *nd);	// hitch thresholds, for drawing guides
float	ND_LateThresholdOf (const netdiag_t *nd);

// queries
typedef struct nd_summary_s
{
	double	coverage_s;			// seconds actually covered in the window
	int		frames;
	float	frame_avg_ms, frame_max_ms;
	float	starved_ms;
	int		lates;
	float	late_avg_ms, jitter_ms, late_max_ms, late_net_max_ms;
	int		arrivals;
	float	gap_avg_ms, gap_dev_ms;
	int		accepted, missing, gaps;
	float	loss_pct;			// <0 = no samples
	int		resent, dup, stale;
	int		backlog_max;
	float	update_hz;			// snapshots received per second (loss lowers it; loss is reported separately), <0 unknown
	double	in_bps, out_bps, in_pps, out_pps;	// over the rate window
	float	avg_pkt[ND_DIRS];
	int		max_pkt[ND_DIRS];
} nd_summary_t;

void	ND_Summarize (const netdiag_t *nd, int nbuckets, nd_summary_t *out);
const nd_bucket_t *ND_Bucket (const netdiag_t *nd, uint32_t index);	// NULL if not retained
uint32_t ND_LastComplete (const netdiag_t *nd);	// index of newest completed bucket
float	ND_HistPercentile (const uint32_t *hist, double p);	// <0 = empty
int		ND_HitchCount (const netdiag_t *nd, int marked);

const char *ND_StateName (const netdiag_t *nd, const nd_info_t *info);
void	ND_FormatStatus (const netdiag_t *nd, const nd_info_t *info, double now, char *buf, size_t size);
void	ND_WriteReportText (const netdiag_t *nd, const nd_info_t *info, double now, FILE *f);
void	ND_WriteReportJSON (const netdiag_t *nd, const nd_info_t *info, double now, FILE *f);
void	ND_WriteReportCSV (const netdiag_t *nd, FILE *f);
int		ND_ReadCSV (FILE *f, const char *set, int n, nd_bucket_t *out, int max);	// rows of one set back into buckets

extern const char *nd_svc_names[ND_SVC_TYPES];	// may contain NULLs; set by glue

/*
=============================================================================
Hook points for engine code

Callers check the guard before calling, so with netgraph off each hook is a
single branch: NETDIAG_SOCK(s) for socket-level hooks (also filters out every
socket except the client's own connection), netdiag_active for client hooks.
=============================================================================
*/

extern int				netdiag_active;
extern int				netdiag_alloc;
extern int				netdiag_wantframe;	// frame hook needed: data kept, replay, or scheduled dev commands
extern struct qsocket_s	*netdiag_sock;

#define NETDIAG_SOCK(s)	(netdiag_active && (s) == netdiag_sock)

// datagram classes, mirrors ND_UNREL.. above
#define NETDIAG_UNREL	0
#define NETDIAG_REL		1
#define NETDIAG_ACK		2
#define NETDIAG_OTHER	3

void NetDiag_Init (void);
void NetDiag_Frame (void);					// after each presented frame (guard: netdiag_wantframe)

// panels (netgraph.c, part 3)
struct netdiag_s;
const struct netdiag_s *NetDiag_Data (void);	// retained data (live, kept after a disconnect, or replay); NULL if none
int NetDiag_Live (void);				// connected (or replaying): the panel draws only then
int NetDiag_PanelMode (void);				// 0 none, 1 compact, 2 detailed
int NetDiag_PanelAnchor (void);				// 0 TL, 1 TR, 2 BL, 3 BR
const char *NetDiag_StateWord (void);		// LIVE, LOCAL, REPLAY, ...
float NetDiag_Silence (void);				// seconds since the last update on a network connection, <0 unknown
void NetDiag_SockClosed (struct qsocket_s *sock);

// socket hooks (guard: NETDIAG_SOCK)
void NetDiag_Sent (int klass, int len, int ret);	// ret = driver Write() result
void NetDiag_Resent (void);
void NetDiag_Received (int klass, int len);
void NetDiag_Arrival (int missing);
void NetDiag_Dup (void);
void NetDiag_Stale (void);
void NetDiag_Short (void);

// client hooks (guard: netdiag_active)
void NetDiag_ReadPass (void);
void NetDiag_ServerTime (double svtime);
void NetDiag_Svc (int cmd, int pos);		// cmd < 0 = end of message
void NetDiag_Starved (void);
void NetDiag_Ping (int slot, int ping, int moveloss);	// moveloss < 0 = not reported
void NetDiag_CmdSent (int seq);
void NetDiag_CmdAck (int seq);
void NetDiag_CmdAck16 (int ack16);

#endif
