/*
Copyright (C) 2026 QSS-M contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#include "quakedef.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#include <time.h>

/*
==============================================================================

	DISCORD IPC TRANSPORT

	Bounded, nonblocking local IPC to the Discord client: unix sockets on
	macOS/Linux, named pipes on Windows. No inbound JSON is parsed.

==============================================================================
*/

#define DISCORD_IPC_MAX_PAYLOAD (16 * 1024)
#define DISCORD_IPC_HEADER_SIZE 8
#define DISCORD_IPC_FRAME_SIZE (DISCORD_IPC_HEADER_SIZE + DISCORD_IPC_MAX_PAYLOAD)

enum
{
	DISCORD_IPC_OP_HANDSHAKE = 0,
	DISCORD_IPC_OP_FRAME = 1,
	DISCORD_IPC_OP_CLOSE = 2,
	DISCORD_IPC_OP_PING = 3,
	DISCORD_IPC_OP_PONG = 4
};

typedef enum
{
	DISCORD_IPC_DISCONNECTED,
	DISCORD_IPC_CONNECTING,
	DISCORD_IPC_HANDSHAKE,
	DISCORD_IPC_READY
} discord_ipc_state_t;

typedef struct
{
	discord_ipc_state_t state;
#ifdef _WIN32
	HANDLE pipe;
#else
	int fd;
#endif
	char application_id[32];
	byte read_buffer[DISCORD_IPC_FRAME_SIZE];
	size_t read_length;
	byte write_buffer[DISCORD_IPC_FRAME_SIZE];
	size_t write_length;
	size_t write_offset;
	byte pong_buffer[DISCORD_IPC_FRAME_SIZE];
	size_t pong_length;
	size_t pong_offset;
} discord_ipc_connection_t;

static discord_ipc_connection_t discord_ipc;

static uint32_t DiscordIPC_ReadLE32(const byte *data)
{
	return (uint32_t)data[0] |
		((uint32_t)data[1] << 8) |
		((uint32_t)data[2] << 16) |
		((uint32_t)data[3] << 24);
}

static void DiscordIPC_WriteLE32(byte *data, uint32_t value)
{
	data[0] = (byte)(value & 0xff);
	data[1] = (byte)((value >> 8) & 0xff);
	data[2] = (byte)((value >> 16) & 0xff);
	data[3] = (byte)((value >> 24) & 0xff);
}

static qboolean DiscordIPC_BuildFrame(byte *buffer, size_t buffer_size,
	uint32_t opcode, const void *payload, size_t payload_length, size_t *frame_length)
{
	if (payload_length > DISCORD_IPC_MAX_PAYLOAD ||
		payload_length + DISCORD_IPC_HEADER_SIZE > buffer_size)
		return false;

	DiscordIPC_WriteLE32(buffer, opcode);
	DiscordIPC_WriteLE32(buffer + 4, (uint32_t)payload_length);
	if (payload_length)
		memcpy(buffer + DISCORD_IPC_HEADER_SIZE, payload, payload_length);
	*frame_length = DISCORD_IPC_HEADER_SIZE + payload_length;
	return true;
}

static qboolean DiscordIPC_QueueFrame(uint32_t opcode, const void *payload, size_t payload_length)
{
	if (discord_ipc.write_length || discord_ipc.pong_length)
		return false;

	discord_ipc.write_offset = 0;
	return DiscordIPC_BuildFrame(discord_ipc.write_buffer,
		sizeof(discord_ipc.write_buffer), opcode, payload, payload_length,
		&discord_ipc.write_length);
}

static void DiscordIPC_Disconnect(void)
{
#ifdef _WIN32
	if (discord_ipc.pipe && discord_ipc.pipe != INVALID_HANDLE_VALUE)
		CloseHandle(discord_ipc.pipe);
	discord_ipc.pipe = INVALID_HANDLE_VALUE;
#else
	if (discord_ipc.fd >= 0)
		close(discord_ipc.fd);
	discord_ipc.fd = -1;
#endif
	discord_ipc.state = DISCORD_IPC_DISCONNECTED;
	discord_ipc.read_length = 0;
	discord_ipc.write_length = 0;
	discord_ipc.write_offset = 0;
	discord_ipc.pong_length = 0;
	discord_ipc.pong_offset = 0;
}

static void DiscordIPC_Init(void)
{
	memset(&discord_ipc, 0, sizeof(discord_ipc));
#ifdef _WIN32
	discord_ipc.pipe = INVALID_HANDLE_VALUE;
#else
	discord_ipc.fd = -1;
#endif
}

static void DiscordIPC_Shutdown(void)
{
	DiscordIPC_Disconnect();
	memset(discord_ipc.application_id, 0, sizeof(discord_ipc.application_id));
}

static qboolean DiscordIPC_ApplicationIdValid(const char *application_id)
{
	const unsigned char *p = (const unsigned char *)application_id;

	if (!p || !*p)
		return false;
	for (; *p; ++p)
		if (*p < '0' || *p > '9')
			return false;
	return (size_t)(p - (const unsigned char *)application_id) < sizeof(discord_ipc.application_id);
}

static qboolean DiscordIPC_OnConnected(void)
{
	char handshake[96];
	int length;

	discord_ipc.state = DISCORD_IPC_HANDSHAKE;
	length = q_snprintf(handshake, sizeof(handshake),
		"{\"v\":1,\"client_id\":\"%s\"}", discord_ipc.application_id);
	if (length < 0 || (size_t)length >= sizeof(handshake) ||
		!DiscordIPC_QueueFrame(DISCORD_IPC_OP_HANDSHAKE, handshake, (size_t)length))
	{
		DiscordIPC_Disconnect();
		return false;
	}
	return true;
}

