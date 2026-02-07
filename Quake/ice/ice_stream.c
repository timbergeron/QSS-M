//This file handles TCP and WebSocket things.
//TCP is a fairly simple pipe but we abstract it anyway.
//TLS can be used as an extra layer, via a third party library.
//WebSockets require extra handshakes so take a little more time to connect.
//We have a hack for compat with netquake.io, so netquake.io can connect to us. that happens according to protocol. disable the defines below.

#include "ice_private.h"

#if 1
	//both qss and fteqw just use websocket packets as an alternative to udp packets, including the initial ccreq etc handshake.
	#define WEBSOCKET_SUBPROTOCOL "fteqw"
	//there's no wasm port of qss so we'll just link to fte's web port instead.
	#define WEBPORT_SITE "https://fte.triptohell.info/moodles/web/ftewebgl.html?+connect%%20"
	//for people who insist that me pushing a specific server upon them is malicious... register your own client as a handler for the following uri scheme.
	#define REDIRECT_SCHEME "quake"
#endif
#if 1
	//netquake.io is also a thing. but buggy
	//note: netquake.io normally expects a -game=foo arg too. also, no support for webrtc.
	#define NETQUAKEIO_SITE "https://www.netquake.io/quake?-connect="

	//WARNING: netquake.io cannot support fragmentation.
	#define NETQUAKE_IO_HACK "quake"	//note: netquake.io claims 'quake' but instead of tunnelling quake's 'datagram' packets over websockets it instead does its own headers and skips handshakes etc.
#elif 0
	//some sort of emscripten-specific mess.
	#define WEBSOCKET_SUBPROTOCOL "binary"
	#define WEBPORT_SITE "https://qwasm.m-h.org.uk?+connect%%20"
#endif

#ifdef HAVE_POLL
#include <sys/poll.h>	//doesn't have issues with fdset limits. will fall back on select when not available.
#endif

//abstraction over tcp
static int TCP_IsStillConnecting(SOCKET sock)
{
#ifdef HAVE_POLL
	//poll has no arbitrary fd limit. use it instead of select where possible.
	struct pollfd ourfd[1];
	ourfd[0].fd = sock;
	ourfd[0].events = POLLOUT;
	ourfd[0].revents = 0;
	if (!poll(ourfd, countof(ourfd), 0))
	{
		if (ourfd[0].revents & POLLERR)
			return VFS_ERROR_UNSPECIFIED;
		if (ourfd[0].revents & POLLHUP)
			return VFS_ERROR_REFUSED;
		return true;	//no events yet.
	}
#else
	//okay on windows where sock+1 is ignored, has issues when lots of other fds are already open (for any reason).
	fd_set fdw, fdx;
	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	FD_ZERO(&fdw);
	FD_SET(sock, &fdw);
	FD_ZERO(&fdx);
	FD_SET(sock, &fdx);
	//check if we can actually write to it yet, without generating weird errors...
	if (!select((int)sock+1, NULL, &fdw, &fdx, &timeout))
		return true;
#endif

	//if we get here then its writable(read: connected) or failed.

//	int error = NET_ENOTCONN;
//	socklen_t sz = sizeof(error);
//	if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &sz))
//		error = NET_ENOTCONN;
	return false;
}
typedef struct {
	icestream_t funcs;

	SOCKET sock;
	qboolean conpending;
	int readaborted;	//some kind of error. don't spam
	int writeaborted;	//some kind of error. don't spam

	char readbuffer[65536];
	int readbuffered;
	char peer[1];
} tcpstream_t;
static int TCP_ReadBytes (struct icestream_s *file, void *buffer, int bytestoread)
{
	tcpstream_t *tf = (tcpstream_t*)file;
	int len;
	int trying;

	if (tf->conpending)
	{
		trying = TCP_IsStillConnecting(tf->sock);
		if (trying < 0)
		{
			tf->readaborted = trying;
			tf->writeaborted = true;
		}
		else if (trying)
			return 0;
		tf->conpending = false;
	}

	if (!tf->readaborted)
	{
		trying = sizeof(tf->readbuffer) - tf->readbuffered;
		if (bytestoread > 1500)
		{
			if (trying > bytestoread)
				trying = bytestoread;
		}
		else
		{
			if (trying > 1500)
				trying = 1500;
		}
		len = recv(tf->sock, tf->readbuffer + tf->readbuffered, trying, 0);
		if (len == -1)
		{
			int e = NET_ERRNO();
			if (e != NET_EWOULDBLOCK && e != NET_EINTR)
			{
				tf->readaborted = VFS_ERROR_UNSPECIFIED;
				switch(e)
				{
				case NET_ENOTCONN:
					Con_Printf("connection to \"%s\" failed\n", tf->peer);
					tf->readaborted = VFS_ERROR_NORESPONSE;
					tf->writeaborted = true;
					break;
				case NET_ECONNABORTED:
					Con_DPrintf("connection to \"%s\" aborted\n", tf->peer);
					tf->readaborted = VFS_ERROR_NORESPONSE;
					tf->writeaborted = true;
					break;
				case NET_ETIMEDOUT:
					Con_Printf("connection to \"%s\" timed out\n", tf->peer);
					tf->readaborted = VFS_ERROR_NORESPONSE;
					tf->writeaborted = true;
					break;
				case NET_ECONNREFUSED:
					Con_DPrintf("connection to \"%s\" refused\n", tf->peer);
					tf->readaborted = VFS_ERROR_REFUSED;
					tf->writeaborted = true;
					break;
				case NET_ECONNRESET:
					Con_DPrintf("connection to \"%s\" reset\n", tf->peer);
					break;
				default:
					Con_Printf("tcp socket error %i (%s)\n", e, tf->peer);
				}
			}
			//fixme: figure out wouldblock or error
		}
		else if (len == 0 && trying != 0)
		{
			//peer disconnected
			tf->readaborted = VFS_ERROR_EOF;
		}
		else
		{
			tf->readbuffered += len;
		}
	}

	//return a partially filled buffer.
	if (bytestoread > tf->readbuffered)
		bytestoread = tf->readbuffered;
	if (bytestoread < 0)
		return VFS_ERROR_UNSPECIFIED;	//caller error...

	if (bytestoread > 0)
	{
		memcpy(buffer, tf->readbuffer, bytestoread);
		tf->readbuffered -= bytestoread;
		memmove(tf->readbuffer, tf->readbuffer+bytestoread, tf->readbuffered);
		return bytestoread;
	}
	else return tf->readaborted;
}
static int TCP_WriteBytes (struct icestream_s *file, const void *buffer, int bytestoread)
{
	tcpstream_t *tf = (tcpstream_t*)file;
	int len;

	if (tf->writeaborted)
		return VFS_ERROR_UNSPECIFIED;	//a previous write failed.

	if (tf->conpending)
	{
		len = TCP_IsStillConnecting(tf->sock);
		if (len < 0)
		{
			tf->writeaborted = true;
			tf->conpending = false;
			return len;
		}
		if (len)
			return 0;
		tf->conpending = false;
	}

	len = send(tf->sock, buffer, bytestoread, MSG_NOSIGNAL);
	if (len == -1 || len == 0)
	{
		int reason = VFS_ERROR_UNSPECIFIED;
		int e = (len==0)?NET_ECONNABORTED:NET_ERRNO();
		switch(e)
		{
		case NET_EINTR:
		case NET_EWOULDBLOCK:
			return 0;	//nothing available yet.
		case NET_ETIMEDOUT:
			Con_Printf("connection to \"%s\" timed out\n", tf->peer);
			tf->writeaborted = true;
			tf->conpending = false;
			return VFS_ERROR_NORESPONSE;	//don't bother trying to read if we never connected.
		case NET_ECONNREFUSED:	//peer sent a reset instead of accepting a new connection
			Con_DPrintf("connection to \"%s\" refused\n", tf->peer);
			tf->writeaborted = true;
			tf->conpending = false;
			return VFS_ERROR_REFUSED;	//don't bother trying to read if we never connected.
		case NET_ECONNABORTED:	//peer closed its socket
			Con_DPrintf("connection to \"%s\" aborted\n", tf->peer);
			reason = len?VFS_ERROR_NORESPONSE:VFS_ERROR_EOF;
			break;
		case NET_ECONNRESET:	//'peer' claims no knowledge (rebooted?) or forcefully closed
			Con_DPrintf("connection to \"%s\" reset\n", tf->peer);
			reason = VFS_ERROR_EOF;
			break;
		case NET_ENOTCONN:
#ifdef NET_EPIPE
		case NET_EPIPE:
#endif
			Con_Printf("connection to \"%s\" failed\n", tf->peer);
			tf->writeaborted = true;
			tf->conpending = false;
			return VFS_ERROR_NORESPONSE;	//don't bother trying to read if we never connected.
		default:
			Con_DPrintf("tcp socket error %i (%s)\n", e, tf->peer);
			break;
		}
//		don't destroy it on write errors, because that prevents us from reading anything that was sent to us afterwards.
//		instead let the read handling kill it if there's nothing new to be read
		TCP_ReadBytes(file, NULL, 0);
		tf->writeaborted = true;
		return reason;
	}
	return len;
}
static qboolean TCP_Close (struct icestream_s *file)
{
	tcpstream_t *f = (tcpstream_t *)file;
	qboolean success = f->sock != INVALID_SOCKET;
	if (f->sock != INVALID_SOCKET)
	{
		closesocket(f->sock);
		f->sock = INVALID_SOCKET;
	}
	free(f);
	return success;
}

