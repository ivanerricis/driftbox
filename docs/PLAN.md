# Driftbox — piano di implementazione

## Context
Nuovo plugin audio **Driftbox** (JUCE 8, C++20, CMake), separato da Pentagrit ma con la stessa qualità tecnica, la stessa organizzazione e un'UI coerente. Driftbox applica un "tolerance modeling" originale: N voci interne con piccole differenze pseudocasuali e stabili di risposta in frequenza, fase, guadagno, saturazione e rumore. Le differenze restano fisse finché l'utente non preme Randomize e vengono salvate nello stato del progetto. Repo pubblico `ivanerricis/driftbox`, AGPL-3.0-or-later.

Analisi di Pentagrit (commit `44c2fa9`) fatta su: CMake, preset, `cmake/`, processor/editor, `dsp/`, `params/`, `presets/`, `settings/`, `ui/`, `tests/`, docs, `scripts/ci`, `scripts/dev`, `installer/`, workflow GitHub, `cliff.toml`, `.clang-*`.

### Decisioni prese con l'utente
| Tema | Decisione |
|---|---|
| Mappatura stereo → voci | N/2 voci per lato; ogni voce riceve il segnale del suo lato; ogni uscita è la media pesata delle sue voci (guadagno unitario). Con 2 voci si ha la classica differenza L/R |
| Drift Amount | È il fattore principale: scala **tutto**, anche Saturation e Noise. A Drift = 0 il plugin è trasparente con qualunque impostazione |
| Randomize | Azione della UI, non un parametro. Cambia il seed, che viene salvato nello stato (non automatabile) |
| Artefatti | `Driftbox-vX.Y.Z-windows-setup.exe`, `-macos.pkg`, `-windows.zip`, `-macos.zip`, `-linux.tar.gz`, `SHA256SUMS.txt`. Gli script `scripts/ci/*` sono adattati da Pentagrit; `scripts/build-release.{ps1,sh}` e `scripts/create-release-artifacts.sh` sono wrapper locali sottili |
| Extra da Pentagrit | Impostazioni utente + zoom; preset di fabbrica + utente (versione ridotta); undo/redo. **Esclusi**: stili multipli e update checker |
| Standalone | **Nessuno**: il formato non viene compilato né distribuito. Setup Windows = VST3 e CLAP; pkg macOS = VST3, CLAP e AU |
| Repo | `C:\Users\Utente\Desktop\GitHub Repositories\driftbox`, accanto al clone aggiornato di Pentagrit; repo GitHub **pubblico** |
| Lingua | Commenti nel codice in italiano. README, CONTRIBUTING, docs e CHANGELOG in inglese come Pentagrit; `docs/PLAN.md` in italiano |

### Default stabiliti senza chiedere (dalle convenzioni di Pentagrit)
- Pin identici a Pentagrit:
  - JUCE `8.0.15`
  - clap-juce-extensions `7adee3a1bd4684d4caa5601100e364abccff4b4d`
  - Catch2 `v3.16.1`
  - CPM `v0.43.2`, vendorizzato con controllo SHA256 `49a3bef9…`
  - CMake ≥ 3.25 con `CMAKE_POLICY_VERSION_MINIMUM=3.5`
  - target macOS 11.0, universal arm64;x86_64
- Cache CPM condivisa `C:\dev\.cpm`, già popolata da Pentagrit. Non è impostata come variabile utente: la passo per comando, senza modificare l'ambiente.
- Ambiente locale verificato:
  - CMake 4.5.0-rc2
  - VS Build Tools 2026 e 2022 (preset `win-vs` = "Visual Studio 18 2026")
  - git e gh
  - Inno Setup 6 in `%LOCALAPPDATA%\Programs\Inno Setup 6`
  - clang-format 19 in VS 2022 BuildTools `…\VC\Tools\Llvm\bin` (la CI usa la versione 18)
  - Ninja non installato
- Codici plugin:
  - manufacturer `Ivnr`, plugin `Drbx`, `BUNDLE_ID com.ivanerricis.driftbox`
  - `CLAP_ID "com.ivanerricis.driftbox"`, feature `audio-effect mixing stereo` (verificate in `clap/plugin-features.h`)
  - categorie VST3 `Fx`
  - auval `aufx Drbx Ivnr`

