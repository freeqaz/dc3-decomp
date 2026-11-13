# Character Animation — Native Reference

How character animation flows from `.milo` clip data to skinned meshes on the native port: crowds, lip sync, blinks, IK poll order, and the conventions the bone tests rely on.

Last verified against the tree: 2026-11-13 (np-docs @ 2d678db7e).

## Data flow: clip -> bones -> skinned mesh

1. **Load.** FileMerger loads character `.milo` files and outfit packages. `CharClip` holds
   compressed keyframe samples (`CharBonesSamples`).
2. **Clip playback.** `CharDriver::Poll()` (`src/system/char/CharDriver.cpp`) drives a stack of
   `CharClipDriver`s:
   - `mFirst->PreEvaluate(beat, deltaBeat, dt)`, then `mFirst->Evaluate(...)`.
   - Then `ScaleAdd` of each clip into the bone buffer, or `GetClip()->ScaleDown(*mBones, deltaBeat)`.
3. **Bone buffer.** `CharBones` (`src/system/char/CharBones.h`) is a flat buffer split by
   `enum Type`: `TYPE_POS`, `TYPE_SCALE`, `TYPE_QUAT`, `TYPE_ROTX/Y/Z`. `CharBonesMeshes::PoseMeshes()`
   writes it into the bone `RndTransformable`s.
4. **GPU skinning.** `FillBoneUniforms()` in `../milo-native-engine/src/platform/BoneSetup.cpp`
   (the engine copy links; `native/src/platform/BoneSetup.cpp` is removed from every engine-linking
   target and has drifted):
   - Builds `skinMatrix = BoneOffsetAt(i) * WorldXfm()` per bone, for up to `kMaxBones = 40` bones
     per draw (`../milo-native-engine/src/gfx/UniformStructs.h`).
   - Any bone whose world translation has a component with `|v| >= 100000` is treated as garbage:
     it is logged ("BoneSetup: garbage WorldXfm ...") and not used as a skin matrix.
   - The WGSL vertex shader blends 4 influences per vertex (`boneWeightAt`/`boneIndexAt` in
     `../milo-native-engine/src/gfx/standard_wgsl.inc`).

In the game, the normal loop polls everything: `Character` -> `RndDir::mPolls` -> every
`CharPollable`, including `CharDriver` and `CharFaceServo`.

### milo-viewer vs dc3-native

The viewer (`native/src/viewer/`) is a tool, not the game loop.

- **Interactive mode** walks the character's `CharPollable`s (`ObjDirItr<CharPollable>`) in
  dependency order.
- **Screenshot/video modes** pose directly with `PoseMeshesWithFacing()` (`ViewerAnimation.cpp`:
  `ScaleDown` + `ScaleAdd` + `PoseMeshes`), bypassing `CharDriver`.
- Modes are a `std::variant` (`ViewerMode` in `native/src/viewer/ViewerCapture.h`).
- The viewer creates a `CharServoBone` (`bone.servo`) or `CharFaceServo` when the `.milo` lacks
  one, calls `StuffBones` to populate bone lists, and falls back to `CharTwistSolver`
  (`../milo-native-engine/src/char/CharTwistSolver.cpp`; a DC3 copy also exists in `native/src/char/`).

### "Characters don't animate" checklist

1. The character has a `CharDriver`, with `SetBones(...)` and `SetClips(...)` wired
   (`src/system/char/CharDriver.h`).
2. `CharDriver::Enter()` has run.
3. `CharDriver::Poll()` runs each frame via `RndDir::mPolls`.
4. Beat/time is advancing: `TheTaskMgr.SetSecondsAndBeat(...)` (`src/system/obj/Task.h`).
5. Grep the char/ files you are reading for `HX_NATIVE` before assuming the image path runs.

## IK poll order (feet)

Native uses the image's producer-first `ChangedBy` polarity by default, so IK effector writes
survive the frame: song driver -> `bone.servo` -> IK effectors. `Dc3PollOrderFixActive()` in
`src/system/char/CharPollGroup.cpp` decides this. It shipped as the default in `3fb97a370`.

Debug switches (native only):

| Env var | Effect |
|---|---|
| `DC3_POLL_ORDER_FIX=0` | Reversed (non-image) polarity; re-enables the fallback foot clamp and pelvis retarget |
| `DC3_FEET_POST_PLANT=1` / `DC3_FEET_POST_PLANT_OFF=1` | Force the post-poll foot plant clamp on / off (`src/system/char/CharIKFoot.cpp`) |
| `DC3_FEET_PELVIS_OFF=1` | Disable the fallback pelvis retarget |

