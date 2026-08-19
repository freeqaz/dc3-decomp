#!/usr/bin/env python3
"""Unit tests for the unicorn evidence-refresh tooling (wave-3 lane B).

Covers the deterministic logic — source-hash gate + flip-cause adjudicator +
the source_hash COFF fingerprint on a real frontier .obj. Does NOT run the
emulator (that's exercised by the live sweep).

Run (unittest or direct — NOT pytest; see below):
    python3 -m unittest scripts.unicorn.test_refresh -v
    python3 scripts/unicorn/test_refresh.py

scripts/test_tools.py runs this file in its script-mode arm, which is what
gives it a caller.

Why not pytest, precisely
-------------------------
Under pytest's ``prepend`` import mode the basedir for this file is ``scripts/``
(the first ancestor without an ``__init__.py``), so pytest names the module
``unicorn.test_refresh`` — inside a package called ``unicorn``, which is also
the name of the third-party emulator bindings. Once anything has imported the
real bindings, ``sys.modules['unicorn']`` is the emulator and pytest's import
fails with ``ModuleNotFoundError: No module named 'unicorn.test_refresh'``.

That is a *module-naming* collision, and unlike the ``sys.path``-ordering
collision that ``unicorn_dep`` fixes, ordering cannot help: whichever package
wins the name, the other one is unreachable. The two real fixes are renaming
this package or adding ``scripts/__init__.py`` (which would move the basedir to
the repo root and make the module ``scripts.unicorn.test_refresh``). Both change
import semantics for every test tree under ``scripts/``, so they are deliberately
left alone here and the file is run as a script instead.

What DID change: the hardcoded ``/home/free/code/milohax/unicorn/bindings/python``
on the old line 22 is gone. It violated the no-machine-paths rule and was simply
wrong on any other box or in any worktree; the location is now resolved by
``scripts/unicorn_runner/unicorn_dep.py``.
"""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(os.path.dirname(HERE))
# PROJECT_ROOT first for the scripts.* package imports below...
sys.path.insert(0, PROJECT_ROOT)
# ...then the unicorn bindings, inserted at position 0 by the resolver so
# `import unicorn` cannot resolve to this package.
from scripts.unicorn_runner.unicorn_dep import ensure_unicorn_on_path  # noqa: E402

ensure_unicorn_on_path()

from scripts.unicorn.refresh_frontier import (
    classify_flip, classify_flip_cause, is_authorable_sql, SCOPES,
)
from scripts.unicorn.source_hash import function_source_hash
from scripts.unicorn_runner.coff import COFFParser


class TestClassifyFlip(unittest.TestCase):
    def test_new_when_no_prior(self):
        self.assertEqual(classify_flip(None, "EQUIVALENT"), "new")
        self.assertEqual(classify_flip("", "DIVERGENT"), "new")

    def test_stable_when_unchanged(self):
        self.assertEqual(classify_flip("EQUIVALENT", "EQUIVALENT"), "stable")

    def test_transition(self):
        self.assertEqual(
            classify_flip("EQUIVALENT", "DIVERGENT"), "EQUIVALENT->DIVERGENT")
        self.assertEqual(
            classify_flip("DIVERGENT", "EQUIVALENT"), "DIVERGENT->EQUIVALENT")


class TestFlipCause(unittest.TestCase):
    def test_signal_version_cap_exhausted(self):
        # The v2 cap-exhaustion rule: prior EQUIV now DIVERGENT(cap) = NOT a bug.
        self.assertEqual(
            classify_flip_cause("EQUIVALENT", "DIVERGENT", "cap_exhausted",
                                "cap_exhausted_both"),
            "signal_version")

    def test_signal_version_wild_jump(self):
        self.assertEqual(
            classify_flip_cause("EQUIVALENT", "DIVERGENT", "wild_jump_match",
                                "wild_jump_match"),
            "signal_version")

    def test_artifact_class_is_floor(self):
        for cls in ("build_env", "regalloc", "stack_layout", "merged_call",
                    "merged_arg", "fpr_precision", "orig_error"):
            self.assertEqual(
                classify_flip_cause("EQUIVALENT", "DIVERGENT", cls, "x"),
                "artifact", msg=cls)

    def test_candidate_bug_real_classes(self):
        for cls in ("logic", "error", "call_arg", "object_memory",
                    "return_value", "call_count", "unmapped_access_mismatch"):
            self.assertEqual(
                classify_flip_cause("EQUIVALENT", "DIVERGENT", cls, "x"),
                "candidate_bug", msg=cls)

    def test_one_sided_cap_is_candidate_bug(self):
        # One side loops where the other terminates = a real divergence (or
        # fixture artifact), but NOT the symmetric v2 cap-exhaustion case.
        self.assertEqual(
            classify_flip_cause("EQUIVALENT", "DIVERGENT", "cap_exhausted_decomp",
                                "cap_exhausted_decomp"),
            "candidate_bug")

    def test_recovered(self):
        self.assertEqual(
            classify_flip_cause("DIVERGENT", "EQUIVALENT", None, None),
            "recovered")

    def test_other_for_non_eq_div_transitions(self):
        self.assertEqual(
            classify_flip_cause("EQUIVALENT", "SKIPPED", None, None), "other")
        self.assertEqual(
            classify_flip_cause(None, "EQUIVALENT", None, None), "other")


