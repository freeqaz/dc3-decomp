# 2026-06-10 — ASM-Archaeology Wave 1 (orchestrated)

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
all based on `7a30ad8c`. Delete with `git worktree remove` when stale.
