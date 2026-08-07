/*
	csqc.qc -- CSQC half of the harness.

	Uses the "simple csqc" entry point (CSQC_DrawHud), so the engine still
	renders the world normally and the test grid is drawn as an overlay.
*/

float cq_frames;


// 0303a9d9 -- builtin_find must not report a number the current module has no
// handler for. It takes the name as a string, so nothing is declared here.
float(string builtinname) builtin_find = #0;

//======================================================================

void() cq_builtin_find =
{
	// ssqc-only: clientstat is PF_NoCSQC, te_lightningblood has a NULL csqcfunc
	st_eqf("builtin_find/ssqc-only-clientstat", builtin_find("clientstat"), 0);
	st_eqf("builtin_find/ssqc-only-te_lightningblood", builtin_find("te_lightningblood"), 0);
	// present in csqc, so the number still comes back
	st_ok("builtin_find/csqc-drawpic", builtin_find("drawpic") != 0);
	st_eqf("builtin_find/unknown-name", builtin_find("nosuchbuiltin"), 0);
};

// a8675028 -- both operate on qcvm->cursorforced, which is per-VM, so they are
// meaningful in CSQC and not just the menu.
void() cq_mousetarget =
{
	setmousetarget(2);
	st_eqf("mousetarget/forced", getmousetarget(), 2);
	setmousetarget(1);
	st_eqf("mousetarget/released", getmousetarget(), 1);
};

// 20d62460 -- frameduration left OFS_RETURN untouched for an invalid model or
// frame, so callers read whatever happened to be in the return global.
void() cq_frameduration =
{
	st_eqf("frameduration/bad-modelindex", frameduration(9999, 0), 0);
	st_eqf("frameduration/bad-frame", frameduration(1, 9999), 0);
};

void() cq_selftest =
{
	cq_builtin_find();
	cq_mousetarget();
	cq_frameduration();
	st_getgamedirinfo();
	st_setkeybind();
	st_numbering_shared();
	st_report("csqc");
	localcmd("quit\n");
};

//======================================================================

void(vector virtsize, float showscores) CSQC_DrawHud =
{
	cq_frames = cq_frames + 1;
	if (autocvar_hq_selftest && cq_frames == 60)
		cq_selftest();

	RotPic_DrawTests(virtsize);
};

void(float apilevel, string enginename, float engineversion) CSQC_Init =
{
	print("csqcharness: csqc loaded on ", enginename, "\n");
};
