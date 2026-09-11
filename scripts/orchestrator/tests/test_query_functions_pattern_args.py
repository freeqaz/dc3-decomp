"""`query_functions`'s pattern trio must reach the database layer.

CLAUDE.md tells every lane to use `query_functions(objdiff_pattern=...)` instead
of reading a `has_*` column, and `database.query_functions` implements it --
including the deliberate REFUSALS that make an absent measurement distinguishable
from an absent pattern.  The MCP tool schema never declared `objdiff_pattern`,
`pattern_ruler` or `stale_units`, and `_query_functions` never read them, so
every one of those calls was answered by an UNFILTERED query.

Measured on the code these tests were written against (three functions, one
carrying WRONG_CALLEE in the recorded scan):

    database.query_functions(objdiff_pattern='WRONG_CALLEE') -> ['?Alpha@@YAXXZ']
    MCP _query_functions(objdiff_pattern='WRONG_CALLEE')     -> Alpha, Beta, Gamma
    MCP _query_functions(..., pattern_ruler='none')          -> Alpha, Beta, Gamma
                                                                (documented to RAISE)

A wrong SUPERSET is the nastier half: nothing in the answer says the filter was
dropped, and the extra rows look like work.

Each test here is paired with the sabotage that must turn it red -- named in the
test, because a guard nobody has watched fail is an untested branch of the build.
"""

import asyncio
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from unittest import mock

from scripts.orchestrator import database as db
from scripts.orchestrator.mcp_server import DecompMCPServer


def _server(db_path: Path) -> DecompMCPServer:
    """A server object with a db_path and nothing else constructed.

    `DecompMCPServer.__init__` builds an mcp Server and registers handlers; the
    handler under test reads only `self.db_path`.
    """
    srv = DecompMCPServer.__new__(DecompMCPServer)
    srv.db_path = str(db_path)
    return srv


def _call(srv: DecompMCPServer, args: dict) -> str:
    out = asyncio.run(srv._query_functions(args))
    return out[0].text


class _FixtureDB:
    """Three functions; one carries WRONG_CALLEE in a recorded name_check scan."""

    SYMBOLS = ("?Alpha@@YAXXZ", "?Beta@@YAXXZ", "?Gamma@@YAXXZ")

    def __init__(self, tmp: Path, *, with_scan: bool = True):
        self.path = tmp / "fixture.db"
        with redirect_stdout(StringIO()):
            conn = db.init_database(self.path)
        for i, sym in enumerate(self.SYMBOLS, start=1):
            conn.execute(
                "INSERT INTO functions (id, symbol, demangled, unit, size, "
                "current_percent) VALUES (?,?,?,?,?,?)",
                (i, sym, sym, "default/test/Unit", 100, 50.0))
        if with_scan:
            conn.execute(
                "INSERT INTO pattern_scans (id, ruler, tool_version, project_dir, "
                "universe, examined) VALUES (1,'name_check',"
                "'objdiff-cli 4.2.8 (abcdef, xxh3 0123456789abcdef)','/tmp/t',3,3)")
            conn.execute("INSERT INTO function_patterns (scan_id, function_id, "
                         "pattern) VALUES (1,1,'WRONG_CALLEE')")
            # No `pattern_scan_units` rows on purpose: that is the pre-v18
            # shape, and the reader must serve those rows flagged
            # 'unfingerprinted' rather than silently clean.
        conn.commit()
        conn.close()


