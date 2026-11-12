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
    static constexpr double kVoiceRampSeconds = 0.050;
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
    // Valori smussati dei parametri (aggiornati una volta per sotto-blocco).
    struct SmoothedParams
    {
        float drift = 0.0f, freqTol = 0.0f, phaseTol = 0.0f, gainTol = 0.0f, saturation = 0.0f, noise = 0.0f;
    };

    // Un sotto-blocco di n <= kControlInterval campioni sui canali `numCh` (puntatori in `ch`).
    void processChunk(float* const* ch, int numCh, int n) noexcept;
    // Aggiorna profilo, pesi e smoothing per un sotto-blocco di n campioni; riempie w0/w1 per voce.
    void advanceControl(int n) noexcept;
    // Imposta i seed del profilo `profile` sui generatori di rumore delle voci (senza reset dei filtri).
    void applyNoiseSeeds(const DriftProfile& profile) noexcept;
    void computeTargetWeights() noexcept;

    double sampleRate_ = 48000.0;
    int maxBlock_ = 0, numChannels_ = 2;
    EngineSettings settings_;
    std::uint64_t seed_ = 0;
    // profile_ è il profilo effettivo del sotto-blocco corrente; from/to servono solo durante il crossfade.
    DriftProfile profile_, fromProfile_, toProfile_;
    bool fading_ = false;
    float fadePos_ = 0.0f; // 0..1
    std::array<DriftChannel, kMaxVoices> voices_;

    // Pesi: correnti, di partenza della rampa e target; rampPos_ in 0..1 (1 = rampa finita).
    std::array<float, kMaxVoices> weights_{}, startWeights_{}, targetWeights_{};
    std::array<float, kMaxVoices> w0_{}, w1_{}; // pesi a inizio e fine del sotto-blocco corrente
    float rampPos_ = 1.0f;
    int targetVoices_ = 0; // N del target corrente (0 = nessuno ancora)

    SmoothedParams smooth_;
    float outGain_ = 1.0f;
    float wetMix_ = 1.0f; // 1 = wet, 0 = dry (bypass)

    bool fresh_ = true; // nessun blocco elaborato dopo prepare()/reset(): i valori partono dai target
    // Le voci non sono state elaborate (bypass stabile): vanno resettate alla ripresa.
    bool voicesIdle_ = false;

    // Buffer di lavoro a dimensione fissa (nessuna allocazione in process()).
    std::array<std::array<float, kControlInterval>, 2> in_{}, acc_{};

    MeterState meters_;
    float blockInPeak_ = 0.0f, blockOutPeak_ = 0.0f;
    bool blockCeiling_ = false;
};
} // namespace driftbox
