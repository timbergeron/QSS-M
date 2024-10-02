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

#include <curl/curl.h> // woods #libcurl

qsocket_t	*net_activeSockets = NULL;
qsocket_t	*net_freeSockets = NULL;
int		net_numsockets = 0;

qboolean	ipxAvailable = false;
qboolean	ipv4Available = false;
qboolean	ipv6Available = false;

int		net_hostport;
int		DEFAULTnet_hostport = 26000;

char		my_ipx_address[NET_NAMELEN];
char		my_ipv4_address[NET_NAMELEN];
char		my_ipv6_address[NET_NAMELEN];
char        my_public_ip[NET_NAMELEN]; // woods #extip

qboolean	listening = false; // woods #listens

qboolean	slistInProgress = false;
qboolean	slistSilent = false;
enum slistScope_e	slistScope = SLIST_LOOP;
static double	slistStartTime;
static double	slistActiveTime;
static int		slistLastShown;

static void Slist_Send (void *);
static void Slist_Poll (void *);
static PollProcedure	slistSendProcedure = {NULL, 0.0, Slist_Send};
static PollProcedure	slistPollProcedure = {NULL, 0.0, Slist_Poll};

sizebuf_t	net_message;
int		net_activeconnections		= 0;

int		messagesSent			= 0;
int		messagesReceived		= 0;
int		unreliableMessagesSent		= 0;
int		unreliableMessagesReceived	= 0;

cvar_t	net_messagetimeout = {"net_messagetimeout","300",CVAR_NONE};
cvar_t	net_connecttimeout = {"net_connecttimeout","10",CVAR_NONE};	//this might be a little brief, but we don't have a way to protect against smurf attacks.
cvar_t	hostname = {"hostname", "UNNAMED", CVAR_SERVERINFO};

// these two macros are to make the code more readable
#define sfunc	net_drivers[sock->driver]
#define dfunc	net_drivers[net_driverlevel]

int		net_driverlevel;

double		net_time;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) // woods #extip
{
	size_t realsize = size * nmemb;
	char* buffer = (char*)userp;
	size_t buffer_len = strlen(buffer);
	size_t buffer_remaining = NET_NAMELEN - buffer_len - 1;

	if (realsize > buffer_remaining) {
		realsize = buffer_remaining;
	}

	memcpy(buffer + buffer_len, contents, realsize);
	buffer[buffer_len + realsize] = '\0';

	return realsize;
}

int GetExternalIP(void* data) // woods #extip
{
	CURL* curl;
	CURLcode res;
	char public_ip[NET_NAMELEN] = { 0 };

	strncpy(my_public_ip, "UNKNOWN", sizeof(my_public_ip));

	curl = curl_easy_init();
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_URL, "http://checkip.amazonaws.com");
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)public_ip);
		res = curl_easy_perform(curl);
		if (res == CURLE_OK)
		{
			public_ip[strcspn(public_ip, "\n")] = '\0'; // Strip newline
			strncpy(my_public_ip, public_ip, sizeof(my_public_ip) - 1);
			my_public_ip[sizeof(my_public_ip) - 1] = '\0'; // Ensure null-termination
			Con_DPrintf("Public IP: %s\n", my_public_ip);
		}
		else
		{
			Con_DPrintf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
		}
		curl_easy_cleanup(curl);
	}
	else {
		Con_DPrintf("curl_easy_init() failed\n");
	}
	return 0; // Return success
}

double SetNetTime (void)
{
	net_time = Sys_DoubleTime();
	return net_time;
}


