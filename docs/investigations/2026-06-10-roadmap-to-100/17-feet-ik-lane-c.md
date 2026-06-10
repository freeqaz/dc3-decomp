# 17 — Lane C: Feet/IK Bug — doc-12 F6 verification + mConstraints wiring + measured residual

**Date:** 2026-06-10. **Lane:** Wave-2 Lane C (`wave2/c-feet-ik`).
**Worktree:** `/home/free/code/milohax/wt-wave2-c-feet-ik`.
**Source plan:** `93-EXECUTION-WAVE-2.md` Lane C. **Prior:** `docs/sessions/2026-06-03-ik-ground-truth-comparison.md`
(+ `docs/sessions/2026-06-08-feet-reverify-data.md` Pushes 1–7b), doc 12 F6, doc 11 F7.

All measurements below were computed in this worktree (objdiff via the orchestrator MCP, native
build RelWithDebInfo, milo-tests run from `orig-assets/`). No `decomp.db` writes; no main commits.

---

## Acceptance item 1 — doc-12 F6 "int-vs-float field" claim: **REFUTED with asm evidence**

**The claim (doc 12 F6, lines 115–122):** `CharIKFoot::DoFSM` is divergent at 97.4%; the 4 `replace`
lines at offsets 0x30/0x34 where **TGT uses `lwz`/`stw` (integer)** and **SRC uses `lfs`/`stfs`
(float)** prove "a **field-type mismatch** — a `Vector`/`Transform` field declared/handled as int
vs float — which would corrupt ankle/toe placement math … the strongest single decomp-bug suspect
for the feet bug."

**What the asm actually shows** (`run_objdiff` on
`?DoFSM@CharIKFoot@@IAAXPAVCharacter@@AAVTransform@@@Z`, 97.4% normalized, full listing):

| idx | TARGET (Xbox) | BASE (our src) | source line |
|----:|---------------|----------------|-------------|
| 91 | `lwz  r11, 0x30, r3`  | `lfs  f0, 0x30, r3`  | `tf.v.x = wt.v.x;` (load wt.v.x) |
| 93 | `stw  r11, 0x30, r29` | `stfs f0, 0x30, r30` | store tf.v.x |
| 94 | `lwz  r11, 0x34, r3`  | `lfs  f0, 0x34, r3`  | `tf.v.y = wt.v.y;` (load wt.v.y) |
| 95 | `stw  r11, 0x34, r29` | `stfs f0, 0x34, r30` | store tf.v.y |

These are the two assignments in the `if (mFootFsmState == 0)` block of `DoFSM`
(`src/system/char/CharIKFoot.cpp:454-457`):
```cpp
const Transform &wt = mFinger->WorldXfm();   // r3 = &wt
tf.v.x = wt.v.x;   // offset 0x30
tf.v.y = wt.v.y;   // offset 0x34
```

**Three independent proofs the field is genuinely `float`, not `int`:**

1. **Struct ground truth** (`lookup_struct_offset Transform 0x30` / `0x34`, RB2 DWARF):
   offset 0x30 = `Transform::v` (`class Vector3`); 0x34 = `+0x4` within it = `Vector3::y`.
   Both are float fields of the Vector3 translation. Both sides agree on the offset (no offset diff).

2. **The SAME offsets are accessed with `lfs`/`stfs` (float) elsewhere in the SAME function on the
   TARGET** — idx 121 `lfs f13,0x34,r29`, 127 `lfs f12,0x30,r3`, 128 `lfs f11,0x30,r29`,
   150/157/172–176 `lfs`/`stfs` on 0x30/0x34/0x38. A genuinely int-declared field could not be
   read as float two instructions later. The field IS float on Xbox.

3. **Behaviorally inert.** `lwz`/`stw` (a 32-bit GPR bit-copy of the float pattern) and `lfs`/`stfs`
   (a float load/store) move the **identical 4 bytes** — a member-to-member `float = float` copy. No
   bit is changed. It cannot "corrupt ankle/toe placement math."

**What the `lwz`/`stw` actually is:** an MSVC **lowering / copy-propagation** choice. Immediately
after this block the target copies the whole `tf.v` (and `mFootPosition = tf.v`) via integer
`lwz`/`stw` (idx 97–105: `lwz r10,0x30,r29 → stw r10,0xe0,r31`, …). MSVC bit-copies the
`tf.v.x=wt.v.x; tf.v.y=wt.v.y;` pair through GPRs because the value is only forwarded into that
adjacent integer struct copy, never used arithmetically there. Our build loads as float (`lfs`),
stores as float (`stfs`), then re-loads as int — same result, different lowering.

