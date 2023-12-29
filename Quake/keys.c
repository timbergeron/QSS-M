/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "quakedef.h"
#include "arch_def.h"
#include "q_ctype.h" // woods #iwtabcomplete

/* key up events are sent even if in console mode */

#define		HISTORY_FILE_NAME "history.txt"

char		key_lines[CMDLINES][MAXCMDLINE];
char		key_tabhint[MAXCMDLINE]; // woods #iwtabcomplete

size_t	key_linepos = 0; // woods -- int to size_t
int		key_insert = true;	//johnfitz -- insert key toggle (for editing)
double		key_blinktime; //johnfitz -- fudge cursor blinking to make it easier to spot in certain cases

int		edit_line = 0;
int		history_line = 0;
static qboolean history_initialized = false; // woods #serverhistory
static char history_saved_current[MAXCMDLINE]; // woods #serverhistory
static qboolean history_controls_registered = false;

static cvar_t con_savehistory = {"con_savehistory", "1", CVAR_ARCHIVE}; // woods #historyprivacy
static qboolean History_SaveHistoryEnabled (void);

keydest_t	key_dest;

int			key_bindmap[2] = {0,1};
char		*keybindings[MAX_BINDMAPS][MAX_KEYS];
qboolean	consolekeys[MAX_KEYS];	// if true, can't be rebound while in console
qboolean	menubound[MAX_KEYS];	// if true, can't be rebound while in menu
qboolean	keydown[MAX_KEYS];
#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC)
static qboolean command_q_active;
#endif

qboolean Key_IsShortcutModifierDown (void)
{
#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC)
	return keydown[K_CTRL] || keydown[K_COMMAND];
#else
	return keydown[K_CTRL];
#endif
}

qboolean	Cmd_Exists2(const char* cmd_name); // woods #ezsay
qboolean	ctrlpressed; // woods #saymodifier
static qboolean key_gamepad_altmodifier_pressed = false;
static qboolean key_gamepad_alttranslated[K_GAMEPAD_COUNT];

void Sound_Toggle_Mute_f (void); // woods #usermute
void SCR_Mute_Switch (void); // woods #usermute
void Con_Copy_f (void); // woods #concopy
extern char mute[2];			// woods for mute to memory #usermute

void VID_Minimize (void); // woods for mac command-tab

typedef struct
{
	const char	*name;
	int		keynum;
} keyname_t;

static const keyname_t keynames[] =
{
	{"TAB", K_TAB},
	{"ENTER", K_ENTER},
	{"ESCAPE", K_ESCAPE},
	{"SPACE", K_SPACE},
	{"BACKSPACE", K_BACKSPACE},
	{"UPARROW", K_UPARROW},
	{"DOWNARROW", K_DOWNARROW},
	{"LEFTARROW", K_LEFTARROW},
	{"RIGHTARROW", K_RIGHTARROW},
	{"CAPSLOCK", K_CAPSLOCK}, // woods #capslock
	{"PRINTSCREEN", K_PRINTSCREEN}, // woods #printscreen

	{"ALT", K_ALT},
	{"CTRL", K_CTRL},
	{"SHIFT", K_SHIFT},

//	{"KP_NUMLOCK", K_KP_NUMLOCK},
	{"KP_SLASH", K_KP_SLASH},
	{"KP_STAR", K_KP_STAR},
	{"KP_MINUS", K_KP_MINUS},
	{"KP_HOME", K_KP_HOME},
	{"KP_UPARROW", K_KP_UPARROW},
	{"KP_PGUP", K_KP_PGUP},
	{"KP_PLUS", K_KP_PLUS},
	{"KP_LEFTARROW", K_KP_LEFTARROW},
	{"KP_5", K_KP_5},
	{"KP_RIGHTARROW", K_KP_RIGHTARROW},
	{"KP_END", K_KP_END},
	{"KP_DOWNARROW", K_KP_DOWNARROW},
	{"KP_PGDN", K_KP_PGDN},
	{"KP_ENTER", K_KP_ENTER},
	{"KP_INS", K_KP_INS},
	{"KP_DEL", K_KP_DEL},

	{"F1", K_F1},
	{"F2", K_F2},
	{"F3", K_F3},
	{"F4", K_F4},
	{"F5", K_F5},
	{"F6", K_F6},
	{"F7", K_F7},
	{"F8", K_F8},
	{"F9", K_F9},
	{"F10", K_F10},
	{"F11", K_F11},
	{"F12", K_F12},

	{"INS", K_INS},
	{"DEL", K_DEL},
	{"PGDN", K_PGDN},
	{"PGUP", K_PGUP},
	{"HOME", K_HOME},
	{"END", K_END},

	{"COMMAND", K_COMMAND},

	{"MOUSE1", K_MOUSE1},
	{"MOUSE2", K_MOUSE2},
	{"MOUSE3", K_MOUSE3},
	{"MOUSE4", K_MOUSE4},
	{"MOUSE5", K_MOUSE5},

	{"JOY1", K_JOY1},
	{"JOY2", K_JOY2},
	{"JOY3", K_JOY3},
	{"JOY4", K_JOY4},

	{"AUX1", K_AUX1},
	{"AUX2", K_AUX2},
	{"AUX3", K_AUX3},
	{"AUX4", K_AUX4},
	{"AUX5", K_AUX5},
	{"AUX6", K_AUX6},
	{"AUX7", K_AUX7},
	{"AUX8", K_AUX8},
	{"AUX9", K_AUX9},
	{"AUX10", K_AUX10},
	{"AUX11", K_AUX11},
	{"AUX12", K_AUX12},
	{"AUX13", K_AUX13},
	{"AUX14", K_AUX14},
	{"AUX15", K_AUX15},
	{"AUX16", K_AUX16},
	{"AUX17", K_AUX17},
	{"AUX18", K_AUX18},
	{"AUX19", K_AUX19},
	{"AUX20", K_AUX20},
	{"AUX21", K_AUX21},
	{"AUX22", K_AUX22},
	{"AUX23", K_AUX23},
	{"AUX24", K_AUX24},
	{"AUX25", K_AUX25},
	{"AUX26", K_AUX26},
	{"AUX27", K_AUX27},
	{"AUX28", K_AUX28},
	{"AUX29", K_AUX29},
	{"AUX30", K_AUX30},
	{"AUX31", K_AUX31},
	{"AUX32", K_AUX32},

	{"PAUSE", K_PAUSE},

	{"MWHEELUP", K_MWHEELUP},
	{"MWHEELDOWN", K_MWHEELDOWN},

	{"SEMICOLON", ';'},	// because a raw semicolon seperates commands

	{"BACKQUOTE", '`'},	// because a raw backquote may toggle the console
	{"TILDE", '~'},		// because a raw tilde may toggle the console

	{"LTHUMB", K_LTHUMB},
	{"RTHUMB", K_RTHUMB},
	{"LSHOULDER", K_LSHOULDER},
	{"RSHOULDER", K_RSHOULDER},
	{"DPAD_UP", K_DPAD_UP},
	{"DPAD_DOWN", K_DPAD_DOWN},
	{"DPAD_LEFT", K_DPAD_LEFT},
	{"DPAD_RIGHT", K_DPAD_RIGHT},
	{"ABUTTON", K_ABUTTON},
	{"BBUTTON", K_BBUTTON},
	{"XBUTTON", K_XBUTTON},
	{"YBUTTON", K_YBUTTON},
	{"LTRIGGER", K_LTRIGGER},
	{"RTRIGGER", K_RTRIGGER},
	{"MISC1", K_MISC1},
	{"PADDLE1", K_PADDLE1},
	{"PADDLE2", K_PADDLE2},
	{"PADDLE3", K_PADDLE3},
	{"PADDLE4", K_PADDLE4},
	{"TOUCHPAD", K_TOUCHPAD},

	// Gamepad "Start" and "Back" buttons are always mapped to ESC/TAB.
	// We don't expose key names for them so they can't be rebound in the console.

	{"LTHUMB_ALT", K_LTHUMB_ALT},
	{"RTHUMB_ALT", K_RTHUMB_ALT},
	{"LSHOULDER_ALT", K_LSHOULDER_ALT},
	{"RSHOULDER_ALT", K_RSHOULDER_ALT},
	{"DPAD_UP_ALT", K_DPAD_UP_ALT},
	{"DPAD_DOWN_ALT", K_DPAD_DOWN_ALT},
	{"DPAD_LEFT_ALT", K_DPAD_LEFT_ALT},
	{"DPAD_RIGHT_ALT", K_DPAD_RIGHT_ALT},
	{"ABUTTON_ALT", K_ABUTTON_ALT},
	{"BBUTTON_ALT", K_BBUTTON_ALT},
	{"XBUTTON_ALT", K_XBUTTON_ALT},
	{"YBUTTON_ALT", K_YBUTTON_ALT},
	{"LTRIGGER_ALT", K_LTRIGGER_ALT},
	{"RTRIGGER_ALT", K_RTRIGGER_ALT},
	{"MISC1_ALT", K_MISC1_ALT},
	{"PADDLE1_ALT", K_PADDLE1_ALT},
	{"PADDLE2_ALT", K_PADDLE2_ALT},
	{"PADDLE3_ALT", K_PADDLE3_ALT},
	{"PADDLE4_ALT", K_PADDLE4_ALT},
	{"TOUCHPAD_ALT", K_TOUCHPAD_ALT},

	{NULL,		0}
};

static const char *const xbox_names[K_GAMEPAD_COUNT] =
{
#define GAMEPAD_KEY_NAME(keycode, value, xboxname, psname, nintendoname) xboxname,
	GAMEPAD_KEY_LIST(GAMEPAD_KEY_NAME)
#undef GAMEPAD_KEY_NAME
};

static const char *const ps_names[K_GAMEPAD_COUNT] =
{
#define GAMEPAD_KEY_NAME(keycode, value, xboxname, psname, nintendoname) psname,
	GAMEPAD_KEY_LIST(GAMEPAD_KEY_NAME)
#undef GAMEPAD_KEY_NAME
};

static const char *const nintendo_names[K_GAMEPAD_COUNT] =
{
#define GAMEPAD_KEY_NAME(keycode, value, xboxname, psname, nintendoname) nintendoname,
	GAMEPAD_KEY_LIST(GAMEPAD_KEY_NAME)
#undef GAMEPAD_KEY_NAME
};



