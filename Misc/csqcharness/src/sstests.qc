/*
	sstests.qc -- server half of the SSQC lane: a minimal game progs that only
	spawns the player, runs its own server-side checks, then fires the step
	table from ss_shared.qc one step per frame.

	Server-side names are prefixed "ss/" so the runner can reconcile them
	against this module's tally separately from the client's.
*/

float ss_pass, ss_fail;

void(string name, float ok) ss_ok =
{
	if (ok)
	{
		ss_pass = ss_pass + 1;
		print("SELFTEST PASS ss/", name, "\n");
	}
	else
	{
		ss_fail = ss_fail + 1;
		print("SELFTEST FAIL ss/", name, "\n");
	}
};

void(string name, float got, float want) ss_eqf =
{
	if (got == want)
	{
		ss_pass = ss_pass + 1;
		print("SELFTEST PASS ss/", name, "\n");
	}
	else
	{
		ss_fail = ss_fail + 1;
		print("SELFTEST FAIL ss/", name, " got=", ftos(got), " want=", ftos(want), "\n");
	}
};

.float pitch_speed;		// DP_QC_CHANGEPITCH: mods declare it themselves

entity ss_player, ss_testent;
float ss_frames, ss_step, ss_selfchecked;

void() main = {};
void() SetNewParms = {};
void() SetChangeParms = {};
void() ClientKill = {};
void() ClientConnect = {};
void() ClientDisconnect = {};
void() PlayerPreThink = {};
void() PlayerPostThink = {};
void() info_player_start = {};

void() worldspawn =
{
	precache_model("progs/player.mdl");
	precache_model("progs/s_explod.spr");
};

void() PutClientInServer =
{
	local entity spot = find(world, classname, "info_player_start");
	self.classname = "player";
	self.solid = SOLID_SLIDEBOX;
	self.movetype = MOVETYPE_WALK;
	self.flags = FL_CLIENT;
	self.health = 100;
	setmodel(self, "progs/player.mdl");
	setsize(self, '-16 -16 -24', '16 16 32');
	setorigin(self, spot.origin + '0 0 1');
	self.view_ofs = '0 0 22';
	self.angles = spot.angles;
	self.fixangle = TRUE;
	ss_player = self;
};

void(float seq) ss_marker =
{
	WriteByte(MSG_MULTICAST, 23);	// svc_temp_entity
	WriteByte(MSG_MULTICAST, 2);	// TE_GUNSHOT
	WriteCoord(MSG_MULTICAST, SS_MARKER_X);
	WriteCoord(MSG_MULTICAST, SS_MARKER_Y);
	WriteCoord(MSG_MULTICAST, seq * 8);
	multicast(ss_player.origin, MULTICAST_PVS);
};

void(float yaw) ss_setangle =
{
	msg_entity = ss_player;
	WriteByte(MSG_ONE, 10);		// svc_setangle
	WriteAngle(MSG_ONE, 0);
	WriteAngle(MSG_ONE, yaw);
	WriteAngle(MSG_ONE, 0);
};

