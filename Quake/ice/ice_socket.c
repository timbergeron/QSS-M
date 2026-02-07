#include "ice_private.h"


static enum addressscope_e NET_ClassifyAddressipv4(int ip, const char **outdesc)
{
	int scope = ASCOPE_NET;
	const char *desc = NULL;
	if ((ip&BigLong(0xffff0000)) == BigLong(0xA9FE0000))	//169.254.x.x/16
		scope = ASCOPE_LINK, desc = ("link-local");
	else if ((ip&BigLong(0xff000000)) == BigLong(0x0a000000))	//10.x.x.x/8
		scope = ASCOPE_LAN, desc = ("private");
	else if ((ip&BigLong(0xff000000)) == BigLong(0x7f000000))	//127.x.x.x/8
		scope = ASCOPE_HOST, desc = "localhost";
	else if ((ip&BigLong(0xfff00000)) == BigLong(0xac100000))	//172.16.x.x/12
		scope = ASCOPE_LAN, desc = ("private");
	else if ((ip&BigLong(0xffff0000)) == BigLong(0xc0a80000))	//192.168.x.x/16
		scope = ASCOPE_LAN, desc = ("private");
	else if ((ip&BigLong(0xffc00000)) == BigLong(0x64400000))	//100.64.x.x/10
		scope = ASCOPE_LAN, desc = ("CGNAT");
	else if (ip == BigLong(0x00000000))	//0.0.0.0/32
		scope = ASCOPE_HOST, desc = "any";

	*outdesc = desc;
	return scope;
}
enum addressscope_e NET_ClassifyAddress(const netadr_t *adr, const char **outdesc)
{
	int scope = ASCOPE_NET;
	const char *desc = NULL;

