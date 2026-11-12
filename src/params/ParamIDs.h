// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Gli ID dei parametri sono STABILI: non vanno mai rinominati (CLAP ne calcola l'hash, gli host li salvano).
namespace driftbox
{
inline constexpr int kParamVersion = 1;
inline constexpr int kStateVersion = 1;
inline constexpr const char* kStateTag = "DRIFTBOX_STATE";

namespace pid
{
inline constexpr const char *chanL = "chanL", *chanR = "chanR", *drift = "drift", *freqTol = "freqTol",
                            *phaseTol = "phaseTol", *gainTol = "gainTol", *saturation = "saturation",
                            *noise = "noise", *outGain = "outGain", *bypass = "bypass";
} // namespace pid
} // namespace driftbox
