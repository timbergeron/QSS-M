/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QSS-M Aura bridge.
 *
 * This is a tiny, read-only HTTP/1.1 chunked stream.  It is intentionally
 * not a general HTTP parser or web server: one bounded request, one fixed
 * endpoint, one bearer token, and one byte of application data.
 */

#include "quakedef.h"
#include "qwatch.h"
#include "arch_def.h"
#include "net_sys.h"
#include "sys.h"

#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <dns_sd.h>
#endif

#define QWATCH_DEFAULT_PORT 27999
#define QWATCH_REQUEST_MAX 512
#define QWATCH_TOKEN_HEX 64
#define QWATCH_MAX_CLIENTS 2
#define QWATCH_TX_MAX 512
#define QWATCH_REQUEST_TIMEOUT 5.0
#define QWATCH_POLL_INTERVAL 0.05
#define QWATCH_LISTENER_RETRY 1.0
#define QWATCH_PAIR_CODE_LENGTH 6
#define QWATCH_PAIR_TTL 300.0
#define QWATCH_PAIR_MAX_ATTEMPTS 12
#define QWATCH_BONJOUR_TYPE "_qssm-aura._tcp"
#define QWATCH_BONJOUR_HOST "qssm-aura.local."
#define QWATCH_BONJOUR_ADDRESS_POLL 1.0

#define QWATCH_AURA_PENT 1
#define QWATCH_AURA_QUAD 2

cvar_t qwatch_aura = {"qwatch_aura", "0", CVAR_ARCHIVE};
cvar_t qwatch_port = {"qwatch_port", "27999", CVAR_ARCHIVE};

typedef struct
{
	sys_socket_t socket;
	char request[QWATCH_REQUEST_MAX];
	size_t request_length;
	qboolean authenticated;
	char tx[QWATCH_TX_MAX];
	size_t tx_length;
	size_t tx_offset;
	int pending_aura;
	qboolean pending_valid;
	qboolean close_after_flush;
	double connected_at;
} qwatch_client_t;

static sys_socket_t qwatch_listener = INVALID_SOCKET;
static int qwatch_bound_port = 0;
static qwatch_client_t qwatch_clients[QWATCH_MAX_CLIENTS];
static char qwatch_token[QWATCH_TOKEN_HEX + 1];
static int qwatch_last_aura = -1;
static qboolean qwatch_token_loaded = false;
static double qwatch_next_poll = 0;
static double qwatch_listener_retry = 0;
static char qwatch_pair_code[QWATCH_PAIR_CODE_LENGTH + 1];
static double qwatch_pair_expires = 0;
static qboolean qwatch_pair_used = false;
static unsigned int qwatch_pair_attempts = 0;

#ifdef __APPLE__
static DNSServiceRef qwatch_bonjour = NULL;
static DNSServiceRef qwatch_bonjour_host = NULL;
static DNSRecordRef qwatch_bonjour_host_record = NULL;
static uint32_t qwatch_bonjour_host_address = 0;
static double qwatch_bonjour_host_next_poll = 0;
#endif

static const char qwatch_response_headers[] =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: application/octet-stream\r\n"
	"Transfer-Encoding: chunked\r\n"
	"Cache-Control: no-cache\r\n"
	"Connection: keep-alive\r\n"
	"\r\n";

static const char qwatch_unauthorized_response[] =
	"HTTP/1.1 401 Unauthorized\r\n"
	"Content-Length: 0\r\n"
	"Connection: close\r\n"
	"\r\n";

static void QWatch_CloseClient(qwatch_client_t *client)
{
	if (client->socket != INVALID_SOCKET)
		closesocket(client->socket);
	memset(client, 0, sizeof(*client));
	client->socket = INVALID_SOCKET;
}

static void QWatch_StopBonjour(void)
{
#ifdef __APPLE__
	if (qwatch_bonjour)
		DNSServiceRefDeallocate(qwatch_bonjour);
	qwatch_bonjour = NULL;
	if (qwatch_bonjour_host)
		DNSServiceRefDeallocate(qwatch_bonjour_host);
	qwatch_bonjour_host = NULL;
	qwatch_bonjour_host_record = NULL;
	qwatch_bonjour_host_address = 0;
	qwatch_bonjour_host_next_poll = 0;
#endif
}

