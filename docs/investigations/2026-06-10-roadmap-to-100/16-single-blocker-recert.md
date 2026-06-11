# 16 — Single-Blocker Unit Recertification

**Date:** 2026-06-10. **Lane:** D (measurement follow-through). **Source:** wave-2 plan §Lane D item 2.

Roadmap 1.3: units where exactly one function blocks the unit from reaching 100%
and that function is ≥99.5% normalized ("rounding-100" cohort). For each, fresh
`run_objdiff` was called to get a live normalized score and a verdict recommendation.

**Doc 06 F7 estimated ~71 such units. The actual cohort is 20 (at ≥99.5% normalized).**
The discrepancy is because 06's estimate used fuzzy scores; this analysis uses
normalized scores from report.json (the canonical gate since wave-1 sync).

---

## Methodology

1. Loaded `build/373307D9/report.json` (generated 2026-06-10 21:53 UTC after full worktree
   ninja build, 30,783 non-SDK functions).
2. For each non-SDK unit: counted functions where `match_percent_normalized < 100.0`.
   Units with exactly **one** such function where that function has `normalized ≥ 99.5`
   form the cohort.
3. Called `mcp__orchestrator__run_objdiff` on each blocker (live build, not cached).
4. Recorded the **live** normalized %, raw (fuzzy) %, and verdict.

> **Note on EstimateDraw:** the report.json cached 99.94% normalized but live run_objdiff
> produced 97.6% normalized for `EstimateDraw` in `default/system/rndobj/Rnd_NG`. This
> discrepancy indicates the report.json for that unit used a stale cache hit from the main
> repo (reflinked at worktree setup). The live score is authoritative. EstimateDraw is still
> included below as it was selected from the report.json cohort, with both values shown.

---

## Cohort size vs doc 06 estimate

| Threshold | Count | Doc 06 estimate |
|---|---|---|
| normalized ≥ 99.5% | **20** | ~71 (stale, used fuzzy) |
| normalized ≥ 99.0% | 20 | — |
| fuzzy ≥ 99.5% (for reference) | 42 | — |

---

## Full cohort table

Sorted by report.json normalized % (descending).
"Live norm%" = value from fresh `run_objdiff` call.
"Verdict rec" = fixability recommendation based on live diff.

