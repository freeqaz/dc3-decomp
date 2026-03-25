# Decompilation Retrospective — March 24, 2026

## Sessions Covered
| Function | Start | Final | Status |
|---|---|---|---|
| `RndScaleObject` | stub | 94.0% | Functionally Equivalent |
| `ClipCollide::Collide` | 19.4% | 99.7% | AT_LIMIT (stack layout) |
| `HamAudio::FinishLoad` | 84.4% | 98.6% | AT_LIMIT (register allocation) |
| `HamSkeletonConverter::Set` | 67.6% | 73.3% | Likely AT_LIMIT (matrix init codegen) |
| `kdTree<Triangle>::kdTreeNode::Pack` | — | — | Referenced for tooling lessons |

---

## RndScaleObject (94.0%)

**Target:** `RndScaleObject`

## Summary of Work
The function `RndScaleObject` handles scaling for various `Hmx::Object` subclasses by casting them dynamically. The decompilation involved:
1.  **Structural Rewrite:** Converting the original codebase's flat `if { return; }` structure into a deeply nested `else-if` chain. This perfectly mapped to the compiler's emitted branch logic for the `dynamic_cast` cascade, eliminating control-flow mismatches.
2.  **Missing Classes Implemented:** Added scaling logic for previously stubbed classes: `RndEnviron`, `RndGenerator`, `RndLightAnim`, and `RndMatAnim`.
3.  **Fixed Math & Ordering:** Corrected parameter usage (switching between `scale` and `fovScale`) in `RndParticleSys` and `RndLine`. Reordered the setter sequence in `RndParticleSys` to coerce the compiler into matching the target instruction scheduler perfectly.

This work also reinforced a broader decompilation pattern that came up again immediately afterward on `kdTree<Triangle>::kdTreeNode::Pack`: getting from "mostly correct C++" to "matching compiler output" depends on having a tight feedback loop between high-level decompilation, object diffing, and source-attributed assembly. The semantic recovery phase is only half the work. The last 10-20% is usually about proving exactly why the compiler chose a different register, stack slot, local ordering, or call setup sequence.

---

## ClipCollide::Collide (19.4% → 99.7%)

### Algorithm Recovery
This was a full decompilation from a near-empty stub. The function implements a clip-based collision detection tool for the Dance Central character system:
1. Guard check (`mChar && mWaypoint && mClip`), hide character drawable
2. Find 3 bone transforms (L-ankle, R-ankle, guitar) via `CharUtlFindBoneTrans`
3. Get the venue as `RndDrawable*` via `dynamic_cast<RndDrawable*>(Dir())`
4. Loop from `clip->StartBeat()` to `clip->EndBeat()` in 1.0-beat steps:
   - Pose character via `ScaleDown(*servo, 0.0f)` / `ScaleAdd(*servo, delta, beat, blend)` / `servo->Poll()`
   - For each bone: get world position, offset guitar bone by +2.5× Z-axis
   - After first frame: build collision `Segment` from previous→current position, test against venue
   - Exempt floor hits (`pos.z < char.z + 1.0`) and invisible meshes (no diffuse tex + zero alpha)
   - Report non-exempt collisions via `AddReport(pos)`
5. Re-show character

### Key Fixes That Drove Match% Jumps

| Change | Match% Jump | Insight |
|---|---|---|
| Full function body implementation | 19.4% → 88.9% | Algorithm was correct from first pass thanks to Ghidra + RB2 locals |
| `bool b1 = X && Y && Z; if (b1)` instead of early return | +2% | Target materializes boolean in GPR (`li r10,1`/`bne`/`li r10,0`/`clrlwi.`) |
| `float blend` declared before `float f` | +4% | Swapped f28↔f27 and f29↔f30 FPR assignments to match target |
| `float delta` moved inside `if` block | (combined) | Target loads 1.0f after condition check, not before |
| `Vector3 p = xfm.v` instead of `p.x=xfm.v.x; ...` | 95.3% → 99.7% | Full struct copy emits `lwz/stw` (4 integer words); component copy emits `lfs/stfs` (3 floats, no padding) |

### Remaining Gap (0.3%)
15 stack offset mismatches — all `off:-16` or `off:+32`. The compiler placed local variables (bone names array, Color temp, Segment, interp result) at different stack slots. No source-level fix possible.

### Notable Decompilation Patterns Discovered

**Virtual call through multiple-inheritance sub-object:** `servo + 8` (the `CharPollable` sub-object within `CharServoBone`) → vtable slot 1 = `Poll()`. The `CharServoBone` class has three non-virtual bases: `RndHighlightable` (offset 0, 8 bytes), `CharPollable` (offset 8), `CharBonesMeshes` (offset 0xC). The sub-object vtable at +8 contains new virtuals from `RndPollable` (not the virtual base `Hmx::Object`), with `PollEnabled` at slot 0 and `Poll` at slot 1. Writing `b->Poll()` in source correctly generates the `servo+8` dispatch.

**ObjPtr member offset arithmetic:** When Ghidra shows `*(int*)(mesh + 0x128)`, that's `mesh->mMat.mObject` — the raw pointer lives at offset 0xC within `ObjPtr` (due to `ObjRef` base class), so `mMat` at 0x11c + 0xC = 0x128. This pattern recurs for every `ObjPtr` member access in Ghidra output.

**Integer vs float Vector3 copy:** `Vector3 p = other` generates 4× `lwz/stw` (16-byte memcpy via GPRs). `p.x = other.x; p.y = other.y; p.z = other.z;` generates 3× `lfs/stfs` (FPR loads, no padding copy). The target consistently uses the integer copy pattern.

