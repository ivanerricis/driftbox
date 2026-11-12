// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/DriftProfile.h"
#include "dsp/Prng.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>

using namespace driftbox;
using Catch::Approx;

namespace
{
using Field = float VoiceTolerance::*;

constexpr std::array<Field, 8> kBipolarFields = {&VoiceTolerance::lowShelfDb,  &VoiceTolerance::lowShelfOct,
                                                 &VoiceTolerance::highShelfDb, &VoiceTolerance::highShelfOct,
                                                 &VoiceTolerance::bellDb,      &VoiceTolerance::gainDb,
                                                 &VoiceTolerance::driveSpread, &VoiceTolerance::noiseLevel};

constexpr std::array<Field, 10> kAllFields = {&VoiceTolerance::lowShelfDb,  &VoiceTolerance::lowShelfOct,
                                              &VoiceTolerance::highShelfDb, &VoiceTolerance::highShelfOct,
                                              &VoiceTolerance::bellDb,      &VoiceTolerance::bellPos01,
                                              &VoiceTolerance::delay01,     &VoiceTolerance::gainDb,
                                              &VoiceTolerance::driveSpread, &VoiceTolerance::noiseLevel};

bool identical(const DriftProfile& a, const DriftProfile& b)
{
    if (a.seed != b.seed || a.noiseSeeds != b.noiseSeeds)
    {
        return false;
    }
    for (int v = 0; v < kMaxVoices; ++v)
    {
        for (auto f : kAllFields)
        {
            if (!(a.voices[static_cast<size_t>(v)].*f == b.voices[static_cast<size_t>(v)].*f))
            {
                return false;
            }
        }
    }
    return true;
}

std::uint64_t testSeed(int i)
{
    return 0x9E3779B97F4A7C15ull * static_cast<std::uint64_t>(i + 1) + 12345ull;
}
} // namespace

TEST_CASE("SplitMix64 matches the reference implementation", "[profile]")
{
    SplitMix64 rng(0);
    CHECK(rng.next() == 0xE220A8397B1DCDAFull);
    CHECK(rng.next() == 0x6E789E6AA1B965F4ull);
    CHECK(rng.next() == 0x06C45D188009454Full);
}

TEST_CASE("DriftProfile: same seed gives identical profile", "[profile]")
{
    for (std::uint64_t seed : {0ull, 1ull, 0xDEADBEEFCAFEF00Dull})
    {
        CHECK(identical(DriftProfile::generate(seed), DriftProfile::generate(seed)));
    }
}

TEST_CASE("DriftProfile: different seeds give different profiles", "[profile]")
{
    const std::array<std::uint64_t, 6> seeds = {
        0ull, 1ull, 2ull, 0xFFFFFFFFFFFFFFFFull, 0x8000000000000000ull, 0x0123456789ABCDEFull};
    for (size_t i = 0; i < seeds.size(); ++i)
    {
        for (size_t j = i + 1; j < seeds.size(); ++j)
        {
            CHECK_FALSE(identical(DriftProfile::generate(seeds[i]), DriftProfile::generate(seeds[j])));
        }
    }
}

TEST_CASE("DriftProfile: fields stay inside their ranges", "[profile]")
{
    for (int i = 0; i < 200; ++i)
    {
        const auto p = DriftProfile::generate(testSeed(i));
        CHECK(p.seed == testSeed(i));
        for (int v = 0; v < kMaxVoices; ++v)
        {
            const auto& t = p.voices[static_cast<size_t>(v)];
            for (auto f : kBipolarFields)
            {
                const float x = t.*f;
                REQUIRE_FALSE(std::isnan(x));
                REQUIRE(x >= -1.0f);
                REQUIRE(x <= 1.0f);
            }
            for (auto f : {&VoiceTolerance::bellPos01, &VoiceTolerance::delay01})
            {
                const float x = t.*f;
                REQUIRE_FALSE(std::isnan(x));
                REQUIRE(x >= 0.0f);
                REQUIRE(x < 1.0f);
            }
            REQUIRE(p.noiseSeeds[static_cast<size_t>(v)] != 0u);
        }
    }
}

