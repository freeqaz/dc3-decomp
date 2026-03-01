# Porting Analysis: DC3 → x86_64 Native

Comprehensive analysis of what it takes to get DC3 compiling and running on
x86_64 Linux. Based on exploration of the full decomp source tree.

## Codebase Scale

| Metric | Count |
|--------|-------|
| .cpp files | ~874 |
| .h files | ~1,389 |
| Total lines | ~52,000 |
| Xbox build objects | 2,211 (across 10 libraries) |

**Top directories by size:**
- `lazer/meta_ham/` — 251 files (metagame UI, panels, achievements)
- `system/rndobj/` — 174 files (Milo rendering objects)
- `system/hamobj/` — 156 files (DC3 game objects)
- `system/utl/` — 147 files (utilities, memory, containers)
- `system/synth/` — 122 files (sound synthesis)
- `system/char/` — 119 files (character animation, IK)
- `system/os/` — 101 files (platform abstraction, I/O, threading)
- `system/synth_xbox/` — 83 files (Xbox audio: XAudio2, Kinect mic)
- `system/ui/` — 72 files (UI framework)
- `system/gesture/` — 72 files (Kinect skeleton, gesture recognition)

## What Already Works

- **Endianness**: Already abstracted in `src/system/os/Endian.h`. Xbox is
  big-endian PPC, file formats store BE. Swaps happen on load — works as-is
  for little-endian x86_64. Confirmed: archive loading, DTA parsing, .milo
  loading all work correctly through the existing endian layer.
- **Calling conventions**: `macros.h` already defines `CDECL`, `STDCALL`,
  `FASTCALL` as empty on non-MSVC compilers.
- **SEH**: `macros.h` maps `SEH_TRY`/`SEH_EXCEPT`/`SEH_FINALLY` to C++ try/catch.
- **Win32 types**: `src/xdk/win_types.h` provides all Win32 typedefs (`DWORD`,
  `HANDLE`, `HRESULT`, `BOOL`, `LARGE_INTEGER`, etc.) using the project's own
  `types.h` as foundation. These are just typedefs — portable as-is.
- **Third-party libs**: zlib, curl, json-c, libjpeg, ogg/vorbis, SoundTouch
  are all vendored in `src/` and cross-platform.
- **Dawn/WebGPU**: Built and proven headless (see `native/`).
- **DataArray scripting**: DTA parser/interpreter works on x86_64 after LP64 fixes.
  Config files parse and execute correctly.
- **Archive system**: Loads main_xbox.hdr (6,377 files, 10 ark files) correctly.
- **CharClip/skeleton loading**: .milo files with skeleton clips load and parse.

## LP64 Issues (ILP32 → LP64 Type Model) {#lp64-issues}

**This was the single biggest source of runtime bugs.** The decomp was written for
Xbox 360's ILP32 model where `int = long = pointer = 4 bytes`. On x86_64 Linux
(LP64), `long = pointer = 8 bytes` while `int = 4 bytes`.

All fixes use `#ifdef HX_NATIVE` guards to avoid affecting the decomp build.

