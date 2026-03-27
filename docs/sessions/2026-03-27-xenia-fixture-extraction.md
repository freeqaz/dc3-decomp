# Session: Xenia Fixture Extraction Research

*Date: 2026-03-27*

## Goal

Leverage Xenia to extract game state "fixtures" from the running Xbox 360 debug binary — specifically the state of Dir objects after they merge — so we can write tests ensuring decomp logic matches the original game.

## Motivation

The [HUD merge target divergence](2026-03-26-hud-merge-target-divergence.md) demonstrated how subtle differences in merge behavior cause crashes. We discovered `~ObjectDir::NullifyAllRefs` was cascading too aggressively, killing reparented objects. These bugs are hard to catch without knowing exactly what the Xbox game's state looks like at each merge point. If we can dump that state as fixtures, we can write deterministic tests that compare decomp output against ground truth.

## Current Xenia State

- **Branch**: `headless-vulkan-linux` at `../xenia`
- **Binary**: `build/bin/Linux/Checked/xenia-headless` (rebuilt 2026-03-27 with Vulkan)
- **Debug XEX**: `orig-assets/debug.xex` boots cleanly, renders splash screen, runs main loop at ~175fps
- **Layout detection**: `--dc3_nui_patch_layout=original` correctly identifies original binary
- **Screenshots**: Validated in `archive/screenshots/xenia-test/`

## Research Findings

### Xenia Hooking Capabilities

Five mechanisms are available for intercepting/inspecting guest code:

| Mechanism | API | Notes |
|-----------|-----|-------|
| **Guest extern overrides** | `processor->RegisterGuestFunctionOverride(addr, handler)` | Replace any guest function with C++ callback. Handler receives `PPCContext*` with r3-r10 args. Fast. Already used for 59 NUI stubs. |
| **PPC bytepatch** | `memory->TranslateVirtual()` + `store_and_swap` | Write PPC instructions directly into guest memory. Used for ArkFile::Read trampoline. Can create code caves. |
| **Breakpoints** | `Breakpoint(processor, kGuest, addr, callback)` | Stop at any guest PC with callback. Slow (JIT unwind). Good for debugging, not for production hooks. |
| **Memory read/write** | `memory->TranslateVirtual<T>(guest_addr)` + `load_and_swap<uint32_t>` | Direct host pointer to guest memory. Big-endian reads. Already used in `HostSetDirtyForce`. |
| **Guest function invocation** | `processor->Execute(thread_state, addr, args, count)` | Call any guest PPC function from host C++. Sets r3-r10 args, returns r3. Battle-tested in Xenia's own test suite. |

### Symbol Resolution

`config/373307D9/symbols.txt` contains every function from the debug binary with exact guest virtual addresses. No translation needed — addresses are ready to use.

```
?PostMerge@FileMerger@@IAAXPAUMerger@1@PAVDirLoader@@_N@Z = .text:0x823BC100; // type:function size:0x4E4
?SaveObjects@DirLoader@@SAXAAVBinStream@@PAVObjectDir@@@Z = .text:0x825A5298; // type:function size:0x4E0
?DataReadString@@YAPAVDataArray@@PBD@Z = .text:0x825C12E8
?Execute@DataArray@@QAA?AVDataNode@@_N@Z = .text:0x825A1528
```

### ObjectDir Memory Layout

ObjectDir is 0x9c bytes. Key offsets for memory walking:

| Struct | Offset | Field | Type |
|--------|--------|-------|------|
| Hmx::Object | +0x00 | vtable | `void**` |
| Hmx::Object | +0x04 | mRefs | `ObjRef` |
| Hmx::Object | +0x10 | mTypeProps | `TypeProps*` |
| Hmx::Object | +0x20 | mName | `const char*` |
| Hmx::Object | +0x24 | mDir | `ObjectDir*` |
| ObjectDir | +0x08 | mHashTable | `KeylessHash<>` |
| KeylessHash | +0x00 | mEntries | `Entry*` |
| KeylessHash | +0x04 | mSize | `int` (capacity) |
| KeylessHash | +0x08 | mOwnEntries | `bool` (+3 padding) |
| KeylessHash | +0x0c | mNumEntries | `int` (used) |
| KeylessHash | +0x10 | mEmpty | `Entry` (8 bytes, sentinel) |
| KeylessHash | +0x18 | mRemoved | `Entry` (8 bytes, sentinel) |
| Entry | +0x00 | name | `const char*` |
| Entry | +0x04 | obj | `Hmx::Object*` |
| ObjectDir | +0x28 | mStringTable | `StringTable` |
| ObjectDir | +0x50 | mSubDirs | `vector<ObjDirPtr>` |
| ObjectDir | +0x5c | mIsSubDir | `bool` |
| ObjectDir | +0x64 | mPathName | `const char*` |
| ObjectDir | +0x94 | mAlwaysInlined | `int` |

**Virtual inheritance caveat**: ObjectDir uses virtual inheritance from Hmx::Object. The vbase pointer complicates direct offset arithmetic from the ObjectDir base. Safer to work from Entry.obj pointers (which point to the Hmx::Object subobject directly).

**Proven pattern**: `HostSetDirtyForce` in `dc3_hack_pack.cc` already walks child object lists via raw memory reads at hardcoded offsets with `xe::load_and_swap<uint32_t>`.

### DTA Scripting

The engine has 170+ built-in DTA functions including object introspection (`object_list`, `foreach`, property access). The native port exposes `/api/dta/eval` via HTTP. Xenia doesn't have this yet, but we can call `DataReadString` + `DataArray::Execute` from host code via `Processor::Execute()` to evaluate DTA inside the running guest.

### Guest Function Invocation

Xenia fully supports calling guest PPC functions from host C++:

```cpp
// High-level API
uint64_t args[] = { guest_string_ptr };
uint64_t result = processor->Execute(thread_state, 0x825C12E8, args, 1);
// result = DataArray* from DataReadString

// Low-level: manual PPCContext setup
auto ctx = thread_state->context();
ctx->r[3] = dir_ptr;
ctx->lr = 0xBCBCBCBC;
auto fn = processor->ResolveFunction(save_objects_addr);
fn->Call(thread_state, 0xBCBCBCBC);
```

This is how `Processor::Execute` works internally — sets r3-r10 for args, lr for return, calls into JIT.

## Approaches Evaluated

### Approach 1: Memory Structure Walking (Pure Host-Side) — SELECTED FOR PHASE 1

Hook `FileMerger::PostMerge`. When it fires, walk the ObjectDir hash table and subdir list directly from guest memory.

```
PostMerge fires → read ObjectDir at this_ptr (r3)
  → read mHashTable (+0x08): iterate Entry[] array
    → for each entry: read name (char*), obj pointer
      → for each obj: read mName (+0x20), mDir (+0x24), vtable (+0x00)
  → read mSubDirs (+0x50): iterate vector
    → recurse into each subdir
  → write JSON to host filesystem
  → call original function via trampoline
```

**Pros**: No guest code execution needed, works at any point, predictable timing
**Cons**: Only captures fields we know offsets for, can't get typed property values

### Approach 2: Guest DTA Eval Bridge — PHASE 2

Allocate guest memory for a DTA string, call `DataReadString()` + `DataArray::Execute()`, capture output via the Debug::Print hook (already in hack pack).

**Pros**: Full property access, game's own type system, rich output
**Cons**: DTA eval may not be safe at every execution point (reentrancy, thread safety)

### Approach 3: Save/Load Stream Capture

Hook `DirLoader::SaveObjects` or `BinStream::Write` to tee serialized bytes. Or invoke `SaveObjects` from host after merge.

**Pros**: Captures exact binary .milo format, existing parsing tools
**Cons**: Save only writes what the Save method includes (not runtime-only state like merged children)

### Approach 4: Hybrid Hook + DTA

Combine memory walk for structure + DTA eval for properties.

**Pros**: Best of both worlds
**Cons**: Most complex

### Approach 5: Full Memory Region Dump + Offline Analysis

