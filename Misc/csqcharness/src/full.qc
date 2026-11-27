// Only the server-approved CSQC_UpdateView entry point can run this suite.
float full_frames, full_api, full_scenes;
entity full_chain_parent, full_chain_child, full_contents_ent;
float full_chain_calls, full_contents_calls, full_oldcontents, full_newcontents;

void() cq_chain_moved =
{
	if (self == full_chain_child && other == full_chain_parent)
		full_chain_calls = full_chain_calls + 1;
};

void(float previous, float current) cq_contents_changed =
{
	if (self == full_contents_ent)
	{
		full_contents_calls = full_contents_calls + 1;
		full_oldcontents = previous;
		full_newcontents = current;
	}
};

void() cq_setup_entity_physics =
{
	local vector camera = (vector)getproperty(VF_ORIGIN);
	physics_mode = 2;
	full_chain_parent = spawn();
	full_chain_child = spawn();
	full_chain_parent.movetype = MOVETYPE_NOCLIP;
	setorigin(full_chain_parent, camera);
	setorigin(full_chain_child, camera + '0 10 0');
	full_chain_parent.velocity = '100 0 0';
	full_chain_parent.avelocity = '0 30 0';
	full_chain_parent.movechain = full_chain_child;
	full_chain_child.movechain = full_chain_parent;
	full_chain_child.flags = FL_MOVECHAIN_ANGLE;
	full_chain_child.chainmoved = cq_chain_moved;

	full_contents_ent = spawn();
	setorigin(full_contents_ent, camera);
	full_contents_ent.movetype = MOVETYPE_STEP;
	full_contents_ent.flags = FL_ONGROUND;
	full_contents_ent.watertype = CONTENT_WATER;
	full_contents_ent.contentstransition = cq_contents_changed;
};

void() cq_check_entity_physics =
{
	st_ok("movechain/callback-owner-and-member", full_chain_calls > 0);
	st_ok("movechain/preserves-offset", vlen(full_chain_child.origin - full_chain_parent.origin - '0 10 0') < 0.01);
	st_ok("movechain/follows-angles", full_chain_parent.angles_y > 0 && full_chain_child.angles_y == full_chain_parent.angles_y);
	st_eqf("contentstransition/once", full_contents_calls, 1);
	st_eqf("contentstransition/old", full_oldcontents, CONTENT_WATER);
	st_eqf("contentstransition/new", full_newcontents, CONTENT_EMPTY);
	remove(full_chain_parent);
	remove(full_chain_child);
	remove(full_contents_ent);
};

void() cq_render_entity_fields =
{
	local vector mins = (vector)getentity(0, GE_MINS);
	local vector maxs = (vector)getentity(0, GE_MAXS);
	local vector origin = (vector)getentity(0, GE_ORIGIN);
	st_ok("ge/world-bounds", maxs_x > mins_x && maxs_y > mins_y && maxs_z > mins_z);
	st_eqv("ge/world-absmin", (vector)getentity(0, GE_ABSMIN), origin + mins);
	st_eqv("ge/world-absmax", (vector)getentity(0, GE_ABSMAX), origin + maxs);
	st_eqv("ge/world-gravity", (vector)getentity(0, GE_GRAVITYDIR), '0 0 -1');
	st_eqf("ge/world-no-secondary-model", (float)getentity(0, GE_MODELINDEX2), 0);
	st_eqf("ge/world-no-rt-light-radius", (float)getentity(0, GE_RTRADIUS), 0);
	st_poisonv();
	st_eqv("ge/invalid-entity-clears-vector", (vector)getentity(-1, GE_LIGHT), '0 0 0');
};

