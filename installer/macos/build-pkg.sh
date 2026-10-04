#!/usr/bin/env bash
# Builds the double-click macOS installer (.pkg) from the built VST3 bundles.
#
#   installer/macos/build-pkg.sh build/VST3/Release dist/Plugins-macOS.pkg [version]
#
# Every bundle in the folder becomes one component package and one tick box on the installer's
# "Customise" screen (all ticked). They install for all users into
# /Library/Audio/Plug-Ins/VST3/bfielstr. Each component's preinstall script (installer/macos/preinstall)
# first removes the older copy, our copies under renamed names (common.sh), and duplicates elsewhere.
#
# Signing and notarisation switch on by themselves when these are set (unsigned otherwise):
#   APPLE_INSTALLER_IDENTITY   "Developer ID Installer: Name (TEAMID)", in the keychain: signs the .pkg
#   APPLE_NOTARY_APPLE_ID, APPLE_NOTARY_TEAM_ID, APPLE_NOTARY_PASSWORD (app-specific password):
#                              with a signed .pkg, notarise it with Apple and staple the ticket
# (The bundles themselves are signed before this runs: see the workflow's "Sign the plug-ins" step.)
#
# PKG_DRY_RUN=1 writes the component layout, scripts and distribution.xml into PKG_WORK_DIR (or a
# temporary folder) and stops before pkgbuild, so the generation can be checked on any system.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=installer/common.sh
. "$here/../common.sh"