### Skill e strumenti di Pentagrit (cosa esiste davvero)
Il repo Pentagrit **non contiene skill di progetto** (niente `.claude/`). Le "skill" usate sono quelle globali in `~/.claude/skills`:
- `ui-ux-pro-max` e `design-taste-frontend` per il mockup HTML;
- `logo-design` per logo e icona.

Il resto sono script e configurazioni, riusati e adattati:
- **Si riusano**:
  - `scripts/ci/{check_version.sh, package.sh, make_installer_windows.ps1, make_installer_macos.sh, get_pluginval.sh, get_clap_validator.sh, run_pluginval.sh}`
  - `scripts/dev/{setup-windows.ps1, run-pluginval.ps1}`
  - `.github/workflows/{build,ci,release}.yml`
  - `cliff.toml`, `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitattributes`, `.gitignore`
  - template di PR e issue, `dependabot.yml`, `CODEOWNERS`
  - `tests/TestSignals.h` e `tests/TestMain.cpp`
  - le funzioni `fastTanh`, `dbToGain`, `timeToCoeff` e `clampTo` di `src/dsp/DspUtils.h`
  - lo schema di stato e versioning e l'`UndoRecorder` di `PluginProcessor.cpp`
  - i pattern di `SettingsManager`, `PresetManager` e `Fonts` (cache con `DeletedAtShutdown`)
  - i font IBM Plex (OFL)
- **Adattamenti per Driftbox** (vanno documentati in `docs/RELEASING.md`):
  - nomi `Pentagrit` → `Driftbox` e regex `project(Driftbox VERSION …)`
  - **nuovo GUID `AppId`** Inno Setup
  - id dei pkg `com.ivanerricis.driftbox.{vst3,clap,au}`
  - suffissi degli artefatti senza `-x64`/`-universal`
  - nessuno Standalone, nessuno zip Linux
  - `HeaderFilterRegex` di `.clang-tidy`
  - `make_latest: false` sulle pre-release
- Copiare codice dal proprio repo Pentagrit (stessa licenza AGPL) è legittimo. Non si copia nulla di plugin commerciali: algoritmi originali e formule pubbliche (biquad RBJ cookbook, splitmix64, allpass del primo ordine).

---

## Architettura

```
processBlock (stereo in/out, mono ammesso come in Pentagrit)
  └─ DriftEngine
       ├─ seed atomico → DriftProfile::generate(seed)   (puro, deterministico, senza allocazioni)
       ├─ crossfade del profilo (~250 ms) al Randomize
       ├─ 16 DriftChannel preallocati; voce v → lato v & 1
       │     pesi per voce con rampa lineare (30 ms), Σ pesi per lato = 1 sempre
       ├─ uscita lato = Σ peso_v · voce_v(x_lato)
       ├─ Output Gain (smoothed 20 ms)
       ├─ SafetyCeiling (soft knee da −1 dBFS, asintoto a 0 dBFS, latenza 0)
       ├─ sanitizzazione NaN/Inf
       └─ bypass = crossfade 20 ms (latenza 0, nessun delay)
```
- Latenza 0, niente oversampling: il drive è basso e una tanh dolce genera poche armoniche. L'aliasing è trascurabile ed è documentato.
- Realtime safe come in Pentagrit:
  - `prepare()` alloca solo in `prepareToPlay`;
  - `process`, `reset` e `setSettings` sono `noexcept` e senza allocazioni;
  - `ScopedNoDenormals`, parametri via `std::atomic<float>*` (`ParamRefs::snapshot()`), meter atomici, chunking interno.

