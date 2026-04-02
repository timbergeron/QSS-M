//This file acts as an adaptor for including fte's net_ice.c in qss
#ifndef ICE_PRIVATE_H
#define ICE_PRIVATE_H


#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#if defined(_MSC_VER)
	#include <malloc.h>
	#ifndef alloca
		#define alloca _alloca
	#endif
	#ifndef strcasecmp
		#define strcasecmp _stricmp
	#endif
	#ifndef strncasecmp
		#define strncasecmp _strnicmp
	#endif
	#include <stddef.h>
	typedef ptrdiff_t ssize_t;
#endif
#define qboolean int
typedef unsigned char qbyte;

#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>

	#define NET_ERRNO() (WSAGetLastError())
	#define NET_EWOULDBLOCK		WSAEWOULDBLOCK
	#define NET_EINTR			WSAEINTR
	#define NET_ENOTCONN		WSAENOTCONN
	#define NET_ECONNABORTED	WSAECONNABORTED
	#define NET_ETIMEDOUT		WSAETIMEDOUT
	#define NET_EINPROGRESS		WSAEINPROGRESS
	#define NET_ECONNREFUSED	WSAECONNREFUSED
	#define NET_ECONNRESET		WSAECONNRESET
	#define NET_ENETUNREACH		WSAENETUNREACH
	#define NET_EADDRNOTAVAIL	WSAEADDRNOTAVAIL
	#define NET_EACCES			WSAEACCES
	#define	NET_EAFNOSUPPORT	WSAEAFNOSUPPORT

	#define SOCK_CLOEXEC 0
	#define MSG_NOSIGNAL 0
#else
	//'bsd' sockets is common to basically all current unicies.
	#include <unistd.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <netinet/tcp.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <errno.h>
	#include <sys/ioctl.h>
	#define ioctlsocket ioctl
	#define closesocket close
	#define SOCKET int
	#define INVALID_SOCKET (-1)
	#define NET_ERRNO()			(errno)
	#define NET_EWOULDBLOCK		EWOULDBLOCK
	#define NET_EINTR			EINTR
	#define NET_ENOTCONN		ENOTCONN
	#define NET_ECONNABORTED	ECONNABORTED
	#define NET_ETIMEDOUT		ETIMEDOUT
	#define NET_EINPROGRESS		EINPROGRESS
	#define NET_ECONNREFUSED	ECONNREFUSED
	#define NET_ECONNRESET		ECONNRESET
	#define NET_ENETUNREACH		ENETUNREACH
	#define NET_EADDRNOTAVAIL	EADDRNOTAVAIL
	#define NET_EACCES			EACCES
	#define	NET_EAFNOSUPPORT	EAFNOSUPPORT

	#ifndef SOCK_CLOEXEC
		#define SOCK_CLOEXEC 0
	#endif
	#ifndef MSG_NOSIGNAL
		#define MSG_NOSIGNAL 0
	#endif

	#ifndef GNUTLS_STATIC
		#define GNUTLS_STATIC //hardlink gnutls, instead of dlopening it.
	#endif
#endif

#define BigShort(i) (short)ntohs(i)
#define BigLong(i) (int)ntohl(i)

#if (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1))
	#define FTE_DEPRECATED  __attribute__((__deprecated__))	//no idea about the actual gcc version
	#if defined(_WIN32)
		#include <stdio.h>
		#ifdef __MINGW_PRINTF_FORMAT
			#define LIKEPRINTF(x) __attribute__((format(__MINGW_PRINTF_FORMAT,x,x+1)))
		#else
			#define LIKEPRINTF(x) __attribute__((format(ms_printf,x,x+1)))
		#endif
	#else
		#define LIKEPRINTF(x) __attribute__((format(printf,x,x+1)))
	#endif
#endif
#ifndef LIKEPRINTF
#define LIKEPRINTF(x)
#endif

