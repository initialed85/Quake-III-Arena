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
- [x] Textures render (S3TC upload path fixed — see AGENTS.md)
- [x] Console and chat text entry (`SE_CHAR` from `SDL_TEXTINPUT`), verified with `in_selftest 1`
- [x] Regression check for the platform layer (headless / dedicated run) — `./test.sh`
- [x] UDP multiplayer: dedicated server + client `+connect localhost:27960` — `tools/mptest.py`

WASM:

- [ ] `build-wasm.sh` / `run-wasm.sh` (emscripten, `-sUSE_SDL=2`, preload `baseq3`)
- [ ] Emscripten main loop: `main` is `while(1) Com_Frame()`; needs `emscripten_set_main_loop`
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
