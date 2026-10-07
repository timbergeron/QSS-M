/*
test_netdiag.c -- standalone tests for the netgraph collector core  // woods #netgraph

Builds the collector core of Quake/netgraph.c on its own (NETGRAPH_STANDALONE) and drives
it with simulated server/client timelines. Exit status 0 = all passed.

	make -C Misc/netdiag_test
*/

#include "../../Quake/netgraph.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf ("FAIL %s:%d: ", __FILE__, __LINE__); printf (__VA_ARGS__); printf ("\n"); } } while (0)
#define NEAR(a, b, tol) (fabs ((double)(a) - (double)(b)) <= (tol))

/*
A tiny world: the server sends one 100-byte unreliable snapshot per tick,
stamped with its own clock. The client runs frames; each frame reads every
datagram that has arrived, parses it, then presents.
*/
#define MAXPKT 20000

typedef struct
{
	double	arrive;		// when it reaches our socket
	double	svtime;		// server timestamp inside
	int		seq;
	int		bytes;
	int		dropped;
} pkt_t;

typedef struct
{
	netdiag_t	*nd;
	pkt_t		pkts[MAXPKT];
	int			npkts;
	int			next;			// next packet not yet read
	int			expect_seq;
	double		now;			// client clock
	double		frame_ms;		// normal frame length
	int			starve_next;
} sim_t;

static sim_t *Sim_New (double tick_hz, double seconds, double frame_ms)
{
	sim_t *s = (sim_t *) calloc (1, sizeof(*s));
	int i;
	s->nd = (netdiag_t *) calloc (1, sizeof(netdiag_t));
	ND_Reset (s->nd);
	ND_SetSource (s->nd, ND_SRC_NET);
	s->npkts = (int)(tick_hz * seconds);
	if (s->npkts > MAXPKT)
		s->npkts = MAXPKT;
	for (i = 0; i < s->npkts; i++)
	{
		s->pkts[i].svtime = 100.0 + i / tick_hz;
		s->pkts[i].arrive = 1000.0 + i / tick_hz + 0.030;	// 30 ms one-way, constant
		s->pkts[i].seq = i;
		s->pkts[i].bytes = 100;
	}
	s->now = 1000.0;
	s->frame_ms = frame_ms;
	return s;
}

static void Sim_Free (sim_t *s)
{
	free (s->nd);
	free (s);
}

// one client frame lasting `ms`: read what has arrived, then present at the end
static void Sim_Frame (sim_t *s, double ms, int cond)
{
	double t = s->now;
	ND_ReadPass (s->nd, t);
	while (s->next < s->npkts && s->pkts[s->next].arrive <= t)
	{
		pkt_t *p = &s->pkts[s->next++];
		int missing;
		if (p->dropped)
			continue;
		t += 0.00001;	// reads take a moment
		ND_Packet (s->nd, t, ND_IN, ND_UNREL, p->bytes);
		missing = p->seq - s->expect_seq;
		s->expect_seq = p->seq + 1;
		ND_Arrival (s->nd, t, missing);
		ND_Svc (s->nd, 7, 0);			// svc_time
		ND_ServerTime (s->nd, p->svtime);
		ND_Svc (s->nd, ND_SVC_FAST, 6);	// entity updates
		ND_Svc (s->nd, -1, p->bytes);
		ND_MessageDone (s->nd);
	}
	if (s->starve_next)
	{
		ND_Starved (s->nd);
		s->starve_next = 0;
	}
	s->now += ms / 1000.0;
	ND_Frame (s->nd, s->now, cond);
}

static void Sim_Run (sim_t *s, double seconds)
{
	double end = s->now + seconds;
	while (s->now < end)
		Sim_Frame (s, s->frame_ms, 0);
}

static int HitchCount (const netdiag_t *nd)
{
	return ND_HitchCount (nd, 0);
}

static const nd_hitch_t *FirstHitch (const netdiag_t *nd)
{
	int i;
	for (i = 0; i < ND_HITCH_SLOTS; i++)
		if (nd->hitches[i].m.used)
			return &nd->hitches[i];
	return NULL;
}

