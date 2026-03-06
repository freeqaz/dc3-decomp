# HX_NATIVE Guards Audit

Comprehensive audit of all `#ifdef HX_NATIVE` guards in `src/` that aren't just logging.
Goal: understand every hack, classify risk, and track cleanup/decomp work.

## Summary

| Category | Count | Risk | Action |
|---|---|---|---|
| STUB | 10 | Medium | Implement properly or verify not needed |
| NULL_GUARD | 39 | Low-Med | Most are correct — globals not init'd on native |
| CRASH_FIX | 35 | High | Many mask real bugs — need proper fixes |
| SAFETY | 20 | High | Object lifecycle hacks — invasive but necessary |
| TYPE_FIX | ~40 | Low | Correct LP64 fixes, no cleanup needed |
| PLATFORM_IMPL | ~120 | Low | Legitimate platform differences |
| SKIP_FEATURE | ~50 | Low | Xbox-only features, STLport instantiations |

---

## HIGH RISK — Needs attention

### Object Lifecycle Tracking (SAFETY)
**Files:** `obj/Object.cpp`, `obj/ObjPtr_p.h`, `obj/Dir.h`, `obj/Object.h`

The most invasive hack. Adds:
- `HmxObjectIsLive()` — global `unordered_set<Object*>` tracking all live objects
- `gSuppressRefErase` — prevents ObjRef ring modification during ReplaceList iteration
- `gSuppressDirPtrDelete` — prevents HasDirPtrs crash during MergeObjectsRecurse
- Snapshot-based `ReplaceRefs` — copies ObjRef ring to vector before iterating
- `ObjDirItr` — skips null, dead, and null-vptr entries

**Why it exists:** Dir merges (MergeObjectsRecurse) corrupt the ObjRef ring, causing
infinite loops and use-after-free. The PPC build doesn't hit these because the memory
allocator reuses slots differently and STLport containers have different invalidation rules.

**Proper fix:** Understand why the ObjRef ring gets corrupted during merge. Likely a
missing Unlink/Link call or wrong iteration order. Would require careful analysis of
the ObjRef lifecycle in the original binary.

### MILO_FAIL → WARN fallthrough (CRASH_FIX)
**Files:** `obj/DataFunc.cpp:578,962,1024`, `obj/Object.cpp:501,523,567`

On PPC, `MILO_FAIL` shows a dialog and halts. On native, it just logs and continues.
Functions that FAIL then fall through to undefined behavior — using uninitialized values,
dereferencing null, etc.

**Current guards:**
- `DataFunc.cpp:578` — null check NewObject result, return default DataNode
- `DataFunc.cpp:962,1024` — WARN instead of FAIL on object not found
- `Object.cpp:501,523,567` — WARN instead of FAIL, return 0 on property not found

**Proper fix:** Make native MILO_FAIL either abort or longjmp. Or ensure all FAIL
callsites have proper fallback paths.

### DirLoader Corruption Guards (CRASH_FIX)
**Files:** `obj/DirLoader.cpp:654,662,855`

- `:654` — early return on RealEof instead of reading past end
- `:662` — skip objects with null/corrupted vtable pointer
- `:855` — null/vptr check on newly created objects

**Why it exists:** Deserialization sometimes creates objects with corrupt vtables.
On PPC this might crash silently or happen to work. On native, null vptr = instant segfault.

**Proper fix:** Find the root cause of vtable corruption. Likely a type registration
issue — factory creates wrong type, or type not registered for native.

### UI Auto-Advance (CRASH_FIX)
**Files:** `ui/UI.cpp:544,623,665`, `ui/UIPanel.cpp:55`

