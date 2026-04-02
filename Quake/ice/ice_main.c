/*license gplv2+ */
/*
Interactive Connectivity Establishment (rfc 5245)
find out your peer's potential ports.
spam your peer with stun packets.
see what sticks.
the 'controller' assigns some final candidate pair to ensure that both peers send+receive from a single connection.
if no candidates are available, try using stun to find public nat addresses.

in this code, a 'pair' is actually in terms of each local socket and remote address. hopefully that won't cause too much weirdness.
	this does limit which interfaces we can send packets from, which may cause issues with local TURN relays(does not result in extra prflx candidates) and VPNs(may need to be set up as the default route), and prevents us from being able to report reladdr in candidate offers (again mostly only of use with TURN)
	lan connections should resolve to a host interface anyway

stun test packets must contain all sorts of info. username+integrity+fingerprint for validation. priority+usecandidate+icecontrol(ing) to decree the priority of any new remote candidates, whether its finished, and just who decides whether its finished.
peers don't like it when those are missing.

host candidates - addresses that are directly known (but are probably unroutable private things)
server reflexive candidates - addresses that we found from some public stun server (useful for NATs that use a single public port for each unique private port)
peer reflexive candidates - addresses that our peer finds out about as we spam them
relayed candidates - some sort of socks5 or something proxy.


Note: Even after the ICE connection becomes active, you should continue to collect local candidates and transmit them to the peer out of band.
this allows the connection to pick a new route if some router dies (like a relay kicking us).
FIXME: the client currently disconnects from the broker. the server tracks players via ip rather than ICE.

tcp rtp framing should generally be done with a 16-bit network-endian length prefix followed by the data.

NOTE: we do NOT distinguish between media-level and session-level attributes, as such we can only handle ONE media stream per session. we also don't support rtcp.
*/
/*
webrtc
basically just sctp-over-dtls-over-ice or srtp(negotiated via dtls)-over-ice.
the sctp part is pure bloat+pain for us, but as its required for browser compat we have to support it anyway - but we only use it where we must.
we don't do any srtp stuff at all.
*/
/*
multiplexing...
  application: 191 < leadbyte < 256
  rtp: 127 < leadbyte < 192 (if you're using it, otherwise free...)
  turnchan: 64 < leadbyte < 79 (excluded by sender, so can be reused by application, we don't use it cos we're too lazy to track)
  dtls: 19 < leadbyte < 64
  stun: leadbyte < 4
So, |=0x80 to avoid ambiguity with lower layers, so long as any rtp is in-band. helps to use big-endian.
*/

#include "ice_private.h"

#ifdef SUPPORT_ICE

typedef struct
{
	uint16_t msgtype;
	uint16_t msglen;
	union
	{
		uint32_t magiccookie;
		uint16_t portxor;
	};
	uint32_t transactid[3];
} stunhdr_t;
//class
#define STUN_REQUEST	0x0000
#define STUN_REPLY		0x0100
#define STUN_ERROR		0x0110
#define STUN_INDICATION	0x0010
//request
#define STUN_BINDING	0x0001
#define STUN_ALLOCATE	0x0003 //TURN
#define STUN_REFRESH	0x0004 //TURN
#define STUN_SEND		0x0006 //TURN
#define STUN_DATA		0x0007 //TURN
#define STUN_CREATEPERM	0x0008 //TURN
#define STUN_CHANBIND	0x0009 //TURN

//misc stuff...
#define STUN_MAGIC_COOKIE	0x2112a442

//attributes
#define STUNATTR_MAPPED_ADDRESS			0x0001
#define STUNATTR_USERNAME				0x0006
#define STUNATTR_MSGINTEGRITIY_SHA1		0x0008
#define STUNATTR_ERROR_CODE				0x0009
//#define STUNATTR_CHANNELNUMBER			0x000c	//TURN
#define STUNATTR_LIFETIME				0x000d	//TURN
#define STUNATTR_XOR_PEER_ADDRESS		0x0012	//TURN
#define STUNATTR_DATA					0x0013	//TURN
#define STUNATTR_REALM					0x0014	//TURN
#define STUNATTR_NONCE					0x0015	//TURN
#define STUNATTR_XOR_RELAYED_ADDRESS	0x0016	//TURN
#define STUNATTR_REQUESTED_ADDRFAM		0x0017	//TURN
//#define STUNATTR_EVEN_PORT			0x0018	//TURN
#define STUNATTR_REQUESTED_TRANSPORT	0x0019	//TURN
#define STUNATTR_DONT_FRAGMENT			0x001a	//TURN
#define STUNATTR_MSGINTEGRITIY_SHA2_256	0x001C
#define STUNATTR_PASSWORD_ALGORITHM		0x001D	//yay, screw md5
#define	STUNATTR_XOR_MAPPED_ADDRESS		0x0020
#define	STUNATTR_ICE_PRIORITY			0x0024	//ICE
#define	STUNATTR_ICE_USE_CANDIDATE		0x0025	//ICE

//0x8000 attributes are optional, and may be silently ignored without issue.
#define STUNATTR_ADDITIONAL_ADDRFAM		0x8000	//TURN -- listen for ipv6 in addition to ipv4
#define STUNATTR_SOFTWARE				0x8022	//TURN
#define STUNATTR_FINGERPRINT			0x8028
#define	STUNATTR_ICE_CONTROLLED			0x8029	//ICE
#define STUNATTR_ICE_CONTROLLING		0x802A	//ICE

typedef struct
{
	uint16_t attrtype;
	uint16_t attrlen;
} stunattr_t;
#include "zlib.h"	//we need crc32.

#if 0	//enable this for testing only. will generate excessive error messages with non-hacked turn servers...
	#define ASCOPE_TURN_REQUIRESCOPE	ASCOPE_HOST //try sending it any address.
#else
	#define ASCOPE_TURN_REQUIRESCOPE	ASCOPE_LAN	//don't report loopback/link-local addresses to turn relays.
#endif


struct icecandidate_s
{
	struct icecandinfo_s info;

	struct icecandidate_s *next;

	netadr_t peer;
	//peer needs telling or something.
	qboolean dirty;
	qboolean ismdns;		//indicates that the candidate is a .local domain and thus can be shared without leaking private info.

	//these are bitmasks. one bit for each local socket.
	unsigned int reachable;	//looked like it was up a while ago...
	unsigned int reached;	//timestamp of when it was last reachable. pick a new one if too old.
	unsigned int tried;		//don't try again this round
	unsigned int noroute;	//permanently unreachable from these networks (NETERR_NOROUTE)
};
struct icestate_s
{
	struct icestate_s *next;
	struct icemodule_s *module;

	netadr_t chosenpeer;	//address we're sending our data to.

	struct iceserver_s
	{	//stun/turn servers
		netadr_t addr;
		qboolean isstun;
		unsigned int stunretry;	//once a second, extended to once a minite on reply
		unsigned int stunrnd[3];

		//turn stuff.
		struct icesocket_s *con;	//TURN needs unique client addresses otherwise it gets confused and starts reporting errors about client ids. make sure these connections are unique.
									//FIXME: should be able to get away with only one if the addr field is unique... move to icestate_s
		int connum;

		netadrtype_t family;	//ipv4 or ipv6. can't send to other peers.
		char *user, *auth;
		enum {
			TURN_UNINITED,		//never tried to poke server, need to send a quicky allocate request.
			TURN_HAVE_NONCE,	//nonce should be valid, need to send a full allocate request.
			TURN_ALLOCATED,		//we have an allocation that we need to refresh every now and then.
			TURN_TERMINATING,		//we have an allocation that we need to refresh every now and then.
		} state;
		unsigned int expires;	//allocation expiry time.
		char *nonce, *realm;	//gathered from the server
//		netadr_t relay, srflx;	//our relayed port, and our local address for the sake of it.

		unsigned int peers;
		struct
		{
			unsigned int expires;		//once a second, extended to once a minite on reply
			unsigned int retry;			//gotta keep retrying
			unsigned int stunrnd[3];	//so we know when to stop retrying.
			struct icecandidate_s *rc;	//if its not rc then we need to reauth now.
		} peer[32];
	} server[8];
	unsigned int servers;

	qboolean brokerless;	//we lost connection to our broker... clean up on failure status.
	unsigned int icetimeout;	//time when we consider the connection dead
	unsigned int icetimeout_ms;	//timeout duration in ms (0 = default 30s)
	unsigned int keepalive;	//sent periodically...
	unsigned int retries;	//bumped after each round of connectivity checks. affects future intervals.
	enum iceproto_e proto;
	unsigned int modeflags;	//permissions of what we're allowed to do.
	qboolean initiator;		//sends the initial sdpoffer.
	qboolean controlled;	//controller chooses final ports.
	enum icestate_e state;
	char *conname;		//internal id.
	char *friendlyname;	//who you're talking to.

	unsigned int originid;	//should be randomish
	unsigned int originversion;//bumped each time something in the sdp changes.
	char originaddress[16];

	struct icecandidate_s *lc;
	char *lpwd;
	char *lufrag;

	struct icecandidate_s *rc;
	char *rpwd;
	char *rufrag;

	unsigned int tiehigh;
	unsigned int tielow;
	int foundation;

	qboolean blockcandidates;		//don't send candidates yet. FIXME: replace with gathering.
#ifdef HAVE_SCTP
	struct sctp_s *sctp;
	uint16_t mysctpport;
	uint16_t peersctpport;
	qboolean sctpoptional;
	qboolean peersctpoptional;
#endif
#ifdef HAVE_DTLS
	void *dtlsstate;

	const dtlsfuncs_t *dtlsfuncs;
	qboolean dtlspassive;	//true=server, false=client (separate from ice controller and whether we're hosting. yay...)
	dtlscred_t cred;	//credentials info for dtls (both peer and local info)
#endif

	struct icecodecslot_s
	{
		//FIXME: we should probably include decode state in here somehow so multiple connections don't clobber each other.
		int id;
		char *name;
	} codecslot[34];		//96-127. don't really need to care about other ones.

	void *userptr;
};

#ifdef HAVE_DTLS
static const struct
{
	const char *name;
	hashfunc_t *hash;
} webrtc_hashes[] =
{	//RFC8211 specifies this list of hashes
//	{"md2",	&hash_md2},	//deprecated, hopefully we won't see it
//	{"md5",	&hash_md5},	//deprecated, hopefully we won't see it
	{"sha-1",	&hash_sha1},
	{"sha-224",	&hash_sha2_224},
	{"sha-256",	&hash_sha2_256},
	{"sha-384",	&hash_sha2_384},
	{"sha-512",	&hash_sha2_512},
};
#endif
static neterr_t ICE_Transmit(void *cbctx, const qbyte *data, size_t datasize);
static neterr_t TURN_Encapsulate(struct icestate_s *ice, netadr_t *to, const qbyte *data, size_t datasize);
static void TURN_AuthorisePeer(struct icestate_s *con, struct iceserver_s *srv, int peer);
static qboolean ICE_ProcessPacket (struct icemodule_s *module, struct icesocket_s *net_from_connection, netadr_t *from, void *msg_data, size_t msg_size);
static struct icestate_s *icelist;	//fixme: move to modules.
static qboolean icedestroyed; //thread unsafe.
static void ICE_AddLCandidateInfo(struct icestate_s *con, netadr_t *adr, int type);
static unsigned int ICE_ComputePriority(netadr_t *adr, struct icecandinfo_s *info);

static const char *ICE_GetCandidateType(struct icecandinfo_s *info)
{
	switch(info->type)
	{
	case ICE_HOST:	return "host";
	case ICE_SRFLX:	return "srflx";
	case ICE_PRFLX:	return "prflx";
	case ICE_RELAY:	return "relay";
	}
	return "?";
}


static struct icecodecslot_s *ICE_GetCodecSlot(struct icestate_s *ice, int slot)
{
	if (slot >= 96 && slot < 96+32)
		return &ice->codecslot[slot-96];
	else if (slot == 0)
		return &ice->codecslot[32];
	else if (slot == 8)
		return &ice->codecslot[33];
	return NULL;
}

neterr_t ICE_SendUDPPacket(struct icemodule_s *module, netadr_t *addr, const void *data, size_t datasize)
{
	neterr_t e;
	int i = 0;
	struct icesocket_s *s;
	if (addr->connum && addr->connum <= countof(module->conn))
	{
		s = module->conn[addr->connum-1];
		if (s)
			return s->SendPacket(s, addr, data, datasize);
	}
	else for (i = 0; i < countof(module->conn); i++)
	{	//send it from the first that'll accept it...
		s = module->conn[i];
		if (s)
		{
			e = s->SendPacket(s, addr, data, datasize);
			if (e == NETERR_NOROUTE)
				continue;	//try the next.
			return e;
		}
	}
	return NETERR_NOROUTE;
}

int ICE_EnumerateAddresses(struct icemodule_s *module, int *out_networks, unsigned int *out_flags, netadr_t *out_addr, const char **out_params, size_t maxresults)
{
	int r = 0, nr;
	int i;
	struct icesocket_s *s;
	for (i = 0; i < countof(module->conn); i++)
	{
		s = module->conn[i];
		if (!s)
			continue;	//not opened.
		nr = s->EnumerateAddresses(s, out_addr+r, maxresults-r);
		while(nr --> 0)
		{
			out_addr[r].connum = i+1;
			out_networks[r] = i+1;
			out_flags[r] = 0;
			out_params[r] = NULL;
			r++;
		}
	}
	return r;
}

void ICE_WriteData(icebuf_t *buf, const void *data, size_t sz)
{
	if (buf->cursize + sz > buf->maxsize)
		sz = buf->maxsize-buf->cursize;	//silently truncate it... ouch?...
	memcpy(buf->data + buf->cursize, data, sz);
	buf->cursize += sz;
}
void ICE_WriteChar(icebuf_t *buf, int8_t i)
{
	if (buf->cursize + sizeof(i) > buf->maxsize)
		return;
	buf->data[buf->cursize++] = (i    ) & 0xff;
}
void ICE_WriteByte(icebuf_t *buf, uint8_t i)
{
	if (buf->cursize + sizeof(i) > buf->maxsize)
		return;
	buf->data[buf->cursize++] = (i    ) & 0xff;
}
void ICE_WriteShort(icebuf_t *buf, uint16_t i)
{	//spits out little-endian...
	if (buf->cursize + sizeof(i) > buf->maxsize)
		return;
	buf->data[buf->cursize++] = (i    ) & 0xff;
	buf->data[buf->cursize++] = (i>> 8) & 0xff;
}
void ICE_WriteLong(icebuf_t *buf, uint32_t i)
{	//spits out little-endian...
	if (buf->cursize + sizeof(i) > buf->maxsize)
		return;
	buf->data[buf->cursize++] = (i    ) & 0xff;
	buf->data[buf->cursize++] = (i>> 8) & 0xff;
	buf->data[buf->cursize++] = (i>>16) & 0xff;
	buf->data[buf->cursize++] = (i>>24) & 0xff;
}

#if defined(HAVE_CLIENT) && defined(VOICECHAT)
extern cvar_t snd_voip_send;
struct rtpheader_s
{
	unsigned char v2_p1_x1_cc4;
	unsigned char m1_pt7;
	unsigned short seq;
	unsigned int timestamp;
	unsigned int ssrc;
	unsigned int csrc[1];	//sized according to cc
};
void S_Voip_RTP_Parse(unsigned short sequence, const char *codec, const unsigned char *data, unsigned int datalen);
qboolean S_Voip_RTP_CodecOkay(const char *codec);
static qboolean NET_RTP_Parse(netadr_t *from, void *msg_data, size_t msg_size)
{
	struct rtpheader_s *rtpheader = (void*)net_message.data;
	if (msg_size >= sizeof(*rtpheader) && (rtpheader->v2_p1_x1_cc4 & 0xc0) == 0x80)
	{
		int hlen;
		int padding = 0;
		struct icestate_s *con;
		//make sure this really came from an accepted rtp stream
		//note that an rtp connection equal to the game connection will likely mess up when sequences start to get big
		//(especially problematic in sane clients that start with a random sequence)
		for (con = icelist; con; con = con->next)
		{
			if (con->state != ICE_INACTIVE && (con->proto == ICEP_VIDEO || con->proto == ICEP_VOICE) && NET_CompareAdr(from, &con->chosenpeer))
			{
				struct icecodecslot_s *codec = ICE_GetCodecSlot(con, rtpheader->m1_pt7 & 0x7f);
				if (codec)	//untracked slot
				{
					char *codecname = codec->name;
					if (!codecname)	//inactive slot
						continue;

					if (rtpheader->v2_p1_x1_cc4 & 0x20)
						padding = net_message.data[msg_size-1];
					hlen = sizeof(*rtpheader);
					hlen += ((rtpheader->v2_p1_x1_cc4 & 0xf)-1) * sizeof(int);
					if (con->proto == ICEP_VOICE)
						S_Voip_RTP_Parse((unsigned short)BigShort(rtpheader->seq), codecname, hlen+(char*)(rtpheader), msg_size - padding - hlen);
//					if (con->proto == ICEP_VIDEO)
//						S_Video_RTP_Parse((unsigned short)BigShort(rtpheader->seq), codecname, hlen+(char*)(rtpheader), msg_size - padding - hlen);
					return true;
				}
			}
		}
	}
	return false;
}
qboolean NET_RTP_Active(void)
{
	struct icestate_s *con;
	for (con = icelist; con; con = con->next)
	{
		if (con->state == ICE_CONNECTED && con->proto == ICEP_VOICE)
			return true;
	}
	return false;
}
qboolean NET_RTP_Transmit(unsigned int sequence, unsigned int timestamp, const char *codec, char *cdata, int clength)
{
	icebuf_t buf;
	char pdata[512];
	int i;
	struct icestate_s *con;
	qboolean built = false;

	memset(&buf, 0, sizeof(buf));
	buf.maxsize = sizeof(pdata);
	buf.cursize = 0;
	buf.allowoverflow = true;
	buf.data = pdata;

	for (con = icelist; con; con = con->next)
	{
		if (con->state == ICE_CONNECTED && con->proto == ICEP_VOICE)
		{
			for (i = 0; i < countof(con->codecslot); i++)
			{
				if (con->codecslot[i].name && !strcmp(con->codecslot[i].name, codec))
				{
					if (!built)
					{
						built = true;
						ICE_WriteByte(&buf, (2u<<6) | (0u<<5) | (0u<<4) | (0<<0));	//v2_p1_x1_cc4
						ICE_WriteByte(&buf, (0u<<7) | (con->codecslot[i].id<<0));	//m1_pt7
						ICE_WriteShort(&buf, BigShort(sequence&0xffff));	//seq
						ICE_WriteLong(&buf, BigLong(timestamp));	//timestamp
						ICE_WriteLong(&buf, BigLong(0));			//ssrc
						ICE_WriteData(&buf, cdata, clength);
						if (buf.overflowed)
							return built;
					}
					ICE_Transmit(con, buf.data, buf.cursize);
					break;
				}
			}
		}
	}
	return built;
}
#endif


struct icemodule_s *ICE_FindMDNS(const char *mdnsname)
{	//find the ice state that generated the given name. we reuse it for each ice state within a given module. its just that we have a list of ice states rather than modules.
	struct icestate_s *con;
	for (con = icelist; con; con = con->next)
	{
		if (!strcmp(con->module->mdns_name, mdnsname))
			return con->module;
	}
	return NULL;
}
static struct icestate_s *ICE_Find(struct icemodule_s *module, const char *conname)
{
	struct icestate_s *con;

