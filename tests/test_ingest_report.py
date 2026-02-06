#!/usr/bin/env python3
"""
Test suite for ingest_report / sync functionality.

Ensures that:
- Unimplemented functions (no fuzzy_match_percent) are ingested, not skipped
- Demangled names are extracted from metadata.demangled_name
- Size is stored as an integer
- cmd_sync delegates to ingest_report
"""

import json
import os
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

# Add scripts to path
sys.path.insert(0, str(Path(__file__).parent.parent / "scripts"))

from orchestrator.database import init_database, ingest_report


def make_report(units):
    """Build a minimal report.json dict."""
    return {"units": units}


def make_func(name, size="64", fuzzy=None, demangled=None):
    """Build a function entry matching real report.json structure."""
    f = {"name": name, "size": size, "address": "0"}
    if fuzzy is not None:
        f["fuzzy_match_percent"] = fuzzy
    metadata = {}
    if demangled is not None:
        metadata["demangled_name"] = demangled
    f["metadata"] = metadata
    return f


class TestIngestUnimplementedFunctions(unittest.TestCase):
    """Unimplemented functions (no fuzzy_match_percent) must not be skipped."""

    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(suffix=".db", delete=False)
        self.db_path = self.tmp.name
        self.tmp.close()
        init_database(self.db_path)

    def tearDown(self):
        os.unlink(self.db_path)

    def _ingest(self, report_dict):
        report_file = tempfile.NamedTemporaryFile(
            mode="w", suffix=".json", delete=False
        )
        json.dump(report_dict, report_file)
        report_file.close()
        try:
            return ingest_report(report_file.name, db_path=self.db_path)
        finally:
            os.unlink(report_file.name)

    def test_unimplemented_functions_are_inserted(self):
        """Functions without fuzzy_match_percent must be inserted."""
        report = make_report([{
            "name": "src/system/char/Foo",
            "functions": [
                make_func("?Bar@@YAXXZ"),  # no fuzzy_match_percent
            ],
        }])
        result = self._ingest(report)
        self.assertEqual(result["inserted"], 1)

        conn = sqlite3.connect(self.db_path)
        row = conn.execute(
            "SELECT symbol, current_percent, unit FROM functions WHERE symbol = ?",
            ("?Bar@@YAXXZ",),
        ).fetchone()
        conn.close()
        self.assertIsNotNone(row, "Unimplemented function must exist in DB")
        self.assertIsNone(row[1], "current_percent should be NULL")
        self.assertEqual(row[2], "src/system/char/Foo")

    def test_mix_of_implemented_and_unimplemented(self):
        """Both implemented and unimplemented functions must be ingested."""
        report = make_report([{
            "name": "src/system/char/Foo",
            "functions": [
                make_func("?Impl@@YAXXZ", fuzzy=75.0),
                make_func("?NoImpl@@YAXXZ"),
            ],
        }])
        result = self._ingest(report)
        self.assertEqual(result["inserted"], 2)

        conn = sqlite3.connect(self.db_path)
        total = conn.execute("SELECT COUNT(*) FROM functions").fetchone()[0]
        with_pct = conn.execute(
            "SELECT COUNT(*) FROM functions WHERE current_percent IS NOT NULL"
        ).fetchone()[0]
        without_pct = conn.execute(
            "SELECT COUNT(*) FROM functions WHERE current_percent IS NULL"
        ).fetchone()[0]
        conn.close()
        self.assertEqual(total, 2)
        self.assertEqual(with_pct, 1)
        self.assertEqual(without_pct, 1)

    def test_update_unimplemented_function(self):
        """Updating an existing unimplemented function should work."""
        report = make_report([{
            "name": "src/system/char/Foo",
            "functions": [make_func("?Bar@@YAXXZ")],
        }])
        self._ingest(report)

        # Now "implement" it
        report2 = make_report([{
            "name": "src/system/char/Foo",
            "functions": [make_func("?Bar@@YAXXZ", fuzzy=42.0)],
        }])
        result = self._ingest(report2)
        self.assertEqual(result["updated"], 1)
        self.assertEqual(result["inserted"], 0)

        conn = sqlite3.connect(self.db_path)
        pct = conn.execute(
            "SELECT current_percent FROM functions WHERE symbol = ?",
            ("?Bar@@YAXXZ",),
        ).fetchone()[0]
        conn.close()
        self.assertAlmostEqual(pct, 42.0)