class TestPatternArgsReachTheDatabase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)
        db._migrated_dbs.clear()

    # ---------------------------------------------------------------- forwarding

    def test_objdiff_pattern_reaches_database_query_functions(self):
        """SABOTAGE: drop `objdiff_pattern=objdiff_pattern` from the
        db_query_functions(...) call in `_query_functions`."""
        fx = _FixtureDB(self.tmp)
        srv = _server(fx.path)
        seen = {}

        real = db.query_functions

        def spy(**kwargs):
            seen.update(kwargs)
            return real(**kwargs)

        with mock.patch("scripts.orchestrator.mcp_server.db_query_functions",
                        side_effect=spy):
            _call(srv, {"objdiff_pattern": "WRONG_CALLEE",
                        "pattern_ruler": "name_check",
                        "stale_units": "exclude"})

        self.assertEqual(seen.get("objdiff_pattern"), "WRONG_CALLEE")
        self.assertEqual(seen.get("pattern_ruler"), "name_check")
        self.assertEqual(seen.get("stale_units"), "exclude")

    def test_defaults_match_the_database_layer(self):
        """A caller who omits the ruler must get 'name_check', not whatever the
        handler felt like.  SABOTAGE: change either default in the handler."""
        fx = _FixtureDB(self.tmp)
        srv = _server(fx.path)
        seen = {}
        real = db.query_functions

        with mock.patch("scripts.orchestrator.mcp_server.db_query_functions",
                        side_effect=lambda **kw: (seen.update(kw), real(**kw))[1]):
            _call(srv, {"objdiff_pattern": "WRONG_CALLEE"})
        self.assertEqual(seen.get("pattern_ruler"), "name_check")
        self.assertEqual(seen.get("stale_units"), "flag")

    # ------------------------------------------------------- end-to-end filtering

    def test_pattern_filter_actually_narrows_the_result(self):
        """The filter must DISCRIMINATE -- the negative control is in the test.

        Without the control this passes on a query that returns one row for
        every reason, including a permanently broken one.

        SABOTAGE: drop the forwarding -> unfiltered returns all three.
        """
        fx = _FixtureDB(self.tmp)
        srv = _server(fx.path)

        unfiltered = _call(srv, {})
        for sym in _FixtureDB.SYMBOLS:                      # negative control
            self.assertIn(sym, unfiltered,
                          "control failed: the fixture must return all three "
                          "rows with NO pattern filter, or 'one row' below "
                          "proves nothing")

        filtered = _call(srv, {"objdiff_pattern": "WRONG_CALLEE"})
        self.assertIn("?Alpha@@YAXXZ", filtered)
        self.assertNotIn("?Beta@@YAXXZ", filtered)
        self.assertNotIn("?Gamma@@YAXXZ", filtered)

    # ------------------------------------------------------------- the refusals

    def test_ruler_none_errors_instead_of_answering(self):
        """`pattern_ruler='none'` must ERROR, not return rows.

        The mcp lowlevel server renders an exception from a tool handler as
        CallToolResult(isError=True); an empty -- or worse, unfiltered -- list
        reads as a measurement.

        SABOTAGE: wrap the db_query_functions call in
        `except ValueError: results = []`.
        """
        fx = _FixtureDB(self.tmp)
        srv = _server(fx.path)
        with self.assertRaises(ValueError) as cm:
            _call(srv, {"objdiff_pattern": "WRONG_CALLEE", "pattern_ruler": "none"})
        self.assertIn("none", str(cm.exception))

    def test_unmeasured_ruler_errors_instead_of_answering_empty(self):
        """No scan for the ruler -> error. SABOTAGE: same as above."""
        fx = _FixtureDB(self.tmp, with_scan=False)
        srv = _server(fx.path)
        with self.assertRaises(ValueError) as cm:
            _call(srv, {"objdiff_pattern": "WRONG_CALLEE"})
        self.assertIn("pattern_census", str(cm.exception))

    def test_a_measured_empty_class_says_it_was_measured(self):
        """A genuine zero must not read like the refusals above.

        SABOTAGE: delete the `if objdiff_pattern:` branch on the
        'No functions found' message.
        """
        fx = _FixtureDB(self.tmp)
        srv = _server(fx.path)
        text = _call(srv, {"objdiff_pattern": "TEMPLATE_INSTANTIATION_MISMATCH"})
        self.assertIn("No functions found", text)
        self.assertIn("IS a measurement", text)

    # ---------------------------------------------------------------- rendering

    def test_stale_objects_are_rendered_on_the_row(self):
        """`unit_objects_stale` must reach the caller's eyes.

        A pattern row is a finding about two object files; both sides move.
        The database layer attaches the currency per row -- a renderer that
        drops it turns a stored finding back into a current fact.

        SABOTAGE: drop `stale_str` from the output line.
        """
        fx = _FixtureDB(self.tmp)                      # scan has no unit baseline
        srv = _server(fx.path)
        text = _call(srv, {"objdiff_pattern": "WRONG_CALLEE"})
        self.assertIn("?Alpha@@YAXXZ", text)
        self.assertIn("STALE", text)
        self.assertIn("unfingerprinted", text)

    def test_stale_label_is_absent_when_currency_was_not_checked(self):
        """Control for the test above -- and an end-to-end check of the third arg.

        `stale_units='ignore'` skips the currency check entirely, so no row
        carries `unit_objects_stale` and no row may be labelled.  Without this
        control, printing 'STALE' unconditionally would pass the test above.

        SABOTAGE: hardcode `stale_units='flag'` in the handler -> the label
        reappears here.
        """
        fx = _FixtureDB(self.tmp)
        srv = _server(fx.path)
        text = _call(srv, {"objdiff_pattern": "WRONG_CALLEE",
                           "stale_units": "ignore"})
        self.assertIn("?Alpha@@YAXXZ", text)
        self.assertNotIn("STALE", text)


class TestPatternArgsAreAdvertised(unittest.TestCase):
    """The schema must declare what the handler now honours (and vice versa)."""

    def setUp(self):
        self.src = (Path(__file__).resolve().parents[1] / "mcp_server.py").read_text()

    def test_schema_declares_the_trio(self):
        """SABOTAGE: remove any of the three properties from the tool schema."""
        import ast
        tree = ast.parse(self.src)
        schema = None
        for node in ast.walk(tree):
            if (isinstance(node, ast.Call)
                    and getattr(node.func, "id", None) == "Tool"):
                kw = {k.arg: k.value for k in node.keywords}
                if "name" not in kw or "inputSchema" not in kw:
                    continue
                try:
                    if ast.literal_eval(kw["name"]) == "query_functions":
                        schema = ast.literal_eval(kw["inputSchema"])
                except ValueError:
                    continue
        self.assertIsNotNone(schema, "query_functions tool not found in source")
        props = schema.get("properties", {})
        for key in ("objdiff_pattern", "pattern_ruler", "stale_units"):
            self.assertIn(key, props, f"tool schema does not advertise {key}")

    def test_ruler_enum_admits_none_so_the_refusal_is_reachable(self):
        """'none' stays in the enum ON PURPOSE: a caller who asks for the
        forbidden ruler must receive the explanation, not a schema rejection
        that says nothing about why."""
        import ast, json  # noqa: E401
        m = [n for n in ast.walk(ast.parse(self.src))
             if isinstance(n, ast.Call) and getattr(n.func, "id", None) == "Tool"]
        for node in m:
            kw = {k.arg: k.value for k in node.keywords}
            if "name" not in kw:
                continue
            try:
                if ast.literal_eval(kw["name"]) != "query_functions":
                    continue
                schema = ast.literal_eval(kw["inputSchema"])
            except ValueError:
                continue
            self.assertIn("none", schema["properties"]["pattern_ruler"]["enum"])
            return
        self.fail("query_functions tool not found")


if __name__ == "__main__":
    unittest.main()
