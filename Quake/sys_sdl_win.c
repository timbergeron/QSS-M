/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2005 John Fitzgibbons and others
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

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

#include "quakedef.h"

#include <sys/types.h>
#include <errno.h>
#include <io.h>
#include <direct.h>

#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#if defined(USE_SDL2)
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif
#else
#include "SDL.h"
#endif


qboolean		isDedicated;
qboolean	Win95, Win95old, WinNT, WinVista;
cvar_t		sys_throttle = {"sys_throttle", "0.02", CVAR_ARCHIVE};

static HANDLE		hinput, houtput;
static qboolean		use_vtp = false; // ANSI virtual terminal processing available

static size_t	sys_handles_max;	/* spike -- removed limit, was 32 (johnfitz -- was 10) */
static FILE		**sys_handles;
static int findhandle (void)
{
	size_t i, n;

	for (i = 1; i < sys_handles_max; i++)
	{
		if (!sys_handles[i])
			return i;
	}
	n = sys_handles_max+10;
	sys_handles = realloc(sys_handles, sizeof(*sys_handles)*n);
	if (!sys_handles)
		Sys_Error ("out of handles");
	while (sys_handles_max < n)
		sys_handles[sys_handles_max++] = NULL;
	return i;
}

qofs_t Sys_filelength (FILE *f)
{
	long		pos, end;

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	return end;
}

qofs_t Sys_FileOpenRead (const char *path, int *hndl)
{
	FILE	*f;
	int	i;
	qofs_t retval;

	i = findhandle ();
	f = fopen(path, "rb");

	if (!f)
	{
		*hndl = -1;
		retval = -1;
	}
	else
	{
		sys_handles[i] = f;
		*hndl = i;
		retval = Sys_filelength(f);
	}

	return retval;
}

int Sys_FileOpenWrite (const char *path)
{
	FILE	*f;
	int		i;

	i = findhandle ();
	f = fopen(path, "wb");

	if (!f)
		Sys_Error ("Error opening %s: %s", path, strerror(errno));

	sys_handles[i] = f;
	return i;
}

int Sys_FileOpenStdio (FILE *file)
{
	int		i;
	i = findhandle ();
	sys_handles[i] = file;
	return i;
}

void Sys_FileClose (int handle)
{
	fclose (sys_handles[handle]);
	sys_handles[handle] = NULL;
}

void Sys_FileSeek (int handle, qofs_t position)
{
	fseek (sys_handles[handle], position, SEEK_SET);
}

int Sys_FileRead (int handle, void *dest, int count)
{
	return fread (dest, 1, count, sys_handles[handle]);
}

int Sys_FileWrite (int handle, const void *data, int count)
{
	return fwrite (data, 1, count, sys_handles[handle]);
}

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES	((DWORD)-1)
#endif
int Sys_FileType (const char *path)
{
	DWORD result = GetFileAttributes(path);

	if (result == INVALID_FILE_ATTRIBUTES)
		return FS_ENT_NONE;
	if (result & FILE_ATTRIBUTE_DIRECTORY)
		return FS_ENT_DIRECTORY;

	return FS_ENT_FILE;
}

static qboolean Sys_FileURLCharIsSafe (unsigned char c)
{
	return (c >= 'A' && c <= 'Z') ||
	       (c >= 'a' && c <= 'z') ||
	       (c >= '0' && c <= '9') ||
	       c == '/' || c == ':' || c == '-' || c == '.' || c == '_' || c == '~';
}

