"""The advertised MCP surface must match what the handlers actually read.

WHY: the failure mode this guards is not "the tool is missing" but "the tool
advertises a flag it silently ignores". `run_objdiff` gained `include_data`,
`diff_mode` and `output_format` in one commit; a schema entry whose handler
never calls `args.get()` for it looks like a working capability and is worse
than no capability, because an agent will trust the answer.

Also pins that the tool LIST is a superset of what CLAUDE.md tells agents to
use, and that CLAUDE.md no longer states a rule the tools cannot satisfy.

Hermetic: reads the module source, builds no server, runs no objdiff.
"""
import ast
import inspect
import re
import sys
import unittest
from pathlib import Path

ORCH = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ORCH.parent))

MCP_SRC = (ORCH / "mcp_server.py").read_text()


def _tool_schemas():
    """Extract every Tool(name=..., inputSchema={...}) literal from the source.

    Parsing the AST rather than importing avoids pulling in the `mcp` package
    and a live database just to inspect a declaration.
    """
    tree = ast.parse(MCP_SRC)
    out = {}
    for node in ast.walk(tree):
        if not (isinstance(node, ast.Call) and getattr(node.func, "id", None) == "Tool"):
            continue
        kw = {k.arg: k.value for k in node.keywords}
        if "name" not in kw or "inputSchema" not in kw:
            continue
        try:
            name = ast.literal_eval(kw["name"])
            schema = ast.literal_eval(kw["inputSchema"])
        except ValueError:
            continue
        out[name] = schema
    return out


def _handler_source(method: str) -> str:
    m = re.search(
        rf"\n    async def {re.escape(method)}\(self, args: dict\).*?(?=\n    async def |\n    def |\Z)",
        MCP_SRC,
        re.S,
    )
    return m.group(0) if m else ""


class TestToolSurface(unittest.TestCase):
    def setUp(self):
        self.schemas = _tool_schemas()

    def test_all_documented_tools_are_declared(self):
        for t in ("run_objdiff", "run_diff_inspect", "run_analyze_function",
                  "query_functions", "lookup_rb3", "run_symbol_sweep"):
            self.assertIn(t, self.schemas, f"{t} is not advertised")

    def test_run_objdiff_exposes_the_bypassed_capabilities(self):
        """The three flags 464 transcript bypasses reached past MCP to get."""
        props = self.schemas["run_objdiff"]["properties"]
        for p in ("include_data", "diff_mode", "output_format", "unit",
                  "full_listing", "context"):
            self.assertIn(p, props, f"run_objdiff does not advertise {p}")
        self.assertEqual(
            set(props["diff_mode"]["enum"]), {"normalized", "raw", "name_check"}
        )
        self.assertEqual(set(props["output_format"]["enum"]), {"markdown", "json"})

    def test_run_objdiff_handler_reads_every_flag_it_advertises(self):
        src = _handler_source("_run_objdiff")
        self.assertTrue(src, "could not locate _run_objdiff")
        for p in self.schemas["run_objdiff"]["properties"]:
            self.assertRegex(
                src, rf'args\.get\(\s*["\']{re.escape(p)}["\']',
                f"run_objdiff advertises `{p}` but its handler never reads it",
            )

    def test_run_symbol_sweep_handler_reads_every_flag_it_advertises(self):
        src = _handler_source("_run_symbol_sweep")
        self.assertTrue(src, "could not locate _run_symbol_sweep")
        for p in self.schemas["run_symbol_sweep"]["properties"]:
            self.assertRegex(
                src, rf'args\.get\(\s*["\']{re.escape(p)}["\']',
                f"run_symbol_sweep advertises `{p}` but its handler never reads it",
            )

    def test_every_declared_tool_is_dispatched(self):
        dispatch = re.search(r"async def call_tool\(.*?\n\n", MCP_SRC, re.S).group(0)
        for name in self.schemas:
            self.assertIn(f'"{name}"', dispatch, f"{name} is declared but never dispatched")

    def test_include_data_is_rendered_not_merely_passed(self):
        """A flag that reaches objdiff but whose output is never shown is inert."""
        self.assertIn("--include-data", MCP_SRC)
        self.assertIn("_format_data_diff", MCP_SRC)
        src = _handler_source("_run_objdiff")
        self.assertIn("_format_data_diff", src)

    def test_sweep_result_always_carries_a_coverage_block(self):
        from orchestrator import symbol_sweep as S
        sig = inspect.getsource(S.sweep_data_symbols)
        self.assertIn('"_coverage"', sig)
        self.assertIn('"_coverage_render"', sig)
        sigf = inspect.getsource(S.sweep_functions)
        self.assertIn('"_coverage"', sigf)