//QC key codes are based upon DP's keycode constants. This is on account of menu.dat coming first.
int Key_NativeToQC(int code)
{
	switch(code)
	{
	case K_TAB:				return 9;
	case K_ENTER:			return 13;
	case K_ESCAPE:			return 27;
	case K_SPACE:			return 32;
	case K_BACKSPACE:		return 127;
	case K_UPARROW:			return 128;
	case K_DOWNARROW:		return 129;
	case K_LEFTARROW:		return 130;
	case K_RIGHTARROW:		return 131;
	case K_ALT:				return 132;
	case K_CTRL:			return 133;
	case K_SHIFT:			return 134;
	case K_F1:				return 135;
	case K_F2:				return 136;
	case K_F3:				return 137;
	case K_F4:				return 138;
	case K_F5:				return 139;
	case K_F6:				return 140;
	case K_F7:				return 141;
	case K_F8:				return 142;
	case K_F9:				return 143;
	case K_F10:				return 144;
	case K_F11:				return 145;
	case K_F12:				return 146;
	case K_INS:				return 147;
	case K_DEL:				return 148;
	case K_PGDN:			return 149;
	case K_PGUP:			return 150;
	case K_HOME:			return 151;
	case K_END:				return 152;
	case K_PAUSE:			return 153;
	case K_KP_NUMLOCK:		return 154;
	case K_CAPSLOCK:		return 155; // woods enable #capslock
//	case K_SCRLCK:			return 156;
	case K_KP_INS:			return 157;
	case K_KP_END:			return 158;
	case K_KP_DOWNARROW:	return 159;
	case K_KP_PGDN:			return 160;
	case K_KP_LEFTARROW:	return 161;
	case K_KP_5:			return 162;
	case K_KP_RIGHTARROW:	return 163;
	case K_KP_HOME:			return 164;
	case K_KP_UPARROW:		return 165;
	case K_KP_PGUP:			return 166;
	case K_KP_DEL:			return 167;
	case K_KP_SLASH:		return 168;
	case K_KP_STAR:			return 169;
	case K_KP_MINUS:		return 170;
	case K_KP_PLUS:			return 171;
	case K_KP_ENTER:		return 172;
//	case K_KP_EQUALS:		return 173;
	case K_PRINTSCREEN:		return 174; // woods #printscreen

	case K_MOUSE1:			return 512;
	case K_MOUSE2:			return 513;
	case K_MOUSE3:			return 514;
	case K_MWHEELUP:		return 515;
	case K_MWHEELDOWN:		return 516;
	case K_MOUSE4:			return 517;
	case K_MOUSE5:			return 518;
//	case K_MOUSE6:			return 519;
//	case K_MOUSE7:			return 520;
//	case K_MOUSE8:			return 521;
//	case K_MOUSE9:			return 522;
//	case K_MOUSE10:			return 523;
//	case K_MOUSE11:			return 524;
//	case K_MOUSE12:			return 525;
//	case K_MOUSE13:			return 526;
//	case K_MOUSE14:			return 527;
//	case K_MOUSE15:			return 528;
//	case K_MOUSE16:			return 529;

	case K_JOY1:			return 768;
	case K_JOY2:			return 769;
	case K_JOY3:			return 770;
	case K_JOY4:			return 771;
//	case K_JOY5:			return 772;
//	case K_JOY6:			return 773;
//	case K_JOY7:			return 774;
//	case K_JOY8:			return 775;
//	case K_JOY9:			return 776;
//	case K_JOY10:			return 777;
//	case K_JOY11:			return 778;
//	case K_JOY12:			return 779;
//	case K_JOY13:			return 780;
//	case K_JOY14:			return 781;
//	case K_JOY15:			return 782;
//	case K_JOY16:			return 783;

	case K_AUX1:			return 784;
	case K_AUX2:			return 785;
	case K_AUX3:			return 786;
	case K_AUX4:			return 787;
	case K_AUX5:			return 788;
	case K_AUX6:			return 789;
	case K_AUX7:			return 790;
	case K_AUX8:			return 791;
	case K_AUX9:			return 792;
	case K_AUX10:			return 793;
	case K_AUX11:			return 794;
	case K_AUX12:			return 795;
	case K_AUX13:			return 796;
	case K_AUX14:			return 797;
	case K_AUX15:			return 798;
	case K_AUX16:			return 799;
	case K_AUX17:			return 800;
	case K_AUX18:			return 801;
	case K_AUX19:			return 802;
	case K_AUX20:			return 803;
	case K_AUX21:			return 804;
	case K_AUX22:			return 805;
	case K_AUX23:			return 806;
	case K_AUX24:			return 807;
	case K_AUX25:			return 808;
	case K_AUX26:			return 809;
	case K_AUX27:			return 810;
	case K_AUX28:			return 811;
	case K_AUX29:			return 812;
	case K_AUX30:			return 813;
	case K_AUX31:			return 814;
	case K_AUX32:			return 815;

	case K_DPAD_UP:			return 816;
	case K_DPAD_DOWN:		return 817;
	case K_DPAD_LEFT:		return 818;
	case K_DPAD_RIGHT:		return 819;
//	case K_GP_START:		return 820;
//	case K_GP_BACK:			return 821;
	case K_LTHUMB:			return 822;
	case K_RTHUMB:			return 823;
	case K_LSHOULDER:		return 824;
	case K_RSHOULDER:		return 825;
	case K_ABUTTON:			return 826;
	case K_BBUTTON:			return 827;
	case K_XBUTTON:			return 828;
	case K_YBUTTON:			return 829;
	case K_LTRIGGER:		return 830;
	case K_RTRIGGER:		return 831;
	case K_MISC1:			return 840;
	case K_PADDLE1:			return 841;
	case K_PADDLE2:			return 842;
	case K_PADDLE3:			return 843;
	case K_PADDLE4:			return 844;
	case K_TOUCHPAD:		return 845;
	case K_LTHUMB_ALT:		return 846;
	case K_RTHUMB_ALT:		return 847;
	case K_LSHOULDER_ALT:	return 848;
	case K_RSHOULDER_ALT:	return 849;
	case K_DPAD_UP_ALT:		return 850;
	case K_DPAD_DOWN_ALT:	return 851;
	case K_DPAD_LEFT_ALT:	return 852;
	case K_DPAD_RIGHT_ALT:	return 853;
	case K_ABUTTON_ALT:		return 854;
	case K_BBUTTON_ALT:		return 855;
	case K_XBUTTON_ALT:		return 856;
	case K_YBUTTON_ALT:		return 857;
	case K_LTRIGGER_ALT:	return 858;
	case K_RTRIGGER_ALT:	return 859;
	case K_MISC1_ALT:		return 860;
	case K_PADDLE1_ALT:		return 861;
	case K_PADDLE2_ALT:		return 862;
	case K_PADDLE3_ALT:		return 863;
	case K_PADDLE4_ALT:		return 864;
	case K_TOUCHPAD_ALT:	return 865;

	default:
		//ascii chars are mapped as-is (yes this means upper-case keys don't get used).
		if (code >= 0 && code < 127)
			return code;
		return -code;	//qc doesn't have extended keys available to it.
	}
}

int Key_QCToNative(int code)
{
	switch(code)
	{
	case 9:			return K_TAB;
	case 13:		return K_ENTER;
	case 27:		return K_ESCAPE;
	case 32:		return K_SPACE;
	case 127:		return K_BACKSPACE;
	case 128:		return K_UPARROW;
	case 129:		return K_DOWNARROW;
	case 130:		return K_LEFTARROW;
	case 131:		return K_RIGHTARROW;
	case 132:		return K_ALT;
	case 133:		return K_CTRL;
	case 134:		return K_SHIFT;
	case 135:		return K_F1;
	case 136:		return K_F2;
	case 137:		return K_F3;
	case 138:		return K_F4;
	case 139:		return K_F5;
	case 140:		return K_F6;
	case 141:		return K_F7;
	case 142:		return K_F8;
	case 143:		return K_F9;
	case 144:		return K_F10;
	case 145:		return K_F11;
	case 146:		return K_F12;
	case 147:		return K_INS;
	case 148:		return K_DEL;
	case 149:		return K_PGDN;
	case 150:		return K_PGUP;
	case 151:		return K_HOME;
	case 152:		return K_END;
	case 153:		return K_PAUSE;
	case 154:		return K_KP_NUMLOCK;
	case 155:		return K_CAPSLOCK; // woods #capslock
//	case 156:		return K_SCRLCK;
	case 157:		return K_KP_INS;
	case 158:		return K_KP_END;
	case 159:		return K_KP_DOWNARROW;
	case 160:		return K_KP_PGDN;
	case 161:		return K_KP_LEFTARROW;
	case 162:		return K_KP_5;
	case 163:		return K_KP_RIGHTARROW;
	case 164:		return K_KP_HOME;
	case 165:		return K_KP_UPARROW;
	case 166:		return K_KP_PGUP;
	case 167:		return K_KP_DEL;
	case 168:		return K_KP_SLASH;
	case 169:		return K_KP_STAR;
	case 170:		return K_KP_MINUS;
	case 171:		return K_KP_PLUS;
	case 172:		return K_KP_ENTER;
//	case 173:		return K_KP_EQUALS;
	case 174:		return K_PRINTSCREEN; // woods #printscreen

	case 512:		return K_MOUSE1;
	case 513:		return K_MOUSE2;
	case 514:		return K_MOUSE3;
	case 515:		return K_MWHEELUP;
	case 516:		return K_MWHEELDOWN;
	case 517:		return K_MOUSE4;
	case 518:		return K_MOUSE5;
//	case 519:		return K_MOUSE6;
//	case 520:		return K_MOUSE7;
//	case 521:		return K_MOUSE8;
//	case 522:		return K_MOUSE9;
//	case 523:		return K_MOUSE10;
//	case 524:		return K_MOUSE11;
//	case 525:		return K_MOUSE12;
//	case 526:		return K_MOUSE13;
//	case 527:		return K_MOUSE14;
//	case 528:		return K_MOUSE15;
//	case 529:		return K_MOUSE16;

	case 768:		return K_JOY1;
	case 769:		return K_JOY2;
	case 770:		return K_JOY3;
	case 771:		return K_JOY4;
//	case 772:		return K_JOY5;
//	case 773:		return K_JOY6;
//	case 774:		return K_JOY7;
//	case 775:		return K_JOY8;
//	case 776:		return K_JOY9;
//	case 777:		return K_JOY10;
//	case 778:		return K_JOY11;
//	case 779:		return K_JOY12;
//	case 780:		return K_JOY13;
//	case 781:		return K_JOY14;
//	case 782:		return K_JOY15;
//	case 783:		return K_JOY16;

	case 784:		return K_AUX1;
	case 785:		return K_AUX2;
	case 786:		return K_AUX3;
	case 787:		return K_AUX4;
	case 788:		return K_AUX5;
	case 789:		return K_AUX6;
	case 790:		return K_AUX7;
	case 791:		return K_AUX8;
	case 792:		return K_AUX9;
	case 793:		return K_AUX10;
	case 794:		return K_AUX11;
	case 795:		return K_AUX12;
	case 796:		return K_AUX13;
	case 797:		return K_AUX14;
	case 798:		return K_AUX15;
	case 799:		return K_AUX16;
	case 800:		return K_AUX17;
	case 801:		return K_AUX18;
	case 802:		return K_AUX19;
	case 803:		return K_AUX20;
	case 804:		return K_AUX21;
	case 805:		return K_AUX22;
	case 806:		return K_AUX23;
	case 807:		return K_AUX24;
	case 808:		return K_AUX25;
	case 809:		return K_AUX26;
	case 810:		return K_AUX27;
	case 811:		return K_AUX28;
	case 812:		return K_AUX29;
	case 813:		return K_AUX30;
	case 814:		return K_AUX31;
	case 815:		return K_AUX32;

	case 816:		return K_DPAD_UP;
	case 817:		return K_DPAD_DOWN;
	case 818:		return K_DPAD_LEFT;
	case 819:		return K_DPAD_RIGHT;
//	case 820:		return K_GP_START;
//	case 821:		return K_GP_BACK;
	case 822:		return K_LTHUMB;
	case 823:		return K_RTHUMB;
	case 824:		return K_LSHOULDER;
	case 825:		return K_RSHOULDER;
	case 826:		return K_ABUTTON;
	case 827:		return K_BBUTTON;
	case 828:		return K_XBUTTON;
	case 829:		return K_YBUTTON;
	case 830:		return K_LTRIGGER;
	case 831:		return K_RTRIGGER;
	case 840:		return K_MISC1;
	case 841:		return K_PADDLE1;
	case 842:		return K_PADDLE2;
	case 843:		return K_PADDLE3;
	case 844:		return K_PADDLE4;
	case 845:		return K_TOUCHPAD;
	case 846:		return K_LTHUMB_ALT;
	case 847:		return K_RTHUMB_ALT;
	case 848:		return K_LSHOULDER_ALT;
	case 849:		return K_RSHOULDER_ALT;
	case 850:		return K_DPAD_UP_ALT;
	case 851:		return K_DPAD_DOWN_ALT;
	case 852:		return K_DPAD_LEFT_ALT;
	case 853:		return K_DPAD_RIGHT_ALT;
	case 854:		return K_ABUTTON_ALT;
	case 855:		return K_BBUTTON_ALT;
	case 856:		return K_XBUTTON_ALT;
	case 857:		return K_YBUTTON_ALT;
	case 858:		return K_LTRIGGER_ALT;
	case 859:		return K_RTRIGGER_ALT;
	case 860:		return K_MISC1_ALT;
	case 861:		return K_PADDLE1_ALT;
	case 862:		return K_PADDLE2_ALT;
	case 863:		return K_PADDLE3_ALT;
	case 864:		return K_PADDLE4_ALT;
	case 865:		return K_TOUCHPAD_ALT;
//	case 832:		return K_GP_LEFT_THUMB_UP;
//	case 833:		return K_GP_LEFT_THUMB_DOWN;
//	case 834:		return K_GP_LEFT_THUMB_LEFT;
//	case 835:		return K_GP_LEFT_THUMB_RIGHT;
//	case 836:		return K_GP_RIGHT_THUMB_UP;
//	case 837:		return K_GP_RIGHT_THUMB_DOWN;
//	case 838:		return K_GP_RIGHT_THUMB_LEFT;
//	case 839:		return K_GP_RIGHT_THUMB_RIGHT;
	default:
		//ascii chars are mapped as-is (yes this means upper-case keys don't get used).
		if (code >= 0 && code < 127)
			return code;
		else if (code < 0)
		{
			code = -code;
			if (code < 0 || code >= MAX_KEYS)
				code = -1;	//was invalid somehow... don't crash anything.
			return code;	//qc doesn't have extended keys available to it. so map negative keys back to native ones.
		}
		else
			return -code;	//this qc keycode has no native equivelent. use negatives, because we can.
	}
}

/*
==============================================================================

			LINE TYPING INTO THE CONSOLE

==============================================================================
*/

qboolean CheckForCommand(void)  // woods added for don't have to type "say " every time you wanna say something #ezsay (joequake)
{
	char* s, command[256];

	Q_strncpy(command, key_lines[edit_line] + 1, sizeof(command));
	for (s = command; *s > ' '; s++)
		;
	*s = 0;

	return (Cvar_FindVar(command) || Cmd_Exists2(command) || Cmd_AliasExists(command));
}

static void AdjustConsoleHeight(int delta) // woods (Qrack) by joe, from ZQuake
{
	extern	cvar_t	scr_consize;
	int		height;

	if (!cl.worldmodel || cls.signon != SIGNONS)
		return;
	height = (scr_consize.value * vid.height + delta + 5) / 10;
	height *= 10;
	if (delta < 0 && height < 30)
		height = 30;
	if (delta > 0 && height > vid.height - 10)
		height = vid.height - 10;
	Cvar_SetValue("scr_consize", (float)height / vid.height);
}

