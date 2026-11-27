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

#include "q_stdinc.h"
#include "arch_def.h"
#include "net_sys.h"
#include "quakedef.h"
#include "net_defs.h"
#include "net_loop.h"

static qboolean	localconnectpending = false;
static qsocket_t	*loop_client = NULL;
static qsocket_t	*loop_server = NULL;

int Loop_Init (void)
{
	if (cls.state == ca_dedicated)
		return -1;
	return 0;
}


void Loop_Shutdown (void)
{
}


void Loop_Listen (qboolean state)
{
}


qboolean Loop_SearchForHosts (qboolean xmit)
{
	if (!sv.active)
		return false;

	hostCacheCount = 1;
	if (Q_strcmp(hostname.string, "UNNAMED") == 0)
		q_strlcpy(hostcache[0].name, "local", sizeof(hostcache[0].name));
	else
		q_strlcpy(hostcache[0].name, hostname.string, sizeof(hostcache[0].name));
	q_strlcpy(hostcache[0].map, sv.name, sizeof(hostcache[0].map));
	hostcache[0].users = net_activeconnections;
	hostcache[0].maxusers = svs.maxclients;
	hostcache[0].driver = net_driverlevel;
	q_strlcpy(hostcache[0].cname, "local", sizeof(hostcache[0].cname));
	return false;
}


qsocket_t *Loop_Connect (const char *host)
{
	if (Q_strcmp(host,"local") != 0)
		return NULL;

	localconnectpending = true;

	if (!loop_client)
	{
		if ((loop_client = NET_NewQSocket ()) == NULL)
		{
			Con_Printf("Loop_Connect: no qsocket available\n");
			return NULL;
		}
		Q_strcpy (loop_client->trueaddress, "localhost");
		Q_strcpy (loop_client->maskedaddress, "localhost");
	}
	loop_client->receiveMessageLength = 0;
	loop_client->sendMessageLength = 0;
	loop_client->canSend = true;

	if (!loop_server)
	{
		if ((loop_server = NET_NewQSocket ()) == NULL)
		{
			Con_Printf("Loop_Connect: no qsocket available\n");
			return NULL;
		}
		Q_strcpy (loop_server->trueaddress, "LOCAL");
		Q_strcpy (loop_server->maskedaddress, "LOCAL");
	}
	loop_server->receiveMessageLength = 0;
	loop_server->sendMessageLength = 0;
	loop_server->canSend = true;

	loop_client->driverdata = (void *)loop_server;
	loop_server->driverdata = (void *)loop_client;

	loop_client->proquake_angle_hack = loop_server->proquake_angle_hack = true;

	return loop_client;
}


qsocket_t *Loop_CheckNewConnections (void)
{
	if (!localconnectpending)
		return NULL;

	localconnectpending = false;
	loop_server->sendMessageLength = 0;
	loop_server->receiveMessageLength = 0;
	loop_server->canSend = true;
	loop_client->sendMessageLength = 0;
	loop_client->receiveMessageLength = 0;
	loop_client->canSend = true;
	return loop_server;
}


static int IntAlign(int value)
{
	return (value + (sizeof(int) - 1)) & (~(sizeof(int) - 1));
}

int Loop_GetMessage (qsocket_t *sock)
{
	qsocket_t *sender = sock->driverdata;
	int		ret;
	int		length;

	if (sock->receiveMessageLength == 0)
	{
		if (!sender || !sender->sendMessageLength)
			return 0;

		/* The sender's reliable buffer holds a packet that did not fit
		 * behind the datagrams already queued here. */
		SZ_Clear (&net_message);
		SZ_Write (&net_message, sender->sendMessage, sender->sendMessageLength - 1);
		sender->sendMessageLength = 0;
		sender->canSend = true;
		return 1;
	}

	ret = sock->receiveMessage[0];
	length = sock->receiveMessage[1] + (sock->receiveMessage[2] << 8);
	// alignment byte skipped here
	SZ_Clear (&net_message);
	if (ret == 2)
	{	//unreliables have sequences that we (now) care about so that clients can ack them.
		sock->unreliableReceiveSequence = sock->receiveMessage[4] |
			((unsigned int)sock->receiveMessage[5] << 8) |
			((unsigned int)sock->receiveMessage[6] << 16) |
			((unsigned int)sock->receiveMessage[7] << 24);
		sock->unreliableReceiveSequence++;
		SZ_Write (&net_message, &sock->receiveMessage[8], length);
		length = IntAlign(length + 8);
	}
	else
	{	//reliable
		SZ_Write (&net_message, &sock->receiveMessage[4], length);
		length = IntAlign(length + 4);
	}

	sock->receiveMessageLength -= length;

	if (sock->receiveMessageLength)
		memmove (sock->receiveMessage, &sock->receiveMessage[length], sock->receiveMessageLength);

	if (sender && ret == 1)
		sender->canSend = true;

	return ret;
}

