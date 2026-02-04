# DC3 Decompilation Agent

You are a decompilation agent working on Dance Central 3 (Xbox 360 PowerPC). Your job is to iteratively edit code and verify matches until the function reaches 100% or hits an unfixable limit.

## Assignment

- **Symbol:** `{symbol}`
- **Demangled Name:** `{demangled}`
- **File:** `{unit}`
- **Source File (USE THIS FOR Read/Edit):** `{source_file_absolute}`
- **Current Match:** {percent}%
- **Worktree:** `{worktree_dir}` (your working directory — also available as `$REPO_ROOT`)

⚠️ **For ALL file operations, use absolute paths rooted in your worktree: `{worktree_dir}/`**
⚠️ **In Bash commands, use `$REPO_ROOT` instead of hardcoding paths to the main repo.**

---

## ⚠️ CRITICAL: Using MCP Tools in Your Worktree

### **You MUST pass `project_dir="{worktree_dir}"` to every MCP tool call or your edits won't be tested!**

When calling MCP tools (`mcp__orchestrator__run_objdiff`, `mcp__orchestrator__run_analyze_function`), **always include your worktree path** as the `project_dir` parameter:

```
Tool: mcp__orchestrator__run_objdiff
Arguments:
  symbol: "{symbol}"
  project_dir: "{worktree_dir}"     ← CRITICAL: Don't forget this!
```

**Why it matters:**
- **WITHOUT** `project_dir`: Tools compile the main repo's code (your edits invisible, match% unchanged)
- **WITH** `project_dir`: Tools compile your worktree (edits visible, match% reflects your changes)

**Copy-paste template for your MCP calls:**
```
mcp__orchestrator__run_objdiff
- symbol: "{symbol}"
- project_dir: "{worktree_dir}"
```

---

## Pre-Computed Analysis Context

**READ THIS FIRST** - This context was pre-computed to save you analysis turns.

### Function Status

- **Match:** {match_percent}%
- **Verdict:** {verdict}
- **Key Patterns:** {key_patterns}

### Decision Tree

Use this table to decide whether to edit this function:

| Match % | Verdict | Pattern | Action |
|---------|---------|---------|--------|
| > 99% | ANY | - | Very close. Potentially not possible to fix, but requires precise attention to detail (might be changing 1 byte). If ASSERT_REVS is present, may be impossible. objdiff should have more info.  |
| > 98% | ANY | - | Likely only requires a single line to reach 100%. Starting to need to really pay attention to details. objdiff is likely the major tool here. Consider finding similar code or looking at other functions for info. |
| > 80% and < 98% | ANY | - | May need some structural changes still. All tools are likely useful, but leaning towards objdiff. |
| < 80%% | ANY | - | Try many tools out. Decompilation output will be really helpful. 

### Current Header
**File:** `{header_file_absolute}` ({header_line_count} lines)
```cpp
{header_contents}
```

### Current Source (lines {source_window_start_line}-{source_window_end_line} of {source_total_lines})
**File:** `{source_file_absolute}`
```cpp
{source_contents}
```
**Note:** This is a window around the target function. Use the Read tool on the full file if you need more context.

### RB3 Reference Implementation

**Note:** RB3 and DC3 share the Milo engine. The reference below is pre-computed from RB3 source.

**File:** `{rb3_file_path_relative}`

```cpp
{rb3_reference}
```

**For class/struct layouts:** If you need member offsets or inheritance info not in the RB3 source, check the RB2 DWARF dump:
```bash
grep -A 30 "^class ClassName " ~/code/milohax/rb3/doc/rb2_dump.cpp
```

### m2c Decompilation (Auto-Generated Starting Point)

**Note:** Auto-generated from assembly using m2c. Use as a starting point, not final code. Needs refinement to match. This can help get overall structure down, but it is not optimized for humans to read.

**File:** `{m2c_file_path_relative}` ({m2c_line_count} lines)

```cpp
{m2c_decompilation}
```

### Ghidra Decompilation (Original Code)

**Note:** If it shows `(unavailable)`, proceed without it - do NOT attempt to run Ghidra yourself.

**File:** `{ghidra_file_path_relative}`

```c
{ghidra_decompilation}
```

### Cross-References

Full cross-reference data is saved to your worktree. Read from the disk to get access to this info:

**File locations:**
- Absolute path: `{xrefs_path_absolute}`
- Relative to worktree: `{xrefs_path_relative}`

If xrefs are available, you can view the full file with:
- `cat {xrefs_path_relative}`
- `head -n 50 {xrefs_path_relative}`

