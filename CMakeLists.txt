cmake_minimum_required(VERSION 3.16)
project(DS2_Sitcom LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(dinput8 SHARED
  src/dllmain.cpp
  src/dinput8_proxy.cpp
  src/paths.cpp
  src/log.cpp
  src/config.cpp
  src/audio.cpp
  src/credit.cpp
  src/fmod_volume.cpp
  src/item_hooks.cpp
  src/estus_hooks.cpp
  src/area_title_hooks.cpp
  src/boss_bar_hooks.cpp
  src/mem.cpp
  src/ds2_flavor.cpp
  src/event_flags.cpp
  src/game_state.cpp
  src/events.cpp
  src/exports.def
)

target_include_directories(dinput8 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_compile_definitions(dinput8 PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  MA_NO_ENCODING
  MA_NO_GENERATION
)

target_link_libraries(dinput8 PRIVATE
  ole32
  winmm
  psapi
  user32
  gdi32
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  target_link_libraries(dinput8 PRIVATE d3d11 dxgi)
else()
  target_link_libraries(dinput8 PRIVATE d3d9)
  target_link_options(dinput8 PRIVATE -Wl,--enable-stdcall-fixup)
endif()

# Fully static so players don't need MinGW runtime DLLs next to the game.
target_link_options(dinput8 PRIVATE
  -static
  -static-libgcc
  -static-libstdc++
)

set_target_properties(dinput8 PROPERTIES
  PREFIX ""
  OUTPUT_NAME "dinput8"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_custom_command(TARGET dinput8 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/sitcom"
    "$<TARGET_FILE_DIR:dinput8>/sitcom"
  COMMENT "Copy sitcom/ next to dinput8.dll"
)
