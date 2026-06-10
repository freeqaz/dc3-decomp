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
