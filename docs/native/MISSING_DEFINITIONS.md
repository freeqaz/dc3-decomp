# Missing Function Definitions — Native Port

How the native link treats symbols with no real definition, why "real definitions override the weak stubs" is not a safe assumption, and the tools that answer "which body did the linker actually choose".

Last verified against the tree: 2026-11-13 (np-docs @ 2d678db7e).

## The link policy

On Linux, every native executable links with `-Wl,--no-undefined` and `-Wl,--allow-multiple-definition` (the `DC3_PLATFORM_LINK_OPTIONS` block in `native/CMakeLists.txt`, just above the `set(DC3_PLATFORM_LIBS` for Linux). A symbol with no definition is now a **link error**.

`eb62d26a1` (2026-08-31, "make the link refuse undefined symbols, and fix the 26 it was hiding") made that change. It replaced the old `--unresolved-symbols=ignore-all` + `--warn-unresolved-symbols` pair, which between them could not report a missing symbol at all:

- `ignore-all` suppresses the warn flag.
- The warn flag on its own demotes `--no-undefined`'s errors back to warnings.

**Do not re-add either flag.** When the link fails on an undefined symbol, add a definition rather than silencing it. `native/src/xbox_link_stubs.cpp` is the home for Xbox-only entry points, using the `HX_STUB_TRACE` convention.

`--allow-multiple-definition` stays, because the decomp has intentional stub/real pairs. It is also the reason a weak stub can win over a real body; see the next section.

macOS is different: Apple ld links with `-Wl,-undefined,dynamic_lookup` (plus `-ld_classic` and `-multiply_defined,suppress`). A missing symbol there is still only discovered when it is first called.

## Where placeholder bodies live

| File | Role |
|---|---|
| `native/src/engine_stubs_generated.cpp` | Weak fallback bodies (`__attribute__((weak))`, plus `ASM_SYM`-labelled `_stub_fn_N` aliases) and zero-filled weak vtable/typeinfo blobs. |
| `native/src/xbox_link_stubs.cpp` | Typed stand-ins for Xbox SDK, Bink, NUI and Dm entry points that no DC3 body will ever replace. |
| `native/src/native_link_glue.cpp` | Native counterparts of PPC-only definitions: template instantiations that `src/link_glue.cpp` provides for the PPC link (`CopyRef`, `BinStream` operators on the ObjPtr family), plus bodies from Xbox-only TUs. |
| `src/link_glue.cpp` | PPC build only. Do not remove definitions from it. |

To count the current stub population, use:

```bash
grep -c '_stub_fn_' native/src/engine_stubs_generated.cpp                  # 42 on 2026-11-13
grep -c '__attribute__((weak' native/src/engine_stubs_generated.cpp       # 74 on 2026-11-13
```

Those are source-line counts. To learn what the *linked binary* actually binds, run the gate below.

## "Real definitions override these automatically" is not true

There are three ways the weak stub ends up as the body that runs. All three have occurred in this tree.

1. **The stub is the only definition of a symbol that a real object references.**
   - `LiveCameraInput.cpp` declared an anonymous-namespace `YUVtoRGB` it never defined.
   - A weak stub satisfied the reference and returned 0, so every Kinect colour texel decoded to black.
   - Fixed when `LiveCameraInput.cpp` got its real inline body. The deletion note is still at the end of `engine_stubs_generated.cpp`.
2. **Signature drift makes the real body a different symbol.**
   - `MakeString.cpp` defined `ValidateThreadId(DWORD)`. On native, `DWORD` is `unsigned int`, while `os/OSFuncs.h` declares `unsigned long`. So one object file both defined one overload and referenced the other.
   - `DspAllocate` was the same class of bug: the engine took `void *` where callers declare `IXAudioBatchAllocator *`.
   - `XNotifyCreateListener` (`DWORD` vs `QWORD`) was another, fixed in `eb62d26a1`.
3. **A `weak` stub can beat a `weak_odr` body.**
   - `NuiTransformSkeletonToDepthImage` (stub `_stub_fn_111`, deleted in `54681e861`): at `-O2` clang inlined the header body everywhere and emitted no out-of-line copy. That left the stub as the only definition.
   - At `-O0`, which the web debug build uses, every call went out of line and hit the stub.

A zero-filled weak **vtable** stub is the same failure, applied to virtual dispatch. If a class's key function (its first non-inline virtual) is not defined in any compiled `.cpp`, the compiler emits no vtable and the zero blob wins. See [reference/PORTING.md](reference/PORTING.md#key-functions-and-zero-filled-vtables).

**The only way to answer "which body did the linker choose" is to look at the linked binary.** Source inspection cannot answer it.

## The gate: `scripts/native/check_stub_shadow.py`

