// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new driftbox::DriftboxAudioProcessor();
}
