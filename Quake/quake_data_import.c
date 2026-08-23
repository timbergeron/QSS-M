/*
Copyright (C) 2026 QSS-M contributors

Cross-platform, pre-filesystem import for Quake's base PAK files.  Candidate
PAKs are inspected without mounting them; imported files are staged, validated,
SHA-256 checked against their sources, and installed without replacement.
*/

#include "quakedef.h"
#include "quake_data_import.h"
#include "platform.h"
#include "crc.h"
#include "q_hash.h"
#include "q_ctype.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/mount.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#include <sys/vfs.h>
#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1u << 0)
#endif
#endif
#endif

#define QDI_PAK_ENTRY_SIZE 64u
#define QDI_PAK_NAME_SIZE 56u
#define QDI_MAX_PAK_ENTRIES 4096u
#define QDI_PAK0_COUNT 339u
#define QDI_PAK0_CRC_V100 13900u
#define QDI_PAK0_CRC_V101 62751u
#define QDI_PAK0_CRC_V106 32981u
#define QDI_PICKER_PATH_SIZE 32768u

typedef struct
{
	qboolean valid;
	qboolean recognized_pak0;
	qboolean registered_pak1;
	uint32_t num_entries;
} qdi_pak_info_t;

typedef struct
{
	char *directory;
	char *pak0;
	char *pak1;
	qdi_pak_info_t info0;
	qdi_pak_info_t info1;
} qdi_candidate_t;

typedef struct
{
	char **items;
	int count;
	int capacity;
} qdi_path_list_t;

#ifndef _WIN32
typedef struct
{
	dev_t device;
	ino_t inode;
	qboolean used;
} qdi_visited_entry_t;

typedef struct
{
	qdi_visited_entry_t *entries;
	size_t count;
	size_t capacity;
} qdi_visited_set_t;
#endif

typedef enum
{
	QDI_SEARCH_NOT_FOUND = 0,
	QDI_SEARCH_FOUND = 1,
	QDI_SEARCH_CANCELLED = -1
} qdi_search_result_t;

typedef enum
{
	QDI_SELECT_INVALID = 0,
	QDI_SELECT_FOUND = 1,
	QDI_SELECT_CANCELLED = -1
} qdi_select_result_t;

/* This is the registered-data signature historically used by
 * COM_CheckRegistered.  Keeping it here gives mounted and raw checks one
 * implementation and one byte-order rule. */
static const unsigned short qdi_pop[] =
{
	0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
	0x0000,0x0000,0x6600,0x0000,0x0000,0x0000,0x6600,0x0000,
	0x0000,0x0066,0x0000,0x0000,0x0000,0x0000,0x0067,0x0000,
	0x0000,0x6665,0x0000,0x0000,0x0000,0x0000,0x0065,0x6600,
	0x0063,0x6561,0x0000,0x0000,0x0000,0x0000,0x0061,0x6563,
	0x0064,0x6561,0x0000,0x0000,0x0000,0x0000,0x0061,0x6564,
	0x0064,0x6564,0x0000,0x6469,0x6969,0x6400,0x0064,0x6564,
	0x0063,0x6568,0x6200,0x0064,0x6864,0x0000,0x6268,0x6563,
	0x0000,0x6567,0x6963,0x0064,0x6764,0x0063,0x6967,0x6500,
	0x0000,0x6266,0x6769,0x6a68,0x6768,0x6a69,0x6766,0x6200,
	0x0000,0x0062,0x6566,0x6666,0x6666,0x6666,0x6562,0x0000,
	0x0000,0x0000,0x0062,0x6364,0x6664,0x6362,0x0000,0x0000,
	0x0000,0x0000,0x0000,0x0062,0x6662,0x0000,0x0000,0x0000,
	0x0000,0x0000,0x0000,0x0061,0x6661,0x0000,0x0000,0x0000,
	0x0000,0x0000,0x0000,0x0000,0x6500,0x0000,0x0000,0x0000,
	0x0000,0x0000,0x0000,0x0000,0x6400,0x0000,0x0000,0x0000
};

static unsigned short QDI_BigShort(unsigned short value)
{
	const uint16_t marker = 0x0102;
	if (*(const unsigned char *)&marker == 0x01)
		return value;
	return (unsigned short)((value << 8) | (value >> 8));
}

qboolean QuakeDataImport_ValidatePopData(const void *data, size_t size)
{
	const unsigned short *check = (const unsigned short *)data;
	size_t i;

	if (!data || size != sizeof(qdi_pop))
		return false;
	for (i = 0; i < sizeof(qdi_pop) / sizeof(qdi_pop[0]); ++i)
	{
		unsigned short value;
		memcpy(&value, &check[i], sizeof(value));
		if (qdi_pop[i] != QDI_BigShort(value))
			return false;
	}
	return true;
}

static char *QDI_Strdup(const char *text)
{
	size_t size;
	char *copy;
	if (!text)
		return NULL;
	size = strlen(text) + 1;
	copy = (char *)malloc(size);
	if (copy)
		memcpy(copy, text, size);
	return copy;
}

static char *QDI_Join(const char *base, const char *name)
{
	size_t base_len, name_len, size;
	qboolean slash;
	char *path;

	if (!base || !name)
		return NULL;
	base_len = strlen(base);
	name_len = strlen(name);
	slash = base_len > 0 && base[base_len - 1] != '/' && base[base_len - 1] != '\\';
	if (base_len > SIZE_MAX - name_len - (slash ? 2u : 1u))
		return NULL;
	size = base_len + name_len + (slash ? 2u : 1u);
	path = (char *)malloc(size);
	if (!path)
		return NULL;
	q_snprintf(path, size, slash ? "%s/%s" : "%s%s", base, name);
	return path;
}

static void QDI_TrimPath(char *path)
{
	size_t length;
	char *p;
	if (!path)
		return;
	for (p = path; *p; ++p)
		if (*p == '\\') *p = '/';
	length = strlen(path);
	while (length > 1 && path[length - 1] == '/' &&
		!(length == 3 && path[1] == ':') &&
		!(length == 2 && path[0] == '/' && path[1] == '/'))
		path[--length] = '\0';
}

static const char *QDI_BaseName(const char *path)
{
	const char *slash, *backslash;
	if (!path) return "";
	slash = strrchr(path, '/');
	backslash = strrchr(path, '\\');
	if (!slash || (backslash && backslash > slash)) slash = backslash;
	return slash ? slash + 1 : path;
}

static char *QDI_Parent(const char *path)
{
	char *copy = QDI_Strdup(path);
	char *slash;
	if (!copy) return NULL;
	QDI_TrimPath(copy);
	slash = strrchr(copy, '/');
	if (!slash)
	{
		free(copy);
		return NULL;
	}
	if (slash == copy)
		slash[1] = '\0';
	else
		*slash = '\0';
	return copy;
}

#ifdef _WIN32
static wchar_t *QDI_WidePath(const char *path, qboolean extended)
{
	wchar_t *wide, *result, *normalized, *p;
	UINT codepage = CP_UTF8;
	DWORD flags = MB_ERR_INVALID_CHARS;
	int needed;
	DWORD full_needed;
	qboolean absolute;

	needed = MultiByteToWideChar(codepage, flags, path, -1, NULL, 0);
	/* The Windows engine's basedir is historically supplied in the active code
	 * page, while Steam registry and picker paths enter import as UTF-8. */
	if (needed <= 0)
	{
		codepage = CP_ACP;
		flags = 0;
		needed = MultiByteToWideChar(codepage, flags, path, -1, NULL, 0);
	}
	if (needed <= 0)
		return NULL;
	wide = (wchar_t *)malloc((size_t)needed * sizeof(*wide));
	if (!wide)
		return NULL;
	if (!MultiByteToWideChar(codepage, flags, path, -1, wide, needed))
	{
		free(wide);
		return NULL;
	}
	/* The extended-length namespace does not translate '/' to '\\'.  Normalize
	 * before adding its prefix so every Win32 file API receives NT-style paths. */
	for (p = wide; *p; ++p)
		if (*p == L'/') *p = L'\\';
	absolute = (needed > 3 && wide[1] == L':') ||
		(needed > 2 && wide[0] == L'\\' && wide[1] == L'\\');
	if (!absolute || !wcsncmp(wide, L"\\\\?\\", 4))
		return wide;
	/* The extended namespace does not resolve dot components.  Canonicalize
	 * while the path still has ordinary Win32 semantics, then add the prefix. */
	full_needed = GetFullPathNameW(wide, 0, NULL, NULL);
	if (full_needed > 0)
	{
		normalized = (wchar_t *)malloc((size_t)full_needed * sizeof(*normalized));
		if (!normalized)
		{
			free(wide);
			return NULL;
		}
		if (!GetFullPathNameW(wide, full_needed, normalized, NULL))
		{
			free(normalized);
			free(wide);
			return NULL;
		}
		free(wide);
		wide = normalized;
		needed = (int)wcslen(wide) + 1;
	}
	if (!extended)
		return wide;
	if (wide[0] == L'\\' && wide[1] == L'\\')
	{
		result = (wchar_t *)malloc(((size_t)needed + 7u) * sizeof(*result));
		if (result)
		{
			wcscpy(result, L"\\\\?\\UNC\\");
			wcscat(result, wide + 2);
		}
	}
	else
	{
		result = (wchar_t *)malloc(((size_t)needed + 4u) * sizeof(*result));
		if (result)
		{
			wcscpy(result, L"\\\\?\\");
			wcscat(result, wide);
		}
	}
	free(wide);
	return result;
}

