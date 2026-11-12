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
    CHECK(presets.presetName() == "Default");
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

// ============================== T8a: impostazioni e preset nell'editor
namespace
{
// Impostazioni e cartella dei preset isolate per il test: parte dai valori predefiniti, punta i preset utente
// a una cartella temporanea e ripristina tutto alla fine (le impostazioni sono condivise tra i test).
struct SettingsGuard
{
    explicit SettingsGuard(DriftboxAudioProcessor& p)
        : proc(p), dir(juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("DriftboxEditorTest-" +
                                         juce::String::toHexString(juce::Random().nextInt64())))
    {
        proc.getSettings().resetToDefaults();
        UserSettings s;
        s.presetFolder = dir.getFullPathName();
        proc.getSettings().set(s);
    }
    ~SettingsGuard()
    {
        proc.getSettings().resetToDefaults();
        dir.deleteRecursively();
    }
    DriftboxAudioProcessor& proc;
    juce::File dir;
};

// Come un clic sull'interruttore (triggerClick() è asincrono e nei test non gira il message loop).
void flip(ui::ToggleSwitch& sw)
{
    sw.setToggleState(!sw.getToggleState(), juce::dontSendNotification);
    sw.onClick();
}

juce::StringArray menuTexts(ui::OverlayMenu& m)
{
    juce::StringArray out;
    for (const auto& i : m.items())
    {
        out.add(i.text);
    }
    return out;
}
} // namespace

