/*
	WebRTC necessitates using sctp (over dtls) to implement its data channels.
	we just wanna play quake, man...
	if the other side creates a data channel then we'll acknowlege that, but otherwise we don't really care.

	quake has its own reliables mechanism (quakeworld in merged packets, netquake in individual packets). thus we don't need to implement reliables here. so we don't.
	if we did (and fixed up qw to split them) we would be able to solve the only-one-reliable-at-a-time issue reducing latency on later reliables. until then, NO RELIABLES!

	This code does not handle unsolicited connects, this is fine when there's already an ICE connection open.

	FIXME: Add Packetisation Layer Path MTU Discovery (PLPMTUD / RFC4821)
		we would then be able to avoid getting black-holed when sending large reliables (nq has no way to shrink them after, but sctp does). as well as more reliably reporting EMSGSIZE(equivelent)

	in the mean time, this code is basically used ONLY for web browser compat. the extra overhead
*/

#include "ice_private.h"

#if defined(SUPPORT_ICE) && defined(HAVE_SCTP)

typedef struct sctp_s
{
	struct icestate_s *context;	//for the callbacks.
	neterr_t (*SendPacket) (struct icestate_s *context, const void *msg_data, size_t msg_size);	//called when sctp needs to send a packet (probably ends up doing the dtls thing)

	unsigned int modeflags;	//mostly for ICEF_VERBOSE.
	const char *friendlyname;	//for printing/debugging.

	uint16_t myport, peerport;
	qboolean peerhasfwdtsn;
	double nextreinit;
	void *cookie;	//sent part of the handshake (might need resending).
	size_t cookiesize;
	struct
	{
		uint32_t verifycode;
		qboolean writable;
		uint32_t tsn;	//Transmit Sequence Number

		uint32_t ctsn;	//acked tsn
		uint32_t losttsn; //total gap size...
	} o;
	struct
	{
		uint32_t verifycode;
		int ackneeded;
		uint32_t ctsn;
		uint32_t htsn;	//so we don't have to walk so many packets to count gaps.
#define SCTP_RCVSIZE 2048	//cannot be bigger than 65536
		qbyte received[SCTP_RCVSIZE/8];

		struct
		{
			uint32_t	firsttns; //so we only ack fragments that were complete.
			uint32_t	tsn; //if a continuation doesn't match, we drop it.
			uint32_t	ppid;
			uint16_t	sid;
			uint16_t	seq;
			size_t		size;
			qboolean	toobig;
			qbyte		buf[65536];
		} r;
	} i;
	unsigned short qstreamid;	//in network endian.
} sctp_t;

//========================================
//WebRTC's interpretation of SCTP. its annoying, but hey its only 28 wasted bytes... along with the dtls overhead too. most of this is redundant.
//we only send unreliably.
//there's no point in this code without full webrtc code.

struct sctp_header_s
{
	uint16_t srcport;		//redundant when encapsulated in udp...
	uint16_t dstport;		//redundant when encapsulated in udp...
	uint32_t verifycode;	//redundant when encapsulated in dtls...
	uint32_t crc;			//redundant when encapsulated in dtls...
};
struct sctp_chunk_s
{
	qbyte type;
#define SCTP_TYPE_DATA 0
#define SCTP_TYPE_INIT 1
#define SCTP_TYPE_INITACK 2
#define SCTP_TYPE_SACK 3
#define SCTP_TYPE_PING 4
#define SCTP_TYPE_PONG 5
#define SCTP_TYPE_ABORT 6
#define SCTP_TYPE_SHUTDOWN 7
#define SCTP_TYPE_SHUTDOWNACK 8
#define SCTP_TYPE_ERROR 9
#define SCTP_TYPE_COOKIEECHO 10
#define SCTP_TYPE_COOKIEACK 11
#define SCTP_TYPE_SHUTDOWNDONE 14
#define SCTP_TYPE_PAD 132
#define SCTP_TYPE_FORWARDTSN 192
	qbyte flags;
	uint16_t length;
	//value...
};
struct sctp_chunk_data_s
{
	struct sctp_chunk_s chunk;
	uint32_t tsn;
	uint16_t sid;
	uint16_t seq;
	uint32_t ppid;
#define SCTP_PPID_DCEP 50 //datachannel establishment protocol
#define SCTP_PPID_DATA 53 //our binary quake data.
};
struct sctp_chunk_init_s
{
	struct sctp_chunk_s chunk;
	uint32_t verifycode;
	uint32_t arwc;
	uint16_t numoutstreams;
	uint16_t numinstreams;
	uint32_t tsn;
};
struct sctp_chunk_sack_s
{
	struct sctp_chunk_s chunk;
	uint32_t tsn;
	uint32_t arwc;
	uint16_t gaps;
	uint16_t dupes;
	/*struct {
		quint16_t first;
		quint16_t last;
	} gapblocks[];	//actually received rather than gaps, but same meaning.
	quint32_t dupe_tsns[];*/
};
struct sctp_chunk_fwdtsn_s
{
	struct sctp_chunk_s chunk;
	uint32_t tsn;
	/*struct
	{
		quint16_t sid;
		quint16_t seq;
	} streams[];*/
};

