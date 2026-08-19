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

E3  coverage-without-denominator
    A file that constructs a `CoverageReport` but never calls `universe()` or
    `universe_unknown()` on it.  This is the exit-4 BYPASS, and it is a one-line
    regression: `unaccounted` is `universe - (examined + drops)`, so with no
    universe it is `None`, the arithmetic tripwire cannot fire, and `emit()`
    used to return 0 under a banner reading NO DENOMINATOR.  Deleting the
    `cov.universe(...)` line restored the exact silence the whole module exists
    to end, and no static check objected.
    `emit()` now returns EXIT_NO_DENOMINATOR (6) at runtime; this rule catches
    it at commit time, which is cheaper.
    Fix: call `cov.universe(n, "what")` before filtering — or, if you genuinely
    cannot compute one, `cov.universe_unknown("why")`, which exits 0 and prints
    the reason.
    NOT applied under `scripts/analysis/tests/`: those files construct
    `CoverageReport` objects as FIXTURES to exercise this very contract, and a
    denominator-less one there is the negative control for this rule, not an
    instance of it.  `test_e3_still_fires_outside_the_tests_directory` pins that
    the exemption is scoped to the directory and not to the rule.

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
Exit code 0 = no ERROR findings, 1 = ERROR findings,
5 = the lint examined no files at all (see EXIT_NO_INPUT below).
"""
from __future__ import annotations

import argparse
import io
import json
import os
import re
import subprocess
import sys
import tokenize
from typing import Dict, Iterable, List, NamedTuple, Optional

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

#: Same code and same meaning as scripts/analysis/coverage.EXIT_NO_INPUT: this
#: run had nothing to look at.  Kept as a literal rather than imported so the
#: lint stays runnable standalone from any cwd.
EXIT_NO_INPUT = 5

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
    "scripts/orchestrator/context_collector.py":
        "MAX_CALLEE_SIGNATURES budgets an LLM prompt, not a measured population; "
        "nothing downstream reads it as a count",
}
# NOTE: this list started with five entries and is down to one.  Four were
# either fixed properly (remaining_work, compare_progress now route through
# CoverageReport) or had never fired at all (at_limit_rb3_candidates,
# validate_symbols slice inline rather than self-assigning, so E2 could never
# match them).  A speculative allowlist entry is its own small lie: it asserts
# "we looked at this and it's fine" about a site the checker never examined.
# test_allowlist_has_no_dead_entries keeps the list honest by failing on any
# entry whose file no longer produces the finding it excuses.

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
# E3: a CoverageReport built but never given a denominator.
COVERAGE_CTOR_RE = re.compile(r"^[ \t]*([A-Za-z_][\w]*)\s*=\s*CoverageReport\s*\(", re.M)
UNIVERSE_CALL_RE = re.compile(r"\.universe(?:_unknown)?\s*\(")
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
# E3 — a CoverageReport with no denominator: the exit-4 bypass
# --------------------------------------------------------------------------- #

#: E3 is about SCANNERS.  The negative-control files deliberately build
#: denominator-less reports to prove the runtime check fires, so linting them
#: for it would flag the control as the bug.
E3_EXEMPT_PREFIXES = ("scripts/analysis/tests/",)


def check_coverage_without_denominator(path: str, src: str) -> List[Finding]:
    """Flag a `CoverageReport` that is never told its universe.

    Matched per FILE rather than per variable: a scanner may build the report in
    one function and declare the universe in another (dta_access_audit does),
    and a per-variable rule would false-positive on that and get switched off --
    which is how the original defects survived.  A file that builds one and
    mentions neither `universe(` nor `universe_unknown(` anywhere has no such
    excuse.
    """
    found: List[Finding] = []
    if path.replace(os.sep, "/").startswith(E3_EXEMPT_PREFIXES):
        return found
    if UNIVERSE_CALL_RE.search(src):
        return found
    for m in COVERAGE_CTOR_RE.finditer(src):
        line = src[:m.start()].count("\n") + 1
        found.append(Finding(
            "E3", "ERROR", path, line, m.group(0).strip(),
            f"`{m.group(1)}` is a CoverageReport that is never given a denominator: "
            f"no universe() or universe_unknown() call anywhere in this file. "
            f"`unaccounted` stays None, so the exit-4 arithmetic check cannot fire "
            f"-- the tripwire is disarmed by omission"))
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
    out += check_coverage_without_denominator(rel, src)
    out += check_swallowed_empty(rel, src)
    out += check_worker_mutated_global(rel, src)
    return out


def _tracked_py_files(root: str, dirs: Iterable[str]) -> Optional[List[str]]:
    """Tracked *.py under `dirs`, via git.  None when this is not a git tree.

    The lint governs code that is IN THE REPOSITORY.  Walking the filesystem
    instead made its verdict depend on whatever untracked scratch a developer
    happened to have lying about: a gitignored
    `scripts/scratch/find_effective_complete.py` containing the historical
    `LIKE '??_9%'` made the repo ratchet fail on a clean checkout, for a file
    no clean checkout contains.  A gate whose result depends on untracked local
    files is not a gate.
    """
    try:
        out = subprocess.run(
            ["git", "-C", root, "ls-files", "-z", "--", *[f"{d}/*.py" for d in dirs]],
            capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return None
    if out.returncode != 0:
        return None
    rels = [p for p in out.stdout.split("\0") if p.endswith(".py")]
    return sorted(os.path.join(root, p) for p in rels)


def scan_files(root: str = REPO, dirs: Iterable[str] = LINT_DIRS) -> List[str]:
    """The .py files this lint would examine, sorted.

    Split out from `lint_repo` so a caller can state the DENOMINATOR of a lint
    run.  `0 error(s)` over zero files and `0 error(s)` over 600 files are
    different claims, and this checker used to print them identically -- sub-
    shape 3 (missing input => clean bill of health) inside the tool written to
    catch sub-shape 3.

    Prefers `git ls-files` so untracked/gitignored scratch cannot change the
    verdict; falls back to a filesystem walk outside a git tree (a tarball
    export, a vendored copy) rather than reporting an empty universe, which
    would be that same sub-shape again.
    """
    tracked = _tracked_py_files(root, dirs)
    if tracked is not None:
        return tracked
    files = []
    for d in dirs:
        for dirpath, dirnames, filenames in os.walk(os.path.join(root, d)):
            dirnames[:] = sorted(x for x in dirnames
                                 if x not in ("__pycache__", "node_modules", ".git"))
            for fn in sorted(filenames):
                if fn.endswith(".py"):
                    files.append(os.path.join(dirpath, fn))
    return sorted(files)                         # sorted => deterministic output


def lint_repo(root: str = REPO, dirs: Iterable[str] = LINT_DIRS) -> List[Finding]:
    out: List[Finding] = []
    for p in scan_files(root, dirs):
        out += lint_file(p, os.path.relpath(p, root))
    return sorted(out, key=lambda f: (f.rule, f.path, f.line))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=REPO)
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--warnings", action="store_true", help="also print W-rule findings")
    args = ap.parse_args()

    scanned = scan_files(args.root)
    findings = lint_repo(args.root)
    errors = [f for f in findings if f.severity == "ERROR"]
    warns = [f for f in findings if f.severity == "WARN"]

    # A lint that examined NOTHING must not print like a lint that passed.
    # `--root /does/not/exist` returned `0 error(s), 0 warning(s)` and exit 0:
    # os.walk on a missing path yields nothing and raises nothing, so the
    # checker for "missing input => clean verdict" had a missing input and gave
    # a clean verdict.  EXIT_NO_INPUT (5), same code the scanners use.
    if not scanned:
        print(f"INCONCLUSIVE: honesty_lint examined 0 files under {args.root} "
              f"-- THIS RUN CHECKED NOTHING.")
        print(f"  Looked for *.py under: "
              f"{', '.join(os.path.join(args.root, d) for d in LINT_DIRS)}")
        print("  '0 errors' here means 'no input', not 'no findings'.")
        return EXIT_NO_INPUT

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
              f"across {len(scanned)} files under {args.root}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