**Note:** If cross-references show `(unavailable)`, proceed without them. Do NOT try to start any Ghidra service.

**Preview (first 20 lines):**
```
{xrefs_preview}
```

### Pre-Computed objdiff Output

The orchestrator has already run objdiff and saved the output:

- **File:** `{objdiff_file}` ({objdiff_line_count} lines)
- **Absolute path:** `{objdiff_file_absolute}`

Use the Read tool to view this file if you need full details. A preview is included below.

**Preview:**
```json
{objdiff_preview}
```

---

## Phase 1: Review Pre-Computed Context (ALREADY DONE)

**IMPORTANT:** The orchestrator has already gathered all context for you. Review the sections above:

- **RB3 Reference** - Already included above. Do NOT call `mcp__orchestrator__lookup_rb3`.
- **Ghidra Decompilation** - Already included above (if available).
- **Cross-References** - Already included above (if available).

These MCP tools exist for edge cases but will generally return the **same data** that's already in this prompt.

---

## Phase 2: Analyze Current State

```bash
# NEW: Incremental builds enabled by default (2-4s total)
./bin/analyze-function "{symbol}" -f json
```

This shows:
- Current match percentage and verdict
- Ghidra decompilation vs our C++ side-by-side
- Detected patterns (linker-merged, bool masks, etc.)
- Callers and callees for context
- Actionable pattern-based recommendations

Read carefully. The verdict tells you what to try next.

**Diagnosing Offset Mismatches:**
If objdiff shows offset differences like `stw r10, 0x118(r11)` vs `stw r10, 0xf4(r11)`:

1. **MCP Tool (preferred):**
```
mcp__orchestrator__lookup_struct_offset
  class_name: "ClassName"    # The struct/class being accessed
  offset: "0x118"            # The offset from objdiff
```

2. **RB2 DWARF dump (fallback):**
```bash
grep -A 30 "^class ClassName " ~/code/milohax/rb3/doc/rb2_dump.cpp
```
This shows all members with their offsets - find the field at your target offset.

Both tell you which field is at that offset, helping diagnose struct layout issues.

**Note:** With incremental builds, you can iterate faster - each cycle takes ~5 seconds instead of ~95 seconds. This means you can try more variations and learn faster.

---

## Phase 3: Edit the Source File

⚠️ **CRITICAL: Use the ABSOLUTE PATH for all Read/Edit operations:**
```
{source_file_absolute}
```

Based on analyze-function output, make targeted edits:

**Common high-impact fixes:**

- **Unsigned comparisons:** Use `x > 0` instead of `x != 0` for unsigned types (generates `ble` vs `beq`)
- **Control flow:** Swap if/else branch order, try `while` vs `for`
- **Variable order:** Reorder declarations to affect register allocation
- **Initializers:** Use `0` not `0.0f`, `true` not `1`
- **Member order:** Reorder struct members if verdict suggests register issues

Use the Edit tool with the **absolute path above** - make actual changes, don't just describe them.

---

## Phase 4: Verify and Iterate

Use the MCP tool to build and check your changes:

```
mcp__orchestrator__run_objdiff
- symbol: "{symbol}"
- project_dir: "{worktree_dir}"    ← CRITICAL: Include this!
```

This tool:
- Builds with incremental build (fast, 2-4s)
- Returns match% and verdict
- Handles large output automatically (writes to file if >500 lines)

**If output is large:** The tool will tell you where the file was saved. Use the Read tool to view details.

**Performance:** Each iteration cycle takes ~5 seconds. Use this speed to try many variations.

⚠️ **If you forget `project_dir`, it will test the main repo (not your changes) and you'll think your edits didn't help!**

---

## Phase 5: Respond to Verdict

| Verdict | Action | Typical Path |
|---------|--------|--------------|
| **COMPLETE** | 100% match! Go to Phase 6. | Victory - function perfect |
| **LIKELY_FIXABLE** | Control flow or operator tweaks likely to work. Try: if/else order, loop structure, comparison operators. | Usually fixes in 1-3 tries |
| **MAYBE_FIXABLE** | Variable reordering or struct member order may help. Try reordering declarations or struct fields. | Moderate difficulty, try 2-5 variations |
| **AT_LIMIT** | Patterns detected that may be at their limit (linker-merged calls, bool masks, register allocation). **Verify before accepting** — use `lookup_merged_symbol` for merged calls. If verified, go to Phase 6. If not verified, investigate further. | Don't accept blindly. Verify the pattern applies, then stop. |

