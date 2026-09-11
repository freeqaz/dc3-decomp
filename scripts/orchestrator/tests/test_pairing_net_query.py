"""The callee-name classes must be served NET of objdiff's own pairing guesses.

WHY
===
Measured on pattern scan 18 (name_check, objdiff 4.2.8, whole binary,
2026-09-11): **56 of 62** ``WRONG_CALLEE`` rows also carry
``UNVERIFIABLE_PAIRING`` in the same scan.  Their enclosing symbol is an unnamed
``fn_<addr>`` MSVC EH funclet, which objdiff pairs by MASKED BYTE SIGNATURE --
and byte-identical funclets pair arbitrarily, so the "wrong callee" is objdiff's
pick, not a claim about our source.  A worklist handed out gross is ~90% rows a
lane cannot adjudicate; the callee-13 lane spent a pass discovering that.

Subtracting them SILENTLY would be the same defect in the other direction, and
it is the defect this whole subsystem exists to prevent: a query that returns 3
rows where 59 were measured is indistinguishable, at the call site, from a class
that only ever had 3 members.  So the count of what was hidden is part of the
answer, on both the populated and the EMPTY path.

Each test names the sabotage that must turn it red.  A guard nobody has watched
fail is an untested branch of the build.

Module-spelling note: ``mcp_server`` does ``from orchestrator.database import
query_functions as db_query_functions``, so the callable it holds belongs to the
``orchestrator.database`` module object -- a DIFFERENT object from
``scripts.orchestrator.database``, with its own ``_migrated_dbs`` cache.  Both
spellings are cleared in setUp, and the spy patches the name in ``mcp_server``'s
own namespace (which is spelling-independent).
"""

import asyncio
import json
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from unittest import mock

from scripts.orchestrator import database as db
from scripts.orchestrator.mcp_server import DecompMCPServer

#: objdiff 4.2.8's vocabulary, trimmed to what these fixtures use.  Recorded on
#: the scan row so `_pairing_vocabulary_missing` does not fire: a scan whose
#: vocabulary predates the detector is a DIFFERENT case, tested separately.
VOCAB_4_2_8 = ["REGISTER_SWAP", "UNVERIFIABLE_PAIRING", "WRONG_CALLEE",
               "TEMPLATE_INSTANTIATION_MISMATCH", "MAKESTRING_TEMPLATE_MISMATCH"]


def _server(db_path: Path) -> DecompMCPServer:
    srv = DecompMCPServer.__new__(DecompMCPServer)
    srv.db_path = str(db_path)
    return srv


def _call(srv: DecompMCPServer, args: dict) -> str:
    return asyncio.run(srv._query_functions(args))[0].text


class _FixtureDB:
    """Two WRONG_CALLEE rows -- one name-asserted, one a paired funclet.

    Shaped after the real population:

      id 1  ?NamedCallee@@YAXXZ   WRONG_CALLEE                        (adjudicable)
      id 2  fn_8264a930           WRONG_CALLEE + UNVERIFIABLE_PAIRING (guessed pair)
      id 3  ?Untouched@@YAXXZ     REGISTER_SWAP + UNVERIFIABLE_PAIRING
                                  -- a NON-callee pattern on a paired symbol.
                                  The filter must not touch it: pairing tells you
                                  nothing about a register-swap finding, which is
                                  read off the instructions, not off the pair.
    """

    NAMED = "?NamedCallee@@YAXXZ"
    FUNCLET = "fn_8264a930"
    OTHER = "?Untouched@@YAXXZ"

    def __init__(self, tmp: Path, *, vocabulary=VOCAB_4_2_8,
                 name: str = "fixture.db"):
        self.path = tmp / name
        with redirect_stdout(StringIO()):
            conn = db.init_database(self.path)
        rows = [(1, self.NAMED, "default/system/utl/Named"),
                (2, self.FUNCLET, "default/system/utl/Funclet"),
                (3, self.OTHER, "default/system/utl/Other")]
        for fid, sym, unit in rows:
            conn.execute(
                "INSERT INTO functions (id, symbol, demangled, unit, size, "
                "current_percent) VALUES (?,?,?,?,?,?)",
                (fid, sym, sym, unit, 100, 50.0))
        conn.execute(
            "INSERT INTO pattern_scans (id, ruler, tool_version, project_dir, "
            "universe, examined, patterns_checked) VALUES (1,'name_check',"
            "'objdiff-cli 4.2.8 (abcdef, xxh3 0123456789abcdef)','/tmp/t',3,3,?)",
            (json.dumps(vocabulary) if vocabulary is not None else None,))
        for fid, pattern in ((1, "WRONG_CALLEE"),
                             (2, "WRONG_CALLEE"),
                             (2, "UNVERIFIABLE_PAIRING"),
                             (3, "REGISTER_SWAP"),
                             (3, "UNVERIFIABLE_PAIRING")):
            conn.execute("INSERT INTO function_patterns (scan_id, function_id, "
                         "pattern) VALUES (1,?,?)", (fid, pattern))
        conn.commit()
        conn.close()

    def query(self, **kw):
        kw.setdefault("db_path", self.path)
        kw.setdefault("stale_units", "ignore")
        kw.setdefault("limit", 999)
        return db.query_functions(**kw)


