"""`callee_emitted_anywhere` must DISCRIMINATE, and must not answer questions it
was never able to ask.

The check's whole value is one bit per row, so the ways it can be worthless are
few and specific:

  * it says NOT_EMITTED for everything (an empty our-side universe -- the shape
    that looks like a bumper harvest of bugs and is really an unbuilt tree);
  * it says EMITTED for everything (a universe that swallows every name);
  * it manufactures NOT_EMITTED out of its own bad guess at a detector payload
    (the MakeString reconstruction);
  * it reports "nothing flagged" over rows it silently skipped.

Every test below names the sabotage that must turn it red.  The COFF fixtures
are synthesised in-process, so nothing here depends on a built tree and nothing
can pass by skipping.
"""

from __future__ import annotations

import struct
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from scripts.analysis import callee_emitted_anywhere as cea  # noqa: E402
from scripts.orchestrator.callee_gate import LinkerMap  # noqa: E402

IMAGE_SYM_CLASS_EXTERNAL = 2
IMAGE_SYM_CLASS_STATIC = 3


def write_coff(path: Path, symbols) -> Path:
    """A minimal COFF object carrying exactly `symbols`.

    `symbols` is a sequence of (name, section_number, value, storage_class).
    Names longer than 8 bytes go through the string table, which is the path the
    real Xbox 360 objects take for every mangled C++ name -- so a reader that
    only handles the inline form would pass a fixture built any other way.
    """
    recs, strtab = [], bytearray(b"\0\0\0\0")
    for name, secnum, value, sclass in symbols:
        raw = name.encode("latin1")
        if len(raw) <= 8:
            field = raw.ljust(8, b"\0")
        else:
            field = struct.pack("<II", 0, len(strtab))
            strtab += raw + b"\0"
        recs.append(field + struct.pack("<IhHBB", value, secnum, 0, sclass, 0))
    struct.pack_into("<I", strtab, 0, len(strtab))
    symptr = 20
    header = struct.pack("<HHIIIHH", 0x01F2, 0, 0, symptr, len(recs), 0, 0)
    path.write_bytes(header + b"".join(recs) + bytes(strtab))
    return path


SYMBOLS = [
    ("?Defined@@YAXXZ", 1, 0x10, IMAGE_SYM_CLASS_EXTERNAL),
    ("?Referenced@@YAXXZ", 0, 0, IMAGE_SYM_CLASS_EXTERNAL),
    ("?StaticOnly@@YAXXZ", 1, 0x20, IMAGE_SYM_CLASS_STATIC),
    ("short", 1, 0x30, IMAGE_SYM_CLASS_STATIC),
]


class TestCoffReader(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)
        self.obj = write_coff(self.tmp / "fixture.obj", SYMBOLS)

    def test_reads_every_storage_class_and_both_name_forms(self):
        """SABOTAGE: filter `coff_symbol_names` to IMAGE_SYM_CLASS_EXTERNAL, or
        drop the string-table branch -> the long/static names disappear and the
        check invents NOT_EMITTED for callees we really do emit."""
        names = cea.coff_symbol_names(str(self.obj))
        self.assertEqual(names, {n for n, *_ in SYMBOLS})

    def test_is_a_superset_of_the_repos_external_reader(self):
        """The widening must stay a widening. SABOTAGE: as above -- or any
        rewrite that changes WHICH names are read rather than how many."""
        import check_undefined_decomp_symbols as cuds
        defined, undef = cuds.coff_externals(str(self.obj))
        # Negative control: the oracle must actually see something here, or the
        # subset assertion below is true of an empty set.
        self.assertEqual(defined | undef,
                         {"?Defined@@YAXXZ", "?Referenced@@YAXXZ"})
        self.assertTrue((defined | undef) <= cea.coff_symbol_names(str(self.obj)))

    def test_a_truncated_object_yields_nothing_rather_than_raising(self):
        """SABOTAGE: remove the length guards -> a partially written object
        (a concurrent build) takes the whole run down."""
        (self.tmp / "stub.obj").write_bytes(b"\x00" * 8)
        self.assertEqual(cea.coff_symbol_names(str(self.tmp / "stub.obj")), set())


