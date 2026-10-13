# PLAN

Ref: `~/Projects/Home/Quake` (Quake 1) — same shape, same process.

## Goal

Modernise the GPL Quake III Arena source into a CMake project that builds and runs
natively on macOS / Linux with SDL2, then builds to WASM with a WebSocket multiplayer
gateway.

## How we'll know it's done

- `./build-native.sh` produces `build-native/Quake3` with no warnings we've not accepted
- `./run-native.sh` opens a window, renders `baseq3` menus, plays a local `q3test`/`q3dm17`
  match with sound, mouse, keyboard
- `./build-wasm.sh` produces a browser build that runs the same
- A browser client drops straight into `q3dm17` from a URL and `r_pixelprobe` reports
  thousands of distinct colours (not a uniform clear colour) with `ClientBegin: 0` in the
  server log
- `./test.sh` passes, and covers the WebSocket net layer
- A browser client can join a native/WASM listen server over WebSocket

## Ordered checklist

Native:

- [x] Scope the CMake target to the engine only (client, qcommon, server, botlib, renderer,
      jpeg-6, sdl2 platform) — `common/`, `game/`, `cgame/`, `q3_ui/`, `bspc/` are separate
      modules and must not be linked into the engine
- [x] Link SDL2 (FetchContent) into the executable, expose SDL2 + khronos headers
- [x] Add include guards to the `code/botlib` headers that are included twice
- [x] Engine compiles and links
- [x] `sdl2_main.c`: real `main`, `Sys_Milliseconds`, `Sys_Error`, `Sys_Quit`, filesystem
      search paths, `Sys_Dialog`
- [x] `sdl2_glimp.c`: SDL2 GL context, `GLimp_Init` / `EndFrame` / `Shutdown`, GL extension
      pointers (`qgl*`)
- [x] `sdl2_input.c`: keyboard, mouse (relative mode), `Sys_SendKeyEvents`
- [x] `sdl2_snddma.c`: SDL2 audio capture callback, `SNDDMA_Init` / `GetDMAPos` / `Submit`
- [x] `sdl2_net.c`: UDP send / receive, `NET_StringToAdr`, broadcasts, `NET_Restart_f`
- [x] `build-native.sh`, `run-native.sh` (build dir is `build/`, run is `./build/Quake3`)
- [x] Verify: window opens, renders, sound plays, a local match runs
- [ ] **Audio distortion.** Native SDL honours the requested rate exactly (`SDL audio: format
      16-bit, 22050 Hz, 2 channels, 512 samples`, `22050 speed`), so this is not a
      sample-rate mismatch. Check: count `SNDDMA_Submit` calls per second against
      `shm.speed` (`shm.samples / shm.speed` seconds per submission) and compare the
      audible tempo/pitch of a known `.wav` loop. A callback rate above `shm.samples /
      shm.speed` means the DMA position advances faster than the mixer fills it.
- [x] Textures render (S3TC upload path fixed — see AGENTS.md)
- [x] Console and chat text entry (`SE_CHAR` from `SDL_TEXTINPUT`), verified with `in_selftest 1`
- [x] Regression check for the platform layer (headless / dedicated run) — `./test.sh`
- [x] UDP multiplayer: dedicated server + client `+connect localhost:27960` — `tools/mptest.py`

WASM:

- [x] `build-wasm.sh` produces `Quake3.js/.html/.wasm/.data`; `tools/browser-test.py` drives
      headless Chrome (SwiftShader) over CDP, captures console + screenshots.
      Verified: engine inits, map loads, `AAS initialized`, 120+ frames, `glGetError()==0`.
- [ ] **Every rendering check so far is software GL.** `tools/browser-test.py` launches
      `--headless=new --disable-gpu --enable-unsafe-swiftshader`, so SwiftShader is always in
      the loop and `Page.captureScreenshot` captures a software-composited frame. Nothing has
      been validated against a real GPU context. Cross-check the same URL on
      `chromium.dev.initialed85.cc` (headless Chromium + CDP, see `SERVICES.md`) — a real
      Chromium with a GPU process — to rule SwiftShader out. A frame that renders there and
      not here is a GL-emulation bug, not a game-logic bug.
