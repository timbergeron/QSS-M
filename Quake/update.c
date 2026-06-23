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

#include "update.h"

#ifdef NO_UPDATER

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

qboolean M_Update_IsSelfTestArg(const char *arg)
{
	return arg && !strcmp(arg, "-qssm-update-selftest");
}

qboolean M_Update_IsHelperArg(const char *arg)
{
	return arg && !strcmp(arg, "-qssm-update-helper");
}

int M_Version_Compare(int l_major, int l_minor, int l_patch,
	int r_major, int r_minor, int r_patch)
{
	if (l_major != r_major)
		return (l_major > r_major) ? 1 : -1;
	if (l_minor != r_minor)
		return (l_minor > r_minor) ? 1 : -1;
	if (l_patch != r_patch)
		return (l_patch > r_patch) ? 1 : -1;
	return 0;
}

qboolean M_Version_ParseTagFull(const char *tag, int *major, int *minor,
	int *patch, char *suffix, size_t suffix_size)
{
	const char *p = tag;
	char *end;
	long maj, min, pat;

	if (major)
		*major = 0;
	if (minor)
		*minor = 0;
	if (patch)
		*patch = 0;
	if (suffix && suffix_size)
		suffix[0] = '\0';
	if (!tag)
		return false;
	if (*p == 'v' || *p == 'V')
		p++;
	if (!isdigit((unsigned char)*p))
		return false;
	maj = strtol(p, &end, 10);
	if (*end != '.')
		return false;
	p = end + 1;
	min = strtol(p, &end, 10);
	if (*end != '.')
		return false;
	p = end + 1;
	pat = strtol(p, &end, 10);
	if (major)
		*major = (int)maj;
	if (minor)
		*minor = (int)min;
	if (patch)
		*patch = (int)pat;
	if (suffix && suffix_size && *end)
		q_strlcpy(suffix, end, suffix_size);
	return true;
}

int M_Version_ParseTag(const char *tag, int *major, int *minor, int *patch)
{
	return M_Version_ParseTagFull(tag, major, minor, patch, NULL, 0);
}

int M_Version_CompareWithSuffix(int l_major, int l_minor, int l_patch,
	const char *l_suffix, qboolean l_prerelease, int r_major, int r_minor,
	int r_patch, const char *r_suffix, qboolean r_prerelease)
{
	int comparison = M_Version_Compare(l_major, l_minor, l_patch,
		r_major, r_minor, r_patch);
	qboolean l_pre = l_prerelease || (l_suffix && *l_suffix);
	qboolean r_pre = r_prerelease || (r_suffix && *r_suffix);

	if (comparison)
		return comparison;
	if (l_pre != r_pre)
		return l_pre ? -1 : 1;
	return 0;
}

int M_Version_CompareToCurrent(int major, int minor, int patch,
	const char *suffix, qboolean prerelease)
{
	return M_Version_CompareWithSuffix(QSSM_VER_MAJOR, QSSM_VER_MINOR,
		QSSM_VER_PATCH, "" QSSM_VER_SUFFIX, false, major, minor, patch,
		suffix, prerelease);
}

int M_Version_CompareTagToCurrent(const char *tag, qboolean prerelease)
{
	int major, minor, patch;
	char suffix[32];

	if (!M_Version_ParseTagFull(tag, &major, &minor, &patch, suffix,
		sizeof(suffix)))
		return 0;
	return M_Version_CompareToCurrent(major, minor, patch, suffix,
		prerelease);
}

void M_Update_CurlOptions(void *curl_handle)
{
	(void)curl_handle;
}

qboolean M_Version_GitHubHttpGet(const char *url, versionhttpmem_t *mem,
	char *error, size_t errorsz, size_t max_bytes)
{
	(void)url;
	(void)max_bytes;
	if (mem)
		memset(mem, 0, sizeof(*mem));
	q_strlcpy(error, "GitHub/update support is unavailable in this build",
		errorsz);
	return false;
}

qboolean M_VerifySHA256File(const char *path, const char *expected,
	qboolean allow_empty, m_sha256_cancel_fn_t cancel_fn)
{
	(void)path;
	(void)cancel_fn;
	return allow_empty && (!expected || !*expected);
}

void M_Update_f(void)
{
	Con_Printf("Updater is not available in this build.\n");
}

void M_Update_Completion_f(const char *partial)
{
	(void)partial;
}

int M_UpdateHelperMain(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	return 2;
}

void M_Update_RecoverAtStartup(void) {}
void M_Update_ConfirmStartup(void) {}
void M_Update_PruneStagingAtStartup(void) {}

#else

#include "q_ctype.h"
#include "q_hash.h"
#include <curl/curl.h>
#include "json.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sys/types.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#else
#include <dirent.h>
#include <fcntl.h>
#if !defined(PLATFORM_HAIKU)
#include <sys/statvfs.h>
#endif
#include <unistd.h>
#endif

#ifndef UPDATE_METADATA_USE_GNUTLS
#if defined(USE_GNUTLS) && !defined(__APPLE__)
#define UPDATE_METADATA_USE_GNUTLS 1
#else
#define UPDATE_METADATA_USE_GNUTLS 0
#endif
#endif

#if UPDATE_METADATA_USE_GNUTLS
#include <gnutls/gnutls.h>
#include <gnutls/abstract.h>
#endif

extern qboolean stop_curl_download;
extern qboolean curl_download_active;

/*
==================
Update Command
==================
*/

#define UPDATE_RELEASE_URL			VERSION_GITHUB_RELEASE_URL
#define UPDATE_RELEASE_PAGE			"https://github.com/timbergeron/QSS-M/releases/latest"
#define UPDATE_RELEASE_ASSET_PREFIX	"https://github.com/timbergeron/QSS-M/releases/download/"
#define UPDATE_RELEASE_BODY_CHARS	2000
#define UPDATE_STAGE_ROOT			"qssm-updates"
#define UPDATE_MAX_DOWNLOAD_BYTES	((curl_off_t)(256LL * 1024LL * 1024LL))
#define UPDATE_HELPER_ARG			"-qssm-update-helper"
#define UPDATE_SELFTEST_ARG		"-qssm-update-selftest"
#define UPDATE_SELFTEST_TIMEOUT_MS	10000
#define UPDATE_HELPER_MANIFEST		"update-manifest.txt"
#define UPDATE_HELPER_LOG			"update-helper.log"
#define UPDATE_HELPER_DIR			"helper"
#define UPDATE_HELPER_BACKUP_DIR	"backup"
#define UPDATE_RECOVERY_DIR		".qssm-update-recovery"
#define UPDATE_RECOVERY_MANIFEST	"recovery-manifest.txt"
#define UPDATE_APPLY_LOCK			"update.lock"
#define UPDATE_MAX_APPLY_FILES		32
#define UPDATE_STAGE_PRUNE_AGE		(7 * 24 * 60 * 60)
#define UPDATE_WINDOWS_MIN_MAJOR	6
#define UPDATE_WINDOWS_MIN_MINOR	1
#define UPDATE_WINDOWS_MIN_NAME		"Windows 7"
#define UPDATE_METADATA_ASSET		"qssm-update.json"
#define UPDATE_METADATA_SIG_ASSET	"qssm-update.json.sig"
#define UPDATE_METADATA_MAX_BYTES	(32 * 1024)
#define UPDATE_METADATA_SIG_MAX_BYTES	4096
#ifndef UPDATE_METADATA_REQUIRED
#define UPDATE_METADATA_REQUIRED	0
#endif

/*
 * Release metadata signing is enabled by embedding the PEM public key used to
 * verify qssm-update.json.sig. UPDATE_METADATA_REQUIRED should stay off for
 * the bridge release so existing builds can update through normal GitHub
 * assets before signed metadata becomes mandatory.
 */
#ifndef UPDATE_METADATA_PUBLIC_KEY_PEM
#define UPDATE_METADATA_PUBLIC_KEY_PEM ""
#endif
static const char update_metadata_public_key_pem[] =
	UPDATE_METADATA_PUBLIC_KEY_PEM;

typedef struct
{
	char		tag[64];
	char		name[128];
	char		body[UPDATE_RELEASE_BODY_CHARS + 1];
	char		html_url[256];
	qboolean	prerelease;
	int			comparison;

	char		platform[32];
	qboolean	platform_supported;
	char		asset_name[128];
	char		asset_url[512];
	double		asset_size;
	char		asset_digest[96];
	char		asset_sha256[65];
	qboolean	body_platform_zip;
	qboolean	metadata_present;
	qboolean	metadata_verified;
	qboolean	metadata_asset_pinned;
	qboolean	metadata_min_windows_set;
	int			metadata_min_windows_major;
	int			metadata_min_windows_minor;
	char		metadata_min_windows_name[64];
} updatereleaseinfo_t;

typedef struct
{
	char		name[128];
	char		url[512];
	char		digest[96];
	char		sha256[65];
	double		size;
} updateassetinfo_t;

typedef struct
{
	FILE		*file;
	curl_off_t	max_bytes;
	curl_off_t	written;
	qboolean	too_large;
	qboolean	pump_events;
	double		last_progress_time;
	double		last_event_time;
	double		last_screen_time;
	char		display_name[64];
} updatedownload_t;

typedef struct
{
	char		src[64];
	char		dst[64];
	char		sha256[65];
	qboolean	executable;
	qboolean	backup_exists;
	qboolean	applied;
} updateapplyfile_t;

typedef struct
{
	unsigned long	parent_pid;
	uintptr_t		parent_wait_token;
	char			live_dir[MAX_OSPATH];
	char			extract_dir[MAX_OSPATH];
	char			backup_dir[MAX_OSPATH];
	char			log_path[MAX_OSPATH];
	char			recovery_manifest[MAX_OSPATH];
	char			relaunch_dst[64];
	qboolean		complete;
	qboolean		startup_attempted;
	qboolean		startup_confirmed;
	updateapplyfile_t files[UPDATE_MAX_APPLY_FILES];
	int				num_files;
} updateapplymanifest_t;

typedef struct
{
	char			path[MAX_OSPATH];
#ifdef _WIN32
	HANDLE			handle;
#else
	int				fd;
#endif
} updateapplylock_t;

static qboolean M_Update_RootFileNameOkay(const char *name);
static updateapplylock_t update_startup_lock;
static qboolean update_startup_lock_initialized = false;
static qboolean update_startup_lock_active = false;
static qboolean update_startup_recovery_failed = false;
static qboolean update_startup_attempt_marked_this_process = false;

qboolean M_Update_IsSelfTestArg(const char *arg)
{
	return arg && !strcmp(arg, UPDATE_SELFTEST_ARG);
}

qboolean M_Update_IsHelperArg(const char *arg)
{
	return arg && !strcmp(arg, UPDATE_HELPER_ARG);
}

int M_Version_Compare(int l_major, int l_minor, int l_patch,
	int r_major, int r_minor, int r_patch)
{
	if (l_major != r_major)
		return (l_major > r_major) ? 1 : -1;
	if (l_minor != r_minor)
		return (l_minor > r_minor) ? 1 : -1;
	if (l_patch != r_patch)
		return (l_patch > r_patch) ? 1 : -1;
	return 0;
}

static qboolean M_Version_ParseInt(const char **cursor, int *out)
{
	const char *p;
	long value = 0;

	if (!cursor || !*cursor || !out)
		return false;
	p = *cursor;
	if (!q_isdigit((unsigned char)*p))
		return false;
	while (q_isdigit((unsigned char)*p))
	{
		value = value * 10 + (*p - '0');
		if (value > INT_MAX)
			return false;
		p++;
	}

	*out = (int)value;
	*cursor = p;
	return true;
}

qboolean M_Version_ParseTagFull(const char *tag, int *major, int *minor,
	int *patch, char *suffix, size_t suffix_size)
{
	const char *p;
	int maj, min, pat;

	if (suffix && suffix_size)
		suffix[0] = '\0';
	if (!tag)
		return false;

	p = tag;
	if (*p == 'v' || *p == 'V')
		p++;
	if (!M_Version_ParseInt(&p, &maj) || *p++ != '.' ||
		!M_Version_ParseInt(&p, &min) || *p++ != '.' ||
		!M_Version_ParseInt(&p, &pat))
		return false;

	if (major)
		*major = maj;
	if (minor)
		*minor = min;
	if (patch)
		*patch = pat;
	if (suffix && suffix_size)
		q_strlcpy(suffix, p, suffix_size);
	return true;
}

int M_Version_ParseTag(const char *tag, int *major, int *minor, int *patch)
{
	return M_Version_ParseTagFull(tag, major, minor, patch, NULL, 0);
}

static qboolean M_Version_SuffixIsPrerelease(const char *suffix,
	qboolean prerelease)
{
	if (prerelease)
		return true;
	if (!suffix || !suffix[0])
		return false;
	return suffix[0] != '+';
}

static const char *M_Version_LocalSuffix(void)
{
	return "" QSSM_VER_SUFFIX;
}

int M_Version_CompareWithSuffix(int l_major, int l_minor, int l_patch,
	const char *l_suffix, qboolean l_prerelease, int r_major, int r_minor,
	int r_patch, const char *r_suffix, qboolean r_prerelease)
{
	int comparison;
	qboolean l_pre, r_pre;

	comparison = M_Version_Compare(l_major, l_minor, l_patch, r_major,
		r_minor, r_patch);
	if (comparison != 0)
		return comparison;

	l_pre = M_Version_SuffixIsPrerelease(l_suffix, l_prerelease);
	r_pre = M_Version_SuffixIsPrerelease(r_suffix, r_prerelease);
	if (l_pre != r_pre)
		return l_pre ? -1 : 1;
	if (!l_pre)
		return 0;

	if (!l_suffix)
		l_suffix = "";
	if (!r_suffix)
		r_suffix = "";
	if (!q_strcasecmp(l_suffix, r_suffix))
		return 0;

	/* Different prerelease channels are not ordered without an explicit policy. */
	return 2;
}

int M_Version_CompareToCurrent(int major, int minor, int patch,
	const char *suffix, qboolean prerelease)
{
	const char *local_suffix = M_Version_LocalSuffix();

	return M_Version_CompareWithSuffix(QSSM_VER_MAJOR, QSSM_VER_MINOR,
		QSSM_VER_PATCH, local_suffix, false, major, minor, patch,
		suffix, prerelease);
}

int M_Version_CompareTagToCurrent(const char *tag, qboolean prerelease)
{
	int major, minor, patch;
	char suffix[32];

	if (!M_Version_ParseTagFull(tag, &major, &minor, &patch, suffix,
		sizeof(suffix)))
		return 2;
	return M_Version_CompareToCurrent(major, minor, patch, suffix,
		prerelease);
}

static size_t M_Version_GitHubWriteCallback(void *contents, size_t size,
	size_t nmemb, void *userp)
{
	size_t realsize;
	versionhttpmem_t *mem = (versionhttpmem_t *)userp;
	char *ptr;

	if (size && nmemb > (size_t)-1 / size)
	{
		mem->too_large = true;
		return 0;
	}
	realsize = size * nmemb;
	if (realsize > (size_t)-1 - mem->size - 1 ||
		(mem->max_size && mem->size + realsize > mem->max_size))
	{
		mem->too_large = true;
		return 0;
	}

	ptr = (char *)realloc(mem->memory, mem->size + realsize + 1);
	if (!ptr)
		return 0;

	mem->memory = ptr;
	memcpy(mem->memory + mem->size, contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = '\0';

	return realsize;
}

void M_Update_CurlOptions(void *curl_handle)
{
	CURL *curl = (CURL *)curl_handle;

	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, ENGINE_NAME_AND_VER);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
#if CURL_AT_LEAST_VERSION(7, 85, 0)
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
}

qboolean M_Version_GitHubHttpGet(const char *url, versionhttpmem_t *mem,
	char *error, size_t errorsz, size_t max_bytes)
{
	CURL *curl;
	CURLcode res;
	long http_code = 0;

	memset(mem, 0, sizeof(*mem));
	mem->memory = (char *)malloc(1);
	mem->max_size = max_bytes;
	if (!mem->memory)
	{
		q_strlcpy(error, "out of memory", errorsz);
		return false;
	}
	mem->memory[0] = '\0';

	curl = curl_easy_init();
	if (!curl)
	{
		q_strlcpy(error, "curl init failed", errorsz);
		free(mem->memory);
		mem->memory = NULL;
		return false;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		M_Version_GitHubWriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, mem);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 3L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 10L);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	if (max_bytes)
		curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE,
			(curl_off_t)max_bytes);
	M_Update_CurlOptions(curl);

	res = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_easy_cleanup(curl);

	if (res == CURLE_FILESIZE_EXCEEDED)
		mem->too_large = true;
	if (res != CURLE_OK)
	{
		q_strlcpy(error, mem->too_large ? "response too large" :
			curl_easy_strerror(res), errorsz);
		free(mem->memory);
		mem->memory = NULL;
		return false;
	}

	if (http_code < 200 || http_code >= 300)
	{
		q_snprintf(error, errorsz, "HTTP %ld", http_code);
		free(mem->memory);
		mem->memory = NULL;
		return false;
	}

	return true;
}

static qboolean M_Update_SHA256StringOkay(const char *sha)
{
	int i;

	if (!sha || strlen(sha) != 64)
		return false;
	for (i = 0; i < 64; i++)
		if (!q_isxdigit((unsigned char)sha[i]))
			return false;
	return true;
}

static qboolean M_Update_SHA256FromDigest(const char *digest,
	char *out, size_t outsize)
{
	if (out && outsize)
		out[0] = '\0';
	if (!digest || !out || outsize < 65)
		return false;
	if (q_strncasecmp(digest, "sha256:", 7))
		return false;
	digest += 7;
	if (!M_Update_SHA256StringOkay(digest))
		return false;
	q_strlcpy(out, digest, outsize);
	return true;
}

qboolean M_VerifySHA256File(const char *path, const char *expected,
	qboolean allow_empty, m_sha256_cancel_fn_t cancel_fn)
{
	FILE *f;
	void *ctx;
	byte buffer[65536];
	byte digest[32];
	char actual[65];
	size_t readbytes, i;

	if (!expected || !*expected)
		return allow_empty;
	if (!M_Update_SHA256StringOkay(expected))
		return false;

	f = fopen(path, "rb");
	if (!f)
		return false;

	ctx = malloc(hash_sha2_256.contextsize);
	if (!ctx)
	{
		fclose(f);
		return false;
	}

	hash_sha2_256.init(ctx);
	while ((readbytes = fread(buffer, 1, sizeof(buffer), f)) > 0)
	{
		if (cancel_fn && cancel_fn())
		{
			free(ctx);
			fclose(f);
			return false;
		}
		hash_sha2_256.process(ctx, buffer, readbytes);
	}
	hash_sha2_256.terminate(digest, ctx);

	free(ctx);
	if (ferror(f))
	{
		fclose(f);
		return false;
	}
	fclose(f);

	for (i = 0; i < sizeof(digest); i++)
		q_snprintf(actual + i * 2, sizeof(actual) - i * 2, "%02x", digest[i]);

	return q_strcasecmp(actual, expected) == 0;
}

static void M_Update_SHA256MemoryHex(const void *data, size_t data_size,
	char out[65])
{
	byte digest[32];
	size_t i;

	CalcHash(&hash_sha2_256, digest, sizeof(digest),
		(const unsigned char *)data, data_size);
	for (i = 0; i < sizeof(digest); i++)
		q_snprintf(out + i * 2, 65 - i * 2, "%02x", digest[i]);
	out[64] = '\0';
}

static qboolean M_Update_SHA256FileHex(const char *path, char *out,
	size_t outsize)
{
	FILE *f;
	void *ctx;
	byte buffer[65536];
	byte digest[32];
	size_t readbytes, i;

	if (out && outsize)
		out[0] = '\0';
	if (!path || !out || outsize < 65)
		return false;

	f = fopen(path, "rb");
	if (!f)
		return false;

	ctx = malloc(hash_sha2_256.contextsize);
	if (!ctx)
	{
		fclose(f);
		return false;
	}

	hash_sha2_256.init(ctx);
	while ((readbytes = fread(buffer, 1, sizeof(buffer), f)) > 0)
		hash_sha2_256.process(ctx, buffer, readbytes);
	hash_sha2_256.terminate(digest, ctx);
	free(ctx);

	if (ferror(f))
	{
		fclose(f);
		return false;
	}
	fclose(f);

	for (i = 0; i < sizeof(digest); i++)
		q_snprintf(out + i * 2, outsize - i * 2, "%02x", digest[i]);
	out[64] = '\0';
	return true;
}

static qboolean M_Update_VerifySHA256Memory(const void *data,
	size_t data_size, const char *expected)
{
	char actual[65];

	if (!M_Update_SHA256StringOkay(expected))
		return false;

	M_Update_SHA256MemoryHex(data, data_size, actual);
	return !q_strcasecmp(actual, expected);
}

