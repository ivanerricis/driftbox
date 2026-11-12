// SPDX-License-Identifier: AGPL-3.0-or-later
#include "params/DriftboxParameters.h"

namespace driftbox
{
namespace
{
using juce::ParameterID;
using Attr = juce::AudioParameterFloatAttributes;

ParameterID makeID(const juce::String& id)
{
    return ParameterID{id, kParamVersion};
}

juce::String dbText(float v, int)
{
    return juce::String(v, 1) + " dB";
}

juce::String percentText(float v, int)
{
    return juce::String(juce::roundToInt(v * 100.0f)) + " %";
}

// Parser del testo digitato, inversi dei formattatori sopra: accettano la forma mostrata (con unità) e un
// numero semplice, così il valore digitato nell'host coincide con quello visualizzato.
float dbValue(const juce::String& text)
{
    return text.getFloatValue();
}

float percentValue(const juce::String& text)
{
    return text.getFloatValue() / 100.0f;
}

std::unique_ptr<juce::AudioParameterFloat> unitParam(const juce::String& id, const juce::String& name,
                                                     float def)
{
    return std::make_unique<juce::AudioParameterFloat>(
        makeID(id), name, juce::NormalisableRange<float>(0.0f, 1.0f), def,
        Attr().withStringFromValueFunction(percentText).withValueFromStringFunction(percentValue));
}

juce::StringArray channelNames()
{
    juce::StringArray names;
    for (const int n : kChannelChoices)
    {
        names.add(juce::String(n));
    }
    return names;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Automatabile: il cambio del numero di voci è senza click e non cambia la latenza.
    layout.add(std::make_unique<juce::AudioParameterChoice>(makeID(pid::channels), "Channels", channelNames(),
                                                            kDefaultChannelIndex));
    layout.add(unitParam(pid::drift, "Drift Amount", 0.5f));
    layout.add(unitParam(pid::freqTol, "Frequency Tolerance", 0.5f));
    layout.add(unitParam(pid::phaseTol, "Phase Tolerance", 0.5f));
    layout.add(unitParam(pid::gainTol, "Gain Tolerance", 0.5f));
    layout.add(unitParam(pid::saturation, "Saturation", 0.2f));
    layout.add(unitParam(pid::noise, "Noise", 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        makeID(pid::outGain), "Output Gain", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
        Attr().withLabel("dB").withStringFromValueFunction(dbText).withValueFromStringFunction(dbValue)));
    layout.add(std::make_unique<juce::AudioParameterBool>(makeID(pid::bypass), "Bypass", false));

    return layout;
}

ParamRefs::ParamRefs(juce::AudioProcessorValueTreeState& apvts)
    : channels(apvts.getRawParameterValue(pid::channels)), drift(apvts.getRawParameterValue(pid::drift)),
      freqTol(apvts.getRawParameterValue(pid::freqTol)), phaseTol(apvts.getRawParameterValue(pid::phaseTol)),
      gainTol(apvts.getRawParameterValue(pid::gainTol)),
      saturation(apvts.getRawParameterValue(pid::saturation)), noise(apvts.getRawParameterValue(pid::noise)),
      outGain(apvts.getRawParameterValue(pid::outGain)), bypass(apvts.getRawParameterValue(pid::bypass))
{
    jassert(channels && drift && freqTol && phaseTol && gainTol && saturation && noise && outGain && bypass);
}

EngineSettings ParamRefs::snapshot() const noexcept
{
    constexpr auto relaxed = std::memory_order_relaxed;
    EngineSettings s;
    const int idx = juce::jlimit(0, static_cast<int>(kChannelChoices.size()) - 1,
                                 juce::roundToInt(channels->load(relaxed)));
    s.numVoices = kChannelChoices[static_cast<size_t>(idx)];
    s.drift01 = drift->load(relaxed);
    s.freqTol01 = freqTol->load(relaxed);
    s.phaseTol01 = phaseTol->load(relaxed);
    s.gainTol01 = gainTol->load(relaxed);
    s.saturation01 = saturation->load(relaxed);
    s.noise01 = noise->load(relaxed);
    s.outGainDb = outGain->load(relaxed);
    s.bypass = bypass->load(relaxed) >= 0.5f;
    return s;
}
} // namespace driftbox
