/*
	we use mdns as a way to share LAN IP addresses without leaking them. giving a randomised name that can only be looked up by someone already on the target lan
	mdns is basically just regular dns except to a multicast lan address.
	this means we need:
	a) ability to send multicast packets to some known address+port
	b) ability to listen for lookups.
*/

#include "ice_private.h"

#define MDNS_MAX_RETRIES 4

static struct mdns_peer_s
{
	double nextretry;
	int tries;	//stop sending after MDNS_MAX_RETRIES, forget a couple seconds after that.

	struct icestate_s *con;
	struct icecandinfo_s can;

	struct mdns_peer_s *next;
} *mdns_peers;
static SOCKET mdns_socket = INVALID_SOCKET;

struct dnshdr_s
{
	unsigned short tid, flags, questions, answerrr, authrr, addrr;
};
static qbyte *MDNS_ReadCName(qbyte *in, qbyte *end, char *out, char *outend)
{
	char *cname = out;
	while (*in && in < end)
	{
		if (cname != out)
			*cname++ = '.';
		if (cname+1+*in > outend)
			return end;	//if it overflows then its an error.
		memcpy(cname, in+1, *in);
		cname += *in;
		in += 1+*in;
	}
	*cname = 0;
	return ++in;
}
static void MDNS_ProcessPacket(qbyte *inmsg, size_t inmsgsize, netadr_t *source)
{
	struct dnshdr_s *rh = (struct dnshdr_s *)inmsg;
	unsigned short flags;
	qbyte *in, *end;
	struct {
		unsigned short idx, count;
		char name[256];
		unsigned short type;
		unsigned short class;
		unsigned int ttl;
		qbyte *data;
		unsigned short datasize;
	} a;
	struct mdns_peer_s *peer;

	end = inmsg + inmsgsize;

	//ignore packets from outside the lan...
	if (NET_ClassifyAddress(source, NULL) > ASCOPE_LAN)
		return;
	if (NET_AdrToPort(source) != 5353)
		return;	//don't answer/read anything unless its actually mdns. there's supposed to be legacy stuff, but browsers don't seem to respond to that either so lets just play safe.

	if (inmsgsize < sizeof(*rh))
		return;	//some kind of truncation...

	flags = ntohs(rh->flags);
	if (flags & 0x780f)
		return;	//opcode must be 0, response must be 0

	in = (qbyte*)(rh+1);

	if (rh->questions)
	{
		a.count = ntohs(rh->questions);
		for (a.idx = 0; a.idx < a.count; a.idx++)
		{
			struct icemodule_s *module;

			qbyte *questionstart = in;
			in = MDNS_ReadCName(in, end, a.name, a.name+sizeof(a.name));
			if (in+4 > end)
				return;	//error...
			a.type = *in++<<8;
			a.type |= *in++<<0;
			a.class = *in++<<8;
			a.class |= *in++<<0;

			module = ICE_FindMDNS(a.name);

			if (module && (a.type == 1/*A*/ || a.type == 28/*AAAA*/) && a.class == 1/*IN*/)
			{
				qbyte resp[512], *o = resp;
				int n,m, found=0, sz,ty;
				netadr_t	addr[16];
				int	network[sizeof(addr)/sizeof(addr[0])];
				unsigned int			flags[sizeof(addr)/sizeof(addr[0])];
				const char *params[sizeof(addr)/sizeof(addr[0])];
				struct sockaddr_storage dest;
				const unsigned int ttl = 120;

				m = ICE_EnumerateAddresses(module, network, flags, addr, params, sizeof(addr)/sizeof(addr[0]));
				*o++ = 0;*o++ = 0;	//tid - must be 0 for mdns responses.
				*o++=0x84;*o++= 0;	//flags
				*o++ = 0;*o++ = 0;	//questions
				*o++ = 0;*o++ = 0;	//answers
				*o++ = 0;*o++ = 0;	//auths
				*o++ = 0;*o++ = 0;	//additionals
				for (n = 0; n < m; n++)
				{
					if (NET_ClassifyAddress(&addr[n], NULL) == ASCOPE_LAN)
					{	//just copy a load of crap over
						qbyte *ab;
						if (addr[n].type == NA_IP)
							sz = 4, ty=1, ab = (qbyte*)&addr[n].in.sin_addr;/*A*/
						else if (addr[n].type == NA_IPV6)
							sz = 16, ty=28, ab = (qbyte*)&addr[n].in6.sin6_addr;/*AAAA*/
						else
							continue;	//nope.
						if (ty != a.type)
							continue;
						a.class |= 0x8000;

						if (o+(in-questionstart)+6+sz > resp+sizeof(resp))
							break;	//won't fit.

						memcpy(o, questionstart, in-questionstart-4);
						o += in-questionstart-4;
						*o++ = ty>>8; *o++ = ty;
						*o++ = a.class>>8; *o++ = a.class;
						*o++ = ttl>>24; *o++ = ttl>>16; *o++ = ttl>>8; *o++ = ttl>>0;
						*o++ = sz>>8; *o++ = sz;
						memcpy(o, ab, sz);
						o+=sz;

						found++;
					}
				}
				resp[6] = found>>8; resp[7] = found&0xff;	//replace the answer count now that we actually know

				if (!found)	//don't bother if we can't actually answer it.
					continue;

				//send a multicast response... (browsers don't seem to respond to unicasts).
				if (a.type & 0x8000)
				{	//they asked for a unicast response.
					resp[0] = inmsg[0]; resp[1] = inmsg[1];	//redo the tid.
					sz = NetadrToSockadr(source, &dest);
				}
				else
				{
					sz = sizeof(struct sockaddr_in);
					memset(&dest, 0, sz);
					((struct sockaddr_in*)&dest)->sin_family = AF_INET;
					((struct sockaddr_in*)&dest)->sin_port = htons(5353);
					((struct sockaddr_in*)&dest)->sin_addr.s_addr = inet_addr("224.0.0.251");	//or FF02::FB
				}
				sendto(mdns_socket, (void*)resp, o-resp, 0, (struct sockaddr*)&dest, sz);
//				if (net_ice_debug.ival)
//					Con_Printf(S_COLOR_GRAY"%s: Answering mdns (%s)\n", NET_AdrToString(resp, sizeof(resp), source), a.name);
			}
		}
	}

	a.count = ntohs(rh->answerrr);
	for (a.idx = 0; a.idx < a.count; a.idx++)
	{
		in = MDNS_ReadCName(in, end, a.name, a.name+sizeof(a.name));
		if (in+10 > end)
			return;	//error...
		a.type = *in++<<8;
		a.type |= *in++<<0;
		a.class = *in++<<8;
		a.class |= *in++<<0;
		a.ttl = *in++<<24;
		a.ttl |= *in++<<16;
		a.ttl |= *in++<<8;
		a.ttl |= *in++<<0;
		a.datasize = *in++<<8;
		a.datasize |= *in++<<0;
		a.data = in;
		in += a.datasize;
		if (in > end)
			return;

		for (peer = mdns_peers; peer; peer = peer->next)
		{
			if (!strcmp(a.name, peer->can.addr))
			{	//this is the record we were looking for. yay.
				if ((a.type&0x7fff) == 1/*A*/ && (a.class&0x7fff) == 1/*IN*/ && a.datasize == 4)
				{	//we got a proper ipv4 address. yay.
					q_snprintf(peer->can.addr, sizeof(peer->can.addr), "%i.%i.%i.%i", a.data[0], a.data[1], a.data[2], a.data[3]);
				}
				else if ((a.type&0x7fff) == 28/*AAAA*/ && (a.class&0x7fff) == 1/*IN*/ && a.datasize == 16)
				{	//we got a proper ipv4 address. yay.
					q_snprintf(peer->can.addr, sizeof(peer->can.addr), "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x",
							(a.data[ 0]<<8)|a.data[ 1],
							(a.data[ 2]<<8)|a.data[ 3],
							(a.data[ 4]<<8)|a.data[ 5],
							(a.data[ 6]<<8)|a.data[ 7],
							(a.data[ 8]<<8)|a.data[ 9],
							(a.data[10]<<8)|a.data[11],
							(a.data[12]<<8)|a.data[13],
							(a.data[14]<<8)|a.data[15]);
				}
				else
				{
					Con_Printf("Useless answer\n");
					break;
				}
//				if (net_ice_debug.ival)
//					Con_Printf(S_COLOR_GRAY"[%s]: Resolved %s to %s\n", ICE_GetConnName(peer->con), a.name, peer->can.addr);
				if (peer->tries != MDNS_MAX_RETRIES)
				{	//first response?...
					peer->tries = MDNS_MAX_RETRIES;
					peer->nextretry = Sys_DoubleTime()+0.5;
				}
				ICE_AddRCandidateInfo(peer->con, &peer->can);	//restore it, so we can handle alternatives properly.
				q_strlcpy(peer->can.addr, a.name, sizeof(peer->can.addr));
				break;
			}
		}
	}
}
static void MDNS_ReadPackets(void)
{
	qbyte inmsg[9000];
	int inmsgsize;
	netadr_t adr;
	struct sockaddr_storage source;

	for(;;)
	{
		socklen_t slen = sizeof(source);
		inmsgsize = recvfrom(mdns_socket, (void*)inmsg, sizeof(inmsg), 0, (struct sockaddr*)&source, &slen);
		if (inmsgsize <= 0)
			break;
		SockadrToNetadr(&source, slen, &adr);
		MDNS_ProcessPacket(inmsg, inmsgsize, &adr);
	}
}

