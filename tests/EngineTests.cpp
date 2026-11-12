// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TestSignals.h"
#include "dsp/DriftEngine.h"
#include "dsp/DspUtils.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace driftbox;
using namespace driftbox::test;
using Catch::Approx;

namespace
{
constexpr double kSr = 48000.0;

EngineSettings makeSettings(int voices, float drift, float tol = 1.0f, float sat = 0.0f, float noise = 0.0f,
                            float gainDb = 0.0f, bool bypass = false)
{
    EngineSettings s;
    s.numVoices = voices;
    s.drift01 = drift;
    s.freqTol01 = s.phaseTol01 = s.gainTol01 = tol;
    s.saturation01 = sat;
    s.noise01 = noise;
    s.outGainDb = gainDb;
    s.bypass = bypass;
    return s;
}

// Elabora in place il tratto [from, to) di un segnale stereo a blocchi di `block` campioni (l'ultimo può
// essere più corto).
void runStereo(DriftEngine& e, Signal& l, Signal& r, size_t from, size_t to, int block)
{
    for (size_t pos = from; pos < to;)
    {
        const int n = static_cast<int>(std::min(static_cast<size_t>(block), to - pos));
        float* chans[2] = {l.data() + pos, r.data() + pos};
        juce::AudioBuffer<float> buf(chans, 2, n);
        e.process(buf);
        pos += static_cast<size_t>(n);
    }
}

void runStereo(DriftEngine& e, Signal& l, Signal& r, int block)
{
    runStereo(e, l, r, 0, l.size(), block);
}

void prepareEngine(DriftEngine& e, int channels = 2, double sr = kSr, std::uint64_t seed = 12345)
{
    e.prepare(sr, 512, channels);
    e.setSeed(seed, false);
}

double maxDelta(const Signal& s, size_t from, size_t to)
{
    double m = 0.0;
    for (size_t i = std::max<size_t>(from, 1); i < std::min(to, s.size()); ++i)
    {
        m = std::max(m, static_cast<double>(std::fabs(s[i] - s[i - 1])));
    }
    return m;
}

bool allFinite(const Signal& s)
{
    return std::all_of(s.begin(), s.end(), [](float v) { return std::isfinite(v); });
}

// Somma dei pesi per lato (stereo: v & 1; mono: solo il lato 0).
double sideSum(const DriftEngine& e, int side)
{
    double sum = 0.0;
    for (int v = side; v < kMaxVoices; v += 2)
    {
        sum += static_cast<double>(e.voiceWeight(v));
    }
    return sum;
}

void checkWeightSums(const DriftEngine& e)
{
    REQUIRE(std::fabs(sideSum(e, 0) - 1.0) < 1.0e-6);
    REQUIRE(std::fabs(sideSum(e, 1) - 1.0) < 1.0e-6);
}

void checkFinalWeights(const DriftEngine& e, int n)
{
    for (int v = 0; v < kMaxVoices; ++v)
    {
        const float expected = v < n ? 2.0f / static_cast<float>(n) : 0.0f;
        REQUIRE(e.voiceWeight(v) == expected);
    }
}

// Risposta in frequenza (DFT in double) a una frequenza qualsiasi.
double responseDb(const Signal& ir, double sampleRate, double hz)
{
    const double w = 2.0 * kPi * hz / sampleRate;
    std::complex<double> acc{0.0, 0.0};
    for (size_t n = 0; n < ir.size(); ++n)
    {
        acc += static_cast<double>(ir[n]) * std::polar(1.0, -w * static_cast<double>(n));
    }
    return 20.0 * std::log10(std::abs(acc));
}
} // namespace

TEST_CASE("Engine output gain: values within half a 0.1 dB step of 0 are exact unity", "[engine]")
{
    // Su macOS il default "0 dB" del parametro arriva come 3.6e-7 dB: deve restare trasparente bit per bit.
    for (float gainDb : {0.0f, 3.6e-7f, -3.6e-7f, 0.049f, -0.049f})
    {
        DriftEngine e;
        prepareEngine(e);
        e.setSettings(makeSettings(2, 0.0f, 1.0f, 1.0f, 1.0f, gainDb));
        auto l = sine(4096, 1000.0, kSr, 0.5f);
        auto r = noise(4096, 7, 0.5f);
        const auto l0 = l, r0 = r;
        runStereo(e, l, r, 256);
        CHECK(l == l0);
        CHECK(r == r0);
    }
}

