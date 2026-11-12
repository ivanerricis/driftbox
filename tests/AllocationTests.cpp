// SPDX-License-Identifier: AGPL-3.0-or-later
// Conta le allocazioni sull'heap fatte dal thread audio (di test) tra la costruzione e la distruzione di
// ScopedCounter. Sostituisce l'operator new globale per tutto l'eseguibile dei test.
#include "PluginProcessor.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <new>
#include <random>
#include <utility>

namespace
{
thread_local bool g_counting = false;
thread_local std::size_t g_allocations = 0;

struct ScopedCounter
{
    ScopedCounter()
    {
        g_allocations = 0;
        g_counting = true;
    }
    ~ScopedCounter() { g_counting = false; }
    static std::size_t count() { return g_allocations; }
};

void* countedAlloc(std::size_t size)
{
    if (g_counting)
        ++g_allocations;
    if (void* p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* countedAlignedAlloc(std::size_t size, std::size_t align)
{
    if (g_counting)
        ++g_allocations;
#if defined(_WIN32)
    if (void* p = _aligned_malloc(size == 0 ? 1 : size, align))
        return p;
#else
    void* p = nullptr;
    if (posix_memalign(&p, std::max(align, sizeof(void*)), size == 0 ? 1 : size) == 0)
        return p;
#endif
    throw std::bad_alloc();
}

void countedAlignedFree(void* p)
{
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
} // namespace

void* operator new(std::size_t size)
{
    return countedAlloc(size);
}
void* operator new[](std::size_t size)
{
    return countedAlloc(size);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return countedAlloc(size);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return countedAlloc(size);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new(std::size_t size, std::align_val_t align)
{
    return countedAlignedAlloc(size, static_cast<std::size_t>(align));
}
void* operator new[](std::size_t size, std::align_val_t align)
{
    return countedAlignedAlloc(size, static_cast<std::size_t>(align));
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::align_val_t) noexcept
{
    countedAlignedFree(p);
}
void operator delete[](void* p, std::align_val_t) noexcept
{
    countedAlignedFree(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
    countedAlignedFree(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept
{
    countedAlignedFree(p);
}

using namespace driftbox;

namespace
{
void setParam(DriftboxAudioProcessor& p, const char* id, float normalised)
{
    p.getAPVTS().getParameter(id)->setValueNotifyingHost(normalised);
}

// Cambia tutti i parametri (come farebbe l'automazione), escluso bypass e i canali L/R se richiesto.
void randomiseParams(DriftboxAudioProcessor& p, std::mt19937& gen, bool includeChannels)
{
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (auto* param : p.getParameters())
    {
        auto* withID = dynamic_cast<juce::HostedAudioProcessorParameter*>(param);
        if (withID == nullptr)
            continue;
        const auto id = withID->getParameterID();
        if (id == pid::bypass || (!includeChannels && (id == pid::chanL || id == pid::chanR)))
            continue;
        param->setValueNotifyingHost(u(gen));
    }
}

// Valore normalizzato del parametro intero 1..64.
float normalisedChannel(int channel)
{
    return static_cast<float>(channel - 1) / static_cast<float>(kNumConsoleChannels - 1);
}

constexpr int kMaxBlock = 4096;

// Processore preparato con buffer stereo di kMaxBlock campioni; fill() riempie `n` campioni di rumore.
struct Rig
{
    DriftboxAudioProcessor proc;
    juce::AudioBuffer<float> storage{2, kMaxBlock};
    juce::MidiBuffer midi;
    std::mt19937 gen{42};

    explicit Rig(int maxBlock = kMaxBlock)
    {
        proc.setPlayConfigDetails(2, 2, 48000.0, maxBlock);
        proc.prepareToPlay(48000.0, maxBlock);
    }

    // Vista di `n` campioni sul buffer di lavoro (nessuna allocazione).
    juce::AudioBuffer<float> view(int n)
    {
        float* chans[2] = {storage.getWritePointer(0), storage.getWritePointer(1)};
        return juce::AudioBuffer<float>(chans, 2, n);
    }

    void fill(int n)
    {
        std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                storage.setSample(c, i, noise(gen));
    }

    // Elabora un blocco da `n` campioni e restituisce le allocazioni contate dentro processBlock.
    std::size_t processCounted(int n)
    {
        fill(n);
        auto buf = view(n);
        std::size_t seen = 0;
        {
            ScopedCounter counter;
            proc.processBlock(buf, midi);
            seen = ScopedCounter::count();
        }
        return seen;
    }
};
} // namespace

TEST_CASE("The allocation counter sees heap allocations", "[allocation]")
{
    std::size_t seen = 0;
    {
        ScopedCounter counter;
        // Si passa dall'operator con la dimensione e si pubblica il puntatore, così il compilatore non può
        // eliminare l'allocazione (clang su macOS rimuove un semplice `new int(3)` il cui risultato viene
        // solo cancellato).
        static void* volatile sink;
        void* p = ::operator new(sizeof(int));
        sink = p;
        seen = ScopedCounter::count();
        ::operator delete(sink);
    }
    CHECK(seen >= 1);
}

TEST_CASE("processBlock does not allocate at steady state", "[allocation]")
{
    for (const auto& [l, r] : {std::pair{1, 2}, std::pair{17, 17}, std::pair{64, 1}})
    {
        DYNAMIC_SECTION("channels " << l << " / " << r)
        {
            Rig rig(512);
            setParam(rig.proc, pid::chanL, normalisedChannel(l));
            setParam(rig.proc, pid::chanR, normalisedChannel(r));
            setParam(rig.proc, pid::noise, 1.0f);
            setParam(rig.proc, pid::saturation, 0.5f);
            for (int i = 0; i < 20; ++i) // riscaldamento
                rig.processCounted(512);

            std::size_t total = 0;
            for (int i = 0; i < 200; ++i)
                total += rig.processCounted(512);
            CHECK(total == 0);
        }
    }
}

TEST_CASE("Changing the channels does not allocate", "[allocation]")
{
    Rig rig(512);
    for (int i = 0; i < 10; ++i)
        rig.processCounted(512);

    std::size_t total = 0;
    for (int i = 0; i < 120; ++i)
    {
        // Il parametro cambia fuori dalla sezione misurata, come l'host; il fade si estende su più blocchi
        // e a volte un nuovo cambio arriva a fade in corso.
        if (i % 4 == 0)
        {
            setParam(rig.proc, pid::chanL, normalisedChannel(1 + (i * 7) % 64));
            setParam(rig.proc, pid::chanR, normalisedChannel(1 + (i * 13 + 5) % 64));
        }
        total += rig.processCounted(512);
    }
    CHECK(total == 0);
}

TEST_CASE("Randomize and its crossfade do not allocate", "[allocation]")
{
    Rig rig(512);
    setParam(rig.proc, pid::noise, 1.0f);
    for (int i = 0; i < 10; ++i)
        rig.processCounted(512);

    std::size_t total = 0;
    for (int i = 0; i < 120; ++i)
    {
        if (i % 10 == 0)
            rig.proc.randomize(); // FUORI dalla sezione misurata (alloca le azioni di undo)
        total += rig.processCounted(512);
    }
    CHECK(total == 0);
}

TEST_CASE("Restoring a state, then processing, does not allocate", "[allocation]")
{
    Rig rig(512);
    juce::MemoryBlock block;
    rig.proc.getStateInformation(block);
    for (int i = 0; i < 10; ++i)
        rig.processCounted(512);

    std::size_t total = 0;
    for (int i = 0; i < 20; ++i)
    {
        if (i % 5 == 0)
        {
            setParam(rig.proc, pid::chanL,
                     normalisedChannel(10 + i)); // il ripristino cambia davvero i canali
            rig.proc.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        }
        total += rig.processCounted(512);
    }
    CHECK(total == 0);
}

TEST_CASE("Bypass on and off does not allocate", "[allocation]")
{
    Rig rig(512);
    for (int i = 0; i < 10; ++i)
        rig.processCounted(512);

    std::size_t total = 0;
    for (int i = 0; i < 40; ++i)
    {
        setParam(rig.proc, pid::bypass, (i / 4) % 2 == 0 ? 1.0f : 0.0f);
        total += rig.processCounted(512);
    }
    CHECK(total == 0);
}

TEST_CASE("Changing parameters does not allocate", "[allocation]")
{
    Rig rig(512);
    std::mt19937 gen(7);
    for (int i = 0; i < 20; ++i)
    {
        randomiseParams(rig.proc, gen, true);
        rig.processCounted(512);
    }

    std::size_t total = 0;
    for (int i = 0; i < 200; ++i)
    {
        randomiseParams(rig.proc, gen, true); // automazione, fuori dalla sezione misurata
        total += rig.processCounted(512);
    }
    CHECK(total == 0);
}

TEST_CASE("Variable block sizes do not allocate", "[allocation]")
{
    Rig rig(kMaxBlock);
    std::mt19937 gen(11);
    for (int i = 0; i < 10; ++i)
        rig.processCounted(512);

    std::size_t total = 0;
    for (int n : {1, 2, 3, 31, 32, 33, 37, 64, 100, 127, 480, 512, 1000, 2048, 4095, kMaxBlock})
    {
        randomiseParams(rig.proc, gen, true);
        total += rig.processCounted(n);
    }
    std::uniform_int_distribution<int> size(1, kMaxBlock);
    for (int i = 0; i < 100; ++i)
    {
        randomiseParams(rig.proc, gen, true);
        total += rig.processCounted(size(gen));
    }
    CHECK(total == 0);
}
