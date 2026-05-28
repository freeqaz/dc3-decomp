"""Tests for the corpus_audit pattern stress-test harness."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.corpus_audit import (
    _count_struct_refs,
    _pod_remove_is_unsafe,
    audit_patterns,
    collect_cpp_files,
    count_errors,
)


class TestTreeSitterHelpers(unittest.TestCase):
    def test_count_errors_clean_source(self):
        self.assertEqual(count_errors(b"int f() { return 1; }\n"), 0)

    def test_count_errors_malformed_source(self):
        # An unterminated function body produces at least one ERROR/MISSING node.
        self.assertGreater(count_errors(b"int f() { return 1; "), 0)


class TestFileCollection(unittest.TestCase):
    def test_collect_cpp_files(self):
        import tempfile

        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "a.cpp").write_text("int a(){return 0;}\n")
            (root / "sub").mkdir()
            (root / "sub" / "b.cpp").write_text("int b(){return 0;}\n")
            (root / "c.h").write_text("int c();\n")  # not a .cpp
            files = collect_cpp_files([root])
            names = sorted(f.name for f in files)
            self.assertEqual(names, ["a.cpp", "b.cpp"])


class TestPodHelpers(unittest.TestCase):
    SRC = b"""
struct Edge {
    Edge();
    Edge(unsigned short, unsigned short);
    unsigned short a;
    unsigned short b;
};
void uses_default() { Edge e; e.a = 1; }
void constructs_with_args() { v.push_back(Edge(1, 2)); }
void unrelated() { int x = 0; (void)x; }
"""

    def test_pod_remove_unsafe_detects_arg_construction(self):
        self.assertTrue(_pod_remove_is_unsafe(self.SRC, "Edge"))

    def test_pod_remove_safe_when_only_default_constructed(self):
        src = b"""
struct Edge { Edge(); unsigned short a; unsigned short b; };
void only_default() { Edge e; e.a = 1; }
"""
        self.assertFalse(_pod_remove_is_unsafe(src, "Edge"))

    def test_count_struct_refs_counts_value_and_type_uses(self):
        # uses_default (value decl) + constructs_with_args (functional cast) =>
        # 2 referencing functions; `unrelated` does not reference Edge.
        self.assertEqual(_count_struct_refs(self.SRC, "Edge"), 2)


class TestAuditSmoke(unittest.TestCase):
    def test_audit_runs_and_reports_zero_parse_errors(self):
        import tempfile

        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            # A value-used POD struct: pod_ctor_toggle add-direction should fire,
            # and never introduce a parse error.
            (root / "rec.cpp").write_text(
                "struct Rec { int slot; float t; };\n"
                "std::vector<Rec> g;\n"
                "void f() { Rec r; r.slot = 0; g.push_back(r); }\n"
            )
            files = collect_cpp_files([root])
            audits = audit_patterns(files, ["pod_ctor_toggle"])
            a = audits["pod_ctor_toggle"]
            self.assertGreater(a.variants, 0, "expected pod_ctor_toggle to fire")
            self.assertEqual(a.new_parse_errors, 0)
            self.assertEqual(a.gate_leaks, 0)


if __name__ == "__main__":
    unittest.main()
