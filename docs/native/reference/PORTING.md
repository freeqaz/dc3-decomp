# Porting Reference: LP64, Endianness, Linking and Guards

What changes when the decomp's Xbox 360 source (ILP32, big-endian, MSVC) is compiled by Clang for a 64-bit little-endian host, and the rules for keeping native fixes out of the matched PPC build.

Last verified against the tree: 2026-11-13 (np-docs @ 2d678db7e).

## Guard policy

The PPC build and the native port compile the same `src/` tree. Native-only code goes inside `#ifdef HX_NATIVE` (web-only code goes inside `__EMSCRIPTEN__` / `HX_WEB`).

- **PPC decomp parity is not negotiable.** Anything is allowed inside an `HX_NATIVE` block; nothing outside one may change PPC codegen.
- **A guard is for a real platform divergence, not for suppressing a crash.**
  - Good reasons: async vs sync loading, 64-bit vs 32-bit pointer logic, missing hardware (Kinect/NUI), STL implementation differences, endianness.
  - Bad reasons: a null check that hides uninitialised data, an early return that skips initialisation, a `MILO_WARN` downgrade that hides a missing object.
- **Failing silently is worse than crashing.** Masking a crash moves the corruption downstream, where it is harder to find. Let it fail loudly (`MILO_FAIL`). `MILO_FAIL` and `MILO_ASSERT` are non-fatal on native by default (`MILO_FATAL_FAILS` unset or `0`; see `src/system/os/Debug.cpp`). That makes a guard *after* a `MILO_FAIL`, to stop the null dereference that follows, the one legitimate defensive guard.
- **A new native-only function body must itself be guarded.** Adding an out-of-line body to a shared `.cpp`, even one the PPC build never calls, can change MSVC's inlining decisions for the rest of that TU. See "Header Edits Shift Inlining TU-Wide" in [docs/decomp/TECHNICAL_NOTES.md](../../decomp/TECHNICAL_NOTES.md).
- **Two failure modes are invisible to the match metric:**
  - A native fix written *without* a guard leaks into the PPC build: [native-guard-leak-scan.md](../../decomp/patterns/native-guard-leak-scan.md).
  - A guarded native body replaces the measured one, so objdiff only ever scores the `#else` branch: [native-shadow-bodies-are-unmeasured.md](../../decomp/patterns/native-shadow-bodies-are-unmeasured.md).

Count guards with a stated unit; the units are not interchangeable. For example, conditional *lines* naming the macro:

```bash
grep -rE '^\s*#\s*(if|ifdef|ifndef|elif).*HX_NATIVE' src | wc -l   # 979 lines on 2026-11-13
```

`scripts/analysis/native_shadow_audit.py` produces a classified inventory of the guard regions.

## The compiler setup

Native TUs are compiled by Clang in MSVC-compatibility mode. The flags live in `DC3_MSVC_COMPAT_FLAGS` in `native/CMakeLists.txt`:

- `-fms-extensions` (`__declspec` in the XDK headers)
- `-fms-compatibility` (dependent-base lookup)
- `-fms-compatibility-version=19.29`
- `-fdelayed-template-parsing`

On Linux, `-D__GNUC_STDC_INLINE__` and `-D__GCC_ATOMIC_TEST_AND_SET_TRUEVAL=1` are added so that glibc/libstdc++ headers parse in that mode.

macOS builds disable MSVC compatibility for the TUs that include SDK or framework headers (`MetalSurface.mm`, `GpuDevice.cpp`, `AudioDevice.cpp`, set via `set_source_files_properties` under `if(APPLE)`). The SDK's `ConditionalMacros.h` cannot identify the compiler otherwise.

### MSVC `vector::begin()` returns `T*`: the iterator shim

The decomp assumes that MSVC's old STL returns a raw `T*` from `begin()`/`end()`. Rather than editing every call site, the port shadows the host STL's iterator wrapper with a copy that adds an implicit conversion to the underlying pointer:

- libstdc++: `native/include/bits/stl_iterator.h` adds `operator _Iterator() const` to `__normal_iterator`.
- libc++: `native/include/__iterator/wrap_iter.h` adds `operator iterator_type() const` to `__wrap_iter`. This is the macOS case.

Both take priority because `native/CMakeLists.txt` adds `native/include` with `include_directories(BEFORE SYSTEM ${CMAKE_SOURCE_DIR}/include)`. Do not "fix" a call site that relies on the conversion; the shim exists so the decomp source can stay as MSVC wrote it.

## LP64 hazards

The Xbox is ILP32 (`int` = `long` = pointer = 4 bytes). The native hosts are LP64 (`long` = pointer = 8 bytes). Everything below follows from that.

