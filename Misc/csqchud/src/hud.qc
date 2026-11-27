/*
	csqchud -- a portable simple-CSQC HUD that recreates QSS-M's scr_sbar 1/2/3
	status bars from standard CSQC builtins only, and lets every widget be moved.

	Only CSQC_DrawHud is used, so it also runs as "simple CSQC" (a local
	csprogs.dat the server didn't send). It uses standard CSQC builtins only,
	plus optional QSS-M serverkey() keys (pq_*, qssm_*) that read as empty on
	other engines; the same progs runs on FTEQW.

	Layout model. Each widget has a native size (in 320x200-era HUD units) and:
		chud_<w>_show    ""=preset default, 0 hide, 1 show
		chud_<w>_anchor  "ax ay"  point of the widget pinned to the same point
		                          of the screen (0 0 top-left, 0.5 1 bottom-centre)
		chud_<w>_ofs     "x y"    offset from that point, in widget units
		chud_<w>_scale   "s"      multiplier on top of scr_sbarscale
	Empty strings mean "use the preset's value", so presets stay pixel-exact
	until something is overridden.

	chud_preset 0 follows scr_sbar; 1 classic, 2 QuakeWorld, 3 Quake re-release.
*/

// ---- item bits (id1) ----
const float IT_SHOTGUN = 1;
const float IT_LIGHTNING = 64;
const float IT_SHELLS = 256;
const float IT_NAILS = 512;
const float IT_ROCKETS = 1024;
const float IT_CELLS = 2048;
const float IT_ARMOR1 = 8192;
const float IT_ARMOR2 = 16384;
const float IT_ARMOR3 = 32768;
const float IT_KEY1 = 131072;
const float IT_KEY2 = 262144;
const float IT_INVISIBILITY = 524288;
const float IT_INVULNERABILITY = 1048576;
const float IT_QUAD = 4194304;

// ---- widgets ----
const float W_STATUS = 0;	// sbar row: armor, face, health, ammo (presets 1, 2)
const float W_IBAR = 1;		// classic inventory strip: weapons + ammo counts (preset 1)
const float W_ITEMS = 2;	// keys, powerups, sigils row (presets 1, 2)
const float W_FRAGS = 3;	// frag counts + ProQuake clock/team totals (preset 1, multiplayer)
const float W_QWAMMO = 4;	// side column: weapons + ammo counts (presets 2, 3)
const float W_QEARMOR = 5;	// re-release armor block (preset 3)
const float W_QEHEALTH = 6;	// re-release face + health (preset 3)
const float W_QEAMMO = 7;	// re-release current ammo (preset 3)
const float W_QEKEYS = 8;	// re-release keys + sigils (preset 3)
const float W_SPEED = 9;	// speed readout (follows scr_showspeed)
const float W_MOVEKEYS = 10;	// movement keys (follows scr_movekeys)
const float W_LOC = 11;		// location name (follows r_showlocs)
const float W_COUNT = 12;

string widget_name[W_COUNT] = {"status", "ibar", "items", "frags", "qwammo", "qe_armor", "qe_health", "qe_ammo", "qe_keys", "speed", "movekeys", "loc"};
vector widget_size[W_COUNT] = {'320 24', '320 24', '128 16', '128 8', '44 188', '104 25', '104 25', '106 24', '80 16', '32 8', '24 24', '64 8'};

// resolved per frame
vector loc_size;	// the loc widget is as wide as the current name
string loc_name;	// looked up only while the loc widget is shown
vector scr;		// virtual screen size (CSQC_DrawHud's argument)
float preset;
float sbaralpha;
float hud_items, hud_sigils, hud_viewsize;
vector wpos;	// current widget's top-left on screen
float wscale;	// current widget's scale

// pickup flash bookkeeping, mirrors cl.item_gettime
float item_gettime[32];
float lastitems;
float lasthealth;
float faceanimtime;

float(float i) bit =
{
	float b = 1;
	while (i > 0)
	{
		b = b * 2;
		i = i - 1;
	}
	return b;
};

// ---- cvars ----
// QSS-M's scr_sbar* cvars don't exist on other engines; cvar() would read 0.
float(string name, float def) cvar_or =
{
	if (!cvar_type(name))
		return def;
	return cvar(name);
};

float(string s) is_blank = { return s == "" || s == "-"; };

