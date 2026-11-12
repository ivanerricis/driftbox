// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/DriftProfile.h"

#include "dsp/Prng.h"

namespace driftbox
{
namespace
{
// Distribuzione triangolare in [-1, 1): somma di due uniformi bipolari. Le tolleranze reali dei componenti si
// addensano vicino al valore nominale. L'ordine delle due estrazioni fa parte del formato.
float nextTriangular(SplitMix64& rng) noexcept
{
    const float a = rng.nextBipolar();
    const float b = rng.nextBipolar();
    return (a + b) * 0.5f;
}

// Costante del "mixer" virtuale (ASCII "DRIFTBXO"), parte del formato congelato v1.
constexpr std::uint64_t kConsoleSeed = 0x445249465442584Full;

// Interpolazione che garantisce i valori esatti agli estremi: t = 0 -> a, t >= 1 -> b.
float mix(float a, float b, float t) noexcept
{
    return t >= 1.0f ? b : a + (b - a) * t;
}
} // namespace

// FORMATO CONGELATO, versione 1. Non modificare mai questo algoritmo: le sessioni salvate devono suonare
// identiche. Per cambiare la generazione si aggiunge una nuova versione.
//
//   kConsoleSeed = 0x445249465442584F
//   Seed del canale c (1..kNumConsoleChannels):
//     SplitMix64 h(kConsoleSeed + c);  s = h.next();
//   SplitMix64 rng(s); si estraggono, nell'ordine:
//     lowShelfDb, lowShelfOct, highShelfDb, highShelfOct, bellDb   triangolari in [-1, 1)
//     bellPos01, delay01                                           uniformi in [0, 1)
//     gainDb, driveSpread, noiseLevel                              triangolari in [-1, 1)
//     noiseSeed = uint32(rng.next() >> 32) | 1                     (mai 0)
//   Triangolare = (nextBipolar() + nextBipolar()) * 0.5f (due estrazioni, in quest'ordine).
//
// Perché il passaggio di hash: SplitMix64 avanza lo stato di una costante fissa (gamma) a ogni estrazione,
// quindi due stati che differiscono di un multiplo di gamma producono lo stesso stream sfasato: sequenze
// correlate. Usare direttamente seed consecutivi (kConsoleSeed + c) non dà alcuna garanzia di
// indipendenza tra canali vicini, mentre il primo output di h è una funzione mescolata dell'indice e porta
// ogni canale in uno stato di partenza scorrelato dagli altri.
ChannelProfile channelProfile(int channel, int version) noexcept
{
    // Versioni sconosciute: si usa la più recente. Oggi esiste solo la 1.
    if (version <= 0 || version > kProfileVersion)
    {
        version = kProfileVersion;
    }

    channel = channel < 1 ? 1 : (channel > kNumConsoleChannels ? kNumConsoleChannels : channel);

    SplitMix64 h(kConsoleSeed + static_cast<std::uint64_t>(channel));
    const std::uint64_t s = h.next();
    SplitMix64 rng(s);

    ChannelProfile p;
    auto& t = p.tol;
    t.lowShelfDb = nextTriangular(rng);
    t.lowShelfOct = nextTriangular(rng);
    t.highShelfDb = nextTriangular(rng);
    t.highShelfOct = nextTriangular(rng);
    t.bellDb = nextTriangular(rng);
    t.bellPos01 = rng.nextUnipolar();
    t.delay01 = rng.nextUnipolar();
    t.gainDb = nextTriangular(rng);
    t.driveSpread = nextTriangular(rng);
    t.noiseLevel = nextTriangular(rng);
    p.noiseSeed = static_cast<std::uint32_t>(rng.next() >> 32) | 1u;
    return p;
}

VoiceTolerance lerp(const VoiceTolerance& a, const VoiceTolerance& b, float t) noexcept
{
    // Il confronto scritto così scarta anche i NaN (t diventa 0).
    t = t > 0.0f ? (t < 1.0f ? t : 1.0f) : 0.0f;

    VoiceTolerance r;
    r.lowShelfDb = mix(a.lowShelfDb, b.lowShelfDb, t);
    r.lowShelfOct = mix(a.lowShelfOct, b.lowShelfOct, t);
    r.highShelfDb = mix(a.highShelfDb, b.highShelfDb, t);
    r.highShelfOct = mix(a.highShelfOct, b.highShelfOct, t);
    r.bellDb = mix(a.bellDb, b.bellDb, t);
    r.bellPos01 = mix(a.bellPos01, b.bellPos01, t);
    r.delay01 = mix(a.delay01, b.delay01, t);
    r.gainDb = mix(a.gainDb, b.gainDb, t);
    r.driveSpread = mix(a.driveSpread, b.driveSpread, t);
    r.noiseLevel = mix(a.noiseLevel, b.noiseLevel, t);
    return r;
}
} // namespace driftbox
