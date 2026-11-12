// SPDX-License-Identifier: AGPL-3.0-or-later
// Stato del plugin: round trip (parametri, canali L/R, dimensione UI), formato XML, versioni e stati non
// validi.
#include "PluginProcessor.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>
#include <utility>

using namespace driftbox;
using Catch::Approx;

namespace
{
void setValue(DriftboxAudioProcessor& p, const char* id, float value)
{
    auto* param = p.getAPVTS().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

float raw(DriftboxAudioProcessor& p, const char* id)
{
    return p.getAPVTS().getRawParameterValue(id)->load();
}

juce::MemoryBlock saveState(DriftboxAudioProcessor& p)
{
    juce::MemoryBlock block;
    p.getStateInformation(block);
    return block;
}

void loadState(DriftboxAudioProcessor& p, const juce::MemoryBlock& block)
{
    p.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
}

std::unique_ptr<juce::XmlElement> toXml(const juce::MemoryBlock& block)
{
    return juce::AudioProcessor::getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
}

// Stato di `src` con l'attributo `name` sostituito (o rimosso se value è nullptr).
juce::MemoryBlock withAttribute(DriftboxAudioProcessor& src, const char* name, const char* value)
{
    auto xml = toXml(saveState(src));
    REQUIRE(xml != nullptr);
    if (value != nullptr)
    {
        xml->setAttribute(name, value);
    }
    else
    {
        xml->removeAttribute(name);
    }
    juce::MemoryBlock out;
    juce::AudioProcessor::copyXmlToBinary(*xml, out);
    return out;
}
} // namespace

TEST_CASE("State round trip", "[state]")
{
    DriftboxAudioProcessor a;
    setValue(a, pid::chanL, 37.0f);
    setValue(a, pid::chanR, 5.0f);
    setValue(a, pid::drift, 0.83f);
    setValue(a, pid::freqTol, 0.31f);
    setValue(a, pid::phaseTol, 0.12f);
    setValue(a, pid::gainTol, 0.97f);
    setValue(a, pid::saturation, 0.44f);
    setValue(a, pid::noise, 0.66f);
    setValue(a, pid::outGain, -7.5f);
    setValue(a, pid::bypass, 1.0f);
    a.setUiSize(1000, 640);

    const auto block = saveState(a);
    REQUIRE(block.getSize() > 0);

    DriftboxAudioProcessor b;
    loadState(b, block);

    CHECK(raw(b, pid::chanL) == Approx(37.0f));
    CHECK(raw(b, pid::chanR) == Approx(5.0f));
    CHECK(raw(b, pid::drift) == Approx(raw(a, pid::drift)));
    CHECK(raw(b, pid::freqTol) == Approx(raw(a, pid::freqTol)));
    CHECK(raw(b, pid::phaseTol) == Approx(raw(a, pid::phaseTol)));
    CHECK(raw(b, pid::gainTol) == Approx(raw(a, pid::gainTol)));
    CHECK(raw(b, pid::saturation) == Approx(raw(a, pid::saturation)));
    CHECK(raw(b, pid::noise) == Approx(raw(a, pid::noise)));
    CHECK(raw(b, pid::outGain) == Approx(-7.5f).margin(0.05f));
    CHECK(raw(b, pid::bypass) == Approx(1.0f));
    CHECK(b.getUiSize().x == 1000);
    CHECK(b.getUiSize().y == 640);
}

TEST_CASE("Channel extremes round trip", "[state]")
{
    for (const auto& [l, r] : {std::pair{1, 64}, std::pair{64, 1}, std::pair{1, 1}, std::pair{64, 64}})
    {
        DriftboxAudioProcessor a;
        setValue(a, pid::chanL, static_cast<float>(l));
        setValue(a, pid::chanR, static_cast<float>(r));
        DriftboxAudioProcessor b;
        loadState(b, saveState(a));
        CHECK(raw(b, pid::chanL) == Approx(static_cast<float>(l)));
        CHECK(raw(b, pid::chanR) == Approx(static_cast<float>(r)));
    }
}

TEST_CASE("State XML carries the profile version and no seed", "[state]")
{
    DriftboxAudioProcessor a;
    auto xml = toXml(saveState(a));
    REQUIRE(xml != nullptr);
    CHECK(xml->hasTagName(kStateTag));
    CHECK(xml->getIntAttribute("stateVersion") == kStateVersion);
    CHECK(xml->getIntAttribute("profileVersion") == kProfileVersion);
    CHECK_FALSE(xml->hasAttribute("driftSeed"));
}

TEST_CASE("A legacy driftSeed property is dropped on load", "[state]")
{
    DriftboxAudioProcessor src;
    DriftboxAudioProcessor p;
    loadState(p, withAttribute(src, "driftSeed", "000000000000002a"));
    auto xml = toXml(saveState(p));
    REQUIRE(xml != nullptr);
    CHECK_FALSE(xml->hasAttribute("driftSeed"));
}

TEST_CASE("Garbage state is ignored", "[state]")
{
    DriftboxAudioProcessor p;
    setValue(p, pid::drift, 0.25f);
    setValue(p, pid::chanL, 9.0f);

    SECTION("random bytes")
    {
        const char junk[] = "this is definitely not a plugin state";
        p.setStateInformation(junk, static_cast<int>(sizeof(junk)));
    }
    SECTION("null and empty")
    {
        p.setStateInformation(nullptr, 0);
        const char dummy = 0;
        p.setStateInformation(&dummy, 0);
    }
    SECTION("valid XML with the wrong tag")
    {
        juce::XmlElement wrong("SOMETHING_ELSE");
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary(wrong, mb);
        p.setStateInformation(mb.getData(), static_cast<int>(mb.getSize()));
    }

    CHECK(raw(p, pid::drift) == Approx(0.25f));
    CHECK(raw(p, pid::chanL) == Approx(9.0f));
}

TEST_CASE("Future-version state is rejected, old and version-less states load", "[state]")
{
    DriftboxAudioProcessor src;
    setValue(src, pid::drift, 0.9f);
    setValue(src, pid::outGain, -6.0f);
    setValue(src, pid::chanL, 40.0f);
    setValue(src, pid::chanR, 41.0f);

    SECTION("future stateVersion leaves parameters untouched")
    {
        const auto block = withAttribute(src, "stateVersion", juce::String(kStateVersion + 1).toRawUTF8());
        DriftboxAudioProcessor p;
        setValue(p, pid::drift, 0.2f);
        setValue(p, pid::chanL, 5.0f);
        loadState(p, block);
        CHECK(raw(p, pid::drift) == Approx(0.2f));
        CHECK(raw(p, pid::chanL) == Approx(5.0f));
    }
    SECTION("stateVersion 1 loads")
    {
        const auto block = withAttribute(src, "stateVersion", "1");
        DriftboxAudioProcessor p;
        loadState(p, block);
        CHECK(raw(p, pid::drift) == Approx(0.9f));
        CHECK(raw(p, pid::outGain) == Approx(-6.0f).margin(0.05f));
        CHECK(raw(p, pid::chanL) == Approx(40.0f));
        CHECK(raw(p, pid::chanR) == Approx(41.0f));
    }
    SECTION("missing stateVersion loads as version 1")
    {
        const auto block = withAttribute(src, "stateVersion", nullptr);
        DriftboxAudioProcessor p;
        loadState(p, block);
        CHECK(raw(p, pid::drift) == Approx(0.9f));
        CHECK(raw(p, pid::chanL) == Approx(40.0f));
    }
    SECTION("future profileVersion is rejected")
    {
        const auto block =
            withAttribute(src, "profileVersion", juce::String(kProfileVersion + 1).toRawUTF8());
        DriftboxAudioProcessor p;
        setValue(p, pid::drift, 0.2f);
        setValue(p, pid::chanL, 5.0f);
        setValue(p, pid::chanR, 6.0f);
        loadState(p, block);
        CHECK(raw(p, pid::drift) == Approx(0.2f));
        CHECK(raw(p, pid::chanL) == Approx(5.0f));
        CHECK(raw(p, pid::chanR) == Approx(6.0f));
    }
    SECTION("missing profileVersion loads as version 1")
    {
        const auto block = withAttribute(src, "profileVersion", nullptr);
        DriftboxAudioProcessor p;
        loadState(p, block);
        CHECK(raw(p, pid::drift) == Approx(0.9f));
        CHECK(raw(p, pid::chanL) == Approx(40.0f));
    }
}

TEST_CASE("New instances do not all start with the same channel pair", "[state]")
{
    // Con 64 coppie possibili, 20 istanze uguali per caso hanno probabilità trascurabile (64^-19).
    std::set<std::pair<int, int>> pairs;
    for (int i = 0; i < 20; ++i)
    {
        DriftboxAudioProcessor p;
        const int l = static_cast<int>(raw(p, pid::chanL)), r = static_cast<int>(raw(p, pid::chanR));
        CHECK(l >= 1);
        CHECK(l <= 64);
        CHECK(r == l % 64 + 1);
        pairs.insert({l, r});
    }
    CHECK(pairs.size() > 1);
}
