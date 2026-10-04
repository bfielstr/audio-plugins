#!/usr/bin/env bash
# Builds the double-click Windows installer with Inno Setup (run in Git Bash on the Windows runner).
#
#   installer/windows/build-setup.sh build/VST3/Release dist [version]
#
# Writes <output folder>/Plugins-Windows-x64-Setup.exe. Every bundle in the folder becomes one
# component (tick box) of setup.iss, so a new plug-in is picked up without editing anything.
#
# Signing switches on by itself when a certificate is set (unsigned otherwise):
#   WINDOWS_CERT_FILE       path to a .pfx code-signing certificate
#   WINDOWS_CERT_PASSWORD   its password
#   (signtool.exe from the Windows SDK signs the setup and the uninstaller; a certificate kept in a
#   cloud HSM would replace the sign command below)
#
# SETUP_DRY_RUN=1 writes the generated script into SETUP_WORK_DIR (or a temporary folder) and stops
# before running iscc, so the generation can be checked on any system.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=installer/common.sh
. "$here/../common.sh"

if [ $# -lt 2 ]; then
    echo "usage: $0 <folder with .vst3 bundles> <output folder> [version]" >&2
    exit 2
fi
src="$(cd "$1" && pwd)"
outdir="$2"
version="${3:-$(app_version)}"
dry_run="${SETUP_DRY_RUN:-0}"

winpath() { # a path as Windows tools want it (unchanged where there is no cygpath)
    if command -v cygpath >/dev/null 2>&1; then cygpath -w "$1"; else printf '%s' "$1"; fi
}

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

work="${SETUP_WORK_DIR:-$(mktemp -d)}"
mkdir -p "$work"
if [ -z "${SETUP_WORK_DIR:-}" ] && [ "$dry_run" != 1 ]; then
    trap 'rm -rf "$work"' EXIT
fi
cp "$here/setup.iss" "$here/welcome.txt" "$work/"
cp "$REPO_ROOT/LICENSE" "$work/LICENSE.txt"
: >"$work/components.iss"
: >"$work/files.iss"
: >"$work/installdelete.iss"
for p in "${plugins[@]}"; do
    lower="$(echo "$p" | tr '[:upper:]' '[:lower:]')"
    printf 'Name: "%s"; Description: "%s"; Types: full custom\n' "$lower" "$lower" >>"$work/components.iss"
    printf 'Source: "%s\\*"; DestDir: "{app}\\%s.vst3"; Components: %s; Flags: ignoreversion recursesubdirs createallsubdirs\n' \
        "$(winpath "$src/$p.vst3")" "$p" "$lower" >>"$work/files.iss"
    printf 'Type: filesandordirs; Name: "{app}\\%s.vst3"; Components: %s\n' "$p" "$lower" >>"$work/installdelete.iss"
done

plugin_list="$(IFS=,; echo "${plugins[*]}")"
retired_list="$(IFS=,; echo "${retired[*]+${retired[*]}}")"
args=("/DAppVersion=$version" "/DPluginList=$plugin_list" "/DRetiredList=$retired_list")

if [ -n "${WINDOWS_CERT_FILE:-}" ]; then
    signtool="$(command -v signtool.exe || true)"
    if [ -z "$signtool" ]; then
        # the newest Windows SDK on the machine
        signtool="$(printf '%s\n' "/c/Program Files (x86)/Windows Kits/10/bin/"*/x64/signtool.exe | grep -v "\*" | sort -V | tail -n 1 || true)"
    fi
    [ -n "$signtool" ] || { echo "WINDOWS_CERT_FILE is set but signtool.exe was not found" >&2; exit 1; }
    echo "Signing the installer with $WINDOWS_CERT_FILE"
    # Inno Setup replaces $f with the file to sign and $q with a double quote
    args+=("/DSign" "/Ssigntool=\$q$(winpath "$signtool")\$q sign /f \$q$(winpath "$WINDOWS_CERT_FILE")\$q /p \$q${WINDOWS_CERT_PASSWORD:-}\$q /fd sha256 /tr http://timestamp.digicert.com /td sha256 \$f")
else
    echo "No WINDOWS_CERT_FILE: the installer is unsigned"
fi

if [ "$dry_run" = 1 ]; then
    echo "Dry run: script in $work; iscc arguments:"
    printf '  %s\n' "${args[@]}"
    exit 0
fi

iscc="$(command -v iscc || command -v ISCC.exe || true)"
if [ -z "$iscc" ]; then
    for c in "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" "/c/Program Files/Inno Setup 6/ISCC.exe"; do
        if [ -f "$c" ]; then iscc="$c"; break; fi
    done
fi
[ -n "$iscc" ] || { echo "Inno Setup (iscc) not found: choco install innosetup" >&2; exit 1; }

# (Git Bash would otherwise turn "/DAppVersion=..." into a path)
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' "$iscc" "${args[@]}" "$(winpath "$work/setup.iss")"
mkdir -p "$outdir"
cp "$work/output/Plugins-Windows-x64-Setup.exe" "$outdir/"
echo "Built $outdir/Plugins-Windows-x64-Setup.exe"
ls -l "$outdir/Plugins-Windows-x64-Setup.exe"
