/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#ifndef __ICE_QUAKE_H
#define __ICE_QUAKE_H

int			NQICE_Init (void);
void		NQICE_Listen (qboolean state);		//used by server (enables websocket connection).
int			NQICE_QueryAddresses(qhostaddr_t *addresses, int maxaddresses);
qboolean	NQICE_SearchForHosts (qboolean xmit);
qsocket_t	*NQICE_Connect (const char *host);	//used by client (enables websocket connection). fails when not ice, otherwise succeeds pending async broker failure.
qsocket_t	*NQICE_CheckNewConnections (void);	//used by server.
void		NQICE_GetAnyMessages(void(*callback)(qsocket_t *));			//used by server.
int			NQICE_GetMessage (qsocket_t *sock);	//used by client.
int			NQICE_SendMessage (qsocket_t *sock, sizebuf_t *data);
int			NQICE_SendUnreliableMessage (qsocket_t *sock, sizebuf_t *data);
qboolean	NQICE_CanSendMessage (qsocket_t *sock);
qboolean	NQICE_CanSendUnreliableMessage (qsocket_t *sock);
void		NQICE_Close (qsocket_t *sock);
void		NQICE_Shutdown (void);
qboolean	NQICE_IsListening (void);	//returns true if the ICE/WebSocket server is active
const char	*NQICE_GetWsAddr (void);	//returns sv_addr_ws cvar value (empty string if unset)
const char	*NQICE_GetFingerprint (void);	//returns base64 DTLS cert fingerprint for *fp infostring
void		NQICE_ShareGameSocket (sys_socket_t sock);	//share the datagram driver's UDP socket with ICE
void		NQICE_UnshareGameSockets (void);	//invalidate shared sockets (call before closing datagram sockets)
qboolean	NQICE_ProcessPacket (byte *data, int len, struct qsockaddr *addr, void(*callback)(qsocket_t *));	//forward a non-quake packet to ICE. returns true if consumed.
struct icesocket_s;
qboolean	BrokerDTLS_HandlePacket (byte *data, int len, struct qsockaddr *addr, struct icesocket_s *sendsock);
void		BrokerDTLS_Cleanup (void);
void		BrokerDTLS_Shutdown (void);
qboolean	BrokerDTLS_IsAuthenticated (void);
int		BrokerDTLS_Send (const void *data, int len);	//returns 0 on success

//implemented in net_dgrm.c. capacity is the total size of the data allocation,
//including the one byte of terminator headroom the callee may write to.
void		_Datagram_BrokerPacket (byte *data, unsigned int length, size_t capacity, sys_socket_t sock, struct qsockaddr *addr);

//broker-to-server ICE signaling over the UDP game port (for /udp/IP:Port connections)
typedef void (*ice_udp_send_t)(const void *data, int len);
void		SVC_ICE_Offer(const char *clientaddr, const char *brokerid, const char *sdpdata, const char *brokeraddr, ice_udp_send_t sendpacket);
void		SVC_ICE_Candidate(const char *brokerid, const char *seq_s, const char *ack_s, const char *canddata, ice_udp_send_t sendpacket);

#endif	/* __ICE_QUAKE_H */

