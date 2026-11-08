cmake_minimum_required(VERSION 3.22)
project(SonicDNA VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(SDNA_BUILD_PLUGIN "Build the JUCE VST3/Standalone application" ON)
option(SDNA_BUILD_TESTS "Build core unit tests" ON)

# ---------------------------------------------------------------- core (no JUCE)
add_library(sdna_core STATIC
  core/src/Dsp.cpp
  core/src/Profile.cpp
  core/src/ArtistDatabase.cpp
  core/src/Kit.cpp
  core/src/KitGenerator.cpp
  core/src/DrumMachine.cpp
  core/src/Json.cpp
  core/src/KitIO.cpp
  core/src/OnlineAnalysis.cpp
  core/src/engines/Synth.cpp
  core/src/engines/DrumEngine.cpp
  core/src/engines/CymbalEngine.cpp
  core/src/engines/BassEngine.cpp
  core/src/engines/MelodicEngine.cpp
  core/src/engines/FxEngine.cpp
  core/src/engines/VocalEngine.cpp)
target_include_directories(sdna_core PUBLIC core/include)
find_package(Threads REQUIRED)
target_link_libraries(sdna_core PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(sdna_core PRIVATE /W3 /utf-8 /bigobj)
  add_compile_options(/utf-8)
else()
  target_compile_options(sdna_core PRIVATE -Wall -Wextra -Wno-unused-parameter -O2)
endif()

add_executable(sonicdna cli/main.cpp)
target_link_libraries(sonicdna PRIVATE sdna_core)

if(SDNA_BUILD_TESTS)
  enable_testing()
  add_executable(sdna_tests tests/core_tests.cpp)
  target_link_libraries(sdna_tests PRIVATE sdna_core)
  add_test(NAME core COMMAND sdna_tests)
endif()

if(SDNA_BUILD_PLUGIN)
  add_subdirectory(plugin)
endif()
