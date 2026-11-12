// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ui/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace driftbox::ui
{
// Look and feel dell'editor: imposta i colori standard dei componenti JUCE usati (campi di testo, tooltip,
// menu) con la palette del mockup. I widget propri (manopole, pulsanti) si disegnano da soli.
class DriftboxLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    DriftboxLookAndFeel();

    juce::Font getPopupMenuFont() override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& text, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;
    void drawTextEditorOutline(juce::Graphics&, int, int, juce::TextEditor&) override {}
    void fillTextEditorBackground(juce::Graphics&, int width, int height, juce::TextEditor&) override;
};
} // namespace driftbox::ui