	for (con = icelist; con; con = con->next)
	{
		if (con->module == module && !strcmp(con->conname, conname))
			return con;
	}
	return NULL;
}
#ifdef HAVE_TURN
static qboolean TURN_AddXorAddressAttrib(icebuf_t *buf, unsigned int attr, netadr_t *to)
{	//12 or 24 bytes.
	int alen, atype, i, port;
	qbyte *addr;
	if (to->type == NA_IP)
	{
		alen = 4;
		atype = 1;
		addr = (qbyte*)&to->in.sin_addr;
		port = to->in.sin_port;
	}
	else if (to->type == NA_IPV6 &&
				!*(int*)&to->in6.sin6_addr.s6_addr[0] &&
				!*(int*)&to->in6.sin6_addr.s6_addr[4] &&
				!*(short*)&to->in6.sin6_addr.s6_addr[8] &&
				*(short*)&to->in6.sin6_addr.s6_addr[10] == (short)0xffff)
	{	//just because we use an ipv6 address for ipv4 internally doesn't mean we should tell the peer that they're on ipv6...
		alen = 4;
		atype = 1;
		addr = (qbyte*)&to->in6.sin6_addr;
		addr += sizeof(to->in6.sin6_addr.s6_addr) - sizeof(to->in.sin_addr);
		port = to->in6.sin6_port;
	}
	else if (to->type == NA_IPV6)
	{
		alen = 16;
		atype = 2;
		addr = (unsigned char*)&to->in6.sin6_addr;
		port = to->in6.sin6_port;
	}
	else
		return false;

	ICE_WriteShort(buf, BigShort(attr));
	ICE_WriteShort(buf, BigShort(4+alen));
	ICE_WriteShort(buf, BigShort(atype));
	ICE_WriteShort(buf, port ^ *(short*)(buf->data+4));
	for (i = 0; i < alen; i++)
		ICE_WriteByte(buf, addr[i] ^ (buf->data+4)[i]);
	return true;
}
static qboolean TURN_AddAuth(icebuf_t *buf, struct iceserver_s *srv)
{	//adds auth info to a stun packet
	unsigned short len;
	qbyte integrity[DIGEST_MAXSIZE];
	hashfunc_t *hash = &hash_sha1;
	hashfunc_t *pwdhash = &hash_md5;

	if (!srv->user || !srv->nonce || !srv->realm)
		return false;
	ICE_WriteShort(buf, BigShort(STUNATTR_USERNAME));
	len = strlen(srv->user);
	ICE_WriteShort(buf, BigShort(len));
	ICE_WriteData (buf, srv->user, len);
	if (len&3)
		ICE_WriteData (buf, "\0\0\0\0", 4-(len&3));

	ICE_WriteShort(buf, BigShort(STUNATTR_REALM));
	len = strlen(srv->realm);
	ICE_WriteShort(buf, BigShort(len));
	ICE_WriteData (buf, srv->realm, len);
	if (len&3)
		ICE_WriteData (buf, "\0\0\0\0", 4-(len&3));

	ICE_WriteShort(buf, BigShort(STUNATTR_NONCE));
	len = strlen(srv->nonce);
	ICE_WriteShort(buf, BigShort(len));
	ICE_WriteData (buf, srv->nonce, len);
	if (len&3)
		ICE_WriteData (buf, "\0\0\0\0", 4-(len&3));

	if (pwdhash != &hash_md5)
	{
		ICE_WriteShort(buf, BigShort(STUNATTR_PASSWORD_ALGORITHM));
		len = strlen(srv->nonce);
		ICE_WriteShort(buf, 4);
		if (pwdhash == &hash_md5)
			ICE_WriteShort(buf, 1);
		else if (pwdhash == &hash_sha2_256)
			ICE_WriteShort(buf, 2);
		else
			return false;	//not defined... panic.
		ICE_WriteShort(buf, 0);	//paramlength
		//no params.
	}

	//message integrity is a bit annoying
	buf->data[2] = ((buf->cursize+4+hash->digestsize-20)>>8)&0xff;	//hashed header length is up to the end of the hmac attribute
	buf->data[3] = ((buf->cursize+4+hash->digestsize-20)>>0)&0xff;
	//but the hash is to the start of the attribute's header
	{	//long-term credentials do stuff weird.
		const char *tmpkey = va("%s:%s:%s", srv->user, srv->realm, srv->auth);
		len = CalcHash(pwdhash, integrity,sizeof(integrity), (const qbyte*)tmpkey, strlen(tmpkey));
	}
	len = CalcHMAC(hash, integrity, sizeof(integrity), buf->data, buf->cursize, integrity,len);
	if (hash == &hash_sha2_256)
		ICE_WriteShort(buf, BigShort(STUNATTR_MSGINTEGRITIY_SHA2_256));
	else if (hash == &hash_sha1)
		ICE_WriteShort(buf, BigShort(STUNATTR_MSGINTEGRITIY_SHA1));
	else
		return false;	//not defined!
	ICE_WriteShort(buf, BigShort(len));	//integrity length
	ICE_WriteData(buf, integrity, len);	//integrity data
	return true;
}
#endif

static const char *ICE_NetworkToName(struct icestate_s *ice, int network)
{
	network -= 1;
	if (network >= 0 && network < MAX_NETWORKS)
	{	//return the cvar name
		/*ftenet_connections_t *col = ICE_PickConnection(ice);
		if (col && col->conn[network])
			return col->conn[network]->name;*/
	}
#ifdef HAVE_TURN
	else if (network >= MAX_NETWORKS)
	{
		network -= MAX_NETWORKS;
		if (network >= countof(ice->server))
		{	//a peer-reflexive address from poking a TURN server...
			network -= countof(ice->server);
			if (network < ice->servers)
				return "turn-reflexive";
		}
		else
			return va("turn:%s", ice->server[network].realm);
	}
#endif

	return "<UNKNOWN>";
}
static neterr_t TURN_Encapsulate(struct icestate_s *ice, netadr_t *to, const qbyte *data, size_t datasize)
{
	icebuf_t buf;
	unsigned int network = to->connum-1;
	struct iceserver_s *srv;
	if (to->type == NA_INVALID)
		return NETERR_NOROUTE;
#ifdef HAVE_TURN
	if (to->connum && network >= MAX_NETWORKS)
	{	//fancy turn-related gubbins
		network -= MAX_NETWORKS;
		if (network >= countof(ice->server))
		{	//really high, its from the raw socket, unstunned.
			network -= countof(ice->server);
			if (network >= countof(ice->server))
				return NETERR_NOROUTE;
			srv = &ice->server[network];

			if (!srv->con || (ice->modeflags & ICEF_RELAY_ONLY))
				return NETERR_CLOGGED;
			return srv->con->SendPacket(srv->con, &srv->addr, data, datasize);
		}
		srv = &ice->server[network];

		memset(&buf, 0, sizeof(buf));
		buf.maxsize =	20+//stun header
						8+16+//(max)peeraddr
						4+((datasize+3)&~3);//data
		buf.cursize = 0;
		buf.data = alloca(buf.maxsize);

		ICE_WriteShort(&buf, BigShort(STUN_INDICATION|STUN_SEND));
		ICE_WriteShort(&buf, 0);	//fill in later
		ICE_WriteLong(&buf, BigLong(STUN_MAGIC_COOKIE));
		ICE_WriteLong(&buf, 0);	//randomid
		ICE_WriteLong(&buf, 0);	//randomid
		ICE_WriteLong(&buf, 0);	//randomid

		if (!TURN_AddXorAddressAttrib(&buf, STUNATTR_XOR_PEER_ADDRESS, to))
			return NETERR_NOROUTE;

		ICE_WriteShort(&buf, BigShort(STUNATTR_DATA));
		ICE_WriteShort(&buf, BigShort(datasize));
		ICE_WriteData(&buf, data, datasize);
		if (datasize&3)
			ICE_WriteData(&buf, "\0\0\0\0", 4-(datasize&3));

		//fill in the length (for the final time, after filling in the integrity and fingerprint)
		buf.data[2] = ((buf.cursize-20)>>8)&0xff;
		buf.data[3] = ((buf.cursize-20)>>0)&0xff;

		if (!srv->con)
			return NETERR_CLOGGED;
		return srv->con->SendPacket(srv->con, &srv->addr, buf.data, buf.cursize);
	}
#endif
	if (ice->modeflags & ICEF_RELAY_ONLY)
		return NETERR_CLOGGED;
	return ICE_SendUDPPacket(ice->module, to, data, datasize);
}
static neterr_t ICE_Transmit(void *cbctx, const qbyte *data, size_t datasize)
{
	struct icestate_s *ice = cbctx;
	if (ice->chosenpeer.type == NA_INVALID)
	{
		if (ice->state == ICE_FAILED)
			return NETERR_DISCONNECTED;
		else
			return NETERR_CLOGGED;
	}

	return TURN_Encapsulate(ice, &ice->chosenpeer, data, datasize);
}
#ifdef HAVE_SCTP
static neterr_t SCTP_SendLowerPacket(struct icestate_s *ice, const void *data, size_t length)
{	//sends to the layer under sctp... usually dtls...
	if (ice)
	{
#ifdef HAVE_DTLS
		if (ice->dtlsstate)
			return ice->dtlsfuncs->Transmit(ice->dtlsstate, data, length);
		else
#endif
		if (ice->chosenpeer.type != NA_INVALID)
			return ICE_Transmit(ice, data, length);
		else if (ice->state < ICE_CONNECTING)
			return NETERR_DISCONNECTED;
		else
			return NETERR_CLOGGED;
	}
	else
		return NETERR_NOROUTE;
}
#endif
static neterr_t ICE_SendPacket(struct icestate_s *con, const void *data, size_t length)
{
	con->icetimeout = Sys_Milliseconds() + (con->icetimeout_ms ? con->icetimeout_ms : 30*1000);

	if (con->state == ICE_CONNECTING)
		return NETERR_CLOGGED;
	else if (con->state == ICE_INACTIVE || con->state == ICE_GATHERING)
		return NETERR_NOROUTE;	//not open yet... requires extra poking.
	else if (con->state != ICE_CONNECTED)
		return NETERR_DISCONNECTED;
#ifdef HAVE_SCTP
	if (con->sctp)
		return SCTP_Transmit(con->sctp, data, length);
#endif
#ifdef HAVE_DTLS
	if (con->dtlsstate)
		return con->dtlsfuncs->Transmit(con->dtlsstate, data, length);
#endif
	if (con->chosenpeer.type != NA_INVALID)
		return ICE_Transmit(con, data, length);
	return NETERR_CLOGGED;	//still pending... waiting for the handshakes.
}

static struct icestate_s *ICE_Create(struct icemodule_s *module, const char *conname, const char *peername, unsigned int modeflags, enum iceproto_e proto)
{
	struct icestate_s *con;
	static unsigned int icenum;
	qboolean initiator = modeflags&ICEF_INITIATOR;

	//only allow modes that we actually support.
#ifndef HAVE_DTLS
	if (modeflags & ICEF_ALLOW_WEBRTC)
		if (!(modeflags & ICEF_ALLOW_PLAIN))
			return NULL;
#endif

	if (!conname)
	{
		int rnd[2];
		Sys_RandomBytes((void*)rnd, sizeof(rnd));
		conname = va("fte%08x%08x", rnd[0], rnd[1]);
	}

	if (ICE_Find(module, conname))
		return NULL;	//don't allow dupes.

	con = calloc(1, sizeof(*con));
	con->module = module;
	con->conname = strdup(conname);
	icenum++;
	con->friendlyname = peername?strdup(peername):strdup(va("%i", icenum));
	con->proto = proto;
	con->rpwd = strdup("");
	con->rufrag = strdup("");
	Sys_RandomBytes((void*)&con->originid, sizeof(con->originid));
	con->originversion = 1;
	q_strlcpy(con->originaddress, "127.0.0.1", sizeof(con->originaddress));

	con->initiator = initiator;
	con->controlled = !initiator;
	con->blockcandidates = true;	//until offers/answers are sent.

#ifdef HAVE_DTLS
	con->dtlspassive = con->controlled;//(proto == ICEP_SERVER);	//note: may change later.

	if (modeflags & ICEF_ALLOW_WEBRTC)
	{	//dtls+sctp is a mandatory part of our connection, sadly.
		if (!con->dtlsfuncs)
		{
			if (con->dtlspassive)
				con->dtlsfuncs = ICE_DTLS_InitServer();
			else
				con->dtlsfuncs = ICE_DTLS_InitClient();	//credentials are a bit different, though fingerprints make it somewhat irrelevant.
		}
		if (con->dtlsfuncs && con->dtlsfuncs->GenTempCertificate && !con->cred.local.certsize)
		{
			Con_DPrintf("Generating dtls certificate...\n");
			if (!con->dtlsfuncs->GenTempCertificate(NULL, &con->cred.local))
			{
				con->dtlsfuncs = NULL;
				modeflags &= ~ICEF_ALLOW_WEBRTC;
				Con_DPrintf("Failed\n");
			}
			else
				Con_DPrintf("Done\n");
		}
		else
		{	//failure if we can't do the whole dtls thing.
			con->dtlsfuncs = NULL;
			Con_Printf(CON_WARNING"DTLS %s support unavailable, disabling encryption (and webrtc compat).\n", con->dtlspassive?"server":"client");
			modeflags &= ~ICEF_ALLOW_WEBRTC;	//fall back on unencrypted (this doesn't depend on the peer, so while shitty it hopefully shouldn't be exploitable with a downgrade-attack)
		}
	}
#endif
#ifdef HAVE_SCTP
	if (modeflags & ICEF_ALLOW_WEBRTC)
	{
		if ((modeflags & ICEF_ALLOW_WEBRTC) && (modeflags & ICEF_ALLOW_PLAIN))
			con->sctpoptional = true;

		if (modeflags & ICEF_ALLOW_WEBRTC)
			con->mysctpport = 27500;
	}
#endif

	con->modeflags = modeflags;

	con->next = icelist;
	icelist = con;

	{
		int rnd[1];	//'must have at least 24 bits randomness'
		Sys_RandomBytes((void*)rnd, sizeof(rnd));
		con->lufrag = strdup(va("%08x", rnd[0]));
	}
	{
		int rnd[4];	//'must have at least 128 bits randomness'
		Sys_RandomBytes((void*)rnd, sizeof(rnd));
		con->lpwd = strdup(va("%08x%08x%08x%08x", rnd[0], rnd[1], rnd[2], rnd[3]));
	}

	Sys_RandomBytes((void*)&con->tiehigh, sizeof(con->tiehigh));
	Sys_RandomBytes((void*)&con->tielow, sizeof(con->tielow));

	{
		int i, m;

		netadr_t	addr[64];
		int			networks[sizeof(addr)/sizeof(addr[0])];
		unsigned int			flags[sizeof(addr)/sizeof(addr[0])];
		const char *params[sizeof(addr)/sizeof(addr[0])];

		m = ICE_EnumerateAddresses(module, networks, flags, addr, params, sizeof(addr)/sizeof(addr[0]));

		for (i = 0; i < m; i++)
		{
			if (addr[i].type == NA_IP || addr[i].type == NA_IPV6)
			{
				if (flags[i] & ADDR_REFLEX)
					ICE_AddLCandidateInfo(con, &addr[i], ICE_SRFLX); //FIXME: needs reladdr relport info
				else
					ICE_AddLCandidateInfo(con, &addr[i], ICE_HOST);
			}
		}
	}

	return con;
}
//if either remotecand is null, new packets will be sent to all.
static qboolean ICE_SendSpam(struct icestate_s *con)
{
	struct icecandidate_s *rc;
	int i;
	int bestlocal = -1;
	struct icecandidate_s *bestpeer = NULL;
	struct icemodule_s *module = con->module;

	//only send one ping to each.
retry_noroute:
	bestlocal = -1;
	bestpeer = NULL;
	for(rc = con->rc; rc; rc = rc->next)
	{
		for (i = 0; i < MAX_NETWORKS; i++)
		{
			if (module->conn[i])
			{
				if (!(rc->tried & (1u<<i)))
				{
					if (!bestpeer || bestpeer->info.priority < rc->info.priority)
					{
						if (NET_ClassifyAddress(&rc->peer, NULL) < ASCOPE_TURN_REQUIRESCOPE)
						{	//don't waste time asking the relay to poke its loopback. if only because it'll report lots of errors.
							rc->tried |= (1u<<i);
							continue;
						}

						bestpeer = rc;
						bestlocal = i;
					}
				}
			}
		}

		//send via appropriate turn servers
		if (rc->info.type == ICE_RELAY || rc->info.type == ICE_SRFLX)
		{
			for (i = 0; i < con->servers; i++)
			{
				if (con->server[i].state!=TURN_ALLOCATED)
					continue;	//not ready yet...
				if (!con->server[i].con)
					continue;	//can't...
				if (!(rc->tried & (1u<<(MAX_NETWORKS+i))))
				{
					//fixme: no local priority. a multihomed machine will try the same ip from different ports.
					if (!bestpeer || bestpeer->info.priority < rc->info.priority)
					{
						if (con->server[i].family && rc->peer.type != con->server[i].family)
							continue;	//if its ipv4-only then don't send it ipv6 packets or whatever.
						bestpeer = rc;
						bestlocal = MAX_NETWORKS+i;
					}
				}
			}
		}
	}


	if (bestpeer && bestlocal >= 0)
	{
		neterr_t err;
		netadr_t to;
		icebuf_t buf;
		qbyte data[512];
		qbyte integ[20];
		int crc;
		qboolean usecandidate = false;
		const char *candtype;
		unsigned int priority;
		memset(&buf, 0, sizeof(buf));
		buf.maxsize = sizeof(data);
		buf.cursize = 0;
		buf.data = data;

		bestpeer->tried |= (1u<<bestlocal);

		to = bestpeer->peer;
		to.connum = 1+bestlocal;

		if (to.type == NA_INVALID)
			return true; //erk?

		switch(bestpeer->info.type)
		{
		case ICE_HOST:	candtype="host";	break;
		case ICE_SRFLX:	candtype="srflx";	break;
		case ICE_PRFLX:	candtype="prflx";	break;
		case ICE_RELAY:	candtype="relay";	break;
		default: candtype="?";				break;
		}

#ifdef HAVE_TURN
		if (bestlocal >= MAX_NETWORKS)
		{
			struct iceserver_s *srv = &con->server[bestlocal-MAX_NETWORKS];
			unsigned int i;
			unsigned int now = Sys_Milliseconds();

			for (i = 0; ; i++)
			{
				if (i == srv->peers)
				{
					if (i == countof(srv->peer))
						return true;
					srv->peer[i].expires = now;
					srv->peer[i].rc = bestpeer;
					Sys_RandomBytes((qbyte*)srv->peer[i].stunrnd, sizeof(srv->peer[i].stunrnd));
					srv->peer[i].retry = now;
					srv->peers++;
				}
				if (srv->peer[i].rc == bestpeer)
				{
					break;
				}
			}
			if ((int)(srv->peer[i].retry-now) <= 0)
			{
				srv->peer[i].retry = now + (con->state==ICE_CONNECTED?2000:50);	//keep retrying till we get an ack. be less agressive once it no longer matters so much
				TURN_AuthorisePeer(con, srv, i);
			}
		}
#endif

		if (!con->controlled && NET_CompareAdr(&to, &con->chosenpeer) && to.connum == con->chosenpeer.connum)
			usecandidate = true;

		ICE_WriteShort(&buf, BigShort(STUN_BINDING));
		ICE_WriteShort(&buf, 0);	//fill in later
		ICE_WriteLong(&buf, BigLong(STUN_MAGIC_COOKIE));	//magic
		ICE_WriteLong(&buf, BigLong(0));					//randomid
		ICE_WriteLong(&buf, BigLong(0));					//randomid
		ICE_WriteLong(&buf, BigLong(0x80000000|bestlocal));	//randomid

		if (usecandidate)
		{
			ICE_WriteShort(&buf, BigShort(STUNATTR_ICE_USE_CANDIDATE));//ICE-USE-CANDIDATE
			ICE_WriteShort(&buf, BigShort(0));	//just a flag, so no payload to this attribute
		}

		//username
		ICE_WriteShort(&buf, BigShort(STUNATTR_USERNAME));	//USERNAME
		ICE_WriteShort(&buf, BigShort(strlen(con->rufrag) + 1 + strlen(con->lufrag)));
		ICE_WriteData(&buf, con->rufrag, strlen(con->rufrag));
		ICE_WriteChar(&buf, ':');
		ICE_WriteData(&buf, con->lufrag, strlen(con->lufrag));
		while(buf.cursize&3)
			ICE_WriteChar(&buf, 0);	//pad

		//priority
		priority =
			//FIXME. should be set to:
			//			priority =	(2^24)*(type preference) +
			//						(2^8)*(local preference) +
			//						(2^0)*(256 - component ID)
			//type preference should be 126 and is a function of the candidate type (direct sending should be highest priority at 126)
			//local preference should reflect multi-homed preferences. ipv4+ipv6 count as multihomed.
			//component ID should be 1 (rtcp would be 2 if we supported it)
			(1<<24)*(bestlocal>=MAX_NETWORKS?0:126) +
			(1<<8)*((bestpeer->peer.type == NA_IP?32768:0)+bestlocal*256+(255/*-adrno*/)) +
			(1<<0)*(256 - bestpeer->info.component);
		ICE_WriteShort(&buf, BigShort(STUNATTR_ICE_PRIORITY));//ICE-PRIORITY
		ICE_WriteShort(&buf, BigShort(4));
		ICE_WriteLong(&buf, BigLong(priority));

		//these two attributes carry a random 64bit tie-breaker.
		//the controller is the one with the highest number.
		if (con->controlled)
		{
			ICE_WriteShort(&buf, BigShort(STUNATTR_ICE_CONTROLLED));//ICE-CONTROLLED
			ICE_WriteShort(&buf, BigShort(8));
			ICE_WriteLong(&buf, BigLong(con->tiehigh));
			ICE_WriteLong(&buf, BigLong(con->tielow));
		}
		else
		{
			ICE_WriteShort(&buf, BigShort(STUNATTR_ICE_CONTROLLING));//ICE-CONTROLLING
			ICE_WriteShort(&buf, BigShort(8));
			ICE_WriteLong(&buf, BigLong(con->tiehigh));
			ICE_WriteLong(&buf, BigLong(con->tielow));
		}

		//message integrity is a bit annoying
		data[2] = ((buf.cursize+4+sizeof(integ)-20)>>8)&0xff;	//hashed header length is up to the end of the hmac attribute
		data[3] = ((buf.cursize+4+sizeof(integ)-20)>>0)&0xff;
		//but the hash is to the start of the attribute's header
		CalcHMAC(&hash_sha1, integ, sizeof(integ), data, buf.cursize, (qbyte*)con->rpwd, strlen(con->rpwd));
		ICE_WriteShort(&buf, BigShort(STUNATTR_MSGINTEGRITIY_SHA1));	//MESSAGE-INTEGRITY
		ICE_WriteShort(&buf, BigShort(20));	//sha1 key length
		ICE_WriteData(&buf, integ, sizeof(integ));	//integrity data

		data[2] = ((buf.cursize+8-20)>>8)&0xff;	//dummy length
		data[3] = ((buf.cursize+8-20)>>0)&0xff;
		crc = crc32(0, data, buf.cursize)^0x5354554e;
		ICE_WriteShort(&buf, BigShort(STUNATTR_FINGERPRINT));	//FINGERPRINT
		ICE_WriteShort(&buf, BigShort(sizeof(crc)));
		ICE_WriteLong(&buf, BigLong(crc));

		//fill in the length (for the fourth time, after filling in the integrity and fingerprint)
		data[2] = ((buf.cursize-20)>>8)&0xff;
		data[3] = ((buf.cursize-20)>>0)&0xff;

		err = TURN_Encapsulate(con, &to, buf.data, buf.cursize);

		switch(err)
		{
		case NETERR_SENT:
			if (con->modeflags & ICEF_VERBOSE_PROBE)
				Con_Printf(S_COLOR_GRAY"[%s]: checking %s -> %s:%i (%s)\n", con->friendlyname, ICE_NetworkToName(con, to.connum), bestpeer->info.addr, bestpeer->info.port, candtype);
			break;
		case NETERR_CLOGGED:	//oh well... we retry anyway.
			break;

		case NETERR_NOROUTE:
			bestpeer->noroute |= (1u<<bestlocal);	//permanently unreachable from this network, don't retry
			if (con->modeflags & ICEF_VERBOSE_PROBE)
				Con_Printf(S_COLOR_GRAY"ICE NETERR_NOROUTE to %s:%i(%s)\n", bestpeer->info.addr, bestpeer->info.port, candtype);
			goto retry_noroute;	//try next candidate/network immediately instead of waiting
		case NETERR_DISCONNECTED:
			if (con->modeflags & ICEF_VERBOSE_PROBE)
				Con_Printf(S_COLOR_GRAY"ICE NETERR_DISCONNECTED to %s:%i(%s)\n", bestpeer->info.addr, bestpeer->info.port, candtype);
			break;
		case NETERR_MTU:
			if (con->modeflags & ICEF_VERBOSE_PROBE)
				Con_Printf(S_COLOR_GRAY"ICE NETERR_MTU to %s:%i(%s) (%i bytes)\n", bestpeer->info.addr, bestpeer->info.port, candtype, buf.cursize);
			break;
		default:
			if (con->modeflags & ICEF_VERBOSE_PROBE)
				Con_Printf(S_COLOR_GRAY"ICE send error(%i) to %s:%i(%s)\n", err, bestpeer->info.addr, bestpeer->info.port, candtype);
			break;
		}
		return true;
	}
	return false;
}

