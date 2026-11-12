// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TestSignals.h"
#include "dsp/DriftEngine.h"
#include "dsp/DspUtils.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <random>
#include <utility>
#include <vector>

using namespace driftbox;
using namespace driftbox::test;
using Catch::Approx;

namespace
{
constexpr double kSr = 48000.0;

EngineSettings makeSettings(int chanL, int chanR, float drift, float tol = 1.0f, float sat = 0.0f,
                            float noise = 0.0f, float gainDb = 0.0f, bool bypass = false)
{
    EngineSettings s;
    s.channelL = chanL;
    s.channelR = chanR;
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

void prepareEngine(DriftEngine& e, int channels = 2, double sr = kSr)
{
    e.prepare(sr, 512, channels);
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

// Numero di sotto-blocchi da 32 campioni dopo i quali il fade del lato finisce (con un massimo di sicurezza).
int blocksUntilFadeEnds(DriftEngine& e, int side, int maxBlocks = 1000)
{
    Signal l(DriftEngine::kControlInterval, 0.25f), r = l;
    for (int b = 1; b <= maxBlocks; ++b)
    {
        runStereo(e, l, r, DriftEngine::kControlInterval);
        if (!e.isFading(side))
        {
            return b;
        }
    }
    return -1;
}
} // namespace

TEST_CASE("Engine output gain: values within half a 0.1 dB step of 0 are exact unity", "[engine]")
{
    // Su macOS il default "0 dB" del parametro arriva come 3.6e-7 dB: deve restare trasparente bit per bit.
    for (float gainDb : {0.0f, 3.6e-7f, -3.6e-7f, 0.049f, -0.049f})
    {
        DriftEngine e;
        prepareEngine(e);
        e.setSettings(makeSettings(3, 40, 0.0f, 1.0f, 1.0f, 1.0f, gainDb));
        auto l = sine(4096, 1000.0, kSr, 0.5f);
        auto r = noise(4096, 7, 0.5f);
        const auto l0 = l, r0 = r;
        runStereo(e, l, r, 256);
        CHECK(l == l0);
        CHECK(r == r0);
    }
}

TEST_CASE("Engine accepts any block length, including 0, 1, odd and larger than maxBlockSize", "[engine]")
{
    DriftEngine e;
    prepareEngine(e);
    e.setSettings(makeSettings(8, 50, 1.0f, 1.0f, 0.5f, 0.5f));
    for (int n : {0, 1, 2, 31, 32, 33, 63, 97, 511, 512, 513, 1000, 4096, 5000})
    {
        Signal l = noise(static_cast<size_t>(n), 7u, 0.5f), r = l;
        runStereo(e, l, r, std::max(n, 1));
        CHECK(allFinite(l));
        CHECK(allFinite(r));
    }
}

TEST_CASE("Engine reports the target channel of each side", "[engine]")
{
    DriftEngine e;
    prepareEngine(e);
    CHECK(e.channelOf(0) == 1);
    CHECK(e.channelOf(1) == 2);
    CHECK_FALSE(e.isFading(0));
    CHECK_FALSE(e.isFading(1));

    e.setSettings(makeSettings(12, 63, 1.0f));
    Signal l(64, 0.1f), r = l;
    runStereo(e, l, r, 64);
    CHECK(e.channelOf(0) == 12);
    CHECK(e.channelOf(1) == 63);
    // Il primo blocco dopo prepare() non fa fade: i valori sono i target.
    CHECK_FALSE(e.isFading(0));
    CHECK_FALSE(e.isFading(1));

    // Numeri fuori range vengono limitati a 1..64.
    e.setSettings(makeSettings(0, 100, 1.0f));
    runStereo(e, l, r, 64);
    CHECK(e.channelOf(0) == 1);
    CHECK(e.channelOf(1) == 64);
}

TEST_CASE("Engine channel change: about 250 ms fade, independent sides", "[engine]")
{
    const size_t n = 48000, switchAt = 9600;
    const Signal in = sine(n, 1000.0, kSr, 0.5f);
    const EngineSettings before = makeSettings(5, 9, 1.0f);
    EngineSettings after = before;
    after.channelL = 40;

    auto run = [&](const EngineSettings& second, Signal& l, Signal& r)
    {
        DriftEngine e;
        prepareEngine(e);
        e.setSettings(before);
        l = in;
        r = in;
        runStereo(e, l, r, 0, switchAt, 64);
        e.setSettings(second);
        runStereo(e, l, r, switchAt, n, 64);
    };
    Signal refL, refR, chL, chR;
    run(before, refL, refR);
    run(after, chL, chR);

    // R non è toccato: bit-identico al riferimento. L cambia.
    CHECK(chR == refR);
    CHECK(chL != refL);
    for (size_t i = 0; i < switchAt; ++i)
    {
        REQUIRE(chL[i] == refL[i]);
    }

    // Durata del fade: 250 ms = 375 sotto-blocchi da 32 campioni.
    DriftEngine e;
    prepareEngine(e);
    e.setSettings(before);
    Signal l = in, r = in;
    runStereo(e, l, r, 0, 4096, 64);
    e.setSettings(after);
    Signal tmpL(32, 0.25f), tmpR = tmpL;
    runStereo(e, tmpL, tmpR, 32);
    CHECK(e.isFading(0));
    CHECK_FALSE(e.isFading(1));
    CHECK(e.channelOf(0) == 40);
    CHECK(e.channelOf(1) == 9);
    const int blocks = 1 + blocksUntilFadeEnds(e, 0);
    const double fadeMs = blocks * 32.0 / kSr * 1000.0;
    std::printf("[engine] channel fade: %.1f ms\n", fadeMs);
    CHECK(fadeMs > 245.0);
    CHECK(fadeMs < 256.0);

    // Senza click: il delta campione-campione non supera quello di regime.
    double steady = 0.0;
    for (int c : {5, 40})
    {
        DriftEngine s;
        prepareEngine(s);
        s.setSettings(makeSettings(c, 9, 1.0f));
        Signal sl = in, sr2 = in;
        runStereo(s, sl, sr2, 64);
        steady = std::max(steady, maxDelta(sl, 4800, n));
    }
    const double during = maxDelta(chL, switchAt, n);
    std::printf("[engine] channel change max delta %.5f (steady %.5f)\n", during, steady);
    CHECK(during <= 1.05 * steady + 1.0e-4);

    // A fade concluso l'uscita ha il livello di regime del nuovo canale.
    DriftEngine s;
    prepareEngine(s);
    s.setSettings(makeSettings(40, 9, 1.0f));
    Signal sl = in, sr2 = in;
    runStereo(s, sl, sr2, 64);
    CHECK(toDb(rms(chL, n - 4800, n)) == Approx(toDb(rms(sl, n - 4800, n))).margin(0.01));
}

TEST_CASE("Engine channel change during a fade restarts from the current value", "[engine]")
{
    const size_t n = 48000;
    const Signal in = sine(n, 1000.0, kSr, 0.5f);
    DriftEngine e;
    prepareEngine(e);
    e.setSettings(makeSettings(5, 9, 1.0f));
    Signal l = in, r = in;
    runStereo(e, l, r, 0, 4800, 64);
    e.setSettings(makeSettings(40, 9, 1.0f));
    runStereo(e, l, r, 4800, 4800 + 4800, 64); // 100 ms: fade a metà
    CHECK(e.isFading(0));
    e.setSettings(makeSettings(7, 9, 1.0f)); // nuovo cambio a fade in corso, e infine un altro
    runStereo(e, l, r, 9600, 9600 + 1920, 64);
    CHECK(e.isFading(0));
    e.setSettings(makeSettings(60, 9, 1.0f));
    runStereo(e, l, r, 11520, n, 64);
    CHECK_FALSE(e.isFading(0));
    CHECK(e.channelOf(0) == 60);

    double steady = 0.0;
    for (int c : {5, 40, 7, 60})
    {
        DriftEngine s;
        prepareEngine(s);
        s.setSettings(makeSettings(c, 9, 1.0f));
        Signal sl = in, sr2 = in;
        runStereo(s, sl, sr2, 64);
        steady = std::max(steady, maxDelta(sl, 4800, n));
    }
    CHECK(maxDelta(l, 4800, n) <= 1.05 * steady + 1.0e-4);

    DriftEngine s;
    prepareEngine(s);
    s.setSettings(makeSettings(60, 9, 1.0f));
    Signal sl = in, sr2 = in;
    runStereo(s, sl, sr2, 64);
    CHECK(toDb(rms(l, n - 4800, n)) == Approx(toDb(rms(sl, n - 4800, n))).margin(0.01));
}

TEST_CASE("Engine channel changes of the two sides fade independently", "[engine]")
{
    DriftEngine e;
    prepareEngine(e);
    e.setSettings(makeSettings(5, 9, 1.0f));
    Signal l(64, 0.2f), r = l;
    runStereo(e, l, r, 64);

    e.setSettings(makeSettings(6, 9, 1.0f)); // solo L
    runStereo(e, l, r, 32);
    CHECK(e.isFading(0));
    CHECK_FALSE(e.isFading(1));

    // R cambia 100 ms dopo: i due fade sono sfasati e finiscono in momenti diversi.
    for (int b = 0; b < 150; ++b)
    {
        runStereo(e, l, r, 32);
    }
    e.setSettings(makeSettings(6, 20, 1.0f));
    runStereo(e, l, r, 32);
    CHECK(e.isFading(0));
    CHECK(e.isFading(1));
    const int endL = blocksUntilFadeEnds(e, 0);
    CHECK(e.isFading(1));
    const int endR = blocksUntilFadeEnds(e, 1);
    CHECK(endL > 0);
    CHECK(endR > 0);
    std::printf("[engine] independent fades: L ends %d blocks after R started, R %d blocks later\n", endL,
                endR);
}

TEST_CASE("Engine reset() makes the next channel change immediate", "[engine]")
{
    DriftEngine e;
    prepareEngine(e);
    e.setSettings(makeSettings(5, 9, 1.0f));
    Signal l(64, 0.2f), r = l;
    runStereo(e, l, r, 64);
    e.setSettings(makeSettings(6, 10, 1.0f));
    runStereo(e, l, r, 32);
    CHECK(e.isFading(0));
    e.reset();
    runStereo(e, l, r, 32);
    CHECK_FALSE(e.isFading(0));
    CHECK_FALSE(e.isFading(1));
    CHECK(e.channelOf(0) == 6);
    CHECK(e.channelOf(1) == 10);
}

TEST_CASE("Engine mono: only the left side is processed", "[engine]")
{
    const Signal x = sine(48000, 1000.0, kSr, 0.5f);

    DriftEngine mono;
    prepareEngine(mono, 1);
    mono.setSettings(makeSettings(21, 50, 1.0f, 1.0f, 0.3f, 1.0f));
    Signal m = x;
    for (size_t pos = 0; pos < m.size(); pos += 32)
    {
        float* chans[1] = {m.data() + pos};
        juce::AudioBuffer<float> buf(chans, 1, 32);
        mono.process(buf);
    }
    CHECK(allFinite(m));
    CHECK(rms(m) > 0.2);
    CHECK(mono.channelOf(0) == 21);
    CHECK_FALSE(mono.isFading(1));

    // Lo stesso del lato L di un motore stereo, bit per bit.
    DriftEngine stereo;
    prepareEngine(stereo, 2);
    stereo.setSettings(makeSettings(21, 50, 1.0f, 1.0f, 0.3f, 1.0f));
    Signal l = x, r = x;
    runStereo(stereo, l, r, 32);
    CHECK(m == l);

    // Cambio di canale in mono: fade solo sul lato 0.
    mono.setSettings(makeSettings(22, 50, 1.0f, 1.0f, 0.3f, 1.0f));
    float* chans[1] = {m.data()};
    juce::AudioBuffer<float> buf(chans, 1, 32);
    mono.process(buf);
    CHECK(mono.isFading(0));
    CHECK_FALSE(mono.isFading(1));
}

TEST_CASE("Engine bypass: exact at steady state, 20 ms click-free transitions", "[engine]")
{
    const size_t n = 48000;
    Signal inL = sine(n, 1000.0, kSr, 0.5f), inR = sine(n, 700.0, kSr, 0.5f);
    const double inDelta = maxDelta(inL, 0, n);

    DriftEngine e;
    prepareEngine(e);
    e.setSettings(makeSettings(4, 27, 1.0f, 1.0f, 0.5f, 1.0f));

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

    e.setSettings(makeSettings(4, 27, 1.0f, 1.0f, 0.5f, 1.0f, 0.0f, true));
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
    e.setSettings(makeSettings(4, 27, 1.0f, 1.0f, 0.5f, 1.0f, 0.0f, false));
    runStereo(e, l, r, 19200, 38400, 256);
    const double offDelta = maxDelta(l, 19200, 19200 + 1100);
    std::printf("[engine] bypass-off max delta %.5f (steady %.5f)\n", offDelta, std::max(wetDelta, inDelta));
    CHECK(offDelta <= 1.05 * std::max(wetDelta, inDelta) + 1.0e-4);
    CHECK(maxDelta(l, 19200 + 1100, 38400) <= 1.05 * std::max(wetDelta, inDelta) + 1.0e-4);
    CHECK(allFinite(l));

    // Bypass dall'inizio: uscita esattamente uguale all'ingresso anche con Drift al 100 %.
    DriftEngine b;
    prepareEngine(b);
    b.setSettings(makeSettings(64, 1, 1.0f, 1.0f, 1.0f, 1.0f, 12.0f, true));
    Signal bl = inL, br = inR;
    runStereo(b, bl, br, 777);
    CHECK(bl == inL);
    CHECK(br == inR);
}

TEST_CASE("Engine is bit-exact deterministic after prepare, noise included", "[engine]")
{
    const Signal in = noise(48000, 99u, 0.5f);
    Signal outs[2][2];
    for (auto& o : outs)
    {
        DriftEngine e;
        e.prepare(kSr, 512, 2);
        e.setSettings(makeSettings(33, 12, 0.8f, 0.9f, 0.6f, 1.0f, -3.0f));
        o[0] = in;
        o[1] = in;
        runStereo(e, o[0], o[1], 300);
    }
    CHECK(outs[0][0] == outs[1][0]);
    CHECK(outs[0][1] == outs[1][1]);
    CHECK(rms(outs[0][0]) > 0.0);
    // L e R hanno stesso ingresso ma canali diversi.
    CHECK(outs[0][0] != outs[0][1]);
}

TEST_CASE("Engine noise floor is about -84 dBFS RMS per side, whatever the channel", "[engine]")
{
    // Tutti i 64 canali: il livello di ogni canale è -84 dBFS più il suo scarto (+-1.5 dB, limits), quindi il
    // caso peggiore sta entro lo scarto voluto più 0.5 dB; la media dei 64 canali non dipende dalla scelta.
    double worstDev = 0.0, sumDb = 0.0;
    int measured = 0;
    for (int c = 1; c <= kNumConsoleChannels / 2; ++c)
    {
        DriftEngine e;
        prepareEngine(e);
        e.setSettings(makeSettings(c, c + kNumConsoleChannels / 2, 1.0f, 1.0f, 0.0f, 1.0f));
        Signal l(48000, 0.0f), r(48000, 0.0f);
        runStereo(e, l, r, 512);
        for (const Signal* s : {&l, &r})
        {
            const double db = toDb(rms(*s, 4800));
            worstDev = std::max(worstDev, std::fabs(db + 84.0));
            sumDb += db;
            ++measured;
        }
    }
    const double meanDb = sumDb / measured;
    std::printf("[engine] noise, %d channels: mean %.2f dBFS, worst |dev from -84| %.2f dB\n", measured,
                meanDb, worstDev);
    CHECK(measured == kNumConsoleChannels);
    CHECK(worstDev <= static_cast<double>(limits::kNoiseSpreadDb) + 0.5);
    CHECK(std::fabs(meanDb + 84.0) <= 0.5);
}

TEST_CASE("Engine L and R differ for different channels, and match for the same channel", "[engine]")
{
    for (const auto& [cl, cr] : {std::pair{1, 2}, std::pair{10, 50}, std::pair{64, 1}})
    {
        DriftEngine e;
        prepareEngine(e);
        e.setSettings(makeSettings(cl, cr, 1.0f));
        Signal l = noise(24000, 5u, 0.5f), r = l;
        runStereo(e, l, r, 512);
        double maxDiff = 0.0;
        for (size_t i = 0; i < l.size(); ++i)
        {
            maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(l[i] - r[i])));
        }
        std::printf("[engine] L/R max difference, channels %d/%d: %.5f\n", cl, cr, maxDiff);
        CHECK(maxDiff > 1.0e-3);
    }

