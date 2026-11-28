/*
	scores.qc -- the always-on scores list (QSS-M's scr_showscores) and a
	Tab scoreboard with three layouts, for issue #29.

		chud_scores_*		the list is an ordinary widget ("scores"): show,
							anchor, ofs and scale like the others, plus
		chud_scores_alpha	opacity (default 1)
		chud_scores_rows	most rows shown (default 16, like the engine)

		chud_scoreboard		Tab layout: 0 follows scr_scoreboard_teamsort,
							1 regular, 2 team-sorted, 3 double column
		chud_scoreboard_alpha	row background opacity (default 0.8, like the engine)
		chud_scoreboard_scale	multiplier on top of scr_sbarscale

	Everything comes from standard playerkeys (frag-sorted through negative
	player numbers). On ProQuake/CRMod servers team totals use the server's own
	scores (pq_teamfragsN, which include CRMod arena round wins); elsewhere a
	team's total is the sum of its connected players' frags.
*/

var float autocvar_chud_scores_alpha = 1;
var float autocvar_chud_scores_rows = 16;
var float autocvar_chud_scoreboard = 0;
var float autocvar_chud_scoreboard_alpha = 0.8;
var float autocvar_chud_scoreboard_scale = 1;
var float autocvar_scr_scoreboard_teamsort = 0;

// the engine scoreboard's palette fills: 16 header, 18 row, 20 your row, 0 borders
const vector SB_HEADER = '0.0588 0.0431 0.0275';
const vector SB_ROW = '0.1216 0.0902 0.0431';
const vector SB_SELF = '0.1843 0.1373 0.0745';
const vector SB_BORDER = '0 0 0';

// ---- one frame's player table (frag order, from the engine's fragsort) ----
const float SC_MAX = 32;
float sc_n;
float sc_frags[SC_MAX];
float sc_spec[SC_MAX];		// 1 = spectator
float sc_self[SC_MAX];		// the player being viewed
float sc_team[SC_MAX];		// index into the team table, -1 for none

const float SCT_MAX = 8;
float sct_n;
string sct_name[SCT_MAX];	// strzone'd; freed when the table is rebuilt
float sct_total[SCT_MAX];
vector sct_rgb[SCT_MAX];
float sct_order[SCT_MAX];	// team indices, highest total first
float sc_teams;				// TRUE when rows should be grouped by team

// player i of the frag-sorted list
string(float i, string key) sc_key = { return getplayerkeyvalue(-1 - i, key); };

string(float c) team_colour_name =
{
	if (c == 0) return "white";		if (c == 1) return "brown";
	if (c == 2) return "light blue";	if (c == 3) return "green";
	if (c == 4) return "red";		if (c == 5) return "olive";
	if (c == 6) return "orange";		if (c == 7) return "peach";
	if (c == 8) return "purple";		if (c == 9) return "magenta";
	if (c == 10) return "grey";		if (c == 11) return "aqua";
	if (c == 12) return "yellow";		return "blue";
};

void() sc_collect =
{
	float i, k, j, pq;
	string t;

	for (i = 0; i < sct_n; i++)
		strunzone(sct_name[i]);
	sct_n = 0;
	sc_n = 0;
	// explicit ifs: QC's logical operators don't short-circuit or yield 0/1 reliably
	pq = FALSE;
	if (serverkey("pq_teams") != "")
		pq = TRUE;
	sc_teams = pq;
	if (serverkeyfloat("teamplay") > 0)
		sc_teams = TRUE;

	for (i = 0; i < SC_MAX; i++)
	{
		if (sc_key(i, "name") == "")
			break;
		sc_frags[sc_n] = stof(sc_key(i, "frags"));
		sc_spec[sc_n] = stof(sc_key(i, "*spectator")) == 1;
		sc_self[sc_n] = stof(sc_key(i, "viewentity")) == player_localentnum;
		sc_team[sc_n] = -1;
		if (sc_teams && !sc_spec[sc_n])
		{
			t = sc_key(i, "team");
			if (t != "")
			{
				for (k = 0; k < sct_n; k++)
					if (sct_name[k] == t)
						break;
				if (k == sct_n && sct_n < SCT_MAX)
				{
					sct_name[k] = strzone(t);
					sct_total[k] = 0;
					sct_rgb[k] = stov(sc_key(i, "bottomcolor_rgb"));
					sct_n++;
				}
				if (k < sct_n)
				{
					sc_team[sc_n] = k;
					sct_total[k] += sc_frags[sc_n];
				}
			}
		}
		sc_n++;
	}

	// ProQuake/CRMod: the server's team scores win (they can be arena round
	// wins, not frag sums). QSS-M's "team" key is the palette colour + 1.
	if (pq)
		for (k = 0; k < sct_n; k++)
		{
			t = serverkey(sprintf("pq_teamfrags%d", stof(sct_name[k]) - 1));
			if (t != "")
				sct_total[k] = stof(t);
		}

	for (k = 0; k < sct_n; k++)
	{	// insertion sort, highest total first
		j = k;
		while (j > 0)	// no "&&" guard: fteqcc doesn't short-circuit, [j - 1] would read -1
		{
			if (sct_total[k] <= sct_total[sct_order[j - 1]])
				break;
			sct_order[j] = sct_order[j - 1];
			j--;
		}
		sct_order[j] = k;
	}
};

