// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/Widgets.h"

#include <array>
#include <cmath>

namespace driftbox::ui
{
namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;
constexpr float kRotaryStart = kPi * 1.25f;
constexpr float kRotaryEnd = kPi * 2.75f;

// Ruota dentata del mockup: 8 denti trapezoidali tra raggio interno 5 e raggio esterno 7 (viewBox 16).
juce::Path makeGear()
{
    constexpr float c = 8.0f, ri = 5.0f, ro = 7.0f;
    constexpr int n = 8;
    const float step = 2.0f * kPi / static_cast<float>(n);
    juce::Path p;
    bool first = true;
    for (int i = 0; i < n; ++i)
    {
        const float a = static_cast<float>(i) * step;
        const std::array<std::pair<float, float>, 5> pts{{{a - step * 0.5f, ri},
                                                          {a - step * 0.22f, ri},
                                                          {a - step * 0.14f, ro},
                                                          {a + step * 0.14f, ro},
                                                          {a + step * 0.22f, ri}}};
        for (const auto& [t, r] : pts)
        {
            const float x = c + r * std::cos(t), y = c + r * std::sin(t);
            if (first)
            {
                p.startNewSubPath(x, y);
                first = false;
            }
            else
            {
                p.lineTo(x, y);
            }
        }
    }
    p.closeSubPath();
    return p;
}

struct IconSpec
{
    juce::Path path;
    float w, h, stroke;
    bool filled;
};

const IconSpec& specFor(Icon icon)
{
    static const auto specs = []
    {
        std::array<IconSpec, 10> s;
        auto svg = [](const char* d) { return juce::Drawable::parseSVGPath(d); };
        s[static_cast<size_t>(Icon::chevronLeft)] = {svg("M7.5 2.5 4 6l3.5 3.5"), 12.0f, 12.0f, 1.6f, false};
        s[static_cast<size_t>(Icon::chevronRight)] = {svg("M4.5 2.5 8 6l-3.5 3.5"), 12.0f, 12.0f, 1.6f,
                                                      false};
        s[static_cast<size_t>(Icon::caretDown)] = {svg("M2 3.5 5 6.5 8 3.5"), 10.0f, 10.0f, 1.4f, false};
        juce::Path dots;
        for (const float x : {3.0f, 7.0f, 11.0f})
        {
            dots.addEllipse(x - 1.3f, 7.0f - 1.3f, 2.6f, 2.6f);
        }
        s[static_cast<size_t>(Icon::dots)] = {dots, 14.0f, 14.0f, 0.0f, true};
        s[static_cast<size_t>(Icon::undo)] = {svg("M4.5 3 2 5.5 4.5 8 M2 5.5h6a3.5 3.5 0 0 1 0 7H6"), 14.0f,
                                              14.0f, 1.5f, false};
        s[static_cast<size_t>(Icon::redo)] = {svg("M9.5 3 12 5.5 9.5 8 M12 5.5H6a3.5 3.5 0 0 0 0 7h2"), 14.0f,
                                              14.0f, 1.5f, false};
        juce::Path gear = makeGear();
        gear.addEllipse(8.0f - 2.1f, 8.0f - 2.1f, 4.2f, 4.2f);
        s[static_cast<size_t>(Icon::gear)] = {gear, 16.0f, 16.0f, 1.3f, false};
        s[static_cast<size_t>(Icon::randomize)] = {
            svg("M2 4h2.5c1.6 0 2.4 1 3.1 2.2l.8 1.6C9.1 9 9.9 10 11.5 10H12 M2 10h2.5c.9 0 1.5-.3 2-.8 "
                "M8.6 4.8c.5-.5 1.1-.8 2-.8H12 M10.5 2 12 4l-1.5 2 M10.5 8 12 10l-1.5 2"),
            14.0f, 14.0f, 1.4f, false};
        s[static_cast<size_t>(Icon::selectorLeft)] = {svg("M8 2 3.5 7 8 12"), 12.0f, 14.0f, 2.0f, false};
        s[static_cast<size_t>(Icon::selectorRight)] = {svg("M4 2 8.5 7 4 12"), 12.0f, 14.0f, 2.0f, false};
        return s;
    }();
    return specs[static_cast<size_t>(icon)];
}

// Punto a distanza `radius` dal centro all'angolo "alla JUCE" (0 = ore 12, senso orario).
juce::Point<float> polar(juce::Point<float> c, float radius, float angle)
{
    return {c.x + radius * std::sin(angle), c.y - radius * std::cos(angle)};
}
} // namespace

void drawIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float scale)
{
    const auto& s = specFor(icon);
    const auto t = juce::AffineTransform::translation(-s.w * 0.5f, -s.h * 0.5f)
                       .scaled(scale)
                       .translated(area.getCentreX(), area.getCentreY());
    g.setColour(colour);
    if (s.filled)
    {
        g.fillPath(s.path, t);
    }
    else
    {
        g.strokePath(s.path,
                     juce::PathStrokeType(s.stroke * scale, juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded),
                     t);
    }
}

// ============================== IconButton
IconButton::IconButton(Icon icon, const juce::String& name, const juce::String& hint, bool filledBackground)
    : juce::Button(name), icon_(icon), filled_(filledBackground)
{
    setTitle(name);
    setTooltip(hint);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void IconButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto area = getLocalBounds().toFloat();
    const bool en = isEnabled();
    if (filled_ || (en && (highlighted || down)))
    {
        g.setColour(
            (en && (highlighted || down) ? col::bg3 : col::bg2).withMultipliedAlpha(en ? 1.0f : 0.5f));
        g.fillRoundedRectangle(area, kButtonRadius);
    }
    const auto colour = en ? (highlighted || down ? col::ink : colour_) : col::ink3.withAlpha(0.5f);
    drawIcon(g, icon_, area, colour);
    if (hasKeyboardFocus(false))
    {
        g.setColour(col::ink2);
        g.drawRoundedRectangle(area.reduced(0.5f), kButtonRadius, 1.0f);
    }
}

// ============================== PillButton
PillButton::PillButton(const juce::String& text, const juce::String& hint) : juce::Button(text)
{
    setTitle(text);
    setTooltip(hint);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void PillButton::setLabel(const juce::String& label)
{
    setButtonText(label);
    repaint();
}

void PillButton::setLeadingIcon(Icon icon, juce::Colour colour)
{
    icon_ = icon;
    iconColour_ = colour;
    repaint();
}

void PillButton::setColours(juce::Colour offBg, juce::Colour offFg, juce::Colour onBg, juce::Colour onFg)
{
    offBg_ = offBg;
    offFg_ = offFg;
    onBg_ = onBg;
    onFg_ = onFg;
    repaint();
}

void PillButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto area = getLocalBounds().toFloat();
    const bool on = getToggleState();
    auto bg = on ? onBg_ : offBg_;
    if (highlighted || down)
    {
        bg = on ? bg.brighter(0.08f) : col::bg3;
    }
    g.setColour(bg);
    g.fillRoundedRectangle(area, radius_);

    const auto fg = on ? onFg_ : offFg_;
    const auto font = fonts::sans(fontPx_, fonts::Weight::semiBold, tracking_);
    juce::GlyphArrangement ga;
    ga.addLineOfText(font, getButtonText(), 0.0f, 0.0f);
    const float textW = ga.getBoundingBox(0, -1, true).getWidth();
    constexpr float iconW = 14.0f, iconGap = 6.0f;
    const float total = textW + (icon_.has_value() ? iconW + iconGap : 0.0f);
    float x = area.getCentreX() - total * 0.5f;
    if (icon_.has_value())
    {
        drawIcon(g, *icon_, {x, area.getY(), iconW, area.getHeight()}, iconColour_);
        x += iconW + iconGap;
    }
    g.setColour(fg);
    g.setFont(font);
    g.drawText(getButtonText(), juce::Rectangle<float>(x, area.getY(), textW + 2.0f, area.getHeight()),
               juce::Justification::centredLeft, false);
    if (hasKeyboardFocus(false))
    {
        g.setColour(col::ink2);
        g.drawRoundedRectangle(area.reduced(0.5f), radius_, 1.0f);
    }
}

// ============================== KnobSlider
KnobSlider::KnobSlider()
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setRotaryParameters(kRotaryStart, kRotaryEnd, true);
    setSliderSnapsToMousePosition(false);
    setMouseDragSensitivity(170);
    setPopupMenuEnabled(false);
}

void KnobSlider::mouseDown(const juce::MouseEvent& e)
{
    // Ctrl+click (Cmd+click su macOS) riporta al valore predefinito (un gesto completo).
    if (e.mods.isLeftButtonDown() && e.mods.isCommandDown())
    {
        resetClick_ = true;
        juce::Slider::mouseDoubleClick(e); // torna al valore del doppio click
        return;
    }
    resetClick_ = false;
    juce::Slider::mouseDown(e);
}

