# 2026-07-02 — Feet-in-floor "still": web loop never called the plant (FIXED) + knee-path refutation overturned

Continues [[2026-07-01-feet-faithful-root-confirmed]]. User report: dancer feet in the floor,
fine during the animation intro, sunk the moment the song starts — despite the default-on
post-poll plant and the green gate.

## ROOT CAUSE of the "still" (fixed this session)

**The web/Emscripten build never ran the plant.** `main_web.cpp:233` drives frames through
`App::RunOneFrame()` (App.cpp:776), which polled the world and drew but did not call
`Dc3RunPostPollFootPlant()`. The desktop loop (`App::RunWithoutDebugging`, which contains the
plant) is compiled out on web (`#if defined(HX_NATIVE) && !defined(__EMSCRIPTEN__)`), so web
could not even reach the plant in principle. The gate only ever runs the desktop loop
(milo-tests popen-launches the real dc3-native binary) → gate green, web sunk.

**Fix:** plant call inserted in `App::RunOneFrame` after `TheSynth->Poll()` and before
`TheRnd.BeginDrawing()` — the exact ordering of the desktop loop. PPC bytes untouched
(the whole function is HX_NATIVE-only).

**Verified:**
- Desktop: feet gate GREEN post-rebuild (0/753 below floor both feet); live gameplay
  screenshots (headless ymca run, HTTP `/api/screenshot`) show planted feet; per-frame
  telemetry (13k samples) shows zero below-floor, toe clamped at exactly +0.60 (plant margin).
- Web: rebuilt both bundles (`scripts/web/build.sh --both`), drove to gameplay with
  `scripts/web/gameplay.mjs` / `scripts/web/tmp-feet-burst.mjs`; on-canvas IK diags show
  `IkSnap fingerW.v z=0.60` and NeutralWXfm live toe z=0.60 — the plant clamp signature
  (pre-fix behavior at crouch beats was toe ≈ −3…−4). Visual confirmation blocked by a
  separate web-headless quirk: the venue camera never frames the dancers in the puppeteer
  run (and songMs stalled at ~73s) — worth its own look, unrelated to feet.

## Why "intro fine, song start sunk" (verified, workflow tracer + adversarial verify upheld)

The boundary is one scalar: `TheTaskMgr.Seconds(kRealTime)` crossing 0.
- Intro: `song.anim` pinned at pre-roll (`HamDirector::Enter` SetFrame(-kHugeFloat),
  HamDirector.cpp:382-386); `songAnim->SetFrame` is gated on realTime >= 0 on BOTH platforms
  (native HamDirector.cpp:3227-3233, Xbox OnSelectCamera :2619). PlayAnims reads the frozen
  frame → first/rest standing clip → feet fine.
- At realTime >= 0 the choreography clips unblock (same instant as `intro_over` /
  `kGameInIntro→kGamePlaying`); deep-crouch clips start playing and the known native
  under-bend manifests immediately. Also `HamCharacter::Poll` mutes the intro CharDriver
  (`mDriver->SetWeight(0)`, HamCharacter.cpp:958) — the writer switches to song.hdrv.
- NO dancer/IK re-creation, wardrobe reload, or floor change at the boundary (ruled out;
  outfit/world load completes at wait-state 5). Do NOT pursue a boundary-specific patch.

## Draw-path audit (verified): the plant IS visible to pixels by construction

No cached/parallel bone palette exists on native: skin matrices are computed per draw call
(`DrawMeshImmediate → FillBoneUniforms`, milo-native-engine Mesh_Wgpu.cpp:262 /
BoneSetup.cpp:179-227) by reading the SAME `RndTransformable::WorldXfm` objects the plant
writes, after the plant, every frame. All `PoseMeshes` callers are poll-time; no draw-time
re-posers. So desktop rendered pose == sampled pose.

Real holes found in passing (not the current bug, keep on radar):
1. **Gate blindness:** telemetry samples player 0 only (plant covers 6 dancers); test
   threshold is toe Z > **−2.0** (not 0); samples every 10th frame.
