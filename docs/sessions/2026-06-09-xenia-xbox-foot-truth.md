# 2026-06-09 — Xenia is UP: live Xbox dance/foot telemetry captured (feet-in-floor ground truth)

## Headline
After months blocked on "Xenia can't reach a playing song headless," the current `xenia-headless`
(branch `headless-vulkan-linux`, built Jun 5 with ~2,211 lines of uncommitted working-tree work)
**boots → renders → plays the song → the dancer ANIMATES**, and we read **live Xbox IK/foot
telemetry**. This is the ground truth the feet-in-floor investigation
([[dc3-feet-in-floor-anim]], `docs/sessions/2026-06-08-feet-reverify-data.md`) needed.

The "async-completion stall" we set out to fix was **already solved** in the working tree (the
`merge_busy` FileMerger HOLD). The doc `docs/runtime/XENIA_ASYNC_COMPLETION_STALL.md` is reframed;
its old "import thunk `0x83A00964` / song-load CS spin" framing was a misdiagnosis (that address is
guest BSS, read by a buggy diagnostic; the `0x825E4794` spin is the Kinect `SkeletonUpdate` gesture
poll, which does NOT block the dance).

## How it was established (5-agent recon + a single GPU run)
- Read-only recon workflow mapped: the uncommitted tree (merge HOLD + unpause nudge + IK-telemetry
  rig + render stabilization), the spin (`CriticalSection::Enter`@0x825E4778 on
  `SkeletonUpdateHandle::sCritSec`@0x82F5F888 — both confirmed against `config/373307D9/symbols.txt`),
  and the import red herring.
- One GPU run reproduced a PLAYING song with live IK telemetry. Log:
  `/tmp/xenia-stall-baseline/run.log`.

## Xbox ankle ground truth (the decisive new data)
`DC3:IK CLAMP` advances **frame 990 → 5280** (every 30; the dance is really animating). The Xbox
raw-animated ("neutral", pre-IK) **ankle Z in venue-world** (floor at Z=0, `groundHeight=0`):

| stat | value |
|---|---|
| n samples | 144 |
| min | −0.346 |
| median | **0.049** (= at floor) |
| p90 | 4.999 |
| max | 10.575 |
| below floor (Z<0) | 23 / 144 |
| near floor [0,0.6] | 85 / 144 |
| lifted (Z>1, dance steps) | 30 / 144 |

Foot-plant **IK is near-inert on Xbox**: `clampF=0.0000` on every sample (the clamp only engages
for feet lifted >5u above ground). Verdicts: 288 `PLANTED(z~floor)`, 143 `PLANTED(neutral!=eff)`.

**Interpretation:** on Xbox the ankle DANCES and mostly sits AT the floor (median 0.05), even dips
slightly negative on some frames, and the IK does essentially nothing to lift it. **This matches the
native finding** (native: ankle raw-posed at floor, IK inert/discarded — `docs/sessions/2026-06-08-feet-reverify-data.md`).
→ The native pose pipeline is **faithful at the ankle**; the ankle height is set by the raw move
pose on BOTH platforms, and the IK is a near-no-op on BOTH.

## The one remaining decisive datum: Xbox TOE Z
The feet gate fails on the **toe** (native toe ~−4.2 with ankle ~0). The toe is NOT an IK effector,
so it is not in the CLAMP data; and the `bone_*.mesh world=(0,0,5.00xx)` "BONE" telemetry is a
**broken read** (wrong offset/instance — returns a near-constant while the same object's effector
read animates). So we do not yet have the Xbox toe.

**The crux comparison (to settle the whole investigation):**
- Xbox ankle ≈ native ankle (both ~0, near floor) — CONFIRMED.
- If **Xbox toe ≈ 0 (flat foot)** while **native toe ≈ −4** → native bug is a **foot/ankle-rotation
  or knee-extension** divergence (cf native "leg over-extends straight, knee barely flexes"), NOT
  ankle height → redirects the native fix to the foot-orientation/knee channel.
- If **Xbox toe ≈ −4 too** → the move authentically points the foot down here and **Xbox sinks too**
  → the gate's premise is wrong for this dance (accept-premise).