void KnobSlider::mouseDrag(const juce::MouseEvent& e)
{
    if (!resetClick_)
    {
        juce::Slider::mouseDrag(e);
    }
}

void KnobSlider::mouseUp(const juce::MouseEvent& e)
{
    if (resetClick_)
    {
        resetClick_ = false;
    }
    else
    {
        juce::Slider::mouseUp(e);
    }
}

void KnobSlider::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown() && !e.mods.isCommandDown() && onTypeRequest)
    {
        onTypeRequest();
    }
}

void KnobSlider::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (!isEnabled() || w.deltaY == 0.0f)
    {
        return;
    }
    const double dir = (w.deltaY > 0.0f ? 1.0 : -1.0) * (w.isReversed ? -1.0 : 1.0);
    const double p = juce::jlimit(0.0, 1.0, valueToProportionOfLength(getValue()) + dir * 0.02);
    // Un gesto (e quindi un passo di undo) per ogni scatto della rotellina.
    juce::Slider::ScopedDragNotification drag(*this);
    setValue(proportionOfLengthToValue(p), juce::sendNotificationSync);
}

void KnobSlider::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f - strokeWidth * 0.5f - 1.0f;
    const float prop = static_cast<float>(valueToProportionOfLength(getValue()));
    const float endAngle = kRotaryStart + (kRotaryEnd - kRotaryStart) * prop;
    const juce::PathStrokeType stroke(strokeWidth, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc(c.x, c.y, r, r, 0.0f, kRotaryStart, kRotaryEnd, true);
    g.setColour(col::bg3);
    g.strokePath(track, stroke);

    // Come nel mockup, sotto mezzo grado di arco non si disegna nulla.
    if ((endAngle - kRotaryStart) * 180.0f / kPi >= 0.5f)
    {
        juce::Path arc;
        arc.addCentredArc(c.x, c.y, r, r, 0.0f, kRotaryStart, endAngle, true);
        g.setColour(arcColour.withMultipliedAlpha(isEnabled() ? 1.0f : 0.4f));
        g.strokePath(arc, stroke);
    }

    g.setColour(col::bg2);
    g.fillEllipse(c.x - discRadius, c.y - discRadius, discRadius * 2.0f, discRadius * 2.0f);

    // Lancetta con capi arrotondati (stroke-linecap del mockup).
    juce::Path pointer;
    pointer.startNewSubPath(polar(c, r * 0.28f, endAngle));
    pointer.lineTo(polar(c, r * 0.62f, endAngle));
    g.setColour(col::ink);
    g.strokePath(pointer, juce::PathStrokeType(strokeWidth * 0.5f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    if (hasKeyboardFocus(false))
    {
        g.setColour(col::ink2);
        g.drawEllipse(b.reduced(0.5f), 1.0f);
    }
}

// ============================== LabelledKnob
LabelledKnob::LabelledKnob(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
                           const juce::String& label, const juce::String& hint, Fmt fmt, const Look& look)
    : label_(label.toUpperCase()), fmt_(fmt), look_(look)
{
    auto* param = apvts.getParameter(paramID);
    jassert(param != nullptr);

    setTitle(label);
    setDescription(hint);
    setTooltip(hint);

    slider_.setTitle(label);
    slider_.setDescription(hint);
    slider_.setTooltip(hint);
    slider_.strokeWidth = look.stroke;
    slider_.discRadius = look.disc;
    slider_.arcColour = look.arc;
    addAndMakeVisible(slider_);

    attachment_ = std::make_unique<juce::SliderParameterAttachment>(*param, slider_, nullptr);
    slider_.setDoubleClickReturnValue(true, param->convertFrom0to1(param->getDefaultValue()));
    slider_.onValueChange = [this] { updateText(); };
    slider_.onTypeRequest = [this] { beginTyping(); };
    updateText();
}

void LabelledKnob::updateText()
{
    const double v = slider_.getValue();
    juce::String t;
    switch (fmt_)
    {
        case Fmt::percent:
            t = juce::String(juce::roundToInt(v * 100.0)) + " %";
            break;
        case Fmt::dbSigned:
            t = (v > 0.049 ? "+" : "") + juce::String(v, 1) + " dB";
            break;
    }
    if (t != valueText_)
    {
        valueText_ = t;
        repaint();
    }
}

double LabelledKnob::parse(const juce::String& text) const
{
    const double n = text.retainCharacters("0123456789.-+").getDoubleValue();
    return fmt_ == Fmt::percent ? n / 100.0 : n;
}

void LabelledKnob::beginTyping()
{
    if (editor_ != nullptr)
    {
        return;
    }
    editor_ = std::make_unique<juce::TextEditor>();
    editor_->setTitle(label_);
    editor_->setJustification(juce::Justification::centred);
    editor_->setFont(fonts::mono(look_.valuePx, fonts::Weight::regular));
    editor_->setIndents(0, 0);
    editor_->setBorder({0, 0, 0, 0});
    editor_->setText(valueText_.retainCharacters("0123456789.-+"), false);
    editor_->onReturnKey = [this] { endTyping(true); };
    editor_->onEscapeKey = [this] { endTyping(false); };
    editor_->onFocusLost = [this] { endTyping(true); };
    addAndMakeVisible(*editor_);
    editor_->setBounds(getLocalBounds().removeFromBottom(kValueH).reduced(2, 0));
    if (editor_->isShowing())
    {
        editor_->grabKeyboardFocus();
    }
    editor_->selectAll();
}

void LabelledKnob::commitText(const juce::String& text)
{
    // Un gesto completo: un passo di undo, e l'host lo vede come gesto di automazione.
    juce::Slider::ScopedDragNotification drag(slider_);
    slider_.setValue(juce::jlimit(slider_.getMinimum(), slider_.getMaximum(), parse(text)),
                     juce::sendNotificationSync);
}

void LabelledKnob::endTyping(bool commit)
{
    if (editor_ == nullptr)
    {
        return;
    }
    auto ed = std::move(editor_);
    ed->onFocusLost = nullptr;
    if (commit)
    {
        commitText(ed->getText());
    }
    removeChildComponent(ed.get());
    // Distrutto in modo asincrono: può essere chiamato da un callback dello stesso editor.
    juce::MessageManager::callAsync([keep = std::shared_ptr<juce::TextEditor>(std::move(ed))] {});
    repaint();
}

void LabelledKnob::resized()
{
    auto b = getLocalBounds();
    b.removeFromTop(kLabelH + look_.gap);
    const auto kb = b.removeFromTop(look_.knobSize);
    slider_.setBounds(kb.withSizeKeepingCentre(look_.knobSize, look_.knobSize));
    if (editor_ != nullptr)
    {
        editor_->setBounds(getLocalBounds().removeFromBottom(kValueH).reduced(2, 0));
    }
}

void LabelledKnob::paint(juce::Graphics& g)
{
    auto b = getLocalBounds();
    g.setColour(col::ink3);
    g.setFont(fonts::sans(10.0f, fonts::Weight::medium, look_.labelTracking));
    g.drawText(label_, b.removeFromTop(kLabelH), juce::Justification::centred, false);
    if (editor_ == nullptr)
    {
        g.setColour(look_.valueColour);
        g.setFont(
            fonts::mono(look_.valuePx, look_.valueMedium ? fonts::Weight::medium : fonts::Weight::regular));
        g.drawText(valueText_, getLocalBounds().removeFromBottom(kValueH), juce::Justification::centred,
                   false);
    }
}

// ============================== ChannelSelector
ChannelSelector::ChannelSelector(juce::RangedAudioParameter& param, const juce::String& side,
                                 const juce::String& hint)
    : param_(param), numChannels_(juce::roundToInt(param.getNormalisableRange().end))
{
    setTitle("Channel " + side);
    setDescription(hint);
    setTooltip(hint);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    attachment_ = std::make_unique<juce::ParameterAttachment>(
        param_,
        [this](float v)
        {
            const int n = juce::jlimit(1, numChannels_, juce::roundToInt(v));
            if (n != value_)
            {
                value_ = n;
                repaint();
            }
        },
        nullptr);
    attachment_->sendInitialUpdate();
}

void ChannelSelector::step(int delta)
{
    setChannel((value_ - 1 + delta % numChannels_ + numChannels_) % numChannels_ + 1);
}

void ChannelSelector::setChannel(int channel)
{
    attachment_->setValueAsCompleteGesture(static_cast<float>(juce::jlimit(1, numChannels_, channel)));
}

void ChannelSelector::commitText(const juce::String& text)
{
    const auto digits = text.retainCharacters("0123456789");
    if (digits.isNotEmpty())
    {
        setChannel(digits.getIntValue());
    }
}

void ChannelSelector::beginTyping()
{
    if (editor_ != nullptr)
    {
        return;
    }
    editor_ = std::make_unique<juce::TextEditor>();
    editor_->setTitle(getTitle());
    editor_->setJustification(juce::Justification::centred);
    editor_->setFont(fonts::mono(20.0f, fonts::Weight::medium));
    editor_->setInputRestrictions(2, "0123456789");
    editor_->setIndents(0, 0);
    editor_->setBorder({0, 0, 0, 0});
    editor_->setText(format(value_), false);
    editor_->onReturnKey = [this] { endTyping(true); };
    editor_->onEscapeKey = [this] { endTyping(false); };
    editor_->onFocusLost = [this] { endTyping(true); };
    addAndMakeVisible(*editor_);
    editor_->setBounds(boxArea().reduced(2, 4));
    if (editor_->isShowing())
    {
        editor_->grabKeyboardFocus();
    }
    editor_->selectAll();
}

void ChannelSelector::endTyping(bool commit)
{
    if (editor_ == nullptr)
    {
        return;
    }
    auto ed = std::move(editor_);
    ed->onFocusLost = nullptr;
    if (commit)
    {
        commitText(ed->getText());
    }
    removeChildComponent(ed.get());
    juce::MessageManager::callAsync([keep = std::shared_ptr<juce::TextEditor>(std::move(ed))] {});
    repaint();
}

void ChannelSelector::resized()
{
    if (editor_ != nullptr)
    {
        editor_->setBounds(boxArea().reduced(2, 4));
    }
}

void ChannelSelector::mouseDown(const juce::MouseEvent& e)
{
    if (!e.mods.isLeftButtonDown())
    {
        return;
    }
    if (prevArea().contains(e.getPosition()))
    {
        step(-1);
    }
    else if (nextArea().contains(e.getPosition()))
    {
        step(1);
    }
}

void ChannelSelector::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown() && boxArea().contains(e.getPosition()))
    {
        beginTyping();
    }
}

