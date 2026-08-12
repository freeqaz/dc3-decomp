#!/usr/bin/env python3
"""Respell `#include` separators the way retail's `__FILE__` says they were.

MSVC composes a header's `__FILE__` as the `/I` prefix it was found under,
a backslash, and then THE SPELLING USED IN THE `#include` -- verbatim, separators
and all.  `MILO_ASSERT` embeds `__FILE__`, and a `??_C@` COMDAT is named after a
hash of its text, so at `functionRelocDiffs=name_check` retail tells us which
slash its own source wrote:

    retail  e:\\lazer_build_gmc1\\system\\src\\hamobj/FreestyleMoveRecorder.h
    ours    e:\\lazer_build_gmc1\\system\\src\\hamobj\\FreestyleMoveRecorder.h
                                              ^ we wrote `#include "hamobj\\..."`

Only pairs that differ SOLELY in separators are touched; a pair that differs in
a path COMPONENT is a different header, which is a finding and not a respelling.

    fix_include_separators.py <oracle.txt> [--apply] [--repo .]

`oracle.txt` is the output of the string-COMDAT decoder (retail/ours pairs).
"""
import argparse
import re
import sys
from pathlib import Path

PAIR = re.compile(r"retail: '(?P<retail>.*?)'\n  ours  : '(?P<ours>.*?)'")
UNIT = re.compile(r"^=== (?P<unit>\S+)\s+\((?P<src>[^)]+)\)", re.M)


def unescape(s):
    """The decoder prints C-style paths; collapse its doubled backslashes."""
    while "\\\\" in s:
        s = s.replace("\\\\", "\\")
    return s


def include_spelling(path):
    """The part of a composed __FILE__ that came from the #include directive.

    The `/I` prefix is always backslash-separated, so the spelling begins after
    the last backslash that precedes the first forward slash.  With no forward
    slash at all there is nothing to learn, and the caller skips it.
    """
    slash = path.find("/")
    if slash < 0:
        return None
    cut = path.rfind("\\", 0, slash)
    return path[cut + 1:] if cut >= 0 else path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("oracle")
    ap.add_argument("--repo", default=".")
    ap.add_argument("--apply", action="store_true")
    args = ap.parse_args()
    repo = Path(args.repo)

    text_all = Path(args.oracle).read_text()
    # Which TU each disagreement was OBSERVED in.  This matters: `__FILE__` for a
    # header is composed from the `#include` that opened it IN THAT TU, so the
    # right spelling is a property of the including file and not of the header.
    # Respelling tree-wide was measured on 2026-08-12 at +30 functions and -27:
    # it repairs the TUs the oracle names and breaks the ones already correct.
    owners = []
    for m in UNIT.finditer(text_all):
        owners.append((m.start(), m.group("src")))

    def owner_of(pos):
        best = None
        for start, src in owners:
            if start <= pos:
                best = src
            else:
                break
        return best

    jobs = []
    for m in PAIR.finditer(text_all):
        r, o = unescape(m.group("retail")), unescape(m.group("ours"))
        if r == o or r == "<absent>" or o == "<absent>":
            continue
        if r.replace("\\", "/") != o.replace("\\", "/"):
            continue                       # a different header, not a respelling
        want = include_spelling(r)
        src = owner_of(m.start())
        if want and src:
            jobs.append((src, want.replace("/", "\\"), want))

    if not jobs:
        print("no separator-only disagreements in the oracle")
        return 0

    edits, total = {}, 0
    for src_rel, have, want in jobs:
        src = repo / src_rel
        if not src.is_file():
            print(f"  SKIP  {src_rel}: no such source")
            continue
        text = edits.get(src, src.read_text(errors="surrogateescape"))
        pat = re.compile(r'(#\s*include\s*")' + re.escape(have) + r'(")')
        new, n = pat.subn(r"\g<1>" + want + r"\g<2>", text)
        if not n:
            print(f"  SKIP  {src_rel}: does not include \"{have}\" directly "
                  f"(it arrives through another header)")
            continue
        edits[src] = new
        print(f"  {src_rel}: {have} -> {want}")
        total += n

    if args.apply:
        for src, text in edits.items():
            src.write_text(text, errors="surrogateescape")
    print(f"\n{total} include(s) respelled in {len(edits)} file(s)"
          f"{'' if args.apply else '  [dry run -- pass --apply]'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