#ifdef __APPLE__
static qboolean QWatch_GetPrimaryIPv4(uint32_t *address)
{
	struct sockaddr_in remote, local;
	socklen_t local_length = sizeof(local);
	sys_socket_t socket_id;

	/* A connected UDP socket reveals the source address selected by the
	 * routing table without sending any traffic. The documentation-only
	 * destination is intentionally never contacted. */
	socket_id = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (socket_id == INVALID_SOCKET)
		return false;
	memset(&remote, 0, sizeof(remote));
	remote.sin_family = AF_INET;
	remote.sin_port = htons(9);
	remote.sin_addr.s_addr = htonl(UINT32_C(0xc0000201)); /* 192.0.2.1 */
	if (connect(socket_id, (struct sockaddr *)&remote, sizeof(remote)) == SOCKET_ERROR)
	{
		closesocket(socket_id);
		return false;
	}
	memset(&local, 0, sizeof(local));
	if (getsockname(socket_id, (struct sockaddr *)&local, &local_length) == SOCKET_ERROR)
	{
		closesocket(socket_id);
		return false;
	}
	closesocket(socket_id);
	if (local.sin_addr.s_addr == htonl(INADDR_ANY) ||
		(ntohl(local.sin_addr.s_addr) >> 24) == 127)
		return false;
	*address = local.sin_addr.s_addr;
	return true;
}

static void DNSSD_API QWatch_BonjourHostReply(DNSServiceRef service,
	DNSRecordRef record, DNSServiceFlags flags, DNSServiceErrorType error,
	void *context)
{
	(void)service;
	(void)record;
	(void)flags;
	(void)error;
	(void)context;
}

static void QWatch_StartBonjourHost(void)
{
	DNSServiceErrorType error;
	uint32_t address;

	if (!QWatch_GetPrimaryIPv4(&address))
		return;
	error = DNSServiceCreateConnection(&qwatch_bonjour_host);
	if (error == kDNSServiceErr_NoError)
		error = DNSServiceRegisterRecord(qwatch_bonjour_host,
			&qwatch_bonjour_host_record, kDNSServiceFlagsUnique, 0,
			QWATCH_BONJOUR_HOST, kDNSServiceType_A, kDNSServiceClass_IN,
			(uint16_t)sizeof(address), &address, 0,
			QWatch_BonjourHostReply, NULL);
	if (error != kDNSServiceErr_NoError)
	{
		if (qwatch_bonjour_host)
			DNSServiceRefDeallocate(qwatch_bonjour_host);
		qwatch_bonjour_host = NULL;
		qwatch_bonjour_host_record = NULL;
		Con_DPrintf("Aura Bonjour host registration failed (%d)\n", (int)error);
		return;
	}
	qwatch_bonjour_host_address = address;
}

static void QWatch_UpdateBonjourHost(double now)
{
	DNSServiceErrorType error;
	uint32_t address;

	if (now < qwatch_bonjour_host_next_poll)
		return;
	qwatch_bonjour_host_next_poll = now + QWATCH_BONJOUR_ADDRESS_POLL;
	if (!qwatch_bonjour_host)
	{
		QWatch_StartBonjourHost();
		return;
	}
	if (!QWatch_GetPrimaryIPv4(&address) || address == qwatch_bonjour_host_address)
		return;
	error = DNSServiceUpdateRecord(qwatch_bonjour_host, qwatch_bonjour_host_record,
		0, (uint16_t)sizeof(address), &address, 0);
	if (error == kDNSServiceErr_NoError)
		qwatch_bonjour_host_address = address;
	else
		Con_DPrintf("Aura Bonjour host update failed (%d)\n", (int)error);
}
#endif

static void QWatch_StartBonjour(int port)
{
#ifdef __APPLE__
	DNSServiceErrorType error;

	QWatch_StopBonjour();
	QWatch_StartBonjourHost();
	error = DNSServiceRegister(&qwatch_bonjour, 0, 0, "QSS-M Aura",
		QWATCH_BONJOUR_TYPE, NULL, NULL, htons((unsigned short)port),
		0, NULL, NULL, NULL);
	if (error != kDNSServiceErr_NoError)
	{
		qwatch_bonjour = NULL;
		Con_DPrintf("Aura Bonjour advertisement failed (%d)\n", (int)error);
	}
#else
	(void)port;
#endif
}

