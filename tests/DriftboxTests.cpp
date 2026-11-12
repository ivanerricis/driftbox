// SPDX-License-Identifier: AGPL-3.0-or-later
// Test di accettazione di Driftbox. Lo scaffold (T0) contiene solo i controlli di base; i cinque test di
// accettazione (trasparenza, NaN/Inf, cambio canali senza click, stato, niente clipping) arrivano con T3 e
// T4.
#include "PluginProcessor.h"
#include "dsp/DspUtils.h"
#include "TestSignals.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
