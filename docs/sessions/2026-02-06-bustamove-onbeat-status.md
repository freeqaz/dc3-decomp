# BustAMovePanel::OnBeat Decompilation

**Date:** 2026-02-06 (initial investigation), 2026-02-07 (team-agent approach)
**Symbol:** `?OnBeat@BustAMovePanel@@QAAXXZ`
**Result:** 63.7% -> 95.1% match (at_limit)

## Overview

Multi-session effort to match `BustAMovePanel::OnBeat`, a massive ~12KB state machine function with 10 switch cases governing Dance Central 3's Bust-A-Move minigame. Started at 63.7% on Feb 6 with incremental single-agent work, then escalated to a 5-agent team approach on Feb 7 that ultimately reached 95.1% -- the practical limit due to unfixable register allocation artifacts.

## Feb 6: Initial Investigation (63.7% -> 65.9%)

### Starting State

The function was at 63.7% with base size 10668 vs target 12056 bytes (~1200 bytes of missing code). Prior work had already established the switch structure with cases 0-9 mapping to BAM states (CountIn, Recording, Playing, ShowMove, PlayCountIn, RecordCountIn, FreestyleMove, ShowMoveSequenceSetup, ShowMoveSequence, End).

### Key Findings

**Ghidra symbol recovery:** Real function names from the linker map (`SetShowing`, `PlayVO`, `IncreaseScore`, `SetMovePrompt`, `QueueMovePromptVO`) replaced placeholder names, revealing several incorrect API calls in the decomp.

**String corrections from Ghidra addresses:**
- `0x820fa474` = `acc_flawless_every_move` (trophy accomplishment symbol)
- `0x820fa3a8` = `nar_bam_finale_fast` (not `nar_bam_final_winner`)
- `0x820fa5b0` = `live` (Recording strstr target, not `hide_bam_ghost`)

**ObjPtr unlink pattern:** The Ghidra showed inline pointer manipulation at offsets 0x190/0x194/0x198, which mapped to `ObjPtr<RndTex> unk18c` in DepthBuffer3D. The "unlink from parent" pattern is `unk18c.SetObjConcrete(NULL)` -- clearing a player palette texture reference.

**DataNode::Equal discovery:** Winner VO logic uses `DataNode::Equal(const DataNode&, DataArray*, bool)` for comparisons, not `operator==`. Initial attempt caused a 63.8% -> 50.2% regression due to code bloat.

### Changes Applied (Session 1-3)

| Change | Effect |
|--------|--------|
| Case 0/5: SetMovePrompt/QueueMovePromptVO/CountIn call swap | +0.1% |
| Case 8: SetFlashcardText -> PlayVO("nar_bam_finale_fast") | +0.4% |
| Cases 1,5,8,9: unk18c.SetObjConcrete(NULL/palette) pattern | +0.6% |
| Case 2: StopRecording -> StopPlayback | +0.1% |
| advance.anim loop: iterate [0],[1] not [1],[0] | +0.1% |
| VO if-else chains converted to switch statements | +0.6% |

### Critical Lesson: Cascading Misalignment

Any code addition to case 9 caused ~15% regression (e.g., 63.8% -> 48.6%) because adding code shifts the `end_handling` section, misaligning ~1300 subsequent instructions. The function's size must match the target before late-function changes can improve the match percentage. All missing code must be added simultaneously, not incrementally.

### Static vs Stack-Local Symbol Regression

Partially converting static Symbols to stack-local caused a catastrophic 62.6% -> 41.5% regression. Must convert ALL at once or none -- the static guard bits are assigned sequentially and shifting one shifts all subsequent bits.

## Feb 7: Team-Agent Approach (48% -> 95.1%)

### Team Structure

Five specialized agents coordinated by a human operator:

| Agent | Role |
|-------|------|
| **team-lead** | Coordinator: reads source, assigns research, synthesizes findings, directs edits |
| **ppc-expert** | PowerPC codegen specialist: register allocation, compiler scheduling, instruction patterns |
| **m2c-expert** | m2c decompiler analyst: .obj relocation data, authoritative function call counts |
| **ghidra-expert** | Ghidra output analyst: broke 1420-line decompilation into 14 labeled chunks |
| **editor** | Code editor: applies changes, runs objdiff, reports results |

### Phase 1: Adding Missing Code (65.9% -> 48%)