//print colouring
#define S_COLOR_GRAY	"" //for info
#define S_COLOR_GREEN	"" //things that are good
#define S_COLOR_YELLOW	"" //things that may be an issue
#define S_COLOR_RED		"" //things that are bad.
#define CON_WARNING		"" //for major warnings
#define CON_ERROR		"" //for even bigger warnings...
#define CON_DEFAULT		"" //sets it back to white.

#define SUPPORT_ICE		//kinda the whole point...
#define HAVE_TURN		//enables use of TURN relays, when needed.
#define HAVE_TCP		//kinda need this for websockets.
#define HAVE_IPV4		//might as well...
#define HAVE_IPV6		//when possible...
#ifdef USE_OPENSSL
	#define HAVE_OPENSSL
#elif defined(USE_GNUTLS)
	#define HAVE_GNUTLS
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	#define HAVE_TLS	//for securing the broker connection.
	#define HAVE_DTLS	//needed for webrtc.
	#define HAVE_SCTP	//needed for webrtc. otherwise sucks.
#endif
#define HAVE_JSON		//fte's browser port insisted on json... grr. native ports tend to send it too.

#if defined(__unix__) && !defined(HAVE_POLL)
	#define HAVE_POLL	//avoids fd limit issues.
#endif

#define countof(x) (sizeof(x)/sizeof((x)[0]))
#define STRINGIFY2(x) #x
#define STRINGIFY(x) STRINGIFY2(x)

//hash functions.
#define DIGEST_MAXSIZE	(512/8)	//largest valid digest size, in bytes
typedef struct
{
	unsigned int digestsize;
	unsigned int contextsize;	//you need to alloca(te) this much memory...
	void (*init) (void *context);
	void (*process) (void *context, const void *data, size_t datasize);
	void (*terminate) (unsigned char *digest, void *context);
} hashfunc_t;
//its annoying how many different types of hashes we need for different things.
extern hashfunc_t hash_md5;			//required for turn, otherwise deprecated
extern hashfunc_t hash_sha1;		//required for websockets. probably should be deprecated.
extern hashfunc_t hash_sha2_224;	//potentially used by webrtc. probably should be deprecated.
extern hashfunc_t hash_sha2_256;	//required for webrtc
extern hashfunc_t hash_sha2_384;	//potentially used by webrtc
extern hashfunc_t hash_sha2_512;	//potentially used by webrtc
#define hash_certfp hash_sha2_256	//This is the hash function we're using to compute *fp serverinfo. we can detect 1/2-256/2-512 by sizes, but we need consistency to avoid confusion in clientside things too.

//burried in the sha1 file, but kept generic.
size_t CalcHMAC(const hashfunc_t *hashfunc, unsigned char *digest, size_t maxdigestsize,
				 const unsigned char *data, size_t datalen,
				 const unsigned char *key, size_t keylen);
size_t CalcHash(const hashfunc_t *func, unsigned char *digest, size_t maxdigestsize, const unsigned char *string, size_t stringlen);	//simple helper.

typedef enum
{
	NETERR_SENT,
	NETERR_CLOGGED,
	NETERR_DISCONNECTED,
	NETERR_MTU,
	NETERR_NOROUTE,
	NETERR_NQIO	//haxx...
} neterr_t;

typedef enum {
	NA_INVALID,
	NA_IP,
	NA_IPV6,
} netadrtype_t;
typedef enum {
	NP_DGRAM,
	NP_DTLS,
	NP_STREAM,
	NP_TLS,
} netadrprot_t;
typedef struct
{
	netadrtype_t type;	//swap for sa_addressfamily
	netadrprot_t prot;	//remove...
	int connum;	//remove...
	union
	{
		struct sockaddr sa;
		struct sockaddr_in in;
		struct sockaddr_in6 in6;
		struct sockaddr_storage ss;
	};
} netadr_t;	//fixme: just use sockaddr_storage instead.

