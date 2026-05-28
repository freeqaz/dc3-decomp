#!/usr/bin/env python3
"""Get decomp progress summary.

Returns total/complete/at_limit counts, percentages, pattern breakdown,
and top units with remaining work.

Excludes MSVC EH funclets (__unwind$NNN / __catch$NNN) from "remaining" —
the compiler emits these as a side effect of compiling a parent function
with /EHsc; they have no source representation you can author. dtk's
splitter emits each as an unnamed `fn_<addr>` symbol because the binary
loses the funclet's name at link time, which inflates the workable count.
We treat any `fn_<addr>` whose address appears as an `__unwind$` or
`__catch$` entry in the original linker map (orig/373307D9/ham_xbox_r.map)
as a build artifact, not orchestrator-tracked work.

objdiff v4.2.0 now *pairs* these funclets by byte signature, so they DO
score in report.json (~1,297 at 100% normalized). The orchestrator DB
(decomp.db, which this script reads) is a separate data plane and does not
ingest those matches, so the exclusion above stays correct for the
DB-derived "remaining" count. As a cross-check we read report.json and
warn if funclet pairing has regressed (the agreed sanity check that
replaced a blind denominator skip) — if that warning fires, objdiff's
funclet pairing broke and the exclusion is masking real numbers.

Usage:
    python3 scripts/get_progress.py
"""

import re
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT / "scripts"))

from orchestrator.database import get_connection, get_stats

DB_PATH = str(PROJECT_ROOT / "decomp.db")
MAP_PATH = PROJECT_ROOT / "orig" / "373307D9" / "ham_xbox_r.map"
REPORT_PATH = PROJECT_ROOT / "build" / "373307D9" / "report.json"

# A funclet counts as "paired" in report.json once objdiff matches it past
# this normalized %. Below it (or absent) means pairing failed for it.
FUNCLET_PAIRED_THRESHOLD = 99.0
# Warn if more than this fraction of funclets present in report.json are
# unpaired — signals an objdiff funclet-pairing regression.
FUNCLET_UNPAIRED_WARN_FRAC = 0.05


def _load_funclet_addrs(map_path: Path = MAP_PATH) -> set[str]:
    """Return lowercase 8-hex addresses of __unwind$ / __catch$ funclets in
    the original linker map. Empty set if the map is missing — filter just
    becomes a no-op so callers don't have to special-case it."""
    if not map_path.exists():
        return set()
    addrs: set[str] = set()
    addr_re = re.compile(r"\s([0-9a-f]{8})\s")
    with open(map_path) as f:
        for line in f:
            if "__unwind$" in line or "__catch$" in line:
                m = addr_re.search(line)
                if m:
                    addrs.add(m.group(1).lower())
    return addrs


def _funclet_filter_sql(funclet_addrs: set[str]) -> str:
    """SQL fragment for `WHERE ... AND <here>` that excludes any function
    whose symbol is `fn_<hexaddr>` and whose address is a funclet.

    Inlined-literal join (not a parameter list) because sqlite3 hard-caps
    bound-parameter count below the ~22k funclet count."""
    if not funclet_addrs:
        return "1=1"
    # quoted lowercase 'fn_<addr>' literals
    quoted = ",".join(f"'fn_{a}'" for a in sorted(funclet_addrs))
    return f"LOWER(symbol) NOT IN ({quoted})"


def _funclet_pairing_health(funclet_addrs: set[str]):
    """Cross-check report.json: of the funclet `fn_<addr>` symbols objdiff
    enumerates, how many pair to >= FUNCLET_PAIRED_THRESHOLD. Returns
    (present, paired, unpaired_samples) or None if report.json is absent."""
    import json
    if not REPORT_PATH.exists() or not funclet_addrs:
        return None
    try:
        report = json.loads(REPORT_PATH.read_text())
    except (OSError, ValueError):
        return None
    present = paired = 0
    unpaired_samples: list[str] = []
    for unit in report.get("units", []):
        for fn in unit.get("functions", []):
            name = fn.get("name", "")
            if not name.startswith("fn_"):
                continue
            if name[3:].lower() not in funclet_addrs:
                continue
            present += 1
            pct = fn.get("match_percent_normalized")
            if pct is not None and pct >= FUNCLET_PAIRED_THRESHOLD:
                paired += 1
            elif len(unpaired_samples) < 10:
                unpaired_samples.append(f"{name} ({pct}%)")
    return present, paired, unpaired_samples


