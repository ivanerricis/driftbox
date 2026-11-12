// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace driftbox
{
// SCAFFOLD (T0): editor provvisorio. L'interfaccia definitiva arriva con il task T7, dopo l'approvazione del
// mockup (T6).
class DriftboxAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit DriftboxAudioProcessorEditor(DriftboxAudioProcessor& processor);
    ~DriftboxAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    DriftboxAudioProcessor& processor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DriftboxAudioProcessorEditor)
};
} // namespace driftbox
