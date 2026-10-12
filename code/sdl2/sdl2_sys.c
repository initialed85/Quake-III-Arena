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
// sdl2_sys.c -- the boring system services: time, filesystem, paths

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <SDL.h>

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"

static char sysCwd[MAX_OSPATH] = { 0 };
static char homePath[MAX_OSPATH] = { 0 };
static char installPath[MAX_OSPATH] = { 0 };

/*
================
Sys_Milliseconds
================
*/
int Sys_Milliseconds (void)
{
	return (int) SDL_GetTicks();
}

/*
================
Sys_SnapVector
================
*/
void Sys_SnapVector( float *v )
{
	v[0] = roundf(v[0]);
	v[1] = roundf(v[1]);
	v[2] = roundf(v[2]);
}

/*
================
Sys_Mkdir
================
*/
void Sys_Mkdir( const char *path )
{
	mkdir(path, 0777);
}

/*
================
strlwr
================
*/
char *strlwr (char *s) {
	if ( s==NULL ) {
		assert(0);
		return s;
	}
	while (*s) {
		*s = tolower(*s);
		s++;
	}
	return s;
}

/*
================
Sys_GetCurrentUser
================
*/
char *Sys_GetCurrentUser( void )
{
	struct passwd *p;

	if ((p = getpwuid(getuid())) == NULL) {
		return "player";
	}
	return p->pw_name;
}

unsigned int Sys_ProcessorCount(void)
{
	return (unsigned int) SDL_GetCPUCount();
}

int Sys_GetProcessorId( void )
{
	return CPUID_GENERIC;
}

int Sys_MonkeyShouldBeSpanked( void )
{
	return 0;
}

void Sys_ShowConsole( int visLevel, qboolean quitOnClose )
{
}

void Sys_DisplaySystemConsole( qboolean show )
{
}

void Sys_SetErrorText( const char *text )
{
}

void Sys_BeginProfiling( void )
{
}

void Sys_EndProfiling( void )
{
}

qboolean Sys_LowPhysicalMemory()
{
	return qfalse;
}

qboolean Sys_CheckCD( void )
{
	return qtrue;
}

char *Sys_GetClipboardData( void )
{
	char *data = NULL;

	data = SDL_GetClipboardText();

	if (!data) {
		return NULL;
	}

	return CopyString(data);
}

//============================================

#define	MAX_FOUND_FILES	0x1000

void Sys_ListFilteredFiles( const char *basedir, char *subdirs, char *filter, char **list, int *numfiles ) {
	char search[MAX_OSPATH], newsubdirs[MAX_OSPATH];
	char filename[MAX_OSPATH];
	DIR *fdir;
	struct dirent *d;
	struct stat st;

	if (*numfiles >= MAX_FOUND_FILES - 1) {
		return;
	}

	if (strlen(subdirs)) {
		Com_sprintf(search, sizeof(search), "%s/%s", basedir, subdirs);
	}
	else {
		Com_sprintf(search, sizeof(search), "%s", basedir);
	}

	if ((fdir = opendir(search)) == NULL) {
		return;
	}

	while ((d = readdir(fdir)) != NULL) {
		Com_sprintf(filename, sizeof(filename), "%s/%s", search, d->d_name);
		if (stat(filename, &st) == -1)
			continue;

		if (st.st_mode & S_IFDIR) {
			if (Q_stricmp(d->d_name, ".") && Q_stricmp(d->d_name, "..")) {
				if (strlen(subdirs)) {
					Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s/%s", subdirs, d->d_name);
				}
				else {
					Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s", d->d_name);
				}
				Sys_ListFilteredFiles(basedir, newsubdirs, filter, list, numfiles);
			}
		}
		if (*numfiles >= MAX_FOUND_FILES - 1) {
			break;
		}
		Com_sprintf(filename, sizeof(filename), "%s/%s", subdirs, d->d_name);
		if (!Com_FilterPath(filter, filename, qfalse))
			continue;
		list[*numfiles] = CopyString(filename);
		(*numfiles)++;
	}

	closedir(fdir);
}

