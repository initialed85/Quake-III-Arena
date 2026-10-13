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
// sdl2_main.c -- SDL2 system driver: main loop, event queue, console input

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <dlfcn.h>

#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderer/tr_public.h"

#include "sdl2_shared.h"

#define	MAX_QUED_EVENTS		256
#define	MASK_QUED_EVENTS		( MAX_QUED_EVENTS - 1 )

static sysEvent_t eventQue[MAX_QUED_EVENTS];
static int eventHead = 0;
static int eventTail = 0;
static byte sys_packetReceived[MAX_MSGLEN];

static qboolean stdinActive = qtrue;

/*
================
Sys_QueEvent

A time of 0 will get the current time
Ptr should either be null, or point to a block of data that can
be freed by the game later.
================
*/
void Sys_QueEvent( int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr )
{
	sysEvent_t *ev;

	ev = &eventQue[ eventHead & MASK_QUED_EVENTS ];

	if ( eventHead - eventTail >= MAX_QUED_EVENTS ) {
		Com_Printf("Sys_QueEvent: overflow\n");
		// we are discarding an event, but don't leak memory
		if ( ev->evPtr ) {
			Z_Free( ev->evPtr );
		}
		eventTail++;
	}

	eventHead++;

	if ( time == 0 ) {
		time = Sys_Milliseconds();
	}

	ev->evTime = time;
	ev->evType = type;
	ev->evValue = value;
	ev->evValue2 = value2;
	ev->evPtrLength = ptrLength;
	ev->evPtr = ptr;
}

/*
================
Sys_GetEvent
================
*/
sysEvent_t Sys_GetEvent( void )
{
	sysEvent_t ev;
	char *s;
	msg_t netmsg;
	netadr_t adr;

	// return if we have data
	if ( eventHead > eventTail ) {
		eventTail++;
		return eventQue[ ( eventTail - 1 ) & MASK_QUED_EVENTS ];
	}

	// pump the message loop
	Sys_SendKeyEvents();

	// check for console commands
	s = Sys_ConsoleInput();
	if ( s ) {
		char *b;
		int len;

		len = strlen( s ) + 1;
		b = Z_Malloc( len );
		strcpy( b, s );
		Sys_QueEvent( 0, SE_CONSOLE, 0, 0, len, b );
	}

	// check for other input devices
	IN_Frame();

	// check for network packets
	MSG_Init( &netmsg, sys_packetReceived, sizeof( sys_packetReceived ) );
	if ( Sys_GetPacket( &adr, &netmsg ) ) {
		netadr_t *buf;
		int len;

		// copy out to a separate buffer for queueing
		len = sizeof( netadr_t ) + netmsg.cursize;
		buf = Z_Malloc( len );
		*buf = adr;
		memcpy( buf + 1, netmsg.data, netmsg.cursize );
		Sys_QueEvent( 0, SE_PACKET, 0, 0, len, buf );
	}

	// return if we have data
	if ( eventHead > eventTail ) {
		eventTail++;
		return eventQue[ ( eventTail - 1 ) & MASK_QUED_EVENTS ];
	}

	// create an empty event to return
	memset( &ev, 0, sizeof( ev ) );
	ev.evTime = Sys_Milliseconds();

	return ev;
}

/*
================
Sys_ConsoleInput

Raw-mode stdin line editor, so a dedicated server can be driven from a
terminal. Non-tty stdin is read as whole lines.
================
*/
static struct termios savedTc;
static qboolean ttyOn = qfalse;