```bash
# after building native/build/dc3-native
python3 scripts/native/check_stub_shadow.py --build-dir native/build
python3 scripts/native/check_stub_shadow.py --build-dir native/build --all    # + the LIVE worklist
python3 scripts/native/check_stub_shadow.py --build-dir native/build --json   # machine-readable
python3 scripts/native/check_stub_shadow.py --build-dir native/build --self-test
```

Other flags: `--target` (default `dc3-native`), `--binary`, `--skip-unresolved`, `-v`.

It is **not wired into the default build**: run it by hand. Exit status is `0` clean, `1` when a stub body is bound in the final binary while a real definition exists (or a new unresolved symbol appeared), and `2` for a usage error.

What it does:

1. Parses the target's real link line out of `build.ninja`.
2. Runs `nm` on the stub object to list the symbols it defines, then on every other link input for the same names.
3. **Disassembles the final executable** for each duplicated symbol and fingerprints the stub shape (the `HX_STUB_TRACE` preamble plus a zero return).
4. Reports stubs that are the *only* definition yet are referenced by a real object. Failure modes 1 and 2 above live here, and a duplicate-definition test cannot see them.
5. Runs `ldd -r` for still-unresolved symbols, against `UNRESOLVED_BASELINE` in the script.

`--self-test` pins the fingerprint in both directions, against bodies whose nature is not in question. That control exists because an early fingerprint classified the real function `dc3::StubTraceHit` as a stub.

Two parts of the script were written before `--no-undefined` and have not been updated:

- Its module docstring still describes the `ignore-all` link.
- `UNRESOLVED_BASELINE` is the 2026-08-19 set, most of which `eb62d26a1` then defined.

A shrunk unresolved set shows up as `unresolved_fixed` in `--json`. Trimming the baseline is how you tighten the gate.

## Finding a hole at runtime

Undefined dynamic symbols are lazily bound, so a binary can boot fine and die on the first call into a missing symbol. To make every hole fail at startup instead:

```bash
LD_BIND_NOW=1 ./native/build/dc3-native     # dies at startup, naming the first hole
ldd -r native/build/dc3-native 2>&1 | grep -oP 'undefined symbol: \K\S+' | sort -u | c++filt
```

After `eb62d26a1`, the only undefined dynamic symbols expected are the standard weak ones (`__gmon_start__` and the `_ITM_*TMCloneTable` hooks).

`DC3_STUB_TRACE=1` makes every `HX_STUB_TRACE` stub log its hits. `DC3_STUB_TRACE_DUMP=<file>` persists the table at exit (`native/src/main_native.cpp`), and the HTTP server serves it at `/api/stubs` (`native/src/platform/HttpServer.cpp`). The table only covers stubs on the path you exercised. A menu-only boot never reaches a stub that sits behind gameplay, audio effects or hardware, so treat the hit table as a worklist, never as a regression gate.

`MILO_ASSERT` is non-fatal on native, so a clean exit code is not evidence of anything on its own. In a headless run, the signal that carries weight is a zero `FAIL:` count, with a control that produces `FAIL:` lines on known-bad input.

## The trap: dc3's `native/src/platform/` copy is usually not what links

`native/CMakeLists.txt` builds `DC3_NATIVE_CORE_SOURCES_ENGINE` from `DC3_NATIVE_CORE_SOURCES` and then `list(REMOVE_ITEM ...)`s the files that `libmilo-engine.a` provides. That was 31 files on 2026-11-13, counted with `awk '/list\(REMOVE_ITEM DC3_NATIVE_CORE_SOURCES_ENGINE/{f=1;next} f&&/^\)/{exit} f&&/src\//{n++} END{print n}' native/CMakeLists.txt`. Examples include `File_Native.cpp`, `Rnd_Wgpu.cpp`, `Mesh_Wgpu.cpp`, `Tex_Wgpu.cpp`, `BoneSetup.cpp`, `MaterialSetup.cpp`, `Synth_Stub.cpp` and `src/audio/AudioDevice.cpp`.

Every target that builds today gets the **engine's** copy of those files: dc3-native, milo-viewer, render-test, milo-tests, the three export tools through `DC3_EXPORT_TOOL_SOURCES`, and dc3-web. dc3-web's `DC3_WEB_CORE_SOURCES` lists none of the removed files, and it links `milo-engine` (since `2a51acd7e`). The dc3 copies of the removed files therefore compile into no target that builds.

**Before editing anything under `native/src/platform/`:**

- Check that `REMOVE_ITEM` block.
- Confirm with `nm` which object actually defines the symbol you care about.
- Fix the engine copy (`../milo-native-engine/src/platform/...`).

This is how the `DspAllocate` bug survived a fix to the dc3 file. It is also why the `FileEnumerate` fix had to land in the engine (`84f9a8d`). That bug: the POSIX `FileEnumerate` lacked `File_Win.cpp`'s `qualified == "."` special case, so any pattern not starting with a wildcard enumerated empty.