// Try to add an srflx candidate from the module's cached STUN response
// instead of sending a new binding request.
// Returns true if a cached address was available.
static qboolean ICE_UseCachedSrflx(struct icestate_s *con, struct iceserver_s *srv)
{
	int fidx = (srv->addr.type != NA_IP);
	netadr_t *cached = &con->module->srflx[fidx];
	struct icecandidate_s *rc;
	int rnd[2];
	struct icecandidate_s *src;

	if (cached->type == NA_INVALID)
		return false;

	// Already have this candidate?
	for (rc = con->lc; rc; rc = rc->next)
	{
		if (NET_CompareAdr(cached, &rc->peer))
			return true;
	}

	src = calloc(1, sizeof(*src));
	src->next = con->lc;
	con->lc = src;
	src->peer = *cached;
	NET_BaseAdrToString(src->info.addr, sizeof(src->info.addr), cached);
	src->info.port = con->module->srflx_port ? con->module->srflx_port : NET_AdrToPort(cached);
	src->info.type = ICE_SRFLX;
	src->info.component = 1;
	src->info.network = cached->connum;
	src->dirty = true;
	src->info.priority = ICE_ComputePriority(&src->peer, &src->info);

	Sys_RandomBytes((void*)rnd, sizeof(rnd));
	q_strlcpy(src->info.candidateid, va("x%08x%08x", rnd[0], rnd[1]), sizeof(src->info.candidateid));

	if (con->modeflags & ICEF_VERBOSE)
		Con_Printf(S_COLOR_GRAY"[%s]: Public address (cached): %s:%d\n", con->friendlyname, src->info.addr, src->info.port);

	return true;
}

static void ICE_ToStunServer(struct icestate_s *con, struct iceserver_s *srv)
{
	icebuf_t buf;
	qbyte data[512];
	char adrstr[64];
	int crc;
	struct icemodule_s *module = con->module;
	neterr_t err;

	memset(&buf, 0, sizeof(buf));
	buf.maxsize = sizeof(data);
	buf.cursize = 0;
	buf.data = data;

	if (srv->isstun)
	{
		if (!(con->modeflags&ICEF_ALLOW_STUN) || (con->modeflags & ICEF_RELAY_ONLY))
			return;
		if (con->modeflags & ICEF_VERBOSE_PROBE)
			Con_Printf(S_COLOR_GRAY"[%s]: STUN: Checking public IP via %s\n", con->friendlyname, NET_AdrToString(adrstr, sizeof(adrstr), &srv->addr));
		ICE_WriteShort(&buf, BigShort(STUN_BINDING));
	}
	else
	{
		if (!(con->modeflags&ICEF_ALLOW_TURN))
		{
			if (con->modeflags&ICEF_RELAY_ONLY)
			{
				Con_Printf("%s: forcing TURN on\n", con->friendlyname);
				con->modeflags|=ICEF_ALLOW_TURN;
			}
			return;
		}
		if (!srv->con)
		{
			if (srv->addr.type == NA_INVALID)
				return; //nope...
			if (srv->addr.prot != NP_DGRAM)
			{
#ifdef HAVE_TCP
				srv->con = TURN_TCP_EstablishConnection(srv->realm, &srv->addr, srv->addr.prot == NP_TLS);
#else
				srv->con = NULL;
#endif
			}
			else
				srv->con = ICE_OpenUDP(srv->addr.type, 0);
			if (!srv->con)
			{
				srv->addr.type = NA_INVALID; //fail it.
				return;
			}
			srv->connum = 1+MAX_NETWORKS+countof(con->server)+(srv-con->server);	//*sigh*
		}

		if (srv->state==TURN_TERMINATING)
		{
			if (con->modeflags & ICEF_VERBOSE_PROBE)
				Con_Printf(S_COLOR_GRAY"[%s]: TURN: Terminating %s\n", con->friendlyname, NET_AdrToString(adrstr, sizeof(adrstr), &srv->addr));
			ICE_WriteShort(&buf, BigShort(STUN_REFRESH));
		}
		else if (srv->state==TURN_ALLOCATED)
		{
			if (con->modeflags & ICEF_VERBOSE_PROBE)
				Con_Printf(S_COLOR_GRAY"[%s]: TURN: Refreshing %s\n", con->friendlyname, NET_AdrToString(adrstr, sizeof(adrstr), &srv->addr));
			ICE_WriteShort(&buf, BigShort(STUN_REFRESH));
		}
		else
		{
			if (con->modeflags & ICEF_VERBOSE_PROBE)
				Con_Printf(S_COLOR_GRAY "[%s]: TURN: Allocating %s\n", con->friendlyname, NET_AdrToString(adrstr, sizeof(adrstr), &srv->addr));
			ICE_WriteShort(&buf, BigShort(STUN_ALLOCATE));
		}
	}
	Sys_RandomBytes((qbyte*)srv->stunrnd, sizeof(srv->stunrnd));

	ICE_WriteShort(&buf, 0);	//fill in later
	ICE_WriteLong(&buf, BigLong(STUN_MAGIC_COOKIE));
	ICE_WriteLong(&buf, srv->stunrnd[0]);	//randomid
	ICE_WriteLong(&buf, srv->stunrnd[1]);	//randomid
	ICE_WriteLong(&buf, srv->stunrnd[2]);	//randomid

	if (!srv->isstun)
	{
		if (srv->state<TURN_ALLOCATED)
		{
			ICE_WriteShort(&buf, BigShort(STUNATTR_REQUESTED_TRANSPORT));
			ICE_WriteShort(&buf, BigShort(4));
			ICE_WriteLong(&buf, 17/*udp*/);

			switch (srv->family)
			{
			case NA_IP:
				//ICE_WriteShort(&buf, BigShort(STUNATTR_REQUESTED_ADDRFAM));
				//ICE_WriteShort(&buf, BigShort(4));
				//ICE_WriteLong(&buf, 1/*ipv4*/);
				break;
			case NA_IPV6:
				ICE_WriteShort(&buf, BigShort(STUNATTR_REQUESTED_ADDRFAM));
				ICE_WriteShort(&buf, BigShort(4));
				ICE_WriteLong(&buf, 2/*ipv6*/);
				break;
			case NA_INVALID:	//ask for both ipv4+ipv6.
				ICE_WriteShort(&buf, BigShort(STUNATTR_ADDITIONAL_ADDRFAM));
				ICE_WriteShort(&buf, BigShort(4));
				ICE_WriteLong(&buf, 2/*ipv6*/);
				break;
			default:
				return;	//nope... not valid.
			}
		}

//		ICE_WriteShort(&buf, BigShort(STUNATTR_DONT_FRAGMENT));
//		ICE_WriteShort(&buf, BigShort(0));

/*		ICE_WriteShort(&buf, BigShort(STUNATTR_SOFTWARE));
		crc = strlen(FULLENGINENAME);
		ICE_WriteShort(&buf, BigShort(crc));
		ICE_WriteData (&buf, FULLENGINENAME, crc);
		if (crc&3)
			ICE_WriteData (&buf, "\0\0\0\0", 4-(crc&3));
*/

		if (srv->state==TURN_TERMINATING)
		{
			ICE_WriteShort(&buf, BigShort(STUNATTR_LIFETIME));
			ICE_WriteShort(&buf, BigShort(4));
			ICE_WriteLong(&buf, 0);
		}
		else
		{
//			ICE_WriteShort(&buf, BigShort(STUNATTR_LIFETIME));
//			ICE_WriteShort(&buf, BigShort(4));
//			ICE_WriteLong(&buf, BigLong(300));

			if (srv->state != TURN_UNINITED)
			{
				if (!TURN_AddAuth(&buf, srv))
					return;
			}
		}
	}
	else
	{
		data[2] = ((buf.cursize+8-20)>>8)&0xff;	//dummy length
		data[3] = ((buf.cursize+8-20)>>0)&0xff;
		crc = crc32(0, data, buf.cursize)^0x5354554e;
		ICE_WriteShort(&buf, BigShort(STUNATTR_FINGERPRINT));
		ICE_WriteShort(&buf, BigShort(sizeof(crc)));
		ICE_WriteLong(&buf, BigLong(crc));
	}

	//fill in the length (for the final time, after filling in the integrity and fingerprint)
	data[2] = ((buf.cursize-20)>>8)&0xff;
	data[3] = ((buf.cursize-20)>>0)&0xff;

	if (srv->isstun)
		err = ICE_SendUDPPacket(module, &srv->addr, data, buf.cursize);
	else if (srv->con)
		err = srv->con->SendPacket(srv->con, &srv->addr, data, buf.cursize);
	else
		return;
	if (err == NETERR_CLOGGED)
		srv->stunretry = Sys_Milliseconds();	//just keep retrying until it actually goes through.
}

static void TURN_AuthorisePeer(struct icestate_s *con, struct iceserver_s *srv, int peer)
{
	struct icecandidate_s *rc = srv->peer[peer].rc;
	icebuf_t buf;
	netadr_t to2;
	qbyte data[512];
	if (srv->state != TURN_ALLOCATED)
		return;
	memset(&buf, 0, sizeof(buf));
	buf.maxsize = sizeof(data);
	buf.cursize = 0;
	buf.data = data;

	ICE_WriteShort(&buf, BigShort(STUN_CREATEPERM));
	ICE_WriteShort(&buf, 0);	//fill in later
	ICE_WriteLong(&buf, BigLong(STUN_MAGIC_COOKIE));		//magic
	ICE_WriteLong(&buf, srv->peer[peer].stunrnd[0]);	//randomid
	ICE_WriteLong(&buf, srv->peer[peer].stunrnd[1]);	//randomid
	ICE_WriteLong(&buf, srv->peer[peer].stunrnd[2]);	//randomid

	if (!TURN_AddXorAddressAttrib(&buf, STUNATTR_XOR_PEER_ADDRESS, &rc->peer))
		return;
	if (*rc->info.reladdr && strcmp(rc->info.addr, rc->info.reladdr))	//allow the relay to bypass the peer's relay if its different (TURN doesn't care about port permissions).
		if (NET_StringToAdr(rc->info.reladdr, rc->info.relport, &to2, 1))
			TURN_AddXorAddressAttrib(&buf, STUNATTR_XOR_PEER_ADDRESS, &to2);
	if (!TURN_AddAuth(&buf, srv))
		return;

	buf.data[2] = ((buf.cursize-20)>>8)&0xff;
	buf.data[3] = ((buf.cursize-20)>>0)&0xff;
	srv->con->SendPacket(srv->con, &srv->addr, buf.data, buf.cursize);

	if (con->modeflags & ICEF_VERBOSE)
		Con_Printf(S_COLOR_GRAY"[%s]: (re)registering %s -> %s:%i (%s)\n", con->friendlyname, srv->realm, rc->info.addr, rc->info.port, ICE_GetCandidateType(&rc->info));
}

void ICE_AddRCandidateInfo(struct icestate_s *con, struct icecandinfo_s *n)
{
	struct icecandidate_s *o;
	int rccount = 0;
	qboolean isnew;
	netadr_t peer;
	int peerbits;
	//I don't give a damn about rtcp.
	if (n->component != 1)
		return;
	if (n->transport != 0)
		return;	//only UDP is supported.

	//check if its an mDNS name - must be a UUID, with a .local on the end.
	if ((con->modeflags & ICEF_ALLOW_MDNS) && MDNS_AddQuery(con->module, con, n))
		return;

	//don't use the regular string->addr, they can fail and stall and make us unresponsive etc. hostnames don't really make sense here anyway.
	peerbits = ParsePartialIP(n->addr, &peer);
	peer.prot = NP_DGRAM;
	if (peer.type == NA_IP && peerbits == 32)
	{
		qbyte *ip = (qbyte*)&peer.in.sin_addr;
		//ignore invalid addresses
		if (!ip[0] && !ip[1] && !ip[2] && !ip[3])
			return;

		peer.in.sin_port = htons(n->port);
	}
	else if (peer.type == NA_IPV6 && peerbits == 128)
	{
		//ignore invalid addresses
		int i;
		qbyte *ip = (qbyte*)&peer.in6.sin6_addr;
		for (i = 0; i < sizeof(peer.in6.sin6_addr); i++)
			if (ip[i])
				break;
		if (i == sizeof(peer.in6.sin6_addr))
			return; //all clear. in6_addr_any

		peer.in6.sin6_port = htons(n->port);
	}
	else
	{
		Con_Printf("Bad remote candidate name: %s\n", n->addr);
		return;	//bad address type, or partial.
	}

	if (*n->candidateid)
	{
		for (o = con->rc; o; o = o->next)
		{
			rccount++;
			//not sure that updating candidates is particuarly useful tbh, but hey.
			if (!strcmp(o->info.candidateid, n->candidateid))
				break;
		}
	}
	else
	{
		for (o = con->rc; o; o = o->next)
		{
			rccount++;
			//avoid dupes.
			if (!strcmp(o->info.addr, n->addr) && o->info.port == n->port)
				break;
		}
	}
	if (!o)
	{
		if (rccount >= 256)
			return;	//reject excessive candidates
		o = calloc(1, sizeof(*o));
		o->next = con->rc;
		con->rc = o;
		q_strlcpy(o->info.candidateid, n->candidateid, sizeof(o->info.candidateid));

		isnew = true;
	}
	else
	{
		isnew = false;
	}
	q_strlcpy(o->info.addr, n->addr, sizeof(o->info.addr));
	o->info.port = n->port;
	o->info.type = n->type;
	o->info.priority = n->priority;
	o->info.network = n->network;
	o->info.generation = n->generation;
	o->info.foundation = n->foundation;
	o->info.component = n->component;
	o->info.transport = n->transport;
	o->dirty = true;
	o->peer = peer;
	o->tried = 0;
	o->reachable = 0;

	if (con->modeflags & ICEF_VERBOSE)
		Con_Printf(S_COLOR_GRAY"[%s]: %s remote candidate %s: [%s]:%i\n", con->friendlyname, isnew?"Added":"Updated", o->info.candidateid, o->info.addr, o->info.port);

	if (n->type == ICE_RELAY && *n->reladdr && (strcmp(n->addr, n->reladdr) || n->port != n->relport))
	{	//for relay candidates, add an srflx candidate too.
		struct icecandinfo_s t = o->info;
		t.type = ICE_SRFLX;
		q_strlcpy(t.addr, n->reladdr, sizeof(t.addr));
		t.port = n->relport;
		*t.reladdr = 0;
		t.relport = 0;
		t.priority |= 1<<24;	//nudge its priority up slightly to favour more direct links when we can.
		*t.candidateid = 0;		//anonymous...
		ICE_AddRCandidateInfo(con, &t);
	}
}


static char Base64_Encode(int byt)
{
	if (byt >= 0 && byt < 26)
		return 'A' + byt - 0;
	if (byt >= 26 && byt < 52)
		return 'a' + byt - 26;
	if (byt >= 52 && byt < 62)
		return '0' + byt - 52;
	if (byt == 62)
		return '+';
	if (byt == 63)
		return '/';
	return '!';
}
size_t Base64_EncodeBlock(const qbyte *in, size_t length, char *out, size_t outsize)
{
	char *start = out;
	char *end = out+outsize-1;
	unsigned int v;
	while(length > 0)
	{
		v = 0;
		if (length > 0)
			v |= in[0]<<16;
		if (length > 1)
			v |= in[1]<<8;
		if (length > 2)
			v |= in[2]<<0;

		if (out < end) *out++ = (length>=1)?Base64_Encode((v>>18)&63):'=';
		if (out < end) *out++ = (length>=1)?Base64_Encode((v>>12)&63):'=';
		if (out < end) *out++ = (length>=2)?Base64_Encode((v>>6)&63):'=';
		if (out < end) *out++ = (length>=3)?Base64_Encode((v>>0)&63):'=';

		in+=3;
		if (length <= 3)
			break;
		length -= 3;
	}
	end++;
	if (out < end)
		*out = 0;
	return out-start;
}
size_t Base64_EncodeBlockURI(const qbyte *in, size_t length, char *out, size_t outsize)
{	//special uri-safe version (also trims)
	outsize = Base64_EncodeBlock(in, length, out, outsize);
	for (length = 0; length < outsize; length++)
	{
		if (out[length] == '+')
			out[length] = '-';
		else if (out[length] == '/')
			out[length] = '_';
		else if (out[length] == '=')
		{	//truncate it here.
			out[length] = 0;
			return length;
		}
	}
	return outsize;
}


