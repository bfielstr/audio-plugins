# Visual theme: aged copper + cinnabar

Design context for the plugin suite's UI, shared with the portfolio site so both read as one world.
This is a reference only: no plugin code follows it yet.

## The idea in one paragraph

A derelict ship seen through its own schematics. Almost everything is dark and still: a warm-black ground
with hair-thin, intricate copper linework. Energy is rare and arrives as an event: a hot pulse running
along a thin line, an element striking on, a brief glint. Power is unreliable, so things stutter on,
sag under load and re-strike. It should feel foreign, precise and a little menacing, never busy or bright.

## Palette

| Role | Hex | Use |
|---|---|---|
| Ground | `#070504` | Window / page background. Warm black, never pure `#000`. |
| Panel | `#0d0a08` | Slightly lifted surfaces. |
| Well | `#040302` | Recessed areas: meter beds, display wells, input fields. |
| Dim line | `#33291f` | Secondary structure, grids, inactive tracks, hatching. |
| Copper | `#9c6a4c` | Primary linework: outlines, rules, tick scales, knob bodies. Lines and large shapes only. |
| Pale copper | `#c79a7c` | Small labels and microtype that must stay readable on the ground. |
| Text | `#d4cdbf` | Body text, values, headings. Warm off-white. |
| Text dim | `#8f897c` | Secondary text, units, hints. |
| Energy idle | `#7a2c1c` | Dull cinnabar: an unlit indicator, an idle conduit, a meter at rest. |
| Energy live | `#dc4a2a` | Cinnabar: active state, signal present, the lit part of a meter. |
| Energy peak | `#ffb08a` | Hot head of a pulse, clip / overload, momentary strike. Tiny areas only. |

Pale copper and text dim are approximate; check contrast (4.5:1 for small text) against the surface you put
them on. Copper `#9c6a4c` fails for small text on the ground: use it for lines, not labels.

Rules:

- No brass, no pure red, no green, no blue. One metal (copper), one energy colour (cinnabar) in three states.
- Colour lives in what is lit. Structure is copper or dim; cinnabar appears only where there is signal,
  power or interaction.
- Nothing is permanently bright. Peak colour is for moments (a transient, a clip, a strike), not states.
- Most of any surface should be ground. Aim for a low average brightness with small, sharp bright areas.

## Line and form

- Lines are thin (1 px, 0.5 px on high-density displays) and numerous. Complexity comes from the amount of
  fine detail, not from line weight or filled shapes.
- Drafting language: tick scales, nested corner brackets, callout leaders ending in small terminals,
  dimension marks, fine hatching, concentric and offset contours. Think engineering callouts on a blueprint.
- Avoid filled plates, thick borders, drop shadows, gradients used as decoration, rounded "app" cards.
  Corner radii are small (2-3 px) or zero.
- Dense overlapping contour lines forming folded wireframe surfaces are welcome for hero elements.

## Type

- Display and body: **Archivo Variable**, using the width axis. Headings are large, condensed, heavy,
  uppercase, tightly tracked, in the text colour.
- Labels, values and microtype: **JetBrains Mono Variable**, small (about 11 px), uppercase, letter-spaced.
- Both are open-source (SIL OFL) and can be bundled.

## Glyphs

A procedural alien script is used as decoration: short runs of small stroke-built glyphs beside indices and
labels, as part markers. A label may appear first as glyphs and resolve into readable text. Glyphs are never
the only carrier of meaning; the real label is always present.

## Motion and energy

- **Sound drives light, not movement.** Audio lights things by frequency band, launches pulses along lines
  and makes elements strike. It does not rotate, scale, bounce or pump whole objects.
- **Pulses:** a hot (peak) head with a short cinnabar tail and a small local glow, travelling along a thin
  line, then the line returns to idle.
- **Lossy power:** on appearing, elements come up in stages with a few false starts (on, off, on, dim, full)
  in a randomised order. Under load, brightness sags and recovers. Individual small elements may buzz,
  sit half-lit, or drop out and re-strike.
