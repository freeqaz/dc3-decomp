#!/usr/bin/env python3
"""Find DC3 AT_LIMIT functions where RB3 has the same symbol at 100%.

The upstream-port workflow (docs/decomp/UPSTREAM_PORT_WORKFLOW.md) is the
fastest way to recover AT_LIMIT functions in shared engine code: when a
related decomp tree has the function at 100%, port their source verbatim
and re-measure. This tool produces a ranked candidate list.

The join is normalized-demangled-name based:
  - DC3 uses MSVC mangling -> "public: virtual void __cdecl Foo::Bar(class X)"
  - RB3 uses MetroWerks   -> "Foo::Bar(X)"
Both reduce to "Foo::Bar" for the join key. Param-list overload ambiguity
is reported separately so a human can disambiguate.

Usage:
    python3 scripts/at_limit_rb3_candidates.py              # top 30 by size
    python3 scripts/at_limit_rb3_candidates.py --limit 200
    python3 scripts/at_limit_rb3_candidates.py --unit-pattern '%/char/%'
    python3 scripts/at_limit_rb3_candidates.py --min-percent 90 --min-size 500
    python3 scripts/at_limit_rb3_candidates.py --json > candidates.json
"""

from __future__ import annotations

import argparse
import json
import re
import sqlite3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DC3_DB = ROOT / "decomp.db"
RB3_DB = (ROOT.parent / "rb3" / "decomp.db").resolve()
RB3_SRC = (ROOT.parent / "rb3" / "src").resolve()

# DC3 MSVC-style demangled name decoration we strip before joining.
_PREFIX = re.compile(r"^(public|protected|private)\s*:\s*(virtual\s+)?(static\s+)?")
_CALLCONV = re.compile(r"\b__(cdecl|thiscall|stdcall|fastcall)\b\s*")
# Pull the qualified name (Foo::Bar, Outer::Foo::Bar) up to the open paren.
_QNAME = re.compile(r"((?:[\w<>~]+::)+~?[\w<>~]+)\s*\(")
# Free function fallback (e.g. "void __cdecl ArchiveInit(void)" -> "ArchiveInit")
_FREE = re.compile(r"(?:^|\s)(\w+)\s*\(")


def normalize(demangled: str | None) -> str | None:
    """Reduce a demangled name to a comparable canonical key.

    Returns None for blank input and ICF-merged thunks (which carry no
    meaningful name to join on).
    """
    if not demangled or demangled.startswith("merged_"):
        return None
    s = _PREFIX.sub("", demangled)
    s = _CALLCONV.sub("", s)
    m = _QNAME.search(s)
    if m:
        return m.group(1)
    m2 = _FREE.search(s)
    return m2.group(1) if m2 else None


def rb3_source_path(unit: str) -> str | None:
    """Map an RB3 unit (e.g. 'main/system/world/Spotlight') to its source file.

    Returns the relative path if it exists, or None.
    """
    if not unit or not unit.startswith("main/"):
        return None
    rel = unit[len("main/") :] + ".cpp"
    candidate = RB3_SRC / rel
    if candidate.exists():
        try:
            return str(candidate.relative_to(ROOT.parent))
        except ValueError:
            return str(candidate)
    return None


# Platform-divergent unit suffixes — if the DC3 and RB3 units only match on
# the class name but the platform suffix differs, the implementations are
# almost certainly different. Filter these out to avoid wasted porting.
_PLATFORM_SUFFIXES = ("_Xbox", "_Wii", "_PS3", "_Win", "_OSX", "_Mac")


