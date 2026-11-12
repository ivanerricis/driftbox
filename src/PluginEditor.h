// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"
#include "ui/Footer.h"
#include "ui/LookAndFeel.h"
#include "ui/OverlayMenu.h"
#include "ui/Panels.h"
#include "ui/PresetDialog.h"
#include "ui/SettingsPanel.h"
#include "ui/TopBar.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>

namespace driftbox
{
// Editor di Driftbox (T7). Tutto il contenuto sta in content(), disegnato alla dimensione di base 720 x 440 e
// scalato in modo uniforme alla dimensione dell'editor: la finestra si ridimensiona con proporzioni fisse
// (75 % - 200 %). Lo zoom è la dimensione della finestra stessa (setZoom), così non si scontra con lo
// scaling per DPI che applica l'host.
class DriftboxAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer,
                                           private juce::ChangeListener
{
public:
    explicit DriftboxAudioProcessorEditor(DriftboxAudioProcessor& processor);
    ~DriftboxAudioProcessorEditor() override;

    // Notifica del clic sull'ingranaggio; la finestra delle impostazioni si apre comunque (openSettings()).
    std::function<void()> onSettings;
    // Mostra "CPU x.x %" nel piè di pagina (lo governano le impostazioni, ma si può cambiare direttamente).
    bool showCpu = true;

    // Zoom 0.75 - 2.0 (1 = 720 x 440 pixel). Ridimensiona l'editor mantenendo le proporzioni.
    void setZoom(float zoom);
    float getZoom() const noexcept
    {
        return static_cast<float>(getWidth()) / static_cast<float>(ui::kBaseWidth);
    }
    // Suggerimenti al passaggio del mouse come finestrella (oltre che nel piè di pagina).
    void setTooltipsEnabled(bool enabled);

    // Undo e redo dei cambi fatti dalla UI (pulsanti, Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z, Ctrl+Y). Falsi se non
    // c'era nulla da annullare o ripetere.
    bool undo();
    bool redo();

    // Esegue subito un aggiornamento della UI (quello che fa il timer).
    void refreshNow();

    // Genitore per i pannelli sovrapposti: stesso sistema di coordinate del layout.
    juce::Component& content() noexcept { return content_; }

    // ---- Finestra delle impostazioni e dei preset (sovrapposte all'editor, mai finestre native) ----
    void openSettings();
    bool isSettingsOpen() const;
    ui::SettingsPanel* getSettingsPanel() noexcept;
    // Chiude la finestra sovrapposta aperta (impostazioni o preset): si nasconde subito e si distrugge dopo.
    void closeModal();
    // Menu dei preset (elenco di fabbrica / utente) e menu ⋯ (Save, Save As, Delete).
    void openPresetList();
    void openPresetActions();
    bool isMenuOpen() const;
    // Solo se è visibile (un menu appena chiuso si distrugge al giro successivo del message loop).
    ui::OverlayMenu* getMenu() noexcept;
    void closeMenu();
    // Finestra "Save As" / "Delete" aperta dal menu ⋯.
    ui::PresetDialog* getPresetDialog() noexcept;

    // Accesso ai componenti (T8 e test).
    ui::TopBar& getTopBar() noexcept { return topBar_; }
    ui::Footer& getFooter() noexcept { return footer_; }
    ui::ChannelPanel& getChannelPanel() noexcept { return channelPanel_; }
    ui::TolerancePanel& getTolerancePanel() noexcept { return tolerancePanel_; }
    ui::DriftPanel& getDriftPanel() noexcept { return driftPanel_; }
    ui::AdvancedPanel& getAdvancedPanel() noexcept { return advancedPanel_; }
    ui::OutputPanel& getOutputPanel() noexcept { return outputPanel_; }

    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    // Contiene tutta la UI alla dimensione di base; la scala una trasformazione.
    class Content final : public juce::Component
    {
    public:
        void paint(juce::Graphics& g) override { g.fillAll(ui::col::bg0); }
    };

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    // Applica le impostazioni (suggerimenti, CPU, zoom se cambiato, cartella dei preset utente).
    void applySettings();
    void openPresetDialog(ui::PresetDialog::Mode mode);
    void showMenu(std::unique_ptr<ui::OverlayMenu> menu);
    juce::Point<int> menuAnchor(juce::Component& button) const;
    void layoutContent();
    void updateHint(const juce::MouseEvent&);

    DriftboxAudioProcessor& processor_;
    ui::DriftboxLookAndFeel lnf_;
    double lastTickMs_ = 0.0;

    Content content_;
    ui::TopBar topBar_;
    ui::ChannelPanel channelPanel_;
    ui::TolerancePanel tolerancePanel_;
    ui::DriftPanel driftPanel_;
    ui::AdvancedPanel advancedPanel_;
    ui::OutputPanel outputPanel_;
    ui::Footer footer_;
    std::unique_ptr<juce::TooltipWindow> tooltip_;
    std::unique_ptr<ui::OverlayMenu> menu_;
    std::unique_ptr<ui::ModalOverlay> modal_;
    int appliedZoomPercent_ = 100;
    bool presetsScanned_ = false;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DriftboxAudioProcessorEditor)
};
} // namespace driftbox