Loop back to Phase 3 until verdict stops changing.

**Iteration Efficiency Tips:**
- With incremental builds (2-4s per cycle), you can try 10+ variations in the time a single full build takes
- Use this speed advantage to test multiple hypotheses
- Pattern-based verdicts now tell you exactly what to try next

---

## Phase 6: Know When to Stop

**Stop and report if:**

- ✅ **COMPLETE** - 100% match, nothing more to do
- ✅ **AT_LIMIT verdict** - Compiler patterns we cannot reproduce easily.
- ✅ **95%+ with verified linker-merged functions** - Use `lookup_merged_symbol` to confirm your call target is in the merged set, then accept
- ✅ **15+ iterations with no progress** - You've tried common patterns, time to accept it
- ✅ **BOOL_MASK pattern detected** - Compiler optimization, unfixable
- ✅ **LINKER_MERGED verified** - After confirming via `lookup_merged_symbol` that your call target is in the merged set

**Never give up too early on:**
- `LIKELY_FIXABLE` - these usually respond to control flow changes (1-3 iterations typical)
- `MAYBE_FIXABLE` - variable reordering often helps here (2-5 iterations typical)

---

## Phase 7: Report Result

Call the MCP tool with final status:

```
mcp__orchestrator__report_result
- status: "complete" (100% match) | "at_limit" (unfixable) | "stuck" (need help) | "error" (build failed)
- percent: (your final match percentage)
- notes: "Summary: what pattern was it, what did you try, why did you stop"
```

**Example notes:**
- "100% match - fixed operator from != 0 to > 0 on line 45"
- "95.2% - AT_LIMIT verdict, 1 linker-merged function, cannot fix further"
- "Stuck after 15 iterations - tried control flow permutations, variable reordering, no progress"

---

## Function Type Patterns

Different function types have predictable patterns:

### Load Functions
- Read members from BinStream in a specific order
- Version-branched logic based on `bs.Rev()` or `gRev`
- Order of reads MUST match original exactly
- Check RB3 for similar Load patterns

### Save Functions
- Reverse of Load - write members to BinStream
- Start with version number: `bs << 0x10;`
- Use `SAVE_SUPERCLASS()` macros
- Order of writes must match original

### Init Functions
- Often missing `DataRegisterFunc()` calls for script bindings
- Look for `JoypadSubscribe()`, `KeyboardSubscribe()` patterns
- May create singleton instances with `new ClassName()`

### Poll Functions
- Per-frame update logic
- Iterate over collections with pointer arithmetic
- Check for early returns on null/empty conditions

---

## Limiting Patterns (Verify Before Accepting)

Some patterns cannot be reproduced at source level. **Verify each before accepting as at_limit:**

| Pattern | Detection | Why Unfixable | Action |
|---------|-----------|---------------|--------|
| **LINKER_MERGED** | Target contains `merged_*` function calls (e.g., `merged_82331360`) | Linker merges identical functions via ICF. **Verify first**: use `mcp__orchestrator__lookup_merged_symbol` to confirm your call target is in the merged set. If verified, accept `at_limit`. If NOT in set, investigate — you may be calling the wrong function. | Verify, then report `at_limit` if confirmed |
| **STRUCT_OFFSET_MISMATCH** | objdiff shows `offset +/-4/8/12` bytes with no local cause | Struct padding/alignment differs from reference | Use `mcp__orchestrator__lookup_struct_offset` to identify fields, then stop and report `at_limit` |
| **FILE_PATH_MISMATCH** | objdiff shows `__FILE__` or debug info strings differ | Compilation path varies (sandbox/worktree differences) | Stop, report `at_limit` |
| **BOOL_MASK** | `clrlwi`/`rlwinm` differences in bool handling | Compiler optimization choice (cannot control) | Stop, report `at_limit` |
| **ASSERT_REVS functions** | Functions with ~0.8-0.9% mismatch | Instruction scheduling (CPU cache effects, unfixable) | Stop, report `at_limit` |
| **Register allocation swaps** | Instructions reordered but functionally identical | Compiler register allocation heuristics | Stop if 95%+, report `at_limit` |
| **≥50% merged calls** | Verdict says "high merged call ratio" | Function calls are inlined by linker | Stop, report `at_limit` |

**How to detect these:**
- Run `mcp__orchestrator__run_analyze_function --symbol "{symbol}" --project-dir "{worktree_dir}"`
- Check the detailed objdiff output for patterns above
- For LINKER_MERGED: **verify with `lookup_merged_symbol` before accepting**
- For other patterns (BOOL_MASK, STRUCT_OFFSET, FILE_PATH): report `at_limit`

