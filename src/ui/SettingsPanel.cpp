// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/SettingsPanel.h"

#include "PluginInfo.h"
#include "update/UpdateChecker.h"

namespace driftbox::ui
{
namespace
{
juce::String ellipsisText(const char* utf8WithEllipsis)
{
    return juce::String(juce::CharPointer_UTF8(utf8WithEllipsis));
}

// Percorso come lo mostra il mockup: %APPDATA%\Driftbox\Presets su Windows, ~ al posto della cartella
// personale altrove.
juce::String friendlyPath(const juce::File& folder)
{
    auto path = folder.getFullPathName();
#if JUCE_WINDOWS
    const auto appData =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getFullPathName();
    if (appData.isNotEmpty() && path.startsWithIgnoreCase(appData))
    {
        return "%APPDATA%" + path.substring(appData.length());
    }
#else
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getFullPathName();
    if (home.isNotEmpty() && path.startsWith(home))
    {
        return "~" + path.substring(home.length());
    }
#endif
    return path;
}
} // namespace

// ============================== ToggleSwitch
ToggleSwitch::ToggleSwitch(const juce::String& name) : juce::Button(name)
{
    setTitle(name);
    setClickingTogglesState(true);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void ToggleSwitch::paintButton(juce::Graphics& g, bool highlighted, bool)
{
    const auto area = getLocalBounds().toFloat();
    const bool on = getToggleState();
    g.setColour(on ? col::accent : (highlighted ? col::bg3.brighter(0.1f) : col::bg3));
    g.fillRoundedRectangle(area, area.getHeight() * 0.5f);
    const float d = area.getHeight() - 4.0f;
    const float x = on ? area.getRight() - 2.0f - d : area.getX() + 2.0f;
    g.setColour(on ? col::bg0 : col::ink3);
    g.fillEllipse(x, area.getY() + 2.0f, d, d);
    if (hasKeyboardFocus(false))
    {
        g.setColour(col::ink2);
        g.drawRoundedRectangle(area.reduced(0.5f), area.getHeight() * 0.5f, 1.0f);
    }
}

// ============================== LinkButton
LinkButton::LinkButton(const juce::String& text, const juce::String& url) : juce::Button(text), url_(url)
{
    setTitle(text);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void LinkButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto area = getLocalBounds().toFloat();
    const auto font = fonts::sans(11.0f);
    g.setColour(highlighted || down ? col::accent.brighter(0.2f) : col::accent);
    g.setFont(font);
    g.drawText(getButtonText(), area, juce::Justification::centredRight, false);
    if (highlighted || down || hasKeyboardFocus(false))
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText(font, getButtonText(), 0.0f, 0.0f);
        const float w = ga.getBoundingBox(0, -1, true).getWidth();
        g.fillRect(area.getRight() - w, area.getCentreY() + 7.0f, w, 1.0f);
    }
}

// ============================== UpdatesSection
UpdatesSection::UpdatesSection()
{
    setTitle("Updates");
    for (juce::Component* child :
         {static_cast<juce::Component*>(&toggle_), static_cast<juce::Component*>(&checkNow_),
          static_cast<juce::Component*>(&download_)})
    {
        addAndMakeVisible(*child);
    }
    checkNow_.setLook(11.0f, 0.0f, kButtonRadius);
    checkNow_.setColours(col::bg3, col::ink, col::bg3, col::ink);
    toggle_.setTooltip("Look for a newer release once a day, pre-releases included");
    download_.setVisible(false);
}

void UpdatesSection::setState(const juce::String& status, bool autoCheck, const juce::String& downloadUrl,
                              bool enabled, bool busy)
{
    status_ = status;
    toggle_.setToggleState(autoCheck, juce::dontSendNotification);
    toggle_.setEnabled(enabled);
    checkNow_.setEnabled(enabled && !busy);
    toggle_.setAlpha(enabled ? 1.0f : 0.4f); // su Linux i controlli sono spenti
    checkNow_.setAlpha(enabled && !busy ? 1.0f : 0.5f);
    download_.setUrl(downloadUrl);
    download_.setTooltip(downloadUrl);
    download_.setVisible(downloadUrl.isNotEmpty());
    resized();
    repaint();
}

void UpdatesSection::resized()
{
    auto r = getLocalBounds();
    auto centred = [&r](int w, int h, int gapRight)
    {
        const juce::Rectangle<int> b(r.getRight() - w, r.getCentreY() - h / 2, w, h);
        r.removeFromRight(w + gapRight);
        return b;
    };
    r.removeFromRight(10);
    toggle_.setBounds(centred(38, 22, 8));
    checkNow_.setBounds(centred(78, 24, 8));
    download_.setBounds(centred(60, 20, 0));
}

void UpdatesSection::paint(juce::Graphics& g)
{
    // Il fondo della riga lo disegna SettingsPanel; qui solo i due testi (come drawRow del pannello).
    int reserved = 10 + 38 + 8 + 78 + 8 + 10;
    if (download_.isVisible())
    {
        reserved += 60 + 8;
    }
    auto text = getLocalBounds().withTrimmedLeft(10).withTrimmedRight(reserved);
    g.setColour(col::ink);
    g.setFont(fonts::sans(12.0f));
    g.drawText("Check for updates", text.removeFromTop(getHeight() / 2 + 1).withTrimmedTop(6),
               juce::Justification::centredLeft, true);
    g.setColour(col::ink2);
    g.setFont(fonts::sans(10.5f));
    g.drawText(status_, text.withTrimmedBottom(6), juce::Justification::centredLeft, true);
}

// ============================== ZoomSelector
ZoomSelector::ZoomSelector()
{
    setTitle("Zoom");
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<int> ZoomSelector::segment(int index) const
{
    return {kPad + index * (kSegmentW + kGap), kPad, kSegmentW, kSegmentH};
}

int ZoomSelector::indexAt(juce::Point<int> p) const
{
    for (int i = 0; i < 5; ++i)
    {
        if (segment(i).contains(p))
        {
            return i;
        }
    }
    return -1;
}

void ZoomSelector::setPercent(int percent)
{
    if (percent != percent_)
    {
        percent_ = percent;
        repaint();
    }
}

void ZoomSelector::select(int index)
{
    if (index >= 0 && index < 5)
    {
        setPercent(UserSettings::kZoomSteps[index]);
        if (onSelect)
        {
            onSelect(UserSettings::kZoomSteps[index]);
        }
    }
}

void ZoomSelector::mouseDown(const juce::MouseEvent& e)
{
    select(indexAt(e.getPosition()));
}

void ZoomSelector::paint(juce::Graphics& g)
{
    g.setColour(col::bg0);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), kButtonRadius);
    for (int i = 0; i < 5; ++i)
    {
        const auto r = segment(i).toFloat();
        const bool on = UserSettings::kZoomSteps[i] == percent_;
        if (on)
        {
            g.setColour(col::ink);
            g.fillRoundedRectangle(r, 3.0f);
        }
        g.setColour(on ? col::bg0 : col::ink2);
        g.setFont(fonts::mono(10.5f));
        g.drawText(juce::String(UserSettings::kZoomSteps[i]) + "%", r, juce::Justification::centred, false);
    }
}