static FILE *QDI_Open(const char *path, const wchar_t *mode)
{
	wchar_t *wide = QDI_WidePath(path, true);
	FILE *file = wide ? _wfopen(wide, mode) : NULL;
	free(wide);
	return file;
}
#else
static FILE *QDI_Open(const char *path, const char *mode)
{
	return fopen(path, mode);
}
#endif

static qboolean QDI_FileExists(const char *path)
{
#ifdef _WIN32
	wchar_t *wide = QDI_WidePath(path, true);
	DWORD attrs = wide ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
	free(wide);
	return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
	struct stat st;
	/* Candidate PAK symlinks are intentional on many Unix installations.  The
	 * recursive search still uses lstat() and refuses symlink traversal. */
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

static qboolean QDI_DirectoryExists(const char *path)
{
#ifdef _WIN32
	wchar_t *wide = QDI_WidePath(path, true);
	DWORD attrs = wide ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
	free(wide);
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static qboolean QDI_HasAlternativeBaseData(const char *directory)
{
	static const char *names[] = {"pak0.pk3", "gfx.wad"};
	size_t i;
	for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
	{
		char *path = QDI_Join(directory, names[i]);
		qboolean exists = path && QDI_FileExists(path);
		free(path);
		if (exists) return true;
	}
	return false;
}

static qboolean QDI_Mkdir(const char *path)
{
#ifdef _WIN32
	wchar_t *wide;
	qboolean result;
	if (QDI_DirectoryExists(path)) return true;
	wide = QDI_WidePath(path, true);
	result = wide && (CreateDirectoryW(wide, NULL) ||
		GetLastError() == ERROR_ALREADY_EXISTS);
	free(wide);
	return result;
#else
	if (QDI_DirectoryExists(path)) return true;
	return mkdir(path, 0777) == 0 || errno == EEXIST;
#endif
}

static qboolean QDI_Remove(const char *path)
{
#ifdef _WIN32
	wchar_t *wide = QDI_WidePath(path, true);
	qboolean result = wide && DeleteFileW(wide);
	free(wide);
	return result;
#else
	return unlink(path) == 0;
#endif
}

static qboolean QDI_CopyToNewFile(const char *source, const char *dest);

static qboolean QDI_InstallNoReplace(const char *source, const char *dest)
{
#ifdef _WIN32
	wchar_t *wsource = QDI_WidePath(source, true);
	wchar_t *wdest = QDI_WidePath(dest, true);
	qboolean result = wsource && wdest && MoveFileW(wsource, wdest);
	free(wsource);
	free(wdest);
	return result;
#else
	/* Prefer an atomic exclusive rename where the host exposes one.  This works
	 * on FAT/exFAT volumes that do not support hard links. */
#if defined(__APPLE__)
	if (renamex_np(source, dest, RENAME_EXCL) == 0)
		return true;
	if (errno == EEXIST)
		return false;
#elif defined(__linux__) && defined(SYS_renameat2)
	if (syscall(SYS_renameat2, AT_FDCWD, source, AT_FDCWD, dest,
		RENAME_NOREPLACE) == 0)
		return true;
	if (errno == EEXIST)
		return false;
#endif
	/* link() is still atomic and broadly portable when supported. */
	if (link(source, dest) != 0)
	{
		int link_error = errno;
		if (link_error == EEXIST)
			return false;
		if (link_error != EPERM && link_error != EXDEV
#ifdef EOPNOTSUPP
			&& link_error != EOPNOTSUPP
#endif
#if defined(ENOTSUP) && (!defined(EOPNOTSUPP) || ENOTSUP != EOPNOTSUPP)
			&& link_error != ENOTSUP
#endif
			)
			return false;
		/* Last-resort portable path: exclusive creation preserves no-replace.
		 * QDI_CopyToNewFile flushes the new file before it becomes success. */
		if (!QDI_CopyToNewFile(source, dest))
			return false;
		if (unlink(source) != 0)
		{
			QDI_Remove(dest);
			return false;
		}
		return true;
	}
	if (unlink(source) != 0)
	{
		unlink(dest);
		return false;
	}
	return true;
#endif
}

static qboolean QDI_Seek(FILE *file, uint64_t offset)
{
#ifdef _WIN32
	return offset <= INT64_MAX && _fseeki64(file, (__int64)offset, SEEK_SET) == 0;
#else
	return offset <= (uint64_t)INT64_MAX && fseeko(file, (off_t)offset, SEEK_SET) == 0;
#endif
}

static qboolean QDI_FileSize(FILE *file, uint64_t *size)
{
#ifdef _WIN32
	__int64 pos;
	if (_fseeki64(file, 0, SEEK_END) != 0 || (pos = _ftelli64(file)) < 0)
		return false;
#else
	off_t pos;
	if (fseeko(file, 0, SEEK_END) != 0 || (pos = ftello(file)) < 0)
		return false;
#endif
	*size = (uint64_t)pos;
	return true;
}

static uint32_t QDI_ReadLE32(const unsigned char *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
		((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static qboolean QDI_EntryNameEquals(const unsigned char *entry, const char *name)
{
	size_t length = strlen(name);
	return length < QDI_PAK_NAME_SIZE && !memcmp(entry, name, length) &&
		entry[length] == '\0';
}

static qboolean QDI_RecognizedPak0Policy(uint32_t entries, unsigned short crc)
{
	return entries == QDI_PAK0_COUNT && (crc == QDI_PAK0_CRC_V100 ||
		crc == QDI_PAK0_CRC_V101 || crc == QDI_PAK0_CRC_V106);
}

static qboolean QDI_InspectPak(const char *path, qdi_pak_info_t *info)
{
	FILE *file = NULL;
	unsigned char header[12];
	unsigned char *directory = NULL;
	uint64_t file_size;
	uint32_t dir_offset, dir_length, entries, i;
	qboolean result = false;

	memset(info, 0, sizeof(*info));
#ifdef _WIN32
	file = QDI_Open(path, L"rb");
#else
	file = QDI_Open(path, "rb");
#endif
	if (!file || !QDI_FileSize(file, &file_size) || file_size < sizeof(header) ||
		!QDI_Seek(file, 0) || fread(header, 1, sizeof(header), file) != sizeof(header) ||
		memcmp(header, "PACK", 4))
		goto done;
	dir_offset = QDI_ReadLE32(header + 4);
	dir_length = QDI_ReadLE32(header + 8);
	/* On-disk fields are signed in the original format. */
	if (dir_offset > INT32_MAX || dir_length > INT32_MAX || dir_length == 0 ||
		dir_length % QDI_PAK_ENTRY_SIZE != 0 || dir_offset > file_size ||
		dir_length > file_size - dir_offset)
		goto done;
	entries = dir_length / QDI_PAK_ENTRY_SIZE;
	if (!entries || entries > QDI_MAX_PAK_ENTRIES)
		goto done;
	directory = (unsigned char *)malloc(dir_length);
	if (!directory || !QDI_Seek(file, dir_offset) ||
		fread(directory, 1, dir_length, file) != dir_length)
		goto done;

	for (i = 0; i < entries; ++i)
	{
		const unsigned char *entry = directory + (size_t)i * QDI_PAK_ENTRY_SIZE;
		uint32_t offset = QDI_ReadLE32(entry + 56);
		uint32_t length = QDI_ReadLE32(entry + 60);
		if (offset > INT32_MAX || length > INT32_MAX || offset > file_size ||
			length > file_size - offset)
			goto done;
	}

	info->valid = true;
	info->num_entries = entries;
	info->recognized_pak0 = QDI_RecognizedPak0Policy(entries,
		CRC_Block(directory, dir_length));
	for (i = 0; i < entries; ++i)
	{
		const unsigned char *entry = directory + (size_t)i * QDI_PAK_ENTRY_SIZE;
		if (QDI_EntryNameEquals(entry, "gfx/pop.lmp"))
		{
			uint32_t offset = QDI_ReadLE32(entry + 56);
			uint32_t length = QDI_ReadLE32(entry + 60);
			unsigned char pop_data[sizeof(qdi_pop)];
			if (length == sizeof(pop_data) && QDI_Seek(file, offset) &&
				fread(pop_data, 1, sizeof(pop_data), file) == sizeof(pop_data) &&
				QuakeDataImport_ValidatePopData(pop_data, sizeof(pop_data)))
				info->registered_pak1 = true;
			break;
		}
	}
	result = true;

done:
	free(directory);
	if (file) fclose(file);
	return result;
}

static void QDI_FreeCandidate(qdi_candidate_t *candidate)
{
	if (!candidate) return;
	free(candidate->directory);
	free(candidate->pak0);
	free(candidate->pak1);
	memset(candidate, 0, sizeof(*candidate));
}

static qboolean QDI_FindNamedFile(const char *directory, const char *name,
	qboolean exact, char **result)
{
	char *path = QDI_Join(directory, name);
	if (path && QDI_FileExists(path))
	{
		*result = path;
		return true;
	}
	free(path);
	if (exact)
		return false;
#ifdef _WIN32
	{
		char *pattern = QDI_Join(directory, "*");
		wchar_t *wpattern = pattern ? QDI_WidePath(pattern, true) : NULL;
		WIN32_FIND_DATAW data;
		HANDLE find = wpattern ? FindFirstFileW(wpattern, &data) : INVALID_HANDLE_VALUE;
		free(pattern); free(wpattern);
		if (find != INVALID_HANDLE_VALUE)
		{
			do
			{
				char utf8[1024];
				if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
					WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, utf8,
						sizeof(utf8), NULL, NULL) > 0 && !q_strcasecmp(utf8, name))
				{
					*result = QDI_Join(directory, utf8);
					FindClose(find);
					return *result != NULL;
				}
			} while (FindNextFileW(find, &data));
			FindClose(find);
		}
	}
#else
	{
		DIR *dir = opendir(directory);
		struct dirent *entry;
		if (dir)
		{
			while ((entry = readdir(dir)) != NULL)
			{
				if (!q_strcasecmp(entry->d_name, name))
				{
					path = QDI_Join(directory, entry->d_name);
					if (path && QDI_FileExists(path))
					{
						*result = path;
						closedir(dir);
						return true;
					}
					free(path);
				}
			}
			closedir(dir);
		}
	}
#endif
	return false;
}

static char *QDI_FindPak(const char *id1dir, const char *name, qboolean exact)
{
	char *result = NULL;
	char *paks;
	if (QDI_FindNamedFile(id1dir, name, exact, &result))
		return result;
	paks = QDI_Join(id1dir, "paks");
	if (paks)
	{
		QDI_FindNamedFile(paks, name, exact, &result);
		free(paks);
	}
	return result;
}

static qboolean QDI_InspectDirectory(const char *directory, qboolean exact,
	qdi_candidate_t *candidate)
{
	memset(candidate, 0, sizeof(*candidate));
	candidate->directory = QDI_Strdup(directory);
	candidate->pak0 = QDI_FindPak(directory, "pak0.pak", exact);
	candidate->pak1 = QDI_FindPak(directory, "pak1.pak", exact);
	if (candidate->pak0)
		QDI_InspectPak(candidate->pak0, &candidate->info0);
	if (candidate->pak1)
		QDI_InspectPak(candidate->pak1, &candidate->info1);
	return candidate->info0.valid && candidate->info1.valid;
}

static qboolean QDI_CandidateRecognized(const qdi_candidate_t *candidate)
{
	return candidate->info0.recognized_pak0 &&
		candidate->info1.registered_pak1;
}

static void QDI_PathListFree(qdi_path_list_t *list)
{
	int i;
	for (i = 0; i < list->count; ++i) free(list->items[i]);
	free(list->items);
	memset(list, 0, sizeof(*list));
}

static qboolean QDI_PathListAdd(qdi_path_list_t *list, const char *path)
{
	int i;
	char **items;
	char *copy;
	if (!path || !path[0]) return false;
	copy = QDI_Strdup(path);
	if (!copy) return false;
	QDI_TrimPath(copy);
	for (i = 0; i < list->count; ++i)
		if (!strcmp(list->items[i], copy))
		{
			free(copy);
			return true;
		}
	if (list->count == list->capacity)
	{
		int capacity = list->capacity ? list->capacity * 2 : 16;
		items = (char **)realloc(list->items, (size_t)capacity * sizeof(*items));
		if (!items) { free(copy); return false; }
		list->items = items;
		list->capacity = capacity;
	}
	list->items[list->count++] = copy;
	return true;
}

/* Search traversal does not need the O(n) string dedupe used by the small
 * Steam/source lists.  POSIX cycle protection is handled by inode identity. */
static qboolean QDI_PathStackPush(qdi_path_list_t *stack, const char *path)
{
	char **items;
	char *copy;
	if (!path || !path[0]) return false;
	if (stack->count == stack->capacity)
	{
		int capacity = stack->capacity ? stack->capacity * 2 : 64;
		items = (char **)realloc(stack->items,
			(size_t)capacity * sizeof(*items));
		if (!items) return false;
		stack->items = items;
		stack->capacity = capacity;
	}
	copy = QDI_Strdup(path);
	if (!copy) return false;
	QDI_TrimPath(copy);
	stack->items[stack->count++] = copy;
	return true;
}

#ifndef _WIN32
static size_t QDI_VisitedHash(dev_t device, ino_t inode)
{
	uint64_t value = (uint64_t)device * UINT64_C(11400714819323198485) ^
		(uint64_t)inode;
	value ^= value >> 33;
	value *= UINT64_C(0xff51afd7ed558ccd);
	value ^= value >> 33;
	return (size_t)value;
}

static qboolean QDI_VisitedInsertRaw(qdi_visited_set_t *set, dev_t device,
	ino_t inode)
{
	size_t mask = set->capacity - 1;
	size_t slot = QDI_VisitedHash(device, inode) & mask;
	while (set->entries[slot].used)
	{
		if (set->entries[slot].device == device &&
			set->entries[slot].inode == inode)
			return false;
		slot = (slot + 1) & mask;
	}
	set->entries[slot].device = device;
	set->entries[slot].inode = inode;
	set->entries[slot].used = true;
	++set->count;
	return true;
}

static qboolean QDI_VisitedAdd(qdi_visited_set_t *set, dev_t device, ino_t inode)
{
	if (!set->capacity || (set->count + 1) * 10 >= set->capacity * 7)
	{
		qdi_visited_entry_t *old = set->entries;
		size_t old_capacity = set->capacity;
		size_t i;
		set->capacity = old_capacity ? old_capacity * 2 : 256;
		set->entries = (qdi_visited_entry_t *)calloc(set->capacity,
			sizeof(*set->entries));
		if (!set->entries)
		{
			set->entries = old;
			set->capacity = old_capacity;
			return false;
		}
		set->count = 0;
		for (i = 0; i < old_capacity; ++i)
			if (old[i].used)
				QDI_VisitedInsertRaw(set, old[i].device, old[i].inode);
		free(old);
	}
	return QDI_VisitedInsertRaw(set, device, inode);
}
#endif

static const char *QDI_ParseQuoted(const char *input, char **output)
{
	const char *p;
	char *value, *dst;
	size_t capacity;
	if (!input || *input != '"') return NULL;
	capacity = strlen(input) + 1;
	value = (char *)malloc(capacity);
	if (!value) return NULL;
	dst = value;
	for (p = input + 1; *p && *p != '"'; ++p)
	{
		if (*p == '\\' && p[1]) ++p;
		*dst++ = *p;
	}
	if (*p != '"')
	{
		free(value);
		return NULL;
	}
	*dst = '\0';
	*output = value;
	return p + 1;
}

static qboolean QDI_Numeric(const char *text)
{
	if (!text || !*text) return false;
	while (*text) if (!q_isdigit(*text++)) return false;
	return true;
}

static void QDI_ParseVDF(const char *path, qdi_path_list_t *libraries)
{
	FILE *file;
	uint64_t length;
	char *text = NULL;
	const char *p;
#ifdef _WIN32
	file = QDI_Open(path, L"rb");
#else
	file = QDI_Open(path, "rb");
#endif
	if (!file || !QDI_FileSize(file, &length) || length == 0 ||
		length > 4u * 1024u * 1024u || !QDI_Seek(file, 0))
		goto done;
	text = (char *)malloc((size_t)length + 1);
	if (!text || fread(text, 1, (size_t)length, file) != (size_t)length)
		goto done;
	text[length] = '\0';
	p = text;
	while (*p)
	{
		char *key = NULL, *value = NULL;
		const char *next;
		while (*p && *p != '"') ++p;
		if (!*p) break;
		next = QDI_ParseQuoted(p, &key);
		if (!next) break;
		p = next;
		while (*p && q_isspace(*p)) ++p;
		if (*p == '"')
		{
			next = QDI_ParseQuoted(p, &value);
			if (!next) { free(key); break; }
			p = next;
			if ((!q_strcasecmp(key, "path") || QDI_Numeric(key)) &&
				(strchr(value, '/') || strchr(value, '\\') || strchr(value, ':')))
				QDI_PathListAdd(libraries, value);
		}
		free(key);
		free(value);
	}
done:
	free(text);
	if (file) fclose(file);
}

#ifdef _WIN32
static void QDI_AddRegistrySteamRoot(HKEY root, const char *subkey,
	const wchar_t *value, qdi_path_list_t *roots)
{
	HKEY key;
	DWORD type = 0, bytes = 0;
	wchar_t *wide = NULL;
	char *utf8 = NULL;
	if (RegOpenKeyExA(root, subkey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
		return;
	if (RegQueryValueExW(key, value, NULL, &type, NULL, &bytes) == ERROR_SUCCESS &&
		(type == REG_SZ || type == REG_EXPAND_SZ) && bytes >= sizeof(wchar_t))
	{
		wide = (wchar_t *)malloc(bytes + sizeof(wchar_t));
		if (wide && RegQueryValueExW(key, value,
			NULL, &type, (LPBYTE)wide, &bytes) == ERROR_SUCCESS)
		{
			int needed;
			wide[bytes / sizeof(wchar_t)] = L'\0';
			needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
			utf8 = needed > 0 ? (char *)malloc((size_t)needed) : NULL;
			if (utf8 && WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8,
				needed, NULL, NULL) > 0)
				QDI_PathListAdd(roots, utf8);
		}
	}
	free(utf8); free(wide); RegCloseKey(key);
}
#endif

static void QDI_AddSteamRoots(qdi_path_list_t *roots)
{
	char *path;
#ifdef _WIN32
	const char *pf;
	QDI_AddRegistrySteamRoot(HKEY_CURRENT_USER, "Software\\Valve\\Steam",
		L"SteamPath", roots);
	QDI_AddRegistrySteamRoot(HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam",
		L"InstallPath", roots);
	QDI_AddRegistrySteamRoot(HKEY_LOCAL_MACHINE,
		"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath", roots);
	pf = getenv("ProgramFiles(x86)");
	if (pf && (path = QDI_Join(pf, "Steam")) != NULL)
		{ QDI_PathListAdd(roots, path); free(path); }
	pf = getenv("ProgramFiles");
	if (pf && (path = QDI_Join(pf, "Steam")) != NULL)
		{ QDI_PathListAdd(roots, path); free(path); }
	QDI_PathListAdd(roots, "C:/Program Files (x86)/Steam");
	QDI_PathListAdd(roots, "C:/Program Files/Steam");
#elif defined(__APPLE__)
	const char *home = getenv("HOME");
	if (home && (path = QDI_Join(home, "Library/Application Support/Steam")) != NULL)
		{ QDI_PathListAdd(roots, path); free(path); }
#else
	const char *home = getenv("HOME");
	static const char *suffixes[] = {
		".local/share/Steam", ".steam/steam",
		".var/app/com.valvesoftware.Steam/.local/share/Steam",
		"snap/steam/common/.local/share/Steam"
	};
	size_t i;
	if (home) for (i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
		if ((path = QDI_Join(home, suffixes[i])) != NULL)
			{ QDI_PathListAdd(roots, path); free(path); }
#endif
}

static qboolean QDI_TryCandidateDirectory(const char *path,
	qdi_candidate_t *candidate)
{
	qdi_candidate_t test;
	if (!path) return false;
	QDI_InspectDirectory(path, false, &test);
	if (test.info0.valid && test.info1.valid)
	{
		*candidate = test;
		return true;
	}
	QDI_FreeCandidate(&test);
	return false;
}

/* Builds which previously enabled per-user storage may have left the user's
 * PAKs there.  Treat that folder only as an import source: successful import
 * always copies into the self-contained id1 beside the executable/app. */
static qboolean QDI_FindLegacyUserCandidate(qdi_candidate_t *candidate)
{
#ifdef _WIN32
	(void)candidate;
	return false;
#else
	const char *home = getenv("HOME");
	const char *suffix;
	char *path;
	qboolean found;

	if (!home || !home[0])
		return false;
#if defined(__APPLE__)
	suffix = "Library/Application Support/QuakeSpasm/id1";
#else
	suffix = ".quakespasm/id1";
#endif
	path = QDI_Join(home, suffix);
	if (!path)
		return false;
	found = QDI_TryCandidateDirectory(path, candidate);
	free(path);
	return found;
#endif
}

static qboolean QDI_ProbeQuakeRoot(const char *root, qdi_candidate_t *candidate)
{
	static const char *suffixes[] = {
		"id1", "Id1", "rerelease/id1", "rerelease/Id1"
	};
	size_t i;
	for (i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
	{
		char *path = QDI_Join(root, suffixes[i]);
		qboolean found = QDI_TryCandidateDirectory(path, candidate);
		free(path);
		if (found) return true;
	}
	return false;
}

static qboolean QDI_FindSteamCandidate(qdi_candidate_t *candidate)
{
	qdi_path_list_t roots = {0}, libraries = {0};
	int i;
	qboolean found = false;
	QDI_AddSteamRoots(&roots);
	for (i = 0; i < roots.count; ++i)
	{
		char *vdf;
		QDI_PathListAdd(&libraries, roots.items[i]);
		vdf = QDI_Join(roots.items[i], "steamapps/libraryfolders.vdf");
		if (vdf) { QDI_ParseVDF(vdf, &libraries); free(vdf); }
	}
	for (i = 0; i < libraries.count && !found; ++i)
	{
		char *quake = QDI_Join(libraries.items[i], "steamapps/common/Quake");
		if (quake) { found = QDI_ProbeQuakeRoot(quake, candidate); free(quake); }
	}
	QDI_PathListFree(&libraries);
	QDI_PathListFree(&roots);
	return found;
}

static qboolean QDI_NormalizeSelection(const char *selection,
	qdi_candidate_t *candidate)
{
	qdi_path_list_t paths = {0};
	char *copy = QDI_Strdup(selection);
	char *path, *parent;
	int i;
	qboolean found = false;
	if (!copy) return false;
	QDI_TrimPath(copy);
	QDI_PathListAdd(&paths, copy);
	if (!q_strcasecmp(QDI_BaseName(copy), "paks"))
	{
		parent = QDI_Parent(copy);
		if (parent) { QDI_PathListAdd(&paths, parent); free(parent); }
	}
	path = QDI_Join(copy, "id1");
	if (path) { QDI_PathListAdd(&paths, path); free(path); }
	path = QDI_Join(copy, "Id1");
	if (path) { QDI_PathListAdd(&paths, path); free(path); }
	path = QDI_Join(copy, "rerelease/id1");
	if (path) { QDI_PathListAdd(&paths, path); free(path); }
	path = QDI_Join(copy, "rerelease/Id1");
	if (path) { QDI_PathListAdd(&paths, path); free(path); }
	for (i = 0; i < paths.count && !found; ++i)
		found = QDI_TryCandidateDirectory(paths.items[i], candidate);
	QDI_PathListFree(&paths);
	free(copy);
	return found;
}

static qboolean QDI_SearchCheckpoint(const char *scope, uint64_t directories,
	time_t *next_checkpoint)
{
	char message[512];
	static const pl_dialog_button_t buttons[] = {
		{1, "Continue Searching"}, {0, "Stop"}
	};
	if (time(NULL) < *next_checkpoint)
		return true;
	q_snprintf(message, sizeof(message),
		"Searching %s\n\n%llu directories checked.\n\nContinue searching?",
		scope, (unsigned long long)directories);
	*next_checkpoint = time(NULL) + 30;
	return PL_MessageDialog("Find Quake Data Files", message, buttons, 2, 1, 0) == 1;
}

static qboolean QDI_SearchDirectoryCandidate(const char *directory,
	qdi_candidate_t *candidate)
{
	char *parent;
	const char *name = QDI_BaseName(directory);
	if (!q_strcasecmp(name, "id1") &&
		QDI_TryCandidateDirectory(directory, candidate))
		return true;
	if (q_strcasecmp(name, "paks"))
		return false;
	parent = QDI_Parent(directory);
	if (parent)
	{
		qboolean found = QDI_TryCandidateDirectory(parent, candidate);
		free(parent);
		return found;
	}
	return false;
}

static qboolean QDI_SearchSkipName(const char *name)
{
	static const char *skip[] = {
		".", "..", ".Trash", ".Trashes", ".Spotlight-V100", ".fseventsd",
		"Backups.backupdb", "$RECYCLE.BIN", "System Volume Information",
		"proc", "sys", "dev"
	};
	size_t i;
	for (i = 0; i < sizeof(skip) / sizeof(skip[0]); ++i)
		if (!q_strcasecmp(name, skip[i])) return true;
	return false;
}

#ifndef _WIN32
static qboolean QDI_SearchPathIsLocal(const char *path)
{
#if defined(__APPLE__)
	struct statfs fs;
	return statfs(path, &fs) == 0 && (fs.f_flags & MNT_LOCAL) != 0;
#elif defined(__linux__)
	struct statfs fs;
	/* Network and pseudo filesystems are omitted by root choice and symlink
	 * rejection.  Reject the most common virtual/container filesystem types. */
	if (statfs(path, &fs) != 0) return false;
	switch ((unsigned long)fs.f_type)
	{
	case 0x9fa0:       /* proc */
	case 0x62656572:   /* sysfs */
	case 0x6969:       /* NFS */
	case 0x517b:       /* SMB */
		return false;
	default:
		return true;
	}
#else
	/* Root selection and refusal to follow symlinks provide the portable safety
	 * boundary.  Filesystem type/mount flags are platform-specific. */
	(void)path;
	return true;
#endif
}

#if defined(__APPLE__)
static qboolean QDI_SearchSpotlight(qdi_candidate_t *candidate)
{
	int output[2];
	pid_t child;
	FILE *stream;
	char *line = NULL;
	size_t capacity = 0;
	ssize_t length;
	qboolean found = false;
	if (pipe(output) != 0) return false;
	child = fork();
	if (child == 0)
	{
		close(output[0]); dup2(output[1], STDOUT_FILENO); close(output[1]);
		execlp("mdfind", "mdfind", "kMDItemFSName == 'pak0.pak'c", (char *)NULL);
		_exit(127);
	}
	close(output[1]);
	if (child < 0) { close(output[0]); return false; }
	stream = fdopen(output[0], "r");
	while (stream && !found && (length = getline(&line, &capacity, stream)) > 0)
	{
		char *directory;
		while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
			line[--length] = '\0';
		directory = QDI_Parent(line);
		if (directory)
		{
			found = QDI_SearchDirectoryCandidate(directory, candidate);
			free(directory);
		}
	}
	free(line);
	if (stream) fclose(stream); else close(output[0]);
	if (found)
		kill(child, SIGTERM);
	while (waitpid(child, NULL, 0) < 0 && errno == EINTR)
		;
	return found;
}
#endif

static qdi_search_result_t QDI_SearchPOSIX(qdi_candidate_t *candidate)
{
	qdi_path_list_t stack = {0};
	qdi_visited_set_t visited = {0};
	const char *home = getenv("HOME");
	time_t next_checkpoint = time(NULL) + 15;
	uint64_t directories = 0;
	qboolean found = false, cancelled = false;
	char *path;

#if defined(__APPLE__)
	if (QDI_SearchSpotlight(candidate)) return QDI_SEARCH_FOUND;
#endif
	if (home) QDI_PathStackPush(&stack, home);
#if defined(__APPLE__)
	QDI_PathStackPush(&stack, "/Volumes");
#else
	const char *user = getenv("USER");
	if (user)
	{
		path = QDI_Join("/media", user);
		if (path) { QDI_PathStackPush(&stack, path); free(path); }
		path = QDI_Join("/run/media", user);
		if (path) { QDI_PathStackPush(&stack, path); free(path); }
	}
	QDI_PathStackPush(&stack, "/mnt");
#endif
	while (stack.count > 0 && !found)
	{
		DIR *dir;
		struct dirent *entry;
		struct stat current_stat;
		char *current = stack.items[--stack.count];
		stack.items[stack.count] = NULL;
		if (lstat(current, &current_stat) != 0 ||
			!S_ISDIR(current_stat.st_mode) ||
			!QDI_VisitedAdd(&visited, current_stat.st_dev, current_stat.st_ino))
			{ free(current); continue; }
		if (!QDI_SearchPathIsLocal(current)) { free(current); continue; }
		++directories;
		if (!QDI_SearchCheckpoint(current, directories, &next_checkpoint))
			{ cancelled = true; free(current); break; }
		if (QDI_SearchDirectoryCandidate(current, candidate))
			{ found = true; free(current); break; }
		dir = opendir(current);
		if (!dir) { free(current); continue; }
		while ((entry = readdir(dir)) != NULL)
		{
			struct stat st;
			if (QDI_SearchSkipName(entry->d_name)) continue;
			path = QDI_Join(current, entry->d_name);
			if (!path) continue;
			if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode))
				QDI_PathStackPush(&stack, path);
			free(path);
		}
		closedir(dir); free(current);
	}
	QDI_PathListFree(&stack);
	free(visited.entries);
	return found ? QDI_SEARCH_FOUND :
		cancelled ? QDI_SEARCH_CANCELLED : QDI_SEARCH_NOT_FOUND;
}
#else
static qdi_search_result_t QDI_SearchWindows(qdi_candidate_t *candidate)
{
	qdi_path_list_t stack = {0};
	wchar_t drives[512], *drive;
	time_t next_checkpoint = time(NULL) + 15;
	uint64_t directories = 0;
	qboolean found = false, cancelled = false;

	if (GetLogicalDriveStringsW((DWORD)(sizeof(drives) / sizeof(drives[0])), drives))
	{
		for (drive = drives; *drive; drive += wcslen(drive) + 1)
		{
			char utf8[32];
			UINT type = GetDriveTypeW(drive);
			if ((type == DRIVE_FIXED || type == DRIVE_REMOVABLE ||
				type == DRIVE_CDROM) &&
				WideCharToMultiByte(CP_UTF8, 0, drive, -1, utf8, sizeof(utf8),
					NULL, NULL) > 0)
				QDI_PathStackPush(&stack, utf8);
		}
	}
	while (stack.count > 0 && !found && !cancelled)
	{
		char *current = stack.items[--stack.count];
		char *pattern;
		wchar_t *wpattern;
		WIN32_FIND_DATAW data;
		HANDLE find;
		stack.items[stack.count] = NULL;
		++directories;
		if (!QDI_SearchCheckpoint(current, directories, &next_checkpoint))
			{ cancelled = true; free(current); break; }
		if (QDI_SearchDirectoryCandidate(current, candidate))
			{ found = true; free(current); break; }
		pattern = QDI_Join(current, "*");
		wpattern = pattern ? QDI_WidePath(pattern, true) : NULL;
		find = wpattern ? FindFirstFileW(wpattern, &data) : INVALID_HANDLE_VALUE;
		free(pattern); free(wpattern);
		if (find != INVALID_HANDLE_VALUE)
		{
			do
			{
				char utf8[1024];
				char *path;
				if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
					(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
					!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L"..") ||
					!WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, utf8,
						sizeof(utf8), NULL, NULL) || QDI_SearchSkipName(utf8))
					continue;
				path = QDI_Join(current, utf8);
				if (path) { QDI_PathStackPush(&stack, path); free(path); }
			} while (FindNextFileW(find, &data));
			FindClose(find);
		}
		free(current);
	}
	QDI_PathListFree(&stack);
	return found ? QDI_SEARCH_FOUND :
		cancelled ? QDI_SEARCH_CANCELLED : QDI_SEARCH_NOT_FOUND;
}
#endif

static qdi_search_result_t QDI_SearchComputer(qdi_candidate_t *candidate)
{
#ifdef _WIN32
	return QDI_SearchWindows(candidate);
#else
	return QDI_SearchPOSIX(candidate);
#endif
}

static qboolean QDI_SHA256(const char *path, unsigned char digest[32])
{
	FILE *file;
	void *context;
	unsigned char buffer[65536];
	size_t bytes;
#ifdef _WIN32
	file = QDI_Open(path, L"rb");
#else
	file = QDI_Open(path, "rb");
#endif
	if (!file) return false;
	context = malloc(hash_sha2_256.contextsize);
	if (!context) { fclose(file); return false; }
	hash_sha2_256.init(context);
	for (;;)
	{
		bytes = fread(buffer, 1, sizeof(buffer), file);
		if (bytes > 0) hash_sha2_256.process(context, buffer, bytes);
		if (bytes < sizeof(buffer)) break;
	}
	if (ferror(file))
	{
		free(context); fclose(file); return false;
	}
	hash_sha2_256.terminate(digest, context);
	free(context); fclose(file);
	return true;
}

static unsigned long QDI_ProcessId(void)
{
#ifdef _WIN32
	return (unsigned long)GetCurrentProcessId();
#else
	return (unsigned long)getpid();
#endif
}

static unsigned long qdi_temp_counter;
static int qdi_selftest_fail_after_install = -1;

static char *QDI_TemporaryName(const char *directory, const char *tag)
{
	char name[160];
	unsigned long counter = ++qdi_temp_counter;
	unsigned long noise = (unsigned long)time(NULL) ^
		(counter * 2654435761u) ^ QDI_ProcessId();
	q_snprintf(name, sizeof(name), ".qssm-pak-%lu-%lu-%s.tmp",
		QDI_ProcessId(), noise, tag);
	return QDI_Join(directory, name);
}

static qboolean QDI_IsTemporaryName(const char *name)
{
	size_t length;
	if (!name || strncmp(name, ".qssm-pak-", 10)) return false;
	length = strlen(name);
	return length > 14 && !strcmp(name + length - 4, ".tmp");
}

static void QDI_SweepStaleTemps(const char *directory)
{
#ifdef _WIN32
	char *pattern = QDI_Join(directory, ".qssm-pak-*.tmp");
	wchar_t *wide_pattern = pattern ? QDI_WidePath(pattern, true) : NULL;
	WIN32_FIND_DATAW data;
	HANDLE find = wide_pattern ? FindFirstFileW(wide_pattern, &data) :
		INVALID_HANDLE_VALUE;
	FILETIME now_filetime;
	ULARGE_INTEGER now, modified;
	GetSystemTimeAsFileTime(&now_filetime);
	now.LowPart = now_filetime.dwLowDateTime;
	now.HighPart = now_filetime.dwHighDateTime;
	free(pattern); free(wide_pattern);
	if (find == INVALID_HANDLE_VALUE) return;
	do
	{
		char utf8[1024];
		char *path;
		modified.LowPart = data.ftLastWriteTime.dwLowDateTime;
		modified.HighPart = data.ftLastWriteTime.dwHighDateTime;
		if ((data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY |
			FILE_ATTRIBUTE_REPARSE_POINT)) ||
			now.QuadPart <= modified.QuadPart + UINT64_C(864000000000) ||
			!WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, utf8,
				sizeof(utf8), NULL, NULL) || !QDI_IsTemporaryName(utf8))
			continue;
		path = QDI_Join(directory, utf8);
		if (path) { QDI_Remove(path); free(path); }
	} while (FindNextFileW(find, &data));
	FindClose(find);
#else
	DIR *dir = opendir(directory);
	struct dirent *entry;
	time_t cutoff = time(NULL) - 24 * 60 * 60;
	if (!dir) return;
	while ((entry = readdir(dir)) != NULL)
	{
		char *path;
		struct stat st;
		if (!QDI_IsTemporaryName(entry->d_name)) continue;
		path = QDI_Join(directory, entry->d_name);
		if (path && lstat(path, &st) == 0 && S_ISREG(st.st_mode) &&
			st.st_mtime < cutoff)
			QDI_Remove(path);
		free(path);
	}
	closedir(dir);
#endif
}

