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
| `DC3_RND_OVERLAYS` | The original's `RndOverlay`s (`cheat_display` notices, the framerate/stats/timer quick cheats) in a corner window, plus a `DC3 OVERLAY [<name>] <text>` stderr line when one changes. `WgpuRnd::EndDrawing` never drew them | off (invisible) | `log` | off | Logged, not drawn (np-notifyband, PLAN H-105): the console game never drew its debug-build notices over the picture, and a player was reading "... creating target cache for boombox.trans, targets changed while playing camera" across gameplay on desktop and the web page. A notice nobody can see is still useless, so every line stays in the log; `DC3_RND_OVERLAYS=1` (or `scripts/play.sh --overlays`) draws them in the corner again. FAILs never went through an overlay |
| `DC3_HTTP` | HTTP debug server on **127.0.0.1 only** | off | `1` (desktop) | off | `curl` state, eval DTA, screenshot; loopback, so nothing is exposed |
| `DC3_TEL` | Gameplay telemetry. `api`: sampled for `GET /api/telemetry`, no stderr lines | off | `api` (desktop) | off | The API is free; the ~2 KB-per-10-frames lines are for scripts (`DC3_TEL=1`) |
| `DC3_TAG_MODALS` | `dev`: every `MILO_WARN/NOTIFY/FAIL/ASSERT` as one `MILO_<KIND>:` line, rate-limited (first 5, then powers of ten), no backtraces | off | `dev` (desktop) | off | See what the game complains about; `=1` is still the harvest mode |
| `MILO_INPUT_LOG` | One `Joypad: down/up ...` line per button message | off | `1` | off | Shows what a key or pad press really sent |
| `DC3_MOVE_RATING_LOG` | One `DC3 MOVE_RATING` line per scored move | off | `1` | off | Scoring is visible while you dance |
| `DC3_BINDING_LOG` | A `DC3 SCORING:` line when a player is bound to another body or a skeleton slot changes hands | off | `1` | off | A playtest log says which body each player was scored on; change-only, at most 20 lines per 5 s |
| `MILO_PERF` | A `[PERF]` fps/frame-time line every 5 s | off | `1` | off | Cheap; presence-tested, so `MILO_PERF=0` is unset by the profile and reads as off |
| `DC3_DLC` | Every DLC package under `<data>/dlc` (`orig-assets/dlc`, made by `scripts/dlc/import.sh`) is mounted; `DC3_DLC_DIR` names other roots and wins; `DC3_DLC=0` is off ([DLC](../native/DLC.md)). On web the packages the server has are listed and each song's files are fetched when it is played (W-23) | off | `1` | off | A 360 lists the songs of every package installed on it, so this is the original's behaviour for the content present. Flows pick songs by name (`highlight`), and ctest pins `DC3_DLC=0`, so tests see the disc list on every box. No `<data>/dlc` = no change |

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
| `DC3_FAST_BOOT` | Not a dev default: it advances past `title_screen` without the confirm that sets the initial profiles and `$post_load_dest_screen`, and skips the voice-control first-run tutorial (`src/system/ui/UI.cpp`, the `sBoot` table; read from the code, 2026-11-14), so a playtester would never see the profile setup and first-run prompts. The default boot of a played console ([Boot sequence](#boot-sequence)) is not this switch and not a profile row: it passes attract and the autosave warning only and waits on `title_screen`, so that confirm is still the player's |
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
| `MILO_HEADLESS_FRAMES_IN_FLIGHT` | `2` | Headless GPU only: how many frames the loop may run ahead of the GPU. Each `EndDrawing` waits for the frame this many back to retire (a window is paced by present instead). `0` = unbounded, the behaviour before PLAN H-75: an uncapped headless loop then queues frames as fast as it records them and the driver's descriptor / command / staging pools grow with the backlog (H-45: +210 MB rss and +55 MB of `/dev/nvidia*` mappings over 10 songs) | `../milo-native-engine/src/gfx/GpuDevice.cpp` |
| `MILO_NORENDER` | unset | Presence. No GPU at all (logic only, fastest). GPU rendering is **on by default** | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp` |
| `MILO_RENDER` | unset | Legacy. Presence forces GPU init even over `MILO_NORENDER` — note `MILO_RENDER=0` also forces it on | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp` |
| `MILO_WIDTH` / `MILO_HEIGHT` | `1280` / `720` | Windowed: the window's starting size (in screen coordinates; times the content scale on a HiDPI X11 desktop, so `Xft.dpi: 192` opens 2560x1440). The frame is always the Rnd virtual resolution (1280x720), scaled into the window with its aspect kept and black bars (PLAN H-18, `gfx/PresentScale`). Headless: the render target size | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp`, `../milo-native-engine/src/gfx/GpuDevice.cpp` |
| `MILO_FULLSCREEN` | unset | Truthy: start fullscreen on the primary monitor at its mode (`scripts/play.sh --fullscreen`). F11 or Alt+Enter toggles at any time; leaving restores the window's position and size | `../milo-native-engine/src/gfx/GpuDevice.cpp` |
| `MILO_PRESENT_SCALE` | on | `0`: the old present -- the swapchain is the frame, and a window larger than 1280x720 shows the picture in its top-left corner. A diagnostic, not a setting | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp` |
| `DC3_LOOK` | `enhanced` | `retail`: the original hardware's look, for parity measurement against Xenia (two mip levels per bitmap, no anisotropy, no LOD bias, one sample per pixel, the original's alpha test). Unset or `enhanced`: native polish (whole mip chains, 16x anisotropy and LOD bias -1 on world textures, text/HUD at the original's two levels, sharpened coverage on cut-outs, a soft contact shadow under each 3D crowd member, and 2x2 supersampling on a discrete GPU **when presenting to a window**). Anything else warns and uses `enhanced`. Read once; DC3 only (rb3 / rb3-xenon never read it) | `../milo-native-engine/src/platform/LookProfile.cpp` |
| `DC3_DISPLAY_GAMMA` | `tv` | The curve between the front buffer and the screen (`DisplayRamp`, after the UI, on every output including screenshots). Unset or `tv`: what the console sends a television, the game's ramp `pow(v, 0.85)` passed through the console's display correction (PLAN H-160). `srgb`: the ramp alone, which is a console on a VGA cable and every native frame before 2026-11-21; it is 13 to 16 levels brighter through the dark and middle tones. `off` or `0`: the front buffer, no curve. A number: that gamma in place of 0.85, on a television (**before 2026-11-21 a number meant the ramp alone**; `srgb:<g>` asks for that, `tv:<g>` is the explicit form). The same switch at runtime: `{rnd native_look display_tv}`, `display_srgb`, `display_off`, `display_rule` (back to what the environment asked for). Applies to both `DC3_LOOK` profiles. DC3 only | `../milo-native-engine/src/platform/rndshape/DC3PostChain.cpp`, `../milo-native-engine/src/gfx/DisplayRamp.cpp` |
| `MILO_RENDER_SCALE` | profile default | Internal render scale (supersampling), box-filtered back to the frame's size at present and readback; clamped to `[1, 2]`. Unset: the profile's default, which applies only to a windowed run -- enhanced draws 2x on a discrete GPU (the 1280x720 frame drawn at 2560x1440 whatever the window size; capped so the drawn frame never exceeds 3840x2160, whole scales only), 1x on an integrated GPU, 1x under `retail`. **A headless run (tests, gates, screenshots, `scripts/gpu/look_capture.sh`) draws 1x unless this asks**: `look_capture.sh --scale 2` gives the windowed enhanced look. Set, it wins under either `DC3_LOOK` profile, headless too, on any adapter, past the cap: `1` keeps the enhanced look without supersampling (a weak card), `1.5` / `2` to try it on a handheld. Not a number > 0: warns, default. DC3 only | `../milo-native-engine/src/platform/LookProfile.cpp`, `../milo-native-engine/src/gfx/GpuDevice.cpp` (`ResolveRenderScale`) |
| `MILO_GLFW_PLATFORM` | any (GLFW's choice) | `x11` or `wayland`: the window system to open the window on. Without it, a window whose surface WebGPU cannot present to is reopened through X11/XWayland (the prebuilt Dawn has no Wayland surface, PLAN H-33); with it, no fallback | `../milo-native-engine/src/gfx/GpuDevice.cpp` |
| `MILO_SURFACE_FAULT` | unset | Test hook. `init`: every window's surface probe fails (exit 97); `init-once`: the first only (the X11 reopen runs, even on X11); `acquire`: the surface never gives a frame (exit 97 after 5 s) | `../milo-native-engine/src/gfx/GpuDevice.cpp` |
| `MILO_MAX_FRAMES` | `10000` | Exit after N frames. `0` or negative = unlimited | `src/App.cpp` |
| `DC3_FPS_CAP` | windowed `60`, headless off | Frame cap (PLAN H-29). The image never draws more than 60 fps (`rnd sync 1` → `D3DRS_PRESENTINTERVAL 1`), so a windowed run sleeps out the rest of each frame instead of drawing at the panel's rate. The cap is snapped to a whole number of refreshes so every frame is held equally long: 72 on a 144 Hz panel, 60 on 120/240 Hz, 82.5 on 165 Hz; at 60/75 Hz vsync alone paces and the cap is off. `0`/`off` = no cap (display rate). `N` = cap at N (windowed: snapped the same way). **Headless runs are never capped unless this is set** (flow tests depend on running faster than real time); headless `N` caps at exactly N. The log names the decision (`DC3 Native: frame cap: …`) and, at exit, how many frames were paced. Desktop only; web is paced by `requestAnimationFrame` | `src/App.cpp`, `native/src/platform/FramePacer_Native.cpp` |
| `DC3_DISPLAY_HZ` | unset | Windowed only: take N as the display's refresh rate for the frame-cap decision and the slow-display notice (PLAN H-127: under 50 Hz the game posts `This display runs at N Hz ...` for 15 s at startup). For tests and for seeing the notice; the real display still paces through vsync | `src/App.cpp` (`RunWithoutDebugging`), `native/src/platform/FramePacer_Native.cpp` |
| `DC3_CANVAS2D_DEMO` | unset | `1`: draw a fixed test scene from the engine's 2D canvas (`gfx/Canvas2D.h`) over every frame, in the overlay pass (`HamRenderHook::DrawGameOverlay`): a dark rounded card at the centre with a red square, a green rounded square, a cyan ring and a clipped yellow bar. Prints `Canvas2D demo: 5 quads in 2 draws` once. The example of game code using the canvas; `NativeWindowTest.Canvas2DDemoIsOnScreen` reads it back from the window |
| `DC3_SONG_BROWSER` | desktop `auto`; web `0`; `0` when `MILO_INPUT_SCRIPT` is set | The natively drawn song browser on song select ([SONG_SELECT.md](../native/SONG_SELECT.md#the-song-browser)). `auto`: shown unless somebody steers by hand -- a hand held in the list's pose for 0.75 s, Start with a body in view, or a song select reached by gesture; a tracked body alone never hides it, and a press or a typed character brings it back at once. `1`: always, on the visits that pick one song (Perform, Rehearse, Dance Battle). `0`: never. `DC3_CLASSIC_SONG_SELECT=1` wins over it. **The pin: unset with `MILO_INPUT_SCRIPT` set means `0`**, so every scripted run keeps driving the classic list; a script that wants the browser sets `1` or `auto`. Logged once as `DC3 SongBrowser: mode=...`. State: `GET /api/songbrowser/state`; typing: `POST /api/input/text`; Random Song / Playlists: `POST /api/songbrowser/command` | `native/src/platform/SongBrowserPanel_Native.cpp` (`ResolveMode`) |
| `MILO_THERMAL_GOVERNOR` | on | `0` (or `off`, `false`, `no`) turns the thermal governor off (PLAN H-128): the output is never resized for heat. On, the engine lowers the output's row cap one step (1080, 900, 720 rows; never lower, never above the cap already in force) when the machine reports thermal pressure (fair or worse) AND the game has been under 90 % of its frame rate for 5 s (2 s at serious/critical), and gives one step back per 90 s at nominal. Each change prints `GpuDevice: thermal level N (name), frames late: output AxB -> CxD (thermal row cap R)` (`cooled:` on the way back). Only macOS reports a level (`NSProcessInfo.thermalState`, windowed runs); everywhere else, and headless, nothing happens unless one is injected | engine `src/gfx/ThermalGovernor.{h,cpp}` (the rule, pure), `src/platform/ThermalState.{h,cpp}` (the level, the driver), `src/platform/ThermalState_Mac.mm` |
| `MILO_THERMAL_STATE` | unset | `0`..`3` (nominal, fair, serious, critical): inject that thermal level instead of the OS's, headless too. For tests and for seeing the governor work on a machine that reports none; `PUT /api/thermal?level=N` does the same in a running game (`level=os` withdraws it) | engine `src/platform/ThermalState.cpp` |
| `MILO_THERMAL_COOL_SECONDS` | `90` | The governor's cool dwell: seconds at nominal before one step is given back. A test's knob (the engine-run test uses 3) | engine `src/platform/ThermalState.cpp` |
| `MILO_FATAL_FAILS` | `0` (non-fatal) | `MILO_FAIL`/`MILO_ASSERT` print `FAIL:` and continue, like the 360's "Continue" dialog. Non-zero = `abort()`. A fail inside `MILO_TRY` throws to the `MILO_CATCH` regardless, as in the image (this includes `MILO_FAIL_DTA`) | `src/system/os/Debug.cpp` |
| `DC3_DATA` | search | Game data root (must contain `gen/main_xbox.hdr`). Unset: tries `orig-assets`, `../orig-assets`, `../../orig-assets`, `.` | `native/src/platform/System_Native.cpp` |
| `DC3_DLC` | unset (dev profile: `1`) | `1`: mount every package under `<data>/dlc` when that directory exists; `0`: none. `DC3_DLC_DIR` wins over it. ctest sets `0` | `native/src/platform/ContentMgr_Native.cpp` (`NativeDlcRoots`) |
| `DC3_DLC_DIR` | unset | DLC roots, `:`-separated (set but empty: none, whatever `DC3_DLC` says; `play.sh --no-dlc`); every subdirectory is one extracted package (`scripts/dlc/import.sh`, lowercase paths), mounted on the 360's `cntNNNNNNNN:` drive. See [DLC.md](../native/DLC.md) | `native/src/platform/ContentMgr_Native.cpp`, engine `File_Native.cpp` |
| `DC3_WEB_DLC_KEEP` | unset | Web only: `1` keeps a played DLC song's files in MEMFS. By default they are unlinked when song select or the main menu is entered again (W-27), so a session that plays through the DLC list does not keep every song's 6–27 MB | `native/src/main_web.cpp` (`WebDlcDropSongFiles`) |
| `DC3_CACHE_DIR` | `$HOME/.local/share/dc3/cache` | Native cache backend root (song cache etc.) | `native/src/platform/Cache_Stub.cpp` |
| `DC3_SAVE_DIR` | `$HOME/.local/share/dc3/saves` | Memory-card / save directory | `native/src/platform/MemcardMgr_Stub.cpp` |
| `DC3_FAST_BOOT` | unset | Non-zero: the boot screens (attract → autosave warning → title → main) are force-advanced after 10 frames each (title after 60), to `main_screen`; the test suites' boot. `0`: the original sequence, with no screen ever passed for the player. Unset: the console's state decides, see [Boot sequence](#boot-sequence). Web: `?fast_boot=1` | `native/src/platform/System_Native.cpp` (`Dc3BootPlanResolve`), `src/system/ui/UI.cpp` |
| `DC3_SHOW_SPLASH` | first launch: shown; played console: not | `1` shows the ESRB + Harmonix splash screens, `0` (also `false`/`off`/`no`) skips them, whatever the console's state; unset follows the [boot sequence](#boot-sequence). The engine's splash is never shown on web (the page plays a video over the canvas while the wasm boots, and takes it down at the title screen on a browser that has played before) | `native/src/platform/System_Native.cpp`, `src/App.cpp` |
| `DC3_BOOT_INTRO` | unset | `1`: the original boot sequence on every launch (splash screens, attract movie, autosave warning). `0`: never, even on a first launch. Unset: shown until the console has been played once. See [Boot sequence](#boot-sequence). Web: `?env=DC3_BOOT_INTRO=1` | `native/src/platform/System_Native.cpp` (`Dc3BootPlanResolve`) |
| `DC3_CLASSIC_SONG_SELECT` | off | `1`: song select exactly as in the image -- three sorts, no Recently Played section, Left/Right and LB/RB inert, the sort not remembered. Off: the native song select ([SONG_SELECT.md](../native/SONG_SELECT.md)). The play history in `$DC3_SAVE_DIR/song_select.txt` is still recorded under it, only not shown. Also turns the song browser off (`DC3_SONG_BROWSER`) | `native/src/platform/SongSelect_Native.cpp` (`Dc3ClassicSongSelect`) |
| `DC3_PROFILE` | `dev` | `dev` or `release`: the defaults of the rows in [Profiles](#profiles-dc3_profile) | `native/src/platform/System_Native.cpp` |
| `DC3_DEV_OPTIONS` | dev profile `1`, release off | `Dc3EnvFlag`. Shows the native overlay's developer rows (Camera Blend, the debug panel) in Options → Gameplay; off, the page has the retail rows only (`{native_settings dev_options}`, PLAN G-20) | `src/App.cpp`, `native/dta/ui/options/options_gameplay.dta` |
| `DC3_TUTORIALS_SEEN` | off | `Dc3EnvFlag`. Dev/test switch: every first-run tutorial reads as seen (`ProfileMgr::HasSeenTutorial`, `HX_NATIVE`), so a scripted flow on a fresh profile (`omg.txt`, any flow that starts a song) goes from `multiuser_screen` straight to loading instead of waiting on `tutorial_perform_it_screen_0`. Nothing is marked or saved. Not a profile row: it skips screens. The flow tests seed a profile instead (`SeedGlobalOptions`) | `src/lazer/meta_ham/ProfileMgr.cpp` |
| `DC3_FAST_TIME` | unset | Presence. Song time advances per frame instead of on the wall clock (see [Fast Time Mode](#fast-time-mode)) | `src/lazer/game/LiveInput.cpp` |
| `DC3_HEARTBEAT_S` | `60` | Seconds between `DC3_HEARTBEAT: uptime_s=… rss_kb=…` lines on stderr; `0` disables | `native/src/main_native.cpp` |
| `DC3_TAG_MODALS` | desktop: dev profile `dev`, release `0`; **web `1`** | Every `MILO_WARN`/`MILO_NOTIFY` (desktop also `MILO_FAIL`) printed as one `MILO_<KIND>: <msg>` line. Desktop `1` (the harvest mode, `scripts/native_assert_harvest.py`): stderr, every line, plus a backtrace for each new message. Desktop `dev`: stderr, each distinct message's first 5 then every power of ten with `[repeat N]`, no backtraces. Web: the browser devtools console only (`console.warn`, not the page's console), each distinct message's first 5 repeats then every power of ten (W-9); `?env=DC3_TAG_MODALS=0` turns it off. Without it a notify at `notify_level` 1 goes to `cheat_display` and is never printed | `src/system/os/Debug.cpp` |

### Boot sequence

The original runs the same sequence on every launch: the ESRB and Harmonix
splash screens, the attract movie (`videos/intro.bik`, 83 s unless a button
skips it), a 4 s autosave warning that takes no input, then the title screen.
Native does that **once per console**. What it reads is the disk: the global
options file the game writes on its way from the title screen to the main menu
(`<DC3_CACHE_DIR>/global/global`, 0x39 bytes), looked at once in `App::App`,
before this run can write it.

| The console | Boot | Log line |
|---|---|---|
| no global options on disk (a first launch; every test, each on its own empty `DC3_CACHE_DIR`) | the original sequence | `DC3 Boot: the original sequence (first launch on this console); splash screens on; …` |
| global options on disk | no splash screens; attract and the autosave warning are each left after 10 frames; the game then waits on `title_screen` | `DC3 Boot: straight to the title screen (this console has been played before); splash screens off; …` |

Each of three variables wins over the disk when it is set (an empty value
counts as unset):

| Variable | Effect |
|---|---|
| `DC3_BOOT_INTRO=1` | the original sequence on every launch: **the one switch back** (`0`: never, even on a first launch) |
| `DC3_SHOW_SPLASH=1` / `0` | the splash screens only |
| `DC3_FAST_BOOT=1` | the test suites' boot: attract, the warning, the title confirm and the save wait all pass by themselves, to `main_screen` |
| `DC3_FAST_BOOT=0` | what it always gave: the original sequence, and no screen ever passed for the player |

The decision is `Dc3BootPlanResolve` (`native/src/platform/System_Native.cpp`),
a pure function; `UIManager::Poll` (`src/system/ui/UI.cpp`, the `sBoot` table)
and the splash block in `App::App` only ask it.

What a played console's boot keeps from the original:

- **The title confirm.** It sets the initial profiles and
  `$post_load_dest_screen`; only `DC3_FAST_BOOT` makes it for the player, and
  that stays opt-in under every profile. The skip ends at the first screen that
  is not attract or the warning, so nothing later in a session is passed.
- **The autosave warning screen's work.** Its `enter` starts loading sixteen
  panels the shell needs and its panel's `unload` finishes them
  (`ui/title/autosave_warning.dta`), so the screen is still entered and left
  through the game's own transition; only its 4 s message is gone.
- **Every tutorial.** Nothing is marked seen. A tutorial is shown until the
  game itself records it, and that record is now kept (desktop: on disk; web:
  the [save store](../native/WEB.md#save-data-w-36)).

It is not a `DC3_PROFILE` row: it follows the console's state, not who is
playing, and applies under `dev` and `release` alike.

Measured 2026-11-20 with `scripts/native_boot_time.py` (windowed in a private
Xvfb, muted, a key press as soon as each screen takes one; this box's Xvfb
window ran at 32 to 41 fps, not 60), seconds from starting the process:

| | title screen | main menu | key presses |
|---|---|---|---|
| played console, before (`DC3_BOOT_INTRO=1` gives the same today: 9.9, 9.9) | 7.7, 8.1, 7.8 | 10.1, 10.6, 10.3 | 2 |
| played console, now | 3.5, 3.5, 3.3 | 5.8, 5.8, 5.6 | 1 |
| first launch, before / now | 8.0, 7.9 / 7.9, 7.7 | 10.3, 10.4 / 9.9, 9.8 | 2 |

Of the 4.5 s saved, 3.2 s is the autosave warning (4.3 s to 1.1 s) and 0.8 s
the attract screen (1.3 s with the skip press to 0.5 s). The 1.1 s that is left
of the warning is the screen's load and its sixteen panels (read from its DTA,
not timed apart), which the 4 s message used to cover. The splash screens are
not in it: a
loose-file run does not wait for them (`splash.SetWaitForSplash(false)` when
`!UsingCD()`), attract is entered about 2 s after the process starts either
way, and `DC3_SHOW_SPLASH=0` on the old build measured 11.5, 11.8, 10.3 s, the
same within noise. A player who does not press on the attract movie waits out
all 83 s of it before; now there is nothing to wait out.

Web, the same day with `scripts/web/boot-time.mjs` (Chromium headless shell,
localhost, warm HTTP cache, seconds from the navigation; the two "now" rows of
a played browser are one run that alternates the two URLs, so the box's load
is the same for both):

| | title screen | main menu | key presses |
|---|---|---|---|
| played browser, before (the build with the save store only) | 9.9, 10.0, 11.0 | 12.0, 12.1, 13.5 | 4 |
| played browser, now, `&env=DC3_BOOT_INTRO=1` | 10.6, 10.6, 10.2, 10.4 | 12.7, 12.8, 12.2, 12.5 | 4 |
| played browser, now | 6.1, 6.0, 6.4, 6.5 | 8.2, 8.1, 8.5, 8.6 | 2 |
| played browser, now, nothing pressed | 6.2, 6.4, 6.0 | stays on the title screen | 0 |
| first launch (`&reset_save=1`), before / now | 10.4, 9.9 / 10.5, 10.0, 12.2 | 12.4, 12.0 / 12.6, 12.1, 14.3 | 4 |

About 4.6 s of every web row is the page itself (download from localhost,
wasm compile, engine boot) before the first screen. With nothing pressed the
page's splash video is gone half a second after the title screen comes up
(its fade; `main_web.cpp`, `WebScreenBundleHook`). Under the original sequence
with nothing pressed it stays to its end, 9.8 s after the navigation (measured
with `&env=DC3_BOOT_INTRO=1`), which would cover the title screen for three
and a half seconds. The driver presses on the attract movie only when the
engine says it is showing the original sequence: a press that lands in a
played browser's half-second attract screen makes that screen last 1.3 s
instead of 0.5 (measured, 4 of 4 loads, which reached the main menu at 9.8 to
10.6 s).

What is left on a played console is two screen loads: attract and the autosave
warning are still entered and left (1.4 s on web, 1.6 s on desktop, both
dwells included). Not entering attract at all would save about half a second
of that and was not tried.

Tests: `Dc3BootPlanTest.*` (the decision table, in-process) and
`HeadlessBootTest.{AFirstLaunchIsTheOriginalSequence,
AReturningConsoleGoesStraightToTheTitleScreen,
AReturningConsoleWaitsOnTheTitleScreen,
BootIntroAndFastBootOffShowTheOriginalSequenceAgain}` (a real `dc3-native`: one
first launch driven like a player, then the same console launched again).
Five sabotages, each caught by both a unit and an engine test where both
apply: the title passed too, `DC3_BOOT_INTRO` ignored, a played console not
recognised, `DC3_FAST_BOOT=0` not winning, every console counted as played.

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
| The window cannot be drawn into (PLAN H-33): its surface is unusable on every window system tried, or it has not given a single frame in 5 s | `DC3 Native: FATAL - the window cannot be presented to: …` (or `… has not given a single frame in 5 s (N attempts, …)`), `how=exit_runtime detail=window_surface_unusable`, **rc 97**. Never a fall back to an invisible headless run. A failed acquire waits a frame period, so the loop does not spin while it lasts. `MILO_HEADLESS=1` runs without a window |
| `exit()` from anywhere (DTA `{exit}`, `Debug::Exit` → `XLaunchNewImage`, Xlib's default error handlers) | `how=exit_runtime` with exit()'s own code; the static destructors are skipped, as the main loop's return always did (they crash in `ObjDirPtr<ObjectDir>::operator=`). A FAIL raised by an exit callback is non-fatal, as everywhere on native, even inside an eval's try scope: once `Debug::Exit` has begun, `Debug::Fail` no longer throws, so the exit always runs every callback and reaches `exit()` (H-101; before, `{exit}` during main_screen's content refresh threw out of the exit half done and crashed in the next `HamUI::Draw`) |

Tests: `NativeExitTest.*` and `NativeWindowTest.*` (run their own Xvfb; no window manager needed).

### Input

| Variable | Default | Effect | Read in |
|----------|---------|--------|---------|
| `MILO_INPUT_SCRIPT` | unset | Scripted button presses, used when there is no window (see [Scripted Input System](#scripted-input-system)) | `../milo-native-engine/src/platform/Joypad_Native.cpp` |
| `MILO_INPUT_WAKE_BUTTON` | `l3` | Button a script `wake` presses | `../milo-native-engine/src/platform/Joypad_Native.cpp` |
| `MILO_INPUT_LOG` | dev profile `1`, release unset | Non-zero: one `Joypad: down pad=0 button=7 (X) action=14` / `Joypad: up ...` line per button message, any source (keyboard, gamepad, script, HTTP) | `../milo-native-engine/src/platform/Joypad_Native.cpp` |
| `DC3_CONTROLLER_MODE` | `auto` | `auto`, `forced` or `faithful` (see [Controller Mode Policy](#controller-mode-policy)) | `native/src/platform/System_Native.cpp` |
| `DC3_CONTROLLER_MODE_TIMEOUT_MS` | helpbar value | faithful policy only: overrides the helpbar's `controller_mode_timeout` | `native/src/platform/System_Native.cpp` |
| `DC3_CONTROLLER_HOLD_MS` | `300000`, or `DC3_CONTROLLER_MODE_TIMEOUT_MS` when that is set | faithful policy only: how long a keyboard/pad press keeps the menus without another one; both hands raised end it at once. `0` = off (the image's 5 s idle exit). See [Controller Mode Policy](#controller-mode-policy) | `native/src/platform/System_Native.cpp` |
| `DC3_HTTP_PACE_FPS` | unset (unpaced) | Cap the frame rate of a debug-server run, so a client answering every frame (the synthetic performer on `/api/frame/wait`) keeps up; `PUT /api/pace?fps=N` changes it at run time | `native/src/platform/HttpServer.cpp` |
| `DC3_MESHFILTER_LOG` / `DC3_MESHFILTER_ALLOW` | unset | Log each mesh `MeshFilter` refuses (once, with its rule) / draw what the named rules (or `all`) would refuse | `native/src/platform/MeshFilter.cpp` |
| `DC3_PLAYER_BOX` | `auto` | The cursor panel's two player boxes (top corners of the menus, under each score in a song). `auto`: drawn only while a camera stream is live; `on`: always, as before 2026-11-21; `off`: never. Changes at run time with `{native_settings set_player_box auto\|on\|off}`; `GET /api/camera`'s `playerBox` member says what the draws did (PLAN H-147) | `native/src/platform/PlayerBox_Native.h` |

### Scoring and pose

Full semantics (value parsing, provider selection, expected scores) are in
[native/SCORING_ENV_VARS.md](../native/SCORING_ENV_VARS.md). `Dc3EnvFlag`
variables are value-parsed: `0`/`false`/`off`/`no` disable.

| Variable | Default | Effect | Read in |
|----------|---------|--------|---------|
| `DC3_NATIVE_SCORING` | on | `Dc3EnvFlag`. Per-frame gesture poll and the sensor feed | `native/src/platform/GestureMgr_Native.cpp`, `src/App.cpp` |
| `DC3_REAL_MOVE_PASSED` | on | `Dc3EnvFlag`. The image's `move_passed` path | `src/lazer/game/Game.cpp` |
| `DC3_POSE_SELFTEST` | off | `Dc3EnvFlag`. Feed the choreography's own reference pose as the player | `native/src/platform/GestureMgr_Native.cpp`, `src/App.cpp`, `src/system/hamobj/FilterQueue.cpp` |
| `DC3_SCORING_DEBUG` | off | `Dc3EnvFlag`. Once-per-second scoring liveness counters; who is bound to what and why (`bind pN -> tracking id … rule=same-id\|near\|auto lost= dist=`, `stand-in tracking id … released`, `tracker id … is tracking id …`, `slot N <- tracking id`, `player->slot binding`); and each generic passive message's key (`passive message join_in_progress` is "New player detected!") | `native/src/platform/GestureMgr_Native.cpp`, `src/lazer/game/Game.cpp`, `src/lazer/meta_ham/PassiveMessenger.cpp` (`HX_NATIVE`) |
| `DC3_MOVE_RATING_LOG` | dev profile `1`, release off | `Dc3EnvFlag`. One `DC3 MOVE_RATING` line per scored move; `body=` is the tracking id of the body the player was bound to (-1: none) | `src/lazer/game/Game.cpp` |
| `DC3_BINDING_LOG` | dev profile `1`, release off | `Dc3EnvFlag`. The change-only who-is-who lines of `DC3_SCORING_DEBUG` without its per-second counters: `bind pN -> tracking id … rule= lost= dist=`, `stand-in tracking id … released`, `tracker id … is tracking id …`, `slot N <- tracking id`, `player->slot binding p0= p1= (tracking ids …)`, `the picture moved under …`. At most 20 lines per 5 s, the rest counted in one line (`DC3_SCORING_DEBUG=1` prints every one). PLAN H-158 | `native/src/platform/GestureMgr_Native.cpp` (`BindingLog`) |
| `DC3_MOVE_WEIGHTS_LOG` | off | `Dc3EnvFlag`. Once per move, `DC3 MOVE_WEIGHTS move= frames= <node>=<share> ...`: each ham2 error node's share of `DetectFrame::LimbPSNR`'s weight over the move's frames (which joints the move scores). `scripts/pose_cutoff/move_weights.py` aggregates them | `src/lazer/game/Game.cpp` (`HX_NATIVE`), `native/src/platform/PoseTarget_Native.cpp` |
| `DC3_POSE` | unset | Provider select. `off`: no pose server, the stand-in skeleton. Any other value (conventionally `external`) only overrides the headless skip: in headless runs the pose provider is skipped unless this or a valid `DC3_POSE_PERFORM` mode is set | `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_POSE_CAMERA` | unset: the choice saved in `$DC3_SAVE_DIR/pose_camera.txt`, else `auto` | Camera for the spawned pose server at launch, passed as `--camera`. When set it wins over the saved choice and the file is left alone (a change made in Options > Gameplay Settings > Camera during the run still applies and is saved). `auto` = first V4L2 video-capture device (OpenCV's camera 0 off Linux), re-resolved whenever the server reopens it; or an index (on Linux falls back to `auto` if that node is missing or not a camera), a `/dev/videoN` path, a `/dev/v4l/by-id` name, `id:<identity>` or a camera's name from `pose_server.py --list-cameras`. See POSE_INPUT.md "Which camera" and "The camera setting" | `native/src/platform/PoseCamera_Native.cpp` |
| `DC3_POSE_CAMERA_LIST` | unset | Test hook: a file holding the camera list in `pose_server.py --list-cameras tsv` form, read instead of running the lister, so a test does not depend on which cameras are plugged in (`OptionsCameraRowFlowTest`) | `native/src/platform/PoseCamera_Native.cpp` |
| `DC3_POSE_MODEL` | `<tree>/native/models/pose_landmarker_full.task`, found from the executable, not the cwd | MediaPipe model passed to the spawned server | `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_DUMMY_PLAYERS` | auto | `1` or `2`: how many standing stand-ins the sensor shows when no pose provider runs. Unset: one, two in two-player modes (`requires_2_players` / `is_in_party_mode`). Ignored while a provider runs | `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_POSE_SOCKET` | spawned server: `/tmp/dc3_pose_<pid>.sock`; with `DC3_POSE_NO_SPAWN`: `/tmp/dc3_pose.sock` | Unix socket shared with the pose server. Per process when the engine spawns the server, since `pose_server.py` unlinks whatever is at its path | `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_POSE_NO_SPAWN` | unset | Presence. Connect to an already-running pose server instead of forking `pose_server.py` | `native/src/platform/Skeleton_Native.cpp` |
| `DC3_POSE_HFOV` | unset | Horizontal camera FOV passed to the spawned server | `native/src/platform/Skeleton_Native.cpp` |
| `DC3_POSE_MASK` | `refined` | The person mask behind the player's silhouette (menu player boxes, gameplay box, the cube figure; PLAN P-20, POSE_INPUT.md "The mask, measured"). `refined` (default): `pose_server.py` steadies MediaPipe's mask across frames (motion-aware filter + hysteresis), fills tracked limbs the model dropped, closes pinholes and drops specks. `raw`: the mask as it was, MediaPipe's cut at 0.5 (the game passes `--mask raw`). Desktop only: the web build always sends the raw mask. Any other value: the default | `native/src/platform/Skeleton_Native.cpp` (`ServerCommand`), `native/scripts/pose_server.py` (`MaskRefiner`) |
| `DC3_POSE_HOLD_MS` | `1000` | Dropout hold (PLAN P-12): a person the detector loses is still reported, same slot and tracking id, every joint inferred at its last pose, for this much sensor time; then the slot is released and the image's own 1 s recovery window runs. `0` = off (lost at once). Not applied to someone seen under 500 ms (someone the identity layer has seen for 500 ms counts from her first frame back). With `DC3_POSE_REACQUIRE_MS=0` only, also ended when a new id appears within 0.3 m. Capped at 5000 | `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_POSE_REACQUIRE_MS` | `5000` | Player identity (PLAN P-18, POSE_INPUT.md "P-18"). How long a lost person is remembered. Within it: a detection where she was is her and keeps her tracking id whatever id the tracker gives it; a player whose skeleton was lost is bound again, same player, any mode, to her own id or to a body that appeared after she was lost within 0.3-0.75 m of where she stood; whoever the auto-assign takes meanwhile is a stand-in that gives her back. `0` = the old strict behaviour throughout: the tracker's id is the tracking id, no memory, a lost player 2 re-joins by raising a hand. Max 30000; negative or unparsable: the default. Read on every call | `native/src/platform/PoseIdentity_Native.h`, `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_POSE_FILL` | `extrap` | Frames between a slow camera's (POSE_INPUT.md "Frame fill"). Engaged only while the camera delivers under 24 frames a second by its own stamps (`DC3_POSE_FILL_RATE`), released once it has stayed over 27 for 10 s (`DC3_POSE_FILL_DWELL`), so a 30 Hz camera is untouched. `extrap` (default, **deliberately generous**, owner decision 2026-11-14): each camera frame at once, then a frame every 1/30 s stepped on by its joints' velocity x `DC3_POSE_FILL_GAIN` for at most one camera period; a guess (not-tracked joint) is not moved, joint speed is clamped to 4 m/s, a step that looks like a left/right swap is held, nothing after a frame with nobody, no velocity across a gap. Scores 1.05-1.10 of a real 30 Hz camera at 8-15 Hz on the footage. `interp`: the conservative mode, a lerp between camera frames with the stream 1.2 camera periods behind and that delay added to the scorer's latency (0.92 at 8 Hz, never above 30 Hz). `off` / `0`: camera frames only. `hold`, `extrapc`: measured alternatives. Any other value: the default, with a warning. Logs `DC3 POSE FILL: extrap (gain G) engaged at song S s (camera N frames/s, stream delay D ms …)`. Fill frames are never written by `DC3_POSE_RECORD` and are not counted in `pose_fps`. **While a song is played**; everywhere else: `DC3_POSE_FILL_MENU` | `native/src/platform/GestureMgr_Native.cpp`, `native/src/platform/PoseFill_Native.cpp` |
| `DC3_POSE_FILL_CLOCK` | `stall` | The clock the fill runs on. `stall` (default): the song clock, carried on at UI time once it has stood still for 0.25 s. Outside a song the song clock stops (the pause menu, between song previews, the results screens: 38 % and 47 % of the camera-frame intervals outside songs in two playtest recordings); with it stopped `extrap` and `hold` made no fill and `interp` delivers nothing at all, camera frames included, so no `DC3_POSE_FILL_MENU` mode but `off` works without it. (The synthetic Back cell first quoted for it, 56 of 73 selected on the song clock against 90 of 90 on this one, was a noise-free hand at 8 Hz meeting the arc detector's had-progress latch, which happens with the fill off too: [MENU_GESTURES.md](../native/reference/MENU_GESTURES.md) "The latch".) Inside a song it is the song clock plus a constant. `song`: the song clock alone, as before (for an A/B) | `native/src/platform/GestureMgr_Native.cpp`, `native/src/platform/PoseFill_Native.h` (`StallClock`) |
| `DC3_POSE_FILL_MENU` | `off` | The fill's mode when no song is being played: the menus, the pause menu, the results. `off` (default): the menus' gesture detectors get the camera's own frames. `interp`, `extrap`, `hold`, `extrapc`: as `DC3_POSE_FILL`; `same`: whatever `DC3_POSE_FILL` is. Why not the song's: replaying the 113 s around the 7 gesture selects of two recorded 7-11 fps playtests, `extrap` selected 2-4 times (never with the left hand), `off` 7 and 7, `interp` 7 and 5; on the synthetic performer at 8 Hz `extrap` moves the highlight under a resting hand in 12 of 96 reads, `off` in 0 of 96, for 92 % against 86 % selected at the first sweep (`interp`: 93 %, 0.2-0.3 s slower). Tables: [MENU_GESTURES.md](../native/reference/MENU_GESTURES.md). Logs `DC3 POSE FILL: off for the menus` / `extrap for the song` at each change; the camera's rate is kept across it. Any other value: the default, with a warning | `native/src/platform/GestureMgr_Native.cpp` (`FillSelectMode`), `native/src/platform/PoseFill_Native.h` |
| `DC3_MENU_SELECT` | `swipe` | How a hand picks a menu row. `swipe`: the original's forearm sweep, nothing changed. `hold`: the hand kept on a row picks it after `DC3_MENU_HOLD_SETTLE_MS` (300) + `DC3_MENU_HOLD_MS` (1200; the left hand's list, i.e. Back: `DC3_MENU_HOLD_LEFT_MS`, 1800), the list's slide showing the progress; a sweep no longer picks. `both`: either. After any pick, and on a list that appears under a hand already held out, a hold cannot start until the hand moves 0.10 of the list's height, drops for 0.3 s, or `DC3_MENU_HOLD_REARM_MS` (2500) passes. Never in controller mode or during a song. At run time: **F1** steps through the three, `{native_settings set_menu_select hold}` / `{native_settings cycle_menu_select}` / `{native_settings menu_select}` over `/api/dta/eval`; `scripts/play.sh --menu-select MODE`. Logs `DC3 MENU SELECT: ...`. `GET /api/menu/state` reports the mode and each list's `hold_progress` | `native/src/platform/MenuGesture_Native.cpp`, `src/system/hamobj/HamNavList.cpp` (`UpdateGestures`) |
| `DC3_MENU_ARC_START` | `tracked` | Which frames may start a menu sweep (the first node of `ArcDetector`'s path). `tracked` (default): the original's rule, wrist and elbow both Tracked. `inferred`: also a frame whose wrist and elbow the camera itself scored at least Inferred (MediaPipe visibility 0.3 or more) -- never a joint the camera lost and `DC3_POSE_ESTIMATE` placed, although the detectors see that one as Inferred too. At run time `{native_settings set_menu_arc_start inferred}` / `{native_settings menu_arc_start}` over `/api/dta/eval`, with a notice. Logs `DC3 MENU ARC START: ...`. `GET /api/menu/state` reports `arc_start` per arm: frames the detector asked to start on, those the original's rule refused, those started anyway. Synthetic performer with a wrist as poorly seen as in the worse GPD recording: first sweep 52-56 % under `tracked`, 81-87 % under `inferred`; on the recordings themselves it changed none of the recorded selects and added one. [reference/MENU_GESTURES.md](../native/reference/MENU_GESTURES.md) "The Tracked gate". |
| `DC3_POSE_FILL_RATE` | `camera` | What the fill reads the camera's rate from. `camera` (default): the camera's own stamps over the frames it delivered, so a game loop near the camera's rate does not read as a slow camera (a steady 30 Hz pose server under a 31 fps game engaged the fill 19 times in 401 s, logging `camera 22.5 frames/s`). Falls back to `arrival` by itself while the stream has no advancing stamps or the camera's clock and the fill's differ in pace by more than 1.5 (`DC3_FAST_TIME` with a camera on the wall clock). `arrival`: the time between the frames the game took, as before 2026-11-20 (for an A/B). Any other value: the default, with a warning. POSE_INPUT.md "Steady engagement" | `native/src/platform/GestureMgr_Native.cpp`, `native/src/platform/PoseFill_Native.h` |
| `DC3_POSE_FILL_DWELL` | `10` | Seconds (of the fill's clock) the camera must have stayed over the release rate, 27 frames a second, before the fill lets go; a reading back under it starts the wait again. `0`: release with the first reading over it, as before 2026-11-20. `0` .. `60`; anything else: the default, with a warning. Why 10: a camera wandering between 18 and 29 Hz changed the fill 6-30 times a minute with no dwell and once in 5 minutes with 6 s or more; the M3 Air's recorded tracker rates changed it 13 times with none and 7 with 10 s. Engaging is unchanged, so the fill is on wherever it was before. With `DC3_POSE_FILL_RATE=arrival` this is the fill before `np-pose-steady` | `native/src/platform/GestureMgr_Native.cpp`, `native/src/platform/PoseFill_Native.h` |
| `DC3_POSE_FILL_GAIN` | `1.0` | `extrap`'s strength, 0 (a hold for one period) .. 1 (the full linear step); outside [0, 1] or unparsable: the default, with a warning. The calibration knob: at 8 Hz gain 0.75 scores 0.99 of a real 30 Hz camera, at 15 Hz gain 0.5 scores 1.00 (score-vs-gain table in POSE_INPUT.md "Calibrating DC3_POSE_FILL_GAIN") | `native/src/platform/PoseFill_Native.cpp` |
| `DC3_POSE_ESTIMATE` | `kinematic` | Joints the camera cannot see (POSE_INPUT.md "Cut-off joints", PLAN H-91): a joint that arrives NotTracked (MediaPipe visibility < 0.3: below or beside the picture) is estimated and marked Inferred before the slot layer takes the person, desktop and web alike. `kinematic` (default, generous): a leg joint keeps the tracker's guess when it is plausible (a bone 0.5-1.6x the player's learned length), the head the guess's direction at the head length, else a floor + two-bone knee solve; a joint's last tracked pose is held relative to the body for ~0.35 s. Arms get the hold only. `tracker`: the tracker's guess, relabelled Inferred. `off` / `0`: the old behaviour, the joint frozen at its last position. Any other value: the default, with a warning. Logs `DC3 POSE ESTIMATE: kinematic (gain G) N joints a frame estimated` at the first estimate and every 30 s | `native/src/platform/PoseEstimate_Native.cpp`, `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_POSE_ESTIMATE_GAIN` | `1.0` | `kinematic`'s strength: 0 = the tracker's guess, 1 = the full estimate, linear between. Outside [0, 1] or unparsable: the default, with a warning | `native/src/platform/PoseEstimate_Native.cpp` |
| `DC3_POSE_STALL_MS` | `1500` | A pose source that stops sending frames while it still runs (a hung server, a blocked camera, a stalled web worker): after this much wall time without a frame the sensor reports nobody, one empty frame per 1/30 s, so P-12's hold and the image's skeleton-loss handling pause the song as for a player who left; an on-screen notice says so, and a second one when frames return -- only while a song is in play (`GamePanel` up, intro or playing); a stall elsewhere (song select between replayed songs) is logged with `[no song in play: no notice]` and never drawn. A source that has never sent a frame is not watched, so a camera that never delivers, and keyboard/pad play (`DC3_POSE=off`: no provider at all), never see it. `0` = off (the last frame stays tracked, the old behaviour). Capped at 60000. Logs `DC3 POSE STALL:` | `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_POSE_SERVER_STALL_MS` | `5000` | Desktop: a spawned `pose_server.py` that has sent no packet for this long is killed (SIGKILL) and restarted by the restart-on-death path (max 3 per start, with backoff). Until a server's first frame the limit is 30 s (or this, if longer): the server is connected before its model is loaded, and at 5 s an M3 Air's was killed four times in a row while MediaPipe loaded. `0` = never. Logs `Pose server stalled: no frame for N s` / `waiting for the first frame for N s` | `native/src/platform/Skeleton_Native.cpp` |
| `DC3_POSE_LATENCY_LOG` | off | `Dc3EnvFlag`. Every 5 s, `DC3 POSE AGE: capture->game median/p10/p90`: the age of a pose frame when the game takes it, from `pose_server.py`'s capture stamp (desktop only) | `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_POSE_PYTHON` | the game chooses | Interpreter the spawned pose server runs under. Unset, the game takes the first executable of: `${DC3_POSE_VENV:-~/.local/share/dc3/pose-venv}/bin/python3` (what `scripts/playtest/bootstrap.sh --fix` makes), the checkout's `.venv/bin/python3`, a worktree's main-checkout `.venv/bin/python3`, then `python3` from PATH. It logs `Pose server python: PATH (why)`; when that Python lacks mediapipe/opencv the notice names it and `bootstrap.sh --fix`. `scripts/playtest/pose_python.py` is the same order (preflight checks the game's choice) | `native/src/platform/Skeleton_Native.cpp` (`ChoosePosePython`) |
| `DC3_POSE_VENV` | `~/.local/share/dc3/pose-venv` | The playtest venv the game looks for (step 2 above); replaces the default rather than adding to it | `Skeleton_Native.cpp` (`ChoosePosePython`), `scripts/playtest/bootstrap.sh` |
| `DC3_POSE_SERVER` | `<tree>/native/scripts/pose_server.py` | A drop-in for `pose_server.py`, started with the same arguments (`scripts/playtest/footage_camera.py` is one) | `native/src/platform/Skeleton_Native.cpp` |
| `DC3_POSE_VIDEO` | unset | The spawned server reads this video file (looped) instead of `--camera N` (`scripts/play.sh --video`) | `native/src/platform/Skeleton_Native.cpp` (`ServerCommand`) |
| `DC3_POSE_RECORD` | unset | Write every sensor frame the game accepts, as the scorer receives it (persons, frame id, timestamp, song time, floor normal), to this dc3-pose-session v1 file; flushed per frame. `scripts/play.sh` sets it to `<session>/pose.dc3session` | `native/src/platform/PoseSession_Native.cpp` |
| `DC3_POSE_RECORD_VIDEO` | unset | The spawned server (`--record-video`) also writes every frame it gives MediaPipe to this video (mp4v; `.mkv` survives a kill) plus `<stem>.frames.tsv`, each frame's packet id and capture `ts` (they join this session's `pose.dc3session`), on its own thread: a full queue drops and counts a frame, never waits. Never overwrites: a restarted server writes `<stem>-2.<ext>`. `scripts/play.sh --record-video` / `DC3_PLAY_RECORD_VIDEO=1` in `~/.config/dc3/playtest.env` set it to `<session>/camera.mkv`; `scripts/playtest/camera_video.py` reads it | `native/src/platform/Skeleton_Native.cpp` (`ServerCommand`), `native/scripts/video_recorder.py` |
| `DC3_POSE_RECORD_VIDEO_MAX_GB` | unset (24) | With `DC3_POSE_RECORD_VIDEO`: stop recording when the parts reach this many GB (`--record-max-gb`); it also stops below 10 GB free | `native/scripts/video_recorder.py` |
| `DC3_POSE_SESSION_SYNC` | `song` | With `DC3_POSE_PERFORM=session:FILE`: `song` publishes each recorded frame at its recorded song time (and uses its recorded floor normal); `wall` at its recorded wall offset | `native/src/platform/PoseSession_Native.cpp` |
| `DC3_POSE_PERFORM_STALL` | unset | With `DC3_POSE_PERFORM`: `from,secs` publishes nothing from song second `from` for `secs` of wall time, a hung pose source; the stall watchdog (`DC3_POSE_STALL_MS`) should then pause the song. Tested by `PoseStallTest` | `native/src/platform/PosePerformer_Native.cpp` |
| `DC3_PERF_LOG` | off | Every 5 s: `DC3 PERF: fps= pose_fps= persons=` (game frames/s, sensor frames accepted/s, mean people) | `native/src/platform/GestureMgr_Native.cpp` |
| `DC3_POSE_STATS` | off | Read by `pose_server.py`: every 5 s `POSE_STATS: fps= infer_ms_mean= infer_ms_p95= persons= sent= dropped= window_s= camera_fps=` on stderr (`fps` = frames processed, `camera_fps` = frames the camera delivered; absent with `--no-capture-thread` or a video file) | `native/scripts/pose_server.py` |
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
| `DC3_CROWD_STATS` | unset | `N`: every N render frames, one `DC3_CROWD: frame=… draws3d=… impostors=… pose=… ms=… shadows=…` line on stderr (3D crowd draws, impostor renders, checksum of the drawn crowd poses, wall time inside the crowd's draw, contact shadows the look asked for). Draws nothing differently; read by `CrowdTest` | `src/system/world/Crowd.cpp` |
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
| `MILO_BACKGROUND_AUDIO` | unset | `1` keeps audio playing when the window is hidden. Unset: the output is muted while the window is hidden and starved (a compositor that stops presenting a hidden window drops the game to about one frame a second, and the music then plays in bursts). Two signals, either is enough: the window has no focus and no frame has finished for 500 ms; or the game has been inside one wait on the compositor (acquiring or presenting its frame) for 500 ms, focus or not (Hyprland leaves a window focused when you switch to an empty workspace). It comes back on the first frame that did not wait on the compositor while the window has focus, or after 30 full-rate frames without focus. Never on the web or headless. Measured on the GPD (Hyprland, Xwayland), 2026-11-15: hidden behind a workspace with a focused window, muted and back; hidden on an empty workspace, the first signal alone did not mute | `../milo-native-engine/src/audio/BackgroundMute.h`, `native/tests/test_background_mute.cpp` |
| `DC3_DUMP_AUDIO` / `DC3_DUMP_SECONDS` | unset | Capture the mixed PCM to a file | `../milo-native-engine/src/audio/AudioDevice.cpp` |

### Rendering and UI diagnostics

| Variable | Default | Effect | Read in |
|----------|---------|--------|---------|
| `MILO_CLEAR_COLOR` | unset | `R,G,B,A` clear colour override | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp`, `src/App.cpp` |
| `MILO_SIMPLE_RENDER` | unset | Presence. Simplified material rendering | `../milo-native-engine/src/platform/MaterialSetup.cpp` |
| `MILO_GPU_LABELS` | auto | `1`: create the device with Dawn's `use_user_defined_labels_in_backend` toggle, so every Vulkan object carries its WebGPU label (readable names in RenderDoc / validation messages). `0`: never. Unset: on only when a Vulkan capture or debug layer is requested (`VK_INSTANCE_LAYERS`, `VK_LOADER_LAYERS_ENABLE`, `ENABLE_VULKAN_RENDERDOC_CAPTURE`), off otherwise — naming each object cost 5–8% of the main thread in menus. The log says `GpuDevice: backend object labels ON` when it is on | `../milo-native-engine/src/gfx/GpuDevice.cpp` |
| `MILO_GPU_TIMERS` | unset | `1` (truthy): GPU time per render pass from WebGPU timestamp queries (PLAN H-96). The device asks for `timestamp-query` only if the adapter offers it (device creation never fails over it) and disables Dawn's `timestamp_quantization` toggle, read back from the device: the log says `GPU timers ON (...) timestamp_quantization off (device confirms)`. Every pass is named by its descriptor label (`MainPass`, `MainPassResume`, `TexturePass`, `HudOverlayPass`, `RB3RetailBloom`, `DC3RetailPostComposite`, `DC3Velocity`, `DisplayRampPass`, `ProjShadowCasters`, ...; a pass begun without one reads `unnamed@<file>:<line>`), same-name passes in a frame add up. Every ~1 s: `[GPUTIME] window=… frames=… skipped=… gpu_sum_ms=… gpu_span_ms=… ts_zero_low_bits=…` (sum of pass times, and first begin to last end, per frame) then one `[GPUTIME] pass=<name> ms=<mean per frame> max=<max per frame> n=<passes per frame>` per name, largest first. The latest window is `GET /api/gputime`. Readback is asynchronous and never waited on: a frame that finds all 4 readback buffers in flight is not timed (`skipped`). No adapter feature / the web build: one `GPU timers unavailable: <reason>` line, nothing else. Unset: no query set or buffer exists. A/B one switch on the GPU inside one gameplay run: `scripts/playtest/gpu_pass_ab.py` (GPU lock, sandbox off) | `../milo-native-engine/src/gfx/GpuTimers.cpp` |
| `MILO_GPU_COUNTERS` | unset | `N`: a `[GPUCOUNT]` line every N frames (`0` or non-numeric: every 600) with the bind-group cache (live groups, created, hits, evicted, uncached), the four uniform rings (total bytes, grows per ring, `WriteBuffer` calls and bytes so far), cached pipelines, live GPU meshes and textures, and the post chain's own cache (`postBgLive/Created/Hits/Evicted/Uncached`: the bloom, DoF, composite, velocity and ramp groups, PLAN H-66; a steady gameplay frame creates none). Growth across songs is a leak (PLAN H-30/H-31) | `../milo-native-engine/src/platform/Rnd_Wgpu.cpp` |
| `MILO_GPU_PASS_TRACE` | unset | `N`: one `[GPUPASS]` line every Nth timed frame (needs `MILO_GPU_TIMERS=1`) with the frame's render passes in order, each with what it does to colour 0 and depth (`L` load, `C` clear, `S` store, `D` discard, `+r` resolve). On a tile-based GPU every load and store of a frame-sized attachment is a copy between memory and the tiles, so the order and count of passes is the cost (PLAN H-124) | `../milo-native-engine/src/gfx/GpuTimers.cpp` |
| `MILO_DRAW_INVISIBLE` | unset | `1` (truthy): send the mesh draws that cannot change a pixel (material alpha 0, alpha blend, no depth, stencil or bloom-mask write), as before PLAN H-125. The `[PERF]` line counts the draws left out. DC3 only | `../milo-native-engine/src/platform/MeshGpuCache.cpp`, `../milo-native-engine/src/platform/Mesh_Wgpu.cpp` |
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
| `DC3_RND_OVERLAYS` | dev profile `log` (since np-notifyband; was `1`), release off; `scripts/play.sh`: `log` | `1`: draws the original's `RndOverlay`s (`cheat_display` = every `MILO_NOTIFY`, quick-cheat stats) in a top-left ImGui window and logs `DC3 OVERLAY [<name>] <text>` when one changes (the first drawn frame also logs `DC3 OVERLAY: drawn on screen`). `log` (the default): the same log lines, nothing drawn -- what a player wants: no developer notice over the picture, every line still in `engine.log`. `play.sh --overlays` (or the caller's `DC3_RND_OVERLAYS=1`) draws them again. `0`/unset: off | `native/src/platform/DebugPanel.cpp`, `src/system/rndobj/Overlay.cpp` |

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

**Two clocks, two rates.** In fast time the game-seconds clock (`{taskmgr delta_seconds}`) advances 1/120 s per frame, while a headless engine's UI clock (`{taskmgr ui_delta_seconds}`) advances 1/30 s per frame. Anything timed in game seconds therefore takes four times as many frames as the UI animation around it. Measured 2026-11-20 on the XP reward screen (OMG, easy, choreo performer):

- A 0.2 s `FlowTimer` at rate 0 (game seconds) in `play_hide_frame_sound.flow` is still pending when the panel unloads 8 frames after `hide_frame.flow` starts. Its task then logs `MILO_NOTIFY: EventTask::Poll NULL mOwner` and deletes itself: 3 such lines in the fast-time run, 0 in a real-time run of the same song. That line in a fast-time log is not a lead.
- A duration read off the wall clock in an unpaced headless run is not a game duration. The reward screen holds 128 to 131 frames (4.3 to 4.4 s of UI time) on every run, and an unpaced run with a GPU showed it for about one second. Count frames (`/api/frame`), or pace the engine at 30 fps (`PUT /api/pace?fps=30`), before calling a screen short.

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
wakes controller mode (and is swallowed unless it is B) and the rest
navigate. What differs from the 360 is how the hands get the UI back, **the
press hold** (`System_Native.cpp`, PLAN H-157). The image hands it back 5 s
after the last press; a webcam at a desk sees the arm of the person at the
keyboard cut by the edge of the picture, and that arm then moved the
highlight and selected rows. So under `faithful`:

- an idle exit that follows a press waits `DC3_CONTROLLER_HOLD_MS` (default
  300000) instead of the helpbar's 5000. The exit line logs the timeout in
  force (`timeout_ms=300000`). The handover when a camera comes up with no
  key touched is still the helpbar's 5 s; a key touched within the hold while
  the policy was still `forced` keeps the menus through it
  (`Dc3ControllerHoldNoteHandover`);
- raising both hands (the image's `HandsUpGestureFilter`, 1.2 s, at least
  1.5 s after the last press) leaves controller mode at once, logged as
  `DC3 ControllerMode: both hands raised, the hands take the menus`. That
  raise does not also open the correct-identity overlay; the next one does,
  as in the image;
- a notice names the way back the first three times a press takes the menus,
  and one confirms the hands have them again;
- Start, a song starting and the scripts' own exits (loading, the results)
  are the image's, unchanged.

`DC3_CONTROLLER_HOLD_MS=0` turns all of it off (the image's 5 s, and hands up
does what it does in the image). A run that sets
`DC3_CONTROLLER_MODE_TIMEOUT_MS` and not the hold gets that value for both, so
its timing is what it was. `scripts/controller_hold_check.py` checks it end
to end with a synthetic player (`ControllerHoldTest`, and
`ControllerHoldSabotageTest` with the hold off). A button-driven tool
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
