# Instruction Attribution Pipeline

This document describes how to connect mismatched instructions back to specific
source lines, enabling surgically targeted source edits instead of broad
pattern sweeps.

This builds on the existing `/FAs` assembly listing support in
[`tools/compiler_trace/invoker.py`](../../tools/compiler_trace/invoker.py) and
the `asm_listing` mode in
[`scripts/analysis/diff_inspect.py`](../../scripts/analysis/diff_inspect.py).

## The Problem

When objdiff reports a mismatch, it says: "instruction at offset 0x3C is
`subf.` in target but `cmpw` in ours." What it does not say is: "this
instruction was generated from line 47 of your source file."

Without attribution, the permuter applies all potentially relevant patterns to
the entire function body and hopes one of them changes the right instruction.
For a function with 200 instructions and 40 source lines, this means most
variant builds are wasted — they change parts of the source that have nothing
to do with the mismatched region.

With attribution, the engine knows: "the mismatch at offset 0x3C comes from
the comparison on line 47. Try comparison-related patterns on that specific
expression."

## What Already Exists

- **`/FAs` assembly listings**: MSVC's `/FAs` flag produces source-interleaved
  assembly. Each assembly block is preceded by the source line that generated
  it.
  [`invoker.py`](../../tools/compiler_trace/invoker.py) already supports this
  flag.

- **[`asm_regmap.py`](../../tools/compiler_trace/asm_regmap.py)**: Parses `/FAs`
  output to extract variable-to-register mappings. Already demonstrates that
  the listing format is machine-parseable.

- **[`asm_diff.py`](../../tools/compiler_trace/asm_diff.py)**: Compiles two
  source variants with `/FAs`, normalizes listings, diffs with register rename
  detection. The normalization logic is reusable for the attribution parser.

- **[`diff_inspect.py`](../../scripts/analysis/diff_inspect.py) `asm_listing`
  mode**: Compiles with `/FAs` and returns the annotated assembly. Already
  integrated as an orchestrator tool via `run_diff_inspect`.

- **[`scorer.py`](../../scripts/permuter/scorer.py)**: The permuter's build
  pipeline. Baseline computation already runs objdiff with
  `--include-instructions`, which provides per-instruction offset, opcode, and
  arguments for both target and base.

- **[`types.py`](../../scripts/permuter/types.py)** `Diagnosis` dataclass:
  already captures clusters, diff_ops, and prologue save counts. Mismatch
  regions would extend this with source-line attribution.

What is missing is the **join**: connecting objdiff's instruction-level
mismatch data with the `/FAs` listing's source-line annotations.

## Design

### Assembly Listing Parser

Parse `/FAs` output into a structured representation:

```
AsmListing:
  entries: list[AsmEntry]

AsmEntry:
  source_file: str
  source_line: int
  source_text: str
  instructions: list[AsmInstruction]

AsmInstruction:
  offset: int              # byte offset in .text
  opcode: str              # e.g., "cmpw", "subf.", "beq"
  operands: list[str]      # e.g., ["r3", "r4"]
  raw_text: str            # full line from listing
```

The parser should handle:
- Source line annotations (`;` comment lines with file:line info)
- Macro expansions (multiple source lines per instruction block)
- Inline function boundaries (when inlined code appears in the listing)
- Alignment/padding directives (skip these)

### Mismatch Attribution

Given objdiff's instruction-level diff and our parsed listing:

1. For each mismatched instruction in our output, look up its offset in the
   listing to find the responsible source line.

2. For each mismatched instruction in the target, we don't have a listing —
   but we can often infer the source region from:
   - Ghidra decompilation (which maps target offsets to decompiled lines)
   - Surrounding matched instructions (if instructions before and after a
     mismatch map to lines 45 and 49, the mismatch is likely lines 46-48)

3. Produce an attributed mismatch report:

```
AttributedMismatch:
  target_offset: int
  target_opcode: str
  base_offset: int
  base_opcode: str
  source_file: str
  source_line: int
  source_text: str
  mismatch_type: str       # "opcode", "register", "immediate", "missing"
  confidence: float        # how certain the attribution is
```

### Mismatch Region Aggregation

Individual instruction mismatches often cluster around the same source region.
Aggregate attributed mismatches into regions:

```
MismatchRegion:
  source_lines: range      # e.g., lines 45-49
  source_text: list[str]   # the actual source lines
  mismatches: list[AttributedMismatch]
  dominant_type: str        # most common mismatch type in region
  total_instructions: int   # how many instructions this region generates
  match_ratio: float        # fraction of instructions that match in region
```

This tells the engine: "Lines 45-49 are responsible for 12 mismatched
instructions. The dominant issue is opcode differences (comparison pattern).
The rest of the function matches perfectly."

### Targeted Pattern Application

With mismatch regions identified, the pattern generator can:

1. **Scope patterns to relevant regions**: Only apply `comparison_flip` to
   expressions within the identified mismatch region, not to every comparison
   in the function.

2. **Skip irrelevant patterns**: If the mismatch region contains no
   comparisons, don't try comparison patterns at all.

3. **Prioritize by region impact**: If region A has 12 mismatched instructions
   and region B has 2, allocate more budget to region A.