TEST_CASE("Engine weights: per-side sum is 1 during every N change, final weights are exact", "[engine]")
{
    DriftEngine e;
    prepareEngine(e);
    e.setSettings(makeSettings(2, 0.5f));
    Signal l = sine(32, 1000.0, kSr, 0.5f), r = l;

    // Un sotto-blocco alla volta: ad ogni passo la somma dei pesi per lato vale 1.
    auto step = [&](int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            runStereo(e, l, r, 32);
            checkWeightSums(e);
        }
    };

    step(3);
    checkFinalWeights(e, 2);

    // 2 -> 16, lasciando finire la rampa (50 ms = 75 sotto-blocchi a 48 kHz).
    e.setSettings(makeSettings(16, 0.5f));
    step(80);
    checkFinalWeights(e, 16);

    // 16 -> 4 interrotto dopo 10 sotto-blocchi da 4 -> 8, poi 8 -> 2 a rampa ancora in corso, infine 2 -> 16.
    e.setSettings(makeSettings(4, 0.5f));
    step(10);
    CHECK(e.voiceWeight(0) != 2.0f / 4.0f);
    e.setSettings(makeSettings(8, 0.5f));
    step(7);
    e.setSettings(makeSettings(2, 0.5f));
    step(80);
    checkFinalWeights(e, 2);

    e.setSettings(makeSettings(16, 0.5f));
    step(2);
    e.setSettings(makeSettings(2, 0.5f)); // ritorno a rampa appena iniziata
    step(80);
    checkFinalWeights(e, 2);

    // Le voci oltre N pesano 0 anche per N = 4 e 8.
    for (int n : {4, 8})
    {
        e.setSettings(makeSettings(n, 0.5f));
        step(80);
        checkFinalWeights(e, n);
        for (int v = n; v < kMaxVoices; ++v)
        {
            CHECK(e.voiceWeight(v) == 0.0f);
        }
    }
}

TEST_CASE("Engine accepts any block length, including 0, 1, odd and larger than maxBlockSize", "[engine]")
{
    DriftEngine e;
    prepareEngine(e);
    e.setSettings(makeSettings(8, 1.0f, 1.0f, 0.5f, 0.5f));
    for (int n : {0, 1, 2, 31, 32, 33, 63, 97, 511, 512, 513, 1000, 4096, 5000})
    {
        Signal l = noise(static_cast<size_t>(n), 7u, 0.5f), r = l;
        runStereo(e, l, r, std::max(n, 1));
        CHECK(allFinite(l));
        CHECK(allFinite(r));
    }
}

TEST_CASE("Engine mono: odd voices stay at weight 0 and the output is finite", "[engine]")
{
    DriftEngine e;
    prepareEngine(e, 1);
    e.setSettings(makeSettings(8, 1.0f, 1.0f, 0.3f, 1.0f));
    Signal x = sine(48000, 1000.0, kSr, 0.5f);
    for (size_t pos = 0; pos < x.size(); pos += 32)
    {
        float* chans[1] = {x.data() + pos};
        juce::AudioBuffer<float> buf(chans, 1, 32);
        e.process(buf);
        double sum = 0.0;
        for (int v = 0; v < kMaxVoices; ++v)
        {
            if ((v & 1) != 0)
            {
                REQUIRE(e.voiceWeight(v) == 0.0f);
            }
            sum += static_cast<double>(e.voiceWeight(v));
        }
        REQUIRE(std::fabs(sum - 1.0) < 1.0e-6);
    }
    CHECK(allFinite(x));
    CHECK(rms(x) > 0.2);

    // Cambio di N in mono: le voci dispari restano a 0 durante tutta la rampa.
    e.setSettings(makeSettings(16, 1.0f));
    for (int b = 0; b < 80; ++b)
    {
        float* chans[1] = {x.data()};
        juce::AudioBuffer<float> buf(chans, 1, 32);
        e.process(buf);
        for (int v = 1; v < kMaxVoices; v += 2)
        {
            REQUIRE(e.voiceWeight(v) == 0.0f);
        }
    }
    CHECK(e.voiceWeight(0) == 2.0f / 16.0f);
}

