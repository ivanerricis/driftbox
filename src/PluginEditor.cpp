// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"

#include "params/ParamIDs.h"
#include <cmath>

namespace driftbox
{
namespace
{
using namespace ui;

float rawValue(juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    return apvts.getRawParameterValue(id)->load();
}
} // namespace

DriftboxAudioProcessorEditor::DriftboxAudioProcessorEditor(DriftboxAudioProcessor& processor)
    : AudioProcessorEditor(&processor), processor_(processor), topBar_(processor.getAPVTS()),
      channelPanel_(processor.getAPVTS()), driftPanel_(processor.getAPVTS()),
      advancedPanel_(processor.getAPVTS()), outputPanel_(processor.getAPVTS())
{
    setTitle("Driftbox");
    setWantsKeyboardFocus(true); // i clic su controlli che non prendono il focus arrivano qui (undo e redo)
    setLookAndFeel(&lnf_);

    addAndMakeVisible(content_);
    content_.setTitle("Driftbox");
    for (juce::Component* c :
         {static_cast<juce::Component*>(&topBar_), static_cast<juce::Component*>(&channelPanel_),
          static_cast<juce::Component*>(&tolerancePanel_), static_cast<juce::Component*>(&driftPanel_),
          static_cast<juce::Component*>(&advancedPanel_), static_cast<juce::Component*>(&outputPanel_),
          static_cast<juce::Component*>(&footer_)})
    {
        content_.addAndMakeVisible(*c);
    }
    footer_.setInterceptsMouseClicks(false, false);

    topBar_.onUndo = [this] { undo(); };
    topBar_.onRedo = [this] { redo(); };
    topBar_.onSettings = [this]
    {
        if (onSettings)
        {
            onSettings();
        }
    };
    channelPanel_.onRandomize = [this] { processor_.randomize(); };

    // Dimensione: quella salvata se ha le nostre proporzioni, altrimenti la base.
    const auto stored = processor_.getUiSize();
    int w = kBaseWidth, h = kBaseHeight;
    const float aspect = static_cast<float>(kBaseWidth) / static_cast<float>(kBaseHeight);
    if (std::abs(static_cast<float>(stored.x) / static_cast<float>(stored.y) - aspect) < 0.02f &&
        static_cast<float>(stored.x) >= static_cast<float>(kBaseWidth) * kMinZoom - 1.0f &&
        static_cast<float>(stored.x) <= static_cast<float>(kBaseWidth) * kMaxZoom + 1.0f)
    {
        w = stored.x;
        h = juce::roundToInt(static_cast<float>(w) / aspect);
    }
    constrainer_.setFixedAspectRatio(static_cast<double>(aspect));
    constrainer_.setSizeLimits(juce::roundToInt(static_cast<float>(kBaseWidth) * kMinZoom),
                               juce::roundToInt(static_cast<float>(kBaseHeight) * kMinZoom),
                               juce::roundToInt(static_cast<float>(kBaseWidth) * kMaxZoom),
                               juce::roundToInt(static_cast<float>(kBaseHeight) * kMaxZoom));
    setResizable(true, true);
    setConstrainer(&constrainer_);

    content_.setSize(kBaseWidth, kBaseHeight);
    layoutContent();
    addMouseListener(this, true);
    setTooltipsEnabled(true);
    setSize(w, h);

    lastTickMs_ = juce::Time::getMillisecondCounterHiRes();
    refreshNow();
    startTimerHz(30);
}

DriftboxAudioProcessorEditor::~DriftboxAudioProcessorEditor()
{
    stopTimer();
    removeMouseListener(this);
    tooltip_.reset();
    setLookAndFeel(nullptr);
}

void DriftboxAudioProcessorEditor::setZoom(float zoom)
{
    zoom = juce::jlimit(kMinZoom, kMaxZoom, zoom);
    setSize(juce::roundToInt(static_cast<float>(kBaseWidth) * zoom),
            juce::roundToInt(static_cast<float>(kBaseHeight) * zoom));
}

void DriftboxAudioProcessorEditor::setTooltipsEnabled(bool enabled)
{
    if (enabled && tooltip_ == nullptr)
    {
        tooltip_ = std::make_unique<juce::TooltipWindow>(this, 700);
        tooltip_->setLookAndFeel(&lnf_);
    }
    else if (!enabled && tooltip_ != nullptr)
    {
        tooltip_.reset();
    }
}

bool DriftboxAudioProcessorEditor::undo()
{
    const bool done = processor_.getUndoManager().undo();
    refreshNow();
    return done;
}

bool DriftboxAudioProcessorEditor::redo()
{
    const bool done = processor_.getUndoManager().redo();
    refreshNow();
    return done;
}

bool DriftboxAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    // Mai mentre un campo di testo ha il focus (valore digitato): ha la sua modifica del testo.
    if (dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr)
    {
        return false;
    }

