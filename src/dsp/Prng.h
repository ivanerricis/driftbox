// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>

namespace driftbox
{
// Generatore SplitMix64 (Steele, Lea, Flood 2014; dominio pubblico). Usato al posto delle distribuzioni di
// <random>, il cui output dipende dall'implementazione della libreria standard: lo stesso seed deve produrre
// le stesse tolleranze su Windows, macOS e Linux. Anche la conversione da intero a float è esplicita.
class SplitMix64
{
public:
    explicit SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}

    std::uint64_t next() noexcept
    {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // Valore in [0, 1): i 24 bit alti, rappresentabili esattamente in float.
    float nextUnipolar() noexcept { return static_cast<float>(next() >> 40) * (1.0f / 16777216.0f); }

    // Valore in [-1, 1).
    float nextBipolar() noexcept { return nextUnipolar() * 2.0f - 1.0f; }

private:
    std::uint64_t state_;
};

// Xorshift32 (Marsaglia 2003): generatore molto economico per il rumore campione per campione. Lo stato non
// può essere 0, quindi un seed nullo viene sostituito con una costante.
class Xorshift32
{
public:
    explicit Xorshift32(std::uint32_t seed = 0x6D2B79F5u) noexcept { setSeed(seed); }

    void setSeed(std::uint32_t seed) noexcept { state_ = seed != 0 ? seed : 0x6D2B79F5u; }

    std::uint32_t next() noexcept
    {
        std::uint32_t x = state_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state_ = x;
        return x;
    }

    // Valore in [-1, 1).
    float nextBipolar() noexcept { return static_cast<float>(next() >> 8) * (2.0f / 16777216.0f) - 1.0f; }

private:
    std::uint32_t state_ = 0x6D2B79F5u;
};
} // namespace driftbox
