# Driftbox UI theme

Source of truth: the approved mockup (720 x 440 at 100 %). The tokens live in `src/ui/Theme.h`; this file
documents them and the component sizes used by the editor. All sizes are pixels of the base layout; the editor
scales the whole content uniformly (zoom 75 % to 200 %, fixed aspect ratio 720:440).

## Palette

| Token | Value | Use |
|---|---|---|
| `bg0` | `#0F1113` | window background, inset fields (channel number, plot, meter) |
| `bg1` | `#161A1E` | panels, top bar, footer |
| `bg2` | `#1F2429` | buttons, stat rows, knob discs, grid lines |
| `bg3` | `#2B3138` | knob tracks, 0 dB line, hover, menus |
| `ink` | `#E7EAEE` | primary text, pointer, output arc |
| `ink2` | `#A0A8B2` | secondary text, icons |
| `ink3` | `#808A96` | captions and axis labels |
| `accent` | `#F2B866` | channel L, knob arcs, logo, selector arrows |
| `curveR` | `#9FB4C8` | channel R |
| `bypass` | `#FF8A7A` | active Bypass, BYPASSED badge |
| `ghost` | `#5A6370` | rotated back copy of the logo mark |

## Typography

IBM Plex Sans (Regular, Medium, SemiBold) and IBM Plex Mono (Regular, Medium), embedded from
`resources/ui/fonts` (SIL OFL 1.1, licence in `resources/ui/fonts/licenses`). Sizes are CSS-like px as in the mockup.

| Element | Font |
|---|---|
| Wordmark `DRIFTBOX` | Sans SemiBold 13, tracking 0.14 em |
| Panel titles (`CONSOLE CHANNEL`, `ADVANCED`, ...) | Sans Medium 10, tracking 0.08 em, `ink3` |
| Knob captions | Sans Medium 10, tracking 0.06 em (Drift: 0.08 em), `ink3` |
| Knob values | Mono 10.5, `ink2` (Drift: Mono Medium 12, `ink`) |
| Channel number | Mono Medium 20 |
| Buttons (Bypass, Randomize) | Sans SemiBold 11, tracking 0.06 em; Show/Hide: 10 |
| Preset name | Sans Medium 12.5 |
| Axis labels, stats | Mono 9 / Mono 10 |
| Footer hint / right text | Sans 11 / Mono 10.5 |

## Layout (base 720 x 440)

| Area | Size |
|---|---|
| Top bar | 44 high, 10 side padding, 8 gap between items |
| Footer | 22 high, 12 side padding |
| Main area | padding 8, gap 6 between panels |
| Top row (channel + tolerance) | 214 high |
| Console channel panel | 150 wide, radius 6, padding 10, items 9 apart |
| Tolerance panel | rest of the row, radius 6, padding 10, gap 6 |
| Bottom row | remaining height (138) |
| Drift panel | 150 wide |
| Advanced panel | rest of the row, padding 8 / 6 |
| Output panel | 118 wide |

Top bar items: logo 22 x 22; preset bar 26 + 158 + 26 + 26 (2 apart) x 28; Undo, Redo 28 x 28; Bypass 28 high
with 10 horizontal padding; Gear 28 x 28 at the right edge.

Channel selector: arrow buttons 24 x 36, number box 52 x 36, 4 apart (108 wide); Randomize 108 x 28.

Knobs (270 degree sweep, start at 7:30): Drift 76 px, track/arc stroke 5, disc radius 24; others 46 px,
stroke 4, disc radius 13; pointer from 0.28 r to 0.62 r, stroke half the arc stroke. Arc colour `accent`,
except Output (`ink`). Meters: two bars 6 x 92, 3 apart, fill `accent` at 85 %.

Corner radii: panels 6, buttons 4, Show/Hide and the BYPASSED badge 3, plot 4.

## Tolerance graph

- Vertical axis +-3 dB (labels `+3`, `0`, `-3`), horizontal lines every 1 dB, the 0 dB line in `bg3`.
- Horizontal axis 20 Hz to 20 kHz, logarithmic; labels `20 Hz`, `200`, `2 k`, `20 kHz` are spread evenly.
- R curve 1.6 px, L curve 1.8 px (L on top). Curves are clipped at 1.5 x the vertical range.
- When Bypass is on, labels and plot are drawn at 35 % opacity and a BYPASSED badge appears in the header.
- The curve is the small-signal magnitude deviation of the channel chain (low shelf, bell, high shelf and
  gain; the all-pass has unit magnitude, saturation does not change small-signal level). It is computed by
  `toleranceDeviationDb()` in `src/dsp/ToleranceResponse.h` from the same design values the engine uses
  (`DriftChannel::design()`), with amounts `drift x freq`, `drift x phase`, `drift x gain`,
  `drift x saturation`. At Drift 0 it is exactly 0 dB.
- Stats per side: `gain` is the broadband gain in dB, `delay` is the all-pass delay in microseconds, and
  `drive` is the channel's saturation-drive spread (up to +-15 %), scaled by Drift x Saturation, so it is 0 when
  either is 0.

## Hints

The footer shows the tooltip text of the control under the mouse; the default hint is
"Give every track its own channel numbers. At Drift 0 % Driftbox is transparent." The same texts are also
shown as a tooltip window (switchable with `setTooltipsEnabled()`).
