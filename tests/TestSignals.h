// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <random>
#include <vector>
#include <juce_dsp/juce_dsp.h>

namespace driftbox::test
{
using Signal = std::vector<float>;

inline constexpr double kPi = 3.14159265358979323846;

inline Signal impulse(size_t n, size_t position = 0, float amplitude = 1.0f)
{
    Signal s(n, 0.0f);
    if (position < n)
        s[position] = amplitude;
    return s;
}

inline Signal sine(size_t n, double freqHz, double sampleRate, float amplitude = 1.0f, double phase = 0.0)
{
    Signal s(n);
    for (size_t i = 0; i < n; ++i)
        s[i] = amplitude *
               static_cast<float>(std::sin(2.0 * kPi * freqHz * static_cast<double>(i) / sampleRate + phase));
    return s;
}

inline Signal noise(size_t n, unsigned seed = 1234, float amplitude = 1.0f)
{
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    Signal s(n);
    for (auto& v : s)
        v = amplitude * dist(gen);
    return s;
}

// Silence, then `length` samples of a sine starting at `start`, then silence (Hann-free hard burst).
inline Signal burst(size_t n, size_t start, size_t length, double freqHz, double sampleRate,
                    float amplitude = 1.0f)
{
    Signal s(n, 0.0f);
    for (size_t i = start; i < start + length && i < n; ++i)
        s[i] = amplitude *
               static_cast<float>(std::sin(2.0 * kPi * freqHz * static_cast<double>(i - start) / sampleRate));
    return s;
}

inline size_t nextPow2(size_t n)
{
    size_t p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

// Complex spectrum (bins 0..N/2) of the signal zero-padded to a power of two.
inline std::vector<std::complex<float>> spectrum(const Signal& s)
{
    const size_t n = nextPow2(s.size());
    int order = 0;
    while ((size_t(1) << order) < n)
        ++order;

    juce::dsp::FFT fft(order);
    std::vector<std::complex<float>> in(n, {0.0f, 0.0f}), out(n);
    for (size_t i = 0; i < s.size(); ++i)
        in[i] = {s[i], 0.0f};
    fft.perform(in.data(), out.data(), false);
    out.resize(n / 2 + 1);
    return out;
}

inline std::vector<float> fftMagnitude(const Signal& s)
{
    const auto spec = spectrum(s);
    std::vector<float> mag(spec.size());
    for (size_t i = 0; i < spec.size(); ++i)
        mag[i] = std::abs(spec[i]);
    return mag;
}

inline std::vector<float> fftPhase(const Signal& s)
{
    const auto spec = spectrum(s);
    std::vector<float> ph(spec.size());
    for (size_t i = 0; i < spec.size(); ++i)
        ph[i] = std::arg(spec[i]);
    return ph;
}

inline double rms(const Signal& s, size_t from = 0, size_t to = static_cast<size_t>(-1))
{
    to = std::min(to, s.size());
    if (from >= to)
        return 0.0;
    double acc = 0.0;
    for (size_t i = from; i < to; ++i)
        acc += static_cast<double>(s[i]) * static_cast<double>(s[i]);
    return std::sqrt(acc / static_cast<double>(to - from));
}

inline double toDb(double linear)
{
    return 20.0 * std::log10(std::max(linear, 1.0e-12));
}
inline double fromDb(double db)
{
    return std::pow(10.0, db / 20.0);
}
} // namespace driftbox::test