if [ $# -lt 2 ]; then
    echo "usage: $0 <folder with .vst3 bundles> <output .pkg> [version]" >&2
    exit 2
fi
src="$(cd "$1" && pwd)"
out="$2"
version="${3:-$(app_version)}"
dry_run="${PKG_DRY_RUN:-0}"

install_location="/Library/Audio/Plug-Ins/VST3/bfielstr"
id_prefix="com.bfielstr.audio-plugins"

# (no mapfile, and empty arrays guarded under set -u: macOS's own bash is 3.2)
plugins=()
while IFS= read -r line; do plugins+=("$line"); done < <(list_plugins "$src")
if [ ${#plugins[@]} -eq 0 ]; then
    echo "No .vst3 bundles in $src" >&2
    exit 1
fi
retired=()
while IFS= read -r line; do retired+=("$line"); done < <(retired_bundles "$src")
echo "Version $version; plug-ins: ${plugins[*]}"
echo "Renamed bundles to clear away: ${retired[*]+${retired[*]}}"

work="${PKG_WORK_DIR:-$(mktemp -d)}"
mkdir -p "$work"
if [ -z "${PKG_WORK_DIR:-}" ] && [ "$dry_run" != 1 ]; then
    trap 'rm -rf "$work"' EXIT
fi
rm -rf "$work/roots" "$work/scripts" "$work/pkgs" "$work/resources"
mkdir -p "$work/roots" "$work/scripts" "$work/pkgs" "$work/resources"

copy_tree() { # from to
    if command -v ditto >/dev/null 2>&1; then ditto "$1" "$2"; else cp -R "$1" "$2"; fi
}

# The installer may only run on Macs the binaries support (dev builds are one architecture).
host_archs="arm64,x86_64"
if command -v lipo >/dev/null 2>&1; then
    first="${plugins[0]}"
    host_archs="$(lipo -archs "$src/$first.vst3/Contents/MacOS/$first" | tr ' ' ',')"
fi

choices_outline=""
choices=""
pkg_refs=""
for p in "${plugins[@]}"; do
    lower="$(echo "$p" | tr '[:upper:]' '[:lower:]')"
    id="$id_prefix.$lower"

    mkdir -p "$work/roots/$p" "$work/scripts/$p"
    copy_tree "$src/$p.vst3" "$work/roots/$p/$p.vst3"
    cp "$here/preinstall" "$work/scripts/$p/preinstall"
    chmod 755 "$work/scripts/$p/preinstall"
    {
        echo "replace $p"
        for old in ${retired[@]+"${retired[@]}"}; do echo "retire $old"; done
    } >"$work/scripts/$p/actions.txt"

    desc="$(describe_plugin "$p")"
    choices_outline+="        <line choice=\"$p\"/>"$'\n'
    choices+="    <choice id=\"$p\" title=\"$(xml_escape "$lower")\" description=\"$(xml_escape "$desc")\" start_selected=\"true\">"$'\n'
    choices+="        <pkg-ref id=\"$id\"/>"$'\n'
    choices+="    </choice>"$'\n'
    pkg_refs+="    <pkg-ref id=\"$id\" version=\"$version\" onConclusion=\"none\">$p.pkg</pkg-ref>"$'\n'
done

cat >"$work/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Audio plug-ins $version</title>
    <organization>$id_prefix</organization>
    <welcome file="welcome.html" mime-type="text/html"/>
    <license file="LICENSE.txt" mime-type="text/plain"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <options customize="always" require-scripts="false" rootVolumeOnly="true" hostArchitectures="$host_archs"/>
    <domains enable_localSystem="true" enable_currentUserHome="false" enable_anywhere="false"/>
    <volume-check>
        <allowed-os-versions>
            <os-version min="11.0"/>
        </allowed-os-versions>
    </volume-check>
    <choices-outline>
$choices_outline    </choices-outline>
$choices$pkg_refs</installer-gui-script>
EOF

sed "s/@VERSION@/$version/g" "$here/resources/welcome.html" >"$work/resources/welcome.html"
cp "$here/resources/conclusion.html" "$work/resources/conclusion.html"
cp "$REPO_ROOT/LICENSE" "$work/resources/LICENSE.txt"

if [ "$dry_run" = 1 ]; then
    echo "Dry run: layout and distribution.xml in $work"
    exit 0
fi

for p in "${plugins[@]}"; do
    lower="$(echo "$p" | tr '[:upper:]' '[:lower:]')"
    plist="$work/$p-component.plist"
    # Not relocatable: otherwise Installer finds a bundle with the same ID somewhere else on the disk
    # (a copy in a user's Library, a build folder) and "upgrades" that one instead of installing here.
    # Not version-checked: installing an older release over a newer one must work too.
    pkgbuild --analyze --root "$work/roots/$p" "$plist" >/dev/null
    i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$plist" >/dev/null 2>&1; do
        # (Set when pkgbuild wrote the key, Add when it did not: newer pkgbuilds leave some keys out)
        for key in BundleIsRelocatable BundleIsVersionChecked; do
            /usr/libexec/PlistBuddy -c "Set :$i:$key false" "$plist" 2>/dev/null ||
                /usr/libexec/PlistBuddy -c "Add :$i:$key bool false" "$plist"
        done
        i=$((i + 1))
    done
    pkgbuild --root "$work/roots/$p" --component-plist "$plist" \
        --identifier "$id_prefix.$lower" --version "$version" \
        --install-location "$install_location" --scripts "$work/scripts/$p" \
        "$work/pkgs/$p.pkg"
done

sign_args=()
if [ -n "${APPLE_INSTALLER_IDENTITY:-}" ]; then
    sign_args=(--sign "$APPLE_INSTALLER_IDENTITY" --timestamp)
    echo "Signing the installer as $APPLE_INSTALLER_IDENTITY"
else
    echo "No APPLE_INSTALLER_IDENTITY: the installer is unsigned"
fi
mkdir -p "$(dirname "$out")"
productbuild --distribution "$work/distribution.xml" --resources "$work/resources" \
    --package-path "$work/pkgs" ${sign_args[@]+"${sign_args[@]}"} "$out"

if [ ${#sign_args[@]} -gt 0 ] && [ -n "${APPLE_NOTARY_APPLE_ID:-}" ] && [ -n "${APPLE_NOTARY_TEAM_ID:-}" ] &&
    [ -n "${APPLE_NOTARY_PASSWORD:-}" ]; then
    echo "Notarising $out"
    xcrun notarytool submit "$out" --apple-id "$APPLE_NOTARY_APPLE_ID" --team-id "$APPLE_NOTARY_TEAM_ID" \
        --password "$APPLE_NOTARY_PASSWORD" --wait
    xcrun stapler staple "$out"
fi

echo "Built $out"
ls -l "$out"
