# Session: RhythmBattle::OnBeat Deep Dive & diff_inspect.py Tooling

**Date**: 2026-02-07
**Function**: `RhythmBattle::OnBeat` (4186 instructions)
**Symbol**: `?OnBeat@RhythmBattle@@AAAXXZ`
**Result**: 92.0% -> 92.1% (at_limit, confirmed)
**Previous session**: `2026-02-07-onbeat-at-limit-and-diff-inspect.md` (92.0%, 6 experiments)

## Overview

This session was a second, more systematic attempt at pushing `RhythmBattle::OnBeat` beyond 92%. The earlier session had already tried 6 experiments and built the initial `diff_inspect.py` tool. This session:

1. Built out `diff_inspect.py` into a full-featured analysis suite (5 analysis modes)
2. Used `run_analyze_function` (Ghidra/m2c) to get a decompilation of the target binary
3. Ran a further systematic experiment campaign informed by instruction-level analysis
4. Conclusively established the root cause as unfixable compiler behavior
5. Achieved a small improvement (92.0% -> 92.1%) via one successful experiment

## The One Fix That Worked

**Experiment A: Remove redundant unk130 null guard**

```cpp
// Before (92.0%):
if (b22 && unk130 && unk130->GetCurrentMoveNumFrames() != 0
    && (unsigned int)unk130->GetCurrentMoveNumFrames() <= unk134.size()) {

// After (92.1%):
if (b22 && unk130->GetCurrentMoveNumFrames() != 0
    && (unsigned int)unk130->GetCurrentMoveNumFrames() <= unk134.size()) {
```

**Rationale**: At this code point (line 837), we're inside `if (mFullKTB && !mFinale)` which guarantees `unk130` was allocated in `Begin()`. The target binary doesn't null-check here. Removing the guard eliminated 2 inserted instructions (`cmplwi cr6, r11, 0x0` / `beq cr6, ...`) at diff index 924-925.

## Experiments That Failed

| Experiment | Target | Change | Result | Why |
|---|---|---|---|---|
| B: i16/i19 compare | idx 1258 (ble->bgt) | `(i19 >= i16)`, `(i16 <= i19)`, if/else | All worse | Register allocation swaps comparison operands, not our code |
| C: beq vs ble modulo | idx 955 (beq->ble after divw.) | `!i6cc`, `<= 0`, unsigned cast | Neutral | `ble` after `divw.` is compiler being safe about signed division |
| D: bool computation | idx 1384 (srwi vs clrlwi) | int instead of bool, `!!` normalization | 90.6% regression | Completely different boolean computation idiom generated |
| F: declaration reorder | r20<->r21 swap (111 insts) | Swap focusPanel/goofy, group statics | 91.3-91.9% regression | Variable order doesn't control register allocation at this scale |
| Collapsed SetUnk2a5 | Lines 692-698 | Remove if/else, single expression | 91.8% regression | Different code structure, not what target expects |

## Root Cause Analysis

### Primary: `__savegprlr` vs `__savegprlr_14` (the register cascade)

This is the dominant unfixable difference and explains ~318 register swap instructions (the largest mismatch category).

The **target binary** calls `__savegprlr` at function entry, which saves registers r13-r31 (19 callee-saved GPRs). Our compiled code calls `__savegprlr_14`, saving only r14-r31 (18 callee-saved GPRs).

Why the difference? The target binary has a short `__FILE__` string (`"RhythmBattle.cpp"`, 20 bytes). The compiler decides this is worth caching in callee-saved r14 for reuse across the many `MILO_ASSERT` calls in the function. Our build has a full path (`"/home/.../RhythmBattle.cpp"`, much longer), and the compiler decides *not* to cache it.

This single decision cascades through the entire function:
- Target puts `this` in r20, ours puts it in r21
- Target puts TheDebug in r15, ours in r14
- Target puts true/false constants in r17/r18, ours shifts them
- Net effect: 111 instances of r20<->r21 alone, plus 22 other swap pairs

This is **fundamentally unfixable** at the source level. The `__FILE__` expansion is controlled by the build system, and even if we could shorten it, the compiler's register allocation heuristics are a black box.

### Secondary: 43 LINKER_MERGED calls (ICF)

