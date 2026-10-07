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
netgraph.c -- network and frame diagnostics  // woods #netgraph

1. Collector core: buckets, statistics, hitches, reports, CSV. Pure C; with
   NETGRAPH_STANDALONE only this part is built (Misc/netdiag_test).
2. Engine glue: reads engine state, timestamps events, the netgraph command.
3. Panels and the Inspect page.

Design ideas adapted from FTE, DarkPlaces and ezQuake netgraphs; design notes
in network-graph.plan.md.
*/

#ifdef NETGRAPH_STANDALONE
#include "netgraph.h"
typedef unsigned char byte_nd;
#else
#include "quakedef.h"	// includes netgraph.h
typedef unsigned char byte_nd;
#endif

#include <math.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

/*
=============================================================================

1. COLLECTOR CORE

=============================================================================
*/

/*
Collector core  // woods #netgraph

Buckets of 20 ms hold everything the outputs show. All events land in the
bucket their timestamp falls in; queries stop at the last completed bucket so
every number describes the same instant. Hitch detection runs on completed
buckets and copies its 10 s window 4 s after the trigger, never on the spike
frame itself. Design ideas adapted from FTE, DarkPlaces and ezQuake netgraphs.
*/




const char *nd_svc_names[ND_SVC_TYPES];

#define ND_MAX(a,b) ((a) > (b) ? (a) : (b))
#define ND_MIN(a,b) ((a) < (b) ? (a) : (b))

#define ND_GAP_RESET		1024	// larger sequence jumps are treated as a reset, not loss (a 1.5 s
									// outage at 62 Hz is ~94 real losses; reconnects start a new session)
#define ND_SVGAP_MAX		1.0		// server time jumps beyond this rebaseline lateness
#define ND_MEDIAN_REFRESH	50		// buckets between session median refreshes
#define ND_STARVED_SPAN		10		// buckets (200 ms) summed for the starved trigger
#define ND_LOSS_SPAN		50		// buckets (1 s) for the two-gaps loss trigger
#define ND_MERGE_SPAN		ND_HITCH_BUCKETS	// hitches closer than 10 s merge
#define ND_CONTEXT			10		// buckets either side of a trigger for its sentence
#define ND_PING_FRESH		10.0	// seconds: about two server ping reports
#define ND_RTT_FRESH		2.0		// seconds: command acks arrive every frame when they arrive at all
#define ND_SETTLE			100		// buckets (2 s) without detection after joining, a load or a pause:
									// spawning and map loads stall the server for a moment, by design

static void ND_Settle (netdiag_t *nd);
static void ND_CmdRestart (netdiag_t *nd);

static int ND_Finite (double v)
{
	return v == v && v > -1e300 && v < 1e300;
}

static void ND_ClearBucket (nd_bucket_t *b, uint32_t index)
{
	memset (b, 0, sizeof(*b));
	b->index = index;
	b->ping_ms = -1.f;
	b->cmdrtt_ms = -1.f;
	b->late_max_ms = -1e9f;
	b->late_net_max_ms = -1e9f;
}

static nd_bucket_t *ND_CurBucket (netdiag_t *nd)
{
	return &nd->ring[nd->cur % ND_RING];
}

static void ND_ResetBaselines (netdiag_t *nd)
{
	nd->last_frame_time = -1;
	nd->last_arrival = -1;
	nd->last_sv_read = -1;
	nd->last_sv_time = -1;
	nd->cur_read_time = -1;
	nd->pass_time = -1;
	nd->pass_prev = -1;
	nd->frame_starved = 0;
}

void ND_Reset (netdiag_t *nd)
{
	memset (nd, 0, sizeof(*nd));
	ND_ResetBaselines (nd);
	nd->last_ping_ms = -1.f;
	nd->last_ping_time = -1;
	nd->last_moveloss = -1;
	nd->last_update_time = -1;
	nd->last_cmdrtt_ms = -1.f;
	nd->last_cmdrtt_time = -1;
	nd->last_acked = -1;
	nd->last_sent_seq = -1;
	nd->svc_cur = -1;
	nd->pend_slot = -1;
	nd->median_frame_ms = -1.f;
	nd->jitter_ms = 0.f;
	nd->s.ping_min = 1e9;
	nd->s.ping_max = 0;
}

void ND_SetSource (netdiag_t *nd, int source)
{
	if (nd->source != source)
	{
		nd->source = source;
		ND_ResetBaselines (nd);
		if (nd->started)
		{
			ND_CurBucket(nd)->flags |= ND_BF_DISCONT;
			ND_Settle (nd);
		}
	}
}

/*
=============
Histograms and session statistics
=============
*/
static void ND_HistAdd (uint32_t *hist, double ms)
{
	int bin;
	if (ms <= 0)
		bin = 0;
	else if (ms >= ND_HIST_BINS - 1)
		bin = ND_HIST_BINS - 1;
	else
		bin = (int)ms;
	hist[bin]++;
}

float ND_HistPercentile (const uint32_t *hist, double p)
{
	uint64_t total = 0, run = 0, target;
	int i;
	for (i = 0; i < ND_HIST_BINS; i++)
		total += hist[i];
	if (!total)
		return -1.f;
	target = (uint64_t)ceil (p * (double)total);
	if (target < 1)
		target = 1;
	for (i = 0; i < ND_HIST_BINS; i++)
	{
		run += hist[i];
		if (run >= target)
			return (float)i + (i < ND_HIST_BINS - 1 ? 0.5f : 0.f);
	}
	return (float)(ND_HIST_BINS - 1);
}

static double ND_Dev (double sum, double sumsq, double n)
{
	double mean, var;
	if (n < 2)
		return 0;
	mean = sum / n;
	var = sumsq / n - mean * mean;
	return var > 0 ? sqrt (var) : 0;	// clamp tiny negative roundoff
}

static void ND_RefreshMedians (netdiag_t *nd)
{
	nd->median_frame_ms = ND_HistPercentile (nd->s.frame_hist, 0.5);
	// from LATE, not raw lateness: a client stall must not raise the bar for later network delays
	nd->jitter_ms = (float)ND_Dev (nd->s.lnet_sum, nd->s.lnet_sumsq, nd->s.lates);
}

static float ND_FrameThreshold (const netdiag_t *nd)
{
	return (float)ND_MAX (50.0, 4.0 * (nd->median_frame_ms > 0 ? nd->median_frame_ms : 0));
}

static float ND_LateThreshold (const netdiag_t *nd)
{
	return (float)ND_MAX (40.0, 4.0 * nd->jitter_ms);
}

// one starved frame at 30 fps is already 33 ms; require a few frames' worth
static float ND_StarvedThreshold (const netdiag_t *nd)
{
	return (float)ND_MAX (30.0, 2.5 * (nd->median_frame_ms > 0 ? nd->median_frame_ms : 0));
}

static void ND_Settle (netdiag_t *nd)
{
	nd->settle_until = nd->cur + ND_SETTLE;
}

/*
=============
Message-type profile: pick the bucket's top three, then clear only touched slots
=============
*/
static void ND_FinishSvc (netdiag_t *nd, nd_bucket_t *b)
{
	int i, j, k;
	for (i = 0; i < nd->svc_ntouched; i++)
	{
		int id = nd->svc_touched_list[i];
		uint32_t bytes = nd->svc_accum[id];
		b->svc_total += bytes;
		for (j = 0; j < ND_TOP_SVC; j++)
		{
			if (bytes > b->top_svc_bytes[j])
			{
				for (k = ND_TOP_SVC - 1; k > j; k--)
				{
					b->top_svc[k] = b->top_svc[k-1];
					b->top_svc_bytes[k] = b->top_svc_bytes[k-1];
				}
				b->top_svc[j] = (uint8_t)id;
				b->top_svc_bytes[j] = (uint16_t)ND_MIN (bytes, 65535u);
				break;
			}
		}
		nd->svc_accum[id] = 0;
		nd->svc_touched[id] = 0;
	}
	nd->svc_ntouched = 0;
}

/*
=============
Hitches
=============
*/
static const nd_bucket_t *ND_Retained (const netdiag_t *nd, int64_t index)
{
	const nd_bucket_t *b;
	if (index < 0 || index > (int64_t)nd->cur || !nd->started)
		return NULL;
	b = &nd->ring[(uint32_t)index % ND_RING];
	return b->index == (uint32_t)index ? b : NULL;
}

static void ND_CopyWindow (const netdiag_t *nd, uint32_t trigger, nd_bucket_t *window)
{
	int k;
	for (k = 0; k < ND_HITCH_BUCKETS; k++)
	{
		int64_t idx = (int64_t)trigger - ND_HITCH_PRE + k;
		const nd_bucket_t *src = ND_Retained (nd, idx);
		if (src)
			window[k] = *src;
		else
		{
			ND_ClearBucket (&window[k], idx < 0 ? 0 : (uint32_t)idx);
			window[k].flags |= ND_BF_DISCONT;
		}
	}
}

static void ND_DescribeHitch (const netdiag_t *nd, nd_hitch_t *h)
{
	const nd_bucket_t *w = h->window;
	float frame = 0, late = -1e9f, starved = 0;
	int missing = 0, k, stalled, waslate, lates = 0, frames = 0;
	char extra[96];
	size_t n;

	for (k = ND_HITCH_PRE - ND_CONTEXT; k <= ND_HITCH_PRE + ND_CONTEXT; k++)
	{
		frame = ND_MAX (frame, w[k].frame_max_ms);
		frames += w[k].frames;
		if (w[k].lates)
			late = ND_MAX (late, w[k].late_net_max_ms);
		lates += w[k].lates;
		missing += w[k].missing;
	}
	if (!lates)
		late = -1;		// no network evidence: say nothing about the network
	else if (late < 0)
		late = 0;

	{	// the whole starved run through the trigger, not just the context window
		int lo = ND_HITCH_PRE, hi = ND_HITCH_PRE;
		for (k = ND_HITCH_PRE - ND_CONTEXT; k <= ND_HITCH_PRE + ND_CONTEXT; k++)
			if (w[k].starved_ms > 0)
			{
				lo = ND_MIN (lo, k);
				hi = ND_MAX (hi, k);
			}
		while (lo > 0 && (w[lo - 1].starved_ms > 0 || (w[lo - 1].frames == 0 && w[lo - 2 > 0 ? lo - 2 : 0].starved_ms > 0)))
			lo--;
		while (hi < ND_HITCH_BUCKETS - 1 && (w[hi + 1].starved_ms > 0 || (w[hi + 1].frames == 0 && hi + 2 < ND_HITCH_BUCKETS && w[hi + 2].starved_ms > 0)))
			hi++;
		for (k = lo; k <= hi; k++)
			starved += w[k].starved_ms;
	}

	h->m.frame_ms = frame;
	h->m.late_ms = late;
	h->m.starved_ms = starved;
	h->m.missing = missing;
	h->m.normal_frame_ms = nd->median_frame_ms;

	stalled = frames && frame > ND_FrameThreshold (nd);
	waslate = lates && late > ND_LateThreshold (nd);
	extra[0] = 0;

	if (h->m.kind != ND_HITCH_MARK)	// the type always matches the sentence
		h->m.kind = (stalled && waslate) ? ND_HITCH_BOTH : stalled ? ND_HITCH_FRAME : waslate ? ND_HITCH_LATE :
			missing ? ND_HITCH_LOSS : ND_HITCH_STARVED;

	if (stalled && waslate)
		snprintf (h->m.sentence, sizeof(h->m.sentence), "Frames stalled %d ms and updates were %d ms late at the same moment.", (int)frame, (int)late);
	else if (stalled && lates)	// LATE is a lower bound: during a stall it can rule out extra delay, not prove timeliness
		snprintf (h->m.sentence, sizeof(h->m.sentence), "Frames stalled %d ms; no network delay detected.", (int)frame);
	else if (stalled)
		snprintf (h->m.sentence, sizeof(h->m.sentence), "Frames stalled %d ms.", (int)frame);
	else if (waslate && frames)
		snprintf (h->m.sentence, sizeof(h->m.sentence), "Updates arrived %d ms late; frames were steady.", (int)late);
	else if (waslate)
		snprintf (h->m.sentence, sizeof(h->m.sentence), "Updates arrived %d ms late.", (int)late);
	else if (missing)
	{
		snprintf (h->m.sentence, sizeof(h->m.sentence), "%d packet%s lost.", missing, missing == 1 ? "" : "s");
		missing = 0;	// already said
	}
	else if (starved >= ND_StarvedThreshold (nd))
	{
		snprintf (h->m.sentence, sizeof(h->m.sentence), "Ran out of updates for %d ms.", (int)starved);
		starved = 0;
	}
	else if (h->m.kind == ND_HITCH_MARK)
		snprintf (h->m.sentence, sizeof(h->m.sentence), "Marked. Nothing unusual was measured.");
	else if (lates)
		snprintf (h->m.sentence, sizeof(h->m.sentence), "Frames peaked at %d ms; updates up to %d ms late.", (int)frame, (int)late);
	else
		snprintf (h->m.sentence, sizeof(h->m.sentence), "Frames peaked at %d ms.", (int)frame);

	if (missing)
	{
		n = strlen (extra);
		snprintf (extra + n, sizeof(extra) - n, " %d packet%s lost.", missing, missing == 1 ? "" : "s");
	}
	if (starved >= ND_StarvedThreshold (nd))
	{
		n = strlen (extra);
		snprintf (extra + n, sizeof(extra) - n, " Ran out of updates for %d ms.", (int)starved);
	}
	n = strlen (h->m.sentence);
	if (extra[0] && n < sizeof(h->m.sentence) - 1)
		snprintf (h->m.sentence + n, sizeof(h->m.sentence) - n, "%s", extra);
}

static void ND_CaptureDetected (netdiag_t *nd)
{
	int slot = nd->pend_slot, i;
	nd_hitch_t *h;

	if (slot < 0)
	{	// free slot, else the weakest one if we are worse than it
		for (i = 0; i < ND_HITCH_SLOTS; i++)
			if (!nd->hitches[i].m.used)
			{
				slot = i;
				break;
			}
		if (slot < 0)
		{
			slot = 0;
			for (i = 1; i < ND_HITCH_SLOTS; i++)
				if (nd->hitches[i].m.severity < nd->hitches[slot].m.severity)
					slot = i;
			if (nd->hitches[slot].m.severity >= nd->pend.severity)
				slot = -1;
		}
	}
	nd->pending = 0;
	nd->pend_slot = -1;
	if (slot < 0)
		return;

	h = &nd->hitches[slot];
	h->m = nd->pend;
	h->m.used = 1;
	ND_CopyWindow (nd, h->m.trigger, h->window);
	ND_DescribeHitch (nd, h);
	nd->s.hitches_detected++;
}

static void ND_CaptureMark (netdiag_t *nd)
{
	nd_hitch_t *h = &nd->marks[nd->mark_next];
	nd->mark_next = (nd->mark_next + 1) % ND_HITCH_SLOTS;
	memset (&h->m, 0, sizeof(h->m));
	h->m.used = 1;
	h->m.kind = ND_HITCH_MARK;
	h->m.trigger = nd->mark_trigger;
	h->m.session_time = (double)(nd->mark_trigger - nd->first) * ND_BUCKET_SEC;
	ND_CopyWindow (nd, h->m.trigger, h->window);
	ND_DescribeHitch (nd, h);
	nd->mark_pending = 0;
}

static void ND_Trigger (netdiag_t *nd, uint32_t index, int kind, float severity)
{
	int i;

	if (nd->pending)
	{
		if (index - nd->pend.trigger < ND_MERGE_SPAN)
		{
			if (severity > nd->pend.severity)
			{
				nd->pend.trigger = index;
				nd->pend.kind = kind;
				nd->pend.severity = severity;
				nd->pend.session_time = (double)(index - nd->first) * ND_BUCKET_SEC;
			}
			return;
		}
		ND_CaptureDetected (nd);	// can't happen with POST < MERGE, but stay safe
	}

	nd->pend_slot = -1;
	for (i = 0; i < ND_HITCH_SLOTS; i++)
	{
		nd_hitch_t *h = &nd->hitches[i];
		if (h->m.used && index - h->m.trigger < ND_MERGE_SPAN)
		{	// merge into a stored hitch: only a worse moment replaces it
			if (severity <= h->m.severity)
				return;
			nd->pend_slot = i;
			break;
		}
	}

	memset (&nd->pend, 0, sizeof(nd->pend));
	nd->pending = 1;
	nd->pend.trigger = index;
	nd->pend.kind = kind;
	nd->pend.severity = severity;
	nd->pend.session_time = (double)(index - nd->first) * ND_BUCKET_SEC;
}

static void ND_Detect (netdiag_t *nd, nd_bucket_t *b, uint32_t index)
{
	int frame = 0, late = 0, starved = 0, loss = 0, kind;
	float severity = 0, starved_ms = 0;
	int64_t k;

	if (b->flags & ND_BF_SUSPEND)
	{
		nd->settle_until = index + ND_SETTLE;
		return;
	}
	if (index < nd->settle_until)
		return;

	if (b->frames && b->frame_max_ms > ND_FrameThreshold (nd))
	{
		frame = 1;
		severity = ND_MAX (severity, b->frame_max_ms);
	}

	if (nd->source == ND_SRC_OTHER && b->starved_ms > 0)
	{	// other transports: no datagram view, but running out of snapshots is still measured
		for (k = (int64_t)index - ND_STARVED_SPAN + 1; k <= (int64_t)index; k++)
		{
			const nd_bucket_t *o = ND_Retained (nd, k);
			if (o && !(o->flags & ND_BF_SUSPEND))
				starved_ms += o->starved_ms;
		}
		if (starved_ms >= ND_StarvedThreshold (nd))
		{
			starved = 1;
			severity = ND_MAX (severity, starved_ms);
		}
	}
	if (nd->source == ND_SRC_NET)
	{
		if (b->lates && b->late_net_max_ms > ND_LateThreshold (nd))
		{
			late = 1;
			severity = ND_MAX (severity, b->late_net_max_ms);
		}

		if (b->starved_ms > 0)
		{
			for (k = (int64_t)index - ND_STARVED_SPAN + 1; k <= (int64_t)index; k++)
			{
				const nd_bucket_t *o = ND_Retained (nd, k);
				if (o && !(o->flags & ND_BF_SUSPEND))
					starved_ms += o->starved_ms;
			}
			if (starved_ms >= ND_StarvedThreshold (nd))
			{
				starved = 1;
				severity = ND_MAX (severity, starved_ms);
			}
		}

		if (b->gaps)
		{
			if (b->missing >= 3 || b->gaps >= 2 ||
				(nd->have_last_gap && index - nd->last_gap_bucket <= ND_LOSS_SPAN))
			{
				loss = 1;
				severity = ND_MAX (severity, 20.f * (float)ND_MAX (b->missing, 2));
			}
			nd->have_last_gap = 1;
			nd->last_gap_bucket = index;
		}
	}

	if (!(frame | late | starved | loss))
		return;

	if (frame && late)
		kind = ND_HITCH_BOTH;
	else if (frame)
		kind = ND_HITCH_FRAME;
	else if (late)
		kind = ND_HITCH_LATE;
	else if (loss)	// a cause outranks its effect: losses often starve the client too
		kind = ND_HITCH_LOSS;
	else
		kind = ND_HITCH_STARVED;

	b->flags |= ND_BF_HITCH;
	ND_Trigger (nd, index, kind, severity);
}

/*
=============
Time
=============
*/
static void ND_Complete (netdiag_t *nd, uint32_t index)
{
	nd_bucket_t *b = &nd->ring[index % ND_RING];

	if (index == nd->cur)
		ND_FinishSvc (nd, b);

	if (nd->source != ND_SRC_NONE && !(b->flags & ND_BF_UNMEASURED))
		nd->s.rec_time += ND_BUCKET_SEC;	// wall time while recording; a quiet bucket is still recorded

	if (index - nd->median_at >= ND_MEDIAN_REFRESH)
	{
		nd->median_at = index;
		ND_RefreshMedians (nd);
	}

	ND_Detect (nd, b, index);

	if (nd->pending && index >= nd->pend.trigger + ND_HITCH_POST - 1)
		ND_CaptureDetected (nd);
	if (nd->mark_pending && index >= nd->mark_trigger + ND_HITCH_POST - 1)
		ND_CaptureMark (nd);
}

void ND_Advance (netdiag_t *nd, double now)
{
	double rel;
	uint32_t idx, i;

	if (!ND_Finite (now))
		return;

	if (!nd->started)
	{
		nd->started = 1;
		nd->t0 = now;
		nd->cur = 0;
		nd->first = 0;
		nd->last_now = now;
		nd->median_at = 0;
		ND_ClearBucket (&nd->ring[0], 0);
		nd->ring[0].flags |= ND_BF_DISCONT;
		ND_Settle (nd);
		return;
	}

	if (now < nd->last_now)
	{	// clock went backwards: rebase so `now` maps to the current bucket
		nd->t0 = now - (double)nd->cur * ND_BUCKET_SEC;
		nd->last_now = now;
		ND_ResetBaselines (nd);
		ND_CurBucket(nd)->flags |= ND_BF_DISCONT;
		return;
	}

	rel = (now - nd->t0) / ND_BUCKET_SEC;
	if (rel >= 4.0e9)
	{	// absurd span: start over rather than wrap the index
		int source = nd->source;
		ND_Reset (nd);
		nd->source = source;
		ND_Advance (nd, now);
		return;
	}
	idx = (uint32_t)rel;
	nd->last_now = now;
	if (idx <= nd->cur)
		return;

	if (idx - nd->cur > ND_RING)
	{	// longer than the ring: finish the current bucket, skip the rest
		ND_Complete (nd, nd->cur);
		nd->cur = idx;
		ND_ClearBucket (ND_CurBucket(nd), idx);
		ND_CurBucket(nd)->flags |= ND_BF_DISCONT;
		ND_ResetBaselines (nd);
		if (nd->pending)
			ND_CaptureDetected (nd);
		if (nd->mark_pending)
			ND_CaptureMark (nd);
		return;
	}

	for (i = nd->cur; i < idx; i++)
	{
		ND_Complete (nd, i);
		ND_ClearBucket (&nd->ring[(i + 1) % ND_RING], i + 1);
		if (i + 1 < nd->unmeasured_until)
			nd->ring[(i + 1) % ND_RING].flags |= ND_BF_UNMEASURED;
		nd->cur = i + 1;
	}
}

