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
// console.c

#include <sys/types.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif
#include "quakedef.h"
#include "q_ctype.h"
#include <errno.h> // woods
#include <curl/curl.h> // woods #discord
#include "json.h" // woods #discord

int 		con_linewidth;

float		con_cursorspeed = 4;

#define		CON_TEXTSIZE (1024 * 1024) //ericw -- was 65536. johnfitz -- new default size
#define		CON_MINSIZE  16384 //johnfitz -- old default, now the minimum size
#define		CON_MARGIN   1 // woods #iwtabcomplete

int		con_buffersize; //johnfitz -- user can now override default

qboolean 	con_forcedup;		// because no entities to refresh
qboolean 	matchstats = false;	// woods
qboolean 	netquakeio = false;	// woods
qboolean	firstCheckPassed = false; // woods

int		con_totallines;		// total lines in console scrollback
int		con_backscroll;		// lines up from bottom to display
int		con_current;		// where next message will be printed
int		con_x;				// offset in current line for next print
char		*con_text = NULL;

extern qboolean cl_mm2; // woods #con_mm1mute
extern char afk_name[16]; // woods #smartafk

cvar_t		con_notifytime = {"con_notifytime","3",CVAR_ARCHIVE};	//seconds
cvar_t		con_logcenterprint = {"con_logcenterprint", "1", CVAR_ARCHIVE}; //johnfitz

cvar_t		con_filter = { "con_filter", "1", CVAR_ARCHIVE }; //johnfitz
cvar_t		con_notifylist = { "con_notifylist", "", CVAR_ARCHIVE }; // woods #notiy
cvar_t		con_mm1mute = {"con_mm1mute", "1", CVAR_ARCHIVE}; // woods #con_mm1mute
cvar_t		con_notifylines = { "con_notifylines","4",CVAR_ARCHIVE }; // woods #notifylines
cvar_t		con_notifyposition = { "con_notifyposition","0",CVAR_ARCHIVE }; // woods #notifyposition
cvar_t		con_notifyfade = {"con_notifyfade", "1", CVAR_ARCHIVE}; // woods #confade
cvar_t		con_notifyfadetime = {"con_notifyfadetime", "0.5", CVAR_ARCHIVE}; // woods #confade
cvar_t		con_colmax = { "con_colmax", "0", CVAR_ARCHIVE}; // woods #consolecols
cvar_t		con_coldirection = { "con_coldirection", "0", CVAR_ARCHIVE}; // woods #consolecols
cvar_t		con_notifydiscord = {"con_notifydiscord", "", CVAR_ARCHIVE}; // woods #discord

char		con_lastcenterstring[1024]; //johnfitz

void (*con_redirect_flush)(const char *buffer);	//call this to flush the redirection buffer (for rcon)
char con_redirect_buffer[8192];

#define	NUM_CON_TIMES 30 // woods from proquake 493 #notifylines
double		con_times[NUM_CON_TIMES];	// realtime time the line was generated
						// for transparent notify lines

int			con_vislines;
int			con_notifylines_; // woods from proquake 493 #notifylines

qboolean	con_debuglog = false;

qboolean	con_initialized;

void Char_Console2(int key); // woods #ezsay add leading space for mode 2
void Key_Console(int key); // woods con_clear_input_on_toggle
extern qboolean	endscoreprint; // woods -- don't filter end scores pq_confilter+
char lastconnected[3]; // woods -- #identify+
char lc[3]; // woods -- #identify+
int retry_counter = 0; // woods #ms
extern SDL_TimerID chatTimerID; // woods #chatinfo
extern qboolean isChatTimerRunning; // woods #chatinfo

#define BIRTHDAY_DURATION 30000 // 30 seconds in ms -- woods #qbday
extern qboolean pak0; // pak0 present  -- woods #qbday
static Uint32 birthday_start_time = 0; // woods #qbday
static int console_msg_since_lastchat = 0; // woods #like
extern qboolean WordFilter_Check(const char* text, char* dest_buffer, size_t buffer_size); // woods #contentfilter

// woods #discord
typedef struct {
    char payload[1300];
    char url[256];
} discord_job_t;

static int DiscordThread(void *data);
static void MakeDiscordPayload(const char *raw, char *out, size_t outsz);
static void QSSM_DiscordNotify(const char *raw_msg);

/* Redact webhook token when printing URLs to logs/errors */
static void MaskDiscordURL(const char *url, char *out, size_t outsz)
{
    if (!url || !*url) { if (outsz) out[0] = 0; return; }
    const char *needle = "/api/webhooks/";
    const char *p = strstr(url, needle);
    if (!p) { q_strlcpy(out, "<invalid>", outsz); return; }
    size_t keep = (size_t)(p - url) + strlen(needle);
    if (keep >= outsz) keep = outsz ? outsz - 1 : 0;
    memcpy(out, url, keep);
    if (keep < outsz) q_strlcpy(out + keep, "REDACTED", outsz - keep);
}

// URL validation for Discord webhooks
static qboolean IsDiscordWebhookURL(const char *url)
{
    static const char *okprefixes[] = {
        "https://discord.com/api/webhooks/",
        "https://canary.discord.com/api/webhooks/",
        "https://ptb.discord.com/api/webhooks/",
        "https://discordapp.com/api/webhooks/", // legacy, optional
    };
    size_t i;

    if (!url || !*url) return false;

    // reject control chars / whitespace
    for (const unsigned char *p = (const unsigned char*)url; *p; ++p)
        if (*p <= 0x20) return false;

    for (i = 0; i < sizeof(okprefixes)/sizeof(okprefixes[0]); ++i)
        if (!Q_strncmp(url, okprefixes[i], strlen(okprefixes[i])))
            return true;

    return false;
}

static char g_last_good_discord[256] = ""; // persists between calls

static void ConNotifyDiscord_Callback(cvar_t* var)
{
	if (!var->string[0]) {            // empty is allowed
		g_last_good_discord[0] = 0;
		return;
	}

	if (IsDiscordWebhookURL(var->string)) {
		q_strlcpy(g_last_good_discord, var->string, sizeof(g_last_good_discord));
		return;
	}

	char masked[256];
	MaskDiscordURL(var->string, masked, sizeof(masked));
	Con_Printf("discord: invalid webhook URL ^m%s^m (must be https://*.discord.com/api/webhooks/...)\n", masked);

	// Temporarily remove callback to prevent recursion, then restore it
	cvarcallback_t saved_callback = var->callback;
	var->callback = NULL;
	Cvar_SetQuick(var, g_last_good_discord); // guaranteed-valid (or empty)
	var->callback = saved_callback;
}

/*
================
Con_Quakebar -- johnfitz -- returns a bar of the desired length, but never wider than the console

includes a newline, unless len >= con_linewidth.
================
*/
const char *Con_Quakebar (int len)
{
	static char bar[42];
	int i;

	len = q_min(len, (int)sizeof(bar) - 2);
	len = q_min(len, con_linewidth);

	bar[0] = '\35';
	for (i = 1; i < len - 1; i++)
		bar[i] = '\36';
	bar[len-1] = '\37';

	if (len < con_linewidth)
	{
		bar[len] = '\n';
		bar[len+1] = 0;
	}
	else
		bar[len] = 0;

	return bar;
}

/*
================
Con_ToggleConsole_f
================
*/
extern int history_line; //johnfitz

void Con_ToggleConsole_f (void)
{
	if (key_dest == key_console/* || (key_dest == key_game && con_forcedup)*/)
	{
		//key_lines[edit_line][1] = 0;	// clear any typing -- woods con_clear_input_on_toggle from Qrack (R00k)
		//key_linepos = 1; // woods con_clear_input_on_toggle from Qrack (R00k)

		Key_Console(K_BACKSPACE); // woods con_clear_input_on_toggle

		con_backscroll = 0; //johnfitz -- toggleconsole should return you to the bottom of the scrollback
		history_line = edit_line; //johnfitz -- it should also return you to the bottom of the command history

		if (cls.state == ca_connected)
			key_dest = key_game;
		else
			M_ToggleMenu(0); // woods, was 1, better ui not to go to menu (kilomile) 
	}
	else
	{
		M_ToggleMenu(0);
		key_dest = key_console;
	}

	if ((key_linepos == 1) && (cl_say.value == 2 || cl_say.value == 3)) // woods #ezsay add leading space for mode 2
		Char_Console2(32);

	if (cl_say.value) // woods #chatinfo
	{ 
		SetChatInfo (0);

		if (isChatTimerRunning) // woods #chatinfo
		{
			SDL_RemoveTimer(chatTimerID);
			isChatTimerRunning = false;
			chatTimerID = 0;
		}
	}

	cl.expectingpingtimes = realtime + 1; // woods

	SCR_EndLoadingPlaque ();
	memset (con_times, 0, sizeof(con_times));

	IN_UpdateGrabs();
}

/*
================
Con_Clear_f
================
*/
static void Con_Clear_f (void)
{
	if (con_text)
		Q_memset (con_text, ' ', con_buffersize); //johnfitz -- con_buffersize replaces CON_TEXTSIZE
	con_backscroll = 0; //johnfitz -- if console is empty, being scrolled up is confusing
}

/*
================
Con_Dump_f -- johnfitz -- adapted from quake2 source
================
*/
static void Con_Dump_f (void)
{
	int		l, x;
	const char	*line;
	FILE	*f;
	char	buffer[1024];
	char	name[MAX_OSPATH];

	q_snprintf (name, sizeof(name), "%s/condump.txt", com_gamedir);
	COM_CreatePath (name);
	f = fopen (name, "w");
	if (!f)
	{
		Con_Printf ("ERROR: couldn't open file %s.\n", name);
		return;
	}

	// skip initial empty lines
	for (l = con_current - con_totallines + 1; l <= con_current; l++)
	{
		line = con_text + (l % con_totallines)*con_linewidth;
		for (x = 0; x < con_linewidth; x++)
			if (line[x] != ' ')
				break;
		if (x != con_linewidth)
			break;
	}

	// write the remaining lines
	buffer[con_linewidth] = 0;
	for ( ; l <= con_current; l++)
	{
		line = con_text + (l%con_totallines)*con_linewidth;
		strncpy (buffer, line, con_linewidth);
		for (x = con_linewidth - 1; x >= 0; x--)
		{
			if (buffer[x] == ' ')
				buffer[x] = 0;
			else
				break;
		}
		for (x = 0; buffer[x]; x++)
			buffer[x] &= 0x7f;

		unsigned char* ch; // woods dequake
		for (ch = (unsigned char*)buffer; *ch; ch++)
		*ch = dequake[*ch];

		fprintf (f, "%s\n", buffer);
	}

	fclose (f);
	if (cl_contentfilter.value) // woods #contentfilter
		Con_Printf("Dumped console text to %s/condump.txt.\n", COM_SkipPath(com_gamedir));
	else
		Con_Printf("Dumped console text to %s.\n", name);
}

/*
================
Con_Copy_f -- woods #concopy
================
*/
void Con_Copy_f(void)
{
	char *f;

	Con_Dump_f();
	f = (char*)COM_LoadHunkFile("condump.txt", NULL);
#if defined(USE_SDL2)
	SDL_SetClipboardText(f);
#endif
	const char* soundFile = COM_FileExists("sound/qssm/copy.wav", NULL) ? "qssm/copy.wav" : "player/tornoff2.wav";
	S_LocalSound(soundFile); // woods add sound to screenshot
}

/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify (void)
{
	int		i;

	for (i = 0; i < NUM_CON_TIMES; i++)
		con_times[i] = 0;
}

/*
================
Con_MessageMode_f
================
*/
static void Con_MessageMode_f (void)
{
	if (cls.state != ca_connected || cls.demoplayback)
		return;
	chat_team = false;
	key_dest = key_message;
	SetChatInfo (CIF_CHAT); // woods #chatinfo
}

/*
================
Con_MessageMode2_f
================
*/
static void Con_MessageMode2_f (void)
{
	if (cls.state != ca_connected || cls.demoplayback)
		return;
	chat_team = true;
	key_dest = key_message;
	SetChatInfo (CIF_CHAT); // woods #chatinfo
}

