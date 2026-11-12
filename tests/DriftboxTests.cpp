// SPDX-License-Identifier: AGPL-3.0-or-later
// Test di accettazione di Driftbox: trasparenza, niente NaN/Inf, cambio canali senza click e niente
// clipping (T3), stato e seed (T4).
#include "PluginProcessor.h"
#include "dsp/DriftEngine.h"
#include "dsp/DspUtils.h"
#include "TestSignals.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <utility>
#include <vector>

using namespace driftbox;
using Catch::Approx;

TEST_CASE("Processor processes a block and stays finite", "[smoke]")
{
    DriftboxAudioProcessor p;
    p.setPlayConfigDetails(2, 2, 48000.0, 512);
    p.prepareToPlay(48000.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);
    const auto sig = test::sine(512, 1000.0, 48000.0, 0.5f);
    for (int ch = 0; ch < 2; ++ch)
    {
        buffer.copyFrom(ch, 0, sig.data(), 512);
    }
    juce::MidiBuffer midi;
    p.processBlock(buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
    {
        for (int i = 0; i < 512; ++i)
        {
            REQUIRE(std::isfinite(buffer.getSample(ch, i)));
        }
    }
    p.releaseResources();
}

TEST_CASE("Soft ceiling: transparent below -1 dBFS, never above 0 dBFS", "[dsputils]")
{
    for (float x = -kCeilingThreshold; x <= kCeilingThreshold; x += 0.001f)
    {
        CHECK(softCeiling(x) == x);
    }
    for (float x : {0.9f, 1.0f, 2.0f, 10.0f, 1.0e6f})
    {
        CHECK(softCeiling(x) <= 1.0f);
        CHECK(softCeiling(-x) >= -1.0f);
        CHECK(softCeiling(x) > kCeilingThreshold);
    }
    // Monotono sopra la soglia.
    float prev = softCeiling(kCeilingThreshold);
    for (float x = kCeilingThreshold; x < 4.0f; x += 0.01f)
    {
        const float y = softCeiling(x);
        CHECK(y >= prev);
        prev = y;
    }
}

TEST_CASE("Sanitize replaces NaN and Inf", "[dsputils]")
{
    CHECK(sanitize(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    CHECK(sanitize(std::numeric_limits<float>::infinity()) == 0.0f);
    CHECK(sanitize(-std::numeric_limits<float>::infinity()) == 0.0f);
    CHECK(sanitize(1.0e30f) == Approx(kSanitizeLimit));
    CHECK(sanitize(0.25f) == 0.25f);
}

namespace
{
EngineSettings makeSettings(int voices, float drift, float tol = 1.0f, float sat = 0.0f, float noise = 0.0f,
                            float gainDb = 0.0f)
{
    EngineSettings s;
    s.numVoices = voices;
    s.drift01 = drift;
    s.freqTol01 = s.phaseTol01 = s.gainTol01 = tol;
    s.saturation01 = sat;
    s.noise01 = noise;
    s.outGainDb = gainDb;
    s.bypass = false;
    return s;
}

// Elabora in place il tratto [from, to) di un segnale stereo a blocchi di `block` campioni.
void runStereo(DriftEngine& e, test::Signal& l, test::Signal& r, size_t from, size_t to, int block)
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

void prepareEngine(DriftEngine& e, double sr, std::uint64_t seed)
{
    e.prepare(sr, 512, 2);
    e.setSeed(seed, false);
}

double maxDelta(const test::Signal& s, size_t from, size_t to)
{
    double m = 0.0;
    for (size_t i = std::max<size_t>(from, 1); i < std::min(to, s.size()); ++i)
    {
        m = std::max(m, static_cast<double>(std::fabs(s[i] - s[i - 1])));
    }
    return m;
}

double maxAbs(const test::Signal& s)
{
    double m = 0.0;
    for (float v : s)
    {
        m = std::max(m, static_cast<double>(std::fabs(v)));
    }
    return m;
}

bool allFiniteAndBounded(const test::Signal& s, size_t from, size_t to, double bound)
{
    for (size_t i = from; i < to; ++i)
    {
        if (!std::isfinite(s[i]) || std::fabs(static_cast<double>(s[i])) > bound)
        {
            return false;
        }
    }
    return true;
}

// Massimo salto di livello RMS (dB) tra finestre consecutive da `window` campioni.
double maxRmsStepDb(const test::Signal& s, size_t from, size_t to, size_t window)
{
    double worst = 0.0;
    bool havePrev = false;
    double prev = 0.0;
    for (size_t pos = from; pos + window <= to; pos += window)
    {
        const double db = test::toDb(test::rms(s, pos, pos + window));
        if (havePrev)
        {
            worst = std::max(worst, std::fabs(db - prev));
        }
        prev = db;
        havePrev = true;
    }
    return worst;
}
} // namespace

TEST_CASE("Acceptance: transparency at Drift = 0 with everything else at maximum", "[acceptance]")
{
    double worst = 0.0;
    for (double sr : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        const auto len = static_cast<size_t>(sr * 0.4);
        const std::vector<test::Signal> inputs = {
            test::noise(len, 11u, 0.501187f), test::sine(len, 100.0, sr, 0.501187f),
            test::sine(len, 1000.0, sr, 0.501187f), test::sine(len, 10000.0, sr, 0.501187f)};
        for (int voices : {2, 4, 8, 16})
        {
            for (const auto& x : inputs)
            {
                DriftEngine e;
                prepareEngine(e, sr, 31337);
                e.setSettings(makeSettings(voices, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f));
                test::Signal l = x, r = test::noise(len, 12u, 0.501187f);
                const test::Signal rIn = r;
                runStereo(e, l, r, 0, len, 480);
                for (size_t i = 0; i < len; ++i)
                {
                    worst = std::max(worst, static_cast<double>(std::fabs(l[i] - x[i])));
                    worst = std::max(worst, static_cast<double>(std::fabs(r[i] - rIn[i])));
                }
            }
        }
    }
    std::printf("[acceptance] transparency: max |y - x| = %.3g\n", worst);
    CHECK(worst < 1.0e-5);
}

TEST_CASE("Acceptance: fuzzed parameters and block sizes never produce NaN or Inf", "[acceptance]")
{
    std::mt19937 gen(20240607);
    auto uni = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(gen); };
    auto pickInt = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(gen); };

    DriftEngine e;
    e.prepare(48000.0, 4096, 2);
    e.setSeed(1, false);

    std::vector<float> l(4096), r(4096);
    const float poison[] = {std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity(),
                            1.0e30f,
                            -1.0e30f,
                            1.0e-40f};
    for (int block = 0; block < 4000; ++block)
    {
        EngineSettings s;
        const int voiceChoices[] = {2, 4, 8, 16};
        s.numVoices = voiceChoices[pickInt(0, 3)];
        s.drift01 = uni(0.0f, 1.0f);
        s.freqTol01 = uni(0.0f, 1.0f);
        s.phaseTol01 = uni(0.0f, 1.0f);
        s.gainTol01 = uni(0.0f, 1.0f);
        s.saturation01 = uni(0.0f, 1.0f);
        s.noise01 = uni(0.0f, 1.0f);
        s.outGainDb = uni(-60.0f, 24.0f);
        s.bypass = pickInt(0, 9) == 0;
        e.setSettings(s);
        if (pickInt(0, 19) == 0)
        {
            const auto lo = static_cast<std::uint64_t>(gen());
            const auto hi = static_cast<std::uint64_t>(gen());
            e.setSeed(lo | (hi << 32), pickInt(0, 1) == 0);
        }

        // Lunghezza: 0, 1, dispari, potenze di due, qualunque.
        int n = 0;
        switch (pickInt(0, 4))
        {
            case 0:
                n = pickInt(0, 3);
                break;
            case 1:
                n = 2 * pickInt(0, 2047) + 1;
                break;
            case 2:
                n = 1 << pickInt(0, 12);
                break;
            default:
                n = pickInt(1, 4096);
                break;
        }
        n = std::min(n, 4096);

        const float amp = uni(0.0f, 1.0f);
        const bool corrupt = pickInt(0, 7) == 0;
        for (int i = 0; i < n; ++i)
        {
            const auto k = static_cast<size_t>(i);
            l[k] = uni(-amp, amp);
            r[k] = uni(-amp, amp);
            if (corrupt && pickInt(0, 15) == 0)
            {
                l[k] = poison[pickInt(0, 5)];
                r[k] = poison[pickInt(0, 5)];
            }
        }
        float* chans[2] = {l.data(), r.data()};
        juce::AudioBuffer<float> buf(chans, 2, n);
        e.process(buf);
        // In bypass il dry e' esattamente sanitize(in): un ingresso 1e30 esce come kSanitizeLimit (1e6),
        // quindi |y| <= 1 vale solo senza bypass. Nei blocchi con bypass o corrotti il limite e'
        // kSanitizeLimit.
        const double bound = (s.bypass || corrupt) ? static_cast<double>(kSanitizeLimit) : 1.0;
        bool finite = true;
        double peak = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto k = static_cast<size_t>(i);
            finite = finite && std::isfinite(l[k]) && std::isfinite(r[k]);
            peak =
                std::max({peak, std::fabs(static_cast<double>(l[k])), std::fabs(static_cast<double>(r[k]))});
        }
        REQUIRE(finite);
        REQUIRE(peak <= bound);
    }
}

