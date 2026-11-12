// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TestSignals.h"
#include "dsp/Biquad.h"
#include "dsp/DriftChannel.h"
#include "dsp/DriftProfile.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

using namespace driftbox;
using namespace driftbox::test;
using Catch::Approx;

namespace
{
using Cx = std::complex<double>;

// Risposta in frequenza (DFT in double) della risposta all'impulso a una frequenza qualsiasi.
Cx dftAt(const Signal& ir, double sampleRate, double hz)
{
    const double w = 2.0 * kPi * hz / sampleRate;
    Cx acc{0.0, 0.0};
    for (size_t n = 0; n < ir.size(); ++n)
    {
        acc += static_cast<double>(ir[n]) * std::polar(1.0, -w * static_cast<double>(n));
    }
    return acc;
}

double magDb(const Signal& ir, double sampleRate, double hz)
{
    return 20.0 * std::log10(std::abs(dftAt(ir, sampleRate, hz)));
}

Signal biquadImpulse(const BiquadCoeffs& c, size_t n)
{
    Biquad b;
    b.setCoeffs(c);
    Signal s = impulse(n);
    for (auto& v : s)
    {
        v = b.process(v);
    }
    return s;
}

Signal channelImpulse(DriftChannel& ch, size_t n)
{
    Signal s = impulse(n);
    for (auto& v : s)
    {
        v = ch.processSample(v);
    }
    return s;
}

DriftChannel makeChannel(double sampleRate, std::uint32_t seed = 1u)
{
    DriftChannel ch;
    ch.setNoiseSeed(seed);
    ch.prepare(sampleRate);
    return ch;
}

bool allFinite(const Signal& s)
{
    return std::all_of(s.begin(), s.end(), [](float v) { return std::isfinite(v); });
}

bool sameBits(const Signal& a, const Signal& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

bool isIdentity(const BiquadCoeffs& c)
{
    const auto id = BiquadCoeffs::identity();
    return c.b0 == id.b0 && c.b1 == id.b1 && c.b2 == id.b2 && c.a1 == id.a1 && c.a2 == id.a2;
}

// Ritardo di gruppo in campioni attorno a hz, dalla fase della risposta all'impulso.
double groupDelaySamples(const Signal& ir, double sampleRate, double hz)
{
    const double f1 = hz * 0.98, f2 = hz * 1.02;
    const double p1 = std::arg(dftAt(ir, sampleRate, f1));
    const double p2 = std::arg(dftAt(ir, sampleRate, f2));
    const double dw = 2.0 * kPi * (f2 - f1) / sampleRate;
    return -(p2 - p1) / dw;
}

constexpr double kRates[] = {44100.0, 48000.0, 96000.0, 192000.0};
} // namespace

TEST_CASE("Biquad with zero gain is exactly the identity", "[channel]")
{
    for (double sr : kRates)
    {
        CHECK(isIdentity(BiquadCoeffs::lowShelf(sr, 150.0, 0.7071, 0.0)));
        CHECK(isIdentity(BiquadCoeffs::highShelf(sr, 8000.0, 0.7071, 0.0)));
        CHECK(isIdentity(BiquadCoeffs::peak(sr, 1000.0, 0.7, 0.0)));
    }
}

TEST_CASE("Biquad shelves and bell have the requested response", "[channel]")
{
    const double sr = 48000.0;
    const size_t n = 1 << 16;

    SECTION("low shelf")
    {
        const auto ir = biquadImpulse(BiquadCoeffs::lowShelf(sr, 150.0, 0.7071, 1.0), n);
        CHECK(magDb(ir, sr, 20.0) == Approx(1.0).margin(0.05));
        CHECK(magDb(ir, sr, 10000.0) == Approx(0.0).margin(0.05));
        CHECK(magDb(ir, sr, 150.0) == Approx(0.5).margin(0.05)); // metà del guadagno alla frequenza di centro
    }
    SECTION("high shelf")
    {
        const auto ir = biquadImpulse(BiquadCoeffs::highShelf(sr, 8000.0, 0.7071, -1.0), n);
        CHECK(magDb(ir, sr, 20000.0) == Approx(-1.0).margin(0.1));
        CHECK(magDb(ir, sr, 100.0) == Approx(0.0).margin(0.05));
        CHECK(magDb(ir, sr, 8000.0) == Approx(-0.5).margin(0.05));
    }
    SECTION("bell")
    {
        const auto ir = biquadImpulse(BiquadCoeffs::peak(sr, 1000.0, 4.0, 3.0), n);
        CHECK(magDb(ir, sr, 1000.0) == Approx(3.0).margin(0.02));
        CHECK(magDb(ir, sr, 50.0) == Approx(0.0).margin(0.05));
        CHECK(magDb(ir, sr, 20000.0) == Approx(0.0).margin(0.1));
    }
}

TEST_CASE("Biquad coefficients are finite and stable at every sample rate", "[channel]")
{
    constexpr size_t len = 1 << 18;
    for (double sr : kRates)
    {
        for (double hz : {10.0, 150.0, 8000.0, 0.5 * sr, 3.0 * sr})
        {
            for (double q : {0.0, 0.7071, 4.0})
            {
                for (double gain : {-6.0, 1.0, 6.0})
                {
                    const BiquadCoeffs all[] = {BiquadCoeffs::lowShelf(sr, hz, q, gain),
                                                BiquadCoeffs::highShelf(sr, hz, q, gain),
                                                BiquadCoeffs::peak(sr, hz, q, gain)};
                    for (const auto& c : all)
                    {
                        REQUIRE(std::isfinite(c.b0));
                        REQUIRE(std::isfinite(c.b1));
                        REQUIRE(std::isfinite(c.b2));
                        REQUIRE(std::isfinite(c.a1));
                        REQUIRE(std::isfinite(c.a2));
                        const auto ir = biquadImpulse(c, len);
                        REQUIRE(allFinite(ir));
                        CHECK(rms(ir, len - 1024) < 1.0e-3); // la risposta all'impulso decade
                    }
                }
            }
        }
    }
}

TEST_CASE("DriftChannel is bit-exact transparent with all amounts at zero", "[channel]")
{
    const VoiceAmounts zero{};
    for (int seedIdx = 0; seedIdx < 3; ++seedIdx)
    {
        const auto profile = DriftProfile::generate(0xABCDEF01ull * static_cast<std::uint64_t>(seedIdx + 1));
        for (int v = 0; v < kMaxVoices; v += 5)
        {
            const auto vi = static_cast<size_t>(v);
            for (double sr : {44100.0, 96000.0})
            {
                auto ch = makeChannel(sr, profile.noiseSeeds[vi]);
                ch.update(profile.voices[vi], zero, 3.7f);

                const std::vector<Signal> inputs = {noise(4096, 77u + static_cast<unsigned>(v)),
                                                    sine(4096, 997.0, sr, 0.9f), sine(4096, 60.0, sr, 1.0f)};
                for (const auto& in : inputs)
                {
                    ch.reset();
                    Signal out(in.size());
                    for (size_t i = 0; i < in.size(); ++i)
                    {
                        out[i] = ch.processSample(in[i]);
                    }
                    CHECK(sameBits(in, out));

                    ch.reset();
                    Signal acc(in.size(), 0.0f);
                    ch.processAdd(in.data(), acc.data(), static_cast<int>(in.size()), 1.0f, 1.0f);
                    CHECK(sameBits(in, acc));
                }
            }
        }
    }
}

TEST_CASE("DriftChannel gain deviation is exact", "[channel]")
{
    const double sr = 48000.0;
    const auto in = noise(1 << 15, 5u, 0.3f);
    for (float sign : {1.0f, -1.0f})
    {
        auto ch = makeChannel(sr);
        VoiceTolerance tol;
        tol.gainDb = sign;
        VoiceAmounts a;
        a.gain = 1.0f;
        ch.update(tol, a, 1.0f);
        Signal out(in.size());
        for (size_t i = 0; i < in.size(); ++i)
        {
            out[i] = ch.processSample(in[i]);
        }
        CHECK(toDb(rms(out) / rms(in)) ==
              Approx(static_cast<double>(sign) * limits::kMaxGainDb).margin(0.01));
    }
}

TEST_CASE("DriftChannel phase stage is a flat, positive fractional delay", "[channel]")
{
    VoiceAmounts a;
    a.phase = 1.0f;

    for (double sr : {48000.0, 96000.0})
    {
        auto ch = makeChannel(sr);
        VoiceTolerance tol;
        tol.delay01 = 1.0f;
        ch.update(tol, a, 1.0f);
        const auto ir = channelImpulse(ch, 4096);

        for (double f : {50.0, 500.0, 2000.0, 8000.0, 16000.0})
        {
            CHECK(magDb(ir, sr, f) == Approx(0.0).margin(0.001));
        }
        const double expected = static_cast<double>(limits::kMaxDelayUs) * 1.0e-6 * sr;
        CHECK(groupDelaySamples(ir, sr, 100.0) == Approx(expected).epsilon(0.05));
    }

    SECTION("delay is never negative")
    {
        const double sr = 48000.0;
        for (float delay01 : {0.0f, 0.01f, 0.25f, 0.5f, 1.0f})
        {
            auto ch = makeChannel(sr);
            VoiceTolerance tol;
            tol.delay01 = delay01;
            ch.update(tol, a, 1.0f);
            const auto ir = channelImpulse(ch, 4096);
            for (double f : {20.0, 100.0, 1000.0, 10000.0})
            {
                CHECK(groupDelaySamples(ir, sr, f) >= -1.0e-6);
            }
        }
    }
}

TEST_CASE("DriftChannel frequency deviation stays within the limits", "[channel]")
{
    const double sr = 48000.0;
    const size_t n = 1 << 16;
    const double maxDev = static_cast<double>(2.0f * limits::kMaxShelfDb + limits::kMaxBellDb) + 0.05;

    for (float sign : {1.0f, -1.0f})
    {
        for (float pos : {0.0f, 0.5f, 1.0f})
        {
            for (float oct : {-1.0f, 0.0f, 1.0f})
            {
                auto ch = makeChannel(sr);
                VoiceTolerance tol;
                tol.lowShelfDb = tol.highShelfDb = tol.bellDb = sign;
                tol.lowShelfOct = tol.highShelfOct = oct;
                tol.bellPos01 = pos;
                VoiceAmounts a;
                a.freq = 1.0f;
                ch.update(tol, a, 1.0f);
                const auto ir = channelImpulse(ch, n);
                for (double f = 20.0; f <= 20000.0; f *= 1.12)
                {
                    REQUIRE(std::abs(magDb(ir, sr, f)) <= maxDev);
                }
            }
        }
    }

    SECTION("freq = 0 gives zero deviation")
    {
        auto ch = makeChannel(sr);
        VoiceTolerance tol;
        tol.lowShelfDb = tol.highShelfDb = tol.bellDb = 1.0f;
        VoiceAmounts a;
        ch.update(tol, a, 1.0f);
        const auto ir = channelImpulse(ch, 4096);
        for (double f : {20.0, 1000.0, 20000.0})
        {
            CHECK(magDb(ir, sr, f) == Approx(0.0).margin(1.0e-9));
        }
    }
}

TEST_CASE("DriftChannel saturation is gentle and bounded", "[channel]")
{
    const double sr = 48000.0;
    VoiceTolerance tol;
    VoiceAmounts a;
    a.saturation = 1.0f;

    SECTION("small signals have unity gain")
    {
        auto ch = makeChannel(sr);
        ch.update(tol, a, 1.0f);
        const auto in = sine(1 << 14, 1000.0, sr, 0.001f); // -60 dBFS
        Signal out(in.size());
        for (size_t i = 0; i < in.size(); ++i)
        {
            out[i] = ch.processSample(in[i]);
        }
        CHECK(toDb(rms(out) / rms(in)) == Approx(0.0).margin(0.01));
    }
    SECTION("full-scale sine: peak <= 1 and odd harmonics appear")
    {
        auto ch = makeChannel(sr);
        ch.update(tol, a, 1.0f);
        const size_t n = 1 << 16;
        const auto in = sine(n, 1500.0, sr, 1.0f); // 1500 Hz cade esattamente sul bin 2048
        Signal out(n);
        float peak = 0.0f;
        for (size_t i = 0; i < n; ++i)
        {
            out[i] = ch.processSample(in[i]);
            peak = std::max(peak, std::abs(out[i]));
        }
        CHECK(peak <= 1.0f);
        const auto mag = fftMagnitude(out);
        CHECK(toDb(mag[3 * 2048] / mag[2048]) > -80.0);
        CHECK(toDb(mag[2 * 2048] / mag[2048]) < -80.0); // niente armoniche pari
    }
    SECTION("output stays finite with huge input")
    {
        auto ch = makeChannel(sr);
        VoiceTolerance t2;
        t2.lowShelfDb = t2.highShelfDb = t2.bellDb = 1.0f;
        VoiceAmounts a2 = a;
        a2.freq = 1.0f;
        ch.update(t2, a2, 1.0f);
        Signal out(256);
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = ch.processSample(i % 2 == 0 ? 1.0e6f : -1.0e6f);
        }
        CHECK(allFinite(out));
    }
}