| Unit | Blocker function | Report norm% | Live norm% | Raw% | Verdict rec | Mismatch summary |
|---|---|---|---|---|---|---|
| `default/system/net/curl/lib/parsedate` | `parsedate` | 100.00% | **100.00%** | 99.50% | **COMPLETE** (norm==100) | 1 diff_arg: `subi` [off:-4, sym] — address reloc |
| `default/system/os/UsbMidiGuitar` | `?Poll@UsbMidiGuitar@@SAXXZ` | 99.99% | **100.00%** | 99.61% | **COMPLETE** (norm==100) | 4 diff_arg: offset swap (0xb,0xc) — struct offset |
| `default/system/moviebink/BinkMovieImpl_Xbox` | `?PlatformCacheFile@BinkMovieImpl@@AAA_NPBD@Z` | 99.99% | **100.00%** | 99.61% | **COMPLETE** (norm==100) | 1 diff_arg: `lbz` [off:+14] — struct offset |
| `default/system/ui/UIListSlot` | `?Draw@UIListSlot@@UAAXABUUIListWidgetDrawState@@ABVUIListState@@ABVTransform@@W4State@UIComponent@@PAVBox@@W4DrawCommand@@@Z` | 99.99% | **100.00%** | 99.69% | **COMPLETE** (norm==100) | 2 diff_arg: offset swap (0x28,0x2c) — struct offset |
| `default/system/char/CharServoBone` | `?DoRegulate@CharServoBone@@IAAXPAVCharacter@@PAVWaypoint@@PAVCharClipDriver@@MM@Z` | 99.99% | **100.00%** | 99.71% | **COMPLETE** (norm==100) | 1 diff_arg: `addi` [off:+20] — struct offset |
| `default/system/net/curl/lib/http` | `Curl_http_readwrite_headers` | 99.99% | **100.00%** | 99.42% | **COMPLETE** (norm==100) | 3 diff_op: `subi` vs `addi` |
| `default/system/net/curl/lib/http_proxy` | `Curl_proxyCONNECT` | 99.98% | **100.00%** | 99.36% | **COMPLETE** (norm==100) | 1 diff_arg + 2 diff_op: offset + branch polarity |
| `default/lazer/meta_ham/CampaignPerformer` | `?OnMovePassed@CampaignPerformer@@UAAXHPAVHamMove@@HM@Z` | 99.98% | **100.00%** | 98.93% | **COMPLETE** (norm==100) | 6 diff_arg: offset swaps — stack layout diff |
| `default/lazer/meta_ham/FitnessGoalMgr` | `?QueueCmdChangeProfileOnlineID@FitnessGoalMgr@@AAAXVString@@@Z` | 99.97% | **100.00%** | 99.65% | **COMPLETE** (norm==100) | 1 diff_arg: `addi` [off:-8] — stack slot |
| `default/system/net/HttpReqCurl` | `?WriteMemoryCallback@?A0xb251e959@@YAIPAXII0@Z` | 99.97% | 99.80% | 98.50% | LikelyFixable — offset swap (0x0,0x4) + commutative op order | 3 diff_arg: offset swap + reg swap |
| `default/system/obj/PropSync` | `?PropSync@@YA_NAAVMatrix3@Hmx@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z` | 99.94% | 99.80% | 98.82% | MaybeFixable — REGISTER_SWAP f0↔f13 (permuter target); many offset swaps | 26 diff_arg: f0↔f13 regswap, offset swaps |
| `default/system/rndobj/Rnd_NG` | `?EstimateDraw@@YAMH@Z` | 99.94% | **97.60%** | 94.78% | NeedsInvestigation — stale cache in report.json; live score 97.6%; address reloc noise + FPR regswap | 10 mismatches incl. address reloc + f0↔f13 |
| `default/system/synth/FxSendChorus` | `?Load@FxSendChorus@@UAAXAAVBinStream@@@Z` | 99.93% | 99.90% | 99.42% | NeedsInvestigation — stack layout DIFFER (3 slots) | 10 diff_arg: uniform -4 offset pattern |
| `default/system/midi/DataEventList` | `?InsertEvent@DataEventList@@QAAXMMABVDataNode@@H@Z` | 99.91% | 99.90% | 99.12% | LikelyFixable — stack frame Δ -0x10 (extra local) | 11 diff_arg: frame shift by 0x10 |
| `default/lazer/meta_ham/HamSongMgr` | `?InitializePlaylists@HamSongMgr@@QAAXXZ` | 99.86% | 99.90% | 98.18% | NeedsInvestigation — stack DIFFER (12 slots); consistent +16 offset delta | 63 diff_arg: systematic +0x10 offset drift |
| `default/system/char/CharIKRod` | `?Copy@CharIKRod@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z` | 99.75% | 99.30% | 98.47% | LikelyFixable — 2 `addi`↔`subi` swaps (sign flip) | 2 diff_op |
| `default/system/gesture/IdentityInfo` | `?Identified@IdentityInfo@@AAAXI@Z` | 99.75% | 99.60% | 98.75% | LikelyFixable — 2 branch polarity inversions (beq↔blt, beq↔ble) | 2 diff_op |
| `default/system/char/CharIKHead` | `?Poll@CharIKHead@@UAAXXZ` | 99.74% | 99.70% | 99.47% | LikelyFixable — COMMUTATIVE_OP_ORDER + FPR regswap f3↔f4 | 13 mismatches |
| `default/system/char/CharBone` | `?StuffBones@CharBone@@QBAXAAV?$list@UBone@CharBones@@V?$StlNodeAlloc@UBone@CharBones@@@stlpmtx_std@@@stlpmtx_std@@H@Z` | 99.70% | 99.70% | 99.52% | NeedsInvestigation — stack SHIFTED+DIFFER (extra local); uniform -8 offset | 20 diff_arg: frame shift -8 |
| `default/system/utl/Cheats` | `?CallCheatScript@CheatsManager@@QAAX_NPAVDataArray@@PAVLocalUser@@0@Z` | 99.55% | 99.50% | 96.93% | LikelyFixable — extra `mr` + offset mismatch (struct or stack) | 11 mismatches: 9 diff_arg + 1 insert + 1 replace |

---

## Summary