---

## HamAudio::FinishLoad (84.4% → 98.6%)

### Summary of Work
`HamAudio::FinishLoad` sets up audio streams, faders, and track routing for Dance Central song playback. The function was already implemented but had significant codegen mismatches — wrong string literals, a static Symbol that should have been a local, and pointer arithmetic the PPC compiler couldn't strength-reduce.

### Key Fixes That Drove Match% Jumps

| Change | Match% Jump | Insight |
|---|---|---|
| `static Symbol main("main")` → `const char *mogg = "mogg"` | 84.4% → 91.1% | Target constructs Symbol twice (once per NewBufStream call) via implicit conversion from `const char*`. A named `Symbol` variable is constructed once and reused — different codegen. The `const char*` approach caches the string pointer in a callee-saved register and constructs a temporary Symbol at each call site, matching the target's two `bl Symbol::Symbol` calls. |
| Permuter `const_ref_swap + value_address_caching` chain | (same fix) | The permuter found this transformation automatically — converting a named `Symbol` to `auto _val0 = ("mogg")` (cleaned up to `const char *mogg`). This was the single non-obvious insight. |
| `"reverb_send"` → `"song.send"` | (combined) | Wrong FxSend name in original decomp. Identified via Ghidra's string reference. |
| Fixed MILO_NOTIFY string | (combined) | Original: "stream 1 not ready for resync". Target: "almost tried to resync stream before it was ready". |
| Crossfader: pointer subtraction → hardcoded byte offset `0x38` | 95.1% → 96.3% | `mCrossFaders[pStream - &stream0]` generates `srawi/addi/slwi/lwzx/mr` (5 instructions). Target accesses crossfader at constant byte offset from pStream (`lwz r4, 0x38, r24`). The MSVC PPC compiler doesn't strength-reduce pointer subtraction even when both arrays are at known offsets from `this`. Requires `#ifdef HX_NATIVE` guard for 64-bit portability. |
| Removed `crossFader` local variable | 96.3% → 98.6% (combined) | Target loads crossfader from memory twice (before Add and before SetVolume). Our `Fader *crossFader = ...` told the compiler to cache in r31, generating `mr r4,r31` / `mr r3,r31` around the call. Inlining the expression lets the compiler reload naturally across the `bl Add` sequence point. |
| Moved `counter` declaration after if-block | 96.3% → 97.9% | Target initializes `counter=2` and `pStream` after the `if(mFileLoader)` block. Having them before generated 3 extra instructions in the prologue area. |

### Remaining Gap (1.4%)
The `auto& stream0 = mStreams[0]` reference consumes callee-saved register r30, shifting the entire register allocation: `TheSynth` address in r17 vs r19, `mogg` pointer in r30 vs r29, and `pStream` materialized via `mr r24, r30` instead of `addi r24, r25, 0x44`. Removing the reference was tested and dropped match to 90.0% — it provides more benefit (pointer reuse across the if-block and loop) than cost (1 extra instruction + register shift). The remaining 22 diff_arg instructions are all register swaps and address relocation noise.

### Notable Decompilation Patterns Discovered

**Implicit Symbol conversion from `const char*`:** When the target constructs a `Symbol` temporary at each call site (visible as two separate `bl Symbol::Symbol` calls), using a `const char*` variable with implicit conversion matches better than a named `Symbol` variable. The `const char*` caches only the string pointer, while `Symbol` caches the interned result — different register usage and constructor count.

**Compiler doesn't strength-reduce member pointer subtraction:** `mCrossFaders[pStream - mStreams]` should mathematically fold to `*(pStream + 0x38)` since both arrays are at known offsets from `this`. The MSVC PPC compiler does NOT perform this optimization — it generates the full divide-by-element-size, add-offset, multiply-back sequence. The target compiler (same MSVC, but the original build) DID fold it, suggesting the optimization depends on inlining context or optimization level specifics we can't replicate. Hardcoded byte offsets are the only workaround, requiring `#ifdef HX_NATIVE` for portability.

**Local variable caching vs memory reload across calls:** When a value is used before and after a function call, declaring a local variable tells the compiler to cache it in a callee-saved register (2 `mr` instructions to shuttle to/from the param register). NOT declaring it forces a reload from memory, which is cheaper when the base address is already in a callee-saved register (1 `lwz` instruction). The target often prefers the reload pattern for values derived from simple member offsets.

**Declaration placement affects instruction scheduling:** Moving `unsigned int counter = 2` from before to after an if-block changed where the `li r11, 0x2` and `stw` instructions appeared in the output. The compiler materializes locals at declaration point even if the value is trivial. Matching the target's placement eliminated 3 instructions from the prologue.

---

## HamSkeletonConverter::Set (67.6% → 73.3%)

### Summary of Work
`HamSkeletonConverter::Set` converts Kinect skeleton tracking data into the Dance Central character bone system. The function builds a coordinate transform from camera space to game space, projects all 20 joint positions, computes a pelvis orientation matrix, and dispatches spine/arm/leg bone calculations. At 482 target instructions (1928 bytes), this is one of the largest single functions in the hamobj subsystem.

The initial implementation was structurally incorrect in 7 ways — wrong multiply overloads, wrong joint indices, a typo in the pelvis center computation, swapped matrix rows, missing cross product components, and wrong scaling order. All 7 bugs were identified from a single Ghidra decompilation pass by comparing member offsets and function call signatures against the source.

