// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/DriftProfile.h"
#include "dsp/Prng.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <tuple>

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

bool identical(const VoiceTolerance& a, const VoiceTolerance& b)
{
    for (auto f : kAllFields)
    {
        if (!(a.*f == b.*f))
        {
            return false;
        }
    }
    return true;
}

bool identical(const ChannelProfile& a, const ChannelProfile& b)
{
    return a.noiseSeed == b.noiseSeed && identical(a.tol, b.tol);
}

// Tutti i campi (e il seed del rumore) di un profilo come tupla ordinabile.
auto key(const ChannelProfile& p)
{
    const auto& t = p.tol;
    return std::make_tuple(t.lowShelfDb, t.lowShelfOct, t.highShelfDb, t.highShelfOct, t.bellDb, t.bellPos01,
                           t.delay01, t.gainDb, t.driveSpread, t.noiseLevel, p.noiseSeed);
}
} // namespace

TEST_CASE("SplitMix64 matches the reference implementation", "[profile]")
{
    SplitMix64 rng(0);
    CHECK(rng.next() == 0xE220A8397B1DCDAFull);
    CHECK(rng.next() == 0x6E789E6AA1B965F4ull);
    CHECK(rng.next() == 0x06C45D188009454Full);
}

TEST_CASE("channelProfile: same channel gives identical profile", "[profile]")
{
    for (int c = 1; c <= kNumConsoleChannels; ++c)
    {
        CHECK(identical(channelProfile(c), channelProfile(c)));
        CHECK(identical(channelProfile(c), channelProfile(c, 1)));
    }
}

TEST_CASE("channelProfile: all 64 channels are different", "[profile]")
{
    std::set<decltype(key(channelProfile(1)))> profiles;
    std::set<std::uint32_t> noiseSeeds;
    for (int c = 1; c <= kNumConsoleChannels; ++c)
    {
        profiles.insert(key(channelProfile(c)));
        noiseSeeds.insert(channelProfile(c).noiseSeed);
    }
    CHECK(profiles.size() == static_cast<size_t>(kNumConsoleChannels));
    CHECK(noiseSeeds.size() == static_cast<size_t>(kNumConsoleChannels));

    // Tutte le 2016 coppie di canali sono distinte campo per campo.
    for (int a = 1; a <= kNumConsoleChannels; ++a)
    {
        for (int b = a + 1; b <= kNumConsoleChannels; ++b)
        {
            REQUIRE_FALSE(identical(channelProfile(a), channelProfile(b)));
        }
    }
}

TEST_CASE("channelProfile: fields stay inside their ranges", "[profile]")
{
    for (int c = 1; c <= kNumConsoleChannels; ++c)
    {
        const auto p = channelProfile(c);
        for (auto f : kBipolarFields)
        {
            const float x = p.tol.*f;
            REQUIRE_FALSE(std::isnan(x));
            REQUIRE(x >= -1.0f);
            REQUIRE(x <= 1.0f);
        }
        for (auto f : {&VoiceTolerance::bellPos01, &VoiceTolerance::delay01})
        {
            const float x = p.tol.*f;
            REQUIRE_FALSE(std::isnan(x));
            REQUIRE(x >= 0.0f);
            REQUIRE(x < 1.0f);
        }
        REQUIRE(p.noiseSeed != 0u);
    }
}

TEST_CASE("channelProfile: statistics over the 64 channels", "[profile]")
{
    // Ogni campo bipolare è triangolare in [-1, 1] con varianza 1/6 (somma di due uniformi in [-1, 1]
    // dimezzata: (1/3 + 1/3) / 4), quindi sigma = 0.408. La media di 64 canali ha sigma = 0.408 / sqrt(64) =
    // 0.051 e la soglia 0.15 vale circa 2.9 sigma (0.1 sarebbe solo 2 sigma per campo, circa il 34 % di
    // fallimenti casuali su 8 campi). I valori sono deterministici: il test non è casuale. delay01 è uniforme
    // in [0, 1) con sigma 0.289 / 8 = 0.036: la soglia 0.1 vale 2.8 sigma.
    std::array<double, kBipolarFields.size()> sum{};
    double delaySum = 0.0;
    for (int c = 1; c <= kNumConsoleChannels; ++c)
    {
        const auto p = channelProfile(c);
        for (size_t k = 0; k < kBipolarFields.size(); ++k)
        {
            sum[k] += static_cast<double>(p.tol.*kBipolarFields[k]);
        }
        delaySum += static_cast<double>(p.tol.delay01);
    }
    const double n = kNumConsoleChannels;
    for (size_t k = 0; k < kBipolarFields.size(); ++k)
    {
        INFO("bipolar field " << k << " mean " << sum[k] / n);
        CHECK(std::abs(sum[k] / n) < 0.15);
    }
    CHECK(delaySum / n == Approx(0.5).margin(0.1));
}