| Issue | File | Symptom | Fix |
|-------|------|---------|-----|
| `u32 = unsigned long` is 8 bytes | `types.h` | `sizeof(Vector3) = 24` instead of 16, struct layout corruption | `u32 = unsigned int` under HX_NATIVE |
| BinStream duplicate `operator<<(u32)` | `BinStream.h` | Compile error: u32=uint on native | `#ifndef HX_NATIVE` around u32 operators |
| Missing `unsigned long` BinStream ops | `BinStream.h` | `bs << map.size()` has no match (size_t=ulong) | Added `#ifdef HX_NATIVE` unsigned long ops |
| `DataNode(unsigned int)` ambiguous | `Data.h` | Compile error: u32 (uint) has no exact DataNode ctor | Added `#ifdef HX_NATIVE` constructor |
| `(int)ptr` truncation in CharClip | `CharClip.cpp/h` | Pointer arithmetic wraps/truncates | `(intptr_t)` casts |
| NodeVector temp alloc too small | `CharClip.cpp` | ObjOwnerPtr is ~40 bytes (not 20), heap overflow | Scale allocation 4x on native |
| Uninitialized `_MemAllocTemp` memory | `CharClip.cpp` | Garbage ObjOwnerPtr fields, crash in SetObjConcrete | `memset(start, 0, allocSize)` |
| `Resize()` pointer arithmetic bug | `CharClip.cpp` | `mNodeStart + size` multiplies by sizeof(NodeVector) | `(char*)mNodeStart + size` (byte arithmetic) |
| `TrigTableInit` OOB write | `Trig.cpp` | gBigSinTable[513] writes past 512-element array | Guard `if (i * 2 + 1 < 0x200)` |
| `Release(nullptr)` crash | `Object.h` | ObjRef::Release dereferences null | Null check under HX_NATIVE |
| XDK callback type mismatches | Various | `DWORD` (uint) vs `unsigned long` (8 bytes) return types | Skip threading calls under HX_NATIVE |

**Key insight**: The Resize() bug is an actual decomp bug — it happens to be benign
on Xbox where sizeof(NodeVector) alignment makes the math work, but is fundamentally
wrong (pointer arithmetic on byte counts). Fixed only under HX_NATIVE to not affect
decomp matching.

**Pattern**: Any code that casts pointers to `int`/`u32` or uses `int` for pointer
arithmetic is suspect on LP64. Search for `(int)` casts near pointer variables.

### Non-Virtual Thunks (Itanium ABI)

GCC/Clang's Itanium C++ ABI generates `this`-pointer adjustment thunks for virtual
functions in multiply-inherited classes. The decomp's asm-label stubs provide mangled
symbols but don't generate these thunks. Solution: `native/src/thunk_stubs.cpp` with
proper C++ method definitions that the compiler can generate thunks for.

24 functions needed thunk stubs. Return types must be exact (void/float/bool/int)
for ABI correctness.

### ChunkStream Limitations

ChunkStreams (compressed .milo files) are **forward-only**. `Seek()` backwards
corrupts stream state. This means you cannot:
- Peek at data and then seek back
- Hex-dump a range and seek back to re-read it
- Use any read-then-rewind debugging pattern

This caused a subtle bug during debugging when hex-dump code broke clip loading.

## What Needs Porting

### 1. XDK Headers (`src/xdk/`)

The `xdk/` directory provides stub/shim headers for the Xbox SDK. Currently
these define the real Xbox API — for native port, they need to provide
POSIX equivalents.

| Header | What it provides | Port strategy |
|--------|-----------------|---------------|
| `XBOXKRNL.h` | `RTL_CRITICAL_SECTION`, thread/sync functions | → `pthread_mutex_t` |
| `XAPILIB.h` | `CreateThread`, `WaitForSingleObject`, file ops | → pthreads, POSIX |
| `D3D9.h` / `XGRAPHICS.h` | Direct3D 9, Xbox GPU | → WebGPU (Dawn) |
| `XAUDIO2.h` | XAudio2 sound | → miniaudio or stub |
| `NUI.h` | Kinect skeleton/camera | → stub (no-op) |
| `XBC.h` / `XSOCIAL.h` / `XONLINE.h` | Xbox Live | → stub (no-op) |
| `XNET.h` | Xbox networking | → stub or POSIX sockets |
| `XMP.h` | Media playback | → stub |
| `win_types.h` | Win32 typedefs | Already portable (just typedefs) |

### 2. Platform-Specific Source Files (32 files)

These need stub/replacement implementations:

**Must implement (critical path):**
- `Memory_Xbox.cpp` → `Memory_Native.cpp` (use malloc/free)
- `system/os/PlatformMgr_Xbox.cpp` → `PlatformMgr_Native.cpp` (minimal)
- `system/os/System_Xbox.cpp` → `System_Native.cpp`
- `system/os/Joypad_Xbox.cpp` → `Joypad_Stub.cpp` (no-op for now)

