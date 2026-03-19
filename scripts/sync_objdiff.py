#!/usr/bin/env python3
"""Run objdiff on every function and sync results to decomp.db.

Unlike sync_match_percent.py (which reads report.json for just match%),
this script runs `objdiff-cli diff --batch` to get analysis results for
all functions efficiently (grouping by unit to avoid redundant object
file loading), then updates enrichment columns:

  - current_percent, best_percent, size, demangled
  - has_linker_merged, has_bool_mask, primary_pattern, reachable_100
  - pattern detection columns from Rust analysis engine
  - verdict (COMPLETE for 100%, optionally AT_LIMIT for flagged patterns)

Usage:
    python3 scripts/sync_objdiff.py                         # divergent only (default)
    python3 scripts/sync_objdiff.py --all                   # full scan
    python3 scripts/sync_objdiff.py --unit 'system/char/*'  # filter by unit
    python3 scripts/sync_objdiff.py --dry-run               # preview
    python3 scripts/sync_objdiff.py --skip-100              # skip already-COMPLETE
"""
from __future__ import annotations

import argparse
import json
import math
import re
import sqlite3
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
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


def _extract_patterns_from_analysis(result: FunctionResult, data: dict) -> None:
    """Extract detected patterns from Rust analysis output."""
    analysis = data.get("analysis")
    if not analysis:
        return

    pattern_types = {p["pattern"] for p in analysis.get("patterns", [])}
    if not pattern_types:
        return

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


def _run_single_batch(functions: list[tuple[int, str]], project_dir: str) -> list[FunctionResult]:
    """Run a single objdiff-cli --batch process for a chunk of functions.

    Top-level function for ProcessPoolExecutor pickling.
    """
    # Build lookup map: lookup_name -> (db_id, original_symbol)
    lookup_to_info: dict[str, tuple[int, str]] = {}
    lookup_names: list[str] = []
    for db_id, symbol in functions:
        lookup = demangle_itanium(symbol) or symbol
        lookup_to_info[lookup] = (db_id, symbol)
        lookup_names.append(lookup)

    stdin_data = "\n".join(lookup_names) + "\n"

    try:
        proc = subprocess.run(
            [str(OBJDIFF_CLI), "diff", "-p", project_dir,
             "-c", "functionRelocDiffs=none", "--batch"],
            input=stdin_data, capture_output=True, text=True,
            timeout=600,
        )
    except subprocess.TimeoutExpired:
        return [FunctionResult(db_id=db_id, symbol=sym, error="timeout")
                for db_id, sym in functions]
    except Exception as e:
        return [FunctionResult(db_id=db_id, symbol=sym, error=str(e))
                for db_id, sym in functions]

    # Parse JSONL output
    results: list[FunctionResult] = []
    seen_lookups: set[str] = set()

    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            data = json.loads(line)
        except json.JSONDecodeError:
            continue

        symbol_name = data.get("symbol", "")
        seen_lookups.add(symbol_name)

        info = lookup_to_info.get(symbol_name)
        if not info:
            continue
        db_id, original_symbol = info

        result = FunctionResult(db_id=db_id, symbol=original_symbol)
        result.unit = data.get("unit", "")

        if "error" in data:
            result.error = data["error"]
            results.append(result)
            continue

        result.match_percent = data.get("fuzzy_match_percent")
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

    # Mark missing symbols as not_found
    for lookup, (db_id, original_symbol) in lookup_to_info.items():
        if lookup not in seen_lookups:
            results.append(FunctionResult(
                db_id=db_id, symbol=original_symbol, error="not_found"))

    return results


def run_batch(functions: list[tuple[int, str]], project_dir: str,
              jobs: int = 4, verbose: bool = False) -> list[FunctionResult]:
    """Run objdiff-cli in batch mode, splitting across parallel workers.

    Each worker gets a chunk of symbols and runs its own --batch process.
    Within each process, objdiff groups symbols by unit for efficient loading.
    """
    if not functions:
        return []

    # Split into chunks for parallel processing
    chunk_size = max(1, math.ceil(len(functions) / jobs))
    chunks = [functions[i:i + chunk_size] for i in range(0, len(functions), chunk_size)]
    actual_workers = len(chunks)

    all_results: list[FunctionResult] = []

    if actual_workers == 1:
        # Single chunk — run directly, print verbose inline
        results = _run_single_batch(chunks[0], project_dir)
        if verbose:
            for r in results:
                if r.error:
                    print(f"  SKIP {r.symbol}: {r.error}")
                else:
                    pats = ",".join(r.detected_patterns) if r.detected_patterns else "-"
                    print(f"  {r.match_percent:6.2f}% {r.symbol} [{pats}]")
        return results

    with ProcessPoolExecutor(max_workers=actual_workers) as pool:
        futures = {
            pool.submit(_run_single_batch, chunk, project_dir): i
            for i, chunk in enumerate(chunks)
        }

        for future in as_completed(futures):
            chunk_idx = futures[future]
            chunk_results = future.result()
            all_results.extend(chunk_results)

            if verbose:
                for r in chunk_results:
                    if r.error:
                        print(f"  SKIP {r.symbol}: {r.error}")
                    else:
                        pats = ",".join(r.detected_patterns) if r.detected_patterns else "-"
                        print(f"  {r.match_percent:6.2f}% {r.symbol} [{pats}]")

            print(f"  Worker {chunk_idx + 1}/{actual_workers} done "
                  f"({len(chunk_results)} functions)")

    return all_results


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
                   help="Only scan functions with unicorn_verdict = DIVERGENT (default)")
    p.add_argument("--all", action="store_false", dest="divergent",
                   help="Scan all functions, not just divergent")
    p.add_argument("-v", "--verbose", action="store_true")
    return p.parse_args()


