// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DriftChannel.h"
#include "dsp/DriftProfile.h"
#include "dsp/DspTypes.h"
#include "dsp/Meters.h"
#include <array>
#include <juce_audio_basics/juce_audio_basics.h>

namespace driftbox
{
// Motore di Driftbox: una catena DriftChannel per lato, come una striscia di un mixer virtuale.
//
// Flusso per blocco (stereo o mono, latenza 0):
//   per lato s (0 = L, 1 = R): uscita_s = catena_s(ingresso_s)
//   -> Output Gain (smussato) -> limitatore di sicurezza softCeiling() -> sanitizzazione NaN/Inf
//   -> crossfade di bypass verso l'ingresso.
// In mono si elabora solo il lato 0 (L).
//
// Tolleranze: ogni lato ha un numero di canale (1..kNumConsoleChannels) a cui corrisponde un profilo fisso,
// channelProfile(canale). Quando il canale di un lato cambia, le tolleranze di quel lato passano dal valore
// effettivo corrente (anche a fade in corso) al profilo del nuovo canale con un crossfade lineare di
// kProfileFadeSeconds (lerp()); il seed del rumore passa subito al nuovo, senza reset dei filtri. I due lati
// hanno fade indipendenti. Al primo blocco dopo prepare()/reset() non c'è nessun fade: i valori sono i
// target.
//
// Contratto: prepare() può allocare (solo da prepareToPlay); tutto il resto è noexcept e senza allocazioni.
// process() accetta blocchi di qualunque lunghezza (anche 0 o > maxBlockSize: chunking interno).
class DriftEngine
{
public:
    static constexpr int kControlInterval = 32; // campioni tra due aggiornamenti dei coefficienti
    static constexpr double kProfileFadeSeconds = 0.250;
    static constexpr double kBypassFadeSeconds = 0.020;
    static constexpr double kGainSmoothSeconds = 0.020;  // Output Gain
    static constexpr double kParamSmoothSeconds = 0.050; // drift, tolleranze, saturazione, rumore

    DriftEngine() noexcept { sides_[1].channel = 2; } // default L = 1, R = 2

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;

    void setSettings(const EngineSettings& settings) noexcept;

    void process(juce::AudioBuffer<float>& buffer) noexcept;

    MeterState& meters() noexcept { return meters_; }

    // Canale TARGET del lato (0 = L, 1 = R) applicato all'ultimo sotto-blocco elaborato (prima del primo
    // blocco vale il default 1 / 2). In mono il lato 1 non viene elaborato e resta fermo. Per i test e la UI.
    int channelOf(int side) const noexcept;

    // Vero finché il crossfade delle tolleranze del lato è in corso. Per i test e la UI.
    bool isFading(int side) const noexcept;

private:
    // Valori smussati dei parametri (aggiornati una volta per sotto-blocco).
    struct SmoothedParams
    {
        float drift = 0.0f, freqTol = 0.0f, phaseTol = 0.0f, gainTol = 0.0f, saturation = 0.0f, noise = 0.0f;
    };

    // Catena di un lato con le sue tolleranze: `cur` è il valore effettivo del sotto-blocco corrente,
    // `from`/`to` servono solo durante il crossfade.
    struct Side
    {
        DriftChannel chain;
        int channel = 1;
        VoiceTolerance cur, from, to;
        bool fading = false;
        float fadePos = 0.0f; // 0..1
    };

    // Un sotto-blocco di n <= kControlInterval campioni sui canali `numCh` (puntatori in `ch`).
    void processChunk(float* const* ch, int numCh, int n) noexcept;
    // Aggiorna smoothing e tolleranze dei lati attivi per un sotto-blocco di n campioni.
    void advanceControl(int numCh, int n) noexcept;

    double sampleRate_ = 48000.0;
    int maxBlock_ = 0, numChannels_ = 2;
    EngineSettings settings_;
    std::array<Side, 2> sides_;

    SmoothedParams smooth_;
    float outGain_ = 1.0f;
    float wetMix_ = 1.0f; // 1 = wet, 0 = dry (bypass)

    bool fresh_ = true; // nessun blocco elaborato dopo prepare()/reset(): i valori partono dai target
    // Le catene non sono state elaborate (bypass stabile): vanno resettate alla ripresa.
    bool chainsIdle_ = false;

    // Buffer di lavoro a dimensione fissa (nessuna allocazione in process()).
    std::array<std::array<float, kControlInterval>, 2> in_{}, acc_{};

    MeterState meters_;
    float blockInPeak_ = 0.0f, blockOutPeak_ = 0.0f;
    bool blockCeiling_ = false;
};
} // namespace driftbox