### Moduli DSP (`src/dsp/`)
| File | Contenuto |
|---|---|
| `DspTypes.h` | `enum class ChannelCount {two, four, eight, sixteen}`, `struct EngineSettings {numVoices, drift01, freqTol01, phaseTol01, gainTol01, saturation01, noise01, outGainDb, bypass}`, `kMaxVoices = 16` |
| `DspUtils.h` | Ripreso da Pentagrit (fastTanh, dbToGain, timeToCoeff, clampTo) più `sanitize(float)` |
| `Prng.h` | `SplitMix64` (seed → stream) con mappatura propria da intero a float in [-1, 1). **Non** si usa `std::*_distribution`, il cui output dipende dall'implementazione: lo stesso seed deve suonare uguale su Windows, macOS e Linux |
| `DriftProfile.h/.cpp` | `struct VoiceTolerance {lowShelfDb, lowShelfHzOct, highShelfDb, highShelfHzOct, bellDb, bellHzOct, phaseUs01, gainDb, driveSpread, noiseLevelDb}` normalizzati in [-1, 1] o [0, 1]; `struct DriftProfile {std::array<VoiceTolerance, 16>; uint64 seed; static DriftProfile generate(uint64 seed, int profileVersion = 1) noexcept; static lerp(a, b, t)}`; `kProfileVersion = 1` |
| `Biquad.h` | Biquad TDF-II float (coefficienti RBJ: low shelf, high shelf, bell); i coefficienti identità sono esatti a 0 dB |
| `DriftChannel.h/.cpp` | Una voce: `prepare(sr)`, `reset()`, `setTargets(const VoiceTolerance&, scaled amounts)` a control rate, `processSample` inline o `process(span)` |
| `DriftEngine.h/.cpp` | Multi-voce, pesi, crossfade del profilo, output stage, bypass, meter |
| `Meters.h` | Picco in ingresso e in uscita, `cpuLoad`, `std::array<atomic<float>,16> voiceGainDb` per il display |

### Catena per voce (DriftChannel)
Ogni deviazione vale: **valore_profilo × Drift × Tolleranza × massimo**. Le deviazioni sono *a* = Drift × Tol e si ricalcolano a control rate (ogni 32 campioni) da valori smoothed. Sono piccolissime, quindi non producono zipper.

| Stadio | Implementazione | Massimo a 100% / 100% |
|---|---|---|
| Frequenza | low shelf (≈ 150 Hz ± ½ ottava), bell Q 0.7 (log-random 400 Hz – 3 kHz), high shelf (≈ 8 kHz ± ½ ottava) | shelf ±1.0 dB, bell ±0.75 dB |
| Fase | allpass del primo ordine `a = (1−d)/(1+d)`, con d = τ·fs e τ ∈ [0, τmax]. È un piccolo ritardo frazionario, **sempre positivo**: nessuna inversione e nessuna cancellazione. Con d = 0 vale a = 1, cioè identità esatta | τmax = 5 µs; differenza massima tra due voci 10 µs, cioè −0.44 dB a 10 kHz sulla somma |
| Guadagno | lineare, smoothed | ±1.0 dB (nel range richiesto 0.1–1 dB) |
| Saturazione | `y = tanh(k·x)/k` con `k = 1 + Sat·Drift·(1 + 0.15·spread_v)`; guadagno unitario per piccoli segnali; saltata quando k = 1 | k ≤ 2.15 (drive massimo circa +6.6 dB, molto dolce) |
| Rumore | xorshift per voce (seed derivato), bianco filtrato da un passa-basso one-pole a 12 kHz, livello per voce ±1.5 dB dal profilo, scalato per √(N/2) così il rumore in uscita non dipende da N | circa −84 dBFS RMS in uscita |

- **Trasparenza a Drift = 0**: biquad identità, a = 1, guadagno 1, saturazione saltata, rumore 0. Ogni lato è la media di voci identiche, cioè l'ingresso (errore solo di arrotondamento float).
- Più voci producono una media più fitta e quindi una deviazione netta più piccola: 2 voci danno il massimo carattere L/R, 16 un "ensemble" più morbido. Il comportamento va documentato nel README.