/*
===================
NET_NewQSocket

Called by drivers when a new communications endpoint is required
The sequence and buffer fields will be filled in properly
===================
*/
qsocket_t *NET_NewQSocket (void)
{
	qsocket_t	*sock;

	if (net_freeSockets == NULL)
		return NULL;

	if (net_activeconnections >= svs.maxclients)
		return NULL;

	// get one from free list
	sock = net_freeSockets;
	net_freeSockets = sock->next;

	// add it to active list
	sock->next = net_activeSockets;
	net_activeSockets = sock;

	sock->isvirtual = false;
	sock->disconnected = false;
	sock->connecttime = net_time;
	Q_strcpy (sock->trueaddress,"UNSET ADDRESS");
	Q_strcpy (sock->maskedaddress,"UNSET ADDRESS");
	sock->driver = net_driverlevel;
	sock->socket = 0;
	sock->driverdata = NULL;
	sock->canSend = true;
	sock->sendNext = false;
	sock->lastMessageTime = net_time;
	sock->ackSequence = 0;
	sock->sendSequence = 0;
	sock->unreliableSendSequence = 0;
	sock->sendMessageLength = 0;
	sock->receiveSequence = 0;
	sock->unreliableReceiveSequence = 0;
	sock->receiveMessageLength = 0;
	sock->pending_max_datagram = 1024;
	sock->proquake_angle_hack = false;

	return sock;
}


void NET_FreeQSocket(qsocket_t *sock)
{
	qsocket_t	*s;

	// remove it from active list
	if (sock == net_activeSockets)
		net_activeSockets = net_activeSockets->next;
	else
	{
		for (s = net_activeSockets; s; s = s->next)
		{
			if (s->next == sock)
			{
				s->next = sock->next;
				break;
			}
		}

		if (!s)
			Sys_Error ("NET_FreeQSocket: not active");
	}

	// add it to free list
	sock->next = net_freeSockets;
	net_freeSockets = sock;
	sock->disconnected = true;
}


int NET_QSocketGetSequenceIn (const qsocket_t *s)
{	//returns the last unreliable sequence that was received
	return s->unreliableReceiveSequence-1;
}
int NET_QSocketGetSequenceOut (const qsocket_t *s)
{	//returns the next unreliable sequence that will be sent
	return s->unreliableSendSequence;
}
double NET_QSocketGetTime (const qsocket_t *s)
{
	return s->connecttime;
}


const char *NET_QSocketGetTrueAddressString (const qsocket_t *s)
{
	return s->trueaddress;
}
const char *NET_QSocketGetMaskedAddressString (const qsocket_t *s)
{
	return s->maskedaddress;
}
qboolean NET_QSocketGetProQuakeAngleHack(const qsocket_t *s)
{
	if (s && !s->disconnected)
		return s->proquake_angle_hack;
	else
		return false;	//happens with demos
}
void NET_QSocketSetMSS(qsocket_t *s, int mss)
{
	s->pending_max_datagram = mss;
}


static void NET_Listen_f (void)
{
	if (Cmd_Argc () != 2)
	{
		Con_Printf ("\"listen\" is \"%d\"\n", listening ? 1 : 0);
		return;
	}

	listening = Q_atoi(Cmd_Argv(1)) ? true : false;

	for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++)
	{
		if (net_drivers[net_driverlevel].initialized == false)
			continue;
		dfunc.Listen (listening);
	}
}


static void MaxPlayers_f (void)
{
	int	n;

	if (Cmd_Argc () != 2)
	{
		Con_Printf ("\"maxplayers\" is \"%d\"\n", svs.maxclients);
		return;
	}

	if (sv.active)
	{
		Con_Printf ("maxplayers can not be changed while a server is running.\n");
		return;
	}

	n = Q_atoi(Cmd_Argv(1));
	if (n < 1)
		n = 1;
	if (n > svs.maxclientslimit)
	{
		n = svs.maxclientslimit;
		Con_Printf ("\"maxplayers\" set to \"%d\"\n", n);
	}

	if ((n == 1) && listening)
		Cbuf_AddText ("listen 0\n");

	if ((n > 1) && (!listening))
		Cbuf_AddText ("listen 1\n");

	svs.maxclients = n;
	if (n == 1)
		Cvar_Set ("deathmatch", "0");
	else
		Cvar_Set ("deathmatch", "1");
}


static void NET_Port_f (void)
{
	int	n;

	if (Cmd_Argc () != 2)
	{
		Con_Printf ("\"port\" is \"%d\"\n", net_hostport);
		return;
	}

	n = Q_atoi(Cmd_Argv(1));
	if (n < 1 || n > 65534)
	{
		Con_Printf ("Bad value, must be between 1 and 65534\n");
		return;
	}

	DEFAULTnet_hostport = n;
	net_hostport = n;

	if (listening)
	{
		// force a change to the new port
		Cbuf_AddText ("listen 0\n");
		Cbuf_AddText ("listen 1\n");
	}
}


