/*
Copyright (C) 2026 QSS-M contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#include "quakedef.h"
#include "mapshot.h"

#include <curl/curl.h>
#include <time.h>

/*
==============================================================================

	MAPSHOT LOOKUP

	One worker thread, one in-memory result cache, one persisted negative
	cache.  See mapshot.h for the contract.

==============================================================================
*/

#ifndef QSSM_MAPSHOT_QUAKEWORLD_URL
#define QSSM_MAPSHOT_QUAKEWORLD_URL "https://assets.quake.world/mapshots/sm/%s.webp"
#endif
#ifndef QSSM_MAPSHOT_QUAKEONE_URL
#define QSSM_MAPSHOT_QUAKEONE_URL "https://servers.quakeone.com/mapthumbs/%s.jpg"
#endif

#define MAPSHOT_URL_SIZE			301
#define MAPSHOT_KEY_SIZE			(MAX_QPATH * 2 + 2)
#define MAPSHOT_MAX_SOURCES			3
#define MAPSHOT_CACHE_SIZE			64
#define MAPSHOT_RETRY_DELAY			60.0
#define MAPSHOT_MAX_BYTES			(2 * 1024 * 1024)
#define MAPSHOT_LOCAL_DIR			"levelshots"

#define MAPSHOT_NEG_CACHE_FILENAME			"mapshot_download_cache.json"
#define MAPSHOT_NEG_CACHE_SCHEMA_VERSION	2
#define MAPSHOT_NEG_CACHE_MAX_FILE_SIZE		(1024 * 1024)
#define MAPSHOT_NEG_CACHE_MISS_SECONDS		(30 * 24 * 60 * 60)
/* Bumping the source list must invalidate stored misses. */
#define MAPSHOT_NEG_CACHE_SOURCES			"quakeworld.webp,quakeone.jpg"

cvar_t cl_mapshots = {"cl_mapshots", "1", CVAR_ARCHIVE};
/* Below 1 on purpose.  Draw_Levelshot's gamma compensation is a linear scale,
   which can neutralise the midtone exactly but cannot undo a power curve's
   shadow lift, so a neutral 1.0 still reads bright against the menu. */
cvar_t cl_mapshots_brightness = {"cl_mapshots_brightness", "0.8", CVAR_ARCHIVE};

typedef struct
{
	const char	*format;
	qboolean	drawable;			// false for sources the engine cannot decode
	qboolean	gamedir_qualified;	// "<gamedir>/<map>" instead of "<map>"
} mapshot_source_t;

/* Order matters: Discord keeps the historical preference for the .webp source,
   which is smaller and better curated.  The pic path skips it because
   stb_image is built STBI_ONLY_JPEG (see image.c). */
static const mapshot_source_t mapshot_sources[] =
{
	{ QSSM_MAPSHOT_QUAKEWORLD_URL,	false,	false },
	{ QSSM_MAPSHOT_QUAKEONE_URL,	true,	false },
	{ QSSM_MAPSHOT_QUAKEONE_URL,	true,	true  },
};

typedef enum
{
	MAPSHOT_JOB_URL,		// HEAD probe, answer is a URL
	MAPSHOT_JOB_PIC			// GET, answer is a file on disk
} mapshot_jobkind_t;

typedef enum
{
	MAPSHOT_RESULT_NONE,
	MAPSHOT_RESULT_FOUND,
	MAPSHOT_RESULT_MISSING,
	MAPSHOT_RESULT_TRANSIENT,
	MAPSHOT_RESULT_ABORTED
} mapshot_result_t;

typedef struct
{
	qboolean	valid;
	char		key[MAPSHOT_KEY_SIZE];		// "<gamedir>|<map>"
	char		map[MAX_QPATH];
	qboolean	url_resolved;
	char		url[MAPSHOT_URL_SIZE];		// empty when no remote image exists
	qboolean	local_checked;				// disk probed once; don't stat every frame
	qboolean	pic_resolved;
	/* Deliberately not a qpic_t*: the draw layer's levelshot cache is a small
	   LRU, so a pointer held across an eviction would start naming a different
	   map's image.  Re-resolve through Draw_CacheLevelshot instead. */
	qboolean	has_pic;
	double		url_retry_after;			// transient backoff, never persisted
	double		pic_retry_after;
} mapshot_entry_t;

typedef struct
{
	byte	*data;
	size_t	length;
	size_t	capacity;
} mapshot_buffer_t;

static struct
{
	qboolean			initialized;

	SDL_Thread			*thread;
	SDL_atomic_t		abort_requested;
	SDL_atomic_t		done;