**Permuter exhaustion (the floor certificate):** ran the full guided source-synthesis permuter on
`DoFSM` (`decomp_synth.scan_and_permute --max-rounds 10 --max-variants 100 --plateau-limit 3
--chain-depth 5`): **192 candidates scored across the whole catalog** (variable_extraction,
declaration_reorder, statement_reorder, Ghidra-guided reorder for all 6 regswap pairs, FMA reorder,
const_ref_swap, bool_to_uchar, comparison flips, …). **0 improvements; DoFSM is stuck at 97.41%.**
The residual 2.6% is the classic r29↔r30 **register-allocation cascade** (22 of 44 swap instrs;
seeded by the param-assignment order at idx 7/8: target puts `mMe`→r30 / `tf`→r29, we put the
reverse) + 2 commutative `fmuls`/`fadds` (idx 168/173 — known backend floor, memory
`stream3_fmuls_operand_order_floor`) + the `lwz/stw` float-bit-copy lowering. **All cosmetic,
behaviorally neutral, source-unreachable.**

**Verdict:** doc-12 F6's "int-vs-float field declared wrong" rationale is **WRONG** — same family as
doc-11's MemAlloc rationale being refuted. DoFSM has **no field-type bug and no logic divergence**;
it is a regalloc/lowering floor at 97.4% with zero behavioral effect. It is **not** a feet-bug lever.

---

## Acceptance item 2 — `HamIKEffector::mConstraints` wiring: explained with evidence

**Claim (doc 12 F6 / F-table item 3 / TODO.md:8):** "trace why `mConstraints` is never populated at
native runtime → fix at root."

**Evidence (`src/system/hamobj/HamIKEffector.cpp`):** `mConstraints` is **pure serialized data**.
The only population path is the binary loader: `d >> mConstraints;` (HamIKEffector.cpp:232, inside
`BEGIN_LOADS`), plus `SYNC_PROP(constraints, mConstraints)` (:160), `bs << mConstraints` (:178),
`COPY_MEMBER(mConstraints)` (:196). There is **no procedural fill** anywhere — it is whatever the
`.milo` data file holds for that effector. Each `Constraint` is `{mTarget, mWeight}`
(operator>> at :201–211).

