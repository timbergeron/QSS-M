/*
Copyright (C) 2010-2026 QuakeSpasm contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License.
*/

#include "quakedef.h"
#include "json.h"
#include "net_hostname_cache.h"
#include <errno.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define HOSTNAME_CACHE_SCHEMA_VERSION 1
#define HOSTNAME_CACHE_MAX_ENTRIES 512
#define HOSTNAME_CACHE_MAX_PER_HOST 8
/* Must accommodate every valid file the 512-entry writer can produce. */
#define HOSTNAME_CACHE_MAX_FILE_SIZE (512 * 1024)
#define HOSTNAME_CACHE_MAX_SERIALIZED_ENTRY (NET_NAMELEN * 4 + 256)
#define HOSTNAME_CACHE_FRESH_SECONDS (30 * 24 * 60 * 60)

COMPILE_TIME_ASSERT(hostname_cache_file_capacity,
	HOSTNAME_CACHE_MAX_FILE_SIZE >=
	HOSTNAME_CACHE_MAX_ENTRIES * HOSTNAME_CACHE_MAX_SERIALIZED_ENTRY);

typedef struct hostname_cache_entry_s
{
	char hostname[MAX_SERVER_ADDRESS_LEN];
	char address[MAX_SERVER_ADDRESS_LEN];
	int port;
	time_t last_connected;
} hostname_cache_entry_t;

static hostname_cache_entry_t hostname_cache_entries[HOSTNAME_CACHE_MAX_ENTRIES];
static size_t hostname_cache_count;
static qboolean hostname_cache_initialized;
static qboolean hostname_cache_dirty;
static SDL_threadID hostname_cache_owner_thread;
static unsigned int hostname_cache_generation;
static double hostname_cache_next_freshness_change;
static time_t hostname_cache_last_clock_check;

cvar_t cl_display_server_hostnames = {
	"cl_display_server_hostnames", "1", CVAR_ARCHIVE
};

static void NET_HostnameCache_AssertOwner(void)
{
#ifndef NDEBUG
	if (hostname_cache_initialized)
		SDL_assert(SDL_ThreadID() == hostname_cache_owner_thread);
#endif
}

static void NET_HostnameCache_DisplayChanged(cvar_t *var)
{
	(void)var;
	NET_HostnameCache_AssertOwner();
	hostname_cache_generation++;
}

static qboolean NET_HostnameCache_Path(char *path, size_t path_size)
{
	int result;

	if (!path || path_size == 0)
		return false;
	result = q_snprintf(path, path_size, "%s/id1/backups/%s", com_basedir, SERVERHOSTNAMES);
	if (result < 0 || (size_t)result >= path_size)
	{
		path[0] = '\0';
		return false;
	}
	return true;
}

static qboolean NET_HostnameCache_IsFresh(const hostname_cache_entry_t *entry, time_t now)
{
	if (!entry || entry->last_connected <= 0 || now == (time_t)-1)
		return false;
	if (entry->last_connected > now)
		return false;
	return now - entry->last_connected <= HOSTNAME_CACHE_FRESH_SECONDS;
}

static void NET_HostnameCache_UpdateFreshnessDeadline(time_t now)
{
	size_t i;

	hostname_cache_next_freshness_change = 0.0;
	hostname_cache_last_clock_check = now;
	if (now == (time_t)-1)
		return;

	for (i = 0; i < hostname_cache_count; i++)
	{
		const hostname_cache_entry_t *entry = &hostname_cache_entries[i];
		double transition;

		if (entry->last_connected > now)
			transition = (double)entry->last_connected;
		else if (NET_HostnameCache_IsFresh(entry, now))
			transition = (double)entry->last_connected +
				(double)HOSTNAME_CACHE_FRESH_SECONDS + 1.0;
		else
			continue;
		if (hostname_cache_next_freshness_change == 0.0 ||
			transition < hostname_cache_next_freshness_change)
			hostname_cache_next_freshness_change = transition;
	}
}

