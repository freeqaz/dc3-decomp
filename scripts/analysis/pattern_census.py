#!/usr/bin/env python3
"""Whole-binary census of objdiff's pattern detectors, with the ruler attached.

WHAT THIS REPLACES
==================
`scripts/backfill_reloc_patterns.py` answered one question -- "set these six
booleans" -- and lost everything needed to read the answer afterwards: which
ruler, which objdiff binary, which tree, and what the denominator was.  Three of
the numbers it left in `decomp.db` (`has_linker_merged` 1310,
`has_address_relocation` 680, `detected_patterns` 1500) turned out to be
unreadable for three independent reasons at once, and none of them was a wrong
detector:

  1. **Denominator.** It scanned `WHERE excluded = 0`, which is 31,446 of the
     52,568 rows.  16,922 functions that ARE in `report.json` -- diffable,
     scored, real -- were never looked at.  The number was published as if it
     were about the binary.
  2. **Tree.** It measured a tree three other worktrees were rebuilding, so
     1,310 reproduced as 1,051 on two settled trees.  `patch_guard` now closes
     this and the scan records whether it passed.
  3. **Vocabulary.** objdiff 4.2.6 split the over-broad `detect_linker_merged`
     into five classes; `LINKER_MERGED` now names ~2% of its former population
     and four new strings carry the rest.  An old count cannot be adjusted into
     a new one, because the two are not counting the same predicate.

Only (2) was ever recorded as a hazard.  This tool records all three, per scan,
in the database, so a future reader can tell a real zero from a starved one.

THE RULER IS PART OF THE RESULT
===============================
`functionRelocDiffs=none` -- objdiff's default AND what `sync_objdiff.py` runs --
makes eight detectors structurally unable to fire, because they all key off
`match_type == "diff_arg"` on a `bl` and that mode reports those instructions as
equal.  This tool REFUSES that ruler (`symbol_sweep.RelocBlindPatternError`)
rather than reporting its zeros.  `name_check` is the graded ruler and
`report.json`'s; `all` charges the ~2,992 already-adjudicated /OPT:ICF folds too
and is mostly noise.  Run both to get the artifact rate.

Usage:
    # census only, no writes (safe alongside the build fleet)
    python3 scripts/analysis/pattern_census.py --project-dir . \\
        --ruler name_check --out /tmp/census-name_check.jsonl

    # and record it
    python3 scripts/analysis/pattern_census.py --project-dir . \\
        --ruler name_check --db /path/to/main/decomp.db --apply

THE INSTRUMENT MUST HOLD STILL FOR THE LENGTH OF THE MEASUREMENT
===============================================================
`bin/objdiff-cli` is a SYMLINK into `../objdiff/target/release/`, shared with
../rb3 and ../rb3-xenon.  `target/` is mutable shared state: any `cargo build`
in that checkout -- including one a different session started -- replaces the
binary under every live consumer, and nothing in any of the three ninja graphs
notices.  On 2026-08-22 the shared binary changed identity FOUR times in one
session, twice mid-measurement; two `--version` calls issued in the same message
answered `87cc0423c05c` and `358c715835cc`.

A whole-binary census is ~30-60s of objdiff invocations, so it is wide open to
this, and the failure is silent in the worst possible way: the scan row would be
stamped with the version read at one moment while its measurements came from
another binary.  That is *precisely* the defect
`callee_gate.ensure_current_scan()` exists to catch -- laundered into the DB by
the tool that is supposed to fix it, and undetectable afterwards because the
stamp would look perfectly current.

So the census brackets its own sweep: `--version` is read (uncached) immediately
before and immediately again immediately after, and a disagreement REFUSES
`--apply` and exits 5.  That is a distinct outcome from "could not measure" --
the measurement may well be fine, but it cannot be attributed, and an
unattributable certificate is worse than none.  A `--version` read costs ~5ms
against a ~40s scan, so this is free.

--apply IS VALIDATED BEFORE THE SWEEP, NOT AFTER IT
===================================================
Every precondition for `--apply` that can be known without measuring is checked
FIRST, before the patch check and before a single objdiff process starts.  This
is not tidiness; it is the whole difference between a refusal and a loss.

Measured incident: a lane was handed
`python3 scripts/analysis/pattern_census.py --ruler name_check --apply` -- no
`--db` -- and ran it from a worktree.  Every `--apply` precondition lived at the
BOTTOM of `main()`, after the sweep, so the tool spent ~6 minutes diffing 48,290
functions and only then declined to write.  Two costs, and the second is the
expensive one:

  * six minutes of a machine that several lanes are sharing, for nothing; and
  * the refusal arrived on stderr while ~40 lines of stdout were still sitting
    in a block buffer, so it did NOT read as the last word.  The lane took the
    run for a success, and the stale scan it was sent to replace stayed in place
    reading as current -- which is the exact failure `callee_gate` exists to
    prevent, arrived at from the other side.

A destination fault is now exit 6 and takes well under a second.  It names the
database path it tried, because "it did not write" without "here is where it
looked" is not a diagnosis.  The refusals that need the report.json universe
(truncation) are taken as soon as that file is read -- still before the sweep.

Exit codes:
    0  ok
    2  --apply asked for in a combination that cannot produce a valid scan
       (--negative-control, --no-patterns)
    3  TRUNCATED by --limit (with --apply: refused, nothing written)
    4  unsettled/unpatched build tree, or the blind ruler starves the detectors
    5  the objdiff binary was REPLACED mid-scan (nothing written)
    6  --apply DESTINATION UNUSABLE: no --db, missing, not a database (a
       worktree tripwire), not writable, or owned by a different tree.
       Raised BEFORE any work; nothing is built, swept or written.
"""
from __future__ import annotations