void() chud_registercvars =
{
	float i;
	registercvar("chud_preset", "0");
	registercvar("chud_autotest", "0");
	registercvar("chud_frags_count", "2");
	for (i = 0; i < W_COUNT; i++)
	{
		registercvar(strcat("chud_", widget_name[i], "_show"), "");
		registercvar(strcat("chud_", widget_name[i], "_anchor"), "");
		registercvar(strcat("chud_", widget_name[i], "_ofs"), "");
		registercvar(strcat("chud_", widget_name[i], "_scale"), "");
	}
};

// ---- preset defaults ----
// Anchors/offsets below reproduce the engine's GL canvases exactly at
// scr_sbarscale 1 (CANVAS_SBAR, CANVAS_IBAR_QW, CANVAS_BOTTOMLEFTQE, ...).

float(float w) preset_show =
{	// the overlay widgets follow the engine's own toggles, so a HUD shows what the player enabled
	if (w == W_SPEED)
		return cvar_or("scr_showspeed", 0) >= 1;
	if (w == W_MOVEKEYS)
		return cvar_or("scr_movekeys", 0) != 0;
	if (w == W_LOC)
		return cvar_or("r_showlocs", 0) != 0;
	if (preset == 1)
		return w == W_STATUS || w == W_IBAR || w == W_ITEMS || w == W_FRAGS;
	if (preset == 2)
		return w == W_STATUS || w == W_ITEMS || w == W_QWAMMO;
	return w == W_QWAMMO || w == W_QEARMOR || w == W_QEHEALTH || w == W_QEAMMO || w == W_QEKEYS;
};

vector(float w) preset_anchor =
{
	if (w == W_LOC)
		return '0.5 0.5';
	if ((w == W_SPEED || w == W_MOVEKEYS) && preset == 3)
		return '0 1';
	if (w == W_QEARMOR || w == W_QEHEALTH)
		return '0 1';
	if (w == W_QWAMMO || w == W_QEAMMO || w == W_QEKEYS)
		return '1 1';
	return '0.5 1';
};

vector(float w) preset_ofs =
{
	if (w == W_STATUS)	return '0 0';
	if (w == W_IBAR)	return '0 -24';
	if (w == W_ITEMS)	return '96 -24';	// sbar x 192, y -16
	if (w == W_FRAGS)	return '88 -40';	// sbar x 184, canvas y 0
	if (w == W_QWAMMO)
	{
		if (preset == 3)
			return '-21 -117';	// CANVAS_IBAR_QWQE
		return '0 -48';			// CANVAS_IBAR_QW
	}
	if (w == W_SPEED)
	{	// SCR_DrawSpeed: CANVAS_SBAR2 x 0, y 208 (233 at viewsize 110 or QW); QE x 134, y 177
		if (preset == 3)
			return '134 -15';
		if (preset == 2 || hud_viewsize >= 110)
			return '-144 -29';
		return '-144 -54';
	}
	if (w == W_MOVEKEYS)
	{	// SCR_DrawMovementKeys centre (x, y); the widget box starts 8px up and left of it
		if (preset == 3)
			return (cvar_or("scr_showspeed", 0) == 1) ? '164 -36' : '166 -18';
		if (preset == 2)
			return (cvar_or("scr_showspeed", 0) == 1) ? '-146 -44' : '-146 -30';
		if (cvar_or("scr_showspeed", 0) == 1)
			return (hud_viewsize >= 110) ? '-146 -42' : '-146 -68';
		return (hud_viewsize >= 110) ? '-146 -30' : '-146 -56';
	}
	if (w == W_LOC)
		return [0, cvar_or("r_showlocs_y", 30) + 4, 0];	// below the crosshair
	if (w == W_QEARMOR)	return '18 -36';
	if (w == W_QEHEALTH)	return '18 -12';
	if (w == W_QEAMMO)	return '-16 -12';
	if (w == W_QEKEYS)
	{
		if (cvar_or("scr_sbarshowqeammo", 1))
			return '-16 -40';
		return '-16 -64';
	}
	return '0 0';
};

