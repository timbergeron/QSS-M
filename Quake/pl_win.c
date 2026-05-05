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

#include "quakedef.h"
#include <windows.h>
#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#if defined(USE_SDL2)
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#else
#include <SDL/SDL.h>
#include <SDL/SDL_syswm.h>
#endif
#else
#include "SDL.h"
#include "SDL_syswm.h"
#endif

static HICON icon;

void PL_SetWindowIcon (void)
{
	HINSTANCE handle;
	SDL_SysWMinfo wminfo;
	HWND hwnd;

	handle = GetModuleHandle(NULL);
	icon = LoadIcon(handle, "icon");

	if (!icon)
		return;	/* no icon in the exe */

	SDL_VERSION(&wminfo.version);

#if defined(USE_SDL2)
	if (SDL_GetWindowWMInfo((SDL_Window*) VID_GetWindow(), &wminfo) != SDL_TRUE)
		return;	/* wrong SDL version */

	hwnd = wminfo.info.win.window;
#else
	if (SDL_GetWMInfo(&wminfo) != 1)
		return;	/* wrong SDL version */

	hwnd = wminfo.window;
#endif
#ifdef _WIN64
	SetClassLongPtr(hwnd, GCLP_HICON, (LONG_PTR) icon);
#else
	SetClassLong(hwnd, GCL_HICON, (LONG) icon);
#endif
}

void PL_VID_Shutdown (void)
{
	DestroyIcon(icon);
}

#define MAX_CLIPBOARDTXT	MAXCMDLINE	/* 256 */
char *PL_GetClipboardData (void)
{
	char *data = NULL;
	char *cliptext;

	if (OpenClipboard(NULL) != 0)
	{
		HANDLE hClipboardData;

		if ((hClipboardData = GetClipboardData(CF_TEXT)) != NULL)
		{
			cliptext = (char *) GlobalLock(hClipboardData);
			if (cliptext != NULL)
			{
				size_t size = GlobalSize(hClipboardData) + 1;
			/* this is intended for simple small text copies
			 * such as an ip address, etc:  do chop the size
			 * here, otherwise we may experience Z_Malloc()
			 * failures and all other not-oh-so-fun stuff. */
				size = q_min((size_t)(MAX_CLIPBOARDTXT), size);
				data = (char *) Z_Malloc((int)size);
				q_strlcpy (data, cliptext, size);
				GlobalUnlock (hClipboardData);
			}
		}
		CloseClipboard ();
	}
	return data;
}

static char *PL_CopyClipboardPathA(const char *path, size_t bytes)
{
	char *data;

	if (!path || bytes == 0 || bytes >= (size_t)Q_MAXINT)
		return NULL;

	data = (char *) Z_Malloc((int)bytes + 1);
	memcpy(data, path, bytes);
	data[bytes] = '\0';
	return data;
}

static char *PL_CopyClipboardPathW(const WCHAR *path, size_t chars)
{
	char	*data;
	int	size;

	if (!path || chars == 0 || chars >= (size_t)Q_MAXINT)
		return NULL;

	size = WideCharToMultiByte(CP_UTF8, 0, path, (int)chars, NULL, 0, NULL, NULL);
	if (size <= 0 || size >= Q_MAXINT)
		return NULL;

	data = (char *) Z_Malloc(size + 1);
	if (WideCharToMultiByte(CP_UTF8, 0, path, (int)chars, data, size, NULL, NULL) == size)
	{
		data[size] = '\0';
		return data;
	}

	Z_Free(data);
	return NULL;
}

char **PL_GetClipboardFilePaths (int *count)
{
	typedef struct
	{
		DWORD pFiles;
		POINT pt;
		BOOL fNC;
		BOOL fWide;
	} pl_dropfiles_t;

	char **paths = NULL;
	int local_count = 0;
	int local_capacity = 0;

	if (OpenClipboard(NULL) != 0)
	{
		HANDLE hClipboardData;

		if ((hClipboardData = GetClipboardData(CF_HDROP)) != NULL)
		{
			pl_dropfiles_t *drop = (pl_dropfiles_t *) GlobalLock(hClipboardData);
			SIZE_T drop_size = GlobalSize(hClipboardData);

			if (drop != NULL && drop->pFiles >= sizeof(pl_dropfiles_t) && drop->pFiles < drop_size)
			{
				const char *files = (const char *)drop + drop->pFiles;
				SIZE_T bytes_remaining = drop_size - drop->pFiles;

				if (drop->fWide)
				{
					const WCHAR *wpath = (const WCHAR *)files;
					size_t max_chars = bytes_remaining / sizeof(WCHAR);
					size_t pos = 0;

					while (pos < max_chars && wpath[pos])
					{
						size_t start = pos;

						while (pos < max_chars && wpath[pos])
							pos++;
						if (pos >= max_chars)
							break;
						if (pos > start)
						{
							char *path = PL_CopyClipboardPathW(wpath + start, pos - start);
							if (path)
								PL_AddClipboardFilePath(&paths, &local_count, &local_capacity, path);
						}
						pos++;
					}
				}
				else
				{
					size_t pos = 0;

					while (pos < bytes_remaining && files[pos])
					{
						size_t start = pos;

						while (pos < bytes_remaining && files[pos])
							pos++;
						if (pos >= bytes_remaining)
							break;
						if (pos > start)
						{
							char *path = PL_CopyClipboardPathA(files + start, pos - start);
							if (path)
								PL_AddClipboardFilePath(&paths, &local_count, &local_capacity, path);
						}
						pos++;
					}
				}
			}
			if (drop != NULL)
				GlobalUnlock(hClipboardData);
		}
		CloseClipboard ();
	}

	if (count)
		*count = local_count;
	return paths;
}

void PL_FreeClipboardFilePaths (char **paths, int count)
{
	int i;

	if (!paths)
		return;
	for (i = 0; i < count; ++i)
	{
		if (paths[i])
			Z_Free(paths[i]);
	}
	Z_Free(paths);
}

char *PL_GetClipboardFilePath (void)
{
	char **paths;
	char *data = NULL;
	int count = 0;

	paths = PL_GetClipboardFilePaths(&count);
	if (paths && count > 0)
	{
		data = paths[0];
		paths[0] = NULL;
	}
	PL_FreeClipboardFilePaths(paths, count);
	return data;
}

void PL_ErrorDialog(const char *errorMsg)
{
	MessageBox (NULL, errorMsg, "Quake Error",
			MB_OK | MB_SETFOREGROUND | MB_ICONSTOP);
}

qboolean PL_ConfirmDialog(const char *title, const char *text)
{
	int result = MessageBox(NULL, text, title,
		MB_YESNO | MB_SETFOREGROUND | MB_ICONQUESTION | MB_DEFBUTTON1);
	return (result == IDYES) ? true : false;
}
