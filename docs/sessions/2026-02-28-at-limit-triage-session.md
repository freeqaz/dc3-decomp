# AT_LIMIT Triage Session — 2026-02-28

## Summary

Systematic sweep of AT_LIMIT functions (883 total, 2.6% of non-excluded) searching for
fixable matches. Analyzed ~50 functions across the 30-99.8% match range. One function
improved to COMPLETE; the rest confirmed as genuinely at their limit.

## Results

### Win: HamAudio::GetCurrLoopMarkers (83.7% → 99.8% COMPLETE)

The single real improvement this session. Three changes were needed simultaneously:

1. **Swap declaration order**: `Marker m2, m1;` instead of `Marker m1, m2;`
   - Fixes stack offset assignment (m2 at lower offset than m1)

2. **Swap assignment targets**: `f1 = m2.posMS; f2 = m1.posMS;` instead of the intuitive
   `f1 = m1.posMS; f2 = m2.posMS;`
   - Confirmed via Ghidra decompilation — the target binary genuinely assigns f1 from m2

3. **Early-return pattern instead of if/else**:
   ```cpp
   // BEFORE (83.7%) - compiler merges destructors into common epilogue
   if (s && s->CurrentJumpPoints(m1, m2)) {
       f1 = m2.posMS; f2 = m1.posMS; return true;
   } else {
       return false;
   }

   // AFTER (99.8%) - separate destructor paths at each return point
   if (!s || !s->CurrentJumpPoints(m1, m2)) {
       return false;
   }
   f1 = m2.posMS;
   f2 = m1.posMS;
   return true;
   ```
   - The if/else generated 5 `String::~String` calls; the target had 6
   - Early-return forces each return path to destroy its own local `Marker` objects independently

### Other 13 files in commit (from prior session)

| File | Change | Before → After |
|------|--------|----------------|
| StorePreviewMgr.cpp | Early-return refactor | 85.6% → 99.0% |
| MidiParser.cpp | Assignment reorder | 85.9% → 99.9% |
| BinkMovieSys.cpp | Control flow fix | 82.2% → 99.2% |
| VirtualKeyboard_Xbox.cpp | Struct init order | 82.8% → 98.8% |
| ResourceDirPtr.cpp | Control flow restructure | 82.0% → 94.3% |
| Cache_Xbox.cpp | Variable reorder | 82.3% → 93.9% |
| StubCameraInput.cpp | Signed cast fix | 81.6% → 91.5% |
| ChallengeResultPanel.cpp | Comparison flip + var extract | ~88% → 91.5% |
| FitnessCalorieSortByCalorie.cpp | Format string + method fix | minor |
| SongSortByLocation.cpp | Minor cleanup | minor |
| ClipDistMap.cpp | Code cleanup | neutral |
| Text.cpp | UpdateScrolling fix | minor |
| HamAudio.cpp | See above | 83.7% → 99.8% |
| MoveVariant.cpp | Correctness fix | neutral |

## Useful Techniques

### What actually improved matches

1. **Early-return pattern** — The single most impactful technique. When a function has
   if/else with local objects that have destructors (String, Marker, etc.), the compiler
   can merge destructor calls into a common epilogue path. Early-return forces separate
   destructor generation per exit path. This was the key insight for HamAudio.

2. **Ghidra decompilation for ground truth** — When objdiff shows assignment-level
   mismatches, Ghidra reveals what the target binary actually does. The HamAudio fix
   required knowing that f1←m2 and f2←m1 (counterintuitive reversed assignment).

3. **Declaration order swaps** — Reordering local variable declarations changes stack
   layout. `Marker m2, m1;` vs `Marker m1, m2;` shifts which variable gets which stack offset.

4. **Signed/unsigned casts** — The permuter catches these mechanically. `(int)x` vs
   `(unsigned)x` changes sign-extension instructions.

### What did NOT help (diminishing returns)

1. **Permuter** — Ran on 11+ functions this session. Zero improvements found.
   The permuter is most effective on fresh code in the 50-80% range, not on
   already-polished AT_LIMIT code.

2. **Register swap analysis** — Most AT_LIMIT functions have 2-10 register swap pairs.
   These are compiler register allocation decisions that cannot be influenced from source
   without completely restructuring the function (which breaks everything else).

3. **Address relocation noise** — 4,741 instances project-wide. These are COFF section
   address differences between our object files and the target. Completely unfixable
   without matching the exact original link layout.

4. **MakeString template mismatches** — 2,928 instances. Different `__FILE__` string
   lengths or argument types produce different template instantiation targets. Unfixable
   unless we match the exact original file paths.

5. **ICF (Identical COMDAT Folding)** — 264 instances. The linker merged functions with
   identical machine code. Our linker makes different folding decisions. Unfixable.

## Patterns Observed in AT_LIMIT Functions

### By match percentage range

| Range | Typical Issues | Fixability |
|-------|---------------|------------|
| 95-99.8% | 1-2 register swaps, address relocations, ICF noise | Rarely fixable |
| 85-95% | Register swaps + structural (MakeString, static guards) | Occasionally fixable with permuter |
| 60-85% | Multiple register swaps + control flow + inlining differences | Fixable if structural issue dominates |
| 30-60% | Fundamental algorithmic differences (bit manipulation, inlined templates) | Almost never fixable |

### Systematic unfixable patterns

- **Static guard counters**: Per-TU counters for `static` locals depend on function definition
  order within the translation unit. Changing order breaks other functions.
- **Constructor inlining**: Target may inline a constructor while ours calls it (or vice versa).
  Controlled by compiler heuristics we can't influence.
- **vector<bool> bit operations**: Target often inlines bit manipulation that our compiler
  emits as function calls to `_Bit_reference::operator[]`.
- **FPR register swaps** (f0↔f13): Floating-point register allocation. Never fixable.

## Key Insight: The 883 AT_LIMIT Ceiling

At 97.4% COMPLETE (32,827 functions), the remaining 883 AT_LIMIT functions are genuinely
at their practical limit. The breakdown:

- ~264 blocked by ICF (linker merged symbols)
- ~2,928 blocked by MakeString template mismatches
- ~4,741 affected by address relocation noise
- ~81 blocked by bool mask differences
- Remainder: register allocation, static guards, constructor inlining

Finding one more fixable function required analyzing ~50 candidates. The project has
reached saturation — further improvements require toolchain-level fixes (matching linker
behavior, matching __FILE__ paths exactly, etc.) rather than source-level changes.

## Tools Used

- `mcp__orchestrator__query_functions` — Find candidates by match range and verdict
- `mcp__orchestrator__run_diff_inspect` (diagnose mode) — Root cause analysis
- `mcp__orchestrator__run_diff_inspect` (mismatches mode) — Instruction-level diff
- `scripts.permuter` — Automated source variation testing (11 runs, 0 improvements)
- Ghidra decompile skill — Ground truth for assignment order in HamAudio
- `mcp__orchestrator__run_objdiff` — Build + diff after each source change
