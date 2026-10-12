cmake_minimum_required(VERSION 3.22.1)

project(Quake-III-Arena VERSION 1.337 LANGUAGES C)

#
# misc stuff
#

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEBUG TRUE)
    add_compile_definitions(DEBUG)
endif()

add_compile_definitions(__SDL2__)

set(CMAKE_C_FLAGS "-Wno-comment -Wno-format-security")

#
# external deps
#

include(FetchContent)

FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.32.4
)

FetchContent_MakeAvailable(SDL2)

#
# Quake3
#

file(GLOB QCOMMON_HEADERS "code/qcommon/*.h")
file(GLOB QCOMMON_SOURCES "code/qcommon/*.c")
list(FILTER QCOMMON_SOURCES EXCLUDE REGEX "vm_ppc*")
list(FILTER QCOMMON_SOURCES EXCLUDE REGEX "vm_x86*")

file(GLOB COMMON_HEADERS "common/*.h")
file(GLOB COMMON_SOURCES "common/*.c")

file(GLOB GAME_HEADERS "code/game/*.h")
file(GLOB GAME_SOURCES "code/game/*.c")
list(FILTER GAME_SOURCES EXCLUDE REGEX "g_rankings*")
list(FILTER GAME_SOURCES EXCLUDE REGEX "bg_lib*")

file(GLOB CLIENT_HEADERS "code/client/*.h")
file(GLOB CLIENT_SOURCES "code/client/*.c")

file(GLOB SERVER_HEADERS "code/server/*.h")
file(GLOB SERVER_SOURCES "code/server/*.c")
list(FILTER SERVER_SOURCES EXCLUDE REGEX "sv_rankings*")

file(GLOB BSPC_HEADERS "code/bspc/*.h")
file(GLOB BSPC_SOURCES "code/bspc/*.c")
list(FILTER BSPC_SOURCES INCLUDE REGEX "qbsp.h|l_log.h|l_mem.h|l_qfiles.h")

file(GLOB BOTLIB_HEADERS "code/botlib/*.h")
file(GLOB BOTLIB_SOURCES "code/botlib/*.c")

file(GLOB RENDERER_HEADERS "code/renderer/*.h")
file(GLOB RENDERER_SOURCES "code/renderer/*.c")

file(GLOB PLATFORM_HEADERS "code/sdl2/*.h")
file(GLOB PLATFORM_SOURCES "code/sdl2/*.c")

add_executable(Quake3
    ${QCOMMON_HEADERS}
    ${QCOMMON_SOURCES}

    ${COMMON_HEADERS}
    ${COMMON_SOURCES}

    ${GAME_HEADERS}
    ${GAME_SOURCES}

    ${CLIENT_HEADERS}
    ${CLIENT_SOURCES}

    ${SERVER_HEADERS}
    ${SERVER_SOURCES}

    ${BSPC_HEADERS}
    ${BSPC_SOURCES}

    ${BOTLIB_HEADERS}
    ${BOTLIB_SOURCES}

    ${RENDERER_HEADERS}
    ${RENDERER_SOURCES}    

    ${PLATFORM_HEADERS}
    ${PLATFORM_SOURCES}    
)

# target_include_directories(Quake3 PRIVATE ${SDL2_SOURCE_DIR}/include)

# target_link_libraries(Quake3 PUBLIC m SDL2::SDL2)
target_link_libraries(Quake3 PUBLIC m)

target_compile_definitions(Quake3 PUBLIC __SDL2__)
