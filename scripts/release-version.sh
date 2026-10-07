#!/usr/bin/env bash
# release-version.sh <before> <after>
# Used by .github/workflows/release-tag.yml. Reads the version from the root CMakeLists.txt
# (project(... VERSION x.y.z ...)) at both commits. When <after> raises it, every
# plugins/*/CMakeLists.txt at <after> carries the same version (x.y.z or x.y.z.0) and the tag vX.Y.Z
# does not exist yet (locally or on origin), prints tag=vX.Y.Z (also to $GITHUB_OUTPUT when set).
# Otherwise it prints why and no tag. Exits non-zero when the plug-ins' versions disagree with the root.
set -euo pipefail

before="${1:?usage: release-version.sh <before> <after>}"
after="${2:?usage: release-version.sh <before> <after>}"

out() {
    echo "$1"
    if [ -n "${GITHUB_OUTPUT:-}" ]; then echo "$1" >> "$GITHUB_OUTPUT"; fi
}

# the version in project(... VERSION x.y.z ...) of a CMakeLists.txt read on stdin (the first project())
project_version() {
    tr '\n' ' ' | grep -oE 'project\([^)]*\)' | head -n 1 | grep -oE 'VERSION[[:space:]]+[0-9]+(\.[0-9]+)*' |
        awk '{ print $2 }'
}

# a commit that is all zeros (a new branch) or missing from a shallow clone: fetch it, or give up
if [[ "$before" =~ ^0+$ ]]; then
    out "tag="
    echo "no previous commit to compare with; nothing to tag"
    exit 0
fi
if ! git cat-file -e "$before^{commit}" 2>/dev/null; then
    git fetch --quiet --depth=1 origin "$before" || true
fi
if ! git cat-file -e "$before^{commit}" 2>/dev/null; then
    echo "::error::cannot read the previous commit $before"
    exit 1
fi

old=$(git show "$before:CMakeLists.txt" | project_version)
new=$(git show "$after:CMakeLists.txt" | project_version)
echo "version: $old -> $new"
if [[ ! "$new" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "::error::the root CMakeLists.txt's project() VERSION '$new' is not x.y.z"
    exit 1
fi
if [ "$old" = "$new" ]; then
    out "tag="
    echo "the version did not change; nothing to tag"
    exit 0
fi
if [ "$(printf '%s\n%s\n' "$old" "$new" | sort -V | tail -n 1)" != "$new" ]; then
    out "tag="
    echo "::warning::the version went down ($old -> $new); not tagging"
    exit 0
fi

# every plug-in carries the same version
bad=0
for f in $(git ls-tree -r --name-only "$after" -- plugins | grep -E '^plugins/[^/]+/CMakeLists\.txt$'); do
    v=$(git show "$after:$f" | project_version)
    if [ "$v" != "$new" ] && [ "$v" != "$new.0" ]; then
        echo "::error file=$f::$f has VERSION '$v', the root CMakeLists.txt $new (bump every plugins/*/CMakeLists.txt with it)"
        bad=1
    fi
done
if [ "$bad" != 0 ]; then
    exit 1
fi
if ! git show "$after:README.md" | grep -qF "The current version is **$new**"; then
    echo "::warning file=README.md::README.md does not say 'The current version is **$new**'"
fi

tag="v$new"
if git rev-parse -q --verify "refs/tags/$tag" >/dev/null ||
   git ls-remote --exit-code --tags origin "refs/tags/$tag" >/dev/null 2>&1; then
    out "tag="
    echo "$tag already exists; nothing to do (an existing tag is never moved)"
    exit 0
fi
out "tag=$tag"
