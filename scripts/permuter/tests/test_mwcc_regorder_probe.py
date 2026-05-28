"""Tests for mwcc_regorder_probe pattern.

Pure AST/text-level tests — no builds, no objdiff.

Coverage:
  - Positive: function with 4+ this->member accesses + callee-saved regswap diagnosis
    -> emits permutation variants
  - Negative: volatile-only regswap -> does NOT fire
  - Negative: function with < 3 this->member accesses -> does NOT fire
  - Cap: never emits > 10 variants per function
  - Saturation: saturated function (most members already bound) -> does NOT fire

Usage:
    python -m pytest scripts/permuter/tests/test_mwcc_regorder_probe.py -x -q
"""

from __future__ import annotations

import sys
import textwrap
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.patterns import mwcc_regorder_probe  # noqa: F401 — triggers registration
from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.tests.conftest import make_context, _empty_diag, normalize
from scripts.permuter.types import Diagnosis, SwapInfo


# ---------------------------------------------------------------------------
# Diagnosis factories
# ---------------------------------------------------------------------------

def _diag_callee_saved_gpr() -> Diagnosis:
    """Callee-saved GPR swaps only — strongest trigger for mwcc_regorder_probe."""
    d = _empty_diag()
    d.reg_swap_pairs = {
        ("r20", "r21"): SwapInfo(count=5, first_idx=10, last_idx=60),
        ("r18", "r19"): SwapInfo(count=3, first_idx=15, last_idx=55),
    }
    return d


def _diag_callee_saved_fpr() -> Diagnosis:
    """Callee-saved FPR swaps only."""
    d = _empty_diag()
    d.reg_swap_pairs = {
        ("f20", "f21"): SwapInfo(count=4, first_idx=8, last_idx=50),
    }
    return d


def _diag_volatile_only() -> Diagnosis:
    """Volatile-only swaps (r3-r12) — should NOT trigger this pattern."""
    d = _empty_diag()
    d.reg_swap_pairs = {
        ("r3", "r4"): SwapInfo(count=2, first_idx=5, last_idx=20),
        ("r5", "r6"): SwapInfo(count=1, first_idx=8, last_idx=12),
    }
    return d


def _diag_mixed_callee_volatile() -> Diagnosis:
    """Mixed callee-saved + volatile — should trigger with lower priority."""
    d = _empty_diag()
    d.reg_swap_pairs = {
        ("r20", "r21"): SwapInfo(count=3, first_idx=10, last_idx=50),  # callee-saved
        ("r3", "r4"): SwapInfo(count=2, first_idx=5, last_idx=20),      # volatile
    }
    return d


def _diag_no_swaps() -> Diagnosis:
    """No register swaps at all."""
    return _empty_diag()


# ---------------------------------------------------------------------------
# Source fixtures
# ---------------------------------------------------------------------------

# NOTE: make_context() only finds top-level function_definition nodes (not methods
# inside class/struct bodies). So we write these as standalone functions with an
# explicit Gem* this parameter to simulate the this->member access pattern.

# Function with 4 distinct this->member accesses — should generate variants
_FOUR_MEMBER_SRC = """\
struct Gem { float mX; float mY; int mCount; float mScore; };
bool Gem_IsSpotlightGem(Gem* this, int idx) {
    float x = this->mX;
    float y = this->mY;
    if (this->mCount > idx) {
        return this->mScore > 0.5f;
    }
    return false;
}
"""

# Function with only 2 distinct this->member accesses — should NOT fire
_TWO_MEMBER_SRC = """\
struct Gem { float mX; float mY; };
bool Gem_IsSmall(Gem* this) {
    return this->mX < this->mY;
}
"""

# Free function with no this-> accesses — should NOT fire
_FREE_FUNC_SRC = """\
int add(int a, int b) {
    int c = a + b;
    int d = c * 2;
    int e = d - a;
    return e;
}
"""

# Function with 3 distinct members — boundary case: should fire
_THREE_MEMBER_SRC = """\
struct Track { int mGems; float mSpeed; bool mActive; };
void doSomething(int, float);
void Track_UpdateLeftyFlip(Track* this) {
    if (this->mActive) {
        int g = this->mGems;
        float s = this->mSpeed;
        doSomething(g, s);
    }
}
"""

