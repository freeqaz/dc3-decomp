"""Tests for comparator.py — comparison logic with mock ExecutionResults."""

import struct
import unittest

from .helpers import MockExecutionResult, make_call_log_entry, make_reloc


class TestCompare(unittest.TestCase):
    """Tests for compare()."""

    def setUp(self):
        from scripts.unicorn_runner.comparator import compare
        self.compare = compare

    def _make_pair(self, **overrides):
        """Make identical decomp/orig results, with optional overrides for decomp."""
        base = MockExecutionResult(r3=0, f1=0, call_log=[], error=None)
        decomp = MockExecutionResult(**{**{"r3": 0, "f1": 0, "call_log": [], "error": None}, **overrides})
        return decomp, base

    def test_equivalent_basic(self):
        decomp = MockExecutionResult(r3=100, f1=0)
        orig = MockExecutionResult(r3=100, f1=0)
        result = self.compare(decomp, orig, [], [])
        self.assertEqual(result.verdict, "EQUIVALENT")

    def test_equivalent_with_calls(self):
        log = [make_call_log_entry(0, r3=1, r4=2)]
        decomp = MockExecutionResult(r3=0, call_log=log)
        orig = MockExecutionResult(r3=0, call_log=list(log))
        result = self.compare(decomp, orig, [], [])
        self.assertEqual(result.verdict, "EQUIVALENT")
        self.assertEqual(result.details["call_count"], 1)

    def test_equivalent_f1_in_details(self):
        decomp = MockExecutionResult(r3=0, f1=0xDEADBEEF)
        orig = MockExecutionResult(r3=0, f1=0xDEADBEEF)
        result = self.compare(decomp, orig, [], [])
        self.assertEqual(result.verdict, "EQUIVALENT")
        self.assertEqual(result.details["f1"], 0xDEADBEEF)

    def test_divergent_r3_mismatch(self):
        decomp = MockExecutionResult(r3=42)
        orig = MockExecutionResult(r3=99)
        result = self.compare(decomp, orig, [], [])
        self.assertEqual(result.verdict, "DIVERGENT")
        self.assertEqual(result.details["reason"], "return_value_mismatch")
        self.assertEqual(result.details["decomp_r3"], 42)
        self.assertEqual(result.details["orig_r3"], 99)

    def test_divergent_f1_mismatch(self):
        decomp = MockExecutionResult(r3=0, f1=0x3FF0000000000000)
        orig = MockExecutionResult(r3=0, f1=0x4000000000000000)
        result = self.compare(decomp, orig, [], [])
        self.assertEqual(result.verdict, "DIVERGENT")
        self.assertEqual(result.details["reason"], "fpr_return_mismatch")

    def test_divergent_call_count(self):
        decomp = MockExecutionResult(call_log=[make_call_log_entry(0)])
        orig = MockExecutionResult(call_log=[make_call_log_entry(0), make_call_log_entry(1)])
        result = self.compare(decomp, orig, [], [])
        self.assertEqual(result.verdict, "DIVERGENT")
        self.assertEqual(result.details["reason"], "call_count_mismatch")

    def test_divergent_call_args(self):
        d_log = [make_call_log_entry(0, r3=1, r4=10), make_call_log_entry(1, r3=2, r4=99)]
        o_log = [make_call_log_entry(0, r3=1, r4=10), make_call_log_entry(1, r3=2, r4=50)]
        decomp = MockExecutionResult(call_log=d_log)
        orig = MockExecutionResult(call_log=o_log)
        result = self.compare(decomp, orig, [], [])
        self.assertEqual(result.verdict, "DIVERGENT")
        self.assertEqual(result.details["reason"], "call_arg_mismatch")
        self.assertEqual(result.details["call_index"], 1)
        self.assertEqual(result.details["register"], "r4")

    def test_divergent_memory(self):
        # Write different values at offset 0 in object memory
        decomp_mem = bytearray(0x10000)
        orig_mem = bytearray(0x10000)
        struct.pack_into(">I", decomp_mem, 0, 0xAAAAAAAA)
        struct.pack_into(">I", orig_mem, 0, 0xBBBBBBBB)

        decomp = MockExecutionResult(r3=0, object_memory=bytes(decomp_mem))
        orig = MockExecutionResult(r3=0, object_memory=bytes(orig_mem))
        result = self.compare(decomp, orig, [], [])
        self.assertEqual(result.verdict, "DIVERGENT")
        self.assertEqual(result.details["reason"], "memory_mismatch")
        self.assertTrue(len(result.details["object_diffs"]) > 0)

    def test_decomp_error(self):
        decomp = MockExecutionResult(error="Unexpected fetch from unmapped 0x00000000")
        orig = MockExecutionResult()
        result = self.compare(decomp, orig, [], [])
        self.assertEqual(result.verdict, "DIVERGENT")
        self.assertEqual(result.details["reason"], "decomp_error")

    def test_orig_error(self):
        decomp = MockExecutionResult()
        orig = MockExecutionResult(error="timeout")
        result = self.compare(decomp, orig, [], [])
        self.assertEqual(result.verdict, "DIVERGENT")
        self.assertEqual(result.details["reason"], "orig_error")

    def test_matching_errors_equivalent(self):
        """Both sides hit the same error → EQUIVALENT with matching_error detail."""
        err = "Unexpected fetch from unmapped 0x00000000"
        decomp = MockExecutionResult(error=err)
        orig = MockExecutionResult(error=err)
        result = self.compare(decomp, orig, [], [])
        self.assertEqual(result.verdict, "EQUIVALENT")
        self.assertEqual(result.details["matching_error"], err)
        self.assertTrue(len(result.warnings) > 0)

    def test_mismatched_errors_divergent(self):
        """Both sides error but with different messages → DIVERGENT."""
        decomp = MockExecutionResult(error="Unexpected fetch from unmapped 0x00000000")
        orig = MockExecutionResult(error="Unexpected fetch from unmapped 0xDEADBEEF")
        result = self.compare(decomp, orig, [], [])
        self.assertEqual(result.verdict, "DIVERGENT")
        self.assertEqual(result.details["reason"], "error_mismatch")


class TestFormatResult(unittest.TestCase):
    """Tests for format_result()."""

    def setUp(self):
        from scripts.unicorn_runner.comparator import compare, format_result
        self.compare = compare
        self.format_result = format_result

    def test_format_equivalent(self):
        decomp = MockExecutionResult(r3=42, f1=0)
        orig = MockExecutionResult(r3=42, f1=0)
        result = self.compare(decomp, orig, [], [])
        output = self.format_result(result, decomp, orig, [], [])
        self.assertIn("EQUIVALENT", output)
        self.assertIn("0x0000002A", output)  # r3=42

    def test_format_divergent_fpr(self):
        decomp = MockExecutionResult(r3=0, f1=0x3FF0000000000000)
        orig = MockExecutionResult(r3=0, f1=0x4000000000000000)
        result = self.compare(decomp, orig, [], [])
        output = self.format_result(result, decomp, orig, [], [])
        self.assertIn("DIVERGENT", output)
        self.assertIn("Float return value mismatch", output)
        self.assertIn("3FF0000000000000", output)
        self.assertIn("4000000000000000", output)


if __name__ == "__main__":
    unittest.main()