/*
=============
Tests
=============
*/
static void Test_Clean (void)
{
	sim_t *s = Sim_New (72, 12, 1000.0 / 144);
	nd_summary_t w;
	Sim_Run (s, 10);
	ND_Summarize (s->nd, ND_WINDOW_BUCKETS, &w);
	CHECK (NEAR (w.in_bps, 7200, 250), "clean: in_bps %.0f, expected ~7200", w.in_bps);
	CHECK (NEAR (w.in_pps, 72, 3), "clean: in_pps %.1f", w.in_pps);
	CHECK (w.loss_pct == 0, "clean: loss %.2f", w.loss_pct);
	CHECK (NEAR (w.update_hz, 72, 1), "clean: updates %.1f/s", w.update_hz);
	CHECK (fabs (w.late_avg_ms) < 3, "clean: late avg %.2f", w.late_avg_ms);
	CHECK (w.late_net_max_ms < 10, "clean: late net max %.2f", w.late_net_max_ms);
	CHECK (NEAR (w.frame_avg_ms, 1000.0 / 144, 0.5), "clean: frame avg %.2f", w.frame_avg_ms);
	CHECK (HitchCount (s->nd) == 0, "clean: %d hitches", HitchCount (s->nd));
	CHECK (NEAR (w.coverage_s, 5.0, 0.05), "clean: coverage %.2f", w.coverage_s);
	CHECK (NEAR (w.avg_pkt[ND_IN], 100, 0.01) && w.max_pkt[ND_IN] == 100, "clean: pkt size %.1f/%d", w.avg_pkt[ND_IN], w.max_pkt[ND_IN]);
	{	// message mix: svc_time 6 bytes, entities 94
		const nd_session_t *ss = &s->nd->s;
		CHECK (ss->svc_bytes[7] == 6ull * ss->svc_count[7] && ss->svc_bytes[ND_SVC_FAST] == 94ull * ss->svc_count[ND_SVC_FAST],
			"clean: svc bytes %llu/%llu", (unsigned long long)ss->svc_bytes[7], (unsigned long long)ss->svc_bytes[ND_SVC_FAST]);
	}
	Sim_Free (s);
}

static void Test_Drop (void)
{
	sim_t *s = Sim_New (72, 20, 1000.0 / 144);
	nd_summary_t w;
	int i, base = 72 * 3;
	const nd_hitch_t *h;
	for (i = 0; i < 3; i++)
		s->pkts[base + i].dropped = 1;	// three in a row
	s->pkts[72 * 9].dropped = 1;		// a lone one later
	Sim_Run (s, 14);
	CHECK (s->nd->s.missing == 4, "drop: missing %u, expected 4", s->nd->s.missing);
	CHECK (s->nd->s.gaps == 2, "drop: gaps %u, expected 2", s->nd->s.gaps);
	CHECK (fabs (s->nd->s.late_sum / s->nd->s.lates) < 3, "drop: lateness polluted by loss (avg %.2f)", s->nd->s.late_sum / s->nd->s.lates);
	CHECK (HitchCount (s->nd) == 1, "drop: %d hitches, expected 1 (the 3-in-a-row)", HitchCount (s->nd));
	h = FirstHitch (s->nd);
	if (h)
	{
		CHECK (h->m.kind == ND_HITCH_LOSS, "drop: kind %d", h->m.kind);
		CHECK (strstr (h->m.sentence, "3 packets lost") != NULL, "drop: sentence \"%s\"", h->m.sentence);
	}
	ND_Summarize (s->nd, ND_RING, &w);
	CHECK (NEAR (w.loss_pct, 100.0 * 4 / (w.accepted + 4), 0.001), "drop: loss pct %.3f", w.loss_pct);
	Sim_Free (s);
}

static void Test_Delay (void)
{
	sim_t *s = Sim_New (72, 20, 1000.0 / 144);
	const nd_hitch_t *h;
	int i, k = 72 * 5;
	for (i = 0; i < 8; i++)	// one 100 ms delay: packets during it arrive together after
		if (s->pkts[k + i].arrive < s->pkts[k].arrive + 0.100)
			s->pkts[k + i].arrive = s->pkts[k].arrive + 0.100;
	Sim_Run (s, 14);
	CHECK (HitchCount (s->nd) == 1, "delay: %d hitches", HitchCount (s->nd));
	h = FirstHitch (s->nd);
	if (h)
	{
		CHECK (h->m.kind == ND_HITCH_LATE, "delay: kind %d", h->m.kind);
		CHECK (NEAR (h->m.late_ms, 100, 10), "delay: late %.1f, expected ~100", h->m.late_ms);
		CHECK (strstr (h->m.sentence, "Updates arrived") == h->m.sentence, "delay: sentence \"%s\"", h->m.sentence);
		CHECK (h->m.frame_ms < 10, "delay: frames should be steady, peak %.1f", h->m.frame_ms);
	}
	Sim_Free (s);
}

static void Test_Stall (void)
{
	sim_t *s = Sim_New (72, 20, 1000.0 / 144);
	const nd_hitch_t *h;
	Sim_Run (s, 5);
	Sim_Frame (s, 150, 0);	// one 150 ms client stall; packets queue meanwhile
	Sim_Run (s, 6);
	CHECK (HitchCount (s->nd) == 1, "stall: %d hitches", HitchCount (s->nd));
	h = FirstHitch (s->nd);
	if (h)
	{
		CHECK (h->m.kind == ND_HITCH_FRAME, "stall: kind %d (late net %.1f)", h->m.kind, h->m.late_ms);
		CHECK (NEAR (h->m.frame_ms, 150, 1), "stall: frame %.1f", h->m.frame_ms);
		CHECK (strstr (h->m.sentence, "no network delay detected") != NULL, "stall: sentence \"%s\"", h->m.sentence);
	}
	CHECK (s->nd->s.frames_over_100 == 1, "stall: frames over 100 = %u", s->nd->s.frames_over_100);
	Sim_Free (s);
}

