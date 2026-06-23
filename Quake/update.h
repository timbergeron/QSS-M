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

#ifndef QUAKE_UPDATE_H
#define QUAKE_UPDATE_H

#include "quakedef.h"

#define VERSION_GITHUB_RELEASE_URL "https://api.github.com/repos/timbergeron/QSS-M/releases/latest"
#define VERSION_GITHUB_MAX_RESPONSE_BYTES	(8 * 1024 * 1024)

typedef struct
{
	char	*memory;
	size_t	size;
	size_t	max_size;
	qboolean too_large;
} versionhttpmem_t;

typedef qboolean (*m_sha256_cancel_fn_t)(void);

int M_Version_Compare(int l_major, int l_minor, int l_patch,
	int r_major, int r_minor, int r_patch);
int M_Version_ParseTag(const char *tag, int *major, int *minor, int *patch);
qboolean M_Version_GitHubHttpGet(const char *url, versionhttpmem_t *mem,
	char *error, size_t errorsz, size_t max_bytes);
qboolean M_Version_ParseTagFull(const char *tag, int *major, int *minor,
	int *patch, char *suffix, size_t suffix_size);
int M_Version_CompareWithSuffix(int l_major, int l_minor, int l_patch,
	const char *l_suffix, qboolean l_prerelease, int r_major, int r_minor,
	int r_patch, const char *r_suffix, qboolean r_prerelease);
int M_Version_CompareToCurrent(int major, int minor, int patch,
	const char *suffix, qboolean prerelease);
int M_Version_CompareTagToCurrent(const char *tag, qboolean prerelease);
void M_Update_CurlOptions(void *curl);

qboolean M_VerifySHA256File(const char *path, const char *expected,
	qboolean allow_empty, m_sha256_cancel_fn_t cancel_fn);

void M_Update_f(void);
void M_Update_Completion_f(const char *partial);
qboolean M_Update_IsSelfTestArg(const char *arg);
qboolean M_Update_IsHelperArg(const char *arg);
int M_UpdateHelperMain(int argc, char **argv);
void M_Update_RecoverAtStartup(void);
void M_Update_ConfirmStartup(void);
void M_Update_PruneStagingAtStartup(void);

#endif /* QUAKE_UPDATE_H */
