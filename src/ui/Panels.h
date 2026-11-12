// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DriftChannel.h"
#include "ui/Widgets.h"
#include <array>
#include <functional>

namespace driftbox::ui
{
// Pannello arrotondato con il titolo in alto (stile comune dei riquadri del mockup).
void drawPanelBackground(juce::Graphics& g, juce::Rectangle<int> bounds);
void drawPanelTitle(juce::Graphics& g, const juce::String& title, juce::Rectangle<int> area,
                    juce::Justification just);

// "CONSOLE CHANNEL": selettori del canale L e R e pulsante RANDOMIZE.
class ChannelPanel final : public juce::Component
{
public:
    explicit ChannelPanel(juce::AudioProcessorValueTreeState& apvts);

    // Chiamato dal pulsante Randomize (l'editor lo collega a processor.randomize()).
    std::function<void()> onRandomize;

    ChannelSelector& selector(int side) noexcept { return side == 0 ? selL_ : selR_; }
    PillButton& randomizeButton() noexcept { return randomize_; }

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    ChannelSelector selL_, selR_;
    PillButton randomize_{"RANDOMIZE", "Pick a new random pair of console channels (one undo step)"};
    juce::Rectangle<int> titleArea_, labelArea_[2];
};

// "TOLERANCE OF THIS CHANNEL PAIR": curve di deviazione (+-3 dB, 20 Hz - 20 kHz) e valori di L e R.
class TolerancePanel final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int kPoints = 121; // punti della curva (asse delle frequenze logaritmico)
    static constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f;

    // Tutto ciò da cui dipende il disegno. Il pannello si ricalcola e si ridisegna solo se cambia.
    struct Inputs
    {
        int channelL = 1, channelR = 2;
        float drift = 0.0f, freqTol = 0.0f, phaseTol = 0.0f, gainTol = 0.0f, saturation = 0.0f;
        double sampleRate = 48000.0;
        bool bypassed = false;
        bool operator==(const Inputs&) const = default;
    };

    TolerancePanel();

    // Imposta gli ingressi; ritorna vero se qualcosa è cambiato (e quindi si è ridisegnato).
    bool setInputs(const Inputs& in);
    const Inputs& inputs() const noexcept { return in_; }

    // Deviazione in dB del punto i (0..kPoints-1) della curva del lato (0 = L, 1 = R), non ancora limitata
    // a +-3 dB. Per i test.
    float curveDb(int side, int i) const noexcept
    {
        return curve_[static_cast<size_t>(side)][static_cast<size_t>(i)];
    }
    // Testo dei valori numerici del lato (es. "gain +0.12 dB").
    juce::String gainText(int side) const { return gain_[static_cast<size_t>(side)]; }
    juce::String delayText(int side) const { return delay_[static_cast<size_t>(side)]; }
    juce::String driveText(int side) const { return drive_[static_cast<size_t>(side)]; }

    static float frequencyAt(int i) noexcept;

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void recompute();
    void paintPlot(juce::Graphics&, juce::Rectangle<float> plot) const;
    void paintStats(juce::Graphics&, juce::Rectangle<int> area, int side) const;

    Inputs in_;
    std::array<std::array<float, kPoints>, 2> curve_{};
    std::array<juce::String, 2> gain_, delay_, drive_;
    juce::Rectangle<int> titleArea_, badgeArea_, legendArea_, yLabels_, plotArea_, xLabels_, statsArea_[2];
};

// "DRIFT AMOUNT": la manopola grande.
class DriftPanel final : public juce::Component
{
public:
    explicit DriftPanel(juce::AudioProcessorValueTreeState& apvts);

    LabelledKnob& knob() noexcept { return knob_; }

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    LabelledKnob knob_;
};

// "ADVANCED": pulsante SHOW/HIDE e le manopole Freq, Phase, Gain, Saturation, Noise.
class AdvancedPanel final : public juce::Component
{
public:
    explicit AdvancedPanel(juce::AudioProcessorValueTreeState& apvts);

    bool isOpen() const noexcept { return open_; }
    void setOpen(bool open);
    PillButton& toggleButton() noexcept { return toggle_; }
    LabelledKnob& knob(int index) noexcept { return *knobs_[static_cast<size_t>(index)]; }
    static constexpr int kNumKnobs = 5;

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    bool open_ = false;
    PillButton toggle_{"SHOW", "Show or hide the advanced tolerance weights"};
    std::array<std::unique_ptr<LabelledKnob>, kNumKnobs> knobs_;
    juce::Rectangle<int> titleArea_, bodyArea_;
};

// "OUTPUT": guadagno di uscita e meter stereo.
class OutputPanel final : public juce::Component
{
public:
    explicit OutputPanel(juce::AudioProcessorValueTreeState& apvts);

    LabelledKnob& knob() noexcept { return knob_; }
    StereoMeter& meter() noexcept { return meter_; }

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    LabelledKnob knob_;
    StereoMeter meter_;
};
} // namespace driftbox::ui
