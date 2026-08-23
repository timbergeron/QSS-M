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
#include <objbase.h>

#include "quakedef.h"
#include "q_ctype.h"

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#include <sys/types.h>
#include <limits.h>
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
#if defined(USE_SDL2)
#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#include <SDL2/SDL_syswm.h>
#else
#include "SDL_syswm.h"
#endif
#endif


qboolean		isDedicated;
qboolean	Win95, Win95old, WinNT, WinVista;
cvar_t		sys_throttle = {"sys_throttle", "0.02", CVAR_ARCHIVE};

static HANDLE		hinput, houtput;
static qboolean		use_vtp = false; // ANSI virtual terminal processing available

#if defined(USE_SDL2)
typedef enum {
	QSS_TBPF_NOPROGRESS = 0,
	QSS_TBPF_INDETERMINATE = 0x1,
	QSS_TBPF_NORMAL = 0x2
} qss_tbpflag_t;

typedef struct qss_ITaskbarList3 qss_ITaskbarList3;
typedef struct qss_ITaskbarList3Vtbl {
	HRESULT (STDMETHODCALLTYPE *QueryInterface)(qss_ITaskbarList3 *self, REFIID riid, void **ppvObject);
	ULONG (STDMETHODCALLTYPE *AddRef)(qss_ITaskbarList3 *self);
	ULONG (STDMETHODCALLTYPE *Release)(qss_ITaskbarList3 *self);
	HRESULT (STDMETHODCALLTYPE *HrInit)(qss_ITaskbarList3 *self);
	HRESULT (STDMETHODCALLTYPE *AddTab)(qss_ITaskbarList3 *self, HWND hwnd);
	HRESULT (STDMETHODCALLTYPE *DeleteTab)(qss_ITaskbarList3 *self, HWND hwnd);
	HRESULT (STDMETHODCALLTYPE *ActivateTab)(qss_ITaskbarList3 *self, HWND hwnd);
	HRESULT (STDMETHODCALLTYPE *SetActiveAlt)(qss_ITaskbarList3 *self, HWND hwnd);
	HRESULT (STDMETHODCALLTYPE *MarkFullscreenWindow)(qss_ITaskbarList3 *self, HWND hwnd, BOOL fFullscreen);
	HRESULT (STDMETHODCALLTYPE *SetProgressValue)(qss_ITaskbarList3 *self, HWND hwnd, ULONGLONG ullCompleted, ULONGLONG ullTotal);
	HRESULT (STDMETHODCALLTYPE *SetProgressState)(qss_ITaskbarList3 *self, HWND hwnd, qss_tbpflag_t tbpFlags);
	HRESULT (STDMETHODCALLTYPE *RegisterTab)(qss_ITaskbarList3 *self, HWND hwndTab, HWND hwndMDI);
	HRESULT (STDMETHODCALLTYPE *UnregisterTab)(qss_ITaskbarList3 *self, HWND hwndTab);
	HRESULT (STDMETHODCALLTYPE *SetTabOrder)(qss_ITaskbarList3 *self, HWND hwndTab, HWND hwndInsertBefore);
	HRESULT (STDMETHODCALLTYPE *SetTabActive)(qss_ITaskbarList3 *self, HWND hwndTab, HWND hwndMDI, DWORD reserved);
	HRESULT (STDMETHODCALLTYPE *ThumbBarAddButtons)(qss_ITaskbarList3 *self, HWND hwnd, UINT count, void *buttons);
	HRESULT (STDMETHODCALLTYPE *ThumbBarUpdateButtons)(qss_ITaskbarList3 *self, HWND hwnd, UINT count, void *buttons);
	HRESULT (STDMETHODCALLTYPE *ThumbBarSetImageList)(qss_ITaskbarList3 *self, HWND hwnd, HANDLE imageList);
	HRESULT (STDMETHODCALLTYPE *SetOverlayIcon)(qss_ITaskbarList3 *self, HWND hwnd, HICON icon, LPCWSTR description);
} qss_ITaskbarList3Vtbl;
struct qss_ITaskbarList3 {
	const qss_ITaskbarList3Vtbl *lpVtbl;
};

typedef HRESULT (WINAPI *qss_CoInitializeEx_f)(LPVOID pvReserved, DWORD dwCoInit);
typedef void (WINAPI *qss_CoUninitialize_f)(void);
typedef HRESULT (WINAPI *qss_CoCreateInstance_f)(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID *ppv);
typedef UINT (WINAPI *qss_GetDpiForWindow_f)(HWND hwnd);
typedef int (WINAPI *qss_GetSystemMetricsForDpi_f)(int index, UINT dpi);

static const CLSID qss_CLSID_TaskbarList =
	{0x56fdf344, 0xfd6d, 0x11d0, {0x95, 0x8a, 0x00, 0x60, 0x97, 0xc9, 0xa0, 0x90}};
