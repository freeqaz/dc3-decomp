"""Tests for scripts/notes/migrate_notes.py (lane-note walls -> sidecars).

What has to hold, each with a control that would fail if the tool cheated:
  * nothing is lost: every moved line is in a sidecar fence, verbatim, and the
    new source is the old source with exactly the moved ranges replaced;
  * documentation stays: untagged prose, short notes, a leading doc paragraph,
    `/* */` interiors and macro continuation lines are never touched;
  * attribution: a note inside a body files under THAT function's mangled
    symbol, one above a definition under the definition below it, overloads
    split by parameter count, BEGIN_COPYS bodies under `T::Copy`, and a note
    above no definition under `(file-level)` -- each saying so;
  * idempotent: a second run changes no byte;
  * the stub is <= 4 lines, quotes only verbatim text, keeps verdicts;
  * find_refutations answers for a moved function exactly as before.

Every test builds a throwaway tree (src/, objdiff.json, report.json).
"""

import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
TOOL = REPO / "scripts" / "notes" / "migrate_notes.py"
FIND = REPO / "scripts" / "find_refutations.py"
sys.path.insert(0, str(REPO / "scripts" / "notes"))
sys.path.insert(0, str(REPO / "scripts"))
import migrate_notes as mn  # noqa: E402
from find_refutations import parse_sidecar  # noqa: E402

REL = "src/system/x/Tu.cpp"
SIDE = "docs/decomp/notes/system/x/Tu.md"

FUNCS = [
    ("?Bar@Foo@@QAAXXZ", "public: void __cdecl Foo::Bar(void)"),
    # (int,int) listed FIRST: a resolver that ignores parameter count and takes
    # the first candidate files Baz(int)'s notes under the wrong overload
    ("?Baz@Foo@@QAAXHH@Z", "public: void __cdecl Foo::Baz(int,int)"),
    ("?Baz@Foo@@QAAXH@Z", "public: void __cdecl Foo::Baz(int)"),
    ("?Copy@Foo@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z",
     "public: virtual void __cdecl Foo::Copy(class Hmx::Object const *,enum Hmx::Object::CopyType)"),
    ("?Free@@YAXXZ", "void __cdecl Free(void)"),
]

WALL_ABOVE = """// w21-z (97.98, 11 rows): the image keeps two pointers where we keep one.
// Fifteen standalone spellings keep ONE pointer: direct, ref, pointer, hoisted.
// w28-c: the image derives the element as `subi r30, r31, 0x10` inside the loop;
// that single choice is the whole residual, and every load resolves the same.
// w29-q: keeping the explicit `> 0x20` test is load-bearing because the image carries
// `cmplwi 0x20 / ble` and a switch drops it, which costs four points and changes the
// branch order in a way that every later spelling inherited from the first one tried.
// w-this-adjust (2026-11-13): POINTER AUDIT, NOT A BUG -- same address passed.
// Hypothesis (not measured): c1 canonicalises on the first element address?"""

WALL_INSIDE = """    // w7-bs: Until 2026-09-15 this notify sat after the nested ifs, so a
    // zero-area face printed a decompilation-introduced message the image
    // never emits.  Moving it inside the UV test fixed behaviour and matched.
    // w27-l: measured inert at 100.0: hoisting the face pointer, a const ref.
    // w27-m: also inert, a do/while; do not retry the loop shapes.
    // w27-n: NOT a behaviour bug -- the message order is the image's."""

DOC = """// Builds the cylinder mesh used by the debug drawer.  The vertex order is
// the one RndMesh::Sync expects: ring by ring, bottom to top, and the result
// is cached in sCylinderMesh until the directory is torn down, which is
// load-bearing for UtilDrawCylinder's caller, and inert otherwise.  No lane
// tags here, so this is documentation even though it says "inert"."""

SHORT = """// w9-q: inert at 100.0 (two spellings).
// w9-r: same."""


