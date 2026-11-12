// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

namespace driftbox
{
// Impostazioni dell'utente (mai parte dello stato del progetto né dei preset).
struct UserSettings
{
    int zoomPercent = 100; // 75 / 100 / 125 / 150 / 200
    bool tooltips = true;
    bool showCpu = true;
    juce::String presetFolder; // vuoto = cartella predefinita della piattaforma

    static constexpr int kZoomSteps[] = {75, 100, 125, 150, 200};
};

// Impostazioni salvate in un juce::PropertiesFile (<dati app>/Driftbox/settings.settings). Un'unica istanza è
// condivisa da tutte le istanze del plugin del processo (juce::SharedResourcePointer<SettingsManager>); il
// file è protetto da un lock tra processi e un file mancante o corrotto dà semplicemente i valori
// predefiniti. SOLO MESSAGE THREAD. Invia (in modo sincrono) un messaggio di cambio a ogni modifica.
class SettingsManager final : public juce::ChangeBroadcaster
{
public:
    SettingsManager(); // file predefinito
    explicit SettingsManager(const juce::File& file);
    ~SettingsManager() override;

    static juce::File getDefaultFile();
    // Solo test: reindirizza il file predefinito e la cartella dei preset utente predefinita (così nessun
    // test tocca i dati veri dell'utente). Vale per le istanze create dopo la chiamata.
    static void setTestOverrides(const juce::File& file, const juce::File& defaultPresetFolder);

    juce::File getFile() const { return file_; }
    // Falso se il file esisteva ma non era leggibile (le impostazioni sono allora quelle predefinite).
    bool loadedOk() const noexcept { return loadedOk_; }

    const UserSettings& get() const noexcept { return data_; }
    // Valida (i valori sconosciuti diventano quelli predefiniti), salva e notifica.
    void set(const UserSettings& s);
    // Ripristina tutte le impostazioni predefinite; salva e notifica.
    void resetToDefaults();

    // Cartella dei preset utente: quella impostata o quella predefinita (%APPDATA%/Driftbox/Presets).
    juce::File getEffectivePresetFolder() const;
    static juce::File getEffectivePresetFolder(const UserSettings& s);

private:
    void init(const juce::File& file);
    void readFromProperties();
    void writeToProperties();
    static UserSettings sanitised(UserSettings s);

    juce::File file_;
    std::unique_ptr<juce::InterProcessLock> lock_;
    std::unique_ptr<juce::PropertiesFile> props_;
    UserSettings data_;
    bool loadedOk_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsManager)
};
} // namespace driftbox
