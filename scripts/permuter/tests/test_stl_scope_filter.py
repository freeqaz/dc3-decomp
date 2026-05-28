"""Regression tests for the STL-scope filter in batch_auto.get_workable_candidates.

The bug: the old SQL clause `demangled NOT LIKE '%stlpmtx_std::%'` excluded
user functions whose *parameter types* mention STL types, not just STL internals.
E.g. ChordShapeGenerator::BuildSpan takes a stlpmtx_std::map argument and was
wrongly excluded.

The fix: filter only when the function's *own* scope (everything before the
first '(' arg-list opener) starts with an STL namespace prefix.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.batch_auto import _STL_NAMESPACES


def _is_stl_internal(demangled: str) -> bool:
    """Mirror the filter logic in batch_auto.get_workable_candidates."""
    paren = demangled.find("(")
    scope = demangled[:paren] if paren != -1 else demangled
    return any(scope.startswith(ns) for ns in _STL_NAMESPACES)


class StlScopeFilterTests(unittest.TestCase):
    # --- user functions that take STL params: must NOT be filtered out ---

    def test_chord_shape_generator_build_span(self):
        demangled = (
            "ChordShapeGenerator::BuildSpan("
            "RndMesh*, stlpmtx_std::map<unsigned short, unsigned short, "
            "stlpmtx_std::less<unsigned short>, "
            "stlpmtx_std::StlNodeAlloc<stlpmtx_std::pair<const unsigned short, unsigned short>>>&, "
            "int, int, const Transform&, const Transform&, Hmx::Color32, Hmx::Color32)"
        )
        self.assertFalse(
            _is_stl_internal(demangled),
            "BuildSpan takes an STL map arg but is NOT an STL internal",
        )

    def test_chord_shape_generator_build_end_cap(self):
        demangled = (
            "ChordShapeGenerator::BuildEndCap("
            "RndMesh*, stlpmtx_std::map<unsigned short, unsigned short, "
            "stlpmtx_std::less<unsigned short>, "
            "stlpmtx_std::StlNodeAlloc<stlpmtx_std::pair<const unsigned short, unsigned short>>>&, "
            "int, const Transform&, Symbol, Hmx::Color32)"
        )
        self.assertFalse(_is_stl_internal(demangled), "BuildEndCap is NOT an STL internal")

    def test_chord_shape_generator_extend_profile(self):
        demangled = (
            "ChordShapeGenerator::ExtendProfile("
            "RndMesh*, stlpmtx_std::map<unsigned short, unsigned short, "
            "stlpmtx_std::less<unsigned short>, "
            "stlpmtx_std::StlNodeAlloc<stlpmtx_std::pair<const unsigned short, unsigned short>>>&, "
            "const Transform&, const Transform&, float, float, "
            "const ChordShapeGenerator::CrossSec&, Hmx::Color32, Hmx::Color32)"
        )
        self.assertFalse(_is_stl_internal(demangled), "ExtendProfile is NOT an STL internal")

    def test_user_function_stl_return_type(self):
        # A user class method that returns an STL type — also must not be filtered.
        demangled = "SomeClass::GetItems(std::vector<int>&)"
        self.assertFalse(_is_stl_internal(demangled))

    # --- true STL internals: MUST be filtered out ---

    def test_stlpmtx_destructor(self):
        demangled = "stlpmtx_std::StlNodeAlloc<int>::~StlNodeAlloc()"
        self.assertTrue(_is_stl_internal(demangled))

    def test_stlpmtx_vector_destructor(self):
        demangled = (
            "stlpmtx_std::vector<int, unsigned short, "
            "stlpmtx_std::StlNodeAlloc<int>>::~vector()"
        )
        self.assertTrue(_is_stl_internal(demangled))

    def test_stlpmtx_list_destructor(self):
        demangled = (
            "stlpmtx_std::list<String, stlpmtx_std::StlNodeAlloc<String>>::~list()"
        )
        self.assertTrue(_is_stl_internal(demangled))

    def test_stlpmtx_ok_to_memcpy(self):
        demangled = "stlpmtx_std::_OKToMemCpy<float, float>::_Answer()"
        self.assertTrue(_is_stl_internal(demangled))

    def test_std_operator(self):
        demangled = "std::operator>><Hmx::Object>(int)"
        self.assertTrue(_is_stl_internal(demangled))

    def test_std_vector_push_back(self):
        demangled = "std::vector<int>::push_back(const int&)"
        self.assertTrue(_is_stl_internal(demangled))


if __name__ == "__main__":
    unittest.main()
