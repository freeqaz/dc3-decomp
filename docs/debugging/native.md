# Native Build Debugging

Techniques for debugging the DC3 native Linux port — memory errors, object lifecycle issues, and crash diagnosis.

## Quick Reference

```bash
# Standard headless run (logic only, fastest)
# Good for boot/flow smoke tests. This is NOT long enough to verify post-intro gameplay animation.
MILO_HEADLESS=1 MILO_NORENDER=1 MILO_FATAL_FAILS=0 \
  MILO_MAX_FRAMES=3000 MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
  native/build/dc3-native

# ASan build (catches use-after-free, double-free, buffer overflow)
cmake -S native -B native/build-asan -G Ninja -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build native/build-asan --target dc3-native -- -j$(nproc)

# Run with ASan (suppress the benign strcpy overlap in FileGetPath)
echo "interceptor_via_fun:FileGetPath" > /tmp/asan_suppress.txt
ASAN_OPTIONS="detect_leaks=0:halt_on_error=1:suppressions=/tmp/asan_suppress.txt" \
  MILO_HEADLESS=1 MILO_NORENDER=1 MILO_FATAL_FAILS=0 \
  MILO_MAX_FRAMES=3000 MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
  native/build-asan/dc3-native

# Unit tests
cmake --build native/build --target milo-tests -- -j$(nproc)
native/build/milo-tests --gtest_filter='ObjectLifetimeTest.*'

# Single test (useful when others hang)
timeout 15 native/build/milo-tests --gtest_filter='ObjectLifetimeTest.SomeTest'
```

## Song Verification Loop

For character animation bugs, use a rendered headless run and capture screenshots after the intro. A logic-only `MILO_NORENDER=1` run is still useful for fast flow checks, but it is not enough to prove dancers are visually animating correctly.

```bash
mkdir -p /tmp/dc3_song_probe/shots

env DC3_DATA=orig-assets MILO_RENDER=1 MILO_HEADLESS=1 MILO_FATAL_FAILS=0 \
  DC3_SHOW_SPLASH=0 DC3_TEL=1 DC3_TEL_INTERVAL=25 \
  MILO_MAX_FRAMES=9050 MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
  MILO_SCREENSHOT_DIR=/tmp/dc3_song_probe/shots \
  MILO_SCREENSHOT_FRAMES=7750,8000,8450,8975 \
  timeout 180 native/build/dc3-native 2>&1 | tee /tmp/dc3_song_probe/run.log
```

Verification checklist:
- Create `MILO_SCREENSHOT_DIR` before launch. Screenshot capture can fail silently except for a stderr write error if the directory does not exist.
- Capture frames after the intro. On the current YMCA flow, `3000` frames is too short; `9050` reaches stable post-intro gameplay.
- Compare multiple screenshots, not just one. Camera motion can continue while dancers are frozen.
- Watch telemetry in `run.log`:
  - `mergeMoves=1`
  - `charClipLayers > 0`
  - `p0SongAnim > 0`
- `songAnimFrame` advancing by itself is not enough. We reproduced a real freeze where beats and camera advanced while clip layers dropped out.
- The native renderer also prints `=== BONE DIAG ===` blocks to stderr for the first few skinned meshes. Use those dumps to check local/world transforms, bind-pose offsets, and final skin matrices while the song is running.

## AddressSanitizer (ASan)

ASan is the most important tool for native debugging. Most native-port bugs are memory errors (use-after-free, double-free) that manifest as SIGSEGV on native but are silently tolerated on Xbox.

### Known Suppressions

| Error | Cause | Suppression |
|-------|-------|-------------|
| `strcpy-param-overlap` in `FileGetPath` | Self-copy to static buffer (`strcpy(static_path, static_path)`) | `interceptor_via_fun:FileGetPath` |

### ASan vs Normal Allocator Behavior

ASan catches different bugs than a normal crash run because the allocators behave differently:

