# Next Phase: Work Areas & Prompts

Quick reference for continuing DC3 decomp work. Each section has context and a copy-paste prompt for a new Claude Code terminal.

**Last Updated:** 2026-02-15

---

## Status Snapshot

| Metric | Value |
|--------|-------|
| Functions done (COMPLETE + AT_LIMIT) | 31,194 / 31,814 (98.1%) |
| Remaining workable | 620 |
| Code match (bytes) | 35.84% |
| Link errors | 242 (down from 437 after ICF glue + pdata fix) |
| Divergent (logic) | 754 functions with behavioral bugs |
| XEX built | `build/373307D9/default.xex` (19.6MB) |
| Build pipeline | `ninja build/373307D9/default.exe && python3 scripts/build/build_xex.py` (pdata fix + glue automated) |

---

## Work Area 1: Bulk Decomp (Remaining 620 Functions)

**Goal:** Improve match% on the 620 functions that aren't COMPLETE or AT_LIMIT.

**What to know:**
- These are partial matches — some code exists but doesn't fully match
- Top units: RhythmBattle (28), Voice (17), PlatformMgr_Xbox (16), ExternalMic (10)
- Use `mcp__orchestrator__query_functions` to find targets
- Use `mcp__orchestrator__run_recon` before starting any function
- RB3 reference available for system/ code via `mcp__orchestrator__lookup_rb3`

**Relevant docs:**
- `docs/STATE_OF_THE_DECOMP.md` — where the project is, with denominators
- `docs/decomp/REMAINING_WORK.md` — how to find targets (queries; replaces the archived GAP_ANALYSIS / SUBAGENT_STRATEGY / LOW_HANGING_FRUIT)
- `docs/decomp/TECHNICAL_NOTES.md` — compiler patterns

### Prompt: Sweep a specific unit

```
I'm working on the DC3 decompilation project. Your job is to improve function matches in a specific unit.

## Target Unit
Pick the highest-remaining unit from the orchestrator:
- Run: mcp__orchestrator__query_functions with unit_pattern="system/hamobj/RhythmBattle" (or another unit)
- Sort by match percentage descending — start with the closest-to-done functions

## Workflow for each function
1. Run mcp__orchestrator__run_recon on the function to understand its state
2. Read the source file and header
3. Check RB3 reference: mcp__orchestrator__lookup_rb3
4. Make changes to improve the match
5. Test with mcp__orchestrator__run_objdiff (pass project_dir!)
6. If stuck, run mcp__orchestrator__run_diff_inspect with mode="diagnose"
7. When done (100% or AT_LIMIT), report with mcp__orchestrator__report_result

## Rules
- Only edit the specific file for your function
- Don't modify MILO_ASSERT calls or OBJ_MEM_OVERLOAD macros without testing
- Use `x > 0` instead of `x != 0` for unsigned types
- Stop on LINKER_MERGED — those are unfixable
- Commit logical chunks as you go

Work through as many functions as you can. Report what you improved.
```

### Prompt: Batch check a unit for hidden matches

```
Run /batch-check on these units to find any hidden 100% matches:

system/hamobj/*
system/synth_xbox/*
system/os/*
system/ui/*

Report the results — how many newly COMPLETE, how many partial matches found.
```

---

## Work Area 2: Link Error Reduction

**Goal:** Reduce the 437 link errors (81 unique symbols) so the build links cleaner.

**What to know:**
- Build uses `/FORCE` to bypass errors — the PE is produced but may have issues
- **ICF-merged symbols (189 errors, 3 symbols):** DataArray::Node, operator delete, MemOrPoolFreeSTL — the original linker merged identically-compiled functions. Our split objects can't reconstruct the aliases. Needs COMDAT marking in dtk or manual aliasing.
- **lbl_* locals (14 errors):** dtk needs to globalize these data labels. External dependency.
- **.CRT initializers (24 errors):** Expected for hybrid build — decomp .obj has static init code that split objects don't.
- **Ogg Vorbis (13 errors):** Missing split objects from dtk.
- **Other (170+ errors):** Mixed decomp cross-refs, jump tables, merged symbols.

**Relevant docs:**
- `docs/plans/BUILD_ROADMAP.md` — full error breakdown and phases
- `docs/sessions/2026-02-11-x360-linking-pipeline.md` — linking details

### Prompt: Analyze and reduce link errors

