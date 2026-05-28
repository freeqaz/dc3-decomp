"""Pattern scan — Semgrep-like AST scanning for permuter pattern matches.

Scans source files for functions where specific permuter patterns would
generate variants, WITHOUT building or scoring. Pure tree-sitter analysis.

This is much faster than batch_triage+batch_sweep because it skips:
- objdiff builds (~1s per function)
- Diagnosis from instruction-level diffs
- Variant scoring (compilation)

Instead, it just parses C++ and runs pattern generators to see which
functions produce variants.

Usage:
    # Scan specific patterns across all decomp source files
    python -m scripts.permuter.pattern_scan --patterns null_guard_elimination

    # Scan a specific unit
    python -m scripts.permuter.pattern_scan --patterns null_guard_elimination --unit "meta_ham/*"

    # Scan multiple patterns
    python -m scripts.permuter.pattern_scan --patterns null_guard_elimination,reference_elimination

    # Show variant details (what exactly would change)
    python -m scripts.permuter.pattern_scan --patterns null_guard_elimination --show-variants

    # Only show functions that aren't already 100%
    python -m scripts.permuter.pattern_scan --patterns null_guard_elimination --incomplete-only

    # JSON output for piping to other tools
    python -m scripts.permuter.pattern_scan --patterns null_guard_elimination --json

    # Limit to functions with match < 99%
    python -m scripts.permuter.pattern_scan --patterns null_guard_elimination --max-pct 99
"""

from __future__ import annotations

import argparse
import fnmatch
import glob
import hashlib
import json
import os
import re
import sqlite3
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

from .extractor import (
    _PARSER,
    _find_all_function_defs,
    _find_function_preproc_regions,
    _get_function_name,
)
from .patterns import get_pattern, list_patterns
from .patterns.base import Pattern
from .repo_paths import get_decomp_db_path
from .types import Diagnosis, FunctionContext, Variant

from .project import get_project_config as _get_project_config
REPO_ROOT = _get_project_config().repo_root
DECOMP_DB = get_decomp_db_path()
OBJDIFF_JSON = REPO_ROOT / "objdiff.json"
DIFF_CACHE_DIR = Path("/tmp/claude")


@dataclass
class ScanHit:
    """A function where a pattern generated at least one variant.

    ``confidence`` (set by asm-signal gating in :func:`_apply_asm_gating`):
      - ``ast_only``         — no asm diagnosis available; AST only.
      - ``asm_signal_match`` — diagnosis available and ``pattern.relevant()``
                               returned True (the asm shows the signal).
      - ``unknown``          — diagnosis unavailable for this fn (no cached
                               diff, no fresh build performed) OR the
                               function's qualified name is ambiguous between
                               multiple sub-100% overloads in the same TU
                               (Wave H1: we can't pick the right diff JSON).
      - ``excluded``         — diagnosis available and ``relevant()`` returned
                               False; only present when ``--include-unmatched-asm``
                               is on (otherwise dropped from results).
    """
    source_path: str
    function_name: str
    pattern_name: str
    variant_count: int
    variants: list[dict] = field(default_factory=list)  # name, description
    # Optional: match info from decomp.db
    symbol: str = ""
    match_percent: float | None = None
    unit: str = ""
    confidence: str = "ast_only"
    # Wave H1: true when multiple sub-100% overloads share the qualified
    # name in this TU, so the AST hit can't be uniquely attributed.
    ambiguous_overload: bool = False


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="python -m scripts.permuter.pattern_scan",
        description="Scan codebase for functions matching permuter patterns (no build required).",
    )
    parser.add_argument(
        "--patterns",
        help="Comma-separated pattern names to scan for (omit to list available patterns)",
    )
    parser.add_argument(
        "--unit",
        help="Unit glob pattern (e.g. 'meta_ham/*', 'system/obj/*')",
    )
    parser.add_argument(
        "--source",
        type=Path,
        help="Scan a single source file instead of the whole codebase",
    )
    parser.add_argument(
        "--show-variants", action="store_true",
        help="Show variant details (descriptions of what would change)",
    )
    parser.add_argument(
        "--incomplete-only", action="store_true",
        help="Only show functions that aren't already 100%% matched",
    )
    parser.add_argument(
        "--max-pct", type=float, default=100.0,
        help="Only show functions with match%% below this threshold (default: 100)",
    )
    parser.add_argument(
        "--min-pct", type=float, default=0.0,
        help="Only show functions with match%% above this threshold (default: 0)",
    )
    parser.add_argument(
        "--json", action="store_true", dest="json_output",
        help="Output results as JSON",
    )
    parser.add_argument(
        "--limit", type=int, default=0,
        help="Max hits to report (0 = unlimited)",
    )
    parser.add_argument(
        "--scan", action="store_true",
        help="When listing patterns (no --patterns), scan codebase and show hit counts (~30s)",
    )
    # ── asm-signal gating ──────────────────────────────────────────────
    parser.add_argument(
        "--require-asm-signal", action="store_true",
        help="Only report hits where the pattern's relevant(diagnosis) returns True. "
             "Uses cached diff_*.json by default; missing cache → confidence=unknown.",
    )
    parser.add_argument(
        "--include-unmatched-asm", action="store_true",
        help="With --require-asm-signal, also emit hits whose diagnosis says "
             "relevant=False (marked confidence=excluded) instead of dropping them.",
    )
    parser.add_argument(
        "--fresh-objdiff", action="store_true",
        help="Run objdiff to populate the diff cache for symbols whose diff JSON is missing. "
             "WARNING: ~1s per function — can be slow on large scans.",
    )
    parser.add_argument(
        "--diff-cache-dir", default=str(DIFF_CACHE_DIR),
        help=f"Directory containing diff_*.json files (default: {DIFF_CACHE_DIR}).",
    )
    return parser.parse_args()