float(float w) preset_scale =
{
	float s, qs;
	if (w == W_QWAMMO && preset == 3)
	{	// CANVAS_IBAR_QWQE runs at scr_sbarscale/1.4 while the CSQC canvas is at scr_sbarscale
		s = max(1, cvar("scr_sbarscale"));
		qs = max(1, cvar("scr_sbarscale") / 1.4);
		return qs / s;
	}
	if (w == W_MOVEKEYS && preset == 3)
	{	// CANVAS_BOTTOMLEFTQESMALL: scr_sbarscale/1.2
		s = max(1, cvar("scr_sbarscale"));
		qs = max(1, cvar("scr_sbarscale") / 1.2);
		return qs / s;
	}
	return 1;
};

// engine viewsize gates: inventory < 110, classic status < 120, re-release vitals <= 110
float(float w) widget_viewsize_ok =
{
	if (w == W_SPEED || w == W_MOVEKEYS)
		return hud_viewsize <= 110;
	if (w == W_LOC)
		return TRUE;
	if (w == W_STATUS)
		return hud_viewsize < 120;
	if (w == W_QEARMOR || w == W_QEHEALTH || w == W_QEAMMO)
		return hud_viewsize <= 110;
	return hud_viewsize < 110;
};

// resolve user overrides, set wpos/wscale, return FALSE if hidden
float(float w) widget_begin =
{
	string s;
	float show;
	vector anchor, ofs, size;

	s = cvar_string(strcat("chud_", widget_name[w], "_show"));
	if (is_blank(s))
		show = preset_show(w) && widget_viewsize_ok(w);
	else
		show = stof(s);
	if (!show)
		return FALSE;
	if (w == W_LOC)
	{	// centred on its own width
		loc_name = serverkey("qssm_loc");
		loc_size = [strlen(loc_name) * 8, 8, 0];
	}

	s = cvar_string(strcat("chud_", widget_name[w], "_anchor"));
	anchor = is_blank(s) ? preset_anchor(w) : stov(s);
	s = cvar_string(strcat("chud_", widget_name[w], "_ofs"));
	ofs = is_blank(s) ? preset_ofs(w) : stov(s);
	s = cvar_string(strcat("chud_", widget_name[w], "_scale"));
	wscale = is_blank(s) ? preset_scale(w) : stof(s);
	if (wscale <= 0)
		return FALSE;

	size = ((w == W_LOC) ? loc_size : widget_size[w]) * wscale;
	wpos_x = anchor_x * scr_x - anchor_x * size_x + ofs_x * wscale;
	wpos_y = anchor_y * scr_y - anchor_y * size_y + ofs_y * wscale;
	return TRUE;
};

// ---- drawing helpers, all in current-widget units ----
void(vector p, string pic, float alpha) wpic =
{
	drawpic(wpos + p * wscale, pic, drawgetimagesize(pic) * wscale, '1 1 1', alpha, 0);
};

// sub-rectangle of a pic, in pixels of that pic
void(vector p, string pic, vector src, vector srcsize, float alpha) wsubpic =
{
	vector isz = drawgetimagesize(pic);
	vector st, sz;
	if (isz_x <= 0 || isz_y <= 0)
		return;	// pic missing
	st_x = src_x / isz_x;	st_y = src_y / isz_y;
	sz_x = srcsize_x / isz_x;	sz_y = srcsize_y / isz_y;
	drawsubpic(wpos + p * wscale, srcsize * wscale, pic, st, sz, '1 1 1', alpha, 0);
};

void(vector p, float c) wchar =
{
	drawcharacter(wpos + p * wscale, c, '8 8' * wscale, '1 1 1', 1, 0);
};

void(vector p, vector size, vector rgb) wfill =
{
	drawfill(wpos + p * wscale, size * wscale, rgb, 1, 0);
};

// Sbar_DrawNum: right-aligned 24px digits, red ("anum_") when flagged
void(vector p, float num, float digits, float red) wnum =
{
	string s, c, prefix;
	float l, i;

	num = min(999, floor(num));
	s = ftos(num);
	l = strlen(s);
	if (l > digits)
	{
		s = substring(s, l - digits, digits);
		l = digits;
	}
	else
		p_x += (digits - l) * 24;
	prefix = red ? "gfx/anum_" : "gfx/num_";
	for (i = 0; i < l; i++)
	{
		c = substring(s, i, 1);
		if (c == "-")
			c = "minus";
		wpic(p, strcat(prefix, c), 1);
		p_x += 24;
	}
};

// Sbar_BatchAmmoCount: small gold digits (conchars 18-27)
void(vector p, float count) wammocount =
{
	count = bound(0, floor(count), 999);
	if (count >= 100)
		wchar(p, 18 + floor(count / 100));
	if (count >= 10)
		wchar(p + '8 0', 18 + floor(count / 10) % 10);
	wchar(p + '16 0', 18 + count % 10);
};

