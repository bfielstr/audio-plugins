# Audio plug-ins: Simplr, Multidyn, Lowfocus

VST3 plug-ins for REAPER (and any VST3 host) on **macOS, Windows and Linux**, built on the
Steinberg VST3 SDK and VSTGUI. MIT licensed.

| Plug-in | What it is | Modelled on |
|---|---|---|
| [**Simplr**](plugins/simplr/README.md) | Sampler instrument: Classic / One-Shot / Slicing, tempo-synced warping, multi-circuit filter, breakpoint envelopes, LFO | Ableton Live's Simpler |
| [**Multidyn**](plugins/multidyn/README.md) | Multiband upward/downward compressor-expander with side-chain | Ableton Live's Multiband Dynamics |
| [**Lowfocus**](plugins/lowfocus/README.md) | Low-end contrast: brings the dominant bass events into focus or thickens the low end | iZotope Ozone's Low End Focus |

![Simplr](docs/simplr/ui_slicing.png)

## Install

**macOS** (universal: Apple Silicon + Intel) **and Linux** (x86_64):

```sh
curl -fsSL https://raw.githubusercontent.com/bfielstr/simplr/main/scripts/install.sh | sh
```

**Windows** (x64), in PowerShell. Run it as Administrator to install into
`C:\Program Files\Common Files\VST3`; otherwise it installs for your user only:

```powershell
irm https://raw.githubusercontent.com/bfielstr/simplr/main/scripts/install.ps1 | iex
```

The installers download the latest [release](https://github.com/bfielstr/simplr/releases), verify
its SHA-256 checksum and copy the `.vst3` bundles into the standard VST3 folder. Options (environment
variables): `SIMPLR_PLUGINS="Multidyn Lowfocus"` installs only some plug-ins, `SIMPLR_VERSION=v0.2.0`
picks a release, `SIMPLR_DEST=...` chooses the folder.

Then in REAPER: *Options → Preferences → Plug-ins → VST → Re-scan*.

## Build from source

```sh
git clone https://github.com/bfielstr/simplr.git && cd simplr
cmake -B build -DCMAKE_BUILD_TYPE=Release   # first run downloads the VST3 SDK into external/
cmake --build build --config Release        # also runs Steinberg's VST3 validator on each plug-in
ctest --test-dir build -C Release           # DSP tests (+ plug-in host tests on macOS)
```

- **macOS**: Command Line Tools are enough (no Xcode needed). `scripts/build.sh` builds, tests and installs.
- **Linux**: needs `libx11-dev libx11-xcb-dev libxcb-util-dev libxcb-cursor-dev libxcb-keysyms1-dev
  libxcb-xkb-dev libxkbcommon-dev libxkbcommon-x11-dev libfontconfig1-dev libcairo2-dev
  libfreetype6-dev libpango1.0-dev libgtkmm-3.0-dev` (Debian/Ubuntu names; gtkmm is only for the SDK's test hosts).
- **Windows**: Visual Studio 2022 with the C++ workload; use `cmake -B build -A x64`.

Every push is built and tested on all three platforms by GitHub Actions; pushing a `v*` tag
publishes a release and then runs the installers against it.

## Layout

```
plugins/simplr     sampler                 src/core (DSP) · src/plugin (VST3) · src/ui · tests
plugins/multidyn   multiband dynamics      same structure
plugins/lowfocus   low-end focus           same structure
shared/pluginkit   code all plug-ins share: parameter tables, VST3 controller/editor bases,
                   VSTGUI widgets, the macOS host-test harness
cmake/PluginKit.cmake   SDK fetch + pk_add_plugin() / pk_add_host_test()
scripts            build.sh, install.sh, install.ps1
```

Each plug-in keeps its DSP free of any framework so it is tested headlessly (`*_tests`); the
`*_hosttest` programs load the built bundles through the VST3 hosting API, play audio/MIDI, check
state, drive the real editor with synthetic mouse events and save screenshots.

## License

MIT (see [LICENSE](LICENSE)). Third-party components: see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