def _load_source_files(unit_glob: str | None) -> list[tuple[str, str]]:
    """Load source file paths from objdiff.json, optionally filtered by unit glob.

    Returns list of (unit_name, source_path).
    """
    if not OBJDIFF_JSON.exists():
        print(f"Error: {OBJDIFF_JSON} not found", file=sys.stderr)
        sys.exit(1)

    with open(OBJDIFF_JSON) as f:
        data = json.load(f)

    results = []
    for unit in data.get("units", []):
        name = unit.get("name", "")
        source = unit.get("metadata", {}).get("source_path")
        if not source:
            continue
        if unit_glob and not fnmatch.fnmatch(name, f"*{unit_glob}*"):
            continue
        results.append((name, source))

    return results


def _load_match_info() -> dict[str, tuple[float, str]]:
    """Load function match percentages from decomp.db.

    Returns dict mapping qualified_name -> (match_percent, symbol).

    decomp.db's schema uses ``current_percent``; an older column name
    ``match_percent`` was used in DC3 and is tried as a fallback.

    NOTE (Wave H1): when multiple symbols share a qualified name (overloads,
    template instantiations), this returns the LAST one written into the dict.
    That's a known limitation — callers who care about disambiguation should
    use :func:`_load_match_info_multi` and choose the best candidate.
    """
    multi = _load_match_info_multi()
    return {q: (cands[0][0], cands[0][1]) for q, cands in multi.items() if cands}


def _load_match_info_multi() -> dict[str, list[tuple[float, str, str]]]:
    """Like :func:`_load_match_info` but returns ALL candidates per qname.

    Returns dict mapping qualified_name -> list of (match_percent, symbol, unit)
    tuples sorted by match_percent ASCENDING (lowest match first — most
    actionable candidate first).

    Added in Wave H1 to fix asm-signal misattribution: when two overloads
    share a qualified name (e.g. ``CamShotCrowd::AddCrowdChars`` has both a
    parameterless variant at 100% and a list-arg variant at 97%), the old
    last-write-wins dict could resolve a pattern hit to the WRONG overload —
    either masking a sub-100% target behind a 100% sibling, or vice versa
    pointing the asm-signal lookup at the wrong diff JSON.
    """
    if not DECOMP_DB.exists():
        return {}

    try:
        conn = sqlite3.connect(str(DECOMP_DB))
        conn.row_factory = sqlite3.Row
        try:
            rows = conn.execute(
                "SELECT symbol, demangled, unit, "
                "current_percent AS match_percent "
                "FROM functions WHERE current_percent IS NOT NULL"
            ).fetchall()
        except sqlite3.OperationalError:
            # Fallback for legacy schemas where the column is `match_percent`.
            rows = conn.execute(
                "SELECT symbol, demangled, unit, match_percent "
                "FROM functions WHERE match_percent IS NOT NULL"
            ).fetchall()
        conn.close()

        from .types import extract_qualified_name
        result: dict[str, list[tuple[float, str, str]]] = {}
        for row in rows:
            demangled = row["demangled"]
            # Skip rows with no demangled name. decomp.db contains many such
            # rows (static-init `__sinit_*`, bare `main`, asm-only symbols).
            # Passing None to extract_qualified_name raises TypeError, and the
            # broad except below would then discard EVERY entry — silently
            # returning {} and breaking asm-signal attribution for all hits.
            if not isinstance(demangled, str):
                continue
            qname = extract_qualified_name(demangled)
            if qname:
                result.setdefault(qname, []).append(
                    (row["match_percent"], row["symbol"], row["unit"] or "")
                )
        # Sort each candidate list by ascending match% (lowest first).
        for cands in result.values():
            cands.sort(key=lambda t: t[0])
        return result
    except Exception:
        return {}


