#!/usr/bin/env python3
"""Show the lane notes a function already carries, BEFORE you work it.

WHY THIS EXISTS
===============
Measured 2026-11-09, picking rows cold from `query_functions` / a symbol sweep:
THREE of five had a multi-hundred-line refutation sitting directly above the
function, and two more had one inside the body. Every one of those was a
previous lane's measured-and-reverted lever list. Rediscovering them costs the
whole investigation.

Examples, all found by hand that night and all now recorded here:
  * `RndBitmap::PixelOffset`  -- w21-bh + w26-r: all 8 multiply operand orders,
    all variants of width/pixelScale/scaledWidth/mBpp, all 120 statement orders
    of the compressed arm, all measured byte-identical.
  * `ObjectDir::Save`         -- w26-n2: the last two rows are a store order
    inside an inlined vector::swap; three spellings inert, two worse.
  * `Hmx::Rnd::Modal`         -- w26-f11: binding the bool is byte-inert, the
    swap sits in regasg's scratch schedule.
  * `BoxMapLighting::ApplyLight` -- w7-at/w7-bl/w21-bg: eleven spellings,
    including "flipping the three colour products (22, inert -- MSVC
    canonicalises them)".
  * `ObjPtrVec::erase<FlowNode>` -- w24-pch: definition order is inert; the
    open question is what keeps the two division results live.

WHAT IT DOES NOT DO
===================
It greps. It cannot tell you whether the note is *about this function* or about
the one next door, and a note that says "open question" is not a refutation. It
prints the evidence and lets you adjudicate -- the same rule the rest of this
repo's instruments follow. Treat a hit as a lead that saves time, never as
proof that the row is closed.

USAGE
=====
    scripts/find_refutations.py '?RndScaleObject@@YAXPAVObject@Hmx@@MM@Z'
    scripts/find_refutations.py --unit default/system/rndobj/Utl
    scripts/find_refutations.py --file src/system/rndobj/Bitmap.cpp

Exit codes:  0 = notes found   1 = function/file resolved but NO notes
             2 = could not resolve (symbol not in DB, or the TU file is absent)
Every run prints which of those it is, so "no notes" is never mistaken for
"could not look" -- the failure mode that has burned this project repeatedly.
"""

import argparse
import os
import re
import sqlite3
import sys

# A line counts as a lane note if it carries one of these. `w<n>-<x>` is the
# lane tag convention (w21-bh, w26-r, w47-x ...).
NOTE_RE = re.compile(
    r"\bw\d{1,3}-[a-z0-9]+"        # lane tags: w7-at, w21-bh, w26-n2, w47-x
    r"|\bMEASURED\b|\bREFUTED\b|\bRESIDUAL\b|\bNEGATIVE\b"
    r"|\bbyte-identical\b|\bexactly inert\b|\binert\b"
    r"|\bdo not (?:re-?open|retry)\b|\bdo NOT (?:re-?open|retry)\b",
    re.IGNORECASE,
)

COMMENT_RE = re.compile(r"^\s*(//|/\*|\*)")


def _short_name(sym):
    """`?RndScaleObject@@YAX...` -> `RndScaleObject`; demangled input too."""
    s = (sym or "").strip()
    if s.startswith("?"):
        s = s.split("@")[0].lstrip("?$")
        return s or None
    m = re.search(r"([A-Za-z_]\w*)\s*\(", s)
    return m.group(1) if m else None


def unit_to_source(unit):
    """`default/system/rndobj/Utl` -> src/system/rndobj/Utl.cpp.

    Also tries the .h, because a template implementation often lives there
    (ObjPtrVec lives in src/system/obj/ObjPtr_p.h).
    """
    stem = unit
    if stem.startswith("default/"):
        stem = stem[len("default/") :]
    stem = "src/" + stem
    for suffix in (".cpp", ".cc", ".c"):
        if os.path.exists(stem + suffix):
            return stem + suffix
    d, base = os.path.split(stem)
    # header fallbacks: exact, then the `_p.h` private-header convention
    for name in (base + ".h", base + "_p.h", base + "_impl.h"):
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    return None


def resolve_symbol(db, sym):
    rows = db.execute(
        "SELECT symbol, unit FROM functions WHERE symbol = ?", (sym,)
    ).fetchall()
    if rows:
        return rows[0]
    # tolerate a demangled name or a prefix
    rows = db.execute(
        "SELECT symbol, unit FROM functions WHERE symbol LIKE ? LIMIT 2",
        (sym + "%",),
    ).fetchall()
    if len(rows) == 1:
        return rows[0]
    return None


def blocks(lines):
    """Yield (start_idx, end_idx, text) per contiguous run of comment lines."""
    run = []
    for i, ln in enumerate(lines):
        if COMMENT_RE.match(ln):
            run.append(i)
        else:
            if run:
                yield run[0], run[-1], "\n".join(lines[run[0] : run[-1] + 1])
                run = []
    if run:
        yield run[0], run[-1], "\n".join(lines[run[0] : run[-1] + 1])


