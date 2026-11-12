// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ui/Fonts.h"
#include "ui/Theme.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace driftbox::ui
{
// Base delle finestre sovrapposte (impostazioni, salva con nome, conferma di eliminazione): copre tutto il
// genitore con lo sfondo scurito del mockup (rgba(8,9,11,0.66)), raccoglie tutti i clic e i tasti (niente
// arriva ai controlli sotto: né il mouse né undo e redo) e si chiude con Escape. Disegna la scheda centrale
// (card()) con sfondo bg1 e angoli a 8 px; i contenuti li disegnano e li dispongono le classi derivate.
// Come OverlayMenu non usa cicli modali: il proprietario lo distrugge quando riceve onClose.
class ModalOverlay : public juce::Component
{
public:
    static constexpr int kCardRadius = 8;

    // Chiamata una volta quando la finestra vuole chiudersi (Escape, DONE, ...): il proprietario la nasconde
    // e la distrugge in modo differito.
    std::function<void()> onClose;

    // Dopo l'aggiunta al genitore: si dimensiona sul genitore, dispone i contenuti e prende il focus.
    void open();
    void requestClose();

    juce::Rectangle<int> card() const noexcept { return card_; }

    void paint(juce::Graphics&) override;
    bool hitTest(int, int) override { return true; }
    void mouseDown(const juce::MouseEvent&) override {}
    bool keyPressed(const juce::KeyPress&) override;

protected:
    ModalOverlay();
    // Altezza della scheda per la larghezza `cardWidth`; la scheda è centrata nel genitore.
    void setCard(int width, int height);
    // Disegna lo sfondo scurito e la scheda; le classi derivate lo chiamano all'inizio di paint().
    void paintCard(juce::Graphics&) const;

private:
    juce::Rectangle<int> card_;
    bool closing_ = false;
};
} // namespace driftbox::ui