struct icecandinfo_s
{
	char candidateid[64];
	char addr[64];		//v4/v6/fqdn. fqdn should prefer ipv6
	int port;			//native endian...
	int transport;		//0=udp. other values not supported
	int foundation;		//to figure out...
	int component;		//1-based. allows rtp+rtcp in a single ICE... we only support one.
	int priority;		//some random value...
	enum
	{
		ICE_HOST=0,
		ICE_SRFLX=1,	//Server Reflexive (from stun, etc)
		ICE_PRFLX=2,	//Peer Reflexive
		ICE_RELAY=3,
	} type;				//says what sort of proxy is used.
	char reladdr[64];	//when proxied, this is our local info
	int relport;
	int generation;		//for ice restarts. starts at 0.
	int network;		//which network device this comes from.
};
enum iceproto_e
{
	ICEP_INVALID,	//not allowed..
	ICEP_SERVER,	//we're server side
	ICEP_CLIENT,	//we're client side
	ICEP_VOICE,		//speex. requires client.
	ICEP_VIDEO		//err... REALLY?!?!?
};

#define ICEF_ALLOW_PROBE	(1<<0)	//rfc5245. holepunches. more likely to succeed
#define ICEF_ALLOW_WEBRTC	(1<<1)	//IP+UDP+((ICE/STUN/SDP)+(DTLS+SCTP))... lots of layers that add overheads and extra connection delays, but does provide encryption(as long as the broker is secure), as well as compat with web browsers.
#define ICEF_ALLOW_PLAIN	(1<<2)	//may fall back to not using dtls.
#define ICEF_ALLOW_STUN		(1<<3)	//may send stun packets.
#define ICEF_ALLOW_TURN		(1<<4)	//may connect to turn relays.
#define ICEF_ALLOW_MDNS		(1<<5)	//may send multicast dns packets instead of sharing private ips.
#define ICEF_SHARE_PRIVATE	(1<<6)	//share private ips
#define ICEF_RELAY_ONLY		(1<<7)	//do not send packets directly, but only via relays. fail if there's no relay available. this prevents the target from seeing our public ip, but the relay still can.
#define ICEF_INITIATOR		(1<<8)	//the initiator is the side expected to send the sdp offer (the client part of dtls+sctp, though not necessarily the client of the final game protocol, eg a server admin inviting a player to their game)
#define ICEF_VERBOSE		(1<<9)	//print status/state changes.
#define ICEF_VERBOSE_PROBE	(1<<10)	//print for each probe. spammy.

#define ICEF_DEFAULT (ICEF_ALLOW_PROBE|ICEF_ALLOW_WEBRTC|ICEF_ALLOW_PLAIN|ICEF_ALLOW_STUN|ICEF_ALLOW_TURN|ICEF_ALLOW_MDNS)

enum icestate_e
{
	ICE_INACTIVE,	//idle.
	ICE_FAILED,
	ICE_GATHERING,
	ICE_CONNECTING,	//exchanging pings.
	ICE_CONNECTED	//media is flowing, supposedly. sending keepalives.
};
struct icestate_s;

struct icemodule_s
{	//callbacks/state for the ICE code to call. expected to be static or something.
#define MAX_NETWORKS 16
	qboolean (*ReadGamePacket) (struct icestate_s *ice, const void *data, size_t datasize); //ICE routed a game packet. sctp potentially packs multiple game packets into a single sctp packet so be prepared to handle multiple per ProcessPacket.
	void (*ReadUnsolicitedPacket) (struct icestate_s *temp, const void *data, size_t datasize, struct icestate_s*(*Accept)(struct icestate_s *ice));	//received a non-ice packet. you can still reply via iceapi.SendPacket. Call the callback if you want to make it permanent.
	void (*SendInitial) (struct icemodule_s *module, struct icestate_s *ice);	//ice state completed. send any game handshakes now that they should be able to get through.
	void (*ClosedState) (struct icemodule_s *module, struct icestate_s *ice);	//an ice state timed out or was otherwise destroyed

	struct icesocket_s *conn[MAX_NETWORKS];
	int setupudpport;	// for lazy retry if sockets fail at init
	int setuptcpport;
	int srflx_port;		// if set, override srflx candidate port (for NAT port mapping)

