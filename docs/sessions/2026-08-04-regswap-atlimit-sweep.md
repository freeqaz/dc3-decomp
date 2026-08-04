# 2026-08-04 — AT_LIMIT + REGISTER_SWAP sweep (statement-level half)

Sweep of the `verdict='AT_LIMIT' AND has_register_swap=1 AND is_stub=0 AND
excluded=0` bucket. Population is **836 functions**, not the "~485" older docs
claim. A blind stratified audit the previous session measured the real hit rate
of this bucket at **3/10 = 30%** — a third of a bucket labelled unfixable had
real, byte-exact source fixes, and one of them (`HDCache::WriteDone`) was a
live bug.

This session's job: sweep more of the remaining ~830 at roughly **1 win per 3
functions**, scoped to the *statement-level* half of the bucket.

---

## The triage rule being calibrated

Split candidates by **what the residual implicates**:

- **Statement-level → INVESTIGATE.** Tells: insert/delete clusters, Function
  Call Diff rows, `addi`/`lwz` field-offset diffs, `__savegprlr_NN` deltas,
  control-flow polarity, signed-vs-unsigned compares, bool materialization,
  extra/missing inlined call. Implicated: control flow, which field is read,
  which call is made, what stays live across a call, the shape of an
  *explicitly nested* expression.
- **Within one arithmetic expression → FLOOR, skip.** Commutative operand
  order, flat-sum term order, which of two independent loads issues first.
  MSVC canonicalises a flat `a+b+c` to `A+(C+B)` however you write it.
  - **Exception:** explicit parenthesization is NOT inert. A nested
    `a+(b+(c+…))` chain preserves its shape, and its term order IS recoverable
    from the asm — target and base share a schedule, so it's a fixed
    permutation: read it off the base and invert.

Register swaps are **symptoms, not causes**. Nobody permutes a register name;
the whole swap set flips at once when the real issue is fixed.

---

## The database is not trustworthy for this bucket

Measured, not speculated:

- `verdict='AT_LIMIT'` is wrong ≥30% of the time here.
- `tier=` has **no discriminative power and is mildly ANTI-correlated**: 9/10
  sampled were `tier=A_HAND_FIXABLE` and 8 of those 9 failed; the single
  `tier=B_PERMUTER` was a win. `share=` is likewise unrelated to outcome.
- `current_percent` is **stale by up to 12 points** (DB 94.94 vs measured 86.1;
  53.47 vs 41.1). Do not band on it — re-measure with `run_objdiff` passing
  `project_dir` = your worktree.
- `has_prologue_mismatch` is identically 0 for every row — the detector never
  populated it, so that heuristic cannot be applied from the DB at all.
- A `REGISTER_SWAP` flag can be pure noise: one sampled function was 88
  inserts / 88 deletes — an incomplete reconstruction, not a swap case.

`PROLOGUE_MISMATCH` is the fingerprint of a value held across a call — do NOT
read it as floor evidence.

---

## NEW LEVER — inline-level counting via the parameter home area

**This is the main research result of the session.** Full write-up in
[`docs/decomp/patterns/fixable-inline-boundary.md`](../decomp/patterns/fixable-inline-boundary.md#inline-level-counting-via-the-parameter-home-area).

On this build every *inlined* callee writes its `this` into the outgoing
parameter home slot — you see a recurring `stw rN, 0x50(r31)` with no matching
reload. Those stores are dead, so they are easy to dismiss as noise. They are
not noise: **counting them counts inline levels.**

- An **extra base-only** `addi rN, rBase, <field-offset>` + `stw rN, 0x50(r31)`
  pair ⇒ our source has *one more* inline level than the target at that point.
  Usual cause: a small accessor delegating to another inline.
- A **target-only** home write ⇒ the target has an inline level we are missing.

Worked example (landed):

```cpp
// src/system/rndobj/Part.h — ours (one extra inline level: Vector2::Set)
void SetEmitRate(float x, float y) { mEmitRate.Set(x, y); }

// target — assigns the components directly
void SetEmitRate(float x, float y) {
    mEmitRate.x = x;
    mEmitRate.y = y;
}
```

`RhythmBattlePlayer::UpdateScore(Hmx::Object*)` showed a base-only
`addi r10, r11, 0x198` + `stw r10, 0x50(r31)` around
`mStealPart->SetEmitRate(0, 0)`. `0x198` is `RndParticleSys::mEmitRate`, so the
extra home write was `Vector2::Set`'s `this`. Rewriting the accessor took the
function **97.5% → 98.4%** and collapsed the insert/delete clusters entirely.

**Caveat:** this is a header edit, so re-measure *every* caller of the accessor
before committing. For `SetEmitRate` the other four callers
(`RndParticleSys::OnSetEmitRate`, `RndParticleSysAnim::SetFrame`,
`RhythmBattlePlayer::Enter`, `ResourceFileCacheHelper::CacheFile`) all stayed
at 100%.

---

## Other levers confirmed this session

### `bool` vs `unsigned char` for flag locals

An `unsigned char` local consumed in a bool context makes MSVC emit a
uchar→bool normalization (`clrlwi` / `subic` / `subfe`) at the call site that
the target does not have. Declaring the local `bool` removes it.

Confirmed on `BustAMovePanel::Poll` (`isPlayer0Pink`, `forceShow`).

### Signed vs unsigned compares, and the conversion that goes with them

Target `cmpw`/`cmpwi` vs base `cmplw`/`cmplwi` ⇒ a spurious unsigned cast in
our source (or a missing one). Loop bounds are the usual culprit. Read the
*conversion* instruction as well to recover the induction variable's declared
type:

- `extsw` ⇒ the variable is a signed `int`
- `rldicl r,r,0,32` ⇒ unsigned

`BustAMovePanel::Poll`'s debug loop needed **both**: `int i` (for `extsw` on
`(float)i`) compared **unsigned** against `mSongStructure.size()`. Writing
`for (int i = 0; i < mSongStructure.size(); i++)` — no `(int)` cast on
`size()` — gets both. `(unsigned int i)` gets the compare right but the
conversion wrong; `i < (int)size()` gets the conversion right but the compare
wrong.

