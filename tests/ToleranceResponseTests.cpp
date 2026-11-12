// SPDX-License-Identifier: AGPL-3.0-or-later
// Curva di tolleranza mostrata dalla UI: zero a Drift 0 e dentro i limiti documentati a Drift 100 %.
#include "dsp/ToleranceResponse.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace driftbox;
using Catch::Approx;

namespace
{
const double kFreqs[] = {20.0, 50.0, 100.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0, 20000.0};
}

TEST_CASE("Tolerance response: exactly flat at Drift 0 for every channel", "[tolerance]")
{
    const VoiceAmounts zero{}; // drift = 0 azzera tutte le quantità
    for (int ch = 1; ch <= kNumConsoleChannels; ++ch)
    {
        const auto tol = channelProfile(ch).tol;
        for (const double sr : {44100.0, 48000.0, 96000.0})
        {
            for (const double hz : kFreqs)
            {
                REQUIRE(toleranceDeviationDb(tol, zero, hz, sr) == 0.0);
            }
        }
        const auto r = toleranceReadout(tol, zero);
        CHECK(r.gainDb == 0.0f);
        CHECK(r.delayMicros == 0.0f);
        CHECK(r.drivePercent == 0.0f);
    }
}

TEST_CASE("Tolerance response: within the documented limits at 100 %", "[tolerance]")
{
    const VoiceAmounts full{1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    double worst = 0.0;
    bool anyDeviation = false;
    for (int ch = 1; ch <= kNumConsoleChannels; ++ch)
    {
        const auto tol = channelProfile(ch).tol;
        for (const double hz : kFreqs)
        {
            const double dev = toleranceDeviationDb(tol, full, hz, 48000.0);
            worst = std::max(worst, std::fabs(dev));
            anyDeviation = anyDeviation || std::fabs(dev) > 0.01;
        }
        const auto r = toleranceReadout(tol, full);
        CHECK(std::fabs(r.gainDb) <= limits::kMaxGainDb + 1e-6f);
        CHECK(r.delayMicros >= 0.0f);
        CHECK(r.delayMicros <= limits::kMaxDelayUs + 1e-6f);
        CHECK(std::fabs(r.drivePercent) <= limits::kDriveSpread * 100.0f + 1e-4f);
    }
    CHECK(anyDeviation);
    CHECK(worst <= static_cast<double>(kMaxDeviationDb) + 1e-3);
}

TEST_CASE("Tolerance response: scales with the amounts and matches the gain alone", "[tolerance]")
{
    const auto tol = channelProfile(7).tol;
    const VoiceAmounts gainOnly{0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    const double expected = static_cast<double>(tol.gainDb) * limits::kMaxGainDb;
    for (const double hz : kFreqs)
    {
        CHECK(toleranceDeviationDb(tol, gainOnly, hz, 48000.0) == Approx(expected).margin(1e-9));
    }
    // Frequenze oltre Nyquist vengono limitate: nessun NaN.
    CHECK(std::isfinite(toleranceDeviationDb(tol, VoiceAmounts{1, 1, 1, 1, 1}, 1.0e6, 44100.0)));
}