**Why native has 0:** because the loaded `.milo` data for these ankle/pelvis/hand effectors **has
zero constraints**. And that is **faithful to Xbox**: the prior Xenia ground-truth capture
(`2026-06-03-ik-ground-truth-comparison.md` Phase 1b/1c) measured `constraints=0` on **all five
effectors on the real Xbox binary too** ("effector constraints (L/R-ankle, L/R-hand, pelvis) = 0
ALL five | 0 same"). Empty `mConstraints` is **NORMAL on both platforms** and is **NOT a missing
native wiring path** — there is nothing to "wire up." The doc-12 / TODO premise that it *should* be
populated is refuted by the Xbox capture. "Populate the constraints" was already REFUTED in the
prior session and remains refuted; do not pursue it.

---

## Acceptance item 3 — the failing test: **BLOCKED on a Lane-A boot crash**; residual narrowed

**Gate:** `GameplayTelemetryTest.FeetNotBelowFloorDuringGameplay`
(`native/tests/test_gameplay_telemetry.cpp:963`). Asserts every gameplay toe sample Z ≥ −2.0.

**Measured this lane (native RelWithDebInfo, run from `orig-assets/`):** the gate **cannot reach
gameplay** — it fails at `ASSERT_FALSE(playing.empty())` ("no telemetry samples", test runs ~1.2 s)
because **dc3-native SIGSEGVs during App construction**, before any gameplay frame:

```
SIGSEGV (signal 11) at address (nil)
  ObjPtrList<CamShot,ObjectDir>::insert
  CameraManager::RandomizeCategory  (CameraManager.cpp:181)
  CameraManager::SyncObjects → WorldDir::SyncObjects → DirLoader::Cleanup
  → … → UIManager::Init → HamUI::Init → App::App
```

- This is **the exact pre-existing crash Wave-2 Lane A owns** (plan §Lane A item 1:
  "Fix `CameraManager::RandomizeCategory` (vector OOB during App construction — it crashes
  dc3-native before the HTTP server binds)"). The **main-repo binary built today (Jun 10 21:48)
  reproduces the identical crash** — this is current main HEAD state, not a worktree artifact.
- Root cause (diagnosed, NOT fixed here — Lane A's file): `CameraManager::RandomizeCategory`
  (`CameraManager.cpp:181`) does `std::vector<CamShot*> camshots; { MemTemp m;
  camshots.resize(camlist.size()); }` — the vector's backing buffer is allocated from the `MemTemp`
  temp arena, which is **freed at the `}` scope exit** while `camshots` is still used (sort/swap/
  `camlist.push_back(camshots[i])`) → host use-after-free → crash in `insert`.
  `RandomizeCategory` is **100.0% matched to Xbox** (`run_objdiff` =
  `?RandomizeCategory@CameraManager@@AAAX...` 100.0%), so the MemTemp scope is faithful; it is
  **host-UB only** (Xbox's temp allocator tolerated it). The correct fix is an **HX_NATIVE guard**
  (drop the MemTemp scope so the vector uses the normal heap) — squarely Lane A's task; **not
  touched here** to avoid a cross-lane conflict.

Because the venue camera setup is on the gameplay critical path and there is no env flag to skip it,
**the feet gate is unreachable until Lane A lands the CameraManager fix.** This is a hard cross-lane
dependency, reported as a contradiction/blocker.

### Residual cause narrowed with my own measurements (Xenia-free, no venue boot)

The `ClipPoseFixture` suite poses `char/main` + a crouch clip directly on the shared skeleton,
bypassing the crashing venue boot. **All 12 ClipPoseFixture tests PASS in this worktree.** The
decode/plant evidence (`LegBoneDecodeChannelTypesAndLocalStability`):

- Leg bones are **ROTATION-ONLY** (`bone_L-thigh/knee/ankle/toe.*`, `dFromBind = 0.000` across all
  beats — no LP64 channel-stride / local-translation corruption). Only `bone_pelvis.mesh`
  translates (`.pos`+`.quat`).
- The crouch clip **plants the foot**: toe worldZ = **+2.70 / +2.52 / +1.56** (ABOVE floor),
  ankle worldZ = +2.45 / +2.53 / +0.80 — even at deep crouch (pelvis localZ 12.6 → 18.9).

So, **independently re-confirmed this lane:** the pose-channel decode is faithful and plants the
foot in isolation → the feet-in-floor sink is **NOT** a decode bug, **NOT** the DoFSM int/float
red herring, and **NOT** empty mConstraints. It is specific to the **gameplay song-move + venue
path** (consistent with `2026-06-08-feet-reverify-data.md` PUSH 6/7), which is exactly the path the
CameraManager crash blocks. The prior session's leading unresolved hypothesis (a native poll-order
divergence in *when* the foot-plant IK runs relative to the song-move pose, HamDriver.cpp:95–101 —
still UNCONFIRMED per PUSH 7b) is the live residual.

---

## Net result

| Acceptance item | Result |
|---|---|
| doc-12 F6 field claim | **REFUTED with asm** (offset 0x30/0x34 = float `Transform::v.x/.y`; `lwz/stw` = MSVC float-bit-copy lowering, behaviorally inert; DoFSM permuter-exhausted at 97.41% = regalloc floor, zero logic diff) |
| mConstraints wiring | **Explained:** serialized-data-only (no procedural fill); empty = faithful (Xbox also = 0); no native wiring gap to fix |
| failing gate | **BLOCKED** by Lane-A `CameraManager::RandomizeCategory` boot crash (current main HEAD too); residual narrowed to gameplay song-move/poll-order path; decode independently re-proven faithful (ClipPoseFixture 12/12 plant the foot) |

**No PPC match regressions:** zero source edits landed (the permuter reverted its 0-win run);
`CharIKFoot::DoFSM` stays 97.4%, `CameraManager::RandomizeCategory` stays 100.0%.

## Recommended next step (for the orchestrator)

Re-run this lane (or fold into Lane A's verification) **after Lane A lands the CameraManager +
CharBones::ScaleDown boot fix** — only then can `FeetNotBelowFloorDuringGameplay` reach gameplay and
the song-move/poll-order residual be measured and attacked. Do **not** spend further effort on
DoFSM int/float or mConstraints (both closed here).