namespace
{
double noiseRmsDb(double sr, float noiseLevel, float noiseScale, std::uint32_t seed = 99u)
{
    auto ch = makeChannel(sr, seed);
    VoiceTolerance tol;
    tol.noiseLevel = noiseLevel;
    VoiceAmounts a;
    a.noise = 1.0f;
    ch.update(tol, a, noiseScale);
    const size_t n = 1 << 20;
    Signal out(n);
    for (auto& v : out)
    {
        v = ch.processSample(0.0f);
    }
    return toDb(rms(out));
}
} // namespace

TEST_CASE("DriftChannel noise has the target RMS", "[channel]")
{
    for (double sr : {48000.0, 96000.0})
    {
        const double base = noiseRmsDb(sr, 0.0f, 1.0f);
        CHECK(base == Approx(static_cast<double>(limits::kNoiseMaxDbfs)).margin(0.5));
        CHECK(noiseRmsDb(sr, 0.0f, 2.0f) - base == Approx(6.02).margin(0.1));
        CHECK(noiseRmsDb(sr, 1.0f, 1.0f) - base == Approx(1.5).margin(0.1));
        CHECK(noiseRmsDb(sr, -1.0f, 1.0f) - base == Approx(-1.5).margin(0.1));
    }
}

TEST_CASE("DriftChannel noise is reproducible and seed dependent", "[channel]")
{
    VoiceTolerance tol;
    VoiceAmounts a;
    a.noise = 1.0f;
    const auto run = [](DriftChannel& ch)
    {
        Signal out(2048);
        for (auto& v : out)
        {
            v = ch.processSample(0.0f);
        }
        return out;
    };

    auto ch = makeChannel(48000.0, 1234u);
    ch.update(tol, a, 1.0f);
    const auto first = run(ch);
    const auto second = run(ch);
    CHECK_FALSE(sameBits(first, second));
    ch.reset();
    CHECK(sameBits(first, run(ch)));

    auto other = makeChannel(48000.0, 4321u);
    other.update(tol, a, 1.0f);
    CHECK_FALSE(sameBits(first, run(other)));
}