At breakpoints, dump entire memory regions and process offline with Python tooling.

**Pros**: Simple capture, flexible analysis
**Cons**: Large dumps, requires maintaining offline struct definitions, slow iteration cycle

## Implementation Plan

### Phase 1 — Memory Walk on PostMerge (Proof of Concept)

**Goal**: When `FileMerger::PostMerge` fires, dump the target Dir's object tree to JSON.

**Steps**:
1. Add PostMerge address to Dc3Addresses struct (from symbols.txt)
2. Write `Dc3DumpObjectDir(Memory*, uint32_t dir_ptr)` — walks hash table, reads names, recurses subdirs
3. Register guest extern override at PostMerge that:
   - Reads r3 (Merger* this), follows to get the target ObjectDir*
   - Calls `Dc3DumpObjectDir` to serialize tree
   - Writes JSON to `/tmp/dc3_fixtures/`
   - Trampolines to original PostMerge code
4. Boot debug.xex, let it reach gameplay, collect fixtures
5. Validate: compare fixture against known HUD structure

**Key addresses** (from symbols.txt):
- `FileMerger::PostMerge` = `0x823BC100`
- `MergeDirs` (free function) = look up in symbols.txt
- `ObjectDir::PostLoad` = look up in symbols.txt

**Output format**:
```json
{
  "function": "FileMerger::PostMerge",
  "timestamp_ms": 12345,
  "dir": {
    "name": "hud",
    "path": "world/hud",
    "class": "PanelDir",
    "num_objects": 48,
    "objects": [
      {"name": "hud_left", "class": "RndDir", "dir": "hud"},
      {"name": "score_left", "class": "UILabel", "dir": "hud"},
      ...
    ],
    "subdirs": [
      {"name": "flash_cards", "class": "ObjectDir", "inline": true, "objects": [...]}
    ]
  }
}
```

### Phase 2 — Guest DTA Eval Bridge

1. Write `Dc3EvalDTA(Memory*, Processor*, ThreadState*, const char* dta)` helper
2. Allocate guest memory for DTA string (use existing guest pool allocator)
3. Call `DataReadString` → `DataArray::Execute` from host
4. Capture output via Debug::Print hook redirect
5. Wire into PostMerge hook for property-level dumps (showing, position, etc.)

### Phase 3 — Test Integration

1. Define fixture JSON schema per test case
2. Native port tests load fixture files and compare against decomp's merge output
3. Test pattern: load same .milo → merge → compare object tree against fixture

### Phase 4 — CI Regression

1. Boot script: `xenia-headless --target=debug.xex` → collect fixtures → diff against baseline
2. Any fixture change = decomp behavioral regression
3. Runs headless, no GPU needed for structural fixtures

## Additional Research Notes

### Existing Infrastructure We Can Leverage

- **ViewerPoseDump** (`native/src/viewer/ViewerPoseDump.cpp`): Already iterates Dirs recursively with `ObjDirItr<RndTransformable>`, writes JSON. Good pattern reference for the native-side fixture comparison.

- **HostSetDirtyForce** (`dc3_hack_pack.cc`): Walks object children list at offset +0x9C via raw memory reads. Proves the pattern works. Uses `TranslateVirtual + load_and_swap`.

- **Test infrastructure** (`native/tests/test_merge_*.cpp`): Has `VerifyRingIntegrity()`, `VerifyAllRingsInDir()`, `CountUnreachableObjects()`. Can be extended to compare against fixtures.

- **ArkFile::Read trampoline** (`dc3_hack_pack.cc:2655-2685`): Shows how to bytepatch a function entry to branch to a stub, register an extern override on the stub, then return to the original. This is the trampoline pattern we need for PostMerge.

### DTA Functions Available for Phase 2

Key functions from `DataFunc.cpp` (170+ total):
- `object` — find by name/path
- `object_list` — list objects in a dir
- `exists` — check if object exists
- `foreach` / `foreach_int` — iteration
- `printf` / `sprintf` / `print` — output
- `handle` / `handle_type` — method dispatch
- Property access via object method syntax

