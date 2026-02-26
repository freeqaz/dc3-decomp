# DC3 Decomp Progress Planning Document

**Generated**: 2026-02-23
**Updated**: 2026-02-24 (Session 3)
**Current Overall Progress**: 92.4% COMPLETE, 4.7% AT_LIMIT
**Orchestrator Status**: 97.1% (COMPLETE + AT_LIMIT covers tracked functions)

## Session 3 Summary (2026-02-24)

### Completed Fixes

| Function | Before | After | Fix Applied |
|----------|--------|-------|-------------|
| `MoveAsyncDetector::EnableDetector` | 31.9% | **96.6%** | Control flow with goto, mActive compare with 1, int stores for floats |
| `UILabel::OnSetInt` | 68.96% | **69.88%** | Comparison flip via permuter |

### Key Learnings

1. **goto for control flow**: Using `goto` can generate `beq` instead of `bne` when needed
2. **Integer stores for zero floats**: `*(int *)&floatVar = 0` generates `stw` instead of `stfs`
3. **Comparison values matter**: `if (active == 1)` generates different code than `if (!active)`

## Session 2 Summary (2026-02-23)

### Completed Fixes

| Function | Before | After | Fix Applied |
|----------|--------|-------|-------------|
| `UIListState::PageScroll` | 99.7% | **99.9%** | Changed `if (amount <= 0)` to `if (amount > 0)` and swapped branches |
| `SynthSample360::NewInst` | 4.2% | **99.4%** | Implemented using `new SampleInst360` with POOL_OVERLOAD macro |
| `SynthSample360::LengthMs` | 0% | **97.0%** | Fixed calculation: use (numSamples * 1000) / sampleRate |
| `SynthSample360::IsXMA` | N/A | **100%** | Already correct |
| `SampleInst360::SampleInst360` | N/A | **100%** | Stub with POOL_OVERLOAD matches perfectly |
| `SynthSample360::Init` | N/A | **98.1%** | Near complete (symbol diffs only) |
| `SampleFree` | N/A | **97.1%** | Near complete (__FILE__ path diffs) |

### New Files Created
- `src/system/synth_xbox/SampleInst360.h` - Header for Xbox 360 sample instance
- `src/system/synth_xbox/SampleInst360.cpp` - Stub implementation (needs Voice class integration)
- Added `SampleData::HasData()` accessor method

## Completed Fixes (Previous Sessions)

| Function | Before | After | Fix Applied |
|----------|--------|-------|-------------|
| `UIListState::PageScroll` | 99.7% | **99.9%** | Changed `if (amount <= 0)` to `if (amount > 0)` and swapped branches |
| `SynthSample360::NewInst` | 4.2% | **85.2%** | Implemented function using PoolAlloc + placement new (needs SampleInst360 constructor work to improve further) |

## Attempted but Unsuccessful

| Function | Issue | Result |
|----------|-------|--------|
| `ClipPredict::Predict` | COMMUTATIVE_OP (fadds f0/f1) | No change - compiler quirk, operand swap doesn't help |
| `DataArray::Remove` | CONTROL_FLOW (index > cnt) | Made worse (95.0% → 94.9%), reverted |
| `Rand::Seed` | srwi vs srawi (signed/unsigned) | Made worse (82.2% → 75.2%), reverted - complex expression optimization |
| `PartyModeMgr::ShufflePlaylist` | beq/bne inversion | No change (83.6%), reverted |
| `ChallengeSortNode::Custom` | CONTROL_FLOW (bne/beq) + OFFSET_SWAP | Tried `if (valid) return;`, `else if` restructuring - all made it worse (98.8% → 85.6%), reverted |
| `HamCamShot::SetFrame` | CONSTANT_POOL (lis/lfs for 1.0f) | Unfixable - constant pool address difference, not source-controllable |

---

## Key Learnings from Fix Attempts

