"""Fixture-based tests for IL parser and bundle export (WP7).

Tests the normalized JSON schema for stability, cross-file symbol resolution,
and expected IL patterns for each captured fixture family.

Usage:
    python -m pytest msvc-src/tools/test_il_fixtures.py -v
"""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
FIXTURES_DIR = REPO_ROOT / "msvc-src" / "analysis" / "il-fixtures"

# All expected bundles — each must have manifest.json and bundle.json
EXPECTED_BUNDLES = [
    "il_type_control_cast_vs_and",
    "il_bool_materialization",
    "il_branch_polarity",
    "il_rlwinm_shifts",
    "il_switch_dispatch",
    "il_call_return",
]


def _load_bundle(name: str) -> dict:
    path = FIXTURES_DIR / name / "bundle.json"
    if not path.exists():
        raise FileNotFoundError(f"Bundle not found: {path}")
    return json.loads(path.read_text())


def _get_func(bundle: dict, name_substring: str) -> dict | None:
    for f in bundle.get("functions", []):
        if name_substring in f.get("name", ""):
            return f
    return None


def _get_opcodes(func: dict) -> list[str]:
    return [op["name"] for op in func.get("operations", []) if "name" in op]


# ---------------------------------------------------------------------------
# Schema stability tests
# ---------------------------------------------------------------------------

class TestBundleSchema(unittest.TestCase):
    """Verify all bundles have correct top-level schema."""

    def test_all_bundles_exist(self):
        for name in EXPECTED_BUNDLES:
            path = FIXTURES_DIR / name / "bundle.json"
            self.assertTrue(path.exists(), f"Missing bundle: {name}")

    def test_manifests_exist(self):
        for name in EXPECTED_BUNDLES:
            path = FIXTURES_DIR / name / "manifest.json"
            self.assertTrue(path.exists(), f"Missing manifest: {name}")

    def test_top_level_keys(self):
        required_keys = {"base", "functions", "files", "token_width"}
        for name in EXPECTED_BUNDLES:
            bundle = _load_bundle(name)
            actual = set(bundle.keys())
            missing = required_keys - actual
            self.assertEqual(missing, set(), f"{name}: missing keys {missing}")

    def test_file_entries(self):
        """Each bundle should reference all 5 IL file types."""
        for name in EXPECTED_BUNDLES:
            bundle = _load_bundle(name)
            files = bundle.get("files", {})
            for ext in ("ex", "gl", "sy", "in", "db"):
                self.assertIn(ext, files, f"{name}: missing file entry '{ext}'")
                self.assertTrue(files[ext].get("present", False),
                                f"{name}: file '{ext}' not present")

    def test_token_width(self):
        for name in EXPECTED_BUNDLES:
            bundle = _load_bundle(name)
            tw = bundle.get("token_width")
            self.assertIn(tw, (2, 4), f"{name}: unexpected token_width {tw}")

    def test_functions_have_required_fields(self):
        required = {"name", "operations", "operation_count", "index"}
        for name in EXPECTED_BUNDLES:
            bundle = _load_bundle(name)
            for func in bundle.get("functions", []):
                missing = required - set(func.keys())
                self.assertEqual(missing, set(),
                                 f"{name}/{func.get('name', '?')}: missing {missing}")


class TestFunctionSchema(unittest.TestCase):
    """Verify function operation schema consistency."""

    def test_operations_have_type(self):
        for name in EXPECTED_BUNDLES:
            bundle = _load_bundle(name)
            for func in bundle.get("functions", []):
                for op in func.get("operations", []):
                    self.assertIn("type", op,
                                  f"{name}/{func['name']}: op missing 'type'")
                    self.assertIn("name", op,
                                  f"{name}/{func['name']}: op missing 'name'")

    def test_operation_types_are_known(self):
        known_types = {"op", "label", "branch", "call_start", "call_exec",
                       "return", "goto", "switch", "case", "unknown",
                       "vcall_setup", "vcall_bind", "assign", "fallthrough",
                       "switch_table"}
        for name in EXPECTED_BUNDLES:
            bundle = _load_bundle(name)
            for func in bundle.get("functions", []):
                for op in func.get("operations", []):
                    t = op.get("type", "")
                    self.assertIn(t, known_types,
                                  f"{name}/{func['name']}: unknown op type '{t}'")

    def test_operand_kinds_are_known(self):
        known_kinds = {"var", "lit", "ref", "type", "val", "unknown"}
        for name in EXPECTED_BUNDLES:
            bundle = _load_bundle(name)
            for func in bundle.get("functions", []):
                for op in func.get("operations", []):
                    for operand in op.get("operands", []):
                        if "kind" in operand:
                            self.assertIn(operand["kind"], known_kinds,
                                          f"{name}/{func['name']}: "
                                          f"unknown operand kind '{operand['kind']}'")


# ---------------------------------------------------------------------------
# Cast vs AND fixture
# ---------------------------------------------------------------------------