TEST_CASE("Acceptance: corrupted input is bounded and does not poison the state", "[acceptance]")
{
    const double sr = 48000.0;
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::vector<std::vector<float>> patterns = {{nan},
                                                      {inf},
                                                      {-inf},
                                                      {1.0e30f},
                                                      {-1.0e30f},
                                                      {1.0e-40f, -1.0e-40f, 1.0e-44f},
                                                      {nan, inf, -inf, 1.0e30f, 0.1f}};

    for (const auto& pattern : patterns)
    {
        for (int voices : {2, 16})
        {
            const EngineSettings s = makeSettings(voices, 1.0f, 1.0f, 1.0f, 1.0f, 6.0f);
            DriftEngine corrupted, reference;
            prepareEngine(corrupted, sr, 5);
            prepareEngine(reference, sr, 5);
            corrupted.setSettings(s);
            reference.setSettings(s);

            const size_t badLen = 2048, cleanLen = 9600; // 200 ms puliti dopo il blocco corrotto
            const test::Signal clean = test::sine(badLen + cleanLen, 1000.0, sr, 0.25f);

            test::Signal bl = clean, br = clean;
            for (size_t i = 0; i < badLen; ++i)
            {
                bl[i] = pattern[i % pattern.size()];
                br[i] = pattern[(i + 1) % pattern.size()];
            }
            runStereo(corrupted, bl, br, 0, badLen, 512);
            CHECK(allFiniteAndBounded(bl, 0, badLen, 1.0));
            CHECK(allFiniteAndBounded(br, 0, badLen, 1.0));
            runStereo(corrupted, bl, br, badLen, badLen + cleanLen, 512);

            test::Signal rl = clean, rr = clean;
            runStereo(reference, rl, rr, 0, badLen + cleanLen, 512);

            CHECK(allFiniteAndBounded(bl, 0, bl.size(), 1.0));
            // Dopo 50 ms il livello coincide con quello del motore mai corrotto (entro 1 dB).
            const size_t from = badLen + 2400, to = badLen + cleanLen;
            const double dbCorrupted = test::toDb(test::rms(bl, from, to));
            const double dbReference = test::toDb(test::rms(rl, from, to));
            CHECK(std::fabs(dbCorrupted - dbReference) < 1.0);
        }
    }
}

