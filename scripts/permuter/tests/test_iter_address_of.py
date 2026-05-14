"""Tests for the iter_address_of pattern.

Verifies:
- `&*<expr>` is detected and dropped to `<expr>` at call sites
- iterator-named bare-identifier args get the reverse `&*` wrap variant
- "drop all" variant fires when 2+ sites are present
- relevant() requires a non-empty diagnosis signal
- pattern is registered

Usage:
    python -m pytest scripts/permuter/tests/test_iter_address_of.py -x -q
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.tests.conftest import (
    _empty_diag,
    diag_with_clusters,
    diag_with_gpr_swaps,
    make_context,
    normalize,
)
from scripts.permuter.patterns.base import get_pattern


def _diag_with_replace_real():
    """Diagnosis with at least one real (non-noise) replace."""
    d = _empty_diag()
    d.replace_real = 1
    return d


# ---------------------------------------------------------------------------
# Test sources
# ---------------------------------------------------------------------------

# Mimics FitnessCalorieSort::BuildTree shape — `&*` on iterator pair
_SOURCE_DROP_AMP_DEREF = """\
void Outer::Build() {
    auto begin = mValues.begin();
    auto it = begin + 1;
    InsertHeaderRange(&*begin, &*it);
}
"""

# Single &*<expr>
_SOURCE_DROP_AMP_DEREF_SINGLE = """\
void Outer::Build() {
    auto it = mValues.begin();
    Use(&*it);
}
"""

# Iterator-named arg passed bare — reverse direction candidate
_SOURCE_REVERSE_WRAP = """\
void Outer::Build() {
    auto begin = mValues.begin();
    auto end = mValues.end();
    InsertHeaderRange(begin, end);
}
"""

# No `&*` and no iterator-named args → no variants
_SOURCE_NO_SITES = """\
void Outer::Build() {
    int x = 1;
    Use(x, this);
}
"""


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


class IterAddressOfRegistrationTests(unittest.TestCase):
    def test_pattern_is_registered(self):
        pat = get_pattern("iter_address_of")
        self.assertEqual(pat.name, "iter_address_of")
        self.assertEqual(pat.safety_tier, "moderate")
        self.assertEqual(pat.structural_domain, "data_flow")


class IterAddressOfRelevanceTests(unittest.TestCase):
    def test_irrelevant_on_empty_diag(self):
        pat = get_pattern("iter_address_of")
        self.assertFalse(pat.relevant(_empty_diag()))
        self.assertEqual(pat.priority(_empty_diag()), 0.0)

    def test_relevant_on_replace_real(self):
        pat = get_pattern("iter_address_of")
        self.assertTrue(pat.relevant(_diag_with_replace_real()))

    def test_relevant_on_clusters(self):
        pat = get_pattern("iter_address_of")
        self.assertTrue(pat.relevant(diag_with_clusters()))

    def test_relevant_on_reg_swaps(self):
        pat = get_pattern("iter_address_of")
        self.assertTrue(pat.relevant(diag_with_gpr_swaps()))


class IterAddressOfDropTests(unittest.TestCase):
    def test_drops_amp_deref_pair(self):
        ctx = make_context(_SOURCE_DROP_AMP_DEREF, "Outer::Build", _diag_with_replace_real())
        pat = get_pattern("iter_address_of")
        variants = list(pat.generate(ctx))
        self.assertGreater(len(variants), 0)
        # At least one variant should remove a `&*`
        sources = [v.source.decode("utf-8") for v in variants]
        self.assertTrue(
            any("&*" not in s for s in sources),
            f"No variant removed all `&*` markers; got: {sources}",
        )

    def test_drops_single_amp_deref(self):
        ctx = make_context(
            _SOURCE_DROP_AMP_DEREF_SINGLE, "Outer::Build", _diag_with_replace_real()
        )
        pat = get_pattern("iter_address_of")
        variants = list(pat.generate(ctx))
        self.assertGreater(len(variants), 0)
        # The single-site variant should produce `Use(it)`.
        self.assertTrue(
            any("Use(it)" in v.source.decode("utf-8") for v in variants),
            f"Expected `Use(it)` in some variant; got: "
            f"{[v.source.decode('utf-8') for v in variants]}",
        )

    def test_drop_all_variant_when_multiple_sites(self):
        ctx = make_context(
            _SOURCE_DROP_AMP_DEREF, "Outer::Build", _diag_with_replace_real()
        )
        pat = get_pattern("iter_address_of")
        variants = list(pat.generate(ctx))
        # Look for the "drop all" name
        names = [v.name for v in variants]
        self.assertTrue(
            any(n.startswith("iteraddr_drop_all_") for n in names),
            f"Expected a 'drop all' variant; got names: {names}",
        )


class IterAddressOfReverseWrapTests(unittest.TestCase):
    def test_wraps_iter_named_args(self):
        ctx = make_context(_SOURCE_REVERSE_WRAP, "Outer::Build", _diag_with_replace_real())
        pat = get_pattern("iter_address_of")
        variants = list(pat.generate(ctx))
        names = [v.name for v in variants]
        self.assertTrue(
            any(n.startswith("iteraddr_wrap_") for n in names),
            f"Expected a wrap variant for iterator-named args; got names: {names}",
        )
        # At least one variant should contain `&*begin` or `&*end`
        sources = [v.source.decode("utf-8") for v in variants]
        self.assertTrue(
            any("&*begin" in s or "&*end" in s for s in sources),
            f"No wrap variant produced &*<iter>; got: {sources}",
        )


class IterAddressOfNoSitesTests(unittest.TestCase):
    def test_no_variants_when_no_sites(self):
        ctx = make_context(_SOURCE_NO_SITES, "Outer::Build", _diag_with_replace_real())
        pat = get_pattern("iter_address_of")
        variants = list(pat.generate(ctx))
        self.assertEqual(variants, [])


if __name__ == "__main__":
    unittest.main()
