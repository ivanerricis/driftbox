<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Driftbox architecture

This is the source of truth for the frozen contracts, which were set by the scaffold (T0). The headers in `src/` are authoritative, and the work plan is in `docs/PLAN.md`.

Parameter IDs are stable and must never be renamed once released: CLAP hashes them and hosts store them.

## DSP summary

Product model: a **channel number per side**. L and R each pick a channel from 1 to 64, like a strip of a virtual mixer. Every channel number maps to a fixed, deterministic set of tolerances that is identical on every platform.

Signal flow for one block (stereo or mono in/out, zero latency):

```
for each side s (0 = L, 1 = R):
  out_s = chain_s(in_s)          one DriftChannel per side, tolerances of channel chanL / chanR
-> output gain (smoothed 20 ms) -> softCeiling() (soft knee from -1 dBFS, never above 0 dBFS)
-> sanitize NaN/Inf -> bypass crossfade (20 ms) towards the dry input
```

- **Chain** (`DriftChannel`, one per side): low shelf -> bell -> high shelf -> first-order allpass (a small, always-positive fractional delay) -> gain -> soft saturation `tanh(k x) / k` -> added noise (white noise, one-pole low-passed).
- **Deviations**: every deviation equals channel tolerance x `drift` x tolerance amount x limit. The limits are in `limits::` in `src/dsp/DriftProfile.h`:
  - shelves +-1 dB, bell +-0.75 dB
  - delay 0..5 us
  - gain +-1 dB
  - drive k <= 2.15
  - noise about -84 dBFS RMS at the output
- **Drift Amount scales everything.** At `drift = 0` every chain is the exact identity (identity biquad coefficients, allpass a = 1, gain 1, saturation skipped, no noise), so the output is the input itself.
- **Channel profiles**: `channelProfile(channel, version)` is pure and deterministic and is a FROZEN FORMAT (v1). Seed of channel c: `SplitMix64 h(kConsoleSeed + c); s = h.next();` then a `SplitMix64 rng(s)` yields the ten tolerance fields in a fixed order and the noise seed. The hash step avoids feeding consecutive seeds straight into SplitMix64 (states that differ by multiples of the gamma increment give shifted, correlated sequences). It uses its own PRNG with an explicit integer-to-float mapping, so a channel sounds the same on every OS. Channel numbers are clamped to 1..64.
- **Channel changes**: when the channel of a side changes, the tolerances of that side crossfade from their current effective value (also when a fade is already running) to the profile of the new channel in 250 ms (`lerp`). The noise seed of the side switches immediately, without resetting the filters. The two sides fade independently. At the first block after `prepare()` or `reset()` there is no fade: the values are the targets.
- **Mono**: only side 0 (L) is processed.
- **Control rate**: coefficients are recomputed every 32 samples from smoothed values (50 ms smoothing for the amounts).
- **Realtime safety**: no allocations or locks on the audio thread, `ScopedNoDenormals`, parameters through `std::atomic<float>*`, atomic meters.

## Parameters (10)

| ID | Type | Range | Default |
|---|---|---|---|
| `chanL` | int | 1..64 ("Channel L") | 1 |
| `chanR` | int | 1..64 ("Channel R") | 2 |
| `drift` | float | 0..1 (shown as %) | 0.5 |
| `freqTol` | float | 0..1 | 0.5 |
| `phaseTol` | float | 0..1 | 0.5 |
| `gainTol` | float | 0..1 | 0.5 |
| `saturation` | float | 0..1 | 0.2 |
| `noise` | float | 0..1 | 0 |
| `outGain` | float | -24..+24 dB, step 0.1 | 0 |
| `bypass` | bool | | off |

- All IDs use `ParameterID{id, 1}`, and all parameters are automatable (a channel change is a click-free crossfade).
- **Randomize is not a parameter.** It is a UI action that picks a random channel pair and sets `chanL` and `chanR`.
- Typed text ("50 %", "-3 dB", "12") round-trips through `withValueFromStringFunction`.
- The old `channels` parameter (2/4/8/16 voices) was removed before any release. States saved by development builds lose that value (see the CHANGELOG).

## State and versioning

The state is the APVTS value tree as XML, with tag `DRIFTBOX_STATE`. It carries these properties:

- `stateVersion`: `kStateVersion`, currently 1
- `profileVersion`: `kProfileVersion`, currently 1 (the only channel profile generator version that exists so far)
- `uiWidth` and `uiHeight`
- `advancedOpen`: whether the Advanced panel is open (absent means closed; not a parameter, no undo step)

The channels are ordinary parameters, so they are saved with the parameter tree. Loading and saving follow these rules:

- **Versions**: `setStateInformation` ignores a state whose `stateVersion` or `profileVersion` is newer than the plugin's. A missing version counts as 1. Older versions go through `migrateState()`.
- **Reopening a session**: the restored `chanL`/`chanR` are read at the first block after `prepareToPlay`, so there is no fade and a reopened session sounds bit-identical from the first sample. A state restored during playback fades like any automation.
- **New instances**: the constructor picks a random channel pair (as `randomize()` does, but without an undo step), so two new instances do not sound identical by chance. The parameter defaults (used by reset and Ctrl-click) stay 1 and 2. A restored state overwrites everything.
- **Randomize and undo**: `randomize()` (message thread) picks L in 1..64 and R = L % 64 + 1, different from the current pair, and applies both as ONE undo step named "Randomize" (a transaction with nested gestures on both parameters). Parameter gestures from the UI are recorded as one step each by `UndoRecorder`; host automation is not. Restoring a state clears the history. The constructor leaves no step in the history.
- **Legacy**: a `driftSeed` property from development builds is dropped on load.
- **Presets**: they never contain or overwrite `bypass`, because it is a project-level value.

## Frozen C++ contracts (excerpts)

```cpp
// params/ParamIDs.h
namespace driftbox {
inline constexpr int kParamVersion = 1, kStateVersion = 1;
inline constexpr const char* kStateTag = "DRIFTBOX_STATE";
namespace pid { chanL, chanR, drift, freqTol, phaseTol, gainTol, saturation, noise, outGain, bypass }
}
// params/DriftboxParameters.h
inline constexpr int kNumParameters = 10;
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
struct ParamRefs { explicit ParamRefs(apvts&); std::atomic<float>* ...; EngineSettings snapshot() const noexcept; };
// dsp/DspTypes.h
inline constexpr int kNumConsoleChannels = 64;
struct EngineSettings { int channelL = 1, channelR = 2; float drift01, freqTol01, phaseTol01, gainTol01,
                        saturation01, noise01, outGainDb; bool bypass; };
// dsp/Prng.h       SplitMix64 { next(), nextUnipolar() [0,1), nextBipolar() [-1,1) }, Xorshift32
// dsp/DriftProfile.h
struct VoiceTolerance { lowShelfDb, lowShelfOct, highShelfDb, highShelfOct, bellDb, bellPos01, delay01, gainDb,
                        driveSpread, noiseLevel; };
struct ChannelProfile { VoiceTolerance tol; std::uint32_t noiseSeed; };
ChannelProfile channelProfile(int channel, int version = kProfileVersion) noexcept;   // frozen v1
VoiceTolerance lerp(const VoiceTolerance&, const VoiceTolerance&, float t) noexcept;
// dsp/Biquad.h     BiquadCoeffs::{identity, lowShelf, highShelf, peak}(sr, hz, q, gainDb); Biquad (TDF-II)
// dsp/DriftChannel.h
struct VoiceAmounts { float freq, phase, gain, saturation, noise; };   // already x drift01
class DriftChannel { prepare(sr); reset(); setNoiseSeed(u32); update(tol, amounts, noiseScale);
                     processAdd(in, acc, n, w0, w1); processSample(x); };
// dsp/DriftEngine.h
class DriftEngine { prepare(sr, maxBlock, numCh); reset(); setSettings(EngineSettings); process(AudioBuffer<float>&);
                    meters(); channelOf(side); isFading(side); };
// dsp/DspUtils.h   fastTanh, dbToGain, gainToDb, timeToCoeff, clampTo (from Pentagrit), sanitize, softCeiling
```

DSP module conventions (same as Pentagrit):

- `prepare()` may allocate, and is only called from `prepareToPlay`.
- `reset()`, `update()`, `setSettings()` and `process*()` are `noexcept` and allocation-free.

## Editor (T7)

- `PluginEditor` holds a `Content` component laid out at the base size 720 x 440 and scaled by a transform to the
  editor size (fixed aspect ratio, zoom 75 % to 200 %; `setZoom()` resizes the window, the size is saved with
  `setUiSize()`). Host DPI scaling is left to the host.
- `src/ui/`: `Theme.h` (palette and sizes, see `docs/design/THEME.md`), `Fonts` (embedded IBM Plex), `LookAndFeel`,
  `Widgets` (knobs, channel selector, buttons, stereo meter), `TopBar`, `PresetBar` (T8 hooks), `Panels`
  (channel, tolerance graph, drift, advanced, output) and `Footer`.
- A 30 Hz timer reads the parameters; the tolerance graph recomputes and repaints only when its inputs change.
  The curve comes from `toleranceDeviationDb()` (`src/dsp/ToleranceResponse.h`), which uses
  `DriftChannel::design()`, the same design values the engine applies.
- The UI only reads atomics (`MeterState`, parameters) and never runs in the audio path. `MeterState` has per-side
  output peaks (`outPeakDbL`, `outPeakDbR`) for the stereo meter.
- Hooks for T8: `PresetBar::onPrev/onNext/onOpenList/onOpenActions` and `setPresetName()`,
  `DriftboxAudioProcessorEditor::onSettings`, `showCpu`, `setZoom()`, `setTooltipsEnabled()`, `content()`.

## Build notes

- `src/` sources are collected with a CMake glob (`CONFIGURE_DEPENDS`) into the `DriftboxCore` INTERFACE target, which the plugin and the tests share. A new file needs no CMake edit, but test files must be listed in `tests/CMakeLists.txt`.
- Formats: VST3 and CLAP on all platforms, plus AU on macOS. There is no Standalone.