**Must stub (can be no-op):**
- `system/gesture/LiveCameraInput.h` → return "no Kinect"
- `system/synth_xbox/Synth.cpp` → silent audio
- `system/os/Memcard_Xbox.cpp` → filesystem save
- `system/os/ContentMgr_Xbox.cpp` → no DLC
- `system/net/*_Xbox.cpp` → no online
- `system/meta/Achievements_Xbox.cpp` → no achievements
- `system/meta/MemcardMgr_Xbox.cpp` → filesystem

### 3. Threading Primitives

`CriticalSection` in `src/system/os/CritSec.h` wraps Xbox `RTL_CRITICAL_SECTION`
(0x1C bytes). For native port:

```
RTL_CRITICAL_SECTION → pthread_mutex_t (or std::recursive_mutex)
RtlInitializeCriticalSection → pthread_mutex_init
RtlEnterCriticalSection → pthread_mutex_lock
RtlLeaveCriticalSection → pthread_mutex_unlock
RtlTryEnterCriticalSection → pthread_mutex_trylock

CreateThread → pthread_create (or std::thread)
WaitForSingleObject → pthread_join / condition variable
CreateEventA / SetEvent → pthread_cond_t
XSetThreadProcessor → pthread_setaffinity_np (or no-op)
```

Key files: `CritSec.h`, `ThreadCall_Win.cpp`, `SynchronizationEvent.cpp`

### 4. File I/O

`File` base class (`src/system/os/File.h`) is a clean virtual interface.
Platform-specific: `AsyncFile_Win.h` uses Windows file handles.

For native: implement using POSIX `open`/`read`/`write`/`lseek` or `fopen`.
The `Archive` class (`.ark` file reading) is mostly platform-neutral.

### 5. Memory Management

`MemHeap` (`src/system/utl/MemHeap.h`) and `MemMgr` are custom allocators.
`Memory_Xbox.cpp` uses `XPhysicalAlloc`/`XPhysicalFree`.

For native: simplest path is redirect to `malloc`/`free`. The custom heap
system can be kept as-is for allocation tracking, just backed by standard
allocator instead of Xbox physical memory.

### 6. Rendering (Biggest Task)

Class hierarchy: `Rnd` → `NgRnd` → `DxRnd` (D3D9 Xbox)

For native: `Rnd` → `NgRnd` → `WgpuRnd` (WebGPU via Dawn)

Key virtual methods to implement:
- `PreInit()` / `Init()` / `Terminate()`
- `BeginDrawing()` / `EndDrawing()`
- `Clear(flags, color)`
- Texture, mesh, material, shader management

36 files in `system/rnddx9/` need native replacements.

### 7. Audio

`Synth` class → `XAudio2` on Xbox. 205 files across `system/synth/` and
`system/synth_xbox/`.

For native: either miniaudio, SDL2 audio, or FMOD. Can stub entirely
for initial boot (silent).

## Engine Boot Sequence

From `src/App.cpp`:

```
main() → App(argc, argv) → app.Run()

App::App():
  1. SystemPreInit("config/ham_preinit_keep.dta")  // Archive + early config
  2. TheRnd.PreInit()                               // Renderer pre-init
  3. Splash::Init()                                  // Splash videos
  4. LiveCameraInput::PreInit() + Init()             // Kinect (stub for native)
  5. SystemInit("config/ham_keep.dta")               // Main config
  6. TheRnd.Init()                                   // Full renderer init
  7. SynthInit()                                     // Audio
  8. FlowInit()                                      // Flow graph engine
  9. CharInit()                                      // Character animation
  10. WorldInit()                                     // Physics, cameras
  11. HamInit()                                       // DC3 game objects
  12. GameInit()                                      // Game modes
  13. TheUI->Init()                                   // UI system
  ... (20+ more subsystem inits)

App::RunWithoutDebugging() (main loop):
  while (true):
    SystemPoll()
    TheSynth->Poll()
    TheGestureMgr->Poll()
    TheUI->Poll()
    TheTaskMgr.Poll()
    TheFlowMgr->Poll()
    DrawRegular() or CaptureHiRes()
```