static void Test_Merge (void)
{
	sim_t *s = Sim_New (72, 30, 1000.0 / 144);
	const nd_hitch_t *h;
	Sim_Run (s, 3);
	Sim_Frame (s, 80, 0);
	Sim_Run (s, 2);
	Sim_Frame (s, 200, 0);	// worse, 2 s later: same hitch, re-centred
	Sim_Run (s, 8);
	CHECK (HitchCount (s->nd) == 1, "merge: %d hitches", HitchCount (s->nd));
	h = FirstHitch (s->nd);
	if (h)
		CHECK (NEAR (h->m.frame_ms, 200, 1), "merge: kept %.1f, expected the 200 ms one", h->m.frame_ms);
	Sim_Free (s);
}

static void Test_Slots (void)
{
	sim_t *s = Sim_New (72, 120, 1000.0 / 144);
	int i, n;
	float minsev = 1e9f;
	const float stalls[6] = {60, 300, 70, 250, 90, 400};
	Sim_Run (s, 2);
	for (i = 0; i < 6; i++)
	{
		Sim_Frame (s, stalls[i], 0);
		Sim_Run (s, 12);
	}
	n = HitchCount (s->nd);
	CHECK (n == 4, "slots: %d hitches", n);
	for (i = 0; i < ND_HITCH_SLOTS; i++)
		if (s->nd->hitches[i].m.used && s->nd->hitches[i].m.severity < minsev)
			minsev = s->nd->hitches[i].m.severity;
	CHECK (minsev >= 89, "slots: weakest kept %.1f, expected the four worst (>= 90)", minsev);
	Sim_Free (s);
}

static void Test_Pause (void)
{
	sim_t *s = Sim_New (72, 20, 1000.0 / 144);
	int i;
	Sim_Run (s, 3);
	for (i = 0; i < 300; i++)	// 2 s paused; the server stops sending
		Sim_Frame (s, 1000.0 / 144, ND_COND_PAUSED);
	// resume: server clock continues after the pause gap
	for (i = s->next; i < s->npkts; i++)
	{
		s->pkts[i].arrive += 2.08;
		s->pkts[i].svtime += 2.08;
	}
	Sim_Run (s, 8);
	CHECK (HitchCount (s->nd) == 0, "pause: %d hitches (a pause must not look like lag)", HitchCount (s->nd));
	Sim_Free (s);
}

static void Test_Mark (void)
{
	sim_t *s = Sim_New (72, 20, 1000.0 / 144);
	Sim_Run (s, 7);
	ND_Mark (s->nd, s->now);
	Sim_Run (s, 3);
	CHECK (ND_HitchCount (s->nd, 1) == 0, "mark: captured too early");
	Sim_Run (s, 1.5);
	CHECK (ND_HitchCount (s->nd, 1) == 1, "mark: not captured");
	if (s->nd->marks[0].m.used)
		CHECK (strstr (s->nd->marks[0].m.sentence, "Marked") == s->nd->marks[0].m.sentence, "mark: sentence \"%s\"", s->nd->marks[0].m.sentence);
	Sim_Free (s);
}

static void Test_Clock (void)
{
	netdiag_t *nd = (netdiag_t *) calloc (1, sizeof(*nd));
	nd_summary_t w;
	int i;
	ND_Reset (nd);
	ND_SetSource (nd, ND_SRC_NET);
	for (i = 0; i < 100; i++)
		ND_Frame (nd, 10.0 + i * 0.01, 0);
	ND_Frame (nd, 9.0, 0);			// clock went backwards
	ND_Frame (nd, 9.01, 0);
	ND_Summarize (nd, ND_RING, &w);
	CHECK (w.frame_max_ms < 20, "clock: backwards produced a %.1f ms frame", w.frame_max_ms);
	ND_Frame (nd, 9.0 + 3600, 0);	// an hour later (sleep): longer than the ring
	ND_Frame (nd, 9.01 + 3600, 0);
	ND_Frame (nd, 9.02 + 3600, 0);
	CHECK (nd->cur > ND_RING, "clock: didn't advance");
	ND_Frame (nd, 1e308, 0);		// nonsense time must not crash or wrap
	ND_Frame (nd, -1.0 / 0.0, 0);
	CHECK (1, "clock: survived");
	free (nd);
}

static void Test_Reset (void)
{
	sim_t *s = Sim_New (72, 10, 1000.0 / 144);
	Sim_Run (s, 2);
	s->pkts[s->next + 5].seq += 5000;	// sequence jump: a reset, not 5000 losses
	{
		int i;
		for (i = s->next + 6; i < s->npkts; i++)
			s->pkts[i].seq += 5000;
	}
	Sim_Run (s, 3);
	CHECK (s->nd->s.missing == 0, "reset: counted %u missing", s->nd->s.missing);
	CHECK (s->nd->s.resets == 1, "reset: resets %u", s->nd->s.resets);
	Sim_Free (s);
}

