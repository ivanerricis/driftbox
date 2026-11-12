# Changelog

## Unreleased

### Build and CI
- Project scaffold with frozen parameter and DSP contracts
- Build, test, lint and package pipelines adapted from Pentagrit (#5)
- Release packaging, installers and SHA256SUMS (#13)

### Bug fixes
- **dsp**: Treat output gain within half a 0.1 dB step of zero as exact unity (#6)

### Documentation
- README, contributing guide, changelog and third-party notices

### Features
- **dsp**: Deterministic drift profile from seed (#1)
- **dsp**: Per-voice drift channel with gentle EQ, phase, gain, saturation and noise (#2)
- **dsp**: Multi-voice drift engine with click-free voice changes and safety ceiling (#3)
- **processor**: Persist the drift seed, randomize with undo, gesture-based undo/redo (#4)
- **brand**: Driftbox mark (twin box), one-colour mark and app icon (#7)
- Replace internal voices with a per-side console channel selector (1-64) (#8) **BREAKING**
- **ui**: JUCE editor from the approved mockup (#9)
- **ui**: ±1.5 dB tolerance graph and saved Advanced panel state (#10)
- **presets**: Factory and user presets, settings panel (#11)
- **update**: Daily update check including pre-releases (#12)