	//private ICE state.
	struct icemodule_s *next;
	const struct dtlsfuncs_s *dtlsfuncs;

	//for stun - cached server-reflexive addresses (one per address family)
	netadr_t srflx[2];	//ipv4, ipv6

	//for mdns
	char mdns_name[43];
};

typedef struct
{
	struct icestate_s *(*Create)(struct icemodule_s *module, const char *conname, const char *peername, unsigned int modeflags/*ICEF_**/, enum iceproto_e proto);	//doesn't start pinging anything.
	qboolean (*Set)(struct icestate_s *con, const char *prop, const char *value);
	qboolean (*Get)(struct icestate_s *con, const char *prop, char *value, size_t valuesize);
	struct icecandinfo_s *(*GetLCandidateInfo)(struct icestate_s *con);		//retrieves candidates that need reporting to the peer.
	void (*AddRCandidateInfo)(struct icestate_s *con, struct icecandinfo_s *cand);		//stuff that came from the peer.
	void (*Close)(struct icestate_s *con, qboolean force);	//bye then.
	void (*CloseModule)(struct icemodule_s *module);	//closes all unclosed connections, with warning.
	qboolean (*GetLCandidateSDP)(struct icestate_s *con, char *out, size_t valuesize);		//retrieves candidates that need reporting to the peer.
	struct icestate_s *(*Find)(struct icemodule_s *module, const char *conname);

	void (*ProcessModule)(struct icemodule_s *module);
	qboolean (*ProcessPacket) (struct icemodule_s *module, struct icesocket_s *srcnetwork, netadr_t *srcaddr, void *data, size_t datasize);	//could be stun, turn, sctp, dtls, etc. also has to figure out which peer its from.
	neterr_t (*SendPacket) (struct icestate_s *con, const void *data, size_t datasize);	//figures out which route to follow
} icefuncs_t;
extern icefuncs_t iceapi;	//main thing to use.

//ice_main.c
const char *ICE_GetConnName(struct icestate_s *ice);
qboolean ICE_SetFailed(struct icestate_s *con, const char *reasonfmt, ...) LIKEPRINTF(2);
void ICE_Debug(struct icestate_s *con);	//prints debugging info about the connection.
void ICE_AddRCandidateInfo(struct icestate_s *con, struct icecandinfo_s *n); //result from mdns.
int ICE_EnumerateAddresses(struct icemodule_s *module, int *out_networks, unsigned int *out_flags, netadr_t *out_addr, const char **out_params, size_t maxresults);	//gathers all addresses from a module.
neterr_t ICE_SendUDPPacket(struct icemodule_s *module, netadr_t *addr, const void *data, size_t datasize);
struct icemodule_s *ICE_FindMDNS(const char *mdnsname);	//so our mdns server can find the right address info.
void *ICE_GetUserPtr(struct icestate_s *ice);	//if you set it, be sure to free it in icemodule_s::ClosedState
void ICE_SetUserPtr(struct icestate_s *ice, void *value);	//if you set it, be sure to free it in icemodule_s::ClosedState

struct dtlslocalcred_s;
void ICE_DePEM(struct dtlslocalcred_s *cred);
size_t Base64_EncodeBlock(const qbyte *in, size_t length, char *out, size_t outsize);
size_t Base64_EncodeBlockURI(const qbyte *in, size_t length, char *out, size_t outsize);

typedef struct
{
	size_t maxsize;
	size_t cursize;
	qbyte *data;
} icebuf_t;
void ICE_WriteClear(icebuf_t *buf, void *data, size_t maxsize);
void ICE_WriteData(icebuf_t *buf, const void *data, size_t datasize);
void ICE_WriteChar(icebuf_t *buf, int8_t i);
void ICE_WriteByte(icebuf_t *buf, uint8_t i);
void ICE_WriteShort(icebuf_t *buf, uint16_t i);
void ICE_WriteLong(icebuf_t *buf, uint32_t i);

