"""Compatibility tests for patterns against sparse Diagnosis objects."""

from __future__ import annotations

import unittest

import scripts.permuter.patterns  # noqa: F401
from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.tests.conftest import _empty_diag


class TestPatternDiagnosisCompat(unittest.TestCase):
    def test_bool_materialize_handles_missing_optional_diagnosis_fields(self):
        diagnosis = _empty_diag()
        pattern = get_pattern("bool_materialize")

        self.assertFalse(pattern.relevant(diagnosis))
        self.assertEqual(pattern.priority(diagnosis), 0.0)

    def test_float_literal_pressure_handles_missing_optional_diagnosis_fields(self):
        diagnosis = _empty_diag()
        pattern = get_pattern("float_literal_pressure")

        self.assertFalse(pattern.relevant(diagnosis))
        self.assertEqual(pattern.priority(diagnosis), 0.0)


if __name__ == "__main__":
    unittest.main()
