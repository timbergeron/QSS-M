/*
	csqchud -- an id1 sample simple-CSQC HUD that recreates QSS-M's scr_sbar
	1/2/3 status bars and lets every widget be moved, scaled or hidden.

	Tested on QSS-M and FTEQW. Only CSQC_DrawHud is defined, so it works as
	"simple CSQC" (a local csprogs.dat the server didn't send). Standard CSQC
	paths come first (STAT_TIME / STAT_MATCHSTARTTIME + timelimit for timed
	matches, playerkey "team" summing gated on teamplay for team scores);
	optional QSS-M extensions (pq_* serverkeys and speed/location playerkeys)
	read as empty on other engines and degrade gracefully.

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
const float IT_GRENADE_LAUNCHER = 16;

// Hipnotic: hipweapons[] bit numbers (laser cannon, mjolnir, grenade launcher, proximity gun)
const float HIT_PROXIMITY_GUN_BIT = 16;
const float HIT_GRENADE_LAUNCHER_BIT = 4;
// Rogue weapon/ammo bits below bit 23 (the rest are read through has_bit())
const float RIT_SHELLS = 128;
const float RIT_NAILS = 256;
const float RIT_ROCKETS = 512;
const float RIT_CELLS = 1024;
const float RIT_LAVA_NAILGUN = 4096;
// Rogue bit numbers at 23 and above
const float RIT_ARMOR1_BIT = 23;
const float RIT_ARMOR2_BIT = 24;
const float RIT_ARMOR3_BIT = 25;
const float RIT_LAVA_NAILS_BIT = 26;
const float RIT_PLASMA_AMMO_BIT = 27;
const float RIT_MULTI_ROCKETS_BIT = 28;

// Standard FTE/ZQuake timer stats (named with a prefix so we compile cleanly
// whether or not the dumped engine header defines STAT_TIME/STAT_MATCHSTARTTIME).
const float CHUD_STAT_TIME = 17;
const float CHUD_STAT_MATCHSTARTTIME = 18;

// ---- engine cvars as autocvars (created with defaults on engines without them) ----
var float autocvar_scr_sbar = 1;
var float autocvar_scr_sbaralpha = 1;
var float autocvar_scr_sbarscale = 1;
var float autocvar_scr_sbarshowqeammo = 1;
var float autocvar_scr_showspeed = 0;
var float autocvar_r_showlocs = 0;
var float autocvar_r_showlocs_y = 30;
var float autocvar_viewsize = 100;

var float autocvar_chud_preset = 0;
var float autocvar_chud_frags_count = 2;
var float autocvar_scr_showscores = 0;

// scores.qc
float() scores_list_rows;
void() draw_scores_list;

// ---- widgets ----
const float W_STATUS = 0;	// sbar row: armor, face, health, ammo (presets 1, 2)
const float W_IBAR = 1;		// classic inventory strip: weapons + ammo counts (preset 1)
const float W_ITEMS = 2;	// keys, powerups, sigils row (presets 1, 2)
const float W_FRAGS = 3;	// frag counts + match clock / team totals (preset 1, multiplayer)
const float W_QWAMMO = 4;	// side column: weapons + ammo counts (presets 2, 3)
const float W_QEARMOR = 5;	// re-release armor block (preset 3)
const float W_QEHEALTH = 6;	// re-release face + health (preset 3)
const float W_QEAMMO = 7;	// re-release current ammo (preset 3)
const float W_QEKEYS = 8;	// re-release keys + sigils (preset 3)
const float W_SPEED = 9;	// speed readout (follows scr_showspeed)
const float W_LOC = 10;		// location name (follows r_showlocs)
const float W_SCORES = 11;	// always-on scores list (follows scr_showscores), scores.qc
const float W_COUNT = 12;

string widget_name[W_COUNT] = {"status", "ibar", "items", "frags", "qwammo", "qe_armor", "qe_health", "qe_ammo", "qe_keys", "speed", "loc", "scores"};
vector widget_size[W_COUNT] = {'320 24', '320 24', '128 16', '128 8', '44 188', '104 25', '104 25', '106 24', '80 16', '32 8', '64 8', '140 128'};

// resolved per frame
vector loc_size;	// the loc widget is as wide as the current name
string loc_name;	// looked up only while the loc widget is shown
vector scr;		// virtual screen size (CSQC_DrawHud's argument)
float preset;
float sbaralpha;
float hud_items, hud_sigils, hud_viewsize;
vector wpos;	// current widget's top-left on screen
float wscale;	// current widget's scale
float hudtype;		// 0 id1, 1 Hipnotic, 2 Rogue: detected from gfx.wad like Sbar_LoadPics
float hud_items2;	// STAT_ITEMS bits 23-31 (Rogue armour/ammo/powerups, Hipnotic laser and suits)

// pickup flash bookkeeping, mirrors cl.item_gettime
float item_gettime[32];
float lastitems, lastitems2;
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

// any of the 32 STAT_ITEMS bits (getstatbits can only hand back 23 at a time)
float(float n) has_bit =
{
	if (n < 23)
		return (hud_items & bit(n)) != 0;
	return (hud_items2 & bit(n - 23)) != 0;
};

float(string s) is_blank = { return s == "" || s == "-"; };

void() chud_registercvars =
{
	float i;
	registercvar("chud_autotest", "0");
	for (i = 0; i < W_COUNT; i++)
	{
		registercvar(sprintf("chud_%s_show", widget_name[i]), "");
		registercvar(sprintf("chud_%s_anchor", widget_name[i]), "");
		registercvar(sprintf("chud_%s_ofs", widget_name[i]), "");
		registercvar(sprintf("chud_%s_scale", widget_name[i]), "");
	}
};

// ---- preset defaults ----
// Anchors/offsets below reproduce the engine's GL canvases exactly at
// scr_sbarscale 1 (CANVAS_SBAR, CANVAS_IBAR_QW, CANVAS_BOTTOMLEFTQE, ...).

float(float w) preset_show =
{	// the overlay widgets follow the engine's own toggles, so a HUD shows what the player enabled
	if (w == W_SPEED)
		return autocvar_scr_showspeed >= 1;
	if (w == W_LOC)
		return autocvar_r_showlocs != 0;
	if (w == W_SCORES)
		return autocvar_scr_showscores != 0;
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
	if (w == W_SCORES)
		return '0 1';
	if (w == W_SPEED && preset == 3)
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
	if (w == W_LOC)
		return [0, autocvar_r_showlocs_y + 4, 0];	// below the crosshair
	if (w == W_SCORES)
		return '10 -132';	// SCR_ShowObsFrags: bottom-left, growing upwards
	if (w == W_QEARMOR)	return '18 -36';
	if (w == W_QEHEALTH)	return '18 -12';
	if (w == W_QEAMMO)	return '-16 -12';
	if (w == W_QEKEYS)
	{
		if (autocvar_scr_sbarshowqeammo)
			return '-16 -40';
		return '-16 -64';
	}
	return '0 0';
};

float(float w) preset_scale =
{
	if (w == W_QWAMMO && preset == 3 && autocvar_scr_sbarscale > 1)
	{	// CANVAS_IBAR_QWQE runs at max(1, scr_sbarscale/1.4) while CANVAS_CSQC runs at scr_sbarscale
		return max(1, autocvar_scr_sbarscale / 1.4) / autocvar_scr_sbarscale;
	}
	return 1;
};

// engine viewsize gates: inventory < 110, classic status < 120, re-release vitals <= 110
float(float w) widget_viewsize_ok =
{
	if (w == W_SPEED)
		return hud_viewsize <= 110;
	if (w == W_LOC)
		return TRUE;
	if (w == W_SCORES)
		return hud_viewsize < 120;
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

	s = cvar_string(sprintf("chud_%s_show", widget_name[w]));
	if (is_blank(s))
		show = preset_show(w) && widget_viewsize_ok(w);
	else
		show = stof(s);
	if (!show)
		return FALSE;
	if (w == W_LOC)
	{	// centred on its own width
		loc_name = getplayerkeyvalue(player_localentnum - 1, "location");
		loc_size = [strlen(loc_name) * 8, 8, 0];
	}

	s = cvar_string(sprintf("chud_%s_anchor", widget_name[w]));
	anchor = is_blank(s) ? preset_anchor(w) : stov(s);
	s = cvar_string(sprintf("chud_%s_ofs", widget_name[w]));
	ofs = is_blank(s) ? preset_ofs(w) : stov(s);
	s = cvar_string(sprintf("chud_%s_scale", widget_name[w]));
	wscale = is_blank(s) ? preset_scale(w) : stof(s);
	if (wscale <= 0)
		return FALSE;

	if (w == W_LOC)
		size = loc_size * wscale;
	else if (w == W_SCORES)	// one 8px row per player, so a bottom anchor grows upwards
		size = [140, 8 * scores_list_rows(), 0] * wscale;
	else
		size = widget_size[w] * wscale;
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
	s = sprintf("%d", num);
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
		wpic(p, sprintf("%s%s", prefix, c), 1);
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
// sb_weapons[flashon][i]: 0 owned, 1 active, 2-6 pickup flash. n is the
// item bit, activeval the STAT_ACTIVEWEAPON value that marks it in use.
float(float n, float activeval) flash_state =
{
	float flashon = floor((cltime - item_gettime[n]) * 10);
	if (flashon < 0)
	{
		item_gettime[n] = cltime;
		flashon = 0;
	}
	if (flashon >= 10)
		return (getstatf(STAT_ACTIVEWEAPON) == activeval) ? 1 : 0;
	return (flashon % 5) + 2;
};

string(float flashon, string name) flash_pic =
{
	if (flashon == 0)
		return sprintf("gfx/inv_%s", name);
	if (flashon == 1)
		return sprintf("gfx/inv2_%s", name);
	return sprintf("gfx/inva%d_%s", flashon - 1, name);
};

string(float i) weapon_pic =
{
	return flash_pic(flash_state(i, bit(i)), weapon_name(i));
};

// hsb_weapons[][]: laser, mjolnir, grenade+prox flash, prox+grenade, prox
string(float j) hip_weapon_name =
{
	if (j == 0) return "laser";
	if (j == 1) return "mjolnir";
	if (j == 2) return "gren_prox";
	if (j == 3) return "prox_gren";
	return "prox";
};

float(float i) hip_weapon_bit =
{
	if (i == 0) return 23;	// laser cannon
	if (i == 1) return 7;	// mjolnir
	if (i == 2) return 4;	// grenade launcher
	return 16;				// proximity gun
};

// Sbar_DrawInventory / _QW hipnotic block, including the grenade/proximity overlap
void(float qw) draw_hip_weapons =
{
	float i, n, f, grenadeflashing = 0, alpha = qw ? sbaralpha : 1;
	vector slot96 = qw ? '20 64 0' : '96 8 0';
	for (i = 0; i < 4; i++)
	{
		n = hip_weapon_bit(i);
		if (!has_bit(n))
			continue;
		f = flash_state(n, bit(n));
		if (i == 2)
		{
			if (has_bit(HIT_PROXIMITY_GUN_BIT) && f)
			{
				grenadeflashing = 1;
				wpic(slot96, flash_pic(f, hip_weapon_name(2)), alpha);
			}
		}
		else if (i == 3)
		{
			if (has_bit(HIT_GRENADE_LAUNCHER_BIT))
			{
				if (f && !grenadeflashing)
					wpic(slot96, flash_pic(f, hip_weapon_name(3)), alpha);
				else if (!grenadeflashing)
					wpic(slot96, flash_pic(0, hip_weapon_name(3)), alpha);
			}
			else
				wpic(slot96, flash_pic(f, hip_weapon_name(4)), alpha);
		}
		else if (qw)
			wpic([20, (i + 7) * 16, 0], flash_pic(f, hip_weapon_name(i)), alpha);
		else
			wpic([176 + i * 24, 8, 0], flash_pic(f, hip_weapon_name(i)), alpha);
	}
};

string(float i) rogue_weapon_pic =
{
	if (i == 0) return "gfx/r_lava";
	if (i == 1) return "gfx/r_superlava";
	if (i == 2) return "gfx/r_gren";
	if (i == 3) return "gfx/r_multirock";
	return "gfx/r_plasma";
};

// Rogue's powered-up weapon replaces the matching id weapon icon
void(float qw) draw_rogue_powered =
{
	float i, aw = getstatf(STAT_ACTIVEWEAPON);
	if (aw < RIT_LAVA_NAILGUN)
		return;
	for (i = 0; i < 5; i++)
		if (aw == RIT_LAVA_NAILGUN * bit(i))
		{
			if (qw)
				wpic([20, (i + 2) * 16 + 32, 0], rogue_weapon_pic(i), sbaralpha);
			else
				wpic([(i + 2) * 24, 8, 0], rogue_weapon_pic(i), 1);
		}
};

// rsb_invbar[0] while a Rogue powered weapon is active, else [1]
string() rogue_invbar =
{
	return (getstatf(STAT_ACTIVEWEAPON) >= RIT_LAVA_NAILGUN) ? "gfx/r_invbar1" : "gfx/r_invbar2";
};

string() ammo_icon =
{
	if (hudtype == 2)
	{	// rsb_ammo[] is loaded lava, multi, plasma but indexed lava, plasma, multi; mirrored as-is
		if (hud_items & RIT_SHELLS)	return "gfx/sb_shells";
		if (hud_items & RIT_NAILS)	return "gfx/sb_nails";
		if (hud_items & RIT_ROCKETS)	return "gfx/sb_rocket";
		if (hud_items & RIT_CELLS)	return "gfx/sb_cells";
		if (has_bit(RIT_LAVA_NAILS_BIT))	return "gfx/r_ammolava";
		if (has_bit(RIT_PLASMA_AMMO_BIT))	return "gfx/r_ammomulti";
		if (has_bit(RIT_MULTI_ROCKETS_BIT))	return "gfx/r_ammoplasma";
		return "";
	}
	if (hud_items & IT_SHELLS)	return "gfx/sb_shells";
	if (hud_items & IT_NAILS)	return "gfx/sb_nails";
	if (hud_items & IT_ROCKETS)	return "gfx/sb_rocket";
	if (hud_items & IT_CELLS)	return "gfx/sb_cells";
	return "";
};

string() armor_icon =
{
	if (hudtype == 2)
	{
		if (has_bit(RIT_ARMOR3_BIT))	return "gfx/sb_armor3";
		if (has_bit(RIT_ARMOR2_BIT))	return "gfx/sb_armor2";
		if (has_bit(RIT_ARMOR1_BIT))	return "gfx/sb_armor1";
		return "";
	}
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
		return sprintf("gfx/face_p%d", 5 - f);
	return sprintf("gfx/face%d", 5 - f);
};

// ---- widgets ----
void() draw_status =
{
	float armor, health, ammo;
	string pic;

	wpic('0 0', "gfx/sbar", sbaralpha);
	if (hudtype == 1)
	{	// Hipnotic moved the keys onto the status row so its items fit
		if (hud_items & IT_KEY1)
			wpic('209 3', "gfx/sb_key1", 1);
		if (hud_items & IT_KEY2)
			wpic('209 12', "gfx/sb_key2", 1);
	}
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
	wpic('0 0', (hudtype == 2) ? rogue_invbar() : "gfx/ibar", sbaralpha);
	for (i = 0; i < 7; i++)
		if (hud_items & bit(i))
			wpic([i * 24, 8, 0], weapon_pic(i), 1);
	if (hudtype == 1)
		draw_hip_weapons(FALSE);
	else if (hudtype == 2)
		draw_rogue_powered(FALSE);
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
	for (i = 0; i < 6; i++)	// Hipnotic draws its keys on the status row instead
		if ((hud_items & (IT_KEY1 * bit(i))) && (hudtype != 1 || i > 1))
			wpic([i * 16, 0, 0], item_pic(i), 1);
	if (hudtype == 1)
	{	// wetsuit, empathy shields
		if (has_bit(24))	wpic('96 0', "gfx/sb_wsuit", 1);
		if (has_bit(25))	wpic('112 0', "gfx/sb_eshld", 1);
	}
	else if (hudtype == 2)
	{	// shield, antigrav (replace the sigils)
		if (has_bit(29))	wpic('96 0', "gfx/r_shield1", 1);
		if (has_bit(30))	wpic('112 0', "gfx/r_agrav1", 1);
		return;
	}
	if (!isdemo())	// the engine leaves the sigil slots free for its demo percentage
		for (i = 0; i < 4; i++)
			if (hud_sigils & bit(i))
				wpic([96 + i * 8, 0, 0], sprintf("gfx/sb_sigil%d", i + 1), 1);
};

// Quake palette entries (colour << 4) + 8: what the engine fills ProQuake team slots with
vector team_rgb[14] = {'0.4824 0.4824 0.4824', '0.3255 0.2314 0.1059', '0.3098 0.3098 0.451', '0.2157 0.2157 0.0275', '0.2784 0 0', '0.3725 0.2784 0.0275', '0.5608 0.2627 0.2', '0.498 0.3255 0.2471', '0.3412 0.2157 0.2627', '0.3725 0.2 0.2471', '0.4196 0.3412 0.2784', '0.1843 0.2627 0.2157', '0.4824 0.3882 0.0275', '0.1843 0.1843 0.498'};

// Sbar_DrawFrags.
// Clock: standard FTE STAT_TIME / STAT_MATCHSTARTTIME + timelimit first, with
// QSS-M's pq_clock* fallback for prematch, sudden death, pause, count-up,
// overtime or older ProQuake servers without timelimit.
// Teams: QSS-M's pq_teams/pq_teamfragsN when the server sends ProQuake team
// scores (including CRMod arena round wins); otherwise sum frags grouped by
// the "team" playerkey when teamplay > 0 (works with QW team names on FTE).
float clock_last_st, clock_last_change;	// detects a server time that never advances

void() draw_frags =
{
	float i, j, n, x, f, mask, secs, t, best, tl, has_clock;
	string clock, mode, s, pq_teams;
	float team[4], tfrags[4];

	n = autocvar_chud_frags_count;

	mode = serverkey("pq_clockmode");
	tl = serverkeyfloat("timelimit");
	has_clock = FALSE;
	if (tl > 0 && !(mode == "suddendeath" || mode == "prematch" || mode == "up" || serverkeyfloat("pq_matchpaused")))
	{	// FTE convention: timelimit - (server time - match start). Needs a real
		// match start and a server time that advances in this entry point (FTE
		// leaves servertime and both stats at 0 inside CSQC_DrawHud); without
		// them show the legacy clock or nothing, never a frozen countdown.
		float st = getstatf(CHUD_STAT_TIME) / 1000;
		if (st <= 0)
			st = servertime;
		if (st != clock_last_st)
		{
			clock_last_st = st;
			clock_last_change = cltime;
		}
		float mstart = getstatf(CHUD_STAT_MATCHSTARTTIME) / 1000;
		secs = ceil(tl * 60 - (st - mstart));
		if (mstart > 0 && st > 0 && st >= mstart && secs >= 0 &&
			cltime - clock_last_change < 1.5)	// a stalled server time is no clock either
		{
			mask = (secs < 60) ? 128 : 0;
			s = sprintf("%3d:%02d", floor(secs / 60), secs % 60);
			has_clock = TRUE;
		}
	}
	if (!has_clock)
	{
		clock = serverkey("pq_clock");
		if (clock != "")
		{
			secs = stof(clock);
			mask = serverkeyfloat("pq_clockwarn") ? 128 : 0;
			if (mode == "suddendeath")
				s = "    SD";
			else if (mode == "prematch")
				s = sprintf(" -0:%02d", secs);
			else
				s = sprintf("%3d:%02d", floor(secs / 60), secs % 60);
			has_clock = TRUE;
		}
	}
	if (has_clock)
	{	// the clock takes the right half of the slot, leaving room for two scores
		n = min(n, 2);
		for (i = 0; i < 6; i++)
			wchar([75 + i * 8, 0, 0], str2chr(s, i) + mask);
	}

	pq_teams = serverkey("pq_teams");
	if (pq_teams != "")
	{	// ProQuake/CRMod server-sent team scores (includes arena round wins)
		string mybot = getplayerkeyvalue(player_localentnum - 1, "bottomcolor");
		n = min(n, 4);
		j = tokenize(pq_teams);
		for (i = 0; i < 4; i++)
			team[i] = -1;
		for (t = 0; t < j; t++)
		{
			f = serverkeyfloat(sprintf("pq_teamfrags%s", argv(t)));
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
			// String compare: legacy colours are "0"-"13", RGB is "0x...", so RGB never false-matches.
			if (mybot == sprintf("%d", team[i]))
			{
				wchar([x + 5, 0, 0], 16);
				wchar([x + 33, 0, 0], 17);
			}
		}
		return;
	}

	if (serverkeyfloat("teamplay") > 0)
	{	// Standard QW/FTE fallback: sum connected players' frags by string "team" key
		string tnames[8];
		float tsum[8], order[8];
		vector trgb[8];
		float nteams = 0, k;
		string localteam = strzone(getplayerkeyvalue(player_localentnum - 1, "team"));

		for (i = 0; i < maxclients; i++)
		{
			if (getplayerkeyvalue(i, "name") == "")
				continue;
			if (stof(getplayerkeyvalue(i, "*spectator")) != 0)
				continue;
			s = getplayerkeyvalue(i, "team");
			if (s == "")
				continue;
			for (k = 0; k < nteams; k++)
				if (tnames[k] == s)
					break;
			if (k == nteams)
			{
				if (nteams >= 8)
					continue;
				tnames[k] = strzone(s);
				tsum[k] = 0;
				trgb[k] = stov(getplayerkeyvalue(i, "bottomcolor_rgb"));
				order[k] = k;
				nteams++;
			}
			tsum[k] += stof(getplayerkeyvalue(i, "frags"));
		}

		// Insertion-sort team indices by summed frags, highest first
		for (i = 1; i < nteams; i++)
		{
			k = order[i];
			j = i;
			while (j > 0)	// no "&&" guard: fteqcc doesn't short-circuit, [j - 1] would read -1
			{
				if (tsum[k] <= tsum[order[j - 1]])
					break;
				order[j] = order[j - 1];
				j--;
			}
			order[j] = k;
		}

		n = min(n, nteams);
		for (i = 0, x = 0; i < n; i++, x += 32)
		{
			k = order[i];
			wfill([x + 10, 1, 0], '28 4', trgb[k]);
			wfill([x + 10, 5, 0], '28 3', trgb[k]);
			drawstring(wpos + [x + 12, 0, 0] * wscale, sprintf("%3d", tsum[k]), '8 8' * wscale, '1 1 1', 1, 0);
			if (localteam != "" && tnames[k] == localteam)
			{
				wchar([x + 5, 0, 0], 16);
				wchar([x + 33, 0, 0], 17);
			}
		}

		for (i = 0; i < nteams; i++)
			strunzone(tnames[i]);
		strunzone(localteam);
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
	float i, extraguns = (hudtype == 1) ? 2 : 0;
	if (hudtype == 2)
	{	// Rogue: the active inventory bar's ammo slots at scr_sbaralpha
		for (i = 0; i < 4; i++)
			wsubpic([0, 144 + 11 * i, 0], rogue_invbar(), [1 + 48 * i, 0, 0], '44 11', sbaralpha);
	}
	else	// scr_sbaralphaqwammo 1: ibar ammo slots at full alpha
		for (i = 0; i < 4; i++)
			wsubpic([2, 144 + 11 * i, 0], "gfx/ibar", [3 + 48 * i, 0, 0], '42 11', 1);
	for (i = 0; i < 7; i++)
		if (hud_items & bit(i))
			wpic([20, 32 + i * 16 - 16 * extraguns, 0], weapon_pic(i), sbaralpha);
	if (hudtype == 1)
		draw_hip_weapons(TRUE);
	else if (hudtype == 2)
		draw_rogue_powered(TRUE);
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
	if (!autocvar_scr_sbarshowqeammo)
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
			wpic([x, 0, 0], sprintf("gfx/sb_sigil%d", i + 1), 1);
			x -= 8;
		}
};

// SCR_DrawSpeed text mode: white, red over 400, gold digits over 600
void() draw_speed =
{
	string v = getplayerkeyvalue(player_localentnum - 1, "speed");
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
	hud_items2 = getstatbits(STAT_ITEMS, 23, 9);
	hud_sigils = getstatbits(STAT_ITEMS, 28, 4);
	if (hud_items != lastitems || hud_items2 != lastitems2)
	{	// CL_ParseClientdata: a newly set bit restarts that item's pickup flash
		for (i = 0; i < 23; i++)
		{
			b = bit(i);
			if ((hud_items & b) && !(lastitems & b))
				item_gettime[i] = cltime;
		}
		for (i = 0; i < 9; i++)
		{
			b = bit(i);
			if ((hud_items2 & b) && !(lastitems2 & b))
				item_gettime[23 + i] = cltime;
		}
		lastitems = hud_items;
		lastitems2 = hud_items2;
	}
	// The engine animates the face on svc_damage, which simple CSQC can't see;
	// a health drop is the portable stand-in.
	health = getstatf(STAT_HEALTH);
	if (health < lasthealth)
		faceanimtime = cltime + 0.2;
	lasthealth = health;
};

// Every lump Sbar_LoadPics requires before it switches to that pack's HUD.
const string ROGUE_LUMPS = "r_invbar1 r_invbar2 r_lava r_superlava r_gren r_multirock r_plasma r_shield1 r_agrav1 r_teambord r_ammolava r_ammomulti r_ammoplasma";

string() hip_lumps =
{
	string l = "sb_wsuit sb_eshld";
	float i, f;
	for (i = 0; i < 5; i++)
	{
		l = sprintf("%s inv_%s inv2_%s", l, hip_weapon_name(i), hip_weapon_name(i));
		for (f = 1; f <= 5; f++)
			l = sprintf("%s inva%d_%s", l, f, hip_weapon_name(i));
	}
	return l;
};

float(string lumps) wad_has_all =
{
	float i, n = tokenize(lumps);
	for (i = 0; i < n; i++)
		if (precache_pic(argv(i), PRECACHE_PIC_FROMWAD | PRECACHE_PIC_TEST) == "")
			return FALSE;
	return TRUE;
};

void() chud_precache =
{
	float i, j;
	string pics = "sbar ibar scorebar disc sb_shells sb_nails sb_rocket sb_cells sb_armor1 sb_armor2 sb_armor3 sb_key1 sb_key2 sb_invis sb_invuln sb_suit sb_quad sb_sigil1 sb_sigil2 sb_sigil3 sb_sigil4 face_inv2 face_quad face_invis face_invul2 num_minus anum_minus";
	float n = tokenize(pics);
	for (i = 0; i < n; i++)
		precache_pic(sprintf("gfx/%s", argv(i)));
	for (i = 0; i < 10; i++)
	{
		precache_pic(sprintf("gfx/num_%d", i));
		precache_pic(sprintf("gfx/anum_%d", i));
	}
	for (i = 1; i <= 5; i++)
	{
		precache_pic(sprintf("gfx/face%d", i));
		precache_pic(sprintf("gfx/face_p%d", i));
	}
	for (i = 0; i < 7; i++)
	{
		precache_pic(sprintf("gfx/inv_%s", weapon_name(i)));
		precache_pic(sprintf("gfx/inv2_%s", weapon_name(i)));
		for (j = 1; j <= 5; j++)
			precache_pic(sprintf("gfx/inva%d_%s", j, weapon_name(i)));
	}

	// Sbar_LoadPics picks the HUD only when gfx.wad holds the pack's whole lump
	// set; do the same. Bare lump names with PRECACHE_PIC_FROMWAD look in the
	// wad only (a loose gfx/inv_laser.tga must not count) and are cached apart
	// from the gfx/ names used for drawing.
	hudtype = 0;
	if (wad_has_all(hip_lumps()))
	{
		hudtype = 1;
		for (i = 0; i < 5; i++)
			for (j = 0; j <= 6; j++)
				precache_pic(flash_pic(j, hip_weapon_name(i)));
		precache_pic("gfx/sb_wsuit");
		precache_pic("gfx/sb_eshld");
	}
	else if (wad_has_all(ROGUE_LUMPS))
	{
		hudtype = 2;
		n = tokenize(ROGUE_LUMPS);
		for (i = 0; i < n; i++)
			precache_pic(sprintf("gfx/%s", argv(i)));
	}
};

void(vector virtsize, float showscores) chud_draw =
{
	scr = virtsize;

	preset = autocvar_chud_preset;
	if (preset < 1 || preset > 3)
		preset = bound(1, floor(autocvar_scr_sbar), 3);
	sbaralpha = autocvar_scr_sbaralpha;
	hud_viewsize = autocvar_viewsize;

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
	if (widget_begin(W_LOC))
		draw_loc();
	if (maxclients > 1 && widget_begin(W_SCORES))
		draw_scores_list();
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
