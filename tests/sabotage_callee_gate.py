#!/usr/bin/env python3
"""Sabotage harness: prove every guard in the callee gate can actually fail.

A guard nobody has watched fail is a guard nobody has tested.  This applies one
deliberate defect at a time to a CLEAN checkout of the gate's files, runs the
test that is supposed to catch it, and asserts the result is RED -- then
restores and asserts GREEN.  A sabotage that leaves the suite green is reported
as `NOT CAUGHT`, which is a failure of this harness's exit code too.

Run from the repo root of a worktree with the gate committed::

    python3 tests/sabotage_callee_gate.py

It refuses to run against a dirty tree, because `git checkout --` is how it
restores and that would take your uncommitted work with it.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GATE = ROOT / "scripts/orchestrator/callee_gate.py"
VERIFY = ROOT / "scripts/verify_pattern_scan_current.py"
SYNC = ROOT / "scripts/sync_objdiff.py"
TESTS = ROOT / "tests/test_callee_gate.py"

#: (label, file, old, new, test node that must go RED)
SABOTAGES = [
    ("S1  tool_version comparison dropped", GATE,
     'if scan["tool_version"] != installed:',
     'if False and scan["tool_version"] != installed:',
     "test_scan_from_a_different_objdiff_raises_and_the_same_scan_corrected_does_not"),

    ("S2  absent scan returns an empty set", GATE,
     "    if scan is None:\n        raise StalePatternScanError(",
     "    if scan is None:\n        return {'id': 0, 'ruler': ruler, "
     "'tool_version': installed, 'tree_verified': 1}\n    if False:\n"
     "        raise StalePatternScanError(",
     "test_absence_of_a_scan_raises_rather_than_returning_an_empty_set"),

    ("S3  tree_verified refusal dropped", GATE,
     'if not scan["tree_verified"]:',
     'if False and not scan["tree_verified"]:',
     "test_unverified_tree_and_hashless_version_both_refuse"),

    ("S4  hashless tool_version accepted", GATE,
     'if "xxh3 unavailable" in scan["tool_version"] or "xxh3 " not in scan["tool_version"]:',
     'if False:',
     "test_unverified_tree_and_hashless_version_both_refuse"),

    ("S5  verify_pattern_scan_current always exits 0", VERIFY,
     '        print(f"STALE PATTERN SCAN ({a.ruler}): {e}", file=sys.stderr)\n        return 1',
     '        print(f"STALE PATTERN SCAN ({a.ruler}): {e}", file=sys.stderr)\n        return 0',
     "test_verify_pattern_scan_current_check_exit_codes"),

    ("S6  classify_pair: same-address test dropped", GATE,
     'return "ICF_FOLD" if at == ab else "REAL_OTHER_ADDR"',
     'return "ICF_FOLD"',
     "test_classify_pair_truth_table"),

    ("S6b classify_pair sabotage vs the REAL population", GATE,
     'return "ICF_FOLD" if at == ab else "REAL_OTHER_ADDR"',
     'return "ICF_FOLD"',
     "test_positive_control_on_the_real_population_both_directions"),

    ("S7  empty linker map adjudicates blind", GATE,
     "    if not addr:\n        raise LinkerMapError(",
     "    if False:\n        raise LinkerMapError(",
     "test_a_map_that_parses_to_nothing_refuses_instead_of_adjudicating_blind"),

    ("S8  a different callee address stops blocking", GATE,
     '"REAL_OTHER_ADDR":   (True,  "real_other_address"),',
     '"REAL_OTHER_ADDR":   (False, "icf_fold"),',
     "test_gate_judges_folds_and_pairing_artifacts_but_blocks_a_different_address"),

    ("S8b the same, vs the REAL population", GATE,
     '"REAL_OTHER_ADDR":   (True,  "real_other_address"),',
     '"REAL_OTHER_ADDR":   (False, "icf_fold"),',
     "test_positive_control_on_the_real_population_both_directions"),

    ("S9  clear when ANY pair folds instead of ALL", GATE,
     "        if blocking:\n",
     "        if blocking and len(blocking) == len(verdicts):\n",
     "test_one_blocking_pair_among_folds_still_withholds_the_certificate"),

    ("S10 one guessed pair launders the whole function", GATE,
     'actionable = [r for r in rows if (r["fixability"] or "") != UNVERIFIABLE]',
     'actionable = [] if any((r["fixability"] or "") == UNVERIFIABLE for r in rows) '
     'else list(rows)',
     "test_unverifiable_pairing_clears_only_when_every_finding_is_unverifiable"),

    ("S10b a guessed-pair function is blocked, vs REAL", GATE,
     '            gate.cleared[symbol] = "unverifiable_pairing"',
     '            gate.blocked[symbol] = "unverifiable_pairing"',
     "test_positive_control_on_the_real_population_both_directions"),

    ("S13 _callee_rows silently drops a symbol", GATE,
     "    for r in rows:\n        out[r[\"symbol\"]].append(r)\n    return dict(out)",
     "    for r in rows:\n        out[r[\"symbol\"]].append(r)\n"
     "    out.pop(sorted(out)[0], None)\n    return dict(out)",
     "test_positive_control_on_the_real_population_both_directions"),

    ("S11 merged_* stub no longer recognised", GATE,
     "    if MERGED_STUB_RE.search(base) or MERGED_STUB_RE.search(target):\n"
     '        return "MERGED_STUB"',
     "    if False:\n"
     '        return "MERGED_STUB"',
     "test_positive_control_on_the_real_population_both_directions"),

    ("S12 sync_objdiff warns instead of exiting 5", SYNC,
     '            print(f"\\nREFUSING to issue auto-AT_LIMIT certificates:\\n{gate_refusal}\\n",\n'
     '                  file=sys.stderr)\n            sys.exit(5)',
     '            print(f"\\nREFUSING to issue auto-AT_LIMIT certificates:\\n{gate_refusal}\\n",\n'
     '                  file=sys.stderr)',
     "test_sync_objdiff_refuses_to_certify_from_a_stale_scan_end_to_end"),
]


def dirty() -> list[str]:
    out = subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain", "--",
                          str(GATE), str(VERIFY), str(SYNC), str(TESTS)],
                         capture_output=True, text=True).stdout
    return [l for l in out.splitlines() if l.strip()]


def run(node: str) -> bool:
    """True = GREEN."""
    r = subprocess.run([sys.executable, "-m", "pytest", "-q", "-p", "no:cacheprovider",
                        f"{TESTS}::{node}"], cwd=ROOT, capture_output=True, text=True)
    return r.returncode == 0


def restore() -> None:
    subprocess.run(["git", "-C", str(ROOT), "checkout", "--",
                    str(GATE), str(VERIFY), str(SYNC)], check=True)


def main() -> int:
    if dirty():
        print("REFUSING: the gate's files have uncommitted changes; this harness "
              "restores with `git checkout --` and would destroy them:", file=sys.stderr)
        print("\n".join(dirty()), file=sys.stderr)
        return 2

    print("baseline: every sabotaged test must be GREEN before we start")
    for label, _f, _o, _n, node in SABOTAGES:
        if not run(node):
            print(f"  BASELINE RED on {node} -- nothing below is interpretable")
            return 2
    print(f"  ok, {len({s[4] for s in SABOTAGES})} distinct tests green\n")

    failures = 0
    for label, path, old, new, node in SABOTAGES:
        src = path.read_text()
        if src.count(old) != 1:
            print(f"{label:46} PATCH DID NOT APPLY ({src.count(old)} matches) "
                  f"-- a sabotage that does not land proves nothing")
            failures += 1
            continue
        path.write_text(src.replace(old, new, 1))
        red = not run(node)
        restore()
        green = run(node)
        verdict = ("CAUGHT" if red else "NOT CAUGHT") + ("" if green else " / RESTORE FAILED")
        print(f"{label:46} {'RED' if red else 'green':6} -> "
              f"{'GREEN' if green else 'RED':6}  {verdict}")
        print(f"{'':46}   ({node})")
        if not (red and green):
            failures += 1

    print()
    if failures:
        print(f"{failures} sabotage(s) NOT CAUGHT -- those guards cannot fail")
        return 1
    print(f"all {len(SABOTAGES)} sabotages caught, tree restored green")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
