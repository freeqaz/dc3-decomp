"""Regression tests: "Symbol not found" must say WHERE it looked.

The bug, observed live on 2026-08-22::

    Failed: Symbol not found: ?GetNumSongs@Playlist@@QBAHXZ
    Did you mean:
      - `?GetNumSongs@Playlist@@QBAHXZ` (0.0%)

A byte-identical suggestion, which reads as a broken tool. It was not broken --
it was answering a different question. `decomp.db` knows the symbol (and files
it under `default/system/rndobj/Text`, a `Playlist` method attributed to
`RndText`); the DIFF failed because no TARGET object defines it anywhere. Our
base build does, in `default/lazer/meta_ham/Playlist`.

Those are three distinct outcomes that shared one message:

  * only in BASE   -> symbol-attribution artifact; usually nothing to fix
  * only in TARGET -> real unimplemented work
  * nowhere        -> spelling, or the wrong repo

Objects are built here with `struct.pack`, so these tests are hermetic: no
objdiff-cli, no build, no database. The COFF fixtures are minimal but real --
`coffx.read_coff` parses them the same way it parses a 160 MB tree.
"""
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from orchestrator.symbol_locator import (  # noqa: E402
    SymbolLocation,
    format_not_found,
    locate_symbol,
    retry_hint,
)

IMAGE_SYM_CLASS_EXTERNAL = 2


def make_coff(defined: list[str], undefined: list[str]) -> bytes:
    """A minimal but genuinely parseable COFF object.

    One `.text` section; every name goes in the string table (so it is found by
    the stage-1 substring pass exactly as a real long MSVC name would be), and
    each symbol carries either a real section number (DEFINED) or 0 with
    storage class EXTERNAL (UNDEFINED reference).
    """
    names = [(n, 1) for n in defined] + [(n, 0) for n in undefined]
    nsym = len(names)
    symoff = 20 + 40
    strtab_off = symoff + nsym * 18

    strtab = bytearray(struct.pack("<I", 4))   # size field, patched below
    offsets = []
    for n, _sec in names:
        offsets.append(len(strtab))
        strtab += n.encode("ascii") + b"\0"
    struct.pack_into("<I", strtab, 0, len(strtab))

    out = bytearray()
    # File header: machine, nsec, timestamp, symoff, nsym, optsz, characteristics
    out += struct.pack("<HHIIIHH", 0x01F2, 1, 0, symoff, nsym, 0, 0)
    # One section: .text, no raw data, no relocations
    out += b".text\0\0\0" + struct.pack("<IIIIIIHH", 0, 0, 0, 0, 0, 0, 0, 0) \
        + struct.pack("<I", 0x60000020)
    assert len(out) == symoff
    for (n, sec), off in zip(names, offsets):
        out += struct.pack("<I", 0) + struct.pack("<I", off)   # long-name form
        out += struct.pack("<I", 0)                            # value
        out += struct.pack("<h", sec)                          # section number
        out += struct.pack("<H", 0x20)                         # type = function
        out += bytes([IMAGE_SYM_CLASS_EXTERNAL, 0])            # class, naux
    assert len(out) == strtab_off
    out += strtab
    return bytes(out)


class _Project:
    """A throwaway project tree with objdiff.json and hand-built objects."""

    def __init__(self, units: dict):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        cfg_units = []
        for name, sides in units.items():
            entry = {"name": name}
            for side, key in (("target", "target_path"), ("base", "base_path")):
                spec = sides.get(side)
                if spec is None:
                    continue
                rel = f"obj/{side}/{name.replace('/', '_')}.obj"
                p = self.root / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(make_coff(spec.get("defined", []),
                                        spec.get("undefined", [])))
                entry[key] = rel
            cfg_units.append(entry)
        (self.root / "objdiff.json").write_text(json.dumps({"units": cfg_units}))

    def __enter__(self):
        return self.root

    def __exit__(self, *a):
        self.tmp.cleanup()


SYM = "?GetNumSongs@Playlist@@QBAHXZ"
OTHER = "?SomethingElse@Other@@QAAXXZ"


class TestFixtureIsReal(unittest.TestCase):
    """Positive control on the fixture itself.

    Everything below rests on `make_coff` producing something `coffx` actually
    parses. If it silently produced garbage, every locate would come back empty
    and every test asserting "found in target only" would fail loudly -- but the
    tests asserting ABSENCE would pass for the wrong reason. That is the exact
    trap that made a probe report "32 of 32 symbols absent" when the real cause
    was zero bytes of output. So: assert the fixture is parseable and that the
    two symbol classes are distinguishable, before trusting any absence.
    """

    def test_coffx_parses_the_fixture_and_separates_defined_from_undefined(self):
        analysis = Path(__file__).resolve().parents[2] / "analysis"
        sys.path.insert(0, str(analysis))
        import coffx  # noqa: E402

        _secs, syms = coffx.read_coff(make_coff([SYM], [OTHER]))
        self.assertIsNotNone(syms)
        by_name = {s.name: s for s in syms}
        self.assertIn(SYM, by_name, "long names must reach the string table")
        self.assertIn(OTHER, by_name)
        self.assertGreater(by_name[SYM].sec, 0, "defined symbol has a section")
        self.assertEqual(by_name[OTHER].sec, 0, "undefined symbol has section 0")