#ifdef HAVE_DTLS
static int Base64_Decode(char inp)
{
	if (inp >= 'A' && inp <= 'Z')
		return (inp-'A') + 0;
	if (inp >= 'a' && inp <= 'z')
		return (inp-'a') + 26;
	if (inp >= '0' && inp <= '9')
		return (inp-'0') + 52;
	if (inp == '+' || inp == '-')
		return 62;
	if (inp == '/' || inp == '_')
		return 63;
	//if (inp == '=') //padding char
	return 0;	//invalid
}
static size_t Base64_DecodeBlock(const char *in, const char *in_end, qbyte *out, size_t outsize)
{
	qbyte *start = out;
	unsigned int v;
	if (!in_end)
		in_end = in + strlen(in);
	if (!out)
		return ((in_end-in+3)/4)*3 + 1;	//upper estimate, with null terminator for convienience.

	for (; outsize > 1;)
	{
		while(*in > 0 && *in < ' ')
			in++;
		if (in >= in_end || !*in || outsize < 1)
			break;	//end of message when EOF, otherwise error
		v  = Base64_Decode(*in++)<<18;
		while(*in > 0 && *in < ' ')
			in++;
		if (in >= in_end || !*in || outsize < 1)
			break;	//some kind of error
		v |= Base64_Decode(*in++)<<12;
		*out++ = (v>>16)&0xff;
		if (in >= in_end || *in == '=' || !*in || outsize < 2)
			break;	//end of message when '=', otherwise error
		v |= Base64_Decode(*in++)<<6;
		*out++ = (v>>8)&0xff;
		if (in >= in_end || *in == '=' || !*in || outsize < 3)
			break;	//end of message when '=', otherwise error
		v |= Base64_Decode(*in++)<<0;
		*out++ = (v>>0)&0xff;
		outsize -= 3;
	}
	return out-start;	//total written (no null, output is considered binary)
}
void ICE_DePEM(struct dtlslocalcred_s *cred)
{	//base64 is bigger, and we handily have padding at the start. this means we can do it in-place and just truncate.
	static const char cert_begin[] = "-----BEGIN CERTIFICATE-----";
	static const char cert_end[] = "-----END CERTIFICATE-----";
	static const char rsa_key_begin[] = "-----BEGIN RSA PRIVATE KEY-----";
	static const char rsa_key_end[] = "-----END RSA PRIVATE KEY-----";
	static const char pkcs8_key_begin[] = "-----BEGIN PRIVATE KEY-----";
	static const char pkcs8_key_end[] = "-----END PRIVATE KEY-----";
	static const char ec_key_begin[] = "-----BEGIN EC PRIVATE KEY-----";
	static const char ec_key_end[] = "-----END EC PRIVATE KEY-----";

	if (cred->certsize >= sizeof(cert_begin)-1 && !strncmp(cred->cert, cert_begin, sizeof(cert_begin)-1))
	{
		char *start = (char*)cred->cert + sizeof(cert_begin)-1;
		char *end;
		end = strstr(start, cert_end);
		if (end)
			cred->certsize = Base64_DecodeBlock(start, end, cred->cert, cred->certsize);
	}

	if (cred->keysize >= sizeof(rsa_key_begin)-1 && !strncmp(cred->key, rsa_key_begin, sizeof(rsa_key_begin)-1))
	{
		char *start = (char*)cred->key + sizeof(rsa_key_begin)-1;
		char *end;
		end = strstr(start, rsa_key_end);
		if (end)
			cred->keysize = Base64_DecodeBlock(start, end, cred->key, cred->keysize);
	}
	else if (cred->keysize >= sizeof(pkcs8_key_begin)-1 && !strncmp(cred->key, pkcs8_key_begin, sizeof(pkcs8_key_begin)-1))
	{
		char *start = (char*)cred->key + sizeof(pkcs8_key_begin)-1;
		char *end;
		end = strstr(start, pkcs8_key_end);
		if (end)
			cred->keysize = Base64_DecodeBlock(start, end, cred->key, cred->keysize);
	}
	else if (cred->keysize >= sizeof(ec_key_begin)-1 && !strncmp(cred->key, ec_key_begin, sizeof(ec_key_begin)-1))
	{
		char *start = (char*)cred->key + sizeof(ec_key_begin)-1;
		char *end;
		end = strstr(start, ec_key_end);
		if (end)
			cred->keysize = Base64_DecodeBlock(start, end, cred->key, cred->keysize);
	}
}
#endif

static qboolean ICE_Set(struct icestate_s *con, const char *prop, const char *value);
static void ICE_ParseSDPLine(struct icestate_s *con, const char *value)
{
	if      (!strncmp(value, "a=ice-pwd:", 10))
		ICE_Set(con, "rpwd", value+10);
	else if (!strncmp(value, "a=ice-ufrag:", 12))
		ICE_Set(con, "rufrag", value+12);
#ifdef HAVE_DTLS
	else if (!strncmp(value, "a=setup:", 8))
	{	//this is their state, so we want the opposite.
		if (!strncmp(value+8, "passive", 7))
			con->dtlspassive = false;
		else if (!strncmp(value+8, "active", 6))
			con->dtlspassive = true;
	}
	else if (!strncmp(value, "a=fingerprint:", 14))
	{
		hashfunc_t *hash = NULL;
		int i;
		char name[64];
		value = COM_ParseOut(value+14, name, sizeof(name));
		for (i = 0; i < countof(webrtc_hashes); i++)
		{
			if (!q_strcasecmp(name, webrtc_hashes[i].name))
			{
				hash = webrtc_hashes[i].hash;
				break;
			}
		}
		if (hash && (!con->cred.peer.hash || hash->digestsize>con->cred.peer.hash->digestsize))	//FIXME: digest size is not a good indicator of whether its exploitable or not, but should work for sha1/sha2 options. the sender here is expected to be trustworthy anyway.
		{
			int b, o, v;
			while (*value == ' ')
				value++;
			for (b = 0; b < hash->digestsize; )
			{
				v = *value;
				if      (v >= '0' && v <= '9')
					o = (v-'0');
				else if (v >= 'A' && v <= 'F')
					o = (v-'A'+10);
				else if (v >= 'a' && v <= 'f')
					o = (v-'a'+10);
				else
					break;
				o <<= 4;
				v = *++value;
				if      (v >= '0' && v <= '9')
					o |= (v-'0');
				else if (v >= 'A' && v <= 'F')
					o |= (v-'A'+10);
				else if (v >= 'a' && v <= 'f')
					o |= (v-'a'+10);
				else
					break;
				con->cred.peer.digest[b++] = o;
				v = *++value;
				if (v != ':')
					break;
				value++;
			}
			if (b == hash->digestsize)
				con->cred.peer.hash = hash;	//it was the right size, woo.
			else
				con->cred.peer.hash = NULL; //bad! (should we 0-pad?)
		}
	}
#endif
#ifdef HAVE_SCTP
	else if (!strncmp(value, "a=sctp-port:", 12))
		con->peersctpport = atoi(value+12);
	else if (!strncmp(value, "a=sctp-optional:", 16))
		con->peersctpoptional = atoi(value+16);
#endif
	else if (!strncmp(value, "a=rtpmap:", 9))
	{
		char name[64];
		int codec;
		char *sl;
		value += 9;
		codec = strtoul(value, (char**)&value, 0);
		if (*value == ' ') value++;

		COM_ParseOut(value, name, sizeof(name));
		sl = strchr(name, '/');
		if (sl)
			*sl = '@';
		ICE_Set(con, va("codec%i", codec), name);
	}
	else if (!strncmp(value, "a=candidate:", 12))
	{
		struct icecandinfo_s n;
		memset(&n, 0, sizeof(n));

		value += 12;
		n.foundation = strtoul(value, (char**)&value, 0);

		if(*value == ' ')value++;
		n.component = strtoul(value, (char**)&value, 0);

		if(*value == ' ')value++;
		if (!q_strncasecmp(value, "UDP ", 4))
		{
			n.transport = 0;
			value += 3;
		}
		else
			return;

		if(*value == ' ')value++;
		n.priority = strtoul(value, (char**)&value, 0);

		if(*value == ' ')value++;
		value = COM_ParseOut(value, n.addr, sizeof(n.addr));
		if (!value) return;

		if(*value == ' ')value++;
		n.port = strtoul(value, (char**)&value, 0);

		if(*value == ' ')value++;
		if (strncmp(value, "typ ", 4)) return;
		value += 3;

		if(*value == ' ')value++;
		if (!strncmp(value, "host", 4))
			n.type = ICE_HOST;
		else if (!strncmp(value, "srflx", 4))
			n.type = ICE_SRFLX;
		else if (!strncmp(value, "prflx", 4))
			n.type = ICE_PRFLX;
		else if (!strncmp(value, "relay", 4))
			n.type = ICE_RELAY;
		else
			return;

		while (*value)
		{
			if(*value == ' ')value++;
			if (!strncmp(value, "raddr ", 6))
			{
				value += 6;
				value = COM_ParseOut(value, n.reladdr, sizeof(n.reladdr));
				if (!value)
					break;
			}
			else if (!strncmp(value, "rport ", 6))
			{
				value += 6;
				n.relport = strtoul(value, (char**)&value, 0);
			}
			/*else if (!strncmp(value, "network-cost ", 13))
			{
				value += 13;
				n.netcost = strtoul(value, (char**)&value, 0);
			}*/
			/*else if (!strncmp(value, "ufrag ", 6))
			{
				value += 6;
				while (*value && *value != ' ')
					value++;
			}*/
			else
			{
				//this is meant to be extensible.
				while (*value && *value != ' ')
					value++;
				if(*value == ' ')value++;
				while (*value && *value != ' ')
					value++;
			}
		}
		ICE_AddRCandidateInfo(con, &n);
	}
}