static void NET_HostnameCache_CheckFreshnessGeneration(time_t now)
{
	if (now == (time_t)-1)
		return;
	if ((hostname_cache_last_clock_check != (time_t)-1 &&
		now < hostname_cache_last_clock_check) ||
		(hostname_cache_next_freshness_change > 0.0 &&
		(double)now >= hostname_cache_next_freshness_change))
	{
		hostname_cache_generation++;
		NET_HostnameCache_UpdateFreshnessDeadline(now);
	}
	else
		hostname_cache_last_clock_check = now;
}

static void NET_HostnameCache_RemoveIndex(size_t index)
{
	if (index >= hostname_cache_count)
		return;
	if (index + 1 < hostname_cache_count)
		memmove(&hostname_cache_entries[index], &hostname_cache_entries[index + 1],
			(hostname_cache_count - index - 1) * sizeof(hostname_cache_entries[0]));
	hostname_cache_count--;
}

static void NET_HostnameCache_MoveToFront(size_t index)
{
	hostname_cache_entry_t entry;

	if (index == 0 || index >= hostname_cache_count)
		return;
	entry = hostname_cache_entries[index];
	memmove(&hostname_cache_entries[1], &hostname_cache_entries[0],
		index * sizeof(hostname_cache_entries[0]));
	hostname_cache_entries[0] = entry;
}

static void NET_HostnameCache_TrimPerHost(const char *host_name, int port)
{
	size_t i;
	int seen = 0;

	for (i = 0; i < hostname_cache_count; )
	{
		hostname_cache_entry_t *entry = &hostname_cache_entries[i];
		if (!q_strcasecmp(entry->hostname, host_name) && entry->port == port)
		{
			seen++;
			if (seen > HOSTNAME_CACHE_MAX_PER_HOST)
			{
				NET_HostnameCache_RemoveIndex(i);
				continue;
			}
		}
		i++;
	}
}

static qboolean NET_HostnameCache_ReadTime(const jsonentry_t *object,
	const char *name, time_t *output)
{
	const char *text;
	char *end;
	long long value;
	time_t converted;

	if (!object || !name || !output)
		return false;
	text = JSON_FindString(object, name);
	if (!text || !*text)
		return false;

	errno = 0;
	value = strtoll(text, &end, 10);
	if (errno == ERANGE || end == text || *end || value <= 0)
		return false;
	converted = (time_t)value;
	if ((long long)converted != value)
		return false;
	*output = converted;
	return true;
}

static qboolean NET_HostnameCache_ValidateEntry(const char *hostname_text,
	const char *address, int port, net_endpoint_t *host_endpoint,
	net_endpoint_t *address_endpoint)
{
	if (!hostname_text || !*hostname_text || !address || !*address ||
		port <= 0 || port > 65535)
		return false;
	if (!NET_ParseEndpoint(hostname_text, port, host_endpoint) ||
		host_endpoint->kind != NET_HOST_DNS || host_endpoint->port != port ||
		strlen(host_endpoint->host) >= NET_NAMELEN)
		return false;
	if (!NET_ParseEndpoint(address, port, address_endpoint) ||
		(address_endpoint->kind != NET_HOST_IPV4 && address_endpoint->kind != NET_HOST_IPV6) ||
		address_endpoint->port != port || strlen(address_endpoint->host) >= NET_NAMELEN)
		return false;
	return true;
}