- **`u32` and friends:** `src/types.h` maps `u32`/`s32` to `unsigned int`/`signed int` on native and to `unsigned long`/`signed long` on the Xbox. Any API that took `DWORD` or `unsigned long` on the Xbox can now drift from a caller that spells it differently, and **an overload that drifts becomes a new symbol rather than a definition**. Examples: `XNotifyCreateListener` (`DWORD` vs `QWORD`) and `ValidateThreadId`; see [MISSING_DEFINITIONS.md](../MISSING_DEFINITIONS.md).
- **Pointer-to-`int` casts:** `(int)ptr` truncates on native. Spell pointer differences as `intptr_t` / `uintptr_t`; that is the same type on PPC, so it is codegen-neutral there. `CharClip::BytesInMemory` and `CharClip`'s resize (`src/system/char/CharClip.h/.cpp`) are the in-tree examples.
- **Unsigned subtraction that relies on 32-bit wrap:** `(pitch >> 1) - 640` as `unsigned int` wraps to about 4e9. On PPC adding that to a pointer steps backwards; on LP64 it jumps gigabytes forward. Compute the difference as signed `int` first. The live example is `LiveCameraInput.cpp`'s row step.
- **Hard-coded word counts and sizes:**
  - A header that is "3 words" on PPC can be 4 pointer-sized words on LP64 (`b86a9b2c4`, MemHeap's free-block header).
  - An array of pointers is not sized in 4-byte units (`0a83ac4cf` MemTrack name stacks; `11c1a6f3e` sizes the hash block as `hashSize * sizeof(AllocInfo *)`).
- **Pointer arithmetic on byte counts:** advance a row pointer as `char *`, not `int` (`3db8c1dce`, `RndBitmap::LoadDIB`).
- **DataNode payloads:** a DataNode holds a pointer-sized value natively. Equality must compare the whole pointer, and loading a 4-byte value must zero the high half (`ae67a642f`).
- **Floating-point select semantics:** retail `Min`/`Max` compile to `fsel`, whose NaN behaviour differs from a C ternary (`8c4f92011`).

## Endianness

Game data (`.milo`, `.dtb`, saves) is big-endian. Byte order and bit order are independent problems: swapping a word's bytes does not fix a C bitfield, because MSVC/Xenon allocates bitfields MSB-first. The bitfield and packed-flag-word audit is [ENDIANNESS_PORT_CHECKLIST_DC3.md](../ENDIANNESS_PORT_CHECKLIST_DC3.md). One example of a byte-order fix that had to be host-independent is `LoadDtz`'s size trailer (`d713d4128`).

### `CompressedVertex_Xbox`: the field names are wrong

`struct CompressedVertex_Xbox` (`src/system/rndobj/MeshVertCompress.h`) has field names that do **not** describe what the D3D vertex declaration in `src/system/rnddx9/Mesh.cpp` puts in them:

| Offset | Field | D3D type | D3D usage | Actual content |
|---|---|---|---|---|
| 0 | `mPosX/Y/Z` | FLOAT3 | POSITION | position |
| 12 | `mColor` | D3DCOLOR | COLOR | packed colour |
| 16 | `mNormal` | FLOAT16_2 | TEXCOORD | **UV (2 half-floats)** |
| 20 | `mTangent` | DEC4N | NORMAL | **normal** |
| 24 | `mBinormal` | DEC4N | TANGENT | tangent |
| 28 | `mBoneIndices` | UDEC4N | BLENDWEIGHT | **bone weights** |

Trust the declaration, not the names. The native unpacker that follows it is `UnpackCompressedVertices` in `../milo-native-engine/src/gfx/VertexFormats.cpp`.

## Linking

The Linux link refuses undefined symbols (`-Wl,--no-undefined`, since `eb62d26a1`), while macOS still uses `-undefined dynamic_lookup`. The details are on [MISSING_DEFINITIONS.md](../MISSING_DEFINITIONS.md):

- where stubs live;
- why a weak stub can still beat a real body;
- the `scripts/native/check_stub_shadow.py` gate;
- `LD_BIND_NOW=1`;
- why the dc3 copy of a `REMOVE_ITEM`'d platform file is not what links.

### Key functions and zero-filled vtables

Under the Itanium C++ ABI, a class's vtable is emitted in the TU that defines its **key function**, the first non-inline, non-pure virtual. If no compiled `.cpp` defines that function, no vtable is emitted. The zero-filled weak blob in `native/src/engine_stubs_generated.cpp` ("vtable and typeinfo stubs for classes without key functions") then wins, and the first virtual call jumps to address 0.

When a class crashes on a null vtable, look for its key function before anything else. The usual case is a `Handle`/`SyncProperty` declared in the header but with no `BEGIN_HANDLERS` block in the `.cpp`.

A related ABI difference: Clang emits non-virtual `this`-adjusting thunks for multiply-inherited classes, which an asm-label alias in the stub file cannot provide. Such bodies must be real C++ definitions, placed in the decomp source under `#ifdef HX_NATIVE`. `native/src/thunk_stubs.cpp` is now an empty placeholder that says so.

## Loading pitfalls

### Missing factory registration

If a class has no registered factory, `DirLoader::CreateObjects` produces a NULL entry for it, and `ReadDead` then skips the wrong number of bytes. Every later object in that `.milo` reads garbage. Typical symptoms, far from the cause:

- `MILO_FAIL("String chars %d > %d")` from `BinStream::ReadString`.
- Native `ASSERT_REVS WARNING: <Class> '<name>' version N > M` lines (`src/system/obj/Object.h`).
- `BinStream::operator>>(String) ABORT: bad size=…`.

What to check:

1. Look for an `Unknown class` assert (`src/system/obj/Object.cpp`).
2. Find the subsystem `Init()` that registers the class.
3. For tests, add `REGISTER_OBJ_FACTORY(<Class>)` to `EnsureEngineInit()` in `native/tests/test_helpers.cpp`. Registering factories there without the full `UIManager::Init` is exactly what that function does.

### Diagnosing a stream desync

1. Log `Tell()` before and after each object's `PreLoad`/`PostLoad`/`Load`.
2. Compare the bytes consumed with what the object should consume.
3. The first object whose delta is wrong is the culprit.
4. Check whether its `Load` is a stub (`native/src/engine_stubs_generated.cpp`) or an unrecovered body.
5. If it is a stub, implement it from the symmetric `::Save`.
6. If it is implemented, compare it field by field with `::Save` to find the format mismatch.

`ChunkStream` (compressed `.milo`) is **forward-only**: `ChunkStream::SeekImpl` fails with "Can't seek on chunkstream". You cannot peek and seek back, or hex-dump a range and re-read it. A native `ChunkStream::Unreread` exists in `src/system/utl/ChunkStream.h` but has no callers.

### Native-only bookkeeping must not change class layout

Do not add members to `Hmx::Object` or other root classes for native-only state. A new member shifts every multiple-inheritance `this` adjustment and breaks the layout the decomp depends on. Keep the data outside the class.

The in-tree example: `ObjectDir::HasDirPtrs()` (`src/system/obj/Dir.cpp`) used to walk the ObjRef ring, which is O(n) per call and O(n²) while a large venue directory is destroyed. On native it now consults `DirPtrRefCounts()`, a static `unordered_map` in `src/system/obj/Dir.h`. The PPC branch is unchanged.

## Dawn / WebGPU API notes

All of these are in `../milo-native-engine/src/gfx/GpuDevice.cpp` today:

- **X11 `Success`:** X11 defines `Success`, which breaks `wgpu::RequestAdapterStatus::Success`. `#undef Success` after the native GLFW headers.
- **Readback row pitch:** `CopyTextureToBuffer` needs a 256-byte-aligned row pitch, `(bytesPerRow + 255) & ~255u`.
- **Callback types:** modern Dawn callbacks take `wgpu::StringView`, not `const char *`. The copy structs are `wgpu::TexelCopyTextureInfo` / `wgpu::TexelCopyBufferInfo`, and WGSL source is `wgpu::ShaderSourceWGSL`.
- **Synchronous requests:** synchronous adapter and device requests need the `wgpu::InstanceFeatureName::TimedWaitAny` instance feature.
- **Device loss:** the device-lost callback is registered with `CallbackMode::AllowSpontaneous` and logs `GpuDevice: device lost (reason N)`. A device-lost line at process exit is expected teardown (unverified at write time; check the reason code before treating it as a bug).

## Sources

- [archive: plans/custom-graphics-engine/PORTING_ANALYSIS.md](../../archive/2026-11-13-native-doc-audit/plans/custom-graphics-engine/PORTING_ANALYSIS.md): LP64 issue table, iterator shim, key-function lesson, ChunkStream
- [archive: native/NATIVE_PORT_STATUS.md](../../archive/2026-11-13-native-doc-audit/native/NATIVE_PORT_STATUS.md): CompressedVertex mapping, guarding new native bodies, MSVC-compat flags
- [archive: plans/custom-graphics-engine/IMPLEMENTATION_GUIDE.md](../../archive/2026-11-13-native-doc-audit/plans/custom-graphics-engine/IMPLEMENTATION_GUIDE.md): Dawn API gotchas
- [archive: plans/custom-graphics-engine/STREAM_DESYNC.md](../../archive/2026-11-13-native-doc-audit/plans/custom-graphics-engine/STREAM_DESYNC.md): desync diagnostic
- [archive: native/HACK_AUDIT.md](../../archive/2026-11-13-native-doc-audit/native/HACK_AUDIT.md): Guiding Principles
- [archive: native/DECOMP_GAPS.md](../../archive/2026-11-13-native-doc-audit/native/DECOMP_GAPS.md): key-function pattern, factory-registration diagnostic, HasDirPtrs fix
- [sessions/NATIVE_SUBDIR_LOADING_FIX.md](../../sessions/NATIVE_SUBDIR_LOADING_FIX.md)
