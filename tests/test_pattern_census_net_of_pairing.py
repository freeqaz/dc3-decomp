"""The census summary must print GROSS and NET-OF-PAIRING, per class.

Measured on scan 18 (name_check, objdiff 4.2.8): WRONG_CALLEE is **62 gross, 6
net** -- 56 of its rows sit on a symbol pair objdiff matched by BYTE SIGNATURE
(an unnamed `fn_<addr>` EH funclet), where the differing `bl` is objdiff's guess
rather than a claim about our source.  A summary printing only 62 sends a lane
after 56 rows it cannot adjudicate; a summary printing only 6 hides a population
that still answers "does our tree emit that callee anywhere?" -- 7 real source
defects out of 58, 0 false positives.  So the table prints both.

Each test names the sabotage that must turn it red.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from scripts.analysis.pattern_census import render_pattern_table  # noqa: E402
from scripts.orchestrator.database import (  # noqa: E402
    PAIRING_SENSITIVE_PATTERNS, UNVERIFIABLE_PAIRING,
)

#: Shaped after scan 18 at 1/10 scale: two of the three WRONG_CALLEE functions
#: are funclets, and one REGISTER_SWAP function is too.
BY_PATTERN = {
    "WRONG_CALLEE": ["?Named@@YAXXZ", "fn_82001000", "fn_82002000"],
    "UNVERIFIABLE_PAIRING": ["fn_82001000", "fn_82002000", "?Swapped@@YAXXZ"],
    "REGISTER_SWAP": ["?Swapped@@YAXXZ"],
}


def _row(text: str, pattern: str) -> str:
    for line in text.splitlines():
        if line.startswith(pattern + " ") or line.startswith(pattern + "\t"):
            return line
    raise AssertionError(f"{pattern} not in table:\n{text}")


class TestNetOfPairingColumn(unittest.TestCase):
    def test_callee_class_shows_gross_and_net(self):
        """SABOTAGE: drop the `net` column, or compute it as `n` -> the table
        reads 3/3 and the funclet subtraction is invisible."""
        table = render_pattern_table(BY_PATTERN, examined=1000)
        row = _row(table, "WRONG_CALLEE")
        self.assertIn("3", row.split())          # gross
        self.assertEqual(row.split()[-1], "1")   # net: only ?Named@@YAXXZ

    def test_a_non_callee_class_is_not_given_a_net_number(self):
        """Pairing is not a defined subtraction for REGISTER_SWAP, which is read
        off the instructions -- printing 0 there would be read as a measurement.

        SABOTAGE: apply the subtraction to every pattern -> REGISTER_SWAP reads
        1 gross / 0 net, inventing an exhausted class out of nothing.
        """
        row = _row(render_pattern_table(BY_PATTERN, 1000), "REGISTER_SWAP")
        self.assertEqual(row.split()[-1], "n/a")

    def test_the_two_columns_differ_on_this_fixture(self):
        """NEGATIVE CONTROL, in the test: if gross == net here, every assertion
        above is also true of a table that never subtracts anything."""
        table = render_pattern_table(BY_PATTERN, 1000)
        gross, net = _row(table, "WRONG_CALLEE").split()[1], \
            _row(table, "WRONG_CALLEE").split()[-1]
        self.assertNotEqual(gross, net,
                            "control failed: the fixture must have funclet rows")

    def test_the_legend_states_what_was_subtracted(self):
        """A bare number in a new column is a number nobody can act on.
        SABOTAGE: delete the legend paragraph."""
        table = render_pattern_table(BY_PATTERN, 1000)
        self.assertIn(UNVERIFIABLE_PAIRING, table)
        self.assertIn("byte signature", table)
        self.assertIn("include_unverifiable", table)

    def test_a_ruler_where_the_detector_never_fired_says_so(self):
        """net == gross is ambiguous between 'no funclet rows' and 'this binary
        cannot see funclet rows'. The table must name the second possibility.

        SABOTAGE: delete the `elif` branch -> a 4.2.6 census silently reports
        its gross numbers as net ones.
        """
        table = render_pattern_table(
            {"WRONG_CALLEE": ["?Named@@YAXXZ"]}, examined=1000)
        self.assertIn("fired on ZERO functions", table)
        self.assertIn("4.2.7", table)

    def test_the_class_list_is_shared_with_the_query_surface(self):
        """One definition, or the census and `query_functions` come to disagree
        about the same scan. SABOTAGE: re-declare the set locally in
        pattern_census.py."""
        import scripts.analysis.pattern_census as pc
        self.assertIs(pc.PAIRING_SENSITIVE_PATTERNS, PAIRING_SENSITIVE_PATTERNS)
        self.assertIn("WRONG_CALLEE", PAIRING_SENSITIVE_PATTERNS)


if __name__ == "__main__":
    unittest.main()
