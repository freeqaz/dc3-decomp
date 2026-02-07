# Session: Regression Recovery & WIP Cherry-Pick

**Date**: 2026-02-07
**Result**: 41.31% → 43.05% (+1.74%), zero new regressions

## Problem

Between commits `8fce3bc7` and `5db25017`, 231 functions regressed (~31.5 KB lost). Three root causes:

1. **STL Heap Template** - `const` added to `_Tp __val` in `__push_heap` (`_heap.c` line 77)
2. **STL Allocator** - `StlAlloc.h` changed from if/else to ternary operators, removed `int size` local
3. **Individual function changes** - Various modifications across multiple files

Additionally, 103 source files of WIP work from previous agent sessions sat uncommitted in the working directory, mixing improvements with regressions.

## Phase 1: STL Template Fixes

### _heap.c (8 functions)
Removed `const` from `_Tp __val` parameter in the custom comparator overload of `__push_heap`. The non-comparator overload already used non-const. Xbox 360 compiler is sensitive to const qualifiers in template parameters - they change mangled symbol names.

### StlAlloc.h (~200 functions)
Reverted two changes:
- `allocate()`: ternary back to if/else for name lookup
- `deallocate()`: restored `int size = count * sizeof(T)` local variable, ternary back to if/else

**Key insight**: Since `StlAlloc.h` is template code inlined into every STL container operation across the entire codebase, even small codegen differences cascade into hundreds of regressions.

**Recovery**: +25.4 KB, +101 functions (commit `1edd0031`)

### Individual Fixes
- `FormatTimeMSH`: Reverted from multi-step double algorithm to original single-expression float
- `MQSongSortMgr::IsSong`: Reverted from `const std::vector<Symbol>&` to vector copy (`std::vector<Symbol> syms = it->second`)

## Phase 2: WIP Cherry-Pick

### Strategy
Instead of discarding the junior engineers' WIP work, applied it incrementally in tested batches:

1. Apply batch of files from `wip-junior-work` branch
2. Build (`ninja`)
3. Check progress delta
4. If positive: commit. If negative: revert and bisect.

### Results by Batch

| Batch | Files | Delta Bytes | Delta Funcs | Notes |
|-------|-------|-------------|-------------|-------|
| 1: Headers | 7 | +576 | +3 | Getters, destructors |
| 2: Small code | 12 | +800 | +4 | Object.h push_back, UIEventMgr enums |
| 3a: char/hamobj | 4 | +460 | +3 | NavHighlightMsg implementation |
| 3b: system | 5 | +300 | +2 | UILabelDir, HamDirector, etc. |
| 3c: os/rndobj | 5 | +620 | +4 | File, CharTransDraw, Mat |
| 3d: math/meta | 3 | +104 | +1 | mtx, MetaPerformer, NavListSort |
| 3e: flow/gesture | 11 | +248 | +1 | Flow system + gesture filters |
| 3f: synth_xbox | 5 | +108 | +1 | SynapseAPO, HeadsetXferEffect |
| 3g: rndobj/char | 7 | +492 | +3 | Watcher, Bitmap, Font, CharClip |
| 4a: game/meta | 14 | +4,784 | +33 | Big win - SaveLoadManager, HamSongMgr |
| 4b: remaining | 20 | +1,820 | +13 | OS, rnddx9, ui, utl, world |
| Headers | 6 | +380 | +7 | Sequence.h, synth/Utl.h |
| **Total** | **102** | **~+10,700** | **~+75** | |

### Rejected: ObjPtr_p.h

The `CopyRef` template method addition to `ObjPtr_p.h` caused **-36,680 bytes, -176 functions** when applied alone. This is the single worst regression discovered.

**Root cause**: `ObjPtr_p.h` defines `ObjRefConcrete<T1, T2>` which is instantiated for every `ObjPtr` and `ObjRef` across the entire codebase. Adding any new method to this template changes the compiled output of every translation unit that includes it.

**Future fix options**:
- Define `CopyRef` only in the specific .cpp file that needs it
- Use explicit template instantiation to control which TUs get it
- Check if the original binary even has CopyRef as a separate method vs inlined

### Broken WIP Files (fixed during cherry-pick)

- `synth/Sequence.cpp`: Referenced `AvgIntervalSecs` / `IntervalSpread` members that didn't exist yet in `Sequence.h`. Fixed by applying Sequence.h first.
- `synth/Utl.cpp`: Type mismatch with `gWavFileCacheHelper` (pointer vs object). Fixed by applying Utl.h change (pointer→object was correct).

## Pre-existing Regressions (NOT from our work)

| Unit | Change | Notes |
|------|--------|-------|
| system/net/curl/lib/connect | -0.02% | Trivial, likely rounding |
| system/rndobj/MeshDeform | -0.81% (+1 func) | Gained a function but lost bytes in `Load` (88.5%, register swap issues) |

Both verified by testing against the clean STL-fix-only state.

## Lessons Learned

1. **Template headers are nuclear**: A single method addition to a widely-included template header can cause -36 KB regression. Always test header changes in isolation.
2. **Ternary vs if/else matters**: The Xbox 360 MSVC compiler generates different code for ternary operators vs if/else blocks. This is a known pattern for this compiler.
3. **Incremental testing works**: The batch-and-test approach caught the ObjPtr_p.h regression before it was committed, while still recovering 102/103 files successfully.
4. **Don't throw away WIP code**: The junior engineers' work contained real improvements (+10.7 KB) that just needed careful integration. Staff engineer approach of debugging rather than discarding paid off.

## Commits Created

```
1edd0031 Fix STL template regressions in heap and allocator (~200 functions)
71e80a98 Fix FormatTimeMSH signature to use float instead of double
bfcadea9 Fix MQSongSortMgr::IsSong to copy vector instead of using reference
585e8f62 Cherry-pick Batch 1: Simple header additions from WIP
90131920 Cherry-pick Batch 2: Small code improvements from WIP
323334c9 Cherry-pick Batch 3a: char/hamobj improvements
17d5df60 Cherry-pick Batch 3b: system improvements
453797cc Cherry-pick Batch 3c: os/char/rndobj improvements
751c1643 Cherry-pick Batch 3d (partial): math/meta_ham improvements
fc64747d Cherry-pick Batch 3e: flow/gesture improvements
97f6d9f0 Cherry-pick Batch 3f: synth_xbox improvements
57419b9a Cherry-pick Batch 3g: rndobj/char improvements
fb8aecdd Cherry-pick Batch 4a: game/meta/system files
a5c48113 Cherry-pick Batch 4b: remaining system files
6568725c Apply Vec.h comment improvements
21aa15e1 Apply ogg/mdct improvements
30787796 Apply Sequence/BinkMovieSys header improvements
548250c5 Apply Sequence.cpp improvements
e9877285 Apply synth/Utl improvements
```