char *Sys_ConsoleInput( void )
{
	static char text[256];
	static field_t ttyCon;
	static qboolean ttyChecked = qfalse;
	int avail;
	char key;

	if (!ttyChecked) {
		ttyChecked = qtrue;
#ifdef __EMSCRIPTEN__
		// Emscripten reports a TTY on stdin, but read() on it blocks the main
		// thread (the JS TTY prompts synchronously). There is no terminal in a
		// browser, so the console input path is disabled and the engine drives
		// itself from the canvas and the URL command line.
		return NULL;
#endif
		if (isatty(STDIN_FILENO) == 1) {
			struct termios tc;

			signal(SIGTTIN, SIG_IGN);
			signal(SIGTTOU, SIG_IGN);

			tcgetattr(0, &savedTc);
			tc = savedTc;
			tc.c_lflag &= ~(ECHO | ICANON);
			tc.c_iflag &= ~(ISTRIP | INPCK);
			tc.c_cc[VMIN] = 1;
			tc.c_cc[VTIME] = 0;
			tcsetattr(0, TCSADRAIN, &tc);
			// VMIN=1 VTIME=0 makes read() block until a byte arrives, which stalls
			// Sys_GetEvent and the whole Com_Frame loop. Non-blocking stdin makes the
			// read return -1 (EAGAIN) and the loop keeps running.
			fcntl(0, F_SETFL, O_NONBLOCK);
			Field_Clear(&ttyCon);
			ttyOn = qtrue;
		}
	}

	if (ttyOn) {
		avail = read(0, &key, 1);
		if (avail == -1) {
			return NULL;
		}
		if (key == '\n' || key == '\r') {
			if (ttyCon.cursor > 0 && ttyCon.buffer[0] == '\\') {
				memmove(ttyCon.buffer, ttyCon.buffer + 1, ttyCon.cursor);
				ttyCon.cursor--;
			}
			if (ttyCon.cursor == 0) {
				return NULL;
			}
			Q_strncpyz(text, ttyCon.buffer, sizeof(text));
			write(1, "\n", 1);
			Field_Clear(&ttyCon);
			return text;
		}
		if (key == '\b' || key == 127) {
			if (ttyCon.cursor > 0) {
				ttyCon.cursor--;
				write(1, "\b \b", 3);
			}
			return NULL;
		}
		if (ttyCon.cursor < (int) sizeof(ttyCon.buffer) - 1) {
			ttyCon.buffer[ttyCon.cursor++] = key;
			write(1, &key, 1);
		}
		return NULL;
	}

	if (!com_dedicated || !com_dedicated->value) {
		return NULL;
	}
	if (!stdinActive) {
		return NULL;
	}

	{
		fd_set fdset;
		struct timeval timeout;
		int len;

		FD_ZERO(&fdset);
		FD_SET(0, &fdset); // stdin
		timeout.tv_sec = 0;
		timeout.tv_usec = 0;
		if (select(1, &fdset, NULL, NULL, &timeout) == -1 || !FD_ISSET(0, &fdset)) {
			return NULL;
		}

		len = read(0, text, sizeof(text));
		if (len == 0) { // eof!
			stdinActive = qfalse;
			return NULL;
		}
		if (len < 1) {
			return NULL;
		}
		text[len - 1] = 0; // rip off the \n and terminate

		return text;
	}
}

/*
================
Sys_Print
================
*/
void Sys_Print( const char *msg )
{
	fputs(msg, stdout);
	fflush(stdout);
}

/*
================
Sys_Error
================
*/
void Sys_Error( const char *error, ...)
{
	va_list argptr;
	char string[1024];

	CL_Shutdown();

	va_start(argptr, error);
	vsprintf(string, error, argptr);
	va_end(argptr);
	fprintf(stderr, "Sys_Error: %s\n", string);

	exit(1);
}

/*
================
Sys_Quit
================
*/
void Sys_Quit (void)
{
	CL_Shutdown();
	Sys_ShutdownAudio();
	Sys_ShutdownWindow();
	if (ttyOn) {
		fcntl(0, F_SETFL, 0);
		tcsetattr(0, TCSADRAIN, &savedTc);
	}
	SDL_Quit();
	exit(0);
}

/*
================
Sys_Init
================
*/
void Sys_Init(void)
{
	Cmd_AddCommand("in_restart", Sys_In_Restart_f);
	Cmd_AddCommand("net_restart", Sys_Net_Restart_f);

#ifdef __APPLE__
	Cvar_Set("arch", "sdl2 macos");
#elif defined(__linux__)
	Cvar_Set("arch", "sdl2 linux");
#else
	Cvar_Set("arch", "sdl2 unknown");
#endif

	Cvar_Set("username", Sys_GetCurrentUser());

	// no x86 JIT in this build, so run the game modules as bytecode
	Cvar_Set("vm_game", "1");
	Cvar_Set("vm_cgame", "1");
	Cvar_Set("vm_ui", "1");

	NET_Init();
	IN_Init();
}

//===================================================================
// streamed file reads: the download code asks for read-ahead, we just read
// straight through the filesystem layer

static fileHandle_t streamFile;

void Sys_BeginStreamedFile( fileHandle_t f, int readAhead ) {
	streamFile = f;
}

void Sys_EndStreamedFile( fileHandle_t f ) {
	streamFile = 0;
}

int Sys_StreamedRead( void *buffer, int size, int count, fileHandle_t f ) {
	return FS_Read(buffer, size * count, f) / size;
}

void Sys_StreamSeek( fileHandle_t f, int offset, int origin ) {
	FS_Seek(f, offset, origin);
}

