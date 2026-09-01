/* Transactional extension and rotating-pusher ownership checks. */

entity test_rotator;
entity collision_probe;
entity rider_pusher;
entity rider;
entity rider_trigger;
entity linear_pusher;
entity mixed_pusher;
#ifdef QEX
entity stopped_pusher;
entity tweened_pusher;
#endif
entity blocked_pusher;
entity blocked_entity;
entity blocking_wall;
float test_case;
float test_frame;
float test_pass;
float test_fail;
float test_done;
float expected_angle;
float rotation_ok;
float rotation_query;
float precache_query;
float effects_before;
float effects_query;
float effects_after;
float probe_initial_across_y;
float probe_initial_across_x;
float probe_rotated_across_y;
float probe_rotated_across_x;
float rider_trigger_count;
float blocked_count;
vector rider_start;
vector rider_angles_start;

void(string name, float condition) test_ok =
{
	if (condition)
	{
		test_pass = test_pass + 1;
		dprint("SELFTEST PASS ", name, "\n");
	}
	else
	{
		test_fail = test_fail + 1;
		dprint("SELFTEST FAIL ", name, "\n");
	}
};

void(string name, float got, float wanted) test_eq =
{
	if (got == wanted)
		test_ok(name, 1);
	else
	{
		test_fail = test_fail + 1;
		dprint("SELFTEST FAIL ", name, " got=", ftos(got), " want=", ftos(wanted), "\n");
	}
};

float(float angle) wrap_angle =
{
	while (angle >= 360)
		angle = angle - 360;
	while (angle < 0)
		angle = angle + 360;
	return angle;
};

entity(string model_name, vector new_origin) spawn_pusher =
{
	entity result;

	result = spawn();
	result.classname = "selftest_pusher";
	result.movetype = MOVETYPE_PUSH;
	result.solid = SOLID_BSP;
	setmodel(result, model_name);
	setorigin(result, new_origin);
	result.nextthink = 999999;
	return result;
};

float(vector start, vector end) trace_hit =
{
	traceline(start, end, 0, rider_trigger);
	return trace_fraction < 1 || trace_startsolid;
};

void() test_trigger_touch =
{
	if (other == rider_pusher)
		rider_trigger_count = rider_trigger_count + 1;
};

void() test_blocked =
{
	blocked_count = blocked_count + 1;
};

#ifdef QEX
float() finaleFinished =
{
	return 0;
};
#endif

void() worldspawn =
{
	test_case = cvar("ssqc_case");
	rotation_ok = 1;

	/* Case 5 sweeps other stateful extensions before probing rotation. */
	if (test_case == 5)
	{
		effects_before = checkextension("EX_EXTENDED_EF");
		effects_query = checkextension("DP_EF_RED");
		effects_after = checkextension("EX_EXTENDED_EF");
		precache_query = checkextension("DP_SV_PRECACHEANYTIME");
		rotation_query = checkextension("DP_SV_ROTATINGBMODEL");
	}
	else
	{
		/* Case 4 proves that ordinary QC which never queries remains legacy. */
		if (test_case == 4)
			rotation_query = -1;
		else
			rotation_query = checkextension("DP_SV_ROTATINGBMODEL");

		effects_before = checkextension("EX_EXTENDED_EF");
		effects_query = checkextension("DP_EF_RED");
		effects_after = checkextension("EX_EXTENDED_EF");
		precache_query = checkextension("DP_SV_PRECACHEANYTIME");
	}

	test_rotator = spawn_pusher("*1", '0 0 0');
	test_rotator.classname = "selftest_rotator";
	test_rotator.avelocity = '0 90 0';

	/* A directly angled thin brush proves both rotated hull and broadphase use. */
	collision_probe = spawn_pusher("*1", '256 0 0');
	collision_probe.avelocity = '0 0.001 0';

	/* The ordinary DP path must continue to orbit and orient grounded riders. */
	rider_pusher = spawn_pusher("*2", '0 256 0');
	rider_pusher.avelocity = '0 90 0';
	rider = spawn();
	rider.classname = "selftest_rider";
	rider.movetype = MOVETYPE_WALK;
	rider.solid = SOLID_BBOX;
	setsize(rider, '-4 -4 0', '4 4 16');
	setorigin(rider, '40 256 10');
	rider.flags = FL_ONGROUND;
	rider.groundentity = rider_pusher;
	rider_start = rider.origin;
	rider_angles_start = rider.angles;

	rider_trigger = spawn();
	rider_trigger.classname = "selftest_trigger";
	rider_trigger.movetype = MOVETYPE_NONE;
	rider_trigger.solid = SOLID_TRIGGER;
	rider_trigger.touch = test_trigger_touch;
	setsize(rider_trigger, '-80 -80 -16', '80 80 32');
	setorigin(rider_trigger, '0 256 0');

	linear_pusher = spawn_pusher("*4", '0 512 0');
	linear_pusher.velocity = '40 0 0';

	mixed_pusher = spawn_pusher("*4", '0 768 0');
	mixed_pusher.velocity = '40 0 0';
	mixed_pusher.avelocity = '0 90 0';

#ifdef QEX
	/* Rerelease stop/tween semantics retain nominal avelocity in both cases. */
	stopped_pusher = spawn_pusher("*5", '-256 0 0');
	stopped_pusher.avelocity = '0 90 0';
	tweened_pusher = spawn_pusher("*6", '-256 256 0');
	tweened_pusher.avelocity = '0 90 0';
#endif

	/* Constrain an intersected bbox so ordinary angular pushing must roll back. */
	if (test_case == 0)
	{
		blocked_pusher = spawn_pusher("*1", '0 1024 0');
		blocked_pusher.avelocity = '0 90 0';
		blocked_pusher.blocked = test_blocked;

		blocking_wall = spawn_pusher("*3", '-160 1011 0');
		blocked_entity = spawn();
		blocked_entity.classname = "selftest_blocker";
		blocked_entity.movetype = MOVETYPE_STEP;
		blocked_entity.solid = SOLID_BBOX;
		blocked_entity.flags = FL_FLY;
		setsize(blocked_entity, '-4 -4 -4', '4 4 4');
		setorigin(blocked_entity, '0 1024 0');
	}
};