// a team's label: QuakeWorld team names as-is, NQ colour teams by colour name
string(float k) team_label =
{
	float c = stof(sct_name[k]);
	if (sprintf("%d", c) == sct_name[k] && c >= 1 && c <= 14)
		return sprintf("%s team", team_colour_name(c - 1));
	return sct_name[k];
};

// ---- drawing in board units: (x, y) * sb_scale from sb_org ----
vector sb_org;
float sb_scale;
float sb_alpha;

// Whole pixels everywhere: at fractional positions (any scale but 1) font
// quads sample their neighbours in the glyph sheet and show stray marks.
vector(vector p) pix = { return [floor(p_x + 0.5), floor(p_y + 0.5), 0]; };

void(float x, float y, float w, float h, vector rgb, float a) sb_fill =
{
	vector a0 = pix(sb_org + [x, y, 0] * sb_scale), a1 = pix(sb_org + [x + w, y + h, 0] * sb_scale);
	drawfill(a0, a1 - a0, rgb, a, 0);
};

void(float x, float y, string s) sb_text =
{
	drawstring(pix(sb_org + [x, y, 0] * sb_scale), s, '8 8' * sb_scale, '1 1 1', 1, 0);
};

void(float x, float y, float c) sb_char =
{
	drawcharacter(pix(sb_org + [x, y, 0] * sb_scale), c, '8 8' * sb_scale, '1 1 1', 1, 0);
};

// Sbar_DeathmatchOverlay's anchor: x (width - 320) / 2, first row at
// (height - 200) / 2 - 20, raised only as far as needed to fit. Whole pixels:
// a fractional origin makes text quads sample neighbouring font glyphs.
float(vector vs, float rows) sb_place =
{
	float row_y, need, maxrows;
	maxrows = min(16, rows);
	need = (maxrows * 10 + 1 + (rows > maxrows ? 10 : 0)) * sb_scale;
	row_y = (vs_y - 200 * sb_scale) * 0.5 - 20 * sb_scale;
	if (row_y + need > vs_y)
		row_y = vs_y - need;
	row_y = max(12 * sb_scale, row_y);
	sb_org = [floor((vs_x - 320 * sb_scale) * 0.5), floor(row_y - 12 * sb_scale), 0];
	return min(maxrows, max(1, floor(((vs_y - row_y) / sb_scale - 11) / 10)));
};

// frame of a panel w wide: borders left/right for rows [y, y + h)
void(float x, float y, float w, float h) sb_sides =
{
	sb_fill(x, y, 1, h, SB_BORDER, 1);
	sb_fill(x + w - 1, y, 1, h, SB_BORDER, 1);
};

void(float x, float y, float w, string title) sb_header =
{
	sb_fill(x, y - 11, w - 1, 10, SB_HEADER, 1);
	sb_fill(x, y - 12, w, 1, SB_BORDER, 1);
	sb_sides(x, y - 12, w, 11);
	sb_fill(x, y - 1, w, 1, SB_BORDER, 1);
	sb_text(x, y - 10, title);
};

