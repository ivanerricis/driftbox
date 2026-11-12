// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DspTypes.h"
#include <array>
#include <cstdint>

namespace driftbox
{
// Versione dell'algoritmo che genera il profilo dal seed. Salvata nello stato: se un giorno la generazione
// cambia, si aggiunge una nuova versione e generate() continua a riprodurre quelle vecchie.
inline constexpr int kProfileVersion = 1;

// Limiti delle deviazioni a Drift = 100 % e tolleranza = 100 %. La deviazione effettiva di una voce vale
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
inline constexpr float kNoiseSpreadDb = 1.5f;  // differenza di livello del rumore tra le voci, +-dB
inline constexpr float kNoiseLowpassHz = 12000.0f;
} // namespace limits

// Tolleranze normalizzate di una voce (i componenti "fuori tolleranza" di un singolo canale).
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

// Profilo completo delle tolleranze: deterministico, derivato solo da (seed, versione).
struct DriftProfile
{
    std::uint64_t seed = 0;
    std::array<VoiceTolerance, kMaxVoices> voices{};
    // Seed del generatore di rumore di ogni voce (derivati dal seed principale, mai 0).
    std::array<std::uint32_t, kMaxVoices> noiseSeeds{};

    // Pura e senza allocazioni: può essere chiamata anche dall'audio thread.
    static DriftProfile generate(std::uint64_t seed, int version = kProfileVersion) noexcept;

    // Interpolazione lineare campo per campo (t in [0, 1]) usata per il crossfade dopo un Randomize.
    // seed e noiseSeeds vengono presi da b.
    static DriftProfile lerp(const DriftProfile& a, const DriftProfile& b, float t) noexcept;
};
} // namespace driftbox