## Porting Effort Estimate

| Component | Files | Effort | Status | Notes |
|-----------|-------|--------|--------|-------|
| CMake build system | 1 | HIGH | **DONE** | `native/CMakeLists.txt`, ~874 files |
| XDK header shims | ~15 | MEDIUM | **DONE** | POSIX wrappers + `#ifdef HX_NATIVE` |
| Platform stubs | ~32 | MEDIUM | **DONE** | `native/src/platform/` |
| LP64 type fixes | ~8 | **HIGH** (surprise) | **DONE** | Biggest runtime bug source |
| Thunk stubs | 1 | MEDIUM | **DONE** | 24 Itanium ABI thunks |
| Threading | ~5 | LOW | **DONE** | Skipped (sync fallback) |
| File I/O | ~5 | LOW | **DONE** | POSIX file ops |
| Memory | ~3 | LOW | **DONE** | malloc/free redirect |
| Rendering | ~36 | VERY HIGH | **Tier 1 DONE** | WebGPU/Dawn — meshes, textures, lighting working |
| Audio | ~205 | HIGH | Stubbed | XAudio2 → miniaudio (stub first) |
| Kinect/Gesture | ~72 | LOW | **DONE** | Stubbed, no-op |
| Xbox Live/Social | ~20 | LOW | **DONE** | Stubbed, no-op |
| Game logic | ~500+ | NONE | Works | Platform-independent |
| Object lifecycle | ? | MEDIUM | **DONE** | Load stubs implemented, clean exit |
| Iterator/pointer compat | 1 | MEDIUM | **DONE** | Patched `__normal_iterator` in shadow header |

**Surprise effort**: LP64 type model issues were NOT anticipated and caused the most
runtime debugging time. The original estimate didn't account for ILP32→LP64 struct
layout changes, pointer truncation, and template instantiation differences.

**Second surprise**: Iterator/pointer compatibility. MSVC's `vector::begin()` returns
`T*`; libstdc++ returns a wrapper class. 605 call sites affected. Required patching
a system header (shadow copy with one-line addition).

**Critical path to title screen:**
1. ~~CMake build (compile everything)~~ **DONE**
2. ~~XDK shims + platform stubs (link everything)~~ **DONE**
3. ~~File I/O + Archive loading (load game data)~~ **DONE**
4. ~~Fix object loading crashes (ChunkStream, RndTex, Font)~~ **DONE**
5. ~~Fix iterator/pointer compatibility (patched stl_iterator.h)~~ **DONE**
6. Implement remaining stubbed Load functions for full object parsing
7. Get into main loop (currently exits after loading)
8. Stub renderer that shows title screen texture
9. Screenshot → `we-did-it-wtf.png`

## Boot Progress (Sessions 3-6)

Engine successfully boots through:

1. **Archive loading** — `main_xbox.hdr`, 6,377 files, 10 ark files
2. **Config/DTA parsing** — ham_preinit_keep, macros, sfx_macros, etc.
3. **SystemInit** — subsystem initialization begins
4. **CharClip/skeleton loading** — skeleton_clips.milo loads 14 clips successfully
5. **DirLoader::LoadObjs** — .milo object loading (Tex, Font, Text, etc.)
6. **Subsystem inits** — FlowInit, CharInit, WorldInit, HamInit all complete
7. **TheUI->Init()** — starts loading shared subdirs

**Current work**: Stream desync in `UIManager::Init()` → `PreloadSharedSubdirs()`.
Root cause identified: nested ObjectDir objects (like `boxyman` in
`timey_wimey_elements.milo`) use DirLoader-format data instead of PreLoad/PostLoad
format. Added peek-and-unreread detection to spawn sub-DirLoaders for these. Also
implemented `DrivenPropertyEntry::Load` and `FlowMathOp::Load` (were stubbed, causing
desync in FlowAnimate objects). Multiple defensive guards prevent crashes from
residual desync. See [STREAM_DESYNC.md](docs/plans/custom-graphics-engine/STREAM_DESYNC.md).

