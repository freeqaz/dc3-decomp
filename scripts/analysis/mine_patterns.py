#!/usr/bin/env python3
"""
Mine commit history for decomp patterns by diffing cached baseline reports.

Walks consecutive pairs of cached baseline reports, identifies functions that
improved, extracts the git diff for each, and classifies which source-level
patterns were applied.

Usage:
    python3 scripts/analysis/mine_patterns.py
    python3 scripts/analysis/mine_patterns.py --json
    python3 scripts/analysis/mine_patterns.py --unclassified   # Show only unknown patterns
    python3 scripts/analysis/mine_patterns.py --summary        # Pattern frequency summary
    python3 scripts/analysis/mine_patterns.py --validate       # Compare against known ROI data
    python3 scripts/analysis/mine_patterns.py --build-baselines 20  # Build baselines for last N commits
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field, asdict
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
BASELINES_DIR = REPO_ROOT / "build" / "373307D9" / "baselines"
REPORT_REL = "build/373307D9/report.json"


# ---------------------------------------------------------------------------
# Data types
# ---------------------------------------------------------------------------

@dataclass
class FunctionDelta:
    """A function whose match% changed between two baselines."""
    symbol: str
    demangled: str
    unit: str
    old_pct: float
    new_pct: float
    delta: float
    size: int


@dataclass
class PatternMatch:
    """A classified pattern found in a diff hunk."""
    pattern: str
    confidence: float  # 0.0-1.0
    evidence: str  # Short description of what triggered the match
    line: str  # The actual diff line(s) that matched


@dataclass
class ClassifiedImprovement:
    """A function improvement with classified patterns."""
    commit_from: str
    commit_to: str
    symbol: str
    demangled: str
    unit: str
    old_pct: float
    new_pct: float
    delta: float
    size: int
    patterns: list[PatternMatch] = field(default_factory=list)
    diff_text: str = ""
    source_file: str = ""


# ---------------------------------------------------------------------------
# Baseline management
# ---------------------------------------------------------------------------

def get_cached_baselines() -> list[tuple[str, Path]]:
    """Return (commit_hash, path) pairs sorted chronologically."""
    if not BASELINES_DIR.exists():
        return []

    baselines = []
    for f in BASELINES_DIR.glob("*.json"):
        commit_hash = f.stem
        # Get commit timestamp for sorting
        try:
            result = subprocess.run(
                ["git", "-C", str(REPO_ROOT), "log", "-1", "--format=%ct", commit_hash],
                capture_output=True, text=True, timeout=5
            )
            if result.returncode == 0:
                ts = int(result.stdout.strip())
                baselines.append((commit_hash, f, ts))
        except (subprocess.TimeoutExpired, ValueError):
            continue

    baselines.sort(key=lambda x: x[2])
    return [(h, p) for h, p, _ in baselines]


def build_baselines_for_recent_commits(count: int = 20) -> None:
    """Build baseline reports for recent commits that touch src/."""
    # Get recent commits touching src/
    result = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "log", "--format=%H", f"-{count}",
         "--no-merges", "--", "src/"],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"Error getting git log: {result.stderr}", file=sys.stderr)
        return

    commits = result.stdout.strip().split("\n")
    existing = {f.stem for f in BASELINES_DIR.glob("*.json")} if BASELINES_DIR.exists() else set()
    missing = [c for c in commits if c not in existing]

    if not missing:
        print(f"All {count} recent commits already have cached baselines.")
        return

    print(f"Need to build {len(missing)} baselines (have {len(existing)}, need {len(commits)})...")
    for i, commit in enumerate(missing):
        short = commit[:10]
        print(f"\n[{i+1}/{len(missing)}] Building baseline for {short}...")
        result = subprocess.run(
            ["bash", str(REPO_ROOT / "scripts" / "measure_progress.sh"),
             "--functions", commit],
            capture_output=True, text=True, cwd=str(REPO_ROOT),
            timeout=600  # 10 min max per build
        )
        if result.returncode != 0:
            print(f"  FAILED: {result.stderr[:200]}", file=sys.stderr)
        else:
            print(f"  OK")


# ---------------------------------------------------------------------------
# Report comparison (reuses compare_progress.py logic inline)
# ---------------------------------------------------------------------------

def load_report(path: Path) -> dict:
    with open(path) as f:
        return json.load(f)


def compare_functions(baseline: dict, current: dict, min_diff: float = 0.5) -> list[FunctionDelta]:
    """Compare function match percentages between two reports."""
    def build_func_map(report):
        fmap = {}
        for unit in report.get("units", []):
            unit_name = unit["name"]
            for func in unit.get("functions", []):
                fname = func.get("name", "")
                pct = func.get("fuzzy_match_percent", None)
                demangled = func.get("metadata", {}).get("demangled_name", "")
                fmap[(unit_name, fname)] = {
                    "pct": pct,
                    "size": int(func.get("size", 0)),
                    "demangled": demangled,
                }
        return fmap

    base_funcs = build_func_map(baseline)
    curr_funcs = build_func_map(current)

    results = []
    for key, curr in curr_funcs.items():
        if key in base_funcs:
            base = base_funcs[key]
            if base["pct"] is None and curr["pct"] is None:
                continue
            base_pct = base["pct"] or 0
            curr_pct = curr["pct"] or 0
            diff = curr_pct - base_pct

            if diff >= min_diff:  # Only improvements
                unit_name, func_name = key
                display = curr["demangled"] or base["demangled"] or func_name
                results.append(FunctionDelta(
                    symbol=func_name,
                    demangled=display,
                    unit=unit_name,
                    old_pct=base_pct,
                    new_pct=curr_pct,
                    delta=diff,
                    size=curr["size"],
                ))

    results.sort(key=lambda x: x.delta, reverse=True)
    return results


# ---------------------------------------------------------------------------
# Unit name -> source file path resolution
# ---------------------------------------------------------------------------

def resolve_source_file(unit_name: str) -> Path | None:
    """Map a unit name like 'default/system/obj/Object' to its source path."""
    # Strip 'default/' prefix if present
    name = unit_name
    if name.startswith("default/"):
        name = name[len("default/"):]

    # Try common extensions
    for ext in [".cpp", ".c"]:
        path = REPO_ROOT / "src" / (name + ext)
        if path.exists():
            return path

    return None


# ---------------------------------------------------------------------------
# Git diff extraction
# ---------------------------------------------------------------------------

def get_source_diff(commit_from: str, commit_to: str, source_file: Path) -> str:
    """Get the git diff for a specific source file between two commits."""
    try:
        rel_path = source_file.relative_to(REPO_ROOT)
    except ValueError:
        rel_path = source_file

    result = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "diff", commit_from, commit_to, "--", str(rel_path)],
        capture_output=True, text=True, timeout=10
    )
    if result.returncode == 0:
        return result.stdout
    return ""


def split_diff_into_hunks(file_diff: str) -> list[dict]:
    """Split a per-file unified diff into individual hunks with line ranges.

    Returns list of {start_line, end_line, text, added, removed} dicts.
    Line ranges refer to the NEW file (+) side.
    """
    hunks = []
    current_hunk = None

    for line in file_diff.split('\n'):
        hunk_match = re.match(r'^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@', line)
        if hunk_match:
            if current_hunk:
                hunks.append(current_hunk)
            start = int(hunk_match.group(1))
            count = int(hunk_match.group(2)) if hunk_match.group(2) else 1
            current_hunk = {
                'start_line': start,
                'end_line': start + count,
                'text': line + '\n',
                'added': [],
                'removed': [],
            }
        elif current_hunk is not None:
            current_hunk['text'] += line + '\n'
            if line.startswith('+') and not line.startswith('+++'):
                current_hunk['added'].append(line[1:])
            elif line.startswith('-') and not line.startswith('---'):
                current_hunk['removed'].append(line[1:])

    if current_hunk:
        hunks.append(current_hunk)

    return hunks


# ---------------------------------------------------------------------------
# Pattern classifier — the core innovation
# ---------------------------------------------------------------------------

# Each pattern is: (name, list_of_classifiers)
# A classifier is a function(added_lines, removed_lines, diff_text) -> (confidence, evidence) or None

def _classify_variable_extraction(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect variable extraction: new auto/type local assigned from nested call."""
    patterns = [
        r'\+\s*(auto|int|float|bool|unsigned|Symbol|String|DataNode|DataArray\s*\*)\s+_?tmp\d*\s*=',
        r'\+\s*auto\s+_tmp\d+\s*=',
        r'\+\s*auto\s+\w+\s*=\s*\w+->[\w()]+',
    ]
    for pat in patterns:
        m = re.search(pat, diff)
        if m:
            return (0.9, f"New temp variable: {m.group(0).strip()[:60]}")
    return None


