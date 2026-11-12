// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/Biquad.h"
#include "dsp/DriftProfile.h"
#include "dsp/Prng.h"
#include <cstdint>

namespace driftbox
{
// Quantità effettive di deviazione per una voce, in 0..1, GIÀ moltiplicate per drift01 dal motore:
// freq = drift x freqTol, phase = drift x phaseTol, gain = drift x gainTol, saturation = drift x saturation,
// noise = drift x noise. Con tutti i campi a 0 la voce è l'identità esatta.
struct VoiceAmounts
{
    float freq = 0.0f, phase = 0.0f, gain = 0.0f, saturation = 0.0f, noise = 0.0f;
};

// Una voce interna: catena DSP indipendente e molto leggera.
//   ingresso -> low shelf -> campana -> high shelf -> allpass del primo ordine (ritardo frazionario >= 0)
//            -> guadagno -> saturazione morbida tanh(k x) / k -> + rumore
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

    // Ricalcola i coefficienti. noiseScale (>= 1) è il fattore sqrt(N/2) con cui il motore compensa la media
    // su N/2 voci, così il livello di rumore in uscita non dipende dal numero di voci.
    void update(const VoiceTolerance& tol, const VoiceAmounts& amounts, float noiseScale) noexcept;

    // Elabora n campioni di `in` e li ACCUMULA in `acc`: acc[i] += w(i) * y[i], dove w va linearmente da w0
    // (primo campione) a w1 (ultimo campione + 1, cioè w(i) = w0 + (w1 - w0) * i / n).
    void processAdd(const float* in, float* acc, int n, float w0, float w1) noexcept;

    // Un campione (per i test e per processAdd).
    float processSample(float x) noexcept;

private:
    double sampleRate_ = 48000.0;
    Biquad lowShelf_, bell_, highShelf_;
    float apCoeff_ = 1.0f, apState_ = 0.0f; // allpass del primo ordine; a = 1 -> identità
    float gain_ = 1.0f;
    float drive_ = 1.0f;    // k; 1 -> saturazione saltata
    float noiseAmp_ = 0.0f; // ampiezza del rumore bianco prima del passa-basso
    float noiseLpCoeff_ = 0.0f, noiseLpState_ = 0.0f;
    std::uint32_t noiseSeed_ = 1u;
    Xorshift32 noise_;
};
} // namespace driftbox
