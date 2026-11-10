# Amiga recomp core: the reusable base of the ports (from Lightmere).
# Use it from a game project with:   add_subdirectory(<path>/Framework/core amigacore)
#   target_link_libraries(mygame PRIVATE amigacore amigacore_sdl)
cmake_minimum_required(VERSION 3.20)
project(amigacore CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(AMIGACORE_SDL "Build the SDL2 parts (video, input, settings, dialogs)" ON)
option(AMIGACORE_TESTS "Build the core self-test" ON)

# 68000 CPU: Moira (Dirk W. Hoffmann, MIT licence)
add_library(amigacore_moira STATIC ext/moira/Moira.cpp ext/moira/MoiraCore68000.cpp ext/moira/MoiraCore68010.cpp
            ext/moira/MoiraCore68020.cpp ext/moira/MoiraDebugger.cpp)
target_include_directories(amigacore_moira SYSTEM PUBLIC ext/moira)
if(MSVC)
  target_compile_options(amigacore_moira PRIVATE /W0 /bigobj)
else()
  target_compile_options(amigacore_moira PRIVATE -w)
endif()

# machine (A500 chipset, optional AGA, hooks) + formats, no SDL
add_library(amigacore STATIC
  src/amiga/machine.cpp src/amiga/blitter.cpp
  src/formats/adf.cpp src/formats/hunk.cpp src/formats/rnc1.cpp src/formats/epf.cpp
  src/formats/ilbm.cpp src/formats/planar.cpp src/formats/palette.cpp
  src/platform/sha256.cpp src/platform/png_write.cpp
  src/platform/picture.cpp
  src/ui/options_panel.cpp)
target_include_directories(amigacore PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(amigacore PUBLIC amigacore_moira Threads::Threads)

if(AMIGACORE_SDL)
  find_package(SDL2 CONFIG REQUIRED)
  add_library(amigacore_sdl STATIC src/platform/video.cpp src/platform/settings.cpp src/platform/folder_dialog.cpp
              src/input/input.cpp)
  target_link_libraries(amigacore_sdl PUBLIC amigacore
      $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>)
endif()

if(AMIGACORE_TESTS)
  add_executable(amigacore_selftest tests/selftest.cpp)
  target_link_libraries(amigacore_selftest PRIVATE amigacore)
  # chipset tests: OCS framebuffer unchanged (SHA-256), AGA registers against independently computed pixels
  add_executable(amigacore_ocs_regress tests/ocs_regress.cpp)
  target_link_libraries(amigacore_ocs_regress PRIVATE amigacore)
  # picture modes: coverage and scanline regularity on simulated screens (1080p, 1440p, 4K, 720p)
  add_executable(amigacore_picture_check tests/picture_check.cpp)
  target_link_libraries(amigacore_picture_check PRIVATE amigacore)
  add_executable(amigacore_aga_selftest tests/aga_selftest.cpp)
  target_link_libraries(amigacore_aga_selftest PRIVATE amigacore)
endif()
