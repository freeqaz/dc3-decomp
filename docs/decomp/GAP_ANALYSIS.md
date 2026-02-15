# Gap Analysis - Strategic Investment Guide

This document identifies where to invest effort for maximum decompilation progress. Updated periodically with fresh data from the build report.

**Last Updated:** 2026-02-14

---

## Current Status Snapshot

### Overall

| Metric | Value |
|--------|-------|
| Total Functions | 47,124 |
| Matched Functions | 23,950 (50.82%) |
| **Fuzzy Match** | **43.62%** |
| Matched Code | 35.76% (3,957KB / 11,063KB) |
| Complete Code (100%) | 2.07% (229KB) |
| Complete Units | 164 / 2,223 |

### Database Triage Status

| Category | Count |
|----------|-------|
| Complete (100%) | 23,956 |
| AT_LIMIT (classified stuck) | 1,821 |
| Partial (1-99%) | ~1,993 |
| Unimplemented (0%) | 21,886 |

**Quick check:** Run `./tools/progress.sh` to get current stats.

---

## Remaining Work Breakdown

### By Subsystem (Top Priority Areas)

| Unmatched | Files | Subsystem | Notes |
|-----------|-------|-----------|-------|
| **428KB** | 81 | system/rndobj | Rendering - has RB3 reference |
| **374KB** | 75 | system/hamobj | HAM gameplay objects - DC3 specific |
| **295KB** | 106 | lazer/meta_ham | Menus/UI - DC3 specific |
| **265KB** | 59 | system/char | Character system - has RB3 reference |
| 143KB | 47 | system/net | Networking/curl - low priority |
| 136KB | 36 | system/ui | UI framework - has RB3 reference |
| 128KB | 22 | system/world | World/lighting - has RB3 reference |
| 108KB | 36 | system/os | Platform layer - mostly Xbox specific |
| 97KB | 30 | system/flow | Flow system |
| 93KB | 51 | system/utl | Utilities |

### Biggest Individual Files to Finish

| Unmatched | Current | File | RB3 Ref? |
|-----------|---------|------|----------|
| 39.5KB | 22.5% | rndobj/Utl | Partial |
| 33.4KB | 34% | rndobj/Text | Yes |
| 29.9KB | 12.8% | hamobj/RhythmBattle | No |
| 28.9KB | 63.6% | hamobj/HamDirector | No |
| 26.5KB | 6.2% | lazer/meta_ham/MetagameRank | No |
| 25.6KB | 37.9% | hamobj/HamNavList | No |
| 25.4KB | 48.4% | hamobj/MoveDir | No |
| 25.1KB | 4.1% | os/PlatformMgr_Xbox | No |
| 23.8KB | 47.8% | rndobj/Mesh | Yes |
| 23.7KB | 64.1% | world/LightPreset | Yes |

### Code Status

- **Remaining in partially-matched files:** 2,628KB
- **Completely unstarted files:** 122KB (Bink, FFT, synth)
- **Total remaining:** ~2,750KB

---

## What NOT to Work On

These categories are external code and will remain at 0%:

| Category | Why Unfixable |
|----------|---------------|
| `xdk/*` | Xbox SDK libraries (binary-only) |
| `d3dx9/*` | DirectX shader compiler |
| `xgraphics/*` | Xbox graphics libraries |
| `nuispeech/*` | Kinect speech recognition |
| `ST/*` | Skeletal tracking (Kinect) |
| `Curl/*` | HTTP library (external) |

These account for significant 0% code but are not decompilable.

---

## High-Impact Investment Areas

### Tier 1: Largest Gaps in Game Code (lazer/)

| Unit | Match % | Key Unmatched Functions | Impact |
|------|---------|------------------------|--------|
| **MetagameRank** | 19.9% | `UpdateScore` (8.6KB) | Scoring system |
| **BustAMovePanel** | 33.0% | `OnBeat` (12KB), `Poll` (3KB) | Minigame |
| **HamStorePanel** | 42.4% | `CreateCartUIs`, `Poll` | Store UI |
| **SaveLoadManager** | 58.0% | `SetState` (5.2KB), `Poll` (3.1KB) | Save system |
| **Challenges** | 63.5% | `SetupInGameChallenges` | Challenge system |

**Recommendation:** These are DC3-specific with no RB3 reference. Require reverse engineering from scratch.

### Tier 2: Largest Gaps in System Code

| Unit | Match % | Key Unmatched Functions | RB3 Ref? |
|------|---------|------------------------|----------|
| **Utl** | 27.6% | Various utility functions | Partial |
| **Text** | 43.3% | Text rendering | Yes |
| **HamNavList** | 55.2% | Navigation UI | No |
| **MoveDir** | 59.0% | `UpdateOverlay` (5KB) | No |
| **Part** | 65.9% | Particle system | Yes |
| **LightPreset** | 66.4% | Lighting | Yes |
| **Mesh** | 67.2% | Mesh rendering | Yes |
| **Character** | 68.8% | Character system | Yes |

**Recommendation:** System code with RB3 reference (Text, Part, LightPreset, Mesh, Character) offers better ROI than DC3-specific code.

### Tier 3: Near-Match Quick Wins (90%+, large size)

These are close to done and may yield quick completions:

| Function | Match % | Size | Unit |
|----------|---------|------|------|
| `RndParticleSys::SyncProperty` | 99.7% | 7.3KB | Part |
| `Spotlight::SyncProperty` | 99.7% | 4.8KB | Lit |
| `HamNavList::Handle` | 99.0% | 4.1KB | HamNavList |
| `RndMesh::Handle` | 98.3% | 3.6KB | Mesh |
| `UIList::Handle` | 97.5% | 3.7KB | UIList |
| `ShaderOptions::GenerateMacros` | 97.2% | 3.6KB | Shader |

**Caveat:** Many 97%+ functions are at linker limit. Check with `objdiff-cli diff --include-instructions` before investing time. See [OBJDIFF_LEARNINGS.md](../OBJDIFF_LEARNINGS.md) for diagnosis patterns.

---

## Category Breakdown

### System Code (`system/`) - Engine with RB3 Reference

| Subsystem | Gap | Priority | Notes |
|-----------|-----|----------|-------|
| system/rndobj | 428KB | **HIGH** | Rendering, largest gap, RB3 reference |
| system/char | 265KB | **HIGH** | Character system, RB3 reference |
| system/ui | 136KB | Medium | UI framework, RB3 reference |
| system/world | 128KB | Medium | World/lighting, RB3 reference |
| system/os | 108KB | Low | Platform layer, Xbox-specific |
| system/flow | 97KB | Medium | Flow system |
| system/utl | 93KB | Medium | Utility code |
| system/synth | 86KB | Low | Audio synthesis |
| system/gesture | 81KB | Low | Kinect gestures |
| system/obj | 71KB | Medium | Core object model |
| system/math | 32KB | **HIGH** | Small gap, quick wins, RB3 reference |

### Game Code (`lazer/`) - DC3 Specific

| Subsystem | Gap | Priority | Notes |
|-----------|-----|----------|-------|
| lazer/meta_ham | 295KB | Medium | Menus/UI, no RB3 ref |
| lazer/game | 70KB | Medium | Core gameplay |
| lazer/net_ham | 16KB | Low | Networking |

### DC3-Specific Engine (`system/hamobj`)

| Subsystem | Gap | Priority | Notes |
|-----------|-----|----------|-------|
| system/hamobj | 374KB | Medium | HAM gameplay objects, no RB3 ref |

---

## Effort vs Payoff Matrix

```
                    HIGH PAYOFF
                         │
    ┌────────────────────┼────────────────────┐
    │                    │                    │
    │  system/math       │  RndParticleSys    │
    │  system/rndobj     │  Spotlight         │
    │  (RB3 reference)   │  (99%+ functions)  │
    │                    │                    │
LOW ├────────────────────┼────────────────────┤ HIGH
EFFORT                   │                    EFFORT
    │                    │                    │
    │  Tiny functions    │  BustAMovePanel    │
    │  (<100 bytes)      │  MetagameRank      │
    │  See LOW_HANGING   │  (DC3-specific,    │
    │  _FRUIT.md         │   no reference)    │
    │                    │                    │
    └────────────────────┼────────────────────┘
                         │
                    LOW PAYOFF
```

---

## Recommended Work Order

### Immediate (high ROI with RB3 reference)
1. **system/rndobj** (428KB gap) - Largest gap, has RB3 reference
   - Focus: Text.cpp (34%), Mesh.cpp (48%), Part.cpp (66%)
   - Many near-matches in SyncProperty functions
2. **system/char** (265KB gap) - Has RB3 reference
   - Focus: Character.cpp (69%), CharDriver.cpp (31%)
3. **system/math** - Small functions, quick wins, RB3 reference
   - Trig.cpp (90%), Decibels.cpp (50%), Key.cpp (40%)

### Medium-term (DC3-specific but high impact)
1. **system/hamobj** (374KB gap) - Core gameplay, no RB3 ref
   - Focus: RhythmBattle (13%), HamDirector (64%), MoveDir (48%)
2. **lazer/meta_ham** (295KB gap) - Menus/UI, DC3 specific
   - Focus: MetagameRank (6%), SaveLoadManager (22%)

### Lower priority
1. **system/net** (143KB) - Curl library code, tedious
2. **system/os** (108KB) - Xbox-specific platform code
3. **Unstarted files** (122KB) - Bink, FFT, synth internals

### Avoid for now
- XDK/SDK code - Not decompilable
- Third-party libs - External code

---

## How to Use This Document

1. **Picking work:** Start from "Recommended Work Order" or "High-Impact Investment Areas"
2. **Before deep-diving:** Run objdiff diagnosis to check if function is at linker limit
3. **Function-level targets:** See [LOW_HANGING_FRUIT.md](LOW_HANGING_FRUIT.md) for specific easy functions
4. **Methodology:** See [SUBAGENT_STRATEGY.md](SUBAGENT_STRATEGY.md) for parallel agent approach
5. **After completing work:** Update this doc's snapshot stats and move items to completed

---

## Related Documents

- [LOW_HANGING_FRUIT.md](LOW_HANGING_FRUIT.md) - Function-level tactical targets
- [SUBAGENT_STRATEGY.md](SUBAGENT_STRATEGY.md) - How to parallelize work
- [RB3_REFERENCE.md](RB3_REFERENCE.md) - Shared code with Rock Band 3
- [TECHNICAL_NOTES.md](TECHNICAL_NOTES.md) - Compiler patterns
- [../OBJDIFF_LEARNINGS.md](../OBJDIFF_LEARNINGS.md) - Fixability diagnosis patterns