static qboolean ICE_Set(struct icestate_s *con, const char *prop, const char *value)
{
	if (!strcmp(prop, "state"))
	{
		int oldstate = con->state;
		if (!strcmp(value, STRINGIFY(ICE_CONNECTING)))
		{
			con->state = ICE_CONNECTING;
			if (con->modeflags & ICEF_VERBOSE)
				Con_Printf(S_COLOR_GRAY"[%s]: ice state connecting\n", con->friendlyname);
		}
		else if (!strcmp(value, STRINGIFY(ICE_GATHERING)))
		{	//an initial state available before querying sdpoffer (doesn't time out)
			con->state = ICE_GATHERING;
			if (con->modeflags & ICEF_VERBOSE)
				Con_Printf(S_COLOR_GRAY"[%s]: ice state gathering\n", con->friendlyname);
		}
		else if (!strcmp(value, STRINGIFY(ICE_INACTIVE)))
		{
			con->state = ICE_INACTIVE;
			if (con->modeflags & ICEF_VERBOSE)
				Con_Printf(S_COLOR_GRAY"[%s]: ice state inactive\n", con->friendlyname);
		}
		else if (!strcmp(value, STRINGIFY(ICE_FAILED)))
		{
			if (con->state != ICE_FAILED)
			{
				con->state = ICE_FAILED;
				if (con->modeflags & ICEF_VERBOSE)
					Con_Printf(S_COLOR_GRAY"[%s]: ice state failed\n", con->friendlyname);
			}
		}
		else if (!strcmp(value, STRINGIFY(ICE_CONNECTED)))
		{
			con->state = ICE_CONNECTED;
			if (con->modeflags & ICEF_VERBOSE)
				Con_Printf(S_COLOR_GRAY"[%s]: ice state connected\n", con->friendlyname);
		}
		else
		{
			Con_Printf("ICE_Set invalid state %s\n", value);
			con->state = ICE_INACTIVE;
		}
		con->icetimeout = Sys_Milliseconds() + (con->icetimeout_ms ? con->icetimeout_ms : 30*1000);

		con->retries = 0;

		if (con->state >= ICE_CONNECTING)
		{
			if (con->module->SendInitial)
				con->module->SendInitial(con->module, con);

			if (con->modeflags & ICEF_ALLOW_WEBRTC)
			{
#ifdef HAVE_DTLS
				if (!con->dtlsstate && con->dtlsfuncs)
				{
					if (con->cred.peer.hash)
						con->dtlsstate = con->dtlsfuncs->CreateContext(&con->cred, con, ICE_Transmit, con->dtlspassive);
					else if (!(con->modeflags & ICEF_ALLOW_PLAIN))
					{	//peer doesn't seem to support dtls.
						ICE_SetFailed(con, "peer does not support dtls. Set net_enable_dtls to 1 to make optional.\n");
					}
					//else if (con->state == ICE_CONNECTING && net_enable_dtls.ival>=2)
					//	Con_Printf(CON_WARNING"WARNING: [%s]: peer does not support dtls.\n", con->friendlyname);
				}
#endif
#ifdef HAVE_SCTP
				if (!con->sctp && (!con->sctpoptional || !con->peersctpoptional) && con->mysctpport && con->peersctpport)
					con->sctp = SCTP_Create(con, con->friendlyname, con->modeflags, con->mysctpport, con->peersctpport, SCTP_SendLowerPacket);
#endif
			}
#ifdef HAVE_DTLS
			else if (!con->dtlsstate && con->cred.peer.hash)
			{
#ifdef HAVE_SCTP
				if (!con->peersctpoptional)
#endif
					ICE_SetFailed(con, "peer is trying to use dtls.\n");
			}
#endif
		}

		if (oldstate != con->state && con->state == ICE_INACTIVE)
		{	//forget our peer
			struct icecandidate_s *c;
			int i;
			memset(&con->chosenpeer, 0, sizeof(con->chosenpeer));

#ifdef HAVE_SCTP
			if (con->sctp)
			{
				SCTP_Destroy(con->sctp);
				con->sctp = NULL;
			}
#endif
#ifdef HAVE_DTLS
			if (con->dtlsstate)
			{
				con->dtlsfuncs->DestroyContext(con->dtlsstate);
				con->dtlsstate = NULL;
			}
#endif
			while(con->rc)
			{
				c = con->rc;
				con->rc = c->next;
				free(c);
			}
			while(con->lc)
			{
				c = con->lc;
				con->lc = c->next;
				free(c);
			}
			for (i = 0; i < con->servers; i++)
			{
				struct iceserver_s *s = &con->server[i];
				if (s->con)
				{	//make sure we tell the TURN server to release our allocation.
					s->state = TURN_TERMINATING;
					ICE_ToStunServer(con, s);

					s->con->CloseSocket(s->con);
					s->con = NULL;
				}
				free(s->nonce);
				s->nonce = NULL;
				s->peers = 0;
			}
		}

		if (oldstate != con->state && con->state == ICE_CONNECTED)
		{
			if (con->chosenpeer.type == NA_INVALID)
				ICE_SetFailed(con, "ICE failed. peer not valid.\n");
/*#ifdef HAVE_SERVER
			else if (con->proto == ICEP_SERVER && con->mode != ICEM_WEBRTC)
			{	//optional, but can save some time by not waiting for resend.
				net_from = con->qadr;
				SVC_GetChallenge(false);
			}
#endif*/
			if (con->state == ICE_CONNECTED)
			{
				if (con->proto >= ICEP_VOICE || (con->modeflags & ICEF_VERBOSE))
				{
					char msg[256];
					Con_Printf(S_COLOR_GRAY "[%s]: %s connection established (peer %s, via %s).\n", con->friendlyname, con->proto == ICEP_VOICE?"voice":"data", NET_AdrToString(msg, sizeof(msg), &con->chosenpeer), ICE_NetworkToName(con, con->chosenpeer.connum));
				}
			}
		}

#if defined(HAVE_CLIENT) && defined(VOICECHAT)
		snd_voip_send.ival = (snd_voip_send.ival & ~4) | (NET_RTP_Active()?4:0);
#endif
	}
	else if (!strcmp(prop, "controlled"))
		con->controlled = !!atoi(value);
	else if (!strcmp(prop, "controller"))
		con->controlled = !atoi(value);
	else if (!strncmp(prop, "codec", 5))
	{
		struct icecodecslot_s *codec = ICE_GetCodecSlot(con, atoi(prop+5));
		if (!codec)
			return false;
		codec->id = atoi(prop+5);
#if defined(HAVE_CLIENT) && defined(VOICECHAT)
		if (!S_Voip_RTP_CodecOkay(value))
#endif
		{
			free(codec->name);
			codec->name = NULL;
			return false;
		}
		free(codec->name);
		codec->name = strdup(value);
	}
	else if (!strcmp(prop, "rufrag"))
	{
		free(con->rufrag);
		con->rufrag = strdup(value);
	}
	else if (!strcmp(prop, "rpwd"))
	{
		free(con->rpwd);
		con->rpwd = strdup(value);
	}
	else if (!strcmp(prop, "brokerless"))
	{
		con->brokerless = atoi(value) != 0;
	}
	else if (!strcmp(prop, "timeout"))
	{
		con->icetimeout_ms = atoi(value);
	}
	else if (!strcmp(prop, "server"))
	{
		netadr_t hostadr[1];
		qboolean okay;
		qboolean tcp = false;
		qboolean tls;
		qboolean stun;
		char *s, *next;
		const char *user=NULL, *auth=NULL;
		char *host;
		netadrtype_t family = NA_INVALID;
		if (!strncmp(value, "stun:", 5))
			stun=true, tls=false, value+=5;
		else if (!strncmp(value, "turn:", 5))
			stun=false, tls=false, value+=5;
		else if (!strncmp(value, "turns:", 6))
			stun=false, tls=true, value+=6;
		else
			return false;	//nope, uri not known.

		host = strdup(value);

		s = strchr(host, '?');
		for (;s;s=next)
		{
			*s++ = 0;
			next = strchr(s, '?');
			if (next)
				*next = 0;

			if (!strncmp(s, "transport=", 10))
			{
				if (!strcmp(s+10, "udp"))
					tcp=false;
				else if (!strcmp(s+10, "tcp"))
					tcp=true;
			}
			else if (!strncmp(s, "user=", 5))
				user = s+5;
			else if (!strncmp(s, "auth=", 5))
				auth = s+5;
			else if (!strncmp(s, "fam=", 4))
			{
				if (!strcmp(s+4, "ipv4") || !strcmp(s+4, "ip4") || !strcmp(s+4, "ip") || !strcmp(s+4, "4"))
					family=NA_IP;
				else if (!strcmp(s+4, "ipv6") || !strcmp(s+4, "ip6") || !strcmp(s+4, "6"))
					family=NA_IPV6;
			}
		}

		okay = !strchr(host, '/');
		if (con->servers == countof(con->server))
			okay = false;
		else if (okay)
		{
			struct iceserver_s *srv = &con->server[con->servers];

			//handily both stun and turn default to the same port numbers.
			//FIXME: worker thread...
			okay = NET_StringToAdr(host, tls?5349:3478, hostadr, 1);
			if (okay)
			{
				if (tls)
					hostadr->prot = tcp?NP_TLS:NP_DTLS;
				else
					hostadr->prot = tcp?NP_STREAM:NP_DGRAM;

				con->servers++;
				srv->isstun = stun;
				srv->family = family;
				srv->realm = strdup(host);
				Sys_RandomBytes((qbyte*)srv->stunrnd, sizeof(srv->stunrnd));
				srv->stunretry = Sys_Milliseconds();	//'now'...
				srv->addr = *hostadr;
				srv->user = user?strdup(user):NULL;
				srv->auth = auth?strdup(auth):NULL;
			}
		}

		free(host);
		return !!okay;
	}
	else if (!strcmp(prop, "sdp") || !strcmp(prop, "sdpoffer") || !strcmp(prop, "sdpanswer"))
	{
		char line[8192];
		const char *eol;
		for (; *value; value = eol)
		{
			eol = strchr(value, '\n');
			if (!eol)
				eol = value+strlen(value);

			if (eol-value < sizeof(line))
			{
				memcpy(line, value, eol-value);
				line[eol-value] = 0;
				if (eol>value && line[eol-value-1] == '\r')
					line[eol-value-1] = 0;
				ICE_ParseSDPLine(con, line);
			}

			if (eol && *eol)
				eol++;
		}

		if (strcmp(prop, "sdp"))
		{	//progress the state if we're sending the offer/answer.
			if (con->state == ICE_GATHERING)
				con->state = ICE_CONNECTING;
			con->blockcandidates = false;
		}
	}
	else if (!strcmp(prop, "peer"))
	{	//this bypasses the whole broker etc thing.
		struct icesocket_s *link = NULL;
		if (con->state != ICE_INACTIVE || con->servers || con->rc)
			return false;	//was already doing something else...

		if (!strncmp(value, "dtls://", 7))
		{
#ifdef HAVE_DTLS
			char *a = strchr((value+=7), '?');
			char *e;
			if (a) *a = 0;
			for (; a; a = e)
			{
				a++;	//skip the '?' or '&'
				e = strchr(a, '&');
				if (!strncmp(a, "fp=", 3))
				{
					int l = Base64_DecodeBlock(a+3, e, con->cred.peer.digest, sizeof(con->cred.peer.digest));
					if (l == hash_sha2_512.digestsize)
						con->cred.peer.hash = &hash_sha2_512;
					else if (l == hash_sha2_256.digestsize)
						con->cred.peer.hash = &hash_sha2_256;
					else if (l == hash_sha1.digestsize)
						con->cred.peer.hash = &hash_sha1;
					else
					{
						Con_Printf(CON_ERROR"Unsupported fingerprint length\n");
						return false;
					}
				}
			}

			con->dtlsfuncs = ICE_DTLS_InitClient();	//credentials are a bit different, though fingerprints make it somewhat irrelevant.
			if (!con->dtlsfuncs)
				return false;	//oops.
			con->dtlsstate = con->dtlsfuncs->CreateContext(&con->cred, con, ICE_Transmit, con->dtlspassive);
			if (!con->dtlsstate)
#endif
				return false;
		}
/*		else if (!strncmp(value, "tls://", 6) || !strncmp(value, "tcp://", 6))
			//link = qizmo framing?
			return false;*/
		else if (!strncmp(value, "wss://", 6) || !strncmp(value, "ws://", 5))
		{
			qboolean sec = (value[2]=='s');
			value += 5+sec;
			if (!NET_StringToAdr(value, 0, &con->chosenpeer, 1))
				return false;
			link = ICE_WSS_EstablishConnection(value, &con->chosenpeer, sec);
			if (!link)
				return false;
		}
		else if (!strncmp(value, "udp://", 6))
			value += 6;
		else if (strstr(value, "://"))
			return false;	//some unknown scheme.

		if (!link && !NET_StringToAdr(value, 0, &con->chosenpeer, 1))
			return false;

		con->brokerless = true;
		con->state = ICE_CONNECTED;
		con->icetimeout = Sys_Milliseconds() + (con->icetimeout_ms ? con->icetimeout_ms : 30*1000);
		if (link)
		{
			con->servers = 1;
			con->server[0].con = link;
			con->chosenpeer.connum = con->server[0].connum = 1+MAX_NETWORKS+countof(con->server)+0;	//send through our private socket instead of wrapping in TURN.
			con->server[0].addr = con->chosenpeer;
		}

		//so we match up inbound packets properly.
		con->rc = calloc(1, sizeof(*con->rc));
		con->rc->peer = con->chosenpeer;
		NET_BaseAdrToString(con->rc->info.addr, sizeof(con->rc->info.addr), &con->chosenpeer);
		con->rc->info.port = NET_AdrToPort(&con->chosenpeer);
		//if (net_from.connum >= 1 && net_from.connum < 1+MAX_NETWORKS && col->conn[net_from.connum-1])
		//	col->conn[net_from.connum-1]->GetLocalAddresses(col->conn[net_from.connum-1], &relflags, &reladdr, &relpath, 1);
		//FIXME: we don't really know which one... NET_BaseAdrToString(rc->info.reladdr, sizeof(rc->info.reladdr), &reladdr);
		//rc->info.relport = ntohs(reladdr.port);
		con->rc->info.type = ICE_HOST;
		con->rc->dirty = true;
		con->rc->info.priority = 0;
	}
	else
		return false;
	return true;
}
void ICE_SetUserPtr(struct icestate_s *con, void *value)
{
	con->userptr = value;
}
void *ICE_GetUserPtr(struct icestate_s *con)
{
	return con->userptr;
}
qboolean ICE_SetFailed(struct icestate_s *con, const char *reasonfmt, ...)
{
	va_list		argptr;
	char		string[256];

	va_start (argptr, reasonfmt);
	q_vsnprintf (string,sizeof(string)-1, reasonfmt,argptr);
	va_end (argptr);

	if (con->state == ICE_FAILED)
		Con_Printf(S_COLOR_GRAY"[%s]: %s\n", con->friendlyname, string);	//we were probably already expecting this. don't show it as a warning.
	else
		Con_Printf(CON_WARNING"[%s]: %s\n", con->friendlyname, string);
	return ICE_Set(con, "state", STRINGIFY(ICE_FAILED));	//does the disconnection stuff.
}
static char *ICE_CandidateToSDP(struct icestate_s *ice, struct icecandidate_s *can, char *value, size_t valuelen)
{
	q_snprintf(value, valuelen, "a=candidate:%i %i %s %i %s %i typ %s",
			can->info.foundation,
			can->info.component,
			can->info.transport==0?"UDP":"ERROR",
			can->info.priority,
			can->info.addr,
			can->info.port,
			ICE_GetCandidateType(&can->info)
			);
	if (can->info.generation)
		q_strlcat(value, va(" generation %i", can->info.generation), valuelen);	//firefox doesn't like this.
	if (can->info.type != ICE_HOST)
	{
		if (ice->modeflags & ICEF_RELAY_ONLY)
		{	//don't leak srflx info (technically this info is mandatory)
			q_strlcat(value, va(" raddr %s", can->info.addr), valuelen);
			q_strlcat(value, va(" rport %i", can->info.port), valuelen);
		}
		else
		{
			if (*can->info.reladdr)
				q_strlcat(value, va(" raddr %s", can->info.reladdr), valuelen);
			else
				q_strlcat(value, " raddr 0.0.0.0", valuelen);
			q_strlcat(value, va(" rport %i", can->info.relport), valuelen);
		}
	}

	return value;
}
static qboolean ICE_LCandidateIsPrivate(struct icestate_s *ice, struct icecandidate_s *caninfo)
{	//return true for the local candidates that we're actually allowed to report. they'll stay flagged as 'dirty' otherwise.
	if (!(ice->modeflags&ICEF_SHARE_PRIVATE) && caninfo->info.type == ICE_HOST && !caninfo->ismdns)
		return true;
	if ((ice->modeflags&ICEF_RELAY_ONLY) && caninfo->info.type != ICE_RELAY)
		return true;
	return false;
}
const char *ICE_GetConnName(struct icestate_s *ice)
{
	if (ice && ice->friendlyname)
		return ice->friendlyname;
	return "?";
}
static qboolean ICE_Get(struct icestate_s *con, const char *prop, char *value, size_t valuelen)
{
	if (!strcmp(prop, "name"))
		q_strlcpy(value, con->friendlyname, valuelen);
	else if (!strcmp(prop, "sid"))
		q_strlcpy(value, con->conname, valuelen);
	else if (!strcmp(prop, "state"))
	{
		switch(con->state)
		{
		case ICE_INACTIVE:
			q_strlcpy(value, STRINGIFY(ICE_INACTIVE), valuelen);
			break;
		case ICE_FAILED:
			q_strlcpy(value, STRINGIFY(ICE_FAILED), valuelen);
			break;
		case ICE_GATHERING:
			q_strlcpy(value, STRINGIFY(ICE_GATHERING), valuelen);
			break;
		case ICE_CONNECTING:
			q_strlcpy(value, STRINGIFY(ICE_CONNECTING), valuelen);
			break;
		case ICE_CONNECTED:
			q_strlcpy(value, STRINGIFY(ICE_CONNECTED), valuelen);
			break;
		}
	}
	else if (!strcmp(prop, "lufrag"))
		q_strlcpy(value, con->lufrag, valuelen);
	else if (!strcmp(prop, "lpwd"))
		q_strlcpy(value, con->lpwd, valuelen);
	else if (!strncmp(prop, "codec", 5))
	{
		int codecid = atoi(prop+5);
		struct icecodecslot_s *codec = ICE_GetCodecSlot(con, atoi(prop+5));
		if (!codec || codec->id != codecid)
			return false;
		if (codec->name)
			q_strlcpy(value, codec->name, valuelen);
		else
			q_strlcpy(value, "", valuelen);
	}
	else if (!strcmp(prop, "newlc"))
	{
		struct icecandidate_s *can;
		q_strlcpy(value, "0", valuelen);
		for (can = con->lc; can; can = can->next)
		{
			if (can->dirty && !ICE_LCandidateIsPrivate(con, can))
			{
				q_strlcpy(value, "1", valuelen);
				break;
			}
		}
	}
	else if (!strcmp(prop, "peersdp"))
	{	//for debugging.
		q_strlcpy(value, "", valuelen);
		if ((con->proto == ICEP_SERVER || con->proto == ICEP_CLIENT) && con->modeflags & ICEF_ALLOW_WEBRTC)
		{
#ifdef HAVE_DTLS
			if (con->cred.peer.hash)
			{
				int b;
				q_strlcat(value, "a=fingerprint:", valuelen);
				for (b = 0; b < countof(webrtc_hashes); b++)
				{
					if (con->cred.peer.hash == webrtc_hashes[b].hash)
						break;
				}
				q_strlcat(value, (b==countof(webrtc_hashes))?"UNKNOWN":webrtc_hashes[b].name, valuelen);
				for (b = 0; b < con->cred.peer.hash->digestsize; b++)
					q_strlcat(value, va(b?":%02X":" %02X", con->cred.peer.digest[b]), valuelen);
				q_strlcat(value, "\n", valuelen);
			}
#endif
		}
		q_strlcat(value, va("a=ice-pwd:%s\n", con->rpwd), valuelen);
		q_strlcat(value, va("a=ice-ufrag:%s\n", con->rufrag), valuelen);

#ifdef HAVE_SCTP
		if (con->peersctpport)
			q_strlcat(value, va("a=sctp-port:%i\n", con->peersctpport), valuelen);	//stupid hardcoded thing.
		if (con->peersctpoptional)
			q_strlcat(value, "a=sctp-optional:1\n", valuelen);
#endif
	}
	else if (!strcmp(prop, "sdp") || !strcmp(prop, "sdpoffer") || !strcmp(prop, "sdpanswer"))
	{
		struct icecandidate_s *can;
		netadr_t sender;
		char tmpstr[64], *at;
		int i;

		{
			netadr_t		addr[1];
			int				networks[countof(addr)];
			unsigned int	flags[countof(addr)];
			const char		*params[countof(addr)];

			if (!ICE_EnumerateAddresses(con->module, networks, flags, addr, params, countof(addr)))
			{
				memset(&sender, 0, sizeof(sender));
				sender.type = NA_INVALID;
			}
			else
				sender = *addr;
		}

		q_strlcpy(value, "v=0\n", valuelen);	//version...
		q_strlcat(value, va("o=%s %u %u IN IP4 %s\n", "-", con->originid, con->originversion, con->originaddress), valuelen);	//originator. usually just dummy info.
		q_strlcat(value, va("s=%s\n", con->conname), valuelen);	//session name.
		q_strlcat(value, "t=0 0\n", valuelen);	//start+end times...
		q_strlcat(value, va("a=ice-options:trickle\n"), valuelen);

		if (con->proto == ICEP_SERVER || con->proto == ICEP_CLIENT)
		{
#ifdef HAVE_DTLS
			if (!(con->modeflags & ICEF_ALLOW_PLAIN))
			{	//this is a preliminary check to avoid wasting time
				if (!con->cred.local.certsize)
					return false;	//fail if we cannot do dtls when its required.
				if (!strcmp(prop, "sdpanswer") && !con->cred.peer.hash)
					return false;	//don't answer if they failed to provide a cert
			}
			if (con->cred.local.certsize)
			{
				qbyte fingerprint[DIGEST_MAXSIZE];
				int b;
				hashfunc_t *hash = &hash_sha2_256;	//browsers use sha-256, lets match them.
				CalcHash(hash, fingerprint, sizeof(fingerprint), con->cred.local.cert, con->cred.local.certsize);
				q_strlcat(value, "a=fingerprint:sha-256", valuelen);
				for (b = 0; b < hash->digestsize; b++)
					q_strlcat(value, va(b?":%02X":" %02X", fingerprint[b]), valuelen);
				q_strlcat(value, "\n", valuelen);

#ifdef HAVE_SCTP
				if (con->modeflags & ICEF_ALLOW_WEBRTC)
				{
					q_strlcat(value, "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\n", valuelen);
					if (con->mysctpport)
						q_strlcat(value, va("a=sctp-port:%i\n", con->mysctpport), valuelen);	//stupid hardcoded thing.
					if (con->sctpoptional)
						q_strlcat(value, "a=sctp-optional:1\n", valuelen);
				}
				else
#endif
					q_strlcat(value, "m=application 9 UDP/DTLS ice-data\n", valuelen);
			}
			else
			{
#ifdef HAVE_SCTP
				if (con->modeflags & ICEF_ALLOW_WEBRTC)
					q_strlcat(value, "m=application 9 UDP/SCTP ice-datachannel\n", valuelen);
				else
#endif
					q_strlcat(value, "m=application 9 UDP ice-data\n", valuelen);
			}
#endif
		}
//		q_strlcat(value, va("c=IN %s %s\n", sender.type==NA_IPV6?"IP6":"IP4", NET_BaseAdrToString(tmpstr, sizeof(tmpstr), &sender)), valuelen);
		q_strlcat(value, "c=IN IP4 0.0.0.0\n", valuelen);

		for (can = con->lc; can; can = can->next)
		{
			char canline[256];
			if (ICE_LCandidateIsPrivate(con, can))
				continue;	//ignore it.
			can->dirty = false;	//doesn't matter now.
			ICE_CandidateToSDP(con, can, canline, sizeof(canline));
			q_strlcat(value, canline, valuelen);
			q_strlcat(value, "\n", valuelen);
		}

		q_strlcat(value, va("a=ice-pwd:%s\n", con->lpwd), valuelen);
		q_strlcat(value, va("a=ice-ufrag:%s\n", con->lufrag), valuelen);

#ifdef HAVE_DTLS
		if (con->dtlsfuncs)
		{
			if (!strcmp(prop, "sdpanswer"))
			{	//answerer decides.
				if (con->dtlspassive)
					q_strlcat(value, va("a=setup:passive\n"), valuelen);
				else
					q_strlcat(value, va("a=setup:active\n"), valuelen);
			}
			else if (!strcmp(prop, "sdpoffer"))
				q_strlcat(value, va("a=setup:actpass\n"), valuelen);	//don't care if we're active or passive
		}
#endif

		/*fixme: merge the codecs into a single media line*/
		for (i = 0; i < countof(con->codecslot); i++)
		{
			int id = con->codecslot[i].id;
			if (!con->codecslot[i].name)
				continue;

			q_strlcat(value, va("m=audio %i RTP/AVP %i\n", NET_AdrToPort(&sender), id), valuelen);
			q_strlcat(value, va("b=RS:0\n"), valuelen);
			q_strlcat(value, va("b=RR:0\n"), valuelen);
			q_strlcpy(tmpstr, con->codecslot[i].name, sizeof(tmpstr));
			at = strchr(tmpstr, '@');
			if (at)
			{
				*at = '/';
				q_strlcat(value, va("a=rtpmap:%i %s\n", id, tmpstr), valuelen);
			}
			else
				q_strlcat(value, va("a=rtpmap:%i %s/%i\n", id, tmpstr, 8000), valuelen);

			for (can = con->lc; can; can = can->next)
			{
				char canline[256];
				can->dirty = false;	//doesn't matter now.
				ICE_CandidateToSDP(con, can, canline, sizeof(canline));
				q_strlcat(value, canline, valuelen);
				q_strlcat(value, "\n", valuelen);
			}
		}
	}

	else if (!strcmp(prop, "status"))
	{
		int i, c;
		*value = 0;
		switch(con->state)
		{
		case ICE_INACTIVE:
			q_strlcpy(value, "idle", valuelen);
			break;
		case ICE_FAILED:
			q_strlcpy(value, "Failed", valuelen);
			break;
		case ICE_GATHERING:
			q_strlcpy(value, "Gathering", valuelen);
			break;
		case ICE_CONNECTING:
			for (i = 0, c = false; i < con->servers; i++)
				if (!con->server[i].isstun)
				{
					if (con->server[i].state == TURN_ALLOCATED)
						break;
					c = true;
				}
			if (i == con->servers)
			{
				if (con->modeflags & ICEF_RELAY_ONLY)
					q_strlcpy(value, "Probing ("CON_ERROR"NO TURN SERVER"CON_DEFAULT")", valuelen);	//can't work, might still get an allocation though.
				else if (c)
					q_strlcpy(value, "Probing ("CON_WARNING"waiting for TURN allocation"CON_DEFAULT")", valuelen);	//still good for latency. not for privacy though.
				else
					q_strlcpy(value, "Probing ("CON_WARNING"no relay configured"CON_DEFAULT")", valuelen);	//still good for latency. not for privacy though.
			}
			else
				q_strlcpy(value, "Probing ("S_COLOR_GREEN"with fallback"CON_DEFAULT")", valuelen);	//we have a relay for a fallback, all is good, hopefully. except we're still at this stage...
			break;
		case ICE_CONNECTED:	//past the ICE stage (but maybe not the dtls+sctp layers, these should be less likely to fail, but dtls versions may become an issue)
			//if (con->dtlsstate && notokay) {} else
#if 0//def HAVE_SCTP
			if (con->sctp && !con->sctp->o.writable)
				q_strlcpy(out, "Establishing", valuelen);	//will also block for the dtls channel of course. its not as easy check the dtls layer.
			else
#endif
				q_strlcpy(value, "Established", valuelen);
			break;
		}
	}
	else if (!strcmp(prop, "encrypted"))
	{
#ifdef HAVE_DTLS
		if (con->dtlsstate)
			q_strlcpy(value, "1", valuelen);
		else
#endif
			q_strlcpy(value, "0", valuelen);
	}
	else
		return false;
	return true;
}

/*static void ICE_PrintSummary(struct icestate_s *con, qboolean islisten)
{
	char msg[64];

	Con_Printf(S_COLOR_GRAY" ^[[%s]\\ice\\%s^]: ", con->friendlyname, con->friendlyname);
	switch(con->proto)
	{
	case ICEP_VOICE:	Con_Printf(S_COLOR_GRAY"(voice) "); break;
	case ICEP_VIDEO:	Con_Printf(S_COLOR_GRAY"(video) "); break;
	default:			break;
	}
	switch(con->state)
	{
	case ICE_INACTIVE:		Con_Printf(S_COLOR_RED "inactive"); break;
	case ICE_FAILED:		Con_Printf(S_COLOR_RED "failed"); break;
	case ICE_GATHERING:		Con_Printf(S_COLOR_YELLOW "gathering"); break;
	case ICE_CONNECTING:	Con_Printf(S_COLOR_YELLOW "connecting"); break;
	case ICE_CONNECTED:		Con_Printf(S_COLOR_GRAY"%s via %s", NET_AdrToString(msg,sizeof(msg), &con->chosenpeer), ICE_NetworkToName(con, con->chosenpeer.connum)); break;
	}
#ifdef HAVE_DTLS
	if (con->dtlsstate)
	{
#ifdef HAVE_SCTP
		Con_Printf(S_COLOR_GREEN " (encrypted%s)", con->sctp?", sctp":"");
#else
		Con_Printf(S_COLOR_GREEN " (encrypted)");
#endif
	}
	else
#endif
#ifdef HAVE_SCTP
	if (con->sctp)
		Con_Printf(S_COLOR_RED " (plain-text, sctp)");	//weeeeeeird and pointless...
	else
#endif
		Con_Printf(S_COLOR_RED " (plain-text)");
	Con_Printf("\n");
}*/
void ICE_Debug(struct icestate_s *con)
{
	const char *addrclass;
	struct icecandidate_s *can;
	char buf[65536];
	int i;
	if (!con)
	{	//recurse and show all that are registered.
		for (con = icelist; con; con = con->next)
			ICE_Debug(con);
		return;
	}

	ICE_Get(con, "state", buf, sizeof(buf));
	Con_Printf("ICE [%s] (%s):\n", con->friendlyname, buf);
	if (con->brokerless)
		Con_Printf(" timeout: %g\n", (int)(con->icetimeout-Sys_Milliseconds())/1000.0);
	else
	{
		unsigned int idle = (Sys_Milliseconds()+30*1000 - con->icetimeout);
		if (idle > 500)
			Con_Printf(" idle: %g\n", idle/1000.0);
	}
	if (con->modeflags & ICEF_VERBOSE_PROBE)
	{	//rather uninteresting really...
		if (con->initiator)
			ICE_Get(con, "sdpoffer", buf, sizeof(buf));
		else
			ICE_Get(con, "sdpanswer", buf, sizeof(buf));
		Con_Printf("sdp:\n"S_COLOR_YELLOW "%s\n", buf);

		//incomplete anyway
		ICE_Get(con, "peersdp", buf, sizeof(buf));
		Con_Printf("peer:\n"S_COLOR_YELLOW"%s\n", buf);
	}

	Con_Printf(" servers:\n");
	for (i = 0; i < con->servers; i++)
	{
		const char *status = "?";
		if (con->server[i].isstun)
			status = "";
		else switch(con->server[i].state)
		{
		case TURN_UNINITED:		status = "uninited";	break;
		case TURN_HAVE_NONCE:	status = "registering";	break;
		case TURN_ALLOCATED:	status = "allocated";	break;
		case TURN_TERMINATING:	status = "terminating";	break;
		}
		NET_AdrToString(buf,sizeof(buf), &con->server[i].addr);
		Con_Printf("  %s:%s %s realm=%s user=%s auth=%s\n", con->server[i].isstun?"stun":"turn", buf, status, con->server[i].realm, con->server[i].user?con->server[i].user:"<unspecified>", con->server[i].auth?"<hidden>":"<none>");
	}

	Con_Printf(" local:\n");
	for (can = con->lc; can; can = can->next)
	{
		ICE_CandidateToSDP(con, can, buf, sizeof(buf));
		if (con->chosenpeer.type!=NA_INVALID && con->chosenpeer.connum == can->info.network)
			Con_Printf(S_COLOR_GREEN "  %s"S_COLOR_GRAY" <chosen>\n", buf);
		else if (can->dirty)
			Con_Printf(S_COLOR_RED   "  %s"S_COLOR_GRAY" <not sent>\n", buf);
		else
			Con_Printf(S_COLOR_YELLOW"  %s\n", buf);
	}

	Con_Printf(" remote:\n");
	for (can = con->rc; can; can = can->next)
	{
		ICE_CandidateToSDP(con, can, buf, sizeof(buf));
		if (can->reachable)
		{
			if (con->chosenpeer.type!=NA_INVALID && NET_CompareAdr(&can->peer,&con->chosenpeer))
				Con_Printf(S_COLOR_GREEN "  %s"S_COLOR_GRAY" <chosen>\n", buf);
			else
				Con_Printf(S_COLOR_YELLOW"  %s"S_COLOR_GRAY" <reachable>\n", buf);
		}
		else if (NET_ClassifyAddress(&can->peer, &addrclass) < ASCOPE_TURN_REQUIRESCOPE)
			Con_Printf(S_COLOR_RED"  %s"S_COLOR_GRAY" <ignored: %s>\n", buf, addrclass);
		else
			Con_Printf(S_COLOR_RED"  %s"S_COLOR_GRAY" <unreachable>\n", buf);
	}
}
static struct icecandinfo_s *ICE_GetLCandidateInfo(struct icestate_s *con)
{
	struct icecandidate_s *can;
	for (can = con->lc; can; can = can->next)
	{
		if (can->dirty)
		{
			if (ICE_LCandidateIsPrivate(con, can))
				continue;

			can->dirty = false;
			return &can->info;
		}
	}
	return NULL;
}