static void QWatch_CloseListener(void)
{
	int i;

	QWatch_StopBonjour();
	if (qwatch_listener != INVALID_SOCKET)
		closesocket(qwatch_listener);
	qwatch_listener = INVALID_SOCKET;
	qwatch_bound_port = 0;
	for (i = 0; i < QWATCH_MAX_CLIENTS; ++i)
		QWatch_CloseClient(&qwatch_clients[i]);
}

static qboolean QWatch_SetNonBlocking(sys_socket_t socket_id)
{
	/* Winsock's ioctlsocket API requires an unsigned long argument, while
	 * the Unix ioctl compatibility path takes an int. */
#ifdef PLATFORM_WINDOWS
	u_long one = 1;
#else
	int one = 1;
#endif
	return ioctlsocket(socket_id, FIONBIO, &one) != SOCKET_ERROR;
}

static qboolean QWatch_IsWouldBlock(void)
{
	int error = SOCKETERRNO;
	if (error == NET_EWOULDBLOCK)
		return true;
#ifdef NET_EINTR
	if (error == NET_EINTR)
		return true;
#endif
	return false;
}

static int QWatch_GetPort(void)
{
	int port = (int)qwatch_port.value;
	return (port >= 1024 && port <= 65535) ? port : QWATCH_DEFAULT_PORT;
}

