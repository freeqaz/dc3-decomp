# Native Port — Decomp Gaps & Missing Units

Inventory of decomp gaps affecting the native build. Prioritized by impact on rendering and UI.

## Priority 1: Rendering & Draw Pipeline

These directly affect what's visible on screen.

| Class | Method | Status | Impact |
|-------|--------|--------|--------|
| `MeterDisplay` | `DrawShowing()` | **Stubbed** | Score/progress meters invisible |
| `Spotlight` | `DrawShowing()` | **Stubbed** | Stage lighting missing |
| `SpotlightDrawer` | `DrawShowing()` | **Stubbed** | Spotlight manager missing |
| `RndTexBlender` | `DrawShowing()` | **Stubbed** | Texture blend effects missing |
| `MoveDir` | `DrawShowing()` | **Stubbed** | Dance move overlay invisible |
| `InlineHelp` | `DrawShowing()` | **Stubbed** | On-screen help text invisible |
| `RndParticleSys` | `Poll()` | **Stubbed** | Particles don't animate |
| `TexProc` | `DrawShowing()`, `Poll()` | **Stubbed** | Procedural textures don't render/update |
| `LabelShrinkWrapper` | `Poll()` | **Stubbed** | Label auto-sizing doesn't update |

### Text Rendering Workarounds (src/system/rndobj/Text.cpp)
- `FitTextJust()` — Partial impl in native_link_glue.cpp (no binary search for optimal scale)
- `AllocateMeshes()` — Clamps displayableChars instead of asserting (font data sometimes corrupt)
- Multiple `#ifdef HX_NATIVE` guards for font page validation and vertex allocation safety

### Material/Shader Gaps
- `RndMat::Init()` — Skips MetaMaterial loading (`sMetaMaterials = nullptr`)
- All `RndShader*::Select()` — Stubbed (24 methods in native_link_glue.cpp)
- All `RndShader*::CalcShaderOpts()` — Return 0 (12 methods)
- `PostProc_NG::DoVelocity()` — Motion blur stubbed (hardcoded ILP32 offsets)

## Priority 2: UI & Screen Flow

These affect menu navigation, list population, and screen transitions.

| Class | Method | Status | Impact |
|-------|--------|--------|--------|
| `UIListWidget` | `Poll()` | **Stubbed** | List widget animations don't update |
| `UIListSlotElement` | `Poll()` | **Stubbed** | List slot animations don't update |
| `UIListSubListElement` | `Poll()` | **Stubbed** | Sub-list animations don't update |
| `LoadingPanel` | `Poll()` | **Stubbed** | Loading state never updates |
| `OptionsPanel` | `Poll()` | **Stubbed** | Options panel never updates |
| `ProfileMgr` | `Poll()` | **Stubbed** | Profile management inactive |
| `SaveLoadManager` | `Poll()` | **Stubbed** | Save/load state stuck |
| `Flow` | `Enter()` | **Stubbed** | Flow graph entry logic missing |

### Screen Transition Workarounds (src/system/ui/UIScreen.cpp)
- ~~`UnloadPanels()` — Skipped on native (ObjRef crash)~~ **FIXED**: Real crash was null `sHamMaster` in `MetaPanel::Load`, not ObjRef corruption. UnloadPanels fully re-enabled.
- `SetTypeDef()` — Skips null panels

### Object System Workarounds (src/system/obj/)
- `Dir.cpp` — Two-pass object deletion to avoid dangling ObjRef walks
- `Dir.cpp` — `DeleteShared()` skipped (vtable corruption during cleanup)
- `Object.cpp` — `ReplaceRefs()` snapshots ref ring before processing
- `Object.cpp` — Multiple `if (!this)` guards for null object access
- `DataNode.cpp` — Returns safe defaults instead of crashing on bad data
- `DataFunc.cpp` — MILO_WARN instead of MILO_FAIL when objects not found
- `DirLoader.cpp` — Validates vtable after NewObject(), clears on EOF

## Priority 3: Game Logic & Animation

These affect gameplay but not the menu/rendering flow.

