"""Negative controls for the `is_stub` re-derivation and the writer guards.

Same discipline as ``test_honesty_db_writers.py``: every test reconstructs the
false negative the fix is supposed to catch, and where the fix changes a
BRANCH the OLD logic is implemented inline and asserted to DISAGREE on a
concrete input.  Asserting only that the new code returns X would be a
tautology -- this project has been burned by exactly that.

The fixture is a whole miniature tree, because the classifier's whole claim is
that it reads four independent substrates and not the database's own opinion:

    build/373307D9/report.json     is the symbol measured at all
    build/373307D9/src/u.obj       a REAL Xbox-360 COFF, written byte by byte
    orig/373307D9/ham_xbox_r.map   does the address hold an __unwind$ funclet
    config/373307D9/symbols.txt    dtk's own name for that address

Four rows, one per class:

    fn_825CE39C            funclet  -- map says __unwind$110262
    ?Gone@Old@@QAAXXZ      retired  -- in no report.json, in no object
    ?NoBody@Thing@@QAAXXZ  stub     -- in report.json at 0%, we define nothing
    ?HasBody@Thing@@QAAXXZ not stub -- in report.json, our COFF defines it

NO TEST HERE TOUCHES THE REAL decomp.db.
"""
from __future__ import annotations

import json
import sqlite3
import struct

import pytest

import scripts.sync_objdiff as so
from scripts.analysis import stub_flag_audit as sfa

TITLE = "373307D9"

FUNCLET = "fn_825CE39C"
FUNCLET_VA = 0x825CE39C
RETIRED = "?Gone@Old@@QAAXXZ"
STUB = "?NoBody@Thing@@QAAXXZ"
BODIED = "?HasBody@Thing@@QAAXXZ"


# =========================================================================== #
# Fixture: a real COFF object, not a mock.
# =========================================================================== #

def _coff(defined: list[str], section_size: int = 0x40) -> bytes:
    """A minimal but genuine Xbox-360 COFF (machine 0x1f2) defining `defined`.

    Hand-assembled rather than mocked because the reader under test parses raw
    bytes: a mock of ``coff_defined_symbols`` would verify nothing about the
    18-byte symbol record, the ``SectionNumber > 0`` test or the string table,
    which is where a reader like this actually goes wrong.
    """
    nsym = len(defined)
    nsec = 1
    sec_off = 20
    symptr = sec_off + nsec * 40

    hdr = struct.pack("<HHIIIHH", 0x01F2, nsec, 0, symptr, nsym, 0, 0)
    sec = (b".text\0\0\0"
           + struct.pack("<IIIIIIHHI", 0, 0, section_size, symptr + nsym * 18,
                         0, 0, 0, 0, 0x60500020))

    # Every name goes through the string table: these are mangled names, and
    # the 8-byte inline form would silently truncate them.
    strtab = bytearray(b"\0\0\0\0")
    syms = bytearray()
    for name in defined:
        off = len(strtab)
        strtab += name.encode("latin1") + b"\0"
        syms += struct.pack("<IIIhHBB", 0, off, 0, 1, 0x20, 2, 0)
    struct.pack_into("<I", strtab, 0, len(strtab))
    return hdr + sec + bytes(syms) + bytes(strtab)