- [x] Emscripten main loop: `main` is `while(1) Com_Frame()`; needs `emscripten_set_main_loop`
- [x] Dedicated / headless: skip `SDL_INIT_VIDEO|AUDIO` when `dedicated` is in argv.
      `SO_BROADCAST` is advisory on Emscripten (browser UDP broadcast is impossible).
- [x] No-op shims for `glArrayElement` / `glCallList` (never live in 1.32), `qglDrawBuffer`,
      and `qglMultiTexCoord2fARB` routed through the client active texture.
      `r_primitives 3` forced on Emscripten.
- [x] WebGL 1 has no sized internal formats: `GL_RGB8`/`GL_RGB5`/`GL_RGBA4`/S3TC are
      `INVALID_VALUE` in `texImage2D`, so every texture upload failed. `Upload32` uses
      `GL_RGBA` on Emscripten. Remaining WebGL noise: `GL_CLAMP` and
      `GL_TEXTURE_BORDER_COLOR` are `INVALID_ENUM` in `texParameter` (~220 warnings).
- [x] `glConfig.vidWidth/Height` follow the real window size, not the requested `r_mode`.
      SDL clamps the window to the display bounds (canvas 800x600), so `cg.refdef` and the
      framebuffer agree.
- [x] Pixel assertion inside the engine: `r_pixelprobe N` reads the centre 48x48 of the
      drawing buffer in `GLimp_EndFrame` (before the swap) and prints mean RGB, distinct
      colours, white/black percentages. Emscripten creates the WebGL context inside the
      wasm module, so no JS-side handle exists: `HTMLCanvasElement.prototype.getContext`
      patching, `Module.__gl`, `addScriptToEvaluateOnNewDocument` all failed (the minifier
      drops dead stores to non-live globals). `tools/browser-test.py` now reports the
      PIXELPROBE lines; a black canvas and a real frame are distinguishable signatures.
- [x] Font flood suppressed: `RE_RegisterFont` printed `FreeType code not available` once
      per registration (~40 per boot). The shipped pk3s contain no `fonts/fontImage_N.dat`
      and no `.ttf`, so the `.dat` path can never succeed; the warning is now one-shot.
      Generating the font atlases is a separate milestone.
- [ ] **Fonts for the browser: build FreeType for wasm.** `https://github.com/Ciantic/freetype-wasm`
      is a FreeType wasm build to evaluate. `tr_font.c` has the `.dat` atlas path and a
      FreeType path, and the FreeType path needs FreeType available to Emscripten. Preferred
      shape: build FreeType once as a wasm static lib and feed `tr_font.c`'s existing FreeType
      branch. Assert with `r_pixelprobe` plus a screenshot of a text-heavy screen — "the menu
      is visible" is not proof the glyphs are legible.
- [x] WebGL enum fixes: `GL_CLAMP` -> `GL_CLAMP_TO_EDGE` and `GL_TEXTURE_BORDER_COLOR`
      dropped in `qglTexParameterfv` on Emscripten; `texParameter: invalid parameter` count
      is now 0. `r_mode -1` with `r_customwidth/height` applies (`setting mode -1: 908 681`).
- [x] Engine side of the client is fully healthy in the browser: loopback connect, gamestate,
      `CL_InitCGame: 0.34 seconds`, `CA_ACTIVE`, snapshots parse (`SNAPNUM 3 serverTime 900
      parsed 47` -> `SNAPNUM 6 serverTime 950 parsed 188`), the canvas composites, and
      screenshots capture a real frame.
- [x] URL query arguments become engine command-line arguments, so the browser can drop
      straight into a map with no menu:
      `http://127.0.0.1:8081/Quake3.html?+set net_noudp 1&+set sv_pure 0&+devmap q3dm17`
      (send `+` as `%2B`). Verified: `Server: q3dm17`, `CGINIT ... clientNum 0`, snapshots
      parsing and advancing, **zero unhandled traps with no bot**.