class TestLocate(unittest.TestCase):
    def test_base_only_is_distinguished_from_nowhere(self):
        # The live case: our build defines it, no target object does.
        with _Project({"u/Playlist": {"base": {"defined": [SYM]},
                                      "target": {"defined": [OTHER]}}}) as root:
            loc = locate_symbol(root, SYM)
            self.assertEqual(loc.base_units, ["u/Playlist"])
            self.assertEqual(loc.target_units, [])
            msg = format_not_found(loc, None)
            self.assertIn("BASE build only", msg)

            # ...and the NEGATIVE control, same project, same call: a symbol
            # that really is absent produces a DIFFERENT message. Without this
            # the "base only" assertion could be passing on empty output.
            absent = locate_symbol(root, "?NotHere@X@@QAAXXZ")
            self.assertFalse(absent.found_anywhere)
            self.assertIn("in NO object in this project",
                          format_not_found(absent, None))

    def test_target_only_reads_as_unimplemented_work(self):
        with _Project({"u/Foo": {"target": {"defined": [SYM]},
                                 "base": {"defined": [OTHER]}}}) as root:
            msg = format_not_found(locate_symbol(root, SYM), None)
            self.assertIn("TARGET only", msg)
            self.assertIn("unimplemented work", msg)

    def test_wrong_unit_names_the_unit_to_retry_with(self):
        with _Project({"u/Right": {"target": {"defined": [SYM]},
                                   "base": {"defined": [SYM]}},
                       "u/Wrong": {"target": {"defined": [OTHER]},
                                   "base": {"defined": [OTHER]}}}) as root:
            msg = format_not_found(locate_symbol(root, SYM), "u/Wrong")
            self.assertIn("Defined on both sides", msg)
            self.assertIn("but not in `u/Wrong`", msg)
            self.assertIn("Retry with `unit=` one of: `u/Right`", msg)

    def test_referenced_but_never_defined(self):
        with _Project({"u/Caller": {"base": {"undefined": [SYM]},
                                    "target": {"defined": [OTHER]}}}) as root:
            loc = locate_symbol(root, SYM)
            self.assertEqual(loc.base_units, [])
            self.assertEqual(loc.base_refs, ["u/Caller"])
            self.assertIn("Referenced but never defined",
                          format_not_found(loc, None))

    def test_defined_on_both_sides_in_different_units(self):
        with _Project({"u/T": {"target": {"defined": [SYM]},
                               "base": {"defined": [OTHER]}},
                       "u/B": {"target": {"defined": [OTHER]},
                               "base": {"defined": [SYM]}}}) as root:
            msg = format_not_found(locate_symbol(root, SYM), None)
            self.assertIn("DIFFERENT units", msg)

    def test_an_unscannable_project_does_not_read_as_absence(self):
        # "I scanned nothing" and "it is nowhere" look identical in output and
        # mean opposite things. The denominator is what separates them.
        with tempfile.TemporaryDirectory() as d:
            loc = locate_symbol(d, SYM)          # no objdiff.json at all
            self.assertEqual(loc.stats["units_declared"], 0)
            msg = format_not_found(loc, None)
            self.assertIn("NOT evidence the symbol is absent", msg)
            self.assertNotIn("in NO object", msg)

    def test_the_denominator_is_always_reported(self):
        with _Project({"u/Foo": {"target": {"defined": [SYM]},
                                 "base": {"defined": [SYM]}}}) as root:
            msg = format_not_found(locate_symbol(root, SYM), "u/Nope")
            self.assertIn("objects,", msg)
            self.assertIn("Searched:", msg)


class TestNeverAnswersWithTheQuestion(unittest.TestCase):
    """The defect in one sentence: it suggested the string you typed.

    ⚠ The first version of this class tested the invariant only THROUGH
    ``format_not_found``, and a sabotage run proved it vacuous: deleting the
    self-exclusion entirely left every assertion passing, because the branch
    ordering already makes a self-suggestion unreachable from that direction.
    The contract is therefore exercised directly on ``retry_hint`` -- where
    removing the guard DOES fail -- and the end-to-end sweep below is kept as
    the regression test for the branch ordering itself.
    """

    def test_retry_hint_excludes_the_searched_unit(self):
        self.assertEqual(retry_hint(["u/A"], "u/A"), "")
        self.assertIn("`u/B`", retry_hint(["u/A", "u/B"], "u/A"))
        self.assertNotIn("`u/A`", retry_hint(["u/A", "u/B"], "u/A"))

    def test_retry_hint_still_offers_units_when_none_is_the_query(self):
        # Positive control: suppressing the self-suggestion must not suppress
        # every suggestion, or the guard is just an off switch.
        self.assertIn("`u/A`", retry_hint(["u/A"], "u/other"))
        self.assertIn("`u/A`", retry_hint(["u/A"], None))

    def test_no_configuration_ever_suggests_the_unit_just_searched(self):
        # Exhaustive over every placement of SYM across two units x two sides.
        # This is the guard against a future branch-ordering change reopening
        # the `_fmt_units(both or t)` hole.
        sides = ["target", "base"]
        names = ["u/A", "u/B"]
        for mask in range(16):
            units: dict = {n: {s: {"defined": [OTHER]} for s in sides}
                           for n in names}
            bit = 0
            for n in names:
                for s in sides:
                    if mask & (1 << bit):
                        units[n][s]["defined"] = [SYM, OTHER]
                    bit += 1
            with self.subTest(mask=mask), _Project(units) as root:
                loc = locate_symbol(root, SYM)
                for searched in names + [None]:
                    msg = format_not_found(loc, searched)
                    if searched:
                        self.assertNotIn(
                            f"Retry with `unit=` one of: `{searched}`", msg,
                            f"mask={mask} searched={searched}")


class TestFormatterIsPure(unittest.TestCase):
    def test_no_units_declared_is_the_only_unscannable_message(self):
        loc = SymbolLocation(symbol=SYM, stats={"units_declared": 0})
        self.assertIn("NOT evidence", format_not_found(loc, "x"))


if __name__ == "__main__":
    unittest.main()
