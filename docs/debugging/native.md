# Native Build Debugging

Techniques for debugging the DC3 native Linux port — memory errors, object lifecycle issues, and crash diagnosis.

## Quick Reference

```bash
# Agent testing — HTTP server + fast boot + telemetry (see docs/tools/HTTP_DEBUG_SERVER.md)
scripts/dc3-agent-test.sh --headless

# Standard headless run (logic only, fastest)
# Good for boot/flow smoke tests. This is NOT long enough to verify post-intro gameplay animation.
# (Fails are non-fatal by default; MILO_FATAL_FAILS=1 makes them abort.)
MILO_HEADLESS=1 MILO_NORENDER=1 \
  MILO_MAX_FRAMES=3000 MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
  native/build/dc3-native

# ASan build (catches use-after-free, double-free, buffer overflow).
# native_configure.sh finds Dawn/clang; a bare cmake does not.
# ulimit -d: see "Running the gameplay routes under ASan" below.
ulimit -d unlimited
scripts/native_configure.sh "$PWD/native/build-asan" -DENABLE_ASAN=ON \
  -DMILO_ENGINE_ENABLE_ASAN=ON \
  -DCMAKE_C_FLAGS=-fsanitize-recover=address -DCMAKE_CXX_FLAGS=-fsanitize-recover=address
ninja -C native/build-asan dc3-native milo-tests

# Run with ASan (suppress the benign strcpy overlap in FileGetPath)
echo "interceptor_via_fun:FileGetPath" > /tmp/asan_suppress.txt
ASAN_OPTIONS="detect_leaks=0:halt_on_error=1:suppressions=/tmp/asan_suppress.txt" \
  MILO_HEADLESS=1 MILO_NORENDER=1 \
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

env DC3_DATA=orig-assets MILO_HEADLESS=1 \
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

### Known allocator-convention reports (not suppressible by file)

An ASan suppressions file cannot name `alloc-dealloc-mismatch` (it only takes
`interceptor_*` and `odr_violation`), so these show up in every gameplay run.
Both are benign on this toolchain: libstdc++'s `operator new(size_t)` is
`malloc` and `operator delete(void*)` is `free`. On the 360 both sides went
through MemMgr, so they matched there by construction. Silence the whole class
with `alloc_dealloc_mismatch=0` only when you are sure you are not hunting it.

| Report | Where | Why it is benign |
|--------|-------|------------------|
| `malloc vs operator delete` in `array_list_free` / `json_object_generic_delete` (json-c) | `DingoJob::~DingoJob` -> `JsonArray` teardown, once per `MetaPerformer::SendOmgDatapoint` | json-c allocates with `JsonCalloc` (MemAlloc, `malloc` under HX_NATIVE) and frees with `::operator delete`, as the image does |
| `operator new vs free` in `ObjectDir::ReleaseCascadeBlock` | `HamDirector::UnloadMergers` -> `~MoveDir` -> `FlushDeferredFrees` | native cascade frees every object block with `free()`; classes without `OBJ_MEM_OVERLOAD` (e.g. `MoveGraph`) came from global `operator new` |

### Running the gameplay routes under ASan

`scripts/native_assert_harvest.py --binary <asan dc3-native>` drives the
perform / battle / practice / party routes against an ASan build. Two things
are needed that a normal run is not:

- **`ulimit -d unlimited`.** This box's default `RLIMIT_DATA` is 32 GB, and
  ASan's 15 TB `MAP_NORESERVE` shadow reservation counts against it: every
  ASan binary dies at startup with `ReserveShadowMemoryRange failed ... ulimit
  -v or ulimit -d`. This includes `milo-tests`' POST_BUILD `gtest_discover_tests`
  step, so even the *build* fails without it.
- **Recover mode, to get more than one report per run.** `halt_on_error=0` is
  ignored unless the code was compiled with `-fsanitize-recover=address`; pass
  `-DCMAKE_C_FLAGS=-fsanitize-recover=address -DCMAKE_CXX_FLAGS=...` at
  configure time. Pass `-DMILO_ENGINE_ENABLE_ASAN=ON` as well (`ENABLE_ASAN`'s
  `add_compile_options` already reaches the engine subdirectory, but say so).

**ASan also changes UNINITIALISED reads.** Its allocator fills every new block
with `0xbe` (`malloc_fill_byte`, first `max_malloc_fill_size` = 4 KB), where
glibc tends to hand a young process zeroed pages. A member the image never
initialises -- because retail code always assigns it later, and a native arm
skips that assignment -- reads as null under glibc and as `0xbebebebe...`
under ASan, so the first dereference is a SIGSEGV with `si_addr = 0`
(non-canonical address), caught by the engine's own handler rather than
reported by ASan. Raise `max_malloc_fill_size` to widen the net.

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

| Component | Purpose | Files (under `src/system/`) |
|-----------|---------|-------|
| `gInReplaceList` | Set for the duration of `ObjRef::ReplaceList` and `Hmx::Object::ReplaceRefs`. While it is set, `ObjPtrVec::ReplaceNode` and `CharClip`'s `Transitions` do not erase (an erase shifts the following nodes via `CopyRef` and rewrites ring `prev`/`next` under the walk) | `obj/Object.h`, `obj/Object.cpp`, `obj/ObjPtr_p.h`, `char/CharClip.cpp` |
| `gInRefSnapshot` + `DeferVecCompact` | `ReplaceRefs` walks a snapshot of the ring (`SnapshotRing`). In that walk an `ObjPtrList` erases as the image does; a `kObjListNoNull` `ObjPtrVec` (or `Transitions`) that loses a node *defers* its erase until the outermost walk returns (`FlushPendingVecCompacts`), so it ends in the image's state | `obj/Object.h`, `obj/Object.cpp`, `obj/ObjPtr_p.h` |
| `DirPtrRefCounts` | O(1) `HasDirPtrs()` check (replaces an O(n) ring walk) | `obj/Dir.h`, `obj/Dir.cpp` |
| Force-unlink in `ObjRef::ReplaceList` | Keeps the image's `ReplaceList stuck in infinite loop` assert, then unlinks the non-advancing ref so a native (non-fatal) build does not spin | `obj/Object.cpp` |

**Do not remove these** — they solve real problems independent of any specific bug fix. Two opt-in diagnostics sit next to them: `DC3_REFRING_AUDIT=1` (shadow index of every ref; logs `REFAUDIT` when a ref is left pointing at a freed object) and `DC3_POISON_FREED_OBJECTS=1` (cascade-freed object blocks are filled with `0xDD` and never reused; memory grows without bound).

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

This is the master table for the native port's runtime switches. Each row was
checked against its read site on 2026-11-13. "Presence" means the variable is
tested with `getenv(...) != nullptr`, so **any value, including `0`, turns it
on** — unset it to turn it off. Paths starting `../milo-native-engine/` are the
shared engine, which is what every native target links (dc3's own copies of
`Rnd_Wgpu.cpp`, `AudioDevice.cpp`, `Joypad_Native.cpp` etc. under
`native/src/` are not compiled into any buildable target).

To re-derive the full set, including the one-off probes this table leaves out:

```bash
grep -rhoE '(getenv|Dc3EnvFlag)\s*\(\s*"[A-Z0-9_]+"' native/src src ../milo-native-engine/src \
  | grep -oE '"[A-Z0-9_]+"' | tr -d '"' | sort -u