```
I'm working on the DC3 decompilation project. Your job is to reduce the number of link errors in our hybrid Xbox 360 build.

## Context
The build links decomp .obj files with original split .obj files using the Xbox 360 linker (via wine). It uses /FORCE to produce a PE despite errors.

Current state: 437 errors, 81 unique symbols.

## Steps
1. Read docs/plans/BUILD_ROADMAP.md for the full error breakdown
2. Run the link and capture errors:
   ```
   WINEPREFIX=/tmp/claude/.wine ninja link 2>&1 | tee /tmp/claude/link_errors.txt
   ```
3. Parse the errors — categorize by type (LNK2001, LNK2019, LNK4006, etc.)
4. For each category, determine if we can fix it from the decomp side:
   - Missing symbol definitions → add to appropriate .cpp files
   - ICF aliases → check if we can provide COMDAT aliases
   - Cross-unit references → ensure the defining unit is Matching
5. Make fixes and re-link to verify error count drops

## What's fixable from our side
- Adding missing global definitions (like we did for TheHamProvider, HamSong::mPreferStreaming)
- Ensuring constructors/destructors exist in our source (like String::String)
- Adding stub implementations for missing functions

## What's NOT fixable (external deps)
- lbl_* symbols (needs dtk PR to globalize)
- ICF-merged symbols at root level (needs COMDAT support in dtk)
- Ogg Vorbis missing objects (needs dtk split config update)

Focus on what we CAN fix. Report: starting error count, ending error count, what you changed.
```

---

## Work Area 3: Xenia Boot Test

**Goal:** Boot the XEX on Xenia emulator and capture the first crash.

**What to know:**
- Requires a Windows machine with Xenia installed (not available in dev env)
- The XEX is at `build/373307D9/default.xex`
- Xenia has extensive debug flags: `--debug`, `--break_on_start`, `--log_level=3`
- Even a crash is valuable — tells us where the first failure is

**Relevant docs:**
- `docs/plans/BUILD_ROADMAP.md` — "Xenia Debugging Strategy" section
- `docs/sessions/2026-02-11-xexp-patch-generation-investigation.md` — XEXP format

### Prompt: Debug a Xenia crash

```
I'm working on the DC3 decompilation project. Xenia crashed when booting our hybrid XEX build.

## Crash Info
[PASTE XENIA LOG / CRASH OUTPUT HERE]

## Context
- Our build is a hybrid: decomp .obj files linked with original split .obj files
- The PE was linked with /FORCE (437 unresolved symbols)
- XEX packaging copies headers from the original game's XEX

## Steps
1. Read docs/plans/BUILD_ROADMAP.md for build context
2. Parse the crash address from the Xenia log
3. Look up the crash address in the linker map file to identify the function
4. Determine if it's decomp code or split-object code:
   - Decomp: check with mcp__orchestrator__run_objdiff, fix source
   - Split: likely a seam issue (struct layout, vtable offset, missing symbol)
5. Check if the crash is related to an unresolved symbol from the link errors
6. Propose a fix and rebuild

The goal is to get past this crash and closer to the title screen.
```

### Manual steps (on a Windows machine):

```powershell
# Basic boot test
xenia.exe --debug --log_level=3 path\to\default.xex 2>&1 | tee xenia_boot.log

# With break on start (to inspect initial state)
xenia.exe --debug --break_on_start --log_level=3 path\to\default.xex

# Break at specific address (after identifying crash point)
xenia.exe --debug --break_on_instruction=0xADDRESS path\to\default.xex
```

---

## Work Area 4: XEXP Patch Tooling (Strategy B)

**Goal:** Build a tool to patch individual functions into the original XEX for isolated testing.

**What to know:**
- The original game XEX works on Xenia. We want to replace individual function bodies with our compiled code to isolate failures.
- Xenia supports `.xexp` delta patches natively (loads `game.xexp` alongside `game.xex`)
- No open-source tool generates `.xexp` files — jeff (dtk) has a TODO for it
- Alternative: direct PE surgery (decompress original PE, patch function bytes, repackage)

**Relevant docs:**
- `docs/sessions/2026-02-11-xexp-patch-generation-investigation.md` — full XEXP format details
- `docs/plans/BUILD_ROADMAP.md` — Strategy A vs B comparison

### Prompt: Build function patching tool

