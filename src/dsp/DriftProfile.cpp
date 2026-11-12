// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/DriftProfile.h"

#include "dsp/Prng.h"

namespace driftbox
{
// STUB (scaffold T0): profilo neutro. L'implementazione completa arriva con il task T1.
DriftProfile DriftProfile::generate(std::uint64_t seed, int) noexcept
{
    DriftProfile p;
    p.seed = seed;
    for (auto& s : p.noiseSeeds)
    {
        s = 1u;
    }
    return p;
}

DriftProfile DriftProfile::lerp(const DriftProfile&, const DriftProfile& b, float) noexcept
{
    return b;
}
} // namespace driftbox
