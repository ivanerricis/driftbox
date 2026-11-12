// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"

#include "params/ParamIDs.h"
#include <cmath>

namespace driftbox
{
namespace
{
using namespace ui;

float rawValue(juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    return apvts.getRawParameterValue(id)->load();
}
} // namespace

DriftboxAudioProcessorEditor::DriftboxAudioProcessorEditor(DriftboxAudioProcessor& processor)
    : AudioProcessorEditor(&processor), processor_(processor), topBar_(processor.getAPVTS()),
      channelPanel_(processor.getAPVTS()), driftPanel_(processor.getAPVTS()),
      advancedPanel_(processor.getAPVTS()), outputPanel_(processor.getAPVTS())
{
    setTitle("Driftbox");
    setWantsKeyboardFocus(true); // i clic su controlli che non prendono il focus arrivano qui (undo e redo)
    setLookAndFeel(&lnf_);

    addAndMakeVisible(content_);
    content_.setTitle("Driftbox");
    for (juce::Component* c :
         {static_cast<juce::Component*>(&topBar_), static_cast<juce::Component*>(&channelPanel_),
          static_cast<juce::Component*>(&tolerancePanel_), static_cast<juce::Component*>(&driftPanel_),
          static_cast<juce::Component*>(&advancedPanel_), static_cast<juce::Component*>(&outputPanel_),
          static_cast<juce::Component*>(&footer_)})
    {
        content_.addAndMakeVisible(*c);
    }
    footer_.setInterceptsMouseClicks(false, false);

    topBar_.onUndo = [this] { undo(); };
    topBar_.onRedo = [this] { redo(); };
    topBar_.onSettings = [this]
    {
        if (onSettings)
        {
            onSettings();
        }
        openSettings();
    };

    // Barra dei preset: il gestore (nel processor) fa il lavoro, la barra mostra nome e "modificato".
    auto& bar = topBar_.presetBar();
    bar.onPrev = [this]
    {
        processor_.getPresetManager().loadPrevious();
        refreshNow();
    };
    bar.onNext = [this]
    {
        processor_.getPresetManager().loadNext();
        refreshNow();
    };
    bar.onOpenList = [this] { openPresetList(); };
    bar.onOpenActions = [this] { openPresetActions(); };
    channelPanel_.onRandomize = [this] { processor_.randomize(); };

    // Pannello Advanced: stato salvato nel progetto, scritto a ogni apertura o chiusura.
    advancedPanel_.setOpen(processor_.getAdvancedOpen());
    advancedPanel_.onOpenChanged = [this](bool open) { processor_.setAdvancedOpen(open); };

    // Dimensione: quella salvata se ha le nostre proporzioni, altrimenti la base.
    const auto stored = processor_.getUiSize();
    int w = kBaseWidth, h = kBaseHeight;
    const int zoomPercent = processor_.getSettings().get().zoomPercent;
    const float aspect = static_cast<float>(kBaseWidth) / static_cast<float>(kBaseHeight);
    if (processor_.hasStoredUiSize() &&
        std::abs(static_cast<float>(stored.x) / static_cast<float>(stored.y) - aspect) < 0.02f &&
        static_cast<float>(stored.x) >= static_cast<float>(kBaseWidth) * kMinZoom - 1.0f &&
        static_cast<float>(stored.x) <= static_cast<float>(kBaseWidth) * kMaxZoom + 1.0f)
    {
        w = stored.x;
        h = juce::roundToInt(static_cast<float>(w) / aspect);
    }
    else if (!processor_.hasStoredUiSize())
    {
        // Editor nuovo in uno stato senza dimensione salvata: parte dallo zoom delle impostazioni.
        const float zoom = static_cast<float>(zoomPercent) / 100.0f;
        w = juce::roundToInt(static_cast<float>(kBaseWidth) * zoom);
        h = juce::roundToInt(static_cast<float>(kBaseHeight) * zoom);
    }
    constrainer_.setFixedAspectRatio(static_cast<double>(aspect));
    constrainer_.setSizeLimits(juce::roundToInt(static_cast<float>(kBaseWidth) * kMinZoom),
                               juce::roundToInt(static_cast<float>(kBaseHeight) * kMinZoom),
                               juce::roundToInt(static_cast<float>(kBaseWidth) * kMaxZoom),
                               juce::roundToInt(static_cast<float>(kBaseHeight) * kMaxZoom));
    setResizable(true, true);
    setConstrainer(&constrainer_);

    content_.setSize(kBaseWidth, kBaseHeight);
    layoutContent();
    addMouseListener(this, true);
    appliedZoomPercent_ = zoomPercent; // lo zoom iniziale è già in w e h
    applySettings();
    processor_.getSettings().addChangeListener(this);
    setSize(w, h);

    lastTickMs_ = juce::Time::getMillisecondCounterHiRes();
    refreshNow();
    startTimerHz(30);
}

DriftboxAudioProcessorEditor::~DriftboxAudioProcessorEditor()
{
    stopTimer();
    processor_.getSettings().removeChangeListener(this);
    removeMouseListener(this);
    tooltip_.reset();
    setLookAndFeel(nullptr);
}

void DriftboxAudioProcessorEditor::setZoom(float zoom)
{
    zoom = juce::jlimit(kMinZoom, kMaxZoom, zoom);
    setSize(juce::roundToInt(static_cast<float>(kBaseWidth) * zoom),
            juce::roundToInt(static_cast<float>(kBaseHeight) * zoom));
}

void DriftboxAudioProcessorEditor::setTooltipsEnabled(bool enabled)
{
    if (enabled && tooltip_ == nullptr)
    {
        tooltip_ = std::make_unique<juce::TooltipWindow>(this, 700);
        tooltip_->setLookAndFeel(&lnf_);
    }
    else if (!enabled && tooltip_ != nullptr)
    {
        tooltip_.reset();
    }
}

void DriftboxAudioProcessorEditor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    applySettings();
}

