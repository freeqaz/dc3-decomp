#!/usr/bin/env python3
"""Does our tree emit that callee ANYWHERE?  The one question a funclet row answers.

WHY THIS EXISTS
===============
56 of the 62 ``WRONG_CALLEE`` rows in pattern scan 18 (name_check, objdiff 4.2.8,
whole binary) also carry ``UNVERIFIABLE_PAIRING``.  Their enclosing symbol is an
unnamed ``fn_<addr>`` MSVC EH funclet, which objdiff pairs by MASKED BYTE
SIGNATURE -- and byte-identical funclets pair arbitrarily.  So "the target calls
X here, we call Y" is, on those rows, a statement about objdiff's *guess*.  As a
name-level adjudication they are dead: you cannot read a source bug off a pair
objdiff invented.  ``query_functions`` now subtracts them by default.

**Unverifiable is not uninformative.**  There is exactly one falsifiable question
those rows still answer, and it does not depend on the pairing being right:

    the target image calls X somewhere in this unit.
    Does OUR tree emit X anywhere at all?

If it does not, then whatever else is or is not true about the pairing, a name
the original binary uses is a name we never produce -- and that is a source-level
statement.  Asked of all 58 rows of scan 18's class by the callee-13 lane, it
flagged **7**, and all 7 were real source defects, 0 false positives, negative-
controlled against the pre-fix tree:

  * ``MemTemp`` was a FABRICATED duplicate of ``MemDoTempAllocations`` -- it
    appears 0x in the retail map.  Our out-of-line fabricated dtor held the real
    name and blocked the header COMDAT, pinning that function at 0.0%.
  * ``aligned_vector<float>`` is a real container the image has (PitchDetector.obj)
    beside ``vector<float, XboxAllocator<float>>``; we had only the latter.
  * ``MMRESULT`` was typedef'd signed, so every mmio return test compiled ``cmpwi``
    against the image's ``cmplwi``.

WHAT IS AND IS NOT PROVED
=========================
``NOT_EMITTED`` is a lead with a very good hit rate, not a proof of a bug: the
target name may be an inline the original emitted out-of-line, or a fold-group
member whose spelling we legitimately never chose.  Read it as "a name the image
has and we do not" and go look.

``EMITTED`` proves only that the NAME exists somewhere in our build.  It says
nothing about whether this call site is right -- and on a guessed pairing there
is no "this call site" to speak of.  It is a clear, not an all-clear.

HOW A NAME IS RESOLVED
======================
A callee name is checked as an EQUIVALENCE CLASS, never as a literal string,
because /OPT:ICF folded many bodies and the retail map prints whichever member
the linker kept:

  1. ``scripts/symbol_aliases.json`` -- the project's adjudicated fold groups
     (survivor + folded), each carrying its own evidence.
  2. ``orig/373307D9/ham_xbox_r.map`` -- resolve the name to an address, then
     take EVERY name the linker parked at that address (the live fold group).

The class is the union.  A single member found in our objects clears the row;
that is deliberately the permissive direction, so a flag is hard to earn.

OUR SIDE is the COFF symbol tables of ``build/<title>/src/**/*.obj`` plus the
PCH object -- every symbol name, any storage class, defined or undefined.  No
link, no optimizer in the loop, ~1 s over ~990 objects.  Including statics and
undefined externals is the permissive direction again: a *reference* to X counts
as emitting X, because a reference is a name our source spelled.

EXIT CODES
==========
  0  ran; nothing flagged
  1  rows FLAGGED -- our tree never emits a callee the image uses.  Signal, not
     a tool failure; the header above says how much it proves.
  2  UNREADABLE -- no objects (run `ninja`), no linker map, or no such scan
  5  SELFTEST vacuous or failed -- the check could not be made to discriminate

USAGE
=====
  python3 scripts/analysis/callee_emitted_anywhere.py --selftest
  python3 scripts/analysis/callee_emitted_anywhere.py --scan-id 18 \
      --db /home/free/code/milohax/dc3-decomp/decomp.db
  ... --pattern TEMPLATE_INSTANTIATION_MISMATCH --rows funclet --json out.json

From a worktree the bare ``decomp.db`` is a deliberate tripwire; pass ``--db``
naming the main checkout's database.  This script only ever opens it READ-ONLY.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import sqlite3
import struct
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from scripts.orchestrator.callee_gate import (  # noqa: E402
    LinkerMap, LinkerMapError, load_linker_map,
)
from scripts.orchestrator.database import (  # noqa: E402
    PAIRING_SENSITIVE_PATTERNS, UNVERIFIABLE_PAIRING,
)

VERSION = "373307D9"
ALIASES_JSON = REPO_ROOT / "scripts" / "symbol_aliases.json"

#: A name that cannot be in any real object.  Used by --selftest as the
#: "certainly not emitted" probe; the space makes it unspellable by a mangler.
DECOY = "?not a real symbol@CalleeEmittedAnywhereSelftest@@YAXXZ"

EXIT_OK, EXIT_FLAGGED, EXIT_UNREADABLE, EXIT_SELFTEST = 0, 1, 2, 5


# ---------------------------------------------------------------- our objects

def coff_symbol_names(path: str) -> set[str]:
    """EVERY symbol name in a COFF object -- any storage class, defined or not.

    Deliberately wider than
    ``check_undefined_decomp_symbols.coff_externals``, which reads only
    ``IMAGE_SYM_CLASS_EXTERNAL``.  A static (anon-namespace) definition is still
    a name our source spelled, and missing it would manufacture a NOT_EMITTED
    flag for a callee we do emit -- a false positive in the one direction this
    check must not have.  ``--selftest`` cross-checks the external subset
    against that reader, so the widening cannot silently become a rewrite.

    These are Xbox 360 COFF (machine 0x1f2), which llvm will not parse; the
    18-byte symbol record and the string table are read directly.
    """
    with open(path, "rb") as fh:
        d = fh.read()
    if len(d) < 20:
        return set()
    symptr, nsym = struct.unpack_from("<II", d, 8)
    if symptr == 0 or nsym == 0 or symptr + nsym * 18 > len(d):
        return set()
    strtab = symptr + nsym * 18
    names: set[str] = set()
    i = 0
    while i < nsym:
        off = symptr + i * 18
        raw = d[off:off + 8]
        if raw[:4] == b"\x00\x00\x00\x00":
            so = struct.unpack_from("<I", d, off + 4)[0]
            try:
                end = d.index(b"\x00", strtab + so)
            except ValueError:
                break
            name = d[strtab + so:end].decode("latin1")
        else:
            name = raw.rstrip(b"\x00").decode("latin1")
        naux = d[off + 17]
        if name:
            names.add(name)
        i += 1 + naux
    return names


def our_object_paths(project_dir: Path) -> list[str]:
    build = project_dir / "build" / VERSION
    out: list[str] = []
    for root in (build / "src", build / "pch"):
        out += glob.glob(os.path.join(str(root), "**", "*.obj"), recursive=True)
    return sorted(out)


def emitted_universe(project_dir: Path) -> tuple[set[str], int]:
    """Every symbol name our built objects carry, and how many objects supplied it."""
    paths = our_object_paths(project_dir)
    universe: set[str] = set()
    for p in paths:
        universe |= coff_symbol_names(p)
    return universe, len(paths)


# ------------------------------------------------------------ name resolution

def load_alias_groups(path: Path = ALIASES_JSON) -> dict[str, set[str]]:
    """name -> every name in its adjudicated ICF fold group."""
    if not path.exists():
        return {}
    try:
        doc = json.loads(path.read_text())
    except (OSError, ValueError):
        return {}
    index: dict[str, set[str]] = {}
    for group in doc.get("groups", []) or []:
        members = set(group.get("folded") or [])
        survivor = group.get("survivor")
        if survivor:
            members.add(survivor)
        for m in members:
            index.setdefault(m, set()).update(members)
    return index


def alias_class(name: str, lmap: LinkerMap,
                groups: dict[str, set[str]]) -> tuple[set[str], str | None]:
    """Every spelling that names the same code as `name`, plus its map address.

    The address is returned because its ABSENCE is itself a finding: a callee the
    retail map has never heard of (``MemTemp``) is a name the original binary does
    not contain, which is a different and stronger statement than "we do not emit
    it".
    """
    names = {name} | groups.get(name, set())
    addr = lmap.address(name)
    if addr:
        names.update(lmap.group(addr))
        for n in list(names):
            names.update(groups.get(n, set()))
    return names, addr


# ----------------------------------------------------------------- scan rows

def open_db_readonly(path: Path) -> sqlite3.Connection:
    conn = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    conn.row_factory = sqlite3.Row
    return conn


def scan_rows(conn: sqlite3.Connection, scan_id: int, pattern: str,
              rows_filter: str) -> list[dict]:
    """One entry per (function, divergent callee pair) in the class.

    `rows_filter`: 'any' (default -- the honest denominator), 'funclet' (only
    rows objdiff declared UNVERIFIABLE_PAIRING), 'named' (only the rest).
    """
    paired = {r[0] for r in conn.execute(
        "SELECT function_id FROM function_patterns "
        "WHERE scan_id = ? AND pattern = ?", (scan_id, UNVERIFIABLE_PAIRING))}
    out: list[dict] = []
    for row in conn.execute(
            "SELECT f.id, f.symbol, f.unit, p.details "
            "FROM function_patterns p JOIN functions f ON f.id = p.function_id "
            "WHERE p.scan_id = ? AND p.pattern = ? ORDER BY f.unit, f.symbol",
            (scan_id, pattern)):
        guessed = row["id"] in paired
        if rows_filter == "funclet" and not guessed:
            continue
        if rows_filter == "named" and guessed:
            continue
        try:
            payload = json.loads(row["details"] or "{}")
        except ValueError:
            payload = {}
        pairs = _callee_pairs(payload)
        if not pairs:
            # No evidence is not a clear. Carry the row with a null callee so it
            # shows up in the denominator rather than vanishing from it.
            out.append({"function": row["symbol"], "unit": row["unit"],
                        "pairing": "guessed" if guessed else "asserted",
                        "target_callee": None, "base_callee": None,
                        "reconstructed": False})
            continue
        for target, base, recon in pairs:
            out.append({"function": row["symbol"], "unit": row["unit"],
                        "pairing": "guessed" if guessed else "asserted",
                        "target_callee": target, "base_callee": base,
                        "reconstructed": recon})
    return out


#: objdiff's MakeString detector reports the template ARGUMENT LIST, not the
#: symbol: `W4_D3DFORMAT@@@@YAPBDPBDABW4_D3DFORMAT@@@Z` is the tail of
#: `??$MakeString@W4_D3DFORMAT@@@@YAPBDPBDABW4_D3DFORMAT@@@Z`. Re-attaching the
#: prefix is a GUESS about the detector's payload shape, so every reconstructed
#: name is required to resolve in the retail linker map before its verdict is
#: believed (see `adjudicate`). Without that, one wrong prefix would flag the
#: entire class as "a name the image has and we do not".
MAKESTRING_PREFIX = "??$MakeString@"


def _callee_pairs(payload: dict) -> list[tuple[str | None, str | None, bool]]:
    """(target, base, reconstructed) for each divergent callee in a payload.

    Two payload shapes are in the wild, both from objdiff 4.2.8:
      * `divergent_callees` -- full mangled symbols (WRONG_CALLEE,
        TEMPLATE_INSTANTIATION_MISMATCH).
      * `mismatches` with `target_template`/`base_template` -- template argument
        lists (MAKESTRING_TEMPLATE_MISMATCH).
    """
    pairs: list[tuple[str | None, str | None, bool]] = []
    for pair in payload.get("divergent_callees") or []:
        pairs.append((pair.get("target_symbol"), pair.get("base_symbol"), False))
    for m in payload.get("mismatches") or []:
        tt, bt = m.get("target_template"), m.get("base_template")
        if tt or bt:
            pairs.append((MAKESTRING_PREFIX + tt if tt else None,
                          MAKESTRING_PREFIX + bt if bt else None, True))
    return pairs


# --------------------------------------------------------------- the question

def adjudicate(rows: list[dict], universe: set[str], lmap: LinkerMap,
               groups: dict[str, set[str]]) -> list[dict]:
    for r in rows:
        target = r.get("target_callee")
        if not target:
            r["verdict"] = "NO_EVIDENCE"
            r["reason"] = "detector payload carried no callee"
            r["alias_class_size"] = 0
            r["in_retail_map"] = None
            r["matched_spelling"] = None
            continue
        names, addr = alias_class(target, lmap, groups)
        hit = sorted(names & universe)
        r["alias_class_size"] = len(names)
        r["in_retail_map"] = addr is not None
        r["matched_spelling"] = hit[0] if hit else None
        if r.get("reconstructed") and addr is None and not hit:
            # The name was rebuilt from a template-argument fragment, and it
            # resolves NOWHERE -- not in the image, not in our build. A target
            # name that is absent from the TARGET's own linker map is evidence
            # that the reconstruction is wrong, not that the image lacks the
            # symbol, so this must not be served as NOT_EMITTED.
            r["verdict"] = "NO_EVIDENCE"
            r["reason"] = ("name reconstructed from a template fragment does "
                           "not resolve in the retail map: cannot tell a real "
                           "gap from a bad reconstruction")
            continue
        r["verdict"] = "EMITTED" if hit else "NOT_EMITTED"
        r["reason"] = None
    return rows


def render(rows: list[dict], universe_size: int, n_objects: int,
           scan_id: int, pattern: str, rows_filter: str) -> str:
    flagged = [r for r in rows if r["verdict"] == "NOT_EMITTED"]
    no_ev = [r for r in rows if r["verdict"] == "NO_EVIDENCE"]
    out = [
        f"callee-emitted-anywhere: scan {scan_id}, pattern {pattern}, "
        f"rows={rows_filter}",
        f"  our side : {universe_size} distinct symbol names across "
        f"{n_objects} objects",
        f"  rows     : {len(rows)} callee pairs "
        f"({sum(1 for r in rows if r['pairing'] == 'guessed')} on a GUESSED "
        f"symbol pair, "
        f"{sum(1 for r in rows if r['pairing'] == 'asserted')} name-asserted)",
        f"  verdicts : {len(rows) - len(flagged) - len(no_ev)} EMITTED, "
        f"{len(flagged)} NOT_EMITTED, {len(no_ev)} NO_EVIDENCE",
    ]
    if no_ev:
        out.append("")
        out.append(f"  {len(no_ev)} row(s) NOT ADJUDICATED -- the question was "
                   f"never asked of them, which is not an answer:")
        for reason in sorted({r.get("reason") or "unknown" for r in no_ev}):
            n = sum(1 for r in no_ev if (r.get("reason") or "unknown") == reason)
            out.append(f"    {n:4d}  {reason}")
    if not flagged:
        out.append("")
        if len(no_ev) == len(rows):
            out.append("NOTHING FLAGGED, AND NOTHING ADJUDICATED: every row in "
                       "this class was skipped for the reason(s) above. Do not "
                       "read this as a clean class -- it is an unasked question.")
        else:
            out.append("NOTHING FLAGGED: every target callee this check could "
                       "adjudicate has some spelling our tree emits. That is a "
                       "clear, not an all-clear -- see this script's header for "
                       "what EMITTED does not prove.")
        return "\n".join(out)
    out.append("")
    out.append(f"FLAGGED -- {len(flagged)} target callees our tree never emits:")
    for r in flagged:
        in_map = ("yes" if r["in_retail_map"] else
                  "NO -- the image does not contain this name either; "
                  "suspect a fabricated symbol")
        out.append(f"  {r['unit']}  [{r['pairing']} pair]")
        out.append(f"    enclosing  : {r['function']}")
        out.append(f"    target uses: {r['target_callee']}")
        out.append(f"      retail map: {in_map}; "
                   f"fold class {r['alias_class_size']} spelling(s)")
        out.append(f"    we emit    : {r['base_callee']}")
    return "\n".join(out)


# ------------------------------------------------------------------ selftest

def selftest(project_dir: Path) -> int:
    """Synthesise one row we certainly emit and one we certainly do not.

    The two verdicts MUST differ.  A check that answers the same thing either
    way is not a check, and this is the shape that has passed vacuously in this
    repo before: if `universe` came back empty (no objects built), every row on
    earth reads NOT_EMITTED and the flag list looks like a bumper harvest.
    """
    def say(ok: bool, label: str, detail: str = "") -> bool:
        print(f"  [{'ok' if ok else 'FAIL'}] {label}{'  ' + detail if detail else ''}")
        return ok

    print("callee_emitted_anywhere --selftest")
    universe, n_obj = emitted_universe(project_dir)
    if not universe:
        print(f"  [VACUOUS] no COFF symbols under "
              f"{project_dir}/build/{VERSION}/src -- run `ninja` first. Every "
              f"row would read NOT_EMITTED and the check would look productive "
              f"while measuring nothing.")
        return EXIT_SELFTEST
    ok = say(True, "our-side universe is non-empty",
             f"{len(universe)} names / {n_obj} objects")

    # The positive probe is a name taken FROM the universe, so "we emit it" is
    # true by construction and the probe cannot rot when the tree changes.
    # Prefer a real MANGLED C++ name (`?...`) over a compiler-internal label, so
    # the probe travels the same alias-resolution path a real row does.
    mangled = sorted(n for n in universe if n.startswith("?"))
    positive = (mangled or sorted(universe))[len(mangled or universe) // 2]

    try:
        lmap = load_linker_map(project_dir)
    except LinkerMapError as e:
        print(f"  [VACUOUS] {e}")
        return EXIT_SELFTEST
    groups = load_alias_groups()
    ok &= say(bool(groups), "alias groups loaded", f"{len(groups)} names")

    rows = [
        {"function": "selftest", "unit": "selftest", "pairing": "asserted",
         "target_callee": positive, "base_callee": positive},
        {"function": "selftest", "unit": "selftest", "pairing": "asserted",
         "target_callee": DECOY, "base_callee": positive},
    ]
    adjudicate(rows, universe, lmap, groups)
    ok &= say(rows[0]["verdict"] == "EMITTED",
              "a name we certainly emit reads EMITTED", f"{positive[:60]}")
    ok &= say(rows[1]["verdict"] == "NOT_EMITTED",
              "a name we certainly do NOT emit reads NOT_EMITTED", DECOY)
    if rows[0]["verdict"] == rows[1]["verdict"]:
        print("  [VACUOUS] both probes returned the SAME verdict -- this check "
              "does not discriminate and its output means nothing.")
        return EXIT_SELFTEST

    # Cross-check the widened COFF reader against the repo's existing one: the
    # external subset must agree exactly, on a real object, or `coff_symbol_names`
    # has quietly become a different reader rather than a wider one.
    try:
        sys.path.insert(0, str(REPO_ROOT / "scripts"))
        import check_undefined_decomp_symbols as cuds  # noqa: PLC0415
        # The LARGEST object, not the first: the PCH object carries 8 symbols,
        # and a superset assertion over 8 names is not a cross-check.
        probe = max(our_object_paths(project_dir), key=os.path.getsize)
        defined, undef = cuds.coff_externals(probe)
        mine = coff_symbol_names(probe)
        ok &= say((defined | undef) <= mine,
                  "widened reader is a SUPERSET of coff_externals",
                  f"{len(defined | undef)} external <= {len(mine)} total "
                  f"in {os.path.basename(probe)}")
    except (ImportError, IndexError) as e:      # pragma: no cover
        ok &= say(False, "cross-check against coff_externals", repr(e))

    print("SELFTEST PASSED" if ok else "SELFTEST FAILED")
    return EXIT_OK if ok else EXIT_SELFTEST


# ---------------------------------------------------------------------- main

def default_db() -> Path:
    """The main checkout's decomp.db when run from a worktree, else ours."""
    here = REPO_ROOT / "decomp.db"
    try:
        from scripts.orchestrator.database import shadow_target  # noqa: PLC0415
        return shadow_target(here) or here
    except Exception:                            # noqa: BLE001
        return here


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="Does our tree emit the target's callee anywhere?")
    ap.add_argument("--scan-id", type=int,
                    help="pattern_scans.id to read (see pattern_census.py)")
    ap.add_argument("--pattern", default="WRONG_CALLEE",
                    help="pattern class to read (default WRONG_CALLEE)")
    ap.add_argument("--rows", default="any", choices=("any", "funclet", "named"),
                    help="which rows: 'any' (default, the honest denominator), "
                         "'funclet' (UNVERIFIABLE_PAIRING only), 'named'")
    ap.add_argument("--db", type=Path, default=None,
                    help=f"decomp.db to read (READ-ONLY). Default: {default_db()}")
    ap.add_argument("--project", type=Path, default=REPO_ROOT,
                    help="tree whose build/<title>/src objects are 'ours'")
    ap.add_argument("--json", type=Path, default=None,
                    help="also write every row, with its verdict, here")
    ap.add_argument("--selftest", action="store_true",
                    help="negative control: two synthetic rows whose verdicts "
                         "must differ")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest(args.project)
    if args.scan_id is None:
        ap.error("--scan-id is required (or --selftest)")

    db_path = args.db or default_db()
    if not Path(db_path).exists():
        print(f"UNREADABLE: no database at {db_path}", file=sys.stderr)
        return EXIT_UNREADABLE
    try:
        conn = open_db_readonly(Path(db_path))
        scan = conn.execute("SELECT id, ruler, tool_version FROM pattern_scans "
                            "WHERE id = ?", (args.scan_id,)).fetchone()
    except sqlite3.DatabaseError as e:
        print(f"UNREADABLE: {db_path}: {e}\n"
              f"(A worktree's decomp.db is a deliberate tripwire -- pass --db "
              f"naming the main checkout's database.)", file=sys.stderr)
        return EXIT_UNREADABLE
    if scan is None:
        print(f"UNREADABLE: no pattern_scans row id={args.scan_id} in {db_path}",
              file=sys.stderr)
        return EXIT_UNREADABLE

    universe, n_obj = emitted_universe(args.project)
    if not universe:
        print(f"UNREADABLE: no COFF symbols under "
              f"{args.project}/build/{VERSION}/src -- run `ninja` first. "
              f"Answering from an empty universe would flag EVERY row.",
              file=sys.stderr)
        return EXIT_UNREADABLE
    try:
        lmap = load_linker_map(args.project)
    except LinkerMapError as e:
        print(f"UNREADABLE: {e}", file=sys.stderr)
        return EXIT_UNREADABLE

    rows = scan_rows(conn, args.scan_id, args.pattern, args.rows)
    adjudicate(rows, universe, lmap, load_alias_groups())

    print(f"scan {scan['id']}: ruler={scan['ruler']}  {scan['tool_version']}")
    if args.pattern in PAIRING_SENSITIVE_PATTERNS:
        print(f"({args.pattern} is a callee-NAME class: on a 'guessed' pair the "
              f"call site is objdiff's, but this question is not about the call "
              f"site.)")
    print(render(rows, len(universe), n_obj, args.scan_id, args.pattern,
                 args.rows))

    if args.json:
        args.json.write_text(json.dumps(
            {"scan_id": args.scan_id, "pattern": args.pattern,
             "rows_filter": args.rows, "universe_names": len(universe),
             "objects": n_obj, "rows": rows}, indent=2))
        print(f"\nwrote {len(rows)} rows -> {args.json}")

    return (EXIT_FLAGGED
            if any(r["verdict"] == "NOT_EMITTED" for r in rows) else EXIT_OK)


if __name__ == "__main__":
    sys.exit(main())
