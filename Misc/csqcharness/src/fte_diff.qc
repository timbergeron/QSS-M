/*
	fte_diff.qc -- differential probe. Built as ftediff.dat (a full CSQC) and
	run unchanged under QSS-M and FTEQW on the same map; diff_fte.py compares
	the "DIFF <name> <value>" lines. Only deterministic results go here: no
	random, time, or engine-identity values.
*/

float df_frames;

void(string name, string value) df =
{
	print("DIFF ", name, " ", value, "\n");
};
void(string name, float value) dff =
{
	df(name, sprintf("%.6g", value));
};
void(string name, vector value) dfv =
{
	df(name, sprintf("%.4g %.4g %.4g", value_x, value_y, value_z));
};

void() df_math =
{
	dff("math/sin", sin(1));
	dff("math/cos", cos(1));
	dff("math/sqrt", sqrt(2));
	dff("math/sqrt-neg-is-nan", sqrt(-1) != sqrt(-1));
	dff("math/pow", pow(2, 10));
	dff("math/pow-frac", pow(2, 0.5));
	dff("math/atan2", atan2(1, -1));
	dff("math/tan", tan(0.5));
	dff("math/asin", asin(0.5));
	dff("math/acos", acos(0.5));
	dff("math/min", min(3, -2, 7));
	dff("math/max", max(3, -2, 7));
	dff("math/bound", bound(0, 12, 10));
	dff("math/rint-half", rint(2.5));
	dff("math/rint-neg-half", rint(-2.5));
	dff("math/floor", floor(-1.5));
	dff("math/ceil", ceil(-1.5));
	dff("math/fabs", fabs(-3.25));
	dff("math/bitshift-left", bitshift(3, 4));
	dff("math/bitshift-right", bitshift(256, -3));
	dff("math/anglemod-neg", anglemod(-90));
	dff("math/anglemod-big", anglemod(725));
};

void() df_vectors =
{
	dfv("vec/vectoangles", vectoangles('1 1 1'));
	dfv("vec/vectoangles-down", vectoangles('0 0 -1'));
	dfv("vec/vectoangles-roll", vectoangles('1 0 0', '0 1 1'));
	makevectors('30 45 10');
	dfv("vec/makevectors-forward", v_forward);
	dfv("vec/makevectors-right", v_right);
	dfv("vec/makevectors-up", v_up);
	dfv("vec/normalize", normalize('3 4 0'));
	dfv("vec/normalize-zero", normalize('0 0 0'));
	dff("vec/vlen", vlen('1 2 2'));
	dff("vec/vectoyaw", vectoyaw('-1 1 0'));
	dfv("vec/crossproduct", crossproduct('1 0 0', '0 1 0'));
	vectorvectors('0 0 1');
	dfv("vec/vectorvectors-forward", v_forward);
	dfv("vec/vectorvectors-right", v_right);
	makevectors('0 0 0');
	rotatevectorsbyangle('0 90 0');
	dfv("vec/rotatevectorsbyangle", v_forward);
};

