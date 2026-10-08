#!/usr/bin/env python3
"""Writes the README of the public downloads repository from this README.

usage: releases-readme.py README.md OUT.md
Keeps the introduction, the plug-in table and the Install section; links into the source tree
(which is private) become plain text or point at the release.
"""
import re, sys

src = open(sys.argv[1], encoding="utf-8").read()
sections = re.split(r"(?m)^(?=## )", src)
intro, rest = sections[0], sections[1:]
install = next(s for s in rest if s.startswith("## Install"))

text = intro + install.rstrip() + "\n\n## Licence\n\nFree to use, not for sale: see [LICENSE](LICENSE) and " \
    "[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Questions, bug reports and requests: open an issue here.\n"
# relative links into the private source tree become plain text (LICENSE and the notices are copied over)
text = re.sub(r"\[([^\]]+)\]\((?!https?://|#|LICENSE\)|THIRD_PARTY_NOTICES\.md\))[^)]*\)", r"\1", text)
open(sys.argv[2], "w", encoding="utf-8").write(text)
