"""Tests for the pod_ctor_toggle pattern.

Mirrors the structure of test_accessor_outline.py: diagnosis factories, variant
generation (add + remove directions), the relevant() gate, the safety guard
(don't remove a ctor that's constructed with args), and pattern metadata.
"""

from __future__ import annotations

import unittest

from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.tests.conftest import (
    _empty_diag,
    make_context,
    normalize,
)
from scripts.permuter.types import DiffOp, Diagnosis


# ---------------------------------------------------------------------------
# Diagnosis factories specific to pod_ctor_toggle
# ---------------------------------------------------------------------------

def _diag_typed_target() -> Diagnosis:
    """Base emits word copy (stw), target emits typed float copy (stfs).

    -> we must become NON-POD -> ADD ctor direction.
    """
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=10, target_opcode="stfs", base_opcode="stw")]
    return d


def _diag_word_target() -> Diagnosis:
    """Base emits typed u16 copy (sth), target emits word copy (stw).

    -> we must become POD -> REMOVE ctor direction.
    """
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=10, target_opcode="stw", base_opcode="sth")]
    return d


def _diag_mixed() -> Diagnosis:
    """No clear polarity — both directions allowed."""
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=10, target_opcode="lwz", base_opcode="lhz")]
    return d


# ---------------------------------------------------------------------------
# Add-ctor direction
# ---------------------------------------------------------------------------

class TestAddCtorDirection(unittest.TestCase):
    """A POD struct should gain an empty ctor to force typed member copies."""

    def test_add_ctor_on_pod_struct(self):
        pattern = get_pattern("pod_ctor_toggle")
        ctx = make_context(
            """\
struct UpcomingFretRelease {
    int slot;
    float time;
};
void test_func() {
    UpcomingFretRelease r;
    r.slot = 0;
}
""",
            "test_func",
            _diag_typed_target(),
        )
        variants = list(pattern.generate(ctx))
        self.assertTrue(variants, "Expected an add-ctor variant for the POD struct")

        added = [v for v in variants if b"pod_ctor_add" in v.name.encode()]
        self.assertTrue(added, "Expected a pod_ctor_add variant")
        src = added[0].source.decode("utf-8")
        # The empty ctor should be inserted into the struct body.
        self.assertIn("UpcomingFretRelease() {}", normalize(src))
        # The data members must be preserved.
        self.assertIn("int slot;", src)
        self.assertIn("float time;", src)

    def test_add_ctor_skipped_when_struct_already_has_ctor(self):
        """A struct that already has a user ctor is not an add-direction target."""
        pattern = get_pattern("pod_ctor_toggle")
        ctx = make_context(
            """\
struct WithCtor {
    WithCtor() {}
    int slot;
    float time;
};
void test_func() {
    WithCtor r;
    r.slot = 0;
}
""",
            "test_func",
            _diag_typed_target(),
        )
        variants = list(pattern.generate(ctx))
        added = [v for v in variants if b"pod_ctor_add" in v.name.encode()]
        self.assertEqual(added, [], "Should not add a second ctor")

    def test_add_skipped_for_remove_polarity(self):
        """If the asm asks for word copies (remove polarity), a POD struct with
        no ctor produces no add variant."""
        pattern = get_pattern("pod_ctor_toggle")
        ctx = make_context(
            """\
struct PodOnly {
    int slot;
    float time;
};
void test_func() {
    PodOnly r;
    r.slot = 0;
}
""",
            "test_func",
            _diag_word_target(),  # remove polarity
        )
        variants = list(pattern.generate(ctx))
        self.assertEqual(
            variants, [], "Remove-polarity diagnosis should not add a ctor"
        )


# ---------------------------------------------------------------------------
# Remove-ctor direction
# ---------------------------------------------------------------------------