static qboolean QWatch_RandomBytes(unsigned char *bytes, size_t length)
{
#ifdef _WIN32
	HCRYPTPROV provider;
	qboolean result = false;

	if (length > UINT32_MAX)
		return false;
	if (CryptAcquireContext(&provider, NULL, NULL, PROV_RSA_FULL,
		CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
	{
		result = CryptGenRandom(provider, (DWORD)length, bytes) != 0;
		CryptReleaseContext(provider, 0);
	}
	return result;
#else
	int fd;
	size_t offset = 0;

	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		return false;
	while (offset < length)
	{
		ssize_t count = read(fd, bytes + offset, length - offset);
		if (count <= 0)
		{
			close(fd);
			return false;
		}
		offset += (size_t)count;
	}
	close(fd);
	return true;
#endif
}

static qboolean QWatch_GenerateToken(void)
{
	static const char hex[] = "0123456789abcdef";
	unsigned char random[QWATCH_TOKEN_HEX / 2];
	int i;

	if (!QWatch_RandomBytes(random, sizeof(random)))
		return false;
	for (i = 0; i < (int)sizeof(random); ++i)
	{
		qwatch_token[i * 2] = hex[random[i] >> 4];
		qwatch_token[i * 2 + 1] = hex[random[i] & 0xf];
	}
	qwatch_token[QWATCH_TOKEN_HEX] = '\0';
	return true;
}

static qboolean QWatch_TokenValid(void)
{
	int i;
	for (i = 0; i < QWATCH_TOKEN_HEX; ++i)
		if (!((qwatch_token[i] >= '0' && qwatch_token[i] <= '9') ||
			(qwatch_token[i] >= 'a' && qwatch_token[i] <= 'f') ||
			(qwatch_token[i] >= 'A' && qwatch_token[i] <= 'F')))
			return false;
	return qwatch_token[QWATCH_TOKEN_HEX] == '\0';
}

static qboolean QWatch_GeneratePairCode(void)
{
	uint32_t random;
	const uint32_t limit = UINT32_MAX - (UINT32_MAX % 1000000U);

	do
	{
		if (!QWatch_RandomBytes((unsigned char *)&random, sizeof(random)))
			return false;
	} while (random >= limit);
	q_snprintf(qwatch_pair_code, sizeof(qwatch_pair_code), "%06u",
		(unsigned int)(random % 1000000U));
	qwatch_pair_expires = Sys_DoubleTime() + QWATCH_PAIR_TTL;
	qwatch_pair_used = false;
	qwatch_pair_attempts = 0;
	return true;
}

static qboolean QWatch_RequestIsPairRequest(const char *request, size_t length)
{
	static const char prefix[] = "GET /v1/pair?code=";
	return length >= sizeof(prefix) - 1 &&
		memcmp(request, prefix, sizeof(prefix) - 1) == 0;
}

static qboolean QWatch_RequestIsDiscoveryRequest(const char *request, size_t length)
{
	static const char prefix[] = "GET /v1/discover HTTP/1.1\r\n";
	return length >= sizeof(prefix) - 1 + 4 &&
		memcmp(request, prefix, sizeof(prefix) - 1) == 0 &&
		memcmp(request + length - 4, "\r\n\r\n", 4) == 0;
}

static qboolean QWatch_RequestHasPairCode(const char *request, size_t length)
{
	static const char prefix[] = "GET /v1/pair?code=";
	static const char suffix[] = " HTTP/1.1\r\n";
	char code[QWATCH_PAIR_CODE_LENGTH + 1];
	const char *cursor;
	int i;

	if (qwatch_pair_attempts >= QWATCH_PAIR_MAX_ATTEMPTS ||
		length < sizeof(prefix) - 1 + QWATCH_PAIR_CODE_LENGTH + sizeof(suffix) - 1 + 4)
		return false;
	if (memcmp(request, prefix, sizeof(prefix) - 1) != 0)
		return false;
	cursor = request + sizeof(prefix) - 1;
	for (i = 0; i < QWATCH_PAIR_CODE_LENGTH; ++i)
	{
		if (cursor[i] < '0' || cursor[i] > '9')
			return false;
		code[i] = cursor[i];
	}
	code[QWATCH_PAIR_CODE_LENGTH] = '\0';
	if (memcmp(cursor + QWATCH_PAIR_CODE_LENGTH, suffix, sizeof(suffix) - 1) != 0)
		return false;
	if (memcmp(request + length - 4, "\r\n\r\n", 4) != 0)
		return false;
	return qwatch_pair_code[0] && !qwatch_pair_used &&
		Sys_DoubleTime() <= qwatch_pair_expires &&
		strcmp(code, qwatch_pair_code) == 0;
}

static qboolean QWatch_UserinfoValueEnabled(const char *value)
{
	return value && value[0] && q_strcasecmp(value, "off") &&
		q_strcasecmp(value, "0") && q_strcasecmp(value, "no") &&
		q_strcasecmp(value, "n");
}

qboolean QWatch_LocalPlayerSpectating(void)
{
	const char *userinfo = CL_GetSafeRealViewEntityUserinfo();
	char observer[16], star_observer[16], observing[64], spectator[16];

	Info_GetKey(userinfo, "observer", observer, sizeof(observer));
	Info_GetKey(userinfo, "*observer", star_observer, sizeof(star_observer));
	Info_GetKey(userinfo, "observing", observing, sizeof(observing));
	Info_GetKey(userinfo, "*spectator", spectator, sizeof(spectator));
	return QWatch_UserinfoValueEnabled(observer) ||
		QWatch_UserinfoValueEnabled(star_observer) ||
		QWatch_UserinfoValueEnabled(observing) || atoi(spectator) > 0;
}

static FILE *QWatch_OpenPrivateTokenFile(const char *path)
{
#ifdef _WIN32
	return fopen(path, "wb");
#else
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	FILE *file;

	if (fd < 0)
		return NULL;
	file = fdopen(fd, "wb");
	if (!file)
		close(fd);
	return file;
#endif
}

static void QWatch_LoadToken(void)
{
	char path[MAX_OSPATH];
	FILE *file;
	size_t length;

	qwatch_token_loaded = false;
	qwatch_token[0] = '\0';
	if (com_gamedir[0])
	{
		q_snprintf(path, sizeof(path), "%s/qwatch.token", com_gamedir);
		file = fopen(path, "rb");
		if (file)
		{
			length = fread(qwatch_token, 1, QWATCH_TOKEN_HEX + 1, file);
			fclose(file);
			if (length > QWATCH_TOKEN_HEX &&
				qwatch_token[length - 1] != '\n' && qwatch_token[length - 1] != '\r')
				length = 0;
			if (length > 0 && qwatch_token[length - 1] == '\n')
				qwatch_token[--length] = '\0';
			if (length > 0 && qwatch_token[length - 1] == '\r')
				qwatch_token[--length] = '\0';
			qwatch_token[length] = '\0';
			qwatch_token_loaded = QWatch_TokenValid();
#ifndef _WIN32
			if (qwatch_token_loaded)
				chmod(path, S_IRUSR | S_IWUSR);
#endif
		}
	}

	if (!qwatch_token_loaded)
	{
		if (!QWatch_GenerateToken())
		{
			Con_Printf("Aura could not obtain secure random data; pairing is disabled.\n");
			return;
		}
		if (com_gamedir[0])
		{
			q_snprintf(path, sizeof(path), "%s/qwatch.token", com_gamedir);
			file = QWatch_OpenPrivateTokenFile(path);
			if (file)
			{
				qboolean saved = fwrite(qwatch_token, 1, QWATCH_TOKEN_HEX, file) ==
					QWATCH_TOKEN_HEX && fputc('\n', file) != EOF;
				if (fclose(file) == EOF)
					saved = false;
				if (!saved)
					Con_Printf("Aura could not save its token to %s; pairing will not persist.\n",
						path);
			}
			else
			{
				Con_Printf("Aura could not save its token to %s; pairing will not persist.\n",
					path);
			}
		}
		qwatch_token_loaded = true;
	}
}

static qboolean QWatch_RequestHasToken(const char *request, size_t length)
{
	static const char prefix[] = "GET /v1/aura HTTP/1.1\r\n";
	static const char auth_prefix[] = "\r\nAuthorization: Bearer ";
	const char *auth;
	const char *end;
	size_t prefix_length = sizeof(prefix) - 1;
	size_t auth_prefix_length = sizeof(auth_prefix) - 1;

	if (length < prefix_length || memcmp(request, prefix, prefix_length) != 0)
		return false;
	if (length < 4 || memcmp(request + length - 4, "\r\n\r\n", 4) != 0)
		return false;
	auth = q_strcasestr(request, auth_prefix);
	if (!auth)
		return false;
	auth += auth_prefix_length;
	end = strstr(auth, "\r\n");
	if (!end || (size_t)(end - auth) != QWATCH_TOKEN_HEX)
		return false;
	return memcmp(auth, qwatch_token, QWATCH_TOKEN_HEX) == 0;
}

static qboolean QWatch_Queue(qwatch_client_t *client, const char *data, size_t length)
{
	if (client->tx_offset < client->tx_length)
		return false;
	if (length > sizeof(client->tx))
		return false;
	memcpy(client->tx, data, length);
	client->tx_length = length;
	client->tx_offset = 0;
	return true;
}

static qboolean QWatch_QueuePairResponse(qwatch_client_t *client)
{
	char body[QWATCH_TOKEN_HEX + 16];
	char response[QWATCH_TX_MAX];
	int body_length;
	int response_length;

	body_length = q_snprintf(body, sizeof(body), "{\"token\":\"%s\"}\n", qwatch_token);
	response_length = q_snprintf(response, sizeof(response),
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"\r\n"
		"%s", body_length, body);
	return response_length > 0 && QWatch_Queue(client, response, (size_t)response_length);
}

static qboolean QWatch_QueueDiscoveryResponse(qwatch_client_t *client)
{
	char body[96];
	char response[QWATCH_TX_MAX];
	int body_length;
	int response_length;

	body_length = q_snprintf(body, sizeof(body),
		"{\"name\":\"QSS-M Aura\",\"port\":%d}\n", qwatch_bound_port);
	response_length = q_snprintf(response, sizeof(response),
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"\r\n"
		"%s", body_length, body);
	return response_length > 0 && QWatch_Queue(client, response, (size_t)response_length);
}

static void QWatch_QueueAura(qwatch_client_t *client, int aura)
{
	char chunk[16];
	int length;

	length = q_snprintf(chunk, sizeof(chunk), "1\r\n%c\r\n", (char)aura);
	if (length > 0 && !QWatch_Queue(client, chunk, (size_t)length))
	{
		client->pending_aura = aura;
		client->pending_valid = true;
	}
}

static void QWatch_Flush(qwatch_client_t *client)
{
	while (client->tx_offset < client->tx_length)
	{
		int flags = 0;
		int sent;
#ifdef MSG_NOSIGNAL
		flags |= MSG_NOSIGNAL;
#endif
		sent = (int)send(client->socket, client->tx + client->tx_offset,
			client->tx_length - client->tx_offset, flags);
		if (sent > 0)
		{
			client->tx_offset += (size_t)sent;
			continue;
		}
		if (sent < 0 && QWatch_IsWouldBlock())
			return;
		QWatch_CloseClient(client);
		return;
	}
	if (client->close_after_flush)
	{
		QWatch_CloseClient(client);
		return;
	}
	if (client->authenticated && client->pending_valid)
	{
		int aura = client->pending_aura;
		client->pending_valid = false;
		QWatch_QueueAura(client, aura);
	}
}

static void QWatch_PreventSigpipe(sys_socket_t socket_id)
{
#ifdef SO_NOSIGPIPE
	int one = 1;
	setsockopt(socket_id, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one, sizeof(one));
#else
	(void)socket_id;
#endif
}

static void QWatch_Accept(void)
{
	struct sockaddr_in address;
	socklen_t address_length = sizeof(address);
	sys_socket_t socket_id;
	int i;

	socket_id = accept(qwatch_listener, (struct sockaddr *)&address, &address_length);
	if (socket_id == INVALID_SOCKET)
		return;
	if (!QWatch_SetNonBlocking(socket_id))
	{
		closesocket(socket_id);
		return;
	}
	QWatch_PreventSigpipe(socket_id);
	for (i = 0; i < QWATCH_MAX_CLIENTS; ++i)
		if (qwatch_clients[i].socket == INVALID_SOCKET)
		{
			qwatch_clients[i].socket = socket_id;
			qwatch_clients[i].connected_at = Sys_DoubleTime();
			return;
		}
	closesocket(socket_id);
}

static qboolean QWatch_OpenListener(void)
{
	struct sockaddr_in address;
	int reuse = 1;
	int port = QWatch_GetPort();
	qwatch_listener = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (qwatch_listener == INVALID_SOCKET)
		return false;
	QWatch_PreventSigpipe(qwatch_listener);
	setsockopt(qwatch_listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
	if (!QWatch_SetNonBlocking(qwatch_listener))
	{
		QWatch_CloseListener();
		return false;
	}
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons((unsigned short)port);
	if (bind(qwatch_listener, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR ||
		listen(qwatch_listener, QWATCH_MAX_CLIENTS) == SOCKET_ERROR)
	{
		QWatch_CloseListener();
		return false;
	}
	qwatch_bound_port = port;
	QWatch_StartBonjour(port);
	return true;
}

static void QWatch_ProcessClient(qwatch_client_t *client, double now)
{
	int received;

	if (client->socket == INVALID_SOCKET)
		return;
	if (!client->authenticated)
	{
		if (now - client->connected_at > QWATCH_REQUEST_TIMEOUT)
		{
			QWatch_CloseClient(client);
			return;
		}
		received = (int)recv(client->socket, client->request + client->request_length,
			(int)(sizeof(client->request) - 1 - client->request_length), 0);
		if (received > 0)
		{
			client->request_length += (size_t)received;
			client->request[client->request_length] = '\0';
			if (client->request_length >= 4 &&
				memcmp(client->request + client->request_length - 4, "\r\n\r\n", 4) == 0)
			{
				if (QWatch_RequestIsDiscoveryRequest(client->request, client->request_length))
				{
					client->authenticated = true;
					client->close_after_flush = true;
					if (!QWatch_QueueDiscoveryResponse(client))
						QWatch_CloseClient(client);
					if (client->socket != INVALID_SOCKET)
						QWatch_Flush(client);
					return;
				}
				if (QWatch_RequestIsPairRequest(client->request, client->request_length))
				{
					if (QWatch_RequestHasPairCode(client->request, client->request_length))
					{
						qwatch_pair_used = true;
						client->authenticated = true;
						client->close_after_flush = true;
						if (!QWatch_QueuePairResponse(client))
							QWatch_CloseClient(client);
						else
							Con_Printf("Aura Watch paired.\n");
					}
					else
					{
						qwatch_pair_attempts++;
						if (qwatch_pair_attempts >= QWATCH_PAIR_MAX_ATTEMPTS)
						{
							qwatch_pair_used = true;
							qwatch_pair_expires = 0;
						}
						QWatch_CloseClient(client);
					}
					if (client->socket != INVALID_SOCKET)
						QWatch_Flush(client);
					return;
				}
				if (!QWatch_RequestHasToken(client->request, client->request_length))
				{
					client->close_after_flush = true;
					if (!QWatch_Queue(client, qwatch_unauthorized_response,
						sizeof(qwatch_unauthorized_response) - 1))
						QWatch_CloseClient(client);
					if (client->socket != INVALID_SOCKET)
						QWatch_Flush(client);
					return;
				}
				client->authenticated = true;
				QWatch_Queue(client, qwatch_response_headers, sizeof(qwatch_response_headers) - 1);
				QWatch_QueueAura(client, qwatch_last_aura < 0 ? 0 : qwatch_last_aura);
			}
		}
		else if (received < 0 && !QWatch_IsWouldBlock())
			QWatch_CloseClient(client);
		else if (received == 0 || client->request_length == sizeof(client->request) - 1)
			QWatch_CloseClient(client);
	}
	else
	{
		char probe;
		received = (int)recv(client->socket, &probe, 1, MSG_PEEK);
		if (received == 0 || (received < 0 && !QWatch_IsWouldBlock()))
			QWatch_CloseClient(client);
	}
	if (client->socket != INVALID_SOCKET)
		QWatch_Flush(client);
}

static void QWatch_Status_f(void)
{
	char path[MAX_OSPATH];
	if (com_gamedir[0])
		q_snprintf(path, sizeof(path), "%s/qwatch.token", com_gamedir);
	else
		q_strlcpy(path, "qwatch.token", sizeof(path));
	Con_Printf("qwatch_aura %s, port %d, %s\n", qwatch_aura.value ? "on" : "off",
		qwatch_bound_port, qwatch_bound_port ? "listening" : "not listening");
	Con_Printf("qwatch token: configured (stored on disk)\n");
	Con_Printf("qwatch token file: %s\n", path);
	if (qwatch_pair_code[0] && !qwatch_pair_used &&
		Sys_DoubleTime() <= qwatch_pair_expires)
		Con_Printf("qwatch pairing code: %s (valid for 5 minutes)\n", qwatch_pair_code);
	else if (qwatch_pair_code[0])
		Con_Printf("qwatch pairing code: expired or already used\n");
}

static void QWatch_Pair_f(void)
{
	if (!qwatch_token_loaded)
	{
		QWatch_LoadToken();
		if (!qwatch_token_loaded)
			return;
	}
	if (!qwatch_aura.value)
	{
		Cvar_SetQuick(&qwatch_aura, "1");
		Con_Printf("Aura enabled on port %d.\n", QWatch_GetPort());
	}
	if (!QWatch_GeneratePairCode())
	{
		Con_Printf("Aura could not obtain secure random data; pairing failed.\n");
		return;
	}
	Con_Printf("Aura pairing code: %s (valid for 5 minutes)\n", qwatch_pair_code);
}

void QWatch_InitLocal(void)
{
	int i;
	for (i = 0; i < QWATCH_MAX_CLIENTS; ++i)
		qwatch_clients[i].socket = INVALID_SOCKET;
	qwatch_next_poll = 0;
	qwatch_listener_retry = 0;
	qwatch_pair_code[0] = '\0';
	qwatch_pair_expires = 0;
	qwatch_pair_used = false;
	qwatch_pair_attempts = 0;
	QWatch_LoadToken();
	Cmd_AddCommand("qwatch_status", QWatch_Status_f);
	Cmd_AddCommand("qwatch_pair", QWatch_Pair_f);
}

void QWatch_Frame(qboolean valid, unsigned int items)
{
	int i;
	double now;
	int aura = valid ? (((items & IT_INVULNERABILITY) ? QWATCH_AURA_PENT : 0) |
		((items & IT_QUAD) ? QWATCH_AURA_QUAD : 0)) : 0;

	if (!qwatch_aura.value)
	{
		if (qwatch_listener != INVALID_SOCKET)
			QWatch_CloseListener();
		qwatch_last_aura = -1;
		return;
	}
	if (!qwatch_token_loaded)
		return;
	now = Sys_DoubleTime();
	if (qwatch_listener == INVALID_SOCKET || qwatch_bound_port != QWatch_GetPort())
	{
		if (qwatch_listener != INVALID_SOCKET)
			QWatch_CloseListener();
		if (now < qwatch_listener_retry)
			return;
		if (!QWatch_OpenListener())
		{
			qwatch_listener_retry = now + QWATCH_LISTENER_RETRY;
			return;
		}
	}
	if (now < qwatch_next_poll)
		return;
	qwatch_next_poll = now + QWATCH_POLL_INTERVAL;
#ifdef __APPLE__
	QWatch_UpdateBonjourHost(now);
#endif
	QWatch_Accept();
	if (aura != qwatch_last_aura)
	{
		qwatch_last_aura = aura;
		for (i = 0; i < QWATCH_MAX_CLIENTS; ++i)
			if (qwatch_clients[i].socket != INVALID_SOCKET && qwatch_clients[i].authenticated)
			{
				qwatch_clients[i].pending_aura = aura;
				qwatch_clients[i].pending_valid = true;
			}
	}
	for (i = 0; i < QWATCH_MAX_CLIENTS; ++i)
		QWatch_ProcessClient(&qwatch_clients[i], now);
}

void QWatch_Shutdown(void)
{
	QWatch_CloseListener();
}