/*
=============
Events
=============
*/
void ND_Frame (netdiag_t *nd, double now, int cond)
{
	nd_bucket_t *b;
	double ms;
	int counted = 0;

	ND_Advance (nd, now);
	b = ND_CurBucket (nd);

	if (cond & ND_COND_LOADING)
		b->flags |= ND_BF_LOADING;
	if (cond & ND_COND_PAUSED)
		b->flags |= ND_BF_PAUSED;
	if (cond & ND_COND_UNFOCUSED)
		b->flags |= ND_BF_UNFOCUSED;

	if (!cond && !nd->cond && nd->last_frame_time >= 0 && now >= nd->last_frame_time)
	{
		ms = (now - nd->last_frame_time) * 1000.0;
		b->frames++;
		b->frame_sum_ms += (float)ms;
		if (ms > b->frame_max_ms)
			b->frame_max_ms = (float)ms;
		nd->s.frames++;
		if (ms > 50)
			nd->s.frames_over_50++;
		if (ms > 100)
			nd->s.frames_over_100++;
		ND_HistAdd (nd->s.frame_hist, ms);
		if (nd->frame_starved)
		{
			b->starved_frames++;
			b->starved_ms += (float)ms;
			nd->s.starved_ms += ms;
		}
		counted = 1;
	}
	(void)counted;

	if (cond & (ND_COND_LOADING | ND_COND_PAUSED))
	{	// the server stops or restarts its clock; gaps across this are not network
		nd->last_arrival = -1;
		nd->last_sv_read = -1;
		nd->last_sv_time = -1;
	}
	if (cond & ND_COND_LOADING)
		ND_CmdRestart (nd);	// a new map restarts the move sequence

	nd->cond = cond;
	nd->last_frame_time = now;
	nd->frame_starved = 0;
}

void ND_Starved (netdiag_t *nd)
{
	nd->frame_starved = 1;
}

void ND_ReadPass (netdiag_t *nd, double now)
{
	nd->pass_prev = nd->pass_time;
	nd->pass_time = now;
}

void ND_Packet (netdiag_t *nd, double now, int dir, int klass, int bytes)
{
	nd_bucket_t *b;
	if (dir < 0 || dir >= ND_DIRS || klass < 0 || klass >= ND_CLASSES || bytes <= 0)
		return;
	ND_Advance (nd, now);
	b = ND_CurBucket (nd);
	b->bytes[dir][klass] += (uint32_t)bytes;
	if (b->pkts[dir][klass] < 65535)
		b->pkts[dir][klass]++;
	if (bytes > b->maxpkt[dir])
		b->maxpkt[dir] = (uint16_t)ND_MIN (bytes, 65535);
	nd->s.bytes[dir][klass] += (uint64_t)bytes;
	nd->s.pkts[dir][klass]++;
	if ((uint32_t)bytes > nd->s.maxpkt[dir])
		nd->s.maxpkt[dir] = (uint32_t)bytes;
}

void ND_WriteFail (netdiag_t *nd, double now)
{
	ND_Advance (nd, now);
	ND_CurBucket(nd)->flags |= ND_BF_WRITEFAIL;
	nd->s.writefail++;
}

void ND_Resent (netdiag_t *nd, double now)
{
	nd_bucket_t *b;
	ND_Advance (nd, now);
	b = ND_CurBucket (nd);
	if (b->resent < 65535)
		b->resent++;
	nd->s.resent++;
}

void ND_Arrival (netdiag_t *nd, double now, int missing)
{
	nd_bucket_t *b;
	double gap;

	ND_Advance (nd, now);
	b = ND_CurBucket (nd);

	if (missing > ND_GAP_RESET || missing < 0)
	{	// a reset or a sequence we can't interpret: never a confident loss figure
		b->flags |= ND_BF_GAPRESET;
		nd->s.resets++;
		missing = 0;
		nd->last_arrival = -1;
	}

	if (b->accepted < 65535)
		b->accepted++;
	nd->s.accepted++;
	if (missing)
	{
		b->missing = (uint16_t)ND_MIN (b->missing + missing, 65535);
		if (b->gaps < 65535)
			b->gaps++;
		nd->s.missing += (uint32_t)missing;
		nd->s.gaps++;
	}

	if (nd->last_arrival >= 0 && now >= nd->last_arrival)
	{
		gap = (now - nd->last_arrival) * 1000.0;
		if (b->arrivals < 65535)
			b->arrivals++;
		b->gap_sum_ms += (float)gap;
		b->gap_sumsq += (float)(gap * gap);
		if (gap > b->gap_max_ms)
			b->gap_max_ms = (float)gap;
	}
	nd->last_arrival = now;

	nd->cur_read_time = now;
	nd->cur_read_delay = (nd->pass_prev >= 0 && now >= nd->pass_prev) ? now - nd->pass_prev : 0;
	nd->last_update_time = now;
}

void ND_Dup (netdiag_t *nd, double now)
{
	nd_bucket_t *b;
	ND_Advance (nd, now);
	b = ND_CurBucket (nd);
	if (b->dup < 65535)
		b->dup++;
	nd->s.dup++;
}

void ND_Stale (netdiag_t *nd, double now)
{
	nd_bucket_t *b;
	ND_Advance (nd, now);
	b = ND_CurBucket (nd);
	if (b->stale < 65535)
		b->stale++;
	nd->s.stale++;
}

void ND_Short (netdiag_t *nd, double now)
{
	nd_bucket_t *b;
	ND_Advance (nd, now);
	b = ND_CurBucket (nd);
	if (b->shortp < 65535)
		b->shortp++;
	nd->s.shortp++;
}

/*
The server stamps each snapshot with its own clock. Lateness is how much more
time passed between our reads than between the server's stamps, so the
server's tick rate and dropped packets (both gaps widen together) cancel out.
Only messages that arrived in an unreliable datagram carry a read time.
*/
void ND_ServerTime (netdiag_t *nd, double svtime)
{
	nd_bucket_t *b;
	double svgap, readgap, late, lnet;

	if (nd->cur_read_time < 0 || !ND_Finite (svtime))
		return;

	if (nd->last_sv_time >= 0 && nd->last_sv_read >= 0)
	{
		svgap = svtime - nd->last_sv_time;
		if (svgap == 0)
			return;	// same snapshot split across messages
		if (svgap > 0 && svgap <= ND_SVGAP_MAX)
		{
			readgap = nd->cur_read_time - nd->last_sv_read;
			late = (readgap - svgap) * 1000.0;
			lnet = late - nd->cur_read_delay * 1000.0;
			{	// LATE: only what's left after the client's own read delay; "beyond" can't be negative
				double pos = lnet > 0 ? lnet : 0;
				b = ND_CurBucket (nd);
				b->late_net_sum_ms += (float)pos;
				b->late_net_sumsq += (float)(pos * pos);
				nd->s.lnet_sum += pos;
				nd->s.lnet_sumsq += pos * pos;
				ND_HistAdd (nd->s.lnet_hist, pos);
			}

			b = ND_CurBucket (nd);
			if (b->lates < 65535)
				b->lates++;
			b->late_sum_ms += (float)late;
			b->late_sumsq += (float)(late * late);
			if (late > b->late_max_ms)
				b->late_max_ms = (float)late;
			if (lnet > b->late_net_max_ms)
				b->late_net_max_ms = (float)lnet;
			b->svgap_sum_ms += (float)(svgap * 1000.0);

			nd->s.lates++;
			nd->s.late_sum += late;
			nd->s.late_sumsq += late * late;
			ND_HistAdd (nd->s.late_hist, late);
		}
	}
	nd->last_sv_time = svtime;
	nd->last_sv_read = nd->cur_read_time;
}

void ND_Svc (netdiag_t *nd, int svc, int pos)
{
	if (nd->svc_cur >= 0 && pos > nd->svc_pos)
	{
		uint32_t bytes = (uint32_t)(pos - nd->svc_pos);
		int id = nd->svc_cur;
		nd->svc_accum[id] += bytes;
		if (!nd->svc_touched[id])
		{
			nd->svc_touched[id] = 1;
			nd->svc_touched_list[nd->svc_ntouched++] = (uint8_t)id;
		}
		nd->s.svc_bytes[id] += bytes;
		nd->s.svc_count[id]++;
	}
	if (svc >= 0 && svc < ND_SVC_TYPES)
	{
		nd->svc_cur = svc;
		nd->svc_pos = pos;
	}
	else
		nd->svc_cur = -1;
}

void ND_MessageDone (netdiag_t *nd)
{
	nd->cur_read_time = -1;
	nd->svc_cur = -1;
}

void ND_Ping (netdiag_t *nd, double now, float ms, int moveloss)
{
	if (!ND_Finite (ms) || ms < 0)
		return;
	ND_Advance (nd, now);
	ND_CurBucket(nd)->ping_ms = ms;
	nd->last_ping_ms = ms;
	nd->last_ping_time = now;
	nd->last_moveloss = moveloss;
	nd->s.ping_reports++;
	nd->s.ping_sum += ms;
	nd->s.ping_sumsq += (double)ms * ms;
	if (ms < nd->s.ping_min)
		nd->s.ping_min = ms;
	if (ms > nd->s.ping_max)
		nd->s.ping_max = ms;
}

// a new map restarts the move sequence: forget commands, acks and the latency they gave
static void ND_CmdRestart (netdiag_t *nd)
{
	memset (nd->cmds, 0, sizeof(nd->cmds));
	nd->last_acked = -1;
	nd->last_sent_seq = -1;
	nd->last_cmdrtt_time = -1;
	nd->last_ping_time = -1;
	nd->last_moveloss = -1;
}

void ND_CmdSent (netdiag_t *nd, int seq, double now)
{
	nd_cmd_t *c;
	if (seq < 0)
		return;
	if (seq < nd->last_sent_seq)
		ND_CmdRestart (nd);	// our own move sequence started over
	c = &nd->cmds[seq & (ND_CMD_RING - 1)];
	c->seq = seq;
	c->time = now;
	nd->last_sent_seq = seq;
}

void ND_CmdAck (netdiag_t *nd, int seq, double now)
{
	int i;
	float ms;

	if (seq <= nd->last_acked || seq < 0)
		return;
	nd->last_acked = seq;

	i = seq & (ND_CMD_RING - 1);
	{
		if (nd->cmds[i].seq == seq && nd->cmds[i].time > 0 && now >= nd->cmds[i].time)
		{
			ms = (float)((now - nd->cmds[i].time) * 1000.0);
			ND_Advance (nd, now);
			ND_CurBucket(nd)->cmdrtt_ms = ms;
			nd->last_cmdrtt_ms = ms;
			nd->last_cmdrtt_time = now;
			nd->s.cmdrtt_samples++;
			nd->s.cmdrtt_sum += ms;
			return;
		}
	}
}

void ND_Backlog (netdiag_t *nd, int bytes)
{
	nd_bucket_t *b;
	if (!nd->started || bytes <= 0)
		return;
	b = ND_CurBucket (nd);
	if (bytes > b->backlog_max)
		b->backlog_max = (uint16_t)ND_MIN (bytes, 65535);
	if ((uint32_t)bytes > nd->s.backlog_peak)
		nd->s.backlog_peak = (uint32_t)bytes;
}

void ND_MsgSize (netdiag_t *nd, int bytes)
{
	if (bytes > 0 && (uint32_t)bytes > nd->s.msg_peak)
		nd->s.msg_peak = (uint32_t)bytes;
}

void ND_Mark (netdiag_t *nd, double now)
{
	ND_Advance (nd, now);
	if (nd->mark_pending)
		return;
	nd->mark_pending = 1;
	nd->mark_trigger = nd->cur;
	ND_CurBucket(nd)->flags |= ND_BF_MARK;
}

void ND_LoadBuckets (netdiag_t *nd, const nd_bucket_t *b, int n)
{
	int i;
	if (n > ND_RING - 1)
	{
		b += n - (ND_RING - 1);
		n = ND_RING - 1;
	}
	ND_Reset (nd);
	nd->source = ND_SRC_NET;
	nd->started = 1;
	for (i = 0; i < n; i++)
	{
		nd->ring[i] = b[i];
		nd->ring[i].index = (uint32_t)i;
	}
	nd->cur = (uint32_t)n;
	ND_ClearBucket (&nd->ring[n % ND_RING], (uint32_t)n);
	nd->s.rec_time = n * ND_BUCKET_SEC;
	nd->settle_until = nd->cur;
}

void ND_Break (netdiag_t *nd, double now)
{
	double rel;
	if (!nd->started || !ND_Finite (now) || now < nd->last_now)
		return;
	// everything between the last recorded bucket and now was not measured
	rel = (now - nd->t0) / ND_BUCKET_SEC;
	if (rel < 4.0e9)
		nd->unmeasured_until = (uint32_t)rel;
	ND_Advance (nd, now);
	ND_ResetBaselines (nd);
	nd->cond = 0;
	ND_CurBucket(nd)->flags |= ND_BF_DISCONT;
	ND_Settle (nd);
}

void ND_Finish (netdiag_t *nd)
{	// keep the moment even without its full "after"; the missing buckets are marked as gaps
	if (!nd->started)
		return;
	ND_Complete (nd, nd->cur);	// the current bucket may hold the trigger itself
	nd->cur++;
	ND_ClearBucket (ND_CurBucket(nd), nd->cur);
	if (nd->pending)
		ND_CaptureDetected (nd);
	if (nd->mark_pending)
		ND_CaptureMark (nd);
}

float ND_FrameThresholdOf (const netdiag_t *nd)
{
	return ND_FrameThreshold (nd);
}

float ND_LateThresholdOf (const netdiag_t *nd)
{
	return ND_LateThreshold (nd);
}

/*
=============
Queries
=============
*/
const nd_bucket_t *ND_Bucket (const netdiag_t *nd, uint32_t index)
{
	return ND_Retained (nd, (int64_t)index);
}

uint32_t ND_LastComplete (const netdiag_t *nd)
{
	return nd->cur ? nd->cur - 1 : 0;
}

int ND_HitchCount (const netdiag_t *nd, int marked)
{
	const nd_hitch_t *list = marked ? nd->marks : nd->hitches;
	int i, n = 0;
	for (i = 0; i < ND_HITCH_SLOTS; i++)
		if (list[i].m.used)
			n++;
	return n;
}

void ND_Summarize (const netdiag_t *nd, int nbuckets, nd_summary_t *out)
{
	int64_t last, k, first;
	double frame_sum = 0, late_sum = 0, late_sumsq = 0, gap_sum = 0, gap_sumsq = 0, svgap_sum = 0;
	double rate_span = 0, bytes[ND_DIRS] = {0, 0}, pkts[ND_DIRS] = {0, 0};
	double size_bytes[ND_DIRS] = {0, 0}, size_pkts[ND_DIRS] = {0, 0};
	int covered = 0, d, c;

	memset (out, 0, sizeof(*out));
	out->late_max_ms = 0;
	out->late_net_max_ms = 0;
	out->loss_pct = -1.f;
	out->update_hz = -1.f;
	if (!nd->started || !nd->cur)
		return;

	last = (int64_t)nd->cur - 1;
	first = last - nbuckets + 1;
	if (first < (int64_t)nd->first)
		first = nd->first;
	{
		int have_late = 0;
		for (k = first; k <= last; k++)
		{
			const nd_bucket_t *b = ND_Retained (nd, k);
			if (!b || (b->flags & ND_BF_UNMEASURED))
				continue;	// not recorded: no data, not zero
			covered++;
			out->frames += b->frames;
			frame_sum += b->frame_sum_ms;
			out->frame_max_ms = ND_MAX (out->frame_max_ms, b->frame_max_ms);
			out->starved_ms += b->starved_ms;
			if (b->lates)
			{
				if (!have_late)
				{
					out->late_max_ms = b->late_max_ms;
					out->late_net_max_ms = b->late_net_max_ms;
					have_late = 1;
				}
				out->late_max_ms = ND_MAX (out->late_max_ms, b->late_max_ms);
				out->late_net_max_ms = ND_MAX (out->late_net_max_ms, b->late_net_max_ms);
			}
			out->lates += b->lates;
			late_sum += b->late_net_sum_ms;	// LATE, beyond the client's read delay
			late_sumsq += b->late_net_sumsq;
			svgap_sum += b->svgap_sum_ms;
			out->arrivals += b->arrivals;
			gap_sum += b->gap_sum_ms;
			gap_sumsq += b->gap_sumsq;
			if (!(b->flags & ND_BF_GAPRESET))
			{
				out->accepted += b->accepted;
				out->missing += b->missing;
				out->gaps += b->gaps;
			}
			out->resent += b->resent;
			out->dup += b->dup;
			out->stale += b->stale;
			out->backlog_max = ND_MAX (out->backlog_max, (int)b->backlog_max);
			for (d = 0; d < ND_DIRS; d++)
			{
				for (c = 0; c < ND_CLASSES; c++)
				{
					size_bytes[d] += b->bytes[d][c];
					size_pkts[d] += b->pkts[d][c];
				}
				out->max_pkt[d] = ND_MAX (out->max_pkt[d], (int)b->maxpkt[d]);
			}
			if (k > last - ND_RATE_BUCKETS)
			{
				rate_span += ND_BUCKET_SEC;
				for (d = 0; d < ND_DIRS; d++)
					for (c = 0; c < ND_CLASSES; c++)
					{
						bytes[d] += b->bytes[d][c];
						pkts[d] += b->pkts[d][c];
					}
			}
		}
	}

	out->coverage_s = covered * ND_BUCKET_SEC;
	if (out->frames)
		out->frame_avg_ms = (float)(frame_sum / out->frames);
	if (out->lates)
	{
		out->late_avg_ms = (float)(late_sum / out->lates);
		out->jitter_ms = (float)ND_Dev (late_sum, late_sumsq, out->lates);
		if (svgap_sum > 0)
			out->update_hz = (float)(1000.0 * out->lates / svgap_sum);	// received snapshots per second of server time
	}
	if (out->arrivals)
	{
		out->gap_avg_ms = (float)(gap_sum / out->arrivals);
		out->gap_dev_ms = (float)ND_Dev (gap_sum, gap_sumsq, out->arrivals);
	}
	if (out->accepted + out->missing > 0)
		out->loss_pct = (float)(100.0 * out->missing / (double)(out->accepted + out->missing));
	if (rate_span > 0)
	{
		out->in_bps = bytes[ND_IN] / rate_span;
		out->out_bps = bytes[ND_OUT] / rate_span;
		out->in_pps = pkts[ND_IN] / rate_span;
		out->out_pps = pkts[ND_OUT] / rate_span;
	}
	for (d = 0; d < ND_DIRS; d++)
		if (size_pkts[d] > 0)
			out->avg_pkt[d] = (float)(size_bytes[d] / size_pkts[d]);
}

/*
=============
Text output
=============
*/
const char *ND_StateName (const netdiag_t *nd, const nd_info_t *info)
{
	if (!info->recording)
		return "off";
	if (!info->connected)
		return "idle";
	switch (nd->source)
	{
	case ND_SRC_NONE:	return "idle";
	case ND_SRC_DEMO:	return "demo";
	case ND_SRC_LOCAL:	return "local";
	default:			break;
	}
	if (nd->cond & ND_COND_LOADING)
		return "loading";
	if (nd->cond & ND_COND_PAUSED)
		return "paused";
	if (nd->cond & ND_COND_UNFOCUSED)
		return "unfocused";
	if (nd->s.rec_time < 1.0)
		return "warming";
	return "live";
}

static const char *ND_SourceName (int source)
{
	switch (source)
	{
	case ND_SRC_NET:	return "net";
	case ND_SRC_OTHER:	return "other";
	case ND_SRC_LOCAL:	return "local";
	case ND_SRC_DEMO:	return "demo";
	default:			return "none";
	}
}

static const char *ND_SvcName (int id, char *tmp, size_t size)
{
	if (id >= 0 && id < ND_SVC_TYPES && nd_svc_names[id])
		return nd_svc_names[id];
	snprintf (tmp, size, "svc%d", id);
	return tmp;
}

// appends " key=value" with `--` when unknown
static void ND_KV (char *buf, size_t size, const char *key, int known, const char *fmt, double v)
{
	size_t n = strlen (buf);
	if (n >= size - 1)
		return;
	if (!known)
		snprintf (buf + n, size - n, " %s=--", key);
	else
	{
		char val[64];
		snprintf (val, sizeof(val), fmt, v);
		snprintf (buf + n, size - n, " %s=%s", key, val);
	}
}

static int ND_NetKnown (const netdiag_t *nd)
{
	return nd->source == ND_SRC_NET;
}

void ND_FormatStatus (const netdiag_t *nd, const nd_info_t *info, double now, char *buf, size_t size)
{
	nd_summary_t s;
	int net = ND_NetKnown (nd);
	int ping_fresh;

	ND_Summarize (nd, ND_WINDOW_BUCKETS, &s);
	ping_fresh = nd->last_ping_time >= 0;

	snprintf (buf, size, "netgraph: v=2 state=%s src=%s proto=%s ext=%s",
		ND_StateName (nd, info), ND_SourceName (nd->source),
		info->protocol ? info->protocol : "--", info->extensions && *info->extensions ? info->extensions : "-");
	ND_KV (buf, size, "updates_hz", net && s.update_hz > 0, "%.1f", s.update_hz);
	// stale numbers aren't current: ping reports every few seconds, round trips every frame
	ND_KV (buf, size, "ping", ping_fresh && now - nd->last_ping_time <= ND_PING_FRESH, "%.0f", nd->last_ping_ms);
	ND_KV (buf, size, "ping_age", ping_fresh, "%.1f", now - nd->last_ping_time);
	ND_KV (buf, size, "move_loss", ping_fresh && now - nd->last_ping_time <= ND_PING_FRESH && nd->last_moveloss >= 0, "%.0f", nd->last_moveloss);
	ND_KV (buf, size, "cmdrtt", (nd->source == ND_SRC_NET || nd->source == ND_SRC_OTHER) && nd->last_cmdrtt_time >= 0 &&
		now - nd->last_cmdrtt_time <= ND_RTT_FRESH, "%.0f", nd->last_cmdrtt_ms);	// loopback acks say nothing about a network
	ND_KV (buf, size, "late_ms", net && s.lates, "%.1f", s.late_avg_ms);
	ND_KV (buf, size, "late_peak_ms", net && s.lates, "%.1f", s.late_net_max_ms > 0 ? s.late_net_max_ms : 0);
	ND_KV (buf, size, "jitter_ms", net && s.lates > 1, "%.1f", s.jitter_ms);
	ND_KV (buf, size, "frame_ms", s.frames > 0, "%.1f", s.frame_avg_ms);
	ND_KV (buf, size, "frame_peak_ms", s.frames > 0, "%.1f", s.frame_max_ms);
	ND_KV (buf, size, "starved_ms", net || nd->source == ND_SRC_OTHER, "%.0f", s.starved_ms);
	ND_KV (buf, size, "loss_pct", net && s.loss_pct >= 0, "%.2f", s.loss_pct);
	ND_KV (buf, size, "gaps", net, "%.0f", s.gaps);
	ND_KV (buf, size, "resent", net, "%.0f", s.resent);
	ND_KV (buf, size, "backlog_b", net, "%.0f", info->backlog_now);
	ND_KV (buf, size, "in_bps", net, "%.0f", s.in_bps);
	ND_KV (buf, size, "out_bps", net, "%.0f", s.out_bps);
	ND_KV (buf, size, "in_pps", net, "%.0f", s.in_pps);
	ND_KV (buf, size, "out_pps", net, "%.0f", s.out_pps);
	ND_KV (buf, size, "silence_s", info->silence_s >= 0, "%.1f", info->silence_s);
	ND_KV (buf, size, "hitches", 1, "%.0f", ND_HitchCount (nd, 0));
	ND_KV (buf, size, "marked", 1, "%.0f", ND_HitchCount (nd, 1));
	ND_KV (buf, size, "rec_s", 1, "%.1f", nd->s.rec_time);
}