## Next step (Push: get the Xbox toe)
Fix the `bone_*-toe.mesh` / `bone_*-knee` world-Z read in the IK telemetry (`xenia
src/xenia/dc3_hack_pack.cc`, `ReadDc3IKTelemetry` bone-walk via `TheHamWardrobe 0x82F60110`). The
effector/CLAMP read path works (reads the live posed skeleton); the bone-walk path reads a wrong
offset/instance. Once toe Z reads correctly, capture a dense trajectory and compare frame-matched
against the native toe. (GDB-RSP read of the toe `mWorldXfm.v` is the fallback if fixing the read is
slow — RSP client `xenia docs/dc3_rsp_client.py`, stub cvars `--dc3_gdb_rsp_stub=true
--dc3_gdb_rsp_port=9001 --dc3_gdb_rsp_break_on_connect=true`.)

## DECISIVE — Xbox TOE captured (GDB-RSP, read-only): Xbox plants the foot, native sinks it

Read the live posed skeleton's rendered bone world transforms over the RSP stub (no source edits,
no rebuild). **Bone-memory layout cracked:** `RndTransformable::mWorldXfm` is a `Transform{Matrix3,
Vector3}` at `+0x48`, and on **Xbox 360 the Vector3 is 16-byte aligned (VMX128)** → Matrix3 is 0x30
(not 0x24) → the world translation `v` is at **`+0x78`** (`v.x +0x78, v.y +0x7C, v.z +0x80`). (The
in-tree IK telemetry's `.mesh` bone-read assumed unpadded 0x0C vectors → read `+0x6C`/`+0xAC` →
garbage constant (0,0,5); that's why the bone-walk looked "frozen". The matrix at
`+0x48/+0x58/+0x68` verified as a valid orthonormal rotation.)

Two independent gameplay frames (venue-world, floor at Z=0; `bone_*.mesh` rendered world `v.z`):

| frame | L-toe | R-toe | L-ankle | R-ankle | pelvis |
|---|---|---|---|---|---|
| A | **0.025** | **0.021** | 4.10 | 4.27 | 35.9 |
| B | **0.527** | **0.006** | 5.38 | 4.08 | 39.3 |

**Xbox: toes planted on the floor (Z ∈ [0.006, 0.53], NEVER negative), ankles ~4–5 above, pelvis
~36–39 (hip height).** A normal standing/dancing skeleton with feet on the ground.

**Native (gate + prior telemetry): toe ≈ −4.2 (below floor), ankle ≈ 0.2 (at floor).**

### Conclusion (answers the months-old gated question)
- **The feet-in-floor bug is a REAL native divergence, NOT "Xbox sinks too" / accept-premise.**
  Xbox plants the toe (~0); native sinks it (−4.2). Δtoe ≈ 4.2.
- **The divergence is in the LEG, not the ankle height per se.** Xbox keeps the ankle ~4.1–5.4
  ABOVE the floor; native collapses it to ~0.2 (~4 too low). The native pelvis is roughly
  comparable, so the **lower leg over-extends downward ~4u** (knee too straight) — matching the
  prior native note "leg over-extends straight, knee barely flexes."
- Note the apparent tension with the prior "native pose decode is faithful (drift 0.000)" finding:
  that gtest used a CROUCH/test clip, not the gameplay song-move. Either the gameplay-move leg decode
  diverges, or something downstream drops the native leg ~4u that Xbox doesn't. **Next:** capture the
  native gameplay rendered bone world Z (toe/ankle/knee/pelvis) and compare frame-shape to the Xbox
  table above to pin which leg segment (knee vs ankle vs hip) over-extends → that localizes the fix.

Tools: `/tmp/xenia-rsp/read_bones.py` (offset-finder dump), `/tmp/xenia-rsp/traj.py` (Z trajectory).
Xbox bone addrs are per-run (heap); parse them from `DC3:IK BONE ... meshBase=` and read `meshBase+0x78`.

