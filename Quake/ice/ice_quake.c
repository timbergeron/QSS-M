/*
	this is the quake-specific part of our ice implementation. the parts that interface cvars and bits.
	also handles interfacing with the broker (ftemaster).
	provides an alternative to quake's loopback/dgram layer which allows us a little more time to complete the ice/turn/dtls/sctp state before quake's silly dgram code times out.

	sctp supports reliables which would allow higher reliable bandwidth... but our sctp implementation is
	a) optional.
	b) lacks code for reliables.
	FIXME: we really should be using that and ignoring any acks.

	we (embarassingly) need to reimplement various chunks of quake's net_dgram.c, in part for compat with FTE's code which benefits from extra handshakes (as well as for our sctp implementation's missing reliables).
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

#include "../quakedef.h"
#include "../arch_def.h"
#include "../net_sys.h"
#include "../net_defs.h"

#include "ice_private.h"
#include "ice_quake.h"

#define DGRAM_PROTOCOL_NAME "QUAKE" //combined with NET_PROTOCOL_VERSION. not to be confused with 'com_protocolname'.

//broker protocol (over websockets).
#define PORT_ICEBROKER	27950	//same as q3's master.
enum icemsgtype_s
{	//shared by rtcpeers+broker
	ICEMSG_PEERLOST=0,	//other side dropped connection
	ICEMSG_GREETING=1,	//master telling us our unique game name
	ICEMSG_NEWPEER=2,	//relay established, send an offer now.
	ICEMSG_OFFER=3,		//peer->peer - peer's offer or answer details
	ICEMSG_CANDIDATE=4,	//peer->peer - candidate updates. may arrive late as new ones are discovered.
	ICEMSG_ACCEPT=5,	//go go go (response from offer)
	ICEMSG_SERVERINFO=6,//server->broker (for advertising the server properly)
	ICEMSG_SERVERUPDATE=7,//broker->browser (for querying available server lists)
	ICEMSG_NAMEINUSE=8,	//requested resource is unavailable.
};

#define CVAR_NOTFROMSERVER 0 //FIXME
#define CVARFD(name,val,flags,desc) {name,val,flags}

static cvar_t net_ice_exchangeprivateips = CVARFD("net_ice_exchangeprivateips", "0", CVAR_NOTFROMSERVER, "Boolean. When set to 0, hides private IP addresses from your peers - only addresses determined from the other side of your router will be shared. You should only need to set this to 1 if mdns is unavailable.");
static cvar_t net_ice_allowstun = CVARFD("net_ice_allowstun", "1", CVAR_NOTFROMSERVER, "Boolean. When set to 0, prevents the use of stun to determine our public address (does not prevent connecting to our peer's server-reflexive candidates).");
static cvar_t net_ice_allowturn = CVARFD("net_ice_allowturn", "1", CVAR_NOTFROMSERVER, "Boolean. When set to 0, prevents registration of turn connections (does not prevent connecting to our peer's relay candidates).");
static cvar_t net_ice_allowmdns = CVARFD("net_ice_allowmdns", "1", CVAR_NOTFROMSERVER, "Boolean. When set to 0, prevents the use of multicast-dns to obtain candidates using random numbers instead of revealing private network info.");
static cvar_t net_ice_relayonly = CVARFD("net_ice_relayonly", "0", CVAR_NOTFROMSERVER, "Boolean. When set to 1, blocks reporting non-relay local candidates, does not attempt to connect to remote candidates other than via a relay.");
#ifdef HAVE_DTLS
static cvar_t net_ice_usewebrtc = CVARFD("net_ice_usewebrtc", "", CVAR_NOTFROMSERVER, "Use webrtc's extra overheads rather than simple ICE. This makes packets larger and is slower to connect, but is compatible with the web port.");
#endif
static cvar_t net_ice_servers = CVARFD("net_ice_servers", "", CVAR_NOTFROMSERVER, "A space-separated list of ICE servers, eg stun:host.example:3478 or turn:host.example:3478?user=foo?auth=blah");
static cvar_t net_ice_debug = CVARFD("net_ice_debug", "0", CVAR_NOTFROMSERVER, "0: Hide messy details.\n1: Show new candidates.\n2: Show each connectivity test.");
static cvar_t net_ice_broker = CVARFD("net_ice_broker", "master.frag-net.com:27950", CVAR_NOTFROMSERVER, "This is the default broker we attempt to connect through when using 'sv_port_rtc /foo' or 'connect /foo'.");
cvar_t sv_port_rtc = CVARFD("sv_port_rtc", "/", CVAR_NOTFROMSERVER, "This is the '/roomname' for your server to register as. When '/' will request the master assign one. Empty will disable registration.");
extern cvar_t com_protocolname;


//this is the clientside part of our custom accountless broker protocol
//basically just keeps the broker processing, but doesn't send/receive actual game packets.
//inbound messages can change ice connection states.
//clients only handle one connection. servers need to handle multiple
typedef struct {
	struct icemodule_s icemodule;

	//config state
	char brokername[64];	//dns name:port
	int brokerport;
	char gamename[64];		//what we're trying to register as/for with the broker
	qboolean isserver;
	qboolean issecure;		//connecting over tls only.

	//broker connection state
	icestream_t *broker;
	double reconnecttimeout;
	double heartbeat;	//timestamp for when to send the next heartbeat (for server browsers).
	qboolean error;

	//client state...
	struct icestate_s *ice;
	qsocket_t *qsock;
	int serverid;
	double dohandshake;	//timestamp of next ccreq_connect request

	//server state...
	struct
	{
		struct icestate_s *ice;
		qboolean isnew;
	} *clients;
	size_t numclients;
} qice_connection_t;
static void QICE_Close(qice_connection_t *b)
{
	qsocket_t *s;
	int cl;
	if (b->broker)
	{	//kill the websocket connection
		b->broker->Close(b->broker);
		b->broker = NULL;
	}

	for (cl = 0; cl < b->numclients; cl++)
		if (b->clients[cl].ice)
			iceapi.Close(b->clients[cl].ice, false);
	if (b->ice)
		iceapi.Close(b->ice, false);

	for (s = net_activeSockets; s; s = s->next)
		if (s->driverdata == b)
		{	//the icemodule is about to be destroyed. if there's any ice states still attached then make sure they're detatched.
			if (s->driverdata == b)
				iceapi.Close(s->driverdata2, true);
			s->driverdata = NULL;
		}
	iceapi.CloseModule(&b->icemodule);


	Z_Free(b->clients);
	Z_Free(b);
}

static int QICE_PrepareBrokerFrame(int icemsg, int cl, char *data)	//returns offset.
{
	data[0] = icemsg;
	data[1] = cl&0xff;
	data[2] = (cl>>8)&0xff;
	return 3;
}
static void QICE_SendBrokerFrame(qice_connection_t *b, const char *msg)
{	//call QICE_PrepareBrokerFrame first.
	size_t msgsize = 3 + strlen(msg+3);
	b->broker->WriteBytes(b->broker, msg, msgsize);
}

extern void Datagram_GenerateGetInfoString(char *out, size_t outsize);
static void QICE_Heartbeat(qice_connection_t *b)
{
	b->heartbeat = realtime+30;
	if (b->isserver)
	{	//let the broker know the current serverinfo details, so its available via https://$net_ice_broker/raw/$com_protocolname
		char info[2048];
		int ofs = QICE_PrepareBrokerFrame(ICEMSG_SERVERINFO, -1, info);
		Datagram_GenerateGetInfoString(info+ofs, sizeof(info)-ofs);
		QICE_SendBrokerFrame(b, info);
	}
}

//escape a string so that COM_Parse will give the same string.
//maximum expansion is strlen(string)*2+4 (includes null terminator)
const char *JSON_QuotedString(const char *string, char *buf, int buflen, qboolean omitquotes)
{
	const char *result = buf;
	//strings of the form \"foo" can contain c-style escapes, including for newlines etc.
	//it might be fancy to ALWAYS escape non-ascii chars too, but mneh
	if (!omitquotes)
	{
		*buf++ = '\\';	//prefix so the reader knows its a quoted string.
		*buf++ = '\"';	//opening quote
		buflen -= 4;
	}
	else
		buflen -= 1;
	while(*string && buflen >= 2)
	{
		switch(*string)
		{
		case '\n':
			*buf++ = '\\';
			*buf++ = 'n';
			break;
		case '\r':
			*buf++ = '\\';
			*buf++ = 'r';
			break;
		case '\t':
			*buf++ = '\\';
			*buf++ = 't';
			break;
		case '\'':
			*buf++ = '\\';
			*buf++ = '\'';
			break;
		case '\"':
			*buf++ = '\\';
			*buf++ = '\"';
			break;
		case '\\':
			*buf++ = '\\';
			*buf++ = '\\';
			break;
		case '$':
			*buf++ = '\\';
			*buf++ = '$';
			break;
		default:
			*buf++ = *string++;
			buflen--;
			continue;
		}
		buflen -= 2;
		string++;
	}
	if (!omitquotes)
		*buf++ = '\"';	//closing quote
	*buf++ = 0;
	return result;
}
static void QICE_SendOffer(qice_connection_t *b, int cl, struct icestate_s *ice, const char *type)
{
	char buf[8192];
#if defined(HAVE_JSON) && defined(HAVE_DTLS)
	if (net_ice_usewebrtc.value || !*net_ice_usewebrtc.string)//if (ice->modeflags & ICEF_ALLOW_WEBRTC)
	{
		//okay, now send the sdp (encapsulated in json) to our peer.
		if (iceapi.Get(ice, type, buf, sizeof(buf)))
		{
			char json[8192+256];
			int ofs = QICE_PrepareBrokerFrame(ICEMSG_OFFER, cl, json);

			q_strlcpy(json+ofs, va("{\"type\":\"%s\",\"sdp\":\"", type+3), sizeof(json)-ofs);
			JSON_QuotedString(buf, json+ofs+strlen(json+ofs), sizeof(json)-ofs-strlen(json+ofs)-2, true);
			q_strlcat(json+ofs, "\"}", sizeof(json)-ofs);
			QICE_SendBrokerFrame(b, json);
		}
	}
	else
#endif
	{
		//okay, now send the sdp to our peer.
		int ofs = QICE_PrepareBrokerFrame(ICEMSG_OFFER, cl, buf);
		if (iceapi.Get(ice, type, buf+ofs, sizeof(buf)-ofs))
		{
			QICE_SendBrokerFrame(b, buf);
		}
	}
}
static void QICE_FoundPeer(qice_connection_t *b, const char *peeraddr, int cl, struct icestate_s **ret)
{	//sends offer
	struct icestate_s *ice;
	const char *s;
	unsigned int modeflags = 0;
	if (*ret)
		iceapi.Close(*ret, false);
#ifdef HAVE_DTLS
	if (net_ice_usewebrtc.value)
		modeflags |= ICEF_ALLOW_WEBRTC;
	else if (!*net_ice_usewebrtc.string)
		modeflags |= ICEF_ALLOW_WEBRTC|ICEF_ALLOW_PLAIN;	//let the peer decide. this means we can use dtls, but not sctp.
	else
		modeflags |= ICEF_ALLOW_PLAIN;
#endif
	if (!b->isserver)
		modeflags |= ICEF_INITIATOR;
	modeflags |= ICEF_ALLOW_PROBE;	//yes, we want to allow probes. don't be ice-lite.
	if (net_ice_allowstun.value)
		modeflags |= ICEF_ALLOW_STUN;	//query stun servers. relatively cheap.
	if (net_ice_allowturn.value)
		modeflags |= ICEF_ALLOW_TURN;	//register with a TURN server if defined.
	if (net_ice_allowmdns.value)
		modeflags |= ICEF_ALLOW_MDNS;	//share lan addresses without sharing lan addesses (queries and responses)
	if (net_ice_relayonly.value)
		modeflags |= ICEF_RELAY_ONLY;	//laggier
	if (net_ice_exchangeprivateips.value)
		modeflags |= ICEF_SHARE_PRIVATE;	//bad
	if (net_ice_debug.value)
		modeflags |= ICEF_VERBOSE;
	if (net_ice_debug.value>=2)
		modeflags |= ICEF_VERBOSE_PROBE;
	ice = *ret = iceapi.Create(&b->icemodule, NULL, b->isserver?((peeraddr&&*peeraddr)?va("%s:%i", peeraddr,cl):NULL):va("/%s", b->gamename), modeflags, b->isserver?ICEP_SERVER:ICEP_CLIENT);
	if (!*ret)
		return;	//some kind of error?!?

	iceapi.Set(ice, "server", va("stun:%s:%i", b->brokername, b->brokerport));

	s = net_ice_servers.string;
	while((s=COM_Parse(s)))
		iceapi.Set(ice, "server", com_token);

	//we're meant to wait until we reach the end of ICE_GATHERING state, but we assume our peer also supports trickle ice so we can just skip straight past that.
	if (!b->isserver)
		QICE_SendOffer(b, cl, ice, "sdpoffer");
}
static void QICE_Refresh(qice_connection_t *b, int cl, struct icestate_s *ice)
{	//sends offer
	char buf[8192];

#if defined(HAVE_JSON) && defined(HAVE_DTLS)
	if (net_ice_usewebrtc.value || !*net_ice_usewebrtc.string)//if (ice->modeflags & ICEF_ALLOW_WEBRTC)
	{
		while (ice && iceapi.GetLCandidateSDP(ice, buf, sizeof(buf)))
		{
			char json[8192+256];
			int ofs = QICE_PrepareBrokerFrame(ICEMSG_CANDIDATE, cl, json);

			q_strlcpy(json+ofs, "{\"candidate\":\"", sizeof(json)-ofs);
			JSON_QuotedString(buf+2, json+ofs+strlen(json+ofs), sizeof(json)-ofs-strlen(json+ofs)-2, true);
			q_strlcat(json+ofs, "\",\"sdpMid\":\"0\",\"sdpMLineIndex\":0}", sizeof(json)-ofs);
			QICE_SendBrokerFrame(b, json);
		}
	}
	else
#endif
	{
		int ofs = QICE_PrepareBrokerFrame(ICEMSG_CANDIDATE, cl, buf);
		while (ice && iceapi.GetLCandidateSDP(ice, buf+ofs, sizeof(buf)-ofs))
		{
			QICE_SendBrokerFrame(b, buf);
		}
	}
}
static void Buf_ReadString(const char **data, const char *end, char *out, size_t outsize)
{
	const char *in = *data;
	char c;
	outsize--;	//count the null early.
	while (in < end)
	{
		c = *in++;
		if (!c)
			break;
		if (outsize)
		{
			outsize--;
			*out++ = c;
		}
	}
	*out = 0;
	*data = in;
}
static qboolean QICE_UpdateBroker(qice_connection_t *b)
{
#ifdef HAVE_JSON
	json_t *json;
#endif
	int len, cl;
	const char *data;
	char msgbuf[8192];
	qboolean result = false;

	if (!b->broker && !b->error)
	{
		const char *roomname = b->gamename;
		char *url;
		if (b->reconnecttimeout > realtime || !*b->brokername)
			return false;

		if (b->isserver && *sv_port_rtc.string && strcmp(sv_port_rtc.string,"/"))
		{
			roomname = sv_port_rtc.string;
			if (*roomname == '/')
				roomname++;
		}

		if (!b->isserver)
			Con_SafePrintf("Connecting to rtc%s://%s:%i/%s...\n",
				b->issecure?"s":"",	//secure or not.
				b->brokername,	//broker ip/name,
				b->brokerport,
				roomname);	//server name

		COM_Parse(com_protocolname.string);
		url = va("ws%s:%s://%s/%s/%s",
			b->issecure?"s":"",	//secure or not.
			b->isserver?"rtc_host":"rtc_client",	//whether we're hosting or connecting
			b->brokername,	//broker ip/name,
			com_token,		//protocol/game
			roomname);	//server name
		b->broker = ICE_OpenTCP(url, b->brokerport, true);

		if (!b->broker)
		{
			b->reconnecttimeout = realtime + 30;
			Con_Printf("rtc broker connection to %s failed%s\n", b->brokername, b->isserver?" (retry: 30 secs)":"");
			return false;
		}


		b->heartbeat = realtime;
	}
	if (b->error)
	{
handleerror:
		if (b->broker)
			b->broker->Close(b->broker);
		b->broker = NULL;
		b->reconnecttimeout = realtime + 30;

		/*for (cl = 0; cl < b->numclients; cl++)
		{
			if (b->clients[cl].ice)
				iceapi.Close(b->clients[cl].ice, false);
			if (b->clients[cl].qsock)
				b->clients[cl].qsock->driverdata2 = NULL;	//remove that dead link too.
			b->clients[cl].ice = NULL;
		}
		if (b->ice)
			iceapi.Close(b->ice, false);
		if (b->qsock)
			b->qsock->driverdata2 = NULL;	//remove that dead link too.
		b->ice = NULL;*/

		if (b->error != 1 || !b->isserver)
			return false;	//permanant error...
		b->error = false;
		return false;
	}

	//keep checking for new candidate info.
	if (b->isserver)
	{
		for (cl = 0; cl < b->numclients; cl++)
			if (b->clients[cl].ice)
				QICE_Refresh(b, cl, b->clients[cl].ice);
		if (realtime >= b->heartbeat)
			QICE_Heartbeat(b);
	}
	else
	{
		if (b->ice)
			QICE_Refresh(b, b->serverid, b->ice);
	}

	len = b->broker->ReadBytes(b->broker, msgbuf, sizeof(msgbuf)-1);
	if (!len)
		return false;	//nothing new
	if (len < 0)
	{
//		if (!b->error)
			Con_Printf("rtc broker connection to %s failed%s\n", b->brokername, b->isserver?" (retry: 30 secs)":"");
		b->error = true;
		goto handleerror;
	}
	msgbuf[len] = 0;

	if (len < 3)
		Con_Printf("rtc runt (%s)\n", b->brokername);
	else
	{
		cl = (short)(msgbuf[1] | (msgbuf[2]<<8));
		data = msgbuf+3;

		switch(msgbuf[0])
		{
		case ICEMSG_PEERLOST:	//the broker lost its connection to our peer...
			if (cl == -1)
			{
				b->error = true;
				if (net_ice_debug.value)
					Con_Printf(S_COLOR_GRAY"[%s]: Broker host lost connection: %s\n", ICE_GetConnName(b->ice), *data?data:"<NO REASON>");
			}
			else if (cl >= 0 && cl < b->numclients)
			{
				if (net_ice_debug.value)
					Con_Printf(S_COLOR_GRAY"[%s]: Broker client lost connection: %s\n", ICE_GetConnName(b->clients[cl].ice), *data?data:"<NO REASON>");
				if (b->clients[cl].ice)
					iceapi.Close(b->clients[cl].ice, b->clients[cl].isnew);
				b->clients[cl].ice = NULL;
				b->clients[cl].isnew = false;	//just in case...
			}
			break;
		case ICEMSG_NAMEINUSE:
			Con_Printf("Unable to listen on /%s - name already taken\n", b->gamename);
			b->error = true;	//try again later.
			break;
		case ICEMSG_GREETING:	//reports the trailing url we're 'listening' on. anyone else using that url will connect to us.
			data = strchr(data, '/');
			if (data++)
				q_strlcpy(b->gamename, data, sizeof(b->gamename));
			Con_Printf("Publicly listening on /%s\n", b->gamename);
			break;
		case ICEMSG_NEWPEER:	//relay connection established with a new peer
			//note that the server ought to wait for an offer from the client before replying with any ice state, but it doesn't really matter for our use-case.
			{
				char peer[MAX_QPATH];
				char relay[MAX_QPATH];
				const char *s;
				Buf_ReadString(&data, msgbuf+len, peer, sizeof(peer));
				Buf_ReadString(&data, msgbuf+len, relay, sizeof(relay));

				if (b->isserver)
				{
//					Con_DPrintf("Client connecting: %s\n", data);
					if (cl < 1024 && cl >= b->numclients)
					{	//looks like a new one... but don't waste memory if too many slots were used...
						void *n = realloc(b->clients, sizeof(b->clients[0])*(cl+1));
						if (!n)
							break;
						b->clients = n;
						memset(b->clients+b->numclients, 0, sizeof(b->clients[0]) * ((cl+1) - b->numclients));
						b->numclients = cl+1;
					}
					if (cl >= 0 && cl < b->numclients)
					{
						if (b->clients[cl].isnew)
						{	//erk? don't leak
							iceapi.Close(b->clients[cl].ice, true);
							b->clients[cl].ice = NULL;
						}
						QICE_FoundPeer(b, *peer?peer:NULL, cl, &b->clients[cl].ice);
						b->clients[cl].isnew = true;
						for (s = relay; (s=COM_Parse(s)); )
							iceapi.Set(b->clients[cl].ice, "server", com_token);
						if (net_ice_debug.value)
							Con_Printf(S_COLOR_GRAY"[%s]: New client spotted...\n", ICE_GetConnName(b->clients[cl].ice));
					}
					else if (net_ice_debug.value)
						Con_Printf(S_COLOR_GRAY"[%s]: New client spotted, but index is unusable\n", ICE_GetConnName(NULL));
				}
				else
				{
					//Con_DPrintf("Server found: %s\n", data);
					QICE_FoundPeer(b, *peer?peer:NULL, cl, &b->ice);
					b->serverid = cl;
					for (s = relay; (s=COM_Parse(s)); )
						iceapi.Set(b->ice, "server", com_token);
					if (net_ice_debug.value)
						Con_Printf(S_COLOR_GRAY"[%s]: Server identified\n", ICE_GetConnName(b->ice));
				}
				result = true;
			}
			break;
		case ICEMSG_OFFER:	//we received an offer from a client
#ifdef HAVE_JSON
			json = JSON_Parse(data);
			if (json)
				//should probably also verify the type.
				data = JSON_GetString(json, "sdp", com_token,sizeof(com_token), NULL);
#endif
			if (b->isserver)
			{
				if (cl >= 0 && cl < b->numclients && b->clients[cl].ice)
				{
					if (net_ice_debug.value)
						Con_Printf(S_COLOR_GRAY"[%s]: Got offer:\n%s\n", ICE_GetConnName(b->clients[cl].ice), data);
					iceapi.Set(b->clients[cl].ice, "sdpoffer", data);
					iceapi.Set(b->clients[cl].ice, "state", STRINGIFY(ICE_CONNECTING));

					QICE_SendOffer(b, cl, b->clients[cl].ice, "sdpanswer");
				}
				else if (net_ice_debug.value)
					Con_Printf(S_COLOR_GRAY"[%s]: Got bad offer/answer:\n%s\n", ICE_GetConnName(b->clients[cl].ice), data);
			}
			else
			{
				Con_Printf ("Server contacted...\n");
				if (b->ice)
				{
					if (net_ice_debug.value)
						Con_Printf(S_COLOR_GRAY"[%s]: Got answer:\n%s\n", ICE_GetConnName(b->ice), data);
					iceapi.Set(b->ice, "sdpanswer", data);
					iceapi.Set(b->ice, "state", STRINGIFY(ICE_CONNECTING));
				}
				else if (net_ice_debug.value)
					Con_Printf(S_COLOR_GRAY"[%s]: Got bad offer/answer:\n%s\n", ICE_GetConnName(b->ice), data);
			}
#ifdef HAVE_JSON
			JSON_Destroy(json);
#endif
			break;
		case ICEMSG_CANDIDATE:
#ifdef HAVE_JSON
			json = JSON_Parse(data);
			if (json)
			{
				data = com_token;
				com_token[0]='a';
				com_token[1]='=';
				com_token[2]=0;
				JSON_GetString(json, "candidate", com_token+2,sizeof(com_token)-2, NULL);
			}
#endif
//			Con_Printf("Candidate update: %s\n", data);
			if (b->isserver)
			{
				if (cl >= 0 && cl < b->numclients && b->clients[cl].ice)
				{
					if (net_ice_debug.value)
						Con_Printf(S_COLOR_GRAY"[%s]: Got candidate:\n%s\n", ICE_GetConnName(b->clients[cl].ice), data);
					iceapi.Set(b->clients[cl].ice, "sdp", data);
				}
			}
			else
			{
				if (b->ice)
				{
					if (net_ice_debug.value)
						Con_Printf(S_COLOR_GRAY"[%s]: Got candidate:\n%s\n", ICE_GetConnName(b->ice), data);
					iceapi.Set(b->ice, "sdp", data);
				}
			}
#ifdef HAVE_JSON
			JSON_Destroy(json);
#endif
			break;
		default:
			if (net_ice_debug.value)
				Con_Printf(S_COLOR_GRAY"[%s]: Broker send unknown packet: %i\n", ICE_GetConnName(b->ice), msgbuf[0]);
			break;
		}
	}

	b->broker->ReadBytes(b->broker, NULL, 0);	//should flush any pending outogoing data.
	return result;
}