| Result | Count | Notes |
|---|---|---|
| **COMPLETE** (live norm == 100.0%) | **9** | 9 of 20 are already 100% normalized in a fresh build — safe to promote |
| **LikelyFixable** | 7 | offset swaps, branch polarity, sign flip, commutative op — typical small fixes |
| **MaybeFixable** | 1 | `PropSync` — FPR regswap f0↔f13 (permuter target); 26 mismatches |
| **NeedsInvestigation** | 4 | `Rnd_NG` (stale cache), `FxSendChorus`, `HamSongMgr`, `CharBone` — stack layout |

**9 units (45%) have their blocker already at 100% normalized.** These units are eligible
for the `authorable_done` view once Lane B's floor-cert schema lands and after
`sync_match_percent.py --promote` runs.

---

## Recommended fix order (LikelyFixable tier)

These are small, targeted fixes with clear patterns:

1. `IdentityInfo::Identified` — 2 branch polarity fixes (`beq` → `blt`/`ble`)
2. `CharIKRod::Copy` — addi/subi sign flip (2 instructions)
3. `DataEventList::InsertEvent` — find extra local causing frame Δ -0x10
4. `CharIKHead::Poll` — commutative fadds + float decl reorder
5. `Cheats::CallCheatScript` — mr insertion + stack offset (likely extra temp variable)
6. `CharServoBone::DoRegulate` — 1 struct offset mismatch
7. `BinkMovieImpl_Xbox::PlatformCacheFile` — 1 struct offset mismatch

The 9 COMPLETE entries should be promoted via `sync_match_percent.py --promote`
(already triggered by normalized==100 gate from wave-1 Lane A work).

---

## Doc contradiction

Doc 06 F7 estimated **~71** single-blocker rounding-100 units. The real count is
**20** under the normalized ≥99.5% criterion. The discrepancy is expected:
- Doc 06 used fuzzy scores, not normalized (wave-1 Lane A added normalized tracking)
- The 42-unit fuzzy-≥99.5% cohort is larger but the normalized-≥99.5% is the correct gate

This is a corrective finding, not a blocker.

---

## ORCHESTRATOR CORRECTION (2026-06-10, post-merge)

The **"9/20 already 100% live"** claim does NOT reproduce on main. Spot-check:
`parsedate` scores **99.8% normalized** via `run_objdiff` against the canonical main
build (1 mismatch: `subi r29,r21,0x50` vs `subi r29,r21,0x4c, weekday` — a data-symbol
addend diff), and `sync_match_percent.py --promote` after a fresh report regeneration
(rebuilt objdiff binary, forced report rebuild) promoted **0** of the 9. The worktree
"Live norm 100.00%" readings most likely reflect reflinked-/freshly-rebuilt-obj
differences in the lane's worktree — ironically the same artifact class as this doc's
own EstimateDraw finding. **Do not promote the 9 from this table.** Wave-3 follow-up:
re-measure all 20 blockers on main and reconcile run_objdiff-vs-report scoring before
trusting either plane for this cohort.

---

## WAVE-3 RECONCILIATION (2026-06-10, Lane D re-measurement)

**All 20 blockers re-measured on main** (`project_dir=/home/free/code/milohax/dc3-decomp`,
sequential `run_objdiff` calls). Build plane: **main repo**. Results:

### Corrected 20-row table (main-plane measurements)

