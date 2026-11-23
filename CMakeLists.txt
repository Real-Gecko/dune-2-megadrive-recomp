cmake_minimum_required(VERSION 3.20)
project(dune2_port CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL3 REQUIRED CONFIG)

file(GLOB DUNE_GAME_SOURCES CONFIGURE_DEPENDS src/game/*.cpp)
add_executable(dune2
  src/main.cpp
  src/assets.cpp
  src/host.cpp
  src/display.cpp
  src/md/mem.cpp
  src/audio.cpp
  src/gems.cpp
  src/psg.cpp
  src/ymfm/ymfm_opn.cpp
  src/ymfm/ymfm_adpcm.cpp
  src/ymfm/ymfm_ssg.cpp
  ${DUNE_GAME_SOURCES}
)
target_compile_options(dune2 PRIVATE -Wall -Wextra)
set_source_files_properties(src/audio.cpp src/gems.cpp src/ymfm/ymfm_opn.cpp src/ymfm/ymfm_adpcm.cpp src/ymfm/ymfm_ssg.cpp
  PROPERTIES COMPILE_OPTIONS -Wno-unused-parameter)
target_link_libraries(dune2 PRIVATE SDL3::SDL3)
if(WIN32)
  set_target_properties(dune2 PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(dune2 PRIVATE -static -s)
endif()
set_target_properties(dune2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