TEST_CASE("Acceptance: voice count changes are click-free", "[acceptance]")
{
    const double sr = 48000.0;
    const size_t total = 60000;
    const size_t window = 240; // 5 ms
    const size_t rampLen = static_cast<size_t>(sr * DriftEngine::kVoiceRampSeconds);
    const test::Signal in = test::sine(total, 1000.0, sr, 0.501187f);

    // Sequenza 2 -> 16 -> 4 -> 8 -> 2, con cambi a rampa in corso.
    struct Change
    {
        size_t at;
        int before, after;
    };
    const Change changes[] = {{12000, 2, 16}, {24000, 16, 4}, {24500, 4, 8},
                              {36000, 8, 2},  {36300, 2, 16}, {36600, 16, 2}};
    // Cambi con rampa completa (il successivo arriva dopo la fine): qui si controlla il livello finale.
    const size_t completeAt[] = {12000, 24500, 36600};

    // Più seed: ogni N ha un livello medio diverso a 1 kHz (le tolleranze delle singole voci).
    double worstRatio = 0.0, worstStepRatio = 0.0, worstEndDb = 0.0;
    for (std::uint64_t seed = 1; seed <= 30; ++seed)
    {
        INFO("seed " << seed);
        // Regime di ogni N: delta massimo e livello RMS (dB) per lato.
        double steadyDelta = 0.0;
        double steadyDb[17][2] = {};
        for (int voices : {2, 4, 8, 16})
        {
            DriftEngine e;
            prepareEngine(e, sr, seed);
            e.setSettings(makeSettings(voices, 1.0f));
            test::Signal l = in, r = in;
            runStereo(e, l, r, 0, total, 256);
            steadyDelta = std::max({steadyDelta, maxDelta(l, 9600, total), maxDelta(r, 9600, total)});
            steadyDb[voices][0] = test::toDb(test::rms(l, total - 4800, total));
            steadyDb[voices][1] = test::toDb(test::rms(r, total - 4800, total));
        }

        DriftEngine e;
        prepareEngine(e, sr, seed);
        e.setSettings(makeSettings(2, 1.0f));
        test::Signal out[2] = {in, in};
        size_t pos = 0;
        for (const auto& c : changes)
        {
            runStereo(e, out[0], out[1], pos, c.at, 64);
            pos = c.at;
            e.setSettings(makeSettings(c.after, 1.0f));
        }
        runStereo(e, out[0], out[1], pos, total, 64);

        const double during = std::max(maxDelta(out[0], 12000, total), maxDelta(out[1], 12000, total));
        worstRatio = std::max(worstRatio, (during - 1.0e-4) / steadyDelta);
        CHECK(during <= 1.05 * steadyDelta + 1.0e-4);

        for (int side = 0; side < 2; ++side)
        {
            const auto& y = out[side];
            // Coppie di finestre consecutive da 5 ms: variazione massima ammessa durante una rampa.
            for (size_t w = 9600; w + 2 * window <= total; w += window)
            {
                double allowed = 0.1;
                for (const auto& c : changes)
                {
                    if (w < c.at + rampLen + window && w + 2 * window > c.at)
                    {
                        const double dL = std::fabs(steadyDb[c.after][side] - steadyDb[c.before][side]);
                        allowed = std::max(allowed, 1.5 * dL * (static_cast<double>(window) / rampLen));
                    }
                }
                const double step = std::fabs(test::toDb(test::rms(y, w + window, w + 2 * window)) -
                                              test::toDb(test::rms(y, w, w + window)));
                worstStepRatio = std::max(worstStepRatio, step / allowed);
                if (step > allowed)
                {
                    FAIL_CHECK("RMS step " << step << " dB > " << allowed << " dB at sample " << w);
                }
            }
            // A fine rampa il livello coincide con quello di regime della nuova configurazione.
            for (const auto& c : changes)
            {
                if (std::find(std::begin(completeAt), std::end(completeAt), c.at) == std::end(completeAt))
                {
                    continue;
                }
                const size_t from = c.at + rampLen + window;
                const double dev =
                    std::fabs(test::toDb(test::rms(y, from, from + window)) - steadyDb[c.after][side]);
                worstEndDb = std::max(worstEndDb, dev);
                CHECK(dev <= 0.05);
            }
        }
    }
    std::printf("[acceptance] voice changes, 30 seeds: worst delta ratio %.4f (limit 1.05), worst RMS step / "
                "allowed %.3f (limit 1), worst end-of-ramp level error %.4f dB (limit 0.05)\n",
                worstRatio, worstStepRatio, worstEndDb);
}