void Key_Extra (int* key) // woods #namemaker
{
	if (Key_IsShortcutModifierDown())
	{
		if (*key >= '0' && *key <= '9')
		{
			*key = *key - '0' + 0x12;	// yellow number
		}
		else
		{
			switch (*key)
			{
			case '[': *key = 0x10; break;
			case ']': *key = 0x11; break;
			case 'g': *key = 0x86; break;
			case 'r': *key = 0x87; break;
			case 'y': *key = 0x88; break;
			case 'b': *key = 0x89; break;
			case '(': *key = 0x80; break;
			case '=': *key = 0x81; break;
			case ')': *key = 0x82; break;
			case 'a': *key = 0x83; break;
			case '<': *key = 0x1d; break;
			case '-': *key = 0x1e; break;
			case '>': *key = 0x1f; break;
			case ',': *key = 0x1c; break;
			case '.': *key = 0x9c; break;
			case 'B': *key = 0x8b; break;
			case 'C': *key = 0x8d; break;
			}
		}
	}

	if (keydown[K_ALT])
		*key |= 0x80;		// red char
}

static void PasteToConsole (void)
{
	char *cbd, *p, *workline;
	int mvlen, inslen;

	if (key_linepos == MAXCMDLINE - 1)
		return;

	if ((cbd = PL_GetClipboardData()) == NULL)
		return;

	p = cbd;
	while (*p)
	{
		if (*p == '\n' || *p == '\r' || *p == '\b')
		{
			*p = 0;
			break;
		}
		p++;
	}

	inslen = (int) (p - cbd);
	if (inslen + key_linepos > MAXCMDLINE - 1)
		inslen = MAXCMDLINE - 1 - key_linepos;
	if (inslen <= 0) goto done;

	workline = key_lines[edit_line];
	workline += key_linepos;
	mvlen = (int) strlen(workline);
	if (mvlen + inslen + key_linepos > MAXCMDLINE - 1)
	{
		mvlen = MAXCMDLINE - 1 - key_linepos - inslen;
		if (mvlen < 0) mvlen = 0;
	}

	// insert the string
	if (mvlen != 0)
		memmove (workline + inslen, workline, mvlen);
	memcpy (workline, cbd, inslen);
	key_linepos += inslen;
	workline[mvlen + inslen] = '\0';
  done:
	Z_Free(cbd);
}

void Char_Console2(int key) // woods #ezsay add leading space for mode 2
{
	char* workline = key_lines[edit_line];
	int max;

	if (cl_chatmode.value && (cls.state == ca_connected && cl.gametype == GAME_DEATHMATCH))
		if ((cl.modtype == 1) || (cl.modtype == 4))
			max = MAX_CHAT_SIZE_EX;
		else
			max = MAX_CHAT_SIZE;
	else
		max = MAXCMDLINE;
	if (key_linepos < max) // woods limit chat to 45 server limit  #chatlimit
	{
		qboolean endpos = !workline[key_linepos];

		
		{
			workline += key_linepos;
			*workline = key;
			// null terminate if at the end
			if (endpos)
				workline[1] = 0;
		}
		key_linepos++;
	}
}

void Word_Delete (void) // woods from ezquake
{
	size_t len = 0;

	while (key_linepos > 1 && key_lines[edit_line][key_linepos - 1] == ' ')
	{
		key_linepos--;
		len++;
	}

	while (key_linepos > 1 && key_lines[edit_line][key_linepos - 1] != ' ')
	{
		key_linepos--;
		len++;
	}

	// remove spaces after this word leaving only last one
	while (key_linepos < strlen(key_lines[edit_line]) && key_lines[edit_line][key_linepos - 1] == ' ' && key_lines[edit_line][key_linepos - 2] == ' ')
	{
		key_linepos--;
		len++;
	}

	memmove(key_lines[edit_line] + key_linepos, key_lines[edit_line] + key_linepos + len, strlen(key_lines[edit_line] + key_linepos + len) + 1);
}

static qboolean Key_IsWordSeparator(char c) // woods (ironwail)
{
	switch (c)
	{
	case ' ':
	case '_':
	case '\t':
	case ';':
		return true;
	default:
		return false;
	}
}

static int Key_FindWordBoundary(int dir) // woods (ironwail)
{
	char* workline = key_lines[edit_line];
	int		len = (int)strlen(workline);
	int		pos = key_linepos;

	if (dir < 0)
	{
		while (pos > 1 && Key_IsWordSeparator(workline[pos - 1]))
			pos--;
		while (pos > 1 && !Key_IsWordSeparator(workline[pos - 1]))
			pos--;
	}
	else
	{
		while (pos < len && !Key_IsWordSeparator(workline[pos]))
			pos++;
		while (pos < len && Key_IsWordSeparator(workline[pos]))
			pos++;
	}

	return pos;
}

/*
====================
Key_Console -- johnfitz -- heavy revision

Interactive line editing and console scrollback
====================
*/
extern	char *con_text, key_tabpartial[MAXCMDLINE];
extern	int con_current, con_linewidth, con_vislines;

/*
====================
Key_ConsoleQuitMistype -- woods #smartquit

If the line a user just typed looks like a fumbled "quit", ask before acting on
it.  Returns true when the line has been handled and must not be executed.
====================
*/
static qboolean Key_ConsoleQuitMistype (const char *line)
{
	char	word[64];
	size_t	n;

	while (*line == ' ' || *line == '\t')
		line++;
	for (n = 0; n < sizeof(word) - 1 && line[n] && line[n] != ' ' && line[n] != '\t'
		    && line[n] != ';' && line[n] != '\n'; n++)
		word[n] = line[n];
	word[n] = '\0';

	if (!n || !Cmd_IsQuitMistype (word))
		return false;

	if (SCR_ModalMessage (va("you typed: ^m%s^m\n\n do you want to quit? (^my^m/^mn^m)\n", word), 0.0f))
		Host_Quit_f ();
	return true;
}

/*
====================
Key_ConsoleCommitTabHint

The console's auto-hint is drawn after the cursor but is not part of
key_lines[].  Commit it when Enter is pressed so accepting a visible
completion has the same effect as pressing Tab first.
====================
*/
static void Key_ConsoleCommitTabHint (char *workline)
{
	size_t line_len, hint_len, copy_len;

	if (!key_tabhint[0])
		return;

	line_len = strlen(workline);
	if (key_linepos != line_len)
		return;

	hint_len = strlen(key_tabhint);
	copy_len = q_min(hint_len, (size_t)(MAXCMDLINE - 1) - line_len);
	if (!copy_len)
		return;

	memcpy(workline + line_len, key_tabhint, copy_len);
	workline[line_len + copy_len] = '\0';
	key_linepos += copy_len;
	key_tabhint[0] = '\0';
	key_tabpartial[0] = '\0';
}

void Key_Console (int key)
{
	static	char current[MAXCMDLINE] = "";
	int	history_line_last;
	size_t		len;
	char *workline = key_lines[edit_line];
	qboolean	chatprefixed = false;

	switch (key)
	{
	case K_ENTER:
		if (cls.state == ca_connected && !CheckForCommand() && (cl_chatmode.value == 1 || cl_chatmode.value == 2 || (cl_chatmode.value == 3 && key_lines[edit_line][1] == ' '))) // woods don't have to type "say " every time you wanna say something #ezsay (joequake)
		{
				Cbuf_AddText("say ");
				key_tabhint[0] = '\0';
				chatprefixed = true;
		}
		// K_ABUTTON shares enter behavior, but skips the chat shortcut branch above.
	case K_ABUTTON:
	case K_KP_ENTER:
		Key_ConsoleCommitTabHint(workline);
		key_tabpartial[0] = 0;
		// woods -- #smartquit -- a human typed this line, so it is the one place
		// a mistyped "quit" may raise the confirmation prompt.  Doing it deeper,
		// in Cmd_ExecuteString, cannot tell us apart from an alias body or a
		// server stufftext, both of which reach the buffer as src_command.
		if (!chatprefixed && Key_ConsoleQuitMistype (workline + 1))
			return;
		Cbuf_AddText (workline + 1);	// skip the prompt
		Cbuf_AddText ("\n");
		Con_Printf ("%s\n", workline);

		if (History_SaveHistoryEnabled())
		{
			// If the last two lines are identical, skip storing this line in history
			// by not incrementing edit_line
			if (strcmp(workline, key_lines[(edit_line - 1) & (CMDLINES - 1)]))
				edit_line = (edit_line + 1) & (CMDLINES - 1);
		}

		history_line = edit_line;
		key_lines[edit_line][0] = ']';
		key_lines[edit_line][1] = 0; //johnfitz -- otherwise old history items show up in the new edit line
		key_linepos = 1;
		key_tabhint[0] = '\0'; // woods #iwtabcomplete
		if (cls.state == ca_disconnected)
			SCR_UpdateScreen (); // force an update, because the command may take some time
		if (cl_chatmode.value == 2 || cl_chatmode.value == 3) // woods #ezsay add leading space for mode 2
			Char_Console2(32);
		return;

	case K_TAB:
		Con_TabComplete (TABCOMPLETE_USER); // woods #iwtabcomplete
		return;

	case K_BACKSPACE: // woods #iwtabcomplete
		key_tabpartial[0] = 0;
		if (key_linepos > 1)
		{
			int numchars = Key_IsShortcutModifierDown() ? key_linepos - Key_FindWordBoundary(-1) : 1;
			SDL_assert (numchars > 0);
			workline += key_linepos - numchars;
			len = strlen(workline);
			SDL_assert ((int)len >= numchars);
			memmove (workline, workline + numchars, len + 1 - numchars);
			key_linepos -= numchars;
			Con_TabComplete (TABCOMPLETE_AUTOHINT);
		}
		return;

	case K_DEL: // woods #iwtabcomplete
		key_tabpartial[0] = 0;
		workline += key_linepos;
		if (*workline)
		{
			int numchars = Key_IsShortcutModifierDown() ? Key_FindWordBoundary(1) - key_linepos : 1;
			SDL_assert(numchars > 0);
			len = strlen(workline);
			SDL_assert ((int)len >= numchars);
			memmove (workline, workline + numchars, len + 1 - numchars);
			Con_TabComplete (TABCOMPLETE_AUTOHINT);
		}
		return;

	case K_HOME:
		if (Key_IsShortcutModifierDown())
		{
			//skip initial empty lines
			int i, x;
			char *line;

			for (i = con_current - con_totallines + 1; i <= con_current; i++)
			{
				line = con_text + (i % con_totallines) * con_linewidth;
				for (x = 0; x < con_linewidth; x++)
				{
					if (line[x] != ' ')
						break;
				}
				if (x != con_linewidth)
					break;
			}
			con_backscroll = CLAMP(0, con_current-i+1, con_totallines-(glheight>>3)-1); // woods
		}
		else	key_linepos = 1;
		Con_TabComplete (TABCOMPLETE_AUTOHINT); // woods #iwtabcomplete
		return;

	case K_END:
		if (Key_IsShortcutModifierDown())
			con_backscroll = 0;
		else	key_linepos = strlen(workline);
		Con_TabComplete (TABCOMPLETE_AUTOHINT); // woods #iwtabcomplete
		return;

	case K_PGUP:
	case K_MWHEELUP:
		con_backscroll += Key_IsShortcutModifierDown() ? ((con_vislines>>3) - 4) : 2;
		if (con_backscroll > con_totallines - (vid.height>>3) - 1)
			con_backscroll = con_totallines - (vid.height>>3) - 1;
		return;

	case K_PGDN:
	case K_MWHEELDOWN:
		con_backscroll -= Key_IsShortcutModifierDown() ? ((con_vislines>>3) - 4) : 2;
		if (con_backscroll < 0)
			con_backscroll = 0;
		return;

	case K_LEFTARROW:
	case K_DPAD_LEFT:
		if (keydown[K_SHIFT]) { // woods #conselection - extend selection left
			Con_MoveSelection(-1, 0);
			return;
		}
		if (key_linepos > 1) // woods (ironwail) support for shortcut+left/right per word
		{
			if (Key_IsShortcutModifierDown())
				key_linepos = Key_FindWordBoundary(-1);
			else
				key_linepos--;
			}
		key_blinktime = realtime;
		Con_TabComplete(TABCOMPLETE_AUTOHINT); // woods #iwtabcomplete
		return;

	case K_RIGHTARROW:
	case K_DPAD_RIGHT:
		if (keydown[K_SHIFT]) { // woods #conselection - extend selection right
			Con_MoveSelection(1, 0);
			return;
		}
		len = strlen(workline);
		if ((int)len == key_linepos)
		{
			len = strlen(key_lines[(edit_line + (CMDLINES - 1)) & (CMDLINES - 1)]);
			if ((int)len <= key_linepos)
				return; // no character to get
			workline += key_linepos;
			*workline = key_lines[(edit_line + (CMDLINES - 1)) & (CMDLINES - 1)][key_linepos];
			workline[1] = 0;
			key_linepos++;
		}
		else
		{
			if (Key_IsShortcutModifierDown()) // woods (ironwail)
				key_linepos = Key_FindWordBoundary(1);
			else
				key_linepos++;
			key_blinktime = realtime;
		}
		Con_TabComplete(TABCOMPLETE_AUTOHINT); // woods #iwtabcomplete
		return;

	case K_UPARROW:
	case K_DPAD_UP:
		if (keydown[K_SHIFT]) { // woods #conselection - extend selection up
			Con_MoveSelection(0, 1); // +1 because higher line numbers are older
			return;
		}
		if (Key_IsShortcutModifierDown()) // woods (qrack)
		{
			AdjustConsoleHeight(-10);
			return;
		}

		if (history_line == edit_line)
			Q_strcpy(current, workline);

		history_line_last = history_line;
		do
		{
			history_line = (history_line - 1) & (CMDLINES - 1);
		} while (history_line != edit_line && !key_lines[history_line][1]);

		if (history_line == edit_line)
		{
			history_line = history_line_last;
			return;
		}

		key_tabpartial[0] = 0;
		len = strlen(key_lines[history_line]);
		memmove(workline, key_lines[history_line], len+1);
		key_linepos = (int)len;
		Con_TabComplete(TABCOMPLETE_AUTOHINT); // woods #iwtabcomplete
		return;

	case K_DOWNARROW:
	case K_DPAD_DOWN:
		if (keydown[K_SHIFT]) { // woods #conselection - extend selection down
			Con_MoveSelection(0, -1); // -1 because lower line numbers are newer
			return;
		}
		if (Key_IsShortcutModifierDown()) // woods (qrack)
		{
			AdjustConsoleHeight(10);
			return;
		}

		if (history_line == edit_line)
			return;

		key_tabpartial[0] = 0;

		do
		{
			history_line = (history_line + 1) & (CMDLINES - 1);
		} while (history_line != edit_line && !key_lines[history_line][1]);

		if (history_line == edit_line)
		{
			len = strlen(current);
			memcpy(workline, current, len+1);
		}
		else
		{
			len = strlen(key_lines[history_line]);
			memmove(workline, key_lines[history_line], len+1);
		}
		key_linepos = (int)len;
		Con_TabComplete (TABCOMPLETE_AUTOHINT); // woods #iwtabcomplete
		return;

	case K_INS:
		if (keydown[K_SHIFT])		/* Shift-Ins paste */
			PasteToConsole();
		else	key_insert ^= 1;
		Con_TabComplete (TABCOMPLETE_AUTOHINT); // woods #iwtabcomplete
		return;

	case 'U':
	case 'u':
		if (Key_IsShortcutModifierDown()) {
			key_lines[edit_line][1] = 0;	// woods clear all line typing (Qrack)
			key_linepos = 1;
			Con_TabComplete (TABCOMPLETE_AUTOHINT); // woods #iwtabcomplete
			return;
		}
		break;

	case 'v':
	case 'V':
		if (Key_IsShortcutModifierDown()) {
			PasteToConsole();
			Con_TabComplete (TABCOMPLETE_AUTOHINT); // woods #iwtabcomplete
			return;
		}
		break;

	case 'a': // woods #consolecursor
	case 'A':
		if (Key_IsShortcutModifierDown()) { Con_SelectAll(); return; }
		break;

	case 'c':
	case 'C':
		if (key_dest == key_console && Key_IsShortcutModifierDown())
		{
			Con_Copy_f();
			return;
		}
		break;
	case 'd':
	case 'D':
		if (Key_IsShortcutModifierDown()) { // woods switched to D, from C #concopy
			Con_Printf ("%s\n", workline);
			workline[0] = ']';
			workline[1] = 0;
			key_linepos = 1;
			history_line= edit_line;
			Con_TabComplete (TABCOMPLETE_AUTOHINT); // woods #iwtabcomplete
			return;
		}
		break;
	}
}