/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize (void)
{
	int	i, j, width, oldwidth, oldtotallines, numlines, numchars;
	char	*tbuf; //johnfitz -- tbuf no longer a static array
	int mark; //johnfitz

	width = (vid.conwidth >> 3) - 2; //johnfitz -- use vid.conwidth instead of vid.width

	if (width == con_linewidth)
		return;

	oldwidth = con_linewidth;
	con_linewidth = width;
	oldtotallines = con_totallines;
	con_totallines = con_buffersize / con_linewidth; //johnfitz -- con_buffersize replaces CON_TEXTSIZE
	numlines = oldtotallines;

	if (con_totallines < numlines)
		numlines = con_totallines;

	numchars = oldwidth;

	if (con_linewidth < numchars)
		numchars = con_linewidth;

	mark = Hunk_LowMark (); //johnfitz
	tbuf = (char *) Hunk_Alloc (con_buffersize); //johnfitz

	Q_memcpy (tbuf, con_text, con_buffersize);//johnfitz -- con_buffersize replaces CON_TEXTSIZE
	Q_memset (con_text, ' ', con_buffersize);//johnfitz -- con_buffersize replaces CON_TEXTSIZE

	for (i = 0; i < numlines; i++)
	{
		for (j = 0; j < numchars; j++)
		{
			con_text[(con_totallines - 1 - i) * con_linewidth + j] =
					tbuf[((con_current - i + oldtotallines) % oldtotallines) * oldwidth + j];
		}
	}

	Hunk_FreeToLowMark (mark); //johnfitz

	Con_ClearNotify ();

	con_backscroll = 0;
	con_current = con_totallines - 1;
}


/*
================
Con_Init
================
*/
void Con_Init (void)
{
	int i;

	//johnfitz -- user settable console buffer size
	i = COM_CheckParm("-consize");
	if (i && i < com_argc-1) {
		con_buffersize = Q_atoi(com_argv[i+1])*1024;
		if (con_buffersize < CON_MINSIZE)
			con_buffersize = CON_MINSIZE;
	}
	else
		con_buffersize = CON_TEXTSIZE;
	//johnfitz

	con_text = (char *) Hunk_AllocName (con_buffersize, "context");//johnfitz -- con_buffersize replaces CON_TEXTSIZE
	Q_memset (con_text, ' ', con_buffersize);//johnfitz -- con_buffersize replaces CON_TEXTSIZE
	con_linewidth = -1;

	//johnfitz -- no need to run Con_CheckResize here
	con_linewidth = 78;
	con_totallines = con_buffersize / con_linewidth;//johnfitz -- con_buffersize replaces CON_TEXTSIZE
	con_backscroll = 0;
	con_current = con_totallines - 1;
	//johnfitz

	Con_Printf ("Console initialized.\n");

	Cvar_RegisterVariable (&con_notifytime);
	Cvar_RegisterVariable (&con_logcenterprint); //johnfitz
	Cvar_RegisterVariable (&con_colmax); // woods #consolecols
	Cvar_RegisterVariable (&con_coldirection); // woods #consolecols

	Cvar_RegisterVariable( &con_filter);
	Cvar_RegisterVariable (&con_notifylist); // woods #notiy
	Cvar_RegisterVariable (&con_mm1mute); // woods #con_mm1mute
	Cvar_RegisterVariable (&con_notifylines); // woods #notifylines
	Cvar_RegisterVariable (&con_notifyposition); // woods #notifyposition
	Cvar_RegisterVariable (&con_notifyfade); // woods #confade
	Cvar_RegisterVariable (&con_notifyfadetime); // woods #confade
	Cvar_RegisterVariable (&con_notifydiscord); // woods #discord
	Cvar_SetCallback (&con_notifydiscord, &ConNotifyDiscord_Callback); // woods #discord


	Cmd_AddCommand ("toggleconsole", Con_ToggleConsole_f);
	Cmd_AddCommand ("messagemode", Con_MessageMode_f);
	Cmd_AddCommand ("messagemode2", Con_MessageMode2_f);
	Cmd_AddCommand ("clear", Con_Clear_f);
	Cmd_AddCommand ("condump", Con_Dump_f); //johnfitz
	con_initialized = true;
}


/*
===============
Con_Linefeed
===============
*/
static void Con_Linefeed (void)
{
	//johnfitz -- improved scrolling
	if (con_backscroll)
		con_backscroll++;
	if (con_backscroll > con_totallines - (glheight>>3) - 1)
		con_backscroll = con_totallines - (glheight>>3) - 1;
	//johnfitz

	con_x = 0;
	con_current++;
	Q_memset (&con_text[(con_current%con_totallines)*con_linewidth], ' ', con_linewidth);
}

#define ishex(c) ((c>='0' && c<= '9') || (c>='a' && c<='f') || (c>='A' && c<='F'))
static int dehex(char c)
{
	if (c >= '0' && c <= '9')
		return c-'0';
	if (c >= 'A' && c <= 'F')
		return c-('A'-10);
	if (c >= 'a' && c <= 'f')
		return c-('a'-10);
	return 0;
}

/*
===============
Ghost_ID_Backup_f // woods #ghost backup the name externally to a text file for possible crash event
===============
*/
void Ghost_ID_Backup_f (void)
{
	FILE* f;

	char ghost[MAX_OSPATH];
	char str[6];

	q_snprintf(ghost, sizeof(ghost), "%s/id1/backups", com_basedir); //  create backups folder if not there
	Sys_mkdir(ghost);

	sprintf(str, "ghost");

	f = fopen(va("%s/id1/backups/%s.txt", com_basedir, str), "w");

	if (!f)
	{
		Con_Printf("Couldn't write backup ghostcode\n");
		return;
	}

	fprintf(f, "%.3s", cl.ghostcode);

	fclose(f);
}

static qboolean Con_ProcessPlayerMessage(const char* txt) // woods #like
{
	if (!(cls.signon == SIGNONS && cl.maxclients > 1))
		return false;

	// Find the first colon which might separate a player name from message
	const char* colon_pos = strstr(txt, ": ");

	if (!colon_pos)
		return false;

	// Extract potential player name
	int name_length = colon_pos - txt;

	// Check if this could reasonably be a player name
	if (name_length <= 0 || name_length >= 32)
		return false;

	char sender_name[32];
	memcpy(sender_name, txt, name_length);
	sender_name[name_length] = '\0';

	// Clean the sender name of control characters
	char clean_sender_name[32];
	int clean_idx = 0;
	for (int i = 0; i < name_length && clean_idx < 31; i++) {
		if ((unsigned char)sender_name[i] >= 32) { // Skip control characters
			clean_sender_name[clean_idx++] = sender_name[i];
		}
	}
	clean_sender_name[clean_idx] = '\0';

	// Check if this name exists in the scoreboard
	qboolean valid_player = false;
	for (int i = 0; i < cl.maxclients; i++) {
		scoreboard_t* s = &cl.scores[i];
		if (!s->name[0]) // Skip empty slots
			continue;

		// Create a clean version of the scoreboard name, only first 15 chars (actual name)
		char clean_name[16]; // Just for the actual player name
		const char* src = s->name;
		char* dst = clean_name;
		int len = 0;

		// Copy while removing color codes, limit to first part of name (max 15 chars)
		while (*src && len < 15) {
			if (*src == '^' && *(src + 1)) {
				src += 2; // Skip color code
				continue;
			}
			
			if ((unsigned char)*src >= 32) // Only copy printable characters
			{
				*dst++ = *src;
				len++;
			}
			src++;
		}
		*dst = '\0';

		// Trim trailing spaces
		dst = clean_name + strlen(clean_name) - 1;
		while (dst >= clean_name && *dst == ' ')
			*dst-- = '\0';

		if (strlen(clean_name) == 0) // Skip if no valid name after cleaning
			continue;

		// Compare with our sender name using substring matching (case insensitive)
		qboolean name_match = false;

		// First check for team chat format with parentheses
		char team_format[34]; // Clean name with parentheses
		snprintf(team_format, sizeof(team_format), "(%s)", clean_name);

		// Use case-insensitive substring search with the clean sender name
		if (q_strcasestr(clean_sender_name, clean_name) != NULL ||
			q_strcasestr(clean_sender_name, team_format) != NULL) {
			name_match = true;
		}

		// Additional check for special cases
		if (!name_match && clean_idx > 0 && clean_sender_name[0] == '(' &&
			strstr(clean_sender_name + 1, clean_name) != NULL) {
			name_match = true;
		}

		// If content filtering is enabled, check if the filtered name would match
		if (!name_match && cl_contentfilter.value == 2) 
		{
			char filtered_name[32];
			char filtered_sender[32];

			// Filter both the sender name and the scoreboard name
			WordFilter_Check(clean_name, filtered_name, sizeof(filtered_name));
			WordFilter_Check(clean_sender_name, filtered_sender, sizeof(filtered_sender));

			// Check for matches with filtered names
			if (q_strcasestr(filtered_sender, filtered_name) != NULL) {
				name_match = true;
			}

			// Check for team format with filtered name
			snprintf(team_format, sizeof(team_format), "(%s)", filtered_name);
			if (q_strcasestr(filtered_sender, team_format) != NULL) {
				name_match = true;
			}
		}

		if (name_match) {
			valid_player = true;
			break;
		}
	}

	// If it's a valid player message, look for "likes" and format it, for qss(m) white chars
	if (valid_player)
	{
		const char* search = ": likes ";
		const char* likes_pos = strstr(txt, search);

		if (likes_pos)
		{
			size_t new_len = strlen(txt) + strlen("^m^m"); // Additional length for ^m markers
			char* modified_txt = (char*)malloc(new_len + 1); // +1 for null terminator

			if (modified_txt)
			{
				size_t prefix_len = likes_pos - txt;

				Q_strncpy(modified_txt, txt, prefix_len); // Copy before ": likes "
				Q_strcpy(modified_txt + prefix_len, ": ^mlikes^m "); // Add formatted "likes"
				Q_strcpy(modified_txt + prefix_len + strlen(": ^mlikes^m "),
					likes_pos + strlen(search));

				// Don't copy "likes" messages to lastchat
				Con_Printf("%s", modified_txt);

				free(modified_txt);
				return true; // Message was handled
			}
		}

		// For non-likes messages, copy to lastchat
		Q_strncpy(cl.lastchat, txt, sizeof(cl.lastchat) - 1);
		cl.lastchat[sizeof(cl.lastchat) - 1] = '\0';

		// Reset the console message counter when we store a new chat
		console_msg_since_lastchat = 0;
	}

	return false; // Continue normal processing
}

static void Con_UpdateLastchatCounter(void) // woods #like
{
	// Check if we need to clear lastchat due to too many console messages
	if (cl.lastchat[0] != '\0') {
		console_msg_since_lastchat++;

		// If 20+ console messages have occurred, clear lastchat
		if (console_msg_since_lastchat >= 20) {
			cl.lastchat[0] = '\0';
			console_msg_since_lastchat = 0;
		}
	}
}

Uint32 RideDelayCallback(Uint32 interval, void* param) // runequake changelevel hack
{
	strncpy(cl.observer, "y", sizeof(cl.observer));
	cl.modtype = 6;
	return 0; // 0 means the timer won't repeat
}