static qboolean QDI_CreateExclusiveEmpty(const char *path)
{
#ifdef _WIN32
	wchar_t *wide = QDI_WidePath(path, true);
	HANDLE file = wide ? CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
	free(wide);
	if (file == INVALID_HANDLE_VALUE) return false;
	CloseHandle(file);
	return true;
#else
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd < 0) return false;
	close(fd);
	return true;
#endif
}

static qboolean QDI_CopyToNewFile(const char *source, const char *dest)
{
	FILE *input = NULL, *output = NULL;
	unsigned char buffer[65536];
	size_t bytes;
	int output_fd = -1;
	qboolean success = false;
#ifdef _WIN32
	wchar_t *wide_dest;
	input = QDI_Open(source, L"rb");
	if (!input) goto done;
	wide_dest = QDI_WidePath(dest, true);
	output_fd = wide_dest ? _wopen(wide_dest,
		_O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
		_S_IREAD | _S_IWRITE) : -1;
	free(wide_dest);
	if (output_fd >= 0)
	{
		output = _fdopen(output_fd, "wb");
		if (output) output_fd = -1;
	}
#else
	input = QDI_Open(source, "rb");
	if (!input) goto done;
	output_fd = open(dest, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (output_fd >= 0)
	{
		output = fdopen(output_fd, "wb");
		if (output) output_fd = -1;
	}
#endif
	if (!output) goto done;
	for (;;)
	{
		bytes = fread(buffer, 1, sizeof(buffer), input);
		if (bytes > 0 && fwrite(buffer, 1, bytes, output) != bytes) goto done;
		if (bytes < sizeof(buffer)) break;
	}
	if (ferror(input) || fflush(output) != 0) goto done;
#ifdef _WIN32
	if (_commit(_fileno(output)) != 0) goto done;
#else
	if (fsync(fileno(output)) != 0) goto done;
#endif
	success = true;
done:
	if (input) fclose(input);
	if (output && fclose(output) != 0) success = false;
	if (output_fd >= 0)
#ifdef _WIN32
		_close(output_fd);
#else
		close(output_fd);
#endif
	if (!success) QDI_Remove(dest);
	return success;
}

static void QDI_SyncDirectory(const char *directory)
{
#ifdef _WIN32
	/* MoveFileW provides the atomic namespace operation.  Staged file contents
	 * have already been committed by QDI_CopyToNewFile. */
	(void)directory;
#else
	int fd = open(directory, O_RDONLY
#ifdef O_DIRECTORY
		| O_DIRECTORY
#endif
		);
	if (fd >= 0)
	{
		(void)fsync(fd);
		close(fd);
	}
#endif
}

static qboolean QDI_PreflightDestination(const char *root, char **id1,
	char *error, size_t error_size)
{
	char *probe;
	if (!QDI_Mkdir(root))
	{
		q_snprintf(error, error_size, "Cannot create or write data root: %s", root);
		return false;
	}
	*id1 = QDI_Join(root, GAMENAME);
	if (!*id1 || !QDI_Mkdir(*id1))
	{
		q_snprintf(error, error_size, "Cannot create destination folder: %s/%s",
			root, GAMENAME);
		free(*id1); *id1 = NULL;
		return false;
	}
	QDI_SweepStaleTemps(*id1);
	probe = QDI_TemporaryName(*id1, "write-test");
	if (!probe || !QDI_CreateExclusiveEmpty(probe))
	{
		q_snprintf(error, error_size, "Destination is not writable: %s", *id1);
		free(probe); free(*id1); *id1 = NULL;
		return false;
	}
	QDI_Remove(probe);
	free(probe);
	return true;
}

static qboolean QDI_InstallCandidate(const qdi_candidate_t *source,
	const char *destination, qboolean need0, qboolean need1,
	char *error, size_t error_size)
{
	const char *sources[2] = {source->pak0, source->pak1};
	const char *names[2] = {"pak0.pak", "pak1.pak"};
	qboolean needed[2] = {need0, need1};
	char *temps[2] = {NULL, NULL};
	char *dests[2] = {NULL, NULL};
	qboolean installed[2] = {false, false};
	int i;
	qboolean success = false;

	for (i = 0; i < 2; ++i)
	{
		qdi_pak_info_t staged_info;
		unsigned char source_hash[32], staged_hash[32];
		if (!needed[i]) continue;
		dests[i] = QDI_Join(destination, names[i]);
		if (!dests[i] || QDI_FileExists(dests[i]))
		{
			q_snprintf(error, error_size, "Destination appeared during import: %s",
				dests[i] ? dests[i] : names[i]);
			goto done;
		}
		temps[i] = QDI_TemporaryName(destination, names[i]);
		if (!temps[i] || !QDI_CopyToNewFile(sources[i], temps[i]))
		{
			q_snprintf(error, error_size, "Failed while staging %s", names[i]);
			goto done;
		}
		if (!QDI_InspectPak(temps[i], &staged_info) || !staged_info.valid)
		{
			q_snprintf(error, error_size, "Staged %s failed PAK validation", names[i]);
			goto done;
		}
		if (!QDI_SHA256(sources[i], source_hash) || !QDI_SHA256(temps[i], staged_hash) ||
			memcmp(source_hash, staged_hash, sizeof(source_hash)))
		{
			q_snprintf(error, error_size, "Staged %s does not match its source", names[i]);
			goto done;
		}
	}
	for (i = 0; i < 2; ++i)
	{
		unsigned char source_hash[32], installed_hash[32];
		if (!needed[i]) continue;
		if (!QDI_InstallNoReplace(temps[i], dests[i]))
		{
			q_snprintf(error, error_size,
				"Could not install %s without replacing an existing file", names[i]);
			goto done;
		}
		installed[i] = true;
		if (!QDI_SHA256(sources[i], source_hash) ||
			!QDI_SHA256(dests[i], installed_hash) ||
			memcmp(source_hash, installed_hash, sizeof(source_hash)))
		{
			q_snprintf(error, error_size,
				"Installed %s does not match its source", names[i]);
			goto done;
		}
		if (qdi_selftest_fail_after_install == i)
		{
			q_strlcpy(error, "Self-test injected install failure", error_size);
			goto done;
		}
	}
	QDI_SyncDirectory(destination);
	success = true;
done:
	if (!success)
	{
		for (i = 0; i < 2; ++i) if (installed[i]) QDI_Remove(dests[i]);
		QDI_SyncDirectory(destination);
	}
	for (i = 0; i < 2; ++i)
	{
		if (temps[i]) QDI_Remove(temps[i]);
		free(temps[i]); free(dests[i]);
	}
	return success;
}

static void QDI_SetHint(char *hint, size_t hint_size, const char *format, ...)
{
	va_list args;
	if (!hint || !hint_size) return;
	va_start(args, format);
	q_vsnprintf(hint, hint_size, format, args);
	va_end(args);
}

static qdi_select_result_t QDI_SelectCandidate(const char *initial,
	qdi_candidate_t *candidate)
{
	char *selected = (char *)malloc(QDI_PICKER_PATH_SIZE);
	qdi_select_result_t result = QDI_SELECT_CANCELLED;
	static const pl_dialog_button_t invalid_buttons[] = {{0, "OK"}};
	if (!selected) return QDI_SELECT_CANCELLED;
	if (PL_SelectDirectory("Find Quake Data Files", initial, selected,
		QDI_PICKER_PATH_SIZE))
	{
		if (QDI_NormalizeSelection(selected, candidate))
			result = QDI_SELECT_FOUND;
		else
		{
			PL_MessageDialog("Quake Data Files Not Found",
				"That folder does not contain structurally valid pak0.pak and pak1.pak files.",
				invalid_buttons, 1, 0, 0);
			result = QDI_SELECT_INVALID;
		}
	}
	free(selected);
	return result;
}

qboolean QuakeDataImport_IsSelfTestArg(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; ++i)
		if (argv[i] && !strcmp(argv[i], "-quakeimportselftest")) return true;
	return false;
}

