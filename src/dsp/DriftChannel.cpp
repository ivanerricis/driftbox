// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/DriftChannel.h"

namespace driftbox
{
// STUB (scaffold T0): la voce è l'identità. L'implementazione completa arriva con il task T2.
void DriftChannel::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
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

void DriftChannel::update(const VoiceTolerance&, const VoiceAmounts&, float) noexcept {}

float DriftChannel::processSample(float x) noexcept
{
    return x;
}

void DriftChannel::processAdd(const float* in, float* acc, int n, float w0, float w1) noexcept
{
    if (n <= 0)
    {
        return;
    }
    const float step = (w1 - w0) / static_cast<float>(n);
    for (int i = 0; i < n; ++i)
    {
        acc[i] += (w0 + step * static_cast<float>(i)) * processSample(in[i]);
    }
}
} // namespace driftbox