// ============================== SettingsPanel
SettingsPanel::SettingsPanel(SettingsManager& settings, std::function<juce::String()> debugInfo)
    : copyToClipboard([](const juce::String& t) { juce::SystemClipboard::copyTextToClipboard(t); }),
      settings_(settings), debugInfo_(std::move(debugInfo)), source_("Source code", kSourceUrl),
      changeFolder_(ellipsisText("Change\xe2\x80\xa6"), "Choose the folder for your presets")
{
    setTitle("Settings");

    for (juce::Component* c :
         {static_cast<juce::Component*>(&close_), static_cast<juce::Component*>(&zoom_),
          static_cast<juce::Component*>(&tooltips_), static_cast<juce::Component*>(&cpu_),
          static_cast<juce::Component*>(&changeFolder_), static_cast<juce::Component*>(&updatesSection_),
          static_cast<juce::Component*>(&source_), static_cast<juce::Component*>(&copyDebug_),
          static_cast<juce::Component*>(&reset_), static_cast<juce::Component*>(&done_)})
    {
        addAndMakeVisible(*c);
    }

    for (auto* b : {&changeFolder_, &copyDebug_, &reset_})
    {
        b->setLook(11.0f, 0.0f, kButtonRadius);
        b->setColours(col::bg3, col::ink, col::bg3, col::ink);
    }
    done_.setLook(11.0f, 0.06f, kButtonRadius);
    done_.setColours(col::ink, col::bg0, col::ink, col::bg0);
    done_.setToggleState(true, juce::dontSendNotification); // sempre "acceso": sfondo chiaro del mockup

    source_.setTooltip(kSourceUrl);
    source_.onClick = [] { juce::URL(juce::String(kSourceUrl)).launchInDefaultBrowser(); };

    tooltips_.setTooltip("Show short help when hovering a control");
    cpu_.setTooltip("Show the CPU load in the footer");

    close_.onClick = [this] { requestClose(); };
    done_.onClick = [this] { requestClose(); };
    zoom_.onSelect = [this](int percent)
    { applyChange([percent](UserSettings& s) { s.zoomPercent = percent; }); };
    tooltips_.onClick = [this]
    { applyChange([this](UserSettings& s) { s.tooltips = tooltips_.getToggleState(); }); };
    cpu_.onClick = [this] { applyChange([this](UserSettings& s) { s.showCpu = cpu_.getToggleState(); }); };
    changeFolder_.onClick = [this] { chooseFolder(); };
    updatesSection_.toggle().onClick = [this]
    {
        const bool on = updatesSection_.toggle().getToggleState();
        applyChange([on](UserSettings& s) { s.checkUpdates = on; });
        if (on)
        {
            settings_.checkForUpdatesIfDue();
        }
    };
    updatesSection_.checkNowButton().onClick = [this]
    {
        settings_.checkForUpdatesNow();
        syncUpdates();
    };
    updatesSection_.downloadLink().onClick = [this]
    {
        const auto url = updatesSection_.downloadLink().url();
        if (url.isNotEmpty())
        {
            juce::URL(url).launchInDefaultBrowser(); // niente HyperlinkButton::setURL (errore di UBSan in CI)
        }
    };
    reset_.onClick = [this] { settings_.resetToDefaults(); };
    copyDebug_.onClick = [this]
    {
        if (copyToClipboard)
        {
            copyToClipboard(debugInfo_ ? debugInfo_() : juce::String());
        }
        copyDebug_.setLabel("Copied");
        startTimer(1500);
    };

    syncFromSettings();
    settings_.addChangeListener(this);
    settings_.updateChanges().addChangeListener(this);
}

