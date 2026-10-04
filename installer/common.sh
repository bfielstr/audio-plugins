# Shared helpers for the installer builds (sourced by macos/build-pkg.sh and windows/build-setup.sh).
# The plug-in list always comes from the built bundles, so a new plug-in is picked up without edits here.
# shellcheck shell=bash

# Root of the repository (this file lives in <repo>/installer).
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Bundles that were renamed. A copy left under the old name would load next to the new one (same
# plug-in IDs). Keep in step with the lists in scripts/install.sh and scripts/install.ps1.
RENAMED_BUNDLES="Simplr Lowfocus Smatcheratr Perrera Smempler Gently"

# The suite's version, from the project() call in CMakeLists.txt.
app_version() {
    sed -n '/^project(/,/)/s/^ *VERSION \([0-9][0-9.]*\).*/\1/p' "$REPO_ROOT/CMakeLists.txt" | head -n 1
}

# Names of the .vst3 bundles in folder $1, one per line, without the extension (e.g. "Gentlr").
list_plugins() {
    local b
    for b in "$1"/*.vst3; do
        [ -d "$b" ] || continue
        b="$(basename "$b")"
        echo "${b%.vst3}"
    done
}

# Renamed bundle names that the build in folder $1 no longer ships, one per line.
retired_bundles() {
    local old
    for old in $RENAMED_BUNDLES; do
        [ -d "$1/$old.vst3" ] || echo "$old"
    done
}

# What plug-in $1 does, in plain text, from its row in the README's table (empty if it has no row).
describe_plugin() {
    local lower
    lower="$(echo "$1" | tr '[:upper:]' '[:lower:]')"
    sed -n "s/^| \[\*\*$lower\*\*\]([^)]*) | \(.*\) |[[:space:]]*\$/\1/p" "$REPO_ROOT/README.md" | head -n 1 |
        sed -e 's/\*\*//g' -e 's/\[\([^]]*\)\]([^)]*)/\1/g'
}

# $1 with the characters XML gives a meaning to escaped.
xml_escape() {
    printf '%s' "$1" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' -e 's/"/\&quot;/g'
}
