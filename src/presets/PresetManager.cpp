// SPDX-License-Identifier: AGPL-3.0-or-later
#include "presets/PresetManager.h"

#include "PluginProcessor.h"
#include <DriftboxPresetData.h>
#include <algorithm>
#include <cmath>

namespace driftbox
{
namespace
{
bool isReservedWindowsName(const juce::String& n)
{
    static const juce::StringArray reserved{"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
                                            "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                                            "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    return reserved.contains(n.upToFirstOccurrenceOf(".", false, false).trim(), true);
}

float clampValue(int index, float v)
{
    return index == kNumPresetParams - 1 ? juce::jlimit(kPresetOutGainMinDb, kPresetOutGainMaxDb, v)
                                         : juce::jlimit(0.0f, 1.0f, v);
}

// Tolleranza del confronto "modificato": più stretta di quello che la UI mostra (1 % e 0.1 dB).
bool sameValue(int index, float a, float b)
{
    return std::abs(a - b) <= (index == kNumPresetParams - 1 ? 0.05f : 5.0e-4f);
}

bool looksNumeric(const juce::String& text)
{
    const auto t = text.trim();
    return t.isNotEmpty() && t.containsAnyOf("0123456789") && t.containsOnly("0123456789+-.eE");
}
} // namespace

PresetManager::PresetManager(DriftboxAudioProcessor& processor)
    : processor_(processor), userFolder_(getDefaultUserFolder()), factory_(loadFactoryPresets())
{
}

juce::File PresetManager::getDefaultUserFolder()
{
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile("Application Support");
#endif
    return base.getChildFile("Driftbox").getChildFile("Presets");
}

juce::String PresetManager::sanitiseName(const juce::String& name)
{
    juce::String out;
    bool lastSpace = false;
    for (auto c : name)
    {
        if (c < 32 || juce::String("\\/:*?\"<>|").containsChar(c))
        {
            continue;
        }
        if (juce::CharacterFunctions::isWhitespace(c))
        {
            if (!lastSpace && out.isNotEmpty())
            {
                out << ' ';
            }
            lastSpace = true;
        }
        else
        {
            out << juce::String::charToString(c);
            lastSpace = false;
        }
    }
    out = out.substring(0, kMaxNameLength).trim();
    while (out.endsWithChar('.') || out.endsWithChar(' '))
    {
        out = out.dropLastCharacters(1);
    }
    if (out.isEmpty() || isReservedWindowsName(out))
    {
        return {};
    }
    return out;
}

juce::Result PresetManager::parsePreset(const juce::XmlElement& root, PresetValues& values,
                                        juce::String& name)
{
    if (!root.hasTagName(kPresetTag))
    {
        return juce::Result::fail("Not a Driftbox preset");
    }
    // Versione mancante = 1; una versione più recente del plugin non si carica (il formato potrebbe essere
    // cambiato).
    const int version = root.getIntAttribute("version", 1);
    if (version < 1 || version > kPresetVersion)
    {
        return juce::Result::fail("Unsupported preset version");
    }

    values = kPresetDefaults;
    for (auto* p : root.getChildWithTagNameIterator("PARAM"))
    {
        const auto id = p->getStringAttribute("id");
        for (int i = 0; i < kNumPresetParams; ++i)
        {
            // Solo i sette parametri dei preset: qualunque altro id (chanL, chanR, bypass...) è ignorato.
            if (id == kPresetParamIds[i])
            {
                const auto text = p->getStringAttribute("value");
                if (!looksNumeric(text))
                {
                    return juce::Result::fail("Invalid value for " + id);
                }
                const auto v = static_cast<float>(text.getDoubleValue());
                if (!std::isfinite(v))
                {
                    return juce::Result::fail("Invalid value for " + id);
                }
                values[static_cast<size_t>(i)] = clampValue(i, v);
            }
        }
    }
    name = root.getStringAttribute("name");
    return juce::Result::ok();
}

juce::Result PresetManager::parsePresetFile(const juce::File& file, PresetValues& values)
{
    constexpr juce::int64 kMaxFileBytes = 1048576;
    if (!file.existsAsFile() || file.getSize() > kMaxFileBytes)
    {
        return juce::Result::fail("Unreadable file");
    }
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
    {
        return juce::Result::fail("Not valid XML");
    }
    juce::String ignored;
    return parsePreset(*xml, values, ignored);
}

std::unique_ptr<juce::XmlElement> PresetManager::toXml(const PresetValues& values, const juce::String& name)
{
    auto root = std::make_unique<juce::XmlElement>(kPresetTag);
    root->setAttribute("version", kPresetVersion);
    root->setAttribute("name", name);
    for (int i = 0; i < kNumPresetParams; ++i)
    {
        auto* p = root->createNewChildElement("PARAM");
        p->setAttribute("id", kPresetParamIds[i]);
        p->setAttribute("value", juce::String(static_cast<double>(values[static_cast<size_t>(i)]), 4));
    }
    return root;
}

std::vector<PresetEntry> PresetManager::loadFactoryPresets()
{
    struct Item
    {
        juce::String fileName;
        PresetEntry entry;
    };
    std::vector<Item> items;
    for (int i = 0; i < DriftboxPresetData::namedResourceListSize; ++i)
    {
        const char* resName = DriftboxPresetData::namedResourceList[i];
        int size = 0;
        const char* data = DriftboxPresetData::getNamedResource(resName, size);
        const juce::String fileName = DriftboxPresetData::getNamedResourceOriginalFilename(resName);
        if (data == nullptr || !fileName.endsWithIgnoreCase(".xml"))
        {
            continue;
        }
        Item item;
        item.fileName = fileName;
        const auto xml = juce::XmlDocument::parse(juce::String::fromUTF8(data, size));
        if (xml == nullptr || parsePreset(*xml, item.entry.values, item.entry.name).failed() ||
            item.entry.name.isEmpty())
        {
            jassertfalse; // un preset incorporato rotto è un errore di build che i test intercettano
            continue;
        }
        item.entry.factory = true;
        items.push_back(std::move(item));
    }
    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b) { return a.fileName < b.fileName; });