1. **CONTROL_FLOW fixes work when** the comparison is simple and isolated (like `amount <= 0` → `amount > 0`)
2. **COMMUTATIVE_OP swaps rarely help** - the compiler chooses register order based on allocation, not source order
3. **Complex expressions with shifts** are fragile - changing types can trigger different optimization paths
4. **Functions with LINKER_MERGED calls** have a hard ceiling - even fixing control flow may not improve overall %
5. **CONTROL_FLOW + OFFSET_SWAP combined** are usually unfixable - the offset swap indicates struct access pattern differences that affect register allocation
6. **CONSTANT_POOL differences** (lis/lfs for float constants) are unfixable linker artifacts
7. **Register allocation (r27-r31 shifts)** is hard to control - variable declaration order rarely helps
8. **Placement new + PoolAlloc** requires matching stack variable usage patterns for best match

## Future Work: SampleInst360 Constructor

The SampleInst360 constructor needs full implementation to improve SynthSample360::NewInst match %:
- Requires Voice class implementation (0x7C bytes)
- Calls merged functions for GetServiceId, GetPort, GetIPAddr, GetNumBytes, GetData
- Sets vtables for Object and PlayableSample interfaces
- Optional loop region setup based on bool parameter

---

## Executive Summary

This document tracks decompilation progress by unit/subsystem and identifies:
1. **Truly fixable functions** - Functions where source changes can improve match %
2. **AT_LIMIT patterns** - Unfixable patterns that block further progress
3. **Unimplemented functions** - Functions needing implementation from scratch

### Progress Overview

| Subsystem | Current | Key Blocker | Priority |
|-----------|---------|-------------|----------|
| system/jpeg | 99.22% | 1 complex function | Low |
| system/zlib | 87.44% | ICF merged + external asm | Low |
| system/midi | 87.77% | LINKER_MERGED, massive regswaps | Low |
| system/net | 82.26% | LINKER_MERGED, CONTROL_FLOW | Medium |
| system/meta | 77.88% | LINKER_MERGED dominates | Low |
| lazer/net_ham | 73.25% | Similar to meta | Low |
| system/synth | 72.80% | LINKER_MERGED, unimplemented | Medium |
| system/movie | 70.08% | TBD | Medium |
| system/obj | 70.55% | Mixed patterns | **High** |
| lazer/meta_ham | 73.28% | CONTROL_FLOW, regswaps | Medium |
| system/oggvorbis | 71.73% | TBD | Low |
| system/hamobj | 67.63% | LINKER_MERGED, regswaps | Medium |
| system/char | 68.79% | REGISTER_SWAP dominant | Medium |
| system/ui | 65.23% | LINKER_MERGED, CONTROL_FLOW | Medium |
| system/world | 64.64% | TBD | Low |
| system/flow | 64.43% | Mixed, some fixable | Medium |
| lazer/game | 62.02% | CONTROL_FLOW, unimplemented | **High** |
| system/utl | 57.99% | TBD | Low |
| system/rndobj | 59.95% | LINKER_MERGED dominates | Low |
| system/os | 55.39% | TBD | Low |
| system/gesture | 60.98% | TBD | Low |
| system/rnddx9 | 46.49% | TBD | Low |
| system/math | 36.60% | REGISTER_SWAP, unimplemented | Medium |
| system/synth_xbox | 28.86% | Unimplemented, complex | **High** |
| default/App | 19.31% | TBD | Low |
| system/moviebink | 6.47% | Mostly unimplemented | Low |

---

## Blocking Pattern Analysis

### Pattern Definitions

| Pattern | Fixability | Description |
|---------|------------|-------------|
| **LINKER_MERGED** | Unfixable | ICF (Identical COMDAT Folding) - linker merged identical functions to single address |
| **BOOL_MASK** | Unfixable | Compiler bool return optimization - bit masking differs |
| **REGISTER_SWAP** | Maybe | Compiler register allocation differences - may be fixable by variable reordering |
| **CONTROL_FLOW** | Fixable | Condition inversions (beq/bne, blt/bge) - fix with comparison changes |
| **OFFSET_SWAP** | Fixable | Struct member access order - reorder struct members or variables |
| **COMMUTATIVE_OP** | Noise | Operand order in commutative ops (a+b vs b+a) |

### Common Merged Symbols (Unfixable)