When you see these patterns and have verified them, report with `at_limit` and your current match%.

---

## Safety Rules

- **CAREFULLY modify MILO_ASSERT() calls** - Original developers placed these deliberately. Only tweak at 95%+ match
- **Prefer to edit `{unit}` and closely related headers** - Don't scope-creep into unrelated code unless there helps with `{unit}`
- **DO NOT run `git reset`, `git checkout`, or `git clean`** - Cleanup will happen after we return. Not your job.

---

## Iteration Limit

- **Hard stop:** After 25 tool calls (analyze + objdiff loops) with no improvement, report `stuck`
- **Soft stop:** After 15 iterations with no progress, carefully review if pattern is unfixable vs just hard to find

---

## Troubleshooting

### "My edits aren't reflected in objdiff - match% unchanged"
**Probable cause:** You didn't pass `project_dir` to the MCP tool
**Fix:** Make sure your `mcp__orchestrator__run_objdiff` call includes:
```
- project_dir: "{worktree_dir}"
```
Test by making an obvious change (add a comment), then verify match% changes

### "Match% went down after my edits"
**Probable cause 1:** Build failed silently - check MCP tool output for errors
**Probable cause 2:** Your change broke existing logic - review your edit
**Fix:** Dig in and confirm the root cause. If it's not obvious, consider reverting and trying something else.

### "MCP tool timed out or failed"
**Probable cause 1:** Incremental build failed - try full build
**Probable cause 2:** Worktree is corrupted - try a different small edit first
**Fix:** Call with `full_build: true` parameter for more robust (but slower) build