Full function list available via native port's `/api/dta/funcs` endpoint (381 functions when including object methods).

### Memory Allocation in Guest

The hack pack has `Dc3GuestPool` — a bump allocator that allocates guest-visible memory. Could be used to allocate DTA string buffers for Phase 2 guest eval.

### Telemetry System

`dc3_runtime_telemetry.h` provides JSONL event recording. Could extend to record fixture dumps as telemetry events rather than standalone files.

### GDB RSP Stub

The `--dc3_gdb_rsp_stub` flag enables a GDB remote debugging stub. While useful for interactive debugging, it's too slow for automated fixture extraction. Mentioned for completeness.

### Breakpoint Callbacks

`Breakpoint` class supports hit callbacks with `ThreadDebugInfo*` context. Slower than extern overrides (requires JIT unwind) but could be useful for one-off investigation points without needing a full trampoline.

### Thread Safety Concerns

Guest extern overrides execute on the calling guest thread. For PostMerge this is fine (runs on main thread). For Phase 2 DTA eval, must ensure we're on the main thread — DTA evaluation is not thread-safe. The PostMerge hook naturally runs on main thread so this is safe.

### Virtual Table Resolution

To determine an object's class name from memory, we need its vtable pointer. The vtable's first few entries contain RTTI or class-specific functions. An alternative: the engine stores class names in `TypeProps` at offset +0x10 from Hmx::Object. If we can read that, we get the class name string directly. Need to verify the TypeProps layout.

### StringTable Buffer Chain

Object names in ObjectDir are stored in StringTable buffers (not standard C strings). The `const char*` in Entry.name points INTO a StringTable buffer, so it IS a regular null-terminated string — just allocated from a pool. Safe to read with `TranslateVirtual<const char*>`.

---

## Phase 1 Finalized Implementation Spec

*Produced by Opus subagent after validating addresses, struct layouts, and trampoline patterns against source.*

### Verified Addresses

| Symbol | Address | Size |
|--------|---------|------|
| `FileMerger::PostMerge(Merger*, DirLoader*, bool)` | **0x823BC100** | 0x4E4 |
| `FileMerger::FinishLoading(Loader*)` | 0x823BE698 | 0x20C |
| `MergeDirs(ObjectDir*, ObjectDir*, MergeFilter&)` | 0x8259CB80 | 0x140 |
| `DataReadString(const char*)` | 0x825C12E8 | (Phase 2) |
| `DataArray::Execute(bool)` | 0x825A1528 | (Phase 2) |
| `Debug::Print` | 0x8297D380 | (already hooked) |

### PostMerge Calling Convention

```
r3 = FileMerger*        (this)
r4 = Merger*            (the merger struct with target dir info)
r5 = DirLoader*         (source dir loader; NULL on failed load)
r6 = bool               (true = merge succeeded)
```

### Navigating Merger -> Target ObjectDir

From `r4` (Merger*):
- Read `mDir.mObject` at **Merger + 0x34** → this is the target ObjectDir*
- If null, fallback: read `mDir.mOwner` at **Merger + 0x38**, then that object's `mDir` at **owner + 0x24**

### Validated Struct Offsets

**Merger** (at r4):
```
+0x00  mName.mStr       (const char*)     Merger identifier
+0x18  mLoaded          (FilePath)        Path of loaded file
+0x34  mDir.mObject     (ObjectDir*)      TARGET DIR POINTER
+0x38  mDir.mOwner      (Hmx::Object*)    Fallback owner
```

**ObjectDir** (from dir_ptr) — *validated against Dir.h*:
```
+0x00  vbptr            (uint32_t)        Points to vbtable
+0x08  mHashTable       (KeylessHash)     32 bytes total
  +0x08  .mEntries      (Entry*)
  +0x0C  .mSize         (int)             Hash table capacity
  +0x10  .mNumEntries   (int)             Used entries count  [CORRECTED: was 0x14]
  +0x14  .mEmpty        (Entry)           Sentinel (8 bytes)  [CORRECTED: was 0x18]
+0x28  mStringTable     (StringTable)     20 bytes
+0x3C  mProxyFile       (FilePath)        8 bytes
+0x44  mProxyOverride   (bool)
+0x48  mInlineProxyType (enum)
+0x4C  mLoader          (DirLoader*)
+0x50  mSubDirs._Myfirst      (ObjDirPtr*)
+0x54  mSubDirs._Mylast       (ObjDirPtr*)
+0x64  mPathName              (const char*)
```