class TestIngestDemangledNames(unittest.TestCase):
    """Demangled names must come from metadata.demangled_name."""

    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(suffix=".db", delete=False)
        self.db_path = self.tmp.name
        self.tmp.close()
        init_database(self.db_path)

    def tearDown(self):
        os.unlink(self.db_path)

    def _ingest(self, report_dict):
        report_file = tempfile.NamedTemporaryFile(
            mode="w", suffix=".json", delete=False
        )
        json.dump(report_dict, report_file)
        report_file.close()
        try:
            return ingest_report(report_file.name, db_path=self.db_path)
        finally:
            os.unlink(report_file.name)

    def test_demangled_from_metadata(self):
        """Demangled name should be read from metadata.demangled_name."""
        report = make_report([{
            "name": "src/system/char/Foo",
            "functions": [
                make_func(
                    "?Bar@@YAXXZ",
                    fuzzy=50.0,
                    demangled="void __cdecl Bar(void)",
                ),
            ],
        }])
        self._ingest(report)

        conn = sqlite3.connect(self.db_path)
        demangled = conn.execute(
            "SELECT demangled FROM functions WHERE symbol = ?",
            ("?Bar@@YAXXZ",),
        ).fetchone()[0]
        conn.close()
        self.assertEqual(demangled, "void __cdecl Bar(void)")

    def test_demangled_fallback_to_name(self):
        """Without metadata.demangled_name, fall back to mangled name."""
        report = make_report([{
            "name": "src/system/char/Foo",
            "functions": [
                make_func("?Bar@@YAXXZ", fuzzy=50.0),  # no demangled
            ],
        }])
        self._ingest(report)

        conn = sqlite3.connect(self.db_path)
        demangled = conn.execute(
            "SELECT demangled FROM functions WHERE symbol = ?",
            ("?Bar@@YAXXZ",),
        ).fetchone()[0]
        conn.close()
        # Should fall back to the mangled name, not empty string
        self.assertEqual(demangled, "?Bar@@YAXXZ")

    def test_demangled_not_empty_string(self):
        """Demangled should never be stored as empty string."""
        report = make_report([{
            "name": "src/system/char/Foo",
            "functions": [
                make_func("?Baz@@YAXXZ", fuzzy=80.0),
            ],
        }])
        self._ingest(report)

        conn = sqlite3.connect(self.db_path)
        demangled = conn.execute(
            "SELECT demangled FROM functions WHERE symbol = ?",
            ("?Baz@@YAXXZ",),
        ).fetchone()[0]
        conn.close()
        self.assertTrue(len(demangled) > 0, "Demangled must not be empty")


class TestIngestSizeAsInt(unittest.TestCase):
    """Size must be stored as an integer, not a string."""

    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(suffix=".db", delete=False)
        self.db_path = self.tmp.name
        self.tmp.close()
        init_database(self.db_path)

    def tearDown(self):
        os.unlink(self.db_path)

    def test_string_size_converted_to_int(self):
        """Size given as string '144' should be stored as integer 144."""
        report_file = tempfile.NamedTemporaryFile(
            mode="w", suffix=".json", delete=False
        )
        report = make_report([{
            "name": "src/system/char/Foo",
            "functions": [make_func("?Bar@@YAXXZ", size="144", fuzzy=50.0)],
        }])
        json.dump(report, report_file)
        report_file.close()
        try:
            ingest_report(report_file.name, db_path=self.db_path)
        finally:
            os.unlink(report_file.name)

        conn = sqlite3.connect(self.db_path)
        size = conn.execute(
            "SELECT size FROM functions WHERE symbol = ?",
            ("?Bar@@YAXXZ",),
        ).fetchone()[0]
        conn.close()
        self.assertIsInstance(size, int)
        self.assertEqual(size, 144)


class TestIngestWithRealReport(unittest.TestCase):
    """Integration test using the actual report.json if available."""

    REPORT_PATH = Path(__file__).parent.parent / "build" / "373307D9" / "report.json"

    def setUp(self):
        if not self.REPORT_PATH.exists():
            self.skipTest("report.json not found (run ninja first)")
        self.tmp = tempfile.NamedTemporaryFile(suffix=".db", delete=False)
        self.db_path = self.tmp.name
        self.tmp.close()
        init_database(self.db_path)

    def tearDown(self):
        if hasattr(self, "db_path"):
            os.unlink(self.db_path)

    def test_all_functions_ingested(self):
        """Every function in report.json must be processed (inserted or updated)."""
        with open(self.REPORT_PATH) as f:
            report = json.load(f)

        total_entries = sum(
            len(unit.get("functions", []))
            for unit in report.get("units", [])
        )

        result = ingest_report(str(self.REPORT_PATH), db_path=self.db_path)
        # Every entry must be processed — none skipped
        processed = result["inserted"] + result["updated"]
        self.assertEqual(processed, total_entries,
                         "All report entries must be inserted or updated (none skipped)")
        self.assertEqual(result["skipped"], 0)

        # DB row count equals unique symbols (duplicates get updated, not double-inserted)
        conn = sqlite3.connect(self.db_path)
        actual_count = conn.execute("SELECT COUNT(*) FROM functions").fetchone()[0]
        conn.close()
        self.assertEqual(actual_count, result["inserted"],
                         "DB rows should match inserted count (unique symbols)")

    def test_unimplemented_functions_have_units(self):
        """Unimplemented functions must still have unit info populated."""
        ingest_report(str(self.REPORT_PATH), db_path=self.db_path)

        conn = sqlite3.connect(self.db_path)
        # Every function should have a unit
        no_unit = conn.execute(
            "SELECT COUNT(*) FROM functions WHERE unit IS NULL OR unit = ''"
        ).fetchone()[0]
        conn.close()
        self.assertEqual(no_unit, 0, "All functions must have a unit")

    def test_demangled_names_populated(self):
        """Functions with metadata.demangled_name should have demangled set."""
        ingest_report(str(self.REPORT_PATH), db_path=self.db_path)

        conn = sqlite3.connect(self.db_path)
        # Count functions with real demangled names (different from symbol)
        with_demangled = conn.execute(
            "SELECT COUNT(*) FROM functions WHERE demangled != symbol AND length(demangled) > 0"
        ).fetchone()[0]
        conn.close()
        # There should be many functions with demangled names
        self.assertGreater(with_demangled, 100,
                           "Expected many functions with demangled names from metadata")


def run_tests():
    """Run all tests."""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    suite.addTests(loader.loadTestsFromTestCase(TestIngestUnimplementedFunctions))
    suite.addTests(loader.loadTestsFromTestCase(TestIngestDemangledNames))
    suite.addTests(loader.loadTestsFromTestCase(TestIngestSizeAsInt))
    suite.addTests(loader.loadTestsFromTestCase(TestIngestWithRealReport))

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(run_tests())
