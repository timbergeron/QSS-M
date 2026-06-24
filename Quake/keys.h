/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
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

#ifndef _QUAKE_KEYS_H
#define _QUAKE_KEYS_H

//
// gamepad button definitions
//
#define GAMEPAD_KEY_LIST(def) \
	def(K_START,         243, "MENU",             "OPTIONS",           "+") \
	def(K_BACK,          244, "VIEW",             "CREATE",            "-") \
	def(K_LTHUMB,        245, "LS",               "L3",                "LSB") \
	def(K_RTHUMB,        246, "RS",               "R3",                "RSB") \
	def(K_LSHOULDER,     247, "LB",               "L1",                "L") \
	def(K_RSHOULDER,     248, "RB",               "R1",                "R") \
	def(K_DPAD_UP,       249, "DPAD UP",          "DPAD UP",           "DPAD UP") \
	def(K_DPAD_DOWN,     250, "DPAD DOWN",        "DPAD DOWN",         "DPAD DOWN") \
	def(K_DPAD_LEFT,     251, "DPAD LEFT",        "DPAD LEFT",         "DPAD LEFT") \
	def(K_DPAD_RIGHT,    252, "DPAD RIGHT",       "DPAD RIGHT",        "DPAD RIGHT") \
	def(K_ABUTTON,       253, "A",                "X",                 "A") \
	def(K_BBUTTON,       254, "B",                "CIRCLE",            "B") \
	def(K_XBUTTON,       255, "X",                "SQUARE",            "X") \
	def(K_YBUTTON,       256, "Y",                "TRIANGLE",          "Y") \
	def(K_LTRIGGER,      257, "LT",               "L2",                "ZL") \
	def(K_RTRIGGER,      258, "RT",               "R2",                "ZR") \
	def(K_MISC1,         259, NULL,               "MUTE",              "CAPTURE") \
	def(K_PADDLE1,       260, "P1 PADDLE",        NULL,                NULL) \
	def(K_PADDLE2,       261, "P3 PADDLE",        NULL,                NULL) \
	def(K_PADDLE3,       262, "P2 PADDLE",        NULL,                NULL) \
	def(K_PADDLE4,       263, "P4 PADDLE",        NULL,                NULL) \
	def(K_TOUCHPAD,      264, NULL,               "TOUCHPAD",          NULL) \
	def(K_LTHUMB_ALT,    265, "LS (alt)",         "L3 (alt)",          "LSB (alt)") \
	def(K_RTHUMB_ALT,    266, "RS (alt)",         "R3 (alt)",          "RSB (alt)") \
	def(K_LSHOULDER_ALT, 267, "LB (alt)",         "L1 (alt)",          "L (alt)") \
	def(K_RSHOULDER_ALT, 268, "RB (alt)",         "R1 (alt)",          "R (alt)") \
	def(K_DPAD_UP_ALT,   269, "DPAD UP (alt)",    "DPAD UP (alt)",     "DPAD UP (alt)") \
	def(K_DPAD_DOWN_ALT, 270, "DPAD DOWN (alt)",  "DPAD DOWN (alt)",   "DPAD DOWN (alt)") \
	def(K_DPAD_LEFT_ALT, 271, "DPAD LEFT (alt)",  "DPAD LEFT (alt)",   "DPAD LEFT (alt)") \
	def(K_DPAD_RIGHT_ALT,272, "DPAD RIGHT (alt)", "DPAD RIGHT (alt)",  "DPAD RIGHT (alt)") \
	def(K_ABUTTON_ALT,   273, "A (alt)",          "X (alt)",           "A (alt)") \
	def(K_BBUTTON_ALT,   274, "B (alt)",          "CIRCLE (alt)",      "B (alt)") \
	def(K_XBUTTON_ALT,   275, "X (alt)",          "SQUARE (alt)",      "X (alt)") \
	def(K_YBUTTON_ALT,   276, "Y (alt)",          "TRIANGLE (alt)",    "Y (alt)") \
	def(K_LTRIGGER_ALT,  277, "LT (alt)",         "L2 (alt)",          "ZL (alt)") \
	def(K_RTRIGGER_ALT,  278, "RT (alt)",         "R2 (alt)",          "ZR (alt)") \
	def(K_MISC1_ALT,     279, NULL,               "MUTE (alt)",        "CAPTURE (alt)") \
	def(K_PADDLE1_ALT,   280, "P1 PADDLE (alt)",  NULL,                NULL) \
	def(K_PADDLE2_ALT,   281, "P3 PADDLE (alt)",  NULL,                NULL) \
	def(K_PADDLE3_ALT,   282, "P2 PADDLE (alt)",  NULL,                NULL) \
	def(K_PADDLE4_ALT,   283, "P4 PADDLE (alt)",  NULL,                NULL) \
	def(K_TOUCHPAD_ALT,  284, NULL,               "TOUCHPAD (alt)",    NULL)

