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
| still genuinely unresolved (`ldd -r`) | 31 |

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

Yes, and cheaply. Relinking with `--unresolved-symbols=report-all` leaves
**31 distinct undefined symbols** — `ldd -r` on the normal binary reports exactly
the same 31 in seconds, so `ldd -r` is the practical gate and a full strict
relink is unnecessary. Each of the 31 gets a PLT entry whose `JUMP_SLOT`
relocation resolves to 0; lazy binding means the process does not fail at
startup, only when that path first executes.

Most are Bink / Kinect / Xbox SDK / PPC intrinsics that will never have bodies.
These are real decomp gaps and are the ones worth closing:

| symbol | referenced from |
|---|---|
| `LiveCameraInput::LockStream(void const*, LockedRect&)` | `LiveCameraInput.cpp`, `DrawUtl.cpp` |
| `LiveCameraInput::UnlockStream(void const*)` | `LiveCameraInput.cpp`, `DrawUtl.cpp` |
| `Hmx::Matrix4::Col3(int) const` | math |
| `CDGetError()` | CD/loader |
| `RecursePatternInternal(char const*, void(*)(char const*, char const*), bool, bool)` | file utils |
| `ReadSingleJoypad`, `createFilter`, `requestBreedWrite` | input / gesture |
| `std::type_info::operator==(std::type_info const&) const` | host-STL shim |

Tightening the flag to `report-all` requires giving all 31 a body or an explicit
stub first. Until then, the `ldd -r` baseline in `check_stub_shadow.py` is the
guard: any *new* unresolved symbol fails the gate.

## Root Causes

### 1. PPC-only definitions in `link_glue.cpp`
The PPC decomp build uses `src/link_glue.cpp` which defines many template instantiations (CopyRef, BinStream operator<<, etc.). The native build doesn't compile this file.

**Fix**: `native/src/native_link_glue.cpp` mirrors these definitions for the native build.

### 2. Declared-but-never-defined methods
Some methods are declared in headers but never defined in any .cpp (copy ctors, operator=, constructors). The native build's different template instantiation patterns may pull them in.

### 3. Not-yet-decomp'd functions
Many stubs (~1000) are Milo engine functions that exist in the original binary but haven't been decompiled yet. These require actual reverse engineering work.

### 4. Xbox/3rd-party only (~1500)
Bink, D3D, NUI, XNet, json_object, Kinect — these are 3rd party or Xbox-specific and will never be decomp'd. They need native replacements or are safely stubbed.

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
`--unresolved-symbols=report-all` relink exactly (both: 31 symbols, verified
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