def source():
    return f"""#include "x.h"

{DOC}
void Free() {{
    int a = 1;
}}

{WALL_ABOVE}
void Foo::Bar() {{
    int x = 0;
{WALL_INSIDE}
    x++;
}}

// w30-a: the int overload is the one with the long story.  It is the one that
// carries the residual mechanism, measured over eight spellings this lane.
// w30-b: every one of them byte-identical; the two-arg overload is fine.
// w30-c: a third line of history about the int overload, inert as well.
// w30-d: and a fourth, so that this block is long enough to move.
void Foo::Baz(int a) {{
{SHORT.replace("//", "    //")}
}}

void Foo::Baz(int a, int b) {{}}

/* block comment
// w1-a: this line is inside a block comment and is not a line comment at all
// w1-b: neither is this one
// w1-c: nor this
// w1-d: nor this
// w1-e: nor this
*/

#define LONG_MACRO(x) \\
    do {{ x; }} while (0)

BEGIN_COPYS(Foo)
    CREATE_COPY(Foo)
    // w40-a: the copy order is the image's: mA, then mB, measured on both
    // sides; swapping them is 93.1 and was reverted.  w40-b: confirmed inert
    // for a named temp.  w40-c: a cast at the use is inert as well, and the
    // last row is an ICF fold, NOT a bug.
    // w40-d: stopped here.
END_COPYS

// w50-a: orphan-instantiation anchor for the std::list<int> COMDATs the map
// attributes to this unit; nothing here calls them, and /OPT:REF dropped the
// caller.  Reproducing that needs an unreferenced EXTERNAL function, which is
// why the template instantiation below exists at all.  Removing it takes the
// four rows to 0%.  w50-b: measured.
template class std::list<int>;
"""


def make_tree(tmp_path, src=None, pilot=None):
    root = tmp_path / "t"
    f = root / REL
    f.parent.mkdir(parents=True, exist_ok=True)
    f.write_text(src if src is not None else source())
    (root / "objdiff.json").write_text(json.dumps({"units": [
        {"name": "default/system/x/Tu", "base_path": "build/373307D9/src/system/x/Tu.obj",
         "metadata": {"source_path": REL}}]}))
    rep = root / "build" / "373307D9" / "report.json"
    rep.parent.mkdir(parents=True, exist_ok=True)
    rep.write_text(json.dumps({"units": [{"name": "default/system/x/Tu", "functions": [
        {"name": m, "metadata": {"demangled_name": d}} for m, d in FUNCS]}]}))
    if pilot is not None:
        p = root / SIDE
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(pilot)
    return root


def run(root, *args):
    p = subprocess.run([sys.executable, str(TOOL), "--repo", str(root), "--date", "2026-11-13",
                        *args], capture_output=True, text=True, timeout=60, cwd=root)
    return p.returncode, p.stdout + p.stderr


def migrated(tmp_path, **kw):
    root = make_tree(tmp_path, **kw)
    rc, out = run(root, REL)
    assert rc == 0, out
    return root, (root / REL).read_text(), (root / SIDE).read_text()


def fences(side_text, tmp_path):
    p = tmp_path / "parse.md"
    p.write_text(side_text)
    return parse_sidecar(str(p))


def test_documentation_and_short_notes_survive(tmp_path):
    _, src, _ = migrated(tmp_path)
    assert DOC in src                       # untagged prose, even with "inert"
    assert SHORT.replace("//", "    //") in src  # 2-line lane note: below --min-lines
    assert "// w1-a: this line is inside a block comment" in src
    assert "#define LONG_MACRO(x) \\\n    do" in src


def test_nothing_is_lost_and_nothing_else_changes(tmp_path):
    root, src, side = migrated(tmp_path)
    secs = fences(side, tmp_path)
    moved = [t for blocks in secs.values() for _, _, t in blocks]
    for wall in (WALL_ABOVE, WALL_INSIDE):
        assert wall in moved, f"not verbatim in sidecar:\n{wall}"
        assert wall not in src
    old = source().split("\n")
    new = src.split("\n")
    strip = [l for l in new if not (mn.MARKER_RE.match(l) or mn.STUB_CONT_RE.match(l))]
    moved_lines = sum(t.count("\n") + 1 for t in moved)
    assert len(old) - moved_lines == len(strip)
    # every non-stub line of the new source is an original line, in order
    it = iter(old)
    assert all(any(l == o for o in it) for l in strip)


