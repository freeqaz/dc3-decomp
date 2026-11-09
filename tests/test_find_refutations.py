"""Tests for scripts/find_refutations.py.

The property that matters is NOT "it finds notes" -- it is that it can
distinguish the three states apart:

    found notes      (0)
    looked, nothing  (1)
    could not look   (2)

Because the failure this tool exists to prevent is a lane working a row whose
refutation is already written down. A tool that answers "no notes" when it
actually failed to resolve the symbol would cause exactly that, so `1` and `2`
are separate and both are asserted below.

Every test writes its own temp TU; nothing here reads the real tree.
"""

import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
TOOL = REPO / "scripts" / "find_refutations.py"


def run(*args):
    p = subprocess.run(
        [sys.executable, str(TOOL), *args], capture_output=True, text=True, timeout=60
    )
    return p.returncode, p.stdout + p.stderr


def write(tmp_path, body):
    # `--file` mode, so no DB or unit-name mapping is involved
    f = tmp_path / "Tu.cpp"
    f.write_text(body)
    return str(f)


def test_note_inside_the_body_is_found(tmp_path):
    f = write(tmp_path, """
void Foo::Bar() {
    // w21-bh: flipping both operands is byte-identical (measured)
    a = b * c;
}
""")
    rc, out = run("?Bar@Foo@@QAAXXZ", "--file", f)
    assert rc == 0
    assert "w21-bh" in out


def test_note_adjacent_above_outranks_a_note_far_above(tmp_path):
    """The block directly above the definition is this function's; the one 20
    lines up belongs to the previous function and must sort AFTER it."""
    f = write(tmp_path, """
// w10-aa: this note belongs to the PREVIOUS function
void Previous() {
    x = 1;
}



void Foo::Bar() {
    y = 2;
}
// w26-r: this one is Bar's own residual note
""")
    rc, out = run("?Bar@Foo@@QAAXXZ", "--file", f)
    assert rc == 0
    # both are reported (we do not drop the window), but Bar's own comes first
    assert out.index("w26-r") < out.index("w10-aa")


def test_adjacent_above_ranks_before_inside_the_body(tmp_path):
    f = write(tmp_path, """
// w7-at: adjacent above the definition
void Foo::Bar() {
    // w22-a16: inside the body
    y = 2;
}
""")
    rc, out = run("?Bar@Foo@@QAAXXZ", "--file", f)
    assert rc == 0
    assert out.index("w7-at") < out.index("w22-a16")


def test_file_with_no_notes_exits_1_and_says_so(tmp_path):
    f = write(tmp_path, """
void Foo::Bar() {
    y = 2;
}
""")
    rc, out = run("?Bar@Foo@@QAAXXZ", "--file", f)
    assert rc == 1, out
    assert "NO LANE NOTES FOUND" in out
    assert "NOT proof" in out  # never claims the row is unexplored


def test_unresolvable_target_exits_2_not_1(tmp_path):
    """`2` must be reachable and distinct from `1`, or a failed lookup reads as
    'clean, go ahead'."""
    rc, out = run("--file", str(tmp_path / "absent.cpp"))
    assert rc == 2, out
    assert "CANNOT LOOK" in out


def test_window_limits_the_scan_but_all_does_not(tmp_path):
    far = "// w99-zz: far away note\n" + "\n" * 400
    f = write(tmp_path, far + "void Foo::Bar() {\n    y = 2;\n}\n")
    rc_windowed, _ = run("?Bar@Foo@@QAAXXZ", "--file", f)
    rc_all, out_all = run("?Bar@Foo@@QAAXXZ", "--file", f, "--all")
    assert rc_windowed == 1  # outside the default window
    assert rc_all == 0 and "w99-zz" in out_all


def test_short_name_from_mangled_and_demangled():
    sys.path.insert(0, str(REPO / "scripts"))
    import importlib

    mod = importlib.import_module("find_refutations")
    assert mod._short_name("?RndScaleObject@@YAXPAVObject@Hmx@@MM@Z") == "RndScaleObject"
    assert mod._short_name("??$MakeString@H@@YAPBDPBDABH@Z") == "MakeString"
    assert (
        mod._short_name("public: void __cdecl RndScaleObject(class Hmx::Object *, float)")
        == "RndScaleObject"
    )
