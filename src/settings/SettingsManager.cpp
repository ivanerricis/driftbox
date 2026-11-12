// SPDX-License-Identifier: AGPL-3.0-or-later
#include "settings/SettingsManager.h"

#include "presets/PresetManager.h"

namespace driftbox
{
namespace
{
struct Overrides
{
    juce::File file;
    juce::File presetFolder;
};

Overrides& overrides()
{
    static Overrides o;
    return o;
}

constexpr const char* kZoom = "uiZoom";
constexpr const char* kTooltips = "tooltips";
constexpr const char* kShowCpu = "showCpu";
constexpr const char* kPresetFolder = "presetFolder";

int zoomOrDefault(int v)
{
    for (int a : UserSettings::kZoomSteps)
    {
        if (a == v)
        {
            return v;
        }
    }
    return UserSettings{}.zoomPercent;
}
} // namespace

juce::File SettingsManager::getDefaultFile()
{
    if (overrides().file != juce::File())
    {
        return overrides().file;
    }
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile("Application Support");
#endif
    return base.getChildFile("Driftbox").getChildFile("settings.settings");
}

void SettingsManager::setTestOverrides(const juce::File& file, const juce::File& defaultPresetFolder)
{
    overrides().file = file;
    overrides().presetFolder = defaultPresetFolder;
}

SettingsManager::SettingsManager()
{
    init(getDefaultFile());
}

SettingsManager::SettingsManager(const juce::File& file)
{
    init(file);
}

SettingsManager::~SettingsManager()
{
    if (props_ != nullptr)
    {
        props_->saveIfNeeded();
    }
}

void SettingsManager::init(const juce::File& file)
{
    file_ = file;
    lock_ = std::make_unique<juce::InterProcessLock>(
        "DriftboxSettings" + juce::String::toHexString(file.getFullPathName().hashCode64()));
    juce::PropertiesFile::Options o;
    o.applicationName = "Driftbox";
    o.filenameSuffix = ".settings";
    o.folderName = "Driftbox";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = -1; // salvato esplicitamente (saveIfNeeded) a ogni modifica
    o.processLock = lock_.get();
    props_ = std::make_unique<juce::PropertiesFile>(file, o);
    // Un file salvato ha sempre almeno una proprietà: uno non vuoto senza proprietà è illeggibile (corrotto).
    loadedOk_ = !file.existsAsFile() || file.getSize() == 0 || props_->getAllProperties().size() > 0;
    readFromProperties();
}

UserSettings SettingsManager::sanitised(UserSettings s)
{
    s.zoomPercent = zoomOrDefault(s.zoomPercent);
    s.presetFolder = s.presetFolder.trim();
    return s;
}

void SettingsManager::readFromProperties()
{
    UserSettings s;
    auto& p = *props_;
    s.zoomPercent = p.getIntValue(kZoom, s.zoomPercent);
    s.tooltips = p.getBoolValue(kTooltips, s.tooltips);
    s.showCpu = p.getBoolValue(kShowCpu, s.showCpu);
    s.presetFolder = p.getValue(kPresetFolder, {});
    data_ = sanitised(s);
}

void SettingsManager::writeToProperties()
{
    auto& p = *props_;
    p.setValue(kZoom, data_.zoomPercent);
    p.setValue(kTooltips, data_.tooltips);
    p.setValue(kShowCpu, data_.showCpu);
    p.setValue(kPresetFolder, data_.presetFolder);
    p.saveIfNeeded();
}

void SettingsManager::set(const UserSettings& s)
{
    data_ = sanitised(s);
    writeToProperties();
    sendSynchronousChangeMessage();
}

void SettingsManager::resetToDefaults()
{
    data_ = UserSettings{};
    props_->clear();
    writeToProperties();
    sendSynchronousChangeMessage();
}

juce::File SettingsManager::getEffectivePresetFolder() const
{
    return getEffectivePresetFolder(data_);
}

juce::File SettingsManager::getEffectivePresetFolder(const UserSettings& s)
{
    if (s.presetFolder.isNotEmpty())
    {
        return juce::File(s.presetFolder);
    }
    if (overrides().presetFolder != juce::File())
    {
        return overrides().presetFolder;
    }
    return PresetManager::getDefaultUserFolder();
}
} // namespace driftbox