## Reproduction
```bash
# Build (only when xenia src changes; one process at a time on native/build dirs):
cd /home/free/code/milohax/xenia/build && make xenia-headless config=checked_linux -j"$(nproc)"

# Run the YMCA flow (GPU; dangerouslyDisableSandbox). Binary already at build/bin/Linux/Checked/.
XENIA=/home/free/code/milohax/xenia/build/bin/Linux/Checked/xenia-headless
DC3=/home/free/code/milohax/dc3-decomp
cd "$DC3" && timeout 230 "$XENIA" \
  --target=orig-assets/debug.xex --gpu=vulkan \
  --dc3_nui_patch_layout=original --dc3_crt_skip_nui=true --fake_kinect_data=true \
  --dc3_ik_telemetry=true \
  --scripted_input_file=scripts/dc3-input-flows/xenia-ymca.txt \
  --headless_timeout_ms=200000 2>&1 | tee /tmp/xenia-run/run.log

# Extract the Xbox ankle trajectory:
grep -oE 'IK CLAMP2 \[frame [0-9]+\] name=ankle neutral=\([^)]*\)' /tmp/xenia-run/run.log
```

## Do NOT clobber (uncommitted xenia working tree, separate efforts)
- GPU/Vulkan render-stabilization files + `docs/dc3_render_stabilization.md` /
  `dc3_render_pipeline_architecture.md`.
- `emulator.cc` `merge_busy` HOLD + `dc3_game_screen_real_goto` cvar + CompleteLaunch patches.
- `nop_input_driver.cc` UNPAUSE NUDGE.
- `dc3_hack_pack.cc` IK-telemetry rig (this IS the task-#15 measurement tool — extend, don't rewrite).
- Committed APC fix `4f3a5d8bf` (correct; irrelevant to this; keep).

## PUSH 9 (2026-06-09, ultracode) — FRAME-MATCHED: the feet sink is an UNAPPLIED LEG IK, not the anim pose

Built the native per-segment leg table and the **frame-matched** Xbox leg trajectory
(per-frame telemetry, corrected VMX128 offsets). This OVERTURNS the prior
"Xbox plants via the anim pose / IK discarded on both" conclusion.

### Native per-segment (FootGeom/FootLocal diag, GameplayTelemetry.cpp)
| | rest | gameplay (sunk) |
|---|---|---|
| pelvis world Z | 42.51 | 35.2 |
| thigh world Z | — | 35.28 |
| knee(shin) world Z | — | 17.83 |
| ankle world Z | 4.39 | ~0.1 |
| toe world Z | 0.01 | −3.8 |
| knee LOCAL v.x (femur) | 20.27 | 17.56 |
| knee LOCAL rotZ | ~−4° | **−20°** |

### Bone lengths are FAITHFUL — femur "shrink" was a RED HERRING
Xbox `bone_L-knee.mesh` LOCAL v.x = **17.708, CONSTANT across all 150 gameplay
frames** (min==max). Native posed femur = 17.56 → matches. The native "rest" 20.27
is the raw-mesh bind before the skeleton_bones.servo poses it; the posed value is
~17.6 on BOTH platforms. Tibia (Xbox 18.23 / native ~18.0) and thigh-offset (3.70 /
3.60) also match. Bone lengths/decode are not the bug.

### Frame-matched knee flex (the decisive datum) — pelvis ≈ 35.2 on both
| @ pelvis ~35.2 | knee rotZ | ankle world Z | toe world Z |
|---|---|---|---|
| **Xbox** | **−58°** | 4.41 | **0.01 (PLANTED)** |
| **Native** | **−20°** | ~0.1 | **−3.8 (SUNK)** |

(Xbox trajectory: pelvis 33.9–41.0, knee rotZ −9.75°…−115.5°, toe world Z min −0.00
i.e. never below floor. Native pure-anim knee = −20° at the same beat.)

### Conclusion
- The knee.rotz DECODE is shared/matched and the BE swap is correct for comp=1
  (ShortQuat) — verified A/B (DC3_CLIP_SWAP_LEGACY identical). So native's pure-anim
  knee (−20°, IK discarded) == Xbox's pure-anim knee.
- Xbox's rendered knee LOCAL is −58° at the matched beat → **Xbox's LEG IK writes
  ~−38° of extra knee flex INTO THE LOCAL transform (it survives render) to plant
  the foot.** Native's leg IK contributes ~0 (DC3_IK_CHARFOOT_SKIP byte-identical →
  CharIKFoot inactive; HamIKEffector clampF=0 + world-write discarded).