void Loop_GetAnyMessages(void(*callback)(qsocket_t *))
{
	while (loop_server && Loop_GetMessage(loop_server) > 0)
		callback(loop_server);
}


int Loop_SendMessage (qsocket_t *sock, sizebuf_t *data)
{
	qsocket_t *receiver = sock->driverdata;
	byte *buffer;
	int queued, end;

	if (!receiver || (unsigned int)data->cursize > sizeof(sock->sendMessage))
		return -1;
	if (!sock->canSend)
		return 0;

	queued = receiver->receiveMessageLength;
	end = IntAlign(queued + data->cursize + 4);
	if (end > (int)sizeof(receiver->receiveMessage))
	{
		if (data->cursize)
			Q_memcpy(sock->sendMessage, data->data, data->cursize);
		/* Zero means no deferred packet. Store length + 1 for an empty packet. */
		sock->sendMessageLength = data->cursize + 1;
		sock->canSend = false;
		return 1;
	}

	buffer = receiver->receiveMessage + queued;

	// message type
	*buffer++ = 1;

	// length
	*buffer++ = data->cursize & 0xff;
	*buffer++ = data->cursize >> 8;

	// align
	buffer++;

	// message
	Q_memcpy(buffer, data->data, data->cursize);
	receiver->receiveMessageLength = end;

	sock->canSend = false;
	return 1;
}


int Loop_SendUnreliableMessage (qsocket_t *sock, sizebuf_t *data)
{
	qsocket_t *receiver = sock->driverdata;
	byte *buffer;
	int queued, end;
	unsigned int sequence = sock->unreliableSendSequence++;

	if (!receiver)
		return -1;

	/* Keep later datagrams behind the deferred reliable packet. Unreliable
	 * sends can be dropped when the receive queue is full. */
	if (sock->sendMessageLength || (unsigned int)data->cursize > sizeof(receiver->receiveMessage))
		return 0;
	queued = receiver->receiveMessageLength;
	end = IntAlign(queued + data->cursize + 8);
	if (end > (int)sizeof(receiver->receiveMessage))
		return 0;

	buffer = receiver->receiveMessage + queued;

	// message type
	*buffer++ = 2;

	// length
	*buffer++ = data->cursize & 0xff;
	*buffer++ = data->cursize >> 8;

	// align
	buffer++;

	*buffer++ = (sequence >>  0) & 0xff;
	*buffer++ = (sequence >>  8) & 0xff;
	*buffer++ = (sequence >> 16) & 0xff;
	*buffer++ = (sequence >> 24) & 0xff;

	// message
	Q_memcpy(buffer, data->data, data->cursize);
	receiver->receiveMessageLength = end;
	return 1;
}


qboolean Loop_CanSendMessage (qsocket_t *sock)
{
	if (!sock->driverdata)
		return false;
	return sock->canSend;
}


qboolean Loop_CanSendUnreliableMessage (qsocket_t *sock)
{
	return true;
}


void Loop_Close (qsocket_t *sock)
{
	if (sock->driverdata)
		((qsocket_t *)sock->driverdata)->driverdata = NULL;
	sock->receiveMessageLength = 0;
	sock->sendMessageLength = 0;
	sock->canSend = true;
	if (sock == loop_client)
		loop_client = NULL;
	else
		loop_server = NULL;
}

