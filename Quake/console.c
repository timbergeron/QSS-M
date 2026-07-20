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
#include <stdint.h> // woods #debuglogsize
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

static SDL_Cursor *con_cursor_arrow = NULL; // woods #conselection
static SDL_Cursor *con_cursor_ibeam = NULL; // woods #conselection
static SDL_Cursor *con_cursor_hand  = NULL; // woods #conselection
static SDL_Cursor *con_cursor_current = NULL; // woods #conselection
static SDL_Cursor *con_cursor_saved = NULL; // woods #conselection
static int         con_cursor_saved_visible = SDL_DISABLE; // woods #conselection

typedef struct
{
	qboolean valid;
	int x, y;
	int ofs;
	int width_chars;
} chatmousearea_t;

static chatmousearea_t chat_mouse_area;
static qboolean chat_mouse_down = false;
static qboolean chat_mouse_selecting = false;

int 		con_linewidth;

float		con_cursorspeed = 4;

#define		CON_TEXTSIZE (1024 * 1024) //ericw -- was 65536. johnfitz -- new default size
#define		CON_MINSIZE  16384 //johnfitz -- old default, now the minimum size
#define		CON_MARGIN   1 // woods #iwtabcomplete
#define     CHARSIZE     8 // woods #conselection
#define     CON_SCROLL_ZONE      (CHARSIZE * 2) // woods #conselection
#define     CON_MAX_SCROLL_SPEED 32.f // woods #conselection

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
cvar_t		con_typing = {"con_typing", "1", CVAR_ARCHIVE}; // woods #typing...
cvar_t		con_cursorcolor = {"con_cursorcolor", "0", CVAR_ARCHIVE}; // woods #cursorcolor (0=white, 1=red, 2=gold)
cvar_t		con_clear_input_on_toggle = {"con_clear_input_on_toggle", "0", CVAR_ARCHIVE}; // R00k, erase console input line when toggling the console
cvar_t		con_autohint = {"con_autohint", "1", CVAR_ARCHIVE}; // show inline console completion hints

static void QWMapList_f(void);

static void Con_Autohint_Callback (cvar_t *var)
{
	if (!var->value)
		key_tabhint[0] = '\0';
}

char		con_lastcenterstring[1024]; //johnfitz

void (*con_redirect_flush)(const char *buffer);	//call this to flush the redirection buffer (for rcon)
char con_redirect_buffer[CON_REDIRECT_BUFFER_SIZE];

#define	NUM_CON_TIMES 30 // woods from proquake 493 #notifylines
double		con_times[NUM_CON_TIMES];	// realtime time the line was generated
						// for transparent notify lines

int			con_vislines;
int			con_notifylines_; // woods from proquake 493 #notifylines

qboolean	con_debuglog = false;

qboolean	con_initialized;


// woods #conselection ported from ironwail

typedef struct { int line; int col; } conofs_t;
typedef struct { const char *path; conofs_t begin, end; } conlink_t;
typedef struct { conofs_t begin, end; } conselection_t;

typedef enum {
    CT_INSIDE, //strict hit test: must be inside visible text cell
    CT_NEAREST //selection hit test: clamp to nearest legal edge
} contest_t;

typedef enum {
    CMS_NOTPRESSED,
    CMS_PRESSED,
    CMS_DRAGGING
} conmouse_t;

static conlink_t   **con_links = NULL; // sorted by end offset
static conlink_t   *con_hotlink = NULL;
static double       con_mouseclickdelay = 0.0;
static int          con_mouseclicks = 0; // 1: char, 2: word, 3: line, >=4: all
static conmouse_t   con_mousestate = CMS_NOTPRESSED;
static conselection_t con_mouseselection;
static conselection_t con_selection; //final (normalized) selection
static int          con_clickx, con_clicky; //canvas coords at press
static float        con_scrollspeed = 0.f; //autoscroll during drag
static float        con_scrolldelta = 0.f;
static const double DOUBLECLICK_TIME = 0.5;
static qboolean     con_cursor_was_active = false;
static qboolean     con_blockselectionuntilrelease = false;

static qboolean Con_LinksEnabled(void) { return modestate == MS_WINDOWED; }
static void Con_SetHotLink(conlink_t* link) { con_hotlink = Con_LinksEnabled() ? link : NULL; }
static void Con_ClearSelection(void) { memset(&con_selection, 0, sizeof(con_selection)); }
static void Con_EnterCursorMode(void);
static void Con_LeaveCursorMode(void);


extern double host_frametime;
extern float scr_con_current;

#define CON_VEC_PUSH(vec, val) do { \
    size_t __n = (vec) ? ((size_t *)(vec))[-1] : 0; \
    size_t __newn = __n + 1; \
    size_t __bytes = sizeof(size_t) + __newn * sizeof(*(vec)); \
    size_t *__hdr = (size_t*)realloc((vec)?((char*)(vec) - sizeof(size_t)):NULL, __bytes); \
    if (!__hdr) Sys_Error("con vec oom"); \
    __hdr[0] = __newn; \
    (vec) = (void*)(&__hdr[1]); \
    (vec)[__newn - 1] = (val); \
} while(0)
#define CON_VEC_SIZE(vec) ((vec)?(((size_t*)(vec))[-1]):0)
#define CON_VEC_CLEAR(vec) do { if (vec) { free(((size_t*)(vec))-1); (vec)=NULL; } } while(0)

static void Con_ClearLinks(void)
{
	size_t	i, n;

	n = CON_VEC_SIZE(con_links);
	for (i = 0; i < n; i++)
		free(con_links[i]);
	CON_VEC_CLEAR(con_links);
	con_hotlink = NULL;
}

void Char_Console2(int key); // woods #ezsay add leading space for mode 2
extern qboolean	endscoreprint; // woods -- don't filter end scores pq_confilter+
char lastconnected[3]; // woods -- #identify+
char lc[3]; // woods -- #identify+
int retry_counter = 0; // woods #ms
extern int chat_setinfo_defer; // woods #chatinfo

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
	if (con_clear_input_on_toggle.value)
	{
		key_lines[edit_line][1] = 0;
		key_linepos = 1;
	}

	if (key_dest == key_console/* || (key_dest == key_game && con_forcedup)*/)
	{
		con_backscroll = 0; //johnfitz -- toggleconsole should return you to the bottom of the scrollback
		history_line = edit_line; //johnfitz -- it should also return you to the bottom of the command history

		if (cls.state == ca_connected)
			key_dest = key_game;
		else
			M_ToggleMenu(0); // woods, was 1, better ui not to go to menu (kilomile) 

		Con_LeaveCursorMode(); // woods #conselection - leaving console -> restore (game) or set stable menu cursor
	}
	else
	{
		M_ToggleMenu(0);
		key_dest = key_console;
		Con_EnterCursorMode(); // woods #conselection - entering console -> save host cursor+visibility, then enable console cursor

       keydown[K_MOUSE1] = keydown[K_MOUSE2] = keydown[K_MOUSE3] = false; // woods #conselection - make sure no queued mouse presses leak into the game
       keydown[K_MWHEELUP] = keydown[K_MWHEELDOWN] = false; // woods #conselection - wheels too, if you track them as keys
	}

       Con_SetHotLink(NULL); // woods #conselection - clear hover/selection on toggle to avoid stale state
       Con_ClearSelection();

	if ((key_linepos == 1) && (cl_chatmode.value == 2 || cl_chatmode.value == 3)) // woods #ezsay add leading space for mode 2
		Char_Console2(32);

	if (cl_chatmode.value) // woods #chatinfo
	{
		SetChatInfo (0);

		Host_CancelDeferredCall(chat_setinfo_defer); // woods #chatinfo
		chat_setinfo_defer = 0;
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

	// woods #conselection - also clear link/selection tables
    Con_ClearLinks();
    Con_ClearSelection();
}

/*
===============
Con_CursorColor_Completion_f -- woods #iwtabcomplete
===============
*/
static void Con_CursorColor_Completion_f(cvar_t* cvar, const char* partial)
{
	(void)cvar;

	Con_AddToTabList("0", partial, "white", NULL);
	Con_AddToTabList("1", partial, "red", NULL);
	Con_AddToTabList("2", partial, "gold", NULL);
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
	char	relname[MAX_OSPATH];
	char	name[MAX_OSPATH];

	q_strlcpy (relname, Cmd_Argc () >= 2 ? Cmd_Argv (1) : "condump.txt", sizeof (relname));
	if (strstr (relname, ".."))
	{
		Con_Printf ("Relative pathnames are not allowed.\n");
		return;
	}
	COM_AddExtension (relname, ".txt", sizeof (relname));
	q_snprintf (name, sizeof(name), "%s/%s", com_gamedir, relname);
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
	{
		Con_SafePrintf("Dumped console text to ");
		Con_LinkPrintf(name, "%s/%s", COM_SkipPath(com_gamedir), relname);
		Con_SafePrintf(".\n");
	}
	else
	{
		Con_SafePrintf("Dumped console text to ");
		Con_LinkPrintf(name, "%s", name);
		Con_SafePrintf(".\n");
	}
}

/*
================
Con_Copy_f -- woods #concopy #conselection
================
*/
void Con_Copy_f(void)
{
	qboolean copied = false;
	const char* copySnd = COM_FileExists("sound/qssm/copy.wav", NULL) ? "qssm/copy.wav" : "player/tornoff2.wav";


	if (Con_CopySelectionToClipboard()) // Try copying the current selection first
	{
		copied = true;
	}
	else
	{
		Con_Dump_f(); // Fallback: dump entire console, then load file and push to clipboard
		{
			char* f = (char*)COM_LoadHunkFile("condump.txt", NULL);
			if (f)
			{
				// SDL returns 0 on success
				if (SDL_SetClipboardText(f) == 0)
					copied = true;
			}
		}
	}

	if (copied)
		S_LocalSound(copySnd);
}


// woods #conselection ported from ironwail

static const char *Con_GetLine (int line)
{
    return con_text + (line % con_totallines) * con_linewidth;
}

static size_t Con_StrLen (int line)
{
    const char *text; size_t len;
    if (line > con_current) return 0;
    text = Con_GetLine(line); len = con_linewidth;
    while (len > 0 && (char)(text[len - 1] & 0x7f) == ' ') len--;
    return len;
}

static void Con_ScreenToCanvas (int x, int y, int *outx, int *outy)
{
    int lines = vid.conheight - (int)(scr_con_current * vid.conheight / glheight);
    float fx = (float)x, fy = (float)y;
    float px, py;

#if defined(USE_SDL2)
    {
        /* High-DPI backends report mouse events in window points while the
           console canvas maps GL drawable pixels (see Draw_WindowToCanvas) */
        SDL_Window *window = (SDL_Window *)VID_GetWindow();
        int window_w = 0, window_h = 0, drawable_w = 0, drawable_h = 0;

        if (window)
        {
            SDL_GetWindowSize(window, &window_w, &window_h);
            SDL_GL_GetDrawableSize(window, &drawable_w, &drawable_h);
        }
        if (window_w > 0 && window_h > 0 && drawable_w > 0 && drawable_h > 0 &&
            (window_w != drawable_w || window_h != drawable_h))
        {
            fx = fx * (float)drawable_w / (float)window_w;
            fy = fy * (float)drawable_h / (float)window_h;
        }
    }
#endif

    px = (fx - glx) * (float)vid.conwidth / glwidth;
    py = (fy - gly) * (float)vid.conheight / glheight + lines;
    *outx = (int)(px + 0.5f);
    *outy = (int)(py + 0.5f);
}

static void Con_TrimTrailingSpaces(char* str) // woods #typing...
{
	if (!str)
		return;

	size_t len = strlen(str);
	while (len > 0)
	{
		unsigned char ch = (unsigned char)str[len - 1] & 0x7F;
		if (ch != ' ')
			break;

		str[--len] = '\0';
	}
}

static void Con_GetShortName(const char* full_name, char* out, size_t out_size) // woods #typing...
{
	if (!out || out_size == 0)
		return;

	if (!full_name)
	{
		out[0] = '\0';
		return;
	}

	q_snprintf(out, out_size, "%.15s", full_name);
	Con_TrimTrailingSpaces(out);
}

static int Con_OfsCompare (const conofs_t *a, const conofs_t *b)
{
    if (a->line != b->line) return a->line - b->line;
    return a->col - b->col;
}

static void Con_PruneLinks(void)
{
	conofs_t	first;
	size_t		i, keep, n;

	n = CON_VEC_SIZE(con_links);
	if (!n)
		return;

	first.line = con_current - con_totallines + 1;
	first.col = 0;

	for (i = 0; i < n; i++)
	{
		if (Con_OfsCompare(&con_links[i]->end, &first) > 0)
			break;
		if (con_hotlink == con_links[i])
			con_hotlink = NULL;
		free(con_links[i]);
	}

	if (!i)
		return;

	keep = n - i;
	if (keep)
	{
		memmove(con_links, con_links + i, keep * sizeof(*con_links));
		((size_t *)con_links)[-1] = keep;
	}
	else
		CON_VEC_CLEAR(con_links);
}

static qboolean Con_CanvasToOffset (int x, int y, conofs_t *ofs, contest_t mode)
{
    qboolean ret = true;
    /* canvas origin is bottom-left of console */
    y = vid.conheight - y;
    if (mode == CT_NEAREST) x += 4;
    x >>= 3; y >>= 3;
    x -= CON_MARGIN; y -= 2;
    if (mode == CT_INSIDE) {
        if (x < 0 || x >= con_linewidth) ret = false;
        if (y < 0 || y >= con_vislines)   ret = false;
        if (con_backscroll && y < 2)      ret = false;
    } else {
        if (x < 0) x = 0;
        if (x > con_linewidth) x = con_linewidth;
        if (y < -1) y = -1;
        if (y > con_vislines) y = con_vislines;
        if (y < 0) x = 0;
        if (con_backscroll && y < 2) { x = 0; y = 1; }
    }
    y += con_backscroll;
    y = con_current - y;
    ofs->line = y; ofs->col = x;
    return ret;
}

static void Con_GetCurrentRange (conofs_t *b, conofs_t *e)
{
    b->line = con_current - con_totallines + 1; b->col = 0;
    e->line = con_current + 1;                  e->col = 0;
}

static qboolean Con_IntersectRanges (conofs_t *b, conofs_t *e,
                                     const conofs_t *sb, const conofs_t *se)
{
    if (Con_OfsCompare(se,b) <= 0) return false;
    if (Con_OfsCompare(e,sb) <= 0) return false;
    if (Con_OfsCompare(b,sb) < 0) *b = *sb;
    if (Con_OfsCompare(se,e) < 0) *e = *se;
    return true;
}

static conlink_t *Con_GetLinkAtOfs (const conofs_t *ofs)
{
    size_t lo=0, hi=CON_VEC_SIZE(con_links);
    while (lo < hi) {
        size_t mid = (lo+hi)/2;
        if (Con_OfsCompare(ofs, &con_links[mid]->end) >= 0) lo = mid + 1;
        else hi = mid;
    }
    if (lo == CON_VEC_SIZE(con_links)) return NULL;
    if (Con_OfsCompare(ofs, &con_links[lo]->begin) >= 0) return con_links[lo];
    return NULL;
}

static qboolean Con_CursorActive (void)
{
    return (key_dest == key_console) || (con_forcedup && key_dest != key_menu);
}

/* helper: set OS cursor safely */
static void Con_SetCursor(SDL_Cursor *cur)
{
    if (!cur) return;
    if (con_cursor_current == cur && SDL_GetCursor() == cur) return;
    VID_SetCursorHandle(cur);
    con_cursor_current = cur;
}

static void Con_SetNeutralCursor(void)
{
    SDL_Cursor *game_cursor = VID_GetGameCursorHandle();
    Con_SetCursor(game_cursor ? game_cursor : con_cursor_arrow);
}

static qboolean Con_ChatMouseToCursorPos (int canvas_x, int canvas_y, qboolean nearest, int *pos)
{
	int cell;

	if (!pos || !chat_mouse_area.valid || chat_mouse_area.width_chars <= 0)
		return false;

	if (!nearest)
	{
		if (canvas_x < chat_mouse_area.x ||
			canvas_x > chat_mouse_area.x + chat_mouse_area.width_chars * CHARSIZE ||
			canvas_y < chat_mouse_area.y ||
			canvas_y >= chat_mouse_area.y + CHARSIZE)
			return false;
	}

	cell = (canvas_x - chat_mouse_area.x + CHARSIZE / 2) / CHARSIZE;
	cell = CLAMP(0, cell, chat_mouse_area.width_chars);
	*pos = CLAMP(0, chat_mouse_area.ofs + cell, Key_GetChatMsgLen());
	return true;
}

