// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ui/Widgets.h"
#include <functional>

namespace driftbox::ui
{
// Barra dei preset nella barra alta: [<] [nome v] [>] [...]. I pulsanti non hanno logica propria: l'editor
// collega il gestore dei preset ai ganci qui sotto (onPrev, onNext, onOpenList, onOpenActions) e aggiorna il
// nome mostrato (setPresetName) e l'indicatore di "modificato" (setModified: un pallino ambra dopo il nome).
class PresetBar final : public juce::Component
{
public:
    static constexpr int kButtonW = 26, kNameW = 158, kH = 28, kGap = 2;
    static constexpr int kTotalW = kButtonW * 3 + kNameW + kGap * 3;

    PresetBar();

    // Ganci (message thread). Se vuoti il pulsante non fa nulla.
    std::function<void()> onPrev, onNext, onOpenList, onOpenActions;

    void setPresetName(const juce::String& name);
    const juce::String& presetName() const noexcept { return name_; }
    // Pallino "modificato": i valori correnti differiscono dal preset caricato.
    void setModified(bool modified);
    bool isModified() const noexcept { return modified_; }

    IconButton& prevButton() noexcept { return prev_; }
    IconButton& nextButton() noexcept { return next_; }
    IconButton& actionsButton() noexcept { return actions_; }
    juce::Button& nameButton() noexcept { return nameButton_; }

    void resized() override;

private:
    // Pulsante con il nome del preset e la freccia del menu.
    class NameButton final : public juce::Button
    {
    public:
        NameButton();
        void setLabelText(const juce::String& n, bool modified)
        {
            name_ = n;
            modified_ = modified;
            setTitle(modified ? n + " (modified)" : n);
            repaint();
        }
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;

    private:
        juce::String name_;
        bool modified_ = false;
    };

    IconButton prev_{Icon::chevronLeft, "Previous preset", "Previous preset", true};
    NameButton nameButton_;
    IconButton next_{Icon::chevronRight, "Next preset", "Next preset", true};
    IconButton actions_{Icon::dots, "Preset actions", "Preset actions (save, delete)", true};
    juce::String name_{"Default"};
    bool modified_ = false;
};
} // namespace driftbox::ui