| Scenario | Normal (glibc) | ASan |
|----------|---------------|------|
| Freed memory content | Zeroed or reused quickly | Quarantined — original content preserved |
| Null vtable on freed ObjRef | Yes (zeroed) → SIGSEGV at 0x28 | No (vtable survives in quarantine) → no crash |
| Double-free | May corrupt heap silently | Immediate `heap-use-after-free` report |
| Use-after-free via reused memory | Random crash or silent corruption | Immediate report with alloc/free stacks |

**Key insight**: If a bug crashes without ASan but NOT with ASan, the bug depends on allocator zeroing behavior. This typically means stale pointers to freed memory — the vtable is null under glibc but valid under ASan's quarantine. See the ObjRef ring corruption case study below.

### Reading ASan Output

ASan reports three stack traces:

1. **Access site** — where the invalid read/write happened
2. **Freed-by** — where the memory was deallocated
3. **Allocated-by** — where the memory was originally allocated

The freed-by stack is usually the most important — it tells you who deleted the object you're accessing.

## SIGSEGV Address Decoding

The crash address itself is diagnostic:

| Address | Meaning | Likely cause |
|---------|---------|--------------|
| `0x0` | Null pointer dereference | Missing null check |
| `0x28` | Null vtable + virtual slot 5 | Freed ObjRef in ring (Replace = vtable slot 5, 5×8=0x28 on x86_64) |
| `0x8`–`0x40` | Null pointer + small offset | Accessing member of null object |
| `0xfffffff8` | Corrupted allocator metadata | Double-free or heap corruption |
| `0x100000000` | 32-bit pointer in upper half | Corrupted pointer (e.g. stale `mTypeDef`) |
| Near-null but not zero | Freed memory, partially zeroed | Use-after-free with partial reuse |

## ObjRef Ring System

Most native-port crashes involve the ObjRef doubly-linked ring. Understanding it is essential.

### How Rings Work

Every `Hmx::Object` has an `ObjRef mRefs` sentinel. When an `ObjPtr`, `ObjDirPtr`, or `ObjPtrVec::Node` starts pointing to an object, it calls `AddRef` to link itself into that object's `mRefs` ring. When it stops pointing, it calls `Release` to unlink.

```
sentinel ←→ refA ←→ refB ←→ sentinel
```

`ReplaceRefs(newObj)` walks the ring and calls `Replace(newObj)` on each ref, redirecting them from the old object to the new one.

### Ring Corruption Symptoms

| Symptom | Root Cause |
|---------|-----------|
| SIGSEGV at 0x28 during `ReplaceList` | Freed ObjRef still linked in ring — null vtable |
| SIGSEGV at 0xfffffff8 | Freed ObjRef still linked — memory reused, garbage vtable |
| Infinite hang in `~ObjectDir → mSubDirs.clear()` | Self-looping ObjRef node (next = prev = this) |
| ASan `heap-use-after-free` in `FlowNode::~FlowNode` | Double-delete: parent deletes children, `DeleteObjects` also deletes them |

### Debugging Ring Issues

Add targeted logging to `ReplaceList` in `Object.cpp`:

```cpp
while (next != this) {
    ObjRef *cur = next;
    fprintf(stderr, "REPLLIST: cur=%p vtable=%p obj=%p\n",
        (void*)cur, *(void**)cur, (void*)obj);
    cur->Replace(obj);
}
```

Check for self-loops (indicates double-AddRef corruption):
```cpp
// In any ring walk
for (ObjRef *it = mRefs.next; it != &mRefs; it = it->next) {
    if (it->next == it) {
        fprintf(stderr, "SELF-LOOP detected: ref=%p\n", (void*)it);
        break;
    }
}
```

Use GDB hardware watchpoints to catch the moment a ring pointer is corrupted:
```
(gdb) watch -l obj->mRefs.next
```

### Native-Only Ring Infrastructure

The native port adds infrastructure around the Xbox ring to handle behavioral differences:

| Component | Purpose | Files |
|-----------|---------|-------|
| `SuppressEraseScope` | Prevents `ObjPtrVec::erase` during ring walks (would shift vector elements, invalidating prev/next pointers) | `Object.h`, `Object.cpp` |
| `gDeferredPurges` | Queues null-entry cleanup for after the walk | `Object.h`, `Object.cpp`, `ObjPtr_p.h` |
| `DirPtrRefCounts` | O(1) `HasDirPtrs()` check (replaces O(n) ring walk) | `Dir.h`, `Dir.cpp` |
| Vtable null-check in `ReplaceList` | Defense-in-depth against freed refs with zeroed vtable | `Object.cpp` |

**Do not remove these** — they solve real problems independent of any specific bug fix. The `SuppressEraseScope` in particular prevents vector element shifting from corrupting ring pointers during `ReplaceList` walks.

## Object Lifecycle & Destruction Order

### The Deletion Cascade

When an `ObjectDir` is destroyed (e.g. `UIPanel::Unload`), the destruction order is:

```
~PanelDir → ~RndDir → ~ObjectDir
  1. mSubDirs.clear()        — ObjDirPtr destructors → may delete subdirs
  2. delete mLoader
  3. DeleteObjects()          — ObjDirItr walks hash table, deletes each object
  4. DeleteSubDirs()          — iterates mSubDirs (already empty from step 1)
```

**Step 1** can trigger cascading deletes: `ObjDirPtr::operator=(nullptr)` checks `HasDirPtrs()`, and if the subdir has no remaining DirPtr refs, deletes it — which triggers its own `~ObjectDir` cascade.

**Step 3** deletes objects in hash table iteration order. If an object's destructor also deletes other objects (e.g. `FlowNode::~FlowNode` deletes child FlowNodes), those children are also in the hash table. `~Object::RemoveFromDir()` nulls the hash entry, and `ObjDirItr` skips nulls — but the ordering must be correct.

### Double-Ownership Pattern

Watch for classes that assume they own their members AND are in an ObjectDir:

```
ObjectDir::DeleteObjects()  → deletes parent FlowNode
FlowNode::~FlowNode()      → deletes child FlowNodes (also in hash table)
ObjectDir::DeleteObjects()  → encounters already-freed children → USE-AFTER-FREE
```

This is the FlowNode double-free pattern. `RemoveFromDir()` should null the hash entry before `DeleteObjects` reaches the child, but destruction ordering can break this.

### ObjDirPtr Delete-During-Cascade

