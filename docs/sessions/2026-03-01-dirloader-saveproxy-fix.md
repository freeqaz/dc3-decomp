# DirLoader SaveProxy Fix & UILabel PostLoad Corrections

**Date**: 2026-03-01
**Focus**: Fix .milo loading crashes in native port caused by unconsumed SaveProxy data and decomp bugs

## Problem

Complex .milo files (e.g., `timey_wimey_elements.milo_xbox` — 1.7MB, 145+ objects, 10+ nested subdirs) crashed during loading. The root causes were:

1. **SaveProxy data left unconsumed in stream** — causing all subsequent object reads to desync
2. **Decomp bugs in UILabel::PostLoad** — wrong pointer in PopRev, null pointer dereference

## Debugging Approach

### What Worked Best

**1. Stream position tracing (`Tell()`) at every boundary** — The single most effective technique. Adding `printf` with `Tell()` at every PreLoad/PostLoad/ReadDead transition made desyncs immediately visible. When `rev=32` (DirLoader's mRev=0x20) appeared where `rev=11` (Tex revision) was expected, it told us exactly where unconsumed data sat in the stream and how many bytes were wrong.

**2. Ghidra decompilation to settle decomp questions** — The UILabel `Dir()` vs `this` bug could have taken hours of guessing. One decompile of the original binary showed the virtual-base-adjusted `this` pattern unambiguously. Same for confirming the original `LoadObjs` doesn't peek — that ruled out an entire class of fixes.

**3. Understanding Save/Load symmetry** — Reading `SaveObjects` and `SaveProxy` side-by-side with `LoadObjs` revealed the SaveProxy blob nobody was consuming. The format detection heuristic (plain int > 28, upper 16 bits zero) came directly from understanding how DirLoader mRev differs from packed class revisions.

**4. ASan (when it was enabled)** — While noisy, it pinpointed exact allocation/deallocation mismatches (`new[]` vs `delete` in RndMesh) and null dereferences (`TheUI` in AllowEditText) that would have been silent corruption on Xbox. ASan was later disabled globally in CMakeLists.txt due to complex virtual inheritance VTT issues (session 15-16), but the fixes it surfaced here remain valid.

### What Didn't Work

The earlier session's **pre-PreLoad peek approach** — trying to detect DirLoader-format data before PreLoad and skip objects entirely. This fought the architecture instead of understanding it. The correct fix was post-ReadDead consumption, which lets normal PreLoad/PostLoad run first.

## Investigation Details

### SaveProxy Mechanism (Dir.cpp / DirLoader.cpp)

`DirLoader::SaveObjects` writes each object as:
```
obj->Save(bs) → WriteDeadAndMark(bs) → obj->SaveProxy(bs)
```

`ObjectDir::SaveProxy` (Dir.cpp:513) writes a **full DirLoader-format blob** after the dead marker when:
- `IsProxy()` is true
- `InlineProxy(bs)` is true (`kInlineCached` + cached stream, or `kInlineAlways`)

This means proxy ObjectDirs in cached `.milo_xbox` files have extra data after their dead markers that must be consumed.

### How the Original Game Handles It

Ghidra decompilation of `DirLoader::LoadObjs` (0x825a5de8) confirmed the original code does NOT explicitly consume SaveProxy data. The original game relies on LoadMgr's interleaved loading — `ObjectDir::PreLoad` creates sub-DirLoaders via `LoadInlinedFile` that consume the SaveProxy stream data as part of their normal loading cycle.

In the native port's synchronous loading model, this interleaving doesn't happen the same way for proxy dirs whose proxy files point to external .milo files loaded separately. The SaveProxy blob sits in the stream unconsumed.

## Fixes

### 1. DirLoader SaveProxy Consumption (DirLoader.cpp)

**After** ReadDead completes for an ObjectDir, peek at the next 4 bytes. If they match DirLoader format (plain int > 28, upper 16 bits = 0 — no class revision exceeds 28), consume the blob via `DirLoader::LoadObjects`.

```cpp
#ifdef HX_NATIVE
if (dynamic_cast<ObjectDir *>(obj)) {
    ChunkStream *cs = dynamic_cast<ChunkStream *>(mStream);
    if (cs && mStream->Eof() == NotEof) {
        int peekVal;
        *mStream >> peekVal;
        cs->Unreread(4);
        bool isDirLoaderFormat = (peekVal & 0xFFFF0000) == 0
                              && (peekVal & 0xFFFF) > 28;
        if (isDirLoaderFormat) {
            ObjectDir *proxySubDir = DirLoader::LoadObjects(mFile, nullptr, mStream);
            if (proxySubDir) delete proxySubDir;
        }
    }
}
#endif
```

Key difference from the broken earlier fix: this runs **after** normal PreLoad/PostLoad/ReadDead, only consuming the SaveProxy blob that follows.

### 2. UILabel::PostLoad PopRev Fix (UILabel.cpp)

**Bug**: `PopRev(Dir())` used the containing ObjectDir pointer, but `PushRev(this)` used the UILabel instance pointer. These are different objects.

**Confirmation**: Ghidra decompilation showed the original uses virtual-base-adjusted `this`, not `Dir()`.

**Fix**: Changed all three `bs.PopRev(Dir())` → `bs.PopRev(this)` in PostLoad.

**Decomp impact**: Match improved from 65.9% → 72.1% (+6.2pp). This was a real decomp bug, not just a native issue.

### 3. UILabel::AllowEditText Null Guard (UILabel.cpp)

**Bug**: `TheUI->DefaultAllowEditText()` crashes when `TheUI` is null (native viewer has no UIManager).

**Fix**: `#ifdef HX_NATIVE` null check on `TheUI` before dereferencing.

### 4. RndMesh::ClearCompressedVerts delete[] Fix (Mesh.cpp)

**Bug**: `mCompressedVerts` allocated with `new unsigned char[]` but freed with scalar `delete` via `RELEASE()`. ASan flags this mismatch on native (original Xbox custom allocator handles both identically via `MemFree`).

**Fix**: `#ifdef HX_NATIVE` to use `delete[]` instead of `RELEASE()`.

## Files Modified

- `src/system/obj/DirLoader.cpp` — SaveProxy consumption after ReadDead
- `src/system/ui/UILabel.cpp` — `PopRev(Dir())` → `PopRev(this)`, `TheUI` null guard
- `src/system/rndobj/Mesh.cpp` — `delete[]` for mCompressedVerts on native

## Test Results

### timey_wimey_elements.milo_xbox (1.7MB)
- 145+ objects across 10+ nested subdirs
- Includes: move_feedback (x2), phrase_meter (x2), text_feedback (x2), boxyman, shared.milo (3.8MB)
- All SaveProxy blobs consumed: animate_timeywimey.flow, boxyman, Hide_Boxyman_Feedback.flow, Reset_Anims.flow, Show_Boxyman_Feedback.flow, Start.flow, move_feedback0 (with nested flow.flow, reset.flow, set_bustamove.flow), move_feedback1
- **Result**: Loads completely, renders screenshot (1280x720)
- Screenshot: `archive/screenshots/timey_wimey_elements.png`

### discoballsml.milo_xbox (simple file)
- Still loads and renders correctly — no regression

### skeleton.milo_xbox
- Separate crash in `RndLine::SetNumPoints` — unrelated, pre-existing issue

## Key Insight: DirLoader Format Detection

DirLoader's mRev is a plain 32-bit int with value > 28 (current mRev = 0x20 = 32). Class revisions use `packRevs(altRev, rev)` format where altRev occupies the upper 16 bits. No class has rev > 28, so:

```
isDirLoaderFormat = (value & 0xFFFF0000) == 0 && (value & 0xFFFF) > 28
```

This reliably distinguishes DirLoader blobs from any class revision header in the stream.