## Crowd

### Crowd clip loading

`HamWardrobe::LoadCrowdClips` (`src/system/hamobj/HamWardrobe.cpp`) drives the `crowd_clips.fm`
FileMerger's `load_tempo` handler (`orig-assets/extracted/char/char_objects.dta`).

- **Era from venue:** rollerrink -> 70s, streetside -> 80s, houseparty -> 90s, dclive -> 00s,
  dci -> 10s. Any other venue -> 10s.
- **Six files per tempo and era**, for each of `female` and `male`:
  - `char/crowd/anim/<gender>_<tempo>.milo`
  - `char/crowd/anim/<gender>_base_<era>.milo`
  - `char/crowd/anim/<gender>_<tempo>_<era>.milo`
- `<tempo>` is slow, medium or fast. The built files are under `char/crowd/anim/gen/*.milo_xbox`,
  alongside `<gender>_base.milo_xbox`.
- **Merge semantics.** `MergeDirs` -> `MergeObject` (`src/system/obj/Utl.cpp`). When an object of
  the same name exists and the filter says `kMerge`, the existing object is updated with
  `Copy(new, kCopyFromMax)`. `CharClipGroup`'s `kCopyFromMax` copy appends each clip not already
  present by name (`src/system/char/CharClipGroup.cpp`).
- **Debugging.** `MILO_DEBUG_MERGE=1` logs every merge decision (native only).
- **Playback.** `HamWardrobe::PlayCrowdAnimation` calls `PlayGroup` on each registered crowd member
  (`mCrowdMembers`).

### WorldCrowd rendering

`src/system/world/Crowd.cpp`:

- **Registration.** WorldCrowd objects load their placement mesh and character list. The DTA
  `WorldCrowd` init sets type `band`, whose `enter` runs `{$this set_fullness 1 1}` and registers
  with `$hamwardrobe add_crowd` (`orig-assets/extracted/world/world_objects.dta`).
- **3D crowd.** `Load` passes the serialised force flag to `Force3DCrowd`. Every WorldCrowd in
  the five crowd venues saves force=1 (34 objects: dci 10, houseparty 7, rollerrink 6,
  streetside 6, dclive 5; measured by loading each venue `.milo`), so every instance moves into
  `m3DChars`. Each frame `Draw3DChars` draws the crowd member's `Character` at every instance's
  transform (`Apply3DCharXfm`), with the member's three `random*.pal` colours set as
  `kColorModModulate` first. The Characters are polled by `WorldCrowd::Poll`, so the crowd
  animates; rollerrink draws up to 57 members a frame (27 in some camera shots).
- **Billboards (impostors).** Each character type also has a 4-vertex billboard (`BuildBillboard`)
  drawn with `gImpostorMat` through an `RndMultiMesh`. For every character type with billboard
  instances, `DrawShowing` re-renders the Character into the shared `gImpostorTex[mLod]` every
  frame in `kDrawNormal`, crops the quad to the `CharCollide` screen rects, and draws it
  `kBlendSrc` with the 0x80 alpha cut. With force=1 there are no billboard instances, so in the
  shipped venues this path draws nothing.
- **Native:** runs the image's code (H-6, `np-crowd`). Until then `Load` forced
  `Force3DCrowd(false)`, `Draw3DChars` broke out before drawing, and `DrawShowing` rendered each
  character once into a cached texture drawn with an additive blend that the transparent queue
  then drew with the restored alpha-cut material, so no crowd reached the screen at all.
  `CrowdTest` (`native/tests/test_native_crowd.cpp`, `DC3_CROWD_STATS`) guards it. Colours need
  the engine change in PLAN H-6 (render-target alpha write, `MatColorMod`, no fringe fade on
  colour-mod band texels); without it the crowd draws with untinted clothes and black skin.
- **`could not find group _00s_cameraperson_ok` (and `_70s_dj_skills_ok`,
  `_70s_arcademachine_skills_ok`)** is the shipped data, not a native gap.
  `HamWardrobe::PlayCrowdAnimation` plays `<stance>_<anim>`, then falls back to `<stance>_ok`
  (`CharDriver::PlayGroup` notifies on each miss). The crowd anim files define only
  `_00s_cameraperson` and `_00s_cameraperson_realtime_idle` for that stance, and `_70s_dj_*` only
  in the tempo files; the original logs the same kind of notify under Xenia
  (`archive/screenshots/xenia-gameplay/12-gameplay-venue-midsong.png`: `crowd_m_70s_02 ... could
  not find clip from group _70s_dj_skills...`).

Venues whose crowd objects are empty in the `.milo` and populated only by DTA scripting were
reported in March as unsupported (unverified now).