class TestRemoveCtorDirection(unittest.TestCase):
    """A struct with dead ctors should drop them to become POD (word copies)."""

    def test_remove_dead_ctors(self):
        pattern = get_pattern("pod_ctor_toggle")
        ctx = make_context(
            """\
struct Edge {
    Edge() {}
    Edge(unsigned short, unsigned short);
    unsigned short a;
    unsigned short b;
};
void test_func() {
    Edge e;
    e.a = 1;
    e.b = 2;
}
""",
            "test_func",
            _diag_word_target(),
        )
        variants = list(pattern.generate(ctx))
        removed = [v for v in variants if b"pod_ctor_remove" in v.name.encode()]
        self.assertTrue(removed, "Expected a pod_ctor_remove variant")

        src = removed[0].source.decode("utf-8")
        # Both ctor declarations are gone.
        self.assertNotIn("Edge()", src)
        self.assertNotIn("Edge(unsigned short", src)
        # The data members remain.
        self.assertIn("unsigned short a;", src)
        self.assertIn("unsigned short b;", src)

    def test_remove_skipped_for_add_polarity(self):
        """If the asm asks for typed copies (add polarity), a dead-ctor struct
        produces no remove variant."""
        pattern = get_pattern("pod_ctor_toggle")
        ctx = make_context(
            """\
struct Edge {
    Edge() {}
    Edge(unsigned short, unsigned short);
    unsigned short a;
    unsigned short b;
};
void test_func() {
    Edge e;
    e.a = 1;
}
""",
            "test_func",
            _diag_typed_target(),  # add polarity
        )
        variants = list(pattern.generate(ctx))
        self.assertEqual(
            variants, [], "Add-polarity diagnosis should not remove ctors"
        )

    def test_remove_skipped_for_nonempty_inline_ctor(self):
        """A ctor with a non-empty body is not 'dead' — don't remove it."""
        pattern = get_pattern("pod_ctor_toggle")
        ctx = make_context(
            """\
struct Edge {
    Edge() { a = 0; b = 0; }
    unsigned short a;
    unsigned short b;
};
void test_func() {
    Edge e;
    e.a = 1;
}
""",
            "test_func",
            _diag_word_target(),
        )
        variants = list(pattern.generate(ctx))
        removed = [v for v in variants if b"pod_ctor_remove" in v.name.encode()]
        self.assertEqual(
            removed, [], "Non-empty ctor body is not dead; should not remove"
        )

    def test_remove_skipped_when_field_initializer_list_present(self):
        """A ctor with a member-initializer list is not trivially dead."""
        pattern = get_pattern("pod_ctor_toggle")
        ctx = make_context(
            """\
struct Edge {
    Edge() : a(0), b(0) {}
    unsigned short a;
    unsigned short b;
};
void test_func() {
    Edge e;
    e.a = 1;
}
""",
            "test_func",
            _diag_word_target(),
        )
        variants = list(pattern.generate(ctx))
        removed = [v for v in variants if b"pod_ctor_remove" in v.name.encode()]
        self.assertEqual(
            removed, [], "Init-list ctor is not dead; should not remove"
        )


# ---------------------------------------------------------------------------
# Safety guard: arg-construction call sites
# ---------------------------------------------------------------------------

