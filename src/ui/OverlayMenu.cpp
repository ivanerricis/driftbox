// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/OverlayMenu.h"

namespace driftbox::ui
{
namespace
{
const juce::Colour kHighlight{0xff394252};
} // namespace

OverlayMenu::OverlayMenu(const juce::String& title, std::vector<Item> items, juce::Point<int> topLeft,
                         int width, std::function<void()> onDismiss)
    : items_(std::move(items)), topLeft_(topLeft), width_(width), onDismiss_(std::move(onDismiss))
{
    setTitle(title);
    setWantsKeyboardFocus(true);
    setInterceptsMouseClicks(true, false);
}

void OverlayMenu::open()
{
    if (auto* parent = getParentComponent())
    {
        setBounds(parent->getLocalBounds());
    }
    grabKeyboardFocus();
}

int OverlayMenu::itemHeight(const Item& i) const
{
    return i.kind == Item::Kind::header ? kHeaderH : i.kind == Item::Kind::note ? kNoteH : kItemH;
}

int OverlayMenu::contentHeight() const
{
    int h = 0;
    for (const auto& i : items_)
    {
        h += itemHeight(i) + kGap;
    }
    return juce::jmax(0, h - kGap);
}

juce::Rectangle<int> OverlayMenu::panelBounds() const
{
    // Non oltre il fondo del genitore (lascia lo spazio del piè di pagina): il resto scorre con la rotellina.
    const int maxH = juce::jmax(60, getHeight() - topLeft_.y - kFooterH - 6);
    const int h = juce::jmin(contentHeight() + 2 * kPad, maxH);
    return {topLeft_.x, topLeft_.y, width_, h};
}

juce::Rectangle<int> OverlayMenu::itemBounds(int index) const
{
    const auto panel = panelBounds();
    int y = panel.getY() + kPad - scroll_;
    for (int i = 0; i < index; ++i)
    {
        y += itemHeight(items_[static_cast<size_t>(i)]) + kGap;
    }
    return {panel.getX() + kPad, y, panel.getWidth() - 2 * kPad,
            itemHeight(items_[static_cast<size_t>(index)])};
}

int OverlayMenu::indexAt(juce::Point<int> p) const
{
    const auto panel = panelBounds();
    if (!panel.reduced(0, kPad).contains(p))
    {
        return -1;
    }
    for (int i = 0; i < static_cast<int>(items_.size()); ++i)
    {
        if (items_[static_cast<size_t>(i)].kind == Item::Kind::item && itemBounds(i).contains(p))
        {
            return i;
        }
    }
    return -1;
}

bool OverlayMenu::hitTest(int, int)
{
    return true; // modale: raccoglie tutti i clic dell'editor finché è aperto
}

void OverlayMenu::paint(juce::Graphics& g)
{
    const auto panel = panelBounds();
    juce::DropShadow(juce::Colours::black.withAlpha(0.45f), 12, {0, 6}).drawForRectangle(g, panel);
    g.setColour(col::menuBg);
    g.fillRoundedRectangle(panel.toFloat(), 6.0f);

    g.saveState();
    g.reduceClipRegion(panel.reduced(0, kPad));
    for (int i = 0; i < static_cast<int>(items_.size()); ++i)
    {
        const auto& it = items_[static_cast<size_t>(i)];
        const auto r = itemBounds(i);
        if (!r.intersects(panel))
        {
            continue;
        }
        switch (it.kind)
        {
            case Item::Kind::header:
                g.setColour(col::ink2);
                g.setFont(fonts::sans(9.5f, fonts::Weight::medium, 0.08f));
                g.drawText(it.text.toUpperCase(), r.withTrimmedLeft(8).withTrimmedTop(4),
                           juce::Justification::centredLeft, false);
                break;
            case Item::Kind::note:
                g.setColour(col::ink2);
                g.setFont(fonts::sans(11.5f));
                g.drawText(it.text, r.withTrimmedLeft(8), juce::Justification::centredLeft, true);
                break;
            case Item::Kind::item:
            {
                if (it.checked || (i == hover_ && it.enabled))
                {
                    g.setColour(kHighlight);
                    g.fillRoundedRectangle(r.toFloat(), 3.0f);
                }
                g.setColour(it.enabled ? col::ink : col::ink3.withAlpha(0.6f));
                g.setFont(fonts::sans(12.0f));
                g.drawText(it.text, r.withTrimmedLeft(8).withTrimmedRight(24),
                           juce::Justification::centredLeft, true);
                if (it.checked)
                {
                    juce::Path tick;
                    const auto c = juce::Point<float>(static_cast<float>(r.getRight()) - 14.0f,
                                                      static_cast<float>(r.getCentreY()));
                    tick.startNewSubPath(c.x - 4.0f, c.y);
                    tick.lineTo(c.x - 1.0f, c.y + 3.0f);
                    tick.lineTo(c.x + 4.0f, c.y - 3.5f);
                    g.setColour(col::accent);
                    g.strokePath(tick, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                                            juce::PathStrokeType::rounded));
                }
                break;
            }
        }
    }
    g.restoreState();
}

