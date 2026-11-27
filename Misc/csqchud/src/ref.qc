// Reference progs for compare.py: a full CSQC that renders the normal view
// and lets the engine draw its own status bar (VF_DRAWENGINESBAR), so both
// screenshots come from the same frame path. Not part of the sample HUD.

void(float apilevel, string enginename, float engineversion) CSQC_Init =
{
	registercvar("chud_autotest", "0");
};

void(float vwidth, float vheight, float notmenu) CSQC_UpdateView =
{
	clearscene();
	addentities(MASK_ENGINE | MASK_VIEWMODEL);
	setproperty(VF_DRAWENGINESBAR, 1);
	setproperty(VF_DRAWCROSSHAIR, 0);
	renderscene();
	autotest_frame();
};