static void ND_Sparkline (FILE *f, const nd_hitch_t *h, int late)
{
	static const char ramp[] = " .:-=+*#%@";
	const int cols = 60, per = (ND_HITCH_BUCKETS + cols - 1) / cols;
	float vals[60], top = 1;
	int c, k;

	for (c = 0; c < cols; c++)
	{
		float v = 0;
		for (k = c * per; k < (c + 1) * per && k < ND_HITCH_BUCKETS; k++)
		{
			const nd_bucket_t *b = &h->window[k];
			if (late)
			{
				if (b->lates)
					v = ND_MAX (v, b->late_net_max_ms);
			}
			else
				v = ND_MAX (v, b->frame_max_ms);
		}
		vals[c] = v;
		top = ND_MAX (top, v);
	}
	fprintf (f, "    %-6s|", late ? "LATE" : "FRAME");
	for (c = 0; c < cols; c++)
	{
		int i = vals[c] <= 0 ? 0 : (int)(vals[c] / top * 9.0f + 0.5f);
		fputc (ramp[ND_MIN (ND_MAX (i, 0), 9)], f);
	}
	fprintf (f, "| peak %d ms\n", (int)top);
}

static void ND_WriteHitchText (FILE *f, const nd_hitch_t *h, const char *label, int n)
{
	fprintf (f, "  %s %d at %d:%02d into recording\n", label, n,
		(int)h->m.session_time / 60, (int)h->m.session_time % 60);
	fprintf (f, "    %s\n", h->m.sentence);
	{
		char late[16];
		if (h->m.late_ms >= 0)
			snprintf (late, sizeof(late), "%d ms", (int)h->m.late_ms);
		else
			snprintf (late, sizeof(late), "--");
		fprintf (f, "    FRAME %d ms (normal %d)  LATE %s  STARVED %d ms  LOST %d\n",
			(int)h->m.frame_ms, (int)(h->m.normal_frame_ms > 0 ? h->m.normal_frame_ms : 0),
			late, (int)h->m.starved_ms, h->m.missing);
	}
	ND_Sparkline (f, h, 1);
	ND_Sparkline (f, h, 0);
	{	// axis under the 60 sparkline columns, which start after "    LABEL |" (11 chars)
		char axis[80];
		const int per = (ND_HITCH_BUCKETS + 59) / 60, at = ND_HITCH_PRE / per;
		memset (axis, ' ', sizeof(axis));
		memcpy (axis + 11, "-6s", 3);
		axis[11 + at] = '^';
		memcpy (axis + 11 + 60 - 3, "+4s", 3);
		axis[11 + 60] = 0;
		fprintf (f, "%s\n", axis);
	}
}

void ND_WriteReportText (const netdiag_t *nd, const nd_info_t *info, double now, FILE *f)
{
	const nd_session_t *s = &nd->s;
	double totals[ND_SVC_TYPES];
	int order[ND_SVC_TYPES], i, j, n, shown;
	char tmp[16];
	uint64_t in = 0, out = 0, inp = 0, outp = 0;

	for (i = 0; i < ND_CLASSES; i++)
	{
		in += s->bytes[ND_IN][i];
		out += s->bytes[ND_OUT][i];
		inp += s->pkts[ND_IN][i];
		outp += s->pkts[ND_OUT][i];
	}

	fprintf (f, "QSS-M netgraph report (v1)\n");
	fprintf (f, "  engine %s, protocol %s (%s)\n", info->engine ? info->engine : "?",
		info->protocol ? info->protocol : "--", info->extensions && *info->extensions ? info->extensions : "-");
	fprintf (f, "  map %s, server \"%s\", source %s\n", info->map && *info->map ? info->map : "--",
		info->hostname && *info->hostname ? info->hostname : "--", ND_SourceName (nd->source));
	fprintf (f, "  recorded %d:%02d\n\n", (int)s->rec_time / 60, (int)s->rec_time % 60);

	fprintf (f, "SESSION\n");
	{
		float fm = ND_HistPercentile (s->frame_hist, 0.5), f99 = ND_HistPercentile (s->frame_hist, 0.99);
		float lm = ND_HistPercentile (s->lnet_hist, 0.5), l99 = ND_HistPercentile (s->lnet_hist, 0.99);
		float r99 = ND_HistPercentile (s->late_hist, 0.99);
		if (fm >= 0)
			fprintf (f, "  frames      %u, median %.1f ms, p99 %.1f ms, over 50 ms %u, over 100 ms %u\n",
				s->frames, fm, f99, s->frames_over_50, s->frames_over_100);
		else
			fprintf (f, "  frames      --\n");
		if (s->lates)
		{
			fprintf (f, "  late        median %.1f ms, p99 %.1f ms, jitter %.1f ms (%u samples; beyond the client's read delay)\n",
				lm, l99, ND_Dev (s->lnet_sum, s->lnet_sumsq, s->lates), s->lates);
			fprintf (f, "  raw late    p99 %.1f ms, jitter %.1f ms (includes the client's own frame and read delays)\n",
				r99, ND_Dev (s->late_sum, s->late_sumsq, s->lates));
		}
		else
			fprintf (f, "  late        --\n");
	}
	if (nd->source != ND_SRC_NET)
	{	// loopback, demo or another transport: the datagram view wasn't measured
		if (nd->source == ND_SRC_OTHER)
			fprintf (f, "  starved     %.0f ms\n", s->starved_ms);
		fprintf (f, "  network     not measured (%s)\n", ND_SourceName (nd->source));
		fprintf (f, "  message peak %u B\n\n", s->msg_peak);
	}
	else
	{
	fprintf (f, "  starved     %.0f ms\n", s->starved_ms);
	if (s->accepted + s->missing)
		fprintf (f, "  loss        %u missing in %u gaps (%.2f%%), %u resets\n", s->missing, s->gaps,
			100.0 * s->missing / (double)(s->accepted + s->missing), s->resets);
	else
		fprintf (f, "  loss        --\n");
	fprintf (f, "  dup/stale   %u / %u, short %u, resent %u, write failures %u\n", s->dup, s->stale, s->shortp, s->resent, s->writefail);
	if (s->ping_reports)
		fprintf (f, "  ping        %u reports, avg %.0f ms (%.0f-%.0f), dev %.1f ms\n", s->ping_reports,
			s->ping_sum / s->ping_reports, s->ping_min, s->ping_max,
			ND_Dev (s->ping_sum, s->ping_sumsq, s->ping_reports));
	else
		fprintf (f, "  ping        --\n");
	if (s->cmdrtt_samples)
		fprintf (f, "  cmd rtt     avg %.0f ms (%u samples)\n", s->cmdrtt_sum / s->cmdrtt_samples, s->cmdrtt_samples);
	else
		fprintf (f, "  cmd rtt     --\n");
	fprintf (f, "  traffic     in %llu B in %llu pkts (max %u), out %llu B in %llu pkts (max %u)\n",
		(unsigned long long)in, (unsigned long long)inp, s->maxpkt[ND_IN],
		(unsigned long long)out, (unsigned long long)outp, s->maxpkt[ND_OUT]);
	fprintf (f, "  backlog     peak %u B; message peak %u B\n\n", s->backlog_peak, s->msg_peak);
	}

	n = ND_HitchCount (nd, 0) + ND_HitchCount (nd, 1);
	fprintf (f, "HITCHES (%d)\n", n);
	for (i = 0, j = 1; i < ND_HITCH_SLOTS; i++)
		if (nd->hitches[i].m.used)
			ND_WriteHitchText (f, &nd->hitches[i], "HITCH", j++);
	for (i = 0, j = 1; i < ND_HITCH_SLOTS; i++)
		if (nd->marks[i].m.used)
			ND_WriteHitchText (f, &nd->marks[i], "MARK", j++);
	if (!n)
		fprintf (f, "  none\n");

	for (i = 0; i < ND_SVC_TYPES; i++)
	{
		order[i] = i;
		totals[i] = (double)s->svc_bytes[i];
	}
	for (i = 1; i < ND_SVC_TYPES; i++)
	{
		int v = order[i];
		for (j = i; j > 0 && totals[order[j-1]] < totals[v]; j--)
			order[j] = order[j-1];
		order[j] = v;
	}
	fprintf (f, "\nMESSAGES (server -> you, by bytes)\n");
	for (i = 0, shown = 0; i < ND_SVC_TYPES && shown < 8; i++)
	{
		int id = order[i];
		if (!s->svc_bytes[id])
			break;
		fprintf (f, "  %-24s %10llu B  %8u msgs\n", ND_SvcName (id, tmp, sizeof(tmp)),
			(unsigned long long)s->svc_bytes[id], s->svc_count[id]);
		shown++;
	}
	if (!shown)
		fprintf (f, "  --\n");

	fprintf (f, "\nNote: times are when QSS-M read each packet, once per frame. A frame stall\n"
				"delays reads too, so LATE counts only lateness beyond the client's own read delay.\n");
	(void)now;
}

static void ND_JSONString (FILE *f, const char *s)
{
	fputc ('"', f);
	for (; s && *s; s++)
	{
		unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\')
			fprintf (f, "\\%c", c);
		else if (c < 0x20 || c >= 0x7f)
			fprintf (f, "\\u%04x", c);
		else
			fputc (c, f);
	}
	fputc ('"', f);
}

static void ND_JSONNum (FILE *f, const char *key, int known, double v, int comma)
{
	fprintf (f, "\"%s\":", key);
	if (known && ND_Finite (v))
		fprintf (f, "%.3f", v);
	else
		fprintf (f, "null");
	if (comma)
		fputc (',', f);
}

static void ND_JSONHitch (FILE *f, const nd_hitch_t *h)
{
	fprintf (f, "{");
	ND_JSONNum (f, "kind", 1, h->m.kind, 1);
	ND_JSONNum (f, "time_s", 1, h->m.session_time, 1);
	ND_JSONNum (f, "trigger_bucket", 1, h->m.trigger, 1);
	ND_JSONNum (f, "severity", 1, h->m.severity, 1);
	ND_JSONNum (f, "frame_ms", 1, h->m.frame_ms, 1);
	ND_JSONNum (f, "late_ms", h->m.late_ms >= 0, h->m.late_ms, 1);
	ND_JSONNum (f, "starved_ms", 1, h->m.starved_ms, 1);
	ND_JSONNum (f, "missing", 1, h->m.missing, 1);
	ND_JSONNum (f, "normal_frame_ms", h->m.normal_frame_ms > 0, h->m.normal_frame_ms, 1);
	fprintf (f, "\"sentence\":");
	ND_JSONString (f, h->m.sentence);
	fprintf (f, "}");
}

void ND_WriteReportJSON (const netdiag_t *nd, const nd_info_t *info, double now, FILE *f)
{
	const nd_session_t *s = &nd->s;
	char status[1024];
	nd_summary_t w;
	int i, first, net = ND_NetKnown (nd);

	ND_Summarize (nd, ND_WINDOW_BUCKETS, &w);
	ND_FormatStatus (nd, info, now, status, sizeof(status));

	fprintf (f, "{\"v\":1,");
	fprintf (f, "\"engine\":"); ND_JSONString (f, info->engine); fputc (',', f);
	fprintf (f, "\"protocol\":"); ND_JSONString (f, info->protocol); fputc (',', f);
	fprintf (f, "\"extensions\":"); ND_JSONString (f, info->extensions); fputc (',', f);
	fprintf (f, "\"map\":"); ND_JSONString (f, info->map); fputc (',', f);
	fprintf (f, "\"hostname\":"); ND_JSONString (f, info->hostname); fputc (',', f);
	fprintf (f, "\"source\":"); ND_JSONString (f, ND_SourceName (nd->source)); fputc (',', f);
	fprintf (f, "\"state\":"); ND_JSONString (f, ND_StateName (nd, info)); fputc (',', f);
	fprintf (f, "\"status\":"); ND_JSONString (f, status); fputc (',', f);

	fprintf (f, "\"session\":{");
	ND_JSONNum (f, "rec_s", 1, s->rec_time, 1);
	ND_JSONNum (f, "frames", 1, s->frames, 1);
	ND_JSONNum (f, "frames_over_50", 1, s->frames_over_50, 1);
	ND_JSONNum (f, "frames_over_100", 1, s->frames_over_100, 1);
	ND_JSONNum (f, "frame_median_ms", s->frames > 0, ND_HistPercentile (s->frame_hist, 0.5), 1);
	ND_JSONNum (f, "frame_p99_ms", s->frames > 0, ND_HistPercentile (s->frame_hist, 0.99), 1);
	// late_* and jitter: beyond the client's read delay (what the graph shows); raw_*: including it
	ND_JSONNum (f, "late_median_ms", s->lates > 0, ND_HistPercentile (s->lnet_hist, 0.5), 1);
	ND_JSONNum (f, "late_p99_ms", s->lates > 0, ND_HistPercentile (s->lnet_hist, 0.99), 1);
	ND_JSONNum (f, "jitter_ms", s->lates > 1, ND_Dev (s->lnet_sum, s->lnet_sumsq, s->lates), 1);
	ND_JSONNum (f, "raw_late_median_ms", s->lates > 0, ND_HistPercentile (s->late_hist, 0.5), 1);
	ND_JSONNum (f, "raw_late_p99_ms", s->lates > 0, ND_HistPercentile (s->late_hist, 0.99), 1);
	ND_JSONNum (f, "raw_jitter_ms", s->lates > 1, ND_Dev (s->late_sum, s->late_sumsq, s->lates), 1);
	ND_JSONNum (f, "late_samples", 1, s->lates, 1);
	ND_JSONNum (f, "starved_ms", net, s->starved_ms, 1);
	ND_JSONNum (f, "accepted", net, s->accepted, 1);
	ND_JSONNum (f, "missing", net, s->missing, 1);
	ND_JSONNum (f, "gaps", net, s->gaps, 1);
	ND_JSONNum (f, "resets", net, s->resets, 1);
	ND_JSONNum (f, "dup", net, s->dup, 1);
	ND_JSONNum (f, "stale", net, s->stale, 1);
	ND_JSONNum (f, "short", net, s->shortp, 1);
	ND_JSONNum (f, "resent", net, s->resent, 1);
	ND_JSONNum (f, "write_failures", net, s->writefail, 1);
	ND_JSONNum (f, "backlog_peak_b", net, s->backlog_peak, 1);
	ND_JSONNum (f, "msg_peak_b", 1, s->msg_peak, 1);
	ND_JSONNum (f, "ping_reports", 1, s->ping_reports, 1);
	ND_JSONNum (f, "ping_avg_ms", s->ping_reports > 0, s->ping_reports ? s->ping_sum / s->ping_reports : 0, 1);
	ND_JSONNum (f, "cmdrtt_samples", net || nd->source == ND_SRC_OTHER, s->cmdrtt_samples, 1);
	ND_JSONNum (f, "cmdrtt_avg_ms", (net || nd->source == ND_SRC_OTHER) && s->cmdrtt_samples > 0, s->cmdrtt_samples ? s->cmdrtt_sum / s->cmdrtt_samples : 0, 1);
	{
		static const char *dn[ND_DIRS] = {"in", "out"};
		static const char *cn[ND_CLASSES] = {"unreliable", "reliable", "ack", "other"};
		int d, c;
		for (d = 0; d < ND_DIRS; d++)
			for (c = 0; c < ND_CLASSES; c++)
			{
				char key[48];
				snprintf (key, sizeof(key), "%s_%s_bytes", dn[d], cn[c]);
				ND_JSONNum (f, key, net, (double)s->bytes[d][c], 1);
				snprintf (key, sizeof(key), "%s_%s_pkts", dn[d], cn[c]);
				ND_JSONNum (f, key, net, (double)s->pkts[d][c], 1);
			}
		ND_JSONNum (f, "in_max_pkt", net, s->maxpkt[ND_IN], 1);
		ND_JSONNum (f, "out_max_pkt", net, s->maxpkt[ND_OUT], 0);
	}
	fprintf (f, "},");

	fprintf (f, "\"hitches\":[");
	for (i = 0, first = 1; i < ND_HITCH_SLOTS; i++)
		if (nd->hitches[i].m.used)
		{
			if (!first)
				fputc (',', f);
			ND_JSONHitch (f, &nd->hitches[i]);
			first = 0;
		}
	fprintf (f, "],\"marks\":[");
	for (i = 0, first = 1; i < ND_HITCH_SLOTS; i++)
		if (nd->marks[i].m.used)
		{
			if (!first)
				fputc (',', f);
			ND_JSONHitch (f, &nd->marks[i]);
			first = 0;
		}
	fprintf (f, "],\"messages\":{");
	{
		char tmp[16];
		for (i = 0, first = 1; i < ND_SVC_TYPES; i++)
			if (s->svc_bytes[i])
			{
				if (!first)
					fputc (',', f);
				ND_JSONString (f, ND_SvcName (i, tmp, sizeof(tmp)));
				fprintf (f, ":{\"bytes\":%llu,\"count\":%u}", (unsigned long long)s->svc_bytes[i], s->svc_count[i]);
				first = 0;
			}
	}
	fprintf (f, "}}\n");
}

/*
=============
CSV: one field table for writing and reading, so export and replay can't drift.
Floats use %.9g (exact for float); "none" values are empty cells.
=============
*/
typedef enum { NDC_U8, NDC_U16, NDC_U32, NDC_FLT } nd_ctype_t;
#define NDC_LATE	1	// empty when the bucket has no lateness samples
#define NDC_NEG		2	// empty when negative ("none")
typedef struct { const char *name; size_t ofs; nd_ctype_t type; int opt; } nd_cfield_t;
#define NDC(n, f, t, o) { n, offsetof(nd_bucket_t, f), t, o }
static const nd_cfield_t nd_cfields[] = {
	NDC("flags", flags, NDC_U16, 0),
	NDC("in_unrel_b", bytes[ND_IN][ND_UNREL], NDC_U32, 0), NDC("in_rel_b", bytes[ND_IN][ND_REL], NDC_U32, 0),
	NDC("in_ack_b", bytes[ND_IN][ND_ACK], NDC_U32, 0), NDC("in_other_b", bytes[ND_IN][ND_OTHER], NDC_U32, 0),
	NDC("in_unrel_p", pkts[ND_IN][ND_UNREL], NDC_U16, 0), NDC("in_rel_p", pkts[ND_IN][ND_REL], NDC_U16, 0),
	NDC("in_ack_p", pkts[ND_IN][ND_ACK], NDC_U16, 0), NDC("in_other_p", pkts[ND_IN][ND_OTHER], NDC_U16, 0),
	NDC("in_maxpkt", maxpkt[ND_IN], NDC_U16, 0),
	NDC("out_unrel_b", bytes[ND_OUT][ND_UNREL], NDC_U32, 0), NDC("out_rel_b", bytes[ND_OUT][ND_REL], NDC_U32, 0),
	NDC("out_ack_b", bytes[ND_OUT][ND_ACK], NDC_U32, 0), NDC("out_other_b", bytes[ND_OUT][ND_OTHER], NDC_U32, 0),
	NDC("out_unrel_p", pkts[ND_OUT][ND_UNREL], NDC_U16, 0), NDC("out_rel_p", pkts[ND_OUT][ND_REL], NDC_U16, 0),
	NDC("out_ack_p", pkts[ND_OUT][ND_ACK], NDC_U16, 0), NDC("out_other_p", pkts[ND_OUT][ND_OTHER], NDC_U16, 0),
	NDC("out_maxpkt", maxpkt[ND_OUT], NDC_U16, 0),
	NDC("frames", frames, NDC_U16, 0), NDC("frame_sum_ms", frame_sum_ms, NDC_FLT, 0), NDC("frame_max_ms", frame_max_ms, NDC_FLT, 0),
	NDC("starved_frames", starved_frames, NDC_U16, 0), NDC("starved_ms", starved_ms, NDC_FLT, 0),
	NDC("arrivals", arrivals, NDC_U16, 0), NDC("gap_sum_ms", gap_sum_ms, NDC_FLT, 0), NDC("gap_sumsq", gap_sumsq, NDC_FLT, 0),
	NDC("gap_max_ms", gap_max_ms, NDC_FLT, 0),
	NDC("lates", lates, NDC_U16, 0), NDC("late_sum_ms", late_sum_ms, NDC_FLT, NDC_LATE), NDC("late_sumsq", late_sumsq, NDC_FLT, NDC_LATE),
	NDC("late_net_sum_ms", late_net_sum_ms, NDC_FLT, NDC_LATE), NDC("late_net_sumsq", late_net_sumsq, NDC_FLT, NDC_LATE),
	NDC("late_max_ms", late_max_ms, NDC_FLT, NDC_LATE), NDC("late_net_max_ms", late_net_max_ms, NDC_FLT, NDC_LATE),
	NDC("svgap_sum_ms", svgap_sum_ms, NDC_FLT, NDC_LATE),
	NDC("accepted", accepted, NDC_U16, 0), NDC("missing", missing, NDC_U16, 0), NDC("gaps", gaps, NDC_U16, 0),
	NDC("dup", dup, NDC_U16, 0), NDC("stale", stale, NDC_U16, 0), NDC("short", shortp, NDC_U16, 0),
	NDC("resent", resent, NDC_U16, 0), NDC("backlog_max", backlog_max, NDC_U16, 0),
	NDC("svc1", top_svc[0], NDC_U8, 0), NDC("svc1_b", top_svc_bytes[0], NDC_U16, 0),
	NDC("svc2", top_svc[1], NDC_U8, 0), NDC("svc2_b", top_svc_bytes[1], NDC_U16, 0),
	NDC("svc3", top_svc[2], NDC_U8, 0), NDC("svc3_b", top_svc_bytes[2], NDC_U16, 0),
	NDC("svc_total_b", svc_total, NDC_U32, 0),
	NDC("ping_ms", ping_ms, NDC_FLT, NDC_NEG), NDC("cmdrtt_ms", cmdrtt_ms, NDC_FLT, NDC_NEG),
};
#define ND_NCFIELDS ((int)(sizeof(nd_cfields) / sizeof(nd_cfields[0])))

