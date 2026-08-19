#!/usr/bin/env python3
"""honesty_lint.py — a static check for the "lying by omission" shape.

This is the mechanical half of the scanner-truthfulness contract (the other
half is `coverage.py`, which is a runtime contract). It catches the two
historical defects that CAN be detected by reading source, so that the next
instance is caught by CI instead of by someone noticing a suspicious total
eighteen months later.

RULES (ERROR — these fail the test suite)
-----------------------------------------
E1  unescaped-like
    A SQL ``LIKE 'literal'`` whose literal contains an unescaped ``_`` or ``%``
    and no ``ESCAPE`` clause.  `_` is a SINGLE-CHARACTER WILDCARD in SQL LIKE,
    so ``symbol NOT LIKE '??_%'`` — which reads as "not the ??_ artifacts" —
    actually excludes EVERY '??'-prefixed symbol.  In certify_floor.py that one
    line hid 6,835 functions from every band query in this repo.
    Fix: `coverage.like_prefix_clause()` / `database.like_prefix()`, or a
    bound `?` parameter.

E2  uncounted-cap
    A self-truncating slice — ``xs = xs[:args.limit]`` and friends — whose file
    contains no evidence that the truncation is reported.  This is the
    data_symbol_scan defect: ``tasks = tasks[:args.max_symbols]`` with a default
    of 4000 against an 18,549-symbol universe, and a stderr line that printed
    only ``scanned=``.  Every count that scanner produced for a month was a 22%
    sample presented as a total.
    Fix: route the cap through `CoverageReport.cap()`, or — if the slice only
    shortens a PRINTOUT of an already-complete count — say so, and register the
    site in ALLOW_DISPLAY_ONLY below with a reason.

RULES (WARN — reported, do not fail)
------------------------------------
W1  swallowed-empty
    ``except ...: return []`` / ``return None`` / ``return {}``.  A scanner that
    turns a parse error into an empty result makes "the input was broken" and
    "there are no bugs here" print identically.

W2  worker-mutated-global
    A module that both uses `ThreadPoolExecutor`/`ProcessPoolExecutor` and
    assigns to a module-level `global`.  That is the shape of the
    data_symbol_scan race, where a lazily-built linker-map index was published
    EMPTY and then filled from inside the pool, making a month of verdicts
    nondeterministic.

Usage:
    python3 scripts/analysis/honesty_lint.py                  # lint the repo
    python3 scripts/analysis/honesty_lint.py --json
    python3 scripts/analysis/honesty_lint.py --warnings       # include W rules
Exit code 0 = no ERROR findings, 1 = ERROR findings.
"""
from __future__ import annotations

import argparse
import io
import json
import os
import re
import sys
import tokenize
from typing import Dict, Iterable, List, NamedTuple

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Directories linted by default: everything that measures or counts.
LINT_DIRS = ("scripts",)

# Files exempt from E1 because they *demonstrate* the bug on purpose (the
# negative controls) or only quote it in prose.
ALLOW_UNESCAPED_LIKE = {
    "scripts/analysis/tests/test_coverage.py",
    "scripts/orchestrator/tests/test_like_prefix_escape.py",
    "scripts/test_certify_floor.py",
    "scripts/analysis/honesty_lint.py",
    "scripts/analysis/tests/test_honesty_lint.py",
}

# Slice sites that truncate a PRINTOUT of an already-complete count, not the
# analysis.  Each entry needs a reason; an unexplained entry is how an allowlist
# turns back into the bug it was meant to prevent.
ALLOW_DISPLAY_ONLY: Dict[str, str] = {
    "scripts/analysis/remaining_work.py":
        "max_per_cat shortens a per-category printout; the totals above it are full",
    "scripts/analysis/header_cluster.py":
        "limit shortens the cluster printout; cluster counts are computed first",
    "scripts/analysis/compare_progress.py":
        "limit shortens the regression/improvement listings; the headline deltas are full",
    "scripts/at_limit_rb3_candidates.py":
        "limit shortens the candidate printout; 'DC3 AT_LIMIT pool ...: N' prints the full N",
    "scripts/validate_symbols.py":
        "limit shortens sample_errors; the error COUNT is reported separately",
    "scripts/orchestrator/context_collector.py":
        "MAX_CALLEE_SIGNATURES budgets an LLM prompt, not a measured population; "
        "nothing downstream reads it as a count",
}