void() df_strings =
{
	local string s;
	df("str/ftos-int", ftos(42));
	df("str/ftos-frac", ftos(1.5));
	df("str/ftos-small", ftos(0.0001));
	df("str/ftos-neg", ftos(-7.25));
	df("str/ftos-big", ftos(16777216));
	df("str/vtos", vtos('1 -2.5 3'));
	df("str/sprintf-d", sprintf("%d|%5d|%-5d|", 42, 42, 42));
	df("str/sprintf-f", sprintf("%f|%.2f|%8.3f", 3.14159, 3.14159, 3.14159));
	df("str/sprintf-s", sprintf("[%s][%10s][%-10s]", "ab", "ab", "ab"));
	df("str/sprintf-x", sprintf("%x|%X|%08x", 255, 255, 255));
	df("str/sprintf-v", sprintf("%v", '1 2 3'));
	df("str/sprintf-pct", sprintf("100%%"));
	dff("str/strlen", strlen("hello"));
	df("str/substring", substring("abcdef", 1, 3));
	df("str/substring-neg-start", substring("abcdef", -2, 2));
	df("str/substring-neg-len", substring("abcdef", 1, -2));
	df("str/substring-past-end", substring("abc", 2, 10));
	dff("str/strstrofs", strstrofs("hello world", "o", 5));
	dff("str/strstrofs-miss", strstrofs("hello", "z"));
	df("str/strtolower", strtolower("MiXeD"));
	df("str/strtoupper", strtoupper("MiXeD"));
	df("str/strreplace", strreplace("a", "bb", "banana"));
	df("str/strireplace", strireplace("A", "x", "banana"));
	df("str/strpad-left", strpad(6, "ab"));
	df("str/strpad-right", strpad(-6, "ab"));
	dff("str/str2chr", str2chr("ABC", 1));
	dff("str/str2chr-neg", str2chr("ABC", -1));
	df("str/chr2str", chr2str(72, 105));
	dff("str/stof-hex", stof("0x10"));
	dff("str/stof-space", stof("  5"));
	dff("str/stof-junk", stof("abc"));
	dff("str/stof-trailing", stof("12abc"));
	dfv("str/stov", stov("'1 2 3'"));
	dfv("str/stov-bare", stov("4 5 6"));
	dff("str/strncmp", strncmp("abcd", "abce", 3));
	dff("str/strcasecmp", strcasecmp("ABC", "abc"));
	dff("str/strncasecmp", strncasecmp("ABCx", "abcy", 3));
	df("str/strdecolorize", strdecolorize("^1red^7white"));
	dff("str/strlennocol", strlennocol("^1red^7white"));
	df("str/uri_escape", uri_escape("a b/c?d=e"));
	df("str/uri_unescape", uri_unescape("a%20b%2Fc"));
	df("str/infoadd", infoadd("\\a\\1", "b", "2"));
	df("str/infoget", infoget("\\a\\1\\b\\2", "b"));
	s = strzone("zoned");
	df("str/strzone", s);
	strunzone(s);
	df("str/itos", itos(-123i));
	dff("str/stoi", itof(stoi("456")));
	df("str/htos", htos(255i));
	dff("str/stoh", itof(stoh("ff")));
	dff("str/crc16", crc16(FALSE, "hello"));
	dff("str/crc16-insensitive", crc16(TRUE, "HELLO"));
	df("str/digest-md4", digest_hex("MD4", "abc"));
	df("str/digest-md5", digest_hex("MD5", "abc"));
	df("str/digest-sha1", digest_hex("SHA1", "abc"));
	df("str/digest-sha224", digest_hex("SHA224", "abc"));
	df("str/digest-sha256", digest_hex("SHA256", "abc"));
	df("str/digest-sha384", digest_hex("SHA384", "abc"));
	df("str/digest-sha512", digest_hex("SHA512", "abc"));
	df("str/digest-crc16", digest_hex("CRC16", "abc"));
	df("str/digest-unknown", digest_hex("NOPE", "abc"));
	df("str/strconv-upper", strconv(2, 0, 0, "abc"));
	df("str/strconv-lower", strconv(1, 0, 0, "ABC"));
	dff("str/strconv-red-char", str2chr(strconv(0, 1, 1, "a1"), 0));	// compare codes: fte's stdout colours high-bit chars
	dff("str/strconv-red-digit", str2chr(strconv(0, 1, 1, "a1"), 1));
	df("str/ftos-tenth", ftos(0.1));
	df("str/ftos-third", ftos(1/3));
	df("str/ftos-nan", ftos(sqrt(-1)));
};

void() df_tokens =
{
	dff("tok/tokenize-count", tokenize("a \"b c\" d"));
	df("tok/tokenize-1", argv(1));
	dff("tok/tokenize-start", argv_start_index(2));
	dff("tok/tokenize-end", argv_end_index(2));
	dff("tok/console-count", tokenize_console("a \"b c\" // d"));
	df("tok/console-1", argv(1));
	dff("tok/separator-count", tokenizebyseparator("a,b,,c", ","));
	df("tok/separator-2", argv(2));
	df("tok/separator-3", argv(3));
	dff("tok/separator-multi", tokenizebyseparator("a::b;c", "::", ";"));
	df("tok/separator-multi-2", argv(2));
	df("tok/argv-past-end", argv(99));
	dff("tok/tokenize-empty", tokenize(""));
};