TEST_CASE("Engine bypass: exact at steady state, 20 ms click-free transitions", "[engine]")
{
    const size_t n = 48000;
    Signal inL = sine(n, 1000.0, kSr, 0.5f), inR = sine(n, 700.0, kSr, 0.5f);
    const double inDelta = maxDelta(inL, 0, n);

    DriftEngine e;
    prepareEngine(e);
    e.setSettings(makeSettings(4, 1.0f, 1.0f, 0.5f, 1.0f));

    // Segmenti da 9600 campioni (200 ms): wet, bypass, wet.
    Signal l = inL, r = inR;
    runStereo(e, l, r, 0, 9600, 256);
    // La parte wet differisce dall'ingresso.
    double diff = 0.0;
    for (size_t i = 4800; i < 9600; ++i)
    {
        diff = std::max(diff, static_cast<double>(std::fabs(l[i] - inL[i])));
    }
    CHECK(diff > 1.0e-4);
    const double wetDelta = maxDelta(l, 4800, 9600);

    e.setSettings(makeSettings(4, 1.0f, 1.0f, 0.5f, 1.0f, 0.0f, true));
    runStereo(e, l, r, 9600, 19200, 256);

    // Durata della transizione: ultimo campione che differisce dall'ingresso.
    size_t lastDiff = 0;
    for (size_t i = 9600; i < 19200; ++i)
    {
        if (l[i] != inL[i])
        {
            lastDiff = i;
        }
    }
    const double fadeMs = static_cast<double>(lastDiff - 9600 + 1) / kSr * 1000.0;
    std::printf("[engine] bypass-on transition: %.2f ms\n", fadeMs);
    CHECK(fadeMs > 18.0);
    CHECK(fadeMs < 22.0);
    // A regime l'uscita è esattamente l'ingresso, su entrambi i canali.
    for (size_t i = 9600 + 1100; i < 19200; ++i)
    {
        REQUIRE(l[i] == inL[i]);
        REQUIRE(r[i] == inR[i]);
    }
    // Nessun salto durante la transizione.
    CHECK(maxDelta(l, 9600, 9600 + 1100) <= 1.05 * std::max(wetDelta, inDelta) + 1.0e-4);

    // Smontaggio del bypass.
    e.setSettings(makeSettings(4, 1.0f, 1.0f, 0.5f, 1.0f, 0.0f, false));
    runStereo(e, l, r, 19200, 38400, 256);
    const double offDelta = maxDelta(l, 19200, 19200 + 1100);
    std::printf("[engine] bypass-off max delta %.5f (steady %.5f)\n", offDelta, std::max(wetDelta, inDelta));
    CHECK(offDelta <= 1.05 * std::max(wetDelta, inDelta) + 1.0e-4);
    CHECK(maxDelta(l, 19200 + 1100, 38400) <= 1.05 * std::max(wetDelta, inDelta) + 1.0e-4);
    CHECK(allFinite(l));

    // Bypass dall'inizio: uscita esattamente uguale all'ingresso anche con Drift al 100 %.
    DriftEngine b;
    prepareEngine(b);
    b.setSettings(makeSettings(16, 1.0f, 1.0f, 1.0f, 1.0f, 12.0f, true));
    Signal bl = inL, br = inR;
    runStereo(b, bl, br, 777);
    CHECK(bl == inL);
    CHECK(br == inR);
}