qboolean QuakeDataImport_RunAtStartup(const char *basedir, const char *userdir,
	qboolean dedicated, char *missing_hint, size_t missing_hint_size)
{
	qdi_candidate_t base = {0}, user = {0}, source = {0};
	const char *destination_root;
	char *base_id1 = NULL, *user_id1 = NULL, *destination = NULL;
	char error[768], prompt[4096];
	qboolean base_pair, user_pair, invalid0, invalid1;
	qboolean need0, need1, source_from_steam = false, source_from_user = false;
	qboolean ready = false;
	int choice, invalid_choice;
	qdi_search_result_t search_result;
	qdi_select_result_t select_result;
	static const pl_dialog_button_t source_buttons[] = {
		{1, "Copy Files"}, {2, "Choose Folder..."},
		{3, "Search Computer"}, {0, "Cancel"}
	};
	static const pl_dialog_button_t choose_buttons[] = {
		{2, "Choose Folder..."}, {3, "Search Computer"}, {0, "Cancel"}
	};
	static const pl_dialog_button_t ok_button[] = {{0, "OK"}};
	static const pl_dialog_button_t invalid_buttons[] = {
		{1, "Show File"}, {0, "Close"}
	};

	if (missing_hint && missing_hint_size) missing_hint[0] = '\0';
	if (!basedir || !basedir[0] || COM_CheckParm("-basegame") ||
		COM_CheckParm("-noquakeimport"))
		return true;
	base_id1 = QDI_Join(basedir, GAMENAME);
	if (base_id1) QDI_InspectDirectory(base_id1, true, &base);
	if (userdir && userdir[0] && q_strcasecmp(userdir, basedir))
	{
		user_id1 = QDI_Join(userdir, GAMENAME);
		if (user_id1) QDI_InspectDirectory(user_id1, true, &user);
	}
	base_pair = base.info0.valid && base.info1.valid;
	user_pair = user.info0.valid && user.info1.valid;
	if (base_pair || base.info0.valid ||
		(user.info0.valid && !user.info1.valid) ||
		(base_id1 && QDI_HasAlternativeBaseData(base_id1)) ||
		(user_id1 && QDI_HasAlternativeBaseData(user_id1)))
	{
		/* The engine also supports base PK3s and loose extracted data.  Import is
		 * advisory and must not block configurations the normal filesystem mounts. */
		ready = true;
		goto done;
	}
	/* Only an invalid destination file blocks import.  The user-data folder is
	 * a possible source and can be ignored in favor of Steam or a chosen folder. */
	invalid0 = base.pak0 && !base.info0.valid;
	invalid1 = base.pak1 && !base.info1.valid;
	if (invalid0 || invalid1)
	{
		const char *invalid = invalid0 ? base.pak0 : base.pak1;
		QDI_SetHint(missing_hint, missing_hint_size,
			"\n\nInvalid or unreadable Quake data file: %s\nMove or remove that file, then restart Find Quake Data Files. Existing files are never replaced.",
			invalid);
		if (!dedicated)
		{
			q_snprintf(prompt, sizeof(prompt),
				"This Quake data file is invalid or unreadable:\n%s\n\n"
				"Move or remove it, then restart QSS-M. Import never replaces an existing file.",
				invalid);
			invalid_choice = PL_MessageDialog("Quake Data File Is Invalid", prompt,
				invalid_buttons, 2, 0, 0);
			if (invalid_choice == 1)
				Sys_Explore(invalid);
		}
		goto done;
	}

	destination_root = basedir;
	need0 = !base.info0.valid;
	need1 = !base.info1.valid;
	QDI_SetHint(missing_hint, missing_hint_size,
		"\n\nQuake data import destination: %s/%s\nUse Find Quake Data Files to select a folder containing pak0.pak and pak1.pak. Existing files are never replaced.",
		destination_root, GAMENAME);
	if (dedicated)
		goto done;

	if (user_pair)
	{
		source = user;
		memset(&user, 0, sizeof(user));
		source_from_user = true;
	}
	else if (QDI_FindLegacyUserCandidate(&source))
		source_from_user = true;
	else
		source_from_steam = QDI_FindSteamCandidate(&source);
	for (;;)
	{
		if (!source.directory)
		{
			choice = PL_MessageDialog("Quake Data Files Missing",
				"QSS-M could not find valid Quake data in its known local or Steam folders.\n\n"
				"Choose the Quake folder, id1/Id1 folder, paks folder, or a folder containing both PAK files.",
				choose_buttons, 3, 2, 0);
			if (choice == 2)
			{
				select_result = QDI_SelectCandidate(basedir, &source);
				if (select_result != QDI_SELECT_FOUND) continue;
			}
			else if (choice == 3)
			{
				search_result = QDI_SearchComputer(&source);
				if (search_result != QDI_SEARCH_FOUND)
				{
					if (search_result == QDI_SEARCH_NOT_FOUND)
						PL_MessageDialog("Quake Data Files Not Found",
							"The computer search finished without finding a valid Quake PAK pair.",
							ok_button, 1, 0, 0);
					continue;
				}
			}
			else goto done;
		}

		if (!QDI_PreflightDestination(destination_root, &destination, error,
			sizeof(error)))
		{
			q_snprintf(prompt, sizeof(prompt),
				"%s\n\nSource files were found at:\n%s\n\n"
				"Copy pak0.pak and pak1.pak manually into %s/%s, then restart QSS-M.",
				error, source.directory, destination_root, GAMENAME);
			PL_MessageDialog("Quake Data Cannot Be Copied", prompt,
				ok_button, 1, 0, 0);
			QDI_SetHint(missing_hint, missing_hint_size, "\n\n%s", prompt);
			goto done;
		}

		q_snprintf(prompt, sizeof(prompt),
			"Source files:\n%s\n%s%s\n\nDestination:\n%s\n\nFiles to copy: %s%s%s\n\n"
			"The source PAKs are structurally valid%s. No existing file will be replaced.%s",
			source.pak0, source.pak1, source_from_steam ? " (Steam)" :
				source_from_user ? " (QuakeSpasm data folder)" : "", destination,
			need0 ? "pak0.pak" : "", need0 && need1 ? " and " : "",
			need1 ? "pak1.pak" : "",
			QDI_CandidateRecognized(&source) ? " and match a base Quake PAK layout recognized by QSS-M" : "",
			QDI_CandidateRecognized(&source) ? "" :
				"\n\nWarning: these files appear usable but do not match a base PAK layout recognized by the engine.");
		choice = PL_MessageDialog("Copy Quake Data Files", prompt,
			source_buttons, 4, 1, 0);
		if (choice == 2)
		{
			QDI_FreeCandidate(&source);
			free(destination); destination = NULL;
			source_from_steam = false;
			source_from_user = false;
			select_result = QDI_SelectCandidate(basedir, &source);
			if (select_result != QDI_SELECT_FOUND) continue;
			continue;
		}
		if (choice == 3)
		{
			QDI_FreeCandidate(&source);
			free(destination); destination = NULL;
			source_from_steam = false;
			source_from_user = false;
			search_result = QDI_SearchComputer(&source);
			if (search_result != QDI_SEARCH_FOUND)
			{
				if (search_result == QDI_SEARCH_NOT_FOUND)
					PL_MessageDialog("Quake Data Files Not Found",
						"The computer search finished without finding a valid Quake PAK pair.",
						ok_button, 1, 0, 0);
				continue;
			}
			continue;
		}
		if (choice != 1) goto done;
		if (!QDI_InstallCandidate(&source, destination, need0, need1,
			error, sizeof(error)))
		{
			q_snprintf(prompt, sizeof(prompt),
				"Quake data was not installed.\n\n%s\n\nNo existing destination file was changed.",
				error);
			PL_MessageDialog("Quake Data Copy Failed", prompt, ok_button, 1, 0, 0);
			QDI_SetHint(missing_hint, missing_hint_size, "\n\n%s", prompt);
			goto done;
		}
		if (missing_hint && missing_hint_size) missing_hint[0] = '\0';
		ready = true;
		Sys_Printf("Imported Quake data files from %s to %s\n",
			source.directory, destination);
		goto done;
	}
done:
	free(destination);
	free(base_id1); free(user_id1);
	QDI_FreeCandidate(&base); QDI_FreeCandidate(&user); QDI_FreeCandidate(&source);
	return ready;
}