@pytest.fixture
def tree(tmp_path):
    """A project tree + a decomp.db carrying one row of each class."""
    build = tmp_path / "build" / TITLE
    (build / "src").mkdir(parents=True)
    (tmp_path / "orig" / TITLE).mkdir(parents=True)
    (tmp_path / "config" / TITLE).mkdir(parents=True)

    report = {"units": [{"name": "default/system/u", "functions": [
        {"name": FUNCLET, "size": 40, "match_percent_normalized": 99.9},
        {"name": STUB, "size": 200, "match_percent_normalized": 0.0},
        {"name": BODIED, "size": 64, "match_percent_normalized": 91.5},
    ]}]}
    (build / "report.json").write_text(json.dumps(report))

    # Our object defines the bodied function and nothing else.
    (build / "src" / "u.obj").write_bytes(_coff([BODIED]))

    (tmp_path / "orig" / TITLE / "ham_xbox_r.map").write_text(
        " 0005:00001200       __unwind$110262            "
        f"{FUNCLET_VA:08x} f i u.obj\n")
    (tmp_path / "config" / TITLE / "symbols.txt").write_text(
        f"__unwind$110262 = .text:0x{FUNCLET_VA:08X}; // type:function\n")

    db = tmp_path / "fixture.db"
    conn = sqlite3.connect(db)
    conn.execute(
        "CREATE TABLE functions (id INTEGER PRIMARY KEY, symbol TEXT UNIQUE, "
        "demangled TEXT, unit TEXT, size INT, excluded INT DEFAULT 0, "
        "verdict TEXT, verdict_reason TEXT, is_stub INT DEFAULT 0, "
        "updated_at TIMESTAMP)")
    for i, sym in enumerate((FUNCLET, RETIRED, STUB, BODIED), start=1):
        conn.execute(
            "INSERT INTO functions (id, symbol, demangled, unit, size, is_stub,"
            " verdict_reason) VALUES (?,?,?,?,?,1,?)",
            (i, sym, sym, "default/system/u", 200,
             "STUB: no body emitted; % is stale"))
    conn.commit()
    conn.close()
    return tmp_path, db


def _classes(tmp_path, db, **kw):
    out = sfa.run(tmp_path, db, apply=False, json_out=None, quiet=True, **kw)
    return {r["symbol"]: r["klass"] for r in out["rows"]}


# =========================================================================== #
# 1. The classifier separates the four populations.
# =========================================================================== #

def test_four_populations_separate(tree):
    tmp_path, db = tree
    got = _classes(tmp_path, db)
    assert got == {
        FUNCLET: "FUNCLET_MISLABELLED",
        RETIRED: "RETIRED_SPELLING",
        STUB: "REAL_STUB",
        BODIED: "NOT_A_STUB",
    }


def test_only_the_real_stub_survives(tree):
    tmp_path, db = tree
    out = sfa.run(tmp_path, db, apply=False, json_out=None, quiet=True)
    assert out["cleared"] == 3, "3 of the 4 rows are not stubs at all"
    assert out["counts"]["REAL_STUB"] == 1


def test_apply_writes_a_reason_and_keeps_the_real_stub(tree):
    tmp_path, db = tree
    sfa.run(tmp_path, db, apply=True, json_out=None, quiet=True)
    conn = sqlite3.connect(db)
    rows = dict(conn.execute(
        "SELECT symbol, is_stub FROM functions").fetchall())
    reasons = dict(conn.execute(
        "SELECT symbol, verdict_reason FROM functions").fetchall())
    conn.close()
    assert rows == {FUNCLET: 0, RETIRED: 0, STUB: 1, BODIED: 0}
    # Provenance: a bare 0 cannot say WHY, and a later reader would re-guess.
    assert "FUNCLET_MISLABELLED" in reasons[FUNCLET]
    assert "RETIRED_SPELLING" in reasons[RETIRED]
    assert reasons[STUB] == "STUB: no body emitted; % is stale", \
        "the surviving stub's row must be left exactly as it was"


# =========================================================================== #
# 2. SABOTAGE -- remove the funclet guard, the funclet row must change class.
# =========================================================================== #

