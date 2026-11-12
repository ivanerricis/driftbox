// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/Panels.h"

#include "dsp/DriftProfile.h"
#include "dsp/ToleranceResponse.h"
#include "params/ParamIDs.h"
#include <cmath>

namespace driftbox::ui
{
namespace
{
// Caratteri non ASCII scritti come UTF-8 esplicito (immuni dalla codifica del file sorgente).
const juce::String kMinus{juce::CharPointer_UTF8("\xe2\x88\x92")};
const juce::String kMicro{juce::CharPointer_UTF8("\xc2\xb5")};

float textWidth(const juce::Font& f, const juce::String& s)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText(f, s, 0.0f, 0.0f);
    return ga.getBoundingBox(0, -1, true).getWidth();
}

juce::Font titleFont()
{
    return fonts::sans(10.0f, fonts::Weight::medium, 0.08f);
}

// "+0.12", "−0.30" con il segno meno tipografico.
juce::String signedText(double v, int decimals)
{
    const auto body = juce::String(std::abs(v), decimals);
    // Anche -0.00 (o un valore che arrotonda a zero) si mostra con il segno più.
    const bool negative = juce::String(v, decimals).startsWithChar('-') && body.containsAnyOf("123456789");
    return (negative ? kMinus : juce::String("+")) + body;
}
} // namespace

void drawPanelBackground(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(col::bg1);
    g.fillRoundedRectangle(bounds.toFloat(), kPanelRadius);
}

void drawPanelTitle(juce::Graphics& g, const juce::String& title, juce::Rectangle<int> area,
                    juce::Justification just)
{
    g.setColour(col::ink3);
    g.setFont(titleFont());
    g.drawText(title, area, just, false);
}

// ============================== ChannelPanel
ChannelPanel::ChannelPanel(juce::AudioProcessorValueTreeState& apvts)
    : selL_(*apvts.getParameter(pid::chanL), "L",
            "Left console channel, 1 to 64. Arrows, wheel or double-click to type."),
      selR_(*apvts.getParameter(pid::chanR), "R",
            "Right console channel, 1 to 64. Arrows, wheel or double-click to type.")
{
    setTitle("Console channel");
    addAndMakeVisible(selL_);
    addAndMakeVisible(selR_);
    addAndMakeVisible(randomize_);
    randomize_.setLook(11.0f, 0.06f, kButtonRadius);
    randomize_.setLeadingIcon(Icon::randomize, col::accent);
    randomize_.setColours(col::bg2, col::ink, col::bg2, col::ink);
    randomize_.onClick = [this]
    {
        if (onRandomize)
        {
            onRandomize();
        }
    };
}

void ChannelPanel::resized()
{
    // Colonna centrata in verticale: titolo, L, R, Randomize, con 9 px tra gli elementi.
    constexpr int titleH = 13, labelH = 14, labelGap = 4, gap = 9, buttonH = 28, buttonW = 108;
    constexpr int sideH = labelH + labelGap + ChannelSelector::kH;
    constexpr int total = titleH + gap + sideH + gap + sideH + gap + buttonH;
    int y = (getHeight() - total) / 2;
    const int cx = getWidth() / 2;

    titleArea_ = {0, y, getWidth(), titleH};
    y += titleH + gap;
    for (int s = 0; s < 2; ++s)
    {
        labelArea_[s] = {0, y, getWidth(), labelH};
        selector(s).setBounds(cx - ChannelSelector::kTotalW / 2, y + labelH + labelGap,
                              ChannelSelector::kTotalW, ChannelSelector::kH);
        y += sideH + gap;
    }
    randomize_.setBounds(cx - buttonW / 2, y, buttonW, buttonH);
}

void ChannelPanel::paint(juce::Graphics& g)
{
    drawPanelBackground(g, getLocalBounds());
    drawPanelTitle(g, "CONSOLE CHANNEL", titleArea_, juce::Justification::centred);
    g.setFont(fonts::sans(11.0f, fonts::Weight::semiBold));
    g.setColour(col::accent);
    g.drawText("L", labelArea_[0], juce::Justification::centred, false);
    g.setColour(col::curveR);
    g.drawText("R", labelArea_[1], juce::Justification::centred, false);
}

// ============================== TolerancePanel
TolerancePanel::TolerancePanel()
{
    setTitle("Tolerance of this channel pair");
    setTooltip("How far the left and right channel stray from ideal, in dB, from 20 Hz to 20 kHz.");
    recompute();
}