class TestSourceHash(unittest.TestCase):
    OBJ = os.path.join(
        PROJECT_ROOT, "build", "373307D9", "src", "system", "gesture",
        "Skeleton.obj")

    @unittest.skipUnless(os.path.exists(OBJ), "Skeleton.obj not built")
    def test_hash_is_deterministic_and_keyed(self):
        coff = COFFParser(self.OBJ)
        sym = "?ElapsedMs@Skeleton@@UBAHXZ"
        h1 = function_source_hash(coff, sym)
        h2 = function_source_hash(coff, sym)
        self.assertIsNotNone(h1)
        self.assertEqual(len(h1), 16)
        self.assertEqual(h1, h2, "same obj must hash identically")
        # Different function => different hash.
        h_other = function_source_hash(coff, "?Init@Skeleton@@QAAXXZ")
        self.assertNotEqual(h1, h_other)

    @unittest.skipUnless(os.path.exists(OBJ), "Skeleton.obj not built")
    def test_unknown_symbol_returns_none(self):
        coff = COFFParser(self.OBJ)
        self.assertIsNone(function_source_hash(coff, "?NoSuchSymbol@@QAAXXZ"))


class TestScope(unittest.TestCase):
    """scope='all' must actually reach the rows scope='frontier' cannot.

    The whole reason ~26k rows kept 2026-03 verdicts from the pre-defect-fix
    harness is that every refresh only ever swept 0<match%<100. A regression
    here silently reintroduces that.
    """

    def _rows(self, scope):
        import sqlite3
        conn = sqlite3.connect(":memory:")
        conn.execute(
            "CREATE TABLE functions (symbol TEXT, unit TEXT, excluded INT, "
            "is_stub INT, match_percent_normalized REAL, unicorn_verdict TEXT)")
        conn.executemany(
            "INSERT INTO functions VALUES (?,?,?,?,?,?)",
            [
                ("?Partial@@QAAXXZ", "default/system/obj/Dir", 0, 0, 85.0, None),
                ("?Complete@@QAAXXZ", "default/system/obj/Dir", 0, 0, 100.0, None),
                ("?Zero@@QAAXXZ", "default/system/obj/Dir", 0, 0, 0.0, None),
                # carries a stale verdict but is otherwise out of scope
                ("?Excluded@@QAAXXZ", "default/system/obj/Dir", 1, 0, 100.0, "EQUIVALENT"),
                ("?Sdk@@QAAXXZ", "default/xdk/foo", 0, 0, 90.0, None),
                ("merged_82331360", "default/system/obj/Dir", 0, 0, 90.0, None),
                ("?Stub@@QAAXXZ", "default/system/obj/Dir", 0, 1, 90.0, None),
            ],
        )
        return {r[0] for r in conn.execute(
            "SELECT symbol FROM functions WHERE " + is_authorable_sql(scope))}

    def test_frontier_is_partial_only(self):
        self.assertEqual(self._rows("frontier"), {"?Partial@@QAAXXZ"})

    def test_all_reaches_complete_and_zero_and_stale(self):
        got = self._rows("all")
        self.assertIn("?Complete@@QAAXXZ", got)   # the ~26k-row blind spot
        self.assertIn("?Zero@@QAAXXZ", got)
        self.assertIn("?Partial@@QAAXXZ", got)
        # excluded, but carries a verdict a re-ingest has to be able to correct
        self.assertIn("?Excluded@@QAAXXZ", got)
        # still no SDK / artifact-prefix / stub rows without a prior verdict
        self.assertNotIn("?Sdk@@QAAXXZ", got)
        self.assertNotIn("merged_82331360", got)
        self.assertNotIn("?Stub@@QAAXXZ", got)

    def test_unknown_scope_rejected(self):
        with self.assertRaises(ValueError):
            is_authorable_sql("everything")
        self.assertEqual(SCOPES, ("frontier", "all"))


class TestHarnessVersion(unittest.TestCase):
    """HARNESS_VERSION must be independent of SIGNAL_VERSION and must be >= 4.

    h4 is the first version with the register-save-helper fix (merge 0871d63df).
    Anything below that measured a machine where 87.5% of functions had r3
    zeroed in their prologue.
    """

    def test_harness_version_is_at_least_the_helper_fix(self):
        from scripts.unicorn_runner.signal_version import (
            HARNESS_VERSION, SIGNAL_VERSION,
        )
        self.assertGreaterEqual(HARNESS_VERSION, 4)
        self.assertIsInstance(HARNESS_VERSION, int)
        # They are separate axes: do not let a refactor alias one to the other.
        self.assertIsNot(HARNESS_VERSION, SIGNAL_VERSION)


if __name__ == "__main__":
    unittest.main()