/*
====================
woods #chatinfo -- deletct chat typing and set userinfo chat key, with a 3 second delay to set it back to 0
====================
*/

int chat_setinfo_defer = 0; // woods #chatinfo -- main-thread deferred-call handle (replaces SDL timer)
#define CHAT_TIMER_DELAY 3.0 // seconds -- delay before resetting chat userinfo to 0

void ExecuteSetInfoChat()
{
	if (cl_chatmode.value)
		SetChatInfo (0);
}

static void ChatSetInfo_Deferred(void *param) // woods #chatinfo -- runs on the main thread via Host_DeferCall
{
	chat_setinfo_defer = 0; // the deferred call has run
	ExecuteSetInfoChat();
}

void Char_Console(int key) // woods -- added detection for when typing in console to set chat to 1 for multiplayer games
{
	size_t		len;
	char *workline = key_lines[edit_line];
	int max;

	if (cl_chatmode.value) // woods #chatinfo
	{
		char tmp[3];
		int chat;

		Info_GetKey(cls.userinfo, "chat", tmp, sizeof(tmp));
		chat = atoi(tmp);

		if (chat == 0)
			SetChatInfo(CIF_CHAT);
	}

	if (cl_chatmode.value && (cls.state == ca_connected && cl.gametype == GAME_DEATHMATCH))
		if ((cl.modtype == 1) || (cl.modtype == 4))
			max = MAX_CHAT_SIZE_EX;
		else
			max = MAX_CHAT_SIZE;
	else
		max = MAXCMDLINE;
	if (key_linepos < max) // woods limit chat to 45 server limit  #chatlimit
	{
		qboolean endpos = !workline[key_linepos];

		key_tabpartial[0] = 0; //johnfitz
		// if inserting, move the text to the right
		if (key_insert && !endpos)
		{
			workline[MAXCMDLINE - 2] = 0;
			workline += key_linepos;
			len = strlen(workline) + 1;
			if (len > MAXCMDLINE-2)
				len = MAXCMDLINE-2;
			#if defined(__GNUC__) && (__GNUC__ > 7)
			#pragma GCC diagnostic ignored "-Warray-bounds"
			#endif
			memmove (workline + 1, workline, len);
			#if defined(__GNUC__) && (__GNUC__ > 7)
			#pragma GCC diagnostic pop
			#endif
			*workline = key;
		}
		else
		{
			workline += key_linepos;
			*workline = key;
			// null terminate if at the end
			if (endpos)
				workline[1] = 0;
		}
		key_linepos++;

		Con_TabComplete (TABCOMPLETE_AUTOHINT);
	}

	if (cl_chatmode.value) // woods #chatinfo -- delay before setting chat to 0
	{
		// restart the countdown on each keystroke (cancel is a safe no-op if already fired)
		Host_CancelDeferredCall(chat_setinfo_defer);
		chat_setinfo_defer = Host_DeferCall(CHAT_TIMER_DELAY, ChatSetInfo_Deferred, NULL);
	}
}

//============================================================================

qboolean	chat_team = false;
static char	chat_buffer[MAX_CHAT_SIZE_EX]; // woods limit chat to 100 server limit (legacy is 45)  #chatlimit
static int	chat_bufferlen = 0;
static int	chat_cursorpos = 0;
static int	chat_selection_anchor = -1;
#define CHAT_HISTORY_LINES 32
#define CHAT_HISTORY_PAGE_LINES 5
static char chat_history[CHAT_HISTORY_LINES][MAX_CHAT_SIZE_EX];
static int chat_history_count = 0;
static int chat_history_line = -1;
static char chat_history_saved_current[MAX_CHAT_SIZE_EX];

static void Chat_HistoryResetBrowse (void);

const char *Key_GetChatBuffer (void)
{
	return chat_buffer;
}

int Key_GetChatMsgLen (void)
{
	return chat_bufferlen;
}

int Key_GetChatCursorPos (void)
{
	return chat_cursorpos;
}

qboolean Key_GetChatSelection (int *start, int *end)
{
	int a, b;

	if (chat_selection_anchor < 0 || chat_selection_anchor == chat_cursorpos)
		return false;

	a = chat_selection_anchor;
	b = chat_cursorpos;
	if (a > b)
	{
		int t = a;
		a = b;
		b = t;
	}

	if (start)
		*start = a;
	if (end)
		*end = b;
	return true;
}

static void Chat_ClearSelection (void)
{
	chat_selection_anchor = -1;
}

static void Chat_ClampEditState (void)
{
	if (chat_bufferlen < 0)
		chat_bufferlen = 0;
	if (chat_bufferlen >= (int)sizeof(chat_buffer))
		chat_bufferlen = (int)sizeof(chat_buffer) - 1;
	if (chat_cursorpos < 0)
		chat_cursorpos = 0;
	if (chat_cursorpos > chat_bufferlen)
		chat_cursorpos = chat_bufferlen;
	if (chat_selection_anchor > chat_bufferlen)
		chat_selection_anchor = chat_bufferlen;
	if (chat_selection_anchor == chat_cursorpos)
		Chat_ClearSelection ();
}

static qboolean Chat_CommandOrCtrlDown (void)
{
	return Key_IsShortcutModifierDown();
}

static int Chat_FindWordBoundary (int pos, int dir)
{
	if (dir < 0)
	{
		while (pos > 0 && Key_IsWordSeparator (chat_buffer[pos - 1]))
			pos--;
		while (pos > 0 && !Key_IsWordSeparator (chat_buffer[pos - 1]))
			pos--;
	}
	else
	{
		while (pos < chat_bufferlen && !Key_IsWordSeparator (chat_buffer[pos]))
			pos++;
		while (pos < chat_bufferlen && Key_IsWordSeparator (chat_buffer[pos]))
			pos++;
	}

	return pos;
}

static void Chat_MoveCursor (int pos, qboolean selecting)
{
	pos = CLAMP(0, pos, chat_bufferlen);

	if (selecting)
	{
		if (chat_selection_anchor < 0)
			chat_selection_anchor = chat_cursorpos;
		chat_cursorpos = pos;
		if (chat_selection_anchor == chat_cursorpos)
			Chat_ClearSelection ();
	}
	else
	{
		chat_cursorpos = pos;
		Chat_ClearSelection ();
	}

	key_blinktime = realtime;
}

void Key_SetChatCursorPos (int pos, qboolean selecting)
{
	Chat_MoveCursor (pos, selecting);
}

static qboolean Chat_DeleteRange (int start, int end)
{
	if (start > end)
	{
		int t = start;
		start = end;
		end = t;
	}

	start = CLAMP(0, start, chat_bufferlen);
	end = CLAMP(0, end, chat_bufferlen);
	if (start == end)
		return false;

	memmove (chat_buffer + start, chat_buffer + end, chat_bufferlen - end + 1);
	chat_bufferlen -= end - start;
	chat_cursorpos = start;
	Chat_ClearSelection ();
	Chat_HistoryResetBrowse ();
	key_blinktime = realtime;
	return true;
}

static qboolean Chat_DeleteSelection (void)
{
	int start, end;

	if (!Key_GetChatSelection (&start, &end))
		return false;
	return Chat_DeleteRange (start, end);
}

static qboolean Chat_CopySelectionToClipboard (void)
{
	int start, end, len;
	char copy[MAX_CHAT_SIZE_EX];

	if (!Key_GetChatSelection (&start, &end))
		return false;

	len = end - start;
	if (len <= 0)
		return false;
	if (len >= (int)sizeof(copy))
		len = (int)sizeof(copy) - 1;

	memcpy(copy, chat_buffer + start, len);
	copy[len] = 0;
	return SDL_SetClipboardText(copy) == 0;
}

static qboolean Chat_InsertText (const char *text, int len)
{
	int maxlen, avail;

	if (!text || len <= 0)
		return false;

	Chat_DeleteSelection ();

	maxlen = (int)sizeof(chat_buffer) - 1;
	avail = maxlen - chat_bufferlen;
	if (len > avail)
		len = avail;
	if (len <= 0)
		return false;

	memmove (chat_buffer + chat_cursorpos + len,
		chat_buffer + chat_cursorpos,
		chat_bufferlen - chat_cursorpos + 1);
	memcpy (chat_buffer + chat_cursorpos, text, len);
	chat_bufferlen += len;
	chat_cursorpos += len;
	Chat_ClearSelection ();
	Chat_HistoryResetBrowse ();
	key_blinktime = realtime;
	return true;
}

static void Chat_HistoryResetBrowse (void)
{
	chat_history_line = -1;
	chat_history_saved_current[0] = 0;
}

static void Chat_SetBuffer (const char *text)
{
	q_strlcpy (chat_buffer, text ? text : "", sizeof(chat_buffer));
	chat_bufferlen = (int)strlen(chat_buffer);
	chat_cursorpos = chat_bufferlen;
	Chat_ClearSelection ();
}

static void Chat_HistorySaveDraft (void)
{
	if (chat_history_line < 0)
		q_strlcpy(chat_history_saved_current, chat_buffer,
			sizeof(chat_history_saved_current));
}

static qboolean Chat_HistoryNormalize (const char *text, char *out, size_t out_size)
{
	const unsigned char *start;
	const unsigned char *end;
	size_t len = 0;

	if (!out || !out_size)
		return false;
	out[0] = 0;
	if (!text)
		return false;

	start = (const unsigned char *)text;
	while (*start && q_isspace(*start))
		start++;

	end = start + strlen((const char *)start);
	while (end > start && q_isspace(end[-1]))
		end--;

	if (end - start >= 2 && start[0] == '"' && end[-1] == '"')
	{
		start++;
		end--;
		while (start < end && q_isspace(*start))
			start++;
		while (end > start && q_isspace(end[-1]))
			end--;
	}

	while (start < end && len + 1 < out_size)
	{
		unsigned char c = *start++;

		/* The chat box replays history through say "..."; COM_Parse has no
		 * escape syntax for embedded quotes, so don't store unsafe entries. */
		if (c == '"')
			return false;
		if (c == '\r' || c == '\n' || c == '\b' || c == '\t')
			c = ' ';
		out[len++] = (char)c;
	}
	while (len > 0 && q_isspace((unsigned char)out[len - 1]))
		len--;
	out[len] = 0;

	return len > 0;
}

