# Missing Function Definitions — Native Port

## Problem

The native port links with `-Wl,--unresolved-symbols=ignore-all` and
`-Wl,--allow-multiple-definition` (`native/CMakeLists.txt:199-201`), so neither a
missing symbol nor a duplicated one produces any diagnostic. `engine_stubs_generated.cpp`
provides weak `return 0` bodies as a safety net.

This means functions **declared in headers but never defined** don't cause link
errors — they silently return 0/nullptr at runtime, causing corruption or crashes.

These functions are invisible to the orchestrator DB (which tracks original
binary .obj functions) and to objdiff.

### Measured state (2026-08-19, `dc3-native`, 799 objects + 4 archives)

Earlier revisions of this page said "2,538 weak stubs" and estimated ~1000
undecomp'd + ~1500 Xbox/3rd-party stubs. Those numbers counted *source lines*
across the file's whole history. Measured from the linked binary:

| | count |
|---|---|
| global/weak symbols `engine_stubs_generated.cpp.o` defines | **160** |
| ...also defined by another link input | 17 |
| ...**of which the stub won (SHADOWED)** | **0** |
| ...stub is the only definition | 143 |
| ...**of those, referenced by a real object (LIVE)** | **119** |
| ...unreferenced (dead weight) | 24 |
| still genuinely unresolved (`ldd -r`) | 24 |

Run `python3 scripts/native/check_stub_shadow.py --build-dir native/build` to
re-derive all of these. Do not quote them from here.

### "Real definitions override these automatically" is not true

That claim used to sit at the bottom of this page and it is the single most
expensive misconception about this file. Three ways it fails, all found in one
pass on 2026-08-19 and all fixed:

1. **The stub is the only definition of a symbol a real object references.**
   `LiveCameraInput.cpp` declared an anonymous-namespace `YUVtoRGB` it never
   defined; `_stub_yuvtorgb` satisfied the reference and every Kinect colour
   texel decoded to 0 — a solid black camera feed.
2. **Signature drift makes the real body a different symbol.** `MakeString.cpp`
   defined `ValidateThreadId(DWORD)`; on native `DWORD` is `unsigned int`, but
   `os/OSFuncs.h` declares `unsigned long`, so one object file both *defined*
   `ValidateThreadId(unsigned int)` and *referenced*
   `ValidateThreadId(unsigned long)`. Same story for `DspAllocate`, whose engine
   implementation took `void *` where every caller declares
   `IXAudioBatchAllocator *`.
3. **A `weak` stub can beat a `weak_odr` body.** `NuiTransformSkeletonToDepthImage`
   (stub `_stub_fn_111`, deleted in `54681e861`): at `-O2` clang inlined the real
   header body everywhere and emitted no out-of-line copy at all, so the stub was
   the only definition left in the link — harmless at `-O2`, but the web build is
   `-O0`, where every call site goes out-of-line and hit the stub.

The only way to answer "which body did the linker choose" is to disassemble the
linked binary. Source inspection cannot answer it.

## The gate: `scripts/native/check_stub_shadow.py`

```bash
# after building native/build/dc3-native
python3 scripts/native/check_stub_shadow.py --build-dir native/build
python3 scripts/native/check_stub_shadow.py --build-dir native/build --all    # + the LIVE worklist
python3 scripts/native/check_stub_shadow.py --build-dir native/build --json   # machine-readable
python3 scripts/native/check_stub_shadow.py --build-dir native/build --self-test
```

It is **not wired into the default build** — run it by hand, or add it to CI.
Exit status: `0` clean, `1` a stub body is bound in the final binary while a real
definition exists (or a new unresolved symbol appeared), `2` usage error.

What it actually does:

1. parses `dc3-native`'s link edge out of `build.ninja` — the real link line, not
   a guess or a source scan;
2. `nm`s the stub object for the symbols it defines, then every other link input
   for the same names;
3. **disassembles the final executable** for each duplicated symbol and
   fingerprints the stub shape (all instructions are the `HX_STUB_TRACE`
   preamble plus a zero return, and any `call` goes to `dc3::StubTraceHit`);
4. arm 2: reports stubs that are the *only* definition but are referenced by a
   real object — the class (1) and (2) bugs above live here and are invisible to
   the duplicate-definition test;