static int M_Update_HexValue(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static qboolean M_Update_DecodeHexBlob(const char *hex, byte *out,
	size_t out_size, size_t *out_len)
{
	int high = -1;
	size_t len = 0;

	if (out_len)
		*out_len = 0;
	if (!hex || !out || !out_len)
		return false;

	for (; *hex; hex++)
	{
		int value;

		if (*hex == ' ' || *hex == '\t' || *hex == '\r' || *hex == '\n')
			continue;

		value = M_Update_HexValue((unsigned char)*hex);
		if (value < 0)
			return false;
		if (high < 0)
		{
			high = value;
			continue;
		}

		if (len >= out_size)
			return false;
		out[len++] = (byte)((high << 4) | value);
		high = -1;
	}

	if (high >= 0)
		return false;

	*out_len = len;
	return len > 0;
}

static qboolean M_Update_VerifyMetadataSignature(const byte *data,
	size_t data_size, const byte *signature, size_t signature_size,
	char *error, size_t error_size)
{
#if UPDATE_METADATA_USE_GNUTLS
	gnutls_pubkey_t pubkey;
	gnutls_datum_t key_datum;
	gnutls_datum_t data_datum;
	gnutls_datum_t sig_datum;
	static const gnutls_sign_algorithm_t algorithms[] =
	{
		GNUTLS_SIGN_ECDSA_SHA256,
		GNUTLS_SIGN_RSA_SHA256,
		GNUTLS_SIGN_RSA_PSS_SHA256,
#ifdef GNUTLS_SIGN_EDDSA_ED25519
		GNUTLS_SIGN_EDDSA_ED25519,
#endif
		GNUTLS_SIGN_UNKNOWN
	};
	int rc;
	int i;

	if (!update_metadata_public_key_pem[0])
	{
		q_strlcpy(error,
			"update metadata public key is not configured", error_size);
		return false;
	}

	rc = gnutls_global_init();
	if (rc < 0)
	{
		q_snprintf(error, error_size, "GnuTLS init failed: %s",
			gnutls_strerror(rc));
		return false;
	}

	rc = gnutls_pubkey_init(&pubkey);
	if (rc < 0)
	{
		q_snprintf(error, error_size, "public key init failed: %s",
			gnutls_strerror(rc));
		return false;
	}

	key_datum.data = (unsigned char *)update_metadata_public_key_pem;
	key_datum.size = (unsigned int)strlen(update_metadata_public_key_pem);
	rc = gnutls_pubkey_import(pubkey, &key_datum, GNUTLS_X509_FMT_PEM);
	if (rc < 0)
	{
		q_snprintf(error, error_size, "public key import failed: %s",
			gnutls_strerror(rc));
		gnutls_pubkey_deinit(pubkey);
		return false;
	}

	data_datum.data = (unsigned char *)data;
	data_datum.size = (unsigned int)data_size;
	sig_datum.data = (unsigned char *)signature;
	sig_datum.size = (unsigned int)signature_size;

	for (i = 0; algorithms[i] != GNUTLS_SIGN_UNKNOWN; i++)
	{
		rc = gnutls_pubkey_verify_data2(pubkey, algorithms[i], 0,
			&data_datum, &sig_datum);
		if (rc >= 0)
		{
			gnutls_pubkey_deinit(pubkey);
			return true;
		}
	}

	q_strlcpy(error, "update metadata signature verification failed",
		error_size);
	gnutls_pubkey_deinit(pubkey);
	return false;
#else
	(void)data;
	(void)data_size;
	(void)signature;
	(void)signature_size;
	q_strlcpy(error,
		"update metadata signature verification is unavailable in this build",
		error_size);
	return false;
#endif
}

static qboolean M_Update_ParseWindowsVersion(const char *value,
	int *major, int *minor)
{
	const char *p = value;
	long maj = 0;
	long min = 0;

	if (!value || !q_isdigit((unsigned char)*p))
		return false;
	while (q_isdigit((unsigned char)*p))
	{
		maj = maj * 10 + (*p++ - '0');
		if (maj > 99)
			return false;
	}
	if (*p == '\0')
	{
		min = 0;
	}
	else
	{
		if (*p++ != '.' || !q_isdigit((unsigned char)*p))
			return false;
		while (q_isdigit((unsigned char)*p))
		{
			min = min * 10 + (*p++ - '0');
			if (min > 99)
				return false;
		}
		if (*p)
			return false;
	}

	*major = (int)maj;
	*minor = (int)min;
	return true;
}

static void M_Update_FormatSize(double bytes, char *out, size_t outsize)
{
	double gb = bytes / (1024.0 * 1024.0 * 1024.0);
	double mb = bytes / (1024.0 * 1024.0);
	double kb = bytes / 1024.0;

	if (gb >= 1.0)
		q_snprintf(out, outsize, "%.2f GB", gb);
	else if (mb >= 1.0)
		q_snprintf(out, outsize, "%.2f MB", mb);
	else if (kb >= 1.0)
		q_snprintf(out, outsize, "%.0f KB", kb);
	else if (bytes > 0.0)
		q_snprintf(out, outsize, "%.0f bytes", bytes);
	else
		q_strlcpy(out, "unknown", outsize);
}

static void M_Update_SanitizeSegment(const char *src, char *out,
	size_t outsize, const char *fallback)
{
	const char *s;
	char *d, *end;

	if (!out || outsize == 0)
		return;

	d = out;
	end = out + outsize - 1;
	for (s = src; s && *s && d < end; s++)
	{
		unsigned char c = (unsigned char)*s;

		if (q_isalnum(c) || c == '.' || c == '_' || c == '-')
			*d++ = (char)c;
		else
			*d++ = '_';
	}
	*d = '\0';

	if (!out[0] || !strcmp(out, ".") || !strcmp(out, ".."))
		q_strlcpy(out, fallback && *fallback ? fallback : "item", outsize);
}

static qboolean M_Update_Mkdir(const char *path, char *error,
	size_t error_size)
{
	int type;

	if (!path || !*path)
		return true;

	type = Sys_FileType(path);
	if (type & FS_ENT_DIRECTORY)
		return true;
	if (type & FS_ENT_FILE)
	{
		q_snprintf(error, error_size, "path is a file: %s", path);
		return false;
	}

#ifdef _WIN32
	if (_mkdir(path) != 0 && errno != EEXIST)
#else
	if (mkdir(path, 0777) != 0 && errno != EEXIST)
#endif
	{
		q_snprintf(error, error_size, "unable to create %s: %s",
			path, strerror(errno));
		return false;
	}

	if (!(Sys_FileType(path) & FS_ENT_DIRECTORY))
	{
		q_snprintf(error, error_size, "unable to create %s", path);
		return false;
	}

	return true;
}

static qboolean M_Update_CreateDirectoryPath(const char *path, char *error,
	size_t error_size)
{
	char temp[MAX_OSPATH];
	char *p;
	size_t len;

	if (!path || !*path)
	{
		q_strlcpy(error, "empty path", error_size);
		return false;
	}

	len = strlen(path);
	if (len >= sizeof(temp))
	{
		q_strlcpy(error, "path too long", error_size);
		return false;
	}
	q_strlcpy(temp, path, sizeof(temp));

	for (p = temp; *p; p++)
		if (*p == '\\')
			*p = '/';

#ifdef _WIN32
	if (q_isalpha((unsigned char)temp[0]) && temp[1] == ':' && temp[2] == '/')
		p = temp + 3;
	else if (temp[0] == '/' && temp[1] == '/')
	{
		p = temp + 2;
		while (*p && *p != '/')
			p++;
		if (*p == '/')
			p++;
		while (*p && *p != '/')
			p++;
		if (*p == '/')
			p++;
	}
	else
#endif
		p = temp + 1;

	for (; *p; p++)
	{
		if (*p != '/')
			continue;
		*p = '\0';
		if (!M_Update_Mkdir(temp, error, error_size))
			return false;
		*p = '/';
	}

	return M_Update_Mkdir(temp, error, error_size);
}

static qboolean M_Update_JoinPath(char *out, size_t outsize,
	const char *dir, const char *name)
{
	size_t len;

	if (!out || !outsize || !dir || !*dir || !name || !*name)
		return false;

	len = strlen(dir);
	if (dir[len - 1] == '/' || dir[len - 1] == '\\')
		return (size_t)q_snprintf(out, outsize, "%s%s", dir, name) < outsize;
	return (size_t)q_snprintf(out, outsize, "%s/%s", dir, name) < outsize;
}

static qboolean M_Update_PathIsAbsolute(const char *path)
{
	if (!path || !*path)
		return false;

#ifdef _WIN32
	if ((path[0] == '\\' && path[1] == '\\') ||
		(q_isalpha((unsigned char)path[0]) && path[1] == ':' &&
		(path[2] == '\\' || path[2] == '/')) ||
		path[0] == '/' || path[0] == '\\')
		return true;
#else
	if (path[0] == '/')
		return true;
#endif

	return false;
}

static qboolean M_Update_CurrentDirectory(char *out, size_t outsize)
{
	if (!out || !outsize)
		return false;

#ifdef _WIN32
	return _getcwd(out, outsize) != NULL;
#else
	return getcwd(out, outsize) != NULL;
#endif
}

static qboolean M_Update_MakeAbsolutePath(const char *path, char *out,
	size_t outsize)
{
	char cwd[MAX_OSPATH];

	if (!path || !*path || !out || !outsize)
		return false;
	if (M_Update_PathIsAbsolute(path))
		return q_strlcpy(out, path, outsize) < outsize;

	if (!M_Update_CurrentDirectory(cwd, sizeof(cwd)))
		return false;
	return M_Update_JoinPath(out, outsize, cwd, path);
}

static void M_Update_PathDirName(const char *path, char *out, size_t outsize)
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
#ifdef _WIN32
	if (len == 2 && q_isalpha((unsigned char)path[0]) &&
		path[1] == ':' && (path[2] == '/' || path[2] == '\\'))
		len = 3;
#endif
	if (len == 0)
		len = 1;
	if (len >= outsize)
		len = outsize - 1;
	memcpy(out, path, len);
	out[len] = '\0';
}

static const char *M_Update_PathBaseName(const char *path)
{
	const char *slash, *backslash;

	if (!path || !*path)
		return "";

	slash = strrchr(path, '/');
	backslash = strrchr(path, '\\');
	if (slash && (!backslash || slash > backslash))
		return slash + 1;
	if (backslash)
		return backslash + 1;
	return path;
}

static qboolean M_Update_PathsEqual(const char *a, const char *b)
{
#ifdef _WIN32
	size_t len_a, len_b, i;

	if (!a || !b)
		return false;

	len_a = strlen(a);
	len_b = strlen(b);
	while (len_a > 1 && (a[len_a - 1] == '/' || a[len_a - 1] == '\\') &&
		!(len_a == 3 && q_isalpha((unsigned char)a[0]) &&
		a[1] == ':' && (a[2] == '/' || a[2] == '\\')))
		len_a--;
	while (len_b > 1 && (b[len_b - 1] == '/' || b[len_b - 1] == '\\') &&
		!(len_b == 3 && q_isalpha((unsigned char)b[0]) &&
		b[1] == ':' && (b[2] == '/' || b[2] == '\\')))
		len_b--;
	if (len_a != len_b)
		return false;

	for (i = 0; i < len_a; i++)
	{
		unsigned char ca = (unsigned char)a[i];
		unsigned char cb = (unsigned char)b[i];

		if (ca == '\\')
			ca = '/';
		if (cb == '\\')
			cb = '/';
		if (q_tolower(ca) != q_tolower(cb))
			return false;
	}
	return true;
#else
	return a && b && !strcmp(a, b);
#endif
}

static qboolean M_Update_RemoveTree(const char *path)
{
#ifdef _WIN32
	char pattern[MAX_OSPATH];
	WIN32_FIND_DATAA data;
	HANDLE find;
	DWORD attrs;

	attrs = GetFileAttributesA(path);
	if (attrs == INVALID_FILE_ATTRIBUTES)
		return false;
	if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) ||
		!(attrs & FILE_ATTRIBUTE_DIRECTORY))
	{
		SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
		if (attrs & FILE_ATTRIBUTE_DIRECTORY)
			return RemoveDirectoryA(path) != 0;
		return DeleteFileA(path) != 0;
	}
	if ((size_t)q_snprintf(pattern, sizeof(pattern), "%s/*", path) >=
		sizeof(pattern))
		return false;

	find = FindFirstFileA(pattern, &data);
	if (find != INVALID_HANDLE_VALUE)
	{
		do
		{
			char child[MAX_OSPATH];

			if (!strcmp(data.cFileName, ".") ||
				!strcmp(data.cFileName, ".."))
				continue;
			if (!M_Update_JoinPath(child, sizeof(child), path,
				data.cFileName))
				continue;
			if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
				!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
				M_Update_RemoveTree(child);
			else if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				SetFileAttributesA(child, FILE_ATTRIBUTE_NORMAL);
				RemoveDirectoryA(child);
			}
			else
			{
				SetFileAttributesA(child, FILE_ATTRIBUTE_NORMAL);
				DeleteFileA(child);
			}
		} while (FindNextFileA(find, &data));
		FindClose(find);
	}

	SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
	return RemoveDirectoryA(path) != 0;
#else
	DIR *dir;
	struct dirent *entry;
	struct stat st;

	if (lstat(path, &st) != 0)
		return false;
	if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
		return remove(path) == 0;

	dir = opendir(path);
	if (dir)
	{
		while ((entry = readdir(dir)) != NULL)
		{
			char child[MAX_OSPATH];

			if (!strcmp(entry->d_name, ".") ||
				!strcmp(entry->d_name, ".."))
				continue;
			if (!M_Update_JoinPath(child, sizeof(child), path,
				entry->d_name))
				continue;
			if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode) &&
				!S_ISLNK(st.st_mode))
				M_Update_RemoveTree(child);
			else
				remove(child);
		}
		closedir(dir);
	}

	return rmdir(path) == 0;
#endif
}

static qboolean M_Update_StageDirNameOwned(const char *name)
{
	static const char *const platform_markers[] =
	{
		"-win64-",
		"-win32-",
		"-linux64-",
		"-macos-",
		NULL
	};
	const char *p;
	int i;

	if (!M_Update_RootFileNameOkay(name))
		return false;

	for (i = 0; platform_markers[i]; i++)
	{
		size_t marker_len = strlen(platform_markers[i]);

		for (p = name; (p = q_strcasestr(p, platform_markers[i])) != NULL;
			p++)
		{
			const char *suffix;

			if (p == name)
				continue;
			suffix = p + marker_len;
			if (!q_isdigit((unsigned char)*suffix))
				continue;
			while (q_isdigit((unsigned char)*suffix))
				suffix++;
			if (*suffix++ != '-' ||
				!q_isdigit((unsigned char)*suffix))
				continue;
			while (q_isdigit((unsigned char)*suffix))
				suffix++;
			if (!*suffix)
				return true;
		}
	}

	return false;
}

static void M_Update_PruneStageRoot(const char *current_stage_dir)
{
	char root[MAX_OSPATH];
	time_t now;

	if ((size_t)q_snprintf(root, sizeof(root), "%s/%s", com_basedir,
		UPDATE_STAGE_ROOT) >= sizeof(root))
		return;
	if (!(Sys_FileType(root) & FS_ENT_DIRECTORY))
		return;

	now = time(NULL);

#ifdef _WIN32
	{
		char pattern[MAX_OSPATH];
		WIN32_FIND_DATAA data;
		HANDLE find;

		if ((size_t)q_snprintf(pattern, sizeof(pattern), "%s/*", root) >=
			sizeof(pattern))
			return;
		find = FindFirstFileA(pattern, &data);
		if (find == INVALID_HANDLE_VALUE)
			return;
		do
		{
			char child[MAX_OSPATH];
			time_t mtime;
			LARGE_INTEGER li;

			if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
				!strcmp(data.cFileName, ".") ||
				!strcmp(data.cFileName, "..") ||
				!M_Update_StageDirNameOwned(data.cFileName))
				continue;
			if (!M_Update_JoinPath(child, sizeof(child), root,
				data.cFileName))
				continue;
			if (current_stage_dir &&
				M_Update_PathsEqual(child, current_stage_dir))
				continue;
			li.LowPart = data.ftLastWriteTime.dwLowDateTime;
			li.HighPart = data.ftLastWriteTime.dwHighDateTime;
			mtime = (time_t)(li.QuadPart / 10000000LL - 11644473600LL);
			if (difftime(now, mtime) < UPDATE_STAGE_PRUNE_AGE)
				continue;
			if (M_Update_RemoveTree(child))
				Con_DPrintf("Pruned stale update staging dir %s\n",
					child);
		} while (FindNextFileA(find, &data));
		FindClose(find);
	}
#else
	{
		DIR *dir;
		struct dirent *entry;

		dir = opendir(root);
		if (!dir)
			return;
		while ((entry = readdir(dir)) != NULL)
		{
			char child[MAX_OSPATH];
			time_t mtime;

			if (!strcmp(entry->d_name, ".") ||
				!strcmp(entry->d_name, "..") ||
				!M_Update_StageDirNameOwned(entry->d_name))
				continue;
			if (!M_Update_JoinPath(child, sizeof(child), root,
				entry->d_name))
				continue;
			if (!(Sys_FileType(child) & FS_ENT_DIRECTORY))
				continue;
			if (current_stage_dir &&
				M_Update_PathsEqual(child, current_stage_dir))
				continue;
			if (!Sys_GetFileTime(child, &mtime) ||
				difftime(now, mtime) < UPDATE_STAGE_PRUNE_AGE)
				continue;
			if (M_Update_RemoveTree(child))
				Con_DPrintf("Pruned stale update staging dir %s\n",
					child);
		}
		closedir(dir);
	}
#endif
}

static qboolean M_Update_GetFreeDiskSpace(const char *path,
	double *bytes_available)
{
	if (bytes_available)
		*bytes_available = 0.0;
	if (!path || !*path || !bytes_available)
		return false;

#ifdef _WIN32
	{
		ULARGE_INTEGER available;

		if (!GetDiskFreeSpaceExA(path, &available, NULL, NULL))
			return false;
		*bytes_available = (double)available.QuadPart;
		return true;
	}
#elif !defined(PLATFORM_HAIKU)
	{
		struct statvfs st;

		if (statvfs(path, &st) != 0)
			return false;
		*bytes_available = (double)st.f_bavail * (double)st.f_frsize;
		return true;
	}
#else
	(void)path;
	return false;
#endif
}

static qboolean M_Update_CheckFreeDiskSpace(const char *path,
	const char *label, double asset_size, char *error, size_t error_size)
{
	double available;
	double required;
	const char *where = (label && *label) ? label : "update area";

	if (asset_size <= 0.0)
		return true;
	if (!M_Update_GetFreeDiskSpace(path, &available))
	{
		Con_Printf("Unable to check free disk space for %s; continuing.\n",
			path);
		return true;
	}

	required = asset_size * 4.0 + (64.0 * 1024.0 * 1024.0);
	if (available < required)
	{
		char have[32];
		char need[32];

		M_Update_FormatSize(available, have, sizeof(have));
		M_Update_FormatSize(required, need, sizeof(need));
		q_snprintf(error, error_size,
			"not enough free disk space in %s: %s available, %s needed",
			where, have, need);
		return false;
	}

	return true;
}

static qboolean M_Update_RootFileNameOkay(const char *name)
{
	const char *s;

	if (!name || !*name || !strcmp(name, ".") || !strcmp(name, ".."))
		return false;
	for (s = name; *s; s++)
	{
		if (*s == '/' || *s == '\\' || *s == ':' || *s == '|' ||
			*s == '<' || *s == '>' || *s == '"' || (unsigned char)*s < 32)
			return false;
	}
	if (strstr(name, ".."))
		return false;
	return true;
}

static qboolean M_Update_FlushFileToDisk(FILE *f, char *error,
	size_t error_size)
{
	if (fflush(f) != 0 || ferror(f))
	{
		q_strlcpy(error, "flush failed", error_size);
		return false;
	}

#ifdef _WIN32
	{
		int fd = _fileno(f);
		HANDLE handle;

		if (fd < 0)
			return true;
		handle = (HANDLE)_get_osfhandle(fd);
		if (handle == INVALID_HANDLE_VALUE)
			return true;
		if (!FlushFileBuffers(handle))
		{
			q_snprintf(error, error_size, "disk flush failed: %lu",
				(unsigned long)GetLastError());
			return false;
		}
	}
#else
	{
		int fd = fileno(f);

		if (fd >= 0)
		{
			int rc;

			do
			{
				rc = fsync(fd);
			} while (rc != 0 && errno == EINTR);

			if (rc != 0)
			{
				q_snprintf(error, error_size, "disk flush failed: %s",
					strerror(errno));
				return false;
			}
		}
	}
#endif

	return true;
}

#ifndef _WIN32
static void M_Update_SyncDirectoryForPath(const char *path)
{
	char dir[MAX_OSPATH];
	int fd;

	M_Update_PathDirName(path, dir, sizeof(dir));
	fd = open(dir, O_RDONLY);
	if (fd < 0)
		return;
	(void)fsync(fd);
	close(fd);
}
#endif

static void M_Update_ClearReadOnly(const char *path)
{
#ifdef _WIN32
	DWORD attrs;

	attrs = GetFileAttributesA(path);
	if (attrs != INVALID_FILE_ATTRIBUTES &&
		!(attrs & FILE_ATTRIBUTE_DIRECTORY) &&
		(attrs & FILE_ATTRIBUTE_READONLY))
	{
		SetFileAttributesA(path, attrs & ~FILE_ATTRIBUTE_READONLY);
	}
#else
	(void)path;
#endif
}

static int M_Update_RemoveFile(const char *path)
{
	M_Update_ClearReadOnly(path);
	return remove(path);
}

#ifdef _WIN32
static const char *M_Update_WinErrorName(DWORD err)
{
	switch (err)
	{
	case ERROR_SUCCESS:			return "ERROR_SUCCESS";
	case ERROR_FILE_NOT_FOUND:	return "ERROR_FILE_NOT_FOUND";
	case ERROR_PATH_NOT_FOUND:	return "ERROR_PATH_NOT_FOUND";
	case ERROR_ACCESS_DENIED:	return "ERROR_ACCESS_DENIED";
	case ERROR_NOT_READY:		return "ERROR_NOT_READY";
	case ERROR_WRITE_PROTECT:	return "ERROR_WRITE_PROTECT";
	case ERROR_SHARING_VIOLATION:	return "ERROR_SHARING_VIOLATION";
	case ERROR_LOCK_VIOLATION:	return "ERROR_LOCK_VIOLATION";
	case ERROR_HANDLE_DISK_FULL:	return "ERROR_HANDLE_DISK_FULL";
	case ERROR_DISK_FULL:		return "ERROR_DISK_FULL";
	case ERROR_FILE_EXISTS:		return "ERROR_FILE_EXISTS";
	case ERROR_ALREADY_EXISTS:	return "ERROR_ALREADY_EXISTS";
	case ERROR_INVALID_NAME:	return "ERROR_INVALID_NAME";
	case ERROR_INVALID_PARAMETER:	return "ERROR_INVALID_PARAMETER";
	case ERROR_USER_MAPPED_FILE:	return "ERROR_USER_MAPPED_FILE";
	case ERROR_VIRUS_INFECTED:	return "ERROR_VIRUS_INFECTED";
	case ERROR_VIRUS_DELETED:	return "ERROR_VIRUS_DELETED";
	default:					return "(see winerror.h)";
	}
}

