# Driftbox — piano di implementazione

## Revisione 2026-11-12: selettore di canale L/R

**Decisione del maintainer** (presa dopo che DSP, motore, processor e CI erano già fatti): le voci interne 2/4/8/16 (mediate per lato) e il seed nascosto sono SOSTITUITI da un **numero di canale per lato**: L e R, ognuno da 1 a 64, come una striscia di un mixer virtuale.
- Ogni numero di canale corrisponde a un set di tolleranze FISSO e deterministico, identico su ogni piattaforma (`channelProfile(canale)`, formato congelato v1).
- Una sola catena DSP (`DriftChannel`) per lato; in mono si elabora solo L.
- Randomize sceglie una coppia di canali a caso (L casuale, R = L % 64 + 1, diversa dalla corrente) ed è un solo passo di undo.
- Tutto è visibile, salvato e automatabile come parametro: `chanL` e `chanR` (interi 1..64, default 1 e 2). Il parametro `channels` e il seed (`driftSeed`) non esistono più; i parametri diventano 10.
- Cambio di canale: crossfade di 250 ms delle tolleranze del lato, senza click; lati indipendenti.
- Una nuova istanza parte con una coppia casuale (le tolleranze sono generate all'avvio della sessione); lo stato ripristinato sovrascrive tutto e, letto prima del primo blocco, suona identico dal primo campione.
- UI (solo documentazione, il codice dell'editor non cambia): i controlli Freq/Phase/Gain/Saturation/Noise finiranno in un pannello "Advanced" apribile; restano in vista L/R, Drift, Output, Randomize e Bypass. I parametri restano tutti invariati.

Le sezioni qui sotto che parlano di voci 2/4/8/16, pesi, seed e `SeedChangeAction` sono **superate** da questa revisione dove in contrasto: fanno fede questa sezione e `docs/ARCHITECTURE.md`.

---

## Context
Nuovo plugin audio **Driftbox** (JUCE 8, C++20, CMake), separato da Pentagrit ma con la stessa qualità tecnica, la stessa organizzazione e un'UI coerente. Driftbox applica un "tolerance modeling" originale: un canale (1..64) per lato con piccole differenze pseudocasuali e stabili di risposta in frequenza, fase, guadagno, saturazione e rumore. Le differenze sono fisse per ogni numero di canale, restano finché l'utente non cambia canale o preme Randomize e vengono salvate nello stato del progetto (i parametri `chanL` e `chanR`). Repo pubblico `ivanerricis/driftbox`, AGPL-3.0-or-later.

Analisi di Pentagrit (commit `44c2fa9`) fatta su: CMake, preset, `cmake/`, processor/editor, `dsp/`, `params/`, `presets/`, `settings/`, `ui/`, `tests/`, docs, `scripts/ci`, `scripts/dev`, `installer/`, workflow GitHub, `cliff.toml`, `.clang-*`.

### Decisioni prese con l'utente
| Tema | Decisione |
|---|---|
| Mappatura stereo → canali | **Superata dalla revisione 2026-11-12.** Una catena per lato (canale 1..64 per L e per R); le voci 2/4/8/16 mediate per lato non esistono più |
| Drift Amount | È il fattore principale: scala **tutto**, anche Saturation e Noise. A Drift = 0 il plugin è trasparente con qualunque impostazione |
| Randomize | Azione della UI, non un parametro. Sceglie una coppia di canali a caso e imposta `chanL` e `chanR` (un solo passo di undo) |
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
       ├─ chanL / chanR (parametri) → channelProfile(canale)   (puro, deterministico, senza allocazioni)
       ├─ crossfade delle tolleranze (~250 ms) al cambio di canale, indipendente per lato
       ├─ 2 DriftChannel preallocati: lato 0 = L, lato 1 = R (in mono solo L)
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
| `DspTypes.h` | `kNumConsoleChannels = 64`, `struct EngineSettings {channelL, channelR, drift01, freqTol01, phaseTol01, gainTol01, saturation01, noise01, outGainDb, bypass}` |
| `DspUtils.h` | Ripreso da Pentagrit (fastTanh, dbToGain, timeToCoeff, clampTo) più `sanitize(float)` |
| `Prng.h` | `SplitMix64` (seed → stream) con mappatura propria da intero a float in [-1, 1). **Non** si usa `std::*_distribution`, il cui output dipende dall'implementazione: lo stesso seed deve suonare uguale su Windows, macOS e Linux |
| `DriftProfile.h/.cpp` | `struct VoiceTolerance {lowShelfDb, lowShelfOct, highShelfDb, highShelfOct, bellDb, bellPos01, delay01, gainDb, driveSpread, noiseLevel}` normalizzati in [-1, 1] o [0, 1]; `struct ChannelProfile {VoiceTolerance tol; uint32 noiseSeed;}`; `ChannelProfile channelProfile(int canale, int version = 1) noexcept` (formato congelato v1); `VoiceTolerance lerp(a, b, t)`; `kProfileVersion = 1` |
| `Biquad.h` | Biquad TDF-II float (coefficienti RBJ: low shelf, high shelf, bell); i coefficienti identità sono esatti a 0 dB |
| `DriftChannel.h/.cpp` | Una voce: `prepare(sr)`, `reset()`, `setTargets(const VoiceTolerance&, scaled amounts)` a control rate, `processSample` inline o `process(span)` |
| `DriftEngine.h/.cpp` | Due catene (L e R), crossfade delle tolleranze al cambio di canale, output stage, bypass, meter |
| `Meters.h` | Picco in ingresso e in uscita, `cpuLoad`, (il display dei canali userà i profili, non i meter) |

### Catena per voce (DriftChannel)
Ogni deviazione vale: **valore_profilo × Drift × Tolleranza × massimo**. Le deviazioni sono *a* = Drift × Tol e si ricalcolano a control rate (ogni 32 campioni) da valori smoothed. Sono piccolissime, quindi non producono zipper.

| Stadio | Implementazione | Massimo a 100% / 100% |
|---|---|---|
| Frequenza | low shelf (≈ 150 Hz ± ½ ottava), bell Q 0.7 (log-random 400 Hz – 3 kHz), high shelf (≈ 8 kHz ± ½ ottava) | shelf ±1.0 dB, bell ±0.75 dB |
| Fase | allpass del primo ordine `a = (1−d)/(1+d)`, con d = τ·fs e τ ∈ [0, τmax]. È un piccolo ritardo frazionario, **sempre positivo**: nessuna inversione e nessuna cancellazione. Con d = 0 vale a = 1, cioè identità esatta | τmax = 5 µs; differenza massima tra due canali 5 µs, cioè circa −0.1 dB a 10 kHz sulla somma mono |
| Guadagno | lineare, smoothed | ±1.0 dB (nel range richiesto 0.1–1 dB) |
| Saturazione | `y = tanh(k·x)/k` con `k = 1 + Sat·Drift·(1 + 0.15·spread_v)`; guadagno unitario per piccoli segnali; saltata quando k = 1 | k ≤ 2.15 (drive massimo circa +6.6 dB, molto dolce) |
| Rumore | xorshift per catena (seed del canale), bianco filtrato da un passa-basso one-pole a 12 kHz, livello ±1.5 dB dal profilo del canale | circa −84 dBFS RMS in uscita |

- **Trasparenza a Drift = 0**: biquad identità, a = 1, guadagno 1, saturazione saltata, rumore 0. Ogni lato è l'ingresso stesso (uscita bit-identica).
- Canali diversi su L e R danno il carattere stereo; lo stesso canale su entrambi i lati produce due uscite identiche (a rumore 0). Da documentare nel README.

### Cambio di canale senza click
*(Sostituisce la vecchia sezione sui pesi delle voci 2/4/8/16, rampa da 50 ms.)*
- Le due catene (L e R) sono preallocate in `prepare()`. Il target di ogni lato è `channelProfile(canale)`.
- Al cambio di canale di un lato, le tolleranze di quel lato passano dal valore effettivo corrente (anche a fade in corso) al profilo del nuovo canale con un crossfade lineare di 250 ms (`lerp`). Il seed del rumore passa subito al nuovo, senza reset dei filtri. I due lati hanno fade indipendenti.
- Al primo blocco dopo `prepare()`/`reset()` non c'è fade: i valori sono i target (sessione riaperta identica fin dal primo campione).
- `chanL` e `chanR` sono **automatabili** perché il cambio è senza click e senza variazione di latenza. Diversamente da `numBands` di Pentagrit, che invece cambia la latenza.

### Randomize e stato delle tolleranze
*(Sostituisce il modello a seed: `driftSeed` e `SeedChangeAction` non esistono più.)*
- `processor.randomize()` (message thread) sceglie L casuale in 1..64 e R = L % 64 + 1, diversa dalla coppia corrente, e imposta `chanL` e `chanR` come UN solo passo di undo "Randomize" (`beginNewTransaction("Randomize")`, gesti annidati su L e R uniti dall'`UndoRecorder`, che non rinomina la transazione mentre `randomize()` è attivo).
- Una nuova istanza sceglie nel costruttore una coppia casuale come `randomize()`, ma senza passo di undo: le tolleranze sono generate all'avvio della sessione e due istanze nuove non suonano uguali per caso. La coppia è il default dei parametri `chanL`/`chanR` di quell'istanza (valore iniziale == default, come richiede CLAP; reset e Ctrl-click tornano a quella coppia).
- L'audio thread vede il cambio dei parametri e fa il crossfade di 250 ms descritto sopra.
- Stato: tag `DRIFTBOX_STATE`, che contiene:
  - l'albero APVTS (con `chanL` e `chanR`)
  - `stateVersion = 1`
  - `profileVersion = 1`
  - `uiWidth` e `uiHeight`
  - `advancedOpen` (pannello Advanced aperto; se manca vale falso; non è un parametro e non crea passi di undo)
- `setStateInformation`: rifiuta versioni future (anche `profileVersion`), `migrateState()` come in Pentagrit, scarta un eventuale `driftSeed` delle build di sviluppo e svuota la cronologia undo. Uno stato letto prima del primo blocco suona identico dal primo campione (nessun fade); uno ripristinato durante la riproduzione fa il crossfade come un'automazione.
- I preset **non** contengono né sovrascrivono `bypass`: vale a livello di progetto, come `bypass` e `osPhase` in Pentagrit.
- Il determinismo cross-platform è garantito da un test con valori golden dei profili dei canali 1, 2 e 64.

### Parametri (`src/params/ParamIDs.h` + `DriftboxParameters.h/.cpp`; tutti `ParameterID{id, 1}`, ID immutabili)
| ID | Tipo | Range | Default |
|---|---|---|---|
| `chanL` | Int (“Channel L”) | 1–64 | 1 |
| `chanR` | Int (“Channel R”) | 1–64 | 2 |
| `drift` | Float % | 0–100 | 50 |
| `freqTol` | Float % | 0–100 | 50 |
| `phaseTol` | Float % | 0–100 | 50 |
| `gainTol` | Float % | 0–100 | 50 |
| `saturation` | Float % | 0–100 | 20 |
| `noise` | Float % | 0–100 | 0 |
| `outGain` | Float dB | −24…+24, passo 0.1 | 0 |
| `bypass` | Bool | — | off (`getBypassParameter()` override) |

- `kNumParameters = 10`. Il parametro `channels` è stato rimosso (revisione 2026-11-12).
- Testo digitato parsato con `withValueFromStringFunction` ("50 %", "-3 dB", "12" per i canali), come in Pentagrit.
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
  - **Centro** *(rivisto)*: selettori dei canali L e R (1–64) e **VoiceDisplay**, che mostra la deviazione di guadagno e di tono dei due canali scelti. Viene ridisegnato solo quando cambia il profilo o un parametro (timer 30 Hz con dirty flag).
  - **Riga di manopole** *(rivista: Freq, Phase, Gain, Saturation e Noise vanno in un pannello "Advanced" apribile)*: in vista Drift (grande) e Output, più L/R, Randomize e Bypass.
  - **Footer**: picco in uscita, CPU, versione.
- Interazioni riprese da Pentagrit:
  - doppio clic per digitare un valore, Ctrl-clic per il reset, tooltip;
  - Ctrl/Cmd+Z per annullare e Ctrl/Cmd+Shift+Z per ripetere;
  - attachment JUCE con gesture.

## Preset e impostazioni (versioni ridotte)
- **PresetManager** (solo message thread; i preset salvano anche `chanL` e `chanR`):
  - factory read-only incorporati più file utente `.dbp` (XML + `stateVersion`) in `%APPDATA%/Driftbox/Presets`, con le cartelle equivalenti su macOS e Linux;
  - operazioni: Save, Save As, Delete, prev/next, indicatore "modificato";
  - niente A/B, categorie, import o export;
  - escludono `bypass`.
- **Preset di fabbrica**, con nomi originali e nessun riferimento a hardware: Init (Drift 0), Subtle Spread, Wide Pair (canali lontani), Close Pair (canali vicini), Warm Stack, Gentle Grit, Noisy Desk.
- **SettingsManager** (`Driftbox/settings.settings`, condiviso tra le istanze, tollerante a file mancanti o corrotti): zoom, tooltip e cartella dei preset utente.

## Test (Catch2, `ctest`)
Test di accettazione richiesti:
1. **Trasparenza**: Drift = 0 con tutti gli altri parametri al massimo, a 44.1/48/96/192 kHz e per varie coppie di canali. Rumore e sinusoidi in ingresso; max |y − x| < 1e−5 (−100 dB).
2. **Niente NaN/Inf**:
   - fuzz casuale di parametri e block size (1..4096, anche dispari, block size 0);
   - ingresso con NaN, Inf e valori 1e30: l'uscita è sempre finita e il motore si riprende al blocco successivo;
   - denormal.
3. **Cambio di canale senza click** *(rivisto)*: sinusoide da 1 kHz a −6 dBFS, Drift 100 %, cambi di `chanL` e `chanR`, anche a ogni blocco come un'automazione veloce. Il Δ massimo tra campioni consecutivi è ≤ 1.05 × il Δ a regime + 1e−4; il salto RMS tra finestre da 5 ms è ≤ max(0.1 dB, 1.5 · |dL| · 5/250). Stesso controllo su `randomize()`.
4. **Stato**:
   - round trip di tutti i parametri (compresi `chanL` e `chanR`) e della dimensione UI;
   - due istanze con lo stesso stato (letto prima del primo blocco) producono **un'uscita identica bit per bit** (anche il rumore è seminato dal canale);
   - uno stato garbage viene ignorato; una versione futura (`stateVersion` o `profileVersion`) viene rifiutata; uno stato senza versione vale come versione 1.
5. **Niente clipping**: sinusoide e rumore a −1 dBFS con tutto al massimo e Output +24 dB danno picco ≤ 1.0. Un segnale a −6 dBFS con Output 0 non tocca il ceiling (|y| < 0.891).

Test aggiuntivi in stile Pentagrit:
- profili dei canali: determinismo, 64 canali tutti diversi, range, statistica, valori golden dei canali 1, 2 e 64, limiti del numero di canale;
- voce: identità, guadagno entro ±1 dB, ritardo di fase entro τmax;
- anti-cancellazione: somma mono (L+R)/2 con ingresso identico, solo fase al 100 % e Drift 100 %, entro ±0.5 dB fino a 10 kHz su tutte le coppie (c, c+1) e su coppie casuali;
- parametri: numero, ID, range, parsing del testo;
- zero allocazioni in `processBlock` (override globale di `new`, tag `[allocation]`);
- editor headless: creazione, distruzione, zoom;
- undo di parametri e di Randomize (un solo passo "Randomize"; il costruttore non lascia passi);
- preset e impostazioni con round trip e file corrotti;
- performance: due catene a 48 kHz, soglia larga in CI.

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
| T4 | Integrazione nel processor | `PluginProcessor.*` (randomize, stato, UndoRecorder; il seed e `SeedChangeAction` sono stati rimossi dalla revisione 2026-11-12), `StateTests`, `AllocationTests`, `UndoTests`, `PerfTests` | Round trip bit-identico; zero allocazioni; pluginval 10 in locale |
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
1. **Somma mono L+R con cancellazioni di fase**: ritardi sempre positivi e limitati (τmax 5 µs); test sulla risposta della somma entro ±0.5 dB fino a 10 kHz.
2. **Determinismo cross-platform dei profili**: PRNG proprio con mappatura esplicita, passaggio di hash sul numero di canale, `profileVersion`, test con valori golden.
3. **Click al cambio di canale o al Randomize**: crossfade delle tolleranze dal valore corrente, seed del rumore cambiato senza reset dei filtri; test sul Δ campione per campione.
4. **Ceiling e trasparenza**: il knee parte da −1 dBFS, quindi sotto quella soglia il segnale resta invariato; è documentato che con Output alto il ceiling lavora.
5. **CPU con molte istanze**: solo due catene, coefficienti a control rate, `fastTanh`, saturazione saltata a k = 1; il PerfTest fa da guardia.
6. **Percorsi lunghi su Windows** (repo nella cartella Desktop): build in `build/<preset>` e cache CPM in `C:\dev\.cpm`; worktree degli agenti in `C:\dev\db-tN`.
7. **pluginval 10**: fuzz su `chanL`, `chanR` e bypass, block size e sample rate variabili, già coperti dai test.
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
