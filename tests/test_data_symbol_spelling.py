#!/usr/bin/env python3
"""Sabotage tests for `scripts/verify_data_symbol_spelling.py`.

Same discipline as `tests/test_objs_patched.py`: every case asserts GREEN on a
healthy fixture, plants exactly one defect, asserts RED **pinning the reason**,
then restores and asserts GREEN again.  The fixtures are synthetic COFF
objects built by the script's own `synth_obj`, so the tests run in a tree that
has never been compiled -- and `SyntheticObjectSanityTest` proves the fixture
carries what the other cases assume it carries (a fixture with no static datum
would make every "the check found the static" assertion pass vacuously).

The mutation harness `tests/sabotage_objs_patched.py` edits the script under
test and requires the named cases here to go red.

Run:  python3 -m pytest tests/test_data_symbol_spelling.py -q
"""
from __future__ import annotations

import importlib.util
import io
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPT = REPO_ROOT / "scripts" / "verify_data_symbol_spelling.py"


def _load(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


dss = _load(SCRIPT, "_dss_under_test")

FN = ("?f@@YAXXZ", 0, 1, 0x20, 2)          # a function in .text
STATIC_LONG = ("gRndThread", 0x10, 2, 0, 3)  # >8 chars: string-table name
STATIC_SHORT = ("gFoo", 0x14, 2, 0, 3)       # <=8 chars: inline name
GLOBAL = ("?gNumHeaps@@3HA", 0x18, 2, 0, 2)


class SyntheticObjectSanityTest(unittest.TestCase):
    def test_fixture_parses_back_with_both_name_encodings(self):
        obj = dss.CoffObject(dss.synth_obj([FN, STATIC_LONG, STATIC_SHORT, GLOBAL]))
        names = {s.name: s for s in obj.symbols}
        self.assertEqual(set(names), {FN[0], "gRndThread", "gFoo", "?gNumHeaps@@3HA"})
        self.assertEqual(obj.n_symbol_entries, 4)
        self.assertTrue(names["?f@@YAXXZ"].is_function)
        self.assertFalse(names["gRndThread"].is_function)
        self.assertEqual(names["gRndThread"].cls, dss.IMAGE_SYM_CLASS_STATIC)
        self.assertEqual(names["?gNumHeaps@@3HA"].cls, dss.IMAGE_SYM_CLASS_EXTERNAL)
        # Negative control: the section-code flag is what excludes .text.
        self.assertTrue(obj.section_flags[0] & dss.IMAGE_SCN_CNT_CODE)
        self.assertFalse(obj.section_flags[1] & dss.IMAGE_SCN_CNT_CODE)

    def test_classify_partitions_by_spelling(self):
        bare, mangled = dss.classify(dss.CoffObject(
            dss.synth_obj([FN, STATIC_LONG, STATIC_SHORT, GLOBAL])))
        self.assertEqual(set(bare), {"gRndThread", "gFoo"})
        self.assertEqual(set(mangled), {"gNumHeaps"})


class TreeFixture(unittest.TestCase):
    """A temp tree shaped like build/<v>/{src,obj,asm}/u/x.*."""

    def setUp(self):
        self._td = tempfile.TemporaryDirectory(prefix="dss-test-")
        self.addCleanup(self._td.cleanup)
        root = Path(self._td.name)
        self.src, self.obj, self.asm = root / "src", root / "obj", root / "asm"
        for d in (self.src / "u", self.obj / "u", self.asm / "u"):
            d.mkdir(parents=True)

    def plant(self, ours, theirs, listing=None):
        (self.src / "u" / "x.obj").write_bytes(dss.synth_obj(ours))
        (self.obj / "u" / "x.obj").write_bytes(dss.synth_obj(theirs))
        (self.asm / "u" / "x.s").write_text(
            listing if listing is not None else dss._listing_for(theirs))

    def check(self):
        out, err = io.StringIO(), io.StringIO()
        rc = dss.run_check(self.src, self.obj, self.asm, None, out=out, err=err)
        return rc, out.getvalue(), err.getvalue()

    def assert_green(self):
        rc, out, err = self.check()
        self.assertEqual(rc, 0, f"expected green; stderr:\n{err}")
        self.assertIn("0 bare-vs-mangled disagreements", out)
        return out

    def assert_red(self, reason: str, rc_expected: int = dss.EXIT_DISAGREEMENT):
        rc, _out, err = self.check()
        self.assertEqual(rc, rc_expected, f"expected exit {rc_expected}, got {rc}; stderr:\n{err}")
        self.assertIn(reason, err, f"red, but not for {reason!r}; stderr:\n{err}")
        return err


class DisagreementTest(TreeFixture):
    def test_static_vs_mangled_target_is_red_and_names_the_variable(self):
        self.plant([FN, ("gUsingCD", 0x10, 2, 0, 3)], [FN, ("gUsingCD", 0, 3, 0, 2)])
        self.assert_green()                                          # control
        self.plant([FN, STATIC_LONG], [FN, ("?gRndThread@@3PAXA", 0, 3, 0, 2)])
        err = self.assert_red("gRndThread")
        self.assertIn("storage class STATIC", err)
        self.assertIn("?gRndThread@@3PAXA", err)
        self.assertIn("symbols.txt", err, "the fix hint must name the config file")
        self.plant([FN, ("gRndThread", 0x10, 2, 0, 3)], [FN, ("gRndThread", 0, 3, 0, 2)])
        self.assert_green()                                          # restore

    def test_the_inline_short_name_path_is_scanned_too(self):
        """An 8-byte inline COFF name is a different decode path from the
        string table; a checker that only read the string table would report
        'clean' over the subset it could see."""
        self.plant([FN, STATIC_SHORT], [FN, ("?gFoo@@3HA", 0, 3, 0, 2)])
        self.assert_red("gFoo")

    def test_global_ours_vs_bare_target_is_red(self):
        self.plant([FN, GLOBAL], [FN, ("gNumHeaps", 0, 3, 0, 2)])
        err = self.assert_red("gNumHeaps")
        self.assertIn("storage class EXTERNAL", err)
        self.assertIn("make gNumHeaps `static`", err)

    def test_a_target_that_only_references_the_datum_still_charges(self):
        """The datum can live in another unit's split range, so the target
        object carries it as an UNDEFINED external -- objdiff still names it
        at the relocation, so it is still a disagreement."""
        self.plant([FN, STATIC_LONG], [FN, ("?gRndThread@@3PAXA", 0, 0, 0, 2)], listing="")
        self.assert_red("gRndThread")

    def test_agreement_in_either_spelling_is_green(self):
        self.plant([FN, ("gUsingCD", 0x10, 2, 0, 3)], [FN, ("gUsingCD", 0, 3, 0, 2)])
        self.assert_green()
        self.plant([FN, GLOBAL], [FN, GLOBAL])
        self.assert_green()

    def test_out_of_scope_spellings_are_not_charged(self):
        """Anonymous-namespace members and class statics are other classes
        (the anon-ns patcher's, and `@@2`); placeholders carry no claim; a
        bare FUNCTION name is not a datum."""
        self.plant([FN, ("sThreadData", 0x10, 2, 0, 3), ("gX", 0x20, 2, 0, 3),
                    ("gRev", 0x24, 2, 0, 3), ("memcpy", 0, 1, 0x20, 2)],
                   [FN, ("?sThreadData@?A0x1234abcd@@3HA", 0, 3, 0, 2),
                    ("?gX@Cls@@2HA", 0, 3, 0, 2), ("lbl_82F14008", 0, 3, 0, 2),
                    ("?memcpy@@3HA", 0, 3, 0, 2)])
        self.assert_green()
        # In-test negative control: the same fixture with ONE in-scope pair
        # added must go red, or the green above proved nothing.
        self.plant([FN, ("sThreadData", 0x10, 2, 0, 3), STATIC_SHORT],
                   [FN, ("?sThreadData@?A0x1234abcd@@3HA", 0, 3, 0, 2),
                    ("?gFoo@@3HA", 0, 3, 0, 2)])
        self.assert_red("gFoo")

    def test_denominators_are_printed(self):
        self.plant([FN, ("gUsingCD", 0x10, 2, 0, 3)], [FN, ("gUsingCD", 0, 3, 0, 2)])
        out = self.assert_green()
        self.assertIn("1 paired units", out)
        self.assertIn("1 target objects cross-checked", out)


class CrossCheckTest(TreeFixture):
    """The target side is enumerated twice; a disagreement is a REFUSAL."""

    def test_listing_naming_a_datum_the_coff_table_lacks_is_refused(self):
        self.plant([FN, ("gUsingCD", 0x10, 2, 0, 3)], [FN, ("gUsingCD", 0, 3, 0, 2)])
        self.assert_green()
        self.plant([FN, ("gUsingCD", 0x10, 2, 0, 3)], [FN, ("gUsingCD", 0, 3, 0, 2)],
                   listing='.obj "gUsingCD", global\n.obj "?gGhost@@3HA", global\n')
        err = self.assert_red("CROSS-CHECK", dss.EXIT_CROSS_CHECK)
        self.assertIn("?gGhost@@3HA", err)

    def test_coff_table_naming_a_datum_the_listing_lacks_is_refused(self):
        self.plant([FN, ("gUsingCD", 0x10, 2, 0, 3)], [FN, ("gUsingCD", 0, 3, 0, 2)],
                   listing="")
        err = self.assert_red("CROSS-CHECK", dss.EXIT_CROSS_CHECK)
        self.assertIn("gUsingCD", err)

    def test_a_refusal_outranks_a_disagreement(self):
        """If the enumeration is suspect, the disagreement count is not a
        measurement, so the refusal must win even when a real defect is
        also present."""
        self.plant([FN, STATIC_LONG], [FN, ("?gRndThread@@3PAXA", 0, 3, 0, 2)],
                   listing="")
        self.assert_red("CROSS-CHECK", dss.EXIT_CROSS_CHECK)

    def test_listing_directive_parser_handles_quoted_and_bare_names(self):
        names = dss.listing_data_names(
            '.obj "?gRndThread@@3PAXA", global\n'
            '.obj lbl_82092070, global\n'
            '.obj gUsingCD, local\n'
            '.fn "?f@@YAXXZ", global\n'          # a function: not a datum
            '  .obj "  indented ", global\n')
        self.assertEqual(names, {"?gRndThread@@3PAXA", "lbl_82092070", "gUsingCD",
                                 "  indented "})


class RefusalTest(unittest.TestCase):
    def test_an_empty_universe_is_a_refusal_not_a_pass(self):
        with tempfile.TemporaryDirectory(prefix="dss-test-") as td:
            root = Path(td)
            (root / "src").mkdir()
            (root / "obj").mkdir()
            err = io.StringIO()
            rc = dss.run_check(root / "src", root / "obj", None, None,
                               out=io.StringIO(), err=err)
        self.assertEqual(rc, dss.EXIT_EMPTY_UNIVERSE)
        self.assertIn("not a pass", err.getvalue())

    def test_selftest_is_not_vacuous(self):
        """The script's own --selftest must contain cases that REQUIRE a
        failure; exit 5 is what it reports when none does."""
        self.assertEqual(dss.selftest(), 0)
        self.assertNotEqual(dss.EXIT_SELFTEST_VACUOUS, 0)


if __name__ == "__main__":
    unittest.main()