#define UPDATE_WIN_REPLACE_RETRY_ATTEMPTS	30

static qboolean M_Update_WinReplaceRetryable(DWORD err)
{
	return err == ERROR_ACCESS_DENIED ||
		err == ERROR_SHARING_VIOLATION ||
		err == ERROR_LOCK_VIOLATION ||
		err == ERROR_USER_MAPPED_FILE;
}

static DWORD M_Update_WinReplaceRetryDelayMS(int attempt)
{
	return attempt < 10 ? 100 : 1000;
}

static qboolean M_Update_WinProbeReplaceable(const char *path, DWORD *err_out)
{
	DWORD attrs;
	HANDLE h;

	attrs = GetFileAttributesA(path);
	if (attrs == INVALID_FILE_ATTRIBUTES)
	{
		DWORD err = GetLastError();

		if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
			return true;
		if (err_out)
			*err_out = err;
		return false;
	}
	if (attrs & FILE_ATTRIBUTE_DIRECTORY)
	{
		if (err_out)
			*err_out = ERROR_ACCESS_DENIED;
		return false;
	}

	M_Update_ClearReadOnly(path);
	h = CreateFileA(path, DELETE, 0, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		if (err_out)
			*err_out = GetLastError();
		return false;
	}
	CloseHandle(h);
	return true;
}

/*
 * Delete any "<dst>.qssm-stale-*" images left behind by a prior replace of a
 * file that could not be deleted at the time (e.g. a running executable that
 * was renamed aside). Best-effort: an image that is still running stays
 * locked and is swept on a later run once it is no longer in use.
 */
static void M_Update_SweepStaleReplacements(const char *dst)
{
	char pattern[MAX_OSPATH];
	char dir[MAX_OSPATH];
	WIN32_FIND_DATAA data;
	HANDLE find;

	if ((size_t)q_snprintf(pattern, sizeof(pattern), "%s.qssm-stale-*",
		dst) >= sizeof(pattern))
		return;
	M_Update_PathDirName(dst, dir, sizeof(dir));

	find = FindFirstFileA(pattern, &data);
	if (find == INVALID_HANDLE_VALUE)
		return;
	do
	{
		char path[MAX_OSPATH];

		if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		if (M_Update_JoinPath(path, sizeof(path), dir, data.cFileName))
		{
			M_Update_ClearReadOnly(path);
			DeleteFileA(path);
		}
	} while (FindNextFileA(find, &data));
	FindClose(find);
}
#endif

