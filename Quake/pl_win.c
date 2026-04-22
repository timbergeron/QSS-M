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

char *PL_GetClipboardFilePath (void)
{
	typedef struct
	{
		DWORD pFiles;
		POINT pt;
		BOOL fNC;
		BOOL fWide;
	} pl_dropfiles_t;

	char *data = NULL;

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
					size_t chars = 0;

					while (chars < max_chars && wpath[chars])
						chars++;

					if (chars > 0 && chars < max_chars && chars < (size_t)Q_MAXINT)
					{
						int size = WideCharToMultiByte(CP_UTF8, 0, wpath, (int)chars, NULL, 0, NULL, NULL);
						if (size > 0 && size < Q_MAXINT)
						{
							data = (char *) Z_Malloc(size + 1);
							if (WideCharToMultiByte(CP_UTF8, 0, wpath, (int)chars, data, size, NULL, NULL) == size)
								data[size] = '\0';
							else
							{
								Z_Free(data);
								data = NULL;
							}
						}
					}
				}
				else
				{
					size_t bytes = 0;

					while (bytes < bytes_remaining && files[bytes])
						bytes++;

					if (bytes > 0 && bytes < bytes_remaining && bytes < (size_t)Q_MAXINT)
					{
						data = (char *) Z_Malloc((int)bytes + 1);
						memcpy(data, files, bytes);
						data[bytes] = '\0';
					}
				}
			}
			if (drop != NULL)
				GlobalUnlock(hClipboardData);
		}
		CloseClipboard ();
	}
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