def test_removing_the_funclet_guard_breaks_the_classification(tree, monkeypatch):
    """The guard is load-bearing, not decorative.

    Without it, `fn_825CE39C` is judged by the same rule as any other symbol:
    it IS in report.json and our objects do NOT define it, so it lands in
    REAL_STUB -- precisely the 26-row mislabelling this whole change exists to
    remove.  If this test ever passes with the guard deleted, the guard is not
    what produces the verdict and the other tests are tautologies.
    """
    tmp_path, db = tree
    baseline = _classes(tmp_path, db)
    assert baseline[FUNCLET] == "FUNCLET_MISLABELLED"

    monkeypatch.setattr(sfa, "FN_ADDR_RE",
                        sfa.re.compile(r"^\0never\0$"))
    monkeypatch.setattr(sfa, "is_funclet_name", lambda _name: False)
    sabotaged = _classes(tmp_path, db)

    assert sabotaged[FUNCLET] != baseline[FUNCLET], \
        "funclet guard removed and nothing changed -- the guard does nothing"
    assert sabotaged[FUNCLET] == "REAL_STUB"
    # And only the funclet row moves: the sabotage must be targeted, or the
    # disagreement above would not be evidence about THIS guard.
    assert {k: v for k, v in sabotaged.items() if k != FUNCLET} == \
           {k: v for k, v in baseline.items() if k != FUNCLET}


def test_removing_the_report_presence_check_hides_retired_spellings(tree,
                                                                    monkeypatch):
    """The other half: without the report.json lookup a retired spelling is
    indistinguishable from a stub, which is how 123 of them accumulated."""
    tmp_path, db = tree
    baseline = _classes(tmp_path, db)

    # Pretend every symbol is present in the report (the pre-fix world, where
    # nothing ever asked).
    real = sfa.report_index
    monkeypatch.setattr(sfa, "report_index", lambda pd: {
        **real(pd), RETIRED: {"unit": "u", "size": 200,
                              "normalized": 0.0, "fuzzy": None}})
    sabotaged = _classes(tmp_path, db)
    assert baseline[RETIRED] == "RETIRED_SPELLING"
    assert sabotaged[RETIRED] == "REAL_STUB"


# =========================================================================== #
# 3. The WRITER guards, against the pre-fix logic implemented inline.
# =========================================================================== #

def _old_stub_action(error, symbol, was_stub):
    """sync_objdiff's rule BEFORE this change, transcribed from the branches
    it replaced: `not_found` fell through untouched, and `skipped` /
    `unimplemented` set the flag with no regard for what the symbol was."""
    if error is None:
        return so.STUB_CLEAR if was_stub else so.STUB_NONE
    if error == "not_found":
        return so.STUB_NONE          # <- the leak: a stale 1 lived forever
    if error in ("skipped", "unimplemented"):
        return so.STUB_SET           # <- funclets included
    return so.STUB_NONE


@pytest.mark.parametrize("error,symbol", [
    ("not_found", RETIRED),
    ("unimplemented", FUNCLET),
    ("skipped", "__unwind$110262"),
])
def test_new_writer_disagrees_with_the_old_one(error, symbol):
    """Each guard must change the answer on a real input, not merely exist."""
    old = _old_stub_action(error, symbol, was_stub=True)
    new = so.stub_action(error, symbol, was_stub=True)
    assert old != new, f"{symbol!r}/{error}: guard added but answer unchanged"
    assert new == so.STUB_VOID


@pytest.mark.parametrize("error,symbol,expected", [
    # A genuine stub is still a stub -- the guards must not fail open.
    ("unimplemented", STUB, so.STUB_SET),
    ("skipped", "??__Fmsg@?4??Foo@@QAAXXZ@YAXXZ", so.STUB_SET),
    # A measured row with a body still clears.
    (None, BODIED, so.STUB_CLEAR),
])
def test_guards_do_not_swallow_real_stubs(error, symbol, expected):
    assert so.stub_action(error, symbol, was_stub=True) == expected
    assert _old_stub_action(error, symbol, True) == expected, \
        "these cases must be UNCHANGED by the fix, or the fix is too broad"


def test_void_never_fires_on_a_row_that_was_not_flagged():
    """Voiding is a repair, not an opinion: it may only touch rows already
    carrying the flag, or a sync pass would start writing verdict_reason over
    thousands of untouched rows."""
    for error, symbol in (("not_found", RETIRED), ("unimplemented", FUNCLET)):
        assert so.stub_action(error, symbol, was_stub=False) == so.STUB_NONE


