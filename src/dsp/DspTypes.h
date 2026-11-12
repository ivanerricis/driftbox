// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace driftbox
{
// Numero di canali del "mixer" virtuale: ogni lato (L e R) sceglie un canale da 1 a kNumConsoleChannels e a
// ogni numero corrisponde un set di tolleranze fisso (vedi channelProfile() in DriftProfile.h).
inline constexpr int kNumConsoleChannels = 64;

// Impostazioni del motore per un blocco, lette dai parametri (ParamRefs::snapshot()).
// Tutte le grandezze *01 sono in 0..1. Drift Amount (drift01) scala TUTTE le deviazioni, anche saturazione e
// rumore: a drift01 = 0 il motore è trasparente qualunque siano gli altri valori.
struct EngineSettings
{
    int channelL = 1; // canale del lato sinistro, 1..kNumConsoleChannels
    int channelR = 2; // canale del lato destro, 1..kNumConsoleChannels
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
