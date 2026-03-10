"""Compiler Atlas — structured knowledge base of instruction→source mappings.

Machine-readable atlas of known instruction-pattern to source-feature
relationships. Entries come from differential testing (FINDINGS_SUMMARY.md),
documented patterns (TECHNICAL_NOTES.md), and unfixable patterns
(unfixable-compiler.md).

This is Synthesis Engine Phase 3 — see docs/plans/synthesis-engine/ROADMAP.md.

Usage:
    from scripts.permuter.compiler_atlas import atlas, lookup, boost_patterns

    # Look up entries matching a target opcode sequence
    entries = lookup(["subf.", "ble"])

    # Get pattern boost/suppress recommendations
    boosts, suppresses = boost_patterns(entries)
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Optional


class Confidence(Enum):
    """How confident we are in this atlas entry."""
    PROVEN = "proven"       # Confirmed via differential testing or controlled experiments
    INFERRED = "inferred"   # Documented from decomp experience, not controlled test
    NEGATIVE = "negative"   # Confirmed no source-level fix (AT_LIMIT)


@dataclass(frozen=True)
class AtlasEntry:
    """A single instruction-pattern → source-feature mapping."""

    name: str                           # Short descriptive name
    opcodes: tuple[str, ...]            # Instruction opcodes that signal this pattern
    source_feature: str                 # C++ construct that triggers it
    example_fix: str                    # Concrete code change (empty for negatives)
    confidence: Confidence              # How well proven
    gap_estimate: str                   # Typical match% loss: "<1%", "1-3%", "5-20%"
    pattern_names: tuple[str, ...]      # Permuter patterns that can fix it
    provenance: str                     # Where this was documented/proven
    fixable: bool                       # Whether source-level fix is possible
    tags: frozenset[str] = frozenset()  # Additional categorization tags


# ---------------------------------------------------------------------------
# Atlas Entries
# ---------------------------------------------------------------------------

_ENTRIES: list[AtlasEntry] = []


def _add(name, opcodes, source_feature, example_fix, confidence, gap,
         patterns, provenance, fixable=True, tags=()):
    """Helper to add an entry to the atlas."""
    _ENTRIES.append(AtlasEntry(
        name=name,
        opcodes=tuple(opcodes),
        source_feature=source_feature,
        example_fix=example_fix,
        confidence=confidence,
        gap_estimate=gap,
        pattern_names=tuple(patterns),
        provenance=provenance,
        fixable=fixable,
        tags=frozenset(tags),
    ))


# ── Boolean Materialization (PROVEN via diff-test) ──────────────────────────

_add(
    "bool_zero_test",
    ("addic", "subfe"),
    "Zero test: (x == 0) or (x != 0) for any type",
    "Use ternary: (x != 0) ? 1 : 0",
    Confidence.PROVEN, "<1%",
    ("bool_materialize",),
    "FINDINGS_SUMMARY.md §Boolean Materialization",
    tags=("bool",),
)

_add(
    "bool_equality_nonzero",
    ("addi", "cntlzw", "rlwinm"),
    "Equality to non-zero constant: (x == N) ? 1 : 0",
    "Use explicit ternary with constant comparison",
    Confidence.PROVEN, "<1%",
    ("bool_materialize", "comparison_equivalence"),
    "FINDINGS_SUMMARY.md §Boolean Materialization",
    tags=("bool",),
)

_add(
    "bool_inequality_nonzero",
    ("addi", "addic", "subfe"),
    "Inequality to non-zero: (x != N) ? 1 : 0",
    "Use explicit ternary with constant comparison",
    Confidence.PROVEN, "<1%",
    ("bool_materialize",),
    "FINDINGS_SUMMARY.md §Boolean Materialization",
    tags=("bool",),
)

_add(
    "bool_signed_positive",
    ("neg", "andc", "srwi"),
    "Signed positive test: (int x > 0) ? 1 : 0",
    "Use x > 0 with signed int type",
    Confidence.PROVEN, "<1%",
    ("bool_materialize", "signed_unsigned"),
    "FINDINGS_SUMMARY.md §Boolean Materialization",
    tags=("bool", "signedness"),
)

_add(
    "bool_unsigned_ordered",
    ("subfic", "subfe", "clrlwi"),
    "Unsigned ordered comparison: (unsigned x > N) where N > 0",
    "Ensure operand is unsigned type",
    Confidence.PROVEN, "1-3%",
    ("signed_unsigned", "bool_materialize"),
    "FINDINGS_SUMMARY.md §Boolean Materialization",
    tags=("bool", "signedness"),
)

_add(
    "bool_signed_ordered",
    ("li", "subfc", "eqv", "srwi", "addze", "clrlwi"),
    "Signed ordered comparison: (int x > N) where N > 0",
    "Ensure operand is signed int type",
    Confidence.PROVEN, "1-3%",
    ("signed_unsigned", "bool_materialize"),
    "FINDINGS_SUMMARY.md §Boolean Materialization",
    tags=("bool", "signedness"),
)

_add(
    "bool_cast_trigger",
    ("subfc", "eqv", "srwi", "addze", "clrlwi."),
    "(bool) cast forces branchless materialization with short-circuit",
    "a && (bool)(x > 1) for branchless + short-circuit",
    Confidence.PROVEN, "1-3%",
    ("bool_materialize",),
    "MEMORY.md §Boolean materialization proven",
    tags=("bool",),
)

# ── Comparison & Branch Patterns (PROVEN) ───────────────────────────────────

_add(
    "unsigned_zero_comparison",
    ("ble",),
    "Unsigned x > 0 generates ble; x != 0 generates beq",
    "Change x != 0 to x > 0 for unsigned types",
    Confidence.PROVEN, "<1%",
    ("comparison_equivalence", "signed_unsigned"),
    "TECHNICAL_NOTES.md §Unsigned Zero Comparisons",
    tags=("comparison",),
)

_add(
    "subf_loop_condition",
    ("subf.",),
    "while (hi - lo >= 0) generates subf.; while (hi >= lo) generates cmpw",
    "Write while (hi - lo >= 0) for subf. fusion",
    Confidence.PROVEN, "<1%",
    ("loop_condition_subtract",),
    "FINDINGS_SUMMARY.md §Record-Form Fusion + MEMORY.md",
    tags=("loop", "comparison"),
)

_add(
    "branch_polarity_inversion",
    ("bne", "beq", "ble", "bge", "blt", "bgt"),
    "Compiler always inverts: if (x) → bne (skip if NOT x)",
    "Branch polarity is compiler-determined, not source-fixable in isolation",
    Confidence.PROVEN, "1-3%",
    ("branch_polarity",),
    "FINDINGS_SUMMARY.md §Branch Polarity",
    tags=("branch",),
)

_add(
    "division_signedness",
    ("divw",),
    "Signed division by power of 2; unsigned uses srwi",
    "Cast sizeof() to (int) or use unsigned type",
    Confidence.PROVEN, "<1%",
    ("signed_unsigned",),
    "TECHNICAL_NOTES.md §Division Patterns",
    tags=("signedness",),
)

# ── Register Allocation (PROVEN) ───────────────────────────────────────────

_add(
    "regalloc_decl_order",
    ("__savegprlr",),
    "First declared var → r31, second → r30, etc. (linear scan)",
    "Reorder declarations to match target register assignment",
    Confidence.PROVEN, "1-5%",
    ("declaration_reorder", "declaration_movement"),
    "FINDINGS_SUMMARY.md §Register Allocation Ordering",
    tags=("regalloc",),
)

_add(
    "regalloc_prologue_mismatch",
    ("__savegprlr",),
    "Different callee-saved count causes all registers to shift by ±1",
    "Add/remove variable to match callee-saved count, or kill live range",
    Confidence.PROVEN, "1-5%",
    ("variable_extraction", "declaration_reorder"),
    "MEMORY.md §Prologue Mismatch Fixes",
    tags=("regalloc", "prologue"),
)

# ── Float Patterns (PROVEN) ────────────────────────────────────────────────

_add(
    "float_literal_precision",
    ("lfs",),
    "0.001f (float) → lfs; 0.001 (double) → lfd",
    "Use f suffix for float literals: 0.001f not 0.001",
    Confidence.PROVEN, "<1%",
    ("float_literal_pressure",),
    "FINDINGS_SUMMARY.md §Float Precision",
    tags=("float",),
)

_add(
    "fma_fusion",
    ("fmadds",),
    "a*b + c can fuse to fmadds; separate fmuls+fadds when not",
    "Restructure expression or use #pragma fp_contract",
    Confidence.PROVEN, "<1%",
    ("fma_reorder",),
    "TECHNICAL_NOTES.md §FMA Fusion",
    tags=("float",),
)

# ── Peephole Patterns (PROVEN) ─────────────────────────────────────────────

_add(
    "nor_peephole",
    ("not",),
    "u8 XOR 0xFF triggers NOR peephole (not instruction)",
    "Widen to u32 before XOR: u32 w32 = w; w32 ^ 0xFF",
    Confidence.PROVEN, "<1%",
    (),
    "TECHNICAL_NOTES.md §NOR Peephole + FINDINGS_SUMMARY.md",
    tags=("peephole",),
)

_add(
    "bit_test_materialize",
    ("rlwinm.",),
    "Bit test in-place: if ((flags & MASK)) uses rlwinm.",
    "Use (flags & MASK) != 0 with bool type for extrwi. form",
    Confidence.PROVEN, "<1%",
    ("bool_materialize",),
    "FINDINGS_SUMMARY.md §Bit Test Materialization",
    tags=("bool", "peephole"),
)

# ── Inlining (PROVEN) ─────────────────────────────────────────────────────

_add(
    "inlining_threshold",
    (),  # No specific opcode — inlining eliminates call entirely
    "Callee with weighted cost ~40 units gets inlined (branch=8x arithmetic)",
    "Add complexity to prevent inlining, or __forceinline to force it",
    Confidence.PROVEN, "varies",
    (),
    "FINDINGS_SUMMARY.md §Inlining Threshold",
    tags=("inlining",),
)

# ── Data Layout (INFERRED) ─────────────────────────────────────────────────

_add(
    "member_bitwidth",
    ("sth",),
    "u16 member stored as sth; u32 as stw. Wrong type = wrong instruction",
    "Fix struct member type to match target's load/store width",
    Confidence.INFERRED, "<1%",
    (),
    "TECHNICAL_NOTES.md §Data Type Sizing",
    tags=("data_layout",),
)

_add(
    "empty_vs_size",
    ("divw",),
    ".size() == 0 generates divw; .empty() generates cmplw only",
    "Use .size() == 0 if target has divw, .empty() if target has cmplw",
    Confidence.INFERRED, "<1%",
    ("empty_size_swap",),
    "TECHNICAL_NOTES.md §STL Size Patterns",
    tags=("stl",),
)

_add(
    "string_iteration_signedness",
    ("extsb",),
    "Iterating char* generates extsb; unsigned char* avoids it",
    "Cast to (const unsigned char*) for string iteration",
    Confidence.INFERRED, "<1%",
    ("signed_unsigned",),
    "TECHNICAL_NOTES.md §String Iteration",
    tags=("signedness",),
)

# ── Unfixable Patterns (NEGATIVE) ──────────────────────────────────────────

_add(
    "volatile_regswap",
    ("mr",),
    "Volatile GPR swaps (r0-r12) from compiler-internal allocation",
    "",
    Confidence.NEGATIVE, "1-3%",
    (),
    "unfixable-compiler.md §Volatile Register Swaps",
    fixable=False,
    tags=("regalloc", "unfixable"),
)

_add(
    "callee_saved_regswap_bsf",
    ("mr",),
    "Callee-saved GPR swaps locked by BSF graph coloring interference",
    "",
    Confidence.NEGATIVE, "1-3%",
    (),
    "unfixable-compiler.md §Callee-Saved Register Swaps",
    fixable=False,
    tags=("regalloc", "unfixable"),
)

_add(
    "address_reloc_noise",
    ("lis", "addi"),
    "Global address differences from .text section size delta (~18KB)",
    "",
    Confidence.NEGATIVE, "0.5-2%",
    (),
    "unfixable-compiler.md §Address Relocation",
    fixable=False,
    tags=("reloc", "unfixable"),
)

_add(
    "static_guard_naming",
    ("lwz", "stw"),  # Guard check/set pattern
    "??_B vs $S static guard naming from MSVC version difference",
    "",
    Confidence.NEGATIVE, "1-3%",
    (),
    "unfixable-compiler.md §Static Guard Naming + MEMORY.md",
    fixable=False,
    tags=("static", "unfixable"),
)

_add(
    "scheduler_store_reload",
    ("stw", "lwz"),
    "Instruction scheduler aliasing fence (store-reload sequence)",
    "",
    Confidence.NEGATIVE, "0.5-1%",
    (),
    "unfixable-compiler.md §Scheduler Fences",
    fixable=False,
    tags=("scheduling", "unfixable"),
)

_add(
    "bss_zero_elision",
    ("stfs",),
    "Target skips storing 0.0f to BSS static (already zero); we emit stfs",
    "",
    Confidence.NEGATIVE, "<1%",
    (),
    "MEMORY.md §BSS zero-elision",
    fixable=False,
    tags=("float", "unfixable"),
)

_add(
    "vbtable_recompute",
    ("lwz",),
    "Virtual base iteration: target recomputes vbase each iter, we cache ritEnd",
    "",
    Confidence.NEGATIVE, "1-2%",
    (),
    "MEMORY.md §vbase Recomputation vs Cached ritEnd",
    fixable=False,
    tags=("vtable", "unfixable"),
)

_add(
    "fma_mixed_direction",
    ("fmadds", "fmuls", "fadds"),
    "Mixed FMA direction in same function: some expressions fuse, others don't",
    "",
    Confidence.NEGATIVE, "1-3%",
    (),
    "unfixable-compiler.md §Mixed FMA Direction",
    fixable=False,
    tags=("float", "unfixable"),
)

_add(
    "bool_subic_subfe",
    ("subic", "subfe"),
    "Carry-based bool materialization (target) vs sign-bit-based (our compiler)",
    "",
    Confidence.NEGATIVE, "varies",
    (),
    "MEMORY.md §Boolean materialization: subic/subfe vs neg/andc/srwi",
    fixable=False,
    tags=("bool", "unfixable"),
)


# ── rlwinm Fusion / IL Type Control (PROVEN via ByteGrinder) ────────────────

_add(
    "rlwinm_fusion_extrwi",
    ("extrwi",),
    "u8 intermediate type causes G5P10 to fuse shift+mask into rlwinm (extrwi form)",
    "Use unsigned long intermediates + (int)((expr) & 0xFF) return instead of u8()",
    Confidence.PROVEN, "5-20%",
    ("u8_to_unsigned_long",),
    "IL_TYPE_CONTROL.md — u8() CAST in IL triggers fusion, &0xFF AND prevents it",
    fixable=True,
    tags=("byte", "shift", "type", "rlwinm"),
)

_add(
    "rlwinm_fusion_clrlslwi",
    ("clrlslwi",),
    "u8 intermediate type causes G5P10 to fuse shift+mask into rlwinm (clrlslwi form)",
    "Use unsigned long intermediates + (int)((expr) & 0xFF) return instead of u8()",
    Confidence.PROVEN, "5-20%",
    ("u8_to_unsigned_long",),
    "IL_TYPE_CONTROL.md — u8() CAST in IL triggers fusion, &0xFF AND prevents it",
    fixable=True,
    tags=("byte", "shift", "type", "rlwinm"),
)

_add(
    "u8_backward_propagation",
    ("extrwi", "clrlslwi", "clrlwi"),
    "u8() cast on XOR/OR/ADD result propagates backward, masking all operands early",
    "Use & 0xFF at the end instead of u8() cast: (int)((a ^ b) & 0xFF)",
    Confidence.PROVEN, "5-20%",
    ("u8_to_unsigned_long",),
    "IL_TYPE_CONTROL.md — CAST propagates backward through binary ops; AND stays local",
    fixable=True,
    tags=("byte", "type", "rlwinm", "xor"),
)

# ── Virtual Dispatch / Inline Wrapper (PROVEN via accessor outline fix) ───

_add(
    "vtable_slot_dispatch",
    ("lwz", "mtctr", "bctrl"),
    "Virtual method call via vtable slot — slot offset identifies which method",
    "",
    Confidence.INFERRED, "varies",
    (),
    "MEMORY.md §vbase Recomputation, IL_FORMAT.md §VCALL_SETUP",
    fixable=False,
    tags=("vtable", "virtual"),
)

_add(
    "accessor_inline_vs_outline",
    ("bl", "lwz"),
    "Target calls accessor via bl but our compiler inlines it (direct lwz load)",
    "Move accessor body from header to .cpp to force outlined call",
    Confidence.PROVEN, "1-5%",
    ("noinline_stub",),
    "MEMORY.md §Accessor outline fix — UIListSlot::Draw 96.6→100%",
    fixable=True,
    tags=("inline", "accessor", "header"),
)

_add(
    "trivial_forwarding_wrapper",
    ("bl", "blr"),
    "Trivial forwarding function: single call + return, may inline differently",
    "Use noinline_stub to control inlining boundary",
    Confidence.INFERRED, "1-5%",
    ("noinline_stub",),
    "INLINER_RE.md §threshold 150 IL nodes",
    fixable=True,
    tags=("inline", "wrapper"),
)

# ── Entries harvested from docs/decomp/patterns/*.md ─────────────────────

# ── Casting Patterns (PROVEN) ─────────────────────────────────────────────

_add(
    "noreturn_dead_code",
    ("blr",),
    "__declspec(noreturn) eliminates dead epilogue code after exit/abort",
    "Add __declspec(noreturn) to functions that never return",
    Confidence.PROVEN, "5-38%",
    (),
    "fixable-casting.md §noreturn Attribute",
    tags=("attribute",),
)

_add(
    "float_double_separation",
    ("lfs", "lfd", "frsp"),
    "Mixed float/double ops cause FPU spillage; separate precision with intermediates",
    "Separate float and double ops with explicit intermediate variables",
    Confidence.PROVEN, "5-80%",
    ("float_literal_pressure",),
    "fixable-casting.md §Float/Double Separation",
    tags=("float",),
)

_add(
    "cast_placement_fmul_fmuls",
    ("fmul", "fmuls"),
    "Moving (double)(float) cast boundary changes fmul vs fmuls opcode selection",
    "Wrap subexpression: (double)(float)((a * b + c) * d) keeps inner chain single-precision",
    Confidence.PROVEN, "1-13%",
    (),
    "fixable-casting.md §Cast Placement Controls fmul vs fmuls",
    tags=("float",),
)

_add(
    "sizeof_signed_cast",
    ("srawi", "addze"),
    "sizeof() is unsigned (srwi); cast to (int)sizeof() for signed division (srawi+addze)",
    "Cast sizeof() to (int) in signed arithmetic contexts",
    Confidence.PROVEN, "<1%",
    ("sizeof_signed_cast", "signed_unsigned"),
    "fixable-casting.md §sizeof() Signedness",
    tags=("signedness",),
)

_add(
    "dynamic_cast_avoidance",
    ("bl",),
    "Obj<T>() adds dynamic_cast the target doesn't have; use GetObj() directly",
    "Replace a->Obj<Hmx::Object>(2) with a->GetObj(2)",
    Confidence.PROVEN, "5-7%",
    (),
    "fixable-casting.md §Avoid Unnecessary dynamic_cast",
    tags=("cast",),
)

_add(
    "makestring_template_type",
    ("bl",),
    "MILO macro with Symbol args generates different MakeString template than const char*",
    "Convert Symbol args to const char* via .Str() before MILO macros",
    Confidence.PROVEN, "5-21%",
    (),
    "fixable-casting.md §MakeString Template Type Mismatch",
    tags=("template",),
)

_add(
    "float_int_reconversion",
    ("fctiwz", "stfd"),
    "Float-to-int-to-float reconversion: target does fctiwz+stfd, we do stfs directly",
    "Use (float)(int)pNode->Float() for truncation round-trip",
    Confidence.PROVEN, "2-8%",
    (),
    "fixable-casting.md §Float-to-Int-to-Float Reconversion",
    tags=("float",),
)

# ── Declaration Patterns (PROVEN) ─────────────────────────────────────────

_add(
    "explicit_destructor",
    ("bl",),
    "Missing explicit destructor generates atexit callback wrappers (~8 extra insns)",
    "Define empty destructor explicitly: ~GlitchFinder() {}",
    Confidence.PROVEN, "37-70%",
    (),
    "fixable-declarations.md §Explicit Destructor",
    tags=("dtor",),
)

_add(
    "precompute_ref_before_call",
    ("lwz", "addi"),
    "Compute derived ref before clobbering virtual call, not after (avoids reload)",
    "Movie &movie = obj->GetMovie(); obj->SetShowing(true); movie.Method();",
    Confidence.PROVEN, "5-18%",
    ("variable_extraction",),
    "fixable-declarations.md §Pre-Compute References Before Clobbering Calls",
    tags=("regalloc",),
)

_add(
    "hoist_sret_loop_var",
    ("stw", "lwz"),
    "Pre-declare sret variable before loop; inside-loop decl copies to stack slot",
    "Symbol s; for (...) { s = a->Sym(i); ... } instead of Symbol s = a->Sym(i) inside",
    Confidence.PROVEN, "5-6%",
    ("variable_extraction",),
    "fixable-declarations.md §Hoist Loop Variable for sret Register Matching",
    tags=("regalloc",),
)

_add(
    "braced_vs_braceless_scope",
    ("ori",),
    "Braced if increments MSVC scope counter; braceless does not. Affects static ?N? mangling",
    "Remove braces from single-statement if blocks before static declarations",
    Confidence.PROVEN, "<1%",
    (),
    "fixable-declarations.md §Braced vs Braceless If (Scope Counter)",
    tags=("static",),
)

_add(
    "function_def_order_guards",
    ("rlwinm.", "ori"),
    "Function definition order determines $S# guard counter across TU",
    "Reorder function definitions to match target's $S# numbering",
    Confidence.PROVEN, "3-5%",
    (),
    "fixable-declarations.md §Function Definition Order (TU-Wide Static Guard Counters)",
    tags=("static",),
)

_add(
    "alloca_intrinsic",
    ("bl",),
    "_alloca (intrinsic) generates inline stack probe; alloca (CRT) generates bl alloca",
    "Change alloca() to _alloca() when target uses _RtlCheckStack12",
    Confidence.PROVEN, "10-15%",
    ("alloca_intrinsic",),
    "fixable-declarations.md §alloca vs _alloca",
    tags=("intrinsic",),
)

_add(
    "bodyless_copy_ctor",
    ("bl",),
    "Declared-but-not-defined copy ctor suppresses implicit generation; template funcs drop to 0%",
    "Remove the bodyless copy ctor declaration; let compiler generate implicit one",
    Confidence.PROVEN, "varies",
    (),
    "fixable-copy-ctor.md §Bodyless Copy Constructor Declarations",
    tags=("dtor",),
)

# ── Control Flow Patterns (PROVEN) ────────────────────────────────────────

_add(
    "ternary_vs_ifelse",
    ("beq", "bne"),
    "Ternary generates different branch structure than if/else for simple conditionals",
    "Replace if/else with ternary for simple value/bool selection",
    Confidence.PROVEN, "5-74%",
    ("ternary_swap",),
    "fixable-control-flow.md §Ternary vs If-Else",
    tags=("branch",),
)

_add(
    "early_return_dtor_separation",
    ("bl",),
    "Early return forces separate destructor paths; if/else merges into shared epilogue",
    "Restructure to early-return so compiler emits separate dtor calls per exit path",
    Confidence.PROVEN, "10-16%",
    ("early_return_merge", "guard_to_nested"),
    "fixable-control-flow.md §Early Return for Destructor Path Separation",
    tags=("branch", "dtor"),
)

_add(
    "multiple_returns_to_or_chain",
    ("blt", "bne", "beq"),
    "Multiple if(cond) return false; → single || chain eliminates repeated inline sequences",
    "Combine independent guard conditions into single || chain with shared return",
    Confidence.PROVEN, "15-40%",
    ("early_return_merge",),
    "fixable-control-flow.md §Multiple Early Returns to || Chain",
    tags=("branch",),
)

_add(
    "bool_return_and_chain",
    ("neg", "andc", "srwi", "clrlwi"),
    "return A && B generates branchless bool materialization; if/return generates branches",
    "Convert if(cond) return false; return true; → return !cond;",
    Confidence.PROVEN, "5-15%",
    ("bool_materialize",),
    "fixable-control-flow.md §Bool Return Expression",
    tags=("bool", "branch"),
)

_add(
    "bitwise_bool_accumulator",
    ("and",),
    "Bitwise & for bool accumulation generates and instruction; && generates branches",
    "Use allRestricted &= check instead of allRestricted = allRestricted && check",
    Confidence.PROVEN, "10-15%",
    (),
    "fixable-control-flow.md §Bitwise & for Bool Accumulator",
    tags=("bool",),
)

_add(
    "local_bool_extraction",
    ("clrlwi",),
    "Extract complex condition to local bool forces clrlwi mask at assignment boundary",
    "bool shouldCheck = (complex || condition); if (shouldCheck) { ... }",
    Confidence.PROVEN, "5-8%",
    ("bool_materialize", "variable_extraction"),
    "fixable-control-flow.md §Local Bool Extraction for Complex Conditions",
    tags=("bool",),
)

# ── Operator Patterns (PROVEN) ────────────────────────────────────────────

_add(
    "fma_subtract_order",
    ("fmsubs", "fnmsubs"),
    "(x*y - 1.0f) → fmsubs; (1.0f - x*y) → fnmsubs — expression order selects variant",
    "Restructure float expression to match target's fmsubs/fnmsubs choice",
    Confidence.PROVEN, "1-4%",
    ("fma_reorder",),
    "fixable-operators.md §FMA Expression Order",
    tags=("float",),
)

_add(
    "negation_split_frsp",
    ("fneg", "frsp"),
    "-func() generates fneg before frsp; splitting generates frsp then fneg",
    "float angle = acos(x); angle = -angle; instead of float angle = -acos(x);",
    Confidence.PROVEN, "3-4%",
    ("negation_split",),
    "fixable-operators.md §Negation Splitting (fneg/frsp Scheduling)",
    tags=("float",),
)

_add(
    "byte_mask_extraction",
    ("rlwimi",),
    "Named variable for byte-mask breaks rlwimi recognition → separate ops instead",
    "unsigned long bw = u8(w); ret = bw | (bw << 8); instead of u8(w) | ((w<<8) & 0xFF00)",
    Confidence.PROVEN, "5-30%",
    ("byte_mask_extraction",),
    "fixable-operators.md §Byte Mask Extraction (rlwimi)",
    tags=("peephole",),
)

_add(
    "iterator_index_compare",
    ("subf", "clrrwi"),
    "Index-based iterator comparison (it-begin()) < (it2-begin()) vs direct it1 < it2",
    "return (it1 - vec.begin()) < (it2 - vec.begin()); for sort comparators",
    Confidence.PROVEN, "40-100%",
    ("iterator_index_compare",),
    "fixable-comparison.md §Iterator Index Comparison",
    tags=("stl",),
)

# ── Float Select Patterns (PROVEN) ───────────────────────────────────────

_add(
    "fsel_clamp_template",
    ("fsel", "fneg"),
    "Clamp/Min/Max<float> templates generate fsel; branched if generates fcmpu+branch",
    "Use Clamp(0.0f, 1.0f, val) or __fsel intrinsic for float clamping",
    Confidence.PROVEN, "5-53%",
    ("float_clamp",),
    "fixable-fsel-fma.md §fsel via Clamp/Min/Max Templates",
    tags=("float",),
)

_add(
    "fp_contract_pragma",
    ("fmuls", "fadds"),
    "#pragma fp_contract(off) prevents FMA fusion: a*b+c → fmuls+fadds instead of fmadds",
    "Add #pragma fp_contract(off) before function, #pragma fp_contract(on) after",
    Confidence.PROVEN, "1-12%",
    ("fp_contract",),
    "fixable-fsel-fma.md §FMA Control via #pragma fp_contract",
    tags=("float",),
)

# ── Comparison Patterns (PROVEN) ──────────────────────────────────────────

_add(
    "isnan_vs_threshold",
    ("fcmpu",),
    "IsNaN(x) → fcmpu fN,fN (self-compare); threshold → fcmpu fN,fM (constant compare)",
    "Replace IsNaN(x) with x < -0.0001f if target uses threshold check",
    Confidence.PROVEN, "3-5%",
    (),
    "fixable-comparison.md §IsNaN vs Threshold Check",
    tags=("float", "comparison"),
)

_add(
    "rlwimi_plus_avoidance",
    ("rlwimi", "rlwinm"),
    "| triggers rlwimi peephole for non-overlapping bit merge; + avoids it",
    "Use (x >> 16) + (s & 0x7FFF0000) instead of | to prevent rlwimi",
    Confidence.PROVEN, "2-3%",
    (),
    "fixable-comparison.md §rlwimi Peephole Avoidance",
    tags=("peephole",),
)

# ── Bool Mask Patterns (PROVEN) ───────────────────────────────────────────

_add(
    "extrwi_bool_type",
    ("extrwi",),
    "bool type triggers extract-to-LSB (extrwi); non-bool triggers mask-in-place (rlwinm)",
    "Use bool b = (flags & MASK) != 0; or bool(flags & MASK) for extrwi encoding",
    Confidence.PROVEN, "1-2%",
    ("bit_test_bool",),
    "fixable-bool-mask.md §extrwi vs rlwinm Bit Test Encoding",
    tags=("bool", "peephole"),
)

# ── Macro Patterns (PROVEN) ──────────────────────────────────────────────

_add(
    "handler_macro_extraction",
    ("bl",),
    "Manual _NEW_STATIC_SYMBOL+if handler vs HANDLE_ACTION macro — different inlining",
    "Extract handler body to method, use HANDLE_ACTION/HANDLE_EXPR macro",
    Confidence.PROVEN, "3-5%",
    (),
    "fixable-macros.md §Manual Handler Extraction",
    tags=("macro",),
)

# ── Additional Unfixable Patterns (NEGATIVE) from pattern docs ───────────

_add(
    "assert_revs_scheduling",
    ("subi", "addi"),
    "ASSERT_REVS/INIT_REVS instruction scheduling and gRevs base pointer choice differ",
    "",
    Confidence.NEGATIVE, "0.8-2.3%",
    (),
    "unfixable-compiler.md §ASSERT_REVS / INIT_REVS Scheduling",
    fixable=False,
    tags=("scheduling", "unfixable"),
)

_add(
    "fsel_register_pressure",
    ("fsel",),
    "fsel keeps float values alive longer, increasing FPR register pressure vs branched code",
    "",
    Confidence.NEGATIVE, "5-20%",
    (),
    "unfixable-compiler.md §fsel Register Pressure",
    fixable=False,
    tags=("float", "regalloc", "unfixable"),
)

_add(
    "anon_namespace_hash",
    ("bl",),
    "Anonymous namespace symbols have different ?A0x<hash>@@ between builds (cosmetic)",
    "",
    Confidence.NEGATIVE, "0.5-3%",
    (),
    "unfixable-compiler.md §Anonymous Namespace Hash Mismatch",
    fixable=False,
    tags=("reloc", "unfixable"),
)

_add(
    "dead_store_dtor_merge",
    ("li", "stw"),
    "Target merges explicit cleanup with destructor, eliminating null-out stores",
    "",
    Confidence.NEGATIVE, "1-2%",
    (),
    "unfixable-compiler.md §Dead Store Elimination / Destructor Merging",
    fixable=False,
    tags=("dtor", "unfixable"),
)

_add(
    "icf_linker_merged",
    ("bl",),
    "Identical COMDAT Folding merges functions with identical machine code to one address",
    "",
    Confidence.NEGATIVE, "0.5-3%",
    (),
    "at-limit-systemic.md §LINKER_MERGED / ICF",
    fixable=False,
    tags=("reloc", "unfixable"),
)

_add(
    "large_offset_addressing",
    ("lis", "ori", "lwzx"),
    "Offsets > 0x7FFF use lis+ori+lwzx (target) vs addis+subi (our compiler)",
    "",
    Confidence.NEGATIVE, "~30%",
    (),
    "unfixable-compiler.md §Large Offset Addressing",
    fixable=False,
    tags=("addressing", "unfixable"),
)

_add(
    "scalar_deleting_dtor",
    ("bl",),
    "Target generates ??_G scalar deleting destructor; we emit separate ~T + operator delete",
    "",
    Confidence.NEGATIVE, "~10%",
    (),
    "unfixable-compiler.md §Scalar Deleting Destructor",
    fixable=False,
    tags=("dtor", "unfixable"),
)

_add(
    "stack_spill_scheduling",
    ("stw",),
    "Target spills local to stack frame; our compiler keeps it in register (or vice versa)",
    "",
    Confidence.NEGATIVE, "1-2%",
    (),
    "unfixable-compiler.md §Stack Spill Scheduling",
    fixable=False,
    tags=("scheduling", "regalloc", "unfixable"),
)

_add(
    "bool_negation_subfic",
    ("subfic",),
    "Pointer negation generates subfic; bool negation generates subic — type dependent",
    "",
    Confidence.NEGATIVE, "3-8%",
    (),
    "unfixable-compiler.md §Boolean Negation: subfic vs subic",
    fixable=False,
    tags=("bool", "unfixable"),
)


# ── Switch Dispatch (PROVEN via binary patching + diff-test) ──────────────

_add(
    "switch_table_dispatch",
    ("bctr", "mtctr", "lwzx"),
    "Switch statement lowered to jump table: bctr dispatch via computed address",
    "Convert switch to if/else if chain when target uses compare chain",
    Confidence.PROVEN, "5-30%",
    ("switch_if_convert",),
    "PASS_GROUPS.md §G5P10, diff-test suite_scope_nesting",
    tags=("switch", "control_flow"),
)

_add(
    "switch_compare_chain",
    ("cmpwi", "beq", "bne"),
    "Switch lowered to compare chain: sequential cmpwi+beq/bne for each case",
    "Convert if/else if chain to switch when target uses jump table",
    Confidence.PROVEN, "5-30%",
    ("switch_if_convert",),
    "PASS_GROUPS.md §G5P10, diff-test suite_scope_nesting",
    tags=("switch", "control_flow"),
)

# ── Tail Call Optimization (PROVEN via decomp experience) ─────────────────

_add(
    "tail_call_b_vs_bl",
    ("b", "bl"),
    "Last call uses b (tail call) vs bl (normal call) — depends on call order at function end",
    "Reorder trailing calls so the tail-callable one is last",
    Confidence.PROVEN, "1-5%",
    ("tail_call_reorder",),
    "TECHNICAL_NOTES.md, COLOR_RE.md §prologue implications",
    tags=("tail_call", "control_flow"),
)

_add(
    "tail_call_prologue_delta",
    ("__savegprlr",),
    "Tail call saves fewer registers — __savegprlr_N vs __savegprlr_N+1",
    "Reorder calls at function end to enable tail-call optimization",
    Confidence.INFERRED, "1-3%",
    ("tail_call_reorder",),
    "COLOR_RE.md §prologue implications, INLINER_RE.md",
    tags=("tail_call", "prologue"),
)

# ---------------------------------------------------------------------------
# Opcode Index (built at import time)
# ---------------------------------------------------------------------------

# Maps each opcode to the list of entries that mention it
_OPCODE_INDEX: dict[str, list[AtlasEntry]] = {}

for _entry in _ENTRIES:
    for _op in _entry.opcodes:
        _OPCODE_INDEX.setdefault(_op, []).append(_entry)


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def all_entries() -> list[AtlasEntry]:
    """Return all atlas entries."""
    return list(_ENTRIES)


def lookup(
    target_opcodes: list[str],
    *,
    fixable_only: bool = False,
    include_negative: bool = True,
) -> list[AtlasEntry]:
    """Look up atlas entries matching any of the given target opcodes.

    Returns entries sorted by confidence (proven first) then by number
    of opcode matches (more specific matches first).

    Args:
        target_opcodes: Opcodes from the target binary's mismatch region.
        fixable_only: If True, exclude negative (unfixable) entries.
        include_negative: If False, exclude negative entries.
    """
    if not target_opcodes:
        return []

    opcode_set = set(target_opcodes)
    scored: list[tuple[int, int, AtlasEntry]] = []

    seen: set[str] = set()
    for op in opcode_set:
        for entry in _OPCODE_INDEX.get(op, []):
            if entry.name in seen:
                continue
            seen.add(entry.name)

            if fixable_only and not entry.fixable:
                continue
            if not include_negative and entry.confidence == Confidence.NEGATIVE:
                continue

            # Score: confidence tier + opcode overlap count
            conf_score = {
                Confidence.PROVEN: 3,
                Confidence.INFERRED: 2,
                Confidence.NEGATIVE: 1,
            }.get(entry.confidence, 0)
            overlap = len(opcode_set & set(entry.opcodes))
            scored.append((conf_score, overlap, entry))

    # Sort: highest confidence first, then most overlapping opcodes
    scored.sort(key=lambda x: (-x[0], -x[1]))
    return [e for _, _, e in scored]


def boost_patterns(entries: list[AtlasEntry]) -> tuple[set[str], set[str]]:
    """Compute pattern boost and suppress sets from atlas entries.

    Returns:
        (boost_set, suppress_set): Pattern names to boost or suppress.
        Boost = patterns from fixable entries with high confidence.
        Suppress = patterns mentioned in negative entries.
    """
    boost: set[str] = set()
    suppress: set[str] = set()

    for entry in entries:
        if entry.confidence == Confidence.NEGATIVE:
            # Negative entries suppress their (empty) pattern list
            # but more importantly signal that the mismatch is unfixable
            pass
        elif entry.fixable and entry.pattern_names:
            boost.update(entry.pattern_names)

    return boost, suppress


def lookup_for_diagnosis(
    diff_ops: list[str] | None = None,
    reg_swap_pairs: list[tuple[str, str]] | None = None,
    has_prologue_mismatch: bool = False,
) -> list[AtlasEntry]:
    """Look up atlas entries based on diagnosis signals.

    Convenience wrapper that maps Diagnosis fields to opcode lookups.
    """
    opcodes: list[str] = []

    if diff_ops:
        opcodes.extend(diff_ops)

    if reg_swap_pairs:
        for a, b in reg_swap_pairs:
            if a.startswith("r") and b.startswith("r"):
                opcodes.append("mr")  # register move
            elif a.startswith("f") and b.startswith("f"):
                opcodes.append("fmr")

    if has_prologue_mismatch:
        opcodes.append("__savegprlr")

    return lookup(opcodes)
