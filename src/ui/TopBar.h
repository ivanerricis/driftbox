// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ui/PresetBar.h"
#include "ui/Widgets.h"
#include <functional>

namespace driftbox::ui
{
// Barra alta (44 px): marchio e scritta DRIFTBOX, barra dei preset (T8), Undo, Redo, Bypass e, a destra,
// l'ingranaggio delle impostazioni.
class TopBar final : public juce::Component
{
public:
    explicit TopBar(juce::AudioProcessorValueTreeState& apvts);

    // Ganci: la finestra (editor) esegue undo e redo e tiene allineato lo stato dei pulsanti con
    // setHistoryState(). onSettings apre la finestra delle impostazioni (T8).
    std::function<void()> onUndo, onRedo, onSettings;
    void setHistoryState(bool canUndo, bool canRedo);

    PresetBar& presetBar() noexcept { return presetBar_; }
    IconButton& undoButton() noexcept { return undo_; }
    IconButton& redoButton() noexcept { return redo_; }
    PillButton& bypassButton() noexcept { return bypass_; }
    IconButton& settingsButton() noexcept { return gear_; }

    // Marchio: due quadrati con foro, quello dietro ruotato di -15 gradi (resources/brand/logo.svg).
    static void drawLogoMark(juce::Graphics&, juce::Rectangle<float> area);

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    juce::Rectangle<int> logoArea_, wordArea_;
    PresetBar presetBar_;
    IconButton undo_{Icon::undo, "Undo", "Undo the last change (Ctrl+Z)", false};
    IconButton redo_{Icon::redo, "Redo", "Redo (Ctrl+Shift+Z)", false};
    PillButton bypass_{"BYPASS", "Bypass: hear the dry input (20 ms crossfade)"};
    IconButton gear_{Icon::gear, "Settings", "Settings", false};
    std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment_;
};
} // namespace driftbox::ui