def find_notes(path, short=None, window=250, after=600):
    """Note blocks NEAR the function, not the whole file.

    A lane writes its residual note either directly ABOVE the definition or
    INSIDE the body, so an unbounded file scan is mostly the neighbours' notes
    (src/system/rndobj/Utl.cpp has 27 blocks / 782 lines of them, which is
    useless as a first look). We locate the definition and keep only blocks
    within `window` lines before it or `after` lines after it, nearest first.
    """
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        lines = fh.read().splitlines()

    anchor = None
    if short:
        name = re.escape(short)
        pat = re.compile(rf"\b{name}\s*\(")
        # A CALL site and a DEFINITION both match the name; only the definition
        # has a body opener. Prefer those, and among them the LAST, because
        # forward declarations and call sites come first in a TU.
        # (Observed: BoxMapLighting::ApplyLight's first match is a call at
        # line 117; the definition is at 449, and anchoring on the call puts
        # the window over an unrelated function.)
        defs, loose = [], []
        for i, ln in enumerate(lines):
            if not pat.search(ln):
                continue
            # The signature may wrap over several lines before its `{`:
            #   void Foo::Bar(
            #       const T &x
            #   ) const {
            # But only scan forward until the first terminator -- a `;` means
            # this was a CALL, and stopping there is what keeps
            #   return GetSweepAmount() > 0.0f;      <- call, ends in ';'
            #   void Foo::Bar(...) {                 <- NEXT function
            # from anchoring on the call. (It did exactly that to
            # ArcDetector::GetSwipeAmount, landing on line 373 inside
            # IsPathAcceptable instead of the definition.)
            body_like = False
            for l in [ln] + lines[i + 1 : i + 5]:
                t = l.rstrip()
                if t.endswith("{"):
                    body_like = True
                    break
                if t.endswith(";"):
                    break
            if body_like and re.match(
                r"^[\w:*&<>\s]*\b" + name + r"\s*\(", ln.lstrip()
            ):
                defs.append(i)
            else:
                loose.append(i)
        if defs:
            anchor = defs[-1]
        elif loose:
            anchor = loose[0]

    lo = max(0, anchor - window) if anchor is not None else 0
    hi = min(len(lines), anchor + after) if anchor is not None else len(lines)

    scored = []
    for start, end, text in blocks(lines):
        if end < lo or start > hi:
            continue
        if not NOTE_RE.search(text):
            continue
        if anchor is None:
            rank, dist = 3, 0
        elif start > anchor:
            # inside the body, or trailing it: almost certainly this function's
            rank, dist = 1, start - anchor
        elif end >= anchor - 3:
            # adjacent above the definition: certainly this function's
            rank, dist = 0, anchor - end
        else:
            # further above: usually the PREVIOUS function's note
            rank, dist = 2, anchor - end
        scored.append((rank, dist, start + 1, end + 1, text))
    scored.sort()
    return [(s, e, t) for _, _, s, e, t in scored], len(lines), anchor


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("symbol", nargs="?", help="mangled (or unambiguous prefix) symbol")
    ap.add_argument("--unit", help="unit name, e.g. default/system/rndobj/Utl")
    ap.add_argument("--file", help="source file path")
    ap.add_argument(
        "--db",
        default="decomp.db",
        help="decomp.db path (from a worktree, pass the main checkout's copy)",
    )
    ap.add_argument("--all", action="store_true",
                    help="whole file instead of the function's window (noisy)")
    ap.add_argument("--window", type=int, default=250,
                    help="lines before the definition to scan (default 250)")
    ap.add_argument("--after", type=int, default=600,
                    help="lines after the definition to scan (default 600)")
    ap.add_argument("--max-chars", type=int, default=1200,
                    help="truncate each block (default 1200; 0 = no truncation)")
    args = ap.parse_args()

    short = None
    if args.file:
        path = args.file
        if not os.path.exists(path):
            print(f"CANNOT LOOK: no such file {path}", file=sys.stderr)
            return 2
        if args.symbol:
            short = _short_name(args.symbol)
    else:
        unit = args.unit
        sym = args.symbol
        if sym and not unit:
            if not os.path.exists(args.db):
                print(f"CANNOT LOOK: no db at {args.db}", file=sys.stderr)
                return 2
            db = sqlite3.connect(args.db)
            got = resolve_symbol(db, sym)
            if not got:
                print(
                    f"CANNOT LOOK: {sym!r} is not in {args.db} "
                    "(wrong repo? dc3/rb3/rb3-xenon share symbol names)",
                    file=sys.stderr,
                )
                return 2
            sym, unit = got
            short = _short_name(sym)
        elif unit and args.symbol:
            short = _short_name(args.symbol)
        elif not unit:
            ap.error("give a symbol, --unit or --file")
        path = unit_to_source(unit)
        if not path:
            print(
                f"CANNOT LOOK: unit {unit!r} maps to no source file under src/ "
                "(some units are generated or live in a sibling tree)",
                file=sys.stderr,
            )
            return 2

    hits, nlines, anchor = find_notes(
        path,
        short,
        window=10 ** 9 if args.all else args.window,
        after=0 if args.all else args.after,
    )
    where = f"function at line {anchor + 1}" if anchor is not None else "function NOT located"
    print(f"file: {path}  ({nlines} lines, {where})")
    if not hits:
        print(
            "NO LANE NOTES FOUND near this function -- NOT proof the row is unexplored. "
            "A template implementation's notes usually live in a shared header instead "
            "(e.g. src/system/obj/ObjPtr_p.h for ObjPtrVec<T>::erase), so re-run with "
            "--file on that header. Also check docs/decomp/patterns/."
        )
        return 1

    total = sum(e - s + 1 for s, e, _ in hits)
    print(f"{len(hits)} note block(s), {total} lines. Adjudicate; do not assume closed.\n")
    for start, end, text in hits:
        body = text if args.max_chars in (0, None) else text[: args.max_chars]
        print(f"--- {path}:{start}-{end} " + "-" * 40)
        print(body)
        if args.max_chars and len(text) > args.max_chars:
            print(f"... [{len(text) - args.max_chars} chars truncated; --max-chars 0]")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