void Chat_HistoryStore (const char *text)
{
	char normalized[MAX_CHAT_SIZE_EX];

	if (!Chat_HistoryNormalize(text, normalized, sizeof(normalized)))
		return;

	Chat_HistoryResetBrowse ();
	if (chat_history_count > 0 &&
		!strcmp(chat_history[chat_history_count - 1], normalized))
		return;

	if (chat_history_count == CHAT_HISTORY_LINES)
	{
		memmove(chat_history, chat_history + 1,
			sizeof(chat_history[0]) * (CHAT_HISTORY_LINES - 1));
		chat_history_count--;
	}

	q_strlcpy(chat_history[chat_history_count], normalized,
		sizeof(chat_history[0]));
	chat_history_count++;
}

static void Chat_HistoryBrowse (int direction)
{
	int line;

	if (chat_history_count <= 0)
		return;

	if (direction < 0)
	{
		Chat_HistorySaveDraft ();
		line = chat_history_line < 0 ? -direction - 1 :
			chat_history_line - direction;
		if (line >= chat_history_count)
			line = chat_history_count - 1;
		chat_history_line = line;
	}
	else
	{
		if (chat_history_line < 0)
			return;
		line = chat_history_line - direction;
		if (line < 0)
		{
			Chat_SetBuffer(chat_history_saved_current);
			Chat_HistoryResetBrowse ();
			return;
		}
		chat_history_line = line;
	}

	Chat_SetBuffer(chat_history[chat_history_count - 1 - chat_history_line]);
}

static void Chat_HistoryOldest (void)
{
	if (chat_history_count <= 0)
		return;

	Chat_HistorySaveDraft ();
	chat_history_line = chat_history_count - 1;
	Chat_SetBuffer(chat_history[0]);
}

static void Chat_HistoryEnd (void)
{
	if (chat_history_line < 0)
		return;

	Chat_SetBuffer(chat_history_saved_current);
	Chat_HistoryResetBrowse ();
}

void Key_EndChat (void)
{
	key_dest = key_game;
	Chat_SetBuffer("");
	Chat_HistoryResetBrowse ();
	SetChatInfo (0); // woods #chatinfo
	IN_UpdateGrabs();
}

void PasteToMessage (void) // woods zircon (baker)
{
	char* cbd, * p;
	if ((cbd = PL_GetClipboardData()) != 0) 
	{
		int i;
		p = cbd;
		while (*p)
		{
			if (*p == '\r' && *(p + 1) == '\n')
			{
				*p++ = ';';
				*p++ = ' ';
				continue;
			}
			else if (*p == '\n' || *p == '\r' || *p == '\b')
			{
				*p++ = ';';
				continue;
			}
			p++;
		}

		i = (int)strlen (cbd);
		Chat_InsertText (cbd, i);
		Chat_ClampEditState ();
		Z_Free(cbd);
	}
}

void Key_Message (int key)
{
	switch (key)
	{
	case K_ENTER:
	case K_KP_ENTER:
		if (chat_team)
			Cbuf_AddText ("say_team \"");
		else
			Cbuf_AddText ("say \"");
		Cbuf_AddText(chat_buffer);
		Cbuf_AddText("\"\n");

		Key_EndChat ();
		return;

	case K_ESCAPE:
		Key_EndChat ();
		return;

	case K_UPARROW:
	case K_KP_UPARROW:
	case K_DPAD_UP:
		Chat_HistoryBrowse (-1);
		return;

	case K_DOWNARROW:
	case K_KP_DOWNARROW:
	case K_DPAD_DOWN:
		Chat_HistoryBrowse (1);
		return;

	case K_PGUP:
	case K_KP_PGUP:
		Chat_HistoryBrowse (-CHAT_HISTORY_PAGE_LINES);
		return;

	case K_PGDN:
	case K_KP_PGDN:
		Chat_HistoryBrowse (CHAT_HISTORY_PAGE_LINES);
		return;

	case K_HOME:
	case K_KP_HOME:
		if (Key_IsShortcutModifierDown())
			Chat_HistoryOldest ();
		else
			Chat_MoveCursor (0, keydown[K_SHIFT]);
		return;

	case K_END:
	case K_KP_END:
		if (Key_IsShortcutModifierDown())
			Chat_HistoryEnd ();
		else
			Chat_MoveCursor (chat_bufferlen, keydown[K_SHIFT]);
		return;

	case K_LEFTARROW:
	case K_KP_LEFTARROW:
	case K_DPAD_LEFT:
		if (Chat_CommandOrCtrlDown ())
			Chat_MoveCursor (Chat_FindWordBoundary (chat_cursorpos, -1), keydown[K_SHIFT]);
		else
			Chat_MoveCursor (chat_cursorpos - 1, keydown[K_SHIFT]);
		return;

	case K_RIGHTARROW:
	case K_KP_RIGHTARROW:
	case K_DPAD_RIGHT:
		if (Chat_CommandOrCtrlDown ())
			Chat_MoveCursor (Chat_FindWordBoundary (chat_cursorpos, 1), keydown[K_SHIFT]);
		else
			Chat_MoveCursor (chat_cursorpos + 1, keydown[K_SHIFT]);
		return;

	case K_BACKSPACE:
		if (Chat_DeleteSelection ())
			return;
		if (Chat_CommandOrCtrlDown ()) // woods delete entire words
		{
			int startPos = chat_cursorpos;
			// move the cursor to the left, stopping at word boundaries
			while (startPos > 0 && Key_IsWordSeparator (chat_buffer[startPos - 1]))
				startPos--;
			while (startPos > 0 && !Key_IsWordSeparator (chat_buffer[startPos - 1]))
				startPos--;

			Chat_DeleteRange (startPos, chat_cursorpos);
		}
		else 
		{
			Chat_DeleteRange (chat_cursorpos - 1, chat_cursorpos);
		}
		return;

	case K_DEL:
	case K_KP_DEL:
		if (Chat_DeleteSelection ())
			return;
		if (Chat_CommandOrCtrlDown ())
			Chat_DeleteRange (chat_cursorpos, Chat_FindWordBoundary (chat_cursorpos, 1));
		else
			Chat_DeleteRange (chat_cursorpos, chat_cursorpos + 1);
		return;

	case 'U': // woods delete entire line
	case 'u':
		if (Chat_CommandOrCtrlDown ())
		{
			Chat_SetBuffer("");
			Chat_HistoryResetBrowse ();
			return;
		}
		break;

	case 'V':
	case 'v':
		if (Chat_CommandOrCtrlDown ())
		{
			PasteToMessage ();
			return;
		}
		break;

	case 'C':
	case 'c':
		if (Chat_CommandOrCtrlDown ())
		{
			Chat_CopySelectionToClipboard ();
			return;
		}
		break;

	case 'A':
	case 'a':
		if (Chat_CommandOrCtrlDown ())
		{
			chat_selection_anchor = 0;
			chat_cursorpos = chat_bufferlen;
			if (chat_bufferlen == 0)
				Chat_ClearSelection ();
			key_blinktime = realtime;
			return;
		}
		break;
	}
}

void Char_Message (int key)
{
	char text[2];

	text[0] = key;
	text[1] = 0;
	Chat_InsertText (text, 1);
}

//============================================================================


/*
===================
Key_StringToKeynum

Returns a key number to be used to index keybindings[] by looking at
the given string.  Single ascii characters return themselves, while
the K_* names are matched up.
===================
*/
int Key_StringToKeynum (const char *str)
{
	const keyname_t	*kn;

	if (!str || !str[0])
		return -1;
	if (!str[1])
		return str[0];

	for (kn=keynames ; kn->name ; kn++)
	{
		if (!q_strcasecmp(str,kn->name))
			return kn->keynum;
	}
	return -1;
}

/*
===================
Key_KeynumToString

Returns a string (either a single ascii char, or a K_* name) for the
given keynum.
FIXME: handle quote special (general escape sequence?)
===================
*/
const char *Key_KeynumToString (int keynum)
{
	static	char	tinystr[128][2]; // woods #iwtabcomplete
	const keyname_t	*kn;

	if (keynum == -1)
		return "<KEY NOT FOUND>";
	if (keynum > 32 && keynum < 127)
	{	// printable ascii
		tinystr[keynum][0] = q_tolower(keynum); // woods #iwtabcomplete
		tinystr[keynum][1] = 0; // woods #iwtabcomplete
		return tinystr[keynum]; // woods #iwtabcomplete
	}

	for (kn = keynames; kn->name; kn++)
	{
		if (keynum == kn->keynum)
			return kn->name;
	}

	return "<UNKNOWN KEYNUM>";
}

/*
===================
Key_KeynumToFriendlyString

Returns a user-facing string for the given keynum.
===================
*/
const char *Key_KeynumToFriendlyString (int keynum)
{
	if (keynum >= K_GAMEPAD_BEGIN && keynum < K_GAMEPAD_END)
	{
		const char *str = NULL;

		switch (IN_GetGamepadType())
		{
		default:
		case GAMEPAD_NONE:
		case GAMEPAD_XBOX:
			str = xbox_names[keynum - K_GAMEPAD_BEGIN];
			break;
		case GAMEPAD_PLAYSTATION:
			str = ps_names[keynum - K_GAMEPAD_BEGIN];
			break;
		case GAMEPAD_NINTENDO:
			str = nintendo_names[keynum - K_GAMEPAD_BEGIN];
			break;
		}

		if (str && *str)
			return str;
	}

	return Key_KeynumToString(keynum);
}

static const char *Key_GetEffectiveBinding (int keynum)
{
	if (keynum < 0 || keynum >= MAX_KEYS)
		return NULL;

	if (key_bindmap[0] >= 0 && key_bindmap[0] < MAX_BINDMAPS && keybindings[key_bindmap[0]][keynum])
		return keybindings[key_bindmap[0]][keynum];
	if (key_bindmap[1] >= 0 && key_bindmap[1] < MAX_BINDMAPS && keybindings[key_bindmap[1]][keynum])
		return keybindings[key_bindmap[1]][keynum];

	return NULL;
}

static qboolean Key_IsAltModifierBinding (const char *binding)
{
	return binding && !strcmp(binding, "+altmodifier");
}

static qboolean *Key_GetGamepadAltTranslatedSlot (int keynum)
{
	if (keynum < K_LTHUMB || keynum > K_TOUCHPAD)
		return NULL;

	return &key_gamepad_alttranslated[keynum - K_GAMEPAD_BEGIN];
}

/*
===================
Key_GetDeviceForKeynum
===================
*/
keydevice_t Key_GetDeviceForKeynum (int keynum)
{
	if (keynum < 0 || keynum >= MAX_KEYS)
		return KD_NONE;

	switch (keynum)
	{
	case K_MOUSE1:
	case K_MOUSE2:
	case K_MOUSE3:
	case K_MOUSE4:
	case K_MOUSE5:
	case K_MWHEELUP:
	case K_MWHEELDOWN:
		return KD_MOUSE;
	default:
		break;
	}

	if ((keynum >= K_JOY1 && keynum <= K_AUX32) ||
		(keynum >= K_GAMEPAD_BEGIN && keynum < K_GAMEPAD_END))
		return KD_GAMEPAD;

	return KD_KEYBOARD;
}

/*
===================
Key_GetDeviceMaskForKeynum
===================
*/
keydevicemask_t Key_GetDeviceMaskForKeynum (int keynum)
{
	keydevice_t device = Key_GetDeviceForKeynum(keynum);
	return device == KD_NONE ? KDM_NONE : (keydevicemask_t)(1 << (int)device);
}

/*
===================
Key_GetKeysForCommand
===================
*/
int Key_GetKeysForCommand (const char *command, int *keys, int maxkeys, keydevicemask_t devmask)
{
	int i, count;

	if (maxkeys <= 0)
		return 0;

	for (i = 0; i < maxkeys; i++)
		keys[i] = -1;
	count = 0;

	for (i = 0; i < MAX_KEYS; i++)
	{
		const char *binding = Key_GetEffectiveBinding(i);
		if (binding && !strcmp(binding, command))
		{
			if ((Key_GetDeviceMaskForKeynum(i) & devmask) == 0)
				continue;
			keys[count++] = i;
			if (count == maxkeys)
				break;
		}
	}

	return count;
}

/*
===================
Key_IsKeyGamepadAltModifier
===================
*/
qboolean Key_IsKeyGamepadAltModifier (int keynum)
{
	return Key_IsAltModifierBinding(Key_GetEffectiveBinding(keynum));
}

/*
===================
Key_GetGamepadAltModifierState
===================
*/
qboolean Key_GetGamepadAltModifierState (void)
{
	return key_gamepad_altmodifier_pressed;
}

static void Key_GamepadAltModifierDown (void)
{
	key_gamepad_altmodifier_pressed = true;
}

static void Key_GamepadAltModifierUp (void)
{
	key_gamepad_altmodifier_pressed = false;
}


/*
===================
Key_SetBinding
===================
*/
void Key_SetBinding (int keynum, const char *binding, int bindmap)
{
	if (keynum == -1)
		return;
	if (bindmap < 0 || bindmap >= MAX_BINDMAPS)
		return;

// free old bindings
	if (keybindings[bindmap][keynum])
	{
		Z_Free (keybindings[bindmap][keynum]);
		keybindings[bindmap][keynum] = NULL;
	}

// allocate memory for new binding
	if (binding)
		keybindings[bindmap][keynum] = Z_Strdup(binding);
}

