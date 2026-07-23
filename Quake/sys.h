/*
Copyright (C) 1996-2001 Id Software, Inc.
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

#ifndef _QUAKE_SYS_H
#define _QUAKE_SYS_H

// sys.h -- non-portable functions

void Sys_Init (void);

//
// file IO
//

// returns the file size or -1 if file is not present.
// the file should be in BINARY mode for stupid OSs that care
qofs_t Sys_FileOpenRead (const char *path, int *hndl);
int Sys_FileOpenWrite (const char *path);
int Sys_FileOpenStdio (FILE *file);

void Sys_FileClose (int handle);
void Sys_FileSeek (int handle, qofs_t position);
int Sys_FileRead (int handle, void *dest, int count);
int Sys_FileWrite (int handle,const void *data, int count);
void Sys_mkdir (const char *path);
qboolean Sys_GetFileTime (const char *path, time_t *out);

int Sys_FileType (const char *path);
/* returns an FS entity type, i.e. FS_ENT_FILE or FS_ENT_DIRECTORY.
 * returns FS_ENT_NONE (0) if no such file or directory is present. */

//
// system IO
//
FUNC_NORETURN void Sys_Quit (void);
FUNC_NORETURN void Sys_Error (const char *error, ...) FUNC_PRINTF(1,2);
// an error will cause the entire program to exit
#ifdef __WATCOMC__
#pragma aux Sys_Error aborts;
#pragma aux Sys_Quit aborts;
#endif

void Sys_Printf (const char *fmt, ...) FUNC_PRINTF(1,2);
// send text to the console

qboolean Sys_Explore (const char *path);
// shows path in file browser

double Sys_DoubleTime (void);

const char *Sys_ConsoleInput (void);
void Sys_InstallDedicatedSignalHandlers (void);
qboolean Sys_HasDedicatedQuitRequest (void);

void Sys_Sleep (unsigned long msecs);
// yield for about 'msecs' milliseconds.

void Sys_SendKeyEvents (void);
// Perform Key_Event () callbacks until the input que is empty

int Sys_remove (const char *path);

qboolean Sys_GetExecutablePath(char *out, size_t outsize);
unsigned long Sys_GetProcessId(void);
qboolean Sys_MakeExecutable(const char *path);
qboolean Sys_LaunchUpdateHelper(const char *helper_path,
	const char *helper_arg, const char *manifest_path, char *error,
	size_t error_size);
qboolean Sys_LaunchProgram(const char *exe_path, const char *working_dir,
	char *error, size_t error_size);
qboolean Sys_RunUpdateSelfTest(const char *exe_path, const char *working_dir,
	const char *selftest_arg, unsigned int timeout_ms, char *error,
	size_t error_size);
qboolean Sys_UpdateWaitForParentExit(uintptr_t wait_token,
	unsigned long fallback_pid, unsigned int timeout_ms);

void Sys_SetDockProgress (float fraction);
// Show platform download progress: macOS Dock badge or Windows taskbar strip.
// fraction 0..1 shows progress; a negative value clears it.
// No-op on other platforms or when the shell API is unavailable.

void Sys_IncrementDockNotificationBadge (void);
// Increment the macOS Dock or Windows taskbar notification count. The badge is
// cleared when the game regains focus. No-op on other platforms.
void Sys_ClearDockNotificationBadge (void);

#if defined(_WIN32) // woods #disablecaps via ironwail
void Sys_ActivateKeyFilter (qboolean active);
#endif

#if defined(_WIN32) || defined(__APPLE__)
void Sys_Image_BGRA_To_Clipboard(byte* bmbits, int width, int height, int size); // woods
#endif

#endif	/* _QUAKE_SYS_H */

