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
// sdl2_input.c -- keyboard, mouse and joystick through SDL2

#include <SDL.h>

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../client/keys.h"

#include "sdl2_shared.h"

static cvar_t *in_mouse;
static cvar_t *in_nograb;
static cvar_t *in_joystick;
static cvar_t *in_joystickThreshold;
static cvar_t *in_joystickNo;
static cvar_t *in_keyboardDebug;

static qboolean mouseActive;
static SDL_Joystick *stick;

/*
===============
IN_PrintKey
===============
*/
static void IN_PrintKey( const SDL_Keysym *keysym, int key, qboolean down )
{
	Com_Printf(down ? "+ " : "  ");
	Com_Printf("Scancode: 0x%02x Sym: 0x%02x Q:0x%02x\n",
		keysym->scancode, keysym->sym, key);
}

/*
===============
IN_TranslateSDLToQ3Key
===============
*/
static int IN_TranslateSDLToQ3Key( SDL_Keysym *keysym )
{
	int key = 0;

	if (keysym->scancode >= SDL_SCANCODE_1 && keysym->scancode <= SDL_SCANCODE_0) {
		if (keysym->scancode == SDL_SCANCODE_0) {
			key = '0';
		} else {
			key = '1' + keysym->scancode - SDL_SCANCODE_1;
		}
	}
	else if (keysym->sym >= SDLK_SPACE && keysym->sym < SDLK_DELETE) {
		key = (int)keysym->sym;
	}
	else {
		switch (keysym->sym) {
			case SDLK_PAGEUP:      key = K_PGUP;       break;
			case SDLK_KP_9:        key = K_KP_PGUP;    break;
			case SDLK_PAGEDOWN:    key = K_PGDN;       break;
			case SDLK_KP_3:        key = K_KP_PGDN;    break;
			case SDLK_KP_7:        key = K_KP_HOME;    break;
			case SDLK_HOME:        key = K_HOME;       break;
			case SDLK_KP_1:        key = K_KP_END;     break;
			case SDLK_END:         key = K_END;        break;
			case SDLK_KP_4:        key = K_KP_LEFTARROW; break;
			case SDLK_LEFT:        key = K_LEFTARROW;  break;
			case SDLK_KP_6:        key = K_KP_RIGHTARROW; break;
			case SDLK_RIGHT:       key = K_RIGHTARROW; break;
			case SDLK_KP_2:        key = K_KP_DOWNARROW; break;
			case SDLK_DOWN:        key = K_DOWNARROW;  break;
			case SDLK_KP_8:        key = K_KP_UPARROW; break;
			case SDLK_UP:          key = K_UPARROW;    break;
			case SDLK_ESCAPE:      key = K_ESCAPE;     break;
			case SDLK_KP_ENTER:    key = K_KP_ENTER;   break;
			case SDLK_RETURN:      key = K_ENTER;      break;
			case SDLK_TAB:         key = K_TAB;        break;
			case SDLK_F1:          key = K_F1;         break;
			case SDLK_F2:          key = K_F2;         break;
			case SDLK_F3:          key = K_F3;         break;
			case SDLK_F4:          key = K_F4;         break;
			case SDLK_F5:          key = K_F5;         break;
			case SDLK_F6:          key = K_F6;         break;
			case SDLK_F7:          key = K_F7;         break;
			case SDLK_F8:          key = K_F8;         break;
			case SDLK_F9:          key = K_F9;         break;
			case SDLK_F10:         key = K_F10;        break;
			case SDLK_F11:         key = K_F11;        break;
			case SDLK_F12:         key = K_F12;        break;
			case SDLK_F13:         key = K_F13;        break;
			case SDLK_F14:         key = K_F14;        break;
			case SDLK_F15:         key = K_F15;        break;

			case SDLK_BACKSPACE:   key = K_BACKSPACE;  break;
			case SDLK_KP_PERIOD:   key = K_KP_DEL;     break;
			case SDLK_DELETE:      key = K_DEL;        break;
			case SDLK_PAUSE:       key = K_PAUSE;      break;

			case SDLK_LSHIFT:
			case SDLK_RSHIFT:      key = K_SHIFT;      break;

			case SDLK_LCTRL:
			case SDLK_RCTRL:       key = K_CTRL;       break;

			case SDLK_RGUI:
			case SDLK_LGUI:        key = K_COMMAND;    break;

			case SDLK_RALT:
			case SDLK_LALT:        key = K_ALT;        break;

			case SDLK_KP_5:        key = K_KP_5;       break;
			case SDLK_INSERT:      key = K_INS;        break;
			case SDLK_KP_0:        key = K_KP_INS;     break;
			case SDLK_KP_MULTIPLY: key = K_KP_STAR;    break;
			case SDLK_KP_PLUS:     key = K_KP_PLUS;    break;
			case SDLK_KP_MINUS:    key = K_KP_MINUS;   break;
			case SDLK_KP_DIVIDE:   key = K_KP_SLASH;   break;

			case SDLK_POWER:       key = K_POWER;      break;
			case SDLK_NUMLOCKCLEAR: key = K_KP_NUMLOCK; break;
			case SDLK_CAPSLOCK:    key = K_CAPSLOCK;   break;

			default:
				break;
		}
	}

	if (in_keyboardDebug->integer) {
		IN_PrintKey(keysym, key, qtrue);
	}

	return key;
}