void MDNS_Shutdown(void)
{
	if (mdns_socket == INVALID_SOCKET)
		return;
	closesocket(mdns_socket);
	mdns_socket = INVALID_SOCKET;
}

static void MDNS_GenChars(char *d, size_t len, qbyte *s)
{	//big endian hex, big endian data, can just do it by bytes.
	static char tohex[16] = "0123456789abcdef";
	for (; len--; s++)
	{
		*d++ = tohex[*s>>4];
		*d++ = tohex[*s&15];
	}
}
static void MDNS_Generate(char name[43])
{	//generate a suitable mdns name.
	unsigned char uuid[16];
	Sys_RandomBytes((qbyte*)uuid, sizeof(uuid));

	uuid[8]&=~(1<<6);	//clear clk_seq_hi_res bit 6
	uuid[8]|=(1<<7);	//set clk_seq_hi_res bit 7

	uuid[6] &= ~0xf0;	//clear time_hi_and_version's high 4 bits
	uuid[6] |= 0x40;	//replace with version

	MDNS_GenChars(name+0, 4, uuid+0);
	name[8] = '-';
	MDNS_GenChars(name+9, 2, uuid+4);
	name[13] = '-';
	MDNS_GenChars(name+14, 2, uuid+6);
	name[18] = '-';
	MDNS_GenChars(name+19, 2, uuid+8);
	name[23] = '-';
	MDNS_GenChars(name+24, 6, uuid+10);
	strcpy(name+36, ".local");
}

