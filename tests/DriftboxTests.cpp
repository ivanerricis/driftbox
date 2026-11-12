// SPDX-License-Identifier: AGPL-3.0-or-later
// Test di accettazione di Driftbox: trasparenza, niente NaN/Inf, cambio di canale senza click, stato
// bit-identico alla riapertura e niente clipping.
#include "PluginProcessor.h"
#include "dsp/DriftEngine.h"
#include "dsp/DspUtils.h"
#include "TestSignals.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
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
EngineSettings makeSettings(int chanL, int chanR, float drift, float tol = 1.0f, float sat = 0.0f,
                            float noise = 0.0f, float gainDb = 0.0f)
{
    EngineSettings s;
    s.channelL = chanL;
    s.channelR = chanR;
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

void prepareEngine(DriftEngine& e, double sr)
{
    e.prepare(sr, 512, 2);
}

// Coppie di canali usate dai test di trasparenza, corrotti e clipping: estremi, uguali e a caso.
const std::vector<std::pair<int, int>> kPairs = {{1, 2}, {17, 17}, {64, 1}, {33, 60}};

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
        for (const auto& [cl, cr] : kPairs)
        {
            for (const auto& x : inputs)
            {
                DriftEngine e;
                prepareEngine(e, sr);
                e.setSettings(makeSettings(cl, cr, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f));
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
        // Canali diversi a ogni blocco (come un'automazione veloce), anche fuori range: il motore li limita.
        s.channelL = pickInt(0, kNumConsoleChannels + 1);
        s.channelR = pickInt(0, kNumConsoleChannels + 1);
        s.drift01 = uni(0.0f, 1.0f);
        s.freqTol01 = uni(0.0f, 1.0f);
        s.phaseTol01 = uni(0.0f, 1.0f);
        s.gainTol01 = uni(0.0f, 1.0f);
        s.saturation01 = uni(0.0f, 1.0f);
        s.noise01 = uni(0.0f, 1.0f);
        s.outGainDb = uni(-60.0f, 24.0f);
        s.bypass = pickInt(0, 9) == 0;
        e.setSettings(s);
        if (pickInt(0, 199) == 0)
        {
            e.reset();
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
        for (const auto& [cl, cr] : {std::pair{1, 2}, std::pair{40, 7}})
        {
            const EngineSettings s = makeSettings(cl, cr, 1.0f, 1.0f, 1.0f, 1.0f, 6.0f);
            DriftEngine corrupted, reference;
            prepareEngine(corrupted, sr);
            prepareEngine(reference, sr);
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

namespace
{
constexpr double kClickSr = 48000.0;
constexpr size_t kClickTotal = 96000;
constexpr size_t kClickWindow = 240; // 5 ms
constexpr size_t kFadeLen = 12000;   // 250 ms

// Delta massimo e livello RMS (dB) di regime di ogni canale (1..64) con Drift al 100 %, sinusoide da 1 kHz a
// -6 dBFS, Saturation 0 e Noise 0.
struct SteadyTable
{
    std::array<double, kNumConsoleChannels + 1> delta{}, db{};
};

const SteadyTable& steadyTable()
{
    static const SteadyTable table = []
    {
        SteadyTable t;
        const test::Signal in = test::sine(kClickTotal / 2, 1000.0, kClickSr, 0.501187f);
        for (int c = 1; c <= kNumConsoleChannels; ++c)
        {
            DriftEngine e;
            prepareEngine(e, kClickSr);
            e.setSettings(makeSettings(c, c, 1.0f));
            test::Signal l = in, r = in;
            runStereo(e, l, r, 0, in.size(), 256);
            t.delta[static_cast<size_t>(c)] = maxDelta(l, 9600, in.size());
            t.db[static_cast<size_t>(c)] = test::toDb(test::rms(l, in.size() - 4800, in.size()));
        }
        return t;
    }();
    return table;
}

// Un cambio di canali all'istante `at`.
struct ChannelEvent
{
    size_t at;
    int l, r;
};

struct ClickResult
{
    double delta = 0.0, steadyDelta = 0.0, stepRatio = 0.0;
};

// Controlla uscita `out` (L e R) di una sequenza di cambi di canale partita da (startL, startR):
// - il delta campione-campione non supera 1.05 x quello di regime + 1e-4;
// - il salto RMS tra finestre da 5 ms non supera max(0.1 dB, 1.5 x |dL| x 5 / 250), dove dL è il massimo
// scarto di
//   livello di regime tra i canali toccati dal lato nell'intorno della finestra.
ClickResult checkClickFree(const std::array<test::Signal, 2>& out, int startL, int startR,
                           const std::vector<ChannelEvent>& events)
{
    const auto& table = steadyTable();
    ClickResult result;

    // Segmenti (inizio, canale) per lato.
    std::array<std::vector<std::pair<size_t, int>>, 2> segments;
    segments[0].push_back({0, startL});
    segments[1].push_back({0, startR});
    for (const auto& ev : events)
    {
        segments[0].push_back({ev.at, ev.l});
        segments[1].push_back({ev.at, ev.r});
    }

    for (size_t side = 0; side < 2; ++side)
    {
        const auto& seg = segments[side];
        const auto& y = out[side];

        double steadyDelta = 0.0;
        for (const auto& [at, c] : seg)
        {
            steadyDelta = std::max(steadyDelta, table.delta[static_cast<size_t>(c)]);
        }
        const double during = maxDelta(y, 9600, y.size());
        result.delta = std::max(result.delta, during);
        result.steadyDelta = std::max(result.steadyDelta, steadyDelta);
        CHECK(during <= 1.05 * steadyDelta + 1.0e-4);

        for (size_t w = 9600; w + 2 * kClickWindow <= y.size(); w += kClickWindow)
        {
            // Canali attivi tra (w - fade) e la fine della coppia di finestre.
            double lo = 1.0e9, hi = -1.0e9;
            for (size_t i = 0; i < seg.size(); ++i)
            {
                const size_t start = seg[i].first;
                const size_t end = i + 1 < seg.size() ? seg[i + 1].first : y.size();
                if (start < w + 2 * kClickWindow && end + kFadeLen > w)
                {
                    const double db = table.db[static_cast<size_t>(seg[i].second)];
                    lo = std::min(lo, db);
                    hi = std::max(hi, db);
                }
            }
            const double allowed =
                std::max(0.1, 1.5 * (hi - lo) * (static_cast<double>(kClickWindow) / kFadeLen));
            const double step = std::fabs(test::toDb(test::rms(y, w + kClickWindow, w + 2 * kClickWindow)) -
                                          test::toDb(test::rms(y, w, w + kClickWindow)));
            result.stepRatio = std::max(result.stepRatio, step / allowed);
            if (step > allowed)
            {
                FAIL_CHECK("RMS step " << step << " dB > " << allowed << " dB at sample " << w << " side "
                                       << side);
            }
        }
    }
    return result;
}

std::array<test::Signal, 2> runChannelEvents(int startL, int startR, const std::vector<ChannelEvent>& events,
                                             int block)
{
    const test::Signal in = test::sine(kClickTotal, 1000.0, kClickSr, 0.501187f);
    DriftEngine e;
    prepareEngine(e, kClickSr);
    e.setSettings(makeSettings(startL, startR, 1.0f));
    std::array<test::Signal, 2> out{in, in};
    size_t pos = 0;
    for (const auto& ev : events)
    {
        runStereo(e, out[0], out[1], pos, ev.at, block);
        pos = ev.at;
        e.setSettings(makeSettings(ev.l, ev.r, 1.0f));
    }
    runStereo(e, out[0], out[1], pos, kClickTotal, block);
    return out;
}
} // namespace

TEST_CASE("Acceptance: channel changes are click-free", "[acceptance]")
{
    // Cambi isolati e cambi a fade in corso, su entrambi i lati.
    const std::vector<ChannelEvent> slow = {{12000, 40, 9},  {24000, 40, 50}, {24500, 17, 50}, {36000, 3, 22},
                                            {36300, 60, 22}, {60000, 61, 1},  {60100, 8, 2}};
    const auto a = checkClickFree(runChannelEvents(5, 9, slow, 64), 5, 9, slow);

    // Automazione veloce: entrambi i canali cambiano a OGNI blocco (64 campioni e 32 campioni).
    std::mt19937 gen(4242);
    std::uniform_int_distribution<int> pick(1, kNumConsoleChannels);
    ClickResult worstFast;
    for (int block : {64, 32, 480})
    {
        std::vector<ChannelEvent> fast;
        for (size_t at = 12000; at < kClickTotal; at += static_cast<size_t>(block))
        {
            fast.push_back({at, pick(gen), pick(gen)});
        }
        const auto r = checkClickFree(runChannelEvents(1, 2, fast, block), 1, 2, fast);
        worstFast.delta = std::max(worstFast.delta, r.delta);
        worstFast.steadyDelta = std::max(worstFast.steadyDelta, r.steadyDelta);
        worstFast.stepRatio = std::max(worstFast.stepRatio, r.stepRatio);
    }
    std::printf("[acceptance] channel changes, slow: max delta %.5f (steady %.5f, limit %.5f), RMS step / "
                "allowed %.3f\n",
                a.delta, a.steadyDelta, 1.05 * a.steadyDelta + 1.0e-4, a.stepRatio);
    std::printf("[acceptance] channel changes, every block: max delta %.5f (steady %.5f, limit %.5f), RMS "
                "step / allowed %.3f\n",
                worstFast.delta, worstFast.steadyDelta, 1.05 * worstFast.steadyDelta + 1.0e-4,
                worstFast.stepRatio);
}

TEST_CASE("Acceptance: no clipping with everything at maximum and Output +24 dB", "[acceptance]")
{
    const double sr = 48000.0;
    const size_t len = 48000;
    const float amp = 0.891251f; // -1 dBFS
    const std::vector<test::Signal> inputs = {test::sine(len, 1000.0, sr, amp),
                                              test::sine(len, 60.0, sr, amp), test::noise(len, 77u, amp)};
    for (const auto& [cl, cr] : {std::pair{1, 2}, std::pair{64, 33}})
    {
        for (const auto& x : inputs)
        {
            DriftEngine e;
            prepareEngine(e, sr);
            e.setSettings(makeSettings(cl, cr, 1.0f, 1.0f, 1.0f, 1.0f, 24.0f));
            test::Signal l = x, r = x;
            runStereo(e, l, r, 0, len, 512);
            CHECK(std::max(maxAbs(l), maxAbs(r)) <= 1.0);
            CHECK(allFiniteAndBounded(l, 0, len, 1.0));
            CHECK(e.meters().ceilingActive.load());
        }
    }
}

TEST_CASE("Acceptance: -6 dBFS tones with Output 0 dB and Drift at maximum do not engage the ceiling",
          "[acceptance]")
{
    const double sr = 48000.0;
    const size_t len = 48000;
    const float amp = 0.501187f; // -6 dBFS
    const std::vector<test::Signal> tones = {test::sine(len, 1000.0, sr, amp),
                                             test::sine(len, 100.0, sr, amp)};
    const test::Signal noiseIn = test::noise(len, 78u, amp);
    double tonePeak = 0.0, noisePeak = 0.0;
    // Saturazione a 0 (caso con meno compressione, quindi meno margine) e al massimo. Un canale ogni tre,
    // più gli estremi con Saturation al massimo.
    std::vector<std::pair<int, float>> configs;
    for (int c = 1; c <= kNumConsoleChannels; c += 3)
    {
        configs.push_back({c, 0.0f});
    }
    configs.push_back({1, 1.0f});
    configs.push_back({64, 1.0f});
    for (const auto& [chan, sat] : configs)
    {
        for (const auto& x : tones)
        {
            DriftEngine e;
            prepareEngine(e, sr);
            e.setSettings(makeSettings(chan, chan % kNumConsoleChannels + 1, 1.0f, 1.0f, sat, 1.0f, 0.0f));
            test::Signal l = x, r = x;
            for (size_t pos = 0; pos < len; pos += 512)
            {
                runStereo(e, l, r, pos, std::min(pos + 512, len), 512);
                INFO("channel " << chan << " pos " << pos);
                REQUIRE_FALSE(e.meters().ceilingActive.load());
            }
            tonePeak = std::max({tonePeak, maxAbs(l), maxAbs(r)});
        }

        // Rumore bianco a pieno spettro: l'allpass del primo ordine ruota la fase delle alte frequenze (fino
        // a un'inversione di polarità a Nyquist) e un singolo canale per lato può alzare il picco di un
        // campione vicino al doppio. Qui il ceiling può lavorare, ma l'uscita non supera mai 0 dBFS.
        DriftEngine e;
        prepareEngine(e, sr);
        e.setSettings(makeSettings(chan, chan % kNumConsoleChannels + 1, 1.0f, 1.0f, sat, 1.0f, 0.0f));
        test::Signal l = noiseIn, r = noiseIn;
        runStereo(e, l, r, 0, len, 512);
        noisePeak = std::max({noisePeak, maxAbs(l), maxAbs(r)});
    }
    std::printf("[acceptance] -6 dBFS tones, Drift max: peak %.4f; -6 dBFS white noise: peak %.4f\n",
                tonePeak, noisePeak);
    CHECK(tonePeak < 0.891);
    CHECK(noisePeak <= 1.0);
}

namespace
{
constexpr double kStateSr = 48000.0;
constexpr int kStateBlock = 480;

// Parametri del test di stato: Drift 100 %, Noise 100 %, Saturation 50 %, canali L 21 e R 55.
void configureForStateTest(DriftboxAudioProcessor& p)
{
    auto set = [&p](const char* id, float value)
    {
        auto* param = p.getAPVTS().getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(value));
    };
    set(pid::chanL, 21.0f);
    set(pid::chanR, 55.0f);
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

    // B: nuova istanza, prepareToPlay PRIMA e stato DOPO (i canali arrivano a playback pronto, ma prima del
    // primo blocco: nessun fade).
    DriftboxAudioProcessor b;
    prepareForStateTest(b);
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(b.getAPVTS().getRawParameterValue(pid::chanL)->load() == Approx(21.0f));
    CHECK(b.getAPVTS().getRawParameterValue(pid::chanR)->load() == Approx(55.0f));
    CHECK(b.getAPVTS().getRawParameterValue(pid::drift)->load() == Approx(1.0f));
    const auto outB = runProcessor(b, inL, inR, 0, len);

    // A2: ricreata da zero dallo stesso stato (stato PRIMA, prepareToPlay DOPO).
    DriftboxAudioProcessor a2;
    a2.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    prepareForStateTest(a2);
    const auto outA2 = runProcessor(a2, inL, inR, 0, len);

    CHECK(bitIdentical(outB.l, outA2.l));
    CHECK(bitIdentical(outB.r, outA2.r));

    // Il segnale è davvero elaborato e finito, e i canali contano: un'altra istanza con lo stesso stato ma un
    // canale diverso suona diversa.
    CHECK(maxDiff(outB.l, inL) > 1.0e-3);
    for (size_t i = 0; i < len; ++i)
    {
        REQUIRE(std::isfinite(outB.l[i]));
        REQUIRE(std::isfinite(outB.r[i]));
    }
    DriftboxAudioProcessor other;
    other.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    auto* chanL = other.getAPVTS().getParameter(pid::chanL);
    chanL->setValueNotifyingHost(chanL->convertTo0to1(22.0f));
    prepareForStateTest(other);
    const auto outOther = runProcessor(other, inL, inR, 0, len);
    CHECK(maxDiff(outOther.l, outB.l) > 1.0e-3);
}

TEST_CASE("Acceptance: Randomize is click-free", "[acceptance]")
{
    const test::Signal in = test::sine(kClickTotal, 1000.0, kStateSr, 0.501187f);

    DriftboxAudioProcessor p;
    configureForStateTest(p);
    auto set = [&p](const char* id, float value)
    {
        auto* param = p.getAPVTS().getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(value));
    };
    set(pid::noise, 0.0f);
    set(pid::saturation, 0.0f);
    set(pid::chanL, 5.0f);
    set(pid::chanR, 9.0f);
    prepareForStateTest(p);

    const auto channels = [&p]
    {
        return std::pair{static_cast<int>(p.getAPVTS().getRawParameterValue(pid::chanL)->load()),
                         static_cast<int>(p.getAPVTS().getRawParameterValue(pid::chanR)->load())};
    };

    // Randomize a crossfade concluso, a fade in corso (dopo 100 ms) e due volte di fila.
    std::vector<ChannelEvent> events;
    StereoOut out;
    size_t pos = 0;
    auto runTo = [&](size_t to)
    {
        const auto part = runProcessor(p, in, in, pos, to);
        out.l.insert(out.l.end(), part.l.begin(), part.l.end());
        out.r.insert(out.r.end(), part.r.begin(), part.r.end());
        pos = to;
    };
    const size_t step = 480;
    runTo(12000);
    for (const size_t at : std::vector<size_t>{12000, 30000, 34800, 34800 + step, 60000})
    {
        if (at > pos)
        {
            runTo(at);
        }
        p.randomize();
        const auto [l, r] = channels();
        events.push_back({pos, l, r});
    }
    runTo(kClickTotal);
    REQUIRE(out.l.size() == kClickTotal);

    const auto result = checkClickFree({out.l, out.r}, 5, 9, events);
    std::printf("[acceptance] randomize: max delta %.5f (steady %.5f, limit %.5f), RMS step / allowed %.3f\n",
                result.delta, result.steadyDelta, 1.05 * result.steadyDelta + 1.0e-4, result.stepRatio);
}
