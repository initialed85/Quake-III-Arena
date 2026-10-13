/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// sdl2_net.c -- UDP networking through BSD sockets

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"

#include "sdl2_shared.h"

static cvar_t *net_noudp;
static cvar_t *net_ip;
static cvar_t *net_port;

static qboolean networkingEnabled;
static int ipSocket;

#define MAX_IPS 16
static int numIP;
static byte localIP[MAX_IPS][4];

static char *NET_ErrorString( void )
{
	return strerror(errno);
}

void NetadrToSockadr( netadr_t *a, struct sockaddr_in *s )
{
	memset(s, 0, sizeof(*s));

	if (a->type == NA_BROADCAST) {
		s->sin_family = AF_INET;
		s->sin_port = a->port;
		s->sin_addr.s_addr = INADDR_BROADCAST;
	}
	else if (a->type == NA_IP) {
		s->sin_family = AF_INET;
		memcpy(&s->sin_addr, a->ip, 4);
		s->sin_port = a->port;
	}
}

void SockadrToNetadr( struct sockaddr_in *s, netadr_t *a )
{
	memcpy(a->ip, &s->sin_addr, 4);
	a->port = s->sin_port;
	a->type = NA_IP;
}

qboolean Sys_StringToSockaddr( const char *s, struct sockaddr *sadr )
{
	struct addrinfo hints, *res;

	memset(sadr, 0, sizeof(struct sockaddr_in));
	((struct sockaddr_in *)sadr)->sin_family = AF_INET;
	((struct sockaddr_in *)sadr)->sin_port = 0;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;

	if (getaddrinfo(s, NULL, &hints, &res) != 0) {
		return qfalse;
	}

	memcpy(sadr, res->ai_addr, sizeof(struct sockaddr_in));
	freeaddrinfo(res);

	return qtrue;
}

qboolean Sys_StringToAdr( const char *s, netadr_t *a )
{
	struct sockaddr_in sadr;

	if (!Sys_StringToSockaddr(s, (struct sockaddr *)&sadr)) {
		return qfalse;
	}

	SockadrToNetadr(&sadr, a);

	return qtrue;
}

qboolean Sys_GetPacket( netadr_t *net_from, msg_t *net_message )
{
	int ret;
	struct sockaddr_in from;
	int fromlen;

	if (!ipSocket) {
		return qfalse;
	}

	fromlen = sizeof(from);
	ret = recvfrom(ipSocket, net_message->data, net_message->maxsize, 0,
		(struct sockaddr *)&from, &fromlen);

	SockadrToNetadr(&from, net_from);
	net_message->readcount = 0;

	if (ret == -1) {
		if (errno == EWOULDBLOCK || errno == ECONNREFUSED) {
			return qfalse;
		}
		Com_Printf("NET_GetPacket: %s from %s\n", NET_ErrorString(), NET_AdrToString(*net_from));
		return qfalse;
	}

	if (ret == net_message->maxsize) {
		Com_Printf("Oversize packet from %s\n", NET_AdrToString(*net_from));
		return qfalse;
	}

	net_message->cursize = ret;
	return qtrue;
}

