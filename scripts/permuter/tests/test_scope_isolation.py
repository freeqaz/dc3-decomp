"""Tests for variant scope isolation (func_byte_range validation)."""

from __future__ import annotations

import unittest
from pathlib import Path

from scripts.permuter.types import Variant, variant_file_updates


def _make_variant(
    source: bytes,
    original: bytes | None = None,
    byte_range: tuple[int, int] | None = None,
    name: str = "test",
    pattern_name: str = "test_pattern",
) -> Variant:
    return Variant(
        name=name,
        pattern_name=pattern_name,
        description="test variant",
        source=source,
        func_byte_range=byte_range,
        original_source=original,
    )


_PATH = Path("/tmp/test.cpp")

# Simulate a file with three regions:
#   bytes 0-9:   "// before\n"
#   bytes 10-29: "void foo() { ... }\n"  (target function)
#   bytes 30-39: "// after\n"
_ORIGINAL = b"// before\nvoid foo() { bar(); }\n// after\n"
_FUNC_START = 10
_FUNC_END = 32  # end of "void foo() { bar(); }\n"


class TestScopeIsolation(unittest.TestCase):

    def test_in_scope_modification_passes(self):
        """Modifying only the function body should pass validation."""
        mod = bytearray(_ORIGINAL)
        # Replace "bar" with "baz" inside function
        idx = _ORIGINAL.index(b"bar")
        mod[idx : idx + 3] = b"baz"
        variant = _make_variant(
            bytes(mod), _ORIGINAL, (_FUNC_START, _FUNC_END)
        )
        # Should not raise
        result = variant_file_updates(_PATH, variant)
        self.assertIn(_PATH.resolve(), result)

    def test_before_function_modification_rejected(self):
        """Modifying bytes before the function should raise ValueError."""
        mod = bytearray(_ORIGINAL)
        mod[0:2] = b"XX"  # modify "// before" region
        variant = _make_variant(
            bytes(mod), _ORIGINAL, (_FUNC_START, _FUNC_END)
        )
        with self.assertRaises(ValueError) as cm:
            variant_file_updates(_PATH, variant)
        self.assertIn("BEFORE", str(cm.exception))

    def test_after_function_modification_rejected(self):
        """Modifying bytes after the function should raise ValueError."""
        mod = bytearray(_ORIGINAL)
        mod[-3:] = b"XXX"  # modify "// after" region
        variant = _make_variant(
            bytes(mod), _ORIGINAL, (_FUNC_START, _FUNC_END)
        )
        with self.assertRaises(ValueError) as cm:
            variant_file_updates(_PATH, variant)
        self.assertIn("AFTER", str(cm.exception))

    def test_no_metadata_skips_validation(self):
        """Without scope metadata, any modification passes (backward compat)."""
        mod = bytearray(_ORIGINAL)
        mod[0:2] = b"XX"
        variant = _make_variant(bytes(mod))  # no byte_range or original
        # Should not raise
        result = variant_file_updates(_PATH, variant)
        self.assertIn(_PATH.resolve(), result)

    def test_function_body_growth(self):
        """Growing the function body should pass if before/after are intact."""
        # Insert extra code inside the function
        insert_at = _ORIGINAL.index(b"bar")
        mod = _ORIGINAL[:insert_at] + b"extra(); " + _ORIGINAL[insert_at:]
        variant = _make_variant(
            mod, _ORIGINAL, (_FUNC_START, _FUNC_END)
        )
        result = variant_file_updates(_PATH, variant)
        self.assertIn(_PATH.resolve(), result)

    def test_function_body_shrink(self):
        """Shrinking the function body should pass if before/after are intact."""
        # Remove "bar(); " from inside the function
        bar_start = _ORIGINAL.index(b"bar();")
        bar_end = bar_start + len(b"bar(); ")
        mod = _ORIGINAL[:bar_start] + _ORIGINAL[bar_end:]
        variant = _make_variant(
            mod, _ORIGINAL, (_FUNC_START, _FUNC_END)
        )
        result = variant_file_updates(_PATH, variant)
        self.assertIn(_PATH.resolve(), result)

    def test_growth_plus_after_modification_rejected(self):
        """Growing function AND modifying after-region should raise."""
        insert_at = _ORIGINAL.index(b"bar")
        mod = bytearray(
            _ORIGINAL[:insert_at] + b"extra(); " + _ORIGINAL[insert_at:]
        )
        # Also corrupt the after region
        mod[-1] = ord("X")
        variant = _make_variant(
            bytes(mod), _ORIGINAL, (_FUNC_START, _FUNC_END)
        )
        with self.assertRaises(ValueError):
            variant_file_updates(_PATH, variant)

    def test_cross_unit_pattern_skips_before_check(self):
        """Cross-unit patterns may insert wrappers before the function."""
        mod = bytearray(_ORIGINAL)
        # Modify a byte BEFORE the function — simulates a wrapper insertion.
        mod[0:2] = b"XX"
        variant = _make_variant(
            bytes(mod),
            _ORIGINAL,
            (_FUNC_START, _FUNC_END),
            pattern_name="accessor_outline",
        )
        # Should NOT raise — cross-unit patterns are exempt.
        result = variant_file_updates(_PATH, variant)
        self.assertIn(_PATH.resolve(), result)

    def test_composed_cross_unit_pattern_skips_before_check(self):
        """compose:accessor_outline+X should also be exempt.

        Regression test: previously the cross-unit allowlist used exact
        pattern_name equality, so composed names never matched and
        legitimate wrapper insertions raised ValueError.
        """
        mod = bytearray(_ORIGINAL)
        mod[0:2] = b"XX"
        variant = _make_variant(
            bytes(mod),
            _ORIGINAL,
            (_FUNC_START, _FUNC_END),
            pattern_name="compose:accessor_outline+declaration_reorder",
        )
        result = variant_file_updates(_PATH, variant)
        self.assertIn(_PATH.resolve(), result)

    def test_chain_cross_unit_pattern_skips_before_check(self):
        """chain:a+accessor_outline+b should also be exempt."""
        mod = bytearray(_ORIGINAL)
        mod[0:2] = b"XX"
        variant = _make_variant(
            bytes(mod),
            _ORIGINAL,
            (_FUNC_START, _FUNC_END),
            pattern_name="chain:declaration_reorder+accessor_outline+goto_to_return",
        )
        result = variant_file_updates(_PATH, variant)
        self.assertIn(_PATH.resolve(), result)

    def test_compose_without_cross_unit_stage_still_strict(self):
        """compose:foo+bar with no cross-unit stage must still raise."""
        mod = bytearray(_ORIGINAL)
        mod[0:2] = b"XX"
        variant = _make_variant(
            bytes(mod),
            _ORIGINAL,
            (_FUNC_START, _FUNC_END),
            pattern_name="compose:declaration_reorder+goto_to_return",
        )
        with self.assertRaises(ValueError) as cm:
            variant_file_updates(_PATH, variant)
        self.assertIn("BEFORE", str(cm.exception))


if __name__ == "__main__":
    unittest.main()
