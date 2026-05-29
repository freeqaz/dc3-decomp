# Register Swap Source-Fix Roadmap

Systematic plan to fix register allocation mismatches from source code,
eliminating dependence on the post-build binary patcher.

**Goal**: Push AT_LIMIT functions to COMPLETE via source-level changes that
survive rebuilds. The `obj_regswap_patcher.py` bandaid patches are lost on
every rebuild. This workstream is the permanent fix.

**Last updated**: 2026-03-03

---

## The Landscape

### AT_LIMIT Breakdown (2,461 functions)

| Category | Count | Avg % | Description |
|----------|------:|------:|-------------|
| `unknown/untagged` | 1,210 | 37.1% | 289 at 99%+ (objdiff reset artifacts), 809 at 0% (stubs) |
| `addr_reloc_only` | 603 | 97.4% | Pure address relocation noise — no real code mismatch |
| `regswap+prologue` | 212 | 66.3% | Register swap + different number of saved registers |
| `regswap+ctrl_flow` | 139 | 70.4% | Register swap + branch polarity mismatch |
| `regswap_simple` | 126 | 86.1% | Register swap with only addr_reloc noise |
| `ctrl_flow_only` | 94 | 63.6% | Branch polarity only |
| `regswap+offset` | 54 | 81.9% | Register swap + field offset swap |
| `offset_only` | 16 | 86.7% | Field offset swap only |
| `prologue_only` | 6 | 74.5% | Different variable count only |
| `anon_ns_only` | 2 | 98.9% | Anonymous namespace hash mismatch |

### Register Swap Sub-Landscape (531 functions)

Match % distribution for AT_LIMIT functions with register swaps:

| Bucket | Count | Approachability |
|--------|------:|-----------------|
| 99-100% | 10 | **Easiest** — 1-2 instruction mismatch |
| 98-99% | 13 | Easy — likely 1 swap pair |
| 97-98% | 19 | Moderate — 1-2 swap pairs |
| 95-97% | 15 | Moderate |
| 90-95% | 72 | Hard — multiple issues |
| 80-90% | 82 | Hard — significant structural diff |
| <80% | 320 | Very hard — regswap is one of many problems |

**Pure regswap-only** (no other mismatch types): **34 functions** — cleanest targets.

### BSF Guidance Applicability

From empirical scanning of 100 AT_LIMIT+regswap functions:
- **17%** have BSF calls (addressable by BSF-guided declaration reorder)
- **83%** have 0 BSF calls (compiler uses simpler register allocator)
- Functions need ~7+ callee-saved variables to trigger graph-coloring BSF allocation
- **FPR allocation does NOT use BSF** — separate allocator, sequential by declaration order
- **FPR callee-saved swaps exist and are fixable**: ~25% of FPR-swapped functions have
  callee-saved (f14-f31) swaps addressable by float declaration reorder (confirmed 2026-03-03)

---

## Strategic Priorities

### P0: Data Hygiene (unblocks accurate measurement)

**Problem**: 1,210 "untagged" AT_LIMIT functions pollute metrics.
- 289 at 99%+ are likely COMPLETE (reset artifacts from `base_size=0` objdiff bug)
- 809 at 0% are genuine stubs (no source implementation)

**Action**: Batch-check the 289 high-% untagged functions with `batch_check`.
Auto-report 100% matches as COMPLETE. Reclassify stubs with `is_stub=1`.
This cleans the funnel so we can measure real progress.

**Impact**: Reduces real AT_LIMIT count from 2,461 to ~1,200-1,500.

### P1: Pure Noise Reclassification (603 functions)

**Problem**: 603 AT_LIMIT functions have ONLY address relocation mismatches —
no real code difference. These are functionally COMPLETE but objdiff counts
address relocation `diff_arg` against match%.

**Status**: objdiff MakeString ICF normalization (2026-03-03) already pushed
many of these to 100%. The remaining 603 have non-MakeString address relocs
(different symbol addresses between decomp and original binary).

