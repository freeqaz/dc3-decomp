"""Regression tests: which key on an `objdiff-cli diff` payload is CANONICAL.

`batch_check.py` and `sync_objdiff.py` each carry a `match_percent_from_diff`,
and both read `normalized_match_percent` believing it to be "the canonical
scorer". It is not, and objdiff-cli says so in its own source::

    /// MISNOMER, kept for compatibility. This is the FUZZY score measured under
    /// a relaxed RELOCATION mode -- it is not `match_percent_normalized` ...
    /// Read `canonical_match_percent` if you want the number `report.json`
    /// reports.

`canonical_match_percent` arrived in objdiff-cli 4.2.4. Both readers predate it
and neither noticed, so a promotion gate was reading a ruler its own promotion
criterion does not use.

MEASURED 2026-08-23 on this tree, under `-c functionRelocDiffs=none` (the mode
`sync_objdiff.py` actually passes, and the one its docstring claimed made the
two agree):

    ?asciiDigitToHex@@YAED@Z   normalized 95.55556  canonical 100.0
    ?parseHex16@@YAXPBDPAE@Z   normalized 96.59091  canonical 100.0
    ?roll@@YAHH@Z              normalized 94.583336 canonical 100.0

Across report.json: 308 of 31,813 functions are canonical-100 with the fuzzy
score below 100; 0 are the reverse. So the error never manufactured a COMPLETE
-- it suppressed 308 real ones, and its demotion arm fought
`sync_match_percent.py --promote` over exactly that population. That is the
"COMPLETE verdicts flapping" already documented in sync_objdiff.py's header;
this is its cause.

Hermetic: no objdiff-cli, no build, no database.
"""
import importlib.util
import sys
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SCRIPTS))


def _load(name: str):
    """Import a top-level script by path (they are not a package)."""
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


#: The three live payloads above, verbatim.
LIVE_CASES = [
    ("?asciiDigitToHex@@YAED@Z", 95.55556, 100.0),
    ("?parseHex16@@YAXPBDPAE@Z", 96.59091, 100.0),
    ("?roll@@YAHH@Z", 94.583336, 100.0),
]


class _RulerContract:
    """Shared contract. Subclasses set `fn`."""

    fn = None

    def test_canonical_is_preferred_over_the_misnomer(self):
        for sym, fuzzy, canonical in LIVE_CASES:
            with self.subTest(symbol=sym):
                pct, ruler = self.fn({
                    "symbol": sym,
                    "fuzzy_match_percent": fuzzy,
                    "normalized_match_percent": fuzzy,
                    "canonical_match_percent": canonical,
                    "raw_match_percent": fuzzy,
                })
                self.assertEqual(pct, canonical)
                self.assertEqual(ruler, "canonical")

    def test_the_308_class_now_reaches_100(self):
        # The whole point: these rows must be promotable. Reading the misnomer
        # returned 95.55556 and no gate keyed on `== 100` could ever fire.
        pct, _ = self.fn({"normalized_match_percent": 95.55556,
                          "fuzzy_match_percent": 95.55556,
                          "canonical_match_percent": 100.0})
        self.assertEqual(pct, 100.0)

    def test_a_pre_4_2_4_payload_is_labelled_fuzzy_not_normalized(self):
        # Falling back is fine. Calling the fallback "normalized" is not: that
        # word names a fuzzy score in objdiff's vocabulary, and a log line
        # saying "normalized" over a fuzzy number is how this survived.
        pct, ruler = self.fn({"normalized_match_percent": 95.55556,
                              "fuzzy_match_percent": 95.55556})
        self.assertEqual(pct, 95.55556)
        self.assertIn("fuzzy", ruler)
        self.assertNotIn("normalized", ruler.split(";")[0])
        self.assertIn("4.2.4", ruler)

    def test_no_percent_at_all(self):
        self.assertEqual(self.fn({"symbol": "x"}), (None, "none"))

    def test_canonical_wins_even_when_it_is_LOWER(self):
        # Under name_check (objdiff >= 4.2.4) a vetted relocation-name
        # disagreement stays in diff_score, so canonical CAN sit below fuzzy.
        # Preference must be by NAME, never by "whichever is larger" -- a
        # max() would have looked right on all 308 live rows and been wrong
        # here, which is exactly the kind of accident this file exists to stop.
        pct, ruler = self.fn({"normalized_match_percent": 100.0,
                              "fuzzy_match_percent": 100.0,
                              "canonical_match_percent": 84.7})
        self.assertEqual(pct, 84.7)
        self.assertEqual(ruler, "canonical")


class TestBatchCheck(_RulerContract, unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fn = staticmethod(_load("batch_check").match_percent_from_diff)


class TestSyncObjdiff(_RulerContract, unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fn = staticmethod(_load("sync_objdiff").match_percent_from_diff)


class TestBothReadersAgree(unittest.TestCase):
    """Two copies of one function is how one of them drifts.

    They already had: `batch_check.py` was writing `equal_percent` (a third
    ruler) until 2026-08-19. Pin agreement so a future edit to one is caught.
    """

    def test_identical_answers_on_every_payload_shape(self):
        a = _load("batch_check").match_percent_from_diff
        b = _load("sync_objdiff").match_percent_from_diff
        payloads = [
            {"canonical_match_percent": 100.0, "normalized_match_percent": 95.5,
             "fuzzy_match_percent": 95.5, "raw_match_percent": 90.0},
            {"normalized_match_percent": 95.5, "fuzzy_match_percent": 95.5},
            {"fuzzy_match_percent": 42.0},
            {"canonical_match_percent": 0.0, "fuzzy_match_percent": 0.0},
            {},
        ]
        for p in payloads:
            with self.subTest(payload=sorted(p)):
                self.assertEqual(a(p), b(p))


if __name__ == "__main__":
    unittest.main()
