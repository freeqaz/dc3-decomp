# Wave 24 Results — Native-port correctness pivot: FileMerger infinite-loop fix + gap-inventory refresh

**Date:** 2026-06-22 · **Landed through main `92b9ae13` (fix) + follow-up (tests/docs)** · all-Opus, agentRetry, adversarially verified, native-gated.

Goal-loop wave. After the PPC frontier was confirmed exhausted for the game+engine
scope (the concurrent **clrrwi sweep** finished at `07c6f7d7` — "NARROW lever,
5/241 signature, 1/241 actionable"), this wave pivoted to the genuinely-open,
non-colliding lever: **native-port runtime correctness** (the user's standing
"fix decomp gaps when the native port hits bugs" preference, IK explicitly
excluded this turn).

## Why this lever (scouting, not spot-check)
- **clrrwi sweep owns the entire sub-100 PPC frontier** (241 near-misses across
  every game/engine unit, 6 worktrees) → duplicating it would collide. It then
  finished and confirmed the lever is narrow.
- **Unicorn logic-divergence DB is empty** — the 102 divergences are all
  emulation artifacts (`cap_exhausted`/`call_count`/`orig_error`), no source bugs.
- → Native-port correctness is orthogonal to PPC matching and non-colliding.

## Lane A — FileMerger::Merger::Clear sync-reload infinite loop (REAL BUG, fixed)
**Root cause:** `FileMerger::Merger::Clear`'s native drain `while(!mLoadedObjects.empty()){ delete front(); }`
relies on `delete front` auto-erasing the front node via `ObjPtrList::Node::NullifyObj`
(`Object.h:531`), which only unlinks when `Mode()==kObjListNoNull && !gInReplaceList`.
During a synchronous outfit reload (`HamCharacter::StartLoad(false)`), `Clear` runs
inside a `ReplaceList` ring-walk (`gInReplaceList==true`), so the node erase is
suppressed (`ObjPtr_p.h:465` "suppressed erase during ReplaceList") — `front()`
never advances, the loop spins forever (CPU-bound).

**Fix (`92b9ae13`):** Narrowest correct `#ifdef HX_NATIVE`-guarded drain — pop the
front node out of the list **before** deleting the object (`pop_front()` detaches the
Node from both the list and the object's ObjRef ring while the object is still alive),
so loop progress depends on `Unlink`/`mSize--` rather than the suppressible
auto-erase. Mirrors the established `Faders.cpp` drain idiom. The `#else` PPC branch
is byte-identical → **zero PPC impact** (objdiff `?Clear@Merger@FileMerger@@QAAX_N@Z`
unchanged at 98.4%). Root fix (scoping `gInReplaceList`) deliberately NOT taken —
that guard is load-bearing engine-wide against ring-walk heap corruption.

**Result:** 6 outfit-reload tests **hang → 6/6 PASS**; full suite green; the
`SKIP_IF_OUTFIT_RELOAD_BROKEN` gate neutralized so they now run **by default**
(skip gracefully without assets). Adversarial verifier: **CONFIRMED** — memory-safe
(each object freed once, Node detached before free), no double-free/UAF/leak, PPC
unchanged. Tests re-enabled: MainCharacterFileMergerConfiguresOutfitAndVisemeByDefault,
BackupOutfitBonePointersMatchServoDirectory, BackupOutfitPreservesArmPollableInventory,
SkinnedMeshesCarryNontrivialForeTwistWeights, InspectForearmVertexBoneAssignments,
CpuSkinForearmVertexFromCompressedMesh.

## Lane B — Native gap-inventory refresh (read-only)
`STUB_BURNDOWN.md`/`DECOMP_GAPS.md` were 3 months stale and actively misleading
(they list `SampleData::Load`, `RndLine::UpdateLine`, `complex::eval` as stubs — all
implemented). Re-verified **41 named non-Xbox functions** against current source:
**all 41 implemented, ZERO still stubs.** Only stub-like markers left are 2 deliberate
Kinect-gated TODOs (Game.cpp:468 move_passed, GamePanel.cpp:448 autoplay scoring) —
Xbox/gesture domain. Docs updated with dated RE-VERIFICATION notes.

## Loop status
The game+engine **native-relevant stub lever is now empirically EMPTY** (41/41 done),
and the **PPC near-miss lever is exhausted** (clrrwi sweep: 1/241 actionable). This
wave landed the one remaining confirmed native correctness bug. Remaining game+engine
work is deep-fidelity native investigation (IK — user-deferred) and PPC backend floors
(register-allocation/commutative/scheduling — not source-fixable). The orchestrated-wave
levers for this scope are spent; further native correctness is single-investigation,
not wave-shaped.
