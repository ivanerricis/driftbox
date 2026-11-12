// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/ToleranceResponse.h"

#include <algorithm>

namespace driftbox
{
double toleranceDeviationDb(const VoiceTolerance& tol, const VoiceAmounts& amounts, double hz,
                            double sampleRate) noexcept
{
    const auto d = DriftChannel::design(tol, amounts);
    hz = std::clamp(hz, 1.0, 0.49 * sampleRate);
    const auto q = [](const DriftChannel::Design::Filter& f) { return static_cast<double>(f.q); };
    const auto g = [](const DriftChannel::Design::Filter& f) { return static_cast<double>(f.gainDb); };
    const auto f = [](const DriftChannel::Design::Filter& x) { return static_cast<double>(x.hz); };

    return BiquadCoeffs::lowShelf(sampleRate, f(d.lowShelf), q(d.lowShelf), g(d.lowShelf))
               .magnitudeDb(sampleRate, hz) +
           BiquadCoeffs::peak(sampleRate, f(d.bell), q(d.bell), g(d.bell)).magnitudeDb(sampleRate, hz) +
           BiquadCoeffs::highShelf(sampleRate, f(d.highShelf), q(d.highShelf), g(d.highShelf))
               .magnitudeDb(sampleRate, hz) +
           d.gainDb;
}

ToleranceReadout toleranceReadout(const VoiceTolerance& tol, const VoiceAmounts& amounts) noexcept
{
    const auto d = DriftChannel::design(tol, amounts);
    ToleranceReadout r;
    r.gainDb = static_cast<float>(d.gainDb);
    r.delayMicros = static_cast<float>(d.delaySeconds * 1.0e6);
    r.drivePercent = tol.driveSpread * limits::kDriveSpread * 100.0f * amounts.saturation;
    return r;
}
} // namespace driftbox