    // Stesso canale e niente rumore: le due catene sono identiche, quindi L == R bit per bit.
    for (int c : {1, 33, 64})
    {
        DriftEngine e;
        prepareEngine(e);
        e.setSettings(makeSettings(c, c, 1.0f, 1.0f, 0.7f, 0.0f));
        Signal l = noise(24000, 5u, 0.5f), r = l;
        runStereo(e, l, r, 512);
        CHECK(l == r);
        CHECK(rms(l) > 0.1);
    }
}

namespace
{
// Peggior scostamento (dB) tra 20 Hz e 10 kHz della somma mono (L + R) / 2 con ingresso identico, per le
// coppie date, con le sole deviazioni di fase al 100 % e Drift al 100 %.
double worstSummedPhaseResponseDb(const std::vector<std::pair<int, int>>& pairs)
{
    const int len = 2048;
    double worstDb = 0.0;
    for (const auto& [cl, cr] : pairs)
    {
        DriftEngine e;
        prepareEngine(e);
        EngineSettings s = makeSettings(cl, cr, 1.0f);
        s.freqTol01 = 0.0f;
        s.gainTol01 = 0.0f;
        s.phaseTol01 = 1.0f;
        e.setSettings(s);
        Signal l = impulse(static_cast<size_t>(len), 0, 0.5f), r = l;
        runStereo(e, l, r, len);
        Signal sum(l.size());
        for (size_t i = 0; i < sum.size(); ++i)
        {
            sum[i] = (l[i] + r[i]) * 0.5f * 2.0f; // (L + R) / 2, normalizzato all'ampiezza 0.5 dell'impulso
        }
        for (int k = 0; k <= 60; ++k)
        {
            const double hz = 20.0 * std::pow(500.0, k / 60.0); // 20 Hz .. 10 kHz
            worstDb = std::max(worstDb, std::fabs(responseDb(sum, kSr, hz)));
        }
    }
    return worstDb;
}
} // namespace