void() df_buffers =
{
	local strbuf b = buf_create();
	bufstr_set(b, 0, "zeta");
	bufstr_set(b, 2, "alpha");
	bufstr_add(b, "mid", TRUE);
	dff("buf/size", buf_getsize(b));
	df("buf/get-hole", bufstr_get(b, 1));
	df("buf/implode", buf_implode(b, ","));
	buf_sort(b, 0, FALSE);
	df("buf/sorted", buf_implode(b, ","));
	buf_sort(b, 0, TRUE);
	df("buf/sorted-reverse", buf_implode(b, ","));
	df("buf/get-out-of-range", bufstr_get(b, 99));
	buf_del(b);
};

void() df_world =
{
	local entity e = spawn();
	local entity oldself = self;
	dff("world/pointcontents-outside", pointcontents('0 0 -60000'));
	if (checkbuiltin(pointcontentsmask))
		dff("world/pointcontentsmask-outside", utof(pointcontentsmask('0 0 -60000')));
	else
		df("world/pointcontentsmask-outside", "<missing>");
	traceline('480 -352 100', '480 -352 -1000', TRUE, world);
	dff("world/trace-down-fraction", trace_fraction);
	dfv("world/trace-down-endpos", trace_endpos);
	dfv("world/trace-down-normal", trace_plane_normal);
	tracebox('480 -352 100', '-16 -16 -24', '16 16 32', '480 -352 -1000', TRUE, world);
	dfv("world/tracebox-down-endpos", trace_endpos);

	self = e;
	self.angles = '0 0 0';
	self.idealpitch = 350;
	self.pitch_speed = 20;
	changepitch(self);
	dff("world/changepitch-wrap", self.angles_x);
	self.angles = '0 170 0';
	self.ideal_yaw = -170;
	self.yaw_speed = 30;
	changeyaw();
	dff("world/changeyaw-wrap", self.angles_y);
	self = oldself;
	remove(e);

	dff("world/particleeffectnum-nonzero", particleeffectnum("TE_EXPLOSION") != 0);
	if (checkbuiltin(particleeffectquery))
		df("world/particleeffectquery-name", particleeffectquery(particleeffectnum("TE_EXPLOSION"), 0));
	else
		df("world/particleeffectquery-name", "<missing>");
	dff("world/cvar_type-gravity", cvar_type("sv_gravity"));
	dff("world/cvar_type-missing", cvar_type("hq_no_such_cvar"));
	df("world/cvar_string-missing", cvar_string("hq_no_such_cvar"));
};

void() df_extensions =
{
	local float i, n = tokenize("DP_QC_CHANGEPITCH DP_CSQC_ROTATEMOVES DP_QC_DIGEST_SHA256 FTE_QC_DIGEST_SHA1 FTE_QC_DIGEST_SHA224 FTE_QC_DIGEST_SHA384 FTE_QC_DIGEST_SHA512 FTE_CALLTIMEOFDAY FTE_TE_STANDARDEFFECTBUILTINS FTE_QC_PAUSED DP_SV_EFFECT DP_TE_BLOOD DP_TE_CUSTOMFLASH DP_TE_EXPLOSIONRGB DP_TE_PARTICLECUBE DP_TE_SMALLFLASH DP_TE_SPARK DP_TE_BLOODSHOWER DP_TE_FLAMEJET DP_TE_PLASMABURN DP_TE_QUADEFFECTS1 FTE_MVD_PLAYERSTATS DP_GFX_FONTS DP_QC_URI_GET");
	local string names[24];
	for (i = 0; i < n; i++)
		names[i] = strzone(argv(i));
	for (i = 0; i < n; i++)
	{
		dff(strcat("ext/", names[i]), checkextension(names[i]));
		strunzone(names[i]);
	}
};

void(float vwidth, float vheight, float notmenu) CSQC_UpdateView =
{
	df_frames = df_frames + 1;
	clearscene();
	renderscene();
	if (df_frames == 10)
	{
		df_math();
		df_vectors();
		df_strings();
		df_tokens();
		df_buffers();
		df_world();
		df_extensions();
		print("DIFF-DONE\n");
		localcmd("quit\n");
	}
};
