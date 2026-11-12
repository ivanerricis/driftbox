# Changelog

All notable changes to this project are documented here. The format follows the sections that `cliff.toml` generates from Conventional Commits.

## Unreleased

### Features
- Deterministic per-voice drift profile generated from a seed (SplitMix64, frozen version 1), with lerp for crossfades.
- Per-voice drift channel (RBJ low shelf, bell and high shelf, first-order allpass delay, gain, soft saturation, filtered noise), bit-exact transparent at zero drift.
- Multi-voice drift engine: control-rate smoothing, click-free voice count changes and Randomize crossfade, 20 ms bypass fade, soft safety ceiling, NaN/Inf sanitizing and meters.

### Build and CI
- Project scaffold: CMake with CPM (JUCE 8.0.15, clap-juce-extensions, Catch2), presets, frozen parameter and DSP contracts, test harness.