static void QICE_Closed(struct icemodule_s *module, struct icestate_s *ice)
{	//callback from ice to avoid hanging pointers.
	qice_connection_t *b = (qice_connection_t*)module;
	qsocket_t *s;
	int i;
	for (s = net_activeSockets; s; s = s->next)
	{
		if (s->driverdata == b)
		if (s->driverdata2 == ice)
		{
			s->driverdata = NULL;	//detach from the module, should start reporting errors causing any other state to close.
			s->driverdata2 = NULL;	//detach from ice state as its no longer valid.
		}
	}

	//and any broker state.
	if (b->ice == ice)
		b->ice = NULL;
	for (i = 0; i < b->numclients; i++)
	{
		if (b->clients[i].ice == ice)
			b->clients[i].ice = NULL;
	}
}

static void QICE_SendInitial(struct icemodule_s *module, struct icestate_s *ice)
{
	qice_connection_t *b = (qice_connection_t*)module;

	b->dohandshake = Sys_DoubleTime();	//reset handshake timer so we try NOW.
}

#ifdef HAVE_DTLS
static qboolean QICE_LoadCerts(struct dtlslocalcred_s *cred, char *priv, char *cert)
{
	qofs_t sz;
	int fd;

	sz = Sys_FileOpenRead(priv, &fd);	//private key is most likely to need special permissions to read, so fail on that one first.
	if (sz>=0)
	{
		cred->keysize = sz;
		if (cred->key)
			free(cred->key);
		cred->key = malloc(cred->keysize);
		Sys_FileRead(fd, cred->key, cred->keysize);
		Sys_FileClose(fd);
	}
	else
		return false;

	sz = Sys_FileOpenRead(cert, &fd);
	if (sz>=0)
	{
		cred->certsize = sz;
		if (cred->cert)
			free(cred->cert);
		cred->cert = malloc(cred->certsize);
		Sys_FileRead(fd, cred->cert, cred->certsize);
		Sys_FileClose(fd);
	}
	else
	{
		free(cred->key);
		cred->key = NULL;
		return false;
	}

	ICE_DePEM(cred);
	return true;
}
#endif

