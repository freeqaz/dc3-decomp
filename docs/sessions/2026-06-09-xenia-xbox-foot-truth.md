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