string(float i) weapon_name =
{
	if (i == 0) return "shotgun";
	if (i == 1) return "sshotgun";
	if (i == 2) return "nailgun";
	if (i == 3) return "snailgun";
	if (i == 4) return "rlaunch";
	if (i == 5) return "srlaunch";
	return "lightng";
};

// sb_weapons[flashon][i]: 0 owned, 1 active, 2-6 pickup flash
string(float i) weapon_pic =
{
	float flashon, b = bit(i);
	flashon = floor((cltime - item_gettime[i]) * 10);
	if (flashon < 0)
	{
		item_gettime[i] = cltime;
		flashon = 0;
	}
	if (flashon >= 10)
		flashon = (getstatf(STAT_ACTIVEWEAPON) == b) ? 1 : 0;
	else
		flashon = (flashon % 5) + 2;
	if (flashon == 0)
		return strcat("gfx/inv_", weapon_name(i));
	if (flashon == 1)
		return strcat("gfx/inv2_", weapon_name(i));
	return strcat("gfx/inva", ftos(flashon - 1), "_", weapon_name(i));
};

string() ammo_icon =
{
	if (hud_items & IT_SHELLS)	return "gfx/sb_shells";
	if (hud_items & IT_NAILS)	return "gfx/sb_nails";
	if (hud_items & IT_ROCKETS)	return "gfx/sb_rocket";
	if (hud_items & IT_CELLS)	return "gfx/sb_cells";
	return "";
};

string() armor_icon =
{
	if (hud_items & IT_ARMOR3)	return "gfx/sb_armor3";
	if (hud_items & IT_ARMOR2)	return "gfx/sb_armor2";
	if (hud_items & IT_ARMOR1)	return "gfx/sb_armor1";
	return "";
};

string() face_pic =
{
	float f, health = getstatf(STAT_HEALTH);
	if (health > 0)
	{
		if ((hud_items & (IT_INVISIBILITY | IT_INVULNERABILITY)) == (IT_INVISIBILITY | IT_INVULNERABILITY))
			return "gfx/face_inv2";
		if (hud_items & IT_QUAD)
			return "gfx/face_quad";
		if (hud_items & IT_INVISIBILITY)
			return "gfx/face_invis";
		if (hud_items & IT_INVULNERABILITY)
			return "gfx/face_invul2";
	}
	f = bound(0, floor(health / 20), 4);
	if (cltime <= faceanimtime)
		return strcat("gfx/face_p", ftos(5 - f));
	return strcat("gfx/face", ftos(5 - f));
};

// ---- widgets ----
void() draw_status =
{
	float armor, health, ammo;
	string pic;

	wpic('0 0', "gfx/sbar", sbaralpha);
	armor = getstatf(STAT_ARMOR);
	health = getstatf(STAT_HEALTH);
	ammo = getstatf(STAT_AMMO);

	if (hud_items & IT_INVULNERABILITY)
	{
		wnum('24 0', 666, 3, 1);
		wpic('0 0', "gfx/disc", 1);
	}
	else
	{
		wnum('24 0', armor, 3, armor <= 25);
		pic = armor_icon();
		if (pic != "")
			wpic('0 0', pic, 1);
	}
	wpic('112 0', face_pic(), 1);
	wnum('136 0', health, 3, health <= 25);
	pic = ammo_icon();
	if (pic != "")
		wpic('224 0', pic, 1);
	wnum('248 0', ammo, 3, ammo <= 10);
};

// single-player Tab overlay on the status row (scorebar + level stats)
void() draw_soloscores =
{
	string s;
	if (!widget_begin(W_STATUS))
	{	// the re-release preset has no status row; borrow its slot
		wscale = 1;
		wpos_x = (scr_x - 320) / 2;
		wpos_y = scr_y - 24;
	}
	wpic('0 0', "gfx/scorebar", sbaralpha);
	s = sprintf("Kills: %d/%d", getstatf(STAT_KILLEDMONSTERS), getstatf(STAT_TOTALMONSTERS));
	drawstring(wpos + '8 12' * wscale, s, '8 8' * wscale, '1 1 1', 1, 0);
	s = sprintf("Secrets: %d/%d", getstatf(STAT_FOUNDSECRETS), getstatf(STAT_TOTALSECRETS));
	drawstring(wpos + [312 - strlen(s) * 8, 12, 0] * wscale, s, '8 8' * wscale, '1 1 1', 1, 0);
};