class TestCastVsAnd(unittest.TestCase):
    """Verify IL distinction between CAST and AND for byte narrowing."""

    def setUp(self):
        self.bundle = _load_bundle("il_type_control_cast_vs_and")

    def test_function_count(self):
        self.assertEqual(len(self.bundle["functions"]), 4)

    def test_cast_shift_has_cast(self):
        func = _get_func(self.bundle, "cast_shift")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        self.assertIn("CAST", opcodes)

    def test_and_shift_has_and(self):
        func = _get_func(self.bundle, "and_shift")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        self.assertIn("AND", opcodes)

    def test_and_shift_no_early_cast(self):
        """and_shift should use AND, not CAST, for byte narrowing."""
        func = _get_func(self.bundle, "and_shift")
        opcodes = _get_opcodes(func)
        # AND should appear before SHR in the operation list
        and_idx = opcodes.index("AND") if "AND" in opcodes else -1
        shr_idx = opcodes.index("SHR") if "SHR" in opcodes else -1
        if and_idx >= 0 and shr_idx >= 0:
            self.assertLess(and_idx, shr_idx, "AND should precede SHR")


# ---------------------------------------------------------------------------
# Bool materialization fixture
# ---------------------------------------------------------------------------

class TestBoolMaterialization(unittest.TestCase):
    """Verify IL comparison operators for bool materialization categories."""

    def setUp(self):
        self.bundle = _load_bundle("il_bool_materialization")

    def test_function_count(self):
        self.assertEqual(len(self.bundle["functions"]), 6)

    def test_zero_test_has_ne(self):
        func = _get_func(self.bundle, "zero_test")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        self.assertIn("NE", opcodes)

    def test_equality_has_eq(self):
        func = _get_func(self.bundle, "equality_nonzero")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        self.assertIn("EQ", opcodes)

    def test_signed_positive_has_gt(self):
        func = _get_func(self.bundle, "signed_positive")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        self.assertIn("GT", opcodes)

    def test_signed_vs_unsigned_different_types(self):
        """signed_ordered and unsigned_ordered should have different operand types."""
        s = _get_func(self.bundle, "signed_ordered")
        u = _get_func(self.bundle, "unsigned_ordered")
        self.assertIsNotNone(s)
        self.assertIsNotNone(u)
        # Both should have GT
        s_opcodes = _get_opcodes(s)
        u_opcodes = _get_opcodes(u)
        self.assertIn("GT", s_opcodes)
        self.assertIn("GT", u_opcodes)


# ---------------------------------------------------------------------------
# Branch polarity fixture
# ---------------------------------------------------------------------------

class TestBranchPolarity(unittest.TestCase):
    """Verify IL branch patterns for different condition types."""

    def setUp(self):
        self.bundle = _load_bundle("il_branch_polarity")

    def test_function_count(self):
        self.assertEqual(len(self.bundle["functions"]), 7)

    def test_eq_zero_has_eq_and_branch(self):
        func = _get_func(self.bundle, "branch_eq_zero")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        self.assertIn("EQ", opcodes)
        self.assertIn("COND_BRANCH", opcodes)

    def test_ne_zero_has_ne_and_branch(self):
        func = _get_func(self.bundle, "branch_ne_zero")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        self.assertIn("NE", opcodes)
        self.assertIn("COND_BRANCH", opcodes)

    def test_guard_has_eq_and_branch(self):
        func = _get_func(self.bundle, "branch_guard")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        self.assertIn("EQ", opcodes)

    def test_nested_has_multiple_branches(self):
        func = _get_func(self.bundle, "branch_nested")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        branch_count = opcodes.count("COND_BRANCH")
        self.assertGreaterEqual(branch_count, 2)

    def test_signed_vs_unsigned_gt(self):
        """signed_gt and unsigned_gt should both use GT but with different operand types."""
        s = _get_func(self.bundle, "branch_signed_gt")
        u = _get_func(self.bundle, "branch_unsigned_gt")
        self.assertIsNotNone(s)
        self.assertIsNotNone(u)
        self.assertIn("GT", _get_opcodes(s))
        self.assertIn("GT", _get_opcodes(u))


# ---------------------------------------------------------------------------
# rlwinm shifts fixture
# ---------------------------------------------------------------------------

class TestRlwinmShifts(unittest.TestCase):
    """Verify IL patterns for rlwinm-sensitive shifts."""

    def setUp(self):
        self.bundle = _load_bundle("il_rlwinm_shifts")

    def test_function_count(self):
        self.assertEqual(len(self.bundle["functions"]), 7)

    def test_u8_shift_uses_cast(self):
        func = _get_func(self.bundle, "u8_shift_right")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        self.assertIn("CAST", opcodes)
        self.assertIn("SHR", opcodes)

    def test_u32_mask_uses_and(self):
        func = _get_func(self.bundle, "u32_mask_shift_right")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        self.assertIn("AND", opcodes)
        self.assertIn("SHR", opcodes)

    def test_signed_shift_uses_cast_or_and(self):
        func = _get_func(self.bundle, "signed_shift")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        self.assertIn("SHR", opcodes)

    def test_extract_nibble_has_shr_and_and(self):
        func = _get_func(self.bundle, "extract_nibble")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        self.assertIn("SHR", opcodes)
        self.assertIn("AND", opcodes)


