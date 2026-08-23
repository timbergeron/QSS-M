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

#ifndef _QUAKE_PLATFORM_H
#define _QUAKE_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

/* platform dependent way to set the window icon */
void PL_SetWindowIcon(void);

/* platform dependent cleanup */
void PL_VID_Shutdown (void);

/* retrieve text from the clipboard (returns Z_Malloc()'ed data) */
char *PL_GetClipboardData (void);

/* retrieve a file path from the clipboard (returns Z_Malloc()'ed data) */
char *PL_GetClipboardFilePath (void);

/* retrieve file paths from the clipboard (returns Z_Malloc()'ed array/data) */
qboolean PL_AddClipboardFilePath (char ***paths, int *count, int *capacity, char *path);
char **PL_GetClipboardFilePaths (int *count);
void PL_FreeClipboardFilePaths (char **paths, int count);

/* show an error dialog */
void PL_ErrorDialog(const char *text);

#if defined(__APPLE__) || defined(PLATFORM_OSX) || defined(PLATFORM_MAC)
/* Forward a raw engine Command-Q transition to the native hold-to-quit UI. */
void PL_CommandQEvent(int down);
#endif

typedef struct
{
	int id;
	const char *text;
} pl_dialog_button_t;

/* Startup-safe native dialogs.  PL_MessageDialog returns a button id. */
int PL_MessageDialog(const char *title, const char *message,
	const pl_dialog_button_t *buttons, int num_buttons,
	int default_button, int cancel_button);
qboolean PL_SelectDirectory(const char *title, const char *initial_path,
	char *result, size_t result_size);

/* Compatibility wrapper for older callers. */
qboolean PL_ConfirmDialog(const char *title, const char *text);

#if defined(_WIN32)
/* Forward a raw engine Ctrl-W transition to the native hold-to-quit UI. */
void PL_ControlWEvent(int down);
#endif

#ifdef __cplusplus
}
#endif

#endif	/* _QUAKE_PLATFORM_H */
