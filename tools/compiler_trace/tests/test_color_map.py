"""Unit tests for `color_map.py` — parsing / partitioning / correlation logic.

Unlike `test_bsf_engine.py` / `test_bsf_pipeline.py` (which compile real
source under GDB), these tests operate purely on captured trace TEXT and
synthetic fixtures — no compiler, wibo, or GDB required. The captured
transcript below is the exact probe output recorded in
`docs/plans/il-witness/g2_push/V1_COLOR_DUMP_NOTES.md` §4 (task C2), so these
tests double as a regression check that the parser/regex still matches the
validated probe evidence.

Usage:
    python -m pytest tools/compiler_trace/tests/test_color_map.py -v
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest import mock

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from tools.compiler_trace.color_map import (
    ColorEvent,
    ColorMapTrace,
    PRIMARY_ALLOC_CALLER_RVA,
    CLASS_GPR,
    CLASS_FPR,
    _parse_color_map_output,
    select_function_key,
)

# ---------------------------------------------------------------------------
# The exact probe transcript from V1_COLOR_DUMP_NOTES.md §4 (task C2), plus
# the GDB harness boilerplate lines that a real run interleaves it with, to
# make sure the parser is robust to the surrounding noise, not just a clean
# fixture.
# ---------------------------------------------------------------------------
CAPTURED_PROBE_TRANSCRIPT = """\
### /FAs compile rc: 0
### /FAs __savegprlr_: ['27', '27']
### c2 mapped: base=0x10b00000
COLORSET #1: caller=0x0c6031 idx=32 node=0x6c28f5b8 lrreg=78 hint=0x10c2f208 cls=1
COLORSET #2: caller=0x0c6031 idx=31 node=0x6c28fa98 lrreg=91 hint=0x10c2f208 cls=1
COLORSET #3: caller=0x0c6031 idx=30 node=0x6c28faf8 lrreg=92 hint=0x10c2f208 cls=1
COLORSET #4: caller=0x0c6031 idx=29 node=0x6c28fb58 lrreg=93 hint=0x10c2f208 cls=1
COLORSET #5: caller=0x0c6031 idx=28 node=0x6c28f858 lrreg=85 hint=0x10c2f208 cls=1
COLORSET #6: caller=0x0c6031 idx=29 node=0x6c28fc18 lrreg=95 hint=0x00000000 cls=1
COLORSET #7: caller=0x0c6031 idx=30 node=0x6c2a0878 lrreg=160 hint=0x00000000 cls=1
COLORSET #8: caller=0x0c6031 idx=31 node=0x6c2a08d8 lrreg=161 hint=0x00000000 cls=1
### Total COLORSET events: 8
"""

# Fixture asm listing: two functions, PROC/ENDP + __savegprlr_N markers, in
# the format bsf_trace._parse_function_info expects (models SOURCE_MULTI_FUNC
# from test_bsf_engine.py, kept local so this file needs no compiler).
FIXTURE_ASM_TWO_FUNCS = """\
?func_alpha@@YAHH@Z PROC NEAR
        __savegprlr_29
        .endprolog
; 3    :     return a + b + c;
        blr
?func_alpha@@YAHH@Z ENDP
?func_beta@@YAHH@Z PROC NEAR
        __savegprlr_30
        .endprolog
; 8    :     return x + y;
        blr