static void PrintSlistHeader(void)
{
	Con_Printf("Server          Map             Users\n");
	Con_Printf("--------------- --------------- -----\n");
	slistLastShown = 0;
}


static void PrintSlist(void)
{
	size_t	n;

	for (n = slistLastShown; n < hostCacheCount; n++)
	{
		if (hostcache[n].maxusers)
			Con_Printf("%-15.15s %-15.15s %2u/%2u\n", hostcache[n].name, hostcache[n].map, hostcache[n].users, hostcache[n].maxusers);
		else
			Con_Printf("%-15.15s %-15.15s\n", hostcache[n].name, hostcache[n].map);
	}
	slistLastShown = n;
}


static void PrintSlistTrailer(void)
{
	if (hostCacheCount)
		Con_Printf("== end list ==\n\n");
	else
		Con_Printf("No Quake servers found.\n\n");
}


void NET_Slist_f (void)
{
	if (slistInProgress)
		return;

	if (! slistSilent)
	{
		Con_Printf("Looking for Quake servers...\n");
		PrintSlistHeader();
	}

	slistInProgress = true;
	slistActiveTime = slistStartTime = Sys_DoubleTime();

	SchedulePollProcedure(&slistSendProcedure, 0.0);
	SchedulePollProcedure(&slistPollProcedure, 0.1);

	hostCacheCount = 0;
}


void NET_SlistSort (void)
{
	if (hostCacheCount > 1)
	{
		size_t	i, j;
		hostcache_t temp;
		for (i = 0; i < hostCacheCount; i++)
		{
			for (j = i + 1; j < hostCacheCount; j++)
			{
				if (strcmp(hostcache[j].name, hostcache[i].name) < 0)
				{
					memcpy(&temp, &hostcache[j], sizeof(hostcache_t));
					memcpy(&hostcache[j], &hostcache[i], sizeof(hostcache_t));
					memcpy(&hostcache[i], &temp, sizeof(hostcache_t));
				}
			}
		}
	}
}


const char *NET_SlistPrintServer (size_t idx)
{
	static char	string[64];

	if (idx >= hostCacheCount)
		return "";

	if (hostcache[idx].maxusers)
	{
		q_snprintf(string, sizeof(string), "%-15.15s %-15.15s %2u/%2u\n",
					hostcache[idx].name, hostcache[idx].map,
					hostcache[idx].users, hostcache[idx].maxusers);
	}
	else
	{
		q_snprintf(string, sizeof(string), "%-15.15s %-15.15s\n",
					hostcache[idx].name, hostcache[idx].map);
	}

	return string;
}


const char *NET_SlistPrintServerName (size_t idx)
{
	if (idx >= hostCacheCount)
		return "";
	return hostcache[idx].cname;
}

const char* NET_SlistPrintServerInfo (size_t idx, ServerInfoType type) // woods #serversmenu
{
	static char buf[32];

	if (idx >= hostCacheCount) {
		return "";
	}
	switch (type) {
	case SERVER_NAME:
		return hostcache[idx].name;
	case SERVER_CNAME:
		return hostcache[idx].cname;
	case SERVER_MAP:
		return hostcache[idx].map;
	case SERVER_USERS:
		snprintf(buf, sizeof(buf), "%d", hostcache[idx].users);
		return buf;
	case SERVER_MAX_USERS:
		snprintf(buf, sizeof(buf), "%d", hostcache[idx].maxusers);
		return buf;
	default:
		return "";
	}
}

static void Slist_Send (void *unused)
{
	for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++)
	{
		if (slistScope!=SLIST_LOOP && IS_LOOP_DRIVER(net_driverlevel))
			continue;
		if (net_drivers[net_driverlevel].initialized == false)
			continue;
		dfunc.SearchForHosts (true);
	}

	if ((Sys_DoubleTime() - slistStartTime) < 0.5)
		SchedulePollProcedure(&slistSendProcedure, 0.75);
}


