cmake_minimum_required(VERSION 3.22.1)

project(Quake-III-Arena VERSION 1.337 LANGUAGES C)

#
# misc stuff
#

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEBUG TRUE)
    add_compile_definitions(DEBUG)
endif()

# the platform layer we implement (video / input / sound / net) is SDL2
add_compile_definitions(__SDL2__)

# make the .so / .dylib generation path more deterministic (I think)
set(CMAKE_INSTALL_RPATH "@executable_path")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# this is how you tell emscripten to produce the final .html
if (EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
endif()

#
# CFLAGS and LDFLAGS
#

set(DRY_CFLAGS
    "-std=gnu89" # the source relies on pre-C99 __inline semantics (see botlib/be_aas_route.c)
    "-trigraphs" # Quake 3 source uses this archaic character thing
    "-fno-common" # reduce scope of globals a bit
    "-Wall" # warn for everything
    "-Wextra" # warn for more than everything?
    "-Werror" # error for all warnings
    # disable a selection of warnings that the Quake 3 source contravenes
    "-Wno-comment"
    "-Wno-dangling-else"
    "-Wno-deprecated-declarations"
    "-Wno-deprecated-non-prototype"
    "-Wno-format-security"
    "-Wno-format"
    "-Wno-parentheses-equality"
    "-Wno-misleading-indentation"
    "-Wno-missing-field-initializers"
    "-Wno-pointer-sign"
    "-Wno-pointer-to-int-cast"
    "-Wno-shift-overflow"
    "-Wno-sign-compare"
    "-Wno-switch"
    "-Wno-trigraphs"
    "-Wno-unused-but-set-variable"
    "-Wno-unused-parameter"
    "-Wno-unused-variable"
    "-Wno-void-pointer-to-int-cast"
    "-Wno-strncat-size"
    "-Wno-macro-redefined"
    "-Wno-shift-negative-value"
    "-Wno-self-assign"
    "-Wno-enum-compare"
    "-Wno-implicit-enum-enum-cast"
    "-Wno-absolute-value"
    "-Wno-int-conversion"
    "-Wno-int-to-void-pointer-cast"
    "-Wno-int-to-pointer-cast"
    "-Wno-null-dereference"
    "-Wno-cast-function-type-mismatch"
    "-Wno-semicolon-before-method-body"
    "-Wno-error=unused-result"
    "-Wno-error=format-overflow"
    "-Wno-error=format-truncation"
    "-Wno-error=array-bounds"
    "-Wno-error=strict-aliasing"
)

set(DRY_LDFLAGS
)

if (EMSCRIPTEN)
  set(DRY_CFLAGS ${DRY_CFLAGS}
    "-sUSE_SDL=2" # compile the emscripten-provided SDL2
    "-matomics"
    "-mbulk-memory"
  )

  set(DRY_LDFLAGS ${DRY_LDFLAGS}
    "-sUSE_SDL=2" # link the emscripten-provided SDL2
    # Q3's renderer is fixed-function multitexture. WebGL has no glTexEnvf,
    # glPushMatrix, glLoadMatrixf, glOrtho, glPolygonMode, glAlphaFunc,
    # glClipPlane. Emscripten's legacy GL emulation supplies all of them.
    "-sLEGACY_GL_EMULATION=1"
    "-sGL_UNSAFE_OPTS=0"
    "--error-limit=0" # report every undefined GL symbol, not just the first 10
    "-sINITIAL_MEMORY=256MB"
    "-sTOTAL_STACK=64MB"
    "-sSTACK_SIZE=8MB"
    "-lidbfs.js"
    "-lwebsocket.js"
    "--shell-file=../shell.html"
    "-lopenal"
    "-sEXIT_RUNTIME=1"
    "-sASYNCIFY=1" # needed because the connect loop and probably other parts don't yield to the main loop
    "-sASYNCIFY_STACK_SIZE=16384000"
    "-Wno-experimental"
    "-sALLOW_BLOCKING_ON_MAIN_THREAD=0"
  )
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(DRY_CFLAGS ${DRY_CFLAGS}
    "-g" # debug symbols
  )
else()
  set(DRY_CFLAGS ${DRY_CFLAGS}
    "-O3"
    # note: don't enable this- the VM and the renderer rely on exact float behaviour
    # "-ffast-math"
  )

  set(DRY_LDFLAGS ${DRY_LDFLAGS}
    "-O3"
    "-flto"
  )
endif()

if (EMSCRIPTEN)
  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DRY_CFLAGS ${DRY_CFLAGS}
      "-gsource-map"
      "--source-map-base=/"
    )

    set(DRY_LDFLAGS ${DRY_LDFLAGS}
      "-Wno-limited-postlink-optimizations"
      "-gsource-map"
      "--source-map-base=/"
      "-sSOURCE_MAP_PREFIXES=/${PROJECT_SOURCE_DIR}/=/"
      "-sSTACK_OVERFLOW_CHECK=2"
      "-sCHECK_NULL_WRITES=1"
      "-sASSERTIONS=2"
      "-sGL_DEBUG=1"
    )
  endif()
endif()

