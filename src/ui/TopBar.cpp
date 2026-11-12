// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/TopBar.h"

#include "params/ParamIDs.h"

namespace driftbox::ui
{
namespace
{
constexpr int kLogoSize = 22, kItemGap = 8, kSidePad = 10, kButton = 28;
const auto kWordmarkFont = [] { return fonts::sans(13.0f, fonts::Weight::semiBold, 0.14f); };

float textWidth(const juce::Font& f, const juce::String& s)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText(f, s, 0.0f, 0.0f);
    return ga.getBoundingBox(0, -1, true).getWidth();
}
} // namespace

void TopBar::drawLogoMark(juce::Graphics& g, juce::Rectangle<float> area)
{
    // Quadrato con foro (fill-rule evenodd) in un viewBox 256 x 256.
    static const juce::Path square = []
    {
        auto p = juce::Drawable::parseSVGPath("M58 58h140v140H58z M80 80v96h96V80z");
        p.setUsingNonZeroWinding(false);
        return p;
    }();
    const float s = juce::jmin(area.getWidth(), area.getHeight()) / 256.0f;
    const auto place = juce::AffineTransform::scale(s).translated(area.getX(), area.getY());
    g.setColour(col::ghost);
    g.fillPath(
        square,
        juce::AffineTransform::rotation(juce::degreesToRadians(-15.0f), 128.0f, 128.0f).followedBy(place));
    g.setColour(col::accent);
    g.fillPath(square, place);
}

TopBar::TopBar(juce::AudioProcessorValueTreeState& apvts)
{
    setTitle("Top bar");
    addAndMakeVisible(presetBar_);
    addAndMakeVisible(undo_);
    addAndMakeVisible(redo_);
    addAndMakeVisible(bypass_);
    addAndMakeVisible(gear_);

    undo_.setEnabled(false);
    redo_.setEnabled(false);
    undo_.onClick = [this]
    {
        if (onUndo)
        {
            onUndo();
        }
    };
    redo_.onClick = [this]
    {
        if (onRedo)
        {
            onRedo();
        }
    };
    gear_.onClick = [this]
    {
        if (onSettings)
        {
            onSettings();
        }
    };

    // Bypass: interruttore legato al parametro (gesto completo a ogni click, quindi annullabile).
    bypass_.setClickingTogglesState(true);
    bypass_.setLook(11.0f, 0.06f, kButtonRadius);
    bypass_.setColours(col::bg2, col::ink, col::bypass, col::bg0);
    bypassAttachment_ =
        std::make_unique<juce::ButtonParameterAttachment>(*apvts.getParameter(pid::bypass), bypass_, nullptr);
}

void TopBar::setHistoryState(bool canUndo, bool canRedo)
{
    undo_.setEnabled(canUndo);
    redo_.setEnabled(canRedo);
}

void TopBar::resized()
{
    auto b = getLocalBounds().reduced(kSidePad, 0);
    const int cy = getHeight() / 2;
    auto centred = [cy](int x, int w, int h) { return juce::Rectangle<int>(x, cy - h / 2, w, h); };

    int x = b.getX();
    logoArea_ = centred(x, kLogoSize, kLogoSize);
    x += kLogoSize + kItemGap;
    const int wordW = static_cast<int>(std::ceil(textWidth(kWordmarkFont(), "DRIFTBOX")));
    wordArea_ = centred(x, wordW, 18);
    x += wordW + 6 + kItemGap; // margine a destra del marchio + spazio tra gli elementi

    presetBar_.setBounds(centred(x, PresetBar::kTotalW, PresetBar::kH));
    x += PresetBar::kTotalW + kItemGap + 6 + kItemGap; // distanziatore da 6 px tra preset e undo

    undo_.setBounds(centred(x, kButton, kButton));
    x += kButton + kItemGap;
    redo_.setBounds(centred(x, kButton, kButton));
    x += kButton + kItemGap + 4; // margin-left del pulsante Bypass

    const int bypassW =
        static_cast<int>(std::ceil(textWidth(fonts::sans(11.0f, fonts::Weight::semiBold, 0.06f), "BYPASS"))) +
        20;
    bypass_.setBounds(centred(x, bypassW, kButton));

    gear_.setBounds(centred(b.getRight() - kButton, kButton, kButton));
}

void TopBar::paint(juce::Graphics& g)
{
    g.fillAll(col::bg1);
    drawLogoMark(g, logoArea_.toFloat());
    g.setColour(col::ink);
    g.setFont(kWordmarkFont());
    g.drawText("DRIFTBOX", wordArea_.expanded(4, 0), juce::Justification::centredLeft, false);
}
} // namespace driftbox::ui
