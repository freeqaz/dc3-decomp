# Dispatch + data-symbol scanner re-run, 2026-08-18

Periodic re-run of the two standing "bugs the metric hides" scanners (last run 2026-06-16,
waves 10-12), on base `ccd4c8036`, worktree plane. Fixes landed on this branch; this doc
preserves the **negative result and the residual worklist**, which otherwise lived only in
a session transcript.

## vtable_dispatch_scan.py: CLEAN NEGATIVE — the wrong-target-dispatch class looks exhausted

`--all` over 1,642 raw<norm functions: 284 hits (31 strong / 63 medium / 190 weak).
Filtered to alignment-certain rows (every neighbouring instruction matches): 11 survive,
and **none is a wrong vtable slot**:

- `UIListSlot::Draw` 99.9%, `RndMesh::MakeWorldSphere` 99.8% — OFFSET_SWAP on struct
  fields (`mDrawOrder`, `mVerts`), not dispatch.
- `Voice::Init` 99.5% "strong" — same two instructions in swapped order; scheduling.
- `RndTexRenderer::DrawToTexture` ×2 — slots `0xc`/`0x18` match both sides; only receiver
  *addressing* differs (`this+0x84` vs `&mDrawable + 0xc` — same field, folded
  displacement vs held pointer).
- `HolmesClient::DumpHolmesLog`, `TexBlender::DrawBlendList`,
  `HamNavList::LinkRibbonDrawState`, `FreestyleMoveRecorder`, `BinkIntegration`,
  `HamCamShot::SetPreFrame` — member-offset/arg loads or sub-95% alignment noise.

Prior-known skips re-confirmed: the 3 `CalcShaderOpts`, `Rnd::DrawPreClear`,
`DingoJob::SendCallback`, `ChunkStream::Eof`, `StorePanel` (wave-16 floors),
`BinkMovieImpl` (certified floor).

## data_symbol_scan.py: the productive plane — missing-override proof method

Candidate rows become provable by cross-checking `orig/373307D9/ham_xbox_r.map`: if the
target's slot address hosts `Owner::Method` and our slot points at `Base::Method`, the
original has an override we never declared. ICF representatives disassembled:
**0x82AEAE70 = `li r3,0; blr`** (return false/NULL group), **0x82E2AB00 = `li r3,1; blr`**
(return true group). 21 overrides/RTTI/return-type fixes landed on this branch (see
commits). Candidate-bug rows: 144 → 110.

Note: the scanner had a thread race (map oracle published empty then filled inside the
worker pool) making June's candidate counts partly nondeterministic; fixed in
`0e91b968b`. Coverage limit: 6,194 of 18,549 data symbols fail with "Symbol not found in
target" — dtk's split doesn't reconstruct every RTTI symbol.

## Residual worklist (documented, not guessed)

**36 missing-override rows need real function bodies** (non-trivial ICF addresses) —
reference-less reconstruction, the wave-16 boundary. Several are `meta_ham` game classes
where `../og-dc3-decomp` is the right source, not synthesis:

- `*HeaderNode` family: `GetItemCount` (= `lwz r3,0x58(r3)`), `Select`,
  `UpdateItemCount`, `OnUnHighlight` ×4 classes
- `SongSortNode`/`MQSongSortNode`/`PlaylistSortNode::GetAlbumArtPath`
- `SongSort`/`PlaylistSort`/`FitnessCalorieSort::Handle`
- `RndPollAnim::{StartAnim,EndAnim,SetFrame}` (virtual-base adjustor thunks)
- `HighFiveGestureFilter`/`SigninScreen`/`FitnessFilterObj::SyncProperty`
- `CacheIDXbox::GetDeviceID`, `SampleInst360::EndLoopImpl`,
  panel `Enter`/`Exiting`/`FinishLoad`

**Documented floors (deliberately skipped, wide blast radius for zero layout change):**

- ~20 `??_R1` base-class-descriptor access-attribute divergences: `ObjPtrVec<T>` derives
  *privately* from `ObjRefOwner` in the original (20 instantiations); likewise
  `CacheXbox`/`ThreadCallback`, `XboxPurchaser` (`EA`=0x40 public vs `EN`/`EJ` private).
- `??_R2` virtual-base ordering sets (`HamNavList`, `UIComponent`, `UILabel`,
  `MeterDisplay`, `LabelShrinkWrapper`, `MiniLeaderboardDisplay`, `AppLabel`,
  `HamProfile`): target entries carry vdisp/`FA`=0x50 `BCD_VBOFCONTOBJ` where ours are
  direct bases; converting to virtual inheritance would move every offset.

## Open lead

`Hmx::Object::RefOwner`: the original's body at 0x823E3B70 is a bare `blr` — it falls off
the end without setting `r3`; ours is `return const_cast<Object *>(this);`
(Object.h:1264). (Commit `45d64fbe4`'s message mis-states ours as `return nullptr` —
the code was never changed; the PhysicsManager::RefOwner override fix is independently
map-proven.) The target/our semantic relationship here deserves its own look.
