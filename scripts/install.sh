#!/bin/sh
# Downloads the latest release and installs the VST3 plug-ins (Simplr, Multidyn, Lowfocus)
# for the current user.
#
#   curl -fsSL https://raw.githubusercontent.com/bfielstr/simplr/main/scripts/install.sh | sh
#
# Environment overrides:
#   SIMPLR_VERSION=v0.1.0   install a specific release instead of the latest
#   SIMPLR_DEST=/some/dir   install into a different VST3 folder
#   SIMPLR_PLUGINS="Multidyn Lowfocus"   install only some of the plug-ins
set -eu

REPO="${SIMPLR_REPO:-bfielstr/simplr}"
VERSION="${SIMPLR_VERSION:-latest}"

os="$(uname -s)"
arch="$(uname -m)"
case "$os" in
    Darwin)
        asset="Plugins-macOS.zip" # universal: Apple Silicon + Intel
        dest="${SIMPLR_DEST:-$HOME/Library/Audio/Plug-Ins/VST3}"
        ;;
    Linux)
        case "$arch" in
            x86_64 | amd64) asset="Plugins-Linux-x86_64.zip" ;;
            *)
                echo "No prebuilt plug-ins for Linux/$arch yet. Build from source:" >&2
                echo "  https://github.com/$REPO#build--install" >&2
                exit 1
                ;;
        esac
        dest="${SIMPLR_DEST:-$HOME/.vst3}"
        ;;
    *)
        echo "Unsupported system: $os. On Windows, run in PowerShell:" >&2
        echo "  irm https://raw.githubusercontent.com/$REPO/main/scripts/install.ps1 | iex" >&2
        exit 1
        ;;
esac

if [ "$VERSION" = "latest" ]; then
    base="https://github.com/$REPO/releases/latest/download"
else
    base="https://github.com/$REPO/releases/download/$VERSION"
fi

download() { # url file
    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 --progress-bar -o "$2" "$1"
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "$2" "$1"
    else
        echo "Need curl or wget to download." >&2
        exit 1
    fi
}

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM

echo "Downloading $asset ($VERSION) from github.com/$REPO"
download "$base/$asset" "$tmp/$asset"

# Verify the checksum published with the release.
if download "$base/SHA256SUMS.txt" "$tmp/SHA256SUMS.txt" 2>/dev/null; then
    expected="$(grep " $asset\$" "$tmp/SHA256SUMS.txt" | cut -d' ' -f1)"
    if command -v sha256sum >/dev/null 2>&1; then
        actual="$(sha256sum "$tmp/$asset" | cut -d' ' -f1)"
    else
        actual="$(shasum -a 256 "$tmp/$asset" | cut -d' ' -f1)"
    fi
    if [ -z "$expected" ] || [ "$expected" != "$actual" ]; then
        echo "Checksum mismatch for $asset - aborting." >&2
        exit 1
    fi
    echo "Checksum OK"
else
    echo "Warning: no SHA256SUMS.txt in this release; skipping checksum verification." >&2
fi

mkdir -p "$tmp/x"
if [ "$os" = "Darwin" ]; then
    ditto -x -k "$tmp/$asset" "$tmp/x"
elif command -v unzip >/dev/null 2>&1; then
    unzip -q "$tmp/$asset" -d "$tmp/x"
elif command -v python3 >/dev/null 2>&1; then
    python3 -m zipfile -e "$tmp/$asset" "$tmp/x"
else
    echo "Need unzip (or python3) to extract the download." >&2
    exit 1
fi

plugins="${SIMPLR_PLUGINS:-}"
if [ -z "$plugins" ]; then
    plugins="$(cd "$tmp/x" && ls -d *.vst3 2>/dev/null | sed 's/\.vst3$//' | tr '\n' ' ')"
fi
if [ -z "$plugins" ]; then
    echo "Unexpected archive layout (no .vst3 inside)." >&2
    exit 1
fi

mkdir -p "$dest"
for p in $plugins; do
    if [ ! -d "$tmp/x/$p.vst3" ]; then
        echo "No $p.vst3 in this release - skipping." >&2
        continue
    fi
    rm -rf "$dest/$p.vst3"
    mv "$tmp/x/$p.vst3" "$dest/"
    if [ "$os" = "Darwin" ]; then
        # Files fetched outside a browser aren't quarantined, but clear it in case.
        xattr -dr com.apple.quarantine "$dest/$p.vst3" 2>/dev/null || true
    fi
    echo "Installed: $dest/$p.vst3"
done

echo "In REAPER: Options > Preferences > Plug-ins > VST > Re-scan."