- **The feet-in-floor bug = the leg foot-plant IK is not applied on native.** The
  prior "Xbox plants via anim, survive-fix would diverge" conclusion is REFUTED by
  the frame-matched Xbox knee (−58° ≠ anim −20°). A survivable leg-IK foot-plant is
  exactly what Xbox does.

### NEXT (Push 10): activate the native leg IK
CharIKFoot (the 2-bone leg solver, writes LOCAL → survives) is inactive on native:
its `if (mFinger && mHand && mData)` guard fails (CharIKFoot.cpp:105). Find why
mFinger/mHand/mData are null on native (init gap) and fix so CharIKFoot bends the
knee (~−58° local) to plant the foot. Gate `#ifdef HX_NATIVE`, opt-out env. Verify
against GameplayTelemetryTest.FeetNotBelowFloorDuringGameplay (toe Z >= −2.0) and
re-capture the native knee rotZ → should reach ~−58° at pelvis 35.2.

Tools: native FootLocal/RestLocal diag (committed); Xbox per-frame leg telemetry
(xenia dc3_hack_pack.cc, committed) + /tmp/xenia-rsp/parse_traj.py (frame-match by
pelvis). Native run: dc3-native + ymca.txt; Xbox run: xenia + xenia-ymca.txt.

## PUSH 10 (2026-06-09, ultracode) — ROOT CAUSE: leg IK knee-bend overwritten by a later PoseMeshes (poll order)

The feet sink because the **leg foot-plant IK bends the knee via a LOCAL write, but
the anim PoseMeshes runs AFTER the IK on native and overwrites it** → the IK is 100%
discarded. Confirmed end-to-end:

1. `CharIKHand::IKElbow` DOES write LOCAL transforms — `elbow->DirtyLocalXfm().m.Set(
   cos,sin,0,-sin,cos,0,0,0,1)` (CharIKHand.cpp:~399) is a knee Z-rotation written to
   LOCAL (survives the WorldXfm recompute). The prior "CharIKHand writes only
   SetWorldXfm, ZERO local writes" claim was WRONG (it missed IKElbow).
2. `IKElbow` only runs `if (charWeight != 0 || mAlwaysIKElbow)` AND not gated off by
   `if (!mMoveElbow) shoulderParent = 0` (CharIKHand.cpp:322). On native the leg
   `*.ikfoot` has **mMoveElbow=false** (ctor default is true), so shoulderParent is
   zeroed → IKElbow gets null shoulder → skips the knee bend. (Diag: charWeight=1.0,
   hand=bone_L-ankle.mesh, handParent=bone_L-knee.mesh NON-NULL, but knee/shoulder=null.)
3. Forcing the elbow path (DC3_IK_MOVEELBOW=1) makes IKElbow run and write the knee
   LOCAL to **−36.4°** (post-IK diag) — BUT the rendered foot is byte-identical (FootGeom
   ankle −0.13, toe −4.02) and the knee LOCAL at Sample time is back to **−20° (anim)**
   (FootLocal m.x=(0.941,−0.338)). So PoseMeshes overwrote the IK's local write.
4. Poll trace confirms the order: `CharIKFootPoll` (IK) → `POSEMESHES dir='skeleton'`
   within the frame → PoseMeshes (anim) runs AFTER the IK and re-poses bone_L-knee.mesh.

### Frame-matched proof (PUSH 9): Xbox knee −58° (planted), native −20° (sunk) @ pelvis 35.2.
Xbox's IK survives (renders the bent knee); native's is discarded (poll order). Bone
lengths/decode are faithful (femur constant 17.7).