/*
===================
Key_Unbind_f
===================
*/
void Key_Unbind_f (void)
{
	int	b;
	int keyarg = !strcmp(Cmd_Argv(0), "in_bind")?2:1;
	int bindmap = keyarg==2?atoi(Cmd_Argv(1)):0;
	if (bindmap < 0 || bindmap >= MAX_BINDMAPS)
		bindmap = 0;

	if (Cmd_Argc() != keyarg+1)
	{
		Con_Printf ("unbind <key> : remove commands from a key\n");
		return;
	}

	b = Key_StringToKeynum (Cmd_Argv(keyarg));
	if (b == -1)
	{
		Con_Printf ("\"%s\" isn't a valid key\n", Cmd_Argv(1));
		return;
	}

	Key_SetBinding (b, NULL, bindmap);
}

void Key_Unbindall_f (void)
{
	int	i, b;

	for (b = 0; b < MAX_BINDMAPS; b++)
	{
		for (i = 0; i < MAX_KEYS; i++)
		{
			if (keybindings[b][i])
				Key_SetBinding (i, NULL, b);
		}
	}
}

/*
============
Key_Bindlist_f -- johnfitz
============
*/
void Key_Bindlist_f (void)
{
	int	i, count;
	int bindmap = 0;

	count = 0;
	for (i = 0; i < MAX_KEYS; i++)
	{
		if (keybindings[bindmap][i] && *keybindings[bindmap][i])
		{
			Con_SafePrintf ("   %s \"%s\"\n", Key_KeynumToString(i), keybindings[bindmap][i]);
			count++;
		}
	}
	Con_SafePrintf ("%i bindings\n", count);
}

/*
============
Key_EditBind_f -- woods #editbind
============
*/
void Key_EditBind_f (void)
{
	int argc = Cmd_Argc();

	if (argc < 2) {
		Con_Printf("\n%s <key> : modify a bind\n", Cmd_Argv(0));
		Con_Printf("bindlist : list all binds\n\n");
		return;
	}

	// Determine if a bindmap is specified
	int keyarg = 1;
	int bindmap = 0;

	// Convert key string to keynum
	int keynum = Key_StringToKeynum(Cmd_Argv(keyarg));
	if (keynum == -1) 
	{
		Con_Printf("\"%s\" isn't a valid key\n\n", Cmd_Argv(keyarg));
		return;
	}

	const char* keybinding = (keybindings[bindmap][keynum] ? keybindings[bindmap][keynum] : ""); // Retrieve the current binding for the key

	char final_string[MAXCMDLINE]; 	// Construct the bind command string
	q_snprintf(final_string, sizeof(final_string), "bind \"%s\" \"%s\"", Cmd_Argv(keyarg), keybinding);

	if (edit_line < 0 || edit_line >= CMDLINES) // Ensure edit_line is within bounds
	{
		Con_Printf("\nedit line index out of bounds.\n\n");
		return;
	}

	key_lines[edit_line][0] = ']'; // Prompt character
	key_lines[edit_line][1] = '\0'; // Null terminate

	q_snprintf(key_lines[edit_line] + 1, MAXCMDLINE - 1, "%s", final_string);

	key_linepos = (int)strlen(key_lines[edit_line]); // Set key_linepos to the end of the line
}

/*
===================
Key_Bind_f
===================
*/
void Key_Bind_f (void)
{
	int	i, c, b;
	char	cmd[1024];
	int keyarg = !strcmp(Cmd_Argv(0), "in_bind")?2:1;
	int bindmap = keyarg==2?atoi(Cmd_Argv(1)):0;
	if (bindmap < 0 || bindmap >= MAX_BINDMAPS)
		bindmap = 0;

	c = Cmd_Argc();

	if (c < keyarg+1 )
	{
		Con_Printf("\n");
		Con_Printf ("bind <key> [command] : attach a command to a key\n");
		Con_Printf("\n");
		Con_Printf("example: bind \"e\" \"+forward\"\n");
		Con_Printf("\n");
		Con_Printf("TAB          KP_UPARROW      F5        MOUSE2       SEMICOLON\n");
		Con_Printf("ENTER        KP_PGUP         F6        MOUSE3       BACKQUOTE\n");
		Con_Printf("ESCAPE       KP_PLUS         F7        MOUSE4       TILDE\n");
		Con_Printf("SPACE        KP_LEFTARROW    F8        MOUSE5       LTHUMB\n");
		Con_Printf("BACKSPACE    KP_5            F9        JOY1         RTHUMB\n");
		Con_Printf("UPARROW      KP_RIGHTARROW   F10       JOY2         LSHOULDER\n");
		Con_Printf("DOWNARROW    KP_END          F11       JOY3         RSHOULDER\n");
		Con_Printf("LEFTARROW    KP_DOWNARROW    F12       JOY4         ABUTTON\n");
		Con_Printf("RIGHTARROW   KP_PGDN         INS       AUX1         BBUTTON\n");
		Con_Printf("CAPSLOCK     KP_ENTER        DEL       AUX2         XBUTTON\n");
		Con_Printf("ALT          KP_INS          PGDN      AUX3         YBUTTON\n");
		Con_Printf("CTRL         KP_DEL          PGUP      AUX4         LTRIGGER\n");
		Con_Printf("KP_SLASH     F1              HOME      AUX5         RTRIGGER\n");
		Con_Printf("KP_STAR      F2              END       PAUSE        PRINTSCREEN\n");
		Con_Printf("KP_MINUS     F3              COMMAND   MWHEELUP     \n");
		Con_Printf("KP_HOME      F4              MOUSE1    MWHEELDOWN   \n");
		Con_Printf("\n");
		Con_Printf("Gamepad extras: DPAD_UP DPAD_DOWN DPAD_LEFT DPAD_RIGHT MISC1\n");
		Con_Printf("PADDLE1 PADDLE2 PADDLE3 PADDLE4 TOUCHPAD *_ALT\n");
		Con_Printf("\n");
		return;
	}

	b = Key_StringToKeynum (Cmd_Argv(keyarg));
	if (b == -1)
	{
		Con_Printf ("\"%s\" isn't a valid key\n", Cmd_Argv(keyarg));
		return;
	}

	if (c == keyarg+1)
	{
		if (keybindings[bindmap][b])
			Con_Printf ("\"%s\" = \"%s\"\n", Cmd_Argv(keyarg), keybindings[bindmap][b] );
		else
			Con_Printf ("\"%s\" is not bound\n", Cmd_Argv(keyarg) );
		return;
	}

// copy the rest of the command line
	cmd[0] = 0;
	for (i = keyarg+1; i < c; i++)
	{
		q_strlcat (cmd, Cmd_Argv(i), sizeof(cmd));
		if (i != (c-1))
			q_strlcat (cmd, " ", sizeof(cmd));
	}

	Key_SetBinding (b, cmd, bindmap);
}

/*
============
Key_WriteBindings

Writes lines containing "bind key value"
============
*/
void Key_WriteBindings (FILE *f)
{
	int	i;
	int bindmap;

	// unbindall before loading stored bindings:
	if (cfg_unbindall.value)
		fprintf (f, "unbindall\n");
	for (bindmap = 0; bindmap < MAX_BINDMAPS; bindmap++)
	{
		for (i = 0; i < MAX_KEYS; i++)
		{
			if (keybindings[bindmap][i] && *keybindings[bindmap][i])
			{
				if (bindmap)
					fprintf (f, "in_bind %i \"%s\" \"%s\"\n", bindmap, Key_KeynumToString(i), keybindings[bindmap][i]);
				else
					fprintf (f, "bind \"%s\" \"%s\"\n", Key_KeynumToString(i), keybindings[bindmap][i]);
			}
		}
	}
}


static qboolean History_SaveHistoryEnabled (void)
{
	return con_savehistory.value != 0;
}

static qboolean History_GetPath (char *path, size_t path_size)
{
	int len;

	if (!path || !path_size)
		return false;

	path[0] = 0;
	if (!host_parms || !host_parms->userdir || !host_parms->userdir[0])
		len = q_snprintf(path, path_size, "%s", HISTORY_FILE_NAME);
	else
		len = q_snprintf(path, path_size, "%s/%s", host_parms->userdir, HISTORY_FILE_NAME);

	if (len < 0 || (size_t)len >= path_size)
	{
		path[0] = 0;
		return false;
	}

	return true;
}

static void History_ClearMemory (void)
{
	int i;

	for (i = 0; i < CMDLINES; i++)
	{
		key_lines[i][0] = ']';
		key_lines[i][1] = 0;
	}

	edit_line = 0;
	history_line = 0;
	key_linepos = 1;
	history_saved_current[0] = 0; // woods #serverhistory
}

static void History_RemoveFile (void)
{
	char path[MAX_OSPATH];

	if (History_GetPath(path, sizeof(path)))
		Sys_remove(path);
}

static void History_Clear (qboolean remove_file)
{
	History_ClearMemory();

	if (remove_file)
		History_RemoveFile();
}

static void History_Clear_f (void)
{
	History_Clear(true);
	Con_Printf("Console command history cleared.\n");
}

static void History_SaveHistory_Callback (cvar_t *var)
{
	if (!var->value)
		History_Clear(true);
}

static void History_RegisterControls (void)
{
	if (history_controls_registered)
		return;

	Cvar_RegisterVariable(&con_savehistory);
	Cvar_SetCallback(&con_savehistory, History_SaveHistory_Callback);
	Cmd_AddCommand("clearhistory", History_Clear_f);

	history_controls_registered = true;
}

qboolean History_IsSaving (void)
{
	History_RegisterControls();

	return History_SaveHistoryEnabled();
}

void History_Init (void)
{
	int i, c;
	FILE *hf;
	char path[MAX_OSPATH];

	History_RegisterControls();

	if (history_initialized) // woods #serverhistory
		return;

	history_initialized = true; // woods #serverhistory
	History_ClearMemory();

	if (!History_SaveHistoryEnabled())
	{
		History_RemoveFile();
		return;
	}

	if (!History_GetPath(path, sizeof(path)))
		return;

	hf = fopen(path, "rt");
	if (hf != NULL)
	{
		do
		{
			i = 1;
			do
			{
				c = fgetc(hf);
				key_lines[edit_line][i++] = c;
			} while (c != '\r' && c != '\n' && c != EOF && i < MAXCMDLINE);
			key_lines[edit_line][i - 1] = 0;
			edit_line = (edit_line + 1) & (CMDLINES - 1);
			/* for people using a windows-generated history file on unix: */
			if (c == '\r' || c == '\n')
			{
				do
					c = fgetc(hf);
				while (c == '\r' || c == '\n');
				if (c != EOF)
					ungetc(c, hf);
				else	c = 0; /* loop once more, otherwise last line is lost */
			}
		} while (c != EOF && edit_line < CMDLINES);
		fclose(hf);

		history_line = edit_line = (edit_line - 1) & (CMDLINES - 1);
		key_lines[edit_line][0] = ']';
		key_lines[edit_line][1] = 0;
	}
}

void History_Shutdown (void)
{
	int i;
	FILE *hf;
	char path[MAX_OSPATH];

	if (!history_initialized) // woods #serverhistory
		return;

	if (!History_SaveHistoryEnabled())
	{
		History_Clear(true);
		history_initialized = false; // woods #serverhistory
		return;
	}

	if (!History_GetPath(path, sizeof(path)))
	{
		history_initialized = false; // woods #serverhistory
		history_saved_current[0] = 0; // woods #serverhistory
		return;
	}

	hf = fopen(path, "wt");
	if (hf != NULL)
	{
		i = edit_line;
		do
		{
			i = (i + 1) & (CMDLINES - 1);
		} while (i != edit_line && !key_lines[i][1]);

		while (i != edit_line && key_lines[i][1])
		{
			fprintf(hf, "%s\n", key_lines[i] + 1);
			i = (i + 1) & (CMDLINES - 1);
		}
		fclose(hf);
	}

	history_initialized = false; // woods #serverhistory
	history_saved_current[0] = 0; // woods #serverhistory
}

void Print_History(void) // woods #shortcuts #history
{
	Cmd_ExecuteString("history -a\n", src_command);
	return;
}

void History_StoreCommand (const char *line) // woods #serverhistory
{
	char *workline;

	if (!history_initialized)
		return;

	if (!History_SaveHistoryEnabled())
		return;

	if (!line)
		line = "";

	if (!line[0])
	{
		history_line = edit_line;
		key_lines[edit_line][0] = ']';
		key_lines[edit_line][1] = 0;
		key_linepos = 1;
		history_saved_current[0] = 0;
		return;
}

	workline = key_lines[edit_line];
	workline[0] = ']';
	q_strlcpy (workline + 1, line, MAXCMDLINE - 1);

	if (strcmp(workline, key_lines[(edit_line - 1) & (CMDLINES - 1)]))
		edit_line = (edit_line + 1) & (CMDLINES - 1);

	history_line = edit_line;
	key_lines[edit_line][0] = ']';
	key_lines[edit_line][1] = 0;
	key_linepos = 1;
	history_saved_current[0] = 0;
}

qboolean History_GetPrevious (const char *current, char *out, size_t out_size) // woods #serverhistory
{
	int history_line_last;

	if (!history_initialized || !History_SaveHistoryEnabled() || !out || !out_size)
		return false;

	if (history_line == edit_line)
		q_strlcpy (history_saved_current, current ? current : "", sizeof(history_saved_current));

	history_line_last = history_line;
	do
	{
		history_line = (history_line - 1) & (CMDLINES - 1);
	} while (history_line != edit_line && !key_lines[history_line][1]);

	if (history_line == edit_line)
	{
		history_line = history_line_last;
		return false;
	}

	q_strlcpy (out, key_lines[history_line] + 1, out_size);
	return true;
}