def test_attribution_inside_above_overload_macro_file_level(tmp_path):
    _, _, side = migrated(tmp_path)
    secs = fences(side, tmp_path)
    assert WALL_ABOVE in [t for _, _, t in secs["?Bar@Foo@@QAAXXZ"]]
    assert WALL_INSIDE in [t for _, _, t in secs["?Bar@Foo@@QAAXXZ"]]
    assert any("w30-a" in t for _, _, t in secs["?Baz@Foo@@QAAXH@Z"])   # int, not (int,int)
    assert "?Baz@Foo@@QAAXHH@Z" not in secs
    copy = "?Copy@Foo@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z"
    assert any("w40-a" in t for _, _, t in secs[copy])
    assert "macro-generated (BEGIN_COPYS)" in side
    assert any("w50-a" in t for _, _, t in secs["(file-level)"])
    assert "File-level (was lines" in side
    assert "Inside the body (was lines" in side and "Above the definition (was lines" in side


def test_rerun_is_a_no_op(tmp_path):
    root, src, side = migrated(tmp_path)
    rc, out = run(root, REL)
    assert rc == 0, out
    assert (root / REL).read_text() == src
    assert (root / SIDE).read_text() == side
    rc, out = run(root, "--check", REL)
    assert rc == 0, out


def test_stub_is_never_moved_again_even_below_min_lines(tmp_path):
    """The stub marker, not the 5-line threshold, is what protects a stub: a
    re-run at --min-lines 2 moves the 2-line note but leaves every stub."""
    root, src, _ = migrated(tmp_path)
    n_stubs = sum(1 for l in src.split("\n") if mn.MARKER_RE.match(l))
    rc, out = run(root, "--min-lines", "2", REL)
    assert rc == 0, out
    src2 = (root / REL).read_text()
    assert sum(1 for l in src2.split("\n") if mn.MARKER_RE.match(l)) == n_stubs + 1
    secs = fences((root / SIDE).read_text(), tmp_path)
    assert not any("Lane history (" in t for v in secs.values() for _, _, t in v)


def test_check_refuses_a_wall_and_names_it(tmp_path):
    root = make_tree(tmp_path)
    rc, out = run(root, "--check", REL)
    assert rc == 1, out
    assert f"{REL}:" in out and "lane-note" in out
    # a NEW wall added after migration is caught again
    root, src, _ = migrated(tmp_path / "b")
    (root / REL).write_text(src.replace("void Foo::Baz(int a, int b) {}",
                                        WALL_ABOVE + "\nvoid Foo::Baz(int a, int b) {}"))
    rc, out = run(root, "--check", REL)
    assert rc == 1, out


