# MergeObjectsRecurse: Tooling Retrospective

**Date**: 2026-03-05
**Function**: `MergeObjectsRecurse` in `src/system/obj/Utl.cpp`
**Result**: 97.8% -> 100% match

## Summary

Improving `MergeObjectsRecurse` from 97.8% to 100% exposed gaps in the MCP orchestrator tooling. The fix itself was relatively simple (restructure subdirs loop, add unsigned cast), but diagnosing the last 1-instruction mismatch required breaking out of the MCP tools and using `objdiff-cli` directly and then `wibo + cl.exe` for ASM listing output. This session documents what happened, what tools were used vs what was needed, and concrete proposals for better tooling.

## The Fix

### What changed (97.8% -> 99.6%)

Removed the named `oPtr` reference variable and used `subDirs[i]` directly:

```cpp
// Before (97.8%)
for (int i = 0; i < subDirs.size(); i++) {
    ObjDirPtr<ObjectDir> &oPtr = subDirs[i];
    if (oPtr != NULL)
        MergeObjectsRecurse(oPtr, toDir, filt, false);
}

// After (99.6%)
for (int i = 0; i < subDirs.size(); i++) {
    if (subDirs[i] != NULL)
        MergeObjectsRecurse(subDirs[i], toDir, filt, false);
}
```

This fixed load ordering (4 instructions) and r28<->r29 register swap (5 instructions). The named reference caused the compiler to compute the vector address and load through it differently.

### What changed (99.6% -> 100%)

Cast the null check to `(unsigned int)` to force `cmplwi` (unsigned) instead of `cmpwi` (signed):

```cpp
if ((unsigned int)(ObjectDir *)subDirs[i])  // generates cmplwi
// vs
if (subDirs[i] != NULL)                     // generates cmpwi
```

## Timeline & Tool Usage

### Phase 1: Initial diagnosis (MCP tools -- worked well)

1. `run_objdiff(concise=false)` -- got match%, mismatch list, verdict, patterns
2. `lookup_rb3` -- found RB3 reference implementation
3. `run_diff_inspect(mode="diagnose")` -- root cause analysis
4. `run_diff_inspect(mode="mismatches")` -- instruction table of all mismatches

**Verdict**: These tools worked great for the initial diagnosis and getting from 97.8% to 99.6%.

### Phase 2: Diagnosing the last instruction (MCP tools fell short)

After reaching 99.6% with 1 remaining mismatch (`cmplwi` vs `cmpwi`), I needed to understand WHY the compiler was choosing signed vs unsigned comparison.

**What I needed**: The instructions BEFORE the mismatch (specifically instruction 133: `lwz r3, 0xc, r11`) to understand what value was being compared. I also needed to see how a MATCHING null check earlier in the same function (instruction 95: `cmplwi cr6, r30, 0x0`) worked for comparison.

**What `run_objdiff` gave me**: Only the mismatched instruction `[134] replace: cmplwi vs cmpwi`. With `context=8`, I should have gotten surrounding instructions, but the context only shows around mismatches -- when there's only 1 mismatch in a 15-instruction region, the context still doesn't show the full picture of matching instructions in other parts of the function.

**What `run_diff_inspect(mode="mismatches")` gave me**: A table of ONLY mismatched instructions. Matching instructions are omitted. I couldn't see the data flow into the mismatch.

### Phase 3: Breaking out to CLI tools

**Tool 1: `objdiff-cli diff --full-listing`**

I ran objdiff-cli directly with `--full-listing` to get ALL instructions side-by-side, including matching ones. This immediately showed me:
- Instruction 133 (`lwz r3, 0xc, r11` -- loads mObject) flows into instruction 134 (the comparison)
- The `bl 0x0` vs `bl 0x8` relocation difference at the recursive call
- How the matching null check at instruction 95 uses `cmplwi` with a callee-saved register

**Tool 2: `wibo + cl.exe /FAs` (ASM listing)**

To confirm whether my source-level changes actually affected the generated code, I compiled with `/FAs` to get an annotated assembly listing with source line numbers. This showed:
- The `(unsigned int)` cast was being ignored by the compiler (still `cmpwi`)
- Which C++ line mapped to which instruction
- The compiler's internal decisions about comparison opcodes

## Tool Gap Analysis

### Gap 1: No full listing through MCP

**The problem**: `run_objdiff` and `run_diff_inspect` only show mismatched instructions. There is no way to see matching instructions through the MCP interface. The `--full-listing` flag exists in `objdiff-cli` but is not exposed through the orchestrator.

**Why it matters**: When diagnosing a single mismatch, the surrounding context of MATCHING instructions is often more informative than the mismatch itself. Data flow analysis (what loaded r3?) requires seeing the instructions before the mismatch.

**Concrete example**: I needed to see instructions 126-140 (15 instructions) but only instruction 134 was mismatched. The MCP tools showed me 1 line. The CLI showed me all 15.

**Proposed fix**: Add a `full_listing` boolean parameter to `run_objdiff`, or better, add a `region` mode to `run_diff_inspect` that shows all instructions (matching + mismatched) in a range:

```
run_diff_inspect(symbol="Foo", mode="region", region_start=126, region_end=140)
```

Or simpler: add `full_listing: bool` to `run_objdiff` that passes `--full-listing` to objdiff-cli. This already exists in the CLI, just needs plumbing.