void Sys_SendPacket( int length, const void *data, netadr_t to )
{
	struct sockaddr_in addr;

	if (to.type != NA_IP && to.type != NA_BROADCAST) {
		Com_Error(ERR_FATAL, "NET_SendPacket: bad address type");
		return;
	}

	if (!ipSocket) {
		return;
	}

	NetadrToSockadr(&to, &addr);

	if (sendto(ipSocket, data, length, 0, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		Com_Printf("NET_SendPacket ERROR: %s to %s\n", NET_ErrorString(), NET_AdrToString(to));
	}
}

qboolean Sys_IsLANAddress( netadr_t adr )
{
	int i;

	if (adr.type == NA_LOOPBACK) {
		return qtrue;
	}
	if (adr.type != NA_IP) {
		return qfalse;
	}

	// Class A
	if ((adr.ip[0] & 0x80) == 0x00) {
		for (i = 0; i < numIP; i++) {
			if (adr.ip[0] == localIP[i][0]) {
				return qtrue;
			}
		}
		return qfalse;
	}

	// Class B
	if ((adr.ip[0] & 0xc0) == 0x80) {
		for (i = 0; i < numIP; i++) {
			if (adr.ip[0] == localIP[i][0] && adr.ip[1] == localIP[i][1]) {
				return qtrue;
			}
			if (adr.ip[0] == 172 && localIP[i][0] == 172 &&
				(adr.ip[1] & 0xf0) == 16 && (localIP[i][1] & 0xf0) == 16) {
				return qtrue;
			}
		}
		return qfalse;
	}

	// Class C
	for (i = 0; i < numIP; i++) {
		if (adr.ip[0] == localIP[i][0] && adr.ip[1] == localIP[i][1] && adr.ip[2] == localIP[i][2]) {
			return qtrue;
		}
		if (adr.ip[0] == 192 && localIP[i][0] == 192 && adr.ip[1] == 168 && localIP[i][1] == 168) {
			return qtrue;
		}
	}
	return qfalse;
}

void NET_GetLocalAddress( void )
{
	struct ifaddrs *ifap, *p;

	numIP = 0;

	if (getifaddrs(&ifap) != 0) {
		return;
	}

	for (p = ifap; p; p = p->ifa_next) {
		if (p->ifa_addr == NULL || p->ifa_addr->sa_family != AF_INET) {
			continue;
		}
		if (p->ifa_flags & IFF_LOOPBACK) {
			continue;
		}

		{
			struct sockaddr_in *sin = (struct sockaddr_in *)p->ifa_addr;
			int ip = ntohl(sin->sin_addr.s_addr);

			localIP[numIP][0] = (ip >> 24) & 0xff;
			localIP[numIP][1] = (ip >> 16) & 0xff;
			localIP[numIP][2] = (ip >> 8) & 0xff;
			localIP[numIP][3] = ip & 0xff;
			Com_Printf("IP: %i.%i.%i.%i (%s)\n", localIP[numIP][0], localIP[numIP][1],
				localIP[numIP][2], localIP[numIP][3], p->ifa_name);
			numIP++;
		}
	}

	freeifaddrs(ifap);
}

void Sys_ShowIP( void )
{
	int i;

	for (i = 0; i < numIP; i++) {
		Com_Printf("IP: %i.%i.%i.%i\n", localIP[i][0], localIP[i][1], localIP[i][2], localIP[i][3]);
	}
}

static int NET_IPSocket( char *net_interface, int port )
{
	int newsocket;
	struct sockaddr_in address;
	int one = 1;

	Com_Printf("Opening IP socket: %s:%i\n", net_interface ? net_interface : "localhost", port);

	if ((newsocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		Com_Printf("ERROR: UDP_OpenSocket: socket: %s", NET_ErrorString());
		return 0;
	}

	// make it non-blocking
	if (ioctl(newsocket, FIONBIO, &one) == -1) {
		Com_Printf("ERROR: UDP_OpenSocket: ioctl FIONBIO:%s\n", NET_ErrorString());
		close(newsocket);
		return 0;
	}

	// make it broadcast capable
	if (setsockopt(newsocket, SOL_SOCKET, SO_BROADCAST, (char *)&one, sizeof(one)) == -1) {
		// SO_BROADCAST only matters for the LAN server discovery broadcast, which a
		// browser socket cannot do at all. Emscripten rejects the option, so treat it
		// as advisory there rather than failing the socket.
#ifdef __EMSCRIPTEN__
		Com_DPrintf("UDP_OpenSocket: SO_BROADCAST unavailable (%s)\n", NET_ErrorString());
#else
		Com_Printf("ERROR: UDP_OpenSocket: setsockopt SO_BROADCAST:%s\n", NET_ErrorString());
		close(newsocket);
		return 0;
#endif
	}

	if (!net_interface || !net_interface[0] || !Q_stricmp(net_interface, "localhost")) {
		address.sin_addr.s_addr = INADDR_ANY;
	}
	else {
		Sys_StringToSockaddr(net_interface, (struct sockaddr *)&address);
	}

	address.sin_port = (port == PORT_ANY) ? 0 : htons((short)port);
	address.sin_family = AF_INET;

	if (bind(newsocket, (void *)&address, sizeof(address)) == -1) {
		Com_Printf("ERROR: UDP_OpenSocket: bind: %s\n", NET_ErrorString());
		close(newsocket);
		return 0;
	}

	return newsocket;
}

void NET_OpenIP( void )
{
	int port = net_port->integer;
	int i;

	for (i = 0; i < 10; i++) {
		ipSocket = NET_IPSocket(net_ip->string, port + i);
		if (ipSocket) {
			Cvar_SetValue("net_port", port + i);
			net_port->modified = qfalse;
			NET_GetLocalAddress();
			return;
		}
	}
	Com_Error(ERR_FATAL, "Couldn't allocate IP port");
}

void NET_Config( qboolean enableNetworking )
{
	qboolean modified = net_ip->modified || net_port->modified;

	if (net_noudp->integer) {
		enableNetworking = qfalse;
	}

	if (enableNetworking == networkingEnabled && !modified) {
		return;
	}

	networkingEnabled = enableNetworking;

	if (ipSocket) {
		close(ipSocket);
		ipSocket = 0;
	}

	if (networkingEnabled) {
		NET_OpenIP();
	}
}

void NET_Init( void )
{
	net_noudp = Cvar_Get("net_noudp", "0", 0);
	net_ip = Cvar_Get("net_ip", "localhost", 0);
	net_port = Cvar_Get("net_port", va("%i", PORT_SERVER), 0);

	NET_Config(qtrue);
}

void NET_Shutdown( void )
{
	NET_Config(qfalse);
}

void NET_Restart( void )
{
	NET_Config(networkingEnabled);
}

void Sys_Net_Restart_f( void )
{
	NET_Restart();
}

/*
====================
NET_Sleep

sleeps msec or until the net socket is ready
====================
*/
void NET_Sleep( int msec )
{
	struct timeval timeout;
	fd_set fdset;

	if (!ipSocket || !com_dedicated->integer) {
		return; // we're not a server, just run full speed
	}

	FD_ZERO(&fdset);
	FD_SET(ipSocket, &fdset);
	timeout.tv_sec = msec / 1000;
	timeout.tv_usec = (msec % 1000) * 1000;
	select(ipSocket + 1, &fdset, NULL, NULL, &timeout);
}
