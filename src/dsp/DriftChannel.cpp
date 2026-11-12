// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/DriftChannel.h"
#include "dsp/DspUtils.h"
#include <cmath>

namespace driftbox
{
namespace
{
constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kInvSqrt3 = 0.57735026918962576451; // RMS di un uniforme in [-1, 1)
} // namespace

void DriftChannel::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    // Passa-basso one-pole del rumore: y = c y + (1 - c) x, con c = exp(-2 pi fc / fs).
    noiseLpCoeff_ =
        static_cast<float>(std::exp(-kTwoPi * static_cast<double>(limits::kNoiseLowpassHz) / sampleRate));
    reset();
}

void DriftChannel::reset() noexcept
{
    lowShelf_.reset();
    bell_.reset();
    highShelf_.reset();
    apState_ = 0.0f;
    noiseLpState_ = 0.0f;
    noise_.setSeed(noiseSeed_);
}

void DriftChannel::setNoiseSeed(std::uint32_t seed) noexcept
{
    noiseSeed_ = seed;
    noise_.setSeed(seed);
}

void DriftChannel::updateFilter(Biquad& filter, FilterKey& key,
                                BiquadCoeffs (*design)(double, double, double, double), double sampleRate,
                                float hz, float q, float gainDb) noexcept
{
    // I coefficienti dipendono solo da (gain, hz, sampleRate): se sono identici all'ultima chiamata non si
    // ricalcola.
    if (gainDb == key.gainDb && hz == key.hz && sampleRate == key.sampleRate)
    {
        return;
    }
    key = {gainDb, hz, sampleRate};
    filter.setCoeffs(
        design(sampleRate, static_cast<double>(hz), static_cast<double>(q), static_cast<double>(gainDb)));
}

void DriftChannel::update(const VoiceTolerance& tol, const VoiceAmounts& amounts, float noiseScale) noexcept
{
    using namespace limits;

    // Equalizzazione: low shelf, campana, high shelf.
    updateFilter(lowShelf_, lowKey_, &BiquadCoeffs::lowShelf, sampleRate_,
                 kLowShelfHz * std::exp2(tol.lowShelfOct * kShelfSpreadOct), kShelfQ,
                 tol.lowShelfDb * amounts.freq * kMaxShelfDb);
    updateFilter(bell_, bellKey_, &BiquadCoeffs::peak, sampleRate_,
                 kBellLoHz * std::pow(kBellHiHz / kBellLoHz, tol.bellPos01), kBellQ,
                 tol.bellDb * amounts.freq * kMaxBellDb);
    updateFilter(highShelf_, highKey_, &BiquadCoeffs::highShelf, sampleRate_,
                 kHighShelfHz * std::exp2(tol.highShelfOct * kShelfSpreadOct), kShelfQ,
                 tol.highShelfDb * amounts.freq * kMaxShelfDb);

    // Allpass del primo ordine: ritardo frazionario d >= 0 (in campioni), a = (1 - d) / (1 + d);
    // con d = 0 si ha a = 1.
    const double d = static_cast<double>(tol.delay01) * static_cast<double>(amounts.phase) * kMaxDelayUs *
                     1.0e-6 * sampleRate_;
    apCoeff_ = static_cast<float>((1.0 - d) / (1.0 + d));

    // Guadagno: esattamente 1 quando l'esponente è 0.
    const double gainDb = static_cast<double>(tol.gainDb) * static_cast<double>(amounts.gain) * kMaxGainDb;
    gain_ = gainDb == 0.0 ? 1.0f : static_cast<float>(std::pow(10.0, gainDb / 20.0));

    // Saturazione: k = 1 -> saltata. La miscela m = clamp(k - 1, 0, 1) porta con continuità da y (m = 0) a
    // tanh(k y) / k (m = 1), così non c'è salto quando la quantità arriva a 0.
    drive_ = 1.0f + kMaxDrive * amounts.saturation * (1.0f + kDriveSpread * tol.driveSpread);
    satMix_ = clampTo(drive_ - 1.0f, 0.0f, 1.0f);

    // Rumore: l'RMS della voce deve valere il target. Il bianco uniforme ha RMS 1/sqrt(3) e il passa-basso
    // one-pole ne riduce la potenza di (1 - c) / (1 + c): si compensano entrambi analiticamente.
    if (amounts.noise == 0.0f)
    {
        noiseAmp_ = 0.0f;
    }
    else
    {
        const double target = static_cast<double>(dbToGain(kNoiseMaxDbfs + tol.noiseLevel * kNoiseSpreadDb)) *
                              static_cast<double>(amounts.noise) * static_cast<double>(noiseScale);
        const double c = static_cast<double>(noiseLpCoeff_);
        const double lpPower = (1.0 - c) / (1.0 + c);
        noiseAmp_ = static_cast<float>(target / (kInvSqrt3 * std::sqrt(lpPower)));
    }
}

template <bool Sat, bool Noise> float DriftChannel::tick(float x) noexcept
{
    float y = lowShelf_.process(x);
    y = bell_.process(y);
    y = highShelf_.process(y);

    // Allpass: y = a x + s; s = x - a y (con a = 1 è l'identità esatta).
    const float ap = apCoeff_ * y + apState_;
    apState_ = y - apCoeff_ * ap;
    y = ap * gain_;

    if constexpr (Sat)
    {
        y += satMix_ * (fastTanh(drive_ * y) / drive_ - y);
    }
    if constexpr (Noise)
    {
        noiseLpState_ = noiseLpCoeff_ * noiseLpState_ + (1.0f - noiseLpCoeff_) * noise_.nextBipolar();
        y += noiseAmp_ * noiseLpState_;
    }
    return y;
}

template <bool Sat, bool Noise>
void DriftChannel::addBlock(const float* in, float* acc, int n, float w0, float w1) noexcept
{
    const float step = (w1 - w0) / static_cast<float>(n);
    for (int i = 0; i < n; ++i)
    {
        acc[i] += (w0 + step * static_cast<float>(i)) * tick<Sat, Noise>(in[i]);
    }
}

float DriftChannel::processSample(float x) noexcept
{
    const bool sat = drive_ != 1.0f;
    const bool noisy = noiseAmp_ != 0.0f;
    if (sat)
    {
        return noisy ? tick<true, true>(x) : tick<true, false>(x);
    }
    return noisy ? tick<false, true>(x) : tick<false, false>(x);
}

void DriftChannel::processAdd(const float* in, float* acc, int n, float w0, float w1) noexcept
{
    if (n <= 0)
    {
        return;
    }
    const bool sat = drive_ != 1.0f;
    const bool noisy = noiseAmp_ != 0.0f;
    if (sat)
    {
        noisy ? addBlock<true, true>(in, acc, n, w0, w1) : addBlock<true, false>(in, acc, n, w0, w1);
    }
    else
    {
        noisy ? addBlock<false, true>(in, acc, n, w0, w1) : addBlock<false, false>(in, acc, n, w0, w1);
    }
}
} // namespace driftbox