def _unit_matches_source(unit: str, source_path: str) -> bool:
    """Return True when a decomp.db unit and a source file refer to the same TU.

    objdiff.json unit names look like ``main/system/rndobj/PropAnim`` while
    source paths look like ``src/system/rndobj/PropAnim.cpp``. We compare on the
    stem after stripping the conventional prefixes.
    """
    if not unit or not source_path:
        return False
    src_stem = source_path
    if src_stem.startswith("src/"):
        src_stem = src_stem[len("src/"):]
    for suf in (".cpp", ".cxx", ".cc", ".c"):
        if src_stem.endswith(suf):
            src_stem = src_stem[: -len(suf)]
            break
    unit_stem = unit
    if unit_stem.startswith("main/"):
        unit_stem = unit_stem[len("main/"):]
    return src_stem == unit_stem


def _resolve_hit_candidate(
    function_name: str,
    source_path: str,
    candidates_by_qname: dict[str, list[tuple[float, str, str]]],
) -> tuple[tuple[float, str, str] | None, bool]:
    """Pick the best (match_pct, symbol, unit) for an AST hit.

    Returns ``(candidate, ambiguous)`` where ``ambiguous`` is True when more
    than one *sub-100%* overload in the same TU shares the qualified name —
    in that case we can't reliably attribute the AST hit to a single symbol,
    so callers should treat the asm-signal lookup as unknown.

    Algorithm:
      1. Look up the qname's candidates.
      2. Prefer candidates whose ``unit`` matches the source file's path
         (filters out cross-TU collisions like template instantiations
         emitted into multiple TUs).
      3. Among the in-TU candidates, pick the lowest match% (most actionable).
      4. If multiple in-TU candidates are still sub-100%, mark ambiguous.
    """
    cands = candidates_by_qname.get(function_name, [])
    if not cands:
        return None, False

    in_tu = [c for c in cands if _unit_matches_source(c[2], source_path)]
    pool = in_tu if in_tu else cands

    # Ambiguity = multiple distinct overloads, more than one of which is sub-100%.
    sub_hundred = [c for c in pool if c[0] < 100.0]
    ambiguous = len(sub_hundred) > 1

    # Picked candidate: lowest match% in the pool (already sorted ascending).
    return pool[0], ambiguous


def _diff_filename_for_symbol(symbol: str, cache_dir: Path) -> Path:
    """Reproduce the diff cache filename for a symbol (mirrors diff_inspect.py)."""
    h = hashlib.md5(symbol.encode()).hexdigest()[:12]
    slug = re.sub(r'[^a-zA-Z0-9]+', '_', symbol)[:40].strip('_').lower()
    return cache_dir / f"diff_{slug}_{h}.json"


def _build_diff_index(cache_dir: Path) -> dict[str, Path]:
    """Index every diff_*.json in ``cache_dir`` by its ``symbol`` field.

    Falls back to filename-slug matching only if needed. Returns
    ``{symbol_str: Path}``. Diff files without a ``symbol`` field are skipped.
    """
    idx: dict[str, Path] = {}
    if not cache_dir.exists():
        return idx
    for path_str in glob.glob(str(cache_dir / "diff_*.json")):
        path = Path(path_str)
        try:
            with open(path) as f:
                data = json.load(f)
        except (OSError, json.JSONDecodeError):
            continue
        sym = data.get("symbol", "") or ""
        if sym:
            # If multiple diffs exist for one symbol, prefer the most recent.
            existing = idx.get(sym)
            if existing is None or path.stat().st_mtime > existing.stat().st_mtime:
                idx[sym] = path
    return idx


