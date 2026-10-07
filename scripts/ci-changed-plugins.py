#!/usr/bin/env python3
"""ci-changed-plugins.py <base> <head>

Used by .github/workflows/build.yml on pull requests: which plug-ins a change needs built and tested.
Prints plugins=<a;b;...> (also to $GITHUB_OUTPUT when set), the value for -DPK_ONLY_PLUGINS; empty
means build everything.

- Files changed between the merge base of <base> and <head> (git diff base...head), ignoring Markdown
  and docs/ (the workflow's paths-ignore: they build nothing).
- Anything outside plugins/<name>/ (shared/, cmake/, the root CMakeLists.txt, .github/, installer/,
  scripts/, third_party/, ...) builds everything.
- Otherwise the changed plug-ins, plus every plug-in that uses one of them (links its <name>_core or
  <name>_ui, includes its headers, or compiles its files), again and again until nothing new turns up.
  Should that be every plug-in, everything is built.
"""
import os
import re
import subprocess
import sys

ROOT = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True,
                      check=True).stdout.strip()
PLUGINS_DIR = os.path.join(ROOT, "plugins")


def emit(value, why):
    print(why, file=sys.stderr)
    line = f"plugins={value}"
    print(line)
    out = os.environ.get("GITHUB_OUTPUT")
    if out:
        with open(out, "a") as f:
            f.write(line + "\n")


def plugin_names():
    return sorted(d for d in os.listdir(PLUGINS_DIR)
                  if os.path.isfile(os.path.join(PLUGINS_DIR, d, "CMakeLists.txt")))


def uses(user, names):
    """The plug-ins whose code `user` builds on (its CMakeLists.txt and its sources)."""
    found = set()
    others = [n for n in names if n != user]
    link = re.compile(r"(?<![a-z_])(" + "|".join(others) + r")_(core|ui)(?![a-z_])")
    path = re.compile(r"(?:plugins/|#include\s+\"(?:\.\./)*)(" + "|".join(others) + r")/")
    for dirpath, _, files in os.walk(os.path.join(PLUGINS_DIR, user)):
        for f in files:
            if f != "CMakeLists.txt" and not f.endswith((".h", ".hpp", ".cpp", ".mm", ".m", ".c")):
                continue
            with open(os.path.join(dirpath, f), errors="replace") as fh:
                text = fh.read()
            if f == "CMakeLists.txt":
                found.update(m.group(1) for m in link.finditer(text))
            found.update(m.group(1) for m in path.finditer(text))
    return found


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    base, head = sys.argv[1], sys.argv[2]
    diff = subprocess.run(["git", "diff", "--name-only", f"{base}...{head}"], capture_output=True,
                          text=True, cwd=ROOT)
    if diff.returncode != 0:
        emit("", f"git diff {base}...{head} failed ({diff.stderr.strip()}): building everything")
        return
    names = plugin_names()
    changed = set()
    for f in diff.stdout.split():
        if f.endswith(".md") or f.startswith("docs/"):
            continue
        parts = f.split("/")
        if len(parts) >= 3 and parts[0] == "plugins" and parts[1] in names:
            changed.add(parts[1])
        else:
            emit("", f"{f} is outside plugins/<name>/: building everything")
            return
    if not changed:
        emit("", "no plug-in's code changed: building everything")
        return
    users = {n: uses(n, names) for n in names}
    needed = set(changed)
    while True:
        more = {n for n in names if n not in needed and users[n] & needed}
        if not more:
            break
        needed |= more
    if needed == set(names):
        emit("", f"changed: {', '.join(sorted(changed))}; every plug-in uses them: building everything")
        return
    extra = sorted(needed - changed)
    emit(";".join(sorted(needed)), f"changed: {', '.join(sorted(changed))}" +
         (f"; also building the plug-ins that use them: {', '.join(extra)}" if extra else ""))


if __name__ == "__main__":
    main()
