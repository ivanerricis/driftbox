// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/PresetBar.h"

namespace driftbox::ui
{
PresetBar::NameButton::NameButton() : juce::Button("Choose preset")
{
    setTitle("Choose preset");
    setTooltip("Choose a preset");
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void PresetBar::NameButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour(highlighted || down ? col::bg3 : col::bg2);
    g.fillRoundedRectangle(area, kButtonRadius);

    g.setColour(col::ink);
    g.setFont(fonts::sans(12.5f, fonts::Weight::medium));
    g.drawText(name_, area.withTrimmedLeft(10.0f).withTrimmedRight(30.0f), juce::Justification::centredLeft,
               true);
    drawIcon(g, Icon::caretDown,
             juce::Rectangle<float>(area.getRight() - 20.0f, area.getY(), 10.0f, area.getHeight()),
             col::ink2);
    if (hasKeyboardFocus(false))
    {
        g.setColour(col::ink2);
        g.drawRoundedRectangle(area.reduced(0.5f), kButtonRadius, 1.0f);
    }
}

PresetBar::PresetBar()
{
    setTitle("Presets");
    for (juce::Component* c :
         {static_cast<juce::Component*>(&prev_), static_cast<juce::Component*>(&nameButton_),
          static_cast<juce::Component*>(&next_), static_cast<juce::Component*>(&actions_)})
    {
        addAndMakeVisible(*c);
    }
    nameButton_.setLabelText(name_);
    prev_.onClick = [this]
    {
        if (onPrev)
        {
            onPrev();
        }
    };
    next_.onClick = [this]
    {
        if (onNext)
        {
            onNext();
        }
    };
    nameButton_.onClick = [this]
    {
        if (onOpenList)
        {
            onOpenList();
        }
    };
    actions_.onClick = [this]
    {
        if (onOpenActions)
        {
            onOpenActions();
        }
    };
}

void PresetBar::setPresetName(const juce::String& name)
{
    name_ = name;
    nameButton_.setLabelText(name);
}

void PresetBar::resized()
{
    auto b = getLocalBounds().withSizeKeepingCentre(kTotalW, kH);
    prev_.setBounds(b.removeFromLeft(kButtonW));
    b.removeFromLeft(kGap);
    nameButton_.setBounds(b.removeFromLeft(kNameW));
    b.removeFromLeft(kGap);
    next_.setBounds(b.removeFromLeft(kButtonW));
    b.removeFromLeft(kGap);
    actions_.setBounds(b.removeFromLeft(kButtonW));
}
} // namespace driftbox::ui