//
// these are the key numbers that should be passed to Key_Event
//
#define	K_TAB			9
#define	K_ENTER			13
#define	K_ESCAPE		27
#define	K_SPACE			32

// normal keys should be passed as lowercased ascii

#define	K_BACKSPACE		127
#define	K_CAPSLOCK		171 // woods #capslock
#define	K_UPARROW		128
#define	K_DOWNARROW		129
#define	K_LEFTARROW		130
#define	K_RIGHTARROW	131

#define	K_ALT			132
#define	K_CTRL			133
#define	K_SHIFT			134
#define	K_F1			135
#define	K_F2			136
#define	K_F3			137
#define	K_F4			138
#define	K_F5			139
#define	K_F6			140
#define	K_F7			141
#define	K_F8			142
#define	K_F9			143
#define	K_F10			144
#define	K_F11			145
#define	K_F12			146
#define	K_INS			147
#define	K_DEL			148
#define	K_PGDN			149
#define	K_PGUP			150
#define	K_HOME			151
#define	K_END			152

#define	K_KP_NUMLOCK		153
#define	K_KP_SLASH		154
#define	K_KP_STAR		155
#define	K_KP_MINUS		156
#define	K_KP_HOME		157
#define	K_KP_UPARROW		158
#define	K_KP_PGUP		159
#define	K_KP_PLUS		160
#define	K_KP_LEFTARROW		161
#define	K_KP_5			162
#define	K_KP_RIGHTARROW		163
#define	K_KP_END		164
#define	K_KP_DOWNARROW		165
#define	K_KP_PGDN		166
#define	K_KP_ENTER		167
#define	K_KP_INS		168
#define	K_KP_DEL		169

#define	K_COMMAND		170
#define K_PRINTSCREEN	174 // woods #printscreen

//
// mouse buttons generate virtual keys
//
#define	K_MOUSE1		200
#define	K_MOUSE2		201
#define	K_MOUSE3		202

//
// joystick buttons
//
#define	K_JOY1			203
#define	K_JOY2			204
#define	K_JOY3			205
#define	K_JOY4			206
// aux keys are for multi-buttoned joysticks to generate so they can use
// the normal binding process
// aux29-32: reserved for the HAT (POV) switch motion
#define	K_AUX1			207
#define	K_AUX2			208
#define	K_AUX3			209
#define	K_AUX4			210
#define	K_AUX5			211
#define	K_AUX6			212
#define	K_AUX7			213
#define	K_AUX8			214
#define	K_AUX9			215
#define	K_AUX10			216
#define	K_AUX11			217
#define	K_AUX12			218
#define	K_AUX13			219
#define	K_AUX14			220
#define	K_AUX15			221
#define	K_AUX16			222
#define	K_AUX17			223
#define	K_AUX18			224
#define	K_AUX19			225
#define	K_AUX20			226
#define	K_AUX21			227
#define	K_AUX22			228
#define	K_AUX23			229
#define	K_AUX24			230
#define	K_AUX25			231
#define	K_AUX26			232
#define	K_AUX27			233
#define	K_AUX28			234
#define	K_AUX29			235
#define	K_AUX30			236
#define	K_AUX31			237
#define	K_AUX32			238

// JACK: Intellimouse(c) Mouse Wheel Support

#define K_MWHEELUP		239
#define K_MWHEELDOWN		240

// thumb buttons
#define K_MOUSE4		241
#define K_MOUSE5		242