float TolerancePanel::frequencyAt(int i) noexcept
{
    return kMinHz * std::pow(kMaxHz / kMinHz, static_cast<float>(i) / static_cast<float>(kPoints - 1));
}

bool TolerancePanel::setInputs(const Inputs& in)
{
    if (in == in_)
    {
        return false;
    }
    in_ = in;
    recompute();
    repaint();
    return true;
}

void TolerancePanel::recompute()
{
    const VoiceAmounts amounts{in_.drift * in_.freqTol, in_.drift * in_.phaseTol, in_.drift * in_.gainTol,
                               in_.drift * in_.saturation, 0.0f};
    const int channels[2]{in_.channelL, in_.channelR};
    for (int s = 0; s < 2; ++s)
    {
        const auto tol = channelProfile(channels[s]).tol;
        auto& curve = curve_[static_cast<size_t>(s)];
        for (int i = 0; i < kPoints; ++i)
        {
            curve[static_cast<size_t>(i)] = static_cast<float>(
                toleranceDeviationDb(tol, amounts, static_cast<double>(frequencyAt(i)), in_.sampleRate));
        }
        const auto r = toleranceReadout(tol, amounts);
        gain_[static_cast<size_t>(s)] = "gain " + signedText(r.gainDb, 2) + " dB";
        delay_[static_cast<size_t>(s)] = "delay " + juce::String(r.delayMicros, 1) + " " + kMicro + "s";
        drive_[static_cast<size_t>(s)] = "drive " + signedText(std::round(r.drivePercent), 0) + " %";
    }
}

void TolerancePanel::resized()
{
    constexpr int pad = 10, gap = 6, headerH = 14, xLabelsH = 12, statsH = 22, yLabelsW = 22;
    auto b = getLocalBounds().reduced(pad);

    auto header = b.removeFromTop(headerH);
    const int titleW =
        static_cast<int>(std::ceil(textWidth(titleFont(), "TOLERANCE OF THIS CHANNEL PAIR"))) + 2;
    titleArea_ = header.removeFromLeft(titleW);
    const int badgeW = static_cast<int>(std::ceil(
                           textWidth(fonts::sans(10.0f, fonts::Weight::semiBold, 0.08f), "BYPASSED"))) +
                       12;
    badgeArea_ = juce::Rectangle<int>(titleArea_.getRight() + 10, header.getY(), badgeW, 15);
    const int legendW =
        static_cast<int>(std::ceil(textWidth(fonts::sans(10.0f), "L") + textWidth(fonts::sans(10.0f), "R"))) +
        2 * (12 + 5) + 10;
    legendArea_ = header.removeFromRight(legendW);

    b.removeFromTop(gap);
    auto stats = b.removeFromBottom(statsH);
    b.removeFromBottom(gap);
    xLabels_ = b.removeFromBottom(xLabelsH);
    b.removeFromBottom(gap);
    yLabels_ = b.removeFromLeft(yLabelsW);
    b.removeFromLeft(gap);
    plotArea_ = b;
    xLabels_.removeFromLeft(yLabelsW + gap);
    const int colW = (stats.getWidth() - gap) / 2;
    statsArea_[0] = stats.removeFromLeft(colW);
    stats.removeFromLeft(gap);
    statsArea_[1] = stats;
}