```

The engine also keeps a classified registry of its own flags in
`../milo-native-engine/src/platform/NativeCompatFlags.classification.json`.

### Profiles (`DC3_PROFILE`)

`DC3_PROFILE=dev` (the default; unset, empty or an unknown value is dev) or
`release`. A profile is only a set of default **values** for ordinary variables.
`Dc3ProfileApply()` runs first thing in `main()` (native and web; table
`kDc3DevDefaults` in `native/src/platform/System_Native.cpp`, the one place a
dev default lives). Under dev it sets each row that is unset or empty. **A
variable that is already set always wins** (`DC3_HTTP=0` turns the server off
under dev, `DC3_DEV_OPTIONS=1` shows the rows under release). `release` sets
nothing, so it is the player build: each variable's compiled default, as before
the profile existed. Web: `?profile=release`, or `?env=DC3_PROFILE=release`.
Startup prints `DC3 Native: profile=dev (...)` and the rows it applied (for
scripts), then a boxed banner for a person: one `| on` line per row with its
switch (`off: DC3_HTTP=0`), `| off` for a row you turned off, the main opt-in
switches, and `DC3_PROFILE=release turns all of it off`. Release prints
`profile=release` and no banner.

| Variable | What it gives you | Before | dev | release | Why on in dev |
|---|---|---|---|---|---|
| `DC3_DEV_OPTIONS` | Options → Gameplay's native rows (Camera Blend, Debug Overlay) | off | `1` | off | Owner's ask; the rows are appended, retail rows don't move |
| `DC3_DEV_MENU` | The original debug build's dev menu (`{ui show_dev_menu}`): 9 extra modes in Choose Mode, extra campaign difficulty rows. On the 360 this is Alt+D, which native cannot send | off | `1` | off | The original's own dev path. Rows appended; `{ui toggle_dev_menu}` flips it at run time |
| `DC3_RND_OVERLAYS` | The original's `RndOverlay`s (`cheat_display` notices, the framerate/stats/timer quick cheats) in a corner window, plus a `DC3 OVERLAY [<name>] <text>` stderr line when one changes. `WgpuRnd::EndDrawing` never drew them | off (invisible) | `1` | off | The debug build shows them; a notice nobody can see is useless |
| `DC3_HTTP` | HTTP debug server on **127.0.0.1 only** | off | `1` (desktop) | off | `curl` state, eval DTA, screenshot; loopback, so nothing is exposed |
| `DC3_TEL` | Gameplay telemetry. `api`: sampled for `GET /api/telemetry`, no stderr lines | off | `api` (desktop) | off | The API is free; the ~2 KB-per-10-frames lines are for scripts (`DC3_TEL=1`) |
| `DC3_TAG_MODALS` | `dev`: every `MILO_WARN/NOTIFY/FAIL/ASSERT` as one `MILO_<KIND>:` line, rate-limited (first 5, then powers of ten), no backtraces | off | `dev` (desktop) | off | See what the game complains about; `=1` is still the harvest mode |
| `MILO_INPUT_LOG` | One `Joypad: down/up ...` line per button message | off | `1` | off | Shows what a key or pad press really sent |
| `DC3_MOVE_RATING_LOG` | One `DC3 MOVE_RATING` line per scored move | off | `1` | off | Scoring is visible while you dance |
| `MILO_PERF` | A `[PERF]` fps/frame-time line every 5 s | off | `1` | off | Cheap; presence-tested, so `MILO_PERF=0` is unset by the profile and reads as off |
| `DC3_DLC` | Every DLC package under `<data>/dlc` (`orig-assets/dlc`, made by `scripts/dlc/import.sh`) is mounted; `DC3_DLC_DIR` names other roots and wins; `DC3_DLC=0` is off ([DLC](../native/DLC.md)) | off | `1` (desktop) | off | A 360 lists the songs of every package installed on it, so this is the original's behaviour for the content present. Flows pick songs by name (`highlight`), and ctest pins `DC3_DLC=0`, so tests see the disc list on every box. No `<data>/dlc` = no change |

"Desktop" rows are not applied on web: there is no debug server there, and the
web tap is already on (`DC3_TAG_MODALS` defaults to `1` on web, and its parser
would misread `dev`).

Already on under both profiles (no change): `DC3_CONTROLLER_MODE=auto`, the
splash, `DC3_HEARTBEAT_S=60`, `DC3_NATIVE_SCORING`, `DC3_REAL_MOVE_PASSED`,
web `DC3_TAG_MODALS`, the original's `notify_level 1` and `disable_cheats
FALSE` (so the gamepad quick cheats, LB+LT / RB+RT + a button, should be live: inferred from the DTA, not exercised natively).

**Opt-in under every profile.** A row must not move the game away from the
original in a way that invalidates testing it, or cost real time
(`Dc3ProfileTest.NoRowChangesTheGameOrCostsTime` holds the list):

| Variable | Why it stays opt-in |
|---|---|
| `DC3_FAST_BOOT` | Not a dev default: it advances past `title_screen` without the confirm that sets the initial profiles and `$post_load_dest_screen`, and skips the voice-control first-run tutorial (`src/system/ui/UI.cpp`, the `sBoot` table; read from the code, 2026-11-14), so a playtester would never see the profile setup and first-run prompts |
| `DC3_FAST_TIME`, `DC3_POSE_SELFTEST`, `DC3_POSE_PERFORM`, `DC3_DUMMY_PLAYERS` | Change timing or fake a player |
| `DC3_POSE_LATENCY_MS` and the pose knobs | Owned by the tracking lane |
| `MILO_FATAL_FAILS`, `MILO_MAX_FRAMES` | Change how a run ends |
| `DC3_REFRING_AUDIT`, `DC3_POISON_FREED_OBJECTS`, `DC3_STUB_TRACE`, `DC3_DRAW_CRASH_GUARD` | Real cost or changed behaviour |
| `DC3_SCORING_DEBUG`, `MILO_DEBUG_UI_FLOW`, `DC3_TEL=1` | Close calls: per-second or per-frame floods |
| `MILO_CAM_DEBUG`, the camera-blend enhancement, the ImGui panel open at boot | Change the picture; the panel is one `` ` `` away |
| Unlock-all (`cheats.dta` `a`) | Its `HasCheated` side effect turns off autosave (removed as a default in `3126df522`) |
| Keyboard cheats (Alt+D, `a`, ...) | Not reachable natively at all (`EnableKeyCheats` is `#ifndef HX_NATIVE`, and no GLFW key callback feeds Milo key messages); plain-key cheats would also clash with the keyboard-as-pad map. PLAN H-22 |

Tests: `Dc3ProfileTest.*` (in-process, the table and the apply rules),
`HeadlessBootTest.DevProfileIsTheDefault` / `ReleaseProfileIsThePlayerBuild` (a
real `dc3-native` under each profile), `OptionsGameplayFlowTest` (dev, 6 rows)
and `OptionsGameplayPlayerFlowTest` (`DC3_PROFILE=release`, 4 rows).
`milo-tests` itself never applies a profile, so an in-process test sees the
compiled defaults (= release); the `dc3-native` processes it starts do apply
it, so a test that asserts a player-facing default pins `DC3_PROFILE=release`.
`MILO_TEST_ENV` sets `DC3_HTTP_PORT=0`.