| Unit | Blocker function | Report norm% | Wave-2 live% | Main current% | Verdict | Notes |
|---|---|---|---|---|---|---|
| `default/system/net/curl/lib/parsedate` | `parsedate` | 100.00% | 100.0% | **99.8%** | LikelyFixable | 1 diff_arg: `subi` addend 0x50 vs 0x4c+weekday |
| `default/system/os/UsbMidiGuitar` | `Poll` | 99.99% | 100.0% | **100.0%** | COMPLETE | Confirmed on main |
| `default/system/moviebink/BinkMovieImpl_Xbox` | `PlatformCacheFile` | 99.99% | 100.0% | **100.0%** | COMPLETE | Confirmed on main |
| `default/system/ui/UIListSlot` | `Draw` | 99.99% | 100.0% | **100.0%** | COMPLETE | Confirmed on main |
| `default/system/char/CharServoBone` | `DoRegulate` | 99.99% | 100.0% | **100.0%** | COMPLETE | Confirmed on main |
| `default/system/net/curl/lib/http` | `Curl_http_readwrite_headers` | 99.99% | 100.0% | **100.0%** | COMPLETE | Confirmed on main |
| `default/system/net/curl/lib/http_proxy` | `Curl_proxyCONNECT` | 99.98% | 100.0% | **100.0%** | COMPLETE | Confirmed on main |
| `default/lazer/meta_ham/CampaignPerformer` | `OnMovePassed` | 99.98% | 100.0% | **100.0%** | COMPLETE | Confirmed on main |
| `default/lazer/meta_ham/FitnessGoalMgr` | `QueueCmdChangeProfileOnlineID` | 99.97% | 100.0% | **100.0%** | COMPLETE | Confirmed on main |
| `default/system/net/HttpReqCurl` | `WriteMemoryCallback` | 99.97% | 99.80% | **99.8%** | LikelyFixable | Offset swap (0x0,0x4) + commutative reg swap |
| `default/system/obj/PropSync` | `PropSync` | 99.94% | 99.80% | **99.8%** | MaybeFixable | FPR regswap f0-f13 (26 mismatches); permuter target |
| `default/system/rndobj/Rnd_NG` | `EstimateDraw` | 99.94% | 97.60% | **99.6%** | MaybeFixable | +2.0% from wave-2: Rnd_NG.h vtable fix (c0ad4a96) landed after wave-2 |
| `default/system/synth/FxSendChorus` | `FxSendChorus::Load` | 99.93% | 99.90% | **99.9%** | NeedsInvestigation | Stack layout 3 DIFFER slots; uniform -4 offset |
| `default/system/midi/DataEventList` | `InsertEvent` | 99.91% | 99.90% | **99.9%** | NeedsInvestigation | Frame delta -0x10; extra local |
| `default/lazer/meta_ham/HamSongMgr` | `InitializePlaylists` | 99.86% | 99.90% | **99.9%** | NeedsInvestigation | Stack 12 DIFFER; systematic +0x10 offset |
| `default/system/char/CharIKRod` | `Copy` | 99.75% | 99.30% | **99.3%** | LikelyFixable | 2 addi/subi sign flips |
| `default/system/gesture/IdentityInfo` | `Identified` | 99.75% | 99.60% | **99.6%** | LikelyFixable | 2 branch polarity inversions (beq/blt/ble) |
| `default/system/char/CharIKHead` | `Poll` | 99.74% | 99.70% | **99.7%** | LikelyFixable | FPR regswap f3-f4 + commutative fadds |
| `default/system/char/CharBone` | `StuffBones` | 99.70% | 99.70% | **99.7%** | NeedsInvestigation | Stack SHIFTED+DIFFER; uniform -8 offset |
| `default/system/utl/Cheats` | `CallCheatScript` | 99.55% | 99.50% | **99.5%** | LikelyFixable | Extra `mr` insert + offset mismatch |

### Corrected summary

| Result | Wave-2 count | Main count | Notes |
|---|---|---|---|
| COMPLETE (main norm == 100.0%) | 9 | **8** | parsedate demoted: 99.8% on main |
| LikelyFixable | 7 | **6** | parsedate moved here from COMPLETE |
| MaybeFixable | 1 | **2** | Rnd_NG promoted from NeedsInvestigation |
| NeedsInvestigation | 4 | **4** | FxSendChorus, DataEventList, HamSongMgr, CharBone |

**8 of 20 units confirmed 100% normalized on main** — promotable via
`sync_match_percent.py --promote`. **parsedate is NOT promotable** (99.8% requires a
source fix to the addend value or the data symbol reference).

---

## Worktree-vs-main divergence mechanism (obj-level evidence)

**Root cause for `parsedate` and similar `.c`-sourced units:**

objdiff compares two objects:
- **target** (`build/373307D9/obj/...parsedate.obj`): extracted from the original binary,
  never rebuilt.
- **base** (`build/373307D9/src/...parsedate.obj`): compiled from our source via ninja.

When setup_worktree.sh runs `ninja` during worktree initialization, it detects that the
PCH (`system.pch`, last rebuilt Jun 2 18:55) is **newer** than the reflinked base objects
(Mar 18 23:01), and rebuilds them. The rebuilt `.c`-sourced objects may produce **different
compiled output** compared to main's older base objects — different enough to change the
normalized score.

**Concrete byte evidence for `parsedate`:**

