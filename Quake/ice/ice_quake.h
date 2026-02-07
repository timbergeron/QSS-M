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
qsocket_t	*NQICE_Connect (const char *host);	//used by client (enables websocket connection). fails when not ice, fails when unable to resolve broker, otherwise succeeds pending broker failure.
qsocket_t	*NQICE_CheckNewConnections (void);	//used by server.
void		NQICE_GetAnyMessages(void(*callback)(qsocket_t *));			//used by server.
int			NQICE_GetMessage (qsocket_t *sock);	//used by client.
int			NQICE_SendMessage (qsocket_t *sock, sizebuf_t *data);
int			NQICE_SendUnreliableMessage (qsocket_t *sock, sizebuf_t *data);
qboolean	NQICE_CanSendMessage (qsocket_t *sock);
qboolean	NQICE_CanSendUnreliableMessage (qsocket_t *sock);
void		NQICE_Close (qsocket_t *sock);
void		NQICE_Shutdown (void);

#endif	/* __ICE_QUAKE_H */

