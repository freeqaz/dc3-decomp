"""Tests for IL canonicalization and hashing prototype."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
FIXTURES_DIR = TOOLS_DIR.parent / "analysis" / "il-fixtures"
sys.path.insert(0, str(TOOLS_DIR))

from il_permuter import (  # noqa: E402
    bucket_functions,
    canonicalize_function,
    compare_bundles,
    function_hash,
    load_bundle,
)


class TestCanonicalization(unittest.TestCase):
    def test_alpha_renames_local_tokens(self):
        func_a = {
            "name": "?demo@@YAHH@Z",
            "operation_count": 4,
            "params": [1001],
            "result_var": 1002,
            "operations": [
                {"type": "label", "name": "LABEL", "label": 9},
                {
                    "type": "op",
                    "name": "ADD",
                    "operands": [
                        {"kind": "var", "value": 1001, "type": "int", "name": "x"},
                        {"kind": "lit", "value": 1, "type": "int"},
                    ],
                },
                {"type": "assign", "name": "ASSIGN", "target": 1002, "operands": []},
                {"type": "return", "name": "RETURN", "target": 1002},
            ],
        }
        func_b = {
            "name": "?demo@@YAHH@Z",
            "operation_count": 4,
            "params": [7001],
            "result_var": 8002,
            "operations": [
                {"type": "label", "name": "LABEL", "label": 33},
                {
                    "type": "op",
                    "name": "ADD",
                    "operands": [
                        {"kind": "var", "value": 7001, "type": "int", "name": "renamed"},
                        {"kind": "lit", "value": 1, "type": "int"},
                    ],
                },
                {"type": "assign", "name": "ASSIGN", "target": 8002, "operands": []},
                {"type": "return", "name": "RETURN", "target": 8002},
            ],
        }
        self.assertEqual(canonicalize_function(func_a), canonicalize_function(func_b))
        self.assertEqual(function_hash(func_a), function_hash(func_b))

    def test_preserves_external_symbol_hints_when_present(self):
        func = {
            "name": "?demo@@YAXXZ",
            "operation_count": 2,
            "operations": [
                {
                    "type": "call_start",
                    "name": "CALL_START",
                    "return_type": "int",
                    "operands": [
                        {"kind": "ref", "value": 9001, "name": "?helper@@YAHXZ"},
                    ],
                },
                {"type": "call_exec", "name": "CALL_EXEC", "return_type": "int", "operands": []},
            ],
        }
        canonical = canonicalize_function(func)
        self.assertEqual(
            canonical["operations"][0]["operands"][0]["symbol"],
            "?helper@@YAHXZ",
        )


class TestFixtures(unittest.TestCase):
    def test_cast_vs_and_hashes_differ(self):
        bundle = load_bundle(FIXTURES_DIR / "il_type_control_cast_vs_and")
        functions = {func["name"]: func for func in bundle["functions"]}
        cast_hash = function_hash(functions["?cast_shift@@YAII@Z"])
        and_hash = function_hash(functions["?and_shift@@YAII@Z"])
        self.assertNotEqual(cast_hash, and_hash)

    def test_tail_call_and_plain_call_differ(self):
        bundle = load_bundle(FIXTURES_DIR / "il_call_return")
        functions = {func["name"]: func for func in bundle["functions"]}
        call_hash = function_hash(functions["?call_and_return@@YAHH@Z"])
        tail_hash = function_hash(functions["?tail_call@@YAHH@Z"])
        self.assertNotEqual(call_hash, tail_hash)

    def test_bundle_json_and_dir_load_same_result(self):
        bundle_from_dir = load_bundle(FIXTURES_DIR / "il_switch_dispatch")
        bundle_from_json = load_bundle(FIXTURES_DIR / "il_switch_dispatch" / "bundle.json")
        result = compare_bundles(bundle_from_dir, bundle_from_json)
        self.assertEqual(result["changed"], [])
        self.assertEqual(result["added"], [])
        self.assertEqual(result["removed"], [])
        self.assertEqual(result["bundle_hash_a"], result["bundle_hash_b"])

    def test_fixture_bucket_corpus_has_no_large_accidental_collisions(self):
        groups = bucket_functions(FIXTURES_DIR, function_filter=None)
        large_groups = [members for members in groups.values() if len(members) > 1]
        self.assertEqual(
            large_groups,
            [],
            f"unexpected canonical IL collisions: {large_groups}",
        )


if __name__ == "__main__":
    unittest.main()
