// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/Biquad.h"
#include "dsp/DriftProfile.h"
#include "dsp/Prng.h"
#include <cstdint>

namespace driftbox
{
// Quantità effettive di deviazione per una catena, in 0..1, GIÀ moltiplicate per drift01 dal motore:
// freq = drift x freqTol, phase = drift x phaseTol, gain = drift x gainTol, saturation = drift x saturation,
// noise = drift x noise. Con tutti i campi a 0 la voce è l'identità esatta.
struct VoiceAmounts
{
    float freq = 0.0f, phase = 0.0f, gain = 0.0f, saturation = 0.0f, noise = 0.0f;
};

// Una catena DSP per lato (canale del mixer virtuale): catena DSP indipendente e molto leggera.
//   ingresso -> low shelf -> campana -> high shelf -> allpass del primo ordine (ritardo frazionario >= 0)
//            -> guadagno -> miscela continua verso tanh(k x) / k -> + rumore
// Tutte le deviazioni vengono da VoiceTolerance (profilo) x VoiceAmounts x limiti (vedi DriftProfile.h).
//
// Contratto:
// - prepare() non alloca; reset(), update() e processAdd() sono noexcept e senza allocazioni.
// - update() viene chiamata dal motore a control rate (ogni DriftEngine::kControlInterval campioni)
//   con valori già smussati; le variazioni tra due chiamate sono piccole e non producono zipper.
// - Con VoiceAmounts tutto a 0: coefficienti identità (BiquadCoeffs::identity()), allpass con a = 1,
//   guadagno 1, saturazione saltata, rumore 0: uscita == ingresso bit per bit.
class DriftChannel
{
public:
    void prepare(double sampleRate) noexcept;

    // Azzera lo stato dei filtri e riavvia il generatore di rumore dal suo seed.
    void reset() noexcept;

    void setNoiseSeed(std::uint32_t seed) noexcept;

    // Ricalcola i coefficienti. noiseScale moltiplica l'ampiezza del rumore (il motore passa 1: c'è una sola
    // catena per lato, quindi nessuna compensazione).
    void update(const VoiceTolerance& tol, const VoiceAmounts& amounts, float noiseScale) noexcept;

    // Elabora n campioni di `in` e li ACCUMULA in `acc`: acc[i] += w(i) * y[i], dove w va linearmente da w0
    // (primo campione) a w1 (ultimo campione + 1, cioè w(i) = w0 + (w1 - w0) * i / n).
    void processAdd(const float* in, float* acc, int n, float w0, float w1) noexcept;

    // Un campione (per i test e per processAdd).
    float processSample(float x) noexcept;

private:
    // Ingressi dell'ultimo calcolo dei coefficienti di un filtro: si ricalcola solo se cambiano (confronto
    // esatto).
    struct FilterKey
    {
        float gainDb = 0.0f, hz = 0.0f;
        double sampleRate = 0.0;
    };

    static void updateFilter(Biquad& filter, FilterKey& key,
                             BiquadCoeffs (*design)(double, double, double, double), double sampleRate,
                             float hz, float q, float gainDb) noexcept;

    // Un campione della catena; Sat e Noise scelgono a compile time se saturazione e rumore sono attivi.
    template <bool Sat, bool Noise> float tick(float x) noexcept;
    template <bool Sat, bool Noise>
    void addBlock(const float* in, float* acc, int n, float w0, float w1) noexcept;

    double sampleRate_ = 48000.0;
    Biquad lowShelf_, bell_, highShelf_;
    FilterKey lowKey_, bellKey_, highKey_;
    float apCoeff_ = 1.0f, apState_ = 0.0f; // allpass del primo ordine; a = 1 -> identità
    float gain_ = 1.0f;
    float drive_ = 1.0f;    // k; 1 -> saturazione saltata
    float satMix_ = 0.0f;   // m = clamp(k - 1, 0, 1): miscela tra y e tanh(k y) / k
    float noiseAmp_ = 0.0f; // ampiezza del rumore bianco prima del passa-basso (compensata)
    float noiseLpCoeff_ = 0.0f, noiseLpState_ = 0.0f;
    std::uint32_t noiseSeed_ = 1u;
    Xorshift32 noise_;
};
} // namespace driftbox
