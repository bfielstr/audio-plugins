# Smemplr

(Formerly Smempler: projects and presets carry over.)

A VST3 sampler instrument modelled on Ableton Live's **Simpler** (Live manual §31.11), built on the
Steinberg VST3 SDK + VSTGUI. It's aimed at REAPER, and works in any VST3 host on macOS, Windows and Linux. Install instructions are in the
[top-level README](../../README.md).

![Smemplr in Slicing mode](../../docs/smemplr/ui_slicing.png)

## What's implemented (mapped to the Simpler manual)

| Simpler | Smemplr |
|---|---|
| **Classic** mode: start/end flags, Start / Length, Loop on/off, loop Fade (constant-power or linear), Snap to zero crossings, Gain, Voices (1–32) with subtle voice stealing, Retrig | ✓ (Loop is on by default and begins at Start; **Length** is the loop's length and never shortens the sample, which plays to the end flag; what does not play is dimmed. **Fade**, off by default, crossfades the loop's end into its start and also fades the start in on the first pass) |
| **One-Shot** mode: monophonic, Trigger / Gate, Fade In / Fade Out, Snap | ✓ |
| **Slicing** mode: Slice by Transient (Sensitivity, ≤64) / Beat (Division) / Region / Manual; Mono / Poly / Thru playback; Trigger / Gate; fades per slice or to region end; slices mapped chromatically from C1 (MIDI 36) | ✓ |
| Slice editing: double-click adds a slice (a solid line; automatic ones are dashed) or removes one; drag to move; Alt-click toggles manual/auto | ✓ |
| **Warp** in every mode, locked to host tempo: Beats (Preserve, Transient Loop Mode, Envelope), Tones (Grain Size), Texture (Grain Size, Flux), Re-Pitch, Complex, Complex Pro (Formants, Envelope); "Warp as" with ÷2 / ×2 | ✓ (own algorithms, see below) |
| **Filter**: LP / HP / BP / Notch / Morph, 12/24 dB, circuits Clean / OSR / MS2 / SMP / PRD, Drive, Morph, Vel / Key / Env / LFO modulation, draggable response curve | ✓ |
| **Envelopes**: Amp / Filter / Pitch ADSR with draggable displays; amp loop modes None / Trigger / Loop / Beat / Sync with Time / Rate | ✓ plus up to 6 extra breakpoints (double-click) and per-segment curves (Shift+drag), all automatable; the Filter and Pitch envelopes' **Loop Lock** (Classic, Loop on): **Restart** starts the envelope again at every pass of the loop, **Fit** also stretches its attack, breakpoints and decay to exactly one pass (at the speed the note plays); Off by default |
| **LFO**: Sine / Square / Triangle / Saw Down / Saw Up / Random, Hz or tempo sync, Attack, Retrigger + Offset, Key, → Volume / Pitch / Pan / Filter, per voice | ✓ |
| **Global**: Pan, Random Pan, Spread (2 detuned voices L/R, decided at note-on), Volume, Vel→Vol, Transpose ±48, Detune ±50 ct, pitch bend (range adjustable, default ±5), Glide (mono legato) / Portamento (poly) + Time | ✓ plus a high-pass that follows the transposition (**HP**, see below) |
| Context menu: Normalize, Reverse, Crop, constant-power fade toggle, Show in Finder | ✓ (all non-destructive; the file on disk is never changed) |
| Sustain pedal (CC64) | ✓ |
| Sample stays at its original pitch on C3 (MIDI 60) | ✓ |

**The effects rack** (at the bottom): after the sampler, up to 8 effects in any order, and the same
effect as many times as you like. A new Smemplr starts with **smacheratr** in the first slot (on,
with Smacheratr's own defaults: Drive 0 dB, Pre-Limit on); it is a slot like any other, so it can be
moved, switched off or removed. **+** adds an effect at the end of the chain; the tabs show the chain
left to right: click a tab to show that effect, **drag a tab sideways** to move the effect (a cinnabar
bar shows where it will land; the effects in between move over); **Ctrl-drag** (Cmd on macOS) puts a copy
of it, with its settings, in the gap you let go on (the ones after it move up one); **Alt-click** a tab
(Option-click) removes that effect. For the selected one, **On**, **Remove** (the ones after it move
up), and **Copy** / **Paste**: the effect's settings as text on the clipboard, the same text the
effect's own plug-in copies and pastes (its **Menu → Copy Settings / Paste Settings**), so settings go
from a Para plug-in on a track into a para slot and back, or from one slot to another of the same
effect. Settings of another effect are ignored; the parameters the rack does not use (an effect's own
end saturator) are left out. Each has its own display and controls:

- **para**: the parallel high-pass / low-pass (with its Vocal movement), each filter's slope (6 to 96
  dB and Brickwall: HP Slope, LP Slope) and Gain Lock (HP Lock, LP Lock: a locked gain stops at 0 dB)
  beside the display, and a drive for each filter; its envelope is triggered by the notes played here,
  and its display shows the live spectrum.