class TestArgConstructionGuard(unittest.TestCase):
    """Don't remove a ctor that's actually constructed with arguments."""

    def test_guard_direct_init_declaration(self):
        """`Edge e(1, 2);` direct-init means the arg ctor is live."""
        pattern = get_pattern("pod_ctor_toggle")
        ctx = make_context(
            """\
struct Edge {
    Edge() {}
    Edge(unsigned short, unsigned short);
    unsigned short a;
    unsigned short b;
};
void test_func() {
    Edge e(1, 2);
    e.a = 3;
}
""",
            "test_func",
            _diag_word_target(),
        )
        variants = list(pattern.generate(ctx))
        removed = [v for v in variants if b"pod_ctor_remove" in v.name.encode()]
        self.assertEqual(
            removed, [], "Direct-init construction must block ctor removal"
        )

    def test_guard_functional_cast_construction(self):
        """`push_back(Edge(1, 2))` functional-cast means the arg ctor is live."""
        pattern = get_pattern("pod_ctor_toggle")
        ctx = make_context(
            """\
struct Edge {
    Edge() {}
    Edge(unsigned short, unsigned short);
    unsigned short a;
    unsigned short b;
};
void test_func() {
    v.push_back(Edge(1, 2));
}
""",
            "test_func",
            _diag_word_target(),
        )
        variants = list(pattern.generate(ctx))
        removed = [v for v in variants if b"pod_ctor_remove" in v.name.encode()]
        self.assertEqual(
            removed, [], "Functional-cast construction must block ctor removal"
        )

    def test_guard_allows_default_construction(self):
        """`Edge e;` default construction does NOT block removal."""
        pattern = get_pattern("pod_ctor_toggle")
        ctx = make_context(
            """\
struct Edge {
    Edge() {}
    Edge(unsigned short, unsigned short);
    unsigned short a;
    unsigned short b;
};
void test_func() {
    Edge e;
    e.a = 1;
    e.b = 2;
}
""",
            "test_func",
            _diag_word_target(),
        )
        variants = list(pattern.generate(ctx))
        removed = [v for v in variants if b"pod_ctor_remove" in v.name.encode()]
        self.assertTrue(
            removed, "Default construction should not block ctor removal"
        )

    def test_guard_out_of_line_definition(self):
        """An out-of-line `Edge::Edge(...)` definition blocks removal (would
        orphan the definition)."""
        pattern = get_pattern("pod_ctor_toggle")
        ctx = make_context(
            """\
struct Edge {
    Edge();
    unsigned short a;
    unsigned short b;
};
Edge::Edge() { a = 0; }
void test_func() {
    Edge e;
    e.a = 1;
}
""",
            "test_func",
            _diag_word_target(),
        )
        variants = list(pattern.generate(ctx))
        removed = [v for v in variants if b"pod_ctor_remove" in v.name.encode()]
        self.assertEqual(
            removed, [], "Out-of-line ctor definition must block removal"
        )


# ---------------------------------------------------------------------------
# Non-POD member rejection / size gate
# ---------------------------------------------------------------------------

class TestStructFiltering(unittest.TestCase):
    """Only small, structurally-POD-ish structs are candidates."""

    def test_rejects_by_value_class_member(self):
        pattern = get_pattern("pod_ctor_toggle")
        ctx = make_context(
            """\
struct Bad {
    int a;
    SomeClass c;
};
void test_func() {
    Bad b;
    b.a = 1;
}
""",
            "test_func",
            _diag_typed_target(),
        )
        variants = list(pattern.generate(ctx))
        self.assertEqual(
            variants, [], "By-value class member must disqualify the struct"
        )

    def test_rejects_template_member(self):
        pattern = get_pattern("pod_ctor_toggle")
        ctx = make_context(
            """\
struct Bad {
    int a;
    std::vector<int> v;
};
void test_func() {
    Bad b;
    b.a = 1;
}
""",
            "test_func",
            _diag_typed_target(),
        )
        variants = list(pattern.generate(ctx))
        self.assertEqual(
            variants, [], "Template-typed member must disqualify the struct"
        )

    def test_rejects_oversized_struct(self):
        pattern = get_pattern("pod_ctor_toggle")
        ctx = make_context(
            """\
struct Big {
    int a, b, c, d, e, f;
};
void test_func() {
    Big x;
    x.a = 1;
}
""",
            "test_func",
            _diag_typed_target(),
        )
        variants = list(pattern.generate(ctx))
        self.assertEqual(
            variants, [], "Structs over the byte threshold must be skipped"
        )

    def test_accepts_pointer_and_small_array(self):
        pattern = get_pattern("pod_ctor_toggle")
        ctx = make_context(
            """\
struct PA {
    int* ptr;
    char buf[8];
};
void test_func() {
    PA x;
    x.ptr = 0;
}
""",
            "test_func",
            _diag_typed_target(),
        )
        variants = list(pattern.generate(ctx))
        added = [v for v in variants if b"pod_ctor_add" in v.name.encode()]
        self.assertTrue(
            added, "Pointer + small fixed array members are POD-ish; expected add"
        )

    def test_rejects_struct_with_inheritance(self):
        pattern = get_pattern("pod_ctor_toggle")
        ctx = make_context(
            """\
struct Derived : public Base {
    int a;
    float b;
};
void test_func() {
    Derived d;
    d.a = 1;
}
""",
            "test_func",
            _diag_typed_target(),
        )
        variants = list(pattern.generate(ctx))
        self.assertEqual(
            variants, [], "A struct with a base class must be skipped"
        )

    def test_rejects_struct_with_virtual_method(self):
        pattern = get_pattern("pod_ctor_toggle")
        ctx = make_context(
            """\
struct V {
    virtual void f();
    int a;
    float b;
};
void test_func() {
    V v;
    v.a = 1;
}
""",
            "test_func",
            _diag_typed_target(),
        )
        variants = list(pattern.generate(ctx))
        self.assertEqual(
            variants, [], "A struct with a virtual method must be skipped"
        )


