// FTE parity regressions shared by the HUD, full-CSQC and menu programs.
void() st_bufstr_find =
{
	local strbuf b = buf_create();
	bufstr_set(b, 0, "alpha");
	bufstr_set(b, 2, "alphabet");
	bufstr_set(b, 4, "beta");
	bufstr_set(b, 5, "");
	st_eqf("bufstr_find/exact", bufstr_find(b, "alpha", MATCH_EXACT), 0);
	st_eqf("bufstr_find/left-start", bufstr_find(b, "alpha", MATCH_LEFT, 1), 2);
	st_eqf("bufstr_find/right", bufstr_find(b, "bet", MATCH_RIGHT), 2);
	st_eqf("bufstr_find/right-too-long", bufstr_find(b, "longer-than-alphabet", MATCH_RIGHT), -1);
	st_eqf("bufstr_find/middle", bufstr_find(b, "phab", MATCH_MIDDLE), 2);
	st_eqf("bufstr_find/pattern", bufstr_find(b, "a?ph*", MATCH_PATTERN), 0);
	st_eqf("bufstr_find/auto", bufstr_find(b, "*bet", MATCH_AUTO), 2);
	st_eqf("bufstr_find/default-pattern", bufstr_find(b, "*ta"), 4);
	st_eqf("bufstr_find/wildcard-case-insensitive", bufstr_find(b, "ALPHA", MATCH_PATTERN), 0);
	st_eqf("bufstr_find/exact-case-sensitive", bufstr_find(b, "ALPHA", MATCH_EXACT), -1);
	st_eqf("bufstr_find/start-step", bufstr_find(b, "*", MATCH_PATTERN, 1, 3), 4);
	st_eqf("bufstr_find/skip-holes", bufstr_find(b, "*", MATCH_PATTERN, 1), 2);
	st_eqf("bufstr_find/empty-string", bufstr_find(b, "", MATCH_EXACT), 5);
	st_eqf("bufstr_find/no-match", bufstr_find(b, "missing", MATCH_EXACT), -1);
	st_eqf("bufstr_find/invalid-mode-pattern", bufstr_find(b, "*ta", 99), 4);
	st_eqf("bufstr_find/negative-start", bufstr_find(b, "*", MATCH_PATTERN, -1), -1);
	st_eqf("bufstr_find/past-end", bufstr_find(b, "*", MATCH_PATTERN, 6), -1);
	st_eqf("bufstr_find/zero-step", bufstr_find(b, "*", MATCH_PATTERN, 0, 0), -1);
	st_eqf("bufstr_find/negative-step", bufstr_find(b, "*", MATCH_PATTERN, 0, -1), -1);
	st_eqf("bufstr_find/huge-step", bufstr_find(b, "missing", MATCH_EXACT, 0, 2147483520), -1);
	st_eqf("bufstr_find/overflow-step", bufstr_find(b, "*", MATCH_PATTERN, 0, 2147483648), -1);
	st_eqf("bufstr_find/invalid-handle", bufstr_find(-1, "*"), -1);
	st_eqf("bufstr_find/nan-handle", bufstr_find(stof("nan"), "*"), -1);
	st_eqf("bufstr_find/nan-start", bufstr_find(b, "*", MATCH_PATTERN, stof("nan")), -1);
	st_eqf("bufstr_find/infinite-step", bufstr_find(b, "*", MATCH_PATTERN, 0, stof("inf")), -1);
	buf_del(b);
	st_eqf("bufstr_find/deleted-handle", bufstr_find(b, "*"), -1);
};

#ifdef CSQC
void() cq_parity_models =
{
	local float i, missed, idx, grouped;
	st_eqf("bufstr_find/foreign-vm", bufstr_find(autocvar_hq_foreignbuf, "foreign"), -1);
	// More misses than the entire client precache table can hold. Query-only
	// must not load models or occupy even one slot.
	while (i < 5000)
	{
		if (getmodelindex(strcat("progs/hq_query_", ftos(i), ".mdl"), TRUE) != 0)
			missed = missed + 1;
		i = i + 1;
	}
	st_eqf("model/query-only-misses", missed, 0);
	st_eqf("model/query-only-existing-file", getmodelindex("progs/hq_single.mdl", TRUE), 0);
	st_eqf("model/missing-load", getmodelindex("progs/hq_absent.mdl"), 0);
	st_eqf("model/missing-query", getmodelindex("progs/hq_absent.mdl", TRUE), 0);
	st_eqf("model/truncated-header", getmodelindex("progs/hq_truncated.mdl"), 0);
	st_eqf("model/unsupported-format", getmodelindex("progs/hq_unsupported.md2"), 0);
	idx = getmodelindex("progs/hq_single.mdl");
	st_eqf("model/no-slots-consumed-by-misses", idx, -1);
	st_eqf("model/query-client-handle", getmodelindex("progs/hq_single.mdl", TRUE), idx);
	st_eqf("model/repeated-client-handle", getmodelindex("progs/hq_single.mdl"), idx);
	st_eqf("model/query-server-handle", getmodelindex(strcat("maps/", mapname, ".bsp"), TRUE), 1);
	st_eqf("model/empty", getmodelindex(""), 0);
	st_eqf("frame/negative-handle-name", frameforname(idx, "hq_frame"), 0);
	st_eqs("frame/negative-handle-to-name", frametoname(idx, 0), "hq_frame");
	grouped = getmodelindex("progs/hq_group.mdl");
	st_ok("frame/negative-handle-duration", fabs(frameduration(grouped, 0) - 0.2) < 0.0001);
	st_eqf("frame/negative-handle-bad-frame", frameduration(grouped, -1), 0);
	st_eqf("frame/lower-bound", frameforname(-65536, "hq_frame"), -1);
	st_eqf("frame/int-min", frameforname(-2147483648, "hq_frame"), -1);
	st_eqs("frame/unused-negative-handle", frametoname(-4095, 0), "");
	st_eqf("frame/upper-bound", frameduration(65536, 0), 0);
	st_eqf("frame/nan-model", frameforname(stof("nan"), "hq_frame"), -1);
	st_eqf("frame/infinite-model", frameduration(stof("inf"), 0), 0);
	st_eqf("frame/nan-frame", frameduration(grouped, stof("nan")), 0);
};

