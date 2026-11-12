// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/PresetDialog.h"

namespace driftbox::ui
{
PresetDialog::PresetDialog(PresetManager& presets, Mode mode, std::function<void()> onApplied)
    : presets_(presets), mode_(mode), onApplied_(std::move(onApplied)),
      confirm_(mode == Mode::saveAs ? "SAVE" : "DELETE",
               mode == Mode::saveAs ? "Save the preset" : "Delete the preset file")
{
    setTitle(mode == Mode::saveAs ? "Save preset" : "Delete preset");
    presets_.rescan();

    for (auto* b : {&confirm_, &cancel_})
    {
        b->setLook(11.0f, 0.06f, kButtonRadius);
    }
    cancel_.setColours(col::bg3, col::ink, col::bg3, col::ink);
    if (mode == Mode::saveAs)
    {
        confirm_.setColours(col::ink, col::bg0, col::ink, col::bg0);
    }
    else
    {
        confirm_.setColours(col::bypass, col::bg0, col::bypass, col::bg0);
    }
    confirm_.setToggleState(true, juce::dontSendNotification);

    addAndMakeVisible(confirm_);
    addAndMakeVisible(cancel_);
    confirm_.onClick = [this] { confirm(); };
    cancel_.onClick = [this] { requestClose(); };

    if (mode == Mode::saveAs)
    {
        // Parte dal nome del preset utente corrente (per sovrascriverlo), da "<nome> copy" se è di fabbrica.
        juce::String initial = "New preset";
        if (presets_.isCurrentUser())
        {
            initial = presets_.getCurrentName();
        }
        else if (presets_.getCurrentName().isNotEmpty())
        {
            initial = presets_.getCurrentName() + " copy";
        }
        name_.setFont(fonts::sans(12.0f));
        name_.setIndents(8, 0);
        name_.setBorder({0, 0, 0, 0});
        name_.setMultiLine(false);
        name_.setInputRestrictions(PresetManager::kMaxNameLength);
        name_.setSelectAllWhenFocused(true);
        name_.setText(initial, false);
        name_.setTitle("Preset name");
        name_.onTextChange = [this] { updateStatus(); };
        name_.onReturnKey = [this] { confirm(); };
        name_.onEscapeKey = [this] { requestClose(); };
        addAndMakeVisible(name_);
    }
    else
    {
        deleteName_ = presets_.getCurrentName();
    }
    updateStatus();
}

void PresetDialog::updateStatus()
{
    status_ = {};
    statusIsWarning_ = false;
    bool ok = true;
    if (mode_ == Mode::saveAs)
    {
        const auto clean = PresetManager::sanitiseName(name_.getText());
        if (clean.isEmpty())
        {
            status_ = "Enter a valid preset name";
            ok = false;
        }
        else if (presets_.isFactoryName(clean))
        {
            status_ = "This name belongs to a factory preset";
            ok = false;
        }
        else if (presets_.userPresetExists(clean))
        {
            status_ = "Replaces the existing preset with this name";
            statusIsWarning_ = true;
        }
    }
    confirm_.setEnabled(ok);
    confirm_.setAlpha(ok ? 1.0f : 0.45f);
    repaint();
}

bool PresetDialog::confirm()
{
    updateStatus();
    if (!confirm_.isEnabled())
    {
        return false;
    }
    juce::Result r = juce::Result::ok();
    if (mode_ == Mode::saveAs)
    {
        r = presets_.saveAs(name_.getText(), /*overwrite*/ true);
    }
    else
    {
        r = presets_.deleteCurrent();
    }
    if (r.failed())
    {
        status_ = r.getErrorMessage();
        statusIsWarning_ = false;
        repaint();
        return false;
    }
    if (onApplied_)
    {
        onApplied_();
    }
    requestClose();
    return true;
}

void PresetDialog::resized()
{
    const int h = mode_ == Mode::saveAs ? 150 : 118;
    setCard(kCardW, h);
    auto inner = card().reduced(14, 12);
    inner.removeFromTop(26); // titolo
    if (mode_ == Mode::saveAs)
    {
        inner.removeFromTop(6);
        name_.setBounds(inner.removeFromTop(28));
        inner.removeFromTop(2 + 18); // messaggio di stato
        inner.removeFromTop(6);
    }
    else
    {
        inner.removeFromTop(6 + 30);
    }
    auto buttons = inner.removeFromBottom(26);
    confirm_.setBounds(buttons.removeFromRight(mode_ == Mode::saveAs ? 64 : 72));
    buttons.removeFromRight(6);
    cancel_.setBounds(buttons.removeFromRight(72));
}

void PresetDialog::paint(juce::Graphics& g)
{
    paintCard(g);
    auto inner = card().reduced(14, 12);
    g.setColour(col::ink);
    g.setFont(fonts::sans(13.0f, fonts::Weight::semiBold, 0.04f));
    g.drawText(mode_ == Mode::saveAs ? "Save preset as" : "Delete preset", inner.removeFromTop(26),
               juce::Justification::centredLeft, false);
    inner.removeFromTop(6);
    if (mode_ == Mode::saveAs)
    {
        const auto field = inner.removeFromTop(28);
        g.setColour(col::bg0);
        g.fillRoundedRectangle(field.toFloat(), 3.0f);
        g.setColour(name_.hasKeyboardFocus(true) ? col::ink2 : col::bg3);
        g.drawRoundedRectangle(field.toFloat().reduced(0.5f), 3.0f, 1.0f);
        inner.removeFromTop(2);
        g.setColour(statusIsWarning_ ? col::accent : col::bypass);
        g.setFont(fonts::sans(10.5f));
        g.drawText(status_, inner.removeFromTop(18), juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour(col::ink2);
        g.setFont(fonts::sans(11.5f));
        g.drawFittedText("Delete \"" + deleteName_ + "\"? The preset file is removed and Init is loaded.",
                         inner.removeFromTop(30), juce::Justification::topLeft, 2);
        if (status_.isNotEmpty())
        {
            g.setColour(col::bypass);
            g.drawText(status_, inner.removeFromTop(18), juce::Justification::centredLeft, true);
        }
    }
}
} // namespace driftbox::ui