	// Owned by the main thread while no job runs, by the worker while one does.
	mapshot_jobkind_t	kind;
	char				key[MAPSHOT_KEY_SIZE];
	char				map[MAX_QPATH];
	char				gamedir[MAX_QPATH];
	char				local_path[MAX_OSPATH];	// final destination for PIC jobs
	char				result_url[MAPSHOT_URL_SIZE];
	char				result_path[MAX_OSPATH];
	mapshot_result_t	result;

	mapshot_entry_t		cache[MAPSHOT_CACHE_SIZE];
	size_t				next_cache;

	// Last "votemap" seen in serverinfo, so we only prefetch on change.
	char				votemap[MAX_QPATH];
	// Explicit destination captured before a local map load starts.
	char				loading_map[MAX_QPATH];
	// Keep a same-map load hidden after CL_ClearState forgets the old map name.
	qboolean			hide_loading_map;
} mapshots;

static com_negative_cache_t mapshot_negative_cache;

/*
==============================================================================

	NAME NORMALIZATION

==============================================================================
*/

/*
=================
Mapshot_NormalizeSegment

Map and gamedir names go straight into a URL and into a file path, so accept
only a conservative character set and fail closed on anything else.
=================
*/
static qboolean Mapshot_NormalizeSegment (char *destination,
	size_t destination_size, const char *source)
{
	size_t written = 0;

	if (!destination_size)
		return false;
	destination[0] = '\0';
	if (!source || !source[0])
		return false;

	while (*source && written + 1 < destination_size)
	{
		unsigned char c = (unsigned char)*source++;

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '_' || c == '+' ||
			c == '.'))
			return false;
		if (c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
		destination[written++] = (char)c;
	}
	if (*source)
		return false;
	destination[written] = '\0';

	/* "." and ".." would escape the levelshots directory. */
	if (!strcmp(destination, ".") || !strcmp(destination, ".."))
	{
		destination[0] = '\0';
		return false;
	}
	return true;
}

void Mapshot_CurrentGameDir (char *destination, size_t destination_size)
{
	char gamedir[MAX_OSPATH] = "";
	const char *leaf, *p;

	if (!destination_size)
		return;
	destination[0] = '\0';
	if (cls.state == ca_connected)
	{
		Info_GetKey(cl.serverinfo, "*gamedir", gamedir, sizeof(gamedir));
		if (!gamedir[0])
			q_strlcpy(gamedir, cl.server_gamedir, sizeof(gamedir));
	}
	if (!gamedir[0])
		q_strlcpy(gamedir, COM_SkipPath(com_gamedir), sizeof(gamedir));

	leaf = gamedir;
	for (p = gamedir; *p; ++p)
		if (*p == '/' || *p == '\\' || *p == ';')
			leaf = p + 1;
	if (!Mapshot_NormalizeSegment(destination, destination_size, leaf) ||
		!q_strcasecmp(destination, GAMENAME) || !q_strcasecmp(destination, "qw"))
		destination[0] = '\0';
}

static void Mapshot_BuildKey (char *destination, size_t destination_size,
	const char *map, const char *gamedir)
{
	q_snprintf(destination, destination_size, "%s|%s", gamedir, map);
}

/*
==============================================================================

	PERSISTED NEGATIVE CACHE

	Keyed by job kind as well as map, because a map with only a .webp is a
	hit for the URL path and a miss for the pic path.

==============================================================================
*/

static const char *Mapshot_KindName (mapshot_jobkind_t kind)
{
	return (kind == MAPSHOT_JOB_PIC) ? "pic" : "url";
}

static qboolean Mapshot_NegCacheValidate (const char *kind, const char *key, void *ctx)
{
	(void)ctx;
	if (!kind || !key || !key[0])
		return false;
	return !strcmp(kind, "pic") || !strcmp(kind, "url");
}

static const com_negative_cache_config_t mapshot_negative_cache_config =
{
	MAPSHOT_NEG_CACHE_FILENAME,
	MAPSHOT_NEG_CACHE_SCHEMA_VERSION,
	MAPSHOT_NEG_CACHE_MAX_FILE_SIZE,
	"kind",
	"map",
	8,
	MAPSHOT_KEY_SIZE,
	"sources",
	MAPSHOT_NEG_CACHE_SOURCES,
	Mapshot_NegCacheValidate,
	NULL
};

/* The negative cache rejects empty keys, and a bare id1 gamedir is empty. */
static void Mapshot_NegCacheKey (char *destination, size_t destination_size,
	const char *key)
{
	if (key[0] == '|')
		q_snprintf(destination, destination_size, "-%s", key);
	else
		q_strlcpy(destination, key, destination_size);
}