### Cambio canali senza click
- Le 16 voci sono preallocate in `prepare()`. Il peso target è 2/N per le voci v < N e 0 per le altre.
- Rampa lineare di 30 ms sullo stesso orologio per tutte le voci, quindi Σ pesi per lato resta 1 anche durante la rampa. Se il cambio arriva a rampa in corso, si riparte dai pesi correnti.
- Una voce con peso esattamente 0 non viene elaborata. Quando riparte, `reset()` azzera lo stato e la rampa da 0 maschera il piccolo transitorio iniziale (i coefficienti sono vicini all'identità).
- I valori dei 4 canali (2/4/8/16) mantengono le voci basse comuni, quindi il passaggio 4 → 8 aggiunge solo le voci 4..7.
- `channels` resta **automatabile** perché il cambio è senza click e senza variazione di latenza. Diversamente da `numBands` di Pentagrit, che invece cambia la latenza.

### Randomize e stato delle tolleranze
- Il seed `uint64` viene generato al primo avvio dell'istanza (`juce::Random::getSystemRandom()`).
- `processor.randomize()` (message thread) crea un nuovo seed, lo pubblica in un `std::atomic<uint64_t>` e lo registra come passo di undo (`SeedChangeAction`). Il seed non è un parametro, quindi serve un'azione dedicata nell'`UndoManager` di Pentagrit.
- L'audio thread vede il seed cambiato, genera il profilo target (operazione economica, su `std::array`) e fa un crossfade in ~250 ms tra i valori del profilo vecchio e nuovo, così non ci sono click.
- Stato: tag `DRIFTBOX_STATE`, che contiene:
  - l'albero APVTS
  - `stateVersion = 1`
  - `driftSeed` (stringa esadecimale a 16 cifre)
  - `profileVersion = 1`
  - `uiWidth` e `uiHeight`
- `setStateInformation`: rifiuta versioni future, `migrateState()` come in Pentagrit, ripristina il seed **senza crossfade** (la sessione riaperta suona identica fin dal primo campione) e svuota la cronologia undo.
- I preset **non** contengono né sovrascrivono `driftSeed` e `bypass`: valgono a livello di progetto, come `bypass` e `osPhase` in Pentagrit.
- Il determinismo cross-platform è garantito da un test con valori golden del profilo per un seed fisso.

### Parametri (`src/params/ParamIDs.h` + `DriftboxParameters.h/.cpp`; tutti `ParameterID{id, 1}`, ID immutabili)
| ID | Tipo | Range | Default |
|---|---|---|---|
| `channels` | Choice | 2 / 4 / 8 / 16 | 4 |
| `drift` | Float % | 0–100 | 50 |
| `freqTol` | Float % | 0–100 | 50 |
| `phaseTol` | Float % | 0–100 | 50 |
| `gainTol` | Float % | 0–100 | 50 |
| `saturation` | Float % | 0–100 | 20 |
| `noise` | Float % | 0–100 | 0 |
| `outGain` | Float dB | −24…+24, passo 0.1 | 0 |
| `bypass` | Bool | — | off (`getBypassParameter()` override) |

- `kNumParameters = 9`.
- Testo digitato parsato con `withValueFromStringFunction` ("50 %", "-3 dB"), come in Pentagrit.
- Struttura `ParamRefs` con `snapshot()` → `EngineSettings`.

---

## Struttura dei file
Struttura richiesta, più ciò che serve per allinearsi a Pentagrit:
```
driftbox/
  CMakeLists.txt  CMakePresets.json  PluginInfo.h.in  cliff.toml  LICENSE (AGPL-3.0)
  README.md  CHANGELOG.md  CONTRIBUTING.md  THIRD_PARTY_NOTICES.md
  .clang-format .clang-tidy .editorconfig .gitignore .gitattributes
  cmake/{CPM.cmake, Dependencies.cmake, CompilerSettings.cmake}
  resources/{brand/{logo.svg,icon.svg}, ui/fonts/IBMPlex*.ttf + licenses/, presets/*.xml}
  scripts/build-release.ps1  scripts/build-release.sh  scripts/create-release-artifacts.sh
  scripts/ci/{check_version.sh, package.sh, make_installer_windows.ps1, make_installer_macos.sh,
              get_pluginval.sh, get_clap_validator.sh, run_pluginval.sh}
  scripts/dev/{setup-windows.ps1, run-pluginval.ps1}
  installer/windows/driftbox.iss   installer/macos/distribution.xml.in
  .github/{workflows/{build,ci,release}.yml, ISSUE_TEMPLATE/*, PULL_REQUEST_TEMPLATE.md, CODEOWNERS, dependabot.yml}
  docs/{PLAN.md, ARCHITECTURE.md, DEVELOPMENT.md, RELEASING.md, MACOS_SIGNING.md, BRANCH_PROTECTION.md, design/}
  src/PluginEntry.cpp  PluginProcessor.{h,cpp}  PluginEditor.{h,cpp}
  src/dsp/{DspTypes.h, DspUtils.h, Prng.h, Biquad.h, Meters.h, DriftProfile.*, DriftChannel.*, DriftEngine.*}
  src/params/{ParamIDs.h, DriftboxParameters.*}
  src/presets/{PresetManager.*, FactoryPresets.*}
  src/settings/SettingsManager.*
  src/ui/{Theme.h, DriftboxLookAndFeel.*, Fonts.*, Widgets.*, TopBar.*, PresetBar.*, ChannelSelector.*,
          VoiceDisplay.*, SettingsPanel.*, Footer.*}
  tests/{CMakeLists.txt, TestMain.cpp, TestSignals.h, DriftboxTests.cpp, ProfileTests.cpp, ChannelTests.cpp,
         EngineTests.cpp, ParameterTests.cpp, StateTests.cpp, AllocationTests.cpp, EditorTests.cpp,
         PresetTests.cpp, SettingsTests.cpp, UndoTests.cpp, PerfTests.cpp}
```
- CMake è identico nello schema a Pentagrit:
  - glob `src/*.cpp` (escluso `PluginEntry.cpp`) in `DriftboxCore` INTERFACE, condiviso da plugin e test;
  - `juce_add_binary_data` per font e preset;
  - `DriftboxCompilerSettings`;
  - `juce_add_plugin` con FORMATS `VST3` (+`AU` su Apple), **senza Standalone**, più `clap_juce_extensions_plugin`;
  - opzione `DRIFTBOX_BUILD_TESTS`.
- Preset CMake con gli stessi nomi di Pentagrit: `win-vs`, `win-ninja`, `mac-universal`, `linux`, `ci-windows`, `ci-macos`, `ci-linux`, `ci-linux-asan`, più i build e test preset corrispondenti.
- `DriftboxTests.cpp` (nome richiesto) contiene i 5 test di accettazione richiesti. Gli altri file seguono la suddivisione per modulo di Pentagrit.

## UI
- Il mockup HTML viene fatto prima, con le skill `ui-ux-pro-max` e `design-taste-frontend`, e pubblicato come Artifact per la tua approvazione. Logo e icona con `logo-design`. Stesso flusso di Pentagrit (WP5a).
- Un solo tema scuro: grafite, un accento originale (non la palette di Pentagrit) e IBM Plex Sans/Mono. Il rapporto d'aspetto è fisso (base circa 720 × 440) e lo zoom va dal 75 al 200%, salvato nelle impostazioni.
- Layout:
  - **Top bar**: logo, preset bar (◀ ▶, menu Save / Save As / Delete), undo/redo, Randomize, Bypass, ingranaggio.
  - **Centro**: selettore segmentato dei canali 2 / 4 / 8 / 16 e **VoiceDisplay**, una barra per voce attiva che mostra la deviazione di guadagno e di tono. Viene ridisegnato solo quando cambia il profilo o un parametro (timer 30 Hz con dirty flag).
  - **Riga di manopole**: Drift (grande), Freq, Phase, Gain, Saturation, Noise, Output.
  - **Footer**: picco in uscita, CPU, versione.
- Interazioni riprese da Pentagrit:
  - doppio clic per digitare un valore, Ctrl-clic per il reset, tooltip;
  - Ctrl/Cmd+Z per annullare e Ctrl/Cmd+Shift+Z per ripetere;
  - attachment JUCE con gesture.

## Preset e impostazioni (versioni ridotte)
- **PresetManager** (solo message thread):
  - factory read-only incorporati più file utente `.dbp` (XML + `stateVersion`) in `%APPDATA%/Driftbox/Presets`, con le cartelle equivalenti su macOS e Linux;
  - operazioni: Save, Save As, Delete, prev/next, indicatore "modificato";
  - niente A/B, categorie, import o export;
  - escludono `driftSeed` e `bypass`.
- **Preset di fabbrica**, con nomi originali e nessun riferimento a hardware: Init (Drift 0), Subtle Spread, Wide Pair (2 canali), Ensemble 16, Warm Stack, Gentle Grit, Noisy Desk.
- **SettingsManager** (`Driftbox/settings.settings`, condiviso tra le istanze, tollerante a file mancanti o corrotti): zoom, tooltip e cartella dei preset utente.

## Test (Catch2, `ctest`)
Test di accettazione richiesti:
1. **Trasparenza**: Drift = 0 con tutti gli altri parametri al massimo, a 44.1/48/96/192 kHz e per 2/4/8/16 canali. Rumore e sinusoidi in ingresso; max |y − x| < 1e−5 (−100 dB).
2. **Niente NaN/Inf**:
   - fuzz casuale di parametri e block size (1..4096, anche dispari, block size 0);
   - ingresso con NaN, Inf e valori 1e30: l'uscita è sempre finita e il motore si riprende al blocco successivo;
   - denormal.
3. **Cambio canali senza click**: sinusoide da 1 kHz a −6 dBFS e passaggi 2 → 16 → 4 → 8 → 2, anche durante una rampa. Il Δ massimo tra campioni consecutivi è ≤ 1.05 × il Δ a regime + 1e−4; nessun salto di livello RMS > 0.1 dB nelle finestre di 5 ms. Stesso controllo su Randomize.
4. **Stato**:
   - round trip di tutti i parametri, del seed e della dimensione UI;
   - due istanze con lo stesso stato producono **un'uscita identica bit per bit** (anche il rumore è seminato dal seed);
   - uno stato garbage viene ignorato; una versione futura viene rifiutata; uno stato senza versione vale come versione 1.
5. **Niente clipping**: sinusoide e rumore a −1 dBFS con tutto al massimo e Output +24 dB danno picco ≤ 1.0. Un segnale a −6 dBFS con Output 0 non tocca il ceiling (|y| < 0.891).

Test aggiuntivi in stile Pentagrit:
- profilo deterministico con valori golden, range, seed diversi → profili diversi;
- voce: identità, guadagno entro ±1 dB, ritardo di fase entro τmax;
- risposta sommata entro ±0.5 dB fino a 10 kHz al 100%;
- parametri: numero, ID, range, parsing del testo;
- zero allocazioni in `processBlock` (override globale di `new`, tag `[allocation]`);
- editor headless: creazione, distruzione, zoom;
- undo di parametri e di Randomize;
- preset e impostazioni con round trip e file corrotti;
- performance: 16 voci a 48 kHz sotto ~2% di un core per istanza, con soglia larga in CI.

## CI/CD e release
- **ci.yml** (come Pentagrit):
  - titolo della PR in Conventional Commit, clang-format 18, clang-tidy con `--warnings-as-errors`, asan-ubsan;
  - build su Windows e Linux sempre, macOS su `main` e con la label `ci:macos`.
- **build.yml** riusabile:
  - matrix win/mac/linux, sccache/ccache, cache CPM;
  - ctest, pluginval a strictness 10, auval `aufx Drbx Ivnr`, clap-validator (advisory);
  - `package.sh`, poi gli installer solo con `installers: true`;
  - azioni pinnate per SHA, come in Pentagrit.
- **release.yml**:
  - un tag `v*` avvia `check_version.sh`, la build sui 3 OS con gli installer e `create-release-artifacts.sh` (SHA256SUMS.txt su tutti gli artefatti);
  - note generate da git-cliff;
  - `softprops/action-gh-release` con `prerelease: contains(tag,'-')` e `make_latest: false` per le pre-release.
- **Artefatti**:
  - `windows.zip`: VST3 e CLAP con LICENSE e README.txt;
  - `macos.zip`: VST3, CLAP e AU con firma ad-hoc;
  - `linux.tar.gz`: VST3 e CLAP;
  - setup Windows (Inno Setup, admin, componenti VST3/CLAP in `{commoncf64}\VST3` e `\CLAP`);
  - pkg macOS (VST3, CLAP e AU in `/Library/Audio/Plug-Ins/...`, non firmato, procedura per Gatekeeper nel README).
- **Wrapper locali**:
  - `build-release.ps1` (Windows): preset `win-vs`, build, test, poi `package.sh` via Git Bash e `make_installer_windows.ps1`;
  - `build-release.sh` (macOS/Linux): preset, build, test, package e, su macOS, il pkg;
  - `create-release-artifacts.sh <dir>`: verifica i nomi attesi e scrive `SHA256SUMS.txt`.
- Conventional Commits, branch `feat/` `fix/` `ci/` `docs/`, PR in squash, CHANGELOG da git-cliff, **nessun trailer di attribuzione**.
- Il percorso di versione segue Pentagrit: `v0.1.0-rc1` (pre-release) e poi `v0.1.0`. Il tag deve coincidere con la versione in CMake.

---

## Esecuzione: task sequenziali con agenti Sonnet 5.5
Regole per ogni task:
- Un agente Sonnet 5.5 alla volta, nel proprio git worktree (`C:\dev\db-tN`, per percorsi corti) e su un branch dedicato. Apre una PR con titolo Conventional Commit.
- Prima di dichiarare il task finito l'agente deve:
  - eseguire `cmake --preset win-vs`, `cmake --build --preset win-vs-release` e `ctest --preset win-vs-release` (con `CPM_SOURCE_CACHE=C:\dev\.cpm`);
  - applicare clang-format;
  - riassumere le modifiche.
- Se incontra un'ambiguità si ferma e chiede invece di improvvisare.
- Io rivedo il diff e la CI, e faccio il merge in squash solo con CI verde. Il task successivo parte solo dopo.

| # | Task | File | Criteri di accettazione |
|---|---|---|---|
| T0 | Scaffold (lo faccio io) | CMake, preset, `cmake/*`, config lint, LICENSE, `PluginInfo.h.in`, `ParamIDs.h` e `DriftboxParameters.*` completi, header DSP con API congelate e stub, processor ed editor stub, `tests/` con TestMain e TestSignals, `docs/PLAN.md` e `ARCHITECTURE.md`; `gh repo create ivanerricis/driftbox --public` | Configura e compila VST3/CLAP su Windows; ParameterTests verdi |
| T1 | DriftProfile + Prng | `dsp/Prng.h`, `dsp/DriftProfile.*`, `ProfileTests` | Valori golden, range, determinismo |
| T2 | DriftChannel + Biquad | `dsp/Biquad.h`, `dsp/DriftChannel.*`, `ChannelTests` | Identità a 0, limiti di guadagno e fase, niente NaN |
| T3 | DriftEngine | `dsp/DriftEngine.*`, `Meters.h`, `EngineTests`, i 5 test in `DriftboxTests.cpp` | Trasparenza, NaN/Inf, cambio canali, Randomize senza click, niente clipping |
| T4 | Integrazione nel processor | `PluginProcessor.*` (seed, randomize, stato, UndoRecorder + SeedChangeAction), `StateTests`, `AllocationTests`, `UndoTests`, `PerfTests` | Round trip bit-identico; zero allocazioni; pluginval 10 in locale |
| T5 | CI/CD | `.github/*`, `scripts/ci/*` adattati, `scripts/dev/*` | CI verde sui 3 OS (label `ci:macos`), clang-tidy e asan verdi |
| T6 | Mockup + brand (skill) | `docs/design/*`, `resources/brand/*`; Artifact | **Tua approvazione** del mockup |
| T7 | UI JUCE | `PluginEditor.*`, `ui/*`, font, `EditorTests` | Editor headless verde; screenshot da te in un DAW |
| T8 | Preset + impostazioni | `presets/*`, `settings/*`, `ui/PresetBar`, `ui/SettingsPanel`, `resources/presets/*`, i relativi test | Round trip, file corrotti, preset di fabbrica tutti caricabili |
| T9 | Release e packaging | `installer/*`, `make_installer_*`, `package.sh`, i 3 wrapper, `release.yml` | Generazione locale di `windows.zip`, setup.exe e SHA256SUMS; pkg e zip macOS e tar.gz Linux su CI (workflow_dispatch) |
| T10 | Documentazione | README (descrizione, TMT spiegata in modo semplice, build, test nel DAW, parametri, tolleranze, installazione, SHA256, Gatekeeper, licenza), CONTRIBUTING, CHANGELOG, THIRD_PARTY_NOTICES, `docs/*` | Revisione |
| T11 | Integrazione finale | — | pluginval 10 e auval; checklist manuale nel DAW (tua); tag `v0.1.0-rc1` **solo dopo il tuo ok** |

Revisione del codice per ogni PR:
- niente allocazioni o lock nell'audio thread;
- `noexcept` dove previsto dal contratto;
- ID dei parametri invariati;
- commenti in italiano;
- test aggiunti;
- nessun riferimento o asset di plugin commerciali.

## Rischi e soluzioni
1. **Somma delle voci con cancellazioni di fase**: ritardi sempre positivi e limitati (τmax 5 µs); test sulla risposta sommata entro ±0.5 dB fino a 10 kHz.
2. **Determinismo cross-platform del seed**: PRNG proprio con mappatura esplicita, `profileVersion`, test con valori golden.
3. **Click al cambio canali o al Randomize**: rampe con somma costante, reset delle voci solo a peso 0, crossfade del profilo; test sul Δ campione per campione.
4. **Ceiling e trasparenza**: il knee parte da −1 dBFS, quindi sotto quella soglia il segnale resta invariato; è documentato che con Output alto il ceiling lavora.
5. **CPU con molte istanze**: voci a peso 0 saltate, coefficienti a control rate, `fastTanh`, saturazione saltata a k = 1; il PerfTest fa da guardia.
6. **Percorsi lunghi su Windows** (repo nella cartella Desktop): build in `build/<preset>` e cache CPM in `C:\dev\.cpm`; worktree degli agenti in `C:\dev\db-tN`.
7. **pluginval 10**: fuzz su `channels` e bypass, block size e sample rate variabili, già coperti dai test.
8. **AU, Gatekeeper e firma**: firma ad-hoc e pkg non firmato come in Pentagrit; `docs/MACOS_SIGNING.md` copiato e adattato.
9. **AGPL**: il repo pubblico soddisfa l'obbligo di rendere disponibile il sorgente.
10. **Marchio "Driftbox"**: da verificare prima della release stabile (da te).

## Verifica end-to-end
- Locale (Windows):
  ```
  cmake --preset win-vs
  cmake --build --preset win-vs-release
  ctest --preset win-vs-release
  ```
  Poi `scripts/dev/run-pluginval.ps1 -Vst3 build\win-vs\Driftbox_artefacts\Release\VST3\Driftbox.vst3`.
- Release locale: `scripts/build-release.ps1 -Version v0.1.0-rc1` produce in `dist/` lo zip, il setup.exe e `SHA256SUMS.txt`; poi `sha256sum -c`.
- CI: run completo sui 3 OS con pluginval, auval e clap-validator; `workflow_dispatch` della release in modalità draft per controllare gli artefatti macOS e Linux.
- Manuale (tu): Reaper con VST3 e CLAP. Da controllare: trasparenza a Drift 0 (null test contro bypass), cambio canali durante il playback, Randomize, salvataggio e riapertura del progetto con suono identico, automazione, CPU con molte istanze.

## Primi passi dopo l'approvazione
1. T0: lo eseguo io. Creo il repo pubblico `ivanerricis/driftbox`, faccio il commit dello scaffold e copio questo piano in `docs/PLAN.md`.
2. Poi T1 → T11 in sequenza con agenti Sonnet 5.5, verificando ogni task prima di passare al successivo.
