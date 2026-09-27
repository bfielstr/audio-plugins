#!/bin/sh
# Builds Simplr (universal arm64 + x86_64), runs the tests and installs the VST3 into
# ~/Library/Audio/Plug-Ins/VST3. Usage: scripts/build.sh [--no-install]
set -e
cd "$(dirname "$0")/.."

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(sysctl -n hw.ncpu)"   # also runs Steinberg's VST3 validator

./build/simplr_tests
mkdir -p build/test-output
./build/bin/Release/simplr_hosttest build/VST3/Release/Simplr.vst3 build/test-output

if [ "$1" != "--no-install" ]; then
    dest="$HOME/Library/Audio/Plug-Ins/VST3"
    mkdir -p "$dest"
    rm -rf "$dest/Simplr.vst3"
    cp -R build/VST3/Release/Simplr.vst3 "$dest/"
    echo "Installed to $dest/Simplr.vst3"
fi