qboolean MDNS_Setup(struct icemodule_s *module)
{
	struct sockaddr_in adr;
	int _true = true;
//	int _false = false;
	struct ip_mreq mbrship;
	qboolean success = true;
	int i;

	//generate a name for the module if it didn't get one already.
	for (i = 0; ; i++)
	{
		if (i == countof(module->mdns_name))
		{
			MDNS_Generate(module->mdns_name);
			break;
		}
		if (module->mdns_name[i])
			break;	//already set.
	}

	if (mdns_socket != INVALID_SOCKET)
		return true;	//already got one

	memset(&adr, 0, sizeof(adr));
	adr.sin_family = AF_INET;
	adr.sin_port = htons(5353);
	adr.sin_addr.s_addr = INADDR_ANY;

	memset(&mbrship, 0, sizeof(mbrship));
	mbrship.imr_multiaddr.s_addr = inet_addr("224.0.0.251");	//or FF02::FB

	//browsers don't seem to let us take the easy route, so we can't just use our existing helpers.
	mdns_socket = socket (PF_INET, SOCK_CLOEXEC|SOCK_DGRAM, IPPROTO_UDP);
	if (mdns_socket == INVALID_SOCKET) success = false;
	if (!success || 0 > ioctlsocket(mdns_socket, FIONBIO, (void*)&_true)) success = false;
	//other processes on the same host may be listening for mdns packets to insert their own responses (eg two browsers), so we need to ensure that other processes can use the same port. there's no real security here for us (that comes from stun's user/pass stuff).
	if (!success || 0 > setsockopt (mdns_socket, SOL_SOCKET, SO_REUSEADDR, (const void*)&_true, sizeof(_true))) success = false;
#ifdef SO_REUSEPORT	//not on windows.
	if (!success || 0 > setsockopt (mdns_socket, SOL_SOCKET, SO_REUSEPORT, (const void*)&_true, sizeof(_true))) success = false;
#endif
#if IP_MULTICAST_LOOP	//ideally we'd prefer to not receive our own requests, but this is host-level, not socket-level, so unusable for communication with browsers on the same machine
//	if (success)		setsockopt (mdns_socket, IPPROTO_IP, IP_MULTICAST_LOOP, &_false, sizeof(_false));
#endif
	if (!success || 0 > setsockopt (mdns_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const void*)&mbrship, sizeof(mbrship))) success = false;

	adr.sin_addr.s_addr = INADDR_ANY;
	if (!success || bind (mdns_socket, (void *)&adr, sizeof(adr)) < 0)
		success = false;
	if (!success)
	{
		MDNS_Shutdown();
		Con_Printf("mdns setup failed\n");
	}
	return success;
}

