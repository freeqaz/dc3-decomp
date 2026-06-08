# IK Ground-Truth Comparison: Xenia (Xbox) vs Native — Feet-in-Ground Fix

**Status:** ACTIVE (2026-06-03). Coordinator: main session. Implementation: Opus subagents.

## Goal

Wire up per-frame skeleton/IK telemetry on **both** the Xenia-emulated Xbox binary (ground
truth) and the dc3-decomp native port, capture the same playing song on each, and compare —
numerically (joint world positions) and visually (screenshots) — to pin and **fix the
feet-in-the-floor IK bug** at its source.

## Why this is newly possible (the unblock)

The IK fix has been blocked **for months** on Xbox ground truth: the Xenia path could never reach
an *animating* dancer (rest pose / `GetKeys` hang / never-unpaused). Those blockers were resolved
2026-06-02 (see [[project_xenia_async_stall]] cont.11/cont.12): gameplay now reaches
`gpState=2` (kGamePlaying), the song plays, dancers animate; and the **`dc3_inline_render`** fix
([[xenia-vulkan-rendering]]) gives a clean full-scene capture every frame. So for the first time,
`--dc3_ik_telemetry=true` runs during *actually-animating* gameplay.

## The two competing theories (what we're disambiguating)

Source: `docs/sessions/2026-05-14-feet-in-floor-empty-constraints.md` (latest IK doc).

- **Theory A — silent foot IK / no inputs (favored by latest doc).**
  - `HamIKEffector::mConstraints` is EMPTY (`constraintCount=0`) for every effector on native →
    `ApplyConstraints` returns `totalWeight=0.000` → nothing anchors the feet.
    (`src/system/hamobj/HamIKEffector.{h:93,cpp:115,214-253,339}`)
  - AND `CharIKFoot::Poll` (the *actual* foot-planting IK DC3 uses) **never fires** on native.
    (`src/system/char/CharIKFoot.cpp:76`; held by `HamRegulate::mLeftFoot/mRightFoot`,
    found via `left.ikfoot`/`right.ikfoot`.)
- **Theory B — pelvis-last dirty cascade.**
  - Pelvis IK effector polls LAST in `CharPollableSorter`; `pelvis->SetWorldXfm` dirties
    thigh→shin→ankle; the renderer then recomputes the ankle from **stale `mLocalXfm`**, dropping
    the IK correction. (`Trans.cpp:408-415` SetWorldXfm cascade; `Trans.cpp:655-676`
    WorldXfm_Force recompute; `HamIKEffector::PollDeps` `HamIKEffector.cpp:141-170` — note NO
    pelvis branch for the leg chain.) Cascade-fix commits (`f1b65b4b`) did NOT change the symptom,
    and with empty constraints there's no correction to lose → B is the weaker theory; survives
    only as the `DC3_FOOT_OFFSET` render hack in `native/src/platform/BoneSetup.cpp:221-242`.

## Telemetry mechanisms (both sides already instrumented)

### Xenia (Xbox ground truth) — `--dc3_ik_telemetry=true`
- Impl is **uncommitted WIP** in the xenia tree (`headless-vulkan-linux`); compiled into the
  Checked binary `build/bin/Linux/Checked/xenia-headless`.
  `ApplyDc3IKInstrumentation` (`src/xenia/dc3_hack_pack.cc:5613`) byte-patches `HamIKEffector`
  funcs + registers a reader on `HolmesClientPoll` (0x82631C58).
- Instruments **HamIKEffector** (ankle/pelvis/hand effectors): `Poll` (captures `this`),
  `ApplyConstraints` (f1=totalWeight), `GetGroundHeight`, `GetType`, `ApplyPosConstraints`,
  `IKElbow`, `DoFancyElbow`. Reads effector world pos (mWorldXfm @ +0x6C/+0x70/+0x74), `mDirty`
  (+0xBD), constraint count (ObjVector @ +0xBC). Does **NOT** instrument `CharIKFoot`.
- **Output:** `DC3:IK [frame N] type=<pelvis|ankle|hand|..> totalWeight=.. groundHeight=.. posWeight=..
  ikElbowZ=.. fancyWeight=.. this=.. effector=.. ground=.. more=.. constraints=N
  effWorldPos=(x,y,z) effDirty=N` to the **main Xenia log** (`--log_file`), every 60 frames.
- Limitation: logs once per 60 frames; the per-Poll `this` slot is overwritten so we see the
  LAST effector that polled, not a full per-frame poll ORDER. Enhance if needed (see Phase E).

### Native port — `DC3_TEL=1`
- `native/src/telemetry/GameplayTelemetry.cpp` `CaptureSnapshot()` reads world-space
  `bone_L/R-ankle.mesh`, `bone_L/R-toe.mesh`, `bone_pelvis.mesh`, `bone_L/R-hand.mesh` via
  `WorldXfm()`; captures dirty flags, local xfm, NaN checks, deltas.
- **Output (stderr):** `DC3_TEL: frame=.. lAnkleZ=.. lToeZ=.. rAnkleZ=.. rToeZ=.. lAnkleX/Y ..
  lAnkleLocalX/Y/Z .. lHandX/Y/Z .. lAnkleDirty rAnkleDirty pelvisDirty`. Interval via
  `DC3_TEL_INTERVAL` (set =1 for dense). One-shot `DC3_IK_DIAG` dumps: `CharIKFootPoll`
  (`CharIKFoot.cpp:82` — KEY: does it fire?), `IkSnap`/`PollOrder`/`TypePropsDump`
  (`HamIKEffector.cpp`), `DirtyChain`, `FootGeom`, `RestGeom`.
- Parser: `native/tests/telemetry_parser.{h,cpp}`.
- **Screenshots:** `/api/screenshot` (PNG bytes) or deterministic `MILO_SCREENSHOT_DIR` +
  `MILO_SCREENSHOT_FRAMES=...`; wrapper `scripts/gpu/screenshot.sh`. `DC3_FAST_TIME=1` for
  reproducible song-time. Same song via `/api/dta/eval` on `meta_performer`
  (`set_venue_pref rollerrink` / `set_song thehustle` / `setup_venue`) or input flow `ymca.txt`.

## Decisive measurements (disambiguate A vs B)

1. **Bone poll order** (pelvis vs ankle) on Xbox vs native — most decisive.
2. **`CharIKFoot::Poll` activity** on Xbox — does `left/right.ikfoot` exist + poll? (native: silent)
3. **`mConstraints.size()`** on Xbox for ankle/pelvis (native: 0).
4. **Per-frame WORLD Z of pelvis/ankle/toe** Xbox vs native (native crouch: pelvis 33.62, ankle
   0.84, toe -3.12; toe should sit ≈ floor on Xbox).
5. **`totalWeight`** from `ApplyConstraints` on Xbox (native: 0.000).

