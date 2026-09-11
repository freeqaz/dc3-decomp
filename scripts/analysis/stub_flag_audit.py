#!/usr/bin/env python3
"""Re-derive ``decomp.db.functions.is_stub`` from first principles, and repair it.

WHY THIS EXISTS
===============
``is_stub`` has been documented as one of the columns you *can* trust
(``docs/decomp/REMAINING_WORK.md``, "Columns that are reliable").  It is not.
A read of the 675 rows carrying ``is_stub = 1`` finds three populations mixed
together, only one of which is a stub:

  * ``fn_<addr>`` MSVC EH **funclets** sitting at 99.9% with a stale
    "no body emitted" reason.  A funclet is not a stub -- the enclosing
    function has a body, and objdiff pairs these by MASKED BYTE SIGNATURE
    (``docs/decomp/patterns/...``, ``reference_objdiff_funclet_pairing``), so
    the pairing itself is a guess.
  * **retired spellings**: symbols dtk no longer emits, because the
    ``/OPT:ICF`` fold-survivor rename retired that spelling.  They are not in
    ``report.json`` at all, so nothing measures them and nothing clears them.
  * genuine stubs -- a name the target defines with a real body that our tree
    does not define at all.

THE WRITER DEFECT
=================
``scripts/sync_objdiff.py`` is the only routine writer.  Its stub rule is

    base_size == 0   ->   is_stub = 1          (as "unimplemented" or "skipped")
    base_size  > 0   ->   is_stub = 0          (stub_clears)

and the clear side is the bug: it only fires for rows objdiff actually
**measured**.  A row whose symbol objdiff cannot find takes the
``error == "not_found"`` branch, which ``continue``s without touching
``is_stub``.  So the moment a spelling is retired, its stale ``is_stub = 1``
becomes permanent and unreachable by the writer that set it.  Same shape for a
funclet row whose ``fn_<addr>`` name churns when the target is re-split: the
old name is never looked at again.

WHAT THIS SCRIPT PROVES, AND WHAT IT DOES NOT
=============================================
Four independent substrates, none of them the DB itself:

  1. ``build/<title>/report.json``        -- is the symbol measured at all, and at what %
  2. COFF symbol tables of our ``.obj``  -- do WE define it (section number > 0), and how big
  3. ``orig/<title>/ham_xbox_r.map``     -- is the address an ``__unwind$`` / ``__catch$`` funclet
  4. ``config/<title>/symbols.txt``      -- dtk's own name for that address

``REAL_STUB`` is a *lead* and states only: the target has a body here, we emit
none.  It does not prove the body is authorable (many are XDK/Bink/platform).
``NOT_A_STUB`` is a proof in the direction that matters -- we demonstrably emit
a body, so the flag is wrong whatever else is true.

Read-only by default.  ``--apply`` requires the MAIN checkout: a worktree's
``decomp.db`` is a deliberate tripwire and its real DB is shared, so a
worktree must never write it.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sqlite3
import struct
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
VERSION = "373307D9"


def main_checkout(start: Path = REPO_ROOT) -> Path:
    """The MAIN checkout, even when we are running inside a worktree.

    ``git rev-parse --git-common-dir`` is the idiom the repo already uses for
    this (``MILO_ENGINE_PATH``'s worktree bug).  It matters here because a
    worktree's ``decomp.db`` is a deliberately-invalid tripwire file, so the
    default DB must never be resolved relative to ``__file__``.
    """
    import subprocess
    try:
        common = subprocess.run(
            ["git", "-C", str(start), "rev-parse", "--git-common-dir"],
            capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return start
    p = Path(common)
    if not p.is_absolute():
        p = (start / p).resolve()
    return p.parent


def in_worktree(start: Path = REPO_ROOT) -> bool:
    import subprocess
    try:
        r = subprocess.run(["git", "-C", str(start), "rev-parse",
                            "--git-common-dir", "--git-dir"],
                           capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return False
    lines = r.stdout.split()
    if len(lines) != 2:
        return False
    a, b = (Path(x) if Path(x).is_absolute() else (start / x) for x in lines)
    return os.path.realpath(a) != os.path.realpath(b)

FN_ADDR_RE = re.compile(r"^fn_([0-9A-Fa-f]{8})$")
FUNCLET_PREFIXES = ("__unwind$", "__catch$", "__ehhandler$", "__tryblocktable$")

# Classification vocabulary.  Order is the reporting order.
CLASSES = (
    "FUNCLET_MISLABELLED",
    "UNNAMED_TARGET_SYMBOL",
    "RETIRED_SPELLING",
    "NOT_A_STUB",
    "REAL_STUB",
)

PROVENANCE_TAG = "stub_flag_audit"


# --------------------------------------------------------------------- COFF

def coff_defined_symbols(path: str) -> dict[str, int]:
    """name -> size in bytes of the section that defines it, for DEFINED symbols.

    Xbox 360 COFF (machine 0x1f2); llvm will not parse it, so the 18-byte
    symbol records, the section headers and the string table are read directly.

    "Defined" means ``SectionNumber > 0`` (0 is IMAGE_SYM_UNDEFINED, negative
    values are ABSOLUTE/DEBUG).  With MSVC ``/Gy`` every function lands in its
    own COMDAT section, so the section's ``SizeOfRawData`` is the function's
    size to within padding -- good enough to tell a real body from a ``blr``.
    """
    try:
        with open(path, "rb") as fh:
            d = fh.read()
    except OSError:
        return {}
    if len(d) < 20:
        return {}
    nsections, _, symptr, nsym = struct.unpack_from("<HIII", d, 2)
    if symptr == 0 or nsym == 0 or symptr + nsym * 18 > len(d):
        return {}

    # Section headers follow the 20-byte file header + optional header.
    opthdr = struct.unpack_from("<H", d, 16)[0]
    sec_off = 20 + opthdr
    sec_size: dict[int, int] = {}
    for i in range(nsections):
        off = sec_off + i * 40
        if off + 40 > len(d):
            break
        sec_size[i + 1] = struct.unpack_from("<I", d, off + 16)[0]  # SizeOfRawData

    strtab = symptr + nsym * 18
    out: dict[str, int] = {}
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
        secnum = struct.unpack_from("<h", d, off + 12)[0]
        naux = d[off + 17]
        if name and secnum > 0:
            size = sec_size.get(secnum, 0)
            # Keep the largest definition seen (a name can be defined once per
            # object; across objects prefer the one with a real body).
            if size > out.get(name, -1):
                out[name] = size
        i += 1 + naux
    return out


def our_defined_universe(project_dir: Path) -> tuple[dict[str, int], int]:
    build = project_dir / "build" / VERSION
    paths: list[str] = []
    for root in (build / "src", build / "pch"):
        paths += glob.glob(os.path.join(str(root), "**", "*.obj"), recursive=True)
    universe: dict[str, int] = {}
    for p in sorted(paths):
        for name, size in coff_defined_symbols(p).items():
            if size > universe.get(name, -1):
                universe[name] = size
    return universe, len(paths)


# ---------------------------------------------------------------- report.json

def report_index(project_dir: Path) -> dict[str, dict]:
    path = project_dir / "build" / VERSION / "report.json"
    with open(path) as fh:
        report = json.load(fh)
    out: dict[str, dict] = {}
    for unit in report.get("units") or []:
        for fn in unit.get("functions") or []:
            name = fn.get("name")
            if not name:
                continue
            rec = {
                "unit": unit.get("name"),
                "size": int(fn.get("size") or 0),
                "normalized": fn.get("match_percent_normalized"),
                "fuzzy": fn.get("fuzzy_match_percent"),
            }
            # A symbol can appear in several units (COMDAT).  Keep the best
            # measured one -- that is the one a percentage would be quoted from.
            prev = out.get(name)
            if prev is None or (rec["normalized"] or 0) > (prev["normalized"] or 0):
                out[name] = rec
    return out


# ------------------------------------------------------------------- the map

def map_index(repo_root: Path) -> dict[int, list[str]]:
    """VA -> every name the shipped linker map parks there."""
    path = repo_root / "orig" / VERSION / "ham_xbox_r.map"
    out: dict[int, list[str]] = {}
    with open(path, encoding="latin1") as fh:
        for line in fh:
            parts = line.split()
            # " 0005:00001200       __unwind$303813   82331200 f i App.obj"
            if len(parts) < 3 or ":" not in parts[0]:
                continue
            try:
                va = int(parts[2], 16)
            except ValueError:
                continue
            if va < 0x82000000 or va > 0x84000000:
                continue
            out.setdefault(va, []).append(parts[1])
    return out


def symbols_txt_index(repo_root: Path) -> dict[int, str]:
    """VA -> dtk's own name, straight out of config/<v>/symbols.txt."""
    path = repo_root / "config" / VERSION / "symbols.txt"
    out: dict[int, str] = {}
    pat = re.compile(r"^(\S+)\s*=\s*\.text:0x([0-9A-Fa-f]+)")
    with open(path, encoding="latin1") as fh:
        for line in fh:
            m = pat.match(line)
            if m:
                out[int(m.group(2), 16)] = m.group(1)
    return out


# ------------------------------------------------------- name normalization

# An anonymous namespace mangles as ``?A0x<8 hex>`` and the hash is a function
# of the TU's *path*, so ours legitimately differs from the shipped one; the
# anon-ns obj patcher rewrites it after the fact (and was blind to nested
# namespaces until 8bdc5baee).  A function-local static's scope ordinal
# (``@?BJ@??``) is a per-TU counter.  Both are documented in CLAUDE.md as
# noise classes that normalization forgives -- so neither may be allowed to
# manufacture a "we emit no body" finding.  Measured: without this, 26 rows
# that our objects demonstrably define were classified REAL_STUB, including
# every ``FriendEnumRequest`` list method in PlatformMgr_Xbox.
ANON_NS_RE = re.compile(r"\?A0x[0-9a-f]{8}")
SCOPE_ORD_RE = re.compile(r"@\?[0-9A-Z]{1,4}@\?\?")


def normalize_name(name: str) -> str:
    n = ANON_NS_RE.sub("?A0x@@", name)
    n = SCOPE_ORD_RE.sub("@?@@??", n)
    return n


def index_normalized(d: dict) -> dict:
    """Fold a name->value map onto normalized keys, keeping the best value."""
    out: dict = {}
    for k, v in d.items():
        nk = normalize_name(k)
        if nk == k:
            continue
        prev = out.get(nk)
        if prev is None:
            out[nk] = v
        elif isinstance(v, int) and isinstance(prev, int) and v > prev:
            out[nk] = v
    return out


# ------------------------------------------------------------ classification

def is_funclet_name(name: str) -> bool:
    return name.startswith(FUNCLET_PREFIXES)


def classify(row: dict, report: dict[str, dict], defined: dict[str, int],
             vamap: dict[int, list[str]], symtxt: dict[int, str],
             report_n: dict[str, dict] | None = None,
             defined_n: dict[str, int] | None = None) -> dict:
    """Decide one row's class, and record the evidence that decided it."""
    sym = row["symbol"]
    ev: list[str] = []

    # --- funclet, by name or by address ---
    funclet = False
    unnamed = False
    if is_funclet_name(sym):
        funclet = True
        ev.append("name is an EH funclet spelling")
    m = FN_ADDR_RE.match(sym)
    if m:
        va = int(m.group(1), 16)
        names = vamap.get(va, [])
        dtk = symtxt.get(va)
        hit = [n for n in names if is_funclet_name(n)]
        if hit:
            funclet = True
            ev.append(f"map@{va:08x} = {hit[0]}")
        elif dtk and is_funclet_name(dtk):
            funclet = True
            ev.append(f"symbols.txt@{va:08x} = {dtk}")
        else:
            # An unnamed fn_<addr> is byte-signature-paired by objdiff whatever
            # it is, so it is never authorable source under THAT spelling and
            # can never be a stub.  Say which it is rather than calling
            # everything a funclet: the map often has a real name there, and
            # whether we define THAT name is the interesting fact.
            unnamed = True
            real = names[0] if names else None
            if real:
                mine = defined.get(real)
                ev.append(f"unnamed fn_ at {va:08x}; map says {real}"
                          + (f", which we define ({mine} B)" if mine
                             else ", which we do NOT define"))
            else:
                ev.append(f"unnamed fn_ at {va:08x}, absent from map")

    nsym = normalize_name(sym)
    rep = report.get(sym)
    if rep is None and report_n is not None and nsym != sym:
        rep = report_n.get(nsym)
        if rep is not None:
            ev.append("report.json has it modulo anon-ns/scope-ordinal noise")
    ours = defined.get(sym)
    if ours is None and defined_n is not None and nsym != sym:
        ours = defined_n.get(nsym)
        if ours is not None:
            ev.append("we define it modulo anon-ns/scope-ordinal noise")

    if funclet:
        cls = "FUNCLET_MISLABELLED"
    elif unnamed:
        cls = "UNNAMED_TARGET_SYMBOL"
    elif rep is None:
        cls = "RETIRED_SPELLING"
        ev.append("absent from report.json")
        if ours is not None:
            ev.append(f"but our objects define it ({ours} B section)")
    elif ours is not None and ours > 0:
        cls = "NOT_A_STUB"
        ev.append(f"we define it ({ours} B section), "
                  f"norm={rep['normalized']}")
    else:
        cls = "REAL_STUB"
        ev.append(f"target size {rep['size']} B, we define no body, "
                  f"norm={rep['normalized']}")

    return {
        "id": row["id"],
        "symbol": sym,
        "demangled": row["demangled"],
        "unit": row["unit"],
        "size": row["size"],
        "excluded": row["excluded"],
        "verdict": row["verdict"],
        "report_norm": rep["normalized"] if rep else None,
        "report_size": rep["size"] if rep else None,
        "our_section_bytes": ours,
        "klass": cls,
        "evidence": "; ".join(ev),
    }


# ------------------------------------------------------------------- the DB

def open_db(path: Path, write: bool) -> sqlite3.Connection:
    if write:
        conn = sqlite3.connect(str(path))
    else:
        conn = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    conn.row_factory = sqlite3.Row
    return conn


def load_rows(conn: sqlite3.Connection) -> list[dict]:
    cur = conn.execute(
        "SELECT id, symbol, demangled, unit, size, excluded, verdict, verdict_reason "
        "FROM functions WHERE is_stub = 1 ORDER BY symbol")
    return [dict(r) for r in cur.fetchall()]


# ---------------------------------------------------------------------- main

def run(project_dir: Path, db_path: Path, apply: bool,
        json_out: Path | None, quiet: bool = False,
        skip: frozenset[str] = frozenset()) -> dict:
    conn = open_db(db_path, write=apply)
    rows = load_rows(conn)

    report = report_index(project_dir)
    defined, n_objs = our_defined_universe(project_dir)
    vamap = map_index(project_dir)
    symtxt = symbols_txt_index(project_dir)

    report_n = index_normalized(report)
    defined_n = index_normalized(defined)

    results = [classify(r, report, defined, vamap, symtxt, report_n, defined_n)
               for r in rows]

    counts = {c: 0 for c in CLASSES}
    for r in results:
        counts[r["klass"]] += 1

    clears = [r for r in results
              if r["klass"] != "REAL_STUB" and r["symbol"] not in skip]
    held = [r for r in results
            if r["klass"] != "REAL_STUB" and r["symbol"] in skip]

    if not quiet:
        print(f"stub_flag_audit -- {len(rows)} rows with is_stub=1")
        print(f"  report.json:   {len(report)} symbols  ({project_dir})")
        print(f"  our objects:   {n_objs} .obj, {len(defined)} defined symbols")
        print(f"  linker map:    {len(vamap)} addresses")
        print()
        for c in CLASSES:
            print(f"  {c:22s} {counts[c]:5d}")
        print()
        print(f"  would clear is_stub on {len(clears)} rows "
              f"({'APPLIED' if apply else 'dry-run, nothing written'})")
        if held:
            print(f"  held back (--skip-symbol, owned elsewhere): "
                  f"{', '.join(r['symbol'] for r in held)}")

    if apply:
        for r in clears:
            note = (f"{PROVENANCE_TAG}: is_stub cleared -- {r['klass']} "
                    f"({r['evidence']})")
            conn.execute(
                "UPDATE functions SET is_stub = 0, verdict_reason = ?, "
                "updated_at = CURRENT_TIMESTAMP WHERE id = ?",
                (note, r["id"]))
        conn.commit()
    conn.close()

    if json_out:
        json_out.write_text(json.dumps(
            {"counts": counts, "rows": results}, indent=2))

    return {"counts": counts, "rows": results, "cleared": len(clears)}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--project-dir", default=str(REPO_ROOT),
                    help="tree whose build/<title>/report.json and .obj are read")
    ap.add_argument("--db", default=None,
                    help="decomp.db to read (default: the main checkout's)")
    ap.add_argument("--apply", action="store_true",
                    help="clear is_stub on every non-REAL_STUB row "
                         "(MAIN checkout only)")
    ap.add_argument("--json", default=None, help="write the full table here")
    ap.add_argument("--skip-symbol", action="append", default=[],
                    metavar="SYMBOL",
                    help="classify but never WRITE this row -- for a symbol "
                         "another lane is holding.  Repeatable.")
    ap.add_argument("--list", action="store_true",
                    help="print every row, grouped by class")
    args = ap.parse_args(argv)

    project_dir = Path(args.project_dir).resolve()
    db_path = (Path(args.db).resolve() if args.db
               else main_checkout() / "decomp.db")

    if args.apply and in_worktree():
        # The shared DB must never be written from a worktree: the worktree's
        # own decomp.db is a tripwire, and pointing --db at the main one from a
        # worktree is exactly the mistake that tripwire exists to stop.
        print("REFUSED: --apply from a worktree.  Run it from the main checkout.",
              file=sys.stderr)
        return 2

    out = run(project_dir, db_path, args.apply,
              Path(args.json) if args.json else None,
              skip=frozenset(args.skip_symbol))

    if args.list:
        for c in CLASSES:
            rows = [r for r in out["rows"] if r["klass"] == c]
            if not rows:
                continue
            print(f"\n=== {c}  ({len(rows)}) ===")
            for r in sorted(rows, key=lambda x: -(x["report_size"] or 0)):
                print(f"  {r['report_size'] or 0:6d} B  "
                      f"norm={str(r['report_norm']):>8s}  {r['unit']}  {r['symbol']}")
                print(f"           {r['evidence']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