qboolean History_GetNext (const char *current, char *out, size_t out_size) // woods #serverhistory
{
	if (!history_initialized || !History_SaveHistoryEnabled() || !out || !out_size)
		return false;

	(void)current;

	if (history_line == edit_line)
		return false;

	do
	{
		history_line = (history_line + 1) & (CMDLINES - 1);
	} while (history_line != edit_line && !key_lines[history_line][1]);

	if (history_line == edit_line)
		q_strlcpy (out, history_saved_current, out_size);
	else
		q_strlcpy (out, key_lines[history_line] + 1, out_size);

	return true;
}

/*
===================
Key_Init
===================
*/
void Key_Init (void)
{
	int	i;

	History_RegisterControls();
	History_Init ();

	key_blinktime = realtime; //johnfitz

//
// initialize consolekeys[]
//
	for (i = 32; i < 127; i++) // ascii characters
		consolekeys[i] = true;
	consolekeys['`'] = false;
	consolekeys['~'] = false;
	consolekeys[K_TAB] = true;
	consolekeys[K_ENTER] = true;
	consolekeys[K_ESCAPE] = true;
	consolekeys[K_BACKSPACE] = true;
	consolekeys[K_UPARROW] = true;
	consolekeys[K_DOWNARROW] = true;
	consolekeys[K_LEFTARROW] = true;
	consolekeys[K_RIGHTARROW] = true;
	consolekeys[K_DPAD_UP] = true;
	consolekeys[K_DPAD_DOWN] = true;
	consolekeys[K_DPAD_LEFT] = true;
	consolekeys[K_DPAD_RIGHT] = true;
	consolekeys[K_CTRL] = true;
	consolekeys[K_SHIFT] = true;
	consolekeys[K_INS] = true;
	consolekeys[K_DEL] = true;
	consolekeys[K_PGDN] = true;
	consolekeys[K_PGUP] = true;
	consolekeys[K_HOME] = true;
	consolekeys[K_END] = true;
	consolekeys[K_KP_NUMLOCK] = true;
	consolekeys[K_KP_SLASH] = true;
	consolekeys[K_KP_STAR] = true;
	consolekeys[K_KP_MINUS] = true;
	consolekeys[K_KP_HOME] = true;
	consolekeys[K_KP_UPARROW] = true;
	consolekeys[K_KP_PGUP] = true;
	consolekeys[K_KP_PLUS] = true;
	consolekeys[K_KP_LEFTARROW] = true;
	consolekeys[K_KP_5] = true;
	consolekeys[K_KP_RIGHTARROW] = true;
	consolekeys[K_KP_END] = true;
	consolekeys[K_KP_DOWNARROW] = true;
	consolekeys[K_KP_PGDN] = true;
	consolekeys[K_KP_ENTER] = true;
	consolekeys[K_KP_INS] = true;
	consolekeys[K_KP_DEL] = true;
#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC)
	consolekeys[K_COMMAND] = true;
#endif
	consolekeys[K_MWHEELUP] = true;
	consolekeys[K_MWHEELDOWN] = true;
	consolekeys[K_ABUTTON] = true;
	consolekeys[K_CAPSLOCK] = true; // woods #capslock
	consolekeys[K_PRINTSCREEN] = true; // woods #printscreen

//
// initialize menubound[]
//
	menubound[K_ESCAPE] = true;
	for (i = 0; i < 12; i++)
		menubound[K_F1+i] = true;

//
// register our functions
//
	Cmd_AddCommand ("bindlist",Key_Bindlist_f); //johnfitz
	Cmd_AddCommand ("bind",Key_Bind_f);
	Cmd_AddCommand ("bindedit", Key_EditBind_f); // woods #editbind
	Cmd_AddCommand ("unbind",Key_Unbind_f);
	Cmd_AddCommand ("unbindall",Key_Unbindall_f);

	Cmd_AddCommand ("in_bind",Key_Bind_f);	//spike -- purely for dp compat.
	Cmd_AddCommand ("in_unbind",Key_Unbind_f);	//spike -- purely for dp compat.
	Cmd_AddCommand ("+altmodifier", Key_GamepadAltModifierDown);
	Cmd_AddCommand ("-altmodifier", Key_GamepadAltModifierUp);
}

static struct {
	qboolean active;
	int lastkey;
	int lastchar;
} key_inputgrab = { false, -1, -1 };

/*
===================
Key_BeginInputGrab
===================
*/
void Key_BeginInputGrab (void)
{
	Key_ClearStates ();

	key_inputgrab.active = true;
	key_inputgrab.lastkey = -1;
	key_inputgrab.lastchar = -1;

	IN_UpdateInputMode ();
}

/*
===================
Key_EndInputGrab
===================
*/
void Key_EndInputGrab (void)
{
	Key_ClearStates ();

	key_inputgrab.active = false;

	IN_UpdateInputMode ();
}

/*
===================
Key_GetGrabbedInput
===================
*/
void Key_GetGrabbedInput (int *lastkey, int *lastchar)
{
	if (lastkey)
		*lastkey = key_inputgrab.lastkey;
	if (lastchar)
		*lastchar = key_inputgrab.lastchar;
}

qboolean Menu_HandleKeyEvent(qboolean down, int keyc, int unic)
{
	qboolean inhibit = false;
	if (key_dest != key_menu)
		;
	else if (cls.menu_qcvm.extfuncs.Menu_InputEvent)
	{
		PR_SwitchQCVM(&cls.menu_qcvm);
		G_FLOAT(OFS_PARM0) = down?CSIE_KEYDOWN:CSIE_KEYUP;
		G_VECTORSET(OFS_PARM1, Key_NativeToQC(keyc), 0, 0);	//x
		G_VECTORSET(OFS_PARM2, unic, 0, 0);	//y
		G_VECTORSET(OFS_PARM3, 0, 0, 0);	//devid
		PR_ExecuteProgram(cls.menu_qcvm.extfuncs.Menu_InputEvent);
		inhibit	 = G_FLOAT(OFS_RETURN);
		PR_SwitchQCVM(NULL);
	}
	else if (down?cls.menu_qcvm.extfuncs.m_keydown:cls.menu_qcvm.extfuncs.m_keyup)
	{
		int legacykey = keyc;

		switch (legacykey)
		{
		case K_DPAD_UP:		legacykey = K_UPARROW; break;
		case K_DPAD_DOWN:	legacykey = K_DOWNARROW; break;
		case K_DPAD_LEFT:	legacykey = K_LEFTARROW; break;
		case K_DPAD_RIGHT:	legacykey = K_RIGHTARROW; break;
		case K_ABUTTON:		legacykey = K_ENTER; break;
		case K_BBUTTON:		legacykey = K_ESCAPE; break;
		default:
			break;
		}

		PR_SwitchQCVM(&cls.menu_qcvm);
		G_FLOAT(OFS_PARM0) = Key_NativeToQC(legacykey);	//scancode
		G_FLOAT(OFS_PARM1) = unic;					//unicode
		PR_ExecuteProgram(down?cls.menu_qcvm.extfuncs.m_keydown:cls.menu_qcvm.extfuncs.m_keyup);
		inhibit	 = G_FLOAT(OFS_RETURN);
		PR_SwitchQCVM(NULL);
	}
	return inhibit;
}
qboolean CSQC_HandleKeyEvent(qboolean down, int keyc, int unic)
{
	qboolean inhibit = false;
	if (cl.qcvm.extfuncs.CSQC_InputEvent && (key_dest == key_game || !down))
	{
		PR_SwitchQCVM(&cl.qcvm);
		G_FLOAT(OFS_PARM0) = down?CSIE_KEYDOWN:CSIE_KEYUP;
		G_VECTORSET(OFS_PARM1, Key_NativeToQC(keyc), 0, 0);	//x
		G_VECTORSET(OFS_PARM2, unic, 0, 0);	//y
		G_VECTORSET(OFS_PARM3, 0, 0, 0);	//devid
		PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_InputEvent);
		inhibit	 = G_FLOAT(OFS_RETURN);
		PR_SwitchQCVM(NULL);
	}
	if (key_dest == key_game)
		return inhibit;
	return false;
}

/*
===================
Key_Event

Called by the system between frames for both key up and key down events
Should NOT be called during an interrupt!
===================
*/
void Key_Event (int key, qboolean down)
{
	Key_EventWithKeycode (key, down, 0);
}