static const IID qss_IID_ITaskbarList3 =
	{0xea1afb91, 0x9e28, 0x4b86, {0x90, 0xe9, 0x9e, 0x9f, 0x8a, 0x5e, 0xef, 0xaf}};

static HMODULE taskbar_ole32;
static qss_CoUninitialize_f taskbar_CoUninitialize;
static qss_ITaskbarList3 *taskbar_list;
static qboolean taskbar_init_attempted;
static qboolean taskbar_com_initialized;
static HWND taskbar_progress_hwnd;
static HWND taskbar_notification_hwnd;
static unsigned int taskbar_notification_count;
static UINT taskbar_button_created_message;
static qboolean taskbar_message_hook_installed;
static qboolean taskbar_shutdown;	/* set at quit so late calls cannot re-init COM */

/* last progress state, reapplied when explorer recreates the taskbar button */
static const ULONGLONG taskbar_progress_total = 1000;
static qss_tbpflag_t taskbar_progress_state;
static ULONGLONG taskbar_progress_completed;

static void Sys_ShutdownTaskbarShell(void);
#endif

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

int Sys_remove (const char *path)
{
	return remove (path);
}

qboolean Sys_GetExecutablePath(char *out, size_t outsize)
{
	DWORD len;

	if (!out || outsize == 0)
		return false;

	len = GetModuleFileNameA(NULL, out, (DWORD)outsize);
	if (len > 0 && (size_t)len < outsize)
		return true;

	out[0] = '\0';
	return false;
}

unsigned long Sys_GetProcessId(void)
{
	return (unsigned long)GetCurrentProcessId();
}

qboolean Sys_MakeExecutable(const char *path)
{
	(void)path;
	return true;
}

static void Sys_PathDirName(const char *path, char *out, size_t outsize)
{
	const char *slash, *backslash, *last;
	size_t len;

	if (out && outsize)
		out[0] = '\0';
	if (!path || !*path || !out || !outsize)
		return;

	slash = strrchr(path, '/');
	backslash = strrchr(path, '\\');
	last = slash;
	if (!last || (backslash && backslash > last))
		last = backslash;
	if (!last)
	{
		q_strlcpy(out, ".", outsize);
		return;
	}

	len = (size_t)(last - path);
	if (len == 2 && q_isalpha((unsigned char)path[0]) &&
		path[1] == ':' && (path[2] == '/' || path[2] == '\\'))
		len = 3;
	if (len == 0)
		len = 1;
	if (len >= outsize)
		len = outsize - 1;
	memcpy(out, path, len);
	out[len] = '\0';
}

static qboolean Sys_AppendQuotedArg(char *cmdline, size_t cmdline_size,
	const char *arg)
{
	const char *s;
	size_t len;
	size_t backslashes = 0;

	len = strlen(cmdline);
	if (len + 2 >= cmdline_size)
		return false;
	if (len)
		cmdline[len++] = ' ';
	cmdline[len++] = '"';
	cmdline[len] = '\0';

	for (s = arg; s && *s; s++)
	{
		if (*s == '\\')
		{
			backslashes++;
			continue;
		}
		if (*s == '"')
		{
			while (backslashes > 0)
			{
				backslashes--;
				if (len + 2 >= cmdline_size)
					return false;
				cmdline[len++] = '\\';
				cmdline[len++] = '\\';
			}
			backslashes = 0;
			if (len + 2 >= cmdline_size)
				return false;
			cmdline[len++] = '\\';
			cmdline[len++] = '"';
		}
		else
		{
			while (backslashes > 0)
			{
				backslashes--;
				if (len + 1 >= cmdline_size)
					return false;
				cmdline[len++] = '\\';
			}
			backslashes = 0;
			if (len + 1 >= cmdline_size)
				return false;
			cmdline[len++] = *s;
		}
		cmdline[len] = '\0';
	}

	while (backslashes > 0)
	{
		backslashes--;
		if (len + 2 >= cmdline_size)
			return false;
		cmdline[len++] = '\\';
		cmdline[len++] = '\\';
	}
	if (len + 1 >= cmdline_size)
		return false;
	cmdline[len++] = '"';
	cmdline[len] = '\0';
	return true;
}

static qboolean Sys_CreateProcessCommand(const char *exe_path,
	const char *arg, const char *working_dir, char *error, size_t error_size)
{
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	char cmdline[MAX_OSPATH * 3];

	if (error && error_size)
		error[0] = '\0';

	memset(&si, 0, sizeof(si));
	memset(&pi, 0, sizeof(pi));
	si.cb = sizeof(si);
	cmdline[0] = '\0';

	if (!Sys_AppendQuotedArg(cmdline, sizeof(cmdline), exe_path) ||
		(arg && *arg && !Sys_AppendQuotedArg(cmdline, sizeof(cmdline), arg)))
	{
		q_strlcpy(error, "command line too long", error_size);
		return false;
	}

	if (!CreateProcessA(exe_path, cmdline, NULL, NULL, FALSE,
		CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, NULL,
		(working_dir && *working_dir) ? working_dir : NULL, &si, &pi))
	{
		q_snprintf(error, error_size, "CreateProcess failed: %lu",
			(unsigned long)GetLastError());
		return false;
	}

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}