void() test_extension_results =
{
#ifdef QEX
	test_eq("rotation/qex-query-rejected", rotation_query, 0);
	test_eq("effects/qex-extended-before", effects_before, 1);
	if (test_case == 2)
	{
		test_eq("effects/blocked-query-rejected", effects_query, 0);
		test_eq("effects/blocked-query-no-mutation", effects_after, 1);
		test_eq("precache/blocked-query-rejected", precache_query, 0);
	}
	else
	{
		test_eq("effects/accepted-query", effects_query, 1);
		test_eq("effects/accepted-query-mutates", effects_after, 0);
		test_eq("precache/accepted-query", precache_query, 1);
	}
#else
	test_eq("effects/ordinary-extended-before", effects_before, 0);
	test_eq("effects/ordinary-extended-after", effects_after, 0);
	if (test_case == 0)
	{
		test_eq("rotation/ordinary-query-accepted", rotation_query, 1);
		test_eq("effects/accepted-query", effects_query, 1);
		test_eq("precache/accepted-query", precache_query, 1);
	}
	else if (test_case == 1)
	{
		test_eq("rotation/blocked-query-rejected", rotation_query, 0);
		test_eq("effects/blocked-query-rejected", effects_query, 0);
		test_eq("precache/blocked-query-rejected", precache_query, 0);
	}
	else
	{
		test_eq("rotation/ordinary-query-skipped", rotation_query, -1);
		test_eq("effects/accepted-query", effects_query, 1);
		test_eq("precache/accepted-query", precache_query, 1);
	}
#endif
};

