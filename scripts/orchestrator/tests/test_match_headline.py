"""Regression tests: the match headline may not contradict its own body.

The bug, observed live on 2026-08-22 against
``?CalcShaderOpts@RndShaderDepthVolume@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z``
(unit ``default/system/rndobj/Shader``, ``diff_mode="name_check"``)::

    **Match: 0.0% normalized (0.0% raw)**  <- authoritative; headline rewrite
                                             did not match the markdown below
    # protected: virtual ... CalcShaderOpts(...) -- Match: 3.4% canonical (0.0% raw)

Two numbers, and the STALE one claimed authority. Two independent defects:

1. MISNOMER (the fifth of this family in this project). The banner read
   ``fuzzy_match_percent`` and printed the word "normalized". objdiff-cli's own
   ``DiffOutput`` documents ``normalized_match_percent`` as "MISNOMER, kept for
   compatibility ... the FUZZY score measured under a relaxed RELOCATION mode";
   4.2.4 added ``canonical_match_percent`` to carry objdiff-core's
   ``match_percent_normalized`` -- report.json's ruler. Live values for that
   symbol: canonical 3.4375, fuzzy 0.0.

2. UNEARNED AUTHORITY. The rewrite was ``re.sub`` on
   ``Match: X% normalized (Y% raw)``, a string objdiff-cli stopped emitting
   when it renamed the label to "canonical". The failure path then declared the
   number it HAD computed authoritative and the number it had NOT computed
   unverified -- exactly backwards.

Hermetic: no objdiff-cli, no build, no database.
"""
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from orchestrator.mcp_server import (  # noqa: E402
    apply_match_headline,
    match_headline,
    _MD_MATCH_LINE,
    _MD_MATCH_LOOSE,
)

#: The live JSON for the symbol above, fields verbatim from
#: `objdiff-cli diff -p . -u default/system/rndobj/Shader -f json ...`.
LIVE = {
    "symbol": "?CalcShaderOpts@RndShaderDepthVolume@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z",
    "unit": "default/system/rndobj/Shader",
    "fuzzy_match_percent": 0.0,
    "normalized_match_percent": 0.0,
    "canonical_match_percent": 3.4375,
    "raw_match_percent": 0.0,
}

#: objdiff-cli 4.2.8 markdown, both shapes, for that same symbol.
LIVE_CONCISE = (
    "# protected: virtual unsigned __int64 __cdecl "
    "RndShaderDepthVolume::CalcShaderOpts(class NgMat *, enum ShaderType, bool)"
    " -- Match: 3.4% canonical (0.0% raw)\n"
    "\n- **Unit**: `default/system/rndobj/Shader`\n"
)
LIVE_FULL = (
    "# Diff: RndShaderDepthVolume::CalcShaderOpts\n\n"
    "- **Symbol**: `?CalcShaderOpts@...`\n"
    "- **Unit**: `default/system/rndobj/Shader`\n"
    "- **Match**: 3.4% canonical (0.0% raw)\n"
    "  - fuzzy (relocation-sensitive): 0.00000%\n"
    "- **Target Size**: 128 bytes\n"
)

DISAGREE = "disagrees and neither side is assumed right"


class TestMisnomer(unittest.TestCase):
    def test_the_live_regression_leads_with_canonical_not_fuzzy(self):
        head = match_headline(LIVE)
        self.assertEqual(head["field"], "canonical_match_percent")
        self.assertEqual(head["pct"], 3.4375)
        self.assertIn("3.4%", head["text"])
        # The precise thing that was wrong: the fuzzy number is not the
        # headline. 0.0 must not be the leading value.
        self.assertNotRegex(head["text"], r"Match:\s*0\.0%")

    def test_the_word_normalized_never_labels_a_headline(self):
        # In objdiff's JSON vocabulary "normalized" names a FUZZY score. Using
        # it as a label is what made four earlier surfaces lie, so the word is
        # banned from output regardless of which field we ended up reading.
        for data in (LIVE,
                     {"fuzzy_match_percent": 50.0, "raw_match_percent": 40.0},
                     {"canonical_match_percent": 100.0, "raw_match_percent": 99.0}):
            with self.subTest(data=sorted(data)):
                self.assertNotIn("normalized", match_headline(data)["text"])

    def test_fuzzy_fallback_says_it_is_fuzzy_and_flags_the_old_binary(self):
        # objdiff-cli < 4.2.4 has no canonical field. Falling back is fine;
        # promoting the fallback under a canonical-sounding word is not.
        head = match_headline({"fuzzy_match_percent": 90.5,
                               "normalized_match_percent": 90.5,
                               "raw_match_percent": 88.0})
        self.assertEqual(head["field"], "fuzzy_match_percent")
        self.assertEqual(head["label"], "fuzzy")
        self.assertIn("fuzzy", head["text"])
        self.assertIn("4.2.4", head["caveat"])

    def test_self_diff_gets_no_number_at_all(self):
        # objdiff >= 4.2.8 withholds every percent on a self-diff because it is
        # 100% by construction. Manufacturing one here would undo that.
        head = match_headline({"self_diff": {"reason": "same path",
                                             "scores_withheld": True},
                               "fuzzy_match_percent": 100.0})
        self.assertIsNone(head["pct"])
        self.assertIn("WITHHELD", head["text"])

    def test_no_percent_at_all_is_stated_not_invented(self):
        head = match_headline({"symbol": "x"})
        self.assertIsNone(head["pct"])
        self.assertIn("unavailable", head["text"])