static void Test_PingCmd (void)
{
	sim_t *s = Sim_New (72, 10, 1000.0 / 144);
	Sim_Run (s, 1);
	ND_Ping (s->nd, s->now, 45, -1);
	Sim_Run (s, 3);
	CHECK (s->nd->s.ping_reports == 1, "ping: one report became %u samples", s->nd->s.ping_reports);
	CHECK (s->nd->last_moveloss == -1, "ping: absent move loss stored as %d", s->nd->last_moveloss);
	ND_Ping (s->nd, s->now, 50, 0);
	CHECK (s->nd->last_moveloss == 0, "ping: explicit zero lost");

	ND_CmdSent (s->nd, 10, s->now);
	ND_CmdSent (s->nd, 11, s->now + 0.007);
	ND_CmdAck (s->nd, 11, s->now + 0.057);
	CHECK (NEAR (s->nd->last_cmdrtt_ms, 50, 0.01), "cmdrtt: %.2f", s->nd->last_cmdrtt_ms);
	ND_CmdAck (s->nd, 10, s->now + 0.06);	// older ack: ignored
	CHECK (s->nd->s.cmdrtt_samples == 1, "cmdrtt: samples %u", s->nd->s.cmdrtt_samples);
	Sim_Free (s);
}

static void Test_Status (void)
{
	sim_t *s = Sim_New (72, 10, 1000.0 / 144);
	nd_info_t info;
	char buf[1024];
	const char *keys[] = {"v=2", " state=live", " src=net", " updates_hz=7", " ping=--", " cmdrtt=--", " late_ms=",
		" jitter_ms=", " frame_ms=6.9", " loss_pct=0.00", " in_bps=7", " hitches=0", " rec_s="};
	int i;
	Sim_Run (s, 6);
	memset (&info, 0, sizeof(info));
	info.recording = 1;
	info.connected = 1;
	info.protocol = "666";
	info.extensions = "";
	info.silence_s = 0;
	ND_FormatStatus (s->nd, &info, s->now, buf, sizeof(buf));
	for (i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++)
		CHECK (strstr (buf, keys[i]) != NULL, "status: missing \"%s\" in: %s", keys[i], buf);
	info.recording = 0;
	CHECK (!strcmp (ND_StateName (s->nd, &info), "off"), "status: off state");
	{
		FILE *f = tmpfile ();
		long size;
		ND_WriteReportJSON (s->nd, &info, s->now, f);
		size = ftell (f);
		CHECK (size > 200, "json: %ld bytes", size);
		fclose (f);
		f = tmpfile ();
		ND_WriteReportText (s->nd, &info, s->now, f);
		CHECK (ftell (f) > 200, "text: short report");
		fclose (f);
		f = tmpfile ();
		ND_WriteReportCSV (s->nd, f);
		CHECK (ftell (f) > 1000, "csv: short");
		fclose (f);
	}
	Sim_Free (s);
}

#include <time.h>
static double Now (void)
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// rough per-event cost, for the performance budget (not a pass/fail check)
static void Bench (void)
{
	netdiag_t *nd = (netdiag_t *) calloc (1, sizeof(*nd));
	const int n = 2000000;
	double t0, t1, t = 1000.0;
	int i;
	ND_Reset (nd);
	ND_SetSource (nd, ND_SRC_NET);
	t0 = Now ();
	for (i = 0; i < n; i++)
	{	// one 62.5 Hz packet's worth of events
		t += 0.016;
		ND_ReadPass (nd, t);
		ND_Packet (nd, t, ND_IN, ND_UNREL, 120);
		ND_Arrival (nd, t, 0);
		ND_Svc (nd, 7, 0);
		ND_ServerTime (nd, t - 900);
		ND_Svc (nd, -1, 120);
		ND_MessageDone (nd);
		ND_Packet (nd, t, ND_OUT, ND_UNREL, 40);
		ND_Frame (nd, t + 0.001, 0);
	}
	t1 = Now ();
	printf ("bench: %.0f ns per packet+frame (9 events incl. bucket roll-over and detection)\n", (t1 - t0) / n * 1e9);
	free (nd);
}

// review fixes (2026-10-04)

static void Test_OffOn (void)
{	// recording off for 2 s then on again must not become a 2 s "frame"
	sim_t *s = Sim_New (72, 30, 1000.0 / 144);
	Sim_Run (s, 5);
	s->now += 2.0;		// nothing recorded meanwhile
	while (s->next < s->npkts && s->pkts[s->next].arrive <= s->now)
		s->expect_seq = s->pkts[s->next++].seq + 1;	// the engine keeps sequencing while we don't record
	ND_Break (s->nd, s->now);
	{
		double before = s->nd->s.rec_time;
		Sim_Run (s, 8);
		CHECK (HitchCount (s->nd) == 0, "offon: %d hitches after a recording pause", HitchCount (s->nd));
		CHECK (s->nd->s.frames_over_100 == 0, "offon: the pause became a frame");
		CHECK (s->nd->s.rec_time - before < 8.5, "offon: the pause counted as recorded time (%.1f s for 8 s)", s->nd->s.rec_time - before);
	}
	Sim_Free (s);
}