static void Slist_Poll (void *unused)
{
	for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++)
	{
		if (slistScope!=SLIST_LOOP && IS_LOOP_DRIVER(net_driverlevel))
			continue;
		if (net_drivers[net_driverlevel].initialized == false)
			continue;
		if (dfunc.SearchForHosts (false))
			slistActiveTime = Sys_DoubleTime();	//something was sent, reset the timer.
	}

	if (! slistSilent)
		PrintSlist();

	if ((Sys_DoubleTime() - slistActiveTime) < 1.5)
	{
		SchedulePollProcedure(&slistPollProcedure, 0.1);
		return;
	}

	if (! slistSilent)
		PrintSlistTrailer();
	slistInProgress = false;
	slistSilent = false;
	slistScope = SLIST_LOOP;
}


/*
===================
NET_Connect
===================
*/

size_t hostCacheCount = 0;
hostcache_t hostcache[HOSTCACHESIZE];

qsocket_t *NET_Connect (const char *host)
{
	qsocket_t		*ret;
	size_t				n;
	int				numdrivers = net_numdrivers;

	SetNetTime();

	if (host && *host == 0)
		host = NULL;

	if (host)
	{
		if (q_strcasecmp (host, "local") == 0)
		{
			numdrivers = 1;
			goto JustDoIt;
		}

		if (hostCacheCount)
		{
			for (n = 0; n < hostCacheCount; n++)
				if (q_strcasecmp (host, hostcache[n].name) == 0)
				{
					host = hostcache[n].cname;
					break;
				}
			if (n < hostCacheCount)
				goto JustDoIt;
		}
	}

	slistSilent = host ? true : false;
	NET_Slist_f ();

	while (slistInProgress)
		NET_Poll();

	if (host == NULL)
	{
		if (hostCacheCount != 1)
			return NULL;
		host = hostcache[0].cname;
		Con_Printf("Connecting to...\n%s @ %s\n\n", hostcache[0].name, host);
	}

	if (hostCacheCount)
	{
		for (n = 0; n < hostCacheCount; n++)
		{
			if (q_strcasecmp (host, hostcache[n].name) == 0)
			{
				host = hostcache[n].cname;
				break;
			}
		}
	}

JustDoIt:
	for (net_driverlevel = 0; net_driverlevel < numdrivers; net_driverlevel++)
	{
		if (net_drivers[net_driverlevel].initialized == false)
			continue;
		ret = dfunc.Connect (host);
		if (ret)
			return ret;
	}

	if (host)
	{
		Con_Printf("\n");
		PrintSlistHeader();
		PrintSlist();
		PrintSlistTrailer();
	}

	return NULL;
}


/*
===================
NET_CheckNewConnections
===================
*/
qsocket_t *NET_CheckNewConnections (void)
{
	qsocket_t	*ret;

	SetNetTime();

	for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++)
	{
		if (net_drivers[net_driverlevel].initialized == false)
			continue;
		if (!IS_LOOP_DRIVER(net_driverlevel) && listening == false)
			continue;
		ret = dfunc.CheckNewConnections ();
		if (ret)
		{
			return ret;
		}
	}

	return NULL;
}

/*
===================
NET_Close
===================
*/
void NET_Close (qsocket_t *sock)
{
	if (!sock)
		return;

	if (sock->disconnected)
		return;

	SetNetTime();

	// call the driver_Close function
	sfunc.Close (sock);

	NET_FreeQSocket(sock);
}


/*
=================
NET_GetMessage

If there is a complete message, return it in net_message

returns 0 if no data is waiting
returns 1 if a message was received
returns -1 if connection is invalid
=================
*/
int	NET_GetMessage (qsocket_t *sock)
{
	int ret;

	if (!sock)
		return -1;

	if (sock->disconnected)
	{
		Con_Printf("NET_GetMessage: disconnected socket\n");
		return -1;
	}

	SetNetTime();

	ret = sfunc.QGetMessage(sock);

	// see if this connection has timed out
	if (ret == 0 && !IS_LOOP_DRIVER(sock->driver))
	{
		if (net_time - sock->lastMessageTime > net_messagetimeout.value)
		{
			NET_Close(sock);
			return -1;
		}
	}

	if (ret > 0)
	{
		if (!IS_LOOP_DRIVER(sock->driver))
		{
			sock->lastMessageTime = net_time;
			if (ret == 1)
				messagesReceived++;
			else if (ret == 2)
				unreliableMessagesReceived++;
		}
	}

	return ret;
}

