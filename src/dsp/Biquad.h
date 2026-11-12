// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace driftbox
{
// Coefficienti normalizzati (a0 = 1) di un biquad: y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2.
struct BiquadCoeffs
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;

    static BiquadCoeffs identity() noexcept { return {}; }

    // Formule pubbliche dell'"Audio EQ Cookbook" di R. Bristow-Johnson, calcolate in double. Con gainDb == 0
    // restituiscono ESATTAMENTE identity(), così a Drift = 0 i filtri sono trasparenti bit per bit.
    static BiquadCoeffs lowShelf(double sampleRate, double hz, double q, double gainDb) noexcept;
    static BiquadCoeffs highShelf(double sampleRate, double hz, double q, double gainDb) noexcept;
    static BiquadCoeffs peak(double sampleRate, double hz, double q, double gainDb) noexcept;
};

// Biquad in forma trasposta diretta II, float, un canale.
class Biquad
{
public:
    void setCoeffs(const BiquadCoeffs& c) noexcept { c_ = c; }
    const BiquadCoeffs& coeffs() const noexcept { return c_; }
    void reset() noexcept { s1_ = s2_ = 0.0f; }

    float process(float x) noexcept
    {
        const float y = c_.b0 * x + s1_;
        s1_ = c_.b1 * x - c_.a1 * y + s2_;
        s2_ = c_.b2 * x - c_.a2 * y;
        return y;
    }

private:
    BiquadCoeffs c_;
    float s1_ = 0.0f, s2_ = 0.0f;
};
} // namespace driftbox