static void Test_FinishPending (void)
{	// a hitch 1 s before the session ends is saved, with the missing "after" marked
	sim_t *s = Sim_New (72, 20, 1000.0 / 144);
	Sim_Run (s, 5);
	Sim_Frame (s, 150, 0);
	Sim_Run (s, 1);
	CHECK (HitchCount (s->nd) == 0, "finish: captured too early");
	ND_Finish (s->nd);
	CHECK (HitchCount (s->nd) == 1, "finish: pending hitch lost at session end");
	if (FirstHitch (s->nd))
		CHECK (FirstHitch (s->nd)->window[ND_HITCH_BUCKETS - 1].flags & ND_BF_DISCONT, "finish: missing context not marked");
	Sim_Free (s);
}

static void Test_LocalSentence (void)
{	// no network samples: the sentence mustn't talk about updates
	netdiag_t *nd = (netdiag_t *) calloc (1, sizeof(*nd));
	int i;
	ND_Reset (nd);
	ND_SetSource (nd, ND_SRC_LOCAL);
	for (i = 0; i < 600; i++)
		ND_Frame (nd, 10.0 + i * 0.007, 0);
	ND_Frame (nd, 10.0 + 600 * 0.007 + 0.2, 0);
	for (i = 0; i < 800; i++)
		ND_Frame (nd, 14.5 + i * 0.007, 0);
	CHECK (ND_HitchCount (nd, 0) == 1, "local: %d hitches", ND_HitchCount (nd, 0));
	if (nd->hitches[0].m.used || nd->hitches[1].m.used)
	{
		const nd_hitch_t *h = nd->hitches[0].m.used ? &nd->hitches[0] : &nd->hitches[1];
		CHECK (!strstr (h->m.sentence, "update") && !strstr (h->m.sentence, "network"), "local: sentence claims network facts: \"%s\"", h->m.sentence);
		CHECK (h->m.late_ms < 0, "local: missing lateness became %.0f", h->m.late_ms);
	}
	free (nd);
}

static void Test_MessageTotal (void)
{	// four equal message types: the bucket keeps three, but the total keeps all four
	netdiag_t *nd = (netdiag_t *) calloc (1, sizeof(*nd));
	const nd_bucket_t *b;
	ND_Reset (nd);
	ND_SetSource (nd, ND_SRC_NET);
	ND_Frame (nd, 1.0, 0);
	ND_Svc (nd, 1, 0);
	ND_Svc (nd, 2, 100);
	ND_Svc (nd, 3, 200);
	ND_Svc (nd, 4, 300);
	ND_Svc (nd, -1, 400);
	ND_Frame (nd, 1.05, 0);
	b = ND_Bucket (nd, 0);
	CHECK (b && b->svc_total == 400, "svc: total %u, expected 400", b ? b->svc_total : 0);
	CHECK (b && b->top_svc_bytes[0] + b->top_svc_bytes[1] + b->top_svc_bytes[2] == 300, "svc: top three hold 300");
	free (nd);
}

static void Test_UpdatesUnderLoss (void)
{	// every second snapshot lost: UPDATES reports what arrived (36/s); loss is reported on its own
	sim_t *s = Sim_New (72, 12, 1000.0 / 144);
	nd_summary_t w;
	int i;
	for (i = 1; i < s->npkts; i += 2)
		s->pkts[i].dropped = 1;
	Sim_Run (s, 10);
	ND_Summarize (s->nd, ND_WINDOW_BUCKETS, &w);
	CHECK (NEAR (w.update_hz, 36, 1.5), "updates: %.1f/s with half the snapshots lost, expected 36 received", w.update_hz);
	CHECK (NEAR (w.loss_pct, 50, 2), "updates: loss %.1f%%, expected 50", w.loss_pct);
	Sim_Free (s);
}

static void Test_SplitSnapshots (void)
{	// every snapshot in two datagrams, the second always lost: 72 snapshots/s still arrive
	sim_t *s = Sim_New (72, 12, 1000.0 / 144);
	nd_summary_t w;
	int i;
	for (i = 0; i < s->npkts; i++)
		s->pkts[i].seq = i * 2;	// the lost second fragment takes every odd sequence number
	Sim_Run (s, 10);
	ND_Summarize (s->nd, ND_WINDOW_BUCKETS, &w);
	CHECK (NEAR (w.update_hz, 72, 1.5), "split: %.1f updates/s, expected 72 (lost fragments aren't lost snapshots)", w.update_hz);
	Sim_Free (s);
}