TEST_CASE("Acceptance: Randomize is click-free", "[acceptance]")
{
    const double sr = 48000.0;
    const size_t total = 72000;
    const test::Signal in = test::sine(total, 1000.0, sr, 0.501187f);

    // Delta massimo a regime: il peggiore tra i profili usati.
    double steady = 0.0;
    for (std::uint64_t seed : {1u, 2u, 3u})
    {
        DriftEngine e;
        prepareEngine(e, sr, seed);
        e.setSettings(makeSettings(4, 1.0f));
        test::Signal l = in, r = in;
        runStereo(e, l, r, 0, total, 256);
        steady = std::max({steady, maxDelta(l, 9600, total), maxDelta(r, 9600, total)});
    }

    DriftEngine e;
    prepareEngine(e, sr, 1);
    e.setSettings(makeSettings(4, 1.0f));
    test::Signal l = in, r = in;
    runStereo(e, l, r, 0, 12000, 64);
    e.setSeed(2, true);
    runStereo(e, l, r, 12000, 24000, 64);
    e.setSeed(3, true); // seconda randomizzazione a crossfade concluso
    runStereo(e, l, r, 24000, 30000, 64);
    e.setSeed(1, true); // e una a fade in corso (dopo 100 ms)
    runStereo(e, l, r, 30000, 34800, 64);
    e.setSeed(2, true);
    runStereo(e, l, r, 34800, total, 64);

    const double during = std::max(maxDelta(l, 12000, total), maxDelta(r, 12000, total));
    const double stepDb = std::max(maxRmsStepDb(l, 9600, total, 240), maxRmsStepDb(r, 9600, total, 240));
    std::printf("[acceptance] randomize: max delta %.5f (steady %.5f, limit %.5f), max RMS step %.4f dB\n",
                during, steady, 1.05 * steady + 1.0e-4, stepDb);
    CHECK(during <= 1.05 * steady + 1.0e-4);
    CHECK(stepDb <= 0.1);
}

