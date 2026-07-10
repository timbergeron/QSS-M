#ifndef QSSM_NET_WS_H
#define QSSM_NET_WS_H

/* Returns the URI scheme length (5 for ws://, 6 for wss://), or zero. */
static inline size_t NET_WebSocketSchemeLength(const char *address, qboolean *secure)
{
	if (secure)
		*secure = false;
	if (!address)
		return 0;
	if (!q_strncasecmp(address, "wss://", 6))
	{
		if (secure)
			*secure = true;
		return 6;
	}
	if (!q_strncasecmp(address, "ws://", 5))
		return 5;
	return 0;
}

#endif