### Gap 2: No ASM listing (source-to-instruction mapping)

**The problem**: When trying different source-level changes, I couldn't tell if my changes were actually affecting the generated code without compiling with `/FAs` manually. I had to extract the compile command from `ninja -t commands`, modify it to add `/FAs /Fa<output>`, and run it through wibo.

**Why it matters**: The source-to-instruction mapping is invaluable for understanding compiler decisions. Seeing `; 337: if ((unsigned int)(ObjectDir *)subDirs[i])` next to `cmpwi cr6,r3,0` immediately tells you the cast isn't working.

**Concrete example**: I tried 5 different null check patterns (`if (ptr)`, `if (ptr != NULL)`, `if (ptr != 0)`, `(unsigned int)` cast, `(ObjectDir*)` cast). Each time I had to rebuild and check via `run_objdiff`. If I could see the annotated ASM, I'd have known immediately that the compiler was ignoring the casts.

**Proposed fix**: Add an `asm_listing` mode to `run_diff_inspect` or a standalone tool that compiles with `/FAs` and returns the annotated listing for a specific function. The compile command can be extracted from `ninja -t commands <target>`.

### Gap 3: Context parameter doesn't help with isolated mismatches

**The problem**: `run_objdiff(context=8)` is designed to show N instructions of context around each mismatch. But when there's only 1 mismatch in a 27-instruction region (like our case), the context around that mismatch doesn't show the important matching instructions at the BEGINNING of the region (loop setup, data loads).

**Why it matters**: The `context` parameter is grep-like -- it shows N lines before/after. But for assembly analysis, you often need to see the entire logical block (loop setup through loop end), not just a window around the mismatch.

**Proposed fix**: The `region` mode suggested above would address this. Alternatively, `run_objdiff` could auto-detect region boundaries (branch targets) and show the full basic block containing each mismatch.

### Gap 4: Permuter didn't try the winning transformation

**The problem**: The permuter's `signed_unsigned` pattern generates casts like `(int)ptr`, `(unsigned int)ptr`, `(unsigned long)ptr`. It DID try `(unsigned int)` casts on comparisons but applied them to the ObjDirPtr, not to the result of the pointer conversion. And its `variable_extraction` pattern removes named variables but doesn't try removing them to change codegen.

**Why it matters**: The two key fixes were: (1) removing the named `oPtr` variable (codegen change), and (2) using `(unsigned int)(ObjectDir*)subDirs[i]` (unsigned cast on extracted pointer). Neither was in the permuter's search space.

**Proposed fix**: Two new permuter patterns:
- **`reference_elimination`**: Try removing intermediate reference variables and using the expression directly (the `oPtr` -> `subDirs[i]` transformation)
- **`conversion_cast`**: When a user-defined conversion operator is involved, try extracting the converted value with an explicit cast (the `(unsigned int)(ObjectDir*)expr` pattern)

### Existing tools I missed

**`objdiff-cli diff --full-listing`**: This flag exists and does exactly what I needed for Gap 1. I knew about it (used it eventually) but couldn't use it through the MCP interface. The fix is just plumbing it through `run_objdiff`.

**`objdiff-cli diff -C N`**: The context flag. This IS exposed through the MCP tool's `context` parameter, and I did use it. But it wasn't sufficient for this case (isolated mismatch in a large region).

**`ninja -t commands`**: Available via Bash, works fine. Not really a "missed" tool, more of a "shouldn't need to do this manually" situation.

### Why I missed `--full-listing`

The `docs/tools/INDEX.md` says "Do not call objdiff-cli directly" and routes everything through MCP tools. The MCP tool descriptions don't mention `--full-listing` as an available option. The `WORKFLOW.md` mentions `--full-listing` in the CLI section but the strong directive to use MCP tools discouraged going to the CLI.

**Proposed fix**: Either (a) expose `--full-listing` through the MCP tool, or (b) soften the "do not call objdiff-cli directly" guidance to "prefer MCP tools, but use CLI directly when you need full listings or advanced flags not available through MCP."

## Lessons Learned

1. **Named reference variables affect codegen**: Even when a reference should be a pure alias, MSVC PPC's optimizer treats `subDirs[i]` differently from `oPtr` where `ObjDirPtr<ObjectDir> &oPtr = subDirs[i]`. The named reference forces the compiler to compute the address first, while direct indexing allows loading members directly from the parent struct.

2. **`cmplwi` vs `cmpwi` for pointer null checks**: The MSVC PPC compiler inconsistently uses signed vs unsigned comparison for pointer null checks. `(unsigned int)(ObjectDir*)ptr` forces unsigned. This may be useful for other functions with the same pattern.

3. **MCP tools are great for 90% of the work**: The initial diagnosis, RB3 reference lookup, and iterative objdiff checks all worked perfectly through MCP. The gap only appeared for the last 0.4% where source-to-instruction mapping was needed.

4. **The permuter is a time-saver but has blind spots**: It correctly identified the function as having signed/unsigned and register swap issues, but its transformation patterns didn't cover "remove named reference variable" or "cast through conversion operator chain."

5. **Full instruction listings are essential for single-mismatch debugging**: When there's only 1 mismatch, the mismatch itself is rarely informative. The data flow leading INTO the mismatch is what matters.