| Class | Method | Status | Impact |
|-------|--------|--------|--------|
| `Game` | `Poll()` | **Stubbed** | Core game loop inactive |
| `CharBones` | `Enter()` | **Stubbed** | Bone system doesn't initialize |
| `Flow` | `Enter()` | **Stubbed** | Flow graph entry missing |
| `PhysicsManager` | `Enter()`, `Poll()` | **Stubbed** | Physics inactive |
| `SeqInst` | `Poll()` | **Stubbed** | Audio sequencer inactive |
| `GroupSeqInst` | `Poll()` | **Stubbed** | Group audio sequencer inactive |
| `MovieImpl` | `Poll()` | **Stubbed** | Video playback dead |
| `StarsDisplay` | `Poll()` | **Stubbed** | Star rating display frozen |
| `HamCharacter` | `Poll()` (partial) | Complex list clearing skipped | Character update incomplete |

### Animation/Character Workarounds
- `HamRibbon::UpdateChase()` — Stubbed (needs Interp<Transform>)
- `MoveDir::UpdateOverlay()` — Returns 0 (complex overlay rendering)
- `CharTransDraw::~CharTransDraw()` — Uses method call instead of pointer arithmetic

### Stream Deserialization Guards
- `DrivenPropertyEntry::Load()` — Caps numOps to [0, 256]
- `DrivenPropertyMathOps::Load()` — Rejects rev > 20
- `FlowNode::Load()` — Aborts if numEntries out of [0, 256]

## Priority 4: Platform/Network (Not Needed for Single-Player)

| Category | Stub Count | Notes |
|----------|-----------|-------|
| Xbox XDK APIs | ~25 | XInput, XNet, marketplace, etc. |
| Kinect/NUI APIs | ~47 | Replaced by native YOLO pose server |
| Bink Video | 6 | BinkOpen, BinkGoto, etc. |
| JSON Library | ~14 | Network serialization |
| Crypto (Rijndael) | 6 | Network encryption |
| Xbox Live | DingoServer, HttpReqCurl, WebSvcMgr, RockCentral | All Poll() stubbed |

## Units to Focus On

Sorted by rendering impact — fixing these would most improve visual output:

### Tier 1: Would unlock new visual elements
1. **`rndobj/TexBlender.cpp`** — DrawShowing needed for texture blend effects
2. **`hamobj/MeterDisplay.cpp`** (or wherever MeterDisplay lives) — Score/progress meters
3. **`rndobj/Part.cpp`** — `RndParticleSys::Poll()` needed for particle animation
4. **`ui/UIListWidget.cpp`** — `Poll()` needed for list item animations

### Tier 2: Would improve existing rendering
5. **`rndobj/Text.cpp`** — Full `FitTextJust()` implementation (binary search scale)
6. **`rndobj/Mat.cpp`** — MetaMaterial loading support
7. **`rndobj/PostProc_NG.cpp`** — Motion blur (complex, low priority)

### Tier 3: Would enable gameplay features
8. **`lazer/game/Game.cpp`** — `Game::Poll()` for core game loop
9. **`flow/Flow.cpp`** — `Flow::Enter()` for flow graph execution
10. **`char/CharBones.cpp`** — `CharBones::Enter()` for bone initialization

## Key Architecture Notes

- **Weak stubs**: `native/src/engine_stubs_generated.cpp` uses `__attribute__((weak))` — any real implementation in a compiled .cpp automatically overrides the stub. **Critical**: If a class's "key function" (first non-inline virtual) is missing from its .cpp file, GCC can't emit the vtable and the weak zero-filled stub wins → null vtable dispatch crash. Fixed for: MemStream, OvershellSlot, StreamReceiverFile, RandomIntervalGroupSeqInst
- **ObjRef ring corruption**: Largely resolved. Two-pass deletion + ref ring snapshots + gSuppressRefErase (now covering ObjPtrList too) handle object lifecycle correctly. The "UnloadPanels crash" was actually a null `sHamMaster` dereference, not ring corruption.
- **Stream deserialization**: Multiple guards against corrupt data suggest some Load() functions have bugs or version mismatches
- ~~**Screen stacking**: `UnloadPanels` crash~~ **FIXED**: Screens now properly unload panels during transitions