### Run control

| Variable | Default | Effect | Read in |
|----------|---------|--------|---------|
| `MILO_HEADLESS` | unset | Presence. No GLFW window; the GPU renders offscreen. Also skips audio output (see `MILO_AUDIO`) and, unless `DC3_POSE` or a valid `DC3_POSE_PERFORM` mode is set, the pose provider (a dummy skeleton is used; an unknown `DC3_POSE_PERFORM` value is named in the log) | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp`, `../milo-native-engine/src/audio/AudioDevice.cpp`, `native/src/platform/GestureMgr_Native.cpp` |
| `MILO_NORENDER` | unset | Presence. No GPU at all (logic only, fastest). GPU rendering is **on by default** | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp` |
| `MILO_RENDER` | unset | Legacy. Presence forces GPU init even over `MILO_NORENDER` — note `MILO_RENDER=0` also forces it on | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp` |
| `MILO_WIDTH` / `MILO_HEIGHT` | `1280` / `720` | Render target size | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp` |
| `MILO_MAX_FRAMES` | `10000` | Exit after N frames. `0` or negative = unlimited | `src/App.cpp` |
| `DC3_FPS_CAP` | windowed `60`, headless off | Frame cap (PLAN H-29). The image never draws more than 60 fps (`rnd sync 1` → `D3DRS_PRESENTINTERVAL 1`), so a windowed run sleeps out the rest of each frame instead of drawing at the panel's rate. The cap is snapped to a whole number of refreshes so every frame is held equally long: 72 on a 144 Hz panel, 60 on 120/240 Hz, 82.5 on 165 Hz; at 60/75 Hz vsync alone paces and the cap is off. `0`/`off` = no cap (display rate). `N` = cap at N (windowed: snapped the same way). **Headless runs are never capped unless this is set** (flow tests depend on running faster than real time); headless `N` caps at exactly N. The log names the decision (`DC3 Native: frame cap: …`) and, at exit, how many frames were paced. Desktop only; web is paced by `requestAnimationFrame` | `src/App.cpp`, `native/src/platform/FramePacer_Native.cpp` |
| `MILO_FATAL_FAILS` | `0` (non-fatal) | `MILO_FAIL`/`MILO_ASSERT` print `FAIL:` and continue, like the 360's "Continue" dialog. Non-zero = `abort()`. A fail inside `MILO_TRY` throws to the `MILO_CATCH` regardless, as in the image (this includes `MILO_FAIL_DTA`) | `src/system/os/Debug.cpp` |
| `DC3_DATA` | search | Game data root (must contain `gen/main_xbox.hdr`). Unset: tries `orig-assets`, `../orig-assets`, `../../orig-assets`, `.` | `native/src/platform/System_Native.cpp` |
| `DC3_DLC` | unset (dev profile: `1`) | `1`: mount every package under `<data>/dlc` when that directory exists; `0`: none. `DC3_DLC_DIR` wins over it. ctest sets `0` | `native/src/platform/ContentMgr_Native.cpp` (`NativeDlcRoots`) |
| `DC3_DLC_DIR` | unset | DLC roots, `:`-separated (set but empty: none, whatever `DC3_DLC` says; `play.sh --no-dlc`); every subdirectory is one extracted package (`scripts/dlc/import.sh`, lowercase paths), mounted on the 360's `cntNNNNNNNN:` drive. See [DLC.md](../native/DLC.md) | `native/src/platform/ContentMgr_Native.cpp`, engine `File_Native.cpp` |
| `DC3_CACHE_DIR` | `$HOME/.local/share/dc3/cache` | Native cache backend root (song cache etc.) | `native/src/platform/Cache_Stub.cpp` |
| `DC3_SAVE_DIR` | `$HOME/.local/share/dc3/saves` | Memory-card / save directory | `native/src/platform/MemcardMgr_Stub.cpp` |
| `DC3_FAST_BOOT` | `0` | Non-zero: the boot screens (attract → autosave warning → title → main) are force-advanced after 10 frames each. Off: no forced advance; the screens progress as the image's flow drives them. Web: `?fast_boot=1` | `src/system/ui/UI.cpp` |
| `DC3_SHOW_SPLASH` | shown | `0` skips the splash. Never shown on web | `src/App.cpp` |
| `DC3_PROFILE` | `dev` | `dev` or `release`: the defaults of the rows in [Profiles](#profiles-dc3_profile) | `native/src/platform/System_Native.cpp` |
| `DC3_DEV_OPTIONS` | dev profile `1`, release off | `Dc3EnvFlag`. Shows the native overlay's developer rows (Camera Blend, the debug panel) in Options → Gameplay; off, the page has the retail rows only (`{native_settings dev_options}`, PLAN G-20) | `src/App.cpp`, `native/dta/ui/options/options_gameplay.dta` |
| `DC3_FAST_TIME` | unset | Presence. Song time advances per frame instead of on the wall clock (see [Fast Time Mode](#fast-time-mode)) | `src/lazer/game/LiveInput.cpp` |
| `DC3_HEARTBEAT_S` | `60` | Seconds between `DC3_HEARTBEAT: uptime_s=… rss_kb=…` lines on stderr; `0` disables | `native/src/main_native.cpp` |
| `DC3_TAG_MODALS` | desktop: dev profile `dev`, release `0`; **web `1`** | Every `MILO_WARN`/`MILO_NOTIFY` (desktop also `MILO_FAIL`) printed as one `MILO_<KIND>: <msg>` line. Desktop `1` (the harvest mode, `scripts/native_assert_harvest.py`): stderr, every line, plus a backtrace for each new message. Desktop `dev`: stderr, each distinct message's first 5 then every power of ten with `[repeat N]`, no backtraces. Web: the browser devtools console only (`console.warn`, not the page's console), each distinct message's first 5 repeats then every power of ten (W-9); `?env=DC3_TAG_MODALS=0` turns it off. Without it a notify at `notify_level` 1 goes to `cheat_display` and is never printed | `src/system/os/Debug.cpp` |

### Stopping the engine (H-9)

Every exit path writes one `DC3_EXIT: how=… detail=… code=…` line.

| What | Result |
|---|---|
| Window close (window manager's close button, `WM_DELETE_WINDOW`) | loop ends: `how=main_loop_returned detail=window_closed`, rc 0 |
| **Ctrl+Q** in the window (G-5) | a quit request, like a signal but a natural end: the loop ends at the next frame boundary, `detail=quit_key`, rc 0. Registered by `main_native.cpp` through the engine's `JoypadSetQuitKeyHandler`. Esc is the B button, not quit |
| Window destroyed by another client (`xdotool windowclose`), or a `BadWindow` on it | loop ends before drawing: `detail=window_lost`, rc 0 (`native/src/platform/WindowLoss_X11.cpp`) |
| `SIGTERM` / `SIGINT` / `SIGHUP` | a quit request: the loop ends at the next frame boundary, so a save being written in that frame completes; `detail=SIGTERM` (etc.), rc 128+sig. A second signal, or 5 s without reaching the boundary, stops at once (`how=terminating_signal`) |
| `SIGQUIT`, a fatal signal | immediate, rc 128+sig |
| The HTTP debug server's port cannot be bound (`DC3_HTTP_PORT` held by another process) | `DC3 Native: FATAL - HTTP debug server: port N already in use (DC3_HTTP_PORT=N)`, `how=exit_runtime detail=http_port_in_use`, **rc 98** (`http_port_bind_failed` for another bind error, `http_port_file_unwritable` for `DC3_HTTP_PORT_FILE`) |
| `exit()` from anywhere (DTA `{exit}`, `Debug::Exit` → `XLaunchNewImage`, Xlib's default error handlers) | `how=exit_runtime` with exit()'s own code; the static destructors are skipped, as the main loop's return always did (they crash in `ObjDirPtr<ObjectDir>::operator=`) |

Tests: `NativeExitTest.*` (runs its own Xvfb; no window manager needed).

### Input

| Variable | Default | Effect | Read in |
|----------|---------|--------|---------|
| `MILO_INPUT_SCRIPT` | unset | Scripted button presses, used when there is no window (see [Scripted Input System](#scripted-input-system)) | `../milo-native-engine/src/platform/Joypad_Native.cpp` |
| `MILO_INPUT_WAKE_BUTTON` | `l3` | Button a script `wake` presses | `../milo-native-engine/src/platform/Joypad_Native.cpp` |
| `MILO_INPUT_LOG` | dev profile `1`, release unset | Non-zero: one `Joypad: down pad=0 button=7 (X) action=14` / `Joypad: up ...` line per button message, any source (keyboard, gamepad, script, HTTP) | `../milo-native-engine/src/platform/Joypad_Native.cpp` |
| `DC3_CONTROLLER_MODE` | `auto` | `auto`, `forced` or `faithful` (see [Controller Mode Policy](#controller-mode-policy)) | `native/src/platform/System_Native.cpp` |
| `DC3_CONTROLLER_MODE_TIMEOUT_MS` | helpbar value | faithful policy only: overrides the helpbar's `controller_mode_timeout` | `native/src/platform/System_Native.cpp` |
| `DC3_HTTP_PACE_FPS` | unset (unpaced) | Cap the frame rate of a debug-server run, so a client answering every frame (the synthetic performer on `/api/frame/wait`) keeps up; `PUT /api/pace?fps=N` changes it at run time | `native/src/platform/HttpServer.cpp` |
| `DC3_MESHFILTER_LOG` / `DC3_MESHFILTER_ALLOW` | unset | Log each mesh `MeshFilter` refuses (once, with its rule) / draw what the named rules (or `all`) would refuse | `native/src/platform/MeshFilter.cpp` |

### Scoring and pose

Full semantics (value parsing, provider selection, expected scores) are in
[native/SCORING_ENV_VARS.md](../native/SCORING_ENV_VARS.md). `Dc3EnvFlag`
variables are value-parsed: `0`/`false`/`off`/`no` disable.

| Variable | Default | Effect | Read in |
|----------|---------|--------|---------|
| `DC3_NATIVE_SCORING` | on | `Dc3EnvFlag`. Per-frame gesture poll and the sensor feed | `native/src/platform/GestureMgr_Native.cpp`, `src/App.cpp` |
| `DC3_REAL_MOVE_PASSED` | on | `Dc3EnvFlag`. The image's `move_passed` path | `src/lazer/game/Game.cpp` |
| `DC3_POSE_SELFTEST` | off | `Dc3EnvFlag`. Feed the choreography's own reference pose as the player | `native/src/platform/GestureMgr_Native.cpp`, `src/App.cpp`, `src/system/hamobj/FilterQueue.cpp` |
| `DC3_SCORING_DEBUG` | off | `Dc3EnvFlag`. Once-per-second scoring liveness counters | `native/src/platform/GestureMgr_Native.cpp`, `src/lazer/game/Game.cpp` |
| `DC3_MOVE_RATING_LOG` | dev profile `1`, release off | `Dc3EnvFlag`. One `DC3 MOVE_RATING` line per scored move | `src/lazer/game/Game.cpp` |
| `DC3_POSE` | unset | Provider select. `off`: no pose server, the stand-in skeleton. Any other value (conventionally `external`) only overrides the headless skip: in headless runs the pose provider is skipped unless this or a valid `DC3_POSE_PERFORM` mode is set | `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_POSE_CAMERA` | `auto` | Camera for the spawned pose server, passed as `--camera`: `auto` = first V4L2 video-capture device, re-resolved whenever the server reopens it; or an index (falls back to `auto` if that node is missing or not a camera), a `/dev/videoN` path, or a `/dev/v4l/by-id` name. See POSE_INPUT.md "Which camera" | `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_POSE_MODEL` | `<tree>/native/models/pose_landmarker_full.task`, found from the executable, not the cwd | MediaPipe model passed to the spawned server | `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_DUMMY_PLAYERS` | auto | `1` or `2`: how many standing stand-ins the sensor shows when no pose provider runs. Unset: one, two in two-player modes (`requires_2_players` / `is_in_party_mode`). Ignored while a provider runs | `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_POSE_SOCKET` | spawned server: `/tmp/dc3_pose_<pid>.sock`; with `DC3_POSE_NO_SPAWN`: `/tmp/dc3_pose.sock` | Unix socket shared with the pose server. Per process when the engine spawns the server, since `pose_server.py` unlinks whatever is at its path | `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_POSE_NO_SPAWN` | unset | Presence. Connect to an already-running pose server instead of forking `pose_server.py` | `native/src/platform/Skeleton_Native.cpp` |
| `DC3_POSE_HFOV` | unset | Horizontal camera FOV passed to the spawned server | `native/src/platform/Skeleton_Native.cpp` |
| `DC3_POSE_HOLD_MS` | `1000` | Dropout hold (PLAN P-12): a person the detector loses is still reported, same slot and tracking id, every joint inferred at its last pose, for this much sensor time; then the slot is released and the image's own 1 s recovery window runs. `0` = off (lost at once). Not applied to someone seen under 500 ms, or when a new id appears within 0.3 m. Capped at 5000 | `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_POSE_FILL` | `extrap` | Frames between a slow camera's (POSE_INPUT.md "Frame fill"). Engaged only while camera frames arrive under 24 a second of song time (released over 27), so a 30 Hz camera is untouched. `extrap` (default, **deliberately generous**, owner decision 2026-11-14): each camera frame at once, then a frame every 1/30 s stepped on by its joints' velocity x `DC3_POSE_FILL_GAIN` for at most one camera period; a guess (not-tracked joint) is not moved, joint speed is clamped to 4 m/s, a step that looks like a left/right swap is held, nothing after a frame with nobody, no velocity across a gap. Scores 1.05-1.10 of a real 30 Hz camera at 8-15 Hz on the footage. `interp`: the conservative mode, a lerp between camera frames with the stream 1.2 camera periods behind and that delay added to the scorer's latency (0.92 at 8 Hz, never above 30 Hz). `off` / `0`: camera frames only. `hold`, `extrapc`: measured alternatives. Any other value: the default, with a warning. Logs `DC3 POSE FILL: extrap (gain G) engaged at song S s (camera N frames/s, stream delay D ms …)`. Fill frames are never written by `DC3_POSE_RECORD` and are not counted in `pose_fps` | `native/src/platform/GestureMgr_Native.cpp`, `native/src/platform/PoseFill_Native.cpp` |
| `DC3_POSE_FILL_GAIN` | `1.0` | `extrap`'s strength, 0 (a hold for one period) .. 1 (the full linear step); outside [0, 1] or unparsable: the default, with a warning. The calibration knob: at 8 Hz gain 0.75 scores 0.99 of a real 30 Hz camera, at 15 Hz gain 0.5 scores 1.00 (score-vs-gain table in POSE_INPUT.md "Calibrating DC3_POSE_FILL_GAIN") | `native/src/platform/PoseFill_Native.cpp` |
| `DC3_POSE_LATENCY_LOG` | off | `Dc3EnvFlag`. Every 5 s, `DC3 POSE AGE: capture->game median/p10/p90`: the age of a pose frame when the game takes it, from `pose_server.py`'s capture stamp (desktop only) | `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_POSE_PYTHON` | the game chooses | Interpreter the spawned pose server runs under. Unset, the game takes the first executable of: `${DC3_POSE_VENV:-~/.local/share/dc3/pose-venv}/bin/python3` (what `scripts/playtest/bootstrap.sh --fix` makes), the checkout's `.venv/bin/python3`, a worktree's main-checkout `.venv/bin/python3`, then `python3` from PATH. It logs `Pose server python: PATH (why)`; when that Python lacks mediapipe/opencv the notice names it and `bootstrap.sh --fix`. `scripts/playtest/pose_python.py` is the same order (preflight checks the game's choice) | `native/src/platform/Skeleton_Native.cpp` (`ChoosePosePython`) |
| `DC3_POSE_VENV` | `~/.local/share/dc3/pose-venv` | The playtest venv the game looks for (step 2 above); replaces the default rather than adding to it | `Skeleton_Native.cpp` (`ChoosePosePython`), `scripts/playtest/bootstrap.sh` |
| `DC3_POSE_SERVER` | `<tree>/native/scripts/pose_server.py` | A drop-in for `pose_server.py`, started with the same arguments (`scripts/playtest/footage_camera.py` is one) | `native/src/platform/Skeleton_Native.cpp` |
| `DC3_POSE_VIDEO` | unset | The spawned server reads this video file (looped) instead of `--camera N` (`scripts/play.sh --video`) | `native/src/platform/Skeleton_Native.cpp` (`ServerCommand`) |
| `DC3_POSE_RECORD` | unset | Write every sensor frame the game accepts, as the scorer receives it (persons, frame id, timestamp, song time, floor normal), to this dc3-pose-session v1 file; flushed per frame. `scripts/play.sh` sets it to `<session>/pose.dc3session` | `native/src/platform/PoseSession_Native.cpp` |
| `DC3_POSE_SESSION_SYNC` | `song` | With `DC3_POSE_PERFORM=session:FILE`: `song` publishes each recorded frame at its recorded song time (and uses its recorded floor normal); `wall` at its recorded wall offset | `native/src/platform/PoseSession_Native.cpp` |
| `DC3_PERF_LOG` | off | Every 5 s: `DC3 PERF: fps= pose_fps= persons=` (game frames/s, sensor frames accepted/s, mean people) | `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_POSE_STATS` | off | Read by `pose_server.py`: every 5 s `POSE_STATS: fps= infer_ms_mean= infer_ms_p95= persons= sent= dropped=` on stderr | `native/scripts/pose_server.py` |
| `DC3_FOOTAGE_PRE`, `DC3_FOOTAGE_CONTROL` | `8`, unset | `footage_camera.py`: seconds of clip before song time 0; a file holding `CLIP PRE`, re-read every 0.5 s (a driver changes the dancer between songs) | `scripts/playtest/footage_camera.py` |

### Capture and output

| Variable | Default | Effect | Read in |
|----------|---------|--------|---------|
| `MILO_SCREENSHOT_DIR` | unset | Auto-capture PNGs into this directory (create it first — a missing directory fails with only a stderr write error) | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp` |
| `MILO_SCREENSHOT_FRAMES` | `100,600,900,1500` | Comma-separated frames to capture | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp` |
| `MILO_VIDEO` | unset | Record frames to this video file | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp` |
| `MILO_VIDEO_FPS` | `30` | Frame rate for `MILO_VIDEO` | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp` |
| `MILO_CAPTURE_FRAME` | unset | Arm a GPU frame capture at frame N (see [native/TOOLS.md](../native/TOOLS.md)) | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp` |
| `MILO_FRAME_TIMES` | unset | Write a per-frame timing CSV to this path | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp` |
| `DC3_CROWD_STATS` | unset | `N`: every N render frames, one `DC3_CROWD: frame=… draws3d=… impostors=… pose=…` line on stderr (3D crowd draws, impostor renders, checksum of the drawn crowd poses). Draws nothing differently; read by `CrowdTest` | `src/system/world/Crowd.cpp` |
| `MILO_PERF` | dev profile `1`, release unset | Presence (the profile unsets `MILO_PERF=0`, so that reads as off). A `[PERF]` frame-timing line every 5 s | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp` |
| `DC3_MEM_LOG` | unset | Presence. At every UI screen Enter, one `DC3 MEM <screen> rss=…MB inuse=…MB heldfree=…MB mmap=…MB objects=<live Hmx::Objects> created=<ever> alloc=glibc\|jemalloc gen=<n>` line on stderr (glibc `mallinfo2`, or jemalloc's `stats.*` when it is preloaded), plus a `DC3 MEM trim …` line each time `NativeReleaseFreeHeap` gives pages back. `MemoryFlatSongOverSongTest` reads it (PLAN H-24) | `src/system/obj/Object.cpp` (`NativeMemReport`), called from `src/system/ui/UIScreen.cpp` |
| `DC3_MEM_CENSUS` | unset | Presence (with `DC3_MEM_LOG`). Keeps a registry of every live `Hmx::Object` and, at each report, prints `DC3 MEM   class <Name> live=N delta=±D` for every class whose count moved. Costs a hash insert per object; off, the counters are two integers | `src/system/obj/Object.cpp` |
| `DC3_MEM_CENSUS_BT` | unset | Presence. The census also records a 14-frame creation backtrace per object (slow) | `src/system/obj/Object.cpp` |
| `DC3_MEM_CENSUS_GEN` / `DC3_MEM_CENSUS_GEN_AT` | unset | `G` / `N`: at report number `N`, list the objects created between reports `G` and `G+1` that are still alive, grouped by class and creation stack (needs `DC3_MEM_CENSUS_BT`). How the H-24 widget leak was found | `src/system/obj/Object.cpp` |
| `DC3_MEM_PROF_DUMP` | unset | Presence, under a preloaded jemalloc with `MALLOC_CONF=prof:true`: `prof.dump` at every report (read with `jeprof`) | `src/system/obj/Object.cpp` |
| `DC3_MEM_TRIM` | on | `0` disables `NativeReleaseFreeHeap`: at a screen Enter, once glibc holds ≥32 MB more free heap than at the last trim, `malloc_trim(0)` gives the free pages back to the kernel (~2–70 ms, MEASURED at load ~105). Without it a finished song's freed working set stays resident (H-24). No effect under jemalloc or on web | `src/system/obj/Object.cpp` |
| `DC3_TEL` | desktop: dev profile `api`, release `0` | `api`: telemetry sampled for `GET /api/telemetry`, nothing printed. Other non-zero: also a `DC3_TEL:` line on stderr every `DC3_TEL_INTERVAL` frames | `native/src/telemetry/GameplayTelemetry.cpp` |
| `DC3_TEL_INTERVAL` | `10` | Frames between telemetry lines | `native/src/telemetry/GameplayTelemetry.cpp` |
| `DC3_HTTP` | desktop: dev profile `1`, release `0` | Non-zero: start the HTTP debug server on **127.0.0.1 only** ([tools/HTTP_DEBUG_SERVER.md](../tools/HTTP_DEBUG_SERVER.md); another box: `ssh -L 9090:127.0.0.1:9090`). Only `dc3-native` is built with `DC3_HTTP_SERVER` | `native/src/platform/HttpServer.cpp` (DC3 glue; the engine copy is not compiled) |
| `DC3_HTTP_PORT` | window: `9090` if free, else kernel-assigned; headless: kernel-assigned | HTTP debug server port; the one in use is printed on stdout as `DC3_HTTP_PORT=<n>`. `0` or `auto`: kernel-assigned. An explicit port is strict: one that cannot be bound exits 98 (see [Stopping the engine](#stopping-the-engine-h-9)) | `native/src/platform/HttpServer.cpp` |
| `DC3_HTTP_PORT_FILE` | unset | Once the debug server is bound, write its port to this file (temp file + rename). How harnesses learn a `DC3_HTTP_PORT=0` port | `native/src/platform/HttpServer.cpp` |
| `DC3_HTTP_HOST` | `127.0.0.1` | Address the HTTP debug server binds. Unset is loopback only; set it (e.g. `0.0.0.0`) only to widen that on purpose: the server evaluates any DTA it is sent. The listen line then says `NOT loopback only`; an ssh tunnel is the safer way to reach another box | `native/src/platform/HttpServer.cpp` |

### Audio

| Variable | Default | Effect | Read in |
|----------|---------|--------|---------|
| `DC3_NO_AUDIO` | unset | Presence. No audio output device | `../milo-native-engine/src/audio/AudioDevice.cpp` |
| `MILO_AUDIO` | unset | `1` opens the audio device even when `MILO_HEADLESS` or `DC3_NO_AUDIO` is set | `../milo-native-engine/src/audio/AudioDevice.cpp` |
| `MILO_AUDIO_BACKEND` | ALSA | `pulseaudio`, `alsa`, `null` (dummy device on a real clock), or `default` (miniaudio's own choice) | `../milo-native-engine/src/audio/AudioDevice.cpp` |
| `DC3_AUDIO_GAIN` | `1.0` | Pre-limiter gain | `../milo-native-engine/src/audio/AudioDevice.cpp` |
| `DC3_DUMP_AUDIO` / `DC3_DUMP_SECONDS` | unset | Capture the mixed PCM to a file | `../milo-native-engine/src/audio/AudioDevice.cpp` |

### Rendering and UI diagnostics

| Variable | Default | Effect | Read in |
|----------|---------|--------|---------|
| `MILO_CLEAR_COLOR` | unset | `R,G,B,A` clear colour override | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp`, `src/App.cpp` |
| `MILO_SIMPLE_RENDER` | unset | Presence. Simplified material rendering | `../milo-native-engine/src/platform/MaterialSetup.cpp` |
| `MILO_GPU_LABELS` | auto | `1`: create the device with Dawn's `use_user_defined_labels_in_backend` toggle, so every Vulkan object carries its WebGPU label (readable names in RenderDoc / validation messages). `0`: never. Unset: on only when a Vulkan capture or debug layer is requested (`VK_INSTANCE_LAYERS`, `VK_LOADER_LAYERS_ENABLE`, `ENABLE_VULKAN_RENDERDOC_CAPTURE`), off otherwise — naming each object cost 5–8% of the main thread in menus. The log says `GpuDevice: backend object labels ON` when it is on | `../milo-native-engine/src/gfx/GpuDevice.cpp` |
| `MILO_DEBUG_PIPELINES` | unset | Presence. Log pipeline creation | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp`, `../milo-native-engine/src/gfx/PipelineManager.cpp` |
| `MILO_NO_TRANSPARENT_DEFER` | unset | Presence. Disable the transparent draw queue | `../milo-native-engine/src/platform/TransparentQueue.cpp` |
| `MILO_NO_SPOT_BEAMS` | unset | Presence. Drop spotlight beams | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp` |
| `MILO_NO_POINT_TESTS` | unset | Presence. Register no point tester | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp` |
| `MILO_DUMP_RT` | unset | Presence. Debug dump of render targets | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp` |
| `MILO_UI_CAM_MODE` | `original` | `original`, `widened`, `z_hack`, `rotate_hack` (native UI-camera experiments) | `src/system/ui/UI.cpp` |
| `MILO_NATIVE_FLOW_FILTER` | `none` | Escape hatch for the retired native flow filter: `curated`, `all`, `menu_only` | `src/system/ui/PanelDir.cpp` |
| `MILO_DEBUG_UI_FLOW` | unset | Presence. Log UI screen/panel flow | `src/system/ui/UI.cpp`, `UIPanel.cpp`, `UIScreen.cpp` |
| `MILO_DEBUG_MERGE` | `0` | Non-zero: log `MergeObject` source/target/action | `src/system/obj/Utl.cpp`, `src/system/rndobj/PropAnim.cpp` |
| `DC3_NO_UI` / `DC3_HUD_ONLY` | unset | Presence. Skip `TheUI->Draw()` | `src/App.cpp` |
| `DC3_DRAW_CRASH_GUARD` | off | `Dc3EnvFlag`. Run UI draw under a `sigsetjmp` guard | `src/App.cpp` |
| `DC3_DEV_MENU` | dev profile `1`, release off | `Dc3EnvFlag`. Starts the original's development menu on (`{ui show_dev_menu}`, Alt+D on the 360): extra Choose Mode modes and campaign difficulty rows, appended | `src/system/ui/UI.cpp` (`UIManager::Init`) |
| `DC3_RND_OVERLAYS` | dev profile `1`, release off | `Dc3EnvFlag`. Draws the original's `RndOverlay`s (`cheat_display`, quick-cheat stats) in a top-left ImGui window and logs `DC3 OVERLAY [<name>] <text>` when one changes | `native/src/platform/DebugPanel.cpp`, `src/system/rndobj/Overlay.cpp` |

### Object-lifetime and stub diagnostics

| Variable | Default | Effect | Read in |
|----------|---------|--------|---------|
| `DC3_REFRING_AUDIT` | unset | Non-empty, not `0`: shadow index of every `ObjRef`, logs `REFAUDIT` on a ref left pointing at freed memory | `src/system/obj/Object.cpp` |
| `DC3_POISON_FREED_OBJECTS` | unset | Non-empty, not `0`: poison cascade-freed object blocks with `0xDD` and never reuse them (memory grows without bound) | `src/system/obj/Dir.cpp` |
| `DC3_STUB_TRACE` | unset | Non-empty, not `0`: record every native stub hit | `native/src/StubTrace.cpp` |
| `DC3_STUB_TRACE_DUMP` | unset | Path to write the stub-trace table at exit | `native/src/main_native.cpp` |

### Test-only variables

Read by `milo-tests`, not by `dc3-native`. See [native/TESTING.md](../native/TESTING.md).

| Variable | Effect |
|----------|--------|
| `MILO_LIB`, `DC3_DTA_FLOW_TESTS`, `DC3_DLC_TESTS`, `DC3_GAMEPLAY_TESTS`, `DC3_AUDIO_TESTS`, `MILO_LONG_TEST` | Test gates (`DC3_DLC_TESTS` decides registration, not skipping: see `docs/native/DLC.md`) |
| `MILO_TEST_BIK`, `MILO_DIAG_FILE`, `MILO_DIAG_OBJECT`, `MILO_POSE_GOLDEN_DIR` | Fixture overrides |
| `MILO_BULK_CATEGORY`, `MILO_BULK_LIMIT`, `MILO_BULK_ALL` | `BulkLoad` sweep scope |
| `MILO_TEST_ALLOW_STALE_BUILD` | Skip the `TestGates.BuildMatchesSources` currency check |

### `scripts/dc3-agent-test.sh` defaults

The agent launcher sets these unless the caller already has them:
`DC3_HTTP=1`, `DC3_HTTP_PORT=9090`, `DC3_FAST_BOOT=1`, `DC3_TEL=1`,
`DC3_SHOW_SPLASH=0`, `MILO_MAX_FRAMES=0` (unlimited; bound the run yourself or
pass `--frames N`). `--headless` additionally exports `MILO_HEADLESS=1` and
`DC3_FAST_TIME=1`. (The script's own `--help` text still says
`MILO_MAX_FRAMES=100000`; the code sets `0`.) It runs under the dev profile, so it
also gets the other [Profiles](#profiles-dc3_profile) rows; its `DC3_TEL=1` and
`DC3_HTTP_PORT=9090` win over the profile's `api` and port policy.

## Fast Time Mode

By default, song time advances on the wall clock — a 3:30 song takes 3:30 of real time even in `MILO_NORENDER=1` mode where frames process at thousands of FPS. `DC3_FAST_TIME=1` overrides this so song time advances by ~8.33ms per frame (120fps equivalent), letting a full song complete in seconds.

```bash
# Full song playthrough in ~35 seconds instead of ~5 minutes
DC3_FAST_TIME=1 MILO_HEADLESS=1 MILO_NORENDER=1 \
  MILO_MAX_FRAMES=60000 MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
  native/build/dc3-native
```

`scripts/dc3-agent-test.sh --headless` enables this by default. `DC3_FAST_TIME` is a **presence** test, so `DC3_FAST_TIME=0` does *not* turn it off. For real-time progression in a headless agent run, leave out `--headless` and set the headless flag yourself: `MILO_HEADLESS=1 scripts/dc3-agent-test.sh`.

**Implementation**: `LiveInput::CurrentMs()` increments a synthetic counter per call instead of reading `mTimer.Ms()` or `mAudio.GetTime()`. Source: `src/lazer/game/LiveInput.cpp`.

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

Note: Milo uses PlayStation-style internal names (`kPad_X` = confirm, `kPad_Circle` = cancel) but the Xbox 360 layout maps A to confirm and B to cancel. Buttons are pressed for exactly 1 frame (press on N, auto-release on N+1). `#` starts a comment.

Directives run **in file order — the file is not sorted**. A `wait_screen` blocks until that screen is current and not transitioning; after 30×60 frames it logs `DC3 Input: TIMEOUT waiting for '<screen>'` and continues as if satisfied. A `+N` frame counts from the last satisfied `wait_screen`. A directive whose frame has already passed when the cursor reaches it is **skipped silently**, so keep absolute frames increasing. The frame counter is the input system's own monotonic counter, not `TheRnd`'s frame ID.

**Wake** (`N wake` / `+N wake`) means "the player presses the controller-mode wake button". If the game is already in controller mode it does nothing; otherwise it presses the wake button for one frame (`l3`, or `MILO_INPUT_WAKE_BUTTON`). It exists so one flow can drive both the original game under Xenia -- where a screen's first press must be preceded by a wake, see [Controller Mode Policy](#controller-mode-policy) -- and the native port, where under a forced policy (the default without a camera) every wake is a no-op. Each one logs `DC3 Input: wake at frame N: pressed button <enum value>` or `... no-op (already awake)`.

### How It Works

In headless mode (`gNativeWindow == NULL`), `JoypadPoll()` reads from the script instead of GLFW. On first poll, `LoadInputScript()` reads and parses the `MILO_INPUT_SCRIPT` file. Each frame, `GetScriptedButtons()` returns a bitmask of buttons active on that frame. The existing delta computation (`mNewPressed`, `mNewReleased`) handles press/release edges. Windowed mode is unaffected — GLFW gamepad + keyboard fallback works as before.

Source: `../milo-native-engine/src/platform/Joypad_Native.cpp`, which every native target links, `dc3-web` included (it has linked `libmilo-engine.a` since `2a51acd7e`). `native/src/platform/Joypad_Native.cpp` is a byte-identical copy that no buildable target compiles — edit the engine's. Screenshot capture: `../milo-native-engine/src/platform/Rnd_Wgpu.cpp`.

**Troubleshooting**: Create `MILO_SCREENSHOT_DIR` before launch (capture fails silently if missing). If scripted input doesn't work, verify frame numbers align with where the game actually is — the attract screen runs for a while before accepting input. In windowed mode, scripted input is disabled; use keyboard instead: arrow keys, Enter (confirm), Escape (cancel), Space (start), Tab (back).

### Available Input Flow Scripts

| Script | Route | Purpose |
|--------|-------|---------|
| `scripts/dc3-input-flows/song-scroll-test.txt` | boot -> main -> choose_mode -> song_select -> 8x down | Verify list scrolling |
| `scripts/dc3-input-flows/ymca.txt` | boot -> gameplay | Full song load test |
| `scripts/dc3-input-flows/controller-mode-wake.txt` | boot -> main (idle, a `wake` every 120 frames) -> choose_mode | `ControllerMode*` tests in `test_dta_flow.cpp` |

## Controller Mode Policy

On the 360 (and the original under Xenia) the UI has two input modes. Outside
**controller mode** the Kinect hand menus drive the UI (`HamNavList::UpdateGestures`
runs whenever `!GestureMgr::InControllerMode()`), and a pad press only
*enters* controller mode -- `ShellInput::OnMsg(ButtonDownMsg)` swallows it
(B excepted). In controller mode, `ShellInput::Poll` exits back to the hand
menus once `controller_mode_timeout` ms (helpbar property, default 5000) pass
with no pad input (every `ButtonDownMsg` restarts the timer via
`reset_controller_mode_timeout`), and entering gameplay or pressing Start
exits it too. A Kinect is always attached there, so the shell boots out of
controller mode (`GestureMgr::GestureMgr`). That is right exactly when
something can move the hand: with no camera, the timeout would strand a pad
player in menus nobody can drive.

`DC3_CONTROLLER_MODE` (`native/src/platform/System_Native.cpp`, read once and
logged as `DC3 Native: controller mode policy = ...`):

| Value | Behaviour |
|-------|-----------|
| `auto` (default; also unset/empty/unknown) | `faithful` while a real pose provider runs (`NativePoseProviderRunning()`: ncnn, the external pose server, or the web page's pushes), `forced` while only the dummy skeleton feeds the sensor. Latched by `GestureMgr_NativeInit` after the provider is chosen, then re-latched once per frame by `NativeControllerModePolicyTick` (`HelpBarPanel.cpp`, called from `ShellInput::Poll`), logged as `controller mode policy auto -> faithful/forced (...)`. forced→faithful (the web page's first `dc3_pose_push`, or a pose server that connects late) restarts the idle timer, so the image's own timeout hands the UI to the hand menus 5 s later; mid-song it completes the gameplay-panel exit instead. faithful→forced (the camera goes away) runs the image's `EnterControllerMode` body once, then pins. |
| `forced` | No hand menus: `GestureMgr` boots in controller mode and ignores `SetInControllerMode(false)`; `ShellInput::ExitControllerMode` returns at once (except the helpbar's gameplay exit, `NativeForcedHelpbarExitForGameplay`); `EnterControllerMode` only re-asserts the mode and activates the helpbar's `controller_mode.flow` once (`NativeBootControllerModeOnce`); the first `HamScreen::Enter` forces it on. Controller mode never times out, so no pad press is swallowed. |
| `faithful` | The image's logic: boot out of controller mode; `ShellInput::Enter/ExitControllerMode` run their image bodies (helpbar flows, `controller_mode_entered/exited`, `in_controller_mode`, RockCentral counters, active player); the idle timeout and the gameplay-panel exit in `ShellInput::Poll` fire. Each enter/exit logs `DC3 ControllerMode: enter/exit (...)` with the idle time and timeout. `DC3_CONTROLLER_MODE_TIMEOUT_MS` overrides the timeout (the tests use 750). |

With a camera, the keyboard and pad still work as on a 360: the first press
wakes controller mode (and is swallowed unless it is B), the rest navigate,
and 5 s without a press hands the UI back to the hands. A button-driven tool
that runs with a synthetic camera and has no `wake` directives pins
`DC3_CONTROLLER_MODE=forced` (`scripts/pose_scoring_gate.sh`,
`scripts/native_assert_harvest.py`).

Gesture menus themselves (PLAN P-3): an arm held out to the side (more than
1.3 shoulder widths from the spine, `DirectionGestureFilterSingleUser::
IsValidSwipePosition`) engages that side's list; hand height
(`HandHeightGestureFilter`) highlights an item; a forearm sweep across the
body (`ArcDetector`) selects; the left arm does the helpbar (back is its lower
item). `scripts/synthetic_kinect.py` performs these (`point`, `swipe`; the
thresholds are in its header) and `scripts/gesture_menu_flow.py` drives a
whole route with them -- see [POSE_INPUT.md](../native/POSE_INPUT.md#gesture-menus).

What `faithful` leaves out: the rest of the image's gameplay-panel arm of
`ShellInput::Poll` (practice-options hand invoke, wrong-hand-position anim,
`SetIdentificationEnabled`) needs the Kinect gesture filters and anim that
native `ShellInput::Init` never creates. The Enter/Exit bodies themselves
needed no new guards -- everything they touch exists natively (the helpbar is
loaded by `UIManager::Init` before the first screen, and `TheRockCentral`,
`TheProfileMgr`, `TheGameData` and the `SkeletonChooser` are all live).

Sites that consult the policy, each with a pointer comment:
`src/lazer/meta_ham/ShellInput.cpp` (Enter/ExitControllerMode, the native
`Poll` arm and its `NativeControllerModePolicyTick` call,
`SyncToCurrentScreen`'s timeout override),
`src/system/gesture/GestureMgr.cpp` (constructor, `SetInControllerMode`),
`src/lazer/meta_ham/HamScreen.cpp` (first-enter force),
`src/lazer/meta_ham/HelpBarPanel.cpp` (`NativeBootControllerModeOnce`, the
forced helpbar gameplay pair, `NativeControllerModePolicyTick`),
`native/src/platform/GestureMgr_Native.cpp` (boot latch and
`SetInControllerMode(true)`, `NativePoseProviderRunning`).
All of them are inside `#ifdef HX_NATIVE` or `native/`; the PPC build is untouched.

The script **`wake`** directive is the input-side half: under a forced policy it is
always a no-op; under `faithful` (and on the original under Xenia, where the
flow's author must place one before each screen's first press) it presses the
wake button whenever controller mode has been left. A no-op wake presses
nothing, so it does not restart the idle timer: keep the real press a frame or
two behind its wake, or the timeout can fall between them and swallow it. The engine asks the game
through `JoypadScriptSetWakeNeeded` (`platform/JoypadScriptHook.h` in
milo-native-engine); DC3 registers `NativeWakeNeeded` (= not
`TheGestureMgr->InControllerMode()`) in `GestureMgr_NativeInit`.

Tests: `ControllerModeFaithfulTest.*` (timeout after the configured timeout;
a wake re-enters) and `ControllerModeForcedTest.WakeIsANoOpUnderForcedDefault`
(default `auto` with no provider stays forced) in
`native/tests/test_dta_flow.cpp`; `GestureMenusTest` / `GestureMenusForcedTest`
(the whole route by gesture, and the same performer under `forced` stuck on
the title) in `native/tests/test_native_gesture_menus.cpp`. All gated by
`DC3_DTA_FLOW_TESTS`.

## Unit Tests

Object lifetime tests live in `native/tests/test_object_lifetime.cpp`. Key tests:

| Test | What it covers |
|------|---------------|
| `ReplaceListLiveWalkDoesNotCrash` | Ring walk with N refs, all redirect correctly |
| `DirPtrRefCountsConsistentAfterMerge` | DirPtrRefCounts survives MergeDirs |
| `ObjDirPtrCascadeDeleteDoesNotDoubleFree` | Nested subdir cascade with cross-references |
| `RemoveSubDirReleasesDirPtrRef` | Subdir removal + owner deletion |
| `ReplaceRefsWithSelfDeletingObjDirPtr` | ObjDirPtr that triggers self-delete during Replace |
| `DeletingFlowChildRemovesEntryFromParent` | Flow child deletion updates parent's ObjPtrVec |
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

Related regression tests: `ObjectLifetimeTest.FlowAnimateDoubleDeleteRingCorruption` and `ObjectLifetimeTest.DeletingFlowChildRemovesEntryFromParent` in `native/tests/test_object_lifetime.cpp`.

## Sandbox Notes

GPU access requires `dangerouslyDisableSandbox: true`. The Vulkan ICD loader needs filesystem access outside the sandbox allowlist. Non-GPU runs (with `MILO_NORENDER=1`) work within the sandbox.