//address stuff
enum addressscope_e
{
	ASCOPE_PROCESS=0,	//unusable
	ASCOPE_HOST=1,		//unroutable
	ASCOPE_LINK=2,		//unpredictable
	ASCOPE_LAN=3,		//private
	ASCOPE_NET=4		//aka hopefully globally routable
};
#define ADDR_REFLEX (1<<0) //address info came from STUN instead of local interface queries.

//ice_stream.c
enum
{
	VFS_ERROR_TRYLATER = 0,	//no data yet, or couldn't send it, or w/e
	VFS_ERROR_UNSPECIFIED = -1,
	VFS_ERROR_NORESPONSE = -2,
	VFS_ERROR_REFUSED = -3,
	VFS_ERROR_EOF = -4,
	VFS_ERROR_UNTRUSTED = -5,
	VFS_ERROR_DNSFAILURE = -6,
	VFS_ERROR_NETQUAKEIO = -7,
};
typedef struct icestream_s
{
	qboolean (*Close) (struct icestream_s *file);
	int (*WriteBytes) (struct icestream_s *file, const void *buffer, int bytestoread);
	int (*ReadBytes)  (struct icestream_s *file, void *buffer, int bytestoread);
} icestream_t;
icestream_t *ICE_OpenTCP(const char *name, int defaultport, qboolean assumetls/*used when no scheme specified*/); //opens a tcp:// or ws:SUBPROTO:// stream.
struct icesocket_s *TURN_TCP_EstablishConnection(const char *address, netadr_t *adr, qboolean usetls); //special tcp-backed socket for STUN/TURN responses (reads the STUN header to return individual 'packets' properly)
struct icesocket_s *ICE_WSS_EstablishConnection(const char *address, netadr_t *adr, qboolean usetls);

//ice_socket.c
struct icesocket_s
{	//udp sockets.
	neterr_t (*SendPacket) (struct icesocket_s *s, const netadr_t *addr, const void *data, size_t datasize);
	void (*CheckAccept)(struct icesocket_s *s, struct icemodule_s *module, void(*GotConnection)(struct icemodule_s *module, struct icesocket_s *link, netadr_t *adr));
	int (*RecvPacket) (struct icesocket_s *s, netadr_t *addr, void *data, size_t datasize);
	int (*EnumerateAddresses)(struct icesocket_s *mod, netadr_t *out_addr, size_t maxresults);
	void (*CloseSocket) (struct icesocket_s *s);

	//private.
	SOCKET sock;
	int af;
};
struct icesocket_s *ICE_OpenUDP(netadrtype_t type, int port);
struct icesocket_s *ICE_WrapExistingSocket(SOCKET sock, int af);
struct icesocket_s *ICE_WrapExistingSocketSendOnly(SOCKET sock, int af);
void ICE_SetupModule(struct icemodule_s *module, int udpport, int tcpport);

enum addressscope_e NET_ClassifyAddress(const netadr_t *adr, const char **outdesc);
int ParsePartialIP(const char *s, netadr_t *a);
qboolean NET_CompareAdr (const netadr_t *a, const netadr_t *b);
size_t NET_StringToAdr(const char *address, int port, netadr_t *addr, size_t maxaddr);
char *NET_AdrToString (char *s, int len, const netadr_t *a);
char *NET_BaseAdrToString (char *s, int len, const netadr_t *a);
int NET_AdrToPort (const netadr_t *a);
void SockadrToNetadr (const struct sockaddr_storage *s, int sizeofsockaddr, netadr_t *a);
int NetadrToSockadr (const netadr_t *a, struct sockaddr_storage *s); //returns sizeof(sockaddr_in) or w/e

//ice_mdns.c
qboolean MDNS_Setup(struct icemodule_s *module);
void MDNS_Shutdown(void);
qboolean MDNS_AddQuery(struct icemodule_s *module, struct icestate_s *con, struct icecandinfo_s *can);	//sends a query, pokes the ice state on receipt.
void MDNS_RemoveQueries(struct icestate_s *con);						//removes any dead links if a query is pending when ice is killed.
void MDNS_SendQueries(void);	//processes responses too.