// I valori golden sono il formato congelato della versione 1: se questo test fallisce, la generazione è
// cambiata e le sessioni salvate non suonerebbero più uguali. Proteggono la riproducibilità tra Windows,
// macOS e Linux. Non aggiornarli: aggiungere una nuova versione dell'algoritmo.
// Calcolati con uno script Python indipendente che emula SplitMix64 e l'aritmetica float32 (numpy), validato
// sul vettore di riferimento di SplitMix64 e sui golden del vecchio formato. Tutti i valori sono
// esattamente rappresentabili in float, quindi il confronto è esatto.
TEST_CASE("channelProfile: golden values for version 1", "[profile]")
{
    const auto c1 = channelProfile(1);
    CHECK(c1.tol.lowShelfDb == 0x1.5aed3cp-2f);
    CHECK(c1.tol.lowShelfOct == 0x1.02321p-2f);
    CHECK(c1.tol.highShelfDb == -0x1.702104p-1f);
    CHECK(c1.tol.highShelfOct == 0x1.93659p-3f);
    CHECK(c1.tol.bellDb == 0x1.3e192p-5f);
    CHECK(c1.tol.bellPos01 == 0x1.589dfp-2f);
    CHECK(c1.tol.delay01 == 0x1.8813bp-2f);
    CHECK(c1.tol.gainDb == -0x1.cad778p-2f);
    CHECK(c1.tol.driveSpread == -0x1.29cddp-4f);
    CHECK(c1.tol.noiseLevel == 0x1.6e2d9p-3f);
    CHECK(c1.noiseSeed == 0x5982CD53u);

    const auto c2 = channelProfile(2);
    CHECK(c2.tol.lowShelfDb == -0x1.0a6accp-1f);
    CHECK(c2.tol.lowShelfOct == -0x1.e8f7ap-4f);
    CHECK(c2.tol.highShelfDb == 0x1.19dfap-5f);
    CHECK(c2.tol.highShelfOct == -0x1.22f30cp-2f);
    CHECK(c2.tol.bellDb == 0x1.3583bp-2f);
    CHECK(c2.tol.bellPos01 == 0x1.f965ecp-2f);
    CHECK(c2.tol.delay01 == 0x1.71164cp-2f);
    CHECK(c2.tol.gainDb == 0x1.9d6afp-2f);
    CHECK(c2.tol.driveSpread == -0x1.4481c8p-1f);
    CHECK(c2.tol.noiseLevel == 0x1.2aa44ap-1f);
    CHECK(c2.noiseSeed == 0x9233D9B5u);

    const auto c64 = channelProfile(64);
    CHECK(c64.tol.lowShelfDb == 0x1.43e5ap-2f);
    CHECK(c64.tol.lowShelfOct == -0x1.11b2e8p-2f);
    CHECK(c64.tol.highShelfDb == 0x1.3eadb6p-1f);
    CHECK(c64.tol.highShelfOct == 0x1.04e6fp-3f);
    CHECK(c64.tol.bellDb == -0x1.ca106cp-2f);
    CHECK(c64.tol.bellPos01 == 0x1.4e2128p-1f);
    CHECK(c64.tol.delay01 == 0x1.935262p-1f);
    CHECK(c64.tol.gainDb == -0x1.a80fd4p-2f);
    CHECK(c64.tol.driveSpread == -0x1.37648ep-1f);
    CHECK(c64.tol.noiseLevel == -0x1.2899fap-1f);
    CHECK(c64.noiseSeed == 0x8364A78Bu);
}

TEST_CASE("channelProfile: channel numbers are limited to 1..64", "[profile]")
{
    CHECK(identical(channelProfile(0), channelProfile(1)));
    CHECK(identical(channelProfile(-7), channelProfile(1)));
    CHECK(identical(channelProfile(65), channelProfile(64)));
    CHECK(identical(channelProfile(100000), channelProfile(64)));
}

TEST_CASE("channelProfile: unknown versions behave like the current one", "[profile]")
{
    const auto ref = channelProfile(17, 1);
    CHECK(identical(channelProfile(17, 0), ref));
    CHECK(identical(channelProfile(17, 99), ref));
    CHECK(identical(channelProfile(17, -3), ref));
    CHECK(identical(channelProfile(17), ref));
}

TEST_CASE("lerp interpolates field by field", "[profile]")
{
    const auto a = channelProfile(1).tol;
    const auto b = channelProfile(2).tol;

    SECTION("endpoints are exact")
    {
        CHECK(identical(lerp(a, b, 0.0f), a));
        CHECK(identical(lerp(a, b, 1.0f), b));
    }

    SECTION("midpoint")
    {
        const auto r = lerp(a, b, 0.5f);
        for (auto f : kAllFields)
        {
            CHECK(r.*f == Approx(0.5f * (a.*f + b.*f)).margin(1e-6));
        }
    }

    SECTION("t is clamped and NaN counts as 0")
    {
        CHECK(identical(lerp(a, b, -3.0f), a));
        CHECK(identical(lerp(a, b, 7.0f), b));
        CHECK(identical(lerp(a, b, std::nanf("")), a));
    }
}
