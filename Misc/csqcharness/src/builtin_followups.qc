// Follow-up checks for the ordinary light, rotation, font and input APIs.
#ifdef CSQC
float cq_timeofday_called;
void(float seconds, float minutes, float hours, float day, float month, float year, string text) timeofday =
{
	cq_timeofday_called = seconds >= 0 && seconds <= 60 && minutes >= 0 && minutes < 60 &&
		hours >= 0 && hours < 24 && day >= 1 && day <= 31 && month >= 0 && month < 12 &&
		year >= 2000 && text != "";
};

void() cq_followup_files =
{
	local searchhandle search;
	local filestream file, other;
	local strbuf buffer;
	local entity item;

	file = fopen("hq_pkg.txt", 0);
	st_ok("filesystem/pk3-open", file >= 0);
	if (file >= 0)
	{
		st_eqs("filesystem/pk3-data-priority", fgets(file), "compressed data entry");
		st_eqs("filesystem/pk3-eof", fgets(file), "");
		fclose(file);
	}
	file = fopen("hq_large.bin", 0);
	other = fopen("hq_large.bin", 0);
	st_ok("filesystem/pk3-independent-opens", file >= 0 && other >= 0);
	if (file >= 0)
	{
		st_ok("filesystem/pk3-large-size", fsize(file) == 131077i);
		fseek64(file, fsize64(file));
		st_ok("filesystem/pk3-eof-64-bit-seek", fseek(file) == 131077i);
		fseek(file, 131072i);
		st_eqs("filesystem/pk3-large-seek", fgets(file), "tail");
		fclose(file);
	}
	if (other >= 0)
	{
		st_ok("filesystem/pk3-independent-position", fseek(other) == 0i);
		fclose(other);
	}
	file = fopen("hq_empty.txt", 0);
	st_ok("filesystem/pk3-deflated-empty", file >= 0 && fsize(file) == 0i);
	if (file >= 0)
		fclose(file);
	file = fopen("hq_stored.txt", 0);
	st_ok("filesystem/pk3-stored-open", file >= 0);
	if (file >= 0)
	{
		st_eqs("filesystem/pk3-stored-bounds", fgets(file), "stored boundary");
		fclose(file);
	}
	file = fopen("hq_corrupt.txt", 0);
	st_eqf("filesystem/pk3-corrupt-denied", (float)file, -1);
	if (file >= 0)
		fclose(file);
	search = search_begin("hq_pkg.txt", 0, 1, "hq_files.pk3");
	st_eqf("filesystem/pk3-search", search_getsize(search), 1);
	file = search_fopen(search, 0);
	st_ok("filesystem/pk3-search-open", file >= 0);
	if (file >= 0)
	{
		st_eqs("filesystem/pk3-search-exact", fgets(file), "compressed root entry");
		fclose(file);
	}
	search_end(search);
	search = search_begin("hq_pkg.txt", 2, 1);
	st_eqs("filesystem/whichpack-virtual", strcat(whichpack("hq_pkg.txt"), "/hq_files.pk3"), search_getpackagename(search, 0));
	search_end(search);
	st_eqf("filesystem/pk3-loose-exists", fexists("hq_pkg.txt"), 0);
	buffer = buf_create();
	st_ok("filesystem/pk3-buf-open", buf_loadfile("hq_pkg.txt", buffer));
	st_eqs("filesystem/pk3-buf-data", bufstr_get(buffer, 0), "compressed data entry");
	st_eqf("filesystem/buf-outside-denied", buf_loadfile("hq_external.txt", buffer), 0);
	buf_del(buffer);
	st_eqf("filesystem/pk3-entities", loadfromfile("hq_pkg.ent"), 0);
	item = find(world, classname, "hq_packed_entity");
	st_ok("filesystem/pk3-entity-restored", item != world);
	if (item != world)
		removeinstant(item);
	if (checkbuiltin(fcopy))
	{
		st_eqf("filesystem/pk3-copy", fcopy("hq_pkg.txt", "hq_copy.txt"), 0);
		file = fopen("hq_copy.txt", 0);
		if (file >= 0)
		{
			st_eqs("filesystem/pk3-copy-data", fgets(file), "compressed data entry");
			fclose(file);
		}
		else
			st_ok("filesystem/pk3-copy-data", 0);
		if (checkbuiltin(fremove))
			fremove("hq_copy.txt");
	}
	file = fopen("hq_append.txt", 2);
	st_ok("filesystem/write", file >= 0);
	if (file >= 0)
	{
		fputs(file, "first\n");
		fclose(file);
	}
	file = fopen("hq_append.txt", 1);
	st_ok("filesystem/append", file >= 0);
	if (file >= 0)
	{
		fputs(file, "second\n");
		fclose(file);
	}
	file = fopen("hq_append.txt", 0);
	st_ok("filesystem/read-written", file >= 0);
	if (file >= 0)
	{
		st_eqs("filesystem/append-first", fgets(file), "first");
		st_eqs("filesystem/append-second", fgets(file), "second");
		fclose(file);
	}
	file = fopen("hq_external.txt", 0);
	st_eqf("filesystem/read-outside-denied", (float)file, -1);
	if (file >= 0)
		fclose(file);
	search = search_begin("data/hq_external.txt", 0, 1);
	st_eqf("filesystem/search-outside-file-denied", search_getsize(search), 0);
	search_end(search);
	search = search_begin("data/hq_external_dir/*.txt", 0, 1);
	st_eqf("filesystem/search-outside-parent-denied", search_getsize(search), 0);
	search_end(search);
	if (whichpack("hq_symlink_fixture.txt") != "")
	{
		file = fopen("hq_external.txt", 2);
		st_eqf("filesystem/write-outside-denied", (float)file, -1);
		if (file >= 0)
			fclose(file);
		file = fopen("hq_external_dir/hq_external.txt", 2);
		st_eqf("filesystem/write-outside-parent-denied", (float)file, -1);
		if (file >= 0)
			fclose(file);
	}
};

