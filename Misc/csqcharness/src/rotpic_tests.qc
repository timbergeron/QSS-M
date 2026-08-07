/*
	rotpic_tests.qc -- shared test panel for QSS-M's drawrotpic/drawrotsubpic.

	Compiled into both csprogs.dat (CSQC_DrawHud) and menu.dat (m_draw) so that
	both builtin paths registered by pr_ext.c get exercised.

	The panel is laid out as a grid of 64x64 cells.  Cells are arranged in
	reference/candidate pairs so that a screenshot can be diffed
	programmatically -- see check_rotpic.py.

	Grid (origin GX,GY, cell 64, gap 8):

	  row 0:  [A] drawpic          [B] drawrotpic 0     [C] drawrotpic 360   [D] drawrotsubpic 0 (full)
	  row 1:  [E] drawrotpic 90    [F] drawrotpic 180   [G] drawrotpic 270   [H] drawrotpic -90
	  row 2:  [I] drawsubpic q0    [J] drawrotsubpic 0  [K] drawrotsubpic 90 [L] drawrotsubpic q3
	  row 3:  [M] drawpic tinted   [N] drawrotpic tint  [O] pivot offset     [P] free spinner

	A==B==C==D, E==rot90(A), F==rot180(A), G==rot270(A), H==G,
	I==J, K==rot90(I), M==N.
*/

#define TESTPIC		"gfx/help0.lmp"
#define SPINPIC		"gfx/qplaque.lmp"

#define CELL		64
#define GAP			8
#define GX			16
#define GY			72		// clear of the engine's top-of-screen widgets

#define WHITE		'1 1 1'

// "rotpic_freeze 1" pins every angle so a screenshot is deterministic
var float autocvar_rotpic_freeze = 0;
// "rotpic_autoshot 1" freezes the panel, screenshots it, then quits.  The QC
// drives that itself: a wait-spammed cfg stalls the client's signon, so an
// externally timed screenshot never catches a running CSQC.
var float autocvar_rotpic_autoshot = 0;
// "hq_selftest 1" runs the builtin assertions and quits
var float autocvar_hq_selftest = 0;

float rp_frames;

#define rotpic_freeze (autocvar_rotpic_freeze || autocvar_rotpic_autoshot)

// cell top-left corner
vector(float col, float row) rp_cell =
{
	return [GX + col * (CELL + GAP), GY + row * (CELL + GAP), 0];
};
// cell centre, i.e. the pivot for a centred rotation
vector(float col, float row) rp_pivot =
{
	return [GX + col * (CELL + GAP) + CELL * 0.5, GY + row * (CELL + GAP) + CELL * 0.5, 0];
};

void(float col, float row, string label) rp_label =
{
	local vector p = rp_cell(col, row);
	drawstring([p_x, p_y - 10, 0], label, '8 8 0', WHITE, 1, 0);
};