    const auto mods = key.getModifiers();
    if (mods.isAltDown() || !(mods.isCommandDown() || mods.isCtrlDown()))
    {
        return false;
    }

    const int code = key.getKeyCode();
    const int letter =
        code < 128
            ? static_cast<int>(juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(code)))
            : code;
    if (letter == 'Z')
    {
        mods.isShiftDown() ? redo() : undo();
        return true;
    }
    if (letter == 'Y' && !mods.isShiftDown())
    {
        redo();
        return true;
    }
    return false;
}

void DriftboxAudioProcessorEditor::updateHint(const juce::MouseEvent& e)
{
    juce::String hint;
    for (auto* c = e.eventComponent; c != nullptr && c != this; c = c->getParentComponent())
    {
        if (auto* tc = dynamic_cast<juce::TooltipClient*>(c))
        {
            hint = tc->getTooltip();
            if (hint.isNotEmpty())
            {
                break;
            }
        }
    }
    footer_.setHint(hint);
}

void DriftboxAudioProcessorEditor::mouseEnter(const juce::MouseEvent& e)
{
    updateHint(e);
}

void DriftboxAudioProcessorEditor::mouseExit(const juce::MouseEvent& e)
{
    if (e.eventComponent == this)
    {
        footer_.setHint({});
    }
}

void DriftboxAudioProcessorEditor::layoutContent()
{
    constexpr int w = kBaseWidth, h = kBaseHeight;
    topBar_.setBounds(0, 0, w, kHeaderH);
    footer_.setBounds(0, h - kFooterH, w, kFooterH);

    const juce::Rectangle<int> main(kPad, kHeaderH + kPad, w - 2 * kPad, h - kHeaderH - kFooterH - 2 * kPad);
    auto top = main.withHeight(kTopRowH);
    auto bottom = main.withTrimmedTop(kTopRowH + kGap);

    channelPanel_.setBounds(top.removeFromLeft(kSidePanelW));
    top.removeFromLeft(kGap);
    tolerancePanel_.setBounds(top);

    driftPanel_.setBounds(bottom.removeFromLeft(kSidePanelW));
    bottom.removeFromLeft(kGap);
    outputPanel_.setBounds(bottom.removeFromRight(kOutputPanelW));
    bottom.removeFromRight(kGap);
    advancedPanel_.setBounds(bottom);
}

void DriftboxAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / static_cast<float>(kBaseWidth);
    content_.setBounds(0, 0, kBaseWidth, kBaseHeight);
    content_.setTransform(juce::AffineTransform::scale(s));
    processor_.setUiSize(getWidth(), getHeight());
}

void DriftboxAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(col::bg0);
}

void DriftboxAudioProcessorEditor::timerCallback()
{
    refreshNow();
}

void DriftboxAudioProcessorEditor::refreshNow()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = juce::jlimit(0.001f, 0.25f, static_cast<float>((now - lastTickMs_) * 0.001));
    lastTickMs_ = now;

    auto& apvts = processor_.getAPVTS();

    // Curve e valori: si ricalcolano e ridisegnano solo se un ingresso è cambiato (flag di "sporco" interno).
    TolerancePanel::Inputs in;
    in.channelL = juce::jlimit(1, kNumConsoleChannels, juce::roundToInt(rawValue(apvts, pid::chanL)));
    in.channelR = juce::jlimit(1, kNumConsoleChannels, juce::roundToInt(rawValue(apvts, pid::chanR)));
    in.drift = rawValue(apvts, pid::drift);
    in.freqTol = rawValue(apvts, pid::freqTol);
    in.phaseTol = rawValue(apvts, pid::phaseTol);
    in.gainTol = rawValue(apvts, pid::gainTol);
    in.saturation = rawValue(apvts, pid::saturation);
    in.sampleRate = processor_.getSampleRate() > 0.0 ? processor_.getSampleRate() : 48000.0;
    in.bypassed = rawValue(apvts, pid::bypass) > 0.5f;
    tolerancePanel_.setInputs(in);

    auto& meters = processor_.getMeters();
    outputPanel_.meter().update(meters.outPeakDbL.load(std::memory_order_relaxed),
                                meters.outPeakDbR.load(std::memory_order_relaxed), dt);

    auto& um = processor_.getUndoManager();
    topBar_.setHistoryState(um.canUndo(), um.canRedo());

    footer_.setShowCpu(showCpu);
    footer_.setCpu(meters.cpuLoad.load(std::memory_order_relaxed));
}
} // namespace driftbox