---

## Noise that is NOT worth chasing

- `ADDRESS_RELOCATION_NOISE`, `LINKER_MERGED` / `merged_<addr>`.
- **Function Call Diff rows that differ only in `MakeString<>` template array
  sizes.** e.g. target `MakeString<char const[8], int, char const[35]>` used 3×
  vs base `MakeString<char const[13], int, char const[17]>` + `[13],[33]` +
  `[13],[13]` used 1× each. Those are the `MILO_ASSERT` `(__FILE__, line, #expr)`
  instantiations. objdiff diffs the **pre-link .obj**, so our TU shows its three
  distinct instantiations while the target shows the single ICF-folded survivor,
  whose name is picked arbitrarily from some other TU. Pure naming noise.
- `__savegprlr` vs `__savegprlr_14` in the same row — same class of artifact.

---

## Results

Landed on `sweep/regswap-statement` (main coordinator lane):

| Function | Before | After | Cause |
|---|---|---|---|
| `BustAMovePanel::Poll` | 97.7% | **98.6%** | `unsigned char` → `bool` flag locals; signed→unsigned loop-bound compare |
| `RhythmBattlePlayer::UpdateScore(Hmx::Object*)` | 97.5% | **98.4%** | `RndParticleSys::SetEmitRate` had one inline level too many (`Part.h`) |

Six parallel lanes (`sweep/regswap-a` … `sweep/regswap-f`, worktrees
`wt/rsw-a` … `wt/rsw-f`) ran the same playbook over ~40 further candidates.
Their results are folded into the final report for this sweep.

### Investigated and dropped

- `WorldInstance::SyncDir` — 99.4%, 5 instructions. Target copies the
  `__RTDynamicCast` result with `addi r11, r3, 0` before the pointer→bool
  `subic`/`subfe`, and schedules the unrelated `cmplwi cr6, r16, 0` between
  them. Splitting `bool curMesh = dynamic_cast<RndMesh*>(&*it)` into a named
  pointer plus `!= nullptr` was byte-identical (MSVC coalesced it). Not
  resolved.
- `HamNavList::Poll` — 98.6%. Residual is 4 target-only `stw ..., 0x50(r31)`
  home writes (the same home-area signature as the new lever, pointing the
  *other* way: the target has inline levels we lack) plus a `Smoother::SetParams`
  field-store order that lives in a shared header. **Good re-entry point now
  that the home-area lever is understood.**
- `MoveDir::PostUpdateFilters` — 97.2%, 6 register-swap pairs. Real signal in
  the `prevFracs[2]` loop (target `stfs` + separate `addi` vs base `stfsu`
  update-form) and in the Ham1 `limbErrors` block (target keeps both a base
  pointer and a `+0x18` derived pointer live; base overwrites in place). Not
  attempted for time.

---

## Calibration of the statement-vs-expression rule

Coordinator lane: **5 triaged, 4 investigated, 2 fixed.**

The split held in the sense that mattered — *both* wins came from
statement-level tells (bool materialization, signed/unsigned compare, extra
inlined call), and nothing was lost by refusing to touch expression-level
residuals. But at very high match (≥98%) statement-level classification is
**necessary, not sufficient**: `SyncDir` and `HamNavList::Poll` both presented
clean statement-level tells and both resisted. Treat the rule as a filter for
what to *open*, not a predictor of what will *close*.

### One honest caveat worth recording

On `BustAMovePanel::Poll` the target uses a **signed** `cmpwi r11, 3` for the
`mBeatCount >= 3` guard while our source casts to unsigned. Dropping the cast
fixes that instruction but perturbs FPR/`addi` scheduling ~20 instructions
later for a net **−0.3%**. The cast stays, and the divergence is documented in
the commit message. `mBeatCount` is reset to 0 and only incremented, so the two
forms are behaviourally identical at runtime — this is a match-score artifact,
**not** a live bug. Recording it because "the semantically-correct edit scores
lower" is a real and recurring situation, and silently keeping the wrong-looking
source without a note is how these get re-litigated every few months.

### Live bugs

None found in the coordinator lane. (For contrast, the previous session's
`HDCache::WriteDone` had `1 << mWriteBlock` where the target had
`1 << (mWriteBlock % 32)` — the wrong bit was set in `mBlockState` for any
block index ≥ 32.)

---

## Method notes for the next sweep

- Work in a worktree: `scripts/setup_worktree.sh <path> <branch>`. Fresh
  worktrees need a `ninja` warmup (~1 min) before the first `run_objdiff`.
- Always pass `project_dir` to `run_objdiff` / `run_diff_inspect` — without it
  the tool measures the main repo, not your edits, and your changes are
  invisible.
- `run_diff_inspect mode=attributed` needs a `/FAs` recompile and segfaults on
  some TUs (`BustAMovePanel.cpp`). Fall back to `run_objdiff full_listing=true`
  and grep the emitted `function_analysis/` file — never read it whole.
- The **Region Summary** table from `run_objdiff concise=false` is the fastest
  statement-vs-expression triage: contiguous 100% regions punctuated by small
  low-% regions ⇒ statement-level; a smear of register swaps across every
  region ⇒ regalloc symptom, find the single upstream cause or move on.
- Land via rebase-then-`git merge --no-ff`. Never cherry-pick/squash/`--ff-only`.
