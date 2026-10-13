# AGENTS.md

Working notes for anyone continuing this modernisation. Read `PLAN.md` first.

## What this repo is

GPL Quake III Arena 1.32, rebuilt with CMake on top of an SDL2 platform layer
(`code/sdl2/`). The engine is `client`, `qcommon`, `server`, `botlib`, `renderer`,
`jpeg-6`, `sdl2`. **`code/game`, `code/cgame`, `code/q3_ui` are NOT compiled into the
executable** — they ship as `.qvm` bytecode in `baseq3/pak2.pk3` and run through
`code/qcommon/vm.c` + `vm_interpreted.c`. Do not add them to the link.

## Day-to-day

```
./build-native.sh          # cmake into build-native/, DEBUG=1 for Debug, CLEAN=1 to wipe
./run-native.sh            # windowed; WIDTH/HEIGHT/FULLSCREEN/LOGFILE/DEVELOPER env, args forwarded
./test.sh                  # display pick, input pump, multiplayer loopback (SKIP_BUILD=1 to skip build)
./watch-build-native.sh    # entr rebuild loop
```

`baseq3/pak0.pk3` (479MB) is gitignored and must be on disk.

**Window placement.** `SDL_WINDOWPOS_CENTERED` centres on display 0, which on a
multi-monitor box is often not the screen you are looking at. `SDL_WINDOW_DISPLAY` takes
a display index **or** a name substring (SDL reports names from the display product
string, e.g. "Color LCD", "Built-in Retina Display", "LG UltraFine"). Unset, the engine
picks the built-in panel so the window lands on the laptop screen. The log prints every
display name, then `Creating window on display N ("name") of M` and a `WINDOW:` line with
the final position. `SDL_WINDOW_XY=<x>,<y>` pins a position and overrides centring.

## Verification — the part that matters

You cannot claim anything works without a check. These are the checks that exist:

**Drive the game headlessly from a pty.** The game reads `+`-prefixed commands from the
command line (`Com_AddStartupCommands`) and reads stdin as a console when attached to a
pty. Send commands by writing to the pty, then read the log:

```python
import os, pty, time, select
args = ["./build/Quake3", "+set", "r_fullscreen", "0", "+set", "r_mode", "-1",
        "+set", "r_customwidth", "1280", "+set", "r_customheight", "720",
        "+devmap", "q3dm17", "+addbot", "sarge", "1", "free", "0"]
pid, fd = pty.fork()
if pid == 0:
    os.execv(args[0], args)
def drain(seconds):
    out = b""; t0 = time.time()
    while time.time() - t0 < seconds:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try: out += os.read(fd, 4096)
            except OSError: return out
    return out
out = drain(10)
os.write(fd, b"screenshot\n"); out += drain(2)
os.write(fd, b"quit\n")
# then waitpid with WNOHANG and assert exit status 0
```

`quit` must exit `0` with zero `Sys_Error` lines. `status` lists connected bots.
`screenshot` writes `baseq3/screenshots/shotNNNN.tga`.

**Assert on pixels, not on vibes.** `tools/tga-stats.py` reports size, mean RGB,
pure-white / pure-black percentage, and distinct colour count for a TGA. A healthy
`q3dm17` frame at 1280x720 is roughly: mean RGB ~30, pure white ~0%, ~27000 distinct
colours. A broken texture path shows up as mean RGB >200 and pure white >40%.

```
python3 tools/tga-stats.py "$(ls -t ~/Library/Application\ Support/Quake3/baseq3/screenshots/*.tga | head -1)"
sips -s format png "$shot" --out /tmp/shot.png     # then look at it
```

**`+addbot sarge 1 free 0`**, not `+bot add sarge`. The `bot` command is not registered
in the shipped game VM.

**`in_selftest 1`** pushes synthetic SDL events through the real input pump and prints
the engine events received (`SE_CHAR`, `SE_KEY`), then drives the real `g_consoleField`
through `Field_CharEvent` and prints the resulting text. It is the only way to verify the
text input path without a keyboard.

