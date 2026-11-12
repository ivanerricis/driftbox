// SPDX-License-Identifier: AGPL-3.0-or-later
// Impostazioni dell'utente: salvataggio, file mancante o corrotto, ripristino, notifiche.
#include "PluginProcessor.h"
#include "PluginInfo.h"
#include "settings/SettingsManager.h"
#include <catch2/catch_test_macros.hpp>

using namespace driftbox;

namespace
{
struct TempFile
{
    TempFile()
        : dir(juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("DriftboxSettingsTest-" +
                                juce::String::toHexString(juce::Random().nextInt64()))),
          file(dir.getChildFile("settings.settings"))
    {
    }
    ~TempFile() { dir.deleteRecursively(); }
    juce::File dir, file;
};

struct Counter final : juce::ChangeListener
{
    void changeListenerCallback(juce::ChangeBroadcaster*) override { ++count; }
    int count = 0;
};
} // namespace

TEST_CASE("Settings defaults", "[settings]")
{
    TempFile t;
    SettingsManager sm(t.file);
    CHECK(sm.get().zoomPercent == 100);
    CHECK(sm.get().tooltips);
    CHECK(sm.get().showCpu);
    CHECK(sm.get().presetFolder.isEmpty());
    CHECK(sm.get().checkUpdates); // il controllo degli aggiornamenti è attivo di default
    CHECK(sm.loadedOk());
    // Senza cartella impostata vale quella predefinita (qui quella di prova di TestMain).
    CHECK(sm.getEffectivePresetFolder().getFileName() == "Presets");
}

TEST_CASE("Settings round trip through the file", "[settings]")
{
    TempFile t;
    {
        SettingsManager sm(t.file);
        UserSettings s;
        s.zoomPercent = 150;
        s.tooltips = false;
        s.showCpu = false;
        s.presetFolder = t.dir.getChildFile("MyPresets").getFullPathName();
        sm.set(s);
    }
    REQUIRE(t.file.existsAsFile());
    SettingsManager sm(t.file);
    CHECK(sm.loadedOk());
    CHECK(sm.get().zoomPercent == 150);
    CHECK_FALSE(sm.get().tooltips);
    CHECK_FALSE(sm.get().showCpu);
    CHECK(sm.get().presetFolder == t.dir.getChildFile("MyPresets").getFullPathName());
    CHECK(sm.getEffectivePresetFolder() == t.dir.getChildFile("MyPresets"));
}

TEST_CASE("A missing settings file gives the defaults and is not created by reading", "[settings]")
{
    TempFile t;
    REQUIRE_FALSE(t.file.exists());
    {
        SettingsManager sm(t.file);
        CHECK(sm.loadedOk());
        CHECK(sm.get().zoomPercent == 100);
        CHECK(sm.get().tooltips);
        CHECK(sm.get().showCpu);
    }
    CHECK_FALSE(t.file.exists());
}

TEST_CASE("A corrupt settings file gives the defaults", "[settings]")
{
    for (const char* garbage : {"this is not xml <<<", "<PROPERTIES><VALUE name=\"uiZoom\" val=\"abc\"/>",
                                "\x01\x02\x03", "<PROPERTIES></PROPERTIES>"})
    {
        TempFile t;
        t.dir.createDirectory();
        REQUIRE(t.file.replaceWithText(garbage));
        SettingsManager sm(t.file);
        CHECK(sm.get().zoomPercent == 100);
        CHECK(sm.get().tooltips);
        CHECK(sm.get().showCpu);
        CHECK(sm.get().presetFolder.isEmpty());

        // Dopo un file illeggibile le impostazioni si possono comunque salvare.
        UserSettings s;
        s.zoomPercent = 125;
        sm.set(s);
        SettingsManager again(t.file);
        CHECK(again.get().zoomPercent == 125);
    }
}

TEST_CASE("Invalid values are replaced by the defaults", "[settings]")
{
    TempFile t;
    t.dir.createDirectory();
    REQUIRE(t.file.replaceWithText("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<PROPERTIES>"
                                   "<VALUE name=\"uiZoom\" val=\"137\"/>"
                                   "<VALUE name=\"tooltips\" val=\"0\"/></PROPERTIES>"));
    SettingsManager sm(t.file);
    CHECK(sm.get().zoomPercent == 100); // non è uno dei cinque valori
    CHECK_FALSE(sm.get().tooltips);     // il resto del file si legge

    UserSettings s;
    s.zoomPercent = 999;
    sm.set(s);
    CHECK(sm.get().zoomPercent == 100);
    for (int z : {75, 100, 125, 150, 200})
    {
        s.zoomPercent = z;
        sm.set(s);
        CHECK(sm.get().zoomPercent == z);
    }
}

TEST_CASE("Reset restores every default, saves and notifies", "[settings]")
{
    TempFile t;
    SettingsManager sm(t.file);
    Counter counter;
    sm.addChangeListener(&counter);

    UserSettings s;
    s.zoomPercent = 200;
    s.tooltips = false;
    s.showCpu = false;
    s.presetFolder = "C:/somewhere";
    s.checkUpdates = false;
    sm.set(s);
    CHECK(counter.count == 1); // sincrono
    {
        SettingsManager saved(t.file);
        CHECK_FALSE(saved.get().checkUpdates); // salvato nel file
    }

    sm.resetToDefaults();
    CHECK(counter.count == 2);
    CHECK(sm.get().zoomPercent == 100);
    CHECK(sm.get().tooltips);
    CHECK(sm.get().showCpu);
    CHECK(sm.get().presetFolder.isEmpty());
    CHECK(sm.get().checkUpdates);

    SettingsManager reread(t.file);
    CHECK(reread.get().zoomPercent == 100);
    CHECK(reread.get().tooltips);
    CHECK(reread.get().showCpu);
    CHECK(reread.get().presetFolder.isEmpty());
    CHECK(reread.get().checkUpdates);
    sm.removeChangeListener(&counter);
}

TEST_CASE("Processors share one settings object", "[settings]")
{
    DriftboxAudioProcessor a, b;
    CHECK(&a.getSettings() == &b.getSettings());
}

TEST_CASE("Debug info lists name, version, format, host, OS, sample rate and block size", "[settings]")
{
    DriftboxAudioProcessor p;
    p.setRateAndBufferSizeDetails(44100.0, 256);
    const auto text = p.getDebugInfo();
    CHECK(text.contains(info::kName));
    CHECK(text.contains(info::kVersion));
    CHECK(text.contains("Format: "));
    CHECK(text.contains("Host: "));
    CHECK(text.contains("OS: " + juce::SystemStats::getOperatingSystemName()));
    CHECK(text.contains("Sample rate: 44100 Hz"));
    CHECK(text.contains("Block size: 256"));
}
