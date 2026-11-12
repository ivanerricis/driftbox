// SPDX-License-Identifier: AGPL-3.0-or-later
// Preset di fabbrica e utente: caricamento, salvataggio, file non validi, navigazione, "modificato", undo.
#include "PluginProcessor.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace driftbox;
using Catch::Approx;

namespace
{
float raw(DriftboxAudioProcessor& p, const char* id)
{
    return p.getAPVTS().getRawParameterValue(id)->load();
}

void setParam(DriftboxAudioProcessor& p, const char* id, float value)
{
    auto* param = p.getAPVTS().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

// Cartella utente temporanea, rimossa alla fine del test.
struct TempFolder
{
    TempFolder()
        : dir(juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("DriftboxPresetTest-" +
                                juce::String::toHexString(juce::Random().nextInt64())))
    {
    }
    ~TempFolder() { dir.deleteRecursively(); }
    juce::File dir;
};

// Processor con la cartella dei preset utente puntata su una cartella temporanea.
struct Fixture
{
    Fixture() { p.getPresetManager().setUserFolder(tmp.dir); }
    TempFolder tmp;
    DriftboxAudioProcessor p;
    PresetManager& pm = p.getPresetManager();
};

const PresetEntry* factoryNamed(PresetManager& pm, const juce::String& name)
{
    for (const auto& e : pm.getFactoryPresets())
    {
        if (e.name == name)
        {
            return &e;
        }
    }
    return nullptr;
}

void writeFile(const juce::File& f, const juce::String& text)
{
    f.getParentDirectory().createDirectory();
    REQUIRE(f.replaceWithText(text));
}

juce::String presetXml(const juce::String& attrs, const juce::String& body)
{
    return "<DRIFTBOX_PRESET " + attrs + ">" + body + "</DRIFTBOX_PRESET>";
}
} // namespace

TEST_CASE("Preset defaults match the parameter defaults", "[presets]")
{
    DriftboxAudioProcessor p;
    for (int i = 0; i < kNumPresetParams; ++i)
    {
        auto* param = p.getAPVTS().getParameter(kPresetParamIds[i]);
        REQUIRE(param != nullptr);
        CHECK(param->convertFrom0to1(param->getDefaultValue()) ==
              Approx(kPresetDefaults[static_cast<size_t>(i)]).margin(1e-5));
    }
}

TEST_CASE("Factory presets: six, in menu order, Init is transparent", "[presets]")
{
    Fixture f;
    const auto& list = f.pm.getFactoryPresets();
    REQUIRE(list.size() == 6);
    const char* expected[] = {"Init",         "Subtle Spread", "Wide Pair",
                              "Warm Console", "Gentle Grit",   "Noisy Desk"};
    for (size_t i = 0; i < 6; ++i)
    {
        CHECK(list[i].name == expected[i]);
        CHECK(list[i].factory);
    }
    // Init: Drift 0 %, tutto il resto predefinito.
    PresetValues init = kPresetDefaults;
    init[0] = 0.0f;
    CHECK(list[0].values == init);
    // Valori sensati: tutti nei limiti dei parametri.
    for (const auto& e : list)
    {
        for (size_t i = 0; i < kNumPresetParams - 1; ++i)
        {
            CHECK(e.values[i] >= 0.0f);
            CHECK(e.values[i] <= 1.0f);
        }
        CHECK(e.values[kNumPresetParams - 1] >= -24.0f);
        CHECK(e.values[kNumPresetParams - 1] <= 24.0f);
    }
}

TEST_CASE("Every factory preset loads and sets only the seven preset parameters", "[presets]")
{
    Fixture f;
    // Valori diversi dal predefinito per i parametri che i preset NON devono toccare, più una proprietà
    // dello stato e un segno nell'albero (che una sostituzione dello stato farebbe sparire).
    setParam(f.p, pid::chanL, 17.0f);
    setParam(f.p, pid::chanR, 40.0f);
    setParam(f.p, pid::bypass, 1.0f);
    f.p.setAdvancedOpen(true);
    f.p.setUiSize(900, 550);
    auto state = f.p.getAPVTS().state;
    state.setProperty("marker", 42, nullptr);

    for (const auto& entry : f.pm.getFactoryPresets())
    {
        // Parte da valori lontani da quelli del preset.
        for (int i = 0; i < kNumPresetParams; ++i)
        {
            setParam(f.p, kPresetParamIds[i], i == kNumPresetParams - 1 ? 7.0f : 0.93f);
        }
        REQUIRE(f.pm.loadPreset(entry).wasOk());

        for (int i = 0; i < kNumPresetParams; ++i)
        {
            CHECK(raw(f.p, kPresetParamIds[i]) == Approx(entry.values[static_cast<size_t>(i)]).margin(1e-4));
        }
        CHECK(raw(f.p, pid::chanL) == Approx(17.0f));
        CHECK(raw(f.p, pid::chanR) == Approx(40.0f));
        CHECK(raw(f.p, pid::bypass) > 0.5f);
        CHECK(f.p.getAdvancedOpen());
        CHECK(f.p.getUiSize() == juce::Point<int>(900, 550));
        CHECK(f.p.getPresetName() == entry.name);
        CHECK(f.pm.getCurrentName() == entry.name);
        CHECK_FALSE(f.pm.isModified());
        CHECK(f.pm.isCurrentFactory());
    }
    // L'albero dello stato è lo stesso oggetto e conserva le proprietà non parametriche.
    CHECK(state == f.p.getAPVTS().state);
    CHECK(static_cast<int>(f.p.getAPVTS().state.getProperty("marker")) == 42);
}

TEST_CASE("Preset file names are sanitised", "[presets]")
{
    CHECK(PresetManager::sanitiseName("  My   preset  ") == "My preset");
    CHECK(PresetManager::sanitiseName("a/b\\c:d*e?f\"g<h>i|j") == "abcdefghij");
    CHECK(PresetManager::sanitiseName("name...") == "name");
    CHECK(PresetManager::sanitiseName("").isEmpty());
    CHECK(PresetManager::sanitiseName("///").isEmpty());
    CHECK(PresetManager::sanitiseName("CON").isEmpty());
    CHECK(PresetManager::sanitiseName("lpt1").isEmpty());
    CHECK(PresetManager::sanitiseName(juce::String::repeatedString("x", 200)).length() ==
          PresetManager::kMaxNameLength);
}

TEST_CASE("Default user folder is Driftbox/Presets under the application data folder", "[presets]")
{
    const auto folder = PresetManager::getDefaultUserFolder();
    CHECK(folder.getFileName() == "Presets");
    CHECK(folder.getParentDirectory().getFileName() == "Driftbox");
}

TEST_CASE("Save as then load round trips and the file holds only the preset parameters", "[presets]")
{
    Fixture f;
    setParam(f.p, pid::drift, 0.33f);
    setParam(f.p, pid::freqTol, 0.11f);
    setParam(f.p, pid::phaseTol, 0.22f);
    setParam(f.p, pid::gainTol, 0.44f);
    setParam(f.p, pid::saturation, 0.66f);
    setParam(f.p, pid::noise, 0.77f);
    setParam(f.p, pid::outGain, -6.5f);
    setParam(f.p, pid::chanL, 5.0f);
    setParam(f.p, pid::bypass, 1.0f);

    REQUIRE(f.pm.saveAs("My Preset").wasOk());
    const auto file = f.tmp.dir.getChildFile("My Preset.dbp");
    REQUIRE(file.existsAsFile());
    CHECK(f.p.getPresetName() == "My Preset");
    CHECK(f.pm.isCurrentUser());
    CHECK_FALSE(f.pm.isCurrentFactory());
    CHECK_FALSE(f.pm.isModified());
    REQUIRE(f.pm.getUserPresets().size() == 1);

    // Contenuto del file: versione, sette parametri, nessun canale né bypass.
    auto xml = juce::XmlDocument::parse(file);
    REQUIRE(xml != nullptr);
    CHECK(xml->hasTagName("DRIFTBOX_PRESET"));
    CHECK(xml->getIntAttribute("version") == PresetManager::kPresetVersion);
    std::set<juce::String> ids;
    for (auto* e : xml->getChildWithTagNameIterator("PARAM"))
    {
        ids.insert(e->getStringAttribute("id"));
    }
    CHECK(ids == std::set<juce::String>{"drift", "freqTol", "phaseTol", "gainTol", "saturation", "noise",
                                        "outGain"});

    // Cambia tutto e ricarica dal file.
    for (int i = 0; i < kNumPresetParams; ++i)
    {
        setParam(f.p, kPresetParamIds[i], i == kNumPresetParams - 1 ? 0.0f : 0.9f);
    }
    CHECK(f.pm.isModified());
    f.pm.rescan();
    REQUIRE(f.pm.loadPreset(f.pm.getUserPresets().front()).wasOk());
    CHECK(raw(f.p, pid::drift) == Approx(0.33f).margin(1e-4));
    CHECK(raw(f.p, pid::freqTol) == Approx(0.11f).margin(1e-4));
    CHECK(raw(f.p, pid::phaseTol) == Approx(0.22f).margin(1e-4));
    CHECK(raw(f.p, pid::gainTol) == Approx(0.44f).margin(1e-4));
    CHECK(raw(f.p, pid::saturation) == Approx(0.66f).margin(1e-4));
    CHECK(raw(f.p, pid::noise) == Approx(0.77f).margin(1e-4));
    CHECK(raw(f.p, pid::outGain) == Approx(-6.5f).margin(0.01));
    CHECK(raw(f.p, pid::chanL) == Approx(5.0f)); // mai toccato
    CHECK(raw(f.p, pid::bypass) > 0.5f);
    CHECK_FALSE(f.pm.isModified());
}

TEST_CASE("Save as refuses factory names, bad names and existing names without overwrite", "[presets]")
{
    Fixture f;
    CHECK(f.pm.saveAs("Init").failed());
    CHECK(f.pm.saveAs("warm console").failed()); // senza distinzione tra maiuscole e minuscole
    CHECK(f.pm.saveAs("???").failed());
    CHECK(f.pm.saveAs("").failed());
    CHECK_FALSE(f.tmp.dir.exists());

    REQUIRE(f.pm.saveAs("Mine").wasOk());
    CHECK(f.pm.saveAs("mine").failed());
    setParam(f.p, pid::drift, 0.91f);
    CHECK(f.pm.saveAs("Mine", true).wasOk());
    f.pm.rescan();
    REQUIRE(f.pm.getUserPresets().size() == 1);
    CHECK(f.pm.getUserPresets().front().values[0] == Approx(0.91f).margin(1e-4));
}

TEST_CASE("Save overwrites the current user preset and is refused for factory presets", "[presets]")
{
    Fixture f;
    REQUIRE(f.pm.loadPreset(*factoryNamed(f.pm, "Wide Pair")).wasOk());
    CHECK(f.pm.save().failed());
    CHECK(f.pm.deleteCurrent().failed());

    REQUIRE(f.pm.saveAs("Mine").wasOk());
    setParam(f.p, pid::noise, 0.123f);
    CHECK(f.pm.isModified());
    REQUIRE(f.pm.save().wasOk());
    CHECK_FALSE(f.pm.isModified());
    f.pm.rescan();
    REQUIRE(f.pm.getUserPresets().size() == 1);
    CHECK(f.pm.getUserPresets().front().values[5] == Approx(0.123f).margin(1e-4));
}

TEST_CASE("Corrupt, unreadable and future-version files are skipped", "[presets]")
{
    Fixture f;
    const auto& dir = f.tmp.dir;
    writeFile(dir.getChildFile("good.dbp"),
              presetXml("version=\"1\"", "<PARAM id=\"drift\" value=\"0.25\"/>"));
    writeFile(dir.getChildFile("garbage.dbp"), "this is not xml <<<");
    writeFile(dir.getChildFile("empty.dbp"), "");
    writeFile(dir.getChildFile("wrongtag.dbp"), "<OTHER version=\"1\"/>");
    writeFile(dir.getChildFile("future.dbp"),
              presetXml("version=\"99\"", "<PARAM id=\"drift\" value=\"0.9\"/>"));
    writeFile(dir.getChildFile("zero.dbp"), presetXml("version=\"0\"", ""));
    writeFile(dir.getChildFile("nan.dbp"), presetXml("version=\"1\"", "<PARAM id=\"drift\" value=\"abc\"/>"));
    writeFile(dir.getChildFile("Init.dbp"), presetXml("version=\"1\"", "")); // nome di un preset di fabbrica
    writeFile(dir.getChildFile("notes.txt"), "ignored");

    f.pm.rescan();
    REQUIRE(f.pm.getUserPresets().size() == 1);
    CHECK(f.pm.getUserPresets().front().name == "good");
    CHECK(f.pm.getUserPresets().front().values[0] == Approx(0.25f));
    CHECK(f.pm.getSkippedFiles().size() == 7);
    // Il resto funziona normalmente.
    CHECK(f.pm.loadNext().wasOk());
}

TEST_CASE("Preset files: missing parameters default, out of range is clamped, other ids are ignored",
          "[presets]")
{
    Fixture f;
    writeFile(f.tmp.dir.getChildFile("odd.dbp"),
              presetXml("version=\"1\"",
                        "<PARAM id=\"drift\" value=\"7\"/><PARAM id=\"outGain\" value=\"-99\"/>"
                        "<PARAM id=\"chanL\" value=\"33\"/><PARAM id=\"bypass\" value=\"1\"/>"
                        "<PARAM id=\"unknown\" value=\"5\"/>"));
    setParam(f.p, pid::chanL, 9.0f);
    setParam(f.p, pid::bypass, 0.0f);
    f.pm.rescan();
    REQUIRE(f.pm.getUserPresets().size() == 1);
    const auto e = f.pm.getUserPresets().front();
    CHECK(e.values[0] == 1.0f);                // drift limitato
    CHECK(e.values[1] == kPresetDefaults[1]);  // mancante = predefinito
    CHECK(e.values[6] == kPresetOutGainMinDb); // outGain limitato
    REQUIRE(f.pm.loadPreset(e).wasOk());
    CHECK(raw(f.p, pid::chanL) == Approx(9.0f)); // gli id chanL e bypass nel file non contano
    CHECK(raw(f.p, pid::bypass) < 0.5f);
}

TEST_CASE("A version-less preset file is read as version 1", "[presets]")
{
    PresetValues v;
    juce::String name;
    const auto xml =
        juce::XmlDocument::parse("<DRIFTBOX_PRESET><PARAM id=\"noise\" value=\"0.5\"/></DRIFTBOX_PRESET>");
    REQUIRE(xml != nullptr);
    REQUIRE(PresetManager::parsePreset(*xml, v, name).wasOk());
    CHECK(v[5] == Approx(0.5f));
}

TEST_CASE("Delete removes the file and loads Init", "[presets]")
{
    Fixture f;
    setParam(f.p, pid::drift, 0.8f);
    REQUIRE(f.pm.saveAs("Gone").wasOk());
    const auto file = f.tmp.dir.getChildFile("Gone.dbp");
    REQUIRE(file.existsAsFile());

    REQUIRE(f.pm.deleteCurrent().wasOk());
    CHECK_FALSE(file.existsAsFile());
    CHECK(f.pm.getUserPresets().empty());
    CHECK(f.p.getPresetName() == "Init");
    CHECK(raw(f.p, pid::drift) == Approx(0.0f).margin(1e-4));
}

TEST_CASE("Previous and next wrap across the factory then the user list", "[presets]")
{
    Fixture f;
    REQUIRE(f.pm.saveAs("Alpha").wasOk());
    REQUIRE(f.pm.saveAs("Beta").wasOk());
    // Ordine: 6 di fabbrica, poi Alpha e Beta.
    REQUIRE(f.pm.loadPreset(*factoryNamed(f.pm, "Init")).wasOk());

    CHECK(f.pm.loadPrevious().wasOk()); // Init -> ultimo (Beta)
    CHECK(f.pm.getCurrentName() == "Beta");
    CHECK(f.pm.loadNext().wasOk()); // Beta -> primo (Init)
    CHECK(f.pm.getCurrentName() == "Init");

    const char* order[] = {"Subtle Spread", "Wide Pair", "Warm Console", "Gentle Grit",
                           "Noisy Desk",    "Alpha",     "Beta",         "Init"};
    for (const char* name : order)
    {
        REQUIRE(f.pm.loadNext().wasOk());
        CHECK(f.pm.getCurrentName() == name);
    }
    REQUIRE(f.pm.loadPrevious().wasOk());
    CHECK(f.pm.getCurrentName() == "Beta");
    REQUIRE(f.pm.loadPrevious().wasOk());
    CHECK(f.pm.getCurrentName() == "Alpha");
    REQUIRE(f.pm.loadPrevious().wasOk());
    CHECK(f.pm.getCurrentName() == "Noisy Desk");
}

TEST_CASE("Navigation from a fresh state and from a missing preset", "[presets]")
{
    Fixture f;
    // Stato nuovo: nessun nome salvato, la barra mostra Init e non c'è "modificato".
    CHECK(f.pm.getCurrentName().isEmpty());
    CHECK(f.pm.getDisplayName() == "Init");
    CHECK_FALSE(f.pm.isModified());
    CHECK(f.pm.findCurrent() == nullptr);

    REQUIRE(f.pm.loadNext().wasOk()); // da "Init" (mostrato) al successivo
    CHECK(f.pm.getCurrentName() == "Subtle Spread");

    // Nome che non esiste più (file eliminato): avanti = primo, indietro = ultimo.
    f.p.setPresetName("Ghost");
    CHECK_FALSE(f.pm.isModified());
    REQUIRE(f.pm.loadNext().wasOk());
    CHECK(f.pm.getCurrentName() == "Init");
    f.p.setPresetName("Ghost");
    REQUIRE(f.pm.loadPrevious().wasOk());
    CHECK(f.pm.getCurrentName() == "Noisy Desk");
}

TEST_CASE("The modified flag follows the parameters", "[presets]")
{
    Fixture f;
    const auto* warm = factoryNamed(f.pm, "Warm Console");
    REQUIRE(warm != nullptr);
    REQUIRE(f.pm.loadPreset(*warm).wasOk());
    CHECK_FALSE(f.pm.isModified());

    for (const char* id : kPresetParamIds)
    {
        const float original = raw(f.p, id);
        setParam(f.p, id, original > 0.5f ? original - 0.2f : original + 0.2f);
        CHECK(f.pm.isModified());
        setParam(f.p, id, original);
        CHECK_FALSE(f.pm.isModified());
    }

    // Canali, bypass e proprietà dello stato non contano.
    setParam(f.p, pid::chanL, 50.0f);
    setParam(f.p, pid::bypass, 1.0f);
    f.p.setAdvancedOpen(true);
    CHECK_FALSE(f.pm.isModified());
}

TEST_CASE("Loading a preset is one undo step named after the preset", "[presets][undo]")
{
    Fixture f;
    auto& um = f.p.getUndoManager();
    REQUIRE_FALSE(um.canUndo());
    setParam(f.p, pid::chanL, 12.0f);
    f.p.setAdvancedOpen(true);

    const auto before = f.pm.captureValues();
    const auto* grit = factoryNamed(f.pm, "Gentle Grit");
    REQUIRE(grit != nullptr);
    REQUIRE(f.pm.loadPreset(*grit).wasOk());

    REQUIRE(um.getUndoDescriptions().size() == 1);
    CHECK(um.getUndoDescription() == "Gentle Grit");
    CHECK(f.pm.captureValues() != before);

    CHECK(um.undo());
    const auto restored = f.pm.captureValues();
    for (size_t i = 0; i < kNumPresetParams; ++i)
    {
        CHECK(restored[i] == Approx(before[i]).margin(1e-4));
    }
    CHECK_FALSE(um.canUndo());
    CHECK(raw(f.p, pid::chanL) == Approx(12.0f));
    CHECK(f.p.getAdvancedOpen());

    CHECK(um.redo());
    CHECK(raw(f.p, pid::saturation) == Approx(grit->values[4]).margin(1e-4));
    CHECK(um.canUndo());

    // Un secondo preset è un secondo passo, un gesto della UI dopo di esso un terzo con il suo nome.
    REQUIRE(f.pm.loadPreset(*factoryNamed(f.pm, "Noisy Desk")).wasOk());
    CHECK(um.getUndoDescriptions().size() == 2);
    CHECK(um.getUndoDescription() == "Noisy Desk");
    juce::ParameterAttachment att(*f.p.getAPVTS().getParameter(pid::drift), [](float) {}, nullptr);
    att.setValueAsCompleteGesture(0.3f);
    REQUIRE(um.getUndoDescriptions().size() == 3);
    CHECK(um.getUndoDescription() == "Drift Amount");
}

TEST_CASE("Loading the same preset twice records nothing the second time", "[presets][undo]")
{
    Fixture f;
    auto& um = f.p.getUndoManager();
    REQUIRE(f.pm.loadPreset(*factoryNamed(f.pm, "Init")).wasOk());
    um.clearUndoHistory();
    REQUIRE(f.pm.loadPreset(*factoryNamed(f.pm, "Init")).wasOk());
    CHECK_FALSE(um.canUndo());
    REQUIRE(f.pm.loadPreset(*factoryNamed(f.pm, "Subtle Spread")).wasOk());
    CHECK(um.getUndoDescriptions().size() == 1);
}

TEST_CASE("The preset name is saved in the plugin state", "[presets][state]")
{
    juce::MemoryBlock block;
    {
        Fixture f;
        REQUIRE(f.pm.loadPreset(*factoryNamed(f.pm, "Wide Pair")).wasOk());
        f.p.getStateInformation(block);
    }
    Fixture g;
    CHECK(g.pm.getCurrentName().isEmpty());
    g.p.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    CHECK(g.p.getPresetName() == "Wide Pair");
    CHECK(g.pm.getDisplayName() == "Wide Pair");
    CHECK_FALSE(g.pm.isModified()); // i valori salvati sono quelli del preset

    // Un parametro cambiato dopo il ripristino lo rende "modificato".
    setParam(g.p, pid::noise, 0.99f);
    CHECK(g.pm.isModified());
}

TEST_CASE("A state without a preset name restores to no preset", "[presets][state]")
{
    juce::MemoryBlock block;
    {
        DriftboxAudioProcessor p;
        p.getStateInformation(block);
    }
    Fixture g;
    g.p.setPresetName("Something");
    g.p.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    CHECK(g.p.getPresetName().isEmpty());
}
