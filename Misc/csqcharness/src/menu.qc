/*
	menu.qc -- MenuQC half of the harness.

	Loading a menu.dat replaces QSS-M's built-in menu wholesale, which is why
	this lives in its own gamedir. ESC closes it and hands input back to the
	game, so the console and quit still work normally.
*/

#define KEY_GAME	0
#define KEY_MENU	2
#define MT_GAME		0
#define MT_MENU		2
#define QCK_ESCAPE	27

float menu_shown;
float mq_frames;

// fields used by the findchain checks below.
//   .chain is deliberately a float: the implicit lookup must reject it by type
//   rather than link edict references through a float field (bc7078cd).
.float chain;
.string model;
.float modelindex;
.vector mins;
.vector maxs;
.entity mychain;

// legacy/canonical builtin slots, declared by number on purpose so the
// load-time remap in PR_EnableExtensions is what is under test (061fbbc3).
float(string s) tokenize = #441;			// superseded QSS slot -> must remap to #58
void(strbuf b) buf_del = #441;				// FTE canonical slot, same number
void(strbuf b) buf_del_legacy = #461;		// superseded QSS slot, still bound
float(string s) tokenize_menuqc = #58;		// canonical slot, direct
string(float c) chr2str_menuqc = #78;		// FTE alias
float(string k) stringtokeynum_menuqc = #614;
float(optional float t) gettime_legacy = #519;

//======================================================================

// 65c36549 / 81ca1020 -- registercvar returns 1 only when it creates the cvar,
// 0 when it already exists, and maps DP's flag bits onto QSS-M cvar flags.
void() mq_registercvar =
{
	st_eqf("registercvar/created", registercvar("hq_probe", "7"), 1);
	st_eqf("registercvar/duplicate", registercvar("hq_probe", "9"), 0);
	st_eqs("registercvar/no-op-keeps-value", cvar_string("hq_probe"), "7");
	// bit 5 is DP's archive flag -- run_harness.py checks this reaches config.cfg
	st_eqf("registercvar/archive-accepted", registercvar("hq_archived", "5", 32), 1);
	// an unsupported bit is reported, not silently swallowed, and still creates
	st_eqf("registercvar/unknown-flag-still-creates", registercvar("hq_bogus", "1", 1), 1);
};

// 43901b66 -- these had working handlers but were PF_NoMenu.
void() mq_wiring =
{
	local entity a = spawn();
	local entity b = spawn();
	a.st_flagfield = 3;
	b.st_flagfield = 9;

	st_ok("etos/returns-something", etos(a) != "");
	st_ok("findfloat/finds-b", findfloat(__NULL__, st_flagfield, 9) == b);
	st_ok("randomvec/nonzero", vlen(randomvec()) > 0);
	cprint("");	// must not crash; nothing to assert
	st_ok("cprint/survives", TRUE);
};

// 72fbae5f / bc7078cd / e6e544c0 -- findchainflags is wired for menuqc, links
// through an explicitly supplied field, and refuses to use an implicit `chain`
// that is not an entity field.
void() mq_findchain =
{
	local entity a = spawn();
	local entity b = spawn();
	local entity c = spawn();
	local entity head;
	local float n;

	// a dedicated bit: findchainflags matches with a bitwise AND, so anything
	// the earlier checks left lying around would otherwise join the chain
	a.st_flagfield = 64;
	b.st_flagfield = 2;
	c.st_flagfield = 64;

	// explicit chain field: a and c match, so the chain is two long
	head = findchainflags(st_flagfield, 64, mychain);
	while (head != __NULL__ && n < 10)
	{
		n = n + 1;
		head = head.mychain;
	}
	st_eqf("findchainflags/explicit-field", n, 2);

	// implicit lookup finds `.float chain`, which is the wrong type -- the old
	// code linked through it anyway, writing past the end of menuqc edicts
	st_ok("findchainflags/rejects-non-entity-chain",
		findchainflags(st_flagfield, 64) == __NULL__);
};

// 061fbbc3 -- the #441 collision. Legacy tokenize and FTE buf_del are both
// declared at #441 above; the remap must send tokenize to #58 and leave
// buf_del alone, in the same progs.
void() mq_remap =
{
	local strbuf sb;

	st_eqf("remap/legacy-tokenize-441", tokenize("alpha beta gamma"), 3);
	st_eqs("remap/legacy-tokenize-441-argv", argv(0), "alpha");
	st_eqf("remap/canonical-tokenize-58", tokenize_menuqc("one two"), 2);

	sb = buf_create();
	bufstr_add(sb, "x", 0);
	st_eqf("remap/buf_create", buf_getsize(sb), 1);
	buf_del(sb);				// FTE canonical #441
	st_ok("remap/fte-buf_del-441", TRUE);

	sb = buf_create();
	bufstr_add(sb, "y", 0);
	buf_del_legacy(sb);			// superseded QSS #461, still bound
	st_ok("remap/legacy-buf_del-461", TRUE);
};

// 66180b68 -- FTE-numbered aliases resolve to the same builtins
void() mq_aliases =
{
	st_eqs("alias/chr2str-78", chr2str_menuqc(65), "A");
	st_eqf("alias/stringtokeynum-614", stringtokeynum_menuqc("ESCAPE"), 27);
	st_ok("alias/gettime-legacy-519", gettime_legacy(0) > 0);
};