### Key Fixes That Drove Match% Jumps

| Change | Match% Jump | Insight |
|---|---|---|
| Matrix3 Multiply instead of Transform Multiply for flipX/flipZ | 67.6% → ~70% | Target calls `Multiply(Matrix3,Matrix3,Matrix3)` twice + `Multiply(Transform,Transform,Transform)` twice. Original code called Transform Multiply 4 times. Function call diff was the signal. |
| `pelvisCenter.x = (hipL.x + hipR.x) * 0.5f` (was `+ 0.5f`) | (combined) | Typo found by comparing Ghidra's `* 0.5` on all three components. |
| pelvisOffset: `-2.0f` then `* 39.37008f` after Multiply (was `-2.0f * 39.37008f` before) | (combined) | Ghidra showed `local_46c = -2.0` → Multiply → `local_46c * 39.37008`. Mathematically equivalent for this rotation but generates different instructions. |
| `Subtract(kJointHipRight, kJointHipLeft, hipAxis)` replacing `Subtract(kJointSpine, kJointHipCenter, spineDir)` | (combined) | Ghidra accessed offsets 0x140 and 0x170 = joints 12 and 15 = kJointHipLeft and kJointHipRight. Original used joints 1 and 0. Completely wrong direction vector. |
| Full `Cross(hipAxis, pelvisLateral, pelvisFwd)` replacing 2-component manual cross with `x=0` | 72.5% → ~73% | Target assembly had 3 `fmsubs` instructions (full cross product). Our code had 2 `fmsubs` + `stfs 0`. Cluster analysis at idx 399-409 proved all 3 components were computed. |
| Matrix row swap: m.y=pelvisFwd, m.z=hipAxis (were reversed) | (combined) | Ghidra store offsets: 0x6e0-0x6ec = m.y = pelvisFwd, 0x6f0-0x6fc = m.z = hipAxis. |
| Pre-compute `pvx/diffX/diffY/diffZ` before Distance calls | +0.5% | Target caches `pelvisTransform.v.{x,y,z}` and diffs in callee-saved FPRs before Distance calls, uses `fmuls+fadds`. Our code reloaded from member and used `fmadds`. |

### Remaining Gap (26.7%)

The dominant gap is a **98-instruction delete cluster** (idx 64-138) where the target generates word-by-word Matrix3 copies using callee-saved GPRs (r17-r22):
```
lwz r22, 0x0, r8    ; load 4 words of matrix row from stack local
lwz r21, 0x4, r8
lwz r20, 0x8, r8
lwz r8,  0xc, r8
stw r22, 0x0, r5    ; store to different stack location
stw r21, 0x4, r5
stw r20, 0x8, r5
stw r8,  0xc, r5    ; ... repeated for 3 rows × 2 matrices
```

The target copies the initialized flipX/flipZ matrices from their initialization stack slots to separate locations for the Multiply call arguments. Our compiler passes the matrices directly without copying. This single pattern accounts for:
- 98 of 104 delete instructions
- `savegprlr_17` vs `savegprlr_22` (the 5 extra callee-saved GPRs hold the copied words)
- +304 bytes stack frame difference (extra copy destinations)

**Attempts to reproduce the copy pattern:**
- `GetIdentity()` + `Matrix3 flipX = unk40.m` → compiler uses `memcpy` instead of lwz/stw (dropped to 68.5%)
- Transform locals with `.m` passed to Multiply → +0.8% from extra Transform padding but no word copies
- Individual `Set()` calls on Matrix3 locals → stfs directly to stack, no word copies

The remaining 57-instruction `r30↔r31` register swap is the `this` pointer allocation (target r30, ours r31) — unfixable.

### Notable Decompilation Patterns Discovered

**Ghidra decompilation is unreliable for register-level cross-function analysis.** Ghidra showed the cross product using raw member values (`fVar2 - fVar1` from `this+0x170` and `this+0x140`) mixed with normalized stack-local values (`local_46c`, `local_45c`). This appeared to be a deliberate mix of pre-normalize and post-normalize components — I spent ~20 minutes analyzing whether the source code cached raw x-values before Normalize. The actual assembly (proven via cluster analysis) showed a standard full cross product using all normalized components. Ghidra's register-to-source mapping broke down across the Normalize function call boundary.

**Matrix3 vs Transform Multiply overload selection is visible in the function call diff.** The `run_objdiff` function call diff section (`Target only: Multiply(Matrix3,Matrix3,Matrix3) ×2`) immediately identified the wrong overload. This was the single most efficient diagnostic — one line of output pointed to a structural bug that affected the entire initialization sequence.

**`memcpy` threshold for struct assignment is ~48 bytes on MSVC PPC.** Assigning a Matrix3 (48 bytes) from a static reference generates `bl memcpy`. The target compiler generates individual `lwz/stw` word copies for the same operation. Individual `Set()` calls generate `stfs` float stores. None of these three approaches match each other. The 48-byte threshold appears to be the breakpoint where the compiler switches from inline stores to memcpy.

**Transform locals + Matrix3 reference pass is slightly better than bare Matrix3 locals.** Using `Transform flipX; ... Multiply(flipX.m, ...)` scored 73.3% while `Hmx::Matrix3 flipX; ... Multiply(flipX, ...)` scored 72.5%. The extra 32 bytes of stack padding from the Transform's `v.Zero()` moved surrounding variables to stack offsets that better matched the target layout. This is a fragile, non-obvious coupling between local variable sizes and stack slot assignment.