class TestDatabaseLayerIsNetOfPairing(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)
        db._migrated_dbs.clear()
        try:                                   # the OTHER module object
            import orchestrator.database as od  # noqa: PLC0415
            od._migrated_dbs.clear()
        except ImportError:                     # pragma: no cover
            pass

    # ------------------------------------------------------------ the filter

    def test_default_excludes_the_paired_funclet_row(self):
        """SABOTAGE: delete the `pairing.format(op="AND NOT EXISTS")` clause in
        `database.query_functions` -> the funclet row comes back."""
        fx = _FixtureDB(self.tmp)

        # NEGATIVE CONTROL, inside the test: the opt-in must return BOTH rows.
        # Without it, "one row" below is also what a permanently broken query
        # returns -- and a filter that hides everything looks identical to a
        # filter that hides exactly the right thing.
        gross = fx.query(objdiff_pattern="WRONG_CALLEE", include_unverifiable=True)
        self.assertEqual({r["symbol"] for r in gross},
                         {_FixtureDB.NAMED, _FixtureDB.FUNCLET},
                         "control failed: the opt-in must return the gross set, "
                         "or the net set below proves nothing")

        net = fx.query(objdiff_pattern="WRONG_CALLEE")
        self.assertEqual([r["symbol"] for r in net], [_FixtureDB.NAMED])
        self.assertEqual(net.unverifiable_hidden, 1)

    def test_hidden_count_and_returned_rows_partition_the_gross_set(self):
        """len(net) + hidden == len(gross). SABOTAGE: count the pairing rows
        globally (drop `params` from the COUNT query) -> the identity breaks as
        soon as any other filter narrows the set."""
        fx = _FixtureDB(self.tmp)
        gross = fx.query(objdiff_pattern="WRONG_CALLEE", include_unverifiable=True)
        net = fx.query(objdiff_pattern="WRONG_CALLEE")
        self.assertEqual(len(net) + net.unverifiable_hidden, len(gross))

    def test_hidden_count_is_taken_under_the_callers_other_filters(self):
        """A count that ignores the caller's filters is not comparable to the
        rows beside it. SABOTAGE: as above."""
        fx = _FixtureDB(self.tmp)
        # Restrict to the unit that holds only the NAMED row: nothing is hidden
        # there, even though the scan still has a funclet row elsewhere.
        scoped = fx.query(objdiff_pattern="WRONG_CALLEE",
                          pattern="default/system/utl/Named")
        self.assertEqual([r["symbol"] for r in scoped], [_FixtureDB.NAMED])
        self.assertEqual(scoped.unverifiable_hidden, 0)
        self.assertEqual(scoped.unverifiable_note, "")

        # ... and the complementary control: scoped to the funclet's own unit,
        # the answer is EMPTY and the hidden count is 1.
        other = fx.query(objdiff_pattern="WRONG_CALLEE",
                         pattern="default/system/utl/Funclet")
        self.assertEqual(list(other), [])
        self.assertEqual(other.unverifiable_hidden, 1)

    def test_non_callee_patterns_are_untouched(self):
        """Pairing says nothing about a REGISTER_SWAP finding, which is read off
        the instructions. SABOTAGE: widen `PAIRING_SENSITIVE_PATTERNS` to
        include REGISTER_SWAP (or apply the filter unconditionally)."""
        fx = _FixtureDB(self.tmp)
        rows = fx.query(objdiff_pattern="REGISTER_SWAP")
        self.assertEqual([r["symbol"] for r in rows], [_FixtureDB.OTHER])
        self.assertEqual(rows.unverifiable_hidden, 0)
        self.assertEqual(rows.unverifiable_note, "")

    def test_result_is_still_a_list(self):
        """Every existing caller treats this as a plain list. SABOTAGE: return a
        dataclass/tuple instead of the list subclass."""
        fx = _FixtureDB(self.tmp)
        rows = fx.query(objdiff_pattern="WRONG_CALLEE")
        self.assertIsInstance(rows, list)
        self.assertEqual(rows, [dict(r) for r in rows])

    # ------------------------------------------------- the vocabulary caveat

    def test_a_scan_that_cannot_see_funclets_says_so(self):
        """objdiff < 4.2.7 had no UNVERIFIABLE_PAIRING detector, so subtracting
        it removes nothing -- which must NOT be reported as a clean zero.

        SABOTAGE: delete the `_pairing_vocabulary_missing` branch -> the note is
        empty and a 4.2.6 scan silently serves the gross set as the net set.
        """
        fx = _FixtureDB(self.tmp, vocabulary=["WRONG_CALLEE", "REGISTER_SWAP"],
                        name="old.db")
        rows = fx.query(objdiff_pattern="WRONG_CALLEE")
        self.assertEqual(rows.unverifiable_hidden, 0)
        self.assertIn("4.2.7", rows.unverifiable_note)
        self.assertIn("pattern_census", rows.unverifiable_note)
        # And the note must be TRUE: it claims the funclet rows "are still in
        # this result", so they had better be. A warning that misdescribes the
        # result it annotates is worse than no warning.
        self.assertIn(_FixtureDB.FUNCLET, {r["symbol"] for r in rows})

    def test_current_vocabulary_does_not_emit_the_caveat(self):
        """Control for the test above: a 4.2.8 scan must NOT carry the warning,
        or the warning is noise that everyone learns to skip."""
        fx = _FixtureDB(self.tmp)
        note = fx.query(objdiff_pattern="WRONG_CALLEE").unverifiable_note
        self.assertNotIn("4.2.7", note)
        self.assertIn("rows hidden", note)


