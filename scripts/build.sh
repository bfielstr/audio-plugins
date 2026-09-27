#!/bin/sh
# Builds all plug-ins (universal arm64 + x86_64 on macOS), runs the tests and installs the VST3s
# into ~/Library/Audio/Plug-Ins/VST3 (macOS) or ~/.vst3 (Linux). Usage: scripts/build.sh [--no-install]
set -e
cd "$(dirname "$0")/.."

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"   # also runs Steinberg's VST3 validator

ctest --test-dir build -C Release --output-on-failure

if [ "$1" != "--no-install" ]; then
    if [ "$(uname -s)" = "Darwin" ]; then root="$HOME/Library/Audio/Plug-Ins/VST3"; else root="$HOME/.vst3"; fi
    dest="$root/bfielstr"
    mkdir -p "$dest"
    for old in Simplr Lowfocus; do # renamed in 0.5.0: remove our copies under the old names
        for dir in "$dest" "$root"; do
            if grep -qE '"Vendor": *"(bfielstr|Simplr)"' "$dir/$old.vst3/Contents/Resources/moduleinfo.json" 2>/dev/null; then
                rm -rf "$dir/$old.vst3"
            fi
        done
    done
    for p in build/VST3/Release/*.vst3; do
        name="$(basename "$p")"
        # remove a copy an older installer put directly in the VST3 folder (only if it is ours)
        if grep -qE '"Vendor": *"(bfielstr|Simplr)"' "$root/$name/Contents/Resources/moduleinfo.json" 2>/dev/null; then
            rm -rf "$root/$name"
        fi
        rm -rf "$dest/$name"
        cp -R "$p" "$dest/"
        echo "Installed $dest/$name"
    done
fi
