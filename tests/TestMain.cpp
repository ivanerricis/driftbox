// SPDX-License-Identifier: AGPL-3.0-or-later
#include "settings/SettingsManager.h"
#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Nessun test tocca le impostazioni o i preset veri dell'utente: file delle impostazioni e cartella dei
    // preset predefinita stanno in una cartella temporanea, creata per questa esecuzione e rimossa alla fine.
    const auto sandbox =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("DriftboxTests-" + juce::String::toHexString(juce::Random().nextInt64()));
    sandbox.createDirectory();
    driftbox::SettingsManager::setTestOverrides(sandbox.getChildFile("settings.settings"),
                                                sandbox.getChildFile("Presets"));

    const int result = Catch::Session().run(argc, argv);

    driftbox::SettingsManager::setTestOverrides({}, {});
    sandbox.deleteRecursively();
    return result;
}