qboolean Sys_LaunchUpdateHelper(const char *helper_path,
	const char *helper_arg, const char *manifest_path, char *error,
	size_t error_size)
{
	/* Pass the manifest as a single argv token; the helper splits only argv[1]
	 * and argv[2], so preserve spaces by creating the final command line here. */
	{
		STARTUPINFOEXA si;
		PROCESS_INFORMATION pi;
		HANDLE parent_handle = NULL;
		LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
		SIZE_T attrs_size = 0;
		DWORD create_flags = CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS |
			EXTENDED_STARTUPINFO_PRESENT;
		char cmdline[MAX_OSPATH * 4];
		char helper_dir[MAX_OSPATH];
		char parent_token[32];
		qboolean ok = false;

		memset(&si, 0, sizeof(si));
		memset(&pi, 0, sizeof(pi));
		si.StartupInfo.cb = sizeof(si);
		cmdline[0] = '\0';
		Sys_PathDirName(helper_path, helper_dir, sizeof(helper_dir));

		/*
		 * The game process has pak files open. Do not use broad handle
		 * inheritance here, or the helper inherits those pak handles and then
		 * blocks its own replacement preflight. Restrict inheritance to only
		 * the duplicated parent process wait handle.
		 */
		if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(),
			GetCurrentProcess(), &parent_handle, SYNCHRONIZE, TRUE, 0))
		{
			q_snprintf(error, error_size, "DuplicateHandle failed: %lu",
				(unsigned long)GetLastError());
			return false;
		}
		q_snprintf(parent_token, sizeof(parent_token), "%llu",
			(unsigned long long)(uintptr_t)parent_handle);

		InitializeProcThreadAttributeList(NULL, 1, 0, &attrs_size);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || attrs_size == 0)
		{
			q_snprintf(error, error_size,
				"InitializeProcThreadAttributeList sizing failed: %lu",
				(unsigned long)GetLastError());
			CloseHandle(parent_handle);
			return false;
		}
		attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(),
			0, attrs_size);
		if (!attrs)
		{
			q_strlcpy(error, "unable to allocate process attribute list",
				error_size);
			CloseHandle(parent_handle);
			return false;
		}
		if (!InitializeProcThreadAttributeList(attrs, 1, 0, &attrs_size))
		{
			q_snprintf(error, error_size,
				"InitializeProcThreadAttributeList failed: %lu",
				(unsigned long)GetLastError());
			HeapFree(GetProcessHeap(), 0, attrs);
			CloseHandle(parent_handle);
			return false;
		}
		if (!UpdateProcThreadAttribute(attrs, 0,
			PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &parent_handle,
			sizeof(parent_handle), NULL, NULL))
		{
			q_snprintf(error, error_size,
				"UpdateProcThreadAttribute failed: %lu",
				(unsigned long)GetLastError());
			DeleteProcThreadAttributeList(attrs);
			HeapFree(GetProcessHeap(), 0, attrs);
			CloseHandle(parent_handle);
			return false;
		}
		si.lpAttributeList = attrs;

		if (!Sys_AppendQuotedArg(cmdline, sizeof(cmdline), helper_path) ||
			!Sys_AppendQuotedArg(cmdline, sizeof(cmdline), helper_arg) ||
			!Sys_AppendQuotedArg(cmdline, sizeof(cmdline), manifest_path) ||
			!Sys_AppendQuotedArg(cmdline, sizeof(cmdline), parent_token))
		{
			q_strlcpy(error, "helper command line too long", error_size);
			goto done;
		}

		if (!CreateProcessA(helper_path, cmdline, NULL, NULL, TRUE,
			create_flags, NULL, helper_dir[0] ? helper_dir : NULL,
			&si.StartupInfo, &pi))
		{
			q_snprintf(error, error_size, "CreateProcess failed: %lu",
				(unsigned long)GetLastError());
			goto done;
		}

		ok = true;
		CloseHandle(parent_handle);
		parent_handle = NULL;
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);

done:
		DeleteProcThreadAttributeList(attrs);
		HeapFree(GetProcessHeap(), 0, attrs);
		if (parent_handle)
			CloseHandle(parent_handle);
		if (!ok)
			return false;
	}

	return true;
}

qboolean Sys_LaunchProgram(const char *exe_path, const char *working_dir,
	char *error, size_t error_size)
{
	return Sys_CreateProcessCommand(exe_path, NULL, working_dir, error,
		error_size);
}