/*static neterr_t SCTP_PeerSendPacket(sctp_t *sctp, const void *data, size_t length)
{	//sends to the dtls layer (which will send to the generic ice dispatcher that'll send to the dgram stuff... layers on layers.
	struct icestate_s *peer = sctp->icestate;
	if (peer)
	{
		if (peer->dtlsstate)
			return peer->dtlsfuncs->Transmit(peer->dtlsstate, data, length);
		else if (peer->chosenpeer.type != NA_INVALID)
			return ICE_Transmit(peer, data, length);
		else if (peer->state < ICE_CONNECTING)
			return NETERR_DISCONNECTED;
		else
			return NETERR_CLOGGED;
	}
	else
		return NETERR_NOROUTE;
}*/

static uint32_t SCTP_Checksum(const struct sctp_header_s *h, size_t size)
{
    int k;
    const qbyte *buf = (const qbyte*)h;
    size_t ofs;
    uint32_t crc = 0xFFFFFFFF;

	for (ofs = 0; ofs < size; ofs++)
    {
		if (ofs >= 8 && ofs < 8+4)
			;	//the header's crc should be read as 0.
		else
			crc ^= buf[ofs];
        for (k = 0; k < 8; k++)	            //CRC-32C polynomial 0x1EDC6F41 in reversed bit order.
            crc = crc & 1 ? (crc >> 1) ^ 0x82f63b78 : crc >> 1;
    }
    return ~crc;
}