static qice_connection_t *QICE_Setup(const char *address, qboolean isserver)
{	//[rtc://][broker[:port]]/[roomname]
	qice_connection_t *newcon;
	const char *path;
	char *c;
	int i;

	struct
	{
		const char *name;
		qboolean secure;
	} schemes[] =
	{
		{"ws://", false},	//brokers are connected to via websockets so these two are technically more correct... but ambiguous
		{"wss://", true},
		{"ice://", false},	//individual servers might use these schemes. generally implies to skip dtls...
		{"ices://", true},
		{"rtc://", false},	//supposedly implies using dtls... but with no wss. dumb.
		{"rtcs://", true},
		{"tcp://", false},	//weirdness... lowest common denominator...
		{"tls://", true},
		{"http://", false},	//we can query the broker for servers over http... prolly shouldn't be used.
		{"https://", true},

#ifdef HAVE_GNUTLS
		{"", true},	//otherwise assume wss.
#else
		{"", false},	//otherwise assume unencrypted. FIXME!
#endif
	};

	newcon = Z_Malloc(sizeof(*newcon));

	if (address)
	{
		if (*address=='/' || !*address)
		{	//had a leading slash. use the default broker for it.
			q_strlcpy(newcon->brokername, "", sizeof(newcon->brokername));
			newcon->issecure = true;

			if (*address == '/')
				address++;
			q_strlcpy(newcon->gamename, address, sizeof(newcon->gamename));	//so we know what to tell the broker.
		}
		else
		{	//explicit broker was specified.
			for (i = 0; i < countof(schemes)-1; i++)
			{
				if (!strncmp(address, schemes[i].name, strlen(schemes[i].name)))
					break;
			}

			address += strlen(schemes[i].name);
			q_strlcpy(newcon->brokername, address, sizeof(newcon->brokername));
			newcon->issecure = schemes[i].secure;

			path = strchr(address, '/');
			if (path && path-address < sizeof(newcon->brokername))
			{	//truncate room from broker (broker may end up 0-bytes long).
				newcon->brokername[path-address] = 0;
				q_strlcpy(newcon->gamename, path+1, sizeof(newcon->gamename));	//so we know what to tell the broker.
			}
			else
				*newcon->gamename = 0;
		}

		if (!*newcon->brokername)
		{	//broker name was omitted. use the default.
			q_strlcpy(newcon->brokername, net_ice_broker.string, sizeof(newcon->brokername));	//fallback.
			for (i = 0; i < countof(schemes)-1; i++)
			{
				if (!strncmp(net_ice_broker.string, schemes[i].name, strlen(schemes[i].name)))
					break;
			}
			q_strlcpy(newcon->brokername, net_ice_broker.string+strlen(schemes[i].name), sizeof(newcon->brokername));
			newcon->issecure = schemes[i].secure;
		}

		c = strchr(newcon->brokername, ':');
		if (c)
		{
			newcon->brokerport = atoi(c+1);
			*c = 0;
		}
		else
			newcon->brokerport = PORT_ICEBROKER;
	}
	else
	{
		//not doing the broker thing... just directly using ice.
		*newcon->brokername = 0;
		newcon->brokerport = 0;
		*newcon->gamename = 0;
	}

	newcon->broker = NULL;
	newcon->reconnecttimeout = realtime;
	newcon->heartbeat = realtime;
	newcon->isserver = isserver;
	newcon->dohandshake = realtime;
	newcon->icemodule.SendInitial = QICE_SendInitial;
	newcon->icemodule.ClosedState = QICE_Closed;

	ICE_SetupModule(&newcon->icemodule, isserver?net_hostport+1000:0);	//try to keep to well-defined port numbers, ish, in case people need to set up manual holepunching.

#ifdef HAVE_DTLS
	if (isserver)
	{
		struct dtlslocalcred_s cred = {NULL,0,NULL,0};
		int a_k, a_c;
		newcon->icemodule.dtlsfuncs = ICE_DTLS_InitServer();	//we want to support clients using dtls...

		a_k = COM_CheckParm("-privkey")+1;
		a_c = COM_CheckParm("-pubkey")+1;
		if (a_k <= 1 || a_k >= com_argc || a_c <= 1 || a_c >= com_argc || !QICE_LoadCerts(&cred, com_argv[a_k], com_argv[a_c]))
#if defined(__unix__) || defined(__POSIX__)
			if (!QICE_LoadCerts(&cred, "/etc/ssl/private/privkey.pem", "/etc/ssl/certs/fullchain.pem"))	//look in the system path.
#endif
				if (!QICE_LoadCerts(&cred, va("%s/privkey.pem", com_basedir), va("%s/fullchain.pem", com_basedir)))
					if (!QICE_LoadCerts(&cred, va("%s/privkey.der", com_basedir), va("%s/fullchain.der", com_basedir)))
					{	//generate temp ones.
						char *hostname = "localhost";
						int a_h = COM_CheckParm("-certhost")+1;
						if (a_h > 1 && a_h < com_argc)
							hostname = com_argv[a_h];

						if (newcon->icemodule.dtlsfuncs->GenTempCertificate(hostname, &cred))
						{
							int fd;
							//and save em to disk
							fd = Sys_FileOpenWrite(va("%s/privkey.der", com_basedir));
							if (fd>=0)
							{
								Sys_FileWrite(fd, cred.key, cred.keysize);
								Sys_FileClose(fd);
							}
							fd = Sys_FileOpenWrite(va("%s/fullchain.der", com_basedir));
							if (fd>=0)
							{
								Sys_FileWrite(fd, cred.cert, cred.certsize);
								Sys_FileClose(fd);
							}
						}
					}

		newcon->icemodule.dtlsfuncs->SetCredentials(&cred);
		free(cred.cert);
		free(cred.key);
	}
#endif

	return newcon;
}