static void Test_Coverage30 (void)
{	// 30 fps leaves some 20 ms buckets without a frame; they are still recorded time
	sim_t *s = Sim_New (72, 14, 1000.0 / 30);
	nd_summary_t w;
	Sim_Run (s, 12);
	CHECK (NEAR (s->nd->s.rec_time, 12, 0.15), "coverage: %.2f s recorded of 12 at 30 fps", s->nd->s.rec_time);
	ND_Summarize (s->nd, ND_WINDOW_BUCKETS, &w);
	CHECK (NEAR (w.coverage_s, 5, 0.05), "coverage: window %.2f s", w.coverage_s);
	Sim_Free (s);
}

static void Test_GapUnmeasured (void)
{	// recording off for 3 s: those buckets are unmeasured, not zero traffic
	sim_t *s = Sim_New (72, 30, 1000.0 / 144);
	nd_summary_t w;
	Sim_Run (s, 6);
	s->now += 3.0;
	while (s->next < s->npkts && s->pkts[s->next].arrive <= s->now)
		s->expect_seq = s->pkts[s->next++].seq + 1;
	ND_Break (s->nd, s->now);
	Sim_Run (s, 1);
	ND_Summarize (s->nd, ND_WINDOW_BUCKETS, &w);
	CHECK (NEAR (w.coverage_s, 2.0, 0.1), "gap: window covers %.2f s (5 s minus the 3 s unmeasured)", w.coverage_s);
	CHECK (NEAR (w.in_bps, 7200, 400), "gap: rate %.0f B/s from measured time only", w.in_bps);
	Sim_Free (s);
}

static void Test_FinishLastBucket (void)
{	// a stall in the very last bucket, then the session ends at once
	sim_t *s = Sim_New (72, 20, 1000.0 / 144);
	Sim_Run (s, 5);
	Sim_Frame (s, 150, 0);
	ND_Finish (s->nd);
	CHECK (HitchCount (s->nd) == 1, "finish-last: %d hitches, expected the final stall", HitchCount (s->nd));
	Sim_Free (s);
}

static void Test_AckRestart (void)
{	// a short map restart: move sequence numbers start over below the last ack
	netdiag_t *nd = (netdiag_t *) calloc (1, sizeof(*nd));
	ND_Reset (nd);
	ND_SetSource (nd, ND_SRC_NET);
	ND_CmdSent (nd, 100, 1.0);
	ND_CmdAck (nd, 100, 1.05);
	ND_CmdSent (nd, 3, 2.0);
	ND_CmdAck (nd, 3, 2.04);
	CHECK (NEAR (nd->last_cmdrtt_ms, 40, 0.01) && nd->s.cmdrtt_samples == 2, "ack: restart ignored (rtt %.1f, %u samples)", nd->last_cmdrtt_ms, nd->s.cmdrtt_samples);
	free (nd);
}

static void Test_Freshness (void)
{	// a 90 s old ping and round trip aren't current
	sim_t *s = Sim_New (72, 200, 1000.0 / 144);
	nd_info_t info;
	char buf[1024];
	Sim_Run (s, 2);
	ND_Ping (s->nd, s->now, 45, -1);
	ND_CmdSent (s->nd, 10, s->now);
	ND_CmdAck (s->nd, 10, s->now + 0.05);
	Sim_Run (s, 90);
	memset (&info, 0, sizeof(info));
	info.recording = info.connected = 1;
	ND_FormatStatus (s->nd, &info, s->now, buf, sizeof(buf));
	CHECK (strstr (buf, " ping=--") && strstr (buf, " cmdrtt=--"), "fresh: stale values shown as current: %s", buf);
	Sim_Free (s);
}

static void Test_CSVRoundTrip (void)
{	// export then replay: every field survives exactly
	sim_t *s = Sim_New (72, 20, 1000.0 / 144);
	nd_bucket_t *back = (nd_bucket_t *) calloc (ND_RING, sizeof(nd_bucket_t));
	FILE *f = tmpfile ();
	int n, i, bad = 0;
	int64_t first;
	s->pkts[72 * 4].dropped = 1;
	Sim_Run (s, 6);
	ND_Ping (s->nd, s->now, 45, -1);
	Sim_Run (s, 1);
	ND_WriteReportCSV (s->nd, f);
	rewind (f);
	n = ND_ReadCSV (f, "live", 0, back, ND_RING);
	fclose (f);
	first = (int64_t)ND_LastComplete (s->nd) - n + 1;
	CHECK (n > 300, "csv: %d rows read back", n);
	for (i = 0; i < n; i++)
	{
		nd_bucket_t a = *ND_Bucket (s->nd, (uint32_t)(first + i)), b = back[i];
		a.index = b.index = 0;
		if (!a.lates)	// "none" cells
			a.late_max_ms = a.late_net_max_ms = b.late_max_ms = b.late_net_max_ms = 0;
		if (memcmp (&a, &b, sizeof(a)))
			bad++;
	}
	CHECK (bad == 0, "csv: %d of %d buckets differ after export and replay", bad, n);
	free (back);
	Sim_Free (s);
}

// third review (2026-10-07)