static qboolean Sys_BuildFileURL (const char *path, char *url, size_t urlsize)
{
	static const char	hex[] = "0123456789ABCDEF";
	const unsigned char	*src;
	const char		*prefix;
	char			*dst, *end;
	qboolean		unc;

	if (!path)
		return false;

	unc = (path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/');
	prefix = unc ? "file://" : "file:///";
	if (q_strlcpy(url, prefix, urlsize) >= urlsize)
		return false;

	dst = url + strlen(url);
	end = url + urlsize;
	src = (const unsigned char *)path + (unc ? 2 : 0);
	while (*src)
	{
		unsigned char c = *src++;
		if (c == '\\')
			c = '/';
		if (Sys_FileURLCharIsSafe(c))
		{
			if (dst + 1 >= end)
				return false;
			*dst++ = (char)c;
		}
		else
		{
			if (dst + 3 >= end)
				return false;
			*dst++ = '%';
			*dst++ = hex[c >> 4];
			*dst++ = hex[c & 15];
		}
	}
	*dst = '\0';
	return true;
}

qboolean Sys_Explore (const char *path)
{
	char	url[MAX_OSPATH * 3 + 8];
	char	dir[MAX_OSPATH];
	char	*slash, *backslash, *s;
	int	type;

	type = Sys_FileType (path);
	if (type == FS_ENT_NONE)
		return false;

	q_strlcpy (dir, path, sizeof(dir));
	if (!(type & FS_ENT_DIRECTORY))
	{
		slash = Q_strrchr (dir, '/');
		backslash = Q_strrchr (dir, '\\');
		s = (backslash && (!slash || backslash > slash)) ? backslash : slash;
		if (!s)
			return false;
		if (s == dir || (s == dir + 2 && dir[1] == ':'))
			s[1] = '\0';
		else
			*s = '\0';
	}

	if (!Sys_BuildFileURL(dir, url, sizeof(url)))
		return false;
	return SDL_OpenURL (url) == 0;
}

static char	cwd[1024];

static void Sys_GetBasedir (char *argv0, char *dst, size_t dstsize)
{
	char *tmp;
	size_t rc;

	rc = GetCurrentDirectory(dstsize, dst);
	if (rc == 0 || rc > dstsize)
		Sys_Error ("Couldn't determine current directory");

	tmp = dst;
	while (*tmp != 0)
		tmp++;
	while (*tmp == 0 && tmp != dst)
	{
		--tmp;
		if (tmp != dst && (*tmp == '/' || *tmp == '\\'))
			*tmp = 0;
	}
}

typedef enum { dpi_unaware = 0, dpi_system_aware = 1, dpi_monitor_aware = 2 } dpi_awareness;
typedef BOOL (WINAPI *SetProcessDPIAwareFunc)();
typedef HRESULT (WINAPI *SetProcessDPIAwarenessFunc)(dpi_awareness value);

static void Sys_SetDPIAware (void)
{
	HMODULE hUser32, hShcore;
	SetProcessDPIAwarenessFunc setDPIAwareness;
	SetProcessDPIAwareFunc setDPIAware;

	/* Neither SDL 1.2 nor SDL 2.0.3 can handle the OS scaling our window.
	  (e.g. https://bugzilla.libsdl.org/show_bug.cgi?id=2713)
	  Call SetProcessDpiAwareness/SetProcessDPIAware to opt out of scaling.
	*/

	hShcore = LoadLibraryA ("Shcore.dll");
	hUser32 = LoadLibraryA ("user32.dll");
	setDPIAwareness = (SetProcessDPIAwarenessFunc) (hShcore ? GetProcAddress (hShcore, "SetProcessDpiAwareness") : NULL);
	setDPIAware = (SetProcessDPIAwareFunc) (hUser32 ? GetProcAddress (hUser32, "SetProcessDPIAware") : NULL);

	if (setDPIAwareness) /* Windows 8.1+ */
		setDPIAwareness (dpi_monitor_aware);
	else if (setDPIAware) /* Windows Vista-8.0 */
		setDPIAware ();

	if (hShcore)
		FreeLibrary (hShcore);
	if (hUser32)
		FreeLibrary (hUser32);
}

static void Sys_SetTimerResolution(void)
{
	/* Set OS timer resolution to 1ms.
	   Works around buffer underruns with directsound and SDL2, but also
	   will make Sleep()/SDL_Dleay() accurate to 1ms which should help framerate
	   stability.
	*/
	timeBeginPeriod (1);
}

// woods -- https://github.com/andrei-drexler/ironwail/issues/104 disable CAPSLOCK #disablecaps

#if defined(_WIN32) // woods #disablecaps via ironwail
static HHOOK key_hook = NULL;

#define HOOKED_KEYS			\
	HOOK_KEY (CAPSLOCK)		\
	HOOK_KEY (APPLICATION)	\



#define SC_CAPSLOCK			0x3A
#define SC_APPLICATION		0xE05B // windows key

enum
{
#define HOOK_KEY(k)		HK_##k,
	HOOKED_KEYS
#undef HOOK_KEY

	HK_COUNT,
};

static const SDL_Scancode hk_sdl_scancodes[HK_COUNT] =
{
	#define HOOK_KEY(k)		SDL_SCANCODE_##k,
	HOOKED_KEYS
	#undef HOOK_KEY
};

static int GetFilteredKeyIndex(int scancode)
{
	switch (scancode)
	{
#define HOOK_KEY(k)	case SC_##k: return HK_##k;
		HOOKED_KEYS
#undef HOOK_KEY
	default:
		return -1;
	}
}

LRESULT CALLBACK KeyFilter(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode >= 0 && VID_HasMouseOrInputFocus())
	{
		PKBDLLHOOKSTRUCT p = (PKBDLLHOOKSTRUCT)lParam;
		int scancode = p->scanCode | (p->flags & 1 ? 0xE000 : 0);
		int key = GetFilteredKeyIndex(scancode);
		if (key != -1)
		{
			// Note: if we intercept a key down message,
			// we also need to intercept the corresponding key up.
			static uint32_t pending_mask = 0;

			qboolean force_intercept = (pending_mask >> key) & 1;
			qboolean down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
			qboolean intercept =
				force_intercept ||
				(key_dest == key_game || M_KeyBinding())
				;

			if (intercept)
			{
				SDL_Event ev;
				if (down)
					pending_mask |= (1 << key);
				else
					pending_mask &= ~(1 << key);
				memset(&ev, 0, sizeof(ev));
				ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
				ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
				ev.key.keysym.scancode = hk_sdl_scancodes[key];
				SDL_PushEvent(&ev);
				return 1;
			}
		}
	}

	return CallNextHookEx(NULL, nCode, wParam, lParam);
}
#endif

