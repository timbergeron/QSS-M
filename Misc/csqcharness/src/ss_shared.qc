/*
	ss_shared.qc -- the step table shared by the SSQC lane's server and client halves.

	The server fires one effect per step and follows it, in the same datagram,
	with a marker: a vanilla TE_GUNSHOT at SS_MARKER_X/Y whose z encodes the step.
	The client's CSQC_Parse_TempEntity records the type of every temp entity it
	sees and consumes the markers. A writer and reader that disagree about a
	message's length make the next marker unreadable, so the sequence breaks at
	the step that caused it.
*/

#define SS_MARKER_X		4000
#define SS_MARKER_Y		4000
#define SS_STEPS		25	// effect steps 0..24
#define SS_ENT_SPAWN	26
#define SS_ENT_UPDATE	28
#define SS_ENT_REMOVE	32
#define SS_INFO_STEP	36
#define SS_SETANGLE1	30	// suppressed by CSQC_Parse_SetAngles
#define SS_SETANGLE2	34	// passed through to the engine
#define SS_DELTA_STEP	38
#define SS_YAW_DELTA	22.5
#define SS_DONE_STEP	40
#define SS_DONE_SEQ		99

#define SS_YAW1			37
#define SS_YAW2			123

// The temp entity type the client should see for each step, or -1 when the
// step sends none (svc_particle, svc_effect, or a rejected call).
float(float step) ss_expected_type =
{
	switch (step)
	{
	case 1:		return 51;	// te_spark
	case 2:		return 53;	// te_explosionrgb
	case 3:		return 54;	// te_particlecube
	case 4:		return 52;	// te_bloodshower
	case 5:		return 72;	// te_smallflash
	case 6:		return 73;	// te_customflash
	case 7:		return 74;	// te_flamejet
	case 8:		return 75;	// te_plasmaburn
	case 9:		return 57;	// te_gunshotquad
	case 10:	return 58;	// te_spikequad
	case 11:	return 59;	// te_superspikequad
	case 12:	return 70;	// te_explosionquad
	case 17:	return 73;	// hostile te_customflash
	case 22:	return 16;	// Nehahra explosion, lighting disabled
	case 23:	return 16;	// Nehahra explosion, lighting enabled
	default:	return -1;	// Particle messages, sprite effects, or rejected arguments.
	}
};
