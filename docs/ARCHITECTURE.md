<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Driftbox architecture

This is the source of truth for the frozen contracts, which were set by the scaffold (T0). The headers in `src/` are authoritative, and the work plan is in `docs/PLAN.md`.

Parameter IDs are stable and must never be renamed: CLAP hashes them and hosts store them.

## DSP summary

Signal flow for one block (stereo or mono in/out, zero latency):

```
for each side s (0 = L, 1 = R):
  out_s = sum over voices v with (v & 1) == s of  weight_v * voice_v(in_s)
-> output gain (smoothed 20 ms) -> softCeiling() (soft knee from -1 dBFS, never above 0 dBFS)
-> sanitize NaN/Inf -> bypass crossfade (20 ms) towards the dry input
```

- **Voice chain** (`DriftChannel`, one per voice, up to 16): low shelf → bell → high shelf → first-order allpass (a small, always-positive fractional delay) → gain → soft saturation `tanh(k x) / k` → added noise (white noise, one-pole low-passed).
- **Deviations**: every deviation equals profile value × `drift` × tolerance × limit. The limits are in `limits::` in `src/dsp/DriftProfile.h`:
  - shelves ±1 dB, bell ±0.75 dB
  - delay 0..5 µs
  - gain ±1 dB
  - drive k ≤ 2.15
  - noise ≈ −84 dBFS RMS at the output
- **Drift Amount scales everything.** At `drift = 0` every voice is the exact identity (identity biquad coefficients, allpass a = 1, gain 1, saturation skipped, no noise). Each side is then the mean of identical voices, which is the input itself.
- **Voice weights**: the target is 2/N for v < N and 0 otherwise. Every voice ramps linearly over 50 ms on the same clock, so the weights of one side always sum to 1 and changing the channel count does not click. A voice whose weight is 0 is not processed, and it is reset before it fades back in.
- **Mono**: odd voices are skipped and even voices weigh 1/(N/2).
- **Tolerance profile**: `DriftProfile::generate(seed, version)` is pure and deterministic. It uses its own SplitMix64 with an explicit integer-to-float mapping, so the same seed gives the same sound on every OS. A new seed from Randomize crossfades between the old and new profile over 250 ms (`DriftProfile::lerp`).
- **Control rate**: coefficients are recomputed every 32 samples from smoothed values (50 ms smoothing for the amounts).
- **Realtime safety**: no allocations or locks on the audio thread, `ScopedNoDenormals`, parameters through `std::atomic<float>*`, atomic meters.

## Parameters (9)

| ID | Type | Range | Default |
|---|---|---|---|
| `channels` | choice | 2 / 4 / 8 / 16 voices | 4 |
| `drift` | float | 0..1 (shown as %) | 0.5 |
| `freqTol` | float | 0..1 | 0.5 |
| `phaseTol` | float | 0..1 | 0.5 |
| `gainTol` | float | 0..1 | 0.5 |
| `saturation` | float | 0..1 | 0.2 |
| `noise` | float | 0..1 | 0 |
| `outGain` | float | −24..+24 dB, step 0.1 | 0 |
| `bypass` | bool | | off |

- All IDs use `ParameterID{id, 1}`, and all parameters are automatable.
- **Randomize is not a parameter.** It is a UI action that changes the seed.
- Typed text ("50 %", "-3 dB") round-trips through `withValueFromStringFunction`.

## State and versioning

The state is the APVTS value tree as XML, with tag `DRIFTBOX_STATE`. It carries these properties:

- `stateVersion`: `kStateVersion`, currently 1
- `driftSeed`: 16 lowercase hex digits (always 16)
- `profileVersion`: `kProfileVersion`, currently 1 (the only profile generator version that exists so far)
- `uiWidth` and `uiHeight`

Loading and saving follow these rules:

- **Versions**: `setStateInformation` ignores a state whose `stateVersion` or `profileVersion` is newer than the plugin's. A missing version counts as 1. Older versions go through `migrateState()`.
- **Seed restore**: the seed is restored *without* a crossfade, so a reopened session sounds identical from the first sample. A missing or malformed `driftSeed` leaves the current seed unchanged.
- **Randomize and undo**: `randomize()` (message thread) draws a new seed and records it as one undo step named "Randomize" (`SeedChangeAction`, with a crossfade on undo and redo). Parameter gestures from the UI are recorded as one step each by `UndoRecorder`; host automation is not. Restoring a state clears the history.
- **Presets**: they never contain or overwrite `driftSeed` or `bypass`, because these are project-level values.

## Frozen C++ contracts (excerpts)

```cpp
// params/ParamIDs.h
namespace driftbox {
inline constexpr int kParamVersion = 1, kStateVersion = 1;
inline constexpr const char* kStateTag = "DRIFTBOX_STATE";
namespace pid { channels, drift, freqTol, phaseTol, gainTol, saturation, noise, outGain, bypass }
inline constexpr std::array<int, 4> kChannelChoices{2, 4, 8, 16};
}
// params/DriftboxParameters.h
inline constexpr int kNumParameters = 9;
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
struct ParamRefs { explicit ParamRefs(apvts&); std::atomic<float>* ...; EngineSettings snapshot() const noexcept; };
// dsp/DspTypes.h
inline constexpr int kMaxVoices = 16;
struct EngineSettings { int numVoices; float drift01, freqTol01, phaseTol01, gainTol01, saturation01, noise01,
                        outGainDb; bool bypass; };
// dsp/Prng.h       SplitMix64 { next(), nextUnipolar() [0,1), nextBipolar() [-1,1) }, Xorshift32
// dsp/DriftProfile.h
struct VoiceTolerance { lowShelfDb, lowShelfOct, highShelfDb, highShelfOct, bellDb, bellPos01, delay01, gainDb,
                        driveSpread, noiseLevel; };
struct DriftProfile { uint64_t seed; std::array<VoiceTolerance,16> voices; std::array<uint32_t,16> noiseSeeds;
                      static DriftProfile generate(uint64_t seed, int version = kProfileVersion) noexcept;
                      static DriftProfile lerp(const DriftProfile&, const DriftProfile&, float t) noexcept; };
// dsp/Biquad.h     BiquadCoeffs::{identity, lowShelf, highShelf, peak}(sr, hz, q, gainDb); Biquad (TDF-II)
// dsp/DriftChannel.h
struct VoiceAmounts { float freq, phase, gain, saturation, noise; };   // already x drift01
class DriftChannel { prepare(sr); reset(); setNoiseSeed(u32); update(tol, amounts, noiseScale);
                     processAdd(in, acc, n, w0, w1); processSample(x); };
// dsp/DriftEngine.h
class DriftEngine { prepare(sr, maxBlock, numCh); reset(); setSettings(EngineSettings); setSeed(seed, crossfade);
                    getSeed(); process(AudioBuffer<float>&); meters(); voiceWeight(v); };
// dsp/DspUtils.h   fastTanh, dbToGain, gainToDb, timeToCoeff, clampTo (from Pentagrit), sanitize, softCeiling
```

DSP module conventions (same as Pentagrit):

- `prepare()` may allocate, and is only called from `prepareToPlay`.
- `reset()`, `update()`, `setSettings()`, `setSeed()` and `process*()` are `noexcept` and allocation-free.

## Build notes

- `src/` sources are collected with a CMake glob (`CONFIGURE_DEPENDS`) into the `DriftboxCore` INTERFACE target, which the plugin and the tests share. A new file needs no CMake edit, but test files must be listed in `tests/CMakeLists.txt`.
- Formats: VST3 and CLAP on all platforms, plus AU on macOS. There is no Standalone.