def test_funclet_predicate_covers_both_spellings():
    assert so.is_funclet_symbol("fn_825CE39C")
    assert so.is_funclet_symbol("__unwind$110262")
    assert so.is_funclet_symbol("__catch$4711")
    # Not a funclet: a real function, and a near-miss that must not be caught.
    assert not so.is_funclet_symbol("?Poll@PlatformMgr@@QAAXXZ")
    assert not so.is_funclet_symbol("fn_notanaddress")
    assert not so.is_funclet_symbol("fn_825CE39")  # 7 hex digits, not 8


# =========================================================================== #
# 4. The COFF reader itself -- the substrate the NOT_A_STUB verdict rests on.
# =========================================================================== #

def test_coff_reader_finds_a_defined_symbol_and_not_an_undefined_one(tmp_path):
    obj = tmp_path / "x.obj"
    obj.write_bytes(_coff([BODIED], section_size=0x140))
    got = sfa.coff_defined_symbols(str(obj))
    assert got == {BODIED: 0x140}
    assert STUB not in got


def test_coff_reader_ignores_undefined_symbols(tmp_path):
    """SectionNumber == 0 is IMAGE_SYM_UNDEFINED -- an external reference, not
    a definition.  Counting one would manufacture NOT_A_STUB for a function we
    only CALL, which is the one direction this check must never fail."""
    nsym, nsec = 1, 1
    symptr = 20 + nsec * 40
    hdr = struct.pack("<HHIIIHH", 0x01F2, nsec, 0, symptr, nsym, 0, 0)
    sec = b".text\0\0\0" + struct.pack("<IIIIIIHHI", 0, 0, 0x40,
                                       symptr + 18, 0, 0, 0, 0, 0x60500020)
    strtab = bytearray(b"\0\0\0\0")
    off = len(strtab)
    strtab += STUB.encode() + b"\0"
    sym = struct.pack("<IIIhHBB", 0, off, 0, 0, 0x20, 2, 0)  # SectionNumber 0
    struct.pack_into("<I", strtab, 0, len(strtab))
    obj = tmp_path / "u.obj"
    obj.write_bytes(hdr + sec + sym + bytes(strtab))
    assert sfa.coff_defined_symbols(str(obj)) == {}


# =========================================================================== #
# 5. Anon-namespace / scope-ordinal normalization.
# =========================================================================== #

def test_anon_ns_hash_difference_is_not_a_missing_body(tmp_path):
    """Measured on the real tree: 26 rows our objects demonstrably define were
    called REAL_STUB because the target spells the anonymous-namespace hash
    ?A0xad24ca77 and we spell it ?A0xda23fae1."""
    theirs = "?CopyDepth@?A0xad24ca77@@YAXHHPIAGPIBG@Z"
    ours = "?CopyDepth@?A0xda23fae1@@YAXHHPIAGPIBG@Z"
    assert theirs != ours
    assert sfa.normalize_name(theirs) == sfa.normalize_name(ours)


def test_scope_ordinal_difference_is_not_a_missing_body():
    theirs = "??__F_dw@?BJ@??DrawFacesInRange@DxMesh@@UAAXHH@Z@YAXXZ"
    ours = "??__F_dw@?BC@??DrawFacesInRange@DxMesh@@UAAXHH@Z@YAXXZ"
    assert theirs != ours
    assert sfa.normalize_name(theirs) == sfa.normalize_name(ours)


def test_normalization_does_not_collapse_different_functions():
    """It must fold noise, not identity -- otherwise a real missing body could
    be cleared by an unrelated function that happens to share a shape."""
    a = "?CopyDepth@?A0xad24ca77@@YAXHHPIAGPIBG@Z"
    b = "?CopyPlayerMask@?A0xad24ca77@@YAXHPIAGPIBGH@Z"
    assert sfa.normalize_name(a) != sfa.normalize_name(b)