void Sys_Init (void)
{
	OSVERSIONINFOEX	vinfo;
	DWORDLONG conditionMask = 0;
	int op = VER_GREATER_EQUAL;

	Sys_SetTimerResolution ();
	Sys_SetDPIAware ();

	memset (cwd, 0, sizeof(cwd));
	Sys_GetBasedir(NULL, cwd, sizeof(cwd));
	host_parms->basedir = cwd;

	/* userdirs not really necessary for windows guys.
	 * can be done if necessary, though... */
	host_parms->userdir = host_parms->basedir; /* code elsewhere relies on this ! */

	// Check for Windows version using VerifyVersionInfo instead of deprecated GetVersionEx
	memset(&vinfo, 0, sizeof(vinfo));
	vinfo.dwOSVersionInfoSize = sizeof(vinfo);

	// At least Win95 or NT 4.0 is required (4.0)
	vinfo.dwMajorVersion = 4;
	vinfo.dwMinorVersion = 0;
	VER_SET_CONDITION(conditionMask, VER_MAJORVERSION, op);
	VER_SET_CONDITION(conditionMask, VER_MINORVERSION, op);

	if (!VerifyVersionInfo(&vinfo, VER_MAJORVERSION | VER_MINORVERSION, conditionMask))
		Sys_Error ("QuakeSpasm requires at least Win95 or NT 4.0");

	// Check if we're on NT platform
	vinfo.dwPlatformId = VER_PLATFORM_WIN32_NT;
	VER_SET_CONDITION(conditionMask, VER_PLATFORMID, VER_EQUAL);
	WinNT = VerifyVersionInfo(&vinfo, VER_PLATFORMID, conditionMask);
	
	if (WinNT)
	{
		SYSTEM_INFO info;
		
		// Check for Vista or newer (6.0+)
		memset(&vinfo, 0, sizeof(vinfo));
		vinfo.dwOSVersionInfoSize = sizeof(vinfo);
		vinfo.dwMajorVersion = 6;
		vinfo.dwMinorVersion = 0;
		conditionMask = 0;
		VER_SET_CONDITION(conditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
		WinVista = VerifyVersionInfo(&vinfo, VER_MAJORVERSION, conditionMask);
		
		GetSystemInfo(&info);
		host_parms->numcpus = info.dwNumberOfProcessors;
		if (host_parms->numcpus < 1)
			host_parms->numcpus = 1;
	}
	else
	{
		WinNT = false; /* Win9x or WinME */
		host_parms->numcpus = 1;
		
		// Check for Win95
		memset(&vinfo, 0, sizeof(vinfo));
		vinfo.dwOSVersionInfoSize = sizeof(vinfo);
		vinfo.dwMajorVersion = 4;
		vinfo.dwMinorVersion = 0;
		conditionMask = 0;
		VER_SET_CONDITION(conditionMask, VER_MAJORVERSION, VER_EQUAL);
		VER_SET_CONDITION(conditionMask, VER_MINORVERSION, VER_EQUAL);
		Win95 = VerifyVersionInfo(&vinfo, VER_MAJORVERSION | VER_MINORVERSION, conditionMask);
		
		/* Unfortunately we can't check for Win95-gold vs Win95A/B/C this way 
		   since CSDVersion is not supported with VerifyVersionInfo.
		   Since this OS is so old, let's just assume it's the old version. */
		Win95old = Win95;
	}
	Sys_Printf("Detected %d CPUs.\n", host_parms->numcpus);

	if (isDedicated)
	{
		if (!AllocConsole () && GetLastError () != ERROR_ACCESS_DENIED)
		{
			isDedicated = false;	/* so that we have a graphical error dialog */
			Sys_Error ("Couldn't create dedicated server console");
		}

		hinput = GetStdHandle (STD_INPUT_HANDLE);
		houtput = GetStdHandle (STD_OUTPUT_HANDLE);

		// Enable ANSI escape sequences, UTF-8, and mouse input
		{
			DWORD mode = 0;
			GetConsoleMode (houtput, &mode);
			use_vtp = SetConsoleMode (houtput, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
			SetConsoleOutputCP (CP_UTF8);
			GetConsoleMode (hinput, &mode);
			SetConsoleMode (hinput, mode | ENABLE_EXTENDED_FLAGS);
		}
	}

	else
	{
		key_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyFilter, GetModuleHandleW(NULL), 0);
		if (!key_hook)
			Sys_Printf("Warning: SetWindowsHookExW failed (%ld)\n", GetLastError());
	}

}

void Sys_mkdir (const char *path)
{
	if (CreateDirectory(path, NULL) != 0)
		return;
	if (GetLastError() != ERROR_ALREADY_EXISTS)
		Sys_Error("Unable to create directory %s", path);
}

static const char errortxt1[] = "\nERROR-OUT BEGIN\n\n";
static const char errortxt2[] = "\nQUAKE ERROR: ";

void Sys_Error (const char *error, ...)
{
	va_list		argptr;
	char		text[1024];
	DWORD		dummy;

	host_parms->errstate++;

	va_start (argptr, error);
	q_vsnprintf (text, sizeof(text), error, argptr);
	va_end (argptr);

	PR_SwitchQCVM(NULL);

	Con_Redirect(NULL);

	if (isDedicated)
		WriteFile (houtput, errortxt1, strlen(errortxt1), &dummy, NULL);
	/* SDL will put these into its own stderr log,
	   so print to stderr even in graphical mode. */
	fputs (errortxt1, stderr);
	Host_Shutdown ();
	fputs (errortxt2, stderr);
	fputs (text, stderr);
	fputs ("\n\n", stderr);
	if (!isDedicated)
		PL_ErrorDialog(text);
	else
	{
		WriteFile (houtput, errortxt2, strlen(errortxt2), &dummy, NULL);
		WriteFile (houtput, text,      strlen(text),      &dummy, NULL);
		WriteFile (houtput, "\r\n",    2,		  &dummy, NULL);
		SDL_Delay (3000);	/* show the console 3 more seconds */
	}

	exit (1);
}

/* ============================================================
   Dedicated console scrollback
   ============================================================ */
#define SCROLLBACK_MAXLINES 2048
#define SCROLLBACK_LINESIZE 1024
#define DED_CHAT_COLOR_ON   0x1d
#define DED_CHAT_COLOR_OFF  0x1e

static char  scrollback_lines[SCROLLBACK_MAXLINES][SCROLLBACK_LINESIZE];
static char  scrollback_partial[SCROLLBACK_LINESIZE];
static int   scrollback_partial_len = 0;
static int   scrollback_head = 0;
static int   scrollback_count = 0;
static int   scrollback_offset = 0;
static qboolean scrollback_active = false;
static int   scrollback_enter_head = 0;
static int   scrollback_enter_partial_len = 0;
static qboolean scrollback_wrapped = false; // buffer wrapped fully during scrollback

static void Scrollback_CommitLine (void)
{
	scrollback_partial[scrollback_partial_len] = '\0';
	q_strlcpy (scrollback_lines[scrollback_head], scrollback_partial, SCROLLBACK_LINESIZE);
	scrollback_head = (scrollback_head + 1) % SCROLLBACK_MAXLINES;
	if (scrollback_count < SCROLLBACK_MAXLINES)
		scrollback_count++;
	else if (scrollback_active)
		scrollback_wrapped = true; // ring wrapped past enter_head
	scrollback_partial_len = 0;
	scrollback_partial[0] = '\0';
}

static void Scrollback_Feed (const char *text)
{
	while (*text)
	{
		if (*text == '\n')
			Scrollback_CommitLine ();
		else if (*text != '\r' && scrollback_partial_len < SCROLLBACK_LINESIZE - 1)
		{
			scrollback_partial[scrollback_partial_len++] = *text;
			scrollback_partial[scrollback_partial_len] = '\0';
		}
		text++;
	}
}

static int Scrollback_TermHeight (void)
{
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	if (GetConsoleScreenBufferInfo (houtput, &csbi))
		return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
	return 24;
}

static int Scrollback_TermWidth (void)
{
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	if (GetConsoleScreenBufferInfo (houtput, &csbi))
		return csbi.srWindow.Right - csbi.srWindow.Left + 1;
	return 80;
}

/* Dedicated console input state (file-scope so Sys_Printf and scrollback can access) */
static char	ded_input[MAXCMDLINE];
static int	ded_input_len;
static int	ded_input_cursor;
cvar_t		sys_dedmouse_capture = {"sys_dedmouse_capture", "0", CVAR_ARCHIVE};

static qboolean Scrollback_ShowCaretRow (void)
{
	return scrollback_offset > 0;
}

static int Scrollback_VisibleRowsForOffset (int offset)
{
	int reserved = 1; // input row is always visible

	if (offset > 0)
		reserved++; // caret row only while actually scrolled up

	{
		int visible = Scrollback_TermHeight () - reserved;
		return (visible < 1) ? 1 : visible;
	}
}

static int Scrollback_VisibleRows (void)
{
	return Scrollback_VisibleRowsForOffset (scrollback_offset);
}

static int Scrollback_TotalLines (void)
{
	return scrollback_count + ((scrollback_partial_len > 0) ? 1 : 0);
}

static int Scrollback_LineRows (const char *line, int width)
{
	int cols = 0;

	if (width < 1)
		width = 1;

	while (*line)
	{
		if ((unsigned char)*line == 27 && line[1] == '[')
		{
			line += 2;
			while (*line && !(*line >= '@' && *line <= '~'))
				line++;
			if (*line)
				line++;
			continue;
		}

		if (((unsigned char)*line & 0xC0) == 0x80)
		{
			line++;
			continue;
		}

		cols++;
		line++;
	}

	return (cols <= 0) ? 1 : ((cols - 1) / width) + 1;
}

static int Scrollback_MaxOffset (void)
{
	int maxoff = Scrollback_TotalLines () - Scrollback_VisibleRowsForOffset (1);
	return (maxoff < 0) ? 0 : maxoff;
}

static qboolean Scrollback_AtBottom (void)
{
	return scrollback_offset <= 0;
}

static qboolean Scrollback_Frozen (void)
{
	return scrollback_active && !Scrollback_AtBottom ();
}

static void Scrollback_Redraw (void)
{
	DWORD dummy;
	int height = Scrollback_TermHeight ();
	int width = Scrollback_TermWidth ();
	int total = Scrollback_TotalLines ();
	int visible;
	int first, end, rows, i;

	if (scrollback_offset > Scrollback_MaxOffset ())
		scrollback_offset = Scrollback_MaxOffset ();
	if (scrollback_offset < 0)
		scrollback_offset = 0;

	visible = Scrollback_VisibleRows ();
	end = total - scrollback_offset;
	if (end < 0)
		end = 0;
	if (end > total)
		end = total;

	first = end;
	rows = 0;
	while (first > 0)
	{
		const char *line;
		int line_rows;

		if (first - 1 == scrollback_count)
			line = scrollback_partial;
		else
		{
			int idx = (scrollback_head - scrollback_count + first - 1 + SCROLLBACK_MAXLINES) % SCROLLBACK_MAXLINES;
			line = scrollback_lines[idx];
		}

		line_rows = Scrollback_LineRows (line, width);

		if (rows + line_rows > visible && rows > 0)
			break;

		rows += line_rows;
		first--;
	}

	WriteFile (houtput, "\033[2J\033[H", 7, &dummy, NULL);

	for (i = first; i < end; i++)
	{
		if (i == scrollback_count)
		{
			WriteFile (houtput, scrollback_partial, (DWORD)strlen(scrollback_partial), &dummy, NULL);
		}
		else
		{
			int idx = (scrollback_head - scrollback_count + i + SCROLLBACK_MAXLINES) % SCROLLBACK_MAXLINES;
			WriteFile (houtput, scrollback_lines[idx], (DWORD)strlen(scrollback_lines[idx]), &dummy, NULL);
			WriteFile (houtput, "\n", 1, &dummy, NULL);
		}
	}

	// Caret row — only when scrolled above the live bottom
	if (Scrollback_ShowCaretRow ())
	{
		char pos[32];
		char line[512];
		int j;
		q_snprintf (pos, sizeof(pos), "\033[%d;1H", height - 1);
		WriteFile (houtput, pos, (DWORD)strlen(pos), &dummy, NULL);
		if (width >= (int)sizeof(line))
			width = (int)sizeof(line) - 1;
		for (j = 0; j < width; j++)
			line[j] = (j % 4 == 1) ? '^' : ' ';
		line[width] = '\0';
		WriteFile (houtput, line, (DWORD)width, &dummy, NULL);
	}

	// Input line on the bottom row
	{
		char pos[32];
		q_snprintf (pos, sizeof(pos), "\033[%d;1H\033[2K", height);
		WriteFile (houtput, pos, (DWORD)strlen(pos), &dummy, NULL);
		if (ded_input_len > 0)
			WriteFile (houtput, ded_input, ded_input_len, &dummy, NULL);
		if (ded_input_cursor < ded_input_len)
		{
			q_snprintf (pos, sizeof(pos), "\033[%d;%dH", height, ded_input_cursor + 1);
			WriteFile (houtput, pos, (DWORD)strlen(pos), &dummy, NULL);
		}
	}
}

static void Scrollback_Enter (void)
{
	DWORD dummy;
	if (!use_vtp) // scrollback requires ANSI escape support
		return;
	scrollback_active = true;
	scrollback_enter_head = scrollback_head;
	scrollback_enter_partial_len = scrollback_partial_len;
	scrollback_offset = Scrollback_VisibleRowsForOffset (1); // start one page up
	if (scrollback_offset > Scrollback_MaxOffset ())
		scrollback_offset = Scrollback_MaxOffset ();
	WriteFile (houtput, "\033[?1049h", 8, &dummy, NULL);
	Scrollback_Redraw ();
}

static void Ded_ClearInputLine (void);   // forward decl
static void Ded_RestoreInputLine (void); // forward decl

static void Scrollback_Exit (void)
{
	DWORD dummy;
	int missed, i;

	if (use_vtp)
		WriteFile (houtput, "\033[?1049l", 8, &dummy, NULL);
	scrollback_active = false;
	scrollback_offset = 0;

	// Clear the pending input line before replaying, so replay
	// doesn't append onto partially-typed text
	Ded_ClearInputLine ();

	// Replay any lines that arrived while we were scrolled up
	if (scrollback_wrapped)
	{
		// Ring buffer wrapped fully — replay everything we have
		missed = scrollback_count;
	}
	else
	{
		missed = (scrollback_head - scrollback_enter_head + SCROLLBACK_MAXLINES) % SCROLLBACK_MAXLINES;
		if (missed > scrollback_count)
			missed = scrollback_count;
	}
	scrollback_wrapped = false;

	if (missed > 0)
	{
		for (i = missed; i > 0; i--)
		{
			int idx = (scrollback_head - i + SCROLLBACK_MAXLINES) % SCROLLBACK_MAXLINES;
			WriteFile (houtput, scrollback_lines[idx], (DWORD)strlen(scrollback_lines[idx]), &dummy, NULL);
			WriteFile (houtput, "\n", 1, &dummy, NULL);
		}
	}

	// Flush any partial line fragment that hasn't been newline-terminated
	if (scrollback_partial_len > scrollback_enter_partial_len)
	{
		WriteFile (houtput, scrollback_partial + scrollback_enter_partial_len,
			(DWORD)(scrollback_partial_len - scrollback_enter_partial_len), &dummy, NULL);
	}

	// Restore the input line so the user can keep typing
	Ded_RestoreInputLine ();
}

static void Scrollback_PageUp (void)
{
	scrollback_offset += Scrollback_VisibleRowsForOffset (1);
	Scrollback_Redraw (); // clamp happens inside
}

static void Scrollback_PageDown (void)
{
	if (Scrollback_AtBottom ())
	{
		Scrollback_Exit ();
		return;
	}
	scrollback_offset -= Scrollback_VisibleRows ();
	if (scrollback_offset < 0)
		scrollback_offset = 0;
	Scrollback_Redraw ();
}

static void Scrollback_LineUp (void)
{
	scrollback_offset++;
	Scrollback_Redraw ();
}

static void Scrollback_LineDown (void)
{
	if (Scrollback_AtBottom ())
	{
		Scrollback_Exit ();
		return;
	}
	scrollback_offset--;
	if (scrollback_offset < 0)
		scrollback_offset = 0;
	Scrollback_Redraw ();
}

static char	ded_output_hold[8192];
static size_t	ded_output_hold_len;

static void Ded_ClearInputLine (void)
{
	DWORD dummy;
	int i;
	if (ded_input_len <= 0)
		return;
	WriteFile (houtput, "\r", 1, &dummy, NULL);
	for (i = 0; i < ded_input_len; i++)
		WriteFile (houtput, " ", 1, &dummy, NULL);
	WriteFile (houtput, "\r", 1, &dummy, NULL);
}

static void Ded_RestoreInputLine (void)
{
	DWORD dummy;
	int i;
	if (ded_input_len <= 0)
		return;
	WriteFile (houtput, ded_input, ded_input_len, &dummy, NULL);
	for (i = ded_input_len; i > ded_input_cursor; i--)
		WriteFile (houtput, "\b", 1, &dummy, NULL);
}

static void Ded_WriteOutput (const char *text, size_t len)
{
	DWORD dummy;
	if (len == 0)
		return;
	WriteFile (houtput, text, (DWORD)len, &dummy, NULL);
}

static void Ded_FlushBufferedOutput (qboolean allow_partial)
{
	size_t emit = 0;

	if (!ded_output_hold_len)
		return;

	if (allow_partial)
	{
		emit = ded_output_hold_len;
	}
	else
	{
		size_t i;
		for (i = ded_output_hold_len; i > 0; i--)
		{
			if (ded_output_hold[i - 1] == '\n')
			{
				emit = i;
				break;
			}
		}
		if (!emit)
			return;
	}

	if (ded_input_len > 0)
		Ded_ClearInputLine ();

	Ded_WriteOutput (ded_output_hold, emit);

	if (emit < ded_output_hold_len)
		memmove (ded_output_hold, ded_output_hold + emit, ded_output_hold_len - emit);
	ded_output_hold_len -= emit;

	if (ded_input_len > 0)
		Ded_RestoreInputLine ();
}

static void Ded_HandleOutput (const char *text)
{
	size_t len = strlen (text);

	if (!len)
		return;

	Scrollback_Feed (text);
	if (Scrollback_Frozen ())
		return;

	if (ded_input_len <= 0 && !ded_output_hold_len)
	{
		Ded_WriteOutput (text, len);
		return;
	}

	if (len > sizeof(ded_output_hold) - ded_output_hold_len)
	{
		Ded_FlushBufferedOutput (ded_input_len <= 0);
		if (len > sizeof(ded_output_hold) - ded_output_hold_len && ded_input_len <= 0)
			Ded_FlushBufferedOutput (true);
	}

	if (len > sizeof(ded_output_hold) - ded_output_hold_len)
	{
		size_t keep = sizeof(ded_output_hold) - 1;
		if (len >= keep)
		{
			if (ded_input_len > 0)
			{
				memcpy (ded_output_hold, text + len - keep, keep);
				ded_output_hold_len = keep;
			}
			else
			{
				Ded_WriteOutput (text, len);
				ded_output_hold_len = 0;
			}
			return;
		}
	}

	memcpy (ded_output_hold + ded_output_hold_len, text, len);
	ded_output_hold_len += len;

	Ded_FlushBufferedOutput (ded_input_len <= 0);
}

void Sys_Printf (const char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr,fmt);
	q_vsnprintf (text, sizeof(text), fmt, argptr);
	va_end (argptr);

	if (!isDedicated)
	{
		// Non-dedicated: plain dequake to stdout
		unsigned char *ch = (unsigned char *)text;
		unsigned char *dst = (unsigned char *)text;
		while (*ch)
		{
			if (*ch == '^' && *(ch + 1) != '\0' &&
				(*(ch + 1) == 'm' || *(ch + 1) == 'g' || *(ch + 1) == 'd'))
			{
				ch += 2;
				continue;
			}
			*dst++ = dequake[*ch++];
		}
		*dst = '\0';
		fputs (text, stdout);
		return;
	}

	if (!use_vtp)
	{
		// VTP unavailable (pre-Win10): plain dequake with WriteFile
		unsigned char *ch = (unsigned char *)text;
		unsigned char *dst = (unsigned char *)text;
		while (*ch)
		{
			if (*ch == DED_CHAT_COLOR_ON || *ch == DED_CHAT_COLOR_OFF)
			{
				ch++;
				continue;
			}
			if (*ch == '^' && *(ch + 1) != '\0' &&
				(*(ch + 1) == 'm' || *(ch + 1) == 'g' || *(ch + 1) == 'd'))
			{
				ch += 2;
				continue;
			}
			*dst++ = dequake[*ch++];
		}
		*dst = '\0';
		Ded_HandleOutput (text);
		return;
	}

	// Dedicated: ANSI true color + UTF-8 glyphs
	// 0=normal, 1=red #a85c4c, 2=gold #8d7039, 3=brackets #c97d49
	{
		char output[8192];
		unsigned char *ch = (unsigned char *)text;
		char *dst = output;
		char *end = output + sizeof(output) - 32;
		int cur_color = 0;
		qboolean chat_color = false;
		qboolean gold_digits = false;

		while (*ch && dst < end)
		{
			int want;

			if (*ch == DED_CHAT_COLOR_ON)
			{
				chat_color = true;
				ch++;
				continue;
			}
			if (*ch == DED_CHAT_COLOR_OFF)
			{
				chat_color = false;
				ch++;
				continue;
			}
			if (*ch == '^' && *(ch + 1) != '\0' && *(ch + 1) == 'm')
			{
				ch += 2;
				continue;
			}
			if (*ch == '^' && *(ch + 1) != '\0' && *(ch + 1) == 'd')
			{
				gold_digits = false;
				ch += 2;
				continue;
			}
			if (*ch == '^' && *(ch + 1) != '\0' && *(ch + 1) == 'g')
			{
				gold_digits ^= true;
				ch += 2;
				continue;
			}

			if (chat_color)
				want = 4; // say / say_team message text
			else if (gold_digits && *ch >= '0' && *ch <= '9')
				want = 2; // explicit gold digit markup
			else if ((*ch >= 18 && *ch <= 27) || (*ch >= 146 && *ch <= 155) ||
			         *ch == 133 || *ch == 142 || *ch == 143 || *ch == 156)
				want = 2; // gold digits / gold dots
			else if ((*ch >= 16 && *ch <= 17) || (*ch >= 144 && *ch <= 145))
				want = 3; // brackets
			else if (*ch == 11 || *ch == 139)
				want = 1; // red squares
			else if (*ch == 141)
				want = 1; // red play arrow
			else if (*ch > 127)
				want = 1; // red
			else
				want = 0; // normal

			if (want != cur_color)
			{
				const char *esc;
				int esc_len;

				if (want == 0)
					esc = "\033[0m";
				else if (want == 1) // #a85c4c via ANSI 256 color 95
					esc = "\033[38;5;95m";
				else if (want == 2) // #8d7039 via ANSI 256 color 136
					esc = "\033[38;5;136m";
				else if (want == 3) // #c97d49 via ANSI 256 color 173
					esc = "\033[38;5;173m";
				else // chat text via ANSI 256 color 247
					esc = "\033[38;5;247m";

				esc_len = (int)strlen (esc);
				memcpy (dst, esc, esc_len);
				dst += esc_len;
				cur_color = want;
			}

			if (*ch == 5 || *ch == 14 || *ch == 15 || *ch == 28 ||
			    *ch == 133 || *ch == 142 || *ch == 143 || *ch == 156)
			{
				*dst++ = (char)0xC2;
				*dst++ = (char)0xB7; // UTF-8 middle dot ·
				ch++;
			}
			else if (*ch == 11 || *ch == 139)
			{
				*dst++ = (char)0xE2;
				*dst++ = (char)0x96;
				*dst++ = (char)0xA0; // UTF-8 black square ■
				ch++;
			}
			else if (*ch == 141)
			{
				*dst++ = (char)0xE2;
				*dst++ = (char)0x96;
				*dst++ = (char)0xB6; // UTF-8 play arrow ▶
				ch++;
			}
			else
			{
				*dst++ = dequake[*ch++];
			}
		}

		if (cur_color)
		{
			memcpy (dst, "\033[0m", 4);
			dst += 4;
		}

		*dst = '\0';
		Ded_HandleOutput (output);
	}
}