static qboolean Con_ChatMousePosition (qboolean nearest, int *pos)
{
	int window_x, window_y, canvas_x, canvas_y;

	SDL_GetMouseState (&window_x, &window_y);
	Con_ScreenToCanvas (window_x, window_y, &canvas_x, &canvas_y);
	return Con_ChatMouseToCursorPos (canvas_x, canvas_y, nearest, pos);
}

static qboolean Con_ChatMouseDragPosition (int *pos)
{
	int window_x, window_y, canvas_x, canvas_y;
	int right, len;

	if (!pos || !chat_mouse_area.valid || chat_mouse_area.width_chars <= 0)
		return false;

	SDL_GetMouseState (&window_x, &window_y);
	Con_ScreenToCanvas (window_x, window_y, &canvas_x, &canvas_y);

	len = Key_GetChatMsgLen();
	right = chat_mouse_area.x + chat_mouse_area.width_chars * CHARSIZE;
	if (canvas_x < chat_mouse_area.x && chat_mouse_area.ofs > 0)
	{
		*pos = chat_mouse_area.ofs - 1;
		return true;
	}
	if (canvas_x > right && chat_mouse_area.ofs + chat_mouse_area.width_chars < len)
	{
		*pos = CLAMP(0, chat_mouse_area.ofs + chat_mouse_area.width_chars + 1, len);
		return true;
	}

	return Con_ChatMouseToCursorPos (canvas_x, canvas_y, true, pos);
}

static void Con_ChatComputeView (int chat_max, const char **text, int *cursor, int *ofs, int *visible)
{
	int chat_len, chat_cursor, chat_ofs, chat_visible;

	chat_len = Key_GetChatMsgLen();
	chat_cursor = Key_GetChatCursorPos();

	chat_ofs = 0;
	if (chat_cursor > chat_max)
		chat_ofs = chat_cursor - chat_max;
	if (chat_cursor < chat_ofs)
		chat_ofs = chat_cursor;
	if (chat_ofs > chat_len)
		chat_ofs = chat_len;

	chat_visible = chat_len - chat_ofs;
	if (chat_visible > chat_max)
		chat_visible = chat_max;

	if (text)
		*text = Key_GetChatBuffer();
	if (cursor)
		*cursor = chat_cursor;
	if (ofs)
		*ofs = chat_ofs;
	if (visible)
		*visible = chat_visible;
}

static void Con_UpdateChatMouseState (void)
{
	int pos;
	qboolean inside;

	if (key_dest != key_message)
	{
		chat_mouse_down = false;
		chat_mouse_selecting = false;
		return;
	}

	inside = Con_ChatMousePosition (false, &pos);
	if (chat_mouse_down && chat_mouse_selecting)
	{
		if (Con_ChatMouseDragPosition (&pos))
			Key_SetChatCursorPos (pos, true);
		Con_SetCursor (con_cursor_ibeam ? con_cursor_ibeam : con_cursor_arrow);
	}
	else if (inside)
		Con_SetCursor (con_cursor_ibeam ? con_cursor_ibeam : con_cursor_arrow);
	else
		Con_SetNeutralCursor ();
}

qboolean Con_ChatMouseButton (qboolean down)
{
	int pos;
	qboolean inside;

	if (key_dest != key_message)
		return false;

	inside = Con_ChatMousePosition (false, &pos);
	if (down)
	{
		chat_mouse_down = true;
		chat_mouse_selecting = inside;
		if (inside)
		{
			Key_SetChatCursorPos (pos, keydown[K_SHIFT]);
			Con_SetCursor (con_cursor_ibeam ? con_cursor_ibeam : con_cursor_arrow);
		}
	}
	else
	{
		if (chat_mouse_down && chat_mouse_selecting &&
			Con_ChatMouseDragPosition (&pos))
			Key_SetChatCursorPos (pos, true);
		chat_mouse_down = false;
		chat_mouse_selecting = false;
	}

	return true;
}

static qboolean Con_HasSelection (void) { return Con_OfsCompare(&con_selection.begin,&con_selection.end)!=0; }

/* Enter/leave routines to avoid cursor flicker between console/menu/game */
static void Con_EnterCursorMode(void)
{
    /* remember host cursor & visibility, then make console cursor visible */
    con_cursor_saved = SDL_GetCursor();
    {
        int vis = SDL_ShowCursor(SDL_QUERY);
        con_cursor_saved_visible = (vis == SDL_ENABLE) ? SDL_ENABLE : SDL_DISABLE;
    }
    SDL_ShowCursor(SDL_ENABLE);
    /* actual shape is set by Con_Mousemove (ibeam/hand/arrow) */
}

static void Con_LeaveCursorMode(void)
{
    /* If leaving to menu, force a stable visible arrow.
       If leaving to game, restore what the host had. */
    if (key_dest == key_menu) {
        SDL_ShowCursor(SDL_ENABLE);
        if (con_cursor_arrow) VID_SetCursorHandle(con_cursor_arrow);
    } else {
        if (con_cursor_saved) VID_SetCursorHandle(con_cursor_saved);
        SDL_ShowCursor(con_cursor_saved_visible);
    }
    con_cursor_current = NULL;
    con_cursor_saved = NULL;
}

void Con_SelectAll (void)
{
    Con_GetCurrentRange(&con_selection.begin,&con_selection.end);
    while (Con_HasSelection() && Con_StrLen(con_selection.begin.line) == 0)
        con_selection.begin.line++;

    // woods selection - ensure mouse selection state is synced so shift-click works after select all
    con_mouseselection = con_selection;
}

/*
================
Con_MoveSelection -- woods #conselection

Move selection end point by character (dir_x) or line (dir_y).
If no selection exists, starts one at current view position.
================
*/
void Con_MoveSelection (int dir_x, int dir_y)
{
    int len;

    // If no selection exists, start one at the current view position (bottom of visible area)
    if (!Con_HasSelection()) {
        con_mouseselection.begin.line = con_current - con_backscroll;
        con_mouseselection.begin.col = 0;
        con_mouseselection.end = con_mouseselection.begin;
    }

    // Move the end point - operate on the "mouse" selection which preserves anchor direction
    if (dir_x != 0) {
        // Character movement (left/right)
        con_mouseselection.end.col += dir_x;

        // Clamp and wrap to previous/next line
        if (con_mouseselection.end.col < 0) {
            if (con_mouseselection.end.line > con_current - con_totallines + 1) {
                con_mouseselection.end.line--;
                len = (int)Con_StrLen(con_mouseselection.end.line);
                con_mouseselection.end.col = len;
            } else {
                con_mouseselection.end.col = 0;
            }
        } else {
            len = (int)Con_StrLen(con_mouseselection.end.line);
            if (con_mouseselection.end.col > len) {
                if (con_mouseselection.end.line < con_current) {
                    con_mouseselection.end.line++;
                    con_mouseselection.end.col = 0;
                } else {
                    con_mouseselection.end.col = len;
                }
            }
        }
    }

    if (dir_y != 0) {
        // Line movement (up/down)
        con_mouseselection.end.line += dir_y;

        // Clamp to valid range
        if (con_mouseselection.end.line < con_current - con_totallines + 1)
            con_mouseselection.end.line = con_current - con_totallines + 1;
        if (con_mouseselection.end.line > con_current)
            con_mouseselection.end.line = con_current;

        // Clamp column to line length
        len = (int)Con_StrLen(con_mouseselection.end.line);
        if (con_mouseselection.end.col > len)
            con_mouseselection.end.col = len;
    }

    // Publish to main selection (normalized)
    con_selection = con_mouseselection;
    if (Con_OfsCompare(&con_selection.begin, &con_selection.end) > 0) {
        conofs_t t = con_selection.begin; con_selection.begin = con_selection.end; con_selection.end = t;
    }
}

static qboolean Con_GetNormalizedSelection (conofs_t *b, conofs_t *e)
{
    conofs_t tb, te;
    conofs_t *sb=&con_selection.begin, *se=&con_selection.end;
    if (Con_OfsCompare(sb,se) > 0) { conofs_t *t = sb; sb = se; se = t; }
    *b = *sb; *e = *se;
    Con_GetCurrentRange(&tb,&te);
    return Con_IntersectRanges(b,e,&tb,&te);
}

static int Con_TestWordBoundary (int pos, const char *text, int len)
{
    if (pos <= 0) return 1;
    if (pos >= len) return -1;
    return q_isspace(text[pos-1] & 0x7f) - q_isspace(text[pos] & 0x7f);
}
static int IntSign (int i) { return (i<0)?-1:(i>0)?1:0; }

static void Con_ApplyMouseSelection (void)
{
    const char *line; int len;
    con_selection = con_mouseselection;
    line = Con_GetLine(con_selection.begin.line);
    len = (int)Con_StrLen(con_selection.begin.line);
    if (con_selection.begin.col > len) con_selection.begin.col = len;

    if (con_mouseclicks == 2) {
        int boundary = IntSign(Con_TestWordBoundary(con_selection.begin.col, line, len));
        int dir = IntSign(Con_OfsCompare(&con_selection.end, &con_selection.begin));
        if (boundary && boundary != dir) con_selection.begin.col += boundary;
    }

    if (Con_OfsCompare(&con_selection.begin,&con_selection.end) > 0) {
        conofs_t t = con_selection.begin; con_selection.begin = con_selection.end; con_selection.end = t;
    }

    // If the starting point is beyond the newline, move to the beginning of the next line
    line = Con_GetLine(con_selection.begin.line);
    len = (int)Con_StrLen(con_selection.begin.line);
    if (con_selection.begin.col > len) {
        con_selection.begin.line++;
        con_selection.begin.col = 0;
        // Fix up the end point if necessary
        if (Con_OfsCompare(&con_selection.begin, &con_selection.end) > 0)
            con_selection.end = con_selection.begin;
    }

    if (con_mouseclicks <= 1) return;               /* char selection */
    if (con_mouseclicks >= 4) { Con_SelectAll(); return; }
    if (con_mouseclicks == 3) {                      /* line */
        con_selection.begin.col = 0;
        con_selection.end.col = 0;
        con_selection.end.line = q_min(con_selection.end.line, con_current) + 1;
        return;
    }
    /* word selection */
    line = Con_GetLine(con_selection.begin.line);
    len  = (int)Con_StrLen(con_selection.begin.line);
    while (!Con_TestWordBoundary(con_selection.begin.col, line, len)) --con_selection.begin.col;
    if (con_selection.end.line <= con_current) {
        line = Con_GetLine(con_selection.end.line);
        len  = (int)Con_StrLen(con_selection.end.line);
        while (!Con_TestWordBoundary(con_selection.end.col, line, len)) ++con_selection.end.col;
    }
}

static void Con_SetMouseState (conmouse_t state)
{
    int x,y; conofs_t pos;
    if (con_mousestate == state) return;
    switch (state) {
    case CMS_PRESSED:
        SDL_GetMouseState(&x,&y);
        Con_ScreenToCanvas(x,y,&con_clickx,&con_clicky);
        Con_CanvasToOffset(con_clickx,con_clicky,&pos,CT_NEAREST);
        if (con_mouseclicks == 0 || con_mouseclickdelay >= DOUBLECLICK_TIME
            || Con_OfsCompare(&pos,&con_mouseselection.end) != 0) con_mouseclicks = 1;
        else con_mouseclicks++;
        con_mouseclickdelay = 0.0;

        // woods selection - shift click extends selection
        if (keydown[K_SHIFT] && Con_HasSelection()) {
            con_mouseselection.end = pos; // Extend end point
            // con_mouseselection.begin remains as the anchor
        } else {
            con_mouseselection.begin = con_mouseselection.end = pos; // Start new selection
        }
        
        Con_ApplyMouseSelection();
        break;
    case CMS_DRAGGING:
        Con_SetHotLink(NULL);
        break;
    case CMS_NOTPRESSED:
        if (con_mousestate != CMS_DRAGGING && Con_LinksEnabled() && con_hotlink && !Sys_Explore(con_hotlink->path))
            S_LocalSound("misc/menu2.wav");
        con_scrolldelta = 0.f; con_scrollspeed = 0.f;
        break;
    default: break;
    }
    con_mousestate = state;
}

static void Con_Mousemove (int x, int y)
{
    if (con_mousestate == CMS_NOTPRESSED) {
        conofs_t ofs;
        int cx, cy;
        qboolean inside;

        Con_ScreenToCanvas(x,y,&cx,&cy);
        inside = Con_CanvasToOffset(cx,cy,&ofs,CT_INSIDE);
        Con_SetHotLink(inside ? Con_GetLinkAtOfs(&ofs) : NULL);
        /* Show appropriate cursor while console is active */
        if (Con_CursorActive()) {
            if (con_hotlink)           Con_SetCursor(con_cursor_hand ? con_cursor_hand : con_cursor_arrow);
            else if (inside)           Con_SetCursor(con_cursor_ibeam);
            else                       Con_SetNeutralCursor();
        }
    } else {
        conofs_t ofs;
        int cx, cy, delta; float frac;
        qboolean inside;
        Con_ScreenToCanvas(x,y,&cx,&cy);
        inside = Con_CanvasToOffset(cx,cy,&ofs,CT_INSIDE);
        Con_CanvasToOffset(cx,cy,&con_mouseselection.end,CT_NEAREST);
        if (Con_CursorActive()) {
            if (inside)                Con_SetCursor(con_cursor_ibeam);
            else                       Con_SetNeutralCursor();
        }
        Con_ApplyMouseSelection();
        if (Con_OfsCompare(&con_mouseselection.begin,&con_mouseselection.end) != 0)
            Con_SetMouseState(CMS_DRAGGING);
        delta = cy + con_vislines/2 - vid.conheight;
        if (abs(delta) < con_vislines/2 - CON_SCROLL_ZONE) delta = 0;
        else delta -= IntSign(delta) * (con_vislines/2 - CON_SCROLL_ZONE);
        if (delta < -CON_SCROLL_ZONE) delta = -CON_SCROLL_ZONE;
        if (delta >  CON_SCROLL_ZONE) delta =  CON_SCROLL_ZONE;
        if (delta < 0) {
            int moved = cy - con_clicky;
            int scrolled = q_min(con_mouseselection.end.line - con_mouseselection.begin.line, 0) * CHARSIZE;
            delta = q_max(delta, moved + scrolled/4);
            if (delta > 0) delta = 0;
        }
        frac = (float)delta / (float)CON_SCROLL_ZONE;
        frac *= fabsf(frac);
        con_scrollspeed = -CON_MAX_SCROLL_SPEED * frac;
        if (!delta) con_scrolldelta = 0.f;
    }
}

void Con_ForceMouseMove (void)
{
    int x,y; SDL_GetMouseState(&x,&y); Con_Mousemove(x,y);
}

void Con_Scroll(int lines)
{
    con_backscroll += lines;
    if (con_backscroll < 0) con_backscroll = 0;
    if (con_backscroll > con_totallines - (glheight>>3) - 1)
        con_backscroll = con_totallines - (glheight>>3) - 1;
}