These symbols appear frequently and block progress in many functions:
- `merged_DataArrayNode` - DataArray::Node inline
- `merged_824D1870` - Common utility function
- `merged_SetVirtualObjConcrete` - Virtual object setup
- `merged_823314D8` - Common utility
- `merged_MemOrPoolFreeSTL` - Memory deallocation
- `merged_OperatorDelete` - operator delete

---

## Priority 1: High-Impact Quick Wins

These functions are close to 100% with **fixable patterns**:

### system/obj (High Priority Unit)

| Function | Match | Pattern | Fix |
|----------|-------|---------|-----|
| `DirLoader::CreateObjects` | 98.3% | REGISTER_SWAP (r25/r28) | Reorder variable declarations |
| `DataArray::Remove` | 95.0% | CONTROL_FLOW (blt/bgt) | Fix comparison operator |
| `DataArray::ExecuteScript` | 98.2% | REGISTER_SWAP (r29/r30) | Reorder variables |
| `DirLoader::SaveObjects` | 97.9% | REGISTER_SWAP + CONTROL_FLOW | Reorder + if/else inversion |
| `ObjectDir::ResetViewports` | 97.9% | REGISTER_SWAP (f12/f30) | Reorder float variables |

### system/ui

| Function | Match | Pattern | Fix |
|----------|-------|---------|-----|
| `UIListState::PageScroll` | ~~99.7%~~ → **99.9%** | ~~CONTROL_FLOW~~ | **FIXED**: Changed `if (amount <= 0)` to `if (amount > 0)` |
| `UIListState::BuildScroll` | 95.2% | CONTROL_FLOW (ble/bgt) | Fix comparison |
| `InlineHelp::SyncLabelsToConfig` | 96.2% | CONTROL_FLOW + REGISTER_SWAP | Reorder + comparison |

### system/flow

| Function | Match | Pattern | Fix |
|----------|-------|---------|-----|
| `FlowLabel::Load` | 99.0% | REGISTER_SWAP (r26/r27) | Reorder variables |
| `FlowPtrBase::LoadObject` | 95.3% | Minor CONTROL_FLOW | Check null handling |

### system/hamobj

| Function | Match | Pattern | Fix |
|----------|-------|---------|-----|
| `HamCamShot::SetFrame` | 99.9% | 2 instructions (lis/lfs) | Float constant loading |
| `HamMaster::Jump` | 98.8% | 1 instruction | Parameter passing |
| `DetectFrame::Reset` | 99.1% | REGISTER_SWAP (r30/r31) | Reorder variables |
| `MoveGraph::Load` | 99.1% | REGISTER_SWAP (r27/r29) | Reorder variables |

### lazer/meta_ham

| Function | Match | Pattern | Fix |
|----------|-------|---------|-----|
| `NavListShortcutNode::Insert` | 99.9% | 1 instruction (bl) | Symbol reference |
| `ChallengeSortNode::Custom` | 98.8% | CONTROL_FLOW (bne/beq) | Unsigned comparison fix |

---

## Priority 2: Medium Effort, Good Gains

### lazer/game

| Function | Match | Pattern | Fix |
|----------|-------|---------|-----|
| `PartyModeMgr::ShufflePlaylist` | 83.6% | CONTROL_FLOW (beq/bne) | Fix condition |
| `PseudoRandomPicker<int>::GetNext` | 89.6% | CONTROL_FLOW + REGISTER_SWAP | Reorder + comparison |
| `Game::LoadNewSongAudio` | 96.0% | REGISTER_SWAP (r29/r30) | Reorder variables |

### system/synth

| Function | Match | Pattern | Fix |
|----------|-------|---------|-----|
| `csqrt` (complex) | 93.6% | REGISTER_SWAP (f12/f13) | Reorder float variables |
| `Process` (DistortionEffect) | 94.1% | CONTROL_FLOW | Fix comparisons |
| `Load` (Sound) | 97.8% | CONTROL_FLOW | Fix comparisons |

### system/char

| Function | Match | Pattern | Fix |
|----------|-------|---------|-----|
| `ClipPredict::Predict` | 99.9% | COMMUTATIVE_OP (f0/f1) | Reorder expression |
| `Interp` (Vector3) | 99.4% | OFFSET_SWAP (0x0/0x4) | Vector3 member order |
| `CharCuff::Highlight` | 98.5% | REGISTER_SWAP (f29/f30) | Reorder float variables |