- `UI.cpp:544` — auto-advance stuck screens after 120 frames
- `UI.cpp:623` — skip exit-wait (animations don't complete)
- `UI.cpp:665` — skip entering check (animations don't complete)
- `UIPanel.cpp:55` — force-finish panels stuck in loading state

**Why it exists:** Many panel transitions depend on animations completing or loaders
finishing. Without rendering/animation systems fully working, panels get stuck forever.

**Proper fix:** Implement animation ticking, or make panel transitions not depend on
animation completion. The 120-frame timeout is a reasonable fallback but hides real issues.

### Stream/Data Corruption Guards (CRASH_FIX)
**Files:** `utl/BinStream.cpp:130,206,224`, `obj/DataNode.cpp:770`, `obj/DataArray.cpp:399,410`

- BinStream: abort on empty rev stack, string length overflow, bad size
- DataNode: abort on unrecognized node type (stream corruption)
- DataArray: guard empty conditional stack on kDataElse/kDataEndif

**Why it exists:** ChunkStream forward-only reads sometimes desync on native, causing
garbage to be interpreted as type tags or string lengths.

**Proper fix:** Fix ChunkStream seeking/buffering to be robust. Many of these guards
are actually good defensive programming regardless.

---

## MEDIUM RISK — Should improve but functional

### Stubs (10 functions)

| File | Function | What's missing |
|---|---|---|
| `gesture/DepthBuffer3D.cpp:149` | Save/Copy/Load | Kinect depth buffer — not needed |
| `gesture/StreamRecorder.cpp:252` | Poll() | Stream recording — not needed |
| `hamobj/MoveDir.cpp:1261` | UpdateOverlay | Dance overlay rendering |
| `hamobj/HamRibbon.cpp:140` | UpdateChase | Ribbon animation chase logic |
| `hamobj/HamCharacter.cpp:718` | Poll() | Character polling (IK, etc.) |
| `synth/Emitter.cpp:125` | Poll() | 3D spatial audio |
| `rndobj/Part.cpp:547` | Load() | Particle system deserialization |
| `world/PhysicsVolume.cpp:237` | Load() | Physics volume deserialization |
| `world/Crowd3DCharHandle.cpp:54` | SyncProperty | 3D crowd character sync |
| `world/Reflection.cpp:154` | Highlight() | Reflection highlight rendering |

### Text.cpp Raw Offset Replacements (PLATFORM_IMPL, ~15 guards)

Replaced `int*` pointer arithmetic with struct field access:
- `BlacklightPacket` struct instead of `int[8]` array
- `mat->GetColor()` instead of `*(Hmx::Color*)(mat + 0x2c)`
- `mLocalXfm.v.x` instead of `*(float*)(this + 0x1c)`
- UI camera selection for text rendering passes

These are correct and clean but represent decomp work that could improve the PPC side too.
The raw offsets in the `#else` branches suggest struct layouts that aren't fully decompiled.

### Null Guards for Uninitialized Globals (39 guards)

Most are correct — these globals genuinely aren't initialized on native:
- `TheMetaMusic` (7 guards in MetaPanel) — SongDB/HamMaster not created
- `TheNetCacheMgr` (5 guards in MainMenuPanel, StorePanel, System) — no network
- `TheGameMode` (2 guards in GameMode) — different init path
- `mListDir` (3 guards in UIList) — list dirs sometimes fail to load

### Text.cpp Rendering Bounds (CRASH_FIX)

- `:569` — bounds check numFaces (0 < n <= 100000)
- `:585` — clamp garbage displayableChars to 0
- `:618` — clamp to available verts instead of assert
- `:627` — clamp displayableChars to fixedLength instead of assert

These indicate that `SyncMeshes` sometimes gets called with corrupt state.
Worth investigating whether this is a data issue or a logic bug.

---

## LOW RISK — Correct platform abstractions

### LP64 Type Fixes (~40)
- `(int)ptr` → `(intptr_t)ptr` in Symbol, TypeProps, DataArray
- `unsigned int` → `size_t` in operator new/new[] (~20 files)
- `.begin()` → `.data()` for pointer arithmetic
- `count * 4` → `count * sizeof(Type*)` for allocations
- `u32` → `unsigned long` in BinStream

### Platform Implementations (~120)
- Endian swap logic (LE host vs BE)
- ChunkStream: 2 buffers, sync decompression, no endian swap
- MemMgr/PoolAlloc: malloc/free instead of custom heap
- Synth: CreateNativeSynth factory
- OS: NativeArchiveInit, NativeArkRead
- STLport → libstdc++ type differences
- POSIX vs MSVC (strcasecmp, strtoull, s_addr)

### Skip Features (~50)
- STLport explicit template instantiations (not needed with libstdc++)
- Xbox threading (CreateThread, XSetThreadProcessor)
- Xbox services (Memcard, Leaderboards, Kinect, Holmes)
- Endian swaps in ChunkStream/AsyncFile (already LE)

---

## Decomp Opportunities

Several `HX_NATIVE` guards expose struct layouts that aren't fully decompiled on the PPC side.
Fixing these in the decomp would let us remove the guards:

### Text.cpp — BlacklightPacket and raw offsets
The native code uses `BlacklightPacket` struct fields while PPC uses `int*` casting.
If we define the struct properly in the header and use it on PPC, both paths converge.

### UILabel.cpp — ResourceDirPtr casts
PPC code casts `mLabelDir` to `ResourceDirPtr&` for Load/Save. Native accesses members
directly. The cast suggests ResourceDirPtr and the actual member type share layout — could
be cleaned up with proper type definition.

### UIFontImporter.cpp — raw offset 0x3c
PPC reads `*(float*)(font + 0x3c)` for base kerning. Native uses `font->mBaseKerning`.
If the struct is decompiled with the field at the right offset, the guard is unnecessary.

### Spotlight.cpp — raw offset access in Poll
PPC uses pointer arithmetic for spotlight mesh properties. Native uses proper accessors.
Decompiling the mesh/material structs would unify both paths.

### Crowd.cpp — raw offset 3D char rendering
PPC uses raw `int*` offsets into crowd character data. Native skips 3D crowd entirely.
This is a significant chunk of un-decompiled struct access.

---

## Tracking

### Decomp work (removes guards by fixing the PPC code)

- [ ] **Text.cpp BlacklightPacket** — PPC uses `int[8]` + raw `int*` arithmetic. Native uses
  named struct fields. The struct layout is known: `{RndMesh*, Hmx::Color(4 floats), float, int, RndCam*}` = 32 bytes.
  `mesh + 0x128` = mMat.mObject (ObjPtr internals), `mat + 0x2c` = mColor (BaseMaterial::mColor at 0x2c),
  `mat + 0x228` = mDirty. Replacing raw offsets with `mat->GetColor()` / `mat->MarkDirty()` would
  unify 6 HX_NATIVE blocks. **Functions: QueueBlacklightPacket, DrawBlacklight, DrawShowing** (~15 guards)
  Progress: `BlacklightPacket` now uses named fields on both builds; `QueueBlacklightPacket`
  and `DrawBlacklight` are unified (no `HX_NATIVE` split) and match improved
  (`93.7% -> 97.6%`, `91.0% -> 95.7%`). `DrawShowing` material color save/override/restore
  blocks are also unified (`71.0% -> 72.7%`). Camera-pass `HX_NATIVE` guards in
  `DrawShowing` were moved behind shared helper calls, preserving function match.
- [ ] **UIFontImporter 0x3c** — `*(float*)(font + 0x3c)` = font->mBaseKerning.
  Field offset is confirmed in `RndFontBase` (`mBaseKerning` at `0x3c`).
  `OnSyncWithResourceFile` now uses direct `pKern->mBaseKerning` with no `HX_NATIVE` split.
  Remaining: remove raw-offset/list-crawl guards in `GetGennedFont (88.9%)` and
  `SyncWithGennedFonts (86.0%)` without hurting PPC match.
- [ ] **UILabel ResourceDirPtr casts** — PPC still stores `mLabelDir` in a non-ResourceDirPtr
  layout and uses casts, but repeated per-site cast guards are now centralized via
  `LABEL_STYLE_DIR` in `UILabel.cpp`.
  Progress: removed duplicated guard/cast blocks in `PreLoad`, `PostLoad`, and
  `OldResourcePreload` while keeping match stable (`PreLoad: 84.3% -> 84.5%`).

### Workable near-match functions (from query, independent of guards)

| Function | Unit | Match | Notes |
|---|---|---|---|
| UIFontImporter::HandmadeFontChanged | ui/UIFontImporter | 96.1% | |
| RndMat::LoadOld | rndobj/Mat | 95.1% | |
| UILabel::Highlight | ui/UILabel | 94.4% | ResourceDirPtr cast |
| UIFontImporter::FindTextForFont | ui/UIFontImporter | 94.4% | |
| ObjectDir::Find\<MetaMaterial\> | rndobj/Mat | 89.7% | |
| UIFontImporter::GetGennedFont | ui/UIFontImporter | 88.9% | 0x3c offset |
| RndMatAnim::Load | rndobj/MatAnim | 87.4% | |
| UIFontImporter::SyncWithGennedFonts | ui/UIFontImporter | 86.0% | |
| RndMatAnim::Copy | rndobj/MatAnim | 85.9% | |
| RndMat::UpdatePropertiesFromMetaMat | rndobj/Mat | 83.9% | |

### Infrastructure / native port fixes

- [ ] Investigate ObjRef ring corruption root cause (obj/Object.cpp, ObjPtr_p.h, Dir.h)
- [ ] Make MILO_FAIL behavior consistent (abort or longjmp on native)
- [ ] Fix DirLoader vtable corruption source (DirLoader.cpp:662,855)
- [ ] Fix ChunkStream cross-chunk reads properly (ChunkStream.cpp:216)
- [ ] Remove UI auto-advance once animations tick properly (UI.cpp:544)

### Stubs to implement (for native boot)

- [ ] HamCharacter::Poll — character IK, polling
- [ ] RndParticleSys::Load — particle deserialization
- [ ] HamRibbon::UpdateChase — ribbon chase animation
- [ ] MoveDir::UpdateOverlay — dance overlay rendering