static void ND_CSVRow (FILE *f, const char *set, int n, const nd_bucket_t *b, uint32_t base)
{
	int i;
	fprintf (f, "%s,%d,%.2f", set, n, ((double)b->index - (double)base) * ND_BUCKET_SEC);
	for (i = 0; i < ND_NCFIELDS; i++)
	{
		const nd_cfield_t *fd = &nd_cfields[i];
		const byte_nd *src = (const byte_nd *)b + fd->ofs;
		fputc (',', f);
		switch (fd->type)
		{
		case NDC_U8:	fprintf (f, "%u", *(const uint8_t *)src); break;
		case NDC_U16:	fprintf (f, "%u", *(const uint16_t *)src); break;
		case NDC_U32:	fprintf (f, "%u", *(const uint32_t *)src); break;
		case NDC_FLT:
			{
				float v = *(const float *)src;
				if ((fd->opt == NDC_LATE && !b->lates) || (fd->opt == NDC_NEG && v < 0))
					break;	// none
				fprintf (f, "%.9g", v);
			}
			break;
		}
	}
	fputc ('\n', f);
}

static void ND_CSVHeader (FILE *f)
{
	int i;
	fprintf (f, "set,n,time_s");
	for (i = 0; i < ND_NCFIELDS; i++)
		fprintf (f, ",%s", nd_cfields[i].name);
	fputc ('\n', f);
}

static int ND_CSVSplit (char *line, char **cols, int maxcols)
{
	int n = 0;
	char *p = line;
	while (n < maxcols)
	{
		cols[n++] = p;
		p = strchr (p, ',');
		if (!p)
			break;
		*p++ = 0;
	}
	for (p = cols[n - 1]; *p; p++)
		if (*p == '\n' || *p == '\r')
			*p = 0;
	return n;
}

int ND_ReadCSV (FILE *f, const char *set, int want_n, nd_bucket_t *out, int max)
{
	char line[4096], hdr[4096], *cols[128], *hcols[128];
	int map[128], nh = 0, count = 0, i, j;

	while (fgets (line, sizeof(line), f))
	{
		if (line[0] == '#')
			continue;
		if (!nh)
		{	// header: match columns by name, so older captures (fewer columns) still load
			memcpy (hdr, line, sizeof(hdr));
			nh = ND_CSVSplit (hdr, hcols, 128);
			for (i = 0; i < nh; i++)
			{
				map[i] = -1;
				for (j = 0; j < ND_NCFIELDS; j++)
					if (!strcmp (hcols[i], nd_cfields[j].name))
						map[i] = j;
			}
			continue;
		}
		if (count >= max || ND_CSVSplit (line, cols, 128) != nh || strcmp (cols[0], set) || (want_n && atoi (cols[1]) != want_n))
			continue;
		{
			nd_bucket_t *b = &out[count];
			ND_ClearBucket (b, (uint32_t)count);
			for (i = 0; i < nh; i++)
			{
				const nd_cfield_t *fd;
				byte_nd *dst;
				if (map[i] < 0 || !cols[i][0])
					continue;
				fd = &nd_cfields[map[i]];
				dst = (byte_nd *)b + fd->ofs;
				switch (fd->type)
				{
				case NDC_U8:	*(uint8_t *)dst = (uint8_t)atoi (cols[i]); break;
				case NDC_U16:	*(uint16_t *)dst = (uint16_t)atoi (cols[i]); break;
				case NDC_U32:	*(uint32_t *)dst = (uint32_t)strtoul (cols[i], NULL, 10); break;
				case NDC_FLT:	*(float *)dst = (float)atof (cols[i]); break;
				}
			}
			for (j = 0; j < ND_TOP_SVC; j++)	// ids index 129-entry tables: never trust a file
				if (b->top_svc[j] >= ND_SVC_TYPES)
				{
					b->top_svc[j] = 0;
					b->top_svc_bytes[j] = 0;
				}
			count++;
		}
	}
	return count;
}

void ND_WriteReportCSV (const netdiag_t *nd, FILE *f)
{
	int64_t k, last;
	int i, n;

	fprintf (f, "# QSS-M netgraph buckets v2; 20 ms buckets; bytes include the 8-byte NetQuake header; time_s is relative to the set's start (live) or trigger (hitch); empty = none\n");
	ND_CSVHeader (f);

	if (nd->started && nd->cur)
	{
		uint32_t base;
		last = (int64_t)nd->cur - 1;
		k = last - ND_RING + 2;
		if (k < (int64_t)nd->first)
			k = nd->first;
		base = (uint32_t)k;
		for (; k <= last; k++)
		{
			const nd_bucket_t *b = ND_Retained (nd, k);
			if (b)
				ND_CSVRow (f, "live", 0, b, base);
		}
	}
	for (i = 0, n = 1; i < ND_HITCH_SLOTS; i++)
		if (nd->hitches[i].m.used)
		{
			int j;
			for (j = 0; j < ND_HITCH_BUCKETS; j++)
				ND_CSVRow (f, "hitch", n, &nd->hitches[i].window[j], nd->hitches[i].m.trigger);
			n++;
		}
	for (i = 0, n = 1; i < ND_HITCH_SLOTS; i++)
		if (nd->marks[i].m.used)
		{
			int j;
			for (j = 0; j < ND_HITCH_BUCKETS; j++)
				ND_CSVRow (f, "mark", n, &nd->marks[i].window[j], nd->marks[i].m.trigger);
			n++;
		}
}

#ifndef NETGRAPH_STANDALONE

/*
=============================================================================

2. ENGINE GLUE

=============================================================================
*/

/*
Engine glue and the netgraph command  // woods #netgraph

Reads engine state, timestamps events and feeds the core above. Owns the
`netgraph` command. Every hook in the engine is guarded by netdiag_active (and
for socket hooks, the socket pointer), so with netgraph off nothing here runs.

The hooks only observe: they never change what is sent, when, or whether a
packet is accepted.
*/



int					netdiag_active;		// recording: hooks do work
int					netdiag_alloc;		// collector memory exists (recording, or kept data)
int					netdiag_wantframe;	// frame hook needed
struct qsocket_s	*netdiag_sock;		// the connection being recorded

typedef enum { NDM_OFF, NDM_RECORD, NDM_COMPACT, NDM_DETAILED } nd_mode_t;
static const char *nd_mode_names[] = {"off", "record", "compact", "detailed"};
static const char *nd_anchor_names[] = {"topleft", "topright", "bottomleft", "bottomright"};

static netdiag_t	*nd_live;
static int			nd_mode = NDM_OFF;
static int			nd_anchor = 1;
static struct qsocket_s *nd_session_sock;	// socket the current data belongs to
static qboolean		nd_session_open;		// connected and recording into this session
static qboolean		nd_was_connected;
static qboolean		nd_went_live;			// this session reached gameplay
static int			nd_prev_cond;
static double		nd_backlog_since = -1;	// reliable data waiting for an ACK since, <0 none
static char			nd_info_proto[16], nd_info_ext[64], nd_info_map[64], nd_info_host[64];	// kept for reports written after disconnect

// dev scheduling
static double		nd_autostatus_every;	// 0 = off
static double		nd_autostatus_next;
static qboolean		nd_autoreport;
static double		nd_stall_ms;			// 0 = none pending
static double		nd_stall_after;			// <0 = immediately, else seconds after first live frame
static double		nd_stall_at = -1;		// absolute time, <0 not armed yet
static double		nd_live_since = -1;		// first live frame of this session, <0 not yet
static qboolean		nd_ignorefocus;			// test harness windows sit in the background
static double		nd_disconnect_after;	// seconds after going live, 0 = never
static qboolean		nd_replay;				// showing loaded buckets; recording suspended

#define ND_MAX_AFTER 32
typedef struct { double delay, at; char cmd[128]; } nd_after_t;
static nd_after_t	nd_after[ND_MAX_AFTER];	// _netdiag_after: commands run N s after entering the game
static int			nd_after_count;

extern const char	*svc_strings[128];
extern qboolean		scr_disabled_for_loading;
extern cvar_t		net_messagetimeout;

/*
=============
Lifecycle
=============
*/
static void NetDiag_UpdateActive (void);

static qboolean NetDiag_Alloc (void)
{
	if (!nd_live)
	{
		nd_live = (netdiag_t *) calloc (1, sizeof(*nd_live));
		if (!nd_live)
		{
			Con_Printf ("netgraph: out of memory\n");
			return false;
		}
		ND_Reset (nd_live);
	}
	netdiag_alloc = 1;
	NetDiag_UpdateActive ();
	return true;
}

static void NetDiag_Free (void)
{
	free (nd_live);
	nd_live = NULL;
	netdiag_alloc = 0;
	netdiag_active = 0;
	netdiag_sock = NULL;
	nd_session_sock = NULL;
	nd_session_open = false;
	nd_replay = false;
	NetDiag_UpdateActive ();
}

static void NetDiag_UpdateActive (void)
{
	netdiag_active = (nd_mode != NDM_OFF && nd_live && !nd_replay) ? 1 : 0;
	if (!netdiag_active)
		netdiag_sock = NULL;
	netdiag_wantframe = netdiag_alloc || nd_after_count > 0;
}

void NetDiag_SockClosed (struct qsocket_s *sock)
{
	if (sock == netdiag_sock)
		netdiag_sock = NULL;
}