static qboolean M_Update_ReplaceFile(const char *src, const char *dst,
	qboolean allow_rename_aside, char *error, size_t error_size)
{
#ifdef _WIN32
	DWORD err = ERROR_SUCCESS;
	DWORD waited_ms = 0;
	int attempts = 0;
	int attempt;

	M_Update_SweepStaleReplacements(dst);
	M_Update_ClearReadOnly(dst);

	/*
	 * Retry transient failures: antivirus, the search indexer, and cloud sync
	 * (e.g. OneDrive on a redirected Desktop folder) routinely hold a file
	 * open for a brief moment, which surfaces as a sharing/lock/access error.
	 * A short backoff lets them release the handle.
	 */
	for (attempt = 0; attempt < UPDATE_WIN_REPLACE_RETRY_ATTEMPTS; attempt++)
	{
		DWORD delay_ms;

		if (MoveFileExA(src, dst,
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			return true;
		err = GetLastError();
		attempts = attempt + 1;
		if (!M_Update_WinReplaceRetryable(err))
			break;
		delay_ms = M_Update_WinReplaceRetryDelayMS(attempt);
		Sleep(delay_ms);
		waited_ms += delay_ms;
	}

	/*
	 * The destination still cannot be overwritten. A currently-running
	 * executable (or a file locked without FILE_SHARE_DELETE) can still be
	 * renamed aside; do that, then move the new file into place. The displaced
	 * image is swept on a later replace via the .qssm-stale- marker.
	 */
	if (allow_rename_aside &&
		(err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION))
	{
		char aside[MAX_OSPATH];
		DWORD rename_err;

		if ((size_t)q_snprintf(aside, sizeof(aside), "%s.qssm-stale-%lu-%lu",
			dst, (unsigned long)GetCurrentProcessId(),
			(unsigned long)GetTickCount()) >= sizeof(aside))
		{
			q_snprintf(error, error_size,
				"unable to replace %s after %d attempt(s)/%lu ms: %lu (%s); "
				"aside path too long",
				dst, attempts, (unsigned long)waited_ms,
				(unsigned long)err, M_Update_WinErrorName(err));
			return false;
		}
		if (!MoveFileExA(dst, aside, 0))
		{
			rename_err = GetLastError();
			q_snprintf(error, error_size,
				"unable to replace %s after %d attempt(s)/%lu ms: %lu (%s); "
				"rename-aside failed: %lu (%s)",
				dst, attempts, (unsigned long)waited_ms,
				(unsigned long)err, M_Update_WinErrorName(err),
				(unsigned long)rename_err,
				M_Update_WinErrorName(rename_err));
			return false;
		}
		if (MoveFileExA(src, dst, MOVEFILE_WRITE_THROUGH))
		{
			if (!DeleteFileA(aside))
				MoveFileExA(aside, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
			return true;
		}
		/* Could not place the new file; restore the original name. */
		err = GetLastError();
		if (!MoveFileExA(aside, dst,
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			DWORD restore_err = GetLastError();

			q_snprintf(error, error_size,
				"unable to replace %s after %d attempt(s)/%lu ms: "
				"move-into-place failed: %lu (%s); restore from %s failed: "
				"%lu (%s)",
				dst, attempts, (unsigned long)waited_ms,
				(unsigned long)err, M_Update_WinErrorName(err), aside,
				(unsigned long)restore_err,
				M_Update_WinErrorName(restore_err));
			return false;
		}
		q_snprintf(error, error_size,
			"unable to replace %s after %d attempt(s)/%lu ms: "
			"move-into-place failed: %lu (%s); original restored",
			dst, attempts, (unsigned long)waited_ms,
			(unsigned long)err, M_Update_WinErrorName(err));
		return false;
	}

	q_snprintf(error, error_size,
		"unable to replace %s after %d attempt(s)/%lu ms: %lu (%s)",
		dst, attempts, (unsigned long)waited_ms, (unsigned long)err,
		M_Update_WinErrorName(err));
	return false;
#else
	if (rename(src, dst) != 0)
	{
		q_snprintf(error, error_size, "unable to replace %s: %s",
			dst, strerror(errno));
		return false;
	}
	M_Update_SyncDirectoryForPath(dst);
	return true;
#endif
}

/*
 * Create a uniquely named temp file next to target with exclusive (O_EXCL)
 * semantics so a pre-existing file or symlink in the destination directory
 * cannot be followed or clobbered. The chosen path is returned in temp_path.
 */
static FILE *M_Update_OpenExclusiveTemp(const char *target, char *temp_path,
	size_t temp_path_size, char *error, size_t error_size)
{
	static unsigned long counter = 0;
	unsigned long pid = Sys_GetProcessId();
	unsigned long now = (unsigned long)time(NULL);
	int attempt;

	for (attempt = 0; attempt < 64; attempt++)
	{
		int fd;
		FILE *f;

		if ((size_t)q_snprintf(temp_path, temp_path_size, "%s.tmp-%lu-%lx-%lu",
			target, pid, now, counter++) >= temp_path_size)
		{
			q_strlcpy(error, "temporary path too long", error_size);
			return NULL;
		}

#ifdef _WIN32
		fd = _open(temp_path, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
			_S_IREAD | _S_IWRITE);
#else
		fd = open(temp_path, O_CREAT | O_EXCL | O_WRONLY, 0600);
#endif
		if (fd < 0)
		{
			if (errno == EEXIST)
				continue;
			q_snprintf(error, error_size, "unable to create temp file: %s",
				strerror(errno));
			return NULL;
		}

#ifdef _WIN32
		f = _fdopen(fd, "wb");
#else
		f = fdopen(fd, "wb");
#endif
		if (!f)
		{
#ifdef _WIN32
			_close(fd);
#else
			close(fd);
#endif
			M_Update_RemoveFile(temp_path);
			q_strlcpy(error, "unable to open temp file", error_size);
			return NULL;
		}
		return f;
	}

	q_strlcpy(error, "unable to create unique temp file", error_size);
	return NULL;
}

static qboolean M_Update_CopyFileAtomic(const char *src, const char *dst,
	qboolean executable, char *error, size_t error_size)
{
	FILE *in = NULL, *out = NULL;
	byte buffer[65536];
	char temp[MAX_OSPATH] = "";
#ifndef _WIN32
	struct stat src_stat;
#endif
	size_t readbytes;
	qboolean ok = false;

	in = fopen(src, "rb");
	if (!in)
	{
		q_snprintf(error, error_size, "unable to open %s: %s",
			src, strerror(errno));
		goto done;
	}

	out = M_Update_OpenExclusiveTemp(dst, temp, sizeof(temp), error,
		error_size);
	if (!out)
		goto done;

	while ((readbytes = fread(buffer, 1, sizeof(buffer), in)) > 0)
	{
		if (fwrite(buffer, 1, readbytes, out) != readbytes)
		{
			q_snprintf(error, error_size, "write failed for %s", temp);
			goto done;
		}
	}
	if (ferror(in))
	{
		q_snprintf(error, error_size, "read failed for %s", src);
		goto done;
	}
#ifndef _WIN32
	if (stat(src, &src_stat) == 0)
		(void)chmod(temp, src_stat.st_mode);
#endif
	if (executable && !Sys_MakeExecutable(temp))
	{
		q_snprintf(error, error_size, "unable to make %s executable", temp);
		goto done;
	}
	{
		char flush_error[256] = "";

		if (!M_Update_FlushFileToDisk(out, flush_error,
			sizeof(flush_error)))
		{
			q_snprintf(error, error_size, "%s for %s",
				flush_error[0] ? flush_error : "flush failed", temp);
			goto done;
		}
	}

	fclose(out);
	out = NULL;

	if (!M_Update_ReplaceFile(temp, dst, executable, error, error_size))
		goto done;

	ok = true;

done:
	if (out)
		fclose(out);
	if (in)
		fclose(in);
	if (!ok && temp[0])
		M_Update_RemoveFile(temp);
	return ok;
}

static qboolean M_Update_DirectoryWritable(const char *dir, char *error,
	size_t error_size)
{
	char test_path[MAX_OSPATH];
	unsigned long pid = Sys_GetProcessId();
	long now = (long)time(NULL);
	int fd = -1;
	int attempt;

	for (attempt = 0; attempt < 8; attempt++)
	{
		if (!M_Update_JoinPath(test_path, sizeof(test_path), dir,
			va(".qssm-update-write-test-%lu-%ld-%d", pid, now,
			attempt)))
		{
			q_strlcpy(error, "write test path too long", error_size);
			return false;
		}

#ifdef _WIN32
		fd = _open(test_path, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
			_S_IREAD | _S_IWRITE);
#else
		fd = open(test_path, O_CREAT | O_EXCL | O_WRONLY, 0600);
#endif
		if (fd >= 0)
			break;
		if (errno != EEXIST)
			break;
	}
	if (fd < 0)
	{
		q_snprintf(error, error_size,
			"install directory is not writable: %s", dir);
		return false;
	}
#ifdef _WIN32
	_close(fd);
#else
	close(fd);
#endif
	remove(test_path);
	return true;
}

static qboolean M_Update_RunningFromManagedPackage(const char *live_dir,
	char *error, size_t error_size)
{
	if (!live_dir || !*live_dir)
		return false;

#if defined(__APPLE__) || defined(__linux__)
	if (strstr(live_dir, "/Cellar/") || strstr(live_dir, "/Caskroom/") ||
		!strncmp(live_dir, "/nix/store/", 11))
	{
		q_strlcpy(error,
			"package-managed builds must be updated through their package manager",
			error_size);
		return true;
	}
#endif

#if defined(__APPLE__)
	if (strstr(live_dir, "/AppTranslocation/"))
	{
		q_strlcpy(error,
			"macOS app translocation detected; move QSS-M to a normal writable folder before updating",
			error_size);
		return true;
	}
	if (!strncmp(live_dir, "/Volumes/", 9))
	{
		q_strlcpy(error,
			"mounted DMG/volume install detected; copy QSS-M to a writable folder before updating",
			error_size);
		return true;
	}
#endif

#if defined(__linux__)
	const char *flatpak_id = getenv("FLATPAK_ID");
	const char *snap = getenv("SNAP");
	const char *appimage = getenv("APPIMAGE");

	if ((flatpak_id && *flatpak_id) || (snap && *snap) ||
		(appimage && *appimage))
	{
		q_strlcpy(error,
			"package-managed builds must be updated through their package manager",
			error_size);
		return true;
	}
	if (!strncmp(live_dir, "/usr/", 5) ||
		!strncmp(live_dir, "/nix/store/", 11) ||
		!strncmp(live_dir, "/snap/", 6) ||
		!strncmp(live_dir, "/app/", 5) ||
		!strncmp(live_dir, "/var/lib/flatpak/", 17))
	{
		q_strlcpy(error,
			"system/package-managed install path; use the package manager or a portable build",
			error_size);
		return true;
	}
#endif

	return false;
}

static qboolean M_Update_GetLiveInstallPaths(char *current_exe,
	size_t current_exe_size, char *live_dir, size_t live_dir_size,
	char *live_exe_name, size_t live_exe_name_size, char *error,
	size_t error_size)
{
	const char *base;

	if (!Sys_GetExecutablePath(current_exe, current_exe_size))
	{
		q_strlcpy(error, "unable to determine current executable path",
			error_size);
		return false;
	}

	M_Update_PathDirName(current_exe, live_dir, live_dir_size);
	base = M_Update_PathBaseName(current_exe);
	if (!M_Update_RootFileNameOkay(base))
	{
		q_strlcpy(error, "current executable name is not safe to update",
			error_size);
		return false;
	}
	if (q_strlcpy(live_exe_name, base, live_exe_name_size) >=
		live_exe_name_size)
	{
		q_strlcpy(error, "current executable name is too long to update",
			error_size);
		return false;
	}

	if (M_Update_RunningFromManagedPackage(live_dir, error, error_size))
		return false;
	if (!M_Update_DirectoryWritable(live_dir, error, error_size))
		return false;

	return true;
}

static void M_Update_AssetFileName(const updatereleaseinfo_t *info,
	char *out, size_t outsize)
{
	const char *name = (info && info->asset_name[0]) ?
		info->asset_name : "qssm-update.zip";

	M_Update_SanitizeSegment(COM_SkipPath(name), out, outsize,
		"qssm-update.zip");
}

static qboolean M_Update_BuildStagePaths(const updatereleaseinfo_t *info,
	char *stage_dir, size_t stage_dir_size, char *zip_path,
	size_t zip_path_size, char *extract_dir, size_t extract_dir_size,
	char *error, size_t error_size)
{
	char tag[64];
	char asset_file[128];
	long now;

	M_Update_SanitizeSegment(info->tag, tag, sizeof(tag), "release");
	M_Update_AssetFileName(info, asset_file, sizeof(asset_file));
	now = (long)time(NULL);

	if ((size_t)q_snprintf(stage_dir, stage_dir_size,
		"%s/%s/%s-%s-%ld-%d", com_basedir, UPDATE_STAGE_ROOT, tag,
		info->platform, now, host_framecount) >= stage_dir_size)
	{
		q_strlcpy(error, "stage path too long", error_size);
		return false;
	}
	if ((size_t)q_snprintf(zip_path, zip_path_size, "%s/%s", stage_dir,
		asset_file) >= zip_path_size)
	{
		q_strlcpy(error, "ZIP path too long", error_size);
		return false;
	}
	if ((size_t)q_snprintf(extract_dir, extract_dir_size, "%s/extract",
		stage_dir) >= extract_dir_size)
	{
		q_strlcpy(error, "extract path too long", error_size);
		return false;
	}

	if (!M_Update_CreateDirectoryPath(stage_dir, error, error_size) ||
		!M_Update_CreateDirectoryPath(extract_dir, error, error_size))
		return false;

	M_Update_PruneStageRoot(stage_dir);
	return true;
}

static qboolean M_Update_Platform(char *out, size_t outsize)
{
#ifdef _WIN32
	if (sizeof(void *) == 8)
		return q_strlcpy(out, "win64", outsize) < outsize;
	return q_strlcpy(out, "win32", outsize) < outsize;
#elif defined(__APPLE__)
	return q_strlcpy(out, "macos", outsize) < outsize;
#elif defined(__linux__)
#if defined(__x86_64__) || defined(__amd64__)
	return q_strlcpy(out, "linux64", outsize) < outsize;
#else
	q_strlcpy(out, "linux", outsize);
	return false;
#endif
#else
	q_strlcpy(out, "unknown", outsize);
	return false;
#endif
}

static qboolean M_Update_PlatformInstallSupported(const char *platform)
{
	return !strcmp(platform, "linux64") ||
		!strcmp(platform, "win64") ||
		!strcmp(platform, "win32");
}

#ifdef _WIN32
static qboolean M_Update_WindowsVersionAtLeast(DWORD major, DWORD minor)
{
	OSVERSIONINFOEXA vinfo;
	DWORDLONG condition_mask = 0;

	memset(&vinfo, 0, sizeof(vinfo));
	vinfo.dwOSVersionInfoSize = sizeof(vinfo);
	vinfo.dwMajorVersion = major;
	vinfo.dwMinorVersion = minor;

	VER_SET_CONDITION(condition_mask, VER_MAJORVERSION,
		VER_GREATER_EQUAL);
	VER_SET_CONDITION(condition_mask, VER_MINORVERSION,
		VER_GREATER_EQUAL);

	return VerifyVersionInfoA(&vinfo,
		VER_MAJORVERSION | VER_MINORVERSION, condition_mask) != 0;
}
#endif

static void M_Update_EffectiveWindowsMinimum(const updatereleaseinfo_t *info,
	int *major, int *minor, const char **name)
{
	*major = UPDATE_WINDOWS_MIN_MAJOR;
	*minor = UPDATE_WINDOWS_MIN_MINOR;
	*name = UPDATE_WINDOWS_MIN_NAME;

	if (info->metadata_min_windows_set &&
		(info->metadata_min_windows_major > UPDATE_WINDOWS_MIN_MAJOR ||
		(info->metadata_min_windows_major == UPDATE_WINDOWS_MIN_MAJOR &&
		info->metadata_min_windows_minor > UPDATE_WINDOWS_MIN_MINOR)))
	{
		*major = info->metadata_min_windows_major;
		*minor = info->metadata_min_windows_minor;
		if (info->metadata_min_windows_name[0])
			*name = info->metadata_min_windows_name;
	}
}

static qboolean M_Update_CheckPlatformCompatibility(
	const updatereleaseinfo_t *info, char *error, size_t error_size)
{
#ifdef _WIN32
	int min_major, min_minor;
	const char *min_name;

	M_Update_EffectiveWindowsMinimum(info, &min_major, &min_minor,
		&min_name);

	if ((!strcmp(info->platform, "win64") ||
		!strcmp(info->platform, "win32")) &&
		!M_Update_WindowsVersionAtLeast((DWORD)min_major,
		(DWORD)min_minor))
	{
		q_snprintf(error, error_size,
			"portable Windows updates require %s or later",
			min_name);
		return false;
	}
#else
	(void)info;
	(void)error;
	(void)error_size;
#endif
	return true;
}

static qboolean M_Update_AssetUrlAllowed(const char *url)
{
	return url && !q_strncasecmp(url, UPDATE_RELEASE_ASSET_PREFIX,
		sizeof(UPDATE_RELEASE_ASSET_PREFIX) - 1);
}

static int M_Update_AssetNamePlatformScore(const char *name,
	const char *platform)
{
	const char *base;

	if (!name || !*name || !platform || !*platform)
		return 0;
	if (q_strcasecmp(COM_FileGetExtension(name), "zip"))
		return 0;
	if (!q_strcasestr(name, "qss-m"))
		return 0;

	base = COM_SkipPath(name);

	if (!strcmp(platform, "win64"))
	{
		if (!q_strcasecmp(base, "QSS-M-w64.zip"))
			return 100;
		if (!q_strcasecmp(base, "QSS-M-win64.zip"))
			return 90;
		return (q_strcasestr(name, "w64") ||
			q_strcasestr(name, "win64")) ? 10 : 0;
	}
	if (!strcmp(platform, "win32"))
	{
		if (!q_strcasecmp(base, "QSS-M-w32.zip"))
			return 100;
		if (!q_strcasecmp(base, "QSS-M-win32.zip"))
			return 90;
		return (q_strcasestr(name, "w32") ||
			q_strcasestr(name, "win32")) ? 10 : 0;
	}
	if (!strcmp(platform, "linux64"))
	{
		if (!q_strcasecmp(base, "QSS-M-l64.zip"))
			return 100;
		if (!q_strcasecmp(base, "QSS-M-linux64.zip"))
			return 90;
		return (q_strcasestr(name, "l64") ||
			q_strcasestr(name, "linux64")) ? 10 : 0;
	}
	if (!strcmp(platform, "macos"))
	{
		if (!q_strcasecmp(base, "QSS-M-macos.zip"))
			return 100;
		if (!q_strcasecmp(base, "QSS-M-mac.zip"))
			return 90;
		return (q_strcasestr(name, "macos") ||
			q_strcasestr(name, "mac")) ? 10 : 0;
	}

	return 0;
}

static qboolean M_Update_AssetNameMatchesPlatform(const char *name,
	const char *platform)
{
	return M_Update_AssetNamePlatformScore(name, platform) > 0;
}

static qboolean M_Update_ReleaseBodyHasPlatformZip(const char *body,
	const char *platform)
{
	const char *zip;

	if (!body || !*body)
		return false;

	zip = body;
	while ((zip = q_strcasestr(zip, ".zip")) != NULL)
	{
		char name[256];
		const char *start = zip;
		const char *end = zip + 4;
		size_t len;

		while (start > body &&
			!strchr(" \t\r\n[]()<>\"'", start[-1]))
		{
			start--;
		}

		len = (size_t)(end - start);
		if (len >= sizeof(name))
			len = sizeof(name) - 1;
		memcpy(name, start, len);
		name[len] = '\0';

		if (M_Update_AssetNameMatchesPlatform(name, platform))
			return true;

		zip = end;
	}

	return false;
}

static qboolean M_Update_FileExists(const char *dir, const char *name)
{
	char path[MAX_OSPATH];

	if ((size_t)q_snprintf(path, sizeof(path), "%s/%s", dir, name) >=
		sizeof(path))
		return false;

	return (Sys_FileType(path) & FS_ENT_FILE) != 0;
}

static qboolean M_Update_RequireFile(const char *dir, const char *name,
	char *error, size_t error_size)
{
	if (M_Update_FileExists(dir, name))
		return true;

	q_snprintf(error, error_size, "staged package missing %s", name);
	return false;
}

static qboolean M_Update_ValidateExtracted(const updatereleaseinfo_t *info,
	const char *extract_dir, char *error, size_t error_size)
{
	if (!strcmp(info->platform, "linux64"))
	{
		return M_Update_RequireFile(extract_dir, "QSS-M-l64", error,
			error_size) &&
			M_Update_RequireFile(extract_dir, "qssm.pak", error,
			error_size) &&
			M_Update_RequireFile(extract_dir, "quakespasm.pak", error,
			error_size);
	}

	if (!strcmp(info->platform, "win64"))
	{
		return M_Update_RequireFile(extract_dir, "QSS-M-w64.exe", error,
			error_size) &&
			M_Update_RequireFile(extract_dir, "qssm.pak", error,
			error_size) &&
			M_Update_RequireFile(extract_dir, "quakespasm.pak", error,
			error_size) &&
			M_Update_RequireFile(extract_dir, "SDL2.dll", error,
			error_size) &&
			M_Update_RequireFile(extract_dir, "libcurl.dll", error,
			error_size);
	}

	if (!strcmp(info->platform, "win32"))
	{
		return M_Update_RequireFile(extract_dir, "QSS-M-w32.exe", error,
			error_size) &&
			M_Update_RequireFile(extract_dir, "qssm.pak", error,
			error_size) &&
			M_Update_RequireFile(extract_dir, "quakespasm.pak", error,
			error_size) &&
			M_Update_RequireFile(extract_dir, "SDL2.dll", error,
			error_size) &&
			M_Update_RequireFile(extract_dir, "libcurl.dll", error,
			error_size);
	}

	if (!strcmp(info->platform, "macos"))
	{
		q_strlcpy(error,
			"macOS bundle staging is not enabled until the signed .app swap/quarantine path is implemented",
			error_size);
		return false;
	}

	q_snprintf(error, error_size, "unsupported update platform: %s",
		info->platform);
	return false;
}

static const char *M_Update_PackageExecutableName(const char *platform)
{
	if (!strcmp(platform, "linux64"))
		return "QSS-M-l64";
	if (!strcmp(platform, "win64"))
		return "QSS-M-w64.exe";
	if (!strcmp(platform, "win32"))
		return "QSS-M-w32.exe";
	return NULL;
}

static const char *const update_linux64_files[] =
{
	"QSS-M-l64",
	"qssm.pak",
	"quakespasm.pak",
	"QSS-M-Revision.txt",
	"LICENSE.txt",
	"Quakespasm.html",
	"Quakespasm.txt",
	"Quakespasm-Spiked.txt",
	"Quakespasm-Music.txt",
	NULL
};

static const char *const update_win_files[] =
{
	"QSS-M-w64.exe",
	"QSS-M-w32.exe",
	"qssm.pak",
	"quakespasm.pak",
	"QSS-M-Revision.txt",
	"LICENSE.txt",
	"Quakespasm.html",
	"Quakespasm.txt",
	"Quakespasm-Spiked.txt",
	"Quakespasm-Music.txt",
	"SDL2.dll",
	"libcurl.dll",
	"zlib1.dll",
	"libFLAC-8.dll",
	"libmad-0.dll",
	"libmikmod-3.dll",
	"libmpg123-0.dll",
	"libogg-0.dll",
	"libopus-0.dll",
	"libopusfile-0.dll",
	"libvorbis-0.dll",
	"libvorbisfile-3.dll",
	"libxmp.dll",
	NULL
};

static const char *const *M_Update_FileListForPlatform(const char *platform)
{
	if (!strcmp(platform, "linux64"))
		return update_linux64_files;
	if (!strcmp(platform, "win64") || !strcmp(platform, "win32"))
		return update_win_files;
	return NULL;
}

#ifdef _WIN32
static void M_Update_SweepKnownStaleReplacementsAtStartup(void)
{
	char current_exe[MAX_OSPATH];
	char live_dir[MAX_OSPATH];
	int i;

	if (!Sys_GetExecutablePath(current_exe, sizeof(current_exe)))
		return;
	M_Update_PathDirName(current_exe, live_dir, sizeof(live_dir));
	for (i = 0; update_win_files[i]; i++)
	{
		char path[MAX_OSPATH];

		if (M_Update_JoinPath(path, sizeof(path), live_dir,
			update_win_files[i]))
			M_Update_SweepStaleReplacements(path);
	}
}
#endif

static qboolean M_Update_CopyHelperRuntimeFiles(const char *platform,
	const char *live_dir, const char *extract_dir, const char *helper_dir,
	char *error, size_t error_size)
{
#ifdef _WIN32
	static const char *const update_win_helper_runtime_files[] =
	{
		"SDL2.dll",
		"libcurl.dll",
		"zlib1.dll",
		"libFLAC-8.dll",
		"libmad-0.dll",
		"libmikmod-3.dll",
		"libmpg123-0.dll",
		"libogg-0.dll",
		"libopus-0.dll",
		"libopusfile-0.dll",
		"libvorbis-0.dll",
		"libvorbisfile-3.dll",
		"libxmp.dll",
		NULL
	};
	const char *const *files = update_win_helper_runtime_files;
	int i;

	if (strcmp(platform, "win64") && strcmp(platform, "win32"))
		return true;

	for (i = 0; files[i]; i++)
	{
		char src[MAX_OSPATH];
		char dst[MAX_OSPATH];

		if (!M_Update_JoinPath(dst, sizeof(dst), helper_dir, files[i]))
		{
			q_strlcpy(error, "helper runtime path too long", error_size);
			return false;
		}

		if (M_Update_FileExists(live_dir, files[i]))
		{
			if (!M_Update_JoinPath(src, sizeof(src), live_dir, files[i]))
			{
				q_strlcpy(error, "helper runtime source path too long",
					error_size);
				return false;
			}
		}
		else if (M_Update_FileExists(extract_dir, files[i]))
		{
			if (!M_Update_JoinPath(src, sizeof(src), extract_dir, files[i]))
			{
				q_strlcpy(error, "helper runtime source path too long",
					error_size);
				return false;
			}
		}
		else
			continue;

		if (!M_Update_CopyFileAtomic(src, dst, false, error, error_size))
			return false;
	}
#else
	(void)platform;
	(void)live_dir;
	(void)extract_dir;
	(void)helper_dir;
	(void)error;
	(void)error_size;
#endif

	return true;
}

static qboolean M_Update_AddApplyFile(updateapplymanifest_t *manifest,
	const char *src, const char *dst, qboolean executable, char *error,
	size_t error_size)
{
	updateapplyfile_t *file;

	if (manifest->num_files >= UPDATE_MAX_APPLY_FILES)
	{
		q_strlcpy(error, "too many update files", error_size);
		return false;
	}
	if (!M_Update_RootFileNameOkay(src) || !M_Update_RootFileNameOkay(dst))
	{
		q_snprintf(error, error_size, "invalid update file mapping: %s",
			src ? src : "(null)");
		return false;
	}

	file = &manifest->files[manifest->num_files++];
	memset(file, 0, sizeof(*file));
	if (q_strlcpy(file->src, src, sizeof(file->src)) >=
		sizeof(file->src) ||
		q_strlcpy(file->dst, dst, sizeof(file->dst)) >= sizeof(file->dst))
	{
		manifest->num_files--;
		q_strlcpy(error, "update file name too long", error_size);
		return false;
	}
	file->executable = executable;
	return true;
}

static qboolean M_Update_IsWindowsImageFile(const char *name)
{
	const char *dot;

	if (!name)
		return false;
	dot = strrchr(name, '.');
	return dot && (!q_strcasecmp(dot, ".exe") || !q_strcasecmp(dot, ".dll"));
}

static qboolean M_Update_ManifestValueOkay(const char *value)
{
	const char *s;

	if (!value)
		return false;
	for (s = value; *s; s++)
		if (*s == '\r' || *s == '\n')
			return false;
	return true;
}

static qboolean M_Update_CopyManifestValue(char *dst, size_t dst_size,
	const char *value)
{
	if (!M_Update_ManifestValueOkay(value))
		return false;
	return q_strlcpy(dst, value, dst_size) < dst_size;
}

static qboolean M_Update_ParseManifestBool(const char *value, qboolean *out)
{
	if (!value || !out)
		return false;
	if (!strcmp(value, "0"))
	{
		*out = false;
		return true;
	}
	if (!strcmp(value, "1"))
	{
		*out = true;
		return true;
	}
	return false;
}

static qboolean M_Update_ParseManifestUnsignedLong(const char *value,
	unsigned long *out)
{
	char *end;
	unsigned long parsed;

	if (!value || !q_isdigit((unsigned char)value[0]) || !out)
		return false;
	errno = 0;
	parsed = strtoul(value, &end, 10);
	if (errno == ERANGE || !end || *end)
		return false;
	*out = parsed;
	return true;
}

static qboolean M_Update_ParseWaitToken(const char *value, uintptr_t *out)
{
	char *end;
	unsigned long long parsed;

	if (!value || !q_isdigit((unsigned char)value[0]) || !out)
		return false;
	errno = 0;
	parsed = strtoull(value, &end, 10);
	if (errno == ERANGE || !end || *end ||
		(unsigned long long)(uintptr_t)parsed != parsed)
		return false;
	*out = (uintptr_t)parsed;
	return true;
}

static qboolean M_Update_BuildApplyFileList(updateapplymanifest_t *manifest,
	const char *platform, const char *live_exe_name, const char *extract_dir,
	char *error, size_t error_size)
{
	const char *package_exe = M_Update_PackageExecutableName(platform);
	const char *const *files = M_Update_FileListForPlatform(platform);
	int i;

	if (!package_exe || !files)
	{
		q_snprintf(error, error_size, "unsupported update platform: %s",
			platform);
		return false;
	}

	for (i = 0; files[i]; i++)
	{
		if (!strcmp(files[i], package_exe))
			continue;
		/* Win32 and Win64 packages list only one executable; skip the other
		 * known executable name if it is absent from this release asset. */
		if (!strcmp(files[i], "QSS-M-w64.exe") ||
			!strcmp(files[i], "QSS-M-w32.exe"))
			continue;
		if (!M_Update_FileExists(extract_dir, files[i]))
			continue;
		if (!M_Update_AddApplyFile(manifest, files[i], files[i],
			(!strcmp(platform, "win64") || !strcmp(platform, "win32")) &&
			M_Update_IsWindowsImageFile(files[i]), error, error_size))
			return false;
	}

	/* Apply the main executable last so data/DLL lock failures leave the
	 * currently running binary untouched. */
	if (!M_Update_AddApplyFile(manifest, package_exe, live_exe_name, true,
		error, error_size))
		return false;

	for (i = 0; i < manifest->num_files; i++)
	{
		char src_path[MAX_OSPATH];

		if (!M_Update_JoinPath(src_path, sizeof(src_path), extract_dir,
			manifest->files[i].src))
		{
			q_strlcpy(error, "staged source path too long", error_size);
			return false;
		}
		if (!M_Update_SHA256FileHex(src_path, manifest->files[i].sha256,
			sizeof(manifest->files[i].sha256)))
		{
			q_snprintf(error, error_size, "unable to hash staged file: %s",
				manifest->files[i].src);
			return false;
		}
	}

	q_strlcpy(manifest->relaunch_dst, live_exe_name,
		sizeof(manifest->relaunch_dst));
	return true;
}

static qboolean M_Update_WriteApplyManifest(const updateapplymanifest_t *manifest,
	const char *manifest_path, char *error, size_t error_size)
{
	FILE *f;
	char temp_path[MAX_OSPATH];
	int i;

	if (!M_Update_ManifestValueOkay(manifest->live_dir) ||
		!M_Update_ManifestValueOkay(manifest->extract_dir) ||
		!M_Update_ManifestValueOkay(manifest->backup_dir) ||
		!M_Update_ManifestValueOkay(manifest->log_path) ||
		!M_Update_ManifestValueOkay(manifest->recovery_manifest) ||
		!M_Update_ManifestValueOkay(manifest->relaunch_dst))
	{
		q_strlcpy(error, "update manifest contains an unsupported path",
			error_size);
		return false;
	}

	f = M_Update_OpenExclusiveTemp(manifest_path, temp_path,
		sizeof(temp_path), error, error_size);
	if (!f)
		return false;

	fprintf(f, "qssm_update_manifest=1\n");
	fprintf(f, "parent_pid=%lu\n", manifest->parent_pid);
	fprintf(f, "live_dir=%s\n", manifest->live_dir);
	fprintf(f, "extract_dir=%s\n", manifest->extract_dir);
	fprintf(f, "backup_dir=%s\n", manifest->backup_dir);
	fprintf(f, "log_path=%s\n", manifest->log_path);
	if (manifest->recovery_manifest[0])
		fprintf(f, "recovery_manifest=%s\n",
			manifest->recovery_manifest);
	fprintf(f, "relaunch_dst=%s\n", manifest->relaunch_dst);
	fprintf(f, "complete=%d\n", manifest->complete ? 1 : 0);
	fprintf(f, "startup_attempted=%d\n",
		manifest->startup_attempted ? 1 : 0);
	fprintf(f, "startup_confirmed=%d\n",
		manifest->startup_confirmed ? 1 : 0);
	for (i = 0; i < manifest->num_files; i++)
		fprintf(f, "file=%s|%s|%d|%d|%d|%s\n", manifest->files[i].src,
			manifest->files[i].dst, manifest->files[i].executable ? 1 : 0,
			manifest->files[i].backup_exists ? 1 : 0,
			manifest->files[i].applied ? 1 : 0, manifest->files[i].sha256);

	{
		char flush_error[256] = "";

		if (!M_Update_FlushFileToDisk(f, flush_error,
			sizeof(flush_error)))
		{
			fclose(f);
			remove(temp_path);
			q_snprintf(error, error_size,
				"failed to flush update manifest: %s",
				flush_error[0] ? flush_error : "flush failed");
			return false;
		}
	}

	fclose(f);
	if (!M_Update_ReplaceFile(temp_path, manifest_path, false, error,
		error_size))
	{
		remove(temp_path);
		return false;
	}
	return true;
}

static void M_Update_Chomp(char *s)
{
	size_t len;

	if (!s)
		return;
	len = strlen(s);
	while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n'))
		s[--len] = '\0';
}

static qboolean M_Update_ReadApplyFileLine(updateapplymanifest_t *manifest,
	const char *value)
{
	char line[256];
	char *fields[6];
	char *cursor;
	updateapplyfile_t *file;
	int num_fields = 0;
	int i;
	qboolean bool_value;

	if (manifest->num_files >= UPDATE_MAX_APPLY_FILES)
		return false;
	if (q_strlcpy(line, value, sizeof(line)) >= sizeof(line))
		return false;

	cursor = line;
	for (;;)
	{
		char *separator;

		if (num_fields >= (int)(sizeof(fields) / sizeof(fields[0])))
			return false;
		fields[num_fields++] = cursor;
		separator = strchr(cursor, '|');
		if (!separator)
			break;
		*separator++ = '\0';
		cursor = separator;
	}

	if (num_fields < 3 ||
		!M_Update_RootFileNameOkay(fields[0]) ||
		!M_Update_RootFileNameOkay(fields[1]))
		return false;
	for (i = 0; i < manifest->num_files; i++)
		if (!q_strcasecmp(manifest->files[i].dst, fields[1]))
			return false;
	if (!M_Update_ParseManifestBool(fields[2], &bool_value))
		return false;

	file = &manifest->files[manifest->num_files++];
	memset(file, 0, sizeof(*file));
	if (q_strlcpy(file->src, fields[0], sizeof(file->src)) >=
		sizeof(file->src) ||
		q_strlcpy(file->dst, fields[1], sizeof(file->dst)) >=
		sizeof(file->dst))
	{
		manifest->num_files--;
		return false;
	}
	file->executable = bool_value;
	if (num_fields >= 4)
	{
		if (!M_Update_ParseManifestBool(fields[3], &bool_value))
		{
			manifest->num_files--;
			return false;
		}
		file->backup_exists = bool_value;
	}
	if (num_fields >= 5)
	{
		if (!M_Update_ParseManifestBool(fields[4], &bool_value))
		{
			manifest->num_files--;
			return false;
		}
		file->applied = bool_value;
	}
	if (num_fields >= 6)
	{
		if (fields[5][0] && !M_Update_SHA256StringOkay(fields[5]))
		{
			manifest->num_files--;
			return false;
		}
		q_strlcpy(file->sha256, fields[5], sizeof(file->sha256));
	}
	return true;
}

static qboolean M_Update_ReadApplyManifest(const char *manifest_path,
	updateapplymanifest_t *manifest, char *error, size_t error_size)
{
	FILE *f;
	char line[1024];
	qboolean saw_magic = false;

	memset(manifest, 0, sizeof(*manifest));
	f = fopen(manifest_path, "rb");
	if (!f)
	{
		q_snprintf(error, error_size, "unable to read manifest: %s",
			strerror(errno));
		return false;
	}

	while (fgets(line, sizeof(line), f))
	{
		char *value;

		if (!strchr(line, '\n') && !feof(f))
			goto invalid_value;
		M_Update_Chomp(line);
		value = strchr(line, '=');
		if (!value)
			continue;
		*value++ = '\0';

		if (!strcmp(line, "qssm_update_manifest"))
			saw_magic = !strcmp(value, "1");
		else if (!strcmp(line, "parent_pid"))
		{
			if (!M_Update_ParseManifestUnsignedLong(value,
				&manifest->parent_pid))
				goto invalid_value;
		}
		else if (!strcmp(line, "live_dir") &&
			!M_Update_CopyManifestValue(manifest->live_dir,
			sizeof(manifest->live_dir), value))
			goto invalid_value;
		else if (!strcmp(line, "extract_dir") &&
			!M_Update_CopyManifestValue(manifest->extract_dir,
			sizeof(manifest->extract_dir), value))
			goto invalid_value;
		else if (!strcmp(line, "backup_dir") &&
			!M_Update_CopyManifestValue(manifest->backup_dir,
			sizeof(manifest->backup_dir), value))
			goto invalid_value;
		else if (!strcmp(line, "log_path") &&
			!M_Update_CopyManifestValue(manifest->log_path,
			sizeof(manifest->log_path), value))
			goto invalid_value;
		else if (!strcmp(line, "recovery_manifest") &&
			!M_Update_CopyManifestValue(manifest->recovery_manifest,
			sizeof(manifest->recovery_manifest), value))
			goto invalid_value;
		else if (!strcmp(line, "relaunch_dst") &&
			!M_Update_CopyManifestValue(manifest->relaunch_dst,
			sizeof(manifest->relaunch_dst), value))
			goto invalid_value;
		else if (!strcmp(line, "complete"))
		{
			qboolean bool_value;

			if (!M_Update_ParseManifestBool(value, &bool_value))
				goto invalid_value;
			manifest->complete = bool_value;
		}
		else if (!strcmp(line, "startup_attempted"))
		{
			qboolean bool_value;

			if (!M_Update_ParseManifestBool(value, &bool_value))
				goto invalid_value;
			manifest->startup_attempted = bool_value;
		}
		else if (!strcmp(line, "startup_confirmed"))
		{
			qboolean bool_value;

			if (!M_Update_ParseManifestBool(value, &bool_value))
				goto invalid_value;
			manifest->startup_confirmed = bool_value;
		}
		else if (!strcmp(line, "file") &&
			!M_Update_ReadApplyFileLine(manifest, value))
		{
			fclose(f);
			q_strlcpy(error, "invalid update manifest file entry",
				error_size);
			return false;
		}
	}

	fclose(f);
	if (!saw_magic || !manifest->live_dir[0] || !manifest->extract_dir[0] ||
		!manifest->backup_dir[0] || manifest->num_files <= 0 ||
		(manifest->relaunch_dst[0] &&
		!M_Update_RootFileNameOkay(manifest->relaunch_dst)) ||
		((manifest->startup_attempted || manifest->startup_confirmed) &&
		!manifest->complete) ||
		(manifest->startup_confirmed && !manifest->startup_attempted))
	{
		q_strlcpy(error, "invalid update manifest", error_size);
		return false;
	}

	return true;

invalid_value:
	fclose(f);
	q_strlcpy(error, "invalid update manifest value", error_size);
	return false;
}

static void M_Update_HelperLog(FILE *log, const char *fmt, ...)
{
	va_list argptr;

	if (!log)
		return;

	va_start(argptr, fmt);
	vfprintf(log, fmt, argptr);
	va_end(argptr);
	fflush(log);
}

#ifdef _WIN32
#define QSSM_CCH_RM_SESSION_KEY		32
#define QSSM_CCH_RM_MAX_APP_NAME	255
#define QSSM_CCH_RM_MAX_SVC_NAME	63

typedef struct
{
	DWORD	dwProcessId;
	FILETIME ProcessStartTime;
} qssm_rm_unique_process_t;

typedef enum
{
	QSSM_RM_UNKNOWN_APP = 0,
	QSSM_RM_MAIN_WINDOW = 1,
	QSSM_RM_OTHER_WINDOW = 2,
	QSSM_RM_SERVICE = 3,
	QSSM_RM_EXPLORER = 4,
	QSSM_RM_CONSOLE = 5,
	QSSM_RM_CRITICAL = 1000
} qssm_rm_app_type_t;

typedef struct
{
	qssm_rm_unique_process_t Process;
	WCHAR		strAppName[QSSM_CCH_RM_MAX_APP_NAME + 1];
	WCHAR		strServiceShortName[QSSM_CCH_RM_MAX_SVC_NAME + 1];
	qssm_rm_app_type_t ApplicationType;
	ULONG		AppStatus;
	DWORD		TSSessionId;
	BOOL		bRestartable;
} qssm_rm_process_info_t;

typedef DWORD (WINAPI *qssm_RmStartSession_f)(DWORD *, DWORD, WCHAR *);
typedef DWORD (WINAPI *qssm_RmRegisterResources_f)(DWORD, UINT, LPCWSTR *,
	UINT, qssm_rm_unique_process_t *, UINT, LPCWSTR *);
typedef DWORD (WINAPI *qssm_RmGetList_f)(DWORD, UINT *, UINT *,
	qssm_rm_process_info_t *, LPDWORD);
typedef DWORD (WINAPI *qssm_RmEndSession_f)(DWORD);

static const char *M_Update_WinRmAppTypeName(qssm_rm_app_type_t type)
{
	switch (type)
	{
	case QSSM_RM_MAIN_WINDOW:	return "main-window";
	case QSSM_RM_OTHER_WINDOW:	return "other-window";
	case QSSM_RM_SERVICE:		return "service";
	case QSSM_RM_EXPLORER:		return "explorer";
	case QSSM_RM_CONSOLE:		return "console";
	case QSSM_RM_CRITICAL:		return "critical";
	default:					return "unknown";
	}
}

static void M_Update_WideToAnsi(const WCHAR *src, char *dst, size_t dst_size)
{
	if (!dst_size)
		return;
	dst[0] = '\0';
	if (!src || !src[0])
		return;
	if (!WideCharToMultiByte(CP_ACP, 0, src, -1, dst, (int)dst_size,
		NULL, NULL))
		q_strlcpy(dst, "(unavailable)", dst_size);
}

static void M_Update_HelperLogFileLockOwners(FILE *log, const char *label,
	const char *path)
{
	HMODULE dll;
	qssm_RmStartSession_f pRmStartSession;
	qssm_RmRegisterResources_f pRmRegisterResources;
	qssm_RmGetList_f pRmGetList;
	qssm_RmEndSession_f pRmEndSession;
	WCHAR session_key[QSSM_CCH_RM_SESSION_KEY + 1] = L"";
	WCHAR wpath[MAX_OSPATH];
	LPCWSTR resources[1];
	DWORD session = 0;
	DWORD rc;
	UINT needed = 0;
	UINT count = 0;
	DWORD reboot_reasons = 0;

	if (!log)
		return;

	if (!MultiByteToWideChar(CP_ACP, 0, path, -1, wpath,
		(int)(sizeof(wpath) / sizeof(wpath[0]))))
	{
		M_Update_HelperLog(log,
			"  diag %s: Restart Manager path conversion failed %lu (%s)\n",
			label, (unsigned long)GetLastError(),
			M_Update_WinErrorName(GetLastError()));
		return;
	}

	dll = LoadLibraryA("rstrtmgr.dll");
	if (!dll)
	{
		M_Update_HelperLog(log,
			"  diag %s: Restart Manager unavailable: %lu (%s)\n",
			label, (unsigned long)GetLastError(),
			M_Update_WinErrorName(GetLastError()));
		return;
	}

	pRmStartSession = (qssm_RmStartSession_f)GetProcAddress(dll,
		"RmStartSession");
	pRmRegisterResources = (qssm_RmRegisterResources_f)GetProcAddress(dll,
		"RmRegisterResources");
	pRmGetList = (qssm_RmGetList_f)GetProcAddress(dll, "RmGetList");
	pRmEndSession = (qssm_RmEndSession_f)GetProcAddress(dll,
		"RmEndSession");
	if (!pRmStartSession || !pRmRegisterResources || !pRmGetList ||
		!pRmEndSession)
	{
		M_Update_HelperLog(log,
			"  diag %s: Restart Manager entry points unavailable\n", label);
		FreeLibrary(dll);
		return;
	}

	rc = pRmStartSession(&session, 0, session_key);
	if (rc != ERROR_SUCCESS)
	{
		M_Update_HelperLog(log,
			"  diag %s: Restart Manager start failed %lu (%s)\n",
			label, (unsigned long)rc, M_Update_WinErrorName(rc));
		FreeLibrary(dll);
		return;
	}

	resources[0] = wpath;
	rc = pRmRegisterResources(session, 1, resources, 0, NULL, 0, NULL);
	if (rc != ERROR_SUCCESS)
	{
		M_Update_HelperLog(log,
			"  diag %s: Restart Manager register failed %lu (%s)\n",
			label, (unsigned long)rc, M_Update_WinErrorName(rc));
		pRmEndSession(session);
		FreeLibrary(dll);
		return;
	}

	rc = pRmGetList(session, &needed, &count, NULL, &reboot_reasons);
	if (rc == ERROR_MORE_DATA && needed > 0)
	{
		qssm_rm_process_info_t *infos;
		UINT i;

		infos = (qssm_rm_process_info_t *)calloc(needed, sizeof(*infos));
		if (!infos)
		{
			M_Update_HelperLog(log,
				"  diag %s: Restart Manager found %u owner(s), but allocation failed\n",
				label, (unsigned)needed);
			pRmEndSession(session);
			FreeLibrary(dll);
			return;
		}
		count = needed;
		rc = pRmGetList(session, &needed, &count, infos,
			&reboot_reasons);
		if (rc == ERROR_SUCCESS)
		{
			M_Update_HelperLog(log,
				"  diag %s: Restart Manager lock owner count=%u reboot_reasons=0x%lx\n",
				label, (unsigned)count, (unsigned long)reboot_reasons);
			for (i = 0; i < count; i++)
			{
				char app[QSSM_CCH_RM_MAX_APP_NAME * 2];
				char svc[QSSM_CCH_RM_MAX_SVC_NAME * 2];

				M_Update_WideToAnsi(infos[i].strAppName, app,
					sizeof(app));
				M_Update_WideToAnsi(infos[i].strServiceShortName, svc,
					sizeof(svc));
				M_Update_HelperLog(log,
					"  diag %s:   pid=%lu app=%s service=%s type=%s status=0x%lx restartable=%s\n",
					label, (unsigned long)infos[i].Process.dwProcessId,
					app[0] ? app : "(unknown)",
					svc[0] ? svc : "(none)",
					M_Update_WinRmAppTypeName(infos[i].ApplicationType),
					(unsigned long)infos[i].AppStatus,
					infos[i].bRestartable ? "yes" : "no");
			}
		}
		else
		{
			M_Update_HelperLog(log,
				"  diag %s: Restart Manager get-list failed %lu (%s)\n",
				label, (unsigned long)rc, M_Update_WinErrorName(rc));
		}
		free(infos);
	}
	else if (rc == ERROR_SUCCESS)
	{
		M_Update_HelperLog(log,
			"  diag %s: Restart Manager reports no lock owners\n", label);
	}
	else
	{
		M_Update_HelperLog(log,
			"  diag %s: Restart Manager initial get-list failed %lu (%s)\n",
			label, (unsigned long)rc, M_Update_WinErrorName(rc));
	}

	pRmEndSession(session);
	FreeLibrary(dll);
}
#endif

/*
 * Log everything we can learn about a file when a replace/backup fails, so a
 * helper log is enough to tell a transient lock from a permissions / cloud
 * sync / Controlled Folder Access block without remote debugging.
 */
static void M_Update_HelperLogFileDiag(FILE *log, const char *label,
	const char *path)
{
	if (!log)
		return;

#ifdef _WIN32
	{
		DWORD attrs = GetFileAttributesA(path);
		HANDLE h;

		if (attrs == INVALID_FILE_ATTRIBUTES)
		{
			DWORD e = GetLastError();

			M_Update_HelperLog(log,
				"  diag %s: GetFileAttributes failed %lu (%s) [%s]\n",
				label, (unsigned long)e, M_Update_WinErrorName(e), path);
			return;
		}
		M_Update_HelperLog(log, "  diag %s: attrs=0x%lx%s%s%s%s [%s]\n",
			label, (unsigned long)attrs,
			(attrs & FILE_ATTRIBUTE_READONLY) ? " readonly" : "",
			(attrs & FILE_ATTRIBUTE_HIDDEN) ? " hidden" : "",
			(attrs & FILE_ATTRIBUTE_SYSTEM) ? " system" : "",
			(attrs & FILE_ATTRIBUTE_REPARSE_POINT) ? " reparse" : "", path);

		/*
		 * Opening with DELETE access and no sharing distinguishes the causes:
		 * SHARING_VIOLATION => another process holds it open; ACCESS_DENIED =>
		 * a permission/ACL/Controlled-Folder-Access block; success => neither.
		 */
		h = CreateFileA(path, DELETE, 0, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, NULL);
		if (h == INVALID_HANDLE_VALUE)
		{
			DWORD e = GetLastError();

			M_Update_HelperLog(log,
				"  diag %s: open(DELETE,share=none) failed %lu (%s)%s\n",
				label, (unsigned long)e, M_Update_WinErrorName(e),
				e == ERROR_SHARING_VIOLATION ?
				" -> held open by another process (AV/indexer/cloud sync/second instance)" :
				(e == ERROR_ACCESS_DENIED ?
				" -> permission / ACL / Controlled Folder Access block" : ""));
			if (e == ERROR_SHARING_VIOLATION ||
				e == ERROR_LOCK_VIOLATION ||
				e == ERROR_USER_MAPPED_FILE)
				M_Update_HelperLogFileLockOwners(log, label, path);
		}
		else
		{
			M_Update_HelperLog(log,
				"  diag %s: open(DELETE,share=none) OK -> not locked, have delete rights\n",
				label);
			CloseHandle(h);
		}
	}
#else
	{
		struct stat st;

		if (stat(path, &st) != 0)
		{
			M_Update_HelperLog(log, "  diag %s: stat failed: %s [%s]\n",
				label, strerror(errno), path);
			return;
		}
		M_Update_HelperLog(log,
			"  diag %s: mode=0%o size=%lld writable=%s [%s]\n", label,
			(unsigned)(st.st_mode & 07777), (long long)st.st_size,
			access(path, W_OK) == 0 ? "yes" : "no", path);
	}
#endif
}

static qboolean M_Update_HelperPreflightTargets(
	const updateapplymanifest_t *manifest, FILE *log, char *error,
	size_t error_size)
{
	int i;

	M_Update_HelperLog(log, "preflight: checking %d target file(s)\n",
		manifest->num_files);
	for (i = 0; i < manifest->num_files; i++)
	{
		char src_path[MAX_OSPATH];
		char live_path[MAX_OSPATH];
		int live_type;

		if (!M_Update_JoinPath(src_path, sizeof(src_path),
			manifest->extract_dir, manifest->files[i].src) ||
			!M_Update_JoinPath(live_path, sizeof(live_path),
			manifest->live_dir, manifest->files[i].dst))
		{
			q_strlcpy(error, "update preflight path too long",
				error_size);
			return false;
		}

		if (!(Sys_FileType(src_path) & FS_ENT_FILE))
		{
			q_snprintf(error, error_size, "staged file missing: %s",
				manifest->files[i].src);
			M_Update_HelperLog(log, "preflight failed for %s: %s\n",
				manifest->files[i].dst, error);
			M_Update_HelperLogFileDiag(log, "staged-src", src_path);
			return false;
		}

		live_type = Sys_FileType(live_path);
		if (live_type && !(live_type & FS_ENT_FILE))
		{
			q_snprintf(error, error_size,
				"live target is not a regular file: %s",
				manifest->files[i].dst);
			M_Update_HelperLog(log, "preflight failed for %s: %s\n",
				manifest->files[i].dst, error);
			M_Update_HelperLogFileDiag(log, "live", live_path);
			return false;
		}

#ifdef _WIN32
		if (live_type & FS_ENT_FILE)
		{
			DWORD err = ERROR_SUCCESS;
			DWORD waited_ms = 0;
			qboolean replaceable = false;
			int attempts = 0;
			int attempt;

			for (attempt = 0; attempt < UPDATE_WIN_REPLACE_RETRY_ATTEMPTS;
				attempt++)
			{
				DWORD delay_ms;

				if (M_Update_WinProbeReplaceable(live_path, &err))
				{
					replaceable = true;
					break;
				}
				attempts = attempt + 1;
				if (!M_Update_WinReplaceRetryable(err))
					break;
				delay_ms = M_Update_WinReplaceRetryDelayMS(attempt);
				Sleep(delay_ms);
				waited_ms += delay_ms;
			}
			if (!replaceable)
			{
				q_snprintf(error, error_size,
					"preflight blocked for %s after %d attempt(s)/%lu ms: "
					"%lu (%s)",
					manifest->files[i].dst, attempts,
					(unsigned long)waited_ms, (unsigned long)err,
					M_Update_WinErrorName(err));
				M_Update_HelperLog(log, "preflight failed for %s: %s\n",
					manifest->files[i].dst, error);
				M_Update_HelperLogFileDiag(log, "live", live_path);
				M_Update_HelperLogFileDiag(log, "staged-src", src_path);
				return false;
			}
			if (waited_ms)
				M_Update_HelperLog(log,
					"preflight: waited %lu ms for %s to become replaceable\n",
					(unsigned long)waited_ms, manifest->files[i].dst);
		}
#endif
	}

	M_Update_HelperLog(log, "preflight: all targets replaceable\n");
	return true;
}

static qboolean M_Update_WriteRecoveryManifest(updateapplymanifest_t *manifest,
	FILE *log, char *error, size_t error_size)
{
	if (!manifest->recovery_manifest[0])
		return true;

	if (!M_Update_WriteApplyManifest(manifest, manifest->recovery_manifest,
		error, error_size))
	{
		M_Update_HelperLog(log, "recovery manifest write failed: %s\n",
			error);
		return false;
	}

	return true;
}

static qboolean M_Update_HelperRestore(updateapplymanifest_t *manifest,
	int applied_count, FILE *log, char *error, size_t error_size)
{
	int i;
	qboolean ok = true;

	for (i = applied_count - 1; i >= 0; i--)
	{
		char live_path[MAX_OSPATH];
		char backup_path[MAX_OSPATH];

		if (!M_Update_JoinPath(live_path, sizeof(live_path),
			manifest->live_dir, manifest->files[i].dst) ||
			!M_Update_JoinPath(backup_path, sizeof(backup_path),
			manifest->backup_dir, manifest->files[i].dst))
		{
			ok = false;
			continue;
		}

		if (manifest->files[i].backup_exists)
		{
			char restore_error[256] = "";
			if (!M_Update_CopyFileAtomic(backup_path, live_path,
				manifest->files[i].executable, restore_error,
				sizeof(restore_error)))
			{
				M_Update_HelperLog(log, "restore failed for %s: %s\n",
					manifest->files[i].dst, restore_error);
				ok = false;
			}
		}
		else if (M_Update_RemoveFile(live_path) != 0 && errno != ENOENT)
		{
			M_Update_HelperLog(log, "remove failed during restore for %s\n",
				live_path);
			ok = false;
		}
	}

	if (!ok)
		q_strlcpy(error, "update failed and restore was incomplete",
			error_size);
	return ok;
}

static qboolean M_Update_HelperApply(updateapplymanifest_t *manifest,
	FILE *log, char *error, size_t error_size)
{
	int i;
	char probe_error[256] = "";

	M_Update_HelperLog(log, "apply: live_dir=%s\n", manifest->live_dir);
	M_Update_HelperLog(log, "apply: extract_dir=%s\n", manifest->extract_dir);
	M_Update_HelperLog(log, "apply: backup_dir=%s\n", manifest->backup_dir);
	M_Update_HelperLog(log, "apply: %d file(s) to replace\n",
		manifest->num_files);
	if (M_Update_DirectoryWritable(manifest->live_dir, probe_error,
		sizeof(probe_error)))
		M_Update_HelperLog(log, "apply: live_dir create-file probe OK\n");
	else
		M_Update_HelperLog(log, "apply: live_dir create-file probe FAILED: %s\n",
			probe_error);

	if (!M_Update_HelperPreflightTargets(manifest, log, error, error_size))
		return false;

	if (!M_Update_CreateDirectoryPath(manifest->backup_dir, error, error_size))
		return false;
	manifest->complete = false;
	manifest->startup_attempted = false;
	manifest->startup_confirmed = false;
	if (!M_Update_WriteRecoveryManifest(manifest, log, error, error_size))
		return false;

	for (i = 0; i < manifest->num_files; i++)
	{
		char src_path[MAX_OSPATH];
		char live_path[MAX_OSPATH];
		char backup_path[MAX_OSPATH];

		if (!M_Update_JoinPath(src_path, sizeof(src_path),
			manifest->extract_dir, manifest->files[i].src) ||
			!M_Update_JoinPath(live_path, sizeof(live_path),
			manifest->live_dir, manifest->files[i].dst) ||
			!M_Update_JoinPath(backup_path, sizeof(backup_path),
			manifest->backup_dir, manifest->files[i].dst))
		{
			q_strlcpy(error, "update path too long", error_size);
			M_Update_HelperRestore(manifest, i, log, error, error_size);
			return false;
		}

		if (!(Sys_FileType(src_path) & FS_ENT_FILE))
		{
			q_snprintf(error, error_size, "staged file missing: %s",
				manifest->files[i].src);
			M_Update_HelperRestore(manifest, i, log, error, error_size);
			return false;
		}

		if (manifest->files[i].sha256[0] &&
			!M_VerifySHA256File(src_path, manifest->files[i].sha256, false,
			NULL))
		{
			q_snprintf(error, error_size,
				"staged file failed verification: %s",
				manifest->files[i].src);
			M_Update_HelperLog(log, "%s\n", error);
			M_Update_HelperRestore(manifest, i, log, error, error_size);
			return false;
		}

		if (Sys_FileType(live_path) & FS_ENT_FILE)
		{
			if (!M_Update_CopyFileAtomic(live_path, backup_path,
				manifest->files[i].executable, error, error_size))
			{
				M_Update_HelperLog(log, "backup failed for %s: %s\n",
					manifest->files[i].dst, error);
				M_Update_HelperLogFileDiag(log, "live", live_path);
				M_Update_HelperLogFileDiag(log, "backup-dst", backup_path);
				M_Update_HelperRestore(manifest, i, log, error, error_size);
				return false;
			}
			manifest->files[i].backup_exists = true;
			if (!M_Update_WriteRecoveryManifest(manifest, log, error,
				error_size))
			{
				M_Update_HelperRestore(manifest, i, log, error,
					error_size);
				return false;
			}
		}

		M_Update_HelperLog(log, "applying %s -> %s\n",
			manifest->files[i].src, manifest->files[i].dst);
		manifest->files[i].applied = true;
		if (!M_Update_WriteRecoveryManifest(manifest, log, error,
			error_size))
		{
			M_Update_HelperRestore(manifest, i + 1, log, error,
				error_size);
			return false;
		}
		if (!M_Update_CopyFileAtomic(src_path, live_path,
			manifest->files[i].executable, error, error_size))
		{
			M_Update_HelperLog(log, "apply failed for %s: %s\n",
				manifest->files[i].dst, error);
			M_Update_HelperLogFileDiag(log, "live", live_path);
			M_Update_HelperLogFileDiag(log, "staged-src", src_path);
			M_Update_HelperRestore(manifest, i + 1, log, error, error_size);
			return false;
		}
	}

	manifest->complete = true;
	manifest->startup_attempted = false;
	manifest->startup_confirmed = false;
	if (!M_Update_WriteRecoveryManifest(manifest, log, error, error_size))
	{
		M_Update_HelperRestore(manifest, manifest->num_files, log, error,
			error_size);
		return false;
	}

	return true;
}

static qboolean M_Update_RestoreAppliedFiles(updateapplymanifest_t *manifest,
	FILE *log, char *error, size_t error_size)
{
	int i;
	qboolean ok = true;

	for (i = manifest->num_files - 1; i >= 0; i--)
	{
		char live_path[MAX_OSPATH];
		char backup_path[MAX_OSPATH];

		if (!manifest->files[i].applied)
			continue;
		if (!M_Update_JoinPath(live_path, sizeof(live_path),
			manifest->live_dir, manifest->files[i].dst) ||
			!M_Update_JoinPath(backup_path, sizeof(backup_path),
			manifest->backup_dir, manifest->files[i].dst))
		{
			ok = false;
			continue;
		}

		if (manifest->files[i].backup_exists)
		{
			char restore_error[256] = "";

			if (!M_Update_CopyFileAtomic(backup_path, live_path,
				manifest->files[i].executable, restore_error,
				sizeof(restore_error)))
			{
				M_Update_HelperLog(log, "startup restore failed for %s: %s\n",
					manifest->files[i].dst, restore_error);
				ok = false;
			}
		}
		else if (M_Update_RemoveFile(live_path) != 0 && errno != ENOENT)
		{
			M_Update_HelperLog(log, "startup restore remove failed for %s\n",
				live_path);
			ok = false;
		}
	}

	if (!ok)
		q_strlcpy(error, "startup update recovery was incomplete",
			error_size);
	return ok;
}

static qboolean M_Update_BuildRecoveryPaths(const char *live_dir,
	char *recovery_dir, size_t recovery_dir_size, char *manifest_path,
	size_t manifest_path_size)
{
	return M_Update_JoinPath(recovery_dir, recovery_dir_size, live_dir,
		UPDATE_RECOVERY_DIR) &&
		M_Update_JoinPath(manifest_path, manifest_path_size, recovery_dir,
		UPDATE_RECOVERY_MANIFEST);
}

static qboolean M_Update_StagePathLooksOwned(const char *stage_dir)
{
	char parent[MAX_OSPATH];

	if (!stage_dir || !*stage_dir ||
		!M_Update_StageDirNameOwned(M_Update_PathBaseName(stage_dir)))
		return false;

	M_Update_PathDirName(stage_dir, parent, sizeof(parent));
	return !q_strcasecmp(M_Update_PathBaseName(parent), UPDATE_STAGE_ROOT);
}

static qboolean M_Update_ValidateApplyManifestPaths(
	const updateapplymanifest_t *manifest, const char *expected_live_dir,
	char *error, size_t error_size)
{
	char recovery_dir[MAX_OSPATH];
	char expected_manifest[MAX_OSPATH];
	char expected_backup[MAX_OSPATH];
	char stage_dir[MAX_OSPATH];

	if (!M_Update_PathIsAbsolute(manifest->live_dir) ||
		!M_Update_PathIsAbsolute(manifest->extract_dir) ||
		!M_Update_PathIsAbsolute(manifest->backup_dir) ||
		(manifest->log_path[0] &&
		!M_Update_PathIsAbsolute(manifest->log_path)) ||
		(manifest->recovery_manifest[0] &&
		!M_Update_PathIsAbsolute(manifest->recovery_manifest)))
	{
		q_strlcpy(error, "update manifest paths must be absolute",
			error_size);
		return false;
	}

	if (expected_live_dir &&
		!M_Update_PathsEqual(manifest->live_dir, expected_live_dir))
	{
		q_strlcpy(error, "update manifest is for another install",
			error_size);
		return false;
	}

	if (!M_Update_BuildRecoveryPaths(manifest->live_dir, recovery_dir,
		sizeof(recovery_dir), expected_manifest, sizeof(expected_manifest)) ||
		!M_Update_JoinPath(expected_backup, sizeof(expected_backup),
		recovery_dir, UPDATE_HELPER_BACKUP_DIR))
	{
		q_strlcpy(error, "update manifest recovery path too long",
			error_size);
		return false;
	}

	if (!M_Update_PathsEqual(manifest->backup_dir, expected_backup) ||
		(manifest->recovery_manifest[0] &&
		!M_Update_PathsEqual(manifest->recovery_manifest,
		expected_manifest)))
	{
		q_strlcpy(error, "update manifest recovery path is invalid",
			error_size);
		return false;
	}

	if (q_strcasecmp(M_Update_PathBaseName(manifest->extract_dir),
		"extract"))
	{
		q_strlcpy(error, "update manifest extract path is invalid",
			error_size);
		return false;
	}

	M_Update_PathDirName(manifest->extract_dir, stage_dir,
		sizeof(stage_dir));
	if (!M_Update_StagePathLooksOwned(stage_dir))
	{
		q_strlcpy(error, "update manifest staging path is invalid",
			error_size);
		return false;
	}

	if (manifest->log_path[0])
	{
		char log_dir[MAX_OSPATH];

		M_Update_PathDirName(manifest->log_path, log_dir,
			sizeof(log_dir));
		if (q_strcasecmp(M_Update_PathBaseName(manifest->log_path),
			UPDATE_HELPER_LOG) ||
			!M_Update_PathsEqual(log_dir, stage_dir))
		{
			q_strlcpy(error, "update manifest log path is invalid",
				error_size);
			return false;
		}
	}

	return true;
}

static void M_Update_RemoveManifestStageDir(const updateapplymanifest_t *manifest)
{
	char stage_dir[MAX_OSPATH];

	if (!manifest->extract_dir[0] ||
		q_strcasecmp(M_Update_PathBaseName(manifest->extract_dir), "extract"))
		return;

	M_Update_PathDirName(manifest->extract_dir, stage_dir, sizeof(stage_dir));
	if (M_Update_StagePathLooksOwned(stage_dir))
		M_Update_RemoveTree(stage_dir);
}

static void M_Update_RemoveManifestRecoveryDir(const updateapplymanifest_t *manifest)
{
	char recovery_dir[MAX_OSPATH];
	char expected_dir[MAX_OSPATH];

	if (!manifest->backup_dir[0] ||
		q_strcasecmp(M_Update_PathBaseName(manifest->backup_dir),
		UPDATE_HELPER_BACKUP_DIR))
		return;

	M_Update_PathDirName(manifest->backup_dir, recovery_dir,
		sizeof(recovery_dir));
	if (q_strcasecmp(M_Update_PathBaseName(recovery_dir),
		UPDATE_RECOVERY_DIR))
		return;
	if (!M_Update_JoinPath(expected_dir, sizeof(expected_dir),
		manifest->live_dir, UPDATE_RECOVERY_DIR) ||
		!M_Update_PathsEqual(recovery_dir, expected_dir))
		return;

	M_Update_RemoveTree(recovery_dir);
}

static qboolean M_Update_ManifestHasAppliedImage(
	const updateapplymanifest_t *manifest)
{
	int i;

	for (i = 0; i < manifest->num_files; i++)
		if (manifest->files[i].applied && manifest->files[i].executable)
			return true;
	return false;
}

static void M_Update_InitApplyLock(updateapplylock_t *lock)
{
	memset(lock, 0, sizeof(*lock));
#ifdef _WIN32
	lock->handle = INVALID_HANDLE_VALUE;
#else
	lock->fd = -1;
#endif
}

static qboolean M_Update_AcquireApplyLock(const char *live_dir,
	updateapplylock_t *lock, char *error, size_t error_size)
{
	char recovery_dir[MAX_OSPATH];
	char recovery_manifest[MAX_OSPATH];
	char lock_path[MAX_OSPATH];

	if (!M_Update_BuildRecoveryPaths(live_dir, recovery_dir,
		sizeof(recovery_dir), recovery_manifest, sizeof(recovery_manifest)) ||
		!M_Update_JoinPath(lock_path, sizeof(lock_path), recovery_dir,
		UPDATE_APPLY_LOCK))
	{
		q_strlcpy(error, "update lock path too long", error_size);
		return false;
	}

	if (!M_Update_CreateDirectoryPath(recovery_dir, error, error_size))
		return false;

#ifdef _WIN32
	lock->handle = CreateFileA(lock_path, GENERIC_READ | GENERIC_WRITE,
		0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (lock->handle == INVALID_HANDLE_VALUE)
	{
		DWORD err = GetLastError();

		if (err == ERROR_SHARING_VIOLATION)
			q_strlcpy(error, "another update is already applying",
				error_size);
		else
			q_snprintf(error, error_size,
				"unable to acquire update lock: %lu",
				(unsigned long)err);
		return false;
	}
#else
	lock->fd = open(lock_path, O_RDWR | O_CREAT, 0644);
	if (lock->fd < 0)
	{
		q_snprintf(error, error_size, "unable to open update lock: %s",
			strerror(errno));
		return false;
	}
	{
		struct flock fl;

		memset(&fl, 0, sizeof(fl));
		fl.l_type = F_WRLCK;
		fl.l_whence = SEEK_SET;
		if (fcntl(lock->fd, F_SETLK, &fl) != 0)
		{
			if (errno == EACCES || errno == EAGAIN)
				q_strlcpy(error, "another update is already applying",
					error_size);
			else
				q_snprintf(error, error_size,
					"unable to acquire update lock: %s",
					strerror(errno));
			close(lock->fd);
			lock->fd = -1;
			return false;
		}
	}
#endif

	q_strlcpy(lock->path, lock_path, sizeof(lock->path));
	return true;
}

static void M_Update_ReleaseApplyLock(updateapplylock_t *lock)
{
#ifdef _WIN32
	if (lock->handle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(lock->handle);
		lock->handle = INVALID_HANDLE_VALUE;
	}
#else
	if (lock->fd >= 0)
	{
		close(lock->fd);
		lock->fd = -1;
	}
#endif
	lock->path[0] = '\0';
}

/*
 * The startup recovery and confirmation steps run in two separate calls that
 * straddle Host_Init. Holding the same apply lock across that whole window
 * keeps a second instance (or the helper still applying) from racing the
 * file restore/confirm. The lock must always be released before the recovery
 * directory that contains the lock file is removed.
 */
static qboolean M_Update_HoldStartupLock(const char *live_dir, char *error,
	size_t error_size)
{
	if (update_startup_lock_active)
		return true;
	if (!update_startup_lock_initialized)
	{
		M_Update_InitApplyLock(&update_startup_lock);
		update_startup_lock_initialized = true;
	}
	if (!M_Update_AcquireApplyLock(live_dir, &update_startup_lock, error,
		error_size))
		return false;
	update_startup_lock_active = true;
	return true;
}

static void M_Update_ReleaseStartupLock(void)
{
	if (update_startup_lock_active)
	{
		M_Update_ReleaseApplyLock(&update_startup_lock);
		update_startup_lock_active = false;
	}
}

static qboolean M_Update_LaunchRestoredBuild(
	const updateapplymanifest_t *manifest, char *error, size_t error_size)
{
	char relaunch_path[MAX_OSPATH];

	if (!manifest->relaunch_dst[0] ||
		!M_Update_JoinPath(relaunch_path, sizeof(relaunch_path),
		manifest->live_dir, manifest->relaunch_dst))
	{
		q_strlcpy(error, "restored executable path is unavailable",
			error_size);
		return false;
	}
	return Sys_LaunchProgram(relaunch_path, manifest->live_dir, error,
		error_size);
}

void M_Update_RecoverAtStartup(void)
{
	updateapplymanifest_t manifest;
	char current_exe[MAX_OSPATH];
	char live_dir[MAX_OSPATH];
	char recovery_dir[MAX_OSPATH];
	char manifest_path[MAX_OSPATH];
	char error[512] = "";
	FILE *log = NULL;

	if (!Sys_GetExecutablePath(current_exe, sizeof(current_exe)))
		return;
	M_Update_PathDirName(current_exe, live_dir, sizeof(live_dir));
	if (!M_Update_BuildRecoveryPaths(live_dir, recovery_dir,
		sizeof(recovery_dir), manifest_path, sizeof(manifest_path)))
		return;
	if (!(Sys_FileType(manifest_path) & FS_ENT_FILE))
		return;

	if (!M_Update_ReadApplyManifest(manifest_path, &manifest, error,
		sizeof(error)))
	{
		Sys_Printf("QSS-M update recovery manifest is invalid: %s\n",
			error[0] ? error : "unknown error");
		return;
	}
	if (!manifest.recovery_manifest[0])
		q_strlcpy(manifest.recovery_manifest, manifest_path,
			sizeof(manifest.recovery_manifest));
	if (!M_Update_ValidateApplyManifestPaths(&manifest, live_dir, error,
		sizeof(error)))
	{
		Sys_Printf("QSS-M update recovery manifest is invalid: %s\n",
			error[0] ? error : "unknown error");
		return;
	}

	if (!M_Update_HoldStartupLock(live_dir, error, sizeof(error)))
	{
		Sys_Printf("QSS-M update is being applied; exiting until it completes.\n");
		exit(1);
	}

	if (manifest.log_path[0])
		log = fopen(manifest.log_path, "ab");

	if (manifest.complete)
	{
		if (manifest.startup_confirmed)
		{
			M_Update_HelperLog(log, "startup was already confirmed; retrying cleanup\n");
			Sys_Printf("QSS-M update was already confirmed; removing recovery backups.\n");
			if (log)
				fclose(log);
			M_Update_ReleaseStartupLock();
			M_Update_RemoveTree(recovery_dir);
			M_Update_RemoveManifestStageDir(&manifest);
			return;
		}

		if (manifest.startup_attempted)
		{
			Sys_Printf("Previous QSS-M update startup did not finish; restoring previous files.\n");
			M_Update_HelperLog(log, "startup confirmation missing; rolling back update\n");
			if (M_Update_RestoreAppliedFiles(&manifest, log, error,
				sizeof(error)))
			{
				qboolean restored_image =
					M_Update_ManifestHasAppliedImage(&manifest);
				char launch_error[512] = "";

				M_Update_HelperLog(log, "startup rollback restored previous files\n");
				Sys_Printf("QSS-M update recovery restored previous files.\n");
				if (log)
					fclose(log);
				M_Update_ReleaseStartupLock();
				M_Update_RemoveTree(recovery_dir);
				M_Update_RemoveManifestStageDir(&manifest);
				if (restored_image)
				{
					if (M_Update_LaunchRestoredBuild(&manifest, launch_error,
						sizeof(launch_error)))
					{
						Sys_Printf("Relaunched restored QSS-M build.\n");
						exit(0);
					}
					Sys_Printf("Unable to relaunch restored QSS-M build: %s\n",
						launch_error[0] ? launch_error : "unknown error");
					exit(1);
				}
				return;
			}

			update_startup_recovery_failed = true;
			M_Update_HelperLog(log, "startup rollback failed: %s\n",
				error);
			if (log)
				fclose(log);
			M_Update_ReleaseStartupLock();
			Sys_Printf("QSS-M update recovery failed: %s\n",
				error[0] ? error : "unknown error");
			exit(1);
		}

		manifest.startup_attempted = true;
		if (!M_Update_WriteRecoveryManifest(&manifest, log, error,
			sizeof(error)))
		{
			Sys_Printf("Unable to mark QSS-M update startup attempt: %s\n",
				error[0] ? error : "unknown error");
			if (log)
				fclose(log);
			M_Update_ReleaseStartupLock();
			exit(1);
		}
		update_startup_attempt_marked_this_process = true;
		M_Update_HelperLog(log, "startup attempt marked; waiting for host initialization confirmation\n");
		Sys_Printf("QSS-M update applied; waiting for startup confirmation before removing recovery backups.\n");
		if (log)
			fclose(log);
		return;
	}

	Sys_Printf("Incomplete QSS-M update found; restoring previous files.\n");
	M_Update_HelperLog(log, "startup recovery restoring incomplete update\n");
	if (M_Update_RestoreAppliedFiles(&manifest, log, error, sizeof(error)))
	{
		qboolean restored_image =
			M_Update_ManifestHasAppliedImage(&manifest);
		char launch_error[512] = "";

		M_Update_HelperLog(log, "startup recovery restored previous files\n");
		Sys_Printf("QSS-M update recovery restored previous files.\n");
		if (log)
			fclose(log);
		M_Update_ReleaseStartupLock();
		M_Update_RemoveTree(recovery_dir);
		M_Update_RemoveManifestStageDir(&manifest);
		if (restored_image)
		{
			if (M_Update_LaunchRestoredBuild(&manifest, launch_error,
				sizeof(launch_error)))
			{
				Sys_Printf("Relaunched restored QSS-M build.\n");
				exit(0);
			}
			Sys_Printf("Unable to relaunch restored QSS-M build: %s\n",
				launch_error[0] ? launch_error : "unknown error");
			exit(1);
		}
		return;
	}

	update_startup_recovery_failed = true;
	M_Update_HelperLog(log, "startup recovery failed: %s\n", error);
	if (log)
		fclose(log);
	M_Update_ReleaseStartupLock();
	Sys_Printf("QSS-M update recovery failed: %s\n",
		error[0] ? error : "unknown error");
	exit(1);
}

void M_Update_ConfirmStartup(void)
{
	updateapplymanifest_t manifest;
	char current_exe[MAX_OSPATH];
	char live_dir[MAX_OSPATH];
	char recovery_dir[MAX_OSPATH];
	char manifest_path[MAX_OSPATH];
	char error[512] = "";
	FILE *log = NULL;

	if (!Sys_GetExecutablePath(current_exe, sizeof(current_exe)))
		return;
	M_Update_PathDirName(current_exe, live_dir, sizeof(live_dir));
	if (!M_Update_BuildRecoveryPaths(live_dir, recovery_dir,
		sizeof(recovery_dir), manifest_path, sizeof(manifest_path)))
		return;
	if (!(Sys_FileType(manifest_path) & FS_ENT_FILE))
		return;
	if (!M_Update_ReadApplyManifest(manifest_path, &manifest, error,
		sizeof(error)))
		return;
	if (!manifest.recovery_manifest[0])
		q_strlcpy(manifest.recovery_manifest, manifest_path,
			sizeof(manifest.recovery_manifest));
	if (!M_Update_ValidateApplyManifestPaths(&manifest, live_dir, error,
		sizeof(error)) || !manifest.complete)
		return;
	if (update_startup_recovery_failed ||
		!update_startup_attempt_marked_this_process)
		return;

	if (!M_Update_HoldStartupLock(live_dir, error, sizeof(error)))
		return;

	if (manifest.log_path[0])
		log = fopen(manifest.log_path, "ab");

	manifest.startup_attempted = true;
	manifest.startup_confirmed = true;
	if (!M_Update_WriteRecoveryManifest(&manifest, log, error,
		sizeof(error)))
	{
		M_Update_HelperLog(log, "startup confirmation write failed: %s\n",
			error);
		Sys_Printf("QSS-M update startup confirmation failed: %s\n",
			error[0] ? error : "unknown error");
		if (log)
			fclose(log);
		M_Update_ReleaseStartupLock();
		return;
	}

	M_Update_HelperLog(log, "startup confirmed updated build\n");
	Sys_Printf("QSS-M update confirmed; removing recovery backups.\n");
	if (log)
		fclose(log);
	M_Update_ReleaseStartupLock();
	M_Update_RemoveTree(recovery_dir);
	M_Update_RemoveManifestStageDir(&manifest);
}

void M_Update_PruneStagingAtStartup(void)
{
#ifdef _WIN32
	M_Update_SweepKnownStaleReplacementsAtStartup();
#endif
	if (com_basedir[0])
		M_Update_PruneStageRoot(NULL);
}

static qboolean M_Update_PrepareApplyHelper(const updatereleaseinfo_t *info,
	const char *stage_dir, const char *extract_dir, char *helper_path,
	size_t helper_path_size, char *manifest_path, size_t manifest_path_size,
	char *error, size_t error_size)
{
	updateapplymanifest_t manifest;
	char current_exe[MAX_OSPATH];
	char live_dir[MAX_OSPATH];
	char live_exe_name[64];
	char absolute_stage_dir[MAX_OSPATH];
	char absolute_extract_dir[MAX_OSPATH];
	char helper_dir[MAX_OSPATH];
	char recovery_dir[MAX_OSPATH];
	char backup_dir[MAX_OSPATH];
	char log_path[MAX_OSPATH];
	char recovery_manifest[MAX_OSPATH];
	const char *helper_name =
#ifdef _WIN32
		"qssm-helper.exe";
#else
		"qssm-update-helper";
#endif

	memset(&manifest, 0, sizeof(manifest));

	if (!M_Update_GetLiveInstallPaths(current_exe, sizeof(current_exe),
		live_dir, sizeof(live_dir), live_exe_name, sizeof(live_exe_name),
		error, error_size))
		return false;

	if (!M_Update_MakeAbsolutePath(stage_dir, absolute_stage_dir,
		sizeof(absolute_stage_dir)) ||
		!M_Update_MakeAbsolutePath(extract_dir, absolute_extract_dir,
		sizeof(absolute_extract_dir)))
	{
		q_strlcpy(error, "unable to resolve absolute update staging paths",
			error_size);
		return false;
	}

	if (!M_Update_JoinPath(helper_dir, sizeof(helper_dir), absolute_stage_dir,
		UPDATE_HELPER_DIR) ||
		!M_Update_JoinPath(helper_path, helper_path_size, helper_dir,
		helper_name) ||
		!M_Update_JoinPath(manifest_path, manifest_path_size,
			absolute_stage_dir, UPDATE_HELPER_MANIFEST) ||
		!M_Update_JoinPath(recovery_dir, sizeof(recovery_dir),
			live_dir, UPDATE_RECOVERY_DIR) ||
		!M_Update_JoinPath(backup_dir, sizeof(backup_dir),
			recovery_dir, UPDATE_HELPER_BACKUP_DIR) ||
		!M_Update_JoinPath(log_path, sizeof(log_path), absolute_stage_dir,
			UPDATE_HELPER_LOG) ||
		!M_Update_JoinPath(recovery_manifest, sizeof(recovery_manifest),
			recovery_dir, UPDATE_RECOVERY_MANIFEST))
	{
		q_strlcpy(error, "update helper path too long", error_size);
		return false;
	}

	if (!M_Update_CreateDirectoryPath(helper_dir, error, error_size))
		return false;

	if (!M_Update_CopyFileAtomic(current_exe, helper_path, true, error,
		error_size))
		return false;
	if (!M_Update_CopyHelperRuntimeFiles(info->platform, live_dir, extract_dir,
		helper_dir, error, error_size))
		return false;

	manifest.parent_pid = Sys_GetProcessId();
	if (q_strlcpy(manifest.live_dir, live_dir, sizeof(manifest.live_dir)) >=
		sizeof(manifest.live_dir) ||
		q_strlcpy(manifest.extract_dir, absolute_extract_dir,
		sizeof(manifest.extract_dir)) >= sizeof(manifest.extract_dir) ||
		q_strlcpy(manifest.backup_dir, backup_dir,
		sizeof(manifest.backup_dir)) >= sizeof(manifest.backup_dir) ||
		q_strlcpy(manifest.log_path, log_path, sizeof(manifest.log_path)) >=
		sizeof(manifest.log_path) ||
		q_strlcpy(manifest.recovery_manifest, recovery_manifest,
		sizeof(manifest.recovery_manifest)) >=
		sizeof(manifest.recovery_manifest))
	{
		q_strlcpy(error, "update manifest path too long", error_size);
		return false;
	}

	if (!M_Update_BuildApplyFileList(&manifest, info->platform,
		live_exe_name, absolute_extract_dir, error, error_size))
		return false;

	return M_Update_WriteApplyManifest(&manifest, manifest_path, error,
		error_size);
}

int M_UpdateHelperMain(int argc, char **argv)
{
	updateapplymanifest_t manifest;
	char error[512] = "";
	char relaunch_path[MAX_OSPATH];
	char fallback_log_path[MAX_OSPATH] = "";
	FILE *log = NULL;
	updateapplylock_t apply_lock;
	qboolean success;

	if (argc < 3 || strcmp(argv[1], UPDATE_HELPER_ARG))
		return 2;

	M_Update_InitApplyLock(&apply_lock);

	{
		char manifest_dir[MAX_OSPATH];

		M_Update_PathDirName(argv[2], manifest_dir, sizeof(manifest_dir));
		if (manifest_dir[0] &&
			M_Update_JoinPath(fallback_log_path, sizeof(fallback_log_path),
			manifest_dir, UPDATE_HELPER_LOG))
			log = fopen(fallback_log_path, "ab");
	}
	M_Update_HelperLog(log, "QSS-M update helper starting\n");

	if (!M_Update_ReadApplyManifest(argv[2], &manifest, error, sizeof(error)))
	{
		M_Update_HelperLog(log, "manifest read failed: %s\n", error);
		if (log)
			fclose(log);
		fprintf(stderr, "QSS-M update helper: %s\n", error);
		return 1;
	}
	if (!M_Update_ValidateApplyManifestPaths(&manifest, NULL, error,
		sizeof(error)))
	{
		M_Update_HelperLog(log, "manifest validation failed: %s\n", error);
		if (log)
			fclose(log);
		fprintf(stderr, "QSS-M update helper: %s\n", error);
		return 1;
	}
	if (!manifest.recovery_manifest[0])
	{
		M_Update_HelperLog(log,
			"manifest validation failed: update manifest has no recovery path\n");
		if (log)
			fclose(log);
		fprintf(stderr,
			"QSS-M update helper: update manifest has no recovery path\n");
		return 1;
	}
	if (!log && manifest.log_path[0])
	{
		M_Update_CreateDirectoryPath(manifest.backup_dir, error,
			sizeof(error));
		log = fopen(manifest.log_path, "ab");
	}

	if (argc >= 4 && argv[3] && argv[3][0] &&
		!M_Update_ParseWaitToken(argv[3], &manifest.parent_wait_token))
	{
		q_strlcpy(error, "invalid parent wait token", sizeof(error));
		M_Update_HelperLog(log, "update failed: %s\n", error);
		if (log)
			fclose(log);
		fprintf(stderr, "QSS-M update helper: %s\n", error);
		return 1;
	}

	if (!Sys_UpdateWaitForParentExit(manifest.parent_wait_token,
		manifest.parent_pid, 30000))
	{
		q_strlcpy(error, "original QSS-M process did not exit in time",
			sizeof(error));
		M_Update_HelperLog(log, "update failed: %s\n", error);
		if (log)
			fclose(log);
		fprintf(stderr, "QSS-M update helper: %s\n", error);
		return 1;
	}

	if (!M_Update_AcquireApplyLock(manifest.live_dir, &apply_lock, error,
		sizeof(error)))
	{
		M_Update_HelperLog(log, "update failed: %s\n", error);
		if (log)
			fclose(log);
		fprintf(stderr, "QSS-M update helper: %s\n", error);
		return 1;
	}

	success = M_Update_HelperApply(&manifest, log, error, sizeof(error));
	if (!success)
	{
		M_Update_ReleaseApplyLock(&apply_lock);
		M_Update_HelperLog(log, "update failed: %s\n", error);
		if (log)
			fclose(log);
		fprintf(stderr, "QSS-M update helper: %s\n", error);
		return 1;
	}

	M_Update_HelperLog(log, "update applied successfully\n");
	if (manifest.relaunch_dst[0] &&
		M_Update_JoinPath(relaunch_path, sizeof(relaunch_path),
		manifest.live_dir, manifest.relaunch_dst))
	{
		char selftest_error[512] = "";

		if (!Sys_RunUpdateSelfTest(relaunch_path, manifest.live_dir,
			UPDATE_SELFTEST_ARG, UPDATE_SELFTEST_TIMEOUT_MS,
			selftest_error, sizeof(selftest_error)))
		{
			char rollback_error[512] = "";
			qboolean rollback_ok;

			M_Update_HelperLog(log,
				"updated executable self-test failed: %s\n",
				selftest_error[0] ? selftest_error : "unknown error");
			manifest.complete = false;
			manifest.startup_attempted = false;
			manifest.startup_confirmed = false;
			if (!M_Update_WriteRecoveryManifest(&manifest, log,
				rollback_error, sizeof(rollback_error)))
			{
				M_Update_HelperLog(log,
					"unable to mark self-test rollback state: %s\n",
					rollback_error);
			}

			rollback_error[0] = '\0';
			rollback_ok = M_Update_HelperRestore(&manifest,
				manifest.num_files, log, rollback_error,
				sizeof(rollback_error));
			if (rollback_ok)
			{
				M_Update_HelperLog(log,
					"self-test rollback restored previous files\n");
				M_Update_ReleaseApplyLock(&apply_lock);
				M_Update_RemoveManifestRecoveryDir(&manifest);
				if (!Sys_LaunchProgram(relaunch_path,
					manifest.live_dir, rollback_error,
					sizeof(rollback_error)))
				{
					M_Update_HelperLog(log,
						"restored build relaunch failed: %s\n",
						rollback_error);
				}
			}
			else
			{
				M_Update_HelperLog(log,
					"self-test rollback failed: %s\n",
					rollback_error[0] ? rollback_error :
					"unknown error");
				M_Update_ReleaseApplyLock(&apply_lock);
			}

			if (log)
				fclose(log);
			fprintf(stderr,
				"QSS-M update helper: updated executable self-test failed: %s\n",
				selftest_error[0] ? selftest_error : "unknown error");
			return 1;
		}

		if (!Sys_LaunchProgram(relaunch_path, manifest.live_dir, error,
			sizeof(error)))
			M_Update_HelperLog(log, "relaunch failed: %s\n", error);
	}

	M_Update_ReleaseApplyLock(&apply_lock);
	if (log)
		fclose(log);
	return 0;
}

static void M_Update_CopyAssetDigest(const jsonentry_t *asset,
	updateassetinfo_t *out)
{
	const char *digest = JSON_FindString(asset, "digest");

	if (!digest)
		return;
	q_strlcpy(out->digest, digest, sizeof(out->digest));
	M_Update_SHA256FromDigest(digest, out->sha256, sizeof(out->sha256));
}

static qboolean M_Update_FindExactReleaseAsset(const jsonentry_t *root,
	const char *asset_name, updateassetinfo_t *out)
{
	const jsonentry_t *assets;
	const jsonentry_t *asset;

	memset(out, 0, sizeof(*out));
	assets = JSON_Find(root, "assets", JSON_ARRAY);
	if (!assets)
		return false;

	for (asset = assets->firstchild; asset; asset = asset->next)
	{
		const char *name, *url;
		const double *size;

		if (asset->type != JSON_OBJECT)
			continue;
		name = JSON_FindString(asset, "name");
		if (!name || q_strcasecmp(name, asset_name))
			continue;
		url = JSON_FindString(asset, "browser_download_url");
		if (!M_Update_AssetUrlAllowed(url))
			return false;

		q_strlcpy(out->name, name, sizeof(out->name));
		q_strlcpy(out->url, url, sizeof(out->url));
		size = JSON_FindNumber(asset, "size");
		out->size = size ? *size : 0.0;
		M_Update_CopyAssetDigest(asset, out);
		return true;
	}

	return false;
}

static qboolean M_Update_FetchReleaseAssetMemory(
	const updateassetinfo_t *asset, versionhttpmem_t *mem, size_t max_bytes,
	char *error, size_t error_size)
{
	if (!asset->url[0])
	{
		q_snprintf(error, error_size, "release asset %s has no URL",
			asset->name[0] ? asset->name : "(unknown)");
		return false;
	}
	if (asset->size > 0.0 && asset->size > (double)max_bytes)
	{
		q_snprintf(error, error_size, "release asset %s is too large",
			asset->name);
		return false;
	}

	if (!M_Version_GitHubHttpGet(asset->url, mem, error, error_size,
		max_bytes))
		return false;

	if (asset->sha256[0] && !M_Update_VerifySHA256Memory(mem->memory,
		mem->size, asset->sha256))
	{
		q_snprintf(error, error_size, "release asset %s digest mismatch",
			asset->name);
		free(mem->memory);
		memset(mem, 0, sizeof(*mem));
		return false;
	}

	return true;
}

static qboolean M_Update_MetadataSHA256(const jsonentry_t *entry,
	char *out, size_t outsize)
{
	const char *sha = JSON_FindString(entry, "sha256");

	if (!sha)
		sha = JSON_FindString(entry, "sha-256");
	if (!sha)
		sha = JSON_FindString(entry, "sha");
	if (!sha)
	{
		const char *digest = JSON_FindString(entry, "digest");

		if (digest)
			return M_Update_SHA256FromDigest(digest, out, outsize);
		return false;
	}
	if (!q_strncasecmp(sha, "sha256:", 7))
		return M_Update_SHA256FromDigest(sha, out, outsize);
	if (!M_Update_SHA256StringOkay(sha))
		return false;
	q_strlcpy(out, sha, outsize);
	return true;
}

static qboolean M_Update_ApplySignedMetadata(updatereleaseinfo_t *info,
	const char *metadata_text, char *error, size_t error_size)
{
	json_t *json = NULL;
	const jsonentry_t *assets;
	const jsonentry_t *asset;
	const double *schema;
	const char *tag;
	const char *name;
	const char *min_windows;
	const char *min_windows_name;
	char signed_sha256[65] = "";
	qboolean ok = false;

	json = JSON_Parse(metadata_text);
	if (!json || !json->root || json->root->type != JSON_OBJECT)
	{
		q_strlcpy(error, "invalid signed update metadata JSON",
			error_size);
		goto done;
	}

	schema = JSON_FindNumber(json->root, "schema");
	if (!schema || *schema != 1.0)
	{
		q_strlcpy(error, "unsupported signed update metadata schema",
			error_size);
		goto done;
	}

	tag = JSON_FindString(json->root, "tag");
	if (!tag || q_strcasecmp(tag, info->tag))
	{
		q_strlcpy(error, "signed update metadata tag mismatch",
			error_size);
		goto done;
	}

	assets = JSON_Find(json->root, "assets", JSON_OBJECT);
	asset = JSON_Find(assets, info->platform, JSON_OBJECT);
	if (!asset)
	{
		q_snprintf(error, error_size,
			"signed update metadata has no %s asset entry",
			info->platform);
		goto done;
	}

	name = JSON_FindString(asset, "name");
	if (!name || !*name)
	{
		q_strlcpy(error,
			"signed update metadata asset entry has no name", error_size);
		goto done;
	}
	if (q_strcasecmp(name, info->asset_name))
	{
		q_strlcpy(error, "signed update metadata asset name mismatch",
			error_size);
		goto done;
	}

	if (!M_Update_MetadataSHA256(asset, signed_sha256,
		sizeof(signed_sha256)))
	{
		q_strlcpy(error,
			"signed update metadata asset entry has no valid SHA-256",
			error_size);
		goto done;
	}
	if (info->asset_sha256[0] &&
		q_strcasecmp(info->asset_sha256, signed_sha256))
	{
		q_strlcpy(error, "signed update metadata asset digest mismatch",
			error_size);
		goto done;
	}
	q_strlcpy(info->asset_sha256, signed_sha256,
		sizeof(info->asset_sha256));
	info->metadata_asset_pinned = true;

	min_windows = JSON_FindString(asset, "min_windows_version");
	if (!min_windows)
		min_windows = JSON_FindString(json->root,
			"min_windows_version");
	min_windows_name = JSON_FindString(asset, "min_windows_name");
	if (!min_windows_name)
		min_windows_name = JSON_FindString(json->root,
			"min_windows_name");

	if (!strcmp(info->platform, "win64") || !strcmp(info->platform, "win32"))
	{
		if (!min_windows ||
			!M_Update_ParseWindowsVersion(min_windows,
			&info->metadata_min_windows_major,
			&info->metadata_min_windows_minor))
		{
			q_strlcpy(error,
				"signed update metadata has no valid min_windows_version",
				error_size);
			goto done;
		}
		info->metadata_min_windows_set = true;
		q_strlcpy(info->metadata_min_windows_name,
			(min_windows_name && *min_windows_name) ? min_windows_name :
			min_windows, sizeof(info->metadata_min_windows_name));
	}

	info->metadata_verified = true;
	ok = true;

done:
	if (json)
		JSON_Free(json);
	return ok;
}

static qboolean M_Update_FetchSignedMetadata(const jsonentry_t *root,
	updatereleaseinfo_t *info, char *error, size_t error_size)
{
	updateassetinfo_t metadata_asset;
	updateassetinfo_t signature_asset;
	versionhttpmem_t metadata = {0};
	versionhttpmem_t signature = {0};
	byte signature_bytes[UPDATE_METADATA_SIG_MAX_BYTES / 2];
	size_t signature_size = 0;
	qboolean ok = false;

	if (!M_Update_FindExactReleaseAsset(root, UPDATE_METADATA_ASSET,
		&metadata_asset))
	{
		if (info->asset_name[0] && UPDATE_METADATA_REQUIRED)
		{
			q_strlcpy(error,
				"signed update metadata is required but missing",
				error_size);
			return false;
		}
		return true;
	}

	info->metadata_present = true;

	if (!update_metadata_public_key_pem[0])
	{
		if (UPDATE_METADATA_REQUIRED)
		{
			q_strlcpy(error,
				"signed update metadata is required but no public key is configured",
				error_size);
			return false;
		}
		return true;
	}

	if (!M_Update_FindExactReleaseAsset(root, UPDATE_METADATA_SIG_ASSET,
		&signature_asset))
	{
		q_strlcpy(error,
			"signed update metadata is present but signature asset is missing",
			error_size);
		return false;
	}

	if (!M_Update_FetchReleaseAssetMemory(&metadata_asset, &metadata,
		UPDATE_METADATA_MAX_BYTES, error, error_size))
		goto done;
	if (!M_Update_FetchReleaseAssetMemory(&signature_asset, &signature,
		UPDATE_METADATA_SIG_MAX_BYTES, error, error_size))
		goto done;

	if (!M_Update_DecodeHexBlob(signature.memory, signature_bytes,
		sizeof(signature_bytes), &signature_size))
	{
		q_strlcpy(error, "signed update metadata signature is invalid hex",
			error_size);
		goto done;
	}

	if (!M_Update_VerifyMetadataSignature((const byte *)metadata.memory,
		metadata.size, signature_bytes, signature_size, error,
		error_size))
		goto done;

	ok = M_Update_ApplySignedMetadata(info, metadata.memory, error,
		error_size);

done:
	free(metadata.memory);
	free(signature.memory);
	return ok;
}

static qboolean M_Update_FindPlatformAsset(const jsonentry_t *root,
	updatereleaseinfo_t *info)
{
	const jsonentry_t *assets;
	const jsonentry_t *asset;
	const jsonentry_t *best_asset = NULL;
	int best_score = 0;
	const char *best_name;
	const char *best_url;
	const char *digest;
	const double *size;

	assets = JSON_Find(root, "assets", JSON_ARRAY);
	if (!assets)
		return false;

	for (asset = assets->firstchild; asset; asset = asset->next)
	{
		const char *name, *url;
		int score;

		if (asset->type != JSON_OBJECT)
			continue;
		name = JSON_FindString(asset, "name");
		url = JSON_FindString(asset, "browser_download_url");
		score = M_Update_AssetNamePlatformScore(name, info->platform);
		if (score <= best_score || !M_Update_AssetUrlAllowed(url))
			continue;

		best_asset = asset;
		best_score = score;
	}

	if (!best_asset)
		return false;

	best_name = JSON_FindString(best_asset, "name");
	best_url = JSON_FindString(best_asset, "browser_download_url");
	q_strlcpy(info->asset_name, best_name ? best_name : "",
		sizeof(info->asset_name));
	q_strlcpy(info->asset_url, best_url ? best_url : "",
		sizeof(info->asset_url));

	size = JSON_FindNumber(best_asset, "size");
	info->asset_size = size ? *size : 0.0;

	digest = JSON_FindString(best_asset, "digest");
	if (digest)
	{
		q_strlcpy(info->asset_digest, digest, sizeof(info->asset_digest));
		M_Update_SHA256FromDigest(digest, info->asset_sha256,
			sizeof(info->asset_sha256));
	}

	return true;
}

static qboolean M_Update_FetchReleaseInfo(updatereleaseinfo_t *info,
	char *error, size_t error_size)
{
	versionhttpmem_t mem = {0};
	json_t *json = NULL;
	const char *tag, *name, *body, *html_url;
	const qboolean *prerelease;
	qboolean ok = false;

	memset(info, 0, sizeof(*info));
	info->platform_supported = M_Update_Platform(info->platform,
		sizeof(info->platform));
	info->comparison = 2;

	if (!M_Version_GitHubHttpGet(UPDATE_RELEASE_URL, &mem, error, error_size,
		VERSION_GITHUB_MAX_RESPONSE_BYTES))
		return false;

	json = JSON_Parse(mem.memory);
	if (!json || !json->root || json->root->type != JSON_OBJECT)
	{
		q_strlcpy(error, "invalid JSON", error_size);
		goto done;
	}

	tag = JSON_FindString(json->root, "tag_name");
	if (!tag || !*tag)
	{
		q_strlcpy(error, "missing tag_name", error_size);
		goto done;
	}
	q_strlcpy(info->tag, tag, sizeof(info->tag));

	name = JSON_FindString(json->root, "name");
	if (name && *name)
		q_strlcpy(info->name, name, sizeof(info->name));

	body = JSON_FindString(json->root, "body");
	if (body && *body)
		q_strlcpy(info->body, body, sizeof(info->body));
	info->body_platform_zip = M_Update_ReleaseBodyHasPlatformZip(info->body,
		info->platform);

	html_url = JSON_FindString(json->root, "html_url");
	q_strlcpy(info->html_url,
		(html_url && *html_url) ? html_url : UPDATE_RELEASE_PAGE,
		sizeof(info->html_url));

	prerelease = JSON_FindBoolean(json->root, "prerelease");
	info->prerelease = prerelease ? *prerelease : false;

	info->comparison = M_Version_CompareTagToCurrent(info->tag,
		info->prerelease);

	if (info->platform_supported)
	{
		M_Update_FindPlatformAsset(json->root, info);
		if (info->asset_name[0] &&
			M_Update_PlatformInstallSupported(info->platform) &&
			!M_Update_FetchSignedMetadata(json->root, info, error,
			error_size))
			goto done;
	}

	ok = true;

done:
	if (json)
		JSON_Free(json);
	if (mem.memory)
		free(mem.memory);
	return ok;
}

static void M_Update_PrintReleaseInfo(const updatereleaseinfo_t *info,
	qboolean force)
{
	char size[32];
	char min_windows[64];
	int min_windows_major, min_windows_minor;
	const char *min_windows_name;

	Con_Printf("\n^mQSS-M update check^m\n\n");
	Con_Printf("%-18s ^g%s\n", "Current", QSSM_VER_STRING);
	Con_Printf("%-18s ^g%s^m%s\n", "Latest release", info->tag,
		info->prerelease ? " (prerelease)" : "");
	Con_Printf("%-18s %s\n", "Channel",
		info->prerelease ? "prerelease" : "stable");

	if (info->comparison == 0)
	{
		if (force)
			Con_Printf("%-18s this release is already installed; force allows reinstall\n",
				"Status");
		else
			Con_Printf("%-18s you have this release\n", "Status");
	}
	else if (info->comparison == 2)
	{
		if (force)
			Con_Printf("%-18s unable to compare versions/channels; force allows install\n",
				"Status");
		else
			Con_Printf("%-18s unable to compare versions/channels\n",
				"Status");
	}
	else if (info->comparison > 0)
	{
		if (force)
			Con_Printf("%-18s local build is newer; force allows downgrade\n",
				"Status");
		else
			Con_Printf("%-18s local build is newer; refusing downgrade by default\n",
				"Status");
	}
	else if (info->comparison < 0)
		Con_Printf("%-18s update available\n", "Status");

	Con_Printf("%-18s %s\n", "Platform", info->platform);
	if (!info->platform_supported)
	{
		Con_Printf("%-18s no portable updater asset for this platform yet\n",
			"Asset");
	}
	else if (info->asset_name[0])
	{
		M_Update_FormatSize(info->asset_size, size, sizeof(size));
		Con_Printf("%-18s %s (%s)\n", "Asset", info->asset_name, size);
		if (info->asset_sha256[0])
			Con_Printf("%-18s %s\n", "SHA-256", info->asset_sha256);
		else if (info->asset_digest[0])
			Con_Printf("%-18s unsupported digest: %s\n", "Digest",
				info->asset_digest);
		else
			Con_Printf("%-18s unavailable in release metadata\n", "SHA-256");
	}
	else
	{
		if (info->body_platform_zip)
			Con_Printf("%-18s matching ZIP is only linked in release notes\n",
				"Asset");
		else
			Con_Printf("%-18s no matching ZIP asset found\n", "Asset");
	}

	if (info->metadata_verified)
	{
		Con_Printf("%-18s signed", "Metadata");
		if (info->metadata_asset_pinned)
			Con_Printf(", asset pinned");
		Con_Printf("\n");
		if (info->metadata_min_windows_set)
		{
			M_Update_EffectiveWindowsMinimum(info, &min_windows_major,
				&min_windows_minor, &min_windows_name);
			if (min_windows_name && *min_windows_name)
				q_strlcpy(min_windows, min_windows_name, sizeof(min_windows));
			else
				q_snprintf(min_windows, sizeof(min_windows), "%d.%d",
					min_windows_major, min_windows_minor);
			Con_Printf("%-18s %s\n", "Min Windows", min_windows);
		}
	}
	else if (info->metadata_present)
		Con_Printf("%-18s present but not verified\n", "Metadata");
	else
		Con_Printf("%-18s not provided; using built-in compatibility floor\n",
			"Metadata");

	Con_Printf("%-18s %s\n\n", "Release page",
		info->html_url[0] ? info->html_url : UPDATE_RELEASE_PAGE);
}

static size_t M_Update_WriteData(void *ptr, size_t size, size_t nmemb,
	void *stream)
{
	updatedownload_t *download = (updatedownload_t *)stream;
	size_t bytes;

	if (size && nmemb > (size_t)-1 / size)
		return 0;

	bytes = size * nmemb;
	if (download->max_bytes > 0 &&
		(download->written > download->max_bytes ||
		(curl_off_t)bytes > download->max_bytes - download->written))
	{
		download->too_large = true;
		return 0;
	}

	if (fwrite(ptr, 1, bytes, download->file) != bytes)
		return 0;

	download->written += (curl_off_t)bytes;
	return bytes;
}

static int M_Update_ProgressCallback(void *clientp, curl_off_t dltotal,
	curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	updatedownload_t *download = (updatedownload_t *)clientp;
	double now;

	(void)ultotal;
	(void)ulnow;

	if (stop_curl_download)
		return 1;

	if (download->pump_events)
	{
		int lastkey = -1;
		int lastchar = -1;

		now = Sys_DoubleTime();
		if (now - download->last_event_time >= 0.05)
		{
			download->last_event_time = now;
			Sys_SendKeyEvents();
			Key_GetGrabbedInput(&lastkey, &lastchar);
			if (lastkey == K_ESCAPE || lastkey == K_BBUTTON ||
				lastkey == K_MOUSE2 || lastkey == K_MOUSE4 ||
				lastchar == '`')
			{
				stop_curl_download = true;
				return 1;
			}
		}
		if (now - download->last_screen_time >= 0.10)
		{
			download->last_screen_time = now;
			SCR_UpdateScreen();
		}
	}

	if (download->max_bytes > 0 &&
		((dltotal > 0 && dltotal > download->max_bytes) ||
		dlnow > download->max_bytes))
	{
		download->too_large = true;
		return 1;
	}

	now = Sys_DoubleTime();
	if (now - download->last_progress_time < 0.5 && dltotal != dlnow)
		return 0;

	download->last_progress_time = now;
	if (dltotal > 0)
	{
		int progress = (int)((double)dlnow * 100.0 / (double)dltotal);
		char size[32];

		if (progress < 0)
			progress = 0;
		else if (progress > 100)
			progress = 100;
		M_Update_FormatSize((double)dltotal, size, sizeof(size));
		Con_Printf("DL %s (%s) ^m%d%%\r", download->display_name,
			size, progress);
	}
	else if (dlnow > 0)
	{
		Con_Printf("DL %s %.0f KB\r", download->display_name,
			(double)dlnow / 1024.0);
	}
	if (download->pump_events)
		SCR_UpdateScreen();

	return 0;
}

static qboolean M_Update_RunTransfer(const char *url, const char *path,
	const char *display_name, curl_off_t max_bytes, qofs_t *file_size,
	char *error, size_t error_size)
{
	updatedownload_t download;
	char temp_path[MAX_OSPATH];
	CURL *curl = NULL;
	CURLcode result = CURLE_OK;
	FILE *fp = NULL;
	long response_code = 0;
	qofs_t local_file_size = 0;
	qboolean success = false;
	qboolean write_failed = false;
	qboolean input_grabbed = false;
	char flush_error[256] = "";

	if (file_size)
		*file_size = 0;
	if (error && error_size)
		error[0] = '\0';

	if (!M_Update_AssetUrlAllowed(url))
	{
		q_strlcpy(error, "invalid release asset URL", error_size);
		return false;
	}

	fp = M_Update_OpenExclusiveTemp(path, temp_path, sizeof(temp_path), error,
		error_size);
	if (!fp)
		return false;

	curl = curl_easy_init();
	if (!curl)
	{
		q_strlcpy(error, "unable to initialize curl", error_size);
		fclose(fp);
		remove(temp_path);
		return false;
	}

	memset(&download, 0, sizeof(download));
	download.file = fp;
	download.max_bytes = max_bytes;
	download.pump_events = host_initialized && scr_initialized &&
		con_initialized && cls.state != ca_dedicated;
	q_strlcpy(download.display_name,
		display_name && *display_name ? display_name : "QSS-M update",
		sizeof(download.display_name));

	curl_download_active = true;
	stop_curl_download = false;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 1024L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, M_Update_WriteData);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &download);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, M_Update_ProgressCallback);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &download);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 0L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 500L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);
	if (max_bytes > 0)
		curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, max_bytes);
	M_Update_CurlOptions(curl);

	if (download.pump_events)
	{
		Con_Printf("Press ^mEsc^m to cancel.\n");
		Key_BeginInputGrab();
		input_grabbed = true;
	}

	result = curl_easy_perform(curl);

	if (input_grabbed)
	{
		Key_EndInputGrab();
		input_grabbed = false;
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	write_failed = !M_Update_FlushFileToDisk(fp, flush_error,
		sizeof(flush_error));
	if (fseek(fp, 0, SEEK_END) == 0)
		local_file_size = ftell(fp);
	if (local_file_size < 0)
		local_file_size = 0;

	fclose(fp);
	fp = NULL;
	curl_easy_cleanup(curl);
	curl = NULL;
	Con_Printf("\n");

	if (write_failed && result == CURLE_OK)
		result = CURLE_WRITE_ERROR;

	if (download.too_large || result == CURLE_FILESIZE_EXCEEDED)
	{
		char size[32];

		M_Update_FormatSize((double)max_bytes, size, sizeof(size));
		q_snprintf(error, error_size, "download exceeded %s limit", size);
		remove(temp_path);
	}
	else if (stop_curl_download || result == CURLE_ABORTED_BY_CALLBACK)
	{
		q_strlcpy(error, "download cancelled", error_size);
		remove(temp_path);
	}
	else if (result == CURLE_OK && response_code >= 200 &&
		response_code < 300)
	{
		if (!M_Update_ReplaceFile(temp_path, path, false, error,
			error_size))
			remove(temp_path);
		else
			success = true;
	}
	else if (result != CURLE_OK)
	{
		q_strlcpy(error, flush_error[0] ? flush_error :
			curl_easy_strerror(result), error_size);
		remove(temp_path);
	}
	else
	{
		q_snprintf(error, error_size, "HTTP %ld", response_code);
		remove(temp_path);
	}

	curl_download_active = false;
	stop_curl_download = false;

	if (file_size)
		*file_size = local_file_size;
	return success;
}

