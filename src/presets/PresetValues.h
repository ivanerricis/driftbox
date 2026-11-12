// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>

namespace driftbox
{
// I SOLI parametri che un preset salva e ripristina. chanL, chanR, bypass (e le proprietà dello stato non
// parametriche: advancedOpen, uiWidth, uiHeight) non ne fanno mai parte: valgono per la singola traccia o per
// il progetto.
inline constexpr int kNumPresetParams = 7;

// ID dei parametri, nello stesso ordine di PresetValues.
inline constexpr const char* kPresetParamIds[kNumPresetParams] = {
    "drift", "freqTol", "phaseTol", "gainTol", "saturation", "noise", "outGain"};

// Valori denormalizzati, come quelli grezzi dell'APVTS: drift, freqTol, phaseTol, gainTol, saturation e noise
// in 0..1, outGain in dB. Stesso ordine di kPresetParamIds.
using PresetValues = std::array<float, kNumPresetParams>;

// Valori predefiniti dei parametri (un test verifica che coincidano con quelli dell'APVTS).
inline constexpr PresetValues kPresetDefaults = {0.5f, 0.5f, 0.5f, 0.5f, 0.2f, 0.0f, 0.0f};
inline constexpr float kPresetOutGainMinDb = -24.0f;
inline constexpr float kPresetOutGainMaxDb = 24.0f;
} // namespace driftbox