- **Ambient motion** is slow and deliberate: drift, flow, slow mechanisms. Nothing syncs to the beat by moving.
- Not used: mirroring or kaleidoscope effects, glitch / tear / displacement lines, scanline overlays,
  bloom on everything, colour cycling.

Safety limits (photosensitivity):

- At most 3 large-area brightness changes in any one second; large-area changes ease over at least ~80 ms.
- Fast stutter is allowed only on small elements (an LED, a single line), never the whole surface.
- Dips go darker, never brighter. No full-surface flashes.
- With the OS "reduce motion" setting on, all of the above is off: static, steady rendering.

## Translating to plugin UIs

- **Window:** ground background; a thin copper frame with corner brackets; section dividers as dim hairlines
  with tick marks rather than boxes.
- **Knobs and faders:** drawn as thin copper outlines with a fine tick scale; the value arc or position
  marker is cinnabar live on a dim-line track. No filled caps, no skeuomorphic shading.
- **Meters:** well background; segments idle in energy idle, lit in energy live, peak / clip segment in
  energy peak with a short hold. Thin segments, many of them.
- **Value readouts:** mono type in text colour inside a thin copper bracket; units in text dim.
- **Buttons and toggles:** outlined, not filled. Off is copper outline with an idle-cinnabar marker; on lights
  the marker live and may run one pulse along the outline. Hover or focus runs a pulse along the control's edge.
- **Waveforms, spectra, curves:** hair-thin copper or text-coloured traces on the ground; grid in dim line;
  the active region or playhead in cinnabar. Transients may flash peak colour at the trace only.
- **Selection and focus:** cinnabar outline, 1-2 px, with a clear offset. Must be visible for keyboard use.
- **Errors and clipping:** peak colour briefly, then live cinnabar; pair with text so colour is not the only cue.
- **Bypassed / disabled:** everything drops to dim line and energy idle; text to text dim.

## Values for code

As `VSTGUI::CColor (r, g, b)`, with the constant in `shared/pluginkit/ui/Theme.h` each would replace.
The mapping is a suggestion: nothing in the plugin code has been changed.

```text
role          hex       CColor             Theme.h constant(s)
ground        #070504   (7, 5, 4)          kBackground
panel         #0d0a08   (13, 10, 8)        kPanel, kControlBg
well          #040302   (4, 3, 2)          kHeader, kWaveBg
line-dim      #33291f   (51, 41, 31)       kPanelEdge, kKnobTrack, kGrid, kWaveOutside
copper        #9c6a4c   (156, 106, 76)     outlines, tick scales, kWave (trace)
copper-pale   #c79a7c   (199, 154, 124)    small labels
text          #d4cdbf   (212, 205, 191)    kText, kTextBright, kSliceManual
text-dim      #8f897c   (143, 137, 124)    kTextDim
energy-idle   #7a2c1c   (122, 44, 28)      kAccentDim
energy-live   #dc4a2a   (220, 74, 42)      kAccent, kControlOn, kCurve, kLoop, kSliceAuto
energy-peak   #ffb08a   (255, 176, 138)    kPlayhead, clip / overload
```

Notes for this codebase:

- `Theme.h` currently uses several hues (orange accent, blue auto-slices, green loop, yellow playhead). This
  theme has one energy colour, so those distinctions need another carrier: line style (solid / dashed),
  position, a small glyph or label, or idle / live / peak brightness.
- `theme::font()` uses the platform UI face. Archivo and JetBrains Mono would have to be bundled and
  registered with VSTGUI to match the type described above; until then, keep the platform face and apply
  the uppercase, letter-spaced treatment to labels.
- Filled control backgrounds (`kControlBg`, `kControlOn`) become outlines with a lit marker rather than fills.

## Status

Copied from the portfolio repo (`docs/THEME.md` there). The portfolio is mid-way through adopting this
palette; its `src/theme/palette.ts` is the intended single source of truth. If the panel, pale-copper or
dim-text values there differ slightly from this file after contrast checks, that file wins.