void() cq_followup_resources =
{
	local entity item = spawn();
	local string data, sample;
	local float framecount, sound_index, status;
	local searchhandle search;
	local filestream file;

	item.classname = "hq_file_roundtrip";
	item.origin = '1 2 3';
	st_eqf("entity/previous-writable", entityprotection(item, 1), 0);
	st_eqf("entity/query-protected", entityprotection(item, -1), 1);
	st_eqf("entity/previous-protected", entityprotection(item, 0), 1);
	data = generateentitydata(item);
	st_ok("entity/serialized", strlen(data) > 4);
	st_eqf("entity/loadfromdata", loadfromdata(data), 0);
	st_eqv("entity/restored-origin", find(item, classname, "hq_file_roundtrip").origin, '1 2 3');

	framecount = modelframecount(getmodelindex("progs/player.mdl", 1));
	st_ok("resource/player-model-frames", framecount > 1);
	st_eqf("resource/known-model", resourcestatus(RESTYPE_MODEL, 0, "progs/player.mdl"), RESSTATE_LOADED);
	st_eqf("resource/missing-model", resourcestatus(RESTYPE_MODEL, 0, "progs/hq_missing.mdl"), RESSTATE_NOTKNOWN);
	status = resourcestatus(RESTYPE_PIC, 1, "gfx/hq_grid.tga");
	st_eqf("resource/picture-loaded", status, RESSTATE_LOADED);
	freepic("gfx/hq_grid.tga");
	st_eqf("resource/picture-evicted", resourcestatus(RESTYPE_PIC, 0, "gfx/hq_grid.tga"), RESSTATE_NOTKNOWN);

	sound_index = 1;
	sample = soundnameforindex(sound_index);
	st_ok("audio/precached-name", strlen(sample) > 0);
	st_eqf("audio/sound-index-roundtrip", getsoundindex(sample, 1), sound_index);
	st_eqf("audio/missing-time", getsoundtime(item, 7), -1);
	st_eqf("audio/missing-level", getchannellevel(item, 7), -1);
	st_eqf("audio/no-sample-update", soundupdate(item, 7, "", 0.2, 0), 0);
	stopsound(item, 7);

	search = search_begin("gfx/hq_grid.tga", 0, 1);
	st_ok("filesystem/search-match", search_getsize(search) >= 1);
	file = search_fopen(search, 0);
	st_ok("filesystem/search-file-size", fsize(file) > 18i);
	fclose(file);
	search_end(search);
	st_ok("filesystem/loose-exists", fexists("gfx/hq_grid.tga"));
	cq_followup_files();
	st_ok("utility/timeofday-registered", checkbuiltin(calltimeofday));
	calltimeofday();
	st_ok("utility/timeofday-called", cq_timeofday_called);

	st_eqf("effect/model-slot", builtin_find("effect"), 404);
	st_eqf("effect/blood-slot", builtin_find("te_blood"), 405);
	st_eqf("effect/bloodqw-slot", builtin_find("te_bloodqw"), 239);
	st_eqf("effect/flamejet-slot", builtin_find("te_flamejet"), 457);
	st_eqf("effect/customflash-slot", builtin_find("te_customflash"), 417);
	te_blood('0 0 0', '1 0 0', 0);
	te_customflash('0 0 0', 2, 0.05, '1 1 1');
	removeinstant(item);
};
void() cq_followup_builtins =
{
	local float light;
	local vector forward, right, up;

	makevectors('0 0 0');
	rotatevectorsbyangle('0 90 0');
	st_ok("rotation/yaw", vlen(v_forward - '0 1 0') < 0.001 && vlen(v_right - '1 0 0') < 0.001);
	rotatevectorsbyangle('90 0 0');
	st_ok("rotation/composed-pitch", vlen(v_forward - '0 0 1') < 0.001 && vlen(v_up - '0 -1 0') < 0.001);
	makevectors('0 0 0');
	rotatevectorsbyangle('0 0 90');
	st_ok("rotation/roll", vlen(v_right - '0 0 -1') < 0.001 && vlen(v_up - '0 -1 0') < 0.001);

	makevectors('0 90 0');
	rotatevectorsbyvectors('2 0 0', '0 -3 0', '1 0 4');
	forward = v_forward;
	right = v_right;
	up = v_up;
	st_ok("rotation/matrix-forward", vlen(forward - '0 3 0') < 0.001);
	st_ok("rotation/matrix-right", vlen(right - '2 0 0') < 0.001);
	st_ok("rotation/matrix-up", vlen(up - '1 0 4') < 0.001);

	st_eqf("font/unknown", findfont("hq-missing-font"), 0);
	st_eqf("font/load", loadfont("hq-font", "gfx/hq_grid.tga", "8", 20), 20);
	st_eqf("font/slot-name", findfont("HQ-FONT"), 20);
	st_eqf("font/face-name", findfont("gfx/hq_grid.tga"), 20);
	st_eqf("font/load-conflicting-name", loadfont("gfx/hq_grid.tga", "gfx/hq_direct.tga", "8", 21), 21);
	st_eqf("font/named-slot-before-face", findfont("gfx/hq_grid.tga"), 21);

	light = dynamiclight_add('11 22 33', 42, '1 0.5 0.25', 7);
	st_ok("dlight/allocated", light >= 0);
	st_eqv("dlight/added-origin", (vector)dynamiclight_get(light, LFIELD_ORIGIN), '11 22 33');
	st_eqv("dlight/added-color", (vector)dynamiclight_get(light, LFIELD_COLOUR), '1 0.5 0.25');
	st_eqf("dlight/added-radius", (float)dynamiclight_get(light, LFIELD_RADIUS), 42);
	st_eqf("dlight/added-style", (float)dynamiclight_get(light, LFIELD_STYLE), 7);
	dynamiclight_set(light, LFIELD_ORIGIN, '44 55 66');
	dynamiclight_set(light, LFIELD_COLOUR, '0.5 0.25 1');
	dynamiclight_set(light, LFIELD_RADIUS, 23);
	dynamiclight_set(light, LFIELD_STYLE, 3);
	dynamiclight_set(light, LFIELD_DIETIME, 50);
	dynamiclight_set(light, LFIELD_RGBDECAY, '2 3 4');
	dynamiclight_set(light, LFIELD_RADIUSDECAY, 5);
	st_eqv("dlight/set-origin", (vector)dynamiclight_get(light, LFIELD_ORIGIN), '44 55 66');
	st_eqv("dlight/set-color", (vector)dynamiclight_get(light, LFIELD_COLOUR), '0.5 0.25 1');
	st_eqf("dlight/set-radius", (float)dynamiclight_get(light, LFIELD_RADIUS), 23);
	st_eqf("dlight/set-style", (float)dynamiclight_get(light, LFIELD_STYLE), 3);
	st_eqf("dlight/set-dietime", (float)dynamiclight_get(light, LFIELD_DIETIME), 50);
	st_eqv("dlight/set-rgbdecay", (vector)dynamiclight_get(light, LFIELD_RGBDECAY), '2 3 4');
	st_eqf("dlight/set-radiusdecay", (float)dynamiclight_get(light, LFIELD_RADIUSDECAY), 5);
	st_eqv("dlight/invalid-handle", (vector)dynamiclight_get(-1, LFIELD_ORIGIN), '0 0 0');
	st_eqv("dlight/nonfinite-handle", (vector)dynamiclight_get(stof("nan"), LFIELD_ORIGIN), '0 0 0');
	st_eqv("dlight/unsupported-field", (vector)dynamiclight_get(light, 5), '0 0 0');
	st_ok("dlight/capacity", (float)dynamiclight_get(-1, -1) > light);
	dynamiclight_set(light, LFIELD_RADIUS, 0);
};
#endif