def _classify_member_ref_bind(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect member reference binding: auto& _ref = mMember."""
    patterns = [
        r'\+\s*auto\s*&\s*_ref\d*\s*=\s*m\w+',
        r'\+\s*auto\s*&\s*_ref\d*\s*=\s*\w+',
    ]
    for pat in patterns:
        m = re.search(pat, diff)
        if m:
            return (0.95, f"Member ref bind: {m.group(0).strip()[:60]}")
    return None


def _classify_signed_unsigned(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect signed/unsigned cast changes."""
    cast_patterns = [
        (r'\+.*\(unsigned\s*(int|char|short)\)', "Added unsigned cast"),
        (r'\+.*\(int\)\s*\w+', "Added int cast"),
        (r'-.*\(unsigned\).*\+.*[^(unsigned)]', "Removed unsigned cast"),
        (r'\+.*\(unsigned\)', "Added unsigned cast"),
    ]
    for pat, desc in cast_patterns:
        m = re.search(pat, diff)
        if m:
            return (0.8, f"{desc}: {m.group(0).strip()[:60]}")
    return None


def _classify_float_double(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect float/double literal changes (0.001 -> 0.001f or vice versa)."""
    # Added 'f' suffix
    if re.search(r'-.*\b\d+\.\d+\b(?!f).*\n\+.*\b\d+\.\d+f\b', diff):
        return (0.95, "Added float suffix (double->float)")
    # Removed 'f' suffix
    if re.search(r'-.*\b\d+\.\d+f\b.*\n\+.*\b\d+\.\d+\b(?!f)', diff):
        return (0.95, "Removed float suffix (float->double)")
    # Changed int to float literal (0 -> 0.0f)
    if re.search(r'-.*\bHmx::Color\(\s*0\s*,.*\n\+.*\bHmx::Color\(\s*0\.0f\s*,', diff):
        return (0.85, "Int to float literal in Color ctor")
    return None


def _classify_ternary_swap(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect ternary <-> if/else conversion."""
    # if/else converted to ternary
    has_removed_if = any(re.search(r'if\s*\(', l) for l in removed)
    has_added_ternary = any('?' in l and ':' in l for l in added)
    if has_removed_if and has_added_ternary:
        return (0.7, "if/else -> ternary conversion")

    # ternary converted to if/else
    has_removed_ternary = any('?' in l and ':' in l for l in removed)
    has_added_if = any(re.search(r'if\s*\(', l) for l in added)
    if has_removed_ternary and has_added_if:
        return (0.7, "ternary -> if/else conversion")
    return None


def _classify_comparison_flip(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect comparison operand flip (a < b -> b > a)."""
    # Look for paired remove/add where comparison direction changes
    cmp_ops = {'<': '>', '>': '<', '<=': '>=', '>=': '<='}
    for rem in removed:
        for op, flip in cmp_ops.items():
            if op in rem:
                for add in added:
                    if flip in add:
                        # Check if it looks like the same comparison flipped
                        return (0.5, f"Comparison direction change: {op} -> {flip}")
    return None


def _classify_null_guard(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect null guard insertion/removal."""
    for line in added:
        if re.search(r'if\s*\(\w+\)\s*$', line.strip()):
            return (0.85, f"Null guard added: {line.strip()[:60]}")
        if re.search(r'if\s*\(\w+\)\s*\{?\s*$', line.strip()):
            return (0.75, f"Null guard added: {line.strip()[:60]}")
    return None


def _classify_declaration_reorder(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect variable/statement declaration reordering."""
    # Look for matching removed+added lines that are just swapped
    # Static symbol reorder
    static_removed = [l for l in removed if re.search(r'static\s+Symbol\s+\w+\(', l)]
    static_added = [l for l in added if re.search(r'static\s+Symbol\s+\w+\(', l)]
    if len(static_removed) >= 2 and len(static_added) >= 2:
        return (0.9, f"Static symbol declaration reorder ({len(static_removed)} symbols)")

    # Field assignment reorder
    field_removed = [l for l in removed if re.search(r'\w+\.m\w+\s*=', l) or re.search(r'score\.\w+\s*=', l)]
    field_added = [l for l in added if re.search(r'\w+\.m\w+\s*=', l) or re.search(r'score\.\w+\s*=', l)]
    if len(field_removed) >= 2 and len(field_added) >= 2:
        # Check if it's truly reordered (same lines, different order)
        rem_set = {l.strip() for l in field_removed}
        add_set = {l.strip() for l in field_added}
        if rem_set == add_set:
            return (0.9, f"Field assignment reorder ({len(field_removed)} fields)")
        # Partial overlap also suggests reorder
        overlap = rem_set & add_set
        if len(overlap) >= 2:
            return (0.7, f"Partial field assignment reorder ({len(overlap)} shared)")

    # Variable declaration reorder
    decl_removed = [l for l in removed if re.search(r'(int|float|bool|auto|Symbol|String)\s+\w+', l)]
    decl_added = [l for l in added if re.search(r'(int|float|bool|auto|Symbol|String)\s+\w+', l)]
    if len(decl_removed) >= 2 and len(decl_added) >= 2:
        rem_set = {l.strip() for l in decl_removed}
        add_set = {l.strip() for l in decl_added}
        if rem_set == add_set:
            return (0.85, f"Variable declaration reorder ({len(decl_removed)} decls)")
    return None


def _classify_branch_polarity(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect if/else branch inversion."""
    # if(cond) { A } else { B } -> if(!cond) { B } else { A }
    added_negation = any(re.search(r'if\s*\(\s*!', l) for l in added)
    removed_plain = any(re.search(r'if\s*\(\s*[^!]', l) for l in removed)
    if added_negation and removed_plain:
        return (0.6, "Branch polarity: added negation to condition")

    removed_negation = any(re.search(r'if\s*\(\s*!', l) for l in removed)
    added_plain = any(re.search(r'if\s*\(\s*[^!]', l) for l in added)
    if removed_negation and added_plain:
        return (0.6, "Branch polarity: removed negation from condition")
    return None


def _classify_bool_cast(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect bool cast addition/removal."""
    if re.search(r'\+.*\(bool\)\s*\(', diff):
        return (0.85, "Added (bool) cast")
    if re.search(r'\+.*\(bool\)\w', diff):
        return (0.85, "Added (bool) cast")
    if re.search(r'-.*\(bool\)', diff):
        return (0.8, "Removed (bool) cast")
    return None


def _classify_and_split(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect && split into nested ifs."""
    removed_and = any('&&' in l for l in removed)
    added_nested = sum(1 for l in added if re.search(r'if\s*\(', l))
    if removed_and and added_nested >= 2:
        return (0.7, "&& split into nested ifs")
    return None


def _classify_early_return_merge(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect early return merge (multiple returns -> || chain)."""
    removed_returns = sum(1 for l in removed if 'return' in l)
    added_or = any('||' in l for l in added)
    if removed_returns >= 2 and added_or:
        return (0.7, f"Early return merge: {removed_returns} returns -> || chain")
    return None


def _classify_empty_size_swap(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect .empty() <-> .size() == 0 swap."""
    if re.search(r'-.*\.empty\(\).*\n\+.*\.size\(\)', diff):
        return (0.9, ".empty() -> .size()")
    if re.search(r'-.*\.size\(\).*\n\+.*\.empty\(\)', diff):
        return (0.9, ".size() -> .empty()")
    return None


def _classify_symbol_inline(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect Symbol variable elimination (inline string literal)."""
    removed_sym = any(re.search(r'Symbol\s+\w+\(', l) for l in removed)
    if removed_sym:
        # Check if a string literal is now used inline instead
        added_inline = any(re.search(r'Find\w*\(\s*"', l) or re.search(r'\(\s*\(?"', l) for l in added)
        if added_inline:
            return (0.8, "Symbol variable inlined to string literal")
    return None


def _classify_conditional_split(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect splitting a combined condition (if (a && b) -> if (a) { if (b) })."""
    removed_combined = any(re.search(r'if\s*\(.*&&', l) for l in removed)
    added_nested_ifs = sum(1 for l in added if re.search(r'if\s*\(', l))
    if removed_combined and added_nested_ifs >= 2:
        return (0.75, "Combined condition split into nested ifs")
    return None


def _classify_fma_reorder(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect FMA expression reordering."""
    # Look for arithmetic expression changes involving * and +
    for rem, add in zip(removed, added):
        if '*' in rem and '+' in rem and '*' in add and '+' in add:
            if rem.strip().rstrip(';') != add.strip().rstrip(';'):
                return (0.5, "Arithmetic expression reorder (possible FMA)")
    return None


def _classify_int_cast(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect (int) cast added to float comparison or sizeof."""
    if re.search(r'\+.*\(int\)\s*(m\w+|sizeof)', diff):
        return (0.8, "Added (int) cast")
    if re.search(r'\+.*\(int\)\s*\w+', diff):
        return (0.6, "Added (int) cast")
    return None


def _classify_temp_elimination(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect temporary variable elimination (inlining a single-use local)."""
    # Removed a local variable declaration, and its usage is now inlined
    removed_local = [l for l in removed if re.search(r'(auto|int|float|Symbol|String|DataArray\s*\*)\s+\w+\s*=', l)]
    if removed_local and len(added) < len(removed):
        return (0.6, f"Temp elimination: removed {len(removed_local)} local(s)")
    return None


def _classify_scope_change(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect scope/brace changes affecting variable lifetime."""
    added_braces = sum(1 for l in added if l.strip() in ['{', '}'])
    removed_braces = sum(1 for l in removed if l.strip() in ['{', '}'])
    if abs(added_braces - removed_braces) >= 2:
        return (0.5, f"Scope change: {removed_braces} -> {added_braces} braces")
    return None


def _classify_statement_reorder(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect generic statement reordering (same statements, different order)."""
    if len(removed) < 2 or len(added) < 2:
        return None
    rem_set = {l.strip() for l in removed if l.strip() and not l.strip().startswith('//')}
    add_set = {l.strip() for l in added if l.strip() and not l.strip().startswith('//')}
    overlap = rem_set & add_set
    if len(overlap) >= 2 and len(overlap) / max(len(rem_set), len(add_set)) > 0.6:
        return (0.6, f"Statement reorder ({len(overlap)} statements shuffled)")
    return None


def _classify_noinline_or_pragma(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect __declspec(noinline), #pragma, or attribute additions."""
    if re.search(r'\+.*__declspec\(noinline\)', diff):
        return (0.95, "Added __declspec(noinline)")
    if re.search(r'\+.*#pragma\s+fp_contract', diff):
        return (0.95, "Added #pragma fp_contract")
    if re.search(r'\+.*__declspec\(noreturn\)', diff):
        return (0.95, "Added __declspec(noreturn)")
    return None


def _classify_milo_macro(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect MILO macro changes (WARN/NOTIFY/ASSERT)."""
    for macro_type in ['MILO_WARN', 'MILO_NOTIFY', 'MILO_ASSERT', 'MILO_FAIL', 'MILO_LOG']:
        if any(macro_type in l for l in removed) != any(macro_type in l for l in added):
            return (0.7, f"MILO macro change involving {macro_type}")
    # MILO_NOTIFY -> MILO_NOTIFY_ONCE
    if re.search(r'-.*MILO_NOTIFY\b.*\n\+.*MILO_NOTIFY_ONCE', diff):
        return (0.9, "MILO_NOTIFY -> MILO_NOTIFY_ONCE")
    return None


def _classify_assert_line_fix(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect MILO_ASSERT line number changes."""
    rem_asserts = [re.search(r'MILO_ASSERT\(.*,\s*(\d+|0x[\da-fA-F]+)\)', l) for l in removed]
    add_asserts = [re.search(r'MILO_ASSERT\(.*,\s*(\d+|0x[\da-fA-F]+)\)', l) for l in added]
    rem_asserts = [m for m in rem_asserts if m]
    add_asserts = [m for m in add_asserts if m]
    if rem_asserts and add_asserts:
        # Check if expression is the same but line number differs
        return (0.7, "MILO_ASSERT line number adjustment")
    return None


def _classify_native_guard(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect #ifdef HX_NATIVE guard additions (not a PPC pattern)."""
    if re.search(r'\+.*#ifdef\s+HX_NATIVE', diff):
        return (0.5, "Added #ifdef HX_NATIVE guard (native-only, not PPC pattern)")
    return None


def _classify_header_include_change(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect header include additions/removals that affect inlining."""
    for line in removed:
        if re.search(r'#include\s+"', line):
            return (0.8, f"Removed #include: {line.strip()[:60]}")
    for line in added:
        if re.search(r'#include\s+"', line):
            return (0.7, f"Added #include: {line.strip()[:60]}")
    return None


def _classify_body_removal(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect function body removal/stubbing (affects TU inlining budget)."""
    # Look for destructor or function body being removed
    for line in removed:
        if re.search(r'~\w+\(\)\s*\{', line) or re.search(r'~\w+\(\)\s*;', line):
            return (0.85, f"Destructor body removed: {line.strip()[:60]}")
    # Removing extern declarations
    for line in removed:
        if re.search(r'extern\s+(bool|int|float|char)', line):
            return (0.7, f"Extern declaration removed: {line.strip()[:60]}")
    return None


def _classify_iterator_to_index(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect iterator comparison changed to index-based comparison."""
    if any('- vector.begin()' in l or '- vector.begin())' in l for l in added):
        return (0.95, "Iterator to index-based comparison (it - begin())")
    # FOREACH macro expansion
    if any(re.search(r'FOREACH\s*\(', l) for l in removed):
        has_for = any(re.search(r'for\s*\(auto', l) for l in added)
        if has_for:
            return (0.9, "FOREACH macro -> explicit for loop")
    return None


def _classify_field_rename(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect field/variable renaming (unk -> meaningful name, or wrong field)."""
    for rem in removed:
        if re.search(r'\bunk[\da-fA-F]+\b', rem):
            return (0.7, f"Field renamed from unk: {rem.strip()[:60]}")
    # Detect wrong field access fix (e.g., mForce -> mForceMips)
    rem_fields = set()
    add_fields = set()
    for rem in removed:
        for m in re.finditer(r'\bm[A-Z]\w+\b', rem):
            rem_fields.add(m.group(0))
    for add in added:
        for m in re.finditer(r'\bm[A-Z]\w+\b', add):
            add_fields.add(m.group(0))
    only_removed = rem_fields - add_fields
    only_added = add_fields - rem_fields
    if only_removed and only_added and len(only_removed) <= 2 and len(only_added) <= 2:
        return (0.7, f"Field access fix: {only_removed} -> {only_added}")
    return None


def _classify_single_return(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect multiple returns merged into single return pattern."""
    rem_returns = sum(1 for l in removed if re.search(r'\breturn\b', l))
    add_returns = sum(1 for l in added if re.search(r'\breturn\b', l))
    if rem_returns >= 2 and add_returns == 1:
        return (0.75, f"Multiple returns ({rem_returns}) -> single return")
    return None


def _classify_condition_rewrite(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect condition logic rewrite (De Morgan, negation distribution, etc.)."""
    # != 0 -> > 0 or vice versa
    if re.search(r'-.*!=\s*0.*\n\+.*>\s*0', diff) or re.search(r'-.*>\s*0.*\n\+.*!=\s*0', diff):
        return (0.85, "Comparison rewrite: != 0 <-> > 0")
    # == 0 -> !expr
    if re.search(r'-.*==\s*0.*\n\+.*!', diff):
        return (0.7, "Comparison rewrite: == 0 -> negation")
    # Condition simplification (removed double negation, DeMorgan)
    rem_logic = sum(1 for l in removed if '&&' in l or '||' in l or '!' in l)
    add_logic = sum(1 for l in added if '&&' in l or '||' in l or '!' in l)
    if rem_logic > 0 and add_logic > 0 and rem_logic != add_logic:
        return (0.5, "Logic condition restructured")
    return None


def _classify_struct_type_fix(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect struct/type definition changes (typedef, struct layout)."""
    for line in removed:
        if re.search(r'typedef\s+struct', line):
            return (0.8, f"Removed typedef struct: {line.strip()[:60]}")
    for line in added:
        if re.search(r'WIN32_FILE_ATTRIBUTE_DATA|FILETIME|DWORD', line):
            return (0.85, "Struct type fix (Win32 API types)")
    return None


def _classify_milo_fail_simplify(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect MILO_FAIL argument simplification."""
    rem_fail = [l for l in removed if 'MILO_FAIL' in l]
    add_fail = [l for l in added if 'MILO_FAIL' in l]
    if rem_fail and add_fail:
        # Check if format string was simplified
        rem_len = sum(len(l) for l in rem_fail)
        add_len = sum(len(l) for l in add_fail)
        if rem_len > add_len * 1.3:
            return (0.8, "MILO_FAIL argument simplified (shorter format)")
    return None


def _classify_member_access_extraction(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect extraction of repeated member access into local (m[i][i] -> mii)."""
    # Look for array indexing extracted to local
    for line in added:
        if re.match(r'\s*(float|int|auto)\s+\w+\s*=\s*m\[', line):
            return (0.85, f"Member access extracted to local: {line.strip()[:60]}")
    return None


def _classify_push_back_move(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect push_back statement moved to different location."""
    rem_pb = [l.strip() for l in removed if 'push_back' in l]
    add_pb = [l.strip() for l in added if 'push_back' in l]
    if rem_pb and add_pb:
        overlap = set(rem_pb) & set(add_pb)
        if overlap:
            return (0.8, f"push_back statement relocated: {list(overlap)[0][:50]}")
    return None


def _classify_default_value_fix(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect default value fixes (true->false, wrong init values)."""
    for rem, add in zip(removed, added):
        # true -> false or vice versa
        if 'true' in rem and 'false' in add and rem.replace('true', 'false').strip() == add.strip():
            return (0.9, f"Default value fix: true -> false")
        if 'false' in rem and 'true' in add and rem.replace('false', 'true').strip() == add.strip():
            return (0.9, f"Default value fix: false -> true")
    return None


def _classify_accessor_change(added: list[str], removed: list[str], diff: str) -> tuple[float, str] | None:
    """Detect .first/.second accessor or method call changes."""
    for rem in removed:
        for add in added:
            if re.search(r'->songID', rem) and re.search(r'\.first->songID', add):
                return (0.85, "Accessor change: direct -> .first->")
    return None


# Master classifier list
CLASSIFIERS = [
    ("variable_extraction", _classify_variable_extraction),
    ("member_ref_bind", _classify_member_ref_bind),
    ("signed_unsigned", _classify_signed_unsigned),
    ("float_double", _classify_float_double),
    ("ternary_swap", _classify_ternary_swap),
    ("comparison_flip", _classify_comparison_flip),
    ("null_guard_insert", _classify_null_guard),
    ("declaration_reorder", _classify_declaration_reorder),
    ("branch_polarity", _classify_branch_polarity),
    ("bool_cast", _classify_bool_cast),
    ("and_split", _classify_and_split),
    ("early_return_merge", _classify_early_return_merge),
    ("empty_size_swap", _classify_empty_size_swap),
    ("symbol_inline", _classify_symbol_inline),
    ("conditional_split", _classify_conditional_split),
    ("fma_reorder", _classify_fma_reorder),
    ("int_cast", _classify_int_cast),
    ("temp_elimination", _classify_temp_elimination),
    ("scope_change", _classify_scope_change),
    ("statement_reorder", _classify_statement_reorder),
    ("noinline_or_pragma", _classify_noinline_or_pragma),
    ("milo_macro", _classify_milo_macro),
    ("assert_line_fix", _classify_assert_line_fix),
    ("native_guard", _classify_native_guard),
    ("header_include_change", _classify_header_include_change),
    ("body_removal", _classify_body_removal),
    ("iterator_to_index", _classify_iterator_to_index),
    ("field_rename", _classify_field_rename),
    ("single_return", _classify_single_return),
    ("condition_rewrite", _classify_condition_rewrite),
    ("struct_type_fix", _classify_struct_type_fix),
    ("milo_fail_simplify", _classify_milo_fail_simplify),
    ("member_access_extraction", _classify_member_access_extraction),
    ("push_back_move", _classify_push_back_move),
    ("default_value_fix", _classify_default_value_fix),
    ("accessor_change", _classify_accessor_change),
]


def classify_diff(diff_text: str) -> list[PatternMatch]:
    """Classify patterns found in a unified diff."""
    if not diff_text.strip():
        return []

    # Parse diff into added/removed lines
    added = []
    removed = []
    for line in diff_text.split('\n'):
        if line.startswith('+') and not line.startswith('+++'):
            added.append(line[1:])
        elif line.startswith('-') and not line.startswith('---'):
            removed.append(line[1:])

    if not added and not removed:
        return []

    matches = []
    for name, classifier in CLASSIFIERS:
        result = classifier(added, removed, diff_text)
        if result:
            confidence, evidence = result
            # Find the most relevant diff line for this match
            relevant_line = ""
            for line in added[:3]:
                if line.strip():
                    relevant_line = line.strip()[:80]
                    break

            matches.append(PatternMatch(
                pattern=name,
                confidence=confidence,
                evidence=evidence,
                line=relevant_line,
            ))

    return matches


# ---------------------------------------------------------------------------
# Main analysis pipeline
# ---------------------------------------------------------------------------

def _count_file_hunks(file_diff: str) -> int:
    """Count the number of diff hunks in a file diff."""
    return len(re.findall(r'^@@ ', file_diff, re.MULTILINE))


def analyze_baseline_pair(
    commit_from: str, commit_to: str,
    report_from: dict, report_to: dict,
) -> list[ClassifiedImprovement]:
    """Analyze improvements between two baseline reports.

    Uses per-hunk classification: when a file has multiple hunks, each function
    is only associated with patterns found in nearby hunks (by line proximity),
    or all hunks if the file has <=3 hunks (small change, likely all related).
    """
    deltas = compare_functions(report_from, report_to, min_diff=0.5)
    if not deltas:
        return []

    # Group deltas by unit for efficient diff extraction
    unit_deltas: dict[str, list[FunctionDelta]] = defaultdict(list)
    for d in deltas:
        unit_deltas[d.unit].append(d)

    results = []
    for unit, funcs in unit_deltas.items():
        source = resolve_source_file(unit)
        rel_source = str(source.relative_to(REPO_ROOT)) if source else "<not found>"

        if source is None:
            for fd in funcs:
                results.append(ClassifiedImprovement(
                    commit_from=commit_from[:10],
                    commit_to=commit_to[:10],
                    symbol=fd.symbol, demangled=fd.demangled,
                    unit=unit, old_pct=fd.old_pct, new_pct=fd.new_pct,
                    delta=fd.delta, size=fd.size, source_file=rel_source,
                ))
            continue

        file_diff = get_source_diff(commit_from, commit_to, source)
        if not file_diff:
            for fd in funcs:
                results.append(ClassifiedImprovement(
                    commit_from=commit_from[:10],
                    commit_to=commit_to[:10],
                    symbol=fd.symbol, demangled=fd.demangled,
                    unit=unit, old_pct=fd.old_pct, new_pct=fd.new_pct,
                    delta=fd.delta, size=fd.size, source_file=rel_source,
                ))
            continue

        hunks = split_diff_into_hunks(file_diff)
        num_hunks = len(hunks)

        if num_hunks <= 3:
            # Small change — classify entire file diff and attribute to all
            patterns = classify_diff(file_diff)
            for fd in funcs:
                results.append(ClassifiedImprovement(
                    commit_from=commit_from[:10],
                    commit_to=commit_to[:10],
                    symbol=fd.symbol, demangled=fd.demangled,
                    unit=unit, old_pct=fd.old_pct, new_pct=fd.new_pct,
                    delta=fd.delta, size=fd.size, patterns=patterns,
                    diff_text=file_diff[:2000], source_file=rel_source,
                ))
        else:
            # Large change — classify each hunk independently.
            # Each function gets the union of patterns from ALL hunks,
            # but diff_text only includes the full file diff summary.
            # This is still file-level attribution (we don't know which
            # function each hunk belongs to without parsing the source),
            # but the per-hunk classification avoids false pattern matches
            # from unrelated changes in distant hunks.
            all_patterns: dict[str, PatternMatch] = {}
            for hunk in hunks:
                hunk_patterns = classify_diff(hunk['text'])
                for p in hunk_patterns:
                    # Deduplicate by pattern name, keep highest confidence
                    if p.pattern not in all_patterns or p.confidence > all_patterns[p.pattern].confidence:
                        all_patterns[p.pattern] = p

            patterns = list(all_patterns.values())
            for fd in funcs:
                results.append(ClassifiedImprovement(
                    commit_from=commit_from[:10],
                    commit_to=commit_to[:10],
                    symbol=fd.symbol, demangled=fd.demangled,
                    unit=unit, old_pct=fd.old_pct, new_pct=fd.new_pct,
                    delta=fd.delta, size=fd.size, patterns=patterns,
                    diff_text=file_diff[:2000], source_file=rel_source,
                ))

    results.sort(key=lambda x: x.delta, reverse=True)
    return results


def run_analysis() -> list[ClassifiedImprovement]:
    """Run the full analysis across all cached baseline pairs."""
    baselines = get_cached_baselines()
    if len(baselines) < 2:
        print("Need at least 2 cached baselines. Run with --build-baselines first.",
              file=sys.stderr)
        return []

    all_results = []
    for i in range(len(baselines) - 1):
        commit_from, path_from = baselines[i]
        commit_to, path_to = baselines[i + 1]
        short_from = commit_from[:10]
        short_to = commit_to[:10]

        print(f"Analyzing {short_from} -> {short_to}...", file=sys.stderr)

        report_from = load_report(path_from)
        report_to = load_report(path_to)

        results = analyze_baseline_pair(commit_from, commit_to, report_from, report_to)
        all_results.extend(results)
        print(f"  Found {len(results)} improvements", file=sys.stderr)

    return all_results


# ---------------------------------------------------------------------------
# Output formatting
# ---------------------------------------------------------------------------

def print_detailed(results: list[ClassifiedImprovement], show_unclassified_only: bool = False):
    """Print detailed results."""
    if show_unclassified_only:
        results = [r for r in results if not r.patterns]

    if not results:
        print("No results to display.")
        return

    for r in results:
        pct_str = f"{r.old_pct:.1f}% -> {r.new_pct:.1f}% (+{r.delta:.1f}%)"
        demangled = r.demangled[:70] if len(r.demangled) > 70 else r.demangled
        print(f"\n{'='*78}")
        print(f"  {demangled}")
        print(f"  {r.unit}  |  {pct_str}  |  {r.size} bytes")
        print(f"  {r.commit_from} -> {r.commit_to}  |  {r.source_file}")

        if r.patterns:
            print(f"  Patterns detected:")
            for p in r.patterns:
                conf = f"[{p.confidence:.0%}]"
                print(f"    {conf:>6} {p.pattern}: {p.evidence}")
        else:
            print(f"  ** UNCLASSIFIED ** — potential new pattern")

        # Show a snippet of the diff for unclassified
        if not r.patterns and r.diff_text:
            lines = r.diff_text.split('\n')
            change_lines = [l for l in lines if l.startswith('+') or l.startswith('-')]
            change_lines = [l for l in change_lines if not l.startswith('+++') and not l.startswith('---')]
            if change_lines:
                print(f"  Diff snippet:")
                for l in change_lines[:12]:
                    print(f"    {l}")


def print_summary(results: list[ClassifiedImprovement]):
    """Print pattern frequency summary."""
    pattern_counts: Counter = Counter()
    pattern_deltas: dict[str, list[float]] = defaultdict(list)
    pattern_to_100: dict[str, int] = defaultdict(int)
    unclassified = 0
    total = len(results)

    for r in results:
        if r.patterns:
            for p in r.patterns:
                pattern_counts[p.pattern] += 1
                pattern_deltas[p.pattern].append(r.delta)
                if r.new_pct >= 99.9:
                    pattern_to_100[p.pattern] += 1
        else:
            unclassified += 1

    print(f"\n{'='*70}")
    print(f"  PATTERN FREQUENCY SUMMARY")
    print(f"  {total} function improvements analyzed")
    print(f"  {unclassified} unclassified ({unclassified*100/total:.1f}%)" if total else "")
    print(f"{'='*70}\n")

    headers = ["Pattern", "Count", "Avg Delta", "Max Delta", "To 100%"]
    rows = []
    for pattern, count in pattern_counts.most_common():
        deltas = pattern_deltas[pattern]
        avg_delta = sum(deltas) / len(deltas)
        max_delta = max(deltas)
        to_100 = pattern_to_100.get(pattern, 0)
        rows.append([
            pattern,
            str(count),
            f"+{avg_delta:.1f}%",
            f"+{max_delta:.1f}%",
            str(to_100),
        ])

    if rows:
        widths = [max(len(h), max(len(r[i]) for r in rows)) for i, h in enumerate(headers)]
        header_line = " | ".join(h.ljust(widths[i]) for i, h in enumerate(headers))
        print(header_line)
        print("-+-".join("-" * w for w in widths))
        for row in rows:
            print(" | ".join(row[i].ljust(widths[i]) for i in range(len(headers))))

    # Co-occurrence analysis
    print(f"\n{'='*70}")
    print(f"  PATTERN CO-OCCURRENCE (same file, same commit window)")
    print(f"{'='*70}\n")

    cooccur: Counter = Counter()
    for r in results:
        if len(r.patterns) >= 2:
            names = sorted(set(p.pattern for p in r.patterns))
            for i in range(len(names)):
                for j in range(i + 1, len(names)):
                    cooccur[(names[i], names[j])] += 1

    if cooccur:
        for (p1, p2), count in cooccur.most_common(15):
            print(f"  {p1} + {p2}: {count} co-occurrences")
    else:
        print("  No co-occurrences found (need more data)")


def print_unclassified_diffs(results: list[ClassifiedImprovement]):
    """Print only unclassified improvements with their diffs for manual review."""
    unclassified = [r for r in results if not r.patterns and r.diff_text]

    if not unclassified:
        print("No unclassified improvements found — all patterns recognized!")
        return

    print(f"\n{'='*70}")
    print(f"  UNCLASSIFIED IMPROVEMENTS — POTENTIAL NEW PATTERNS")
    print(f"  {len(unclassified)} improvements with no recognized pattern")
    print(f"{'='*70}")

    # Group by unit to see if patterns cluster by subsystem
    by_unit: dict[str, list[ClassifiedImprovement]] = defaultdict(list)
    for r in unclassified:
        by_unit[r.unit].append(r)

    for unit, items in sorted(by_unit.items(), key=lambda x: -len(x[1])):
        print(f"\n--- {unit} ({len(items)} unclassified) ---")
        for r in items[:5]:  # Limit per unit
            demangled = r.demangled[:60] if len(r.demangled) > 60 else r.demangled
            print(f"\n  {demangled}")
            print(f"  {r.old_pct:.1f}% -> {r.new_pct:.1f}% (+{r.delta:.1f}%)")

            if r.diff_text:
                lines = r.diff_text.split('\n')
                change_lines = [l for l in lines
                                if (l.startswith('+') or l.startswith('-'))
                                and not l.startswith('+++') and not l.startswith('---')]
                for l in change_lines[:10]:
                    print(f"    {l}")


def print_json(results: list[ClassifiedImprovement]):
    """Print results as JSON."""
    out = []
    for r in results:
        d = {
            "commit_from": r.commit_from,
            "commit_to": r.commit_to,
            "symbol": r.symbol,
            "demangled": r.demangled,
            "unit": r.unit,
            "old_pct": r.old_pct,
            "new_pct": r.new_pct,
            "delta": r.delta,
            "size": r.size,
            "source_file": r.source_file,
            "patterns": [
                {"pattern": p.pattern, "confidence": p.confidence, "evidence": p.evidence}
                for p in r.patterns
            ],
        }
        out.append(d)
    print(json.dumps(out, indent=2))


def print_validation(results: list[ClassifiedImprovement]):
    """Validate against known ROI data from PERMUTER_ROI_ANALYSIS.md."""
    # Known win rates from docs
    known_rates = {
        "variable_extraction": 42,
        "signed_unsigned": 30,
        "inline_assignment": 22,
        "declaration_reorder": 20,
        "comparison_flip": 15,
        "branch_polarity": 5,
        "argument_swap": 5,
        "fma_reorder": 2,
        "commutative_swap": 0,
        "empty_size_swap": 0,
        "ternary_swap": 0,
    }

    pattern_counts: Counter = Counter()
    for r in results:
        for p in r.patterns:
            pattern_counts[p.pattern] += 1

    total = len(results)
    print(f"\n{'='*70}")
    print(f"  PATTERN VALIDATION vs PERMUTER ROI DATA")
    print(f"  {total} improvements from commit history")
    print(f"{'='*70}\n")

    headers = ["Pattern", "Commit History", "Permuter Win%", "Notes"]
    rows = []
    for pattern, known_rate in sorted(known_rates.items(), key=lambda x: -x[1]):
        hist_count = pattern_counts.get(pattern, 0)
        hist_pct = f"{hist_count*100/total:.1f}%" if total else "N/A"
        note = ""
        if hist_count > 0 and known_rate == 0:
            note = "!! History shows wins — permuter says 0%"
        elif hist_count == 0 and known_rate > 10:
            note = "No history data — may need more baselines"
        rows.append([pattern, f"{hist_count} ({hist_pct})", f"{known_rate}%", note])

    # Add patterns found in history but not in known_rates
    for pattern, count in pattern_counts.most_common():
        if pattern not in known_rates:
            hist_pct = f"{count*100/total:.1f}%" if total else "N/A"
            rows.append([pattern, f"{count} ({hist_pct})", "N/A", "NEW — not in permuter ROI"])

    widths = [max(len(h), max((len(r[i]) for r in rows), default=0)) for i, h in enumerate(headers)]
    header_line = " | ".join(h.ljust(widths[i]) for i, h in enumerate(headers))
    print(header_line)
    print("-+-".join("-" * w for w in widths))
    for row in rows:
        print(" | ".join(row[i].ljust(widths[i]) for i in range(len(headers))))


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Mine commit history for decomp patterns")
    parser.add_argument("--json", action="store_true", help="Output as JSON")
    parser.add_argument("--summary", action="store_true", help="Pattern frequency summary")
    parser.add_argument("--unclassified", action="store_true", help="Show only unclassified improvements")
    parser.add_argument("--validate", action="store_true", help="Validate against known ROI data")
    parser.add_argument("--build-baselines", type=int, metavar="N",
                        help="Build baseline reports for last N commits first")
    parser.add_argument("--all", action="store_true", help="Show all output modes")
    args = parser.parse_args()

    if args.build_baselines:
        build_baselines_for_recent_commits(args.build_baselines)
        print()

    results = run_analysis()
    if not results:
        print("No improvements found across cached baselines.")
        return

    if args.json:
        print_json(results)
    elif args.all:
        print_summary(results)
        print_validation(results)
        print_unclassified_diffs(results)
    elif args.summary:
        print_summary(results)
    elif args.unclassified:
        print_unclassified_diffs(results)
    elif args.validate:
        print_validation(results)
    else:
        print_detailed(results)


if __name__ == "__main__":
    main()
