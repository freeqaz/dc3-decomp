# Native Port — Decomp Gaps & Missing Units

Inventory of decomp gaps affecting the native build. Prioritized by impact on rendering and UI.

**Last updated**: 2026-03-06

## Current State

- **Track A (Engine Boot)**: Boots to `choose_mode_screen`, 51 draw calls/frame, 5000+ frames stable. Stuck — no further screen navigation without `Flow::Enter()`.
- **Track B (Milo Viewer)**: Full rendering pipeline. 14/44 demo shots render (8 broken YAML paths).
- **Weak stubs**: `engine_stubs_generated.cpp` has ~2530 weak function stubs. Any real .cpp implementation automatically overrides them.

## Priority 1: Rendering & Draw Pipeline

These directly affect what's visible on screen.

| Class | Method | Status | Impact |
|-------|--------|--------|--------|
| `MeterDisplay` | `DrawShowing()` | **73.5%** (FPR regswaps) | Score/progress meters render |
| `Spotlight` | `DrawShowing()` | **95.6%** (stack frame, control flow) | Stage lighting — **Implemented** |
| `SpotlightDrawer` | `DrawShowing()` | **100%** | Spotlight drawer selection |
| `RndTexBlender` | `DrawShowing()` | **79.6%** (regswaps, control flow) | Texture blend effects |
| `MoveDir` | `DrawShowing()` | **24.3%** (complex collision viz) | Dance move overlay (debug) |
| `InlineHelp` | `DrawShowing()` | **81.5%** (regswaps) | On-screen help text positioning |
| ~~`RndParticleSys`~~ | ~~`Poll()`~~ | **100%** | Particles now animate |
| `LabelShrinkWrapper` | `Poll()` | **Done** (pass-through to UIComponent::Poll) | Label auto-sizing |
| ~~`TexProc`~~ | ~~`DrawShowing()`, `Poll()`~~ | **Done** | Poll 100%, DrawToTexture 92.3% |

### Text Rendering — Mostly Complete
- `FitTextJust()` — **Implemented** (94.3% match, binary search scale). Remaining: FPR regswaps.
- `AllocateMeshes()` — Clamps displayableChars instead of asserting (font data sometimes corrupt)
- Multiple `#ifdef HX_NATIVE` guards for font page validation and vertex allocation safety

### Material/Shader Gaps
- `RndMat::Init()` — Skips MetaMaterial loading (`sMetaMaterials = nullptr`). MetaMaterials only control editor property permissions, not rendering. Blocked by MetaMaterial::Load() decomp gap.
- All `RndShader*::Select()` — Intentionally stubbed (24 methods). WebGPU renderer bypasses Xbox shader pipeline entirely.
- All `RndShader*::CalcShaderOpts()` — Return 0 (12 methods). Correct for native — these produce Xbox HLSL macro bitmasks.
- `PostProc_NG::DoVelocity()` — Motion blur stubbed (hardcoded ILP32 offsets)

## Priority 2: UI & Screen Flow

These affect menu navigation, list population, and screen transitions.

| Class | Method | Status | Impact |
|-------|--------|--------|--------|
| `OptionsPanel` | `Poll()` | **80.7%** (control flow, virtual-base helper) | Options purchasing/update flow still diverges |
| `ContentLoadingPanel` | `Poll()` | **71.2%** (control flow, regswaps) | Loading progress animation logic still diverges |
| `ProfileMgr` | `Poll()` | **Stubbed** | Profile management inactive |
| `SaveLoadManager` | `Poll()` | **Stubbed** | Save/load state stuck |
| `Flow` | `Enter()` | **AT_LIMIT stub** | **Single biggest Track A blocker** — menus can't navigate past choose_mode |
| ~~`LoadingPanel`~~ | ~~`Poll()`~~ | **Done** | Real panel update logic lives in `ContentLoadingPanel.cpp` |

### UIList Poll Note
- `UIListWidget::Poll()`, `UIListSlotElement::Poll()`, and `UIListSubListElement::Poll()` are **not** real native-port gaps.
- They are trivial inline virtual defaults in source, and the symbol dump shows them folded into ICF-merged tiny functions.
- The real list polling path is already implemented and complete through `UIList::Poll()`, `UIListDir::PollWidgets()`, and `UIListSlot::Poll()`.

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
| `PhysicsManager` | `Enter()` | **Stubbed** | Physics init missing |
| ~~`PhysicsManager`~~ | ~~`Poll()`~~ | **Done** | DefaultPhysicsManager::Poll() in DefaultPhysicsManager.cpp |
| `SeqInst` | `Poll()` | **Stubbed** | Audio sequencer inactive |
| ~~`GroupSeqInst`~~ | ~~`Poll()`~~ | **Done** | All subclasses implemented in Sequence.cpp |
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

## Recommended Next Steps

Sorted by impact × feasibility:

### Immediate (data fixes, no code risk)
1. **Fix demo.yaml paths** — 8 broken asset paths, 1 duplicate key (`dare_front`), 1 undefined scene (`dare_streets`). Unlocks 44-shot showcase.

### High Impact Decomp Gaps
2. **MeterDisplay::DrawShowing()** — Score/progress meters. Medium effort.
3. **RndTexBlender::DrawShowing()** — Texture blend effects. Medium effort.
4. **OptionsPanel::Poll()** — Real implementation exists and looks likely fixable from current 78.6%.
5. **ContentLoadingPanel::Poll()** — Real implementation exists and looks likely fixable from current 71.2%.

### Critical Track A Blocker
6. **Flow::Enter()** — Flow graph entry. Without this, engine can't navigate beyond choose_mode_screen. Large effort — flow graph is a complex state machine.
7. **Locale data loading** — UI text shows raw tokens. Likely a file path / archive issue, not a decomp gap.

## Key Architecture Notes

- **Weak stubs**: `native/src/engine_stubs_generated.cpp` uses `__attribute__((weak))` — any real implementation in a compiled .cpp automatically overrides the stub. **Critical**: If a class's "key function" (first non-inline virtual) is missing from its .cpp file, GCC can't emit the vtable and the weak zero-filled stub wins → null vtable dispatch crash. Fixed for: MemStream, OvershellSlot, StreamReceiverFile, RandomIntervalGroupSeqInst.
- **ObjRef ring corruption**: Largely resolved. Two-pass deletion + ref ring snapshots + gSuppressRefErase handle object lifecycle correctly.
- **Stream deserialization**: Multiple guards against corrupt data suggest some Load() functions have bugs or version mismatches.
- **Vtable key function pattern**: If a class in `engine_stubs_generated.cpp` has zero-filled weak vtable, check if its first non-inline virtual is defined in its `.cpp` file. See `memory/MEMORY.md` for details.