import argparse
import collections
import json
import sqlite3
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from scripts.orchestrator import object_baseline, symbol_sweep  # noqa: E402
from scripts.orchestrator.patch_guard import (  # noqa: E402
    UnpatchedTreeError, ensure_patched_tree,
)


#: Exit code for "--apply cannot possibly land", raised before any work.
#: Distinct from 5 (instrument drift, which can only be known afterwards) and
#: from 2 (a flag combination that is legal but produces no valid scan).
EXIT_APPLY_DESTINATION = 6


class ApplyDestinationError(RuntimeError):
    """`--apply` was asked for, and the database it would write cannot take it.

    Carries the path that was tried.  Every message this raises must name that
    path: the incident this class exists for was a lane that could not tell
    which database the tool had declined to write, because the tool never said.
    """

    def __init__(self, db_path, reason: str, fix: str = ""):
        self.db_path = db_path
        self.reason = reason
        self.fix = fix
        super().__init__(reason)

    def render(self) -> str:
        body = (
            "========================================================================\n"
            "--apply DESTINATION UNUSABLE -- NOTHING BUILT, SWEPT OR WRITTEN\n"
            "========================================================================\n"
            f"  database tried : {self.db_path}\n"
            f"  reason         : {self.reason}\n"
        )
        if self.fix:
            body += f"\n{self.fix}\n"
        body += (
            "\nThis is checked BEFORE the sweep on purpose.  A whole-binary census\n"
            "is ~6 minutes; discovering the destination afterwards costs that time\n"
            "AND leaves the stale scan it was meant to replace reading as current."
        )
        return body


def preflight_apply(args, project_dir: Path) -> int:
    """Prove `--apply` can land, before anything expensive happens.

    Returns 0 to proceed, or the exit code to return from `main()`.  Every
    branch here is decidable from the arguments and the filesystem alone -- no
    build, no report.json, no objdiff.  The one `--apply` precondition NOT
    checkable here is instrument drift (exit 5), which is a property of the
    scan's own duration and can only be observed after it.
    """
    # --- flag combinations that cannot produce a valid scan (cheap, exact) ---
    if args.negative_control:
        print("--apply refused for a negative control: its zeros are the "
              "artefact being demonstrated, not a finding.", file=sys.stderr)
        return 2
    if not args.patterns:
        print("--apply refused with --no-patterns: a scan with no detector "
              "payload would record every function as 'examined, no patterns', "
              "which is indistinguishable from a clean build.", file=sys.stderr)
        return 2
    if args.limit:
        # `callee_gate.latest_scan()` takes the highest-id scan for the ruler,
        # full stop.  A --limit run recorded here would BECOME the gate's scan,
        # and every function past the limit would read as "examined, no pattern"
        # -- i.e. certified clean by a scan that never looked at it.  The row
        # does store universe/examined, but nothing consults them.
        #
        # Refused on the FLAG, not on the measured truncation: --limit is what
        # the operator asked for, and waiting to compare it against the universe
        # only moves the refusal past the build for no gain.
        print(f"--apply refused: --limit {args.limit} was given. A partial scan "
              f"would become the latest scan for ruler={args.ruler} and its "
              f"silence about the functions it never examined would be read as "
              f"absence. Drop --limit, or drop --apply and keep --out.",
              file=sys.stderr)
        return 3

    # --- the destination ------------------------------------------------------
    try:
        check_apply_destination(args.db, project_dir, args.ruler)
    except ApplyDestinationError as e:
        print(f"\n{e.render()}\n", file=sys.stderr)
        return EXIT_APPLY_DESTINATION
    return 0


