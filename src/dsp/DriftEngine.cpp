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

// Output Gain sotto questa soglia (metà del passo di 0.1 dB del parametro) vale esattamente 0 dB.
constexpr float kUnityGainDb = 0.05f;

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

// Numero di canale valido: 1..kNumConsoleChannels.
int validChannel(int c) noexcept
{
    return clampTo(c, 1, kNumConsoleChannels);
}
} // namespace

void DriftEngine::prepare(double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlockSize;
    numChannels_ = numChannels <= 1 ? 1 : 2; // oltre 2 canali si elaborano solo i primi 2
    for (auto& s : sides_)
    {
        s.chain.prepare(sampleRate);
    }
    reset();
}

void DriftEngine::reset() noexcept
{
    for (auto& s : sides_)
    {
        s.chain.reset();
        s.fading = false;
        s.fadePos = 0.0f;
    }
    fresh_ = true;
    chainsIdle_ = false;
}

void DriftEngine::setSettings(const EngineSettings& settings) noexcept
{
    settings_ = settings;
}

int DriftEngine::channelOf(int side) const noexcept
{
    return side == 1 ? sides_[1].channel : sides_[0].channel;
}

bool DriftEngine::isFading(int side) const noexcept
{
    return side == 1 ? sides_[1].fading : sides_[0].fading;
}

void DriftEngine::advanceControl(int numCh, int n) noexcept
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

    // --- Tolleranze: un profilo fisso per canale, crossfade al cambio ---
    const std::array<int, 2> wanted{validChannel(settings_.channelL), validChannel(settings_.channelR)};
    for (int s = 0; s < numCh; ++s)
    {
        auto& side = sides_[static_cast<size_t>(s)];
        const int target = wanted[static_cast<size_t>(s)];
        if (fresh_ || target != side.channel)
        {
            const ChannelProfile profile = channelProfile(target);
            side.channel = target;
            // Il generatore di rumore passa subito al nuovo seed, senza toccare lo stato dei filtri.
            side.chain.setNoiseSeed(profile.noiseSeed);
            if (fresh_)
            {
                // Nessun fade: i valori sono i target e il rumore riparte dal seed del canale.
                side.cur = profile.tol;
                side.to = profile.tol;
                side.fading = false;
                side.chain.reset();
            }
            else
            {
                // Si parte dal valore effettivo corrente, anche se un fade era già in corso.
                side.from = side.cur;
                side.to = profile.tol;
                side.fadePos = 0.0f;
                side.fading = true;
            }
        }
        if (side.fading)
        {
            side.fadePos += dn / static_cast<float>(sr * kProfileFadeSeconds);
            if (side.fadePos >= 1.0f)
            {
                side.cur = side.to;
                side.fading = false;
            }
            else
            {
                side.cur = lerp(side.from, side.to, side.fadePos);
            }
        }
    }
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

    advanceControl(numCh, n);

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
    // Il parametro ha passo 0.1 dB, ma il suo valore "0 dB" può arrivare come 3.6e-7 per arrotondamenti del
    // range normalizzato (visto su macOS): sotto metà del passo vale esattamente guadagno unitario.
    const float gainDb = finiteClamp(settings_.outGainDb, -100.0f, 48.0f);
    const float gainTarget = std::fabs(gainDb) < kUnityGainDb ? 1.0f : dbToGain(gainDb);
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
        if (chainsIdle_)
        {
            // Ripresa dopo un bypass stabile: stato pulito prima che wetMix salga.
            for (auto& side : sides_)
            {
                side.chain.reset();
            }
            chainsIdle_ = false;
        }

        const VoiceAmounts amounts{smooth_.drift * smooth_.freqTol, smooth_.drift * smooth_.phaseTol,
                                   smooth_.drift * smooth_.gainTol, smooth_.drift * smooth_.saturation,
                                   smooth_.drift * smooth_.noise};
        for (int c = 0; c < numCh; ++c)
        {
            auto& side = sides_[static_cast<size_t>(c)];
            side.chain.update(side.cur, amounts, 1.0f);
            side.chain.processAdd(in_[static_cast<size_t>(c)].data(), acc_[static_cast<size_t>(c)].data(), n,
                                  1.0f, 1.0f);
        }
    }
    else
    {
        chainsIdle_ = true; // bypass stabile: uscita = dry esatto, catene ferme
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
                blockOutPeak_[static_cast<size_t>(c)] =
                    std::max(blockOutPeak_[static_cast<size_t>(c)], std::fabs(out[i]));
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
            blockOutPeak_[static_cast<size_t>(c)] =
                std::max(blockOutPeak_[static_cast<size_t>(c)], std::fabs(y));
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
    blockOutPeak_.fill(0.0f);
    blockCeiling_ = false;

    for (int start = 0; start < total; start += kControlInterval)
    {
        const int n = std::min(kControlInterval, total - start);
        std::array<float*, 2> chunk{ptr[0] + start, ptr[1] != nullptr ? ptr[1] + start : nullptr};
        processChunk(chunk.data(), numCh, n);
    }

    meters_.inPeakDb.store(gainToDb(blockInPeak_), std::memory_order_relaxed);
    const float peakL = blockOutPeak_[0];
    const float peakR = numCh > 1 ? blockOutPeak_[1] : peakL; // mono: R ripete L
    meters_.outPeakDbL.store(gainToDb(peakL), std::memory_order_relaxed);
    meters_.outPeakDbR.store(gainToDb(peakR), std::memory_order_relaxed);
    meters_.outPeakDb.store(gainToDb(std::max(peakL, peakR)), std::memory_order_relaxed);
    meters_.ceilingActive.store(blockCeiling_, std::memory_order_relaxed);
}
} // namespace driftbox