# Function where most members are already bound at top level — saturated
_SATURATED_SRC = """\
struct Track { int mGems; float mSpeed; bool mActive; float mScore; };
void Track_SetupGems(Track* this) {
    int& _mGems = this->mGems;
    float& _mSpeed = this->mSpeed;
    bool& _mActive = this->mActive;
    float& _mScore = this->mScore;
    if (_mActive) {
        _mGems = (int)(_mSpeed * _mScore);
    }
}
"""

# Function with 5 members — should generate variants and cap at 10
_FIVE_MEMBER_SRC = """\
struct Band { int mA; int mB; int mC; int mD; int mE; };
int Band_ScoreSinger(Band* this, int base) {
    int a = this->mA;
    int b = this->mB;
    int c = this->mC;
    int d = this->mD;
    int e = this->mE;
    return a + b + c + d + e + base;
}
"""

# RB3/mwcc style: implicit-this member access (bare mFoo, no `this->`).
# This is the form _collect_this_members must ALSO detect, not just explicit
# this->member. Function below should generate variants.
_IMPLICIT_THIS_SRC = """\
int GemManager_GetBestHit(int idx) {
    int g = mGems;
    float s = mSpeed;
    if (mActive) {
        return g + (int)(s * mScore);
    }
    return 0;
}
"""

# Implicit-this but member name collides with a local — must NOT count the local
# as a member.
_IMPLICIT_THIS_LOCAL_SHADOW_SRC = """\
int GemManager_Foo(int idx) {
    int mLocal = idx;
    int g = mGems;
    float s = mSpeed;
    bool a = mActive;
    return g + (int)(s) + (a ? 1 : 0) + mLocal;
}
"""

# Implicit-this where ALL m-prefixed names are locals — should NOT fire (no real members)
_ALL_LOCALS_SRC = """\
int just_locals(int x) {
    int mFirst = x;
    int mSecond = x + 1;
    int mThird = x + 2;
    return mFirst + mSecond + mThird;
}
"""

# x.mFoo where mFoo is a field of another object — must NOT count as implicit-this
_OTHER_OBJECT_FIELD_SRC = """\
struct Box { int mWidth; int mHeight; int mDepth; };
int sum_box(Box& b, int idx) {
    int w = b.mWidth;
    int h = b.mHeight;
    int d = b.mDepth;
    return w + h + d + idx;
}
"""


# ---------------------------------------------------------------------------
# Test class
# ---------------------------------------------------------------------------