TEST_CASE("DriftProfile: bipolar fields are centred on zero", "[profile]")
{
    constexpr int kSeeds = 1000;
    std::array<double, kBipolarFields.size()> sum{};
    double delaySum = 0.0;
    for (int i = 0; i < kSeeds; ++i)
    {
        const auto p = DriftProfile::generate(testSeed(i));
        for (const auto& t : p.voices)
        {
            for (size_t k = 0; k < kBipolarFields.size(); ++k)
            {
                sum[k] += static_cast<double>(t.*kBipolarFields[k]);
            }
            delaySum += static_cast<double>(t.delay01);
        }
    }
    const double n = static_cast<double>(kSeeds) * kMaxVoices;
    for (size_t k = 0; k < kBipolarFields.size(); ++k)
    {
        CHECK(std::abs(sum[k] / n) < 0.05);
    }
    CHECK(delaySum / n == Approx(0.5).margin(0.05));
}

// I valori golden sono il formato congelato della versione 1: se questo test fallisce, la generazione è
// cambiata e le sessioni salvate non suonerebbero più uguali. Proteggono la riproducibilità tra Windows,
// macOS e Linux. Non aggiornarli: aggiungere una nuova versione dell'algoritmo.
// Tutti i valori sono esattamente rappresentabili in float, quindi il confronto è esatto.
TEST_CASE("DriftProfile: golden values for version 1", "[profile]")
{
    const auto p = DriftProfile::generate(0x0123456789ABCDEFull);
    const auto& v0 = p.voices[0];
    CHECK(v0.lowShelfDb == -0x1.51276p-4f);
    CHECK(v0.lowShelfOct == -0x1.6cd98p-3f);
    CHECK(v0.highShelfDb == -0x1.d406bap-1f);
    CHECK(v0.highShelfOct == 0x1.08b6bcp-2f);
    CHECK(v0.bellDb == 0x1.002d58p-3f);
    CHECK(v0.bellPos01 == 0x1.9b7192p-1f);
    CHECK(v0.delay01 == 0x1.a9b98p-2f);
    CHECK(v0.gainDb == 0x1.358a58p-3f);
    CHECK(v0.driveSpread == 0x1.ff4aep-5f);
    CHECK(v0.noiseLevel == -0x1.aa3d04p-1f);

    const auto& v15 = p.voices[15];
    CHECK(v15.lowShelfDb == -0x1.78975p-3f);
    CHECK(v15.delay01 == 0x1.6ebb2ep-1f);
    CHECK(v15.gainDb == 0x1.e81eb4p-1f);

    CHECK(p.noiseSeeds[0] == 0x997F31F9u);
    CHECK(p.noiseSeeds[15] == 0x7D9A0481u);
}

TEST_CASE("DriftProfile: unknown versions behave like the current one", "[profile]")
{
    const auto ref = DriftProfile::generate(42, 1);
    CHECK(identical(DriftProfile::generate(42, 0), ref));
    CHECK(identical(DriftProfile::generate(42, 99), ref));
    CHECK(identical(DriftProfile::generate(42, -3), ref));
    CHECK(identical(DriftProfile::generate(42), ref));
}

TEST_CASE("DriftProfile::lerp interpolates field by field", "[profile]")
{
    const auto a = DriftProfile::generate(1);
    const auto b = DriftProfile::generate(2);

    SECTION("endpoints are exact")
    {
        const auto r0 = DriftProfile::lerp(a, b, 0.0f);
        const auto r1 = DriftProfile::lerp(a, b, 1.0f);
        for (size_t v = 0; v < a.voices.size(); ++v)
        {
            for (auto f : kAllFields)
            {
                CHECK(r0.voices[v].*f == a.voices[v].*f);
                CHECK(r1.voices[v].*f == b.voices[v].*f);
            }
        }
    }

    SECTION("midpoint")
    {
        const auto r = DriftProfile::lerp(a, b, 0.5f);
        for (size_t v = 0; v < a.voices.size(); ++v)
        {
            for (auto f : kAllFields)
            {
                const float mid = 0.5f * (a.voices[v].*f + b.voices[v].*f);
                CHECK(r.voices[v].*f == Approx(mid).margin(1e-6));
            }
        }
    }

    SECTION("t is clamped")
    {
        const auto lo = DriftProfile::lerp(a, b, -3.0f);
        const auto hi = DriftProfile::lerp(a, b, 7.0f);
        for (size_t v = 0; v < a.voices.size(); ++v)
        {
            for (auto f : kAllFields)
            {
                CHECK(lo.voices[v].*f == a.voices[v].*f);
                CHECK(hi.voices[v].*f == b.voices[v].*f);
            }
        }
    }

    SECTION("seed and noise seeds come from b")
    {
        const auto r = DriftProfile::lerp(a, b, 0.0f);
        CHECK(r.seed == b.seed);
        CHECK(r.noiseSeeds == b.noiseSeeds);
    }
}
