// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/DspTypes.h"
#include "params/ParamIDs.h"
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

namespace driftbox
{
inline constexpr int kNumParameters = 9;

// Layout completo dei parametri. Le percentuali sono memorizzate in 0..1 e mostrate come 0..100 %.
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Puntatori ai valori grezzi (denormalizzati), leggibili in sicurezza dall'audio thread.
struct ParamRefs
{
    explicit ParamRefs(juce::AudioProcessorValueTreeState& apvts);

    std::atomic<float>*channels, *drift, *freqTol, *phaseTol, *gainTol, *saturation, *noise, *outGain,
        *bypass;

    // Copia coerente dei parametri per il blocco corrente (noexcept, senza allocazioni).
    EngineSettings snapshot() const noexcept;
};
} // namespace driftbox