void() cq_extended_viewprops =
{
	local vector p, farther, camera, angles;
	local float clearprop = VF_DP_CLEARSCREEN;
	local entity mesh;
	clearscene();
	camera = (vector)getproperty(VF_ORIGIN);
	angles = (vector)getproperty(VF_ANGLES);
	st_eqf("viewprop/perspective-default", (float)getproperty(VF_PERSPECTIVE), 1);
	st_eqf("viewprop/viewentity-default", (float)getproperty(VF_VIEWENTITY), player_localentnum);
	st_eqf("viewprop/viewentity-set-zero", setproperty(VF_VIEWENTITY, 0), 1);
	st_eqf("viewprop/viewentity-zero", (float)getproperty(VF_VIEWENTITY), 0);
	st_eqf("viewprop/statsentity-follows-player", (float)getproperty(VF_STATSENTITY), player_localentnum);
	st_eqv("viewprop/single-seat-offset", (vector)getproperty(VF_SCREENVOFFSET), '0 0 0');
	p = (vector)getproperty(VF_PIXELPSCALE);
	st_ok("viewprop/pixel-scale", p_x > 0 && p_y > 0 && p_z == 1);
	st_eqf("viewprop/near-set", setproperty(VF_MINDIST, 8), 1);
	st_eqf("viewprop/far-set", setproperty(VF_MAXDIST, 1024), 1);
	st_eqf("viewprop/near-roundtrip", (float)getproperty(VF_MINDIST), 8);
	st_eqf("viewprop/far-roundtrip", (float)getproperty(VF_MAXDIST), 1024);
	st_eqf("viewprop/clear-set", setproperty(clearprop, 1), 1);
	st_eqf("viewprop/clear-roundtrip", (float)getproperty(VF_DP_CLEARSCREEN), 1);
	st_eqf("viewprop/mainview-set", setproperty(VF_DP_MAINVIEW, 0), 1);
	st_eqf("viewprop/mainview-roundtrip", (float)getproperty(VF_DP_MAINVIEW), 0);
	st_eqf("viewprop/skyroom-set", setproperty(VF_SKYROOM_CAMERA, '16 32 64'), 1);
	st_eqv("viewprop/skyroom-roundtrip", (vector)getproperty(VF_SKYROOM_CAMERA), '16 32 64');

	setproperty(VF_VIEWPORT, '0 0 0', '640 480 0');
	setproperty(VF_ORIGIN, '0 0 0');
	setproperty(VF_ANGLES, '0 0 0');
	setproperty(VF_FOV, '90 0 0');
	st_eqf("viewprop/projection-offset-set", setproperty(VF_PROJECTIONOFFSET, '0.25 0 0'), 1);
	st_eqv("viewprop/projection-offset-roundtrip", (vector)getproperty(VF_PROJECTIONOFFSET), '0.25 0 0');
	p = project('100 0 0');
	st_ok("viewprop/projection-offset-shifts-center", fabs(p_x - 320) > 20 && fabs(p_y - 240) < 0.5);
	// project returns clip-space depth. unproject accepts depth-buffer values.
	p_z = (p_z + 1) / 2;
	st_ok("viewprop/projection-offset-unproject", vlen(unproject(p) - '100 0 0') < 0.1);
	setproperty(VF_PROJECTIONOFFSET, '0 0 0');

	st_eqf("viewprop/orthographic-set", setproperty(VF_PERSPECTIVE, 0), 1);
	setproperty(VF_FOV, '0 480 0');
	p = project('100 -320 240');
	st_ok("viewprop/orthographic-y-only-extent", fabs(p_x - 640) < 0.5 && fabs(p_y) < 0.5);
	farther = project('200 -320 240');
	st_ok("viewprop/orthographic-depth-invariant", fabs(farther_x - p_x) < 0.5);
	p_z = (p_z + 1) / 2;
	st_ok("viewprop/orthographic-unproject", vlen(unproject(p) - '100 -320 240') < 0.1);
	clearscene();
	st_eqf("viewprop/perspective-restored", (float)getproperty(VF_PERSPECTIVE), 1);
	setproperty(VF_ORIGIN, camera);
	setproperty(VF_ANGLES, angles);
	setproperty(VF_PERSPECTIVE, 0);
	setproperty(VF_FOV, '640 480 0');
	setproperty(VF_DRAWWORLD, 1);
	makevectors(angles);
	mesh = spawn();
	mesh.modelindex = getmodelindex("progs/hq_single.mdl");
	mesh.origin = camera + v_forward * 96;
	mesh.renderflags = RF_USEAXIS | RF_DEPTHHACK | RF_FIRSTPERSON;
	mesh.glowmod = '1 1 1';
	addentity(mesh);
	renderscene();
	remove(mesh);
	clearscene();
	setproperty(VF_ORIGIN, camera);
	setproperty(VF_ANGLES, angles);
};

void() cq_trace_metadata =
{
	local vector origin = (vector)getproperty(VF_ORIGIN);
	local entity mover, body;
	traceline(origin, origin - '0 0 2048', 1, world);
	st_ok("trace/world-floor-hit", trace_fraction < 1 && !trace_startsolid);
	st_eqf("trace/world-floor-contents-int", trace_endcontentsi, 1);
	st_eqf("trace/world-floor-contents-float", trace_endcontentsf, 1);
	st_eqf("trace/world-floor-dp-contents", trace_dphitcontents, 1);
	st_eqf("trace/world-floor-network-entity", trace_networkentity, 0);
	st_ok("trace/world-floor-surface", trace_surface_id > 0 && trace_surfacename != "");
	st_eqs("trace/world-floor-dp-texture", trace_dphittexturename, trace_surfacename);
	traceline(origin, origin, 1, world);
	st_eqf("trace/point-empty-contents", trace_endcontentsi, 0);
	st_eqf("trace/miss-clears-surface-id", trace_surface_id, 0);
	st_eqs("trace/miss-clears-surface-name", trace_surfacename, "");

	mover = spawn();
	mover.hitcontentsmaski = CONTENTBIT_BODY;
	traceline(origin, origin - '0 0 2048', 1, mover);
	st_eqf("trace/body-mask-ignores-world", trace_fraction, 1);
	mover.hitcontentsmaski = CONTENTBIT_SOLID;
	traceline(origin, origin - '0 0 2048', 1, mover);
	st_eqf("trace/solid-mask-hits-world", trace_endcontentsi, CONTENTBIT_SOLID);

	body = spawn();
	body.solid = SOLID_BBOX;
	setsize(body, '-2 -2 -2', '2 2 2');
	setorigin(body, origin + '0 0 24');
	mover.hitcontentsmaski = CONTENTBIT_BODY;
	traceline(origin + '0 0 12', origin + '0 0 36', 0, mover);
	st_ok("trace/body-mask-hits-box", trace_ent == body);
	st_eqf("trace/body-uses-fte-contents", trace_endcontentsi, CONTENTBIT_BODY);
	mover.hitcontentsmaski = CONTENTBIT_SOLID;
	traceline(origin + '0 0 12', origin + '0 0 36', 0, mover);
	st_ok("trace/solid-mask-ignores-box", trace_ent != body);
	remove(body);
	remove(mover);
};

