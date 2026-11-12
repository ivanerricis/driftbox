// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DriftEngine.h"
#include "params/DriftboxParameters.h"
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>

namespace driftbox
{
inline constexpr int kDefaultUiWidth = 720;
inline constexpr int kDefaultUiHeight = 440;

// SCAFFOLD (T0): processor minimo. Seed nello stato, Randomize, undo/redo, preset e impostazioni arrivano con
// i task T4 e T8 (vedi docs/PLAN.md).
class DriftboxAudioProcessor final : public juce::AudioProcessor
{
public:
    DriftboxAudioProcessor();
    ~DriftboxAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioParameterBool* getBypassParameter() const override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts_; }
    MeterState& getMeters() noexcept { return engine_.meters(); }

    // Seed corrente delle tolleranze (message thread o audio thread).
    std::uint64_t getSeed() const noexcept { return seed_.load(std::memory_order_acquire); }

    // Dimensione dell'editor, salvata nello stato del plugin.
    juce::Point<int> getUiSize() const;
    void setUiSize(int width, int height);

private:
    // Unico punto per le migrazioni dello stato; riceve la versione trovata nello stato salvato.
    static void migrateState(juce::ValueTree& state, int fromVersion);

    juce::AudioProcessorValueTreeState apvts_;
    ParamRefs params_;
    DriftEngine engine_;
    std::atomic<std::uint64_t> seed_{0};
    double currentSampleRate_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DriftboxAudioProcessor)
};
} // namespace driftbox