#ifdef _WIN32

static qboolean DiscordIPC_OpenPlatform(void)
{
	char pipe_name[64];
	DWORD mode;
	int i;

	for (i = 0; i < 10; ++i)
	{
		q_snprintf(pipe_name, sizeof(pipe_name), "\\\\?\\pipe\\discord-ipc-%d", i);
		discord_ipc.pipe = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE,
			0, NULL, OPEN_EXISTING, 0, NULL);
		if (discord_ipc.pipe == INVALID_HANDLE_VALUE)
			continue;

		mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
		if (!SetNamedPipeHandleState(discord_ipc.pipe, &mode, NULL, NULL))
		{
			CloseHandle(discord_ipc.pipe);
			discord_ipc.pipe = INVALID_HANDLE_VALUE;
			continue;
		}
		return DiscordIPC_OnConnected();
	}
	return false;
}

#else

static qboolean DiscordIPC_TryUnixPath(const char *path)
{
	struct sockaddr_un address;
	int flags;

	if (access(path, F_OK) != 0)
		return false;

	discord_ipc.fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (discord_ipc.fd < 0)
		return false;

	flags = fcntl(discord_ipc.fd, F_GETFL, 0);
	if (flags < 0 || fcntl(discord_ipc.fd, F_SETFL, flags | O_NONBLOCK) < 0)
	{
		close(discord_ipc.fd);
		discord_ipc.fd = -1;
		return false;
	}
#ifdef SO_NOSIGPIPE
	{
		int enabled = 1;
		setsockopt(discord_ipc.fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
	}
#endif

	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	q_strlcpy(address.sun_path, path, sizeof(address.sun_path));
	if (connect(discord_ipc.fd, (struct sockaddr *)&address, sizeof(address)) == 0)
		return DiscordIPC_OnConnected();
	if (errno == EINPROGRESS || errno == EAGAIN)
	{
		discord_ipc.state = DISCORD_IPC_CONNECTING;
		return true;
	}

	close(discord_ipc.fd);
	discord_ipc.fd = -1;
	return false;
}

static qboolean DiscordIPC_TryUnixRoot(const char *root, const char *subdirectory)
{
	char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	size_t root_length;
	int i, length;

	if (!root || !root[0])
		return false;
	root_length = strlen(root);

	for (i = 0; i < 10; ++i)
	{
		if (subdirectory && subdirectory[0])
			length = q_snprintf(path, sizeof(path), "%s%s%s/discord-ipc-%d",
				root, root[root_length - 1] == '/' ? "" : "/", subdirectory, i);
		else
			length = q_snprintf(path, sizeof(path), "%s%sdiscord-ipc-%d",
				root, root[root_length - 1] == '/' ? "" : "/", i);
		if (length < 0 || (size_t)length >= sizeof(path))
			continue;
		if (DiscordIPC_TryUnixPath(path))
			return true;
	}
	return false;
}

static qboolean DiscordIPC_TryUnixRootVariants(const char *root)
{
	return DiscordIPC_TryUnixRoot(root, NULL) ||
		DiscordIPC_TryUnixRoot(root, "app/com.discordapp.Discord") ||
		DiscordIPC_TryUnixRoot(root, "snap.discord");
}

static qboolean DiscordIPC_OpenPlatform(void)
{
	const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
	const char *temporary;

	if (DiscordIPC_TryUnixRootVariants(xdg_runtime))
		return true;

	temporary = getenv("TMPDIR");
	if (DiscordIPC_TryUnixRootVariants(temporary))
		return true;
	temporary = getenv("TMP");
	if (DiscordIPC_TryUnixRootVariants(temporary))
		return true;
	temporary = getenv("TEMP");
	if (DiscordIPC_TryUnixRootVariants(temporary))
		return true;
	return DiscordIPC_TryUnixRootVariants("/tmp");
}

static qboolean DiscordIPC_FinishUnixConnect(void)
{
	struct pollfd descriptor;
	int error = 0;
	socklen_t error_length = sizeof(error);
	int result;

	descriptor.fd = discord_ipc.fd;
	descriptor.events = POLLOUT;
	descriptor.revents = 0;
	result = poll(&descriptor, 1, 0);
	if (result == 0)
		return false;
	if (result < 0)
	{
		if (errno == EINTR)
			return false;
		DiscordIPC_Disconnect();
		return false;
	}

	if (getsockopt(discord_ipc.fd, SOL_SOCKET, SO_ERROR, &error, &error_length) != 0)
	{
		DiscordIPC_Disconnect();
		return false;
	}
	if (error == EINPROGRESS || error == EALREADY)
		return false;
	if (error != 0)
	{
		DiscordIPC_Disconnect();
		return false;
	}
	return DiscordIPC_OnConnected();
}

#endif

static qboolean DiscordIPC_Connect(const char *application_id)
{
	DiscordIPC_Disconnect();
	if (!DiscordIPC_ApplicationIdValid(application_id))
		return false;

	q_strlcpy(discord_ipc.application_id, application_id, sizeof(discord_ipc.application_id));
	return DiscordIPC_OpenPlatform();
}

static qboolean DiscordIPC_WriteSome(byte *buffer, size_t length, size_t *offset)
{
#ifdef _WIN32
	DWORD written = 0;
	if (WriteFile(discord_ipc.pipe, buffer + *offset,
		(DWORD)(length - *offset), &written, NULL))
	{
		*offset += written;
		return true;
	}
	if (GetLastError() == ERROR_NO_DATA || GetLastError() == ERROR_PIPE_BUSY)
		return true;
#else
	ssize_t written;
	int flags = 0;
#ifdef MSG_NOSIGNAL
	flags |= MSG_NOSIGNAL;
#endif
	written = send(discord_ipc.fd, buffer + *offset, length - *offset, flags);
	if (written > 0)
	{
		*offset += (size_t)written;
		return true;
	}
	if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
		return true;
#endif
	DiscordIPC_Disconnect();
	return false;
}

static void DiscordIPC_FlushWrites(void)
{
	if (discord_ipc.write_length)
	{
		if (!DiscordIPC_WriteSome(discord_ipc.write_buffer,
			discord_ipc.write_length, &discord_ipc.write_offset))
			return;
		if (discord_ipc.write_offset == discord_ipc.write_length)
		{
			discord_ipc.write_length = 0;
			discord_ipc.write_offset = 0;
		}
	}

	if (!discord_ipc.write_length && discord_ipc.pong_length)
	{
		if (!DiscordIPC_WriteSome(discord_ipc.pong_buffer,
			discord_ipc.pong_length, &discord_ipc.pong_offset))
			return;
		if (discord_ipc.pong_offset == discord_ipc.pong_length)
		{
			discord_ipc.pong_length = 0;
			discord_ipc.pong_offset = 0;
		}
	}
}

static void DiscordIPC_ParseFrames(void)
{
	while (discord_ipc.read_length >= DISCORD_IPC_HEADER_SIZE)
	{
		uint32_t opcode = DiscordIPC_ReadLE32(discord_ipc.read_buffer);
		uint32_t payload_length = DiscordIPC_ReadLE32(discord_ipc.read_buffer + 4);
		size_t frame_length;

		if (payload_length > DISCORD_IPC_MAX_PAYLOAD)
		{
			DiscordIPC_Disconnect();
			return;
		}
		frame_length = DISCORD_IPC_HEADER_SIZE + (size_t)payload_length;
		if (discord_ipc.read_length < frame_length)
			return;

		if (opcode == DISCORD_IPC_OP_CLOSE)
		{
			DiscordIPC_Disconnect();
			return;
		}
		if (opcode == DISCORD_IPC_OP_PING)
		{
			if (discord_ipc.pong_length)
				return;
			discord_ipc.pong_offset = 0;
			if (!DiscordIPC_BuildFrame(discord_ipc.pong_buffer,
				sizeof(discord_ipc.pong_buffer), DISCORD_IPC_OP_PONG,
				discord_ipc.read_buffer + DISCORD_IPC_HEADER_SIZE,
				payload_length, &discord_ipc.pong_length))
			{
				DiscordIPC_Disconnect();
				return;
			}
		}
		else if (opcode == DISCORD_IPC_OP_FRAME &&
			discord_ipc.state == DISCORD_IPC_HANDSHAKE)
		{
			discord_ipc.state = DISCORD_IPC_READY;
		}

		discord_ipc.read_length -= frame_length;
		if (discord_ipc.read_length)
			memmove(discord_ipc.read_buffer,
				discord_ipc.read_buffer + frame_length, discord_ipc.read_length);
	}
}

static void DiscordIPC_ReadSome(void)
{
	size_t available_space;

	DiscordIPC_ParseFrames();
	if (discord_ipc.state == DISCORD_IPC_DISCONNECTED)
		return;
	available_space = sizeof(discord_ipc.read_buffer) - discord_ipc.read_length;
	if (!available_space)
	{
		DiscordIPC_Disconnect();
		return;
	}

#ifdef _WIN32
	{
		DWORD available = 0, received = 0;
		if (!PeekNamedPipe(discord_ipc.pipe, NULL, 0, NULL, &available, NULL))
		{
			DiscordIPC_Disconnect();
			return;
		}
		if (!available)
			return;
		if ((size_t)available > available_space)
			available = (DWORD)available_space;
		if (!ReadFile(discord_ipc.pipe, discord_ipc.read_buffer + discord_ipc.read_length,
			available, &received, NULL))
		{
			if (GetLastError() == ERROR_NO_DATA)
				return;
			DiscordIPC_Disconnect();
			return;
		}
		discord_ipc.read_length += received;
	}
#else
	{
		ssize_t received = recv(discord_ipc.fd,
			discord_ipc.read_buffer + discord_ipc.read_length, available_space, 0);
		if (received > 0)
			discord_ipc.read_length += (size_t)received;
		else if (received == 0)
		{
			DiscordIPC_Disconnect();
			return;
		}
		else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
		{
			DiscordIPC_Disconnect();
			return;
		}
	}
#endif
	DiscordIPC_ParseFrames();
}

static void DiscordIPC_Pump(void)
{
	if (discord_ipc.state == DISCORD_IPC_DISCONNECTED)
		return;
#ifndef _WIN32
	if (discord_ipc.state == DISCORD_IPC_CONNECTING)
	{
		DiscordIPC_FinishUnixConnect();
		if (discord_ipc.state == DISCORD_IPC_CONNECTING ||
			discord_ipc.state == DISCORD_IPC_DISCONNECTED)
			return;
	}
#endif

	DiscordIPC_FlushWrites();
	if (discord_ipc.state == DISCORD_IPC_DISCONNECTED)
		return;
	DiscordIPC_ReadSome();
	if (discord_ipc.state == DISCORD_IPC_DISCONNECTED)
		return;
	DiscordIPC_FlushWrites();
}

static qboolean DiscordIPC_IsOpen(void)
{
	return discord_ipc.state != DISCORD_IPC_DISCONNECTED;
}

static qboolean DiscordIPC_IsReady(void)
{
	return discord_ipc.state == DISCORD_IPC_READY;
}

static qboolean DiscordIPC_IsBusy(void)
{
	return discord_ipc.write_length != 0 || discord_ipc.pong_length != 0;
}

static qboolean DiscordIPC_SendJSON(const char *json, size_t length)
{
	if (discord_ipc.state != DISCORD_IPC_READY || !json)
		return false;
	return DiscordIPC_QueueFrame(DISCORD_IPC_OP_FRAME, json, length);
}

static unsigned int DiscordIPC_ProcessId(void)
{
#ifdef _WIN32
	return (unsigned int)GetCurrentProcessId();
#else
	return (unsigned int)getpid();
#endif
}

/*
==============================================================================

	DISCORD RICH PRESENCE

	Translates engine state into a presence snapshot and publishes it.

==============================================================================
*/

#ifndef QSSM_DISCORD_APP_ID
#define QSSM_DISCORD_APP_ID "1529568069301829783"
#endif
#ifndef QSSM_DISCORD_LARGE_IMAGE
#define QSSM_DISCORD_LARGE_IMAGE "qssm"
#endif

#define DISCORD_PRESENCE_IPC_INTERVAL 0.05
#define DISCORD_PRESENCE_SNAPSHOT_INTERVAL 1.0
#define DISCORD_PRESENCE_SEND_INTERVAL 5.0
#define DISCORD_PRESENCE_RETRY_INITIAL 0.5
#define DISCORD_PRESENCE_RETRY_MAX 60.0
#define DISCORD_PRESENCE_HANDSHAKE_TIMEOUT 5.0
#define DISCORD_PRESENCE_CLEAR_TIMEOUT 1.0

extern Uint64 maptime;
extern char lastmphost[NET_NAMELEN];

cvar_t cl_discord_presence = {"cl_discord_presence", "0", CVAR_ARCHIVE};

typedef enum
{
	DISCORD_PRESENCE_MENU,
	DISCORD_PRESENCE_CONNECTING,
	DISCORD_PRESENCE_DEMO,
	DISCORD_PRESENCE_SINGLE_PLAYER,
	DISCORD_PRESENCE_COOP,
	DISCORD_PRESENCE_MULTIPLAYER
} discord_presence_kind_t;

typedef struct
{
	char details[128];
	char state[128];
	char large_text[128];
	int64_t start_timestamp;
} discord_presence_snapshot_t;

typedef struct
{
	char *buffer;
	size_t capacity;
	size_t length;
	qboolean valid;
} discord_json_writer_t;

typedef struct
{
	qboolean initialized;
	qboolean enabled;
	qboolean disabling;
	qboolean clear_queued;
	qboolean application_id_valid;
	qboolean warned_application_id;
	qboolean was_ready;
	double next_ipc_pump;
	double next_retry;
	double retry_delay;
	double connection_deadline;
	double clear_deadline;
	double next_snapshot;
	double next_send;
	uint64_t nonce;
	Uint64 timestamp_maptime;
	discord_presence_kind_t timestamp_kind;
	int64_t timestamp_start;
	discord_presence_snapshot_t sent;
	discord_presence_snapshot_t pending;
	qboolean have_sent;
	qboolean have_pending;
} discord_presence_state_t;

static discord_presence_state_t discord_presence;

static qboolean DiscordPresence_ApplicationIdValid(void)
{
	const unsigned char *p = (const unsigned char *)QSSM_DISCORD_APP_ID;
	const unsigned char *start = p;
	size_t length;

	if (!*p)
		return false;
	for (; *p; ++p)
		if (*p < '0' || *p > '9')
			return false;
	length = (size_t)(p - start);
	return length >= 17 && length <= 20;
}

static void DiscordPresence_CopyASCII(char *destination, size_t destination_size,
	const char *source)
{
	size_t written = 0;

	if (!destination_size)
		return;
	if (!source)
		source = "";
	while (*source && written + 1 < destination_size)
	{
		unsigned char c = (unsigned char)*source++;
		if (c & 0x80)
			c &= 0x7f;
		if (c >= 32 && c < 127)
			destination[written++] = (char)c;
	}
	destination[written] = '\0';
}

static const char *DiscordPresence_ModeLabel(const char *mode)
{
	if (!q_strcasecmp(mode, "ctf"))
		return "Capture the Flag";
	if (!q_strcasecmp(mode, "dm") || !q_strcasecmp(mode, "ffa"))
		return "Deathmatch";
	if (!q_strcasecmp(mode, "arena"))
		return "Arena";
	if (!q_strcasecmp(mode, "wipeout"))
		return "Wipeout";
	if (!q_strcasecmp(mode, "dmm4") || !q_strcasecmp(mode, "ctfdmm4"))
		return "DMM4";
	if (!q_strcasecmp(mode, "ca") || !q_strcasecmp(mode, "clanarena"))
		return "Clan Arena";
	if (!q_strcasecmp(mode, "ra") || !q_strcasecmp(mode, "rocketarena") ||
		!q_strcasecmp(mode, "rocket_arena"))
		return "Rocket Arena";
	if (!q_strcasecmp(mode, "airshot"))
		return "Airshot";
	if (!q_strcasecmp(mode, "freezetag"))
		return "Freeze Tag";
	if (!q_strcasecmp(mode, "hh") || !q_strcasecmp(mode, "headhunter") ||
		!q_strcasecmp(mode, "headhunters"))
		return "Head Hunters";
	return NULL;
}

static const char *DiscordPresence_PlaymodeLabel(const char *playmode)
{
	if (!q_strcasecmp(playmode, "match"))
		return "Match";
	if (!q_strcasecmp(playmode, "pug"))
		return "PUG";
	if (!q_strcasecmp(playmode, "ffa"))
		return "FFA";
	if (!q_strcasecmp(playmode, "practice"))
		return "Practice";
	if (!q_strcasecmp(playmode, "normal") || !q_strcasecmp(playmode, "pub"))
		return "Public";
	return NULL;
}

static const char *DiscordPresence_ModLabel(const char *mod, const char *modname)
{
	if ((modname[0] && q_strcasestr(modname, "crmod")) ||
		(mod[0] && q_strcasestr(mod, "crmod")))
		return "CRMod 7";
	if (mod[0] && q_strcasestr(mod, "nqcrx"))
		return "ClanRing";
	return NULL;
}

static qboolean DiscordPresence_UserinfoValueEnabled(const char *value)
{
	return value && value[0] && q_strcasecmp(value, "off") &&
		q_strcasecmp(value, "0") && q_strcasecmp(value, "no") &&
		q_strcasecmp(value, "n");
}

static qboolean DiscordPresence_LocalPlayerSpectating(void)
{
	const char *userinfo = CL_GetSafeRealViewEntityUserinfo();
	char observer[16], star_observer[16], observing[64], spectator[16];

	Info_GetKey(userinfo, "observer", observer, sizeof(observer));
	Info_GetKey(userinfo, "*observer", star_observer, sizeof(star_observer));
	Info_GetKey(userinfo, "observing", observing, sizeof(observing));
	Info_GetKey(userinfo, "*spectator", spectator, sizeof(spectator));

	return DiscordPresence_UserinfoValueEnabled(observer) ||
		DiscordPresence_UserinfoValueEnabled(star_observer) ||
		DiscordPresence_UserinfoValueEnabled(observing) || atoi(spectator) > 0;
}

static qboolean DiscordPresence_ScoreboardPlayerSpectating(const scoreboard_t *score)
{
	char observer[16], star_observer[16], observing[64], spectator[16];

	if (score->spectator || score->frags == -99)
		return true;
	Info_GetKey(score->userinfo, "observer", observer, sizeof(observer));
	Info_GetKey(score->userinfo, "*observer", star_observer, sizeof(star_observer));
	Info_GetKey(score->userinfo, "observing", observing, sizeof(observing));
	Info_GetKey(score->userinfo, "*spectator", spectator, sizeof(spectator));
	return DiscordPresence_UserinfoValueEnabled(observer) ||
		DiscordPresence_UserinfoValueEnabled(star_observer) ||
		DiscordPresence_UserinfoValueEnabled(observing) || atoi(spectator) > 0;
}

static int DiscordPresence_CountPlayers(void)
{
	int count = 0;
	int i;

	if (!cl.scores)
		return 0;
	for (i = 0; i < cl.maxclients; ++i)
		if (cl.scores[i].name[0] &&
			!DiscordPresence_ScoreboardPlayerSpectating(&cl.scores[i]))
			++count;
	return count;
}

static void DiscordPresence_AddPlayerCount(discord_presence_snapshot_t *snapshot)
{
	char details[sizeof(snapshot->details)];

	q_strlcpy(details, snapshot->details, sizeof(details));
	q_snprintf(snapshot->details, sizeof(snapshot->details), "%s (%d/%d)",
		details, DiscordPresence_CountPlayers(), cl.maxclients);
}

static void DiscordPresence_BuildServerHover(discord_presence_snapshot_t *snapshot)
{
	char hostname[64] = "";
	char friendly[60] = "", requested[NET_NAMELEN] = "";
	char resolved[64] = "";
	const char *resolved_address;

	Info_GetKey(cl.serverinfo, "hostname", hostname, sizeof(hostname));
	DiscordPresence_CopyASCII(requested, sizeof(requested), lastmphost);
	DiscordPresence_CopyASCII(friendly, sizeof(friendly), hostname);
	if (!friendly[0])
		DiscordPresence_CopyASCII(friendly, sizeof(friendly), requested);
	resolved_address = cls.netcon ?
		NET_QSocketGetResolvedAddressString(cls.netcon) : NULL;
	DiscordPresence_CopyASCII(resolved, sizeof(resolved), resolved_address);

	if (!q_strcasecmp(requested, "local") || !q_strcasecmp(requested, "localhost"))
	{
		if (friendly[0] && q_strcasecmp(friendly, "local") &&
			q_strcasecmp(friendly, "localhost"))
			q_snprintf(snapshot->large_text, sizeof(snapshot->large_text),
				"%s - Local server", friendly);
		else
			q_strlcpy(snapshot->large_text, "Local server",
				sizeof(snapshot->large_text));
	}
	else if (friendly[0] && resolved[0] && q_strcasecmp(friendly, resolved))
		q_snprintf(snapshot->large_text, sizeof(snapshot->large_text),
			"%s (%s)", friendly, resolved);
	else
		q_strlcpy(snapshot->large_text, friendly[0] ? friendly : resolved,
			sizeof(snapshot->large_text));
}

static int64_t DiscordPresence_MapStartTimestamp(discord_presence_kind_t kind)
{
	if (discord_presence.timestamp_maptime != maptime ||
		discord_presence.timestamp_kind != kind || !discord_presence.timestamp_start)
	{
		Uint64 now_ticks = SDL_GetTicks64();
		uint64_t elapsed = (maptime && now_ticks >= maptime) ?
			(uint64_t)((now_ticks - maptime) / 1000) : 0;
		int64_t wall_time = (int64_t)time(NULL);

		if (elapsed > (uint64_t)wall_time)
			elapsed = 0;
		discord_presence.timestamp_maptime = maptime;
		discord_presence.timestamp_kind = kind;
		discord_presence.timestamp_start = wall_time - (int64_t)elapsed;
	}
	return discord_presence.timestamp_start;
}

static void DiscordPresence_BuildMultiplayer(discord_presence_snapshot_t *snapshot,
	const char *map)
{
	char mode[32] = "", gametype[32] = "", playmode[32] = "";
	char mod[64] = "", modname[64] = "";
	const char *mode_label, *playmode_label, *mod_label;
	qboolean spectating;

	Info_GetKey(cl.serverinfo, "mode", mode, sizeof(mode));
	Info_GetKey(cl.serverinfo, "gametype", gametype, sizeof(gametype));
	Info_GetKey(cl.serverinfo, "playmode", playmode, sizeof(playmode));
	Info_GetKey(cl.serverinfo, "mod", mod, sizeof(mod));
	Info_GetKey(cl.serverinfo, "modname", modname, sizeof(modname));

	mode_label = DiscordPresence_ModeLabel(mode[0] ? mode : gametype);
	playmode_label = DiscordPresence_PlaymodeLabel(playmode);
	mod_label = DiscordPresence_ModLabel(mod, modname);
	spectating = DiscordPresence_LocalPlayerSpectating();

	if (mod_label && mode_label)
		q_snprintf(snapshot->details, sizeof(snapshot->details), "%s - %s", mod_label, mode_label);
	else if (mod_label)
		q_strlcpy(snapshot->details, mod_label, sizeof(snapshot->details));
	else if (mode_label)
		q_snprintf(snapshot->details, sizeof(snapshot->details), "Multiplayer - %s", mode_label);
	else
		q_strlcpy(snapshot->details, "Multiplayer", sizeof(snapshot->details));

	if (spectating && playmode_label)
		q_snprintf(snapshot->state, sizeof(snapshot->state), "Spectating - %s - %s", map, playmode_label);
	else if (spectating)
		q_snprintf(snapshot->state, sizeof(snapshot->state), "Spectating - %s", map);
	else if (playmode_label)
		q_snprintf(snapshot->state, sizeof(snapshot->state), "%s - %s", map, playmode_label);
	else
		q_strlcpy(snapshot->state, map, sizeof(snapshot->state));
}

static void DiscordPresence_BuildSnapshot(discord_presence_snapshot_t *snapshot)
{
	discord_presence_kind_t kind;
	char map[sizeof(cl.mapname)];

	memset(snapshot, 0, sizeof(*snapshot));
	DiscordPresence_CopyASCII(map, sizeof(map), cl.mapname);
	if (!map[0])
		q_strlcpy(map, "Unknown map", sizeof(map));

	if (cls.state == ca_disconnected)
	{
		kind = DISCORD_PRESENCE_MENU;
		q_strlcpy(snapshot->details, "Main Menu", sizeof(snapshot->details));
		discord_presence.timestamp_start = 0;
		discord_presence.timestamp_maptime = 0;
		return;
	}
	if (cls.signon != SIGNONS)
	{
		kind = DISCORD_PRESENCE_CONNECTING;
		q_strlcpy(snapshot->details, "Connecting", sizeof(snapshot->details));
		discord_presence.timestamp_start = 0;
		discord_presence.timestamp_maptime = 0;
		return;
	}
	if (cls.demoplayback)
	{
		kind = DISCORD_PRESENCE_DEMO;
		q_strlcpy(snapshot->details, "Watching a Demo", sizeof(snapshot->details));
		q_strlcpy(snapshot->state, map, sizeof(snapshot->state));
	}
	else if (cl.maxclients <= 1)
	{
		kind = DISCORD_PRESENCE_SINGLE_PLAYER;
		q_strlcpy(snapshot->details, "Single Player", sizeof(snapshot->details));
		q_strlcpy(snapshot->state, map, sizeof(snapshot->state));
	}
	else if (cl.gametype == GAME_COOP)
	{
		kind = DISCORD_PRESENCE_COOP;
		q_strlcpy(snapshot->details, "Co-op", sizeof(snapshot->details));
		q_strlcpy(snapshot->state, map, sizeof(snapshot->state));
		DiscordPresence_AddPlayerCount(snapshot);
		DiscordPresence_BuildServerHover(snapshot);
	}
	else
	{
		kind = DISCORD_PRESENCE_MULTIPLAYER;
		DiscordPresence_BuildMultiplayer(snapshot, map);
		DiscordPresence_AddPlayerCount(snapshot);
		DiscordPresence_BuildServerHover(snapshot);
	}
	if (!cls.demoplayback && cl.intermission)
		q_snprintf(snapshot->state, sizeof(snapshot->state),
			"Intermission - %s", map);

	snapshot->start_timestamp = DiscordPresence_MapStartTimestamp(kind);
}

static void DiscordJSON_Init(discord_json_writer_t *writer, char *buffer, size_t capacity)
{
	writer->buffer = buffer;
	writer->capacity = capacity;
	writer->length = 0;
	writer->valid = capacity > 0;
	if (capacity)
		buffer[0] = '\0';
}

static void DiscordJSON_AppendBytes(discord_json_writer_t *writer,
	const char *data, size_t length)
{
	if (!writer->valid || length > writer->capacity - writer->length - 1)
	{
		writer->valid = false;
		return;
	}
	memcpy(writer->buffer + writer->length, data, length);
	writer->length += length;
	writer->buffer[writer->length] = '\0';
}

static void DiscordJSON_Append(discord_json_writer_t *writer, const char *text)
{
	DiscordJSON_AppendBytes(writer, text, strlen(text));
}

static void DiscordJSON_AppendString(discord_json_writer_t *writer, const char *text)
{
	static const char hex[] = "0123456789abcdef";
	const unsigned char *p = (const unsigned char *)text;

	DiscordJSON_Append(writer, "\"");
	while (writer->valid && *p)
	{
		char escaped[6];
		unsigned char c = *p++;

		if (c == '"' || c == '\\')
		{
			escaped[0] = '\\';
			escaped[1] = (char)c;
			DiscordJSON_AppendBytes(writer, escaped, 2);
		}
		else if (c >= 32 && c < 127)
		{
			escaped[0] = (char)c;
			DiscordJSON_AppendBytes(writer, escaped, 1);
		}
		else
		{
			escaped[0] = '\\';
			escaped[1] = 'u';
			escaped[2] = '0';
			escaped[3] = '0';
			escaped[4] = hex[(c >> 4) & 0xf];
			escaped[5] = hex[c & 0xf];
			DiscordJSON_AppendBytes(writer, escaped, sizeof(escaped));
		}
	}
	DiscordJSON_Append(writer, "\"");
}

static qboolean DiscordPresence_BuildJSON(const discord_presence_snapshot_t *snapshot,
	char *json, size_t json_size, size_t *json_length)
{
	discord_json_writer_t writer;
	char number[64];

	DiscordJSON_Init(&writer, json, json_size);
	DiscordJSON_Append(&writer, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":");
	q_snprintf(number, sizeof(number), "%u", DiscordIPC_ProcessId());
	DiscordJSON_Append(&writer, number);
	DiscordJSON_Append(&writer, ",\"activity\":{\"details\":");
	DiscordJSON_AppendString(&writer, snapshot->details);
	if (snapshot->state[0])
	{
		DiscordJSON_Append(&writer, ",\"state\":");
		DiscordJSON_AppendString(&writer, snapshot->state);
	}
	if (snapshot->start_timestamp > 0)
	{
		DiscordJSON_Append(&writer, ",\"timestamps\":{\"start\":");
		q_snprintf(number, sizeof(number), "%lld", (long long)snapshot->start_timestamp);
		DiscordJSON_Append(&writer, number);
		DiscordJSON_Append(&writer, "}");
	}
	if (snapshot->large_text[0])
	{
		DiscordJSON_Append(&writer, ",\"assets\":{\"large_image\":");
		DiscordJSON_AppendString(&writer, QSSM_DISCORD_LARGE_IMAGE);
		DiscordJSON_Append(&writer, ",\"large_text\":");
		DiscordJSON_AppendString(&writer, snapshot->large_text);
		DiscordJSON_Append(&writer, "}");
	}
	DiscordJSON_Append(&writer, "}},\"nonce\":");
	q_snprintf(number, sizeof(number), "%llu", (unsigned long long)++discord_presence.nonce);
	DiscordJSON_AppendString(&writer, number);
	DiscordJSON_Append(&writer, "}");

	if (!writer.valid)
		return false;
	*json_length = writer.length;
	return true;
}

static qboolean DiscordPresence_BuildClearJSON(char *json, size_t json_size,
	size_t *json_length)
{
	int length = q_snprintf(json, json_size,
		"{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%u,\"activity\":null},"
		"\"nonce\":\"%llu\"}", DiscordIPC_ProcessId(),
		(unsigned long long)++discord_presence.nonce);

	if (length < 0 || (size_t)length >= json_size)
		return false;
	*json_length = (size_t)length;
	return true;
}

static void DiscordPresence_ScheduleRetry(double now)
{
	DiscordIPC_Disconnect();
	discord_presence.was_ready = false;
	discord_presence.have_sent = false;
	discord_presence.have_pending = false;
	discord_presence.next_snapshot = 0;
	discord_presence.connection_deadline = 0;
	discord_presence.next_retry = now + discord_presence.retry_delay;
	discord_presence.retry_delay = q_min(discord_presence.retry_delay * 2.0,
		DISCORD_PRESENCE_RETRY_MAX);
}

static void DiscordPresence_FinishDisable(void)
{
	DiscordIPC_Disconnect();
	discord_presence.enabled = false;
	discord_presence.disabling = false;
	discord_presence.clear_queued = false;
	discord_presence.was_ready = false;
	discord_presence.have_sent = false;
	discord_presence.have_pending = false;
	discord_presence.next_retry = 0;
	discord_presence.retry_delay = DISCORD_PRESENCE_RETRY_INITIAL;
	discord_presence.connection_deadline = 0;
	discord_presence.clear_deadline = 0;
	discord_presence.next_snapshot = 0;
	discord_presence.timestamp_start = 0;
	discord_presence.timestamp_maptime = 0;
}

static void DiscordPresence_BeginDisable(double now)
{
	discord_presence.enabled = false;
	discord_presence.have_pending = false;
	discord_presence.connection_deadline = 0;
	if (!DiscordIPC_IsReady())
	{
		DiscordPresence_FinishDisable();
		return;
	}
	discord_presence.disabling = true;
	discord_presence.clear_queued = false;
	discord_presence.clear_deadline = now + DISCORD_PRESENCE_CLEAR_TIMEOUT;
}

static void DiscordPresence_DriveDisable(double now)
{
	char json[256];
	size_t json_length;

	if (!discord_presence.disabling)
		return;
	if (!DiscordIPC_IsReady() || now >= discord_presence.clear_deadline)
	{
		DiscordPresence_FinishDisable();
		return;
	}

	DiscordIPC_Pump();
	if (!DiscordIPC_IsReady())
	{
		DiscordPresence_FinishDisable();
		return;
	}
	if (!discord_presence.clear_queued && !DiscordIPC_IsBusy())
	{
		if (!DiscordPresence_BuildClearJSON(json, sizeof(json), &json_length) ||
			!DiscordIPC_SendJSON(json, json_length))
		{
			DiscordPresence_FinishDisable();
			return;
		}
		discord_presence.clear_queued = true;
		discord_presence.next_send = now + DISCORD_PRESENCE_SEND_INTERVAL;
	}
	if (discord_presence.clear_queued)
	{
		DiscordIPC_Pump();
		if (!DiscordIPC_IsBusy())
			DiscordPresence_FinishDisable();
	}
}

void DiscordPresence_Init(void)
{
	memset(&discord_presence, 0, sizeof(discord_presence));
	discord_presence.retry_delay = DISCORD_PRESENCE_RETRY_INITIAL;
	discord_presence.application_id_valid = DiscordPresence_ApplicationIdValid();
	DiscordIPC_Init();
	Cvar_RegisterVariable(&cl_discord_presence);
	discord_presence.initialized = true;
}

void DiscordPresence_Shutdown(void)
{
	if (!discord_presence.initialized)
		return;
	if (discord_presence.enabled)
		DiscordPresence_BeginDisable(realtime);
	DiscordPresence_DriveDisable(realtime);
	DiscordIPC_Shutdown();
	memset(&discord_presence, 0, sizeof(discord_presence));
}

void DiscordPresence_Frame(void)
{
	double now;

	if (!discord_presence.initialized)
		return;
	now = realtime;
	if (discord_presence.disabling)
	{
		DiscordPresence_DriveDisable(now);
		if (discord_presence.disabling)
			return;
	}
	if (!cl_discord_presence.value || cls.state == ca_dedicated)
	{
		if (discord_presence.enabled)
		{
			DiscordPresence_BeginDisable(now);
			DiscordPresence_DriveDisable(now);
		}
		return;
	}
	if (!discord_presence.application_id_valid)
	{
		if (!discord_presence.warned_application_id)
		{
			Con_Printf("Discord Presence unavailable: this build has no valid application ID\n");
			discord_presence.warned_application_id = true;
		}
		return;
	}

	if (!discord_presence.enabled)
	{
		discord_presence.enabled = true;
		discord_presence.next_retry = now;
		discord_presence.retry_delay = DISCORD_PRESENCE_RETRY_INITIAL;
	}

	if (!DiscordIPC_IsOpen())
	{
		if (now < discord_presence.next_retry)
			return;
		if (!DiscordIPC_Connect(QSSM_DISCORD_APP_ID))
		{
			DiscordPresence_ScheduleRetry(now);
			return;
		}
		discord_presence.next_ipc_pump = now;
		discord_presence.connection_deadline =
			now + DISCORD_PRESENCE_HANDSHAKE_TIMEOUT;
	}

	if (now < discord_presence.next_ipc_pump)
	{
		if (!DiscordIPC_IsReady() && discord_presence.connection_deadline &&
			now >= discord_presence.connection_deadline)
			DiscordPresence_ScheduleRetry(now);
		return;
	}
	discord_presence.next_ipc_pump = now + DISCORD_PRESENCE_IPC_INTERVAL;
	DiscordIPC_Pump();
	if (!DiscordIPC_IsOpen())
	{
		DiscordPresence_ScheduleRetry(now);
		return;
	}
	if (!DiscordIPC_IsReady())
	{
		if (discord_presence.connection_deadline &&
			now >= discord_presence.connection_deadline)
			DiscordPresence_ScheduleRetry(now);
		return;
	}
	discord_presence.connection_deadline = 0;

	if (!discord_presence.was_ready)
	{
		discord_presence.was_ready = true;
		discord_presence.retry_delay = DISCORD_PRESENCE_RETRY_INITIAL;
		discord_presence.next_snapshot = 0;
		discord_presence.next_send = 0;
		discord_presence.have_sent = false;
		Con_DPrintf("Discord Presence connected\n");
	}

	if (now >= discord_presence.next_snapshot)
	{
		discord_presence_snapshot_t snapshot;

		discord_presence.next_snapshot = now + DISCORD_PRESENCE_SNAPSHOT_INTERVAL;
		DiscordPresence_BuildSnapshot(&snapshot);
		if (!discord_presence.have_sent ||
			memcmp(&snapshot, &discord_presence.sent, sizeof(snapshot)))
		{
			discord_presence.pending = snapshot;
			discord_presence.have_pending = true;
		}
		else
			discord_presence.have_pending = false;
	}

	if (discord_presence.have_pending && now >= discord_presence.next_send &&
		!DiscordIPC_IsBusy())
	{
		char json[1024];
		size_t json_length;

		if (!DiscordPresence_BuildJSON(&discord_presence.pending,
			json, sizeof(json), &json_length))
		{
			Con_DWarning("Discord Presence payload exceeded its fixed buffer\n");
			discord_presence.have_pending = false;
			return;
		}
		if (DiscordIPC_SendJSON(json, json_length))
		{
			discord_presence.sent = discord_presence.pending;
			discord_presence.have_sent = true;
			discord_presence.have_pending = false;
			discord_presence.next_send = now + DISCORD_PRESENCE_SEND_INTERVAL;
		}
	}
}