static void M_Update_CopyExtractError(char *error, size_t error_size)
{
	const char *zip_error = ZIP_ExtractError();

	if (zip_error && zip_error[0])
		q_strlcpy(error, zip_error, error_size);
	else
		q_strlcpy(error, "extract failed", error_size);
}

static qboolean M_Update_PreflightRelease(const updatereleaseinfo_t *info,
	qboolean force, char *error, size_t error_size)
{
	if (!info->platform_supported)
	{
		q_snprintf(error, error_size,
			"no portable updater support for platform %s", info->platform);
		return false;
	}
	if (!M_Update_PlatformInstallSupported(info->platform))
	{
		if (!strcmp(info->platform, "macos"))
			q_strlcpy(error,
				"macOS updater staging is deferred until signed .app bundle swap/quarantine handling is implemented",
				error_size);
		else
			q_snprintf(error, error_size,
				"portable updater install is not enabled for platform %s",
				info->platform);
		return false;
	}
	if (!M_Update_CheckPlatformCompatibility(info, error, error_size))
		return false;
	if (info->comparison == 2 && !force)
	{
		q_strlcpy(error,
			"unable to compare local and remote versions/channels; use update force to install or update prepare force to stage anyway",
			error_size);
		return false;
	}
	if (info->comparison > 0 && !force)
	{
		q_strlcpy(error,
			"local build is newer than latest release; use update force to install or update prepare force to stage anyway",
			error_size);
		return false;
	}
	if (info->comparison == 0 && !force)
	{
		q_strlcpy(error,
			"this build already matches the latest release; use update force to reinstall or update prepare force to stage anyway",
			error_size);
		return false;
	}
	if (!info->asset_name[0] || !info->asset_url[0])
	{
		if (info->body_platform_zip)
			q_strlcpy(error,
				"matching ZIP is only linked in release notes; upload it as a GitHub release asset so the updater can verify its digest",
				error_size);
		else
			q_strlcpy(error, "no matching release ZIP asset found",
				error_size);
		return false;
	}
	if (!info->asset_sha256[0])
	{
		if (info->asset_digest[0])
			q_snprintf(error, error_size,
				"release asset has unsupported digest: %s",
				info->asset_digest);
		else
			q_strlcpy(error,
				"release asset has no GitHub SHA-256 digest", error_size);
		return false;
	}
	if (info->asset_size > (double)UPDATE_MAX_DOWNLOAD_BYTES)
	{
		char size[32];

		M_Update_FormatSize((double)UPDATE_MAX_DOWNLOAD_BYTES, size,
			sizeof(size));
		q_snprintf(error, error_size, "release asset exceeds %s limit", size);
		return false;
	}

	return true;
}