# ---------------------------------------------------------------------------
# relevant() gate
# ---------------------------------------------------------------------------

class TestRelevance(unittest.TestCase):
    """relevant() gates on word-op vs typed-op divergence (either polarity)."""

    def test_relevant_typed_target(self):
        pattern = get_pattern("pod_ctor_toggle")
        self.assertTrue(pattern.relevant(_diag_typed_target()))

    def test_relevant_word_target(self):
        pattern = get_pattern("pod_ctor_toggle")
        self.assertTrue(pattern.relevant(_diag_word_target()))

    def test_relevant_mixed(self):
        pattern = get_pattern("pod_ctor_toggle")
        self.assertTrue(pattern.relevant(_diag_mixed()))

    def test_not_relevant_empty(self):
        pattern = get_pattern("pod_ctor_toggle")
        self.assertFalse(pattern.relevant(_empty_diag()))

    def test_not_relevant_unrelated_ops(self):
        pattern = get_pattern("pod_ctor_toggle")
        d = _empty_diag()
        d.diff_ops = [DiffOp(index=1, target_opcode="add", base_opcode="sub")]
        self.assertFalse(pattern.relevant(d))

    def test_not_relevant_two_word_ops(self):
        """Two word ops (no typed op) is not a POD-divergence signal."""
        pattern = get_pattern("pod_ctor_toggle")
        d = _empty_diag()
        d.diff_ops = [DiffOp(index=1, target_opcode="lwz", base_opcode="stw")]
        self.assertFalse(pattern.relevant(d))

    def test_priority_relevant(self):
        pattern = get_pattern("pod_ctor_toggle")
        self.assertGreater(pattern.priority(_diag_typed_target()), 0.0)

    def test_priority_zero_when_irrelevant(self):
        pattern = get_pattern("pod_ctor_toggle")
        self.assertEqual(pattern.priority(_empty_diag()), 0.0)


# ---------------------------------------------------------------------------
# Pattern metadata and registration
# ---------------------------------------------------------------------------

class TestPatternMetadata(unittest.TestCase):
    def test_pattern_registered(self):
        pattern = get_pattern("pod_ctor_toggle")
        self.assertEqual(pattern.name, "pod_ctor_toggle")

    def test_is_opt_in(self):
        """Remove-direction is dangerous; the whole pattern is opt-in."""
        pattern = get_pattern("pod_ctor_toggle")
        self.assertTrue(pattern.opt_in)

    def test_safety_tier(self):
        pattern = get_pattern("pod_ctor_toggle")
        self.assertEqual(pattern.safety_tier, "experimental")

    def test_structural_domain(self):
        pattern = get_pattern("pod_ctor_toggle")
        self.assertEqual(pattern.structural_domain, "cross_unit")

    def test_in_pattern_registry(self):
        from scripts.permuter.patterns.base import list_patterns
        all_patterns = list_patterns(include_opt_in=True)
        self.assertIn("pod_ctor_toggle", all_patterns)

    def test_excluded_from_default_patterns(self):
        """opt_in patterns are excluded from the default (non-opt-in) list."""
        from scripts.permuter.patterns.base import list_patterns
        default = list_patterns(include_opt_in=False)
        self.assertNotIn("pod_ctor_toggle", default)


if __name__ == "__main__":
    unittest.main()
