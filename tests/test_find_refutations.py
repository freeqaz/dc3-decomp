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


def test_anchor_prefers_the_definition_over_a_call_three_lines_earlier(tmp_path):
    """Regression: a call that ends in `;` must NOT be treated as a definition
    just because the NEXT function's `{` is a few lines below. That bug anchored
    ArcDetector::GetSwipeAmount at line 373 (inside IsPathAcceptable) instead of
    263, and the window then covered an unrelated function."""
    body = """bool ArcDetector::IsPathAcceptable() const {
    return GetSwipeAmount() > 0.0f;
}

float ArcDetector::GetSwipeAmount() const {
    // w21-n: this is the note that matters
    return mSwipeExtentX;
}
"""
    f = write(tmp_path, body)
    rc, out = run("?GetSwipeAmount@ArcDetector@@QBAMXZ", "--file", f)
    assert rc == 0, out
    def_line = body.splitlines().index("float ArcDetector::GetSwipeAmount() const {") + 1
    assert f"function at line {def_line}" in out, out
    assert "w21-n" in out


# --- sidecar notes (docs/decomp/notes/<unit>.md) -----------------------------
#
# Lane history can move out of the source into a per-unit sidecar keyed by
# mangled symbol.  The property that matters: a function whose notes MOVED
# answers exactly as before -- exit 0, the same note text -- while 1 and 2 stay
# distinct.  Each test builds its own <tmp>/src/... and <tmp>/docs/decomp/notes/...

SYM = "?Bar@Foo@@QAAXXZ"
ABOVE = """// w21-bh: flipping both operands is byte-identical (measured)
// w26-r: all 8 multiply orders REFUTED, same rows"""
INSIDE = """    // w28-c: hoisting the load is inert -- do not retry"""


def tu(tmp_path, body, rel="system/x/Tu"):
    f = tmp_path / "src" / (rel + ".cpp")
    f.parent.mkdir(parents=True, exist_ok=True)
    f.write_text(body)
    return f


def sidecar(tmp_path, sections, rel="system/x/Tu"):
    """sections: {mangled: [block_text, ...]} -> the pilot's sidecar format."""
    f = tmp_path / "docs" / "decomp" / "notes" / (rel + ".md")
    f.parent.mkdir(parents=True, exist_ok=True)
    out = ["# Lane notes", ""]
    for sym, blocks in sections.items():
        out += [f"## `{sym}`", "", "moved verbatim", ""]
        for b in blocks:
            out += ["```text", b, "```", ""]
    f.write_text("\n".join(out))
    return f


def test_notes_moved_to_the_sidecar_answer_exactly_as_before(tmp_path):
    before = tu(tmp_path, f"\n{ABOVE}\nvoid Foo::Bar() {{\n{INSIDE}\n    y = 2;\n}}\n")
    rc0, out0 = run(SYM, "--file", str(before), "--max-chars", "0")
    assert rc0 == 0, out0
    assert ABOVE in out0 and INSIDE in out0

    # the move: source keeps a plain comment that matches no note keyword
    after = tu(tmp_path, "\n// see docs/decomp/notes\nvoid Foo::Bar() {\n    y = 2;\n}\n")
    sc = sidecar(tmp_path, {SYM: [ABOVE, INSIDE]})
    rc1, out1 = run(SYM, "--file", str(after), "--max-chars", "0")
    assert rc1 == 0, out1
    assert ABOVE in out1 and INSIDE in out1, out1
    assert str(sc) in out1
    assert "2 sidecar" in out1

    # control: the same source WITHOUT the sidecar is genuinely note-free
    sc.unlink()
    rc2, out2 = run(SYM, "--file", str(after))
    assert rc2 == 1, out2


def test_sidecar_notes_print_before_source_notes(tmp_path):
    f = tu(tmp_path, "void Foo::Bar() {\n    // w7-at: in the body\n    y = 2;\n}\n")
    sidecar(tmp_path, {SYM: ["// w99-sc: from the sidecar"]})
    rc, out = run(SYM, "--file", str(f))
    assert rc == 0, out
    assert out.index("w99-sc") < out.index("w7-at")


def test_another_functions_sidecar_section_is_not_this_ones(tmp_path):
    """A sidecar that only holds a DIFFERENT function's notes must leave this
    function at exit 1 -- otherwise the sidecar manufactures a refutation."""
    f = tu(tmp_path, "void Foo::Bar() {\n    y = 2;\n}\n")
    sidecar(tmp_path, {"?Other@Foo@@QAAXXZ": ["// w5-zz: Other's note"]})
    rc, out = run(SYM, "--file", str(f))
    assert rc == 1, out
    assert "w5-zz" not in out


def test_exact_symbol_beats_short_name_overloads(tmp_path):
    f = tu(tmp_path, "void Foo::Bar() {\n    y = 2;\n}\nvoid Baz::Bar() {}\n")
    sidecar(tmp_path, {SYM: ["// w1-foo: Foo's"], "?Bar@Baz@@QAAXXZ": ["// w1-baz: Baz's"]})
    rc, out = run(SYM, "--file", str(f))
    assert rc == 0 and "w1-foo" in out and "w1-baz" not in out, out


def test_unresolvable_target_is_still_2_even_with_a_sidecar(tmp_path):
    sidecar(tmp_path, {SYM: ["// w3-a: orphaned note"]})
    rc, out = run(SYM, "--file", str(tmp_path / "src" / "system" / "x" / "Absent.cpp"))
    assert rc == 2, out
    assert "CANNOT LOOK" in out


def test_explicit_notes_root(tmp_path):
    f = tu(tmp_path, "void Foo::Bar() {\n    y = 2;\n}\n")
    elsewhere = tmp_path / "elsewhere"
    sc = elsewhere / "system" / "x" / "Tu.md"
    sc.parent.mkdir(parents=True)
    sc.write_text(f"## `{SYM}`\n\n```text\n// w4-root: via --notes-root\n```\n")
    rc, out = run(SYM, "--file", str(f), "--notes-root", str(elsewhere))
    assert rc == 0 and "w4-root" in out, out