void TolerancePanel::paintPlot(juce::Graphics& g, juce::Rectangle<float> plot) const
{
    g.setColour(col::bg0);
    g.fillRoundedRectangle(plot, kButtonRadius);

    g.saveState();
    g.reduceClipRegion(plot.toNearestInt());
    // Linee ogni 0,5 dB (il grafico copre +-1,5 dB in 6 parti); quella dello 0 dB è più chiara.
    for (int i = 1; i <= 5; ++i)
    {
        const int y = juce::roundToInt(plot.getY() + plot.getHeight() * static_cast<float>(i) / 6.0f);
        g.setColour(i == 3 ? col::bg3 : col::bg2);
        g.drawHorizontalLine(y, plot.getX(), plot.getRight());
    }

    const juce::PathStrokeType stroke1(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const juce::PathStrokeType stroke2(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    for (int s : {1, 0}) // R sotto, L sopra
    {
        juce::Path p;
        for (int i = 0; i < kPoints; ++i)
        {
            const float db = juce::jlimit(-kResponseGraphDb * 1.5f, kResponseGraphDb * 1.5f,
                                          curve_[static_cast<size_t>(s)][static_cast<size_t>(i)]);
            const float x =
                plot.getX() + plot.getWidth() * static_cast<float>(i) / static_cast<float>(kPoints - 1);
            const float y = plot.getY() + plot.getHeight() * (0.5f - db / (2.0f * kResponseGraphDb));
            i == 0 ? p.startNewSubPath(x, y) : p.lineTo(x, y);
        }
        g.setColour(s == 0 ? col::accent : col::curveR);
        g.strokePath(p, s == 0 ? stroke2 : stroke1);
    }
    g.restoreState();
}

void TolerancePanel::paintStats(juce::Graphics& g, juce::Rectangle<int> area, int side) const
{
    g.setColour(col::bg2);
    g.fillRoundedRectangle(area.toFloat(), kButtonRadius);

    const auto name = juce::String(side == 0 ? "L " : "R ") +
                      ChannelSelector::format(side == 0 ? in_.channelL : in_.channelR);
    const auto& gain = gain_[static_cast<size_t>(side)];
    const auto& delay = delay_[static_cast<size_t>(side)];
    const auto& drive = drive_[static_cast<size_t>(side)];

    // Il mockup usa 10 px con 10 px tra i valori; se in uno spazio stretto non entrano si riduce il corpo.
    const float avail = static_cast<float>(area.getWidth() - 16);
    auto natural = [&](float px)
    {
        return textWidth(fonts::mono(px, fonts::Weight::medium), name) + textWidth(fonts::mono(px), gain) +
               textWidth(fonts::mono(px), delay) + textWidth(fonts::mono(px), drive);
    };
    float px = 10.0f;
    float gapW = 10.0f;
    while (natural(px) + 3.0f * gapW > avail && px > 8.0f)
    {
        px -= 0.25f;
        gapW = juce::jmax(4.0f, gapW - 0.5f);
    }

    float x = static_cast<float>(area.getX() + 8);
    const auto cell = [&](const juce::String& text, const juce::Font& f, juce::Colour c)
    {
        const float w = textWidth(f, text);
        g.setColour(c);
        g.setFont(f);
        g.drawText(text,
                   juce::Rectangle<float>(x, static_cast<float>(area.getY()), w + 2.0f,
                                          static_cast<float>(area.getHeight())),
                   juce::Justification::centredLeft, false);
        x += w + gapW;
    };
    cell(name, fonts::mono(px, fonts::Weight::medium), side == 0 ? col::accent : col::curveR);
    cell(gain, fonts::mono(px), col::ink2);
    cell(delay, fonts::mono(px), col::ink2);
    cell(drive, fonts::mono(px), col::ink2);
}

void TolerancePanel::paint(juce::Graphics& g)
{
    drawPanelBackground(g, getLocalBounds());
    drawPanelTitle(g, "TOLERANCE OF THIS CHANNEL PAIR", titleArea_, juce::Justification::centredLeft);

    if (in_.bypassed)
    {
        g.setColour(col::bypass);
        g.fillRoundedRectangle(badgeArea_.toFloat(), 3.0f);
        g.setColour(col::bg0);
        g.setFont(fonts::sans(10.0f, fonts::Weight::semiBold, 0.08f));
        g.drawText("BYPASSED", badgeArea_, juce::Justification::centred, false);
    }

    // Legenda: tratto da 12 x 2 px e lettera, L poi R.
    {
        float x = static_cast<float>(legendArea_.getX());
        const float cy = static_cast<float>(legendArea_.getCentreY());
        for (int s = 0; s < 2; ++s)
        {
            g.setColour(s == 0 ? col::accent : col::curveR);
            g.fillRect(x, cy - 1.0f, 12.0f, 2.0f);
            x += 12.0f + 5.0f;
            const juce::String letter = s == 0 ? "L" : "R";
            const float w = textWidth(fonts::sans(10.0f), letter);
            g.setColour(col::ink2);
            g.setFont(fonts::sans(10.0f));
            g.drawText(letter, juce::Rectangle<float>(x, cy - 7.0f, w + 2.0f, 14.0f),
                       juce::Justification::centredLeft, false);
            x += w + 10.0f;
        }
    }

    // Etichette dei dB e area del grafico (con trasparenza ridotta se il plugin è in bypass).
    if (in_.bypassed)
    {
        g.beginTransparencyLayer(0.35f);
    }
    g.setColour(col::ink3);
    g.setFont(fonts::mono(9.0f));
    const auto yl = yLabels_;
    g.drawText("+1.5", yl.getX(), yl.getY() + 2, yl.getWidth(), 12, juce::Justification::centredRight, false);
    g.drawText("0", yl.getX(), yl.getCentreY() - 6, yl.getWidth(), 12, juce::Justification::centredRight,
               false);
    g.drawText(kMinus + "1.5", yl.getX(), yl.getBottom() - 14, yl.getWidth(), 12,
               juce::Justification::centredRight, false);
    paintPlot(g, plotArea_.toFloat());
    if (in_.bypassed)
    {
        g.endTransparencyLayer();
    }

    // Etichette delle frequenze: distribuite come "space-between" del mockup.
    {
        const juce::StringArray labels{"20 Hz", "200", "2 k", "20 kHz"};
        const auto f = fonts::mono(9.0f);
        float widths[4], sum = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            widths[i] = textWidth(f, labels[i]);
            sum += widths[i];
        }
        const float spacing = (static_cast<float>(xLabels_.getWidth()) - sum) / 3.0f;
        float x = static_cast<float>(xLabels_.getX());
        g.setColour(col::ink3);
        g.setFont(f);
        for (int i = 0; i < 4; ++i)
        {
            g.drawText(labels[i],
                       juce::Rectangle<float>(x, static_cast<float>(xLabels_.getY()), widths[i] + 2.0f,
                                              static_cast<float>(xLabels_.getHeight())),
                       juce::Justification::centredLeft, false);
            x += widths[i] + spacing;
        }
    }

    paintStats(g, statsArea_[0], 0);
    paintStats(g, statsArea_[1], 1);
}

// ============================== DriftPanel
namespace
{
LabelledKnob::Look driftLook()
{
    LabelledKnob::Look l;
    l.knobSize = 76;
    l.stroke = 5.0f;
    l.disc = 24.0f;
    l.labelTracking = 0.08f;
    l.gap = 4;
    l.valuePx = 12.0f;
    l.valueMedium = true;
    l.valueColour = col::ink;
    return l;
}

LabelledKnob::Look smallLook(juce::Colour arc)
{
    LabelledKnob::Look l;
    l.arc = arc;
    return l;
}
} // namespace

DriftPanel::DriftPanel(juce::AudioProcessorValueTreeState& apvts)
    : knob_(apvts, pid::drift, "Drift Amount",
            "How far each channel strays from ideal. At 0 % the plugin is transparent.",
            LabelledKnob::Fmt::percent, driftLook())
{
    setTitle("Drift amount");
    addAndMakeVisible(knob_);
}

void DriftPanel::resized()
{
    const int h = knob_.preferredHeight();
    knob_.setBounds(getLocalBounds().withSizeKeepingCentre(getWidth() - 12, h));
}

void DriftPanel::paint(juce::Graphics& g)
{
    drawPanelBackground(g, getLocalBounds());
}

// ============================== AdvancedPanel
AdvancedPanel::AdvancedPanel(juce::AudioProcessorValueTreeState& apvts)
{
    setTitle("Advanced");
    struct Def
    {
        const char* id;
        const char* label;
        const char* hint;
    };
    const Def defs[kNumKnobs] = {
        {pid::freqTol, "Freq", "Frequency tolerance: weight of the shelf and bell deviations."},
        {pid::phaseTol, "Phase", "Phase tolerance: weight of the tiny delay between channels."},
        {pid::gainTol, "Gain", "Gain tolerance: weight of the level difference between channels."},
        {pid::saturation, "Saturation",
         "Saturation: how much each channel softly saturates (scaled by Drift)."},
        {pid::noise, "Noise", "Noise: very quiet console hiss (scaled by Drift)."}};
    for (int i = 0; i < kNumKnobs; ++i)
    {
        auto& k = knobs_[static_cast<size_t>(i)];
        k = std::make_unique<LabelledKnob>(apvts, defs[i].id, defs[i].label, defs[i].hint,
                                           LabelledKnob::Fmt::percent, smallLook(col::accent));
        addChildComponent(*k);
    }
    addAndMakeVisible(toggle_);
    toggle_.setLook(10.0f, 0.06f, 3.0f);
    toggle_.setColours(col::bg2, col::ink, col::ink, col::bg0);
    toggle_.onClick = [this]
    {
        setOpen(!open_);
        if (onOpenChanged)
        {
            onOpenChanged(open_);
        }
    };
}

void AdvancedPanel::setOpen(bool open)
{
    open_ = open;
    toggle_.setToggleState(open, juce::dontSendNotification);
    toggle_.setLabel(open ? "HIDE" : "SHOW");
    toggle_.setTooltip(open ? "Hide the advanced tolerance weights" : "Show the advanced tolerance weights");
    for (auto& k : knobs_)
    {
        k->setVisible(open);
    }
    resized();
    repaint();
}

void AdvancedPanel::resized()
{
    constexpr int padV = 8, padH = 6, headerH = 22, innerPad = 4, gap = 4;
    auto b = getLocalBounds().reduced(padH, padV);
    auto header = b.removeFromTop(headerH).reduced(innerPad, 0);
    titleArea_ = header.removeFromLeft(80);
    const int btnW = static_cast<int>(std::ceil(textWidth(fonts::sans(10.0f, fonts::Weight::semiBold, 0.06f),
                                                          open_ ? "HIDE" : "SHOW"))) +
                     16;
    toggle_.setBounds(header.removeFromRight(btnW));
    b.removeFromTop(2);
    bodyArea_ = b;

    const int cellW = (b.getWidth() - (kNumKnobs - 1) * gap) / kNumKnobs;
    for (int i = 0; i < kNumKnobs; ++i)
    {
        auto& k = *knobs_[static_cast<size_t>(i)];
        const int x = b.getX() + i * (cellW + gap);
        k.setBounds(juce::Rectangle<int>(x, b.getY(), cellW, b.getHeight())
                        .withSizeKeepingCentre(cellW, k.preferredHeight()));
    }
}

void AdvancedPanel::paint(juce::Graphics& g)
{
    drawPanelBackground(g, getLocalBounds());
    drawPanelTitle(g, "ADVANCED", titleArea_, juce::Justification::centredLeft);

    if (!open_)
    {
        // Due righe di testo centrate (margini laterali di 24 px).
        const auto area = bodyArea_.reduced(24, 0);
        auto layout = [&](const juce::String& text, const juce::Font& f, juce::Colour c)
        {
            juce::AttributedString a;
            a.setJustification(juce::Justification::centred);
            a.append(text, f, c);
            juce::TextLayout tl;
            tl.createLayout(a, static_cast<float>(area.getWidth()));
            return tl;
        };
        const auto l1 =
            layout("Drift sets how far each channel strays from ideal.", fonts::sans(12.0f), col::ink2);
        const auto l2 =
            layout("Open Advanced to weight frequency, phase, gain, saturation and noise separately.",
                   fonts::sans(11.0f), col::ink3);
        const float total = l1.getHeight() + 6.0f + l2.getHeight();
        const float y = static_cast<float>(area.getCentreY()) - total * 0.5f;
        l1.draw(g, juce::Rectangle<float>(static_cast<float>(area.getX()), y,
                                          static_cast<float>(area.getWidth()), l1.getHeight()));
        l2.draw(g, juce::Rectangle<float>(static_cast<float>(area.getX()), y + l1.getHeight() + 6.0f,
                                          static_cast<float>(area.getWidth()), l2.getHeight()));
    }
}

// ============================== OutputPanel
OutputPanel::OutputPanel(juce::AudioProcessorValueTreeState& apvts)
    : knob_(apvts, pid::outGain, "Output", "Output gain, -24 to +24 dB.", LabelledKnob::Fmt::dbSigned,
            smallLook(col::ink))
{
    setTitle("Output");
    addAndMakeVisible(knob_);
    addAndMakeVisible(meter_);
    meter_.setTooltip("Output level, left and right.");
    meter_.setTitle("Output level meter");
}

void OutputPanel::resized()
{
    constexpr int knobW = 46, gap = 10;
    const int total = knobW + gap + StereoMeter::kTotalW;
    const int x = (getWidth() - total) / 2;
    // Il componente è 10 px più largo della manopola (i valori tipo "+12.0 dB" non entrano in 46 px).
    knob_.setBounds(x - 5, (getHeight() - knob_.preferredHeight()) / 2, knobW + 10, knob_.preferredHeight());
    meter_.setBounds(x + knobW + gap, (getHeight() - StereoMeter::kBarH) / 2, StereoMeter::kTotalW,
                     StereoMeter::kBarH);
}

void OutputPanel::paint(juce::Graphics& g)
{
    drawPanelBackground(g, getLocalBounds());
}
} // namespace driftbox::ui
