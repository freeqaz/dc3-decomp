"""Tests for scripts/notes/verify_comment_move.py (the comment-move byte gate).

The gate is only worth something if it (a) FAILS on a real binary change,
including one that moves no score, (b) PASSES on a benign section-order
shuffle (which the 2026-11-13 pilot saw from a comment move alone), and (c)
refuses to answer when the edited source was never recompiled.  Each test
builds a throwaway tree with synthetic PPC COFF objects; nothing reads the
real build.

The real-toolchain sabotages (one-character code change in MatAnim.cpp; one
comment line above SynthSample.cpp's `__LINE__` use) were run in the
w-notes-move worktree and are recorded in the commit message -- they need a
full MSVC build, which a unit test cannot assume.
"""

import json
import os
import struct
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
TOOL = REPO / "scripts" / "notes" / "verify_comment_move.py"
sys.path.insert(0, str(REPO / "scripts" / "notes"))
import verify_comment_move as gate  # noqa: E402

COMDAT = 0x00001000
CODE = 0x60000020


def coff(sections, symbols):
    """sections: [(name, data, [(off, symidx, type)], characteristics)]
    symbols:  [(name, value, secnum, cls, is_section)]  (section symbols get
    one aux record; indices passed to relocations are RAW table indices)."""
    nsec = len(sections)
    hdr_len = 20 + 40 * nsec
    body, sec_hdrs, off = b"", [], hdr_len
    for name, data, rel, ch in sections:
        dptr = off
        body += data
        off += len(data)
        rptr = off
        for va, si, ty in rel:
            body += struct.pack("<IIH", va, si, ty)
            off += 10
        sec_hdrs.append(struct.pack("<8sIIIIIIHHI", name.encode(), 0, 0, len(data), dptr,
                                    rptr if rel else 0, 0, len(rel), 0, ch))
    symtab, n = b"", 0
    for name, value, secnum, cls, is_sec in symbols:
        naux = 1 if is_sec else 0
        symtab += struct.pack("<8sIhHBB", name.encode()[:8], value, secnum, 0x20, cls, naux)
        n += 1 + naux
        if is_sec:
            symtab += struct.pack("<IHHIHB3s", len(sections[secnum - 1][1]), 0, 0, 0, 0, 2, b"")
    hdr = struct.pack("<HHIIIHH", 0x01F2, nsec, 0, off, n, 0, 0)
    return hdr + b"".join(sec_hdrs) + body + symtab + struct.pack("<I", 4)


def obj(fn_bytes=b"\x38\x60\x00\x01\x4e\x80\x00\x20", callee="bar", swap=False):
    """Two COMDAT functions; foo calls `callee`.  `swap` reverses section order.

    Raw symbol table: 0 .text(foo's) +1 aux, 2 foo, 3 .text(baz's) +4 aux,
    5 baz, 6 callee (undefined).
    """
    foo_sec = (".text", fn_bytes, [(0, 6, 6)], CODE | COMDAT)
    baz_sec = (".text", b"\x4e\x80\x00\x20", [], CODE | COMDAT)
    fs, bs = (2, 1) if swap else (1, 2)
    secs = [baz_sec, foo_sec] if swap else [foo_sec, baz_sec]
    syms = [(".text", 0, fs, 3, True), ("foo", 0, fs, 2, False),
            (".text", 0, bs, 3, True), ("baz", 0, bs, 2, False), (callee, 0, 0, 2, False)]
    return coff(secs, syms)


def tree(tmp_path, data, report_fuzzy=100.0):
    root = tmp_path / "t"
    (root / "src").mkdir(parents=True, exist_ok=True)
    (root / "scripts" / "notes").mkdir(parents=True, exist_ok=True)
    o = root / "build" / gate.VERSION / "src" / "a.obj"
    o.parent.mkdir(parents=True, exist_ok=True)
    o.write_bytes(data)
    rep = {"measures": {}, "units": [{"name": "default/a", "measures": {},
           "functions": [{"name": "foo", "size": "8", "fuzzy_match_percent": report_fuzzy}]}]}
    (root / "build" / gate.VERSION / "report.json").write_text(json.dumps(rep))
    (root / "objdiff.json").write_text(json.dumps({"units": [
        {"name": "default/a", "base_path": f"build/{gate.VERSION}/src/a.obj",
         "metadata": {"source_path": "src/a.cpp"}}]}))
    src = root / "src" / "a.cpp"
    if not src.exists():
        src.write_text("int foo() { return 1; }\n")
        subprocess.run(["git", "init", "-q", str(root)], check=True)
        subprocess.run(["git", "-C", str(root), "add", "src/a.cpp"], check=True)
        subprocess.run(["git", "-C", str(root), "-c", "user.email=t@t", "-c", "user.name=t",
                        "commit", "-qm", "base"], check=True)
    return root