def _load_diagnosis_for_symbol(
    symbol: str,
    diff_index: dict[str, Path],
    cache_dir: Path,
    fresh_objdiff: bool,
    fresh_attempted: set[str],
) -> tuple[Diagnosis | None, dict | None]:
    """Return (diagnosis, raw_objdiff_json) for ``symbol``, or (None, None) if absent.

    Uses the prebuilt index first; if missing AND ``fresh_objdiff`` is set,
    invokes ``bin/objdiff-cli`` once per symbol (tracked via ``fresh_attempted``)
    to populate the cache. Heavy callers should batch via diff_index.
    """
    if not symbol:
        return None, None

    path = diff_index.get(symbol)
    if path is None and fresh_objdiff and symbol not in fresh_attempted:
        fresh_attempted.add(symbol)
        # Lazy import to avoid bringing in subprocess machinery for default paths.
        try:
            target_path = _diff_filename_for_symbol(symbol, cache_dir)
            target_path.parent.mkdir(parents=True, exist_ok=True)
            objdiff_bin = REPO_ROOT / "bin" / "objdiff-cli"
            if not objdiff_bin.exists():
                return None, None
            cmd = [
                str(objdiff_bin), "diff",
                "-p", str(REPO_ROOT),
                symbol,
                "--include-instructions", "--build", "--incremental",
                "-f", "json", "-o", str(target_path),
            ]
            res = subprocess.run(cmd, cwd=str(REPO_ROOT),
                                 capture_output=True, text=True, timeout=120)
            if res.returncode == 0 and target_path.exists():
                path = target_path
                diff_index[symbol] = path
        except (OSError, subprocess.SubprocessError):
            return None, None

    if path is None:
        return None, None
    try:
        with open(path) as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError):
        return None, None
    if not data.get("instructions"):
        return None, data
    # Late import to avoid circular dependency at module load.
    from .diagnosis import diagnose_baseline
    try:
        diag = diagnose_baseline(data)
    except Exception:
        return None, data
    return diag, data


def _scan_file(
    source_path: Path,
    patterns: list[Pattern],
    unit_name: str,
    match_info: dict[str, tuple[float, str]],
    show_variants: bool,
    max_variants_per_func: int = 10,
    match_info_multi: dict[str, list[tuple[float, str, str]]] | None = None,
) -> list[ScanHit]:
    """Scan a single source file for pattern matches.

    ``match_info_multi`` (Wave H1) supersedes ``match_info`` when present: it
    lets the per-hit resolver disambiguate overloads using the source file
    path. Old callers that pass only ``match_info`` get the legacy
    last-write-wins behavior for backward compat.
    """
    if not source_path.exists():
        return []

    try:
        source = source_path.read_bytes()
    except OSError:
        return []

    tree = _PARSER.parse(source)
    hits: list[ScanHit] = []
    src_str = str(source_path)

    for func_node in _find_all_function_defs(tree.root_node):
        name = _get_function_name(func_node)
        if not name:
            continue

        body = func_node.child_by_field_name("body")
        if body is None:
            continue

        statements = list(body.named_children)
        func_range = (func_node.start_byte, func_node.end_byte)
        ctx = FunctionContext(
            file_path=source_path,
            file_source=source,
            func_node=func_node,
            body_node=body,
            statements=statements,
            func_byte_range=func_range,
            preproc_regions=_find_function_preproc_regions(source, func_range),
        )

        # Fix (Wave H1): per-function, in-TU candidate selection.
        # Resolve once per (name, source) and reuse for every pattern hit.
        if match_info_multi is not None:
            cand, ambiguous = _resolve_hit_candidate(
                name, src_str, match_info_multi,
            )
            if cand is None:
                hit_pct: float | None = None
                hit_sym = ""
                hit_unit = unit_name
            else:
                hit_pct, hit_sym, in_tu_unit = cand
                hit_unit = in_tu_unit or unit_name
        else:
            info = match_info.get(name, (None, ""))
            hit_pct = info[0]
            hit_sym = info[1]
            hit_unit = unit_name
            ambiguous = False

        for pattern in patterns:
            try:
                variants = []
                for v in pattern.generate(ctx):
                    variants.append({"name": v.name, "description": v.description})
                    if len(variants) >= max_variants_per_func:
                        break

                if variants:
                    hit = ScanHit(
                        source_path=src_str,
                        function_name=name,
                        pattern_name=pattern.name,
                        variant_count=len(variants),
                        variants=variants if show_variants else [],
                        match_percent=hit_pct,
                        symbol=hit_sym,
                        unit=hit_unit,
                        ambiguous_overload=ambiguous,
                    )
                    hits.append(hit)
            except Exception:
                # Don't let one function's parse error kill the scan
                continue

    return hits


