# DC3 Decomp — Project Status

Last updated: 2026-02-26

## Overview

Dance Central 3 (Xbox 360) decompilation targeting a debug build from an Xbox 360 dev unit. The goal is to produce matching assembly from C++ source code. The target binary uses the Milo engine shared with Rock Band 3, providing a reference decomp for shared subsystems.

## Progress Summary

| Metric | Value |
|--------|-------|
| **Complete functions** | 29,890 / 48,349 (61.9%) |
| **Complete (non-SDK)** | 29,890 / 32,328 (**92.5%**) |
| **AT_LIMIT functions** | 1,652 (5.1% of non-SDK) |
| **Remaining workable** | 786 (2.4% of non-SDK) |
| **Excluded (SDK/lib)** | 16,021 |
| **Done (COMPLETE + AT_LIMIT)** | 31,542 / 32,328 (**97.6%**) |

### Category Breakdown

| Category | Units | Code Size | Fuzzy Match |
|----------|-------|-----------|-------------|
| Engine (Milo) | 809 | 5.2 MB | 78.6% |
| Game (DC3-specific) | 160 | 1.1 MB | 86.8% |
| SDK/XDK | 1,224 | 10 KB | 9.5% |
| Libraries | 18 | — | — |

### Unit Match Distribution

| Range | Count |
|-------|-------|
| 100% | 413 |
| 95–99% | 238 |
| 90–94% | 97 |
| 80–89% | 100 |
| 50–79% | 175 |
| 0–49% | 93 |

## Subsystem Status

Decomp functions broken down by engine subsystem:

| Subsystem | COMPLETE | AT_LIMIT | AT_LIMIT Avg | Notes |
|-----------|----------|----------|--------------|-------|
| lazer (game) | 4,057 | 697 | 84.0% | DC3-specific code, highest avg match |
| system/rndobj | 3,552 | 898 | 61.3% | Rendering: meshes, materials, animation |
| system/hamobj | 3,391 | 742 | 52.2% | Harmonix game objects |
| system/char | 2,883 | 485 | 62.7% | Character system, IK, bones |
| system/synth | 1,409 | 296 | 56.5% | Audio synthesis |
| system/world | 1,365 | 268 | 51.3% | World/scene management |
| system/ui | 1,233 | 323 | 66.2% | UI framework |
| system/flow | 1,076 | 222 | 54.2% | Flow graph scripting |
| system/utl | 946 | 308 | 69.3% | Utilities, debug, memory |
| system/os | 920 | 394 | 74.2% | Platform layer, Xbox integration |
| system/obj | 895 | 152 | 79.7% | Object system, DataArray |
| system/gesture | 833 | 221 | 54.1% | Kinect gesture recognition |
| system/net | 783 | 84 | 62.9% | Networking |
| system/meta | 622 | 104 | 70.0% | Meta-game systems |

## Behavioral Analysis (Unicorn Runner)

The unicorn emulation runner tests behavioral equivalence by executing decomp vs. original functions with randomized inputs on a PowerPC emulator.

| Verdict | Count | Percentage |
|---------|-------|------------|
| EQUIVALENT | 24,280 | 93.1% |
| DIVERGENT | 1,541 | 6.9% |

### Divergence Classes

| Class | Count | Description | Fixable? |
|-------|-------|-------------|----------|
| build_env | 623 | `__FILE__` paths, merged symbols | No |
| call_count | 476 | ICF merged call targets | No |
| error | 224 | `__FILE__` differences in asserts | No |
| stack_layout | 123 | Compiler stack frame choices | No |
| call_arg | 34 | Argument passing differences | Sometimes |
| object_memory | 19 | Wrong field values in ctors | **Yes** |
| regalloc | 18 | Register assignment quirks | No |
| return_value | 17 | Logic bugs in conditionals | **Yes** |
| fpr_precision | 7 | Float precision differences | No |

Primary work targets: **object_memory** (wrong field init) and **return_value** (logic bugs) classes — these indicate real behavioral differences fixable from source.

## AT_LIMIT Analysis

1,652 functions are marked AT_LIMIT — meaning they've been investigated and the remaining mismatches are unfixable compiler artifacts.

| Match Range | Count | Avg Match | Root Causes |
|-------------|-------|-----------|-------------|
| 95–100% | 712 | 98.0% | Register swaps, relocation noise, merged symbols |
| 90–95% | 365 | 92.9% | Boolean mask patterns, float constant pooling |
| 80–90% | 438 | 85.4% | Mixed unfixable patterns |
| 50–80% | 332 | 69.8% | Significant structural differences |
| 1–50% | 92 | 27.4% | Complex structural issues |
| 0% / NULL | 629 | — | Undiffable (ambiguous symbols, missing objects) |

### Common Unfixable Patterns (2026-02-26 Deep Analysis)

Extensive objdiff analysis of 20+ functions across multiple units identified these unfixable patterns:

| Pattern | Description | Occurrences | Fixable? |
|---------|-------------|-------------|----------|
| **LINKER_MERGED (ICF)** | Identical COMDAT Folding - linker merged identical code | Most remaining functions | **NO** |
| **ANONYMOUS_NAMESPACE_HASH** | `@?A0xad41c9dd@@` namespace hash differs | MemHeap::Print (3 calls) | **NO** |
| **STRING_LITERAL_HASH** | `??_C@_0L@HASH@` content hash differs | Many functions | **NO** |
| **__FILE__ path** | Different build paths in asserts | CharBones::Blend | **NO** |
| **BOOL_MASK** | Compiler bool materialization (`rlwinm` vs `subfic+srawi`) | LoadCrew | **NO** |
| **Massive register swaps** | 20-60+ swap instructions across many pairs | Most AT_LIMIT functions | Rarely |
| **FMA mismatches** | Mixed `fmadd`/`fmsub` directions | Some float-heavy code | **NO** |

### Key Findings from Unit Analysis

**HamDirector (5 functions analyzed):**
- PoseIconMan (96.6%): 2 LINKER_MERGED → AT_LIMIT
- GetPracticeFrames (94.9%): 35 regswaps → AT_LIMIT
- FindNextDircut (93.1%): bne↔beq inversion - **TRY FIX**
- ReactToCollision (86.1%): 4 LINKER_MERGED + 63 regswaps → AT_LIMIT
- LoadCrew (84.6%): BOOL_MASK + 52 regswaps → AT_LIMIT

**CharBones (5 functions analyzed):**
- ScaleDown (97.9%): 76 regswaps - maybe fixable
- AddBoneInternal (97.5%): r7↔r8 swap - maybe fixable
- TypeOf (91.9%): String hash + control flow - partially fixable
- Blend (91.6%): LINKER_MERGED + __FILE__ → AT_LIMIT
- ScaleAdd (73.2%): Prologue mismatch, 333 regswaps - hard

**StorePanel (5 functions analyzed):**
- StorePanel ctor (95.2%): 3 regswaps - **TRY FIX**
- Handle (93.2%): 3 LINKER_MERGED + 1 control flow - partially fixable
- OnMsg(SigninChanged) (92.9%): 2 LINKER_MERGED + 11 regswaps - maybe
- CheckOut (92.0%): 4 LINKER_MERGED + 16 regswaps → AT_LIMIT
- OnMsg(SingleItemEnum) (89.9%): 3 LINKER_MERGED + control flow - maybe

**UIList (3 functions analyzed):**
- DrawShowing (84.0%): 3 LINKER_MERGED + 20 regswaps → AT_LIMIT
- PostLoad (74.9%): 1 LINKER_MERGED + 55 regswaps → AT_LIMIT
- Copy (73.1%): 6 LINKER_MERGED + 22 regswaps → AT_LIMIT

### Best Fix Candidates

Functions with high match, no LINKER_MERGED, and fixable patterns:
1. **StorePanel ctor** (95.2%) - Only 3 regswaps, 1 offset swap
2. **FindNextDircut** (93.1%) - bne↔beq inversion is fixable
3. **ScaleDown** (97.9%) - High match but 76 regswaps
4. **AddBoneInternal** (97.5%) - Small, r7↔r8 swap

### Regswap Patcher

A post-build binary patching tool (`scripts/obj_regswap_patcher.py`) fixes register assignment mismatches in decomp `.obj` files. Run after `ninja`:

```bash
python3 scripts/obj_regswap_patcher.py --batch --apply
```

Typical results: ~709 functions processed, ~6 promoted to 100%, ~670 improved, ~15 reverted (safety). Patches are lost on rebuild.

## Tooling

### Build Pipeline

```
dtk xex split → fix_pdata.py → configure.py → ninja → link /FORCE
```

- **Compiler**: Xbox 360 MSVC (`cl.exe` v16.00.11886.00) via Wine
- **Linker**: Xbox 360 `link.exe` via Wine
- **Build system**: Ninja
- **Comparison**: objdiff (fuzzy instruction matching)

### Key Tools

| Tool | Purpose |
|------|---------|
| `objdiff` | Instruction-level diff between decomp and original `.obj` files |
| `decomp.db` | SQLite database tracking all 47K+ functions, match %, verdicts |
| Orchestrator MCP | `run_objdiff`, `run_diff_inspect`, `query_functions`, etc. |
| Unicorn runner | Behavioral equivalence testing via PowerPC emulation |
| Ghidra MCP | Decompilation, struct layout, cross-reference analysis |
| C++ Permuter | Automated source permutation for register allocation fixes |
| RB3 reference | Shared Milo engine code from Rock Band 3 decomp |
| Regswap patcher | Post-build binary patching for register assignment fixes |

### Database Schema

`decomp.db` tracks every function:
- `symbol`: Mangled C++ name
- `demangled`: Human-readable name
- `unit`: Compilation unit (e.g., `default/system/char/Character`)
- `current_percent`: Latest objdiff fuzzy match percentage
- `verdict`: `COMPLETE` (100%), `AT_LIMIT` (investigated, unfixable), or `NULL` (untracked)
- `unicorn_verdict`: `EQUIVALENT`, `DIVERGENT`, `SKIPPED`, or `ERROR`
- `unicorn_class`: Divergence category (logic, build_env, regalloc, etc.)

