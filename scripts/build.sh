#!/bin/sh
# Builds all plug-ins (universal arm64 + x86_64 on macOS), runs the tests and installs the VST3s
# into ~/Library/Audio/Plug-Ins/VST3 (macOS) or ~/.vst3 (Linux). Usage: scripts/build.sh [--no-install]
set -e
cd "$(dirname "$0")/.."

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"   # also runs Steinberg's VST3 validator

ctest --test-dir build -C Release --output-on-failure

if [ "$1" != "--no-install" ]; then
    if [ "$(uname -s)" = "Darwin" ]; then dest="$HOME/Library/Audio/Plug-Ins/VST3"; else dest="$HOME/.vst3"; fi
    mkdir -p "$dest"
    for p in build/VST3/Release/*.vst3; do
        name="$(basename "$p")"
        rm -rf "$dest/$name"
        cp -R "$p" "$dest/"
        echo "Installed $dest/$name"
    done
fi