/*
=================
NET_GetServerMessage

If there is a complete message, return it in net_message

returns the qsocket that the message was meant to be for.
=================
*/
qsocket_t *NET_GetServerMessage(void)
{
	qsocket_t *s;
	for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++)
	{
		if (!net_drivers[net_driverlevel].initialized)
			continue;
		s = net_drivers[net_driverlevel].QGetAnyMessage();
		if (s)
			return s;
	}
	return NULL;
}

/*
Spike: This function is for the menus+status command
Just queries each driver's public addresses (which often requires system-specific calls)
*/
int NET_ListAddresses(qhostaddr_t *addresses, int maxaddresses)
{
	int result = 0;
	for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++)
	{
		if (!net_drivers[net_driverlevel].initialized)
			continue;
		if (net_drivers[net_driverlevel].QueryAddresses)
			result += net_drivers[net_driverlevel].QueryAddresses(addresses+result, maxaddresses-result);
	}
	return result;
}

#if defined(__APPLE__) || defined(__linux__)
#include <ifaddrs.h>
#include <net/if.h>

int Unix_QueryAddresses(qhostaddr_t* addresses, int maxaddresses) // woods #extip
{
	struct ifaddrs* ifaddr, * ifa;
	int family;
	int count = 0;

	if (getifaddrs(&ifaddr) == -1) {
		perror("getifaddrs");
		return 0;
	}

	for (ifa = ifaddr; ifa != NULL && count < maxaddresses; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL)
			continue;

		family = ifa->ifa_addr->sa_family;

		// Skip loopback interfaces
		if (ifa->ifa_flags & IFF_LOOPBACK)
			continue;

		// Only interested in IPv4 addresses (use AF_INET6 for IPv6)
		if (family == AF_INET) {
			char host[NI_MAXHOST];

			if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
				host, sizeof(host), NULL, 0, NI_NUMERICHOST) != 0) {
				continue;
			}

			// Store the address directly into addresses[count]
			q_strlcpy(addresses[count], host, NET_NAMELEN);
			count++;
		}
	}

	freeifaddrs(ifaddr);
	return count;
}
#endif

/*
==================
NET_SendMessage

Try to send a complete length+message unit over the reliable stream.
returns 0 if the message cannot be delivered reliably, but the connection
		is still considered valid
returns 1 if the message was sent properly
returns -1 if the connection died
==================
*/
int NET_SendMessage (qsocket_t *sock, sizebuf_t *data)
{
	int		r;

	if (!sock)
		return -1;

	if (sock->disconnected)
	{
		Con_Printf("NET_SendMessage: disconnected socket\n");
		return -1;
	}

	SetNetTime();
	r = sfunc.QSendMessage(sock, data);
	if (r == 1 && !IS_LOOP_DRIVER(sock->driver))
		messagesSent++;

	return r;
}


int NET_SendUnreliableMessage (qsocket_t *sock, sizebuf_t *data)
{
	int		r;

	if (!sock)
		return -1;

	if (sock->disconnected)
	{
		Con_Printf("NET_SendMessage: disconnected socket\n");
		return -1;
	}

	SetNetTime();
	r = sfunc.SendUnreliableMessage(sock, data);
	if (r == 1 && !IS_LOOP_DRIVER(sock->driver))
		unreliableMessagesSent++;

	return r;
}


/*
==================
NET_CanSendMessage

Returns true or false if the given qsocket can currently accept a
message to be transmitted.
==================
*/
qboolean NET_CanSendMessage (qsocket_t *sock)
{
	if (!sock)
		return false;

	if (sock->disconnected)
		return false;

	SetNetTime();

	return sfunc.CanSendMessage(sock);
}