static qboolean ICE_GetLCandidateSDP(struct icestate_s *con, char *out, size_t outsize)
{
	struct icecandinfo_s *info;
	if (con->blockcandidates)	//don't report any candidates until we're up and running.
		return false;
	info = ICE_GetLCandidateInfo(con);
	if (info)
	{
		struct icecandidate_s *can = (struct icecandidate_s*)info;
		ICE_CandidateToSDP(con, can, out, outsize);
		return true;
	}
	return false;
}

static unsigned int ICE_ComputePriority(netadr_t *adr, struct icecandinfo_s *info)
{
	int tpref, lpref;
	switch(info->type)
	{
	case ICE_HOST:	tpref = 126;	break;	//ideal
	case ICE_PRFLX: tpref = 110;	break;
	case ICE_SRFLX: tpref = 100;	break;
	default:
	case ICE_RELAY:	tpref = 0;	break;	//relays suck
	}
	lpref = 0;
	if (info->transport == 0)
		lpref += 0x8000;	//favour udp the most (tcp sucks for stalls)
	lpref += (255-info->network)<<16;	//favour the first network/socket specified
	lpref += (255-NET_ClassifyAddress(adr, NULL))<<8;
	if (adr->type == NA_IP)
		lpref += 0x0001;	//favour ipv4 over ipv6 (less header/mtu overhead...). this is only slight,

	return (tpref<<24) + (lpref<<lpref) + ((256 - info->component)<<0);
}

//adrno is 0 if the type is anything but host.
static void ICE_AddLCandidateInfo(struct icestate_s *con, netadr_t *adr, int type)
{
	int rnd[2];
	struct icecandidate_s *cand;
	if (!con)
		return;

	switch(adr->type)
	{
	case NA_IP:
	case NA_IPV6:
		switch(NET_ClassifyAddress(adr, NULL))
		{
		case ASCOPE_PROCESS://doesn't make sense.
		case ASCOPE_HOST:	//don't waste time asking the relay to poke its loopback. if only because it'll report lots of errors.
			return;
		case ASCOPE_NET:	//public addresses, just add local candidates as normal
			break;
		case ASCOPE_LINK:	//random screwy addresses... hopefully don't need em if we're talking to a broker... no dhcp server is weird.
			return;
		case ASCOPE_LAN:	//private addresses. give them random info instead...
			if ((con->modeflags & ICEF_ALLOW_MDNS) && MDNS_Setup(con->module))
			{
				for (cand = con->lc; cand; cand = cand->next)
				{
					if (cand->ismdns)
						return;	//DUPE
				}

				cand = calloc(1, sizeof(*cand));
				cand->next = con->lc;
				con->lc = cand;
				q_strlcpy(cand->info.addr, con->module->mdns_name, sizeof(cand->info.addr));
				cand->info.port = NET_AdrToPort(adr);
				cand->info.type = type;
				cand->info.generation = 0;
				cand->info.foundation = 1;
				cand->info.component = 1;
				cand->info.network = adr->connum;
				cand->dirty = true;
				cand->ismdns = true;

				Sys_RandomBytes((void*)rnd, sizeof(rnd));
				q_strlcpy(cand->info.candidateid, va("x%08x%08x", rnd[0], rnd[1]), sizeof(cand->info.candidateid));

				cand->info.priority = ICE_ComputePriority(adr, &cand->info);
				return;
			}
			break;
		}
		break;
	default:	//bad protocols
		return;	//no, just no.
	}
	switch(adr->prot)
	{
	case NP_DTLS:
	case NP_DGRAM:
		break;
	default:
		return;	//don't report any tcp/etc connections...
	}

	for (cand = con->lc; cand; cand = cand->next)
	{
		if (NET_CompareAdr(adr, &cand->peer))
			return; //DUPE
	}

	cand = calloc(1, sizeof(*cand));
	cand->next = con->lc;
	con->lc = cand;
	NET_BaseAdrToString(cand->info.addr, sizeof(cand->info.addr), adr);
	cand->info.port = NET_AdrToPort(adr);
	cand->info.type = type;
	cand->info.generation = 0;
	cand->info.foundation = 1;
	cand->info.component = 1;
	cand->info.network = adr->connum;
	cand->dirty = true;

	Sys_RandomBytes((void*)rnd, sizeof(rnd));
	q_strlcpy(cand->info.candidateid, va("x%08x%08x", rnd[0], rnd[1]), sizeof(cand->info.candidateid));

	cand->info.priority = ICE_ComputePriority(adr, &cand->info);
}

static void ICE_Destroy(struct icestate_s *con)
{
	struct icecandidate_s *c;

	if (con->module->ClosedState)
		con->module->ClosedState(con->module, con);

	ICE_Set(con, "state", STRINGIFY(ICE_INACTIVE));

	MDNS_RemoveQueries(con);	//clear any lingering references.

#ifdef HAVE_SCTP
	if (con->sctp)
		SCTP_Destroy(con->sctp);
#endif
#ifdef HAVE_DTLS
	if (con->dtlsstate)
		con->dtlsfuncs->DestroyContext(con->dtlsstate);
	if (con->cred.local.cert)
		free(con->cred.local.cert);
	if (con->cred.local.key)
		free(con->cred.local.key);
#endif
	while(con->rc)
	{
		c = con->rc;
		con->rc = c->next;
		free(c);
	}
	while(con->lc)
	{
		c = con->lc;
		con->lc = c->next;
		free(c);
	}
	while (con->servers)
	{
		struct iceserver_s *s = &con->server[--con->servers];
		if (s->con)
		{	//make sure we tell the TURN server to release our allocation.
			s->state = TURN_TERMINATING;
			ICE_ToStunServer(con, s);

			s->con->CloseSocket(s->con);
		}
		free(s->user);
		free(s->auth);
		free(s->realm);
		free(s->nonce);
	}
	free(con->lufrag);
	free(con->lpwd);
	free(con->rufrag);
	free(con->rpwd);
	free(con->friendlyname);
	free(con->conname);
	//has already been unlinked

#if 1
	memset(con, 0xfe, sizeof(*con));
#endif
	free(con);

	icedestroyed = true; //break out of any loops that were reading packets from sockets that might no longer be alive.
}

//send pings to establish/keep the connection alive
void ICE_Tick(void)
{
	struct icestate_s **link, *con;
	unsigned int curtime;
	size_t i, j;
	struct iceserver_s *srv;

	if (!icelist)
		return;
	curtime = Sys_Milliseconds();

	icedestroyed = false;

	MDNS_SendQueries();

	for (link = &icelist; (con=*link);)
	{
		if (con->brokerless)
		{
			if (con->state <= ICE_GATHERING || con->state == ICE_FAILED)
			{
				*link = con->next;
				ICE_Destroy(con);
				continue;
			}
			else if ((signed int)(curtime-con->icetimeout) > 0)
			{
				Con_DPrintf(S_COLOR_GRAY"[%s]: brokerless ice timeout\n", con->friendlyname);
				con->state = ICE_FAILED;	//will be destroyed on next tick
			}
		}

		if (!(con->modeflags & ICEF_ALLOW_PROBE))
		{
			//raw doesn't do handshakes or keepalives. it should just directly connect.
			//raw just uses the first (assumed only) option
			if (con->state == ICE_CONNECTING)
			{
				struct icecandidate_s *rc;
				rc = con->rc;
				if (!rc || !NET_StringToAdr(rc->info.addr, rc->info.port, &con->chosenpeer, 1))
					con->chosenpeer.type = NA_INVALID;
				ICE_Set(con, "state", STRINGIFY(ICE_CONNECTED));
			}

			for (i = 0; i < con->servers; i++)
			{
				srv = &con->server[i];
				if (srv->con)
				{
					char buf[8192];
					int msgsize;
					netadr_t from;
					while ((msgsize = srv->con->RecvPacket(srv->con, &from, buf, sizeof(buf)))>0)
					{
						from.connum = srv->connum;
						ICE_ProcessPacket (con->module, srv->con, &from, buf, msgsize);
						if (icedestroyed)
							return;	//something got destroyed while we processed that packet. don't crash.
					}
				}
			}
		}
		else
		{
			if (con->state == ICE_GATHERING || con->state == ICE_CONNECTING || con->state == ICE_CONNECTED)
			{
				for (i = 0; i < con->servers; i++)
				{
					srv = &con->server[i];
					if ((signed int)(srv->stunretry-curtime) < 0)
					{
						if (srv->isstun && ICE_UseCachedSrflx(con, srv))
							srv->stunretry = curtime + 60*1000;
						else
						{
							srv->stunretry = curtime + 2*1000;
							ICE_ToStunServer(con, srv);
						}
					}
#ifdef HAVE_TURN
					for (j = 0; j < srv->peers; j++)
					{
						if ((signed int)(srv->peer[j].retry-curtime) < 0)
						{
							TURN_AuthorisePeer(con, srv, j);
							srv->peer[j].retry = curtime + 2*1000;
						}
					}
					if (srv->con)
					{
						char buf[8192];
						int msgsize;
						netadr_t from;
						while ((msgsize = srv->con->RecvPacket(srv->con, &from, buf, sizeof(buf)))>0)
						{
							from.connum = srv->connum;
							ICE_ProcessPacket (con->module, srv->con, &from, buf, msgsize);
							if (icedestroyed)
								return;	//something got destroyed while we processed that packet. don't crash.
						}
					}
#endif
				}
				if (con->keepalive < curtime && (con->state == ICE_CONNECTING || con->state == ICE_CONNECTED))
				{
					if (!ICE_SendSpam(con))
					{
						struct icecandidate_s *rc;
						struct icecandidate_s *best = NULL;

						for (rc = con->rc; rc; rc = rc->next)
						{	//FIXME:
							if (rc->reachable && (!best || rc->info.priority > best->info.priority))
								best = rc;
						}

						if (best)
						{
							netadr_t nb = best->peer;
							for (i = 0; ; i++)
							{
								if (best->reachable&(1<<i))
								{
									best->tried &= ~(1<<i);	//keep poking it...
									nb.connum = i+1;
									break;
								}
							}
							if (!NET_CompareAdr(&con->chosenpeer, &nb) && (con->chosenpeer.type==NA_INVALID || !con->controlled))
							{	//it actually changed... let them know NOW.
								best->tried &= ~(1<<(con->chosenpeer.connum-1));	//make sure we resend this one.
								con->chosenpeer = nb;
								ICE_SendSpam(con);

								if (con->modeflags & ICEF_VERBOSE)
								{
									char msg[64];
									Con_Printf(S_COLOR_GRAY"[%s]: New peer chosen %s (%s), via %s.\n", con->friendlyname, NET_AdrToString(msg, sizeof(msg), &con->chosenpeer), ICE_GetCandidateType(&best->info), ICE_NetworkToName(con, con->chosenpeer.connum));
								}
							}
						}
						if (con->state == ICE_CONNECTED && best)
						{	//once established, only re-probe reachable candidates
							for (rc = con->rc; rc; rc = rc->next)
								rc->tried = (rc->tried | rc->noroute) & ~rc->reachable;
							con->keepalive = curtime + 5*1000;
						}
						else
						{
							for (rc = con->rc; rc; rc = rc->next)
								rc->tried = rc->noroute;	//preserve permanently unreachable networks

							con->retries++;
							if (con->retries > 32)
								con->retries = 32;
							con->keepalive = curtime + 200*(con->retries);	//RTO... ish.
						}
					}
					else
					{
						if (con->state == ICE_CONNECTED)
							con->keepalive = curtime + 5*1000;	//connected keepalive
						else
							con->keepalive = curtime + 50*(con->retries+1);	//Ta... absmin of 5ms
					}
				}
			}
			if (con->state == ICE_CONNECTED)
			{
#ifdef HAVE_SCTP
				if (con->sctp)
					SCTP_Transmit(con->sctp, NULL,0);	//try to keep it ticking...
#endif
#ifdef HAVE_DTLS
				if (con->dtlsstate)
					con->dtlsfuncs->Timeouts(con->dtlsstate);
#endif

				//FIXME: We should be sending a stun binding indication every 15 secs with a fingerprint attribute
			}
		}

		link = &con->next;
	}
}
static void ICE_CloseState(struct icestate_s *con, qboolean force)
{
	struct icestate_s **link;

	for (link = &icelist; *link; )
	{
		if (con == *link)
		{
			if (!force)
				con->brokerless = true;
			else
			{
				*link = con->next;
				ICE_Destroy(con);
			}
			return;
		}
		else
			link = &(*link)->next;
	}
}
static void ICE_CloseModule(struct icemodule_s *module)
{
	int i;
	struct icestate_s **link, *con;

	for (link = &icelist; *link; )
	{
		con = *link;
		if (con->module == module)
		{
			*link = con->next;
			ICE_Destroy(con);
		}
		else
			link = &(*link)->next;
	}

	for (i = 0; i < countof(module->conn); i++)
		if (module->conn[i])
		{
			module->conn[i]->CloseSocket(module->conn[i]);
			module->conn[i] = NULL;
		}
}

static struct icestate_s *ICE_DirectConnectedInternal(struct icemodule_s *module, struct icesocket_s *link, netadr_t *adr, void *dtlsstate)
{
	struct icestate_s *con;
	char peer[128];
	q_strlcpy(peer, dtlsstate?"dtls://":"", sizeof(peer));
	NET_AdrToString(peer+strlen(peer),sizeof(peer)-strlen(peer), adr);
	con = ICE_Create(module, NULL, peer, 0, ICEP_SERVER);

	con->chosenpeer = *adr;

#ifdef HAVE_DTLS
	if (dtlsstate)
	{
		con->dtlsfuncs = module->dtlsfuncs;
		con->dtlsstate = dtlsstate;
		Con_Printf("%s: Direct DTLS connection\n", peer);
	}
	else
#endif
	if (link)
	{
		con->servers = 1;
		con->server[0].con = link;
		con->chosenpeer.connum = con->server[0].connum = 1+MAX_NETWORKS+countof(con->server)+0;	//send through our private socket instead of wrapping in TURN.
		con->server[0].addr = con->chosenpeer;
		Con_DPrintf("%s: WebSocket connection\n", peer);
	}
	else
		Con_DPrintf("%s: Direct connection\n", peer);

	con->brokerless = true;
	con->icetimeout_ms = 5*1000;	//short timeout — game connections survive via data flow extending it
	con->state = ICE_CONNECTED;
	con->icetimeout = Sys_Milliseconds() + con->icetimeout_ms;

	//so we match up inbound packets properly.
	con->rc = calloc(1, sizeof(*con->rc));
	con->rc->peer = con->chosenpeer;
	NET_BaseAdrToString(con->rc->info.addr, sizeof(con->rc->info.addr), &con->chosenpeer);
	con->rc->info.port = NET_AdrToPort(&con->chosenpeer);
	//if (net_from.connum >= 1 && net_from.connum < 1+MAX_NETWORKS && col->conn[net_from.connum-1])
	//	col->conn[net_from.connum-1]->GetLocalAddresses(col->conn[net_from.connum-1], &relflags, &reladdr, &relpath, 1);
	//FIXME: we don't really know which one... NET_BaseAdrToString(rc->info.reladdr, sizeof(rc->info.reladdr), &reladdr);
	//rc->info.relport = ntohs(reladdr.port);
	con->rc->info.type = ICE_HOST;
	con->rc->dirty = true;
	con->rc->info.priority = 0;

	return con;
}
static struct icestate_s *ICE_DirectConnected(struct icestate_s *fake)
{
	return ICE_DirectConnectedInternal(fake->module, NULL, &fake->chosenpeer, NULL);
}
static void ICE_DirectStreamConnected(struct icemodule_s *module, struct icesocket_s *link, netadr_t *adr)
{
	ICE_DirectConnectedInternal(module, link, adr, NULL);
}
#ifdef HAVE_DTLS
static void ICE_DirectDTLSConnected(void **cbctx, void *state)
{	//a dtls client appears to have tried to connect to one of our listening sockets, outside of any brokers.
	struct icestate_s *fake = *cbctx;	//preliminary state.
	*cbctx = ICE_DirectConnectedInternal(fake->module, NULL, &fake->chosenpeer, state);
}
#endif
void ICE_ProcessModule(struct icemodule_s *module)
{
	static struct icestate_s ice;
	char buffer[65536];
	int i;
	struct icesocket_s *s;
	int sz;
	netadr_t adr;

	for (i = 0; i < countof(module->conn); i++)
	{
		s = module->conn[i];
		if (!s)
			continue;
		for(;;)
		{
			if (s->CheckAccept)
			{
				s->CheckAccept(s, module, ICE_DirectStreamConnected);
				break;
			}
			memset(&adr, 0, sizeof(adr));
			sz = s->RecvPacket(s, &adr, buffer, sizeof(buffer));
			if (sz <= 0)
				break;
			adr.connum = 1+i;
			if (!iceapi.ProcessPacket(module, s, &adr, buffer, sz))
			{	//we can get old packets from disconnecting clients which were not otherwise connected to some ice state.
				//just ignore them all here. if you want to handle unsolicited messages then there's a reason ProcessModule and ProcessPacket are separate.

				//check to see if its a dtls challenge/connect and establish a new connection if so.
				ice.chosenpeer = adr;
				ice.module = module;
				ice.state = ICE_CONNECTED;

#ifdef HAVE_DTLS
				//our dtls driver should be able to check if its a dtls hello/handshake and handle the challenges before wasting memory on an actual connection
				if (module->dtlsfuncs)
				{
					ice.dtlsfuncs = module->dtlsfuncs;
					if (module->dtlsfuncs->CheckConnection(&ice, &adr, sizeof(adr), buffer, sz, ICE_Transmit, ICE_DirectDTLSConnected))
						continue;	//was a dtls handshake packet of some form
				}
				ice.dtlsfuncs = NULL;
#endif

				//see if the game wants to handle it.
				if (module->ReadUnsolicitedPacket)
				{
					module->ReadUnsolicitedPacket(&ice, buffer, sz, ICE_DirectConnected);
					continue;
				}

				Con_DPrintf("Stray packet\n");
			}
		}
	}

	ICE_Tick();	//send keepalives etc.
}

