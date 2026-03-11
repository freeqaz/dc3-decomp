# Native Port: Inlined Subdir Loading Fix & HasDirPtrs O(n²) Fix

**Date**: 2026-03-11
**Scope**: Native port engine fixes — two critical bugs blocking world file loading

## Problem 1: Inlined Subdir Loading Crash

### Symptom
Files containing inlined subdirectories (`director.milo_xbox`, `world/gen/world.milo_xbox`, all venue worlds) crashed with:
```
FAIL: String chars N > 512
```
during `ObjectDir::PreLoad` at the inlined subdir loading stage.

### Root Cause
`PanelDir` was not registered as an object factory in the test engine init. The full game registers it via `UIManager::Init()` (UI.cpp:792), but that function is too heavy for tests (needs SystemConfig, Automator, cameras, etc.).

When `DirLoader::CreateObjects` encounters an unregistered class, it produces a NULL object entry. Later, when the loader tries to read that object's data, it calls `ReadDead` to skip the unknown bytes — but the dead marker scan gets desynchronized from the actual stream position. Subsequent reads interpret random data as string lengths, hitting the `> 512` guard.

### Fix
Added manual factory registration to `native/tests/test_helpers.cpp`:
```cpp
REGISTER_OBJ_FACTORY(PanelDir)
REGISTER_OBJ_FACTORY(UIPanel)
REGISTER_OBJ_FACTORY(UIScreen)
```

### Verification
- Converted two `EXPECT_DEATH` crash tests to normal load-and-verify tests
- `LoadWorldMasterFile`: loads `world/gen/world.milo_xbox`, verifies class is `WorldDir`
- `LoadDirectorSubdir`: loads `director.milo_xbox`, verifies class is `RndDir`
- `LoadFullVenueWorlds`: loads all 8 venue worlds (glitterati, dclive, houseparty, rollerrink, bid, dci, throneroom, streetside)

## Problem 2: O(n²) Destructor Performance

### Symptom
After fixing the crash, world files loaded successfully but took **30+ minutes** to destroy. A single venue world with ~3500 objects would hang the test suite.

### Root Cause
`ObjDirPtr::operator=(nullptr)` (Dir.h:71) is called for every subdir during `ObjectDir::~ObjectDir() → DeleteSubDirs() → mSubDirs.clear()`. Each call does:
1. `mObject->Release(this)` — O(1), removes from ref ring
2. `mObject->HasDirPtrs()` — **O(n)**, walks the ENTIRE ObjRef ring checking `IsDirPtr()` on every ref

With 500+ objects having cross-references, this is O(n²). Performance trace confirmed 100% of CPU time in:
```
WorldDir::~WorldDir() → PanelDir::~PanelDir() → RndDir::~RndDir() →
ObjectDir::~ObjectDir() → vector<ObjDirPtr>::clear() → ObjDirPtr::~ObjDirPtr() →
ObjDirPtr::operator=(nullptr) → HasDirPtrs() → FOREACH(mRefs)
```

### Fix: Static Counter Map (native-only)
Added an `unordered_map<const void*, int>` that tracks how many `ObjDirPtr`s reference each object. The counter is maintained in `ObjDirPtr` constructors and `operator=`. `HasDirPtrs()` checks the counter in O(1) instead of walking the ring.

All changes are guarded by `#ifdef HX_NATIVE` — zero impact on PPC decomp match.

#### Why not add a member to `Hmx::Object`?
The first attempt added `int mDirPtrRefCount` to `Hmx::Object`. This shifted the class layout by 8 bytes (4 + alignment), which changed **every non-virtual thunk offset** for classes with multiple inheritance from Object-derived types. The pre-generated stub file (`engine_stubs_generated.cpp`) had thunks at the old offsets (e.g., `_ZThn88_N9NgDOFProc6DoPostEv`) but the compiler now generated references to new offsets (`_ZThn96_...`), causing link failures.

#### Why not use `IsDirPtr()` in `Object::AddRef/Release`?
During base class construction (`ObjRefConcrete` constructor), the virtual table pointer hasn't been updated to the derived class yet. Calling `ref->IsDirPtr()` in `Object::AddRef()` would resolve to `ObjRef::IsDirPtr()` (returns false), not `ObjDirPtr::IsDirPtr()` (returns true). The counter would never be incremented during construction, but would be decremented during destruction, going negative.

#### Files Modified
- **`src/system/obj/Dir.h`**: Added `DirPtrRefCounts()` static map, counter increment/decrement in `ObjDirPtr` copy constructor, `ObjDirPtr(C*)` constructor, and `operator=(C*)`
- **`src/system/obj/Dir.cpp`**: `HasDirPtrs()` uses counter on native, original ring walk on PPC

### Results
| Test | Before | After |
|------|--------|-------|
| `LoadWorldMasterFile` | 30+ min hang | **431ms** |
| `LoadFullVenueWorlds` (8 venues) | infinite hang | **11 seconds** |
| `HasDirPtrs` PPC match | 100% | **100%** (unchanged) |

## Bulk Loading Infrastructure

Added comprehensive bulk loading tests across all asset categories:

| Category | Files | Status |
|----------|-------|--------|
| flow | 3 | All pass |
| char | 324 | All pass |
| ui | 1,153 | All pass |
| sfx | 1,091 | All pass |
| songs | 2,678 | All pass |
| world | 141 | Most pass; `str_shirt.milo_xbox` hits ChunkStream loop (separate bug) |

Also created `scripts/milo/validate_milo_entries.py` — a pure Python milo container parser that validated all 5,399 `.milo_xbox` files independently as ground truth.

## Key Lessons

1. **Missing object factory registration causes stream desync, not just missing objects.** NULL entries from unregistered classes cause `ReadDead` to lose sync with the stream, corrupting all subsequent reads.

2. **Adding members to root classes breaks thunk stubs.** Non-virtual thunk offsets are baked into the stub file. Changing `Hmx::Object`'s size shifts every multi-inheritance thunk. Use external storage (static maps) for native-only data.

3. **Virtual dispatch is unreliable during construction.** Can't use `IsDirPtr()` in base class `AddRef/Release` because the vtable hasn't been updated to the derived class yet during `ObjRefConcrete` construction.

4. **O(n²) ref ring walks are the #1 native port performance killer.** The ObjRef ring is a linked list traversed per-operation. Any function that walks it per-element creates quadratic behavior. Cache results or use counters.

5. **`MILO_FATAL_FAILS=0` doesn't prevent all hangs.** Some error paths (like ChunkStream read failures) loop even when `Debug::Fail()` returns instead of aborting. The code after the fail point may retry or proceed with corrupt state.
