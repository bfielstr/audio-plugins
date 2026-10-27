# Notes for coding agents

9 VST3 plug-ins in `plugins/<name>/` (`src/core` DSP without any framework, `src/plugin` VST3,
`src/ui` VSTGUI, `tests`, `presets`), shared code in `shared/pluginkit`, CMake helpers in
`cmake/PluginKit.cmake`. The VST3 SDK lives in `external/vst3sdk` (fetched on the first configure; in a
second worktree, symlink `external` to the main checkout's and never commit the link).

## Build and test

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPK_DRAW_BENCH=ON \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache   # ccache only if installed
export CCACHE_BASEDIR="$PWD"   # worktrees of the same repo share ccache hits
cmake --build build
```

- One plug-in only (`tapr`; the bundle target is capitalised):
  `cmake --build build --target tapr_tests Tapr` (add `tapr_state_tests` where it exists:
  gentlr, probr, smacheratr, smemplr, sunder, sweepr, tapr, widr; `tapr_drawbench` with the draw bench).
  Then `ctest --test-dir build -R '^tapr_' --output-on-failure`.
- A smaller build tree: `-DPK_ONLY_PLUGINS="tapr;sunder"` configures only those plug-ins (plus the
  libraries of the ones they use, tests disabled); empty means all.
- Before a pull request: `cmake --build build && ctest --test-dir build -j 4 --output-on-failure`.
  Parallel ctest is fine: the CPU-budget tests are `RUN_SERIAL` with label `cpu` (`pk_cpu_test`), so
  they run alone. A new test that checks a CPU time must call `pk_cpu_test(<test>)` (or pass
  `CPU_BUDGET` to `pk_add_host_test`).
- Draw bench layout check (Linux, `-DPK_DRAW_BENCH=ON`): `ctest --test-dir build -R _drawbench_layouts`
  (or `build/bin/Release/tapr_drawbench --check-layouts`).
- The macOS host tests (`tests/host_test.mm`) do not build on Linux; check their syntax with
  ```sh
  clang++ -fsyntax-only -x objective-c++ -std=c++20 -DRELEASE=1 -Wno-pragma-pack \
    -Iplugins/tapr/src -Iplugins/tapr/src/core -Iplugins -Ishared \
    -Iexternal/vst3sdk -Iexternal/vst3sdk/vstgui4 plugins/tapr/tests/host_test.mm
  ```
  (smemplr also needs `-Ithird_party/dr_libs`).
- A plug-in's parameters, for writing presets: `build/bin/Release/factory_presets_tests --dump tapr`.

## CI and releases

- Pull requests build and test only the changed plug-ins and the plug-ins that use them
  (`scripts/ci-changed-plugins.py`); anything outside `plugins/<name>/` builds everything.
  They build on Linux only, except a version bump or a pull request labelled `full-ci` (macOS, Linux,
  Windows): the macOS host tests then run only there, so label a pull request that changes a host test
  or the UI `full-ci`, and check host tests locally with the syntax check below.
- Releasing: bump the root `CMakeLists.txt` VERSION, every `plugins/*/CMakeLists.txt` VERSION
  (`x.y.z.0`) and the README's "The current version is" line in the pull request. Merging it to main
  tags `vX.Y.Z` and runs the release (`.github/workflows/release-tag.yml`). Do not push tags.
  Releases publish to the downloads repository `bfielstr/audio-plugins-releases`
  (Actions variable `RELEASES_REPO`, secret `RELEASES_TOKEN`), so download links point there.

## Conventions

- Parameter IDs are saved in projects: only ever append to a plug-in's `ParamId` enum, and pin the
  numbers with `static_assert`s as the existing ones do (`src/core/Params.h`).
- Basic page: an editor lists its main controls (3 to 6), display and output in `basicSpec ()`
  (`shared/pluginkit/ui/BasicView.h`); EditorBase builds the page and the Advanced switch, and
  `<plugin>_drawbench --check-layouts` checks it. The capture band (scope, Freeze, drag out as WAV or
  wavetable) is one line, `s.capture = [this] { return &<a pk::CaptureBuffer the processor pushes>; };`. Done: every plug-in. A
  processor pushes its output with `pk::captureOutput`.
- End of the chain: every plug-in except probr ends with **Soft Clip** and **Hard Clip**
  (`shared/pluginkit/EndClip.h`, both off by default), beside its output controls and on its Basic page.
  The old end saturator's parameters stay in the table, retired (`pk::retire`, `ParamInfo::retired`:
  hidden from the host, left out of presets, copy and paste), so their IDs never move and old projects
  load.
- Docs and UI text: plug-in names in lowercase in prose (tapr, smacheratr); controls named exactly
  as their UI labels; no em dashes; no "inspired by" wording.
- Commits: one logical change each, subject `<plug-in or area>: what changed`.
