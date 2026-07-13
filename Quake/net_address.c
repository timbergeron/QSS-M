/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2010-2026 QuakeSpasm contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License.
*/

#include "quakedef.h"
#include "q_ctype.h"
#include "arch_def.h"
#include "net_sys.h"
#include "net_address.h"

static qboolean NET_ParsePortText(const char *text, int *port)
{
	const char *scan;
	long value = 0;

	if (!text || !*text || !port)
		return false;

	for (scan = text; *scan; scan++)
	{
		if (!q_isdigit((unsigned char)*scan))
			return false;
		value = value * 10 + (*scan - '0');
		if (value > 65535)
			return false;
	}

	if (value <= 0)
		return false;
	*port = (int)value;
	return true;
}

static qboolean NET_NormalizeIPv4(const char *host, char *output, size_t output_size)
{
	const char *scan = host;
	int octets[4];
	int i;

	if (!host || !*host || !output || output_size == 0)
		return false;

	for (i = 0; i < 4; i++)
	{
		int digits = 0;
		int value = 0;

		if (!q_isdigit((unsigned char)*scan))
			return false;
		while (q_isdigit((unsigned char)*scan))
		{
			value = value * 10 + (*scan++ - '0');
			if (++digits > 3 || value > 255)
				return false;
		}
		octets[i] = value;
		if (i < 3)
		{
			if (*scan++ != '.')
				return false;
		}
		else if (*scan)
			return false;
	}

	q_snprintf(output, output_size, "%d.%d.%d.%d",
		octets[0], octets[1], octets[2], octets[3]);
	return true;
}

static qboolean NET_NormalizeIPv6(const char *host, char *output, size_t output_size)
{
	if (!host || !*host || !strchr(host, ':') || !output || output_size == 0)
		return false;

#ifndef AI_NUMERICHOST
	/* Never fall back to name service while classifying a display address. */
	return false;
#else
	struct addrinfo hints;
	struct addrinfo *result = NULL;
	int error;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_NUMERICHOST;

	error = getaddrinfo(host, NULL, &hints, &result);
	if (error != 0 || !result)
	{
		if (result)
			freeaddrinfo(result);
		return false;
	}

	error = getnameinfo(result->ai_addr, (socklen_t)result->ai_addrlen,
		output, (socklen_t)output_size, NULL, 0, NI_NUMERICHOST);
	freeaddrinfo(result);
	return error == 0;
#endif
}

static qboolean NET_NormalizeDomain(const char *host, char *output, size_t output_size)
{
	size_t len;
	size_t label_len = 0;
	const char *label_start;
	qboolean label_has_alpha = false;
	qboolean has_alpha = false;
	size_t i;

	if (!host || !*host || !output || output_size == 0)
		return false;

	len = strlen(host);
	if (host[len - 1] == '.')
	{
		if (len == 1)
			return false;
		len--;
	}
	if (len == 0 || len > 253 || len + 1 > output_size)
		return false;

	label_start = host;
	for (i = 0; i <= len; i++)
	{
		const char c = (i == len) ? '\0' : host[i];

		if (i == len || c == '.')
		{
			if (label_len == 0 || label_len > 63)
				return false;
			if (*label_start == '-' || host[i - 1] == '-')
				return false;
			if (label_has_alpha)
				has_alpha = true;
			label_start = host + i + 1;
			label_len = 0;
			label_has_alpha = false;
			continue;
		}

		if (!q_isalnum((unsigned char)c) && c != '-')
			return false;
		if (q_isalpha((unsigned char)c))
			label_has_alpha = true;
		label_len++;
	}

	if (!has_alpha)
		return false;

	for (i = 0; i < len; i++)
		output[i] = (char)q_tolower((unsigned char)host[i]);
	output[len] = '\0';
	return true;
}

qboolean NET_ParseEndpoint(const char *text, int default_port, net_endpoint_t *endpoint)
{
	char input[MAX_SERVER_ADDRESS_LEN];
	char rawhost[MAX_SERVER_ADDRESS_LEN];
	const char *host_start;
	const char *host_end;
	const char *port_text = NULL;
	const char *scan;
	size_t host_len;
	int colon_count = 0;
	const char *last_colon = NULL;

	if (!endpoint)
		return false;
	memset(endpoint, 0, sizeof(*endpoint));
	endpoint->kind = NET_HOST_INVALID;

	if (!text || !*text || default_port <= 0 || default_port > 65535)
		return false;
	if (strlen(text) >= sizeof(input))
		return false;

	q_strlcpy(input, text, sizeof(input));
	host_start = input;
	while (*host_start == ' ' || *host_start == '\t')
		host_start++;
	host_end = host_start + strlen(host_start);
	while (host_end > host_start && (host_end[-1] == ' ' || host_end[-1] == '\t'))
		host_end--;
	if (host_end == host_start)
		return false;
	*((char *)host_end) = '\0';

	endpoint->port = default_port;
	if (*host_start == '[')
	{
		const char *close = strchr(host_start + 1, ']');
		if (!close || close == host_start + 1)
			return false;
		if (close[1])
		{
			if (close[1] != ':' || !close[2])
				return false;
			port_text = close + 2;
			endpoint->port_explicit = true;
		}
		host_start++;
		host_end = close;
	}
	else
	{
		for (scan = host_start; scan < host_end; scan++)
		{
			if (*scan == ':')
			{
				colon_count++;
				last_colon = scan;
			}
		}
		if (colon_count == 1)
		{
			if (!last_colon[1])
				return false;
			port_text = last_colon + 1;
			host_end = last_colon;
			endpoint->port_explicit = true;
		}
	}

	host_len = (size_t)(host_end - host_start);
	if (host_len == 0 || host_len >= sizeof(rawhost))
		return false;
	memcpy(rawhost, host_start, host_len);
	rawhost[host_len] = '\0';

	if (port_text && !NET_ParsePortText(port_text, &endpoint->port))
		return false;

	if (NET_NormalizeIPv4(rawhost, endpoint->host, sizeof(endpoint->host)))
		endpoint->kind = NET_HOST_IPV4;
	else if (NET_NormalizeIPv6(rawhost, endpoint->host, sizeof(endpoint->host)))
		endpoint->kind = NET_HOST_IPV6;
	else if (NET_NormalizeDomain(rawhost, endpoint->host, sizeof(endpoint->host)))
		endpoint->kind = NET_HOST_DNS;
	else
		return false;

	return true;
}