static void Test_StallKeepsThreshold (void)
{	// a 2 s client stall must not raise the bar for a later 120 ms network delay
	sim_t *s = Sim_New (72, 40, 1000.0 / 144);
	int i, k;
	Sim_Run (s, 4);
	Sim_Frame (s, 2000, 0);
	Sim_Run (s, 10);
	k = s->next + 72 * 3;
	for (i = 0; i < 9; i++)
		if (s->pkts[k + i].arrive < s->pkts[k].arrive + 0.120)
			s->pkts[k + i].arrive = s->pkts[k].arrive + 0.120;
	Sim_Run (s, 12);
	CHECK (s->nd->jitter_ms < 10, "stall-threshold: jitter %.1f after a client stall", s->nd->jitter_ms);
	{
		int late = 0;
		for (i = 0; i < ND_HITCH_SLOTS; i++)
			if (s->nd->hitches[i].m.used && s->nd->hitches[i].m.kind == ND_HITCH_LATE)
				late++;
		CHECK (late == 1, "stall-threshold: the later 120 ms delay was %s", late ? "caught" : "missed");
	}
	Sim_Free (s);
}

static void Test_RttHighFps (void)
{	// 1000 moves/s with 100 ms round trips: 100 commands in flight
	netdiag_t *nd = (netdiag_t *) calloc (1, sizeof(*nd));
	int seq;
	ND_Reset (nd);
	ND_SetSource (nd, ND_SRC_NET);
	for (seq = 1; seq <= 3000; seq++)
	{
		double t = seq * 0.001;
		ND_CmdSent (nd, seq, t);
		if (seq > 100)
			ND_CmdAck (nd, seq - 100, t);
	}
	CHECK (nd->s.cmdrtt_samples > 2800, "rtt-fps: %u samples at 1000 fps", nd->s.cmdrtt_samples);
	CHECK (NEAR (nd->last_cmdrtt_ms, 100, 0.5), "rtt-fps: %.1f ms, expected 100", nd->last_cmdrtt_ms);
	free (nd);
}

static void Test_StallLatenessReport (void)
{	// repeated client stalls, no network delay: LATE stays near zero everywhere it's reported
	sim_t *s = Sim_New (72, 40, 1000.0 / 144);
	int i;
	Sim_Run (s, 3);
	for (i = 0; i < 5; i++)
	{
		Sim_Frame (s, 140, 0);
		Sim_Run (s, 3);
	}
	CHECK (ND_HistPercentile (s->nd->s.lnet_hist, 0.99) < 10, "late-report: LATE p99 %.1f ms with only client stalls",
		ND_HistPercentile (s->nd->s.lnet_hist, 0.99));
	CHECK (ND_HistPercentile (s->nd->s.late_hist, 0.998) > 50, "late-report: raw p99.8 %.1f should include the 5 stalls",
		ND_HistPercentile (s->nd->s.late_hist, 0.998));
	Sim_Free (s);
}

static void Test_RestartClearsLatency (void)
{	// a map restart: the old round trip and ping aren't current any more
	netdiag_t *nd = (netdiag_t *) calloc (1, sizeof(*nd));
	ND_Reset (nd);
	ND_SetSource (nd, ND_SRC_NET);
	ND_Ping (nd, 1.0, 45, 3);
	ND_CmdSent (nd, 100, 1.0);
	ND_CmdAck (nd, 100, 1.05);
	ND_CmdSent (nd, 3, 2.0);	// sequence started over
	CHECK (nd->last_cmdrtt_time < 0 && nd->last_ping_time < 0 && nd->last_moveloss < 0, "restart: old latency survived");
	free (nd);
}

// hostile inputs under ASan/UBSan: random event order, time going backwards and jumping,
// garbage sequences and message ids, garbage CSV; nothing may crash or misbehave
static unsigned int fz_state = 12345;
static unsigned int Fz (void)
{
	fz_state = fz_state * 1103515245u + 12345u;
	return fz_state >> 8;
}

