// Compile after the engine under test dumps qscsextensions.qc.
float autocvar_live_fsaa_qc;
float aa_frames;
float aa_phase;

void(float apilevel, string enginename, float engineversion) CSQC_Init =
{
	if (apilevel != 1)
	{
		print("FAIL: live-fsaa full CSQC not approved\n");
		localcmd("quit\n");
	}
};

void(float width, float height, float focus) CSQC_UpdateView =
{
	clearscene();
	aa_frames += 1;
	if (!autocvar_live_fsaa_qc || aa_frames % 2 == 0)
		renderscene();
	if (!autocvar_live_fsaa_qc)
		return;

	drawfill('0 0 0', [width, height, 0], '0 0 0', 1);
	if (autocvar_live_fsaa_qc != aa_phase)
	{
		aa_phase = autocvar_live_fsaa_qc;
		// Mutate AA while the frame's original render target is still in use.
		if (aa_phase == 1) cvar_set("vid_fxaa", "1");
		if (aa_phase == 2) cvar_set("vid_fsaa", "4");
		if (aa_phase == 3) cvar_set("vid_fxaa", "3");
		if (aa_phase == 4) cvar_set("vid_fxaa", "0");
		if (aa_phase == 5) cvar_set("vid_fsaa", "4");
		if (aa_phase == 6) cvar_set("vid_fsaa", "0");
		print("PASS: live-fsaa full CSQC phase ", ftos(aa_phase), "\n");
	}
	drawfill('20 20 0', '60 60 0', '1 0 0', 1);
	drawfill('100 20 0', '60 60 0', '0 1 0', 0.5);
	// Resolve and engine overlays must survive a clip left by the callback.
	drawsetcliparea(0, 0, 1, 1);
};