//public functions, to make qss happy.
#include "../net_defs.h"
static qboolean qice_listening;	//whether we want to host or not.
static qice_connection_t *qice_hostcon;	//broker for our server side.


#if 0//def HAVE_SERVER
void SVC_ICE_Offer(netadr_t *from)
{	//handles an 'ice_offer' udp message from a broker
//	extern cvar_t net_ice_servers;
	struct icestate_s *ice;
	static float throttletimer;
	const char *sdp, *s;
	char buf[1400];
	int sz;
	const char *clientaddr = Cmd_Argv(1);	//so we can do ip bans on the client's srflx address
	const char *brokerid = Cmd_Argv(2);	//specific id to identify the pairing on the broker.
	netadr_t adr;
#ifdef HAVE_JSON
	json_t *json;
#endif
	if (!qice_hostcon)
		return;	//err..?
	if (from->prot != NP_DTLS && from->prot != NP_TLS)
	{	//a) dtls provides a challenge (ensuring we can at least ban them).
		//b) this contains the caller's ips. We'll be pinging them anyway, but hey. also it'll be too late at this point but it keeps the other side honest.
		Con_DPrintf(CON_WARNING"%s: ice handshake from %s was unencrypted\n", NET_AdrToString (buf, sizeof(buf), from), clientaddr);
		return;
	}

	/*if (!NET_StringToAdr_NoDNS(clientaddr, 0, &adr))	//no dns-resolution denial-of-service attacks please.
	{
		Con_DPrintf(CON_WARNING"%s: ice handshake specifies bad client address: %s\n", NET_AdrToString (buf, sizeof(buf), from), clientaddr);
		return;
	}
	if (SV_BannedReason(&adr)!=NULL)
	{
		Con_DPrintf(CON_WARNING"%s: ice handshake for %s - banned\n", NET_AdrToString (buf, sizeof(buf), from), clientaddr);
		return;
	}*/

	ice = iceapi.Create(&qice_hostcon->icemodule, brokerid, clientaddr, ICEF_DEFAULT, ICEP_SERVER);
	if (!ice)
		return;	//some kind of error?!?
	//use the sender as a stun server. FIXME: put server's address in the request instead.
	iceapi.Set(ice, "server", va("stun:%s", NET_AdrToString (buf, sizeof(buf), from)));	//the sender should be able to act as a stun server for use. should probably just pass who its sending to and call it a srflx anyway, tbh.

	s = net_ice_servers.string;
	while((s=COM_Parse(s)))
		iceapi.Set(ice, "server", com_token);

	sdp = MSG_ReadString();
#ifdef HAVE_JSON
	json = JSON_Parse(sdp);	//browsers are poo
	if (json)
		sdp = JSON_GetString(json, "sdp", buf,sizeof(buf), "");
#endif

	if (iceapi.Set(ice, "sdpoffer", sdp))
	{
		iceapi.Set(ice, "state", STRINGIFY(ICE_CONNECTING));	//skip gathering, just trickle.

		q_snprintf(buf, sizeof(buf), "\xff\xff\xff\xff""ice_answer %s", brokerid);
		sz = strlen(buf)+1;
		if (iceapi.Get(ice, "sdpanswer", buf+sz, sizeof(buf)-sz))
		{
			sz += strlen(buf+sz);

			NET_SendPacket(svs.sockets, sz, buf, from);
		}
	}
#ifdef HAVE_JSON
	JSON_Destroy(json);
#endif

	//and because we won't have access to its broker, disconnect it from any persistent state to let it time out.
	iceapi.Close(ice, false);
}
void SVC_ICE_Candidate(netadr_t *from)
{	//handles an 'ice_ccand' udp message from a broker
	struct icestate_s *ice;
#ifdef HAVE_JSON
	json_t *json;
#endif
	const char *sdp;
	char buf[1400];
	const char *brokerid = Cmd_Argv(1);	//specific id to identify the pairing on the broker.
	unsigned int seq = atoi(Cmd_Argv(2));	//their seq, to ack and prevent dupes
	unsigned int ack = atoi(Cmd_Argv(3));	//so we don't resend endlessly... *cough*
	if (!qice_hostcon)
		return;
	if (from->prot != NP_DTLS && from->prot != NP_WSS && from->prot != NP_TLS)
	{
		return;
	}
	ice = iceapi.Find(NULL, brokerid);
	if (!ice)
		return;	//bad state. lost packet?

	//parse the inbound candidates
	for(;;)
	{
		sdp = MSG_ReadStringLine();
		if (msg_badread || !*sdp)
			break;
		if (seq++ < ice->u.inseq)
			continue;
		ice->u.inseq++;
#ifdef HAVE_JSON
		json = JSON_Parse(sdp);
		if (json)
		{
			sdp = buf;
			buf[0]='a';
			buf[1]='=';
			buf[2]=0;
			JSON_GetString(json, "candidate", buf+2,sizeof(buf)-2, NULL);
		}
#endif
		iceapi.Set(ice, "sdp", sdp);
#ifdef HAVE_JSON
		JSON_Destroy(json);
#endif
	}

	while (ack > ice->u.outseq)
	{	//drop an outgoing candidate line
		char *nl = strchr(ice->u.text, '\n');
		if (nl)
		{
			nl++;
			memmove(ice->u.text, nl, strlen(nl)+1);	//chop it away.
			ice->u.outseq++;
			continue;
		}
		//wut?
		if (ack > ice->u.outseq)
			ice->u.outseq = ack;	//a gap? oh noes!
		break;
	}

	//check for new candidates to include
	while (iceapi.GetLCandidateSDP(ice, buf, sizeof(buf)))
	{
		Z_StrCat(&ice->u.text, buf);
		Z_StrCat(&ice->u.text, "\n");
	}

	q_snprintf(buf, sizeof(buf), "\xff\xff\xff\xff""ice_scand %s %u %u\n%s", brokerid, ice->u.outseq, ice->u.inseq, ice->u.text?ice->u.text:"");
	NET_SendPacket(svs.sockets, strlen(buf), buf, from);
}
#endif