//dynamic library loading (for GNUTLS_DYNAMIC etc)
typedef struct {
	void **funcptr;
	char *name;
} dllfunction_t;
typedef void *dllhandle_t;
dllhandle_t *Sys_LoadLibrary(const char *name, dllfunction_t *funcs);
void *Sys_GetAddressForName(dllhandle_t *hmod, const char *name);
void Sys_CloseLibrary(dllhandle_t *lib);

#ifndef VARGS
#define VARGS
#endif

//imported from quake stuff...
qboolean Sys_RandomBytes(unsigned char *out, int len);	//this function is meant to return cryptographically strong randomness. libc is generally inadequete and may be a security risk. hopefully the engine will define this...
unsigned int Sys_Milliseconds(void);	//ints wrap. use fancy wrappy maffs.
const char *COM_ParseOut(const char *str, char *outbuf, size_t outbuf_sz);


int q_strcasecmp(const char *a, const char *b);
int q_strncasecmp(const char *a, const char *b, size_t dstlen);
size_t q_strlcpy(char *dst, const char *src, size_t dstlen);
size_t q_strlcat(char *dst, const char *src, size_t dstlen);
int q_vsnprintf(char *dst, size_t dstsize, const char *format, va_list args);
int q_snprintf(char *dst, size_t dstsize, const char *format, ...);
void Con_Printf(const char *fmt, ...);
void Con_DPrintf(const char *fmt, ...);
char *va(const char *fmt, ...);
void Cvar_Set(const char *n, const char*v);
double Sys_DoubleTime(void);
unsigned int Sys_Milliseconds(void);
qboolean Sys_RandomBytes(unsigned char *b, int l);
const char *COM_ParseOut(const char *s, char *out, size_t outsz);

//our dtls interface
#ifdef HAVE_DTLS
enum certprops_e;
struct dtlsfuncs_s;
typedef struct dtlscred_s
{
	struct dtlslocalcred_s
	{
		void *cert;
		size_t certsize;
		void *key;
		size_t keysize;
		void *rawcert;
		size_t rawcertsize;
		void *rawkey;
		size_t rawkeysize;
	} local;
	struct dtlspeercred_s
	{
		const char *name;	//cert must match this if specified

		hashfunc_t *hash;	//if set peer's cert MUST match the specified digest (with this hash function)
		qbyte digest[DIGEST_MAXSIZE];
	} peer;
} dtlscred_t;
typedef struct dtlsfuncs_s
{
	void *(*CreateContext)(const dtlscred_t *credinfo, void *cbctx, neterr_t(*push)(void *cbctx, const qbyte *data, size_t datasize), qboolean isserver);	//the certificate to advertise.
	qboolean (*CheckConnection)(void *cbctx, void *peeraddr, size_t peeraddrsize, void *indata, size_t insize, neterr_t(*push)(void *cbctx, const qbyte *data, size_t datasize), void (*EstablishTrueContext)(void **cbctx, void *state));
	void (*DestroyContext)(void *ctx);
	neterr_t (*Transmit)(void *ctx, const qbyte *data, size_t datasize);
	neterr_t (*Received)(void *ctx, netadr_t *from, const void *src_data, size_t src_size, void *out_data, size_t out_maxsize, size_t *out_used);	//operates in-place...
	neterr_t (*Timeouts)(void *ctx);
	int (*GetPeerCertificate)(void *ctx, enum certprops_e prop, char *out, size_t outsize);
	qboolean (*GenTempCertificate)(const char *subject, struct dtlslocalcred_s *cred);
	qboolean (*SetCredentials)(struct dtlslocalcred_s *cred);
} dtlsfuncs_t;
const dtlsfuncs_t *ICE_DTLS_InitServer(void);
const dtlsfuncs_t *ICE_DTLS_InitClient(void);
#endif
#ifdef HAVE_TLS
icestream_t *ICE_OpenTLS(const char *hostname, icestream_t *source, qboolean isserver);
#endif