/* ------------------------------ self tests ------------------------------ */

static void QDI_WriteLE32(unsigned char *bytes, uint32_t value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
}

static qboolean QDI_WriteFixture(const char *path, const char *entry_name,
	const void *payload, size_t payload_size, uint32_t entry_offset_override,
	uint32_t entry_length_override)
{
	unsigned char header[12] = {'P','A','C','K'};
	unsigned char entry[64] = {0};
	FILE *file;
	uint32_t payload_offset = 12;
	uint32_t directory_offset = payload_offset + (uint32_t)payload_size;
	QDI_WriteLE32(header + 4, directory_offset);
	QDI_WriteLE32(header + 8, sizeof(entry));
	q_strlcpy((char *)entry, entry_name ? entry_name : "test.bin", 56);
	QDI_WriteLE32(entry + 56, entry_offset_override ? entry_offset_override : payload_offset);
	QDI_WriteLE32(entry + 60, entry_length_override ? entry_length_override : (uint32_t)payload_size);
#ifdef _WIN32
	file = QDI_Open(path, L"wb");
#else
	file = QDI_Open(path, "wb");
#endif
	if (!file) return false;
	if (fwrite(header, 1, sizeof(header), file) != sizeof(header) ||
		(payload_size && fwrite(payload, 1, payload_size, file) != payload_size) ||
		fwrite(entry, 1, sizeof(entry), file) != sizeof(entry))
	{
		fclose(file); return false;
	}
	return fclose(file) == 0;
}

