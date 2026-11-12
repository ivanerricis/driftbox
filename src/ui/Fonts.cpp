// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/Fonts.h"

#include <DriftboxUiData.h>
#include <juce_events/juce_events.h>
#include <memory>

namespace driftbox::ui::fonts
{
namespace
{
struct Faces
{
    juce::Typeface::Ptr sans[3], mono[2];
    bool ok = false;

    Faces()
    {
        auto load = [](const char* data, int size)
        { return juce::Typeface::createSystemTypefaceFor(data, static_cast<size_t>(size)); };
        sans[0] = load(DriftboxUiData::IBMPlexSansRegular_ttf, DriftboxUiData::IBMPlexSansRegular_ttfSize);
        sans[1] = load(DriftboxUiData::IBMPlexSansMedium_ttf, DriftboxUiData::IBMPlexSansMedium_ttfSize);
        sans[2] = load(DriftboxUiData::IBMPlexSansSemiBold_ttf, DriftboxUiData::IBMPlexSansSemiBold_ttfSize);
        mono[0] = load(DriftboxUiData::IBMPlexMonoRegular_ttf, DriftboxUiData::IBMPlexMonoRegular_ttfSize);
        mono[1] = load(DriftboxUiData::IBMPlexMonoMedium_ttf, DriftboxUiData::IBMPlexMonoMedium_ttfSize);
        ok = true;
        for (auto& f : sans)
        {
            ok = ok && f != nullptr;
        }
        for (auto& f : mono)
        {
            ok = ok && f != nullptr;
        }
    }
};

// Possiede i caratteri. Viene distrutta da juce::DeletedAtShutdown quando JUCE è ancora vivo (una static
// sarebbe distrutta all'uscita del processo, dopo lo shutdown di JUCE, e i distruttori dei Typeface
// toccherebbero stato già distrutto). Si ricrea su richiesta se i font servono dopo uno shutdown.
class FontCache final : private juce::DeletedAtShutdown
{
public:
    // Il puntatore restituito mantiene vivi i caratteri anche se la cache viene eliminata nel frattempo.
    static std::shared_ptr<const Faces> get()
    {
        const juce::SpinLock::ScopedLockType lock(lockObject());
        if (instance == nullptr)
        {
            instance = new FontCache();
        }
        if (instance->faces == nullptr)
        {
            instance->faces = std::make_shared<const Faces>();
        }
        return instance->faces;
    }

private:
    FontCache() = default;

    ~FontCache() override
    {
        const juce::SpinLock::ScopedLockType lock(lockObject());
        if (instance == this)
        {
            instance = nullptr;
        }
    }

    // Variabile statica locale con distruttore banale, sicura durante lo smontaggio.
    static juce::SpinLock& lockObject()
    {
        static juce::SpinLock l;
        return l;
    }

    static inline FontCache* instance = nullptr;
    std::shared_ptr<const Faces> faces;
};

juce::Font make(const juce::Typeface::Ptr& face, const juce::String& fallbackFamily, bool bold, float px,
                float tracking)
{
    juce::FontOptions opts = face != nullptr
                                 ? juce::FontOptions(face)
                                 : juce::FontOptions(fallbackFamily, bold ? "Bold" : "Regular", 1.0f);
    opts = opts.withPointHeight(px);
    juce::Font f(opts);
    if (tracking != 0.0f)
    {
        f.setExtraKerningFactor(tracking);
    }
    return f;
}
} // namespace

bool embeddedFontsLoaded()
{
    return FontCache::get()->ok;
}

juce::Font sans(float px, Weight w, float tracking)
{
    const auto f = FontCache::get();
    return make(f->ok ? f->sans[static_cast<int>(w)] : nullptr, "Segoe UI", w == Weight::semiBold, px,
                tracking);
}

juce::Font mono(float px, Weight w, float tracking)
{
    const auto f = FontCache::get();
    return make(f->ok ? f->mono[w == Weight::regular ? 0 : 1] : nullptr, "Consolas", w == Weight::semiBold,
                px, tracking);
}
} // namespace driftbox::ui::fonts