### Session 7-8: Stream Desync

**Nested ObjectDir detection**: Objects like `boxyman` (RndDir) inside
`timey_wimey_elements.milo` have DirLoader-format data (starts with mRev=32)
instead of PreLoad/PostLoad format (starts with packed class revision). Added
peek-and-unreread mechanism: read 4 bytes, unreread via `ChunkStream::Unreread()`,
check if it's a DirLoader mRev (upper 16 bits zero, value > 28). If so, create
a sub-DirLoader to consume the nested container data.

**Stubbed Load functions found**: `DrivenPropertyEntry::Load` and `FlowMathOp::Load`
were no-op stubs in `engine_stubs_generated.cpp`. FlowAnimate objects contain
DrivenPropertyEntry arrays — the stubs consumed zero bytes, causing all subsequent
objects in the stream to read garbage. Implemented both from their symmetric
`::Save` methods.

**Defensive guards**: Added `#ifdef HX_NATIVE` revision/size caps in Object::LoadType,
Object::LoadRest, FlowMathOp::Load, DrivenPropertyEntry::Load, FlowNode::Load, and
BinStream string operators. These prevent crashes but don't fix underlying desync.

See [STREAM_DESYNC.md](STREAM_DESYNC.md) for full details.

### Session 5-6 Breakthroughs

**Font3d vtable corruption**: `RndFont3d` had all-zero vtable (384 bytes). Root cause:
Itanium ABI key function (`Handle()`) was declared but never defined in `Font3d.cpp`.
Linker fell back to weak zero-filled stub in `engine_stubs_generated.cpp`. Fix: added
`BEGIN_HANDLERS(RndFont3d)` and all other virtual function stubs to `Font3d.cpp`.

**Milo Viewer operational**: Standalone viewer (`native/build/milo-viewer`) loads and
renders .milo_xbox files via WebGPU. 15/17 props render correctly (2 blank — likely
transparency-dependent). Proves core milo loading + rendering pipeline works.

**Systemic vtable issue identified**: 44 classes have zero-filled vtable stubs and
18 have zero-filled typeinfo stubs in `engine_stubs_generated.cpp`. Any class that
gets instantiated during a .milo load without its key function defined will have broken
virtual dispatch. This is the same root cause as Font3d and likely contributes to the
stream desync (broken Load/PreLoad/PostLoad → wrong byte count consumed → stream drift).

The engine gets surprisingly far. Most game logic is platform-independent and
"just works" once the LP64 type issues are fixed.

### Session 4 Breakthroughs

**ChunkStream infinite loop**: Stubbed `RndTex::PreLoad`/`PostLoad` left texture
data unconsumed in compressed .milo streams. `ReadDead()` would scan forever looking
for the 0xADDEADDE sentinel. Fix: real `RndTex::PreLoad` and `PostLoad` that consume
the correct byte counts (`native/src/platform/RndTex_Native.cpp`).

**Font loading garbage**: `RndFontBase::Load` was stubbed as a no-op, so the stream
position never advanced past font data. All subsequent objects read garbage. Fix:
real `RndFontBase::Load` implementation in `FontBase.cpp` (based on symmetric
`Save` format). 3 fonts in `eagle_light_heavy.milo` now load correctly.

**Iterator/pointer compatibility (MSVC STL shim)**: MSVC's old STL returns raw `T*`
from `vector::begin()`; modern libstdc++ returns `__normal_iterator<T*>`. 605 call
sites across 174 files assumed raw pointers. Multiple approaches tried:

| Approach | Result |
|----------|--------|
| Per-file `#ifdef HX_NATIVE` | Works but unscalable (605 sites) |
| `#define vector msvc_compat::vector` | Breaks: `std::vector` contains `vector` token |
| libc++ with patched `__wrap_iter` | Iterator works but 42 stubs use libstdc++ mangling |
| libstdc++ free operator overloads | Handles comparison/arithmetic, NOT assignment |
| **Patched `__normal_iterator`** | **One-line fix, all 605 sites** |

