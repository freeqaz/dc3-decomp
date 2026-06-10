# 2026-06-10 — ASM-Archaeology Waves 1–5 (orchestrated)

> Waves 2–4 appended below; wave 5 (permres retry, MakeString mechanism,
> og-port 4, platform cluster) in flight at time of writing.

Follow-on to [2026-06-09-large-function-asm-archaeology.md](2026-06-09-large-function-asm-archaeology.md).
First fully-orchestrated run of the playbook: per-cluster **intel → Opus implementer →
verifier** pipelines in `setup_worktree.sh` worktrees (max 6 concurrent), plus one
strategy-research agent. 16 agents, ~48 min wall clock, ~2.6M subagent tokens.
All 5 clusters harvested onto main, one commit each.

## Headline results (commits `6eeba04f`, `85f29b72`, `5bca1a17`, `03c1fdbf`, `0e6ab068`)

| Function | Before | After | Notes |
|---|---|---|---|
| `SpotlightDrawer::DrawLight` | 58.5% | **98.1%** | 6 real bugs (see commit) |
| `ArcDetector::IsLockedIn` | 48.4% | **100%** | |
| `ArcDetector::ArcDetector` | 86.3% | **100%** | uncredited side effect |
| `RndAmbientOcclusion::BlendVert` | 56.0% | **84.5%** | grouped-loads lever confirmed |
| `ArcDetector::Draw` | 66.2% | **84.9%** | path was drawn reversed, as dots |
| `ArcDetector::PrintJointPath` | 72.3% | **95.5%** | |
| `RndAmbientOcclusion::BuildObjectLists` | 77.1% | **99.2%** | real OOB write fixed (`unique_copy` → `unique`) |
| `RndAmbientOcclusion::CalculateAO` | 84.2% | **92.6%** | |
| `kdTree<Triangle>::Intersect` | 78.8% | **86.5%** | |
| `RndAmbientOcclusion::BurnTransform` | 43.1% | **61.6%** | static fn, found via asm grep |
| `RndRibbon::UpdateChase` | 72.2% | **86.2%** | twin-verified with HamRibbon |
| `HamRibbon::UpdateChase` | 74.1% | **85.6%** | |
| `ArcDetector::GetPathLength` | 65.5% | **81.9%** | |
| `ArcDetector::GetPathError` | 79.1% | **89.7%** | |
| `ArcDetector::IsPathAcceptable` | 47.8% | **78.0%** | acceptance logic was inverted |
| `ArcDetector::DrawPath` | 0% (sig mismatch) | **89.3%** | by-value list param fixes mangling |
| `RndText::OnComputeCharWidths` | 68.1% | **78.5%** | |
| `RndText::FitTextScroll` | 72.7% | **82.5%** | dropped CharAdvance vcall restored |
| `NgSpotlightDrawer::RenderConeDefs` | 66.3% | **80.0%** | |
| `XfmOnCircleEdge` | 73.0% | **77.3%** | cross-product sign bug |
| `ArcDetector::Update` | 63.0% | **74.3%** | |
| `ArcDetector::UpdateOverlay` | 62.9% | **71.3%** | |
| `ArcDetector::GetSwipeAmount` | 79.1% | **84.3%** | |
| `RndAmbientOcclusion::SmoothResults` | 63.1% | **67.4%** | |

Unit-level: AmbientOcclusion 89.3→91.5%, rndobj/Ribbon 93.2→95.9%, SpotlightDrawer
93.3→95.4%, gesture +0.51%. ~25 real logic bugs fixed (full lists in the 5 commit
messages) — the ArcDetector ones matter for **gesture recognition correctness**
(lock-in thresholds 2.5x too strict, path-acceptance inverted, z-error weight 2x off)
and DrawLight's for **spotlight rendering** on the native port.

### Native-port follow-up flags
- `DrawLight` tail now does real view-frustum light-can culling and stores
  `mEnvMesh = (RndMesh*)RndEnviron::sCurrent` — worth a visual spotlight check.
- ArcDetector gesture thresholds changed semantics — Kinect swipe detection behavior
  will differ (correctly now).

## New levers (mined from live residuals by the research agent — add to every wave-2 prompt)

