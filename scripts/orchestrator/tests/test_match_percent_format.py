"""Regression tests: a display must never manufacture a 100% match.

The bug (task #101): ``query_functions`` printed ``Match: 100.0%`` for
``?Terminate@Synth360@@UAAXXZ`` and ``??0?$StandardEffect@VWahEffect@@@@QAA@XZ``.
Both are ``is_stub=1`` rows with unfinished work; the report has them at
99.95385 and 99.98276 fuzzy. Nothing was reading a wrong column -- the values
were right and ``f"{pct:.1f}%"`` rounded them up. The same one-decimal format
was on ``run_objdiff``'s headline, where it rendered
``?Save@ObjectDir@@UAAXAAVBinStream@@@Z`` (99.997 normalized, two real
``diff_arg`` instructions, an offset swap of 0x58/0xa4) as "100.0% normalized".

100 is the one number here that is a CLAIM and not a measurement -- it is what
a COMPLETE verdict is granted on -- so it is the one value a formatter must
never produce from something smaller.

No database and no build are touched.
"""

import unittest

from scripts.orchestrator.mcp_server import format_match_percent


class TestFormatMatchPercent(unittest.TestCase):
    def test_the_two_reported_symbols_no_longer_read_as_matched(self):
        # ?Terminate@Synth360@@UAAXXZ and
        # ??0?$StandardEffect@VWahEffect@@@@QAA@XZ, exactly as report.json
        # carries them.
        self.assertEqual(format_match_percent(99.95385), "99.95%")
        self.assertEqual(format_match_percent(99.98276), "99.98%")

    def test_run_objdiff_headline_values(self):
        # ?Save@ObjectDir@@UAAXAAVBinStream@@@Z, normalized and raw.
        self.assertEqual(format_match_percent(99.997), "99.997%")
        self.assertEqual(format_match_percent(99.7), "99.7%")

    def test_no_value_below_100_ever_renders_as_100(self):
        for pct in (99.9, 99.95, 99.99, 99.995, 99.999, 99.9999, 99.99999,
                    99.999999999):
            with self.subTest(pct=pct):
                rendered = format_match_percent(pct)
                # "<100%" is fine -- it reads as below 100. A leading "100"
                # is not: that is the string a matched function prints.
                self.assertFalse(rendered.startswith("100"),
                                 f"{pct} rendered as {rendered}")

    def test_exactly_100_still_renders_as_100(self):
        self.assertEqual(format_match_percent(100.0), "100.0%")
        self.assertEqual(format_match_percent(100), "100.0%")

    def test_precision_grows_only_as_far_as_needed(self):
        # The common case stays short: no gratuitous decimals on ordinary
        # percentages, or every work-selection listing gets noisier.
        self.assertEqual(format_match_percent(0.0), "0.0%")
        self.assertEqual(format_match_percent(42.5), "42.5%")
        self.assertEqual(format_match_percent(87.25), "87.2%")

    def test_none_is_not_a_percentage(self):
        self.assertEqual(format_match_percent(None), "unimplemented")
        self.assertEqual(format_match_percent(None, ""), "")

    def test_indistinguishably_close_to_100_says_so_rather_than_rounding(self):
        # Beyond four places the honest answer is that it is below 100 but not
        # by a printable amount -- NOT "100.0%".
        self.assertEqual(format_match_percent(99.999999999999), "<100%")


if __name__ == "__main__":
    unittest.main()
