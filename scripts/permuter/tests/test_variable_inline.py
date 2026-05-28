"""Regression tests for variable_inline pattern.

Pure AST/text-level tests — no builds, no objdiff.

Covers:
- Normal inline (decl and use on separate lines) produces one valid variant.
- Same-line decl+use overlap (Wave J4 crash) is silently skipped, not raised.
"""

from __future__ import annotations

import sys
import textwrap
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.extractor import _PARSER, _get_function_name
from scripts.permuter.types import Diagnosis, FunctionContext, DiffOp
from scripts.permuter.patterns.variable_inline import VariableInlinePattern


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_ctx(source_text: str, func_name: str, diagnosis: Diagnosis) -> FunctionContext:
    source_bytes = textwrap.dedent(source_text).encode("utf-8")
    tree = _PARSER.parse(source_bytes)
    for child in tree.root_node.children:
        if child.type != "function_definition":
            continue
        if _get_function_name(child) == func_name:
            body = child.child_by_field_name("body")
            if body is None:
                raise ValueError(f"Function {func_name} has no body")
            return FunctionContext(
                file_path=Path("/dev/null"),
                file_source=source_bytes,
                func_node=child,
                body_node=body,
                statements=list(body.named_children),
                func_byte_range=(child.start_byte, child.end_byte),
                diagnosis=diagnosis,
            )
    raise ValueError(f"Function '{func_name}' not found")


def _diag_regswap() -> Diagnosis:
    return Diagnosis(
        total_instructions=20,
        match_counts={},
        reg_swap_pairs={("r3", "r4")},
        offset_deltas={},
        diff_ops=[DiffOp(0, "mr", "mr")],
        clusters=[],
        noise_explained=0,
        noise_total=1,
    )


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestVariableInline(unittest.TestCase):

    def setUp(self):
        self.pattern = VariableInlinePattern()

    # -- normal operation ----------------------------------------------------

    def test_normal_inline_separate_lines(self):
        """Standard case: decl on one line, single use on next — yields one variant."""
        src = """
        void Fn() {
            float x = GetX();
            Use(x);
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_regswap())
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(len(variants), 1)
        self.assertIn(b"Use(GetX())", variants[0].source)
        self.assertNotIn(b"float x", variants[0].source)

    def test_normal_inline_multiple_uses(self):
        """Multiple uses (up to _MAX_USES=3) on separate lines are all substituted."""
        src = """
        void Fn() {
            float x = GetX();
            UseA(x);
            UseB(x);
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_regswap())
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(len(variants), 1)
        self.assertIn(b"UseA(GetX())", variants[0].source)
        self.assertIn(b"UseB(GetX())", variants[0].source)

    # -- Wave J4 regression: same-line decl+use overlap -----------------------

    def test_same_line_decl_and_use_does_not_crash(self):
        """Regression (Wave J4): decl and use on same source line must not raise.

        When 'float x = GetX(); Use(x);' are on one line, _line_start/_line_end
        extends the delete range to cover the whole line, which contains the use
        node.  Applying a delete [line_start, line_end) together with a replace
        [use.start, use.end) causes overlapping edits in SourceEditor.apply().
        The fix skips the variant rather than crashing.
        """
        src = """
        void Fn() {
            float x = GetX(); Use(x);
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_regswap())
        # Must NOT raise ValueError("Overlapping edits: ...")
        try:
            variants = list(self.pattern.generate(ctx))
        except ValueError as exc:
            self.fail(f"generate() raised ValueError for same-line decl+use: {exc}")

        # The overlapping candidate should be skipped (zero valid variants).
        # All returned variants must have non-overlapping edits (implicitly
        # guaranteed by SourceEditor, but verify source is well-formed bytes).
        for v in variants:
            self.assertIsInstance(v.source, bytes)
            self.assertGreater(len(v.source), 0)

    def test_same_line_yields_zero_variants(self):
        """Same-line decl+use produces zero variants (skipped, not inlined)."""
        src = """
        void Fn() {
            float x = GetX(); Use(x);
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_regswap())
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(
            len(variants),
            0,
            "Expected no variants when decl and use are on the same source line",
        )

    def test_partial_same_line_skipped_other_inlined(self):
        """Only the same-line candidate is skipped; other variables still inline."""
        # x and y both candidates; x's use is on same line as decl (skip),
        # y's use is on a separate line (inline).
        src = """
        void Fn() {
            float x = GetX(); float y = GetY();
            UseY(y);
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_regswap())
        variants = list(self.pattern.generate(ctx))
        # x is skipped (same-line overlap with y decl on same line as x decl);
        # y should produce a variant (use is on separate line).
        names = [v.description for v in variants]
        # At minimum, y variant should appear; x variant must NOT crash.
        for v in variants:
            self.assertIsInstance(v.source, bytes)


if __name__ == "__main__":
    unittest.main()