TEST_CASE("Engine Randomize: same seed is a no-op, a new seed fades in about 250 ms", "[engine]")
{
    const size_t n = 48000;
    const Signal in = sine(n, 1000.0, kSr, 0.5f);
    const EngineSettings s = makeSettings(4, 1.0f);

    // Riferimenti: seed A e seed B per tutto il tempo.
    auto run = [&](std::uint64_t seedFirst, std::uint64_t seedSecond, size_t switchAt)
    {
        DriftEngine e;
        prepareEngine(e, 2, kSr, seedFirst);
        e.setSettings(s);
        Signal l = in, r = in;
        runStereo(e, l, r, 0, switchAt, 64);
        e.setSeed(seedSecond, true);
        runStereo(e, l, r, switchAt, n, 64);
        return l;
    };

    const size_t switchAt = 12000;
    const Signal refA = run(100, 100, switchAt); // stesso seed: niente cambia
    const Signal refB = run(200, 200, switchAt);
    const Signal same = run(100, 100, switchAt);
    CHECK(same == refA);

    const Signal fade = run(100, 200, switchAt);
    // Prima del cambio identico ad A.
    for (size_t i = 0; i < switchAt; ++i)
    {
        REQUIRE(fade[i] == refA[i]);
    }
    double maxDiff = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(refA[i] - refB[i])));
    }
    REQUIRE(maxDiff > 1.0e-3);
    // Inizio e fine del fade: quando l'uscita si stacca da A (> 2 % della differenza) e quando raggiunge B.
    size_t first = n, last = 0;
    for (size_t i = switchAt; i < n; ++i)
    {
        if (std::fabs(fade[i] - refA[i]) > 0.02 * maxDiff)
        {
            first = std::min(first, i);
        }
        if (std::fabs(fade[i] - refB[i]) > 0.02 * maxDiff)
        {
            last = i;
        }
    }
    const double fadeMs = static_cast<double>(last - first) / kSr * 1000.0;
    std::printf("[engine] randomize fade (2%%..98%%): %.1f ms, max |A-B| %.4f\n", fadeMs, maxDiff);
    CHECK(fadeMs > 200.0);
    CHECK(fadeMs < 300.0);
    const double steady = std::max(maxDelta(refA, 24000, n), maxDelta(refB, 24000, n));
    CHECK(maxDelta(fade, switchAt, n) <= 1.05 * steady + 1.0e-4);
}

TEST_CASE("Engine is bit-exact deterministic after setSeed(..., false), noise included", "[engine]")
{
    const Signal in = noise(48000, 99u, 0.5f);
    Signal outs[2][2];
    for (auto& o : outs)
    {
        DriftEngine e;
        e.prepare(kSr, 512, 2);
        e.setSeed(0xABCDEF0123ull, false);
        e.setSettings(makeSettings(8, 0.8f, 0.9f, 0.6f, 1.0f, -3.0f));
        o[0] = in;
        o[1] = in;
        runStereo(e, o[0], o[1], 300);
    }
    CHECK(outs[0][0] == outs[1][0]);
    CHECK(outs[0][1] == outs[1][1]);
    CHECK(rms(outs[0][0]) > 0.0);
    // L e R hanno stesso ingresso ma tolleranze diverse.
    CHECK(outs[0][0] != outs[0][1]);
}

TEST_CASE("Engine noise floor is about -84 dBFS RMS, independent of N", "[engine]")
{
    // Più seed: con N = 2 ogni lato ha una sola voce e il suo scarto di livello (+-1.5 dB, limits) arriva
    // direttamente in uscita, quindi si stampa il caso peggiore.
    for (int voices : {2, 4, 8, 16})
    {
        double worstDev = 0.0, sumDb = 0.0;
        for (std::uint64_t seed = 1; seed <= 30; ++seed)
        {
            DriftEngine e;
            prepareEngine(e, 2, kSr, seed * 104729u);
            e.setSettings(makeSettings(voices, 1.0f, 1.0f, 0.0f, 1.0f));
            Signal l(48000, 0.0f), r(48000, 0.0f);
            runStereo(e, l, r, 512);
            const double dbL = toDb(rms(l, 4800)), dbR = toDb(rms(r, 4800));
            worstDev = std::max({worstDev, std::fabs(dbL + 84.0), std::fabs(dbR + 84.0)});
            sumDb += dbL + dbR;
        }
        const double meanDb = sumDb / 60.0;
        std::printf(
            "[engine] noise N=%2d: mean %.2f dBFS, worst |dev from -84| %.2f dB (30 seeds, L and R)\n",
            voices, meanDb, worstDev);
        // Ogni seed e lato entro lo scarto voluto tra le voci piu' 0.5 dB; la media non dipende da N.
        CHECK(worstDev <= static_cast<double>(limits::kNoiseSpreadDb) + 0.5);
        CHECK(std::fabs(meanDb + 84.0) <= 0.5);
    }
}

