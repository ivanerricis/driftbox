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
| `chanL` | int | 1..64 ("Channel L") | random per new instance |
| `chanR` | int | 1..64 ("Channel R") | L % 64 + 1 (the instance's pair) |
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
- `presetName`: the name of the preset last loaded or saved (absent means no preset is loaded; the bar then shows "Default"; not a parameter, no undo step)

The channels are ordinary parameters, so they are saved with the parameter tree. Loading and saving follow these rules:

- **Versions**: `setStateInformation` ignores a state whose `stateVersion` or `profileVersion` is newer than the plugin's. A missing version counts as 1. Older versions go through `migrateState()`.
- **Reopening a session**: the restored `chanL`/`chanR` are read at the first block after `prepareToPlay`, so there is no fade and a reopened session sounds bit-identical from the first sample. A state restored during playback fades like any automation.
- **New instances**: the constructor draws a random channel pair (L in 1..64, R = L % 64 + 1) BEFORE building the parameter layout and passes it as the default of `chanL`/`chanR`, so the initial value equals the default (CLAP requires it, and clap-validator checks it) and reset / Ctrl-click return to that instance's pair. Two new instances do not sound identical by chance. Tests inject a fixed pair with `DriftboxAudioProcessor(ChannelPair)`. A restored state overwrites everything, and `setStateInformation()` tells the host that values changed (`updateHostDisplay` with `programChanged`, which the CLAP wrapper turns into `params.rescan(VALUES)`; clap-validator requires it).
- **Randomize and undo**: `randomize()` (message thread) picks L in 1..64 and R = L % 64 + 1, different from the current pair, and applies both as ONE undo step named "Randomize" (a transaction with nested gestures on both parameters). Parameter gestures from the UI are recorded as one step each by `UndoRecorder`; host automation is not. Restoring a state clears the history. The constructor leaves no step in the history.
- **Legacy**: a `driftSeed` property from development builds is dropped on load.
- **Presets**: they store and restore only the seven parameters listed in [Presets and settings](#presets-and-settings). They never contain or overwrite `chanL`, `chanR`, `bypass`, `advancedOpen`, `uiWidth` or `uiHeight`.

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
  `Widgets` (knobs, channel selector, buttons, stereo meter), `TopBar`, `PresetBar`, `Panels`
  (channel, tolerance graph, drift, advanced, output), `Footer`, and the overlays `OverlayMenu`, `ModalOverlay`,
  `SettingsPanel` and `PresetDialog` (see below).
- A 30 Hz timer reads the parameters; the tolerance graph recomputes and repaints only when its inputs change.
  The curve comes from `toleranceDeviationDb()` (`src/dsp/ToleranceResponse.h`), which uses
  `DriftChannel::design()`, the same design values the engine applies.
- The UI only reads atomics (`MeterState`, parameters) and never runs in the audio path. `MeterState` has per-side
  output peaks (`outPeakDbL`, `outPeakDbR`) for the stereo meter.
- Hooks: `PresetBar::onPrev/onNext/onOpenList/onOpenActions`, `setPresetName()` and `setModified()`,
  `DriftboxAudioProcessorEditor::onSettings` (notification; the settings modal opens anyway), `showCpu`,
  `setZoom()`, `setTooltipsEnabled()`, `content()`.

## Presets and settings

### Presets

- Everything lives in `PresetManager` (`src/presets/`), owned by the processor, message thread only. A preset stores
  and restores **only** these seven parameters, in this order: `drift`, `freqTol`, `phaseTol`, `gainTol`,
  `saturation`, `noise`, `outGain` (`PresetValues`). `chanL`, `chanR`, `bypass` and the non-parameter state
  properties are never stored or changed.
- Loading never replaces the state tree. `DriftboxAudioProcessor::applyPresetValues()` opens one undo transaction
  named after the preset and sets the seven parameters one by one inside nested gestures, exactly like
  `randomize()` (the `UndoRecorder` does not rename the transaction while `groupedEdit_` is set). Undo restores the
  values; the preset name in the bar is not undone, so the bar then shows the modified dot.
- **Factory presets** are embedded from `resources/presets/*.xml` (`juce_add_binary_data` target
  `DriftboxPresetData`), menu order = file name order. They are read-only.
- **User presets** are `.dbp` files in the user folder (default `%APPDATA%/Driftbox/Presets` on Windows,
  `~/Library/Application Support/Driftbox/Presets` on macOS, `~/.config/Driftbox/Presets` on Linux; overridable by
  the "User preset folder" setting). The file name is the preset name. Format:
  `<DRIFTBOX_PRESET version="1" name="..."><PARAM id="drift" value="0.62"/> ... </DRIFTBOX_PRESET>`, values
  denormalised (unit parameters 0..1, `outGain` in dB). Missing parameters take their default, out-of-range values are
  clamped, ids other than the seven are ignored. A file that is not valid XML, has another root tag, a non-numeric
  value or a `version` newer than the plugin's is skipped without error (`getSkippedFiles()`), as is a file named
  like a factory preset. Files are written through a temporary file. User preset names cannot equal a factory name
  (case-insensitive), so the name in the state is unambiguous.
- Navigation (prev/next, wrapping) walks the factory list then the user list (sorted by name). "Modified" is
  computed on demand by comparing the current values with the loaded preset looked up by name (tolerance 0.05 % for
  unit parameters and 0.05 dB for `outGain`); there are no listeners, so host automation threads are not involved.
  The library is scanned when an editor opens, when a menu or dialog opens and on prev/next, never in the
  processor constructor.
- A state without `presetName` (a new instance, or a project saved before presets existed) shows "Default" in the bar,
  is never "modified" and has no loaded preset: next goes to the first preset (Init), previous to the last one. Deleting the
  current user preset loads Init.

Factory preset values (denormalised; `outGain` is 0 dB in all of them):

| Preset | Drift | Freq tol | Phase tol | Gain tol | Saturation | Noise |
|---|---|---|---|---|---|---|
| Init | 0 % | 50 % | 50 % | 50 % | 20 % | 0 % |
| Subtle Spread | 62 % | 50 % | 50 % | 50 % | 20 % | 0 % |
| Wide Pair | 85 % | 70 % | 90 % | 60 % | 10 % | 0 % |
| Warm Console | 70 % | 60 % | 40 % | 50 % | 55 % | 15 % |
| Gentle Grit | 55 % | 30 % | 30 % | 40 % | 85 % | 10 % |
| Noisy Desk | 80 % | 50 % | 50 % | 50 % | 30 % | 70 % |

Init is the plugin defaults with Drift at 0 % (transparent). The others are the values of the approved mockup.

### Settings

- `SettingsManager` (`src/settings/`) keeps the per-user settings in a `juce::PropertiesFile`
  (`<app data>/Driftbox/settings.settings`, `~/Library/Application Support/Driftbox/` on macOS). One instance is
  shared by all plugin instances of the process (`juce::SharedResourcePointer`), the file is protected by an
  inter-process lock, and a missing or unreadable file gives the defaults (the file is created on the first change, not by
  reading). Message thread only; it broadcasts a synchronous change message.
- Settings: `uiZoom` (75/100/125/150/200 %, default 100), `tooltips` (default on), `showCpu` (default on),
  `presetFolder` (empty = platform default), `checkUpdates` (default on). They never enter the project state or a preset.
- Zoom: a change resizes every open editor (`setZoom()`); a new editor uses it only when the processor state has no
  saved size (`hasStoredUiSize()`), otherwise the saved size of the project wins. Other changes (tooltips, CPU) do
  not resize.
- The tests redirect the settings file and the default preset folder to a temporary folder
  (`SettingsManager::setTestOverrides`, called from `tests/TestMain.cpp`).

### Overlays

The settings modal, the preset list, the actions menu and the Save As / Delete dialog are components inside
`content()` (they scale with the editor and can be snapshotted), not native windows: no modal loops, and a click or
key never reaches the controls below. They are hidden at once and destroyed on the next message-loop turn. The
folder chooser is the only native dialog (`FileChooser::launchAsync`). The "Updates" row of the settings
modal is `SettingsPanel::updatesSection()` (see "Update check" below).

## Update check

- **What is sent**: one plain `GET https://api.github.com/repos/ivanerricis/driftbox/releases` with a single header,
  `User-Agent: Driftbox/<plugin version>`. No query string, no body, no cookies, and no user name, e-mail, machine id or
  project data. The plugin version is public information. GitHub sees the usual network metadata (the IP address).
- **What it does**: the response is the release list. Drafts are skipped, pre-releases are included, and the highest
  version by semver wins (`0.1.0-rc1 < 0.1.0-rc2 < 0.1.0`; the numeric tail of an `rcN` identifier is compared as a
  number, so `rc2 < rc10`). Tags that are not valid versions and links outside `https://github.com/ivanerricis/driftbox/`
  are ignored. If the winner is newer than the running version, the settings row says "Version X available" and shows a
  "Download" link that opens the release page in the default browser. The plugin never downloads or installs anything.
- **How often**: it is on by default. An editor checks when it opens, at most once every 24 hours: the time of the last
  automatic check is stored in the settings file (`lastUpdateCheck`), so all instances and processes share it. A failed
  check also counts, so a broken network does not cause a retry on every editor open. "Check now" always checks and
  does not change that schedule.
- **How to disable it**: Settings, Updates, switch off "Check for updates" (stored as `checkUpdates=false`; "Reset
  settings" turns it back on). With the switch off nothing is sent unless you press "Check now".
- **Platforms**: Windows and macOS use JUCE's native HTTP client. Linux builds have no HTTP backend, so the row is shown
  disabled with "Not available on Linux".
- **Threads**: the request runs on a background `juce::Thread` with a 5 s connection timeout, never on the audio thread
  and never on the message thread. The result is posted back with `MessageManager::callAsync`, guarded by a
  `WeakReference` to the checker (a result arriving after destruction is dropped). The checker lives in the shared
  `SettingsManager`, which broadcasts `updateChanges()`; the settings panel listens to it and removes itself on destruction.
- **Tests** inject the fetch function (`SettingsManager::setUpdateFetcherOverride`) and the clock
  (`setClockOverride`); `tests/TestMain.cpp` installs a fetcher that fails immediately, so no test opens a connection.

## Build notes

- `src/` sources are collected with a CMake glob (`CONFIGURE_DEPENDS`) into the `DriftboxCore` INTERFACE target, which the plugin and the tests share. A new file needs no CMake edit, but test files must be listed in `tests/CMakeLists.txt`.
- Formats: VST3 and CLAP on all platforms, plus AU on macOS. There is no Standalone.