//F210 ("used but not directly precached") is emitted in a sweep at the end of
//compilation, not at the setmodel site, so this must stay disabled for the
//rest of the file -- re-enabling it later re-arms the sweep. We setmodel a
//model we deliberately have not precached, which is the point of the test.
#pragma warning disable F210
void() mq_models =
{
	local float before, idx, after;
	local string frame0;
	local entity ent;

	before = getmodelindex("progs/player.mdl", TRUE);
	st_eqf("model/query-before-precache", before, 0);

	idx = getmodelindex("progs/player.mdl", FALSE);
	st_ok("model/getmodelindex-positive", idx > 0);

	after = getmodelindex("progs/player.mdl", TRUE);
	st_eqf("model/query-after-precache", after, idx);

	// round-trip rather than hardcoding an animation name: whatever the shipped
	// player.mdl calls frame 0, frametoname and frameforname must agree on it.
	// NOTE: only the md3 and iqm loaders populate frames[].name -- the classic
	// .mdl loader never copies it, so this is degenerate ("") for player.mdl.
	// If the mdl loader learns to load frame names, the next check flips and
	// this round-trip becomes a real test.
	frame0 = frametoname(idx, 0);
	st_eqs("model/frametoname-mdl-names-unloaded", frame0, "");
	st_eqf("model/frame-name-roundtrip", frameforname(idx, frame0), 0);
	st_ok("model/frameduration-positive", frameduration(idx, 0) > 0);

	st_poisonf();
	st_eqf("model/frameforname-invalid-handle", frameforname(99999, frame0), -1);
	st_poisonf();
	st_eqf("model/frameduration-invalid-handle", frameduration(99999, 0), 0);
	st_poisons();
	st_eqs("model/frametoname-invalid-handle", frametoname(99999, 0), "");

	ent = spawn();
	setmodel(ent, "progs/player.mdl");
	st_eqf("model/setmodel-populates-index", ent.modelindex, idx);
};

// Bounded malformed input across the handle/index builtins. Deterministic
// boundary values beat random fuzzing: these are the cases that actually
// separate "rejected cleanly" from "read something it should not have".
void(float h, string tag) mq_bad_handle =
{
	st_poisonf();
	st_eqf(strcat("badhandle/frameduration/", tag), frameduration(h, 0), 0);
	st_poisonf();
	st_eqf(strcat("badhandle/frameforname/", tag), frameforname(h, "nosuchframe"), -1);
	st_poisons();
	st_eqs(strcat("badhandle/frametoname/", tag), frametoname(h, 0), "");
};

void() mq_bounds =
{
	local float idx = getmodelindex("progs/player.mdl", FALSE);

	mq_bad_handle(-1, "negative");
	mq_bad_handle(0, "zero");
	mq_bad_handle(idx + 1, "one-past-valid");
	mq_bad_handle(99999, "far-out-of-range");

	// a fractional handle truncates to the valid one rather than being rejected
	st_ok("bounds/fractional-handle-truncates",
		frameduration(idx + 0.5, 0) == frameduration(idx, 0));

	// bad frame numbers on a good handle
	// a negative frame used to alias to frame 0: float->unsigned is undefined
	// and saturates to 0 on arm64. it must be rejected outright.
	st_poisonf();
	st_eqf("bounds/frameduration/frame-negative", frameduration(idx, -1), 0);
	st_ok("bounds/frameduration/frame-negative-differs-from-zero",
		frameduration(idx, 0) > 0);
	st_poisonf();
	st_eqf("bounds/frameduration/frame-far", frameduration(idx, 99999), 0);
	st_poisons();
	st_eqs("bounds/frametoname/frame-far", frametoname(idx, 99999), "");

	// getmodelindex itself
	st_eqf("bounds/getmodelindex/empty-name", getmodelindex("", FALSE), 0);
	st_eqf("bounds/getmodelindex/missing-file",
		getmodelindex("progs/hq_no_such_model.mdl", FALSE), 0);

	// string buffer handles
	st_poisonf();
	st_ok("bounds/buf_getsize/negative", buf_getsize(-1) <= 0);
	st_poisonf();
	st_ok("bounds/buf_getsize/far", buf_getsize(99999) <= 0);
};

void() mq_selftest =
{
	mq_models();
	mq_bounds();
	mq_registercvar();
	mq_wiring();
	mq_findchain();
	mq_remap();
	mq_aliases();
	st_serverkeyfloat();
	st_eqs("serverkey/constate", serverkey("constate"),
		autocvar_hq_expect_connected ? "active" : "disconnected");
	st_getgamedirinfo();
	st_setkeybind();
	st_numbering_shared();
	st_report("menuqc");
	localcmd("quit\n");
};

//======================================================================

void(float wantmode) m_toggle =
{
	if (wantmode < 0)
		menu_shown = !menu_shown;
	else
		menu_shown = wantmode;
	setkeydest(menu_shown ? KEY_MENU : KEY_GAME);
	setmousetarget(menu_shown ? MT_MENU : MT_GAME);
};

void(vector screensize) m_draw =
{
	if (!menu_shown)
		return;

	// gettime reads realtime, which is still 0 during Host_Init -- and boot-time
	// console prints force screen updates, so the first m_draw calls happen then.
	// Wait for the frame loop proper before asserting on it.
	mq_frames = mq_frames + 1;
	if (autocvar_hq_menu_selftest && mq_frames == 120)
		mq_selftest();

	RotPic_DrawTests(screensize);
};

void(float scan, float chr) m_keydown =
{
	if (scan == QCK_ESCAPE)
		m_toggle(0);
};

void(float scan, float chr) m_keyup = {};

void() m_init =
{
	print("csqcharness: menuqc loaded, ESC/togglemenu shows the test panel\n");
};