- [ ] **WASM renders the loading screen, not the 3D view.** The cgame VM never reaches
      `CG_R_RENDERSCENE`. Measured with `cl_cgsyscalls N` (a cvar-gated histogram in
      `CL_CgameSystemCalls`, plus a `SNAPNUM` line in `CL_GetCurrentSnapshotNumber`):
      `cvarupd 1541, setcolor 463, stretchpic 3292, clearscene 2, snapnum 2,
      renderscene 0, getsnap 0`. So `CG_DrawActiveFrame` runs (67 cvar updates per frame),
      `CG_ProcessSnapshots` runs (`CG_GETCURRENTSNAPSHOTNUMBER` fires), `CG_DrawInformation`
      draws (`setcolor` + `stretchpic`), and `CG_R_RENDERSCENE` (44) is never called.
      Latest no-bot run: `46:18713 45:2499 39:208 40:99 51:98 57:268 3:67`, with 44 and 52
      absent, snapshots advancing (`SNAPNUM 92 serverTime 2450` -> `SNAPNUM 99 serverTime
      2550`), zero traps. `cg.snap` stays NULL so `CG_DrawActive` returns before the view.
      This is downstream of the game-VM defect below: the client is never begun server-side,
      so it never becomes a live player. Fixing it from the engine side is not possible —
      `CG_ReadNextSnapshot` lives in the shipped `cgame.qvm`, and the GPL release ships no
      toolchain to rebuild it.
- [x] Main loop clock: `com_maxfps` defaults to 0 on Emscripten so `Com_Frame` does not spin
      waiting for wall-clock; rAF paces it. Measured 3660 frames with PIXELPROBE printing.
- [ ] **Blocker: the game VM takes a different path under wasm, and the client never becomes
      a live player.** Traps 425/426 are **bot-only** — a no-bot `+devmap q3dm17` logs **zero**
      unhandled traps — so they are not the rendering blocker. Returning 0 from them keeps the
      server up (an `ERR_DROP` longjmp out of the rAF callback kills the Emscripten main loop).
      The server *does* activate the browser client: with `developer 1` the log shows
      `Going from CS_PRIMED to CS_ACTIVE for UnnamedPlayer`, which is `SV_ClientEnterWorld`.
      The 1.32 handshake here is the client's first usercmd, not a `begin` clientcommand — there
      is no `"begin"` string and no `SV_BeginClientSession` in this source. `SV_GameClientBegin`
      therefore runs, yet `ClientBegin: 0` never appears.
      **Smallest reproducible symptom: game-VM `trap_Print` strings longer than ~36 chars never
      reach the engine under wasm.** The probe in `SV_GameSystemCalls`' `G_PRINT` case prints
      pointer, mask, offset, length and the string. Native: 19 calls, lengths up to 324,
      including `len 15 [ClientBegin: 0` and `len 324 [InitGame: \g_maxGameClients\0\capturelimit\0\sv_`.
      Wasm: 8 calls, **max length 36** — `InitGame:` and `ClientBegin:` absent. Pointers resolve
      correctly (`ptr 2094984 mask 2097151 off 2094984 len 24 [0 teams with 0 entities]`), so
      this is not general pointer corruption: the long-print syscall never fires.
      Node (dedicated — no renderer, no WebGL, no SwiftShader) reproduces the same class of
      divergence, so GL is not implicated: the bot dies every frame — `EA_RESPAWN:248`,
      `EA_GET_INPUT:0`, `EA_VIEW:0`, `EA_SELECT_WEAPON:0`, `BOTLIB_UpdateEntity:0` vs native
      `101376`. `sv_gravity 0` changes nothing, so it is not falling through the world.
      **Next probe:** instrument the print site *inside* the game module (`G_Print` /
      `Com_Printf` in `code/game/g_main.c`, compiled into `qagame.qvm` by lcc) and log the
      length handed to `trap_Print`. That separates "the syscall never fires" (module side — a
      wasm `va_list`/`Com_sprintf` problem in the compiled module) from "it fires with a bad
      pointer" (engine/interpreter side). Needs `qagame.qvm` built from `code/game` with lcc +
      q3asm, which the GPL release does not ship — so the practical route is to log every
      `G_PRINT` call with its length on the engine side and check whether wasm ever reports a
      call longer than 36 chars.