    std::vector<PresetEntry> out;
    out.reserve(items.size());
    for (auto& it : items)
    {
        out.push_back(std::move(it.entry));
    }
    return out;
}

void PresetManager::rescan()
{
    user_.clear();
    skipped_.clear();
    if (!userFolder_.isDirectory())
    {
        return;
    }
    auto files = userFolder_.findChildFiles(juce::File::findFiles, false, juce::String("*") + kFileExtension);
    std::sort(
        files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
        { return a.getFileNameWithoutExtension().compareIgnoreCase(b.getFileNameWithoutExtension()) < 0; });
    for (const auto& f : files)
    {
        PresetEntry e;
        e.name = f.getFileNameWithoutExtension();
        e.file = f;
        // Un file con il nome di un preset di fabbrica sarebbe ambiguo (il nome corrente è una sola stringa).
        if (e.name.isEmpty() || isFactoryName(e.name) || parsePresetFile(f, e.values).failed())
        {
            skipped_.add(f.getFullPathName());
            continue;
        }
        user_.push_back(std::move(e));
    }
}

std::vector<PresetEntry> PresetManager::getOrderedPresets() const
{
    auto all = factory_;
    all.insert(all.end(), user_.begin(), user_.end());
    return all;
}

juce::String PresetManager::getCurrentName() const
{
    return processor_.getPresetName();
}

juce::String PresetManager::getDisplayName() const
{
    const auto n = getCurrentName();
    return n.isEmpty() ? juce::String("Default") : n;
}

const PresetEntry* PresetManager::findByName(const juce::String& name) const
{
    if (name.isEmpty())
    {
        return nullptr;
    }
    for (const auto* list : {&factory_, &user_})
    {
        for (const auto& e : *list)
        {
            if (e.name.equalsIgnoreCase(name))
            {
                return &e;
            }
        }
    }
    return nullptr;
}

const PresetEntry* PresetManager::findCurrent() const
{
    return findByName(getCurrentName());
}

bool PresetManager::isCurrentFactory() const
{
    const auto* e = findCurrent();
    return e != nullptr && e->factory;
}

bool PresetManager::isCurrentUser() const
{
    const auto* e = findCurrent();
    return e != nullptr && !e->factory;
}

PresetValues PresetManager::captureValues() const
{
    PresetValues v = kPresetDefaults;
    auto& apvts = processor_.getAPVTS();
    for (int i = 0; i < kNumPresetParams; ++i)
    {
        if (auto* raw = apvts.getRawParameterValue(kPresetParamIds[i]))
        {
            v[static_cast<size_t>(i)] = raw->load();
        }
    }
    return v;
}