- **multidyn**: the multiband dynamics, with its Style (OTT, a measured model of Xfer's OTT, or
  Character, Multidyn's own; Soft Knee, Peak/RMS, the RMS window and Soften look disabled in OTT,
  where they do nothing), its lanes and band fields, the crossovers' Slope, Soften's Color and the Sub
  band (on, where it tapers, and its own lane while it is on). A new slot starts in OTT; slots saved
  before Style existed open in Character, so they keep their sound.
- **m/s eq**: a high-pass on the side signal (default 150 Hz) tapers the sides so the low end is mono
  below the cutoff, plus side and mid levels; live meters. Its slope: 6, 12, **24** (default), 36, 48,
  60, 72, 84 or 96 dB per octave (Butterworth above 6 dB, -3 dB at the cutoff), or **Brickwall** (a
  16th-order Chebyshev: flat to the cutoff within 0.05 dB, about -40 dB a tenth below it, -70 dB at
  0.8 x). The mouse wheel on the display's handle (while holding it, or with Shift) steps through them.
  The mid is never filtered, so the mono sum is untouched.
- **smacheratr**: the full saturator (pre-limiter, Gently with its Advanced mode, its Sub and High bands and No Overlap,
  Mid/Side, colour, post clip). Slots saved before the High band (state version 17) load with it and No Overlap off.
  The Sub and High bands have no buttons (here and in gently slots): they work while their Range is above 0 dB and
  start at 0 dB; slots saved before that (state version 19) load with a band that was off at Range 0.
- **widr**: the stereo widener with its left and right voices (it works alone here: the group
  awareness needs its own plug-in instances).
- **wubr**, **levlr** (with Bands and each band's Drive and curve), **gently** (its two bands, Sub
  and High bands on its display, a row of values for each, No Overlap, Advanced and its region Drive) and **smoothr** (its
  gain-reduction history and its limiter's controls; its own saturator before the limiter is off in
  the rack: put a smacheratr slot before it for that). Switched off, an effect keeps its latency.