class TestTheHiddenCountIsRendered(unittest.TestCase):
    """The database layer can only offer the count; the MCP tool is where a lane
    actually reads it."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)
        db._migrated_dbs.clear()
        try:
            import orchestrator.database as od  # noqa: PLC0415
            od._migrated_dbs.clear()
        except ImportError:                     # pragma: no cover
            pass
        self.fx = _FixtureDB(self.tmp)
        self.srv = _server(self.fx.path)

    def test_populated_answer_names_the_hidden_count(self):
        """SABOTAGE: drop the `if pairing_note:` block from the `output` builder
        in `_query_functions`."""
        text = _call(self.srv, {"objdiff_pattern": "WRONG_CALLEE",
                                "stale_units": "ignore"})
        self.assertIn(_FixtureDB.NAMED, text)
        self.assertNotIn(_FixtureDB.FUNCLET, text)
        self.assertIn("1 rows hidden", text)
        self.assertIn("byte-signature-paired funclet", text)

    def test_empty_answer_still_names_the_hidden_count(self):
        """The path where it matters MOST: 0 rows returned with rows subtracted
        behind them is not an exhausted class.

        SABOTAGE: drop the `if pairing_note:` block from the `if not results:`
        branch -> "No functions found ... IS a measurement" with no mention that
        everything in the class was hidden.
        """
        text = _call(self.srv, {"objdiff_pattern": "WRONG_CALLEE",
                                "unit_pattern": "default/system/utl/Funclet",
                                "stale_units": "ignore"})
        self.assertIn("No functions found", text)
        self.assertIn("1 rows hidden", text)

    def test_opt_in_renders_and_returns_the_gross_set(self):
        """SABOTAGE: stop forwarding `include_unverifiable` from the handler ->
        the funclet row is missing even when it was asked for."""
        text = _call(self.srv, {"objdiff_pattern": "WRONG_CALLEE",
                                "include_unverifiable": True,
                                "stale_units": "ignore"})
        self.assertIn(_FixtureDB.NAMED, text)
        self.assertIn(_FixtureDB.FUNCLET, text)
        self.assertIn("include_unverifiable", text)

    def test_handler_default_is_net(self):
        """SABOTAGE: change the handler default to True -> a lane that asks for
        WRONG_CALLEE gets the gross set back without asking."""
        seen = {}
        real = db.query_functions
        with mock.patch("scripts.orchestrator.mcp_server.db_query_functions",
                        side_effect=lambda **kw: (seen.update(kw), real(**kw))[1]):
            _call(self.srv, {"objdiff_pattern": "WRONG_CALLEE"})
        self.assertIs(seen.get("include_unverifiable"), False)

    def test_no_note_on_a_query_with_nothing_to_subtract(self):
        """Control: the line must be absent when nothing was hidden, or its
        presence above carries no information.

        SABOTAGE: render the note unconditionally.
        """
        text = _call(self.srv, {"objdiff_pattern": "REGISTER_SWAP",
                                "stale_units": "ignore"})
        self.assertIn(_FixtureDB.OTHER, text)
        self.assertNotIn("rows hidden", text)


class TestTheArgumentIsAdvertised(unittest.TestCase):
    def setUp(self):
        self.src = (Path(__file__).resolve().parents[1] / "mcp_server.py").read_text()

    def test_schema_declares_include_unverifiable(self):
        """A handler honouring an argument the schema does not declare is an
        argument no caller can discover. SABOTAGE: remove the property."""
        import ast
        for node in ast.walk(ast.parse(self.src)):
            if not (isinstance(node, ast.Call)
                    and getattr(node.func, "id", None) == "Tool"):
                continue
            kw = {k.arg: k.value for k in node.keywords}
            if "name" not in kw or "inputSchema" not in kw:
                continue
            try:
                if ast.literal_eval(kw["name"]) != "query_functions":
                    continue
                schema = ast.literal_eval(kw["inputSchema"])
            except ValueError:
                continue
            prop = schema["properties"].get("include_unverifiable")
            self.assertIsNotNone(prop, "schema does not advertise "
                                       "include_unverifiable")
            self.assertEqual(prop["type"], "boolean")
            return
        self.fail("query_functions tool not found in mcp_server.py")


if __name__ == "__main__":
    unittest.main()