neterr_t SCTP_Transmit(struct sctp_s *sctp, const void *data, size_t length)
{
	qbyte pkt[65536];
	size_t pktlen = 0;
	struct sctp_header_s *h = (void*)pkt;
	struct sctp_chunk_data_s *d;
	struct sctp_chunk_fwdtsn_s *fwd;
	if (length > sizeof(pkt))
		return NETERR_MTU;

	h->dstport = sctp->peerport;
	h->srcport = sctp->myport;
	h->verifycode = sctp->o.verifycode;
	pktlen += sizeof(*h);

	//advance our ctsn if we're received the relevant packets
	while(sctp->i.htsn)
	{
		uint32_t tsn = sctp->i.ctsn+1;
		if (!(sctp->i.received[(tsn>>3)%sizeof(sctp->i.received)] & 1<<(tsn&7)))
			break;
		//advance our cumulative ack.
		sctp->i.received[(tsn>>3)%sizeof(sctp->i.received)] &= ~(1<<(tsn&7));
		sctp->i.ctsn = tsn;
		sctp->i.htsn--;
	}

	if (!sctp->o.writable)
	{
		double time = Sys_DoubleTime();
		if (time > sctp->nextreinit)
		{
			sctp->nextreinit = time + 0.5;
			if (!sctp->cookie)
			{
				struct sctp_chunk_init_s *init = (struct sctp_chunk_init_s*)&pkt[pktlen];
				struct {
					uint16_t ptype;
					uint16_t plen;
				} *ftsn = (void*)(init+1);
				h->verifycode = 0;
				init->chunk.type = SCTP_TYPE_INIT;
				init->chunk.flags = 0;
				init->chunk.length = BigShort(sizeof(*init)+sizeof(*ftsn));
				init->verifycode = sctp->i.verifycode;
				init->arwc = BigLong(65535);
				init->numoutstreams = BigShort(2);
				init->numinstreams = BigShort(2);
				init->tsn = BigLong(sctp->o.tsn);
				ftsn->ptype = BigShort(49152);
				ftsn->plen = BigShort(sizeof(*ftsn));
				pktlen += sizeof(*init) + sizeof(*ftsn);

				h->crc = SCTP_Checksum(h, pktlen);
				return sctp->SendPacket(sctp->context, h, pktlen);
			}
			else
			{
				struct sctp_chunk_s *cookie = (struct sctp_chunk_s*)&pkt[pktlen];

				if (pktlen + sizeof(*cookie) + sctp->cookiesize > sizeof(pkt))
					return NETERR_DISCONNECTED;
				cookie->type = SCTP_TYPE_COOKIEECHO;
				cookie->flags = 0;
				cookie->length = BigShort(sizeof(*cookie)+sctp->cookiesize);
				memcpy(cookie+1, sctp->cookie, sctp->cookiesize);
				pktlen += sizeof(*cookie) + sctp->cookiesize;

				h->crc = SCTP_Checksum(h, pktlen);
				return sctp->SendPacket(sctp->context, h, pktlen);
			}
		}

		return NETERR_CLOGGED;	//nope, not ready yet
	}

	if (sctp->peerhasfwdtsn && (int)(sctp->o.ctsn-sctp->o.tsn) < -5 && sctp->o.losttsn)
	{	
		fwd = (struct sctp_chunk_fwdtsn_s*)&pkt[pktlen];
		fwd->chunk.type = SCTP_TYPE_FORWARDTSN;
		fwd->chunk.flags = 0;
		fwd->chunk.length = BigShort(sizeof(*fwd));
		fwd->tsn = BigLong(sctp->o.tsn-1);

		//we only send unordered unreliables, so this stream stuff here is irrelevant.
//		fwd->streams[0].sid = sctp->qstreamid;
//		fwd->streams[0].seq = BigShort(0);
		pktlen += sizeof(*fwd);
	}

	if (sctp->i.ackneeded >= 2)
	{
		struct sctp_chunk_sack_s *rsack;
		struct sctp_chunk_sack_gap_s {
			uint16_t first;
			uint16_t last;
		} *rgap;
		uint32_t otsn;

		rsack = (struct sctp_chunk_sack_s*)&pkt[pktlen];
		rsack->chunk.type = SCTP_TYPE_SACK;
		rsack->chunk.flags = 0;
		rsack->chunk.length = BigShort(sizeof(*rsack));
		rsack->tsn = BigLong(sctp->i.ctsn);
		rsack->arwc = BigLong(65535);
		rsack->gaps = 0;
		rsack->dupes = BigShort(0);
		pktlen += sizeof(*rsack);

		rgap = (struct sctp_chunk_sack_gap_s*)&pkt[pktlen];
		for (otsn = 0; otsn < sctp->i.htsn; otsn++)
		{
			uint32_t tsn = sctp->i.ctsn+otsn;
			if (!(sctp->i.received[(tsn>>3)%sizeof(sctp->i.received)] & 1<<(tsn&7)))
				continue;	//missing, don't report it in the 'gaps'... yeah, backwards naming.
			if (rsack->gaps && rgap[-1].last == otsn-1)
				rgap[-1].last = otsn;	//merge into the last one.
			else
			{
				rgap->first = otsn;	//these values are Offset from the Cumulative TSN, to save storage.
				rgap->last = otsn;
				rgap++;
				rsack->gaps++;
				pktlen += sizeof(*rgap);
				if (pktlen >= 500)
					break;	//might need fragmentation... just stop here.
			}
		}
		for (otsn = 0, rgap = (struct sctp_chunk_sack_gap_s*)&pkt[pktlen]; otsn < rsack->gaps; otsn++)
		{
			rgap[otsn].first = BigShort(rgap[otsn].first);
			rgap[otsn].last = BigShort(rgap[otsn].last);
		}
		rsack->gaps = BigShort(rsack->gaps);

		sctp->i.ackneeded = 0;
	}

	if (pktlen + sizeof(*d) + length >= 500 && length && pktlen != sizeof(*h))
	{	//probably going to result in fragmentation issues. send separate packets.
		h->crc = SCTP_Checksum(h, pktlen);
		sctp->SendPacket(sctp->context, h, pktlen);

		//reset to the header
		pktlen = sizeof(*h);
	}

	if (length)
	{
		d = (void*)&pkt[pktlen];
		d->chunk.type = SCTP_TYPE_DATA;
		d->chunk.flags = 3|4;
		d->chunk.length = BigShort(sizeof(*d) + length);
		d->tsn = BigLong(sctp->o.tsn++);
		d->sid = sctp->qstreamid;
		d->seq = BigShort(0); //not needed for unordered
		d->ppid = BigLong(SCTP_PPID_DATA);
		memcpy(d+1, data, length);
		pktlen += sizeof(*d) + length;

		//chrome insists on pointless padding at the end. its annoying.
		while(pktlen&3)
			pkt[pktlen++]=0;
	}
	if (pktlen == sizeof(*h))
		return NETERR_SENT; //nothing to send...

	h->crc = SCTP_Checksum(h, pktlen);
	return sctp->SendPacket(sctp->context, h, pktlen);
}