```
I'm working on the DC3 decompilation project. I need a tool that patches individual decomp'd functions into the original game binary for isolated testing.

## Context
- Original XEX: works on Xenia
- Our compiled .obj files: contain decomp'd function implementations
- Goal: replace specific function bodies in the original PE with our compiled versions

## Approach Options

### Option A: Direct PE patching
1. Extract the PE from the original XEX (decompress)
2. Find a function by symbol name → look up its address in the original MAP/PDB
3. Copy our compiled function bytes over the original bytes
4. Handle relocations (our code may reference different addresses)
5. Repackage into a new XEX

### Option B: XEXP delta patch
1. Build an .xexp file (XEX2 with module_flags including PATCH_DELTA)
2. Include a DeltaPatchDescriptor (optional header 0x5FF) with block-level diffs
3. Xenia's ApplyPatch() handles the rest
See docs/sessions/2026-02-11-xexp-patch-generation-investigation.md for format details.

### Option C: Full PE replacement
1. Link our hybrid PE (decomp + split objects)
2. Package as XEX (already working: scripts/build/build_xex.py)
3. This is Strategy A — already done, just needs boot testing

## Deliverable
A Python script (scripts/patch_function.py) that:
- Takes: original XEX path, function symbol name, our .obj file path
- Outputs: patched XEX with that one function replaced
- Verifies: function sizes match (or pads with nops if ours is smaller)

Start with Option A — it's the most straightforward. Read the XEXP investigation doc first.
```

---

## Work Area 5: dtk Upstream Fixes

**Goal:** Get fixes into dtk (the decomp toolkit) for issues we can't fix on our side.

**What to know:**
- dtk repo: `~/code/milohax/dtk` (or wherever it lives)
- Issues to fix:
  1. **Globalize lbl_* symbols** — 14 local data labels need to become global for linking
  2. **COMDAT marking for ICF** — merged functions need COMDAT metadata so the linker can deduplicate
  3. **Ogg Vorbis split gaps** — 5 symbols missing from split config
  4. **Jump table symbols** — 3 remaining local jump table symbols need globalization

**Relevant docs:**
- `docs/plans/BUILD_ROADMAP.md` — Phase 1 table
- `docs/sessions/2026-02-11-x360-linking-pipeline.md` — full link error analysis

### Prompt: Investigate dtk fixes needed

```
I'm working on the DC3 decompilation project. We have link errors caused by limitations in dtk (the decomp toolkit that splits the original binary into .obj files).

## The Issues

### 1. lbl_* symbols (14 errors)
dtk creates local labels for data references but doesn't globalize them. When decomp code references data in a split object, the linker can't find the local label.

Example errors:
- LNK2001: unresolved external symbol "lbl_82002100"

### 2. ICF-merged functions (189 errors, 3 unique symbols)
The original linker used Identical COMDAT Folding to merge functions with identical machine code. dtk's splitter creates separate .obj files for each, but they reference the merged address. Symbols like:
- DataArray::Node::operator=
- operator delete
- MemOrPoolFreeSTL

### 3. Ogg Vorbis (13 errors)
Missing split objects for floor0_unpack, OggFree, and 3 other Ogg/Vorbis symbols.

## Steps
1. Read docs/sessions/2026-02-11-x360-linking-pipeline.md for full context
2. Look at the link error output to identify the exact symbols
3. For lbl_* symbols: investigate how dtk creates these and what change would globalize them
4. For ICF: investigate COMDAT support in dtk's COFF writer
5. Document findings and propose fixes (even if we can't make the changes ourselves)
```

---

## Quick Reference: Build Pipeline

```bash
# Full build + link + XEX (pdata fix and ICF glue are automated)
ninja build/373307D9/default.exe && python3 scripts/build/build_xex.py

# Build single unit
ninja build/373307D9/src/system/hamobj/RhythmBattle.obj

# Check progress
# mcp__orchestrator__get_progress

# Generate report
ninja build/373307D9/report.json
```

## Quick Reference: Orchestrator MCP Tools

```
query_functions(unit_pattern, min_percent, max_percent)  — find targets
run_recon(symbol)                                         — full function analysis
run_objdiff(symbol, project_dir)                          — build + diff
run_diff_inspect(symbol, mode, project_dir)               — deep analysis
run_analyze_function(symbol, project_dir)                 — objdiff + struct resolution
lookup_rb3(symbol)                                        — RB3 reference lookup
report_result(symbol, status, percent, notes)             — mark function done
batch_check(unit_pattern)                                 — sweep unit for matches
get_progress()                                            — overall stats
```

---

## Priority Recommendation

1. **Boot test first** (Work Area 3) — even a crash tells us what matters. Requires Windows/Xenia.
2. **Bulk decomp** (Work Area 1) — the 620 remaining functions are the steady grind
3. **Link errors** (Work Area 2) — reduce noise, some may affect boot
4. **XEXP tooling** (Work Area 4) — only needed if Strategy A boot test fails and we need isolation
5. **dtk fixes** (Work Area 5) — external dependency, pursue when ready to contribute upstream