2. **kMaxBones=40 clamp** (BoneSetup.cpp:153, UniformStructs.h:98): dancer body mesh has
   exactly 40 bones; any >40-bone outfit mesh gets identity skin for the excess, silently.
3. **Plant-guard hole:** `Dc3PlantGuarded` skip exists only in PoseMeshes' POS and QUAT loops
   (CharBonesMeshes.cpp:129,158), NOT rotx/roty/rotz — the knee (a ROTZ channel) is
   unprotected if any same-frame PoseMeshes ever runs after the hook (currently none does).

## FAITHFUL ROOT re-framed: the July-1 "refutation" was a probe artifact (verified)

The July-1 session concluded the knee is NOT posed via LayerClip::Play → CharBones::ScaleAdd
(zero-hit probes). **That refutation is overturned with an internal-consistency proof:**
`LayerArray::Eval` (HamDriver.cpp:356-372) computes song.hdrv root weight as the SUM of
consumed child weights, so the SAME run's DriverWeight=1.0 (40/40 samples) mathematically
requires ≥1 LayerClip with weight>0 whose `Play` body runs `bones.ScaleAdd` every frame.
The zero-hit probes were gating artifacts (candidates: DC3_KNEE_CLIP env not exported in the
gate command; stale binary — exact cause unrecoverable, probes were never committed).

**The actual knee pipeline (end-to-end, every hop source-verified + adversarially upheld):**
1. HamDirector::Poll → ClipPlayer::PlayAnims rebuilds song.hdrv layers from song.anim clip
   keys each frame (ClipPlayer.cpp:178-188, PlayClip :209).
2. HamDriver::Poll (HamDriver.cpp:72-133): mLayers.Eval → `mBones->ScaleDown(1-mWeight)` →
   mLayers.Play → LayerClip::Play :334 `bones.ScaleAdd(mClip, mWeight, ...)`.
3. → CharClip::ScaleAdd/ScaleAddSample → CharBonesSamples::ScaleAddSample →
   CharBones::ScaleAdd(CharBones&,float). **The knee is a `.rotz` SCALAR channel, not a quat**
   (ground truth docs/sessions/2026-06-08-feet-reverify-data.md:183-184; thigh/ankle are
   .quat). Knee accumulates in the ROT section (CharBones.cpp:914-971; compressed branch
   `*otherRotItr += *myRotItr * (f2*0.00061035156f)` at :931).
4. CharServoBone::Poll :67 → CharBonesMeshes::PoseMeshes ROTZ loop
   (CharBonesMeshes.cpp:193-195 MakeRotMatrixZ) writes bone_L-knee.mesh DirtyLocalXfm().m.
   Channel→mesh mapping via CharUtlFindBoneTrans (CharUtl.cpp:83-108, suffix-strip then
   .cb → .trans → .mesh).

So the faithful root is **knee `.rotz` float under-accumulation in the song.hdrv layer
blend** (NOT a "knee QUAT under-bend" — that channel doesn't exist).

## NEXT — instrumentation plan (the wrong-stage failure can't repeat now)
- PRIMARY: HamDriver::LayerClip::Play (HamDriver.cpp:330-336) — resolve
  `(float*)bones.FindPtr(Symbol("bone_L-knee.rotz"))` (radians), log per-clip: dancer
  PathName, mClip->Name(), mWeight, delta-rotz, running total. EXPORT the env gate in the
  actual gate command line.
- COMPANION: HamDriver.cpp:130 (post-Play final knee rotz + pelvis Z for depth binning);
  unconditional call counter at CharBones::ScaleAdd entry (dead-probe canary);
  make TestDstComplain (CharBones.cpp:273) print unconditionally on native — a rot-section
  punt silently drops all remaining channels and is a prime under-accumulation suspect.
- Split hypotheses: (a) clip selection/weight (native picks/weights a shallower clip at
  crouch beats) vs (b) accumulation loss (TestDstComplain punt / ScaleDown residual /
  short-decode rounding 20/32768 that scales with bend depth).
- Acceptance unchanged: knee ≈ −54° at pelvis 34–36 with DC3_FEET_POST_PLANT_OFF=1, gate GREEN.
