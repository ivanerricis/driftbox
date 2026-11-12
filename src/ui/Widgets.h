// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/Meters.h"
#include "ui/Fonts.h"
#include "ui/Theme.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <optional>

namespace driftbox::ui
{
// Icone del mockup (tracciati SVG nel loro viewBox originale), disegnate con contorno arrotondato.
enum class Icon
{
    chevronLeft,  // preset precedente
    chevronRight, // preset successivo
    caretDown,    // menu dei preset
    dots,         // azioni sul preset
    undo,
    redo,
    gear,
    randomize,
    selectorLeft, // selettore di canale
    selectorRight,
    close // chiusura delle finestre sovrapposte (impostazioni)
};

// Disegna l'icona centrata in `area`, scalata in modo che il suo viewBox misuri `scale` x (1 = pixel del
// mockup).
void drawIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour,
              float scale = 1.0f);

// Pulsante con sola icona (preset, undo, redo, gear): sfondo opzionale, evidenziato al passaggio del mouse.
class IconButton final : public juce::Button
{
public:
    IconButton(Icon icon, const juce::String& name, const juce::String& hint, bool filledBackground);

    void setIconColour(juce::Colour c)
    {
        colour_ = c;
        repaint();
    }
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon_;
    juce::Colour colour_{col::ink2};
    bool filled_;
};

// Pulsante con testo (BYPASS, SHOW/HIDE, RANDOMIZE): sfondo che cambia se acceso, icona opzionale.
class PillButton final : public juce::Button
{
public:
    PillButton(const juce::String& text, const juce::String& hint);

    void setLabel(const juce::String& label);
    void setLeadingIcon(Icon icon, juce::Colour colour);
    void setLook(float fontPx, float tracking, float radius)
    {
        fontPx_ = fontPx;
        tracking_ = tracking;
        radius_ = radius;
        repaint();
    }
    // Colori di sfondo e testo da spento e da acceso.
    void setColours(juce::Colour offBg, juce::Colour offFg, juce::Colour onBg, juce::Colour onFg);

    void paintButton(juce::Graphics&, bool highlighted, bool down) override;

private:
    std::optional<Icon> icon_;
    juce::Colour iconColour_{col::accent};
    juce::Colour offBg_{col::bg2}, offFg_{col::ink}, onBg_{col::ink}, onFg_{col::bg0};
    float fontPx_ = 11.0f, tracking_ = 0.06f, radius_ = kButtonRadius;
};

// Slider rotativo con doppio click per digitare, Ctrl/Cmd+click per il valore predefinito e rotellina a passi
// del 2 %. Disegna da sé la manopola del mockup: traccia, arco del valore, disco e lancetta.
class KnobSlider final : public juce::Slider
{
public:
    KnobSlider();

    std::function<void()> onTypeRequest;
    float strokeWidth = 4.0f;
    float discRadius = 13.0f;
    juce::Colour arcColour = col::accent;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void paint(juce::Graphics&) override;

private:
    bool resetClick_ = false;
};

// Manopola con etichetta sopra e valore sotto, legata a un parametro.
class LabelledKnob final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Fmt
    {
        percent,  // 0..1 -> "75 %"
        dbSigned, // dB -> "-2.5 dB", "+3.0 dB", "0.0 dB"
    };

    struct Look
    {
        int knobSize = 46;
        float stroke = 4.0f;
        float disc = 13.0f;
        float labelTracking = 0.06f;
        int gap = 3; // spazio tra etichetta, manopola e valore
        float valuePx = 10.5f;
        bool valueMedium = false;
        juce::Colour valueColour = col::ink2;
        juce::Colour arc = col::accent;
    };

    LabelledKnob(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
                 const juce::String& label, const juce::String& hint, Fmt fmt, const Look& look);

    KnobSlider& slider() noexcept { return slider_; }
    juce::String valueText() const { return valueText_; }
    // Applica un testo digitato come valore (un gesto completo = un passo di undo).
    void commitText(const juce::String& text);
    // Altezza complessiva del componente con etichetta, manopola e valore.
    int preferredHeight() const noexcept
    {
        return kLabelH + look_.gap + look_.knobSize + look_.gap + kValueH;
    }

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    static constexpr int kLabelH = 12, kValueH = 14;

    void updateText();
    void beginTyping();
    void endTyping(bool commit);
    double parse(const juce::String&) const;

    KnobSlider slider_;
    std::unique_ptr<juce::SliderParameterAttachment> attachment_;
    std::unique_ptr<juce::TextEditor> editor_;
    juce::String label_, valueText_;
    Fmt fmt_;
    Look look_;
};

// Selettore di canale "‹ 07 ›" legato a un parametro intero 1..N: frecce con ritorno a capo, rotellina,
// doppio click sul numero per digitarlo. Ogni azione è un gesto completo (un passo di undo).
class ChannelSelector final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChannelSelector(juce::RangedAudioParameter& param, const juce::String& side, const juce::String& hint);

    int value() const noexcept { return value_; }
    // Azione dell'utente: sposta di `delta` canali con ritorno a capo 1..N (come un click su una freccia).
    void step(int delta);
    // Azione dell'utente: imposta il canale (limitato a 1..N).
    void setChannel(int channel);
    // Applica un testo digitato (numero di canale).
    void commitText(const juce::String& text);

    static juce::String format(int channel) { return juce::String(channel).paddedLeft('0', 2); }

    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

    static constexpr int kArrowW = 24, kBoxW = 52, kH = 36, kInnerGap = 4;
    juce::Rectangle<int> prevArea() const { return {0, 0, kArrowW, kH}; }
    juce::Rectangle<int> boxArea() const { return {kArrowW + kInnerGap, 0, kBoxW, kH}; }
    juce::Rectangle<int> nextArea() const
    {
        return {kArrowW + kInnerGap + kBoxW + kInnerGap, 0, kArrowW, kH};
    }
    static constexpr int kTotalW = kArrowW * 2 + kBoxW + kInnerGap * 2;

private:
    void beginTyping();
    void endTyping(bool commit);

    juce::RangedAudioParameter& param_;
    std::unique_ptr<juce::ParameterAttachment> attachment_;
    std::unique_ptr<juce::TextEditor> editor_;
    int value_ = 1, numChannels_ = 1;
};

// Meter stereo verticale (due barre da 6 px) con balistica: sale subito, scende a velocità fissa.
class StereoMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr float floorDb = -60.0f;
    static constexpr int kBarW = 6, kBarGap = 3, kBarH = 92;
    static constexpr int kTotalW = kBarW * 2 + kBarGap;

    // Picchi più recenti in dBFS e tempo trascorso dalla chiamata precedente.
    void update(float leftDb, float rightDb, float dtSec);
    float displayDb(int side) const noexcept { return levelDb_[static_cast<size_t>(side)]; }

    void paint(juce::Graphics&) override;

private:
    static constexpr float releaseDbPerSec = 40.0f;
    float levelDb_[2]{-100.0f, -100.0f};
};
} // namespace driftbox::ui