int NET_SendToAll (sizebuf_t *data, double blocktime)
{
	double		start;
	int			i;
	int			count = 0;
	qboolean	msg_init[MAX_SCOREBOARD];	/* did we write the message to the client's connection	*/
	qboolean	msg_sent[MAX_SCOREBOARD];	/* did the msg arrive its destination (canSend state).	*/

	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
	{
		/*
		if (!host_client->netconnection)
			continue;
		if (host_client->active)
		*/
		if (host_client->netconnection && host_client->active)
		{
			if (IS_LOOP_DRIVER(host_client->netconnection->driver))
			{
				NET_SendMessage(host_client->netconnection, data);
				msg_init[i] = true;
				msg_sent[i] = true;
				continue;
			}
			count++;
			msg_init[i] = false;
			msg_sent[i] = false;
		}
		else
		{
			msg_init[i] = true;
			msg_sent[i] = true;
		}
	}

	start = Sys_DoubleTime();
	while (count)
	{
		count = 0;
		for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		{
			if (! msg_init[i])
			{
				if (NET_CanSendMessage (host_client->netconnection))
				{
					msg_init[i] = true;
					NET_SendMessage(host_client->netconnection, data);
				}
				else
				{
					NET_GetMessage (host_client->netconnection);
				}
				count++;
				continue;
			}

			if (! msg_sent[i])
			{
				if (NET_CanSendMessage (host_client->netconnection))
				{
					msg_sent[i] = true;
				}
				else
				{
					NET_GetMessage (host_client->netconnection);
				}
				count++;
				continue;
			}
		}
		if ((Sys_DoubleTime() - start) > blocktime)
			break;
	}
	return count;
}

void IP_f (void) // woods #extip
{
	int numaddresses;
	qhostaddr_t addresses[16];
	char buf[MAX_OSPATH];
	int argc = Cmd_Argc();

	numaddresses = NET_ListAddresses(addresses, sizeof(addresses) / sizeof(addresses[0]));

	if (argc >= 2)
	{
		const char* arg = Cmd_Argv(1);

		if (!q_strcasecmp(arg, "ext"))
		{
			if (SDL_SetClipboardText(my_public_ip) < 0)
				Con_Printf("\nclipboard copy failed: %s\n\n", SDL_GetError());
			else
				Con_Printf("\nexternal IP copied to clipboard: ^m%s^m\n\n", my_public_ip);
			return;

		}
		else if (!q_strcasecmp(arg, "local"))
		{
			if (numaddresses > 0 && addresses[0][0] != '\0')
			{
				strncpy(buf, addresses[0], sizeof(buf) - 1);
				buf[sizeof(buf) - 1] = '\0';

				if (SDL_SetClipboardText(buf) < 0)
					Con_Printf("\nclipboard copy failed: %s\n\n", SDL_GetError());
				else
					Con_Printf("\nlocal IP copied to clipboard: ^m%s^m\n\n", buf);
				return;
			}
			else
				Con_Printf("\nno local IP addresses found\n\n");

			return;
		}
		else
		{
			Con_Printf("\ninvalid argument. usage:\n");
			Con_Printf("  ip                    - display local and external IPs\n");
			Con_Printf("  ip ext                - copy external IP to clipboard\n");
			Con_Printf("  ip local              - copy local IP to clipboard\n\n");
			return;
		}
	}

	if (numaddresses == 0 || addresses[0][0] == '\0')
		Con_Printf("\n\nlocal:    UNKNOWN\n");
	else
		Con_Printf("\n\nlocal:    ^m%s^m\n", addresses[0]);

	Con_Printf("external: ^m%s^m\n\n", my_public_ip);
}

//=============================================================================

/*
====================
NET_Init
====================
*/