Projects from before 0.7 keep their sound: a para slot's slope stays what it was and its one drive
becomes both filters' drives; a para slot from before the separate slopes gets its one slope on both
filters, its high-pass Gain Lock on unless its high-pass gain is above 0 dB (then off), its low-pass
Gain Lock off, and keeps its Fade in semitones (Para's Fade range grew from 1 .. 36 to 1 .. 60); a multidyn slot's gains move into its controls for the OTT gain staging
(see Multidyn's README), and a levlr slot keeps 4 bands without drive.

Every control is an automatable parameter (the host shows a slot's values in the units of the effect
loaded there), and the latency of the effects in the rack is reported to the host, which is told when
it changes. The **OUTPUT** scope on the right shows the final output (click it to change the time
span). Projects from 0.5 keep their Para, Multidyn and M/S EQ: they load into the first slots.

Before 0.9 a fixed **end: smacheratr** saturator sat after the rack. Projects saved with it on load
with a smacheratr slot at the end of their chain instead (the first free slot after the last effect,
with the same settings); projects with it off get nothing added. If such a project's rack is full,
the old saturator keeps running after the rack as before, and the rack's control line shows **old
end saturator**: once the last slot is free, click it to move the saturator into the rack. (Its
parameters stay in the host's list, named "Old End Saturator ...", so old automation still loads.)
The M/S EQ slopes of older projects (6 / 12 / 24 dB) stay what they were.

**Modulation** (the column at the right): four LFOs that can move any knob, slider or value field of
Smemplr, the effects in the rack included. Each LFO has a **Shape** (Sine, Triangle, Saw Up, Saw Down,
Square, S&H: a new random value every cycle, or Smooth Random: gliding from one random value to the
next), a **Rate** in Hz (0.01 to 40), **Sync** (Off, or a note length from 1/64 to 8 Bars at the host's
tempo, 120 BPM when the host gives none; while the host plays, a synced LFO follows the song position,
so the same place in the bar always sounds the same), a **Phase** (0 to 360 degrees) and **Retrig**
(every note starts the LFO again at its Phase). These are ordinary automatable parameters
("Mod LFO 1 Rate" and so on).

To modulate a control, **drag the LFO's handle** (the "LFO 1" button) onto it: the control is framed in
the LFO's colour while you are over one that can be modulated, and let go there, the mapping is made
with a depth of +25 %. A modulated knob gets a ring in the LFO's colour showing how far the LFO moves it
from its value (a dot shows where it is now); any other control gets a line under it. **Drag a knob's
ring** up or down to change the depth (Shift: fine), **right-click the ring** to remove the mapping (a
right-click on the knob itself still resets the knob, as everywhere in the suite). The **MAPPINGS**
list shows every mapping as LFO, parameter and depth: drag a depth up or down to change it, double-click
it to turn it over (+ / -), click its **x** or right-click the row to remove it. Several LFOs can move
the same control (their depths add up); switches and menus cannot be modulated, nor can the LFOs
themselves. Up to 24 mappings.

The depth is a share of the control's whole range (-100 % to +100 %), added to the control's own value
and held to its range. The host's value is never changed: automation and the control show the
parameter itself, and the LFO moves the sound around it, updated every 32 samples and smoothed over a
few milliseconds so it does not zipper. A mapping onto an effect in the rack belongs to that effect: it
moves with it when the effect is dragged to another place, goes when the effect is removed, and pauses
(dimmed in the list) while another effect is loaded into its slot. The mappings are saved with the
project and in presets; projects from before them load with none and sound exactly as before.

**Root Note** (Global panel): the note on which the sample plays at its own pitch (C3 by default).
**Voices** defaults to 1.

**Transposing far up** stays clean: a sample read many times faster than real time (+48 semitones
reads 16 samples for each one played) needs its top octaves removed first, or they fold down across
the spectrum as aliasing, a lot of it into the low end. When a sample loads, Smemplr makes
band-limited copies of it at 1/2 to 1/64 of its rate (about as much memory again), and a fast read
takes the copy that suits its speed, crossfading into the next one over the last quarter octave
before it, so a pitch bend, glide or LFO moves smoothly across them. Up to +9 semitones (at the
sample's own rate) the sample is read exactly as before. Every mode benefits; Complex and Complex Pro
pick their copy when the note starts (with room for about an octave of bend up), and Complex Pro
still takes its formants from the sample itself (a copy holds only the bottom of its spectrum). Read
far up, the phase vocoder also costs less: its frames are the copy's, a fraction of the size.

**HP** (Global panel, under Transpose and Detune, off by default): a high-pass whose cutoff follows the
transposition, so what was below the audible range in the sample (rumble, a DC drift) stays out of
the way when it is transposed up. The frequency is the cutoff at 0 semitones (10 to 200 Hz, 20 Hz by
default); it moves with Transpose, Detune, pitch bend, the pitch envelope and the LFO's pitch (not with
the key played), gliding a few milliseconds so a bend or a jump does not click. Transposed down it
simply goes below the audible range. The slope is Para's: 6 or 18 dB (-3 dB at the cutoff), 12, 24,
36 or 48 dB (Linkwitz-Riley, -6 dB at the cutoff); 24 dB by default.

**Drop a clip from your DAW** onto the waveform to load it (a clip dragged out of REAPER or Ableton
Live, or a file from Finder / Explorer). A DAW's drag carries the clip's name as text beside its audio
file; the file is what gets loaded. A temporary file (a render the DAW deletes later) is copied to
Documents/bfielstr/Samples first.

Extras: hover tooltips for every control (**?** toggles them), a clickable/draggable loop bar in
the waveform (the shaded loop above it drags too; a click there still auditions), audition by clicking the waveform (plays the slice under the mouse in Slicing mode),
◀ ▶ step through the samples in the current folder, zoom (Cmd/Alt + scroll, or drag the ruler
vertically) and pan (scroll, or drag the ruler horizontally), live playheads, resizable UI (drag
the window corner, or Menu → Interface Size).

## Differences from Live's Simpler

- **Warp algorithms are my own implementations**, not Ableton's (which are proprietary): Beats =
  transient-segmented playback, Tones = WSOLA-aligned grains, Texture = scattered grains, Complex =
  phase-locked phase vocoder, Complex Pro = the same plus cepstral formant correction. They're
  tempo-accurate and tested, but they won't sound identical to Live. Live's manual warp markers
  aren't supported, since there's no clip to take them from.
- The filter circuits are my models of the described behaviour (hard-clipped SVF, soft-clipped
  SVF, ladder variants), not Cytomic's code.
- Live-only features are left out: Slice to Drum Rack / New MIDI Track, Simpler → Sampler conversion.
- Samples are referenced by path in the project (like most samplers). If a file moves, Smemplr
  shows "Missing" but keeps the reference so it isn't lost when you save.
- No MPE yet. Mod wheel is mapped but not routed anywhere.

## License

Smemplr is released under the [MIT License](LICENSE). It builds on the Steinberg VST 3 SDK (MIT),
VSTGUI (BSD 3-clause) and dr_libs (public domain / MIT-0); see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