**ObjectDir -> Hmx::Object** (via virtual base):
```
uint32_t vbptr   = read32(dir_ptr + 0x00);
int32_t  vb_disp = read32(vbptr + 4);        // MSVC vbtable[1]
uint32_t hmx_obj = dir_ptr + vb_disp;
uint32_t name    = read32(hmx_obj + 0x20);    // mName
```

**Always use vbptr indirection** — `vb_disp` varies by derived class (0x9C for plain ObjectDir, larger for PanelDir/RndDir/etc).

**Hash Table Entry** (8 bytes):
```
+0x00  name  (const char*)   NULL-terminated (StringTable pointer)
+0x04  obj   (Hmx::Object*)  Direct pointer to Object vbase
```

**Hmx::Object** (from Entry.obj):
```
+0x00  vtable      (void**)
+0x10  mTypeProps  (TypeProps*)
+0x14  mTypeDef    (DataArray*)
+0x20  mName       (const char*)
+0x24  mDir        (ObjectDir*)
```

**ObjDirPtr** (subdir vector element, size 0x14):
```
+0x0C  mObject     (ObjectDir*)
```

### Class Name Resolution

Build a vtable-to-classname lookup from symbols.txt at setup time:
```
??_7ObjectDir@@6BObject@Hmx@@@ = .rdata:0x82027A94  → "ObjectDir"
??_7PanelDir@@6BObject@Hmx@@@  = .rdata:0x82059078  → "PanelDir"
```

Read `vtable_ptr = read32(obj + 0x00)`, look up in map. O(1) per object, no guest invocation needed.

### Trampoline Design

1. Save PostMerge's first 4 instructions (16 bytes) to a code cave
2. Write trampoline: [4 saved insns] [b PostMerge+0x10]
3. Bytepatch PostMerge entry: [mflr r0] [bl STUB] [mtlr r0] [blr]
4. Write `blr` at STUB, register C++ handler on STUB
5. Handler: dump state, then `processor->Execute(thread_state, trampoline, args, 4)` to run original

### Edge Cases

| Risk | Mitigation |
|------|-----------|
| Null mDir.mObject | Fall back to mDir.mOwner->Dir() |
| Invalid/freed pointers | Validate every pointer: `p != 0 && p < 0xF0000000` |
| SubDir cycles | `std::set<uint32_t> visited` + depth limit 20 |
| Large hash tables | Cap at mSize, warn if > 10000 |
| Code cave collision | Place trampoline outside PostMerge body (in existing protocol_debug_string cave area) |
| Thread safety | PostMerge runs on main thread only — safe |

### Implementation Checklist

**A. dc3-decomp side:**
1. Add `file_merger_post_merge` to ADDRESS_CATALOG in `scripts/build/generate_xenia_dc3_patch_manifest.py`
2. Regenerate manifest with `ninja`

**B. xenia side (dc3_hack_pack.cc):**
3. Add `file_merger_post_merge = 0x823BC100` to `Dc3Addresses`
4. Add `get()` call in `Dc3PopulateAddressesFromCatalog`
5. Build vtable-to-classname map from symbols (hardcode top ~50 for Phase 1)
6. Write `Dc3DumpObjectDir()` — memory walk with JSON output
7. Write `Dc3PostMergeHandler()` — extract args, dump, call-through
8. Set up trampoline in patch application
9. Gate behind `--dc3_fixture_extraction=true` cvar (default false)

**C. Validation:**
10. Boot debug.xex with fixture extraction enabled
11. Let game reach character/HUD loading
12. Check `/tmp/dc3_fixtures/` for valid JSON files
13. Verify game behavior unchanged
14. Parse JSON, confirm plausible object names and counts
