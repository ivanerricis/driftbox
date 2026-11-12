// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "update/UpdateChecker.h"
#include <functional>
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
    bool checkUpdates = true;  // controllo automatico degli aggiornamenti (al massimo uno al giorno)

    static constexpr int kZoomSteps[] = {75, 100, 125, 150, 200};
};

// Impostazioni salvate in un juce::PropertiesFile (<dati app>/Driftbox/settings.settings). Un'unica istanza è
// condivisa da tutte le istanze del plugin del processo (juce::SharedResourcePointer<SettingsManager>); il
// file è protetto da un lock tra processi e un file mancante o corrotto dà semplicemente i valori
// predefiniti. SOLO MESSAGE THREAD. Invia (in modo sincrono) un messaggio di cambio a ogni modifica.
class SettingsManager final : public juce::ChangeBroadcaster
{
public:
    // Il controllo usa il client HTTP nativo di JUCE (WinINet / NSURLSession), che c'è solo su Windows e
    // macOS (JUCE_USE_CURL resta 0): su Linux la sezione "Updates" è disattivata.
#if JUCE_WINDOWS || JUCE_MAC
    static constexpr bool kUpdateCheckAvailable = true;
#else
    static constexpr bool kUpdateCheckAvailable = false;
#endif

    // Stato dell'ultimo controllo del processo (condiviso da tutte le istanze del plugin).
    struct UpdateState
    {
        enum class Phase
        {
            idle, // nessun controllo ancora
            checking,
            done // `result` è valido
        };
        Phase phase = Phase::idle;
        update::UpdateResult result;
    };

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

    // ---- Controllo degli aggiornamenti (Windows / macOS; altrove non fa nulla) ----
    // Non blocca mai: la richiesta gira su un thread in background e lo stato cambia sul message thread,
    // annunciato da updateChanges().
    // Automatico: non fa nulla se `checkUpdates` è spento o se dall'ultimo controllo (salvato nel file delle
    // impostazioni, quindi condiviso tra istanze e processi) è passato meno di un giorno. Da chiamare quando
    // si apre un editor.
    void checkForUpdatesIfDue();
    // Sempre (pulsante "Check now"), anche con l'interruttore spento; non tocca la pianificazione
    // giornaliera. Falso se ne sta già girando uno o la piattaforma non ha un client HTTP.
    bool checkForUpdatesNow();
    bool isUpdateCheckAvailable() const;
    const UpdateState& getUpdateState() const noexcept { return updateState_; }
    juce::ChangeBroadcaster& updateChanges() noexcept { return updateChanges_; }
    juce::int64 getLastUpdateCheckMs() const noexcept { return lastUpdateCheckMs_; }
    // Solo test: sostituisce la richiesta di rete (e abilita il controllo su ogni piattaforma) e l'orologio
    // (millisecondi dall'epoca). Una funzione vuota ripristina la richiesta e l'orologio veri.
    static void setUpdateFetcherOverride(update::Fetcher fetcher);
    static void setClockOverride(std::function<juce::int64()> clock);

private:
    bool startUpdateCheck();
    void init(const juce::File& file);
    void readFromProperties();
    void writeToProperties();
    static UserSettings sanitised(UserSettings s);

    juce::File file_;
    std::unique_ptr<juce::InterProcessLock> lock_;
    std::unique_ptr<juce::PropertiesFile> props_;
    UserSettings data_;
    bool loadedOk_ = true;
    juce::int64 lastUpdateCheckMs_ = 0; // 0 = mai; non fa parte di UserSettings (non lo cambia l'utente)
    UpdateState updateState_;
    juce::ChangeBroadcaster updateChanges_;
    update::UpdateChecker updateChecker_; // ultimo membro: il suo thread si ferma per primo alla distruzione

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsManager)
};
} // namespace driftbox