static qboolean M_Update_PrepareRelease(const updatereleaseinfo_t *info,
	qboolean force, char *stage_dir, size_t stage_dir_size,
	char *zip_path, size_t zip_path_size, char *extract_dir,
	size_t extract_dir_size, char *error, size_t error_size)
{
	qofs_t file_size = 0;

	if (!M_Update_PreflightRelease(info, force, error, error_size))
		return false;

	if (!M_Update_BuildStagePaths(info, stage_dir, stage_dir_size, zip_path,
		zip_path_size, extract_dir, extract_dir_size, error, error_size))
		return false;
	if (!M_Update_CheckFreeDiskSpace(stage_dir, "staging area",
		info->asset_size, error, error_size))
		return false;

	Con_Printf("Downloading %s\n", info->asset_url);
	if (!M_Update_RunTransfer(info->asset_url, zip_path, info->asset_name,
		UPDATE_MAX_DOWNLOAD_BYTES, &file_size, error, error_size))
		return false;

	Con_Printf("Verifying SHA-256...\n");
	if (!M_VerifySHA256File(zip_path, info->asset_sha256, false, NULL))
	{
		remove(zip_path);
		q_strlcpy(error, "SHA-256 verification failed", error_size);
		return false;
	}

	Con_Printf("Extracting to %s\n", extract_dir);
	if (!ZIP_ExtractQuiet(zip_path, extract_dir))
	{
		M_Update_CopyExtractError(error, error_size);
		return false;
	}

	Con_Printf("Validating staged package...\n");
	if (!M_Update_ValidateExtracted(info, extract_dir, error, error_size))
		return false;

	return true;
}

