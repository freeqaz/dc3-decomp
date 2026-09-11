#!/usr/bin/env python3
"""Mutation harness: break the freshness gate one way at a time, require RED.

`tests/test_report_freshness.py` carries its own negative controls.  This is
the second layer -- it proves those cases can actually fail, by editing
`scripts/report_freshness.py` (and the shell that calls it) nine different ways
and asserting each NAMED case reddens.  A mutation nobody's test catches is
reported UNDETECTED and makes this exit non-zero, as does the final control
that re-runs the suite after every restore.

M1 is the defect this whole change exists to remove: a gate that cannot pass on
a freshly built tree.  M3 and M4 are its mirror image -- a gate that passes when
it cannot tell.  Both directions have to be catchable or the guard is decoration.

Restores are in-memory single-anchor replacements (no `git checkout --`), so
this is safe to run on a dirty tree.  A non-unique anchor is reported, never
applied.

Run:  python3 tests/sabotage_report_freshness.py                    (~15 s)
      DC3_FRESHNESS_TREE=<built tree> python3 tests/sabotage_report_freshness.py
          also reddens the integration cases (~7 min; each run rebuilds nothing
          but pays verify_objs_patched.py --check twice)
"""
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RF = ROOT / "scripts" / "report_freshness.py"
MP = ROOT / "scripts" / "measure_progress.sh"
TESTS = ROOT / "tests" / "test_report_freshness.py"

# (label, file, old, new, [test names that MUST fail])
MUTATIONS = [
    ("M1  the shipped defect: only `no work to do` counts as current", RF,
     "    elif saw_always:",
     "    elif False:",
     ["test_freshly_built_tree_reads_current"]),

    ("M2  propagation rule widened to swallow every line", RF,
     '_PROPAGATION_RE = re.compile(r"^(?P<node>.+) is dirty$")',
     '_PROPAGATION_RE = re.compile(r"^(?P<node>.+)$")',
     ["test_touched_source_produces_exactly_one_root",
      "test_touched_source_reads_stale_and_names_the_file",
      "test_sabotage_widened_propagation_rule_is_caught"]),

    ("M3  unexplained pending work reads CURRENT (always-green)", RF,
     "        verdict = Verdict(\n            CANNOT_VERIFY,",
     "        verdict = Verdict(\n            CURRENT,",
     ["test_pending_work_with_no_explanation_cannot_be_verified",
      "test_propagation_without_a_root_cannot_be_verified"]),

    ("M4  CANNOT_VERIFY exits 0", RF,
     "_STATUS_EXIT = {CURRENT: 0, STALE: 1, CANNOT_VERIFY: 2}",
     "_STATUS_EXIT = {CURRENT: 0, STALE: 1, CANNOT_VERIFY: 0}",
     ["test_cannot_verify_is_never_exit_zero"]),

    ("M5  the split guard can no longer veto a settled graph", RF,
     "    if check_split and verdict.status == CURRENT:",
     "    if False and verdict.status == CURRENT:",
     ["test_split_guard_vetoes_a_settled_graph"]),

    ("M6  the post-compile guard can no longer veto a settled graph", RF,
     "    if check_patched and verdict.status == CURRENT:",
     "    if False and verdict.status == CURRENT:",
     ["test_patch_guard_vetoes_a_settled_graph",
      "test_sabotage_disabling_a_subguard_is_visible"]),

    ("M7  the `always` phony stops being recognised (always-red again)", RF,
     '            if m.group("node") == _ALWAYS_NODE:',
     "            if False:",
     ["test_fresh_tree_has_no_root_causes",
      "test_freshly_built_tree_reads_current"]),

    ("M8  a real root-cause format is filed as benign", RF,
     "        roots.append(body)",
     '        if "deps for" not in body:\n            roots.append(body)',
     ["test_every_ninja_root_format_is_a_root_cause"]),

    ("M9  measure_progress.sh goes back to grepping ninja -n", MP,
     '    python3 "${MAIN_REPO}/scripts/report_freshness.py" '
     '--project-dir "${dir}" --quiet',
     '    local out; out="$(cd "${dir}" && ninja -n "${REPORT_REL}" 2>&1)" '
     '&& [[ "${out}" == *"no work to do"* ]]',
     ["test_measure_progress_routes_through_this_checker"]),
]


def run_tests():
    p = subprocess.run(
        [sys.executable, "-m", "pytest", str(TESTS), "-q", "--no-header",
         "-p", "no:cacheprovider", "--tb=no"],
        cwd=str(ROOT), capture_output=True, text=True)
    out = p.stdout + p.stderr
    fails = set(re.findall(
        r"^(?:FAILED|ERROR|SUBFAIL(?:ED)?)(?:\([^)]*\))? [^ ]*::(\S+)", out, re.M))
    if not fails:
        fails = set(re.findall(r"^\S*tests/test_[a-z_]+\.py::(\S+)", out, re.M))
    return fails, p.returncode, out


def main() -> int:
    tree = os.environ.get("DC3_FRESHNESS_TREE")
    print(f"integration half: {'ON  ' + tree if tree else 'OFF (unit cases only)'}")

    fails, rc, out = run_tests()
    if rc != 0 or fails:
        print("BASELINE IS NOT GREEN -- refusing to interpret any mutation.")
        print(out[-3000:])
        return 1
    print("baseline: green\n")

    bad = []
    for label, path, old, new, must_fail in MUTATIONS:
        original = path.read_text()
        n = original.count(old)
        if n != 1:
            print(f"[BAD] {label}\n        -> anchor appears {n}x in {path.name}")
            bad.append((label, "anchor not unique"))
            continue
        path.write_text(original.replace(old, new, 1))
        try:
            fails, rc, out = run_tests()
        finally:
            path.write_text(original)
        missed = [t for t in must_fail
                  if not any(t in f for f in fails)]
        ok = rc != 0 and not missed
        if not ok:
            bad.append((label, f"rc={rc} missed={missed}"))
        print(f"[{'OK ' if ok else 'BAD'}] {label}\n"
              f"        -> {len(fails)} failing: "
              f"{', '.join(sorted(f.split('::')[-1] for f in fails)[:6])}"
              f"{' ...' if len(fails) > 6 else ''}")

    fails, rc, out = run_tests()
    print(f"\nafter restore: rc={rc}, failing={sorted(fails)}")
    if rc != 0:
        bad.append(("RESTORE", "suite not green after restore"))

    detected = len(MUTATIONS) - len([b for b in bad if b[0] != "RESTORE"])
    print(f"\n{detected}/{len(MUTATIONS)} mutations detected")
    for label, why in bad:
        print(f"  UNDETECTED/BROKEN: {label}: {why}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