class TestMwccRegorderProbe(unittest.TestCase):

    def _make_ctx(self, source: str, func_name: str, diag: Diagnosis,
                  dialect: str = "msvc"):
        """Build a FunctionContext.

        Uses 'msvc' dialect by default so tests don't need real class headers
        (mwcc requires concrete member types from headers; msvc uses auto&).
        The AST-level logic (member collection, saturation check, permutations)
        is identical for both dialects.
        """
        ctx = make_context(textwrap.dedent(source), func_name, diag)
        ctx.compiler_dialect = dialect
        return ctx

    def _variants(self, source: str, func_name: str, diag: Diagnosis,
                  dialect: str = "msvc") -> list:
        ctx = self._make_ctx(source, func_name, diag, dialect=dialect)
        pattern = get_pattern("mwcc_regorder_probe")
        return list(pattern.generate(ctx))

    # -----------------------------------------------------------------------
    # Registration
    # -----------------------------------------------------------------------

    def test_registration(self):
        """Pattern must be registered under the correct name."""
        p = get_pattern("mwcc_regorder_probe")
        self.assertIsNotNone(p)
        self.assertEqual(p.name, "mwcc_regorder_probe")

    # -----------------------------------------------------------------------
    # Relevance gate
    # -----------------------------------------------------------------------

    def test_relevant_callee_saved_gpr(self):
        """relevant() returns True for callee-saved GPR swaps."""
        p = get_pattern("mwcc_regorder_probe")
        self.assertTrue(p.relevant(_diag_callee_saved_gpr()))

    def test_relevant_callee_saved_fpr(self):
        """relevant() returns True for callee-saved FPR swaps."""
        p = get_pattern("mwcc_regorder_probe")
        self.assertTrue(p.relevant(_diag_callee_saved_fpr()))

    def test_not_relevant_volatile_only(self):
        """relevant() returns False for volatile-only swaps (r3-r12)."""
        p = get_pattern("mwcc_regorder_probe")
        self.assertFalse(p.relevant(_diag_volatile_only()))

    def test_not_relevant_no_swaps(self):
        """relevant() returns False when there are no register swaps."""
        p = get_pattern("mwcc_regorder_probe")
        self.assertFalse(p.relevant(_diag_no_swaps()))

    def test_relevant_mixed_callee_volatile(self):
        """relevant() returns True for mixed callee-saved + volatile swaps."""
        p = get_pattern("mwcc_regorder_probe")
        self.assertTrue(p.relevant(_diag_mixed_callee_volatile()))

    # -----------------------------------------------------------------------
    # Priority
    # -----------------------------------------------------------------------

    def test_priority_pure_callee_saved(self):
        """priority() returns 0.1 (experimental) for pure callee-saved swaps."""
        p = get_pattern("mwcc_regorder_probe")
        self.assertAlmostEqual(p.priority(_diag_callee_saved_gpr()), 0.1, places=5)

    def test_priority_mixed(self):
        """priority() returns 0.1 (experimental) for mixed callee-saved + volatile."""
        p = get_pattern("mwcc_regorder_probe")
        self.assertAlmostEqual(p.priority(_diag_mixed_callee_volatile()), 0.1, places=5)

    def test_priority_volatile_only_zero(self):
        """priority() returns 0.0 for volatile-only swaps."""
        p = get_pattern("mwcc_regorder_probe")
        self.assertAlmostEqual(p.priority(_diag_volatile_only()), 0.0, places=5)

    # -----------------------------------------------------------------------
    # Positive: 4 members + callee-saved regswap -> generates variants
    # -----------------------------------------------------------------------

    def test_four_members_generates_variants(self):
        """Function with 4 this->members + callee-saved regswap -> variants emitted."""
        variants = self._variants(_FOUR_MEMBER_SRC, "Gem_IsSpotlightGem",
                                  _diag_callee_saved_gpr())
        self.assertGreater(len(variants), 0,
                           "Expected at least 1 variant for 4-member function with callee-saved swaps")

    def test_four_members_variants_contain_ref_decls(self):
        """Variants should contain this->member reference declarations."""
        variants = self._variants(_FOUR_MEMBER_SRC, "Gem_IsSpotlightGem",
                                  _diag_callee_saved_gpr())
        # At least one variant should introduce a local reference with this->
        has_ref = any(b"this->" in v.source for v in variants)
        self.assertTrue(has_ref, "Expected at least one variant with this->member reference")

    def test_five_members_cap_at_ten(self):
        """Never emit more than 10 variants per function call."""
        variants = self._variants(_FIVE_MEMBER_SRC, "Band_ScoreSinger",
                                  _diag_callee_saved_gpr())
        self.assertLessEqual(len(variants), 10,
                             f"Expected <= 10 variants, got {len(variants)}")

    def test_three_members_boundary(self):
        """Function with exactly 3 members should also generate variants."""
        variants = self._variants(_THREE_MEMBER_SRC, "Track_UpdateLeftyFlip",
                                  _diag_callee_saved_gpr())
        self.assertGreater(len(variants), 0,
                           "Expected variants for 3-member function (boundary case)")

    # -----------------------------------------------------------------------
    # Negative: too few members -> no variants
    # -----------------------------------------------------------------------

    def test_two_members_no_variants(self):
        """Function with < 3 distinct this->member accesses -> no variants."""
        variants = self._variants(_TWO_MEMBER_SRC, "Gem_IsSmall",
                                  _diag_callee_saved_gpr())
        self.assertEqual(len(variants), 0,
                         "Expected no variants for function with only 2 this->member accesses")

    def test_free_function_no_variants(self):
        """Free function with no this-> accesses -> no variants."""
        variants = self._variants(_FREE_FUNC_SRC, "add",
                                  _diag_callee_saved_gpr())
        self.assertEqual(len(variants), 0,
                         "Expected no variants for free function with no this-> accesses")

    # -----------------------------------------------------------------------
    # Negative: volatile-only -> no variants
    # -----------------------------------------------------------------------

    def test_volatile_only_no_variants(self):
        """Volatile-only register swaps -> no variants (wrong register class)."""
        variants = self._variants(_FOUR_MEMBER_SRC, "Gem_IsSpotlightGem",
                                  _diag_volatile_only())
        self.assertEqual(len(variants), 0,
                         "Expected no variants when all swaps are volatile-only")

    # -----------------------------------------------------------------------
    # Saturation guard
    # -----------------------------------------------------------------------

    def test_saturated_no_variants(self):
        """Function already saturated with this->member bindings -> no variants."""
        variants = self._variants(_SATURATED_SRC, "Track_SetupGems",
                                  _diag_callee_saved_gpr())
        self.assertEqual(len(variants), 0,
                         "Expected no variants for saturated function (members already bound)")

    # -----------------------------------------------------------------------
    # Variant quality checks
    # -----------------------------------------------------------------------

    def test_variants_differ_from_original(self):
        """All generated variants must differ from the original source."""
        ctx = self._make_ctx(_FOUR_MEMBER_SRC, "Gem_IsSpotlightGem",
                             _diag_callee_saved_gpr())
        pattern = get_pattern("mwcc_regorder_probe")
        original = ctx.file_source
        for v in pattern.generate(ctx):
            self.assertNotEqual(v.source, original,
                                f"Variant {v.name} is identical to original source")

    def test_variants_have_correct_pattern_name(self):
        """All variants must carry pattern_name == 'mwcc_regorder_probe'."""
        ctx = self._make_ctx(_FOUR_MEMBER_SRC, "Gem_IsSpotlightGem",
                             _diag_callee_saved_gpr())
        pattern = get_pattern("mwcc_regorder_probe")
        for v in pattern.generate(ctx):
            self.assertEqual(v.pattern_name, "mwcc_regorder_probe",
                             f"Variant {v.name} has wrong pattern_name: {v.pattern_name}")

    def test_variants_have_tags(self):
        """Variants should carry callee-saved regswap tags."""
        ctx = self._make_ctx(_FOUR_MEMBER_SRC, "Gem_IsSpotlightGem",
                             _diag_callee_saved_gpr())
        pattern = get_pattern("mwcc_regorder_probe")
        for v in pattern.generate(ctx):
            self.assertIn("regorder_probe", v.tags,
                          f"Variant {v.name} missing 'regorder_probe' tag")

    def test_variants_unique(self):
        """All generated variants should have unique source content."""
        variants = self._variants(_FIVE_MEMBER_SRC, "Band_ScoreSinger",
                                  _diag_callee_saved_gpr())
        unique_sources = set(v.source for v in variants)
        self.assertEqual(len(unique_sources), len(variants),
                         "Duplicate variant sources detected")

    # -----------------------------------------------------------------------
    # msvc dialect (should use auto& instead of concrete types)
    # -----------------------------------------------------------------------

    def test_msvc_dialect_uses_auto(self):
        """For msvc dialect, variants use auto& instead of concrete types."""
        # Already default dialect=msvc in _make_ctx, but be explicit here
        variants = self._variants(_FOUR_MEMBER_SRC, "Gem_IsSpotlightGem",
                                  _diag_callee_saved_gpr(), dialect="msvc")
        # msvc does not require type lookup, so should produce variants
        self.assertGreater(len(variants), 0,
                           "Expected variants for msvc dialect (no type lookup needed)")
        # At least one should use auto&
        has_auto = any(b"auto&" in v.source for v in variants)
        self.assertTrue(has_auto, "Expected 'auto&' in at least one msvc variant")

    def test_mwcc_dialect_no_header_no_variants(self):
        """For mwcc dialect without a real header, 0 variants (can't emit typed refs)."""
        # When file_path is /dev/null, no header can be found, so mwcc mode
        # cannot generate safe typed references and emits nothing.
        variants = self._variants(_FOUR_MEMBER_SRC, "Gem_IsSpotlightGem",
                                  _diag_callee_saved_gpr(), dialect="mwcc")
        self.assertEqual(len(variants), 0,
                         "Expected 0 variants for mwcc when no header is available")

    # -----------------------------------------------------------------------
    # Implicit-this member access (RB3/mwcc convention) — bare mFoo without `this->`
    # -----------------------------------------------------------------------

    def test_implicit_this_member_access_collects(self):
        """Bare `mFoo` references should be collected as implicit-this members."""
        from scripts.permuter.patterns.mwcc_regorder_probe import _collect_this_members
        ctx = self._make_ctx(_IMPLICIT_THIS_SRC, "GemManager_GetBestHit",
                             _diag_callee_saved_gpr())
        members = _collect_this_members(ctx)
        names = [name for name, _ in members]
        # Order of first appearance: mGems, mSpeed, mActive, mScore
        self.assertEqual(names[:4], ["mGems", "mSpeed", "mActive", "mScore"])

    def test_implicit_this_generates_variants(self):
        """Implicit-this functions should generate permutation variants."""
        variants = self._variants(_IMPLICIT_THIS_SRC, "GemManager_GetBestHit",
                                  _diag_callee_saved_gpr())
        self.assertGreater(len(variants), 0,
                           "Expected variants for implicit-this member access")

    def test_implicit_this_skips_local_shadow(self):
        """An m-prefixed local must NOT be counted as a member."""
        from scripts.permuter.patterns.mwcc_regorder_probe import _collect_this_members
        ctx = self._make_ctx(_IMPLICIT_THIS_LOCAL_SHADOW_SRC, "GemManager_Foo",
                             _diag_callee_saved_gpr())
        members = _collect_this_members(ctx)
        names = [name for name, _ in members]
        self.assertNotIn("mLocal", names,
                         "Local `mLocal` was incorrectly classified as a member")
        # Real members should still be present
        for expected in ("mGems", "mSpeed", "mActive"):
            self.assertIn(expected, names, f"Missing real member {expected!r}")

    def test_all_locals_no_variants(self):
        """If every m-prefixed identifier is a local, no variants emitted."""
        variants = self._variants(_ALL_LOCALS_SRC, "just_locals",
                                  _diag_callee_saved_gpr())
        self.assertEqual(len(variants), 0,
                         "Expected 0 variants when all m-names are locals")

    def test_other_object_field_not_member(self):
        """`b.mWidth` (field of another object) must NOT count as implicit-this."""
        from scripts.permuter.patterns.mwcc_regorder_probe import _collect_this_members
        ctx = self._make_ctx(_OTHER_OBJECT_FIELD_SRC, "sum_box",
                             _diag_callee_saved_gpr())
        members = _collect_this_members(ctx)
        names = [name for name, _ in members]
        self.assertEqual(names, [],
                         f"Other-object field access incorrectly collected: {names}")

    # -----------------------------------------------------------------------
    # v2 alias-and-replace: verify usages ARE replaced (not dead aliases)
    # -----------------------------------------------------------------------

    def test_variants_replace_usages_not_dead_aliases(self):
        """Variants must REPLACE member usages with alias names, not just insert dead decls.

        This is the critical regression test for v2: the old implementation inserted
        ``auto& _mX = this->mX;`` but left all usages as ``this->mX``, so MWCC
        stripped the dead aliases and all orderings produced identical .o files.

        In v2, every usage of ``this->mX`` must be replaced by ``_mX`` in the variant.
        """
        variants = self._variants(_FOUR_MEMBER_SRC, "Gem_IsSpotlightGem",
                                  _diag_callee_saved_gpr())
        self.assertGreater(len(variants), 0)
        v = variants[0]
        src = v.source.decode("utf-8", errors="replace")

        # The variant must contain an alias declaration (hoisted decl)
        self.assertIn("_mX", src, "Expected alias _mX in variant source")

        # After replacement, the original `this->mX` usage in `float x = this->mX;`
        # must NOT appear — it should be replaced by `_mX`.
        # (The declaration `_mX = this->mX;` itself is the only allowed `this->mX`)
        # Count occurrences: there should be exactly 1 per member (the decl itself)
        # or fewer; NOT the original body usages remaining as `this->mX`.
        # Simpler check: the alias _mX appears in the body, not just in the decl line.
        alias_count = src.count("_mX")
        # At minimum: 1 decl + at least 1 usage replacement = 2 occurrences
        self.assertGreaterEqual(alias_count, 2,
                                f"Expected _mX to appear in both decl and usage; found {alias_count}")

    def test_variants_replace_explicit_this_usages(self):
        """For explicit this->mFoo style, body usages become _mFoo (not dead decls).

        The variant adds 4 alias declarations (each containing `this->`) but all
        4 body usages of `this->mX` etc. are replaced with `_mX`. So the total
        `this->` count stays at 4 (only in decl lines), but the important thing
        is that the BODY now uses aliases, not the original `this->member` form.
        """
        variants = self._variants(_FOUR_MEMBER_SRC, "Gem_IsSpotlightGem",
                                  _diag_callee_saved_gpr())
        self.assertGreater(len(variants), 0)
        v = variants[0]
        src = v.source.decode("utf-8", errors="replace")
        lines = src.splitlines()

        # Find the alias declaration lines (the hoisted block at the top of body)
        decl_lines = [l for l in lines if "= this->" in l and ("auto&" in l or "&" in l)]
        # Find the body lines (after the decl block) that use member names
        body_lines = [l for l in lines if ("= this->" not in l or ("auto&" not in l and "& " not in l))]

        # In v2, body lines that access the aliased members must use alias names.
        # Check that `float x = _mX;` appears (alias in body), not `float x = this->mX;`
        body_uses_alias = any("_mX" in l and "this->" not in l for l in body_lines)
        body_uses_orig = any("float x = this->mX" in l for l in body_lines)
        self.assertTrue(body_uses_alias,
                        f"Expected alias _mX in body lines; body_lines={body_lines}")
        self.assertFalse(body_uses_orig,
                         "Original `float x = this->mX` should be replaced by alias")

    def test_implicit_this_variants_replace_usages(self):
        """For implicit-this (bare mFoo), all body usages become _mFoo."""
        variants = self._variants(_IMPLICIT_THIS_SRC, "GemManager_GetBestHit",
                                  _diag_callee_saved_gpr())
        self.assertGreater(len(variants), 0)
        v = variants[0]
        src = v.source.decode("utf-8", errors="replace")

        # Original has bare `mGems`, `mSpeed`, `mActive`, `mScore`.
        # After replacement, aliases like `_mGems` should appear in the body.
        # The original bare name should appear 0 or fewer times in usages.
        # Check that alias names are present:
        has_alias = any(f"_m{m[1:]}" in src for m in ("mGems", "mSpeed", "mActive", "mScore"))
        self.assertTrue(has_alias,
                        "Expected alias names (_mGems etc.) in implicit-this variant source")

    def test_permutation_order_produces_different_sources(self):
        """Different permutation orderings must produce DIFFERENT source text.

        This verifies that the ordering actually changes something observable —
        proof that the codegen will differ (different first-access order for MWCC).

        Uses _THREE_MEMBER_SRC which reliably produces multiple variants (no saturation
        since members are not pre-assigned to locals).
        """
        variants = self._variants(_THREE_MEMBER_SRC, "Track_UpdateLeftyFlip",
                                  _diag_callee_saved_gpr())
        self.assertGreater(len(variants), 1,
                           "Need at least 2 variants to compare orderings")

        # Sources must all differ from each other (uniqueness already tested, but
        # here we verify the ordering in the DECL BLOCK actually differs between variants)
        sources_as_text = [v.source.decode("utf-8", errors="replace") for v in variants]

        # Extract just the first N lines of each variant (where decls appear)
        def first_n_lines(s, n=8):
            return "\n".join(s.splitlines()[:n])

        decl_blocks = [first_n_lines(s) for s in sources_as_text]
        unique_blocks = set(decl_blocks)
        self.assertGreater(len(unique_blocks), 1,
                           "All variants have identical top-of-body decl blocks — ordering has no effect")

    def test_collect_all_member_nodes_finds_all_occurrences(self):
        """_collect_all_member_nodes must find EVERY occurrence, not just the first."""
        from scripts.permuter.patterns.mwcc_regorder_probe import _collect_all_member_nodes
        # _FOUR_MEMBER_SRC uses each member once in the body; verify counts
        ctx = self._make_ctx(_FOUR_MEMBER_SRC, "Gem_IsSpotlightGem",
                             _diag_callee_saved_gpr())
        nodes = _collect_all_member_nodes(ctx, ["mX", "mY", "mCount", "mScore"])
        # Each member appears exactly once in the body as this->mFoo
        for name in ("mX", "mY", "mCount", "mScore"):
            self.assertGreaterEqual(len(nodes[name]), 1,
                                    f"Expected at least 1 node for {name}, got 0")

    def test_collect_all_member_nodes_repeated_uses(self):
        """Members used multiple times must all be collected."""
        # Source with repeated member access
        src = """\
struct Foo { int mA; int mB; int mC; };
int Foo_Sum(Foo* this) {
    int x = this->mA + this->mA;
    int y = this->mB;
    int z = this->mC + this->mC + this->mC;
    return x + y + z;
}
"""
        from scripts.permuter.patterns.mwcc_regorder_probe import _collect_all_member_nodes
        ctx = self._make_ctx(src, "Foo_Sum", _diag_callee_saved_gpr())
        nodes = _collect_all_member_nodes(ctx, ["mA", "mB", "mC"])
        self.assertEqual(len(nodes["mA"]), 2, "mA appears twice — should collect both")
        self.assertEqual(len(nodes["mB"]), 1, "mB appears once")
        self.assertEqual(len(nodes["mC"]), 3, "mC appears three times — should collect all")

    def test_implicit_this_collect_all_nodes(self):
        """_collect_all_member_nodes works for implicit-this (bare mFoo) references."""
        src = """\
int RB_Check(int idx) {
    if (mActive && mCount > idx) {
        return mCount + (int)(mSpeed);
    }
    return mActive ? 1 : 0;
}
"""
        from scripts.permuter.patterns.mwcc_regorder_probe import _collect_all_member_nodes
        ctx = self._make_ctx(src, "RB_Check", _diag_callee_saved_gpr())
        nodes = _collect_all_member_nodes(ctx, ["mActive", "mCount", "mSpeed"])
        # mActive: 2 uses, mCount: 2 uses, mSpeed: 1 use
        self.assertEqual(len(nodes["mActive"]), 2, "mActive appears twice")
        self.assertEqual(len(nodes["mCount"]), 2, "mCount appears twice")
        self.assertEqual(len(nodes["mSpeed"]), 1, "mSpeed appears once")

    def test_collect_all_nodes_skips_other_object_fields(self):
        """_collect_all_member_nodes must NOT collect `other.mFoo` as a member reference."""
        src = """\
struct Box { int mWidth; int mHeight; int mDepth; };
int Foo_Check(Box* this, Box& other) {
    return this->mWidth + other.mWidth + this->mHeight + other.mHeight + this->mDepth;
}
"""
        from scripts.permuter.patterns.mwcc_regorder_probe import _collect_all_member_nodes
        ctx = self._make_ctx(src, "Foo_Check", _diag_callee_saved_gpr())
        nodes = _collect_all_member_nodes(ctx, ["mWidth", "mHeight", "mDepth"])
        # Only `this->mWidth` and `this->mHeight` and `this->mDepth` should be collected;
        # `other.mWidth` and `other.mHeight` must be excluded
        self.assertEqual(len(nodes["mWidth"]), 1,
                         "Only this->mWidth should be collected, not other.mWidth")
        self.assertEqual(len(nodes["mHeight"]), 1,
                         "Only this->mHeight should be collected, not other.mHeight")
        self.assertEqual(len(nodes["mDepth"]), 1, "this->mDepth should be collected")


if __name__ == "__main__":
    unittest.main()
