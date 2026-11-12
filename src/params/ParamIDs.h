// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>

// Gli ID dei parametri sono STABILI: non vanno mai rinominati (CLAP ne calcola l'hash, gli host li salvano).
namespace driftbox
{
inline constexpr int kParamVersion = 1;
inline constexpr int kStateVersion = 1;
inline constexpr const char* kStateTag = "DRIFTBOX_STATE";

namespace pid
{
inline constexpr const char *channels = "channels", *drift = "drift", *freqTol = "freqTol",
                            *phaseTol = "phaseTol", *gainTol = "gainTol", *saturation = "saturation",
                            *noise = "noise", *outGain = "outGain", *bypass = "bypass";
} // namespace pid

// Valori del parametro `channels` (indice della scelta -> numero di voci interne).
inline constexpr std::array<int, 4> kChannelChoices{2, 4, 8, 16};
inline constexpr int kDefaultChannelIndex = 1; // 4 voci
} // namespace driftbox