void Sys_Quit (void)
{
	Host_Shutdown();

	if (isDedicated)
		FreeConsole ();

	exit (0);
}

void Sys_InstallDedicatedSignalHandlers (void)
{
}

qboolean Sys_HasDedicatedQuitRequest (void)
{
	return false;
}

double Sys_DoubleTime (void)
{
#if 1
	return SDL_GetPerformanceCounter() / (long double)SDL_GetPerformanceFrequency();
#else
	return SDL_GetTicks() / 1000.0;
#endif
}

static void Dedicated_RedrawInputLine(const char* text, int textlen, int cursor_pos, int previous_len)
{
	DWORD dummy;
	const char carriage = '\r';
	const char space = ' ';

	WriteFile(houtput, &carriage, 1, &dummy, NULL);
	if (textlen > 0)
		WriteFile(houtput, text, (DWORD)textlen, &dummy, NULL);

	if (previous_len > textlen)
	{
		int diff = previous_len - textlen;
		for (int i = 0; i < diff; ++i)
			WriteFile(houtput, &space, 1, &dummy, NULL);
	}

	WriteFile(houtput, &carriage, 1, &dummy, NULL);
	if (cursor_pos > 0)
		WriteFile(houtput, text, (DWORD)cursor_pos, &dummy, NULL);
}