bool PresetManager::isModified() const
{
    const auto* e = findCurrent();
    if (e == nullptr)
    {
        return false;
    }
    const auto now = captureValues();
    for (int i = 0; i < kNumPresetParams; ++i)
    {
        if (!sameValue(i, now[static_cast<size_t>(i)], e->values[static_cast<size_t>(i)]))
        {
            return true;
        }
    }
    return false;
}

juce::Result PresetManager::loadPreset(const PresetEntry& entry)
{
    if (entry.name.isEmpty())
    {
        return juce::Result::fail("No preset");
    }
    processor_.applyPresetValues(entry.values, entry.name);
    processor_.setPresetName(entry.name);
    return juce::Result::ok();
}

juce::Result PresetManager::step(int direction)
{
    rescan();
    const auto all = getOrderedPresets();
    if (all.empty())
    {
        return juce::Result::fail("No presets");
    }
    const int n = static_cast<int>(all.size());
    const auto current = getCurrentName();
    int index =
        -1; // nessun preset caricato ("Default") o nome non in libreria: avanti = primo, indietro = ultimo
    if (current.isNotEmpty())
    {
        for (int i = 0; i < n; ++i)
        {
            if (all[static_cast<size_t>(i)].name.equalsIgnoreCase(current))
            {
                index = i;
                break;
            }
        }
    }
    const int target = index < 0 ? (direction > 0 ? 0 : n - 1) : ((index + direction) % n + n) % n;
    return loadPreset(all[static_cast<size_t>(target)]);
}

juce::Result PresetManager::loadNext()
{
    return step(1);
}

juce::Result PresetManager::loadPrevious()
{
    return step(-1);
}

juce::File PresetManager::fileFor(const juce::String& name) const
{
    return userFolder_.getChildFile(name + kFileExtension);
}

juce::Result PresetManager::writeFile(const juce::File& dest, const juce::String& name,
                                      const PresetValues& v) const
{
    if (!userFolder_.createDirectory().wasOk())
    {
        return juce::Result::fail("Cannot create the preset folder");
    }
    const auto xml = toXml(v, name);
    juce::TemporaryFile tmp(dest);
    if (!xml->writeTo(tmp.getFile()) || !tmp.overwriteTargetFileWithTemporary())
    {
        return juce::Result::fail("Cannot write the preset file");
    }
    return juce::Result::ok();
}

bool PresetManager::isFactoryName(const juce::String& name) const
{
    return std::any_of(factory_.begin(), factory_.end(),
                       [&](const PresetEntry& e) { return e.name.equalsIgnoreCase(name.trim()); });
}

bool PresetManager::userPresetExists(const juce::String& name) const
{
    return std::any_of(user_.begin(), user_.end(),
                       [&](const PresetEntry& e) { return e.name.equalsIgnoreCase(name.trim()); });
}

juce::Result PresetManager::save()
{
    rescan();
    const auto* cur = findCurrent();
    if (cur == nullptr || cur->factory)
    {
        return juce::Result::fail("Only user presets can be saved");
    }
    const auto file = cur->file;
    const auto name = cur->name;
    auto r = writeFile(file, name, captureValues());
    if (r.wasOk())
    {
        rescan();
    }
    return r;
}

juce::Result PresetManager::saveAs(const juce::String& rawName, bool overwrite)
{
    const auto name = sanitiseName(rawName);
    if (name.isEmpty())
    {
        return juce::Result::fail("Invalid name");
    }
    if (isFactoryName(name))
    {
        return juce::Result::fail("This name is used by a factory preset");
    }
    rescan();
    // Esiste già un file con quel nome (valido o no): si sovrascrive solo con `overwrite`.
    const auto dest = fileFor(name);
    if ((userPresetExists(name) || dest.existsAsFile()) && !overwrite)
    {
        return juce::Result::fail("A preset with this name already exists");
    }
    auto r = writeFile(dest, name, captureValues());
    if (r.failed())
    {
        return r;
    }
    rescan();
    processor_.setPresetName(name);
    return juce::Result::ok();
}

juce::Result PresetManager::deleteCurrent()
{
    rescan();
    const auto* cur = findCurrent();
    if (cur == nullptr || cur->factory)
    {
        return juce::Result::fail("Only user presets can be deleted");
    }
    if (!cur->file.deleteFile())
    {
        return juce::Result::fail("Cannot delete the preset file");
    }
    rescan();
    // Come in Pentagrit: dopo l'eliminazione si carica Init (il primo preset di fabbrica).
    if (!factory_.empty())
    {
        return loadPreset(factory_.front());
    }
    processor_.setPresetName({});
    return juce::Result::ok();
}
} // namespace driftbox