def test_stub_is_short_verbatim_and_keeps_verdicts(tmp_path):
    _, src, _ = migrated(tmp_path)
    lines = src.split("\n")
    stubs = [i for i, l in enumerate(lines) if mn.MARKER_RE.match(l)]
    assert len(stubs) == 5
    plain_walls = mn._plain(WALL_ABOVE)
    for i in stubs:
        body = [lines[i]]
        k = i + 1
        while k < len(lines) and mn.STUB_CONT_RE.match(lines[k]):
            body.append(lines[k])
            k += 1
        assert len(body) <= 4, body
        assert SIDE in body[0]
    above = src[src.index("Lane history (9 lines"):src.index("void Foo::Bar")]
    assert "NOT A BUG (w-this-adjust)" in above            # verdict kept with its lane
    assert "Hypothesis" not in above and "?" not in above  # speculation never quoted
    quoted = " ".join(l.split("// > ", 1)[1] for l in above.split("\n") if "// > " in l
                      and "Verdicts" not in l)
    # quoted text (minus the trailing lane attribution) occurs verbatim in the wall
    core = quoted.rsplit(" (w", 1)[0]
    assert core and core in plain_walls, quoted
    # every quote in the file is a WHOLE sentence of its own block, never a prefix
    sents = {s for w in (WALL_ABOVE, WALL_INSIDE) for _, s in mn.sentences(mn._plain(w, True))}
    i = 0
    while i < len(lines):
        if mn.STUB_CONT_RE.match(lines[i]) and "Verdicts" not in lines[i]:
            j = i
            while j < len(lines) and mn.STUB_CONT_RE.match(lines[j]) and "Verdicts" not in lines[j]:
                j += 1
            q = " ".join(l.split("// > ", 1)[1] for l in lines[i:j])
            assert q in sents or q.rsplit(" (w", 1)[0] in sents, f"not a whole sentence: {q}"
            i = j
        i += 1
    inside = src[src.index("    x++;") - 600:src.index("    x++;")]
    assert "NOT a behaviour bug (w27-n)" in inside, inside   # negation travels with it


def test_find_refutations_answers_as_before(tmp_path):
    root = make_tree(tmp_path)

    def find():
        p = subprocess.run([sys.executable, str(FIND), "?Bar@Foo@@QAAXXZ", "--file",
                            str(root / REL), "--max-chars", "0"],
                           capture_output=True, text=True, timeout=60)
        return p.returncode, p.stdout
    rc0, out0 = find()
    assert rc0 == 0 and WALL_ABOVE in out0 and WALL_INSIDE in out0
    rc, out = run(root, REL)
    assert rc == 0, out
    rc1, out1 = find()
    assert rc1 == 0 and WALL_ABOVE in out1 and WALL_INSIDE in out1, out1


def test_existing_sidecar_is_kept_byte_for_byte_and_pilot_symbols_untouched(tmp_path):
    pilot = "# Lane notes: `src/system/x/Tu.cpp`\n\nhand-curated intro\n\n## `?Other@@YAXXZ`\n\n" \
            "```text\n// w1-x: old\n```\n"
    root, _, side = migrated(tmp_path, pilot=pilot)
    assert side.startswith(pilot)
    # a pilot symbol's block stays in the source and is reported, not moved
    root2 = make_tree(tmp_path / "p")
    rc, out = run(root2, "--exclude-symbol", "?Bar@Foo@@QAAXXZ", REL)
    assert rc == 1 and "REFUSED" in out
    assert WALL_ABOVE in (root2 / REL).read_text()


def test_directive_block_is_refused_not_moved(tmp_path):
    src = source().replace(WALL_ABOVE, WALL_ABOVE + "\n// clang-format off")
    root = make_tree(tmp_path, src=src)
    rc, out = run(root, REL)
    assert rc == 1 and "REFUSED" in out and "directive" in out
    assert WALL_ABOVE + "\n// clang-format off" in (root / REL).read_text()


def test_dry_run_writes_nothing(tmp_path):
    root = make_tree(tmp_path)
    rc, out = run(root, "--dry-run", REL)
    assert rc == 0 and "would move 5 block(s)" in out, out
    assert (root / REL).read_text() == source()
    assert not (root / SIDE).exists()


def test_leading_doc_paragraph_stays(tmp_path):
    wall = ("// Copies the six key vectors; the owner is set first so the copies\n"
            "// resolve against it.\n"
            "//\n" + WALL_ABOVE)
    root = make_tree(tmp_path, src=source().replace(WALL_ABOVE, wall))
    rc, out = run(root, REL)
    assert rc == 0, out
    src = (root / REL).read_text()
    assert "// Copies the six key vectors; the owner is set first so the copies\n" in src
    assert WALL_ABOVE not in src
    side = (root / SIDE).read_text()
    rc, out = run(root, REL)  # doc paragraph + stub is one comment run: still a no-op
    assert rc == 0, out
    assert (root / REL).read_text() == src and (root / SIDE).read_text() == side