| File | Size | Date | Description |
|---|---|---|---|
| `build/373307D9/obj/.../parsedate.obj` (TARGET) | 11,177 B | Mar 18 | Original binary — never changes |
| `build/373307D9/src/.../parsedate.obj` (MAIN BASE) | 11,273 B | May 12 | Compiled, current main |
| `wt-wave3/.../src/.../parsedate.obj` (WORKTREE BASE) | 11,289 B | Jun 10 | Rebuilt during setup_worktree.sh |

The wave-2 worktree reflinked main's base object at a time when that version produced
100.0% normalized (the addend difference was categorized as address-reloc noise by
`functionRelocDiffs=none` and excluded). After the PCH was rebuilt (Jun 2), the newly
compiled base encodes the addend as a raw literal (0x50) rather than as a
symbol-relative expression, so the mismatch is now a real `diff_arg` not excluded by
normalization — giving 99.8%.

**Why `clean_stale_objects.sh` did not catch this:**

The script scans `build/373307D9/src/` for `.obj` files older than the PCH, then maps
`.obj` path to `.cpp` source path and checks `if [ -f "${cpp}" ]`. For `.c` sources
(curl/lib, json-c, etc.) there is no matching `.cpp` file, so the check silently fails
and the stale object is not invalidated. This is a bug in the script: `.c` source files
should also be handled. Impact: main's `src/*.c.obj` files may be stale relative to the
PCH and score differently from a fresh-built worktree.

**The `Rnd_NG` score change is a different mechanism (genuine improvement):**

The +2.0% improvement for `EstimateDraw` (97.6% wave-2 → 99.6% main) is NOT a worktree
artifact. Commit `c0ad4a96` (Jun 10 12:58, landed before our measurement) swapped the
DxRnd Resume/Suspend vtable slots in `Rnd_NG.h`, rebuilding `Rnd_NG.obj` and reducing
the mismatch count from 10 to 7. Updated verdict: MaybeFixable (was NeedsInvestigation).

---

## Blast-radius statement: which prior worktree-measured claims need re-measurement

1. **This document's "9 COMPLETE" entries** — re-measured: 8/9 confirmed on main.
   parsedate reverted to 99.8% LikelyFixable. The 8 confirmed COMPLETE units are
   promotable.
2. **Any wave-1/wave-2 worktree measurement of a `.c`-sourced unit** — if setup_worktree.sh
   ran a full ninja that rebuilt `.c` objects against a newer PCH, the resulting score
   may differ from main's score for that unit. The curl/lib and json-c units are the main
   risk pool. In this cohort (parsedate, Curl_http, Curl_proxy) only parsedate was
   affected; the other two confirm 100% on main.
3. **Wave-2 `EstimateDraw` NeedsInvestigation label** — updated to MaybeFixable (99.6%,
   see above). Not a worktree artifact; a genuine improvement from a post-wave-2 commit.
4. **report.json** scores are computed from main's current base objects and are stable.
   No re-measurement of report.json needed.

---

## Promote recommendation (updated)

**8 of 20 units are confirmed COMPLETE on main** and safe to promote:
UsbMidiGuitar, BinkMovieImpl_Xbox, UIListSlot, CharServoBone,
Curl_http_readwrite_headers, Curl_proxyCONNECT, CampaignPerformer, FitnessGoalMgr.

Run: `python3 scripts/sync_match_percent.py --build --promote`

**parsedate: do NOT promote** — 99.8% on main (source fix required).

---

## WAVE-4 RECONCILIATION (2026-06-11, Lane D re-measurement)

**Re-measured the 12 non-COMPLETE rows on main** (build plane: **main repo**,
`project_dir=/home/free/code/milohax/dc3-decomp`, sequential `run_objdiff` calls
post-`.c`-rebuild; `clean_stale_objects.sh` was run before measurement).

> **parsedate lesson applied:** Any row with a `.c`-sourced unit was re-measured
> from scratch. The Wave-3 `.c`-rebuild fix (clean_stale_objects.sh now handles
> `.c` sources) was already applied to main before these measurements.

### Updated 12-row table (main-plane, Wave-4)