void OverlayMenu::mouseMove(const juce::MouseEvent& e)
{
    const int i = indexAt(e.getPosition());
    if (i != hover_)
    {
        hover_ = i;
        repaint();
    }
}

void OverlayMenu::mouseExit(const juce::MouseEvent&)
{
    if (hover_ != -1)
    {
        hover_ = -1;
        repaint();
    }
}

void OverlayMenu::mouseDown(const juce::MouseEvent& e)
{
    const int i = indexAt(e.getPosition());
    if (i >= 0)
    {
        activate(i);
    }
    else if (!panelBounds().contains(e.getPosition()))
    {
        dismiss(); // clic fuori dal menu
    }
}

void OverlayMenu::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const int overflow = contentHeight() + 2 * kPad - panelBounds().getHeight();
    if (overflow <= 0)
    {
        return;
    }
    scroll_ = juce::jlimit(0, overflow, scroll_ - juce::roundToInt(w.deltaY * 120.0f));
    repaint();
}

void OverlayMenu::moveSelection(int direction)
{
    const int n = static_cast<int>(items_.size());
    int i = hover_;
    for (int step = 0; step < n; ++step)
    {
        i = ((i + direction) % n + n) % n;
        const auto& it = items_[static_cast<size_t>(i)];
        if (it.kind == Item::Kind::item && it.enabled)
        {
            hover_ = i;
            // Porta la voce in vista.
            const auto panel = panelBounds().reduced(0, kPad);
            const auto r = itemBounds(i);
            if (r.getY() < panel.getY())
            {
                scroll_ -= panel.getY() - r.getY();
            }
            else if (r.getBottom() > panel.getBottom())
            {
                scroll_ += r.getBottom() - panel.getBottom();
            }
            scroll_ = juce::jmax(0, scroll_);
            repaint();
            return;
        }
    }
}

bool OverlayMenu::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        dismiss();
    }
    else if (key == juce::KeyPress::downKey)
    {
        moveSelection(1);
    }
    else if (key == juce::KeyPress::upKey)
    {
        moveSelection(-1);
    }
    else if (key == juce::KeyPress::returnKey)
    {
        if (hover_ >= 0)
        {
            activate(hover_);
        }
    }
    return true; // modale: nessun tasto arriva all'editor (undo e redo compresi)
}

bool OverlayMenu::activate(int index)
{
    if (dismissed_ || index < 0 || index >= static_cast<int>(items_.size()))
    {
        return false;
    }
    const auto& it = items_[static_cast<size_t>(index)];
    if (it.kind != Item::Kind::item || !it.enabled)
    {
        return false;
    }
    auto action = it.action;
    dismiss(); // prima chiude (in modo differito), poi esegue: l'azione non apre mai un altro menu
    if (action)
    {
        action();
    }
    return true;
}

void OverlayMenu::dismiss()
{
    if (!dismissed_)
    {
        dismissed_ = true;
        if (onDismiss_)
        {
            onDismiss_();
        }
    }
}
} // namespace driftbox::ui