TEST_CASE("Engine produces L/R differences with Drift > 0 and identical input", "[engine]")
{
    for (int voices : {2, 4, 16})
    {
        DriftEngine e;
        prepareEngine(e);
        e.setSettings(makeSettings(voices, 1.0f));
        Signal l = noise(24000, 5u, 0.5f), r = l;
        runStereo(e, l, r, 512);
        double maxDiff = 0.0;
        for (size_t i = 0; i < l.size(); ++i)
        {
            maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(l[i] - r[i])));
        }
        std::printf("[engine] L/R max difference N=%d: %.5f\n", voices, maxDiff);
        CHECK(maxDiff > 1.0e-3);
    }
}

namespace
{
// Peggior scostamento (dB) della risposta sommata, per lato, tra 20 Hz e 10 kHz, su `seeds` seed.
double worstSummedResponseDb(int voices, float freqTol, float phaseTol, float gainTol, int seeds)
{
    const int len = 16384;
    double worstDb = 0.0;
    for (int seed = 1; seed <= seeds; ++seed)
    {
        DriftEngine e;
        prepareEngine(e, 2, kSr, static_cast<std::uint64_t>(seed) * 7919u);
        EngineSettings s = makeSettings(voices, 1.0f);
        s.freqTol01 = freqTol;
        s.phaseTol01 = phaseTol;
        s.gainTol01 = gainTol;
        e.setSettings(s);
        Signal l = impulse(static_cast<size_t>(len), 0, 0.5f), r = l;
        runStereo(e, l, r, len);
        for (const Signal* ir : {&l, &r})
        {
            Signal scaled = *ir;
            for (auto& v : scaled)
            {
                v *= 2.0f; // impulso di ampiezza 0.5
            }
            for (int k = 0; k <= 60; ++k)
            {
                const double hz = 20.0 * std::pow(500.0, k / 60.0); // 20 Hz .. 10 kHz
                worstDb = std::max(worstDb, std::fabs(responseDb(scaled, kSr, hz)));
            }
        }
    }
    return worstDb;
}
} // namespace

TEST_CASE("Engine phase-only response has no cancellations (within 0.5 dB)", "[engine]")
{
    // Solo ritardi di fase (freq = gain = 0): la media di voci con ritardi diversi non deve cancellare.
    for (int voices : {2, 4, 8, 16})
    {
        const double worst = worstSummedResponseDb(voices, 0.0f, 1.0f, 0.0f, 30);
        std::printf("[engine] phase-only response N=%2d, 30 seeds, 20 Hz..10 kHz: max |dev| = %.4f dB\n",
                    voices, worst);
        CHECK(worst <= 0.5);
    }
}

TEST_CASE("Engine summed response with everything at 100 % stays within the per-voice maximum", "[engine]")
{
    // Limite superiore: massimo teorico di una singola voce = 2 x kMaxShelfDb + kMaxBellDb + kMaxGainDb
    // = 3.75 dB; la media di piu' voci non puo' superarlo.
    constexpr double kUpperBoundDb = 2.0 * limits::kMaxShelfDb + limits::kMaxBellDb + limits::kMaxGainDb;
    for (int voices : {2, 4, 8, 16})
    {
        const double worst = worstSummedResponseDb(voices, 1.0f, 1.0f, 1.0f, 40);
        std::printf(
            "[engine] full response N=%2d, 40 seeds, 20 Hz..10 kHz: max |dev| = %.3f dB (bound %.2f)\n",
            voices, worst, kUpperBoundDb);
        INFO("N = " << voices << ", measured max deviation " << worst << " dB");
        CHECK(worst <= kUpperBoundDb);
    }
}

TEST_CASE("Engine performance: 10 s of audio with 16 voices at maximum settings", "[engine][perf]")
{
    DriftEngine e;
    prepareEngine(e, 2, kSr, 777);
    e.setSettings(makeSettings(16, 1.0f, 1.0f, 1.0f, 1.0f));
    const size_t total = 480000;
    Signal l = noise(total, 3u, 0.3f), r = noise(total, 4u, 0.3f);

    const auto t0 = std::chrono::steady_clock::now();
    runStereo(e, l, r, 512);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("[perf] 10 s of stereo audio, 16 voices: %.3f s (%.2f %% of one core)\n", seconds,
                seconds * 10.0);
    CHECK(allFinite(l));
    CHECK(seconds < 1.0);
}