icestream_t *FS_WrapTCPSocket(SOCKET sock, qboolean conpending, const char *peername)
{
	tcpstream_t *newf;
	if (sock == INVALID_SOCKET)
		return NULL;

	newf = calloc(1, sizeof(*newf) + strlen(peername));
	strcpy(newf->peer, peername);
	newf->conpending = conpending;
	newf->sock = sock;
	newf->funcs.Close = TCP_Close;
	newf->funcs.ReadBytes = TCP_ReadBytes;
	newf->funcs.WriteBytes = TCP_WriteBytes;

	return &newf->funcs;
}

SOCKET TCP_OpenStream(netadr_t *addr, const char *remotename, qboolean nonagle)
{
#ifdef HAVE_TCP
#ifdef _WIN32
	u_long _true = 1;	//windows specifies an actual type. which comes out as an int32 anyway, but with extra warnings.
#else
	int _true = 1;	//linux seems to expect an int? doesn't matter too much if its treated as a char on little-endian systems.
#endif
	int newsocket;
	size_t addrsize = 0;	//some systems insist on this being correct.
//	struct sockaddr_storage loc;
	int recvbufsize = (1<<19);//512kb
	int sysprot;

	switch(addr->sa.sa_family)
	{
#ifdef HAVE_IPV4
	case AF_INET:
		sysprot = IPPROTO_TCP;
		addrsize = sizeof(struct sockaddr_in);
		break;
#endif
#ifdef HAVE_IPV6
	case AF_INET6:
		sysprot = IPPROTO_TCP;
		addrsize = sizeof(struct sockaddr_in6);
		break;
#endif
#ifdef HAVE_IPX
	case AF_IPX:
		sysprot = NSPROTO_IPX;
		addrsize = sizeof(struct sockaddr_ipx);
		break;
#endif
	//case NA_UNIX:
	default:
		sysprot = 0;	//'auto'
		break;
	}

	if ((newsocket = socket (addr->sa.sa_family, SOCK_CLOEXEC|SOCK_STREAM, sysprot)) == INVALID_SOCKET)
		return (int)INVALID_SOCKET;

	setsockopt(newsocket, SOL_SOCKET, SO_RCVBUF, (void*)&recvbufsize, sizeof(recvbufsize));

	if (nonagle)
		setsockopt(newsocket, IPPROTO_TCP, TCP_NODELAY, (void *)&_true, sizeof(_true));	//keep lag down

	if (ioctlsocket (newsocket, FIONBIO, &_true) == -1)
	{
		Con_Printf (CON_ERROR"TCP_OpenStream: ioctl FIONBIO: %s", strerror(NET_ERRNO()));
		closesocket(newsocket);
		return (int)INVALID_SOCKET;
	}

#ifdef UNIXSOCKETS
	if (addr->sa.sa_family == AF_UNIX)
	{	//if its a unix socket, attempt to bind it to an unnamed address. linux should generate an ephemerial abstract address (otherwise the server will see an empty address).
		struct sockaddr_un un;
		memset(&un, 0, offsetof(struct sockaddr_un, sun_path));
		bind(newsocket, (struct sockaddr*)&un, offsetof(struct sockaddr_un, sun_path));
	}
	else
#endif
	{
//		memset(&loc, 0, sizeof(loc));
//		((struct sockaddr*)&loc)->sa_family = addr->sa.sa_family;
//		bind(newsocket, (struct sockaddr *)&loc, ((struct sockaddr_in*)&loc)->sin_family == AF_INET?sizeof(struct sockaddr_in):sizeof(struct sockaddr_in6));
	}

	if (connect(newsocket, &addr->sa, addrsize) == INVALID_SOCKET)
	{
		int err = NET_ERRNO();
		if (err != NET_EWOULDBLOCK && err != NET_EINPROGRESS)
		{
			if (err == NET_EADDRNOTAVAIL)
			{
				/*if (remoteaddr->port == 0 && (remoteaddr->type == NA_IP || remoteaddr->type == NA_IPV6))
					Con_Printf (CON_ERROR"TCP_OpenStream: no port specified (%s)\n", remotename);
				else*/
					Con_Printf (CON_ERROR"TCP_OpenStream: invalid address trying to connect to %s\n", remotename);
			}
			else if (err == NET_ECONNREFUSED)
				Con_Printf (CON_ERROR"TCP_OpenStream: connection refused (%s)\n", remotename);
			else if (err == NET_EACCES)
				Con_Printf (CON_ERROR"TCP_OpenStream: access denied: check firewall (%s)\n", remotename);
			else if (err == NET_ENETUNREACH)
				Con_Printf (CON_ERROR"TCP_OpenStream: unreachable (%s)\n", remotename);
			else
				Con_Printf (CON_ERROR"TCP_OpenStream: connect: error %i (%s)\n", err, remotename);
			closesocket(newsocket);
			return (int)INVALID_SOCKET;
		}
	}

	return newsocket;
#else
	return (int)INVALID_SOCKET;
#endif
}