string(JOIN " " DRY_LDFLAGS_STR ${DRY_LDFLAGS})
set(CMAKE_EXE_LINKER_FLAGS ${DRY_LDFLAGS_STR})

#
# external deps
#

# Emscripten bundles SDL2; native builds get it from FetchContent
if (NOT EMSCRIPTEN)
  include(FetchContent)

  FetchContent_Declare(
      SDL2
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG release-2.32.4
  )

  # we only need the library, nothing else
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES_WERROR OFF CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(SDL2)
endif()

#
# Quake3Lib - the engine. Game, cgame and UI are NOT in here: they ship as
# .qvm bytecode in baseq3/pak2.pk3 and run through vm_interpreted.c
#

set(QUAKE3_SOURCES
    # qcommon
    code/qcommon/cm_load.c
    code/qcommon/cm_patch.c
    code/qcommon/cm_polylib.c
    code/qcommon/cm_test.c
    code/qcommon/cm_trace.c
    code/qcommon/cmd.c
    code/qcommon/common.c
    code/qcommon/cvar.c
    code/qcommon/files.c
    code/qcommon/huffman.c
    code/qcommon/md4.c
    code/qcommon/msg.c
    code/qcommon/net_chan.c
    code/qcommon/unzip.c
    code/qcommon/vm.c
    code/qcommon/vm_interpreted.c

    # client
    code/client/cl_cgame.c
    code/client/cl_cin.c
    code/client/cl_console.c
    code/client/cl_input.c
    code/client/cl_keys.c
    code/client/cl_main.c
    code/client/cl_net_chan.c
    code/client/cl_parse.c
    code/client/cl_scrn.c
    code/client/cl_ui.c
    code/client/snd_adpcm.c
    code/client/snd_dma.c
    code/client/snd_mem.c
    code/client/snd_mix.c
    code/client/snd_wavelet.c

    # server
    code/server/sv_bot.c
    code/server/sv_ccmds.c
    code/server/sv_client.c
    code/server/sv_game.c
    code/server/sv_init.c
    code/server/sv_main.c
    code/server/sv_net_chan.c
    code/server/sv_snapshot.c
    code/server/sv_world.c

    # botlib
    code/botlib/be_aas_bspq3.c
    code/botlib/be_aas_cluster.c
    code/botlib/be_aas_debug.c
    code/botlib/be_aas_entity.c
    code/botlib/be_aas_file.c
    code/botlib/be_aas_main.c
    code/botlib/be_aas_move.c
    code/botlib/be_aas_optimize.c
    code/botlib/be_aas_reach.c
    code/botlib/be_aas_route.c
    code/botlib/be_aas_routealt.c
    code/botlib/be_aas_sample.c
    code/botlib/be_ai_char.c
    code/botlib/be_ai_chat.c
    code/botlib/be_ai_gen.c
    code/botlib/be_ai_goal.c
    code/botlib/be_ai_move.c
    code/botlib/be_ai_weap.c
    code/botlib/be_ai_weight.c
    code/botlib/be_ea.c
    code/botlib/be_interface.c
    code/botlib/l_crc.c
    code/botlib/l_libvar.c
    code/botlib/l_log.c
    code/botlib/l_memory.c
    code/botlib/l_precomp.c
    code/botlib/l_script.c
    code/botlib/l_struct.c

    # renderer
    code/renderer/tr_animation.c
    code/renderer/tr_backend.c
    code/renderer/tr_bsp.c
    code/renderer/tr_cmds.c
    code/renderer/tr_curve.c
    code/renderer/tr_flares.c
    code/renderer/tr_font.c
    code/renderer/tr_image.c
    code/renderer/tr_init.c
    code/renderer/tr_light.c
    code/renderer/tr_main.c
    code/renderer/tr_marks.c
    code/renderer/tr_mesh.c
    code/renderer/tr_model.c
    code/renderer/tr_noise.c
    code/renderer/tr_scene.c
    code/renderer/tr_shade.c
    code/renderer/tr_shade_calc.c
    code/renderer/tr_shader.c
    code/renderer/tr_shadows.c
    code/renderer/tr_sky.c
    code/renderer/tr_surface.c
    code/renderer/tr_world.c

    # jpeg (for .jpg level images)
    code/jpeg-6/jcapimin.c
    code/jpeg-6/jcapistd.c
    code/jpeg-6/jccoefct.c
    code/jpeg-6/jccolor.c
    code/jpeg-6/jcdctmgr.c
    code/jpeg-6/jchuff.c
    code/jpeg-6/jcinit.c
    code/jpeg-6/jcmainct.c
    code/jpeg-6/jcmarker.c
    code/jpeg-6/jcmaster.c
    code/jpeg-6/jcomapi.c
    code/jpeg-6/jcparam.c
    code/jpeg-6/jcphuff.c
    code/jpeg-6/jcprepct.c
    code/jpeg-6/jcsample.c
    code/jpeg-6/jdapimin.c
    code/jpeg-6/jdapistd.c
    code/jpeg-6/jdatasrc.c
    code/jpeg-6/jdcoefct.c
    code/jpeg-6/jdcolor.c
    code/jpeg-6/jddctmgr.c
    code/jpeg-6/jdhuff.c
    code/jpeg-6/jdinput.c
    code/jpeg-6/jdmainct.c
    code/jpeg-6/jdmarker.c
    code/jpeg-6/jdmaster.c
    code/jpeg-6/jdmerge.c
    code/jpeg-6/jdphuff.c
    code/jpeg-6/jdpostct.c
    code/jpeg-6/jdsample.c
    code/jpeg-6/jdtrans.c
    code/jpeg-6/jerror.c
    code/jpeg-6/jfdctflt.c
    code/jpeg-6/jidctflt.c
    code/jpeg-6/jmemmgr.c
    code/jpeg-6/jmemnobs.c
    code/jpeg-6/jutils.c

    # shared math / string helpers (the game module supplies these)
    code/game/q_math.c
    code/game/q_shared.c

    # SDL2 platform
    code/sdl2/sdl2_glimp.c
    code/sdl2/sdl2_input.c
    code/sdl2/sdl2_net.c
    code/sdl2/sdl2_snddma.c
    code/sdl2/sdl2_sys.c
)

