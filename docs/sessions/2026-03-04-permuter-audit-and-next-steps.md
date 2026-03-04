# Permuter Audit & Next Steps

**Date**: 2026-03-04
**Status**: Audit complete, two high-value improvements identified

## Audit Results

Swept 34 units (~700+ candidates) with the batch permuter. Overall win rate: ~4%.

### Per-Unit Sweep Results

| Unit | Candidates | Wins | Best Win | Notes |
|------|-----------|------|----------|-------|
| system/obj/DataFunc | 89 | 0 | N/A | DEF_DATA_FUNC macro limitation |
| system/char/CharEyes | 46 | 3 | +6.22% ProceduralBlinkUpdate | |
| system/rndobj/Utl | 47 | 4 | +7.05% GetNormalMapTextures | Composed variable_extraction+declaration_reorder |
| system/rndobj/Text | 70 | 3 | +0.61% FontMap::AllocateMeshes | |
| system/rndobj/Mesh | 15 | 5 | +5.66% TransformNormal | declaration_movement, bitwise_accumulator |
| system/rndobj/Bitmap | 10 | 2 | +0.81% RndBitmap::Blt | |
| system/rndobj/Part | 9 | 1 | +0.01% MoveParticles | Composed |
| system/hamobj/HamCharacter | 9 | 0 | N/A | cmplwi/cmpwi not covered |
| system/rndobj/PostProc | 11 | 0 | N/A | SetType macro failures |
| system/rndobj/EventTrigger | 6 | 0 | N/A | SetType + operator= extraction |
| system/world/Spotlight | 7 | 0 | N/A | |
| system/world/CameraShot | 4 | 0 | N/A | 11 GPR swaps |
| system/rndobj/PropAnim | 9 | 0 | N/A | SetType failures |
| system/rndobj/Lit | 6 | 0 | N/A | GPR swaps, SetType errors |
| system/rndobj/Env | 4 | 0 | N/A | GPR swaps, no variants for Save |
| system/rndobj/Cam | 7 | 0 | N/A | 58-64% functions generate 0 variants |
| system/rndobj/Trans | 13 | 0 | N/A | boolcast BUILD FAILED (fixed), GPR swaps |
| system/rndobj/Anim | 7 | 1 | +0.03% AnimTask::Poll | Tiny win |
| system/rndobj/Overlay | 3 | 0 | N/A | BUILD FAILED patterns |
| system/rndobj/Dir | 2 | 0 | N/A | GPR swaps |
| system/gesture/* | 83 | 2 | +0.19% UpdateCallbacks | Small structural wins |
| system/obj/Data | 37 | 1 | +1.93% DataReadFile | Significant structural win |
| system/hamobj/HamNavList | 7 | 0 | N/A | GPR swaps, bl/stw diff_ops |
| system/ui/UIList | 28 | 1 | +0.31% UIListSubList::CreateElement | |
| system/rndobj/Shader | 7 | 0 | N/A | cmpeq+signunsign compositions all miss |
| system/hamobj/Pose | 4 | 0 | N/A | SetType error, GPR swaps |
| system/char/Char* | 139 | 3 | +0.61% CharBones::Blend | Large unit, small wins |
| system/obj/Dir | 20 | 2 | +1.03% ObjectDir::Iterate | Structural wins |
| system/synth/* | ~30 | 3 | +10.31% PollStream (0->10.3%) | Also UpdateVolumes +2.89% |
| system/world/Dir | 4 | 1 | +0.10% WorldDir::DrawShowing | |
| system/flow/* | 59 | 1 | +3.30% FlowNode::DuplicateChild | 22.9->26.2% |
| system/utl/* | ~40 | 3 | +13.95% SuperFormatString | 83.3->97.2% |
| system/hamobj/HamSkeletonConverter | 3 | 0 | N/A | inline BUILD FAILED |
| system/rndobj/Tex* | ~70 | 3+ | +9.89% FitTextScroll (0->9.9%) | Also CleanupSyncMeshes 0->4% |

### Top Wins

| Function | Delta | Unit |
|----------|-------|------|
| SuperFormatString | +13.95% (83->97%) | utl |
| StandardStream::PollStream | +10.31% (0->10%) | synth |
| RndText::FitTextScroll | +9.89% (0->9.9%) | rndobj/Tex |
| GetNormalMapTextures | +7.05% | rndobj/Utl |
| ProceduralBlinkUpdate | +6.22% | char/CharEyes |
| TransformNormal | +5.66% | rndobj/Mesh |
| FlowNode::DuplicateChild | +3.30% | flow |
| StandardStream::UpdateVolumes | +2.89% | synth |
| DataReadFile | +1.93% | obj/Data |
| ObjectDir::Iterate | +1.03% | obj/Dir |

### Winning Patterns
- `declaration_reorder` — most frequent contributor
- `variable_extraction` — especially when composed with declaration_reorder
- `signed_unsigned` — fixes cmplwi/cmpwi mismatches
- `comparison_flip` — branch polarity fixes
- `bitwise_accumulator` — && vs & operator fixes
- `inline_assignment` — occasional structural wins

### Never-Win Patterns
- `negation_split`, `and_split`, `fsel_template`, `pragma_fp_contract` — zero wins across all units

## Bugs Fixed During Audit

1. **`bool_cast` else-if BUILD FAILED**: Pattern 2 inserted `bool _cond = ...;` before `if` in `else if` chains, creating `} else bool _cond = ...; if (_cond)`. Fixed by skipping if_statements whose parent is an `else_clause`.
2. **`list index out of range` in scorer**: `ninja -t commands` returns empty when `_obj_target` is absolute path. Fixed by passing relative paths.
3. **`const_overload` BUILD FAILED**: Adding `const` before pointer types fails in non-const methods. Restricted to reference declarations only.
4. **Error traceback swallowed**: Added `traceback.print_exc()` to hill_climber exception handler.

---

## Two Blockers Identified

### Blocker 1: Macro-Generated Functions (~20% of failures)

**Problem**: `BEGIN_HANDLERS(X)`, `BEGIN_PROPSYNCS(X)`, and `OBJ_SET_TYPE(X)` macros generate `Handle()`, `SyncProperty()`, and `SetType()` methods. Tree-sitter sees the macro invocations, not C++ code, so the permuter can't extract or analyze these functions. SetType alone accounts for 50+ extraction failures.

**Root cause**: Tree-sitter parses pre-preprocessor source. Macros are opaque.

**All macros defined in `src/system/obj/Object.h`**:
- `BEGIN_HANDLERS(objType)` -> `DataNode objType::Handle(DataArray *_msg, bool _warn) { ... }`
- `BEGIN_PROPSYNCS(objType)` -> `bool objType::SyncProperty(DataNode &_val, DataArray *_prop, int _i, PropOp _op) { ... }`
- `OBJ_SET_TYPE(classname)` -> `virtual void SetType(Symbol classname) { ... }`
- Inner macros: `HANDLE`, `HANDLE_ACTION`, `HANDLE_EXPR`, `HANDLE_SUPERCLASS`, `SYNC_PROP`, `SYNC_PROP_SET`, `SYNC_PROP_MODIFY`, `SYNC_SUPERCLASS`, etc.

**Key properties**:
- All expansions are **deterministic** — fully predictable from macro arguments
- No cross-macro dependencies beyond nesting (internal helpers `_NEW_STATIC_SYMBOL`, `_HANDLE_CHECKED` are self-contained)
- Scope isolation via `{ }` blocks is critical (each HANDLE creates a scoped static symbol)

**Proposed solution: Temporary macro expansion**

For each permuter run:
1. Copy the source file to a temp location
2. Expand `BEGIN_HANDLERS...END_HANDLERS` and `BEGIN_PROPSYNCS...END_PROPSYNCS` blocks into valid C++ in the temp copy
3. Run the permuter on the expanded code
4. If a winning variant is found, compute the diff and apply it back to the original macro'd source
5. Discard the temp copy

The key insight: we don't need to "revert" the expansion. We just expand in a temp copy, permute there, and if something wins, apply the relevant changes back. The original file with macros is never modified during permutation.

**Risk**: LOW — original file untouched, temp copy discarded on crash.

### Blocker 2: GPR Swaps (~60% of failures, but not all unfixable)

**Problem**: Functions with register allocation mismatches get 0 improvement. The permuter's `declaration_reorder` pattern tries to fix these but has a low success rate.

**Register swap classification**:

| Type | Registers | Fixable? | Why |
|------|-----------|----------|-----|
| Callee-saved GPR | r13-r31 | YES | Assigned by declaration order: 1st var -> r31, 2nd -> r30 |
| Volatile GPR | r3-r12 | SOMETIMES | Assigned by expression evaluation order, argument passing, scratch scheduling. Can sometimes be influenced by expression restructuring, argument reordering |
| Callee-saved FPR | f14-f31 | YES | Same declaration-order rule as GPR: 1st float -> f31, 2nd -> f30 |
| Volatile FPR | f0-f13 | SOMETIMES | Argument passing (f1-f13), return values (f1), scratch |

**Measured distribution across swapped functions**:
- ~50% pure volatile swaps (hardest to fix, no simple mapping)
- ~25% pure callee-saved swaps (fixable via declaration reorder)
- ~25% mixed (partially fixable)

**Why current declaration_reorder underperforms**:
1. BSF trace guidance only available for **17%** of functions (need ~7+ callee-saved vars to trigger graph coloring)
2. For the 83% without BSF, the permuter does **random** pairwise swaps — combinatorial explosion, low hit rate
3. No FPR-specific reordering integrated into the permuter yet

**Proposed solution: Assembly-listing-based register guidance**

The highest-ROI unimplemented approach:
1. Compile the decomp source with `/FAs` flag to get interleaved source+assembly listing
2. Parse the `.asm` listing to extract variable -> register mappings
3. Compare against target register assignment from objdiff
4. Generate **targeted** declaration reorders that produce the desired register assignment

**Advantages over BSF**:
- Works for **ALL functions** (not just 17% with BSF calls)
- No ptrace requirement
- Handles both GPR and FPR
- Directly observable variable->register mapping (no color indirection)
- Deterministic and reproducible

**Estimated impact**: Could improve regswap fix rate from ~15-20% to ~30-40%, potentially addressing 30-50 additional functions.

**Volatile swap note**: While callee-saved swaps have a clear declaration-order fix, volatile swaps are not always hopeless. They can sometimes be influenced by:
- Reordering function arguments
- Changing expression structure (`a + b` vs `b + a`)
- Extracting subexpressions to temporaries
- Changing variable liveness overlap

The permuter already does some of this (commutative_swap, variable_extraction, inline_assignment) but doesn't target volatile register allocation specifically.

---

## Implementation Priority

1. **Macro expansion for permuter** (HIGH ROI, moderate effort)
   - Unblocks ~20% of currently-failing function extractions
   - Deterministic expansion, well-understood macros
   - Low risk (temp file approach)

2. **Assembly-listing register guidance** (HIGH ROI, higher effort)
   - Unblocks ~30-40% of regswap failures
   - Works for all functions, not just BSF-traced ones
   - Requires `/FAs` parser + register comparator

3. **Disable never-win patterns** (LOW effort, saves cycles)
   - `negation_split`, `and_split`, `fsel_template`, `pragma_fp_contract` never produced wins
   - Reducing pattern count speeds up each permuter run
