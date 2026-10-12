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
// sdl2_glimp.c -- OpenGL context and swap buffers through SDL2

#include <SDL.h>

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderer/tr_public.h"
#include "../renderer/tr_local.h"

#include "sdl2_shared.h"

static SDL_Window *sdlWindow;
static SDL_GLContext glContext;

/*
===============
GLimp_NameContains

Case-insensitive substring: SDL reports display names from the display product
string, which is mixed case ("Built-in Retina Display", "Color LCD").
===============
*/
static qboolean GLimp_NameContains( const char *name, const char *needle )
{
	if (!name || !needle) {
		return qfalse;
	}

	for (; *name; name++) {
		const char *a = name, *b = needle;

		while (*b && SDL_tolower((unsigned char)*a) == SDL_tolower((unsigned char)*b)) {
			a++;
			b++;
		}
		if (!*b) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
===============
GLimp_InitExtensions
===============
*/
static void GLimp_InitExtensions( void )
{
	if (!r_allowExtensions->integer) {
		ri.Printf(PRINT_ALL, "*** IGNORING GL_EXTENSIONS ***\n");
		return;
	}

	ri.Printf(PRINT_ALL, "Initializing GL_EXTENSIONS\n");

	if (r_ext_multitexture->integer) {
		qglMultiTexCoord2fARB = (void (APIENTRY *)(GLenum, GLfloat, GLfloat))
			SDL_GL_GetProcAddress("glMultiTexCoord2fARB");
		qglActiveTextureARB = (void (APIENTRY *)(GLenum))
			SDL_GL_GetProcAddress("glActiveTextureARB");
		qglClientActiveTextureARB = (void (APIENTRY *)(GLenum))
			SDL_GL_GetProcAddress("glClientActiveTextureARB");
	}

	if (r_ext_compiled_vertex_array->integer) {
		qglLockArraysEXT = (void (APIENTRY *)(GLint, GLint))
			SDL_GL_GetProcAddress("glLockArraysEXT");
		qglUnlockArraysEXT = (void (APIENTRY *)(void))
			SDL_GL_GetProcAddress("glUnlockArraysEXT");
	}

	if (SDL_GL_ExtensionSupported("GL_ARB_multitexture")) {
		GLint temp;

		qglGetIntegerv(GL_MAX_ACTIVE_TEXTURES_ARB, &temp);
		glConfig.maxActiveTextures = temp;
	} else {
		qglMultiTexCoord2fARB = NULL;
		qglActiveTextureARB = NULL;
		qglClientActiveTextureARB = NULL;
		glConfig.maxActiveTextures = 1;
	}

	// Upload32 passes raw RGBA bytes with a compressed internalFormat, which Apple's
	// GL 2.1 rejects: the upload silently produces an empty texture and every
	// 3-sample surface renders white. The extension string reports s3tc, so trust
	// r_ext_compressed_textures (default 0) rather than the string.
	if (r_ext_compressed_textures->integer && SDL_GL_ExtensionSupported("GL_EXT_texture_compression_s3tc")) {
		glConfig.textureCompression = TC_S3TC;
	} else {
		glConfig.textureCompression = TC_NONE;
	}

	glConfig.textureEnvAddAvailable = SDL_GL_ExtensionSupported("GL_EXT_texture_env_add") == SDL_TRUE;

	if (SDL_GL_ExtensionSupported("GL_EXT_compiled_vertex_array") != SDL_TRUE) {
		qglLockArraysEXT = NULL;
		qglUnlockArraysEXT = NULL;
	}
}

/*
===============
GLimp_CreateWindow
===============
*/
static qboolean GLimp_CreateWindow( void )
{
	int colorbits = r_colorbits->integer;
	int depthbits = r_depthbits->integer;
	int stencilbits = r_stencilbits->integer;
	int flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
	int xpos, ypos;

	if (colorbits == 16) {
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 5);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
	} else {
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	}

	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, depthbits ? depthbits : 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, stencilbits);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);

	if (r_fullscreen->integer) {
		flags |= SDL_WINDOW_FULLSCREEN;
	}

	// SDL_WINDOWPOS_CENTERED is CENTERED_DISPLAY(0), so on a multi-monitor box the
	// window lands on display 0, which may not be the screen you are looking at.
	// SDL_WINDOW_DISPLAY takes a display index, or a name substring (the name SDL
	// reports from the display product string). Unset, we look for the built-in
	// panel so the game lands on the laptop screen rather than an external.
	{
		const char *dispEnv = SDL_getenv("SDL_WINDOW_DISPLAY");
		const char *xyEnv = SDL_getenv("SDL_WINDOW_XY");
		int count = SDL_GetNumVideoDisplays();
		int disp = 0, i, n;
		qboolean picked = qfalse, numeric = qfalse;

		for (i = 0; i < count; i++) {
			ri.Printf(PRINT_ALL, "display %i: %s\n", i, SDL_GetDisplayName(i));
		}

		if (dispEnv) {
			for (n = 0; dispEnv[n]; n++) {
				if (dispEnv[n] < '0' || dispEnv[n] > '9') {
					break;
				}
			}
			numeric = dispEnv[n] == '\0';
			if (numeric) {
				disp = atoi(dispEnv);
				if (disp >= 0 && disp < count) {
					picked = qtrue;
				} else {
					ri.Printf(PRINT_ALL, "SDL_WINDOW_DISPLAY %i is out of range (0..%i)\n", disp, count - 1);
				}
			}
		}

		// Built-in panels report as "Color LCD" / "Built-in Retina Display" on Macs.
		if (!picked) {
			const char *hints[] = { "built", "internal", "panel", "lcd", "lv" };
			int nhints = dispEnv ? 1 : 5;
			int h;

			for (i = 0; i < count; i++) {
				const char *name = SDL_GetDisplayName(i);
				for (h = 0; h < nhints; h++) {
					if (GLimp_NameContains(name, dispEnv ? dispEnv : hints[h])) {
						disp = i;
						picked = qtrue;
						break;
					}
				}
				if (picked) {
					break;
				}
			}
		}

		if (!picked) {
			ri.Printf(PRINT_ALL, "No display matched '%s', using display 0\n",
				dispEnv ? dispEnv : "built");
			disp = 0;
		}

		if (xyEnv) {
			xpos = SDL_WINDOWPOS_UNDEFINED;
			ypos = SDL_WINDOWPOS_UNDEFINED;
			sscanf(xyEnv, "%d,%d", &xpos, &ypos);
		} else {
			xpos = SDL_WINDOWPOS_CENTERED_DISPLAY(disp);
			ypos = SDL_WINDOWPOS_CENTERED_DISPLAY(disp);
		}

		ri.Printf(PRINT_ALL, "Creating window on display %i (\"%s\") of %i\n",
			disp, SDL_GetDisplayName(disp), count);
	}

	sdlWindow = SDL_CreateWindow("Quake III Arena",
		xpos, ypos, glConfig.vidWidth, glConfig.vidHeight, flags);

	if (!sdlWindow) {
		if (r_fullscreen->integer) {
			ri.Printf(PRINT_ALL, "...WARNING: fullscreen unavailable in this mode\n");
			flags &= ~SDL_WINDOW_FULLSCREEN;
			sdlWindow = SDL_CreateWindow("Quake III Arena",
				xpos, ypos, glConfig.vidWidth, glConfig.vidHeight, flags);
		}
	}

	if (!sdlWindow) {
		ri.Printf(PRINT_ALL, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		return qfalse;
	}

	{
		int disp = SDL_GetWindowDisplayIndex(sdlWindow);
		SDL_DisplayMode dm;
		int x = 0, y = 0, w = 0, h = 0;

		SDL_GetWindowPosition(sdlWindow, &x, &y);
		SDL_GetWindowSize(sdlWindow, &w, &h);
		SDL_GetCurrentDisplayMode(disp, &dm);
		ri.Printf(PRINT_ALL, "WINDOW: display %i at (%i,%i) size %ix%i, display mode %ix%i@%iHz, flags 0x%x, focused=%i\n",
			disp, x, y, w, h, dm.w, dm.h, dm.refresh_rate, SDL_GetWindowFlags(sdlWindow),
			(SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_INPUT_FOCUS) ? 1 : 0);
	}

	SDL_RaiseWindow(sdlWindow);

	glContext = SDL_GL_CreateContext(sdlWindow);
	if (!glContext) {
		ri.Printf(PRINT_ALL, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
		SDL_DestroyWindow(sdlWindow);
		sdlWindow = NULL;
		return qfalse;
	}

	SDL_GL_MakeCurrent(sdlWindow, glContext);
	SDL_GL_SetSwapInterval(r_swapInterval->integer);

	return qtrue;
}

/*
===============
GLimp_Init
===============
*/
void GLimp_Init( void )
{
	char buf[1024];

	ri.Printf(PRINT_ALL, "Initializing OpenGL display\n");

	if (!R_GetModeInfo(&glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect,
			r_mode->integer)) {
		ri.Error(ERR_FATAL, "invalid display mode %i", r_mode->integer);
	}

	ri.Printf(PRINT_ALL, "...setting mode %d: %d %d\n", r_mode->integer,
		glConfig.vidWidth, glConfig.vidHeight);

	if (!GLimp_CreateWindow()) {
		ri.Error(ERR_FATAL, "GLimp_Init() - could not create an OpenGL context");
	}

	glConfig.driverType = GLDRV_ICD;
	glConfig.hardwareType = GLHW_GENERIC;

	Q_strncpyz(glConfig.vendor_string, qglGetString(GL_VENDOR), sizeof(glConfig.vendor_string));
	Q_strncpyz(glConfig.renderer_string, qglGetString(GL_RENDERER), sizeof(glConfig.renderer_string));
	if (*glConfig.renderer_string && glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] == '\n') {
		glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] = 0;
	}
	Q_strncpyz(glConfig.version_string, qglGetString(GL_VERSION), sizeof(glConfig.version_string));
	Q_strncpyz(glConfig.extensions_string, qglGetString(GL_EXTENSIONS), sizeof(glConfig.extensions_string));

	{
		int value;

		SDL_GL_GetAttribute(SDL_GL_RED_SIZE, &value);
		glConfig.colorBits = value;
		SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE, &value);
		glConfig.colorBits += value;
		SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE, &value);
		glConfig.colorBits += value;
		SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &value);
		glConfig.depthBits = value;
		SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &value);
		glConfig.stencilBits = value;
	}

	glConfig.displayFrequency = 0;
	glConfig.isFullscreen = r_fullscreen->integer ? qtrue : qfalse;
	glConfig.stereoEnabled = qfalse;
	glConfig.deviceSupportsGamma = qfalse;

	GLimp_InitExtensions();

	strcpy(buf, glConfig.renderer_string);
	strlwr(buf);
}

/*
===============
GLimp_Shutdown
===============
*/
void GLimp_Shutdown( void )
{
	if (glContext) {
		SDL_GL_DeleteContext(glContext);
		glContext = NULL;
	}
	if (sdlWindow) {
		SDL_DestroyWindow(sdlWindow);
		sdlWindow = NULL;
	}
	glConfig.vidWidth = 0;
	glConfig.vidHeight = 0;
}

void Sys_ShutdownWindow( void )
{
	GLimp_Shutdown();
}

/*
===============
GLimp_EndFrame
===============
*/
void GLimp_EndFrame( void )
{
	if (sdlWindow) {
		SDL_GL_SwapWindow(sdlWindow);
	}
}

void GLimp_LogComment( char *comment )
{
}

void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] )
{
}

qboolean GLimp_SpawnRenderThread( void (*function)( void ) )
{
	return qfalse;
}

void *GLimp_RendererSleep( void )
{
	return NULL;
}

void GLimp_FrontEndSleep( void )
{
}

void GLimp_WakeRenderer( void *data )
{
}