## Linking

The decomp produces a linkable Xbox 360 PE executable.

### Current Link Status

The link uses `/FORCE:MULTIPLE` + `/FORCE:UNRESOLVED` to produce a PE despite errors. The resulting XEX boots in Xenia (headless and graphical modes).

| Error Category | Count | Fixable? |
|----------------|-------|----------|
| String literal COMDATs | 749 | **Yes** — `??_C@` hash is JamCRC over string content only (not build context). Under wibo, CRC always returns 0 (missing `RtlComputeCrc32`). Fix: implement CRC32 in wibo + match build paths for `__FILE__` strings. See `docs/plans/CLEAN_LINK_PROJECT.md`. |
| lbl_ cross-unit labels | 219 | No — dtk split artifact |
| __unwind$/__catch$ EH | 46 | No — exception handling metadata |
| merged_ ICF symbols | 35 | Partially — via `link_glue.cpp` stubs |
| ??__E dynamic init | 26 | No — CRT initialization ordering |
| curl symbols | 24 | No — library not fully linked |
| REL14 fixup overflow | 18 | No — branch distance limitation |
| Vorbis floor0 | 7 | No — library symbols |
| LNK4006 warnings | 408 | Harmless — duplicate symbol selection |

Most link errors stem from mixing decomp `.obj` files (compiled from source) with split `.obj` files (extracted from original binary by dtk). Both use the same compiler (MSVC 16.00.11886). The `??_C@` string literal mangled names encode a JamCRC hash over the **string content bytes only**. Under wibo (Win32-on-Linux shim), the CRC function (`RtlComputeCrc32`) is unimplemented, causing all decomp string hashes to be 0 — while original objects have correct hashes. This mismatch means cross-references between decomp and split objects fail for string symbols. Fixing wibo's CRC32 will resolve all string mismatches except `__FILE__` path strings (which have different content). See `docs/plans/CLEAN_LINK_PROJECT.md`.

### XEX Boots in Xenia

The decomp XEX (both retail and debug builds) boots in Xenia headless mode:
- All 334 variable imports + 323 thunk imports resolved
- 60 guest memory patches (NUI/Kinect + SmartGlass stubs)
- Shows loading animation, reaches main game loop
- Frame captures show rendered content (warm tones, geometric shapes)

## Largest Incomplete Units

Units with the most remaining code to match:

| Match | Code Size | Unit |
|-------|-----------|------|
| 81.4% | 81 KB | system/hamobj/HamDirector |
| 75.0% | 68 KB | system/world/LightPreset |
| 77.3% | 53 KB | system/char/Character |
| 30.5% | 52 KB | system/rndobj/Utl |
| 50.7% | 52 KB | system/rndobj/Text |
| 68.4% | 50 KB | system/hamobj/MoveDir |
| 83.3% | 47 KB | lazer/game/PartyModeMgr |
| 69.8% | 47 KB | system/rndobj/Mesh |
| 82.2% | 47 KB | system/world/CameraShot |
| 85.7% | 44 KB | system/rndobj/Rnd |
| 96.0% | 42 KB | lazer/meta_ham/MetaPerformer |
| 66.3% | 42 KB | system/hamobj/HamNavList |
| 88.9% | 42 KB | system/rndobj/PropAnim |
| 78.9% | 41 KB | system/rndobj/Part |
| 88.5% | 39 KB | system/rndobj/EventTrigger |

## What's Left

### Achievable Improvements

1. **Implement remaining stubs**: ~50 empty function bodies that could be filled in from Ghidra decompilation and RB3 references
2. **Fix unicorn-flagged logic bugs**: 17 return_value + 19 object_memory divergences have real behavioral bugs fixable from source
3. **Promote effectively-complete units**: ~108 units where only merged/boilerplate symbols remain — effectively 100% from a decomp perspective

### Known Ceiling

- **AT_LIMIT average: 64.3%** — most remaining mismatches are unfixable compiler artifacts
- **Regswap patching** adds ~0.3% to overall match (post-build only, lost on rebuild)
- **No LTCG** in target binary (debug build) means most functions should be matchable in isolation, but register allocation and COMDAT folding still cause irreducible differences
- The theoretical ceiling with current source is estimated around **80–82% weighted fuzzy match** for engine code

## File Structure

```
src/              # Decompiled C++ source (mirrors original structure)
  system/         #   Milo engine subsystems
  lazer/          #   DC3 game-specific code
  xdk/            #   Xbox SDK headers
build/            # Build outputs
  373307D9/       #   Build variant (title ID)
    report.json   #   Progress report (ninja target)
config/           # Build configuration
  373307D9/
    objects.json  #   Per-unit matching status
orig/             # Original split objects (from dtk)
tools/            # Build tools, scripts
scripts/          # Analysis and utility scripts
  scratch/        #   Temporary/experimental scripts
docs/             # Documentation (this directory)
decomp.db         # Function tracking database
```

## How to Build

```bash
python3 configure.py        # Generate build files
ninja                        # Build all objects
ninja build/373307D9/report.json  # Generate progress report
ninja link                   # Link into PE (has errors, /FORCE)
```
