cmake_minimum_required(VERSION 3.22)
project(adsb-radar C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)
include(ExternalProject)

set(FETCHCONTENT_QUIET OFF)

# ── SDL3 ────────────────────────────────────────────────────────────────────
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        d9d5536704d585616d4db3c8ba3c4ff6fc2757e1
    GIT_SHALLOW    TRUE
)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO    OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC   OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR   OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA   OFF CACHE BOOL "" FORCE)
set(SDL_GPU      OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG   OFF CACHE BOOL "" FORCE)
set(SDL_TRAY     OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI   OFF CACHE BOOL "" FORCE)
set(SDL_SHARED   OFF CACHE BOOL "" FORCE)
set(SDL_STATIC   ON  CACHE BOOL "" FORCE)
set(SDL_LIBUDEV   OFF CACHE BOOL "" FORCE)
set(SDL_LIBINPUT  ON  CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# ── SDL3_ttf (vendored FreeType — no system dep required) ────────────────────
FetchContent_Declare(SDL3_ttf
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
    GIT_TAG        a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b
    GIT_SHALLOW    TRUE
)
set(SDLTTF_VENDORED  ON  CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ  OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG  OFF CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES   OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3_ttf)

# ── cJSON ────────────────────────────────────────────────────────────────────
FetchContent_Declare(cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG        c859b25da02955fef659d658b8f324b5cde87be3
    GIT_SHALLOW    TRUE
)
set(ENABLE_CJSON_TEST            OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_AND_STATIC_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cjson)

# ── readsb v3.16.15 ──────────────────────────────────────────────────────────
# Makefile project — built out-of-band via ExternalProject, no SDR hardware,
# no ncurses (DISABLE_INTERACTIVE=yes).  Requires: libzstd-dev zlib1g-dev
ExternalProject_Add(readsb_ext
    URL      https://github.com/wiedehopf/readsb/archive/refs/tags/v3.16.15.tar.gz
    URL_HASH SHA256=7a8c3886c6c548578e9a65f7b45a993bcd49aa52f70d5bffe1e76b4cbca9998f
    BUILD_IN_SOURCE  TRUE
    CONFIGURE_COMMAND ""
    BUILD_COMMAND     ${CMAKE_MAKE_PROGRAM} DISABLE_INTERACTIVE=yes AIRCRAFT_HASH_BITS=11 RTLSDR=yes "OPTIMIZE=-O3 -march=native"
    INSTALL_COMMAND   ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/readsb ${CMAKE_BINARY_DIR}/readsb
    USES_TERMINAL_DOWNLOAD TRUE
    USES_TERMINAL_BUILD    TRUE
)

# ── Application target ───────────────────────────────────────────────────────
file(GLOB SOURCES "src/*.c")

add_executable(adsb-radar ${SOURCES})

target_include_directories(adsb-radar PRIVATE
    src
    ${cjson_SOURCE_DIR}
)

target_link_libraries(adsb-radar PRIVATE
    SDL3::SDL3-static
    SDL3_ttf::SDL3_ttf-static
    cjson
    m
)

# ── Compiler flags ───────────────────────────────────────────────────────────
target_compile_options(adsb-radar PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    $<$<CONFIG:Debug>:-O0 -g3 -fsanitize=address,undefined>
    $<$<CONFIG:Release>:-O3 -ffast-math -funroll-loops -fomit-frame-pointer
                            -flto=auto -fvisibility=hidden>
)
target_link_options(adsb-radar PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    $<$<CONFIG:Release>:-flto=auto -O3>
)

add_dependencies(adsb-radar readsb_ext)

# ── RPi AArch64 flags (applied after target exists) ─────────────────────────
include(cmake/rpi_flags.cmake)

# ── Copy assets to build directory ──────────────────────────────────────────
add_custom_command(TARGET adsb-radar POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:adsb-radar>/assets"
    COMMENT "Copying assets to build directory"
)
add_custom_command(TARGET adsb-radar POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/adsb-radar.ini"
        "$<TARGET_FILE_DIR:adsb-radar>/adsb-radar.ini"
    COMMENT "Copying config to build directory"
)