---

## Tooling Retrospective

### What Worked Well
*   **m2c Decompilation Integration:** The `run_analyze_function` tool automatically running `m2c` alongside `objdiff` was incredibly powerful. While Ghidra's decompilation struggled slightly with representing the C++ `dynamic_cast` tree, `m2c` cleanly exposed the nested `else-if` structure. It clearly highlighted the exact order of casts and the missing class handlers in the chain.
*   **Grep + Headers Workflow:** When struct offset lookups failed (see below), utilizing `grep_search` to quickly find class definitions (`Gen.h`, `Env.h`, `LitAnim.h`, `MatAnim.h`) and `read_file` to inspect the class declarations made it straightforward to map raw offsets (e.g., `0x158`, `0x174`) to specific variables (`mFogStart`, `mRateGenLow`, etc.).
*   **Rapid Iteration:** Using the `replace` tool to quickly swap variable orderings and re-running `mcp_orchestrator_run_objdiff` allowed for rapid A/B testing of compiler scheduling behaviors (especially useful in the dense math block of `RndParticleSys`).
*   **Ghidra + m2c Register-to-Variable Mapping:** On `ClipCollide::Collide`, the m2c output used register-based names (`temp_f28`, `var_f27`, `var_f30`, `temp_f29`, `temp_f31`) that directly corresponded to FPR assignments. This made it possible to determine the exact variable declaration order the target compiler expected — `temp_f28=0.0` loaded first, then `var_f27=blend` copied from it, then `var_f30=StartBeat()` — which guided the fix from 88.9% to 95.3% by reordering `float blend` before `float f`.
*   **RB2 Dump Local Variable Lists:** The RB2 dump at `rb3/doc/rb2_dump/` provided the exact local variable list with register assignments for `ClipCollide::Collide` (e.g., `RndDrawable * w; // r31`, `float delta; // f31`, `unsigned char punt; // r27`). This confirmed variable types, register targets, and the algorithm structure before writing any code. Combined with Ghidra's control flow, this made the initial implementation nearly correct in one pass (19.4% → 88.9%).
*   **Triangulation Between Ghidra, objdiff, and Checked-In Target ASM:** This was especially clear on `kdTree::Pack`. Ghidra recovered the real algorithm quickly, `run_objdiff` gave fast numeric feedback, and the checked-in target assembly in `build/373307D9/asm/system/rndobj/AmbientOcclusion.s` made it possible to resolve declaration-order and call-setup questions that summaries alone could not answer.
*   **Manual `/FAs` Compilation Through `wibo`:** This was the most useful fallback once objdiff summaries stopped being precise enough. Extracting the exact compile command from `ninja -t commands` and re-running it through `wibo` with `/FAs` produced a current source-annotated assembly listing for `AmbientOcclusion.cpp`. That made it obvious where the remaining mismatches were coming from: local list ordering, `GetIsLeaf()` lowering, child-node address math, and recursive call setup. This workflow is currently too manual, but it is extremely effective.
*   **Iterative "Logic First, Lowering Second" Workflow:** On `kdTree::Pack`, the sequence of tools made it possible to separate semantic fixes from codegen fixes cleanly. First Ghidra/analyzer output exposed wrong behavior like split dispatch and child indexing. Then the `/FAs` and target ASM comparison exposed non-semantic issues like stack slot layout and masking/reload sequences. That separation prevented wasting time tuning codegen before the algorithm was correct.

*   **`run_diff_inspect clusters` as Primary Diagnostic:** On `HamAudio::FinishLoad`, clusters mode was the single most valuable tool — more useful than diagnose or mismatches for this function. Each cluster showed exactly which source construct was generating extra instructions (pointer subtraction → 5 inserts, counter init → 3 inserts, crossFader caching → 3 inserts). Running this after every change provided immediate actionable feedback about what moved and what remained. On `HamSkeletonConverter::Set`, clusters mode was equally decisive — the 98-instruction delete cluster (idx 64-138) with dominant opcodes `stw, lwz, stfs` immediately identified the matrix word-copy pattern as the primary gap. The cross product cluster (idx 399-409 showing 3 `fmsubs`) proved the target computed all 3 components, overriding the misleading Ghidra decompilation that suggested only 2.
*   **`run_objdiff` Function Call Diff Section:** This section proved disproportionately valuable for structural bugs. On `HamSkeletonConverter::Set`, the diff showed `Target only: Multiply(Matrix3,Matrix3,Matrix3) ×2` and `Count differs: Multiply(Transform,Transform,Transform) target 2, base 4` — one glance identified the wrong Multiply overload, the single biggest structural fix. Similarly, `Base only: memcpy` flagged the `GetIdentity()` copy approach as non-matching. Function call diffs should be checked first before diving into instruction-level analysis.
*   **Permuter for Non-Obvious Type Transformations:** The permuter's `const_ref_swap + value_address_caching` chain found the `Symbol` → `const char*` transformation that was the biggest single improvement (+4%). This is a type-level change (Symbol value vs const char* with implicit conversion) that changes constructor call count — not something that follows from reading the assembly diff. Automated search excels at these "change the type of a variable" transformations.
*   **Incremental objdiff for Rapid A/B Testing:** Each `run_objdiff` call took ~2-3 seconds with incremental builds. This made it practical to try 8+ speculative variations (remove `auto&`, move declarations, try temporaries, try macros, hardcode offsets) and quickly discard the ones that regressed. On `HamSkeletonConverter::Set`, 8 distinct variations were tested (Matrix3 vs Transform, GetIdentity vs Set, declaration order swaps, cached floats vs inline, pelvis diff pre-computation) — the fast cycle was essential because the interactions between these changes were non-linear.

