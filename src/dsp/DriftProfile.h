// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DspTypes.h"
#include <array>
#include <cstdint>

namespace driftbox
{
// Versione dell'algoritmo che genera il profilo di un canale. Salvata nello stato: se un giorno la
// generazione cambia, si aggiunge una nuova versione e channelProfile() continua a riprodurre quelle vecchie.
inline constexpr int kProfileVersion = 1;

// Limiti delle deviazioni a Drift = 100 % e tolleranza = 100 %. La deviazione effettiva di un canale vale
// valore_del_profilo x drift01 x tolleranza01 x limite.
namespace limits
{
inline constexpr float kMaxShelfDb = 1.0f;     // low/high shelf, +-dB
inline constexpr float kMaxBellDb = 0.75f;     // campana, +-dB
inline constexpr float kLowShelfHz = 150.0f;   // centro del low shelf
inline constexpr float kHighShelfHz = 8000.0f; // centro dell'high shelf
inline constexpr float kShelfSpreadOct = 0.5f; // spostamento massimo delle frequenze degli shelf, +-ottave
inline constexpr float kShelfQ = 0.7071f;      // pendenza degli shelf (Q del cookbook RBJ)
inline constexpr float kBellLoHz = 400.0f;     // la campana sta tra kBellLoHz e kBellHiHz (scala logaritmica)
inline constexpr float kBellHiHz = 3000.0f;
inline constexpr float kBellQ = 0.7f;
inline constexpr float kMaxDelayUs = 5.0f; // ritardo frazionario dell'allpass, sempre >= 0
inline constexpr float kMaxGainDb = 1.0f;  // guadagno, +-dB
inline constexpr float kMaxDrive = 1.0f;   // k = 1 + kMaxDrive x sat x drift x (1 + kDriveSpread x spread)
inline constexpr float kDriveSpread = 0.15f;
inline constexpr float kNoiseMaxDbfs = -84.0f; // RMS del rumore in uscita con noise = drift = 100 %
inline constexpr float kNoiseSpreadDb = 1.5f;  // differenza di livello del rumore tra i canali, +-dB
inline constexpr float kNoiseLowpassHz = 12000.0f;
} // namespace limits

// Tolleranze normalizzate di un canale (i componenti "fuori tolleranza" di una striscia del mixer).
// Campi bipolari in [-1, 1], campi *01 in [0, 1].
struct VoiceTolerance
{
    float lowShelfDb = 0.0f;   // x kMaxShelfDb
    float lowShelfOct = 0.0f;  // x kShelfSpreadOct attorno a kLowShelfHz
    float highShelfDb = 0.0f;  // x kMaxShelfDb
    float highShelfOct = 0.0f; // x kShelfSpreadOct attorno a kHighShelfHz
    float bellDb = 0.0f;       // x kMaxBellDb
    float bellPos01 = 0.5f;    // posizione logaritmica tra kBellLoHz e kBellHiHz
    float delay01 = 0.0f;      // x kMaxDelayUs
    float gainDb = 0.0f;       // x kMaxGainDb
    float driveSpread = 0.0f;  // x kDriveSpread
    float noiseLevel = 0.0f;   // x kNoiseSpreadDb
};

// Tolleranze fisse di un canale del mixer virtuale, più il seed del suo generatore di rumore (mai 0).
struct ChannelProfile
{
    VoiceTolerance tol;
    std::uint32_t noiseSeed = 1u;
};

// Tolleranze fisse del canale `channel` (limitato a 1..kNumConsoleChannels) del "mixer" virtuale: a ogni
// numero corrisponde sempre lo stesso profilo, su ogni piattaforma. Pura e senza allocazioni: può essere
// chiamata anche dall'audio thread. Versioni sconosciute usano kProfileVersion.
ChannelProfile channelProfile(int channel, int version = kProfileVersion) noexcept;

// Interpolazione lineare campo per campo, usata per il crossfade al cambio di canale. t è limitato a [0, 1]
// (NaN -> 0); a t = 0 il risultato è esattamente a e a t = 1 esattamente b.
VoiceTolerance lerp(const VoiceTolerance& a, const VoiceTolerance& b, float t) noexcept;
} // namespace driftbox
