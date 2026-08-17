#!/usr/bin/env python3
"""`classify_diff_patterns` used to pattern-match objdiff's own JSON SCHEMA.

The caller hands it `cli_result["preview"]` -- the first 4000 characters of
objdiff-cli's `-f json` output, which is a single 160KB line. The prose/asm
regexes in `DIFF_PATTERNS` were then matched against that prefix, which is
mostly objdiff's metadata: an enum list of every pattern name objdiff knows, doc
URLs, field names. `.` never stops at a newline because the document has none.

Measured 2026-08-17 on six real dc3 symbols, before the fix:

  * the table fired STRUCT_OFFSET on 6/6 -- including `CopyTypeProperties` at
    fuzzy **100.00%**, where objdiff itself reports zero patterns. It was
    matching `OFFSET_SWAP","...","DYNAMIC_CAST_MISMATCH"` inside the
    `patterns_checked` enum list.
  * `bool.*mask` matched the literal `BOOL_MASK` in that same enum list.
  * `stack.*frame` matched a `doc_url`.
  * on the one pattern the two vocabularies share it was ANTI-correlated:
    objdiff reported REGISTER_SWAP on 4/6, the table on 0/6, because the
    `r<N> vs r<N>` indicator needs a " vs " the JSON never contains.

The fixtures under `testdata/` are that real output, captured verbatim from
`objdiff-cli diff -p . <symbol> --verdict --include-instructions -f json`.

Portable lane: pure stdlib, no toolchain, no build, no network.
"""

import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from orchestrator.context_collector import classify_diff_patterns  # noqa: E402

DATA = Path(__file__).resolve().parent / "testdata"


def _fixture(slug):
    preview = (DATA / f"objdiff_preview_{slug}.txt").read_text()
    doc = json.loads((DATA / f"objdiff_analysis_{slug}.json").read_text())
    return preview, doc


class TestClassifyFromObjdiffAnalysis(unittest.TestCase):
    """With the parsed document, the answer is objdiff's own."""

    def test_a_100_percent_function_has_no_patterns(self):
        preview, doc = _fixture("match100")
        self.assertEqual(doc["fuzzy_match_percent"], 100.0)
        c = classify_diff_patterns(preview, objdiff_json=doc)
        self.assertEqual(c["source"], "objdiff")
        self.assertEqual(c["patterns"], [])
        self.assertEqual(c["fixability"], "UNKNOWN")

    def test_a_near_miss_reports_exactly_objdiffs_patterns(self):
        preview, doc = _fixture("nearmiss76")
        truth = [p["pattern"] for p in doc["analysis"]["patterns"]]
        self.assertIn("REGISTER_SWAP", truth)  # the one the old table never saw
        c = classify_diff_patterns(preview, objdiff_json=doc)
        self.assertEqual(c["source"], "objdiff")
        self.assertEqual([p["pattern"] for p in c["patterns"]], truth)

    def test_every_pattern_carries_a_fixability_from_our_vocabulary(self):
        _preview, doc = _fixture("nearmiss76")
        c = classify_diff_patterns("", objdiff_json=doc)
        allowed = {"FIXABLE", "MAYBE_FIXABLE", "NEAR_UNFIXABLE", "UNFIXABLE"}
        for p in c["patterns"]:
            self.assertIn(p["fixability"], allowed, p)


class TestJsonIsNeverPatternMatched(unittest.TestCase):
    """Without a parsed document, JSON text is REFUSED, not guessed at."""

    def test_json_preview_alone_yields_no_patterns(self):
        preview, _doc = _fixture("match100")
        c = classify_diff_patterns(preview)
        self.assertEqual(c["patterns"], [])
        self.assertEqual(c["source"], "none")
        self.assertIn("not classified", c["summary"])

    def test_the_regression_itself_no_STRUCT_OFFSET_on_a_100_percent_symbol(self):
        # The exact failure this file exists for, stated as an assertion.
        preview, doc = _fixture("match100")
        for kwargs in ({}, {"objdiff_json": doc}):
            names = [p["pattern"] for p in
                     classify_diff_patterns(preview, **kwargs)["patterns"]]
            self.assertNotIn("STRUCT_OFFSET", names)
            self.assertNotIn("OFFSET_SWAP", names)

    def test_the_enum_list_that_used_to_trigger_it_is_really_in_the_fixture(self):
        # Guard against a fixture that silently stops exercising the bug.
        preview, _doc = _fixture("match100")
        self.assertIn("OFFSET_SWAP", preview)
        self.assertIn("MISMATCH", preview)
        self.assertIn("BOOL_MASK", preview)
        self.assertNotIn("\n", preview.strip())  # one line -> `.` spans it all


class TestTextHeuristicsStillWork(unittest.TestCase):
    """The regex table is not deleted -- it is confined to TEXT input."""

    def test_a_rendered_listing_still_classifies(self):
        c = classify_diff_patterns(
            "   12  stwu r1, -0x140(r1)\n   13  rlwinm r3,r3,0,0x1\n")
        self.assertEqual(c["source"], "text-heuristics")
        self.assertIn("STACK_FRAME", [p["pattern"] for p in c["patterns"]])

    def test_empty_input_is_still_empty(self):
        c = classify_diff_patterns("")
        self.assertEqual(c["patterns"], [])
        self.assertEqual(c["fixability"], "UNKNOWN")


if __name__ == "__main__":
    unittest.main()
