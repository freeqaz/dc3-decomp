#!/usr/bin/env python3
"""Mutation harness: break one thing at a time, assert the named test goes RED.

`tests/test_objs_patched.py` carries an in-test negative control in every case.
This is the second layer: it proves those cases can actually FAIL, by editing
the code under test 25 different ways and asserting each named case reddens.
A mutation nobody's test catches is reported as UNDETECTED and makes this exit
non-zero -- including the final control, which re-runs the suite after every
restore and fails if the tree is not back to green.

Same role as `tests/sabotage_callee_gate.py`.  Unlike that one it does not use
`git checkout --` to restore, so it is safe on a dirty tree: each mutation is a
single unique-anchor string replacement, applied and reverted in memory.  A
non-unique anchor is reported rather than applied.

Run:  python3 tests/sabotage_objs_patched.py     (~20 s)
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VOP = ROOT / "scripts" / "verify_objs_patched.py"
META = ROOT / "scripts" / "obj_build_metadata_patcher.py"
IO = ROOT / "scripts" / "obj_patch_io.py"
TESTS = ROOT / "tests" / "test_objs_patched.py"

# (label, file, old, new, [tests that MUST fail])
MUTATIONS = [
    ("M1  emit records a constant hash", VOP,
     '"sha256": sha256(p), "size": st.st_size,',
     '"sha256": "c" * 64, "size": st.st_size,',
     ["EmitTest::test_emit_records_every_object_with_its_real_hash"]),

    ("M2  tree_sha256 folds mtime in", VOP,
     '"".join(f"{k}:{v[\'sha256\']}\\n" for k, v in sorted(entries.items()))',
     '"".join(f"{k}:{v[\'sha256\']}:{v[\'mtime_ns\']}\\n" for k, v in sorted(entries.items()))',
     ["EmitTest::test_tree_sha256_is_content_keyed_and_mtime_blind"]),

    ("M3  tree_sha256 drops the path, hashes the hashes", VOP,
     '"".join(f"{k}:{v[\'sha256\']}\\n" for k, v in sorted(entries.items()))',
     '"".join(f"{s}\\n" for s in sorted(v["sha256"] for v in entries.values()))',
     ["EmitTest::test_tree_sha256_binds_each_hash_to_its_path"]),

    ("M4  verify_manifest compares size only", VOP,
     'if st.st_size != ent["size"] or sha256(p) != ent["sha256"]:',
     'if st.st_size != ent["size"]:',
     ["VerifyManifestTest::test_drift_is_caught_even_when_size_is_unchanged",
      "VerifyManifestTest::test_content_drift_is_caught_and_restoring_clears_it"]),

    ("M5  verify_manifest adds an mtime rule", VOP,
     'if st.st_size != ent["size"] or sha256(p) != ent["sha256"]:',
     'if st.st_mtime_ns != ent["mtime_ns"] or st.st_size != ent["size"] or sha256(p) != ent["sha256"]:',
     ["VerifyManifestTest::test_mtime_alone_is_deliberately_not_drift"]),

    ("M6  an absent manifest exits 0", VOP,
     'f"verified patched.  Run `ninja` in it.", file=sys.stderr)\n        return 2',
     'f"verified patched.  Run `ninja` in it.", file=sys.stderr)\n        return 0',
     ["VerifyManifestTest::test_an_absent_manifest_is_a_refusal_not_a_pass"]),

    ("M7  unmanifested objects are ignored", VOP,
     '    extra = sorted(have - set(recorded))',
     '    extra = []',
     ["VerifyManifestTest::test_an_object_the_manifest_never_saw_is_caught"]),

    ("M8  a manifest of zero objects is accepted", VOP,
     '    if not recorded:',
     '    if False:',
     ["VerifyManifestTest::test_a_manifest_vouching_for_zero_objects_is_refused"]),

    ("M9  a deleted object is not reported", VOP,
     '        if not p.exists():\n            missing.append(rel)\n            continue',
     '        if not p.exists():\n            continue',
     ["VerifyManifestTest::test_a_deleted_object_is_caught_and_named"]),

    ("M10 emit() accepts an empty tree", VOP,
     'objs = require_non_empty(repo, "--emit")',
     'objs = objects(repo)',
     ["EmitTest::test_emit_refuses_an_empty_object_tree"]),

    ("M11 run_check() accepts an empty tree", VOP,
     'objs = require_non_empty(repo, "--check")',
     'objs = objects(repo)',
     ["RunCheckTest::test_run_check_refuses_an_empty_object_tree"]),

    ("M12 run_check() ignores patcher exit codes", VOP,
     '        if p.returncode != 0:',
     '        if False:',
     ["RunCheckTest::test_red_naming_the_one_pass_that_reports_pending"]),

    ("M13 PATCHERS gains a name with no file", VOP,
     '    "obj_build_metadata_patcher.py",\n]',
     '    "obj_build_metadata_patcher.py",\n    "obj_ghost_patcher.py",\n]',
     ["RunCheckTest::test_PATCHERS_names_real_files"]),

    ("M14 the metadata pass is no longer last", VOP,
     'MANIFEST_VERSION = 1',
     'PATCHERS = PATCHERS[-1:] + PATCHERS[:-1]\nMANIFEST_VERSION = 1',
     ["RunCheckTest::test_PATCHERS_names_real_files"]),

    ("M15 compile-edge check = whole-file grep", VOP,
     '    unwired = [r for r in COMPILE_RULES\n               if r in commands and COMPILE_EDGE_PASS not in commands[r]]',
     '    unwired = [] if COMPILE_EDGE_PASS in ninja_file.read_text() else list(COMPILE_RULES)',
     ["CompileEdgeTest::test_a_mention_elsewhere_in_the_file_does_not_count",
      "CompileEdgeTest::test_red_when_exactly_one_rule_loses_it"]),

    ("M16 an absent build.ninja exits 0", VOP,
     '"reports.", file=sys.stderr)\n        return EXIT_COMPILE_EDGE',
     '"reports.", file=sys.stderr)\n        return 0',
     ["CompileEdgeTest::test_absent_build_ninja_is_a_refusal_not_a_pass"]),

    ("M17 a missing compile rule is not reported", VOP,
     '    missing_rules = [r for r in COMPILE_RULES if r not in commands]',
     '    missing_rules = []',
     ["CompileEdgeTest::test_red_when_a_compile_rule_disappears_entirely"]),

    ("M18 ninja parser drops $-continuations", VOP,
     '        if raw.endswith("$") and not raw.endswith("$$"):\n            buf += raw[:-1]\n            continue',
     '        if False:\n            pass',
     ["CompileEdgeTest::test_dollar_continuations_are_joined_before_matching",
      "CompileEdgeTest::test_green_when_all_three_rules_carry_the_pass",
      "CompileEdgeTest::test_the_real_build_ninja_is_wired"]),

    ("M19 --obj writes nothing", META,
     '    if offsets:\n        write_patched_obj(str(path), normalize(data, offsets))',
     '    if False:\n        write_patched_obj(str(path), normalize(data, offsets))',
     ["ObjModeTest::test_obj_zeroes_both_fields_preserves_mtime_and_is_idempotent",
      "ObjModeTest::test_obj_check_mode_is_2_when_pending_and_0_when_clean",
      "VerifyManifestTest::test_a_per_target_rebuild_of_a_raw_object_is_caught_end_to_end"]),

    ("M20 --obj stops preserving mtime", IO,
     '    os.utime(path, ns=(st.st_atime_ns, st.st_mtime_ns))',
     '    pass',
     ["ObjModeTest::test_obj_zeroes_both_fields_preserves_mtime_and_is_idempotent"]),

    ("M21 --obj accepts non-objects", META,
     '        require_coff(path, data)',
     '        pass',
     ["ObjModeTest::test_obj_refuses_things_that_are_not_objects"]),

    ("M22 --obj --check always says clean", META,
     '    if args.check:\n        if offsets:',
     '    if args.check:\n        if False:',
     ["ObjModeTest::test_obj_check_mode_is_2_when_pending_and_0_when_clean"]),

    ("M23 --batch/--obj no longer exclusive", META,
     '    if args.batch and args.obj:',
     '    if False:',
     ["ObjModeTest::test_batch_and_obj_are_mutually_exclusive_and_neither_is_optional"]),

    ("M24 --obj becomes silently chatty on stdout", META,
     '    if offsets:\n        write_patched_obj(str(path), normalize(data, offsets))',
     '    print(f"normalizing {path}")\n    if offsets:\n        write_patched_obj(str(path), normalize(data, offsets))',
     ["ObjModeTest::test_obj_zeroes_both_fields_preserves_mtime_and_is_idempotent"]),

    ("M25 S_OBJNAME records stop being found", META,
     '    out = []\n    for praw, size in _debug_s_sections(data):',
     '    out = []\n    for praw, size in []:',
     ["SyntheticObjectSanityTest::test_fixture_carries_both_field_kinds",
      "ObjModeTest::test_obj_zeroes_both_fields_preserves_mtime_and_is_idempotent"]),
]


def failing_tests() -> set:
    p = subprocess.run(
        [sys.executable, "-m", "pytest", str(TESTS), "-q", "--no-header",
         "-p", "no:cacheprovider", "--tb=no"],
        cwd=str(ROOT), capture_output=True, text=True)
    out = p.stdout + p.stderr
    fails = set(re.findall(r"^(?:FAILED|ERROR|SUBFAILED)(?:\([^)]*\))? [^ ]*::(\S+)", out, re.M))
    if not fails:
        fails = set(re.findall(r"^\S*tests/test_objs_patched\.py::(\S+)", out, re.M))
    return fails, p.returncode, out


def main() -> int:
    base_fails, base_rc, base_out = failing_tests()
    if base_rc != 0 or base_fails:
        print("BASELINE IS NOT GREEN -- refusing to interpret any mutation.")
        print(base_out[-3000:])
        return 1
    print(f"baseline: green\n")

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
        missed = [t for t in must_fail
                  if not any(t.split("::")[-1] in f for f in fails)]
        status = "OK " if (rc != 0 and not missed) else "BAD"
        if status == "BAD":
            bad.append((label, f"rc={rc} missed={missed}"))
        print(f"[{status}] {label}\n"
              f"        -> {len(fails)} failing: "
              f"{', '.join(sorted(f.split('::')[-1] for f in fails)[:6])}"
              f"{' ...' if len(fails) > 6 else ''}")

    # Final control: the tree must be back to green after all restores.
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
