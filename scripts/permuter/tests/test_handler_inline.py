"""Tests for the handler_inline pattern.

Verifies:
- named-to-temporary conversion for Message variables
- temporary-to-named conversion
- detection of HANDLE_ACTION macros (call-to-body)
- relevant() for Handle functions (clusters, prologue)
- relevant() for functions with HANDLE macros
- pattern metadata and registration

Usage:
    python -m pytest scripts/permuter/tests/test_handler_inline.py -x -q
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
    make_context,
    normalize,
)
from scripts.permuter.types import Cluster, Diagnosis
from scripts.permuter.patterns.base import get_pattern


# ---------------------------------------------------------------------------
# Diagnosis factories
# ---------------------------------------------------------------------------

def _diag_with_prologue_mismatch() -> Diagnosis:
    """Prologue mismatch (frame size difference from inlining)."""
    d = _empty_diag()
    d.target_gpr_saves = 5
    d.base_gpr_saves = 4
    return d


def _diag_with_clusters_and_prologue() -> Diagnosis:
    """Both clusters and prologue mismatch."""
    d = _empty_diag()
    d.clusters = [Cluster(start_idx=5, end_idx=12, size=7, inserts=4, deletes=3)]
    d.target_gpr_saves = 6
    d.base_gpr_saves = 5
    return d


# ---------------------------------------------------------------------------
# Test sources
# ---------------------------------------------------------------------------

_SOURCE_NAMED_MESSAGE = """\
DataNode Foo::Handle(DataArray* _msg, bool _warn) {
    Symbol sym = _msg->Sym(1);
    Message msg(sym);
    HandleMessage(msg);
    return DataNode(0);
}
"""

_SOURCE_TEMPORARY_MESSAGE = """\
DataNode Foo::Handle(DataArray* _msg, bool _warn) {
    Symbol sym = _msg->Sym(1);
    HandleMessage(Message(sym));
    return DataNode(0);
}
"""

_SOURCE_HANDLE_ACTION = """\
DataNode HamNavProvider::Handle(DataArray* _msg, bool _warn) {
    Symbol sym = _msg->Sym(1);
    HANDLE_ACTION(append_nav_item, AddNavItem())
    return DataNode(0);
}
"""

_SOURCE_HANDLE_ACTION_CLEAR = """\
DataNode HamNavProvider::Handle(DataArray* _msg, bool _warn) {
    Symbol sym = _msg->Sym(1);
    HANDLE_ACTION(clear_items, ClearItem())
    return DataNode(0);
}
"""

_SOURCE_NO_HANDLER = """\
void Foo::Update() {
    int x = 5;
    x += 10;
}
"""

_SOURCE_MULTIPLE_MESSAGES = """\
DataNode Foo::Handle(DataArray* _msg, bool _warn) {
    Symbol sym1 = _msg->Sym(1);
    Message msg1(sym1);
    HandleMessage(msg1);
    Symbol sym2 = _msg->Sym(2);
    Message msg2(sym2);
    HandleMessage(msg2);
    return DataNode(0);
}
"""

_SOURCE_MULTI_USE_MESSAGE = """\
DataNode Foo::Handle(DataArray* _msg, bool _warn) {
    Message msg(_msg->Sym(1));
    HandleMessage(msg);
    OtherCall(msg);
    return DataNode(0);
}
"""

_SOURCE_NAMED_MSG_EXISTING_VAR = """\
DataNode Foo::Handle(DataArray* _msg, bool _warn) {
    Symbol sym = _msg->Sym(1);
    HandleMessage(Message(sym));
    return DataNode(0);
}
"""


# ---------------------------------------------------------------------------
# Relevance tests
# ---------------------------------------------------------------------------

class TestRelevance(unittest.TestCase):

    def setUp(self):
        self.pattern = get_pattern("handler_inline")

    def test_relevant_with_clusters(self):
        """Clusters -> relevant."""
        d = diag_with_clusters()
        self.assertTrue(self.pattern.relevant(d))

    def test_relevant_with_prologue_mismatch(self):
        """Prologue mismatch -> relevant."""
        d = _diag_with_prologue_mismatch()
        self.assertTrue(self.pattern.relevant(d))

    def test_relevant_with_both(self):
        """Both clusters and prologue -> relevant."""
        d = _diag_with_clusters_and_prologue()
        self.assertTrue(self.pattern.relevant(d))

    def test_irrelevant_empty_diag(self):
        """Empty diagnosis -> not relevant."""
        self.assertFalse(self.pattern.relevant(_empty_diag()))


# ---------------------------------------------------------------------------
# Named-to-temporary conversion tests
# ---------------------------------------------------------------------------

class TestNamedToTemporary(unittest.TestCase):

    def setUp(self):
        self.pattern = get_pattern("handler_inline")

    def test_named_message_to_temporary(self):
        """Message msg(sym); HandleMessage(msg); -> HandleMessage(Message(sym));"""
        ctx = make_context(_SOURCE_NAMED_MESSAGE, "Foo::Handle", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))

        found = False
        for v in variants:
            text = v.source.decode("utf-8")
            if "Message(sym)" in text and "Message msg" not in text:
                found = True
                self.assertEqual(v.pattern_name, "handler_inline")
                self.assertIn("temporary", v.description.lower())
                break
        self.assertTrue(found, "No variant converted named Message to temporary")

    def test_multiple_named_messages(self):
        """Two Message declarations -> two named-to-temp variants."""
        ctx = make_context(_SOURCE_MULTIPLE_MESSAGES, "Foo::Handle", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))

        temp_variants = [v for v in variants if "named_to_temp" in v.name]
        self.assertGreaterEqual(len(temp_variants), 2)

    def test_multi_use_message_skipped(self):
        """Message used 2+ times should NOT be converted to temporary."""
        ctx = make_context(_SOURCE_MULTI_USE_MESSAGE, "Foo::Handle", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))

        # Should not find a named-to-temp variant for multi-use msg
        temp_variants = [v for v in variants if "named_to_temp" in v.name]
        self.assertEqual(len(temp_variants), 0)


# ---------------------------------------------------------------------------
# Temporary-to-named conversion tests
# ---------------------------------------------------------------------------

class TestTemporaryToNamed(unittest.TestCase):

    def setUp(self):
        self.pattern = get_pattern("handler_inline")

    def test_temporary_message_to_named(self):
        """HandleMessage(Message(sym)); -> Message msg(sym); HandleMessage(msg);"""
        ctx = make_context(_SOURCE_TEMPORARY_MESSAGE, "Foo::Handle", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))

        found = False
        for v in variants:
            text = v.source.decode("utf-8")
            if "Message msg(sym)" in text or "Message _msg_tmp(sym)" in text:
                found = True
                self.assertEqual(v.pattern_name, "handler_inline")
                self.assertIn("named", v.description.lower())
                break
        self.assertTrue(found, "No variant converted temporary Message to named")


# ---------------------------------------------------------------------------
# HANDLE_ACTION macro tests
# ---------------------------------------------------------------------------

class TestCallToBody(unittest.TestCase):

    def setUp(self):
        self.pattern = get_pattern("handler_inline")

    def test_handle_action_wrapper_inline(self):
        """HANDLE_ACTION(name, AddNavItem()) -> inline body suggestion."""
        ctx = make_context(_SOURCE_HANDLE_ACTION, "HamNavProvider::Handle", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))

        # Should generate at least one call_to_body variant
        body_variants = [v for v in variants if "call_to_body" in v.name]
        self.assertGreater(len(body_variants), 0, "No call_to_body variants generated")

        # Check that the wrapper heuristic produced a push_back expansion
        found_push = False
        for v in body_variants:
            text = v.source.decode("utf-8")
            if "push_back" in text and "NavItem()" in text:
                found_push = True
                break
        self.assertTrue(found_push, "No push_back expansion found for AddNavItem()")

    def test_no_call_to_body_without_handler_macros(self):
        """Non-handler function -> no call_to_body variants."""
        ctx = make_context(_SOURCE_NO_HANDLER, "Foo::Update", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))

        body_variants = [v for v in variants if "call_to_body" in v.name]
        self.assertEqual(len(body_variants), 0)


# ---------------------------------------------------------------------------
# Pattern metadata tests
# ---------------------------------------------------------------------------

class TestMetadata(unittest.TestCase):

    def test_pattern_registered(self):
        """handler_inline should be in the registry."""
        p = get_pattern("handler_inline")
        self.assertIsNotNone(p)
        self.assertEqual(p.name, "handler_inline")

    def test_safety_tier(self):
        """Safety tier should be normal."""
        p = get_pattern("handler_inline")
        self.assertEqual(p.safety_tier, "normal")

    def test_follow_ups(self):
        """follow_ups should include temp_elimination and declaration_reorder."""
        p = get_pattern("handler_inline")
        self.assertIn("temp_elimination", p.follow_ups)
        self.assertIn("declaration_reorder", p.follow_ups)

    def test_not_opt_in(self):
        """handler_inline should not be opt-in."""
        p = get_pattern("handler_inline")
        self.assertFalse(p.opt_in)

    def test_metadata_dict(self):
        """metadata() should return correct structure."""
        p = get_pattern("handler_inline")
        meta = p.metadata()
        self.assertEqual(meta["name"], "handler_inline")
        self.assertEqual(meta["safety_tier"], "normal")
        self.assertIn("temp_elimination", meta["follow_ups"])

    def test_in_composer_follow_up_map(self):
        """handler_inline should have an entry in _FOLLOW_UP_MAP."""
        from scripts.permuter.composer import _FOLLOW_UP_MAP
        self.assertIn("handler_inline", _FOLLOW_UP_MAP)
        follow_ups = _FOLLOW_UP_MAP["handler_inline"]
        self.assertIn("temp_elimination", follow_ups)
        self.assertIn("declaration_reorder", follow_ups)


# ---------------------------------------------------------------------------
# Edge case tests
# ---------------------------------------------------------------------------

class TestEdgeCases(unittest.TestCase):

    def setUp(self):
        self.pattern = get_pattern("handler_inline")

    def test_no_variants_for_non_message_func(self):
        """Non-handler function without Message vars -> no handler_inline variants."""
        ctx = make_context(_SOURCE_NO_HANDLER, "Foo::Update", _empty_diag())
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(len(variants), 0)

    def test_variant_names_unique(self):
        """All variant names should be unique."""
        ctx = make_context(_SOURCE_NAMED_MESSAGE, "Foo::Handle", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        if variants:
            names = [v.name for v in variants]
            self.assertEqual(len(names), len(set(names)), f"Duplicate names: {names}")

    def test_all_variants_have_pattern_name(self):
        """All variants should carry the correct pattern name."""
        ctx = make_context(_SOURCE_NAMED_MESSAGE, "Foo::Handle", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        for v in variants:
            self.assertEqual(v.pattern_name, "handler_inline")

    def test_variant_source_differs_from_original(self):
        """Every variant's source should differ from the input."""
        ctx = make_context(_SOURCE_NAMED_MESSAGE, "Foo::Handle", diag_with_clusters())
        variants = list(self.pattern.generate(ctx))
        for v in variants:
            self.assertNotEqual(v.source, ctx.file_source)


if __name__ == "__main__":
    unittest.main()