def check_apply_destination(db_arg, project_dir: Path, ruler: str) -> Path:
    """Raise `ApplyDestinationError` unless a scan row could be written now.

    Deliberately goes all the way to a WRITE PROBE.  Every weaker check has a
    false green: the path can exist, be named `decomp.db`, hold a valid SQLite
    header and a v17 schema, and still be on a read-only mount or locked open by
    another process.  `BEGIN IMMEDIATE` is the only statement that answers the
    question actually being asked -- "may I write to this, right now" -- and it
    costs a millisecond.
    """
    if not db_arg:
        raise ApplyDestinationError(
            "<not given>",
            "--apply needs --db, and none was given",
            "Pass the MAIN checkout's database explicitly:\n"
            f"  --db {suggested_db()}\n"
            "A worktree has a deliberate tripwire file at `decomp.db`, so the\n"
            "default cannot be inferred from the working directory.")

    db_path = Path(db_arg).resolve()

    # 1. The worktree shadow/tripwire, by the project's own rule.  `write_scan`
    #    reaches sqlite3.connect() directly and so has never been covered by
    #    `get_connection`'s guard; this is where that guard gets applied.
    try:
        db_mod = _database_module()
        db_mod.check_not_shadow_db(db_path)
    except Exception as e:  # noqa: BLE001 -- ShadowDatabaseError, imported lazily
        if type(e).__name__ != "ShadowDatabaseError":
            raise
        raise ApplyDestinationError(
            db_path, "it is a worktree-local decomp.db (shadow/tripwire)",
            str(e)) from e

    # 2. Exists, and is a database.  A census must never CREATE its destination:
    #    a fresh empty file would migrate cleanly and become the latest scan for
    #    its ruler in a database that has none of the rows it references.
    if not db_path.exists():
        raise ApplyDestinationError(
            db_path, "no such file",
            "A census writes into an existing decomp.db; it must not create "
            "one.\nA new file would migrate cleanly and become the latest scan "
            "for\nruler=%s in a database holding none of the functions it "
            "references." % ruler)
    try:
        head = db_path.open("rb").read(16)
    except OSError as e:
        raise ApplyDestinationError(db_path, f"cannot be read: {e}") from e
    if not head.startswith(b"SQLite format 3"):
        raise ApplyDestinationError(
            db_path,
            "not a SQLite database (this is what the worktree tripwire looks "
            "like)",
            f"`cat {db_path}` -- if it explains itself, it is the tripwire\n"
            f"scripts/setup_worktree.sh plants. Pass the main checkout's DB:\n"
            f"  --db {suggested_db()}")

    # 3. The scan must describe the tree that owns the database it lands in.
    #    `decomp.db` is a per-checkout artefact and a worktree is transient, so
    #    a row written from one outlives both the directory it names and the
    #    branch it was taken on, becomes the LATEST scan for its ruler, and
    #    reads green from main.  Four of this DB's eleven scans are of that
    #    shape (ids 1, 5, 6, 9) and three of those directories are already gone.
    #    `callee_gate.check_scan_tree` refuses to READ such a row; refusing to
    #    WRITE it is the same rule one step earlier, where the fix is cheap.
    owner = db_path.parent
    if project_dir.resolve() != owner:
        raise ApplyDestinationError(
            db_path,
            f"it belongs to {owner}, but this scan would describe {project_dir}",
            f"A scan recorded against another tree (typically a worktree) "
            f"becomes\nthe latest scan for ruler={ruler} and cannot be "
            f"re-attached to the tree\nit measured.  Run the census from "
            f"{owner}, or keep this run's output\nwith --out and do not stamp "
            f"it.")

    # 4. Writable, right now.  Also proves the schema is one we can write into.
    try:
        conn = sqlite3.connect(str(db_path), timeout=10.0)
    except sqlite3.Error as e:
        raise ApplyDestinationError(db_path, f"cannot be opened: {e}") from e
    try:
        try:
            conn.execute("SELECT version FROM schema_version").fetchone()
            conn.execute("BEGIN IMMEDIATE")
            conn.execute("ROLLBACK")
        except sqlite3.Error as e:
            # A read-only decomp.db fails on the SELECT, not on BEGIN: this
            # database is in WAL mode, and WAL needs to write `-shm`/`-wal` even
            # to read.  Classifying that as "no readable schema_version" would
            # send the reader looking for a corrupt file instead of a chmod, so
            # the permission case is named by what it is.
            if _is_permission_error(e):
                raise ApplyDestinationError(
                    db_path, f"is not writable: {e}",
                    "Check the file and its directory's permissions (WAL mode "
                    "needs to\ncreate -wal/-shm beside it, so the DIRECTORY "
                    "must be writable too), and\nwhether another process holds "
                    "a write lock on it.") from e
            raise ApplyDestinationError(
                db_path, f"has no readable schema_version: {e}",
                "This does not look like a decomp.db.") from e
    finally:
        conn.close()
    return db_path