TEST_CASE("Mono sum of L and R with phase deviations only has no cancellations (within 0.5 dB)", "[engine]")
{
    // Ritardi di fase sempre positivi e piccoli (max 5 µs): la somma di due canali diversi non cancella.
    std::vector<std::pair<int, int>> adjacent, random;
    for (int c = 1; c <= kNumConsoleChannels; ++c)
    {
        adjacent.push_back({c, c % kNumConsoleChannels + 1});
    }
    std::mt19937 gen(20261112);
    std::uniform_int_distribution<int> pick(1, kNumConsoleChannels);
    for (int i = 0; i < 64; ++i)
    {
        random.push_back({pick(gen), pick(gen)});
    }
    const double worstAdjacent = worstSummedPhaseResponseDb(adjacent);
    const double worstRandom = worstSummedPhaseResponseDb(random);
    std::printf("[engine] mono-sum phase-only response, 20 Hz..10 kHz: adjacent pairs max |dev| = %.4f dB, "
                "random pairs %.4f dB\n",
                worstAdjacent, worstRandom);
    CHECK(worstAdjacent <= 0.5);
    CHECK(worstRandom <= 0.5);
}

TEST_CASE("Engine response with everything at 100 % stays within the per-channel maximum", "[engine]")
{
    // Limite superiore: massimo teorico di un canale = 2 x kMaxShelfDb + kMaxBellDb + kMaxGainDb = 3.75 dB.
    constexpr double kUpperBoundDb = 2.0 * limits::kMaxShelfDb + limits::kMaxBellDb + limits::kMaxGainDb;
    const int len = 4096;
    double worstDb = 0.0;
    for (int c = 1; c <= kNumConsoleChannels; c += 2)
    {
        DriftEngine e;
        prepareEngine(e);
        e.setSettings(makeSettings(c, c + 1, 1.0f));
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
                const double hz = 20.0 * std::pow(500.0, k / 60.0);
                worstDb = std::max(worstDb, std::fabs(responseDb(scaled, kSr, hz)));
            }
        }
    }
    std::printf("[engine] full response, 64 channels, 20 Hz..10 kHz: max |dev| = %.3f dB (bound %.2f)\n",
                worstDb, kUpperBoundDb);
    CHECK(worstDb <= kUpperBoundDb);
}

TEST_CASE("Engine performance: 10 s of audio with two chains at maximum settings", "[engine][perf]")
{
    DriftEngine e;
    prepareEngine(e);
    e.setSettings(makeSettings(17, 18, 1.0f, 1.0f, 1.0f, 1.0f));
    const size_t total = 480000;
    Signal l = noise(total, 3u, 0.3f), r = noise(total, 4u, 0.3f);

    const auto t0 = std::chrono::steady_clock::now();
    runStereo(e, l, r, 512);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("[perf] 10 s of stereo audio, 2 chains: %.3f s (%.2f %% of one core)\n", seconds,
                seconds * 10.0);
    CHECK(allFinite(l));
    CHECK(seconds < 0.25);
}