#if defined(_WIN32)
void Sys_Image_BGRA_To_Clipboard(byte* bmbits, int width, int height, int size) // woods #screenshotcopy
{

	HBITMAP hBitmap = CreateBitmap(width, height, 1, 32 /* bits per pixel is 32 */, bmbits);

	OpenClipboard(NULL);

	if (!EmptyClipboard())
	{
		CloseClipboard();
		return;
	}

	if ((SetClipboardData(CF_BITMAP, hBitmap)) == NULL)
		Sys_Error("SetClipboardData failed");

	CloseClipboard();
}
#endif

static void Sys_RewriteInputLine(const char* newline, char* con_text, size_t con_text_size, int* textlen, int* cursor_pos, DWORD* dummy) // woods #serverhistory
{
	int oldlen = *textlen;
	int oldpos = *cursor_pos;
	size_t newlen;

	for (int i = 0; i < oldpos; i++)
		WriteFile(houtput, "\b", 1, dummy, NULL);
	for (int i = 0; i < oldlen; i++)
		WriteFile(houtput, " ", 1, dummy, NULL);
	for (int i = 0; i < oldlen; i++)
		WriteFile(houtput, "\b", 1, dummy, NULL);

	newlen = q_strlcpy(con_text, newline ? newline : "", con_text_size);
	if (newlen)
		WriteFile(houtput, con_text, (DWORD)newlen, dummy, NULL);

	*textlen = (int)newlen;
	*cursor_pos = *textlen;
}

