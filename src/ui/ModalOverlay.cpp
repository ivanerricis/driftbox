// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/ModalOverlay.h"

namespace driftbox::ui
{
ModalOverlay::ModalOverlay()
{
    setWantsKeyboardFocus(true);
    setInterceptsMouseClicks(true, true);
}

void ModalOverlay::open()
{
    if (auto* parent = getParentComponent())
    {
        setBounds(parent->getLocalBounds());
    }
    resized();
    grabKeyboardFocus();
}

void ModalOverlay::setCard(int width, int height)
{
    card_ = juce::Rectangle<int>(0, 0, width, height).withCentre(getLocalBounds().getCentre());
}

void ModalOverlay::requestClose()
{
    if (!closing_)
    {
        closing_ = true;
        if (onClose)
        {
            onClose();
        }
    }
}

bool ModalOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        requestClose();
    }
    return true; // modale: nessun tasto arriva all'editor (undo e redo compresi)
}

void ModalOverlay::paint(juce::Graphics& g)
{
    paintCard(g);
}

void ModalOverlay::paintCard(juce::Graphics& g) const
{
    g.fillAll(juce::Colour::fromFloatRGBA(8.0f / 255.0f, 9.0f / 255.0f, 11.0f / 255.0f, 0.66f));
    juce::DropShadow(juce::Colours::black.withAlpha(0.5f), 20, {0, 8}).drawForRectangle(g, card_);
    g.setColour(col::bg1);
    g.fillRoundedRectangle(card_.toFloat(), static_cast<float>(kCardRadius));
}
} // namespace driftbox::ui