def get_progress() -> str:
    conn = get_connection(DB_PATH)

    stats = get_stats(DB_PATH)
    total = stats["total_functions"]

    excluded = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE unit LIKE '%xdk%'"
    ).fetchone()[0]
    non_excluded = total - excluded

    # Count only non-xdk verdicts to avoid inflating Done when xdk functions
    # accidentally get classified
    complete = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE verdict = 'COMPLETE' AND unit NOT LIKE '%xdk%'"
    ).fetchone()[0]
    at_limit = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE verdict = 'AT_LIMIT' AND unit NOT LIKE '%xdk%'"
    ).fetchone()[0]
    remaining = non_excluded - complete - at_limit

    # Funclet bookkeeping: fn_<addr> stubs that are __unwind$/__catch$ aren't
    # source-authorable. Subtract them so "Remaining" reflects real work.
    funclet_addrs = _load_funclet_addrs()
    funclet_filter = _funclet_filter_sql(funclet_addrs)
    funclet_remaining = conn.execute(
        f"SELECT COUNT(*) FROM functions WHERE verdict IS NULL "
        f"AND unit NOT LIKE '%xdk%' AND symbol LIKE 'fn_%' "
        f"AND NOT ({funclet_filter})"
    ).fetchone()[0]
    remaining_real = remaining - funclet_remaining
    real_surface = non_excluded - funclet_remaining  # base for the "real" %

    complete_pct = (complete / non_excluded * 100) if non_excluded else 0
    done_pct = ((complete + at_limit) / non_excluded * 100) if non_excluded else 0
    real_done_pct = ((complete + at_limit) / real_surface * 100) if real_surface else 0

    output = "## Decomp Progress\n\n"
    output += "| Metric | Count | % of non-excluded |\n"
    output += "|--------|------:|---:|\n"
    output += f"| Total functions | {total:,} | - |\n"
    output += f"| Excluded (SDK) | {excluded:,} | - |\n"
    output += f"| Non-excluded | {non_excluded:,} | 100% |\n"
    output += f"| COMPLETE | {complete:,} | {complete_pct:.1f}% |\n"
    output += f"| AT_LIMIT | {at_limit:,} | {at_limit / non_excluded * 100:.1f}% |\n"
    output += f"| Remaining (raw) | {remaining:,} | {remaining / non_excluded * 100:.1f}% |\n"
    output += f"| EH funclets (`__unwind$` / `__catch$`) — build artifact | {funclet_remaining:,} | — |\n"
    output += f"| **Remaining (real, post-funclet)** | **{remaining_real:,}** | — |\n"
    output += f"| Done (COMPLETE + AT_LIMIT), raw non-excluded | {complete + at_limit:,} | {done_pct:.1f}% |\n"
    output += f"| **Done (COMPLETE + AT_LIMIT), real surface** | **{complete + at_limit:,}** | **{real_done_pct:.1f}%** |\n"

    # Sanity check: confirm objdiff (v4.2.0+) is still pairing the excluded
    # funclets in report.json. If pairing regressed, the exclusion above is
    # hiding real unmatched work — surface it loudly.
    health = _funclet_pairing_health(funclet_addrs)
    if health is not None:
        present, paired, unpaired_samples = health
        if present:
            frac_unpaired = (present - paired) / present
            pct_paired = paired / present * 100
            output += (
                f"\n_EH funclet pairing (objdiff): {paired:,}/{present:,} "
                f"({pct_paired:.1f}%) matched in report.json._\n"
            )
            if frac_unpaired > FUNCLET_UNPAIRED_WARN_FRAC:
                output += (
                    f"\n> ⚠️ **{frac_unpaired * 100:.1f}% of funclets are unpaired** "
                    f"(threshold {FUNCLET_UNPAIRED_WARN_FRAC * 100:.0f}%). objdiff "
                    f"funclet pairing may have regressed — the funclet exclusion is "
                    f"masking real unmatched work. Samples: {', '.join(unpaired_samples)}\n"
                )

    # Pattern counts
    pattern_keys = [
        ("pattern_merged", "Linker merged"),
        ("pattern_bool_mask", "Bool mask"),
        ("pattern_makestring_mismatch", "MakeString mismatch"),
        ("pattern_address_relocation", "Address relocation"),
        ("pattern_boolean_negation", "Boolean negation"),
        ("pattern_float_precision", "Float precision"),
        ("pattern_fsel_ternary", "fsel ternary"),
        ("pattern_float_to_int_to_float", "Float-int-float"),
        ("pattern_register_swap", "Register swap"),
        ("pattern_comparison_style", "Comparison style"),
        ("pattern_control_flow", "Control flow"),
        ("pattern_commutative_op_order", "Commutative op order"),
        ("pattern_offset_swap", "Offset swap"),
        ("pattern_anonymous_namespace_hash", "Anon namespace hash"),
        ("pattern_static_guard_counter", "Static guard counter"),
        ("pattern_dynamic_cast_mismatch", "dynamic_cast mismatch"),
        ("pattern_dead_store_elimination", "Dead store elimination"),
        ("pattern_prologue_mismatch", "Prologue mismatch"),
        ("pattern_alloca_mismatch", "alloca mismatch"),
        ("pattern_scope_counter_mismatch", "Scope counter mismatch"),
    ]
    has_patterns = any(stats.get(k, 0) > 0 for k, _ in pattern_keys)
    if has_patterns:
        output += "\n### Detected Patterns\n\n"
        output += "| Pattern | Count |\n"
        output += "|---------|------:|\n"
        for key, label in pattern_keys:
            count = stats.get(key, 0)
            if count > 0:
                output += f"| {label} | {count:,} |\n"

    # Top units with remaining work (real surface: exclude EH funclets)
    rows = conn.execute(f"""
        SELECT unit, COUNT(*) as cnt
        FROM functions
        WHERE verdict IS NULL
          AND unit NOT LIKE '%xdk%'
          AND symbol NOT LIKE 'merged_%'
          AND demangled NOT LIKE '%stlpmtx_std::%'
          AND ({funclet_filter})
        GROUP BY unit
        ORDER BY cnt DESC
        LIMIT 15
    """).fetchall()

    if rows:
        output += "\n### Top Units with Remaining Work\n\n"
        output += "| Unit | Remaining |\n"
        output += "|------|----------:|\n"
        for row in rows:
            unit = row["unit"].replace("default/", "")
            output += f"| {unit} | {row['cnt']} |\n"

    return output


if __name__ == "__main__":
    print(get_progress())