### Areas for Improvement

#### 1. Struct Offset Lookup Inheritance
The `mcp_orchestrator_lookup_struct_offset` tool is fantastic, but it currently has a blind spot regarding C++ inheritance. If a target field belongs to an inherited parent class rather than the leaf class being queried (e.g., querying `0x158` on `RndEnviron` failed because the field was declared in a parent or macro), the tool returns nothing. 
**Improvement:** Enhance the tool to automatically traverse C++ inheritance trees using parsed header data or DWARF info. If `RndEnviron` inherits from `RndDrawable`, the offset tool should calculate offsets by virtually stacking the parent layouts.

#### 2. Granular `m2c` Retrieval
The output from `run_analyze_function` often exceeds token limits and dumps to a local file. This requires an extra `run_shell_command(cat...)` or `read_file` turn to view the results. 
**Improvement:** Introduce a standalone `run_m2c` tool or a flag (e.g., `--m2c-only`) that guarantees the output block fits directly into the context window, saving unnecessary turns reading intermediate files.

#### 3. AST-Aware Replacements
Currently, the `replace` tool relies on exact-string matching. Doing massive structural rewrites—like converting a 100-line flat `if-return` block into a 100-line nested `else-if` block—is incredibly brittle. If a single space or indentation is off, the replacement fails, costing valuable context and time.

**Proposed UX for AST-Aware Replacement:**
Integrating a tool powered by something like `ast-grep` or `tree-sitter` would allow semantic replacements instead of literal ones.

**Example 1: Function-level replacement**
Instead of providing the old string, I could just target the function node:
```json
{
  "file_path": "src/system/rndobj/Utl.cpp",
  "target_node": "function_definition: RndScaleObject",
  "new_string": "void RndScaleObject(Hmx::Object *obj, float scale, float fovScale) { ... }"
}
```

**Example 2: Block-level modification**
If I only want to modify the inside of an `if` statement without worrying about surrounding whitespace:
```json
{
  "file_path": "src/system/rndobj/Utl.cpp",
  "target_node": "if_statement: condition(dynamic_cast<RndParticleSys*>)",
  "action": "replace_body",
  "new_string": "Vector3 vb = partsys->ForceDir();\npartsys->SetBubbleSize(partsys->BubbleSize().x * scale, partsys->BubbleSize().y * scale);\n// ..."
}
```

**Example 3: Structural Refactoring**
A specialized command to wrap existing code:
```json
{
  "file_path": "src/system/rndobj/Utl.cpp",
  "action": "wrap_statements",
  "start_statement": "RndCamAnim *camanim = dynamic_cast<RndCamAnim *>(obj);",
  "end_statement": "return;",
  "wrapper": "else { $BODY }"
}
```

By abstracting away whitespace and exact character counts, AST-aware replacements would make large-scale refactoring and structure tweaking significantly more robust and turn-efficient.

#### 4. First-Class `/FAs` Listing Tool
The current workflow for getting a source-attributed assembly listing is too indirect. In the `kdTree::Pack` work, `run_diff_inspect mode=asm_listing` did not cleanly solve the problem for the instantiated target, so the fallback was:
1.  Run `ninja -t commands` for the object file.
2.  Extract the exact compiler invocation.
3.  Add `/FAs` and a custom `/Fa...` output path.
4.  Re-run the command manually through `wibo`.
5.  Open the generated `.asm` and isolate the target function by hand.

This worked well, but it should not require manual shell reconstruction.

**Improvement:** Add a single tool like `run_fas_listing(symbol, unit)` that:
- Resolves the correct translation unit.
- Extracts the existing compile command automatically.
- Rebuilds through `wibo` with `/FAs`.
- Returns only the requested function's listing.
- Optionally includes source-line attribution and local variable/register annotations.

This would eliminate one of the highest-friction parts of last-mile matching.

#### 5. Target-vs-Current Assembly Comparison
The repository already contains checked-in target assembly, and `/FAs` generation gives us current source-attributed assembly. But there is no single tool that compares them in the way a decompiler actually needs:
- target assembly on one side
- current `/FAs` listing on the other
- normalized register renaming
- source-line attribution
- local/stack object names where possible

Right now, the user or agent has to mentally merge:
- Ghidra pseudocode
- objdiff summaries
- target `.s`
- current `/FAs`

**Improvement:** A `compare_target_to_fas` tool should produce a side-by-side annotated diff for one function, highlighting:
- source lines responsible for mismatches
- stack slot / local declaration differences
- call setup differences
- register swap clusters that are likely declaration-driven rather than semantic

This would make the final 10% much more mechanical.

#### 6. Better Stack/Local Attribution
`run_diff_inspect` can report offset swaps, but the output is still one step too low-level for many STL-heavy functions. On `kdTree::Pack`, the useful fact was not just "offset swap around `0x68` and `0x70`". The useful fact was:
- `leftList` and `rightList` are reversed on the stack
- a temporary spill is occupying the slot the target uses for another local
- the assert scratch / iterator storage is perturbing nearby declarations

**Improvement:** Add a stack-layout viewer that correlates:
- target local regions
- current local regions
- likely source variable identities
- constructor/destructor ordering

This would be especially useful for functions with several local STL containers, temporary iterators, and EH cleanup edges.