def main():
    args = parse_args()

    # No patterns specified — show available and exit
    if not args.patterns:
        from .scan_and_permute import _print_pattern_table, _scan_all_counts
        counts = None
        if getattr(args, 'scan', False):
            print("Scanning codebase for all patterns...", file=sys.stderr)
            scan_start = time.time()
            counts = _scan_all_counts(args.unit)
            print(f"  Done in {time.time() - scan_start:.1f}s", file=sys.stderr)
        _print_pattern_table(counts)
        sys.exit(0)

    # Parse pattern names
    default_available = list_patterns()
    all_available = list_patterns(include_opt_in=True)
    if args.patterns.strip() == "all":
        # Keep historical behavior: `all` excludes opt-in patterns.
        pattern_names = default_available
    else:
        pattern_names = [p.strip() for p in args.patterns.split(",")]

    patterns = []
    for name in pattern_names:
        if name not in all_available:
            print(f"Error: unknown pattern '{name}'", file=sys.stderr)
            from .scan_and_permute import _print_pattern_table
            _print_pattern_table()
            sys.exit(1)
        patterns.append(get_pattern(name))

    # Determine files to scan
    if args.source:
        files = [("(single file)", str(args.source))]
    else:
        files = _load_source_files(args.unit)

    if not files:
        print("No source files found.", file=sys.stderr)
        sys.exit(0)

    # Load match info for filtering.
    # Fix (Wave H1): load the multi-candidate variant so _scan_file can
    # disambiguate overloads by source-file/unit, and so the asm-signal
    # filter doesn't silently lock onto a 100%-match sibling overload.
    match_info_multi = _load_match_info_multi()
    match_info = {q: (c[0][0], c[0][1]) for q, c in match_info_multi.items() if c}

    # asm-signal gating setup
    cache_dir = Path(args.diff_cache_dir)
    diff_index: dict[str, Path] = {}
    fresh_attempted: set[str] = set()
    if args.require_asm_signal:
        print(
            f"Building diff cache index from {cache_dir}/diff_*.json…",
            file=sys.stderr,
        )
        diff_index = _build_diff_index(cache_dir)
        print(
            f"  Indexed {len(diff_index)} cached diffs"
            f"{' (will fetch fresh on miss)' if args.fresh_objdiff else ''}.",
            file=sys.stderr,
        )

    # Scan
    start = time.time()
    all_hits: list[ScanHit] = []
    files_scanned = 0
    pattern_str = ", ".join(p.name for p in patterns)

    print(
        f"Scanning {len(files)} files for patterns: {pattern_str}",
        file=sys.stderr,
    )

    confidence_counter = {"ast_only": 0, "asm_signal_match": 0,
                          "unknown": 0, "excluded": 0}

    for unit_name, source_path in files:
        hits = _scan_file(
            Path(source_path), patterns, unit_name,
            match_info, args.show_variants,
            match_info_multi=match_info_multi,
        )

        # Apply filters
        for hit in hits:
            if args.incomplete_only and hit.match_percent is not None and hit.match_percent >= 100.0:
                continue
            if hit.match_percent is not None and hit.match_percent >= args.max_pct:
                continue
            if hit.match_percent is not None and hit.match_percent < args.min_pct:
                continue

            # asm-signal gating: determine confidence
            if args.require_asm_signal:
                # Fix (Wave H1): if the AST hit's qualified name resolves to
                # multiple sub-100% overloads in the same TU, we can't pick
                # the right diff JSON. Mark unknown and let hill_climber's
                # runtime relevant(diagnosis) make the final call per symbol.
                if hit.ambiguous_overload:
                    hit.confidence = "unknown"
                    confidence_counter["unknown"] += 1
                    all_hits.append(hit)
                    continue

                diag, _ = _load_diagnosis_for_symbol(
                    hit.symbol, diff_index, cache_dir,
                    args.fresh_objdiff, fresh_attempted,
                )
                if diag is None:
                    hit.confidence = "unknown"
                else:
                    pattern_obj = get_pattern(hit.pattern_name)
                    try:
                        is_rel = pattern_obj.relevant(diag)
                    except Exception:
                        is_rel = True  # Conservative: keep on relevance crash.
                    if is_rel:
                        hit.confidence = "asm_signal_match"
                    elif args.include_unmatched_asm:
                        hit.confidence = "excluded"
                    else:
                        # Drop entirely.
                        confidence_counter["excluded"] += 1
                        continue
            # else: confidence stays the default "ast_only".

            confidence_counter[hit.confidence] += 1
            all_hits.append(hit)

        files_scanned += 1

        if args.limit and len(all_hits) >= args.limit:
            all_hits = all_hits[:args.limit]
            break

    elapsed = time.time() - start

    # Output
    if args.json_output:
        data = {
            "metadata": {
                "patterns": pattern_names,
                "files_scanned": files_scanned,
                "elapsed_seconds": round(elapsed, 2),
                "require_asm_signal": args.require_asm_signal,
                "include_unmatched_asm": args.include_unmatched_asm,
                "fresh_objdiff": args.fresh_objdiff,
                "diff_cache_dir": str(cache_dir),
                "diff_cache_size": len(diff_index),
            },
            "hits": [
                {
                    "source_path": h.source_path,
                    "function_name": h.function_name,
                    "pattern": h.pattern_name,
                    "variant_count": h.variant_count,
                    "match_percent": h.match_percent,
                    "symbol": h.symbol,
                    "unit": h.unit,
                    "confidence": h.confidence,
                    # Wave H1: emit only when true to avoid bloating JSON.
                    **({"ambiguous_overload": True} if h.ambiguous_overload else {}),
                    **({"variants": h.variants} if h.variants else {}),
                }
                for h in all_hits
            ],
            "summary": {
                "total_hits": len(all_hits),
                "by_pattern": _count_by(all_hits, "pattern_name"),
                "by_confidence": dict(confidence_counter),
            },
        }
        print(json.dumps(data, indent=2))
    else:
        _print_text(all_hits, elapsed, files_scanned, args.show_variants,
                    args.require_asm_signal, confidence_counter)