void() ss_selfchecks =
{
	local entity oldself, e;

	ss_eqf("ext-te-spark", checkextension("DP_TE_SPARK"), 1);
	ss_eqf("ext-sv-effect", checkextension("DP_SV_EFFECT"), 1);
	ss_eqf("ext-paused", checkextension("FTE_QC_PAUSED"), 1);
	ss_eqf("ext-changepitch", checkextension("DP_QC_CHANGEPITCH"), 1);
	ss_eqf("ext-fte-te-standard", checkextension("FTE_TE_STANDARDEFFECTBUILTINS"), 1);
	ss_eqf("ext-calltimeofday", checkextension("FTE_CALLTIMEOFDAY"), 1);
	ss_eqf("ext-no-rotatemoves", checkextension("DP_CSQC_ROTATEMOVES"), 0);
	ss_eqf("ext-no-bloodshower", checkextension("DP_TE_BLOODSHOWER"), 0);

	ss_eqf("setpause-returns-previous-off", setpause(1), 0);
	ss_eqf("setpause-returns-previous-on", setpause(0), 1);
	ss_eqf("setpause-idempotent", setpause(0), 0);

	oldself = self;
	e = spawn();
	self = e;
	self.angles = '0 0 0';
	self.idealpitch = 30;
	self.pitch_speed = 10;
	changepitch(self);
	ss_ok("changepitch-step", fabs(self.angles_x - 10) < 0.01);
	self.idealpitch = -170;
	self.pitch_speed = 400;	// out of range: ignored
	changepitch(self);
	ss_ok("changepitch-bad-speed-ignored", fabs(self.angles_x - 10) < 0.01);
	self = oldself;
	remove(e);

	ss_eqf("pointcontentsmask-outside-solid", utof(pointcontentsmask('0 0 -60000')), utof(CONTENTBIT_SOLID));
	ss_eqf("pointcontentsmask-player-empty", utof(pointcontentsmask(ss_player.origin)), 0);
	ss_ok("fseek64-registered", checkbuiltin(fseek64));
	ss_ok("digest-sha512", digest_hex("SHA512", "") == "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
};

float(entity client, float flags) ss_send_entity =
{
	WriteByte(MSG_MULTICAST, self.health);
	return TRUE;
};

void(vector org) ss_nehahra_explosion =
{
	WriteByte(MSG_MULTICAST, 23); // svc_temp_entity
	WriteByte(MSG_MULTICAST, 16); // TENEH_EXPLOSION3
	WriteCoord(MSG_MULTICAST, org_x);
	WriteCoord(MSG_MULTICAST, org_y);
	WriteCoord(MSG_MULTICAST, org_z);
	WriteCoord(MSG_MULTICAST, 0.5);
	WriteCoord(MSG_MULTICAST, 0.25);
	WriteCoord(MSG_MULTICAST, 0.125);
	multicast(org, MULTICAST_PVS);
};

void() ss_fire_step =
{
	local vector org = ss_player.origin;
	local float nan = stof("nan");
	local vector vnan = [nan, nan, nan];
	local float big = 1000000000;
	local float huge = big * big * big * 1000;	// 1e30; fteqcc has no exponent literals
	local vector vhuge = [huge, -huge, huge];

	switch (ss_step)
	{
	case 0:		te_blood(org, '0 0 50', 10); break;
	case 1:		te_spark(org, '0 0 100', 20); break;
	case 2:		te_explosionrgb(org, '1 0.5 0'); break;
	case 3:		te_particlecube(org - '32 32 0', org + '32 32 32', '0 0 10', 20, 15, 1, 5); break;
	case 4:		te_bloodshower(org - '16 16 0', org + '16 16 16', 50, 10); break;
	case 5:		te_smallflash(org); break;
	case 6:		te_customflash(org, 200, 0.5, '1 0 0'); break;
	case 7:		te_flamejet(org, '0 0 100', 20); break;
	case 8:		te_plasmaburn(org); break;
	case 9:		te_gunshotquad(org); break;
	case 10:	te_spikequad(org); break;
	case 11:	te_superspikequad(org); break;
	case 12:	te_explosionquad(org); break;
	case 13:	effect(org, "progs/s_explod.spr", 0, 0, 10); break;
	case 14:	effect(org, "progs/hq_not_precached.spr", 0, 0, 10); break;
	case 15:	te_bloodqw(org, 2); break;
	// hostile arguments: the server must still write a well-formed message and
	// the client must survive whatever comes out of it
	case 16:	te_spark(vnan, vhuge, big); break;
	case 17:	te_customflash(org, big, big, [big, -big, nan]); break;
	case 18:	te_particlecube(vnan, vhuge, vnan, big, nan, nan, nan); break;
	case 19:	te_explosionrgb(vhuge, [-5, 5, nan]); break;
	case 20:	te_bloodshower(org + '16 16 16', org - '16 16 0', -big, big); break;
	case 21:	effect(org, "progs/s_explod.spr", big, -5, -5); break;
	case 22:
	case 23:
		if (!cvar("hq_ss_quit_server"))
			ss_nehahra_explosion(org);
		break;
	case 24:	effect(org, "progs/s_explod.spr", 256, 1, 10); break; // effect2
	}
	ss_marker(ss_step);
};

void() StartFrame =
{
	if (!ss_player)
		return;
	ss_frames = ss_frames + 1;
	if (ss_frames < 20)
		return;	// let the client finish signon and load its csqc
	if (!ss_selfchecked)
	{
		ss_selfchecked = TRUE;
		ss_selfchecks();
	}
	if (ss_step < SS_STEPS)
		ss_fire_step();
	else if (ss_step == SS_ENT_SPAWN && !cvar("hq_ss_quit_server"))
	{
		ss_testent = spawn();
		ss_testent.pvsflags = 3; // PVSF_IGNOREPVS
		setorigin(ss_testent, ss_player.origin);
		ss_testent.SendEntity = ss_send_entity;
		ss_testent.SendFlags = 1;
		ss_testent.health = 17;
	}
	else if (ss_step == SS_ENT_UPDATE && ss_testent)
	{
		ss_testent.health = 23;
		ss_testent.SendFlags = 1;
	}
	else if (ss_step == SS_ENT_REMOVE && ss_testent)
		remove(ss_testent);
	else if (ss_step == SS_INFO_STEP)
	{
		forceinfokey(world, "hq_test", "server");
		forceinfokey(ss_player, "hq_player", "hq_test");
	}
	else if (ss_step == SS_SETANGLE1)
		ss_setangle(SS_YAW1);
	else if (ss_step == SS_SETANGLE2)
		ss_setangle(SS_YAW2);
	else if (ss_step == SS_DELTA_STEP && !cvar("hq_ss_quit_server"))
	{
		msg_entity = ss_player;
		WriteByte(MSG_ONE, 85); // svcfte_setangledelta
		WriteShort(MSG_ONE, 0);
		WriteShort(MSG_ONE, SS_YAW_DELTA * (65536 / 360));
		WriteShort(MSG_ONE, 0);
	}
	else if (ss_step == SS_DONE_STEP)
	{
		ss_marker(SS_DONE_SEQ);
		print("SELFTEST DONE ssqc pass=", ftos(ss_pass), " fail=", ftos(ss_fail), "\n");
		if (cvar("hq_ss_quit_server"))
			localcmd("quit\n");	// no csqc to finish the run (vanilla-protocol client)
	}
	ss_step = ss_step + 1;
};