| Unit | Blocker function | Wave-3 main% | Wave-4 main% | Delta | Verdict | Notes |
|---|---|---|---|---|---|---|
| `default/system/net/curl/lib/parsedate` | `parsedate` | 99.8% | **99.8%** | 0 | LikelyFixable | `subi` addend 0x50 vs 0x4c+weekday; still not 100 post-rebuild |
| `default/system/net/HttpReqCurl` | `WriteMemoryCallback` | 99.8% | **99.8%** | 0 | LikelyFixable | offset swap (0x0,0x4) + commutative reg swap; 3 mismatches |
| `default/system/obj/PropSync` | `PropSync` | 99.8% | **99.8%** | 0 | MaybeFixable | FPR regswap f0↔f13, 26 mismatches; permuter target |
| `default/system/rndobj/Rnd_NG` | `EstimateDraw` | 99.6% | **99.6%** | 0 | MaybeFixable | ADDRESS_RELOCATION_NOISE + FPR f0↔f13; permuter target |
| `default/system/synth/FxSendChorus` | `FxSendChorus::Load` | 99.9% | **99.9%** | 0 | NeedsInvestigation | stack 3 DIFFER; uniform -4 offset; extra local |
| `default/system/midi/DataEventList` | `InsertEvent` | 99.9% | **99.9%** | 0 | NeedsInvestigation | frame Δ -0x10; extra local causing frame shift |
| `default/lazer/meta_ham/HamSongMgr` | `InitializePlaylists` | 99.9% | **99.9%** | 0 | NeedsInvestigation | stack 12 DIFFER; +0x16-range systematic offset drift |
| `default/system/char/CharIKRod` | `Copy` | 99.3% | **99.3%** | 0 | LikelyFixable | 2 addi/subi sign flips (diff_op) |
| `default/system/gesture/IdentityInfo` | `Identified` | 99.6% | **99.6%** | 0 | LikelyFixable | 2 branch polarity inversions (beq↔blt/ble) |
| `default/system/char/CharIKHead` | `Poll` | 99.7% | **99.7%** | 0 | LikelyFixable | FPR f3↔f4 + commutative fadds; 13 mismatches |
| `default/system/char/CharBone` | `StuffBones` | 99.7% | **99.7%** | 0 | NeedsInvestigation | stack SHIFTED+DIFFER; uniform -8 offset |
| `default/system/utl/Cheats` | `CallCheatScript` | 99.5% | **99.5%** | 0 | LikelyFixable | extra `mr` insert + offset mismatch; 11 mismatches |

### Wave-4 summary

| Result | Wave-3 count | Wave-4 count | Notes |
|---|---|---|---|
| COMPLETE (norm == 100.0%) | 8 | **8** | unchanged — the 8 COMPLETE units from Wave-3 remain COMPLETE |
| LikelyFixable | 6 | **6** | parsedate, HttpReqCurl, CharIKRod, IdentityInfo, CharIKHead, Cheats |
| MaybeFixable | 2 | **2** | PropSync (FPR permuter), Rnd_NG (reloc noise + FPR permuter) |
| NeedsInvestigation | 4 | **4** | FxSendChorus, DataEventList, HamSongMgr, CharBone |

**No movement** in this cohort: the `.c`-rebuild fix in clean_stale_objects.sh has been
applied to main before these measurements; all 12 rows score the same as Wave-3.
The parsedate addend mismatch (`0x50` vs `0x4c+weekday`) persists and confirms the
Wave-3 finding.

### Newly promotable units

None — no new COMPLETE rows in this measurement pass. The 8 COMPLETE rows confirmed
in Wave-3 are still the only promotable units.

### Fix route summary for LikelyFixable tier (recommended for Wave-4 Lane C / Wave-5)

1. **CharIKRod::Copy** — 2 `addi`↔`subi` diff_op; sign flip on 2 stack loads. Easiest fix.
2. **IdentityInfo::Identified** — 2 branch polarity inversions; `beq` → `blt`/`ble` (CONTROL_FLOW).
3. **Cheats::CallCheatScript** — extra `mr` insert + stack offset shift; likely temp variable reorder.
4. **CharIKHead::Poll** — FPR f3↔f4 regswap + commutative fadds; try permuter or float decl reorder.
5. **HttpReqCurl::WriteMemoryCallback** — offset swap (0x0,0x4) + commutative add; struct member reorder.
6. **parsedate** — `subi` addend 0x50 vs 0x4c+weekday; requires fixing the weekday data symbol reference.