def units_platform_divergent(dc3_unit: str, rb3_unit: str) -> bool:
    dc3_tail = dc3_unit.rsplit("/", 1)[-1]
    rb3_tail = rb3_unit.rsplit("/", 1)[-1]
    if dc3_tail == rb3_tail:
        return False
    for s in _PLATFORM_SUFFIXES:
        if dc3_tail.endswith(s) or rb3_tail.endswith(s):
            return True
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--min-percent", type=float, default=95.0, help="Min DC3 match%% (default: 95)")
    ap.add_argument("--max-percent", type=float, default=100.0, help="Max DC3 match%% (default: 100)")
    ap.add_argument("--min-size", type=int, default=0, help="Min DC3 function size in bytes (default: 0)")
    ap.add_argument("--unit-pattern", default="default/system/%", help="DC3 unit LIKE pattern (default: default/system/%%)")
    ap.add_argument("--exclude-pattern", default="%xdk%", help="DC3 unit NOT LIKE pattern (default: %%xdk%%)")
    ap.add_argument("--limit", type=int, default=30, help="Max candidates to print (default: 30)")
    ap.add_argument("--json", action="store_true", help="Emit JSON instead of text")
    ap.add_argument("--show-misses", action="store_true", help="Also list functions with no RB3 100%% match")
    args = ap.parse_args()

    if not DC3_DB.exists():
        sys.exit(f"error: DC3 DB not found at {DC3_DB}")
    if not RB3_DB.exists():
        sys.exit(f"error: RB3 DB not found at {RB3_DB}")

    rb3 = sqlite3.connect(f"file:{RB3_DB}?mode=ro", uri=True)
    rb3.row_factory = sqlite3.Row
    rb3_index: dict[str, list[sqlite3.Row]] = {}
    for row in rb3.execute("SELECT unit, current_percent, verdict, demangled FROM functions WHERE demangled IS NOT NULL"):
        n = normalize(row["demangled"])
        if n:
            rb3_index.setdefault(n, []).append(row)

    dc3 = sqlite3.connect(f"file:{DC3_DB}?mode=ro", uri=True)
    dc3.row_factory = sqlite3.Row
    dc3_rows = dc3.execute(
        """
        SELECT symbol, unit, current_percent, size, demangled
        FROM functions
        WHERE verdict = 'AT_LIMIT'
          AND current_percent >= ?
          AND current_percent <= ?
          AND size >= ?
          AND unit LIKE ?
          AND unit NOT LIKE ?
          AND COALESCE(is_stub, 0) = 0
        ORDER BY size DESC, current_percent DESC
        """,
        (args.min_percent, args.max_percent, args.min_size, args.unit_pattern, args.exclude_pattern),
    ).fetchall()

    candidates: list[dict] = []
    multi: list[dict] = []
    misses: list[dict] = []

    for row in dc3_rows:
        n = normalize(row["demangled"])
        if not n:
            continue
        rb3_complete = [m for m in rb3_index.get(n, []) if m["current_percent"] == 100.0 and m["verdict"] == "COMPLETE"]
        rb3_complete = [m for m in rb3_complete if not units_platform_divergent(row["unit"], m["unit"])]
        entry = {
            "dc3_symbol": row["symbol"],
            "dc3_demangled": row["demangled"],
            "dc3_unit": row["unit"],
            "dc3_percent": row["current_percent"],
            "dc3_size": row["size"],
            "normalized": n,
        }
        if len(rb3_complete) == 1:
            r = rb3_complete[0]
            entry["rb3_unit"] = r["unit"]
            entry["rb3_demangled"] = r["demangled"]
            entry["rb3_source"] = rb3_source_path(r["unit"])
            candidates.append(entry)
        elif len(rb3_complete) > 1:
            entry["rb3_matches"] = [
                {"unit": r["unit"], "demangled": r["demangled"], "source": rb3_source_path(r["unit"])}
                for r in rb3_complete
            ]
            multi.append(entry)
        else:
            misses.append(entry)

    if args.json:
        json.dump(
            {"candidates": candidates[: args.limit], "multi_match": multi, "misses": misses if args.show_misses else []},
            sys.stdout,
            indent=2,
        )
        return 0

    print(f"DC3 AT_LIMIT pool ({args.min_percent}-{args.max_percent}%, unit LIKE {args.unit_pattern}): {len(dc3_rows)}")
    print(f"  Unique RB3 100% match: {len(candidates)}")
    print(f"  Multiple RB3 100% (overload ambiguity): {len(multi)}")
    print(f"  No RB3 100% match: {len(misses)}")
    print()

    if candidates:
        print(f"=== Top {min(args.limit, len(candidates))} candidates (by DC3 size desc) ===")
        for c in candidates[: args.limit]:
            src = c["rb3_source"] or "(source path not found)"
            print(f"  DC3 {c['dc3_percent']:5.1f}%  sz={c['dc3_size']:5}  {c['normalized']}")
            print(f"    DC3: {c['dc3_unit']}")
            print(f"    RB3: {c['rb3_unit']}  -> {src}")
    if multi:
        print(f"\n=== {len(multi)} multi-match (manual disambig) ===")
        for c in multi[:5]:
            print(f"  DC3 {c['dc3_percent']:5.1f}%  sz={c['dc3_size']:5}  {c['normalized']}")
            for m in c["rb3_matches"]:
                print(f"    candidate: {m['unit']}")
    if args.show_misses and misses:
        print(f"\n=== {len(misses)} misses (no RB3 100% match) ===")
        for c in misses[: args.limit]:
            print(f"  DC3 {c['dc3_percent']:5.1f}%  sz={c['dc3_size']:5}  {c['normalized']}  ({c['dc3_unit']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
