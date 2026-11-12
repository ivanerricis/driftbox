// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/LookAndFeel.h"

#include "ui/Fonts.h"

namespace driftbox::ui
{
DriftboxLookAndFeel::DriftboxLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, col::bg0);
    setColour(juce::TextEditor::backgroundColourId, col::bg0);
    setColour(juce::TextEditor::textColourId, col::ink);
    setColour(juce::TextEditor::highlightColourId, col::accent.withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId, col::ink);
    setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::CaretComponent::caretColourId, col::accent);
    setColour(juce::TooltipWindow::backgroundColourId, col::menuBg);
    setColour(juce::TooltipWindow::textColourId, col::ink);
    setColour(juce::TooltipWindow::outlineColourId, col::bg3);
    setColour(juce::PopupMenu::backgroundColourId, col::menuBg);
    setColour(juce::PopupMenu::textColourId, col::ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff394252));
    setColour(juce::PopupMenu::highlightedTextColourId, col::ink);
}

juce::Font DriftboxLookAndFeel::getPopupMenuFont()
{
    return fonts::sans(12.0f);
}

juce::Rectangle<int> DriftboxLookAndFeel::getTooltipBounds(const juce::String& text,
                                                           juce::Point<int> screenPos,
                                                           juce::Rectangle<int> parentArea)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText(fonts::sans(11.0f), text, 0.0f, 0.0f);
    const int w = juce::jmin(static_cast<int>(std::ceil(ga.getBoundingBox(0, -1, true).getWidth())) + 16,
                             juce::jmax(80, parentArea.getWidth() - 8));
    const int h = 22;
    return juce::Rectangle<int>(
               screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
               screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 18, w, h)
        .constrainedWithin(parentArea);
}

void DriftboxLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto b = juce::Rectangle<int>(0, 0, width, height).toFloat();
    g.setColour(col::menuBg);
    g.fillRoundedRectangle(b, 4.0f);
    g.setColour(col::bg3.brighter(0.15f));
    g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);
    g.setColour(col::ink);
    g.setFont(fonts::sans(11.0f));
    g.drawText(text, b.reduced(8.0f, 0.0f), juce::Justification::centredLeft, true);
}

void DriftboxLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height,
                                                   juce::TextEditor&)
{
    g.setColour(col::bg0);
    g.fillRoundedRectangle(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 3.0f);
}
} // namespace driftbox::ui