**Possible approaches**:
1. Extend objdiff normalization to ignore all address relocation `diff_arg`
2. Add a `COMPLETE_WITH_NOISE` verdict tier
3. Accept these as inherent to decomp (different link order = different addresses)

**Impact**: If reclassified, would push us from 54% to ~55-56% fuzzy match.

### P2: Source-Level GPR Regswap Fixes (126-180 functions)

This is the core workstream. Target: `regswap_simple` (126) + high-% entries
from `regswap+ctrl_flow` and `regswap+offset` categories.

#### Tier 2a: BSF-Guided Declaration Reorder (17% of targets)

**Status**: IMPLEMENTED and validated (2026-03-03)
- Exact mangled symbol isolation working
- 3-way cyclic rotation support added
- Color-to-GPR mapping validated

**Limitation**: Only 17% of regswap functions have BSF calls. The rest use
a simpler allocator not traceable via BSF.

**Next step**: Run batch sweep on the 34 pure-regswap functions. Estimate
~30% success rate = ~10 functions fixed.

#### Tier 2b: Assembly-Listing-Based Reorder (all targets)

**Status**: NOT STARTED — highest-value new work item

**Concept**: Instead of BSF tracing (which only works for 17% of functions),
use the MSVC assembly listing directly to determine register-to-variable
mapping. The `/FAs` listing shows which register holds which variable:

```asm
; 42   :     int count = mList.size();
  00038 80 7f 00 48      lwz       r3,0x48(r31)     ; r3 = this->mList
  ; ...
  0003c 7c 7e 1b 78      mr        r30,r3           ; r30 = count
```

**Approach**:
1. Compile with `/FAs` to get interleaved source+asm listing
2. Parse source line annotations to map variables → registers
3. Compare our register assignments against target (objdiff) registers
4. Generate reorderings that produce the target assignment

**Advantages over BSF**:
- Works for ALL functions (not just the 17% with BSF calls)
- Directly observable mapping (no color→register indirection)
- Can handle FPR as well as GPR
- Deterministic and reproducible

**Challenges**:
- Parsing `/FAs` output reliably across different function styles
- Variables may be assigned to multiple registers at different points
- Inlined functions confuse the source-line mapping

**Implementation plan**:
1. Build `/FAs` parser that extracts `{source_line, register, variable_name}` tuples
2. Build register assignment comparator (our assignment vs target)
3. Integrate into `declaration_reorder.py` as a new guidance mode
4. Test on the 34 pure-regswap functions

#### Tier 2c: FPR Declaration Reorder

**Status**: RESEARCH COMPLETE — implementation needed (2026-03-03)

**Confirmed**: Float variable declaration order DOES control FPR (f14-f31)
assignment, same pattern as GPR:
- First float declared → f31, second → f30, third → f29, etc.
- Mirrors GPR: first int → r31, second → r30, etc.
- `__savefpr_N` called based on how many callee-saved FPRs are used

**FPR allocation does NOT use BSF** — `base=7` has only 6 calls per TU
globally (not per-function). FPR uses a simpler sequential allocator that
IS declaration-order-sensitive but doesn't need BSF tracing.

**Real-world FPR swap analysis** (20 AT_LIMIT+regswap functions sampled):

| Category | Count | Pct | Fixable? |
|----------|------:|----:|----------|
| Callee-saved FPR swaps (f14-f31) | 5 | 25% | **YES — via float decl reorder** |
| Volatile FPR swaps only (f0-f13) | 6 | 30% | No — volatile scheduling, not declaration-controlled |
| GPR swaps only (no FPR) | 8 | 40% | Via GPR decl reorder (existing) |
| Errors/no swaps | 1 | 5% | N/A |

**Key examples with callee-saved FPR swaps**:
- `Invert` (99.5%): f30↔f31 — simple 2-way swap
- `NgMat::SetRegularShaderConst` (99.7%): f27↔f28↔f29↔f30 — 4-way cycle
- `TransConstraint::Poll` (98.3%): f28↔f29, f29↔f31 — mixed callee+volatile
- `NgMat::RefreshState` (92.0%): f29↔f30 — 2-way swap + GPR swaps
- `ClipDistMap::FindDists` (83.5%): f21-f31 chain — massive callee-saved FPR shuffle