void(vector screensize) RotPic_DrawTests =
{
	local float ang;
	local vector half = [CELL * 0.5, CELL * 0.5, 0];
	local vector nhalf = [CELL * -0.5, CELL * -0.5, 0];
	local vector p;

	// opaque backdrop: the tint/alpha cells must composite over a known colour
	drawfill([GX - 8, GY - 16, 0],
		[4 * (CELL + GAP) + 8, 4 * (CELL + GAP) + 16, 0], '0 0 0', 1, 0);

	ang = rotpic_freeze ? 0 : (time * 60);

	// --- row 0: identity.  All four cells must be pixel-identical. ---
	drawpic(rp_cell(0, 0), TESTPIC, [CELL, CELL, 0], WHITE, 1, 0);
	drawrotpic(rp_pivot(1, 0), nhalf, half, TESTPIC, WHITE, 1, 0);
	drawrotpic(rp_pivot(2, 0), nhalf, half, TESTPIC, WHITE, 1, 360);
	drawrotsubpic(rp_pivot(3, 0), nhalf, half, TESTPIC, '0 0 0', '1 1 0', WHITE, [1, 0, 0]);
	rp_label(0, 0, "pic");
	rp_label(1, 0, "rot0");
	rp_label(2, 0, "rot360");
	rp_label(3, 0, "rsub0");

	// --- row 1: cardinal rotations of the row-0 reference ---
	drawrotpic(rp_pivot(0, 1), nhalf, half, TESTPIC, WHITE, 1, 90);
	drawrotpic(rp_pivot(1, 1), nhalf, half, TESTPIC, WHITE, 1, 180);
	drawrotpic(rp_pivot(2, 1), nhalf, half, TESTPIC, WHITE, 1, 270);
	drawrotpic(rp_pivot(3, 1), nhalf, half, TESTPIC, WHITE, 1, -90);
	rp_label(0, 1, "rot90");
	rp_label(1, 1, "rot180");
	rp_label(2, 1, "rot270");
	rp_label(3, 1, "rot-90");

	// --- row 2: sub-rect texture coords ---
	drawsubpic(rp_cell(0, 2), [CELL, CELL, 0], TESTPIC, '0 0 0', '0.5 0.5 0', WHITE, 1, 0);
	drawrotsubpic(rp_pivot(1, 2), nhalf, half, TESTPIC, '0 0 0', '0.5 0.5 0', WHITE, [1, 0, 0]);
	drawrotsubpic(rp_pivot(2, 2), nhalf, half, TESTPIC, '0 0 0', '0.5 0.5 0', WHITE, [1, 90, 0]);
	drawrotsubpic(rp_pivot(3, 2), nhalf, half, TESTPIC, '0.5 0.5 0', '0.5 0.5 0', WHITE, [1, 0, 0]);
	rp_label(0, 2, "sub q0");
	rp_label(1, 2, "rsub q0");
	rp_label(2, 2, "rsub90");
	rp_label(3, 2, "rsub q3");

	// --- row 3: colour/alpha, off-centre pivot, and a live spinner ---
	drawpic(rp_cell(0, 3), TESTPIC, [CELL, CELL, 0], '1 0.25 0.25', 0.5, 0);
	drawrotpic(rp_pivot(1, 3), nhalf, half, TESTPIC, '1 0.25 0.25', 0.5, 0);
	// pivot at the cell's top-left with a 0..CELL box: rotating about a corner
	p = rp_cell(2, 3);
	drawrotpic(p, '0 0 0', [CELL, CELL, 0], TESTPIC, WHITE, 1, rotpic_freeze ? 0 : ang);
	// free spinner, drawrotsubpic driving alpha+angle out of one vector
	drawrotsubpic(rp_pivot(3, 3), nhalf, half, SPINPIC, '0 0 0', '1 1 0', WHITE,
		[rotpic_freeze ? 1 : (0.5 + 0.5 * sin(time * 2)), rotpic_freeze ? 0 : -ang, 0]);
	rp_label(0, 3, "tint.5");
	rp_label(1, 3, "rtint.5");
	rp_label(2, 3, "corner");
	rp_label(3, 3, "spin");

	drawstring([GX, GY + 4 * (CELL + GAP) + 4, 0],
		"drawrotpic / drawrotsubpic test - \"rotpic_freeze 1\" stops the animation",
		'8 8 0', '1 1 0.4', 1, 0);

	if (autocvar_rotpic_autoshot)
	{
		rp_frames = rp_frames + 1;
		if (rp_frames == 1)
		{
			// 1:1 canvas so check_rotpic.py can find the cells by pixel, and
			// no engine HUD widgets over the grid -- several of them draw
			// after CSQC_DrawHud and would land on the top row.
			localcmd("scr_sbarscale 1;scr_menuscale 1;con_notifytime 0\n");
			localcmd("crosshair 0;scr_ping 0;scr_clock 0;scr_showfps 0\n");
			localcmd("scr_showspeed 0;scr_showscores 0;scr_matchclock 0\n");
			localcmd("scr_showgrenadecounter 0\n");
		}
		else if (rp_frames == 45)
			localcmd("screenshot tga\n");
		else if (rp_frames == 90)
			localcmd("quit\n");
	}
};