### The fix (Push 11)
Two coupled native divergences, both `#ifdef HX_NATIVE` + opt-out:
- **Poll order**: the leg IK (CharIKFoot/CharIKHand) must poll AFTER the skeleton
  PoseMeshes so its knee-bend local write survives (Xbox order = pose-then-IK). Lives in
  `CharPollableSorter::Sort` (CharPollGroup.cpp) — LP64 pointer-keyed std::map iteration
  reorders the pollables vs Xbox. THE primary fix.
- **mMoveElbow**: the leg ikfoot needs the elbow-move path (DC3_IK_MOVEELBOW proved it
  re-enables the knee bend). Confirm whether Xbox's *.ikfoot mMoveElbow=true (load/prop
  divergence) — if so, fix the load; else the env-gated force is the native override.

Open: with poll order fixed, verify the IK reaches Xbox's −58° (the foot-plant FSM
target must clamp to the floor, CharIKFoot::DoFSM / DC3_IK_FOOTPLANT) and the gate passes
(toe Z >= −2.0). Tools: CharIKHand IKHand diag + GameplayTelemetry FootLocal (committed).

## PUSH 11 (2026-06-09, ultracode) — fix direction CONFIRMED (poll order), partial plant, remaining blocker

`CharIKHand::PollDeps` declares the knee/thigh dependency **only `if (mMoveElbow && mHand)`**
(CharIKHand.cpp). So native's `mMoveElbow=false` for the leg `*.ikfoot` breaks BOTH the
knee bend (Poll line 322 zeros shoulderParent) AND the sorter's knowledge that the IK
touches the knee — so the IK sorts before the skeleton pose and gets overwritten.

Forcing the elbow-move path in BOTH Poll and PollDeps (DC3_IK_MOVEELBOW=1) + clamping the
foot-plant goal to the floor (DC3_IK_FOOTPLANT=1):
- **LEFT foot now PLANTS on many frames** (toe −1.7, ankle 4.0) — the IK knee-bend survives.
- **RIGHT foot still sinks** (toe −3.9); overall min toe still −4.3.

This **L/R asymmetry confirms poll order is the lever**: the name-based AlphaSort
(`left.ikfoot` vs `right.ikfoot`) + the dependency graph place the two leg IKs at different
positions relative to the skeleton pose, so one survives and one is overwritten. (Bone
lengths/decode faithful; charWeight=1.0; the IK bend itself works when it's allowed to run
and survive.)

### mMoveElbow load: it is the REAL loaded value, not a desync
CharWeightable/ObjPtr/IKTarget all stream platform-independent byte counts (verified) and
the bool read is 1 byte — so the stream is in sync and native reads exactly what the .milo
says (mMoveElbow=false). Either the .milo carries move_elbow=false and Xbox flips it true via
an inline prop/setup that native skips (SYNC_PROP move_elbow), OR Xbox loads it true — needs
an Xbox-side read (CharIKHand+0x6b) or a .milo/prop-script check to settle.

### Remaining (Push 12) — make BOTH leg IKs reliably poll after the full pose
The skeleton pose is applied by `CharServoBone::Poll -> PoseMeshes` (a pollable, sortable)
AND possibly by driver-side `HamDirector::PoseMeshes` / `HamCharacter` clip->PoseMeshes
(outside the CharPollGroup sort). The fix must guarantee the leg IK's knee-bend local write
is the LAST word for both feet:
- Set mMoveElbow=true for the leg ikfoot the faithful way (find the Xbox mechanism: load vs
  inline prop), so PollDeps declares the dependency for both feet.
- Verify the CharPollableSorter::Sort result orders BOTH left/right ikfoot after
  skeleton_bones.servo; if a driver-side PoseMeshes runs later, address that ordering too.
- Then the IK bends the knee to the floor-clamped target (~−58°, Xbox) and the gate
  (toe Z >= −2.0) passes for both feet.
All current changes are `#ifdef HX_NATIVE` + `DC3_IK_MOVEELBOW`/`DC3_IK_FOOTPLANT` (off by
default) — the byte-matched build is untouched. Logs: /tmp/dc3-feet-fix/{moveelbow2,me_fp}.log.

## PUSH 12 (2026-06-09, ultracode) — fix mechanism IMPLEMENTED (IK survives); IK-solve diverges (WIP, opt-in)

