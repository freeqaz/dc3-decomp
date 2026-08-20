#!/usr/bin/env python3
"""Run objdiff on every function and sync results to decomp.db.

Unlike sync_match_percent.py (which reads report.json for just match%),
this script runs `objdiff-cli diff --batch` to get analysis results for
all functions efficiently (grouping by unit to avoid redundant object
file loading), then updates enrichment columns:

  - current_percent, best_percent, size, demangled
  - has_bool_mask, primary_pattern, reachable_100
    (NOT has_linker_merged -- see _run_single_batch: this pass runs with
     functionRelocDiffs=none, which masks the relocation diffs that
     detector needs. scripts/backfill_reloc_patterns.py owns it.)
  - pattern detection columns from Rust analysis engine
  - verdict (COMPLETE for 100%, optionally AT_LIMIT for flagged patterns)

Usage:
    python3 scripts/sync_objdiff.py                         # divergent only (default)
    python3 scripts/sync_objdiff.py --all                   # full scan
    python3 scripts/sync_objdiff.py --unit 'system/char/*'  # filter by unit
    python3 scripts/sync_objdiff.py --dry-run               # preview
    python3 scripts/sync_objdiff.py --skip-100              # skip already-COMPLETE

TWO WRITERS, ONE COLUMN -- read this before trusting `current_percent`
======================================================================
`functions.current_percent` is written by at least three scripts, each with a
DIFFERENT ruler, and they overwrite each other:

  sync_match_percent.py  writes report.json's `fuzzy_match_percent`, which in
                         THAT file is the RAW, relocation-sensitive scorer, and
                         separately maintains `match_percent_normalized`.  Its
                         --promote/--demote gates read the NORMALIZED column
                         (sync_match_percent.py:419).
  sync_objdiff.py (this) writes `objdiff-cli diff`'s `fuzzy_match_percent`,
                         which in THAT payload is an alias of
                         `normalized_match_percent` (objdiff-cli
                         diff.rs:1262).  Same key name, different ruler,
                         different file.  Its --promote/demote gates read that.
  batch_check.py         used to write `instruction_summary.equal_percent`, a
                         third ruler entirely (fixed 2026-08-19).

Concretely, in the DB as of 2026-08-19: 374 rows carry verdict=COMPLETE with
`current_percent` in [96.46, 100) and `match_percent_normalized` >= 100.  Run
this script with the default promotion/demotion behaviour and its demotion arm
(`old_verdict == 'COMPLETE' and match_percent < 100`) targets exactly that
population, while sync_match_percent.py --promote puts it straight back.  If
you see COMPLETE verdicts flapping, this is why.

CHANGED DEFAULT (2026-08-19): `--auto-at-limit` is now OFF.  See its help.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import sqlite3
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))
from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402

OBJDIFF_CLI = REPO_ROOT / "bin" / "objdiff-cli"
DEFAULT_DB = REPO_ROOT / "decomp.db"

SDK_UNIT_PREFIXES = [
    "default/xdk/",
]

# Units and symbol patterns for functions that are expected to have base_size=0
# (compiler-generated boilerplate, third-party libs, platform stubs).
# These are reported as "Skipped" rather than "Unimplemented".
SKIP_UNITS = {
    "default/Memory_Xbox",
    "default/link_glue",
}
SKIP_UNIT_PREFIXES = [
    "default/lib/",
    "default/system/oggvorbis/",
    "default/system/net/json-c/",
    "default/system/synth_xbox/soundtouch/",
    "default/system/zlib/",
    "default/system/synth/tomcrypt/",
]
SKIP_UNITS_EXTRA = {
    "default/system/synth/filterdesign",  # C-style DSP filter design library
}


def _is_skippable_stub(symbol: str, unit: str) -> bool:
    """Return True if this base_size=0 function is expected boilerplate."""
    # Platform / third-party / glue units
    if unit in SKIP_UNITS or unit in SKIP_UNITS_EXTRA:
        return True
    for prefix in SKIP_UNIT_PREFIXES:
        if unit.startswith(prefix):
            return True
    # Compiler-generated symbols
    if symbol.startswith("??__F"):       # atexit destructors
        return True
    if symbol.startswith("??__E"):       # dynamic initializers
        return True
    if symbol.startswith("??_9"):        # vcall thunks
        return True
    if symbol.startswith("??_H"):        # vector ctor iterators
        return True
    if symbol.startswith("??_E"):        # scalar deleting destructors
        return True
    if symbol.startswith("??_G"):        # scalar deleting destructors (variant)
        return True
    # Template instantiations that the compiler emits per-TU
    if symbol.startswith("??$"):          # all template instantiations (MakeString, PropSync, Find, sort, etc.)
        return True
    if "stlpmtx_std" in symbol:          # STL internal templates
        return True
    # Methods on template classes (ObjPtrVec, ObjPtrList, ObjRefConcrete, ObjPtr, etc.)
    if "@?$Obj" in symbol or symbol.startswith("??1?$Obj"):
        return True
    # Adjustor thunks for multiple inheritance (contain $4PPPPPPPM or $2PPPPPPPM)
    if "PPPPPPPM@" in symbol:
        return True
    # CRT / std library internals
    if "exception@std@@" in symbol:
        return True
    if "?_Copy_str@" in symbol:
        return True
    # Third-party SDK symbols that land in various TUs via COMDAT
    if "NUISPEECH" in symbol:
        return True
    return False


# Itanium ABI mangled name pattern: MethodName__<N><ClassName><params>
_ITANIUM_PATTERN = re.compile(r'^(.+?)__(\d+)(\w+)')


def demangle_itanium(symbol: str) -> str | None:
    """Demangle Itanium-style name to ClassName::MethodName."""
    if symbol.startswith("?") or "::" in symbol:
        return None
    m = _ITANIUM_PATTERN.match(symbol)
    if not m:
        return None
    method, class_len_str, rest = m.group(1), m.group(2), m.group(3)
    class_len = int(class_len_str)
    if class_len > len(rest) or class_len == 0:
        return None
    class_name = rest[:class_len]
    if method == "__ct":
        method = class_name
    elif method == "__dt":
        method = f"~{class_name}"
    return f"{class_name}::{method}"


@dataclass
class FunctionResult:
    """Result of running objdiff on a single function."""
    db_id: int
    symbol: str
    match_percent: float | None = None
    size: int | None = None
    demangled: str | None = None
    has_merged: bool = False
    has_bool_mask: bool = False
    has_makestring_mismatch: bool = False
    has_address_relocation: bool = False
    has_boolean_negation: bool = False
    has_float_precision: bool = False
    has_fsel_ternary: bool = False
    has_float_to_int_to_float: bool = False
    has_register_swap: bool = False
    has_comparison_style: bool = False
    has_control_flow: bool = False
    has_commutative_op_order: bool = False
    has_offset_swap: bool = False
    has_anonymous_namespace_hash: bool = False
    has_static_guard_counter: bool = False
    has_dynamic_cast_mismatch: bool = False
    has_dead_store_elimination: bool = False
    has_prologue_mismatch: bool = False
    has_alloca_mismatch: bool = False
    has_scope_counter_mismatch: bool = False
    detected_patterns: list[str] | None = None
    primary_pattern: str | None = None
    verdict_classification: str | None = None
    unit: str | None = None
    error: str | None = None
    ruler: str | None = None      # which key `match_percent` came from


def match_percent_from_diff(data: dict) -> tuple[float | None, str]:
    """(percent, ruler) from one `objdiff-cli diff --batch` JSONL record.

    RULER.  `objdiff-cli diff` writes the canonical normalized score into BOTH
    `normalized_match_percent` and the key literally named
    `fuzzy_match_percent` (objdiff-cli diff.rs:1262-1263), and exposes the
    relocation-sensitive one separately as `raw_match_percent`.  report.json
    uses the SAME key name `fuzzy_match_percent` for the RAW score and a
    different key, `match_percent_normalized`, for the canonical one.

    So `data.get("fuzzy_match_percent")` here was already normalized -- reading
    `normalized_match_percent` first pins the ruler BY NAME instead of by
    coincidence, and survives an upstream rename.  This script also passes
    `functionRelocDiffs=none`, under which normalized == primary anyway.
    """
    n = data.get("normalized_match_percent")
    if n is not None:
        return float(n), "normalized"
    f = data.get("fuzzy_match_percent")
    if f is not None:
        return float(f), "normalized-via-fuzzy-key"
    return None, "none"


def _extract_patterns_from_analysis(result: FunctionResult, data: dict) -> None:
    """Extract detected patterns from Rust analysis output."""
    analysis = data.get("analysis")
    if not analysis:
        return

    pattern_types = {p["pattern"] for p in analysis.get("patterns", [])}
    if not pattern_types:
        return

    # objdiff spells this pattern TWO ways and we read the one it does NOT use
    # here. The JSON `patterns[].pattern` field is serde-derived from the enum
    # variant `MakeStringTemplateMismatch` under
    # `rename_all = "SCREAMING_SNAKE_CASE"`, which splits the internal capital
    # in "String" and emits MAKE_STRING_TEMPLATE_MISMATCH. `PatternType::to_str`
    # -- used for `patterns_checked` and for the human output -- returns
    # MAKESTRING_TEMPLATE_MISMATCH. The constant below was the second spelling,
    # so `"MAKESTRING_TEMPLATE_MISMATCH" in pattern_types` was never true and
    # has_makestring_mismatch could not be set on any row, ever. Accept both.
    # (Measured 2026-08-19: 7 hits in a 3,000-function sample.)
    # Canonicalise on the to_str spelling so `detected_patterns` does not end
    # up carrying the same pattern twice under two names.
    if "MAKE_STRING_TEMPLATE_MISMATCH" in pattern_types:
        pattern_types.discard("MAKE_STRING_TEMPLATE_MISMATCH")
        pattern_types.add("MAKESTRING_TEMPLATE_MISMATCH")

    # Set boolean flags from pattern types
    result.has_merged = "LINKER_MERGED" in pattern_types
    result.has_bool_mask = "BOOL_MASK" in pattern_types
    result.has_makestring_mismatch = "MAKESTRING_TEMPLATE_MISMATCH" in pattern_types
    result.has_address_relocation = "ADDRESS_RELOCATION_NOISE" in pattern_types
    result.has_boolean_negation = "BOOLEAN_NEGATION" in pattern_types
    result.has_float_precision = "FLOAT_PRECISION_MISMATCH" in pattern_types
    result.has_fsel_ternary = "FSEL_TERNARY" in pattern_types
    result.has_float_to_int_to_float = "FLOAT_TO_INT_TO_FLOAT" in pattern_types
    result.has_register_swap = "REGISTER_SWAP" in pattern_types
    result.has_comparison_style = "COMPARISON_STYLE" in pattern_types
    result.has_control_flow = "CONTROL_FLOW" in pattern_types
    result.has_commutative_op_order = "COMMUTATIVE_OP_ORDER" in pattern_types
    result.has_offset_swap = "OFFSET_SWAP" in pattern_types
    result.has_anonymous_namespace_hash = "ANONYMOUS_NAMESPACE_HASH" in pattern_types
    result.has_static_guard_counter = "STATIC_GUARD_COUNTER" in pattern_types
    result.has_dynamic_cast_mismatch = "DYNAMIC_CAST_MISMATCH" in pattern_types
    result.has_dead_store_elimination = "DEAD_STORE_ELIMINATION" in pattern_types
    result.has_prologue_mismatch = "PROLOGUE_MISMATCH" in pattern_types
    result.has_alloca_mismatch = "ALLOCA_MISMATCH" in pattern_types
    result.has_scope_counter_mismatch = "SCOPE_COUNTER_MISMATCH" in pattern_types

    # Store all detected patterns as sorted list
    result.detected_patterns = sorted(pattern_types)

    # Primary pattern: first match in priority order (most impactful first)
    priority = [
        "LINKER_MERGED",
        "ANONYMOUS_NAMESPACE_HASH",
        "ADDRESS_RELOCATION_NOISE",
        "BOOLEAN_NEGATION",
        "BOOL_MASK",
        "MAKESTRING_TEMPLATE_MISMATCH",
        "FLOAT_PRECISION_MISMATCH",
        "FSEL_TERNARY",
        "FLOAT_TO_INT_TO_FLOAT",
        "DYNAMIC_CAST_MISMATCH",
        "DEAD_STORE_ELIMINATION",
        "ALLOCA_MISMATCH",
        "PROLOGUE_MISMATCH",
        "SCOPE_COUNTER_MISMATCH",
        "STATIC_GUARD_COUNTER",
        "COMPARISON_STYLE",
        "CONTROL_FLOW",
        "COMMUTATIVE_OP_ORDER",
        "OFFSET_SWAP",
        "REGISTER_SWAP",
    ]
    for p in priority:
        if p in pattern_types:
            result.primary_pattern = p
            break


def _run_single_batch(functions: list[tuple[int, str]], project_dir: str,
                      reloc_config: str = "none",
                      ) -> tuple[list[FunctionResult], dict[str, int]]:
    """Run a single objdiff-cli --batch process for a chunk of functions.

    Top-level function for ProcessPoolExecutor pickling.

    Returns `(results, line_stats)`.  The counters travel back in the RETURN
    VALUE rather than being accumulated in the worker, because a CoverageReport
    is main-thread-only by contract (its docstring: counting inside a pool is
    the data_symbol_scan race shape).  `line_stats` counts the JSONL lines this
    worker discarded -- previously two bare `continue`s that nothing reported.

    `reloc_config` is the `functionRelocDiffs` value. The default `none` is the
    project's canonical ruler -- but it MASKS relocation differences, and some
    pattern detectors read exactly those. `detect_linker_merged` only inspects
    `bl`/`b` instructions whose match_type is `diff_arg`, i.e. calls whose
    relocation targets differ; under `none` those instructions are reported as
    equal and the detector is structurally starved. Measured 2026-08-19 on
    ?Handle@StorePanel@@UAA?AVDataNode@@PAVDataArray@@_N@Z:

        -c functionRelocDiffs=none       patterns = []
        -c functionRelocDiffs=name_only  patterns = [ADDRESS_RELOCATION_NOISE]
        -c functionRelocDiffs=all        patterns = [LINKER_MERGED,
                                                     ADDRESS_RELOCATION_NOISE]

    That -- not a missing detector -- is why has_linker_merged read 0 on all
    52,547 rows while `verdict_reason` on 708 of them says LINKER_MERGED.
    See scripts/backfill_reloc_patterns.py.
    """
    # Build lookup map: lookup_name -> (db_id, original_symbol)
    lookup_to_info: dict[str, tuple[int, str]] = {}
    lookup_names: list[str] = []
    for db_id, symbol in functions:
        lookup = demangle_itanium(symbol) or symbol
        lookup_to_info[lookup] = (db_id, symbol)
        lookup_names.append(lookup)

    stdin_data = "\n".join(lookup_names) + "\n"

    line_stats: dict[str, int] = {
        "stdout_lines": 0,
        "blank_lines": 0,
        "malformed_json_lines": 0,
        "unrequested_symbol_lines": 0,
        "parsed_records": 0,
    }

    try:
        proc = subprocess.run(
            [str(OBJDIFF_CLI), "diff", "-p", project_dir,
             "-c", f"functionRelocDiffs={reloc_config}", "--batch"],
            input=stdin_data, capture_output=True, text=True,
            timeout=600,
        )
    except subprocess.TimeoutExpired:
        return ([FunctionResult(db_id=db_id, symbol=sym, error="timeout")
                 for db_id, sym in functions], line_stats)
    except Exception as e:
        return ([FunctionResult(db_id=db_id, symbol=sym, error=str(e))
                 for db_id, sym in functions], line_stats)

    # Parse JSONL output
    results: list[FunctionResult] = []
    seen_lookups: set[str] = set()

    for line in proc.stdout.splitlines():
        line_stats["stdout_lines"] += 1
        line = line.strip()
        if not line:
            line_stats["blank_lines"] += 1
            continue
        try:
            data = json.loads(line)
        except json.JSONDecodeError:
            # A line objdiff emitted that is not JSON.  Previously a bare
            # `continue`: an objdiff-side format change would have silently
            # shrunk every count this script produced.
            line_stats["malformed_json_lines"] += 1
            continue

        symbol_name = data.get("symbol", "")
        seen_lookups.add(symbol_name)

        info = lookup_to_info.get(symbol_name)
        if not info:
            # objdiff answered about a symbol we did not ask for (e.g. a
            # resolved alias).  Also previously a bare `continue`.
            line_stats["unrequested_symbol_lines"] += 1
            continue
        line_stats["parsed_records"] += 1
        db_id, original_symbol = info

        result = FunctionResult(db_id=db_id, symbol=original_symbol)
        result.unit = data.get("unit", "")

        if "error" in data:
            result.error = data["error"]
            results.append(result)
            continue

        result.match_percent, result.ruler = match_percent_from_diff(data)
        result.size = data.get("target_size") or data.get("base_size")
        result.demangled = data.get("demangled")

        if data.get("base_size", 0) == 0:
            if _is_skippable_stub(original_symbol, result.unit):
                result.error = "skipped"
            else:
                result.error = "unimplemented"
            results.append(result)
            continue

        _extract_patterns_from_analysis(result, data)
        # Extract verdict classification from objdiff analysis
        verdict = data.get("verdict", {})
        result.verdict_classification = verdict.get("classification")
        results.append(result)

    # Mark missing symbols as not_found (sorted: dict order is insertion order,
    # but sorting makes the tail of the result list order-independent)
    for lookup in sorted(lookup_to_info):
        db_id, original_symbol = lookup_to_info[lookup]
        if lookup not in seen_lookups:
            results.append(FunctionResult(
                db_id=db_id, symbol=original_symbol, error="not_found"))

    return results, line_stats


def run_batch(functions: list[tuple[int, str]], project_dir: str,
              jobs: int = 4, verbose: bool = False,
              reloc_config: str = "none",
              ) -> tuple[list[FunctionResult], dict[str, int]]:
    """Run objdiff-cli in batch mode, splitting across parallel workers.

    Each worker gets a chunk of symbols and runs its own --batch process.
    Within each process, objdiff groups symbols by unit for efficient loading.

    DETERMINISM: futures are consumed with `as_completed`, so results arrive in
    whatever order the workers happen to finish.  They are reassembled in CHUNK
    INDEX order before returning, and the final list is sorted by (db_id,
    symbol), so two runs over the same inputs produce identical output.
    """
    if not functions:
        return [], {}

    # Split into chunks for parallel processing
    chunk_size = max(1, math.ceil(len(functions) / jobs))
    chunks = [functions[i:i + chunk_size] for i in range(0, len(functions), chunk_size)]
    actual_workers = len(chunks)

    def _emit_verbose(rs):
        for r in sorted(rs, key=lambda x: (x.symbol, x.db_id)):
            if r.error:
                print(f"  SKIP {r.symbol}: {r.error}")
            else:
                pats = ",".join(r.detected_patterns) if r.detected_patterns else "-"
                pct = "  n/a " if r.match_percent is None else f"{r.match_percent:6.2f}"
                print(f"  {pct}% {r.symbol} [{pats}]")

    totals: dict[str, int] = {}

    def _merge(stats):
        for k, v in stats.items():
            totals[k] = totals.get(k, 0) + v

    if actual_workers == 1:
        # Single chunk — run directly, print verbose inline
        results, stats = _run_single_batch(chunks[0], project_dir, reloc_config)
        _merge(stats)
        if verbose:
            _emit_verbose(results)
        return sorted(results, key=lambda r: (r.db_id, r.symbol)), totals

    by_chunk: dict[int, list[FunctionResult]] = {}
    with ProcessPoolExecutor(max_workers=actual_workers) as pool:
        futures = {
            pool.submit(_run_single_batch, chunk, project_dir, reloc_config): i
            for i, chunk in enumerate(chunks)
        }

        for future in as_completed(futures):
            chunk_idx = futures[future]
            chunk_results, stats = future.result()
            by_chunk[chunk_idx] = chunk_results
            _merge(stats)

            # LIVE progress only, and deliberately WITHOUT the worker index:
            # these lines arrive in completion order, which varies run to run,
            # so an index here would make the output nondeterministic for no
            # information gain (chunks are equal-sized and their numbering is
            # arbitrary).  The ordered per-chunk table is printed below.
            print(f"  worker finished ({len(chunk_results)} functions)",
                  file=sys.stderr)

    all_results: list[FunctionResult] = []
    for i in sorted(by_chunk):                 # chunk order, not completion order
        all_results.extend(by_chunk[i])
        print(f"  Worker {i + 1}/{actual_workers}: {len(by_chunk[i])} functions")
    if verbose:
        _emit_verbose(all_results)

    return sorted(all_results, key=lambda r: (r.db_id, r.symbol)), totals


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Run objdiff on all functions and sync to DB")
    p.add_argument("--db", type=Path, default=DEFAULT_DB)
    p.add_argument("--dry-run", action="store_true",
                   help="Preview changes without writing to DB")
    p.add_argument("--unit", type=str, default=None,
                   help="Only process functions in units matching this glob")
    p.add_argument("--min", type=float, default=None,
                   help="Minimum current_percent to include (e.g. 0, 50, 90)")
    p.add_argument("--max", type=float, default=None,
                   help="Maximum current_percent to include (e.g. 0, 99.9, 100)")
    p.add_argument("--skip-100", action="store_true",
                   help="Skip functions already marked COMPLETE")
    p.add_argument("--promote", action="store_true", default=True,
                   help="Promote 100%% matches to COMPLETE (default: true)")
    p.add_argument("--no-promote", action="store_false", dest="promote",
                   help="Don't promote 100%% matches")
    p.add_argument("-j", "--jobs", type=int, default=4,
                   help="Number of parallel batch workers (default: 4)")
    p.add_argument("--divergent", action="store_true", default=True,
                   help="Only scan functions with unicorn_verdict = DIVERGENT "
                        "(DEFAULT, unchanged). NOTE this also excludes rows with "
                        "unicorn_verdict NULL -- i.e. everything never behaviourally "
                        "tested. The default run is a population-shaped subset; the "
                        "summary now prints how many rows it excluded.")
    p.add_argument("--all", action="store_false", dest="divergent",
                   help="Scan all functions, not just divergent")
    p.add_argument("--auto-at-limit", action="store_true", default=False,
                   help="Auto-write verdict='AT_LIMIT' / verdict_reason='auto: all "
                        "mismatches unfixable' for rows at >=95%% whose detected "
                        "patterns are all in PRACTICALLY_UNFIXABLE, or whose objdiff "
                        "classification is AT_LIMIT. CHANGED DEFAULT 2026-08-19: this "
                        "was ON and unconditional; it is now OFF. It manufactures "
                        "AT_LIMIT certificates from `detected_patterns`, and "
                        "certify_floor.py:69 states that pattern data is stale/noisy "
                        "and must never be evidence. 18,648 rows in the DB already "
                        "carry that exact verdict_reason.")
    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument("--skip-patch-check", action="store_true",
                   help="Skip the verify_objs_patched.py --verify-manifest "
                        "precondition on REPO_ROOT's object tree. Same escape "
                        "hatch, and same warning, as "
                        "backfill_reloc_patterns.py --skip-verify.")
    add_coverage_args(p)
    return p.parse_args()


def main():
    args = parse_args()
    cov = CoverageReport("sync_objdiff", args=args)

    if not OBJDIFF_CLI.exists():
        print(f"Error: objdiff-cli not found at {OBJDIFF_CLI}", file=sys.stderr)
        sys.exit(1)

    # This script writes `detected_patterns` / `has_address_relocation` /
    # `has_linker_merged` -- flags that are read back as facts about the build
    # -- and its pattern pass deliberately uses a relocation-SENSITIVE ruler
    # (`--reloc-config` != none).  That is precisely the ruler the post-compile
    # patchers move: an object left unpatched by a single-TU build carries raw
    # `?A0x<hash>` anon-namespace names, unrenamed `??__F` atexit scope
    # counters and `$S` guards, every one of which reads as a relocation NAME
    # divergence.  Measured 2026-08-20 on BustAMovePanel: functionRelocDiffs=
    # none was completely blind to the difference (8/8 functions identical),
    # while name_check gained phantom rows on 3 of 8 -- +1 on SetUpMoveNames,
    # +4 on Poll, +23 on OnBeat.  Same precondition, and same escape hatch, as
    # backfill_reloc_patterns.py (2026-08-19 triage).
    if not args.skip_patch_check:
        try:
            sys.path.insert(0, str(REPO_ROOT / "scripts"))
            from orchestrator.patch_guard import (UnpatchedTreeError,
                                                  ensure_patched_tree)
            ensure_patched_tree(REPO_ROOT, build=False)
        except UnpatchedTreeError as e:
            print(f"\nREFUSING to sync objdiff flags:\n{e}\n\n"
                  f"To record a survey from an unsettled tree anyway (never "
                  f"with a write unless you mean it), pass --skip-patch-check.",
                  file=sys.stderr)
            sys.exit(4)
    else:
        print("WARNING: --skip-patch-check -- the flags this run writes "
              "describe whatever the object tree was at this minute, not the "
              "build.", file=sys.stderr)

    if not args.db.exists():
        print(f"Error: Database not found: {args.db}", file=sys.stderr)
        sys.exit(1)

    # Query functions from DB
    conn = sqlite3.connect(str(args.db))
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode = WAL")

    SELECT_COLS = ("id, symbol, unit, current_percent, best_percent, verdict, "
                   "is_stub, match_percent_normalized")
    query = f"SELECT {SELECT_COLS} FROM functions WHERE 1=1"
    params: list = []

    # Exclude SDK
    for prefix in SDK_UNIT_PREFIXES:
        query += " AND unit NOT LIKE ?"
        params.append(f"{prefix}%")

    # Exclude merged symbols (ESCAPE the literal '_'; SQL LIKE '_' is a wildcard)
    query += " AND symbol NOT LIKE 'merged\\_%' ESCAPE '\\'"

    if args.unit:
        pattern = args.unit
        if pattern.startswith("src/"):
            pattern = "default/" + pattern[4:]
        elif not pattern.startswith("default/") and not pattern.startswith("*"):
            pattern = "default/" + pattern
        query += " AND unit GLOB ?"
        params.append(pattern)

    if args.min is not None:
        if args.min == 0:
            query += " AND (current_percent IS NULL OR current_percent >= ?)"
        else:
            query += " AND current_percent >= ?"
        params.append(args.min)

    if args.max is not None:
        query += " AND current_percent <= ?"
        params.append(args.max)

    if args.skip_100:
        query += " AND (verdict IS NULL OR verdict != 'COMPLETE')"

    # The DENOMINATOR: identical query WITHOUT the --divergent clause.  The
    # default run scans only unicorn_verdict='DIVERGENT', which also excludes
    # every NULL (never behaviourally tested) row -- a population-shaped subset
    # that the mode line named but never sized.
    query_before_divergent = query
    params_before_divergent = list(params)

    if args.divergent:
        query += " AND unicorn_verdict = 'DIVERGENT'"

    # Deterministic order: the query had no ORDER BY, so chunk membership --
    # and therefore worker assignment and output order -- varied between runs.
    query += " ORDER BY unit ASC, symbol ASC, id ASC"

    rows = conn.execute(query, params).fetchall()
    functions = [(row["id"], row["symbol"]) for row in rows]

    universe_rows = conn.execute(
        query_before_divergent.replace(f"SELECT {SELECT_COLS}", "SELECT count(*)", 1),
        params_before_divergent).fetchone()[0]
    excluded_by_divergent = universe_rows - len(functions) if args.divergent else 0
    never_unicorn_tested = conn.execute(
        query_before_divergent.replace(f"SELECT {SELECT_COLS}", "SELECT count(*)", 1) + " AND unicorn_verdict IS NULL",
        params_before_divergent).fetchone()[0]

    # Read-only: how many rows already carry the auto-AT_LIMIT reason string
    # this script writes.  certify_floor.py:69 says `primary_pattern` is
    # stale/noisy and must never be evidence, so this is a count worth seeing
    # before enabling --auto-at-limit.
    auto_reason_rows = conn.execute(
        "SELECT count(*) FROM functions WHERE verdict_reason = ?",
        ("auto: all mismatches unfixable",)).fetchone()[0]
    auto_reason_by_verdict = conn.execute(
        "SELECT COALESCE(verdict, '(NULL)') AS v, count(*) AS n FROM functions "
        "WHERE verdict_reason = ? GROUP BY v ORDER BY n DESC, v ASC",
        ("auto: all mismatches unfixable",)).fetchall()
    # Build lookup for verdict downgrade decisions
    function_meta: dict[int, dict] = {
        row["id"]: {
            "verdict": row["verdict"],
            "best_percent": row["best_percent"],
            "is_stub": row["is_stub"],
            "match_percent_normalized": row["match_percent_normalized"],
        }
        for row in rows
    }
    conn.close()

    print(f"Functions to scan: {len(functions)} of {universe_rows} "
          f"selected by the same query WITHOUT --divergent")
    if args.divergent:
        pct = (100.0 * excluded_by_divergent / universe_rows) if universe_rows else 0.0
        print(f"  excluded_by_--divergent: {excluded_by_divergent} of {universe_rows} "
              f"({pct:.1f}%), of which "
              f"{never_unicorn_tested} have unicorn_verdict NULL (never tested). "
              f"Pass --all to scan them.")
    print(f"Workers: {args.jobs}")
    filters = []
    if args.dry_run:
        filters.append("DRY RUN")
    if args.divergent:
        filters.append("DIVERGENT only")
    filters.append("BATCH mode")
    filters.append("auto-AT_LIMIT " + ("ON" if args.auto_at_limit else "OFF"))
    print(f"Mode: {', '.join(filters)}")
    print(f"Rows already carrying verdict_reason='auto: all mismatches unfixable': "
          f"{auto_reason_rows}"
          + ("  [" + ", ".join(f"{r['v']}={r['n']}" for r in auto_reason_by_verdict) + "]"
             if auto_reason_by_verdict else ""))
    print()

    cov.universe(universe_rows,
                 "DB rows selected by the unit/percent/skip-100 filters "
                 "(i.e. before --divergent)")
    if excluded_by_divergent:
        cov.drop("excluded-by---divergent", excluded_by_divergent,
                 note=f"unicorn_verdict != 'DIVERGENT'; {never_unicorn_tested} of "
                      f"these are NULL (never behaviourally tested)")
    cov.extra("auto_at_limit_reason_rows_in_db", auto_reason_rows)
    cov.extra("auto_at_limit_enabled", bool(args.auto_at_limit))

    if not functions:
        print("Nothing to scan.")
        sys.exit(cov.emit())

    # Run batch objdiff
    project_dir = str(REPO_ROOT)
    start_time = time.time()

    results, line_stats = run_batch(functions, project_dir, jobs=args.jobs,
                                    verbose=args.verbose)

    elapsed = time.time() - start_time
    rate = len(results) / elapsed if elapsed > 0 else 0
    print(f"\nScan complete: {len(results)} result records for {len(functions)} "
          f"requested functions in {elapsed:.1f}s ({rate:.0f}/s)")
    if line_stats:
        print(f"  objdiff stdout: {line_stats.get('stdout_lines', 0)} lines -> "
              f"{line_stats.get('parsed_records', 0)} parsed; "
              f"{line_stats.get('blank_lines', 0)} blank, "
              f"{line_stats.get('malformed_json_lines', 0)} not-JSON, "
              f"{line_stats.get('unrequested_symbol_lines', 0)} about symbols we "
              f"did not request")
    for k, v in sorted(line_stats.items()):
        cov.extra(f"objdiff_{k}", v)

    # Compute stats
    stats: dict[str, int] = {
        "scanned": len(results),
        "matched": 0,
        "not_found": 0,
        "unimplemented": 0,
        "skipped": 0,
        "errors": 0,
        "pct_updated": 0,
        "promoted": 0,
        "demoted_complete": 0,
        "demoted_at_limit": 0,
        "patterns_set": 0,
    }
    # Track unimplemented functions for breakdown table
    unimplemented_by_unit: dict[str, list[str]] = {}  # unit -> [demangled or symbol]
    # Track partial matches by percentage bucket for work-remaining table
    # bucket -> unit -> [(pct, demangled or symbol)]
    partial_by_bucket: dict[str, dict[str, list[tuple[float, str]]]] = {
        "99-100": {},   # 99.0 <= pct < 100.0
        "95-99": {},    # 95.0 <= pct < 99.0
        "80-95": {},    # 80.0 <= pct < 95.0
        "<80": {},      # 0 <= pct < 80.0
    }

    # Unfixable patterns that prevent reaching 100%
    UNFIXABLE_PATTERNS = {
        "LINKER_MERGED", "BOOL_MASK", "ADDRESS_RELOCATION_NOISE",
        "BOOLEAN_NEGATION", "ANONYMOUS_NAMESPACE_HASH",
        "DEAD_STORE_ELIMINATION",
    }

    pct_updates: list[tuple] = []
    enrich_updates: list[tuple] = []
    promotions: list[int] = []
    at_limit_promotions: list[int] = []  # NULL -> AT_LIMIT
    demotions: list[int] = []  # COMPLETE/AT_LIMIT -> NULL
    stub_updates: list[int] = []
    stub_clears: list[int] = []  # is_stub 1 -> 0 (now has base code)

    # Build set of symbols that have base code somewhere (for COMDAT detection).
    # If a symbol is "unimplemented" in one unit but compiled in another,
    # it's a COMDAT emission difference, not truly missing.
    compiled_symbols: set[str] = set()
    for r in results:
        if r.error is None and r.match_percent is not None:
            compiled_symbols.add(r.symbol)

    for r in results:
        if r.error == "not_found":
            stats["not_found"] += 1
            continue
        if r.error == "skipped":
            stats["skipped"] += 1
            if not args.dry_run:
                stub_updates.append(r.db_id)
            continue
        if r.error == "unimplemented":
            if r.symbol in compiled_symbols:
                stats["comdat_elsewhere"] = stats.get("comdat_elsewhere", 0) + 1
            else:
                stats["unimplemented"] += 1
                unit_key = (r.unit or "unknown").removeprefix("default/")
                name = r.demangled or r.symbol
                unimplemented_by_unit.setdefault(unit_key, []).append(name)
            if not args.dry_run:
                stub_updates.append(r.db_id)
            continue
        if r.error:
            stats["errors"] += 1
            continue

        stats["matched"] += 1

        # Collect partial matches into buckets
        if r.match_percent is not None and r.match_percent < 100.0:
            if r.match_percent >= 99.0:
                bucket = "99-100"
            elif r.match_percent >= 95.0:
                bucket = "95-99"
            elif r.match_percent >= 80.0:
                bucket = "80-95"
            else:
                bucket = "<80"
            unit_key = (r.unit or "unknown").removeprefix("default/")
            name = r.demangled or r.symbol
            partial_by_bucket[bucket].setdefault(unit_key, []).append((r.match_percent, name))

        # Clear stale is_stub flag if function now has base code
        meta = function_meta.get(r.db_id, {})
        if meta.get("is_stub") and not args.dry_run:
            stub_clears.append(r.db_id)
            stats["stub_cleared"] = stats.get("stub_cleared", 0) + 1

        if r.match_percent is not None:
            reachable = 1
            if r.detected_patterns and UNFIXABLE_PATTERNS & set(r.detected_patterns):
                # Functions with unfixable patterns are not reachable to 100%
                # unless they're already at 100%
                if r.match_percent < 100.0:
                    reachable = 0

            detected_json = json.dumps(r.detected_patterns) if r.detected_patterns else None

            pct_updates.append((
                r.match_percent,
                r.match_percent,
                r.size,
                r.demangled,
                r.db_id,
            ))
            # NOTE: has_linker_merged is deliberately NOT written here. This
            # pass runs with functionRelocDiffs=none, which masks the very
            # relocation differences detect_linker_merged reads, so it would
            # write 0 for every row and wipe whatever
            # scripts/backfill_reloc_patterns.py established. See the docstring
            # on _run_single_batch.
            enrich_updates.append((
                1 if r.has_bool_mask else 0,
                1 if r.has_makestring_mismatch else 0,
                1 if r.has_address_relocation else 0,
                1 if r.has_boolean_negation else 0,
                1 if r.has_float_precision else 0,
                1 if r.has_fsel_ternary else 0,
                1 if r.has_float_to_int_to_float else 0,
                1 if r.has_register_swap else 0,
                1 if r.has_comparison_style else 0,
                1 if r.has_control_flow else 0,
                1 if r.has_commutative_op_order else 0,
                1 if r.has_offset_swap else 0,
                1 if r.has_anonymous_namespace_hash else 0,
                1 if r.has_static_guard_counter else 0,
                1 if r.has_dynamic_cast_mismatch else 0,
                1 if r.has_dead_store_elimination else 0,
                1 if r.has_prologue_mismatch else 0,
                1 if r.has_alloca_mismatch else 0,
                1 if r.has_scope_counter_mismatch else 0,
                detected_json,
                r.primary_pattern,
                reachable,
                r.db_id,
            ))
            stats["pct_updated"] += 1

            for flag_name in [
                "merged", "bool_mask", "makestring_mismatch", "address_relocation",
                "boolean_negation", "float_precision", "fsel_ternary",
                "float_to_int_to_float", "register_swap", "comparison_style",
                "control_flow", "commutative_op_order", "offset_swap",
                "anonymous_namespace_hash", "static_guard_counter",
                "dynamic_cast_mismatch", "dead_store_elimination",
                "prologue_mismatch", "alloca_mismatch", "scope_counter_mismatch",
            ]:
                if getattr(r, f"has_{flag_name}", False):
                    stats[f"{flag_name}_flagged"] = stats.get(f"{flag_name}_flagged", 0) + 1
            if r.primary_pattern:
                stats["patterns_set"] += 1

            if args.promote and r.match_percent == 100.0:
                promotions.append(r.db_id)
                stats["promoted"] += 1

            # Verdict downgrade logic
            old_verdict = meta.get("verdict")
            if old_verdict == "COMPLETE" and r.match_percent < 100.0:
                # False COMPLETE — demote back to NULL so it's workable
                demotions.append(r.db_id)
                stats["demoted_complete"] += 1
                # ...but sync_match_percent.py promotes on
                # `match_percent_normalized >= 100` (that file, line 419).  A row
                # that satisfies BOTH conditions is a verdict the two writers
                # will flip back and forth forever.  Counted, not silently done.
                db_norm = meta.get("match_percent_normalized")
                if db_norm is not None and db_norm >= 100.0:
                    stats["demoted_but_db_normalized_100"] = \
                        stats.get("demoted_but_db_normalized_100", 0) + 1

            # Auto-promote to AT_LIMIT when objdiff says all mismatches
            # are unfixable (verdict_classification == AT_LIMIT)
            # OR when match% >= 95% and all detected patterns are practically
            # unfixable (register swaps, prologue mismatches, etc.)
            PRACTICALLY_UNFIXABLE = UNFIXABLE_PATTERNS | {
                "REGISTER_SWAP", "PROLOGUE_MISMATCH",
                "STATIC_GUARD_COUNTER",
            }
            if (old_verdict is None
                    and r.match_percent is not None
                    and r.match_percent < 100.0):
                should_promote = False
                if r.verdict_classification == "AT_LIMIT":
                    should_promote = True
                elif (r.match_percent >= 95.0
                      and r.detected_patterns
                      and set(r.detected_patterns).issubset(PRACTICALLY_UNFIXABLE)):
                    should_promote = True
                if should_promote:
                    # Counted either way, so turning the flag on or off never
                    # changes the reported number -- only whether it is written.
                    stats["auto_at_limit_candidates"] = \
                        stats.get("auto_at_limit_candidates", 0) + 1
                    if args.auto_at_limit:
                        at_limit_promotions.append(r.db_id)
                        stats["auto_at_limit"] = stats.get("auto_at_limit", 0) + 1

    # Apply to DB
    if not args.dry_run and (pct_updates or enrich_updates or promotions or at_limit_promotions or demotions or stub_updates or stub_clears):
        conn = sqlite3.connect(str(args.db))
        conn.execute("PRAGMA journal_mode = WAL")

        if pct_updates:
            conn.executemany(
                """UPDATE functions
                   SET current_percent = ?,
                       best_percent = MAX(COALESCE(best_percent, 0), ?),
                       size = COALESCE(?, size),
                       demangled = COALESCE(?, demangled),
                       updated_at = CURRENT_TIMESTAMP
                   WHERE id = ?""",
                pct_updates,
            )

        if enrich_updates:
            conn.executemany(
                """UPDATE functions
                   SET has_bool_mask = ?,
                       has_makestring_mismatch = ?,
                       has_address_relocation = ?,
                       has_boolean_negation = ?,
                       has_float_precision = ?,
                       has_fsel_ternary = ?,
                       has_float_to_int_to_float = ?,
                       has_register_swap = ?,
                       has_comparison_style = ?,
                       has_control_flow = ?,
                       has_commutative_op_order = ?,
                       has_offset_swap = ?,
                       has_anonymous_namespace_hash = ?,
                       has_static_guard_counter = ?,
                       has_dynamic_cast_mismatch = ?,
                       has_dead_store_elimination = ?,
                       has_prologue_mismatch = ?,
                       has_alloca_mismatch = ?,
                       has_scope_counter_mismatch = ?,
                       detected_patterns = ?,
                       primary_pattern = ?,
                       reachable_100 = ?,
                       updated_at = CURRENT_TIMESTAMP
                   WHERE id = ?""",
                enrich_updates,
            )

        if promotions:
            conn.executemany(
                """UPDATE functions
                   SET verdict = 'COMPLETE',
                       updated_at = CURRENT_TIMESTAMP
                   WHERE id = ?""",
                [(fid,) for fid in promotions],
            )

        if at_limit_promotions:
            conn.executemany(
                """UPDATE functions
                   SET verdict = 'AT_LIMIT',
                       verdict_reason = 'auto: all mismatches unfixable',
                       updated_at = CURRENT_TIMESTAMP
                   WHERE id = ?""",
                [(fid,) for fid in at_limit_promotions],
            )

        if demotions:
            conn.executemany(
                """UPDATE functions
                   SET verdict = NULL,
                       updated_at = CURRENT_TIMESTAMP
                   WHERE id = ?""",
                [(fid,) for fid in demotions],
            )

        if stub_updates:
            conn.executemany(
                """UPDATE functions
                   SET is_stub = 1,
                       updated_at = CURRENT_TIMESTAMP
                   WHERE id = ?""",
                [(fid,) for fid in stub_updates],
            )

        if stub_clears:
            conn.executemany(
                """UPDATE functions
                   SET is_stub = 0,
                       updated_at = CURRENT_TIMESTAMP
                   WHERE id = ?""",
                [(fid,) for fid in stub_clears],
            )

        conn.commit()
        conn.close()

    # Coverage accounting on the main thread: every result record is either
    # examined (a real measurement) or dropped with a reason.
    cov.examine(stats["matched"])
    for slug, key in (("not-found", "not_found"),
                      ("skippable-stub", "skipped"),
                      ("unimplemented", "unimplemented"),
                      ("objdiff-error", "errors")):
        if stats.get(key):
            cov.drop(slug, stats[key])
    if stats.get("comdat_elsewhere"):
        cov.drop("comdat-emitted-in-another-tu", stats["comdat_elsewhere"])
    ruler_counts: dict[str, int] = {}
    for r in results:
        if r.ruler:
            ruler_counts[r.ruler] = ruler_counts.get(r.ruler, 0) + 1
    cov.extra("ruler_counts", dict(sorted(ruler_counts.items())))
    cov.note("ruler: normalized_match_percent from objdiff-cli diff --batch "
             "(objdiff's `diff` command aliases that value into the key named "
             "fuzzy_match_percent; report.json uses that same name for the RAW "
             "score, so the two files disagree on what `fuzzy` means)")

    # Print summary
    mode = " (DRY RUN)" if args.dry_run else ""
    print(f"\n--- Sync Results{mode} ---")
    print(f"  Universe:           {universe_rows} (DB rows the filters selected, "
          f"before --divergent)")
    print(f"  Requested:          {len(functions)}"
          + (f"  ({excluded_by_divergent} excluded by --divergent)"
             if excluded_by_divergent else ""))
    print(f"  Result records:     {stats['scanned']}")
    print(f"  Matched:            {stats['matched']}")
    print(f"  Not found:          {stats['not_found']}")
    print(f"  Skipped:            {stats['skipped']} (boilerplate/third-party with no base code)")
    print(f"  COMDAT elsewhere:   {stats.get('comdat_elsewhere', 0)} (compiled in different TU)")
    print(f"  Unimplemented:      {stats['unimplemented']}")
    print(f"  Stub cleared:       {stats.get('stub_cleared', 0)} (was stub, now has base code)")
    print(f"  Errors:             {stats['errors']}")
    bucket_sum = (stats["matched"] + stats["not_found"] + stats["skipped"]
                  + stats.get("comdat_elsewhere", 0) + stats["unimplemented"]
                  + stats["errors"])
    print(f"  Bucket sum:         {bucket_sum} of {stats['scanned']} result records"
          + ("" if bucket_sum == stats["scanned"]
             else f"   <-- MISMATCH of {stats['scanned'] - bucket_sum}"))
    print(f"  Percent updated:    {stats['pct_updated']}")
    print(f"  Promoted:           {stats['promoted']} (-> COMPLETE)")
    print(f"  Auto AT_LIMIT:      {stats.get('auto_at_limit', 0)} written"
          f"  / {stats.get('auto_at_limit_candidates', 0)} candidates"
          f"  [--auto-at-limit {'ON' if args.auto_at_limit else 'OFF (default since 2026-08-19)'}]")
    print(f"  Demoted COMPLETE:   {stats['demoted_complete']} (-> NULL)")
    if stats.get("demoted_but_db_normalized_100"):
        print(f"  !! of those, {stats['demoted_but_db_normalized_100']} have "
              f"match_percent_normalized >= 100 in the DB, which is exactly what "
              f"sync_match_percent.py --promote promotes on. Those two writers will "
              f"flip these verdicts against each other.")
    print(f"  Demoted AT_LIMIT:   {stats['demoted_at_limit']} (-> NULL)")
    print(f"  Patterns set:       {stats['patterns_set']}")
    if ruler_counts:
        print(f"  Ruler:              "
              + ", ".join(f"{k}={v}" for k, v in sorted(ruler_counts.items())))
    pattern_labels = [
        ("merged", "Merged"),
        ("bool_mask", "Bool mask"),
        ("makestring_mismatch", "MakeString"),
        ("address_relocation", "Address relocation"),
        ("boolean_negation", "Boolean negation"),
        ("float_precision", "Float precision"),
        ("fsel_ternary", "fsel ternary"),
        ("float_to_int_to_float", "float->int->float"),
        ("register_swap", "Register swap"),
        ("comparison_style", "Comparison style"),
        ("control_flow", "Control flow"),
        ("commutative_op_order", "Commutative op order"),
        ("offset_swap", "Offset swap"),
        ("anonymous_namespace_hash", "Anon namespace hash"),
        ("static_guard_counter", "Static guard counter"),
        ("dynamic_cast_mismatch", "dynamic_cast mismatch"),
        ("dead_store_elimination", "Dead store elimination"),
        ("prologue_mismatch", "Prologue mismatch"),
        ("alloca_mismatch", "alloca mismatch"),
        ("scope_counter_mismatch", "Scope counter mismatch"),
    ]
    print(f"  --- Patterns ---")
    for key, label in pattern_labels:
        count = stats.get(f"{key}_flagged", 0)
        if count > 0:
            print(f"  {label + ':':24s}{count}")

    # Print unimplemented breakdown by unit
    if unimplemented_by_unit:
        _CATEGORY_MAP = [
            ("system/os/",          "Platform"),
            ("system/rnddx9/",      "Rendering"),
            ("system/synth_xbox/",  "Audio"),
            ("system/synth/",       "Audio"),
            ("system/moviebink/",   "Video"),
            ("system/gesture/",     "Gesture"),
            ("system/net/",         "Network"),
            ("system/hamobj/",      "Game"),
            ("system/utl/",         "Utility"),
            ("system/char/",        "Character"),
            ("system/ui/",          "UI"),
            ("system/meta/",        "Meta"),
            ("lazer/",              "Game"),
        ]

        def _categorize(unit: str) -> str:
            for prefix, cat in _CATEGORY_MAP:
                if unit.startswith(prefix):
                    return cat
            return "Other"

        total = sum(len(v) for v in unimplemented_by_unit.values())
        # Full tie-break: ties on count must not depend on dict insertion order,
        # which (before the ORDER BY above) varied run to run.
        sorted_units = sorted(unimplemented_by_unit.items(),
                              key=lambda x: (-len(x[1]), x[0]))

        # Aggregate by category
        cat_counts: dict[str, int] = {}
        for unit, funcs in sorted_units:
            cat = _categorize(unit)
            cat_counts[cat] = cat_counts.get(cat, 0) + len(funcs)

        print(f"\n  --- Unimplemented by Unit ({total} total) ---")
        for unit, funcs in sorted_units:
            cat = _categorize(unit)
            print(f"  {unit:50s} {len(funcs):4d}  [{cat}]")

        print(f"\n  --- Unimplemented by Category ---")
        for cat, count in sorted(cat_counts.items(), key=lambda x: (-x[1], x[0])):
            print(f"  {cat + ':':16s}{count}")

    # Print partial match breakdown by percentage bucket
    any_partial = any(partial_by_bucket[b] for b in partial_by_bucket)
    if any_partial:
        bucket_labels = [
            ("99-100", "99%+"),
            ("95-99",  "95-99%"),
            ("80-95",  "80-95%"),
            ("<80",    "<80%"),
        ]

        # Summary line
        bucket_totals = {
            b: sum(len(v) for v in partial_by_bucket[b].values())
            for b, _ in bucket_labels
        }
        grand_total = sum(bucket_totals.values())
        print(f"\n  --- Partial Matches ({grand_total} total) ---")
        print(f"  {'Range':10s} {'Count':>6s}")
        for b, label in bucket_labels:
            if bucket_totals[b]:
                print(f"  {label:10s} {bucket_totals[b]:6d}")

        # Per-bucket unit breakdown
        for bucket_key, label in bucket_labels:
            bucket_data = partial_by_bucket[bucket_key]
            if not bucket_data:
                continue
            total_b = sum(len(v) for v in bucket_data.values())
            sorted_b = sorted(bucket_data.items(), key=lambda x: (-len(x[1]), x[0]))
            print(f"\n  --- {label} by Unit ({total_b} functions) ---")
            for unit, funcs in sorted_b:
                print(f"  {unit:50s} {len(funcs):4d}")

    sys.exit(cov.emit())


if __name__ == "__main__":
    main()