//ice_sctp.c
struct sctp_s;
neterr_t SCTP_Transmit(struct sctp_s *sctp, const void *data, size_t length);
void SCTP_Decode(struct sctp_s *sctp, const void *msg_data, size_t msg_size, qboolean (*ReadGamePacket)(struct icestate_s *context, const void *msg_data, size_t msg_size));
struct sctp_s *SCTP_Create(struct icestate_s *ctx, const char *verbosename, unsigned int modeflags, int localport, int remoteport, neterr_t (*SendLowerPacket)(struct icestate_s *context, const void *msg_data, size_t msg_size));
void SCTP_Destroy(struct sctp_s *sctp);

//json.c
#ifdef HAVE_JSON
#define JSON_Parse ICE_JSON_Parse
#define JSON_ParseNode ICE_JSON_ParseNode
#define JSON_FindChild ICE_JSON_FindChild
#define JSON_GetCount ICE_JSON_GetCount
#define JSON_GetIndexed ICE_JSON_GetIndexed
#define JSON_FindIndexedChild ICE_JSON_FindIndexedChild
#define JSON_Destroy ICE_JSON_Destroy
#define JSON_ReadFloat ICE_JSON_ReadFloat
#define JSON_ReadBody ICE_JSON_ReadBody
#define JSON_Equals ICE_JSON_Equals
#define JSON_GetUInteger ICE_JSON_GetUInteger
#define JSON_GetInteger ICE_JSON_GetInteger
#define JSON_GetIndexedInteger ICE_JSON_GetIndexedInteger
#define JSON_GetFloat ICE_JSON_GetFloat
#define JSON_GetIndexedFloat ICE_JSON_GetIndexedFloat
#define JSON_GetString ICE_JSON_GetString
typedef struct json_s
{
	enum
	{
		json_type_string,
		json_type_number,
		json_type_object,
		json_type_array,
		json_type_true,
		json_type_false,
		json_type_null
	} type;
	const char *bodystart;
	const char *bodyend;

	struct json_s *parent;
	struct json_s *child;
	struct json_s *sibling;
	union
	{
		struct json_s **childlink;
		struct json_s **array;
	};
	size_t arraymax;	//note that child+siblings are kinda updated with arrays too, just not orphaned cleanly...
	qboolean used;	//set to say when something actually read/walked it, so we can flag unsupported things gracefully
	char name[1];
} json_t;

json_t *JSON_Parse(const char *json);

struct jsonparsectx_s
{
	char const *const data;
	const size_t size;
	size_t pos;
};
json_t *JSON_ParseNode(json_t *t, const char *namestart, const char *nameend, struct jsonparsectx_s *ctx); //fancy parsing.

json_t *JSON_FindChild(json_t *t, const char *child);
size_t JSON_GetCount(json_t *t);
json_t *JSON_GetIndexed(json_t *t, unsigned int idx);
json_t *JSON_FindIndexedChild(json_t *t, const char *child, unsigned int idx);

void JSON_Destroy(json_t *t);

double JSON_ReadFloat(json_t *t, double fallback);
size_t JSON_ReadBody(json_t *t, char *out, size_t outsize);

qboolean JSON_Equals(json_t *t, const char *child, const char *expected);
uintptr_t JSON_GetUInteger(json_t *t, const char *child, unsigned int fallback);
intptr_t JSON_GetInteger(json_t *t, const char *child, int fallback);
intptr_t JSON_GetIndexedInteger(json_t *t, unsigned int idx, int fallback);
double JSON_GetFloat(json_t *t, const char *child, double fallback);
double JSON_GetIndexedFloat(json_t *t, unsigned int idx, double fallback);
const char *JSON_GetString(json_t *t, const char *child, char *buffer, size_t buffersize, const char *fallback);

#endif

#endif	//ICE_PRIVATE_H