	if (adr->sa.sa_family == AF_INET6)
	{
		const qbyte *ip6 = (const qbyte *)&adr->in6.sin6_addr;
		if ((*(const int*)ip6&BigLong(0xffc00000)) == BigLong(0xfe800000))	//fe80::/10
			scope = ASCOPE_LINK, desc = ("link-local");
		else if ((*(const int*)ip6&BigLong(0xfe000000)) == BigLong(0xfc00000))	//fc::/7
			scope = ASCOPE_LAN, desc = ("ULA/private");
		else if (*(const int*)ip6 == BigLong(0x20010000)) //2001::/32
			scope = ASCOPE_NET, desc = "toredo";
		else if ((*(const int*)ip6&BigLong(0xffff0000)) == BigLong(0x20020000)) //2002::/16
			scope = ASCOPE_NET, desc = "6to4";
		else if (memcmp(ip6, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\1", 16) == 0)	//::1
			scope = ASCOPE_HOST, desc = "localhost";
		else if (memcmp(ip6, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) == 0)	//::
			scope = ASCOPE_HOST, desc = "any";
		else if (memcmp(ip6, "\0\0\0\0\0\0\0\0\0\0\xff\xff", 12) == 0)	//::ffff:x.y.z.w
		{
			scope = NET_ClassifyAddressipv4(*(const int*)(ip6+12), &desc);
			if (!desc)
				desc = ("v4-mapped");
		}
	}
	else if (adr->sa.sa_family == AF_INET)
		scope = NET_ClassifyAddressipv4(*(const int*)&adr->in.sin_addr, &desc);
	if (outdesc)
		*outdesc = desc;
	return scope;
}

// ParsePartialIP: check string to see if it is a partial IP address and
// return bits to mask and set netadr_t or 0 if not an address
int ParsePartialIP(const char *s, netadr_t *a)
{
	char *colon;
	int bits;

	if (!*s)
		return 0;

	memset (a, 0, sizeof(*a));

	//if its ::ffff:a.b.c.d then parse it as ipv4 by just skipping the prefix.
	//we ought to leave it as ipv6, but any printing will show it as ipv4 anyway.
	if (!q_strncasecmp(s, "::ffff:", 7) && strchr(s+7, '.') && !strchr(s+7, ':'))
		s += 7;

	//multiple colons == ipv6
	//single colon = ipv4:port
	colon = strchr(s, ':');
	if (colon && strchr(colon+1, ':'))
	{
		qbyte *address = (qbyte *)&a->in6.sin6_addr;
		unsigned long tmp;
		int gapstart = -1;	//in bytes...
		bits = 0;

		while(*s)
		{
			tmp = strtoul(s, &colon, 16);
			if (colon == s)
			{
				if (bits)
					return 0;
			}
			else
			{
				if (tmp > 0xffff)
					return 0;	//invalid
				*address++ = (tmp>>8)&0xff;
				*address++ = (tmp>>0)&0xff;
				bits += 16;
			}

			if (bits == 128)
			{
				if (!*colon)
					break;
				return 0;	//must have ended here
			}


			//double-colon is a gap (or partial end).
			//hopefully the last 64 bits or whatever will be irrelevant anyway, so such addresses won't be common
			if (colon[0] == ':' && colon[1] == ':')
			{
				if (gapstart >= 0)
					return 0; //only one gap...
				if (!colon[2])
					break;	//nothing after. its partial.
				gapstart = bits/8;
				colon+=2;
			}
			else if (*colon == ':' && bits)
				colon++;
			else if (*colon)
				return 0; //gibberish here...
			else
				break;	//end of address... anything more is a partial.
			s = colon;
		}
		if (gapstart >= 0)
		{
			int tailsize = (bits/8)-gapstart;	//bits to move to the end
			int gapsize = 16 - gapstart - tailsize;
			memmove((qbyte *)&a->in6.sin6_addr+gapstart+gapsize, (qbyte *)&a->in6.sin6_addr+gapstart, tailsize);	//move the bits we found to the end
			memset((qbyte *)&a->in6.sin6_addr+gapstart, 0, gapsize);	//and make sure the gap is cleared
			bits = 128;	//found it all, or something.
		}
		if (!bits)
			bits = 1;	//FIXME: return of 0 is an error, but :: is 0-length... lie.
		a->in6.sin6_family = AF_INET6;
		a->type = NA_IPV6;
	}
	else
	{
		char *address = (char *)&a->in.sin_addr;
		int port = 0;
		bits = 8;
		while (*s)
		{
			if (*s == ':')
			{
				port = strtoul(s+1, &address, 10);
				if (*address)	//if there was something other than a number there, give up now
					return 0;
				break;	//end-of-string
			}
			else if (*s == '.')
			{
				if (bits >= 32) // only 32 bits in ipv4
					return 0;
				else if (*(s+1) == '.')
					return 0;
				else if (*(s+1) == '\0')
					break; // don't add more bits to the mask for x.x., etc
				address++;

				//many nq servers mask addresses with Xs.
				if (s[1] == 'x' || s[1] == 'X')
				{
					s++;
					while (*s == 'x' || *s == 'X' || *s == '.')
						s++;
					if (*s)
						return 0;
					break;
				}
				bits += 8;
			}
			else if (*s >= '0' && *s <= '9')
				*address = ((*address)*10) + (*s-'0');
			else
				return 0; // invalid character

			s++;
		}
		a->in.sin_family = AF_INET;
		a->type = NA_IP;
		a->in.sin_port = htons(port);
	}

	return bits;
}
qboolean NET_CompareAdr (const netadr_t *a, const netadr_t *b)
{
	if (a->sa.sa_family == AF_INET)
	{
		if (b->sa.sa_family == AF_INET6)
		{	//::ffff:x.y.z.w can match an ip4 address.
			if (a->in.sin_port == b->in6.sin6_port)
				if (!memcmp(&b->in6.sin6_addr, "\x00\x00\x00\x00" "\x00\x00\x00\x00" "\x00\x00\xff\xff", 12))
					if (!memcmp((qbyte*)&b->in6.sin6_addr + 12, &a->in.sin_addr, 4))
						return true;

			return false;	//FIXME
		}
		if (a->in.sin_family == b->in.sin_family &&
			a->in.sin_port == b->in.sin_port &&
			!memcmp(&a->in.sin_addr, &b->in.sin_addr, sizeof(a->in.sin_addr))
			)
			return true;	//seems to match as far as we can tell.
	}
	else if (a->sa.sa_family == AF_INET6)
	{
		if (b->sa.sa_family == AF_INET)
			return NET_CompareAdr(b,a);	//flip it around.

		if (a->in6.sin6_family == b->in6.sin6_family &&
			a->in6.sin6_port == b->in6.sin6_port &&
			!memcmp(&a->in6.sin6_addr, &b->in6.sin6_addr, sizeof(a->in6.sin6_addr))
			)
			return true;	//seems to match as far as we can tell.
	}
	else
		return false;

	return false;
}

size_t NET_StringToAdr(const char *name, int defaultport, netadr_t *addr, size_t maxaddr)
{
	size_t r = 0;
	struct addrinfo *addrinfo = NULL;
	struct addrinfo *pos;
	struct addrinfo udp6hint;
	int error;
	char *port;
	char dupbase[256];
	size_t len;

	memset(&udp6hint, 0, sizeof(udp6hint));
	udp6hint.ai_family = 0;//Any... we check for AF_INET6 or 4
	udp6hint.ai_socktype = SOCK_DGRAM;
	udp6hint.ai_protocol = IPPROTO_UDP;

	if (*name == '[')
	{
		port = strstr(name, "]");
		if (!port)
			error = EAI_NONAME;
		else
		{
			len = port - (name+1);
			if (len >= sizeof(dupbase))
				len = sizeof(dupbase)-1;
			strncpy(dupbase, name+1, len);
			dupbase[len] = '\0';
			error = getaddrinfo(dupbase, (port[1] == ':')?port+2:NULL, &udp6hint, &addrinfo);
		}
	}
	else
	{
		port = strrchr(name, ':');

		if (port)
		{
			len = port - name;
			if (len >= sizeof(dupbase))
				len = sizeof(dupbase)-1;
			strncpy(dupbase, name, len);
			dupbase[len] = '\0';
			error = getaddrinfo(dupbase, port+1, &udp6hint, &addrinfo);
		}
		else
			error = EAI_NONAME;
		if (error)	//failed, try string with no port.
			error = getaddrinfo(name, NULL, &udp6hint, &addrinfo);	//remember, this func will return any address family that could be using the udp protocol... (ip4 or ip6)
	}

	if (!error)
	{
		for (pos = addrinfo; pos; pos = pos->ai_next)
		{
			if (pos->ai_family == AF_INET && r < maxaddr)
			{
				memset(&addr[r], 0, sizeof(addr[r]));
				addr[r].type = NA_IP;
				memcpy(&addr[r].in, pos->ai_addr, pos->ai_addrlen);
				if (!addr[r].in.sin_port)
					addr[r].in.sin_port = htons(defaultport);
				r++;
			}
			if (pos->ai_family == AF_INET6 && r < maxaddr)
			{
				memset(&addr[r], 0, sizeof(addr[r]));
				addr[r].type = NA_IPV6;
				memcpy(&addr[r].in6, pos->ai_addr, pos->ai_addrlen);
				if (!addr[r].in6.sin6_port)
					addr[r].in6.sin6_port = htons(defaultport);
				r++;
			}
		}
		freeaddrinfo (addrinfo);
	}
	return r;
}

int NET_AdrToPort (const netadr_t *a)
{
	if (a->sa.sa_family == AF_INET)
		return ntohs(a->in.sin_port);
	else if (a->sa.sa_family == AF_INET6)
		return ntohs(a->in6.sin6_port);
	return 0;
}
char *NET_AdrToString (char *s, int len, const netadr_t *a)
{
	if (a->sa.sa_family == AF_INET)
	{
		const qbyte *ab = (const qbyte*)&a->in.sin_addr;
		q_snprintf(s, len, "%i.%i.%i.%i:%i", ab[0],ab[1],ab[2],ab[3], ntohs(a->in.sin_port));
	}
	else if (a->sa.sa_family == AF_INET6)
	{
		const unsigned short *ab = (const unsigned short*)&a->in6.sin6_addr;
		q_snprintf(s, len, "[%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x]:%i", ntohs(ab[0]),ntohs(ab[1]),ntohs(ab[2]),ntohs(ab[3]), ntohs(ab[4]),ntohs(ab[5]),ntohs(ab[6]),ntohs(ab[7]), ntohs(a->in6.sin6_port));
	}
	else
		*s = 0;
	return s;
}
char *NET_BaseAdrToString (char *s, int len, const netadr_t *a)
{
	if (a->sa.sa_family == AF_INET)
	{
		const qbyte *ab = (const qbyte*)&a->in.sin_addr;
		q_snprintf(s, len, "%i.%i.%i.%i", ab[0],ab[1],ab[2],ab[3]);
	}
	else if (a->sa.sa_family == AF_INET6)
	{
		const unsigned short *ab = (const unsigned short*)&a->in6.sin6_addr;
		q_snprintf(s, len, "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x", ntohs(ab[0]),ntohs(ab[1]),ntohs(ab[2]),ntohs(ab[3]), ntohs(ab[4]),ntohs(ab[5]),ntohs(ab[6]),ntohs(ab[7]));
	}
	else
		*s = 0;
	return s;
}
void SockadrToNetadr (const struct sockaddr_storage *s, int sizeofsockaddr, netadr_t *a)
{
	a->prot = NP_DGRAM;
	a->connum = 0;
	if (s->ss_family == AF_INET)
	{
		a->in = *(const struct sockaddr_in*)s;
		a->type = NA_IP;
	}
	else if (s->ss_family == AF_INET6)
	{
		a->in6 = *(const struct sockaddr_in6*)s;
		a->type = NA_IPV6;
	}
	else
		a->type = NA_INVALID;
}
int NetadrToSockadr (const netadr_t *a, struct sockaddr_storage *s)
{
	*s = a->ss;

	//return the size.
	if (s->ss_family == AF_INET)
		return sizeof(struct sockaddr_in);
	else if (s->ss_family == AF_INET6)
		return sizeof(struct sockaddr_in6);
	return 0;
}




static neterr_t ICEUDP_SendPacket (struct icesocket_s *s, const netadr_t *addr, const void *data, size_t datasize)
{	int r;
	int sasz;

	if (s->af != addr->sa.sa_family)
		return NETERR_NOROUTE;	//nope, unreachable from this socket.

	//many systems don't like oversized addresses (only a few families are actually variably sized).
	if (addr->sa.sa_family == AF_INET)
		sasz = sizeof(addr->in);
	else if (addr->sa.sa_family == AF_INET6)
		sasz = sizeof(addr->in6);
	else
		return NETERR_NOROUTE;	//unsupported af...

	r = sendto(s->sock, data, datasize, 0, &addr->sa, sasz);
	if (r < 0)
	{
		switch (NET_ERRNO())
		{
		case NET_EINTR:	//shouldn't be blocking in the first place.
		case NET_EINPROGRESS:	//but we're not connecting...
		case NET_EWOULDBLOCK:	//can't send yet. network getting flooded.
		//case NET_EAGAIN:		//sometimes reported instead of wouldblock
			return NETERR_CLOGGED;
		case NET_ENETUNREACH:	//routing issues. may come back later, probably needs user interaction but stuff will likely time out first.
		case NET_EADDRNOTAVAIL:	//bad ip
		case NET_EAFNOSUPPORT:
			return NETERR_NOROUTE;
		//case NET_EMSGSIZE:	//system was tracking MTU errors from icmp responses.
		//	return NETERR_MTU;

		case NET_ECONNREFUSED:	//for tcp
		case NET_ENOTCONN:		//for tcp
		case NET_ECONNABORTED:	//for tcp
		case NET_ECONNRESET:	//for tcp
		case NET_ETIMEDOUT:		//for tcp
			return NETERR_DISCONNECTED; //won't work, let the caller know its dead.
		case NET_EACCES:		//firewall?
			return NETERR_NOROUTE; //a different destination might still work.
		default:
			//don't know... windows has a history of adding new warning codes randomly... hopefully it'll clear up.
			return NETERR_CLOGGED;
		}
	}
	return NETERR_SENT;
}
static int ICEUDP_RecvPacket (struct icesocket_s *s, netadr_t *addr, void *data, size_t datasize)
{
	socklen_t sasz = sizeof(addr->ss);
	int r = recvfrom(s->sock, data, datasize, 0, &addr->sa, &sasz);

	if (r <= 0)
	{
		addr->type = NA_INVALID;
		addr->sa.sa_family = AF_UNSPEC;
	}
	else
		SockadrToNetadr(&addr->ss, sasz, addr);
	return r;
}
static void ICEUDP_Close(struct icesocket_s *s)
{
	closesocket(s->sock);
	free(s);
}






#if defined(_WIN32)
#define MAX_ADR_SIZE 64
static int ICE_GetLocalAddress(int port, int wantfam, netadr_t *addresses, int maxaddresses)
{
	//in win32, we can look up our own hostname to retrieve a list of local interface addresses.
	char		adrs[MAX_ADR_SIZE];
	int found = 0;

	gethostname(adrs, sizeof(adrs));
#if 0//ndef getaddrinfo
	if (!getaddrinfo)
	{
		struct hostent *h = gethostbyname(adrs);
		int b = 0;
#ifdef HAVE_IPV4
		if(h && h->h_addrtype == AF_INET)
		{
			for (b = 0; h->h_addr_list[b] && maxaddresses; b++)
			{
				struct sockaddr_in from;
				from.sin_family = AF_INET;
				from.sin_port = port;
				memcpy(&from.sin_addr, h->h_addr_list[b], sizeof(from.sin_addr));
				SockadrToNetadr((struct sockaddr_storage*)&from, sizeof(from), addresses);

				if (addresses->sa.sa_family == AF_INET)
					addresses->in.sin_port = port;
//				else if (addresses->sa.sa_family == AF_INET6)
//					addresses->in6.sin6_port = port;
				else
					continue;

				addresses++;
				maxaddresses--;
				found++;
			}
		}
#endif
#ifdef HAVE_IPV6
		if(h && h->h_addrtype == AF_INET6)
		{
			for (b = 0; h->h_addr_list[b] && maxaddresses; b++)
			{
				struct sockaddr_in6 from;
				from.sin6_family = AF_INET6;
				from.sin6_port = port;
				from.sin6_scope_id = 0;
				memcpy(&from.sin6_addr, h->h_addr_list[b], sizeof(((struct sockaddr_in6*)&from)->sin6_addr));
				SockadrToNetadr((struct sockaddr_storage*)&from, sizeof(from), addresses);

				if (addresses->sa.sa_family == AF_INET)
					addresses->in.sin_port = port;
				else
				 if (addresses->sa.sa_family == AF_INET6)
					addresses->in6.sin6_port = port;
				else
					continue;

				addresses++;
				maxaddresses--;
				found++;
			}
		}
#endif
	}
	else
#endif
	{
		struct addrinfo hints, *result, *itr;
		memset(&hints, 0, sizeof(struct addrinfo));
		hints.ai_family = 0;    /* Allow IPv4 or IPv6 */
		hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
		hints.ai_flags = 0;
		hints.ai_protocol = 0;          /* Any protocol */

		if (getaddrinfo(adrs, NULL, &hints, &result) == 0)
		{
			for (itr = result; itr; itr = itr->ai_next)
			{
				if (itr->ai_addr->sa_family == wantfam)
				if (maxaddresses)
				{
					SockadrToNetadr((struct sockaddr_storage*)itr->ai_addr, sizeof(struct sockaddr_storage), addresses);
					if (addresses->sa.sa_family == AF_INET)
						addresses->in.sin_port = port;
					else if (addresses->sa.sa_family == AF_INET6)
						addresses->in6.sin6_port = port;
					else
						continue;

					addresses++;
					maxaddresses--;
					found++;
				}
			}
			freeaddrinfo(result);
		}
	}
	return found;
}

#elif defined(__linux__) && !defined(ANDROID)
//in linux, looking up our own hostname to retrieve a list of local interface addresses will give no indication that other systems are able to do the same thing and is thus not supported.
//there's some special api instead
//glibc 2.3.
//also available with certain bsds, I'm but unsure which preprocessor we can use.
#include <ifaddrs.h>

static struct ifaddrs *iflist;
static unsigned int iftime;	//requery sometimes.
static int ICE_GetLocalAddress(int port, int wantfam, netadr_t *addresses, int maxaddresses)
{
	struct ifaddrs *ifa;
	int idx = 0;
	unsigned int time = Sys_Milliseconds();

	if (time - iftime > 1000 && iflist)
	{
		freeifaddrs(iflist);
		iflist = NULL;
	}
	if (!iflist)
	{
		iftime = time;
		getifaddrs(&iflist);
	}

	for (ifa = iflist; ifa && idx < maxaddresses; ifa = ifa->ifa_next)
	{
		//can happen if the interface is not bound.
		if (ifa->ifa_addr == NULL)
			continue;

		//filter out families that we're not interested in.
		if (ifa->ifa_addr->sa_family == wantfam)
		{
			SockadrToNetadr((struct sockaddr_storage*)ifa->ifa_addr, sizeof(struct sockaddr_storage), &addresses[idx]);
			if (addresses[idx].sa.sa_family == AF_INET)
				addresses[idx].in.sin_port = port;
			else if (addresses[idx].sa.sa_family == AF_INET6)
				addresses[idx].in6.sin6_port = port;
			else
				continue;
			idx++;
		}
	}
	return idx;
}
#else
static int ICE_GetLocalAddress(int port, int wantfam, netadr_t *addresses, int maxaddresses)
{
	return 0;
}
#endif


static int ICEUDP_GetAddresses(struct icesocket_s *s, netadr_t *addresses, size_t maxaddresses)
{
	struct sockaddr_storage	from;
	socklen_t fromsize = sizeof(from);
	netadr_t adr;
	int found = 0;

	if (getsockname (s->sock, (struct sockaddr*)&from, &fromsize) != -1)
	{
		memset(&adr, 0, sizeof(adr));
		SockadrToNetadr(&from, fromsize, &adr);

		//if its bound to 'any' address, ask the system what addresses it actually accepts.
		if      (adr.sa.sa_family == AF_INET && adr.in.sin_addr.s_addr == INADDR_ANY)
			found = ICE_GetLocalAddress(adr.in.sin_port,   AF_INET, addresses, maxaddresses);
		else if (adr.sa.sa_family == AF_INET6 && !memcmp(&adr.in6.sin6_addr, &in6addr_any, sizeof(in6addr_any)))
			found = ICE_GetLocalAddress(adr.in6.sin6_port, AF_INET6, addresses, maxaddresses);

		//and use the bound address (even if its 0.0.0.0) if we didn't grab a list from the system.
		if (!found)
		{
			/*if (maxaddresses && adr.type == NA_IPV6 &&
				!*(int*)&adr.address.ip6[0] &&
				!*(int*)&adr.address.ip6[4] &&
				!*(int*)&adr.address.ip6[8] &&
				!*(int*)&adr.address.ip6[12])
			{
				*addresses = adr;
				addresses->type = NA_IP;
				addresses++;
				maxaddresses--;
				found++;
			}*/

			if (maxaddresses)
			{
				*addresses = adr;
				addresses++;
				maxaddresses--;
				found++;
			}
		}
	}

	return found;
}

struct icesocket_s *ICE_OpenUDP(netadrtype_t type, int port)
{
	netadr_t adr;
	int sasz = 0;

	memset(&adr, 0, sizeof(adr));
	if (type == NA_IP)
	{
		adr.in.sin_family = AF_INET;
		adr.in.sin_port = htons(port);
		sasz = sizeof(adr.in);
	}
	else if (type == NA_IPV6)
	{
		adr.in6.sin6_family = AF_INET6;
		adr.in6.sin6_port = htons(port);
		sasz = sizeof(adr.in6);
	}
	else	//unsupported type.
		return NULL;

	if (sasz)
	{
		struct icesocket_s *n = calloc(1, sizeof(*n));
		n->SendPacket = ICEUDP_SendPacket;
		n->RecvPacket = ICEUDP_RecvPacket;
		n->EnumerateAddresses = ICEUDP_GetAddresses;
		n->CloseSocket = ICEUDP_Close;
		n->af = adr.sa.sa_family;
		n->sock = socket(n->af, SOCK_CLOEXEC|SOCK_DGRAM, IPPROTO_UDP);
		if (n->sock != INVALID_SOCKET)
		{
#ifdef _WIN32
			u_long _true = 1;	//windows specifies an actual type. which comes out as an int32 anyway, but with extra warnings.
#else
			int _true = 1;	//linux seems to expect an int? doesn't matter too much if its treated as a char on little-endian systems.
#endif
			if (type == NA_IPV6)
				setsockopt(n->sock, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&_true, sizeof(_true));	//if it fails then its probably ipv6 only anyway, so don't worry about failure.
			if (ioctlsocket (n->sock, FIONBIO, &_true) >= 0)
				if (!bind(n->sock, &adr.sa, sasz))
				{	//socket is okay. use it.
					return n;
				}
			closesocket(n->sock);
		}
		free(n);
	}
	return NULL;
}


static neterr_t ICETCP_SendPacket (struct icesocket_s *s, const netadr_t *addr, const void *data, size_t datasize)
{
	return NETERR_NOROUTE;	//nope, unreachable from this socket.
}
struct icesocket_s *ICE_WS_ServerConnection(SOCKET sock, netadr_t *adr);
static void ICETCP_CheckAccept (struct icesocket_s *s, struct icemodule_s *module, void(*GotConnection)(struct icemodule_s *module, struct icesocket_s *link, netadr_t *adr))
{
	netadr_t adr;
	socklen_t adrlen = sizeof(adr.ss);
	SOCKET fd = accept(s->sock, &adr.sa, &adrlen);
	if (fd >= 0)
	{	//if we got a new client, create a new 'ice' state using that link.
		struct icesocket_s *link;
#ifdef _WIN32
		u_long _true = 1;	//windows specifies an actual type. which comes out as an int32 anyway, but with extra warnings.
#else
		int _true = 1;	//linux seems to expect an int? doesn't matter too much if its treated as a char on little-endian systems.
#endif

		ioctlsocket (fd, FIONBIO, &_true);
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&_true, sizeof(_true));	//keep lag down
		SockadrToNetadr(&adr.ss, adrlen, &adr);
		link = ICE_WS_ServerConnection(fd, &adr);
		GotConnection(module, link, &adr);
	}
}
static struct icesocket_s *ICE_OpenTCPSocket(netadrtype_t type, int port)
{
	netadr_t adr;
	int sasz = 0;