- [x] **The wasm build runs headlessly under node as a dedicated server** — game loop, server
      and game VM with no renderer, no WebGL and no SwiftShader. `cd build-wasm && node
      Quake3.js +set dedicated 1 +set fs_basepath . +set fs_homepath . +set net_port 27960
      +set sv_pure 0 +map q3dm17 +addbot sarge 1 free 0`. `sdl2_main.c` drops
      `SDL_INIT_VIDEO | SDL_INIT_AUDIO` when it sees `dedicated` in argv, which is why node
      runs at all. `sv_gametraphist N` prints a trap histogram from `SV_GameSystemCalls`.
      Native dedicated, same command line: `205:99 207:101376 304:196 422:192 423:96` — 207 is
      `BOTLIB_UpdateEntity` and 101376 = 99 frames x 1024 entities, so `AI_StartFrame`'s entity
      loop runs every frame. Node: `205:252 207:0 304:502 423:495 425:495 426:249` —
      `AI_StartFrame` runs 252 times, `AAS_Initialized` returns 1 all 502 times, and the entity
      loop never runs at all; the bot never calls `EA_GetInput` and calls `EA_RESPAWN` 248
      times. Same qvm (node's `qagame.qvm` comes from `pak2.pk3` via `assets-wasm`), same
      interpreted VM (`vm_game 1` both). Node reproduces the browser's 425/426; native prints
      zero. So the wasm game VM takes a different path through identical bytecode, and that is
      the thing to chase. **The asset set is no longer the variable:**
      `tools/build-asset-dir.sh` now seeds `assets-wasm/baseq3` from `pak8.pk3` first and fills
      the remainder from `pak2.pk3` with `unzip -n`, so wasm runs the same Oct-2002 VMs as
      native — `gamedate: Sep 30 2002` now appears in the wasm log. Behaviour is unchanged,
      which rules the pak2/pak8 VM version gap out.
- [x] **The browser's WASM thread is not spinning.** Measured with `tools/cpu-probe.py`
      (headless Chrome, sampling `ps` CPU time): the renderer process uses 0.10-0.12 CPU
      seconds per second while the GPU process under SwiftShader uses ~3.5. The WASM main loop
      is idle between rAF callbacks, and Emscripten never aborts with "Blocking on the main
      thread", so `ALLOW_BLOCKING_ON_MAIN_THREAD` is not the issue.
- [ ] `ERR_DROP` kills the Emscripten main loop: `Com_Frame` does `setjmp(abortframe)` and a
      longjmp out of the rAF callback skips re-scheduling, so one `ERR_DROP` ends the run.
      Verify by counting PIXELPROBE frames after any `ERR_DROP`.
- [ ] Asset delivery: ship the pak central directories (~230KB) + `pak2` (7.1MB, the VMs),
      prefetch a per-map dep manifest (`fs_logDeps 1` + `tools/mapdeps.py`), cache in
      MEMFS/IDBFS. `FS_Read` is synchronous, so prefetch the manifest before the map loads
      — do not fetch per file on demand.
- [ ] `net_ws.c` + `websockets/` gateway, mirroring the Quake 1 project
- [ ] `ws-server` gateway + `test.sh` covering broadcast fan-out
- [ ] Browser client joins a listen server
- [ ] Renderer: Q3 1.32 is fixed-function multitexture — 21 call sites in `tr_shade.c` /
      `tr_backend.c` (`qglTexEnv*`, `qglActiveTextureARB`, `qglLockArraysEXT`,
      `qglVertexPointer`, `qglColor4ubv`, `qglTexCoord2fv`). WebGL has none of these. Needs
      a shader backend; ioq3's GL3 renderer is the GPL reference. **This is the timeline.**
- [ ] Decide on cinematics: **do not ship `.roq`** (11 files, 117MB uncompressed). No stub
      needed — `CIN_PlayCinematic` returns `-1` when the file is missing and every
      handle-taking `CIN_*` guards `handle < 0`. Cinematics stream frame-by-frame
      (`cl_cin.c:1175`), so there is no up-front load. Cost of omitting them: menus lose
      their background video (map-list previews, team logos) and the campaign's interlevel
      video. Cosmetic. If wanted later, stream them with the same manifest approach.

Later / stretch:

- [ ] Linux Docker build (`build-docker.sh`)
- [ ] `q3map` as its own CMake target
- [ ] `cgame` / `game` / `q3_ui` as native shared modules (VM bytecode already ships in pak2)

## Notes

- Game / cgame / UI ship as `.qvm` bytecode in `baseq3/pak2.pk3`; the engine runs them
  through `vm_interpreted.c`, so we do not need to compile `code/game`, `code/cgame`,
  `code/q3_ui` into the executable.
- `baseq3/pak0.pk3` is gitignored and lives on disk (479MB).