def _map(addr: dict[str, str]) -> LinkerMap:
    by_addr: dict[str, list[str]] = {}
    for name, a in addr.items():
        by_addr.setdefault(a, []).append(name)
    return LinkerMap(addr, by_addr, set(), Path("fixture.map"))


class TestAliasClass(unittest.TestCase):
    def test_fold_group_members_are_equivalent_spellings(self):
        """A callee is checked as a CLASS, because /OPT:ICF folded bodies and
        the map prints whichever member the linker kept.

        SABOTAGE: return `{name}` from `alias_class` -> a folded spelling we do
        emit reads NOT_EMITTED.
        """
        lmap = _map({"?A@@YAXXZ": "82001000", "?B@@YAXXZ": "82001000",
                     "?C@@YAXXZ": "82002000"})
        names, addr = cea.alias_class("?A@@YAXXZ", lmap, {})
        self.assertEqual(addr, "82001000")
        self.assertIn("?B@@YAXXZ", names)
        self.assertNotIn("?C@@YAXXZ", names)   # control: not everything joins

    def test_adjudicated_json_groups_are_honoured_without_the_map(self):
        """SABOTAGE: drop the `groups.get(name)` union."""
        lmap = _map({})
        names, addr = cea.alias_class(
            "?A@@YAXXZ", lmap, {"?A@@YAXXZ": {"?A@@YAXXZ", "?Alias@@YAXXZ"}})
        self.assertIsNone(addr)
        self.assertIn("?Alias@@YAXXZ", names)


class TestAdjudication(unittest.TestCase):
    def setUp(self):
        self.lmap = _map({"?InImage@@YAXXZ": "82001000",
                          "?Folded@@YAXXZ": "82001000"})
        self.universe = {"?Folded@@YAXXZ", "?Other@@YAXXZ"}

    def _row(self, target, **kw):
        row = {"function": "f", "unit": "u", "pairing": "asserted",
               "target_callee": target, "base_callee": "?Other@@YAXXZ",
               "reconstructed": False}
        row.update(kw)
        return row

    def test_the_two_verdicts_differ(self):
        """THE negative control: a name we emit and a name we do not must not
        agree. SABOTAGE: hardcode either verdict."""
        rows = [self._row("?InImage@@YAXXZ"),          # via its fold group
                self._row("?AbsentEverywhere@@YAXXZ")]
        cea.adjudicate(rows, self.universe, self.lmap, {})
        self.assertEqual(rows[0]["verdict"], "EMITTED")
        self.assertEqual(rows[0]["matched_spelling"], "?Folded@@YAXXZ")
        self.assertEqual(rows[1]["verdict"], "NOT_EMITTED")

    def test_an_empty_universe_is_not_a_harvest(self):
        """With no objects read, EVERY row reads NOT_EMITTED -- which is why the
        callers refuse to run on an empty universe. This test pins the fact that
        the adjudicator itself cannot tell the difference, so the guard has to
        live upstream of it.

        SABOTAGE: delete the `if not universe` guard in `main`/`selftest` ->
        the tool reports a bumper crop of bugs from an unbuilt tree.
        """
        rows = [self._row("?InImage@@YAXXZ")]
        cea.adjudicate(rows, set(), self.lmap, {})
        self.assertEqual(rows[0]["verdict"], "NOT_EMITTED")

    def test_a_bad_reconstruction_is_not_served_as_a_finding(self):
        """A name rebuilt from a template fragment that resolves NOWHERE is
        evidence the reconstruction is wrong, not that the image lacks a symbol.

        SABOTAGE: drop the `reconstructed and addr is None` branch -> the whole
        MakeString class reports as "names the image has and we do not".
        """
        rows = [self._row("?NotAThing@@YAXXZ", reconstructed=True)]
        cea.adjudicate(rows, self.universe, self.lmap, {})
        self.assertEqual(rows[0]["verdict"], "NO_EVIDENCE")
        self.assertIn("reconstruction", rows[0]["reason"])

    def test_a_reconstruction_the_map_confirms_is_adjudicated_normally(self):
        """Control for the test above: the escape hatch must not swallow the
        class it was added for. SABOTAGE: return NO_EVIDENCE for every
        reconstructed row."""
        rows = [self._row("?InImage@@YAXXZ", reconstructed=True)]
        cea.adjudicate(rows, self.universe, self.lmap, {})
        self.assertEqual(rows[0]["verdict"], "EMITTED")


