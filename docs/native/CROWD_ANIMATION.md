# Crowd Animation System — Native Port

## Overview

Crowd characters in DC3 use billboard impostor rendering (WorldCrowd) and are animated
via CharClipGroups. The animation pipeline involves FileMerger loading multiple .milo
files and merging their CharClip objects into base clip dirs.

## Pipeline

1. **Crowd character .milo loaded** (e.g., `crowd_f_00s_01.milo`)
   - Contains CharClipGroups (e.g., `stand_ok`, `stand_skills_ok`) with references
     to CharClip objects by name
   - On Xbox, all clip subdirs are pre-loaded, so ObjPtrVec::Load resolves all refs
   - On native, clip subdirs may not be loaded yet → **null ObjPtr entries**

2. **FileMerger `crowd_clips.fm`** triggers `load_tempo` handler
   - Maps venue to era (dclive→00s, glitterati→90s, etc.)
   - Loads 6 animation .milo files per tempo+era combination:
     - `{gender}_{tempo}.milo` (tempo clips)
     - `{gender}_base_{era}s.milo` (era clips)
     - `{gender}_{tempo}_{era}s.milo` (combined clips)
   - Each loads into `female_base` or `male_base` clip dir via `MergeObject(kCopyFromMax)`

3. **Copy(kCopyFromMax)** in CharClipGroup::Copy
   - Adds new clips from merged dir if not already present (checks by name)
   - Skips null entries in source

4. **PlayCrowdAnimation** iterates crowd members
   - Looks up stance property → builds group name (e.g., `stand_skills_ok`)
   - Calls `Driver()->PlayGroup()` → `CharClipGroup::GetClip(flags)`

## Bugs Fixed (Session 73)

### Null Entry Abort in GetClip

**Root cause**: ObjPtrVec::Load inserts null entries for unresolved references when
`mListMode != kObjListNoNull`. CharClipGroup uses `kObjListNoNull`, so during Load,
null entries for clips that don't exist yet are still inserted. After FileMerger adds
valid clips via merge, the original null entries persist alongside the new valid ones.

**Original code** (broken):
```cpp
for (int i = 0; i < mClips.size(); i++) {
    if (!mClips[i]) return nullptr;  // Aborts on FIRST null
}
```

**Fix**: Purge null entries in reverse iteration before running LRU logic:
```cpp
for (int i = mClips.size() - 1; i >= 0; i--) {
    if (!mClips[i]) mClips.erase(mClips.begin() + i);
}
```

### FastInt Out-of-Range (Rand.cpp)

**Root cause**: `Rand::FastInt(low, high)` does `(Int() * range) >> 16`. On both PPC
and x86, `Int()` returns full 32-bit signed values. When `Int()` is large and `range > 1`,
the product overflows 32 bits, and `>> 16` on the truncated result produces values
outside `[low, high)`. On PPC, no bounds checking → silent memory corruption. On x86
with debug STL (`_GLIBCXX_ASSERTIONS`), the OOB vector access asserts.

**Fix** (native only): Use modulo for correctness:
```cpp
return low + ((unsigned int)Int() % (unsigned int)(high - low));
```

### Auto-loading Crowd Clips

On native, `PlayCrowdAnimation` may be called before `LoadCrowdClips` in the normal
game flow. Added auto-detection: if FileMerger `crowd_clips.fm` exists but no mergers
have loaded files, trigger sync `LoadCrowdClips("medium", venue, false)`.

## Test Coverage

`native/tests/test_charclipgroup.cpp`:
- Basic GetClip/AddClip/HasClip/FindClip operations
- LRU rotation with multiple clips
- AddClip idempotency
- Null entry purge behavior
- Real crowd asset merge tests (require MILO_LIB)
- Full 6-file crowd pipeline test

## File Layout

Animation .milo files in `char/crowd/anim/gen/`:
- `{gender}_base.milo_xbox` — base clip dir with CharClipGroups
- `{gender}_{tempo}.milo_xbox` — tempo-specific clips (slow, medium, fast)
- `{gender}_base_{era}s.milo_xbox` — era-specific clips (70s, 80s, 90s, 00s, 10s)
- `{gender}_{tempo}_{era}s.milo_xbox` — combined tempo+era clips

## Known Limitations

- Camera person clips (`_00s_cameraperson_skills_ok`) not found — these may need
  separate animation files or are a venue-specific feature
- `FastInt` on PPC is fundamentally broken (UB from 32-bit overflow) but has "worked"
  for years via silent memory corruption. The native fix uses modulo which has different
  distribution characteristics (uniform vs shift-biased)