## AnimTask lifetime

`AnimTask::Poll` (`src/system/rndobj/Anim.cpp`) ends a task, sending `on_anim_event ended` to its
listener and then `QueueTaskDelete`, when either:

- `mAnimTarget` is null, or
- the task is not looping, not blending, and either past `mFrameSpan` or at `mScale == 0`.

There is no native auto-null of `mAnimTarget`; it was removed 2026-09-30. The target is kept until
the task is deleted, so an AnimTask started from the `ended` callback can chain its blend.
`NativeEngineLeadsTest.AnimTaskEndedKeepsTargetForBlendChaining` (`native/tests/test_native_engineleads.cpp`)
pins this.

## Lip sync and blinks

Classes (`src/system/char/`):

- `CharLipSync` holds viseme weight keyframes at 30 Hz (`Duration() = (mFrames - 1) / 30.0f`).
  `FindLipSyncForSound(Sound*)` looks up `sLipSyncMap`, which `RegisterLipSync`/`UnregisterLipSync`
  maintain.
- `CharLipSyncDriver` is a `CharPollable` that samples the playback at song time and `ScaleAdd`s
  one viseme `CharClip` per active viseme. It also handles blinks, override clips and blend in/out.
- `CharFaceServo` is a `CharBonesMeshes` subclass that owns the face bones, the `Base` and `Blink`
  clips, and procedural blink weights (`SetProceduralBlinkWeight`).
- `CharEyes` handles procedural blinks and gaze in the game (`ForceBlink`, `SetEnableBlinks`).

Data:

- Per-character viseme clip dirs: `char/main/dancer/<name>/gen/viseme.milo_xbox`.
- Shared: `char/shared/gen/viseme_resource.milo_xbox` and `sfx/gen/lipsynchelper.milo_xbox`.
- Where per-song lip-sync data lives was not confirmed; no `.lipsync` files are in `orig-assets/extracted` (unverified).

Viewer:

- `milo-viewer --visemes <path.milo_xbox>` loads a viseme dir (`native/src/viewer/ViewerArgs.cpp`).
- `BlinkState` (`native/src/viewer/ViewerAnimation.{h,cpp}`) is a viewer-only blink timer: the next
  blink is due 2 s plus `rand() % 400 * 0.01` s after the last; a blink lasts 0.15 s with a triangle
  weight. It drives `CharFaceServo::SetProceduralBlinkWeight` and `CharEyes::ForceBlink`.

Whether in-game lip sync is visibly correct on native has not been verified.

## Skeleton conventions (for bone tests)

- Bone names carry a `.mesh` suffix: `bone_pelvis.mesh`, `bone_head.mesh`.
- Character space is centimetre scale and Z-up.
- Pose a clip only at beats inside `clip->StartBeat()`..`clip->EndBeat()`. Clip beats start at the
  clip's export offset, not 0, so beats 0/0.5 sample nothing meaningful.
- `../milo-executable-library/MiloEditor/MiloLib` is a structural/field-order hint, not ground
  truth. Its `Assets/Ham/SkeletonClip.cs` still carries `// TODO: finish this`.
- Tests: `BoneGroundTruth.*`, `ClipPoseFixture.*`, `MainMiloLoadTest.LoadMainCharacterMilo` in
  `native/tests/test_bone_ground_truth.cpp`. They skip when assets are missing. Use
  `MILO_TEST_CHAR` to override the bone dir (default `skeleton_bones_resource`) and
  `MILO_TEST_CLIPS` to override the clip dir. See [../TESTING.md](../TESTING.md).

## Sources

[ANIMATION_PIPELINE.md](../../archive/2026-11-13-native-doc-audit/native/ANIMATION_PIPELINE.md),
[CROWD_ANIMATION.md](../../archive/2026-11-13-native-doc-audit/native/CROWD_ANIMATION.md),
[web-port/CROWD.md](../../archive/2026-11-13-native-doc-audit/plans/web-port/CROWD.md),
[LIP_SYNC.md](../../archive/2026-11-13-native-doc-audit/plans/custom-graphics-engine/LIP_SYNC.md),
[BONE_GROUND_TRUTH_AND_CLIP_VALIDATION_PLAN.md](../../archive/2026-11-13-native-doc-audit/plans/custom-graphics-engine/BONE_GROUND_TRUTH_AND_CLIP_VALIDATION_PLAN.md),
[DECOMP_GAPS.md](../../archive/2026-11-13-native-doc-audit/native/DECOMP_GAPS.md) (AnimTask section — its "keepalive / native auto-null" description is superseded).