class TestDocsAgreeWithTheSurface(unittest.TestCase):
    """A rule agents cannot follow teaches them to route around the convention."""

    def setUp(self):
        self.root = ORCH.parents[1]

    def test_claude_md_names_the_sweep_tool(self):
        text = (self.root / "CLAUDE.md").read_text()
        self.assertTrue("run_symbol_sweep" in text,
                        "CLAUDE.md does not mention run_symbol_sweep")

    def test_claude_md_does_not_carry_an_unqualified_prohibition(self):
        """`bin/objdiff-cli` has legitimate infrastructure callers (the ninja
        report rule, sync/measure scripts, the objdiff fork's own test
        harnesses). A blanket 'never' is false on its face, and a rule known to
        be false gets ignored wholesale."""
        text = (self.root / "CLAUDE.md").read_text()
        self.assertFalse(
            "Do not call `objdiff-cli` directly." in text,
            "CLAUDE.md still carries the unqualified prohibition; name the "
            "legitimate exceptions instead",
        )

    def test_reference_carries_the_invocation_mapping_table(self):
        text = (self.root / "docs" / "tools" / "REFERENCE.md").read_text()
        for marker in ("--include-data", "run_symbol_sweep", "diff_mode",
                       "Still legitimate direct CLI use"):
            self.assertTrue(marker in text,
                            f"REFERENCE.md is missing `{marker}`")


class TestDataDiffRendering(unittest.TestCase):
    """`_format_data_diff` must not read a `delete` row as a match.

    objdiff omits `base_target_symbol` in two different situations that mean
    opposite things: on `replace` it means "both sides name the same symbol",
    on `delete` it means "our side has no slot here at all". The first draft
    rendered both as "(same symbol)".
    """

    def setUp(self):
        sys.path.insert(0, str(ORCH.parent))
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "_mcp_probe", ORCH / "mcp_server.py")
        # importing the whole server pulls in `mcp`; grab the function via exec
        # of its source instead so the test stays hermetic.
        src = MCP_SRC
        start = src.index("def _format_data_diff(")
        end = src.index("\ndef _stack_signal_summary(")
        ns = {}
        exec(src[start:end], ns)  # noqa: S102 - single pure function
        self.fmt = ns["_format_data_diff"]

    def test_delete_row_is_not_rendered_as_same_symbol(self):
        out = self.fmt({"data_diff": {
            "match_percent": 45.0, "total_byte_count": 24, "mismatch_byte_count": 4,
            "relocations": [
                {"offset": 0x14, "kind": "delete",
                 "target_symbol": "??_R4Flow@@6BObjectDir@@@"},
            ],
            "segments": [],
        }})
        self.assertIn("no slot on our side", out)
        self.assertNotIn("same symbol", out)

    def test_replace_without_base_is_same_symbol(self):
        out = self.fmt({"data_diff": {
            "match_percent": 90.0, "total_byte_count": 8, "mismatch_byte_count": 0,
            "relocations": [
                {"offset": 8, "kind": "replace", "target_symbol": "?Enter@Flow@@UAAXXZ"},
            ],
            "segments": [],
        }})
        self.assertIn("same symbol", out)

    def test_absent_data_diff_explains_itself(self):
        out = self.fmt({})
        self.assertIn("CODE symbol", out)


class TestRelocRuler(unittest.TestCase):
    """`diff_mode="raw"` must actually change the ruler.

    It did not, twice. `run_diff_inspect` shipped raw as "omit -c", and the
    first draft of run_objdiff's diff_mode copied that. Measured on this fork
    (objdiff-cli 4.2.3, ?Load@CamShot@@UAAXAAVBinStream@@@Z, 2026-08-19):

        no -c / =none / =name_check  -> fuzzy 99.85558, 119 non-equal rows
        =all                         -> fuzzy 99.66141, 151 non-equal rows

    Only `all` counts relocations. A tool that advertises a raw mode and
    returns the normalized answer is worse than one with no raw mode, because
    an agent hunting a wrong-vtable-slot bug concludes there is not one.
    """

    def test_raw_maps_to_all_not_to_omitting_the_flag(self):
        ns = {}
        start = MCP_SRC.index("RELOC_RULER = {")
        end = MCP_SRC.index("}", start) + 1
        exec(MCP_SRC[start:end], ns)  # noqa: S102
        ruler = ns["RELOC_RULER"]
        self.assertEqual(ruler["raw"], "functionRelocDiffs=all")
        self.assertEqual(ruler["normalized"], "functionRelocDiffs=none")
        self.assertEqual(ruler["name_check"], "functionRelocDiffs=name_check")
        self.assertEqual(len(set(ruler.values())), 3,
                         "two modes resolve to the same ruler -- one of them is inert")

    def test_both_tools_route_through_the_one_table(self):
        for handler in ("_run_objdiff", "_run_diff_inspect"):
            src = _handler_source(handler)
            self.assertIn("RELOC_RULER", src,
                          f"{handler} does not use the shared ruler table")
        self.assertNotIn('if diff_mode != "raw" else []', MCP_SRC,
                         "the inert 'raw == omit -c' spelling is back")


if __name__ == "__main__":
    unittest.main()