// SDL2 game controller keys
#define GAMEPAD_KEYCODE_DEFINE(keycode, value, xboxname, psname, nintendoname) enum { keycode = value };
GAMEPAD_KEY_LIST(GAMEPAD_KEYCODE_DEFINE)
#undef GAMEPAD_KEYCODE_DEFINE

#define K_GAMEPAD_BEGIN		K_START
#define K_GAMEPAD_END		(K_TOUCHPAD_ALT + 1)
#define K_GAMEPAD_COUNT		(K_GAMEPAD_END - K_GAMEPAD_BEGIN)
COMPILE_TIME_ASSERT(gamepad_end_correct, K_TOUCHPAD_ALT + 1 == K_GAMEPAD_END);

#define K_PAUSE			K_GAMEPAD_END

#define	MAX_KEYS		320
#define MAX_BINDMAPS	8

#define	MAXCMDLINE	256
#define	MAX_CHAT_SIZE	45 // woods limit chat to 45 server limit #chatlimit
#define	MAX_CHAT_SIZE_EX	100 // woods limit chat to 100 server limit #chatlimit

typedef enum {key_game, key_console, key_message, key_menu} keydest_t;

typedef enum
{
	KD_NONE = -1,
	KD_KEYBOARD,
	KD_MOUSE,
	KD_GAMEPAD,
} keydevice_t;

typedef enum
{
	KDM_NONE = 0,
	KDM_KEYBOARD = 1 << KD_KEYBOARD,
	KDM_MOUSE = 1 << KD_MOUSE,
	KDM_GAMEPAD = 1 << KD_GAMEPAD,
	KDM_KEYBOARD_AND_MOUSE = KDM_KEYBOARD | KDM_MOUSE,
	KDM_ANY = -1,
} keydevicemask_t;

extern keydest_t	key_dest;
extern	char	*keybindings[MAX_BINDMAPS][MAX_KEYS];
extern	qboolean	keydown[MAX_KEYS];

#define		CMDLINES 64

extern	char	key_lines[CMDLINES][MAXCMDLINE];
extern	char	key_tabhint[MAXCMDLINE]; // woods #iwtabcomplete
extern	int		edit_line;
extern	size_t	key_linepos; // woods -- int to size_t
extern	int		key_insert;
extern	double		key_blinktime;
extern	int		key_bindmap[2];

extern	qboolean	chat_team;

void Key_Init (void);
void Key_ClearStates (void);
void Key_UpdateForDest (void);

void Key_BeginInputGrab (void);
void Key_EndInputGrab (void);
void Key_GetGrabbedInput (int *lastkey, int *lastchar);

void Key_Event (int key, qboolean down);
void Key_EventWithKeycode (int key, qboolean down, int keycode);
void Char_Event (int key);
qboolean Key_TextEntry (void);

void Key_SetBinding (int keynum, const char *binding, int bindmap);
keydevice_t Key_GetDeviceForKeynum (int keynum);
keydevicemask_t Key_GetDeviceMaskForKeynum (int keynum);
int Key_GetKeysForCommand (const char *command, int *keys, int maxkeys, keydevicemask_t devmask);
qboolean Key_IsKeyGamepadAltModifier (int keynum);
qboolean Key_GetGamepadAltModifierState (void);
const char *Key_KeynumToString (int keynum);
const char *Key_KeynumToFriendlyString (int keynum);
int Key_StringToKeynum (const char *str);
int Key_NativeToQC(int code);
int Key_QCToNative(int code);	//warning: will return negative values for unknown qc keys.
void Key_WriteBindings (FILE *f);

void Key_EndChat (void);
const char *Key_GetChatBuffer (void);
int Key_GetChatMsgLen (void);

void History_Init (void);
void History_Shutdown (void);
qboolean History_IsSaving (void);
void History_StoreCommand (const char *line); // woods #serverhistory
qboolean History_GetPrevious (const char *current, char *out, size_t out_size); // woods #serverhistory
qboolean History_GetNext (const char *current, char *out, size_t out_size); // woods #serverhistory
void Key_Extra (int* key); // woods #namemaker

#endif	/* _QUAKE_KEYS_H */

