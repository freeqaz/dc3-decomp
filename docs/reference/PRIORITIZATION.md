# Decomp Prioritization Guide

How to prioritize work for getting DC3 to a functional, compilable state.

## Goal: Workable, Not Perfect

The goal is **functional code that mirrors original intent**, not byte-perfect matching. This means:

- Register swaps are acceptable (same logic, different register allocation)
- Instruction reordering that doesn't affect behavior is fine
- AT_LIMIT functions at 90%+ are effectively "done"
- Focus effort where it impacts functionality, not match percentage

## Current Status Summary

| Metric | Value |
|--------|-------|
| Total Functions | 46,897 |
| Matched Functions | 50.0% |
| Matched Code Bytes | 34.7% |
| Fuzzy Match | 42.6% |

The gap between function match (50%) and byte match (35%) indicates many functions are partially complete or have minor mismatches.

## Priority Tiers

### Tier 0: Ignore (XDK/Third-Party)

These are Xbox SDK and third-party libraries. Don't decompile - stub or link against original binaries.

| Subsystem | Size | Notes |
|-----------|------|-------|
| xdk/xgraphics | 1.28MB | Xbox graphics API |
| xdk/nuispeech | 1.26MB | Kinect speech recognition |
| xdk/d3dx9 | 768KB | DirectX 9 helpers |
| xdk/xaudio2 | 346KB | Xbox audio API |
| xdk/d3d9i | 323KB | DirectX 9 internals |
| xdk/nuiapi | 303KB | Kinect API |
| xdk/ST | 228KB | Unknown SDK component |
| xdk/LIBCMT | 162KB | C runtime |
| lib/binkxenon | 79KB | Bink video codec |

**Total: ~4.4MB** - Nearly 60% of unmatched code is third-party.

### Tier 1: Critical Path (Must Work)

Core systems that must function for the game to run:

| Subsystem | Match% | Unmatched | Why Critical |
|-----------|--------|-----------|--------------|
| system/synth_xbox | 26.2% | 104KB | Xbox audio - game is silent without this |
| system/os | 50.2% | 110KB | File I/O, memory, platform abstraction |
| system/obj | 68.9% | 65KB | Object system - everything inherits from this |
| App | 16.9% | 10KB | Application entry point |

### Tier 2: Gameplay Systems (Must Work for Playability)

| Subsystem | Match% | Unmatched | Why Important |
|-----------|--------|-----------|---------------|
| system/hamobj | 65.4% | 287KB | Dance moves, rhythm battles, game logic |
| system/char | 63.2% | 229KB | Character animation, bones, IK |
| system/gesture | 61.4% | 78KB | Kinect gesture recognition |
| lazer/game | 59.0% | 61KB | Game-specific logic |

### Tier 3: Rendering (Must Work for Visuals)

| Subsystem | Match% | Unmatched | Why Important |
|-----------|--------|-----------|---------------|
| system/rndobj | 58.5% | 400KB | Core rendering objects |
| system/rnddx9 | 45.4% | 47KB | DirectX 9 backend |
| system/world | 64.0% | 120KB | 3D world, cameras, lighting |

### Tier 4: UI/Meta (Important but Secondary)

| Subsystem | Match% | Unmatched | Notes |
|-----------|--------|-----------|-------|
| system/ui | 59.7% | 111KB | UI widgets, lists, labels |
| lazer/meta_ham | 69.2% | 268KB | Menus, song selection, profiles |
| system/flow | 61.7% | 87KB | State machine, animation flow |
| system/meta | 74.7% | 31KB | Store, save/load |

### Tier 5: Supporting Systems (Nice to Have)

| Subsystem | Match% | Unmatched | Notes |
|-----------|--------|-----------|-------|
| system/synth | 71.4% | 84KB | Audio synthesis (non-Xbox specific) |
| system/net | 69.6% | 71KB | Networking, HTTP |
| system/utl | 56.1% | 92KB | Utilities, string handling |
| system/math | 32.7% | 31KB | Math primitives |

## Function Selection Strategy

### Best Targets: LIKELY_FIXABLE at 40-80%

These have room for improvement and aren't blocked by compiler quirks:

```bash
# Find fixable functions in a subsystem
./bin/objdiff-cli near-match --unit "system/hamobj/*" --verdict LIKELY_FIXABLE
```

### AT_LIMIT Functions — the label is unreliable, and "functionally correct" is false

Functions marked AT_LIMIT are *supposed* to have hit compiler/linker barriers:
- Register allocation differences
- Instruction scheduling
- COMDAT folding (merged symbols)
- Bool normalization patterns