static void Test_Fuzz (void)
{
	netdiag_t *nd = (netdiag_t *) calloc (1, sizeof(*nd));
	nd_bucket_t *back = (nd_bucket_t *) calloc (ND_RING, sizeof(nd_bucket_t));
	nd_info_t info;
	nd_summary_t w;
	char buf[1024];
	double t = 1000.0;
	int i, round;
	FILE *f;

	memset (&info, 0, sizeof(info));
	info.recording = info.connected = 1;
	for (round = 0; round < 4; round++)
	{
		ND_Reset (nd);
		ND_SetSource (nd, (int)(Fz () % 5));
		for (i = 0; i < 60000; i++)
		{
			unsigned int r = Fz ();
			switch (r % 23)
			{
			case 0: t += (Fz () % 1000) / 1000.0; break;
			case 1: t -= (Fz () % 100) / 1000.0; break;				// backwards
			case 2: if (!(Fz () % 50)) t += Fz () % 100000; break;		// big jumps
			case 3: ND_Frame (nd, t, (int)(Fz () % 8)); break;
			case 4: ND_Packet (nd, t, (int)(Fz () % 3) - 0, (int)(Fz () % 6) - 1, (int)(Fz () % 3000) - 100); break;
			case 5: ND_Arrival (nd, t, (int)(Fz () % 3000) - 500); break;
			case 6: ND_ServerTime (nd, t - 900 + (Fz () % 100) / 100.0); break;
			case 7: ND_Svc (nd, (int)(Fz () % 300) - 50, (int)(Fz () % 2000)); break;
			case 8: ND_MessageDone (nd); break;
			case 9: ND_Ping (nd, t, (float)((int)(Fz () % 2000) - 100), (int)(Fz () % 200) - 100); break;
			case 10: ND_CmdSent (nd, (int)(Fz () % 5000) - 100, t); break;
			case 11: ND_CmdAck (nd, (int)(Fz () % 5000) - 100, t); break;
			case 12: ND_Starved (nd); break;
			case 13: ND_ReadPass (nd, t); break;
			case 14: ND_Backlog (nd, (int)(Fz () % 70000) - 10); break;
			case 15: ND_Mark (nd, t); break;
			case 16: ND_Break (nd, t); break;
			case 17: if (!(Fz () % 200)) ND_Finish (nd); break;
			case 18: ND_Dup (nd, t); ND_Stale (nd, t); ND_Short (nd, t); break;
			case 19: ND_Resent (nd, t); ND_WriteFail (nd, t); break;
			case 20: ND_SetSource (nd, (int)(Fz () % 5)); break;
			case 21: ND_Summarize (nd, (int)(Fz () % 2000), &w); break;
			case 22: ND_FormatStatus (nd, &info, t, buf, sizeof(buf)); break;
			}
		}
		f = tmpfile ();
		ND_WriteReportText (nd, &info, t, f);
		ND_WriteReportJSON (nd, &info, t, f);
		ND_WriteReportCSV (nd, f);
		rewind (f);
		ND_ReadCSV (f, "live", 0, back, ND_RING);
		fclose (f);
	}
	// garbage CSV: random bytes, a header with too many columns, and out-of-range values
	f = tmpfile ();
	fprintf (f, "set,n,time_s,svc1,svc1_b,svc2,svc2_b,frames,late_net_max_ms,unknown_col\n");
	for (i = 0; i < 3000; i++)
	{	// parseable rows with hostile values, plus a garbage tail inside the last field
		int k, len = (int)(Fz () % 300);
		fprintf (f, "live,0,0,%d,%d,%d,%d,%d,%s,", (int)(Fz () % 1000) - 200, (int)(Fz () % 70000), 255, 9, (int)(Fz () % 70000),
			(Fz () % 2) ? "nan" : "-1e40");
		for (k = 0; k < len; k++)
		{
			int ch = (int)(Fz () % 95) + 32;
			fputc (ch == ',' ? ';' : ch, f);
		}
		fputc ('\n', f);
	}
	fprintf (f, "garbage line with, wrong, column count\n");
	rewind (f);
	i = ND_ReadCSV (f, "live", 0, back, ND_RING);
	fclose (f);
	CHECK (i == ND_RING, "fuzz: %d hostile rows parsed (expected the %d the buffer holds)", i, ND_RING);
	for (round = 0; round < i; round++)
		CHECK (back[round].top_svc[0] < ND_SVC_TYPES && back[round].top_svc[1] < ND_SVC_TYPES, "fuzz: replay kept an out-of-range message id");
	ND_LoadBuckets (nd, back, i);
	ND_Summarize (nd, ND_RING, &w);
	CHECK (1, "fuzz: survived");
	free (back);
	free (nd);
}

static void Test_Size (void)
{
	CHECK (sizeof(netdiag_t) < 1024 * 1024, "size: netdiag_t is %zu bytes (budget 1 MiB)", sizeof(netdiag_t));
	printf ("sizeof bucket %zu, hitch %zu, netdiag_t %zu (%.0f KB)\n", sizeof(nd_bucket_t), sizeof(nd_hitch_t),
		sizeof(netdiag_t), sizeof(netdiag_t) / 1024.0);
}

int main (void)
{
	Test_Size ();
	Test_Clean ();
	Test_Drop ();
	Test_Delay ();
	Test_Stall ();
	Test_Merge ();
	Test_Slots ();
	Test_Pause ();
	Test_Mark ();
	Test_Clock ();
	Test_Reset ();
	Test_PingCmd ();
	Test_Status ();
	Test_OffOn ();
	Test_FinishPending ();
	Test_LocalSentence ();
	Test_MessageTotal ();
	Test_UpdatesUnderLoss ();
	Test_SplitSnapshots ();
	Test_Coverage30 ();
	Test_GapUnmeasured ();
	Test_FinishLastBucket ();
	Test_AckRestart ();
	Test_Freshness ();
	Test_CSVRoundTrip ();
	Test_StallKeepsThreshold ();
	Test_RttHighFps ();
	Test_StallLatenessReport ();
	Test_RestartClearsLatency ();
	Test_Fuzz ();
	Bench ();
	printf ("%d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