4. **Measure region-level improvement**: After applying a variant, check
   whether the specific region improved, not just the overall score. A variant
   that fixes region A but breaks region B has a net-zero score delta but is
   actually valuable — it solved A and the B regression is a separate problem.

### Integration With Search

The attribution pipeline slots into the synthesis engine at two points:

**At baseline time** (once per function):
- Compile with `/FAs`
- Parse listing
- Join with objdiff mismatches
- Produce attributed mismatch regions
- Store in FunctionContext / beam state

**At proposal time** (each round/expansion):
- Filter patterns by relevant regions
- Allocate budget proportional to region impact
- Generate region-scoped variants

**At evaluation time** (after scoring):
- Recompile improved variants with `/FAs`
- Re-attribute to check which regions actually improved
- Update region-level tracking in beam state

## Listing Parse Challenges

### Inlined Functions

When MSVC inlines a function, the listing shows the inlined code with source
annotations from the inlined function's file, not the caller's file. This
means a single listing can reference multiple source files.

The parser should track file transitions and attribute inlined instructions to
their original source location. For pattern application, inlined regions may
need header-level patterns rather than local patterns.

### Macro Expansions

MILO_ASSERT, SAVE_REVS, and other macros expand to multi-instruction sequences
that reference the macro definition site, not the call site. The parser should
detect known macro patterns and attribute to the call site.

### Optimization Reordering

The compiler may reorder instructions relative to source lines. The listing
preserves source annotations, but instruction order may not match source order.
Attribution should use a best-effort mapping that tolerates reordering within
a local window.

### PCH and Templates

Template instantiations may reference header source lines. The parser should
resolve these to the actual instantiation site in the source file when possible.

## Compile Time Impact

Adding `/FAs` to every permuter build would increase compile time. Mitigation:

- Generate listings only for baseline compilation and winning variants, not
  for every candidate. The listing is needed for attribution, not for scoring.
- Cache listings by source hash (same source = same listing).
- The `/FAs` overhead is modest — MSVC generates listings as a side effect of
  compilation, not as a separate pass.

Estimated overhead: ~10-15% compile time increase when listings are enabled.
Since only baseline + winning builds need listings, the amortized impact on
the permuter is <5%.

## Relationship To Other Systems

### Compiler Atlas

The atlas maps instruction patterns to source features. Attribution maps our
compiled instructions to source lines. Together:

1. Attribution identifies which source line produces a mismatched instruction
2. Atlas identifies what source change would produce the target instruction
3. The engine applies the atlas-suggested change at the attributed line

This is the full "inverse compilation" loop: mismatch → source line → target
instruction → atlas → source fix → verify.

### Target Facts Layer

Attribution enriches target facts with source-side grounding. Instead of "the
target has a `subf.` somewhere in this function," we get "the target has a
`subf.` that corresponds to our comparison on line 47." This makes target facts
actionable.

### Beam Search Diversity

Attribution enables region-aware diversity in beam search. Two beam states that
fix different mismatch regions are meaningfully diverse, even if their overall
scores are similar. The beam selector should preserve states that uniquely
improve specific regions.

## Implementation Plan

### Phase 1: Listing Parser

1. Build a robust `/FAs` listing parser in `tools/compiler_trace/`
2. Handle source annotations, inline boundaries, macro patterns
3. Produce `AsmListing` objects with source-line attribution
4. Test on 20-30 representative functions across different units

### Phase 2: Mismatch Join

1. Connect objdiff instruction output to parsed listing
2. Produce `AttributedMismatch` records
3. Aggregate into `MismatchRegion` objects
4. Add an `--attributed` mode to diff_inspect.py

### Phase 3: Permuter Integration

1. Add listing generation to scorer's baseline computation
2. Store mismatch regions in FunctionContext
3. Add region-aware filtering to pattern generators
4. Add region-level tracking to variant evaluation

### Phase 4: Region-Aware Search

1. Allocate proposal budget by region impact
2. Track region-level improvement across rounds
3. Preserve region-fixing states in beam search diversity
4. Report per-region progress in permuter output

## Expected Value

### Budget Efficiency

A function with 3 mismatch regions and 78 patterns currently tries all patterns
everywhere: 78 * (function scope) = 78 proposals. With attribution: 15 relevant
patterns * 3 regions = 45 proposals, each targeted. Fewer builds, higher hit
rate.

### Diagnostic Value

Even without search integration, attributed mismatch reports are useful for
manual decomp work. "Your mismatch is on line 47, a comparison expression"
is much more actionable than "your function is 94.2% matched."

### Composition Guidance

Attribution can guide composition: if region A needs a type change and region B
needs a control flow change, compose those two patterns (one per region) rather
than trying random pairs.

## Open Questions

- How reliable is MSVC's `/FAs` source attribution after optimization? Does
  `/O1` scramble the annotations enough to make attribution noisy?
- Should attribution be stored persistently (in decomp.db) or computed on
  demand? Persistent storage enables cross-session analysis but costs space.
- Can we get useful attribution from the target side without debug info? Ghidra
  provides line-level decompilation, but mapping decompiled lines to original
  source lines is lossy.
- Should region-level improvement be a first-class beam ranking signal, or
  just a diversity input?
