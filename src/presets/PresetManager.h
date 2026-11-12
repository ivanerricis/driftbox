// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "presets/PresetValues.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace driftbox
{
class DriftboxAudioProcessor;

// Una voce della libreria (di fabbrica o utente).
struct PresetEntry
{
    juce::String name;
    bool factory = false;
    juce::File file; // solo preset utente
    PresetValues values = kPresetDefaults;
};

// Gestore dei preset. SOLO MESSAGE THREAD; l'audio thread non lo tocca mai.
//
// Un preset salva solo i sette parametri di PresetValues. Il caricamento NON sostituisce l'albero dello
// stato: imposta i parametri uno per uno come UN solo passo di undo con il nome del preset
// (DriftboxAudioProcessor::applyPresetValues). Il nome del preset corrente sta nello stato del plugin
// (proprietà `presetName`), quindi viene salvato con il progetto; "modificato" si calcola confrontando i
// valori correnti con quelli del preset caricato (cercato per nome), senza listener.
//
// Formato dei file utente (.dbp, XML):
//   <DRIFTBOX_PRESET version="1" name="..."><PARAM id="drift" value="0.62"/> ... </DRIFTBOX_PRESET>
// Per i preset utente il nome è quello del file; i parametri mancanti prendono il valore predefinito, i
// valori fuori intervallo vengono limitati. File illeggibili o di una versione futura sono ignorati
// (getSkippedFiles).
class PresetManager final
{
public:
    static constexpr const char* kFileExtension = ".dbp";
    static constexpr const char* kPresetTag = "DRIFTBOX_PRESET";
    static constexpr int kPresetVersion = 1;
    static constexpr int kMaxNameLength = 64;

    explicit PresetManager(DriftboxAudioProcessor& processor);

    // ---- Cartella utente ----
    // %APPDATA%/Driftbox/Presets su Windows, equivalenti userApplicationDataDirectory altrove.
    static juce::File getDefaultUserFolder();
    juce::File getUserFolder() const { return userFolder_; }
    // Memorizza la cartella; non legge il disco (usare rescan()).
    void setUserFolder(const juce::File& folder) { userFolder_ = folder; }

    // ---- Libreria ----
    void rescan(); // rilegge la cartella utente (i file non validi sono ignorati)
    const std::vector<PresetEntry>& getFactoryPresets() const { return factory_; }
    const std::vector<PresetEntry>& getUserPresets() const { return user_; }
    std::vector<PresetEntry> getOrderedPresets() const; // di fabbrica, poi utente
    const juce::StringArray& getSkippedFiles() const { return skipped_; }

    // ---- Preset corrente ----
    // Nome salvato nello stato; vuoto = nessun preset caricato (stato nuovo o di una versione precedente).
    juce::String getCurrentName() const;
    // Nome mostrato nella barra: "Init" se nessun preset è caricato.
    juce::String getDisplayName() const;
    const PresetEntry* findCurrent() const; // nullptr se non esiste in libreria
    bool isCurrentFactory() const;
    bool isCurrentUser() const;
    // Vero se i parametri differiscono dal preset caricato (falso se non c'è un preset caricato).
    bool isModified() const;
    PresetValues captureValues() const;

    // ---- Caricamento ----
    juce::Result loadPreset(const PresetEntry& entry);
    juce::Result loadNext();     // di fabbrica poi utente, con giro completo
    juce::Result loadPrevious(); // idem

    // ---- Operazioni sui preset utente ----
    juce::Result save(); // sovrascrive il preset utente corrente
    juce::Result saveAs(const juce::String& name, bool overwrite = false);
    juce::Result deleteCurrent(); // poi carica Init
    bool userPresetExists(const juce::String& name) const;
    bool isFactoryName(const juce::String& name) const;

    // ---- Utilità (pubbliche per la UI e i test) ----
    // Stringa vuota se il nome non è utilizzabile come nome di file.
    static juce::String sanitiseName(const juce::String& name);
    static juce::Result parsePreset(const juce::XmlElement& root, PresetValues& values, juce::String& name);
    static juce::Result parsePresetFile(const juce::File& file, PresetValues& values);
    static std::unique_ptr<juce::XmlElement> toXml(const PresetValues& values, const juce::String& name);
    // Preset di fabbrica incorporati (resources/presets/*.xml), in ordine di nome file.
    static std::vector<PresetEntry> loadFactoryPresets();

private:
    const PresetEntry* findByName(const juce::String& name) const;
    juce::Result step(int direction);
    juce::File fileFor(const juce::String& name) const;
    juce::Result writeFile(const juce::File& dest, const juce::String& name, const PresetValues& v) const;

    DriftboxAudioProcessor& processor_;
    juce::File userFolder_;
    std::vector<PresetEntry> factory_;
    std::vector<PresetEntry> user_;
    juce::StringArray skipped_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetManager)
};
} // namespace driftbox