qboolean Sys_RunUpdateSelfTest(const char *exe_path, const char *working_dir,
	const char *selftest_arg, unsigned int timeout_ms, char *error,
	size_t error_size)
{
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	char cmdline[MAX_OSPATH * 3];
	DWORD wait_result;
	DWORD exit_code = 1;

	if (error && error_size)
		error[0] = '\0';
	if (!exe_path || !*exe_path || !selftest_arg || !*selftest_arg)
	{
		q_strlcpy(error, "invalid self-test command", error_size);
		return false;
	}

	memset(&si, 0, sizeof(si));
	memset(&pi, 0, sizeof(pi));
	si.cb = sizeof(si);
	cmdline[0] = '\0';

	if (!Sys_AppendQuotedArg(cmdline, sizeof(cmdline), exe_path) ||
		!Sys_AppendQuotedArg(cmdline, sizeof(cmdline), selftest_arg))
	{
		q_strlcpy(error, "self-test command line too long", error_size);
		return false;
	}

	if (!CreateProcessA(exe_path, cmdline, NULL, NULL, FALSE,
		CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, NULL,
		(working_dir && *working_dir) ? working_dir : NULL, &si, &pi))
	{
		q_snprintf(error, error_size, "CreateProcess self-test failed: %lu",
			(unsigned long)GetLastError());
		return false;
	}

	CloseHandle(pi.hThread);
	wait_result = WaitForSingleObject(pi.hProcess, timeout_ms);
	if (wait_result == WAIT_TIMEOUT)
	{
		TerminateProcess(pi.hProcess, 1);
		WaitForSingleObject(pi.hProcess, 1000);
		CloseHandle(pi.hProcess);
		q_strlcpy(error, "self-test timed out", error_size);
		return false;
	}
	if (wait_result != WAIT_OBJECT_0)
	{
		q_snprintf(error, error_size, "self-test wait failed: %lu",
			(unsigned long)GetLastError());
		CloseHandle(pi.hProcess);
		return false;
	}
	if (!GetExitCodeProcess(pi.hProcess, &exit_code))
	{
		q_snprintf(error, error_size, "self-test exit code failed: %lu",
			(unsigned long)GetLastError());
		CloseHandle(pi.hProcess);
		return false;
	}
	CloseHandle(pi.hProcess);

	if (exit_code != 0)
	{
		q_snprintf(error, error_size, "self-test exited with status %lu",
			(unsigned long)exit_code);
		return false;
	}

	return true;
}

qboolean Sys_UpdateWaitForParentExit(uintptr_t wait_token,
	unsigned long fallback_pid, unsigned int timeout_ms)
{
	HANDLE process;
	DWORD result;

	if (wait_token)
	{
		process = (HANDLE)wait_token;
		result = WaitForSingleObject(process, timeout_ms);
		CloseHandle(process);
		return result == WAIT_OBJECT_0;
	}

	if (!fallback_pid)
		return true;

	process = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)fallback_pid);
	if (!process)
		return GetLastError() == ERROR_INVALID_PARAMETER;

	result = WaitForSingleObject(process, timeout_ms);
	CloseHandle(process);
	return result == WAIT_OBJECT_0;
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

