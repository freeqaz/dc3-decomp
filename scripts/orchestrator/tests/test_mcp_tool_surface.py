"""The advertised MCP surface must match what the handlers actually read.

WHY: the failure mode this guards is not "the tool is missing" but "the tool
advertises a flag it silently ignores". `run_objdiff` gained `include_data`,
`diff_mode` and `output_format` in one commit; a schema entry whose handler
never calls `args.get()` for it looks like a working capability and is worse
than no capability, because an agent will trust the answer.

Also pins that the tool LIST is a superset of what CLAUDE.md tells agents to
use, so the documented rule ("do not call objdiff-cli directly") stays
followable.

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


if __name__ == "__main__":
    unittest.main()