Decision rule:
- Poll order identical AND ankle wrong immediately after its own poll ⇒ **Theory A** (fix inputs:
  populate `mConstraints` / wire `CharIKFoot::Poll`).
- Ankle correct after its poll but corrupted after pelvis polls (+ platform-divergent order) ⇒
  **Theory B** (fix sorter/cascade; remove `DC3_FOOT_OFFSET` hack).

## Plan / phases

- **Phase 1 (#25, in_progress):** Xenia capture — gameplay + inline render + IK telemetry.
  Validate real `DC3:IK` records flow during animation. Output `/tmp/xenia-ik-capture/`.
- **Phase 2 (#30):** Native capture — `DC3_TEL=1 DC3_TEL_INTERVAL=1` + same song + screenshots.
- **Phase 3 (#31):** Comparison harness — parse both, build the disambiguation table.
- **Phase 4 (#32):** Screenshot diff — visual feet-in-ground confirmation.
- **Phase 5 (#33):** Root-cause + fix at source; remove `DC3_FOOT_OFFSET`; validate vs ground truth.
- **Phase E (NEEDED — in progress, task #34):** Fix Xenia telemetry so it emits data. See
  root-cause below.

### Phase E root cause + fix (2026-06-03)
**Why 0 records:** (1) `RegisterGuestFunctionOverride` can't intercept `HolmesClientPoll`
(0x82631C58) — it's a non-virtual `__cdecl` free function reached by a direct guest `bl`; Xenia's
`kExtern` interception only catches the static direct-bl JIT path / kernel `Call()`, and the
compiled body is written into the indirection table so indirect callers bypass the handler. The
handler body (`dc3_hack_pack.cc:5764-5902`) **never executes**. (2) `HolmesClientPoll` is dormant
headless (`HolmesClient.cpp:785-793` early-returns `if(!gHolmesStream)`, caller gates
`if(gUsingCD==0)`). (3) `ApplyConstraints`/`ApplyPosConstraints` end in recursive tail-calls
(compiled to `b`, no `blr`) → "Patched 0 blr sites" → totalWeight/posWeight slots never captured.

**KEY:** the byte-patch code-CAVES *do* fire (guest-memory patches, independent of the broken
override) → the telemetry slots ARE populated; we just never READ them. **Fix (task #34):** read
the slots + do the effector-`this` walk from `Dc3NuiSequencerExtern` (emulator.cc ~339-451), the
host hook that demonstrably fires every NUI frame this session (nui=5400+), reusing its
`is_guest_readable` lambda (~492) + `s_skel_calls` counter, frame-gated ~every 30 frames. PLUS a
best-effort direct bone-subtree walk (`bone_L/R-ankle.mesh`, `bone_pelvis.mesh`, toe bones →
RndTransformable mWorldXfm +0x6C/+0x70/+0x74, mDirty +0xBD) for ground-truth ankle/toe/pelvis Z
that is robust even if the effectors are dormant on Xbox (a real risk — native `CharIKFoot::Poll`
never fires). Then re-run the Phase-1 capture command for the numeric ground truth.

## Run command (Xenia, validated config)

```
cd /home/free/code/milohax/xenia
timeout --signal=KILL 200 build/bin/Linux/Checked/xenia-headless \
  --target=/home/free/code/milohax/dc3-decomp/orig-assets/debug.xex \
  --gpu=vulkan --vulkan_device=1 \
  --dc3_nui_patch_layout=original --dc3_crt_skip_nui=true --fake_kinect_data=true \
  --dc3_ik_telemetry=true --dc3_inline_render=true \
  --scripted_input_file=/home/free/code/milohax/dc3-decomp/scripts/dc3-input-flows/xenia-ymca.txt \
  --headless_timeout_ms=185000 --dump_frames_path=/tmp/xenia-ik-capture/frames \
  --headless_capture_interval=200 --log_file=/tmp/xenia-ik-capture/run.log --flush_log=true
```
GPU runs are serialized + coordinator-supervised; need `dangerouslyDisableSandbox`. After every
run: `pkill -9 -x xenia-headless` (NEVER `pkill -f xenia`); delete cores >100MB. Inline render
needs warm `/tmp/claude/xenia_vulkan_pipeline_cache.bin`.

## Results log

### Phase 1 (Xenia capture) — 2026-06-03 — PARTIAL SUCCESS
Run `/tmp/xenia-ik-capture/` (exit 0, clean). Config = validated command above.
- ✅ **Visual ground truth SECURED.** 88 frames, ALL `verdict=SCENE` (inline render held the
  entire run, frame 200→8800). Spans a full song: `gpState` 2 (playing) → 3 (over), nui 2640→5400.
  `/tmp/xenia-ik-capture/sel_6000.png` shows BOTH dancers on the rollerrink floor with **feet
  cleanly planted on the floor — no penetration** (this is the correct behavior native must match).
- ❌ **Numeric IK telemetry FAILED: 0 `DC3:IK [frame]` records.** The HolmesClientPoll reader
  registered (`DC3:IK Registered IK telemetry reader on HolmesClientPoll (82631C58)`) but never
  fired — not even the every-600-frame "NO DATA" diagnostic. Also `Patched 0 blr sites in
  ApplyConstraints (824BF5D8)` and `... ApplyPosConstraints (824BF430)` (blr scan found nothing →
  totalWeight/posWeight never captured even if the reader fired). Root-cause + fix under
  investigation (leading hypothesis: HolmesClientPoll is a non-virtual direct-`bl` target so
  `RegisterGuestFunctionOverride` doesn't intercept it — cf. cont.10; OR HolmesClient is dormant
  headless. Likely fix = piggyback the IK slot-read on the existing per-NUI-frame host hook in
  emulator.cc and walk the guest Character bone subtree directly).
- Implication: the screenshot comparison (Phase 4) is fully unblocked NOW; the numeric comparison
  (Phase 3) needs the Xenia telemetry fix (Phase E) before a re-run.

### Phase 2 (native capture) — 2026-06-03 — COMPLETE
Run `/tmp/native-ik-capture/` (7149 frames `state=playing`, YMCA, dancer animating).
- **Foot/pelvis Z (gameplay, n=7149):** lAnkleZ mean 1.02, rAnkleZ 0.92; **lToeZ mean −3.56,
  rToeZ −3.42** (toes below floor in 99% of frames); pelvis drops 42.5(rest)→35.1(gameplay).
  Rest pose CORRECT (toe ≈0 on floor). Transforms HEALTHY (no NaN, det≈1, no inversion) →
  systematic downward offset, not corruption.
- **Theory A CONFIRMED on native:** ankle HamIKEffector `typeProps=nil` → `constraintCount=0`,
  `totalWeight=0.000` (IkSnap/TypePropsDump, `HamIKEffector.cpp:355,376`). No ground constraint.
- **Theory B precondition holds:** poll order ankle(L)→ankle(R)→hand(L)→hand(R)→**pelvis (last)**
  (`HamIKEffector.cpp:311`, 30 consistent samples); ankle/knee perpetually dirty, pelvisDirty=0 at
  sample. But zero constraints ⇒ no correction for the cascade to lose ⇒ **B is downstream of A**.
- **Prior doc claim REFUTED:** `CharIKFoot::Poll` DOES fire (5×, all ptrs non-null,
  `CharIKFoot.cpp:76-99`). NOTE: 5× over 7149 frames is suspiciously sparse — needs scrutiny
  (is the DC3_IK_DIAG one-shot/rate-limited, or does CharIKFoot genuinely poll only 5×?).
- **Native headless renders BLACK** (`Mesh_Wgpu: skipping '' — no vertices`) → native screenshots
  NOT usable for Phase 4 visual diff from headless; would need windowed/web build. Numeric
  telemetry (read off the scene graph, render-independent) is the reliable comparison axis.

### Native HEAD is BRICKED — 4 regressions (workflow `fix-native-regressions` in progress, #35)
HEAD does not build/boot; the captured binary used transient fixes (since reverted):
1. `src/system/obj/DataArray.cpp:586` — kDataInclude loop reads `array` (null for include) not
   `macro` (line 582 sizes for `macro`) → **SIGSEGV ~0x10 in DataArray::Load at boot** (og-port
   85a21090). MATCH-SENSITIVE.
2. `src/system/os/PlatformMgr.cpp:231` — `OnSignInUsers(DataArray*)` def vs `const DataArray*`
   decl (header changed 306321c9) → compile error.
3. `BinkMovieImpl::sActivePending` — class static declared, no out-of-line def (og-port ca11b7de)
   → link error `_ZN13BinkMovieImpl14sActivePendingE`.
4. `lbl_82F0E8A4` — `HollaBackMinigame.cpp:33` externs it (04d7066d) but it's missing from
   `native/src/engine_stubs_generated.cpp` (value 4) → link error. Native-only stub.

### Phase 1b (Xenia re-capture w/ fixed telemetry) — 2026-06-03 — TELEMETRY FIRES; KEY FINDING
Run `/tmp/xenia-ik-capture2/` (exit 0). The fix WORKS: **144 `DC3:IK [frame]` records** (was 0).
- **★ KEY FINDING: Xbox ankle effector also has `constraints=0`** (1 ankle record, frame 2850,
  this=409A2A30, ground=409A35B0 resolved, **constraints=0**). SAME as native ⇒ **empty
  `mConstraints` is NORMAL on both platforms, NOT the differentiator. "Populate the constraints"
  is the WRONG fix.** (n=1; iteration #2 corroborates across ankle+pelvis.)
- **Gaps (→ iteration #2, in progress):** (1) single-slot design caught 143 hand + only 1 ankle +
  0 pelvis (overwrite + 30-frame read lag); (2) `mEffector @ +0x44` returned null on the ankle
  record (mGround @ +0x6C DID resolve, so ObjPtr mechanism is fine — either +0x44 wrong or the
  captured `this` is stale by read-time) ⇒ effWorldPos unresolved; (3) bone walk found player0's
  char dir (4095A078, 727 hash slots) but **0 named bones** (entry layout / subdir / name-format).
- **Iteration #2 plan (subagent, code-only):** make the char-dir walk the master source — dump the
  dir entries to learn the real name format, resolve the RndMesh→RndTransformable→mWorldXfm offset
  (via `lookup_struct_offset`), then read ankle/toe/pelvis WORLD Z (`.mesh`) + per-effector
  constraints/type (`.ikf`) directly & live. Then re-capture for the decisive geometry comparison.
- **Reframed decisive question:** since constraints=0 on both, is Xbox's **toe Z ≈ 0 (planted)**
  while native's is **≈ −3.5 (sunk)** with the same zero constraints? If so the divergence is in
  bone GEOMETRY/rest-offsets or CharIKFoot behavior (native fires 5× — sparse) — NOT constraint data.

### Phase 1c (Xenia iteration #2 — subdir walk) — 2026-06-03 — COMPARISON COMPLETE
Run `/tmp/xenia-ik-capture3/` (exit 0; 745 EFF + 894 BONE + 484 DIR records). Subdir walk works
(bones/`.ikf` live in the character dir; RndMesh→RndTransformable vbase recovered via RTTI COL).

**THE COMPARISON (Xbox ground truth vs native):**
| Signal | Xbox (149 samples × 5 eff) | Native | |
|---|---|---|---|
| effector constraints (L/R-ankle, L/R-hand, pelvis) | **0** (ALL five) | **0** | ✅ same |
| ankle effector + ankle/toe/pelvis bone `dirty` | **0 (clean)** | ankle/knee **1 (perpetual)** | ❌ DIVERGE |
| pelvis dirty | 0 | 0 | same |
| poll order | inferred pelvis-before-ankle (from dirty=0) | pelvis **LAST** (confirmed) | ❌ likely diverge |
| feet | **planted** (visual, sel_6000.png) | toe **−3.56 below floor** | ❌ THE BUG |

**CONCLUSION:** Empty `mConstraints` is NORMAL on both platforms — NOT the bug (the
populate-constraints fix is REFUTED). The bug correlates 1:1 with the **`dirty` divergence**:
native leaves the ankle perpetually `dirty=1` ⇒ the renderer recomputes the ankle world xfm from
stale `mLocalXfm` ⇒ the planting correction (from animation and/or CharIKFoot's local-xfm write) is
discarded ⇒ foot sinks. Xbox keeps the ankle `dirty=0` ⇒ correction survives ⇒ foot planted. This
IS the dirty-cascade (Theory B), driven by POLL ORDER: Xbox polls pelvis BEFORE the ankles (cascade
dirties the ankle, then the ankle effector re-polls/cleans it → ends clean); native polls pelvis
LAST (ankle cleaned, then pelvis cascade re-dirties it → ends dirty). Since `CharPollableSorter` is
shared decomp code, the likely root is an **MSVC-vs-libstdc++ `std::sort` tie-break divergence** in
the topological sort, NOT a source bug — so the fix is probably an HX_NATIVE deterministic/stable
sort (or an explicit pelvis→leg-chain PollDeps edge) to match Xbox's order. (Note: Xbox bone
world-Z read is still offset-bugged — all bones read (0,0,~5) — but the visual + dirty/constraint
signals already decide it; NOT chasing the world-Z offset further.)

### Native HEAD regressions — FIXED (workflow `fix-native-regressions` complete, #35)
All 4 fixed + validated (UNCOMMITTED working-tree changes; no commit per project rule):
1. `DataArray.cpp:586` `array`→`macro` — un-bricks boot AND **improves DataArray::Load match
   95.0%→97.9%** (removed a 99-instr r29↔r30 reg-swap cascade; matches RB3 ground truth). No guard.
2. `PlatformMgr.cpp:231` `const DataArray*` — HX_NATIVE stub only; matched `PlatformMgr_Xbox` stays 100%.
3. `BinkMovieImpl::sActivePending` out-of-line def — matches symbols.txt .data layout. No guard.
4. `lbl_82F0E8A4 = 4` in `engine_stubs_generated.cpp` — native-only stub. No match impact.
Result: **native builds + links + boots to main_screen** (200 frames, exit 0, config DTBs parse
through the previously-crashing path); milo-tests 370/371 (the 1 fail = pre-existing ObjectDir
cascade bug). **The native port is un-bricked → the IK fix can now be implemented & validated.**

### Phase 5 — ROOT-CAUSE REVISED: dirty/poll-order theory REFUTED (2026-06-03)
An adversarial verification subagent refuted BOTH my mid-session conclusion AND the old empty-
constraints theory. **The dirty=0-vs-1 delta was a sampling-point artifact, not an engine
divergence:**
- Poll order is DETERMINISTIC + IDENTICAL on both: `AlphaSort` (`CharPollable.h:22-26`) sorts by
  `strcmp` of UNIQUE `.ikf` names → strict total order, NO ties → `std::sort` is implementation-
  independent; pelvis sorts LAST on both. No tie-break. (So a "reorder pelvis first" fix is WRONG —
  it would break the 100% `Sort` match.)
- Cascade mechanism identical Xbox (`Mesh.cpp:743`) vs native (`BoneSetup.cpp:179`): both read
  `WorldXfm()` at draw, both `SetWorldXfm` w/o local writeback.
- Native `dirty=1` sampled PRE-draw (`App.cpp:1093` before Draw `:1105`); Xenia `dirty=0` sampled
  post-resolve → apples-to-oranges.
- `CharIKFoot::Poll` runs EVERY frame (the "5×" was a log rate-limit `sCharIKFootPollLog<5`).

**REAL cause = IK VALUE divergence.** Native ankle Z = 4.39 (rest, toe planted 0.01) → ~1.0
(gameplay); ankle→toe local offset ~constant ~4.4, so the ankle DROPPING in gameplay puts the toe at
−3.56 (below floor). Xbox foot stays planted ⇒ the ankle is held up on Xbox but not native.
**Key insight:** the IK source matches Xbox (Poll 99.9% = lowering diff, logic correct), so the
divergence is likely an **LP64/64-bit/struct-layout issue or animation-eval difference** in the
shared foot-IK chain — NOT the matched IK logic, NOT dirty flags. (`DoFancyElbow`/`ComputeHandPullAndQuat`
are HAND IK → irrelevant to feet.)

**Phase 5 plan:** (a) fix native telemetry to sample post-draw (kills the dirty red-herring,
zero-risk) — note: the native toe Z=−3.56 is ALREADY the post-recompute rendered value (telemetry
forces WorldXfm), so it's trustworthy; (b) pin WHY the native ankle drops in gameplay — trace
CharIKFoot::DoFSM (does it hold the foot up?) + the animation eval + any LP64/layout bug in the foot
chain; (c) fix at root, validate toe Z→~0, remove `DC3_FOOT_OFFSET`. Investigation+experiment agent
launched (native now builds, so it can instrument/run/test directly).

### Phase 5 RESULT (deep dive) — 2026-06-03 — bug localized to the ankle ROTATION channel
**The foot is ANIMATION-driven, not IK-driven** (key reframe): `CharIKFoot::DoFSM` only smooths X/Y
skating (`tf.v.z = mFinger->WorldXfm().v.z`, `CharIKFoot.cpp:109` — never raises Z; matches RB3); the
HamIKEffector ankle target follows the animated toe (ankle clamp pass-through). Measured rest vs
gameplay (DC3_FOOT_OFFSET=0):
| | pelvis WZ | knee WZ | ankle WZ | toe WZ | ankle local-rot |
|---|---|---|---|---|---|
| REST (planted) | 42.5 | 22.3 | 4.39 | **0.01** | flexed |
| GAMEPLAY (sunk) | 34.8 | 17.6 | ~0.0 | **−3.9** | **≈identity (~10°, should be ~90°)** |
**The ankle's LOCAL rotation is under-flexed** → foot stays in-line with the down-pointing shin → toe
sinks ~3.9. Knee bends fine (~40°). **Chain first breaks at bone_L-ankle local rotation.** Prime
suspect = `CharBonesSamples::EvaluateChannel` (`CharBonesSamples.cpp`, 79.2%, compressed short→float
rotation decompression — lowest match in the anim chain CharDriver::Poll 93.9% → CharBones::ScaleAdd →
EvaluateChannel 79.2% → CharBonesMeshes::PoseMeshes). Confidence MEDIUM.
- **Landed (kept):** `HamIKSkeleton::Poll` non-recursive→recursive `Find("bone_pelvis.mesh")` (pelvis
  is in a `skeleton` subdir; lookup failed → null neutral skel dir). HX_NATIVE, 100% match. Real fix,
  insufficient (pelvis-IK adjusts only 1–5%).
- **Keep `DC3_FOOT_OFFSET=3.5`** until fixed (≈ the 3.9 drop = confirmed band-aid).
- **Next:** prove/fix EvaluateChannel (RB3 source-compare + asm structural diff + LP64 check +
  instrument the decompressed ankle quaternion); validate lToeZ→~0. Agent launched.

### Phase 5b RESULT — decompression REFUTED; bug pinned to the IK neutral-anchor (2026-06-03)
Instrumented every link + hand-verified decompression bit-for-bit. **The animation/decompression
chain is CORRECT** — `EvaluateChannel`/`ScaleAdd`/`ShortQuat::ToQuat`/`PoseMeshes` all compute the
ankle pose exactly as Xbox would (raw shorts `(-1873,-606,10229,31067)`×scale → native's exact quat;
`run_diff_inspect` `diff_op: none` → the 84.1% is pure register/scheduling, zero logic diff). The ~10°
ankle rotation is CORRECT clip data (prior "should be ~90°" was a wrong inference). **`PoseMeshes`
plants the toe correctly (z≈+0.1).**
**THE BUG:** the **IK ankle-planting clamp then sinks it.** `HamIKEffector::Poll` ankle clamp
(`HamIKEffector.cpp:438-451`) = `clampFactor=(neutralZ−groundH−5)·0.0909; Interp(neutral,eff,clampFactor)`.
Native gameplay measured (`bone_L-ankle.ikf`): `neutralZ=−4.02, effZ=−4.02, groundH=0.11,
clampFactor=0.000`. **`neutral` has COLLAPSED onto the live sunk pose** (neutralZ==effZ) ⇒ Interp
returns the sunk position regardless ⇒ toe → −3.9. For planting, `neutral` must be the un-dropped
(~+4) ankle. Root: `HamIKSkeleton`'s neutral skeleton tracks the live crouch-dropped pelvis (its neutral
leg hangs straight down from the dropped pelvis → neutral ankle sinks). `HamIKSkeleton::NeutralWorldXfm`
is **100%-matched** (not a source error there) — so the fix is subtle (how the neutral skeleton is
*posed* in `HamIKSkeleton::Poll`, or an LP64 issue) and touches matched IK code. NO fix shipped (would
be a guess on 100%-matched code without Xbox ground truth). `DC3_FOOT_OFFSET=3.5` stays as the band-aid.
**Remaining need:** Xbox ground truth on the ankle clamp's `neutralZ`/`effZ` (or the neutral-skeleton
ankle Z) — if Xbox shows `neutralZ≈+4` (planted) the fix is confirmed (make native's neutral skeleton
ankle stay planted, not follow the dropped pelvis). Needs a targeted Xenia telemetry extension to log
the clamp internals (the bone-world-Z read is still offset-bugged).

### Phase 5c — band-aid STRIPPED + unmasked baseline + clamp internals (2026-06-03)
Removed the `DC3_FOOT_OFFSET` render hack in BOTH copies (`native/src/platform/BoneSetup.cpp` +
`milo-native-engine/src/platform/BoneSetup.cpp`) so the true foot position is visible. Audited the
rest of the foot/IK path: the only behavioral band-aid was DC3_FOOT_OFFSET; every other foot/IK
`#ifdef HX_NATIVE` block (CharIKFoot::Poll, CharBonesMeshes::PoseMeshes, HamIKEffector::Poll@301/340,
HamCharacter::GetNeutralSkeleton hoisted-cast) is diagnostic logging or a load-bearing UB/null guard
(KEPT). The BoneSetup ~1e5 garbage-NaN identity clamp is a safety net (KEPT).

**Unmasked baseline (DC3_FOOT_OFFSET gone, YMCA, n=9037 playing frames):** lToeZ mean −3.31
(min −4.30), rToeZ mean −3.05, lAnkleZ mean 1.28, rAnkleZ mean 1.34. Matches the +3.5 hack default ≈
the −3.3 sink. Idle/rest (beat<0) is PLANTED: lAnkleZ 4.3, lToeZ 0.4.

**New instrumented findings (all HX_NATIVE diag, 100% matches preserved):**
- `GetNeutralSkeleton` is HEALTHY: returns a SEPARATE posed neutral dir (not self), neutral
  REST ankle world Z = **+4.06..+4.12 (planted)**, neutral REST pelvis Z = 40.36. `mSkeletonBones`
  non-null. So the neutral skeleton data is good.
- `NeutralWorldXfm` non-recursive `Find` does **NOT** miss (nonRecFind==recFind for every bone —
  refutes the "subdir miss" theory). The neutral bone's REST world Z is planted (toe +2.23) — but
  `SetBone` then copies the LIVE local rotations onto it AND `HamIKSkeleton::Poll` set the neutral
  pelvis = LIVE (dropped) pelvis, so the returned neutral collapses onto the live sunk pose. **This is
  faithful matched behavior** (Poll/NeutralWorldXfm/SetBone all 100%); it happens identically on Xbox.
- AnkleClamp internals (instrumented at `HamIKEffector.cpp:440`): clampFactor is effectively always 0
  → Interp returns `neutral`. The clamp is a *lift detector* (neutralZ>groundH+5 ⇒ blend to IK eff).
  The clamp PLANTS the ankle POSITION (lAnkleZ≈0..1, near floor); the IK eff target effW.z=−0.13 is
  correct. **The TOE sinks ~4 BELOW the (roughly planted) ankle** because the foot's WORLD rotation
  points down — the leg is over-extended (shin points straight down) since the pelvis dropped
  ~42.5→~35 and `lKneeLocalX` stays constant (knee does NOT bend more to keep the foot at floor).

**ROOT CAUSE (confidence HIGH on locus, MEDIUM on which input): the foot sinks because the LIVE
leg/pelvis pose itself is sunk** — pelvis drops ~7 units in gameplay and the leg/foot follow it down
without re-planting. The IK clamp correctly chooses the animation pose (matched intent); the animation
pose is what's wrong. Every function in the chain (anim decompress → PoseMeshes → HamIKEffector::Poll
99.9% → HamIKSkeleton 100% → CharIKFoot 100%) is matched/behaviorally-faithful to Xbox. **So the
divergence is in an INPUT (pelvis-drop magnitude and/or per-frame animated joint values), not in any of
the matched IK/skinning functions.** A native-only fix in the clamp/neutral-anchor would just be a
re-skinned band-aid that diverges from Xbox — NOT shipped.

**Still BLOCKED on the same thing:** Xbox ground-truth on the LIVE pelvis/ankle/toe WORLD Z during
animation (does Xbox's pelvis drop the same ~7 units? is Xbox's toe at ~0 with the same pelvis drop?).
The Xenia bone-world-Z read is still offset-bugged (all bones read (0,0,~5)); fixing that read is the
gating prerequisite. DC3_FOOT_OFFSET removal is LEFT IN PLACE (no fix shipped that justifies a hack);
to restore the cosmetic foot for demos, re-add the +3.5 z-offset or set `DC3_FOOT_OFFSET` — but the
hack is gone from source by request so the true behavior stays visible. Instrumentation kept:
`DC3_IK_DIAG GetNeutralSkel[Entry]` (HamCharacter), `NeutralWXfm` (HamIKSkeleton), `AnkleClamp`
(HamIKEffector) — ready for the Xbox comparison.

### Phase 5c — Xbox clamp ground truth: FIX DIRECTION CONFIRMED (2026-06-03)
Caved the Xbox `HamIKEffector::Poll` ankle clamp at the exact `Interp` instruction (`0x824C24E4`;
`neutralQ.v.z`@`0xC8(r1)`, `effQ.v.z`@`0x78(r1)`, clampFactor in `f29`). 144 gameplay samples (`DC3:IK
CLAMP`):
| | neutralZ | effZ | clampFactor | groundH |
|---|---|---|---|---|
| Xbox median | **0.017 (planted, floor)** | **0.882 (live, above floor)** | 0.000 | 0.0 |
- `neutralZ < effZ` in **144/144**; collapsed (`|Δ|<0.1`) in **0/144**. clampFactor≈0 ⇒
  `Interp(neutral,eff,0)` returns the FIRST arg = `neutral` (`Vec.h:303`) ⇒ foot SNAPS to the planted
  neutral. THAT is the planting mechanism.
- Native: `neutralZ==effZ==−4.02` (collapsed) ⇒ Interp no-op ⇒ sinks.
**CONFIRMED FIX TARGET:** make native's clamp `neutralZ` ≈ **0 (floor)**, distinct-and-lower than
`effZ` — i.e. the neutral skeleton's toe-target (`spot_L-toe.trans`) must stay planted at the floor,
NOT collapse onto the live crouch-dropped pose. Since clampFactor≈0 returns `neutral` regardless of
`eff`, fixing the neutral alone plants the foot even though native's live pose is also sunk. (Refines
the earlier "~+4 ankle bone" framing: the clamp operates on the floor-level toe-target ~0, not +4.)
Cave landed in xenia `dc3_hack_pack.cc` (BuildAnkleClampCave, logs `DC3:IK CLAMP`).

---

## Back-transform EXPLOSION root-caused (2026-06-04) — bad INPUT (corrupted neutral-skeleton FRAME), NOT a decomp gap

Follow-up to the clip-map/PreEvalClipWeights fix. The post-fix foot was still sunk because the IK
output never survives: the ankle world Z the IK writes EXPLODES (~60–348) in the finger→effector
back-transform, so the render-time `WorldXfm_Force` recompute discards it.

### Where it explodes (file:line)
`HamIKEffector::Poll` (`src/system/hamobj/HamIKEffector.cpp`), the post-clamp blend + back-transform:
- L526 `Interp(neutralQ.v, effQ.v, clampFactor, q.v)` writes `q.v = neutral` (clampFactor==0 always,
  since `(neutralZ-groundH-5)*0.0909 < 0`).
- L537-539 `q.v += remaining*effQ.v` with `remaining = 1.0 - totalWeight`; totalWeight==0 (empty
  constraints) ⇒ `q.v = neutral + eff`.
- L553-561 `Multiply(inv, finalXfm, finalXfm)` (`inv = effW ∘ fingerW⁻¹`) maps the bad `q.v` →
  ankle world Z ~60.8 (logged) / ~272-348 in feedback-amplified frames.
Captured live via new HX_NATIVE `DC3_IK_DIAG BackXform`/`PARENTCHAIN`/`NEUTRALCHAIN` probes:
`q.v=(111.4,-50.1,0.0) = neutral(54.5,-24.9,0) + eff(55.3,-20.4,0.4)` → `finalAfter.v=(173.6,-82.1,60.8)`.

### Classification: bad INPUT — the neutral-skeleton FRAME is venue-contaminated/corrupted
`HamIKEffector::Poll` is byte-exact matched (99.9% norm; all 20 diffs = stack-offset shifts +2
commutative fmuls — backend floor, ZERO logic/diff_op/insert/delete). So `q.v = neutral + eff` IS
the Xbox math. It only works when **`neutral` is a SMALL, origin-rooted correction** (then
`neutral_small + eff ≈ eff`, no doubling). PROOF (all native, same matched code):
- **iconman** (menu char, root at WORLD origin, foot CORRECT): neutral pelvis `L=(0,0.71,40.36)`
  `W=(0,0.71,42.51)` — world≈local, clean. ankle eff `W=(-3.8,-2.8,4.4)` near origin → no explosion.
- **player0/player1** (venue dancers, placed at world X=±37, foot EXPLODES): neutral pelvis
  `L=(0,-0.50,90.64)` **`W=(25.88,-24.68,39.44)`** — **world X=25.9 despite local X=0 under an
  identity-rooted `skeleton` parent (W=0,0,0)**, AND local Z=90.64 vs the correct 40.36. The neutral
  skeleton's pelvis local transform is CORRUPTED and its cached WorldXfm carries the venue placement.
- Xenia ground truth (xenia-ik-capture/run.log, reliable clamp-cave floats): `neutralZ=0.017`,
  `effZ=0.882` — both small ⇒ Xbox neutral IS the small origin-rooted correction. Matches iconman,
  not the venue dancers. (The Xbox BONE/EFF dir-walk world reads `(0,0,5)`/`(0.1,0,0)` are the
  known offset-bugged telemetry — do NOT use for X/Y.)

Corruption mechanism (high confidence): `HamIKSkeleton::SetBone` (`HamIKSkeleton.cpp:133`) copies the
LIVE pelvis local ROTATION matrix onto the neutral pelvis (`t2->SetLocalRot(t1->LocalXfm().m)`) then
force-recomputes `t2->WorldXfm()`. On venue dancers the live frame is already venue-placed/scaled (and
in later frames already exploded — logged liveWorldV like (75,-391,52), (-231,-134,-195)), so the copy
corrupts the neutral pelvis local (Z 40→90) and writes a venue-X world that sticks (mDirty cleared).
Multi-frame feedback: exploded ankle → exploded live finger next frame → fed back through SetBone →
worse. This is why neutral.X tracks eff.X (~54) instead of staying ~0.

### The render discard (task 3) — reconciled
`Trans.cpp`: `SetWorldXfm` clears mDirty + cascades `SetDirty` to children but does NOT update
mLocalXfm; any later `SetDirty` on the ankle (pelvis effector polls LAST, cascades thigh→knee→ankle)
makes `WorldXfm()` call `WorldXfm_Force` = `Multiply(mLocalXfm, parentWorld)` — recompute from stale
local, discarding the IK write. This cascade is matched Xbox behavior; it discards on native ONLY
because the IK wrote an EXPLODED value the renderer then overrides with the (merely sunk) anim pose.
Fix the input frame and the same cascade preserves a sane planted ankle (as it does for iconman).

### Fix direction (NOT a localized match-safe edit — deferred; needs the frame fixed at root)
The matched IK requires the neutral skeleton to be a clean, origin-rooted, per-frame-rebuilt small
correction (like iconman). On the venue dancers the neutral frame is corrupted by SetBone copying a
venue-placed/exploded live frame + multi-frame feedback. Candidate root fixes (all HX_NATIVE, need
validation; do NOT touch matched HamIKEffector::Poll / SetBone / NeutralWorldXfm logic):
1. Run the per-character IK in CHARACTER-LOCAL space (root at origin during IK; apply the venue
   placement at render) — matches iconman + Xbox; biggest architectural change.
2. Re-derive the neutral skeleton from the REST asset each frame instead of mutating it via SetBone
   from the (possibly exploded) live frame — breaks the feedback loop.
Both require Xbox X/Y ground truth on the neutral frame (current Xenia bone-walk read is offset-bugged)
to verify. Left as diagnosis; `HamIKEffector::Poll` confirmed unchanged at 99.9% (HX_NATIVE probes
only). New reusable probes: `DC3_IK_DIAG BackXform` / `PARENTCHAIN` (HamIKEffector.cpp), `NEUTRALCHAIN`
+ vec NeutralWXfm (HamIKSkeleton.cpp).

---

## 2026-06-08 — DECISIVE: feet-in-floor is an ANIMATION-pose bug, NOT an IK bug (whole prior theory REFUTED)

**The single most important result in this doc.** Every theory above (empty constraints, dirty
cascade, neutral-frame contamination, back-transform explosion, the two candidate fixes
CharLocalIKScope + re-derive-neutral) was chasing a computation the renderer **throws away**. The
rendered foot does not depend on the IK at all.

### Method — the validation gate

`native/tests/test_gameplay_telemetry.cpp::FeetNotBelowFloorDuringGameplay` runs the full headless
boot→YMCA-gameplay path (`DC3_GAMEPLAY_TESTS=1 native/build/milo-tests`), parses `DC3_TEL`, and
asserts every gameplay sample has toe world Z ≥ −2.0. Baseline: **L-toe −4.20 (ankle 0.20),
R-toe −4.10; ~700/737 samples below floor.** No GPU/Xenia needed. (Other 47 GameplayTelemetryTest
cases pass.) This is a far faster, deterministic gate than the Xenia GPU loop.

### The kill shot — toe Z is INVARIANT under every IK on/off combination

Added off-by-default `#ifdef HX_NATIVE` skip switches and measured worst toe Z (one build, env-switched):

| config | env | worst toe Z |
|---|---|---|
| baseline (matched stamp) | `DC3_IK_NEUTRAL=stamp` | −4.10 |
| neutral re-rooted char-local (cand #2, X/Y-zero) | `DC3_IK_NEUTRAL=local` | −4.20 |
| skip ankle HamIKEffector write | `DC3_IK_FOOT_SKIP=1` | −4.20 |
| skip pelvis HamIKEffector write | `DC3_IK_PELVIS_SKIP=1` | −4.20 |
| skip BOTH leg HamIKEffectors | `DC3_IK_FOOT_SKIP=1 DC3_IK_PELVIS_SKIP=1` | −4.20 |
| skip CharIKFoot::Poll | `DC3_IK_CHARFOOT_SKIP=1` | −4.20 |
| **skip ALL foot IK (pure anim)** | all three =1 | **−4.20** |

**Toe Z is identical (−4.2) whether the IK computes a sane value, a garbage value, or runs not at
all.** The foot follows the **animation/skinning pose**, period. This includes `CharIKFoot`, whose
`DoFSM` writes the foot bone's *local* xfm (`CharIKFoot.cpp:95,109` — a local write that *would*
survive render) — and even disabling it changes nothing, because it just copies the already-sunk
toe-target (`mFinger`) Z onto the foot.

### Why the IK can't matter — `SetWorldXfm` never writes the local (confirmed)

`RndTransformable::SetWorldXfm` (`Trans.cpp:408-415`) sets `mWorldXfm`, clears `mDirty`, cascades
`SetDirty` to children — but does **not** update `mLocalXfm`. The pelvis HamIKEffector polls LAST
and re-dirties the ankle subtree; at render `WorldXfm_Force` (`Trans.cpp:655`) recomputes the ankle
from its **stale anim `mLocalXfm`**, discarding every `mEffector->SetWorldXfm(finalXfm)` the IK
wrote. Captured live (`DC3_FCHAIN`, frame 801): the IK writes an exploded ankle/toe world (~150–220)
yet telemetry reads the sane sunk anim value (−4.2). The doc's earlier hope that "fixing the input
frame lets the same cascade preserve a sane ankle" is **false**: the cascade discards the IK write
unconditionally (sane or not), because the local is never reconciled.

### Corrected root cause

The animated/skinned foot is ~4u too low in gameplay: rest ankle 4.39 / toe 0.01 → gameplay ankle
~0.2 / toe ~−4.2 (a uniform ~4.2 drop; pelvis drops 42.5→34.8, knee absorbs ~3.5). The IK is
*supposed* to plant the foot during the crouch but its world-write is structurally discarded on
native. Xbox clamp-cave ground truth still holds and is now the key asymmetry: **Xbox's toe-target
(`eff`) sits at the floor (effZ ≈ 0.88) while native's sinks to −4.** So the divergence lives in the
**animated foot / toe-target itself**, upstream of all IK.

This strongly resembles the **rb3 char-skinning fix** (wrong skeleton/bind → mis-posed bones;
`rb3 acc'd` + engine `12455b0`, `RebindOutfitBonesToOwnSkeleton`) — the same shared Milo char engine,
the same class of native skeleton/bind/LP64 divergence. That is the leading hypothesis for the next
pass.

### What changed in the tree (this session) — diagnostics only, ZERO default behavior change

All `#ifdef HX_NATIVE`, all off by default (default native behavior byte-identical to baseline; Wii/Xbox
match build untouched):
- `HamIKEffector.cpp`: `DC3_IK_FOOT_SKIP` / `DC3_IK_PELVIS_SKIP` — skip the ankle/pelvis IK world-write
  (`goto done`) to prove IK-irrelevance.
- `CharIKFoot.cpp`: `DC3_IK_CHARFOOT_SKIP` — early-return `Poll`.
- `HamIKSkeleton.cpp`: `DC3_IK_NEUTRAL=local` — opt-in char-local re-root of the neutral (kills the
  q.v=neutral+eff doubling / back-transform explosion; makes the *discarded* IK computation sane —
  no rendered effect). Default = the original matched stamp.

### Next steps (next pass)

1. **Trace the sunk toe-target/foot on native.** Why is `spot_*-toe.trans` / `bone_*-toe.mesh` world
   Z = −4 in gameplay? Walk its parent chain + bind; check the skeleton/bind path against the rb3
   `RebindOutfitBonesToOwnSkeleton` fix and the gender-bind investigation (same engine).
2. **Confirm vs Xbox** whether the pelvis genuinely crouches to ~35 on Xbox (then the feet must be
   planted by surviving IK) or stays ~42 (then native's anim over-crouches). This is the only place
   the **Xenia bone-world-Z read offset (P0 blocker)** still matters — the clamp-cave already gives
   the toe-target (floor) but not the live leg chain.
3. If the anim is faithful and Xbox relies on foot IK that survives, the native fix is a *survivable*
   foot-plant (write the ankle **local**, fed a floor toe-target) — NOT any of the world-write IK
   tweaks tried so far.

---

## 2026-06-08 (cont.) — AIRTIGHT: ALL foot-IK is world-write (discarded on BOTH platforms) ⇒ it's a NATIVE ANIM-POSE divergence

Pushed the 2026-06-08 finding to a complete, self-consistent root cause. Three new experiments, all
off-by-default `#ifdef HX_NATIVE` diagnostics (default behavior byte-identical; GameplayTelemetryTest
47/48 unchanged):

### (a) The pelvis-drop is in `bone_pelvis.mesh` LOCAL Z, not the facing channel
`PARENTCHAIN` (HamIKEffector.cpp:781) walked the full leg→root chain in gameplay (director f=801):
| bone (local Z) | iconman (CLEAN, planted) | player0/1 (SUNK) |
|---|---|---|
| root (iconman / playerN) | 0.00 | 0.11 (on floor ✓) |
| **bone_pelvis.mesh** | **42.51** | **34.49** |
| → ankle.mesh world Z | 4.39 | ~0.0 |
| → toe world Z | 0.01 | ~−4 |

The dancer's `bone_pelvis.mesh` local Z is ~8u lower than iconman's, and the leg then over-extends
straight down (knee local rotation barely flexes) so ankle→0, toe→−4. `CharServoBone::MoveToFacing`
was RULED OUT: probed `*mFacingPos` = (small X, small Y, **Z=0.000**) for every char — the facing
channel only nudges X/Y on-stage, it does not drop the pelvis. (Note: `CharServoBone`'s `mPelvis`
"bone_pelvis" servo node is a *different* transform at local Z=0 than the skeleton `bone_pelvis.mesh`
in the render chain.)

### (b) The ENTIRE foot-IK subsystem is world-write ⇒ discarded; confirmed by skip + clamp tests
- `HamIKEffector::Poll` writes `mEffector->SetWorldXfm(finalXfm)`.
- `CharIKHand::Poll` (the leg solver behind `CharIKFoot`) writes `mHand->SetWorldXfm(...)` **4× and
  zero local writes** (CharIKHand.cpp:131,160,164,167).
- `RndTransformable::SetWorldXfm` (Trans.cpp:408) never writes `mLocalXfm`; the pelvis effector polls
  LAST and re-dirties the leg; render `WorldXfm_Force` (Trans.cpp:655) recomputes every bone from its
  stale anim local ⇒ every IK write is discarded.
- Confirmed empirically: clamping the `CharIKFoot::DoFSM` foot GOAL to the floor
  (`DC3_IK_FOOTPLANT=1`, CharIKFoot.cpp) changes the rendered toe by **0.0** — the leg solver's
  world-write does not survive, so even a perfect floor goal can't plant the foot.

### (c) THE LOGICAL KILL SHOT — it must be a native ANIM divergence
This engine is matched (Wii/Xbox decomp): `SetWorldXfm` (no local writeback), the pelvis-last poll
order, and the `WorldXfm_Force` recompute-from-local cascade are all the SAME on the real Xbox 360.
**So the IK is discarded on Xbox too** — yet Xbox's foot is planted (visual ground truth). Therefore
**Xbox plants the foot via its ANIMATION POSE**, and **native's animation pose diverges** (sinks).
There is no surviving IK on either platform to "fix"; the bug is that native's animated dancer pose
puts the pelvis ~8u low with an unbent, over-extended leg, where Xbox's keeps the foot on the floor.
(Corollary: the earlier "make the foot IK survive / survivable foot-plant" idea is a band-aid that
would DIVERGE from Xbox — Xbox doesn't survive-IK either. Do not pursue it as the faithful fix.)

### Corrected next steps (supersede the previous list)
1. **Verify the dancer's POSE-channel decompression on native** — specifically the `bone_pelvis.mesh`
   POSITION channel and the `bone_*-knee`/leg ROTATION channels (CharBonesSamples::EvaluateChannel,
   CharBones::ScaleAdd, CharBonesMeshes::PoseMeshes). The doc earlier verified only the *ankle
   rotation* channel as bit-exact; the pelvis-position and knee-rotation channels are UNVERIFIED and
   are the prime suspects (a position/rotation channel-stride or LP64 read divergence would drop the
   pelvis and straighten the leg). The dancer's pelvis local Z is ~constant 34.5 (barely animated),
   which is itself suspicious — a real dance crouch would bounce.
2. **Xbox ground truth (the one place the Xenia P0 bone-world-Z read still matters):** the dancer's
   `bone_pelvis.mesh` / `bone_*-knee` LOCAL during the crouch. If Xbox pelvis ≈ 42 → native
   over-drops the pelvis (position-channel bug). If Xbox pelvis ≈ 34.5 with a bent knee → native's
   knee-rotation channel fails to flex (rotation-channel bug). Either way it is a pose-channel fix.
3. The fix will be in the native pose/skinning pipeline (a faithful channel decode), NOT in any IK.

### Diagnostics added this session (committed; all `#ifdef HX_NATIVE`, off by default)
`DC3_IK_FOOT_SKIP` / `DC3_IK_PELVIS_SKIP` (HamIKEffector), `DC3_IK_CHARFOOT_SKIP` +
`DC3_IK_FOOTPLANT` (CharIKFoot), `DC3_IK_NEUTRAL=local` (HamIKSkeleton), and `DC3_IK_DIAG Facing`
(CharServoBone::MoveToFacing). Plus the pre-existing `ChainZ` / `PARENTCHAIN` / `BackXform` / `FCHAIN`
probes (gated on `DC3_IK_DIAG`; ChainZ needs director-frame > 3000 ⇒ MILO_MAX_FRAMES ≈ 20000).

---

## ⚠️ 2026-06-08 (re-verify) — the "toe channel decode" conclusion above is PARTLY OVERTURNED

A fresh adversarial re-verification (4-agent workflow + the `DC3_IK_FOOT_SKIP`/`CHARFOOT_SKIP`
experiments) corrected the specifics above. See **`docs/sessions/2026-06-08-feet-reverify-data.md`**
for the full data. Summary of what changed:

- **STILL TRUE:** the bug is a native divergence in the rendered POSE, not a localized matched-code
  edit; the sink is in the raw `PoseMeshes` output, IK-independent (IK-skip is byte-identical).
- **OVERTURNED:** "it's the **toe** channel decode." The toe is NOT independently mis-decoded — it
  tracks the ankle by the correct rest offset (gameplay 3.89 ≈ rest 4.38). It is the **leg/ankle**
  that is lowered ~4u below player0's own rest height (7.40 → 0.45). New lead: the leg/pelvis bone
  *local lengths* change between rest and live (shin 15.39→18.48, pelvis-Z 90.64→34.49), pointing at
  leg/pelvis translation channels or a skeleton-bind/LP64 decode — never the toe.
- **OVERTURNED:** "IK is irrelevant (proven by invariance)." Invariance is trivial because the
  foot-plant IK applies **zero weight** (`constraintCount=0` on 9920/9920 samples) — it computes a
  planted effector (effZ≈0.45≈ground, HamIKEffector.cpp:686) but never applies it to the skeleton.
  Whether that no-apply is matched-Xbox-behavior (⇒ decode bug) or a native divergence (⇒ IK-apply
  bug) is the ONE remaining fork, and it needs a reliable Xbox/Xenia ankle-Z capture (P0).