static qboolean Mapshot_NegCacheShouldSkip (mapshot_jobkind_t kind, const char *key)
{
	char storekey[MAPSHOT_KEY_SIZE + 1];
	time_t now = time(NULL);
	time_t next_retry;

	if (now == (time_t)-1)
		return false;

	Mapshot_NegCacheKey(storekey, sizeof(storekey), key);
	if (!COM_NegativeCache_ShouldSkip(&mapshot_negative_cache,
		&mapshot_negative_cache_config, Mapshot_KindName(kind), storekey,
		now, &next_retry))
		return false;

	Con_DPrintf("mapshot cache: skipping %s %s until %lld\n",
		Mapshot_KindName(kind), storekey, (long long)next_retry);
	return true;
}

static void Mapshot_NegCacheRecordMissing (mapshot_jobkind_t kind, const char *key)
{
	char storekey[MAPSHOT_KEY_SIZE + 1];
	time_t now = time(NULL);

	if (now == (time_t)-1)
		return;

	Mapshot_NegCacheKey(storekey, sizeof(storekey), key);
	COM_NegativeCache_Update(&mapshot_negative_cache,
		&mapshot_negative_cache_config, Mapshot_KindName(kind), storekey,
		COM_NEGATIVE_CACHE_MISSING, now, MAPSHOT_NEG_CACHE_MISS_SECONDS, 0);
	COM_NegativeCache_SaveIfDirty(&mapshot_negative_cache,
		&mapshot_negative_cache_config);
}

static void Mapshot_NegCacheRemove (mapshot_jobkind_t kind, const char *key)
{
	char storekey[MAPSHOT_KEY_SIZE + 1];

	Mapshot_NegCacheKey(storekey, sizeof(storekey), key);
	COM_NegativeCache_Remove(&mapshot_negative_cache,
		&mapshot_negative_cache_config, Mapshot_KindName(kind), storekey);
	COM_NegativeCache_SaveIfDirty(&mapshot_negative_cache,
		&mapshot_negative_cache_config);
}

/*
==============================================================================

	IN-MEMORY RESULT CACHE

==============================================================================
*/

static qpic_t *Mapshot_LoadPic (const char *map)
{
	return Draw_CacheLevelshot(map, va("%s/%s", MAPSHOT_LOCAL_DIR, map));
}

static mapshot_entry_t *Mapshot_CacheFind (const char *key)
{
	size_t i;

	for (i = 0; i < Q_COUNTOF(mapshots.cache); ++i)
		if (mapshots.cache[i].valid && !strcmp(mapshots.cache[i].key, key))
			return &mapshots.cache[i];
	return NULL;
}

static mapshot_entry_t *Mapshot_CacheGet (const char *key, const char *map)
{
	mapshot_entry_t *entry = Mapshot_CacheFind(key);

	if (entry)
		return entry;

	entry = &mapshots.cache[mapshots.next_cache];
	mapshots.next_cache = (mapshots.next_cache + 1) % Q_COUNTOF(mapshots.cache);

	/* The pic belongs to the draw layer's own bounded cache, so evicting an
	   entry here just drops our handle to it. */
	memset(entry, 0, sizeof(*entry));
	entry->valid = true;
	q_strlcpy(entry->key, key, sizeof(entry->key));
	q_strlcpy(entry->map, map, sizeof(entry->map));
	return entry;
}

static double *Mapshot_RetryAfter (mapshot_entry_t *entry, mapshot_jobkind_t kind)
{
	return (kind == MAPSHOT_JOB_PIC) ?
		&entry->pic_retry_after : &entry->url_retry_after;
}

/*
==============================================================================

	WORKER

==============================================================================
*/

static int Mapshot_BuildURLs (char urls[MAPSHOT_MAX_SOURCES][MAPSHOT_URL_SIZE],
	const char *map, const char *gamedir, qboolean drawable_only)
{
	char qualified[MAPSHOT_KEY_SIZE];
	int count = 0;
	size_t i;

	for (i = 0; i < Q_COUNTOF(mapshot_sources); ++i)
	{
		const mapshot_source_t *source = &mapshot_sources[i];
		const char *name = map;

		if (drawable_only && !source->drawable)
			continue;
		if (source->gamedir_qualified)
		{
			if (!gamedir[0])
				continue;
			q_snprintf(qualified, sizeof(qualified), "%s/%s", gamedir, map);
			name = qualified;
		}
		if ((size_t)q_snprintf(urls[count], MAPSHOT_URL_SIZE, source->format,
			name) >= MAPSHOT_URL_SIZE)
			continue;
		count++;
	}
	return count;
}