// one Sbar_DeathmatchOverlay row at panel-left x, row-top y
void(float x, float y, float i) sb_row_full =
{
	float ping = bound(1, stof(sc_key(i, "ping")), 9999), pl = stof(sc_key(i, "pl"));
	sb_fill(x + 1, y, 360, 10, sc_self[i] ? SB_SELF : SB_ROW, sb_alpha);
	sb_sides(x, y, 361, 10);
	if (!sc_spec[i])
	{
		sb_fill(x + 96, y, 40, 4, stov(sc_key(i, "topcolor_rgb")), 1);
		sb_fill(x + 96, y + 4, 40, 4, stov(sc_key(i, "bottomcolor_rgb")), 1);
		sb_text(x + 104, y, sprintf("%3d", sc_frags[i]));
		if (sc_self[i])
			sb_char(x + 144, y, 12);
	}
	else
		sb_text(x + 96, y, "spect");
	sb_text(x + 10, y, sprintf("%4d", ping));
	if (pl > 0)
	{
		string s = sprintf("%3d", min(pl, 100));
		if (pl >= 10)	// red, like the engine
			for (float c = 0; c < 3; c++)
				sb_char(x + 54 + c * 8, y, str2chr(s, c) + 128);
		else
			sb_text(x + 54, y, s);
	}
	sb_text(x + 160, y, sc_key(i, "name"));
};

// a team's summary row in the full layout
void(float x, float y, float k) sb_row_team =
{
	sb_fill(x + 1, y, 360, 10, sct_rgb[k], sb_alpha);
	sb_sides(x, y, 361, 10);
	sb_text(x + 104, y, sprintf("%3d", sct_total[k]));
	sb_text(x + 160, y, team_label(k));
};

// 1 regular, 2 team-sorted: the engine's single panel
void(vector vs, float teamrows) sb_draw_single =
{
	float x, y, i, k, rows, maxrows, drawn;

	rows = sc_n + (teamrows ? sct_n : 0);
	maxrows = sb_place(vs, rows);
	x = 0;
	y = 12;

	sb_header(x, y, 361, "  ping  pl  frags   name");
	drawn = 0;
	if (teamrows)
	{
		for (k = 0; k < sct_n; k++)
		{
			if (drawn >= maxrows)
				break;
			sb_row_team(x, y, sct_order[k]);
			y += 10;
			drawn++;
			for (i = 0; i < sc_n && drawn < maxrows; i++)
				if (sc_team[i] == sct_order[k])
				{
					sb_row_full(x, y, i);
					y += 10;
					drawn++;
				}
		}
		for (i = 0; i < sc_n && drawn < maxrows; i++)
			if (sc_team[i] < 0)	// spectators and players without a team
			{
				sb_row_full(x, y, i);
				y += 10;
				drawn++;
			}
	}
	else
		for (i = 0; i < sc_n && drawn < maxrows; i++)
		{
			sb_row_full(x, y, i);
			y += 10;
			drawn++;
		}

	sb_fill(x, y, 361, 1, SB_BORDER, 1);
	if (rows > drawn)
		sb_text(x + 4, y + 3, sprintf("+%d more", rows - drawn));
};

// one compact row for the double-column layout (196 wide)
void(float x, float y, float i) sb_row_compact =
{
	sb_fill(x + 1, y, 194, 10, sc_self[i] ? SB_SELF : SB_ROW, sb_alpha);
	sb_sides(x, y, 196, 10);
	if (!sc_spec[i])
	{
		sb_fill(x + 4, y, 32, 4, stov(sc_key(i, "topcolor_rgb")), 1);
		sb_fill(x + 4, y + 4, 32, 4, stov(sc_key(i, "bottomcolor_rgb")), 1);
		sb_text(x + 8, y, sprintf("%3d", sc_frags[i]));
	}
	else
		sb_text(x + 4, y, "spec");
	sb_text(x + 40, y, sprintf("%4d", bound(1, stof(sc_key(i, "ping")), 9999)));
	sb_text(x + 80, y, substring(sc_key(i, "name"), 0, 14));
};