CAP_NAME_RE = re.compile(r"(limit|max|top|cap|sample|budget|head|first)", re.I)
# ...but a bound named for CHARACTERS is truncating a STRING for display, not
# dropping rows from a population.  `sig = sig[:MAX_SIGNATURE_CHARS]` is fine.
NOT_A_ROW_CAP_RE = re.compile(r"(chars?|len|length|width|bytes|cols?)$", re.I)
SELF_SLICE_RE = re.compile(
    r"^[ \t]*([A-Za-z_][\w]*)\s*=\s*\1\s*\[\s*:\s*(?:args\.)?([A-Za-z_][\w]*)\s*\]", re.M)
LIKE_RE = re.compile(r"\bLIKE\b", re.I)
# Evidence that a file reports its truncation.  Deliberately generous: the point
# is to catch SILENT caps, not to mandate one spelling.
TRUNCATION_EVIDENCE = ("TRUNCAT", "CAPPED", "(capped", "coverage.cap", ".cap(",
                       "CoverageReport", "was_capped", "capped=")
SWALLOW_RE = re.compile(
    r"except[^\n:]*:\s*\n\s*return\s*(\[\]|None|\{\}|0)\s*(?:#.*)?$", re.M)


class Finding(NamedTuple):
    rule: str
    severity: str
    path: str
    line: int
    text: str
    detail: str


# --------------------------------------------------------------------------- #
# E1 — unescaped SQL LIKE
# --------------------------------------------------------------------------- #

def _code_strings(src: str) -> Iterable[tokenize.TokenInfo]:
    """Yield non-docstring, non-comment STRING tokens.

    Prose in a docstring that merely *quotes* the bug (this module does it four
    times) must not trip the lint, or the lint becomes noise and gets disabled —
    which is how the original defect survived.
    """
    try:
        toks = list(tokenize.generate_tokens(io.StringIO(src).readline))
    except (tokenize.TokenError, IndentationError, SyntaxError):
        return []
    out = []
    prev_meaningful = None
    for t in toks:
        if t.type == tokenize.STRING:
            raw = t.string.lstrip("rbfuRBFU")
            is_triple = raw[:3] in ('"""', "'''")
            # A triple-quoted string in statement position is a docstring/prose.
            if is_triple and prev_meaningful in (None, tokenize.NEWLINE,
                                                 tokenize.NL, tokenize.INDENT,
                                                 tokenize.DEDENT):
                pass
            else:
                out.append(t)
        if t.type not in (tokenize.COMMENT,):
            prev_meaningful = t.type
    return out


def check_unescaped_like(path: str, src: str) -> List[Finding]:
    found: List[Finding] = []
    for tok in _code_strings(src):
        body = tok.string
        if not LIKE_RE.search(body):
            continue
        for m in re.finditer(r"LIKE\s+'([^']*)'", body, re.I):
            lit = m.group(1)
            # Drop properly-escaped wildcards, then drop the LEADING/TRAILING
            # '%' that a prefix/substring match legitimately wants
            # (``LIKE '%xdk%'`` is an intended substring search, not a bug).
            # Whatever `_` or `%` survives in the CORE is an accident.
            stripped = re.sub(r"\\\\?[_%]", "", lit)
            core = stripped.strip("%")
            if "_" not in core and "%" not in core:
                continue
            tail = body[m.end():m.end() + 80]
            if "ESCAPE" in tail.upper():
                continue
            found.append(Finding(
                "E1", "ERROR", path, tok.start[0], m.group(0),
                "'_' is a single-char wildcard in SQL LIKE; add ESCAPE or use "
                "coverage.like_prefix_clause()"))
    return found


# --------------------------------------------------------------------------- #
# E2 — a cap that truncates the analysis without saying so
# --------------------------------------------------------------------------- #