Identical COMDAT Folding merged 5 different functions to shared addresses. These appear as `replace` instructions where the target calls `merged_XXXXXXXX` addresses. The linker optimization is not reproducible.

### Tertiary: Tail call optimization at idx 3634

The target optimizes a call to `DataArray::Release()` into a tail call (`b` instead of `bl`), eliminating 5 instructions (the call return, two player field stores, and a branch). Our compiler doesn't make this optimization in this context.

### Quaternary: Boolean mask differences (2 instructions)

At idx 1384-1386, the target uses `srwi r11,r11,31` (extract sign bit) + `addze` + `clrlwi` while our code generates `clrlwi r11,r11,24` (mask to byte) + `subfe` + `clrlwi.` for the same boolean conditional. Different idioms, same result.

### Mismatch Budget

| Category | Instructions | % of Total | Fixable? |
|---|---:|---:|---|
| equal | 2212 | 52.8% | N/A (matched) |
| diff_arg (register swaps) | ~320 | 7.6% | No (compiler allocation) |
| diff_arg (offset shifts) | ~500 | 11.9% | No (stack frame cascade) |
| diff_arg (symbol relocs) | ~550 | 13.1% | No (linker/static scope) |
| diff_arg (branch dests) | ~260 | 6.2% | No (address noise) |
| replace (symbol-reloc noise) | ~165 | 3.9% | No (ICF/static numbering) |
| replace (real structural) | ~30 | 0.7% | Maybe, but attempts failed |
| insert | 59 | 1.4% | Partially (got 2 via Exp A) |
| delete | 86 | 2.1% | Unlikely at source level |
| diff_op | 5 | 0.1% | No (compiler optimization choices) |

### Confirmation via `run_analyze_function`

The `run_analyze_function` MCP tool (Ghidra + m2c decompilation of the target binary) independently confirmed the AT_LIMIT verdict with high confidence:

- **LINKER_MERGED** (43 calls, Unfixable)
- **BOOL_MASK** (2 instructions, UsuallyUnfixable)
- **REGISTER_SWAP** (318 instructions, MaybeFixable in theory but cascaded)
- **CONTROL_FLOW** (7 instructions, LikelyFixable but attempts failed)
- **OFFSET_SWAP** (4 instructions, LikelyFixable but attempts failed)

---

## Tooling: `scripts/diff_inspect.py`

This was the major tooling outcome of the two OnBeat sessions. What started as a simple filter script grew into a comprehensive instruction-level analysis tool that fills the gap between raw instruction dumps and objdiff's high-level pattern engine.

### Why This Tool Exists

When working on a 4000+ instruction function at 92% match, you need to answer specific questions:

1. "Where exactly are the mismatches?" (filter mode)
2. "Why don't these match?" (diagnose mode)
3. "Are these real differences or noise?" (replaces mode, noise budget)
4. "Which register pairs are swapped?" (regswaps mode)
5. "Is the stack frame shifted uniformly?" (offsets mode)
6. "Where are the structural differences clustered?" (clusters mode)

The existing tools (`objdiff-cli diff --analyze`, `show_instrs.py`) either operated at too high a level or too low a level to answer these questions efficiently.

### Architecture

The tool operates on objdiff's JSON output, which contains per-instruction data including:
- `match_type`: equal, diff_arg, diff_op, replace, insert, delete
- `target` / `base`: opcode + args for each side
- `typed_args`: semantic argument types (Register, Symbol, Signed, Unsigned, BranchDest)
- `diff_breakdown`: argument-level diffs with types (register, symbol, immediate, branch_dest)

This rich data allows diff_inspect to classify *why* instructions differ, not just *that* they differ.

### Modes

#### 1. Filter Mode (default)

```bash
python3 scripts/diff_inspect.py diff.json                  # all non-equal
python3 scripts/diff_inspect.py diff.json diff_op          # only opcode mismatches
python3 scripts/diff_inspect.py diff.json replace          # only replaced instructions
python3 scripts/diff_inspect.py diff.json insert,delete    # structural differences
python3 scripts/diff_inspect.py diff.json all              # every instruction
python3 scripts/diff_inspect.py diff.json diff_op -C 8     # with 8 lines context
```

