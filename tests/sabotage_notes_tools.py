#!/usr/bin/env python3
"""Mutation harness: break the two notes tools one way at a time, require RED.

`tests/test_migrate_notes.py` and `tests/test_verify_comment_move.py` carry
their own negative controls.  This proves those controls can fail: each
mutation edits `scripts/notes/migrate_notes.py` or
`scripts/notes/verify_comment_move.py` in place, runs the suite, and requires
at least one of the NAMED tests to fail.  A mutation no test catches is
reported UNDETECTED and makes this exit 1; so does a final clean run that is
not green.  Restores are in-memory single-anchor replacements, so this is safe
on a dirty tree; a non-unique anchor is reported, never applied.

Run:  python3 tests/sabotage_notes_tools.py     (~15 s)
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MN = ROOT / "scripts" / "notes" / "migrate_notes.py"
VG = ROOT / "scripts" / "notes" / "verify_comment_move.py"
TESTS = ["tests/test_migrate_notes.py", "tests/test_verify_comment_move.py"]

# (label, file, [(old, new), ...], [test names, any of which must fail])
MUTATIONS = [
    ("G1 relocations ignored (a wrong callee with identical bytes passes)", VG,
     [("rel = sorted((va, typ, _target(symbols, sections, si, names))\n"
       "                     for va, si, typ in s[\"relocs\"])",
       "rel = []"),
      ("rel = sorted((va - lo, typ, _target(symbols, sections, si, names))\n"
       "                         for va, si, typ in sec[\"relocs\"] if lo <= va < hi)",
       "rel = []")],
     ["test_wrong_callee_same_bytes_is_refused"]),
    ("G2 symbol and section bytes ignored", VG,
     [("hi - lo, _h(body), rel", "hi - lo, 0, rel"),
      ('s["sel"], _h(s["data"]), rel', 's["sel"], 0, rel')],
     ["test_one_byte_code_change_is_refused"]),
    ("G3 report.json not compared", VG,
     [("diffs += compare_reports(", "diffs += [] or (")],
     ["test_report_measure_change_is_refused"]),
    ("G4 vacuity check off (unrebuilt objects compared with themselves)", VG,
     [("    if stale:\n", "    if False:\n")],
     ["test_unrebuilt_object_is_vacuous_not_a_pass"]),
    ("G5 section ORDER counted (the pilot's benign COMDAT shuffle refused)", VG,
     [("    sec_fps.sort()\n", "")],
     ["test_section_order_shuffle_is_not_a_difference"]),
    ("G6 unreadable snapshot reads as a pass", VG,
     [('print(f"CANNOT VERIFY: snapshot unreadable: {e}", file=sys.stderr)\n        return 2',
       'print(f"CANNOT VERIFY: snapshot unreadable: {e}", file=sys.stderr)\n        return 0')],
     ["test_unreadable_snapshot_is_2"]),
    ("G7 numbered labels keyed by raw name (a renumbering reads as a change)", VG,
     [("        m = NUMBERED_LABEL_RE.match(s[\"name\"]) if s[\"sec\"] > 0 and s[\"cls\"] != 2 else None",
       "        m = None")],
     ["test_renumbered_compiler_labels_are_not_a_difference"]),
    ("G8 labels cut function ranges (bytes after a label escape the function)", VG,
     [('starts = sorted({s["value"] for s in lst if not s["key"].startswith("<")})',
       'starts = sorted({s["value"] for s in lst})')],
     ["test_a_label_does_not_hide_the_function_bytes_after_it"]),
    ("G9 label identity ignores its offset", VG,
     [("|{_h(sec['data'])}+{s['value']:#x}>\")", "|{_h(sec['data'])}>\")")],
     ["test_a_label_that_moves_is_a_difference"]),

    ("M1 lane tag not required (documentation saying 'inert' is moved)", MN,
     [("or not LANE_HISTORY_RE.search(", "or not True or LANE_HISTORY_RE.search(")],
     ["test_documentation_and_short_notes_survive"]),
    ("M2 a note inside a body is attributed to the NEXT definition", MN,
     [("inside = [d for d in defs if d.sig <= b.mstart and b.end <= d.close]", "inside = []")],
     ["test_attribution_inside_above_overload_macro_file_level"]),
    ("M3 stub marker not honoured (re-run moves the stub again)", MN,
     [("        if any(MARKER_RE.match(l) for l in bl):\n            continue",
       "        if False:\n            continue")],
     ["test_stub_is_never_moved_again_even_below_min_lines"]),
    ("M4 a quote may be a truncated sentence", MN,
     [("return w if 0 < len(w) <= nlines and all(len(x) <= width for x in w) else None",
       "return w[:nlines] if w else None")],
     ["test_stub_is_short_verbatim_and_keeps_verdicts"]),
    ("M5 verdict negation dropped ('NOT a behaviour bug' -> 'behaviour bug')", MN,
     [(r'r"(?:\bnot\s+(?:a\s+)?)?\bbehaviou?r(?:al)?\s+bug\b|', r'r"\bbehaviou?r(?:al)?\s+bug\b|')],
     ["test_stub_is_short_verbatim_and_keeps_verdicts"]),
    ("M6 last line of each block lost in the sidecar, self-check off", MN,
     [('out.append("```text\\n" + b.text + "\\n```\\n")',
       'out.append("```text\\n" + b.text.rsplit("\\n", 1)[0] + "\\n```\\n")'),
      ("            raise SystemExit(f\"SELF-CHECK FAILED: block at",
       "            print(f\"SELF-CHECK FAILED: block at")],
     ["test_nothing_is_lost_and_nothing_else_changes", "test_find_refutations_answers_as_before"]),
    ("M7 directive lines not refused", MN,
     [('b.refused = "carries a formatter/lint directive line"', "pass")],
     ["test_directive_block_is_refused_not_moved"]),
    ("M8 --exclude-symbol ignored (pilot sections re-moved)", MN,
     [("        if b.symbol in exclude:\n", "        if False:\n")],
     ["test_existing_sidecar_is_kept_byte_for_byte_and_pilot_symbols_untouched"]),
    ("M9 overloads not split by parameter count", MN,
     [("        if len(same) == 1:\n            return same[0][0], \"\"",
       "        if False:\n            return same[0][0], \"\""),
      ("        pool2 = same or cands\n", "        pool2 = cands\n")],
     ["test_attribution_inside_above_overload_macro_file_level"]),
    ("M10 --check always green", MN,
     [("        return 1 if bad else 0", "        return 0")],
     ["test_check_refuses_a_wall_and_names_it"]),
]


def run_tests():
    p = subprocess.run([sys.executable, "-m", "pytest", "-q", "-p", "no:cacheprovider",
                        "-rfE", *TESTS], cwd=ROOT, capture_output=True, text=True, timeout=300)
    # ERROR counts too, but is reported as such: a sabotage that merely CRASHES
    # the module is not evidence that a named test can see the defect.
    failed = {l.split("::")[-1].split(" ")[0] for l in p.stdout.splitlines()
              if l.startswith("FAILED")}
    if any(l.startswith("ERROR") for l in p.stdout.splitlines()):
        failed.add("<collection ERROR>")
    return p.returncode, failed, p.stdout


def main():
    undetected, bad_anchor = [], []
    for label, path, edits, must in MUTATIONS:
        orig = path.read_text()
        text = orig
        ok = True
        for old, new in edits:
            if text.count(old) != 1:
                bad_anchor.append(f"{label}: anchor occurs {text.count(old)}x: {old[:60]!r}")
                ok = False
                break
            text = text.replace(old, new)
        if not ok:
            continue
        try:
            path.write_text(text)
            rc, failed, out = run_tests()
        finally:
            path.write_text(orig)
        hit = sorted(failed & set(must))
        state = "caught" if hit else "UNDETECTED"
        if not hit:
            undetected.append(label)
        print(f"{state:10s} {label}  -> {', '.join(hit) or '(none of ' + ', '.join(must) + ')'}")
    rc, failed, out = run_tests()
    print(f"control (restored tools): pytest exit {rc}, {len(failed)} failing")
    for b in bad_anchor:
        print(f"BAD ANCHOR {b}")
    n = len(MUTATIONS)
    caught = n - len(undetected) - len(bad_anchor)
    print(f"{caught} of {n} mutations caught")
    return 1 if (undetected or bad_anchor or rc != 0) else 0


if __name__ == "__main__":
    sys.exit(main())