char **Sys_ListFiles( const char *directory, const char *extension, char *filter, int *numfiles, qboolean wantsubs )
{
	struct dirent *d;
	DIR *fdir;
	qboolean dironly = wantsubs;
	char search[MAX_OSPATH];
	int nfiles;
	char **listCopy;
	char *list[MAX_FOUND_FILES];
	int i;
	struct stat st;

	if (filter) {
		nfiles = 0;
		Sys_ListFilteredFiles(directory, "", filter, list, &nfiles);

		list[nfiles] = 0;
		*numfiles = nfiles;

		if (!nfiles)
			return NULL;

		listCopy = Z_Malloc((nfiles + 1) * sizeof(*listCopy));
		for (i = 0 ; i < nfiles ; i++) {
			listCopy[i] = list[i];
		}
		listCopy[i] = NULL;

		return listCopy;
	}

	if (!extension)
		extension = "";

	if (extension[0] == '/' && extension[1] == 0) {
		extension = "";
		dironly = qtrue;
	}

	nfiles = 0;

	if ((fdir = opendir(directory)) == NULL) {
		*numfiles = 0;
		return NULL;
	}

	while ((d = readdir(fdir)) != NULL) {
		Com_sprintf(search, sizeof(search), "%s/%s", directory, d->d_name);
		if (stat(search, &st) == -1)
			continue;
		if ((dironly && !(st.st_mode & S_IFDIR)) ||
			(!dironly && (st.st_mode & S_IFDIR)))
			continue;

		if (*extension) {
			if (strlen(d->d_name) < strlen(extension) ||
				Q_stricmp(d->d_name + strlen(d->d_name) - strlen(extension), extension)) {
				continue; // didn't match
			}
		}

		if (nfiles == MAX_FOUND_FILES - 1)
			break;
		list[nfiles] = CopyString(d->d_name);
		nfiles++;
	}

	list[nfiles] = 0;

	closedir(fdir);

	*numfiles = nfiles;

	if (!nfiles) {
		return NULL;
	}

	listCopy = Z_Malloc((nfiles + 1) * sizeof(*listCopy));
	for (i = 0 ; i < nfiles ; i++) {
		listCopy[i] = list[i];
	}
	listCopy[i] = NULL;

	return listCopy;
}

void Sys_FreeFileList( char **list )
{
	int i;

	if (!list) {
		return;
	}

	for (i = 0 ; list[i] ; i++) {
		Z_Free(list[i]);
	}

	Z_Free(list);
}

/*
================
Sys_Cwd
================
*/
char *Sys_Cwd( void )
{
	if (!sysCwd[0]) {
		getcwd(sysCwd, sizeof(sysCwd) - 1);
	}
	return sysCwd;
}

void Sys_SetDefaultCDPath(const char *path)
{
}

char *Sys_DefaultCDPath(void)
{
	return "";
}

void Sys_SetDefaultInstallPath(const char *path)
{
	Q_strncpyz(installPath, path, sizeof(installPath));
}

char *Sys_DefaultInstallPath(void)
{
	if (installPath[0]) {
		return installPath;
	}
	return Sys_Cwd();
}

void Sys_SetDefaultHomePath(const char *path)
{
	Q_strncpyz(homePath, path, sizeof(homePath));
}

char *Sys_DefaultHomePath(void)
{
	char *p;

	if (!homePath[0]) {
		if ((p = getenv("HOME")) != NULL) {
#ifdef __APPLE__
			Com_sprintf(homePath, sizeof(homePath), "%s/Library/Application Support/Quake3", p);
#else
			Com_sprintf(homePath, sizeof(homePath), "%s/.quake3", p);
#endif
		}
	}

	return homePath;
}
