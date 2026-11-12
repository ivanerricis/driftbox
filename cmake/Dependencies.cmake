# SPDX-License-Identifier: AGPL-3.0-or-later
# Pinned third-party dependencies, fetched through CPM.

# CPM.cmake v0.43.2 (vendored release asset). Verify integrity before use.
set(DRIFTBOX_CPM_SHA256 "49a3bef91ceb65bb66d57255e12d1ffd22abc2f6408fa9fe4534c544a2f232aa")
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/CPM.cmake" _driftbox_cpm_actual)
if(NOT _driftbox_cpm_actual STREQUAL DRIFTBOX_CPM_SHA256)
  message(FATAL_ERROR "cmake/CPM.cmake checksum mismatch: ${_driftbox_cpm_actual}")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/CPM.cmake")

# JUCE 8.0.15
CPMAddPackage(
  NAME JUCE
  GITHUB_REPOSITORY juce-framework/JUCE
  GIT_TAG 8.0.15
  GIT_SHALLOW ON
)

# clap-juce-extensions (pinned commit; clap libs come as submodules)
CPMAddPackage(
  NAME clap-juce-extensions
  GITHUB_REPOSITORY free-audio/clap-juce-extensions
  GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d
)

# Catch2 v3.16.1 (tests only)
if(DRIFTBOX_BUILD_TESTS)
  CPMAddPackage(
    NAME Catch2
    GITHUB_REPOSITORY catchorg/Catch2
    GIT_TAG v3.16.1
    GIT_SHALLOW ON
    OPTIONS "CATCH_INSTALL_DOCS OFF" "CATCH_INSTALL_EXTRAS OFF"
  )
  list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
endif()