void ChannelSelector::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (w.deltaY != 0.0f)
    {
        // Un gesto per ogni scatto; verso in su = canale successivo.
        step(((w.deltaY > 0.0f) != w.isReversed) ? 1 : -1);
    }
}

bool ChannelSelector::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::leftKey || k == juce::KeyPress::downKey)
    {
        step(-1);
        return true;
    }
    if (k == juce::KeyPress::rightKey || k == juce::KeyPress::upKey)
    {
        step(1);
        return true;
    }
    return false;
}

void ChannelSelector::paint(juce::Graphics& g)
{
    for (const auto& [area, icon] :
         {std::pair{prevArea(), Icon::selectorLeft}, std::pair{nextArea(), Icon::selectorRight}})
    {
        g.setColour(col::bg2);
        g.fillRoundedRectangle(area.toFloat(), kButtonRadius);
        drawIcon(g, icon, area.toFloat(), col::accent);
    }
    const auto box = boxArea();
    g.setColour(col::bg0);
    g.fillRoundedRectangle(box.toFloat(), kButtonRadius);
    if (editor_ == nullptr)
    {
        g.setColour(col::ink);
        g.setFont(fonts::mono(20.0f, fonts::Weight::medium));
        g.drawText(format(value_), box, juce::Justification::centred, false);
    }
    if (hasKeyboardFocus(false))
    {
        g.setColour(col::ink2);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), kButtonRadius, 1.0f);
    }
}