void NET_Init (void)
{
	int			i;
	qsocket_t	*s;

	i = COM_CheckParm ("-port");
	if (!i)
		i = COM_CheckParm ("-udpport");
	if (!i)
		i = COM_CheckParm ("-ipxport");

	if (i)
	{
		if (i < com_argc-1)
			DEFAULTnet_hostport = Q_atoi (com_argv[i+1]);
		else
			Sys_Error ("NET_Init: you must specify a number after -port");
	}
	net_hostport = DEFAULTnet_hostport;

	net_numsockets = svs.maxclientslimit;
	if (cls.state != ca_dedicated)
		net_numsockets++;
	if (COM_CheckParm("-listen") || cls.state == ca_dedicated)
		listening = true;

	SetNetTime();

	for (i = 0; i < net_numsockets; i++)
	{
		s = (qsocket_t *)Hunk_AllocName(sizeof(qsocket_t), "qsocket");
		s->next = net_freeSockets;
		net_freeSockets = s;
		s->disconnected = true;
	}

	// allocate space for network message buffer
	SZ_Alloc (&net_message, NET_MAXMESSAGE);

	Cvar_RegisterVariable (&net_messagetimeout);
	Cvar_RegisterVariable (&net_connecttimeout);
	Cvar_RegisterVariable (&hostname);

	Cmd_AddCommand ("slist", NET_Slist_f);
	Cmd_AddCommand ("listen", NET_Listen_f);
	Cmd_AddCommand ("maxplayers", MaxPlayers_f);
	Cmd_AddCommand ("port", NET_Port_f);
	Cmd_AddCommand ("ip", IP_f); // woods #extip

	// initialize all the drivers
	for (i = net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++)
	{
		if (net_drivers[net_driverlevel].Init() == -1)
			continue;
		i++;
		net_drivers[net_driverlevel].initialized = true;

#if defined(__APPLE__) || defined(__linux__)
		if (strcmp(net_drivers[net_driverlevel].name, "Datagram") == 0) // woods #extip
			net_drivers[net_driverlevel].QueryAddresses = Unix_QueryAddresses;
#endif

		if (listening)
			net_drivers[net_driverlevel].Listen (true);
	}

	/* Loop_Init() returns -1 for dedicated server case,
	 * therefore the i == 0 check is correct */
	if (i == 0
			&& cls.state == ca_dedicated
	   )
	{
		Sys_Error("Network not available!");
	}

	if (*my_ipx_address)
	{
		Con_DPrintf("IPX address %s\n", my_ipx_address);
	}
	if (*my_ipv4_address)
	{
		Con_DPrintf("IPv4 address %s\n", my_ipv4_address);
	}
	if (*my_ipv6_address)
	{
		Con_DPrintf("IPv6 address %s\n", my_ipv6_address);
	}

	curl_global_init(CURL_GLOBAL_DEFAULT); // woods #libcurl

	SDL_CreateThread(GetExternalIP, "ExternalIPThread", NULL); // woods #extip
}

/*
====================
NET_Shutdown
====================
*/

void NET_Shutdown (void)
{
	qsocket_t	*sock;

	curl_global_cleanup(); // woods #libcurl

	SetNetTime();

	for (sock = net_activeSockets; sock; sock = sock->next)
		NET_Close(sock);

//
// shutdown the drivers
//
	for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++)
	{
		if (net_drivers[net_driverlevel].initialized == true)
		{
			net_drivers[net_driverlevel].Shutdown ();
			net_drivers[net_driverlevel].initialized = false;
		}
	}
}


static PollProcedure *pollProcedureList = NULL;

void NET_Poll(void)
{
	PollProcedure *pp;

	SetNetTime();

	for (pp = pollProcedureList; pp; pp = pp->next)
	{
		if (pp->nextTime > net_time)
			break;
		pollProcedureList = pp->next;
		pp->procedure(pp->arg);
	}
}


void SchedulePollProcedure(PollProcedure *proc, double timeOffset)
{
	PollProcedure *pp, *prev;

	proc->nextTime = Sys_DoubleTime() + timeOffset;
	for (pp = pollProcedureList, prev = NULL; pp; pp = pp->next)
	{
		if (pp->nextTime >= proc->nextTime)
			break;
		prev = pp;
	}

	if (prev == NULL)
	{
		proc->next = pollProcedureList;
		pollProcedureList = proc;
		return;
	}

	proc->next = pp;
	prev->next = proc;
}