static void M_Update_Prepare_f(qboolean force)
{
	updatereleaseinfo_t info;
	char error[256] = "";
	char stage_dir[MAX_OSPATH];
	char zip_path[MAX_OSPATH];
	char extract_dir[MAX_OSPATH];

	if (cls.download.active || curl_download_active)
	{
		Con_Printf("A download is already active\n");
		return;
	}

	if (!M_Update_FetchReleaseInfo(&info, error, sizeof(error)))
	{
		Con_Printf("update prepare failed: %s\n",
			error[0] ? error : "unknown error");
		Con_Printf("Release page: %s\n", UPDATE_RELEASE_PAGE);
		return;
	}

	M_Update_PrintReleaseInfo(&info, force);
	Con_Printf("Preparing update archive only; live files will not be changed.\n");

	if (!M_Update_PrepareRelease(&info, force, stage_dir, sizeof(stage_dir),
		zip_path, sizeof(zip_path), extract_dir, sizeof(extract_dir),
		error, sizeof(error)))
	{
		Con_Printf("update prepare failed: %s\n",
			error[0] ? error : "unknown error");
		return;
	}

	Con_Printf("\n^mUpdate package prepared and verified^m\n");
	Con_Printf("%-18s %s\n", "Staging dir", stage_dir);
	Con_Printf("%-18s %s\n", "ZIP", zip_path);
	Con_Printf("%-18s %s\n", "Extracted", extract_dir);
	Con_Printf("No live QSS-M files were changed.\n");
}