qboolean Sys_GetFileTime (const char *path, time_t *out)
{
	HANDLE		handle;
	FILETIME	filetime;
	qboolean	ret;

	handle = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (handle == INVALID_HANDLE_VALUE)
		return false;

	ret = (GetFileTime(handle, NULL, NULL, &filetime) != FALSE);
	CloseHandle(handle);

	if (ret)
	{
		LARGE_INTEGER li;
		li.LowPart = filetime.dwLowDateTime;
		li.HighPart = filetime.dwHighDateTime;
		*out = (time_t)(li.QuadPart / 10000000LL - 11644473600LL);
	}

	return ret;
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

	if (type == FS_ENT_FILE)
	{
		STARTUPINFOA si;
		PROCESS_INFORMATION pi;
		char command_line[MAX_OSPATH * 2 + 64];

		memset(&si, 0, sizeof(si));
		memset(&pi, 0, sizeof(pi));
		si.cb = sizeof(si);
		if ((size_t)q_snprintf(command_line, sizeof(command_line),
			"explorer.exe /select,\"%s\"", path) < sizeof(command_line) &&
			CreateProcessA(NULL, command_line, NULL, NULL, FALSE, 0, NULL, NULL,
				&si, &pi))
		{
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			return true;
		}
	}

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
	char executable[sizeof(cwd)];
	DWORD length;
	(void)argv0;

	if (Sys_GetExecutablePath(executable, sizeof(executable)))
	{
		Sys_PathDirName(executable, dst, dstsize);
		if (dst[0]) return;
	}
	/* Preserve startup on unusually long/unsupported executable paths.  This is
	 * only a failure fallback; normal launches always use the executable dir. */
	length = GetCurrentDirectory((DWORD)dstsize, dst);
	if (length == 0 || (size_t)length >= dstsize)
		Sys_Error ("Couldn't determine executable or current directory");
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

#if defined(USE_SDL2)
	taskbar_shutdown = true;
	Sys_ShutdownTaskbarShell();
#endif

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
#if defined(USE_SDL2)
	taskbar_shutdown = true;
	Sys_ShutdownTaskbarShell();
#endif

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
	return SDL_GetPerformanceCounter() / (long double)SDL_GetPerformanceFrequency();
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

#if defined(USE_SDL2)
static void Sys_UnloadTaskbarOle32(void)
{
	taskbar_CoUninitialize = NULL;

	if (taskbar_ole32)
	{
		FreeLibrary(taskbar_ole32);
		taskbar_ole32 = NULL;
	}
}

static HWND Sys_TaskbarWindow(void)
{
	SDL_Window *window;
	SDL_SysWMinfo wmInfo;

	if (isDedicated)
		return NULL;

	window = (SDL_Window *)VID_GetWindow();
	if (!window)
		return NULL;

	SDL_VERSION(&wmInfo.version);
	if (!SDL_GetWindowWMInfo(window, &wmInfo))
		return NULL;

	return wmInfo.info.win.window;
}

static UINT Sys_TaskbarWindowDpi(HWND hwnd)
{
	HMODULE user32;
	qss_GetDpiForWindow_f pGetDpiForWindow;
	HDC dc;
	UINT dpi = 96;

	user32 = GetModuleHandleW(L"user32.dll");
	pGetDpiForWindow = user32
		? (qss_GetDpiForWindow_f)GetProcAddress(user32, "GetDpiForWindow")
		: NULL;
	if (pGetDpiForWindow)
		dpi = pGetDpiForWindow(hwnd);
	else
	{
		dc = GetDC(hwnd);
		if (dc)
		{
			dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
			ReleaseDC(hwnd, dc);
		}
	}

	return dpi ? dpi : 96;
}

static int Sys_TaskbarOverlaySize(HWND hwnd, UINT requested_dpi)
{
	HMODULE user32;
	qss_GetSystemMetricsForDpi_f pGetSystemMetricsForDpi;
	UINT dpi;
	int size;

	dpi = requested_dpi ? requested_dpi : Sys_TaskbarWindowDpi(hwnd);
	user32 = GetModuleHandleW(L"user32.dll");
	pGetSystemMetricsForDpi = user32
		? (qss_GetSystemMetricsForDpi_f)GetProcAddress(user32,
			"GetSystemMetricsForDpi")
		: NULL;
	size = pGetSystemMetricsForDpi
		? pGetSystemMetricsForDpi(SM_CXSMICON, dpi)
		: MulDiv(16, (int)dpi, 96);

	/* A shell overlay is nominally 16 logical pixels. Keep malformed DPI or
	   accessibility settings from turning a notification into a huge bitmap. */
	if (size < 16)
		size = 16;
	else if (size > 64)
		size = 64;
	return size;
}

static HBITMAP Sys_CreateTaskbarDIB(int width, int height, void **pixels)
{
	BITMAPINFO bitmap_info;

	memset(&bitmap_info, 0, sizeof(bitmap_info));
	bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
	bitmap_info.bmiHeader.biWidth = width;
	bitmap_info.bmiHeader.biHeight = -height; /* top-down, like the text layout */
	bitmap_info.bmiHeader.biPlanes = 1;
	bitmap_info.bmiHeader.biBitCount = 32;
	bitmap_info.bmiHeader.biCompression = BI_RGB;

	return CreateDIBSection(NULL, &bitmap_info, DIB_RGB_COLORS, pixels, NULL, 0);
}

static HICON Sys_CreateTaskbarNotificationIcon(HWND hwnd, unsigned int count,
	UINT requested_dpi)
{
	enum { supersample = 4 };
	const int size = Sys_TaskbarOverlaySize(hwnd, requested_dpi);
	const int work_size = size * supersample;
	HDC work_dc = NULL;
	HBITMAP work_bitmap = NULL;
	HBITMAP color_bitmap = NULL;
	HBITMAP mask_bitmap = NULL;
	HGDIOBJ old_work_bitmap = NULL;
	HGDIOBJ old_font = NULL;
	HFONT font = NULL;
	HICON icon = NULL;
	ICONINFO info;
	RECT text_rect;
	WCHAR label[16];
	DWORD *work_pixels = NULL;
	DWORD *color_pixels = NULL;
	BYTE mask_bits[512];
	int font_height;
	int radius;
	int inner_radius;
	int x, y, sx, sy;

	work_dc = CreateCompatibleDC(NULL);
	work_bitmap = Sys_CreateTaskbarDIB(work_size, work_size,
		(void **)&work_pixels);
	color_bitmap = Sys_CreateTaskbarDIB(size, size, (void **)&color_pixels);
	memset(mask_bits, 0, sizeof(mask_bits));
	mask_bitmap = CreateBitmap(size, size, 1, 1, mask_bits);
	if (!work_dc || !work_bitmap || !color_bitmap || !mask_bitmap ||
		!work_pixels || !color_pixels)
		goto cleanup;

	old_work_bitmap = SelectObject(work_dc, work_bitmap);
	if (!old_work_bitmap)
		goto cleanup;

	/* Render four times larger and downsample. This keeps the circle, alpha
	   edge, and glyphs clean even at fractional taskbar scales. The subtle
	   top-to-bottom red shading gives the badge shape without visual noise. */
	radius = (work_size - supersample) / 2;
	inner_radius = radius - supersample;
	for (y = 0; y < work_size; y++)
	{
		const int dy = y * 2 + 1 - work_size;
		for (x = 0; x < work_size; x++)
		{
			const int dx = x * 2 + 1 - work_size;
			const int distance_squared = dx * dx + dy * dy;
			const int radius_squared = (radius * 2) * (radius * 2);
			const int inner_squared = (inner_radius * 2) * (inner_radius * 2);
			BYTE red, green, blue;

			if (distance_squared > radius_squared)
			{
				work_pixels[y * work_size + x] = 0;
				continue;
			}

			if (distance_squared > inner_squared)
			{
				red = 174;
				green = 22;
				blue = 38;
			}
			else
			{
				red = (BYTE)(224 - (38 * y) / work_size);
				green = (BYTE)(48 - (20 * y) / work_size);
				blue = (BYTE)(62 - (14 * y) / work_size);
			}
			work_pixels[y * work_size + x] =
				0xff000000u | ((DWORD)red << 16) |
				((DWORD)green << 8) | blue;
		}
	}

	if (count > 99)
	{
		lstrcpyW(label, L"99+");
		font_height = -(work_size * 43) / 100;
	}
	else
	{
		wsprintfW(label, L"%u", count);
		font_height = count > 9
			? -(work_size * 57) / 100
			: -(work_size * 73) / 100;
	}

	font = CreateFontW(font_height, 0, 0, 0, 600, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Semibold");
	old_font = SelectObject(work_dc,
		font ? (HGDIOBJ)font : GetStockObject(DEFAULT_GUI_FONT));
	SetBkMode(work_dc, TRANSPARENT);
	SetTextColor(work_dc, RGB(255, 255, 255));
	SetRect(&text_rect, 0, 0, work_size, work_size);
	DrawTextW(work_dc, label, -1, &text_rect,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	if (old_font)
	{
		SelectObject(work_dc, old_font);
		old_font = NULL;
	}

	/* GDI text drawing does not reliably preserve the unused alpha byte.
	   Restore circle alpha, then box-filter into the actual DPI-sized icon. */
	for (y = 0; y < work_size; y++)
	{
		const int dy = y * 2 + 1 - work_size;
		for (x = 0; x < work_size; x++)
		{
			const int dx = x * 2 + 1 - work_size;
			DWORD *pixel = &work_pixels[y * work_size + x];
			if (dx * dx + dy * dy <= (radius * 2) * (radius * 2))
				*pixel |= 0xff000000u;
			else
				*pixel = 0;
		}
	}

	for (y = 0; y < size; y++)
	{
		for (x = 0; x < size; x++)
		{
			unsigned int alpha = 0, red = 0, green = 0, blue = 0;

			for (sy = 0; sy < supersample; sy++)
			{
				for (sx = 0; sx < supersample; sx++)
				{
					DWORD pixel = work_pixels[
						(y * supersample + sy) * work_size +
						x * supersample + sx];
					unsigned int sample_alpha = pixel >> 24;
					alpha += sample_alpha;
					red += ((pixel >> 16) & 255) * sample_alpha;
					green += ((pixel >> 8) & 255) * sample_alpha;
					blue += (pixel & 255) * sample_alpha;
				}
			}

			if (alpha)
			{
				const unsigned int samples = supersample * supersample;
				const unsigned int divisor = samples * 255;
				color_pixels[y * size + x] =
					((DWORD)(alpha / samples) << 24) |
					((DWORD)(red / divisor) << 16) |
					((DWORD)(green / divisor) << 8) |
					(DWORD)(blue / divisor);
			}
			else
				color_pixels[y * size + x] = 0;
		}
	}

	memset(&info, 0, sizeof(info));
	info.fIcon = TRUE;
	info.hbmColor = color_bitmap;
	info.hbmMask = mask_bitmap;
	icon = CreateIconIndirect(&info);

cleanup:
	if (work_dc && old_font)
		SelectObject(work_dc, old_font);
	if (work_dc && old_work_bitmap)
		SelectObject(work_dc, old_work_bitmap);
	if (font)
		DeleteObject(font);
	if (work_bitmap)
		DeleteObject(work_bitmap);
	if (color_bitmap)
		DeleteObject(color_bitmap);
	if (mask_bitmap)
		DeleteObject(mask_bitmap);
	if (work_dc)
		DeleteDC(work_dc);

	return icon;
}

static void Sys_ApplyTaskbarNotificationBadge(HWND hwnd, UINT requested_dpi)
{
	HICON icon;
	WCHAR description[64];
	HRESULT hr;

	if (!taskbar_list || !hwnd || !taskbar_notification_count)
		return;

	icon = Sys_CreateTaskbarNotificationIcon(hwnd, taskbar_notification_count,
		requested_dpi);
	if (!icon)
		return;

	if (taskbar_notification_count > 99)
		lstrcpyW(description, L"99 or more unread notifications");
	else if (taskbar_notification_count == 1)
		lstrcpyW(description, L"1 unread notification");
	else
		wsprintfW(description, L"%u unread notifications",
			taskbar_notification_count);

	hr = taskbar_list->lpVtbl->SetOverlayIcon(taskbar_list, hwnd, icon, description);
	DestroyIcon(icon); /* SetOverlayIcon keeps its own copy. */
	if (SUCCEEDED(hr))
		taskbar_notification_hwnd = hwnd;
}

static void Sys_ApplyTaskbarProgress(HWND hwnd)
{
	if (!taskbar_list || !hwnd || taskbar_progress_state == QSS_TBPF_NOPROGRESS)
		return;

	taskbar_list->lpVtbl->SetProgressState(taskbar_list, hwnd, taskbar_progress_state);
	if (taskbar_progress_state == QSS_TBPF_NORMAL)
		taskbar_list->lpVtbl->SetProgressValue(taskbar_list, hwnd,
			taskbar_progress_completed, taskbar_progress_total);
	taskbar_progress_hwnd = hwnd;
}

static void SDLCALL Sys_TaskbarMessageHook(void *userdata, void *window,
	unsigned int message, Uint64 wparam, Sint64 lparam)
{
	(void)userdata;
	(void)lparam;

	if (message == taskbar_button_created_message)
	{
		/* Explorer discards overlays and progress when it restarts.
		   TaskbarButtonCreated means this window's replacement taskbar button
		   is ready for the saved count and progress state to be reapplied. */
		taskbar_notification_hwnd = NULL;
		taskbar_progress_hwnd = NULL;
		Sys_ApplyTaskbarNotificationBadge((HWND)window, 0);
		Sys_ApplyTaskbarProgress((HWND)window);
	}
	else if (message == WM_DPICHANGED && taskbar_notification_count)
	{
		/* Moving between differently scaled monitors changes the ideal HICON
		   dimensions. Re-render instead of asking Windows to stretch it. */
		taskbar_notification_hwnd = NULL;
		Sys_ApplyTaskbarNotificationBadge((HWND)window, LOWORD(wparam));
	}
}

static qboolean Sys_InitTaskbarShell(void)
{
	qss_CoInitializeEx_f pCoInitializeEx;
	qss_CoCreateInstance_f pCoCreateInstance;
	HRESULT hr;

	if (taskbar_shutdown)
		return false;

	if (taskbar_init_attempted)
		return taskbar_list != NULL;
	taskbar_init_attempted = true;

	taskbar_ole32 = LoadLibraryA("ole32.dll");
	if (!taskbar_ole32)
		return false;

	pCoInitializeEx = (qss_CoInitializeEx_f)GetProcAddress(taskbar_ole32, "CoInitializeEx");
	taskbar_CoUninitialize = (qss_CoUninitialize_f)GetProcAddress(taskbar_ole32, "CoUninitialize");
	pCoCreateInstance = (qss_CoCreateInstance_f)GetProcAddress(taskbar_ole32, "CoCreateInstance");
	if (!pCoInitializeEx || !taskbar_CoUninitialize || !pCoCreateInstance)
	{
		Sys_UnloadTaskbarOle32();
		return false;
	}

	hr = pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	if (SUCCEEDED(hr))
		taskbar_com_initialized = true;
	else if (hr != RPC_E_CHANGED_MODE)
	{
		Sys_UnloadTaskbarOle32();
		return false;
	}

	hr = pCoCreateInstance(&qss_CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER,
		&qss_IID_ITaskbarList3, (LPVOID *)&taskbar_list);
	if (FAILED(hr) || !taskbar_list)
	{
		Sys_ShutdownTaskbarShell();
		taskbar_init_attempted = true;
		return false;
	}

	hr = taskbar_list->lpVtbl->HrInit(taskbar_list);
	if (FAILED(hr))
	{
		Sys_ShutdownTaskbarShell();
		taskbar_init_attempted = true;
		return false;
	}

	taskbar_button_created_message = RegisterWindowMessageW(L"TaskbarButtonCreated");
	if (taskbar_button_created_message)
	{
		SDL_SetWindowsMessageHook(Sys_TaskbarMessageHook, NULL);
		taskbar_message_hook_installed = true;
	}

	return true;
}

static void Sys_ShutdownTaskbarShell(void)
{
	if (taskbar_message_hook_installed)
	{
		SDL_SetWindowsMessageHook(NULL, NULL);
		taskbar_message_hook_installed = false;
	}
	taskbar_button_created_message = 0;

	if (taskbar_list)
	{
		if (taskbar_notification_hwnd)
			taskbar_list->lpVtbl->SetOverlayIcon(taskbar_list,
				taskbar_notification_hwnd, NULL, NULL);
		if (taskbar_progress_hwnd)
			taskbar_list->lpVtbl->SetProgressState(taskbar_list, taskbar_progress_hwnd, QSS_TBPF_NOPROGRESS);
		taskbar_list->lpVtbl->Release(taskbar_list);
		taskbar_list = NULL;
	}

	taskbar_progress_hwnd = NULL;
	taskbar_notification_hwnd = NULL;
	taskbar_notification_count = 0;
	taskbar_progress_state = QSS_TBPF_NOPROGRESS;
	taskbar_progress_completed = 0;
	taskbar_init_attempted = false;

	if (taskbar_com_initialized && taskbar_CoUninitialize)
	{
		taskbar_CoUninitialize();
		taskbar_com_initialized = false;
	}

	Sys_UnloadTaskbarOle32();
}
#endif

void Sys_SetDockProgress (float fraction, int port_probe)
{
	(void)port_probe;

#if defined(USE_SDL2)
	HWND hwnd;
	ULONGLONG completed;
	const ULONGLONG total = taskbar_progress_total;

	if (isDedicated)
		return;

	if (fraction < 0.0f && !taskbar_list)
		return;

	if (!Sys_InitTaskbarShell())
		return;

	hwnd = Sys_TaskbarWindow();
	if (!hwnd && fraction < 0.0f)
		hwnd = taskbar_progress_hwnd;
	if (!hwnd)
		return;

	if (fraction < 0.0f)
	{
		taskbar_list->lpVtbl->SetProgressState(taskbar_list, hwnd, QSS_TBPF_NOPROGRESS);
		taskbar_progress_state = QSS_TBPF_NOPROGRESS;
		taskbar_progress_hwnd = NULL;
		return;
	}

	taskbar_progress_hwnd = hwnd;

	if (fraction <= 0.0f)
	{
		taskbar_list->lpVtbl->SetProgressState(taskbar_list, hwnd, QSS_TBPF_INDETERMINATE);
		taskbar_progress_state = QSS_TBPF_INDETERMINATE;
		return;
	}

	if (fraction > 1.0f)
		fraction = 1.0f;

	completed = (ULONGLONG)(fraction * (float)total + 0.5f);
	if (completed < 1)
		completed = 1;

	taskbar_list->lpVtbl->SetProgressState(taskbar_list, hwnd, QSS_TBPF_NORMAL);
	taskbar_list->lpVtbl->SetProgressValue(taskbar_list, hwnd, completed, total);
	taskbar_progress_state = QSS_TBPF_NORMAL;
	taskbar_progress_completed = completed;
#else
	(void)fraction;
#endif
}

void Sys_IncrementDockNotificationBadge (void)
{
#if defined(USE_SDL2)
	HWND hwnd;

	if (isDedicated)
		return;

	/* count first: the badge is unread state, so a notification that arrives
	   before there is a window must not be lost. TaskbarButtonCreated and the
	   next notification both reapply whatever has accumulated. */
	if (taskbar_notification_count < UINT_MAX)
		taskbar_notification_count++;

	if (!Sys_InitTaskbarShell())
		return;

	hwnd = Sys_TaskbarWindow();
	if (!hwnd)
		return;

	Sys_ApplyTaskbarNotificationBadge(hwnd, 0);
#endif
}

void Sys_ClearDockNotificationBadge (void)
{
#if defined(USE_SDL2)
	HWND hwnd;

	taskbar_notification_count = 0;
	if (!taskbar_list)
	{
		taskbar_notification_hwnd = NULL;
		return;
	}

	hwnd = Sys_TaskbarWindow();
	if (!hwnd)
		hwnd = taskbar_notification_hwnd;
	if (hwnd)
		taskbar_list->lpVtbl->SetOverlayIcon(taskbar_list, hwnd, NULL, NULL);
	taskbar_notification_hwnd = NULL;
#endif
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