/*
================
Con_Print

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the notify window will pop up.
================
*/
static void Con_Print (const char *txt)
{
	int		y;
	int		c, l;
	static int	cr;
	int		mask;
	qboolean	boundary;
	static int fixline = 0; // woods #confilter

	//con_backscroll = 0; //johnfitz -- better console scrolling

	// begin woods for pq_confilter+

	if (cl.gametype == GAME_DEATHMATCH && cls.state == ca_connected)
	{
		if (cl_autodemo.value == 2) // woods, inspired by uns disconnects :(
			if (!strcmp(txt, "Match unpaused\n") && !cls.demoplayback && !cls.demorecording)
				Cmd_ExecuteString("record\n", src_command);

		if (cl.time < 600 && strstr(txt, "Now riding "))
		{
			SDL_AddTimer(2000, RideDelayCallback, NULL);
		}

		if (cl.modtype == 6) // runequake observer detection
		{
			if (strstr(txt, "Now riding "))
				strncpy(cl.observer, "y", sizeof(cl.observer));

			char name_to_check[32];
			if (cl_contentfilter.value == 2) {
				WordFilter_Check(cl_name.string, name_to_check, sizeof(name_to_check));
			}
			else {
				Q_strncpy(name_to_check, cl_name.string, sizeof(name_to_check) - 1);
				name_to_check[sizeof(name_to_check) - 1] = '\0';
			}

			if (!strcmp(txt, name_to_check))
				firstCheckPassed = true;
			else if (firstCheckPassed && !strcmp(txt, " joined the game\n"))
			{
				strncpy(cl.observer, "n", sizeof(cl.observer));
				firstCheckPassed = false;  // Reset flag after the condition is met
			}
			else if (firstCheckPassed && !strcmp(txt, " became an observer\n"))
			{
				strncpy(cl.observer, "y", sizeof(cl.observer));
				firstCheckPassed = false;  // Reset flag after the condition is met
			}
			else if (firstCheckPassed && !strcmp(txt, " is the new challenger\n")) // ra
			{
				strncpy(cl.observer, "n", sizeof(cl.observer));
				firstCheckPassed = false;  // Reset flag after the condition is met
			}
			else if (firstCheckPassed) // The next text was not expected, reset the flag
				firstCheckPassed = false;

			if (q_strcasestr(txt, "]observer"))
				strncpy(cl.observer, "y", sizeof(cl.observer));

			if (q_strcasestr(txt, "Now riding"))
				strncpy(cl.observer, "y", sizeof(cl.observer));
		}

		if (!strstr(txt, "entered the game")) // woods -- #identify+ copy all strings, except entered
			memcpy(lc, txt, sizeof(lc));

		if (strstr(txt, "entered the game")) // woods -- #identify+ copy the name prior
			memcpy(lastconnected, lc, sizeof(lastconnected));

		if (strstr(txt, "has connected")) // woods -- #identify+
			memcpy(lastconnected, txt, sizeof(lastconnected));

		if (Con_ProcessPlayerMessage(txt)) // woods #like
			return;

		Con_UpdateLastchatCounter(); // woods #like

		if (!strcmp(txt, "You receive "))

			cl.conflag = 2;  // flag beginnings

	// need to terminate the conflag with end of prints to parse out numbers
		if ((!strcmp(txt, " health\n")))  // line end included
			cl.conflag = 0; // flag end of string

		if (cl.conflag == 2 && con_filter.value)  // delete when flag set
		{
			fixline = 1; // voodoo
			return;
		}

		if (!strcmp(txt, "is ")) // the string directly before ghost code #ghostcode
			cl.conflag = 3; // set flag for ghostcode

		if ((cl.conflag == 3) && (strcmp(txt, "is ")))  // string before ghost number #ghostcode
		{
			memcpy(cl.ghostcode, txt, sizeof(cl.ghostcode)); // copy ghostcode to memory
			Ghost_ID_Backup_f ();
			cl.conflag = 0; // reset flag	
		}

		if (!strcmp(txt, "Your ghost code is ")) // the string directly before ghost code #ghostcode
			cl.conflag = 4; // set flag for ghostcode

		if ((cl.conflag == 4) && (strcmp(txt, "Your ghost code is ")))  // string before ghost number #ghostcode
		{
			memcpy(cl.ghostcode, txt, sizeof(cl.ghostcode)); // copy ghostcode to memory
			Ghost_ID_Backup_f ();
			cl.conflag = 0; // reset flag	
		}

		if(cl.modtype == 2 || cl.modtype == 3)
		if (!strcmp(txt, "chase mode - help-chase for help\n") || // woods #observer
			!strcmp(txt, "eyecam mode - help-chase for help\n"))
			strncpy(cl.observer, "y", sizeof(cl.observer));

		if ((!strcmp(txt, "Smoothing ")) || (!strcmp(txt, "OFF "))) // "smoothing OFF" woods #observer
			strncpy(cl.observer, "n", sizeof(cl.observer));

		if     // other messages, exact cases
			((
				!strcmp(txt, "Quad Damage is wearing off\n") ||
				!strcmp(txt, "Protection is almost burned out\n") ||
				!strcmp(txt, "no weapon.\n") ||
				!strcmp(txt, "not enough ammo.\n") ||
				!strcmp(txt, "You got armor\n") ||
				!strcmp(txt, "Ring of Shadows magic is fading\n") ||
				!strcmp(txt, "Air supply in Biosuit expiring\n") ||
				!strcmp(txt, "You got the ") ||
				!strcmp(txt, "Rocket Launcher") ||
				!strcmp(txt, "Grenade Launcher") ||
				!strcmp(txt, "Super Nailgun") ||
				!strcmp(txt, "Thunderbolt") ||
				!strcmp(txt, "Double-barrelled Shotgun") ||
				!strcmp(txt, "nailgun") ||
				!strcmp(txt, "nails") ||
				!strcmp(txt, "cells") ||
				!strcmp(txt, "rockets") ||
				!strcmp(txt, "shells") ||
				!strcmp(txt, "spikes") ||
				(!strncmp(txt, "The Blue team has", 17) && !endscoreprint) ||
				(!strncmp(txt, "The Red team has", 16) && !endscoreprint) ||
				!strncmp(txt, "Match ends", 10) ||
			//	!strcmp(txt, " health\n") ||
				!strncmp(txt, "\"timelimit\" changed",19)) && con_filter.value && !endscoreprint)
		{
			fixline = 1;
			if ((
				!strcmp(txt, "Quad Damage is wearing off\n") ||
				!strcmp(txt, "Protection is almost burned out\n") ||
				!strcmp(txt, "no weapon.\n") ||
				!strcmp(txt, "not enough ammo.\n") ||
				!strcmp(txt, "You got armor\n") ||
				!strcmp(txt, "Ring of Shadows magic is fading\n") ||
				!strcmp(txt, "Air supply in Biosuit expiring\n") ||
				(!strncmp(txt, "The Blue team has", 17) && !endscoreprint) ||
				(!strncmp(txt, "The Red team has", 16) && !endscoreprint) ||
				!strncmp(txt, "Match ends", 10) ||
				!strncmp(txt, "\"timelimit\" changed", 19)) && con_filter.value && !endscoreprint)
				Con_Printf("\n");

			return;
		}

		if ((!strcmp(txt, " health\n") && con_filter.value))
		{
			Con_Printf("\n");
			return;
		}
	}
	// end woods for eliminating messages confilter+

	if (strstr(txt, "VERSION 1.09 SERVER")) // woods
		netquakeio = true; // woods

	if ((strstr(txt, "CL_ParseServerMessage: svc_updatename") && strstr(txt, "MAX_SCOREBOARD")) && retry_counter < 3) // woods, retry 3 times until this is fixed :(
	{
		Cbuf_AddText("reconnect\n");
		retry_counter++;
	}

	if (strstr(txt, "server does not have file locs/") || strstr(txt, "Download locs/")) // woods #locdownloads try to download; don't spam console its missing (kilomile)
		return;

	if (!VID_HasMouseOrInputFocus() && !cls.demoplayback) // woods flash if my name is mentioned #flash
		if ((cl.gametype == GAME_DEATHMATCH) && (cls.state == ca_connected))
		{ 
			char namewithcolon[20]; // me talking while away needs to be avoided (f_ prints, alt tabbed, etc)

			sprintf(namewithcolon, "%s: ", cl_name.string); // "woods: "

			char statistics[11] = { 243, 244, 225, 244, 233, 243, 244, 233, 227, 243, '\0' }; // woods -- quake font red 'statistics'

			if (strstr(txt, statistics) || strstr(txt, "match starting") || strstr(txt, "End of match"))
				matchstats = true;
			if (strstr(txt, "The match is over"))
				matchstats = false;

			if (strstr(txt, ": ")) // detect if a say command from another person (not perfect)
			{ 
				
				if (!strstr(txt, namewithcolon) && !matchstats && !strstr(txt, "alt-tabbed") && !strstr(txt, "next quad at")) // not me typing away or in a match end auto print -> "woods): "
				{ 
					if (cl_afk.value)
					{
						if (Q_strcasestr(txt, afk_name)) // has my name minus AFK (afk_name is only created if cl_afk 1)
						{
							SDL_FlashWindow((SDL_Window*)VID_GetWindow(), SDL_FLASH_BRIEFLY);	
							QSSM_DiscordNotify(txt); // woods #discord
						}
					}
					else
					{ 
						if (Q_strcasestr(txt, cl_name.string)) // has my name
						{
							SDL_FlashWindow((SDL_Window*)VID_GetWindow(), SDL_FLASH_BRIEFLY);
							QSSM_DiscordNotify(txt); // woods #discord
					}
					}

					char notifylist[MAXCMDLINE];
					snprintf(notifylist, sizeof(notifylist), "%s", con_notifylist.string);

					char* saveptr;
					char* token = SDL_strtokr(notifylist, " ", &saveptr);

					if (strstr(txt, ": ")) {
						while (token != NULL) {
							char* found = Q_strcasestr(txt, token);
							if (found != NULL) {
								// Check if the remaining string after the token is exactly one character
								if (strlen(found) == strlen(token) + 1) {
									SDL_FlashWindow((SDL_Window*)VID_GetWindow(), SDL_FLASH_BRIEFLY);
									QSSM_DiscordNotify(txt); // woods #discord
								}
							}
							token = SDL_strtokr(NULL, " ", &saveptr);
						}
					}
				}
			}
		}

	if (strstr(txt, "dm [")) // woods #tell+
	{
		S_LocalSound("misc/talk.wav");
		if (!VID_HasMouseOrInputFocus())
		{
			SDL_FlashWindow((SDL_Window*)VID_GetWindow(), SDL_FLASH_BRIEFLY);
			QSSM_DiscordNotify(txt); // woods #discord
		}
	}

	if (txt[0] == 1)
	{
		mask = 128;		// go to colored text`

		if (con_mm1mute.value && !strstr(txt, cl_name.string) && cl.notobserver && cl.matchinp && cl.teamcolor[0]) // woods #con_mm1mute
		{ 
			if (cl_mm2)
				S_LocalSound("misc/talk.wav");	// play talk wav
		}
		else
			S_LocalSound("misc/talk.wav");	// play talk wav
		txt++;
	}
	else if (txt[0] == 2)
	{
		mask = 128;		// go to colored text
		txt++;
	}
	else
		mask = 0;

	boundary = true;

	while ( (c = *txt) )
	{
		if ((cl.modtype == 4 || cl.modtype == 5) && strstr(txt, "&c")) // woods ezquake console colors filter
		{
			switch (*txt)
			{
			case '&':
				if (*(txt + 1) == 'c' && *(txt + 2) && *(txt + 3) && *(txt + 4))
				{
					txt += 5;
					continue;
				}
				break;
			case '{':
			case '}':
				txt++;
				continue;
			}
		}

		if (c == '^' && pr_checkextension.value)
		{	//parse markup like FTE/DP might.
			switch(txt[1])
			{
			case '^':	//doubled up char for escaping.
				txt++;
				break;
			case '0':	//black
			case '1':	//red
			case '2':	//green
			case '3':	//yellow
			case '4':	//blue
			case '5':	//cyan
			case '6':	//magenta
			case '7':	//white
			case '8':	//white+half-alpha
			case '9':	//grey
			case 'h':	//toggle half-alpha
			case 'b':	//blink
			case 'd':	//reset to defaults (fixme: should reset ^m without resetting \1)
			case 's':	//modstack push
			case 'r':	//modstack restore
				txt+=2;
				continue;
			case 'x':	//RGB 12-bit colour
				if (ishex(txt[2]) && ishex(txt[3]) && ishex(txt[4]))
				{
					txt+=4;
					continue;
				}
				break;	//malformed
			case '[':	//start fte's ^[text\key\value\key\value^] links
			case ']':	//end link
				break;	//fixme... skip the keys, recolour properly, etc
//				txt+=2;
//				continue;
			case '&':
				if ((ishex(txt[2])||txt[2]=='-') && (ishex(txt[3])||txt[3]=='-'))
				{	//ignore fte's fore/back ansi colours
					txt += 4;
					continue;
				}
				break;	//malformed
			case 'm':	//toggle masking.
				txt+=2;
				mask ^= 128;
				continue;
			case 'U':	//ucs-2 unicode codepoint
				if (ishex(txt[2]) && ishex(txt[3]) && ishex(txt[4]) && ishex(txt[5]))
				{
					c = (dehex(txt[2])<<12) | (dehex(txt[3])<<8) | (dehex(txt[4])<<4) | dehex(txt[5]);
					txt += 6-1;

					if (c >= 0xe000 && c <= 0xe0ff)
						c &= 0xff;	//private-use 0xE0XX maps to quake's chars
					else if (c >= 0x20 && c <= 0x7f)
						c &= 0x7f;	//ascii is okay too.
					else
						c = '?'; //otherwise its some unicode char that we don't know how to handle.
					break;
				}
				break; //malformed
			case '{':	//full unicode codepoint, for chars up to 0x10ffff
				txt += 2;
				c = 0;	//no idea
				while(*txt)
				{
					if (*txt == '}')
					{
						txt++;
						break;
					}
					if (!ishex(*txt))
						break;
					c<<=4;
					c |= dehex(*txt++);
				}
				txt--; // for the ++ below

				if (c >= 0xe000 && c <= 0xe0ff)
					c &= 0xff;	//private-use 0xE0XX maps to quake's chars
				else if (c >= 0x20 && c <= 0x7f)
					c &= 0x7f;	//ascii is okay too.
				else
					c = '?'; //otherwise its some unicode char that we don't know how to handle.
				break;
			}
		}

		if (c <= ' ')
		{
			boundary = true;
		}
		else if (boundary)
		{
			// count word length
			for (l = 0; l < con_linewidth; l++)
				if (txt[l] <= ' ')
					break;

			// word wrap
			if (l != con_linewidth && (con_x + l > con_linewidth))
				con_x = 0;

			boundary = false;
		}

		txt++;

		if (cr)
		{
			con_current--;
			cr = false;
		}

		if (!con_x)
		{
			Con_Linefeed ();
		// mark time for transparent overlay
			if (con_current >= 0)
				con_times[con_current % NUM_CON_TIMES] = realtime;
		}

		switch (c)
		{
		case '\n':
			if (fixline) /// woods JPG 1.05 - make the "you got" messages temporary #confilter
			{        
				cr = 1;
				fixline = 0;
			}
			con_x = 0;
			break;

		case '\r':
			con_x = 0;
			cr = 1;
			break;

		default:	// display character and advance
			y = con_current % con_totallines;
			con_text[y*con_linewidth+con_x] = c | mask;
			con_x++;
			if (con_x >= con_linewidth)
				con_x = 0;
			break;
		}
	}
}


