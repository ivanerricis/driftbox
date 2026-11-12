// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "PluginInfo.h"

namespace driftbox
{
namespace
{
const juce::Identifier kStateVersionProp{"stateVersion"};
const juce::Identifier kUiWidthProp{"uiWidth"};
const juce::Identifier kUiHeightProp{"uiHeight"};
} // namespace

DriftboxAudioProcessor::DriftboxAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, juce::Identifier(kStateTag), createParameterLayout()), params_(apvts_)
{
    // Ogni nuova istanza parte con tolleranze proprie; uno stato salvato le sostituisce (task T4).
    seed_.store(static_cast<std::uint64_t>(juce::Random::getSystemRandom().nextInt64()),
                std::memory_order_release);
}

DriftboxAudioProcessor::~DriftboxAudioProcessor() = default;

const juce::String DriftboxAudioProcessor::getName() const
{
    return info::kName;
}

void DriftboxAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate_ = sampleRate;
    engine_.prepare(sampleRate, samplesPerBlock, juce::jmax(1, getTotalNumOutputChannels()));
    engine_.setSettings(params_.snapshot());
    engine_.setSeed(getSeed(), false);
}

void DriftboxAudioProcessor::releaseResources() {}

bool DriftboxAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
    {
        return false;
    }
    return out == layouts.getMainInputChannelSet();
}

void DriftboxAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto startTicks = juce::Time::getHighResolutionTicks();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
    {
        buffer.clear(ch, 0, buffer.getNumSamples());
    }

    engine_.setSettings(params_.snapshot());
    engine_.setSeed(getSeed(), true);
    engine_.process(buffer);

    // Frazione smussata del tempo del blocco spesa qui (economico, realtime safe).
    if (currentSampleRate_ > 0.0 && buffer.getNumSamples() > 0)
    {
        const double elapsed =
            juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - startTicks);
        const double budget = buffer.getNumSamples() / currentSampleRate_;
        auto& load = engine_.meters().cpuLoad;
        const float prev = load.load(std::memory_order_relaxed);
        load.store(prev + 0.1f * (static_cast<float>(elapsed / budget) - prev), std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* DriftboxAudioProcessor::createEditor()
{
    return new DriftboxAudioProcessorEditor(*this);
}

juce::AudioParameterBool* DriftboxAudioProcessor::getBypassParameter() const
{
    return dynamic_cast<juce::AudioParameterBool*>(apvts_.getParameter(pid::bypass));
}

void DriftboxAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    state.setProperty(kStateVersionProp, kStateVersion, nullptr);
    if (auto xml = state.createXml())
    {
        copyXmlToBinary(*xml, destData);
    }
}

void DriftboxAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
    {
        return;
    }

    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(kStateTag))
    {
        return;
    }

    auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.isValid())
    {
        return;
    }

    // Una versione mancante vale 1. Uno stato scritto da un plugin più recente non viene caricato.
    const int version = static_cast<int>(tree.getProperty(kStateVersionProp, 1));
    if (version > kStateVersion)
    {
        return;
    }

    migrateState(tree, version);
    apvts_.replaceState(tree);
}

void DriftboxAudioProcessor::migrateState(juce::ValueTree&, int)
{
    // Punto per le migrazioni future: quando kStateVersion aumenta, aggiornare l'albero qui passo per passo
    // (if (fromVersion < 2) { ... }). La versione 1 è il formato corrente, quindi per ora non c'è nulla da
    // fare.
}

juce::Point<int> DriftboxAudioProcessor::getUiSize() const
{
    const auto& s = apvts_.state;
    return {juce::jlimit(200, 4000, static_cast<int>(s.getProperty(kUiWidthProp, kDefaultUiWidth))),
            juce::jlimit(200, 4000, static_cast<int>(s.getProperty(kUiHeightProp, kDefaultUiHeight)))};
}

void DriftboxAudioProcessor::setUiSize(int width, int height)
{
    apvts_.state.setProperty(kUiWidthProp, width, nullptr);
    apvts_.state.setProperty(kUiHeightProp, height, nullptr);
}
} // namespace driftbox
