#!/usr/bin/env python3
"""Refuse NEW lane-note walls in src/ -- lane history belongs in the sidecar.

Lane history (what was tried, by which lane, what it measured) lives in
`docs/decomp/notes/<path under src/>.md`, one section per function keyed by
mangled symbol, where `scripts/find_refutations.py` reads it.  The source keeps
at most a few lines stating the residual mechanism plus a pointer.  This check
is what keeps it that way: it scans every C/C++ file under src/ with the SAME
detector the migrator uses (`migrate_notes.movable_blocks`: a contiguous `//`
block carrying a lane tag such as `w27-n` / `w-five-clean` AND a
find_refutations note keyword, >= --min-lines lines) and fails when a file
carries more such walls than its allowance.

The allowance (`scripts/notes/lane_walls_allowed.txt`, `<path> <count>` per
line) records the walls the tree-wide migration deliberately left in place,
with the reason.  It is a ratchet: a file with MORE walls than allowed is a
new wall (exit 1); a file with FEWER is a stale allowance (exit 3 -- lock the
improvement in with --write, or a wall could grow back to the old count
unseen).  A missing allowance file means "no walls allowed anywhere", the
strict direction; it can never disarm the check.

Fix a failure with:  python3 scripts/notes/migrate_notes.py <file>
(moves the wall verbatim into the sidecar and leaves a marker + quote), then
re-run the build.

WHERE THIS RUNS (be precise -- an uninstalled check enforces nothing):
  * every full `ninja` in this repo: a post-compile step in configure.py
    ("CHECK no lane-note walls in src/"); a failure fails the build before
    REPORT.  It does NOT run on `ninja <one>.obj`.
  * tests/test_no_lane_walls.py (pytest) runs it over the live tree.
  * nothing runs it at `git commit`: there is no installed hook.

Exit: 0 clean . 1 new wall(s) . 2 cannot run . 3 stale allowance
"""

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from migrate_notes import (LANE_HISTORY_RE, MIN_LINES, NOTE_RE, lex,  # noqa: E402
                           movable_blocks, sidecar_rel)

EXTS = {".cpp", ".cc", ".c", ".h", ".hpp", ".inl"}
ALLOWANCE = Path("scripts") / "notes" / "lane_walls_allowed.txt"


def scan(root, min_lines):
    """{src-relative path: [Block, ...]} for every file carrying a wall."""
    found = {}
    for p in sorted((root / "src").rglob("*")):
        if p.suffix not in EXTS or not p.is_file():
            continue
        try:
            text = p.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            text = p.read_text(encoding="latin-1")
        # cheap prefilter, same two predicates movable_blocks requires per block
        if not LANE_HISTORY_RE.search(text) or not NOTE_RE.search(text):
            continue
        lines = text.split("\n")
        blocks = movable_blocks(lines, lex(lines), min_lines)
        if blocks:
            found[str(p.relative_to(root))] = blocks
    return found


def read_allowance(path):
    allowed, reasons = {}, {}
    if not path.exists():
        return allowed, reasons
    for n, line in enumerate(path.read_text().splitlines(), 1):
        body = line.split("#", 1)[0].strip()
        if not body:
            continue
        parts = body.split()
        if len(parts) != 2 or not parts[1].isdigit() or not parts[0].startswith("src/"):
            raise ValueError(f"{path}:{n}: expected '<path> <count>  # reason', got {line!r}")
        allowed[parts[0]] = int(parts[1])
        reasons[parts[0]] = line.split("#", 1)[1].strip() if "#" in line else ""
    return allowed, reasons


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--repo", help="tree root (default: two levels above this script's invocation path)")
    ap.add_argument("--min-lines", type=int, default=MIN_LINES)
    ap.add_argument("--allowance", help=f"allowance file (default: <repo>/{ALLOWANCE})")
    ap.add_argument("--write", action="store_true",
                    help="rewrite the allowance to the current counts (keeps existing reasons)")
    a = ap.parse_args(argv)
    if a.repo:
        root = Path(a.repo).resolve()
    else:
        sys.path.insert(0, str(HERE.parent))
        from project_root import project_root
        root = project_root(__file__, None, levels=2, warn=False)
    if not (root / "src").is_dir():
        print(f"CANNOT RUN: no src/ under {root}", file=sys.stderr)
        return 2
    allow_path = Path(a.allowance) if a.allowance else root / ALLOWANCE
    try:
        allowed, reasons = read_allowance(allow_path)
    except ValueError as e:
        print(f"CANNOT RUN: {e}", file=sys.stderr)
        return 2
    found = scan(root, a.min_lines)

    if a.write:
        out = ["# Lane-note walls deliberately left in src/ by the tree-wide move.",
               "# <path> <count>  # reason.  Read by scripts/notes/check_no_lane_walls.py.",
               "# A ratchet: lower a count (or drop a line) when a wall moves; never raise one",
               "# to admit a new wall -- run scripts/notes/migrate_notes.py <file> instead."]
        for rel in sorted(found):
            out.append(f"{rel} {len(found[rel])}  # {reasons.get(rel) or 'reason not recorded'}")
        allow_path.write_text("\n".join(out) + "\n")
        print(f"wrote {allow_path}: {len(found)} file(s), {sum(map(len, found.values()))} wall(s)")
        return 0

    new, stale = [], []
    for rel in sorted(set(found) | set(allowed)):
        have, may = len(found.get(rel, [])), allowed.get(rel, 0)
        if have > may:
            new.append(rel)
        elif have < may:
            stale.append((rel, have, may))
    for rel in new:
        for b in found[rel]:
            print(f"{rel}:{b.mstart + 1}-{b.end + 1}: {b.nmoved}-line lane-note wall in src/ "
                  f"(allowed here: {allowed.get(rel, 0)}).  Lane history goes in "
                  f"{sidecar_rel(rel)} -- run: python3 scripts/notes/migrate_notes.py {rel}")
    for rel, have, may in stale:
        print(f"{rel}: allowance says {may} wall(s), the file has {have}.  Lock it in: "
              f"python3 scripts/notes/check_no_lane_walls.py --write")
    nfiles = sum(1 for p in (root / "src").rglob("*") if p.suffix in EXTS)
    total = sum(map(len, found.values()))
    print(f"checked {nfiles} src file(s): {total} lane-note wall(s) of >= {a.min_lines} lines "
          f"in {len(found)} file(s); allowance covers {sum(allowed.values())}; "
          f"{len(new)} file(s) over, {len(stale)} stale")
    if new:
        return 1
    if stale:
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