void() cq_parity_tokens =
{
	local float i;
	st_eqs("token/custom-reset-returns-null", getentitytoken("{ \"classname\" \"hq_custom\" }"), "");
	st_eqs("token/custom-open", getentitytoken(), "{");
	st_eqs("token/custom-key", getentitytoken(), "classname");
	st_eqs("token/custom-value", getentitytoken(), "hq_custom");
	st_eqs("token/custom-close", getentitytoken(), "}");
	st_eqs("token/custom-eof", getentitytoken(), "");
	st_eqs("token/repeated-eof", getentitytoken(), "");
	getentitytoken(strcat("temporary_", "source retained"));
	// Overwrite the QC tempstring ring before reading the copied input.
	while (i < 1024)
	{
		strcat("overwrite_", ftos(i));
		i = i + 1;
	}
	st_eqs("token/tempstring-lifetime", getentitytoken(), "temporary_source");
	getentitytoken("replacement partial stream");
	st_eqs("token/reset-mid-stream", getentitytoken(), "replacement");
	getentitytoken("second");
	st_eqs("token/replaced-source", getentitytoken(), "second");
	st_eqs("token/map-reset-returns-null", getentitytoken(""), "");
	st_eqs("token/map-reset-first-token", getentitytoken(), "{");
	getentitytoken("\"a\\\"b\" tail");
	st_eqs("token/escaped-quote", getentitytoken(), "a\"b");
	st_eqs("token/escaped-quote-next", getentitytoken(), "tail");
	getentitytoken("\"a\\tb\\rc\\nd\\\\e\\q\"");
	st_eqs("token/quoted-escapes", getentitytoken(), "a\tb\rc\nd\\e\\q");
	getentitytoken("path\\folder");
	st_eqs("token/unquoted-backslash", getentitytoken(), "path\\folder");
	getentitytoken("a,b:c");
	st_eqs("token/punctuation-first", getentitytoken(), "a");
	st_eqs("token/comma", getentitytoken(), ",");
	st_eqs("token/punctuation-second", getentitytoken(), "b");
	st_eqs("token/colon", getentitytoken(), ":");
	st_eqs("token/punctuation-third", getentitytoken(), "c");
	getentitytoken("\"unfinished");
	st_eqs("token/unterminated-quote", getentitytoken(), "unfinished");
	st_eqs("token/unterminated-quote-eof", getentitytoken(), "");
	getentitytoken("/* unfinished");
	st_eqs("token/unterminated-comment", getentitytoken(), "");
	getentitytoken("\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\" tail");
	st_eqf("token/oversized-truncates", strlen(getentitytoken()), 1023);
	st_eqs("token/oversized-next", getentitytoken(), "tail");
	// Leave owned input outstanding to exercise VM teardown as well as reset.
	getentitytoken("owned input for shutdown");
};

void() cq_model_stress =
{
	local float i, unexpected;
	// Exceed MAX_MOD_KNOWN as well as the client precache table. Failed loads
	// must not fill either table or prevent a later valid load.
	while (i < 9000)
	{
		if (getmodelindex(strcat("progs/hq_miss_", ftos(i), ".mdl")) != 0)
			unexpected = unexpected + 1;
		i = i + 1;
	}
	st_eqf("model/9000-failed-loads", unexpected, 0);
	st_eqf("model/stress-first-free-slot", getmodelindex("progs/hq_single.mdl"), -1);
	st_eqs("model/stress-frame-readable", frametoname(-1, 0), "hq_frame");
};