**Multiplayer loopback** (`tools/mptest.py`): dedicated server on UDP 27960, client
`+connect localhost:27960`. Asserts `ClientConnect: 1` and `ClientBegin: 1` in the server
log, `lsof` shows the port listening, the client exits 0 with no `Sys_Error`, and the
screenshot is not white. Verified working — UDP multiplayer is functional.

**The wasm build runs headlessly under node as a dedicated server.** This is the way to study
the game loop with no renderer, no WebGL, no SwiftShader and no browser main loop:

```
cd build-wasm && node Quake3.js +set dedicated 1 +set fs_basepath . +set fs_homepath . \
    +set net_port 27960 +set sv_pure 0 +map q3dm17 +addbot sarge 1 free 0
```

It works because `sdl2_main.c` scans argv for `dedicated` and drops `SDL_INIT_VIDEO |
SDL_INIT_AUDIO` from `SDL_Init` — node has no canvas, so video init would abort. Verified
under node: `InitGame`, BotLib init, `loaded maps/q3dm17.aas`, `AAS initialized`,
`ClientBegin: 0`, and the loop advancing (`MSEC frame 1470 msec 17 frameTime 24660`).
Node's assets come from `assets-wasm/baseq3`, which `tools/build-asset-dir.sh` fills by
unzipping `pak2.pk3` — the same `vm/qagame.qvm` native loads.

**Node reproduces the browser-only game trap gap.** Node prints `unhandled game trap 425`
and `426` continuously; native, run with the identical dedicated command line, prints zero.
`sv_gametraphist N` prints a histogram of game system trap numbers every N calls from
`SV_GameSystemCalls`, which is what measured this. Native dedicated (150000 calls):
`205:99 207:101376 304:196 422:192 423:96` — `207` is `BOTLIB_UpdateEntity` and 101376 =
99 frames x 1024 entities, i.e. `AI_StartFrame`'s entity loop runs every frame. Node
(84000 calls): `205:252 207:0 304:502 423:495 425:495 426:249` — `AI_StartFrame` runs 252
times, `AAS_Initialized` reports 1 every time (probed: `AASINIT ret 1`, 502/502), and the
entity loop never runs at all. Node also never calls `EA_GetInput` (422) and calls
`EA_RESPAWN` (408) 248 times, so its bot dies and respawns without ever receiving input.
Same qvm, same interpreted VM (`vm_game 1` on both), same map, same AAS.

**GL questions get answered with a standalone SDL probe**, not with guesses. See
`/tmp/probe/` pattern: a tiny CMake project linking the same SDL2 + OpenGL, printing
`glGetString(GL_VERSION)`, extension strings, and `glGetError()` after each call.

## Traps (each one cost hours)

- **`-std=gnu89` is required.** 1.32 relies on pre-C99 GCC semantics for `inline`. In
  C99+ mode you get link errors for missing external definitions.
- **S3TC is a lie on Apple GL 2.1.** `Upload32` passes raw RGBA bytes with
  `GL_RGB4_S3TC` as the internal format. Apple's driver accepts the extension string and
  rejects the upload, producing an empty texture — every no-alpha surface renders white.
  `r_ext_compressed_textures` is a **dead cvar in 1.32** (declared, never read); the fix
  is to make `GLimp_InitExtensions` honour it. Do not re-enable S3TC without a pixel check.
- **VM API version.** The shipped `.qvm` files are API version 4; the engine expects 6.
  `UI_OLD_API_VERSION` handles 4. The x86 JIT does not work here, so VM cvars are forced
  to bytecode (`vm_game`/`vm_cgame`/`vm_ui` = `1`) and `vm.c` is built with `oDLL_ONLY`.
- **64-bit truncation.** `(int)pointer` casts are UB here. Known fixed sites:
  `Com_InitHunkMemory`, `files.c` `qsort` size, `Q_rsqrt` (must use 32-bit `int` and the
  32-bit magic number), `SWAP_DRAW_SURF` in `tr_main.c`.
- **`tess.xyz not 16 byte aligned`** is a false positive on 64-bit — the check casts the
  pointer to `int`. Ignore it.
- **MD4** in `qcommon/md4.c` must match RFC 1320. AAS files are validated by checksum;
  a broken MD4 means botlib refuses to init and you get "aas file is out of date" plus
  no in-game sound.