5. arm 3: `ldd -r` for symbols that are still unresolved, against a baseline set
   in the script.

`--self-test` pins the fingerprint in both directions against bodies whose
nature is not in question. It is there because the first version of the
fingerprint scanned objdump's whole listing including the symbol's own label
line, so `dc3::StubTraceHit` — a real function — classified as a stub. A gate
whose detector always answers "not a stub" reports zero problems forever and
looks healthy.

## `--unresolved-symbols=ignore-all`: can it be tightened?

Yes, and cheaply. `ldd -r` on the normal binary reports the same set as
relinking with `--unresolved-symbols=report-all`, in seconds, so `ldd -r` is the
practical gate and a strict relink is unnecessary. The count was 31; it is now
**24**, after `fix/kinect-camera-path` closed seven.

**The old wording here said each one "gets a PLT entry whose `JUMP_SLOT`
relocation resolves to 0". That is wrong** and it misled at least one downstream
brief. The zero `readelf` shows is `st_value`, which is zero for *every*
undefined symbol including `printf`'s, and the `.got.plt` slots hold PLT+6 — the
ordinary lazy trampoline. The deferral to first call is plain lazy binding, not
anything specific to this binary. The check that actually settles whether a hole
is real:

```
LD_BIND_NOW=1 ./native/build/dc3-native     # dies at startup, naming the first hole
```

Verified both directions on 2026-08-19: before the recoveries this died on
`undefined symbol: _hypot`; after `_hypot` was given a body it died on
`undefined symbol: BinkOpenTrack` instead.

Most of the 24 are Bink / Kinect / Xbox SDK / PPC intrinsics that will never
have bodies. What is left that is *not* in that bucket:

| symbol | status |
|---|---|
| `ReadSingleJoypad`, `requestBreedWrite` | Xbox controller HID back end. **Unreachable**, not latent: their only caller `JoypadPollCommon` has zero call sites and is not address-taken in `dc3-native` (native input runs through `Joypad_Native.cpp`'s own `JoypadPoll`). |
| `std::type_info::operator==(std::type_info const&) const` | **Not a decomp gap, but not dead code either — "toolchain mismatch, low-probability latent abort".** libstdc++ 15 on this box does not export `_ZNKSt9type_infoeqERKS_` at all. All four references come from libstdc++'s own header-instantiated templates in `HttpServer.cpp.o`, and only two of them are unreachable: the two `_Sp_counted_ptr_inplace<…>::_M_get_deleter` instantiations have 0 callers, but `regex_traits<char>::transform_primary<const char*>` and `<char*>` have **4 callers each**, reached from `std::__detail::_BracketMatcher::_M_apply`'s outlined equivalence-class lambda. A regex containing an equivalence class (`[[=x=]]`) would abort the process on first use. Still nothing to decompile — no DC3 body exists and defining a member of `std::type_info` is reserved-name UB ([namespace.std]) — but the earlier "`_M_get_deleter` has 0 call sites, therefore dead" covered only half the references. |

Closed on 2026-08-19 (`fix/kinect-camera-path`), recorded so they are not
re-opened:

| symbol | it was never missing — it was |
|---|---|
| `LiveCameraInput::LockStream` / `UnlockStream` | Recovered and 100% (25/25 and 6/6 instructions). Sitting inside the `#else` arm of an `#ifdef HX_NATIVE` in `LiveCameraInput.cpp`. Guard removed. |
| `RecursePatternInternal` | Same guard shape — `#ifndef HX_NATIVE` in `os/File.cpp` — but **not a recovered body**: it is a **~80% reconstruction** (240 instructions / **117 mismatches**, 80.1% normalized), derived from a Ghidra decompile as its own in-source comment says, with a 16-instruction target-only cluster and a `String::substr` call the base never makes. Un-guarding it is still right (it closes a hole `FileRecursePattern` and `OnEnumerateFrameRateResults` jump to), but do not read this row as a match. Its POSIX primitive `FileEnumerate` needed a fix of its own — see below. |
| `Hmx::Matrix4::Col3`, `CDGetError`, `_hypot` | Bodies in Xbox-only TUs (`rnddx9/Cam.cpp`, `os/CDReader.cpp`) or the MSVC CRT. Hosted in `native/src/native_link_glue.cpp`. `Col3` had 16 call sites in `Hmx::operator*(const Transform&, const Hmx::Matrix4&)`. |
| `createFilter` | A **signature-drift bug in the decomp**, not a native gap. `dsp/EQEffect.cpp` declared it `extern "C"`, so it referenced an unmangled `createFilter`; the target's symbol is `?createFilter@@YAXW4FilterType@@W4FilterBand@@IMMPAUFILTER@@H@Z` and `synth/filterdesign.cpp` defines it with C++ linkage. objdiff cannot see this class of bug — normalized mode discards relocation targets. |

### Un-guarding `RecursePatternInternal` needed a fix in the *engine*, not here

`RecursePatternInternal`'s only platform primitive is `FileEnumerate`, and the
POSIX implementation was missing `File_Win.cpp:107-111`'s `qualified == "."`
special case. Because `FileQualifiedFilename` prepends `gNativeDataDir`
(default `"."`), the path string it prefix-matched against the caller's pattern
was data-dir-qualified while the pattern was not, and `FileMatch`
(`os/File.cpp:229-251`) requires a literal prefix match up to the first
wildcard. **Every pattern not beginning with a wildcard therefore enumerated
empty rather than failing** — no log, no assert, no error return. So un-guarding
the function without this fix would have replaced an unresolved-symbol crash
with a silent wrong answer, which is strictly worse. Known callers reaching it:
`ShaderProgram.cpp:53` (`"%s/shaders/*.fx"`), `obj/Utl.cpp:482,517`.

**The trap: DC3's own `native/src/platform/File_Native.cpp` is not what
`dc3-native` links.** `native/CMakeLists.txt:1166` `REMOVE_ITEM`s it from
`DC3_NATIVE_CORE_SOURCES_ENGINE`, so `dc3-native`, `milo-viewer`, `render-test`
and `milo-tests` all get **`milo-native-engine`'s** copy — confirmed by `nm`,
which shows the single `T FileEnumerate` in `dc3-native` coming from
`milo-engine.dir/src/platform/File_Native.cpp.o` and nothing from DC3's object.
Fixing only DC3's copy would have been a no-op for every binary that matters.
Both were fixed: the engine side landed as `milo-native-engine` `84f9a8d`
(branch `fix/fileenumerate-dot-prefix`), DC3's copy separately because it is
still live for `dc3-web` (`DC3_WEB_CORE_SOURCES`) and the export tools.

This is the same shape as the `SynthCommon_Stub.cpp` finding recorded during
`fix/native-stub-shadow`'s verification. **Before editing anything under
`native/src/platform/`, check the `REMOVE_ITEM` block at
`native/CMakeLists.txt:1156-1189` and confirm with `nm` which object actually
defines the symbol.**

Tightening the flag to `report-all` requires giving all 24 a body or an explicit
stub first. Until then, the `ldd -r` baseline in `check_stub_shadow.py` is the
guard: any *new* unresolved symbol fails the gate.

## The native gate for this lane, stated honestly

Measured 2026-08-19 in `/home/free/code/milohax/wt/kinect-stream`, against
`milo-native-engine` `84f9a8d`.

| gate | result |
|---|---|
| `cmake --build native/build --target dc3-native milo-tests` | green, 0 errors |
| `ldd -r native/build/dc3-native` | **24** undefined — the branch's baseline, unchanged |
| `ctest` | **361 executed and passed, 84 skipped, of 445 registered**; 0 failures |
| the four `CameraYuvDecode` tests | `#439`–`#442`, all `status="run"`, all Passed |

**Do not quote this as "445/445".** That is literally what `ctest` prints — it
counts skips as passes and exits 0 — and it is the number `CLAUDE.md` explicitly
warns against repeating. The skipped 84 are the whole end-to-end tier
(`DC3_GAMEPLAY_TESTS`, `DC3_DTA_FLOW_TESTS`, `DC3_AUDIO_TESTS`, `MILO_LIB`),
which is where live bugs actually live. The executed count also drifts with the
environment: an independent run on the same branch a few hours earlier measured
360 executed / 85 skipped, so quote the split you measured, with its date.

Also remember `MILO_ASSERT` is non-fatal on native, so a clean exit code is not
by itself evidence of anything. The load-bearing signal in a headless run is a
zero `FAIL:` count, with a control that produces `FAIL:` lines on known-bad
input.

### Known issue, pre-existing and not from this lane

`cmake --build native/build` with **no target** (i.e. all targets) fails on
`wgpu-window-test`. Two stale includes of `gfx/GpuDevice.h` survive a header
that no longer exists at that path:

```
native/src/gfx/GpuDevice.cpp:1:10: fatal error: 'gfx/GpuDevice.h' file not found
native/src/main.cpp:5:10:          fatal error: 'gfx/GpuDevice.h' file not found
```

The header was moved into `milo-native-engine` by `5cfbcba9b`
("build(native): dedup platform/gfx TUs onto milo-engine"), which did not update
these two references. **Pre-existing on `main` and entirely unrelated to this
lane** — both `#include` lines are present in `main`'s tree today — but it means
**"the native build is green" must always be qualified by target**.

## Root Causes

### 1. PPC-only definitions in `link_glue.cpp`
The PPC decomp build uses `src/link_glue.cpp` which defines many template instantiations (CopyRef, BinStream operator<<, etc.). The native build doesn't compile this file.

**Fix**: `native/src/native_link_glue.cpp` mirrors these definitions for the native build.

### 2. Declared-but-never-defined methods
Some methods are declared in headers but never defined in any .cpp (copy ctors, operator=, constructors). The native build's different template instantiation patterns may pull them in.

### 3. Not-yet-decomp'd functions
Milo engine functions that exist in the original binary but haven't been reverse
engineered yet. The historical "~1000" here is wrong — see the measured table at
the top; run the gate with `--all` for the current list.

### 4. Xbox/3rd-party only
Bink, D3D, NUI, XNet, Kinect — 3rd party or Xbox-specific, will never be
decomp'd. They need native replacements or are safely stubbed, and they dominate
the 119 LIVE stubs. (`json_object` is no longer among them: the real json-c
sources were wired in, see `0abd4ad43`.) The historical "~1500" is wrong for the
same reason as above.

## Progress

### DONE — Tier 0: Copy Semantics (crash risk when stubbed)
| Symbol | File | Status |
|---|---|---|
| `FlowPtr<Hmx::Object>::FlowPtr(const FlowPtr&)` | `FlowPtr.h` | Defined inline |
| `FlowMathOp::operator=` | `DrivenPropertyMathOps.cpp` | Defined |
| `Skeleton::operator=` | `gesture/Skeleton.cpp` | Defined (memberwise) |
| `SpotDrawParams::operator=` | `world/SpotlightDrawer.cpp` | Defined |
| `MsgSinks::EventSinkElem::operator=` | `obj/Msg.cpp` | Defined |
| `QuatXfm(const Transform&)` | `math/mtx.cpp` | Defined: `v(tf.v), q(tf.m)` |

### DONE — Tier 1: Template Instantiations (from link_glue.cpp)
All in `native/src/native_link_glue.cpp`:
- ~65 `ObjRefConcrete<T>::CopyRef` instantiations
- ~20 `BinStream& operator<<(ObjPtrList<T>)`
- ~10 `BinStream& operator<<(ObjPtrVec<T>)`
- ~23 `BinStream& operator<<(ObjOwnerPtr<T>)`
- ~5 `BinStream& operator<<(ObjDirPtr<T>)`

### DONE — Tier 2: Small utility functions
| Symbol | File | Status |
|---|---|---|
| `Transform::LookAt` | `math/mtx.cpp` | Defined (Ghidra-verified) |
| `Triangle::Set` | `math/Geo.cpp` | Defined (cross product) |
| `DirLoader::New` | `obj/DirLoader.cpp` | Factory method |
| `ObjDirPtr<T>::ObjDirPtr(T*)` | `obj/Dir.h` | Template constructor |
| `ObjDirPtr<T>::IsLoaded()` | `obj/Dir.h` | Template method |

### TODO — Tier 3: Undecomp'd Milo engine functions (~1000)
These are real functions in the original binary that haven't been reverse-engineered yet. Source files exist but the specific functions are stubs or missing.

| Subsystem | Count | Priority | Notes |
|---|---|---|---|
| Rnd (rendering) | ~117 | High | DrawShowing, Load, Poll, RenderState |
| Char (animation) | ~62 | Medium | CharClip, CharDriver, CharHair, CharIK |
| Ham (DC3 game) | ~68 | Medium | HamCharacter, HamDirector, MoveMgr |
| World | ~46 | Medium | Spotlight, Crowd, Reflection |
| Flow | ~30 | Medium | FlowAnimate, FlowSound, FlowState |
| Synth (audio) | ~29 | Low | Emitter, Fader, StreamDecoder |
| UI | ~10 | Low | UIList, UILabel specifics |

## Detection Method

```bash
# List all C++ method stubs (demangled)
grep '_stub_fn_' native/src/engine_stubs_generated.cpp | \
  sed 's/.*__asm__("//;s/").*//' | c++filt | sort

# Find copy ctors / operator= specifically
grep '_stub_fn_' native/src/engine_stubs_generated.cpp | \
  sed 's/.*__asm__("//;s/").*//' | c++filt | \
  grep -E 'operator=|CopyRef|::[A-Z][a-zA-Z]+\(.*const.*&\)$'

# Count by subsystem
grep '_stub_fn_' native/src/engine_stubs_generated.cpp | \
  sed 's/.*__asm__("//;s/").*//' | c++filt | \
  grep -oP '^[A-Za-z:]+' | sort | uniq -c | sort -rn | head -30
```

## How to Regenerate Stubs

`ldd -r native/build/dc3-native` gives the undefined set directly and matches a
`--unresolved-symbols=report-all` relink exactly (both: 31 symbols at the time, verified
2026-08-19). Prefer it — the strict relink takes minutes, `ldd -r` takes seconds.

```bash
ldd -r native/build/dc3-native 2>&1 | grep -oP 'undefined symbol: \K\S+' | sort -u | c++filt
```

To reproduce the strict relink anyway (e.g. to see which object each reference
comes from), take the real link line rather than editing CMakeLists.txt:

```bash
ninja -C native/build -t commands dc3-native | tail -1 \
  | sed -e 's|ignore-all|report-all|' -e 's|-o dc3-native|-o /tmp/dc3-native-strict|' \
  | bash 2>&1 | grep 'undefined reference'
```

## Architecture Note

- `src/link_glue.cpp` — PPC decomp build only. Contains template instantiations needed for the original binary link. Do NOT remove definitions from here.
- `native/src/native_link_glue.cpp` — Native build only. Mirrors link_glue.cpp template instantiations for GCC/Clang.
- `native/src/engine_stubs_generated.cpp` — Weak fallback stubs. **Real
  definitions do NOT override these automatically** — see the three failure
  modes at the top of this page, and run `scripts/native/check_stub_shadow.py`
  rather than assuming.
- `milo-native-engine/src/platform/*_Stub.cpp` — for the engine-linking targets
  (`dc3-native`, `milo-viewer`, `render-test`, `milo-tests`) the *engine's* copy
  is what links; `native/CMakeLists.txt` `REMOVE_ITEM`s the DC3 copy of every
  file in that list. Editing DC3's copy changes only `dc3-web` and the export
  tools. This is how the `DspAllocate` bug survived a fix to the DC3 file.

## Runtime A/B on the 2026-08-19 fixes — a negative result worth recording

`DC3_STUB_TRACE=1` + `/api/stubs` on a 1200-frame headless boot gives an
*identical* table before and after the YUVtoRGB / ValidateThreadId / DspAllocate
fixes: 171 hits across 4 stubs (`OutputDebugStringA` 100, `vorbis_synthesis_poll`
69, `DmGetSystemInfo` 1, `DmMapDevkitDrive` 1). None of the three fire in a
menu-only boot — `YUVtoRGB` needs a Kinect colour stream, `DspAllocate` needs a
delay/flanger effect instantiated, and `ValidateThreadId` only runs on a
per-thread-table *miss*, which a single-threaded menu never reaches.

So the stub-hit counter is a good worklist for boot-path stubs and useless as a
regression gate for anything gated behind gameplay or hardware. For those, the
evidence is the disassembly, and it should be quoted as such — the runs above
only establish that the fixed binary boots and runs 900/1200 frames to a clean
`DC3_EXIT: code=0`.

## The Kinect colour path is not black — it is unreachable (2026-08-19)

The `YUVtoRGB` fix above is correct, and on the PPC target it was never a bug at
all (the linker map shows one ICF-folded body contributed by both
`gesture:LiveCameraInput.obj` and `gesture:DrawUtl.obj`). But the follow-up
question — "so does the camera feed work now?" — has a blunter answer than
either "yes" or "still black":

**`dc3-native` never constructs a `LiveCameraInput` at all.** Chain, all of it
checked against the linked binary rather than the source:

* `LiveCameraInput::sInstance` is written in exactly two places,
  `LiveCameraInput::PreInit` (set) and `LiveCameraInput::Terminate` (clear).
* `PreInit` has exactly one caller in the binary: `LiveCameraInput::Init`.
* `LiveCameraInput::Init` has **zero** callers and is not address-taken — no
  `call`, and no relocation naming it in any data section. `App.cpp`'s call is
  inside `#ifndef HX_NATIVE`, and `GestureMgr::Init` returns early on native
  before reaching its own `PreInit` call.

So `sInstance` and `GestureMgr::mLiveCamInput` are provably always null, and
every consumer is guarded: `HamUI::Store*BufferAt` tests
`if (TheGestureMgr->GetLiveCameraInput())`, and `HamUI::DrawDebug` — the only
route to `DrawGestureMgr` → `UpdateBufferTex` — returns early under `HX_NATIVE`.
All eight `LockStream` call sites are dead.

Two further reasons the path could not have worked as written, either of which
would have to be fixed before a live feed is possible:

1. **LP64 pointer truncation.** The four `TextureStore::UpdateFrom*Buffer*`
   bodies held texel and source pointers in `unsigned int`. Fixed to `uintptr_t`
   (identical type on PPC, so all four objdiff numbers are unchanged:
   75/26, 158/71, 87/22, 117/38 instructions/mismatches).
1b. **LP64 pointer *wrap*, a third hazard the first pass missed.**
   `LiveCameraInput.cpp:267-268` advanced the row pointers by
   `((pitch>>1) - 640) * 2` and `(mPitch>>2) - 320`, both **unsigned int**
   subtractions; `:385` had the same shape. If the texel pitch is narrower than
   640 texels the result wraps to ~4e9 instead of going negative. On 32-bit PPC
   that still steps the pointer correctly backwards; on LP64 it zero-extends and
   jumps ~8 GB / ~16 GB forward. Now cast to `int` first, matching what
   `UpdateFromColorBufferClip`'s `destStride` already did. Also PPC-neutral
   (same four counts), with a directed negative control: perturbing the `640` to
   `641` moved `UpdateFromColorBuffer` 95.5 → 95.4 normalized and added
   `off:+1` to mismatch `[41] subi`, so the flat numbers are not vacuous.
2. **The decode is endian-dependent.** It reads the UYVY byte stream as 32-bit
   words and takes `>>24` as Cb and `>>8` as Cr, which is the big-endian layout.
   A native NUI shim must hand it big-endian-ordered words, or red and blue come
   out transposed. (Incidentally, the locals in `LiveCameraInput.cpp` name those
   two `cr` and `cb` the wrong way round; the arithmetic is right.)

Where the data would have to come from on a machine with no Kinect:
`NuiImageStreamGetNextFrame` → `Buffer::mFrames[]` → `StreamBufferData()` →
`NUI_IMAGE_FRAME::pFrameTexture` → `LockStream()` →
`D3DLineTexture_LockRect`. The last hop now has a native implementation
(`native/src/platform/NuiImageSurface_Native.cpp`); the rest does not, and
enabling it means giving `LiveCameraInput`'s constructor native `NuiInitialize`
/ `NuiImageStreamOpen` / `NuiAudioCreate` / `CreateCameraBufferMat`. That is a
separate piece of work.

Until then the decode is proved at unit level, not in the running port:
`native/tests/test_camera_yuv_decode.cpp` pushes a synthetic UYVY frame through
the real `UpdateFromColorBuffer` and checks the output against the published
BT.601 matrix (301920 / 307200 texels non-zero, 64 distinct RGB565 values).
It has a recorded negative control: stubbing `YUVtoRGB` back to `return 0` fails
exactly the two colour assertions and leaves the two control cases passing.

The evidence is genuinely non-tautological — the test object defines no decode
symbol at all (`YUVtoRGB` is an anonymous-namespace `inline` that is fully
inlined and has no symbol), so any mutation of the decode necessarily mutates
the code under test. **But the test is narrower than it reads, in three ways
that must be stated wherever it is cited as evidence:**

1. **It is byte-order-blind by construction, so it does NOT test the
   endianness point above.** The input frame is built as a
   `std::vector<uint32_t>` through a `PackUYVY()` helper whose shift positions
   were *copied from the code under test*, then read back through
   `unsigned int*`. Host endianness therefore cancels on both sides. Feeding
   the identical four bytes as a big-endian **byte** stream makes the same
   assertions fail hard (`got(21,63,19)` vs `want(29,3,1)`). The test file
   admits this scoping; earlier commit messages and doc text did not.
2. **Its coefficient resolution is roughly 10%, not exact.** The ±1 tolerance
   is about 2× looser than the measured Q16-vs-double drift (which is 0), so
   subtler coefficient errors slip through. Measured, each a real
   rebuild-and-relink: `cr`/`cb` swapped at both call sites → **caught**;
   red Q16 91881 → 101069 (+10%) → **caught**; RGB565 green shift `<<5` →
   `<<6` → **caught**; red 91881 → 96475 (**+5%**) → **passes silently**;
   green-U 22553 → 24808 (**+10%**) → **passes silently**.
3. **All chroma discrimination lives in one test.**
   `LumaRampReachesTheTextureAsNonZeroPixels` pins chroma at neutral and
   reported an unchanged `301920 / 64` under every one of those five
   mutations. Its "monotonic left-to-right" claim is a **two-endpoint
   assertion**, not a per-column scan (full-row instrumentation does confirm
   monotonicity holds — 0 inversions — but the test does not check it).

### Checking PPC neutrality of a shared-`src/` edit: not raw `report.json` md5

Comparing `md5sum build/373307D9/report.json` between two full `ninja` runs is
the cheap whole-build neutrality check, but **the raw md5 is not stable across
runs of the same tree**: `report.json`'s `provenance` block carries
`cache_hits` / `cache_misses`, which depend on how warm the objdiff cache was.
A cold build writes `cache_misses: 2224` and no `cache_hits`; an incremental
rerun of the identical tree writes `cache_hits: 2223, cache_misses: 1` and a
different md5. Two runs of the same source produced
`b2640ec2c78e721e5bfc0b7a182bb498` and `b138b343668d46311ba863a0b3484d2d` this
way, with zero semantic difference.

Compare the measures instead:

```python
import json
a = json.load(open('<baseline>/build/373307D9/report.json'))
b = json.load(open('<branch>/build/373307D9/report.json'))
flat = lambda r: {(u['name'], f['name']): f['fuzzy_match_percent']
                  for u in r['units'] for f in u.get('functions', [])}
fa, fb = flat(a), flat(b)
print(a['measures'] == b['measures'],
      [(k, fa[k], fb[k]) for k in fa.keys() & fb.keys() if fa[k] != fb[k]])
```

Run against a worktree built from the branch point, this is exact: for the
2026-08-19 `fix/kinect-camera-path` changes it reported one changed function out
of 48,344 — `?SetParameter@EQEffect@@QAAXHM@Z`, 84.49353 → 84.521255 fuzzy, from
the `createFilter` relocation name now matching the target — with identical
top-level and per-unit measures, and **0** functions changed on
`match_percent_normalized`. Note `fuzzy_match_percent` is relocation-sensitive,
which is what makes it able to see a mangling fix that normalized mode cannot.
The mechanism is confirmed in raw mode: the function goes from 341 equal / 24
symbol-relocation arg diffs to 344 / 21 — exactly the **3** `createFilter` call
sites at `EQEffect.cpp:600,607,615`.

**Symbol-name correction.** Earlier write-ups of this lane named the changed
function `?SetParameter@EQEffect@@UAAXIM@Z`. **No such symbol exists.** The real
one is `?SetParameter@EQEffect@@QAAXHM@Z` at `.text:0x82E59430` — public
**non-virtual** (`Q`, not `U`), taking `(int, float)` (`HM`, not `IM`).
