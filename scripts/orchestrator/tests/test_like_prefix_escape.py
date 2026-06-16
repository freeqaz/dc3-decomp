"""Regression tests for the SQL-LIKE-over-symbol escaping fix (wave-10 Lane C).

SQL LIKE treats '_' as a single-char wildcard and '%' as a multi-char wildcard.
Artifact/boilerplate exclusion prefixes (merged_, fn_, ??__F, ??_E, ...) all
contain literal underscores, so an unescaped ``symbol NOT LIKE 'merged_%'`` or
``'??__E%'`` over-matches real authorable symbols and silently undercounts the
frontier. This is the wave-9 measurement bug (hid 6,835 fns from band queries).

These tests pin the escaping helper and prove, against a tiny in-memory DB, that
the escaped boilerplate exclusion no longer over-excludes operator overloads.
No real DB is touched.
"""

import sqlite3
import unittest

from scripts.orchestrator.database import (
    like_prefix_clause,
    BOILERPLATE_SYMBOL_PREFIXES,
    _EXCLUDE_MERGED,
    _EXCLUDE_FN,
    _EXCLUDE_STLPMTX,
)


class TestLikePrefixClause(unittest.TestCase):
    def test_underscore_is_escaped(self):
        clause = like_prefix_clause("symbol", "merged_")
        self.assertEqual(clause, r"symbol NOT LIKE 'merged\_%' ESCAPE '\'")

    def test_qq_underscore_is_escaped(self):
        # The exact wave-9 footgun: '??_' must escape the literal underscore so
        # it does NOT match '??0' (ctor), '??1' (dtor), '??4' (operator=) etc.
        clause = like_prefix_clause("symbol", "??_")
        self.assertIn(r"'??\_%'", clause)
        self.assertIn("ESCAPE", clause)

    def test_positive_form(self):
        clause = like_prefix_clause("symbol", "fn_", negate=False)
        self.assertEqual(clause, r"symbol LIKE 'fn\_%' ESCAPE '\'")

    def test_alias_column(self):
        clause = like_prefix_clause("f.symbol", "merged_")
        self.assertTrue(clause.startswith("f.symbol NOT LIKE"))

    def test_module_constants_escaped(self):
        self.assertIn(r"merged\_%", _EXCLUDE_MERGED)
        self.assertIn(r"fn\_%", _EXCLUDE_FN)
        self.assertIn(r"stlpmtx\_std::", _EXCLUDE_STLPMTX)


class TestBoilerplateExclusionNoOverMatch(unittest.TestCase):
    """Prove the escaped boilerplate exclusion keeps real operator overloads
    while still dropping the genuine atexit/init/thunk boilerplate."""

    def setUp(self):
        self.conn = sqlite3.connect(":memory:")
        self.conn.execute("CREATE TABLE functions (symbol TEXT)")
        self.rows = [
            # genuine boilerplate that MUST be excluded
            ("??__Ekfoobar@@YAXXZ",),          # dynamic initializer
            ("??__Fkbaz@@YAXXZ",),             # atexit destructor
            ("??_GFoo@@UAEPAXI@Z",),           # scalar deleting dtor
            ("??_EFoo@@UAEPAXI@Z",),           # vector deleting dtor
            # real authorable symbols that must SURVIVE (the wave-9 over-match)
            ("??YReplicator@@QAAAAV0@ABVDName@@@Z",),   # operator+= overload
            ("??AReplicator@@QBA?AVDName@@H@Z",),        # operator[] overload
            ("??0HeadOrientationRuntime@@QAA@XZ",),      # ctor
            ("??1ExposureControlSystem@@QAA@XZ",),       # dtor
        ]
        self.conn.executemany("INSERT INTO functions VALUES (?)", self.rows)

    def tearDown(self):
        self.conn.close()

    def _count_surviving(self, escaped: bool) -> set:
        clauses = []
        for prefix in BOILERPLATE_SYMBOL_PREFIXES:
            if escaped:
                clauses.append(like_prefix_clause("symbol", prefix))
            else:
                clauses.append(f"symbol NOT LIKE '{prefix}%'")
        where = " AND ".join(clauses)
        cur = self.conn.execute(f"SELECT symbol FROM functions WHERE {where}")
        return {r[0] for r in cur.fetchall()}

    def test_escaped_keeps_operator_overloads(self):
        survivors = self._count_surviving(escaped=True)
        # The 4 real authorable symbols survive.
        self.assertIn("??YReplicator@@QAAAAV0@ABVDName@@@Z", survivors)
        self.assertIn("??AReplicator@@QBA?AVDName@@H@Z", survivors)
        self.assertIn("??0HeadOrientationRuntime@@QAA@XZ", survivors)
        self.assertIn("??1ExposureControlSystem@@QAA@XZ", survivors)
        # The 4 genuine boilerplate rows are excluded.
        self.assertEqual(len(survivors), 4)

    def test_unescaped_over_excludes(self):
        # Demonstrate the bug the fix prevents: the unescaped form wrongly drops
        # the operator overloads / ctors / dtors (they all start with '??' + one
        # char, which '??_%' / '??__E%' aliases).
        survivors = self._count_surviving(escaped=False)
        self.assertLess(len(survivors), 4)


if __name__ == "__main__":
    unittest.main(verbosity=2)