#### 7. Register Pressure / Lowering Explanation
Current tools are good at saying "register swaps detected", but not at explaining why. The remaining `kdTree::Pack` mismatches were not about missing logic. They were about:
- a parameter being spilled too early
- a cached local changing which callee-saved register was used
- a child pointer being staged in a different order than the target

**Improvement:** A register-pressure explanation tool should answer:
- which local introduced the spill
- which declaration order change altered the live range graph
- why a value ended up in a callee-saved register instead of being reloaded later
- whether a mismatch is likely fixable by source reordering versus effectively compiler-noise

That would be much more actionable than a raw register swap histogram.

#### 8. Recursive Call Shape Analyzer
Recursive and tree-building functions often fail to match in the same ways:
- mask/reload sequences
- child pointer staging
- byte-offset vs element-offset pointer math
- reuse of masked flags vs reload from memory

`kdTree::Pack` is a perfect example. Once the algorithm was correct, the hard part became reproducing exactly how the compiler lowered child-node address computation and recursive call setup.

**Improvement:** Add a specialized analyzer for recursive-call setup that can detect and explain:
- pointer arithmetic form
- reload-vs-cache differences
- argument staging order
- where a temporary local is forcing a different call sequence

This would be a high-leverage tool for trees, linked structures, and traversal-heavy functions.

#### 9. Automatic Fallback Inside Orchestrator
The orchestrator is already very good at surfacing `objdiff`, diagnosis, and related metadata. But when a higher-level mode fails, the fallback path is still manual.

**Improvement:** If `asm_listing` or a similar mode cannot resolve a function directly, the orchestrator should automatically:
- identify the owning unit
- extract the matching compile command
- rebuild with `/FAs` through `wibo`
- return the narrowed function listing

That should be one tool call, not a recovery workflow that has to be reconstructed from docs and shell commands.

#### 10. Virtual Call Resolver
On `ClipCollide::Collide`, the hardest single analysis problem was resolving `servo+8, vtable[1]`. This required manually tracing the `CharServoBone` inheritance chain (3 non-virtual bases with virtual `Hmx::Object`), computing sub-object offsets (RndHighlightable=8 bytes → CharPollable at +8), then determining the vtable slot layout for the CharPollable sub-object (new virtuals only, no destructor entry since it routes through the vbase).

**Improvement:** A tool like `resolve_vcall(class, sub_object_offset, vtable_slot)` that:
- Parses the class hierarchy from headers
- Computes the sub-object layout accounting for virtual inheritance
- Maps vtable slot indices to actual function names
- Handles MSVC PPC ABI specifics (adjustor thunks, vbase dispatch separation)

Example: `resolve_vcall("CharServoBone", 8, 1)` → `RndPollable::Poll()` (overridden by `CharServoBone::Poll()`).

This was the single most time-consuming analysis step in the ClipCollide work (~30 minutes of manual reasoning). A tool would reduce it to seconds.

#### 11. ObjPtr/Wrapper Offset Calculator
Ghidra consistently shows raw offsets like `*(int*)(mesh + 0x128)` for member accesses through `ObjPtr` wrappers. Manually computing that `mMat` (ObjPtr<RndMat>) starts at 0x11c in RndMesh, and `mObject` is at +0xC within ObjPtr (due to ObjRef base), so the raw pointer is at 0x128 — this is tedious arithmetic that recurs for every `ObjPtr`, `ObjOwnerPtr`, and `ObjPtrVec` member.

**Improvement:** A tool like `resolve_member_offset("RndMesh", 0x128)` that:
- Knows the layout of wrapper types (ObjPtr has mObject at +0xC, ObjOwnerPtr similarly)
- Returns "RndMesh::mMat (ObjPtr<RndMat>) → mObject (the raw RndMat* pointer)"
- Works in both directions: given a class + member name, compute the raw Ghidra offset

This would eliminate one of the most common sources of manual error in Ghidra analysis.

#### 12. Permuter: "Inline Local Variable" Pattern
On `HamAudio::FinishLoad`, the crossFader caching issue (compiler caches in r31 vs target reloads from memory) required manually removing a named local and substituting the expression at each use site. The permuter has `variable_extraction` (extract subexpression into local) but not the inverse: **variable inlining** (replace local with expression at each use). This is a common last-mile fix — when a function call separates two uses, the compiler's CSE can't prove memory hasn't changed and must reload, which sometimes matches the target's behavior better than a cached local.

**Improvement:** Add a `variable_inline` pattern that:
- Identifies locals used exactly N times (typically 2-3)
- Substitutes the defining expression at each use site
- Removes the local declaration
- Scores the result

#### 13. Permuter: Declaration Placement Across Control Flow
Moving `unsigned int counter = 2` from before to after an if-block gained 1.6%. The permuter's `declaration_movement` pattern exists but didn't find this — it may be limited to reordering declarations within the same scope rather than moving them across control flow boundaries (before/after an if-block).

**Improvement:** Extend `declaration_movement` to try placing declarations at different program points relative to control flow: before the first if, after the first if, at the start of a loop, etc. This is especially important for trivially-initialized variables where the compiler materializes the value at declaration point.

#### 14. Compiler Strength-Reduction Prediction
The pointer subtraction `mCrossFaders[pStream - mStreams]` should mathematically fold to a constant offset, but the MSVC PPC compiler doesn't perform this optimization. Discovering this required building, checking the diff, seeing the `srawi/addi/slwi` cluster, reasoning about what it computes, and then trying the hardcoded offset. A tool that could predict "this expression will generate N instructions; here's a simpler form" would shortcut this.