Shows matching instructions with surrounding context. Groups nearby matches to avoid redundant output. Annotates diff_arg instructions with what changed (register swap, offset shift, symbol relocation, branch destination).

#### 2. Range Mode

```bash
python3 scripts/diff_inspect.py diff.json --range 920-930
```

Shows all instructions in an index range regardless of match type. Essential for examining a specific code region in detail.

#### 3. Summary Mode

```bash
python3 scripts/diff_inspect.py diff.json --summary
```

Quick count of instructions by match type. Use this to verify changes improved the right metrics.

#### 4. Diagnose Mode

```bash
python3 scripts/diff_inspect.py diff.json --diagnose
```

Full root cause analysis. Outputs:

- **Match summary**: instruction breakdown by type
- **Root causes**: stack/offset shifts (with histogram), register swap pairs, symbol relocations, branch destination noise
- **Actionable mismatches**: diff_ops, insert/delete clusters, real replaces (excluding noise)
- **Noise budget**: how many diff_arg instructions are fully explained by root causes vs unexplained

This is the primary triage tool for deciding whether a function is worth more work. If the noise budget shows >95% of diff_args are explained by register/offset/symbol cascades, the function is likely at its limit.

#### 5. Clusters Mode

```bash
python3 scripts/diff_inspect.py diff.json --clusters
```

Groups insert/delete instructions into contiguous clusters (gap <= 2 instructions). For each cluster shows:
- Index range and size (N inserts / M deletes)
- Dominant opcodes
- Surrounding context with annotations

Clusters represent actual structural code differences (extra null checks, different code generation paths, missing/added code blocks). These are the most promising targets for source-level fixes.

#### 6. Register Swaps Mode

```bash
python3 scripts/diff_inspect.py diff.json --regswaps
```

Detailed register swap pair analysis:
- Separates GPR (general purpose, *may* be fixable via declaration reorder) from FPR (floating point, usually unfixable)
- Shows count, first/last index, and span for each swap pair
- A single dominant pair spanning the entire function (like r20<->r21 x111) strongly suggests a cascaded allocation difference (unfixable)
- Multiple small-span pairs may indicate local allocation choices (potentially fixable)

#### 7. Offsets Mode

```bash
python3 scripts/diff_inspect.py diff.json --offsets
```

Analyzes immediate/offset value differences:
- Histogram of offset deltas (base - target)
- Identifies the dominant delta (stack frame size difference)
- Lists outlier offsets that don't match the dominant delta

A single dominant delta means "pure stack frame shift" (unfixable). Outliers may indicate struct layout differences or different member access patterns (potentially fixable).

#### 8. Replaces Mode

```bash
python3 scripts/diff_inspect.py diff.json --replaces
```

Categorizes `replace` instructions into:
- **Symbol-reloc noise**: Same opcode, but our build has an extra Symbol-type argument due to relocation differences (static variable scope numbering, ICF merged addresses). These are unfixable linker artifacts.
- **Real structural replaces**: Different opcodes or genuinely different computation. These are potential fix targets.

For OnBeat: 165 of 195 replaces were symbol-reloc noise, leaving only 30 real structural differences.

#### Direct Invocation

```bash
python3 scripts/diff_inspect.py --symbol "private: void __cdecl RhythmBattle::OnBeat(void)" --diagnose
```

Runs objdiff-cli internally, generates JSON to a temp file, then analyzes. Avoids the two-step workflow.

### Design Decisions

**Why JSON input, not direct objdiff integration?**
The JSON file is a stable intermediate format. You generate it once and can run multiple analysis modes without rebuilding. It also allows sharing diffs between sessions and agents.

**Why separate modes instead of one big report?**
Different questions need different levels of detail. `--summary` is a 5-line check. `--diagnose` is a full report. `--range` is surgical. Having separate modes keeps output focused and avoids information overload on 4000-instruction functions.

**Why classify replaces separately?**
In the initial OnBeat analysis, 195 replace instructions looked alarming. But 165 of them were just the linker resolving static variable names differently (e.g., `lbl_82F61470` vs `?finale_phaseout_02@?BII@??OnBeat@...`). Without the noise/real split, you'd waste time investigating non-issues.