def _is_permission_error(e: sqlite3.Error) -> bool:
    """True for sqlite's several spellings of 'you may not write here'."""
    m = str(e).lower()
    return ("readonly" in m or "read-only" in m or "permission" in m
            or "unable to open database file" in m or "attempt to write" in m)


def _database_module():
    """`orchestrator.database`, imported the way `write_scan` imports it."""
    sys.path.insert(0, str(REPO_ROOT / "scripts"))
    from orchestrator import database as db_mod
    return db_mod


def suggested_db() -> Path:
    """The database a `--db` suggestion should actually name.

    `REPO_ROOT` is derived from this file's own path, so inside a worktree it is
    the WORKTREE -- and `<worktree>/decomp.db` is the tripwire.  A help string
    that points there sends the reader at the very file that just refused them.
    `shadow_target()` walks the linked-worktree `.git` file back to the main
    checkout, which is the only path worth printing.
    """
    here = REPO_ROOT / "decomp.db"
    try:
        real = _database_module().shadow_target(here)
    except Exception:  # noqa: BLE001 -- a suggestion must never raise
        real = None
    return real or here


class InstrumentChangedError(RuntimeError):
    """`bin/objdiff-cli` was replaced while the census was running."""


def render_instrument_change(before: str, after: str, ruler: str) -> str:
    return (
        "========================================================================\n"
        "OBJDIFF BINARY WAS REPLACED MID-SCAN -- NOTHING WRITTEN\n"
        "========================================================================\n"
        f"  before scan : {before}\n"
        f"  after  scan : {after}\n"
        f"  ruler       : {ruler}\n"
        "\n"
        "`bin/objdiff-cli` is a symlink into ../objdiff/target/release, which is\n"
        "shared with ../rb3 and ../rb3-xenon and rebuilt by hand.  Some part of\n"
        "this scan was measured by one binary and some by another, and there is\n"
        "no way afterwards to say which rows came from which.\n"
        "\n"
        "This is NOT a failure to measure -- it is a refusal to ATTRIBUTE.  A\n"
        "pattern_scans row carries `tool_version`, and callee_gate's\n"
        "ensure_current_scan() trusts it to decide whether a stored finding may\n"
        "still be certified.  A row stamped with a version that did not take the\n"
        "measurement would defeat that check while looking perfectly current.\n"
        "\n"
        "Re-run once the binary is settled.  `objdiff/scripts/install-versioned.sh`\n"
        "installs an IMMUTABLE version-named copy precisely so a measurement can\n"
        "be pinned to a binary that cannot change underneath it."
    )


def read_instrument(project_dir: Path, refresh: bool) -> str:
    """`objdiff-cli --version` verbatim, including the xxh3 build fingerprint.

    `refresh=True` is mandatory on the closing read: symbol_sweep caches the
    version per project, so a cached second read cannot disagree with the first
    and the guard would be a tautology -- a check that cannot fail.
    """
    return symbol_sweep.objdiff_version(project_dir, refresh=refresh)


def report_universe(project_dir: Path) -> dict[str, dict]:
    """Every function `report.json` scores, with its canonical percentage.

    This -- not `WHERE excluded = 0` -- is the honest universe for a census.
    `excluded` is a WORKLIST predicate (XDK/SDK library code, linker glue, ICF
    placeholders absent from the report); it says nothing about whether objdiff
    can measure the function, and 16,922 excluded rows are scored in
    `report.json` right now.

    `match_percent_normalized` is read from here and ONLY from here: objdiff's
    `--batch` JSONL carries `"match_percent_normalized": null` on every row, and
    a previous triage that read it from the batch classified all 1,310 functions
    as norm=0 and reported the prize slice as empty.
    """
    path = project_dir / "build" / "373307D9" / "report.json"
    doc = json.loads(path.read_text())
    out: dict[str, dict] = {}
    dup = 0

    def rec(node, unit=None):
        nonlocal dup
        u = node.get("name") or unit
        for f in node.get("functions") or []:
            name = f["name"]
            if name in out:
                dup += 1
                continue
            out[name] = {
                "unit": u,
                # report.json serialises `size` as a STRING. Multiplying it by
                # a float raises; comparing it to an int silently compares wrong.
                "size": int(f.get("size") or 0),
                "norm": f.get("match_percent_normalized"),
                "fuzzy": f.get("fuzzy_match_percent"),
            }
        for c in node.get("units") or []:
            rec(c, u)

    rec(doc)
    prov = doc.get("provenance") or {}
    return {
        "functions": out,
        "duplicate_names": dup,
        "tool_version": prov.get("tool_version"),
        "tool_commit": prov.get("tool_commit"),
        "diff_config": prov.get("diff_config"),
    }