/*
===================
Key_EventWithKeycode

Called by the system between frames for both key up and key down events
Should NOT be called during an interrupt!
keycode parameter should have the key's actual keycode using the current keyboard layout,
not necessarily the US-keyboard-based scancode. Pass 0 if not applicable.
===================
*/
void Key_EventWithKeycode (int key, qboolean down, int keycode)
{
	const char	*kb;
	char	cmd[1024];
	qboolean wasdown;  // woods #printscreen

	if (key < 0 || key >= MAX_KEYS)
		return;

	if (key_dest == key_message && key == K_MOUSE1 && (down || !keydown[key]))
	{
		Con_ChatMouseButton (down);
		return;
	}

    /* woods #conselection Swallow left+middle click ONLY while the console is active.
       Do NOT swallow right-click (K_MOUSE2) so it can toggle the menu
       from the console. Do NOT swallow wheel so it can be used in game. */
    if (key_dest == key_console &&
        (key == K_MOUSE1 || key == K_MOUSE3)) {
        /* Do not update keydown[] here; console uses SDL_GetMouseState().
           Returning early prevents weapon fires/uses/etc. */
        return;
    }

	// woods #touchpadmenu: the gamepad touchpad click selects the hovered menu
	// item. Fold it into K_MOUSE1 at the source (before keydown[] is set) so the
	// menu's slider drags and release checks stay consistent. A latch keeps the
	// up event paired even if key_dest changes mid-press. Left as the bindable
	// K_TOUCHPAD in-game and while grabbing a bind.
	{
		extern qboolean bind_grab;
		extern cvar_t joy_enable, joy_touchpad;
		static qboolean touchpad_menu_click = false;

		if (key == K_TOUCHPAD)
		{
			if (down)
			{
				if (key_dest == key_menu && !bind_grab &&
					joy_enable.value && joy_touchpad.value)
				{
					touchpad_menu_click = true;
					key = K_MOUSE1;
				}
			}
			else if (touchpad_menu_click)
			{
				touchpad_menu_click = false;
				key = K_MOUSE1;
			}
		}
	}

	if (key == K_CTRL
#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC)
		|| key == K_COMMAND
#endif
		) // woods #saymodifier
	{
#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC)
		// Keep the modifier active if Ctrl and Command overlap and only one is released.
		ctrlpressed = down || keydown[key == K_CTRL ? K_COMMAND : K_CTRL];
#else
		ctrlpressed = down;
#endif
	}

#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC)
	/* The Cocoa monitor normally owns Command-Q, including its one-second hold
	 * UI. Some SDL paths deliver it directly to the engine, so cancel the
	 * native hold if Command itself is released before Q. */
	if (!down && key == K_COMMAND && command_q_active)
	{
		command_q_active = false;
		keydown['q'] = false;
		PL_CommandQEvent(false);
	}
#endif

// handle fullscreen toggle
	if (down && (key == K_ENTER || key == K_KP_ENTER) && keydown[K_ALT])
	{
		VID_Toggle();
		return;
	}

#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC) // woods for mac command-tab to exit fullscreen
	if (down && (key == K_TAB) && keydown[K_COMMAND])
	{
		VID_Minimize();
		return;
	}
#endif

#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC) // woods #shortcuts #history, "command-y", not h, is mac standard for history
	if (down && (key == 'y') && keydown[K_COMMAND])
	{
		Print_History();
		return;
	}

	if (down && key == ',' && keydown[K_COMMAND])
	{
		M_Menu_Options_f();
		return;
	}
#endif

	if (down && (key == 'h') && keydown[K_CTRL]) // woods #shortcuts #history
	{
		Print_History();
		return;
	}

	if (cls.download.active)
	{
		if (down && (key == '.') && Key_IsShortcutModifierDown()) // woods #shortcuts #stopdownload
		{
			Cbuf_AddText("stopdownload\n");
			return;
		}
	}

#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC) // woods #shortcuts
	if (down && key == 'q' && keydown[K_COMMAND])
	{
		if (!command_q_active)
		{
			command_q_active = true;
			PL_CommandQEvent(true);
		}
		keydown[key] = true;
		return;
	}
	if (!down && key == 'q' && command_q_active)
	{
		command_q_active = false;
		keydown[key] = false;
		PL_CommandQEvent(false);
		return;
	}
#endif

	if (!(scr_conscale.value > 11) || !(scr_sbarscale.value > 7)) // max clamp
	if (down && (key == K_MWHEELUP) && Key_IsShortcutModifierDown() && keydown[K_SHIFT]) // woods #shortcuts
	{
		Cmd_ExecuteString("inc scr_conscale 1\n", src_command);
		Cmd_ExecuteString("inc scr_sbarscale 1\n", src_command);
		Cmd_ExecuteString("inc scr_crosshairscale 1\n", src_command);
		return;
	}

	if (!(scr_conscale.value < -1) || !(scr_sbarscale.value < -1)) // min clamp
	if (down && (key == K_MWHEELDOWN) && Key_IsShortcutModifierDown() && keydown[K_SHIFT]) // woods #shortcuts
	{
		Cmd_ExecuteString("inc scr_conscale -1\n", src_command);
		Cmd_ExecuteString("inc scr_sbarscale -1\n", src_command);
		Cmd_ExecuteString("inc scr_crosshairscale -1\n", src_command);
		return;
	}

	if (sfxvolume.value <= 1) // min clamp
		if (down && (key == K_MWHEELUP) && (keydown[K_ALT] && keydown[K_SHIFT])) // woods #shortcuts
		{
			if (sfxvolume.value < 0.98) // Prevent going over 100%
			{
				Cmd_ExecuteString("inc volume .02\n", src_command);
			}
			else
				Cvar_SetValueQuick(&sfxvolume, 1.0f); // Set to exactly 100% if we would exceed it

			return;
		}

	if (sfxvolume.value >= 0)// min clamp
		if (down && (key == K_MWHEELDOWN) && (keydown[K_ALT] && keydown[K_SHIFT])) // woods #shortcuts
		{
			if (sfxvolume.value > 0.02) // Prevent going below 0%
			{
				Cmd_ExecuteString("inc volume -.02\n", src_command);
			}
			else
				Cvar_SetValueQuick(&sfxvolume, 0.0f); // Set to exactly 0% if we would go below it

			return;
		}

	if (down && (key == 'm') && Key_IsShortcutModifierDown()) // woods #usermute
	{
		Sound_Toggle_Mute_f();
		return;
	}

	// If the gamepad alt modifier is held, translate normal gamepad buttons
	// into their alternate-layer keycodes when an alt binding exists.
	if (key >= K_LTHUMB && key <= K_TOUCHPAD)
	{
		qboolean *alttranslated = Key_GetGamepadAltTranslatedSlot(key);

		if (alttranslated && !Key_IsKeyGamepadAltModifier(key))
		{
			if (!down)
			{
				if (*alttranslated)
					key += K_LTHUMB_ALT - K_LTHUMB;
				*alttranslated = false;
			}
			else
			{
				*alttranslated = false;
				if (key_gamepad_altmodifier_pressed)
				{
					int altkey = key + (K_LTHUMB_ALT - K_LTHUMB);

					if (Key_GetEffectiveBinding(altkey) != NULL || Key_GetEffectiveBinding(key) == NULL)
					{
						key = altkey;
						*alttranslated = true;
					}
				}
			}
		}
		else if (alttranslated && down)
		{
			*alttranslated = false;
		}
	}

// handle autorepeats and stray key up events
	if (down)
	{
		if (keydown[key])
		{
			if (key_dest == key_game && !con_forcedup)
				return; // ignore autorepeats in game mode
		}
	}
	else if (!keydown[key])
		return; // ignore stray key up events

	wasdown = keydown[key]; // woods #printscreen
	keydown[key] = down;

	if (key_inputgrab.active)
	{
		if (down)
		{
			key_inputgrab.lastkey = key;
			if (keycode > 0)
				key_inputgrab.lastchar = keycode;
		}
		return;
	}

	if (down && !wasdown && (key == 'v' || key == 'V') && Key_IsShortcutModifierDown() && IN_PasteClipboardFile())
		return;

	if (down && !wasdown && key_dest == key_game && (key == 'c' || key == 'C') && Key_IsShortcutModifierDown())
	{
		if (TexturePointer_Copy(keydown[K_SHIFT]))
			return;
	}

// handle escape specialy, so the user can never unbind it
	if (key == K_ESCAPE)
	{
		if (!down)
		{
			Menu_HandleKeyEvent(down, key, 0);
			CSQC_HandleKeyEvent(down, key, 0);	//Spike -- for consistency
			return;
		}

		if (CL_DemoScrubActive())
		{
			CL_DemoScrub_Cancel();
			IN_UpdateGrabs();
			return;
		}

		if (keydown[K_SHIFT])
		{	//shift+escape forces the console (without closing it again - use a regular escape to get rid fo it after, making it easier to type blind).
			if (key_dest != key_console)
				Con_ToggleConsole_f();
			return;
		}

		switch (key_dest)
		{
		case key_message:
			Key_Message (key);
			break;
		case key_menu:
			if (!Menu_HandleKeyEvent(down, key, 0))
				M_Keydown (key, wasdown);
			break;
		case key_game:
		case key_console:
			if (CSQC_HandleKeyEvent(down, key, 0))	//Spike -- CSQC needs to be able to intercept escape. Note that shift+escape will always give the console for buggy mods.
				break;
			M_ToggleMenu(1);
			break;
		default:
			Sys_Error ("Bad key_dest");
		}

		return;
	}

	if (key_dest == key_console) // woods mouse2 to get to menu from console
		if (down && (key == K_MOUSE2 || key == K_MOUSE4))
		{
			if (CSQC_HandleKeyEvent(down, key, 0))	//Spike -- CSQC needs to be able to intercept escape. Note that shift+escape will always give the console for buggy mods.
				return;
			M_ToggleMenu(1);
			return;
		}

	// if Print Screen isn't bound, take a screenshot // woods #printscreen (ironwail 1734367)

	if (key == K_PRINTSCREEN && !Key_GetEffectiveBinding(key))
	{
		if (down && !wasdown)
			Cbuf_AddText("screenshot\n");
		return;
	}

	// The native scoreboard owns navigation only while its most recent draw
	// established a scrollable viewport. The handler also pairs key releases.
	if (Sbar_HandleScoreboardKey(key, down))
		return;

	// Startup demo reels keep the classic behavior where most keys open the menu.
	if (cls.demoplayback && cls.demoreelplayback && !cl_demoreel_playback_controls.value &&
		down && key_dest == key_game && key != K_TAB &&
		(consolekeys[key] || key == K_DPAD_UP || key == K_DPAD_DOWN ||
		 key == K_DPAD_LEFT || key == K_DPAD_RIGHT))
	{
		M_ToggleMenu (1);
		return;
	}

	// demo controls -- woods (iw) #democontrols

	if (cls.demoplayback && key_dest == key_game)
	{
		if (down && (key == K_LEFTARROW || key == K_RIGHTARROW ||
			key == K_DPAD_LEFT || key == K_DPAD_RIGHT ||
			(key >= '1' && key <= '9')))
		{
			SCR_ShowDemoBar ();
		}

		if (key >= '1' && key <= '9')
		{
			if (down > wasdown)
				CL_DemoSeekPercent((key - '0') * 10.f);
			return;
		}

		switch (key)
		{
		case K_SPACE:
			// Pause
			if (down > wasdown)
				cls.demopaused = !cls.demopaused;
			return;

		case K_UPARROW:
		case K_DPAD_UP:
		case '.':
			// Resume/increase speed
			if (((key == K_UPARROW || key == K_DPAD_UP) || (key == '.' && keydown[K_SHIFT])) && down > wasdown)
			{
				if (!cls.demopaused)
					cls.basedemospeed = CLAMP(0.03125f, cls.basedemospeed * 2.f, 32.f);
				cls.demopaused = false;
			}
			return;

		case K_DOWNARROW:
		case K_DPAD_DOWN:
		case ',':
			// Decrease speed/pause
			if (((key == K_DOWNARROW || key == K_DPAD_DOWN) || (key == ',' && keydown[K_SHIFT])) && down > wasdown)
			{
				cls.basedemospeed *= 0.5f;
				if (cls.basedemospeed < 0.03125f)
				{
					cls.basedemospeed = 0.03125f;
					cls.demopaused = true;
				}
			}
			return;

		case K_LEFTARROW:
		case K_RIGHTARROW:
		case K_DPAD_LEFT:
		case K_DPAD_RIGHT:
		case K_CTRL:
#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC)
		case K_COMMAND:
#endif
			// Temporary modifiers: they don't perform their actions on up/down events, but are queried per frame instead
			// to avoid having to manage state transitions (e.g. pressing esc while still holding left arrow to rewind).
			return;

		case 'j':
		case 'l':
			// One-shot jump keys: CL_UpdateDemoSpeed handles edge detection and seek state.
			return;

		default:
			// Not a demo control key
			break;
		}
	}

	//Spike -- give menuqc a chance to handle (and swallow) key events.
	if ((key_dest == key_menu || !down) && Menu_HandleKeyEvent(down, key, 0))
		return;
	//Spike -- give csqc a chance to handle (and swallow) key events.
	if ((key_dest == key_game || !down) && CSQC_HandleKeyEvent(down, key, 0))
		return;

// key up events only generate commands if the game key binding is
// a button command (leading + sign).  These will occur even in console mode,
// to keep the character from continuing an action started before a console
// switch.  Button commands include the kenum as a parameter, so multiple
// downs can be matched with ups
	if (!down)
	{
		kb = Key_GetEffectiveBinding(key);
		if (kb && kb[0] == '+')
		{
			sprintf (cmd, "-%s %i\n", kb+1, key);
			Cbuf_AddText (cmd);
		}
		return;
	}
/* woods disabled this so I can see scores in demos
// during demo playback, most keys bring up the main menu
	if (cls.demoplayback && down && consolekeys[key] && key_dest == key_game && key != K_TAB)
	{
		M_ToggleMenu (1);
		return;
	}*/

	if (cls.demoplayback && down && consolekeys[key] && key_dest == key_game && (key == '.' || key == ',' || key == K_HOME || key == K_END || (key >= '0' && key <= '9'))) // woods(iw) #democontrols
		return;

// if not a consolekey, send to the interpreter no matter what mode is
	if ((key_dest == key_menu && menubound[key]) ||
	    (key_dest == key_console && !consolekeys[key]) ||
	    (key_dest == key_game && (!con_forcedup || !consolekeys[key])))
	{
		kb = Key_GetEffectiveBinding(key);
		if (kb)
		{
			if (kb[0] == '+')
			{	// button commands add keynum as a parm
				sprintf (cmd, "%s %i\n", kb, key);
				Cbuf_AddText (cmd);
			}
			else
			{
				Cbuf_AddText (kb);
				Cbuf_AddText ("\n");
			}
		}
		//else if (key >= 200)
			//Con_Printf ("%s is unbound, hit F4 to set.\n", Key_KeynumToString(key)); // woods, unecessary print spam
		return;
	}

	if (!down)
		return;		// other systems only care about key down events

	switch (key_dest)
	{
	case key_message:
		Key_Message (key);
		break;
	case key_menu:
		M_Keydown (key, wasdown);
		break;

	case key_game:
	case key_console:
		Key_Console (key);
		break;
	default:
		Sys_Error ("Bad key_dest");
	}
}

/*
===================
Char_Event

Called by the backend when the user has input a character.
===================
*/
void Char_Event (int key)
{
	if (key < 32 || key > 126)
		return;

	// SDL may deliver the text event after a binding has opened the console.
	// Honor the same binding-only characters as Key_Event so the console key
	// itself (normally ` or ~) cannot leak into the input line.
	if (key_dest == key_console && !consolekeys[key])
		return;

#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC)
	if (keydown[K_COMMAND])
		return;
#endif
	if (keydown[K_CTRL])
		return;

	if (key_inputgrab.active)
	{
		key_inputgrab.lastchar = key;
		return;
	}

	switch (key_dest)
	{
	case key_message:
		Char_Message (key);
		break;
	case key_menu:
		if (!Menu_HandleKeyEvent(true, 0, key))
			M_Charinput (key);
		break;
	case key_game:
		if (!con_forcedup)
			break;
		/* fallthrough */
	case key_console:
		Char_Console (key);
		break;
	default:
		break;
	}
}

/*
===================
Key_TextEntry
===================
*/
qboolean Key_TextEntry (void)
{
	if (key_inputgrab.active)
	{
		// This path is used for simple single-letter inputs (y/n prompts) that also
		// accept controller input, so we don't want an onscreen keyboard for this case.
		return false;
	}

	switch (key_dest)
	{
	case key_message:
		return true;
	case key_menu:
		return M_TextEntry();
	case key_game:
		// Don't return true even during con_forcedup, because that happens while starting a
		// game and we don't to trigger text input (and the onscreen keyboard on some devices)
		// during this.
		return false;
	case key_console:
		return true;
	default:
		return false;
	}
}

/*
===================
Key_ClearStates
===================
*/
void Key_ClearStates (void)
{
	int	i;

	for (i = 0; i < MAX_KEYS; i++)
	{
		if (keydown[i])
			Key_Event (i, false);
	}

	memset(key_gamepad_alttranslated, 0, sizeof(key_gamepad_alttranslated));
}

/*
===================
Key_UpdateForDest
===================
*/
void Key_UpdateForDest (void)
{
	static qboolean forced = false;
	static keydest_t last_dest = key_game;

	if (cls.state == ca_dedicated)
		return;

	switch (key_dest)
	{
	case key_console:
		if (forced && cls.state == ca_connected)
		{
			forced = false;
			key_dest = key_game;
			IN_UpdateGrabs();
		}
		break;
	case key_game:
		if (cls.state != ca_connected)
		{
			forced = true;
			key_dest = key_console;
			IN_UpdateGrabs();
			break;
		}
	/* fallthrough */
	default:
		forced = false;
		break;
	}

	if (key_dest != last_dest)
	{
		last_dest = key_dest;
		IN_UpdateGrabs();
	}
}