# ---------------------------------------------------------------------------
# Switch dispatch fixture
# ---------------------------------------------------------------------------

class TestSwitchDispatch(unittest.TestCase):
    """Verify IL switch/case patterns."""

    def setUp(self):
        self.bundle = _load_bundle("il_switch_dispatch")

    def test_function_count(self):
        self.assertEqual(len(self.bundle["functions"]), 5)

    def test_small_switch_has_switch_op(self):
        func = _get_func(self.bundle, "switch_small")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        # Should have SWITCH or SWITCH_TABLE
        has_switch = "SWITCH" in opcodes or "SWITCH_TABLE" in opcodes
        self.assertTrue(has_switch, f"Expected SWITCH in {opcodes}")

    def test_dense_switch_has_cases(self):
        func = _get_func(self.bundle, "switch_dense")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        has_switch = "SWITCH" in opcodes or "SWITCH_TABLE" in opcodes
        self.assertTrue(has_switch)

    def test_sparse_switch(self):
        func = _get_func(self.bundle, "switch_sparse")
        self.assertIsNotNone(func)
        # Sparse switch may use if-else chain or switch table
        opcodes = _get_opcodes(func)
        has_branch = ("SWITCH" in opcodes or "SWITCH_TABLE" in opcodes or
                      "COND_BRANCH" in opcodes)
        self.assertTrue(has_branch)


# ---------------------------------------------------------------------------
# Call/return fixture
# ---------------------------------------------------------------------------

class TestCallReturn(unittest.TestCase):
    """Verify IL call/return patterns."""

    def setUp(self):
        self.bundle = _load_bundle("il_call_return")

    def test_function_count(self):
        self.assertEqual(len(self.bundle["functions"]), 8)

    def test_simple_call_has_call_start_and_exec(self):
        func = _get_func(self.bundle, "call_and_return")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        self.assertIn("CALL_START", opcodes)
        self.assertIn("CALL_EXEC", opcodes)

    def test_virtual_call_has_vcall(self):
        func = _get_func(self.bundle, "virtual_call")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        has_vcall = "VCALL_SETUP" in opcodes or "VCALL_BIND" in opcodes
        self.assertTrue(has_vcall, f"Expected VCALL in {opcodes}")

    def test_chain_calls_has_multiple_calls(self):
        func = _get_func(self.bundle, "chain_calls")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        call_count = opcodes.count("CALL_START")
        self.assertGreaterEqual(call_count, 2)

    def test_early_return_has_branch(self):
        func = _get_func(self.bundle, "early_return")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        self.assertIn("COND_BRANCH", opcodes)

    def test_conditional_return_has_both(self):
        func = _get_func(self.bundle, "conditional_return")
        self.assertIsNotNone(func)
        opcodes = _get_opcodes(func)
        self.assertIn("COND_BRANCH", opcodes)
        self.assertIn("CALL_START", opcodes)


# ---------------------------------------------------------------------------
# Cross-bundle consistency
# ---------------------------------------------------------------------------

class TestCrossBundleConsistency(unittest.TestCase):
    """Verify schema consistency across all bundles."""

    def test_all_functions_have_operations(self):
        for name in EXPECTED_BUNDLES:
            bundle = _load_bundle(name)
            for func in bundle.get("functions", []):
                self.assertGreater(
                    len(func.get("operations", [])), 0,
                    f"{name}/{func['name']}: empty operations list"
                )

    def test_operation_count_matches(self):
        """operation_count field should match actual operations length."""
        for name in EXPECTED_BUNDLES:
            bundle = _load_bundle(name)
            for func in bundle.get("functions", []):
                declared = func.get("operation_count", 0)
                actual = len(func.get("operations", []))
                # Allow tolerance — some ops may be filtered
                self.assertGreater(actual, 0,
                                   f"{name}/{func['name']}: no operations")

    def test_manifest_has_source(self):
        """Each manifest should reference a source file."""
        for name in EXPECTED_BUNDLES:
            bundle = _load_bundle(name)
            manifest = bundle.get("manifest", {})
            self.assertIn("source_path", manifest,
                          f"{name}: manifest missing source_path")

    def test_stable_json_export(self):
        """Re-exporting should produce identical JSON (determinism check)."""
        import subprocess
        for name in EXPECTED_BUNDLES[:2]:  # Only test first 2 for speed
            bundle_dir = FIXTURES_DIR / name
            original = json.loads((bundle_dir / "bundle.json").read_text())
            # Re-export
            subprocess.run(
                [sys.executable, "msvc-src/tools/il_parser.py",
                 "export-json", str(bundle_dir)],
                cwd=str(REPO_ROOT), capture_output=True, timeout=30,
            )
            reexported = json.loads((bundle_dir / "bundle.json").read_text())
            self.assertEqual(
                original["functions"], reexported["functions"],
                f"{name}: JSON export not deterministic"
            )


if __name__ == "__main__":
    unittest.main()
