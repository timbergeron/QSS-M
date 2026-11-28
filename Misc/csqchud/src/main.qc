// Entry points for the sample HUD. Only CSQC_DrawHud: this progs works as
// simple CSQC, so a player can drop it into a gamedir for any server.

void(float apilevel, string enginename, float engineversion) CSQC_Init =
{
	chud_registercvars();
	chud_precache();
};

void(vector virtsize, float showscores) CSQC_DrawHud =
{
	chud_draw(virtsize, showscores);
	autotest_frame();
};

// Tab scoreboard (and the deathmatch intermission board): scores.qc
void(vector virtsize, float showscores) CSQC_DrawScores =
{
	chud_drawscores(virtsize, showscores);
};
