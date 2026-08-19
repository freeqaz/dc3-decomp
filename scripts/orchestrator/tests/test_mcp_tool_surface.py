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
        be false gets ignored wholesale.

        This used to assert the absence of ONE exact sentence, so a reworded
        blanket ban ("Never call `objdiff-cli` directly") would have sailed
        straight through -- the test would have passed while the defect it
        exists to catch was back in the file. Match the FAMILY of phrasings,
        and pair it with a positive assertion, so deleting the guidance
        outright cannot pass either.
        """
        text = (self.root / "CLAUDE.md").read_text()

        prohibition = re.compile(
            r"(?:do\s+not|do\s?n't|don'?t|never|avoid|no\s+need\s+to)\s+"
            r"(?:ever\s+)?"
            r"(?:calling|call|invoking|invoke|using|use|running|run|"
            r"shell(?:ing)?\s+out\s+to)\s+"
            r"[`'\"]?(?:bin/)?objdiff-cli",
            re.IGNORECASE,
        )
        # A prohibition is only acceptable if it is qualified on the same line
        # (e.g. "do not call objdiff-cli directly *except* for ...").
        unqualified = []
        for line in text.splitlines():
            if not prohibition.search(line):
                continue
            if re.search(r"except|unless|other than|only for|see below|named below|"
                         r"legitimate|exception", line, re.IGNORECASE):
                continue
            unqualified.append(line.strip())
        self.assertFalse(
            unqualified,
            "CLAUDE.md carries an UNQUALIFIED prohibition on objdiff-cli; name "
            "the legitimate exceptions instead. Offending line(s):\n  "
            + "\n  ".join(unqualified),
        )

        # Negative control: the regex must actually fire on the phrasings we
        # are trying to exclude, or the assertion above is vacuous.
        for phrasing in (
            "Do not call `objdiff-cli` directly.",
            "Never call objdiff-cli directly.",
            "Don't use bin/objdiff-cli.",
            "Avoid invoking `objdiff-cli`.",
        ):
            self.assertTrue(
                prohibition.search(phrasing),
                f"prohibition regex failed to match {phrasing!r} -- the "
                f"absence assertion above is vacuous",
            )

        # Positive: the exceptions must be named, so this cannot be satisfied
        # by deleting the guidance instead of qualifying it.
        self.assertTrue(
            re.search(r"legitimate direct", text, re.IGNORECASE),
            "CLAUDE.md no longer names the legitimate direct objdiff-cli uses",
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
    (objdiff-cli 4.2.3, ?Load@CamShot@@UAAXAAVBinStream@@@Z, 2026-08-19, with
    the ICF alias map held constant so the project option is the only
    variable):

        objdiff-cli's OWN default (data_value) -> 99.68568, 147 non-equal rows
        no -c under this repo's -p             -> 99.85558, 119 non-equal rows
        =none / =name_check                    -> 99.85558, 119 non-equal rows
        =all                                   -> 99.66141, 151 non-equal rows

    Omitting `-c` is not raw -- but NOT because "the fork's default is already
    normalized", which was the original rationale here and is false (the fork's
    default is a third ruler, 147 rows). It is because THIS REPO'S objdiff.json
    sets `"functionRelocDiffs": "name_check"`, which apply_project_options
    stamps over the CLI default. The behaviour travels with the project config,
    not the binary -- and bin/objdiff-cli is a symlink shared with ../rb3 and
    ../rb3-xenon.

    Severity, corrected: the old raw was MISLABELLED, not blind. It silently
    measured `name_check`, which charges relocation NAME mismatches -- the
    wrong-callee plane. The earlier claim that an agent hunting a wrong-slot
    bug "concludes there is not one" is retracted. `name_check` is in fact the
    sharpest ruler for that class; `all` is the addend view and adds ~99.8%
    noise on top of it.
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


class TestDiffInspectRulerHonesty(unittest.TestCase):
    """`diff_mode` reaches only 3 of run_diff_inspect's 11 modes.

    The five analysis modes (diagnose/clusters/regswaps/offsets/replaces)
    delegate to scripts/analysis/diff_inspect.py, which builds its OWN objdiff
    command and has no ruler switch -- so `reloc_config` never reaches them.
    That file belongs to another lane, so the wrapper cannot fix it; what it
    CAN do is refuse to hand back a normalized report under a raw label.
    Measured 2026-08-19: mode='mismatches' now returns 119 mismatches
    normalized vs 151 raw; mode='diagnose' returns 119 either way.
    """

    def test_analysis_modes_announce_that_they_ignored_the_ruler(self):
        # The banner lives in the module-level `_ruler_ignored_banner` helper,
        # so assert against the whole source, not just the handler body.
        self.assertIn("was IGNORED for mode=", MCP_SRC,
                      "analysis modes silently normalize a raw request")

    def test_every_ruler_deaf_mode_gets_the_banner(self):
        """The schema is honest about which modes honour `diff_mode`; the
        runtime banner must cover the same set.

        It originally covered only the five diff_inspect analysis modes, so
        `asm_listing`, `stack-layout` and `attributed` accepted a ruler and
        silently ignored it -- the exact defect the banner exists to prevent,
        in the three modes nobody checked.
        """
        ns = {}
        start = MCP_SRC.index("RULER_DEAF_MODES = {")
        end = MCP_SRC.index("\n}", start) + 2
        exec(MCP_SRC[start:end], ns)  # noqa: S102
        deaf = ns["RULER_DEAF_MODES"]
        for mode in ("diagnose", "clusters", "regswaps", "offsets", "replaces",
                     "asm_listing", "stack-layout", "attributed"):
            self.assertIn(mode, deaf,
                          f"mode {mode!r} cannot honour diff_mode but is not "
                          f"in RULER_DEAF_MODES, so it will ignore the ruler "
                          f"silently")

        # The three honouring modes must NOT be in the set, or a correct
        # measurement would get a banner saying it was ignored.
        for mode in ("mismatches", "compare", "save_baseline"):
            self.assertNotIn(mode, deaf,
                             f"mode {mode!r} DOES honour diff_mode; bannering "
                             f"it would tell the caller their correct "
                             f"measurement was discarded")

        # Every deaf mode must actually be wired to the helper at its call
        # site, not merely listed in the table.
        handler = _handler_source("_run_diff_inspect")
        self.assertGreaterEqual(
            handler.count("_ruler_ignored_banner("), 4,
            "RULER_DEAF_MODES lists modes the handler never banners; expected "
            "call sites for the generic analysis branch plus asm_listing, "
            "stack-layout and attributed",
        )

    def test_schema_admits_the_partial_support(self):
        props = _tool_schemas()["run_diff_inspect"]["properties"]
        desc = props["diff_mode"]["description"]
        self.assertIn("HONOURED ONLY", desc)
        for m in ("mismatches", "compare", "save_baseline"):
            self.assertIn(m, desc)


if __name__ == "__main__":
    unittest.main()