### system/math

| Function | Match | Pattern | Fix |
|----------|-------|---------|-----|
| `Rand::Seed` | 82.2% | REPLACE (srwi/srawi) | Fix signed/unsigned |
| `Intersect(Segment&, Sphere&)` | 76.7% | REGISTER_SWAP + CONTROL_FLOW | Reorder + comparison |
| `FastInterp` | 68.4% | INSERT/DELETE clusters | Control flow restructure |

---

## Priority 3: Unimplemented Functions (Need Implementation)

### system/synth_xbox (28.86% - Critical)

| Function | Current | Size | Notes |
|----------|---------|------|-------|
| `Init@Synth360` | 30.4% | Large | Missing most implementation |
| `NewInst@SynthSample360` | 4.2% | Medium | Nearly empty stub |
| `LengthMs@SynthSample360` | 0% | Small | Simple calculation |
| `~SynapseAPO` | 21.3% | Medium | Empty destructor |
| Various StaticClassName | 0% | Trivial | Return static Symbol |

### lazer/game (62.02%)

| Function | Notes |
|----------|-------|
| `PartyModeMgr::GetCrewColor` | Trivial accessor |
| `PseudoRandomPicker<Symbol>::Randomize` | Template function |
| `PartyModeMgr::ReadPartySongQueue` | Needs implementation |
| `PartyModeMgr::SetSongsFromPlaylist` | Needs implementation |
| Multiple Game handlers | Various handlers unimplemented |

### system/math (36.60%)

| Function | Notes |
|----------|-------|
| `Multiply(Transform&, Transform&, Transform&)` | mtx.cpp utility |
| `Det(Hmx::Matrix4&)` | mtx.cpp utility |
| `Invert(Hmx::Matrix4&, Hmx::Matrix4&)` | mtx.cpp utility |
| `Clip(Hmx::Polygon&, Hmx::Ray&, Hmx::Polygon&)` | Geo.cpp |
| Various BSP functions | Geo.cpp intersection tests |

---

## Truly Unfixable (Accept AT_LIMIT)

These functions are blocked by patterns that **cannot be fixed**:

### LINKER_MERGED Dominated Functions

| Unit | Function | Match | Merged Calls |
|------|----------|-------|--------------|
| system/obj | `DataArray::FindData(Symbol, Plane&)` | 99.4% | 4 to DataArrayNode |
| system/obj | `DataArray::FindData(Symbol, Color&)` | 99.3% | 4 to DataArrayNode |
| system/obj | `DataArray::~DataArray` | 98.9% | 2 merged calls |
| system/rndobj | `RndMesh::Load` | 94.9% | 34 to 7 merged functions |
| system/synth | ByteGrinder opXX functions | 97-98% | merged_DataArrayNode |
| system/meta | `MetaMusicManager::ConfigureMetaMusicSceneData` | 95.7% | 8 to 5 merged |
| system/meta | `MoviePanel::Poll` | 98.0% | 5 to DataArrayNode |

### BOOL_MASK Pattern (Compiler Optimization)

| Unit | Function | Match |
|------|----------|-------|
| system/meta | `HAQManager::Handle` | 93.3% |
| system/meta | `StorePanel::HandleNetCacheMgrFailure` | 61.4% |
| system/synth | `op1` (ByteGrinder) | 79.4% |
| system/synth | `Poll` (BinkReader) | 14.8% |
| lazer/game | `BustAMovePanel::Poll` | 85.9% |

### Massive REGISTER_SWAP (Compiler Artifact)

| Unit | Function | Match | Swap Count |
|------|----------|-------|------------|
| system/midi | `DisplayEvents` | 92.0% | 59 swaps (f0/f13, etc.) |
| system/rndobj | `RndMesh::Load` | 94.9% | 215 swaps |
| system/synth_xbox | `SetMode@FftIpp` | 58.5% | 56 swaps |
| system/math | `CSHA1::Transform` | 55.0% | 104 pairs |

---

## Strategy Recommendations

### Immediate Actions (Week 1)