void() st_followup_client_builtins =
{
	local vector oldbindmaps = getbindmaps();
	st_eqs("utility/md5", digest_hex("MD5", "abc"), "900150983cd24fb0d6963f7d28e17f72");
	st_eqs("utility/sha1", digest_hex("SHA1", "abc"), "a9993e364706816aba3e25717850c26c9cd0d89d");
	st_eqs("utility/sha256", digest_hex("SHA256", "a", "bc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	st_eqf("utility/argescape-count", tokenize_console(argescape("two words")), 1);
	st_eqs("utility/argescape-value", argv(0), "two words");
	st_eqf("bindmaps/set-returns-true", setbindmaps('3 4 12'), 1);
	st_eqv("bindmaps/roundtrip", getbindmaps(), '3 4 0');
	st_eqf("bindmaps/normalize", setbindmaps([stof("nan"), 999, 0]), 1);
	st_eqv("bindmaps/out-of-range", getbindmaps(), '0 0 0');
	setbindmaps(oldbindmaps);
	st_eqv("bindmaps/restored", getbindmaps(), oldbindmaps);
	st_eqs("resolver/explicit-v4-port", netaddress_resolve("127.0.0.1:27500", 26000), "127.0.0.1:27500");
	st_eqs("resolver/default-v4-port", netaddress_resolve("127.0.0.1", 26000), "127.0.0.1:26000");
	st_eqs("resolver/no-port", netaddress_resolve("127.0.0.1"), "127.0.0.1");
	st_eqs("resolver/explicit-v6-port", netaddress_resolve("[::1]:27500"), "[::1]:27500");
	st_eqs("resolver/malformed", netaddress_resolve("[::1"), "");
	st_eqs("extresponse/empty", getextresponse(), "");
};

// csqc-parity-gaps: advertised extensions, digest variants and the small builtins/globals/entrypoints that closed the FTE gaps.
#ifdef CSQC
float cq_playerinfo_calls, cq_serverinfo_calls;
void(float playernum) CSQC_PlayerInfoChanged =
{
	if (playernum >= 0 && playernum < maxclients)
		cq_playerinfo_calls = cq_playerinfo_calls + 1;
};
void() CSQC_ServerInfoChanged =
{
	cq_serverinfo_calls = cq_serverinfo_calls + 1;
};

void() cq_parity_gaps =
{
	local float light, efnum, slot, i, capacity, nan = stof("nan");
	local float light_slots[64];
	local entity oldself;

	st_eqf("gaps/ext-rotatemoves", checkextension("DP_CSQC_ROTATEMOVES"), 1);
	st_eqf("gaps/ext-changepitch", checkextension("DP_QC_CHANGEPITCH"), 1);
	st_eqf("gaps/ext-sv-effect", checkextension("DP_SV_EFFECT"), 1);
	st_eqf("gaps/ext-te-blood", checkextension("DP_TE_BLOOD"), 1);
	st_eqf("gaps/ext-te-spark", checkextension("DP_TE_SPARK"), 1);
	st_eqf("gaps/ext-te-smallflash", checkextension("DP_TE_SMALLFLASH"), 1);
	st_eqf("gaps/ext-te-customflash", checkextension("DP_TE_CUSTOMFLASH"), 1);
	st_eqf("gaps/ext-te-explosionrgb", checkextension("DP_TE_EXPLOSIONRGB"), 1);
	st_eqf("gaps/ext-calltimeofday", checkextension("FTE_CALLTIMEOFDAY"), 1);
	st_eqf("gaps/ext-paused", checkextension("FTE_QC_PAUSED"), 1);
	st_eqf("gaps/ext-fte-te-standard", checkextension("FTE_TE_STANDARDEFFECTBUILTINS"), 1);
	st_eqf("gaps/ext-bloodshower-unadvertised", checkextension("DP_TE_BLOODSHOWER"), 0);
	st_eqf("gaps/ext-mvd-playerstats-unadvertised", checkextension("FTE_MVD_PLAYERSTATS"), 0);

	st_eqf("gaps/pointcontentsmask-outside-solid", utof(pointcontentsmask('0 0 -60000')), utof(CONTENTBIT_SOLID));
	st_eqf("gaps/pointcontentsmask-allbsps-outside-solid", utof(pointcontentsmask('0 0 -60000', 0)), utof(CONTENTBIT_SOLID));

	efnum = particleeffectnum("TE_EXPLOSION");
	st_eqs("gaps/particleeffectquery-name", particleeffectquery(efnum, 0), "TE_EXPLOSION");
	st_eqs("gaps/particleeffectquery-body-null", particleeffectquery(efnum, 1), "");
	st_eqs("gaps/particleeffectquery-unknown", particleeffectquery(0, 0), "");
	st_eqs("gaps/particleeffectquery-out-of-range", particleeffectquery(-3000000000, 0), "");
	st_eqs("gaps/particleeffectquery-nan", particleeffectquery(nan, 0), "");
	st_eqf("gaps/pointcontentsmask-nan", utof(pointcontentsmask([nan, 0, 0])), 0);

	light = dynamiclight_spawnstatic('1 2 3', 50, '1 0.5 0.25');
	st_ok("gaps/staticlight-allocated", light >= 0);
	st_eqv("gaps/staticlight-origin", (vector)dynamiclight_get(light, LFIELD_ORIGIN), '1 2 3');
	st_ok("gaps/staticlight-persistent", (float)dynamiclight_get(light, LFIELD_DIETIME) > time + 100000);
	dynamiclight_set(light, LFIELD_RADIUS, 0);
	st_poisonf();
	st_eqf("gaps/staticlight-rejects-zero-radius", dynamiclight_spawnstatic('1 2 3', 0, '1 1 1'), -1);
	st_eqf("gaps/staticlight-rejects-nan-origin", dynamiclight_spawnstatic([nan, 0, 0], 20, '1 1 1'), -1);

	capacity = min(64, (float)dynamiclight_get(-1, -1));
	light = dynamiclight_spawnstatic('1 2 3', 50, '1 1 1');
	slot = light;
	for (i = 0; i < capacity && slot >= 0; i = i + 1)
	{
		light_slots[i] = slot;
		slot = dynamiclight_spawnstatic('1 2 3', 20, '1 1 1');
	}
	st_eqf("gaps/staticlight-pool-full", slot, -1);
	te_smallflash('10 20 30');
	st_eqf("gaps/staticlight-survives-full-pool", (float)dynamiclight_get(light, LFIELD_RADIUS), 50);
	while (i > 0)
	{
		i = i - 1;
		dynamiclight_set(light_slots[i], LFIELD_RADIUS, 0);
	}

	st_eqf("gaps/setpause-returns-previous-off", setpause(1), 0);
	st_eqf("gaps/setpause-returns-previous-on", setpause(0), 1);

	oldself = self;
	self = spawn();
	self.angles = '0 0 0';
	self.idealpitch = 30;
	self.pitch_speed = 10;
	changepitch(self);
	st_ok("gaps/changepitch-step", fabs(self.angles_x - 10) < 0.01);	// anglemod quantises to 360/65536, like fte
	remove(self);
	self = oldself;

	st_eqf("gaps/numclientseats", numclientseats, 1);
	st_eqf("gaps/gamespeed", gamespeed, 1);
	st_ok("gaps/servertime-set", servertime > 0 && servertime >= serverprevtime);
	st_ok("gaps/serverdeltatime", serverdeltatime == servertime - serverprevtime);
	st_ok("gaps/playerinfochanged-called", cq_playerinfo_calls > 0);
	st_ok("gaps/serverinfochanged-called", cq_serverinfo_calls > 0);	// full.qc flips the samelevel serverinfo cvar at frame 30
	localcmd("samelevel 0\n");

	te_spark('0 0 0', '0 0 100', 10);
	te_smallflash('0 0 0');
	te_explosionrgb('0 0 0', '1 0 0');
	effect('0 0 0', "progs/s_explod.spr", 0, 0, 10);
};
#endif

void() st_parity_gaps =
{
	st_eqs("gaps/sha224", digest_hex("SHA224", "abc"), "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7");
	st_eqs("gaps/sha384", digest_hex("SHA384", "abc"), "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7");
	st_eqs("gaps/sha512", digest_hex("SHA512", "abc"), "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
	st_eqf("gaps/ext-digest-sha256", checkextension("DP_QC_DIGEST_SHA256"), 1);
	st_eqf("gaps/ext-digest-sha1", checkextension("FTE_QC_DIGEST_SHA1"), 1);
	st_eqf("gaps/ext-digest-sha224", checkextension("FTE_QC_DIGEST_SHA224"), 1);
	st_eqf("gaps/ext-digest-sha384", checkextension("FTE_QC_DIGEST_SHA384"), 1);
	st_eqf("gaps/ext-digest-sha512", checkextension("FTE_QC_DIGEST_SHA512"), 1);
	st_ok("gaps/fseek64-registered", checkbuiltin(fseek64));
	st_ok("gaps/fsize64-registered", checkbuiltin(fsize64));
	st_ok("gaps/drawrotpic_dp-registered", checkbuiltin(drawrotpic_dp));
	st_ok("gaps/addwantedhostcachekey-registered", checkbuiltin(addwantedhostcachekey));
	addwantedhostcachekey("ping");
#ifdef CSQC
	st_eqf("gaps/drawrotpic_dp-slot", builtin_find("drawrotpic_dp"), 329);
#else
	st_eqf("gaps/menu-no-rotatemoves", checkextension("DP_CSQC_ROTATEMOVES"), 0);
	st_eqf("gaps/menu-no-paused", checkextension("FTE_QC_PAUSED"), 0);
	st_eqf("gaps/menu-no-te-spark", checkextension("DP_TE_SPARK"), 0);
#endif
	st_eqf("gaps/drawrotpic-dp-return",
		drawrotpic_dp('100 100 0', "gfx/hq_grid.tga", '32 32 0', '16 16 0', 45, '1 1 1', 1), 1);
};
