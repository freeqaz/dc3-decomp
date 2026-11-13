"""Tests for scripts/notes/check_no_lane_walls.py (keeps lane history out of src/).

The live-tree test is the enforcement half of this file: it fails when anyone
writes a new lane-note wall into src/.  The synthetic tests are its negative
controls -- each proves a direction of the check can actually fail -- and the
wiring test proves the check is reached by a full `ninja`, not just documented.
"""

import re
import subprocess
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[1]
TOOL = REPO / "scripts" / "notes" / "check_no_lane_walls.py"
sys.path.insert(0, str(REPO / "scripts" / "notes"))
import check_no_lane_walls as chk  # noqa: E402
import migrate_notes as mn  # noqa: E402

WALL = """\
// w31-k: tried the swapped operand order, measured inert (22 variants).
// The residual is one mr copy the register allocator places early.
// Do not retry the declaration reorder: w29-q already measured it.
// floor: placement tie, not a behaviour bug.
// see docs/decomp/patterns/stack-slot-sharing.md
"""
DOC = """\
// This table is load-bearing and MEASURED: the inert entries keep the
// layout identical to the target, which the residual depends on.
// Do not reorder it -- the floor is the struct size, not a placement tie.
// (Documentation, no lane tag: it stays in the source.)
// See Mat.h for the field list.
"""
SHORT = """\
// w31-k: measured inert.
// floor: placement tie.
"""


def run(root, *args):
    p = subprocess.run([sys.executable, str(TOOL), "--repo", str(root), *args],
                       capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout + p.stderr


def tree(tmp_path, files, allowance=None):
    root = tmp_path / "t"
    for rel, text in files.items():
        p = root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
    (root / "src").mkdir(parents=True, exist_ok=True)
    if allowance is not None:
        a = root / "scripts" / "notes" / "lane_walls_allowed.txt"
        a.parent.mkdir(parents=True, exist_ok=True)
        a.write_text(allowance)
    return root


def test_live_tree_has_no_new_lane_walls():
    rc, out = run(REPO)
    # 3 = an allowance is stale (a wall moved without --write): reported, and
    # non-fatal in ninja, but not a new wall.  1 is the failure this guards.
    assert rc in (0, 3), out


def test_new_wall_in_a_new_file_is_refused_and_named(tmp_path):
    root = tree(tmp_path, {"src/a/New.cpp": "int x;\n" + WALL + "int f() { return 1; }\n"})
    rc, out = run(root)
    assert rc == 1, out
    assert "src/a/New.cpp:2-6" in out and "docs/decomp/notes/a/New.md" in out


def test_documentation_short_notes_and_markers_pass(tmp_path):
    marker = ("// Lane history (12 lines moved): docs/decomp/notes/a/M.md\n"
              "// > w31-k: tried the swapped operand order, measured inert.\n"
              "// > Verdicts: do not retry; NOT a behaviour bug\n")
    root = tree(tmp_path, {"src/a/Doc.cpp": DOC + "int f();\n",
                           "src/a/Short.h": SHORT + "int g();\n",
                           "src/a/M.cpp": marker + "int h();\n",
                           "src/a/Block.cpp": "/*\n" + WALL + "*/\nint k();\n"})
    rc, out = run(root)
    assert rc == 0, out


def test_allowance_is_a_ratchet(tmp_path):
    files = {"src/a/Old.cpp": WALL + "int f();\n"}
    rc, out = run(tree(tmp_path / "1", files, "src/a/Old.cpp 1  # refused: directive\n"))
    assert rc == 0, out
    files2 = {"src/a/Old.cpp": WALL + "int f();\n\n" + WALL + "int g();\n"}
    rc, out = run(tree(tmp_path / "2", files2, "src/a/Old.cpp 1  # refused\n"))
    assert rc == 1, out  # a second wall in an allowed file is still new
    rc, out = run(tree(tmp_path / "3", {"src/a/Old.cpp": "int f();\n"},
                       "src/a/Old.cpp 1  # refused\n"))
    assert rc == 3, out  # moved without locking it in: stale


def test_missing_allowance_is_strict_and_malformed_cannot_run(tmp_path):
    rc, out = run(tree(tmp_path / "1", {"src/a/Old.cpp": WALL + "int f();\n"}))
    assert rc == 1, out
    rc, out = run(tree(tmp_path / "2", {"src/a/Old.cpp": "int f();\n"}, "budget: 3\n"))
    assert rc == 2, out


def test_write_records_current_walls_and_keeps_reasons(tmp_path):
    root = tree(tmp_path, {"src/a/Old.cpp": WALL + "int f();\n"},
                "src/a/Old.cpp 2  # refused: carries a directive\n")
    rc, out = run(root, "--write")
    assert rc == 0, out
    text = (root / "scripts" / "notes" / "lane_walls_allowed.txt").read_text()
    assert "src/a/Old.cpp 1  # refused: carries a directive" in text
    assert run(root)[0] == 0


def _full_scan(root):
    full = {}
    for p in sorted((root / "src").rglob("*")):
        if p.suffix not in chk.EXTS or not p.is_file():
            continue
        lines = p.read_text(encoding="utf-8", errors="replace").split("\n")
        n = len(mn.movable_blocks(lines, mn.lex(lines)))
        if n:
            full[str(p.relative_to(root))] = n
    return full


def test_prefilter_drops_no_file_the_detector_would_flag(tmp_path):
    # the scan skips files lacking a lane tag or a note keyword before lexing;
    # that must never hide a wall the full detector sees.  Synthetic (walls
    # deep in a file, in a header, two in one file) because after the move the
    # live tree has almost none, which would make the comparison vacuous.
    filler = "int pad%d;\n"
    root = tree(tmp_path, {
        "src/a/Deep.cpp": "".join(filler % i for i in range(400)) + WALL + "int f();\n",
        "src/a/Hdr.h": "#pragma once\n" + WALL + "struct S { int x; };\n",
        "src/a/Two.inl": WALL + "int f();\n\n" + WALL + "int g();\n",
        "src/a/Doc.cpp": DOC + "int h();\n"})
    full = _full_scan(root)
    assert full == {"src/a/Deep.cpp": 1, "src/a/Hdr.h": 1, "src/a/Two.inl": 2}
    assert {k: len(v) for k, v in chk.scan(root, mn.MIN_LINES).items()} == full
    live = {k: len(v) for k, v in chk.scan(REPO, mn.MIN_LINES).items()}
    assert live == _full_scan(REPO)


def test_full_ninja_reaches_the_check():
    """configure.py's post-compile chain carries the step, and the generated
    build.ninja hangs it under the post-compile phony REPORT depends on."""
    src = (REPO / "configure.py").read_text()
    assert "scripts/notes/check_no_lane_walls.py" in src
    ninja = REPO / "build.ninja"
    if not ninja.exists():
        pytest.skip("no build.ninja in this tree (run configure.py); configure.py checked only")
    text = re.sub(r"\$\n\s*", " ", ninja.read_text())  # join ninja line continuations
    m = re.search(r"^build (\S*lane_walls_checked\.stamp): run_script\b.*\n((?:  .*\n)+)",
                  text, re.M)
    assert m, "no lane_walls_checked edge in build.ninja -- re-run configure.py"
    assert "check_no_lane_walls.py" in m.group(2)
    phony = re.search(r"^build post-compile: phony (.*)$", text, re.M)
    assert phony and m.group(1) in phony.group(1), "edge not under the post-compile phony"
