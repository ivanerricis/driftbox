// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"

#include "PluginInfo.h"

namespace driftbox
{
DriftboxAudioProcessorEditor::DriftboxAudioProcessorEditor(DriftboxAudioProcessor& processor)
    : AudioProcessorEditor(processor), processor_(processor)
{
    const auto size = processor_.getUiSize();
    setSize(size.x, size.y);
}

void DriftboxAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff15171a));
    g.setColour(juce::Colour(0xffd8dde3));
    g.setFont(juce::FontOptions(20.0f));
    g.drawText(juce::String(info::kName) + " " + info::kVersion, getLocalBounds(),
               juce::Justification::centred);
}

void DriftboxAudioProcessorEditor::resized() {}
} // namespace driftbox
