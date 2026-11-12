// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/Footer.h"

#include "PluginInfo.h"
#include "ui/Theme.h"

namespace driftbox::ui
{
const char* Footer::defaultHint()
{
    return "Give every track its own channel numbers. At Drift 0 % Driftbox is transparent.";
}

void Footer::setHint(const juce::String& text)
{
    const auto& t = text.isEmpty() ? juce::String(defaultHint()) : text;
    if (t != hint_)
    {
        hint_ = t;
        repaint();
    }
}

void Footer::setCpu(float fraction)
{
    const auto t = "CPU " + juce::String(juce::jlimit(0.0f, 9.99f, fraction) * 100.0f, 1) + " %";
    if (t != cpu_)
    {
        cpu_ = t;
        if (showCpu_)
        {
            repaint();
        }
    }
}

void Footer::setShowCpu(bool show)
{
    if (show != showCpu_)
    {
        showCpu_ = show;
        repaint();
    }
}

juce::String Footer::rightText() const
{
    const juce::String version = juce::String("v") + info::kVersion;
    return showCpu_ ? cpu_ + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) + version : version;
}

void Footer::paint(juce::Graphics& g)
{
    g.fillAll(col::bg1);
    auto b = getLocalBounds().reduced(12, 0);

    g.setColour(col::ink2);
    g.setFont(fonts::mono(10.5f, fonts::Weight::regular));
    g.drawText(rightText(), b.removeFromRight(150), juce::Justification::centredRight, false);

    g.setFont(fonts::sans(11.0f, fonts::Weight::regular));
    g.drawText(hint_, b, juce::Justification::centredLeft, true);
}
} // namespace driftbox::ui
