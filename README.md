# Audio plug-ins: smemplr, multidyn, locus, stretchr, smacheratr, para, widr, wubr, levlr, deepr, smoothr, gentlr, dropr, orbitr

VST3 plug-ins for REAPER, Ableton Live and any other VST3 host on **macOS, Windows and Linux**. MIT
licensed. The current version is **0.12.0**.

| Plug-in | What it does for you |
|---|---|
| [**smemplr**](plugins/smemplr/README.md) | Plays a sample from the keyboard. Loop it, slice it into hits, or warp it to the song tempo. A multi-mode filter, envelopes you can draw, an LFO, four modulation LFOs you drag onto any control, and an effects rack with up to 8 of the suite's effects in any order. |
| [**multidyn**](plugins/multidyn/README.md) | Squashes or opens up 1 to 4 frequency bands at once: quiet detail comes up and loud parts are held down, band by band. The **OTT** style gives the loud, dense, over-the-top sound; **Character** is smoother. Side-chain input and a Sub band. |
| [**locus**](plugins/locus/README.md) | Cleans up or thickens the low end. Positive Contrast brings the main bass notes forward and pushes the mud between them down; negative Contrast evens the low end out for weight. |
| [**stretchr**](plugins/stretchr/README.md) | Changes the pitch and timing of a clip recorded on a track: 8 algorithms, stretch markers, a drawn pitch envelope and formant control. Bounce the result in place or drag it to a track. |
| [**smacheratr**](plugins/smacheratr/README.md) | Adds warmth, grit or hard clipping. A pre-limiter stops transients from clipping harder than the rest, colour filters choose which frequencies saturate, and gentlr keeps a pushed sound from turning muddy or harsh. |
| [**para**](plugins/para/README.md) | A high-pass and a low-pass side by side. Pull them apart to carve a notch, then sweep it, fire it from MIDI notes, or use the **Vocal** movement, where the filter you move leads and fades the other out. Good for bass movement and filter sweeps. |
| [**widr**](plugins/widr/README.md) | Makes a part very wide without falling apart in mono. Widrs in the same project share out the stereo field by role, so they do not all widen the same place. |
| [**wubr**](plugins/wubr/README.md) | Two EQ bands whose level and frequency follow shapes you draw: wubs, pumps and sweeps, synced to the song or free, or fired once by MIDI or by hits in the audio. |
| [**levlr**](plugins/levlr/README.md) | Splits the sound into up to 4 bands and gives each a level and a saturator (Analog, Tape, Tube, Hard Clip, Fold). Lift the lows, dip the low mids, dirty up only the highs. |
| [**deepr**](plugins/deepr/README.md) | Makes a bass sound deeper without making it louder: dips the low mids only while the sub plays, and folds the sub to mono. No latency of its own. |
| [**smoothr**](plugins/smoothr/README.md) | A loudness limiter for the master or a bus that keeps the low end clean: the lows get a slow, smooth gain and the highs catch the fast peaks. True-peak ceiling and a scrolling gain-reduction history. |
| [**gentlr**](plugins/gentlr/README.md) | Keeps a mix clear by turning down mud and harshness only while they build up. Two bands plus a Sub and a High band, each cutting its region only while it is loud. |
| [**dropr**](plugins/dropr/README.md) | Slams a sound flat in 6 bands and drives it into a saturator. Negative ratios turn loud hits down below quieter parts, so a snare's body comes up and its snap is tamed. |
| [**orbitr**](plugins/orbitr/README.md) | Turns a sound into a swarm: 1 to 16 copies fly around you, each one bending in pitch as it moves towards or away from you. |

## Gallery

