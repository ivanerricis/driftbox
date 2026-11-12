// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/Biquad.h"

namespace driftbox
{
// STUB (scaffold T0): restituiscono l'identità. L'implementazione RBJ arriva con il task T2.
BiquadCoeffs BiquadCoeffs::lowShelf(double, double, double, double) noexcept
{
    return identity();
}

BiquadCoeffs BiquadCoeffs::highShelf(double, double, double, double) noexcept
{
    return identity();
}

BiquadCoeffs BiquadCoeffs::peak(double, double, double, double) noexcept
{
    return identity();
}
} // namespace driftbox
