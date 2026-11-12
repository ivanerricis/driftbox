// SPDX-License-Identifier: AGPL-3.0-or-later
// Editor senza finestra: creazione, zoom, selettori di canale, Randomize, pannello Advanced, curve.
#include "PluginEditor.h"
#include "PluginInfo.h"
#include "dsp/ToleranceResponse.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>

using namespace driftbox;
using Catch::Approx;

namespace
{
void setValue(DriftboxAudioProcessor& p, const char* id, float value)
{
    auto* param = p.getAPVTS().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

float raw(DriftboxAudioProcessor& p, const char* id)
{
    return p.getAPVTS().getRawParameterValue(id)->load();
}

// Numero di colori distinti in una griglia rada dell'immagine.
int distinctColours(const juce::Image& img)
{
    std::set<juce::uint32> colours;
    for (int y = 0; y < img.getHeight(); y += 3)
    {
        for (int x = 0; x < img.getWidth(); x += 3)
        {
            colours.insert(img.getPixelAt(x, y).getARGB());
        }
    }
    return static_cast<int>(colours.size());
}

// Disegna l'editor e, se DRIFTBOX_SNAPSHOT_DIR è impostata, salva il PNG (usato per il confronto con il
// mockup).
juce::Image snapshot(DriftboxAudioProcessorEditor& e, const juce::String& name)
{
    auto img = e.createComponentSnapshot(e.getLocalBounds(), false, 1.0f);
    const auto dir = juce::SystemStats::getEnvironmentVariable("DRIFTBOX_SNAPSHOT_DIR", {});
    if (dir.isNotEmpty())
    {
        juce::File out = juce::File(dir).getChildFile(name + ".png");
        out.getParentDirectory().createDirectory();
        out.deleteFile();
        juce::FileOutputStream stream(out);
        if (stream.openedOk())
        {
            juce::PNGImageFormat().writeImageToStream(img, stream);
        }
    }
    return img;
}
} // namespace

TEST_CASE("Editor create and destroy", "[gui][Editor]")
{
    DriftboxAudioProcessor processor;
    {
        auto editor = std::make_unique<DriftboxAudioProcessorEditor>(processor);
        CHECK(editor->getWidth() == ui::kBaseWidth);
        CHECK(editor->getHeight() == ui::kBaseHeight);
        CHECK(editor->getZoom() == Approx(1.0f));
        editor->refreshNow();
    }
    // Un secondo editor si può creare dopo la distruzione del primo.
    auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
    CHECK(editor->getWidth() == ui::kBaseWidth);
    CHECK(ui::fonts::embeddedFontsLoaded());
}

TEST_CASE("Editor zoom keeps the aspect ratio and is clamped to 75 - 200 %", "[gui][Editor]")
{
    DriftboxAudioProcessor processor;
    DriftboxAudioProcessorEditor editor(processor);
    for (const float zoom : {0.75f, 1.0f, 1.25f, 1.5f, 2.0f})
    {
        editor.setZoom(zoom);
        INFO("zoom " << zoom);
        CHECK(editor.getWidth() == juce::roundToInt(ui::kBaseWidth * zoom));
        CHECK(editor.getHeight() == juce::roundToInt(ui::kBaseHeight * zoom));
        CHECK(editor.content().getTransform().mat00 == Approx(zoom).margin(0.002));
        CHECK(editor.getZoom() == Approx(zoom).margin(0.002));
        // La dimensione viene salvata nello stato.
        CHECK(processor.getUiSize().x == editor.getWidth());
        CHECK(processor.getUiSize().y == editor.getHeight());
        editor.refreshNow();
        const auto img = snapshot(editor, "zoom" + juce::String(juce::roundToInt(zoom * 100)));
        CHECK(img.getWidth() == editor.getWidth());
        CHECK(distinctColours(img) > 20);
    }
    editor.setZoom(5.0f);
    CHECK(editor.getWidth() == 2 * ui::kBaseWidth);
    editor.setZoom(0.1f);
    CHECK(editor.getWidth() == juce::roundToInt(ui::kBaseWidth * 0.75f));
}

TEST_CASE("Editor restores the saved size", "[gui][Editor]")
{
    DriftboxAudioProcessor processor;
    processor.setUiSize(1080, 660);
    {
        DriftboxAudioProcessorEditor editor(processor);
        CHECK(editor.getWidth() == 1080);
        CHECK(editor.getHeight() == 660);
    }
    processor.setUiSize(800, 800); // proporzioni sbagliate: dimensione di base
    DriftboxAudioProcessorEditor editor(processor);
    CHECK(editor.getWidth() == ui::kBaseWidth);
    CHECK(editor.getHeight() == ui::kBaseHeight);
}

TEST_CASE("Channel selector wraps 64 -> 1 and each step is one undo step", "[gui][Editor]")
{
    DriftboxAudioProcessor processor;
    DriftboxAudioProcessorEditor editor(processor);
    auto& um = processor.getUndoManager();

    setValue(processor, pid::chanL, 64.0f);
    setValue(processor, pid::chanR, 1.0f);
    processor.clearUndoHistory();
    CHECK(editor.getChannelPanel().selector(0).value() == 64);
    CHECK(editor.getChannelPanel().selector(1).value() == 1);

    auto& left = editor.getChannelPanel().selector(0);
    left.step(1);
    CHECK(raw(processor, pid::chanL) == Approx(1.0f));
    CHECK(left.value() == 1);
    CHECK(um.getUndoDescriptions().size() == 1);

    left.step(-1);
    CHECK(raw(processor, pid::chanL) == Approx(64.0f));
    CHECK(um.getUndoDescriptions().size() == 2);

    // Annullando si torna indietro un passo alla volta.
    CHECK(editor.undo());
    CHECK(raw(processor, pid::chanL) == Approx(1.0f));
    CHECK(editor.undo());
    CHECK(raw(processor, pid::chanL) == Approx(64.0f));
    CHECK_FALSE(editor.undo());

    // Il lato R non è toccato dal lato L e fa il giro nell'altro verso.
    auto& right = editor.getChannelPanel().selector(1);
    right.step(-1);
    CHECK(raw(processor, pid::chanR) == Approx(64.0f));
    CHECK(raw(processor, pid::chanL) == Approx(64.0f));

    // Valori digitati: limitati a 1..64, testo non numerico ignorato.
    left.commitText("12");
    CHECK(raw(processor, pid::chanL) == Approx(12.0f));
    left.commitText("99");
    CHECK(raw(processor, pid::chanL) == Approx(64.0f));
    left.commitText("0");
    CHECK(raw(processor, pid::chanL) == Approx(1.0f));
    left.commitText("abc");
    CHECK(raw(processor, pid::chanL) == Approx(1.0f));
    CHECK(ui::ChannelSelector::format(7) == "07");
}

TEST_CASE("Channel selector follows parameter changes from the host", "[gui][Editor]")
{
    DriftboxAudioProcessor processor;
    DriftboxAudioProcessorEditor editor(processor);
    setValue(processor, pid::chanR, 33.0f);
    CHECK(editor.getChannelPanel().selector(1).value() == 33);
    CHECK_FALSE(processor.getUndoManager().canUndo()); // l'automazione non entra nella cronologia
}

TEST_CASE("Randomize from the button is one undo step", "[gui][Editor]")
{
    DriftboxAudioProcessor processor;
    DriftboxAudioProcessorEditor editor(processor);
    auto& um = processor.getUndoManager();
    setValue(processor, pid::chanL, 5.0f);
    setValue(processor, pid::chanR, 6.0f);
    processor.clearUndoHistory();

    editor.getChannelPanel().randomizeButton().onClick();
    const int l = juce::roundToInt(raw(processor, pid::chanL));
    const int r = juce::roundToInt(raw(processor, pid::chanR));
    CHECK((l != 5 || r != 6));
    CHECK(r == l % kNumConsoleChannels + 1);
    CHECK(um.getUndoDescriptions().size() == 1);
    CHECK(editor.getChannelPanel().selector(0).value() == l);

    CHECK(editor.undo());
    CHECK(raw(processor, pid::chanL) == Approx(5.0f));
    CHECK(raw(processor, pid::chanR) == Approx(6.0f));
    CHECK_FALSE(um.canUndo());
    CHECK(editor.redo());
    CHECK(juce::roundToInt(raw(processor, pid::chanL)) == l);
}

TEST_CASE("Advanced panel state is saved in the processor and restored by a new editor", "[gui][Editor]")
{
    DriftboxAudioProcessor processor;
    processor.clearUndoHistory();
    {
        DriftboxAudioProcessorEditor editor(processor);
        CHECK_FALSE(editor.getAdvancedPanel().isOpen());
        editor.getAdvancedPanel().toggleButton().onClick();
        CHECK(processor.getAdvancedOpen());
        CHECK_FALSE(processor.getUndoManager().canUndo()); // aprire il pannello non è un passo di undo
    }
    {
        DriftboxAudioProcessorEditor editor(processor);
        auto& adv = editor.getAdvancedPanel();
        CHECK(adv.isOpen());
        CHECK(adv.toggleButton().getButtonText() == "HIDE");
        CHECK(adv.knob(0).isVisible());
        adv.toggleButton().onClick();
        CHECK_FALSE(processor.getAdvancedOpen());
    }
    DriftboxAudioProcessorEditor editor(processor);
    CHECK_FALSE(editor.getAdvancedPanel().isOpen());
}

TEST_CASE("Advanced panel shows and hides its knobs", "[gui][Editor]")
{
    DriftboxAudioProcessor processor;
    DriftboxAudioProcessorEditor editor(processor);
    auto& adv = editor.getAdvancedPanel();

    CHECK_FALSE(adv.isOpen());
    CHECK(adv.toggleButton().getButtonText() == "SHOW");
    for (int i = 0; i < ui::AdvancedPanel::kNumKnobs; ++i)
    {
        CHECK_FALSE(adv.knob(i).isVisible());
    }

    adv.toggleButton().onClick();
    CHECK(adv.isOpen());
    CHECK(adv.toggleButton().getButtonText() == "HIDE");
    for (int i = 0; i < ui::AdvancedPanel::kNumKnobs; ++i)
    {
        CHECK(adv.knob(i).isVisible());
        CHECK(adv.knob(i).getWidth() > 0);
    }
    snapshot(editor, "advanced-open");

    adv.toggleButton().onClick();
    CHECK_FALSE(adv.isOpen());
    CHECK_FALSE(adv.knob(0).isVisible());
    CHECK_FALSE(processor.getAdvancedOpen());

    // Digitare una percentuale nella manopola la applica come un gesto (un passo di undo).
    adv.setOpen(true);
    processor.clearUndoHistory();
    adv.knob(3).commitText("85");
    CHECK(raw(processor, pid::saturation) == Approx(0.85f));
    CHECK(adv.knob(3).valueText() == "85 %");
    CHECK(processor.getUndoManager().getUndoDescriptions().size() == 1);
}

TEST_CASE("Knobs are attached to their parameters", "[gui][Editor]")
{
    DriftboxAudioProcessor processor;
    DriftboxAudioProcessorEditor editor(processor);
    setValue(processor, pid::drift, 0.62f);
    CHECK(editor.getDriftPanel().knob().valueText() == "62 %");
    setValue(processor, pid::outGain, -2.5f);
    CHECK(editor.getOutputPanel().knob().valueText() == "-2.5 dB");

    // Undo dal pulsante Undo.
    processor.clearUndoHistory();
    editor.getDriftPanel().knob().commitText("10");
    CHECK(raw(processor, pid::drift) == Approx(0.1f));
    editor.refreshNow();
    CHECK(editor.getTopBar().undoButton().isEnabled());
    CHECK_FALSE(editor.getTopBar().redoButton().isEnabled());
    editor.getTopBar().undoButton().onClick();
    CHECK(raw(processor, pid::drift) == Approx(0.62f));
    CHECK(editor.getTopBar().redoButton().isEnabled());
    editor.getTopBar().redoButton().onClick();
    CHECK(raw(processor, pid::drift) == Approx(0.1f));
}

TEST_CASE("Bypass button and badge follow the parameter", "[gui][Editor]")
{
    DriftboxAudioProcessor processor;
    DriftboxAudioProcessorEditor editor(processor);
    CHECK_FALSE(editor.getTopBar().bypassButton().getToggleState());
    CHECK_FALSE(editor.getTolerancePanel().inputs().bypassed);
    setValue(processor, pid::bypass, 1.0f);
    editor.refreshNow();
    CHECK(editor.getTopBar().bypassButton().getToggleState());
    CHECK(editor.getTolerancePanel().inputs().bypassed);
    snapshot(editor, "bypassed");
}

TEST_CASE("Top bar hooks: settings gear and preset bar placeholders", "[gui][Editor]")
{
    DriftboxAudioProcessor processor;
    DriftboxAudioProcessorEditor editor(processor);
    int settings = 0, next = 0;
    editor.onSettings = [&] { ++settings; };
    editor.getTopBar().settingsButton().onClick();
    CHECK(settings == 1);

    auto& presets = editor.getTopBar().presetBar();
    CHECK(presets.presetName() == "Init");
    presets.nextButton().onClick(); // senza gancio non fa nulla
    presets.onNext = [&] { ++next; };
    presets.nextButton().onClick();
    CHECK(next == 1);
    presets.setPresetName("Warm Console");
    CHECK(presets.presetName() == "Warm Console");
}

TEST_CASE("Footer shows CPU and version, and the CPU flag hides the CPU", "[gui][Editor]")
{
    DriftboxAudioProcessor processor;
    DriftboxAudioProcessorEditor editor(processor);
    const juce::String version = juce::String("v") + info::kVersion;
    CHECK(editor.showCpu);
    editor.refreshNow();
    CHECK(editor.getFooter().rightText().startsWith("CPU "));
    CHECK(editor.getFooter().rightText().endsWith(version));
    CHECK(editor.getFooter().rightText().contains(" %"));

    editor.showCpu = false;
    editor.refreshNow();
    CHECK(editor.getFooter().rightText() == version);

    CHECK(editor.getFooter().hint() == ui::Footer::defaultHint());
    editor.getFooter().setHint("Hello");
    CHECK(editor.getFooter().hint() == "Hello");
    editor.getFooter().setHint({});
    CHECK(editor.getFooter().hint() == ui::Footer::defaultHint());
}

TEST_CASE("Tolerance graph: flat at Drift 0, within the limits at 100 %", "[gui][Editor]")
{
    DriftboxAudioProcessor processor;
    DriftboxAudioProcessorEditor editor(processor);
    auto& graph = editor.getTolerancePanel();

    setValue(processor, pid::drift, 0.0f);
    for (const int ch : {1, 17, 39, 64})
    {
        setValue(processor, pid::chanL, static_cast<float>(ch));
        setValue(processor, pid::chanR, static_cast<float>(ch % 64 + 1));
        editor.refreshNow();
        for (int s = 0; s < 2; ++s)
        {
            for (int i = 0; i < ui::TolerancePanel::kPoints; ++i)
            {
                REQUIRE(graph.curveDb(s, i) == 0.0f);
            }
        }
        CHECK(graph.gainText(0) == "gain +0.00 dB");
    }
    snapshot(editor, "drift0");

    setValue(processor, pid::drift, 1.0f);
    for (const char* id : {pid::freqTol, pid::gainTol, pid::phaseTol, pid::saturation})
    {
        setValue(processor, id, 1.0f);
    }
    float worst = 0.0f;
    for (int ch = 1; ch <= kNumConsoleChannels; ++ch)
    {
        setValue(processor, pid::chanL, static_cast<float>(ch));
        setValue(processor, pid::chanR, static_cast<float>(ch % 64 + 1));
        editor.refreshNow();
        for (int s = 0; s < 2; ++s)
        {
            for (int i = 0; i < ui::TolerancePanel::kPoints; ++i)
            {
                worst = std::max(worst, std::abs(graph.curveDb(s, i)));
            }
        }
    }
    CHECK(worst > 0.1f);
    CHECK(worst <= kMaxDeviationDb + 1e-3f);

    // Stessi ingressi: nessun ricalcolo né ridisegno.
    const auto in = graph.inputs();
    CHECK_FALSE(graph.setInputs(in));
    auto changed = in;
    changed.drift = 0.3f;
    CHECK(graph.setInputs(changed));

    CHECK(ui::TolerancePanel::frequencyAt(0) == Approx(20.0f));
    CHECK(ui::TolerancePanel::frequencyAt(ui::TolerancePanel::kPoints - 1) == Approx(20000.0f));
}

TEST_CASE("Editor snapshot at 100 percent", "[gui][Editor]")
{
    DriftboxAudioProcessor processor;
    setValue(processor, pid::chanL, 39.0f);
    setValue(processor, pid::chanR, 40.0f);
    setValue(processor, pid::drift, 0.62f);
    DriftboxAudioProcessorEditor editor(processor);
    editor.setZoom(1.0f);
    editor.refreshNow();
    const auto img = snapshot(editor, "t7-editor");
    CHECK(img.getWidth() == ui::kBaseWidth);
    CHECK(img.getHeight() == ui::kBaseHeight);
    CHECK(distinctColours(img) > 30);
    // In alto a sinistra c'è la barra alta; in basso a sinistra il piè di pagina.
    CHECK(img.getPixelAt(1, 1) == ui::col::bg1);
    CHECK(img.getPixelAt(1, ui::kBaseHeight - 2) == ui::col::bg1);
}