| | |
|---|---|
| [<img src="docs/smemplr/ui_classic.png" width="420" alt="smemplr">](plugins/smemplr/README.md)<br>smemplr | [<img src="docs/multidyn/ui_multidyn.png" width="420" alt="multidyn">](plugins/multidyn/README.md)<br>multidyn |
| [<img src="docs/locus/ui_locus.png" width="420" alt="locus">](plugins/locus/README.md)<br>locus | [<img src="docs/stretchr/docs.png" width="420" alt="stretchr">](plugins/stretchr/README.md)<br>stretchr |
| [<img src="docs/smacheratr/ui_smacheratr.png" width="420" alt="smacheratr">](plugins/smacheratr/README.md)<br>smacheratr | [<img src="docs/para/ui_para.png" width="420" alt="para">](plugins/para/README.md)<br>para |
| [<img src="docs/widr/ui_widr.png" width="420" alt="widr">](plugins/widr/README.md)<br>widr | [<img src="docs/wubr/ui_wubr.png" width="420" alt="wubr">](plugins/wubr/README.md)<br>wubr |
| [<img src="docs/levlr/ui_levlr.png" width="420" alt="levlr">](plugins/levlr/README.md)<br>levlr | [<img src="docs/deepr/ui_deepr.png" width="420" alt="deepr">](plugins/deepr/README.md)<br>deepr |
| [<img src="docs/smoothr/ui_smoothr.png" width="420" alt="smoothr">](plugins/smoothr/README.md)<br>smoothr | [<img src="docs/gentlr/ui_gentlr.png" width="420" alt="gentlr">](plugins/gentlr/README.md)<br>gentlr |
| [<img src="docs/dropr/ui_dropr.png" width="420" alt="dropr">](plugins/dropr/README.md)<br>dropr | [<img src="docs/orbitr/ui_orbitr.png" width="420" alt="orbitr">](plugins/orbitr/README.md)<br>orbitr |

## What every plug-in has

- **smacheratr at the end.** Every plug-in can end its chain with smacheratr, with all its controls
  (off by default, Drive 0 dB). That includes its **gentlr**: two bands, a **Sub** band (from 20 Hz up
  to a point of 20 to 100 Hz) and a **High** band (from a point of 2 to 16 kHz up). The Sub and High
  bands are always there and start at Range 0 dB, so they cut nothing until you pull them down.
  gentlr's **Slope**, **No Overlap**, band glue and **Advanced** mode (a Threshold per band and a Drive
  for the region it cuts) are there too.
- **Info box.** A strip along the bottom of every window. Point at a control or a display and it says
  what it is and what it does. The **?** in the header switches the floating tooltips on and off; the
  info box is always there.
- **Free resizing.** Drag the window corner to any shape. The interface keeps its proportions, zoomed
  to fit and centred, never stretched. **Menu > Interface Size** sets the usual sizes.
- **Reset and fine control.** A right click (or a double-click) on a control or a display handle puts
  it back to its default. Hold Shift while dragging for fine steps. The mouse wheel on a filter handle,
  while you hold it or with Shift over it, sets its resonance.
- **Copy Settings / Paste Settings** in every plug-in's **Menu** (and **Copy** / **Paste** on each
  effect's page in smemplr's rack) put the settings on the clipboard as text. Paste them into the same
  plug-in on another track, or between a plug-in and the same effect in smemplr's rack, both ways.
- **Presets** in the header of every plug-in: **Init** (every control at its default) always at the
  top, a few factory presets, your own presets in categories with tags you can filter by, and **Save
  as Default** so a new instance starts the way you like it (see below).
- The copper and cinnabar look described in [docs/THEME.md](docs/THEME.md).

Older names: smemplr was called simplr and locus was called lowfocus until 0.5.0. gentlr was called
gently before 0.12. smacheratr's gentlr was called Clarity. Projects keep loading: the plug-in IDs are
unchanged.

## Install

### Download the installer for your system