- **Console / chat text entry uses `SE_CHAR`, not `SE_KEY`.** `Field_KeyDownEvent` only
  handles edit keys; printable characters arrive via `Field_CharEvent` from `SE_CHAR`,
  which SDL delivers as `SDL_TEXTINPUT`.
- **Backspace is `SE_CHAR` 8, not `K_BACKSPACE`.** `Field_KeyDownEvent` has no
  `K_BACKSPACE` case; `Field_CharEvent` handles backspace as ctrl-h (8). Win32 gets 8
  from `WM_CHAR` (`VK_BACK`), X11 from `XLookupString`. SDL sends no `SDL_TEXTINPUT` for
  BackSpace, so `sdl2_input.c` synthesises `SE_CHAR` 8 on keydown. `K_BACKSPACE` is
  never handled anywhere in the engine — do not "fix" it by mapping the keynum.
- **A pty-spawned Quake3 does not die on SIGTERM until you drain the pty.** The game
  writes console output to the pty slave; when the master buffer fills it blocks in
  `write()` and never reaches the signal. Drain the pty after `kill(pid, SIGTERM)`, then
  `waitpid`. `tools/mptest.py` does this. A plain background process (stdout to a file)
  terminates on SIGTERM normally. The same buffer-full condition makes a pty harness read
  as "the engine stopped after `Server: q3dm17`" — it did not, the harness just stopped
  seeing it. A long-running native process is fine; check `$?` on a real exit.
- **`r_pixelprobe N` is the pixel check that works in the browser.** It reads the centre
  48x48 of the drawing buffer in `GLimp_EndFrame` and prints mean RGB, distinct colours,
  white/black percentages. Nothing on the page can reach the WebGL context (Emscripten
  creates it inside the wasm module), so `HTMLCanvasElement.prototype.getContext`
  patching, `Module.__gl`, and CDP `addScriptToEvaluateOnNewDocument` are dead ends — the
  minifier drops dead stores to non-live globals. `tools/browser-test.py` reports the
  PIXELPROBE lines. Black framebuffer = `mean 0,0,0 distinct 1 black 100%`.
- **A `SyntaxError` in `shell.html`'s inline script kills GL init.** `Module` (canvas,
  arguments) is never defined, so `GLimp_Init` never runs and the console stops at
  `Window resized to 800x600` with the canvas at its default 800x600. Check the shell
  script with `node --check` on the extracted `<script>` before blaming the engine.
- **`ERR_DROP` kills the Emscripten main loop.** `Com_Frame` does `setjmp(abortframe)`;
  a longjmp out of the rAF callback skips re-scheduling, so one `ERR_DROP` ends the run
  (PIXELPROBE stops at frame 0). Count PIXELPROBE frames after any `ERR_DROP`.
- **The shipped 1.32 `.qvm` files issue traps this source does not implement.** In the
  browser, `qagame.qvm` calls traps 425 and 426 during bot init, right after `loaded sarge
  from bots/sarge_t.c`. `g_public.h`'s EA series ends at `BOTLIB_EA_RESET_INPUT` (423) and
  `g_syscalls.asm` ends at `trap_EA_ResetInput` (-424), so 425-427 exist in neither. The
  qvm's trap numbering is offset from this source. `SV_GameSystemCalls`' `default:` raises
  `ERR_DROP` -> `Bad game system trap: 426` -> `----- Server Shutdown -----`, so the client
  never joins and the framebuffer stays black. Native never executes these traps. The
  shipped qvm is stripped (`VM_ValueToSymbol` -> `NO SYMBOLS`), so name the trap by
  numbering, not by symbol.

## Rules for this repo

- No success claim without a check. A window opening is not verification.
- Leave a check behind for non-trivial logic. Prefer a pixel assertion, a log line,
  or a cvar-gated self-test over reading code.
- Add compiler warning flags to suppress noise (`-Wno-...`) rather than fixing the
  source, when the source is original 1.32 and the warning is harmless.
- Preserve unrelated changes. No `git stash`, no broad `git restore .`, no commits
  without being asked.