TEST_CASE("Settings modal opens from the gear and closes with DONE, the X and Escape",
          "[gui][Editor][settings]")
{
    DriftboxAudioProcessor processor;
    SettingsGuard guard(processor);
    DriftboxAudioProcessorEditor editor(processor);
    int hook = 0;
    editor.onSettings = [&] { ++hook; };
    CHECK_FALSE(editor.isSettingsOpen());
    CHECK(editor.getSettingsPanel() == nullptr);

    editor.getTopBar().settingsButton().onClick();
    CHECK(hook == 1);
    REQUIRE(editor.isSettingsOpen());
    auto* panel = editor.getSettingsPanel();
    REQUIRE(panel != nullptr);
    CHECK(panel->getParentComponent() == &editor.content());
    CHECK(panel->getBounds() == editor.content().getLocalBounds());
    CHECK(panel->versionText() == juce::String("Driftbox ") + info::kVersion);
    CHECK(panel->sourceLink().getTooltip() == "https://github.com/ivanerricis/driftbox");
    CHECK(panel->card().getWidth() == 460);
    CHECK_FALSE(panel->updatesSection().isVisible()); // T8b

    // Un secondo clic sull'ingranaggio non apre un'altra finestra.
    editor.getTopBar().settingsButton().onClick();
    CHECK(editor.getSettingsPanel() == panel);

    panel->doneButton().onClick();
    CHECK_FALSE(editor.isSettingsOpen());
    CHECK(editor.getSettingsPanel() == nullptr);

    editor.openSettings();
    REQUIRE(editor.isSettingsOpen());
    CHECK(editor.getSettingsPanel()->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK_FALSE(editor.isSettingsOpen());

    editor.openSettings();
    REQUIRE(editor.isSettingsOpen());
    editor.getSettingsPanel()->closeButton().onClick();
    CHECK_FALSE(editor.isSettingsOpen());

    // Il modale tiene per sé i tasti: Ctrl+Z non arriva all'editor.
    editor.openSettings();
    juce::ParameterAttachment att(*processor.getAPVTS().getParameter(pid::drift), [](float) {}, nullptr);
    att.setValueAsCompleteGesture(0.9f);
    CHECK(editor.getSettingsPanel()->keyPressed(juce::KeyPress('z', juce::ModifierKeys::ctrlModifier, 0)));
    CHECK(raw(processor, pid::drift) == Approx(0.9f));
    editor.closeModal();
}

TEST_CASE("Settings: zoom applies to the open editor and is the default for new editors",
          "[gui][Editor][settings]")
{
    DriftboxAudioProcessor processor;
    SettingsGuard guard(processor);
    DriftboxAudioProcessorEditor editor(processor);
    CHECK(editor.getZoom() == Approx(1.0f));
    editor.openSettings();
    auto* panel = editor.getSettingsPanel();
    REQUIRE(panel != nullptr);
    CHECK(panel->zoomSelector().getPercent() == 100);

    panel->zoomSelector().select(3); // 150 %
    CHECK(processor.getSettings().get().zoomPercent == 150);
    CHECK(editor.getZoom() == Approx(1.5f));
    CHECK(editor.getWidth() == 1080);
    CHECK(editor.getHeight() == 660);

    panel->zoomSelector().select(0); // 75 %
    CHECK(editor.getZoom() == Approx(0.75f));

    // Un altro interruttore non ridimensiona la finestra che l'utente ha cambiato a mano.
    editor.setZoom(1.25f);
    flip(panel->tooltipsSwitch());
    CHECK(editor.getZoom() == Approx(1.25f));
    editor.closeModal();

    // Un editor nuovo, in uno stato senza dimensione salvata, parte dallo zoom delle impostazioni.
    UserSettings s = processor.getSettings().get();
    s.zoomPercent = 200;
    processor.getSettings().set(s);
    {
        DriftboxAudioProcessor fresh;
        REQUIRE_FALSE(fresh.hasStoredUiSize());
        DriftboxAudioProcessorEditor e2(fresh);
        CHECK(e2.getZoom() == Approx(2.0f));
        CHECK(fresh.hasStoredUiSize());
    }
    // Con una dimensione salvata nello stato vince quella.
    {
        DriftboxAudioProcessor saved;
        saved.setUiSize(720, 440);
        DriftboxAudioProcessorEditor e3(saved);
        CHECK(e3.getZoom() == Approx(1.0f));
    }
}

TEST_CASE("Settings: every open editor follows a zoom change", "[gui][Editor][settings]")
{
    DriftboxAudioProcessor a, b;
    SettingsGuard guard(a);
    DriftboxAudioProcessorEditor ea(a), eb(b);
    UserSettings s = a.getSettings().get();
    s.zoomPercent = 125;
    a.getSettings().set(s);
    CHECK(ea.getZoom() == Approx(1.25f));
    CHECK(eb.getZoom() == Approx(1.25f));
}

TEST_CASE("Settings: Show CPU hides and shows the CPU text", "[gui][Editor][settings]")
{
    DriftboxAudioProcessor processor;
    SettingsGuard guard(processor);
    DriftboxAudioProcessorEditor editor(processor);
    const juce::String version = juce::String("v") + info::kVersion;
    editor.refreshNow();
    CHECK(editor.getFooter().rightText().startsWith("CPU "));

    editor.openSettings();
    auto* panel = editor.getSettingsPanel();
    REQUIRE(panel != nullptr);
    CHECK(panel->cpuSwitch().getToggleState());
    flip(panel->cpuSwitch());
    CHECK_FALSE(processor.getSettings().get().showCpu);
    CHECK_FALSE(editor.showCpu);
    editor.refreshNow();
    CHECK(editor.getFooter().rightText() == version);

    flip(panel->cpuSwitch());
    CHECK(processor.getSettings().get().showCpu);
    editor.refreshNow();
    CHECK(editor.getFooter().rightText().startsWith("CPU "));

    // Un editor nuovo parte dal valore salvato.
    UserSettings s = processor.getSettings().get();
    s.showCpu = false;
    processor.getSettings().set(s);
    {
        DriftboxAudioProcessor p2;
        DriftboxAudioProcessorEditor e2(p2);
        e2.refreshNow();
        CHECK_FALSE(e2.showCpu);
        CHECK(e2.getFooter().rightText() == version);
    }
    editor.closeModal();
}

TEST_CASE("Settings: tooltips switch, reset button and copy debug info", "[gui][Editor][settings]")
{
    DriftboxAudioProcessor processor;
    processor.setRateAndBufferSizeDetails(48000.0, 512);
    SettingsGuard guard(processor);
    DriftboxAudioProcessorEditor editor(processor);
    editor.openSettings();
    auto* panel = editor.getSettingsPanel();
    REQUIRE(panel != nullptr);

    flip(panel->tooltipsSwitch());
    CHECK_FALSE(processor.getSettings().get().tooltips);
    panel->zoomSelector().select(4);
    flip(panel->cpuSwitch());
    REQUIRE(processor.getSettings().get().zoomPercent == 200);

    panel->resetButton().onClick();
    const auto& s = processor.getSettings().get();
    CHECK(s.zoomPercent == 100);
    CHECK(s.tooltips);
    CHECK(s.showCpu);
    CHECK(s.presetFolder.isEmpty());
    // I controlli e l'editor seguono il ripristino.
    CHECK(panel->zoomSelector().getPercent() == 100);
    CHECK(panel->tooltipsSwitch().getToggleState());
    CHECK(panel->cpuSwitch().getToggleState());
    CHECK(editor.getZoom() == Approx(1.0f));
    CHECK(editor.showCpu);

    juce::String copied;
    panel->copyToClipboard = [&](const juce::String& t) { copied = t; };
    panel->copyDebugButton().onClick();
    CHECK(copied == processor.getDebugInfo());
    CHECK(copied.contains("Sample rate: 48000 Hz"));
    CHECK(copied.contains("Block size: 512"));
    CHECK(panel->copyDebugButton().getButtonText() == "Copied");
    editor.closeModal();
}

TEST_CASE("Preset bar shows the loaded preset name and the modified dot", "[gui][Editor][presets]")
{
    DriftboxAudioProcessor processor;
    SettingsGuard guard(processor);
    DriftboxAudioProcessorEditor editor(processor);
    auto& bar = editor.getTopBar().presetBar();
    CHECK(bar.presetName() == "Default"); // nessun preset caricato
    CHECK_FALSE(bar.isModified());

    REQUIRE(
        processor.getPresetManager().loadPreset(processor.getPresetManager().getFactoryPresets()[3]).wasOk());
    editor.refreshNow();
    CHECK(bar.presetName() == "Warm Console");
    CHECK_FALSE(bar.isModified());

    setValue(processor, pid::saturation, 0.9f);
    editor.refreshNow();
    CHECK(bar.presetName() == "Warm Console");
    CHECK(bar.isModified());

    setValue(processor, pid::saturation, 0.55f);
    editor.refreshNow();
    CHECK_FALSE(bar.isModified());

    // Dopo la riapertura del progetto la barra mostra il nome salvato nello stato.
    juce::MemoryBlock block;
    processor.getStateInformation(block);
    DriftboxAudioProcessor reopened;
    reopened.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    DriftboxAudioProcessorEditor editor2(reopened);
    CHECK(editor2.getTopBar().presetBar().presetName() == "Warm Console");
    CHECK_FALSE(editor2.getTopBar().presetBar().isModified());
}

TEST_CASE("Preset bar buttons load the previous and next preset", "[gui][Editor][presets]")
{
    DriftboxAudioProcessor processor;
    SettingsGuard guard(processor);
    DriftboxAudioProcessorEditor editor(processor);
    auto& bar = editor.getTopBar().presetBar();

    // Da "Default" (nessun preset caricato): avanti = il primo, indietro = l'ultimo.
    bar.nextButton().onClick();
    CHECK(bar.presetName() == "Init");
    bar.prevButton().onClick();
    CHECK(bar.presetName() == "Noisy Desk");
    bar.nextButton().onClick();
    bar.nextButton().onClick();
    CHECK(bar.presetName() == "Subtle Spread");
    CHECK(raw(processor, pid::drift) == Approx(0.62f).margin(1e-4));
    bar.nextButton().onClick();
    CHECK(bar.presetName() == "Wide Pair");
    bar.prevButton().onClick();
    bar.prevButton().onClick();
    CHECK(bar.presetName() == "Init");
    bar.prevButton().onClick(); // giro completo all'indietro
    CHECK(bar.presetName() == "Noisy Desk");
    bar.nextButton().onClick(); // e in avanti
    CHECK(bar.presetName() == "Init");
    CHECK(raw(processor, pid::drift) == Approx(0.0f).margin(1e-4));
}

TEST_CASE("Preset list menu groups Factory and User and loads on click", "[gui][Editor][presets]")
{
    DriftboxAudioProcessor processor;
    SettingsGuard guard(processor);
    REQUIRE(
        processor.getPresetManager().loadPreset(processor.getPresetManager().getFactoryPresets()[1]).wasOk());
    DriftboxAudioProcessorEditor editor(processor);

    editor.getTopBar().presetBar().nameButton().onClick();
    REQUIRE(editor.isMenuOpen());
    auto* menu = editor.getMenu();
    REQUIRE(menu != nullptr);
    CHECK(menu->getParentComponent() == &editor.content());
    CHECK(menuTexts(*menu) == juce::StringArray{"Factory", "Init", "Subtle Spread", "Wide Pair",
                                                "Warm Console", "Gentle Grit", "Noisy Desk", "User",
                                                "No user presets yet"});
    CHECK(menu->items()[2].checked); // il preset corrente ha la spunta
    CHECK_FALSE(menu->items()[1].checked);

    CHECK_FALSE(menu->activate(0)); // un titolo non si può scegliere
    CHECK_FALSE(menu->activate(8)); // né una nota
    CHECK(editor.isMenuOpen());
    CHECK(menu->activate(5)); // Gentle Grit
    CHECK_FALSE(editor.isMenuOpen());
    CHECK(processor.getPresetName() == "Gentle Grit");
    CHECK(raw(processor, pid::saturation) == Approx(0.85f).margin(1e-4));
    CHECK(editor.getMenu() == nullptr);

    // Con un preset utente la sezione User lo elenca.
    REQUIRE(processor.getPresetManager().saveAs("Mine").wasOk());
    editor.openPresetList();
    REQUIRE(editor.isMenuOpen());
    auto texts = menuTexts(*editor.getMenu());
    CHECK(texts[texts.size() - 2] == "User");
    CHECK(texts[texts.size() - 1] == "Mine");
    CHECK(editor.getMenu()->items().back().checked);
    editor.getMenu()->dismiss();
    CHECK_FALSE(editor.isMenuOpen());
}

TEST_CASE("Preset actions menu: Save and Delete are disabled for factory presets", "[gui][Editor][presets]")
{
    DriftboxAudioProcessor processor;
    SettingsGuard guard(processor);
    DriftboxAudioProcessorEditor editor(processor);

    REQUIRE(
        processor.getPresetManager().loadPreset(processor.getPresetManager().getFactoryPresets()[2]).wasOk());
    editor.getTopBar().presetBar().actionsButton().onClick();
    REQUIRE(editor.isMenuOpen());
    auto* menu = editor.getMenu();
    REQUIRE(menu->items().size() == 3);
    CHECK(menu->items()[0].text == "Save");
    CHECK(menu->items()[1].text.startsWith("Save As"));
    CHECK(menu->items()[2].text == "Delete");
    CHECK_FALSE(menu->items()[0].enabled);
    CHECK(menu->items()[1].enabled);
    CHECK_FALSE(menu->items()[2].enabled);
    CHECK_FALSE(menu->activate(0));
    CHECK_FALSE(menu->activate(2));
    menu->dismiss();

    // Un preset utente li abilita.
    REQUIRE(processor.getPresetManager().saveAs("Mine").wasOk());
    editor.openPresetActions();
    REQUIRE(editor.isMenuOpen());
    CHECK(editor.getMenu()->items()[0].enabled);
    CHECK(editor.getMenu()->items()[2].enabled);
    editor.getMenu()->dismiss();
}

TEST_CASE("Save As, Save and Delete from the menu", "[gui][Editor][presets]")
{
    DriftboxAudioProcessor processor;
    SettingsGuard guard(processor);
    DriftboxAudioProcessorEditor editor(processor);
    auto& pm = processor.getPresetManager();
    REQUIRE(pm.loadPreset(pm.getFactoryPresets()[4]).wasOk()); // Gentle Grit
    setValue(processor, pid::noise, 0.4f);
    editor.refreshNow();
    CHECK(editor.getTopBar().presetBar().isModified());

    // Save As...
    editor.openPresetActions();
    REQUIRE(editor.getMenu()->activate(1));
    auto* dialog = editor.getPresetDialog();
    REQUIRE(dialog != nullptr);
    CHECK(dialog->mode() == ui::PresetDialog::Mode::saveAs);
    CHECK(dialog->nameEditor().getText() == "Gentle Grit copy"); // parte dal nome corrente + " copy"

    // Un nome di un preset di fabbrica non si può usare.
    dialog->nameEditor().setText("Init", true);
    dialog->refreshStatus();
    CHECK_FALSE(dialog->confirmButton().isEnabled());
    CHECK(dialog->statusText().isNotEmpty());
    CHECK_FALSE(dialog->confirm());
    dialog->nameEditor().setText("   ", true);
    dialog->refreshStatus();
    CHECK_FALSE(dialog->confirmButton().isEnabled());

    dialog->nameEditor().setText("UI Preset", true);
    dialog->refreshStatus();
    CHECK(dialog->confirmButton().isEnabled());
    CHECK(dialog->statusText().isEmpty());
    REQUIRE(dialog->confirm());
    CHECK(guard.dir.getChildFile("UI Preset.dbp").existsAsFile());
    CHECK(processor.getPresetName() == "UI Preset");
    CHECK(editor.getTopBar().presetBar().presetName() == "UI Preset");
    CHECK_FALSE(editor.getTopBar().presetBar().isModified());
    CHECK(editor.getPresetDialog() == nullptr);

    // Save: sovrascrive il preset utente con i valori correnti.
    setValue(processor, pid::noise, 0.9f);
    editor.refreshNow();
    CHECK(editor.getTopBar().presetBar().isModified());
    editor.openPresetActions();
    REQUIRE(editor.getMenu()->activate(0));
    editor.refreshNow();
    CHECK_FALSE(editor.getTopBar().presetBar().isModified());
    pm.rescan();
    REQUIRE(pm.getUserPresets().size() == 1);
    CHECK(pm.getUserPresets()[0].values[5] == Approx(0.9f).margin(1e-4));

    // Save As con un nome esistente avvisa che lo sostituisce.
    editor.openPresetActions();
    REQUIRE(editor.getMenu()->activate(1));
    dialog = editor.getPresetDialog();
    REQUIRE(dialog != nullptr);
    CHECK(dialog->nameEditor().getText() == "UI Preset");
    CHECK(dialog->statusText().containsIgnoreCase("replaces"));
    CHECK(dialog->confirmButton().isEnabled());
    dialog->cancelButton().onClick();
    CHECK(editor.getPresetDialog() == nullptr);

    // Delete: chiede conferma, poi elimina il file e carica Init.
    editor.openPresetActions();
    REQUIRE(editor.getMenu()->activate(2));
    dialog = editor.getPresetDialog();
    REQUIRE(dialog != nullptr);
    CHECK(dialog->mode() == ui::PresetDialog::Mode::confirmDelete);
    CHECK(guard.dir.getChildFile("UI Preset.dbp").existsAsFile()); // ancora lì finché non si conferma
    REQUIRE(dialog->confirm());
    CHECK_FALSE(guard.dir.getChildFile("UI Preset.dbp").existsAsFile());
    CHECK(editor.getTopBar().presetBar().presetName() == "Init");
    CHECK(raw(processor, pid::drift) == Approx(0.0f).margin(1e-4));
}

TEST_CASE("Snapshots: settings modal and preset list", "[gui][Editor][snapshot]")
{
    DriftboxAudioProcessor processor;
    SettingsGuard guard(processor);
    setValue(processor, pid::chanL, 39.0f);
    setValue(processor, pid::chanR, 40.0f);
    REQUIRE(
        processor.getPresetManager().loadPreset(processor.getPresetManager().getFactoryPresets()[1]).wasOk());
    DriftboxAudioProcessorEditor editor(processor);
    editor.setZoom(1.0f);
    editor.refreshNow();

    editor.openSettings();
    REQUIRE(editor.isSettingsOpen());
    auto img = snapshot(editor, "settings-open");
    CHECK(img.getWidth() == ui::kBaseWidth);
    // Lo sfondo è scurito: il pixel in alto a sinistra non è più il colore della barra alta.
    CHECK(img.getPixelAt(1, 1) != ui::col::bg1);
    // La scheda è centrata e ha il colore dei pannelli.
    const auto card = editor.getSettingsPanel()->card();
    CHECK(img.getPixelAt(card.getX() + 4, card.getY() + 4) == ui::col::bg1);
    editor.closeModal();

    editor.getTopBar().presetBar().nameButton().onClick();
    REQUIRE(editor.isMenuOpen());
    img = snapshot(editor, "presets-list");
    CHECK(distinctColours(img) > 30);
    editor.getMenu()->dismiss();
}

TEST_CASE("Snapshot: fresh instance shows Default in the preset bar", "[gui][Editor][snapshot]")
{
    DriftboxAudioProcessor processor;
    SettingsGuard guard(processor);
    setValue(processor, pid::chanL, 39.0f);
    setValue(processor, pid::chanR, 40.0f);
    DriftboxAudioProcessorEditor editor(processor);
    editor.setZoom(1.0f);
    editor.refreshNow();
    CHECK(editor.getTopBar().presetBar().presetName() == "Default");
    CHECK_FALSE(editor.getTopBar().presetBar().isModified());
    const auto img = snapshot(editor, "preset-default");
    CHECK(distinctColours(img) > 30);
}
