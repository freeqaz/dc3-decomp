"""Tests that keep the permuter pattern registry in sync with pattern files."""

from __future__ import annotations

import unittest
from pathlib import Path

from scripts.permuter.patterns.base import get_pattern_metadata, list_patterns


class TestPatternRegistry(unittest.TestCase):
    def test_all_pattern_modules_are_registered(self):
        pattern_dir = Path(__file__).resolve().parents[1] / "patterns"
        on_disk = {
            path.stem
            for path in pattern_dir.glob("*.py")
            if path.stem not in {"__init__", "base"}
        }
        registered = set(list_patterns(include_opt_in=True))
        self.assertEqual(on_disk, registered)

    def test_declared_follow_ups_reference_registered_patterns(self):
        metadata = get_pattern_metadata(include_opt_in=True)
        registered = set(metadata)
        for name, pattern_meta in metadata.items():
            for follow_up in pattern_meta["follow_ups"]:
                self.assertIn(
                    follow_up,
                    registered,
                    f"{name} declares unknown follow-up {follow_up}",
                )


if __name__ == "__main__":
    unittest.main()