SettingsPanel::~SettingsPanel()
{
    settings_.removeChangeListener(this);
    settings_.updateChanges().removeChangeListener(this);
    stopTimer();
}

juce::String SettingsPanel::versionText() const
{
    return juce::String(info::kName) + " " + info::kVersion;
}

juce::String SettingsPanel::folderText() const
{
    return friendlyPath(settings_.getEffectivePresetFolder());
}

void SettingsPanel::timerCallback()
{
    stopTimer();
    copyDebug_.setLabel("Copy debug info");
}

void SettingsPanel::applyChange(const std::function<void(UserSettings&)>& edit)
{
    auto s = settings_.get();
    edit(s);
    settings_.set(s); // notifica (anche noi: syncFromSettings riallinea i controlli)
}

void SettingsPanel::changeListenerCallback(juce::ChangeBroadcaster*)
{
    syncFromSettings();
}

void SettingsPanel::syncFromSettings()
{
    const auto& s = settings_.get();
    zoom_.setPercent(s.zoomPercent);
    tooltips_.setToggleState(s.tooltips, juce::dontSendNotification);
    cpu_.setToggleState(s.showCpu, juce::dontSendNotification);
    syncUpdates();
    repaint();
}

void SettingsPanel::syncUpdates()
{
    using Phase = SettingsManager::UpdateState::Phase;
    using Status = update::UpdateResult::Status;
    const auto& s = settings_.get();
    const auto& us = settings_.getUpdateState();
    const bool available = settings_.isUpdateCheckAvailable();

    juce::String status;
    juce::String url;
    if (!available)
    {
        status = "Not available on Linux";
    }
    else if (us.phase == Phase::checking)
    {
        status = ellipsisText("Checking\xe2\x80\xa6");
    }
    else if (us.phase == Phase::done && us.result.status == Status::updateAvailable)
    {
        status = "Version " + us.result.version + " available";
        url = us.result.url;
    }
    else if (us.phase == Phase::done && us.result.status == Status::upToDate)
    {
        status = juce::String(ellipsisText("Up to date \xc2\xb7 pre-releases included"));
    }
    else if (us.phase == Phase::done)
    {
        status = "Couldn't check";
    }
    else if (s.checkUpdates)
    {
        status = juce::String(ellipsisText("Once a day \xc2\xb7 pre-releases included"));
    }
    else
    {
        status = juce::String(ellipsisText("Off \xc2\xb7 nothing is sent unless you check"));
    }
    updatesSection_.setState(status, s.checkUpdates, url, available, us.phase == Phase::checking);
}

