// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/DriftEngine.h"

namespace driftbox
{
// STUB (scaffold T0): pass-through. L'implementazione completa arriva con il task T3.
void DriftEngine::prepare(double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlockSize;
    numChannels_ = numChannels;
    for (auto& v : voices_)
    {
        v.prepare(sampleRate);
    }
    reset();
}

void DriftEngine::reset() noexcept
{
    for (auto& v : voices_)
    {
        v.reset();
    }
}

void DriftEngine::setSettings(const EngineSettings& settings) noexcept
{
    settings_ = settings;
}

void DriftEngine::setSeed(std::uint64_t seed, bool) noexcept
{
    if (seed == seed_)
    {
        return;
    }
    seed_ = seed;
    profile_ = DriftProfile::generate(seed);
}

void DriftEngine::process(juce::AudioBuffer<float>&) noexcept {}

float DriftEngine::voiceWeight(int v) const noexcept
{
    return v >= 0 && v < kMaxVoices ? weights_[static_cast<size_t>(v)] : 0.0f;
}
} // namespace driftbox