class TestRewrite(unittest.TestCase):
    def test_both_markdown_shapes_are_rewritten_and_no_banner_appears(self):
        head = match_headline(LIVE)
        for name, md in (("concise", LIVE_CONCISE), ("full", LIVE_FULL)):
            with self.subTest(shape=name):
                out = apply_match_headline(md, head)
                self.assertNotIn(DISAGREE, out)
                self.assertNotIn("authoritative", out)
                # Exactly one number survives, and it is ours.
                pcts = {m.group("pct") for m in _MD_MATCH_LOOSE.finditer(out)}
                self.assertEqual(pcts, {"3.4"})

    def test_full_mode_keeps_its_bold_markers(self):
        # A substring-equality survivor scan (the first draft) reported every
        # full-mode diff as a phantom disagreement purely because the line reads
        # `Match**: ...`. Content, not text.
        out = apply_match_headline(LIVE_FULL, match_headline(LIVE))
        self.assertIn("- **Match**: 3.4% canonical", out)
        self.assertNotIn(DISAGREE, out)

    def test_pre_4_2_4_normalized_spelling_is_still_rewritten(self):
        old_md = "# Foo -- Match: 100.0% normalized (98.0% raw)\n"
        head = match_headline({"canonical_match_percent": 99.99783,
                               "fuzzy_match_percent": 99.99783,
                               "raw_match_percent": 98.0})
        out = apply_match_headline(old_md, head)
        self.assertIn("99.998%", out)
        self.assertNotIn("100.0%", out)

    def test_rounding_is_still_corrected(self):
        # The one job the old rewrite did right: objdiff prints
        # `100.0% canonical` for UIList::Handle at 99.99783.
        md = "# UIList::Handle -- Match: 100.0% canonical (98.7% raw)\n"
        head = match_headline({"canonical_match_percent": 99.99783,
                               "fuzzy_match_percent": 99.99783,
                               "raw_match_percent": 98.68831})
        out = apply_match_headline(md, head)
        self.assertIn("99.998% canonical", out)
        self.assertNotIn("Match: 100.0%", out)

    def test_every_occurrence_is_rewritten_not_just_the_first(self):
        # The old code passed count=1 and would leave a second headline behind.
        md = ("# Foo -- Match: 100.0% canonical (98.0% raw)\n"
              "- **Match**: 100.0% canonical (98.0% raw)\n")
        head = match_headline({"canonical_match_percent": 99.995,
                               "fuzzy_match_percent": 99.995,
                               "raw_match_percent": 98.0})
        out = apply_match_headline(md, head)
        self.assertEqual(out.count("100.0%"), 0)
        self.assertEqual(len(_MD_MATCH_LINE.findall(out)), 2)


class TestTheCheckCanActuallyFail(unittest.TestCase):
    """The survivor scan must be capable of reporting a disagreement.

    A check that cannot fail is worse than no check. The first draft ran the
    survivor scan with the SAME regex it used to rewrite, which makes it
    vacuous by construction -- anything visible to the scan had already been
    rewritten. `_MD_MATCH_LOOSE` is deliberately broader, and these tests are
    the proof that the broadening did something.
    """

    UNKNOWN_LABEL = "# Foo -- Match: 77.7% graded (60.0% raw)\n"

    def test_a_label_the_rewriter_cannot_see_is_reported_not_swallowed(self):
        # Exactly the 4.2.4 failure mode replayed with a future label: the
        # strict regex does not match, so nothing is rewritten, and the body
        # keeps a number that is not ours.
        self.assertIsNone(_MD_MATCH_LINE.search(self.UNKNOWN_LABEL))
        out = apply_match_headline(self.UNKNOWN_LABEL, match_headline(LIVE))
        self.assertIn(DISAGREE, out)
        self.assertIn("77.7", out)          # the survivor is quoted
        self.assertIn("3.4%", out)          # ...next to ours

    def test_the_banner_never_calls_itself_authoritative(self):
        out = apply_match_headline(self.UNKNOWN_LABEL, match_headline(LIVE))
        self.assertNotIn("authoritative", out.lower())
        # It names the field its own number came from -- that is what makes it
        # checkable rather than an assertion.
        self.assertIn("canonical_match_percent", out)

    def test_a_body_with_no_match_line_gets_a_banner_that_says_so(self):
        out = apply_match_headline("# Foo\n\n- **Unit**: `x`\n",
                                   match_headline(LIVE))
        self.assertIn("no `Match:` line to rewrite", out)
        self.assertNotIn(DISAGREE, out)
        self.assertIn("canonical_match_percent", out)

    def test_positive_control_the_scan_is_silent_on_a_correct_body(self):
        # The other half of a usable check: it must NOT fire on good input, or
        # its firing carries no information.
        self.assertNotIn(DISAGREE,
                         apply_match_headline(LIVE_CONCISE, match_headline(LIVE)))
        self.assertNotIn(DISAGREE,
                         apply_match_headline(LIVE_FULL, match_headline(LIVE)))

    def test_loose_regex_is_strictly_broader_than_the_rewriter(self):
        # If these ever become the same pattern the survivor scan silently
        # returns to being vacuous, which is invisible in every other test.
        broader = self.UNKNOWN_LABEL
        self.assertIsNone(_MD_MATCH_LINE.search(broader))
        self.assertIsNotNone(_MD_MATCH_LOOSE.search(broader))

    def test_loose_regex_does_not_fire_on_objdiffs_instruction_table_header(self):
        # `| Index | Target | Base | Match |` appears in --full-listing output
        # and carries no percentage. A false positive here would print a
        # disagreement banner on every full listing.
        table = "| Index | Target | Base | Match |\n|---|---|---|---|\n"
        self.assertIsNone(_MD_MATCH_LOOSE.search(table))
        self.assertNotIn(DISAGREE,
                         apply_match_headline(LIVE_FULL + table,
                                              match_headline(LIVE)))


if __name__ == "__main__":
    unittest.main()