float cq_model_wait, cq_model_done;
void() cq_model_recovery =
{
	local float idx;
	if (!cq_model_wait)
	{
		st_eqf("model/late-absent-not-cached", getmodelindex("progs/hq_late.mdl"), 0);
		st_eqf("model/late-query-absent", getmodelindex("progs/hq_late.mdl", TRUE), 0);
		cq_model_wait = gettime(0);
		return;
	}
	idx = getmodelindex("progs/hq_late.mdl");
	if (idx < 0)
	{
		st_eqf("model/late-first-free-slot", idx, -1);
		st_eqs("model/late-retry-frame", frametoname(idx, 0), "hq_frame");
		st_eqf("model/late-query-found", getmodelindex("progs/hq_late.mdl", TRUE), idx);
		cq_model_done = TRUE;
	}
	else if (gettime(0) - cq_model_wait > 10)
	{
		st_ok("model/late-retry-timeout", FALSE);
		cq_model_done = TRUE;
	}
};
#endif

// Every row has a normal reference, additive draw, explicit normal successor,
// and omitted-flags successor. Pixel checks require both successors to match
// the reference, and the additive sample to preserve the blue background.
#define PARITY_PIC "gfx/hq_white.tga"
void(vector origin) Parity_DrawTests =
{
	local float row, col, flag;
	local vector p, aa;
	for (row = 0; row < 8; row = row + 1)
	{
		for (col = 0; col < 4; col = col + 1)
		{
			p = origin + [col * 32, row * 32, 0];
			drawfill(p, '24 24 0', '0 0 0.5', 1, 0);
		}
		for (col = 0; col < 4; col = col + 1)
		{
			p = origin + [col * 32 + 4, row * 32 + 4, 0];
			flag = col == 1 ? 1 : 0;
			if (col == 3)
			{
				if (row == 0) drawfill(p, '0 0 0', '1 0 0', 0, 1);
				else if (row == 1) drawpic(p, PARITY_PIC, '0 0 0', '1 0 0', 0, 1);
				else if (row == 2) drawsubpic(p, '0 0 0', PARITY_PIC, '0 0 0', '1 1 0', '1 0 0', 0, 1);
				else if (row == 3) drawcharacter(p, 65, '0 0 0', '1 0 0', 0, 1);
				else if (row == 4) drawrawstring(p, "A", '0 0 0', '1 0 0', 0, 1);
				else if (row == 5) drawstring(p, "A", '0 0 0', '1 0 0', 0, 1);
				else if (row == 7) drawline(1, p, p, '1 0 0', 0, 1);
			}
			if (row == 0)
			{
				if (col == 3) drawfill(p, '16 16 0', '1 0 0', 0.5);
				else drawfill(p, '16 16 0', '1 0 0', 0.5, flag);
			}
			else if (row == 1)
			{
				if (col == 3) drawpic(p, PARITY_PIC, '16 16 0', '1 0 0', 0.5);
				else drawpic(p, PARITY_PIC, '16 16 0', '1 0 0', 0.5, flag);
			}
			else if (row == 2)
			{
				if (col == 3) drawsubpic(p, '16 16 0', PARITY_PIC, '0 0 0', '1 1 0', '1 0 0', 0.5);
				else drawsubpic(p, '16 16 0', PARITY_PIC, '0 0 0', '1 1 0', '1 0 0', 0.5, flag);
			}
			else if (row == 3)
			{
				if (col == 3) drawcharacter(p, 65, '16 16 0', '1 0 0', 0.5);
				else drawcharacter(p, 65, '16 16 0', '1 0 0', 0.5, flag);
			}
			else if (row == 4)
			{
				if (col == 3) drawrawstring(p, "A", '16 16 0', '1 0 0', 0.5);
				else drawrawstring(p, "A", '16 16 0', '1 0 0', 0.5, flag);
			}
			else if (row == 5)
			{
				if (col == 3) drawstring(p, "A", '16 16 0', '1 0 0', 0.5);
				else drawstring(p, "A", '16 16 0', '1 0 0', 0.5, flag);
			}
			else if (row == 6)
			{
				aa = [0.5, 0, flag];
				drawrotsubpic(p, '0 0 0', '16 16 0', PARITY_PIC, '0 0 0', '1 1 0', '1 0 0', aa);
			}
			else
			{
				if (col == 3) drawline(9, p + '0 8 0', p + '16 8 0', '1 0 0', 0.5);
				else drawline(9, p + '0 8 0', p + '16 8 0', '1 0 0', 0.5, flag);
			}
		}
	}
	// Clip a line and a fill; drawing after reset must escape the clip.
	p = origin + '0 268 0';
	drawfill(p, '120 24 0', '0 0 0.5', 1, 0);
	drawsetcliparea(p_x + 8, p_y, 8, 24);
	drawline(1, p + '0 8 0', p + '24 8 0', '1 0 0', 1, 1);
	drawfill(p + '0 12 0', '24 8 0', '1 0 0', 0.5, 1);
	drawresetcliparea();
	drawfill(p + '32 4 0', '16 16 0', '1 0 0', 0.5);
	// Reserved blend modes (2/3) use normal blending, matching FTE.
	drawfill(p + '64 4 0', '16 16 0', '1 0 0', 0.5, 2);
	drawfill(p + '96 4 0', '16 16 0', '1 0 0', 0.5, 3);
};
