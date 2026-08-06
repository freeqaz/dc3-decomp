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

> **Correction added 2026-08-06 — this section is left as written; read the
> correction before using it.** Two claims below are refuted by controlled
> probes: (a) the slot is **not** the parameter home area — it is the first
> local-temp slot, onto which several unrelated temps coalesce; and (b) it is
> **not** true that "every inlined callee" writes its `this` there. The store
> requires all three of: a C++ EH state in the caller (`__CxxFrameHandler`), an
> inlined callee whose `this` is a **computed sub-object address**, and that
> callee using `this` **at least twice**. The lever is real and the stores are
> removable from source, but strictly qualifying sites are **rare** (14
> functions in the whole binary). See
> [the 2026-08-06 correction](../decomp/patterns/fixable-inline-boundary.md#correction-2026-08-06--not-an-abi-property-and-not-the-home-area).

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
| `BustAMovePanel::Poll` | 97.7% | **99.6%** | `unsigned char` → `bool` flag locals; signed→unsigned loop-bound compare; then a follow-up deep dive (bool initialiser polarity, decl order, preheader local, parked-cast retry) — see [investigation](../investigations/2026-08-04-bustamovepanel-poll.md) |
| `RhythmBattlePlayer::UpdateScore(Hmx::Object*)` | 97.5% | 97.5% | **REVERTED** — the `SetEmitRate` flattening gained 0.9% here and lost 0.9% on `RndScaleObject`, which is 60% larger. See the correction in [fixable-inline-boundary.md](../decomp/patterns/fixable-inline-boundary.md#correction-same-day--a-single-call-site-does-not-determine-the-body) |

Six parallel lanes (`sweep/regswap-a` … `sweep/regswap-f`, worktrees
`wt/rsw-a` … `wt/rsw-f`) ran the same playbook. **26 functions triaged, 22
investigated, 21 improved, 5 driven to a byte-exact 100%.**

| Lane | Function | Before | After |
|---|---|---|---|
| A | `RndTransformable::Load` | 93.6% | **100.0%** |
| A | `CharEyes::Poll` | 94.7% | 96.2% |
| A | `HamCamShot::UpdateTargetsFlipped` | 94.2% | 96.2% |
| A | `FlowMathOp::Apply` | 96.9% | 97.9% |
| B | `Debug::DoCrucible` | 95.2% | 99.3% |
| B | `HamDirector::OnPopulateMoves` | 95.4% | 98.1% |
| B | `HamCharacter::SyncProperty` | 96.5% | 97.9% |
| B | `Locale::Init` | 88.2% | 92.1% |
| C | `RndPostProc::LoadRev` | 94.1% | **100.0%** |
| C | `Spotlight::BuildNGSheet` | 88.0% | 88.7% |
| D | `FlowIf::Activate` | 87.6% | **100.0%** |
| D | `MoveDir::DrawShowing` | 86.0% | 95.3% |
| D | `DataNode::Equal` | 94.3% | 98.0% |
| E | `HamDirector::DrawIconMan` | 92.5% | 98.4% |
| E | `SkeletonChooser::DrawDebug` | 84.6% | 97.9% |
| E | `SkeletonChooser::ChoosePlayerSides` | 93.8% | 95.8% |
| E | `OSCMessenger::Poll` | 87.3% | 95.6% |
| E | `RndMeshDeform::Load` | 90.3% | 93.7% |
| F | `Sound::Handle` | 97.1% | **100.0%** |
| F | `RndMesh::SetVolume` | 92.0% | 99.3% |
| F | `HamCharacter::Poll` | 91.7% | 98.3% |
| F | `RndShadowMap::PrepShadow` | 89.5% | 92.7% |

Integration verified: all seven branches merge cleanly, the merged tree builds
and links, and the two files touched by two lanes each (`HamDirector.cpp` by B
and E, `HamCharacter.cpp` by B and F) hold every claimed percentage with both
lanes' edits present. Whole-tree delta **53.84% → 53.85% fuzzy, +4 newly
matched functions** across 9 subsystems.

## LIVE BUGS FOUND (11 confirmed)

This is the real return on the sweep. A bucket labelled "unfixable register
allocation" was hiding eleven behavioural defects in shipped code paths.

**`OSCMessenger::Poll` — the OSC parser was comprehensively broken (lane E).**
Three independent bugs, any one of which breaks it: `pos` was
`strlen(str)/4 + 1`, a *word* index, where the target computes
`len - len%4 + 4`, the *byte* offset of the type-tag string, so every
`data[pos+N]` read garbage; `i`/`f` payloads were copied one byte at a time
(`value.buffer[0] = data[pos+4]`) where the target copies a full 32-bit word,
so every `GetInt`/`GetFloat` returned a value built from one byte; and the
`fff` vector payload read `dataIn[1..3]` from the start of the packet,
ignoring `pos` entirely.

**`Locale::Init` — all four permanent locale tables were allocated on the temp
heap (lane B).** `MemPopTemp()` sat at the bottom of the function, after
`mSymTable`, `mStringData`, `mStrTable` and `mUploadedFlags` were allocated.
The target pops at the parse block's closing brace, before the first
allocation. These tables are read by every `Localize()` call for the entire
run of the game.

**`Locale::Init` — the devkit locale override could never have worked (lane
B).** `DataArrayPtr altCfg((DataNode(devkitPath)), DataNode(locale))` builds
`("devkit:\locale\…\locale_keep.dta" locale)`, but the reader treats Node(0)
as the section tag and Node(1..) as the file list — so the only "file" opened
was the literal string `"locale"`. MSVC evaluates arguments right-to-left and
the target constructs the String node first, exactly inverted from ours.

**`HamCamShot::UpdateTargetsFlipped` — the flipped/unflipped player remap was
inverted (lane A).** `if (!flipped)` where the target has `if (flipped)`. In
dance-battle / flipped camera shots every keyframe target named `*player0*`
was remapped for the wrong dancer, and left un-remapped when it should have
been remapped.

**`RndTransformable::Load` — rev 7/8 constraint remap used a stale value (lane
A).** `auto _arg0 = mConstraint + kConstraintLocalRotate;` was computed
*before* `bs >> (int&)mConstraint`, so the remap derived from whatever the
object already held.

**`RndTransformable::Load` — rev 3-5 preserve-scale flag (lane A).**
`mPreserveScale = unkb0;` set the flag for any non-zero packed value; the
target emits `extrwi r10, r11, 1, 24`, i.e. `(packed & 0x80) != 0`.
Corroborated by the case labels below it coming in `0x04/0x84`, `0x08/0x88`,
`0x10/0x90`, `0x20/0xA0` pairs — `0x80` *is* the preserve-scale bit.

**`MoveDir::DrawShowing` — the outer guard was an entirely unrelated
expression (lane D).** It read `if (HashTable().Begin() != nullptr)`, emitting
a call to `KeylessHash<…>::FirstFrom`. The target makes no such call — it
walks the virtual-base table to reach `Hmx::Object::mDir` and compares against
`this`. The real condition is `Dir() != this`; the whole debug-collision block
was gated on a hash-table probe.

**`SkeletonChooser::ChoosePlayerSides` — inverted swap condition (lane E).**
`if (!xGtThresh) SwapPlayerSides()` should be `if (xGtThresh)`. As written the
condition was true for nearly every pose, so player sides would thrash every
frame with one player tracked and `sided_colors_locked` set.

**`HamDirector::DrawIconMan` — the texture was passed as the bool flag (lane
E).** `PoseIconMan(clip, poseBeat, NULL, (bool)tex, …)` vs target
`(clip, poseBeat, tex, true, …)`. On Expert the icon-man posed with no
texture.

**`RndMesh::SetVolume` — a diagnostic the original raised as a notification
was being silently logged (lane F).** `MILO_LOG("BSP tree outside bounding
box")` should be `MILO_NOTIFY`; the target calls `Debug::Notify`.

**`NgMat::RefreshState` — texture half-pixel offsets are numerically wrong,
and NOT yet fixed (lane C).** See the reciprocal-multiply section below.

### Non-behavioural but wrong-typed (recorded, not urgent)

- `Spotlight::BuildNGSheet` face indices are `short`; the target uses
  `unsigned short` (`extsh` vs `clrlwi`). Bit patterns land identically in
  `RndMesh::Face`'s `unsigned short` fields.
- `SkeletonChooser::DrawDebug`'s `Hmx::Rect` had all four arguments reversed —
  debug-only, but it drew a sliver in the wrong place.
- `HamCharacter::Poll` has a spurious `(int)` cast making the viseme loop
  signed.
- `DataNode.cpp` carried a `__declspec(noinline) _outline_Name` shim forcing a
  call the target does not make (removed).
- **Unverified lead worth chasing:** the target's `MakeString` for `texName`
  in `HamCharacter::Poll` instantiates as `char const[7]` against our
  `char const[256]`, which would imply the shipped code declared a 7-byte
  buffer that `strcpy`/`strcat` overflow. Lane F declined to act on a mangled
  name alone — correctly, but it should be checked.

## NEW LEVER #2 — reciprocal-multiply is source-steerable, and it changes results

Given **N ≥ 2 divisions by the same *named local***, MSVC emits one
`fdivs 1.0, d` plus N `fmuls`. Writing the divisor **expression inline** at
each site yields N real `fdivs`. The two forms produce **different float
values**. This is a correctness lever, not a cosmetic one.

- Fixed: `RndPostProc::LoadRev`'s bloom-color inverse
  (`float range = 4.0f - minVal;`) — now byte-exact.
- **Open: `NgMat::RefreshState`.** `0.5f/h`, `-0.5f/h`, `0.5f/w`, `-0.5f/w`
  over `int w, h` locals — the Xbox binary emits four `fdivs`, we emit two
  reciprocals and four `fmuls`. Last-bit-different half-pixel offsets feed
  **every NgMat texture sample**. Four source forms were tried and all
  regressed the match (89.1–90.3% vs 93.3%) because inlining kills the
  reciprocal but makes MSVC re-load the ints with `lwa` instead of reusing the
  guard's `lwz`. Real behavioural difference, not currently fixable from this
  source shape. **Grep the tree for the same `1/x`-named-local shape.**

## Further levers the lanes established

- **`static` vs external linkage on file-scope data is a register-allocation
  lever** — worth 13 points on `SkeletonChooser::DrawDebug`. With internal
  linkage MSVC knows a run of file-scope floats is contiguous and materialises
  *one* base pointer; with external linkage it emits a separate `lis` per
  symbol. Tell: target has N separate `lis` for adjacent statics where we have
  one `addi` base.
- **`ObjPtr<T>` in boolean/equality context emits SIGNED `cmpwi`/`cmpw`; a raw
  `T*` local copy emits UNSIGNED `cmplwi`/`cmplw`.** So a pointer
  signed/unsigned mismatch usually means we introduced a raw-pointer local the
  target doesn't have. Much more actionable than "spurious unsigned cast".
- **A dense `switch` over 0..N lowers to `mtctr` + `bdz` chain with one
  unsigned range check; an if/else-if chain gives signed `cmpwi` binary
  search.** Worth ~6 points on `RndTransformable::Load`.
- **Named local vs unnamed temporary.** `DataNode n1 = mValue1.Node();` pins a
  stack slot, so MSVC re-materialises `addi r3, r31, <slot>` at every use and
  constructs left-to-right; passing the same calls as *arguments* makes them
  rvalues — right-to-left evaluation, each use riding the NRVO'd return
  pointer. A helper without `__forceinline` gets outlined and costs 18 points,
  so the annotation is load-bearing.
- **A cached loop bound can cost a callee-saved register and shift the entire
  frame.** `size_t outputSize = outputs.size()` forced `r18..r31` where the
  target saved `r19..r31` — that one register *was* a +0x10 frame delta and a
  +0x18 offset shift on every local.
- **Destructor emission points are a readable scope map.** Five consecutive
  `stw <vptr>, 0x…(r31)` at a function's end means five objects held live too
  long; the target's per-object positions tell you where each block closes.
  +4.1% on `DoCrucible`.
- **Mid-chain bool materialisation means a named bool lvalue, not
  parentheses.** Parenthesising a 12-term `&&` does nothing (MSVC collapses it
  straight back); only `bool ok = …; ok = ok && …;` reproduces the target's
  periodic `clrlwi.` re-test.
- **`= 0` initializer vs full if/else** applies to *any* conditionally-assigned
  local, not just out-params — a constant initializer gets hoisted above the
  test, where the target emits an out-of-line `else` block. Paid off three
  times independently in lane A.

## A THIRD RESIDUAL BUCKET — stack-slot allocation

Lane A's `RndText::Load` did not fit the binary rule and cost two wasted
cycles. It *looked* statement-level (14 inserts / 15 deletes) but the clusters
were the **same instructions placed differently**; the real cause is MSVC
reusing stack slots across disjoint nested scopes where the target does not
(frame Δ −0x10, 6 target-only slots, 19 DIFFER).

**Added rule: if the insert and delete clusters contain the same instructions
and `stack-layout` shows many DIFFER/PERMUTED slots, it is slot allocation —
drop it.** `HamDirector::OnPopulateMoves` is the same story: its remaining 110
`diff_arg` rows are almost entirely stack offsets shifted by a 0xe0 frame
delta, because the target gives each of three `FileMerger::Merger` locals its
own 0x70 slot while MSVC packs ours onto one. Renaming them apart changed
nothing — the packing is lifetime-based.

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

### Aggregate across all seven lanes

**31 functions triaged, 26 investigated, 23 improved, 5 to byte-exact 100%,
11 live bugs.** Roughly **1 win per 1.3 functions** — far better than the
1-in-3 the sweep was budgeted at, and the bucket was labelled unfixable.

The rule performed as a **filter**, not as a success predictor:

- **It never misfired in the direction that costs budget.** Every
  expression-level residual that was dropped stayed a floor under test. Lane F
  spent two builds testing the parenthesized-nested-sum exception on
  `SetVolume`'s dot product — both groupings produced a byte-identical
  residual, so the exception did not apply. Lane E confirmed three separate
  floors (`data + (pos+4)` vs `(data+4) + pos` associativity produced a
  *byte-identical object*). Lane B spent one diagnostic pass and zero build
  cycles on `UpdateChase`.
- **Register swaps were symptoms in 100% of cases, across every lane, without
  exception.** `FlowIf::Activate` opened with a 45-instruction `r29↔r30` swap
  flagged "one liveness cause, start there" — it had no liveness cause at all
  and evaporated when the *last control-flow difference* was fixed.
  `RndTransformable::Load` had 54 register-swap instructions across 8 pairs and
  reached byte-exact 100% without a single register-motivated edit.
  `Sound::Handle`'s 40-instruction and `HamCharacter::Poll`'s 33-instruction
  cascades each collapsed to zero from one unrelated edit elsewhere.
- **But statement-level did not reliably convert.** Lane C was 2-for-5 with
  three *correct* diagnoses that measured worse (`ObjectDir::Save` −9%,
  `BuildNGSheet` −0.6%, `DrawShowing` −0.2%). The discriminator it found:
  **fixes that removed work paid; fixes that added a local and raised
  register/stack pressure regressed.**

### REVISED RULE — do not discard a correct edit because it measures worse

Three lanes independently hit this, so it is not noise:

- Lane B: the `OnPopulateMoves` loop-bound fix measured **95.4 → 95.2 applied
  first, and +1.1% applied after an unrelated register-pressure fix.** A strict
  "revert anything that lowers" policy would have thrown away a correct edit
  and the function would have stopped at 97.0 instead of 98.1.
- Lane E: the `Hmx::Rect` argument reversal — **a real bug fix** — dropped the
  score 84.6 → 83.8 on its own, even though it moved all four `stfs` onto the
  target's exact FPRs. Alignment noise masked it until the linkage fix landed
  on top and took it to 97.9. Kept on the evidence, not the number.
- Lane F: removing an inline wrapper that the home-area count correctly
  flagged made things *worse*, because the target wanted one level, not zero.

**Procedure:** when an edit you believe is *correct* measures worse, record the
delta, park it, and retry it after every subsequent landed change. Only call it
a genuine trade-off once it has failed to pay following your last successful
fix. And when the edit fixes a *behavioural* bug, keep it regardless of the
number — live bugs outrank match %.

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

## Landing procedure — commit messages are the deliverable

The per-commit messages from the lanes are **the most valuable artifact of this
sweep**, more than the match-percentage delta. Each one records what the
assembly said, which source construct produced it, and why the fix works —
that is the reasoning trace the training-data pipelines mine, and it cannot be
reconstructed from the final diff.

So the landing rule is not a style preference here, it is a data-integrity
requirement:

```bash
# per lane, from the main repo, after the branch is rebased onto main
git merge --no-ff sweep/regswap-<lane>
```

- **Never** `cherry-pick`, `squash`, `--ff-only`, `rebase -i` with fixups, or
  `commit --amend` on a landed lane commit. Every one of those destroys or
  merges away individual messages.
- `git rebase` onto main is fine — it rewrites hashes but preserves messages
  verbatim. If a rebase conflicts badly enough that it tempts you to squash,
  **merge the un-rebased branch directly instead**; a slightly messier graph is
  strictly better than a lost message.
- Write a real merge-commit message per lane: what the lane set out to do, what
  it found, and what it deliberately did not do. Never accept the default
  `Merge branch 'x'`.
- Negative results are first-class. A commit that says "tried X, regressed
  0.3%, reverted" is worth landing; do not drop those commits when tidying.

Lanes in flight: `sweep/regswap-statement` (coordinator), `sweep/regswap-a`
… `sweep/regswap-f`, and `sweep/regswap-bam` (single-function deep dive on
`BustAMovePanel::Poll`, writing to
`docs/investigations/2026-08-04-bustamovepanel-poll.md`).

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