// borrowed from uhexen2 by S.A. for new procs, LOG_Init, LOG_Close

static char	logfilename[MAX_OSPATH];	// current logfile name
static int	log_fd = -1;			// log file descriptor

/*
================
Con_DebugLog
================
*/
void Con_DebugLog(const char *msg)
{
	if (log_fd == -1)
		return;

	ssize_t bytes_written = write(log_fd, msg, strlen(msg)); // woods
	if (bytes_written == -1) {
		fprintf(stderr, "Error writing to debug log: %s\n", strerror(errno));
}
}


/*
================
Con_Printf

Handles cursor positioning, line wrapping, etc
================
*/
#define	MAXPRINTMSG	4096
void Con_Printf (const char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];
	char		demsg[MAXPRINTMSG]; // woods dequake
	static qboolean	inupdate;

	va_start (argptr, fmt);
	q_vsnprintf (msg, sizeof(msg), fmt, argptr);
	va_end (argptr);

	va_start(argptr, fmt); // woods dequake
	q_vsnprintf(demsg, sizeof(demsg), fmt, argptr);
	va_end(argptr);

	if (con_redirect_flush)
		q_strlcat(con_redirect_buffer, msg, sizeof(con_redirect_buffer));

// also echo to debugging console
	Sys_Printf ("%s", msg);

	unsigned char* ch; // woods dequake
	for (ch = (unsigned char*)demsg; *ch; ch++)
		*ch = dequake[*ch];

	// log all messages to file
	if (con_debuglog)
		Con_DebugLog(demsg); // woods dequake

	if (!con_initialized)
		return;

	if (cls.state == ca_dedicated)
		return;		// no graphics mode

// write it to the scrollable buffer
	Con_Print (msg);

// update the screen if the console is displayed
	if (cls.signon != SIGNONS && !scr_disabled_for_loading && !qcvm)
	{
	// protect against infinite loop if something in SCR_UpdateScreen calls
	// Con_Printd
		if (!inupdate)
		{
			inupdate = true;
			SCR_UpdateScreen ();
			inupdate = false;
		}
	}
}

/*
================
Con_DWarning -- ericw
 
same as Con_Warning, but only prints if "developer" cvar is set.
use for "exceeds standard limit of" messages, which are only relevant for developers
targetting vanilla engines
================
*/
void Con_DWarning (const char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	if (developer.value >= 2)
	{	// don't confuse non-developers with techie stuff...
		// (this is limit exceeded warnings)

		va_start (argptr, fmt);
		q_vsnprintf (msg, sizeof(msg), fmt, argptr);
		va_end (argptr);

		Con_SafePrintf ("\x02Warning: ");
		Con_Printf ("%s", msg);
	}
}

/*
================
Con_Warning -- johnfitz -- prints a warning to the console
================
*/
void Con_Warning (const char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start (argptr, fmt);
	q_vsnprintf (msg, sizeof(msg), fmt, argptr);
	va_end (argptr);

	Con_SafePrintf ("\x02Warning: ");
	Con_Printf ("%s", msg);
}

/*
================
Con_DPrintf

A Con_Printf that only shows up if the "developer" cvar is set
================
*/
void Con_DPrintf (const char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	if (!developer.value)
		return;			// don't confuse non-developers with techie stuff...

	va_start (argptr, fmt);
	q_vsnprintf (msg, sizeof(msg), fmt, argptr);
	va_end (argptr);

	Con_SafePrintf ("%s", msg); //johnfitz -- was Con_Printf
}

/*
================
Con_DPrintf2 -- johnfitz -- only prints if "developer" >= 2

currently not used
================
*/
void Con_DPrintf2 (const char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	if (developer.value >= 2)
	{
		va_start (argptr, fmt);
		q_vsnprintf (msg, sizeof(msg), fmt, argptr);
		va_end (argptr);
		Con_Printf ("%s", msg);
	}
}


/*
==================
Con_SafePrintf

Okay to call even when the screen can't be updated
==================
*/
void Con_SafePrintf (const char *fmt, ...)
{
	va_list		argptr;
	char		msg[1024];
	int		temp;

	va_start (argptr, fmt);
	q_vsnprintf (msg, sizeof(msg), fmt, argptr);
	va_end (argptr);

	temp = scr_disabled_for_loading;
	scr_disabled_for_loading = true;
	Con_Printf ("%s", msg);
	scr_disabled_for_loading = temp;
}

/*
================
Con_CenterPrintf -- johnfitz -- pad each line with spaces to make it appear centered
================
*/
void Con_CenterPrintf (int linewidth, const char *fmt, ...) FUNC_PRINTF(2,3);
void Con_CenterPrintf (int linewidth, const char *fmt, ...)
{
	va_list	argptr;
	char	msg[MAXPRINTMSG]; //the original message
	char	line[MAXPRINTMSG]; //one line from the message
	char	spaces[21]; //buffer for spaces
	char	*src, *dst;
	int		len, s;

	va_start (argptr, fmt);
	q_vsnprintf (msg, sizeof(msg), fmt, argptr);
	va_end (argptr);

	linewidth = q_min(linewidth, con_linewidth);
	for (src = msg; *src; )
	{
		dst = line;
		while (*src && *src != '\n')
			*dst++ = *src++;
		*dst = 0;
		if (*src == '\n')
			src++;

		len = strlen(line);
		if (len < linewidth)
		{
			s = (linewidth-len)/2;
			memset (spaces, ' ', s);
			spaces[s] = 0;
			Con_Printf ("%s%s\n", spaces, line);
		}
		else
			Con_Printf ("%s\n", line);
	}
}

// woods -- improve centerprint logging with notification awareness #centerlog

static int con_centerprint_start = -1;
static int con_centerprint_end = -1;
static qboolean centerprint_pending = false;
static char centerprint_pending_text[MAXPRINTMSG];
static SDL_TimerID centerprint_timer_id = 0;

/*
================
Con_HasActiveNotifications -- Check if any notifications are still visible -- woods #centerlog
================
*/
static qboolean Con_HasActiveNotifications(void)
{
	// Clamp the number of lines to between 0 and NUM_CON_TIMES.
	int maxlines = CLAMP(0, con_notifylines.value, NUM_CON_TIMES);
	double current_time = realtime;  // Cache realtime for consistency

	// Calculate the first index to check (ensure it is not negative).
	int start = con_current - maxlines + 1;
	if (start < 0) {
		start = 0;
	}

	// Iterate over the recent notification entries.
	for (int i = start; i <= con_current; i++)
	{
		double notify_time = con_times[i % NUM_CON_TIMES];
		// If this notification is active, return true.
		if (notify_time && current_time < notify_time + con_notifytime.value)
		{
			return true;
		}
	}

	return false;
}

/*
================
Con_ExecuteCenterPrint -- Actually perform the centerprint -- woods #centerlog
================
*/
static void Con_ExecuteCenterPrint(const char* str)
{
	if (con_logcenterprint.value)
	{
		con_centerprint_start = con_current + 1;
		Con_Printf("%s", Con_Quakebar(40));
		Con_CenterPrintf(40, "%s\n", str);
		Con_Printf("%s", Con_Quakebar(40));
		con_centerprint_end = con_current;

		for (int i = con_centerprint_start; i <= con_centerprint_end; i++)
		{
			con_times[i % NUM_CON_TIMES] = 0;
		}
	}

	// Clean up timer when centerprint is executed
	if (centerprint_timer_id)
	{
		SDL_RemoveTimer(centerprint_timer_id);
		centerprint_timer_id = 0;
	}
	centerprint_pending = false;
}

/*
================
CenterPrint_TimerCallback -- Check if notifications have cleared - woods #centerlog
================
*/
static Uint32 CenterPrint_TimerCallback(Uint32 interval, void* param)
{
	if (!centerprint_pending)  // Add early exit
		return 0;

	if (!Con_HasActiveNotifications())
	{
		Con_ExecuteCenterPrint(centerprint_pending_text);
		return 0;  // Don't repeat
	}
	return 100;  // Check again in 100ms
}

/*
==================
Con_LogCenterPrint -- johnfitz -- echo centerprint message to the console - woods #centerlog
==================
*/
void Con_LogCenterPrint (const char *str)
{
	if (!strcmp(str, con_lastcenterstring))
		return; //ignore duplicates

	if (cl.gametype == GAME_DEATHMATCH && con_logcenterprint.value != 2)
		return; //don't log in deathmatch

	q_strlcpy(con_lastcenterstring, str, sizeof(con_lastcenterstring));

	if (!con_logcenterprint.value)
		return;

	// Store the pending centerprint
	q_strlcpy(centerprint_pending_text, str, sizeof(centerprint_pending_text));
	centerprint_pending = true;

	// Cancel any existing timer
	if (centerprint_timer_id)
	{
		SDL_RemoveTimer(centerprint_timer_id);
		centerprint_timer_id = 0;  // Add null assignment
	}

	// If no notifications are active, print immediately.
	if (!Con_HasActiveNotifications())
	{
		Con_ExecuteCenterPrint(str);
	}
	else
	{
		// Start a timer to periodically check if notifications have cleared.
		centerprint_timer_id = SDL_AddTimer(100, CenterPrint_TimerCallback, NULL);
		if (!centerprint_timer_id)
	{
			// If timer creation fails, fall back to executing immediately.
			Con_ExecuteCenterPrint(str);
	}
}
}

qboolean Con_IsRedirected(void)
{
	return !!con_redirect_flush;
}
void Con_Redirect(void(*flush)(const char *))
{
	if (con_redirect_flush)
		con_redirect_flush(con_redirect_buffer);
	*con_redirect_buffer = 0;
	con_redirect_flush = flush;
}

/*
==============================================================================

	TAB COMPLETION

==============================================================================
*/

//johnfitz -- tab completion stuff
//unique defs
char key_tabpartial[MAXCMDLINE];
typedef struct tab_s
{
	const char	*name;
	char date[20]; // woods #demolistsort
	const char	*type;
	struct tab_s	*next;
	struct tab_s	*prev;
	int			count; // woods #iwtabcomplete
} tab_t;
tab_t	*tablist;

//defs from elsewhere
extern qboolean	keydown[256];
extern	cmd_function_t	*cmd_functions;
#define	MAX_ALIAS_NAME	32
typedef struct cmdalias_s
{
	struct cmdalias_s	*next;
	char	name[MAX_ALIAS_NAME];
	char	*value;
} cmdalias_t;
extern	cmdalias_t	*cmd_alias;

/*
============
Con_AddToTabList -- johnfitz // woods #iwtabcomplete

tablist is a doubly-linked loop, alphabetized by name
============
*/

// bash_partial is the string that can be expanded,
// aka Linux Bash shell. -- S.A.
static char	bash_partial[80];
static qboolean	bash_singlematch;