def git_rev(project_dir: Path) -> str | None:
    try:
        p = subprocess.run(["git", "-C", str(project_dir), "rev-parse", "--short", "HEAD"],
                           capture_output=True, text=True, timeout=30)
        return p.stdout.strip() or None
    except Exception:  # noqa: BLE001
        return None


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--project-dir", default=str(REPO_ROOT))
    ap.add_argument("--ruler", default="name_check",
                    choices=[r for r in sorted(symbol_sweep.RELOC_RULERS)],
                    help="functionRelocDiffs. `none` is accepted by the parser "
                         "only so the refusal explains itself.")
    ap.add_argument("--db", default=None,
                    help="path to the REAL decomp.db. A worktree has a tripwire "
                         "file there on purpose; pass the main checkout's path.")
    ap.add_argument("--apply", action="store_true", help="write the scan to --db")
    ap.add_argument("--out", default=None, help="write per-function JSONL here")
    ap.add_argument("-j", "--jobs", type=int, default=8)
    ap.add_argument("--limit", type=int, default=None)
    ap.add_argument("--skip-patch-check", action="store_true",
                    help="do not build/verify the tree first. The scan is then "
                         "about a moment, and is recorded with tree_verified=0.")
    ap.add_argument("--notes", default=None)
    ap.add_argument("--negative-control", action="store_true",
                    help="run the pattern detectors under the BLIND ruler on "
                         "purpose, to measure which of them it starves. The "
                         "output is labelled a negative control and --apply is "
                         "refused. This is how the guard stays a fact.")
    ap.add_argument("--no-patterns", dest="patterns", action="store_false",
                    help="percentages only, no detector payload. This is the "
                         "ONLY legitimate use of --ruler none here: the blind "
                         "ruler's PERCENTAGE is meaningful (it forgives every "
                         "relocation name, so the gap to the graded score is "
                         "exactly what the names cost), while its PATTERNS are "
                         "starved. See pattern_worklist.py.")
    args = ap.parse_args()

    project_dir = Path(args.project_dir).resolve()

    # --- BEFORE ANYTHING EXPENSIVE -------------------------------------------
    # No build, no report.json, no objdiff.  If --apply cannot land, say so now:
    # the alternative, measured, is ~6 minutes of sweep followed by a refusal
    # that arrives after 40 lines of buffered stdout and gets read as success.
    if args.apply:
        rc = preflight_apply(args, project_dir)
        if rc:
            return rc

    started = datetime.now(timezone.utc).isoformat()

    tree_verified = 0
    if args.skip_patch_check:
        print("WARNING: --skip-patch-check. The objects behind this census may "
              "be raw compiler output: unpatched anon-namespace hashes, atexit "
              "scope counters and static guards all read as relocation-NAME "
              "divergences, which is exactly what these detectors count.",
              file=sys.stderr)
    else:
        try:
            note = ensure_patched_tree(project_dir, build=True)
            tree_verified = 1
            print(f"tree: {note}")
        except UnpatchedTreeError as e:
            print(f"\n{e}\n", file=sys.stderr)
            return 4

    uni = report_universe(project_dir)
    fns = uni["functions"]
    symbols = sorted(fns)
    universe = len(symbols)
    truncated = bool(args.limit) and args.limit < universe
    if args.limit:
        symbols = symbols[:args.limit]

    print(f"{'TRUNCATED: ' if truncated else ''}{len(symbols)} of {universe} "
          f"report.json functions ({uni['duplicate_names']} duplicate names "
          f"collapsed), functionRelocDiffs={args.ruler}")

    # --- bracket the sweep: the instrument must be the same one afterwards ---
    instrument_before = read_instrument(project_dir, refresh=True)
    print(f"instrument (pre-scan) : {instrument_before}")

    # --- and bracket it a second way: the OBJECTS must hold still too ---
    # Same argument as the instrument bracket, applied to the other half of the
    # measurement.  A whole-binary census is minutes long and several lanes
    # build at once, so some unit's target or base object can be rewritten
    # mid-sweep; the resulting rows are real findings whose baseline nothing can
    # attribute.  Those units are recorded `raced = 1` rather than dropped or
    # silently stamped -- the same refusal-to-attribute the `--version` bracket
    # makes, at the grain where it is affordable.
    units_cfg = object_baseline.unit_paths(project_dir)
    objects_before = object_baseline.fingerprint_units(project_dir, units_cfg)
    print(f"object baseline (pre-scan) : {len(objects_before)} unit pairs "
          f"content-hashed")

    try:
        res = symbol_sweep.sweep_functions(
            project_dir, symbols, include_patterns=args.patterns,
            reloc_config=args.ruler, jobs=args.jobs, timeout=7200,
            allow_blind_patterns=args.negative_control,
            scanner_name=f"pattern_census.{args.ruler}")
    except symbol_sweep.RelocBlindPatternError as e:
        print(f"\n{e}\n", file=sys.stderr)
        return 4

    objects_after = object_baseline.fingerprint_units(project_dir, units_cfg)
    raced = object_baseline.race_units(objects_before, objects_after)
    if raced:
        print(f"object baseline (post-scan): {len(raced)} of {len(objects_before)} "
              f"unit(s) MOVED DURING THE SCAN and are recorded raced=1 -- their "
              f"findings are real but their baseline is not attributable: "
              f"{', '.join(sorted(raced)[:10])}"
              f"{' ...' if len(raced) > 10 else ''}", file=sys.stderr)
    else:
        print(f"object baseline (post-scan): unchanged across the scan "
              f"({len(objects_before)} unit pairs)")

    instrument_after = read_instrument(project_dir, refresh=True)
    instrument_changed = instrument_before != instrument_after
    if instrument_changed:
        print(f"\n{render_instrument_change(instrument_before, instrument_after, args.ruler)}\n",
              file=sys.stderr)
    else:
        print(f"instrument (post-scan): {instrument_after}  [unchanged across scan]")

    print()
    print(res["_coverage_render"])

    # --- populations, by DISTINCT FUNCTION, with the denominator -------------
    examined = res["_coverage"]["examined"]
    by_pattern: dict[str, list[str]] = collections.defaultdict(list)
    rows_out = []
    for r in res["rows"]:
        sym = r["symbol"]
        pats = (r.get("analysis") or {}).get("patterns") or []
        meta = fns.get(sym, {})
        norm = meta.get("norm")
        canon = []
        for p in pats:
            name = symbol_sweep.canonical_pattern_name(p.get("pattern", ""))
            by_pattern[name].append(sym)
            canon.append({
                "pattern": name,
                "confidence": p.get("confidence"),
                "fixability": p.get("fixability"),
                "instruction_count": p.get("instruction_count"),
                "details": p.get("details"),
            })
        rows_out.append({
            "symbol": sym,
            "unit": meta.get("unit") or r.get("unit"),
            "demangled": r.get("demangled"),
            "size": int(meta.get("size") or r.get("target_size") or 0),
            # From report.json. NEVER from the batch row -- it is null there.
            "norm": norm,
            "fuzzy": r.get("fuzzy_match_percent"),
            "canonical": r.get("canonical_match_percent"),
            "patterns": canon,
        })

    print()
    if args.negative_control:
        print("### NEGATIVE CONTROL: deliberately run under the blind ruler. "
              "Every count below is what a starved detector reports, NOT a "
              "property of the build. Compare against the name_check census.")
    print(f"# Pattern populations -- functionRelocDiffs={args.ruler}, "
          f"{res['objdiff_version']}")
    print(f"# denominator: {examined} functions examined of {universe} in "
          f"report.json ({universe - examined} dropped, see COVERAGE above)")
    print(f"{'pattern':38s} {'functions':>9s} {'% of examined':>14s}")
    for name, syms in sorted(by_pattern.items(), key=lambda kv: -len(set(kv[1]))):
        n = len(set(syms))
        print(f"{name:38s} {n:9d} {100.0 * n / max(examined, 1):13.3f}%")
    silent = sorted(set(res.get("patterns_checked") or []) - set(by_pattern))
    if silent:
        print(f"\n{len(silent)} of {len(res.get('patterns_checked') or [])} "
              f"checked patterns fired on ZERO functions under this ruler "
              f"(measured zero, not an unmeasured one): {', '.join(silent)}")

    if args.out:
        with open(args.out, "w") as fh:
            fh.write(json.dumps({
                "_scan": {
                    "ruler": args.ruler,
                    "tool_version": res["objdiff_version"],
                    "instrument_before": instrument_before,
                    "instrument_after": instrument_after,
                    "instrument_changed": instrument_changed,
                    "project_dir": str(project_dir),
                    "build_rev": git_rev(project_dir),
                    "tree_verified": tree_verified,
                    "object_units": len(objects_before),
                    "object_units_raced": sorted(raced),
                    "tree_verified_note": (
                        "object_units is the number of objdiff.json unit pairs "
                        "whose target AND base objects were content-hashed "
                        "before the sweep; object_units_raced moved before the "
                        "sweep finished"),
                    "jobs": args.jobs,
                    "universe": universe,
                    "examined": examined,
                    "truncated": truncated,
                    "coverage": res["_coverage"],
                    "patterns_checked": res.get("patterns_checked"),
                    "report_provenance": {k: uni[k] for k in
                                          ("tool_version", "tool_commit", "diff_config")},
                    "started_at": started,
                }}) + "\n")
            for r in rows_out:
                fh.write(json.dumps(r) + "\n")
        print(f"\nwrote {len(rows_out)} rows -> {args.out}")

    if instrument_changed:
        if args.apply:
            print("--apply REFUSED: the objdiff binary changed identity during "
                  "this scan (see above). The measurements are not attributable "
                  "to any one instrument, so they must not be stamped as if they "
                  "were.", file=sys.stderr)
        return 5
    if args.apply:
        # Every OTHER --apply precondition was settled by `preflight_apply()`
        # before this sweep started; the destination was opened and write-probed
        # there.  The only refusal that HAS to live down here is the one above,
        # because it is a fact about the scan's own duration.  Re-checking the
        # destination now would be checking a different moment, not a stronger
        # one -- if it went away during the sweep, the write raises, loudly.
        write_scan(Path(args.db), args, res, rows_out, uni, universe, examined,
                   tree_verified, project_dir, started,
                   objects_before, raced)

    return 3 if truncated else 0


