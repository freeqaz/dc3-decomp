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
| `std::type_info::operator==(std::type_info const&) const` | **Not a decomp gap.** libstdc++ 15 on this box does not export `_ZNKSt9type_infoeqERKS_` at all; the four references come from libstdc++'s own templates (`regex_traits::transform_primary`, `_Sp_counted_ptr_inplace::_M_get_deleter`, the latter with 0 call sites). No DC3 body exists to recover. |

Closed on 2026-08-19 (`fix/kinect-camera-path`), recorded so they are not
re-opened:

| symbol | it was never missing — it was |
|---|---|
| `LiveCameraInput::LockStream` / `UnlockStream` | Recovered and 100% (25/25 and 6/6 instructions). Sitting inside the `#else` arm of an `#ifdef HX_NATIVE` in `LiveCameraInput.cpp`. Guard removed. |
| `RecursePatternInternal` | Same: `#ifndef HX_NATIVE` in `os/File.cpp`, around a body whose only platform primitive (`FileEnumerate`) has had a POSIX implementation all along. |
| `Hmx::Matrix4::Col3`, `CDGetError`, `_hypot` | Bodies in Xbox-only TUs (`rnddx9/Cam.cpp`, `os/CDReader.cpp`) or the MSVC CRT. Hosted in `native/src/native_link_glue.cpp`. `Col3` had 16 call sites in `Hmx::operator*(const Transform&, const Hmx::Matrix4&)`. |
| `createFilter` | A **signature-drift bug in the decomp**, not a native gap. `dsp/EQEffect.cpp` declared it `extern "C"`, so it referenced an unmangled `createFilter`; the target's symbol is `?createFilter@@YAXW4FilterType@@W4FilterBand@@IMMPAUFILTER@@H@Z` and `synth/filterdesign.cpp` defines it with C++ linkage. objdiff cannot see this class of bug — normalized mode discards relocation targets. |

Tightening the flag to `report-all` requires giving all 24 a body or an explicit
stub first. Until then, the `ldd -r` baseline in `check_stub_shadow.py` is the
guard: any *new* unresolved symbol fails the gate.

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
   (identical type on PPC, so all four objdiff numbers are unchanged).
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
