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

// Interpolazione che garantisce i valori esatti agli estremi: t = 0 -> a, t >= 1 -> b.
float mix(float a, float b, float t) noexcept
{
    return t >= 1.0f ? b : a + (b - a) * t;
}
} // namespace

// FORMATO CONGELATO, versione 1. Non modificare mai questo algoritmo: le sessioni salvate devono suonare
// identiche. Per cambiare la generazione si aggiunge una nuova versione.
//
//   SplitMix64 rng(seed)
//   Per v = 0..kMaxVoices-1, nell'ordine, si estraggono:
//     lowShelfDb, lowShelfOct, highShelfDb, highShelfOct, bellDb   triangolari in [-1, 1)
//     bellPos01, delay01                                           uniformi in [0, 1)
//     gainDb, driveSpread, noiseLevel                              triangolari in [-1, 1)
//     noiseSeeds[v] = uint32(rng.next() >> 32) | 1                 (mai 0)
//   Triangolare = (nextBipolar() + nextBipolar()) * 0.5f (due estrazioni, in quest'ordine).
//   Le voci sono sempre tutte 16, indipendentemente dal numero di voci attive: cambiare il numero di voci non
//   cambia le tolleranze delle voci già esistenti.
DriftProfile DriftProfile::generate(std::uint64_t seed, int version) noexcept
{
    // Versioni sconosciute: si usa la più recente. Oggi esiste solo la 1.
    if (version <= 0 || version > kProfileVersion)
    {
        version = kProfileVersion;
    }

    DriftProfile p;
    p.seed = seed;

    SplitMix64 rng(seed);
    for (int v = 0; v < kMaxVoices; ++v)
    {
        auto& t = p.voices[static_cast<std::size_t>(v)];
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
        p.noiseSeeds[static_cast<std::size_t>(v)] = static_cast<std::uint32_t>(rng.next() >> 32) | 1u;
    }
    return p;
}

DriftProfile DriftProfile::lerp(const DriftProfile& a, const DriftProfile& b, float t) noexcept
{
    // Il confronto scritto così scarta anche i NaN (t diventa 0).
    t = t > 0.0f ? (t < 1.0f ? t : 1.0f) : 0.0f;

    DriftProfile p = b; // seed e noiseSeeds da b
    for (std::size_t v = 0; v < p.voices.size(); ++v)
    {
        const auto& x = a.voices[v];
        const auto& y = b.voices[v];
        auto& r = p.voices[v];
        r.lowShelfDb = mix(x.lowShelfDb, y.lowShelfDb, t);
        r.lowShelfOct = mix(x.lowShelfOct, y.lowShelfOct, t);
        r.highShelfDb = mix(x.highShelfDb, y.highShelfDb, t);
        r.highShelfOct = mix(x.highShelfOct, y.highShelfOct, t);
        r.bellDb = mix(x.bellDb, y.bellDb, t);
        r.bellPos01 = mix(x.bellPos01, y.bellPos01, t);
        r.delay01 = mix(x.delay01, y.delay01, t);
        r.gainDb = mix(x.gainDb, y.gainDb, t);
        r.driveSpread = mix(x.driveSpread, y.driveSpread, t);
        r.noiseLevel = mix(x.noiseLevel, y.noiseLevel, t);
    }
    return p;
}
} // namespace driftbox
