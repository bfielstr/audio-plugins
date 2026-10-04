#!/usr/bin/env bash
# Installs the macOS .pkg silently on a CI runner (needs sudo without a password) and checks the result:
# a subset install with one plug-in unticked, then a full install over it, with leftovers planted first
# that the preinstall scripts must clear away (and one from another vendor that must stay).
#
#   installer/macos/smoke-test.sh dist/Plugins-macOS.pkg
set -euo pipefail

pkg="$1"
sys="/Library/Audio/Plug-Ins/VST3"
dest="$sys/bfielstr"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# The plug-ins inside the installer: one component package per bundle.
pkgutil --expand "$pkg" "$tmp/x"
plugins=()
for c in "$tmp"/x/*.pkg; do
    c="$(basename "$c")"
    plugins+=("${c%.pkg}")
done
echo "In the installer: ${plugins[*]}"
[ ${#plugins[@]} -gt 1 ] || { echo "expected several plug-ins in the installer"; exit 1; }
grep -q 'customize="always"' "$tmp/x/Distribution"

fake_bundle() { # path vendor
    sudo mkdir -p "$1/Contents/Resources"
    echo "{ \"Vendor\": \"$2\", \"Version\": \"0.1.0.0\" }" | sudo tee "$1/Contents/Resources/moduleinfo.json" >/dev/null
}

# Leftovers: a renamed bundle (in the vendor folder and at the top), an old top-level copy of ours,
# another vendor's plug-in with one of our names, and a stale file inside an older install.
first="${plugins[0]}"
skip="${plugins[1]}"
sudo rm -rf "$dest"
fake_bundle "$dest/Gently.vst3" bfielstr
fake_bundle "$sys/Gently.vst3" bfielstr
fake_bundle "$sys/$first.vst3" Simplr
fake_bundle "$sys/$skip.vst3" "Someone Else"
fake_bundle "$dest/$first.vst3" bfielstr
sudo touch "$dest/$first.vst3/Contents/stale-file"
# A copy the one-line script put in the logged-in user's folder (only checked when that user is us).
console_user="$(stat -f%Su /dev/console || true)"
check_user=0
if [ "$console_user" = "$(id -un)" ]; then
    check_user=1
    mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3/bfielstr/$first.vst3/Contents/Resources"
    echo '{ "Vendor": "bfielstr" }' >"$HOME/Library/Audio/Plug-Ins/VST3/bfielstr/$first.vst3/Contents/Resources/moduleinfo.json"
fi

# 1. Everything except $skip.
cat >"$tmp/choices.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<array>
    <dict>
        <key>attributeSetting</key>
        <integer>0</integer>
        <key>choiceAttribute</key>
        <string>selected</string>
        <key>choiceIdentifier</key>
        <string>$skip</string>
    </dict>
</array>
</plist>
EOF
installer -pkg "$pkg" -target / -showChoiceChangesXML | grep -q "<string>$skip</string>"
sudo installer -pkg "$pkg" -target / -applyChoiceChangesXML "$tmp/choices.xml" -verboseR
test ! -e "$dest/$skip.vst3" || { echo "$skip was installed although it was unticked"; exit 1; }
test -d "$dest/$first.vst3"
test ! -e "$dest/$first.vst3/Contents/stale-file" || { echo "the older $first was not replaced"; exit 1; }
test ! -e "$dest/Gently.vst3" && test ! -e "$sys/Gently.vst3" || { echo "renamed Gently.vst3 not removed"; exit 1; }
test ! -e "$sys/$first.vst3" || { echo "old top-level $first.vst3 not removed"; exit 1; }
test -d "$sys/$skip.vst3" || { echo "another vendor's $skip.vst3 was removed"; exit 1; }
if [ $check_user = 1 ]; then
    test ! -e "$HOME/Library/Audio/Plug-Ins/VST3/bfielstr/$first.vst3" || { echo "the user's copy of $first was not removed"; exit 1; }
fi

# 2. Everything (the default choices).
sudo installer -pkg "$pkg" -target / -verboseR
for p in "${plugins[@]}"; do
    b="$dest/$p.vst3"
    test -f "$b/Contents/MacOS/$p" || { echo "$p missing"; exit 1; }
    codesign -v "$b"
    grep -q '"Vendor": "bfielstr"' "$b/Contents/Resources/moduleinfo.json"
    echo "$p installed"
done
test -d "$sys/$skip.vst3" # still there: not ours
pkgutil --pkgs | grep '^com\.bfielstr\.audio-plugins\.'

# Leave the runner as it was.
sudo rm -rf "$dest" "$sys/$skip.vst3"
for id in $(pkgutil --pkgs | grep '^com\.bfielstr\.audio-plugins\.'); do sudo pkgutil --forget "$id" >/dev/null; done
echo "macOS installer OK"