/*
==============
websocket

writes send individual frames.
reads return an entire frame. make sure your read buffer is large enough or the frame will be dropped.
==============
*/

typedef struct {
	icestream_t funcs;

	icestream_t *stream;
#ifdef HAVE_TLS
	qboolean allowtls;		//as server, will upgrade to tls if sent a packet that looks more tlsey than httpey
#endif
	qboolean serverwaiting;	//we're waiting for the client's request
	const char *protocol;	//protocol we're asking for.
	int conpending;			//waiting for the proper handshake response, don't send past this to avoid issues on errors. we do accept new data to be sent though, while its still handshaking.
	unsigned int mask;		//xor masking, to make it harder to exploit buggy shit that's parsing streams (like magic packets or w/e).

#ifdef NETQUAKE_IO_HACK
	qboolean netquakeiohack;
	unsigned int netquakeiobug;
#endif

	char readbuffer[65536];
	int readbufferofs;
	int readbuffered;

	char *pending;
	int pendingofs;
	int pendingsize;
	int pendingmax;

	int err;
} websocket_t;
//websocket info
enum websocketpackettype_e
{	//websocket packet types, according to the relevant rfc.
	WS_PACKETTYPE_CONTINUATION=0,
	WS_PACKETTYPE_TEXTFRAME=1,
	WS_PACKETTYPE_BINARYFRAME=2,
	WS_PACKETTYPE_CLOSE=8,
	WS_PACKETTYPE_PING=9,
	WS_PACKETTYPE_PONG=10,
};
static void WS_Flush (websocket_t *f)
{
//try flushing it now. note: tls packet sizes can leak.
	int i = f->conpending?f->conpending:f->pendingsize;
	if (f->serverwaiting)
		return;	//still waiting for the client's message before we can our response.
	if (i == f->pendingofs)
		return;	//nothing to flush.
	if (!f->stream)
		return; //connection failed. we'll be reporting errors.
	i = f->stream->WriteBytes(f->stream, f->pending+f->pendingofs, i-f->pendingofs);
	if (i > 0)
		f->pendingofs += i;
	else if (i < 0)
	{
		f->err = i;
		f->stream->Close(f->stream);	//close it.
		f->stream = NULL;
	}
}
static void WS_Append (websocket_t *f, unsigned packettype, const unsigned char *data, size_t length)
{
	union
	{
		unsigned char b[4];
		int i;
	} mask;
	unsigned short ctrl = 0x8000 | (packettype<<8);
	uint64_t paylen = 0;
	unsigned int payoffs = f->pendingsize;
//	int i;
	if (!f->stream)
		return;	//can't do anything anyway...
	switch((ctrl>>8) & 0xf)
	{
#if 0
	case WS_PACKETTYPE_TEXTFRAME:
		for (i = 0; i < length; i++)
		{
			paylen += (data[i] == 0 || data[i] >= 0x80)?2:1;
		}
		break;
#endif
	case WS_PACKETTYPE_BINARYFRAME:
	default:
		paylen = length;
		break;
	}
	payoffs = 2;	//ctrl header
	if (paylen >= (1<<16))
		ctrl |= 127, payoffs+=8;	//64bit len... overkill
	else if (paylen >= 126)
		ctrl |= 126, payoffs+=2;	//16bit len.
	else
		ctrl |= paylen;	//smol
	if (ctrl&0x80)
		payoffs += 4;	//mask
	payoffs += paylen;

	if (f->pendingmax < f->pendingsize+payoffs)
	{	//oh noes. wouldn't be space
		if (f->pendingofs && !f->conpending/*don't get confused*/)
		{	//move it down, we already sent that bit.
			f->pendingsize -= f->pendingofs;
			memmove(f->pending, f->pending + f->pendingofs, f->pendingsize);
			f->pendingofs = 0;
		}
		if (f->pendingmax < f->pendingsize + payoffs)
		{	//still too big. make the buffer bigger.
			f->pendingmax = f->pendingsize + payoffs;
			f->pending = realloc(f->pending, f->pendingmax);
		}
	}

	payoffs = f->pendingsize;
	f->pending[payoffs++] = ctrl>>8;
	f->pending[payoffs++] = ctrl&0xff;
	if ((ctrl&0x7f) == 127)
	{
		f->pending[payoffs++] = (paylen>>56)&0xff;
		f->pending[payoffs++] = (paylen>>48)&0xff;
		f->pending[payoffs++] = (paylen>>40)&0xff;
		f->pending[payoffs++] = (paylen>>32)&0xff;
		f->pending[payoffs++] = (paylen>>24)&0xff;
		f->pending[payoffs++] = (paylen>>16)&0xff;
		f->pending[payoffs++] = (paylen>> 8)&0xff;
		f->pending[payoffs++] = (paylen>> 0)&0xff;
	}
	else if ((ctrl&0x7f) == 126)
	{
		f->pending[payoffs++] = (paylen>>8)&0xff;
		f->pending[payoffs++] = (paylen>>0)&0xff;
	}
	if (ctrl&0x80)
	{
		mask.i = f->mask;
		//'re-randomise' it a bit
		f->mask = (f->mask<<4) | (f->mask>>(32-4));
		f->mask += (payoffs<<16) + paylen;

		f->pending[payoffs++] = mask.b[0];
		f->pending[payoffs++] = mask.b[1];
		f->pending[payoffs++] = mask.b[2];
		f->pending[payoffs++] = mask.b[3];
	}
	switch((ctrl>>8) & 0xf)
	{
#if 0
	case WS_PACKETTYPE_TEXTFRAME:/*utf8ify the data*/
		for (i = 0; i < length; i++)
		{
			if (!data[i])
			{	/*0 is encoded as 0x100 to avoid safety checks*/
				f->pending[payoffs++] = 0xc0 | (0x100>>6);
				f->pending[payoffs++] = 0x80 | (0x100&0x3f);
			}
			else if (data[i] >= 0x80)
			{	/*larger bytes require markup*/
				f->pending[payoffs++] = 0xc0 | (data[i]>>6);
				f->pending[payoffs++] = 0x80 | (data[i]&0x3f);
			}
			else
			{	/*lower 7 bits are as-is*/
				f->pending[payoffs++] = data[i];
			}
		}
		break;
#endif
	default: //raw data
		memcpy(f->pending+payoffs, data, length);
		payoffs += length;
		break;
	}
	if (ctrl&0x80)
	{
		unsigned char *buf = (unsigned char*)f->pending+payoffs-paylen;
		int i;
		for (i = 0; i < paylen; i++)
			buf[i] ^= mask.b[i&3];
	}
	f->pendingsize = payoffs;

	//try flushing it now. note: tls packet sizes can leak.
	WS_Flush(f);
}
static qboolean WS_Close (struct icestream_s *file)
{
	websocket_t *f = (websocket_t *)file;
	qboolean success = f->stream != NULL;
	WS_Append(f, WS_PACKETTYPE_CLOSE, NULL, 0);	//let the other side know it was intended
	if (f->stream != NULL)
	{	//still open? o.O
		f->stream->WriteBytes(f->stream, f->pending+f->pendingofs, f->pendingsize-f->pendingofs);	//final flush
		success = f->stream->Close(f->stream);	//close it.
		f->stream = NULL;
	}
	free(f->pending);
	f->pending = NULL;
	free(f);
	return success;
}