//===================================================================
// dll loading, for development builds that link the game modules directly

void Sys_UnloadDll( void *dllHandle ) {
	if (!dllHandle) {
		Com_Printf("Sys_UnloadDll(NULL)\n");
		return;
	}
	dlclose(dllHandle);
}

extern char *FS_BuildOSPath( const char *base, const char *game, const char *qpath );

void *Sys_LoadDll( const char *name, char *fqpath,
                   int (**entryPoint)(int, ...),
                   int (*systemcalls)(int, ...) )
{
	void *libHandle;
	void (*dllEntry)(int (*syscallptr)(int, ...));
	char fname[MAX_OSPATH];
	char *basepath;
	char *homepath;
	char *pwdpath;
	char *gamedir;
	char *fn;

	*fqpath = 0;

	assert(name);

	snprintf(fname, sizeof(fname), "%s.so", name);

	pwdpath = Sys_Cwd();
	basepath = Cvar_VariableString("fs_basepath");
	homepath = Cvar_VariableString("fs_homepath");
	gamedir = Cvar_VariableString("fs_game");

	fn = FS_BuildOSPath(pwdpath, gamedir, fname);
	Com_Printf("Sys_LoadDll(%s)... \n", fn);
	libHandle = dlopen(fn, RTLD_NOW);

	if (!libHandle) {
		fn = FS_BuildOSPath(homepath, gamedir, fname);
		Com_Printf("Sys_LoadDll(%s)... \n", fn);
		libHandle = dlopen(fn, RTLD_NOW);
	}

	if (!libHandle) {
		fn = FS_BuildOSPath(basepath, gamedir, fname);
		Com_Printf("Sys_LoadDll(%s)... \n", fn);
		libHandle = dlopen(fn, RTLD_NOW);
	}

	if (!libHandle) {
		Com_Printf("Sys_LoadDll(%s) failed\n", fname);
		return NULL;
	}

	Q_strncpyz(fqpath, fn, MAX_OSPATH);

	dllEntry = (void (*)(int (*)(int, ...))) dlsym(libHandle, "dllEntry");
	entryPoint = (int (**)(int, ...)) dlsym(libHandle, "entryPoint");

	if (!dllEntry || !entryPoint) {
		Com_Printf("Sys_LoadDll(%s) didn't find entryPoint or dllEntry\n", fname);
		return NULL;
	}

	dllEntry(systemcalls);

	return libHandle;
}

void Sys_UnloadGame (void) {
}

void *Sys_GetGameAPI (void *parms) {
	return NULL;
}

void Sys_UnloadCGame (void) {
}

void *Sys_GetCGameAPI (void) {
	return NULL;
}

void Sys_UnloadUI (void) {
}

void *Sys_GetUIAPI (void) {
	return NULL;
}

void Sys_UnloadBotLib (void) {
}

void *Sys_GetBotLibAPI (void *parms) {
	return NULL;
}

//===================================================================

int main (int argc, char **argv)
{
	int len, i;
	char *cmdline;
	qboolean dedicated = qfalse;

	// A dedicated server never opens a window, never plays sound. On
	// Emscripten, SDL_INIT_VIDEO initialises the browser video driver, which
	// reads the JS screen object, so a headless run (node, a test harness) has
	// to skip it. Detect +set dedicated 1 from the command line because
	// com_dedicated is not created until Com_Init.
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "dedicated")) {
			if (i + 1 < argc && argv[i+1][0] != '-') {
				dedicated = (argv[i+1][0] != '0');
			} else {
				dedicated = qtrue;
			}
		}
	}

	int sdlFlags = SDL_INIT_TIMER;
	if (!dedicated) {
		sdlFlags |= SDL_INIT_VIDEO | SDL_INIT_AUDIO;
	}
	if (SDL_Init(sdlFlags) != 0) {
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}

	// merge the command line
	for (len = 1, i = 1; i < argc; i++) {
		len += strlen(argv[i]) + 1;
	}
	cmdline = malloc(len);
	*cmdline = 0;
	for (i = 1; i < argc; i++) {
		if (i > 1) {
			strcat(cmdline, " ");
		}
		strcat(cmdline, argv[i]);
	}

	Com_Init(cmdline);

#ifdef __EMSCRIPTEN__
	// The browser cannot block. Com_Frame runs as the main loop callback.
	emscripten_set_main_loop(Com_Frame, 0, 0);
	return 0;
#endif

	while (1) {
		Com_Frame();
	}

	return 0;
}