// ============================== StereoMeter
void StereoMeter::update(float leftDb, float rightDb, float dt)
{
    const float in[2]{leftDb, rightDb};
    bool changed = false;
    for (size_t i = 0; i < 2; ++i)
    {
        const float peak = juce::jmax(in[i], -100.0f);
        const float prev = levelDb_[i];
        levelDb_[i] = peak >= prev ? peak : juce::jmax(peak, prev - releaseDbPerSec * dt);
        changed = changed || std::abs(levelDb_[i] - prev) > 0.05f;
    }
    if (changed)
    {
        repaint();
    }
}

void StereoMeter::paint(juce::Graphics& g)
{
    for (int i = 0; i < 2; ++i)
    {
        const auto bar = juce::Rectangle<float>(static_cast<float>(i * (kBarW + kBarGap)), 0.0f,
                                                static_cast<float>(kBarW), static_cast<float>(getHeight()));
        g.setColour(col::bg0);
        g.fillRoundedRectangle(bar, 2.0f);
        const float u = juce::jlimit(0.0f, 1.0f, (levelDb_[static_cast<size_t>(i)] - floorDb) / -floorDb);
        if (u > 0.0f)
        {
            g.saveState();
            g.reduceClipRegion(bar.toNearestInt());
            g.setColour(col::accent.withAlpha(0.85f));
            g.fillRect(bar.withTop(bar.getBottom() - bar.getHeight() * u));
            g.restoreState();
        }
    }
}
} // namespace driftbox::ui