# Emscripten's legacy GL emulation lacks glArrayElement and glCallList
if (EMSCRIPTEN)
  list(APPEND QUAKE3_SOURCES code/sdl2/sdl2_glemu.c)
endif()

add_library(Quake3Lib ${QUAKE3_SOURCES})

# botlib gates its own includes behind -DBOTLIB (see code/unix/Makefile DO_BOT_CC)
# vm.c needs stubs for the x86 JIT (vm_x86.c is x86-only inline asm); the
# vm_* cvars are forced to bytecode in Sys_Init so the compiled path is never taken
set_source_files_properties(code/qcommon/vm.c PROPERTIES COMPILE_DEFINITIONS "oDLL_ONLY")

set_source_files_properties(
    code/botlib/be_aas_bspq3.c
    code/botlib/be_aas_cluster.c
    code/botlib/be_aas_debug.c
    code/botlib/be_aas_entity.c
    code/botlib/be_aas_file.c
    code/botlib/be_aas_main.c
    code/botlib/be_aas_move.c
    code/botlib/be_aas_optimize.c
    code/botlib/be_aas_reach.c
    code/botlib/be_aas_route.c
    code/botlib/be_aas_routealt.c
    code/botlib/be_aas_sample.c
    code/botlib/be_ai_char.c
    code/botlib/be_ai_chat.c
    code/botlib/be_ai_gen.c
    code/botlib/be_ai_goal.c
    code/botlib/be_ai_move.c
    code/botlib/be_ai_weap.c
    code/botlib/be_ai_weight.c
    code/botlib/be_ea.c
    code/botlib/be_interface.c
    code/botlib/l_crc.c
    code/botlib/l_libvar.c
    code/botlib/l_log.c
    code/botlib/l_memory.c
    code/botlib/l_precomp.c
    code/botlib/l_script.c
    code/botlib/l_struct.c
    PROPERTIES COMPILE_DEFINITIONS "BOTLIB"
)

set_target_properties(Quake3Lib PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if (NOT EMSCRIPTEN)
  find_package(OpenGL REQUIRED)
  target_link_libraries(Quake3Lib PUBLIC m SDL2::SDL2-static OpenGL::GL)
else()
  target_link_libraries(Quake3Lib PUBLIC m)
endif()

target_compile_options(Quake3Lib PUBLIC ${DRY_CFLAGS})

#
# Quake3 - the executable; main() lives in sdl2_main.c
#

add_executable(Quake3
  code/sdl2/sdl2_main.c
)

# main() lives in sdl2_main.c, which is deliberately not part of Quake3Lib

set_target_properties(Quake3 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if (NOT EMSCRIPTEN)
  target_link_libraries(Quake3 PUBLIC Quake3Lib m SDL2::SDL2-static OpenGL::GL)
else()
  target_link_libraries(Quake3 PUBLIC Quake3Lib m)
endif()

target_compile_options(Quake3 PUBLIC ${DRY_CFLAGS})
target_link_options(Quake3 PUBLIC)

if (EMSCRIPTEN)
  # assets-wasm/baseq3 is the per-map dep manifest extracted by
  # tools/build-asset-dir.sh (~29 MB for one arena map). Preloading all of
  # baseq3 would be 457 MB. Without it, preload pak2 so the VMs exist.
  if (EXISTS ${CMAKE_SOURCE_DIR}/assets-wasm/baseq3)
    set_target_properties(Quake3 PROPERTIES LINK_FLAGS "--preload-file ${CMAKE_SOURCE_DIR}/assets-wasm/baseq3@./baseq3 -sEXPORTED_RUNTIME_METHODS=FS,ccall,cwrap")
  else()
    set_target_properties(Quake3 PROPERTIES LINK_FLAGS "--preload-file ${CMAKE_SOURCE_DIR}/baseq3/pak2.pk3@./baseq3/pak2.pk3 -sEXPORTED_RUNTIME_METHODS=FS,ccall,cwrap")
  endif()
endif()