void() cq_legacy_pmove =
{
	local vector start = (vector)getproperty(VF_ORIGIN) + '0 0 48';
	local entity mover;
	st_eqv("pmove/default-gravity", global_gravitydir, '0 0 -1');
	pmove_org = start;
	pmove_vel = '0 0 0';
	pmove_mins = '-16 -16 -24';
	pmove_maxs = '16 16 32';
	pmove_jump_held = pmove_waterjumptime = pmove_onground = 0;
	input_angles = input_movevalues = '0 0 0';
	input_buttons = 0;
	input_timelength = 0.05;
	runstandardplayerphysics();
	st_ok("pmove/legacy-applies-gravity", pmove_org_z < start_z && pmove_vel_z < 0);
	st_eqv("pmove/legacy-preserves-bounds", pmove_mins, '-16 -16 -24');
	pmove_org = start;
	pmove_mins = '32 32 32';
	runstandardplayerphysics();
	st_eqv("pmove/invalid-bounds-leave-origin", pmove_org, start);

	mover = spawn();
	mover.movetype = MOVETYPE_WALK;
	setsize(mover, '-16 -16 -24', '16 16 32');
	setorigin(mover, start);
	mover.gravitydir = '0 0 1';
	runstandardplayerphysics(mover);
	st_ok("pmove/entity-gravity-overrides-global", mover.velocity_z > 0);
	remove(mover);
};

float full_reload_light, full_startframes;

void() StartFrame =
{
	if (self == world && other == world)
		full_startframes = full_startframes + 1;
};

void() CSQC_Shutdown =
{
	if (autocvar_hq_vm_restart && autocvar_hq_restart_stage == 1)
	{
		if (full_reload_light >= 0 && (float)dynamiclight_get(full_reload_light, LFIELD_RADIUS) == 73)
			print("csqcharness: reload shutdown with static light\n");
	}
};

void(float apilevel, string enginename, float engineversion) CSQC_Init =
{
	full_api = apilevel;
	if (autocvar_hq_vm_restart && autocvar_hq_restart_stage == 1)
	{
		st_eqs("token/vm-reload-clears-input", getentitytoken(), "");
		st_eqf("light/vm-reload-clears-static-light", (float)dynamiclight_get(cvar("hq_reload_light"), LFIELD_RADIUS), 0);
	}
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
	if (full_frames == 30)
		localcmd(strcat("samelevel ", ftos(!cvar("samelevel")), "\n"));	// toggle a serverinfo cvar: the server's //svi broadcast drives CSQC_ServerInfoChanged
	if (full_frames == 40) cprint("STATE");
	if (full_frames == 50)
		cq_setup_entity_physics();
	if (full_frames == 60)
	{
		if (autocvar_hq_vm_restart && autocvar_hq_restart_stage == 0)
		{
			getentitytoken("owned input before VM reload");
			full_reload_light = dynamiclight_spawnstatic('1 2 3', 73, '1 1 1');
			cvar_set("hq_reload_light", ftos(full_reload_light));
			cvar_set("hq_restart_stage", "1");
			print("csqcharness: restarting with owned token input\n");
			localcmd(strcat("map ", mapname, "\n"));
			return;
		}
		st_eqf("full/server-approved-api", full_api, 1);
		st_ok("full/updateview-ran", TRUE);
		st_ok("full/startframe-with-world-context", full_startframes > 0);
		st_ok("full/renderscene-survives", full_scenes > 0);
		st_client_ui_globals([width, height, 0]);
		st_bufstr_find();
		cq_parity_models();
		cq_parity_tokens();
		cq_parity_extensions();
		cq_parity_viewprops();
		cq_extended_viewprops();
		cq_render_entity_fields();
		cq_trace_metadata();
		cq_legacy_pmove();
		cq_check_entity_physics();
		cq_followup_builtins();
		cq_followup_resources();
		cq_parity_gaps();
		st_parity_gaps();
		st_report("fullcsqc");
		localcmd("quit\n");
	}
	drawsetcliparea(0, 0, 1, 1);
};
