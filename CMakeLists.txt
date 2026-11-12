# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)

# CMake 4 non accetta più progetti che chiedono < 3.5 (alcune dipendenze lo fanno ancora).
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Driftbox VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(DRIFTBOX_BUILD_TESTS "Build the Catch2 test suite" ON)

include(cmake/Dependencies.cmake)
include(cmake/CompilerSettings.cmake)

configure_file(PluginInfo.h.in "${CMAKE_BINARY_DIR}/generated/PluginInfo.h" @ONLY)

# ---- Libreria core: tutto src/ tranne PluginEntry.cpp (condivisa da plugin e test) ----
file(GLOB_RECURSE DRIFTBOX_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER DRIFTBOX_CORE_SOURCES EXCLUDE REGEX "PluginEntry\.cpp$")

# Font dell'interfaccia incorporati (IBM Plex, SIL OFL 1.1, licenza in resources/ui/fonts/licenses).
file(GLOB DRIFTBOX_FONT_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/ui/fonts/*.ttf")
juce_add_binary_data(DriftboxUiAssets
  HEADER_NAME DriftboxUiData.h
  NAMESPACE DriftboxUiData
  SOURCES ${DRIFTBOX_FONT_FILES})
set_target_properties(DriftboxUiAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(DriftboxCore INTERFACE)
target_sources(DriftboxCore INTERFACE ${DRIFTBOX_CORE_SOURCES})
target_include_directories(DriftboxCore INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(DriftboxCore INTERFACE
  DriftboxCompilerSettings
  juce::juce_audio_utils
  juce::juce_dsp
  DriftboxUiAssets)

# ---- Plugin (nessuno Standalone: solo VST3, CLAP e AU su macOS) ----
set(DRIFTBOX_FORMATS VST3)
if(APPLE)
  list(APPEND DRIFTBOX_FORMATS AU)
endif()

juce_add_plugin(Driftbox
  COMPANY_NAME "Driftbox"
  PLUGIN_MANUFACTURER_CODE Ivnr
  PLUGIN_CODE Drbx
  FORMATS ${DRIFTBOX_FORMATS}
  PRODUCT_NAME "Driftbox"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  VST3_CATEGORIES Fx
  AU_MAIN_TYPE kAudioUnitType_Effect
  COPY_PLUGIN_AFTER_BUILD FALSE
  BUNDLE_ID com.ivanerricis.driftbox)

target_sources(Driftbox PRIVATE src/PluginEntry.cpp)
target_link_libraries(Driftbox PRIVATE DriftboxCore)

clap_juce_extensions_plugin(
  TARGET Driftbox
  CLAP_ID "com.ivanerricis.driftbox"
  CLAP_FEATURES audio-effect mixing stereo)

# ---- Test ----
if(DRIFTBOX_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