- **macOS** (Apple Silicon and Intel, macOS 11 or newer):
  [Plugins-macOS.pkg](https://github.com/bfielstr/audio-plugins/releases/latest/download/Plugins-macOS.pkg)
- **Windows** (64-bit, Windows 10 or newer):
  [Plugins-Windows-x64-Setup.exe](https://github.com/bfielstr/audio-plugins/releases/latest/download/Plugins-Windows-x64-Setup.exe)

Double-click the file and follow the steps. You can untick any plug-ins you do not want. They are
installed for every user on the computer, into `/Library/Audio/Plug-Ins/VST3/bfielstr` on macOS and
`C:\Program Files\Common Files\VST3\bfielstr` on Windows. Running a newer installer replaces the
older versions. Copies under the older names above (such as `Gently.vst3`) are removed, and so are
copies the one-line scripts put in your own user folder, so nothing shows up twice. On Windows,
uninstall from *Settings → Apps → Installed apps → Audio plug-ins (bfielstr)*.

### If your computer will not open the installer

The installers are not signed yet, so your computer may warn you the first time you open one. These
steps tell it that you trust the file you downloaded from this page.

- **macOS** says the installer "cannot be opened" or "cannot verify the developer": in Finder,
  right-click (or Control-click) the file and choose *Open*, then click *Open* again. If there is no
  *Open* button, go to *System Settings → Privacy & Security*, scroll down and click *Open Anyway*
  next to the message about the installer. Then enter your password.
- **Windows** shows a blue "Windows protected your PC" box: click *More info*, then *Run anyway*.

### One-line install scripts

If you prefer the command line, these do the same for your user only and also work on Linux.

**macOS** (universal: Apple Silicon + Intel) **and Linux** (x86_64):

```sh
curl -fsSL https://raw.githubusercontent.com/bfielstr/audio-plugins/main/scripts/install.sh | sh
```

**Windows** (x64), in PowerShell. Run it as Administrator to install into
`C:\Program Files\Common Files\VST3`; otherwise it installs for your user only:

```powershell
irm https://raw.githubusercontent.com/bfielstr/audio-plugins/main/scripts/install.ps1 | iex
```

The scripts download the latest [release](https://github.com/bfielstr/audio-plugins/releases), check
its SHA-256 checksum and install the `.vst3` bundles into a **`bfielstr`** folder inside the standard
VST3 folder (e.g. `~/Library/Audio/Plug-Ins/VST3/bfielstr/`). Running a script again replaces the
installed versions. Copies that older installers put directly in the VST3 folder, and bundles under the
older names above, are removed. Only ours are touched: the vendor in the bundle is checked.

Options (environment variables): `SIMPLR_PLUGINS="Multidyn Locus"` installs only some plug-ins,
`SIMPLR_VERSION=v0.12.0` picks a release, `SIMPLR_DEST=...` chooses the VST3 folder.

### Install by hand

Download the zip for your system from the
[latest release](https://github.com/bfielstr/audio-plugins/releases/latest), unzip it and copy the
`.vst3` bundles you want into a `bfielstr` folder inside your VST3 folder: `/Library/Audio/Plug-Ins/VST3`
or `~/Library/Audio/Plug-Ins/VST3` on macOS, `C:\Program Files\Common Files\VST3` on Windows,
`~/.vst3` on Linux.

### After installing

In REAPER: *Options → Preferences → Plug-ins → VST → Re-scan*. In Live: *Settings → Plug-ins →
Rescan* (with *Use VST3 Plug-in System Folders* on).

## Presets

Every plug-in has a **Presets** menu in its header. From the top:

- **Init**: every control back to its default, as a new instance with no saved default. It is always
  there and cannot be overwritten or deleted.
- **Factory** presets that come with the plug-in, in sub-menus by category. They cannot be changed
  either: save a copy under your own name instead.
- **User**: your presets. A preset saved with a category goes into a sub-menu of that name.
- **Tags**: pick a tag to see only the presets that carry it (factory ones included); **All** shows
  everything again.
- **Save** (over the current preset, if it is one of yours), **Save As...** (a name, an optional
  category and tags, separated by commas), **Rename...**, **Edit Tags...** and **Delete...**.
- **Save as Default** stores the current settings as the plug-in's default: every new instance starts
  from them, and **Load Default** brings them back. **Reset Default** goes back to the factory
  defaults. A project you open still loads exactly as it was saved.
- **Save Preset File...** and **Load Preset File...** save or open a `.vstpreset` file anywhere.

Presets are standard `.vstpreset` files, so hosts can load them as well. Tags and the category are
stored in the file's metadata, and files from older versions load as before (they just have no tags).
They live in

| | |
|---|---|
| Windows | `Documents\VST3 Presets\bfielstr\<plug-in>` |
| macOS | `~/Library/Audio/Presets/bfielstr/<plug-in>` |
| Linux | `~/.vst3/presets/bfielstr/<plug-in>` |

with one sub-folder per category. The saved default is the hidden file `.default.vstpreset` in the
same folder: delete it (or use **Reset Default**) to go back to the factory defaults.

The factory presets are text files in `plugins/<plug-in>/presets/<category>/` in the source, built
into the plug-in. Each line sets one control by its name, as the plug-in shows its value (for example
`Band 1 Range = 6 dB`); controls a file does not name stay at their defaults. The format is described
in [shared/pluginkit/PresetStore.h](shared/pluginkit/PresetStore.h).

## Build from source

```sh
git clone https://github.com/bfielstr/audio-plugins.git && cd audio-plugins
cmake -B build -DCMAKE_BUILD_TYPE=Release   # first run downloads the VST3 SDK into external/
cmake --build build --config Release        # also runs Steinberg's VST3 validator on each plug-in
ctest --test-dir build -C Release           # DSP tests (+ plug-in host tests on macOS)
```

- **macOS**: Command Line Tools are enough (no Xcode needed). `scripts/build.sh` builds, tests and installs.
- **Linux**: needs `libx11-dev libx11-xcb-dev libxcb-util-dev libxcb-cursor-dev libxcb-keysyms1-dev
  libxcb-xkb-dev libxkbcommon-dev libxkbcommon-x11-dev libfontconfig1-dev libcairo2-dev
  libfreetype6-dev libpango1.0-dev libgtkmm-3.0-dev` (Debian/Ubuntu names; gtkmm is only for the SDK's test hosts).
- **Windows**: Visual Studio 2022 with the C++ workload; use `cmake -B build -A x64`.
- **Draw benchmark** (Linux): `-DPK_DRAW_BENCH=ON` adds `<plug-in>_drawbench` (also run by ctest): it plays
  a test signal through each plug-in and times its editor's repaints with and without the cached display
  layers, checks both give the same pixels, and counts the repaints still asked for once the audio stops.

Every push is built and tested on all three platforms by GitHub Actions; pushing a `v*` tag
publishes a release and then runs the installers against it. The screenshots in `docs/<plug-in>/`
come from the macOS host tests.

## Layout

```
plugins/smemplr     sampler                  src/core (DSP) · src/plugin (VST3) · src/ui · tests · presets
plugins/multidyn    multiband dynamics       same structure
plugins/locus       low-end contrast         same structure
plugins/stretchr    pitch/time editor        same structure (reuses Smemplr's warp engines)
plugins/smacheratr  saturator                same structure
plugins/para        parallel filters         same structure
plugins/widr        stereo width             same structure
plugins/wubr        drawn band LFOs          same structure
plugins/levlr       four-band levels         same structure
plugins/smoothr     low-end-first limiter    same structure
plugins/deepr       bass depth by contrast   same structure
plugins/gentlr      gentle de-mud / de-harsh same structure
plugins/dropr       multiband compressor     same structure
plugins/orbitr      doppler swarm            same structure
shared/pluginkit    code all plug-ins share: parameter tables, VST3 controller/editor bases,
                    the preset store, VSTGUI widgets, the instance registry, the macOS host-test harness
cmake/PluginKit.cmake   SDK fetch + pk_add_plugin() / pk_add_host_test()
cmake/EmbedPresets.cmake  builds a plug-in's presets/*.txt into it (its factory presets)
scripts             build.sh, install.sh, install.ps1
installer           the double-click installers: macos (.pkg), windows (Inno Setup), their CI tests
```

Each plug-in keeps its DSP free of any framework so it is tested headlessly (`*_tests`); the
`*_hosttest` programs load the built bundles through the VST3 hosting API, play audio/MIDI, check
state, drive the real editor with synthetic mouse events and save screenshots.

## Credits and licences

The plug-ins are MIT licensed (see [LICENSE](LICENSE)). They are built on the Steinberg VST 3 SDK
(MIT licence) and VSTGUI (BSD 3-clause licence). smemplr reads audio files with dr_wav, dr_flac and
dr_mp3 by David Reed (public domain / MIT-0). multidyn's OTT style uses the constants and laws of David
Braun's fit of OTT (`co.xfer_ott` in the Faust libraries' `compressors.lib`, MIT licence). The full
notices are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries. OTT
is a product of Xfer Records; multidyn is not affiliated with or endorsed by Xfer Records.
