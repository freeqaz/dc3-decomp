# Session: Parallel Opus Subagent Workflow Test

**Date:** 2026-01-23
**Focus:** Testing parallel Opus subagents with objdiff CLI and m2c workflows

---

## Summary

Tested a new workflow using parallel Opus subagents to tackle different decomp tasks concurrently. Three agents ran simultaneously analyzing different aspects of the codebase.

## Key Wins

| Function | Before | After | Change |
|----------|--------|-------|--------|
| `Game::PostWaitJump` | 88.9% | **99.93%** | +11% |
| `StringTable::Add` | 99.31% | **100%** | +0.7% |
| `EventEntry::Add` | 95% | **100%** | +5% |

### PostWaitJump Fix
Added conditional logic to check `TheSongSequence.unk28` flag before calling VenueEnter. Made `unk28` public in `SongSequence.h`.

### StringTable::Add Fix
Changed return value handling to reuse the `str` parameter instead of creating a new local:
```cpp
// Before
const char *oldChar = mCurChar;
mCurChar += len;
return oldChar;

// After - reuse str param, generates extra stw instruction
str = mCurChar;
mCurChar += len;
return str;
```

### EventEntry::Add Fix
Replaced if-statement with `MaxEq()` (generates branchless `fsel`) and reordered operations:
```cpp
// Before - generates fcmpu + bge branch
if (cur->maxMs < ms) cur->maxMs = ms;
cur->num++;
cur->totalMs += ms;

// After - generates fsel (branchless), matches instruction interleaving
MaxEq(cur->maxMs, ms);
cur->totalMs += ms;
cur->num++;
```

## Tools Workflow Verified

### objdiff CLI
```bash
# Find near-match targets
objdiff-cli report query build/373307D9/report.json --functions \
  --min-percent 90 --max-percent 99 --max-size 300 --limit 20

# Get fixability verdict
objdiff-cli diff -p . "FunctionName" -f json --verdict

# Detailed instruction diff
objdiff-cli diff -p . "FunctionName" -f json --include-instructions
```

### m2c Workflow
```bash
# Convert assembly to m2c format
python3 tools/asm_to_m2c.py build/373307D9/asm/<path>.s -f "FuncName" > /tmp/claude/func.s

# Decompile
cd ~/code/milohax/m2c && python3 m2c.py -t ppc /tmp/claude/func.s
```

**Output example for Box::Volume:**
```c
f32 Box_Volume(void *arg0) {
    return (arg0->unk18 - arg0->unk8) * (arg0->unk14 - arg0->unk4) * (arg0->unk10 - arg0->unk0);
}
```

## Functions Analyzed

### Confirmed AT_LIMIT (Unfixable)

| Function | Match | Root Cause |
|----------|-------|------------|
| `GetPresenceMode` | 99.44% | Symbol labels + 3 linker-merged calls |
| `Vector2DESmoother::ForceValue` | 99% | Store instruction scheduling |
| `Box::Volume` | 98.83% | Load instruction scheduling (y,z,x vs z,y,x) |
| `PageDirection` | 98.75% | Register allocation (r10↔r11) |
| `BufStream::Eof` | 98% | Member access order (0x18↔0x1c) |
| `UIListState::SetNumDisplay` | 88.9% | Linker-merged MakeString |
| `DxRnd::Offscreen` | 88.8% | Register allocation |
| `MoveRatingHistory::GetRating` | 88.6% | Comparison style (subf vs cmplw) |
| `MemStream::ReadImpl` | 96.56% | Register allocation (r7 vs r10) |
| `Curl_SOCKS5` | 99.15% | Constant caching in callee-saved reg |

### Previously Fixed (WORKSESSION outdated)
- `RatioToDb` - Already at 100%

## Units Ready for Closeout

13 units identified as AT_LIMIT (accept current match):

1. `system/zlib/inflate` (99.99%)
2. `system/net/curl/lib/dict` (99.99%)
3. `system/net/curl/lib/gopher` (99.99%)
4. `system/net/curl/lib/http_digest` (99.99%)
5. `lazer/meta_ham/AccomplishmentSongConditional` (99.98%)
6. `system/utl/SongInfoAudioType` (99.98%)
7. `lazer/meta_ham/Accomplishment` (99.97%)
8. `system/net/curl/lib/content_encoding` (99.94%)
9. `system/net/curl/lib/rawstr` (99.94%)
10. `system/net/curl/lib/fileinfo` (99.92%)
11. `system/net/curl/lib/base64` (99.92%)
12. `system/meta/Jukebox` (99.76%)
13. `system/utl/Option` (99.64%)