/*
===============
IN_Commands
===============
*/
static void IN_Commands( void )
{
	int b;

	if (!in_joystick->integer || !stick) {
		return;
	}

	for (b = 0; b < SDL_JoystickNumButtons(stick); b++) {
		int key = K_JOY1 + b;

		if (Key_IsDown(key) != SDL_JoystickGetButton(stick, b)) {
			Sys_QueEvent(Sys_Milliseconds(), SE_KEY, key,
				SDL_JoystickGetButton(stick, b), 0, NULL);
		}
	}
}

/*
===============
IN_QueueEvents
===============
*/
static void IN_QueueEvents( void )
{
	SDL_Event event;
	int time;

	time = Sys_Milliseconds();

	while (SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_QUIT:
				Sys_Quit();
				break;

			case SDL_KEYDOWN:
			case SDL_KEYUP: {
				int key = IN_TranslateSDLToQ3Key(&event.key.keysym);

				if (key) {
					Sys_QueEvent(time, SE_KEY, key, event.key.state == SDL_PRESSED, 0, NULL);
				}

				// Field_KeyDownEvent has no K_BACKSPACE case; backspace is handled by
				// Field_CharEvent as ctrl-h (8). Win32 gets that from WM_CHAR (VK_BACK
				// arrives as 8) and X11 from XLookupString. SDL sends no TEXTINPUT for
				// BackSpace, so synthesise the char on keydown.
				if (event.key.state == SDL_PRESSED && event.key.keysym.sym == SDLK_BACKSPACE) {
					Sys_QueEvent(time, SE_CHAR, 8, 0, 0, NULL);
				}
				break;
			}

			// Console and chat entry need SE_CHAR, not SE_KEY: Field_KeyDownEvent only
			// handles edit keys, printable text arrives through Field_CharEvent.
			// SDL_TEXTINPUT is the WM_CHAR equivalent and resolves shift/dead keys.
			case SDL_TEXTINPUT: {
				const char *t = event.text.text;
				int i;

				for (i = 0; t[i]; i++) {
					int ch = (unsigned char)t[i];
					if (ch >= 32 && ch < 127 && ch != '`' && ch != '~') {
						Sys_QueEvent(time, SE_CHAR, ch, 0, 0, NULL);
					}
				}
				break;
			}

			case SDL_MOUSEMOTION:
				if (mouseActive) {
					Sys_QueEvent(time, SE_MOUSE, event.motion.xrel, event.motion.yrel, 0, NULL);
				}
				break;

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP: {
				int key;

				switch (event.button.button) {
					case SDL_BUTTON_LEFT:   key = K_MOUSE1; break;
					case SDL_BUTTON_MIDDLE: key = K_MOUSE3; break;
					case SDL_BUTTON_RIGHT:  key = K_MOUSE2; break;
					case SDL_BUTTON_X1:     key = K_MOUSE4; break;
					case SDL_BUTTON_X2:     key = K_MOUSE5; break;
					default: continue;
				}

				Sys_QueEvent(time, SE_KEY, key, event.button.state == SDL_PRESSED, 0, NULL);
				break;
			}

			case SDL_MOUSEWHEEL:
				Sys_QueEvent(time, SE_KEY, event.wheel.y > 0 ? K_MWHEELUP : K_MWHEELDOWN,
					event.wheel.y > 0, 0, NULL);
				break;

			case SDL_JOYAXISMOTION: {
				int value = event.jaxis.value;
				int threshold = in_joystickThreshold->value;

				if (value < -threshold) {
					value = -255;
				} else if (value > threshold) {
					value = 255;
				} else {
					value = 0;
				}

				Sys_QueEvent(time, SE_JOYSTICK_AXIS, event.jaxis.axis, value, 0, NULL);
				break;
			}

			default:
				break;
		}
	}
}

