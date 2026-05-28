"""Regression test: ingest_report must never wipe a recorded score.

The "CharEyes sweep" bug — ``batch_auto --db-sync`` (default-on) calls
``scripts.orchestrator.database.ingest_report`` to refresh decomp.db from
report.json before selecting candidates. report.json omits both
``fuzzy_match_percent`` and ``match_percent`` for any function it cannot
measure (e.g. one living in a NonMatching unit that dtk can't score —
7,000+ such functions exist). The old code fed that missing percent
(``None``) straight into ``current_percent`` / ``best_percent``, NULL-ing
the recorded score and silently destroying progress.

These tests pin the contract:

* a missing measurement must leave ``current_percent`` / ``best_percent`` /
  ``verdict`` untouched (only metadata may refresh), and
* ``best_percent`` stays a monotonic max — a lower (but real) new
  measurement can update ``current_percent`` but must not pull the
  recorded best down.

Everything runs against a throwaway temp sqlite DB; the live decomp.db is
never touched.
"""

from __future__ import annotations

import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

# Ensure project root is importable when run standalone.
_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.orchestrator.database import ingest_report, init_database


def _write_report(tmp: Path, units: list[dict]) -> Path:
    report = tmp / "report.json"
    report.write_text(json.dumps({"units": units}))
    return report


def _seed_function(
    db_path: Path,
    *,
    symbol: str,
    unit: str,
    current_percent,
    best_percent,
    verdict,
) -> None:
    conn = init_database(db_path)
    conn.execute(
        """
        INSERT INTO functions (symbol, demangled, unit, size,
                               current_percent, best_percent, verdict)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        """,
        (symbol, symbol, unit, 100, current_percent, best_percent, verdict),
    )
    conn.commit()
    conn.close()


def _read_function(db_path: Path, symbol: str) -> sqlite3.Row:
    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row
    row = conn.execute(
        "SELECT current_percent, best_percent, verdict, unit, size "
        "FROM functions WHERE symbol = ?",
        (symbol,),
    ).fetchone()
    conn.close()
    return row


class IngestReportScorePreservationTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmpdir = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmpdir.name)
        self.db_path = self.tmp / "decomp_test.db"

    def tearDown(self) -> None:
        self._tmpdir.cleanup()

    def test_missing_measurement_preserves_score(self) -> None:
        """A function absent of any percent must keep its stored score."""
        _seed_function(
            self.db_path,
            symbol="NextLook__8CharEyesFv",
            unit="main/system/char/CharEyes",
            current_percent=93.3,
            best_percent=93.3,
            verdict=None,
        )

        # report.json has the function but with NO measurable percent
        # (NonMatching unit — dtk omits both percent fields).
        report = _write_report(
            self.tmp,
            [
                {
                    "name": "main/system/char/CharEyes",
                    "functions": [
                        {
                            "name": "NextLook__8CharEyesFv",
                            "size": 100,
                            # neither fuzzy_match_percent nor match_percent
                            "metadata": {"demangled_name": "CharEyes::NextLook()"},
                        }
                    ],
                }
            ],
        )

        result = ingest_report(report, self.db_path, update_existing=True)

        row = _read_function(self.db_path, "NextLook__8CharEyesFv")
        # Score columns must be untouched — NOT zeroed/nulled.
        self.assertEqual(row["current_percent"], 93.3)
        self.assertEqual(row["best_percent"], 93.3)
        # Metadata may still refresh (demangled name was present).
        self.assertEqual(row["unit"], "main/system/char/CharEyes")
        # It must be counted as skipped, not updated.
        self.assertEqual(result["updated"], 0)
        self.assertEqual(result["skipped"], 1)

    def test_missing_measurement_preserves_complete_verdict(self) -> None:
        """A COMPLETE 100% row must not be reduced to COMPLETE/NULL."""
        _seed_function(
            self.db_path,
            symbol="OnShutdown__4WPADFv",
            unit="main/sdk/RVL_SDK/src/wpad/WPAD",
            current_percent=100.0,
            best_percent=100.0,
            verdict="COMPLETE",
        )

        report = _write_report(
            self.tmp,
            [
                {
                    "name": "main/sdk/RVL_SDK/src/wpad/WPAD",
                    "functions": [
                        {"name": "OnShutdown__4WPADFv", "size": 100}
                    ],
                }
            ],
        )

        ingest_report(report, self.db_path, update_existing=True)

        row = _read_function(self.db_path, "OnShutdown__4WPADFv")
        self.assertEqual(row["current_percent"], 100.0)
        self.assertEqual(row["best_percent"], 100.0)
        self.assertEqual(row["verdict"], "COMPLETE")

    def test_valid_measurement_still_updates(self) -> None:
        """A real measurement must still flow through (no over-guarding)."""
        _seed_function(
            self.db_path,
            symbol="Foo__3BarFv",
            unit="main/system/char/Bar",
            current_percent=80.0,
            best_percent=80.0,
            verdict=None,
        )

        report = _write_report(
            self.tmp,
            [
                {
                    "name": "main/system/char/Bar",
                    "functions": [
                        {
                            "name": "Foo__3BarFv",
                            "size": 100,
                            "fuzzy_match_percent": 92.5,
                        }
                    ],
                }
            ],
        )

        result = ingest_report(report, self.db_path, update_existing=True)

        row = _read_function(self.db_path, "Foo__3BarFv")
        self.assertEqual(row["current_percent"], 92.5)
        self.assertEqual(row["best_percent"], 92.5)
        self.assertEqual(result["updated"], 1)
        self.assertEqual(result["skipped"], 0)

    def test_best_percent_is_monotonic_max(self) -> None:
        """A real but lower measurement updates current_percent but not best."""
        _seed_function(
            self.db_path,
            symbol="Baz__3BarFv",
            unit="main/system/char/Bar",
            current_percent=95.0,
            best_percent=95.0,
            verdict=None,
        )

        report = _write_report(
            self.tmp,
            [
                {
                    "name": "main/system/char/Bar",
                    "functions": [
                        {
                            "name": "Baz__3BarFv",
                            "size": 100,
                            "fuzzy_match_percent": 70.0,  # regression
                        }
                    ],
                }
            ],
        )

        ingest_report(report, self.db_path, update_existing=True)

        row = _read_function(self.db_path, "Baz__3BarFv")
        # current_percent reflects the (real, measured) regression...
        self.assertEqual(row["current_percent"], 70.0)
        # ...but best_percent never drops below the prior best.
        self.assertEqual(row["best_percent"], 95.0)


if __name__ == "__main__":
    unittest.main()