## The Kinect colour path is not black — it is unreachable

`dc3-native` never constructs a `LiveCameraInput`:

- `LiveCameraInput::PreInit()` / `Init()` are called from `src/App.cpp` only in the Xbox branch (the `#else` of the `#ifdef HX_NATIVE` around `App::App`'s boot).
- On native, `GestureMgr::Init()` (`src/system/gesture/GestureMgr.cpp`) runs `GestureMgr_NativePreInit()` / `GestureMgr_NativeInit()` from `native/src/platform/GestureMgr_Native.cpp` and returns before its own `LiveCameraInput::PreInit()` call.

`GestureMgr::GetLiveCameraInput()` is therefore always null on native, and its consumers check for that. `HamUI::DrawDebug` (`src/lazer/meta_ham/HamUI.cpp`) returns immediately under `HX_NATIVE`. Native body input comes from the native sensor path instead; see [SCORING_ENV_VARS.md](SCORING_ENV_VARS.md).

Before a live NUI colour feed could work, the decode path would need these:

1. **LP64 pointer width.** The `TextureStore::UpdateFrom*Buffer*` bodies hold texel and source pointers as `uintptr_t`. They already do.
2. **LP64 unsigned wrap.** Pitch-minus-width row steps must be computed as signed `int`, not `unsigned int`. An unsigned difference wraps to about 4e9; on LP64 that zero-extends into a jump of several GB instead of stepping backwards. The current code carries the signed form; the comment near `srcPtr` in `src/system/gesture/LiveCameraInput.cpp` explains it.
3. **Endian-dependent decode.** It reads UYVY as 32-bit words and takes `>>24` as Cb and `>>8` as Cr, which is the big-endian layout. A native NUI shim must hand it big-endian-ordered words, or red and blue come out swapped.
4. **Native implementations of the source chain.** The `D3DLineTexture_LockRect` hop exists (`native/src/platform/NuiImageSurface_Native.cpp`). The rest of the chain does not: `NuiImageStreamGetNextFrame` → `NUI_IMAGE_FRAME::pFrameTexture` → `LockStream()`.

`native/tests/test_camera_yuv_decode.cpp` checks the decode at unit level. It is **byte-order-blind by construction**: the input is packed and read back through `uint32_t`, so host endianness cancels on both sides. It does not test point 3.

## Checking PPC neutrality of a shared-`src/` edit: not by `report.json` md5

The raw md5 of `build/373307D9/report.json` is **not stable across runs of the same tree**. The report's `provenance` block carries `cache_hits` / `cache_misses`, and those depend on how warm the objdiff cache was. Compare the measures instead:

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

Run it against a worktree built from the branch point. `fuzzy_match_percent` is relocation-sensitive, which is what lets this comparison see a symbol-name fix that normalized mode cannot. The `createFilter` `extern "C"` mangling fix in `src/system/dsp/EQEffect.cpp` was found that way. Give the comparator a negative control (a deliberately perturbed copy of one report) before trusting a "0 differences" result.

## `cmake --build` with no target

Builds again since `wgpu-window-test` was deleted (2026-11-13, H-2); it had not compiled since `gfx/GpuDevice.h` moved into the engine in `5cfbcba9b`. `MILO_TEST_REQUIRED_TARGETS` in `native/CMakeLists.txt` stays an explicit target list. Still qualify "the native build is green" with the target.

## Detection recipes

```bash
# All asm-labelled stubs, demangled (21 lines on 2026-11-13; the other
# _stub_fn_ lines are plain extern "C" bodies whose name is the symbol)
grep '_stub_fn_.*__asm__(ASM_SYM("' native/src/engine_stubs_generated.cpp | \
  sed 's/.*__asm__(ASM_SYM("//;s/")).*//' | c++filt | sort

# Count by enclosing class/namespace
grep '_stub_fn_.*__asm__(ASM_SYM("' native/src/engine_stubs_generated.cpp | \
  sed 's/.*__asm__(ASM_SYM("//;s/")).*//' | c++filt | \
  grep -oP '^[A-Za-z:_~]+' | sort | uniq -c | sort -rn | head -30
```

Note the `ASM_SYM("...")` wrapper. The stubs are written `__asm__(ASM_SYM("<mangled>"))`, so that macOS can prepend `_`. The older recipe that matched a bare `__asm__("` no longer extracts anything.

## History

The 2026-08-19 measurements this page used to carry have been removed rather than left to go stale: link-input symbol counts, the 24-symbol unresolved baseline, a per-subsystem "undecomp'd" table, and a `ctest` split. They are in git history. Related archived pages:

- [archive: native/STUB_BURNDOWN.md](../archive/2026-11-13-native-doc-audit/native/STUB_BURNDOWN.md)
- [archive: native/DECOMP_GAPS.md](../archive/2026-11-13-native-doc-audit/native/DECOMP_GAPS.md)
