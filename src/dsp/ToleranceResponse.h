// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DriftChannel.h"

namespace driftbox
{
// Funzioni pure usate dalla UI per mostrare la tolleranza di un canale (message thread, nessun legame con
// l'audio thread). Usano DriftChannel::design() e BiquadCoeffs: nessuna formula è duplicata.

// Estremi della curva mostrata, in dB.
inline constexpr float kResponseGraphDb = 3.0f;

// Limite teorico della deviazione a Drift = 100 % e tolleranze = 100 %: le due shelf, la campana e il
// guadagno possono sommarsi.
inline constexpr float kMaxDeviationDb = 2.0f * limits::kMaxShelfDb + limits::kMaxBellDb + limits::kMaxGainDb;

// Deviazione dell'ampiezza in dB alla frequenza hz rispetto a un canale ideale, per una catena con queste
// tolleranze e quantità, alla frequenza di campionamento sampleRate: shelf, campana e guadagno (l'allpass ha
// modulo 1 e la saturazione non cambia l'ampiezza a segnale piccolo). Con tutte le quantità a 0 vale
// ESATTAMENTE 0. hz è limitata a [1, 0.49 sampleRate].
double toleranceDeviationDb(const VoiceTolerance& tol, const VoiceAmounts& amounts, double hz,
                            double sampleRate) noexcept;

// Valori numerici mostrati accanto alla curva.
struct ToleranceReadout
{
    float gainDb = 0.0f;       // guadagno del canale, dB
    float delayMicros = 0.0f;  // ritardo di fase (allpass), microsecondi, >= 0
    float drivePercent = 0.0f; // scarto del drive della saturazione, +-%, scalato da Drift x Saturation
};

ToleranceReadout toleranceReadout(const VoiceTolerance& tol, const VoiceAmounts& amounts) noexcept;
} // namespace driftbox