def main():
    args = parse_args()

    if not OBJDIFF_CLI.exists():
        print(f"Error: objdiff-cli not found at {OBJDIFF_CLI}", file=sys.stderr)
        sys.exit(1)

    if not args.db.exists():
        print(f"Error: Database not found: {args.db}", file=sys.stderr)
        sys.exit(1)

    # Query functions from DB
    conn = sqlite3.connect(str(args.db))
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode = WAL")

    query = "SELECT id, symbol, unit, current_percent, best_percent, verdict, is_stub FROM functions WHERE 1=1"
    params: list = []

    # Exclude SDK
    for prefix in SDK_UNIT_PREFIXES:
        query += " AND unit NOT LIKE ?"
        params.append(f"{prefix}%")

    # Exclude merged symbols
    query += " AND symbol NOT LIKE 'merged_%'"

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

    if args.divergent:
        query += " AND unicorn_verdict = 'DIVERGENT'"

    rows = conn.execute(query, params).fetchall()
    functions = [(row["id"], row["symbol"]) for row in rows]
    # Build lookup for verdict downgrade decisions
    function_meta: dict[int, dict] = {
        row["id"]: {
            "verdict": row["verdict"],
            "best_percent": row["best_percent"],
            "is_stub": row["is_stub"],
        }
        for row in rows
    }
    conn.close()

    print(f"Functions to scan: {len(functions)}")
    print(f"Workers: {args.jobs}")
    filters = []
    if args.dry_run:
        filters.append("DRY RUN")
    if args.divergent:
        filters.append("DIVERGENT only")
    filters.append("BATCH mode")
    print(f"Mode: {', '.join(filters)}")
    print()

    if not functions:
        print("Nothing to scan.")
        return

    # Run batch objdiff
    project_dir = str(REPO_ROOT)
    start_time = time.time()

    results = run_batch(functions, project_dir, jobs=args.jobs, verbose=args.verbose)

    elapsed = time.time() - start_time
    rate = len(results) / elapsed if elapsed > 0 else 0
    print(f"\nScan complete: {len(results)} functions in {elapsed:.1f}s ({rate:.0f}/s)")

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
            enrich_updates.append((
                1 if r.has_merged else 0,
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
                   SET has_linker_merged = ?,
                       has_bool_mask = ?,
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

    # Print summary
    mode = " (DRY RUN)" if args.dry_run else ""
    print(f"\n--- Sync Results{mode} ---")
    print(f"  Scanned:            {stats['scanned']}")
    print(f"  Matched:            {stats['matched']}")
    print(f"  Not found:          {stats['not_found']}")
    print(f"  Skipped:            {stats['skipped']} (boilerplate/third-party with no base code)")
    print(f"  COMDAT elsewhere:   {stats.get('comdat_elsewhere', 0)} (compiled in different TU)")
    print(f"  Unimplemented:      {stats['unimplemented']}")
    print(f"  Stub cleared:       {stats.get('stub_cleared', 0)} (was stub, now has base code)")
    print(f"  Errors:             {stats['errors']}")
    print(f"  Percent updated:    {stats['pct_updated']}")
    print(f"  Promoted:           {stats['promoted']} (-> COMPLETE)")
    print(f"  Auto AT_LIMIT:      {stats.get('auto_at_limit', 0)} (all mismatches unfixable)")
    print(f"  Demoted COMPLETE:   {stats['demoted_complete']} (-> NULL)")
    print(f"  Demoted AT_LIMIT:   {stats['demoted_at_limit']} (-> NULL)")
    print(f"  Patterns set:       {stats['patterns_set']}")
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
        sorted_units = sorted(unimplemented_by_unit.items(), key=lambda x: -len(x[1]))

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
        for cat, count in sorted(cat_counts.items(), key=lambda x: -x[1]):
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
            sorted_b = sorted(bucket_data.items(), key=lambda x: -len(x[1]))
            print(f"\n  --- {label} by Unit ({total_b} functions) ---")
            for unit, funcs in sorted_b:
                print(f"  {unit:50s} {len(funcs):4d}")


if __name__ == "__main__":
    main()