#ifndef NDEBUG
static void NET_HostnameCache_AssertValid(void)
{
	size_t i, j;

	NET_HostnameCache_AssertOwner();
	SDL_assert(hostname_cache_count <= HOSTNAME_CACHE_MAX_ENTRIES);
	for (i = 0; i < hostname_cache_count; i++)
	{
		const hostname_cache_entry_t *entry = &hostname_cache_entries[i];
		net_endpoint_t host_endpoint;
		net_endpoint_t address;
		int same_host_count = 0;

		SDL_assert(entry->last_connected > 0);
		SDL_assert(NET_HostnameCache_ValidateEntry(entry->hostname, entry->address,
			entry->port, &host_endpoint, &address));
		for (j = 0; j < hostname_cache_count; j++)
		{
			const hostname_cache_entry_t *other = &hostname_cache_entries[j];
			if (!q_strcasecmp(entry->hostname, other->hostname) &&
				entry->port == other->port)
				same_host_count++;
			if (j > i)
				SDL_assert(q_strcasecmp(entry->hostname, other->hostname) ||
					q_strcasecmp(entry->address, other->address) ||
					entry->port != other->port);
		}
		SDL_assert(same_host_count <= HOSTNAME_CACHE_MAX_PER_HOST);
	}
}
#endif

static void NET_HostnameCache_SortMostRecent(void)
{
	size_t i;

	/* Stable insertion sort preserves file order for connections made in the same second. */
	for (i = 1; i < hostname_cache_count; i++)
	{
		hostname_cache_entry_t entry = hostname_cache_entries[i];
		size_t insert = i;

		while (insert > 0 &&
			hostname_cache_entries[insert - 1].last_connected < entry.last_connected)
		{
			hostname_cache_entries[insert] = hostname_cache_entries[insert - 1];
			insert--;
		}
		hostname_cache_entries[insert] = entry;
	}
}

static void NET_HostnameCache_TrimAllPerHost(void)
{
	size_t i;

	for (i = 0; i < hostname_cache_count; i++)
		NET_HostnameCache_TrimPerHost(hostname_cache_entries[i].hostname,
			hostname_cache_entries[i].port);
}

static void NET_HostnameCache_Load(void)
{
	char path[MAX_OSPATH];
	FILE *file;
	long length;
	char *text;
	json_t *json;
	const jsonentry_t *entries;
	const jsonentry_t *item;
	const double *schema_version;

	if (!NET_HostnameCache_Path(path, sizeof(path)))
		return;
	file = fopen(path, "rb");
	if (!file)
		return;
	if (fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		return;
	}
	length = ftell(file);
	rewind(file);
	if (length <= 0 || length > HOSTNAME_CACHE_MAX_FILE_SIZE)
	{
		fclose(file);
		return;
	}

	text = (char *)malloc((size_t)length + 1);
	if (!text)
	{
		fclose(file);
		return;
	}
	if (fread(text, 1, (size_t)length, file) != (size_t)length)
	{
		free(text);
		fclose(file);
		return;
	}
	text[length] = '\0';
	fclose(file);

	json = JSON_Parse(text);
	free(text);
	if (!json || !json->root || json->root->type != JSON_OBJECT)
	{
		if (json)
			JSON_Free(json);
		return;
	}
	schema_version = JSON_FindNumber(json->root, "schema_version");
	entries = JSON_Find(json->root, "entries", JSON_ARRAY);
	if (!schema_version || *schema_version != HOSTNAME_CACHE_SCHEMA_VERSION || !entries)
	{
		JSON_Free(json);
		return;
	}

	for (item = entries->firstchild;
		item && hostname_cache_count < HOSTNAME_CACHE_MAX_ENTRIES;
		item = item->next)
	{
		const char *transport;
		const char *hostname_text;
		const char *address;
		const double *port_number;
		time_t last_connected;
		net_endpoint_t host_endpoint;
		net_endpoint_t address_endpoint;
		hostname_cache_entry_t *entry;
		hostname_cache_entry_t *duplicate_entry = NULL;
		int port;
		size_t i;

		if (item->type != JSON_OBJECT)
			continue;
		transport = JSON_FindString(item, "transport");
		hostname_text = JSON_FindString(item, "hostname");
		address = JSON_FindString(item, "address");
		port_number = JSON_FindNumber(item, "port");
		if (!transport || strcmp(transport, "udp") || !port_number ||
			!isfinite(*port_number) || *port_number < 1.0 || *port_number > 65535.0 ||
			*port_number != (double)(int)*port_number ||
			!NET_HostnameCache_ReadTime(item, "last_connected", &last_connected) ||
			!NET_HostnameCache_ValidateEntry(hostname_text, address, (int)*port_number,
				&host_endpoint, &address_endpoint))
			continue;
		port = (int)*port_number;

		for (i = 0; i < hostname_cache_count; i++)
		{
			entry = &hostname_cache_entries[i];
			if (!q_strcasecmp(entry->hostname, host_endpoint.host) &&
				!q_strcasecmp(entry->address, address_endpoint.host) &&
				entry->port == port)
			{
				duplicate_entry = entry;
				break;
			}
		}
		if (duplicate_entry)
		{
			if (last_connected > duplicate_entry->last_connected)
				duplicate_entry->last_connected = last_connected;
			continue;
		}

		entry = &hostname_cache_entries[hostname_cache_count++];
		q_strlcpy(entry->hostname, host_endpoint.host, sizeof(entry->hostname));
		q_strlcpy(entry->address, address_endpoint.host, sizeof(entry->address));
		entry->port = port;
		entry->last_connected = last_connected;
	}
	JSON_Free(json);
	NET_HostnameCache_SortMostRecent();
	NET_HostnameCache_TrimAllPerHost();
#ifndef NDEBUG
	NET_HostnameCache_AssertValid();
#endif
}