static void ICE_Show_f(void)
{
	const char *findname = Cmd_Argv(1);
	qsocket_t *s;

	if (!*findname)
	{	//passing null will report all of them. even when they're not attached to a qsocket.
		ICE_Debug(NULL);
		return;
	}

	for (s = net_activeSockets; s; s = s->next)
	{
		if (net_drivers[s->driver].Init != NQICE_Init)
			continue;	//uninteresting.
		if (*findname && q_strcasecmp(findname, s->trueaddress+1))
			continue;
		if (s->driverdata2)
			ICE_Debug(s->driverdata2);
	}
}
int NQICE_Init (void)
{
	if (safemode || COM_CheckParm("-noice"))
		return -1;

	if (!COM_CheckParm("-useice"))
		return -1;	//disable by default for now.

	//ICE state
	Cvar_RegisterVariable(&net_ice_exchangeprivateips);
	Cvar_RegisterVariable(&net_ice_allowstun);
	Cvar_RegisterVariable(&net_ice_allowturn);
	Cvar_RegisterVariable(&net_ice_allowmdns);
	Cvar_RegisterVariable(&net_ice_relayonly);
#ifdef HAVE_DTLS
	Cvar_RegisterVariable(&net_ice_usewebrtc);
#endif
	Cvar_RegisterVariable(&net_ice_servers);
	Cvar_RegisterVariable(&net_ice_debug);

	//broker context.
	Cvar_RegisterVariable(&net_ice_broker);
	Cvar_RegisterVariable(&sv_port_rtc);

	Cmd_AddCommand("net_ice_show", ICE_Show_f);

	return 0;	//its okay. we'll create actual sockets later.
}
int NQICE_QueryAddresses(qhostaddr_t *addresses, int maxaddresses)	//server state
{	//returns the server's address, if known.
	int i = 0, j, k, l;
	netadr_t adrs[16];
	if (qice_hostcon)
	{	//include broker url? too lazy.
		if (i < maxaddresses)
		{
			addresses[i][0] = '/';
			q_strlcpy(addresses[i]+1, qice_hostcon->gamename, sizeof(addresses[i])-1);
			i++;
		}

		//FIXME: add stun results.

		for (j = 0; j < countof(qice_hostcon->icemodule.conn); j++)
			if (qice_hostcon->icemodule.conn[j] && qice_hostcon->icemodule.conn[j]->EnumerateAddresses)
			{
				k = qice_hostcon->icemodule.conn[j]->EnumerateAddresses(qice_hostcon->icemodule.conn[j], adrs, countof(adrs));
				for (l = 0; l < k && i < maxaddresses; l++)
				{
					if (NET_ClassifyAddress(&adrs[l], NULL) < ASCOPE_LINK)
						continue;
					NET_AdrToString(addresses[i],sizeof(addresses[i]), &adrs[l]);
					i++;
				}
			}
	}
	return i;
}
void Datagram_AddHostCacheInfo(struct qsockaddr *readaddr, const char *cname, const char *info);
qboolean NQICE_SearchForHosts (qboolean xmit)
{
	static char buf[8192], cname[128];
	static int ofs = 0, sz = 0;
	static int header;
	static icestream_t *lst;
	char *e;
	const char *l;
	int r;

	if (xmit && !lst)
	{
		const char *broker = net_ice_broker.string;
		qboolean sec = true;

#ifndef HAVE_TLS
		sec = false;	//assume no tls if we can't do tls.
#endif

		//clean up the broker name a bit... it was dumb to have so many possibilities.
		if (!strncmp(broker, "ws://", 5))
			broker += 5, sec = false;
		else if (!strncmp(broker, "tcp://", 6) || !strncmp(broker, "ice://", 6) || !strncmp(broker, "rtc://", 6))
			broker += 6, sec = false;
		else if (!strncmp(broker, "tls://", 6) || !strncmp(broker, "wss://", 6))
			broker += 6, sec = true;
		else if (!strncmp(broker, "ices://", 7) || !strncmp(broker, "rtcs://", 7))
			broker += 7, sec = true;
		else if (strstr(broker, "://"))
			broker = NULL;	//something weird.

		ofs = sz = 0;
		header = 2;
		lst = broker?ICE_OpenTCP(broker, PORT_ICEBROKER, sec):NULL;
		if (lst)
		{
			COM_ParseOut(com_protocolname.string, com_token,sizeof(com_token));
			q_snprintf(buf, sizeof(buf),
				"GET /raw/%s HTTP/1.1\r\n"
				"Host: %s\r\n"
				"Connection: close\r\n"
				"User-Agent: "ENGINE_NAME_AND_VER"\r\n"
				"\r\n", com_token, broker);
			sz = strlen(buf);
		}
	}

	if (lst)
	{
		if (header == 2)
		{
			r = lst->WriteBytes(lst, buf+ofs, sz-ofs);
			if (r < 0)
			{	//EOF? error? w/e
				lst->Close(lst);
				lst = NULL;
				return false;	//give up.
			}
			ofs += r;
			if (ofs < sz)
				return true;	//waiting for it to flush (probably delayed due to tls handshakes)

			header = 1;
			ofs = 0; sz = 0;
		}
		r = lst->ReadBytes(lst, buf+sz, sizeof(buf)-1 - sz);
		if (r > 0)
			sz += r;
		else if (r < 0)
		{
			lst->Close(lst);
			lst = NULL; //EOF? error? w/e
		}
		buf[sz] = 0;

		if (header)
		{
			if (strncmp(buf, "HTTP/1.1 200 ", (sz>13)?13:sz))
			{	//not http... or a weird error code that we can't handle. too lazy to do redirects.
				lst->Close(lst);
				lst = NULL;
				return true;
			}
			if (sz < 13)
				return true;	//still waiting...
			e = strstr(buf, "\r\n\r\n");
			if (!e)
				return true;	//headers not complete
			e += 4;
			ofs = e-buf;
			//okay, headers skipped over.
			header = 0;
			//consume it.
			memmove(buf, buf+ofs, sz-ofs+1);
			sz -= ofs;
			ofs = 0;
		}

		for(;;)
		{
			e = strchr(buf+ofs, '\n');
			if (!e)
				break;

			if (e > buf+ofs && e[-1] == '\r')
				e[-1] = 0;
			else
				*e = 0;
			l = buf+ofs;
			ofs = e+1-buf;

			while (*l == ' ' || *l == '\t')
				l++;
			if (*l == '#' || !*l)
				continue;
			l=COM_ParseOut(l, cname, sizeof(cname));
			while (*l == ' ' || *l == '\t')
				l++;
			Datagram_AddHostCacheInfo(NULL, cname, l);
		}

		if (ofs > 0)
		{
			memmove(buf, buf+ofs, sz-ofs+1);
			sz -= ofs;
			ofs = 0;
		}

		return true;
	}

	return false;	//no broadcasts here. we can't implement this.
}
qsocket_t *NQICE_Connect (const char *host)		//used by client (enables websocket connection). fails when not ice, fails when unable to resolve broker, otherwise succeeds pending broker failure.
{
	qice_connection_t *b;
	qsocket_t *dest;
	qboolean direct = true;

	//only allow "/room" type names. also accept rtc[s]:// uris.
	if (!host)
		return NULL;
	else if (*host == '/')
		direct = false;
	else if (!strncmp(host, "rtc://", 6) || !strncmp(host, "rtcs://", 7))
		direct = false;
	else if (!strncmp(host, "udp://", 6))
		direct = true;	//plain text...
	else if (!strncmp(host, "dtls://", 7))
		direct = true;	//direct dtls connection (hopefully with `?fp=b64` on the end
	else if (!strncmp(host, "ws://", 5) || !strncmp(host, "wss://", 6))
		direct = true;	//direct websocket address (fixme: add #fp=? )
	else
		return NULL;

	//create a new ice connection
	dest = NET_NewQSocket();
	if (dest)
	{
		if (direct)
		{
			b = QICE_Setup(NULL, false);
			b->ice = iceapi.Create(&b->icemodule, NULL, NULL, ICEF_INITIATOR, ICEP_CLIENT);
			iceapi.Set(b->ice, "peer", host);
		}
		else
		{	//just broker. ice state will be set up once the broker tells us we have a peer.
			b = QICE_Setup(host, false);
		}

		dest->proquake_angle_hack = true;

		dest->driverdata = b;
		dest->driverdata2 = b->ice;
		b->qsock = dest;
	}
	return dest;
}

