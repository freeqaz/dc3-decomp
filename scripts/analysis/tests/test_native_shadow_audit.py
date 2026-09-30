"""Two-sided controls for scripts/analysis/native_shadow_audit.py.

  * POSITIVE on the real tree: the one HX_NATIVE shadow we KNOW was a real
    native-only bug -- ObjPtrVec<T1,T2>::erase in src/system/obj/ObjPtr_p.h,
    whose native body ignored mEraseMode (fix-ptrvec-erase, a71828235) -- must
    be reported as a function shadow split across two guards.  If this test
    passes on a tree where that fix has landed, it still passes: the fix edits
    the native BODY, not the guard structure the tool reports.
  * MOVES BY ONE: a synthetic guard added to a scratch copy moves the universe,
    the examined count and exactly one shape bucket by exactly one.
  * TRIPWIRE: a parser that loses a directive must leave the books unbalanced
    (exit 4), never print a smaller clean census.
"""
from __future__ import annotations

import os
import shutil

import pytest

from scripts.analysis import native_shadow_audit as nsa
from scripts.analysis.coverage import CoverageReport, EXIT_OK, EXIT_UNACCOUNTED

REPO = nsa.REPO
OBJPTR = os.path.join("src", "system", "obj", "ObjPtr_p.h")


def _scan(root):
    cov = CoverageReport("t", allow_truncation=False)
    regions, splits = nsa.run(root, False, cov)
    return cov, regions, splits


def _shapes(regions):
    out = {"REPLACES": 0, "ADDS": 0, "REMOVES": 0}
    for r in regions:
        out[r["shape"]] += 1
    return out


def test_real_tree_reports_objptrvec_erase_as_split_shadow():
    cov, regions, splits = _scan(REPO)
    assert cov.is_clean(), cov.render()
    hits = [s for s in splits if s["function"] == "ObjPtrVec::erase"
            and s["file"] == OBJPTR.replace(os.sep, "/")]
    assert len(hits) == 1, splits
    s = hits[0]
    assert s["kind"] == "split-pair"
    by_line = {(r["file"], r["line"]): r for r in regions}
    ppc = by_line[(s["file"], s["ppc_region"])]
    nat = by_line[(s["file"], s["native_region"])]
    # The raw per-region shapes are (c) and (b) -- which is exactly why a
    # per-region count alone would hide it; the pairing is what finds it.
    assert ppc["shape"] == "REMOVES" and ppc["directive"].startswith("#ifndef")
    assert nat["shape"] == "ADDS" and nat["directive"].startswith("#ifdef")
    assert ppc["bucket_d"] and nat["bucket_d"]
    assert "ObjPtrVec::erase" in ppc["ppc_defs"]
    assert "ObjPtrVec::erase" in nat["native_defs"]


SYNTH = """
#ifdef HX_NATIVE
    int nativeOnlySynthetic = 1;
#else
    int decompOnlySynthetic = 2;
#endif
"""


def _scratch_with(tmp_path, inject: bool):
    root = tmp_path / ("inj" if inject else "base")
    dst = root / OBJPTR
    dst.parent.mkdir(parents=True)
    shutil.copy(os.path.join(REPO, OBJPTR), dst)
    if inject:
        text = dst.read_text()
        # inside ObjPtrList::pop_back's body, which is UNGUARDED (compiled in
        # both builds).  Anchoring inside a native-only block would be wrong:
        # the injected #else would be dead there and the region an ADD.
        anchor = "void ObjPtrList<T1, T2>::pop_back() {\n"
        assert anchor in text
        text = text.replace(anchor, anchor + SYNTH, 1)
        dst.write_text(text)
    return str(root)


def test_synthetic_guard_moves_counts_by_exactly_one(tmp_path):
    cb, rb, _ = _scan(_scratch_with(tmp_path, False))
    ci, ri, _ = _scan(_scratch_with(tmp_path, True))
    db, di = cb.as_dict(), ci.as_dict()
    assert db["universe"] > 0 and cb.is_clean() and ci.is_clean()
    assert di["universe"] == db["universe"] + 1
    assert di["examined"] == db["examined"] + 1
    assert len(ri) == len(rb) + 1
    sb, si = _shapes(rb), _shapes(ri)
    assert si["REPLACES"] == sb["REPLACES"] + 1
    assert si["ADDS"] == sb["ADDS"] and si["REMOVES"] == sb["REMOVES"]
    new = [r for r in ri if r["scope"] == "in-function"
           and r["enclosing"] == "ObjPtrList::pop_back" and r["shape"] == "REPLACES"]
    assert len(new) == 1


def test_parser_that_loses_a_directive_is_unaccounted(tmp_path, monkeypatch):
    root = _scratch_with(tmp_path, False)
    # Sabotage: the parser no longer recognises #ifndef.  The independent
    # universe pass still counts those lines, so the books must not balance.
    monkeypatch.setattr(nsa, "DIRECTIVE_RE",
                        __import__("re").compile(r"^\s*#\s*(ifdef|if|elif|else|endif)\b(.*)$"))
    cov, _, _ = _scan(root)
    code = nsa.verdict(cov)
    # #ifndef lines now parse as nothing -> their #endif underflows -> the file
    # is dropped as unbalanced (a PARSER drop), or they go unattributed.  Either
    # way the one outcome NOT allowed is a clean exit.
    assert code == EXIT_UNACCOUNTED, cov.render()


def test_block_commented_directive_is_dropped_not_classified(tmp_path):
    root = tmp_path / "c"
    f = root / "src" / "x.cpp"
    f.parent.mkdir(parents=True)
    f.write_text("/*\n#ifdef HX_NATIVE\nint a;\n#endif\n*/\nvoid g() {\n"
                 "#ifndef HX_NATIVE\n    h();\n#endif\n}\n")
    cov, regions, _ = _scan(str(root))
    d = cov.as_dict()
    assert d["universe"] == 2
    assert d["dropped"].get("inside-block-comment") == 1
    assert d["examined"] == 1 and cov.unaccounted == 0
    assert [(r["shape"], r["enclosing"]) for r in regions] == [("REMOVES", "g")]
    assert nsa.verdict(cov) == EXIT_OK


def test_nested_guard_resolves_against_its_context(tmp_path):
    # An #ifndef nested inside an #ifdef HX_NATIVE block is dead in BOTH
    # builds; an #ifdef/#else nested there is effectively a native ADD.
    root = tmp_path / "n"
    f = root / "src" / "n.cpp"
    f.parent.mkdir(parents=True)
    f.write_text("void g() {\n#ifdef HX_NATIVE\n    a();\n#ifndef HX_NATIVE\n    b();\n"
                 "#endif\n#ifdef HX_NATIVE\n    c();\n#else\n    d();\n#endif\n#endif\n}\n")
    cov, regions, _ = _scan(str(root))
    d = cov.as_dict()
    assert d["universe"] == 3 and cov.unaccounted == 0
    assert d["dropped"] == {"dead-nested-under-opposite-guard": 1}
    assert sorted(r["shape"] for r in regions) == ["ADDS", "ADDS"]
