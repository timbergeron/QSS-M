// Only the server-approved CSQC_UpdateView entry point can run this suite.
float full_frames, full_api, full_scenes;
void(float apilevel, string enginename, float engineversion) CSQC_Init =
{
	full_api = apilevel;
	if (autocvar_hq_vm_restart && autocvar_hq_restart_stage == 1)
		st_eqs("token/vm-reload-clears-input", getentitytoken(), "");
	print("csqcharness: full CSQC loaded on ", enginename, "\n");
};
void(float width, float height, float focus) CSQC_UpdateView =
{
	full_frames = full_frames + 1;
	// Exercise both UI-only frames and the transition back from 3D rendering.
	if (full_frames % 2 == 0)
	{
		clearscene();
		renderscene();
		full_scenes = full_scenes + 1;
	}
	drawfill('0 0 0', [width, height, 0], '0 0 0', 1);
	Parity_DrawTests('24 72 0');
	if (full_frames == 1)
		localcmd("con_notifytime 0;crosshair 0;scr_showfps 0;vid_gamma 1\n");
	if (!autocvar_hq_vm_restart && (full_frames == 44 || full_frames == 45))
		localcmd("screenshot tga\n");
	if (full_frames == 40) cprint("STATE");
	if (full_frames == 60)
	{
		if (autocvar_hq_vm_restart && autocvar_hq_restart_stage == 0)
		{
			getentitytoken("owned input before VM reload");
			cvar_set("hq_restart_stage", "1");
			print("csqcharness: restarting with owned token input\n");
			localcmd(strcat("map ", mapname, "\n"));
			return;
		}
		st_eqf("full/server-approved-api", full_api, 1);
		st_ok("full/updateview-ran", TRUE);
		st_ok("full/renderscene-survives", full_scenes > 0);
		st_bufstr_find();
		cq_parity_models();
		cq_parity_tokens();
		st_report("fullcsqc");
		localcmd("quit\n");
	}
	drawsetcliparea(0, 0, 1, 1);
};