**Solution**: Shadow copy of libstdc++ `bits/stl_iterator.h` in `native/include/bits/`
with a single addition: `operator _Iterator() const noexcept { return _M_current; }`
to `__normal_iterator`. This adds implicit conversion from iterator to raw pointer,
matching MSVC behavior. Combined with `include_directories(BEFORE SYSTEM ...)` in
CMakeLists.txt, the patched header takes priority over the system header.

**Current state**: Binary compiles (790 targets), links, and runs. Archive loads
correctly, .milo objects parse, clean exit.

### Build and Run Commands

```bash
# Build
cd native/build && cmake --build . -j$(nproc)

# Run (from project root, where .ark files are accessible)
cd /home/free/code/milohax/dc3-decomp && ./native/build/dc3-native

# Enable AddressSanitizer (uncomment in native/CMakeLists.txt):
#   add_compile_options(-fsanitize=address)
#   add_link_options(-fsanitize=address)
```

### Key Modified Files for Native Port

| File | Change |
|------|--------|
| `src/types.h` | LP64-safe u32/s32 typedefs |
| `src/system/utl/BinStream.h` | u32 operator guards + unsigned long ops |
| `src/system/obj/Data.h` | DataNode(unsigned int) ctor |
| `src/system/obj/Object.h` | Release null guard, ObjVector sanity check |
| `src/system/char/CharClip.cpp` | LP64 pointer fixes, allocation scaling |
| `src/system/char/CharClip.h` | BytesInMemory intptr_t |
| `src/system/math/Trig.cpp` | OOB write guard |
| `src/system/os/Debug.cpp` | Skip SetUnhandledExceptionFilter |
| `src/system/movie/Splash.cpp` | Skip threaded splash |
| `src/system/utl/ChunkStream.cpp` | Skip decompression thread, ReadImpl EOF guard |
| `src/system/gesture/SkeletonUpdate.cpp` | Skip Kinect thread |
| `src/system/gesture/GestureMgr.cpp` | Skip Kinect init |
| `src/system/rndobj/FontBase.cpp` | Real `RndFontBase::Load` implementation |
| `src/system/utl/Locale.cpp` | Native-only destructor |
| `src/system/rndobj/MultiMeshProxy.cpp` | list iterator default construction fix |
| `src/system/world/Crowd3DCharHandle.cpp` | list iterator default construction fix |
| `native/include/bits/stl_iterator.h` | **Patched shadow header**: `operator _Iterator()` on `__normal_iterator` |
| `native/src/platform/RndTex_Native.cpp` | Real `RndTex::PreLoad`/`PostLoad` consuming stream data |
| `native/src/thunk_stubs.cpp` | C++ stubs for 24 non-virtual thunks |
| `native/src/engine_stubs_generated.cpp` | Weak symbol stubs (reduced as real impls added) |
| `native/CMakeLists.txt` | Full engine build config, shadow include dirs |

## Key Source Files

| File | Role |
|------|------|
| `src/Main.cpp` | Entry point |
| `src/App.h` / `App.cpp` | Application lifecycle, init sequence |
| `src/types.h` | Base types (u8, u16, u32, etc.) |
| `src/macros.h` | Platform macros (CDECL, SEH, etc.) |
| `src/xdk/win_types.h` | Win32 type definitions |
| `src/xdk/XBOXKRNL.h` | Critical section, thread functions |
| `src/system/os/CritSec.h` | Mutex wrapper |
| `src/system/os/File.h` | File I/O interface |
| `src/system/os/Endian.h` | Byte swap utilities |
| `src/system/os/Platform.h` | Platform enum |
| `src/system/os/System.h` | System init/poll |
| `src/system/utl/MemMgr.h` | Memory allocation |
| `src/system/rndobj/Rnd.h` | Renderer base class |
| `src/system/synth/Synth.h` | Audio base class |
| `config/373307D9/objects.json` | Full file list (2,211 objects) |
| `configure.py` | Current build system (reference) |