	memset(&adr, 0, sizeof(adr));
	if (type == NA_IP)
	{
		adr.in.sin_family = AF_INET;
		adr.in.sin_port = htons(port);
		sasz = sizeof(adr.in);
	}
	else if (type == NA_IPV6)
	{
		adr.in6.sin6_family = AF_INET6;
		adr.in6.sin6_port = htons(port);
		sasz = sizeof(adr.in6);
	}
	else	//unsupported type.
		return NULL;

	if (sasz)
	{
		struct icesocket_s *n = malloc(sizeof(*n));
		n->SendPacket = ICETCP_SendPacket;
		n->CheckAccept = ICETCP_CheckAccept;
		n->EnumerateAddresses = ICEUDP_GetAddresses;
		n->CloseSocket = ICEUDP_Close;
		n->af = adr.sa.sa_family;
		n->sock = socket(n->af, SOCK_CLOEXEC|SOCK_STREAM, IPPROTO_TCP);
		if (n->sock != INVALID_SOCKET)
		{
#ifdef _WIN32
			u_long _true = 1;	//windows specifies an actual type. which comes out as an int32 anyway, but with extra warnings.
#else
			int _true = 1;	//linux seems to expect an int? doesn't matter too much if its treated as a char on little-endian systems.
#endif
			if (type == NA_IPV6)
				setsockopt(n->sock, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&_true, sizeof(_true));	//if it fails then its probably ipv6 only anyway, so don't worry about failure.
			if (ioctlsocket (n->sock, FIONBIO, &_true) >= 0)
				if (!bind(n->sock, &adr.sa, sasz))
				{	//socket is okay. use it.
					if (!listen(n->sock, 1))	//we don't expect heavy traffic
						return n;
				}
			closesocket(n->sock);
		}
		free(n);
	}
	return NULL;
}


void ICE_SetupModule(struct icemodule_s *module, int port)
{	//fixme: we should be binding one socket on each interface instead of INADDR_ANY. otherwise we're depending on the OS routing to be correct when multihomed. this may cause issues for linklocal addresses.
	if (!module->conn[0])
		module->conn[0] = ICE_OpenUDP(NA_IP, port);
	if (!module->conn[0] && port)
		module->conn[0] = ICE_OpenUDP(NA_IP, 0);	//try again, but with ephemeral.

	if (!module->conn[1])
		module->conn[1] = ICE_OpenUDP(NA_IPV6, port);
	if (!module->conn[1] && port)
		module->conn[1] = ICE_OpenUDP(NA_IPV6, 0);	//try again, but with ephemeral

	//open some tcp sockets too, in case people want to use websockets.
	if (!module->conn[2] && port)
		module->conn[2] = ICE_OpenTCPSocket(NA_IP, port);
	if (!module->conn[3] && port)
		module->conn[3] = ICE_OpenTCPSocket(NA_IPV6, port);
}
