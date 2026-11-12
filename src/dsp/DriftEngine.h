// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DriftChannel.h"
#include "dsp/DriftProfile.h"
#include "dsp/DspTypes.h"
#include "dsp/Meters.h"
#include <array>
#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>

namespace driftbox
{
// Motore multi-voce di Driftbox.
//
// Flusso per blocco (stereo o mono, latenza 0):
//   per lato s: uscita_s = somma su v con (v & 1) == s di peso_v x voce_v(ingresso_s)
//   -> Output Gain (smussato) -> limitatore di sicurezza softCeiling() -> sanitizzazione NaN/Inf
//   -> crossfade di bypass verso l'ingresso.
// In mono il lato 1 non esiste: le voci dispari sono saltate e quelle pari pesano 1/(N/2).
//
// Pesi delle voci: target 2/N per v < N, 0 altrimenti. Rampa lineare di kVoiceRampSeconds sullo stesso
// orologio per tutte le voci, quindi la somma dei pesi di un lato resta 1 anche durante la rampa (niente
// click al cambio del numero di canali). Una voce con peso 0 non viene elaborata; quando riparte da 0 viene
// prima resettata.
//
// Tolleranze: il profilo corrente è DriftProfile::generate(seed). Un nuovo seed con crossfade == true passa
// al nuovo profilo interpolando (DriftProfile::lerp) in kProfileFadeSeconds.
//
// Contratto: prepare() può allocare (solo da prepareToPlay); tutto il resto è noexcept e senza allocazioni.
// process() accetta blocchi di qualunque lunghezza (anche 0 o > maxBlockSize: chunking interno).
class DriftEngine
{
public:
    static constexpr int kControlInterval = 32; // campioni tra due aggiornamenti dei coefficienti
    static constexpr double kVoiceRampSeconds = 0.030;
    static constexpr double kProfileFadeSeconds = 0.250;
    static constexpr double kBypassFadeSeconds = 0.020;
    static constexpr double kGainSmoothSeconds = 0.020;  // Output Gain
    static constexpr double kParamSmoothSeconds = 0.050; // drift, tolleranze, saturazione, rumore

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;

    void setSettings(const EngineSettings& settings) noexcept;

    // Imposta il seed delle tolleranze. Nessun effetto se è già quello corrente. Con crossfade == false
    // (stato ripristinato, prima del primo blocco) il nuovo profilo vale subito, così una sessione riaperta
    // suona identica fin dal primo campione.
    void setSeed(std::uint64_t seed, bool crossfade) noexcept;
    std::uint64_t getSeed() const noexcept { return seed_; }

    void process(juce::AudioBuffer<float>& buffer) noexcept;

    MeterState& meters() noexcept { return meters_; }

    // Peso corrente della voce v (per i test e per la UI).
    float voiceWeight(int v) const noexcept;

private:
    double sampleRate_ = 48000.0;
    int maxBlock_ = 0, numChannels_ = 2;
    EngineSettings settings_;
    std::uint64_t seed_ = 0;
    DriftProfile profile_, fromProfile_, toProfile_;
    std::array<DriftChannel, kMaxVoices> voices_;
    std::array<float, kMaxVoices> weights_{};
    MeterState meters_;
};
} // namespace driftbox