static void SCTP_DecodeDCEP(sctp_t *sctp, qbyte *resp)
{	//send an ack...
	size_t pktlen = 0;
	struct sctp_header_s *h = (void*)resp;
	struct sctp_chunk_data_s *d;
	char *data = "\02";
	size_t length = 1; //*sigh*...

	struct
	{
		qbyte type;
		qbyte chantype;
		uint16_t priority;
		uint32_t relparam;
		uint16_t labellen;
		uint16_t protocollen;
	} *dcep = (void*)sctp->i.r.buf;

	if (dcep->type == 3)
	{
		char *label = (char*)(dcep+1);
		char *prot = label + strlen(label)+1;

		sctp->qstreamid = sctp->i.r.sid;
		if (sctp->modeflags & ICEF_VERBOSE)
			Con_Printf(S_COLOR_GRAY"[%s]: New SCTP Channel: \"%s\" (%s)\n", sctp->friendlyname, label, prot);

		h->dstport = sctp->peerport;
		h->srcport = sctp->myport;
		h->verifycode = sctp->o.verifycode;
		pktlen += sizeof(*h);

		pktlen = (pktlen+3)&~3;	//pad.
		d = (void*)&resp[pktlen];
		d->chunk.type = SCTP_TYPE_DATA;
		d->chunk.flags = 3;
		d->chunk.length = BigShort(sizeof(*d) + length);
		d->tsn = BigLong(sctp->o.tsn++);
		d->sid = sctp->qstreamid;
		d->seq = BigShort(0); //not needed for unordered
		d->ppid = BigLong(SCTP_PPID_DCEP);
		memcpy(d+1, data, length);
		pktlen += sizeof(*d) + length;

		h->crc = SCTP_Checksum(h, pktlen);
		sctp->SendPacket(sctp->context, h, pktlen);
	}
}

struct sctp_errorcause_s
{
	uint16_t cause;
	uint16_t length;
};
static void SCTP_ErrorChunk(sctp_t *sctp, const char *errortype, const struct sctp_errorcause_s *s, size_t totallen)
{
//	struct icestate_s *ice = sctp->icestate;

	uint16_t cc, cl;
	while(totallen > 0)
	{
		if (totallen < sizeof(*s))
			return;	//that's an error in its own right
		cc = BigShort(s->cause);
		cl = BigShort(s->length);
		if (totallen < cl)
			return;	//err..

		if (sctp->modeflags & ICEF_VERBOSE) switch(cc)
		{
		case 1:		Con_Printf(S_COLOR_GRAY"[%s]: SCTP %s: Invalid Stream Identifier\n",	sctp->friendlyname, errortype);	break;
        case 2:		Con_Printf(S_COLOR_GRAY"[%s]: SCTP %s: Missing Mandatory Parameter\n",	sctp->friendlyname, errortype);	break;
        case 3:		Con_Printf(S_COLOR_GRAY"[%s]: SCTP %s: Stale Cookie Error\n",			sctp->friendlyname, errortype);	break;
        case 4:		Con_Printf(S_COLOR_GRAY"[%s]: SCTP %s: Out of Resource\n",				sctp->friendlyname, errortype);	break;
        case 5:		Con_Printf(S_COLOR_GRAY"[%s]: SCTP %s: Unresolvable Address\n",			sctp->friendlyname, errortype);	break;
        case 6:		Con_Printf(S_COLOR_GRAY"[%s]: SCTP %s: Unrecognized Chunk Type\n",		sctp->friendlyname, errortype);	break;
        case 7:		Con_Printf(S_COLOR_GRAY"[%s]: SCTP %s: Invalid Mandatory Parameter\n",	sctp->friendlyname, errortype);	break;
        case 8:		Con_Printf(S_COLOR_GRAY"[%s]: SCTP %s: Unrecognized Parameters\n",		sctp->friendlyname, errortype);	break;
        case 9:		Con_Printf(S_COLOR_GRAY"[%s]: SCTP %s: No User Data\n",					sctp->friendlyname, errortype);	break;
        case 10:	Con_Printf(S_COLOR_GRAY"[%s]: SCTP %s: Cookie Received While Shutting Down\n",			sctp->friendlyname, errortype);	break;
        case 11:	Con_Printf(S_COLOR_GRAY"[%s]: SCTP %s: Restart of an Association with New Addresses\n",	sctp->friendlyname, errortype);	break;
        case 12:	Con_Printf(S_COLOR_GRAY"[%s]: SCTP %s: User Initiated Abort\n",			sctp->friendlyname, errortype);	break;
        case 13:	Con_Printf(S_COLOR_GRAY"[%s]: SCTP %s: Protocol Violation [%s]\n",		sctp->friendlyname, errortype, (const char*)(s+1));	break;
        default:	Con_Printf(S_COLOR_GRAY"[%s]: SCTP %s: Unknown Reason\n",				sctp->friendlyname, errortype);	break;
		}

		totallen -= cl;
		totallen &= ~3;
		s = (const struct sctp_errorcause_s*)((const qbyte*)s + ((cl+3)&~3));
	}
}

