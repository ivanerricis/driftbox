// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>
#include <utility>

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

    for (const char* id : {pid::chanL, pid::chanR, pid::drift, pid::freqTol, pid::phaseTol, pid::gainTol,
                           pid::saturation, pid::noise, pid::outGain, pid::bypass})
    {
        CHECK(ids.count(id) == 1);
    }
}

TEST_CASE("Parameter ranges and defaults", "[params]")
{
    DriftboxAudioProcessor p;
    auto& apvts = p.getAPVTS();

    CHECK(apvts.getRawParameterValue(pid::drift)->load() == Approx(0.5f));
    CHECK(apvts.getRawParameterValue(pid::noise)->load() == Approx(0.0f));
    // Margine assoluto: il default 0 dB passa da un valore normalizzato e su macOS non torna esattamente a 0.
    CHECK(apvts.getRawParameterValue(pid::outGain)->load() == Approx(0.0f).margin(1.0e-4));
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

    // I valori vivi di chanL/chanR sono una coppia casuale (nuova istanza): si controlla il default.
    for (const auto& [id, def] : {std::pair{pid::chanL, 1}, std::pair{pid::chanR, 2}})
    {
        auto* ch = dynamic_cast<juce::AudioParameterInt*>(&param(p, id));
        REQUIRE(ch != nullptr);
        CHECK(ch->getRange().getStart() == 1);
        CHECK(ch->getRange().getEnd() == 64);
        CHECK(ch->getNormalisableRange().interval == Approx(1.0f));
        CHECK(juce::roundToInt(ch->convertFrom0to1(
                  static_cast<juce::RangedAudioParameter&>(*ch).getDefaultValue())) == def);
        CHECK(ch->isAutomatable());
        CHECK(ch->getParameterID() == id);
    }
    CHECK(param(p, pid::chanL).getName(64) == "Channel L");
    CHECK(param(p, pid::chanR).getName(64) == "Channel R");
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

    set(pid::chanL, 12.0f);
    set(pid::chanR, 64.0f);
    set(pid::drift, 0.25f);
    set(pid::outGain, -6.0f);
    set(pid::bypass, 1.0f);

    const auto s = refs.snapshot();
    CHECK(s.channelL == 12);
    CHECK(s.channelR == 64);
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

    for (const char* id : {pid::chanL, pid::chanR})
    {
        auto& ch = param(p, id);
        CHECK(juce::roundToInt(ch.convertFrom0to1(ch.getValueForText("12"))) == 12);
        CHECK(juce::roundToInt(ch.convertFrom0to1(ch.getValueForText("64"))) == 64);
        CHECK(juce::roundToInt(ch.convertFrom0to1(ch.getValueForText("1"))) == 1);
        CHECK(ch.getText(ch.convertTo0to1(37.0f), 32) == "37");
    }
}
