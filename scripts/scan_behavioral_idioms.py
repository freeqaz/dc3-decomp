#!/usr/bin/env python3
"""Scan DC3 source for metric-invisible behavioral-bug idioms.

Finds the *cleanly-detectable* members of the bug class documented in
docs/decomp/patterns/behavioral-divergence.md — defects that objdiff % cannot see
(neutral/near-neutral match, some at 0% as unpaired stubs) yet break behavior at
runtime, several of which were introduced by match-neutral "wins". This is a source
grep, not an objdiff scan; run it to sweep the codebase for the same shapes we fixed
in the 2026-07 camera/text/scoring wave.

Checks:
  reversed-erase     `X.erase(X.end(), X.begin())` — ALWAYS a bug (UB). Exact, zero
                     false positives. (Real: MoveDir::ResetDetectFrames, f57f2307.)
  stale-iterator     an iterator/reference bound from a container, then a
                     push_back/insert/resize/emplace on that container in the same
                     block, then a later use of the iterator. Heuristic — review each.
                     (Real: RndText::OnComputeCharWidths, fabe57a5.)
  self-clobber       `foo.Method(..., foo.member)` — a call passing one of the
                     receiver's own members as a later argument, the aliasing shape
                     behind Transform::LookAt. Heuristic — review each.

Also a two-revision diff mode that flags NON-commutative operand/argument/comparison
changes between HEAD (or a given ref) and the working tree — the shape a permuter
sweep / port / archaeology commit uses to introduce a silent corruption.

Usage:
    python3 scripts/scan_behavioral_idioms.py                       # scan working tree
    python3 scripts/scan_behavioral_idioms.py --check reversed-erase
    python3 scripts/scan_behavioral_idioms.py --path src/system/hamobj
    python3 scripts/scan_behavioral_idioms.py --diff HEAD           # flag risky diffs vs HEAD
    python3 scripts/scan_behavioral_idioms.py --json

Exit code is 1 if any EXACT-confidence finding (reversed-erase, or a diff-mode
non-commutative swap) is present, else 0 — usable as a pre-commit / CI gate.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass, field, asdict
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SRC_DEFAULT = REPO_ROOT / "src"

# ---------------------------------------------------------------------------
# Findings
# ---------------------------------------------------------------------------


@dataclass
class Finding:
    check: str
    confidence: str  # "exact" | "heuristic"
    file: str
    line: int
    text: str
    note: str = ""


# ---------------------------------------------------------------------------
# Working-tree scans (single revision)
# ---------------------------------------------------------------------------

# `X.erase(X.end(), X.begin())` / `erase(end(), begin())`. The end()-before-begin()
# argument order is the tell; the receiver may or may not be spelled out.
_REVERSED_ERASE = re.compile(
    r"\.erase\s*\(\s*"
    r"(?:[A-Za-z_]\w*(?:\.\w+|->\w+)*\.)?end\s*\(\s*\)\s*,\s*"
    r"(?:[A-Za-z_]\w*(?:\.\w+|->\w+)*\.)?begin\s*\(\s*\)\s*\)"
)

# Growth operations that invalidate iterators/refs/pointers into a std::vector.
_GROWTH = re.compile(r"\.(push_back|emplace_back|insert|resize|emplace|reserve)\s*\(")

# An iterator/pointer/reference bound from a container's begin()/front()/&vec[i]/*it.
_ITER_BIND = re.compile(
    r"\b(?:auto|[\w:<>,\s\*&]+?)\s+([A-Za-z_]\w*)\s*=\s*"
    r"([A-Za-z_]\w*)(?:\.|->)(?:begin|end|front|back|data)\s*\(\s*\)"
)

# `receiver.Method( ... receiver.member ... )` / `receiver->Method( ... receiver->m )`
# The aliasing shape behind Transform::LookAt (caller passes a member of the same
# object the method mutates). Heuristic: same identifier as receiver and as an arg
# member-access.
_SELF_CLOBBER = re.compile(
    r"\b([A-Za-z_]\w*)\s*(?:\.|->)\s*[A-Za-z_]\w*\s*\(([^;{}]*)\)"
)


def _iter_sources(root: Path):
    for p in sorted(root.rglob("*.cpp")):
        yield p
    for p in sorted(root.rglob("*.h")):
        yield p


def _rel(p: Path) -> str:
    try:
        return str(p.relative_to(REPO_ROOT))
    except ValueError:
        return str(p)


def scan_reversed_erase(path: Path, text: str) -> list[Finding]:
    out = []
    for i, line in enumerate(text.splitlines(), 1):
        if _REVERSED_ERASE.search(line):
            out.append(Finding(
                check="reversed-erase", confidence="exact", file=_rel(path),
                line=i, text=line.strip(),
                note="erase(end(), begin()) — args reversed; UB when non-empty. "
                     "Did you mean clear() or erase(begin(), end())? NOTE: run_objdiff "
                     "first — if the fn is already ~matched, the retail binary shipped "
                     "this bug (faithful decomp); keep the PPC path, guard the safe "
                     "form under HX_NATIVE only if native reaches it (see "
                     "docs/decomp/patterns/behavioral-divergence.md).",
            ))
    return out


def scan_stale_iterator(path: Path, text: str) -> list[Finding]:
    """Heuristic: iterator bound, then its container grown, then iterator reused."""
    out = []
    lines = text.splitlines()
    # Track bindings within a coarse brace-scope window (reset on a lone '}').
    bound: dict[str, tuple[str, int, str]] = {}  # itername -> (container, line, text)
    for i, line in enumerate(lines, 1):
        if line.strip() == "}":
            bound.clear()
        m = _ITER_BIND.search(line)
        if m:
            bound[m.group(1)] = (m.group(2), i, line.strip())
        g = _GROWTH.search(line)
        if g and bound:
            grown = line[:g.start()].split()[-1] if line[:g.start()].split() else ""
            grown = re.split(r"[^\w]", grown)[-1] if grown else ""
            for itname, (container, bline, btext) in list(bound.items()):
                if grown and grown == container:
                    # find a later use of itname
                    for j in range(i + 1, min(i + 40, len(lines) + 1)):
                        if re.search(rf"\b{re.escape(itname)}\b", lines[j - 1]):
                            out.append(Finding(
                                check="stale-iterator", confidence="heuristic",
                                file=_rel(path), line=j, text=lines[j - 1].strip(),
                                note=f"'{itname}' bound from '{container}' (L{bline}), "
                                     f"'{container}' grown at L{i}; use may be "
                                     f"invalidated. Rebind after the growth.",
                            ))
                            break
                    bound.pop(itname, None)
    return out


# STL/container methods that legitimately take their own iterators — not the
# self-clobber shape. Suppresses the dominant noise source.
_STL_METHODS = {
    "erase", "insert", "assign", "append", "replace", "resize", "swap",
    "push_back", "emplace", "emplace_back", "find", "count", "equal_range",
}
# method names on the RHS of the `.`/`->` before `(` in the self-clobber regex.
_METHOD_NAME = re.compile(r"(?:\.|->)\s*([A-Za-z_]\w*)\s*\(")


def scan_self_clobber(path: Path, text: str) -> list[Finding]:
    """Heuristic (opt-in, noisy): foo.Method(..., foo.member) — caller passes the
    receiver's own member as an argument, the aliasing shape behind Transform::LookAt.
    STL container methods that take their own iterators are excluded."""
    out = []
    for i, line in enumerate(text.splitlines(), 1):
        for m in _SELF_CLOBBER.finditer(line):
            recv, args = m.group(1), m.group(2)
            if recv in ("if", "for", "while", "switch", "return", "sizeof"):
                continue
            mn = _METHOD_NAME.search(line[m.start():m.end()])
            if mn and mn.group(1) in _STL_METHODS:
                continue
            # an arg that is `recv.member` or `recv->member`
            if re.search(rf"\b{re.escape(recv)}\s*(?:\.|->)\s*\w", args):
                out.append(Finding(
                    check="self-clobber", confidence="heuristic", file=_rel(path),
                    line=i, text=line.strip(),
                    note=f"call on '{recv}' passes a member of '{recv}' as an arg; if "
                         f"the method writes that member before reading the param, "
                         f"the basis/value is clobbered (cf. Transform::LookAt).",
                ))
                break
    return out


_SINGLE_REV_CHECKS = {
    "reversed-erase": scan_reversed_erase,
    "stale-iterator": scan_stale_iterator,
    "self-clobber": scan_self_clobber,
}


# ---------------------------------------------------------------------------
# Diff mode (two revisions) — flag NON-commutative operand/arg/comparison changes
# ---------------------------------------------------------------------------

# comparison-direction / boundary flip on an added line where a removed line had the
# opposite. Detected pairwise per hunk.
_CMP = re.compile(r"(<=|>=|<|>|==|!=)")


def scan_diff(ref: str, paths: list[str]) -> list[Finding]:
    """Flag added lines whose only change vs a removed line is a non-commutative
    operand swap, argument swap, or comparison-direction flip — the shape by which
    a match-neutral commit silently corrupts behavior."""
    cmd = ["git", "diff", "--unified=0", "--no-color", ref, "--", *paths]
    try:
        diff = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True,
                              check=True).stdout
    except subprocess.CalledProcessError as e:
        print(f"git diff failed: {e.stderr}", file=sys.stderr)
        return []

    out: list[Finding] = []
    cur_file = ""
    removed: list[str] = []
    added: list[str] = []

    def flush():
        # naive pairwise: for each added line, see if a removed line has the same
        # tokens but a swapped non-commutative op / comparison direction.
        for a in added:
            at = a.strip()
            for r in removed:
                rt = r.strip()
                if at == rt:
                    continue
                # comparison direction flip: same text except <-> or <=->=
                for lhs, rhs in (("<", ">"), ("<=", ">="), ("==", "!=")):
                    if rt.replace(lhs, "\0") == at.replace(rhs, "\0") and lhs in rt and rhs in at:
                        out.append(Finding(
                            check="diff-comparison-flip", confidence="exact",
                            file=cur_file, line=0, text=at,
                            note=f"comparison direction changed {lhs} -> {rhs} vs "
                                 f"prior '{rt}'. If the then/else bodies were NOT "
                                 f"also swapped this inverts the logic.",
                        ))
                # non-commutative binary swap `A - B` -> `B - A` (also / % << >>)
                for op in (" - ", " / ", " % ", " << ", " >> "):
                    if op in rt and op in at:
                        rparts = [s.strip() for s in rt.split(op)]
                        aparts = [s.strip() for s in at.split(op)]
                        if len(rparts) == 2 == len(aparts) and \
                           rparts[0] == aparts[1] and rparts[1] == aparts[0] and \
                           rparts[0] != rparts[1]:
                            out.append(Finding(
                                check="diff-noncommutative-swap", confidence="exact",
                                file=cur_file, line=0, text=at,
                                note=f"operands of non-commutative '{op.strip()}' "
                                     f"swapped vs prior '{rt}' — sign/value inversion, "
                                     f"not a neutral reorder.",
                            ))
        removed.clear()
        added.clear()

    for line in diff.splitlines():
        if line.startswith("+++ b/"):
            flush()
            cur_file = line[6:]
        elif line.startswith("@@"):
            flush()
        elif line.startswith("+") and not line.startswith("++"):
            added.append(line[1:])
        elif line.startswith("-") and not line.startswith("--"):
            removed.append(line[1:])
    flush()
    return out


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--path", default=str(SRC_DEFAULT),
                    help="root dir (or file) to scan (default: src/)")
    ap.add_argument("--check", choices=sorted(_SINGLE_REV_CHECKS), action="append",
                    help="restrict to specific check(s); default runs all")
    ap.add_argument("--diff", metavar="REF",
                    help="diff mode: flag risky non-commutative changes vs REF "
                         "(e.g. HEAD) instead of scanning the working tree")
    ap.add_argument("--json", action="store_true", help="emit JSON")
    args = ap.parse_args()

    findings: list[Finding] = []

    if args.diff:
        findings = scan_diff(args.diff, [args.path])
    else:
        # self-clobber is noisy (heuristic) — opt-in via --check, not in the default run.
        checks = args.check or ["reversed-erase", "stale-iterator"]
        root = Path(args.path)
        srcs = [root] if root.is_file() else list(_iter_sources(root))
        for p in srcs:
            try:
                text = p.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            for name in checks:
                findings.extend(_SINGLE_REV_CHECKS[name](p, text))

    exact = [f for f in findings if f.confidence == "exact"]
    heur = [f for f in findings if f.confidence == "heuristic"]

    if args.json:
        print(json.dumps([asdict(f) for f in findings], indent=2))
    else:
        if not findings:
            print("clean: no behavioral-idiom findings.")
        for f in exact:
            print(f"[EXACT] {f.check}  {f.file}:{f.line}\n    {f.text}\n    -> {f.note}")
        for f in heur:
            print(f"[review] {f.check}  {f.file}:{f.line}\n    {f.text}\n    -> {f.note}")
        print(f"\n{len(exact)} exact, {len(heur)} heuristic finding(s).")

    return 1 if exact else 0


if __name__ == "__main__":
    sys.exit(main())