void SCTP_Decode(struct sctp_s *sctp, const void *msg_data, size_t msg_size, qboolean (*ReadGamePacket)(struct icestate_s *context, const void *msg_data, size_t msg_size))
{
	qbyte resp[4096];

	const qbyte *msg = msg_data;
	const qbyte *msgend = msg+msg_size;
	const struct sctp_header_s *h = (const struct sctp_header_s*)msg;
	const struct sctp_chunk_s *c = (const struct sctp_chunk_s*)(h+1);
	uint16_t clen;
	if ((const qbyte*)c+1 > msgend)
		return;	//runt
	if (h->dstport != sctp->myport)
		return;	//not for us...
	if (h->srcport != sctp->peerport)
		return; //not from them... we could check for a INIT but its over dtls anyway so why give a damn.
	if (h->verifycode != ((c->type == SCTP_TYPE_INIT)?0:sctp->i.verifycode))
		return;	//wrong cookie... (be prepared to parse dupe inits if our ack got lost...
	if (h->crc != SCTP_Checksum(h, msg_size))
		return;	//crc wrong. assume corruption.
	if (msg_size&3)
	{
		if (sctp->modeflags & ICEF_VERBOSE_PROBE)
			Con_Printf(S_COLOR_GRAY"[%s]: SCTP: packet not padded\n", sctp->friendlyname);
		return;	//mimic chrome, despite it being pointless.
	}

	while ((const qbyte*)(c+1) <= msgend)
	{
		clen = BigShort(c->length);
		if ((const qbyte*)c + clen > msgend || clen < sizeof(*c))
		{
			Con_Printf(CON_ERROR"Corrupt SCTP message\n");
			break;
		}
		switch(c->type)
		{
		case SCTP_TYPE_DATA:
			if (clen >= sizeof(struct sctp_chunk_data_s))
			{
				const struct sctp_chunk_data_s *dc = (const void*)c;
				uint32_t tsn = BigLong(dc->tsn), u;
				int32_t adv = tsn - sctp->i.ctsn;
				sctp->i.ackneeded++;
				if (adv >= SCTP_RCVSIZE)
				{
					if (sctp->modeflags & ICEF_VERBOSE)
						Con_Printf(S_COLOR_GRAY"[%s]: SCTP: Future Packet\n", sctp->friendlyname);/*too far in the future. we can't track such things*/
				}
				else if (adv <= 0)
				{
					if (sctp->modeflags & ICEF_VERBOSE_PROBE)
						Con_Printf(S_COLOR_GRAY"[%s]: SCTP: PreCumulative\n", sctp->friendlyname);/*already acked this*/
				}
				else if (sctp->i.received[(tsn>>3)%sizeof(sctp->i.received)] & 1<<(tsn&7))
				{
					if (sctp->modeflags & ICEF_VERBOSE_PROBE)
						Con_DPrintf(S_COLOR_GRAY"[%s]: SCTP: Dupe\n", sctp->friendlyname);/*already processed it. FIXME: Make a list for the next SACK*/
				}
				else
				{
					qboolean err = false;

					if (c->flags & 2)
					{	//beginning...
						sctp->i.r.firsttns = tsn;
						sctp->i.r.tsn = tsn;
						sctp->i.r.size = 0;
						sctp->i.r.ppid = dc->ppid;
						sctp->i.r.sid = dc->sid;
						sctp->i.r.seq = dc->seq;
						sctp->i.r.toobig = false;
					}
					else
					{
						if (sctp->i.r.tsn != tsn || sctp->i.r.ppid != dc->ppid)
							err = true;
					}
					if (err)
						;	//don't corrupt anything in case we get a quick resend that fixes it.
					else
					{
						size_t dlen = clen-sizeof(*dc);
						if (adv > sctp->i.htsn)	//weird maths in case it wraps.
							sctp->i.htsn = adv;
						sctp->i.r.tsn++;
						if (sctp->i.r.size + dlen > sizeof(sctp->i.r.buf))
						{
							if (sctp->modeflags & ICEF_VERBOSE_PROBE)
								Con_Printf(S_COLOR_GRAY"[%s]: SCTP: Oversized\n", sctp->friendlyname);
							sctp->i.r.toobig = true;	//reassembled packet was too large, just corrupt it.
						}
						else
						{
							memcpy(sctp->i.r.buf+sctp->i.r.size, dc+1, dlen);	//include the dc header
							sctp->i.r.size += dlen;
						}
						if (c->flags & 1)	//an ending. we have the complete packet now.
						{
							for (u = sctp->i.r.tsn - sctp->i.r.firsttns; u --> 0; )
							{
								tsn = sctp->i.r.firsttns + u;
								sctp->i.received[(tsn>>3)%sizeof(sctp->i.received)] |= 1<<(tsn&7);
							}
							if (sctp->i.r.toobig)
								;/*ignore it when it cannot be handled*/
							else if (sctp->i.r.ppid == BigLong(SCTP_PPID_DATA))
							{
								if (!ReadGamePacket(sctp->context, sctp->i.r.buf, sctp->i.r.size))
									return;	//socket got killed...
							}
							else if (sctp->i.r.ppid == BigLong(SCTP_PPID_DCEP))
								SCTP_DecodeDCEP(sctp, resp);
						}
					}

					//FIXME: we don't handle reordering properly at all.

//					if (c->flags & 4)
//						Con_Printf("\tUnordered\n");
//					Con_Printf("\tStream Id %i\n", BigShort(dc->sid));
//					Con_Printf("\tStream Seq %i\n", BigShort(dc->seq));
//					Con_Printf("\tPPID %i\n", BigLong(dc->ppid));
				}
			}
			break;
		case SCTP_TYPE_INIT:
		case SCTP_TYPE_INITACK:
			if (clen >= sizeof(struct sctp_chunk_init_s))
			{
				qboolean isack = c->type==SCTP_TYPE_INITACK;
				const struct sctp_chunk_init_s *init = (const void*)c;
				const struct {
						uint16_t ptype;
						uint16_t plen;
				} *p = (const void*)(init+1);

				sctp->i.ctsn = BigLong(init->tsn)-1;
				sctp->i.htsn = 0;
				sctp->o.verifycode = init->verifycode;
				(void)BigLong(init->arwc);
				(void)BigShort(init->numoutstreams);
				(void)BigShort(init->numinstreams);

				while ((const qbyte*)p+sizeof(*p) <= (const qbyte*)c+clen)
				{
					unsigned short ptype = BigShort(p->ptype);
					unsigned short plen = BigShort(p->plen);
					switch(ptype)
					{
					case 7:	//init cookie
						if (sctp->cookie)
							free(sctp->cookie);
						sctp->cookiesize = plen - sizeof(*p);
						sctp->cookie = calloc(1, sctp->cookiesize);
						memcpy(sctp->cookie, p+1, sctp->cookiesize);
						break;
					case 32773:	//Padding
					case 32776:	//ASCONF
						break;
					case 49152:
						sctp->peerhasfwdtsn = true;
						break;
					default:
						if (sctp->modeflags & ICEF_VERBOSE_PROBE)
							Con_Printf(S_COLOR_GRAY"[%s]: SCTP: Found unknown init parameter %i||%#x\n", sctp->friendlyname, ptype, ptype);

						if (ptype&0x4000)
							; //FIXME: SCTP_TYPE_ERROR(6,"Unrecognized Chunk Type")
						if (!(ptype&0x8000))
							return;	//'do not process nay further chunks'
						//otherwise parse the next as normal.
						break;
					}
					p = (const void*)((const qbyte*)p + ((plen+3)&~3));
				}

				if (isack)
				{
					sctp->nextreinit = 0;
					if (sctp->cookie)
						SCTP_Transmit(sctp, NULL, 0);	//make sure we send acks occasionally even if we have nothing else to say.
				}
				else
				{
					struct sctp_header_s *rh = (void*)resp;
					struct sctp_chunk_init_s *rinit = (void*)(rh+1);
					struct {
						uint16_t ptype;
						uint16_t plen;
						struct {
							qbyte data[16];
						} cookie;
					} *rinitcookie = (void*)(rinit+1);
					struct {
						uint16_t ptype;
						uint16_t plen;
					} *rftsn = (void*)(rinitcookie+1);
					qbyte *end = sctp->peerhasfwdtsn?(void*)(rftsn+1):(void*)(rinitcookie+1);

					rh->srcport = sctp->myport;
					rh->dstport = sctp->peerport;
					rh->verifycode = sctp->o.verifycode;
					rh->crc = 0;
					*rinit = *init;
					rinit->chunk.type = SCTP_TYPE_INITACK;
					rinit->chunk.flags = 0;
					rinit->chunk.length = BigShort(end-(qbyte*)rinit);
					rinit->verifycode = sctp->i.verifycode;
					rinit->arwc = BigLong(65536);
					rinit->numoutstreams = init->numoutstreams;
					rinit->numinstreams = init->numinstreams;
					rinit->tsn = BigLong(sctp->o.tsn);
					rinitcookie->ptype = BigShort(7);
					rinitcookie->plen = BigShort(sizeof(*rinitcookie));
					memcpy(&rinitcookie->cookie, "deadbeefdeadbeef", sizeof(rinitcookie->cookie));	//frankly the contents of the cookie are irrelevant to anything. we've already verified the peer's ice pwd/ufrag stuff as well as their dtls certs etc.
					rftsn->ptype = BigShort(49152);
					rftsn->plen = BigShort(sizeof(*rftsn));

					//complete. calc the proper crc and send it off.
					rh->crc = SCTP_Checksum(rh, end-resp);
					sctp->SendPacket(sctp->context, rh, end-resp);
				}
			}
			break;
		case SCTP_TYPE_SACK:
			if (clen >= sizeof(struct sctp_chunk_sack_s))
			{
				const struct sctp_chunk_sack_s *sack = (const void*)c;
				uint32_t tsn = BigLong(sack->tsn);
				sctp->o.ctsn = tsn;

				sctp->o.losttsn = BigShort(sack->gaps);	//if there's a gap then they're telling us they got a later one.

				//Con_Printf(CON_ERROR"Sack %#x (%i in flight)\n"
				//			"\tgaps: %i, dupes %i\n",
				//			tsn, sctp->o.tsn-tsn,
				//			BigShort(sack->gaps), BigShort(sack->dupes));
			}
			break;
		case SCTP_TYPE_PING:
			if (clen >= sizeof(struct sctp_chunk_s))
			{
				const struct sctp_chunk_s *ping = (const void*)c;
				struct sctp_header_s *pongh = calloc(1, sizeof(*pongh) + clen);

				pongh->srcport = sctp->myport;
				pongh->dstport = sctp->peerport;
				pongh->verifycode = sctp->o.verifycode;
				pongh->crc = 0;
				memcpy(pongh+1, ping, clen);
				((struct sctp_chunk_s*)(pongh+1))->type = SCTP_TYPE_PONG;

				//complete. calc the proper crc and send it off.
				pongh->crc = SCTP_Checksum(pongh, sizeof(*pongh) + clen);
				sctp->SendPacket(sctp->context, pongh, sizeof(*pongh) + clen);
				free(pongh);
			}
			break;
//		case SCTP_TYPE_PONG:	//we don't send pings
		case SCTP_TYPE_ABORT:
			SCTP_ErrorChunk(sctp, "Abort", (const struct sctp_errorcause_s*)(c+1), clen-sizeof(*c));
			if (sctp->context)
				ICE_SetFailed(sctp->context, "SCTP Abort");
			break;
		case SCTP_TYPE_SHUTDOWN:	//FIXME. we should send an ack...
			if (sctp->modeflags & ICEF_VERBOSE)
				Con_Printf(S_COLOR_GRAY"[%s]: SCTP: Shutdown\n", sctp->friendlyname);
			if (sctp->context)
				ICE_SetFailed(sctp->context, "SCTP Shutdown");
			break;
//		case SCTP_TYPE_SHUTDOWNACK:	//we don't send shutdowns, cos we're lame like that...
		case SCTP_TYPE_ERROR:
			//not fatal...
			SCTP_ErrorChunk(sctp, "Error", (const struct sctp_errorcause_s*)(c+1), clen-sizeof(*c));
			break;
		case SCTP_TYPE_COOKIEECHO:
			if (clen >= sizeof(struct sctp_chunk_s))
			{
				struct sctp_header_s *rh = (void*)resp;
				struct sctp_chunk_s *rack = (void*)(rh+1);
				qbyte *end = (void*)(rack+1);

				rh->srcport = sctp->myport;
				rh->dstport = sctp->peerport;
				rh->verifycode = sctp->o.verifycode;
				rh->crc = 0;
				rack->type = SCTP_TYPE_COOKIEACK;
				rack->flags = 0;
				rack->length = BigShort(sizeof(*rack));

				//complete. calc the proper crc and send it off.
				rh->crc = SCTP_Checksum(rh, end-resp);
				sctp->SendPacket(sctp->context, rh, end-resp);

				sctp->o.writable = true;	//channel SHOULD now be open for data.
			}
			break;
		case SCTP_TYPE_COOKIEACK:
			sctp->o.writable = true;	//we know the other end is now open.
			break;
		case SCTP_TYPE_PAD:
			//don't care.
			break;
		case SCTP_TYPE_FORWARDTSN:
			if (clen >= sizeof(struct sctp_chunk_fwdtsn_s))
			{
				const struct sctp_chunk_fwdtsn_s *fwd = (const void*)c;
				uint32_t tsn = BigLong(fwd->tsn), count;
				count = tsn - sctp->i.ctsn;
				if ((int)count < 0)
					break;	//overflow? don't go backwards.
				if (count > 1024)
					count = 1024; //don't advance too much in one go. we'd block and its probably an error anyway.
				while(count --> 0)
				{
					tsn = ++sctp->i.ctsn;
					sctp->i.received[(tsn>>3)%sizeof(sctp->i.received)] &= ~(1<<(tsn&7));
					if (sctp->i.htsn)
						sctp->i.htsn--;
					sctp->i.ackneeded++;	//flag for a sack if we actually completed something here.
				}
			}
			break;
//		case SCTP_TYPE_SHUTDOWNDONE:
		default:
			//no idea what this chunk is, just ignore it...
			if (sctp->modeflags & ICEF_VERBOSE)
				Con_Printf(S_COLOR_GRAY"[%s]: SCTP: Unsupported chunk %i\n", sctp->friendlyname, c->type);

			switch (c->type>>6)
			{
			case 0:
				clen = (const qbyte*)msgend - (const qbyte*)c;	//'do not process any further chunks'
				break;
			case 1:
				clen = (const qbyte*)msgend - (const qbyte*)c;	//'do not process any further chunks'
				/*FIXME: SCTP_TYPE_ERROR(6,"Unrecognized Chunk Type")*/
				break;
			case 2:
				//silently ignore it
				break;
			case 3:
				//ignore-with-error
				/*FIXME: SCTP_TYPE_ERROR(6,"Unrecognized Chunk Type")*/
				break;
			}
			break;
		}
		c = (const struct sctp_chunk_s*)((const qbyte*)c + ((clen+3)&~3));	//next chunk is 4-byte aligned.
	}

	if (sctp->i.ackneeded >= 5)
		SCTP_Transmit(sctp, NULL, 0);	//make sure we send acks occasionally even if we have nothing else to say.
}

sctp_t *SCTP_Create(struct icestate_s *ctx, const char *verbosename, unsigned int modeflags, int localport, int remoteport, neterr_t (*SendLowerPacket)(struct icestate_s *context, const void *msg_data, size_t msg_size))
{
	sctp_t *sctp = calloc(1, sizeof(*sctp));
	sctp->context = ctx;
	sctp->SendPacket = SendLowerPacket;

	sctp->friendlyname = verbosename;
	sctp->modeflags = modeflags;
	sctp->myport = htons(localport);
	sctp->peerport = htons(remoteport);

	sctp->o.tsn = rand() ^ (rand()<<16);
	Sys_RandomBytes((void*)&sctp->o.verifycode, sizeof(sctp->o.verifycode));
	Sys_RandomBytes((void*)&sctp->i.verifycode, sizeof(sctp->i.verifycode));

	return sctp;
}

void SCTP_Destroy(sctp_t *sctp)
{
	free(sctp->cookie);
	free(sctp);
}

//========================================
#endif
