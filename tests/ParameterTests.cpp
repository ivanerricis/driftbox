// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace driftbox;
using Catch::Approx;

namespace
{
juce::RangedAudioParameter& param(DriftboxAudioProcessor& p, const char* id)
{
    auto* r = p.getAPVTS().getParameter(id);
    REQUIRE(r != nullptr);
    return *r;
}
} // namespace

TEST_CASE("Parameter layout: count, unique IDs and version", "[params]")
{
    DriftboxAudioProcessor p;
    const auto& params = p.getParameters();
    REQUIRE(params.size() == kNumParameters);

    std::set<juce::String> ids;
    for (auto* base : params)
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*>(base);
        REQUIRE(r != nullptr);
        ids.insert(r->getParameterID());
        CHECK(r->getVersionHint() == kParamVersion);
    }
    CHECK(ids.size() == static_cast<size_t>(kNumParameters));

    for (const char* id : {pid::channels, pid::drift, pid::freqTol, pid::phaseTol, pid::gainTol,
                           pid::saturation, pid::noise, pid::outGain, pid::bypass})
    {
        CHECK(ids.count(id) == 1);
    }
}

TEST_CASE("Parameter ranges and defaults", "[params]")
{
    DriftboxAudioProcessor p;
    auto& apvts = p.getAPVTS();

    CHECK(apvts.getRawParameterValue(pid::channels)->load() ==
          Approx(static_cast<float>(kDefaultChannelIndex)));
    CHECK(apvts.getRawParameterValue(pid::drift)->load() == Approx(0.5f));
    CHECK(apvts.getRawParameterValue(pid::noise)->load() == Approx(0.0f));
    CHECK(apvts.getRawParameterValue(pid::outGain)->load() == Approx(0.0f));
    CHECK(apvts.getRawParameterValue(pid::bypass)->load() == Approx(0.0f));

    auto& out = param(p, pid::outGain);
    CHECK(out.getNormalisableRange().start == Approx(-24.0f));
    CHECK(out.getNormalisableRange().end == Approx(24.0f));

    for (const char* id :
         {pid::drift, pid::freqTol, pid::phaseTol, pid::gainTol, pid::saturation, pid::noise})
    {
        auto& r = param(p, id);
        CHECK(r.getNormalisableRange().start == Approx(0.0f));
        CHECK(r.getNormalisableRange().end == Approx(1.0f));
        CHECK(r.isAutomatable());
    }

    auto* channels = dynamic_cast<juce::AudioParameterChoice*>(&param(p, pid::channels));
    REQUIRE(channels != nullptr);
    CHECK(channels->choices == juce::StringArray{"2", "4", "8", "16"});
    CHECK(channels->isAutomatable());
}

TEST_CASE("Snapshot maps parameters to engine settings", "[params]")
{
    DriftboxAudioProcessor p;
    auto& apvts = p.getAPVTS();
    ParamRefs refs(apvts);

    auto set = [&](const char* id, float value)
    {
        auto& r = param(p, id);
        r.setValueNotifyingHost(r.convertTo0to1(value));
    };

    set(pid::channels, 3.0f); // indice 3 -> 16 voci
    set(pid::drift, 0.25f);
    set(pid::outGain, -6.0f);
    set(pid::bypass, 1.0f);

    const auto s = refs.snapshot();
    CHECK(s.numVoices == 16);
    CHECK(s.drift01 == Approx(0.25f));
    CHECK(s.outGainDb == Approx(-6.0f).margin(0.05f));
    CHECK(s.bypass);
}

TEST_CASE("Typed text round-trips through the parameters", "[params]")
{
    DriftboxAudioProcessor p;

    auto& drift = param(p, pid::drift);
    CHECK(drift.getValueForText("75 %") == Approx(0.75f));
    CHECK(drift.getValueForText("30") == Approx(0.30f));
    CHECK(drift.getText(drift.getValueForText("42 %"), 32) == "42 %");

    auto& out = param(p, pid::outGain);
    CHECK(out.convertFrom0to1(out.getValueForText("-3.5 dB")) == Approx(-3.5f).margin(0.05f));
    CHECK(out.convertFrom0to1(out.getValueForText("12")) == Approx(12.0f).margin(0.05f));
}