qboolean NET_FormatEndpoint(const net_endpoint_t *endpoint, qboolean include_port,
	char *output, size_t output_size)
{
	int result;

	if (!output || output_size == 0)
		return false;
	output[0] = '\0';
	if (!endpoint || endpoint->kind == NET_HOST_INVALID || !endpoint->host[0])
		return false;

	if (!include_port)
		result = q_snprintf(output, output_size, "%s", endpoint->host);
	else if (endpoint->kind == NET_HOST_IPV6)
		result = q_snprintf(output, output_size, "[%s]:%d", endpoint->host, endpoint->port);
	else
		result = q_snprintf(output, output_size, "%s:%d", endpoint->host, endpoint->port);

	if (result < 0 || (size_t)result >= output_size)
	{
		output[0] = '\0';
		return false;
	}
	return true;
}

qboolean NET_EndpointEqual(const net_endpoint_t *first, const net_endpoint_t *second)
{
	if (!first || !second || first->kind == NET_HOST_INVALID || second->kind == NET_HOST_INVALID)
		return false;
	if (first->port != second->port || first->kind != second->kind)
		return false;
	return q_strcasecmp(first->host, second->host) == 0;
}

qboolean NET_ValidIP(const char *address)
{
	net_endpoint_t endpoint;
	return NET_ParseEndpoint(address, 26000, &endpoint) &&
		strlen(endpoint.host) < NET_NAMELEN &&
		(endpoint.kind == NET_HOST_IPV4 || endpoint.kind == NET_HOST_IPV6);
}

qboolean NET_ValidDomain(const char *address)
{
	net_endpoint_t endpoint;
	return NET_ParseEndpoint(address, 26000, &endpoint) &&
		strlen(endpoint.host) < NET_NAMELEN && endpoint.kind == NET_HOST_DNS &&
		strchr(endpoint.host, '.') != NULL;
}

qboolean Valid_IP(const char *address)
{
	return NET_ValidIP(address);
}

qboolean Valid_Domain(const char *address)
{
	return NET_ValidDomain(address);
}

#ifndef NDEBUG
void NET_Address_RunSelfTests(void)
{
	net_endpoint_t a, b;
	char text[MAX_SERVER_ADDRESS_LEN];

	SDL_assert(NET_ParseEndpoint("192.168.001.010", 26000, &a));
	SDL_assert(a.kind == NET_HOST_IPV4 && !strcmp(a.host, "192.168.1.10"));
	SDL_assert(a.port == 26000 && !a.port_explicit);
	SDL_assert(NET_ParseEndpoint("192.168.1.10:27500", 26000, &a));
	SDL_assert(a.port == 27500 && a.port_explicit);
	SDL_assert(!NET_ParseEndpoint("192.168.1.10:0", 26000, &a));
	SDL_assert(!NET_ParseEndpoint("192.168.1.10:65536", 26000, &a));

	SDL_assert(NET_ParseEndpoint("Quake.Example.COM.", 26000, &a));
	SDL_assert(a.kind == NET_HOST_DNS && !strcmp(a.host, "quake.example.com"));
	SDL_assert(NET_ParseEndpoint("quake.example.com:27500", 26000, &b));
	SDL_assert(b.port_explicit && b.port == 27500);
	SDL_assert(NET_ParseEndpoint("localhost", 26000, &a));
	SDL_assert(a.kind == NET_HOST_DNS && !Valid_Domain("localhost"));
	SDL_assert(!NET_ParseEndpoint("-bad.example", 26000, &a));

	SDL_assert(NET_ParseEndpoint("[::1]:26000", 27500, &a));
	SDL_assert(a.kind == NET_HOST_IPV6 && a.port == 26000 && a.port_explicit);
	SDL_assert(NET_ParseEndpoint("0:0:0:0:0:0:0:1", 26000, &b));
	SDL_assert(NET_EndpointEqual(&a, &b));
	SDL_assert(NET_FormatEndpoint(&a, true, text, sizeof(text)));
	SDL_assert(text[0] == '[' && strstr(text, "]:26000") != NULL);
	SDL_assert(!NET_ParseEndpoint("[::1", 26000, &a));
	SDL_assert(!NET_ParseEndpoint("[::1]:", 26000, &a));
}
#endif