void() draw_ibar =
{
	float i;
	wpic('0 0', "gfx/ibar", sbaralpha);
	for (i = 0; i < 7; i++)
		if (hud_items & (bit(i)))
			wpic([i * 24, 8, 0], weapon_pic(i), 1);
	for (i = 0; i < 4; i++)
		wammocount([10 + 48 * i, 0, 0], getstatf(STAT_SHELLS + i));
};

string(float i) item_pic =
{
	if (i == 0) return "gfx/sb_key1";
	if (i == 1) return "gfx/sb_key2";
	if (i == 2) return "gfx/sb_invis";
	if (i == 3) return "gfx/sb_invuln";
	if (i == 4) return "gfx/sb_suit";
	return "gfx/sb_quad";
};

void() draw_items =
{
	float i;
	for (i = 0; i < 6; i++)
		if (hud_items & (IT_KEY1 * bit(i)))
			wpic([i * 16, 0, 0], item_pic(i), 1);
	if (!isdemo())	// the engine leaves the sigil slots free for its demo percentage
		for (i = 0; i < 4; i++)
			if (hud_sigils & bit(i))
				wpic([96 + i * 8, 0, 0], strcat("gfx/sb_sigil", ftos(i + 1)), 1);
};

// Quake palette entries (colour << 4) + 8: what the engine fills team slots with
vector team_rgb[14] = {'0.4824 0.4824 0.4824', '0.3255 0.2314 0.1059', '0.3098 0.3098 0.451', '0.2157 0.2157 0.0275', '0.2784 0 0', '0.3725 0.2784 0.0275', '0.5608 0.2627 0.2', '0.498 0.3255 0.2471', '0.3412 0.2157 0.2627', '0.3725 0.2 0.2471', '0.4196 0.3412 0.2784', '0.1843 0.2627 0.2157', '0.4824 0.3882 0.0275', '0.1843 0.1843 0.498'};

// Sbar_DrawFrags. The ProQuake/CRMod clock and team totals come from QSS-M's
// "pq_*" serverkey pseudo-keys; on engines without them those read as empty
// and this falls back to plain player frags.
void() draw_frags =
{
	float i, j, n, x, f, mask, secs, t, best;
	string clock, mode, s;
	float team[4], tfrags[4];

	n = cvar("chud_frags_count");
	clock = serverkey("pq_clock");
	if (clock != "")
	{	// the clock takes the right half of the slot, leaving room for two scores
		n = min(n, 2);
		secs = stof(clock);
		mode = serverkey("pq_clockmode");
		mask = serverkeyfloat("pq_clockwarn") ? 128 : 0;
		if (mode == "suddendeath")
			s = "    SD";
		else if (mode == "prematch")
			s = sprintf(" -0:%02d", secs);
		else
			s = sprintf("%3d:%02d", floor(secs / 60), secs % 60);
		for (i = 0; i < 6; i++)
			wchar([75 + i * 8, 0, 0], str2chr(s, i) + mask);
	}

	if (serverkeyfloat("pq_teamgame"))
	{	// Sbar_SortTeamFrags: teams by score, highest first
		n = min(n, 4);
		j = tokenize(serverkey("pq_teams"));
		for (i = 0; i < 4; i++)
			team[i] = -1;
		for (t = 0; t < j; t++)
		{
			f = serverkeyfloat(strcat("pq_teamfrags", argv(t)));
			for (i = 0; i < 4; i++)
				if (team[i] < 0 || f > tfrags[i])
					break;
			if (i >= 4)
				continue;
			for (best = 3; best > i; best--)
			{
				team[best] = team[best - 1];
				tfrags[best] = tfrags[best - 1];
			}
			team[i] = stof(argv(t));
			tfrags[i] = f;
		}
		for (i = 0, x = 0; i < n && team[i] >= 0; i++, x += 32)
		{
			wfill([x + 10, 1, 0], '28 4', team_rgb[team[i]]);
			wfill([x + 10, 5, 0], '28 3', team_rgb[team[i]]);
			drawstring(wpos + [x + 12, 0, 0] * wscale, sprintf("%3d", tfrags[i]), '8 8' * wscale, '1 1 1', 1, 0);
			if (stof(getplayerkeyvalue(player_localentnum - 1, "bottomcolor")) == team[i])
			{
				wchar([x + 5, 0, 0], 16);
				wchar([x + 33, 0, 0], 17);
			}
		}
		return;
	}

	x = 0;
	for (i = 0; i < n; i++)
	{	// negative player numbers index the frag-sorted list
		if (getplayerkeyvalue(-1 - i, "name") == "")
			break;
		wfill([x + 10, 1, 0], '28 4', stov(getplayerkeyvalue(-1 - i, "topcolor_rgb")));
		wfill([x + 10, 5, 0], '28 3', stov(getplayerkeyvalue(-1 - i, "bottomcolor_rgb")));
		f = stof(getplayerkeyvalue(-1 - i, "frags"));
		drawstring(wpos + [x + 12, 0, 0] * wscale, sprintf("%3d", f), '8 8' * wscale, '1 1 1', 1, 0);
		if (stof(getplayerkeyvalue(-1 - i, "viewentity")) == player_localentnum)
		{
			wchar([x + 6, 0, 0], 16);
			wchar([x + 32, 0, 0], 17);
		}
		x += 32;
	}
};

