// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/DriftEngine.h"
#include "dsp/DspUtils.h"
#include <algorithm>
#include <cmath>

namespace driftbox
{
namespace
{
// Soglia sotto la quale un valore smussato si aggancia ESATTAMENTE al target: senza, Drift = 0 non
// diventerebbe mai trasparente.
constexpr float kSnapDistance = 1.0e-6f;

// Valore finito limitato a [lo, hi]; un NaN diventa lo.
float finiteClamp(float v, float lo, float hi) noexcept
{
    return std::isfinite(v) ? clampTo(v, lo, hi) : lo;
}

// Coefficiente one-pole per n campioni: a = exp(-n / (tau fs)).
float chunkCoeff(int n, double seconds, double sampleRate) noexcept
{
    return static_cast<float>(std::exp(-static_cast<double>(n) / (seconds * sampleRate)));
}

// Un passo one-pole verso il target, con aggancio esatto quando la distanza è trascurabile.
void smoothStep(float& value, float target, float coeff) noexcept
{
    value = target + (value - target) * coeff;
    if (std::fabs(value - target) < kSnapDistance)
    {
        value = target;
    }
}

// Numero di voci valido: pari, tra 2 e kMaxVoices (i valori previsti sono 2, 4, 8, 16).
int validVoices(int n) noexcept
{
    return clampTo(n, 2, kMaxVoices) & ~1;
}
} // namespace

void DriftEngine::prepare(double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlockSize;
    numChannels_ = numChannels <= 1 ? 1 : 2; // oltre 2 canali si elaborano solo i primi 2
    for (auto& v : voices_)
    {
        v.prepare(sampleRate);
    }
    // Il profilo vale fin da subito anche con il seed iniziale (che può essere 0).
    profile_ = DriftProfile::generate(seed_);
    toProfile_ = profile_;
    fading_ = false;
    applyNoiseSeeds(profile_);
    reset();
}

void DriftEngine::reset() noexcept
{
    // Un eventuale crossfade del profilo in corso viene concluso.
    if (fading_)
    {
        profile_ = toProfile_;
        fading_ = false;
    }
    fadePos_ = 0.0f;
    for (auto& v : voices_)
    {
        v.reset();
    }
    fresh_ = true;
    voicesIdle_ = false;
    targetVoices_ = 0;
    rampPos_ = 1.0f;
}

void DriftEngine::setSettings(const EngineSettings& settings) noexcept
{
    settings_ = settings;
}

void DriftEngine::applyNoiseSeeds(const DriftProfile& profile) noexcept
{
    for (size_t v = 0; v < voices_.size(); ++v)
    {
        voices_[v].setNoiseSeed(profile.noiseSeeds[v]);
    }
}

void DriftEngine::setSeed(std::uint64_t seed, bool crossfade) noexcept
{
    if (seed == seed_)
    {
        return;
    }
    seed_ = seed;

    if (!crossfade || fresh_)
    {
        // Cambio immediato: nuovo profilo, nuovi seed del rumore e voci ripartite da stato pulito.
        profile_ = DriftProfile::generate(seed);
        toProfile_ = profile_;
        fading_ = false;
        applyNoiseSeeds(profile_);
        for (auto& v : voices_)
        {
            v.reset();
        }
        return;
    }

    // Crossfade: si parte dal profilo effettivo corrente (anche se un fade era già in corso).
    fromProfile_ = profile_;
    toProfile_ = DriftProfile::generate(seed);
    fadePos_ = 0.0f;
    fading_ = true;
    // I generatori di rumore passano subito ai nuovi seed, senza toccare lo stato dei filtri.
    applyNoiseSeeds(toProfile_);
}

void DriftEngine::computeTargetWeights() noexcept
{
    const int n = validVoices(settings_.numVoices);
    const float w = 2.0f / static_cast<float>(n);
    for (int v = 0; v < kMaxVoices; ++v)
    {
        // In mono le voci dispari (lato 1) non esistono: le pari pesano 1 / (N / 2) = 2 / N.
        const bool active = v < n && (numChannels_ == 2 || (v & 1) == 0);
        targetWeights_[static_cast<size_t>(v)] = active ? w : 0.0f;
    }
    targetVoices_ = n;
}

void DriftEngine::advanceControl(int n) noexcept
{
    const double sr = sampleRate_;
    const float dn = static_cast<float>(n);

    // --- Parametri smussati (control rate) ---
    const float drift = finiteClamp(settings_.drift01, 0.0f, 1.0f);
    const float ft = finiteClamp(settings_.freqTol01, 0.0f, 1.0f);
    const float pt = finiteClamp(settings_.phaseTol01, 0.0f, 1.0f);
    const float gt = finiteClamp(settings_.gainTol01, 0.0f, 1.0f);
    const float sat = finiteClamp(settings_.saturation01, 0.0f, 1.0f);
    const float noise = finiteClamp(settings_.noise01, 0.0f, 1.0f);
    if (fresh_)
    {
        smooth_ = {drift, ft, pt, gt, sat, noise};
    }
    else
    {
        const float a = chunkCoeff(n, kParamSmoothSeconds, sr);
        smoothStep(smooth_.drift, drift, a);
        smoothStep(smooth_.freqTol, ft, a);
        smoothStep(smooth_.phaseTol, pt, a);
        smoothStep(smooth_.gainTol, gt, a);
        smoothStep(smooth_.saturation, sat, a);
        smoothStep(smooth_.noise, noise, a);
    }

    // --- Crossfade del profilo dopo Randomize ---
    if (fading_)
    {
        fadePos_ += dn / static_cast<float>(sr * kProfileFadeSeconds);
        if (fadePos_ >= 1.0f)
        {
            profile_ = toProfile_;
            fading_ = false;
        }
        else
        {
            profile_ = DriftProfile::lerp(fromProfile_, toProfile_, fadePos_);
        }
    }

    // --- Pesi delle voci: rampa con un'unica posizione per tutte ---
    if (fresh_ || validVoices(settings_.numVoices) != targetVoices_)
    {
        computeTargetWeights();
        if (fresh_)
        {
            weights_ = targetWeights_;
            rampPos_ = 1.0f;
        }
        else
        {
            startWeights_ = weights_; // dai pesi correnti, anche a rampa in corso
            rampPos_ = 0.0f;
        }
    }
    w0_ = weights_;
    if (rampPos_ < 1.0f)
    {
        rampPos_ += dn / static_cast<float>(sr * kVoiceRampSeconds);
        if (rampPos_ >= 1.0f)
        {
            rampPos_ = 1.0f;
            weights_ = targetWeights_; // alla fine della rampa i pesi sono esattamente i target
        }
        else
        {
            for (size_t v = 0; v < weights_.size(); ++v)
            {
                weights_[v] = startWeights_[v] + (targetWeights_[v] - startWeights_[v]) * rampPos_;
            }
        }
    }
    w1_ = weights_;
}

void DriftEngine::processChunk(float* const* ch, int numCh, int n) noexcept
{
    // Ingresso sanitizzato: vale sia per il percorso dry sia per quello wet.
    for (int c = 0; c < numCh; ++c)
    {
        auto& in = in_[static_cast<size_t>(c)];
        for (int i = 0; i < n; ++i)
        {
            const float x = sanitize(ch[c][i]);
            in[static_cast<size_t>(i)] = x;
            blockInPeak_ = std::max(blockInPeak_, std::fabs(x));
        }
    }

    advanceControl(n);

    // --- Bypass: wetMix da 1 a 0 (o viceversa) con rampa lineare ---
    const float wetTarget = settings_.bypass ? 0.0f : 1.0f;
    if (fresh_)
    {
        wetMix_ = wetTarget;
    }
    const float mix0 = wetMix_;
    float mix1 = mix0;
    if (mix0 != wetTarget)
    {
        const float step = static_cast<float>(n) / static_cast<float>(sampleRate_ * kBypassFadeSeconds);
        mix1 = mix0 < wetTarget ? std::min(mix0 + step, wetTarget) : std::max(mix0 - step, wetTarget);
    }
    wetMix_ = mix1;
    const bool wetNeeded = mix0 > 0.0f || mix1 > 0.0f;

    // --- Output Gain: smussato, esattamente 1 a 0 dB ---
    const float gainTarget =
        settings_.outGainDb == 0.0f ? 1.0f : dbToGain(finiteClamp(settings_.outGainDb, -100.0f, 48.0f));
    if (fresh_)
    {
        outGain_ = gainTarget;
    }
    const float gain0 = outGain_;
    smoothStep(outGain_, gainTarget, chunkCoeff(n, kGainSmoothSeconds, sampleRate_));
    const float gainStep = (outGain_ - gain0) / static_cast<float>(n);

    fresh_ = false;

    if (wetNeeded)
    {
        for (auto& a : acc_)
        {
            a.fill(0.0f);
        }
        if (voicesIdle_)
        {
            // Ripresa dopo un bypass stabile: stato pulito prima che wetMix salga.
            for (auto& v : voices_)
            {
                v.reset();
            }
            voicesIdle_ = false;
        }

        // Compensazione del rumore per lato: 1 / sqrt(somma dei pesi al quadrato) con i pesi correnti
        // (a metà sotto-blocco), così il livello resta continuo anche durante le rampe.
        std::array<float, 2> sumSq{0.0f, 0.0f};
        for (size_t v = 0; v < voices_.size(); ++v)
        {
            const float w = 0.5f * (w0_[v] + w1_[v]);
            sumSq[v & 1] += w * w;
        }
        const std::array<float, 2> noiseScale{sumSq[0] > 0.0f ? 1.0f / std::sqrt(sumSq[0]) : 1.0f,
                                              sumSq[1] > 0.0f ? 1.0f / std::sqrt(sumSq[1]) : 1.0f};

        const VoiceAmounts amounts{smooth_.drift * smooth_.freqTol, smooth_.drift * smooth_.phaseTol,
                                   smooth_.drift * smooth_.gainTol, smooth_.drift * smooth_.saturation,
                                   smooth_.drift * smooth_.noise};
        for (size_t v = 0; v < voices_.size(); ++v)
        {
            const size_t side = v & 1;
            if ((w0_[v] == 0.0f && w1_[v] == 0.0f) || static_cast<int>(side) >= numCh)
            {
                continue; // peso 0: la voce non viene elaborata
            }
            if (w0_[v] == 0.0f)
            {
                voices_[v].reset(); // la voce riparte da stato pulito
            }
            voices_[v].update(profile_.voices[v], amounts, noiseScale[side]);
            voices_[v].processAdd(in_[side].data(), acc_[side].data(), n, w0_[v], w1_[v]);
        }
    }
    else
    {
        voicesIdle_ = true; // bypass stabile: uscita = dry esatto, voci ferme
    }

    // --- Uscita: Output Gain, ceiling, sanitizzazione, miscela di bypass ---
    const float dmix = (mix1 - mix0) / static_cast<float>(n);
    for (int c = 0; c < numCh; ++c)
    {
        const auto& in = in_[static_cast<size_t>(c)];
        const auto& acc = acc_[static_cast<size_t>(c)];
        float* out = ch[c];
        if (!wetNeeded)
        {
            for (int i = 0; i < n; ++i)
            {
                out[i] = in[static_cast<size_t>(i)];
                blockOutPeak_ = std::max(blockOutPeak_, std::fabs(out[i]));
            }
            continue;
        }
        for (int i = 0; i < n; ++i)
        {
            const auto k = static_cast<size_t>(i);
            float y = acc[k] * (gain0 + gainStep * static_cast<float>(i));
            if (std::fabs(y) > kCeilingThreshold)
            {
                blockCeiling_ = true;
            }
            y = sanitize(softCeiling(y));
            if (mix0 != 1.0f || mix1 != 1.0f)
            {
                const float m = mix0 + dmix * static_cast<float>(i);
                y = in[k] + (y - in[k]) * m;
            }
            out[i] = y;
            blockOutPeak_ = std::max(blockOutPeak_, std::fabs(y));
        }
    }
}

void DriftEngine::process(juce::AudioBuffer<float>& buffer) noexcept
{
    const int total = buffer.getNumSamples();
    const int numCh = std::min(buffer.getNumChannels(), numChannels_);
    if (total <= 0 || numCh <= 0)
    {
        return;
    }

    std::array<float*, 2> ptr{buffer.getWritePointer(0), numCh > 1 ? buffer.getWritePointer(1) : nullptr};
    blockInPeak_ = 0.0f;
    blockOutPeak_ = 0.0f;
    blockCeiling_ = false;

    for (int start = 0; start < total; start += kControlInterval)
    {
        const int n = std::min(kControlInterval, total - start);
        std::array<float*, 2> chunk{ptr[0] + start, ptr[1] != nullptr ? ptr[1] + start : nullptr};
        processChunk(chunk.data(), numCh, n);
    }

    meters_.inPeakDb.store(gainToDb(blockInPeak_), std::memory_order_relaxed);
    meters_.outPeakDb.store(gainToDb(blockOutPeak_), std::memory_order_relaxed);
    meters_.ceilingActive.store(blockCeiling_, std::memory_order_relaxed);
}

float DriftEngine::voiceWeight(int v) const noexcept
{
    return v >= 0 && v < kMaxVoices ? weights_[static_cast<size_t>(v)] : 0.0f;
}
} // namespace driftbox