**Improvement:** When `run_diff_inspect clusters` detects an insert cluster containing arithmetic sequences (shift/add/shift patterns), flag it as "likely pointer arithmetic that wasn't strength-reduced" and suggest the constant-offset alternative with the computed value.

#### 15. RB2 Dump Auto-Lookup
The RB2 dumps at `rb3/doc/rb2_dump/` are structured text files with local variable lists, register assignments, and reference symbols for every function. Currently finding and reading them requires manual glob + file read. On `ClipCollide::Collide`, this provided the variable list that made the initial implementation nearly correct in one pass.

**Improvement:** A tool like `rb2_locals("ClipCollide::Collide")` that:
- Searches the RB2 dump directory for the matching function
- Parses and returns the local variable table (name, type, register/stack location)
- Parses reference symbols (RTTI types, vtable refs, static symbols)
- Flags differences between RB2 (MW EABI PPC) and DC3 (MSVC PPC) calling conventions

This data is already sitting in the repo — it just needs a parser and lookup interface.

#### 16. Symbol Disambiguation in Orchestrator Tools
On `HamSkeletonConverter::Set`, every orchestrator tool (`run_objdiff`, `run_analyze_function`, `run_diff_inspect`) matched `SetQuatBoneValue` instead of `Set(BaseSkeleton const*)` on the first attempt. Recovering required: (1) trying the demangled name with parameter types, (2) grepping `report.json` for the mangled name, (3) using the raw mangled symbol `?Set@HamSkeletonConverter@@QAAXPBVBaseSkeleton@@@Z` in all subsequent calls. This consumed 3-4 tool calls and significant context.

**Improvement:** When a symbol has multiple overloads, the tools should:
- Prefer exact demangled match including parameter types (e.g., `Set(BaseSkeleton const*)` should match before `SetQuatBoneValue`)
- Accept partial parameter specifications like `Set(BaseSkeleton*)` or `Set(*BaseSkeleton*)`
- When ambiguous, return a disambiguation menu with match% for each candidate so the user can pick without re-running

#### 17. Ghidra Decompilation Reliability Across Function Calls
On `HamSkeletonConverter::Set`, Ghidra's decompilation showed the cross product using raw member values (`*(float*)(this + 0x170)` and `*(float*)(this + 0x140)`) for the x-components while using normalized stack-local values for y/z. This appeared to be deliberate pre-normalize caching — the source code seemed to cache raw member x-values before calling Normalize, then use those cached values in the cross product while using normalized y/z from the stack vectors.

~20 minutes of analysis went into determining whether this was:
1. A real source pattern (explicit caching of raw values)
2. A compiler optimization (reusing pre-normalize register values)
3. A Ghidra decompilation artifact (wrong register-to-source mapping)

The actual assembly (proven via `run_diff_inspect clusters` at idx 399-409) showed 3 `fmsubs` computing a standard `Cross(hipAxis, pelvisLateral, pelvisFwd)` — all normalized components. Ghidra broke down because it couldn't trace register contents through the external `Normalize` function call.

**Improvement:** When Ghidra shows values loaded from member data being used after an intervening function call that takes a reference to a stack-local vector, flag this as "possibly stale — verify that the function call doesn't modify the stack-local through the reference parameter." Better yet, provide a mode that shows Ghidra decompilation alongside the actual target assembly for a specific address range, so register provenance can be verified without trusting the decompiler's data flow analysis.

#### 18. Struct Copy Codegen Prediction
On `HamSkeletonConverter::Set`, three different source approaches for the same logical operation (copy a 48-byte Matrix3) produced three incompatible codegen patterns:
1. `unk40.m = Hmx::Matrix3::GetIdentity()` → `bl memcpy` (48-byte threshold)
2. Individual `Set()` calls → `stfs` float stores directly to destination
3. Target code → `lwz/stw` batched word copies through callee-saved GPRs

No combination of source constructs reproduced pattern 3. The target's pattern uses callee-saved GPRs (r17-r22) as temporary holding registers, loading 4 words at a time from one stack location and storing to another. This might be generated by a struct assignment from a non-static source (e.g., `flipX = coord.m` where `coord` is another local), but attempts to reproduce it with local-to-local assignment generated `memcpy` instead.

**Improvement:** A "codegen pattern database" that maps known PPC instruction sequences to the source constructs that generate them. When `run_diff_inspect clusters` detects a batched lwz/stw pattern, it could suggest "this looks like a struct copy — try local-to-local assignment, or check if the target builds the source struct before copying."

#### 13. Stack Layout Diff Tool
The final 0.3% gap on `ClipCollide::Collide` was 15 stack offset mismatches. The raw diff showed `off:-16` and `off:+32` but gave no indication of which variables occupied which slots.

**Improvement:** A tool that reconstructs and compares stack layouts:
```
Target:  r1+0x50=p(Vector3)  r1+0x60=names[3]  r1+0x70=dist  r1+0x80=color
Base:    r1+0x50=names[3]    r1+0x60=p(Vector3) r1+0x70=color r1+0x80=dist
Diagnosis: names[] and p swapped; color and dist swapped. Likely unfixable (compiler-internal layout).
```
This could be built by correlating:
- `/FAs` listing (source line → stack offset mapping)
- Target assembly (load/store patterns → stack offset usage)
- Type size knowledge (Vector3=16, Segment=32, float=4, etc.)

