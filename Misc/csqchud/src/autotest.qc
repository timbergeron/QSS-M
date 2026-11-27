/*
	Shared screenshot driver for compare.py. chud_autotest 1: give everything
	with impulse 9 once the map has settled, wait out the pickup flash, then
	screenshot and quit. chud_autotest 2: same, but screenshot right away
	(items still flashing) -- unused by default.
*/
float autotest_stage;
float autotest_start;	// cltime, not time: FTE leaves time frozen inside CSQC_DrawHud

void() autotest_frame =
{
	if (!cvar("chud_autotest"))
		return;
	if (!autotest_start)
		autotest_start = cltime;
	if (autotest_stage == 0 && cltime > autotest_start + 1.5)
	{
		localcmd("impulse 9\ngive a 200\n");
		if (cvar_string("chud_autotest_extra") != "")	// e.g. "+forward;+moveright"
			localcmd(strcat(cvar_string("chud_autotest_extra"), "\n"));
		autotest_stage = 1;
	}
	else if (autotest_stage == 1 && cltime > autotest_start + 3.5)
	{
		if (cvar_string("chud_shotcmd") != "")	// e.g. FTE: "screenshot chud.png"
			localcmd(strcat(cvar_string("chud_shotcmd"), "\n"));
		else
			localcmd("screenshot tga\n");
		autotest_stage = 2;
	}
	else if (autotest_stage == 2 && cltime > autotest_start + 4)
	{
		localcmd("quit\n");
		autotest_stage = 3;
	}
};