// 3: two columns -- teams side by side (Q3/QL style), or an FFA list split in half
void(vector vs) sb_draw_double =
{
	float col, i, k, y, n, half, maxrows, rows[2], start;
	float colteam[2];

	colteam[0] = -1;
	colteam[1] = -1;
	if (sc_teams && sct_n >= 2)
	{
		colteam[0] = sct_order[0];
		colteam[1] = sct_order[1];
	}
	for (col = 0; col < 2; col++)
	{
		n = 0;
		for (i = 0; i < sc_n; i++)
			if (colteam[col] >= 0 ? sc_team[i] == colteam[col] : TRUE)
				n++;
		rows[col] = n;
	}
	if (colteam[0] < 0)
	{	// FFA: first half left, the rest right
		half = ceil(sc_n / 2);
		rows[0] = half;
		rows[1] = sc_n - half;
	}

	maxrows = sb_place(vs, max(rows[0], rows[1]));
	sb_org_x = floor((vs_x - 400 * sb_scale) * 0.5);	// two 196-wide columns and a gap

	for (col = 0; col < 2; col++)
	{
		float x = col * 204;
		y = 12;
		if (colteam[col] >= 0)
		{
			k = colteam[col];
			sb_header(x, y, 196, sprintf("%-15s %4d", substring(team_label(k), 0, 15), sct_total[k]));
			sb_fill(x + 1, y - 11, 194, 2, sct_rgb[k], 1);	// team colour stripe
		}
		else
			sb_header(x, y, 196, " frags ping name");

		n = 0;
		start = (colteam[0] < 0 && col == 1) ? half : 0;
		for (i = 0; i < sc_n && n < maxrows; i++)
		{
			if (colteam[col] >= 0 && sc_team[i] != colteam[col])
				continue;
			if (colteam[col] < 0 && (i < start || i >= start + rows[col]))
				continue;
			sb_row_compact(x, y, i);
			y += 10;
			n++;
		}
		sb_fill(x, y, 196, 1, SB_BORDER, 1);
		if (rows[col] > n)
			sb_text(x + 4, y + 3, sprintf("+%d more", rows[col] - n));
	}

	if (colteam[0] >= 0)
	{	// players of a third team or spectators go below, compact
		y = 12 + max(min(rows[0], maxrows), min(rows[1], maxrows)) * 10 + 14;
		n = 0;
		for (i = 0; i < sc_n && n < maxrows; i++)
			if (sc_team[i] != colteam[0] && sc_team[i] != colteam[1])
			{
				sb_row_compact((n & 1) ? 204 : 0, y, i);
				if (n & 1)
					y += 10;
				n++;
			}
	}
};

void(vector vs, float showscores) chud_drawscores =
{
	float layout;
	if (maxclients <= 1)
		return;		// single player: CSQC_DrawHud draws the level-stats bar on Tab
	if (!showscores && !intermission)
		return;

	sb_scale = max(0.25, autocvar_chud_scoreboard_scale);
	sb_alpha = bound(0, autocvar_chud_scoreboard_alpha, 1);
	sc_collect();

	layout = autocvar_chud_scoreboard;
	if (layout < 1 || layout > 3)
		layout = autocvar_scr_scoreboard_teamsort ? 2 : 1;
	if (layout == 3)
		sb_draw_double(vs);
	else
		sb_draw_single(vs, layout == 2 && sc_teams && sct_n > 0);
};

// ---- the always-on list (scr_showscores) as a widget ----
float() scores_list_rows =
{
	float i;
	for (i = 0; i < SC_MAX; i++)
		if (getplayerkeyvalue(-1 - i, "name") == "")
			break;
	return min(i, max(1, autocvar_chud_scores_rows));
};

// SCR_ShowObsFrags: colour bars, frags, your brackets, short name; highest on top
void() draw_scores_list =
{
	float i, n = scores_list_rows(), a = bound(0, autocvar_chud_scores_alpha, 1);
	string name;
	for (i = 0; i < n; i++)
	{
		vector p = [0, i * 8, 0];
		drawfill(pix(wpos + (p + '0 1') * wscale), '40 4' * wscale, stov(getplayerkeyvalue(-1 - i, "topcolor_rgb")), a, 0);
		drawfill(pix(wpos + (p + '0 5') * wscale), '40 3' * wscale, stov(getplayerkeyvalue(-1 - i, "bottomcolor_rgb")), a, 0);
		if (stof(getplayerkeyvalue(-1 - i, "viewentity")) == player_localentnum)
		{
			drawcharacter(pix(wpos + (p + '-2 0') * wscale), 144, '8 8' * wscale, '1 1 1', a, 0);
			drawcharacter(pix(wpos + (p + '33 0') * wscale), 145, '8 8' * wscale, '1 1 1', a, 0);
		}
		drawstring(pix(wpos + (p + '8 0') * wscale), sprintf("%3d", stof(getplayerkeyvalue(-1 - i, "frags"))), '8 8' * wscale, '1 1 1', a, 0);
		name = substring(getplayerkeyvalue(-1 - i, "name"), 0, 15);
		drawstring(pix(wpos + (p + '50 0') * wscale), name, '8 8' * wscale, '1 1 1', a, 0);
	}
};