void() draw_qwammo =
{
	float i;
	// scr_sbaralphaqwammo 1: ibar ammo slots at full alpha, digits at x 9
	for (i = 0; i < 4; i++)
		wsubpic([2, 144 + 11 * i, 0], "gfx/ibar", [3 + 48 * i, 0, 0], '42 11', 1);
	for (i = 0; i < 7; i++)
		if (hud_items & (bit(i)))
			wpic([20, 32 + i * 16, 0], weapon_pic(i), sbaralpha);
	for (i = 0; i < 4; i++)
		wammocount([9, 144 + 11 * i, 0], getstatf(STAT_SHELLS + i));
};

void() draw_qearmor =
{
	float armor = getstatf(STAT_ARMOR);
	string pic;
	if (hud_items & IT_INVULNERABILITY)
		armor = 666;
	if (armor <= 0)
		return;
	wsubpic('0 1', "gfx/sbar", '0 0', '24 24', 1);
	if (hud_items & IT_INVULNERABILITY)
	{
		wnum('32 0', 666, 3, 1);
		wpic('0 1', "gfx/disc", 1);
		return;
	}
	wnum('32 0', armor, 3, armor <= 25);
	pic = armor_icon();
	if (pic != "")
		wpic('0 0', pic, 1);
};

void() draw_qehealth =
{
	float health = getstatf(STAT_HEALTH);
	wpic('0 1', face_pic(), 1);
	wnum('32 0', health, 3, health <= 25);
};

void() draw_qeammo =
{
	float ammo = getstatf(STAT_AMMO);
	string pic;
	if (!cvar_or("scr_sbarshowqeammo", 1))
		return;
	wsubpic('82 0', "gfx/sbar", '0 0', '24 24', 1);
	pic = ammo_icon();
	if (pic != "")
		wpic('82 0', pic, 1);
	wnum('0 0', ammo, 3, ammo <= 10);
};

void() draw_qekeys =
{	// Sbar_DrawInventory_QE, right-to-left from x 288 in a box starting at 224
	float i, x, k1 = hud_items & IT_KEY1, k2 = hud_items & IT_KEY2;
	x = 64;
	if (k1) { wpic([x, 0, 0], "gfx/sb_key1", 1); x -= 16; }
	if (k2) { wpic([x, 0, 0], "gfx/sb_key2", 1); x -= 16; }
	if (!k1 && !k2)
		x = 72;
	else if (k1 && k2)
		x = 32;
	else
		x = 48;
	for (i = 0; i < 4; i++)
		if (hud_sigils & bit(i))
		{
			wpic([x, 0, 0], strcat("gfx/sb_sigil", ftos(i + 1)), 1);
			x -= 8;
		}
};

// SCR_DrawSpeed text mode: white, red over 400, gold digits over 600
void() draw_speed =
{
	string v = serverkey("qssm_speed");
	float speed, i, c;
	if (v == "")
		return;
	speed = stof(v);
	for (i = 0; i < strlen(v); i++)
	{
		c = str2chr(v, i);
		if (speed > 600)
			c = c - 30;		// conchars 18-27: gold digits
		else if (speed > 400)
			c = c + 128;	// red
		wchar([i * 8, 0, 0], c);
	}
};