`ObjDirPtr::operator=(nullptr)` in `Dir.h` can `delete mObject` when `HasDirPtrs()` returns false. This happens during:
- `~ObjDirPtr()` (destructor calls `*this = nullptr`)
- `mSubDirs.clear()` (vector destructor calls each element's destructor)
- `operator=(newDir)` (releases old target before assigning new)

The delete triggers a full destructor cascade. If the deleted ObjectDir has member ObjRefs in other objects' rings, those refs must be properly unlinked before their memory is freed.

## Environment Variables

| Variable | Description |
|----------|-------------|
| `MILO_HEADLESS=1` | No GLFW window, GPU renders to offscreen target |
| `MILO_RENDER=1` | Force GPU init (headless still renders, just no window) |
| `MILO_NORENDER=1` | Skip GPU entirely (logic-only, fastest) |
| `MILO_FATAL_FAILS=0` | Don't abort on non-critical failures |
| `MILO_MAX_FRAMES=N` | Exit after N frames |
| `MILO_INPUT_SCRIPT=path` | Scripted button presses (see below) |
| `MILO_SCREENSHOT_DIR=dir` | Directory for auto-captured PNGs |
| `MILO_SCREENSHOT_FRAMES=f1,f2,...` | Which frames to capture (default: `100,600,900,1500`) |
| `MILO_CLEAR_COLOR=R,G,B,A` | Override clear color (debugging) |
| `MILO_SIMPLE_RENDER` | Simplified material rendering |
| `MILO_DEBUG_PIPELINES` | Log pipeline creation |
| `MILO_NO_TRANSPARENT_DEFER` | Disable transparent draw queue |
| `MILO_PERF` | Enable frame timing |
| `MILO_VIDEO=path.mp4` | Record frames to video |
| `MILO_CAPTURE_FRAME=N` | GFXReconstruct capture at frame N |
| `MILO_TRACE_DELETE_OBJECTS=1` | Log each object deletion in `DeleteObjects()` |
| `MILO_DEBUG_MERGE=1` | Log merge operations (MergeObject source/target/action) |

## Scripted Input System

Input scripts drive the engine through menu screens. Two formats are supported:

**Absolute frame** (simple, but fragile if load times change):
```
60 start          # press Start at frame 60
200 confirm       # press A at frame 200
```

**Wait-for-screen** (robust, adapts to variable load times):
```
wait_screen main_screen
+30 confirm                # 30 frames after main_screen appears
wait_screen choose_mode_screen
+30 confirm
wait_screen song_select_screen
+50 down                   # scroll through songs
+100 down
```

### Button Names

| Name(s) | Milo Button | Xbox Equivalent |
|----------|-------------|-----------------|
| `start` | `kPad_Start` | Start |
| `confirm`, `a` | `kPad_X` | A |
| `cancel`, `b` | `kPad_Circle` | B |
| `x` | `kPad_Square` | X |
| `y` | `kPad_Tri` | Y |
| `up` | `kPad_DUp` | D-pad Up |
| `down` | `kPad_DDown` | D-pad Down |
| `left` | `kPad_DLeft` | D-pad Left |
| `right` | `kPad_DRight` | D-pad Right |
| `option`, `back`, `select` | `kPad_Select` | Back |
| `l1`, `lb` | `kPad_L1` | LB |
| `r1`, `rb` | `kPad_R1` | RB |
| `l2`, `lt` | `kPad_L2` | LT |
| `r2`, `rt` | `kPad_R2` | RT |
| `l3`, `ls` | `kPad_L3` | LS (left stick click) |
| `r3`, `rs` | `kPad_R3` | RS (right stick click) |

Note: Milo uses PlayStation-style internal names (`kPad_X` = confirm, `kPad_Circle` = cancel) but the Xbox 360 layout maps A to confirm and B to cancel. Buttons are pressed for exactly 1 frame (press on N, auto-release on N+1). Events don't need to be in order — the file is sorted by frame at load time.

### How It Works

In headless mode (`gNativeWindow == NULL`), `JoypadPoll()` reads from the script instead of GLFW. On first poll, `LoadInputScript()` reads and parses the `MILO_INPUT_SCRIPT` file. Each frame, `GetScriptedButtons()` returns a bitmask of buttons active on that frame. The existing delta computation (`mNewPressed`, `mNewReleased`) handles press/release edges. Windowed mode is unaffected — GLFW gamepad + keyboard fallback works as before.

Source: `native/src/platform/Joypad_Native.cpp`. Screenshot capture: `native/src/platform/Rnd_Wgpu.cpp`.

**Troubleshooting**: Create `MILO_SCREENSHOT_DIR` before launch (capture fails silently if missing). If scripted input doesn't work, verify frame numbers align with where the game actually is — the attract screen runs for a while before accepting input. In windowed mode, scripted input is disabled; use keyboard instead: arrow keys, Enter (confirm), Escape (cancel), Space (start), Tab (back).

### Available Input Flow Scripts

| Script | Route | Purpose |
|--------|-------|---------|
| `scripts/dc3-input-flows/song-scroll-test.txt` | boot -> main -> choose_mode -> song_select -> 8x down | Verify list scrolling |
| `scripts/dc3-input-flows/ymca.txt` | boot -> gameplay | Full song load test |

## Unit Tests

Object lifetime tests live in `native/tests/test_object_lifetime.cpp`. Key tests:

| Test | What it covers |
|------|---------------|
| `ReplaceListLiveWalkDoesNotCrash` | Ring walk with N refs, all redirect correctly |
| `DirPtrRefCountsConsistentAfterMerge` | DirPtrRefCounts survives MergeDirs |
| `ObjDirPtrCascadeDeleteDoesNotDoubleFree` | Nested subdir cascade with cross-references |
| `RemoveSubDirReleasesDirPtrRef` | Subdir removal + owner deletion |
| `ReplaceRefsWithSelfDeletingObjDirPtr` | ObjDirPtr that triggers self-delete during Replace |
| `DeletingFlowChildRemovesFromParent` | Flow child deletion updates parent's ObjPtrVec |
| `MergeDirsNameCollisionLeavesOnlyLivePointers` | Name collision during merge redirects refs |

Run individual tests with timeout (some may hang if ring bugs are present):
```bash
timeout 15 native/build/milo-tests --gtest_filter='ObjectLifetimeTest.SomeTest'
```

## Case Studies

### ObjDirPtr Double-AddRef (2026-03-18)

**Symptom**: Three symptoms from one root cause — SIGSEGV at 0x28, SIGSEGV at 0xfffffff8, destructor cascade hang.

**Root cause**: Decomp error in `ObjDirPtr(C*)` constructor called `dir->AddRef(this)` in the body, but the base class `ObjRefConcrete(dir)` already called it. The double-AddRef created a self-loop (`next = this, prev = this`), making `Release` a no-op. Ring sentinels permanently held dangling pointers to freed ObjDirPtrs.

**Diagnosis path**: Normal run crashed at 0x28. ASan showed zero errors (quarantine masked it). Adding vtable null-check confirmed freed refs in ring. Backtrace of immediate deletes showed `UIPanel::Unload` cascade. Removing the duplicate `AddRef` fixed all three symptoms and improved PPC match to 100%.

**Key lesson**: When ASan doesn't catch a crash that happens without ASan, the bug depends on allocator zeroing behavior. Check for stale pointers where the allocator zeroes the vtable.

Full writeup: `docs/sessions/2026-03-18-venue-merge-crash-ring-corruption.md`

### ObjDirItr Infinite Loop (2026-03-17)

**Symptom**: Game hung after ~1500 frames during song gameplay.

**Diagnosis**: CDP debugger break showed `ObjDirItr<RndLight>::Advance()` called from `WgpuRnd::WriteSceneUniforms()` — recursive dir iterator on venue WorldDir every frame.

**Fix**: Removed `ObjDirItr<RndLight>(venueDir, true)` scans from `WriteSceneUniforms()`. Environment light lists + fallback defaults are sufficient.

### FlowNode Double-Free (2026-03-18)

**Symptom**: ASan `heap-use-after-free` in `FlowNode::~FlowNode()` at `delete cur`.

**Root cause**: `FlowNode::~FlowNode()` recursively deletes child FlowNodes via `mChildNodes`. But all FlowNodes are also in the ObjectDir hash table. `ObjectDir::DeleteObjects()` iterates the hash table and deletes every object. When it deletes a parent FlowNode, the destructor cascade deletes children. Then `DeleteObjects` encounters those already-freed children.

**Diagnosis**: ASan freed-by stack showed `FlowIf::operator delete` called from `FlowNode::~FlowNode` (parent deleting child). The crash-site stack showed the same `FlowNode::~FlowNode` at a different level trying to access the freed child.

Full writeup: passed to investigating agent (see analysis in conversation history).

## Sandbox Notes

GPU access requires `dangerouslyDisableSandbox: true`. The Vulkan ICD loader needs filesystem access outside the sandbox allowlist. Non-GPU runs (with `MILO_NORENDER=1`) work within the sandbox.