def write_scan(db_path, args, res, rows_out, uni, universe, examined,
               tree_verified, project_dir, started,
               objects_before=None, raced=frozenset()) -> None:
    """Record the scan. Every finding is a row; the ruler is on the scan."""
    db_mod = _database_module()

    conn = sqlite3.connect(str(db_path))
    ver = conn.execute("SELECT version FROM schema_version").fetchone()[0]
    if ver < db_mod.SCHEMA_VERSION:
        conn.close()
        # Route through the project's own migrator rather than issuing DDL here:
        # two places that both know the schema is how they drift.
        db_mod.init_database(str(db_path))
        conn = sqlite3.connect(str(db_path))
    # Not routed through the version ladder: two lanes were extending this
    # schema in the same week, and a database already migrated by the OTHER
    # lane's v18 would never see this table's DDL.  `ensure_pattern_scan_units`
    # is idempotent and version-independent for exactly that reason.
    db_mod.ensure_pattern_scan_units(conn)

    ids = {r[0]: r[1] for r in conn.execute("SELECT symbol, id FROM functions")}

    # `jobs` is on the row because a scan's DURATION is otherwise uninterpretable
    # (schema v18).  Two runs of the same binary over the same tree came back
    # 15 s and ~6 min apart and the starvation hypothesis could not be tested,
    # because nothing recorded how the sweep had been sharded.
    cur = conn.execute(
        "INSERT INTO pattern_scans (ruler, tool_version, project_dir, build_rev,"
        " tree_verified, universe, examined, coverage_json, patterns_checked,"
        " notes, started_at, jobs) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)",
        (args.ruler, res["objdiff_version"], str(project_dir), git_rev(project_dir),
         tree_verified, universe, examined,
         json.dumps(res["_coverage"]), json.dumps(res.get("patterns_checked") or []),
         args.notes, started, args.jobs))
    scan_id = cur.lastrowid

    examined_rows, pattern_rows, unmatched = [], [], 0
    for r in rows_out:
        fid = ids.get(r["symbol"])
        if fid is None:
            unmatched += 1
            continue
        examined_rows.append((scan_id, fid))
        for p in r["patterns"]:
            pattern_rows.append((
                scan_id, fid, p["pattern"], p.get("confidence"),
                p.get("fixability"), p.get("instruction_count"),
                json.dumps(p.get("details")) if p.get("details") is not None else None))

    conn.executemany("INSERT OR REPLACE INTO pattern_scan_examined VALUES (?,?)",
                     examined_rows)
    conn.executemany("INSERT OR REPLACE INTO function_patterns VALUES (?,?,?,?,?,?,?)",
                     pattern_rows)

    # The object baselines, per unit, taken from the SAME objects this scan
    # diffed -- hashed immediately before the sweep, re-hashed immediately
    # after, and any unit that moved between the two reads is marked `raced`
    # rather than being stamped as if it had held still.  Without this the scan
    # records WHICH objdiff took the measurement and nothing about WHAT it
    # measured, and every consumer then reads findings about objects that no
    # longer exist as current facts.
    unit_rows = [(scan_id, unit, h.get("target"), h.get("base"),
                  1 if unit in raced else 0)
                 for unit, h in sorted((objects_before or {}).items())]
    conn.executemany("INSERT OR REPLACE INTO pattern_scan_units "
                     "(scan_id, unit, target_sha256, base_sha256, raced) "
                     "VALUES (?,?,?,?,?)", unit_rows)
    conn.commit()
    print(f"\nrecorded scan id={scan_id} ruler={args.ruler} jobs={args.jobs}: "
          f"{len(examined_rows)} examined rows, {len(pattern_rows)} pattern rows"
          + (f", {unmatched} symbols had no decomp.db row" if unmatched else ""))
    if unit_rows:
        print(f"  object baseline: {len(unit_rows)} unit pairs recorded"
              + (f", {len(raced)} raced" if raced else ""))
    else:
        # Loud, because a scan with no baseline is refused on read
        # (`UnfingerprintedPatternScanError`) and the fix is here, not there.
        print("  WARNING: NO object baseline recorded for this scan -- "
              "callee_gate will refuse to read it.", file=sys.stderr)

    if args.ruler == "name_check":
        refresh_legacy_flags(conn, scan_id)
    else:
        print(f"  legacy has_* columns NOT refreshed: they are defined against "
              f"the graded ruler, and this scan is `{args.ruler}`.")
    conn.close()