void Con_AddToTabList (const char* name, const char* partial, const char* type, const char* param) // woods #iwtabcomplete -- added extra arg for dynamic list type (ie demo vs sky/map etc) #demolistsort
{
	tab_t* t, * insert;
	char* i_bash, * i_bash2;
	const char* i_name, * i_name2;
	int		len, mark;

	if (!Con_Match (name, partial))
		return;

	int FileIsDemo = (param != NULL); // woods #demolistsort

	if (!*bash_partial && bash_singlematch)
	{
		q_strlcpy (bash_partial, name, sizeof(bash_partial));
	}
	else
	{
		bash_singlematch = 0;
		i_bash = q_strcasestr (bash_partial, partial);
		i_name = q_strcasestr (name, partial);
		SDL_assert (i_bash);
		SDL_assert (i_name);
		if (i_name && i_bash)
		{
			i_bash2 = i_bash;
			i_name2 = i_name;
			// find max common between bash_partial and name (right side)
			while (*i_bash && q_toupper (*i_bash) == q_toupper (*i_name))
			{
				i_bash++;
				i_name++;
			}
			*i_bash = 0;
			// find max common between bash_partial and name (left side)
			while (i_bash2 != bash_partial && i_name2 != name &&
				q_toupper (i_bash2[-1]) == q_toupper (i_name2[-1]))
			{
				i_bash2--;
				i_name2--;
			}
			if (i_bash2 != bash_partial)
				memmove (bash_partial, i_bash2, strlen (i_bash2) + 1);
		}
	}

	mark = Hunk_LowMark ();
	len = strlen (name);
	t = (tab_t*)Hunk_AllocName (sizeof(tab_t) + len + 1, "tablist");
	memcpy (t + 1, name, len + 1);
	t->name = (const char*)(t + 1);
	t->type = type;
	t->count = 1;
	if (param)
	{
		strncpy(t->date, param, sizeof(t->date) - 1); // Copy the date
		t->date[sizeof(t->date) - 1] = '\0'; // Ensure null termination
	}

	if (!tablist) //create list
	{
		tablist = t;
		t->next = t;
		t->prev = t;
	}
	else if (FileIsDemo ? (q_sortdemos(param, tablist->date) < 0) : (q_strnaturalcmp(name, tablist->name) < 0)) // Insert at front -- woods #demolistsort
	{
		t->next = tablist;
		t->prev = tablist->prev;
		t->next->prev = t;
		t->prev->next = t;
		tablist = t;
	}
	else //insert later
	{
		insert = tablist;
		do
		{
			int cmp = FileIsDemo ? q_sortdemos(t->date, insert->date) : q_strnaturalcmp(name, insert->name);  // woods #demolistsort
			if (!cmp && !strcmp(name, insert->name)) // avoid duplicates
			{
				Hunk_FreeToLowMark (mark);
				insert->count++;
				return;
			}
			if (cmp < 0)
				break;
			insert = insert->next;
		} while (insert != tablist);

		t->next = insert;
		t->prev = insert->prev;
		t->next->prev = t;
		t->prev->next = t;
	}
}

/*
============
Con_Match -- woods #iwtabcomplete
============
*/
qboolean Con_Match (const char* str, const char* partial)
{
	return q_strcasestr(str, partial) != NULL;
}

/*
============
ParseCommand -- woods #iwtabcomplete
============
*/
static const char* ParseCommand (void)
{
	char buf[MAXCMDLINE];
	const char* str = key_lines[edit_line] + 1;
	const char* end = str + key_linepos - 1;
	const char* ret = str;
	const char* quote = NULL;

	while (*str && str != end)
	{
		char c = *str++;
		if (c == '\"')
		{
			if (!quote)
			{
				quote = ret; // save previous command boundary
				ret = str; // new command
			}
			else
			{
				ret = quote; // restore saved cursor
				quote = NULL;
			}
		}
		else if (c == ';')
			ret = str;
		else if (!quote && c == '/' && *str == '/')
			break;
	}

	while (*ret == ' ')
		ret++;

	q_strlcpy (buf, ret, sizeof(buf));
	if ((uintptr_t)(end - ret) < sizeof(buf))
		buf[end - ret] = '\0';
	end = buf + strlen(buf);

	Cmd_TokenizeString (buf);
	// last arg should always be the one we're trying to complete,
	// so we add a new empty one if the command ends with a space
	if (end != buf && end[-1] == ' ')
		Cmd_AddArg ("");

	return ret;
}

static qboolean CompleteFileList (const char* partial, void* param) // woods #iwtabcomplete
{
	if (Cmd_Argc() != 2)
		return false;
	
	filelist_item_t* file, ** list = (filelist_item_t**)param;
	for (file = *list; file; file = file->next)
		Con_AddToTabList (file->name, partial, NULL, NULL);
	return true;
}

static qboolean CompleteClassnames (const char* partial, void* unused) // woods #iwtabcomplete #iwshowbboxes
{
	extern edict_t* sv_player;
	edict_t* ed;
	int		i;

	if (!sv.active)
		return true;
	PR_SwitchQCVM(&sv.qcvm);

	for (i = 1, ed = NEXT_EDICT(qcvm->edicts); i < qcvm->num_edicts; i++, ed = NEXT_EDICT(ed))
	{
		const char* name;
		if (ed == sv_player || ed->free || !ed->v.classname)
			continue;
		name = PR_GetString(ed->v.classname);
		if (*name)
			Con_AddToTabList(name, partial, "#", NULL);
	}

	PR_SwitchQCVM(NULL);

	return true;
}

static qboolean CompleteFileListDemo (const char* partial, void* param) // woods #iwtabcomplete #demolistsort
{
	filelist_item_t* file, ** list = (filelist_item_t**)param;
	char currentDateStr[80];

	// Get current date/time for the -last option
	time_t now = time(NULL);
	struct tm* tm_now = localtime(&now);

	if (tm_now)
	{
		q_snprintf(currentDateStr, sizeof(currentDateStr), "%04d-%02d-%02d %02d:%02d:%02d",
			tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday,
			tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);
	}
	else
	{
		Q_strncpy(currentDateStr, "current", sizeof(currentDateStr) - 1);
		currentDateStr[sizeof(currentDateStr) - 1] = '\0';
	}

	Con_AddToTabList("-l", partial, "play last demo", currentDateStr);

	for (file = *list; file; file = file->next)
		Con_AddToTabList (file->name, partial, NULL, file->data);
	return true;
}

#define MAX_LENGTH 60 // woods #iwtabcomplete
char truncatedStrings[MAX_KEYS][MAX_LENGTH] = { 0 }; // woods #iwtabcomplete

static void ProcessKeyBinding (int i, const char* partial) // woods #iwtabcomplete
{
	const char* name = Key_KeynumToString(i);
	if (strcmp(name, "<UNKNOWN KEYNUM>") != 0)
	{
		char* keybindingValue = keybindings[0][i];
		if (keybindingValue && strlen(keybindingValue) > MAX_LENGTH) 
		{
			int copyLength = MAX_LENGTH - 4; 
			Q_strncpy(truncatedStrings[i], keybindingValue, copyLength);
			truncatedStrings[i][copyLength] = '\0';
			Q_strcat(truncatedStrings[i], "...");
			keybindingValue = truncatedStrings[i];
		}
		Con_AddToTabList(name, partial, keybindingValue, NULL);
	}
}

static qboolean CompleteBindKeys (const char* partial, void* unused) // woods #iwtabcomplete
{
	if (Cmd_Argc() > 2) return false;
	for (int i = 0; i < MAX_KEYS; i++) 
	{
		ProcessKeyBinding (i, partial);
	}
	return true;
}

static qboolean CompleteBoundKeys(const char* partial, void* unused)
{
    if (Cmd_Argc() > 2) return false;

    for (int i = 0; i < MAX_KEYS; i++)
    {
        char* keybindingValue = keybindings[0][i];

        if (keybindingValue && keybindingValue[0] != '\0')
        {
            ProcessKeyBinding(i, partial);
        }
    }
    return true;
}

static qboolean CompleteUnbindKeys (const char* partial, void* unused) // woods #iwtabcomplete
{
	for (int i = 0; i < MAX_KEYS; i++)
	{
		ProcessKeyBinding (i, partial);
	}
	return true;
}

static qboolean CompleteViewpos (const char* partial, void* unused) // woods
{
	if (Cmd_Argc() != 2)
		return false;
	Con_AddToTabList("copy", partial, NULL, NULL); // #demolistsort add arg

	return true;
}

static qboolean CompleteSetpos(const char* partial, void* unused) // woods
{
	if (Cmd_Argc() != 2)
		return false;
	
	extern qboolean has_last_viewpos;

	if (has_last_viewpos)
		Con_AddToTabList("last", partial, NULL, NULL); // #demolistsort add arg

	return true;
}

static qboolean CompleteAliasEditList(const char* partial, void* unused) // woods #iwtabcomplete
{
	cmdalias_t* alias;

	if (Cmd_Argc() != 2)
		return false;

	// Start from the first alias in the linked list
	for (alias = cmd_alias; alias; alias = alias->next)
		Con_AddToTabList(alias->name, partial, "alias", NULL); // #demolistsort add arg

	return true;
}

static qboolean CompleteCvarList (const char* partial, void* unused) // woods #iwtabcomplete
{
	cvar_t* cvar;

	if (Cmd_Argc() != 2)
		return false;

	cvar = Cvar_FindVarAfter("", CVAR_NONE);
	for (; cvar; cvar = cvar->next)
			Con_AddToTabList(cvar->name, partial, "cvar", NULL); // #demolistsort add arg
	
	return true;
}

static qboolean CompleteCvarArcList(const char* partial, void* unused) // woods #iwtabcomplete
{
	cvar_t* cvar;

	if (Cmd_Argc() != 2)
		return false;

	cvar = Cvar_FindVarAfter("", CVAR_NONE);
	for (; cvar; cvar = cvar->next)
	{
		if (!(cvar->flags & CVAR_ARCHIVE))
			Con_AddToTabList(cvar->name, partial, "cvar", NULL);
	}

	return true;
}

static qboolean CompleteCommandList (const char* partial, void* unused) // woods #iwtabcomplete
{
	cmd_function_t* cmd;

	if (Cmd_Argc() >= 4)
		return false;

	for (cmd = cmd_functions; cmd; cmd = cmd->next)
		Con_AddToTabList (cmd->name, partial, "command", NULL); // #demolistsort add arg

	return true;
}

static qboolean CompleteGeneralList (const char* partial, void* unused) // woods #iwtabcomplete
{
	cmdalias_t* alias;
	cvar_t* cvar;
	cmd_function_t* cmd;

	if (Cmd_Argc() != 2)
		return false;

	cvar = Cvar_FindVarAfter("", CVAR_NONE);
	for (; cvar; cvar = cvar->next)
		Con_AddToTabList (cvar->name, partial, "cvar", NULL); // #demolistsort add arg

	for (cmd = cmd_functions; cmd; cmd = cmd->next)
		Con_AddToTabList (cmd->name, partial, "command", NULL); // #demolistsort add arg

	for (alias = cmd_alias; alias; alias = alias->next)
		Con_AddToTabList (alias->name, partial, "alias", NULL); // #demolistsort add arg

	return true;
}

static qboolean CompleteScreenshotList (const char* partial, void* unused) // woods #iwtabcomplete
{
	if (Cmd_Argc() == 2)
	{
		const char* fileTypes[] = { "jpg", "png", "tga" };
		const int fileTypeCount = sizeof(fileTypes) / sizeof(fileTypes[0]); // Calculate the number of file types

		for (int i = 0; i < fileTypeCount; ++i)
			Con_AddToTabList (fileTypes[i], partial, NULL, NULL); // #demolistsort add arg
	}

	else if (Cmd_Argc() == 3)
	{
		for (int num = 1; num <= 100; ++num)
		{
			char numStr[4];
			snprintf(numStr, sizeof(numStr), "%d", num);

			Con_AddToTabList (numStr, partial, NULL, NULL); // #demolistsort add arg
		}
	}

	return true;
}

static qboolean CompleteWriteCfg (const char* partial, void* unused) // woods #iwtabcomplete
{
	if (Cmd_Argc() != 2)
		return false;
	
	char str[30];
	time_t systime = time(0);
	struct tm loct = *localtime(&systime);


	strftime(str, 30, "config-%m-%d-%Y-%H%M%S.cfg", &loct);
	
	Con_AddToTabList (str, partial, NULL, NULL); // #demolistsort add arg
	Con_AddToTabList ("config.cfg", partial, NULL, NULL); // #demolistsort add arg
	Con_AddToTabList ("qssm.cfg", partial, NULL, NULL); // #demolistsort add arg


	return true;
}

static qboolean CompleteCurrentMap(const char* partial, void* unused) // woods #locext
{
	if (Cmd_Argc() != 2)
		return false;

	if (cls.state == ca_connected)
		Con_AddToTabList(cl.mapname, partial, NULL, NULL);

	return true;
}