void() test_motion_results =
{
	test_ok("collision/initial-thin-axis-hit", probe_initial_across_y);
	test_ok("collision/initial-off-axis-miss", !probe_initial_across_x);
#ifdef QEX
	test_ok("collision/rotated-old-axis-miss", !probe_rotated_across_y);
	test_ok("collision/rotated-new-axis-hit", probe_rotated_across_x);
#else
	if (test_case == 0)
	{
		test_ok("collision/rotated-old-axis-miss", !probe_rotated_across_y);
		test_ok("collision/rotated-new-axis-hit", probe_rotated_across_x);
	}
	else
	{
		test_ok("collision/legacy-old-axis-hit", probe_rotated_across_y);
		test_ok("collision/legacy-new-axis-miss", !probe_rotated_across_x);
	}
#endif

	test_ok("linear/origin-advanced", linear_pusher.origin_x > 1);
	test_ok("linear/ltime-advanced", linear_pusher.ltime > 0);
	test_ok("linear/no-angle", fabs(linear_pusher.angles_y) < 0.05);
	test_ok("mixed/origin-advanced", mixed_pusher.origin_x > 1);
	test_ok("mixed/ltime-advanced", mixed_pusher.ltime > 0);

#ifdef QEX
	test_ok("mixed/external-angle-advanced", mixed_pusher.angles_y > 1);
	test_ok("stopped/external-angle-stationary",
		fabs(stopped_pusher.angles_y) < 0.05);
	test_ok("tween/external-scaled-delta",
		tweened_pusher.angles_y > 0.1 &&
		tweened_pusher.angles_y < test_rotator.angles_y * 0.5);
	test_ok("rider/external-origin-unchanged",
		fabs(rider.origin_x - rider_start_x) < 0.05 &&
		fabs(rider.origin_y - rider_start_y) < 0.05);
	test_ok("rider/external-angles-unchanged",
		fabs(rider.angles_y - rider_angles_start_y) < 0.05);
	test_eq("trigger/external-relink-does-not-touch", rider_trigger_count, 0);
#else
	if (test_case == 0)
	{
		test_ok("mixed/engine-angle-advanced", mixed_pusher.angles_y > 1);
		test_ok("rider/engine-origin-orbited",
			fabs(rider.origin_x - rider_start_x) > 0.1 ||
			fabs(rider.origin_y - rider_start_y) > 0.1);
		test_ok("rider/engine-angles-updated", rider.angles_y > 1);
		test_ok("trigger/engine-pusher-touch", rider_trigger_count > 0);
		dprint("SELFTEST INFO blocked count=", ftos(blocked_count),
			" angle=", ftos(blocked_pusher.angles_y),
			" ltime=", ftos(blocked_pusher.ltime), "\n");
		test_ok("blocked/ordinary-callback", blocked_count > 0);
		test_ok("blocked/ordinary-angle-rollback",
			fabs(blocked_pusher.angles_y - 90 * blocked_pusher.ltime) < 0.1);
		test_ok("blocked/ordinary-ltime-rollback",
			blocked_pusher.ltime < rider_pusher.ltime);
	}
		else
		{
			test_ok("mixed/legacy-angle-stationary", fabs(mixed_pusher.angles_y) < 0.05);
		test_ok("rider/legacy-origin-unchanged",
			fabs(rider.origin_x - rider_start_x) < 0.05 &&
			fabs(rider.origin_y - rider_start_y) < 0.05);
		test_ok("rider/legacy-angles-unchanged",
			fabs(rider.angles_y - rider_angles_start_y) < 0.05);
			test_eq("trigger/legacy-no-angular-touch", rider_trigger_count, 0);
		}
#endif
};

void() StartFrame =
{
	if (test_done)
		return;

	/* This observes the previous frame after pusher physics has completed. */
#ifndef QEX
	if (test_case == 0)
		expected_angle = wrap_angle(90 * test_rotator.ltime);
#endif
	if (test_frame > 0 && fabs(test_rotator.angles_y - expected_angle) > 0.05)
		rotation_ok = 0;

	/* Exercise the warning after ss_loading, never during worldspawn. */
	if (test_frame == 2)
	{
		precache_sound("misc/menu1.wav");
		collision_probe.angles_y = 90;
	}

	if (test_frame == 1)
	{
		probe_initial_across_y = trace_hit('304 -80 0', '304 80 0');
		probe_initial_across_x = trace_hit('176 48 0', '336 48 0');
	}
	if (test_frame == 3)
	{
		probe_rotated_across_y = trace_hit('304 -80 0', '304 80 0');
		probe_rotated_across_x = trace_hit('176 48 0', '336 48 0');
	}

#ifdef QEX
	/* Match rerelease QC: incremental, wrapped integration in StartFrame. */
	test_rotator.angles_y = wrap_angle(test_rotator.angles_y + 90 * frametime);
	expected_angle = wrap_angle(expected_angle + 90 * frametime);
	rider_pusher.angles_y = wrap_angle(rider_pusher.angles_y + 90 * frametime);
	mixed_pusher.angles_y = wrap_angle(mixed_pusher.angles_y + 90 * frametime);
	tweened_pusher.angles_y = wrap_angle(tweened_pusher.angles_y + 22.5 * frametime);
#endif

	test_frame = test_frame + 1;
	if (test_frame == 12)
	{
		test_extension_results();
		test_motion_results();
#ifdef QEX
		test_ok("rotation/external-angles-integrated-once", rotation_ok);
#else
		if (test_case == 0)
		{
			test_ok("rotation/engine-time-advanced", test_rotator.ltime > 0);
			test_ok("rotation/engine-angles-integrated-once", rotation_ok);
		}
		else
			test_ok("rotation/legacy-angles-not-integrated", rotation_ok);
#endif
		dprint("SELFTEST DONE ssqc pass=", ftos(test_pass), " fail=", ftos(test_fail), "\n");
		test_done = 1;
		localcmd("quit\n");
	}
};

void() main = {};
void() info_player_start = {};
void() PlayerPreThink = {};
void() PlayerPostThink = {};
void() ClientKill = {};
void() ClientConnect = {};
void() PutClientInServer = {};
void() ClientDisconnect = {};
void() SetNewParms = {};
void() SetChangeParms = {};