static qboolean NET_HostnameCache_Save(void)
{
	char path[MAX_OSPATH];
	char temp_path[MAX_OSPATH];
	FILE *file;
	qboolean okay = true;
	size_t i;
	int result;

	NET_HostnameCache_AssertOwner();
	if (!hostname_cache_initialized || !hostname_cache_dirty)
		return true;

	if (!NET_HostnameCache_Path(path, sizeof(path)))
		return false;
	result = q_snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
	if (result < 0 || (size_t)result >= sizeof(temp_path))
		return false;
	COM_CreatePath(temp_path);
	file = fopen(temp_path, "w");
	if (!file)
		return false;

	if (fprintf(file, "{\n  \"schema_version\": %d,\n  \"entries\": [\n",
		HOSTNAME_CACHE_SCHEMA_VERSION) < 0)
		okay = false;

	for (i = 0; okay && i < hostname_cache_count; i++)
	{
		hostname_cache_entry_t *entry = &hostname_cache_entries[i];
		char *escaped_hostname = JSON_EscapeString(entry->hostname);
		char *address = JSON_EscapeString(entry->address);

		if (!escaped_hostname || !address)
			okay = false;
		else if (fprintf(file,
			"%s    {\"transport\": \"udp\", \"hostname\": \"%s\", "
			"\"address\": \"%s\", \"port\": %d, \"last_connected\": \"%lld\"}",
			i ? ",\n" : "", escaped_hostname, address, entry->port,
			(long long)entry->last_connected) < 0)
			okay = false;
		free(escaped_hostname);
		free(address);
	}
	if (okay && fprintf(file, "%s  ]\n}\n", hostname_cache_count ? "\n" : "") < 0)
		okay = false;
	if (fclose(file) != 0)
		okay = false;

	if (!okay)
	{
		remove(temp_path);
		return false;
	}
#ifdef _WIN32
	if (!MoveFileExA(temp_path, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
#else
	if (rename(temp_path, path) != 0)
#endif
	{
		remove(temp_path);
		return false;
	}
	hostname_cache_dirty = false;
	return true;
}

static const hostname_cache_entry_t *NET_HostnameCache_FindAddress(
	const net_endpoint_t *endpoint, time_t now)
{
	const hostname_cache_entry_t *match = NULL;
	size_t i;

	if (!endpoint || (endpoint->kind != NET_HOST_IPV4 && endpoint->kind != NET_HOST_IPV6))
		return NULL;
	for (i = 0; i < hostname_cache_count; i++)
	{
		const hostname_cache_entry_t *entry = &hostname_cache_entries[i];
		if (entry->port == endpoint->port &&
			!q_strcasecmp(entry->address, endpoint->host) &&
			NET_HostnameCache_IsFresh(entry, now))
		{
			/* Multiple fresh names for one endpoint are ambiguous; raw is safer. */
			if (match && q_strcasecmp(match->hostname, entry->hostname))
				return NULL;
			match = entry;
		}
	}
	return match;
}

#ifndef NDEBUG
static void NET_HostnameCache_RunSelfTests(void)
{
	const time_t now = (time_t)1000000000;
	net_endpoint_t endpoint;
	unsigned int saved_generation = hostname_cache_generation;
	size_t i;

	SDL_assert(hostname_cache_count == 0);
	for (i = 0; i < 10; i++)
	{
		hostname_cache_entry_t *entry = &hostname_cache_entries[hostname_cache_count++];
		q_strlcpy(entry->hostname, "roundrobin.example", sizeof(entry->hostname));
		q_snprintf(entry->address, sizeof(entry->address), "192.0.2.%u", (unsigned int)i + 1);
		entry->port = 26000;
		entry->last_connected = (time_t)i + 1;
	}
	NET_HostnameCache_SortMostRecent();
	NET_HostnameCache_TrimAllPerHost();
	SDL_assert(hostname_cache_count == HOSTNAME_CACHE_MAX_PER_HOST);
	SDL_assert(!strcmp(hostname_cache_entries[0].address, "192.0.2.10"));
	NET_HostnameCache_AssertValid();

	hostname_cache_count = 2;
	q_strlcpy(hostname_cache_entries[0].hostname, "first.example",
		sizeof(hostname_cache_entries[0].hostname));
	q_strlcpy(hostname_cache_entries[0].address, "192.0.2.42",
		sizeof(hostname_cache_entries[0].address));
	hostname_cache_entries[0].port = 26000;
	hostname_cache_entries[0].last_connected = now;
	q_strlcpy(hostname_cache_entries[1].hostname, "second.example",
		sizeof(hostname_cache_entries[1].hostname));
	q_strlcpy(hostname_cache_entries[1].address, "192.0.2.42",
		sizeof(hostname_cache_entries[1].address));
	hostname_cache_entries[1].port = 26000;
	hostname_cache_entries[1].last_connected = now;
	SDL_assert(NET_ParseEndpoint("192.0.2.42:26000", 27500, &endpoint));
	SDL_assert(NET_HostnameCache_FindAddress(&endpoint, now) == NULL);
	hostname_cache_entries[1].last_connected =
		now - (time_t)HOSTNAME_CACHE_FRESH_SECONDS - 1;
	SDL_assert(NET_HostnameCache_FindAddress(&endpoint, now) ==
		&hostname_cache_entries[0]);

	hostname_cache_count = 1;
	hostname_cache_entries[0].last_connected = now;
	NET_HostnameCache_UpdateFreshnessDeadline(now);
	SDL_assert(hostname_cache_next_freshness_change ==
		(double)now + HOSTNAME_CACHE_FRESH_SECONDS + 1.0);
	NET_HostnameCache_CheckFreshnessGeneration(
		now + (time_t)HOSTNAME_CACHE_FRESH_SECONDS);
	SDL_assert(hostname_cache_generation == saved_generation);
	NET_HostnameCache_CheckFreshnessGeneration(
		now + (time_t)HOSTNAME_CACHE_FRESH_SECONDS + 1);
	SDL_assert(hostname_cache_generation == saved_generation + 1);

	hostname_cache_entries[0].last_connected = now + 100;
	NET_HostnameCache_UpdateFreshnessDeadline(now);
	SDL_assert(hostname_cache_next_freshness_change == (double)now + 100.0);
	NET_HostnameCache_CheckFreshnessGeneration(now + 99);
	SDL_assert(hostname_cache_generation == saved_generation + 1);
	NET_HostnameCache_CheckFreshnessGeneration(now + 100);
	SDL_assert(hostname_cache_generation == saved_generation + 2);

	hostname_cache_count = 0;
	hostname_cache_generation = saved_generation;
	hostname_cache_next_freshness_change = 0.0;
	hostname_cache_last_clock_check = (time_t)-1;
}
#endif

static qboolean NET_HostnameCache_HostMatchesAddress(const net_endpoint_t *host,
	const net_endpoint_t *address, time_t now)
{
	size_t i;

	if (!host || !address || host->kind != NET_HOST_DNS ||
		(address->kind != NET_HOST_IPV4 && address->kind != NET_HOST_IPV6) ||
		host->port != address->port)
		return false;
	for (i = 0; i < hostname_cache_count; i++)
	{
		const hostname_cache_entry_t *entry = &hostname_cache_entries[i];
		if (entry->port == host->port &&
			!q_strcasecmp(entry->hostname, host->host) &&
			!q_strcasecmp(entry->address, address->host) &&
			NET_HostnameCache_IsFresh(entry, now))
			return true;
	}
	return false;
}

static qboolean NET_HostnameCache_IsAmbiguous(const hostname_cache_entry_t *entry,
	time_t now)
{
	size_t i;

	if (!NET_HostnameCache_IsFresh(entry, now))
		return false;
	for (i = 0; i < hostname_cache_count; i++)
	{
		const hostname_cache_entry_t *other = &hostname_cache_entries[i];
		if (other != entry && other->port == entry->port &&
			!q_strcasecmp(other->address, entry->address) &&
			q_strcasecmp(other->hostname, entry->hostname) &&
			NET_HostnameCache_IsFresh(other, now))
			return true;
	}
	return false;
}

static void NET_HostnameCache_Command(void)
{
	const char *subcommand = Cmd_Argc() >= 2 ? Cmd_Argv(1) : "list";
	size_t i;
	time_t now;

	NET_HostnameCache_AssertOwner();
	if (Cmd_Argc() > 2)
	{
		Con_Printf("usage: serverhostnames [list|clear]\n");
		return;
	}
	if (!q_strcasecmp(subcommand, "clear"))
	{
		hostname_cache_count = 0;
		hostname_cache_dirty = true;
		hostname_cache_generation++;
		NET_HostnameCache_UpdateFreshnessDeadline(time(NULL));
#ifndef NDEBUG
		NET_HostnameCache_AssertValid();
#endif
		if (!NET_HostnameCache_Save())
			Con_Printf("Unable to save cleared server hostname cache\n");
		else
			Con_Printf("Server hostname cache cleared\n");
		return;
	}
	if (q_strcasecmp(subcommand, "list"))
	{
		Con_Printf("usage: serverhostnames [list|clear]\n");
		return;
	}

	now = time(NULL);
	Con_Printf("Server hostname cache (%u entr%s):\n",
		(unsigned int)hostname_cache_count, hostname_cache_count == 1 ? "y" : "ies");
	for (i = 0; i < hostname_cache_count; i++)
	{
		const hostname_cache_entry_t *entry = &hostname_cache_entries[i];
		const char *state;
		char when[32] = "unknown";
		struct tm *local = localtime(&entry->last_connected);
		if (!NET_HostnameCache_IsFresh(entry, now))
			state = "stale";
		else if (NET_HostnameCache_IsAmbiguous(entry, now))
			state = "ambig";
		else
			state = "fresh";
		if (local)
			strftime(when, sizeof(when), "%Y-%m-%d", local);
		Con_Printf("  %-5s %-32s %s%s%s:%d  %s\n",
			state,
			entry->hostname,
			strchr(entry->address, ':') ? "[" : "",
			entry->address,
			strchr(entry->address, ':') ? "]" : "",
			entry->port, when);
	}
}

static void NET_HostnameCache_Completion(const char *partial)
{
	Con_AddToTabList("list", partial, NULL, NULL);
	Con_AddToTabList("clear", partial, NULL, NULL);
}

void NET_HostnameCache_Init(void)
{
	cmd_function_t *command;

	if (hostname_cache_initialized)
		return;
	hostname_cache_owner_thread = SDL_ThreadID();
	hostname_cache_initialized = true;
	hostname_cache_count = 0;
	hostname_cache_dirty = false;
	hostname_cache_generation = 1;
	hostname_cache_next_freshness_change = 0.0;
	hostname_cache_last_clock_check = (time_t)-1;
#ifndef NDEBUG
	NET_HostnameCache_RunSelfTests();
#endif
	Cvar_RegisterVariable(&cl_display_server_hostnames);
	Cvar_SetCallback(&cl_display_server_hostnames, NET_HostnameCache_DisplayChanged);
	Cmd_AddCommand("serverhostnames", NET_HostnameCache_Command);
	command = Cmd_FindCommand("serverhostnames");
	if (command)
		command->completion = NET_HostnameCache_Completion;
	NET_HostnameCache_Load();
	NET_HostnameCache_UpdateFreshnessDeadline(time(NULL));
}

void NET_HostnameCache_Shutdown(void)
{
	if (!hostname_cache_initialized)
		return;
	NET_HostnameCache_AssertOwner();
	if (!NET_HostnameCache_Save())
		Con_Warning("Unable to save server hostname cache\n");
	hostname_cache_count = 0;
	hostname_cache_initialized = false;
}

void NET_HostnameCache_RecordSuccessfulConnection(const char *requested_address,
	const struct qsocket_s *socket)
{
	const char *resolved_address;
	net_endpoint_t requested;
	net_endpoint_t resolved;
	time_t now;
	size_t i;

	NET_HostnameCache_AssertOwner();
	if (!hostname_cache_initialized || !requested_address || !socket)
		return;
	resolved_address = NET_QSocketGetResolvedAddressString(socket);
	if (!resolved_address || !*resolved_address ||
		!NET_ParseEndpoint(resolved_address, net_hostport, &resolved) ||
		(resolved.kind != NET_HOST_IPV4 && resolved.kind != NET_HOST_IPV6) ||
		!NET_ParseEndpoint(requested_address, resolved.port, &requested) ||
		requested.kind != NET_HOST_DNS || requested.port != resolved.port ||
		strlen(requested.host) >= NET_NAMELEN || strlen(resolved.host) >= NET_NAMELEN)
		return;

	now = time(NULL);
	if (now == (time_t)-1)
		return;
	for (i = 0; i < hostname_cache_count; i++)
	{
		hostname_cache_entry_t *entry = &hostname_cache_entries[i];
		if (!q_strcasecmp(entry->hostname, requested.host) &&
			!q_strcasecmp(entry->address, resolved.host) && entry->port == resolved.port)
		{
			entry->last_connected = now;
			NET_HostnameCache_MoveToFront(i);
			hostname_cache_dirty = true;
			hostname_cache_generation++;
			NET_HostnameCache_UpdateFreshnessDeadline(now);
#ifndef NDEBUG
			NET_HostnameCache_AssertValid();
#endif
			if (!NET_HostnameCache_Save())
				Con_Warning("Unable to save server hostname cache\n");
			return;
		}
	}

	if (hostname_cache_count == HOSTNAME_CACHE_MAX_ENTRIES)
		hostname_cache_count--;
	if (hostname_cache_count)
		memmove(&hostname_cache_entries[1], &hostname_cache_entries[0],
			hostname_cache_count * sizeof(hostname_cache_entries[0]));
	hostname_cache_count++;
	q_strlcpy(hostname_cache_entries[0].hostname, requested.host,
		sizeof(hostname_cache_entries[0].hostname));
	q_strlcpy(hostname_cache_entries[0].address, resolved.host,
		sizeof(hostname_cache_entries[0].address));
	hostname_cache_entries[0].port = resolved.port;
	hostname_cache_entries[0].last_connected = now;
	NET_HostnameCache_TrimPerHost(requested.host, resolved.port);
	hostname_cache_dirty = true;
	hostname_cache_generation++;
	NET_HostnameCache_UpdateFreshnessDeadline(now);
#ifndef NDEBUG
	NET_HostnameCache_AssertValid();
#endif
	if (!NET_HostnameCache_Save())
		Con_Warning("Unable to save server hostname cache\n");
}

qboolean NET_HostnameCache_FormatDisplay(const char *raw_address, int default_port,
	char *output, size_t output_size)
{
	net_endpoint_t raw;
	net_endpoint_t display;
	const hostname_cache_entry_t *entry;
	char formatted[MAX_SERVER_ADDRESS_LEN + 16];

	if (!output || output_size == 0)
		return false;
	q_strlcpy(output, raw_address ? raw_address : "", output_size);
	NET_HostnameCache_AssertOwner();
	if (!hostname_cache_initialized || !cl_display_server_hostnames.value ||
		!raw_address || !*raw_address ||
		!NET_ParseEndpoint(raw_address, default_port, &raw) ||
		(raw.kind != NET_HOST_IPV4 && raw.kind != NET_HOST_IPV6))
		return false;

	entry = NET_HostnameCache_FindAddress(&raw, time(NULL));
	if (!entry)
		return false;
	display = raw;
	display.kind = NET_HOST_DNS;
	q_strlcpy(display.host, entry->hostname, sizeof(display.host));
	if (!NET_FormatEndpoint(&display, raw.port_explicit, formatted, sizeof(formatted)) ||
		strlen(formatted) + 1 > output_size)
		return false;
	q_strlcpy(output, formatted, output_size);
	return true;
}

qboolean NET_HostnameCache_FormatDetailedDisplay(const char *raw_address, int default_port,
	char *output, size_t output_size)
{
	char friendly[MAX_SERVER_ADDRESS_LEN];
	int result;

	if (!output || output_size == 0)
		return false;
	if (!NET_HostnameCache_FormatDisplay(raw_address, default_port,
		friendly, sizeof(friendly)))
	{
		q_strlcpy(output, raw_address ? raw_address : "", output_size);
		return false;
	}
	result = q_snprintf(output, output_size, "%s (%s)", friendly, raw_address);
	if (result < 0 || (size_t)result >= output_size)
	{
		q_strlcpy(output, raw_address ? raw_address : "", output_size);
		return false;
	}
	return true;
}

qboolean NET_HostnameCache_AddressesMatch(const char *first, const char *second,
	int default_port)
{
	net_endpoint_t a, b;
	time_t now;

	NET_HostnameCache_AssertOwner();
	if (!first || !second || !*first || !*second)
		return false;
	if (!NET_ParseEndpoint(first, default_port, &a) ||
		!NET_ParseEndpoint(second, default_port, &b))
		return !q_strcasecmp(first, second);
	if (NET_EndpointEqual(&a, &b))
		return true;
	if (!hostname_cache_initialized || !cl_display_server_hostnames.value)
		return false;

	now = time(NULL);
	if (a.kind == NET_HOST_DNS && (b.kind == NET_HOST_IPV4 || b.kind == NET_HOST_IPV6))
		return NET_HostnameCache_HostMatchesAddress(&a, &b, now);
	if (b.kind == NET_HOST_DNS && (a.kind == NET_HOST_IPV4 || a.kind == NET_HOST_IPV6))
		return NET_HostnameCache_HostMatchesAddress(&b, &a, now);
	return false;
}

unsigned int NET_HostnameCache_Generation(void)
{
	NET_HostnameCache_AssertOwner();
	if (hostname_cache_initialized)
		NET_HostnameCache_CheckFreshnessGeneration(time(NULL));
	return hostname_cache_generation;
}
