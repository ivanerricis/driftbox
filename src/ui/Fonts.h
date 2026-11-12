// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ui/Theme.h"
#include <juce_graphics/juce_graphics.h>

namespace driftbox::ui::fonts
{
// Tipografia incorporata nel plugin (IBM Plex Sans / IBM Plex Mono, SIL OFL 1.1, vedi
// THIRD_PARTY_NOTICES.md). Se un carattere non si carica si usano quelli di sistema.
enum class Weight
{
    regular,
    medium,
    semiBold
};

// `px` è la dimensione in "px CSS" del mockup; `tracking` è la spaziatura tra lettere in em.
juce::Font sans(float px, Weight w = Weight::regular, float tracking = 0.0f);
juce::Font mono(float px, Weight w = Weight::regular, float tracking = 0.0f);

// Vero se i caratteri incorporati sono stati creati correttamente.
bool embeddedFontsLoaded();
} // namespace driftbox::ui::fonts