1. **Fix CONTROL_FLOW patterns** - These are the easiest wins
   - `UIListState::PageScroll` (99.7%)
   - `ChallengeSortNode::Custom` (98.8%)
   - `PartyModeMgr::ShufflePlaylist` (83.6%)
   - `FlowLabel::Load` (99.0%)

2. **Fix simple REGISTER_SWAP** - Variable reordering
   - `HamCamShot::SetFrame` (99.9%)
   - `DetectFrame::Reset` (99.1%)
   - `DirLoader::CreateObjects` (98.3%)

3. **Update database** - Some functions marked AT_LIMIT are actually complete
   - `Curl_strlcat` (100% but marked AT_LIMIT)

### Short-Term (Weeks 2-4)

1. **Implement missing synth_xbox functions**
   - `Init@Synth360` (critical for unit progress)
   - `NewInst@SynthSample360`
   - StaticClassName functions (trivial)

2. **Implement lazer/game functions**
   - Various PartyModeMgr functions
   - PseudoRandomPicker templates

3. **Medium-effort CONTROL_FLOW fixes**
   - `DataArray::Remove` (95.0%)
   - `UIListState::BuildScroll` (95.2%)

### Long-Term (Ongoing)

1. **Accept AT_LIMIT for LINKER_MERGED functions** - No source fix possible
2. **Accept AT_LIMIT for BOOL_MASK functions** - Compiler artifact
3. **Focus on units with most fixable functions** rather than highest overall %

---

## Unit-by-Unit Summary

### system/jpeg (99.22%)
- 1 incomplete function: `LoadBitmapIntoJpeg` (53.7%)
- **Verdict**: AT_LIMIT - structural issues, divergent behavior
- **Action**: Accept current state

### system/zlib (87.44%)
- Blocked by: `crc32_big` (external asm), ICF merged static functions
- **Verdict**: AT_LIMIT for most
- **Action**: Accept, external library code

### system/midi (87.77%)
- 6 incomplete, all AT_LIMIT
- Dominated by LINKER_MERGED and massive REGISTER_SWAP
- **Verdict**: AT_LIMIT
- **Action**: Accept current state

### system/net (82.26%)
- 50 incomplete functions
- Mix of LINKER_MERGED, CONTROL_FLOW, REGISTER_SWAP
- **Fixable**: `DingoServer::ManageJob` (signed/unsigned), `JsonConverter::LoadFromString` (regswap)
- **Action**: Fix CONTROL_FLOW functions, accept merged

### system/synth_xbox (28.86%)
- Many unimplemented functions
- Some complex DSP with REGISTER_SWAP
- **Action**: Implement missing functions first, accept regalloc noise

### system/math (36.60%)
- 46 incomplete, 11 unimplemented
- Good mix of fixable CONTROL_FLOW and REGISTER_SWAP
- **Action**: Implement mtx.cpp utilities, fix `Rand::Seed` signed issue

### lazer/game (62.02%)
- 18+ unimplemented functions
- Some CONTROL_FLOW fixable
- **Action**: Implement missing, fix control flow

---

## Appendix: Fix Strategies

### CONTROL_FLOW Fixes
```cpp
// Pattern: bne vs beq (unsigned comparison)
// Change:
if (x != 0)   // generates beq
// To:
if (x > 0)    // generates ble for unsigned

// Pattern: blt vs bge (condition inversion)
// Try inverting if/else blocks or changing >= to >
```

### REGISTER_SWAP Fixes
```cpp
// Pattern: r26 vs r27 swap
// Try reordering variable declarations:
// Before:
int a, b, c;  // compiler assigns r26=a, r27=b
// After:
int b, a, c;  // compiler assigns r26=b, r27=a

// Pattern: f29 vs f30 swap
// Reorder float variable declarations
```

### OFFSET_SWAP Fixes
```cpp
// Pattern: 0x50 vs 0x54 access order
// Check struct member order matches target
// May need to reorder struct members or use different access pattern
```

---

## Tracking Progress

After making changes, run:
```bash
ninja build/373307D9/report.json
python3 ./scripts/analysis/compare_progress.py ../og-dc3-decomp/build/373307D9/report.json build/373307D9/report.json --detailed
```

Use `mcp__orchestrator__run_objdiff` to verify individual function improvements.