def run(root, *args):
    p = subprocess.run([sys.executable, str(TOOL), "--repo", str(root), "-j", "2", *args],
                       capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout + p.stderr


def snap_then(tmp_path, after_bytes, edit_source=True, report_fuzzy=100.0, rewrite=True):
    root = tree(tmp_path, obj())
    rc, out = run(root, "snapshot", "--no-build", "--out", str(tmp_path / "snap"))
    assert rc == 0, out
    time.sleep(0.05)
    if edit_source:
        (root / "src" / "a.cpp").write_text("// moved a comment\nint foo() { return 1; }\n")
    if rewrite:
        tree(tmp_path, after_bytes, report_fuzzy=report_fuzzy)
        now = time.time() + 1
        os.utime(root / "build" / gate.VERSION / "src" / "a.obj", (now, now))
        os.utime(root / "build" / gate.VERSION / "report.json", (now, now))
    return run(root, "compare", "--no-build", "--before", str(tmp_path / "snap"))


def test_identical_rebuild_passes(tmp_path):
    rc, out = snap_then(tmp_path, obj())
    assert rc == 0, out
    assert "byte-identical" in out


def test_one_byte_code_change_is_refused(tmp_path):
    rc, out = snap_then(tmp_path, obj(fn_bytes=b"\x38\x60\x00\x02\x4e\x80\x00\x20"))
    assert rc == 1, out
    assert "symbol foo" in out


def test_wrong_callee_same_bytes_is_refused(tmp_path):
    # identical instruction bytes, relocation retargeted: invisible to a byte hash
    rc, out = snap_then(tmp_path, obj(callee="qux"))
    assert rc == 1, out
    assert "symbol foo" in out


def test_section_order_shuffle_is_not_a_difference(tmp_path):
    # the pilot's MatAnim.obj: COMDAT order moved with comments alone
    assert gate.fingerprint_object_bytes(obj(swap=True)) == gate.fingerprint_object_bytes(obj())
    rc, out = snap_then(tmp_path, obj(swap=True))
    assert rc == 0, out


def test_report_measure_change_is_refused(tmp_path):
    rc, out = snap_then(tmp_path, obj(), report_fuzzy=99.9)
    assert rc == 1, out
    assert "report default/a" in out


def test_unrebuilt_object_is_vacuous_not_a_pass(tmp_path):
    rc, out = snap_then(tmp_path, obj(), rewrite=False)
    assert rc == 2, out
    assert "vacuous" in out


def test_unreadable_snapshot_is_2(tmp_path):
    root = tree(tmp_path, obj())
    rc, out = run(root, "compare", "--no-build", "--before", str(tmp_path / "nope"))
    assert rc == 2, out


def labelled(num, at=4, fn_bytes=b"\x38\x60\x00\x01\x4e\x80\x00\x20"):
    """foo with a compiler-numbered label `$M<num>` at offset `at`, and an
    .rdata table whose relocation points at that label (an EH table)."""
    secs = [(".text", fn_bytes, [], CODE | COMDAT),
            (".rdata", b"\x00\x00\x00\x00", [(0, 3, 2)], 0x40000040)]
    syms = [(".text", 0, 1, 3, True), ("foo", 0, 1, 2, False), (f"$M{num}", at, 1, 6, False),
            (".rdata", 0, 2, 3, True), ("tbl", 0, 2, 3, False)]
    # raw: 0 .text +1 aux, 2 foo, 3 $M, 4 .rdata +5 aux, 6 tbl -> reloc targets raw idx 3
    return coff(secs, syms)


def test_renumbered_compiler_labels_are_not_a_difference(tmp_path):
    # measured on Font/Mesh/MatAnim.obj: a comment move renumbers $M/$T/__unwind$
    assert gate.fingerprint_object_bytes(labelled(60594)) == \
        gate.fingerprint_object_bytes(labelled(62026))


def test_a_label_that_moves_is_a_difference(tmp_path):
    assert gate.fingerprint_object_bytes(labelled(1, at=4)) != \
        gate.fingerprint_object_bytes(labelled(1, at=0))


def test_a_label_does_not_hide_the_function_bytes_after_it(tmp_path):
    a = gate.fingerprint_object_bytes(labelled(1))
    b = gate.fingerprint_object_bytes(labelled(1, fn_bytes=b"\x38\x60\x00\x01\x4e\x80\x00\x21"))
    assert a["symbols"]["foo"] != b["symbols"]["foo"]


def header_case(tmp_path, deps_target, rebuild, pch_edge=False):
    """A changed HEADER (no unit of its own).  A fake `ninja` on PATH answers
    `ninja -t deps` with `deps_target` reading src/a.h."""
    root = tree(tmp_path, obj())
    (root / "src" / "a.h").write_text("int foo();\n")
    subprocess.run(["git", "-C", str(root), "add", "src/a.h"], check=True)
    subprocess.run(["git", "-C", str(root), "-c", "user.email=t@t", "-c", "user.name=t",
                    "commit", "-qm", "hdr"], check=True)
    bindir = tmp_path / "bin"
    bindir.mkdir()
    fake = bindir / "ninja"
    fake.write_text("#!/bin/sh\ncat <<'X'\n"
                    f"{deps_target}: #deps 2, deps mtime 1 (VALID)\n    src/a.h\n    src/b.h\n\n"
                    "X\n")
    fake.chmod(0o755)
    o = f"build/{gate.VERSION}/src/a.obj"
    (root / "build.ninja").write_text(
        f"build {o}: {'msvc_pch' if pch_edge else 'msvc'} src/a.cpp"
        + (f" | build/{gate.VERSION}/pch/system.pch\n" if pch_edge else "\n"))
    env = dict(os.environ, PATH=f"{bindir}:{os.environ['PATH']}")
    rc, out = run(root, "snapshot", "--no-build", "--out", str(tmp_path / "snap"))
    assert rc == 0, out
    time.sleep(0.05)
    (root / "src" / "a.h").write_text("// moved a comment\nint foo();\n")
    now = time.time() + 1
    if rebuild:
        os.utime(root / o, (now, now))
    os.utime(root / "build" / gate.VERSION / "report.json", (now, now))
    p = subprocess.run([sys.executable, str(TOOL), "--repo", str(root), "-j", "2", "compare",
                        "--no-build", "--before", str(tmp_path / "snap")],
                       capture_output=True, text=True, timeout=120, env=env)
    return p.returncode, p.stdout + p.stderr


def test_changed_header_whose_reader_was_not_rebuilt_is_vacuous(tmp_path):
    # measured 2026-11-13: the header batch printed "0 recompiled unit(s)" and
    # PASSED -- headers had no unit, so nothing asserted any reader was rebuilt
    rc, out = header_case(tmp_path, f"build/{gate.VERSION}/src/a.obj", rebuild=False)
    assert rc == 2, out
    assert "reads a changed header" in out


def test_changed_header_whose_reader_was_rebuilt_passes(tmp_path):
    rc, out = header_case(tmp_path, f"build/{gate.VERSION}/src/a.obj", rebuild=True)
    assert rc == 0, out
    assert "1 object(s) recompiled for 1 changed header(s)" in out


def test_pch_header_is_charged_to_every_pch_user(tmp_path):
    # MSVC omits PCH headers from a TU's /showIncludes: only the PCH lists it
    rc, out = header_case(tmp_path, f"build/{gate.VERSION}/pch/decomp_pch.obj",
                          rebuild=False, pch_edge=True)
    assert rc == 2, out
    assert "reads a changed header" in out