#: The reloc-sensitive `has_*` columns and the pattern each one means.  These are
#: kept -- not extended -- deliberately.  Existing queries across this repo read
#: them, and leaving them holding a 2026-08-19 number measured under a VOCABULARY
#: THAT NO LONGER EXISTS is worse than either updating them or dropping them.
#: They are refreshed here, from the graded ruler, and stamped with the scan id
#: so a reader can check provenance.  The four NEW 4.2.6 classes get no column:
#: `function_patterns` carries them with their payload, which a boolean cannot.
LEGACY_FLAGS = {
    "has_linker_merged": "LINKER_MERGED",
    "has_makestring_mismatch": "MAKESTRING_TEMPLATE_MISMATCH",
    "has_scope_counter_mismatch": "SCOPE_COUNTER_MISMATCH",
    "has_alloca_mismatch": "ALLOCA_MISMATCH",
    "has_dynamic_cast_mismatch": "DYNAMIC_CAST_MISMATCH",
    "has_address_relocation": "ADDRESS_RELOCATION_NOISE",
    "has_anonymous_namespace_hash": "ANONYMOUS_NAMESPACE_HASH",
    "has_static_guard_counter": "STATIC_GUARD_COUNTER",
    # has_prologue_mismatch is NOT here -- see RETIRED_FLAGS.
}