/*
===============
IN_Frame
===============
*/
static cvar_t *in_selftest;
static void IN_SelfTest( void );

void IN_Frame( void )
{
	IN_Commands();
	IN_QueueEvents();
	IN_SelfTest();
}

/*
===============
IN_SelfTest

Temporary: push SDL events through the real pump and report what the
engine event queue receives. Verifies SE_CHAR reaches CL_CharEvent.
===============
*/
static void IN_SelfTest( void )
{
	SDL_Event ev;
	int n, got_char = 0, got_key = 0;

	if (!in_selftest || !in_selftest->integer) {
		return;
	}
	in_selftest->integer = 0;

	SDL_memset(&ev, 0, sizeof(ev));
	ev.text.type = SDL_TEXTINPUT;
	SDL_strlcpy(ev.text.text, "ab", sizeof(ev.text.text));
	SDL_PushEvent(&ev);

	SDL_memset(&ev, 0, sizeof(ev));
	ev.key.type = SDL_KEYDOWN;
	ev.key.keysym.sym = SDLK_BACKSPACE;
	ev.key.keysym.scancode = SDL_SCANCODE_BACKSPACE;
	ev.key.state = SDL_PRESSED;
	SDL_PushEvent(&ev);

	IN_QueueEvents();

	for (n = 0; n < 16; n++) {
		sysEvent_t e = Sys_GetEvent();
		if (!e.evType) {
			break;
		}
		if (e.evType == SE_CHAR) {
			got_char++;
			Com_Printf("SELFTEST: SE_CHAR %i\n", e.evValue);
			// drive the real console field the way CL_CharEvent does
			Field_CharEvent(&g_consoleField, e.evValue);
		} else if (e.evType == SE_KEY) {
			got_key++;
			Com_Printf("SELFTEST: SE_KEY %i down=%i\n", e.evValue, e.evValue2);
		}
	}
	Com_Printf("SELFTEST: chars=%i keys=%i\n", got_char, got_key);
	Com_Printf("SELFTEST: console field \"%s\" (want \"ab\" -> backspace -> \"a\")\n",
		g_consoleField.buffer);
}

/*
===============
Sys_SendKeyEvents
===============
*/
void Sys_SendKeyEvents( void )
{
	SDL_PumpEvents();
}

/*
===============
IN_Init
===============
*/
void IN_Init( void )
{
	in_mouse = Cvar_Get("in_mouse", "1", CVAR_ARCHIVE);
	in_nograb = Cvar_Get("in_nograb", "0", CVAR_ARCHIVE);
	in_joystick = Cvar_Get("in_joystick", "0", CVAR_ARCHIVE | CVAR_LATCH);
	in_joystickThreshold = Cvar_Get("in_joystickThreshold", "10", CVAR_ARCHIVE);
	in_joystickNo = Cvar_Get("in_joystickNo", "0", CVAR_ARCHIVE);
	in_keyboardDebug = Cvar_Get("in_keyboardDebug", "0", CVAR_ARCHIVE);
	in_selftest = Cvar_Get("in_selftest", "0", 0);

	if (in_joystick->integer && SDL_NumJoysticks() > in_joystickNo->integer) {
		stick = SDL_JoystickOpen(in_joystickNo->integer);
		if (stick) {
			Com_Printf("Opened Joystick %s: %d axes, %d buttons\n",
				SDL_JoystickName(stick), SDL_JoystickNumAxes(stick),
				SDL_JoystickNumButtons(stick));
		}
	}

	mouseActive = in_mouse->integer && !in_nograb->integer;
	SDL_SetRelativeMouseMode(mouseActive ? SDL_TRUE : SDL_FALSE);
	SDL_ShowCursor(mouseActive ? SDL_FALSE : SDL_TRUE);
	SDL_StartTextInput();
}

/*
===============
IN_Shutdown
===============
*/
void IN_Shutdown( void )
{
	if (stick) {
		SDL_JoystickClose(stick);
		stick = NULL;
	}
}

/*
===============
Sys_In_Restart_f
===============
*/
void Sys_In_Restart_f( void )
{
	IN_Shutdown();
	IN_Init();
}
