# PlaylistSortMgr::ResolvePlaylists - Session Notes

**Date:** 2026-02-08
**Symbol:** `?ResolvePlaylists@PlaylistSortMgr@@AAAXXZ`
**Unit:** `default/lazer/meta_ham/PlaylistSortMgr`
**Result:** 72.2% -> 90.2% (reported as at_limit)

## Context

The file had a bad merge/rewrite that also broke `ProcessNextCommand` and added a stray `}` in `ResolvePlaylists`. After reverting `PlaylistSortMgr.cpp` to fix build regressions, `ProcessNextCommand` was already 100% but `ResolvePlaylists` was at 72.2%.

Also fixed a separate regression in `ContentMgr.h` where `virtual void Poll()` was accidentally deleted.

## What Worked

| Change | Impact |
|--------|--------|
| `&activeProfile->GetPlaylist(i)` instead of `*playlist = activeProfile->GetPlaylist(i)` | Eliminated 2 `Playlist::operator=` calls. `GetPlaylist` returns `Playlist&`, so take address instead of value-copy through uninitialized pointer. |
| Precompute `int count = (int)unkd0.size()` before loop | Avoids re-evaluating `size()` (which involves `divw` by 0x2c) at the back-edge. Moved 4 instructions from back-edge to prologue. ~4% improvement. |
| `while (numSongs-- != 0)` countdown | Matches target's `mr./beq` + `subi/cmpwi cr6/bne cr6` pattern. Counting up with `j < numSongs` generated `subic.` (combined subtract-and-compare) instead of separate ops. |
| `playlist->SetOnlineID(-1)` after RemoveSong loop | +5.3% from previous attempt notes. Virtual call not obvious from decompilation. |
| `(int)unkd0.size()` cast to signed | Generates `ble` (signed compare) matching target. Without cast, unsigned `cmplwi` generates `beq` (wrong branch type). |

## What Didn't Work

| Attempt | Result | Why |
|---------|--------|-----|
| `unkb0 == profileName` instead of `!(unkb0 != profileName)` | Worse | Target calls `String::operator!=`, not `operator==` |
| Iterator-based first loop (`unkd0.begin()/end()`) | 84.8% | More structural differences despite fewer register swaps |
| Shared `i` across both loops (`for (i=count; i<5; i++)`) | 85-89% | Compiler doesn't generate the same branchless min(5,size) optimization |
| `flag ? 0 : size()` ternary | 87.7% | Generates `bne/beq` condition inversion |
| `!unkd0.empty()` for final check | 86.6% | Slightly better than `size() > 0` (86.4%) but the precomputed-count version dominates both |
| `while` vs `for` for outer loop | Same | Compiler generates identical code |
| `numSongs > 0` instead of `!= 0` | Worse | Generates `bgt` but target uses `bne` |

## Remaining Gap (90.2% -> 100%)

### Structural mismatches (9 instructions)

The target binary shares `i` between the copy loop and clear loop:

```
// Target's effective logic:
int size = unkd0.size();
for (i = 0; i < size; i++) { /* copy */ }
for (i = size; i < 5; i++) { /* clear */ }
```

The compiler generates a **branchless `min(5, size)`** to clamp the starting index:
```asm
subfic  r11, r30, 0x5    ; r11 = 5 - size
srwi    r10, r11, 31     ; r10 = signbit (1 if size>5, else 0)
subi    r10, r10, 0x1    ; r10 = mask (-1 if size<=5, 0 if size>5)
and     r24, r10, r11    ; r24 = max(0, 5-size)
```

Then `subfic r28, r24, 0x5` gives `min(5, size)` as the second loop's start.

Every attempt to reproduce this in C++ either:
- Generates different branch structure (ternary, explicit shared `i`)
- Loses the optimization (compiler doesn't see the shared variable pattern)
- Changes register allocation unfavorably

### Other

- 1 merged call: `GetNumSongs` -> `merged_826932F8` (ICF with `FontMap::NumMeshes/NumMaterials`)
- 15 register swaps across 3 pairs (cosmetic, `r23<->r29` dominant)
- Stride `li r24, 0x2c` hoisted before flag check in our code vs after in target (1 insert)

## Files Modified

- `src/lazer/meta_ham/PlaylistSortMgr.cpp` (ResolvePlaylists rewritten)
- `src/system/os/ContentMgr.h` (restored `virtual void Poll()`)