void DriftboxAudioProcessorEditor::applySettings()
{
    auto& settings = processor_.getSettings();
    const auto& s = settings.get();
    setTooltipsEnabled(s.tooltips);
    showCpu = s.showCpu;
    // Lo zoom si applica solo quando cambia: così un altro interruttore non ridimensiona la finestra che
    // l'utente ha trascinato.
    if (s.zoomPercent != appliedZoomPercent_)
    {
        appliedZoomPercent_ = s.zoomPercent;
        setZoom(static_cast<float>(s.zoomPercent) / 100.0f);
    }
    auto& presets = processor_.getPresetManager();
    const auto folder = settings.getEffectivePresetFolder();
    if (folder != presets.getUserFolder() || !presetsScanned_)
    {
        presets.setUserFolder(folder);
        presets.rescan();
        presetsScanned_ = true;
    }
}

// ---- Finestre sovrapposte ----
void DriftboxAudioProcessorEditor::openSettings()
{
    if (isSettingsOpen())
    {
        return;
    }
    closeMenu();
    auto panel = std::make_unique<ui::SettingsPanel>(processor_.getSettings(),
                                                     [this] { return processor_.getDebugInfo(); });
    panel->onClose = [this] { closeModal(); };
    modal_ = std::move(panel);
    content_.addAndMakeVisible(*modal_);
    modal_->open();
}

bool DriftboxAudioProcessorEditor::isSettingsOpen() const
{
    return dynamic_cast<ui::SettingsPanel*>(modal_.get()) != nullptr && modal_->isVisible();
}

ui::SettingsPanel* DriftboxAudioProcessorEditor::getSettingsPanel() noexcept
{
    return modal_ != nullptr && modal_->isVisible() ? dynamic_cast<ui::SettingsPanel*>(modal_.get())
                                                    : nullptr;
}

ui::PresetDialog* DriftboxAudioProcessorEditor::getPresetDialog() noexcept
{
    return modal_ != nullptr && modal_->isVisible() ? dynamic_cast<ui::PresetDialog*>(modal_.get()) : nullptr;
}

ui::OverlayMenu* DriftboxAudioProcessorEditor::getMenu() noexcept
{
    return isMenuOpen() ? menu_.get() : nullptr;
}

void DriftboxAudioProcessorEditor::closeModal()
{
    if (modal_ == nullptr)
    {
        return;
    }
    modal_->setVisible(false);
    // Distrugge fuori dal gestore di eventi che ha chiesto la chiusura (un pulsante non si cancella dentro il
    // proprio clic); il confronto sul puntatore evita di distruggere una finestra più nuova.
    juce::Component::SafePointer<DriftboxAudioProcessorEditor> safe(this);
    auto* raw = modal_.get();
    juce::MessageManager::callAsync(
        [safe, raw]
        {
            if (safe != nullptr && safe->modal_.get() == raw)
            {
                safe->modal_.reset();
            }
        });
    grabKeyboardFocus(); // undo e redo da tastiera tornano all'editor
}

bool DriftboxAudioProcessorEditor::isMenuOpen() const
{
    return menu_ != nullptr && menu_->isVisible();
}

void DriftboxAudioProcessorEditor::closeMenu()
{
    if (menu_ == nullptr)
    {
        return;
    }
    menu_->setVisible(false);
    juce::Component::SafePointer<DriftboxAudioProcessorEditor> safe(this);
    auto* raw = menu_.get();
    juce::MessageManager::callAsync(
        [safe, raw]
        {
            if (safe != nullptr && safe->menu_.get() == raw)
            {
                safe->menu_.reset();
            }
        });
    grabKeyboardFocus();
}

