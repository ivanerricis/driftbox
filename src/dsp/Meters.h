// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>

namespace driftbox
{
// L'audio thread scrive con ordinamento relaxed, la UI legge.
struct MeterState
{
    std::atomic<float> inPeakDb{-100.0f}, outPeakDb{-100.0f};
    std::atomic<float> cpuLoad{0.0f}; // frazione 0..1 del tempo del blocco spesa in processBlock
    std::atomic<bool> ceilingActive{
        false}; // true se il limitatore di sicurezza ha lavorato nell'ultimo blocco
};
} // namespace driftbox