TEST_CASE("Acceptance: no clipping with everything at maximum and Output +24 dB", "[acceptance]")
{
    const double sr = 48000.0;
    const size_t len = 48000;
    const float amp = 0.891251f; // -1 dBFS
    const std::vector<test::Signal> inputs = {test::sine(len, 1000.0, sr, amp),
                                              test::sine(len, 60.0, sr, amp), test::noise(len, 77u, amp)};
    for (int voices : {2, 16})
    {
        for (const auto& x : inputs)
        {
            DriftEngine e;
            prepareEngine(e, sr, 8);
            e.setSettings(makeSettings(voices, 1.0f, 1.0f, 1.0f, 1.0f, 24.0f));
            test::Signal l = x, r = x;
            runStereo(e, l, r, 0, len, 512);
            CHECK(std::max(maxAbs(l), maxAbs(r)) <= 1.0);
            CHECK(allFiniteAndBounded(l, 0, len, 1.0));
            CHECK(e.meters().ceilingActive.load());
        }
    }
}

TEST_CASE("Acceptance: -6 dBFS with Output 0 dB and Drift at maximum does not engage the ceiling",
          "[acceptance]")
{
    const double sr = 48000.0;
    const size_t len = 48000;
    const float amp = 0.501187f; // -6 dBFS
    const std::vector<test::Signal> inputs = {test::sine(len, 1000.0, sr, amp),
                                              test::sine(len, 100.0, sr, amp), test::noise(len, 78u, amp)};
    double peak = 0.0;
    // Saturazione a 0 (caso con meno compressione, quindi meno margine) e al massimo.
    for (const auto& [voices, sat] :
         std::vector<std::pair<int, float>>{{2, 0.0f}, {4, 0.0f}, {16, 0.0f}, {2, 1.0f}, {16, 1.0f}})
    {
        for (const auto& x : inputs)
        {
            DriftEngine e;
            prepareEngine(e, sr, 9);
            e.setSettings(makeSettings(voices, 1.0f, 1.0f, sat, 1.0f, 0.0f));
            test::Signal l = x, r = x;
            for (size_t pos = 0; pos < len; pos += 512)
            {
                runStereo(e, l, r, pos, std::min(pos + 512, len), 512);
                INFO("voices " << voices << " input " << (&x - &inputs[0]) << " pos " << pos);
                REQUIRE_FALSE(e.meters().ceilingActive.load());
            }
            peak = std::max({peak, maxAbs(l), maxAbs(r)});
        }
    }
    std::printf("[acceptance] -6 dBFS input, Drift max: peak %.4f\n", peak);
    CHECK(peak < 0.891);
}