juce::Point<int> DriftboxAudioProcessorEditor::menuAnchor(juce::Component& button) const
{
    // Sotto il pulsante, 4 px di distanza, nel sistema di coordinate del layout (come nel mockup).
    const auto r = content_.getLocalArea(&button, button.getLocalBounds());
    return {r.getX(), r.getBottom() + 4};
}

void DriftboxAudioProcessorEditor::showMenu(std::unique_ptr<ui::OverlayMenu> menu)
{
    if (modal_ != nullptr && modal_->isVisible())
    {
        return;
    }
    menu_ = std::move(menu);
    content_.addAndMakeVisible(*menu_);
    menu_->open();
}

void DriftboxAudioProcessorEditor::openPresetList()
{
    if (isMenuOpen())
    {
        return;
    }
    auto& presets = processor_.getPresetManager();
    presets.rescan(); // vede i file aggiunti o tolti da fuori
    const auto current = presets.getCurrentName();
    // Nessun preset caricato: la barra mostra "Default", che non è in elenco, quindi nessuna spunta.
    const auto shown = current.isNotEmpty() ? current : presets.getDisplayName();

    using Item = ui::OverlayMenu::Item;
    std::vector<Item> items;
    auto add = [&](const PresetEntry& e)
    {
        Item it;
        it.text = e.name;
        it.checked = e.name.equalsIgnoreCase(shown);
        it.action = [this, entry = e]
        {
            processor_.getPresetManager().loadPreset(entry);
            refreshNow();
        };
        items.push_back(std::move(it));
    };
    items.push_back({Item::Kind::header, "Factory", true, false, {}});
    for (const auto& e : presets.getFactoryPresets())
    {
        add(e);
    }
    items.push_back({Item::Kind::header, "User", true, false, {}});
    if (presets.getUserPresets().empty())
    {
        items.push_back({Item::Kind::note, "No user presets yet", true, false, {}});
    }
    for (const auto& e : presets.getUserPresets())
    {
        add(e);
    }

    const auto at = menuAnchor(topBar_.presetBar().nameButton());
    showMenu(
        std::make_unique<ui::OverlayMenu>("Presets", std::move(items), at, 200, [this] { closeMenu(); }));
}

void DriftboxAudioProcessorEditor::openPresetActions()
{
    if (isMenuOpen())
    {
        return;
    }
    auto& presets = processor_.getPresetManager();
    presets.rescan();
    const bool user = presets.isCurrentUser(); // Save e Delete solo per i preset utente

    using Item = ui::OverlayMenu::Item;
    std::vector<Item> items;
    items.push_back({Item::Kind::item, "Save", user, false, [this]
                     {
                         const auto r = processor_.getPresetManager().save();
                         if (r.failed())
                         {
                             footer_.setHint("Could not save the preset: " + r.getErrorMessage());
                         }
                         refreshNow();
                     }});
    items.push_back({Item::Kind::item, juce::String(juce::CharPointer_UTF8("Save As\xe2\x80\xa6")), true,
                     false, [this] { openPresetDialog(ui::PresetDialog::Mode::saveAs); }});
    items.push_back({Item::Kind::item, "Delete", user, false,
                     [this] { openPresetDialog(ui::PresetDialog::Mode::confirmDelete); }});

    auto at = menuAnchor(topBar_.presetBar().actionsButton());
    constexpr int width = 150;
    at.x = juce::jmin(at.x, kBaseWidth - width - 8);
    showMenu(std::make_unique<ui::OverlayMenu>("Preset actions", std::move(items), at, width,
                                               [this] { closeMenu(); }));
}

void DriftboxAudioProcessorEditor::openPresetDialog(ui::PresetDialog::Mode mode)
{
    if (modal_ != nullptr && modal_->isVisible())
    {
        return;
    }
    auto dialog =
        std::make_unique<ui::PresetDialog>(processor_.getPresetManager(), mode, [this] { refreshNow(); });
    dialog->onClose = [this] { closeModal(); };
    modal_ = std::move(dialog);
    content_.addAndMakeVisible(*modal_);
    modal_->open();
    if (auto* d = getPresetDialog(); d != nullptr && mode == ui::PresetDialog::Mode::saveAs)
    {
        d->nameEditor().grabKeyboardFocus();
    }
}

bool DriftboxAudioProcessorEditor::undo()
{
    const bool done = processor_.getUndoManager().undo();
    refreshNow();
    return done;
}

bool DriftboxAudioProcessorEditor::redo()
{
    const bool done = processor_.getUndoManager().redo();
    refreshNow();
    return done;
}

