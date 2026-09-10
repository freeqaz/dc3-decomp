#!/usr/bin/env python3
"""Mutation harness for the per-unit object baselines: break one thing, assert RED.

`tests/test_object_baseline.py` carries an in-test negative control in every
case.  This is the second layer, in the same shape as
`tests/sabotage_objs_patched.py`: it edits the code under test one defect at a
time and asserts the NAMED cases go red.  A mutation nobody's test catches is
reported UNDETECTED and makes this exit non-zero -- as is a tree that is not
green again after every restore.

The mutations are chosen to be the plausible wrong implementations, not
scarecrows: dropping one side of the pair, collapsing the two directions into
one error, treating "no base object" as drift (which would be red on 1,234 of
2,224 units forever), reporting a truncated list as a total, and -- the one that
matters most -- letting a scan with NO recorded baseline read as "nothing is
stale", which is the state every pre-v18 scan is in.

Each mutation is a single unique-anchor string replacement, applied and reverted
in memory, so this is safe on a dirty tree.  A non-unique anchor is reported
rather than applied.

Run:  python3 tests/sabotage_object_baseline.py     (~2 min)
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OB = ROOT / "scripts" / "orchestrator" / "object_baseline.py"
CG = ROOT / "scripts" / "orchestrator" / "callee_gate.py"
DB = ROOT / "scripts" / "orchestrator" / "database.py"
VERIFY = ROOT / "scripts" / "verify_pattern_scan_current.py"
CENSUS = ROOT / "scripts" / "analysis" / "pattern_census.py"
TESTS = ROOT / "tests" / "test_object_baseline.py"

T_CURRENT = "test_an_untouched_tree_is_current_in_both_directions"
T_NOBASE = "test_a_unit_with_no_base_object_is_not_stale"
T_TARGET = "test_a_moved_target_object_is_named_with_direction_target"
T_BASE = "test_a_rebuilt_base_object_is_named_with_direction_base"
T_BOTH = "test_both_sides_moving_is_reported_as_both_and_exits_on_the_target_code"
T_SCOPE = "test_a_caller_can_ask_about_one_unit_while_the_tree_churns"
T_TRUNC = "test_the_stale_list_is_truncated_but_the_count_never_is"
T_UNFP = "test_a_scan_with_no_recorded_baseline_is_refused_not_believed"
T_OFF = "test_the_object_check_can_be_switched_off_and_says_so"
T_RACED = "test_a_raced_unit_is_recorded_and_reported_separately_from_staleness"
T_RACE2 = "test_race_units_compares_both_reads"
T_WRITE = "test_census_write_scan_records_the_baseline_it_measured"
T_SETS = "test_added_and_removed_units_are_named_rather_than_ignored"
T_EMPTY = "test_an_empty_unit_list_is_refused_rather_than_matching_everything"
T_QFLAG = "test_query_functions_flags_rows_whose_unit_moved"
T_QUNFP = "test_query_functions_flags_a_pre_v18_scan_rather_than_reporting_clean"

# (label, file, old, new, [tests that MUST fail])
MUTATIONS = [
    ("M1  fingerprint is a constant (every tree matches)", OB,
     '            row[side] = sha256(p) if (p is not None and p.is_file()) else None',
     '            row[side] = "constant"',
     [T_TARGET, T_BASE, T_BOTH]),

    ("M2  compare_fingerprints reports drift unconditionally", OB,
     '    stale: dict[str, str] = {}\n    for unit in sorted',
     '    stale: dict[str, str] = {"default/alpha": "base"}\n    for unit in sorted',
     [T_CURRENT]),

    ("M3  the TARGET side is not hashed at all", OB,
     'SIDES = ("target", "base")',
     'SIDES = ("base",)',
     [T_TARGET, T_BOTH]),

    ("M4  the BASE side is not hashed at all", OB,
     'SIDES = ("target", "base")',
     'SIDES = ("target",)',
     [T_BASE]),

    ("M5  an absent object counts as drift (1,234 units red forever)", OB,
     '        moved = [s for s in SIDES if was.get(s) != now.get(s)]',
     '        moved = [s for s in SIDES if was.get(s) != now.get(s)\n'
     '                 or was.get(s) is None]',
     [T_NOBASE]),

    ("M6  only the recorded units are compared (a NEW unit is invisible)", OB,
     '    for unit in sorted(set(recorded) | set(live)):',
     '    for unit in sorted(set(recorded)):',
     [T_SETS]),

    ("M7  an empty unit list is fingerprinted happily", OB,
     '    if not units:\n        raise ObjectBaselineError(\n'
     '            f"{cfg} lists ZERO units',
     '    if False:\n        raise ObjectBaselineError(\n'
     '            f"{cfg} lists ZERO units',
     [T_EMPTY]),

    ("M8  the stale COUNT is the truncated list's length", OB,
     '    head = (f"{indent}{len(stale)} of the scan\'s units have moved since it ran: "',
     '    head = (f"{indent}{min(len(stale), limit)} of the scan\'s units have moved since it ran: "',
     [T_TRUNC]),

    ("M9  race_units never finds a race", OB,
     '    return {u for u in set(before) | set(after) if before.get(u) != after.get(u)}',
     '    return set()',
     [T_RACE2]),

    ("M10 ensure_current_scan stops checking the objects", CG,
     '    if check_objects:\n        # The tree the scan MEASURED',
     '    if False:\n        # The tree the scan MEASURED',
     [T_TARGET, T_BASE, T_BOTH, T_SCOPE, T_TRUNC, T_UNFP, T_OFF]),

    ("M11 both directions collapse into the target error", CG,
     '    if target_side:\n        raise StaleTargetObjectsError(',
     '    if target_side or base_side:\n        raise StaleTargetObjectsError(',
     [T_BASE]),

    ("M12 the message drops the base-side count", CG,
     '            f"{len(target_side)} on the TARGET side, {len(base_side)} on the BASE side"',
     '            f"{len(target_side)} on the TARGET side"',
     [T_BASE, T_BOTH]),

    ("M13 `units=` scoping is ignored (whole-tree verdict)", CG,
     '    scoped = ({u: d for u, d in stale.items() if u in units}\n'
     '              if units is not None else stale)',
     '    scoped = stale',
     [T_SCOPE]),

    ("M14 a scan with NO baseline reads as 'nothing is stale'", CG,
     '    recorded = recorded_units(db, scan_id)\n    if not recorded:\n'
     '        raise UnfingerprintedPatternScanError(',
     '    recorded = recorded_units(db, scan_id)\n    if not recorded:\n'
     '        return {}\n    if False:\n'
     '        raise UnfingerprintedPatternScanError(',
     [T_UNFP]),

    ("M15 raced rows are not reported on read", CG,
     '    return sorted(u for u, r in recorded_units(db, scan_id).items() if r["raced"])',
     '    return []',
     [T_RACED]),

    ("M16 the census records raced = 0 for everything", CENSUS,
     '    unit_rows = [(scan_id, unit, h.get("target"), h.get("base"),\n'
     '                  1 if unit in raced else 0)',
     '    unit_rows = [(scan_id, unit, h.get("target"), h.get("base"), 0)',
     [T_WRITE]),

    ("M17 the census records no baseline at all", CENSUS,
     '    conn.executemany("INSERT OR REPLACE INTO pattern_scan_units "',
     '    unit_rows = []\n    conn.executemany("INSERT OR REPLACE INTO pattern_scan_units "',
     [T_WRITE]),

    ("M18 query_functions serves pattern rows with no currency", DB,
     '    if objdiff_pattern and stale_units != "ignore":\n'
     '        out = _attach_unit_currency(conn, pattern_scan_id, out,',
     '    if False:\n'
     '        out = _attach_unit_currency(conn, pattern_scan_id, out,',
     [T_QFLAG, T_QUNFP]),

    ("M19 a pre-v18 scan's rows are served unflagged", DB,
     '    if not recorded:\n        for r in rows:\n'
     '            r["unit_objects_stale"] = "unfingerprinted"\n        return rows',
     '    if not recorded:\n        return rows',
     [T_QUNFP]),

    ("M20 --no-check-objects stops saying it skipped the check", VERIFY,
     '                print("  objects   : NOT CHECKED (--no-check-objects): this says "',
     '                print("  objects   : ok "',
     [T_OFF]),

    ("M21 the target direction exits on the base code", VERIFY,
     '            print(f"STALE TARGET OBJECTS ({a.ruler}): {e}", file=sys.stderr)\n'
     '            return 4',
     '            print(f"STALE TARGET OBJECTS ({a.ruler}): {e}", file=sys.stderr)\n'
     '            return 5',
     [T_TARGET, T_BOTH, T_OFF]),
]


def failing_tests():
    p = subprocess.run(
        [sys.executable, "-m", "pytest", str(TESTS), "-q", "--no-header",
         "-p", "no:cacheprovider", "--tb=no"],
        cwd=str(ROOT), capture_output=True, text=True)
    out = p.stdout + p.stderr
    fails = set(re.findall(r"^(?:FAILED|ERROR)(?:\([^)]*\))? [^ ]*::(\S+)", out, re.M))
    return fails, p.returncode, out


def main() -> int:
    base_fails, base_rc, base_out = failing_tests()
    if base_rc != 0 or base_fails:
        print("BASELINE IS NOT GREEN -- refusing to interpret any mutation.")
        print(base_out[-3000:])
        return 1
    print("baseline: green\n")

    bad = []
    for label, path, old, new, must_fail in MUTATIONS:
        original = path.read_text()
        if original.count(old) != 1:
            print(f"[{label}] SKIP-ERROR: anchor appears "
                  f"{original.count(old)} times in {path.name}")
            bad.append((label, "anchor not unique"))
            continue
        path.write_text(original.replace(old, new, 1))
        try:
            fails, rc, out = failing_tests()
        finally:
            path.write_text(original)
        missed = [t for t in must_fail if not any(t in f for f in fails)]
        status = "OK " if (rc != 0 and not missed) else "BAD"
        if status == "BAD":
            bad.append((label, f"rc={rc} missed={missed}"))
        print(f"[{status}] {label}\n"
              f"        -> {len(fails)} failing: "
              f"{', '.join(sorted(f.split('::')[-1] for f in fails)[:5])}"
              f"{' ...' if len(fails) > 5 else ''}")

    fails, rc, out = failing_tests()
    print(f"\nafter restore: rc={rc}, failing={sorted(fails)}")
    if rc != 0:
        bad.append(("RESTORE", "tree not green after restore"))

    print(f"\n{len(MUTATIONS) - len([b for b in bad if b[0] != 'RESTORE'])}"
          f"/{len(MUTATIONS)} mutations detected")
    for label, why in bad:
        print(f"  UNDETECTED/BROKEN: {label}: {why}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