static qboolean M_Update_ConfirmInstall(const updatereleaseinfo_t *info,
	qboolean force)
{
	char prompt[1024];
	char size[32];

	if (isDedicated)
	{
		Con_Printf("update install requires an interactive confirmation dialog\n");
		return false;
	}
	if (!host_initialized || !scr_initialized || !con_initialized)
	{
		Con_Printf("update install is available after video and console initialization; run it from the console or menu after startup completes.\n");
		return false;
	}

	M_Update_FormatSize(info->asset_size, size, sizeof(size));
	q_snprintf(prompt, sizeof(prompt),
		"Install ^mQSS-M^m ^g%s^g%s?\n\n"
		"Current: ^g%s^g\n"
		"Asset: %s\n"
		"Size: %s\n"
		"%s"
		"Configs/saves are not touched\n\n"
		"Continue? (^my^m/^mn^m)",
		info->tag, info->prerelease ? " (prerelease)" : "",
		QSSM_VER_STRING,
		info->asset_name[0] ? info->asset_name : "(none)", size,
		force ? "\nForce allows downgrade/reinstall\n" : "");

	Con_Printf("QSS-M will download and verify the release ZIP, close, replace only known engine package files, back up replaced files, and restart.\n");
	Con_Printf("Configs, saves, demos, mods, and id1 data are not touched.\n");
	return SCR_ModalMessage(prompt, 0.0f);
}

static void M_Update_Install_f(qboolean force)
{
	updatereleaseinfo_t info;
	char error[256] = "";
	char stage_dir[MAX_OSPATH];
	char zip_path[MAX_OSPATH];
	char extract_dir[MAX_OSPATH];
	char helper_path[MAX_OSPATH];
	char manifest_path[MAX_OSPATH];
	char current_exe[MAX_OSPATH];
	char live_dir[MAX_OSPATH];
	char live_exe_name[64];

	if (cls.download.active || curl_download_active)
	{
		Con_Printf("A download is already active\n");
		return;
	}

	if (!M_Update_FetchReleaseInfo(&info, error, sizeof(error)))
	{
		Con_Printf("update failed: %s\n",
			error[0] ? error : "unknown error");
		Con_Printf("Release page: %s\n", UPDATE_RELEASE_PAGE);
		return;
	}

	M_Update_PrintReleaseInfo(&info, force);
	if (!M_Update_PreflightRelease(&info, force, error, sizeof(error)))
	{
		Con_Printf("update refused: %s\n", error);
		return;
	}
	if (!M_Update_GetLiveInstallPaths(current_exe, sizeof(current_exe),
		live_dir, sizeof(live_dir), live_exe_name, sizeof(live_exe_name),
		error, sizeof(error)))
	{
		Con_Printf("update refused: %s\n", error);
		return;
	}
	if (!M_Update_CheckFreeDiskSpace(live_dir, "install directory",
		info.asset_size, error, sizeof(error)))
	{
		Con_Printf("update refused: %s\n", error);
		return;
	}

	if (!M_Update_ConfirmInstall(&info, force))
	{
		Con_Printf("update cancelled\n");
		return;
	}

	Con_Printf("Preparing verified update package...\n");
	if (!M_Update_PrepareRelease(&info, force, stage_dir, sizeof(stage_dir),
		zip_path, sizeof(zip_path), extract_dir, sizeof(extract_dir),
		error, sizeof(error)))
	{
		Con_Printf("update failed: %s\n",
			error[0] ? error : "unknown error");
		return;
	}

	if (!M_Update_PrepareApplyHelper(&info, stage_dir, extract_dir,
		helper_path, sizeof(helper_path), manifest_path,
		sizeof(manifest_path), error, sizeof(error)))
	{
		Con_Printf("update failed: %s\n",
			error[0] ? error : "unknown error");
		return;
	}

	if (!Sys_LaunchUpdateHelper(helper_path, UPDATE_HELPER_ARG,
		manifest_path, error, sizeof(error)))
	{
		Con_Printf("update failed: %s\n",
			error[0] ? error : "unable to launch update helper");
		return;
	}

	Con_Printf("Update helper launched. QSS-M will close now and restart after files are replaced.\n");
	Sys_Quit();
}

static qboolean M_Update_CommandHasForce(int first_arg)
{
	int i;

	for (i = first_arg; i < Cmd_Argc(); i++)
	{
		const char *arg = Cmd_Argv(i);

		if (!q_strcasecmp(arg, "force") || !q_strcasecmp(arg, "-force") ||
			!q_strcasecmp(arg, "--force"))
			return true;
	}

	return false;
}

static void M_Update_AddCompletions(const char *partial,
	const char *const *args, size_t num_args)
{
	size_t i;

	for (i = 0; i < num_args; i++)
		Con_AddToTabList(args[i], partial, NULL, NULL);
}

void M_Update_Completion_f(const char *partial)
{
	static const char *const first_args[] =
	{
		"check",
		"force",
		"-force",
		"--force",
		"help",
		"install",
		"page",
		"prepare"
	};
	static const char *const force_args[] =
	{
		"force",
		"-force",
		"--force"
	};
	const char *subcmd;

	if (Cmd_Argc() == 2)
	{
		M_Update_AddCompletions(partial, first_args,
			sizeof(first_args) / sizeof(first_args[0]));
		return;
	}

	if (Cmd_Argc() != 3)
		return;

	subcmd = Cmd_Argv(1);
	if (!q_strcasecmp(subcmd, "install") ||
		!q_strcasecmp(subcmd, "prepare"))
	{
		M_Update_AddCompletions(partial, force_args,
			sizeof(force_args) / sizeof(force_args[0]));
	}
}

static void M_Update_Usage(void)
{
	Con_Printf("\n");
	Con_Printf("^mupdate^m : confirm, download, verify, stage, and apply the latest portable release\n");
	Con_Printf("^mupdate^m check : check latest GitHub release and matching asset\n");
	Con_Printf("^mupdate^m install [force] : same as update; force bypasses downgrade/unknown-version refusal\n");
	Con_Printf("^mupdate^m prepare [force] : download, SHA-256 verify, extract, and validate the portable ZIP in staging\n");
	Con_Printf("^mupdate^m page  : open the latest QSS-M release page\n");
	Con_Printf("\n");
	Con_Printf("The installer only replaces known engine package files and keeps recovery backups until the restarted build confirms startup.\n");
	Con_Printf("Windows portable updates enforce signed release metadata min_windows_version when present; otherwise Windows 7 or later is required.\n");
	Con_Printf("GitHub unauthenticated API checks are limited to 60 requests/hour per IP; repeated checks may return HTTP 403.\n");
	Con_Printf("\n");
}

void M_Update_f(void)
{
	const char *arg = Cmd_Argc() >= 2 ? Cmd_Argv(1) : "install";

	if (!q_strcasecmp(arg, "help"))
	{
		M_Update_Usage();
		return;
	}

	if (!q_strcasecmp(arg, "page"))
	{
		if (SDL_OpenURL(UPDATE_RELEASE_PAGE) != 0)
			Con_Printf("Unable to open %s\n", UPDATE_RELEASE_PAGE);
		return;
	}

	if (!q_strcasecmp(arg, "check"))
	{
		updatereleaseinfo_t info;
		char error[128] = "";

		if (!M_Update_FetchReleaseInfo(&info, error, sizeof(error)))
		{
			Con_Printf("update check failed: %s\n",
				error[0] ? error : "unknown error");
			Con_Printf("Release page: %s\n", UPDATE_RELEASE_PAGE);
			return;
		}

		M_Update_PrintReleaseInfo(&info, false);
		Con_Printf("Use ^mupdate^m to confirm, stage, and apply the latest portable release.\n");
		return;
	}

	if (!q_strcasecmp(arg, "install"))
	{
		M_Update_Install_f(M_Update_CommandHasForce(2));
		return;
	}

	if (!q_strcasecmp(arg, "force") || !q_strcasecmp(arg, "-force") ||
		!q_strcasecmp(arg, "--force"))
	{
		M_Update_Install_f(true);
		return;
	}

	if (!q_strcasecmp(arg, "prepare"))
	{
		M_Update_Prepare_f(M_Update_CommandHasForce(2));
		return;
	}

	M_Update_Usage();
}

#endif /* NO_UPDATER */
