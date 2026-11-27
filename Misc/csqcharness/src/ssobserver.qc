/*
	ssobserver.qc -- client half of the SSQC lane (full CSQC, installed as
	csprogs.dat next to ssprogs.dat). Watches the server's step table, then
	exercises vid_restart and reports. Names are prefixed "sscl/".
*/

float obs_frames, obs_next, obs_last_type, obs_done_frame;
float obs_setangle_calls, obs_first_yaw, obs_second_yaw, obs_yaw_before_second;
float obs_restart_calls, obs_delta_calls, obs_delta_yaw;
float obs_renderer_valid, obs_init_called, obs_initial_restart, obs_nehahra_light;
float obs_playerinfo, obs_serverinfo;
float obs_spawns, obs_updates, obs_removes, obs_startframes;
entity obs_first_entity, obs_replacement;
.float obs_number;

void(float seq) obs_marker =
{
	if (seq == SS_DONE_SEQ)
	{
		obs_done_frame = obs_frames;
		// vid_restart is a no-op unless a video cvar changed, so change one
		localcmd("vid_width 640; vid_height 480; vid_restart\n");
		return;
	}
	st_eqf(strcat("sscl/marker-order-", ftos(seq)), seq, obs_next);
	st_eqf(strcat("sscl/te-type-", ftos(seq)), obs_last_type, ss_expected_type(seq));
	obs_next = seq + 1;
	if (seq == 22)
		localcmd("r_explosionlight 1\n");
};

void(float apilevel, string enginename, float engineversion) CSQC_Init =
{
	obs_last_type = -1;
	obs_init_called = TRUE;
	physics_mode = 0;
	localcmd("r_explosionlight 0\n");
};

float() CSQC_Parse_TempEntity =
{
	local float t = readbyte();
	local vector p;

	if (t == 2)
	{
		p_x = readcoord();
		p_y = readcoord();
		p_z = readcoord();
		if (p_x == SS_MARKER_X && p_y == SS_MARKER_Y)
		{
			obs_marker(p_z / 8);
			obs_last_type = -1;
			return TRUE;
		}
	}
	if (t == 16)
	{
		if (obs_next == 22)
			st_eqf("sscl/nehahra-in-dark", cvar("r_explosionlight"), 0);
		else
			obs_nehahra_light = cvar("r_explosionlight");
	}
	obs_last_type = t;
	return FALSE;	// the engine rewinds and parses it itself
};

float(float a, float b) obs_yaw_near;
// The server also sends setangle for fixangle on spawn; only the two test yaws count.
float(vector angles, float isdelta) CSQC_Parse_SetAngles =
{
	if (isdelta)
	{
		obs_delta_calls = obs_delta_calls + 1;
		obs_delta_yaw = angles_y;
		return FALSE;
	}
	if (obs_yaw_near(angles_y, SS_YAW1))
	{
		obs_setangle_calls = obs_setangle_calls + 1;
		obs_first_yaw = angles_y;
		return TRUE;	// suppress
	}
	if (obs_yaw_near(angles_y, SS_YAW2))
	{
		obs_setangle_calls = obs_setangle_calls + 1;
		obs_second_yaw = angles_y;
		obs_yaw_before_second = view_angles_y;
	}
	return FALSE;
};

void(string rendererdescription) CSQC_RendererRestarted =
{
	obs_restart_calls = obs_restart_calls + 1;
	print("csqcharness: renderer callback\n");
	obs_renderer_valid = rendererdescription != "";
	if (obs_init_called && !obs_frames)
		obs_initial_restart = obs_initial_restart + 1;
};

float(float a, float b) obs_yaw_near =
{
	local float d = fabs(anglemod(a) - anglemod(b));
	return d < 2 || d > 358;	// WriteAngle quantises to 1.4 degrees on vanilla coords
};

void(float player) CSQC_PlayerInfoChanged =
{
	if (getplayerkeyvalue(player, "hq_player") == "hq_test")
		obs_playerinfo = obs_playerinfo + 1;
};

void() CSQC_ServerInfoChanged =
{
	if (serverkey("hq_test") == "server")
		obs_serverinfo = obs_serverinfo + 1;
};

void(float number) CSQC_Ent_Spawn =
{
	obs_spawns = obs_spawns + 1;
	self = obs_first_entity = spawn();
	self.entnum = number;
	self.obs_number = 11;
};

void(float isnew) CSQC_Ent_Update =
{
	local float value = readbyte();
	obs_updates = obs_updates + 1;
	if (isnew)
	{
		st_eqf("sscl/entity-first-payload", value, 17);
		st_ok("sscl/entity-spawn-self", self == obs_first_entity && self.obs_number == 11 && self.entnum > 0);
		obs_replacement = spawn();
		obs_replacement.entnum = self.entnum;
		remove(self);
		self = obs_replacement;
	}
	else
	{
		st_eqf("sscl/entity-second-payload", value, 23);
		st_ok("sscl/entity-update-self", self == obs_replacement);
	}
};

void() CSQC_Ent_Remove =
{
	obs_removes = obs_removes + 1;
	st_ok("sscl/entity-remove-self", self == obs_replacement);
	remove(self);
};

void() StartFrame =
{
	if (self == world && other == world)
		obs_startframes = obs_startframes + 1;
};

void() obs_report =
{
	st_eqf("sscl/all-markers-seen", obs_next, SS_STEPS);
	st_ok("sscl/startframe-with-physics-disabled", obs_startframes > 0);
	st_eqf("sscl/setangle-calls", obs_setangle_calls, 2);
	st_ok("sscl/setangle-first-value", obs_yaw_near(obs_first_yaw, SS_YAW1));
	st_ok("sscl/setangle-second-value", obs_yaw_near(obs_second_yaw, SS_YAW2));
	st_ok("sscl/setangle-suppressed", !obs_yaw_near(obs_yaw_before_second, SS_YAW1));
	st_eqf("sscl/setangle-delta-calls", obs_delta_calls, 1);
	st_ok("sscl/setangle-delta-value", obs_yaw_near(obs_delta_yaw, SS_YAW_DELTA));
	st_ok("sscl/setangle-applied", obs_yaw_near(view_angles_y, SS_YAW2 + SS_YAW_DELTA));
	st_eqf("sscl/rendererrestarted-at-init", obs_initial_restart, 1);
	st_eqf("sscl/rendererrestarted-total", obs_restart_calls, 2);
	st_ok("sscl/rendererrestarted-description", obs_renderer_valid);
	st_eqf("sscl/nehahra-with-light", obs_nehahra_light, 1);
	st_eqf("sscl/entity-spawn-count", obs_spawns, 1);
	st_eqf("sscl/entity-update-count", obs_updates, 2);
	st_eqf("sscl/entity-remove-count", obs_removes, 1);
	st_eqf("sscl/playerinfo-updated-before-callback", obs_playerinfo, 1);
	st_eqf("sscl/serverinfo-updated-before-callback", obs_serverinfo, 1);
	st_report("sscsqc");
	localcmd("disconnect; vid_width 800; vid_height 600; vid_restart; quit\n");
};

void(float vwidth, float vheight, float notmenu) CSQC_UpdateView =
{
	obs_frames = obs_frames + 1;
	clearscene();
	renderscene();
	if (obs_done_frame && obs_frames == obs_done_frame + 30)
		obs_report();
	else if (obs_frames == 3000)
	{
		st_ok("sscl/server-finished-in-time", FALSE);
		obs_report();
	}
};