static int QICE_ResendReliable (qsocket_t *sock)
{
	neterr_t err;
	struct icestate_s *ice = sock->driverdata2;
	unsigned int length;
	struct
	{
		unsigned int length;
		unsigned int sequence;
		qbyte data[1];
	} *pkt;

	if (!sock->sendMessageLength)
		return NETERR_SENT;	//nothing to actually send.
	if (!ice)	//still waiting for it to become available? don't crash.
		return NETERR_CLOGGED;

	sock->sendNext = false;

	length = sock->max_datagram;
	if (length >= sock->sendMessageLength)
		length = sock->sendMessageLength;

	pkt = alloca(NET_HEADERSIZE + length);
	pkt->length = BigLong((NET_HEADERSIZE+length) | NETFLAG_DATA | ((length==sock->sendMessageLength)?NETFLAG_EOM:0));
	pkt->sequence = BigLong(sock->sendSequence);
	memcpy(pkt->data, sock->sendMessage, length);

	err = iceapi.SendPacket(ice, pkt, NET_HEADERSIZE+length);
	if (err == NETERR_CLOGGED)
	{
		sock->sendNext = true;	//didn't actually send. retry now.
		return 0;
	}
	else if (err == NETERR_NQIO)	//netquake.io is unable to handle fragmentation
	{	//mtu too small...
		sock->max_datagram = 65536; //boost it. hopefully it'll start to get through after.
		sock->sendNext = true;	//didn't actually send. retry now.
		return 0;
	}
	else if (err == NETERR_MTU)
	{	//mtu too big...
		if (sock->max_datagram > 1200)
		{	//should still be on the first segment on the reliable if it didn't get through yet.
			Con_Printf("MTU error, dropping to %i\n", sock->max_datagram);
			sock->max_datagram -= 64;
			return 0;
		}
		Con_Printf("MTU error\n");
		return -1;
	}
	sock->lastSendTime = net_time;
	if (err == NETERR_SENT)
		return 1; //yay
	return -1;	//fatal
}

static void QICE_ProcessMessage(unsigned int length, struct icestate_s *ice, qsocket_t *sock, const void *data, size_t datasize, sizebuf_t *newmsg)
{
	unsigned int sequence;

	if (datasize < 8)
		return;	//runt...

	if ((length & 0xffff) != datasize)
		return;	//err... something weird.

	if (length & NETFLAG_CTL)
	{	//no sequence info here.
		return;
	}
	sequence = BigLong(((const int*)data)[1]);
	data = &((const int*)data)[2];
	datasize -= NET_HEADERSIZE;

	if (length & NETFLAG_UNRELIABLE)
	{
		if (sequence < sock->unreliableReceiveSequence)
		{
			Con_DPrintf("Got a stale datagram\n");
			return;
		}
		if (sequence != sock->unreliableReceiveSequence)
		{
			unsigned int count = sequence - sock->unreliableReceiveSequence;
			Con_DPrintf("Dropped %u datagram(s)\n", count);
		}
		sock->unreliableReceiveSequence = sequence + 1;

		SZ_Clear(newmsg);
		SZ_Write(newmsg, data, datasize);
	}
	else if (length & NETFLAG_ACK)
	{
		if (sequence != sock->sendSequence)
		{
			Con_DPrintf("Stale ACK received\n");
			return;
		}
		sock->sendSequence++;
		sock->sendMessageLength -= sock->max_datagram;
		if (sock->sendMessageLength > 0)
		{
			memmove (sock->sendMessage, sock->sendMessage + sock->max_datagram, sock->sendMessageLength);
			sock->sendNext = true;
		}
		else
		{
			sock->sendMessageLength = 0;
			sock->canSend = true;
		}
	}
	else if (length & NETFLAG_DATA)
	{
		//send ack first (server might have missed the previous ack)
		{
			int ack[2];
			ack[0] = BigLong(NET_HEADERSIZE | NETFLAG_ACK);
			ack[1] = BigLong(sequence);
			iceapi.SendPacket(ice, ack, sizeof(ack));
		}

		if (sequence != sock->receiveSequence)
			return;
		sock->receiveSequence++;

		if (length & NETFLAG_EOM)
		{
			SZ_Clear(newmsg);
			SZ_Write(newmsg, sock->receiveMessage, sock->receiveMessageLength);
			SZ_Write(newmsg, data, datasize);
			sock->receiveMessageLength = 0;
		}
		else
		{
			memcpy(sock->receiveMessage+sock->receiveMessageLength, data, datasize);
			sock->receiveMessageLength += datasize;
		}
	}
}

static qboolean QICE_SV_CheckPassword(int check)
{
	extern cvar_t password;
	if (*password.string && strcmp(password.string, "none"))
	{
		char *e;
		int pwd = strtol(password.string, &e, 0);
		if (*e)
			pwd = Com_BlockChecksum(password.string, strlen(password.string));

		if (check != pwd)
			return false;	//you chose poorly.
	}
	return true; //okay!
}

static qice_connection_t *qice_clientcon;	//evil global pointer passing.
static sizebuf_t qice_msgqueue[16];	//dumb fifo. this is what you get when you process stuff in bulk. sctp can bundle multiple packets too so we can't exactly avoid the whole bulk thing.
static int qice_msgqueuesize;	//number used.
static int qice_msgqueueofs;	//position.
static qboolean QICE_GotS2CMessage (struct icestate_s *ice, const void *data, size_t datasize)
{
	unsigned int header;
	qice_connection_t *b = qice_clientcon;
	qsocket_t *sock = b->qsock;

	if (ice != b->ice)
	{
		Con_Printf("Packet from wrong ice state...\n");
		return false;
	}

	header = BigLong(((const int*)data)[0]);
	if (header == (datasize | NETFLAG_CTL))
	{
		int req;

		SZ_Clear(&net_message);
		SZ_Write(&net_message, data, datasize);
		MSG_BeginReading();
		MSG_ReadLong();

		req = MSG_ReadByte();
		if (!b->isserver)
		{
			extern char m_return_reason[32];

			if (req == CCREP_ACCEPT)
			{
#ifdef HAVE_DTLS
				char enc[32];
#endif
				int mod, flags;
				b->dohandshake = 0;

				/*port =*/MSG_ReadLong();	//don't care, should have been 0 anyway. doesn't make sense over ice.
				mod = MSG_ReadByte();
				/*ver =*/ MSG_ReadByte();
				flags = MSG_ReadByte();

				if (mod == MOD_PROQUAKE)
				{
					if (flags & PQF_CHEATFREE)
					{
						const char *reason = "Server is incompatible";
						Con_Printf("%s\n", reason);
						q_strlcpy(m_return_reason, reason, sizeof(m_return_reason));
						return false;
					}
//					if (flags & PQF_IGNOREPORT)
//						port = 0; //don't switch it, for non-identity port forwarding.
					sock->proquake_angle_hack = true;
				}
				else
					sock->proquake_angle_hack = false;

#ifdef HAVE_DTLS
				if (iceapi.Get(ice, "encrypted", enc,sizeof(enc)) && atoi(enc) && b->issecure)
					Con_Printf ("Connection accepted ("S_COLOR_GREEN"encrypted"CON_DEFAULT")\n");
				else
					Con_Printf ("Connection accepted ("S_COLOR_RED"plaintext"CON_DEFAULT")\n");
#else
				Con_Printf ("Connection accepted\n");	//don't classify it as encrypted nor plain, so as to not let people think they can fix it by tweaking settings. they can't.
#endif
			}
			else if (req == CCREP_REJECT)
			{
				const char *reason = MSG_ReadString();
				Con_Printf(S_COLOR_RED"%s\n", reason);
				q_strlcpy(m_return_reason, reason, sizeof(m_return_reason));
				b->error = true;
			}
		}
		SZ_Clear(&net_message);
		return true;
	}

	if (qice_msgqueuesize == countof(qice_msgqueue))
		return true;	//can't handle it... drop it. if it was a reliable we'll just have to wait for a resend. FIXME: match the server and use callbacks...
	if (!qice_msgqueue[qice_msgqueuesize].data)
	{
		qice_msgqueue[qice_msgqueuesize].maxsize = NET_MAXMESSAGE;
		qice_msgqueue[qice_msgqueuesize].data = Z_Malloc(qice_msgqueue[qice_msgqueuesize].maxsize);
		SZ_Clear(&qice_msgqueue[qice_msgqueuesize]);	//paranoia
	}
	QICE_ProcessMessage(header, ice, b->qsock, data, datasize, &qice_msgqueue[qice_msgqueuesize]);
	if (qice_msgqueue[qice_msgqueuesize].cursize)
		qice_msgqueuesize++;
	return true;
}
int NQICE_GetMessage (qsocket_t *sock)
{	//ICE_GetAnyMessage... but for the client. client may logically only have one connection active at a time.
	qice_connection_t *b = sock->driverdata;

	if (qice_msgqueueofs < qice_msgqueuesize)
	{
		SZ_Clear(&net_message);
		SZ_Write(&net_message, qice_msgqueue[qice_msgqueueofs].data, qice_msgqueue[qice_msgqueueofs].cursize);
		qice_msgqueueofs++;
		return 1;
	}
	qice_msgqueueofs = 0;
	while(qice_msgqueuesize)
		SZ_Clear(&qice_msgqueue[--qice_msgqueuesize]);

	QICE_UpdateBroker(b);	//keep the broker going.

	qice_clientcon = b;
	net_message.cursize = 0;
	b->icemodule.ReadGamePacket = QICE_GotS2CMessage;
	sock->driverdata2 = b->ice;
	iceapi.ProcessModule(&b->icemodule);
	qice_clientcon = NULL;

	if (qice_msgqueuesize)
	{
		//we got one? multiple? only first matters here.
		SZ_Clear(&net_message);
		SZ_Write(&net_message, qice_msgqueue[0].data, qice_msgqueue[0].cursize);
		qice_msgqueueofs = 1;
		return 1;
	}

	//handle nq's reliable resends
	if ((net_time - sock->lastSendTime) > 1.0)
		sock->sendNext = true;
	if (sock->sendNext)
		if (QICE_ResendReliable(sock) < 0)
			return -1;	//erk...

	if (b && !b->error)
		return 0;	//not doable yet
	return -1;
}

qsocket_t *NQICE_CheckNewConnections (void)
{	//poll the server's websocket.
	qice_connection_t *b = qice_hostcon;	//stoopid globals.

	if (b)
		QICE_UpdateBroker(b);
	return NULL;
}