static qboolean CompleteAddLoc(const char* partial, void* unused) // woods #locext
{
	if (Cmd_Argc() != 2)
		return false;

	// Only process if connected
	if (cls.state == ca_connected)
	{
		const char* Names[] = {
			"auto",          "blue-flag",    "blue-ga",       "blue-gl",
			"blue-lg",       "blue-mega",    "blue-ng",       "blue-pent",
			"blue-quad",     "blue-ra",      "blue-ring",     "blue-rl",
			"blue-sng",      "blue-suit",    "blue-ssg",      "blue-tele",
			"blue-tele-exit","blue-ya",      "blue-base",     "ga",
			"gl",            "lg",           "mega",          "mid",
			"ng",            "pent",         "quad",          "ra",
			"red-flag",      "red-ga",       "red-gl",        "red-lg",
			"red-mega",      "red-ng",       "red-pent",      "red-quad",
			"red-ra",        "red-ring",     "red-rl",        "red-sng",
			"red-suit",      "red-ssg",      "red-tele",      "red-tele-exit",
			"red-ya",        "red-base",     "ring",          "rl",
			"sng",           "ssg",          "suit",          "tele",
			"tele-exit",     "ya",
		};

		for (int i = 0; i < sizeof(Names) / sizeof(Names[0]); i++)
		{
			Con_AddToTabList(Names[i], partial, NULL, NULL);
		}
	}

	return true;
}

static qboolean CompleteProtocols(const char* partial, void* unused)
{
	if (Cmd_Argc() != 2)
		return false;

	Con_AddToTabList("Base-15", partial, "netquake", NULL);      // PROTOCOL_NETQUAKE
	Con_AddToTabList("Base-666", partial, "fitzquake", NULL);     // PROTOCOL_FITZQUAKE
	Con_AddToTabList("Base-999", partial, "rmq", NULL);     // PROTOCOL_RMQ
	Con_AddToTabList("Base-10002", partial, "bjp3", NULL);   // PROTOCOL_VERSION_BJP3
	Con_AddToTabList("Base-3504", partial, "dp", NULL);    // PROTOCOL_VERSION_DP7

	Con_AddToTabList("FTE+15", partial, "netquake+pext", NULL);      // PROTOCOL_NETQUAKE with FTE extensions
	Con_AddToTabList("FTE+666", partial, "fitzquake+pext", NULL);     // PROTOCOL_FITZQUAKE with FTE extensions
	Con_AddToTabList("FTE+999", partial, "rmq+pext", NULL);     // PROTOCOL_RMQ with FTE extensions
	Con_AddToTabList("FTE+10002", partial, "bjp3+pext", NULL);   // PROTOCOL_VERSION_BJP3 with FTE extensions
	Con_AddToTabList("FTE+3504", partial, "dp+pext", NULL);    // PROTOCOL_VERSION_DP7 with FTE extensions

	return true;
}

static qboolean CompleteDownload(const char* partial, void* unused) // woods
{
	if (Cmd_Argc() != 2)
		return false;

	Con_AddToTabList("ctf", partial, NULL, NULL); // #demolistsort add arg
	Con_AddToTabList("ra", partial, NULL, NULL);  // #demolistsort add arg

	const char* maps[] =
	{
		"aggressr.bsp",
		"aerowalk.bsp",
		"aerowalkfrost.bsp",
		"aztek.bsp",
		"boom.bsp",
		"bravado.bsp",
		"castlev2.bsp",
		"ctf9.bsp",
		"kaboom.bsp",
		"nova.bsp",
		"pigremix.bsp",
		"pocket.bsp",
		"povdmm4.bsp",
		"schloss.bsp",
		"shifter_nq.bsp",
		"skull.bsp",
		"ztndm3.bsp"
	};

	for (int i = 0; i < sizeof(maps) / sizeof(maps[0]); ++i) {
		char path[256];
		snprintf(path, sizeof(path), "maps/%s", maps[i]);
		if (!COM_FileExists(path, NULL))
			Con_AddToTabList(maps[i], partial, NULL, NULL); // #demolistsort add arg
	}

	return true;
}

static qboolean CompleteLS(const char* partial, void* unused) // woods
{
	if (Cmd_Argc() != 2)
		return false;

	// Common patterns to suggest
	const char* patterns[] = {
		"*.pak",
		"*.txt",
		"*",
		"pak1.pak",
		"?onfig.cfg",
		"config*",
		"*.bsp",
		"maps/*.bsp",
		"sound/*.wav",
		"models/*.mdl"
	};

	int i;
	int num_patterns = sizeof(patterns) / sizeof(patterns[0]);

	for (i = 0; i < num_patterns; ++i) {
		if (strncmp(partial, patterns[i], strlen(partial)) == 0) 
			Con_AddToTabList(patterns[i], partial, NULL, NULL);
	}

	return true;
}

static qboolean CompleteIP(const char* partial, void* unused) // woods
{
	if (Cmd_Argc() != 2)
		return false;
	Con_AddToTabList("ext", partial, NULL, NULL); // #demolistsort add arg
	Con_AddToTabList("local", partial, NULL, NULL); // #demolistsort add arg

	return true;
}

static qboolean CompleteClients(const char* partial, void* unused) // woods
{
	extern char unfun[129];

	if (Cmd_Argc() != 2)
		return false;

	// Only suggest names if we're connected to a server
	if (cls.state != ca_connected && cl.maxclients < 1)
		return false;

	// Check if partial has special chars
	char unfun_partial[32];
	qboolean partial_has_special = false;
	int i;

	for (i = 0; partial[i] && i < 31; i++) {
		if (partial[i] != ' ' && partial[i] != unfun[partial[i] & 127]) {
			partial_has_special = true;
			break;
		}
	}

	// Only convert to unfun if special chars present
	if (partial_has_special) {
		for (i = 0; partial[i] && i < 31; i++)
			unfun_partial[i] = unfun[partial[i] & 127];
		unfun_partial[i] = '\0';
	}

	// Loop through all possible players
	for (i = 0; i < cl.maxclients; i++)
	{
		scoreboard_t* s = &cl.scores[i];
		const char* colored_name = s->name;
		char unfun_name[32];
		qboolean has_special_chars = false;

		// Skip empty slots
		if (!colored_name[0])
			continue;

		// Scan the entire name first to find the true last character
		int true_end = 0;
		for (int j = 0; colored_name[j] && j < 31; j++)
		{
			if (colored_name[j] != ' ')
				true_end = j;
		}

		// Now scan up to true_end to find any large gaps
		int last_char = 0;
		int space_count = 0;
		int max_allowed_spaces = 1;

		for (int j = 0; j <= true_end && j < 31; j++)
		{
			if (colored_name[j] == ' ')
			{
				space_count++;
				if (space_count > max_allowed_spaces)
				{
					break;
				}
			}
			else
			{
				last_char = j;
				space_count = 0;
			}
		}

		// Create permanent copy of trimmed name
		int trim_len = q_min(last_char + 1, 15);
		char* permanent_name = (char*)Hunk_AllocName(trim_len + 1, "tabname");
		memcpy(permanent_name, colored_name, trim_len);
		permanent_name[trim_len] = '\0';

		// First check if name has any special chars
		for (int j = 0; permanent_name[j] && j < trim_len; j++)
		{
			if (permanent_name[j] != ' ' && permanent_name[j] != unfun[permanent_name[j] & 127])
			{
				has_special_chars = true;
				break;
			}
		}

		// Only do unfun conversion if we have special chars
		if (has_special_chars)
		{
			for (int j = 0; permanent_name[j] && j < trim_len; j++)
			{
				unfun_name[j] = unfun[permanent_name[j] & 127];
			}
			unfun_name[trim_len] = '\0';

			if (q_strcasestr(unfun_name, partial_has_special ? unfun_partial : partial))
			{
				Con_AddToTabList(unfun_name, partial, permanent_name, NULL);
			}
		}
		else
		{
			// For normal names, just do direct comparison
			if (q_strcasestr(permanent_name, partial))
			{
				Con_AddToTabList(permanent_name, partial, NULL, NULL);
			}
		}
	}

	return true;
}

/*
================
GetTimeStampedName
================
*/
static const char* GetTimeStampedName(void)
{
	static char suggestion[MAX_OSPATH];
	char str[24];
	time_t systime = time(0);
	struct tm loct = *localtime(&systime);

	strftime(str, sizeof(str), "%m-%d-%Y-%H%M%S", &loct);
	q_snprintf(suggestion, sizeof(suggestion), "%s_%s", cl.mapname, str);

	return suggestion;
}

/*
================
CompleteRecord
================
*/
static qboolean CompleteRecord(const char* partial, void* unused)
{
	if (Cmd_Argc() != 2)
		return false;

	// Only provide completion if we're connected to a map
	if (cls.state != ca_connected)
		return false;

	Con_AddToTabList(GetTimeStampedName(), partial, NULL, NULL);
	return true;
}

/*
================
CompleteSave
================
*/
static qboolean CompleteSave(const char* partial, void* unused)
{
	if (Cmd_Argc() != 2)
		return false;

	// Only provide completion if we're connected to a map in single player/coop
	if (cls.state != ca_connected || cl.gametype == GAME_DEATHMATCH)
		return false;

	Con_AddToTabList(GetTimeStampedName(), partial, NULL, NULL);
	return true;
}

/*
================
GetSaveMapName -- Read mapname from save file
================
*/
static const char* GetSaveMapName(const char* filepath)
{
	static char mapname[MAX_QPATH];
	FILE* f;
	int version;
	float time;
	int j;
	char name[MAX_OSPATH];

	mapname[0] = 0;
	f = fopen(filepath, "r");
	if (!f)
		return mapname;

	// Read version and name
	if (fscanf(f, "%i\n", &version) != 1 ||
		fscanf(f, "%79s\n", name) != 1)
	{
		fclose(f);
		return mapname;
	}

	// Read spawn parms
	for (j = 0; j < NUM_BASIC_SPAWN_PARMS; j++) {
		if (fscanf(f, "%f\n", &time) != 1) {
			fclose(f);
			return mapname;
		}
	}

	// Read skill
	if (fscanf(f, "%f\n", &time) != 1) {
		fclose(f);
		return mapname;
	}

	// Read map name
	if (fscanf(f, "%63s\n", mapname) != 1) {
		fclose(f);
		return mapname;
	}

	fclose(f);
	return mapname;
}

static void SearchSaveFiles(const char* searchdir, const char* partial)
{
#ifdef _WIN32
	char searchpath[MAX_OSPATH];
	WIN32_FIND_DATA fdat;
	HANDLE fhnd;

	q_snprintf(searchpath, sizeof(searchpath), "%s/*.sav", searchdir);
	fhnd = FindFirstFile(searchpath, &fdat);
	if (fhnd != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (!(fdat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				char basename[MAX_OSPATH];
				char fullpath[MAX_OSPATH];
				COM_StripExtension(fdat.cFileName, basename, sizeof(basename));
				q_snprintf(fullpath, sizeof(fullpath), "%s/%s", searchdir, fdat.cFileName);
				Con_AddToTabList(basename, partial, GetSaveMapName(fullpath), NULL);
			}
		} while (FindNextFile(fhnd, &fdat));
		FindClose(fhnd);
	}
#else
	DIR* dir_p;
	struct dirent* dir_t;
	const char* ext = ".sav";

	dir_p = opendir(searchdir);
	if (dir_p)
	{
		while ((dir_t = readdir(dir_p)) != NULL)
		{
			if (strlen(dir_t->d_name) > 4 &&
				!q_strcasecmp(dir_t->d_name + strlen(dir_t->d_name) - 4, ext))
			{
				char basename[MAX_OSPATH];
				char fullpath[MAX_OSPATH];
				COM_StripExtension(dir_t->d_name, basename, sizeof(basename));
				q_snprintf(fullpath, sizeof(fullpath), "%s/%s", searchdir, dir_t->d_name);
				Con_AddToTabList(basename, partial, GetSaveMapName(fullpath), NULL);
			}
		}
		closedir(dir_p);
	}
#endif
}

static qboolean CompleteLoad(const char* partial, void* unused)
{
	if (Cmd_Argc() != 2)
		return false;

	char savedir[MAX_OSPATH];

	// Search in gamedir/saves
	q_snprintf(savedir, sizeof(savedir), "%s/saves", com_gamedir);
	SearchSaveFiles(savedir, partial);

	// Search in gamedir
	SearchSaveFiles(com_gamedir, partial);

	return true;
}

extern qboolean CompletePAKList(const char* partial, void* unused); // woods #unpak

qboolean CompleteImageList (const char* partial, void* unused); // woods
qboolean CompleteSoundList (const char* partial, void* unused); // woods
qboolean CompleteGive (const char* partial, void* unused); // woods #give+

typedef struct arg_completion_type_s // woods #iwtabcomplete
{
	const char* command;
	qboolean(*function) (const char* partial, void* param);
	void* param;
} arg_completion_type_t;

