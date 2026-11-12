# SPDX-License-Identifier: AGPL-3.0-or-later
# Shared compiler settings, applied through the DriftboxCompilerSettings interface target.

add_library(DriftboxCompilerSettings INTERFACE)
target_compile_features(DriftboxCompilerSettings INTERFACE cxx_std_20)

if(MSVC)
  target_compile_options(DriftboxCompilerSettings INTERFACE /utf-8 /Zc:__cplusplus /permissive- /W4)
else()
  target_compile_options(DriftboxCompilerSettings INTERFACE -Wall -Wextra -Wno-unused-parameter)
endif()

target_link_libraries(DriftboxCompilerSettings INTERFACE
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags)

target_compile_definitions(DriftboxCompilerSettings INTERFACE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_REPORT_APP_USAGE=0)