void SettingsPanel::chooseFolder()
{
    // FileChooser asincrono (mai modale in un plugin) e SOLO dal message thread. Il chooser resta vivo finché
    // la finestra esiste; il callback non fa nulla se la finestra è stata chiusa nel frattempo.
    chooser_ = std::make_unique<juce::FileChooser>("Choose the user preset folder",
                                                   settings_.getEffectivePresetFolder(), juce::String());
    juce::Component::SafePointer<SettingsPanel> safe(this);
    chooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe](const juce::FileChooser& fc)
        {
            const auto result = fc.getResult();
            if (safe == nullptr || result == juce::File())
            {
                return;
            }
            safe->applyChange([&result](UserSettings& s) { s.presetFolder = result.getFullPathName(); });
        });
}

int SettingsPanel::cardHeight() const
{
    const int rows = 6; // zoom, suggerimenti, CPU, cartella, aggiornamenti, informazioni
    const int h = kPadY + kHeaderH + kRowGap + rows * kRowH + (rows - 1) * kRowGap;
    return h + kRowGap + 26 + kPadY;
}

void SettingsPanel::updateLayout()
{
    resized();
    repaint();
}

void SettingsPanel::resized()
{
    setCard(kCardW, cardHeight());
    auto inner = card().reduced(kPadX, kPadY);

    auto header = inner.removeFromTop(kHeaderH);
    close_.setBounds(header.removeFromRight(26));
    inner.removeFromTop(kRowGap);

    auto nextRow = [&inner]
    {
        auto r = inner.removeFromTop(kRowH);
        inner.removeFromTop(kRowGap);
        return r;
    };
    auto centred = [](juce::Rectangle<int> row, int w, int h)
    { return juce::Rectangle<int>(row.getRight() - 10 - w, row.getCentreY() - h / 2, w, h); };

    zoomRow_ = nextRow();
    zoom_.setBounds(centred(zoomRow_, ZoomSelector::kWidth, ZoomSelector::kHeight));
    tipsRow_ = nextRow();
    tooltips_.setBounds(centred(tipsRow_, 38, 22));
    cpuRow_ = nextRow();
    cpu_.setBounds(centred(cpuRow_, 38, 22));
    folderRow_ = nextRow();
    changeFolder_.setBounds(centred(folderRow_, 78, 24));
    updatesRow_ = nextRow();
    updatesSection_.setBounds(updatesRow_);
    aboutRow_ = nextRow();
    source_.setBounds(centred(aboutRow_, 80, 20));

    auto buttons = inner.removeFromTop(26);
    done_.setBounds(buttons.removeFromRight(64));
    copyDebug_.setBounds(buttons.removeFromLeft(112));
    buttons.removeFromLeft(6);
    reset_.setBounds(buttons.removeFromLeft(100));
}

void SettingsPanel::paint(juce::Graphics& g)
{
    paintCard(g);

    const auto title = card().reduced(kPadX, kPadY).removeFromTop(kHeaderH);
    g.setColour(col::ink);
    g.setFont(fonts::sans(13.0f, fonts::Weight::semiBold, 0.04f));
    g.drawText("Settings", title, juce::Justification::centredLeft, false);

    auto drawRow = [&g](juce::Rectangle<int> row, const juce::String& label, const juce::String& desc,
                        bool monoDesc, int reservedRight)
    {
        g.setColour(col::bg2);
        g.fillRoundedRectangle(row.toFloat(), kButtonRadius);
        auto text = row.withTrimmedLeft(10).withTrimmedRight(reservedRight + 10);
        g.setColour(col::ink);
        g.setFont(fonts::sans(12.0f));
        g.drawText(label, text.removeFromTop(row.getHeight() / 2 + 1).withTrimmedTop(6),
                   juce::Justification::centredLeft, true);
        g.setColour(col::ink2);
        g.setFont(monoDesc ? fonts::mono(10.0f) : fonts::sans(10.5f));
        g.drawText(desc, text.withTrimmedBottom(6), juce::Justification::centredLeft, true);
    };

    drawRow(zoomRow_, "Zoom", "Window size, saved for every instance", false, ZoomSelector::kWidth);
    drawRow(tipsRow_, "Tooltips", "Short help when hovering a control", false, 38);
    drawRow(cpuRow_, "Show CPU", "CPU load of this instance in the footer", false, 38);
    drawRow(folderRow_, "User preset folder", folderText(), true, 78);
    // Sfondo della riga "Updates"; testi e controlli li disegna updatesSection().
    g.setColour(col::bg2);
    g.fillRoundedRectangle(updatesRow_.toFloat(), kButtonRadius);
    drawRow(aboutRow_, versionText(), "Open source, AGPL-3.0-or-later", false, 80);
}
} // namespace driftbox::ui