?func_beta@@YAHH@Z ENDP
""".splitlines()


def _synthetic_event(
    idx_seq_no: int, caller_rva: int, phys_idx: int, cls: int = CLASS_GPR
) -> ColorEvent:
    return ColorEvent(
        index=idx_seq_no,
        caller_rva=caller_rva,
        phys_idx=phys_idx,
        node_ptr=0x1000 + phys_idx,
        lr_id=phys_idx,
        hint=0,
        cls=cls,
    )


class TestColorEventRegName(unittest.TestCase):
    """GPR naming rule: idx -> r(idx-1); FPR/VMX stay unnamed (unconfirmed)."""

    def test_gpr_r31_first(self):
        ev = _synthetic_event(1, PRIMARY_ALLOC_CALLER_RVA, 32, cls=CLASS_GPR)
        self.assertEqual(ev.reg_name(), "r31")

    def test_gpr_boundary(self):
        # idx=1 -> r0, idx=32 -> r31 (COLOR_RE.md GPR range 1-32)
        self.assertEqual(_synthetic_event(1, 0, 1).reg_name(), "r0")
        self.assertEqual(_synthetic_event(1, 0, 32).reg_name(), "r31")

    def test_gpr_out_of_range_unnamed(self):
        self.assertIsNone(_synthetic_event(1, 0, 33, cls=CLASS_GPR).reg_name())

    def test_fpr_unconfirmed_stays_unnamed(self):
        # Notes caveat 2: FPR/VMX bases are static-RE-only, not probe-confirmed.
        # reg_name() must NOT fabricate an f*/vr* name for them.
        ev = _synthetic_event(1, PRIMARY_ALLOC_CALLER_RVA, 34, cls=CLASS_FPR)
        self.assertIsNone(ev.reg_name())
        self.assertEqual(ev.class_name, "fpr")

    def test_is_primary_alloc(self):
        primary = _synthetic_event(1, PRIMARY_ALLOC_CALLER_RVA, 32)
        spill = _synthetic_event(2, 0x0C4EAE, 32)
        self.assertTrue(primary.is_primary_alloc)
        self.assertFalse(spill.is_primary_alloc)


class TestParseColorMapOutput(unittest.TestCase):
    """Parser regression test against the recorded C2 probe transcript."""

    def setUp(self):
        self.trace = _parse_color_map_output(
            CAPTURED_PROBE_TRANSCRIPT, Path("probe_5var.cpp")
        )

    def test_event_count_matches_probe(self):
        self.assertEqual(self.trace.total_events, 8)

    def test_ignores_non_colorset_lines(self):
        # The '### ...' harness lines must not be mistaken for events.
        self.assertEqual(
            len(self.trace.events),
            len([l for l in CAPTURED_PROBE_TRANSCRIPT.splitlines() if l.startswith("COLORSET")]),
        )

    def test_first_five_events_are_r31_first_descending(self):
        """Reproduces the C2 gate check: idx 32->31->30->29->28 (r31 first)."""
        first_five = self.trace.events[:5]
        idxs = [e.phys_idx for e in first_five]
        self.assertEqual(idxs, [32, 31, 30, 29, 28])
        regs = [e.reg_name() for e in first_five]
        self.assertEqual(regs, ["r31", "r30", "r29", "r28", "r27"])

    def test_distinct_assignment_count_matches_savegprlr(self):
        """distinct primary idx set == {r27..r31} == __savegprlr_27 save set."""
        primary = self.trace.primary_events
        distinct = {e.phys_idx for e in primary}
        self.assertEqual(distinct, {28, 29, 30, 31, 32})
        self.assertEqual(len(distinct), 5)

    def test_reuse_events_hint_zero(self):
        """Events #6-8 are dynamic reuse of already-saved regs (hint=0), not
        new saves — matches the "assignments-only, no free event" verdict
        (notes §3)."""
        reuse = self.trace.events[5:8]
        self.assertTrue(all(e.hint == 0 for e in reuse))
        first_five_hints = [e.hint for e in self.trace.events[:5]]
        self.assertTrue(all(h != 0 for h in first_five_hints))

    def test_all_events_primary_alloc_in_fixture(self):
        self.assertEqual(len(self.trace.primary_events), 8)

    def test_all_events_class_gpr(self):
        self.assertTrue(all(e.cls == CLASS_GPR for e in self.trace.events))
        self.assertTrue(all(e.class_name == "gpr" for e in self.trace.events))

    def test_lr_id_is_not_phys_index(self):
        """Regression guard for the biggest footgun (notes caveat 1): lr_id
        (node+0x1c) must never be confused with phys_idx (the setter's ecx).
        In the probe these value sets are disjoint."""
        phys_idxs = {e.phys_idx for e in self.trace.events}
        lr_ids = {e.lr_id for e in self.trace.events}
        self.assertEqual(phys_idxs & lr_ids, set())


class TestPartitionByFunction(unittest.TestCase):
    """Synthetic two-function partitioning (models bsf_trace's equivalent test)."""

    def _make_trace(self) -> ColorMapTrace:
        # func_alpha needs 3 distinct regs (r29,r30,r31 -> savegprlr_29),
        # func_beta needs 2 (r30,r31 -> savegprlr_30). Include one reuse event
        # per function (repeated idx, hint irrelevant to partitioning).
        events = [
            _synthetic_event(1, PRIMARY_ALLOC_CALLER_RVA, 32),  # alpha r31
            _synthetic_event(2, PRIMARY_ALLOC_CALLER_RVA, 31),  # alpha r30
            _synthetic_event(3, PRIMARY_ALLOC_CALLER_RVA, 30),  # alpha r29
            _synthetic_event(4, PRIMARY_ALLOC_CALLER_RVA, 32),  # alpha reuse (not new)
            _synthetic_event(5, PRIMARY_ALLOC_CALLER_RVA, 32),  # beta r31
            _synthetic_event(6, PRIMARY_ALLOC_CALLER_RVA, 31),  # beta r30
            _synthetic_event(7, PRIMARY_ALLOC_CALLER_RVA, 30),  # remainder
        ]
        return ColorMapTrace(source=Path("two_funcs.cpp"), events=events)

    def test_partition_produces_both_functions(self):
        trace = self._make_trace()
        partitions = trace.partition_by_function(FIXTURE_ASM_TWO_FUNCS)
        func_names = [k for k in partitions if k not in ("__all__", "__remainder__")]
        self.assertEqual(len(func_names), 2)

    def test_partition_distinct_counts_match_callee_saved(self):
        trace = self._make_trace()
        partitions = trace.partition_by_function(FIXTURE_ASM_TWO_FUNCS)

        alpha = partitions["?func_alpha@@YAHH@Z"]
        beta = partitions["?func_beta@@YAHH@Z"]

        alpha_distinct = {e.phys_idx for e in alpha.events}
        beta_distinct = {e.phys_idx for e in beta.events}
        self.assertEqual(len(alpha_distinct), 3, alpha_distinct)
        self.assertEqual(len(beta_distinct), 2, beta_distinct)

    def test_partitions_disjoint_event_indices(self):
        trace = self._make_trace()
        partitions = trace.partition_by_function(FIXTURE_ASM_TWO_FUNCS)
        alpha_idx = {e.index for e in partitions["?func_alpha@@YAHH@Z"].events}
        beta_idx = {e.index for e in partitions["?func_beta@@YAHH@Z"].events}
        self.assertEqual(alpha_idx & beta_idx, set())

    def test_remainder_bucket_for_leftover_events(self):
        trace = self._make_trace()
        partitions = trace.partition_by_function(FIXTURE_ASM_TWO_FUNCS)
        # alpha consumes events #1-4 (3 distinct), beta consumes #5-6 (2
        # distinct); event #7 is unconsumed leftover.
        self.assertIn("__remainder__", partitions)
        self.assertEqual([e.index for e in partitions["__remainder__"].events], [7])

    def test_no_func_info_falls_back_to_all(self):
        trace = self._make_trace()
        partitions = trace.partition_by_function(["not an asm listing"])
        self.assertEqual(set(partitions), {"__all__"})

    def test_no_primary_events_falls_back_to_all(self):
        trace = ColorMapTrace(
            source=Path("x.cpp"),
            events=[_synthetic_event(1, 0x0C4EAE, 32)],  # spill-only, not primary
        )
        partitions = trace.partition_by_function(FIXTURE_ASM_TWO_FUNCS)
        self.assertEqual(set(partitions), {"__all__"})


class TestSelectFunctionKey(unittest.TestCase):
    def test_exact_match(self):
        traces = {"?Foo@@YAHXZ": None, "?Bar@@YAHXZ": None}
        self.assertEqual(select_function_key(traces, "?Foo@@YAHXZ"), "?Foo@@YAHXZ")

    def test_substring_match(self):
        traces = {"?Foo@Baz@@YAHXZ": None, "?Bar@@YAHXZ": None}
        self.assertEqual(select_function_key(traces, "Foo"), "?Foo@Baz@@YAHXZ")

    def test_case_insensitive_fallback(self):
        traces = {"?Foo@Baz@@YAHXZ": None}
        self.assertEqual(select_function_key(traces, "foo"), "?Foo@Baz@@YAHXZ")

    def test_no_match_returns_none(self):
        traces = {"?Foo@@YAHXZ": None}
        self.assertIsNone(select_function_key(traces, "NoSuchFunc"))


class TestVarMapAndDiffTarget(unittest.TestCase):
    """var_map/diff_target correlation logic, with the decl-name extractor and
    objdiff-json extractor mocked (no compiler/objdiff needed)."""

    def _make_trace(self) -> ColorMapTrace:
        events = [
            _synthetic_event(1, PRIMARY_ALLOC_CALLER_RVA, 32),  # -> r31
            _synthetic_event(2, PRIMARY_ALLOC_CALLER_RVA, 31),  # -> r30
            _synthetic_event(3, PRIMARY_ALLOC_CALLER_RVA, 30),  # -> r29
        ]
        return ColorMapTrace(source=Path("x.cpp"), events=events)

    def test_var_map_zips_decl_order_with_distinct_assignments(self):
        trace = self._make_trace()
        with mock.patch(
            "tools.compiler_trace.regmap_solver._extract_declaration_names",
            return_value=["a", "b", "c"],
        ):
            vm = trace.var_map(Path("x.cpp"), "Foo")
        self.assertEqual(vm, {"a": "r31", "b": "r30", "c": "r29"})

    def test_var_map_empty_when_no_decl_names(self):
        trace = self._make_trace()
        with mock.patch(
            "tools.compiler_trace.regmap_solver._extract_declaration_names",
            return_value=[],
        ):
            vm = trace.var_map(Path("x.cpp"), "Foo")
        self.assertEqual(vm, {})

    def test_var_map_dedupes_reuse_events(self):
        events = [
            _synthetic_event(1, PRIMARY_ALLOC_CALLER_RVA, 32),  # a -> r31
            _synthetic_event(2, PRIMARY_ALLOC_CALLER_RVA, 31),  # b -> r30
            _synthetic_event(3, PRIMARY_ALLOC_CALLER_RVA, 32),  # reuse, not "c"
        ]
        trace = ColorMapTrace(source=Path("x.cpp"), events=events)
        with mock.patch(
            "tools.compiler_trace.regmap_solver._extract_declaration_names",
            return_value=["a", "b", "c"],
        ):
            vm = trace.var_map(Path("x.cpp"), "Foo")
        # Only 2 distinct assignments exist -> "c" has no assignment to zip with.
        self.assertEqual(vm, {"a": "r31", "b": "r30"})

    def test_diff_target_flags_divergence(self):
        trace = self._make_trace()
        # objdiff says: target wanted r29 where base(ours) put r31; the rest
        # match (identity, so not returned by extract_target_register_map).
        fake_reg_map = {"r29": "r31"}  # target_reg -> base_reg
        with mock.patch(
            "tools.compiler_trace.regmap_solver._extract_declaration_names",
            return_value=["a", "b", "c"],
        ), mock.patch(
            "tools.compiler_trace.regmap_solver.extract_target_register_map",
            return_value=fake_reg_map,
        ):
            rows = trace.diff_target(Path("x.cpp"), "Foo", {"instructions": []})

        by_var = {r["var"]: r for r in rows}
        self.assertTrue(by_var["a"]["divergent"])
        self.assertEqual(by_var["a"]["ours"], "r31")
        self.assertEqual(by_var["a"]["target"], "r29")
        self.assertFalse(by_var["b"]["divergent"])
        self.assertIsNone(by_var["b"]["target"])

    def test_diff_target_no_divergence_when_reg_map_empty(self):
        trace = self._make_trace()
        with mock.patch(
            "tools.compiler_trace.regmap_solver._extract_declaration_names",
            return_value=["a", "b", "c"],
        ), mock.patch(
            "tools.compiler_trace.regmap_solver.extract_target_register_map",
            return_value={},
        ):
            rows = trace.diff_target(Path("x.cpp"), "Foo", {"instructions": []})
        self.assertTrue(all(not r["divergent"] for r in rows))


if __name__ == "__main__":
    unittest.main()