static void MDNS_SendQuery(struct mdns_peer_s *peer)
{
	char *n = peer->can.addr, *dot;
	struct sockaddr_in dest;
	qbyte outmsg[1024], *o = outmsg;

	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_port = htons(5353);
	dest.sin_addr.s_addr = inet_addr("224.0.0.251");	//or FF02::FB

	*o++ = 0;*o++ = 0;	//tid
	*o++ = 0;*o++ = 0;	//flags
	*o++ = 0;*o++ = 1;	//questions
	*o++ = 0;*o++ = 0;	//answers
	*o++ = 0;*o++ = 0;	//auths
	*o++ = 0;*o++ = 0;	//additionals

	//mdns is strictly utf-8. no punycode needed.
	for(;;)
	{
		dot = strchr(n, '.');
		if (!dot)
			dot = n + strlen(n);
		if (dot == n)
			return; //err... can't write a 0-length label.
		*o++ = dot-n;
		memcpy(o, n, dot-n); o += dot-n; n += dot-n;
		if (!*n)
			break;
		n++;
	}
	*o++ = 0;

	*o++ = 0; *o++ = 1; //type: 'A' record
	*o++ = 0; *o++ = 1; //class: 'IN'

	sendto(mdns_socket, (void*)outmsg, o-outmsg, 0, (struct sockaddr*)&dest, sizeof(dest));
	peer->tries++;
	peer->nextretry = Sys_DoubleTime() + (50/1000.0);
}
void MDNS_SendQueries(void)
{
	double time;
	struct mdns_peer_s *peer, **link;
	if (mdns_socket == INVALID_SOCKET)
		return;
	MDNS_ReadPackets();
	if (!mdns_peers)
		return;
	time = Sys_DoubleTime();

	for (link = &mdns_peers; (peer=*link); )
	{
		if (peer->nextretry < time)
		{
			if (peer->tries == MDNS_MAX_RETRIES)
			{	//bye bye.
				*link = peer->next;
				free(peer);
				continue;
			}

			MDNS_SendQuery(peer);

			if (peer->tries == MDNS_MAX_RETRIES)
				peer->nextretry = Sys_DoubleTime() + 2.0;
			break;	//don't spam multiple each frame.
		}
		link = &peer->next;
	}
}

static qboolean MDNS_CharsAreHex(char *s, size_t len)
{
	for (; len--; s++)
	{
		if (*s >= '0' && *s <= '9')
			;
		else if (*s >= 'a' && *s <= 'f')
			;
		else
			return false;
	}
	return true;
}
qboolean MDNS_AddQuery(struct icemodule_s *module, struct icestate_s *con, struct icecandinfo_s *can)
{
	struct mdns_peer_s *peer;

	//check if its an mDNS name - must be a UUID, with a .local on the end.
	if (!(
		MDNS_CharsAreHex(can->addr, 8) && can->addr[8]=='-' &&
		MDNS_CharsAreHex(can->addr+9, 4) && can->addr[13]=='-' &&
		MDNS_CharsAreHex(can->addr+14, 4) && can->addr[18]=='-' &&
		MDNS_CharsAreHex(can->addr+19, 4) && can->addr[23]=='-' &&
		MDNS_CharsAreHex(can->addr+24, 12) && !strcmp(&can->addr[36], ".local")))
	{
		return false;
	}

	if (!MDNS_Setup(module))
		return false;
	peer = malloc(sizeof(*peer));
	peer->con = con;
	peer->can = *can;
	peer->next = mdns_peers;
	peer->tries = 0;
	peer->nextretry = Sys_DoubleTime();
	mdns_peers = peer;
	MDNS_SendQuery(peer);
	return true;
}
void MDNS_RemoveQueries(struct icestate_s *con)
{
	struct mdns_peer_s **link, *q;

	for (link = &mdns_peers; (q = (*link)); )
	{
		if (q->con == con)
		{
			*link = q->next;
			free(q);
		}
		else
			link = &q->next;
	}
}