TEST_CASE("DriftChannel without noise does not advance the generator", "[channel]")
{
    VoiceTolerance tol;
    VoiceAmounts quiet;
    VoiceAmounts loud;
    loud.noise = 1.0f;

    auto a = makeChannel(48000.0, 555u);
    auto b = makeChannel(48000.0, 555u);
    a.update(tol, quiet, 1.0f);
    for (int i = 0; i < 1000; ++i)
    {
        CHECK(a.processSample(0.0f) == 0.0f);
    }
    a.update(tol, loud, 1.0f);
    b.update(tol, loud, 1.0f);
    for (int i = 0; i < 100; ++i)
    {
        CHECK(a.processSample(0.0f) == b.processSample(0.0f));
    }
}

TEST_CASE("DriftChannel processAdd accumulates with a linear weight ramp", "[channel]")
{
    const double sr = 48000.0;
    const int n = 512;
    const auto in = noise(static_cast<size_t>(n), 3u, 0.5f);

    const auto setup = [](DriftChannel& ch)
    {
        VoiceTolerance tol;
        tol.lowShelfDb = 1.0f;
        tol.bellDb = -1.0f;
        tol.delay01 = 0.7f;
        tol.gainDb = 0.5f;
        VoiceAmounts a;
        a.freq = a.phase = a.gain = 1.0f;
        a.saturation = 0.5f;
        a.noise = 1.0f;
        ch.update(tol, a, 2.0f);
    };

    auto ref = makeChannel(sr, 42u);
    auto dut = makeChannel(sr, 42u);
    setup(ref);
    setup(dut);

    const float w0 = 0.25f, w1 = 0.75f;
    Signal acc(static_cast<size_t>(n));
    for (size_t i = 0; i < acc.size(); ++i)
    {
        acc[i] = 0.1f * static_cast<float>(i % 7) - 0.3f;
    }
    const Signal start = acc;
    dut.processAdd(in.data(), acc.data(), n, w0, w1);

    for (int i = 0; i < n; ++i)
    {
        const auto ui = static_cast<size_t>(i);
        const float w = w0 + (w1 - w0) * static_cast<float>(i) / static_cast<float>(n);
        const float expected = start[ui] + w * ref.processSample(in[ui]);
        REQUIRE(acc[ui] == Approx(expected).margin(1.0e-5));
    }

    SECTION("n = 0 does nothing")
    {
        Signal untouched = start;
        dut.processAdd(in.data(), untouched.data(), 0, 1.0f, 1.0f);
        CHECK(sameBits(untouched, start));
    }
}
