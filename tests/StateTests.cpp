// SPDX-License-Identifier: AGPL-3.0-or-later
// Stato del plugin: round trip (parametri, seed, dimensione UI), formato XML, versioni e stati non validi.
#include "PluginProcessor.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <set>

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

juce::String hexOf(std::uint64_t seed)
{
    return juce::String::formatted("%016llx", static_cast<unsigned long long>(seed));
}
} // namespace

TEST_CASE("State round trip", "[state]")
{
    DriftboxAudioProcessor a;
    setValue(a, pid::channels, 3.0f); // indice della scelta: 16 voci
    setValue(a, pid::drift, 0.83f);
    setValue(a, pid::freqTol, 0.31f);
    setValue(a, pid::phaseTol, 0.12f);
    setValue(a, pid::gainTol, 0.97f);
    setValue(a, pid::saturation, 0.44f);
    setValue(a, pid::noise, 0.66f);
    setValue(a, pid::outGain, -7.5f);
    setValue(a, pid::bypass, 1.0f);
    a.setSeed(0x0123456789abcdefULL);
    a.setUiSize(1000, 640);

    const auto block = saveState(a);
    REQUIRE(block.getSize() > 0);

    DriftboxAudioProcessor b;
    loadState(b, block);

    CHECK(raw(b, pid::channels) == Approx(raw(a, pid::channels)));
    CHECK(raw(b, pid::drift) == Approx(raw(a, pid::drift)));
    CHECK(raw(b, pid::freqTol) == Approx(raw(a, pid::freqTol)));
    CHECK(raw(b, pid::phaseTol) == Approx(raw(a, pid::phaseTol)));
    CHECK(raw(b, pid::gainTol) == Approx(raw(a, pid::gainTol)));
    CHECK(raw(b, pid::saturation) == Approx(raw(a, pid::saturation)));
    CHECK(raw(b, pid::noise) == Approx(raw(a, pid::noise)));
    CHECK(raw(b, pid::outGain) == Approx(-7.5f).margin(0.05f));
    CHECK(raw(b, pid::bypass) == Approx(1.0f));
    CHECK(b.getSeed() == 0x0123456789abcdefULL);
    CHECK(b.getUiSize().x == 1000);
    CHECK(b.getUiSize().y == 640);
}

TEST_CASE("State XML carries the seed as 16 hex digits and the profile version", "[state]")
{
    DriftboxAudioProcessor a;
    a.setSeed(0x2aULL); // zeri iniziali: devono restare 16 cifre

    auto xml = toXml(saveState(a));
    REQUIRE(xml != nullptr);
    CHECK(xml->hasTagName(kStateTag));
    CHECK(xml->getIntAttribute("stateVersion") == kStateVersion);
    CHECK(xml->getIntAttribute("profileVersion") == kProfileVersion);
    CHECK(xml->getStringAttribute("driftSeed") == "000000000000002a");

    for (std::uint64_t seed : {0x0ULL, 0xffffffffffffffffULL, 0x8000000000000000ULL, 0xdeadbeefcafef00dULL})
    {
        a.setSeed(seed);
        auto x = toXml(saveState(a));
        REQUIRE(x != nullptr);
        const auto s = x->getStringAttribute("driftSeed");
        CHECK(s.length() == 16);
        CHECK(s == s.toLowerCase());
        CHECK(s.containsOnly("0123456789abcdef"));
        CHECK(s == hexOf(seed));
    }
}

TEST_CASE("Garbage state is ignored", "[state]")
{
    DriftboxAudioProcessor p;
    setValue(p, pid::drift, 0.25f);
    p.setSeed(77);

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
        wrong.setAttribute("driftSeed", "00000000000000ff");
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary(wrong, mb);
        p.setStateInformation(mb.getData(), static_cast<int>(mb.getSize()));
    }

    CHECK(raw(p, pid::drift) == Approx(0.25f));
    CHECK(p.getSeed() == 77);
}