The team added all missing code blocks simultaneously to close the 1200-byte size gap:
- Case 9 trophy loop (`EarnAccomplishmentForProfile`)
- Case 9 PlayVO winner/loser/tie logic with `DataNode::Equal`
- Case 9 `TheMaster->GetAudio()->SetPaused(true)`
- Case 7 GetShuffledInts choreography mode block (3 modes with vector shuffling)
- Stub function implementations (AnimateFlashcard 86.3%, AdvanceFlashcards 88%, RepsToNextPhrase 79.6%)

Match dropped to ~44-48% as expected (massive structural changes), but size went from 1200 bytes short to within 200 bytes of target.

### Phase 2: m2c-Verified Corrections (48% -> 54%)

The m2c expert re-ran decompilation with bug fixes, achieving full 1674-line coverage. Five critical bugs found from .obj relocation data:

1. **All paired inserts in case 7 mode loops target unk50 (list\<int\>), not mixed unk48/unk50** -- fixed 5 excess list\<Symbol\> inserts
2. **unk50 sentinel values are -1 and -2, not 0**
3. **Case 7 message string: "bustamove_both_dance"** (not "bustamove_sequence_reveal")
4. **Case 5 named symbols:** "bam_record1" through "bam_record4" for last 4 unk48 entries (not all gNullStr)
5. **hide_bam_ghost is NOT a static Symbol** -- uses temporary `Symbol("hide_bam_ghost")` passed to `DataVariable()`

### Phase 3: Structural Alignment (54% -> 95.1%)

The breakthrough discovery: **switch case ordering**. The compiler emits cases in the order they appear in source. Reordering from (0,1,2,3,4,5,6,7,8,9) to **(0,1,2,3,4,5,6,9,7,8)** yielded a +38.6% improvement in a single change by aligning the entire instruction stream.

Additional fixes:
- Beat4 float comparison: `unk974 > 0.65f && unk978 > 0.65f` rewritten to match LZCOUNT pattern (+0.1%)
- Case 5 `unk6c` checks moved inside `if (unk7c)` block (+0.8%)
- Nested if/else scoring in case 8 (vs goto pattern) -- structural fix
- Float-to-int: `(int)beat` inside both if/else branches + `beatF + 8.0f` for SetLoop (+0.5%)
- Case 7 vectors at outer scope sharing stack slots (matching m2c stack offsets)

### Size Convergence

| Stage | Base Size | Target | Delta |
|-------|-----------|--------|-------|
| Start (Feb 6) | 10668 | 12056 | -1388 |
| After adding all code | 12164 | 12056 | +108 |
| After m2c fixes | 11820 | 12056 | -236 |
| After structural alignment | 11872 | 12056 | -184 |
| After case reorder | ~12048 | 12056 | -8 |
| Final | 12052 | 12056 | -4 |

## Remaining Mismatches (Unfixable)

The PPC expert audited all 44 remaining delete instructions:

| Issue | Instructions | Cause |
|-------|-------------|-------|
| Vtable pre-caching | +4 | Compiler scheduling in SetTextToken calls |
| `clrrwi` register copy | -1 | Register allocation artifact (base is more efficient) |
| Branch inversion in scoring | +2 | Goto fall-through pattern |
| Missing trampoline | -3 | Compiler cold-path decision |
| Linked list splice | -2 | Structural difference in ObjRef chain |

Net: +1 instruction (4 bytes short). Every remaining difference is a compiler scheduling decision or register allocation artifact -- not reproducible from source.

## Key Technical Insights

1. **Case ordering matters enormously.** In large switch functions, the compiler emits cases in source order. A single reorder can change alignment of thousands of instructions.

2. **Add all missing code at once.** In functions >10KB, incremental additions cause cascading misalignment. The match percentage is meaningless until the size is within ~100 bytes of target.

3. **m2c .obj relocations are authoritative.** When Ghidra and m2c disagree, m2c wins because it reads the actual compiled .obj file with correct relocation targets.

4. **Static guard bits are sequential.** Moving, adding, or removing any static Symbol shifts all subsequent guard bit positions, causing widespread mismatches.

5. **Team-agent coordination works.** The 5-agent approach enabled parallel analysis (Ghidra, m2c, PPC docs) with an editor applying verified fixes. Self-coordination between experts (ghidra asking m2c for confirmation) produced higher-confidence fixes.

6. **MSVC debug builds are predictable.** No LTCG means most codegen decisions are local. The remaining 4.9% gap is entirely from register allocation and instruction scheduling -- compiler internals not controllable from source.
