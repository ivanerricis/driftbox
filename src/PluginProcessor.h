// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DriftEngine.h"
#include "params/DriftboxParameters.h"
#include "presets/PresetManager.h"
#include "settings/SettingsManager.h"
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <utility>

namespace driftbox
{
inline constexpr int kDefaultUiWidth = 720;
inline constexpr int kDefaultUiHeight = 440;

// Processor di Driftbox: parametri (tra cui il canale L e R), stato, Randomize, preset e undo/redo dei gesti
// della UI. Le impostazioni dell'utente sono condivise da tutte le istanze (SettingsManager).
class DriftboxAudioProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    DriftboxAudioProcessor();
    ~DriftboxAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioParameterBool* getBypassParameter() const override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts_; }
    MeterState& getMeters() noexcept { return engine_.meters(); }

    // Randomize: sceglie una coppia di canali a caso (L in 1..64, R = L % 64 + 1), sempre diversa dalla
    // coppia corrente, e la applica come UN SOLO passo di undo "Randomize". L'audio thread passa ai nuovi
    // profili con un crossfade. SOLO dal message thread.
    void randomize();

    // Cronologia undo dei cambi fatti dalla UI del plugin (SOLO MESSAGE THREAD; l'audio thread non la tocca
    // mai). Un passo per gesto (trascinamento di una manopola, valore digitato, click su un interruttore) e
    // uno per ogni Randomize. L'automazione dell'host non viene registrata. Il ripristino di uno stato
    // dell'host la svuota.
    juce::UndoManager& getUndoManager() noexcept { return undoManager_; }
    void clearUndoHistory();
    // Vero mentre un gesto della UI è in corso (tra beginChangeGesture e endChangeGesture).
    bool isUiGestureOpen() const noexcept;

    // Limiti della cronologia: circa kUndoMaxUnits passi da un'unità, ma mai meno di kUndoMinTransactions.
    static constexpr int kUndoMaxUnits = 200;
    static constexpr int kUndoMinTransactions = 50;

    // Dimensione dell'editor, salvata nello stato del plugin.
    juce::Point<int> getUiSize() const;
    void setUiSize(int width, int height);

    // Pannello Advanced aperto o chiuso, salvato nello stato del plugin (message thread). Non è un
    // parametro e non crea passi di undo; manca nei vecchi stati e allora vale falso.
    bool getAdvancedOpen() const;
    void setAdvancedOpen(bool open);

    // Vero se lo stato contiene già la dimensione dell'editor (un editor nuovo usa allora quella e non lo
    // zoom delle impostazioni).
    bool hasStoredUiSize() const;

    // ---- Preset e impostazioni (SOLO MESSAGE THREAD) ----
    PresetManager& getPresetManager() noexcept { return presets_; }
    const PresetManager& getPresetManager() const noexcept { return presets_; }
    SettingsManager& getSettings() noexcept { return *settings_; }

    // Nome del preset corrente, salvato nello stato (proprietà non parametrica `presetName`, come uiWidth).
    // Vuoto = nessun preset caricato. Non crea passi di undo.
    juce::String getPresetName() const;
    void setPresetName(const juce::String& name);

    // Imposta i sette parametri dei preset (mai canali, bypass o proprietà dello stato) come UN solo passo di
    // undo chiamato `undoName`. Non sostituisce l'albero dello stato. Non cambia presetName.
    void applyPresetValues(const PresetValues& values, const juce::String& undoName);

    // Testo per la segnalazione dei problemi (pulsante "Copy debug info"): nome, versione, formato, host,
    // sistema operativo, frequenza di campionamento e dimensione del blocco.
    juce::String getDebugInfo() const;

private:
    // Message thread: esegue la richiesta differita di svuotare la cronologia (vedi clearUndoHistory()).
    void timerCallback() override;
    // Unico punto per le migrazioni dello stato; riceve la versione trovata nello stato salvato.
    static void migrateState(juce::ValueTree& state, int fromVersion);

    class UndoRecorder;

    // Estrae una coppia di canali (L, R = L % 64 + 1) diversa da quella corrente.
    std::pair<int, int> drawChannelPair() const;

    juce::UndoManager undoManager_{kUndoMaxUnits, kUndoMinTransactions}; // sopravvive ad apvts_ e al recorder
    juce::AudioProcessorValueTreeState apvts_;
    ParamRefs params_;
    DriftEngine engine_;
    juce::SharedResourcePointer<SettingsManager> settings_; // condiviso da tutte le istanze del processo
    PresetManager presets_{*this};
    // Vero dentro randomize() e applyPresetValues(): il recorder non apre una transazione propria e il passo
    // resta quello già aperto ("Randomize" o il nome del preset).
    bool groupedEdit_ = false;
    std::atomic<bool> pendingUndoClear_{false};
    double currentSampleRate_ = 0.0;
    std::unique_ptr<UndoRecorder> undoRecorder_; // ultimo: distrutto per primo

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DriftboxAudioProcessor)
};
} // namespace driftbox
