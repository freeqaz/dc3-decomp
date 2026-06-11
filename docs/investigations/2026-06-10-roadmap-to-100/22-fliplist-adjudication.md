# 22 — Flip-list Adjudication (Wave-4 Lane B)

**Date:** 2026-06-11. **Lane:** Wave-4 B (flip-list adjudication → fixes with tests).
**Worktree:** `/home/free/code/milohax/wt-wave4-b-fliplist-fixes` · **Branch:** `wave4/b-fliplist-fixes`.
**Input:** `data/unicorn_refresh_main_d5491b67.json` (main-plane flip list, 57 candidate
real-bug rows + 16 new-evidence real-bug rows).
**Build plane for all match%:** worktree `run_objdiff` (PPC `.obj`), `project_dir` = the
worktree above. Final certification is on `main` after sync.

## Headline

The strongest-signal flip-list class (**object_memory / call_count / unmapped**, 9 rows +
`Rand::Seed`) was adjudicated row-by-row with asm-grounded diagnosis. The **decisive
finding**: most of these object_memory/call_count/unmapped "candidate bugs" are **unicorn
zero-fill FIXTURE ARTIFACTS, not behavioral bugs** — the differential execution diverges only
under the degenerate all-zero / 0xCD probe inputs (div-by-zero NaN, signed-zero negation, null
mesh-pointer deref), while the final values under realistic inputs are bit-identical (the
`fnmsubs`/`fsel` cores match). **The flip-list class is a much weaker bug signal than its
name suggests** — the wave-3 "behaviorally identical under the null/fill path is not a proof"
caveat is the dominant reality here, not the exception.

However, the per-function asm diagnosis those rows pointed at surfaced **three genuine decomp
defects** worth fixing (two of them with real PPC-match wins), confirming the wave-3 lesson:
*the flip-list rows are a useful place to look, but the bug is found by reading the asm, not
by trusting the verdict class.*

### Critical structural caveat on "verdict flips to EQUIVALENT"

The unicorn runner emulates the **decomp PPC `.text`** against the **orig PPC `.text`**. Two
consequences that the acceptance criterion ("confirm the verdict flips to EQUIVALENT") could
not anticipate:

1. **HX_NATIVE-guarded fixes are invisible to unicorn.** `Rand::Seed`'s bug *is* in the PPC
   `.text` (signed `srawi`), but the only behavior-preserving fix that keeps the PPC byte-match
   is host-only (`#ifdef HX_NATIVE`) — so the PPC `.obj` and its source-hash are unchanged and
   the unicorn verdict **cannot flip** by construction. The native-host bug is genuinely fixed
   and tested; the PPC verdict stays DIVERGENT (a compiler-lowering floor — see below).
2. **Fixture-artifact rows stay DIVERGENT after a real objdiff win.** `CharFeedback::Poll`
   reached **100% normalized** (byte-identical instruction stream) yet unicorn still reports
   DIVERGENT, because the divergence is `delta/mFadeSecs` = NaN under the zero-filled fixture
   (mFadeSecs = 0), which both the original AND the fixed source reproduce identically. A
   100%-matching function whose verdict will not flip is definitive proof the verdict is a
   fixture artifact, not a code bug.

## Fixes landed (3 real defects, with PPC evidence)