**~25% of FPR-swapped AT_LIMIT functions have callee-saved FPR swaps
that are fixable via float declaration reorder.** Combined with GPR-only
functions (40%), ~65% of regswap AT_LIMIT functions have at least one
fixable component.

**Implementation plan**:
1. Add `fpr_to_color()` / `color_to_fpr()` to `regmap_solver.py`
   - Mapping: `color_N → f(31 - N)` (same sequential pattern as GPR)
   - No BSF needed — use assembly-listing or empirical approach
2. Extend `declaration_reorder` to identify and reorder float declarations
3. Handle mixed GPR+FPR cases (reorder both int and float decls)
4. The Tier 2b assembly-listing approach handles this automatically

### P3: Control Flow + Regswap Compound Fixes (139 functions)

**Problem**: Functions with both register swaps AND branch polarity mismatches
require fixing both issues simultaneously. The permuter already has both
`declaration_reorder` and `branch_polarity` patterns, plus composition.

**Approach**: Hill-climbing with composition enabled. Each round tries
single patterns and composed pairs. Branch polarity flips are cheap to
test (only 2 variants per comparison).

**Implementation**: Already supported by `hill_climber.py --compose`.
Needs batch execution on the 139 targets.

### P4: Prologue Mismatch Resolution (212 functions)

**Problem**: The compiler chose a different number of callee-saved registers
(e.g., saves r25-r31 instead of r23-r31). This means our code has fewer
live variables across calls than the original.

**This is the hardest category.** The fix isn't reordering — it's adding
or removing variables to change the live variable count.

**Possible approaches**:
1. **Variable extraction**: Pull subexpressions into named locals to add
   more live variables (increases register pressure)
2. **Variable merging**: Combine temporaries to reduce live variables
3. **Scope manipulation**: Move declarations to change live ranges
4. **Pre-computation**: Compute values before a call to keep them in
   registers across the call

**The permuter's `variable_extraction` pattern already does approach #1.**
Composition with declaration reorder handles the combined case.

**Success rate**: Low (~10-15%). Many prologue mismatches reflect
fundamental differences in how we structured the code.

### P5: Batch Pipeline Integration

**Goal**: One command to process all fixable AT_LIMIT functions.

```bash
python -m decomp_synth.batch_auto \
    --include-at-limit \
    --min-pct 90 --max-pct 100 \
    --max-rounds 5 --compose \
    --asm-guided    # Tier 2b assembly-listing mode
```

**Architecture**:
1. `batch_triage` classifies functions by mismatch type
2. `batch_auto` selects appropriate patterns per function
3. `hill_climber` runs iterative rounds with plateau detection
4. Results committed as source changes (not binary patches)

---

## What Would a Tool Need to Fix "Hard" Cases?

Analysis from CalcRotzBone case study (96.1% → 99.9%):

### Mismatch Type 1: Instruction Scheduling (fneg/frsp order)

**Automatable: YES.** A permuter pattern `negation_split` can:
1. Parse AST for unary `-` on function call results: `-func(...)`
2. Transform to: `T result = func(...); result = -result;`
3. Compile and test

This is a simple tree-sitter transformation. Applies to any function return
value being negated inline. Could also generalize to other unary operators
on function results (`~func()`, `!func()`).

### Mismatch Type 2: Offset Swaps Within Inlined Functions

**Automatable: HARD.** The offset swaps in CalcRotzBone are from the Dot()
inline expansion. The compiler chose to load `dir2.y` before `dir1.y` within
the multiply. This is controlled by:

1. **Evaluation order of operands** in `v1.y * v2.y` — compiler-internal
2. **Stack layout** of `dir1` and `dir2` — controlled by declaration order
3. **Register pressure** at the call site — affects spill/reload decisions

What would help:
- **Commutative operand swaps in inline functions**: Try `v2.y * v1.y` in
  the Dot() implementation. But this changes ALL Dot() call sites.
