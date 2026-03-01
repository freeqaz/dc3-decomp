# Missing Function Definitions — Native Port

## Problem

The native port links with `--unresolved-symbols=ignore-all`, which silently stubs any undefined symbols to `return 0`. The `engine_stubs_generated.cpp` file provides 2,538 weak stubs as a safety net.

This means functions **declared in headers but never defined** don't cause link errors — they silently return 0/nullptr at runtime, causing corruption or crashes.

These functions are invisible to the orchestrator DB (which tracks original binary .obj functions) and to objdiff. They can only be detected via:

1. **`engine_stubs_generated.cpp`** — the canonical list of all unresolved symbols
2. **Runtime crashes** — when a stubbed function is actually called
3. **Header audit** — scanning for declarations without definitions

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

```bash
# Build with error reporting instead of silent ignore
# Change --unresolved-symbols=ignore-all to --unresolved-symbols=report-all in CMakeLists.txt
# Then build and collect the undefined reference errors
```

## Architecture Note

- `src/link_glue.cpp` — PPC decomp build only. Contains template instantiations needed for the original binary link. Do NOT remove definitions from here.
- `native/src/native_link_glue.cpp` — Native build only. Mirrors link_glue.cpp template instantiations for GCC/Clang.
- `native/src/engine_stubs_generated.cpp` — Weak fallback stubs. Real definitions override these automatically.
