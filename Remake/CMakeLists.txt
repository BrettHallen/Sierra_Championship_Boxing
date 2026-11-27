cmake_minimum_required(VERSION 3.16)
project(cbremake C)

# Clean-room style remake engine for Sierra Championship Boxing (1984, IBM PC).
# It contains no original code or assets: graphics and boxer data are read at
# runtime from the player's own copy of the game files.

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(CB_FETCH_SDL "Download and build SDL3 if it is not installed" OFF)

find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
  if(CB_FETCH_SDL)
    include(FetchContent)
    FetchContent_Declare(SDL3
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG release-3.2.24
      GIT_SHALLOW TRUE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
  else()
    message(FATAL_ERROR
      "SDL3 not found. Install it (macOS: brew install sdl3, Debian/Ubuntu: "
      "apt install libsdl3-dev, Windows: vcpkg install sdl3) or configure with "
      "-DCB_FETCH_SDL=ON to build it from source.")
  endif()
endif()

add_executable(cbremake
  src/main.c
  src/cga.c
  src/assets.c
  src/boxer.c
  src/fight.c
  src/screens.c
  src/anim.c
  src/audio.c
)
target_include_directories(cbremake PRIVATE src)
target_link_libraries(cbremake PRIVATE SDL3::SDL3)

if(MSVC)
  target_compile_options(cbremake PRIVATE /W3)
  target_compile_definitions(cbremake PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(cbremake PRIVATE -Wall -Wextra -Wno-unused-parameter)
  target_link_libraries(cbremake PRIVATE m)
endif()

# Small self-test that decodes every file it can find (no window needed).
enable_testing()
add_test(NAME decode_assets COMMAND cbremake --selftest)