namespace
{
constexpr double kStateSr = 48000.0;
constexpr int kStateBlock = 480;

// Parametri del test di stato: Drift 100 %, Noise 100 %, Saturation 50 %, 8 voci.
void configureForStateTest(DriftboxAudioProcessor& p)
{
    auto set = [&p](const char* id, float value)
    {
        auto* param = p.getAPVTS().getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(value));
    };
    set(pid::channels, 2.0f); // indice 2: 8 voci
    set(pid::drift, 1.0f);
    set(pid::noise, 1.0f);
    set(pid::saturation, 0.5f);
}

void prepareForStateTest(DriftboxAudioProcessor& p)
{
    p.setPlayConfigDetails(2, 2, kStateSr, kStateBlock);
    p.prepareToPlay(kStateSr, kStateBlock);
}

struct StereoOut
{
    test::Signal l, r;
};

// Elabora il tratto [from, to) dell'ingresso stereo a blocchi di kStateBlock campioni.
StereoOut runProcessor(DriftboxAudioProcessor& p, const test::Signal& inL, const test::Signal& inR,
                       size_t from, size_t to)
{
    StereoOut out;
    juce::MidiBuffer midi;
    for (size_t pos = from; pos < to; pos += kStateBlock)
    {
        const int n = static_cast<int>(std::min(static_cast<size_t>(kStateBlock), to - pos));
        juce::AudioBuffer<float> buf(2, n);
        buf.copyFrom(0, 0, inL.data() + pos, n);
        buf.copyFrom(1, 0, inR.data() + pos, n);
        p.processBlock(buf, midi);
        out.l.insert(out.l.end(), buf.getReadPointer(0), buf.getReadPointer(0) + n);
        out.r.insert(out.r.end(), buf.getReadPointer(1), buf.getReadPointer(1) + n);
    }
    return out;
}

bool bitIdentical(const test::Signal& a, const test::Signal& b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

double maxDiff(const test::Signal& a, const test::Signal& b)
{
    double m = 0.0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
    {
        m = std::max(m, static_cast<double>(std::fabs(a[i] - b[i])));
    }
    return m;
}
} // namespace