#: RETIRED.  Measured whole-binary 2026-08-21 under BOTH name_check and all:
#: PROLOGUE_MISMATCH and REGISTER_SAVE_HELPER_MISMATCH select the IDENTICAL 219
#: functions -- set equality, zero rows on either side only, under both rulers.
#: That is structural, not coincidental: detect_prologue_mismatch fires on a
#: `__savegprlr_N`/`__savefpr_N` argument disagreement inside the first ten
#: instructions, and every such site is also a register-save-helper callee
#: divergence.  Two columns, one predicate.  `has_prologue_mismatch` keeps its
#: name (it is the one with a documented meaning in
#: docs/decomp/patterns/fixable-liveness.md and a live consumer in
#: sync_objdiff's PRACTICALLY_UNFIXABLE set) and REGISTER_SAVE_HELPER_MISMATCH
#: is left to `function_patterns`, where it costs nothing to carry.
RETIRED_FLAGS = {"has_prologue_mismatch": "PROLOGUE_MISMATCH"}


def refresh_legacy_flags(conn, scan_id: int) -> None:
    """Re-derive the reloc-sensitive booleans from THIS scan, and stamp them.

    Every column is set on every examined row -- 1 where the pattern fired, 0
    where it did not -- so a 0 written here means "the graded ruler looked and
    it is not there".  Rows the scan did not examine are left ALONE rather than
    zeroed: overwriting an unmeasured row with a confident 0 is the entire
    failure this exercise is about.
    """
    cols = {**LEGACY_FLAGS, **RETIRED_FLAGS}
    print("\n  refreshing legacy has_* flags from this scan:")
    for col, pattern in cols.items():
        before = conn.execute(
            f"SELECT COUNT(*) FROM functions WHERE {col} = 1").fetchone()[0]
        conn.execute(f"""
            UPDATE functions SET {col} = (
                SELECT CASE WHEN EXISTS (
                    SELECT 1 FROM function_patterns fp
                    WHERE fp.scan_id = ? AND fp.function_id = functions.id
                      AND fp.pattern = ?) THEN 1 ELSE 0 END)
            WHERE id IN (SELECT function_id FROM pattern_scan_examined
                         WHERE scan_id = ?)""", (scan_id, pattern, scan_id))
        after = conn.execute(
            f"SELECT COUNT(*) FROM functions WHERE {col} = 1").fetchone()[0]
        tag = "  (RETIRED: co-extensive with REGISTER_SAVE_HELPER_MISMATCH)" \
            if col in RETIRED_FLAGS else ""
        print(f"    {col:34s} {before:6d} -> {after:6d}{tag}")
    conn.execute(
        "UPDATE functions SET pattern_flags_scan_id = ? "
        "WHERE id IN (SELECT function_id FROM pattern_scan_examined "
        "WHERE scan_id = ?)", (scan_id, scan_id))
    unstamped = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE pattern_flags_scan_id IS NULL"
    ).fetchone()[0]
    conn.commit()
    print(f"    stamped pattern_flags_scan_id = {scan_id}; {unstamped} rows "
          f"remain NULL (never examined by any scan -- their has_* values are "
          f"of unknown provenance and must not be read as measurements)")


if __name__ == "__main__":
    sys.exit(main())