| Lever | Tell | Fix |
|---|---|---|
| **Split-FMA (fp-contract)** | ours fmadds/fnmsubs where target has fmuls+fadds/fsubs (or reverse); secondary tell: prologue FPR-save divergence (stfd f30/f31 inline vs `bl __savefpr_27`) | split `x=a*b+c` into `t=a*b; x=t+c;` (or merge to induce fusion); evidence: SphereConeTest, LoopVizCallback::UpdateOverlay, HamSkeletonConverter::Set |
| **Cached stack-address pointer** | diagnose shows impossible r1↔rN "swaps" (TGT `addi r6,r1,0x70` vs SRC `mr r5,r30`) | delete the local that caches `&stackStruct`; write the `&local` expression at each callsite; evidence: HamSkeletonConverter::Set (37 instrs) |
| **Literal/global rematerialization** | TGT re-emits lis/addi with string reloc per use; SRC `mr r4,r29` (cached) | remove `const char* s = "lit"` locals; repeat the literal per call (hypothesis grade — validate on Poll/MoveDir residue) |
| **Global-member reload** | TGT `lwz r5,0x3c(rN)` w/ global reloc (TheTaskMgr) where SRC reuses a value | call the accessor inline per use (global analog of the Tessellate Faces() lever) |
| **Static-array pointer-walk** | TGT `addi rX,rBase,+stride` chains carrying a static-data reloc; SRC index-multiply loop | walk a pointer over the static table instead of indexing; evidence: SkeletonViz::DrawJoints |

## Refuted / floor registry (do not retry)

- **BuildNGCone matrix slots**: MSVC assigns stack slots by first-USE order, not
  declaration order — decl reorder can't move Matrix3 slots. Spotlight `Build*` family
  remaining gaps are scheduling/slot floors after first pass (BuildBeam fsel was never
  the gap; unsigned-short face indices and hoisted vertex index all regressed).
- **Ribbon ConstructMesh pair** (53.3/73.3): target's persistent-zero callee-saved reg
  is an optimizer regalloc decision; the single missing `mr` cascades into a 246-replace
  shift. MSVC lifetime/slot-coalescing floor.
- **`int` instead of `bool`** for a flag tested with clrlwi: catastrophic (84.7→54.6 on
  kdTree::Intersect). The BOOL_MASK clrlwi is a floor; leave bools alone.
- **MakeWorldSphere loop rotation**: target re-reads `end()` from memory per iteration
  via bottom-rotated loop — not reachable from source (FOREACH and hand-loop identical).
- **DrawShowing color save**: target's 4-word GPR memcpy-style color copy not
  reproducible; 3-float lfs/stfs scores best.
- ArcDetector residuals: static-anchor reloc choice (GetSwipeAmount), one-off-by-one
  GPR cascades (IsPathAcceptable/Update/Draw), volatile-FPR f12↔f13 (GetPathError) —
  all RarelyHandFixable regalloc.
- BlendVert at 84.5%: `Add(v2.norm,out.norm,out.norm)` beats `operator+=` (store order
  dominates operand order). Tessellate stayed 87.3 after BlendVert — its residual was
  NOT purely the BlendVert cascade.

## Wave-2 plan (from the strategy-research agent; diagnose-verified unless noted)

**Lane 1 — asm-archaeology fan-out** (next session, same pipeline, prompts updated with
the 5 new levers):
1. **rndobj/Utl mesh-utility TU** — 12 fns ~3.3KB (ResetNormals 67.5, TessellateMesh,
   MakeNormals, BuildVisit/BuildFromBSP + math/Geo MakeBSPTree). Same idioms as the
   Tessellate win; diagnose shows structural clusters, not regalloc.
2. **CheatsManager::CallCheatScript** (55.4) — 88-instruction missing block + missing
   string args; near-certain behavioral gap.
3. **HamSkeletonConverter::Set** (73.6) — 57-instr missing block, call_count-DIVERGENT
   (real native-port bug) + the cached-stack-address lever showcase.
4. **gesture/SkeletonViz** (DrawJoints 68.9) — pointer-walk lever + overlay idioms.
5. **rndobj/Lit_NG** (SphereConeTest 50.1) — the split-FMA lever showcase.
   Watch: FP functions historically false-positive; this one has a falsifiable lever.
6. (medium confidence, gate on diagnose) RndSpline Sync*, RndLine::UpdateLine pair,
   LoopVizCallback::UpdateOverlay@GamePanel (81.6, MoveDir sibling).