TEST_CASE("Acceptance: a restored state sounds identical from the first sample", "[acceptance][state]")
{
    const size_t len = static_cast<size_t>(kStateSr); // 1 s
    const test::Signal inL = test::noise(len, 501u, 0.5f), inR = test::noise(len, 502u, 0.5f);

    // Istanza A: configurata, elabora 1 s di rumore, poi salva lo stato.
    DriftboxAudioProcessor a;
    configureForStateTest(a);
    prepareForStateTest(a);
    runProcessor(a, inL, inR, 0, len);
    juce::MemoryBlock state;
    a.getStateInformation(state);

    // B: nuova istanza, prepareToPlay PRIMA e stato DOPO (il seed arriva a playback pronto).
    DriftboxAudioProcessor b;
    prepareForStateTest(b);
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    REQUIRE(b.getSeed() == a.getSeed());
    CHECK(b.getAPVTS().getRawParameterValue(pid::drift)->load() == Approx(1.0f));
    const auto outB = runProcessor(b, inL, inR, 0, len);

    // A2: ricreata da zero dallo stesso stato (stato PRIMA, prepareToPlay DOPO).
    DriftboxAudioProcessor a2;
    a2.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    prepareForStateTest(a2);
    const auto outA2 = runProcessor(a2, inL, inR, 0, len);

    CHECK(bitIdentical(outB.l, outA2.l));
    CHECK(bitIdentical(outB.r, outA2.r));

    // Il segnale è davvero elaborato e finito, e il seed conta: un'altra istanza con lo stesso stato ma
    // un seed diverso suona diversa.
    CHECK(maxDiff(outB.l, inL) > 1.0e-3);
    for (size_t i = 0; i < len; ++i)
    {
        REQUIRE(std::isfinite(outB.l[i]));
        REQUIRE(std::isfinite(outB.r[i]));
    }
    DriftboxAudioProcessor other;
    other.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    other.setSeed(a.getSeed() ^ 0x5555555555555555ULL);
    prepareForStateTest(other);
    const auto outOther = runProcessor(other, inL, inR, 0, len);
    CHECK(maxDiff(outOther.l, outB.l) > 1.0e-3);
}

TEST_CASE("Acceptance: a state restored during playback applies the seed without a crossfade",
          "[acceptance][state]")
{
    const size_t warm = 24 * kStateBlock, len = warm + kStateBlock;
    const test::Signal inL = test::noise(len, 601u, 0.5f), inR = test::noise(len, 602u, 0.5f);

    DriftboxAudioProcessor src;
    configureForStateTest(src);
    juce::MemoryBlock state;
    src.getStateInformation(state);

    // Istanza in riproduzione con gli stessi parametri ma un altro seed; poi arriva lo stato.
    auto makePlaying = [&](DriftboxAudioProcessor& p)
    {
        configureForStateTest(p);
        p.setSeed(src.getSeed() ^ 0xa5a5a5a5a5a5a5a5ULL);
        prepareForStateTest(p);
        runProcessor(p, inL, inR, 0, warm);
    };

    DriftboxAudioProcessor playing;
    makePlaying(playing);
    playing.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    REQUIRE(playing.getSeed() == src.getSeed());
    const auto got = runProcessor(playing, inL, inR, warm, len);

    // Riferimento: istanza preparata da zero con quello stato, con lo stesso ingresso del blocco successivo.
    DriftboxAudioProcessor freshSame;
    freshSame.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    prepareForStateTest(freshSame);
    test::Signal subL(inL.begin() + static_cast<std::ptrdiff_t>(warm), inL.end());
    test::Signal subR(inR.begin() + static_cast<std::ptrdiff_t>(warm), inR.end());
    const auto want = runProcessor(freshSame, subL, subR, 0, kStateBlock);
    const double snapDiff = std::max(maxDiff(got.l, want.l), maxDiff(got.r, want.r));
    std::printf("[acceptance] snap restore: max |playing - fresh| = %.3g\n", snapDiff);
    CHECK(snapDiff < 1.0e-5);

    // Controllo negativo: con un crossfade (seed impostato a mano) l'uscita sarebbe ben diversa.
    DriftboxAudioProcessor faded;
    makePlaying(faded);
    faded.setSeed(src.getSeed());
    const auto fadedOut = runProcessor(faded, inL, inR, warm, len);
    CHECK(std::max(maxDiff(fadedOut.l, want.l), maxDiff(fadedOut.r, want.r)) > 10.0 * snapDiff + 1.0e-4);
}