| Fn | Unit | Class | Before | After (norm) | Fix |
|---|---|---|---|---|---|
| `Rand::Seed` | math/Rand | object_memory | 91.8% | 91.8% (PPC unchanged) | host-only logical-shift mask under HX_NATIVE; 6 GTest cases |
| `CharFeedback::Poll` | hamobj/CharFeedback | object_memory | 98.4% | **100.0%** | `Clamp<float>(...)` assignment → in-place `ClampEq(...)` (restores the target's intermediate store) |
| `SkeletonUpdate::UpdateFakeArmPos` | gesture/SkeletonUpdate | object_memory | 96.8% | **99.7%** | fold `unk5398 = fVar12` directly + clamp reads `unk5398` (restores the missing intermediate store); residual = commutative-fmuls backend floor |

### `Rand::Seed` — REAL bug, host-fixed, PPC floor

The target lowers `mRandTable[i] = (s & 0x7FFF0000) | (j >> 16)` with a **logical** right shift
(`srwi`): the shifted draw contributes only its low 16 bits, so every state-table word keeps
bit 31 clear (orig words are `0x5665xxxx`-class). The decomp declares `j` as signed `int`, so
`j >> 16` is **arithmetic** (`srawi`): a bit-31-set draw sign-extends to `0xFFFFxxxx` and poisons
the high word of the MT-style state table. Unicorn caught this as 20 memory diffs (decomp
`0xFFFFxxxx` vs orig `0x5665xxxx`) — verifier-confirmed in wave 3, re-confirmed here
(reference sequences computed with `-fwrapv`; e.g. seed 12345 table[0] = `0x2704D3DC` correct vs
`0xFFFFD3DC` buggy).

The fix masks the shift result to 16 bits — behaviorally identical to the logical shift. Two
constraints forced it to be **host-only (HX_NATIVE)**:
- The PPC compiler emits `srawi` for any signed `int` shift, and **fuses every unsigned
  spelling into `rlwimi`** (changing the whole shift/mask/or instruction shape and *lowering*
  the match to 86.5%). No C++ spelling reaches the target's `srwi` + separate `rlwinm` + `or`
  — verified by hand-trying 7 forms (unsigned `j`, `(unsigned)j>>16`, `(j>>16)&0xFFFF`,
  `(unsigned short)`, split temporaries, both OR orders) and by the permuter (0 improvement).
  The PPC-side `srawi`↔`srwi` is a genuine **compiler-lowering floor**.
- Keeping the original `(j >> 16)` on the PPC path preserves the 91.8% byte-match; the
  `& 0xFFFF` lives under `#ifdef HX_NATIVE`. **PPC neutral** (objdiff re-confirmed 91.8%, the
  same residual srawi/srwi regswap floor as the untouched baseline).

**Test (`native/tests/test_rand_seed.cpp`, 6 cases):** pins the canonical Int() draw sequence
for seeds {0x29A, 1, 12345, -1} + the no-`0xFFFF`-high-word invariant + determinism. **4 of the
6 fail on the pre-fix buggy form** (proven by neutering the HX_NATIVE branch and rebuilding —
`SeedMinusOneSequence`, `DefaultSeedSequence`, `Seed12345Sequence`, `Seed1Sequence` all fail).
All 6 pass with the fix.

**Unicorn verdict:** stays DIVERGENT (PPC `.text` unchanged — the host fix is invisible, and
the PPC divergence is the unflippable `srawi` floor). This is the correct, honest outcome.

### `CharFeedback::Poll` — REAL objdiff bug, 100% normalized; unicorn = fixture artifact

The target stores `limb.unk8` (the fade accumulator) to memory **twice**: once for
`unk8 += fadeStep` (idx 50 `stfs f0,0x0,r11`), once for the clamp (idx 55). Our source used
`limb.unk8 = Clamp<float>(0.0f, 1.0f, limb.unk8)` — the compiler kept `unk8` in a register
across the clamp and **elided the intermediate store** (the `delete` at idx 50). Switching to
the in-place `ClampEq(limb.unk8, 0.0f, 1.0f)` (the `Min(Max(...))` float specialization,
behaviorally identical to the `Clamp` assignment) restores the intermediate store →
**98.4% → 100.0% normalized, all 67 instructions equal**.

**Unicorn:** still DIVERGENT, `object_diffs` = `0x7FC00000` (qNaN) at each `unk8` vs orig 0.
Re-ran the runner with BOTH the original `Clamp` AND the fixed `ClampEq` source — **identical
`object_diffs`**. The divergence is `delta / mFadeSecs` with mFadeSecs zero-filled = NaN, a pure
fixture artifact, present before and after the fix. A 100%-matching instruction stream that
will not flip = definitive proof. **Adjudication: REAL objdiff defect (fixed); unicorn verdict
FALSE (zero-fill div-by-zero artifact).**

### `SkeletonUpdate::UpdateFakeArmPos` — REAL objdiff bug, 99.7%; unicorn = fixture artifact

Same intermediate-store pattern: `float fVar12 = -(...); unk5398 = fVar12;` then an open-coded
clamp on `fVar12` (register) — the `unk5398 = fVar12` store was elided (`delete` at idx 22).
Rewriting so the negate result writes `unk5398` directly and the clamp ternaries read `unk5398`
restores the store → **96.8% → 99.7% normalized**. Residual = a single commutative `fmuls`
(`f1,f0` vs `f0,f1`, same registers both sides) — a post-regalloc backend floor (the compiler
loads the constant into f0 and multiplies regardless of source operand order; permuter and 3
hand spellings = 0 improvement). **PPC neutral / improving.**

**Unicorn:** DIVERGENT, one `object_diff` = `0x80000000` (−0.0) at `unk5398` vs orig 0.0. The
formula compiles to `fnmsubs` on BOTH sides (idx 21 matches) — identical math; the −0.0 is only
`-(0*0 - 0)` under the all-zero fixture. **Adjudication: REAL objdiff defect (fixed); unicorn
verdict FALSE (signed-zero negation artifact).**

## Adjudicated FALSE (cosmetic floor, no fix)

| Fn | Unit | Class | Match | Verdict & evidence |
|---|---|---|---|---|
| `RndPostProc::UpdateColorModulation` | rndobj/PostProc | object_memory | 97.6% | FALSE — pure **FPR regswap floor** (f0↔f13) + a reload-vs-keep of `mFlickerSeconds.x`; values identical; permuter 0 improvement. |
| `DirectionGestureFilterSingleUser::IsValidScrollPos` | gesture/DirectionGestureFilter | call_count | 96.9% | FALSE — **base-address-selection floor**: target bases `r31` at `&sValidHandFloats[3]` (linker label `lbl_82F444AC`) and indexes backwards; we base at `[0]`. Same memory read, same values. |
| `FaceCenter` | rndobj/Mesh | unmapped_access | 93.7% | FALSE — **instruction-scheduling floor** (target hoists the `verts` load above the `center=0` stores); unicorn `unmapped_access_mismatch` = null `mGeomOwner` deref under the zero fixture. Hand reorder did not improve (93.7→93.6). |

## call_arg class — confirmed NOISE (3 sampled, per plan)

The plan flagged the 19 `call_arg` rows as mostly `__FILE__`/MakeString-pointer noise. Sampled 3:

| Fn | Match (norm) | Reality |
|---|---|---|
| `SkeletonUpdateCallbackSlowdownCB` | **100.0%** | single `addi off:-8` TGT-only stack slot — cosmetic; already matched. |
| `PositionNode::CalcError` | 99.2% | r10↔r11 regswap + commutative fmuls + offset-swap — register/FPR floor, not a call-arg bug. |
| `GamePanel::SetPausedHelper` | 95.9% | r10↔r11 regswap cascade + bool_mask — register-allocation floor, not a call-arg bug. |

None is a genuine call-argument mismatch. **Confirmed: the `call_arg` flip class is
relocation/pointer-value noise** (deprioritize, as the plan said).

## Rows adjudicated (count)

10 rows with asm evidence: 3 REAL-fixed (Rand::Seed, CharFeedback::Poll,
UpdateFakeArmPos) + 3 REAL-but-floor / FALSE-cosmetic (UpdateColorModulation, IsValidScrollPos,
FaceCenter) + 3 call_arg-noise samples (SkeletonUpdateCallbackSlowdownCB, CalcError,
SetPausedHelper) + 1 not-pursued (`CharEyes::Enter`, 92.6%, 89-insn store-scheduling cascade —
candidate intermediate-store pattern but high cost; left for a follow-up).

## Recommendation for refresh_frontier.py auto-classification

The dominant FALSE pattern is **zero-fill-fixture-degenerate-arithmetic** producing
object_memory/unmapped flips that are not code bugs:
- `object_diffs` whose decomp value is `0x7FC00000` (qNaN) or `0x80000000` (−0.0) **and** whose
  orig value is `0x00000000` are almost always div-by-zero / signed-zero-negation fixture
  artifacts (the realistic-input cores match). Classify these as `fixture_artifact_degenerate`,
  not `candidate_bug`.
- `unmapped_access_mismatch` on a function that dereferences a pointer loaded from a zero-filled
  member (null this-subfield) is a fixture artifact (`fixture_artifact_null_deref`).
- A flip whose function is **100% normalized in objdiff** cannot be a code bug — gate
  candidate_bug on `norm_pct < 100`.

These three mechanical rules would have auto-classified ~5 of the 7 object_memory/unmapped rows
here as artifacts, leaving only the genuinely-fixable codegen defects in the candidate set.