**Corrected 2026-08-04.** This section used to end "these are functionally correct —
don't spend time on them." Both halves are wrong for the register-swap sub-bucket, and
this was measured, not argued:

- **The verdict is wrong ≥30% of the time.** A blind stratified audit of
  `verdict='AT_LIMIT' AND has_register_swap=1 AND is_stub=0 AND excluded=0` (836
  functions; selection rule fixed before any diff was inspected) scored **3/10**, all
  three byte-exact fixes.
- **"Functionally correct" does not follow from "at limit".** One of those three,
  `HDCache::WriteDone`, was a live bug: `1 << mWriteBlock` where the target has
  `1 << (mWriteBlock % 32)`, setting the wrong bit in `mBlockState` for any block index
  ≥ 32. A later seven-lane sweep of the same bucket found **11 more** behavioural
  defects, including a comprehensively broken OSC parser and four permanent locale
  tables allocated on the temp heap.
- **Do not band on the DB's `current_percent`** (stale by up to 12 points) or route on
  `tier=` / `share=` (no discriminative power; `tier` is mildly anti-correlated with
  outcome). Re-measure with `mcp__orchestrator__run_objdiff` passing `project_dir`.

So AT_LIMIT is a **deprioritization signal, not an exclusion**. If you do sweep it,
scope to the statement-level half
([Triage Split](../decomp/patterns/fixable-liveness.md#triage-split-statement-level-vs-within-one-expression))
and budget **~1 win per 3 functions**. Detail:
[patterns/INDEX.md: AT_LIMIT Breakdown](../decomp/patterns/INDEX.md#at_limit-breakdown)
and [sessions/2026-08-04-regswap-atlimit-sweep.md](../sessions/2026-08-04-regswap-atlimit-sweep.md).

### Quick Wins: 90%+ Functions

Functions at 90%+ often just need minor tweaks:
- Unsigned comparison patterns (`> 0` vs `!= 0`)
- Loop structure adjustments
- Member access order

```bash
# Find near-complete functions
./bin/objdiff-cli near-match --min-percent 90 --max-percent 99
```

## Subsystem-Specific Notes

### system/synth_xbox (26.2% - Highest Priority)

The lowest match% of any core system. Focus areas:
- `Synth360::Init` - Audio system initialization
- Stream/sample playback functions
- XMA decoding wrappers

### system/hamobj (65.4% - Game Logic)

Core gameplay. Priority functions:
- `RhythmBattle` - The battle system
- `HamMove`, `MoveDir` - Dance move handling
- `HamDirector` - Game flow control
- `DanceRemixer` - Routine generation

### system/char (63.2% - Characters)

Animation system. Priority functions:
- `Character` class methods
- `CharClip`, `CharClipSet` - Animation clips
- `CharBones`, `CharBonesSamples` - Skeleton
- IK solvers (`CharIKHand`, etc.)

### system/rndobj (58.5% - Rendering)

Rendering primitives. Priority functions:
- `RndMesh` - 3D meshes
- `RndMat` - Materials
- `RndTex` - Textures
- `RndDir` - Scene graph

## Workflow Recommendations

### 1. Start with Dependencies

Work bottom-up through the dependency graph:
```
system/obj -> system/utl -> system/math -> system/rndobj -> ...
```

A broken base class breaks all derived classes.

### 2. Focus on Constructors and Init

Object initialization is critical:
- Constructors set up vtables
- Init functions establish state
- Missing initialization causes crashes

### 3. Use RB3 Reference

Rock Band 3 shares the Milo engine. Check RB3 decomp for reference implementations:

```bash
# Look up RB3 equivalent
./bin/objdiff-cli lookup-rb3 "ClassName::MethodName"
```

### 4. Batch Similar Functions

Group work by pattern:
- All `Load` methods in a class
- All `Handle` methods (message handlers)
- All `Poll` methods (update loops)

### 5. Track Progress by Subsystem

Monitor subsystem health rather than individual functions:

```bash
# Check subsystem progress
ninja build/373307D9/report.json
# Then analyze by unit pattern
```

## When to Stop

A function is "done enough" when:

1. **Logic matches** - Same branches, same operations
2. **Data access matches** - Correct struct offsets, correct member access
3. **Side effects match** - Same calls in same order
4. **Only cosmetic differences remain** - Register choices, instruction scheduling

Don't chase 100% on functions where the compiler simply made different choices.

## Priority Checklist

When choosing what to work on:

- [ ] Is this in a critical-path subsystem? (Tier 1-2)
- [ ] Is the function NOT marked AT_LIMIT?
- [ ] Does fixing this unblock other work?
- [ ] Is there an RB3 reference available?
- [ ] Is this a constructor/init that affects object setup?

If 3+ boxes are checked, it's a good target.
