# SPDX-License-Identifier: AGPL-3.0-or-later
include(Catch)

# I task successivi aggiungono qui i propri file di test (ProfileTests, ChannelTests, EngineTests, ...).
add_executable(DriftboxTests
  TestMain.cpp
  DriftboxTests.cpp
  ParameterTests.cpp
  ProfileTests.cpp
  ChannelTests.cpp
  EngineTests.cpp)

target_link_libraries(DriftboxTests PRIVATE DriftboxCore Catch2::Catch2)
target_include_directories(DriftboxTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

catch_discover_tests(DriftboxTests DISCOVERY_MODE PRE_TEST)