- **Per-callsite overrides**: Duplicate the function with swapped operands
  for specific callers. Ugly but effective.
- **Assembly-listing comparison**: Compare our `/FAs` output with target
  disassembly to identify exactly which loads are swapped, then trace back
  to the source expression.

**Current assessment**: These are likely unfixable noise for most functions.
The 0.1% gap from inlined function operand ordering is acceptable.

### Mismatch Type 3: Branch Target (bne address)

**Automatable: NO.** Branch targets are absolute addresses determined by
the linker. Different code size in preceding functions shifts all addresses.
This is pure addr reloc noise — unfixable and already accepted.

### Mismatch Type 4: bl Address Relocation

**Automatable: NO.** Same symbol but at different addresses in decomp vs
original. Pure link-order noise.

### Summary: What the Permuter Needs

| Mismatch Type | Fix Approach | Automatable? | Pattern Name |
|---------------|-------------|:------------:|--------------|
| fneg/frsp order | Split negation | YES | `negation_split` |
| Offset swap (decl order) | Reorder declarations | YES | `declaration_reorder` |
| Offset swap (inline) | Operand swap in callee | HARD | — |
| FPR callee-saved swap | Float declaration reorder | YES | `declaration_reorder` (extended) |
| FPR volatile swap | Expression restructuring | SOMETIMES | Case-by-case |
| Branch target | — | NO | Unfixable |
| bl addr reloc | — | NO | Unfixable |

---

## Implementation Roadmap

### Phase 1: Foundation (current sprint)

- [x] BSF per-function isolation (exact mangled symbol match)
- [x] 3-way cyclic rotation in solver
- [x] FPR limitation documented with tests
- [x] 96 tests passing (14 new: 3-way, FPR, isolation, population + 7 real-world)
- [x] FPR callee-saved mapping (`fpr_to_decl_index` / `decl_index_to_fpr`)
- [x] FPR swap pairs handled in `guided_pairwise_search`
- [x] CalcRotzBone case study: negation split pattern discovered (+3.8%)
- [ ] P0: Batch-check 289 untagged high-% functions
- [ ] P0: Reclassify 809 untagged 0% as stubs

### Phase 2: New Patterns + Assembly-Listing Guidance (next sprint)

- [ ] Implement `negation_split` pattern (detect `-func()`, split to `f=func(); f=-f;`)
- [ ] Build `/FAs` parser for variable→register mapping
- [ ] Build register assignment comparator
- [ ] Integrate as `--asm-guided` mode in declaration_reorder
- [ ] Validate on 34 pure-regswap functions
- [ ] Compare success rate: BSF-guided vs asm-guided vs unguided

### Phase 3: FPR Implementation + Batch Sweep

- [x] Run FPR declaration-order experiment (synthetic TU) — **CONFIRMED: order-dependent**
- [x] Survey real AT_LIMIT FPR swaps — **25% callee-saved (fixable), 30% volatile (unfixable)**
- [x] Add `fpr_to_decl_index()` / `decl_index_to_fpr()` to regmap_solver
- [x] FPR swap pairs handled in `guided_pairwise_search`
- [ ] Extend `declaration_reorder` to identify float declarations in AST
- [ ] Batch sweep: run permuter on all 126 regswap_simple functions
- [ ] Batch sweep: run on 139 regswap+ctrl_flow with --compose
- [ ] Report: functions fixed, patterns that worked, remaining blockers

### Phase 4: Prologue + Integration

- [ ] Targeted variable_extraction for prologue mismatch functions
- [ ] Full batch pipeline (`batch_auto --include-at-limit --asm-guided`)
- [ ] CI integration: run permuter on new AT_LIMIT functions after builds
- [ ] Deprecate post-build regswap patcher for functions fixed at source

---

## Success Metrics

| Metric | Current | Target |
|--------|---------|--------|
| AT_LIMIT functions | 2,461 | <1,500 (after data hygiene) |
| Pure regswap fixed (source) | 0 | 30-50 (of 126) |
| Regswap+compound fixed | 0 | 15-25 (of 193) |
| Fuzzy match % | 54.06% | 55-56% |
| Functions depending on binary patcher | ~200 | <100 |