static void Con_UpdateMouseState (void)
{
    qboolean active = Con_CursorActive();

    if (active != con_cursor_was_active) {
        con_cursor_was_active = active;
        con_mouseclicks = 0;
        con_mouseclickdelay = DOUBLECLICK_TIME;
        Con_SetMouseState(CMS_NOTPRESSED);
        if (active) {
            Uint32 btns = SDL_GetMouseState(NULL, NULL);
            /* Ignore a held menu click until release when the loading console takes focus. */
            con_blockselectionuntilrelease = (btns & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        } else {
            con_blockselectionuntilrelease = false;
        }
    }

    if (!active) {
        Con_SetHotLink(NULL);
        Con_SetMouseState(CMS_NOTPRESSED);
        Con_ClearSelection();
        /* Do NOT change OS cursor visibility here; menu/game own it now. */
        Con_SetCursor(NULL); /* leave shape alone when returning to game/menu */
        return;
    }
    SDL_ShowCursor(SDL_ENABLE);  /* console owns the OS cursor while open */

    /* prevent clicks from leaking to the game while console is active */
    keydown[K_MOUSE1] = keydown[K_MOUSE2] = keydown[K_MOUSE3] = false;
    keydown[K_MWHEELUP] = keydown[K_MWHEELDOWN] = false;

    Con_ForceMouseMove();
    /* use actual SDL button state, not the global keydown[] */
    {
        Uint32 btns = SDL_GetMouseState(NULL, NULL);
        qboolean left_down = (btns & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        if (con_blockselectionuntilrelease) {
            if (left_down)
                return;
            con_blockselectionuntilrelease = false;
        }
        if (!left_down) Con_SetMouseState(CMS_NOTPRESSED);
        else if (con_mousestate == CMS_NOTPRESSED) Con_SetMouseState(CMS_PRESSED);
    }
    con_mouseclickdelay += host_frametime;
    con_scrolldelta += con_scrollspeed * host_frametime;
    if (fabsf(con_scrolldelta) >= 1.f) {
        int lines = (int)con_scrolldelta;
        Con_Scroll(lines);
        con_scrolldelta -= lines;
    }
}

static void Con_DrawSelectionHighlight (int x, int y, int line)
{
    conofs_t sb,se,b,e; size_t len;
    if (!Con_GetNormalizedSelection(&sb,&se)) return;
    len = Con_StrLen(line);
    b.line = line; b.col = 0;
    e.line = line; e.col = (int)len;
    if (e.line != se.line && e.col == (int)len) e.col++; /* highlight EOL */
    if (e.col > con_linewidth) e.col = con_linewidth;
    if (!Con_IntersectRanges(&b,&e,&sb,&se)) return;
    Draw_Fill(x + b.col*CHARSIZE, y, (e.col - b.col)*CHARSIZE, CHARSIZE, 170, 0.4f);
}

/* Map Quake's 8-bit console charset (incl. gold digits) to plain UTF-8/ASCII.
   We strip the 0x80 color bit and translate via dequake[] (same mapping used
   by Con_Dump_f), so pasted text is readable and digits don't disappear. */
static size_t UTF8_FromQuake(char *dst, size_t maxbytes, const char *src)
{
    size_t len = 0;
    const unsigned char *s = (const unsigned char*)src;
    if (!dst) {
        /* length incl. NUL, same as src */
        while (*s++) len++;
        return len + 1;
    }
    while (*s && len + 1 < maxbytes) {
        unsigned char q = (*s++) & 0x7f;  /* drop colour bit */
        if (q == '\n' || q == '\r' || q == '\t') {
            dst[len++] = (char)q;         /* preserve paste-friendly whitespace */
            continue;
        }
        /* translate everything else via dequake (handles gold digits, quakebar, etc.) */
        {
            unsigned char a = dequake[q];
            if (!a) a = ' ';              /* ensure printable */
            dst[len++] = (char)a;
        }
    }
    dst[len] = '\0';
    return len + 1;
}

qboolean Con_CopySelectionToClipboard(void)
{
	conofs_t sb, se, cur, eol;
	char* qtext = NULL, * utf8 = NULL;
	size_t total_bytes = 0, pos = 0, maxsize;

	if (!Con_GetNormalizedSelection(&sb, &se))
		return false;

	/* -------- Pass 1: compute exact byte count -------- */
	for (cur = sb; Con_OfsCompare(&cur, &se) <= 0; cur.line++, cur.col = 0) {
		/* sizing pass: no need to fetch line text */
		eol.line = cur.line; eol.col = (int)Con_StrLen(cur.line);
		if (cur.line == se.line) eol.col = q_min(eol.col, se.col);
		size_t n = (eol.col > cur.col) ? (size_t)(eol.col - cur.col) : 0;
		total_bytes += n;
		if (eol.line != se.line)
			total_bytes += 1; /* newline we insert between lines */
	}

	if (total_bytes == 0)
		return false;

	/* Optional guardrail: cap copy size (8 MiB) to avoid giant allocations */
	static const size_t CON_COPY_MAX = (size_t)8 * 1024 * 1024;
	if (total_bytes > CON_COPY_MAX) {
		Con_Warning("Selection too large to copy (%lu bytes > %lu).\n",
			(unsigned long)total_bytes, (unsigned long)CON_COPY_MAX);
		return false;
	}

	/* -------- Pass 2: allocate once and fill -------- */
	qtext = (char*)malloc(total_bytes + 1);
	if (!qtext) {
		Con_Warning("Out of memory copying selection (alloc %lu bytes).\n",
			(unsigned long)(total_bytes + 1));
		return false;
	}

	for (cur = sb; Con_OfsCompare(&cur, &se) <= 0; cur.line++, cur.col = 0) {
		const char* text = Con_GetLine(cur.line);
		eol.line = cur.line; eol.col = (int)Con_StrLen(cur.line);
		if (cur.line == se.line) eol.col = q_min(eol.col, se.col);
		size_t n = (eol.col > cur.col) ? (size_t)(eol.col - cur.col) : 0;
		if (n) {
			memcpy(qtext + pos, text + cur.col, n);
			pos += n;
		}
		if (eol.line != se.line) {
			qtext[pos++] = '\n';
		}
	}
	qtext[pos] = '\0';

	/* quake->utf8 (really ASCII via dequake, preserving \n/\r/\t) */
	maxsize = UTF8_FromQuake(NULL, 0, qtext);
	utf8 = (char*)malloc(maxsize);
	if (!utf8) {
		Con_Warning("Out of memory converting selection to UTF-8 (%lu bytes).\n",
			(unsigned long)maxsize);
		free(qtext);
		return false;
	}
	UTF8_FromQuake(utf8, maxsize, qtext);

	/* On Windows, normalize to CRLF to paste nicely in Notepad etc. */
#ifdef _WIN32
	{
		size_t i = 0, extra = 0;
		while (utf8[i]) {
			if (utf8[i] == '\n' && (i == 0 || utf8[i - 1] != '\r')) extra++;
			i++;
		}
		if (extra) {
			char* crlf = (char*)malloc(i + extra + 1);
			if (!crlf) {
				Con_Warning("Out of memory normalizing CRLF (%lu bytes).\n",
					(unsigned long)(i + extra + 1));
				SDL_SetClipboardText(utf8); /* fall back with \n */
			}
			else {
				size_t w = 0;
				for (size_t r = 0; r < i; ++r) {
					if (utf8[r] == '\n' && (r == 0 || utf8[r - 1] != '\r'))
						crlf[w++] = '\r';
					crlf[w++] = utf8[r];
				}
				crlf[w] = 0;
				SDL_SetClipboardText(crlf);
				free(crlf);
			}
		}
		else {
			SDL_SetClipboardText(utf8);
		}
	}
#else
	SDL_SetClipboardText(utf8);
#endif
	free(utf8);
	free(qtext);
	return true;
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
	IN_UpdateGrabs();
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
	IN_UpdateGrabs();
	SetChatInfo (CIF_CHAT); // woods #chatinfo
}

static void Con_RecalcOffset (conofs_t *ofs, int oldcurrent)
{
	ofs->col = q_min (ofs->col, con_linewidth);
	ofs->line += con_totallines - 1 - oldcurrent;
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

	for (i = 0; i < (int) CON_VEC_SIZE(con_links); i++)
	{
		conlink_t *link = con_links[i];
		Con_RecalcOffset(&link->begin, con_current);
		Con_RecalcOffset(&link->end, con_current);
	}

	Con_ClearNotify ();

	con_backscroll = 0;
	con_current = con_totallines - 1;
	Con_PruneLinks();
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

	con_text = (char *) Hunk_AllocNameNoFill (con_buffersize, "context");//johnfitz -- con_buffersize replaces CON_TEXTSIZE
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
	Cvar_RegisterVariable (&con_typing); // woods #typing...
	Cvar_RegisterVariable (&con_cursorcolor); // woods #cursorcolor
	Cvar_RegisterVariable (&con_clear_input_on_toggle); // R00k
	Cvar_RegisterVariable (&con_autohint);
	Cvar_SetCompletion (&con_cursorcolor, &Con_CursorColor_Completion_f); // woods #iwtabcomplete
	Cvar_SetCallback (&con_notifydiscord, &ConNotifyDiscord_Callback); // woods #discord
	Cvar_SetCallback (&con_autohint, &Con_Autohint_Callback);


	Cmd_AddCommand ("toggleconsole", Con_ToggleConsole_f);
	Cmd_AddCommand ("messagemode", Con_MessageMode_f);
	Cmd_AddCommand ("messagemode2", Con_MessageMode2_f);
	Cmd_AddCommand ("clear", Con_Clear_f);
	Cmd_AddCommand ("condump", Con_Dump_f); //johnfitz
	Cmd_AddCommand ("qwmaplist", QWMapList_f);
	con_initialized = true;

	// woods #conselection
    con_cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    con_cursor_ibeam = LoadCustomIBeamCursor();
    con_cursor_hand  = LoadCustomLinkCursor();
    con_cursor_current = NULL;
	// woods #conselection - start hidden; Con_UpdateMouseState/EnterCursorMode enables when console is open
    SDL_ShowCursor(SDL_DISABLE);
}


/*
======================
Con_ReloadIBeamCursor -- woods #customcursor
Reload console cursors when video mode changes, so they scale correctly.
======================
*/
void Con_ReloadIBeamCursor (void)
{
    SDL_Cursor *old_ibeam = con_cursor_ibeam;
    SDL_Cursor *old_hand = con_cursor_hand;

    con_cursor_ibeam = LoadCustomIBeamCursor();
    con_cursor_hand = LoadCustomLinkCursor();

    if (con_cursor_current == old_ibeam)
        Con_SetCursor(con_cursor_ibeam);
    else if (con_cursor_current == old_hand)
        Con_SetCursor(con_cursor_hand);

    if (old_ibeam)
        SDL_FreeCursor(old_ibeam);
    if (old_hand)
        SDL_FreeCursor(old_hand);

    /* the Con_Init attempts run before SDL video init and fail, leaving these
       NULL all session; (re)create them here now that video is up */
    if (!con_cursor_arrow)
        con_cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
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
	Con_PruneLinks();
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

	// If it's a valid player message, look for "likes"/"loves" and format it, for qss(m) white chars
	if (valid_player)
	{
		const char* search = ": likes ";
		const char* verb = "likes";
		const char* likes_pos = strstr(txt, search);

		if (!likes_pos)
		{
			search = ": loves ";
			verb = "loves";
			likes_pos = strstr(txt, search);
		}

		if (likes_pos)
		{
			size_t new_len = strlen(txt) + strlen("^m^m"); // Additional length for ^m markers
			char* modified_txt = (char*)malloc(new_len + 1); // +1 for null terminator

			if (modified_txt)
			{
				size_t prefix_len = likes_pos - txt;

				memcpy(modified_txt, txt, prefix_len); // Copy before ": likes "/"loves "
				q_snprintf(modified_txt + prefix_len, new_len + 1 - prefix_len, ": ^m%s^m ", verb);
				Q_strcpy(modified_txt + prefix_len + strlen(": ^m") + strlen(verb) + strlen("^m "),
					likes_pos + strlen(search));

				// Don't copy "likes"/"loves" messages to lastchat
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

static void RideDelay_Deferred(void *param) // runequake changelevel hack -- runs on the main thread via Host_DeferCall
{
	strncpy(cl.observer, "y", sizeof(cl.observer));
	cl.modtype = 6;
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
	qboolean	gold_digits;
	qboolean	boundary, skipnotify;
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
			Host_DeferCall(2.0, RideDelay_Deferred, NULL);
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
	gold_digits = false;

	skipnotify = false;
	if (!Q_strncmp (txt, "[skipnotify]", 12))
	{
		skipnotify = true;
		txt += 12;
	}

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
				gold_digits = false;
				txt+=2;
				continue;
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
			case 'g':	//toggle gold digits.
				txt+=2;
				gold_digits ^= true;
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
				con_times[con_current % NUM_CON_TIMES] = skipnotify ? 0 : realtime;
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
			if (gold_digits && c >= '0' && c <= '9')
				c -= 30;
			y = con_current % con_totallines;
			con_text[y*con_linewidth+con_x] = c | mask;
			con_x++;
			if (con_x >= con_linewidth)
				con_x = 0;
			break;
		}
	}
}

/*
================
Con_StripControlPrefixes
================
*/
static const char *Con_StripControlPrefixes (const char *txt)
{
	if (txt[0] == 1 || txt[0] == 2)
		txt++;

	if (!Q_strncmp (txt, "[skipnotify]", 12))
		txt += 12;

	return txt;
}

#if defined(_WIN32) // woods #debuglogsize
#include <io.h>
typedef __int64        log_off_t;
#define q_lseek        _lseeki64
#define q_truncate(fd, sz)  _chsize_s((fd), (sz))
#define q_write        _write
#define q_read         _read
/* 64-bit stat wrappers and unlink for Windows */
typedef struct _stat64 q_stat_t;
#define q_fstat        _fstat64
#define q_unlink       _unlink
#else
#include <unistd.h>
typedef off_t          log_off_t;
#define q_lseek        lseek
#define q_truncate     ftruncate
#define q_write        write
#define q_read         read
/* POSIX stat wrappers and unlink */
typedef struct stat    q_stat_t;
#define q_fstat        fstat
#define q_unlink       unlink
#endif

// borrowed from uhexen2 by S.A. for new procs, LOG_Init, LOG_Close

static char logfilename[MAX_OSPATH];    // current logfile name
static int  log_fd = -1;                // log file descriptor
static log_off_t log_cap = 0; // woods #condebug
static log_off_t log_size = 0; // current cached size of log file
static qboolean log_roll_pending = false; // deferred roll flag
#define LOG_CAP_MAX_MIB   2048  /* clamp to avoid overflow: 2 GiB max */
#define LOG_CAP_MIN_MIB   1
static SDL_mutex *log_mutex = NULL; /* serialize log I/O */
static const char rollover_banner[] =
    "--- log rolled (size cap reached) ---\n";
static const char startup_banner[] =
    "--- log truncated at start-up (exceeded cap) ---\n";

static log_off_t safe_write(int fd, const char* buf, size_t len) {
    log_off_t total = 0;
    while (len) {
        int n = (int)q_write(fd, buf, (unsigned int)len);
        if (n > 0) {
            buf += n;
            len -= (size_t)n;
            total += (log_off_t)n;
        }
        else if (n < 0 && errno == EINTR)
            continue;
        else
            break;
    }
    return total;
}

static void LOG_Lock(void)   { if (log_mutex) SDL_LockMutex(log_mutex); }
static void LOG_Unlock(void) { if (log_mutex) SDL_UnlockMutex(log_mutex); }

static void LOG_UpdateSize_Locked(void)
{
    q_stat_t st;
    if (log_fd == -1)
        return;
    if (!q_fstat(log_fd, &st))
        log_size = (log_off_t)st.st_size;
}

/* simple helper: "-condebug [N]" where N is MiB. No N => unlimited */
static log_off_t LOG_ParseCapFromCLI(void)
{
    int idx = COM_CheckParm("-condebug");
    if (!idx)
        return -1;   /* logging disabled */

    const char *arg = NULL;

    /* Accept:  -condebug 5     OR   -condebug=5   */
    if (idx + 1 < com_argc)
        arg = com_argv[idx + 1];

    if (!arg || !q_isdigit(arg[0])) {
        /* try "-condebug=5" */
        const char *eq = strchr(com_argv[idx], '=');
        if (eq)
            arg = eq + 1;
    }

    if (!arg || !q_isdigit(arg[0]))
        return 0; /* flag with no size or junk value => unlimited */

    char *endptr = NULL;
    long long mib = strtoll(arg, &endptr, 10);
    if (endptr && *endptr)
        return 0; /* not a clean number */

    if (mib < LOG_CAP_MIN_MIB)
        return 0;
    if (mib > LOG_CAP_MAX_MIB)
        mib = LOG_CAP_MAX_MIB;

    /* use 64-bit intermediate to avoid overflow when scaling to bytes */
    int64_t bytes = (int64_t)mib * 1024 * 1024;
    if (bytes <= 0)
        return 0;

    return (log_off_t)bytes;
}

/* --------------------------------------------------------------------------
 * LOG_RollTail_Locked
 *  Keep the newest part of the file so the total stays <= log_cap.
 *  Writes a banner at the top explaining why it rolled.
 *  CALLER MUST HOLD LOG_Lock().
 * -------------------------------------------------------------------------- */
static void LOG_RollTail_Locked(const char *reason)
{
    if (log_fd == -1 || log_cap <= 0)
        return;

    q_stat_t st;
    if (q_fstat(log_fd, &st) != 0)
    { return; }

    /* Calculate banner length once */
    size_t banner_len = reason ? strlen(reason) : 0;

    /* Leave ~4 KB headroom for the banner and a few post-roll writes. */
    log_off_t keep = log_cap - 4096;
    if (keep < 0) keep = 0;

    /* Guard against tiny caps that can't fit the banner */
    if (keep <= (log_off_t)banner_len) keep = 0;

    if ((log_off_t)st.st_size <= keep)   /* already small enough */
    { return; }

    /* If keep is 0, just truncate and write banner */
    if (keep == 0) {
        q_lseek(log_fd, 0, SEEK_SET);
        if (q_truncate(log_fd, 0) != 0) {
            /* If truncate fails, try to close and reopen */
            close(log_fd);
#ifdef _WIN32
            log_fd = open(logfilename, O_RDWR | O_CREAT | O_TRUNC | O_APPEND | O_BINARY, 0666);
#else
            log_fd = open(logfilename, O_RDWR | O_CREAT | O_TRUNC | O_APPEND, 0666);
#endif
            if (log_fd == -1) {
                return; /* Give up */
            }
        }
        if (reason)
            safe_write(log_fd, reason, banner_len);
        LOG_UpdateSize_Locked();
        return;
    }

    log_off_t start = (log_off_t)st.st_size - keep;
    if (start < 0) start = 0;

    /* Chunked tail copy via temporary file to bound memory usage */
    {
        char tmpname[MAX_OSPATH];
        q_snprintf(tmpname, sizeof(tmpname), "%s.tmp", logfilename);

        /* Open temp file */
#ifdef _WIN32
        int tmp_fd = open(tmpname, O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0666);
#else
        int tmp_fd = open(tmpname, O_RDWR | O_CREAT | O_TRUNC, 0666);
#endif
        if (tmp_fd == -1)
            goto roll_fallback;

        const size_t chunk_size = (size_t)1 << 20; /* 1 MiB */
        char *buf = (char *)malloc(chunk_size);
        if (!buf)
        {
            close(tmp_fd);
            q_unlink(tmpname);
            goto roll_fallback;
        }

        if (q_lseek(log_fd, start, SEEK_SET) == (log_off_t)-1)
        {
            free(buf);
            close(tmp_fd);
            q_unlink(tmpname);
            goto roll_fallback;
        }

        /* Copy desired tail into temp file */
        {
            log_off_t remaining = keep;
            while (remaining > 0)
            {
                size_t to_read = (size_t)((remaining > (log_off_t)chunk_size) ? chunk_size : remaining);
                int rd = (int)q_read(log_fd, buf, (unsigned int)to_read);
                if (rd <= 0)
                {
                    free(buf);
                    close(tmp_fd);
                    q_unlink(tmpname);
                    goto roll_fallback;
                }
                safe_write(tmp_fd, buf, (size_t)rd);
                remaining -= (log_off_t)rd;
            }
        }

        /* Truncate original and write banner + tail from temp */
        q_lseek(log_fd, 0, SEEK_SET);
        if (q_truncate(log_fd, 0) != 0)
        {
            /* If truncate fails, try to close and reopen */
            close(log_fd);
#ifdef _WIN32
            log_fd = open(logfilename, O_RDWR | O_CREAT | O_TRUNC | O_APPEND | O_BINARY, 0666);
#else
            log_fd = open(logfilename, O_RDWR | O_CREAT | O_TRUNC | O_APPEND, 0666);
#endif
            if (log_fd == -1)
            {
                free(buf);
                close(tmp_fd);
                q_unlink(tmpname);
                return; /* Give up */
            }
        }

        if (reason)
            safe_write(log_fd, reason, banner_len);

        /* Rewind temp and stream back into original */
        q_lseek(tmp_fd, 0, SEEK_SET);
        for (;;)
        {
            int rd = (int)q_read(tmp_fd, buf, (unsigned int)chunk_size);
            if (rd <= 0)
                break;
            safe_write(log_fd, buf, (size_t)rd);
        }

        free(buf);
        close(tmp_fd);
        q_unlink(tmpname);
        LOG_UpdateSize_Locked();
        return;
    }

roll_fallback:
    /* Fallback: full truncate and banner only */
    q_lseek(log_fd, 0, SEEK_SET);
    if (q_truncate(log_fd, 0) != 0) {
        /* If truncate fails, try to close and reopen */
        close(log_fd);
#ifdef _WIN32
        log_fd = open(logfilename, O_RDWR | O_CREAT | O_TRUNC | O_APPEND | O_BINARY, 0666);
#else
        log_fd = open(logfilename, O_RDWR | O_CREAT | O_TRUNC | O_APPEND, 0666);
#endif
        if (log_fd == -1) {
            return; /* Give up */
        }
    }
    if (reason)
        safe_write(log_fd, reason, banner_len);
    LOG_UpdateSize_Locked();
    return;
}

/*
================
Con_DebugLog
================
*/
void Con_DebugLog(const char *msg)
{
	if (!con_debuglog || !msg)
		return;

	size_t msg_len = strlen(msg);
	log_off_t written;

	if (msg_len == 0)
		return;

	LOG_Lock();
	if (log_fd == -1) {
		LOG_Unlock();
		return;
	}

	if (log_cap > 0) {
		if (log_size + (log_off_t)msg_len > log_cap)
			/* Defer tail-copy truncation to shutdown; avoid runtime hitches. */
			log_roll_pending = true;
	}

	written = safe_write(log_fd, msg, msg_len);
	log_size += written;
	if (written < (log_off_t)msg_len)
	{
		close(log_fd);
		log_fd = -1;
		con_debuglog = false;
		log_size = 0;
		log_roll_pending = false;
		LOG_Unlock();
		fprintf(stderr, "Error writing to log file\n");
		return;
	}

	LOG_Unlock();
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
		q_strlcat(con_redirect_buffer, Con_StripControlPrefixes(msg), sizeof(con_redirect_buffer));

// also echo to debugging console
	Sys_Printf ("%s", Con_StripControlPrefixes(msg));

	unsigned char* ch; // woods dequake
	for (ch = (unsigned char*)demsg; *ch; ch++)
		*ch = dequake[*ch];

	// log all messages to file
	if (con_debuglog)
		Con_DebugLog(Con_StripControlPrefixes(demsg)); // woods dequake

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
Con_LinkPrintf

Prints text that opens a link when clicked in windowed mode
==================
*/
void Con_LinkPrintf (const char *addr, const char *fmt, ...)
{
	conlink_t	*link;
	size_t		len;
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start (argptr, fmt);
	q_vsnprintf (msg, sizeof(msg), fmt, argptr);
	va_end (argptr);

	if (!addr || !addr[0] || !con_initialized || cls.state == ca_dedicated)
	{
		Con_SafePrintf ("%s", msg);
		return;
	}

	len = strlen (addr);
	link = (conlink_t *) malloc (sizeof(conlink_t) + len + 1);
	if (!link)
		Sys_Error ("Con_LinkPrintf: out of memory on %" SDL_PRIu64 " bytes", (uint64_t)(sizeof(conlink_t) + len + 1));

	memcpy (link + 1, addr, len + 1);
	link->path = (const char *)(link + 1);
	link->begin.col = con_x;
	link->begin.line = con_current + (con_x == 0 && msg[0] ? 1 : 0);
	link->end = link->begin;

	Con_SafePrintf ("%s", msg);

	link->end.line = con_current;
	link->end.col = con_x;

	while (Con_OfsCompare(&link->begin, &link->end) < 0)
	{
		const char *text = Con_GetLine(link->begin.line);
		if ((text[link->begin.col] & 0x7f) != ' ')
			break;
		if (++link->begin.col == con_linewidth)
		{
			link->begin.col = 0;
			link->begin.line++;
		}
	}

	if (Con_OfsCompare(&link->begin, &link->end) >= 0)
	{
		free(link);
		return;
	}

	CON_VEC_PUSH(con_links, link);
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
	char		msg[MAXPRINTMSG];
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

	centerprint_pending = false;
}

/*
================
Con_UpdateCenterPrint -- main-thread poll for a deferred centerprint - woods #centerlog
Runs once per frame on the MAIN thread. This must never be driven from an SDL timer
thread: Con_ExecuteCenterPrint -> Con_Printf can reach SCR_UpdateScreen/OpenGL, and GL
calls off the main thread crash (EXC_BAD_ACCESS in glMatrixMode).
================
*/
void Con_UpdateCenterPrint(void)
{
	if (!centerprint_pending)
		return;

	if (!Con_HasActiveNotifications())
		Con_ExecuteCenterPrint(centerprint_pending_text);
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

	// If no notifications are active, print immediately; otherwise leave it pending
	// for Con_UpdateCenterPrint() to flush from the main thread once they clear.
	if (!Con_HasActiveNotifications())
		Con_ExecuteCenterPrint(str);
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
static char dedicated_tab_partial[MAXCMDLINE];
typedef struct tab_s
{
	const char	*name;
	const char	*matchname;
	char date[20]; // woods #demolistsort
	const char	*type;
	struct tab_s	*next;
	struct tab_s	*prev;
	int			count; // woods #iwtabcomplete
} tab_t;
tab_t	*tablist;

void Con_DedicatedResetTabState(void)
{
	dedicated_tab_partial[0] = '\0';
}

void Con_DedicatedTabComplete(char* text, size_t buf_size, int* textlen, int* cursor_pos)
{
	extern char key_lines[CMDLINES][MAXCMDLINE];
	extern size_t key_linepos;
	extern int edit_line;
	extern char key_tabhint[MAXCMDLINE];
	char saved_line[MAXCMDLINE];
	char saved_tabpartial[MAXCMDLINE];
	char saved_tabhint[MAXCMDLINE];
	size_t saved_pos = key_linepos;
	qboolean saved_shift = keydown[K_SHIFT];

	memcpy(saved_line, key_lines[edit_line], sizeof(saved_line));
	memcpy(saved_tabpartial, key_tabpartial, sizeof(saved_tabpartial));
	memcpy(saved_tabhint, key_tabhint, sizeof(saved_tabhint));

	key_lines[edit_line][0] = ' ';
	key_lines[edit_line][1] = '\0';
	q_strlcpy(key_lines[edit_line] + 1, text, MAXCMDLINE - 1);

	size_t desired_pos = 1;
	if (cursor_pos)
	{
		desired_pos = (size_t)(*cursor_pos + 1);
		if (desired_pos >= MAXCMDLINE)
			desired_pos = MAXCMDLINE - 1;
		if (desired_pos < 1)
			desired_pos = 1;
	}
	key_linepos = desired_pos;

	memcpy(key_tabpartial, dedicated_tab_partial, sizeof(key_tabpartial));
	key_tabhint[0] = '\0';
	keydown[K_SHIFT] = false;

	Con_TabComplete(TABCOMPLETE_USER);

	q_strlcpy(text, key_lines[edit_line] + 1, buf_size);
	
	// Sanitize output to prevent display artifacts (brackets/control chars)
	for (char* p = text; *p; p++)
	{
		if ((unsigned char)*p < ' ' || *p == 127 || *p == '[' || *p == ']')
			*p = ' ';
	}

	size_t new_len = strlen(text);
	if (textlen)
	{
		if (new_len > (size_t)INT_MAX)
			new_len = INT_MAX;
		*textlen = (int)new_len;
	}

	if (cursor_pos)
	{
		int new_cursor = 0;
		if (key_linepos > 0)
			new_cursor = (int)key_linepos - 1;
		if (new_cursor < 0)
			new_cursor = 0;
		if ((size_t)new_cursor > new_len)
			new_cursor = (int)new_len;
		*cursor_pos = new_cursor;
	}

	memcpy(dedicated_tab_partial, key_tabpartial, sizeof(dedicated_tab_partial));

	memcpy(key_lines[edit_line], saved_line, sizeof(saved_line));
	key_linepos = saved_pos;
	memcpy(key_tabpartial, saved_tabpartial, sizeof(saved_tabpartial));
	memcpy(key_tabhint, saved_tabhint, sizeof(saved_tabhint));
	keydown[K_SHIFT] = saved_shift;
}

//defs from elsewhere
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
static int tablist_omitted_count;
static int tablist_limited_count;

// woods -- internal helper. match_name is what's used for Con_Match and the
// bash_partial common-substring logic; if NULL we fall back to name. callers
// that want "type ascii to find a quake-special-char entry, but insert the
// original chars on completion" pass the dequaked form as match_name and the
// original as name.
static void Con_AddToTabListInternal (const char* name, const char* partial, const char* type, const char* param, const char* match_name)
{
	tab_t* t, * insert;
	char* i_bash, * i_bash2;
	const char* i_name, * i_name2;
	size_t	namelen, matchlen, typelen;
	int		allocsize, mark;
	const char* match = match_name ? match_name : name;
	qboolean match_is_name = (!match_name || !strcmp(match_name, name));
	char*	storage;

	if (!Con_Match (match, partial))
		return;

	int FileIsDemo = (param != NULL); // woods #demolistsort

	if (!*bash_partial && bash_singlematch)
	{
		q_strlcpy (bash_partial, match, sizeof(bash_partial));
	}
	else
	{
		bash_singlematch = 0;
		i_bash = q_strcasestr (bash_partial, partial);
		i_name = q_strcasestr (match, partial);
		SDL_assert (i_bash);
		SDL_assert (i_name);
		if (i_name && i_bash)
		{
			i_bash2 = i_bash;
			i_name2 = i_name;
			// find max common between bash_partial and match (right side)
			while (*i_bash && q_toupper (*i_bash) == q_toupper (*i_name))
			{
				i_bash++;
				i_name++;
			}
			*i_bash = 0;
			// find max common between bash_partial and match (left side)
			while (i_bash2 != bash_partial && i_name2 != match &&
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
	namelen = strlen(name) + 1;
	matchlen = match_is_name ? 0 : strlen(match) + 1;
	typelen = type ? strlen(type) + 1 : 0;
	allocsize = (int)(sizeof(tab_t) + namelen + matchlen + typelen);
	t = (tab_t*)Hunk_AllocName (allocsize, "tablist");
	storage = (char*)(t + 1);
	memcpy (storage, name, namelen);
	t->name = storage;
	storage += namelen;
	if (match_is_name)
	{
		t->matchname = t->name;
	}
	else
	{
		memcpy(storage, match, matchlen);
		t->matchname = storage;
		storage += matchlen;
	}
	if (type)
	{
		memcpy(storage, type, typelen);
		t->type = storage;
	}
	else
		t->type = NULL;
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

void Con_AddToTabList (const char* name, const char* partial, const char* type, const char* param) // woods #iwtabcomplete -- added extra arg for dynamic list type (ie demo vs sky/map etc) #demolistsort
{
	Con_AddToTabListInternal (name, partial, type, param, NULL);
}

// woods -- like Con_AddToTabList, but match against match_name (e.g. dequaked
// ascii) while still inserting/displaying the original name (e.g. with quake
// special chars). used by name history tab complete so the user can type the
// ascii equivalent of a colored name and have the colored name actually applied.
void Con_AddToTabListMatched (const char* name, const char* partial, const char* type, const char* param, const char* match_name)
{
	Con_AddToTabListInternal (name, partial, type, param, match_name);
}

static qboolean Con_IsCompletableModelName (const char *name)
{
	const char *ext;

	if (!name || !name[0])
		return false;
	if (name[0] == '*')
		return false;

	ext = COM_FileGetExtension(name);
	if (!q_strcasecmp(ext, "bsp"))
		return false;

	return true;
}

static size_t Con_ModelNameListPrefixLen (const char *partial)
{
	const char *comma = strrchr(partial, ',');
	const char *semicolon = strrchr(partial, ';');
	const char *separator = comma;
	size_t prefix_len;

	if (!separator || (semicolon && semicolon > separator))
		separator = semicolon;
	if (!separator)
		return 0;

	prefix_len = (size_t)((separator + 1) - partial);
	while (partial[prefix_len] && q_isspace((unsigned char)partial[prefix_len]))
		prefix_len++;

	return prefix_len;
}

void Con_AddModelNamesToTabList (const char *partial, qboolean list_completion)
{
	char candidate[MAXCMDLINE];
	size_t prefix_len;
	int i;

	prefix_len = list_completion ? Con_ModelNameListPrefixLen(partial) : 0;

	for (i = 1; i < cl.model_count && i < MAX_MODELS; i++)
	{
		const char *name = cl.model_name[i];

		if (!Con_IsCompletableModelName(name))
			continue;

		if (prefix_len)
			q_snprintf(candidate, sizeof(candidate), "%.*s%s", (int)prefix_len, partial, name);
		else
			q_strlcpy(candidate, name, sizeof(candidate));

		Con_AddToTabList(candidate, partial, "model", NULL);
	}
}

void Con_ModelName_List_Completion_f (cvar_t *cvar, const char *partial)
{
	(void)cvar;

	if (Cmd_Argc() != 2)
		return;

	Con_AddModelNamesToTabList(partial, true);
}

// woods -- shared name tab-complete helpers used by both player completion
// (CompleteClients in this file) and namehistory completion (CL_Name_Completion_f
// in cl_main.c). they let users type the ascii equivalent of a name containing
// quake special chars and have the original (colored) form actually inserted.

extern char unfun[129];

const char *Con_DequakePartial (const char *partial, char *dst, size_t dstsize)
{
	qboolean has_special = false;
	size_t i;

	if (!dstsize)
		return partial;

	for (i = 0; partial[i] && i < dstsize - 1; i++)
	{
		dst[i] = unfun[partial[i] & 127];
		if (partial[i] != ' ' && partial[i] != dst[i])
			has_special = true;
	}
	dst[i] = '\0';

	return has_special ? dst : partial;
}

void Con_AddNameToTabList (const char *name, const char *partial, const char *match_partial)
{
	char unfun_name[32];
	qboolean has_special_chars = false;
	int j;

	if (!name || !name[0])
		return;

	for (j = 0; name[j] && j < (int)sizeof(unfun_name) - 1; j++)
	{
		unfun_name[j] = unfun[name[j] & 127];
		if (name[j] != ' ' && name[j] != unfun_name[j])
			has_special_chars = true;
	}
	unfun_name[j] = '\0';

	if (has_special_chars)
		Con_AddToTabListMatched (name, match_partial, NULL, NULL, unfun_name);
	else
		Con_AddToTabList (name, partial, NULL, NULL);
}

static void Con_TintTabMatch (const tab_t* t, const char* partial, char* out, size_t outsize)
{
	const char* search;
	const char* found;
	size_t partial_len;

	q_strlcpy(out, t->name, outsize);
	if (!*partial)
		return;

	if (t->matchname == t->name)
	{
		COM_TintSubstring(t->name, partial, out, outsize);
		return;
	}

	partial_len = strlen(partial);
	search = t->matchname;
	while ((found = q_strcasestr(search, partial)) != NULL)
	{
		size_t offset = (size_t)(found - t->matchname);
		size_t i;

		for (i = 0; i < partial_len && offset + i + 1 < outsize && out[offset + i]; i++)
		{
			if (out[offset + i] > ' ')
				out[offset + i] |= 0x80;
		}

		search = found + partial_len;
	}
}

static qboolean Con_CopyTabHint (const tab_t* t, const char* partial, char* out, size_t outsize)
{
	const char* found = q_strcasestr(t->matchname, partial);
	size_t offset, partial_len, namelen;

	if (!found)
		return false;

	offset = (size_t)(found - t->matchname);
	partial_len = strlen(partial);
	namelen = strlen(t->name);

	if (offset + partial_len <= namelen)
	{
		if (!t->name[offset + partial_len])
			return false;
		q_strlcpy(out, t->name + offset + partial_len, outsize);
		return true;
	}

	if (!found[partial_len])
		return false;

	q_strlcpy(out, found + partial_len, outsize);
	return true;
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

static qboolean Con_OpenQuoteIsCvarValue(const char* command_start, const char* quote_contents)
{
	char prefix[MAXCMDLINE];
	char* p;
	char* name;
	cvar_t* cvar;
	size_t len;

	if (!command_start || !quote_contents || quote_contents <= command_start || quote_contents[-1] != '\"')
		return false;

	len = (size_t)(quote_contents - command_start - 1);
	if (len >= sizeof(prefix))
		return false;

	memcpy(prefix, command_start, len);
	prefix[len] = '\0';

	p = prefix;
	while (*p && q_isspace((unsigned char)*p))
		p++;
	if (!*p)
		return false;

	name = p;
	while (*p && !q_isspace((unsigned char)*p))
		p++;
	if (*p)
		*p++ = '\0';

	while (*p && q_isspace((unsigned char)*p))
		p++;
	if (*p)
		return false;

	cvar = Cvar_FindVar(name);
	return cvar && cvar->completion;
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
	const char* quote_contents = NULL;
	qboolean open_quote_cvar_value = false;

	while (*str && str != end)
	{
		char c = *str++;
		if (c == '\"')
		{
			if (!quote)
			{
				quote = ret; // save previous command boundary
				quote_contents = str; // first byte inside the quote
				ret = str; // new command
			}
			else
			{
				ret = quote; // restore saved cursor
				quote = NULL;
				quote_contents = NULL;
			}
		}
		else if (c == ';')
			ret = str;
		else if (!quote && c == '/' && *str == '/')
			break;
	}

	if (quote && Con_OpenQuoteIsCvarValue(quote, quote_contents))
	{
		ret = quote;
		open_quote_cvar_value = true;
	}

	while (*ret == ' ')
		ret++;

	q_strlcpy (buf, ret, sizeof(buf));
	if ((uintptr_t)(end - ret) < sizeof(buf))
		buf[end - ret] = '\0';
	end = buf + strlen(buf);

	// woods -- Quake's character set has visible glyphs in the 0x01-0x08 and
	// 0x0E-0x1F range (e.g. 0x10/0x11 are the brackets used in player names like
	// "[DaS].Woods"). The tokenizer treats anything <= 0x20 as whitespace, which
	// would split such names into multiple args and break tab completion (e.g.
	// `name [DaS].Woods` would parse as 3 args, failing the `Cmd_Argc() == 2`
	// check in CL_Name_Completion_f). Replace those visible-but-low chars with a
	// harmless placeholder so the tokenizer keeps the arg count correct. This
	// only affects argv contents, not the `partial` we read directly from the
	// edit line for name matching.
	{
		char *p;
		for (p = buf; *p; p++)
		{
			unsigned char ch = (unsigned char)*p;
			if ((ch >= 0x01 && ch <= 0x08) || (ch >= 0x0E && ch <= 0x1F))
				*p = '_';
		}
	}

	Cmd_TokenizeString (buf);
	// last arg should always be the one we're trying to complete,
	// so we add a new empty one if the command ends with a space
	if (end != buf && end[-1] == ' ' && !open_quote_cvar_value)
		Cmd_AddArg ("");

	return ret;
}

static qboolean CompleteFileList (const char* partial, void* param) // woods #iwtabcomplete
{
	if (Cmd_Argc() != 2)
		return false;
	
	filelist_item_t* file, ** list = (filelist_item_t**)param;
	if (list == &execlist)
		ExecList_EnsureInit (); // built lazily to keep filesystem scans off the launch path
	else if (list == &textlist)
		TextList_EnsureInit ();
	for (file = *list; file; file = file->next)
		Con_AddToTabList (file->name, partial, NULL, NULL);
	return true;
}

static qboolean CompleteServerHistory(const char *partial, void *unused)
{
	filelist_item_t *file;
	(void)unused;

	if (Cmd_Argc() != 2)
		return false;
	for (file = serverlist; file; file = file->next)
	{
		char display[MAX_SERVER_ADDRESS_LEN];
		qboolean friendly;

		if (!file->name[0])
			continue;
		friendly = NET_HostnameCache_FormatDisplay(file->name,
			net_hostport, display, sizeof(display));

		if (friendly && Con_Match(display, partial))
			Con_AddToTabListMatched(file->name, partial, display, NULL, display);
		else
			Con_AddToTabList(file->name, partial,
				friendly ? display : "history", NULL);
	}
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

void FileList_Add(const char* name, const char* data, filelist_item_t** list);

#define DEMO_COMPLETION_MAX_DEPTH 8

static void CompleteDemo_ClearList(filelist_item_t **list)
{
	filelist_item_t *next;

	while (*list)
	{
		next = (*list)->next;
		Z_Free(*list);
		*list = next;
	}
}

#ifndef _WIN32
static void CompleteDemo_FormatDate(time_t mtime, char *out, size_t outlen)
{
	struct tm *tm;

	if (!mtime)
	{
		q_strlcpy(out, "Unknown Date", outlen);
		return;
	}

	tm = localtime(&mtime);
	if (!tm)
	{
		q_strlcpy(out, "Unknown Date", outlen);
		return;
	}

	q_snprintf(out, outlen, "%04d-%02d-%02d %02d:%02d:%02d",
		tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);
}
#endif

static const char *CompleteDemo_StripDemosPrefix(const char *name)
{
	if (name[0] == '.' && (name[1] == '/' || name[1] == '\\'))
		name += 2;
	if (!q_strncasecmp(name, "demos/", 6) || !q_strncasecmp(name, "demos\\", 6))
		return name + 6;
	return name;
}

static void CompleteDemo_AddSubdirDemo(filelist_item_t **list, const char *relpath, const char *date)
{
	if (!relpath || !strchr(relpath, '/'))
		return;

	FileList_Add(relpath, date, list);
}

static qboolean CompleteDemo_ListContains(filelist_item_t *list, const char *name)
{
	filelist_item_t *file;

	for (file = list; file; file = file->next)
	{
		if (!q_strcasecmp(CompleteDemo_StripDemosPrefix(file->name), name))
			return true;
	}

	return false;
}

static void CompleteDemo_ScanPhysicalDir(filelist_item_t **list, const char *basepath, const char *relpath, int depth)
{
	char path[MAX_OSPATH];

	if (depth >= DEMO_COMPLETION_MAX_DEPTH)
		return;

	if (relpath && relpath[0])
		q_snprintf(path, sizeof(path), "%s/%s", basepath, relpath);
	else
		q_strlcpy(path, basepath, sizeof(path));

#ifdef _WIN32
	{
		char searchpath[MAX_OSPATH];
		WIN32_FIND_DATA fdat;
		HANDLE fhnd;

		q_snprintf(searchpath, sizeof(searchpath), "%s/*", path);
		fhnd = FindFirstFile(searchpath, &fdat);
		if (fhnd == INVALID_HANDLE_VALUE)
			return;

		do
		{
			char child_rel[MAX_QPATH];

			if (fdat.cFileName[0] == '.')
				continue;

			if (relpath && relpath[0])
				q_snprintf(child_rel, sizeof(child_rel), "%s/%s", relpath, fdat.cFileName);
			else
				q_strlcpy(child_rel, fdat.cFileName, sizeof(child_rel));

			if (fdat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				CompleteDemo_ScanPhysicalDir(list, basepath, child_rel, depth + 1);
			}
			else
			{
				const char *ext = COM_FileGetExtension(fdat.cFileName);
				if (!q_strcasecmp(ext, "dem") || !q_strcasecmp(ext, "dz"))
				{
					SYSTEMTIME stUTC, stLocal;
					char date[32];

					FileTimeToSystemTime(&fdat.ftLastWriteTime, &stUTC);
					SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);
					q_snprintf(date, sizeof(date), "%04d-%02d-%02d %02d:%02d:%02d",
						stLocal.wYear, stLocal.wMonth, stLocal.wDay,
						stLocal.wHour, stLocal.wMinute, stLocal.wSecond);
					CompleteDemo_AddSubdirDemo(list, child_rel, date);
				}
			}
		} while (FindNextFile(fhnd, &fdat));

		FindClose(fhnd);
	}
#else
	{
		DIR *dir_p;
		struct dirent *dir_t;

		dir_p = opendir(path);
		if (!dir_p)
			return;

		while ((dir_t = readdir(dir_p)) != NULL)
		{
			char fullpath[MAX_OSPATH];
			char child_rel[MAX_QPATH];
			struct stat st;

			if (dir_t->d_name[0] == '.')
				continue;

			q_snprintf(fullpath, sizeof(fullpath), "%s/%s", path, dir_t->d_name);
			if (stat(fullpath, &st) < 0)
				continue;

			if (relpath && relpath[0])
				q_snprintf(child_rel, sizeof(child_rel), "%s/%s", relpath, dir_t->d_name);
			else
				q_strlcpy(child_rel, dir_t->d_name, sizeof(child_rel));

			if (S_ISDIR(st.st_mode))
				CompleteDemo_ScanPhysicalDir(list, basepath, child_rel, depth + 1);
			else if (S_ISREG(st.st_mode))
			{
				const char *ext = COM_FileGetExtension(dir_t->d_name);
				if (!q_strcasecmp(ext, "dem") || !q_strcasecmp(ext, "dz"))
				{
					char date[32];

					CompleteDemo_FormatDate(st.st_mtime, date, sizeof(date));
					CompleteDemo_AddSubdirDemo(list, child_rel, date);
				}
			}
		}

		closedir(dir_p);
	}
#endif
}

static void CompleteDemo_BuildSubdirList(filelist_item_t **list)
{
	searchpath_t *search;

	for (search = com_searchpaths; search; search = search->next)
	{
		char demos_path[MAX_OSPATH];

		if (search->pack)
			continue;

		q_snprintf(demos_path, sizeof(demos_path), "%s/demos", search->filename);
		CompleteDemo_ScanPhysicalDir(list, demos_path, NULL, 0);
	}
}

static qboolean CompleteFileListDemo (const char* partial, void* param) // woods #iwtabcomplete #demolistsort
{
	filelist_item_t* file, ** list = (filelist_item_t**)param;
	filelist_item_t* subdir_demos = NULL;
	filelist_item_t* completed_demos = NULL;
	char currentDateStr[80];

	// Get current date/time for the last demo aliases
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

	Con_AddToTabList("last", partial, "play last.dem/last.dz or last cached demo", currentDateStr);
	Con_AddToTabList("-l", partial, "play last demo", currentDateStr);

	for (file = *list; file; file = file->next)
	{
		const char *name = CompleteDemo_StripDemosPrefix(file->name);
		if (!CompleteDemo_ListContains(completed_demos, name))
		{
			Con_AddToTabList(name, partial, NULL, file->data);
			FileList_Add(name, NULL, &completed_demos);
		}
	}

	CompleteDemo_BuildSubdirList(&subdir_demos);
	for (file = subdir_demos; file; file = file->next)
	{
		if (!CompleteDemo_ListContains(completed_demos, file->name))
		{
			Con_AddToTabList(file->name, partial, NULL, file->data);
			FileList_Add(file->name, NULL, &completed_demos);
		}
	}
	CompleteDemo_ClearList(&subdir_demos);
	CompleteDemo_ClearList(&completed_demos);
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
	static const char* targets[] = {
		"start", "end", "middle",
		"quad", "pent", "ring", "eyes", "suit", "mega",
		"rl", "gl", "lg", "sng", "ng", "ssg", "sg"
	};
	size_t i;

	for (i = 0; i < Q_COUNTOF(targets); i++)
		Con_AddToTabList(targets[i], partial, NULL, NULL);

	if (has_last_viewpos)
		Con_AddToTabList("last", partial, NULL, NULL); // #demolistsort add arg

	return true;
}

static qboolean CompleteColor(const char* partial, void* unused) // woods #iwtabcomplete
{
	static const struct
	{
		const char* value;
		const char* type;
	} colors[] =
	{
		{ "0", "white" },
		{ "1", "brown" },
		{ "2", "light blue" },
		{ "3", "green" },
		{ "4", "red" },
		{ "5", "orange" },
		{ "6", "gold" },
		{ "7", "peach" },
		{ "8", "purple" },
		{ "9", "magenta" },
		{ "10", "tan" },
		{ "11", "green" },
		{ "12", "yellow" },
		{ "13", "blue" },
		{ "x", "random" },
		{ "y", "random" },
		{ "n", "random" },
		{ "0x66ff00", "bright green" },
		{ "0xff00cd", "bright magenta" },
		{ "0xffff00", "bright yellow" }
	};
	size_t i;

	(void)unused;

	if (Cmd_Argc() != 2 && Cmd_Argc() != 3)
		return false;

	for (i = 0; i < sizeof(colors) / sizeof(colors[0]); i++)
		Con_AddToTabList(colors[i].value, partial, colors[i].type, NULL);

	return true;
}

static qboolean CompleteModelName(const char* partial, void* unused) // woods #iwtabcomplete
{
	(void)unused;

	if (Cmd_Argc() != 2)
		return false;

	Con_AddModelNamesToTabList(partial, false);
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

#define QW_MAPLIST_PATH				"misc/qw_maps.txt"
#define QW_MAPLIST_CACHE_DIR		"backups"
#define QW_MAPLIST_CACHE_FILE		"qw_maps.txt"
#define QW_MAPLIST_CACHE_TMP		"qw_maps.tmp"
#define QW_MAPLIST_REMOTE_URL		"https://maps.quakeworld.nu/all/"
#define QW_MAPLIST_MIN_CHARS		2
#define QW_MAPLIST_COMPLETION_CAP	100
#define QW_MAPLIST_REFRESH_SECONDS	(180 * 24 * 60 * 60)
#define QW_MAPLIST_RETRY_SECONDS	(60 * 60)
#define QW_MAPLIST_REMOTE_MAX_BYTES	(16 * 1024 * 1024)

typedef enum
{
	QW_MAPLIST_REFRESH_NONE,
	QW_MAPLIST_REFRESH_SUCCESS,
	QW_MAPLIST_REFRESH_FAILED
} qw_maplist_refresh_result_t;

static qw_maplist_state_t qw_maplist_state = QW_MAPLIST_UNLOADED;
static char *qw_maplist_buffer;
static const char **qw_maplist_names;
static int qw_maplist_count;
static char qw_maplist_path[MAX_OSPATH];
static qboolean qw_maplist_warned;
static qboolean qw_maplist_cache_warned;
static qboolean qw_maplist_temp_swept;
static SDL_atomic_t qw_maplist_refresh_running;
static SDL_atomic_t qw_maplist_refresh_ready;
static SDL_atomic_t qw_maplist_refresh_checked;
static SDL_atomic_t qw_maplist_refresh_last_result;
static time_t qw_maplist_last_refresh_attempt;

typedef struct
{
	char *data;
	size_t length;
	size_t capacity;
	qboolean failed;
} qw_maplist_download_t;

static qboolean QWMapList_StartRefresh(qboolean force);

static void QWMapList_FreeCache(void)
{
	free(qw_maplist_buffer);
	free(qw_maplist_names);
	qw_maplist_buffer = NULL;
	qw_maplist_names = NULL;
	qw_maplist_count = 0;
	qw_maplist_path[0] = '\0';
}

static void QWMapList_Fail(const char *message)
{
	QWMapList_FreeCache();
	qw_maplist_state = QW_MAPLIST_FAILED;

	if (!qw_maplist_warned)
	{
		Con_Warning("qwmaplist: %s\n", message);
		qw_maplist_warned = true;
	}
}

static qboolean QWMapList_NameIsValid(const char *name)
{
	size_t len;
	const unsigned char *p;

	if (!name || !*name)
		return false;

	len = strlen(name);
	if (len < 5 || len >= MAX_QPATH - 5)
		return false;

	if (q_strcasecmp(name + len - 4, ".bsp"))
		return false;

	for (p = (const unsigned char *)name; *p; ++p)
	{
		if (*p < 32 || *p > 126)
			return false;

		switch (*p)
		{
		case '/':
		case '\\':
		case ':':
		case '"':
		case '*':
		case '?':
			return false;
		default:
			break;
		}
	}

	return true;
}

static qboolean QWMapList_PackContains(const pack_t *pack)
{
	int i;

	if (!pack)
		return false;

	for (i = 0; i < pack->numfiles; i++)
		if (!q_strcasecmp(pack->files[i].name, QW_MAPLIST_PATH))
			return true;

	return false;
}

static void QWMapList_SetSourcePath(unsigned int path_id)
{
	searchpath_t *search;

	qw_maplist_path[0] = '\0';

	for (search = com_searchpaths; search; search = search->next)
	{
		if (path_id && search->path_id != path_id)
			continue;

		if (search->pack)
		{
			if (QWMapList_PackContains(search->pack))
			{
				q_snprintf(qw_maplist_path, sizeof(qw_maplist_path), "%s:%s",
					search->purename[0] ? search->purename : search->filename,
					QW_MAPLIST_PATH);
				return;
			}
		}
		else
		{
			char candidate[MAX_OSPATH];
			FILE *f;

			q_snprintf(candidate, sizeof(candidate), "%s/%s", search->filename, QW_MAPLIST_PATH);
			f = fopen(candidate, "rb");
			if (f)
			{
				fclose(f);
				q_strlcpy(qw_maplist_path, candidate, sizeof(qw_maplist_path));
				return;
			}
		}
	}

	q_strlcpy(qw_maplist_path, QW_MAPLIST_PATH, sizeof(qw_maplist_path));
}

static qboolean QWMapList_Parse(char *buffer, const char ***names_out, int *count_out,
	char *error, size_t error_size)
{
	const char **names = NULL;
	int capacity = 0;
	int count = 0;
	int line_number = 0;
	char *line = buffer;

	while (line && *line)
	{
		char *next = strchr(line, '\n');
		char *end;

		++line_number;
		if (next)
			*next++ = '\0';

		if (line_number == 1 &&
			(unsigned char)line[0] == 0xef &&
			(unsigned char)line[1] == 0xbb &&
			(unsigned char)line[2] == 0xbf)
		{
			line += 3;
		}

		while (*line == ' ' || *line == '\t')
			++line;

		end = line + strlen(line);
		while (end > line && (end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
			*--end = '\0';

		if (!*line || *line == '#')
		{
			line = next;
			continue;
		}

		if (!QWMapList_NameIsValid(line))
		{
			q_snprintf(error, error_size, "line %d has an invalid map name", line_number);
			free(names);
			return false;
		}

		if (count > 0 && q_strcasecmp(names[count - 1], line) >= 0)
		{
			q_snprintf(error, error_size, "line %d is not sorted uniquely", line_number);
			free(names);
			return false;
		}

		if (count == capacity)
		{
			int new_capacity = capacity ? capacity * 2 : 1024;
			const char **new_names = (const char **)realloc(names, sizeof(*names) * new_capacity);
			if (!new_names)
			{
				q_snprintf(error, error_size, "out of memory");
				free(names);
				return false;
			}
			names = new_names;
			capacity = new_capacity;
		}

		names[count++] = line;
		line = next;
	}

	*names_out = names;
	*count_out = count;
	return true;
}

static void QWMapList_CachePath(char *path, size_t path_size, const char *filename)
{
	q_snprintf(path, path_size, "%s/id1/%s/%s", com_basedir, QW_MAPLIST_CACHE_DIR, filename);
}

static void QWMapList_SweepTempCache(void)
{
	char path[MAX_OSPATH];

	if (qw_maplist_temp_swept || SDL_AtomicGet(&qw_maplist_refresh_running))
		return;

	qw_maplist_temp_swept = true;
	QWMapList_CachePath(path, sizeof(path), QW_MAPLIST_CACHE_TMP);
	remove(path);
}

static qboolean QWMapList_LoadCache(void)
{
	char path[MAX_OSPATH];
	char error[128];
	const char **names = NULL;
	int count = 0;
	char *buffer;

	QWMapList_CachePath(path, sizeof(path), QW_MAPLIST_CACHE_FILE);
	buffer = (char *)COM_LoadMallocFile_TextMode_OSPath(path, NULL);
	if (!buffer)
		return false;

	error[0] = '\0';
	if (!QWMapList_Parse(buffer, &names, &count, error, sizeof(error)) || count <= 0)
	{
		free(names);
		free(buffer);
		if (!qw_maplist_cache_warned)
		{
			Con_Warning("qwmaplist cache ignored: %s\n",
				error[0] ? error : "malformed " QW_MAPLIST_CACHE_FILE);
			qw_maplist_cache_warned = true;
		}
		QWMapList_StartRefresh(true);
		return false;
	}

	QWMapList_FreeCache();
	qw_maplist_buffer = buffer;
	qw_maplist_names = names;
	qw_maplist_count = count;
	q_strlcpy(qw_maplist_path, path, sizeof(qw_maplist_path));
	qw_maplist_state = QW_MAPLIST_LOADED;
	return true;
}

static qboolean QWMapList_LoadPackaged(void)
{
	unsigned int path_id = 0;
	char error[128];
	const char **names = NULL;
	int count = 0;
	char *buffer;

	buffer = (char *)COM_LoadMallocFile(QW_MAPLIST_PATH, &path_id);
	if (!buffer)
	{
		QWMapList_Fail("unable to load " QW_MAPLIST_PATH);
		return false;
	}

	error[0] = '\0';
	if (!QWMapList_Parse(buffer, &names, &count, error, sizeof(error)) || count <= 0)
	{
		free(names);
		free(buffer);
		QWMapList_Fail(error[0] ? error : "malformed " QW_MAPLIST_PATH);
		return false;
	}

	QWMapList_FreeCache();
	qw_maplist_buffer = buffer;
	qw_maplist_names = names;
	qw_maplist_count = count;
	QWMapList_SetSourcePath(path_id);
	qw_maplist_state = QW_MAPLIST_LOADED;
	return true;
}

static qboolean QWMapList_CacheIsStale(void)
{
	char path[MAX_OSPATH];
	struct stat st;
	time_t now;

	QWMapList_CachePath(path, sizeof(path), QW_MAPLIST_CACHE_FILE);
	if (stat(path, &st) != 0)
		return true;

	now = time(NULL);
	if (now == (time_t)-1 || st.st_mtime > now)
		return false;

	return now - st.st_mtime >= QW_MAPLIST_REFRESH_SECONDS;
}

static qboolean QWMapList_DownloadAppend(qw_maplist_download_t *download,
	const char *data, size_t length)
{
	size_t needed;
	char *new_data;

	if (download->failed)
		return false;

	if (!length)
		return true;

	if (length > QW_MAPLIST_REMOTE_MAX_BYTES ||
		download->length > QW_MAPLIST_REMOTE_MAX_BYTES - length)
	{
		download->failed = true;
		return false;
	}

	needed = download->length + length + 1;
	if (needed > download->capacity)
	{
		size_t new_capacity = download->capacity ? download->capacity * 2 : 65536;
		while (new_capacity < needed)
			new_capacity *= 2;

		if (new_capacity > QW_MAPLIST_REMOTE_MAX_BYTES + 1)
			new_capacity = QW_MAPLIST_REMOTE_MAX_BYTES + 1;

		new_data = (char *)realloc(download->data, new_capacity);
		if (!new_data)
		{
			download->failed = true;
			return false;
		}

		download->data = new_data;
		download->capacity = new_capacity;
	}

	memcpy(download->data + download->length, data, length);
	download->length += length;
	download->data[download->length] = '\0';
	return true;
}

static size_t QWMapList_CurlWrite(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	qw_maplist_download_t *download = (qw_maplist_download_t *)userdata;
	size_t length = size * nmemb;

	if (size && length / size != nmemb)
		return 0;

	return QWMapList_DownloadAppend(download, (const char *)ptr, length) ? length : 0;
}

static qboolean QWMapList_AddRemoteName(char ***names, int *count, int *capacity, const char *name)
{
	char **new_names;
	char *copy;

	if (*count == *capacity)
	{
		int new_capacity = *capacity ? *capacity * 2 : 1024;

		new_names = (char **)realloc(*names, sizeof(*new_names) * new_capacity);
		if (!new_names)
			return false;

		*names = new_names;
		*capacity = new_capacity;
	}

	copy = (char *)malloc(strlen(name) + 1);
	if (!copy)
		return false;

	strcpy(copy, name);
	(*names)[(*count)++] = copy;
	return true;
}

static void QWMapList_FreeRemoteNames(char **names, int count)
{
	int i;

	for (i = 0; i < count; ++i)
		free(names[i]);
	free(names);
}

static int QWMapList_RemoteNameCompare(const void *a, const void *b)
{
	const char * const *name_a = (const char * const *)a;
	const char * const *name_b = (const char * const *)b;

	return q_strcasecmp(*name_a, *name_b);
}

static qboolean QWMapList_HrefToName(const char *href, size_t href_len, char *name, size_t name_size)
{
	char scratch[MAX_OSPATH];
	const char *base;
	char *cut;

	if (!href_len || href_len >= sizeof(scratch))
		return false;

	memcpy(scratch, href, href_len);
	scratch[href_len] = '\0';

	cut = strpbrk(scratch, "?#");
	if (cut)
		*cut = '\0';

	if (strchr(scratch, '/') || strchr(scratch, '\\'))
		return false;

	base = scratch;
	if (!QWMapList_NameIsValid(base))
		return false;

	q_strlcpy(name, base, name_size);
	return true;
}

static qboolean QWMapList_ExtractRemoteNames(const char *html, char ***names_out, int *count_out)
{
	char **names = NULL;
	int capacity = 0;
	int count = 0;
	const char *p = html;

	while ((p = q_strcasestr(p, "href=")) != NULL)
	{
		char quote;
		const char *start;
		const char *end;
		char name[MAX_QPATH];

		p += 5;
		while (*p == ' ' || *p == '\t')
			++p;

		quote = (*p == '"' || *p == '\'') ? *p++ : '\0';
		start = p;
		if (quote)
		{
			end = strchr(start, quote);
			if (!end)
				break;
			p = end + 1;
		}
		else
		{
			while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '>')
				++p;
			end = p;
		}

		if (!QWMapList_HrefToName(start, (size_t)(end - start), name, sizeof(name)))
			continue;

		if (!QWMapList_AddRemoteName(&names, &count, &capacity, name))
		{
			QWMapList_FreeRemoteNames(names, count);
			return false;
		}
	}

	if (count <= 0)
	{
		free(names);
		return false;
	}

	qsort(names, count, sizeof(*names), QWMapList_RemoteNameCompare);
	*names_out = names;
	*count_out = count;
	return true;
}

static qboolean QWMapList_OutputAppend(char **buffer, size_t *length, size_t *capacity,
	const char *data, size_t data_len)
{
	char *new_buffer;
	size_t needed;

	if (data_len > (size_t)-1 - *length - 1)
		return false;

	needed = *length + data_len + 1;
	if (needed > *capacity)
	{
		size_t new_capacity = *capacity ? *capacity * 2 : 65536;

		while (new_capacity < needed)
			new_capacity *= 2;

		new_buffer = (char *)realloc(*buffer, new_capacity);
		if (!new_buffer)
			return false;

		*buffer = new_buffer;
		*capacity = new_capacity;
	}

	memcpy(*buffer + *length, data, data_len);
	*length += data_len;
	(*buffer)[*length] = '\0';
	return true;
}

static qboolean QWMapList_BuildRemoteText(char **names, int count, char **text_out, size_t *text_len_out)
{
	char *text = NULL;
	size_t length = 0;
	size_t capacity = 0;
	char header[256];
	int i;
	int unique_count = 0;
	char *validate_copy;
	const char **validate_names = NULL;
	int validate_count = 0;
	char error[128];

	for (i = 0; i < count; ++i)
		if (i == 0 || q_strcasecmp(names[i - 1], names[i]))
			++unique_count;

	if (unique_count <= 0)
		goto fail;

	q_snprintf(header, sizeof(header),
		"# qssm-qw-maplist-v1\n"
		"# source=%s\n"
		"# refreshed=%lld\n"
		"# count=%d\n",
		QW_MAPLIST_REMOTE_URL,
		(long long)time(NULL),
		unique_count);

	if (!QWMapList_OutputAppend(&text, &length, &capacity, header, strlen(header)))
		goto fail;

	unique_count = 0;
	for (i = 0; i < count; ++i)
	{
		if (i > 0 && !q_strcasecmp(names[i - 1], names[i]))
			continue;

		if (!QWMapList_OutputAppend(&text, &length, &capacity, names[i], strlen(names[i])) ||
			!QWMapList_OutputAppend(&text, &length, &capacity, "\n", 1))
		{
			goto fail;
		}

		++unique_count;
	}

	validate_copy = (char *)malloc(length + 1);
	if (!validate_copy)
		goto fail;

	memcpy(validate_copy, text, length + 1);
	error[0] = '\0';
	if (!QWMapList_Parse(validate_copy, &validate_names, &validate_count, error, sizeof(error)) ||
		validate_count != unique_count)
	{
		free(validate_names);
		free(validate_copy);
		goto fail;
	}

	free(validate_names);
	free(validate_copy);
	*text_out = text;
	*text_len_out = length;
	return true;

fail:
	free(text);
	return false;
}

static qboolean QWMapList_WriteCacheAtomic(const char *text, size_t text_len)
{
	char final_path[MAX_OSPATH];
	char temp_path[MAX_OSPATH];
	FILE *file;

	QWMapList_CachePath(final_path, sizeof(final_path), QW_MAPLIST_CACHE_FILE);
	QWMapList_CachePath(temp_path, sizeof(temp_path), QW_MAPLIST_CACHE_TMP);
	COM_CreatePath(temp_path);

	file = fopen(temp_path, "wb");
	if (!file)
		return false;

	if (fwrite(text, 1, text_len, file) != text_len || ferror(file))
	{
		fclose(file);
		remove(temp_path);
		return false;
	}

	if (fclose(file) != 0)
	{
		remove(temp_path);
		return false;
	}

	if (rename(temp_path, final_path) != 0)
	{
		remove(final_path);
		if (rename(temp_path, final_path) != 0)
		{
			remove(temp_path);
			return false;
		}
	}

	return true;
}

static int QWMapList_RefreshThread(void *unused)
{
	qw_maplist_download_t download;
	CURL *curl;
	CURLcode result;
	long http_code = 0;
	char **names = NULL;
	int count = 0;
	char *text = NULL;
	size_t text_len = 0;
	qboolean success = false;

	(void)unused;
	memset(&download, 0, sizeof(download));

	curl = curl_easy_init();
	if (!curl)
		goto done;

	curl_easy_setopt(curl, CURLOPT_URL, QW_MAPLIST_REMOTE_URL);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, QWMapList_CurlWrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &download);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "QSS-M");
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

	result = curl_easy_perform(curl);
	if (result != CURLE_OK || download.failed)
		goto cleanup_curl;

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	if (http_code < 200 || http_code >= 300 || !download.data)
		goto cleanup_curl;

	if (!QWMapList_ExtractRemoteNames(download.data, &names, &count))
		goto cleanup_curl;

	if (!QWMapList_BuildRemoteText(names, count, &text, &text_len))
		goto cleanup_curl;

	if (!QWMapList_WriteCacheAtomic(text, text_len))
		goto cleanup_curl;

	success = true;

cleanup_curl:
	curl_easy_cleanup(curl);

done:
	if (success)
	{
		SDL_AtomicSet(&qw_maplist_refresh_last_result, QW_MAPLIST_REFRESH_SUCCESS);
		SDL_AtomicSet(&qw_maplist_refresh_ready, 1);
	}
	else
	{
		SDL_AtomicSet(&qw_maplist_refresh_checked, 0);
		SDL_AtomicSet(&qw_maplist_refresh_last_result, QW_MAPLIST_REFRESH_FAILED);
	}

	free(text);
	if (names)
		QWMapList_FreeRemoteNames(names, count);
	free(download.data);
	SDL_AtomicSet(&qw_maplist_refresh_running, 0);
	return 0;
}

static qboolean QWMapList_StartRefresh(qboolean force)
{
	SDL_Thread *thread;
	time_t now;

	now = time(NULL);
	if (!force && now != (time_t)-1 && qw_maplist_last_refresh_attempt &&
		SDL_AtomicGet(&qw_maplist_refresh_last_result) == QW_MAPLIST_REFRESH_FAILED &&
		now - qw_maplist_last_refresh_attempt < QW_MAPLIST_RETRY_SECONDS)
	{
		return false;
	}

	if (!force && !QWMapList_CacheIsStale())
	{
		SDL_AtomicSet(&qw_maplist_refresh_checked, 1);
		return false;
	}

	if (!SDL_AtomicCAS(&qw_maplist_refresh_running, 0, 1))
	{
		SDL_AtomicSet(&qw_maplist_refresh_checked, 1);
		return false;
	}

	SDL_AtomicSet(&qw_maplist_refresh_last_result, QW_MAPLIST_REFRESH_NONE);
	thread = SDL_CreateThread(QWMapList_RefreshThread, "qwmaplist", NULL);
	if (!thread)
	{
		SDL_AtomicSet(&qw_maplist_refresh_running, 0);
		SDL_AtomicSet(&qw_maplist_refresh_checked, 0);
		SDL_AtomicSet(&qw_maplist_refresh_last_result, QW_MAPLIST_REFRESH_FAILED);
		Con_DPrintf("qwmaplist: failed to create refresh thread: %s\n", SDL_GetError());
		return false;
	}

	if (now != (time_t)-1)
		qw_maplist_last_refresh_attempt = now;
	SDL_AtomicSet(&qw_maplist_refresh_checked, 1);
	SDL_DetachThread(thread);
	return true;
}

static void QWMapList_ConsumeRefresh(void)
{
	if (!SDL_AtomicCAS(&qw_maplist_refresh_ready, 1, 0))
		return;

	QWMapList_FreeCache();
	qw_maplist_state = QW_MAPLIST_UNLOADED;
	qw_maplist_warned = false;
	qw_maplist_cache_warned = false;
	SDL_AtomicSet(&qw_maplist_refresh_checked, 0);
}

qboolean QWMapList_LoadOnce(void)
{
	QWMapList_SweepTempCache();
	QWMapList_ConsumeRefresh();
	if (!SDL_AtomicGet(&qw_maplist_refresh_checked))
		QWMapList_StartRefresh(false);

	if (qw_maplist_state == QW_MAPLIST_LOADED)
		return true;
	if (qw_maplist_state == QW_MAPLIST_FAILED)
		return false;

	if (QWMapList_LoadCache())
		return true;

	return QWMapList_LoadPackaged();
}

void QWMapList_Reload(void)
{
	QWMapList_FreeCache();
	qw_maplist_state = QW_MAPLIST_UNLOADED;
	qw_maplist_warned = false;
	qw_maplist_cache_warned = false;
	SDL_AtomicSet(&qw_maplist_refresh_checked, 0);
	QWMapList_StartRefresh(true);
	CL_QWMapListDownloadsRetry();
}

qw_maplist_state_t QWMapList_State(void)
{
	return qw_maplist_state;
}

qboolean QWMapList_IsRefreshing(void)
{
	return SDL_AtomicGet(&qw_maplist_refresh_running) != 0;
}

const char *QWMapList_StateName(void)
{
	switch (qw_maplist_state)
	{
	case QW_MAPLIST_UNLOADED:
		return "unloaded";
	case QW_MAPLIST_LOADED:
		return "loaded";
	case QW_MAPLIST_FAILED:
		return "failed";
	default:
		return "unknown";
	}
}

const char *QWMapList_Path(void)
{
	return qw_maplist_path[0] ? qw_maplist_path : "";
}

const char *QWMapList_NameAt(int index)
{
	if (qw_maplist_state != QW_MAPLIST_LOADED || index < 0 || index >= qw_maplist_count)
		return NULL;

	return qw_maplist_names[index];
}

int QWMapList_Count(void)
{
	return qw_maplist_count;
}

int QWMapList_MinChars(void)
{
	return QW_MAPLIST_MIN_CHARS;
}

int QWMapList_CompletionCap(void)
{
	return QW_MAPLIST_COMPLETION_CAP;
}

static int QWMapList_LowerBoundPrefix(const char *partial, size_t partial_len)
{
	int low = 0;
	int high = qw_maplist_count;

	while (low < high)
	{
		int mid = low + (high - low) / 2;

		if (q_strncasecmp(qw_maplist_names[mid], partial, partial_len) < 0)
			low = mid + 1;
		else
			high = mid;
	}

	return low;
}

static const char *QWMapList_RefreshResultName(void)
{
	switch (SDL_AtomicGet(&qw_maplist_refresh_last_result))
	{
	case QW_MAPLIST_REFRESH_SUCCESS:
		return "success";
	case QW_MAPLIST_REFRESH_FAILED:
		return "failed";
	default:
		return "none";
	}
}

static void QWMapList_FormatTime(time_t timestamp, char *buffer, size_t buffer_size)
{
	struct tm *local;

	if (!buffer_size)
		return;

	if (!timestamp || timestamp == (time_t)-1)
	{
		q_strlcpy(buffer, "(never)", buffer_size);
		return;
	}

	local = localtime(&timestamp);
	if (!local || !strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", local))
		q_snprintf(buffer, buffer_size, "%lld", (long long)timestamp);
}

static void QWMapList_PrintInfo(void)
{
	char cache_path[MAX_OSPATH];
	char cache_mtime[64];
	char last_attempt[64];
	struct stat st;

	QWMapList_CachePath(cache_path, sizeof(cache_path), QW_MAPLIST_CACHE_FILE);
	if (stat(cache_path, &st) == 0)
		QWMapList_FormatTime(st.st_mtime, cache_mtime, sizeof(cache_mtime));
	else
		q_strlcpy(cache_mtime, "(missing)", sizeof(cache_mtime));
	QWMapList_FormatTime(qw_maplist_last_refresh_attempt, last_attempt, sizeof(last_attempt));

	Con_Printf("qwmaplist state: %s\n", QWMapList_StateName());
	Con_Printf("qwmaplist count: %d\n", qw_maplist_count);
	Con_Printf("qwmaplist path: %s\n", qw_maplist_path[0] ? qw_maplist_path : "(none)");
	Con_Printf("qwmaplist cache: %s\n", cache_path);
	Con_Printf("qwmaplist cache mtime: %s\n", cache_mtime);
	Con_Printf("qwmaplist remote: %s\n", QW_MAPLIST_REMOTE_URL);
	Con_Printf("qwmaplist refresh: %s\n",
		SDL_AtomicGet(&qw_maplist_refresh_running) ? "running" :
		(SDL_AtomicGet(&qw_maplist_refresh_ready) ? "ready" : "idle"));
	Con_Printf("qwmaplist last refresh attempt: %s\n", last_attempt);
	Con_Printf("qwmaplist last refresh result: %s\n", QWMapList_RefreshResultName());
	Con_Printf("qwmaplist downloads: %s\n", CL_QWMapListDownloadsAvailable() ? "available" : "unavailable");
	Con_Printf("qwmaplist minchars: %d\n", QW_MAPLIST_MIN_CHARS);
	Con_Printf("qwmaplist completion cap: %d\n", QW_MAPLIST_COMPLETION_CAP);
}

static void QWMapList_f(void)
{
	const char *subcommand;

	if (Cmd_Argc() <= 1)
	{
		QWMapList_PrintInfo();
		return;
	}

	subcommand = Cmd_Argv(1);
	if (!q_strcasecmp(subcommand, "info"))
	{
		QWMapList_PrintInfo();
		return;
	}

	if (!q_strcasecmp(subcommand, "reload"))
	{
		QWMapList_Reload();
		Con_Printf("qwmaplist cache reset\n");
		return;
	}

	Con_Printf("usage: qwmaplist [info|reload]\n");
}

static qboolean CompleteQWMapList(const char* partial, void* unused)
{
	static const char* const subcommands[] = { "info", "reload" };
	size_t partial_len;
	size_t i;

	(void)unused;
	if (Cmd_Argc() != 2)
		return false;

	partial_len = strlen(partial);
	for (i = 0; i < sizeof(subcommands) / sizeof(subcommands[0]); i++)
		if (!q_strncasecmp(subcommands[i], partial, partial_len))
			Con_AddToTabList(subcommands[i], partial, NULL, NULL);

	return true;
}

static qboolean CompleteDownload(const char* partial, void* unused) // woods
{
	size_t partial_len;
	int first, i, added, omitted;

	if (Cmd_Argc() != 2)
		return false;

	Con_AddToTabList("ctf", partial, NULL, NULL); // #demolistsort add arg
	Con_AddToTabList("ra", partial, NULL, NULL);  // #demolistsort add arg

	if (!CL_QWMapListDownloadsAvailable())
		return true;

	partial_len = strlen(partial);
	if (partial_len < QW_MAPLIST_MIN_CHARS)
		return true;

	if (!QWMapList_LoadOnce())
		return true;

	first = QWMapList_LowerBoundPrefix(partial, partial_len);
	added = 0;
	omitted = 0;
	for (i = first; i < qw_maplist_count; ++i)
	{
		const char *name = qw_maplist_names[i];

		if (q_strncasecmp(name, partial, partial_len))
			break;

		if (added < QW_MAPLIST_COMPLETION_CAP)
		{
			Con_AddToTabList(name, partial, NULL, NULL); // #demolistsort add arg
			++added;
		}
		else
		{
			++omitted;
		}
	}

	tablist_limited_count += added;
	tablist_omitted_count += omitted;

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
	char unfun_partial[32];
	const char *match_partial;
	int i;

	if (Cmd_Argc() != 2)
		return false;

	// Only suggest names if we're connected to a server
	if (cls.state != ca_connected && cl.maxclients < 1)
		return false;

	match_partial = Con_DequakePartial (partial, unfun_partial, sizeof(unfun_partial));

	// Loop through all possible players
	for (i = 0; i < cl.maxclients; i++)
	{
		scoreboard_t* s = &cl.scores[i];
		const char* colored_name = s->name;
		char trimmed_name[16];
		int true_end = 0;
		int last_char = 0;
		int space_count = 0;
		int max_allowed_spaces = 1;
		int trim_len;
		int j;

		// Skip empty slots
		if (!colored_name[0])
			continue;

		// Scan the entire name first to find the true last character
		for (j = 0; colored_name[j] && j < 31; j++)
		{
			if (colored_name[j] != ' ')
				true_end = j;
		}

		// Now scan up to true_end to find any large gaps
		for (j = 0; j <= true_end && j < 31; j++)
		{
			if (colored_name[j] == ' ')
			{
				space_count++;
				if (space_count > max_allowed_spaces)
					break;
			}
			else
			{
				last_char = j;
				space_count = 0;
			}
		}

		// Trimmed copy of the colored name (Con_AddToTabListInternal copies
		// both name and match_name, so a stack buffer is fine)
		trim_len = q_min(last_char + 1, 15);
		memcpy(trimmed_name, colored_name, trim_len);
		trimmed_name[trim_len] = '\0';

		Con_AddNameToTabList (trimmed_name, partial, match_partial);
	}

	return true;
}

static qboolean CompleteCmd(const char* partial, void* unused)
{
	if (Cmd_Argc() == 2)
		return CompleteGeneralList(partial, unused);

	if (Cmd_Argc() == 3 &&
		(!q_strcasecmp(Cmd_Argv(1), "ignore") || !q_strcasecmp(Cmd_Argv(1), "unignore")))
		return CompleteClients(partial, NULL);

	return false;
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

static qboolean CompleteSkywind(const char* partial, void* unused)
{
	static const char* const distance_options[] = { "0", "0.25", "0.5", "0.75", "1", "1.5", "2", "-1", "-2" };
	static const char* const yaw_options[] = { "0", "45", "90", "135", "180", "225", "270", "315" };
	static const char* const period_options[] = { "10", "20", "30", "45", "60" };
	static const char* const pitch_options[] = { "-45", "-30", "-15", "0", "15", "30", "45" };

	const char* const* options = NULL;
	size_t option_count = 0;
	int argc = Cmd_Argc();

	switch (argc)
	{
	case 2:
		options = distance_options;
		option_count = sizeof(distance_options) / sizeof(distance_options[0]);
		break;
	case 3:
		options = yaw_options;
		option_count = sizeof(yaw_options) / sizeof(yaw_options[0]);
		break;
	case 4:
		options = period_options;
		option_count = sizeof(period_options) / sizeof(period_options[0]);
		break;
	case 5:
		options = pitch_options;
		option_count = sizeof(pitch_options) / sizeof(pitch_options[0]);
		break;
	default:
		return false;
	}

	for (size_t i = 0; i < option_count; i++)
		Con_AddToTabList(options[i], partial, NULL, NULL);

	return option_count > 0;
}

static qboolean CompleteJumpDemo(const char* partial, void* unused)
{
	static const char* const options[] =
	{
		"mark",
		"500",
		"50%",
		"1:30",
		"90s",
		"+1000",
		"-1000",
		"+5%",
		"-5%",
		"+10s",
		"-10s",
		"+1:30",
		"-0:30"
	};
	size_t i;

	if (Cmd_Argc() != 2)
		return false;

	for (i = 0; i < sizeof(options) / sizeof(options[0]); i++)
		Con_AddToTabList(options[i], partial, NULL, NULL);

	return true;
}

static qboolean CompleteMarkDemo(const char* partial, void* unused)
{
	static const char* const examples[] =
	{
		"clear",
		"start",
		"aerowalk",
		"ztndm3",
		"dm6"
	};
	char path[MAX_OSPATH];
	FILE *file;
	long file_size;
	char *buffer = NULL;
	json_t *json = NULL;
	jsonentry_t *entry;
	char seen_maps[128][MAX_QPATH];
	size_t seen_count = 0;
	size_t i;

	if (Cmd_Argc() != 2)
		return false;

	for (i = 0; i < sizeof(examples) / sizeof(examples[0]); i++)
		Con_AddToTabList(examples[i], partial, NULL, NULL);

	if (cl.mapname[0])
		Con_AddToTabList(cl.mapname, partial, "current map", NULL);

	q_snprintf(path, sizeof(path), "%s/id1/backups/demomarks.json", com_basedir);
	file = fopen(path, "rb");
	if (!file)
		return true;

	fseek(file, 0, SEEK_END);
	file_size = ftell(file);
	rewind(file);

	if (file_size <= 0)
	{
		fclose(file);
		return true;
	}

	buffer = (char *)malloc((size_t)file_size + 1);
	if (!buffer)
	{
		fclose(file);
		return true;
	}

	if (fread(buffer, 1, (size_t)file_size, file) != (size_t)file_size)
	{
		free(buffer);
		fclose(file);
		return true;
	}

	buffer[file_size] = '\0';
	fclose(file);

	json = JSON_Parse(buffer);
	free(buffer);
	if (!json || !json->root || json->root->type != JSON_ARRAY)
	{
		if (json)
			JSON_Free(json);
		return true;
	}

	for (entry = json->root->firstchild; entry; entry = entry->next)
	{
		const char *map;
		qboolean duplicate = false;

		if (entry->type != JSON_OBJECT)
			continue;

		map = JSON_FindString(entry, "map");
		if (!map || !map[0])
			continue;

		for (i = 0; i < seen_count; i++)
		{
			if (!q_strcasecmp(seen_maps[i], map))
			{
				duplicate = true;
				break;
			}
		}

		if (duplicate)
			continue;

		Con_AddToTabList(map, partial, "history map", NULL);
		if (seen_count < sizeof(seen_maps) / sizeof(seen_maps[0]))
		{
			q_strlcpy(seen_maps[seen_count], map, sizeof(seen_maps[seen_count]));
			seen_count++;
		}
	}

	JSON_Free(json);
	return true;
}

extern qboolean CompletePAKList(const char* partial, void* unused); // woods #unpak

qboolean CompleteImageList (const char* partial, void* unused); // woods
qboolean CompleteImageDump (const char* partial, void* unused); // woods
qboolean CompleteSoundList (const char* partial, void* unused); // woods
qboolean CompleteGive (const char* partial, void* unused); // woods #give+
qboolean CompleteMapSize (const char *partial, void *unused);
qboolean CompleteRotateModel (const char* partial, void* unused); // woods #clmrotate

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
	{ "modvote",				CompleteFileList,		&modlist },
	{ "playdemo",				CompleteFileListDemo,	&demolist },
	{ "timedemo",				CompleteFileListDemo,	&demolist },
	{ "jumpdemo",				CompleteJumpDemo,		NULL },
	{ "markdemo",				CompleteMarkDemo,		NULL },
	{ "sky",					CompleteFileList,		&skylist },
	{ "skywind",				CompleteSkywind,		NULL },
	{ "exec",					CompleteFileList,		&execlist },
	{ "connect",				CompleteServerHistory,	NULL },
	{ "test",					CompleteServerHistory,	NULL },
	{ "test2",					CompleteServerHistory,	NULL },
	{ "ping",					CompleteServerHistory,	NULL },
	{ "open",					CompleteFileList,		&folderlist },
	{ "music",					CompleteFileList,		&musiclist },
	{ "printtxt",				CompleteFileList,		&textlist },
	{ "r_showbboxes_filter",	CompleteClassnames,		NULL },
	{ "imagelist",				CompleteImageList,		NULL },
	{ "imagedump",				CompleteImageDump,		NULL },
	{ "bind",					CompleteBindKeys,		NULL },
	{ "bindedit",				CompleteBoundKeys,		NULL },
	{ "unbind",					CompleteUnbindKeys,		NULL },
	{ "viewpos",				CompleteViewpos,		NULL },
	{ "setpos",					CompleteSetpos,			NULL },
	{ "viewmodel",				CompleteModelName,		NULL },
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
	{ "qwmaplist",				CompleteQWMapList,		NULL },
	{ "download",				CompleteDownload,		NULL },
	{ "ls",						CompleteLS,				NULL },
	{ "dir",					CompleteLS,				NULL },
	{ "which",					CompleteLS,				NULL },
	{ "flocate",				CompleteLS,				NULL },
	{ "ip",						CompleteIP,				NULL },
	{ "unpak",					CompletePAKList,		NULL },
	{ "cmd",					CompleteCmd,			NULL },
	{ "goto",					CompleteClients,		NULL },
	{ "identify",				CompleteClients,		NULL },
	{ "tell",					CompleteClients,		NULL },
	{ "color",					CompleteColor,			NULL },
	{ "rotatemodel",			CompleteRotateModel,	NULL },
	{ "ignore",					CompleteClients,		NULL },
	{ "unignore",				CompleteClients,		NULL },
	{ "record",					CompleteRecord,			NULL },
	{ "save",					CompleteSave,			NULL },
	{ "load",					CompleteLoad,			NULL },
	{ "give",					CompleteGive,			NULL },
	{ "mapsize",				CompleteMapSize,		NULL }
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
	tablist_omitted_count = 0;
	tablist_limited_count = 0;

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

	if (cls.state == ca_dedicated)
		q_strlcpy(tinted, t->name, sizeof(tinted));
	else
		Con_TintTabMatch(t, bash_partial, tinted, sizeof(tinted));

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

	if (tablist_omitted_count > 0)
		Con_SafePrintf("   %d shown (cap %d), %d more; type more characters\n",
			tablist_limited_count > 0 ? tablist_limited_count : matches,
			QWMapList_CompletionCap(),
			tablist_omitted_count);

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
	const tab_t* selected = NULL;
	tab_t* t;
	int		mark, i;

	key_tabhint[0] = '\0';
	if (mode == TABCOMPLETE_AUTOHINT)
	{
		key_tabpartial[0] = '\0';
		if (!con_autohint.value)
			return;

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
		{
			Hunk_FreeToLowMark (mark);
			return;
		}

		// print list if length > 1 and action is user-initiated
		if (tablist->next != tablist && mode == TABCOMPLETE_USER)
			Con_PrintTabList (); // woods #consolecols

		if (bash_singlematch)
		{
			selected = tablist;
			match = selected->name;
		}
		else
		{
			// First time, just show maximum matching chars -- S.A.
			match = bash_partial;
		}
	}
	else
	{
		BuildTabList (key_tabpartial);

		if (!tablist)
		{
			Hunk_FreeToLowMark (mark);
			return;
		}

		//find current match -- can't save a pointer because the list will be rebuilt each time
		t = tablist;
		selected = keydown[K_SHIFT] ? t->prev : t;
		do
		{
			if (!q_strcasecmp (t->name, partial))
			{
				selected = keydown[K_SHIFT] ? t->prev : t->next;
				break;
			}
			t = t->next;
		} while (t != tablist);
		match = selected->name;
	}

	if (mode == TABCOMPLETE_AUTOHINT)
	{
		size_t len = strlen(partial);
		if (!(selected && Con_CopyTabHint(selected, partial, key_tabhint, sizeof(key_tabhint))))
		{
			match = q_strcasestr (match, partial);
			if (match && match[len])
				q_strlcpy (key_tabhint, match + len, sizeof (key_tabhint));
		}
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
	extern	qpic_t *pic_ins; //johnfitz -- thin insert cursor

	GL_SetCanvas (CANVAS_CONSOLE); //johnfitz
	v = vid.conheight + con_notifyposition.value; // woods #notifyposition
	if (realtime < scr_volume_display_time)
		v += 24; // keep notify lines below the temporary volume widget

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

	if (key_dest != key_message)
	{
		chat_mouse_area.valid = false;
		chat_mouse_down = false;
		chat_mouse_selecting = false;
	}

	if (key_dest == key_message)
	{
		int chat_x, chat_cursor, chat_ofs, chat_visible, chat_max;
		int sel_start, sel_end;

		clearnotify = 0;

		if (chat_team)
		{
			Draw_String (8, v, "say_team:");
			chat_x = 11;
		}
		else
		{
			Draw_String (8, v, "say:");
			chat_x = 6;
		}

		chat_max = con_linewidth - chat_x - 1;
		if (chat_max < 1)
			chat_max = 1;
		Con_ChatComputeView (chat_max, NULL, NULL, &chat_ofs, NULL);

		chat_mouse_area.valid = true;
		chat_mouse_area.x = chat_x << 3;
		chat_mouse_area.y = v;
		chat_mouse_area.ofs = chat_ofs;
		chat_mouse_area.width_chars = chat_max;

		Con_UpdateChatMouseState ();

		Con_ChatComputeView (chat_max, &text, &chat_cursor, &chat_ofs, &chat_visible);
		chat_mouse_area.ofs = chat_ofs;

		if (Key_GetChatSelection (&sel_start, &sel_end))
		{
			sel_start = CLAMP(chat_ofs, sel_start, chat_ofs + chat_visible);
			sel_end = CLAMP(chat_ofs, sel_end, chat_ofs + chat_visible);
			if (sel_end > sel_start)
				Draw_Fill ((chat_x + sel_start - chat_ofs) << 3, v,
					(sel_end - sel_start) << 3, 8, 170, 0.4f);
		}

		x = chat_x;
		text += chat_ofs;
		for (i = 0; i < chat_visible; i++)
		{
			Draw_Character (x << 3, v, text[i]);
			x++;
		}

		x = chat_x + chat_cursor - chat_ofs;
		if (!((int)((realtime-key_blinktime)*con_cursorspeed) & 1))
			Draw_PicRGBA (x << 3, v, pic_ins, Draw_GetConcharsCursorColor(), 1.0f); // woods #cursorcolor
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

	// johnfitz -- new cursor handling // woods #cursorcolor
	if (!((int)((realtime-key_blinktime)*con_cursorspeed) & 1))
	{
		i = key_linepos - ofs;
		Draw_PicRGBA ((i+1)<<3, vid.conheight - 16, key_insert ? pic_ins : pic_ovr, Draw_GetConcharsCursorColor(), 1.0f);
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

static void Con_DrawTypingStatus(void) // woods #typing...
{
	scoreboard_t* typing_players[2];
	qboolean typing_preview = M_LivePreview_UseTypingStatus();
	int total_typing = 0;

	if (!con_typing.value)
		return;

	if (cls.demoplayback && !typing_preview)
		return;

	if ((cls.state != ca_connected || cls.signon != SIGNONS || cl.maxclients <= 0) && !typing_preview)
		return;

	if (!typing_preview)
	{
		int local_index = cl.realviewentity - 1;

		for (int i = 0; i < cl.maxclients; ++i)
		{
			scoreboard_t* score = &cl.scores[i];

			if (!score->name[0])
				continue;

			if (i == local_index && !developer.value)
				continue;

			char chatbuf[8];
			const char* chat_value = Info_GetKey(score->userinfo, "chat", chatbuf, sizeof(chatbuf));
			int chat_flags = (chat_value && *chat_value) ? atoi(chat_value) : 0;

			if (!(chat_flags & CIF_CHAT))
				continue;

			if (total_typing < 2)
				typing_players[total_typing] = score;

			total_typing++;
		}
	}

	if (total_typing == 0)
	{
		if (!typing_preview)
			return;
		total_typing = 1;
	}

	char message[128];
	char name[16] = {0};
	char name1[16] = {0};
	char name2[16] = {0};

	if (typing_preview)
	{
		Con_GetShortName(cl_name.string[0] ? cl_name.string : "player", name, sizeof(name));
		if (!name[0])
			q_strlcpy(name, "player", sizeof(name));
	}
	else if (total_typing == 1)
	{
		Con_GetShortName(typing_players[0]->name, name, sizeof(name));
	}
	else if (total_typing == 2)
	{
		Con_GetShortName(typing_players[0]->name, name1, sizeof(name1));
		Con_GetShortName(typing_players[1]->name, name2, sizeof(name2));
	}
	else
	{
		q_strlcpy(message, "Several players are typing...", sizeof(message));
	}

	const int base_x = 8;
	const int base_y = vid.conheight - 7;
	const int dots_width = 3 * 8;
	const int gap = 6;

	float console_scale = scr_conscale.value;
	if (console_scale <= 0.0f)
		console_scale = 1.0f;

	float target_scale = console_scale - 1.0f;
	if (target_scale <= 0.0f)
		target_scale = console_scale;

	const float scale_factor = target_scale / console_scale;

	glPushMatrix();
	glTranslatef((float)base_x, (float)base_y, 0.0f);
	glScalef(scale_factor, scale_factor, 1.0f);

	Draw_StringAnimatedDots(0, -3, "...");

	if (total_typing == 1)
	{
		const char* suffix = " is typing...";
		int x = dots_width + gap;

		// Name at full opacity
		Draw_String(x, -1, name);
		x += (int)strlen(name) * 8;

		// Suffix faded
		Draw_StringRGBA(x, -1, suffix, CL_PLColours_Parse("0xffffff"), 0.6f);
	}
	else if (total_typing == 2)
	{
		const char* sep = " & ";
		const char* suffix = " are typing...";
		int x = dots_width + gap;

		Draw_String(x, -1, name1);
		x += (int)strlen(name1) * 8;

		Draw_StringRGBA(x, -1, sep, CL_PLColours_Parse("0xffffff"), 0.6f);
		x += (int)strlen(sep) * 8;

		Draw_String(x, -1, name2);
		x += (int)strlen(name2) * 8;

		Draw_StringRGBA(x, -1, suffix, CL_PLColours_Parse("0xffffff"), 0.6f);
	}
	else
	{
		// No player names, fade the whole message
		Draw_StringRGBA(dots_width + gap, -1, message, CL_PLColours_Parse("0xffffff"), 0.6f);
	}

	glPopMatrix();
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
	qboolean links_enabled;

	if (lines <= 0)
		return;

	con_vislines = lines * vid.conheight / glheight;
	GL_SetCanvas (CANVAS_CONSOLE);
	links_enabled = Con_LinksEnabled();

    Con_UpdateMouseState(); // woods #conselection - update selection/hover each frame while console is up

// draw the background
	Draw_ConsoleBackground ();

// draw the buffer text
	rows = (con_vislines +7)/8;
       y = vid.conheight - rows*CHARSIZE;
	rows -= 2; //for input and version lines
	sb = (con_backscroll) ? 2 : 0;

       for (i = con_current - rows + 1; i <= con_current - sb; i++, y += CHARSIZE)
	{
		j = i - con_backscroll;
		if (j < 0)
			j = 0;
		text = con_text + (j % con_totallines)*con_linewidth;
               Con_DrawSelectionHighlight (CHARSIZE, y, j); // woods #conselection
       }

       y = vid.conheight - (rows+2)*CHARSIZE; // +2 for input and version lines
       for (i = con_current - rows + 1; i <= con_current - sb; i++, y += CHARSIZE)
       {
               conofs_t ofs;
               j = i - con_backscroll;
               if (j < 0)
                       j = 0;
               text = con_text + (j % con_totallines)*con_linewidth;
               ofs.line = j;
		for (x = 0; x < con_linewidth; x++) // woods #conselection
               {
                       conlink_t *drawlink;
                       char c = text[x];
                       ofs.col = x;
                       drawlink = links_enabled ? Con_GetLinkAtOfs(&ofs) : NULL;
                       if (drawlink)
                               c |= 0x80;
                       /* underline hot link */
                       if (drawlink && drawlink == con_hotlink)
                               Draw_Character ((x + 1)<<3, y + 2, '_' | (c & 0x80));
                       Draw_Character ((x + 1)<<3, y, c);
               }
	}

// draw scrollback arrows
	if (con_backscroll)
	{
		y += CHARSIZE; // blank line
		for (x = 0; x < con_linewidth; x += 4)
			Draw_Character ((x + 1)<<3, y, '^');
		y += CHARSIZE;
	}

// draw the input prompt, user text, and cursor
	if (drawinput)
		Con_DrawInput ();

	Con_DrawTypingStatus(); // woods #typing...

//draw version number in bottom right
	for (x = 0; x < (int)strlen(ver); x++)
		Draw_Character ((con_linewidth - strlen(ver) + x + 2)<<3, vid.conheight - CHARSIZE, ver[x] /*+ 128*/); // woods iw

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


void LOG_Init (quakeparms_t *parms) // woods #debuglogsize
{
	time_t	inittime;
	char	session[24];

	log_cap = LOG_ParseCapFromCLI();
	if (log_cap < 0)
		return; /* no -condebug => no logging */

	inittime = time (NULL);
	strftime (session, sizeof(session), "%m/%d/%Y %H:%M:%S", localtime(&inittime));
	q_snprintf (logfilename, sizeof(logfilename), "%s/qconsole.log", parms->basedir);

//	unlink (logfilename);

#ifdef _WIN32
	log_fd = open (logfilename, O_RDWR | O_CREAT | O_APPEND | O_BINARY, 0666);
#else
	log_fd = open (logfilename, O_RDWR | O_CREAT | O_APPEND, 0666);
#endif
	if (log_fd == -1)
	{
		fprintf (stderr, "Error: Unable to create log file %s\n", logfilename);
		return;
	}

	if (!log_mutex) log_mutex = SDL_CreateMutex();
	con_debuglog = true;
	log_roll_pending = false;

	LOG_Lock();
	LOG_UpdateSize_Locked();
	if (log_cap > 0 && log_size > log_cap)
		LOG_RollTail_Locked(startup_banner);
	LOG_Unlock();

	Con_DebugLog (va("\nLOG started on: %s \n\n", session)); // woods add a line
}

void LOG_Close (void)
{
	LOG_Lock();
	if (log_fd == -1) {
		LOG_Unlock();
		return;
	}

	/* Roll if pending before closing */
	if (log_cap > 0 && log_roll_pending) {
		LOG_UpdateSize_Locked();
		if (log_size > log_cap)
			LOG_RollTail_Locked(rollover_banner);
	}

	close (log_fd);
	con_debuglog = false;
	log_fd = -1;
	log_size = 0;
	log_roll_pending = false;
	LOG_Unlock();
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
    const unsigned char *src = (const unsigned char *)raw;
    size_t len = 0;

    if (!outsz) return;
    out[0] = 0;
    if (!raw) return;

    // Strip the console chat colour prefix so Discord doesn't show ".name:".
    while (*src == 1 || *src == 2)
        src++;

    while (*src && len + 1 < sizeof(clean))
        clean[len++] = dequake[*src++];

    while (len > 0 && (clean[len - 1] == '\n' || clean[len - 1] == '\r'))
        len--;

    clean[len] = 0;

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
	if (!job) {
		Con_DPrintf("discord: out of memory creating job\n");
		return;
	}

    q_strlcpy(job->payload, payload, sizeof(job->payload));
    q_strlcpy(job->url, con_notifydiscord.string, sizeof(job->url));

    SDL_Thread *t = SDL_CreateThread(DiscordThread, "discord", job);
	if (t)
		SDL_DetachThread(t);
	else {
		Con_DPrintf("discord: failed to create thread: %s\n", SDL_GetError());
		free(job);
	}
}