static int Mapshot_AbortCallback (void *unused, curl_off_t dltotal,
	curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	(void)unused;
	(void)dltotal;
	(void)dlnow;
	(void)ultotal;
	(void)ulnow;
	return SDL_AtomicGet(&mapshots.abort_requested) ? 1 : 0;
}

static size_t Mapshot_WriteCallback (void *contents, size_t size, size_t nmemb,
	void *userp)
{
	mapshot_buffer_t *buffer = (mapshot_buffer_t *)userp;
	size_t incoming;

	if (size && nmemb > (size_t)-1 / size)
		return 0;
	incoming = size * nmemb;
	if (incoming > MAPSHOT_MAX_BYTES - buffer->length)
		return 0;	// oversized; fail the transfer

	if (buffer->length + incoming > buffer->capacity)
	{
		size_t capacity = buffer->capacity ? buffer->capacity : 64 * 1024;
		byte *grown;

		while (capacity < buffer->length + incoming)
			capacity *= 2;
		grown = (byte *)realloc(buffer->data, capacity);
		if (!grown)
			return 0;
		buffer->data = grown;
		buffer->capacity = capacity;
	}
	memcpy(buffer->data + buffer->length, contents, incoming);
	buffer->length += incoming;
	return incoming;
}

static void Mapshot_SetCommonOptions (CURL *curl)
{
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2000L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 8000L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, ENGINE_NAME_AND_VER);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Mapshot_AbortCallback);
#if CURL_AT_LEAST_VERSION(7, 85, 0)
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
}

static CURLcode Mapshot_ProbeURL (const char *url, long *response_code)
{
	CURL *curl = curl_easy_init();
	CURLcode result;

	*response_code = 0;
	if (!curl)
		return CURLE_FAILED_INIT;
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	Mapshot_SetCommonOptions(curl);
	/* A HEAD has no body to wait on, so keep it shorter than a fetch. */
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 4000L);
	result = curl_easy_perform(curl);
	if (result == CURLE_OK)
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, response_code);
	curl_easy_cleanup(curl);
	return result;
}

static CURLcode Mapshot_FetchURL (const char *url, mapshot_buffer_t *buffer,
	long *response_code)
{
	CURL *curl = curl_easy_init();
	CURLcode result;

	*response_code = 0;
	if (!curl)
		return CURLE_FAILED_INIT;
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Mapshot_WriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)buffer);
	curl_easy_setopt(curl, CURLOPT_MAXFILESIZE, (long)MAPSHOT_MAX_BYTES);
	Mapshot_SetCommonOptions(curl);
	result = curl_easy_perform(curl);
	if (result == CURLE_OK)
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, response_code);
	curl_easy_cleanup(curl);
	return result;
}