bool DriftboxAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    // Mai mentre un campo di testo ha il focus (valore digitato): ha la sua modifica del testo.
    if (dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr)
    {
        return false;
    }

    const auto mods = key.getModifiers();
    if (mods.isAltDown() || !(mods.isCommandDown() || mods.isCtrlDown()))
    {
        return false;
    }

    const int code = key.getKeyCode();
    const int letter =
        code < 128
            ? static_cast<int>(juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(code)))
            : code;
    if (letter == 'Z')
    {
        mods.isShiftDown() ? redo() : undo();
        return true;
    }
    if (letter == 'Y' && !mods.isShiftDown())
    {
        redo();
        return true;
    }
    return false;
}

void DriftboxAudioProcessorEditor::updateHint(const juce::MouseEvent& e)
{
    juce::String hint;
    for (auto* c = e.eventComponent; c != nullptr && c != this; c = c->getParentComponent())
    {
        if (auto* tc = dynamic_cast<juce::TooltipClient*>(c))
        {
            hint = tc->getTooltip();
            if (hint.isNotEmpty())
            {
                break;
            }
        }
    }
    footer_.setHint(hint);
}

void DriftboxAudioProcessorEditor::mouseEnter(const juce::MouseEvent& e)
{
    updateHint(e);
}

void DriftboxAudioProcessorEditor::mouseExit(const juce::MouseEvent& e)
{
    if (e.eventComponent == this)
    {
        footer_.setHint({});
    }
}

void DriftboxAudioProcessorEditor::layoutContent()
{
    constexpr int w = kBaseWidth, h = kBaseHeight;
    topBar_.setBounds(0, 0, w, kHeaderH);
    footer_.setBounds(0, h - kFooterH, w, kFooterH);

    const juce::Rectangle<int> main(kPad, kHeaderH + kPad, w - 2 * kPad, h - kHeaderH - kFooterH - 2 * kPad);
    auto top = main.withHeight(kTopRowH);
    auto bottom = main.withTrimmedTop(kTopRowH + kGap);

    channelPanel_.setBounds(top.removeFromLeft(kSidePanelW));
    top.removeFromLeft(kGap);
    tolerancePanel_.setBounds(top);

    driftPanel_.setBounds(bottom.removeFromLeft(kSidePanelW));
    bottom.removeFromLeft(kGap);
    outputPanel_.setBounds(bottom.removeFromRight(kOutputPanelW));
    bottom.removeFromRight(kGap);
    advancedPanel_.setBounds(bottom);
}

void DriftboxAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / static_cast<float>(kBaseWidth);
    content_.setBounds(0, 0, kBaseWidth, kBaseHeight);
    content_.setTransform(juce::AffineTransform::scale(s));
    processor_.setUiSize(getWidth(), getHeight());
}

void DriftboxAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(col::bg0);
}

void DriftboxAudioProcessorEditor::timerCallback()
{
    refreshNow();
}

void DriftboxAudioProcessorEditor::refreshNow()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = juce::jlimit(0.001f, 0.25f, static_cast<float>((now - lastTickMs_) * 0.001));
    lastTickMs_ = now;

    auto& apvts = processor_.getAPVTS();

    // Curve e valori: si ricalcolano e ridisegnano solo se un ingresso è cambiato (flag di "sporco" interno).
    TolerancePanel::Inputs in;
    in.channelL = juce::jlimit(1, kNumConsoleChannels, juce::roundToInt(rawValue(apvts, pid::chanL)));
    in.channelR = juce::jlimit(1, kNumConsoleChannels, juce::roundToInt(rawValue(apvts, pid::chanR)));
    in.drift = rawValue(apvts, pid::drift);
    in.freqTol = rawValue(apvts, pid::freqTol);
    in.phaseTol = rawValue(apvts, pid::phaseTol);
    in.gainTol = rawValue(apvts, pid::gainTol);
    in.saturation = rawValue(apvts, pid::saturation);
    in.sampleRate = processor_.getSampleRate() > 0.0 ? processor_.getSampleRate() : 48000.0;
    in.bypassed = rawValue(apvts, pid::bypass) > 0.5f;
    tolerancePanel_.setInputs(in);

    auto& meters = processor_.getMeters();
    outputPanel_.meter().update(meters.outPeakDbL.load(std::memory_order_relaxed),
                                meters.outPeakDbR.load(std::memory_order_relaxed), dt);

    auto& um = processor_.getUndoManager();
    topBar_.setHistoryState(um.canUndo(), um.canRedo());

    auto& presets = processor_.getPresetManager();
    topBar_.presetBar().setPresetName(presets.getDisplayName());
    topBar_.presetBar().setModified(presets.isModified());

    footer_.setShowCpu(showCpu);
    footer_.setCpu(meters.cpuLoad.load(std::memory_order_relaxed));
}
} // namespace driftbox