def check_uncounted_cap(path: str, src: str) -> List[Finding]:
    found: List[Finding] = []
    reports = any(e in src for e in TRUNCATION_EVIDENCE)
    for m in SELF_SLICE_RE.finditer(src):
        var, bound = m.group(1), m.group(2)
        if not CAP_NAME_RE.search(bound) or NOT_A_ROW_CAP_RE.search(bound):
            continue                       # `s = s[:paren_idx]` is not a cap
        if reports:
            continue
        line = src[:m.start()].count("\n") + 1
        found.append(Finding(
            "E2", "ERROR", path, line, m.group(0).strip(),
            f"`{var}` is truncated by `{bound}` and nothing in this file reports the "
            f"truncation; route it through CoverageReport.cap() or register a "
            f"display-only reason in honesty_lint.ALLOW_DISPLAY_ONLY"))
    return found


# --------------------------------------------------------------------------- #
# Warnings
# --------------------------------------------------------------------------- #

def check_swallowed_empty(path: str, src: str) -> List[Finding]:
    out = []
    for m in SWALLOW_RE.finditer(src):
        line = src[:m.start()].count("\n") + 1
        out.append(Finding("W1", "WARN", path, line, m.group(0).split("\n")[0].strip(),
                           "an error and 'nothing found' now print identically"))
    return out


def check_worker_mutated_global(path: str, src: str) -> List[Finding]:
    if "PoolExecutor" not in src:
        return []
    out = []
    for m in re.finditer(r"^\s*global\s+([A-Za-z_]\w*)", src, re.M):
        line = src[:m.start()].count("\n") + 1
        out.append(Finding("W2", "WARN", path, line, m.group(0).strip(),
                           f"module-level `{m.group(1)}` is rebound in a file that runs a "
                           f"worker pool — the data_symbol_scan race shape; build into a "
                           f"local and publish once, or warm it on the main thread"))
    return out


# --------------------------------------------------------------------------- #

def lint_file(path: str, rel: str) -> List[Finding]:
    try:
        src = open(path, errors="replace").read()
    except OSError:
        return []
    out: List[Finding] = []
    if rel not in ALLOW_UNESCAPED_LIKE:
        out += check_unescaped_like(rel, src)
    if rel not in ALLOW_DISPLAY_ONLY:
        out += check_uncounted_cap(rel, src)
    out += check_swallowed_empty(rel, src)
    out += check_worker_mutated_global(rel, src)
    return out


def lint_repo(root: str = REPO, dirs: Iterable[str] = LINT_DIRS) -> List[Finding]:
    out: List[Finding] = []
    files = []
    for d in dirs:
        for dirpath, dirnames, filenames in os.walk(os.path.join(root, d)):
            dirnames[:] = sorted(x for x in dirnames
                                 if x not in ("__pycache__", "node_modules", ".git"))
            for fn in sorted(filenames):
                if fn.endswith(".py"):
                    files.append(os.path.join(dirpath, fn))
    for p in sorted(files):                      # sorted => deterministic output
        out += lint_file(p, os.path.relpath(p, root))
    return sorted(out, key=lambda f: (f.rule, f.path, f.line))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=REPO)
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--warnings", action="store_true", help="also print W-rule findings")
    args = ap.parse_args()

    findings = lint_repo(args.root)
    errors = [f for f in findings if f.severity == "ERROR"]
    warns = [f for f in findings if f.severity == "WARN"]

    if args.json:
        print(json.dumps({"errors": [f._asdict() for f in errors],
                          "warnings": [f._asdict() for f in warns]}, indent=2))
    else:
        for f in errors:
            print(f"{f.path}:{f.line}: [{f.rule}] {f.text}\n    -> {f.detail}")
        if args.warnings:
            for f in warns:
                print(f"{f.path}:{f.line}: [{f.rule}] {f.text}\n    -> {f.detail}")
        print(f"\nhonesty_lint: {len(errors)} error(s), {len(warns)} warning(s) "
              f"across {args.root}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
