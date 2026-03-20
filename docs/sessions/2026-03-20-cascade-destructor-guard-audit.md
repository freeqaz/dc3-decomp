# Cascade Destructor Guard Audit

**Date**: 2026-03-20
**Status**: Complete
**Builds on**: [2026-03-20-cascade-teardown-fix.md](2026-03-20-cascade-teardown-fix.md)

## Context

The cascade teardown fix (commits `365019af3`, `41822cdfe`) introduced two-phase
`DeleteObjects` with deferred frees, all behind `#ifdef HX_NATIVE`. A known gap
remained: destructors that `delete` sibling objects in the same ObjectDir cause
double-destroy during Phase 1. Three sites were guarded initially (FileMerger,
FaderGroup, HamCamTransform) plus two more (FlowNode, Sequence). This session
audited the full codebase for additional unguarded sites.

## The Pattern

During cascade `DeleteObjects`, Phase 1 calls `obj->~Object()` on every object
in the dir. If object A's destructor calls `delete objectB`, and B is also in
the dir's Phase 1 list, B gets destroyed twice and freed once by `delete` +
once by `DeferFree` (double-free).

The fix: guard the delete with `ObjectDir::InDeleteObjects()`. During cascade,
either `clear()` the container (Phase 1 handles destruction) or skip the delete
entirely (set pointer to null / early return).

## Audit Methodology

Three parallel subagent sweeps:
1. **Delete-loop audit**: grep for `DeleteAll()`, `while(!empty()) delete`,
   `RELEASE()` in destructors across all of `src/`
2. **Cross-object call audit**: grep for destructors calling methods on sibling
   objects (`->Remove*`, `->Stop*`, `StopAnimation`)
3. **Type verification**: for each candidate, trace inheritance chain to confirm
   whether the deleted type is `Hmx::Object` and whether objects are created
   via `Hmx::Object::New<>()` (dir-owned) vs plain `new` (privately owned)

Key discovery: `#define RELEASE(x) (delete x, x = null)` — RELEASE is a delete,
not ref-count decrement. Every RELEASE on a dir-owned Hmx::Object in a destructor
is a potential double-destroy.

## Changes (6 files)

| File | Class | Dangerous Pattern | Fix |
|------|-------|-------------------|-----|
| MidiInstrument.cpp | `MidiInstrument` | `mActiveVoices.DeleteAll()` — ObjPtrList\<NoteVoiceInst\> | `clear()` + return |
| HamCharacter.cpp | `HamCharacter` | `delete mWaypoint` — created via `New<Waypoint>()` | skip delete |
| PracticeSection.cpp | `PracticeSection` | `DeleteAll(mSeqs)` — DancerSequence via `New<>()` | `clear()` + return |
| UITransitionHandler.cpp | `UITransitionHandler` | `StopAnimation()` on ObjPtr targets — deletes AnimTasks | early return |
| MoveDir.cpp | `MoveDir` | `delete mSkeletonViz` (Hmx::Object) + cross-object overlay calls | return after non-Object cleanup |
| HamNavList.cpp | `HamNavList` | `DeleteAll(mListWidgets)` (UIListWidget) + sound Stop() | `clear()` + delete helpers + return |

### Design decisions per site

**MidiInstrument**: Identical pattern to Sequence (already guarded). ObjPtrList
`clear()` is safe during cascade — Node dtors skip Release via existing guard.

**HamCharacter**: `mWaypoint = Hmx::Object::New<Waypoint>()` confirms dir
ownership. `TheSynth->RemovePlayHandler(this)` left unguarded — TheSynth is a
global singleton, not in the dir.

**PracticeSection**: Both creation sites (`Copy` and `Load`) use
`Hmx::Object::New<DancerSequence>()`. `mSeqs` is `std::vector<DancerSequence*>`
— `clear()` drops raw pointers without invoking destructors.

**UITransitionHandler**: Not an Hmx::Object itself — embedded member of
PanelDir/UIScreen. Its destructor runs during owner's Phase 1 destruction.
`mInAnim`/`mOutAnim` are `ObjPtr<RndAnimatable>` whose targets may already be
destroyed. `StopAnimation()` iterates the refs ring and `delete`s AnimTasks.
Full early return is safest.

**MoveDir**: `RELEASE(mFilterQueue)` and `RELEASE(mAsyncDetector)` are safe
(FilterQueue and MoveAsyncDetector are NOT Hmx::Object — verified from headers).
These run before the guard. Everything after — overlay cross-object calls,
`delete mSkeletonViz`, SkeletonUpdate callback removal — is skipped during
cascade.

**HamNavList**: `mListWidgets` is `std::vector<UIListWidget*>`. `clear()` drops
pointers. Gesture filters (DirectionGestureFilter, HandHeightGestureFilter) are
NOT Hmx::Object — safe to delete during cascade. Duplicated in both paths to
avoid leaking them.

## Verified Safe (no guard needed)

| Site | Why safe |
|------|----------|
| HamGameData::~HamGameData — `DeleteAll(mPlayers)` | HamPlayerData created via plain `new`, stored in ObjVector, not dir-owned |
| Pose::~Pose — `DeleteAll(mElements)` | PoseElement is NOT Hmx::Object |
| PartyModePlayer::~PartyModePlayer | Neither type is Hmx::Object |
| PanelDir::~PanelDir — `RELEASE(*it)` loops | mBackPanels/mFrontPanels are separate loaded RndDirs (via DirLoader), not objects in same dir |
| MoveDir — `RELEASE(mFilterQueue/mAsyncDetector)` | FilterQueue, MoveAsyncDetector NOT Hmx::Object |
| RndTransformable::~RndTransformable | No deletes — cross-object writes on deferred memory |
| Game::~Game — multiple `RELEASE()` calls | Complex singleton; members likely in different dirs |

## Validation

Five parallel subagents independently verified each guard:
- Guard placement and early-return logic correct
- `ObjectDir::InDeleteObjects()` accessible via include chain in all 6 files
- Type classifications confirmed (Hmx::Object vs non-Object)
- Non-cascade paths unchanged
- Build clean, zero regressions

## Total Guarded Sites (cumulative)

| File | Class | Commit |
|------|-------|--------|
| FileMerger.cpp | FileMerger::Merger::Clear | `365019af3` |
| Faders.cpp | FaderGroup | `365019af3` |
| HamCamTransform.cpp | HamCamTransform | `365019af3` |
| FlowNode.cpp | FlowNode | `365019af3` |
| Sequence.cpp | Sequence | `365019af3` |
| MidiInstrument.cpp | MidiInstrument | this commit |
| HamCharacter.cpp | HamCharacter | this commit |
| PracticeSection.cpp | PracticeSection | this commit |
| UITransitionHandler.cpp | UITransitionHandler | this commit |
| MoveDir.cpp | MoveDir | this commit |
| HamNavList.cpp | HamNavList | this commit |