/*
=================
Mapshot_ImageExtension

Trust the bytes, not the URL: a CDN error page served with a .jpg name would
otherwise be written to disk and then fail to decode forever.  Returns NULL
when the payload is not an image the engine can read.
=================
*/
static const char *Mapshot_ImageExtension (const byte *data, size_t length)
{
	if (length >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
		return "jpg";
	if (length >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
		data[3] == 'G' && data[4] == 0x0D && data[5] == 0x0A &&
		data[6] == 0x1A && data[7] == 0x0A)
		return "png";
	return NULL;
}

static qboolean Mapshot_WriteFile (const char *path, const byte *data, size_t length)
{
	FILE *f = fopen(path, "wb");
	size_t written;

	if (!f)
		return false;
	written = fwrite(data, 1, length, f);
	if (fclose(f) || written != length)
	{
		remove(path);
		return false;
	}
	return true;
}

static int Mapshot_WorkerThread (void *unused)
{
	char urls[Q_COUNTOF(mapshot_sources)][MAPSHOT_URL_SIZE];
	int url_count;
	int i;
	qboolean saw_transient = false;
	qboolean want_pic = (mapshots.kind == MAPSHOT_JOB_PIC);

	(void)unused;

	url_count = Mapshot_BuildURLs(urls, mapshots.map, mapshots.gamedir, want_pic);

	mapshots.result = MAPSHOT_RESULT_MISSING;
	mapshots.result_url[0] = '\0';
	mapshots.result_path[0] = '\0';

	for (i = 0; i < url_count; ++i)
	{
		CURLcode result;
		long response_code = 0;
		mapshot_buffer_t buffer = { NULL, 0, 0 };

		if (SDL_AtomicGet(&mapshots.abort_requested))
		{
			mapshots.result = MAPSHOT_RESULT_ABORTED;
			break;
		}

		if (want_pic)
			result = Mapshot_FetchURL(urls[i], &buffer, &response_code);
		else
			result = Mapshot_ProbeURL(urls[i], &response_code);

		if (result == CURLE_OK && response_code >= 200 && response_code < 300)
		{
			if (!want_pic)
			{
				mapshots.result = MAPSHOT_RESULT_FOUND;
				q_strlcpy(mapshots.result_url, urls[i], sizeof(mapshots.result_url));
				free(buffer.data);
				break;
			}
			else
			{
				const char *ext = Mapshot_ImageExtension(buffer.data, buffer.length);

				if (ext)
				{
					char path[MAX_OSPATH];

					q_snprintf(path, sizeof(path), "%s.%s", mapshots.local_path, ext);
					if (Mapshot_WriteFile(path, buffer.data, buffer.length))
					{
						mapshots.result = MAPSHOT_RESULT_FOUND;
						q_strlcpy(mapshots.result_url, urls[i],
							sizeof(mapshots.result_url));
						q_strlcpy(mapshots.result_path, path,
							sizeof(mapshots.result_path));
						free(buffer.data);
						break;
					}
					/* Local write failure is not the server's fault; retry later
					   rather than recording a permanent miss. */
					saw_transient = true;
				}
			}
		}

		free(buffer.data);

		if (result == CURLE_ABORTED_BY_CALLBACK ||
			SDL_AtomicGet(&mapshots.abort_requested))
		{
			mapshots.result = MAPSHOT_RESULT_ABORTED;
			break;
		}
		if (result != CURLE_OK || (response_code != 404 && response_code != 410))
			saw_transient = true;
	}

	if (mapshots.result == MAPSHOT_RESULT_MISSING && saw_transient)
		mapshots.result = MAPSHOT_RESULT_TRANSIENT;
	SDL_AtomicSet(&mapshots.done, 1);
	return 0;
}

/*
=================
Mapshot_StartJob

The caller has already established that no job is running.
=================
*/
static void Mapshot_StartJob (mapshot_jobkind_t kind, const char *key,
	const char *map, const char *gamedir)
{
	mapshots.kind = kind;
	q_strlcpy(mapshots.key, key, sizeof(mapshots.key));
	q_strlcpy(mapshots.map, map, sizeof(mapshots.map));
	q_strlcpy(mapshots.gamedir, gamedir, sizeof(mapshots.gamedir));
	mapshots.result = MAPSHOT_RESULT_NONE;
	mapshots.result_url[0] = '\0';
	mapshots.result_path[0] = '\0';
	mapshots.local_path[0] = '\0';

	if (kind == MAPSHOT_JOB_PIC)
	{
		/* Extension is chosen by the worker once it has seen the bytes. */
		q_snprintf(mapshots.local_path, sizeof(mapshots.local_path),
			"%s/%s/%s", com_gamedir, MAPSHOT_LOCAL_DIR, map);
		COM_CreatePath(mapshots.local_path);
	}

	SDL_AtomicSet(&mapshots.abort_requested, 0);
	SDL_AtomicSet(&mapshots.done, 0);
	mapshots.thread = SDL_CreateThread(Mapshot_WorkerThread, "Mapshot", NULL);
	if (!mapshots.thread)
	{
		mapshot_entry_t *entry = Mapshot_CacheGet(key, map);

		*Mapshot_RetryAfter(entry, kind) = realtime + MAPSHOT_RETRY_DELAY;
		Con_DWarning("Unable to create mapshot worker thread: %s\n", SDL_GetError());
	}
}

typedef enum
{
	MAPSHOT_START_STARTED,	// running now; ask again on a later frame
	MAPSHOT_START_WAIT,		// temporarily blocked; the caller must ask again
	MAPSHOT_START_SETTLED	// persisted miss; stop asking for this session
} mapshot_start_t;

/*
=================
Mapshot_TryStartJob

Starts the job unless the worker is busy or a previous failure asked us to back
off.

Cancellation rules, in order:
  - same map, either kind        -> never abort; the running job answers both
  - pic wants the worker         -> abort whatever is running (cursor moved, or
                                    an on-screen request outranks presence)
  - url wants a pic's worker     -> wait; presence can be a second late

Both asymmetries exist because Discord rebuilds its artwork once a second.
Aborting on job kind, or on any differing key, let that refresh restart an
in-flight levelshot download forever whenever the download outlived one
snapshot interval.  A url probe can be starved by a held preview instead, which
is fine: the preview is transient and presence retries on its own.

WAIT and SETTLED are distinct because callers cache the answer.  A retry window
is temporary and must stay askable; a persisted miss carries its own TTL.
=================
*/
static mapshot_start_t Mapshot_TryStartJob (mapshot_jobkind_t kind,
	const char *key, const char *map, const char *gamedir)
{
	mapshot_entry_t *entry;
	double *retry_after;

	if (mapshots.thread)
	{
		if (strcmp(mapshots.key, key))
		{
			/* A pic feeds something on screen right now; a URL feeds a presence
			   update that can wait a second longer.  Without this asymmetry
			   Discord's one-second refresh cancels a slow levelshot download
			   every time the player is browsing a different map than the one
			   being played, and neither job ever finishes. */
			if (kind != MAPSHOT_JOB_URL || mapshots.kind != MAPSHOT_JOB_PIC)
				SDL_AtomicSet(&mapshots.abort_requested, 1);
		}
		return MAPSHOT_START_WAIT;
	}

	/* Transient failures are per map and per consumer.  They stay in memory so
	   overlapping URL/PIC failures cannot overwrite one another, and are never
	   persisted because a transient cache hit is not a settled "no image"
	   answer. */
	entry = Mapshot_CacheGet(key, map);
	retry_after = Mapshot_RetryAfter(entry, kind);
	if (*retry_after)
	{
		if (realtime < *retry_after)
			return MAPSHOT_START_WAIT;
		*retry_after = 0;
	}
	if (Mapshot_NegCacheShouldSkip(kind, key))
		return MAPSHOT_START_SETTLED;

	Mapshot_StartJob(kind, key, map, gamedir);
	return mapshots.thread ? MAPSHOT_START_STARTED : MAPSHOT_START_WAIT;
}

/*
==============================================================================

	COMPLETION

==============================================================================
*/

static void Mapshot_FinishJob (void)
{
	mapshot_entry_t *entry;

	SDL_WaitThread(mapshots.thread, NULL);
	mapshots.thread = NULL;

	entry = Mapshot_CacheGet(mapshots.key, mapshots.map);
	*Mapshot_RetryAfter(entry, mapshots.kind) = 0;

	switch (mapshots.result)
	{
	case MAPSHOT_RESULT_FOUND:
		Mapshot_NegCacheRemove(mapshots.kind, mapshots.key);
		if (mapshots.kind == MAPSHOT_JOB_PIC)
		{
			entry->has_pic = (Mapshot_LoadPic(mapshots.map) != NULL);
			entry->pic_resolved = true;
			if (!entry->has_pic)
			{
				/* The payload passed the magic-byte check, so a load failure
				   here is local (texture manager state, disk) rather than a bad
				   asset.  Keep the file and back off briefly instead of burning
				   a 30-day miss on it. */
				Con_DPrintf("mapshot: %s downloaded but would not load\n",
					mapshots.map);
				entry->pic_resolved = false;
				entry->pic_retry_after = realtime + MAPSHOT_RETRY_DELAY;
			}
			else
				Con_DPrintf("mapshot: %s -> %s\n", mapshots.map, mapshots.result_path);
		}
		else
		{
			entry->url_resolved = true;
			q_strlcpy(entry->url, mapshots.result_url, sizeof(entry->url));
			Con_DPrintf("mapshot url: %s\n", mapshots.result_url);
		}
		break;

	case MAPSHOT_RESULT_MISSING:
		if (mapshots.kind == MAPSHOT_JOB_PIC)
		{
			entry->pic_resolved = true;
			entry->has_pic = false;
		}
		else
		{
			entry->url_resolved = true;
			entry->url[0] = '\0';
		}
		Mapshot_NegCacheRecordMissing(mapshots.kind, mapshots.key);
		Con_DPrintf("mapshot: no %s image for %s\n",
			Mapshot_KindName(mapshots.kind), mapshots.key);
		break;

	case MAPSHOT_RESULT_TRANSIENT:
		*Mapshot_RetryAfter(entry, mapshots.kind) = realtime + MAPSHOT_RETRY_DELAY;
		Con_DPrintf("mapshot: %s lookup failed for %s; retrying later\n",
			Mapshot_KindName(mapshots.kind), mapshots.key);
		break;

	default:	// aborted, or never ran
		break;
	}

	SDL_AtomicSet(&mapshots.abort_requested, 0);
	SDL_AtomicSet(&mapshots.done, 0);
}

/*
==============================================================================

	PUBLIC API

==============================================================================
*/

qboolean Mapshot_ResolveURL (const char *map, const char *gamedir,
	char *url, size_t url_size)
{
	char normalized[MAX_QPATH];
	char key[MAPSHOT_KEY_SIZE];
	mapshot_entry_t *entry;

	if (url_size)
		url[0] = '\0';
	if (!mapshots.initialized || !map || !gamedir)
		return false;
	if (!Mapshot_NormalizeSegment(normalized, sizeof(normalized), map))
		return false;

	Mapshot_BuildKey(key, sizeof(key), normalized, gamedir);

	entry = Mapshot_CacheFind(key);
	if (entry && entry->url_resolved)
	{
		q_strlcpy(url, entry->url, url_size);
		return entry->url[0] != '\0';
	}

	if (Mapshot_TryStartJob(MAPSHOT_JOB_URL, key, normalized, gamedir) ==
		MAPSHOT_START_SETTLED)
	{
		/* Remember the refusal so we stop walking the negative cache every
		   time presence rebuilds its snapshot.  Only for SETTLED -- a WAIT is
		   temporary and has to stay askable. */
		entry = Mapshot_CacheGet(key, normalized);
		entry->url_resolved = true;
		entry->url[0] = '\0';
	}
	return false;
}

qpic_t *Mapshot_ResolvePic (const char *map)
{
	char normalized[MAX_QPATH];
	char gamedir[MAX_QPATH];
	char key[MAPSHOT_KEY_SIZE];
	mapshot_entry_t *entry;
	int mode;

	if (!mapshots.initialized || !map)
		return NULL;
	mode = (int)cl_mapshots.value;
	if (mode <= MAPSHOT_MODE_OFF)
		return NULL;
	if (!Mapshot_NormalizeSegment(normalized, sizeof(normalized), map))
		return NULL;

	Mapshot_CurrentGameDir(gamedir, sizeof(gamedir));
	Mapshot_BuildKey(key, sizeof(key), normalized, gamedir);

	entry = Mapshot_CacheGet(key, normalized);
	if (entry->pic_resolved)
		return entry->has_pic ? Mapshot_LoadPic(normalized) : NULL;

	/* Local first: a shot already on disk (downloaded earlier, shipped in a
	   pak, or dropped in by hand) never touches the network.  Probe once --
	   this is a filesystem hit and callers ask every frame. */
	if (!entry->local_checked)
	{
		qpic_t *pic;

		entry->local_checked = true;
		pic = Mapshot_LoadPic(normalized);
		if (pic)
		{
			entry->pic_resolved = entry->has_pic = true;
			return pic;
		}
	}

	if (Mapshot_TryStartJob(MAPSHOT_JOB_PIC, key, normalized, gamedir) ==
		MAPSHOT_START_SETTLED)
		entry->pic_resolved = true;
	return NULL;
}

qboolean Mapshot_PicPending (const char *map)
{
	char normalized[MAX_QPATH];
	char gamedir[MAX_QPATH];
	char key[MAPSHOT_KEY_SIZE];
	const mapshot_entry_t *entry;

	if (!mapshots.initialized || !map)
		return false;
	if ((int)cl_mapshots.value < MAPSHOT_MODE_LEVELS)
		return false;
	if (!Mapshot_NormalizeSegment(normalized, sizeof(normalized), map))
		return false;

	Mapshot_CurrentGameDir(gamedir, sizeof(gamedir));
	Mapshot_BuildKey(key, sizeof(key), normalized, gamedir);
	entry = Mapshot_CacheFind(key);
	return !entry || !entry->pic_resolved;
}

void Mapshot_Prefetch (const char *map)
{
	Mapshot_ResolvePic(map);
}

static qboolean Mapshot_IsCurrentMap (const char *map)
{
	if (!map || !map[0] || cls.state != ca_connected ||
		cls.signon != SIGNONS || !cl.mapname[0])
		return false;

	if (FS_IsCaseSensitive())
		return !strcmp(map, cl.mapname);
	return !q_strcasecmp(map, cl.mapname);
}

void Mapshot_SetLoadingMap (const char *map)
{
	char normalized[MAX_QPATH];

	mapshots.loading_map[0] = '\0';
	mapshots.hide_loading_map = false;
	if (!Mapshot_NormalizeSegment(normalized, sizeof(normalized), map))
		return;
	if (Mapshot_IsCurrentMap(normalized))
	{
		mapshots.hide_loading_map = true;
		return;
	}
	q_strlcpy(mapshots.loading_map, normalized, sizeof(mapshots.loading_map));
	if ((int)cl_mapshots.value >= MAPSHOT_MODE_LOADING)
		Mapshot_Prefetch(normalized);
}

const char *Mapshot_LoadingMap (void)
{
	if (mapshots.hide_loading_map)
		return NULL;
	if (mapshots.loading_map[0])
		return mapshots.loading_map;
	if (mapshots.votemap[0])
		return mapshots.votemap;
	return NULL;
}

void Mapshot_EndLoading (void)
{
	mapshots.loading_map[0] = '\0';
	mapshots.hide_loading_map = false;
}

/*
=================
Mapshot_CheckVoteMap

Servers can advertise a pending map-change vote in serverinfo, which lets us
have the levelshot ready before the changelevel lands.  See crmod's
request.qc/votables.qc for the reference producer.
=================
*/
static void Mapshot_CheckVoteMap (void)
{
	char raw[MAX_QPATH];
	char votemap[MAX_QPATH] = "";

	if (cls.state != ca_connected)
		raw[0] = '\0';
	else
		Info_GetKey(cl.serverinfo, "votemap", raw, sizeof(raw));
	if (raw[0])
		Mapshot_NormalizeSegment(votemap, sizeof(votemap), raw);
	if (Mapshot_IsCurrentMap(votemap))
		votemap[0] = '\0';

	if (!strcmp(votemap, mapshots.votemap))
		return;
	q_strlcpy(mapshots.votemap, votemap, sizeof(mapshots.votemap));
	if (votemap[0])
	{
		if ((int)cl_mapshots.value >= MAPSHOT_MODE_LOADING)
			Mapshot_Prefetch(votemap);
	}
}

void Mapshot_CollectFinished (void)
{
	if (!mapshots.initialized)
		return;
	if (mapshots.thread && SDL_AtomicGet(&mapshots.done))
		Mapshot_FinishJob();
}

void Mapshot_Frame (void)
{
	if (!mapshots.initialized)
		return;
	Mapshot_CollectFinished();
	Mapshot_CheckVoteMap();
}

void Mapshot_NewGame (void)
{
	size_t i;

	if (!mapshots.initialized)
		return;
	for (i = 0; i < Q_COUNTOF(mapshots.cache); ++i)
		memset(&mapshots.cache[i], 0, sizeof(mapshots.cache[i]));
	mapshots.next_cache = 0;
	mapshots.votemap[0] = '\0';
}

/*
=================
Mapshot_f

Resolution is asynchronous, so a cold lookup reports "looking" and a later
call reports the result.
=================
*/
static void Mapshot_f (void)
{
	char normalized[MAX_QPATH];
	char gamedir[MAX_QPATH];
	char key[MAPSHOT_KEY_SIZE];
	const char *map;
	mapshot_entry_t *entry;
	qpic_t *pic;

	if (Cmd_Argc() < 2)
	{
		Con_Printf("mapshot <map> [force] : resolve a level screenshot\n");
		Con_Printf("  force discards a cached miss and looks again\n");
		return;
	}
	map = Cmd_Argv(1);
	if (!Mapshot_NormalizeSegment(normalized, sizeof(normalized), map))
	{
		Con_Printf("mapshot: \"%s\" is not a usable map name\n", map);
		return;
	}
	Mapshot_CurrentGameDir(gamedir, sizeof(gamedir));
	Mapshot_BuildKey(key, sizeof(key), normalized, gamedir);

	if (Cmd_Argc() > 2 && !q_strcasecmp(Cmd_Argv(2), "force"))
	{
		entry = Mapshot_CacheFind(key);
		if (entry)
			memset(entry, 0, sizeof(*entry));
		Mapshot_NegCacheRemove(MAPSHOT_JOB_PIC, key);
	}

	pic = Mapshot_ResolvePic(normalized);
	if (pic)
	{
		Con_Printf("mapshot %s: ready (%dx%d)\n", normalized, pic->width, pic->height);
		return;
	}
	entry = Mapshot_CacheFind(key);
	if ((int)cl_mapshots.value <= MAPSHOT_MODE_OFF)
		Con_Printf("mapshot %s: disabled (cl_mapshots 0)\n", normalized);
	else if (entry && entry->pic_resolved)
		Con_Printf("mapshot %s: no image available\n", normalized);
	else
		Con_Printf("mapshot %s: looking...\n", normalized);
}

void Mapshot_Init (void)
{
	if (mapshots.initialized)
		return;
	Cvar_RegisterVariable(&cl_mapshots);
	Cvar_RegisterVariable(&cl_mapshots_brightness);
	Cmd_AddCommand("mapshot", Mapshot_f);
	SDL_AtomicSet(&mapshots.abort_requested, 0);
	SDL_AtomicSet(&mapshots.done, 0);
	mapshots.initialized = true;
}

void Mapshot_Shutdown (void)
{
	if (!mapshots.initialized)
		return;
	if (mapshots.thread)
	{
		SDL_AtomicSet(&mapshots.abort_requested, 1);
		SDL_WaitThread(mapshots.thread, NULL);
		mapshots.thread = NULL;
	}
	COM_NegativeCache_SaveIfDirty(&mapshot_negative_cache,
		&mapshot_negative_cache_config);
	COM_NegativeCache_Free(&mapshot_negative_cache);
	memset(&mapshots, 0, sizeof(mapshots));
}
