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
// sdl2_snddma.c -- audio output through the SDL2 audio callback

#include <string.h>

#include <SDL.h>

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../client/snd_local.h"

#include "sdl2_shared.h"

static qboolean sndInited;
static SDL_AudioDeviceID sdlDevice;

static int dmapos;
static int dmasize;

static cvar_t *s_sdlBits;
static cvar_t *s_sdlSpeed;
static cvar_t *s_sdlChannels;
static cvar_t *s_sdlDevSamps;
static cvar_t *s_sdlMixSamps;

/*
===============
SNDDMA_AudioCallback

The DMA buffer is written by the mixer on the main thread; the callback
copies the next chunk of it into the SDL stream.
===============
*/
static void SNDDMA_AudioCallback( void *userdata, Uint8 *stream, int len )
{
	int pos = dmapos * (dma.samplebits / 8);

	if (pos >= dmasize) {
		dmapos = pos = 0;
	}

	if (!sndInited) {
		memset(stream, 0, len);
		return;
	}

	{
		int tobufend = dmasize - pos;
		int len1 = len > tobufend ? tobufend : len;
		int len2 = len - len1;

		memcpy(stream, dma.buffer + pos, len1);
		if (len2 > 0) {
			memcpy(stream + len1, dma.buffer, len2);
			dmapos = len2 / (dma.samplebits / 8);
		} else {
			dmapos += len1 / (dma.samplebits / 8);
		}
	}

	if (dmapos >= dmasize) {
		dmapos = 0;
	}
}

/*
===============
SNDDMA_Init
===============
*/
qboolean SNDDMA_Init( void )
{
	SDL_AudioSpec desired, obtained;
	int bits, samples;

	if (sndInited) {
		return qtrue;
	}

	s_sdlBits = Cvar_Get("s_sdlBits", "16", CVAR_ARCHIVE);
	s_sdlSpeed = Cvar_Get("s_sdlSpeed", "0", CVAR_ARCHIVE);
	s_sdlChannels = Cvar_Get("s_sdlChannels", "2", CVAR_ARCHIVE);
	s_sdlDevSamps = Cvar_Get("s_sdlDevSamps", "0", CVAR_ARCHIVE);
	s_sdlMixSamps = Cvar_Get("s_sdlMixSamps", "0", CVAR_ARCHIVE);

	Com_Printf("SDL audio driver is \"%s\".\n", SDL_GetCurrentAudioDriver());

	memset(&desired, 0, sizeof(desired));
	memset(&obtained, 0, sizeof(obtained));

	bits = s_sdlBits->integer;
	if (bits != 16 && bits != 8) {
		bits = 16;
	}

	desired.freq = s_sdlSpeed->integer ? s_sdlSpeed->integer : 22050;
	desired.format = (bits == 16) ? AUDIO_S16SYS : AUDIO_U8;
	desired.channels = s_sdlChannels->integer;

	if (s_sdlDevSamps->integer) {
		desired.samples = s_sdlDevSamps->integer;
	} else {
		if (desired.freq <= 11025) {
			desired.samples = 256;
		} else if (desired.freq <= 22050) {
			desired.samples = 512;
		} else if (desired.freq <= 44100) {
			desired.samples = 1024;
		} else {
			desired.samples = 2048;
		}
	}
	desired.callback = SNDDMA_AudioCallback;

	sdlDevice = SDL_OpenAudioDevice(NULL, SDL_FALSE, &desired, &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);
	if (!sdlDevice) {
		Com_Printf("SDL_OpenAudioDevice() failed: %s\n", SDL_GetError());
		return qfalse;
	}

	Com_Printf("SDL audio: format %d-bit, %d Hz, %d channels, %d samples\n",
		SDL_AUDIO_BITSIZE(obtained.format), obtained.freq, obtained.channels, obtained.samples);

	// the mixer needs a buffer much larger than one callback chunk
	samples = s_sdlMixSamps->integer ? s_sdlMixSamps->integer
		: obtained.samples * obtained.channels * 10;
	samples -= samples % obtained.channels;

	dmapos = 0;
	dma.samplebits = SDL_AUDIO_BITSIZE(obtained.format);
	dma.channels = obtained.channels;
	dma.samples = samples;
	dma.submission_chunk = 1;
	dma.speed = obtained.freq;
	dmasize = samples * (dma.samplebits / 8);
	dma.buffer = Z_Malloc(dmasize);
	Com_Memset(dma.buffer, 0, dmasize);

	SDL_PauseAudioDevice(sdlDevice, 0);

	sndInited = qtrue;
	return qtrue;
}

/*
===============
SNDDMA_GetDMAPos
===============
*/
int SNDDMA_GetDMAPos( void )
{
	return dmapos;
}

/*
===============
SNDDMA_Shutdown
===============
*/
void SNDDMA_Shutdown( void )
{
	if (!sndInited) {
		return;
	}

	SDL_CloseAudioDevice(sdlDevice);
	sdlDevice = 0;

	Z_Free(dma.buffer);
	dma.buffer = NULL;
	dmapos = dmasize = 0;
	sndInited = qfalse;
}

void Sys_ShutdownAudio( void )
{
	SNDDMA_Shutdown();
}

/*
===============
SNDDMA_BeginPainting
===============
*/
void SNDDMA_BeginPainting( void )
{
	SDL_LockAudioDevice(sdlDevice);
}

/*
===============
SNDDMA_Submit
===============
*/
void SNDDMA_Submit( void )
{
	SDL_UnlockAudioDevice(sdlDevice);
}
