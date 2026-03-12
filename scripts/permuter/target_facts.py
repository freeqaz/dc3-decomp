"""Target Facts — normalized evidence layer for decomp functions.

Aggregates evidence from diagnosis, attribution, atlas lookups, and
guidance sources into a uniform queryable structure.

This is Synthesis Engine Phase 4 — see docs/plans/synthesis-engine/ROADMAP.md.

Usage:
    from scripts.permuter.target_facts import TargetFacts, extract_facts

    facts = extract_facts(diagnosis, regions, atlas_entries, ghidra_ast)
    register_facts = facts.by_kind("register_pressure")
    region_facts = facts.for_region(500, 510)
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional


# ---------------------------------------------------------------------------
# Dataclasses
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class TargetFact:
    """A single piece of evidence about a target function."""

    kind: str               # Fact type: mismatch_class, register_pressure, etc.
    region: tuple[int, int] | None  # Source line range (start, end) or None for global
    payload: dict           # Kind-specific structured data
    confidence: float       # 0.0-1.0
    provenance: str         # Where this fact came from

    @property
    def is_global(self) -> bool:
        """Whether this fact applies to the whole function."""
        return self.region is None


@dataclass
class TargetFacts:
    """Container for all facts about a target function.

    Supports querying by kind, region, and confidence threshold.
    """

    facts: list[TargetFact] = field(default_factory=list)

    def add(self, fact: TargetFact) -> None:
        self.facts.append(fact)

    def by_kind(self, kind: str) -> list[TargetFact]:
        """Return facts of a specific kind."""
        return [f for f in self.facts if f.kind == kind]

    def for_region(self, start: int, end: int) -> list[TargetFact]:
        """Return facts that overlap the given source line range."""
        result = []
        for f in self.facts:
            if f.region is None:
                result.append(f)  # Global facts apply everywhere
            elif f.region[0] <= end and f.region[1] >= start:
                result.append(f)
        return result

    def high_confidence(self, threshold: float = 0.7) -> list[TargetFact]:
        """Return facts above the confidence threshold."""
        return [f for f in self.facts if f.confidence >= threshold]

    def has_no_touch(self, start: int, end: int) -> bool:
        """Check if a region is marked as no-touch (unfixable)."""
        for f in self.by_kind("no_touch_zone"):
            if f.region and f.region[0] <= end and f.region[1] >= start:
                return True
        return False

    def pattern_recommendations(self) -> tuple[set[str], set[str]]:
        """Aggregate pattern boost/suppress from all facts.

        Returns (boost_set, suppress_set).
        """
        boost: set[str] = set()
        suppress: set[str] = set()
        for f in self.facts:
            boost.update(f.payload.get("boost_patterns", []))
            suppress.update(f.payload.get("suppress_patterns", []))
        return boost, suppress

    def summary_lines(self, max_shapes: int = 4) -> list[str]:
        """Return compact human-readable summary lines for debugging/reporting."""
        if not self.facts:
            return ["  Target facts: none"]

        lines: list[str] = []

        kind_counts: dict[str, int] = {}
        for fact in self.facts:
            kind_counts[fact.kind] = kind_counts.get(fact.kind, 0) + 1
        kind_parts = [
            f"{kind}={count}"
            for kind, count in sorted(kind_counts.items())
        ]
        lines.append(f"  Target facts: {', '.join(kind_parts)}")

        shape_categories: list[str] = []
        for fact in self.by_kind("codegen_shape"):
            category = fact.payload.get("shape_category")
            if category and category not in shape_categories:
                shape_categories.append(category)
        if shape_categories:
            shown = ", ".join(shape_categories[:max_shapes])
            extra = len(shape_categories) - min(len(shape_categories), max_shapes)
            if extra > 0:
                shown += f", +{extra} more"
            lines.append(f"  Codegen shapes: {shown}")

        boost, suppress = self.pattern_recommendations()
        if boost:
            shown = ", ".join(sorted(boost)[:max_shapes])
            extra = len(boost) - min(len(boost), max_shapes)
            if extra > 0:
                shown += f", +{extra} more"
            lines.append(f"  Pattern boosts: {shown}")
        if suppress:
            shown = ", ".join(sorted(suppress)[:max_shapes])
            extra = len(suppress) - min(len(suppress), max_shapes)
            if extra > 0:
                shown += f", +{extra} more"
            lines.append(f"  Pattern suppressions: {shown}")

        return lines


# ---------------------------------------------------------------------------
# Extractors
# ---------------------------------------------------------------------------

def extract_from_diagnosis(
    diagnosis,
    regions: list | None = None,
) -> list[TargetFact]:
    """Extract facts from a Diagnosis and MismatchRegion list.

    Produces: mismatch_class, register_pressure, no_touch_zone facts.
    """
    facts: list[TargetFact] = []

    if diagnosis is None:
        return facts

    # Global: register pressure from prologue analysis
    if diagnosis.has_prologue_mismatch:
        facts.append(TargetFact(
            kind="register_pressure",
            region=None,
            payload={
                "gpr_save_delta": diagnosis.gpr_save_delta,
                "fpr_save_delta": getattr(diagnosis, "fpr_save_delta", 0),
                "prologue_mismatch": True,
            },
            confidence=0.9,
            provenance="diagnosis.prologue",
        ))

    # Global: noise ratio
    if diagnosis.noise_total > 0:
        noise_ratio = diagnosis.noise_explained / max(1, diagnosis.noise_total)
        if noise_ratio > 0.8:
            facts.append(TargetFact(
                kind="mismatch_class",
                region=None,
                payload={
                    "class": "mostly_noise",
                    "noise_ratio": noise_ratio,
                    "noise_total": diagnosis.noise_total,
                },
                confidence=0.8,
                provenance="diagnosis.noise",
            ))

    # Global: register swap pairs
    if diagnosis.reg_swap_pairs:
        for pair in diagnosis.reg_swap_pairs:
            a, b = pair
            fixable = (a.startswith("r") and int(a[1:]) >= 13) or \
                      (a.startswith("f") and int(a[1:]) >= 14)
            facts.append(TargetFact(
                kind="register_pressure",
                region=None,
                payload={
                    "swap": list(pair),
                    "fixable": fixable,
                    "callee_saved": fixable,
                },
                confidence=0.85,
                provenance="diagnosis.regswaps",
            ))

    # Region-level facts from MismatchRegion data
    if regions:
        for region in regions:
            if not hasattr(region, 'start_line'):
                continue
            line_range = (region.start_line, region.end_line)

            facts.append(TargetFact(
                kind="mismatch_class",
                region=line_range,
                payload={
                    "class": region.dominant_type,
                    "mismatch_count": region.mismatch_count,
                    "total_instructions": region.total_instructions,
                    "match_ratio": region.match_ratio,
                },
                confidence=0.85,
                provenance="attribution.region",
            ))

    return facts


def extract_from_atlas(
    atlas_entries: list,
    regions: list | None = None,
) -> list[TargetFact]:
    """Extract facts from atlas lookup results.

    Produces: mismatch_class facts with pattern boost/suppress recommendations.
    """
    facts: list[TargetFact] = []

    for entry in atlas_entries:
        confidence_map = {
            "proven": 0.95,
            "inferred": 0.7,
            "negative": 0.9,
        }
        conf = confidence_map.get(entry.confidence.value, 0.5)

        payload = {
            "atlas_name": entry.name,
            "source_feature": entry.source_feature,
            "fixable": entry.fixable,
            "gap_estimate": entry.gap_estimate,
        }

        if entry.fixable and entry.pattern_names:
            payload["boost_patterns"] = list(entry.pattern_names)
        if not entry.fixable:
            payload["suppress_patterns"] = list(entry.pattern_names)

        kind = "no_touch_zone" if not entry.fixable else "mismatch_class"

        facts.append(TargetFact(
            kind=kind,
            region=None,  # Atlas entries are global (no region info yet)
            payload=payload,
            confidence=conf,
            provenance=f"atlas.{entry.name}",
        ))

    return facts


def extract_from_guidance(
    ghidra_ast=None,
    m2c_code: Optional[str] = None,
    rb3_source: Optional[str] = None,
) -> list[TargetFact]:
    """Extract facts from Ghidra, m2c, and RB3 guidance sources.

    Produces: control_shape, call_order facts.
    """
    facts: list[TargetFact] = []

    if ghidra_ast is not None:
        # Extract control flow shape from Ghidra AST
        try:
            from .ghidra_ast import (
                extract_savegpr_count,
                extract_variable_first_use_order,
            )
            var_order = extract_variable_first_use_order(ghidra_ast)
            if var_order:
                facts.append(TargetFact(
                    kind="call_order",
                    region=None,
                    payload={
                        "variable_order": var_order,
                        "source": "ghidra",
                    },
                    confidence=0.75,
                    provenance="ghidra.variable_order",
                ))
        except Exception:
            pass

    if rb3_source:
        facts.append(TargetFact(
            kind="control_shape",
            region=None,
            payload={
                "has_rb3_reference": True,
                "rb3_length": len(rb3_source),
            },
            confidence=0.6,
            provenance="rb3.source",
        ))

    return facts


def extract_from_shape_facts(
    shape_facts: list[dict] | None = None,
    diagnosis=None,
) -> list[TargetFact]:
    """Extract facts from derived PPC codegen shapes.

    Produces: codegen_shape facts with pattern boost/suppress recommendations.
    """
    facts: list[TargetFact] = []
    if not shape_facts:
        return facts

    target_ops = {
        d.target_opcode
        for d in getattr(diagnosis, "diff_ops", []) or []
        if getattr(d, "target_opcode", None)
    }
    diff_ops = list(getattr(diagnosis, "diff_ops", []) or [])

    target_fusion_ops = {"extrwi", "clrlslwi", "rlwinm"}
    target_separate_ops = {"srwi", "slwi", "clrlwi"}
    bool_target_markers = {
        "addic", "subfe", "cntlzw", "rlwinm", "addi", "neg", "andc",
        "srwi", "subfic", "subfc", "eqv", "addze", "srawi", "adde", "subfze",
    }
    target_switch_markers = (
        "bctr", "mtctr", "lwzx", "bdz", "bdnz",
    )
    target_compare_chain_markers = (
        "cmpwi", "cmplwi", "cmpw", "cmplw",
        "beq", "bne", "blt", "bgt", "ble", "bge",
    )

    def _has_target_prefix(*prefixes: str) -> bool:
        return any(
            any(op.startswith(prefix) for prefix in prefixes)
            for op in target_ops
        )

    target_prefers_tail_call = any(
        getattr(d, "target_opcode", None) == "b"
        and getattr(d, "base_opcode", None) == "bl"
        for d in diff_ops
    ) or bool(
        diagnosis
        and getattr(diagnosis, "has_prologue_mismatch", False)
        and (
            getattr(diagnosis, "gpr_save_delta", 0) < 0
            or getattr(diagnosis, "fpr_save_delta", 0) < 0
        )
    )
    target_prefers_non_tail_call = any(
        getattr(d, "target_opcode", None) == "bl"
        and getattr(d, "base_opcode", None) == "b"
        for d in diff_ops
    ) or bool(
        diagnosis
        and getattr(diagnosis, "has_prologue_mismatch", False)
        and (
            getattr(diagnosis, "gpr_save_delta", 0) > 0
            or getattr(diagnosis, "fpr_save_delta", 0) > 0
        )
    )

    for shape in shape_facts:
        kind = shape.get("kind")
        category = shape.get("category")
        confidence = float(shape.get("confidence", 0.7))
        payload = {
            "shape_kind": kind,
            "shape_category": category,
        }

        if kind == "byte_fusion":
            if category in ("fused_shr_mask", "fused_shl_mask"):
                if target_ops & target_separate_ops:
                    payload["boost_patterns"] = ["u8_to_unsigned_long"]
                elif target_ops & target_fusion_ops:
                    payload["suppress_patterns"] = ["u8_to_unsigned_long"]
            elif category == "separate_shift_and_mask":
                if target_ops & target_fusion_ops:
                    # Target wants fused ops but base has separate — keep separate
                    payload["suppress_patterns"] = ["u8_to_unsigned_long"]
        elif kind == "bool_materialization":
            if target_ops & bool_target_markers:
                payload["boost_patterns"] = ["bool_materialize"]
                if category in ("signed_positive", "signed_ordered", "signed_greater_equal",
                                "unsigned_ordered", "unsigned_greater_equal"):
                    payload["boost_patterns"].append("signed_unsigned")

        elif kind == "switch_dispatch":
            payload["case_count"] = shape.get("case_count")
            if category in ("switch_table", "switch_ctr_chain"):
                payload["switch_table"] = True
                if _has_target_prefix(*target_compare_chain_markers):
                    # Base has switch table but target uses compare chain
                    payload["boost_patterns"] = ["switch_if_convert"]
                else:
                    # Base and target both use switch table — no conversion needed
                    payload["suppress_patterns"] = ["switch_if_convert"]
            elif category == "switch_if_chain":
                payload["switch_if_chain"] = True
                if _has_target_prefix(*target_switch_markers):
                    # Base uses if-chain but target has switch table markers
                    payload["boost_patterns"] = ["switch_if_convert"]
                else:
                    # No switch markers in target — suppress conversion
                    payload["suppress_patterns"] = ["switch_if_convert"]

        elif kind == "call_shape":
            if category == "tail_direct_call":
                payload["tail_call_friendly"] = True
                payload["suppress_patterns"] = ["tail_call_reorder"]
            elif category == "direct_call_return":
                if target_prefers_tail_call:
                    payload["boost_patterns"] = ["tail_call_reorder"]
                elif target_prefers_non_tail_call:
                    payload["suppress_patterns"] = ["tail_call_reorder"]
                else:
                    payload["boost_patterns"] = ["tail_call_reorder"]
            elif category == "call_sequence_return":
                payload["call_sequence"] = True
                if target_prefers_tail_call:
                    payload["boost_patterns"] = ["tail_call_reorder"]
                elif target_prefers_non_tail_call:
                    payload["suppress_patterns"] = ["tail_call_reorder"]
                else:
                    payload["boost_patterns"] = ["tail_call_reorder"]
            elif category == "cached_return_value":
                payload["cached_return_value"] = True
                if target_prefers_tail_call:
                    payload["boost_patterns"] = ["temp_elimination", "tail_call_reorder"]
                elif target_prefers_non_tail_call:
                    payload["boost_patterns"] = ["temp_elimination"]
                else:
                    payload["boost_patterns"] = ["temp_elimination", "tail_call_reorder"]

        elif kind == "virtual_dispatch":
            # Virtual call detected — capture slot detail for struct analysis
            payload["virtual_call"] = True
            if shape.get("slot_offset") is not None:
                payload["slot_offset"] = shape["slot_offset"]
            if shape.get("vbtable_offset") is not None:
                payload["vbtable_offset"] = shape["vbtable_offset"]
            if shape.get("has_vbtable_indirection"):
                payload["has_vbtable_indirection"] = True

        elif kind == "inline_wrapper":
            payload["wrapper_category"] = category
            if category in ("trivial_forwarding", "trivial_tail_forward"):
                # Wrapper detected — if target outlines but we inline,
                # boost noinline_stub to force outlined call
                payload["boost_patterns"] = ["noinline_stub"]
            elif category == "accessor_load":
                # Accessor pattern — relevant for outline-vs-inline decision
                if shape.get("member_offset") is not None:
                    payload["member_offset"] = shape["member_offset"]
            elif category == "return_forwarding":
                payload["boost_patterns"] = ["noinline_stub"]

        elif kind == "prologue_shape":
            # Register save metadata — inform register-pressure-aware patterns
            gprs = shape.get("callee_saved_gprs", 0)
            fprs = shape.get("callee_saved_fprs", 0)
            payload["callee_saved_gprs"] = gprs
            payload["callee_saved_fprs"] = fprs
            payload["stack_frame_size"] = shape.get("stack_frame_size", 0)
            if gprs >= 10:
                payload["boost_patterns"] = ["variable_extraction"]
            if fprs >= 4:
                payload.setdefault("boost_patterns", []).append("signed_unsigned")

        elif kind == "control_flow":
            if category == "cfg_complexity":
                payload["block_count"] = shape.get("block_count", 0)
                payload["loop_count"] = shape.get("loop_count", 0)
                payload["nesting_depth"] = shape.get("nesting_depth", 0)
            elif category == "counted_loop":
                payload["counted_loop"] = True
                payload["boost_patterns"] = ["foreach_to_dowhile"]

        elif kind == "argument_materialization":
            payload["call_target"] = shape.get("call_target")
            payload["arg_count"] = shape.get("arg_count", 0)
            payload["arg_strategy"] = category
            if category == "pre_computed":
                # Pre-computed args in callee-saved regs suggest declaration
                # order matters — the compiler allocated regs early
                payload["boost_patterns"] = ["declaration_reorder"]
            elif category == "mixed":
                # Mixed strategy hints at complex argument setup
                payload["boost_patterns"] = ["declaration_reorder"]
            elif category == "stack_spilled":
                # Stack-spilled args suggest many parameters or large structs
                payload["boost_patterns"] = ["variable_extraction"]

        elif kind == "sparse_switch":
            payload["switch_strategy"] = category
            payload["estimated_cases"] = shape.get("estimated_cases", 0)
            payload["compare_count"] = shape.get("compare_count", 0)
            payload["depth"] = shape.get("depth", 0)
            if category == "linear_scan":
                # Linear scan = if-else chain, boost switch_if_convert
                payload["boost_patterns"] = ["switch_if_convert"]
            elif category == "binary_search":
                # Binary search is a compiler optimization; suppress conversion
                payload["suppress_patterns"] = ["switch_if_convert"]
            elif category == "hybrid":
                # Hybrid is mixed; mild boost for conversion
                payload["boost_patterns"] = ["switch_if_convert"]

        elif kind == "float_fusion":
            payload["fma_count"] = shape.get("count", 0)

        elif kind == "float_conversion":
            payload["conversion_pattern"] = shape.get("category")

        elif kind == "operation_profile":
            payload["total_ops"] = shape.get("total_ops", 0)
            payload["direct_calls"] = shape.get("direct_calls", 0)
            payload["indirect_calls"] = shape.get("indirect_calls", 0)
            payload["float_ops"] = shape.get("float_ops", 0)

        facts.append(TargetFact(
            kind="codegen_shape",
            region=None,
            payload=payload,
            confidence=confidence,
            provenance=f"ppc_shape.{kind}.{category}",
        ))

    return facts


# ---------------------------------------------------------------------------
# Convenience: extract all facts at once
# ---------------------------------------------------------------------------

def extract_facts(
    diagnosis=None,
    regions: list | None = None,
    atlas_entries: list | None = None,
    shape_facts: list[dict] | None = None,
    ghidra_ast=None,
    m2c_code: Optional[str] = None,
    rb3_source: Optional[str] = None,
) -> TargetFacts:
    """Extract facts from all available evidence sources.

    Returns a TargetFacts container with all extracted facts.
    """
    container = TargetFacts()

    for fact in extract_from_diagnosis(diagnosis, regions):
        container.add(fact)

    if atlas_entries:
        for fact in extract_from_atlas(atlas_entries):
            container.add(fact)

    for fact in extract_from_shape_facts(shape_facts, diagnosis=diagnosis):
        container.add(fact)

    for fact in extract_from_guidance(ghidra_ast, m2c_code, rb3_source):
        container.add(fact)

    return container
