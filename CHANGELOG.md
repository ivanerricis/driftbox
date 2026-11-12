# Changelog

All notable changes to this project are documented here. The format follows the sections that `cliff.toml` generates from Conventional Commits.

## Unreleased

### Features
- Deterministic per-voice drift profile generated from a seed (SplitMix64, frozen version 1), with lerp for crossfades.
- Per-voice drift channel (RBJ low shelf, bell and high shelf, first-order allpass delay, gain, soft saturation, filtered noise), bit-exact transparent at zero drift.
- Multi-voice drift engine: control-rate smoothing, click-free voice count changes and Randomize crossfade, 20 ms bypass fade, soft safety ceiling, NaN/Inf sanitizing and meters.
- Processor state with the drift seed (hex, restored without a crossfade) and profile version, Randomize as one undo step, gesture-based undo/redo of parameter changes.

### Build and CI
- GitHub Actions pipelines adapted from Pentagrit: PR title check, clang-format 18, clang-tidy, ASan/UBSan, Windows/Linux/macOS builds with pluginval, auval and clap-validator, packaging scripts (Windows and macOS zip, Linux tar.gz), development and branch protection docs.
- Project scaffold: CMake with CPM (JUCE 8.0.15, clap-juce-extensions, Catch2), presets, frozen parameter and DSP contracts, test harness.