**Lane 2 — og-dc3 port wave** (cheap, proven ~85% hit rate; 190 og=100% fns still open,
~6.3KB): headline **SongSequence::DoNext** (84.7, og diff exposes a real UpdateEraSong
arg bug — we pass mSongLongName twice), the 8-fn **flow/*** cluster (~1.1KB),
CursorPanel::Poll, Sound::SynthPoll, char/* singles (max one char unit per wave).
Port verbatim → measure → rename preserving declaration order → re-measure.

**Lane 3 — background**: permuter only on post-agent 90–95% residue
(BustAMovePanel::Poll 92.5 qualifies); regenerate og-dc3's report.json (currently has
no per-function measures) to refresh port mining; pairing-artifact cleanup (8 fns/9.3KB
anon-namespace-hash, e.g. SetUpWorkingMat reads 0% though correct).

## Process learnings (now standing policy)

- **Diagnose-first gating**: AT_LIMIT labels in the 40–85 band are categorically
  unreliable (every spot-checked one showed structural insert/delete clusters). Only
  permuter-evidenced CONFIRMED notes count as floors (Invert@math/mtx,
  RhythmBattle::OnBeat, SHA1).
- **Same-TU callees before flagship residue** — but note BlendVert→Tessellate gave 0:
  verify the cascade claim per case before banking on it.
- **Unicorn behavioral divergence (call_count/call_arg/return_value/object_memory) as
  target tiebreaker** — double credit: bytes + native-port correctness.
- The intel→implement→verify worktree pipeline works as designed; verifiers caught
  zero fabrications this wave (the no-hacks prompt language is doing its job).
- De-noise renames are safe if re-measured (Ribbon `auto&` alias → named ref, no drop).

## Worktrees (left intact for residue passes)

`/home/free/code/milohax/wt/arch-{ao,arc,spot,ribbon,text}` — branches `arch-*-w1`,
all based on `7a30ad8c`. Wave-2: `wt/arch2-{utl,cheat,hamskel,litng,ogport}`.
Delete with `git worktree remove` when stale.

---

# Wave 2 results (commits `42333189` … `f2d289d9`)

Same pipeline: 4 archaeology clusters + og-port lane + tooling agent. 16 agents,
~60 min. DB sync: 28 improvements, 0 regressions, 7 promotions.

| Function | Before | After | Notes |
|---|---|---|---|
| `CheatsManager::CallCheatScript` | 55.4% | **99.5%** | missing 88-instr block reconstructed; 4 real bugs |
| `UtilDrawSphere` | 60.2% | **100%** | RndMat* param was silently ignored |
| `UIListMeshElement::Draw` | 81.5% | **100%** | og port |
| `FlowQueueable::Activate` | 83.9% | **100%** | og port; 3 real bugs (queue order reversed!) |
| `FlowSetProperty::Load` | 94.2% | **100%** | + header `u8`→`bool` |
| `Rand::Float` / `Int` / `FastInt` | 66/84/0 | **100%** ×3 | header-inline COMDAT (target provably did) |
| `SongSequence::DoNext` | 84.2% | **99.3%** | UpdateEraSong arg bug + 4 more |
| `DrivenPropertyEntry::Load` | 86.5% | **99.5%** | Dir()→GetOwnerFlow() bug |
| `Rand::Gaussian` | 55.1% | **97.9%** | og port |
| `FlowSetProperty::Execute` | 91.2% | **97.5%** | 3 real bugs (ChildFinished, rate remap) |
| `FlowMultiSetProperty::Load` | 86.3% | 95.9% | MI-downcast residual = ceiling |
| `HamSkeletonConverter::Set` | 73.6% | **92.7%** | 2 real Kinect bugs (camera basis, pelvis scale) |
| `Set (cont'd) — unit` | 94.0% | 97.4% | |
| `MakeNormals` | 79.1% | 89.8% | |
| `Intersect(Segment,BSPNode)` | 71.9% | 87.5% | **5 real bugs** (clip recursion was broken) |
| `FlowSwitchCase::IsValidCase` | 71.9% | 85.0% | transition operand pairing inverted |
| `Sound::SynthPoll` | 64.9% | 79.3% | iterator-after-erase = og/target shape (native flag) |
| `ResetNormals` | 67.5% | 77.9% | |
| `BuildVisit` | 64.3% | 77.4% | |
| `CacheResource` | 56.9% | 71.0% | |
| `SetColorWriteMask` | 57.2% | 63.1% | |

**Rejected at harvest:** og's `MemTrackInit` loop (og's own OOB byte-store bug,
verifier-proven against target asm — kept our pointer-store loop).

### Lever calibration from wave 2 (important corrections)

- **Split-FMA REFUTED as a direct source lever** (SphereConeTest, full structural
  rewrite tried): MSVC fuses mul+add unconditionally when multiplicands are
  register-resident; the target's unfused form is a byproduct of *memory round-trips*
  in its value lifetimes. Reinterpreted: the tell marks a residency difference — look
  for a TGT_ONLY spill slot near the FMA; no slot ⇒ floor.
- **Inline-vs-cached accessor is PER-TU**: inlining `Verts()`/`GetGeomOwner()` per use
  EXPLODED on rndobj/Utl (−8 to −16%) — the opposite of the AmbientOcclusion direction.
  A/B one site before committing.
- New floors: MSVC EH-frame DataNode temp ordering (try/catch scopes), early-return
  block placement, optimizer-coalesced redundant copies.

---

# Wave 3 results (commits `081c27c1` … `328e88f6`)

6 lanes, 18 agents, ~41 min. All harvestable.

| Function | Before | After | Notes |
|---|---|---|---|
| `DrawPlayClip@MoveDir` | 81.7% | **100%** | rect width 0.9f bug + disguised int param |
| `GamePanel::DeJitter` | 87.9% | **100%** | |
| `SynthPreInit` | 85.9% | **100%** | og port (+Synth.h friend) |
| `RndMesh::DeleteBones` | 57.7% | **99.3%** | real `dynamic_cast<RndDir*>` root-walk bug ×4 sites |
| `RndParticleSys::InitParticle` | 94.5% | **98.9%** | missing shrink/grow f11 recompute (particle motion) |
| `BustAMovePanel::Poll` | 92.5% | **97.7%** | literal_remat lever CONFIRMED as a fix (+5.2) |
| `FlowMathOp::Apply` | 87.8% | **97.4%** | missing MILO_TRY/CATCH on script case |
| `SkeletonViz::DrawJoints` | 68.7% | **94.1%** | static_ptr_walk lever CONFIRMED; 3 behavioral fixes |
| `FlowSequence::Activate` | 87.2% | 93.3% | NOTIFY %s used wrong name source |
| `LoopVizCallback::UpdateOverlay` | 81.6% | **92.9%** | loop-viz colors all wrong + endMarker timer bug |
| `RndSpline::SyncPristineCtrlPoints` | 74.3% | 92.2% | |
| `Rnd::DrawTimers` | 90.9% | 92.4% | unnamed-temp Symbol (scanner hit was mislabeled) |
| `RndMesh::InstanceGeomOwnerBones` | 73.6% | 88.1% | same RndDir root-walk bug |
| `MoveDir::UpdateOverlay` | 86.1% | 87.2% | |
| `DxCubeTex::Sync` | 58.5% | **79.2%** | **mip upload missing the whole Lock/Tile/Unlock cycle** + Order()/Palette() field bugs |
| `CursorPanel::Poll` | 78.3% | 78.3% | og diverges; skipped |

### Scanner calibration (the levermix lane's purpose)

| Scanner hit | Verdict |
|---|---|
| static_ptr_walk @ DrawJoints | **TRUE positive** — the lever was the fix (+25.4 with it) |
| literal_remat @ Poll | **TRUE positive** — re-materializing literals landed +5.2 |
| global_reload @ MoveDir idx 130 | partially confirmed (small wins inside +1.1) |
| cached_stack_addr @ DrawTimers | MISLABELED — r1↔r31 was whole-frame r31-base mode, not a deletable local; a different (unnamed-temp) lever was found there |
| split_fma @ GrowToContain | FALSE positive — formal gate (no TGT_ONLY spill slot) confirms backend floor |
| static_ptr_walk @ ThreadMemStack | FALSE positive — static-anchor addressing, walk already matches |

**Scanner roadmap:** add the TGT_ONLY-spill-slot gate to split_fma; distinguish
frame-base-mode (whole-function subi r31,r1) from deletable cached-stack-addr;
the two confirmed-fix levers (ptr-walk, literal-remat) deserve priority weighting.

### Known unlanded real bugs (need a dedicated pass — landing regresses match)

- `VelocityBuffer::Draw`: `cam != nullptr & cam == mCam` uses bitwise `&`
  (target short-circuits); `AdvanceFrame(cam)` should be unconditional before
  `PreDepthTexture()`. Landing either regresses 74.8→47.4 via regalloc cascade.
  Recorded in commit `90697f58`.
- `SkeletonViz::DrawJoints` first-iteration uninitialized `shadedColor.alpha`
  is binary-faithful (commented in source) — HX_NATIVE init only if native misrenders.

### New floors (wave 3)

- Counted-loop-vs-pointer (compile-time-constant trip count → downcount li/subic./bne).
- Whole-frame r31-base mode (subi r31,r1,N): no source trigger.
- MSVC loop-invariant hoist of vector loads (lvx128) above loops.
- Position-sum CSE into callee-saved FPR vs inline fmadds recompute.
- Virtual-dispatch global+vtable hoist before bl (TheShaderMgr).
- `__forceinline` to force-match MSVC inlining: catastrophic, never.

---

# Wave 4 results (commits `587de53d` … `a7c335c4`)

6 lanes (permres agent crashed pre-worktree — retried in wave 5), 15 agents, ~44 min.
DB sync: 31 improvements, 1 licensed regression, **18 promotions to COMPLETE**.

| Function | Before | After | Notes |
|---|---|---|---|
| `DxShader::Compile` | 58.4% | **82.4%** | **out-param signature bug: shader bytecode silently dropped + whole error path DCE'd** |
| `QueryXSocialCapabilities` + 11 more og-ports | 68.9–94.5% | **100%** ×12 | incl. FxSend360::Cleanup + ChatReceiver (stubs→100), StorePanel fabricated-vtable hack removed |
| `CursorPanel::Poll` | 78.3% | **94.6%** | **crown flicker bug** (always-true reset condition) |
| `RndRenderState::Init` (+2 authored fns) | 84.2% | **94.7%** | RenderState unit +22% |
| `CacheXbox::ThreadWrite` | 89.5% | 93.0% | 2 I/O bugs (ungated CreateFileA; err<2 routing) |
| `SkeletonChooser::ShouldWaitForRecovery` | 92.2% | 94.1% | recon's logic-bug hypothesis refuted — shape only |
| `CreateBackBuffers` | 91.1% | 92.0% | **EDRAM placement bug** (depth/color sizes swapped) |
| `ScaleAddEq(Matrix3)` | (mis-homed) | **100%** | re-homed to rnddx9/Mesh TU, vec.cpp keeps HX_NATIVE copy |
| `VelocityBuffer::Draw` | 74.8% | 74.2% | **licensed trade**: AdvanceFrame made unconditional (real fix); `&`→`&&` proven behaviorally neutral AND a −27pp match trap |

### Wave-4 calibration

- **Header TYPE bugs are a recurring lever**: `int` members that are really pointers
  (cmpwi vs cmplwi tell; `MakeString<void*>` instantiation proof) — PlatformMgr,
  FxSend360, ChatReceiver signature.
- **MakeString mixed inline/out-of-line discovered**: target inlines single-arg
  MakeString at ~480 sites but `bl`s out-of-line at ~11–20 (HxGuid::Generate 89.8→100
  behind it). A global header toggle = 488 regressions (rejected at harvest by the
  verifier's decomposition test). Wave 5 hunts the overload-resolution mechanism.
- The bughunt lane's full patch was split: harvested only the function-local fixes.
  Verifier decomposition testing (transient revert + rebuild + re-measure) is now a
  proven harvest technique for cross-cutting header edits.
- New floors: STLport vector-init address-temp pattern; MSVC tail-merge of dual
  Fail tails (D3DFormatForBitmap MILO_FAIL is behaviorally right but −13pp — recorded
  unlanded); MultiMesh combined-static-table (0x70 alignment gap unrepresentable).

### Tooling landed (decomp-synth `243e2e3`)

`decomp_synth/lever_scan.py` — instruction-level lever-tell scanner, 5 levers,
validated on all 7 wave-1 known positives, commutative-noise-proof by construction,
false-positive class (local-static address materialization) excluded. Full-frontier
scan: `docs/lever_scan_wave3.{json,md}` in decomp-synth. One command, ~1.4s warm:
`cd dc3-decomp && PYTHONPATH=../decomp-synth python3 -m decomp_synth.lever_scan`.
Gotcha documented: don't copy fpr_scan's `get_cache_db_path().parent` cache-dir trap.
