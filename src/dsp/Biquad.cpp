// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/Biquad.h"
#include <algorithm>
#include <cmath>

namespace driftbox
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kMinHz = 10.0;
constexpr double kMaxHzRatio = 0.45; // frazione della frequenza di campionamento
constexpr double kMinQ = 0.05;

// Coefficienti non normalizzati del cookbook RBJ.
struct Raw
{
    double b0, b1, b2, a0, a1, a2;
};

BiquadCoeffs normalize(const Raw& r) noexcept
{
    const double inv = 1.0 / r.a0;
    BiquadCoeffs c;
    c.b0 = static_cast<float>(r.b0 * inv);
    c.b1 = static_cast<float>(r.b1 * inv);
    c.b2 = static_cast<float>(r.b2 * inv);
    c.a1 = static_cast<float>(r.a1 * inv);
    c.a2 = static_cast<float>(r.a2 * inv);
    return c;
}

// Limita hz a [10, 0.45 fs] e q a un minimo positivo, così i coefficienti restano stabili a ogni sample rate.
void sanitizeParams(double sampleRate, double& hz, double& q) noexcept
{
    hz = std::clamp(hz, kMinHz, kMaxHzRatio * sampleRate);
    q = std::max(q, kMinQ);
}
} // namespace

double BiquadCoeffs::magnitudeDb(double sampleRate, double hz) const noexcept
{
    if (b0 == 1.0f && b1 == 0.0f && b2 == 0.0f && a1 == 0.0f && a2 == 0.0f)
    {
        return 0.0; // identità esatta
    }
    const double w = 2.0 * kPi * hz / sampleRate;
    const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2.0 * w), s2 = std::sin(2.0 * w);
    // H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2) con z^-k = cos(kw) - j sin(kw).
    const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
    const double dr = 1.0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
    const double num = nr * nr + ni * ni, den = dr * dr + di * di;
    return den > 0.0 && num > 0.0 ? 10.0 * std::log10(num / den) : 0.0;
}

BiquadCoeffs BiquadCoeffs::lowShelf(double sampleRate, double hz, double q, double gainDb) noexcept
{
    if (gainDb == 0.0)
    {
        return identity();
    }
    sanitizeParams(sampleRate, hz, q);
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * hz / sampleRate;
    const double cs = std::cos(w0);
    const double tsa = 2.0 * std::sqrt(a) * (std::sin(w0) / (2.0 * q)); // 2 sqrt(A) alpha
    Raw r;
    r.b0 = a * ((a + 1.0) - (a - 1.0) * cs + tsa);
    r.b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cs);
    r.b2 = a * ((a + 1.0) - (a - 1.0) * cs - tsa);
    r.a0 = (a + 1.0) + (a - 1.0) * cs + tsa;
    r.a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cs);
    r.a2 = (a + 1.0) + (a - 1.0) * cs - tsa;
    return normalize(r);
}

BiquadCoeffs BiquadCoeffs::highShelf(double sampleRate, double hz, double q, double gainDb) noexcept
{
    if (gainDb == 0.0)
    {
        return identity();
    }
    sanitizeParams(sampleRate, hz, q);
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * hz / sampleRate;
    const double cs = std::cos(w0);
    const double tsa = 2.0 * std::sqrt(a) * (std::sin(w0) / (2.0 * q));
    Raw r;
    r.b0 = a * ((a + 1.0) + (a - 1.0) * cs + tsa);
    r.b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cs);
    r.b2 = a * ((a + 1.0) + (a - 1.0) * cs - tsa);
    r.a0 = (a + 1.0) - (a - 1.0) * cs + tsa;
    r.a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cs);
    r.a2 = (a + 1.0) - (a - 1.0) * cs - tsa;
    return normalize(r);
}

BiquadCoeffs BiquadCoeffs::peak(double sampleRate, double hz, double q, double gainDb) noexcept
{
    if (gainDb == 0.0)
    {
        return identity();
    }
    sanitizeParams(sampleRate, hz, q);
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * hz / sampleRate;
    const double cs = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    Raw r;
    r.b0 = 1.0 + alpha * a;
    r.b1 = -2.0 * cs;
    r.b2 = 1.0 - alpha * a;
    r.a0 = 1.0 + alpha / a;
    r.a1 = -2.0 * cs;
    r.a2 = 1.0 - alpha / a;
    return normalize(r);
}
} // namespace driftbox