## Overall Reflection

The decompilation workflow has three distinct phases, each with different tool maturity:

| Phase | Quality | Bottleneck |
|---|---|---|
| **Algorithm recovery** (0→80%) | Excellent | Ghidra + m2c + RB2 dumps make this fast |
| **Bug identification** (wrong logic/joints/overloads) | Excellent | Ghidra offset comparison catches bugs that testing might miss (HamSkeletonConverter: 7 bugs found from one decompilation) |
| **Semantic correctness** (80→95%) | Good | `run_analyze_function` + `mismatches` mode gives tight feedback |
| **Codegen matching** (95→100%) | Weak | Requires manual assembly reasoning with insufficient tooling |
| **Compiler init pattern reproduction** | Poor | No tooling exists for matching target's struct copy/init codegen (HamSkeletonConverter: 98-instruction gap from unfixable init pattern) |

The tools already exist in pieces: Ghidra decompilation, `run_analyze_function`, `run_objdiff`, `run_diff_inspect`, checked-in target assembly, manual `/FAs` generation through `wibo`. What is missing is the glue between them.

### The Effective Workflow (confirmed across both sessions)

1. **Ghidra + RB2 dump** to recover control flow, variable types, and register targets.
2. **First implementation pass** — write the full function body. This typically gets to 80-90% if the algorithm is correct.
3. **`run_analyze_function`** for fast percentage feedback after each source change.
4. **`run_diff_inspect mismatches`** to identify specific instruction-level differences — this is where the real diagnosis happens.
5. **Pattern-based fixes**: boolean guard patterns, declaration reordering, struct copy style, scope placement of constants.
6. **Accept AT_LIMIT** when only address relocations or stack layout noise remains.

Steps 1-3 are well-supported. Step 4 works but returns raw data that requires significant mental effort to interpret. Step 5 relies on accumulated pattern knowledge (documented in MEMORY.md and `docs/decomp/patterns/`). The biggest productivity gain would come from making steps 4-5 more automated — specifically, the `/FAs` fallback, virtual call resolution, ObjPtr offset calculation, and stack layout comparison.

### Cross-Session Patterns

All four main sessions (`RndScaleObject`, `ClipCollide::Collide`, `HamAudio::FinishLoad`, `HamSkeletonConverter::Set`) confirmed:
- **Boolean materialization matters**: `bool b = X && Y && Z; if (b)` generates different code than `if (!X || !Y || !Z) return;`. The former uses `li/bne/li/clrlwi.`, the latter chains branches. Pick whichever the target uses.
- **Declaration order is the primary lever for register assignment**: The linear scan allocator assigns callee-saved GPRs (r31→r14) and FPRs (f31→f14) in order of first encountered use. Moving declarations — or changing their type — shifts the entire allocation. On ClipCollide, `float blend` before `float f` swapped four FPR assignments. On HamAudio, the `auto& stream0` reference consuming r30 shifted pStream from r24 to r23.
- **Declaration placement affects instruction scheduling**: The compiler materializes locals at their declaration point. Moving `unsigned int counter = 2` from before to after an if-block eliminated 3 prologue instructions on HamAudio. This also appeared on ClipCollide with `float delta` placement.
- **The last 5% is always about codegen, not semantics**: Integer vs float copy (`lwz/stw` vs `lfs/stfs`), scope placement of constants, local caching vs memory reload, pointer arithmetic form, call-site scheduling. These have nothing to do with program correctness — they're about reproducing compiler-internal choices.
- **Type changes can be more impactful than structural changes**: On HamAudio, changing `Symbol mogg` to `const char *mogg` (same variable, different type) changed constructor call count and register usage — a +4% improvement. On ClipCollide, `Vector3 p = xfm.v` vs `p.x = xfm.v.x; ...` changed copy instruction type — a +4.4% improvement. Type-level decisions propagate through the entire function's codegen.
- **Removing abstractions sometimes helps, sometimes hurts**: On HamAudio, removing the `crossFader` local (forcing memory reload) gained 1.3%. But removing the `auto& stream0` reference (seemingly simpler code) lost 8.6%. The compiler's register allocator interacts non-linearly with variable liveness — a priori prediction is unreliable, so rapid A/B testing is essential.
- **Function call diffs are the fastest structural diagnostic**: On HamSkeletonConverter, the function call diff section of `run_objdiff` immediately revealed 3 structural bugs in one output: wrong Multiply overload (Matrix3 vs Transform), missing memcpy (from GetIdentity copy), and wrong function call count. This was faster and more decisive than instruction-level analysis for identifying the root cause category. Check function call diffs first, then drill into instruction clusters.
- **Ghidra decompilation is high-value for algorithm recovery but unreliable for register-level reasoning**: On HamSkeletonConverter, Ghidra correctly identified 7 bugs (wrong joints, wrong overloads, typo, scale ordering) but gave a misleading cross product representation that appeared to mix normalized and unnormalized values. The lesson: trust Ghidra for member offsets, function calls, and control flow structure. Do NOT trust it for which register holds which value after an external function call — verify with `run_diff_inspect clusters` or target assembly.
- **Local variable size affects stack slot assignment non-linearly**: On HamSkeletonConverter, using `Transform flipX` (64 bytes) vs `Hmx::Matrix3 flipX` (48 bytes) changed the match from 72.5% to 73.3% — the extra 16 bytes of padding shifted surrounding locals to better-matching stack positions. This coupling between variable size and stack layout means that "smaller locals = better" is not always true.