/*
=============
Info for text output
=============
*/
static void NetDiag_FillInfo (nd_info_t *info, char *proto, size_t protosize, char *ext, size_t extsize, char *host, size_t hostsize)
{
	qboolean connected = cls.state == ca_connected;
	int cansend = 0, backlog = 0;
	double lastmsg = -1;

	memset (info, 0, sizeof(*info));
	info->engine = ENGINE_NAME_AND_VER;
	info->recording = netdiag_active;
	info->connected = connected;

	if (connected && !cls.demoplayback)
		q_snprintf (proto, protosize, "%i", cl.protocol);
	else
		q_strlcpy (proto, "--", protosize);
	info->protocol = proto;

	ext[0] = 0;
	if (connected)
	{
		if (cl.protocol == PROTOCOL_VERSION_DP7)
			q_strlcat (ext, "dp7,", extsize);
		if (cl.protocol_pext1)
			q_strlcat (ext, "fte1,", extsize);
		if (cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
			q_strlcat (ext, "deltas,", extsize);
		if (cl.protocol_pext2 & PEXT2_PREDINFO)
			q_strlcat (ext, "predinfo,", extsize);
		if (*ext)
			ext[strlen (ext) - 1] = 0;
	}
	info->extensions = ext;

	info->map = connected ? cl.mapname : "";
	host[0] = 0;
	if (connected && cl.serverinfo[0])
		Info_GetKey (cl.serverinfo, "hostname", host, hostsize);
	info->hostname = host;

	info->silence_s = -1;
	info->timeout_left_s = -1;
	if (connected && !cls.demoplayback && cls.netcon &&
		NET_QSocketDiag (cls.netcon, &cansend, &backlog, &lastmsg) == NETDIAG_KIND_DATAGRAM)
	{
		if (lastmsg > 0)	// the connection's own timeout runs on any message
			info->timeout_left_s = net_messagetimeout.value - (net_time - lastmsg);
		info->silence_s = NetDiag_Silence ();	// gameplay silence runs on snapshots only
		info->backlog_now = cansend ? 0 : backlog;
	}
	info->movemsg_gap = connected ? q_max (0, cl.movemessages - cl.ackedmovemessages) : 0;
}

// snapshot connection details while connected, so a report written after the
// disconnect still says where it happened
static void NetDiag_CacheInfo (void)
{
	nd_info_t info;
	NetDiag_FillInfo (&info, nd_info_proto, sizeof(nd_info_proto), nd_info_ext, sizeof(nd_info_ext), nd_info_host, sizeof(nd_info_host));
	q_strlcpy (nd_info_map, cl.mapname, sizeof(nd_info_map));
}

static void NetDiag_ReportInfo (nd_info_t *info, char *proto, size_t protosize, char *ext, size_t extsize, char *host, size_t hostsize)
{
	NetDiag_FillInfo (info, proto, protosize, ext, extsize, host, hostsize);
	if (cls.state != ca_connected && nd_info_proto[0])
	{
		info->protocol = nd_info_proto;
		info->extensions = nd_info_ext;
		info->map = nd_info_map;
		info->hostname = nd_info_host;
	}
}

static void NetDiag_StatusLine (char *buf, size_t size)
{
	nd_info_t info;
	char proto[16], ext[64], host[64];
	NetDiag_FillInfo (&info, proto, sizeof(proto), ext, sizeof(ext), host, sizeof(host));
	if (!nd_live)
	{
		q_snprintf (buf, size, "netgraph: v=1 state=off");
		return;
	}
	ND_FormatStatus (nd_live, &info, Sys_DoubleTime (), buf, size);
}

/*
=============
Reports
=============
*/
static FILE *NetDiag_OpenReport (const char *ext, char *path, size_t pathsize)
{
	char stamp[32];
	time_t t = time (NULL);
	struct tm *lt = localtime (&t);
	int n;
	FILE *f;

	if (lt)
		strftime (stamp, sizeof(stamp), "%Y%m%d-%H%M%S", lt);
	else
		q_strlcpy (stamp, "unknown", sizeof(stamp));

	for (n = 1; n < 100; n++)
	{
		if (n == 1)
			q_snprintf (path, pathsize, "%s/netgraph-%s.%s", com_gamedir, stamp, ext);
		else
			q_snprintf (path, pathsize, "%s/netgraph-%s-%d.%s", com_gamedir, stamp, n, ext);
		f = fopen (path, "rb");
		if (f)
		{
			fclose (f);
			continue;
		}
		return fopen (path, "wb");
	}
	return NULL;
}

static void NetDiag_WriteReport (const char *kind)
{
	nd_info_t info;
	char proto[16], ext[64], host[64], path[MAX_OSPATH];
	double now = Sys_DoubleTime ();
	FILE *f;

	if (!nd_live)
	{
		Con_Printf ("netgraph: nothing recorded. Turn it on first: netgraph record\n");
		return;
	}
	NetDiag_ReportInfo (&info, proto, sizeof(proto), ext, sizeof(ext), host, sizeof(host));

	f = NetDiag_OpenReport (kind, path, sizeof(path));
	if (!f)
	{
		Con_Printf ("netgraph: couldn't write %s\n", path);
		return;
	}
	if (!strcmp (kind, "json"))
		ND_WriteReportJSON (nd_live, &info, now, f);
	else if (!strcmp (kind, "csv"))
		ND_WriteReportCSV (nd_live, f);
	else
		ND_WriteReportText (nd_live, &info, now, f);
	fclose (f);
	Con_Printf ("netgraph: wrote %s\n", path);
}

/*
=============
Session ownership
=============
*/
static void NetDiag_EndSession (void)
{
	if (!nd_session_open)
		return;
	nd_session_open = false;	// before writing: printing can refresh the screen and re-enter
	if (nd_live)
		ND_Finish (nd_live);			// a hitch caught in the last 4 s is kept, with its missing context marked
	if (nd_live && nd_autoreport)
	{
		NetDiag_WriteReport ("json");
		NetDiag_WriteReport ("csv");
		NetDiag_WriteReport ("txt");
	}
}

static void NetDiag_BeginSession (void)
{
	if (!nd_live)
		return;
	ND_Reset (nd_live);
	nd_session_sock = cls.netcon;
	nd_session_open = true;
	nd_went_live = false;
	nd_live_since = -1;
	nd_prev_cond = ND_COND_LOADING;
	nd_info_proto[0] = 0;
	if (nd_stall_ms > 0 && nd_stall_after >= 0)
		nd_stall_at = -1;	// re-arm relative to this session's first live frame
}

/*
=============
Per-frame hook: after each presented frame
=============
*/
static int NetDiag_Source (void)
{
	int cansend, backlog;
	double lastmsg;

	if (cls.state != ca_connected)
		return ND_SRC_NONE;
	if (cls.demoplayback)
		return ND_SRC_DEMO;
	if (!cls.netcon)
		return ND_SRC_NONE;
	switch (NET_QSocketDiag (cls.netcon, &cansend, &backlog, &lastmsg))
	{
	case NETDIAG_KIND_LOOP:		return ND_SRC_LOCAL;
	case NETDIAG_KIND_DATAGRAM:	return ND_SRC_NET;
	default:					return ND_SRC_OTHER;
	}
}

static int NetDiag_Cond (void)
{
	int cond = 0;
	if (cls.signon != SIGNONS || scr_disabled_for_loading)
		cond |= ND_COND_LOADING;
	if (cl.paused || cl.match_pause_time)
		cond |= ND_COND_PAUSED;
	if (!nd_ignorefocus && (!VID_HasInputFocus () || VID_IsMinimized ()))
		cond |= ND_COND_UNFOCUSED;
	return cond;
}

static void NetDiag_DevFrame (double now, int source, int cond)
{
	if (nd_stall_ms > 0)
	{
		if (nd_stall_after < 0)
			nd_stall_at = now;
		else if (nd_stall_at < 0 && nd_live_since >= 0)
			nd_stall_at = nd_live_since + nd_stall_after;
		if (nd_stall_at >= 0 && now >= nd_stall_at)
		{
			double end = Sys_DoubleTime () + nd_stall_ms / 1000.0;
			Con_Printf ("netgraph: stalling %.0f ms\n", nd_stall_ms);
			nd_stall_ms = 0;
			while (Sys_DoubleTime () < end)
				;	// busy-wait: Sys_Sleep may oversleep and blur the expected stall length
		}
	}

	if (nd_disconnect_after > 0 && nd_live_since >= 0 && now >= nd_live_since + nd_disconnect_after)
	{	// harness sessions end themselves: a pure client can't be rcon'd
		nd_disconnect_after = 0;
		Cbuf_AddText ("disconnect\n");
	}

	if (nd_autostatus_every > 0 && now >= nd_autostatus_next)
	{
		char line[1024];
		nd_autostatus_next = now + nd_autostatus_every;
		NetDiag_StatusLine (line, sizeof(line));
		Con_Printf ("%s\n", line);
	}
	(void)source;
	(void)cond;
}

void NetDiag_Frame (void)
{
	static qboolean in_frame;	// console prints can force a nested screen update
	double now;
	int source, cond, cansend = 0, backlog = 0;
	double lastmsg;
	qboolean connected = cls.state == ca_connected;
	qboolean was_connected = nd_was_connected;

	if (in_frame)
		return;
	in_frame = true;
	now = Sys_DoubleTime ();
	nd_was_connected = connected;

	if (nd_after_count)
	{	// _netdiag_after: the clock starts when we're in the game
		int i;
		qboolean ingame = connected && cls.signon == SIGNONS;
		for (i = 0; i < nd_after_count; )
		{
			nd_after_t *e = &nd_after[i];
			if (e->at < 0 && ingame)
				e->at = now + e->delay;
			if (e->at >= 0 && now >= e->at)
			{
				Cbuf_AddText (e->cmd);
				Cbuf_AddText ("\n");
				nd_after[i] = nd_after[--nd_after_count];
				continue;
			}
			i++;
		}
		NetDiag_UpdateActive ();
	}

	if (!connected && was_connected)
	{	// disconnected: close the session; with recording off, the kept data goes too
		NetDiag_EndSession ();
		if (nd_mode == NDM_OFF && !nd_replay)
			NetDiag_Free ();
	}

	if (!nd_live)
	{
		in_frame = false;
		return;
	}

	if (netdiag_active && connected && (!nd_session_open || cls.netcon != nd_session_sock))
	{
		if (nd_session_open)
			NetDiag_EndSession ();
		NetDiag_BeginSession ();
	}

	if (netdiag_active && connected)
	{
		source = NetDiag_Source ();
		cond = NetDiag_Cond ();
		netdiag_sock = (source == ND_SRC_NET) ? cls.netcon : NULL;

		ND_SetSource (nd_live, source);
		ND_Frame (nd_live, now, cond);

		if (source == ND_SRC_NET &&
			NET_QSocketDiag (cls.netcon, &cansend, &backlog, &lastmsg) == NETDIAG_KIND_DATAGRAM &&
			!cansend)
		{	// every reliable waits one round trip; only a backlog that outlasts that is news
			if (nd_backlog_since < 0)
				nd_backlog_since = now;
			if (now - nd_backlog_since >= 0.25)
				ND_Backlog (nd_live, backlog);
		}
		else
			nd_backlog_since = -1;

		if (!cond && !nd_went_live && source != ND_SRC_NONE)
		{
			nd_went_live = true;
			nd_live_since = now;
		}
		if (!(cond & ND_COND_LOADING) && (nd_prev_cond & ND_COND_LOADING))
			NetDiag_CacheInfo ();	// joined, or a map load finished
		nd_prev_cond = cond;
		NetDiag_DevFrame (now, source, cond);
	}
	else if (netdiag_active)
	{
		netdiag_sock = NULL;
		NetDiag_DevFrame (now, ND_SRC_NONE, 0);
	}
	in_frame = false;
}

/*
=============
Network hooks (callers check NETDIAG_SOCK first)
=============
*/
void NetDiag_Sent (int cls_, int len, int ret)
{
	double now = Sys_DoubleTime ();
	if (ret == len)
		ND_Packet (nd_live, now, ND_OUT, cls_, len);
	else
		ND_WriteFail (nd_live, now);	// 0 = transient error or short write, -1 = error
}

void NetDiag_Resent (void)
{
	ND_Resent (nd_live, Sys_DoubleTime ());
}

void NetDiag_Received (int cls_, int len)
{
	ND_Packet (nd_live, Sys_DoubleTime (), ND_IN, cls_, len);
}

void NetDiag_Arrival (int missing)
{
	ND_Arrival (nd_live, Sys_DoubleTime (), missing);
}

void NetDiag_Dup (void)
{
	ND_Dup (nd_live, Sys_DoubleTime ());
}

void NetDiag_Stale (void)
{
	ND_Stale (nd_live, Sys_DoubleTime ());
}

void NetDiag_Short (void)
{
	ND_Short (nd_live, Sys_DoubleTime ());
}

/*
=============
Client hooks (callers check netdiag_active first)
=============
*/
void NetDiag_ReadPass (void)
{
	ND_ReadPass (nd_live, Sys_DoubleTime ());
}

void NetDiag_ServerTime (double svtime)
{
	ND_ServerTime (nd_live, svtime);
}

void NetDiag_Svc (int cmd, int pos)
{
	if (cmd < 0)
	{
		ND_Svc (nd_live, -1, pos);
		ND_MsgSize (nd_live, net_message.cursize);
		ND_MessageDone (nd_live);
	}
	else
		ND_Svc (nd_live, (cmd & U_SIGNAL) ? ND_SVC_FAST : (cmd & 127), pos);
}

void NetDiag_Starved (void)
{
	if (nd_live->source == ND_SRC_NET || nd_live->source == ND_SRC_OTHER)
		ND_Starved (nd_live);
}

void NetDiag_Ping (int slot, int ping, int moveloss)
{
	if (slot != cl.realviewentity - 1 || cls.demoplayback ||
		(nd_live->source != ND_SRC_NET && nd_live->source != ND_SRC_OTHER))
		return;	// a listen-server host's own "ping" isn't a network measurement
	ND_Ping (nd_live, Sys_DoubleTime (), (float)ping, moveloss);
}

// command round trips only mean something over a network transport (loopback acks are instant)
static qboolean NetDiag_RttMeasured (void)
{
	return nd_live->source == ND_SRC_NET || nd_live->source == ND_SRC_OTHER;
}

void NetDiag_CmdSent (int seq)
{
	if (NetDiag_RttMeasured ())
		ND_CmdSent (nd_live, seq, Sys_DoubleTime ());
}

void NetDiag_CmdAck (int seq)
{
	if (NetDiag_RttMeasured ())
		ND_CmdAck (nd_live, seq, Sys_DoubleTime ());
}

void NetDiag_CmdAck16 (int ack16)
{	// same reconstruction CLFTE_ParseEntitiesUpdate uses for its 16-bit ack
	int seq = (cl.movemessages & 0xffff0000) | (ack16 & 0xffff);
	if (seq > cl.movemessages)
		seq -= 0x10000;
	NetDiag_CmdAck (seq);
}

/*
=============
Command
=============
*/
static void NetDiag_SetMode (int mode)
{
	int was_active = netdiag_active;
	if (mode != NDM_OFF && !NetDiag_Alloc ())
		return;
	nd_mode = mode;
	NetDiag_UpdateActive ();
	if (netdiag_active && !was_active && nd_live)
		ND_Break (nd_live, Sys_DoubleTime ());	// resuming: the time spent off is unmeasured, not a frame or zeros
	if (mode == NDM_OFF && nd_live && cls.state != ca_connected)
		NetDiag_Free ();
}

static void NetDiag_Help (void)
{
	Con_Printf ("netgraph                  cycle off / compact / detailed\n");
	Con_Printf ("netgraph off|compact|detailed\n");
	Con_Printf ("netgraph topleft|topright|bottomleft|bottomright\n");
	Con_Printf ("netgraph record           record without drawing\n");
	Con_Printf ("netgraph status           one-line summary\n");
	Con_Printf ("netgraph mark             save this moment (6s before, 4s after)\n");
	Con_Printf ("netgraph report [csv|json] write a report to the game folder\n");
	Con_Printf ("netgraph inspect          look through live data, saved hitches and marks\n");
	Con_Printf ("Nothing is saved; put e.g. \"netgraph compact topleft\" in autoexec.cfg.\n");
	Con_Printf ("currently: %s, %s\n", nd_mode_names[nd_mode], nd_anchor_names[nd_anchor]);
}

static void NetDiag_PanelNotYet (int mode)
{
	Con_Printf ("netgraph %s\n", nd_mode_names[mode]);
}

static void NetDiag_f (void)
{
	int i, argc = Cmd_Argc ();

	if (argc == 1)
	{
		int next = (nd_mode == NDM_COMPACT) ? NDM_DETAILED : (nd_mode == NDM_DETAILED ? NDM_OFF : NDM_COMPACT);
		NetDiag_SetMode (next);
		if (next == NDM_OFF)
			Con_Printf ("netgraph off\n");
		else
			NetDiag_PanelNotYet (next);
		return;
	}

	for (i = 1; i < argc; i++)
	{
		const char *a = Cmd_Argv (i);
		int k;

		if (!q_strcasecmp (a, "help") || !strcmp (a, "?"))
		{
			NetDiag_Help ();
			continue;
		}
		if (!q_strcasecmp (a, "off") || !q_strcasecmp (a, "record") || !q_strcasecmp (a, "compact") || !q_strcasecmp (a, "detailed"))
		{
			for (k = 0; k < 4; k++)
				if (!q_strcasecmp (a, nd_mode_names[k]))
					break;
			NetDiag_SetMode (k);
			if (k == NDM_COMPACT || k == NDM_DETAILED)
				NetDiag_PanelNotYet (k);
			else
				Con_Printf ("netgraph %s\n", nd_mode_names[k]);
			continue;
		}
		for (k = 0; k < 4; k++)
			if (!q_strcasecmp (a, nd_anchor_names[k]))
				break;
		if (k < 4)
		{
			nd_anchor = k;
			continue;
		}
		if (!q_strcasecmp (a, "status"))
		{
			char line[1024];
			NetDiag_StatusLine (line, sizeof(line));
			Con_Printf ("%s\n", line);
			continue;
		}
		if (!q_strcasecmp (a, "mark"))
		{
			if (!netdiag_active)
				Con_Printf ("netgraph: not recording; turn it on first\n");
			else
			{
				ND_Mark (nd_live, Sys_DoubleTime ());
				Con_Printf ("netgraph: marked; saved in 4 seconds\n");
			}
			continue;
		}
		if (!q_strcasecmp (a, "report"))
		{
			const char *kind = "txt";
			if (i + 1 < argc && (!q_strcasecmp (Cmd_Argv (i + 1), "csv") || !q_strcasecmp (Cmd_Argv (i + 1), "json")))
				kind = !q_strcasecmp (Cmd_Argv (++i), "csv") ? "csv" : "json";
			NetDiag_WriteReport (kind);
			continue;
		}
		if (!q_strcasecmp (a, "inspect"))
		{
			M_Menu_NetGraph_f (false);
			continue;
		}
		Con_Printf ("netgraph: unknown option \"%s\" (netgraph help)\n", a);
	}
}

static qboolean NetDiag_DevAllowed (void)
{
	if (developer.value)
		return true;
	Con_Printf ("requires developer 1\n");
	return false;
}

static void NetDiag_Stall_f (void)
{
	if (!NetDiag_DevAllowed ())
		return;
	if (Cmd_Argc () < 2)
	{
		Con_Printf ("_netdiag_stall <ms> [seconds after the session goes live]\n");
		return;
	}
	nd_stall_ms = q_max (0.0, atof (Cmd_Argv (1)));
	nd_stall_after = Cmd_Argc () > 2 ? q_max (0.0, atof (Cmd_Argv (2))) : -1;
	nd_stall_at = -1;
}

static void NetDiag_AutoStatus_f (void)
{
	if (!NetDiag_DevAllowed ())
		return;
	nd_autostatus_every = Cmd_Argc () > 1 ? q_max (0.0, atof (Cmd_Argv (1))) : 0;
	nd_autostatus_next = 0;
}

static void NetDiag_AutoReport_f (void)
{
	if (!NetDiag_DevAllowed ())
		return;
	nd_autoreport = Cmd_Argc () > 1 ? atoi (Cmd_Argv (1)) != 0 : true;
}

static void NetDiag_IgnoreFocus_f (void)
{
	if (!NetDiag_DevAllowed ())
		return;
	nd_ignorefocus = Cmd_Argc () > 1 ? atoi (Cmd_Argv (1)) != 0 : true;
}

static void NetDiag_DisconnectAfter_f (void)
{
	if (!NetDiag_DevAllowed ())
		return;
	nd_disconnect_after = Cmd_Argc () > 1 ? q_max (0.0, atof (Cmd_Argv (1))) : 0;
}

/*
=============
Panel accessors
=============
*/
// whatever is retained -- Inspect and reports keep working after a disconnect
const struct netdiag_s *NetDiag_Data (void)
{
	return nd_live;
}

int NetDiag_Live (void)
{
	return nd_live && (nd_replay || cls.state == ca_connected);
}

int NetDiag_PanelMode (void)
{
	return nd_mode == NDM_COMPACT ? 1 : nd_mode == NDM_DETAILED ? 2 : 0;
}

int NetDiag_PanelAnchor (void)
{
	return nd_anchor;
}

// time since the last snapshot -- not the last message: chat and other reliables
// keep a connection alive while gameplay updates have stopped
float NetDiag_Silence (void)
{
	if (nd_replay || !nd_live || !netdiag_active || nd_live->source != ND_SRC_NET || nd_live->last_update_time < 0 ||
		(nd_live->cond & (ND_COND_LOADING | ND_COND_PAUSED)))
		return -1;
	return (float)(Sys_DoubleTime () - nd_live->last_update_time);
}

const char *NetDiag_StateWord (void)
{
	static char word[16];
	nd_info_t info;
	char proto[16], ext[64], host[64];
	const char *s;
	int i;

	if (nd_replay)
		return "REPLAY";
	if (!nd_live)
		return "OFF";
	NetDiag_FillInfo (&info, proto, sizeof(proto), ext, sizeof(ext), host, sizeof(host));
	s = ND_StateName (nd_live, &info);
	for (i = 0; s[i] && i < (int)sizeof(word) - 1; i++)
		word[i] = (s[i] >= 'a' && s[i] <= 'z') ? (char)(s[i] - 'a' + 'A') : s[i];
	word[i] = 0;
	return word;
}

/*
=============
Replay: load a saved CSV so the panel can be reviewed without a live session
=============
*/
static void NetDiag_Replay_f (void)
{
	char path[MAX_OSPATH];
	const char *set = "live";
	int want_n = 0, count;
	nd_bucket_t *buckets;
	FILE *f;

	if (!NetDiag_DevAllowed ())
		return;
	if (Cmd_Argc () < 2)
	{
		Con_Printf ("_netdiag_replay <file.csv> [hitch N | mark N] | _netdiag_replay off\n");
		return;
	}
	if (!q_strcasecmp (Cmd_Argv (1), "off"))
	{
		if (nd_replay && nd_live)
			ND_Reset (nd_live);
		nd_replay = false;
		NetDiag_UpdateActive ();
		return;
	}
	if (Cmd_Argc () > 3)
	{
		set = Cmd_Argv (2);
		want_n = atoi (Cmd_Argv (3));
	}

	q_strlcpy (path, Cmd_Argv (1), sizeof(path));
	f = fopen (path, "rb");
	if (!f)
	{
		q_snprintf (path, sizeof(path), "%s/%s", com_gamedir, Cmd_Argv (1));
		f = fopen (path, "rb");
	}
	if (!f)
	{
		Con_Printf ("_netdiag_replay: can't open %s\n", Cmd_Argv (1));
		return;
	}
	buckets = (nd_bucket_t *) calloc (ND_RING, sizeof(*buckets));
	if (!buckets || !NetDiag_Alloc ())
	{
		free (buckets);
		fclose (f);
		return;
	}
	count = ND_ReadCSV (f, set, want_n, buckets, ND_RING);	// the same field table the export used
	fclose (f);
	if (!count)
	{
		Con_Printf ("_netdiag_replay: no \"%s\" rows in %s\n", set, path);
		free (buckets);
		return;
	}
	ND_LoadBuckets (nd_live, buckets, count);
	free (buckets);
	nd_replay = true;
	NetDiag_UpdateActive ();
	Con_Printf ("netgraph: replaying %d buckets (%.1f s) from %s\n", count, count * ND_BUCKET_SEC, path);
}

static void NetDiag_After_f (void)
{
	nd_after_t *e;
	int i;
	if (!NetDiag_DevAllowed ())
		return;
	if (Cmd_Argc () < 3)
	{
		Con_Printf ("_netdiag_after <seconds in game> <command ...>\n");
		return;
	}
	if (nd_after_count >= ND_MAX_AFTER)
	{
		Con_Printf ("_netdiag_after: queue full (%d), dropped \"%s\"\n", ND_MAX_AFTER, Cmd_Args ());
		return;
	}
	e = &nd_after[nd_after_count++];
	e->delay = q_max (0.0, atof (Cmd_Argv (1)));
	e->at = -1;
	e->cmd[0] = 0;
	for (i = 2; i < Cmd_Argc (); i++)
	{
		if (i > 2)
			q_strlcat (e->cmd, " ", sizeof(e->cmd));
		q_strlcat (e->cmd, Cmd_Argv (i), sizeof(e->cmd));
	}
	NetDiag_UpdateActive ();
}

void NetDiag_Init (void)
{
	int i;
	for (i = 0; i < 128; i++)
	{	// some engine names carry their number ("86 svc_updateentities_fte"); drop it
		const char *n = svc_strings[i];
		while (n && *n >= '0' && *n <= '9')
			n++;
		if (n && *n == ' ' && n != svc_strings[i])
			n++;
		else
			n = svc_strings[i];
		nd_svc_names[i] = n;
	}
	nd_svc_names[ND_SVC_FAST] = "entity updates";

	Cmd_AddCommand ("netgraph", NetDiag_f);
	Cmd_AddCommand ("_netdiag_stall", NetDiag_Stall_f);
	Cmd_AddCommand ("_netdiag_autostatus", NetDiag_AutoStatus_f);
	Cmd_AddCommand ("_netdiag_autoreport", NetDiag_AutoReport_f);
	Cmd_AddCommand ("_netdiag_ignorefocus", NetDiag_IgnoreFocus_f);
	Cmd_AddCommand ("_netdiag_disconnect_after", NetDiag_DisconnectAfter_f);
	Cmd_AddCommand ("_netdiag_replay", NetDiag_Replay_f);
	Cmd_AddCommand ("_netdiag_after", NetDiag_After_f);
}

/*
=============================================================================

3. PANELS AND INSPECT

=============================================================================
*/

/*
Panels and the Inspect page  // woods #netgraph

One lane renderer draws any "view" of 20 ms buckets -- the live ring, a frozen
copy, or a saved hitch -- into Compact (2 lanes, 5 s), Detailed (4 lanes,
10 s) and Inspect (4 lanes, the whole view, with a cursor).

Lanes: PING (command round trip as an area, server ping reports as dots),
LATE (updates later than the server's own clock says they should be), FRAME
(time between presented frames, peak with a dotted mean) and IN/OUT (bytes per
second, unreliable / reliable / ack, mirrored around one centre line).

Visual rules: text only in the conchars banks, and geometry only in the three
colours QSS-M samples from whatever conchars is loaded (the 0 glyph of the
white, red and gold banks), so the graph always matches the font. Each colour
means one thing: white is data, red is a problem (past a hitch threshold, loss,
starvation, silence), gold is what you are pointing at (the Inspect cursor).
Nothing animates except data moving left.
*/


// conchars colour indices for Draw_GetConcharsCursorColorByIndex
#define NG_WHITE		0
#define NG_RED			1
#define NG_GOLD			2
#define NG_PAL_BACK		0	// backdrop: original palette black

#define NG_PAD			4
#define NG_MARGIN		8
#define NG_BACK_PAD		6		// backdrop reaches this far past the content box
#define NG_BACK_RADIUS	8.f
#define NG_LABEL_W		48		// "FRAME "
#define NG_TEXT_EVERY	0.25	// numbers refresh 4x a second; lanes every frame
#define NG_MAXCOLS		1024
#define NG_BATCH		2048	// quads per colour before an early flush

extern cvar_t scr_viewsize, scr_menuscale;
void M_Menu_HUD_f (void);

/*
=============================================================================
Geometry batches
=============================================================================
*/
typedef struct
{
	float	v[NG_BATCH * 8];
	int		n;
	int		colour;		// NG_WHITE / NG_RED / NG_GOLD
	float	alpha;
} ng_batch_t;

enum { NGB_GRID, NGB_AREA, NGB_DIM, NGB_TRACE, NGB_MARK, NGB_GOLD, NGB_COUNT };
static ng_batch_t ng_b[NGB_COUNT] = {
	{ {0}, 0, NG_WHITE, 0.22f },	// guides and baselines
	{ {0}, 0, NG_WHITE, 0.25f },	// value areas
	{ {0}, 0, NG_WHITE, 0.5f },		// secondary marks: means, reliable traffic, backlog
	{ {0}, 0, NG_WHITE, 0.95f },	// value edges
	{ {0}, 0, NG_RED, 1.f },		// problems
	{ {0}, 0, NG_GOLD, 1.f },		// what you're pointing at
};

static void NG_FlushOne (ng_batch_t *b)
{
	if (b->n)
	{
		plcolour_t c = Draw_GetConcharsCursorColorByIndex (b->colour);	// sampled from the loaded conchars
		glColor4f (c.rgb[0] / 255.f, c.rgb[1] / 255.f, c.rgb[2] / 255.f, b->alpha * gl_menu_alpha);
		glVertexPointer (2, GL_FLOAT, 0, b->v);
		glDrawArrays (GL_QUADS, 0, b->n * 4);
	}
	b->n = 0;
}

static void NG_BeginGeometry (void)
{
	// the only 2D code that draws from client arrays: make sure no shader or generic attribute
	// array (attribute 0 can alias the vertex position on some drivers) is left from 3D
	if (GL_UseProgramFunc)
		GL_UseProgramFunc (0);
	if (GL_DisableVertexAttribArrayFunc)
		GL_DisableVertexAttribArrayFunc (0);
	GL_BindBuffer (GL_ARRAY_BUFFER, 0);
	glDisable (GL_TEXTURE_2D);
	glEnable (GL_BLEND);
	glDisable (GL_ALPHA_TEST);
	glEnableClientState (GL_VERTEX_ARRAY);
}

static void NG_EndGeometry (void)
{
	int i;
	for (i = 0; i < NGB_COUNT; i++)
		NG_FlushOne (&ng_b[i]);
	glDisableClientState (GL_VERTEX_ARRAY);
	glEnable (GL_TEXTURE_2D);
	if (gl_menu_alpha < 1.0f)
		glColor4f (1, 1, 1, gl_menu_alpha);
	else
	{
		glColor3f (1, 1, 1);
		glDisable (GL_BLEND);
		glEnable (GL_ALPHA_TEST);
	}
}

static void NG_Quad (int batch, float x0, float y0, float x1, float y1)
{
	ng_batch_t *b = &ng_b[batch];
	float *v;
	if (x1 <= x0 || y1 <= y0)
		return;
	if (b->n >= NG_BATCH)
		NG_FlushOne (b);
	v = &b->v[b->n++ * 8];
	v[0] = x0; v[1] = y0;
	v[2] = x1; v[3] = y0;
	v[4] = x1; v[5] = y1;
	v[6] = x0; v[7] = y1;
}

static void NG_Backdrop (int x, int y, int w, int h, float alpha)
{
	const byte *p = (const byte *)&d_8to24table[NG_PAL_BACK];
	plcolour_t c;
	c.type = 2;
	c.rgb[0] = p[0];
	c.rgb[1] = p[1];
	c.rgb[2] = p[2];
	Draw_Fill_Plus_Radius (x - NG_BACK_PAD, y - NG_BACK_PAD, w + 2 * NG_BACK_PAD, h + 2 * NG_BACK_PAD,
		c, alpha, true, DRAW_CORNERS_ALL, NG_BACK_RADIUS);
}

static void NG_DrawRight (int right, int y, const char *s, qboolean alt)
{
	int x = right - (int)strlen (s) * 8;
	if (alt)
		Draw_StringMasked (x, y, s);
	else
		Draw_String (x, y, s);
}

// Quake menu idiom: {labels} in the red bank, values in white
static void NG_DrawMarked (int x, int y, const char *s)
{
	char out[160];
	int n = 0, alt = 0;
	for (; *s && n < (int)sizeof(out) - 1; s++)
	{
		if (*s == '{' || *s == '}')
		{
			alt = *s == '{';
			continue;
		}
		if (*s == '~')	// non-breaking space
		{
			out[n++] = ' ';
			continue;
		}
		out[n++] = (char)(alt && *s != ' ' ? (*s | 128) : *s);
	}
	out[n] = 0;
	Draw_String (x, y, out);
}

// word-wrap marked text to `cols` visible characters; returns lines used (draws when draw is set)
static int NG_DrawWrapped (int x, int y, const char *s, int cols, int line_h, qboolean draw)
{
	static char cont[512];
	char line[160];
	int lines = 0;
	while (*s)
	{
		int vis = 0, n = 0, cut = -1, cutvis = 0, alt = 0, i;
		const char *p = s;
		while (*p && n < (int)sizeof(line) - 2)
		{
			if (*p == '{' || *p == '}')
			{
				line[n++] = *p++;
				continue;
			}
			if (vis >= cols)
				break;
			if (*p == ' ')
			{
				cut = n;
				cutvis = vis;
			}
			line[n++] = *p++;
			vis++;
		}
		if (*p && cut > 0 && cutvis > cols / 3)
		{	// break at the last space
			p = s + cut;
			n = cut;
		}
		if (p == s && *p)
		{	// no room for even one character: take it anyway so the loop always ends
			line[0] = *p++;
			n = 1;
		}
		line[n] = 0;
		for (i = 0; i < n; i++)
			if (line[i] == '{')
				alt = 1;
			else if (line[i] == '}')
				alt = 0;
		if (draw)
			NG_DrawMarked (x, y + lines * line_h, line);
		lines++;
		while (*p == ' ')
			p++;
		if (alt && *p)
		{	// keep the label colour across the break
			char tmp[512];
			q_snprintf (tmp, sizeof(tmp), "{%s", p);
			q_strlcpy (cont, tmp, sizeof(cont));
			p = cont;
		}
		s = p;
	}
	return lines ? lines : 1;
}

// "svc_updateentities_fte" -> "updateentities": shorter, still the engine's own name
static const char *NG_ShortSvc (int id)
{
	static char buf[4][40];
	static int which;
	const char *n = nd_svc_names[id];
	char *o;
	size_t len;
	which = (which + 1) & 3;
	o = buf[which];
	if (!n)
	{
		q_snprintf (o, 40, "#%d", id);
		return o;
	}
	if (!strncmp (n, "svcfte_", 7) || !strncmp (n, "svcdp_", 6) || !strncmp (n, "svcqw_", 6))
		n = strchr (n, '_') + 1;
	else if (!strncmp (n, "svc_", 4))
		n += 4;
	q_strlcpy (o, n, 40);
	len = strlen (o);
	if (len > 4 && !strcmp (o + len - 4, "_fte"))
		o[len - 4] = 0;
	else if (len > 3 && !strcmp (o + len - 3, "_dp"))
		o[len - 3] = 0;
	{	// "updatestatbyte/qe_seq": the first name is enough
		char *slash = strchr (o, '/');
		if (slash)
			*slash = 0;
	}
	return o;
}

static void NG_DrawChoice (int x, int y, const char *s, qboolean selected)
{
	if (selected)
		Draw_StringMasked (x, y, s);
	else
		Draw_String (x, y, s);
}

/*
=============================================================================
Views and columns
=============================================================================
*/
typedef struct
{
	const netdiag_t		*nd;	// live ring when arr is NULL
	const nd_bucket_t	*arr;
	int					n;		// buckets in the view
	int64_t				first;	// live: absolute index of view bucket 0
	int					origin;	// view index that time 0 refers to (hitch trigger), or n for "now"
} ng_view_t;

static const nd_bucket_t *NG_At (const ng_view_t *v, int i)
{
	const nd_bucket_t *b;
	if (i < 0 || i >= v->n)
		return NULL;
	if (v->arr)
		b = &v->arr[i];
	else if (v->first + i < 0)
		return NULL;
	else
		b = ND_Bucket (v->nd, (uint32_t)(v->first + i));
	return (b && (b->flags & ND_BF_UNMEASURED)) ? NULL : b;	// not recorded: no data, not zero
}

// the last n completed buckets; the time scale stays fixed while warming up
static void NG_LiveView (const netdiag_t *nd, int n, ng_view_t *v)
{
	memset (v, 0, sizeof(*v));
	v->nd = nd;
	v->n = n;
	v->first = (int64_t)ND_LastComplete (nd) - n + 1;
	v->origin = n;
}

enum { CF_STARVED = 1, CF_LOSS = 2, CF_RESENT = 4, CF_MARK = 8, CF_SUSPEND = 16, CF_BACKLOG = 32 };

typedef struct
{
	int		b0, b1;				// view bucket range [b0, b1)
	float	late;				// max lateness beyond read delay, <0 none
	float	frame, framesum;	// max / sum of frame intervals
	int		frames;
	float	rtt, ping;			// last sample in the column, <0 none
	float	bytes[ND_DIRS][ND_CLASSES];
	float	rate[ND_DIRS][ND_CLASSES];	// bytes/s over at least 100 ms ending at this column
	int		pkts[ND_DIRS];
	int		maxpkt[ND_DIRS];
	float	starved;
	int		missing, resent, backlog;
	unsigned char flags;
	unsigned char onset;		// events that start in this column
	char	letter;
} ng_col_t;

static ng_col_t ng_cols[NG_MAXCOLS];

// whole buckets per column; sums for counts and bytes, maxima kept, never averages of averages
static void NG_Gather (const ng_view_t *v, int w)
{
	int c, i, d, k, last_letter = -100;
	unsigned char prev = 0;
	for (c = 0; c < w; c++)
	{
		ng_col_t *col = &ng_cols[c];
		memset (col, 0, sizeof(*col));
		col->late = col->frame = col->rtt = col->ping = -1;
		col->b0 = (int)((int64_t)c * v->n / w);
		col->b1 = (int)((int64_t)(c + 1) * v->n / w);
		if (col->b1 <= col->b0)
			col->b1 = col->b0 + 1;
		for (i = col->b0; i < col->b1; i++)
		{
			const nd_bucket_t *b = NG_At (v, i);
			if (!b)
				continue;
			if (b->lates)
				col->late = q_max (col->late, q_max (0.f, b->late_net_max_ms));
			if (b->frames)
			{
				col->frame = q_max (col->frame, b->frame_max_ms);
				col->framesum += b->frame_sum_ms;
				col->frames += b->frames;
			}
			if (b->cmdrtt_ms >= 0)
				col->rtt = b->cmdrtt_ms;
			if (b->ping_ms >= 0)
				col->ping = b->ping_ms;
			for (d = 0; d < ND_DIRS; d++)
			{
				for (k = 0; k < ND_CLASSES; k++)
				{
					col->bytes[d][k] += b->bytes[d][k];
					col->pkts[d] += b->pkts[d][k];
				}
				col->maxpkt[d] = q_max (col->maxpkt[d], (int)b->maxpkt[d]);
			}
			col->starved += b->starved_ms;
			if (!(b->flags & ND_BF_GAPRESET))
				col->missing += b->missing;
			col->resent += b->resent;
			col->backlog = q_max (col->backlog, (int)b->backlog_max);
			if (b->starved_ms > 0)
				col->flags |= CF_STARVED;
			if (b->gaps && !(b->flags & ND_BF_GAPRESET))
				col->flags |= CF_LOSS;
			if (b->resent)
				col->flags |= CF_RESENT;
			if (b->backlog_max)
				col->flags |= CF_BACKLOG;
			if (b->flags & ND_BF_MARK)
				col->flags |= CF_MARK;
			if (b->flags & (ND_BF_LOADING | ND_BF_PAUSED | ND_BF_UNFOCUSED | ND_BF_DISCONT))
				col->flags |= CF_SUSPEND;
		}
		{	// steady 62.5 Hz traffic over 1-2 bucket columns would draw as a comb; rates use >= 100 ms
			int span = q_max (col->b1 - col->b0, 5), j;
			float dur = span * ND_BUCKET_SEC;
			for (j = col->b1 - span; j < col->b1; j++)
			{
				const nd_bucket_t *b = NG_At (v, j);
				if (!b)
					continue;
				for (d = 0; d < ND_DIRS; d++)
					for (k = 0; k < ND_CLASSES; k++)
						col->rate[d][k] += b->bytes[d][k] / dur;
			}
		}
		{	// a letter where an event starts, not for every column it lasts; one letter per 9 px
			unsigned char onset = col->flags & ~prev & (CF_LOSS | CF_RESENT | CF_MARK | CF_BACKLOG);
			col->onset = onset;
			if (onset && c - last_letter >= 9)
			{
				col->letter = (onset & CF_MARK) ? 'M' : (onset & CF_LOSS) ? 'L' : (onset & CF_RESENT) ? 'R' : 'B';
				last_letter = c;
			}
			prev = col->flags;
		}
	}
}

/*
=============================================================================
Axes: go up a step at once for a peak, down a step after 3 s of lower demand
=============================================================================
*/
typedef struct
{
	float	top;
	double	low_since;
} ng_axis_t;

typedef struct
{
	ng_axis_t	ping, late, frame, rate;
} ng_axes_t;

static const float ng_ping_ladder[] = {50, 100, 200, 400, 800, 1600};
static const float ng_late_ladder[] = {40, 80, 160, 320, 640, 1280, 2560};
static const float ng_frame_ladder[] = {16, 33, 66, 133, 266, 533, 1066, 2133};
static const float ng_rate_ladder[] = {1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000, 1000000};

static void NG_UpdateAxis (ng_axis_t *a, const float *ladder, int n, float want, double now)
{
	int i, cur = n - 1, need = n - 1;
	if (a->top <= 0)
		a->top = ladder[0];
	for (i = 0; i < n; i++)
		if (ladder[i] >= a->top - 0.01f)
		{
			cur = i;
			break;
		}
	for (i = 0; i < n; i++)
		if (ladder[i] >= want)
		{
			need = i;
			break;
		}
	if (need > cur)
	{
		a->top = ladder[need];
		a->low_since = -1;
	}
	else if (need < cur)
	{
		if (a->low_since < 0)
			a->low_since = now;
		else if (now - a->low_since >= 3.0)
		{
			a->top = ladder[cur - 1];
			a->low_since = now;
		}
	}
	else
		a->low_since = -1;
}

static int NG_FloatCmp (const void *a, const void *b)
{
	float x = *(const float *)a, y = *(const float *)b;
	return x < y ? -1 : x > y;
}

static void NG_UpdateAxes (ng_axes_t *ax, int w, double now)
{
	static float rates[NG_MAXCOLS * ND_DIRS];
	float pw = 0, lw = 0, fw = 0, rw = 0;
	int c, d, nr = 0;
	for (c = 0; c < w; c++)
	{
		const ng_col_t *col = &ng_cols[c];
		pw = q_max (pw, q_max (col->rtt, col->ping));
		lw = q_max (lw, col->late);
		fw = q_max (fw, col->frame);
		for (d = 0; d < ND_DIRS; d++)
			rates[nr++] = col->rate[d][0] + col->rate[d][1] + col->rate[d][2] + col->rate[d][3];
	}
	if (nr)
	{	// traffic follows its 95th percentile: one reliable burst shouldn't flatten everything else
		qsort (rates, nr, sizeof(rates[0]), NG_FloatCmp);
		rw = rates[(int)(nr * 0.95f)];
	}
	NG_UpdateAxis (&ax->ping, ng_ping_ladder, countof (ng_ping_ladder), pw, now);
	NG_UpdateAxis (&ax->late, ng_late_ladder, countof (ng_late_ladder), lw, now);
	NG_UpdateAxis (&ax->frame, ng_frame_ladder, countof (ng_frame_ladder), fw, now);
	NG_UpdateAxis (&ax->rate, ng_rate_ladder, countof (ng_rate_ladder), rw, now);
}

/*
=============================================================================
Lanes
=============================================================================
*/
enum { NGL_PING, NGL_LATE, NGL_FRAME, NGL_TRAFFIC };

static float NG_H (float v, float top, float h)
{
	float r = q_min (v, top) / top * h;
	return r < 1 ? 1 : r;
}

// a value column: faint area with a crisp top edge; red and 2 px wide past the threshold
static void NG_Bar (float x, float base, float v, float top, float thr, float h)
{
	float hv = NG_H (v, top, h), ht;
	if (thr > 0 && v > thr)
	{
		ht = q_min (thr, top) / top * h;
		NG_Quad (NGB_AREA, x, base - ht, x + 1, base);
		NG_Quad (NGB_MARK, x, base - hv, x + 2, base - ht);
	}
	else
	{
		NG_Quad (NGB_AREA, x, base - hv + 1, x + 1, base);
		NG_Quad (NGB_TRACE, x, base - hv, x + 1, base - hv + 1);
	}
}

static void NG_Guide (float x, float w, float base, float v, float top, float h)
{
	float y;
	if (v <= 0 || v >= top || v / top * h < 4)
		return;
	y = base - v / top * h;
	for (; w > 0; x += 3, w -= 3)	// dotted: a line to watch, not a floor
		NG_Quad (NGB_GRID, x, y, x + 1, y + 1);
}

static void NG_DrawLane (int lane, int w, float x, float y, float h, const ng_axes_t *ax, float late_thr, float frame_thr)
{
	int c, d;
	float base = y + h;

	if (lane == NGL_TRAFFIC)
	{	// IN up from the centre line, OUT down from it, one shared scale
		float mid = y + h / 2, half = h / 2 - 1;
		NG_Quad (NGB_GRID, x, mid, x + w, mid + 1);
		for (c = 0; c < w; c++)
		{
			const ng_col_t *col = &ng_cols[c];
			for (d = 0; d < ND_DIRS; d++)
			{	// stacked from the centre: unreliable, then reliable (darker), then ack/other (dim)
				float total = col->rate[d][0] + col->rate[d][1] + col->rate[d][2] + col->rate[d][3];
				if (total > ax->rate.top)	// past the scale: a cap at the edge, exact value in Inspect
				{
					if (d == ND_IN)
						NG_Quad (NGB_TRACE, x + c, y, x + c + 1, y + 1);
					else
						NG_Quad (NGB_TRACE, x + c, y + h - 1, x + c + 1, y + h);
				}
				static const int layer[3] = {NGB_AREA, NGB_DIM, NGB_GRID};
				float acc = 0;
				int k;
				for (k = 0; k < 3; k++)
				{
					float r = k == 0 ? col->rate[d][ND_UNREL] : k == 1 ? col->rate[d][ND_REL] :
						col->rate[d][ND_ACK] + col->rate[d][ND_OTHER];
					float a0, a1;
					if (r <= 0)
						continue;
					a0 = q_min (acc, ax->rate.top) / ax->rate.top * half;
					acc += r;
					a1 = q_min (acc, ax->rate.top) / ax->rate.top * half;
					if (a1 - a0 < 1)
						a1 = a0 + 1;
					if (d == ND_IN)
						NG_Quad (layer[k], x + c, mid - a1, x + c + 1, mid - a0);
					else
						NG_Quad (layer[k], x + c, mid + 1 + a0, x + c + 1, mid + 1 + a1);
				}
			}
		}
		return;
	}

	NG_Quad (NGB_GRID, x, base - 1, x + w, base);
	if (lane == NGL_LATE)
		NG_Guide (x, w, base, late_thr, ax->late.top, h);
	else if (lane == NGL_FRAME)
		NG_Guide (x, w, base, frame_thr, ax->frame.top, h);

	for (c = 0; c < w; c++)
	{
		const ng_col_t *col = &ng_cols[c];
		if (col->flags & CF_SUSPEND)
			continue;
		switch (lane)
		{
		case NGL_PING:
			if (col->rtt > 0)
				NG_Bar (x + c, base, col->rtt, ax->ping.top, 0, h);
			if (col->ping >= 0)
			{	// server reports are sparse: a dot where each one arrived
				float py = base - NG_H (col->ping, ax->ping.top, h);
				NG_Quad (NGB_TRACE, x + c - 1, py - 1, x + c + 2, py + 2);
			}
			break;
		case NGL_LATE:
			if (col->late > 0)
				NG_Bar (x + c, base, col->late, ax->late.top, late_thr, h);
			break;
		case NGL_FRAME:
			if (col->frame > 0)
			{
				NG_Bar (x + c, base, col->frame, ax->frame.top, frame_thr, h);
				if (col->frames > 1 && !(c & 1))	// dotted mean under the peak
				{
					float my = base - NG_H (col->framesum / col->frames, ax->frame.top, h);
					NG_Quad (NGB_DIM, x + c, my, x + c + 1, my + 1);
				}
			}
			break;
		}
	}
}

// starved as a red underline, discontinuities as faint ticks, events as letters (drawn after)
static void NG_DrawRail (int w, float x, float y)
{
	int c;
	for (c = 0; c < w; c++)
	{
		const ng_col_t *col = &ng_cols[c];
		if (col->flags & CF_STARVED)
			NG_Quad (NGB_MARK, x + c, y, x + c + 1, y + 1);
		if (col->flags & CF_BACKLOG)	// lasting conditions are lines; their start gets the letter
			NG_Quad (NGB_DIM, x + c, y + 7, x + c + 1, y + 8);
		if (col->flags & CF_SUSPEND)
			NG_Quad (NGB_GRID, x + c, y + 2, x + c + 1, y + 4);
		if (col->onset && !col->letter)
			NG_Quad (NGB_MARK, x + c, y + 2, x + c + 1, y + 6);
	}
}

static void NG_DrawRailLetters (int w, int x, int y)
{
	int c;
	for (c = 0; c < w; c++)
	{
		char s[2];
		if (!ng_cols[c].letter)
			continue;
		s[0] = ng_cols[c].letter;
		s[1] = 0;
		Draw_StringMasked (x + q_min (q_max (c - 3, 0), w - 8), y, s);
	}
}

/*
=============================================================================
Numbers
=============================================================================
*/
static void NG_FormatRate (char *out, size_t size, double bps, qboolean persec)
{
	const char *u = persec ? "/s" : "";
	if (bps < 1000)
		q_snprintf (out, size, "%dB%s", (int)(bps + 0.5), u);
	else if (bps < 100000)
		q_snprintf (out, size, "%.1fKB%s", bps / 1000.0, u);
	else
		q_snprintf (out, size, "%dKB%s", (int)(bps / 1000.0 + 0.5), u);
}

static void NG_FormatMs (char *out, size_t size, float ms)
{
	if (ms < 0)
		q_strlcpy (out, "--", size);
	else	// whole milliseconds: tenths only add flicker
		q_snprintf (out, size, "%dms", (int)(ms + 0.5f));
}

typedef struct
{
	float	late, frame, rtt, ping;		// last-second peaks / latest
	float	ping_min, ping_max;
	double	in_bps, out_bps;
	float	jitter, loss_pct, tick_hz;
} ng_nums_t;

// peaks and rates over the view's last second; spreads, loss and tick over all of it
static void NG_Numbers (const ng_view_t *v, ng_nums_t *n)
{
	int i, accepted = 0, missing = 0, lates = 0;
	double late_sum = 0, late_sumsq = 0, svgap = 0, rate_span = 0, in = 0, out = 0;
	const int recent = v->n - ND_RATE_BUCKETS;

	memset (n, 0, sizeof(*n));
	n->late = n->frame = n->rtt = n->ping = -1;
	n->ping_min = 1e9f;
	n->loss_pct = n->tick_hz = -1;

	for (i = 0; i < v->n; i++)
	{
		const nd_bucket_t *b = NG_At (v, i);
		int k;
		if (!b)
			continue;
		if (i >= recent)
		{
			if (b->lates)
				n->late = q_max (n->late, q_max (0.f, b->late_net_max_ms));
			if (b->frames)
				n->frame = q_max (n->frame, b->frame_max_ms);
			if (b->cmdrtt_ms >= 0)
				n->rtt = b->cmdrtt_ms;
			rate_span += ND_BUCKET_SEC;
			for (k = 0; k < ND_CLASSES; k++)
			{
				in += b->bytes[ND_IN][k];
				out += b->bytes[ND_OUT][k];
			}
		}
		if (b->ping_ms >= 0)
		{
			n->ping = b->ping_ms;
			n->ping_min = q_min (n->ping_min, b->ping_ms);
			n->ping_max = q_max (n->ping_max, b->ping_ms);
		}
		lates += b->lates;
		late_sum += b->late_net_sum_ms;	// JITTER is LATE's spread, like everywhere else
		late_sumsq += b->late_net_sumsq;
		svgap += b->svgap_sum_ms;
		if (!(b->flags & ND_BF_GAPRESET))
		{
			accepted += b->accepted;
			missing += b->missing;
		}
	}
	if (rate_span > 0)
	{
		n->in_bps = in / rate_span;
		n->out_bps = out / rate_span;
	}
	if (lates > 1)
	{
		double mean = late_sum / lates, var = late_sumsq / lates - mean * mean;
		n->jitter = var > 0 ? (float)sqrt (var) : 0;
	}
	if (svgap > 0)
		n->tick_hz = (float)(1000.0 * lates / svgap);	// snapshots received per second; loss is shown on its own
	if (accepted + missing)
		n->loss_pct = 100.f * missing / (accepted + missing);
	if (n->ping < 0)
		n->ping_min = n->ping_max = -1;
}

static int NG_SbarLines (void)
{	// status bar footprint in HUD units (this canvas uses the status bar's scale);
	// sb_lines can't be used: it is in pixels and drops to 0 for a translucent bar
	if (scr_viewsize.value >= 130 || cl.intermission)
		return 0;
	return scr_viewsize.value >= 120 ? 24 : 48;
}

static float NG_Scale (void)
{
	return CLAMP (1.0f, scr_sbarscale.value, (float)glwidth / 320.0f);
}

// the match clock and match scores own the top-right corner during matches;
// they draw at twice the console scale: the clock in the top 16 units, scores down to ~52
static int NG_TopRightReserve (void)
{
	extern cvar_t scr_match_hud, scr_matchclock;
	float sc = (float)glwidth / vid.conwidth * 2, units = 0;
	if (scr_matchclock.value)
		units = 16;
	if (scr_match_hud.value && (cl.teamgame || cl.modetype == 8))
		units = 52;
	return units ? (int)(units * sc / NG_Scale () + 0.5f) : 0;
}

static qboolean NG_Place (int w, int h, int *x, int *y)
{
	float s = NG_Scale ();
	int cw = (int)(glwidth / s), ch = (int)(glheight / s), anchor = NetDiag_PanelAnchor ();
	int top = anchor == 1 ? NG_TopRightReserve () : 0;
	w += 2 * NG_BACK_PAD;	// place the backdrop; content sits inside it
	h += 2 * NG_BACK_PAD;
	if (cw < w + 2 * NG_MARGIN || ch < h + 2 * NG_MARGIN + NG_SbarLines () + top)
		return false;
	*x = (anchor & 1) ? cw - w - NG_MARGIN : NG_MARGIN;
	*y = (anchor & 2) ? ch - h - NG_MARGIN - NG_SbarLines () : NG_MARGIN + top;
	*x += NG_BACK_PAD;
	*y += NG_BACK_PAD;
	return true;
}

/*
=============================================================================
Panels
=============================================================================
*/
// Compact: 296 x 64, LATE and FRAME over 5 s
#define NGC_W		296
#define NGC_H		64
#define NGC_PLOT_X	(NG_PAD + NG_LABEL_W + 4)
#define NGC_PLOT_W	(NGC_W - NGC_PLOT_X - 4 - 56 - NG_PAD)
#define NGC_LANE_H	16
#define NGC_LATE_Y	NG_PAD
#define NGC_FRAME_Y	(NGC_LATE_Y + NGC_LANE_H + 2)
#define NGC_RAIL_Y	(NGC_FRAME_Y + NGC_LANE_H + 2)
#define NGC_FOOT_Y	(NGC_RAIL_Y + 8 + 4)
#define NGC_FOOT_CHARS ((NGC_W - 2 * NG_PAD) / 8 - 8)	// before the "HITCH n" slot

// Detailed: 384 wide, PING, LATE, FRAME, IN/OUT over 10 s
#define NGD_W		384
#define NGD_PLOT_X	(NG_PAD + NG_LABEL_W + 4)
#define NGD_PLOT_W	(NGD_W - NGD_PLOT_X - 4 - 56 - NG_PAD)
#define NGD_LANE_H	18
#define NGD_TRAF_H	36
#define NGD_PING_Y	NG_PAD
#define NGD_LATE_Y	(NGD_PING_Y + NGD_LANE_H + 3)
#define NGD_FRAME_Y	(NGD_LATE_Y + NGD_LANE_H + 3)
#define NGD_TRAF_Y	(NGD_FRAME_Y + NGD_LANE_H + 3)
#define NGD_RAIL_Y	(NGD_TRAF_Y + NGD_TRAF_H + 2)
#define NGD_FOOT_Y	(NGD_RAIL_Y + 8 + 4)
#define NGD_H		(NGD_FOOT_Y + 8 + 2 + 8 + NG_PAD)

static ng_axes_t	ng_axes_compact, ng_axes_detailed;
static struct
{
	double	at;
	int		mode;
	char	lane[5][16];	// lane values; Detailed's OUT value is lane[4]
	char	foot[64], footshort[64], foot2[32], hitch[16], state[24];
	qboolean state_alert;
} ng_text;

static void NG_PanelText (const netdiag_t *nd, const ng_view_t *v, int mode)
{
	ng_nums_t n;
	int net = nd->source == ND_SRC_NET || nd->source == ND_SRC_OTHER, k;
	char in[16], out[16], lat[24], a[24], b[16];
	char *p;

	NG_Numbers (v, &n);
	if (net && n.rtt >= 0)	// command round trip is the live number when the server acks moves
		q_snprintf (lat, sizeof(lat), "RTT %d", (int)(n.rtt + 0.5f));
	else if (net && n.ping >= 0)
		q_snprintf (lat, sizeof(lat), "PING %d", (int)(n.ping + 0.5f));
	else
		q_strlcpy (lat, net ? "PING --" : "", sizeof(lat));

	if (mode == 1)
	{
		NG_FormatMs (ng_text.lane[0], sizeof(ng_text.lane[0]), net ? n.late : -1);
		NG_FormatMs (ng_text.lane[1], sizeof(ng_text.lane[1]), n.frame);
		if (nd->source == ND_SRC_NET)
		{
			NG_FormatRate (in, sizeof(in), n.in_bps, true);
			NG_FormatRate (out, sizeof(out), n.out_bps, true);
			q_snprintf (ng_text.foot, sizeof(ng_text.foot), "%s%sIN %s  OUT %s", lat, *lat ? "   " : "", in, out);
			if (strlen (ng_text.foot) > (NGC_W - 2 * NG_PAD) / 8)
				q_snprintf (ng_text.foot, sizeof(ng_text.foot), "%s%sIN %s OUT %s", lat, *lat ? " " : "", in, out);
		}
		else
			q_strlcpy (ng_text.foot, lat, sizeof(ng_text.foot));
		// a shorter form for when the hitch count needs the room
		q_strlcpy (ng_text.footshort, ng_text.foot, sizeof(ng_text.footshort));
		while ((p = strstr (ng_text.footshort, "/s")) != NULL)
			memmove (p, p + 2, strlen (p + 2) + 1);
	}
	else
	{
		if (net && (n.rtt >= 0 || n.ping >= 0))
			q_snprintf (ng_text.lane[0], sizeof(ng_text.lane[0]), "%dms", (int)((n.rtt >= 0 ? n.rtt : n.ping) + 0.5f));
		else
			q_strlcpy (ng_text.lane[0], "--", sizeof(ng_text.lane[0]));
		NG_FormatMs (ng_text.lane[1], sizeof(ng_text.lane[1]), net ? n.late : -1);
		NG_FormatMs (ng_text.lane[2], sizeof(ng_text.lane[2]), n.frame);
		if (nd->source == ND_SRC_NET)
		{
			NG_FormatRate (ng_text.lane[3], sizeof(ng_text.lane[3]), n.in_bps, true);
			NG_FormatRate (ng_text.lane[4], sizeof(ng_text.lane[4]), n.out_bps, true);
		}
		else
		{
			q_strlcpy (ng_text.lane[3], "--", sizeof(ng_text.lane[3]));
			q_strlcpy (ng_text.lane[4], "--", sizeof(ng_text.lane[4]));
		}
		if (net)
		{
			if (n.ping >= 0 && (int)(n.ping_min + 0.5f) != (int)(n.ping_max + 0.5f))
				q_snprintf (a, sizeof(a), "PING %d (%d-%d)", (int)(n.ping + 0.5f), (int)(n.ping_min + 0.5f), (int)(n.ping_max + 0.5f));
			else if (n.ping >= 0)
				q_snprintf (a, sizeof(a), "PING %d", (int)(n.ping + 0.5f));
			else
				q_strlcpy (a, "PING --", sizeof(a));
			if (n.loss_pct >= 0)
				q_snprintf (b, sizeof(b), "%.1f%%", n.loss_pct);
			else
				q_strlcpy (b, "--", sizeof(b));
			q_snprintf (ng_text.foot, sizeof(ng_text.foot), "%s  JITTER %.1fms  LOSS %s", a, n.jitter, b);
			if (n.tick_hz > 0)
				q_snprintf (ng_text.foot2, sizeof(ng_text.foot2), "UPDATES %d/s", (int)(n.tick_hz + 0.5f));
			else
				q_strlcpy (ng_text.foot2, "UPDATES --", sizeof(ng_text.foot2));
		}
		else
		{
			ng_text.foot[0] = 0;
			ng_text.foot2[0] = 0;
		}
	}

	k = ND_HitchCount (nd, 0);	// detected only; marks are on the rail and in reports
	if (k)
		q_snprintf (ng_text.hitch, sizeof(ng_text.hitch), "HITCH %d", k);
	else
		ng_text.hitch[0] = 0;
	{
		const char *w = NetDiag_StateWord ();
		float quiet = NetDiag_Silence ();
		ng_text.state_alert = false;
		if (quiet >= 2)
		{	// the one thing worth shouting about
			q_snprintf (ng_text.state, sizeof(ng_text.state), "NO UPDATES %ds", (int)quiet);
			ng_text.state_alert = true;
		}
		else if (!strcmp (w, "LIVE"))
			ng_text.state[0] = 0;	// the normal state says nothing
		else
			q_strlcpy (ng_text.state, w, sizeof(ng_text.state));
	}
}

static void NG_DrawCompact (const netdiag_t *nd, int x0, int y0)
{
	ng_view_t v;
	float lt = ND_LateThresholdOf (nd), ft = ND_FrameThresholdOf (nd);
	const int px = x0 + NGC_PLOT_X, right = x0 + NGC_W - NG_PAD;

	NG_LiveView (nd, ND_WINDOW_BUCKETS, &v);
	NG_Gather (&v, NGC_PLOT_W);
	NG_UpdateAxes (&ng_axes_compact, NGC_PLOT_W, realtime);
	if (realtime - ng_text.at >= NG_TEXT_EVERY || realtime < ng_text.at || ng_text.mode != 1)
	{
		ng_text.at = realtime;
		ng_text.mode = 1;
		NG_PanelText (nd, &v, 1);
	}

	NG_Backdrop (x0, y0, NGC_W, NGC_H, 0.7f);
	NG_BeginGeometry ();
	NG_DrawLane (NGL_LATE, NGC_PLOT_W, px, y0 + NGC_LATE_Y, NGC_LANE_H, &ng_axes_compact, lt, ft);
	NG_DrawLane (NGL_FRAME, NGC_PLOT_W, px, y0 + NGC_FRAME_Y, NGC_LANE_H, &ng_axes_compact, lt, ft);
	NG_DrawRail (NGC_PLOT_W, px, y0 + NGC_RAIL_Y);
	NG_EndGeometry ();

	NG_DrawRailLetters (NGC_PLOT_W, px, y0 + NGC_RAIL_Y);
	Draw_String (x0 + NG_PAD, y0 + NGC_LATE_Y + 4, "LATE");
	Draw_String (x0 + NG_PAD, y0 + NGC_FRAME_Y + 4, "FRAME");
	NG_DrawRight (right, y0 + NGC_LATE_Y + 4, ng_text.lane[0], false);
	NG_DrawRight (right, y0 + NGC_FRAME_Y + 4, ng_text.lane[1], false);
	if (!ng_text.state_alert)
		NG_DrawRight (right, y0 + NGC_RAIL_Y, ng_text.state, false);
	if (ng_text.state_alert || ng_text.hitch[0])
	{	// the right slot: an alert wins over the hitch count; the left side shortens to make room
		const char *slot = ng_text.state_alert ? ng_text.state : ng_text.hitch;
		int room = (NGC_W - 2 * NG_PAD) / 8 - (int)strlen (slot) - 1;
		const char *left = (int)strlen (ng_text.foot) <= room ? ng_text.foot : ng_text.footshort;
		if ((int)strlen (left) <= room)
			Draw_String (x0 + NG_PAD, y0 + NGC_FOOT_Y, left);
		NG_DrawRight (right, y0 + NGC_FOOT_Y, slot, true);
	}
	else
		Draw_String (x0 + NG_PAD, y0 + NGC_FOOT_Y, ng_text.foot);
}

static void NG_DrawDetailed (const netdiag_t *nd, int x0, int y0)
{
	ng_view_t v;
	float lt = ND_LateThresholdOf (nd), ft = ND_FrameThresholdOf (nd);
	const int px = x0 + NGD_PLOT_X, right = x0 + NGD_W - NG_PAD;

	NG_LiveView (nd, ND_WINDOW_BUCKETS * 2, &v);
	NG_Gather (&v, NGD_PLOT_W);
	NG_UpdateAxes (&ng_axes_detailed, NGD_PLOT_W, realtime);
	if (realtime - ng_text.at >= NG_TEXT_EVERY || realtime < ng_text.at || ng_text.mode != 2)
	{
		ng_text.at = realtime;
		ng_text.mode = 2;
		NG_PanelText (nd, &v, 2);
	}

	NG_Backdrop (x0, y0, NGD_W, NGD_H, 0.7f);
	NG_BeginGeometry ();
	NG_DrawLane (NGL_PING, NGD_PLOT_W, px, y0 + NGD_PING_Y, NGD_LANE_H, &ng_axes_detailed, lt, ft);
	NG_DrawLane (NGL_LATE, NGD_PLOT_W, px, y0 + NGD_LATE_Y, NGD_LANE_H, &ng_axes_detailed, lt, ft);
	NG_DrawLane (NGL_FRAME, NGD_PLOT_W, px, y0 + NGD_FRAME_Y, NGD_LANE_H, &ng_axes_detailed, lt, ft);
	NG_DrawLane (NGL_TRAFFIC, NGD_PLOT_W, px, y0 + NGD_TRAF_Y, NGD_TRAF_H, &ng_axes_detailed, lt, ft);
	NG_DrawRail (NGD_PLOT_W, px, y0 + NGD_RAIL_Y);
	NG_EndGeometry ();

	NG_DrawRailLetters (NGD_PLOT_W, px, y0 + NGD_RAIL_Y);
	Draw_String (x0 + NG_PAD, y0 + NGD_PING_Y + 5, nd->last_cmdrtt_time >= 0 ? "RTT" : "PING");
	Draw_String (x0 + NG_PAD, y0 + NGD_LATE_Y + 5, "LATE");
	Draw_String (x0 + NG_PAD, y0 + NGD_FRAME_Y + 5, "FRAME");
	Draw_String (x0 + NG_PAD, y0 + NGD_TRAF_Y + 5, "IN");
	Draw_String (x0 + NG_PAD, y0 + NGD_TRAF_Y + NGD_TRAF_H - 13, "OUT");
	NG_DrawRight (right, y0 + NGD_PING_Y + 5, ng_text.lane[0], false);
	NG_DrawRight (right, y0 + NGD_LATE_Y + 5, ng_text.lane[1], false);
	NG_DrawRight (right, y0 + NGD_FRAME_Y + 5, ng_text.lane[2], false);
	NG_DrawRight (right, y0 + NGD_TRAF_Y + 5, ng_text.lane[3], false);
	NG_DrawRight (right, y0 + NGD_TRAF_Y + NGD_TRAF_H - 13, ng_text.lane[4], false);
	if (!ng_text.state_alert)
		NG_DrawRight (right, y0 + NGD_RAIL_Y, ng_text.state, false);
	Draw_String (x0 + NG_PAD, y0 + NGD_FOOT_Y, ng_text.foot);
	Draw_String (x0 + NG_PAD, y0 + NGD_FOOT_Y + 10, ng_text.foot2);
	if (ng_text.state_alert || ng_text.hitch[0])
		NG_DrawRight (right, y0 + NGD_FOOT_Y + 10, ng_text.state_alert ? ng_text.state : ng_text.hitch, true);
}

void SCR_DrawNetGraph (void)
{
	const netdiag_t *nd;
	int mode, x0, y0;
	static qboolean warned;

	if (!netdiag_wantframe || !(mode = NetDiag_PanelMode ()))
		return;
	if (key_dest == key_menu && m_state == m_netgraph)
		return;	// Inspect is showing the same data
	nd = NetDiag_Data ();
	if (!nd || !NetDiag_Live () || !nd->started || nd->cur < 2)
		return;

	if (mode == 2 && !NG_Place (NGD_W, NGD_H, &x0, &y0))
		mode = 1;	// Detailed doesn't fit: Compact, setting kept
	if (mode == 1 && !NG_Place (NGC_W, NGC_H, &x0, &y0))
	{	// too small for readable 8 px glyphs; never squeeze text
		if (!warned)
			Con_Printf ("netgraph: the view is too small for the panel at this HUD scale\n");
		warned = true;
		return;
	}

	GL_SetCanvas (CANVAS_NETGRAPH);
	if (mode == 2)
		NG_DrawDetailed (nd, x0, y0);
	else
		NG_DrawCompact (nd, x0, y0);
}

/*
=============================================================================
Inspect: a menu page over the whole view
=============================================================================
*/
enum { NGS_LIVE, NGS_FROZEN, NGS_HITCH, NGS_MARK };
enum { NGF_SOURCE, NGF_DISPLAY, NGF_POSITION, NGF_GRAPH, NGF_COUNT };

typedef struct
{
	int		kind, slot;
	char	label[12];
	int		x, y, w;
} ng_src_t;

static struct
{
	qboolean	from_menu;
	int			focus;
	int			src_kind, src_slot;
	int			cursor;			// plot column, -1 = whole view
	qboolean	pinned;
	nd_bucket_t	*frozen;
	int			frozen_n;
	ng_axes_t	axes;
	// layout from the last draw, for mouse hits (netgraph canvas units)
	int			plot_x, plot_y, plot_w, plot_h, src_y, disp_y, pos_y;
	ng_src_t	srcs[2 + 2 * ND_HITCH_SLOTS];
	int			nsrcs;
	int			disp_x[3], pos_x[4];
	int			mouse_x, mouse_y;
	qboolean	mouse_seen;
	qboolean	mouse_moved;	// hover takes over only after the pointer moves
	qboolean	disp_compact, pos_compact;	// rows shown as "< CURRENT >" when the options don't fit
} ngi;

static const char *ngi_display_cmd[3] = {"netgraph off\n", "netgraph compact\n", "netgraph detailed\n"};
static const char *ngi_position_cmd[4] = {"netgraph topleft\n", "netgraph topright\n", "netgraph bottomleft\n", "netgraph bottomright\n"};

static void NGI_FreeFrozen (void)
{
	free (ngi.frozen);
	ngi.frozen = NULL;
	ngi.frozen_n = 0;
}

// a stable copy of the last 20 s; recording carries on underneath
static void NGI_Freeze (void)
{
	const netdiag_t *nd = NetDiag_Data ();
	ng_view_t v;
	int i;
	if (!nd || !nd->started)
		return;
	NG_LiveView (nd, ND_RING - 24, &v);
	NGI_FreeFrozen ();
	ngi.frozen = (nd_bucket_t *) calloc (v.n, sizeof(nd_bucket_t));
	if (!ngi.frozen)
		return;
	for (i = 0; i < v.n; i++)
	{
		const nd_bucket_t *b = NG_At (&v, i);
		if (b)
			ngi.frozen[i] = *b;
		else
		{
			ngi.frozen[i].ping_ms = ngi.frozen[i].cmdrtt_ms = -1;
			ngi.frozen[i].flags = ND_BF_DISCONT;
		}
	}
	ngi.frozen_n = v.n;
	ngi.src_kind = NGS_FROZEN;
	ngi.src_slot = 0;
	ngi.cursor = -1;
	ngi.pinned = false;
}

static void NGI_BuildSources (const netdiag_t *nd)
{
	int i, n = 0, h = 0, m = 0;
	if (nd)
	{
		ngi.srcs[n].kind = NGS_LIVE;
		ngi.srcs[n].slot = 0;	// not recording: the kept last 20 s
		// LIVE (or LOCAL, REPLAY...) while it's still coming in; afterwards, the kept last 20 s
		q_strlcpy (ngi.srcs[n++].label, NetDiag_Live () && (netdiag_active || !strcmp (NetDiag_StateWord (), "REPLAY")) ?
			NetDiag_StateWord () : "RECENT", sizeof(ngi.srcs[0].label));
	}
	if (ngi.frozen)
	{
		ngi.srcs[n].kind = NGS_FROZEN;
		ngi.srcs[n].slot = 0;
		q_strlcpy (ngi.srcs[n++].label, "FROZEN", sizeof(ngi.srcs[0].label));
	}
	if (nd)
	{
		for (i = 0; i < ND_HITCH_SLOTS; i++)
			if (nd->hitches[i].m.used)
			{
				ngi.srcs[n].kind = NGS_HITCH;
				ngi.srcs[n].slot = i;
				q_snprintf (ngi.srcs[n++].label, sizeof(ngi.srcs[0].label), "HITCH %d", ++h);
			}
		for (i = 0; i < ND_HITCH_SLOTS; i++)
			if (nd->marks[i].m.used)
			{
				ngi.srcs[n].kind = NGS_MARK;
				ngi.srcs[n].slot = i;
				q_snprintf (ngi.srcs[n++].label, sizeof(ngi.srcs[0].label), "MARK %d", ++m);
			}
	}
	ngi.nsrcs = n;
}

static int NGI_SourceIndex (void)
{
	int i;
	for (i = 0; i < ngi.nsrcs; i++)
		if (ngi.srcs[i].kind == ngi.src_kind && ngi.srcs[i].slot == ngi.src_slot)
			return i;
	return -1;
}

static void NGI_SelectSource (int i)
{
	if (i < 0 || i >= ngi.nsrcs)
		return;
	if (ngi.srcs[i].kind != ngi.src_kind || ngi.srcs[i].slot != ngi.src_slot)
	{
		ngi.cursor = -1;
		ngi.pinned = false;
		memset (&ngi.axes, 0, sizeof(ngi.axes));
	}
	ngi.src_kind = ngi.srcs[i].kind;
	ngi.src_slot = ngi.srcs[i].slot;
}

// the view for the selected source, and the hitch it belongs to (if any)
static qboolean NGI_View (const netdiag_t *nd, ng_view_t *v, const nd_hitch_t **h)
{
	*h = NULL;
	memset (v, 0, sizeof(*v));
	switch (ngi.src_kind)
	{
	case NGS_FROZEN:
		if (!ngi.frozen)
			return false;
		v->arr = ngi.frozen;
		v->n = ngi.frozen_n;
		v->origin = v->n;
		return true;
	case NGS_HITCH:
	case NGS_MARK:
		if (!nd)
			return false;
		*h = ngi.src_kind == NGS_HITCH ? &nd->hitches[ngi.src_slot] : &nd->marks[ngi.src_slot];
		if (!(*h)->m.used)
			return false;
		v->arr = (*h)->window;
		v->n = ND_HITCH_BUCKETS;
		v->origin = ND_HITCH_PRE;
		return true;
	default:
		if (!nd || !nd->started)
			return false;
		NG_LiveView (nd, ND_RING - 24, v);
		return true;
	}
}

void M_Menu_NetGraph_f (qboolean from_menu)
{
	const netdiag_t *nd = NetDiag_Data ();
	key_dest = key_menu;
	m_state = m_netgraph;
	m_entersound = true;
	ngi.from_menu = from_menu;
	ngi.focus = nd ? NGF_GRAPH : NGF_DISPLAY;
	ngi.cursor = -1;
	ngi.pinned = false;
	ngi.mouse_seen = false;
	ngi.mouse_moved = false;
	memset (&ngi.axes, 0, sizeof(ngi.axes));
	ngi.src_kind = NGS_LIVE;
	ngi.src_slot = 0;
	if (nd && !netdiag_active)
	{	// not recording: open on the first kept hitch rather than a still live view
		NGI_BuildSources (nd);
		if (ngi.nsrcs > 1)
			NGI_SelectSource (1);
	}
	IN_UpdateGrabs ();
}

void M_NetGraph_Leave (void)
{
	NGI_FreeFrozen ();
}

static void NGI_Close (void)
{
	NGI_FreeFrozen ();	// a frozen copy belongs to this visit
	if (ngi.from_menu)
		M_Menu_HUD_f ();
	else
	{
		key_dest = key_game;
		m_state = m_none;
		IN_UpdateGrabs ();
	}
}

static void NGI_DetailLines (const ng_view_t *v, int c0, int c1, char lines[4][160])
{
	int i, d, frames = 0, pkts[2] = {0, 0}, maxpkt[2] = {0, 0}, missing = 0, resent = 0, backlog = 0;
	float late = -1, frame = -1, framesum = 0, starved = 0, rtt = -1, ping = -1;
	double bytes[2] = {0, 0};
	uint32_t svc[ND_SVC_TYPES];
	uint64_t svctotal = 0;
	int top[3] = {-1, -1, -1};
	char a[24], b[24], t[24], ms1[16], ms2[16], ms3[16];

	// every bucket once: when the plot is wider than the view, neighbouring columns share buckets
	const int b0 = ng_cols[c0].b0, b1 = ng_cols[c1].b1;
	memset (svc, 0, sizeof(svc));
	for (i = b0; i < b1; i++)
	{
		const nd_bucket_t *bk = NG_At (v, i);
		int k;
		if (!bk)
			continue;
		if (bk->lates)
			late = q_max (late, q_max (0.f, bk->late_net_max_ms));
		if (bk->frames)
		{
			frame = q_max (frame, bk->frame_max_ms);
			framesum += bk->frame_sum_ms;
			frames += bk->frames;
		}
		starved += bk->starved_ms;
		if (bk->cmdrtt_ms >= 0)
			rtt = bk->cmdrtt_ms;
		if (bk->ping_ms >= 0)
			ping = bk->ping_ms;
		if (!(bk->flags & ND_BF_GAPRESET))
			missing += bk->missing;
		resent += bk->resent;
		backlog = q_max (backlog, (int)bk->backlog_max);
		for (d = 0; d < ND_DIRS; d++)
		{
			for (k = 0; k < ND_CLASSES; k++)
			{
				bytes[d] += bk->bytes[d][k];
				pkts[d] += bk->pkts[d][k];
			}
			maxpkt[d] = q_max (maxpkt[d], (int)bk->maxpkt[d]);
		}
		for (k = 0; k < ND_TOP_SVC; k++)
			if (bk->top_svc_bytes[k])
				svc[bk->top_svc[k]] += bk->top_svc_bytes[k];
		// shares are of all message bytes; buckets keep only their top three types
		svctotal += bk->svc_total ? bk->svc_total :
			(uint32_t)bk->top_svc_bytes[0] + bk->top_svc_bytes[1] + bk->top_svc_bytes[2];	// older captures
	}
	for (i = 0; i < ND_SVC_TYPES; i++)
	{
		if (!svc[i])
			continue;
		if (top[0] < 0 || svc[i] > svc[top[0]])
		{
			top[2] = top[1];
			top[1] = top[0];
			top[0] = i;
		}
		else if (top[1] < 0 || svc[i] > svc[top[1]])
		{
			top[2] = top[1];
			top[1] = i;
		}
		else if (top[2] < 0 || svc[i] > svc[top[2]])
			top[2] = i;
	}

	// the selection's end, relative to now (live, frozen) or to the trigger (hitch)
	q_snprintf (t, sizeof(t), "%+.2fs", (b1 - v->origin) * ND_BUCKET_SEC);
	NG_FormatMs (ms1, sizeof(ms1), late);
	NG_FormatMs (ms2, sizeof(ms2), frame);
	NG_FormatMs (ms3, sizeof(ms3), frames ? framesum / frames : -1);
	{	// starvation is only measured where snapshots come over a network
		const netdiag_t *src = NetDiag_Data ();
		char st[16];
		if (src && (src->source == ND_SRC_NET || src->source == ND_SRC_OTHER))
			q_snprintf (st, sizeof(st), "%dms", (int)(starved + 0.5f));
		else
			q_strlcpy (st, "--", sizeof(st));
		q_snprintf (lines[0], 160, "%s  {LATE} %s  {FRAME} %s  {AVG} %s  {STARVED} %s", t, ms1, ms2, ms3, st);
	}
	NG_FormatRate (a, sizeof(a), bytes[0], false);
	NG_FormatRate (b, sizeof(b), bytes[1], false);
	q_snprintf (lines[1], 160, "{IN} %s %d pkts max %dB  {OUT} %s %d pkts max %dB", a, pkts[0], maxpkt[0], b, pkts[1], maxpkt[1]);
	if (svctotal)
	{	// what the top-three ranking dropped shows as "other"
		char part[4][40];
		uint64_t shown = 0;
		for (i = 0; i < 3; i++)
		{
			part[i][0] = 0;
			if (top[i] >= 0)
			{
				q_snprintf (part[i], sizeof(part[i]), "%s %d%%  ", NG_ShortSvc (top[i]), (int)(100.0 * svc[top[i]] / svctotal + 0.5));
				shown += svc[top[i]];
			}
		}
		part[3][0] = 0;
		if (svctotal > shown && (int)(100.0 * (svctotal - shown) / svctotal + 0.5) > 0)
			q_snprintf (part[3], sizeof(part[3]), "other %d%%", (int)(100.0 * (svctotal - shown) / svctotal + 0.5));
		q_snprintf (lines[2], 160, "{MESSAGES} %s%s%s%s", part[0], part[1], part[2], part[3]);
	}
	else
		q_strlcpy (lines[2], "{MESSAGES} --", 160);
	if (rtt >= 0)
		q_snprintf (a, sizeof(a), "%dms", (int)(rtt + 0.5f));
	else
		q_strlcpy (a, "--", sizeof(a));
	if (ping >= 0)
		q_snprintf (b, sizeof(b), "%dms", (int)(ping + 0.5f));
	else
		q_strlcpy (b, "--", sizeof(b));
	q_snprintf (lines[3], 160, "{LOST} %d  {RESENT} %d  {BACKLOG} %dB  {RTT} %s  {SERVER PING} %s", missing, resent, backlog, a, b);
	if (NetDiag_Data () && NetDiag_Data ()->source != ND_SRC_NET)
	{	// loopback, demo or another transport: don't show zeros for what wasn't measured
		q_strlcpy (lines[1], "{NETWORK} not measured here", 160);
		lines[3][0] = 0;
	}
}

// lay out a row of choices; when they don't fit, show "< CURRENT >" instead
static qboolean NGI_ChoiceRow (int x, int right, int y, const char *label, const char **opts, int n, int cur,
	int *xs, qboolean focused)
{
	int i, dx = x, need = 0;
	for (i = 0; i < n; i++)
		need += (int)strlen (opts[i]) * 8 + 16;
	Draw_String (x - 16 * 8, y, label);
	if (x + need - 16 > right)
	{	// compact: the current choice only; LEFT/RIGHT or a click cycles it
		char buf[32];
		q_snprintf (buf, sizeof(buf), "< %s >", opts[cur]);
		Draw_StringMasked (x, y, buf);
		if (focused)
			Draw_Character (x - 10, y, 13);
		for (i = 0; i < n; i++)
			xs[i] = x;
		return true;
	}
	for (i = 0; i < n; i++)
	{
		xs[i] = dx;
		NG_DrawChoice (dx, y, opts[i], i == cur);
		if (i == cur && focused)
			Draw_Character (dx - 10, y, 13);
		dx += (int)strlen (opts[i]) * 8 + 16;
	}
	return false;
}

static qboolean ngi_show[4];	// which detail lines fit this frame

void M_NetGraph_Draw (void)
{
	const netdiag_t *nd = NetDiag_Data ();
	const nd_hitch_t *hitch;
	ng_view_t v;
	float s = NG_Scale (), lt, ft;
	int cw = (int)(glwidth / s), ch = (int)(glheight / s);
	int x0, y0, w, h, px, pw, y, i, c0, c1, cols, right, ctl_x, lane_h, traf_h, below, head_lines, detail_lines;
	char lines[4][160], top[24];
	const char *headline;
	static const char *disp[3] = {"OFF", "COMPACT", "DETAILED"};
	static const char *pos[4] = {"TOP LEFT", "TOP RIGHT", "BOTTOM LEFT", "BOTTOM RIGHT"};
	static const char *help = "{UP/DOWN}~row  {LEFT/RIGHT}~move  {CLICK}~pin  {ENTER}~whole~view  {F}~freeze  {R}~report  {ESC}~back";

	GL_SetCanvas (CANVAS_NETGRAPH);
	w = q_min (cw - 2 * (NG_MARGIN + NG_BACK_PAD), 760);
	h = q_min (ch - 2 * (NG_MARGIN + NG_BACK_PAD), 360);
	x0 = (cw - w) / 2;
	y0 = (ch - h) / 2;
	right = x0 + w - 8;
	cols = (w - 16) / 8;
	NG_Backdrop (x0, y0, w, h, 0.88f);

	if (w < 296 || h < 216)
	{
		NG_DrawWrapped (x0 + 8, y0 + 8, "Too small to inspect: enlarge the window or lower the HUD scale.", cols, 10, true);
		GL_SetCanvas (CANVAS_MENU);
		return;
	}

	y = y0 + NG_PAD + 4;
	Draw_StringMasked (x0 + 8, y, "NETWORK GRAPH");

	// sources wrap onto as many rows as they need
	NGI_BuildSources (nd);
	if (NGI_SourceIndex () < 0 && ngi.nsrcs)
		NGI_SelectSource (0);
	ctl_x = x0 + 8 + 16 * 8;
	{
		int sx = ctl_x;
		for (i = 0; i < ngi.nsrcs; i++)
		{
			qboolean sel = ngi.srcs[i].kind == ngi.src_kind && ngi.srcs[i].slot == ngi.src_slot;
			ngi.srcs[i].w = (int)strlen (ngi.srcs[i].label) * 8;
			if (sx > ctl_x && sx + ngi.srcs[i].w > right)
			{
				sx = ctl_x;
				y += 10;
			}
			ngi.srcs[i].x = sx;
			ngi.srcs[i].y = y;
			NG_DrawChoice (sx, y, ngi.srcs[i].label, sel);
			if (sel && ngi.focus == NGF_SOURCE)
				Draw_Character (sx - 10, y, 13);
			sx += ngi.srcs[i].w + 16;
		}
		if (!ngi.nsrcs)
			Draw_String (sx, y, "nothing recorded");
	}

	// display and position: the same choices as the netgraph command
	y += 14;
	ngi.disp_y = y;
	{	// recording without a panel is still recording: say so on the OFF choice
		const char *d[3];
		d[0] = (NetDiag_PanelMode () == 0 && netdiag_active) ? "OFF, RECORDING" : disp[0];
		d[1] = disp[1];
		d[2] = disp[2];
		ngi.disp_compact = NGI_ChoiceRow (ctl_x, right, y, "DISPLAY", d, 3, NetDiag_PanelMode (), ngi.disp_x, ngi.focus == NGF_DISPLAY);
	}
	y += 10;
	ngi.pos_y = y;
	ngi.pos_compact = NGI_ChoiceRow (ctl_x, right, y, "POSITION", pos, 4, NetDiag_PanelAnchor (), ngi.pos_x, ngi.focus == NGF_POSITION);
	y += 16;

	// the help line sits at the bottom, wrapped to the width
	{
		int hl = NG_DrawWrapped (0, 0, help, cols, 10, false);
		NG_DrawWrapped (x0 + 8, y0 + h - 4 - hl * 10, help, cols, 10, true);
		below = hl * 10 + 4;
	}

	if (!NGI_View (nd, &v, &hitch))
	{
		ngi.plot_w = 0;
		NG_DrawWrapped (x0 + 8, y + 8, "Nothing recorded yet. Turn the display on, or type: netgraph record", cols, 10, true);
		GL_SetCanvas (CANVAS_MENU);
		return;
	}

	// details first (to know their height), then the lanes get whatever is left
	px = x0 + 8 + NG_LABEL_W + 4;
	pw = q_min (w - (px - x0) - 8 - 72, NG_MAXCOLS);
	NG_Gather (&v, pw);
	if (ngi.cursor >= pw)
		ngi.cursor = pw - 1;
	if (!ngi.pinned && ngi.mouse_seen && ngi.mouse_moved && ngi.mouse_x >= px && ngi.mouse_x < px + pw &&
		ngi.mouse_y >= ngi.plot_y && ngi.mouse_y < ngi.plot_y + ngi.plot_h)
		ngi.cursor = ngi.mouse_x - px;	// the cursor follows the mouse unless pinned
	if (ngi.cursor >= 0)
		c0 = c1 = ngi.cursor;
	else
	{
		c0 = 0;
		c1 = pw - 1;
	}
	NGI_DetailLines (&v, c0, c1, lines);
	if (hitch)
		headline = hitch->m.sentence;
	else if (ngi.cursor < 0)
		headline = v.origin == v.n ? "The last 20 seconds" : "This capture";
	else
		headline = ngi.pinned ? "One moment (pinned)" : "One moment";
	head_lines = NG_DrawWrapped (0, 0, headline, cols, 10, false);
	{
		int n[4], text_rows, avail;
		static const int priority[4] = {0, 3, 1, 2};	// timing, then events, traffic, message mix
		for (detail_lines = 0, i = 0; i < 4; i++)
			detail_lines += n[i] = lines[i][0] ? NG_DrawWrapped (0, 0, lines[i], cols, 10, false) : 0;
		// lanes: 3 of lane_h, traffic of 2 * lane_h, gaps, rail, time axis
		avail = (y0 + h - below) - y - (head_lines * 10 + 4 + detail_lines * 10 + 4) - 14 - 19;
		lane_h = CLAMP (10, avail / 5, 44);
		traf_h = 2 * lane_h;
		// whatever doesn't fit under the lanes is dropped, least important first
		text_rows = ((y0 + h - below) - (y + 5 * lane_h + 19 + 14 + 4)) / 10 - head_lines;
		for (i = 0; i < 4; i++)
			ngi_show[priority[i]] = text_rows >= n[priority[i]] && (text_rows -= n[priority[i]], true);
	}
	ngi.plot_x = px;
	ngi.plot_w = pw;
	ngi.plot_y = y;
	ngi.plot_h = 3 * (lane_h + 3) + traf_h + 2 + 8;
	lt = nd ? ND_LateThresholdOf (nd) : 40;
	ft = nd ? ND_FrameThresholdOf (nd) : 50;
	NG_UpdateAxes (&ngi.axes, pw, realtime);

	NG_BeginGeometry ();
	NG_DrawLane (NGL_PING, pw, px, y, lane_h, &ngi.axes, lt, ft);
	NG_DrawLane (NGL_LATE, pw, px, y + (lane_h + 3), lane_h, &ngi.axes, lt, ft);
	NG_DrawLane (NGL_FRAME, pw, px, y + 2 * (lane_h + 3), lane_h, &ngi.axes, lt, ft);
	NG_DrawLane (NGL_TRAFFIC, pw, px, y + 3 * (lane_h + 3), traf_h, &ngi.axes, lt, ft);
	NG_DrawRail (pw, px, y + 3 * (lane_h + 3) + traf_h + 2);
	if (v.origin > 0 && v.origin < v.n)
	{
		float tx = px + (float)v.origin * pw / v.n;
		for (i = 0; i < ngi.plot_h; i += 3)	// dotted gold: where the hitch was caught
			NG_Quad (NGB_GOLD, tx, y + i, tx + 1, y + i + 1);
	}
	if (ngi.cursor >= 0)
	{	// gold: the moment you're pointing at; a cap at each end once it's pinned
		NG_Quad (NGB_GOLD, px + ngi.cursor, y - 2, px + ngi.cursor + 1, y + ngi.plot_h + 1);
		if (ngi.pinned)
		{
			NG_Quad (NGB_GOLD, px + ngi.cursor - 2, y - 3, px + ngi.cursor + 3, y - 2);
			NG_Quad (NGB_GOLD, px + ngi.cursor - 2, y + ngi.plot_h + 1, px + ngi.cursor + 3, y + ngi.plot_h + 2);
		}
	}
	NG_EndGeometry ();
	NG_DrawRailLetters (pw, px, y + 3 * (lane_h + 3) + traf_h + 2);

	{	// labels centred on their lanes
		const int ty = lane_h / 2 - 4;
		Draw_String (x0 + 8, y + ty, (nd && nd->last_cmdrtt_time >= 0) ? "RTT" : "PING");
		Draw_String (x0 + 8, y + (lane_h + 3) + ty, "LATE");
		Draw_String (x0 + 8, y + 2 * (lane_h + 3) + ty, "FRAME");
		Draw_String (x0 + 8, y + 3 * (lane_h + 3) + traf_h / 4 - 4, "IN");
		Draw_String (x0 + 8, y + 3 * (lane_h + 3) + traf_h * 3 / 4 - 4, "OUT");
	}
	// each lane's ceiling, so heights read as numbers
	q_snprintf (top, sizeof(top), "%dms", (int)ngi.axes.ping.top);
	Draw_String (px + pw + 6, y, top);
	q_snprintf (top, sizeof(top), "%dms", (int)ngi.axes.late.top);
	Draw_String (px + pw + 6, y + (lane_h + 3), top);
	q_snprintf (top, sizeof(top), "%dms", (int)ngi.axes.frame.top);
	Draw_String (px + pw + 6, y + 2 * (lane_h + 3), top);
	NG_FormatRate (top, sizeof(top), ngi.axes.rate.top, true);
	Draw_String (px + pw + 6, y + 3 * (lane_h + 3), top);
	q_snprintf (top, sizeof(top), "%+.0fs", (0 - v.origin) * ND_BUCKET_SEC);
	Draw_String (px, y + ngi.plot_h + 2, top);
	q_snprintf (top, sizeof(top), v.origin == v.n ? "now" : "%+.0fs", (v.n - v.origin) * ND_BUCKET_SEC);
	NG_DrawRight (px + pw, y + ngi.plot_h + 2, top, false);
	{	// what the rail's marks mean, between the time labels when there's room
		static const char *legend = "{L} lost  {R} resent  {B} backlog  {M} mark  {_} ran out of updates";
		const int lw = 57 * 8;	// visible width of the legend
		if (pw - 2 * 7 * 8 >= lw)
			NG_DrawMarked (px + (pw - lw) / 2, y + ngi.plot_h + 2, legend);
	}

	// one headline, then the numbers, all wrapped to the width
	y += ngi.plot_h + 14;
	y += NG_DrawWrapped (x0 + 8, y, headline, cols, 10, true) * 10 + 4;
	for (i = 0; i < 4; i++)
		if (ngi_show[i] && lines[i][0])
			y += NG_DrawWrapped (x0 + 8, y, lines[i], cols, 10, true) * 10;

	GL_SetCanvas (CANVAS_MENU);
}

// pinning a live view freezes it first, so the pinned column keeps meaning the same moment
static void NGI_Pin (int column)
{
	if (ngi.src_kind == NGS_LIVE)
		NGI_Freeze ();
	ngi.cursor = column;
	ngi.pinned = true;
}

void M_NetGraph_Key (int key)
{
	int i, step = keydown[K_SHIFT] ? 10 : 1;

	switch (key)
	{
	case K_ESCAPE:
	case K_BBUTTON:
	case K_MOUSE4:
	case K_MOUSE2:
		NGI_Close ();
		return;
	case K_UPARROW:
		ngi.focus = (ngi.focus + NGF_COUNT - 1) % NGF_COUNT;
		S_LocalSound ("misc/menu1.wav");
		return;
	case K_DOWNARROW:
	case K_TAB:
		ngi.focus = (ngi.focus + 1) % NGF_COUNT;
		S_LocalSound ("misc/menu1.wav");
		return;
	case 'f':
	case 'F':
		NGI_Freeze ();
		return;
	case 'r':
	case 'R':
		Cbuf_AddText ("netgraph report\n");
		return;
	case K_MOUSE1:
		if (!ngi.mouse_seen)
			return;
		for (i = 0; i < ngi.nsrcs; i++)
			if (ngi.mouse_y >= ngi.srcs[i].y - 2 && ngi.mouse_y < ngi.srcs[i].y + 10 &&
				ngi.mouse_x >= ngi.srcs[i].x - 4 && ngi.mouse_x < ngi.srcs[i].x + ngi.srcs[i].w + 4)
			{
				NGI_SelectSource (i);
				ngi.focus = NGF_SOURCE;
				return;
			}
		if (ngi.mouse_y >= ngi.disp_y - 1 && ngi.mouse_y < ngi.disp_y + 9)
		{
			ngi.focus = NGF_DISPLAY;
			if (ngi.disp_compact)
				Cbuf_AddText (ngi_display_cmd[(NetDiag_PanelMode () + 1) % 3]);
			else
				for (i = 2; i >= 0; i--)
					if (ngi.mouse_x >= ngi.disp_x[i] - 4)
					{
						Cbuf_AddText (ngi_display_cmd[i]);
						break;
					}
		}
		else if (ngi.mouse_y >= ngi.pos_y - 1 && ngi.mouse_y < ngi.pos_y + 9)
		{
			ngi.focus = NGF_POSITION;
			if (ngi.pos_compact)
				Cbuf_AddText (ngi_position_cmd[(NetDiag_PanelAnchor () + 1) % 4]);
			else
				for (i = 3; i >= 0; i--)
					if (ngi.mouse_x >= ngi.pos_x[i] - 4)
					{
						Cbuf_AddText (ngi_position_cmd[i]);
						break;
					}
		}
		else if (ngi.plot_w > 0 && ngi.mouse_x >= ngi.plot_x && ngi.mouse_x < ngi.plot_x + ngi.plot_w &&
			ngi.mouse_y >= ngi.plot_y && ngi.mouse_y < ngi.plot_y + ngi.plot_h)
		{
			ngi.focus = NGF_GRAPH;
			if (ngi.pinned)
				ngi.pinned = false;
			else
				NGI_Pin (ngi.mouse_x - ngi.plot_x);
		}
		return;
	}

	switch (ngi.focus)
	{
	case NGF_SOURCE:
		if ((key == K_LEFTARROW || key == K_RIGHTARROW) && ngi.nsrcs)
			NGI_SelectSource ((NGI_SourceIndex () + (key == K_LEFTARROW ? ngi.nsrcs - 1 : 1)) % ngi.nsrcs);
		break;
	case NGF_DISPLAY:
		if (key == K_LEFTARROW || key == K_RIGHTARROW || key == K_ENTER || key == K_KP_ENTER)
			Cbuf_AddText (ngi_display_cmd[(NetDiag_PanelMode () + (key == K_LEFTARROW ? 2 : 1)) % 3]);
		break;
	case NGF_POSITION:
		if (key == K_LEFTARROW || key == K_RIGHTARROW || key == K_ENTER || key == K_KP_ENTER)
			Cbuf_AddText (ngi_position_cmd[(NetDiag_PanelAnchor () + (key == K_LEFTARROW ? 3 : 1)) % 4]);
		break;
	default:
		if (ngi.plot_w <= 0)
			break;
		if (key == K_LEFTARROW || key == K_RIGHTARROW || key == K_MWHEELUP || key == K_MWHEELDOWN)
		{
			int dir = (key == K_LEFTARROW || key == K_MWHEELDOWN) ? -1 : 1;
			NGI_Pin (ngi.cursor < 0 ? (dir < 0 ? ngi.plot_w - 1 : 0) : CLAMP (0, ngi.cursor + dir * step, ngi.plot_w - 1));
		}
		else if (key == K_HOME || key == K_END)
			NGI_Pin (key == K_HOME ? 0 : ngi.plot_w - 1);
		else if (key == K_ENTER || key == K_KP_ENTER || key == K_DEL || key == K_BACKSPACE)
		{	// back to the whole-view summary; it stays until the mouse moves again
			ngi.cursor = -1;
			ngi.pinned = false;
			ngi.mouse_moved = false;
		}
		break;
	}
}

// menu canvas coordinates in; netgraph canvas coordinates kept for the next draw
void M_NetGraph_Mousemove (int cx, int cy)
{
	float sm = q_min ((float)glwidth / 320.0f, (float)glheight / 200.0f), sn = NG_Scale ();
	int mx, my;
	sm = CLAMP (1.0f, scr_menuscale.value, sm);
	mx = (int)(((glwidth - 320 * sm) / 2 + cx * sm) / sn);
	my = (int)(((glheight - 200 * sm) / 2 + cy * sm) / sn);
	if (ngi.mouse_seen && (mx != ngi.mouse_x || my != ngi.mouse_y))
		ngi.mouse_moved = true;
	ngi.mouse_x = mx;
	ngi.mouse_y = my;
	ngi.mouse_seen = true;
}

#endif // !NETGRAPH_STANDALONE