---

## Key Files

| File | Role |
|------|------|
| `scripts/permuter/patterns/declaration_reorder.py` | Core reorder pattern with BSF guidance |
| `tools/compiler_trace/regmap_solver.py` | Color→register mapping, candidate generation |
| `tools/compiler_trace/bsf_trace.py` | BSF call capture via GDB |
| `tools/compiler_trace/invoker.py` | Compiler invocation with `/FAs` support |
| `scripts/permuter/hill_climber.py` | Iterative improvement loop |
| `scripts/permuter/batch_auto.py` | Batch execution pipeline |
| `scripts/permuter/batch_triage.py` | Function classification |
| `scripts/permuter/diagnosis.py` | Objdiff→diagnosis conversion |
| `scripts/obj_regswap_patcher.py` | Binary patcher (to be deprecated) |
| `scripts/permuter/tests/test_patterns.py` | 96 tests |

## Case Studies

### CalcRotzBone: 96.1% → 99.9% (FPR scheduling fix)

**Symbol**: `?CalcRotzBone@HamSkeletonConverter@@IAAXW4SkeletonJoint@@00@Z`

**Before**: 96.1% — 11 mismatches (4 offset swaps, 2 inserts, 2 deletes,
3 diff_arg). The insert/delete cluster was `fneg`/`frsp` instruction
reordering. FPR swap f0↔f31.

**Root cause**: `float angle = -acos(...)` folds the negation into the
double-precision result before rounding to single: `fneg → frsp`. The
original does `frsp → fneg` (round first, then negate).

**Fix**:
```cpp
// Before
float angle = -acos(Dot(dir1, dir2));

// After
float angle = acos(Dot(dir1, dir2));
angle = -angle;
```

**After**: 99.9% — remaining 6 mismatches are unfixable (4 offset swaps
within Dot() inline expansion, 1 branch target, 1 addr reloc).

**What didn't work**:
- Swapping `dir1`/`dir2` declaration order → made it WORSE (96.1% → 94.2%),
  introduced 22 new register mismatches
- Swapping `Dot(dir1, dir2)` → `Dot(dir2, dir1)` → no change to offset swaps
  (inline expansion order is determined by parameter names, not call order)

**Lessons**:
1. FPR "swaps" involving volatile f0 are often scheduling issues, not
   declaration order issues — expression restructuring is the fix
2. The `fneg`/`frsp` ordering is a general pattern: `-func()` vs
   `f = func(); f = -f;` produce different instruction sequences
3. Offset swaps within inlined functions (like Dot()) may be unfixable —
   the compiler's inline expansion order is determined internally
4. This pattern is automatable: detect `-func()` expressions and try
   splitting the negation

**Pattern added**: `fixable-operators.md#negation-splitting-fnegfrsp-scheduling`

### Invert(Transform): 99.5% (pure FPR swap target)

**Symbol**: `?Invert@@YAXABVTransform@@AAV1@@Z`

**State**: 99.5% — 8 mismatches: 4× f30↔f31 FPR swap + 4 offset swaps.
This is a header inline function in `Mtx.h`. The f30↔f31 is a pure
callee-saved FPR pair — two float subexpressions evaluated in wrong order.

**Fixability**: Likely fixable by reordering evaluation within `out.v.Set()`
arguments, but this is a header change affecting many TUs — needs careful
per-TU verification. Good target for the assembly-listing approach (Tier 2b).

## References

- `docs/decomp/patterns/unfixable-compiler.md` — Hard pattern documentation
- `docs/decomp/patterns/fixable-declarations.md` — Declaration pattern catalog
- `docs/sessions/2026-03-03-permuter-bsf-guided-validation.md` — BSF validation
- `docs/plans/permuter-hill-climbing.md` — Earlier hill-climbing plan (superseded)
- `docs/plans/permuter-macro-extraction/PLAN.md` — Macro function support