Implemented the leg-IK survival fix and confirmed it makes the knee bend survive the move
pose — but applying the IK reveals a THIRD native bug: the foot-plant solve diverges. Fix is
gated **opt-in** (`DC3_FEET_PLANT_FIX=1`, default OFF) so the native build is unbroken.

### The full overwrite chain (now mapped end-to-end)
The dancer's flattened poll order (RndDir::SyncObjects, the world's `!IsSubDir` flatten):
`[6] bone.servo  [22] song.hdrv  [28] feetandhands.pgrp(leg IK)`. The leg IK lives inside the
`feetandhands.pgrp` CharPollGroup; the skeleton pose servo (`skeleton_bones.servo`) is in a
sibling subdir. CRUCIALLY the gameplay move is applied by **`HamDirector::Poll` →
`ClipPlayer::PlayAnims(player0)`**, a SEPARATE RndPollable that runs AFTER the dancers' char
poll and re-poses the skeleton — overwriting the IK's knee bend. So no within-dancer reorder
can win; the IK must re-assert after HamDirector.

### What was implemented (all `#ifdef HX_NATIVE`, opt-in DC3_FEET_PLANT_FIX)
1. `CharIKFoot::Load` forces `mMoveElbow=true` — native loaded it false, which both skipped
   the IKElbow knee bend (Poll:322) and dropped the knee/thigh dep from CharIKHand::PollDeps.
2. `CharIKFoot::Poll` single-run gate (`gDc3DirectorIKReRun`): skip the IK during the normal
   char poll; run it ONCE from the director re-run after the move pose (running it twice
   destabilizes the foot-plant FSM).
3. `HamDirector::Poll` re-asserts each dancer's leg IK (`Find<CharIKFoot>("left/right.ikfoot")
   ->Poll()`) after PlayAnims, wrapped in `gDc3DirectorIKReRun=true/false`.
4. `RndDir::SyncObjects` moves `ikfoot`/`feetandhands` poll entries to the end of the flattened
   list (belt-and-suspenders; the HamDirector re-run is the effective fix).
   (A redundant no-op reorder also sits in `Character::SyncObjects` — the dancer's CharIKFoot
   are NOT in a Character's mPolls, they're in the world's flattened RndDir list; clean up.)

### Result: IK survives but DIVERGES
With the fix on: knee LOCAL is now −36.4° at Sample (survived; was −20° anim). BUT player0's
foot goes WILD — toe min −28, max +66; ankle up to +71. So when the native leg IK is actually
applied (single run, after the pose), the **foot-plant solve diverges** (it does NOT just settle
to a bent, planted leg like Xbox's −58°). Default-OFF confirmed stable (original sink −4.3..3.9).

### Remaining (Push 13) — stabilize the native IK solve
The divergence is a feedback loop: the IK moves the ankle → moves `mFinger` (toe-target, child
of ankle) → next frame the target follows → grows. On Xbox the foot-plant FSM
(`CharIKFoot::DoFSM`) LOCKS the foot at `mFootPosition` when grounded (the `b2` detection on
`mData->LocalXfm().v[mDataIndex]` + tf.v.z thresholds), breaking the loop. On native the lock
isn't engaging (suspect: the foot-plant `mData` channel not driven, or `MeasureLengths`
reach/`mInv2ab`, or the mFinger retarget). Next: instrument DoFSM (vecat/b2/mFootFsmState,
mFootPosition, mWorldDst, mAAPlusBB) for player0's left ikfoot during the re-run; confirm the
FSM plant engages; if `mData` is the issue, ensure the move drives the foot-plant data channel.
Target: knee ~−58° STABLE, toe ∈ [~0, small], gate FeetNotBelowFloorDuringGameplay (toe>=−2).

Logs: /tmp/dc3-feet-fix/{single_ik,hamdir_fix,default_off}.log. Gate:
`DC3_FEET_PLANT_FIX=1 DC3_GAMEPLAY_TESTS=1 native/build/milo-tests --gtest_filter='GameplayTelemetryTest.FeetNotBelowFloorDuringGameplay'`.