static void QCICE_ReadUnsolicitedSPacket (struct icestate_s *ice, const void *data, size_t datasize, struct icestate_s*(*Accept)(struct icestate_s*temp))
{
	unsigned int header;
	header = BigLong(((const int*)data)[0]);
	if (header == 0xffffffff)
		return;	//quakeworld connectionless packets.
	if (header == (datasize | NETFLAG_CTL))
	{
		int req;
		SZ_Clear(&net_message);
		SZ_Write(&net_message, data, datasize);
		MSG_BeginReading();
		MSG_ReadLong();
		req = MSG_ReadByte();
		if (req == CCREQ_CONNECT)
		{
			qice_connection_t *b = qice_hostcon;
			const char *error = NULL;
			const char *game = MSG_ReadString();
			int ver = MSG_ReadByte();
			//proquakeisms
			int mod, mod_passwd = 0;
			int plnum = -1;
			qsocket_t *s = NULL;

			mod = MSG_ReadByte();
			if (mod==1)
			{
				/*modver =*/ MSG_ReadByte();
				/*modflags =*/ MSG_ReadByte();
				mod_passwd = MSG_ReadLong();
				if (msg_badread) mod_passwd = 0;
			}

			if (strcmp(game, DGRAM_PROTOCOL_NAME))
				return;	//wut? huh? did that just happen?!?

			if (ver != NET_PROTOCOL_VERSION)
				error = "Incompatible version.\n";
			else if (!QICE_SV_CheckPassword(mod_passwd))
				error = "bad/missing password.\n";

			SZ_Clear(&net_message);
			MSG_WriteLong(&net_message, 0);	//ctl header...
			if (error)
			{
				MSG_WriteByte(&net_message, CCREP_REJECT);
				MSG_WriteString(&net_message, error);
			}
			else
			{	//this sucks!
				for (s = net_activeSockets; s; s = s->next)
					if (s->driverdata2 == ice)
						break;

				if (!s)
				{	//new client... connect them if we can.
					char *dot, *dot2;

					//find a free player slot
					for (plnum=0 ; plnum<svs.maxclients ; plnum++)
						if (!svs.clients[plnum].active)
							break;
					if (plnum < svs.maxclients)
					{
						if (Accept)
							ice = Accept(ice);	//it was unsolicited and we need to explicitly accept it. Note: DTLS already created a connection so won't reach here.
						s = NET_NewQSocket();
					}
					if (s)
					{	//free slot AND qsocket, we are blessed!
						int i;
						for (i = 0; i < b->numclients; i++)
							if (b->clients[i].ice == ice)
								b->clients[i].isnew = false;	//has one allocated now

						s->proquake_angle_hack = (mod == 1);
						s->driverdata = b;
						s->driverdata2 = ice;
						s->socket = -1;
						s->landriver = -1;

						//there is no truth here. the broker reported and masked their IP.
						//we could report where we're sending our packets, but that's likely a TURN relay rather than the actual target machine.
						*s->trueaddress = '@'; iceapi.Get(ice, "name", s->trueaddress+1, sizeof(s->trueaddress)-1);
						*s->maskedaddress = '@'; iceapi.Get(ice, "name", s->maskedaddress+1, sizeof(s->maskedaddress)-1); //should already be masked.

						dot = strchr(s->maskedaddress, '.');
						if (dot)
						{
							dot2 = strchr(dot+1, '.');
							if (dot2)
								q_strlcpy(dot2, ".x.x", sizeof(s->maskedaddress) - (dot2-s->maskedaddress));
						}
					}
					else
						error = "Server is full.\n";
				}

				if (error)
				{
					MSG_WriteByte(&net_message, CCREP_REJECT);
					MSG_WriteString(&net_message, error);
				}
				else
				{
					MSG_WriteByte(&net_message, CCREP_ACCEPT);

					MSG_WriteLong(&net_message, 0);	//never write the port on ICE
					s->proquake_angle_hack = (mod==1);
					if (s->proquake_angle_hack)
					{
						MSG_WriteByte(&net_message, 1);	//proquake
						MSG_WriteByte(&net_message, 30);//ver 30 should be safe. 34 screws with our single-server-socket stuff.
						MSG_WriteByte(&net_message, PQF_IGNOREPORT);	//flags: 0x80==ignore port

						//FTE adds this tp let the server know to ignore the ccreq and instead reply as a qw server would (avoiding race[packetloss] issues).
						//MSG_WriteString(&net_message, va("getchallenge %i %s\n", connectinfo.clchallenge, COM_QuotedString(com_protocolname.string, tmp, sizeof(tmp), false)));
					}
				}
			}

			*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
			iceapi.SendPacket(ice, net_message.data, net_message.cursize);
			SZ_Clear(&net_message);

			if (plnum>=0 && s)
			{
				//spawn the client.
				//FIXME: come up with some challenge mechanism so that we don't go to the expense of spamming serverinfos+modellists+etc until we know that its an actual connection attempt.
				svs.clients[plnum].netconnection = s;
				SV_ConnectClient (plnum);
			}
		}
		else if (req == CCREQ_SERVER_INFO)
		{	//use Q3's \xff\xff\xff\xffgetinfo request instead.
			qhostaddr_t adr;
			if (Q_strcmp(MSG_ReadString(), DGRAM_PROTOCOL_NAME) != 0)
				return;

			SZ_Clear(&net_message);
			// save space for the header, filled in later
			MSG_WriteLong(&net_message, 0);
			MSG_WriteByte(&net_message, CCREP_SERVER_INFO);
			if (NQICE_QueryAddresses(&adr, 1) < 1)
				adr[0] = 0;	//unknown
			MSG_WriteString(&net_message, adr);	//cannonical name
			MSG_WriteString(&net_message, hostname.string);	//host name
			MSG_WriteString(&net_message, sv.name);	//map name
			MSG_WriteByte(&net_message, net_activeconnections);
			MSG_WriteByte(&net_message, svs.maxclients);
			MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
			*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
			iceapi.SendPacket(ice, net_message.data, net_message.cursize);
			SZ_Clear(&net_message);
		}
		else if (req == CCREQ_PLAYER_INFO)
		{	//use Q3's \xff\xff\xff\xffgetstatus request instead.
			int			playerNumber;
			int			activeNumber;
			int			clientNumber;
			client_t	*client;

			playerNumber = MSG_ReadByte();
			activeNumber = -1;

			for (clientNumber = 0, client = svs.clients; clientNumber < svs.maxclients; clientNumber++, client++)
			{
				if (client->active)
				{
					activeNumber++;
					if (activeNumber == playerNumber)
						break;
				}
			}

			if (clientNumber == svs.maxclients)
				return;

			SZ_Clear(&net_message);
			// save space for the header, filled in later
			MSG_WriteLong(&net_message, 0);
			MSG_WriteByte(&net_message, CCREP_PLAYER_INFO);
			MSG_WriteByte(&net_message, playerNumber);
			MSG_WriteString(&net_message, client->name);
			MSG_WriteLong(&net_message, client->colors);
			MSG_WriteLong(&net_message, (int)client->edict->v.frags);
			if (!client->netconnection)
			{
				MSG_WriteLong(&net_message, 0);
				MSG_WriteString(&net_message, "Bot");
			}
			else
			{
				MSG_WriteLong(&net_message, (int)(net_time - client->netconnection->connecttime));
				MSG_WriteString(&net_message, NET_QSocketGetMaskedAddressString(client->netconnection));
			}
			*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
			iceapi.SendPacket(ice, net_message.data, net_message.cursize);
			SZ_Clear(&net_message);
		}
		else if (req == CCREQ_RULE_INFO)
		{	//use Q3's \xff\xff\xff\xffgetinfo request instead.
			const char	*prevCvarName;
			cvar_t			*var;

			// find the search start location
			prevCvarName = MSG_ReadString();
			var = Cvar_FindVarAfter (prevCvarName, CVAR_SERVERINFO);

			// send the response
			SZ_Clear(&net_message);
			// save space for the header, filled in later
			MSG_WriteLong(&net_message, 0);
			MSG_WriteByte(&net_message, CCREP_RULE_INFO);
			if (var)
			{
				MSG_WriteString(&net_message, var->name);
				MSG_WriteString(&net_message, var->string);
			}
			*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
			iceapi.SendPacket(ice, net_message.data, net_message.cursize);
			SZ_Clear(&net_message);
		}
		else if (req == CCREQ_RCON)
			;
	}
}

static void(*qice_servercb)(qsocket_t *);	//evil global pointer passing.
static qboolean QICE_GotC2SMessage (struct icestate_s *ice, const void *data, size_t datasize)
{	//returns false when the ice state got destroyed...
	unsigned int header;
	qsocket_t *s = NULL;

	header = BigLong(((const int*)data)[0]);
	if (header == 0xffffffff)
		return true;	//quakeworld connectionless packets.
	if (header == (datasize | NETFLAG_CTL))
	{
		QCICE_ReadUnsolicitedSPacket(ice, data, datasize, NULL);
		return true;
	}

	//this sucks!
	for (s = net_activeSockets; s; s = s->next)
		if (s->driverdata2 == ice)
			break;
	if (!s)
		return false;	//no idea who this is... did they try sneaking past the password checks?

	SZ_Clear(&net_message);
	QICE_ProcessMessage(header, ice, s, data, datasize, &net_message);
	if (net_message.cursize && qice_servercb)
		qice_servercb(s);
	return !s->disconnected;
}
void NQICE_GetAnyMessages(void(*callback)(qsocket_t *))
{	//poll the server's sockets, figure out what connection they're from. decode dgram header to handle reliable/unreliables/ccrep
	//should already have called ICE_CheckNewConnections, so no need to poll brokers.

	qice_connection_t *b = qice_hostcon;
	qsocket_t *s;

	if (!b)
		return;	//not listening.

	net_message.cursize = 0;
	qice_servercb = callback;
	iceapi.ProcessModule(&b->icemodule);
	qice_servercb = NULL;

	for (s = net_activeSockets; s; s = s->next)
	{
		if (s->driverdata == b)
		{
		   //handle nq's reliable resends
			if ((net_time - s->lastSendTime) > 1.0)
				s->sendNext = true;
			if (s->sendNext)
				QICE_ResendReliable(s);
		}
	}
}