def _count_by(hits: list[ScanHit], attr: str) -> dict[str, int]:
    from collections import Counter
    return dict(Counter(getattr(h, attr) for h in hits).most_common())


def _print_text(
    hits: list[ScanHit],
    elapsed: float,
    files_scanned: int,
    show_variants: bool,
    require_asm_signal: bool = False,
    confidence_counter: dict[str, int] | None = None,
):
    if not hits:
        print(f"\nNo hits found ({files_scanned} files scanned in {elapsed:.1f}s).")
        if require_asm_signal and confidence_counter:
            print(f"  asm-signal: excluded={confidence_counter.get('excluded', 0)}")
        return

    # Group by source file for readability
    by_file: dict[str, list[ScanHit]] = {}
    for h in hits:
        by_file.setdefault(h.source_path, []).append(h)

    print(f"\n{'=' * 70}")
    print(f"PATTERN SCAN RESULTS ({len(hits)} hits in {files_scanned} files, {elapsed:.1f}s)")
    print(f"{'=' * 70}")

    for source_path, file_hits in sorted(by_file.items()):
        print(f"\n  {source_path}")
        for h in file_hits:
            pct_str = f" ({h.match_percent:.1f}%)" if h.match_percent is not None else ""
            conf_str = f" [{h.confidence}]" if require_asm_signal else ""
            print(f"    [{h.pattern_name}] {h.function_name}{pct_str}{conf_str} — {h.variant_count} variant(s)")
            if show_variants:
                for v in h.variants:
                    print(f"      - {v['description']}")

    # Summary
    by_pattern = _count_by(hits, "pattern_name")
    print(f"\n  Summary: {len(hits)} functions across {len(by_file)} files")
    for pattern, count in by_pattern.items():
        print(f"    {pattern}: {count}")

    if require_asm_signal and confidence_counter:
        print(f"\n  Confidence breakdown:")
        for label in ("asm_signal_match", "unknown", "ast_only", "excluded"):
            n = confidence_counter.get(label, 0)
            if n:
                print(f"    {label:20}: {n}")


if __name__ == "__main__":
    main()
