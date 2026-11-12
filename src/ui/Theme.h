// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_graphics/juce_graphics.h>

// Token grafici dell'editor, presi dal mockup approvato (docs/design/THEME.md). Tutte le misure sono in
// pixel del layout di base (720 x 440, zoom 100 %).
namespace driftbox::ui
{
// ---- Colori ----
namespace col
{
inline const juce::Colour bg0{0xff0f1113};    // fondo della finestra, campi incassati
inline const juce::Colour bg1{0xff161a1e};    // pannelli, barra alta e footer
inline const juce::Colour bg2{0xff1f2429};    // pulsanti, righe di statistiche
inline const juce::Colour bg3{0xff2b3138};    // tracce delle manopole, griglia centrale, hover
inline const juce::Colour ink{0xffe7eaee};    // testo principale
inline const juce::Colour ink2{0xffa0a8b2};   // testo secondario
inline const juce::Colour ink3{0xff808a96};   // etichette
inline const juce::Colour accent{0xfff2b866}; // ambra: canale L, archi, logo
inline const juce::Colour curveR{0xff9fb4c8}; // canale R
inline const juce::Colour bypass{0xffff8a7a}; // bypass attivo
inline const juce::Colour ghost{0xff5a6370};  // copia fantasma del logo
inline const juce::Colour menuBg{0xff2b3138}; // menu a comparsa
} // namespace col

// ---- Dimensioni ----
inline constexpr int kBaseWidth = 720;
inline constexpr int kBaseHeight = 440;
inline constexpr float kMinZoom = 0.75f;
inline constexpr float kMaxZoom = 2.0f;

inline constexpr int kHeaderH = 44;
inline constexpr int kFooterH = 22;
inline constexpr int kPad = 8;          // margine della zona centrale
inline constexpr int kGap = 6;          // spazio tra i pannelli
inline constexpr int kTopRowH = 214;    // riga "canale + tolleranza"
inline constexpr int kSidePanelW = 150; // pannelli a sinistra (canale, drift)
inline constexpr int kOutputPanelW = 118;
inline constexpr float kPanelRadius = 6.0f;
inline constexpr float kButtonRadius = 4.0f;

// ---- Colori derivati ----
inline juce::Colour gridLine()
{
    return col::bg2;
}
} // namespace driftbox::ui