const char *Sys_ConsoleInput (void) // woods #arrowkeys #serverhistory
{
	// Input state is in file-scope ded_input / ded_input_len / ded_input_cursor
	INPUT_RECORD	recs[1024];
	int		ch;
	DWORD		dummy, numread, numevents;

	//apply mouse capture / quick edit mode dynamically based on cvar
	{
		static int last_mouse_capture = -1;
		int want = (int)sys_dedmouse_capture.value;
		if (want != last_mouse_capture)
		{
			DWORD mode = 0;
			GetConsoleMode(hinput, &mode);
			if (want)
			{
				mode |= ENABLE_MOUSE_INPUT;
				mode &= ~ENABLE_QUICK_EDIT_MODE;
			}
			else
			{
				mode &= ~ENABLE_MOUSE_INPUT;
				mode |= ENABLE_QUICK_EDIT_MODE;
			}
			SetConsoleMode(hinput, mode | ENABLE_EXTENDED_FLAGS);
			last_mouse_capture = want;
		}
	}

	for ( ;; )
	{
		if (GetNumberOfConsoleInputEvents(hinput, &numevents) == 0)
			Sys_Error ("Error getting # of console events");

		if (! numevents)
			break;

		if (ReadConsoleInput(hinput, recs, 1, &numread) == 0)
			Sys_Error ("Error reading console input");

		if (numread != 1)
			Sys_Error ("Couldn't read console input");

		if (recs[0].EventType == KEY_EVENT)
		{
			if (recs[0].Event.KeyEvent.bKeyDown == TRUE)
			{
				// PageUp / PageDown for scrollback
				if (recs[0].Event.KeyEvent.wVirtualKeyCode == VK_PRIOR)
				{
					if (!scrollback_active)
						Scrollback_Enter ();
					else
						Scrollback_PageUp ();
					continue;
				}
				else if (recs[0].Event.KeyEvent.wVirtualKeyCode == VK_NEXT)
				{
					if (scrollback_active)
						Scrollback_PageDown ();
					continue;
				}
					else if (recs[0].Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE && scrollback_active)
					{
						Scrollback_Exit ();
						continue;
					}

				// In scrollback mode, arrow keys scroll instead of editing
					if (Scrollback_Frozen ())
					{
						if (recs[0].Event.KeyEvent.wVirtualKeyCode == VK_UP)
							Scrollback_LineUp ();
					else if (recs[0].Event.KeyEvent.wVirtualKeyCode == VK_DOWN)
						Scrollback_LineDown ();
					continue;
				}

				if (recs[0].Event.KeyEvent.wVirtualKeyCode == VK_LEFT)
				{
					if (ded_input_cursor > 0)
					{
						ded_input_cursor--;
						WriteFile(houtput, "\b", 1, &dummy, NULL);
					}
					continue;
				}
				else if (recs[0].Event.KeyEvent.wVirtualKeyCode == VK_RIGHT)
				{
					if (ded_input_cursor < ded_input_len)
					{
						WriteFile(houtput, &ded_input[ded_input_cursor], 1, &dummy, NULL);
						ded_input_cursor++;
					}
					continue;
				}
				else if (recs[0].Event.KeyEvent.wVirtualKeyCode == VK_UP)
				{
					char history_line[MAXCMDLINE];
					if (History_GetPrevious(ded_input, history_line, sizeof(history_line)))
						Sys_RewriteInputLine(history_line, ded_input, sizeof(ded_input), &ded_input_len, &ded_input_cursor, &dummy);
					continue;
				}
				else if (recs[0].Event.KeyEvent.wVirtualKeyCode == VK_DOWN)
				{
					char history_line[MAXCMDLINE];
					if (History_GetNext(ded_input, history_line, sizeof(history_line)))
						Sys_RewriteInputLine(history_line, ded_input, sizeof(ded_input), &ded_input_len, &ded_input_cursor, &dummy);
					continue;
				}
				else if (recs[0].Event.KeyEvent.uChar.AsciiChar == '\t')
				{
					ded_input[ded_input_len] = '\0';
					int previous_len = ded_input_len;
					Con_DedicatedTabComplete(ded_input, sizeof(ded_input), &ded_input_len, &ded_input_cursor);
					Dedicated_RedrawInputLine(ded_input, ded_input_len, ded_input_cursor, previous_len);
					continue;
				}

				ch = recs[0].Event.KeyEvent.uChar.AsciiChar;
				if (ch && (recs[0].Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED))
				{
					BYTE keyboard[256] = {0};
					WORD output = 0;

					keyboard[VK_SHIFT] = 0x80;
					if (ToAscii(recs[0].Event.KeyEvent.wVirtualKeyCode,
						recs[0].Event.KeyEvent.wVirtualScanCode, keyboard, &output, 0) == 1)
						ch = (char) output;
				}

				switch (ch)
				{
				case 21: // Ctrl-U
					Sys_RewriteInputLine(NULL, ded_input, sizeof(ded_input), &ded_input_len, &ded_input_cursor, &dummy);
					Con_DedicatedResetTabState();
					if (!ded_input_len)
						Ded_FlushBufferedOutput (true);
					break;

				case '\r':
					WriteFile(houtput, "\r\n", 2, &dummy, NULL);

					if (ded_input_len != 0)
					{
						ded_input[ded_input_len] = 0;
						ded_input_len = 0;
						ded_input_cursor = 0;
						Con_DedicatedResetTabState();
						Ded_FlushBufferedOutput (true);
						return ded_input;
					}

					break;

				case '\b':
					if (ded_input_cursor > 0)
					{
						memmove(&ded_input[ded_input_cursor - 1], &ded_input[ded_input_cursor], ded_input_len - ded_input_cursor);
						ded_input_cursor--;
						ded_input_len--;

						WriteFile(houtput, "\b", 1, &dummy, NULL);
						if (ded_input_cursor < ded_input_len)
						{
							WriteFile(houtput, &ded_input[ded_input_cursor], ded_input_len - ded_input_cursor, &dummy, NULL);
							WriteFile(houtput, " ", 1, &dummy, NULL);
							for (int i = 0; i < ded_input_len - ded_input_cursor + 1; i++)
								WriteFile(houtput, "\b", 1, &dummy, NULL);
						}
						else
						{
							WriteFile(houtput, " \b", 2, &dummy, NULL);
						}
						Con_DedicatedResetTabState();
						if (!ded_input_len)
							Ded_FlushBufferedOutput (true);
					}
					break;

				default:
					if (ch >= ' ')
					{
						if (ded_input_cursor < ded_input_len)
						{
							memmove(&ded_input[ded_input_cursor + 1], &ded_input[ded_input_cursor], ded_input_len - ded_input_cursor);
							ded_input[ded_input_cursor] = ch;
							ded_input_len++;

							WriteFile(houtput, &ded_input[ded_input_cursor], ded_input_len - ded_input_cursor, &dummy, NULL);

							ded_input_cursor++;
							for (int i = 0; i < ded_input_len - ded_input_cursor; i++)
								WriteFile(houtput, "\b", 1, &dummy, NULL);
						}
						else
						{
							ded_input[ded_input_len] = ch;
							WriteFile(houtput, &ch, 1, &dummy, NULL);
							ded_input_len++;
							ded_input_cursor++;
						}
						Con_DedicatedResetTabState();
					}

					break;
				}
			}
		}
		else if (recs[0].EventType == MOUSE_EVENT &&
		         recs[0].Event.MouseEvent.dwEventFlags == MOUSE_WHEELED)
		{
			short delta = (short)HIWORD(recs[0].Event.MouseEvent.dwButtonState);
			if (delta > 0) // scroll up
			{
				if (!scrollback_active)
				{
					if (sys_dedmouse_capture.value != 0)
						Scrollback_Enter ();
				}
				else
				{
					scrollback_offset += 3;
					Scrollback_Redraw ();
				}
			}
			else if (delta < 0) // scroll down
			{
					if (scrollback_active)
					{
						if (Scrollback_AtBottom ())
							Scrollback_Exit ();
						else
						{
							scrollback_offset -= 3;
							if (scrollback_offset < 0)
								scrollback_offset = 0;
							Scrollback_Redraw ();
						}
					}
			}
		}
	}

	return NULL;
}

void Sys_Sleep (unsigned long msecs)
{
/*	Sleep (msecs);*/
	SDL_Delay (msecs);
}

void Sys_SendKeyEvents (void)
{
	IN_Commands();		//ericw -- allow joysticks to add keys so they can be used to confirm SCR_ModalMessage
	IN_SendKeyEvents();
}

void Sys_SetDockProgress (float fraction)
{
	(void)fraction;	// no dock icon on Windows
}

#if defined(_WIN32) // woods #disablecaps via ironwail
void Sys_ActivateKeyFilter (qboolean active)
{
	if (isDedicated || !!active == (key_hook != NULL))
		return;

	if (key_hook)
	{
		UnhookWindowsHookEx(key_hook);
		key_hook = NULL;
	}
	else
	{
		key_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyFilter, GetModuleHandleW(NULL), 0);
		if (!key_hook)
			Sys_Printf("Warning: SetWindowsHookExW failed (%lu)\n", GetLastError());
	}
}
#endif
