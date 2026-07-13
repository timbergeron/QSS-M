/* Last-known hostname mappings learned from successful client connections. */

#ifndef QUAKE_NET_HOSTNAME_CACHE_H
#define QUAKE_NET_HOSTNAME_CACHE_H

#include <stddef.h>

struct qsocket_s;

void NET_HostnameCache_Init(void);
void NET_HostnameCache_Shutdown(void);
void NET_HostnameCache_RecordSuccessfulConnection(const char *requested_address,
	const struct qsocket_s *socket);
/* Return true only when a numeric address was replaced by an unambiguous fresh hostname. */
qboolean NET_HostnameCache_FormatDisplay(const char *raw_address, int default_port,
	char *output, size_t output_size);
/* As above, but retain the raw address in parentheses; falls back to raw on truncation. */
qboolean NET_HostnameCache_FormatDetailedDisplay(const char *raw_address, int default_port,
	char *output, size_t output_size);
/* Exact endpoint comparison plus fresh, learned hostname-to-numeric matching. */
qboolean NET_HostnameCache_AddressesMatch(const char *first, const char *second,
	int default_port);
/* Changes when display results may have changed, for cached menu filters. */
unsigned int NET_HostnameCache_Generation(void);

#endif /* QUAKE_NET_HOSTNAME_CACHE_H */
