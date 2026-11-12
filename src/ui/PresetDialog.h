// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "presets/PresetManager.h"
#include "ui/ModalOverlay.h"
#include "ui/Widgets.h"
#include <functional>

namespace driftbox::ui
{
// Finestra sovrapposta per i preset utente: "Save As..." (nome del preset) e conferma di "Delete". Esegue
// l'operazione sul PresetManager e poi si chiude (onClose); onApplied dice al proprietario che libreria o
// preset corrente sono cambiati.
class PresetDialog final : public ModalOverlay
{
public:
    enum class Mode
    {
        saveAs,
        confirmDelete
    };
    static constexpr int kCardW = 340;

    PresetDialog(PresetManager& presets, Mode mode, std::function<void()> onApplied);

    Mode mode() const noexcept { return mode_; }
    juce::TextEditor& nameEditor() noexcept { return name_; }
    PillButton& confirmButton() noexcept { return confirm_; }
    PillButton& cancelButton() noexcept { return cancel_; }
    // Ricalcola messaggio e stato di SAVE dal testo corrente (il TextEditor notifica in modo asincrono).
    void refreshStatus() { updateStatus(); }
    // Messaggio sotto il campo (nome non valido, nome riservato, "sostituisce..."), vuoto se tutto bene.
    const juce::String& statusText() const noexcept { return status_; }
    // Come il clic su SAVE / DELETE. Falso se l'operazione non è riuscita (il motivo è in statusText()).
    bool confirm();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void updateStatus();

    PresetManager& presets_;
    Mode mode_;
    std::function<void()> onApplied_;
    juce::TextEditor name_;
    PillButton confirm_;
    PillButton cancel_{"CANCEL", "Close without changing anything"};
    juce::String status_;
    bool statusIsWarning_ = false; // ambra (sostituisce) invece del rosso (errore)
    juce::String deleteName_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetDialog)
};
} // namespace driftbox::ui