class TestPayloadShapes(unittest.TestCase):
    def test_divergent_callees_payload(self):
        """SABOTAGE: read the wrong key -> every WRONG_CALLEE row becomes
        NO_EVIDENCE and the class reports clean."""
        pairs = cea._callee_pairs(
            {"divergent_callees": [{"target_symbol": "?T@@YAXXZ",
                                    "base_symbol": "?B@@YAXXZ"}]})
        self.assertEqual(pairs, [("?T@@YAXXZ", "?B@@YAXXZ", False)])

    def test_makestring_template_payload_is_reassembled(self):
        """objdiff reports the template ARGUMENT LIST for this class.

        SABOTAGE: drop the `mismatches` branch -> all 14 MAKESTRING rows in scan
        18 read NO_EVIDENCE while the summary says "nothing flagged".
        """
        pairs = cea._callee_pairs(
            {"mismatches": [{"target_template": "W4_D3DFORMAT@@@@YAPBDPBDABW4"
                                                "_D3DFORMAT@@@Z",
                             "base_template": "I@@YAPBDPBDABI@Z"}]})
        self.assertEqual(len(pairs), 1)
        target, base, recon = pairs[0]
        self.assertTrue(recon, "a reassembled name must be marked reconstructed")
        self.assertTrue(target.startswith("??$MakeString@"))
        self.assertTrue(base.startswith("??$MakeString@"))

    def test_an_unknown_payload_yields_no_pairs(self):
        """Control: the reader must not invent a pair out of a shape it does not
        know. SABOTAGE: fall back to stringifying the payload."""
        self.assertEqual(cea._callee_pairs({"something_else": [1, 2]}), [])


class TestRendering(unittest.TestCase):
    def _rows(self, verdicts):
        return [{"function": "f", "unit": "u", "pairing": "guessed",
                 "target_callee": "?T@@YAXXZ", "base_callee": "?B@@YAXXZ",
                 "verdict": v, "reason": "unadjudicable" if v == "NO_EVIDENCE"
                 else None, "in_retail_map": True, "alias_class_size": 1}
                for v in verdicts]

    def test_all_skipped_does_not_read_as_a_clean_class(self):
        """The exact failure this script exists to avoid, applied to itself.

        SABOTAGE: delete the `len(no_ev) == len(rows)` branch -> a class where
        nothing could be adjudicated prints "NOTHING FLAGGED".
        """
        text = cea.render(self._rows(["NO_EVIDENCE"] * 3), 10, 1, 18, "P", "any")
        self.assertIn("NOTHING ADJUDICATED", text)
        self.assertIn("unasked question", text)

    def test_a_genuinely_clean_class_says_clean(self):
        """Control for the test above."""
        text = cea.render(self._rows(["EMITTED"] * 3), 10, 1, 18, "P", "any")
        self.assertIn("NOTHING FLAGGED", text)
        self.assertNotIn("NOTHING ADJUDICATED", text)

    def test_skipped_rows_are_counted_even_beside_adjudicated_ones(self):
        """SABOTAGE: drop the `if no_ev:` block -> 13 skipped rows hide behind
        one EMITTED."""
        text = cea.render(self._rows(["EMITTED"] + ["NO_EVIDENCE"] * 13),
                          10, 1, 18, "P", "any")
        self.assertIn("NOT ADJUDICATED", text)
        self.assertIn("13", text)

    def test_a_flag_names_the_unit_and_both_callees(self):
        """A flagged row has to be actionable without a second tool.
        SABOTAGE: print only the count."""
        text = cea.render(self._rows(["NOT_EMITTED"]), 10, 1, 18, "P", "any")
        self.assertIn("FLAGGED", text)
        self.assertIn("?T@@YAXXZ", text)
        self.assertIn("?B@@YAXXZ", text)


class TestSelftestRefusesToBeVacuous(unittest.TestCase):
    def test_empty_project_exits_vacuous(self):
        """SABOTAGE: return EXIT_OK when the universe is empty -> --selftest
        goes green on a tree with no objects, certifying nothing."""
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(cea.selftest(Path(tmp)), cea.EXIT_SELFTEST)


if __name__ == "__main__":
    unittest.main()
