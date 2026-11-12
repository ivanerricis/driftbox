// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "settings/SettingsManager.h"
#include "ui/ModalOverlay.h"
#include "ui/Widgets.h"
#include <functional>
#include <memory>

namespace driftbox::ui
{
// Interruttore on/off del mockup: traccia 38 x 22 (ambra se acceso), pallino 18 px.
class ToggleSwitch final : public juce::Button
{
public:
    explicit ToggleSwitch(const juce::String& name);
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
};

// Selettore a segmenti dello zoom: 75 / 100 / 125 / 150 / 200 %.
class ZoomSelector final : public juce::Component
{
public:
    ZoomSelector();

    std::function<void(int percent)> onSelect;
    void setPercent(int percent);
    int getPercent() const noexcept { return percent_; }
    // Come un clic sul segmento `index` (0..4).
    void select(int index);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    static constexpr int kSegmentW = 38, kSegmentH = 22, kGap = 2, kPad = 2;
    static constexpr int kWidth = 5 * kSegmentW + 4 * kGap + 2 * kPad;
    static constexpr int kHeight = kSegmentH + 2 * kPad;

private:
    int indexAt(juce::Point<int>) const;
    juce::Rectangle<int> segment(int index) const;

    int percent_ = 100;
};

// Finestra delle impostazioni sovrapposta all'editor (come nel mockup): zoom, suggerimenti, CPU, cartella dei
// preset utente, informazioni con il collegamento al codice sorgente e i pulsanti "Copy debug info" e "Reset
// settings". Si chiude con DONE, con la X o con Escape. Legge e scrive il SettingsManager condiviso (message
// thread) e si aggiorna quando le impostazioni cambiano da altrove.
class SettingsPanel final : public ModalOverlay, private juce::ChangeListener, private juce::Timer
{
public:
    static constexpr int kCardW = 460, kRowH = 44, kRowGap = 6, kPadX = 14, kPadY = 12, kHeaderH = 26;
    static constexpr const char* kSourceUrl = "https://github.com/ivanerricis/driftbox";

    // `debugInfo` fornisce il testo di "Copy debug info" (nome, versione, formato, host, SO, frequenza,
    // blocco).
    SettingsPanel(SettingsManager& settings, std::function<juce::String()> debugInfo);
    ~SettingsPanel() override;

    // Tests: sostituisce la copia negli appunti di sistema.
    std::function<void(const juce::String&)> copyToClipboard;

    ZoomSelector& zoomSelector() noexcept { return zoom_; }
    ToggleSwitch& tooltipsSwitch() noexcept { return tooltips_; }
    ToggleSwitch& cpuSwitch() noexcept { return cpu_; }
    PillButton& changeFolderButton() noexcept { return changeFolder_; }
    PillButton& copyDebugButton() noexcept { return copyDebug_; }
    PillButton& resetButton() noexcept { return reset_; }
    PillButton& doneButton() noexcept { return done_; }
    IconButton& closeButton() noexcept { return close_; }
    juce::HyperlinkButton& sourceLink() noexcept { return source_; }
    juce::String versionText() const;
    // Testo della cartella dei preset utente come appare nella finestra (%APPDATA% o ~ al posto del
    // percorso).
    juce::String folderText() const;

    // T8b: la sezione "Updates" (Check for updates / Check now / interruttore) va qui, tra la cartella dei
    // preset e le informazioni. Per ora è nascosta e non occupa spazio; basta renderla visibile (e
    // aggiungerle i controlli) perché la scheda si allarghi di una riga (kRowH) e la disponga.
    juce::Component& updatesSection() noexcept { return updatesSection_; }
    void updateLayout(); // da chiamare dopo aver mostrato o nascosto updatesSection()

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void syncFromSettings();
    void applyChange(const std::function<void(UserSettings&)>& edit);
    void chooseFolder();
    int cardHeight() const;

    SettingsManager& settings_;
    std::function<juce::String()> debugInfo_;

    IconButton close_{Icon::close, "Close settings", "Close settings", false};
    ZoomSelector zoom_;
    ToggleSwitch tooltips_{"Tooltips"};
    ToggleSwitch cpu_{"Show CPU"};
    PillButton changeFolder_; // "Change..." (inizializzato nel costruttore: ha il carattere di ellissi)
    juce::Component updatesSection_; // vedi updatesSection()
    juce::HyperlinkButton source_;
    PillButton copyDebug_{"Copy debug info", "Copy the plugin and host details to the clipboard"};
    PillButton reset_{"Reset settings", "Restore every setting to its default"};
    PillButton done_{"DONE", "Close the settings"};

    juce::Rectangle<int> zoomRow_, tipsRow_, cpuRow_, folderRow_, updatesRow_, aboutRow_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPanel)
};
} // namespace driftbox::ui
