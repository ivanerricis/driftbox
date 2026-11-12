// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ui/Fonts.h"
#include "ui/Theme.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace driftbox::ui
{
// Menu a comparsa disegnato DENTRO l'editor (non una finestra nativa), come nel mockup: sfondo menuBg, angoli
// a 6 px, voci alte 24 px, titoli di sezione in maiuscolo. Copre tutta l'area del genitore con uno strato
// trasparente che raccoglie i clic fuori dal menu (che lo chiudono). Niente cicli modali: il proprietario lo
// crea, lo aggiunge al genitore e lo distrugge quando riceve onDismiss.
class OverlayMenu final : public juce::Component
{
public:
    struct Item
    {
        enum class Kind
        {
            header, // titolo di sezione ("FACTORY", "USER")
            item,   // voce selezionabile
            note    // testo informativo ("No user presets yet")
        };
        Kind kind = Kind::item;
        juce::String text;
        bool enabled = true;
        bool checked = false; // spunta ambra a destra (preset corrente)
        std::function<void()> action;
    };

    static constexpr int kItemH = 24, kHeaderH = 20, kNoteH = 22, kPad = 4, kGap = 1;

    // `topLeft` e `width` sono in coordinate del genitore. onDismiss viene chiamata una volta, quando il menu
    // vuole essere chiuso (clic fuori, Escape o voce scelta): il proprietario lo distrugge in modo differito.
    OverlayMenu(juce::String title, std::vector<Item> items, juce::Point<int> topLeft, int width,
                std::function<void()> onDismiss);

    const std::vector<Item>& items() const noexcept { return items_; }
    // Esegue la voce `index` (se abilitata) e chiude il menu, come un clic. Falso se non eseguibile.
    bool activate(int index);
    void dismiss();

    // Chiamata dal proprietario dopo l'aggiunta al genitore: dimensiona il menu e prende il focus.
    void open();

    void paint(juce::Graphics&) override;
    bool hitTest(int x, int y) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    int itemHeight(const Item& i) const;
    int contentHeight() const;
    juce::Rectangle<int> panelBounds() const;
    juce::Rectangle<int> itemBounds(int index) const; // nel sistema del componente, già scorso
    int indexAt(juce::Point<int>) const;
    void moveSelection(int direction);

    std::vector<Item> items_;
    juce::Point<int> topLeft_;
    int width_;
    std::function<void()> onDismiss_;
    int hover_ = -1;
    int scroll_ = 0;
    bool dismissed_ = false;
};
} // namespace driftbox::ui
