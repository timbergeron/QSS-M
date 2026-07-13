/*
 * Shared parsing and formatting for Quake server endpoints.
 * These helpers are pure: they never resolve DNS or mutate net_hostport.
 */

#ifndef QUAKE_NET_ADDRESS_H
#define QUAKE_NET_ADDRESS_H

#include <stddef.h>

#define MAX_SERVER_ADDRESS_LEN 256

typedef enum net_host_kind_e
{
	NET_HOST_INVALID,
	NET_HOST_IPV4,
	NET_HOST_IPV6,
	NET_HOST_DNS
} net_host_kind_t;

typedef struct net_endpoint_s
{
	char host[MAX_SERVER_ADDRESS_LEN];
	int port;
	qboolean port_explicit;
	net_host_kind_t kind;
} net_endpoint_t;

qboolean NET_ParseEndpoint(const char *text, int default_port, net_endpoint_t *endpoint);
qboolean NET_FormatEndpoint(const net_endpoint_t *endpoint, qboolean include_port,
	char *output, size_t output_size);
qboolean NET_EndpointEqual(const net_endpoint_t *first, const net_endpoint_t *second);
qboolean NET_ValidIP(const char *address);
qboolean NET_ValidDomain(const char *address);

/* Existing public spellings retained for current callers. */
qboolean Valid_IP(const char *address);
qboolean Valid_Domain(const char *address);

#ifndef NDEBUG
void NET_Address_RunSelfTests(void);
#endif

#endif /* QUAKE_NET_ADDRESS_H */