### Integration with the Decomp Workflow

The typical workflow for investigating a hard function:

```bash
# 1. Generate the diff
./bin/objdiff-cli diff "symbol" --include-instructions --build --incremental \
    -f json -o /tmp/claude/diff.json

# 2. Quick triage: is this worth investigating?
python3 scripts/diff_inspect.py /tmp/claude/diff.json --summary

# 3. Root cause analysis: what's causing the mismatch?
python3 scripts/diff_inspect.py /tmp/claude/diff.json --diagnose

# 4. If promising, drill into specific areas:
python3 scripts/diff_inspect.py /tmp/claude/diff.json --clusters    # find structural diffs
python3 scripts/diff_inspect.py /tmp/claude/diff.json --regswaps    # check if register-driven
python3 scripts/diff_inspect.py /tmp/claude/diff.json --replaces    # filter replace noise

# 5. Examine specific regions found in step 4:
python3 scripts/diff_inspect.py /tmp/claude/diff.json --range 920-930

# 6. After making a code change, rebuild and compare:
./bin/objdiff-cli diff "symbol" --include-instructions --build --incremental \
    -f json -o /tmp/claude/diff.json
python3 scripts/diff_inspect.py /tmp/claude/diff.json --summary     # did it improve?
python3 scripts/diff_inspect.py /tmp/claude/diff.json --diagnose    # what changed?
```

---

## Worktree Setup Notes

The experiment campaign used a git worktree at `/tmp/claude/onbeat-fix`. Setting up a worktree for objdiff requires careful symlinking:

```bash
git worktree add /tmp/claude/onbeat-fix --detach HEAD

# IDE support
ln -s /home/free/code/milohax/dc3-decomp/compile_commands.json /tmp/claude/onbeat-fix/
ln -s /home/free/code/milohax/dc3-decomp/.clangd /tmp/claude/onbeat-fix/

# objdiff needs these
ln -sf /home/free/code/milohax/dc3-decomp/bin /tmp/claude/onbeat-fix/bin
ln -sf /home/free/code/milohax/dc3-decomp/orig /tmp/claude/onbeat-fix/orig
ln -sf /home/free/code/milohax/dc3-decomp/objdiff.json /tmp/claude/onbeat-fix/objdiff.json
ln -sf /home/free/code/milohax/dc3-decomp/build/373307D9/asm /tmp/claude/onbeat-fix/build/373307D9/asm
ln -sf /home/free/code/milohax/dc3-decomp/build/373307D9/obj /tmp/claude/onbeat-fix/build/373307D9/obj
ln -sf /home/free/code/milohax/dc3-decomp/build/compilers /tmp/claude/onbeat-fix/build/compilers

# build/tools must be copied (not symlinked) because ninja rebuild rules break on symlinks
cp -r /home/free/code/milohax/dc3-decomp/build/tools /tmp/claude/onbeat-fix/build/tools

# Copy build config
cp /home/free/code/milohax/dc3-decomp/build/373307D9/config.json /tmp/claude/onbeat-fix/build/373307D9/

# Disable download rules in build.ninja (replace with phony)
# Disable configure regeneration rule
```

This was error-prone enough that it should probably be scripted for future use.

## Files Changed

| File | Change |
|---|---|
| `src/system/hamobj/RhythmBattle.cpp:837` | Removed redundant `&& unk130` null guard |
| `scripts/diff_inspect.py` | Added `--replaces` mode, `categorize_replaces()` function, updated `--diagnose` to show replace breakdown |
| `scripts/analyze_replaces.py` | Deleted (functionality integrated into diff_inspect.py) |

## Conclusion

OnBeat at 92.1% is conclusively at its limit. The remaining 7.9% gap is dominated by a register allocation cascade from the `__FILE__` string length difference — a compiler heuristic we cannot influence from source code. The 43 LINKER_MERGED calls, tail call optimization, and boolean mask differences account for the rest.

The real win from this session is the `diff_inspect.py` tool suite, which provides a systematic methodology for triaging at-limit functions. The diagnose -> noise budget -> drill-in workflow demonstrated here can be applied to any function where the simple "try stuff and measure" approach has stalled.