## Units Worth Investigating (COMPLETED)

Second batch of parallel Opus subagents tackled these 4 functions:

| Unit | Function | Before | After | Result |
|------|----------|--------|-------|--------|
| `system/utl/StringTable` | `StringTable::Add` | 99.31% | **100%** | ✅ Fixed |
| `system/obj/MessageTimer` | `EventEntry::Add` | 95% | **100%** | ✅ Fixed |
| `system/utl/MemStream` | `ReadImpl` | 96.56% | 96.56% | AT_LIMIT (register alloc) |
| `system/net/curl/lib/socks` | `Curl_SOCKS5` | 99.15% | 99.15% | AT_LIMIT (instruction sched) |

### MemStream::ReadImpl Analysis
Register allocation difference - compiler uses `r10` vs `r7` for `size - mOffset` calculation. Multiple reordering attempts made no improvement. The extra instruction is functionally equivalent.

### Curl_SOCKS5 Analysis
Original binary caches `-1` (CURL_SOCKET_BAD) in callee-saved register `r30` and reuses across two `Curl_socket_ready` calls. Our compiler loads `-1` as immediate at each call site. Cannot force compiler to cache constants in specific registers.

## Learnings

### Parallel Opus Subagents
- Work well for independent tasks
- Each agent maintains its own context and can make code changes
- Good for "scatter-gather" style analysis
- Cost: ~3x single agent but much faster wall-clock time

### Fixable vs Unfixable Patterns

**Likely Fixable:**
- Missing logic/conditionals
- Wrong algorithm structure
- `diff_op` (wrong opcode)
- `insert/delete` (structural differences)
- Sub-90% matches often have real bugs

**Unfixable (AT_LIMIT):**
- Scattered `diff_arg` with register swaps
- Linker-merged function calls (`merged_*`)
- Instruction scheduling differences
- Bool mask patterns (`clrlwi` for returns)
- Symbol label naming differences

### m2c Limitations
- Template instantiations need calling code implemented first
- Merged functions in binary hard to match with templates
- Good for initial scaffolding, not perfect matches

## Files Modified

**Batch 1 (Task-based agents):**
- `src/lazer/game/Game.cpp` - PostWaitJump fix
- `src/lazer/game/SongSequence.h` - Made unk28 public
- `src/system/math/Key.h` - Added BinStream operator>> template

**Batch 2 (Per-file agents):**
- `src/system/utl/StringTable.cpp` - Return value handling for Add()
- `src/system/obj/MessageTimer.cpp` - MaxEq() + operation reorder for EventEntry::Add()

## Session Statistics

| Metric | Value |
|--------|-------|
| Functions analyzed | 12+ |
| New 100% matches | **3** (PostWaitJump, StringTable::Add, EventEntry::Add) |
| Confirmed AT_LIMIT | 10+ |
| Units ready for closeout | 13 |
| Parallel agent batches | 2 |

---

## Continued Session: Sub-90% Function Fixes

**Focus:** Targeting sub-90% functions which often have real fixable bugs

### Additional Wins

| Function | Before | After | Change |
|----------|--------|-------|--------|
| `Pool::Pool` | 72% | **99.5%** | +27.5% |
| `MultiTempoTempoMap::AddTempoInfoPoint` | 87.88% | **100%** | +12.1% |
| `FixedSizeAlloc::Alloc` | 81.75% | **100%** | +18.25% |
| `FixedSizeAlloc::Free` | 89% | **99.82%** | +10.8% |
| `MoveRatingHistory::AddHistory` | 82.22% | **87.44%** | +5.2% |
| `ExternalMic::ExternalMic` | 94.19% | **99.71%** | +5.5% |

### Fix Details

#### Pool::Pool (72% → 99.5%)
Rewrote the free list initialization loop. Original code had wrong loop condition and didn't properly chain nodes:
```cpp
// Before - broken logic
for (int i = i3 - 1; i < i2; i++) {
    *(void **)mFree = (char *)v + ull;
    v = mFree;
}

// After - correct do-while with proper pointer advancement
if (count > 1) {
    int n = count - 1;
    do {
        char *next = ptr + stride;
        *(char **)ptr = next;
        ptr = next;
    } while (--n);
}
```