static char *QDI_SelfTestDirectory(void)
{
#ifdef _WIN32
	wchar_t temp[MAX_PATH], dir[MAX_PATH];
	char utf8[QDI_PICKER_PATH_SIZE];
	if (!GetTempPathW(MAX_PATH, temp) ||
		!GetTempFileNameW(temp, L"qpr", 0, dir)) return NULL;
	DeleteFileW(dir);
	if (!CreateDirectoryW(dir, NULL) ||
		!WideCharToMultiByte(CP_UTF8, 0, dir, -1, utf8, sizeof(utf8), NULL, NULL))
		return NULL;
	return QDI_Strdup(utf8);
#else
	char pattern[] = "/tmp/qssm-quakeimportselftest-XXXXXX";
	char *dir = mkdtemp(pattern);
	return dir ? QDI_Strdup(dir) : NULL;
#endif
}

static void QDI_RemoveSelfTestDirectory(const char *directory)
{
#ifdef _WIN32
	wchar_t *wide = QDI_WidePath(directory, true);
	if (wide) RemoveDirectoryW(wide);
	free(wide);
#else
	rmdir(directory);
#endif
}

static void QDI_SelfTestResult(const char *name, qboolean passed,
	int *failures)
{
	printf("quakeimportselftest: %-38s %s\n", name, passed ? "PASS" : "FAIL");
	if (!passed) ++*failures;
}

