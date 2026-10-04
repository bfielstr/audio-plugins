# Visual theme: aged copper + cinnabar

Design context for the plugin suite's UI, shared with the portfolio site so both read as one world.
The plugins follow it (see Status at the end); `shared/pluginkit/ui/Theme.h` holds the values.

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
| Copper | `#9c6a4c` | Primary linework: outlines, rules, knob bodies. Lines and large shapes only. |
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
- Drafting language: nested corner brackets, callout leaders ending in small terminals, dimension marks,
  fine hatching, concentric and offset contours. Think engineering callouts on a blueprint. Tick scales
  only where they carry a value (a display's frequency or dB grid, a time ruler): purely decorative
  ticks on knobs and rules made the panels look busy and are gone.
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

- **Window:** ground background; a thin copper frame with corner brackets; the header band over one plain
  dim hairline; panel titles in small uppercase pale copper on plain space (no rule after them).
- **Knobs and faders:** drawn as thin copper outlines (a body circle and a pointer, no tick scale); the
  value arc or position marker is cinnabar live on a dim-line track. No filled caps, no skeuomorphic
  shading.
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

### Info box

Every editor has an info box along its bottom, in the manner of Live's Info View: a well in a dim
hairline that shows the name of whatever the mouse is over (in the text colour, at the left) and its
help (in text dim, wrapped to the box, four lines at most). A control bound to a parameter shows the
parameter's name; a display or button shows the title its editor gives it (`pk::setHelp`), or the short
lead-in of its help ("Presets: ..."). The texts are the same as the floating tooltips', so each help
text is written once (the plug-ins' `Help.h`). The info box is always there; the **?** in each header
switches only the floating tooltips, which are wrapped to about 50 characters a line.
`shared/pluginkit/ui/InfoBox.h` has it; `pk::EditorBase` adds it under every editor's content (the
window is `EditorBase::kInfoHeight` taller than the content, nothing above it moves) and feeds it the
view under the mouse.

### Layout

Controls, labels and value boxes must not overlap or touch, and a value must fit its box: outlines and
texts that run into each other are the clutter this theme avoids. `pk::layoutReport`
(`shared/pluginkit/ui/LayoutCheck.h`) lists every visible pair that overlaps or touches, where they draw
(a label's text, a knob's dial and texts, other controls' rectangles grown to any text that reaches out
of them), and every text wider than its box (measured in the theme's font at the ends of the value's
range, its default and now). A control placed wholly on a display (a readout, a switch in its corner)
is by design and not listed. Set `PK_LAYOUT_REPORT` to a file and every editor opened appends its list
there; the macOS host tests print it.

### Resizing

The window resizes to any shape (and Menu > Interface Size sets the proportional sizes, 75 % to 200 %).
The UI is never stretched: it is zoomed as large as fits both ways, between 50 % and 200 %, and centred,
with the ground in the margins. The zoom is what is remembered, so a window opens again in the UI's own
shape at that zoom.

## Values for code

As `VSTGUI::CColor (r, g, b)`, with the constant in `shared/pluginkit/ui/Theme.h` each would replace.
This is the mapping `Theme.h` now uses: the old names remain as aliases of the role constants.

```text
role          hex       CColor             Theme.h constant(s)
ground        #070504   (7, 5, 4)          kBackground
panel         #0d0a08   (13, 10, 8)        kPanel, kControlBg
well          #040302   (4, 3, 2)          kHeader, kWaveBg
line-dim      #33291f   (51, 41, 31)       kPanelEdge, kKnobTrack, kGrid, kWaveOutside
copper        #9c6a4c   (156, 106, 76)     outlines, rules, kWave (trace)
copper-pale   #c79a7c   (199, 154, 124)    small labels
text          #d4cdbf   (212, 205, 191)    kText, kTextBright, kSliceManual
text-dim      #8f897c   (143, 137, 124)    kTextDim
energy-idle   #7a2c1c   (122, 44, 28)      kAccentDim
energy-live   #dc4a2a   (220, 74, 42)      kAccent, kControlOn, kCurve, kLoop, kSliceAuto
energy-peak   #ffb08a   (255, 176, 138)    kPlayhead, clip / overload
```

Notes for this codebase:

- `Theme.h` used several hues (orange accent, blue auto-slices, green loop, yellow playhead). This
  theme has one energy colour, so those distinctions now have another carrier: line style (solid / dashed),
  position, a small glyph or label, or idle / live / peak brightness.
- `theme::font()` uses the platform UI face. Archivo and JetBrains Mono would have to be bundled and
  registered with VSTGUI to match the type described above; until then, keep the platform face and apply
  the uppercase, letter-spaced treatment to labels.
- Filled control backgrounds (`kControlBg`, `kControlOn`) become outlines with a lit marker rather than fills.

## Status

Copied from the portfolio repo (`docs/THEME.md` there). The portfolio is mid-way through adopting this
palette; its `src/theme/palette.ts` is the intended single source of truth. If the panel, pale-copper or
dim-text values there differ slightly from this file after contrast checks, that file wins.

All the plugins now follow this theme (October 2026):

- `shared/pluginkit/ui/Theme.h` has a constant for every role (`kGround`, `kPanel`, `kWell`, `kLineDim`,
  `kCopper`, `kCopperPale`, `kText`, `kTextDim`, `kEnergyIdle`, `kEnergyLive`, `kEnergyPeak`), three grid
  weights (`kGridMinor`, `kGridMajor`, `kGridZero`), the suite's dashed line style (`kDashed`) and
  `withAlpha`. The old names are kept as aliases, mapped as in the table above.
- The shared controls are linework: knobs are a copper body circle and pointer with a cinnabar value
  arc on a dim track (no tick scale); toggles, buttons and tabs are copper outlines with a lamp (energy
  idle off, energy live on); value boxes sit in a copper bracket with their units dim; panels are
  hairline frames with copper corner brackets and their title on plain space; the editor window has a
  copper frame with nested corner brackets and a plain dim hairline under the header. Display handles
  are rings that light cinnabar while held.
- In the displays, distinctions that used hue are carried by line style (solid / dashed, and for
  Smemplr's four LFOs solid / dashed / dotted / dash-dot), brightness (copper / pale copper / text,
  idle / live / peak), position and labels. Cuts and gain reduction being made, meters, the loop
  region, orbs and modulation are the lit parts; clipping and overload use the peak colour.

Not done yet:

- Bundling Archivo and JetBrains Mono (the platform face is still used; panel titles are uppercase, but
  VSTGUI has no letter spacing, so labels are not tracked).
- The procedural glyphs.
- All motion: power-on stutter, pulses along lines, hover and focus pulses. Nothing animates beyond what
  the meters and displays already did, so the photosensitivity limits are met trivially.
- Keyboard focus rings: the custom controls take no keyboard focus yet, so there is nothing to outline.