TEST_CASE("Future-version state is rejected, old and version-less states load", "[state]")
{
    DriftboxAudioProcessor src;
    setValue(src, pid::drift, 0.9f);
    setValue(src, pid::outGain, -6.0f);
    src.setSeed(0x1111111111111111ULL);

    SECTION("future stateVersion leaves parameters and seed untouched")
    {
        const auto block = withAttribute(src, "stateVersion", juce::String(kStateVersion + 1).toRawUTF8());
        DriftboxAudioProcessor p;
        setValue(p, pid::drift, 0.2f);
        p.setSeed(5);
        loadState(p, block);
        CHECK(raw(p, pid::drift) == Approx(0.2f));
        CHECK(p.getSeed() == 5);
    }
    SECTION("stateVersion 1 loads")
    {
        const auto block = withAttribute(src, "stateVersion", "1");
        DriftboxAudioProcessor p;
        loadState(p, block);
        CHECK(raw(p, pid::drift) == Approx(0.9f));
        CHECK(raw(p, pid::outGain) == Approx(-6.0f).margin(0.05f));
        CHECK(p.getSeed() == 0x1111111111111111ULL);
    }
    SECTION("missing stateVersion loads as version 1")
    {
        const auto block = withAttribute(src, "stateVersion", nullptr);
        DriftboxAudioProcessor p;
        loadState(p, block);
        CHECK(raw(p, pid::drift) == Approx(0.9f));
        CHECK(p.getSeed() == 0x1111111111111111ULL);
    }
    SECTION("future profileVersion is rejected")
    {
        const auto block =
            withAttribute(src, "profileVersion", juce::String(kProfileVersion + 1).toRawUTF8());
        DriftboxAudioProcessor p;
        setValue(p, pid::drift, 0.2f);
        p.setSeed(5);
        loadState(p, block);
        CHECK(raw(p, pid::drift) == Approx(0.2f));
        CHECK(p.getSeed() == 5);
    }
    SECTION("missing profileVersion loads as version 1")
    {
        const auto block = withAttribute(src, "profileVersion", nullptr);
        DriftboxAudioProcessor p;
        loadState(p, block);
        CHECK(raw(p, pid::drift) == Approx(0.9f));
        CHECK(p.getSeed() == 0x1111111111111111ULL);
    }
}

TEST_CASE("A missing or malformed seed keeps the current one", "[state]")
{
    DriftboxAudioProcessor src;
    setValue(src, pid::drift, 0.9f);

    auto check = [&](const char* value)
    {
        const auto block = withAttribute(src, "driftSeed", value);
        DriftboxAudioProcessor p;
        p.setSeed(0xabcdefULL);
        loadState(p, block);
        CHECK(p.getSeed() == 0xabcdefULL);
        CHECK(raw(p, pid::drift) == Approx(0.9f)); // il resto dello stato viene caricato
    };

    SECTION("missing")
    {
        check(nullptr);
    }
    SECTION("empty")
    {
        check("");
    }
    SECTION("not hexadecimal")
    {
        check("zzzz");
    }
    SECTION("hexadecimal with a prefix")
    {
        check("0x00000000000000ff");
    }
    SECTION("too long")
    {
        check("00000000000000000");
    }
    SECTION("with spaces")
    {
        check("00 000000000000ff");
    }
}

TEST_CASE("A short or upper-case seed is accepted", "[state]")
{
    DriftboxAudioProcessor src;
    DriftboxAudioProcessor p;

    loadState(p, withAttribute(src, "driftSeed", "ff"));
    CHECK(p.getSeed() == 0xffULL);

    loadState(p, withAttribute(src, "driftSeed", "FFFFFFFFFFFFFFFF"));
    CHECK(p.getSeed() == 0xffffffffffffffffULL);
}

TEST_CASE("New instances get different seeds", "[state]")
{
    std::set<std::uint64_t> seeds;
    for (int i = 0; i < 8; ++i)
    {
        DriftboxAudioProcessor p;
        seeds.insert(p.getSeed());
    }
    CHECK(seeds.size() == 8);
}