static qboolean ICE_ProcessPacket (struct icemodule_s *module, struct icesocket_s *net_from_connection, netadr_t *from, void *msg_data, size_t msg_size)	//could be stun, turn, sctp, dtls, etc. also has to figure out which peer its from.
{
#if defined(HAVE_CLIENT) && defined(VOICECHAT)
	if (col == cls_sockets)
	{
		if (NET_RTP_Parse())
			return true;
	}
#endif

	if ((from->type == NA_IP || from->type == NA_IPV6) && msg_size >= 20 && *(qbyte*)msg_data<2)
	{
		stunhdr_t *stun = (stunhdr_t*)msg_data;
		int stunlen = BigShort(stun->msglen);
#ifdef SUPPORT_ICE
		if ((stun->msgtype == BigShort(STUN_BINDING|STUN_REPLY) || stun->msgtype == BigShort(STUN_BINDING|STUN_ERROR)) && msg_size == stunlen + sizeof(*stun))
		{
			//binding reply (or error)
			//either from stun service or peer.
			netadr_t adr = *from;
			char xor[16];
			short portxor;
			stunattr_t *attr = (stunattr_t*)(stun+1);
			int alen;
			unsigned short attrval;
			int err = 0;
			char errmsg[64];
			*errmsg = 0;

			adr.type = NA_INVALID;
			while(stunlen)
			{
				stunlen -= sizeof(*attr);
				alen = (unsigned short)BigShort(attr->attrlen);
				if (alen > stunlen)
					return false;
				stunlen -= (alen+3)&~3;
				attrval = BigShort(attr->attrtype);
				switch(attrval)
				{
				case STUNATTR_USERNAME:
				case STUNATTR_MSGINTEGRITIY_SHA1:
					break;
				default:
					if (attrval & 0x8000)
						break;	//okay to ignore
					return true;
				case STUNATTR_MAPPED_ADDRESS:
					if (adr.type != NA_INVALID)
						break;	//ignore it if we already got an address...
				//fallthrough
				case STUNATTR_XOR_MAPPED_ADDRESS:
					if (attrval == STUNATTR_XOR_MAPPED_ADDRESS)
					{
						portxor = stun->portxor;
						memcpy(xor, &stun->magiccookie, sizeof(xor));
					}
					else
					{
						portxor = 0;
						memset(xor, 0, sizeof(xor));
					}
					if (alen == 8 && ((qbyte*)attr)[5] == 1)		//ipv4
					{
						adr.type = NA_IP;
						adr.in.sin_family = AF_INET;
						adr.in.sin_port = (((short*)attr)[3]) ^ portxor;
						*(int*)&adr.in.sin_addr = *(int*)(&((qbyte*)attr)[8]) ^ *(int*)xor;
					}
					else if (alen == 20 && ((qbyte*)attr)[5] == 2)	//ipv6
					{
						adr.type = NA_IPV6;
						adr.in6.sin6_family = AF_INET6;
						adr.in6.sin6_port = (((short*)attr)[3]) ^ portxor;
						((int*)&adr.in6.sin6_addr)[0] = ((int*)&((qbyte*)attr)[8])[0] ^ ((int*)xor)[0];
						((int*)&adr.in6.sin6_addr)[1] = ((int*)&((qbyte*)attr)[8])[1] ^ ((int*)xor)[1];
						((int*)&adr.in6.sin6_addr)[2] = ((int*)&((qbyte*)attr)[8])[2] ^ ((int*)xor)[2];
						((int*)&adr.in6.sin6_addr)[3] = ((int*)&((qbyte*)attr)[8])[3] ^ ((int*)xor)[3];
					}
					break;
				case STUNATTR_ERROR_CODE:
					{
						unsigned short len = BigShort(attr->attrlen)-4;
						if (len > sizeof(errmsg)-1)
							len = sizeof(errmsg)-1;
						memcpy(errmsg, &((qbyte*)attr)[8], len);
						errmsg[len] = 0;
						if (err==0)
							err = (((qbyte*)attr)[6]*100) + (((qbyte*)attr)[7]%100);
					}
					break;
				}
				alen = (alen+3)&~3;
				attr = (stunattr_t*)((char*)(attr+1) + alen);
			}

			if (err)
			{
				char sender[256];
//				if (con->modeflags & ICEF_VERBOSE)
					Con_DPrintf("%s: Stun error code %u : %s\n", NET_AdrToString(sender, sizeof(sender), from), err, errmsg);
			}
			else if (adr.type!=NA_INVALID && !err)
			{
				struct icestate_s *con;
				for (con = icelist; con; con = con->next)
				{
					struct icecandidate_s *rc;
					size_t i;
					struct iceserver_s *s;
					if (!(con->modeflags & ICEF_ALLOW_PROBE))
						continue;

					for (i = 0; i < con->servers; i++)
					{
						s = &con->server[i];
						if (NET_CompareAdr(from, &s->addr) &&	s->stunrnd[0] == stun->transactid[0] &&
																	s->stunrnd[1] == stun->transactid[1] &&
																	s->stunrnd[2] == stun->transactid[2])
						{	//check to see if this is a new server-reflexive address, which happens when the peer is behind a nat.
							for (rc = con->lc; rc; rc = rc->next)
							{
								if (NET_CompareAdr(&adr, &rc->peer))
									break;
							}
							if (!rc)
							{
								//netadr_t reladdr;
								//int relflags;
								//const char *relpath;
								int rnd[2];
								struct icecandidate_s *src;	//server Reflexive Candidate
								char str[256];
								src = calloc(1, sizeof(*src));
								src->next = con->lc;
								con->lc = src;
								src->peer = adr;
								NET_BaseAdrToString(src->info.addr, sizeof(src->info.addr), &adr);
								//use the module's srflx_port override if set (e.g. game port
								//behind Docker/symmetric NAT where STUN reflects an ephemeral port)
								src->info.port = con->module->srflx_port ? con->module->srflx_port : NET_AdrToPort(&adr);
								//if (net_from.connum >= 1 && net_from.connum < 1+MAX_NETWORKS && col->conn[net_from.connum-1])
								//	col->conn[net_from.connum-1]->GetLocalAddresses(col->conn[net_from.connum-1], &relflags, &reladdr, &relpath, 1);
								//FIXME: we don't really know which one... NET_BaseAdrToString(src->info.reladdr, sizeof(src->info.reladdr), &reladdr);
								//src->info.relport = ntohs(reladdr.port);
								src->info.type = ICE_SRFLX;
								src->info.component = 1;
								src->info.network = from->connum;
								src->dirty = true;
								src->info.priority = ICE_ComputePriority(&src->peer, &src->info);	//FIXME

								Sys_RandomBytes((void*)rnd, sizeof(rnd));
								q_strlcpy(src->info.candidateid, va("x%08x%08x", rnd[0], rnd[1]), sizeof(src->info.candidateid));

								if (con->modeflags & ICEF_VERBOSE)
									Con_Printf(S_COLOR_GRAY"[%s]: Public address: %s:%d\n", con->friendlyname, src->info.addr, src->info.port);
							}
							s->stunretry = Sys_Milliseconds() + 60*1000;
							con->module->srflx[adr.type!=NA_IP] = adr;
							return true;
						}
					}

					//check to see if this is a new peer-reflexive address, which happens when the peer is behind a nat.
					for (rc = con->rc; rc; rc = rc->next)
					{
						if (NET_CompareAdr(from, &rc->peer))
						{
							if (!(rc->reachable & (1u<<(from->connum-1))))
							{
								char str[256];
								if (con->modeflags & ICEF_VERBOSE)
									Con_Printf(S_COLOR_GRAY"[%s]: We can reach %s (%s) via %s\n", con->friendlyname, NET_AdrToString(str, sizeof(str), from), ICE_GetCandidateType(&rc->info), ICE_NetworkToName(con, from->connum));
							}
							rc->reachable |= 1u<<(from->connum-1);
							rc->reached = Sys_Milliseconds();

							if (NET_CompareAdr(from, &con->chosenpeer) && (stun->transactid[2] & BigLong(0x80000000)))
							{
								if (con->state == ICE_CONNECTING)
									ICE_Set(con, "state", STRINGIFY(ICE_CONNECTED));
							}
							return true;
						}
					}
				}

				//only accept actual responses, not spoofed stuff.
				/*if (stun->magiccookie == BigLong(0x2112a442)
					&& stun->transactid[0]==module->srflx_tid[0]
					&& stun->transactid[1]==module->srflx_tid[1]
					&& stun->transactid[2]==module->srflx_tid[2]
					&& !NET_CompareAdr(&module->srflx[adr.type!=NA_IP], &adr))
				{
					if (module->srflx[adr.type!=NA_IP].type==NA_INVALID)
						Con_Printf(S_COLOR_GRAY"Public address reported as %s\n", NET_AdrToString(errmsg, sizeof(errmsg), &adr));
					else
						Con_Printf(CON_ERROR"Server reflexive address changed to %s\n", NET_AdrToString(errmsg, sizeof(errmsg), &adr));
					module->srflx[adr.type!=NA_IP] = adr;
				}*/
			}
			return true;
		}
		else if (stun->msgtype == BigShort(STUN_BINDING|STUN_INDICATION))// && msg_size == stunlen + sizeof(*stun) && stun->magiccookie == BigLong(0x2112a442))
		{
			//binding indication. used as an rtp keepalive. should have a fingerprint
			return true;
		}
#ifdef HAVE_TURN
		else if (stun->msgtype == BigShort(STUN_DATA|STUN_INDICATION)
				 && msg_size == stunlen + sizeof(*stun) && stun->magiccookie == BigLong(STUN_MAGIC_COOKIE))
		{
			//TURN relayed data
			//these MUST come from a _known_ turn server.
			netadr_t adr;
			char xor[16];
			short portxor;
			void *data = NULL;
			unsigned short datasize = 0;
			unsigned short attrval;
			stunattr_t *attr = (stunattr_t*)(stun+1);
			int alen;
			unsigned int network = from->connum-1;	//also net_from_connection->connum
			struct icestate_s *con;

			if (network < MAX_NETWORKS)
				return true;	//don't handle this if its on the non-turn sockets.
			network -= MAX_NETWORKS;
			if (network < countof(con->server))
				return true; //don't double-decapsulate...
			network -= countof(con->server);

			for (con = icelist; con; con = con->next)
			{
				if (network < con->servers && net_from_connection == con->server[network].con)
					break;
			}
			if (!con)
				return true;	//don't know what it was. just ignore it.
			if (network >= con->servers || !NET_CompareAdr(from, &con->server[network].addr))
				return true;	//right socket, but not from the server that we expected...

			adr.type = NA_INVALID;
			while(stunlen>0)
			{
				stunlen -= sizeof(*attr);
				alen = (unsigned short)BigShort(attr->attrlen);
				if (alen > stunlen)
					return false;
				stunlen -= (alen+3)&~3;
				attrval = BigShort(attr->attrtype);
				switch(attrval)
				{
				default:
					if (attrval & 0x8000)
						break;	//okay to ignore
					return true;
				case STUNATTR_DATA:
					data = attr+1;
					datasize = alen;
					break;
				case STUNATTR_XOR_PEER_ADDRESS:
					//always xor
					portxor = stun->portxor;
					memcpy(xor, &stun->magiccookie, sizeof(xor));

					adr.prot = NP_DGRAM;
					adr.connum = from->connum;
					if (alen == 8 && ((qbyte*)attr)[5] == 1)		//ipv4
					{
						adr.type = NA_IP;
						adr.in.sin_family = AF_INET;
						adr.in.sin_port = (((short*)attr)[3]) ^ portxor;
						*(int*)&adr.in.sin_addr = *(int*)(&((qbyte*)attr)[8]) ^ *(int*)xor;
					}
					else if (alen == 20 && ((qbyte*)attr)[5] == 2)	//ipv6
					{
						adr.type = NA_IPV6;
						adr.in6.sin6_family = AF_INET6;
						adr.in6.sin6_port = (((short*)attr)[3]) ^ portxor;
						((int*)&adr.in6.sin6_addr)[0] = ((int*)&((qbyte*)attr)[8])[0] ^ ((int*)xor)[0];
						((int*)&adr.in6.sin6_addr)[1] = ((int*)&((qbyte*)attr)[8])[1] ^ ((int*)xor)[1];
						((int*)&adr.in6.sin6_addr)[2] = ((int*)&((qbyte*)attr)[8])[2] ^ ((int*)xor)[2];
						((int*)&adr.in6.sin6_addr)[3] = ((int*)&((qbyte*)attr)[8])[3] ^ ((int*)xor)[3];
						adr.in6.sin6_scope_id = from->in6.sin6_scope_id;
					}
					break;
				}
				alen = (alen+3)&~3;
				attr = (stunattr_t*)((char*)(attr+1) + alen);
			}
			if (data)
			{
//				adr.connum = net_from.connum-countof(con->server);	//came via the relay.
//				net_from = adr;
				con->module->ReadGamePacket(con, data, datasize);
				return true;
			}
		}
#endif
#ifdef HAVE_TURN
		else if ((stun->msgtype == BigShort(STUN_CREATEPERM|STUN_REPLY) || stun->msgtype == BigShort(STUN_CREATEPERM|STUN_ERROR))
				 && msg_size == stunlen + sizeof(*stun) && stun->magiccookie == BigLong(STUN_MAGIC_COOKIE))
		{
			//TURN CreatePermissions reply (or error)
			unsigned short attrval;
			stunattr_t *attr = (stunattr_t*)(stun+1), *nonce=NULL, *realm=NULL;
			int alen;
			struct iceserver_s *s = NULL;
			int i, j;
			struct icestate_s *con;
			char errmsg[128];
			int err = 0;
			*errmsg = 0;

			//make sure it makes sense.
			while(stunlen>0)
			{
				stunlen -= sizeof(*attr);
				alen = (unsigned short)BigShort(attr->attrlen);
				if (alen > stunlen)
					return false;
				stunlen -= (alen+3)&~3;
				attrval = BigShort(attr->attrtype);
				switch(attrval)
				{
				default:
					if (attrval & 0x8000)
						break;	//okay to ignore
					return true;
				case STUNATTR_NONCE:
					nonce = attr;
					break;
				case STUNATTR_REALM:
					realm = attr;
					break;
				case STUNATTR_ERROR_CODE:
					{
						unsigned short len = BigShort(attr->attrlen)-4;
						if (len > sizeof(errmsg)-1)
							len = sizeof(errmsg)-1;
						memcpy(errmsg, &((qbyte*)attr)[8], len);
						errmsg[len] = 0;
						if (err==0)
							err = (((qbyte*)attr)[6]*100) + (((qbyte*)attr)[7]%100);
					}
					break;
				case STUNATTR_MSGINTEGRITIY_SHA1:
					break;
				}
				alen = (alen+3)&~3;
				attr = (stunattr_t*)((char*)(attr+1) + alen);
			}

			//now figure out what it acked.
			for (con = icelist; con; con = con->next)
			{
				for (i = 0; i < con->servers; i++)
				{
					s = &con->server[i];
					if (NET_CompareAdr(from, &s->addr))
						for (j = 0; j < s->peers; j++)
						{
							if (s->peer[j].stunrnd[0] == stun->transactid[0] && s->peer[j].stunrnd[1] == stun->transactid[1] && s->peer[j].stunrnd[2] == stun->transactid[2])
							{	//the lifetime of a permission is a fixed 5 mins (this is separately from the port allocation)
								unsigned int now = Sys_Milliseconds();

								if (err)
								{
									if (err == 438 && realm && nonce)
									{
										alen = BigShort(nonce->attrlen);
										free(s->nonce);
										s->nonce = calloc(1, alen+1);
										memcpy(s->nonce, nonce+1, alen);
										s->nonce[alen] = 0;

										alen = BigShort(realm->attrlen);
										free(s->realm);
										s->realm = calloc(1, alen+1);
										memcpy(s->realm, realm+1, alen);
										s->realm[alen] = 0;

										s->peer[j].retry = now;	//retry fast.
									}
								}
								else
								{
									now -= 25;	//we don't know when it acked, so lets pretend we're a few MS ago.
									s->peer[j].expires = now + 5*60*1000;
									s->peer[j].retry = now + 4*60*1000;	//start trying to refresh it a min early (which will do resends).
								}

								//next attempt will use a different id.
								Sys_RandomBytes((qbyte*)s->peer[i].stunrnd, sizeof(s->peer[i].stunrnd));
								return true;
							}
						}
				}
				if (i < con->servers)
					break;
			}

			return true;
		}
#endif
#ifdef HAVE_TURN
		else if ((stun->msgtype == BigShort(STUN_ALLOCATE|STUN_REPLY) || stun->msgtype == BigShort(STUN_ALLOCATE|STUN_ERROR)||
				 (stun->msgtype == BigShort(STUN_REFRESH|STUN_REPLY) || stun->msgtype == BigShort(STUN_REFRESH|STUN_ERROR)))
				 && msg_size == stunlen + sizeof(*stun) && stun->magiccookie == BigLong(STUN_MAGIC_COOKIE))
		{
			//TURN allocate reply (or error)
			netadr_t adrs[2], ladr, *adr;	//the last should be our own ip.
			char xor[16];
			short portxor;
			unsigned short attrval;
			stunattr_t *attr = (stunattr_t*)(stun+1);
			int alen;
			int err = 0;
			char errmsg[64];
			struct iceserver_s *s = NULL;
			int i;
			struct icestate_s *con;
			qboolean noncechanged = false;
			unsigned int lifetime = 0;

			//gotta have come from our private socket.
			unsigned int network = from->connum-1;	//also net_from_connection->connum
			if (network < MAX_NETWORKS)
				return true;	//don't handle this if its on the non-turn sockets.
			network -= MAX_NETWORKS;
			if (network < countof(con->server))
				return true; //TURN-over-TURN is bad...
			network -= countof(con->server);

			for (con = icelist; con; con = con->next)
			{
				if (network < con->servers && net_from_connection == con->server[network].con)
				{
					s = &con->server[network];
					if (s->stunrnd[0] == stun->transactid[0] && s->stunrnd[1] == stun->transactid[1] && s->stunrnd[2] == stun->transactid[2] && NET_CompareAdr(from, &s->addr))
						break;
					if (con->modeflags & ICEF_VERBOSE)
						Con_Printf(S_COLOR_GRAY"Stale transaction id (got %x, expected %x)\n", stun->transactid[0], s->stunrnd[0]);
				}
			}
			if (!con)
				return true;	//don't know what it was. just ignore it.

			network += 1 + MAX_NETWORKS;	//fix it up to refer to the relay rather than the private socket.

			adrs[0].type = NA_INVALID;
			adrs[1].type = NA_INVALID;
			ladr.type = NA_INVALID;

			while(stunlen>0)
			{
				stunlen -= sizeof(*attr);
				alen = (unsigned short)BigShort(attr->attrlen);
				if (alen > stunlen)
					return false;
				stunlen -= (alen+3)&~3;
				attrval = BigShort(attr->attrtype);
				switch(attrval)
				{
				case STUNATTR_LIFETIME:
					if (alen >= 4)
						lifetime = BigLong(*(int*)(attr+1));
					break;
//				case STUNATTR_SOFTWARE:
				case STUNATTR_MSGINTEGRITIY_SHA1:
//				case STUNATTR_FINGERPRINT:
					break;
				default:
					if (attrval & 0x8000)
						break;	//okay to ignore
					err = -1;	//got an attribute we 'must' handle...
					return true;
				case STUNATTR_NONCE:
					free(s->nonce);
					s->nonce = calloc(1, alen+1);
					memcpy(s->nonce, attr+1, alen);
					s->nonce[alen] = 0;
					noncechanged = true;
					break;
				case STUNATTR_REALM:
					free(s->realm);
					s->realm = calloc(1, alen+1);
					memcpy(s->realm, attr+1, alen);
					s->realm[alen] = 0;
					break;
				case STUNATTR_XOR_RELAYED_ADDRESS:
				case STUNATTR_XOR_MAPPED_ADDRESS:
					if (BigShort(attr->attrtype) == STUNATTR_XOR_MAPPED_ADDRESS)
						adr = &ladr;
					else
						adr = adrs[0].type?&adrs[1]:&adrs[0];
					//always xor
					portxor = stun->portxor;
					memcpy(xor, &stun->magiccookie, sizeof(xor));

					memset(adr, 0, sizeof(*adr));
					adr->prot = NP_DGRAM;
					adr->connum = from->connum;
					if (alen == 8 && ((qbyte*)attr)[5] == 1)		//ipv4
					{
						adr->type = NA_IP;
						adr->in.sin_family = AF_INET;
						adr->in.sin_port = (((short*)attr)[3]) ^ portxor;
						*(int*)&adr->in.sin_addr = *(int*)(&((qbyte*)attr)[8]) ^ *(int*)xor;
					}
					else if (alen == 20 && ((qbyte*)attr)[5] == 2)	//ipv6
					{
						adr->type = NA_IPV6;
						adr->in6.sin6_family = AF_INET6;
						adr->in6.sin6_port = (((short*)attr)[3]) ^ portxor;
						((int*)&adr->in6.sin6_addr)[0] = ((int*)&((qbyte*)attr)[8])[0] ^ ((int*)xor)[0];
						((int*)&adr->in6.sin6_addr)[1] = ((int*)&((qbyte*)attr)[8])[1] ^ ((int*)xor)[1];
						((int*)&adr->in6.sin6_addr)[2] = ((int*)&((qbyte*)attr)[8])[2] ^ ((int*)xor)[2];
						((int*)&adr->in6.sin6_addr)[3] = ((int*)&((qbyte*)attr)[8])[3] ^ ((int*)xor)[3];
						adr->in6.sin6_scope_id = from->in6.sin6_scope_id;
					}
					break;
				case STUNATTR_ERROR_CODE:
					{
						unsigned short len = BigShort(attr->attrlen)-4;
						if (len > sizeof(errmsg)-1)
							len = sizeof(errmsg)-1;
						memcpy(errmsg, &((qbyte*)attr)[8], len);
						errmsg[len] = 0;
						if (!len)
							q_strlcpy(errmsg, "<no description>", len);
						if (err==0)
							err = (((qbyte*)attr)[6]*100) + (((qbyte*)attr)[7]%100);
					}
					break;
				}
				alen = (alen+3)&~3;
				attr = (stunattr_t*)((char*)(attr+1) + alen);
			}

			if (err)
			{
				char sender[256];

				if (err == 438/*stale nonce*/)
				{	//reset everything.
					s->state = noncechanged?TURN_HAVE_NONCE:TURN_UNINITED;
					s->stunretry = Sys_Milliseconds();

					if (con->modeflags & ICEF_VERBOSE)
						Con_Printf(S_COLOR_GRAY"[%s]: %s: TURN error code %u : %s\n", con->friendlyname, NET_AdrToString(sender, sizeof(sender), from), err, errmsg);
				}
				else if (err == 403/*forbidden*/)	//something bad...
				{
					s->state = TURN_UNINITED, s->stunretry = Sys_Milliseconds() + 60*1000;
					if (con->modeflags & ICEF_VERBOSE)
						Con_Printf(CON_ERROR"[%s]: %s: TURN error code %u : %s\n", con->friendlyname, NET_AdrToString(sender, sizeof(sender), from), err, errmsg);
				}
				else if (err == 401 && s->state == TURN_UNINITED && s->nonce)	//failure when sending auth... give up for a min
				{	//this happens from initial auth. we need to reply with the real auth request now.
					s->state = TURN_HAVE_NONCE, s->stunretry = Sys_Milliseconds();
				}
				else
				{
					s->stunretry = Sys_Milliseconds() + 60*1000;
//					if (con->modeflags & ICEF_VERBOSE)
						Con_Printf(CON_ERROR"[%s]: %s: TURN error code %u : %s\n", con->friendlyname, NET_AdrToString(sender, sizeof(sender), from), err, errmsg);
				}
			}
			else
			{
				struct icecandidate_s *lc;
				for (i = 0; i < countof(adrs); i++)
				{
					if (adrs[i].type != NA_INVALID && stun->msgtype == BigShort(STUN_ALLOCATE|STUN_REPLY))
					{
						s->state = TURN_ALLOCATED;

						if (!i)
							s->family = adrs[i].type;
						if (s->family != adrs[i].type)
							s->family = NA_INVALID;	//send it both types.

						if (ladr.type != NA_INVALID)
							adr = &ladr;	//can give a proper reflexive address
						else
							adr = &adrs[i];	//no info... give something.

						//check to see if this is a new server-reflexive address, which happens when the peer is behind a nat.
						for (lc = con->lc; lc; lc = lc->next)
						{
							if (NET_CompareAdr(&adrs[i], &lc->peer))
								break;
						}
						if (!lc)
						{
							int rnd[2];
							struct icecandidate_s *src;	//server Reflexive Candidate
							char str[256];
							src = calloc(1, sizeof(*src));
							src->next = con->lc;
							con->lc = src;
							src->peer = adrs[i];
							NET_BaseAdrToString(src->info.addr, sizeof(src->info.addr), &adrs[i]);
							src->info.port = NET_AdrToPort(&adrs[i]);
							NET_BaseAdrToString(src->info.reladdr, sizeof(src->info.reladdr), adr);
							src->info.relport = NET_AdrToPort(adr);
							src->info.type = ICE_RELAY;
							src->info.component = 1;
							src->info.network = network;
							src->dirty = true;
							src->info.priority = ICE_ComputePriority(&adrs[i], &src->info);

							Sys_RandomBytes((void*)rnd, sizeof(rnd));
							q_strlcpy(src->info.candidateid, va("x%08x%08x", rnd[0], rnd[1]), sizeof(src->info.candidateid));

							if (con->modeflags & ICEF_VERBOSE)
								Con_Printf(S_COLOR_GRAY"[%s]: Relayed local candidate: %s\n", con->friendlyname, NET_AdrToString(str, sizeof(str), &adrs[i]));
						}
					}
				}
				if (lifetime < 60)	//don't spam reauth requests too often...
					lifetime = 60;
				s->stunretry = Sys_Milliseconds() + (lifetime-50)*1000;
				return true;
			}
			return true;
		}
#endif
			if (stun->msgtype == BigShort(STUN_BINDING|STUN_REQUEST) && msg_size == stunlen + sizeof(*stun) && stun->magiccookie == BigLong(STUN_MAGIC_COOKIE))
		{
			char username[256];
			qbyte integrity[20];
#ifdef SUPPORT_ICE
			struct icestate_s *con;
			int role = 0;
			unsigned int tiehigh = 0;
			unsigned int tielow = 0;
			qboolean usecandidate = false;
			unsigned int priority = 0;
			char *lpwd = NULL;
#endif
			char *integritypos = NULL;
			int error = 0;

			icebuf_t buf;
			qbyte data[512];
			int alen = 0, atype = 0, aport;
			qbyte *aip = NULL;
			int crc;

			//binding request
			stunattr_t *attr = (stunattr_t*)(stun+1);
			*username = 0;
			while(stunlen)
			{
				alen = (unsigned short)BigShort(attr->attrlen);
				if (alen+sizeof(*attr) > stunlen)
					return false;
				switch((unsigned short)BigShort(attr->attrtype))
				{				case 0xc057: /*'network cost'*/ break;
				default:
					//unknown attributes < 0x8000 are 'mandatory to parse', and such packets must be dropped in their entirety.
					//other ones are okay.
					if (!((unsigned short)BigShort(attr->attrtype) & 0x8000))
						return false;
					break;
				case STUNATTR_USERNAME:
					if (alen < sizeof(username))
					{
						memcpy(username, attr+1, alen);
						username[alen] = 0;
//						Con_Printf("Stun username = \"%s\"\n", username);
					}
					break;
				case STUNATTR_MSGINTEGRITIY_SHA1:
					memcpy(integrity, attr+1, sizeof(integrity));
					integritypos = (char*)(attr+1);
					break;
#ifdef SUPPORT_ICE
				case STUNATTR_ICE_PRIORITY:
//					Con_Printf("priority = \"%i\"\n", priority);
					priority = BigLong(*(int*)(attr+1));
					break;
				case STUNATTR_ICE_USE_CANDIDATE:
					usecandidate = true;
					break;
#endif
				case STUNATTR_FINGERPRINT:
//					Con_Printf("fingerprint = \"%08x\"\n", BigLong(*(int*)(attr+1)));
					break;
#ifdef SUPPORT_ICE
				case STUNATTR_ICE_CONTROLLED:
				case STUNATTR_ICE_CONTROLLING:
					role = (unsigned short)BigShort(attr->attrtype);
					tiehigh = BigLong(((int*)(attr+1))[0]);
					tielow = BigLong(((int*)(attr+1))[1]);
					break;
#endif
				}
				alen = (alen+3)&~3;
				attr = (stunattr_t*)((char*)(attr+1) + alen);
				stunlen -= alen+sizeof(*attr);
			}

#ifdef SUPPORT_ICE
			if (*username || integritypos)
			{
				//we need to know which connection its from in order to validate the integrity
				for (con = icelist; con; con = con->next)
				{
					if (!strcmp(va("%s:%s", con->lufrag, con->rufrag), username))
						break;
				}
				if (!con)
				{
//					if (con->modeflags & ICEF_VERBOSE_PROBE)
//						Con_Printf("Received STUN request from unknown user \"%s\"\n", username);
					return true;
				}
				/*else if (con->chosenpeer.type != NA_INVALID)
				{	//got one.
					if (!NET_CompareAdr(&net_from, &con->chosenpeer))
						return true;	//FIXME: we're too stupid to handle switching. pretend to be dead.
				}*/
				else if (con->state == ICE_INACTIVE)
					return true;	//bad timing
				else
				{
					struct icecandidate_s *rc;

					if (con->modeflags & ICEF_VERBOSE_PROBE)
						Con_Printf(S_COLOR_GRAY"[%s]: got binding request on %s from %s\n", con->friendlyname, ICE_NetworkToName(con, from->connum), NET_AdrToString(username,sizeof(username), from));

					if (integritypos)
					{
						qbyte key[20];
						//the hmac is a bit weird. the header length includes the integrity attribute's length, but the checksum doesn't even consider the attribute header.
						stun->msglen = BigShort(integritypos+sizeof(integrity) - (char*)stun - sizeof(*stun));
						CalcHMAC(&hash_sha1, key, sizeof(key), (qbyte*)stun, integritypos-4 - (char*)stun, (qbyte*)con->lpwd, strlen(con->lpwd));
						if (memcmp(key, integrity, sizeof(integrity)))
						{
							Con_DPrintf(CON_WARNING"Integrity is bad! needed %x got %x\n", *(int*)key, *(int*)integrity);
							return true;
						}
					}

					//check to see if this is a new peer-reflexive address, which happens when the peer is behind a nat.
					{	int rccount = 0;
					for (rc = con->rc; rc; rc = rc->next)
					{
						rccount++;
						if (NET_CompareAdr(from, &rc->peer))
							break;
					}
					if (!rc && rccount < 256)
					{
						//netadr_t reladdr;
						//int relflags;
						//const char *relpath;
						struct icecandidate_s *rc;
						rc = calloc(1, sizeof(*rc));
						rc->next = con->rc;
						con->rc = rc;

						rc->peer = *from;
						NET_BaseAdrToString(rc->info.addr, sizeof(rc->info.addr), from);
						rc->info.port = NET_AdrToPort(from);
						//if (net_from.connum >= 1 && net_from.connum < 1+MAX_NETWORKS && col->conn[net_from.connum-1])
						//	col->conn[net_from.connum-1]->GetLocalAddresses(col->conn[net_from.connum-1], &relflags, &reladdr, &relpath, 1);
						//FIXME: we don't really know which one... NET_BaseAdrToString(rc->info.reladdr, sizeof(rc->info.reladdr), &reladdr);
						//rc->info.relport = ntohs(reladdr.port);
						rc->info.type = ICE_PRFLX;
						rc->dirty = true;
						rc->info.priority = priority;
					}
					}

					//flip ice control role, if we're wrong.
					if (role && role != (con->controlled?STUNATTR_ICE_CONTROLLING:STUNATTR_ICE_CONTROLLED))
					{
						if (tiehigh == con->tiehigh && tielow == con->tielow)
						{
							Con_Printf("ICE: Evil loopback hack enabled\n");
							if (usecandidate)
							{
								if ((con->chosenpeer.connum != from->connum || !NET_CompareAdr(&con->chosenpeer, from)) && (con->modeflags & ICEF_VERBOSE))
								{
									char msg[64];
									if (con->chosenpeer.connum-1 >= MAX_NETWORKS)
										Con_Printf(S_COLOR_GRAY"[%s]: New peer imposed %s, via %s.\n", con->friendlyname, NET_AdrToString(msg, sizeof(msg), from), ICE_NetworkToName(con, con->chosenpeer.connum));
									else
										Con_Printf(S_COLOR_GRAY"[%s]: New peer imposed %s.\n", con->friendlyname, NET_AdrToString(msg, sizeof(msg), from));
								}
								con->chosenpeer = *from;

								if (con->state == ICE_CONNECTING)
									ICE_Set(con, "state", STRINGIFY(ICE_CONNECTED));
							}
						}
						else
						{
							con->controlled = (tiehigh > con->tiehigh) || (tiehigh == con->tiehigh && tielow > con->tielow);
							if (con->modeflags & ICEF_VERBOSE)
								Con_Printf(S_COLOR_GRAY"[%s]: role conflict detected. We should be %s\n", con->friendlyname, con->controlled?"controlled":"controlling");
							error = 87;
						}
					}
					else if (usecandidate && con->controlled)
					{
						//in the controlled role, we're connected once we're told the pair to use (by the usecandidate flag).
						//note that this 'nominates' candidate pairs, from which the highest priority is chosen.
						//so we just pick select the highest.
						//this is problematic, however, as we don't actually know the real priority that the peer thinks we'll nominate it with.

						if ((con->chosenpeer.connum != from->connum || !NET_CompareAdr(&con->chosenpeer, from)) && (con->modeflags & ICEF_VERBOSE))
						{
							char msg[64];
							Con_Printf(S_COLOR_GRAY"[%s]: New peer imposed %s, via %s.\n", con->friendlyname, NET_AdrToString(msg, sizeof(msg), from), ICE_NetworkToName(con, from->connum));
						}
						con->chosenpeer = *from;

						if (con->state == ICE_CONNECTING)
							ICE_Set(con, "state", STRINGIFY(ICE_CONNECTED));
					}
					lpwd = con->lpwd;
				}
			}//otherwise its just an ip check
			else
				con = NULL;
#else
			(void)integritypos;
#endif

			memset(&buf, 0, sizeof(buf));
			buf.maxsize = sizeof(data);
			buf.cursize = 0;
			buf.data = data;

			if (from->type == NA_IP)
			{
				alen = 4;
				atype = 1;
				aip = (qbyte*)&from->in.sin_addr;
				aport = ntohs(from->in6.sin6_port);
			}
			/*else if (net_from.type == NA_IPV6 &&
						!*(int*)&net_from.address.ip6[0] &&
						!*(int*)&net_from.address.ip6[4] &&
						!*(short*)&net_from.address.ip6[8] &&
						*(short*)&net_from.address.ip6[10] == (short)0xffff)
			{	//just because we use an ipv6 address for ipv4 internally doesn't mean we should tell the peer that they're on ipv6...
				alen = 4;
				atype = 1;
				aip = (qbyte*)&from->in6.sin_addr + 12;
				aport = ntohs(from->in6.sin6_port);
			}*/
			else if (from->type == NA_IPV6)
			{
				alen = 16;
				atype = 2;
				aip = (qbyte*)&from->in6.sin6_addr;
				aport = ntohs(from->in6.sin6_port);
			}
			else
			{
				alen = 0;
				atype = 0;
				aport = 0;
			}

//Con_DPrintf("STUN from %s\n", NET_AdrToString(data, sizeof(data), &net_from));

			ICE_WriteShort(&buf, BigShort(error?(STUN_BINDING|STUN_ERROR):(STUN_BINDING|STUN_REPLY)));
			ICE_WriteShort(&buf, BigShort(0));	//fill in later
			ICE_WriteLong(&buf, stun->magiccookie);
			ICE_WriteLong(&buf, stun->transactid[0]);
			ICE_WriteLong(&buf, stun->transactid[1]);
			ICE_WriteLong(&buf, stun->transactid[2]);

			if (error == 87)
			{
				char *txt = "Role Conflict";
				ICE_WriteShort(&buf, BigShort(STUNATTR_ERROR_CODE));
				ICE_WriteShort(&buf, BigShort(4 + strlen(txt)));
				ICE_WriteShort(&buf, 0);	//reserved
				ICE_WriteByte(&buf, 0);		//class
				ICE_WriteByte(&buf, error);	//code
				ICE_WriteData(&buf, txt, strlen(txt));	//readable
				while(buf.cursize&3)		//padding
					ICE_WriteChar(&buf, 0);
			}
			else if (1)
			{	//xor mapped
				int i;
				ICE_WriteShort(&buf, BigShort(STUNATTR_XOR_MAPPED_ADDRESS));
				ICE_WriteShort(&buf, BigShort(4+alen));
				ICE_WriteShort(&buf, BigShort(atype));
				ICE_WriteShort(&buf, aport ^ *(short*)(data+4));
				for (i = 0; i < alen; i++)
					ICE_WriteByte(&buf, aip[i] & ((qbyte*)data+4)[i]);
			}
			else
			{	//non-xor mapped
				ICE_WriteShort(&buf, BigShort(STUNATTR_MAPPED_ADDRESS));
				ICE_WriteShort(&buf, BigShort(4+alen));
				ICE_WriteShort(&buf, BigShort(atype));
				ICE_WriteShort(&buf, aport);
				ICE_WriteData(&buf, aip, alen);
			}

			ICE_WriteShort(&buf, BigShort(STUNATTR_USERNAME));	//USERNAME
			ICE_WriteShort(&buf, BigShort(strlen(username)));
			ICE_WriteData(&buf, username, strlen(username));
			while(buf.cursize&3)
				ICE_WriteChar(&buf, 0);

#ifdef SUPPORT_ICE
			if (lpwd)
			{
				//message integrity is a bit annoying
				data[2] = ((buf.cursize+4+hash_sha1.digestsize-20)>>8)&0xff;	//hashed header length is up to the end of the hmac attribute
				data[3] = ((buf.cursize+4+hash_sha1.digestsize-20)>>0)&0xff;
				//but the hash is to the start of the attribute's header
				CalcHMAC(&hash_sha1, integrity, sizeof(integrity), data, buf.cursize, (qbyte*)lpwd, strlen(lpwd));
				ICE_WriteShort(&buf, BigShort(STUNATTR_MSGINTEGRITIY_SHA1));
				ICE_WriteShort(&buf, BigShort(hash_sha1.digestsize));	//sha1 key length
				ICE_WriteData(&buf, integrity, hash_sha1.digestsize);	//integrity data
			}
#endif

			data[2] = ((buf.cursize+8-20)>>8)&0xff;	//dummy length
			data[3] = ((buf.cursize+8-20)>>0)&0xff;
			crc = crc32(0, data, buf.cursize)^0x5354554e;
			ICE_WriteShort(&buf, BigShort(STUNATTR_FINGERPRINT));	//FINGERPRINT
			ICE_WriteShort(&buf, BigShort(sizeof(crc)));
			ICE_WriteLong(&buf, BigLong(crc));

			data[2] = ((buf.cursize-20)>>8)&0xff;
			data[3] = ((buf.cursize-20)>>0)&0xff;

#ifdef SUPPORT_ICE
			if (con)
				TURN_Encapsulate(con, from, data, buf.cursize);
			else
#endif
				ICE_SendUDPPacket(module, from, data, buf.cursize);
			return true;
		}
	}


#ifdef SUPPORT_ICE
	{
		struct icestate_s *con;
		struct icecandidate_s *rc;
#ifdef HAVE_DTLS
		char buf[8192];
#endif
		for (con = icelist; con; con = con->next)
		{
			for (rc = con->rc; rc; rc = rc->next)
			{
				if (NET_CompareAdr(from, &rc->peer))
				{
				//	if (rc->reachable)
					{	//found it. fix up its source address to our ICE connection (so we don't have path-switching issues) and keep chugging along.
						con->icetimeout = Sys_Milliseconds() + (con->icetimeout_ms ? con->icetimeout_ms : 30*1000);

#ifdef HAVE_DTLS
						if (con->dtlsstate)
						{
							switch(con->dtlsfuncs->Received(con->dtlsstate, from, msg_data, msg_size, buf,sizeof(buf), &msg_size))
							{
							case NETERR_SENT:
								msg_data = buf;
								break;	//
							case NETERR_NOROUTE:
								return false;	//not a dtls packet at all. don't de-ICE it when we're meant to be using ICE.
							case NETERR_DISCONNECTED:	//dtls failure. ICE failed.
								ICE_SetFailed(con, "DTLS Terminated");
								return true;
							default: //some kind of failure decoding the dtls packet. drop it.
								return true;
							}
						}
#endif

#ifdef HAVE_SCTP
						if (con->sctp)
							SCTP_Decode(con->sctp, msg_data, msg_size, con->module->ReadGamePacket);
						else
#endif
						if (msg_size)
							con->module->ReadGamePacket(con, msg_data, msg_size);
						return true;
					}
				}
			}
		}
	}
#endif
#endif
	return false;
}

icefuncs_t iceapi =
{
	ICE_Create,
	ICE_Set,
	ICE_Get,
	ICE_GetLCandidateInfo,
	ICE_AddRCandidateInfo,
	ICE_CloseState,
	ICE_CloseModule,
	ICE_GetLCandidateSDP,
	ICE_Find,
	ICE_ProcessModule,
	ICE_ProcessPacket,
	ICE_SendPacket,
};
#endif


#if !defined(HAVE_GNUTLS) && !defined(HAVE_OPENSSL)
	#ifdef HAVE_DTLS
		const dtlsfuncs_t *ICE_DTLS_InitServer(void)
		{
			return NULL;
		}
		const dtlsfuncs_t *ICE_DTLS_InitClient(void)
		{
			return NULL;
		}
	#endif
	#ifdef HAVE_TLS
		icestream_t *ICE_OpenTLS(const char *hostname, icestream_t *source, qboolean isserver)
		{
			return NULL;
		}
	#endif
#endif