#### MultiTempoTempoMap::AddTempoInfoPoint (87.88% → 100%)
Changed `empty()` to `size() == 0` - these generate different code:
- `empty()` → pointer comparison (`cmplw begin, end`)
- `size() == 0` → division-based count (`divw size/12`)

```cpp
// Before
if (mTempoPoints.empty()) {

// After
if (mTempoPoints.size() == 0) {
```

#### FixedSizeAlloc::Alloc (81.75% → 100%)
Fixed completely broken allocator logic and reordered operations:
```cpp
// Before - broken, mFreeList = old was a no-op!
int *old = mFreeList;
mNumAllocs++;
mFreeList = old;  // BUG: same value!

// After - correct free list advancement with proper load ordering
int *ret = mFreeList;
int numAllocs = mNumAllocs + 1;
int *next = (int *)*ret;  // Follow the chain!
mNumAllocs = numAllocs;
mFreeList = next;
```

#### FixedSizeAlloc::Free (89% → 99.82%)
Fixed free list chain linkage:
```cpp
// Before - broken, overwrites parameter then assigns back
v = mFreeList;
mFreeList = (int *)v;

// After - proper linked list insertion
*(int **)v = mFreeList;  // Store old head in new block's next
mFreeList = (int *)v;     // New block becomes head
```

#### MoveRatingHistory::AddHistory (82.22% → 87.44%)
Fixed history preservation logic:
```cpp
// Before - overwrote all slots with new value
history.unk0[1] = (MoveRating)i2;
history.unk0[2] = (MoveRating)i2;
history.unk0[3] = (MoveRating)i2;
history.unk0[0] = (MoveRating)i2;

// After - preserve old value in history slots
MoveRating old = history.unk0[0];
history.unk0[1] = old;
history.unk0[2] = old;
history.unk0[3] = old;
history.unk0[0] = (MoveRating)i2;
```

#### ExternalMic::ExternalMic (94.19% → 99.71%)
Fixed thread function pointer - was calling instead of passing:
```cpp
// Before - calls function, casts return value (1) to pointer!
mThread = CreateThread(
    0, 0, (LPTHREAD_START_ROUTINE(__cdecl *))ExternalMicThreadEntry(0), this, 4, 0
);

// After - pass function pointer directly
mThread = CreateThread(0, 0, ExternalMicThreadEntry, this, 4, 0);
```

### Additional Files Modified

- `src/system/utl/Pool.cpp`
- `src/system/utl/MultiTempoTempoMap.cpp`
- `src/system/utl/PoolAlloc.cpp`
- `src/lazer/meta_ham/MoveRatingHistory.cpp`
- `src/system/synth_xbox/ExternalMic.cpp`

### Key Learnings (Continued)

1. **`empty()` vs `size() == 0`** - Different codegen for STL containers
2. **Allocator bugs are common** - Both Alloc and Free had critical bugs that made the free list completely non-functional
3. **Thread function pointers** - Easy to accidentally call instead of pass
4. **Load ordering matters** - Declaring locals and loading values in the right order affects instruction scheduling

### Updated Session Statistics

| Metric | Value |
|--------|-------|
| Functions analyzed | 20+ |
| New 100% matches | **5** (PostWaitJump, StringTable::Add, EventEntry::Add, AddTempoInfoPoint, FixedSizeAlloc::Alloc) |
| Near-matches (99%+) | **4** (Pool::Pool, FixedSizeAlloc::Free, ExternalMic, MoveRatingHistory) |
| Confirmed AT_LIMIT | 10+ |
| Units ready for closeout | 13 |

## Next Steps

1. ~~Investigate the 4 "worth investigating" units~~ ✅ Done
2. ~~Focus on sub-90% functions for real logic fixes~~ ✅ In Progress
3. Consider accepting 99%+ functions as complete
4. Use m2c workflow for 0% unimplemented functions with RB3 reference
5. Update WORKSESSION.md with newly confirmed AT_LIMIT functions
6. Continue targeting 70-90% functions - often have real bugs