static const arg_completion_type_t arg_completion_types[] =
{
	{ "map",					CompleteFileList,		&extralevels },
	{ "maps",					CompleteFileList,		&extralevels },
	{ "changelevel",			CompleteFileList,		&extralevels },
	{ "loadloc",				CompleteFileList,		&extralevels },
	{ "entdump",				CompleteFileList,		&extralevels },
	{ "game",					CompleteFileList,		&modlist },
	{ "gamedir",				CompleteFileList,		&modlist },
	{ "playdemo",				CompleteFileListDemo,	&demolist },
	{ "timedemo",				CompleteFileListDemo,	&demolist },
	{ "sky",					CompleteFileList,		&skylist },
	{ "exec",					CompleteFileList,		&execlist },
	{ "connect",				CompleteFileList,		&serverlist },
	{ "test",					CompleteFileList,		&serverlist },
	{ "test2",					CompleteFileList,		&serverlist },
	{ "ping",					CompleteFileList,		&serverlist },
	{ "open",					CompleteFileList,		&folderlist },
	{ "music",					CompleteFileList,		&musiclist },
	{ "printtxt",				CompleteFileList,		&textlist },
	{ "r_showbboxes_filter",	CompleteClassnames,		NULL },
	{ "imagelist",				CompleteImageList,		NULL },
	{ "imagedump",				CompleteImageList,		NULL },
	{ "bind",					CompleteBindKeys,		NULL },
	{ "bindedit",				CompleteBoundKeys,		NULL },
	{ "unbind",					CompleteUnbindKeys,		NULL },
	{ "viewpos",				CompleteViewpos,		NULL },
	{ "setpos",					CompleteSetpos,			NULL },
	{ "reset",					CompleteCvarList,		NULL },
	{ "toggle",					CompleteCvarList,		NULL },
	{ "cycle",					CompleteCvarList,		NULL },
	{ "set",					CompleteCvarArcList,	NULL },
	{ "seta",					CompleteCvarArcList,	NULL },
	{ "cmdtoggle",				CompleteCommandList,	NULL },
	{ "if",						CompleteGeneralList,	NULL },
	{ "play",					CompleteSoundList,		NULL },
	{ "play2",					CompleteSoundList,		NULL },
	{ "playvol",				CompleteSoundList,		NULL },
	{ "screenshot",				CompleteScreenshotList,	NULL },
	{ "writeconfig",			CompleteWriteCfg,		NULL },
	{ "sv_protocol",			CompleteProtocols,		NULL },
	{ "saveloc",				CompleteCurrentMap,		NULL },
	{ "addloc",					CompleteAddLoc,			NULL },
	{ "aliasedit",				CompleteAliasEditList,	NULL },
	{ "download",				CompleteDownload,		NULL },
	{ "ls",						CompleteLS,				NULL },
	{ "dir",					CompleteLS,				NULL },
	{ "which",					CompleteLS,				NULL },
	{ "flocate",				CompleteLS,				NULL },
	{ "ip",						CompleteIP,				NULL },
	{ "unpak",					CompletePAKList,		NULL },
	{ "cmd",					CompleteGeneralList,	NULL },
	{ "identify",				CompleteClients,		NULL },
	{ "tell",					CompleteClients,		NULL },
	{ "record",					CompleteRecord,			NULL },
	{ "save",					CompleteSave,			NULL },
	{ "load",					CompleteLoad,			NULL },
	{ "give",					CompleteGive,			NULL }
};

static const int num_arg_completion_types =
sizeof (arg_completion_types) / sizeof (arg_completion_types[0]);

/*
============
BuildTabList -- johnfitz // woods #iwtabcomplete
============
*/
static void BuildTabList (const char* partial)
{
	cmdalias_t* alias;
	cvar_t* cvar;
	cmd_function_t* cmd;
	int				i;

	tablist = NULL;

	bash_partial[0] = 0;
	bash_singlematch = 1;

	ParseCommand();

	if (Cmd_Argc() >= 2)
	{
		cvar = Cvar_FindVar(Cmd_Argv(0));
		if (cvar)
		{
			// cvars can only have one argument
			if (Cmd_Argc() == 2 && cvar->completion)
				cvar->completion(cvar, partial);
			return;
		}

		cmd = Cmd_FindCommand(Cmd_Argv(0));
		if (cmd && cmd->completion)
		{
			cmd->completion(partial);
			return;
		}

		for (i = 0; i < num_arg_completion_types; i++)
		{
			// arg_completion contains a command we can complete the arguments
			// for (like "map") and a list of all the maps.
			arg_completion_type_t arg_completion = arg_completion_types[i];

			if (!q_strcasecmp(Cmd_Argv(0), arg_completion.command))
			{
				if (arg_completion.function(partial, arg_completion.param))
					return;
				break;
			}
		}
	}

	if (!*partial)
		return;

	cvar = Cvar_FindVarAfter("", CVAR_NONE);
	for (; cvar; cvar=cvar->next)
		if (q_strcasestr (cvar->name, partial))
			Con_AddToTabList (cvar->name, partial, "cvar", NULL); // #demolistsort add arg

	for (cmd=cmd_functions; cmd; cmd=cmd->next)
		if (cmd->srctype != src_server && q_strcasestr(cmd->name, partial) && !Cmd_IsReservedName(cmd->name))
			Con_AddToTabList (cmd->name, partial, "command", NULL); // #demolistsort add arg

	for (alias=cmd_alias; alias; alias=alias->next)
		if (q_strcasestr (alias->name, partial))
			Con_AddToTabList (alias->name, partial, "alias", NULL); // #demolistsort add arg
}

/*
============
Con_FormatTabMatch -- woods #consolecols (iw 85bf0e8)
============
*/
static void Con_FormatTabMatch (const tab_t* t, char* dst, size_t dstsize)
{
	char tinted[MAXCMDLINE];

	COM_TintSubstring(t->name, bash_partial, tinted, sizeof(tinted));

	if (!t->type)
		q_strlcpy(dst, tinted, dstsize);
	else if (t->type[0] == '#' && !t->type[1])
		q_snprintf(dst, dstsize, "%s (%d)", tinted, t->count);
	else
		q_snprintf(dst, dstsize, "%s (%s)", tinted, t->type);
}

/*
============
GetTabAtIndex -- woods #consolecols
============
*/
static tab_t* GetTabAtIndex (tab_t* list, int index)
{
	if (list == NULL || index < 0)
		return NULL;

	tab_t* current = list;
	for (int i = 0; i < index; i++)
	{
		current = current->next;
		if (current == list) // If we reach the start of the list again
			return NULL;    // Index is out of bounds
	}
	return current;
}

/*
============
Con_PrintTabList -- woods #consolecols (iw 85bf0e8)
============
*/
static void Con_PrintTabList(void)
{
	char    buf[MAXCMDLINE];
	int     i, j, maxlen, cols, rows, matches, total, itemCount;
	tab_t* t;

	// determine maximum item length
	matches = maxlen = 0;
	t = tablist;
	do 
	{
		Con_FormatTabMatch(t, buf, sizeof(buf));
		int total = (int)strlen(buf);
		maxlen = q_max(maxlen, total);
		t = t->next;
		++matches;
	} while (t != tablist);

	// determine number of columns
	if (!maxlen)
		return;
	maxlen += 3;                                        // indent
	maxlen = q_max(maxlen, 8);                          // min width
	maxlen = (maxlen + 3) & ~3;                         // round up to multiple of 4
	cols = q_max(con_linewidth, maxlen) / maxlen;
	if (con_colmax.value >= 1.f)
		cols = q_min(cols, (int)con_colmax.value);     // apply user limit
	if (matches < 6)									// single column if fewer than 6 matches
		cols = 1;

	if (con_coldirection.value == 1)
	{
		// Original method: Left to right, then top to bottom
		Con_SafePrintf("\n");
		i = total = 0;
		t = tablist;
		do {
			Con_FormatTabMatch(t, buf, sizeof(buf));
			if (++i == cols) 
			{
				i = 0;
				Con_SafePrintf("   %s\n", buf);
			}
			else {
				Con_SafePrintf("   %*s", -(maxlen - 3), buf);
			}
			if (t->type && t->type[0] == '#' && !t->type[1])
				total += t->count;
			t = t->next;
		} while (t != tablist);
		if (i != 0)
			Con_SafePrintf("\n");
	}
	else
	{
		// Count total items
		itemCount = 0;
		t = tablist;
		do {
			itemCount++;
			t = t->next;
		} while (t != tablist);

		rows = (itemCount + cols - 1) / cols; // Calculate the number of rows

		// Print all matches in top-to-bottom, then left-to-right order
		Con_SafePrintf("\n");
		total = 0;
		for (i = 0; i < rows; i++) 
		{
			for (j = 0; j < cols; j++) 
			{
				int index = j * rows + i;
				if (index < itemCount) 
				{
					t = GetTabAtIndex
					(tablist, index);
					Con_FormatTabMatch(t, buf, sizeof(buf));
					Con_SafePrintf(" %*s", -(maxlen - 3), buf);
					if (t->type && t->type[0] == '#' && !t->type[1])
						total += t->count;
				}
			}
			Con_SafePrintf("\n");
		}
	}

		if (total > 0)
			Con_SafePrintf("   %d unique matches (%d total)\n", matches, total);

	Con_SafePrintf("\n");
}

/*
============
Con_TabComplete -- johnfitz -- woods #iwtabcomplete
============
*/
void Con_TabComplete (tabcomplete_t mode)
{
	char	partial[MAXCMDLINE];
	const char* match;
	static char* c;
	tab_t* t;
	int		mark, i;

	key_tabhint[0] = '\0';
	if (mode == TABCOMPLETE_AUTOHINT)
	{
		key_tabpartial[0] = '\0';

		if (key_lines[edit_line][1] == ' ') // woods no auto hints if leading space for chatting from console
			return;

		// only show completion hint when the cursor is at the end of the line
		if ((size_t)key_linepos >= sizeof(key_lines[edit_line]) || key_lines[edit_line][key_linepos])
			return;
	}

	// if editline is empty, return
	if (key_lines[edit_line][1] == 0)
		return;

	// get partial string (space -> cursor)
	if (!key_tabpartial[0]) //first time through, find new insert point. (Otherwise, use previous.)
	{
		//work back from cursor until you find a space, quote, semicolon, or prompt
		c = key_lines[edit_line] + key_linepos - 1; //start one space left of cursor
		while (*c!=' ' && *c!='\"' && *c!=';' && c!=key_lines[edit_line])
			c--;
		c++; //start 1 char after the separator we just found
	}
	for (i = 0; c + i < key_lines[edit_line] + key_linepos; i++)
		partial[i] = c[i];
	partial[i] = 0;

	//trim trailing space becuase it screws up string comparisons
	if (i > 0 && partial[i-1] == ' ')
		partial[i-1] = 0;

	// find a match
	mark = Hunk_LowMark();
	if (!key_tabpartial[0]) //first time through
	{
		q_strlcpy (key_tabpartial, partial, MAXCMDLINE);
		BuildTabList (key_tabpartial);

		if (!tablist)
			return;

		// print list if length > 1 and action is user-initiated
		if (tablist->next != tablist && mode == TABCOMPLETE_USER)
			Con_PrintTabList (); // woods #consolecols

		//	match = tablist->name;
		// First time, just show maximum matching chars -- S.A.
		match = bash_singlematch ? tablist->name : bash_partial;
	}
	else
	{
		BuildTabList (key_tabpartial);

		if (!tablist)
			return;

		//find current match -- can't save a pointer because the list will be rebuilt each time
		t = tablist;
		match = keydown[K_SHIFT] ? t->prev->name : t->name;
		do
		{
			if (!q_strcasecmp (t->name, partial))
			{
				match = keydown[K_SHIFT] ? t->prev->name : t->next->name;
				break;
			}
			t = t->next;
		} while (t != tablist);
	}

	if (mode == TABCOMPLETE_AUTOHINT)
	{
		size_t len = strlen(partial);
		match = q_strcasestr (match, partial);
		if (match && match[len])
			q_strlcpy (key_tabhint, match + len, sizeof (key_tabhint));
		Hunk_FreeToLowMark (mark);
		key_tabpartial[0] = '\0';
		return;
	}

	// insert new match into edit line
	q_strlcpy (partial, match, MAXCMDLINE); //first copy match string
	q_strlcat (partial, key_lines[edit_line] + key_linepos, MAXCMDLINE); //then add chars after cursor
	*c = '\0';	//now copy all of this into edit line
	q_strlcat (key_lines[edit_line], partial, MAXCMDLINE);
	key_linepos = c - key_lines[edit_line] + Q_strlen(match); //set new cursor position
	if (key_linepos >= MAXCMDLINE)
		key_linepos = MAXCMDLINE - 1;

	match = NULL;
	Hunk_FreeToLowMark (mark);

	// if cursor is at end of string, let's append a space to make life easier
	if (key_linepos < MAXCMDLINE - 1 &&
		key_lines[edit_line][key_linepos] == 0 && bash_singlematch)
	{
		key_lines[edit_line][key_linepos] = ' ';
		key_linepos++;
		key_lines[edit_line][key_linepos] = 0;
		key_tabpartial[0] = 0; // restart cycle
	// S.A.: the map argument completion (may be in combination with the bash-style
	// display behavior changes, causes weirdness when completing the arguments for
	// the changelevel command. the line below "fixes" it, although I'm not sure about
	// the reason, yet, neither do I know any possible side effects of it:
		c = key_lines[edit_line] + key_linepos;

		Con_TabComplete (TABCOMPLETE_AUTOHINT);
	}
}