#ifdef HAVE_TLS
static char *promote_data;	//evil thread-breaking globals! oh noes!
static size_t promote_size;
static int WS_ReadBytes_Promote (struct icestream_s *file, void *buffer, int bytestoread)
{	//promoting to tls... try to feed the data back in...
	if (bytestoread > promote_size)
		bytestoread = promote_size;
	if (bytestoread)
	{
		memmove(buffer, promote_data, bytestoread);
		promote_size -= bytestoread;
		promote_data += bytestoread;
		return bytestoread;
	}
	return VFS_ERROR_TRYLATER;
}
#endif

static char *HTTP_GetToken(char **input)
{
	char *t;
	char *in = *input;
	*input = NULL;
	if (!in)
		return "";

	//skip whitespace at the start
	while (*in == ' ' || *in == '\t')
		in++;

	//see if we can find the end
	for (t = in; *t; t++)
	{
		if (*t == '\n')
		{
			if (t>in && t[-1] == '\r')
				t--;
			break;
		}
		if (*t == ',')
		{
			*input = t+1;	//more tokens available...
			break;
		}
	}

	//trim whitespace from the end, too
	while (t>in && (t[-1] == ' ' || t[-1] == '\t'))
		t--;

	*t = 0;
	return in;
}

static int WS_ReadBytes (struct icestream_s *file, void *buffer, int bytestoread)
{
	websocket_t *f = (websocket_t *)file;
	int r;
	int t;

	WS_Flush(f);	//flush any pending writes.


	for(t = 0; t < 2; t++)
	{
		if (t==1 && !f->err)
		{
			if (f->readbufferofs >= 1024)
			{
				f->readbuffered -= f->readbufferofs;
				memmove(f->readbuffer, f->readbuffer+f->readbufferofs, f->readbuffered);
				f->readbufferofs = 0;
			}
			r = f->stream?f->stream->ReadBytes(f->stream, f->readbuffer+f->readbuffered, sizeof(f->readbuffer)-f->readbuffered):f->err?f->err:VFS_ERROR_UNSPECIFIED;
			if (r > 0)
				f->readbuffered += r;
			if (r < 0 && f->stream)
				f->err = r;
			if (r <= 0)
				return f->err;	//needed more, couldn't get it.
		}

		if (f->conpending)
		{	//look for \r\n\r\n
			char *l, *e, *le;
			char *upg=NULL, *con=NULL, *accept=NULL, *prot=NULL, *key=NULL, *host = NULL;
			if (!t)
				continue;	//read the size

			if (f->serverwaiting)
			{	//FIXME: be prepared to promote to tls if its a tls handshake (leading byte < 32).
#ifdef HAVE_TLS
				if (f->allowtls)
					if (f->readbuffered > 1)
					{
						if (f->readbuffer[0] < 32)
						{	//evil hax to swap the lower layer from tcp to tls.
							struct icestream_s *src = f->stream;
							int (*oldReadBytes)  (struct icestream_s *file, void *buffer, int bytestoread) = src->ReadBytes;

							promote_data = f->readbuffer + f->readbufferofs;
							promote_size = f->readbuffered - f->readbufferofs;
							src->ReadBytes = WS_ReadBytes_Promote;

							f->stream = ICE_OpenTLS(NULL, src, true);
							if (!f->stream)
							{	//it closed it? oops.
								return f->err=VFS_ERROR_UNTRUSTED;
							}

							f->readbufferofs = 0;

							//pull as much remaining data so the tls layer is forced to read all that was pending.
							r = f->stream->ReadBytes(f->stream, f->readbuffer, sizeof(f->readbuffer));
							if (r < 0)
								r = 0;	//err...?
							f->readbuffered = r;

							src->ReadBytes = oldReadBytes;	//restore it now it should have re-read all we buffeed.
						}
						f->allowtls = false;
					}
#endif
				if (f->readbuffered < 5)
					continue; //not nuff data for the basic header
				if (strncmp(f->readbuffer, "GET /", 5))
				{
					f->err = VFS_ERROR_UNSPECIFIED;
					break;
				}
			}
			else
			{
				if (f->readbuffered < 13)
					continue; //not nuff data for the basic header
				if (strncmp(f->readbuffer, "HTTP/1.1 101 ", 13))
				{
					f->err = VFS_ERROR_UNSPECIFIED;
					break;
				}
			}

			l = f->readbuffer;
			e = f->readbuffer+f->readbuffered;
			for(;;)
			{
				for (le = l; le < e && *le != '\n'; le++)
					;
				if (le == e)
					break;	//failed.
				//track interesting lines as we parse.
				if (!strncmp(l, "Upgrade:", 8))
					upg = l+8;
				else if (!strncmp(l, "Connection:", 11))
					con = l+11;
				else if (!strncmp(l, "Sec-WebSocket-Accept:", 21))
					accept = l+21;
				else if (!strncmp(l, "Sec-WebSocket-Key:", 18))
					key = l+18;
				else if (!strncmp(l, "Sec-WebSocket-Protocol:", 23))
					prot = l+23;
				else if (!strncmp(l, "Host:", 5))
					host = l+5;
				if (le[0] == '\n' && le[1] == '\r' && le[2] == '\n')
				{
					qboolean connnection_upgrade = false;
					qboolean upgrade_websocket = false;
					char acceptkey[20*2];
					unsigned char sha1digest[20];
					char padkey[512];

					le += 3;

					while(con)
					{
						l = HTTP_GetToken(&con);
						if (!strcmp(l, "Upgrade"))
							connnection_upgrade = true;
					}
					while(upg)
					{
						l = HTTP_GetToken(&upg);
						if (!strcmp(l, "websocket"))
							upgrade_websocket = true;
					}

					if (accept)
						accept = HTTP_GetToken(&accept);

					//server needs this to reply, client needs this to verify the server isn't just echoing weirdly.
					l = key;
					q_snprintf(padkey, sizeof(padkey), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", HTTP_GetToken(&l));
					Base64_EncodeBlock(sha1digest, CalcHash(&hash_sha1, sha1digest, sizeof(sha1digest), (const qbyte*)padkey, strlen(padkey)), acceptkey, sizeof(acceptkey));

					if (!connnection_upgrade)
						f->err = VFS_ERROR_UNSPECIFIED;	//wrong connection state...
					else if (!upgrade_websocket)
						f->err = VFS_ERROR_UNSPECIFIED;	//wrong type of upgrade...
					else if(f->serverwaiting?!key:(!accept /*|| strcmp(accept, acceptkey) -- FIXME*/)) //client 'needs' to verify it.
						f->err = VFS_ERROR_UNSPECIFIED;	//wrong hash
					else
					{
#ifdef NETQUAKE_IO_HACK
						qboolean netquakeio = false;
#endif
						while(prot)
						{
							l = HTTP_GetToken(&prot);
							if (!strcmp(l, f->protocol))
							{	//found the one we needed.
								prot = l;
								break;
							}
#ifdef NETQUAKE_IO_HACK
							if (!strcmp(l, NETQUAKE_IO_HACK))
								netquakeio = true;
#endif
						}
#ifdef NETQUAKE_IO_HACK
						if (!prot && netquakeio)
							prot = NETQUAKE_IO_HACK;
#endif
						if (!prot)
						{
							if (!f->serverwaiting)
								f->err = VFS_ERROR_UNSPECIFIED;	//fail if the server didn't report get the right protocol
							Con_Printf("websocket connection without usable protocol\n");
						}
						else
							Con_DPrintf("websocket connection using protocol %s\n", prot);
					}

					f->conpending = false;
					f->readbufferofs = le-f->readbuffer;

					if (f->serverwaiting)
					{
						f->serverwaiting = false;
						f->pendingofs = 0;
						if (f->err)
						{
#if 1
							if (!strncmp(f->readbuffer+4, "/ ", 2) && !strcmp(f->protocol, WEBSOCKET_SUBPROTOCOL))
							{
								//most sane sites do http->https redirects. lots of browsers automatically do it too.
								//this also means we MUST use 'wss' here too.
								char *body;
								host = HTTP_GetToken(&host);
								body = va(	"Try one of the following links<br/>"
#ifdef WEBPORT_SITE
											"<a href=\'"	WEBPORT_SITE"wss://%s"		"\'>"	WEBPORT_SITE"wss://%s"		"</a><br/>\r\n"
#endif
#ifdef NETQUAKEIO_SITE
											"<a href=\'"	NETQUAKEIO_SITE"wss://%s"	"\'>"	NETQUAKEIO_SITE"wss://%s"	"</a><br/>\r\n"
#endif
#ifdef REDIRECT_SCHEME
											"<a href=\'"	"web+"REDIRECT_SCHEME"://%s""\'>"	"web+"REDIRECT_SCHEME"://%s""</a><br/>\r\n"
											"<a href=\'"	REDIRECT_SCHEME"://%s"		"\'>"	REDIRECT_SCHEME"://%s"		"</a><br/>\r\n"
#endif
#ifdef WEBPORT_SITE
									,host,host
#endif
#ifdef NETQUAKEIO_SITE
									,host,host
#endif
#ifdef REDIRECT_SCHEME
									,host,host,host,host
#endif
									);
								f->pendingsize = q_snprintf(f->pending, f->pendingmax,
									"HTTP/1.1 404 Not Found\r\n"
									"Content-Type: text/html\r\n"
									"Content-Length: %i\r\n"
									"\r\n%s", strlen(body), body);
							}
							else
#endif
							{
								char *body = va("Websocket(%s) Server, not for regular http(s) requests.", f->protocol);
								f->pendingsize = q_snprintf(f->pending, f->pendingmax,
									"HTTP/1.1 404 Not Found\r\n"
									"Content-Type: text/plain\r\n"
									"Content-Length: %i\r\n"
									"\r\n%s", strlen(body), body);
							}
						}
						else
						{
							f->pendingsize = q_snprintf(f->pending, f->pendingmax,
								"HTTP/1.1 101 WebSocket Protocol Handshake\r\n"
								"Upgrade: websocket\r\n"
								"Connection: Upgrade\r\n"
								"Access-Control-Allow-Origin: *\r\n"	//allow cross-origin requests. this means you can use any domain to play on any public server.
								"Sec-WebSocket-Accept: %s\r\n"
								"Sec-WebSocket-Protocol: %s\r\n"
								"\r\n", acceptkey, prot?prot:f->protocol);

#ifdef NETQUAKE_IO_HACK
								if (!strcmp(prot?prot:f->protocol, NETQUAKE_IO_HACK))
								{
									icebuf_t m = {bytestoread, 0, buffer};

									ICE_WriteLong(&m, BigLong((1u<<31) | (5+7+7)));
									ICE_WriteByte(&m, 1);

									ICE_WriteByte(&m, 'Q');ICE_WriteByte(&m, 'U');ICE_WriteByte(&m, 'A');ICE_WriteByte(&m, 'K');ICE_WriteByte(&m, 'E');ICE_WriteByte(&m, 0);
									ICE_WriteByte(&m, 3);

									ICE_WriteByte(&m, 1); /*'mod'*/
									ICE_WriteByte(&m, 0); /*'mod' version*/
									ICE_WriteByte(&m, 0); /*flags*/
									ICE_WriteLong(&m, 0); /*password*/

									f->netquakeiohack = true;
									WS_Flush(f);	//flush any pending writes, so the client knows it can start sending everything else.
									return m.cursize;
								}
#endif
						}

						WS_Flush(f);	//flush any pending writes, so the client knows it can start sending everything else.
					}
					break;
				}
				l = le+1;
			}
			if (f->conpending)
				continue;
			if (f->err)
				break;
			//try and read the next thing.
			t = 0;
			continue;
		}
		else if (!buffer)	//no buffer?... don't try reading... hopefully the caller just tried flushing the output buffer.
			return f->err;
		else if (f->readbuffered-f->readbufferofs >= 2)
		{	//try to make sense of the packet..
			unsigned char *inbuffer = (unsigned char*)f->readbuffer + f->readbufferofs;
			size_t inlen = f->readbuffered-f->readbufferofs;

			unsigned short ctrl = inbuffer[0]<<8 | inbuffer[1];
			unsigned long paylen;
			unsigned int payoffs = 2;
			unsigned int mask = 0;

			if (ctrl & 0x7000)
			{
				f->err = VFS_ERROR_UNSPECIFIED;	//reserved bits set
				break;
			}
			else if ((ctrl & 0x7f) == 127)
			{
				uint64_t ullpaylen;
				//as a payload is not allowed to be encoded as too large a type, and quakeworld never used packets larger than 1450 bytes anyway, this code isn't needed (65k is the max even without this)
				if (sizeof(ullpaylen) < 8)
				{
					f->err = VFS_ERROR_UNSPECIFIED;	//wut...
					break;
				}

				if (payoffs + 8 > inlen)
					continue;	//not enough buffered
				ullpaylen =
					(uint64_t)inbuffer[payoffs+0]<<56u |
					(uint64_t)inbuffer[payoffs+1]<<48u |
					(uint64_t)inbuffer[payoffs+2]<<40u |
					(uint64_t)inbuffer[payoffs+3]<<32u |
					(uint64_t)inbuffer[payoffs+4]<<24u |
					(uint64_t)inbuffer[payoffs+5]<<16u |
					(uint64_t)inbuffer[payoffs+6]<< 8u |
					(uint64_t)inbuffer[payoffs+7]<< 0u;
				if (ullpaylen < 0x10000)
				{
					f->err = VFS_ERROR_UNSPECIFIED;	//should have used a smaller encoding...
					break;
				}
				if (ullpaylen > 0x40000)
				{
					f->err = VFS_ERROR_UNSPECIFIED;	//abusively large...
					break;
				}
				paylen = ullpaylen;
				payoffs += 8;
			}
			else if ((ctrl & 0x7f) == 126)
			{
				if (payoffs + 2 > inlen)
					continue;	//not enough buffered
				paylen =
					inbuffer[payoffs+0]<<8 |
					inbuffer[payoffs+1]<<0;
				if (paylen < 126)
				{
					f->err = VFS_ERROR_UNSPECIFIED;	//should have used a smaller encoding...
					break;
				}
				payoffs += 2;
			}
			else
			{
				paylen = ctrl & 0x7f;
			}
			if (ctrl & 0x80)
			{
				if (payoffs + 4 > inlen)
					continue;
				/*this might read data that isn't set yet, but should be safe*/
				((unsigned char*)&mask)[0] = inbuffer[payoffs+0];
				((unsigned char*)&mask)[1] = inbuffer[payoffs+1];
				((unsigned char*)&mask)[2] = inbuffer[payoffs+2];
				((unsigned char*)&mask)[3] = inbuffer[payoffs+3];
				payoffs += 4;
			}
			/*if there isn't space, try again next time around*/
			if (payoffs + paylen > inlen)
			{
				if (payoffs + paylen >= sizeof(inbuffer)-1)
				{
					f->err = VFS_ERROR_UNSPECIFIED;	//payload is too big for out in buffer
					break;
				}
				continue;	//need more data
			}

			if (mask)
			{
				int i;
				for (i = 0; i < paylen; i++)
				{
					inbuffer[i + payoffs] ^= ((unsigned char*)&mask)[i&3];
				}
			}

			t = 0; //allow checking for new data again.
			f->readbufferofs += payoffs + paylen;	//skip to end...
			switch((ctrl>>8) & 0xf)
			{
			case WS_PACKETTYPE_CLOSE:
				if (!f->err)
				{
					WS_Flush(f);
					f->err = VFS_ERROR_EOF;
					if (f->pendingofs < f->pendingsize)
						return VFS_ERROR_EOF;	//nothing more to read (might still have some to flush).
				}
				break;	//will kill it.
			case WS_PACKETTYPE_CONTINUATION:
				f->err = VFS_ERROR_UNSPECIFIED;	//a prior packet lacked the 'fin' flag. we don't support fragmentation though.
				break;
			case WS_PACKETTYPE_TEXTFRAME:	//we don't distinguish. use utf-8 data if you wanted that.
			case WS_PACKETTYPE_BINARYFRAME:	//actual data
#ifdef NETQUAKE_IO_HACK
				if (f->netquakeiohack)
				{
					if (bytestoread+3 >= paylen)
					{	//caller passed a big enough buffer
						char *d = f->readbuffer+f->readbufferofs-paylen;
						memcpy((char*)buffer+4, d+1, paylen);
						paylen += 3;	//swapping a 1 byte header for 4.
						if (*d == 0)
						{
							*(int*)buffer = BigLong((1u<<20) | paylen);
							((int*)buffer)[1] = BigLong(++f->netquakeiobug);	//overwrite the sequence numbers to work around netquake.io bugs.
						}
						else if (*d == 1)
							*(int*)buffer = BigLong((1u<<16) | (1u<<19) | paylen);
						else if (*d == 2)
							*(int*)buffer = BigLong((1u<<17) | paylen);
						else
							continue;	//don't know what it is, don't bother reading it.
						return paylen;
					}
					Con_Printf("websocket connection received %u-byte package. only %i requested\n", 3+(unsigned)paylen, bytestoread);
				}
				else
#endif
				if (bytestoread >= paylen)
				{	//caller passed a big enough buffer
					memcpy(buffer, f->readbuffer+f->readbufferofs-paylen, paylen);
					return paylen;
				}
				else
					Con_Printf("websocket connection received %u-byte package. only %i requested\n", (unsigned)paylen, bytestoread);
				continue;	//buffer too small... sorry
			case WS_PACKETTYPE_PING:
				WS_Append(f, WS_PACKETTYPE_PONG, (unsigned char*)f->readbuffer+f->readbufferofs-paylen, paylen);	//send it back
				continue;	//and look for more.
			case WS_PACKETTYPE_PONG:	//wut? we didn't ask for this
			default:
				break;
			}
		}
		else
			continue;	//need more data

		break;	//oops?
	}

	if (f->err)
	{	//something bad happened
		if (f->stream)
			f->stream->Close(f->stream);
		f->stream = NULL;
		return f->err;
	}
	return VFS_ERROR_TRYLATER;
}
static int WS_WriteBytes (struct icestream_s *file, const void *buffer, int bytestowrite)
{	//websockets are a pseudo-packet protocol, so queue one packet at a time. there may still be extra data queued at a lower level.
	websocket_t *f = (websocket_t *)file;
	if (!f->stream)
		return f->err;
	if (f->pendingsize-f->pendingofs > 8192 || f->serverwaiting)
		return VFS_ERROR_TRYLATER;	//something pending... don't queue excessively.

#ifdef NETQUAKE_IO_HACK
	if (f->netquakeiohack)
	{
		unsigned int flags = ntohl(*(const int*)buffer);
		char *hackbuf = (char*)buffer + 3;
		buffer = hackbuf;
		bytestowrite-=3;
		if (flags & (1u<<16))	//reliable
		{
			if (!(flags & (1u<<19)))	//reliable without end-of-message... we'd need to merge it...
				return VFS_ERROR_NETQUAKEIO;
			*hackbuf = 1;
		}
		else if (flags & (1u<<17))	//reliable ack
			*hackbuf = 2;
		else if (flags & (1u<<20))	//unreliable
			*hackbuf = 0;
		else
			return bytestowrite;	//drop it.
	}
#endif

	//okay, we're taking this packet. all or nothing.
	WS_Append(f, WS_PACKETTYPE_BINARYFRAME, buffer, bytestowrite);
	return bytestowrite;
}
size_t Base64_EncodeBlock(const qbyte *in, size_t length, char *out, size_t outsize);
static icestream_t *Websocket_WrapStream(icestream_t *stream, const char *host, const char *resource, const char *proto)
{	//this is kinda messy. Websocket_WrapStream(FS_OpenSSL(FS_WrapTCPSocket(TCP_OpenStream())))... *sigh*. wss uris kinda require all the extra layers.

	websocket_t *newf;
	char *hello;
	qbyte key[16];
	char b64key[(16*4)/3+3];
	if (!stream)
		return NULL;
	Sys_RandomBytes(key, sizeof(key));
	Base64_EncodeBlock(key, sizeof(key), b64key, sizeof(b64key));

	hello = va("GET %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Connection: Upgrade\r\n"
			"Upgrade: websocket\r\n"
			"Sec-WebSocket-Version: 13\r\n"
			"Sec-WebSocket-Protocol: "
#ifdef NETQUAKE_IO_HACK
					"%s"
#endif
					"%s\r\n"
			"Sec-WebSocket-Key: %s\r\n"
			"\r\n", resource, host,
#ifdef NETQUAKE_IO_HACK
					strcmp(proto, WEBSOCKET_SUBPROTOCOL)?"":NETQUAKE_IO_HACK", ",
#endif
					proto, key);

	newf = calloc(1, sizeof(*newf) + strlen(host));
	Sys_RandomBytes((void*)&newf->mask, sizeof(newf->mask));
	newf->stream = stream;
	newf->funcs.Close = WS_Close;
	newf->funcs.ReadBytes = WS_ReadBytes;
	newf->funcs.WriteBytes = WS_WriteBytes;

	newf->protocol = proto;
	newf->serverwaiting = false;

	//send the hello, the weird way.
	newf->pending = strdup(hello);
	newf->conpending = newf->pendingsize = newf->pendingmax = strlen(newf->pending);
	WS_Flush(newf);

	return &newf->funcs;
}

static icestream_t *Websocket_AcceptStream(icestream_t *stream, const char *host, const char *proto)
{	//things are messny when acting a server, too.

	websocket_t *newf;
	if (!stream)
		return NULL;

	newf = calloc(1, sizeof(*newf) + strlen(host));
	Sys_RandomBytes((void*)&newf->mask, sizeof(newf->mask));
	newf->stream = stream;
	newf->funcs.Close = WS_Close;
	newf->funcs.ReadBytes = WS_ReadBytes;
	newf->funcs.WriteBytes = WS_WriteBytes;

	newf->pendingmax = 65536;
	newf->pending = malloc(newf->pendingmax);
	newf->pendingsize = 0;
	newf->conpending = 1;
	newf->protocol = proto;
	newf->serverwaiting = true;
#ifdef HAVE_TLS
	newf->allowtls = true;
#endif
//	WS_Flush(newf);

	return &newf->funcs;
}

icestream_t *ICE_OpenTCP(const char *name, int defaultport, qboolean assumetls/*used when no scheme specified*/)
{
	icestream_t *f;
	qboolean wanttls;
	netadr_t addr;

	const char *resource = "/";
	const char *host = name;
	const char *proto = NULL;
	if (!strncmp(name, "wss:", 4))
		wanttls = true, host += 4, defaultport=defaultport?defaultport:443, proto="";
	else if (!strncmp(name, "ws:", 3))
		wanttls = false, host += 3, defaultport=defaultport?defaultport:80, proto="";
	else if (!strncmp(name, "tls://", 6))
		wanttls = true, host += 6;
	else if (!strncmp(name, "tcp://", 6))
		wanttls = false, host += 6;
	else if (!strncmp(name, "https://", 8))
		wanttls = true, host += 8, defaultport=defaultport?defaultport:443;
	else if (!strncmp(name, "http://", 7))
		wanttls = false, host += 7, defaultport=defaultport?defaultport:80;
	else
		wanttls = assumetls;	//just use the default....
	if (proto)
	{
		if (host != name && host[0] == '/' && host[1] == '/')
			host += 2;
		else
		{
			proto = host;
			host = strstr(host, "://");
			if (host)
			{
				char *t = alloca(1+host-proto);
				t[host-proto] = 0;
				proto = memcpy(t, proto, host-proto);
				host+=3;
			}
			else
			{
				host = name;
				proto = "";
			}
		}
	}

	resource = strchr(host, '/');
	if (!resource)
		resource = "/";
	else
	{
		char *t = alloca(1+resource-host);
		t[resource-host] = 0;
		host = memcpy(t, host, resource-host);
	}

	//FIXME: should we be doing an any-connect type thing for eg when dns reports an unusable ipv6 address?
	if (!NET_StringToAdr(host, defaultport, &addr, 1))
		return NULL;

#ifndef HAVE_TLS
	if (wanttls)
	{
		Con_Printf("tls not supported - %s\n", name);
		return NULL;	//don't even make the connection if we can't satisfy it.
	}
#endif
	f = FS_WrapTCPSocket(TCP_OpenStream(&addr, name, false), true, name);
#ifdef HAVE_TLS
	if (f && wanttls)
		f = ICE_OpenTLS(host, f, false);
#endif

	if (proto)
		f = Websocket_WrapStream(f, host, resource, proto);
	return f;
}



#if defined(HAVE_TURN) && defined(HAVE_TCP)
struct turntcpsocket_s
{	//this sends packets only to the relay, and accepts them only from there too. all packets must be stun packets (for byte-count framing)
	struct icesocket_s pub;
	icestream_t *f;
	struct
	{
		qbyte buf[20+65536];
		unsigned int offset;
		unsigned int avail;
	} recv, send;
	netadr_t adr;
};
static int TURN_TCP_RecvPacket(struct icesocket_s *s, netadr_t *addr, void *msg_data, size_t msg_maxsize)
{
	struct turntcpsocket_s *n = (struct turntcpsocket_s*)s;
	qboolean tried = false;
	int err;

	if (n->send.avail)
	{
		err = n->f->WriteBytes(n->f, n->send.buf+n->send.offset, n->send.avail);
		if (err >= 0)
		{
			n->send.avail -= err;
			n->send.offset += err;
		}
	}

	for (;;)
	{
		if (n->recv.avail >= 20) //must be a stun packet...
		{
			unsigned int psize = 20 + (n->recv.buf[n->recv.offset+2]<<8)+n->recv.buf[n->recv.offset+3];
			if (psize <= n->recv.avail)
			{
				memcpy(msg_data, n->recv.buf+n->recv.offset, psize);
				n->recv.avail -= psize;
				n->recv.offset += psize;

				*addr = n->adr;
				return psize;
			}
		}
		if (n->recv.offset && n->recv.offset+n->recv.avail == sizeof(n->recv.buf))
		{
			memmove(n->recv.buf, n->recv.buf+n->recv.offset, n->recv.avail);
			n->recv.offset = 0;
		}
		else if (tried)
			return 0;	//don't infinitely loop.
		err = n->f->ReadBytes(n->f, n->recv.buf+n->recv.offset+n->recv.avail, sizeof(n->recv.buf) - (n->recv.offset+n->recv.avail));
		if (err <= 0)
			return err;
		else
		{
			tried = true;
			n->recv.avail += err;
		}
	}
}
static neterr_t TURN_TCP_SendPacket (struct icesocket_s *s, const netadr_t *addr, const void *msg_data, size_t msg_size)
{
	int err;
	struct turntcpsocket_s *n = (struct turntcpsocket_s*)s;
	if (!NET_CompareAdr(addr, &n->adr))
		return NETERR_NOROUTE;

	//validate the packet - make sure its a TURN one. only checking size here cos we're lazy and that's enough.
	if (msg_size < 20 || msg_size != 20 + (((const qbyte*)msg_data)[2]<<8)+((const qbyte*)msg_data)[3])
		return NETERR_NOROUTE;

	if (!n->send.avail && msg_size < sizeof(n->send.buf))
	{	//avoid copying if we have to
		err = n->f->WriteBytes(n->f, msg_data, msg_size);
		if (err >= 0 && err < msg_size)
		{	//but sometimes its partial.
			msg_data = (const char*)msg_data + err;
			msg_size -= err;
			n->send.offset = 0;
			memcpy(n->send.buf, msg_data, msg_size);
			n->send.avail = msg_size;
		}
		if (!err)
			return NETERR_CLOGGED;	//mostly so we don't spam while still doing tcp/tls handshakes.
	}
	else
	{
		if (               n->send.avail+msg_size > sizeof(n->send.buf))
			return NETERR_CLOGGED; //can't possibly fit.
		if (n->send.offset+n->send.avail+msg_size > sizeof(n->send.buf))
		{	//move it down if we need.
			memmove(n->send.buf, n->send.buf+n->send.offset, n->send.avail);
			n->send.offset = 0;
		}
		memcpy(n->send.buf+n->send.offset, msg_data, msg_size);
		n->send.avail += msg_size;
		err = n->f->WriteBytes(n->f, n->send.buf+n->send.offset, n->send.avail);
		if (err >= 0)
		{
			n->send.offset += err;
			n->send.avail -= err;
		}
	}
	if (err >= 0)
	{
		return NETERR_SENT;	//sent something at least.
	}
	else
	{
		//one of the VFS_ERROR_* codes
		return NETERR_DISCONNECTED;
	}
}
static void TURN_TCP_Close(struct icesocket_s *con)
{
	struct turntcpsocket_s *n = (struct turntcpsocket_s*)con;
	if (n->f)
		n->f->Close(n->f);
}
struct icesocket_s *TURN_TCP_EstablishConnection(const char *address, netadr_t *adr, qboolean usetls)
{
	struct turntcpsocket_s *n = calloc(1, sizeof(*n));
	n->f = FS_WrapTCPSocket(TCP_OpenStream(adr, address, true), true, address);

#ifdef HAVE_TLS
	//convert to tls...
	if (usetls)
		n->f = ICE_OpenTLS(address, n->f, false);
#endif

	if (!n->f)
	{
		free(n);
		return NULL;
	}
	n->pub.SendPacket = TURN_TCP_SendPacket;
	n->pub.RecvPacket = TURN_TCP_RecvPacket;
	n->pub.CloseSocket = TURN_TCP_Close;
	n->adr = *adr;

	return &n->pub;
}
#endif

#ifdef HAVE_TCP
//for iceapi.Set(ice, "peer", "wss://host");
struct icewsssocket_s
{	//this sends packets only to the relay, and accepts them only from there too. all packets must be stun packets (for byte-count framing)
	struct icesocket_s pub;
	icestream_t *f;
	netadr_t adr;
};
static int ICE_WSS_RecvPacket(struct icesocket_s *s, netadr_t *addr, void *msg_data, size_t msg_maxsize)
{
	struct icewsssocket_s *n = (struct icewsssocket_s*)s;

	int sz = n->f->ReadBytes(n->f, msg_data, msg_maxsize);
	if (sz >= 0)
		*addr = n->adr;
	return sz;
}
static neterr_t ICE_WSS_SendPacket (struct icesocket_s *s, const netadr_t *addr, const void *msg_data, size_t msg_size)
{
	int err;
	struct icewsssocket_s *n = (struct icewsssocket_s*)s;
	if (!NET_CompareAdr(addr, &n->adr))
		return NETERR_NOROUTE;

	//our websocket code either accepts it all or rejects it all.
	err = n->f->WriteBytes(n->f, msg_data, msg_size);
	if (err > 0)
		return NETERR_SENT;
	switch(err)
	{
	case VFS_ERROR_TRYLATER:	return NETERR_CLOGGED;
	case VFS_ERROR_NETQUAKEIO:	return NETERR_NQIO;	//urgh.
	case VFS_ERROR_UNSPECIFIED:
	case VFS_ERROR_NORESPONSE:
	case VFS_ERROR_REFUSED:
	case VFS_ERROR_UNTRUSTED:
	case VFS_ERROR_DNSFAILURE:
	default:
	case VFS_ERROR_EOF:			return NETERR_DISCONNECTED;
	}
}
static void ICE_WSS_Close(struct icesocket_s *con)
{
	struct icewsssocket_s *n = (struct icewsssocket_s*)con;
	if (n->f)
		n->f->Close(n->f);
}
struct icesocket_s *ICE_WSS_EstablishConnection(const char *address, netadr_t *adr, qboolean usetls)
{	//we're the client
	struct icewsssocket_s *n = calloc(1, sizeof(*n));
	n->f = FS_WrapTCPSocket(TCP_OpenStream(adr, address, true), true, address);

#ifdef HAVE_TLS
	//convert to tls...
	if (usetls)
		n->f = ICE_OpenTLS(address, n->f, false);
#endif

	n->f = Websocket_WrapStream(n->f, address, "/",
#ifdef NETQUAKE_IO_HACK
			NETQUAKE_IO_HACK","
#endif
			WEBSOCKET_SUBPROTOCOL);

	if (!n->f)
	{
		free(n);
		return NULL;
	}
	n->pub.SendPacket = ICE_WSS_SendPacket;
	n->pub.RecvPacket = ICE_WSS_RecvPacket;
	n->pub.CloseSocket = ICE_WSS_Close;
	n->adr = *adr;

	return &n->pub;
}
struct icesocket_s *ICE_WS_ServerConnection(SOCKET sock, netadr_t *adr)
{	//we're the server. new tcp socket was connected. don't know if its ice or not yet.
	char peer[128];
	struct icewsssocket_s *n = calloc(1, sizeof(*n));
	NET_AdrToString(peer,sizeof(peer), adr);
	n->f = FS_WrapTCPSocket(sock, false, peer);

	n->f = Websocket_AcceptStream(n->f, peer, WEBSOCKET_SUBPROTOCOL);

	if (!n->f)
	{
		free(n);
		return NULL;
	}
	n->pub.SendPacket = ICE_WSS_SendPacket;
	n->pub.RecvPacket = ICE_WSS_RecvPacket;
	n->pub.CloseSocket = ICE_WSS_Close;
	n->adr = *adr;

	return &n->pub;
}
#endif