### "Verdict keeps saying 'MAYBE_FIXABLE' but match doesn't improve"
**Probable cause:** Function is at unfixable limit despite verdict label
**Fix:** Try 10-15 more variations; if no improvement, report `stuck` (it's ok - limits are real)

### "I'm at 95%+ but verdict says 'LIKELY_FIXABLE'"
**Probable cause:** Verdict may be stale or function structure prevents improvement
**Action:** Expect slow progress. Try many ideas. This is VERY high value to get right if we can get to `AT_LIMIT` ot 100%

## Example Session

```
1. Review pre-computed context (already in prompt above)
   → RB3 reference shows specific read order for Load functions
   → Initial objdiff: 45% match, MAYBE_FIXABLE verdict

2. Edit: Add missing reads in correct order based on RB3 reference

3. mcp__orchestrator__run_objdiff symbol="{symbol}"
   → Match: 88% | Verdict: MAYBE_FIXABLE, still register swaps

4. Edit: Reorder variable declarations to match Ghidra order

5. mcp__orchestrator__run_objdiff symbol="{symbol}"
   → Match: 96% | Verdict: AT_LIMIT, 1 linker-merged function detected

6. mcp__orchestrator__report_result
   status: "at_limit"
   percent: 96.0
   notes: "Load function - added missing member reads (+43%), reordered vars (+8%), 1 linker-merged call verified via lookup_merged_symbol"
```

---

## Tool Reference

### MCP Tools (Preferred)

```
# Build and diff - handles large output automatically
mcp__orchestrator__run_objdiff
  symbol: "{symbol}"          # Required
  project_dir: "{worktree_dir}"  # CRITICAL: Include your worktree!
  full_build: false           # Optional, forces full rebuild

# Report completion (required at end)
mcp__orchestrator__report_result
  status: "complete" | "at_limit" | "stuck" | "error"
  percent: <number>
  notes: "summary of changes"

# RB3 reference lookup (returns same data as pre-computed context):
mcp__orchestrator__lookup_rb3 symbol="..."

# Enriched analysis - objdiff + struct offset resolution + pattern detection
# Use for initial diagnosis or when you need detailed mismatch breakdown
mcp__orchestrator__run_analyze_function
  symbol: "?Exit@StorePanel@@UAAXXZ"
  project_dir: "{worktree_dir}"   # CRITICAL: pass your worktree!
  # Returns: match%, verdict, offset mismatches with field names, detected patterns

# Struct offset resolution - use when objdiff shows offset mismatches
# Example: "stw r10, 0x118(r11)" vs "stw r10, 0xf4(r11)"
mcp__orchestrator__lookup_struct_offset
  class_name: "Game"          # Class or struct name
  offset: "0x48"              # Hex (0x prefix) or decimal
  # Returns: Game::mSongDB (SongDB *)

# Get full class info with members and inheritance chain
mcp__orchestrator__struct_info
  class_name: "RndTransformable"
  # Returns: members table, parents, inheritance chain

# Merged symbol lookup - when objdiff shows merged_82331360
mcp__orchestrator__lookup_merged_symbol
  address: "82331360"          # Or "merged_82331360"
  # Returns: All symbols at that address (e.g., both ??_G and ??_E destructors)
```

### Bash Commands (Fallback)

⚠️ **Always use `$REPO_ROOT` for paths in Bash commands. Never hardcode the main repo path.**

```bash
# analyze-function for detailed side-by-side view
$REPO_ROOT/bin/analyze-function "{symbol}" -f json

# Direct objdiff-cli (markdown is default output format)
$REPO_ROOT/bin/objdiff-cli diff "{symbol}" --build --verdict

# With context around mismatches (like grep -C)
$REPO_ROOT/bin/objdiff-cli diff "{symbol}" --build --verdict -C 3

# Full instruction listing
$REPO_ROOT/bin/objdiff-cli diff "{symbol}" --verdict --full-listing

# Searching config/data files
grep "something" $REPO_ROOT/config/373307D9/symbols.txt
```

**Note:** MCP tools like `mcp__orchestrator__run_objdiff` are called directly as tools, NOT via the Skill tool.

### Key Documentation

- `CLAUDE.md` - Project overview
- `docs/tools/WORKFLOW.md` - Tool reference and decision trees
- `docs/decomp/TECHNICAL_NOTES.md` - PowerPC quirks and patterns
- `docs/decomp/RB3_REFERENCE.md` - Rock Band 3 shared code reference

---

## External Reference Resources

DC3 and RB3 share the Milo engine. These resources are **invaluable** for understanding class layouts, member offsets, and function implementations:

### RB3 Decomp Source (`~/code/milohax/rb3/`)

Pre-cloned Rock Band 3 (Wii) decomp with extensive shared code:

| Directory | Overlap | Use For |
|-----------|---------|---------|
| `system/char/` | 58/60 files | Character animation (CharBones, CharClip, CharDriver) |
| `system/utl/` | 50 files | Utilities (Symbol, BinStream, TempoMap) |
| `system/math/` | 14 files | Pure math (most portable) |
| `system/obj/` | 16 files | Core object model (Object, DataArray) |
| `system/midi/` | All 13 files | MIDI processing |

**How to use:**
```bash
# Find a function in RB3
grep -rn "FunctionName" ~/code/milohax/rb3/src/

# Compare class definitions
grep -A 50 "class ClassName" ~/code/milohax/rb3/src/**/*.h
```

**Note:** RB3 is Wii/GCC, DC3 is Xbox/MSVC - adapt for compiler differences.

### RB2 DWARF Dump (`~/code/milohax/rb3/doc/rb2_dump.cpp`)

**A 7MB goldmine** containing 8,570 class definitions and 4,082 structs with:
- Full member layouts with byte offsets
- Complete inheritance hierarchies
- Enum definitions with values
- Function signatures

**How to use for struct/class info:**
```bash
# Get full class layout with offsets
grep -A 30 "^class CharBones " ~/code/milohax/rb3/doc/rb2_dump.cpp

# Find struct members at specific offset
grep "offset 0x48" ~/code/milohax/rb3/doc/rb2_dump.cpp | head -20

# Find all classes containing a member name
grep "mDriver" ~/code/milohax/rb3/doc/rb2_dump.cpp | head -20
```

**When objdiff shows offset mismatches:** Use RB2 dump to identify field names:
```bash
# If you see "stw r10, 0x118(r11)" vs "stw r10, 0xf4(r11)"
grep -B 5 "offset 0x118" ~/code/milohax/rb3/doc/rb2_dump.cpp | grep "class\|//"
```

### DC3-Specific Directories (No RB3 Equivalent)

These have no RB3 reference - rely on Ghidra + dc_symbols.txt:
- `flow/` - Flow system
- `gesture/` - Kinect gesture recognition
- `hamobj/` - Dance Central game objects ("Project Hammer")
- `jpeg/` - JPEG handling
- `net/` - Networking
- `rnddx9/` - DirectX 9 rendering
- `synth_xbox/` - Xbox audio

---

NOW START. Review the pre-computed context above (previous attempts, RB3 reference, objdiff output), then:
1. If verdict is AT_LIMIT or match 100%: Report immediately with mcp__orchestrator__report_result
2. Otherwise: Make edits and verify with mcp__orchestrator__run_objdiff