/*
==============================================================================

DRAWING

==============================================================================
*/

/*
================
Con_NotifyAlpha -- // woods #confade (ironwail) ee58794
================
*/
static float Con_NotifyAlpha (double time)
{
	float fade;
	if (!time)
		return 0.f;
	fade = q_max (con_notifyfade.value * con_notifyfadetime.value, 0.f);
	time += con_notifytime.value + fade - realtime;
	if (time <= 0.f)
		return 0.f;
	if (!fade)
		return 1.f;
	time = time / fade;
	return q_min (time, 1.0);
}

/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
void Con_DrawNotify (void)
{
	int	i, x, v;
	const char	*text;
	float	alpha; // woods #confade
	int		maxlines = CLAMP (0, con_notifylines.value, NUM_CON_TIMES); // woods from proquake 493 #notifylines

	GL_SetCanvas (CANVAS_CONSOLE); //johnfitz
	v = vid.conheight + con_notifyposition.value; // woods #notifyposition

	for (i = con_current - maxlines + 1; i <= con_current; i++) // woods from proquake 493 #notifylines
	{
		if (i < 0)
			continue;

		if (i >= con_centerprint_start && i <= con_centerprint_end) // woods #centerlog
			continue;

		alpha = Con_NotifyAlpha (con_times[i % NUM_CON_TIMES]); // woods #confade
		if (alpha <= 0.f)
			continue;
		text = con_text + (i % con_totallines)*con_linewidth;

		clearnotify = 0;

		for (x = 0; x < con_linewidth; x++)
			Draw_CharacterRGBA ((x+1)<<3, v, text[x], CL_PLColours_Parse("0xffffff"), alpha); // woods #confade

		v += 8;

		scr_tileclear_updates = 0; //johnfitz
	}

	if (key_dest == key_message)
	{
		clearnotify = 0;

		if (chat_team)
		{
			Draw_String (8, v, "say_team:");
			x = 11;
		}
		else
		{
			Draw_String (8, v, "say:");
			x = 6;
		}

		text = Key_GetChatBuffer();
		i = Key_GetChatMsgLen();
		if (i > con_linewidth - x - 1)
			text += i - con_linewidth + x + 1;

		while (*text)
		{
			Draw_Character (x<<3, v, *text);
			x++;
			text++;
		}

		Draw_Character (x<<3, v, 10 + ((int)(realtime*con_cursorspeed)&1));
		v += 8;

		scr_tileclear_updates = 0; //johnfitz
	}
	if (v > con_notifylines_) // woods from proquake 493 #notifylines
		con_notifylines_ = v;
}

/*
================
Con_DrawInput -- johnfitz -- modified to allow insert editing

The input line scrolls horizontally if typing goes beyond the right edge
================
*/
extern	qpic_t *pic_ovr, *pic_ins; //johnfitz -- new cursor handling

void Con_DrawInput (void)
{
	const char* workline = key_lines[edit_line]; // woods #iwtabcomplete
	int	i, ofs, len; // woods #iwtabcomplete

	if (key_dest != key_console && !con_forcedup)
		return;		// don't draw anything

// prestep if horizontally scrolling
	if (key_linepos >= con_linewidth)
		ofs = 1 + key_linepos - con_linewidth;
	else
		ofs = 0;

	len = strlen(workline); // woods #iwtabcomplete

	// draw input string // woods #iwtabcomplete
	for (i = 0; i + ofs < len; i++)
		Draw_Character ((i + 1) << 3, vid.conheight - 16, workline[i + ofs]);

	// draw tab completion hint
	if (key_tabhint[0])
	{
		for (i = 0; key_tabhint[i] && i + 1 + len - ofs < con_linewidth + CON_MARGIN * 2; i++)
			Draw_CharacterRGBA ((i+1 + len - ofs) <<3, vid.conheight - 16, key_tabhint[i] | 0x80, CL_PLColours_Parse("0xffffff"), 0.75f);
	}

	// johnfitz -- new cursor handling
	if (!((int)((realtime-key_blinktime)*con_cursorspeed) & 1))
	{
		i = key_linepos - ofs;
		Draw_Pic ((i+1)<<3, vid.conheight - 16, key_insert ? pic_ins : pic_ovr);
	}
}

/*
================
Con_DrawBirthdayMessage -- woods #qbday

Displays a birthday message for Quake on June 22nd
Only shows for 30 seconds on first launch of the day
Requires pak0 to be present
================
*/
static void Con_DrawBirthdayMessage (void)
{
	if (!pak0) // only proceed if valid pak0 detected
		return;

	time_t t = time(NULL);
	struct tm* tm = localtime(&t);

	if (tm->tm_mon != 5 || tm->tm_mday != 22)  // only on June 22
		return;

	char backup_path[MAX_OSPATH];
	q_snprintf(backup_path, sizeof(backup_path), "backups/config-%02d-%02d-%d.cfg",
		tm->tm_mon + 1, tm->tm_mday, tm->tm_year + 1900);

	if (COM_FileExists(backup_path, NULL)) // skip if not first run of the day
		return;

	Uint32 current_time = SDL_GetTicks();

	if (birthday_start_time == 0) {
		birthday_start_time = current_time;
	}

	if (current_time - birthday_start_time < BIRTHDAY_DURATION)
	{
		char birthday[32];
		char yearstr[32];
		int age = (tm->tm_year + 1900) - 1996;
		int x;

		q_snprintf(birthday, sizeof(birthday), "Happy Birthday Quake ");
		q_snprintf(yearstr, sizeof(yearstr), "1996-%d (%d) ", tm->tm_year + 1900, age);

		for (x = 0; x < (int)strlen(birthday); x++)
			Draw_Character((con_linewidth - strlen(birthday) + x + 2) << 3, vid.conheight - 32, birthday[x] + 128);

		for (x = 0; x < (int)strlen(yearstr); x++)
			Draw_Character((con_linewidth - strlen(yearstr) + x + 2) << 3, vid.conheight - 24, yearstr[x] + 128);
	}
}

/*
================
Con_DrawConsole -- johnfitz -- heavy revision

Draws the console with the solid background
The typing input line at the bottom should only be drawn if typing is allowed
================
*/
void Con_DrawConsole (int lines, qboolean drawinput)
{
	int	i, x, y, j, sb, rows;
	const char	*text;
	const char	*ver = ENGINE_NAME_AND_VER;

	if (lines <= 0)
		return;

	con_vislines = lines * vid.conheight / glheight;
	GL_SetCanvas (CANVAS_CONSOLE);

// draw the background
	Draw_ConsoleBackground ();

// draw the buffer text
	rows = (con_vislines +7)/8;
	y = vid.conheight - rows*8;
	rows -= 2; //for input and version lines
	sb = (con_backscroll) ? 2 : 0;

	for (i = con_current - rows + 1; i <= con_current - sb; i++, y += 8)
	{
		j = i - con_backscroll;
		if (j < 0)
			j = 0;
		text = con_text + (j % con_totallines)*con_linewidth;

		for (x = 0; x < con_linewidth; x++)
			Draw_Character ( (x + 1)<<3, y, text[x]);
	}

// draw scrollback arrows
	if (con_backscroll)
	{
		y += 8; // blank line
		for (x = 0; x < con_linewidth; x += 4)
			Draw_Character ((x + 1)<<3, y, '^');
		y += 8;
	}

// draw the input prompt, user text, and cursor
	if (drawinput)
		Con_DrawInput ();

//draw version number in bottom right
	for (x = 0; x < (int)strlen(ver); x++)
		Draw_Character ((con_linewidth - strlen(ver) + x + 2)<<3, vid.conheight - 8, ver[x] /*+ 128*/); // woods iw

	Con_DrawBirthdayMessage (); // woods #qbday - show quake's birthday for 30 seconds on june 22
}


/*
==================
Con_NotifyBox
==================
*/
void Con_NotifyBox (const char *text)
{
	double		t1, t2;
	int		lastkey, lastchar;

// during startup for sound / cd warnings
	Con_Printf ("\n\n%s", Con_Quakebar(40)); //johnfitz
	Con_Printf ("%s", text);
	Con_Printf ("Press a key.\n");
	Con_Printf ("%s", Con_Quakebar(40)); //johnfitz

	key_dest = key_console;
	IN_UpdateGrabs();

	Key_BeginInputGrab ();
	do
	{
		t1 = Sys_DoubleTime ();
		SCR_UpdateScreen ();
		Sys_SendKeyEvents ();
		Key_GetGrabbedInput (&lastkey, &lastchar);
		Sys_Sleep (16);
		t2 = Sys_DoubleTime ();
		realtime += t2-t1;		// make the cursor blink
	} while (lastkey == -1 && lastchar == -1);
	Key_EndInputGrab ();

	Con_Printf ("\n");
	key_dest = key_game;
	realtime = 0;		// put the cursor back to invisible
	IN_UpdateGrabs();
}


void LOG_Init (quakeparms_t *parms)
{
	time_t	inittime;
	char	session[24];

	if (!COM_CheckParm("-condebug"))
		return;

	inittime = time (NULL);
	strftime (session, sizeof(session), "%m/%d/%Y %H:%M:%S", localtime(&inittime));
	q_snprintf (logfilename, sizeof(logfilename), "%s/qconsole.log", parms->basedir);

//	unlink (logfilename);

	log_fd = open (logfilename, O_WRONLY | O_CREAT | O_APPEND, 0666); // woods append, not overwite log
	if (log_fd == -1)
	{
		fprintf (stderr, "Error: Unable to create log file %s\n", logfilename);
		return;
	}

	con_debuglog = true;
	Con_DebugLog (va("\nLOG started on: %s \n\n", session)); // woods add a line

}

void LOG_Close (void)
{
	if (log_fd == -1)
		return;
	close (log_fd);
	log_fd = -1;
}

// woods #discord
static int DiscordThread(void *data)
{
    discord_job_t *job = (discord_job_t*)data;

    CURL *curl = curl_easy_init();
    if (curl) {
        struct curl_slist *hdr = NULL;
        hdr = curl_slist_append(hdr, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, job->url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, job->payload);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(job->payload)); // exact body size
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1500L); // fail fast on connect
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "QSS-M");
        /* keep TLS verification ON (explicit) */
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        CURLcode rc = curl_easy_perform(curl);
        if (rc != CURLE_OK) {
            if (developer.value)
                Con_DPrintf("discord: curl error: %s\n", curl_easy_strerror(rc));
        } else {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code != 204 && http_code != 200) {
                if (developer.value)
                    Con_DPrintf("discord: HTTP %ld\n", http_code);
            }
        }

        curl_slist_free_all(hdr);
        curl_easy_cleanup(curl);
    }

	free(job);
    return 0;
}

static void MakeDiscordPayload(const char *raw, char *out, size_t outsz)
{
    char clean[1024];
    q_strlcpy(clean, raw, sizeof(clean));

    for (unsigned char *ch = (unsigned char*)clean; *ch; ch++)
        *ch = dequake[*ch];

    char *esc = JSON_EscapeString(clean);
    if (!esc) { out[0] = 0; return; }

    /* Avoid accidental pings to @everyone/@here and roles */
    q_snprintf(out, outsz,
               "{\"content\":\"%s\",\"allowed_mentions\":{\"parse\":[]}}",
               esc);
    free(esc);
}

static void QSSM_DiscordNotify(const char *raw_msg)
{
    if (!con_notifydiscord.string[0]) return;
    if (!IsDiscordWebhookURL(con_notifydiscord.string)) {
        char masked[256];
        MaskDiscordURL(con_notifydiscord.string, masked, sizeof(masked));
        Con_Printf("discord: invalid webhook URL ^m%s^m (must be https://*.discord.com/api/webhooks/...)\n", masked);
        return;
    }

    char payload[1300];
    MakeDiscordPayload(raw_msg, payload, sizeof(payload));
    if (!payload[0]) return;

	discord_job_t* job = (discord_job_t*)malloc(sizeof(*job));
    q_strlcpy(job->payload, payload, sizeof(job->payload));
    q_strlcpy(job->url, con_notifydiscord.string, sizeof(job->url));

    SDL_Thread *t = SDL_CreateThread(DiscordThread, "discord", job);
    if (t) SDL_DetachThread(t);
}