int QuakeDataImport_RunSelfTests(void)
{
	char *directory = QDI_SelfTestDirectory();
	char *valid = NULL, *bad = NULL, *copy = NULL;
	qdi_pak_info_t info;
	unsigned char pop_data[sizeof(qdi_pop)];
	unsigned char hash1[32], hash2[32];
	qdi_candidate_t normalized = {0};
	qdi_path_list_t paths = {0};
	FILE *file;
	int failures = 0;
	size_t i;

	printf("quakeimportselftest: MAX_OSPATH=%u\n", (unsigned)MAX_OSPATH);
	if (!directory)
	{
		printf("quakeimportselftest: unable to create temporary directory\n");
		return 1;
	}
	valid = QDI_Join(directory, "valid.pak");
	bad = QDI_Join(directory, "bad.pak");
	copy = QDI_Join(directory, "copy.pak");
	for (i = 0; i < sizeof(qdi_pop) / sizeof(qdi_pop[0]); ++i)
	{
		unsigned short encoded = QDI_BigShort(qdi_pop[i]);
		memcpy(pop_data + i * sizeof(encoded), &encoded, sizeof(encoded));
	}
	QDI_SelfTestResult("valid header and raw entry lookup",
		QDI_WriteFixture(valid, "gfx/pop.lmp", pop_data, sizeof(pop_data), 0, 0) &&
		QDI_InspectPak(valid, &info) && info.valid && info.registered_pak1,
		&failures);
	QDI_SelfTestResult("invalid pop.lmp signature",
		(pop_data[0] ^= 1, QDI_WriteFixture(bad, "gfx/pop.lmp", pop_data,
			sizeof(pop_data), 0, 0)) && QDI_InspectPak(bad, &info) &&
		info.valid && !info.registered_pak1, &failures);
	pop_data[0] ^= 1;
	QDI_Remove(bad);
#ifdef _WIN32
	file = QDI_Open(bad, L"wb");
#else
	file = QDI_Open(bad, "wb");
#endif
	if (file) fclose(file);
	QDI_SelfTestResult("zero-byte PAK rejection",
		!QDI_InspectPak(bad, &info) && !info.valid, &failures);
	QDI_Remove(bad);
	QDI_WriteFixture(bad, "x", NULL, 0, UINT32_MAX, UINT32_MAX);
	QDI_SelfTestResult("signed offset and size overflow",
		!QDI_InspectPak(bad, &info), &failures);
	QDI_Remove(bad);
	QDI_WriteFixture(bad, "x", NULL, 0, 0, 0);
#ifdef _WIN32
	file = QDI_Open(bad, L"r+b");
#else
	file = QDI_Open(bad, "r+b");
#endif
	if (file) { unsigned char length[4] = {63,0,0,0}; fseek(file, 8, SEEK_SET); fwrite(length, 1, 4, file); fclose(file); }
	QDI_SelfTestResult("misaligned/truncated directory rejection",
		!QDI_InspectPak(bad, &info), &failures);
	QDI_SelfTestResult("recognized/unrecognized pak0 policy",
		QDI_RecognizedPak0Policy(339, QDI_PAK0_CRC_V106) &&
		!QDI_RecognizedPak0Policy(338, QDI_PAK0_CRC_V106) &&
		!QDI_RecognizedPak0Policy(339, 1), &failures);
	QDI_SelfTestResult("shared pop comparison",
		QuakeDataImport_ValidatePopData(pop_data, sizeof(pop_data)) &&
		!QuakeDataImport_ValidatePopData(pop_data, sizeof(pop_data) - 1), &failures);
	{
		char *first = QDI_TemporaryName(directory, "a");
		char *second = QDI_TemporaryName(directory, "a");
		QDI_SelfTestResult("unique temporary names",
			first && second && strcmp(first, second), &failures);
		free(first); free(second);
	}
	QDI_SelfTestResult("staged-copy SHA-256 verification",
		QDI_CopyToNewFile(valid, copy) && QDI_SHA256(valid, hash1) &&
		QDI_SHA256(copy, hash2) && !memcmp(hash1, hash2, sizeof(hash1)), &failures);
	QDI_Remove(copy);
	{
		char *occupied = QDI_Join(directory, "occupied.pak");
		char *incoming = QDI_Join(directory, "incoming.pak");
		qboolean setup = occupied && incoming &&
			QDI_CopyToNewFile(valid, occupied) &&
			QDI_CopyToNewFile(valid, incoming) && QDI_SHA256(occupied, hash1);
		QDI_SelfTestResult("atomic no-replace install",
			setup && !QDI_InstallNoReplace(incoming, occupied) &&
			QDI_SHA256(occupied, hash2) &&
			!memcmp(hash1, hash2, sizeof(hash1)) && QDI_FileExists(incoming),
			&failures);
		if (occupied) QDI_Remove(occupied);
		if (incoming) QDI_Remove(incoming);
		free(occupied); free(incoming);
	}
	{
		char *vdf = QDI_Join(directory, "libraryfolders.vdf");
#ifdef _WIN32
		file = QDI_Open(vdf, L"wb");
#else
		file = QDI_Open(vdf, "wb");
#endif
		if (file)
		{
			fputs("\"libraryfolders\" { "
				"\"1\" { \"path\" \"D:\\\\Steam Library\" } "
				"\"2\" \"/mnt/Steam Two\" }", file);
			fclose(file);
		}
		QDI_ParseVDF(vdf, &paths);
		QDI_SelfTestResult("VDF escapes and multiple libraries", paths.count == 2 &&
			!strcmp(paths.items[0], "D:/Steam Library") &&
			!strcmp(paths.items[1], "/mnt/Steam Two"), &failures);
		QDI_PathListFree(&paths); QDI_Remove(vdf); free(vdf);
	}
	{
		char *id1 = QDI_Join(directory, "id1");
		char *paks;
		QDI_Mkdir(id1); paks = QDI_Join(id1, "paks"); QDI_Mkdir(paks);
		{ char *p0 = QDI_Join(paks, "pak0.pak"), *p1 = QDI_Join(paks, "pak1.pak");
			QDI_CopyToNewFile(valid, p0); QDI_CopyToNewFile(valid, p1);
			QDI_SelfTestResult("folder normalization (id1/paks)",
				QDI_NormalizeSelection(paks, &normalized), &failures);
			QDI_FreeCandidate(&normalized); QDI_Remove(p0); QDI_Remove(p1); free(p0); free(p1); }
		QDI_RemoveSelfTestDirectory(paks); QDI_RemoveSelfTestDirectory(id1);
		free(paks); free(id1);
	}
#ifndef _WIN32
	{
		char *linked = QDI_Join(directory, "linked.pak");
		qboolean made = linked && symlink(valid, linked) == 0;
		QDI_SelfTestResult("symlinked PAK inspection",
			made && QDI_FileExists(linked) && QDI_InspectPak(linked, &info) &&
			info.valid, &failures);
		if (linked) QDI_Remove(linked);
		free(linked);
	}
	{
		char *target = QDI_Join(directory, "directory-target");
		char *linked = QDI_Join(directory, "directory-link");
		qboolean made = target && linked && QDI_Mkdir(target) &&
			symlink(target, linked) == 0;
		QDI_SelfTestResult("symlinked directory inspection",
			made && QDI_DirectoryExists(linked), &failures);
		if (linked) QDI_Remove(linked);
		if (target) QDI_RemoveSelfTestDirectory(target);
		free(linked); free(target);
	}
#else
	{
		char drive_root[] = "C:\\";
		wchar_t *wide_path = QDI_WidePath(
			"C:/Games/../Games/QSS-M/id1/pak0.pak", true);
		wchar_t *wide_unc = QDI_WidePath(
			"//server/share/Quake/id1/pak0.pak", true);
		QDI_TrimPath(drive_root);
		QDI_SelfTestResult("Windows extended-path normalization",
			wide_path && !wcscmp(wide_path,
				L"\\\\?\\C:\\Games\\QSS-M\\id1\\pak0.pak") &&
			wide_unc && !wcscmp(wide_unc,
				L"\\\\?\\UNC\\server\\share\\Quake\\id1\\pak0.pak") &&
			!strcmp(drive_root, "C:/") && QDI_PICKER_PATH_SIZE > MAX_PATH,
			&failures);
		free(wide_path); free(wide_unc);
	}
#endif
	{
		qdi_path_list_t normalized_paths = {0};
		char *slash = QDI_Join(directory, "");
		char *lower = QDI_Join(directory, "id1");
		char *upper = QDI_Join(directory, "Id1");
		char double_root[] = "///";
		QDI_TrimPath(double_root);
		QDI_PathListAdd(&normalized_paths, slash);
		QDI_PathListAdd(&normalized_paths, directory);
		QDI_SelfTestResult("trimmed path deduplication",
			normalized_paths.count == 1 && !strcmp(double_root, "//"),
			&failures);
		QDI_PathListAdd(&normalized_paths, lower);
		QDI_PathListAdd(&normalized_paths, upper);
		QDI_SelfTestResult("case-distinct source paths",
			normalized_paths.count == 3, &failures);
		QDI_PathListFree(&normalized_paths);
		free(slash); free(lower); free(upper);
	}
	{
		char *empty = QDI_Join(directory, "not-quake");
		if (empty) QDI_Mkdir(empty);
		QDI_SelfTestResult("invalid folder selection rejection",
			empty && !QDI_NormalizeSelection(empty, &normalized), &failures);
		QDI_FreeCandidate(&normalized);
		if (empty) QDI_RemoveSelfTestDirectory(empty);
		free(empty);
	}
	{
		char *source_dir = QDI_Join(directory, "install-source");
		char *dest_dir = QDI_Join(directory, "install-destination");
		char *p0 = NULL, *p1 = NULL, *d0 = NULL, *d1 = NULL;
		qdi_candidate_t install_source = {0};
		char install_error[256];
		qboolean setup;
		if (source_dir) QDI_Mkdir(source_dir);
		if (dest_dir) QDI_Mkdir(dest_dir);
		p0 = QDI_Join(source_dir, "pak0.pak");
		p1 = QDI_Join(source_dir, "pak1.pak");
		d0 = QDI_Join(dest_dir, "pak0.pak");
		d1 = QDI_Join(dest_dir, "pak1.pak");
		setup = source_dir && dest_dir && p0 && p1 && d0 && d1 &&
			QDI_CopyToNewFile(valid, p0) && QDI_CopyToNewFile(valid, p1) &&
			QDI_TryCandidateDirectory(source_dir, &install_source);
		qdi_selftest_fail_after_install = 0;
		QDI_SelfTestResult("two-file install rollback",
			setup && !QDI_InstallCandidate(&install_source, dest_dir, true, true,
				install_error, sizeof(install_error)) &&
			!QDI_FileExists(d0) && !QDI_FileExists(d1), &failures);
		qdi_selftest_fail_after_install = -1;
		QDI_FreeCandidate(&install_source);
		if (p0) QDI_Remove(p0);
		if (p1) QDI_Remove(p1);
		if (d0) QDI_Remove(d0);
		if (d1) QDI_Remove(d1);
		if (source_dir) QDI_RemoveSelfTestDirectory(source_dir);
		if (dest_dir) QDI_RemoveSelfTestDirectory(dest_dir);
		free(p0); free(p1); free(d0); free(d1);
		free(source_dir); free(dest_dir);
	}

	QDI_Remove(valid); QDI_Remove(bad); QDI_Remove(copy);
	free(valid); free(bad); free(copy);
	QDI_RemoveSelfTestDirectory(directory); free(directory);
	printf("quakeimportselftest: %s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
		failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
