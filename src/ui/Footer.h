// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ui/Fonts.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace driftbox::ui
{
// Piè di pagina (22 px): a sinistra il suggerimento del controllo sotto il mouse, a destra "CPU x.x % ·
// vX.Y.Z" (o solo la versione se il carico CPU è nascosto).
class Footer final : public juce::Component
{
public:
    static const char* defaultHint();

    // Testo vuoto = suggerimento predefinito.
    void setHint(const juce::String& text);
    const juce::String& hint() const noexcept { return hint_; }
    // Carico del processo audio come frazione 0..1 (mostrato in percentuale).
    void setCpu(float fraction);
    void setShowCpu(bool show);
    // Testo della parte destra, per i test.
    juce::String rightText() const;

    void paint(juce::Graphics&) override;

private:
    juce::String hint_{defaultHint()};
    juce::String cpu_{"CPU 0.0 %"};
    bool showCpu_ = true;
};
} // namespace driftbox::ui