void NQICE_Listen (qboolean state)	//used by server (enables websocket connection).
{
	//connect/disconnect the websocket connection etc.
	qice_listening = true;
	if (qice_listening)
	{
		qice_hostcon = QICE_Setup(*sv_port_rtc.string?sv_port_rtc.string:NULL, true);
		qice_hostcon->icemodule.ReadGamePacket = QICE_GotC2SMessage;
		qice_hostcon->icemodule.ReadUnsolicitedPacket = QCICE_ReadUnsolicitedSPacket;
	}
	else if (qice_hostcon)
	{
		QICE_Close(qice_hostcon);
		qice_hostcon = NULL;
	}
}



int NQICE_SendMessage (qsocket_t *sock, sizebuf_t *data)
{	//FIXME: add nq's datagram header... handle resends
	//-1 if fatal, 1 if success.

	qice_connection_t *b = sock->driverdata;

	if (!b)
		return -1;	//no broker? that's bad.
	else if (sock->driverdata2)
		;	//socket has ice state, allow it to continue to run even if the broker has issues.
	else if (b->error)
		return -1;

	if (!sock->canSend)
		return -1;	//err, you shouldn't be here.
	if (sock->sendMessageLength)
		return 0;	//gotta flush it still.
	if (sizeof(sock->sendMessage) < data->cursize)
		return -1;	//oversized.
	memcpy(sock->sendMessage, data->data, data->cursize);
	sock->sendMessageLength = data->cursize;
	sock->canSend = false;

	//can resize at the start of each reliable. stoopid acks.
	sock->max_datagram = sock->pending_max_datagram;

	//make a guess about dtls+sctp overhead
	sock->max_datagram -= 25+28;

	return QICE_ResendReliable(sock);
}
int NQICE_SendUnreliableMessage (qsocket_t *sock, sizebuf_t *data)
{
	qice_connection_t *b = sock->driverdata;
	struct icestate_s *ice = sock->driverdata2;

	if (!b || b->error)
		return -1;
	if (b && !b->error && ice)
	{
		neterr_t err;
		struct
		{
			unsigned int length;
			unsigned int sequence;
			qbyte data[1];
		} *pkt;

		if (data->cursize > 0xffff)
			return NETERR_MTU;	//this is the limit of the nq netchan we're using.
		if (!ice)	//still waiting for it to become available? don't crash.
			return NETERR_CLOGGED;

		pkt = alloca(NET_HEADERSIZE + data->cursize);
		pkt->length = BigLong((NET_HEADERSIZE+data->cursize) | NETFLAG_UNRELIABLE);
		pkt->sequence = BigLong(sock->unreliableSendSequence++);
		memcpy(pkt->data, data->data, data->cursize);

		err = iceapi.SendPacket(ice, pkt, NET_HEADERSIZE+data->cursize);
		if (err == NETERR_CLOGGED)
			return 0;
		if (err == NETERR_SENT)
			return 1; //yay
		return -1;	//fatal
	}
	return 0;	//not doable yet
}
qboolean NQICE_CanSendMessage (qsocket_t *sock)
{
	qice_connection_t *b = sock->driverdata;
	struct icestate_s *s = sock->driverdata2;
	neterr_t e;

	if (!b)
		return true;	//error state...
	if (!s)
		return false;	//broker didn't find a server yet.

	if (b)
	{
		if (b->isserver)
		{
		}
		else if (b->dohandshake)
		{
			if (b->dohandshake > Sys_DoubleTime())
				return false;	//not yet time

			SZ_Clear(&net_message);
			// save space for the header, filled in later
			MSG_WriteLong(&net_message, 0);
			MSG_WriteByte(&net_message, CCREQ_CONNECT);
			MSG_WriteString(&net_message, DGRAM_PROTOCOL_NAME);
			MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
			if (sock->proquake_angle_hack)
			{	/*Spike -- proquake compat. if both engines claim to be using mod==1 then 16bit client->server angles can be used. server->client angles remain 16bit*/
				char *e;
				int pwd;
				extern cvar_t password;
				if (!*password.string || !strcmp(password.string, "none"))
					pwd = 0;	//no password specified, assume none.
				else
				{
					pwd = strtol(password.string, &e, 0);
					if (*e)	//something trailing = not a numer = hash it and send that.
						pwd = Com_BlockChecksum(password.string, strlen(password.string));
				}

				Con_DWarning("Attempting to use ProQuake angle hack\n");
				MSG_WriteByte(&net_message, 1); /*'mod', 1=proquake*/
				MSG_WriteByte(&net_message, 34); /*'mod' version*/
				MSG_WriteByte(&net_message, 0); /*flags*/
				MSG_WriteLong(&net_message, pwd); /*password*/

				//FTE adds a 'getchallenge' hint here for a challenge response instead of nq protocols. QW or DP would expect only a getchallenge.
			}
			*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
			e = iceapi.SendPacket(s, net_message.data, net_message.cursize);
			SZ_Clear(&net_message);

			if (e == NETERR_NOROUTE)
				return false;	//no connection yet...
			if (e == NETERR_CLOGGED)
				return false;	//didn't send for some reason (dtls/sctp still pending?). don't spam and try again soon.

			Con_Printf("still trying...\n");
			b->dohandshake = Sys_DoubleTime() + 3;	//try again in 3 secs. we're trying to avoid dupes.

			return false;	//wait for the server to ack it.
		}
	}

	return sock->canSend;
}
qboolean NQICE_CanSendUnreliableMessage (qsocket_t *sock)
{
	qice_connection_t *b = sock->driverdata;
	struct icestate_s *s = sock->driverdata2;
	if (!b || !s)
		return false;	//broker didn't find a server yet.

	return true;	//find out later...
}
void NQICE_Close (qsocket_t *sock)
{
	qice_connection_t *b = sock->driverdata;
	struct icestate_s *ice = sock->driverdata2;
	qsocket_t *s;
	int i;

	if (ice)
	{	//kill the connection.
		iceapi.Close(ice, true);

		for (s = net_activeSockets; s; s = s->next)
			if (s->driverdata2 == ice)
				break;
		if (s)
			s->driverdata2 = NULL;	//detach it.

		//if it was a client then kill that reference (should only be one)
		b->ice = NULL;
		b->qsock = NULL;

		//if we're the server then forget any references to the state here too (and make sure we don't just create a new qsock).
		for (i = 0; i < b->numclients; i++)
		{
			if (ice == b->clients[i].ice)
				b->clients[i].ice = NULL;
		}
	}
	if (b && b != qice_hostcon)
		QICE_Close(b);	//close the broker too when its a client (server socket persists beyond a single client dropping)
	sock->driverdata = NULL;
	sock->driverdata2 = NULL;
}
void NQICE_Shutdown (void)
{
	int i;
	NQICE_Listen(false);	//just in case.

	qice_msgqueuesize = 0;
	for (i = 0; i < countof(qice_msgqueue); i++)
	{
		if (!qice_msgqueue[i].data)
			continue;
		qice_msgqueue[i].maxsize = 0;
		Z_Free(qice_msgqueue[i].data);
		qice_msgqueue[i].data = NULL;
	}
}



#if defined(__linux__) || defined(__bsd__) || defined(__POSIX__)/*NOT true, but a better guess than failing completely*/
#include <fcntl.h>
qboolean Sys_RandomBytes(unsigned char *out, int len)
{
	qboolean res = false;
	int fd = open("/dev/urandom", 0);
	if (fd >= 0)
	{
		res = (read(fd, out, len) == len);
		close(fd);
	}

	if (!res)
	{	//failed somehow? fill it with libc randoms. sucks. hopefully srand(time()) was used and kept cycled randomly...
		static qboolean warned;
		if (!warned)
			warned = true, Con_Printf("/dev/urandom failed, falling back to low-quality random.\n");

		while (len --> 0)
			out[len] = rand();
	}
	return res;
}
#elif defined(_WIN32)
#include <wincrypt.h>
qboolean Sys_RandomBytes(unsigned char *out, int len)
{
	qboolean ret = true;
	HCRYPTPROV  prov;
	if(CryptAcquireContext( &prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
	{
		ret = !!CryptGenRandom(prov, len, (BYTE *)out);
		CryptReleaseContext(prov, 0);
	}
	if (!ret)
	{	//failed? oh noes.
		static qboolean warned;
		if (!warned)
			warned = true, Con_Printf("Sys_RandomBytes failed, falling back to low-quality random.\n");

		//FIXME
		while (len --> 0)
			out[len] = rand();
	}
	return ret;
}
#else
qboolean Sys_RandomBytes(unsigned char *out, int len)
{
	static qboolean warned;
	if (!warned)
		warned = true, Con_Printf("Sys_RandomBytes has no true implementation on this platform yet.\n");

	//FIXME
	while (len --> 0)
		out[len] = rand();

	return false;	//not cryptographically random
}
#endif
unsigned int Sys_Milliseconds(void)
{
	return Sys_DoubleTime()*1000;
}
const char *COM_ParseOut(const char *str, char *outbuf, size_t outbuf_sz)
{
	str = COM_Parse(str);
	q_strlcpy(outbuf, com_token, outbuf_sz);
	return str;
}
