# Changelog

All notable changes to this project are documented here. The format follows the sections that `cliff.toml` generates from Conventional Commits.

## Unreleased

### Features
- Channel selector L/R: each side picks a virtual mixer channel from 1 to 64 (`chanL`, `chanR`), and every channel number maps to a fixed, deterministic set of tolerances (SplitMix64, frozen profile version 1) that is identical on every platform. One DSP chain per side; a channel change crossfades the tolerances in 250 ms without clicks.
- Randomize picks a random channel pair and applies it as one undo step; new instances start with a random pair.
- Drift channel (RBJ low shelf, bell and high shelf, first-order allpass delay, gain, soft saturation, filtered noise), bit-exact transparent at zero drift.
- Drift engine: control-rate smoothing, 20 ms bypass fade, soft safety ceiling, NaN/Inf sanitizing and meters.
- Processor state with the profile version, gesture-based undo/redo of parameter changes.

### BREAKING
- The `channels` parameter (2/4/8/16 internal voices) is removed, and the hidden `driftSeed` is dropped from the state (a legacy `driftSeed` property is ignored on load). States saved by development builds lose the old channel count and seed, and sound different. There is no release yet, so no released session is affected.

### Build and CI
- GitHub Actions pipelines adapted from Pentagrit: PR title check, clang-format 18, clang-tidy, ASan/UBSan, Windows/Linux/macOS builds with pluginval, auval and clap-validator, packaging scripts (Windows and macOS zip, Linux tar.gz), development and branch protection docs.
- Project scaffold: CMake with CPM (JUCE 8.0.15, clap-juce-extensions, Catch2), presets, frozen parameter and DSP contracts, test harness.
