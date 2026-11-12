// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace driftbox
{
// Numero massimo di voci interne (metà per lato: la voce v elabora il lato v & 1).
inline constexpr int kMaxVoices = 16;

// Impostazioni del motore per un blocco, lette dai parametri (ParamRefs::snapshot()).
// Tutte le grandezze *01 sono in 0..1. Drift Amount (drift01) scala TUTTE le deviazioni, anche saturazione e
// rumore: a drift01 = 0 il motore è trasparente qualunque siano gli altri valori.
struct EngineSettings
{
    int numVoices = 4; // 2, 4, 8 o 16
    float drift01 = 0.5f;
    float freqTol01 = 0.5f;
    float phaseTol01 = 0.5f;
    float gainTol01 = 0.5f;
    float saturation01 = 0.2f;
    float noise01 = 0.0f;
    float outGainDb = 0.0f;
    bool bypass = false;
};
} // namespace driftbox