// SCR_DrawMovementKeys. The engine rotates '^'; drawcharacter can't, so arrows are ^ v < >.
void() draw_movekeys =
{
	float k = serverkeyfloat("qssm_movekeys", -1);
	if (k < 0)
		return;
	if (k & 1)	wchar('8 0', '^');
	if (k & 2)	wchar('8 16', 'v');
	if (k & 4)	wchar('0 8', '<');
	if (k & 8)	wchar('16 8', '>');
	if (k & 16)
		wchar('8 7', 'j' + 128);
	else if (k & 32)
		wchar('8 7', 's' + 128);
};

void() draw_loc =
{
	if (loc_name == "")
		return;
	drawstring(wpos, loc_name, '8 8' * wscale, '1 1 1', 1, 0);
};

// ---- bookkeeping ----
void() track_state =
{
	float i, b, health;

	hud_items = getstatbits(STAT_ITEMS, 0, 23);
	hud_sigils = getstatbits(STAT_ITEMS, 28, 4);
	if (hud_items != lastitems)
	{	// CL_ParseClientdata: a newly set bit restarts that item's pickup flash
		for (i = 0; i < 23; i++)
		{
			b = bit(i);
			if ((hud_items & b) && !(lastitems & b))
				item_gettime[i] = cltime;
		}
		lastitems = hud_items;
	}
	// The engine animates the face on svc_damage, which simple CSQC can't see;
	// a health drop is the portable stand-in.
	health = getstatf(STAT_HEALTH);
	if (health < lasthealth)
		faceanimtime = cltime + 0.2;
	lasthealth = health;
};

void() chud_precache =
{
	float i, j;
	string pics = "sbar ibar scorebar disc sb_shells sb_nails sb_rocket sb_cells sb_armor1 sb_armor2 sb_armor3 sb_key1 sb_key2 sb_invis sb_invuln sb_suit sb_quad sb_sigil1 sb_sigil2 sb_sigil3 sb_sigil4 face_inv2 face_quad face_invis face_invul2 num_minus anum_minus";
	float n = tokenize(pics);
	for (i = 0; i < n; i++)
		precache_pic(strcat("gfx/", argv(i)));
	for (i = 0; i < 10; i++)
	{
		precache_pic(strcat("gfx/num_", ftos(i)));
		precache_pic(strcat("gfx/anum_", ftos(i)));
	}
	for (i = 1; i <= 5; i++)
	{
		precache_pic(strcat("gfx/face", ftos(i)));
		precache_pic(strcat("gfx/face_p", ftos(i)));
	}
	for (i = 0; i < 7; i++)
	{
		precache_pic(strcat("gfx/inv_", weapon_name(i)));
		precache_pic(strcat("gfx/inv2_", weapon_name(i)));
		for (j = 1; j <= 5; j++)
			precache_pic(strcat("gfx/inva", ftos(j), "_", weapon_name(i)));
	}
};

void(vector virtsize, float showscores) chud_draw =
{
	scr = virtsize;
	preset = cvar("chud_preset");
	if (preset < 1 || preset > 3)
		preset = bound(1, floor(cvar_or("scr_sbar", 1)), 3);
	sbaralpha = cvar_or("scr_sbaralpha", 1);
	hud_viewsize = cvar("viewsize");

	track_state();
	if (intermission)
		return;	// the engine never draws the status bar during intermission

	if (widget_begin(W_IBAR))
		draw_ibar();
	if (widget_begin(W_QWAMMO))
		draw_qwammo();
	if (widget_begin(W_ITEMS))
		draw_items();
	if (widget_begin(W_QEKEYS))
		draw_qekeys();
	if (maxclients > 1 && widget_begin(W_FRAGS))
		draw_frags();
	if (widget_begin(W_SPEED))
		draw_speed();
	if (widget_begin(W_MOVEKEYS))
		draw_movekeys();
	if (widget_begin(W_LOC))
		draw_loc();
	if (showscores && maxclients <= 1)
		draw_soloscores();
	else
	{
		if (widget_begin(W_STATUS))
			draw_status();
		if (widget_begin(W_QEARMOR))
			draw_qearmor();
		if (widget_begin(W_QEHEALTH))
			draw_qehealth();
		if (widget_begin(W_QEAMMO))
			draw_qeammo();
	}
};
