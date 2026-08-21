# The ObjectDir cascade dangling-reference class — risk assessment

**Date:** 2026-08-20
**Branch:** `fix/objdir-cascade-class-20260820`
**Repo:** dc3-decomp (Dance Central 3). Native port only — every change here is inside `#ifdef HX_NATIVE`.

**Headline:** there were **two independent defects wearing one symptom**, and the brief's
model of the bug accounted for neither exactly.

1. `~Object` **skipped** ref cleanup during a cascade, so anything destroyed as cascade
   collateral left every holder dangling. Fixed at the root: it now nullifies the ring.
   Because a ring belongs to the **referent**, that covers every `ObjPtr` /
   `ObjPtrList` / `ObjPtrVec` / `ObjOwnerPtr` / `ObjDirPtr` holder regardless of where
   the holder lives. Proven by a deterministic unit test.

2. **That fix did not stop the real crash.** The `TaskMgr::Poll` guard still fired in
   **12 of 51 boot→gameplay runs**. Root-caused by instrumentation: `QueueTaskDelete` is
   handed an **already-destroyed Task, at cascade depth 0** — no cascade involved at all.
   Queuing it splices a live `ObjRef` into a dead object's ring, which nothing will ever
   walk. Fixed by refusing a non-live task at the entry point.

> **CORRECTION, 2026-08-21** (`fix/cascade-refloss-20260821`). §8.2 below — "why
> is `AnimTask::mBlendTask` stale?", recorded here as *the highest-value open
> thread* — asked the wrong question. **`mBlendTask` was never stale, and no ref
> was ever lost from a ring.** Instrumented with a shadow ref index
> (`DC3_REFRING_AUDIT=1`), 12/12 boot→gameplay runs report **zero** nodes lost
> from a ring and **zero** `Replace` callbacks declining to retarget, while
> `QueueTaskDelete` was still handed a dead task in all 12.
>
> The real defect is **re-entrant self-destruction**: `FlowAnimate::OnAnimEvent`
> does `delete mAnimTask` on the very `AnimTask` whose `Poll()` sent it the
> event, and `AnimTask::Poll` then keeps running on the freed block, ending in
> `TheTaskMgr.QueueTaskDelete(this)`. The offending pointer is **`this`**, not
> `mBlendTask`; the caller is **`AnimTask::Poll`** (`Anim.cpp:461`), not
> `~AnimTask`. §8.2's guess named the wrong pointer and the wrong caller.
>
> Also corrected below: §8.3 (the ABA-masking hypothesis) is **refuted by
> measurement**, and §3.7 (GPU-cache release hooks "zero call sites") was
> **half wrong** — `~RndMesh` had always called `CleanupGpuMesh`; `~RndTex`
> called nothing, which was a live rendering bug and is now fixed. See the
> follow-up section at the end of this file.

**What is still exposed:** raw `Hmx::Object*` holders. They register no `ObjRef`, so no
ring walk can ever reach them. That population is large and sits in exactly the code a
screen transition tears down. It is a **different class** with the same symptom, and it
is untouched by this lane.

**The methodological point:** the first fix was correct, tested, suite-green, PPC-clean —
and did not fix the reported crash. A single clean runtime run said it had. Only a
40-run loop showed otherwise. Nothing here should be trusted from a single run.

---

## 0. Correcting the framing

The brief stated the invariant as:

> Every `ObjPtr`/`ObjRef` pointing at an object a cascade destroys must be nullified by
> the Phase-0 `NullifyAllRefs` pre-pass. And the pre-pass only walks objects reachable
> from the dir being deleted. Therefore *any* reference held outside the dir graph is
> left dangling.

The premise is wrong in a way that made the problem look much harder than it was, and
it is worth being precise because the wrong version implies "we need a global registry
of live `ObjRef`s" — an expensive, invasive change that turns out to be unnecessary.

**The pre-pass was never the only nullifier.** `~Object` calls `ReplaceRefs(nullptr)`,
which walks the *referent's own* `mRefs` ring. That ring already **is** the registry:
every `ObjPtr`-family holder registers an `ObjRef` in the referent's ring at
construction, no matter where the holder lives. A global, a file-scope static, a
`TheTaskMgr` queue entry — all of them are in that ring.

So the real defect was narrower and entirely local:

> `~Object` **skipped** `ReplaceRefs(nullptr)` during a cascade, and Phase 0 only covers
> objects *in the dir*. An object destroyed **during** a cascade but **not in the dir's
> iteration set** therefore got **neither** path. Its ring was never walked, and every
> holder dangled — dir-resident or not.

"Not in the dir's iteration set" is not an exotic corner. It is all *cascade collateral*:
anything `delete`d from a Phase-1 destructor. `AnimTask::~AnimTask` queueing its blend
task, `Sequence::~Sequence` deleting its instruments, `HamCharacter::~HamCharacter`
deleting `mWaypoint`, `TaskMgr::Start`'s `delete t`.

`TheTaskMgr` being a global was a red herring. It would have been nullified fine — the
Task's ring reaches it. The Task's ring was simply never walked.

### Correcting the workaround count, in both directions

The brief says three site-specific workarounds. There are **18 files** containing
`ObjectDir::InDeleteObjects()` guards. But they are not all the same thing:

| Class | Count | What it is |
|---|---|---|
| **A — cascade-aware destructors** | ~19 sites | "Don't walk/delete my children during a cascade; Phase 0 already nullified their `ObjPtr`s, or the dir iteration will destroy them." `FlowNode`, `RndTransformable`, `Sequence`, `MidiInstrument`, `Faders`, `FileMerger`, `MoveDir`, `HamNavList`, `PracticeSection`, `HamCamTransform`, `UITransitionHandler`, … These are **intrinsic to the three-phase design**, not bug workarounds. They stay. |
| **B — external-holder defences** | 3 sites | `~AnimTask`, `UIScreen::UnloadPanels`, `TaskMgr::Poll`. These exist *only* because of the defect above. |

So the brief's "three" is right about the class that mattered, and the larger number is
not evidence of a spreading hazard. Class A is design.

---

## 1. Is the root fixable? Yes. Two experiments, both measured.

Both alternatives from the brief were built and run against the full native suite rather
than argued about.

### Experiment A — delete the guard entirely

```c++
ReplaceRefs(nullptr);   // unconditional, as on PPC
```

**452 tests ran. No crash, no hang.** The `SnapshotRing` crash the guard's comment cited
did not reproduce.

That comment was stale as written, and `SnapshotRing` itself is the proof: it reads
`mAliveSentinel` (`0xCAFEBABE`, cleared by `~ObjRef`) out of every node and **skips dead
ones**. It was hardened for precisely the freed-`ObjPtrVec`-buffer hazard the comment
claims would crash it.

Only one test failed that had not failed before — and it failed because the dangling
pointer *no longer existed*. See §5.

### Experiment B — nullify instead of skip (**landed**)

```c++
if (ObjectDir::InDeleteObjects())
    NullifyAllRefs();
else
    ReplaceRefs(nullptr);
```

**Identical suite results to A.** Landed B rather than A because A reintroduces `Replace`
callbacks while Phase 1 is walking its todo list, and those callbacks re-enter —
`MessageTask::Replace` does `delete this`. `NullifyAllRefs` never destroys a referent
object and never re-enters `~Object`, so it cannot deepen the cascade.

Idempotent by construction: Phase 0 leaves the sentinel self-looped, so a second call on
a dir-resident object walks zero nodes.

**Cost: one line of logic.** No new data structure, no global registry, no per-site work.

#### One honest correction

I first described `NullifyAllRefs` as "purely mechanical, no callbacks, no deletes."
**That is false.** `ObjPtrList::Node::NullifyObj` and `ObjPtrVec::Node::NullifyObj`
override it, and in `kObjListNoNull` mode they unlink from the holder's container — the
list variant also does `delete this` on the ref node. The distinction that actually
matters is narrower: it never destroys a referent **object** and never re-enters
`~Object`. Corrected in the source comment (`c2b27631`).

Worth flagging as a pattern, since this lane exists because of it: the comment being
*replaced* on that same line was itself confidently wrong about `SnapshotRing`. A
plausible rationale in a comment is not evidence — including when it is freshly written.

### Option 3 from the brief — a debug invariant check

Delivered, in the form the brief asked for ("converting a silent use-after-free into a
test failure"): `native/tests/test_cascade_ref_invariant.cpp`. The invariant is now
stated from the **holder's** point of view, so a regression fails at a named line instead
of as an intermittent SIGSEGV. `MILO_ASSERT` was deliberately not used — it is non-fatal
on native and would become fall-through corruption.

---

## 2. Verification

| Check | Result |
|---|---|
| Red test before fix | `ExternalHolderOfCascadeCollateralIsNullified` FAILS: `holder.Get()` → `0x5564dce95aa0`, expected `nullptr` |
| Same test after fix | PASS |
| Full native suite, before | 365 passed / 2 failed |
| Full native suite, after both fixes | **370 passed / 1 failed** — the remaining failure is `TestGates.EnabledGatesReachTheTestProcess`, the pre-existing baseline failure from gates being off in this configure |
| Runtime A/B, `TaskMgr::Poll` guard | before: **12 / 51 runs**. after: **0 / 30 runs** |
| Runtime, `QueueTaskDelete` refusal | **30 / 30 runs** — the bad event happens every time and is now caught at the door |
| Runtime, reached gameplay | 30 / 30, exit 0 |
| `MergeLifecycleTest` (all 9) | PASS — see §4 |
| PPC codegen `??1Object@Hmx@@UAA@XZ` | 63/63 instructions equal, 100.0% canonical |
| PPC codegen `?DeleteObjects@ObjectDir@@QAAXXZ` | 44/44 instructions equal, 100.0% canonical |
| PPC codegen `?QueueTaskDelete@TaskMgr@@QAAXPAVTask@@@Z` | 59/59 instructions equal, 100.0% canonical |
| **PPC whole-build** | Full `ninja` in the worktree. `report.json` **identical to main**: `matched_code 43.9871%`, `29496 / 48344` functions. Every `src/` edit is inside `#ifdef HX_NATIVE`. |
| Runtime: `autosave_warning_screen → title_screen` | Transition confirmed executing in the log; boot→gameplay clean, exit 0 in every run |
| Runtime: `TaskMgr::Poll` guard | **STILL FIRES** — see §2.1 |

### 2.1 The guard still fires. A correction, and the most important finding here.

I first ran the flow **once**, saw no guard message, and wrote in this document that the
guard "never fires". That was wrong, and it was wrong in exactly the way this lane was
briefed to avoid: a single clean run is not evidence about an intermittent fault.

A 40-run loop on the fixed binary:

```
FIXED BUILD: pass=40 fail=0 guard_fired=6
```

**6 of 40 runs still hit a dangling queued `ObjPtr<Task>`**, and the log line lands at
exactly the reported crash site:

```
DC3 UI: Screen 'autosave_warning_screen' Exit (to 'title_screen')
TaskMgr::Poll: dropped a queued task destroyed by an ObjectDir::DeleteObjects
cascade without its ObjPtr being nullified. Deleting it would have been a
use-after-free.
```

Across two samples on the fixed build: **12 of 51 runs (~24%)**. The runs all exited 0
*because the guard caught it*; without the guard they would have dispatched a virtual
destructor through freed memory.

So there were **two independent defects with one symptom**, and the ~Object fix only
closed the first.

### 2.2 Chasing the residual: three hypotheses, two refuted by measurement

**Hypothesis 1 — `AddRef`'s ring amnesia.** `Hmx::Object::AddRef` contains

```c++
if (sRingsDirty && mRefs.prev != &mRefs && !IsRingPrevAlive())
    mRefs.Clear();      // drops the ENTIRE ring
```

which orphans every *live* `ObjRef` in the ring along with the dead tail node it was
reacting to. That is a genuine, readable-from-the-source defect (see §8). Replaced it
with a prune-and-rebuild that keeps live nodes and drops only dead ones. Suite stayed
green — and the runtime symptom **did not move: 4 of the first 5 runs still fired**.
Refuted as the cause. (See §8 for what happened to the change.)

**Hypothesis 2 — the cascade frees the block.** Refuted directly. Re-ran under
`DC3_POISON_FREED_OBJECTS=1`, which fills every *cascade-deferred* free with `0xDD`,
and added a read-only probe inside the already-firing branch (so non-firing runs stay
bit-identical). 5 of 10 poisoned runs fired, and every one reported:

```
TEMPDIAG queued=0x55cb4cf0c850 vptr=000055ce0dbfe88c poisoned=0 refs_alive=0 idx=0/1
```

`poisoned=0` on every hit. **The block was never freed through the cascade's deferred
path at all.** The guard's own comment blames `ObjectDir::DeleteObjects`; the evidence
says the cascade is not involved in the free.

**Hypothesis 3 — the entry is created already-dangling. CONFIRMED.** Added a probe in
`QueueTaskDelete` asking whether the incoming Task's `mRefs` sentinel was already dead.
The two events pair on the *same address*:

```
TEMPDIAG2 QueueTaskDelete on a task whose mRefs sentinel is already dead:
    0x555bcb67c4c0 (depth=0)
TEMPDIAG  queued=0x555bcb67c4c0 vptr=0000000555bcb67c poisoned=0 refs_alive=0 idx=0/1
```

**`depth=0` — no cascade is running.** A caller hands `QueueTaskDelete` a pointer to an
*already-destroyed* Task. Constructing `ObjPtr<Task>(nullptr, task)` then calls `AddRef`
on a dead object, splicing a live `ObjRef` into a ring that nobody will ever walk again.
Nothing can nullify it, and `Poll` finds it dangling.

The likely caller is `AnimTask::~AnimTask() { TheTaskMgr.QueueTaskDelete(mBlendTask); }`
— `mBlendTask` is an `ObjPtr<AnimTask>` that can itself be stale.

This defect is **not reachable by fixing `~Object` at all**: the object is already gone
before `QueueTaskDelete` is called. It has to be refused at the door, which is what
§2.3 does. It also means `QueueTaskDelete`'s existing `InDeleteObjects()` guard was
never the relevant predicate for this failure — the observed depth is 0 every time.

### 2.3 Second fix: refuse an already-destroyed task at the entry point

```c++
if (!Task::IsLive(task)) { ++sDeadTasksRefused; return; }
```

This is not a per-callsite hack. `QueueTaskDelete`'s contract is "hand me a live Task";
the check enforces the precondition at the API boundary, where the dangling pointer
enters the system, rather than detecting it two frames later at drain time. With it in
place `TaskMgr::Poll`'s guard becomes genuine defence in depth, and the two counters
(`DeadTasksRefused()` / `DanglingQueuedTasksSkipped()`) separate "caught at the door"
from "got past the door" so a future regression is attributable.

Caveat, stated plainly: this stops the *dangling entry* from being created; it does not
explain why `mBlendTask` is stale in the first place. That upstream question is open —
see §8.

---

## 3. Exposure that remains — raw `Hmx::Object*` holders

The fix reaches holders through the referent's ring. **Raw pointers register no `ObjRef`,
so nothing can reach them.** This is by construction, not oversight.

Ranked by reachability in normal gameplay, not by count.

### Tier 1 — reachable on any screen transition

1. **`gDataVars`** (`src/system/obj/DataNode.cpp:15`). `DataNode` stores objects as a bare
   `Hmx::Object *` (`Data.h:109-112`) with **no ring registration**, and nothing sweeps
   the map on destruction. Every DTA `$var` holding an object is a raw root. Concrete:
   `SetTheWorld` writes `DataVariable("world")` (`world/Dir.cpp:34-38`). Related raw
   roots: `gVarStack`, `gCallStack` (`DataUtl.cpp:19-28`, `DataArray.cpp:129`).
   *This is the largest single untracked root set in the codebase.*

2. **`TheHamUI`'s twelve raw members** (`lazer/meta_ham/HamUI.h:87-107`) — `mHelpBar`,
   `mLetterbox`, `mBlacklight`, `mEventDialogPanel`, `mBackgroundPanel`,
   `mContentLoadingPanel`, `mOverlayPanel`, `mGamePanel`, `mAugmentedPhoto`,
   `mEventScreen`, `mShellInput`, `mUIOverlay`. All dir-owned, all on a process-lifetime
   global. Highest-density cluster in the tree.

3. **`UIManager` / `UIScreen` raw pointers** — `mPushedScreens`, `mCurrentScreen`,
   `mTransitionScreen`, `mSink`, `mCam`, `mEnv`, `mOverlay` (`ui/UI.h:97-109`);
   `PanelRef::mPanel` in `mPanelList`, `mFocusPanel`, `static sUnloadingScreen`
   (`ui/UIScreen.h:16,59,65-66`). These are the objects `UnloadPanels` destroys.

### Tier 2 — reachable when a screen is unloaded and re-entered

4. **~15 resolve-once `Find<>` caches.** Function-local statics initialised on first call
   and **never re-resolved**, so a dir unload/reload leaves them pointing at freed memory:
   `hamobj/RhythmDetector.cpp:308,326,357,689,887`;
   `hamobj/RhythmBattle.cpp:182,292,414,431,672,1242,1398`;
   `hamobj/RhythmBattlePlayer.cpp:129,612,648`; `App.cpp:1369`;
   `moviebink/BinkMovieImpl.cpp:400`.
   `RhythmDetector.cpp:310` dereferences `panel->TypeDef()` with **no null and no
   liveness check**. The `RhythmBattle` variants gate on `panel->LoadedDir()`, which is
   itself a read through the possibly-dangling pointer.

5. **Async-callback holders** — an object supplies itself as a callback target, then its
   screen is torn down before the callback fires:
   `TheMemcardMgr.mSelectDeviceCallBackObj` (`meta/MemcardMgr.h:74`),
   `TheVirtualKeyboard.mPobjKeyboardCallback` (`os/VirtualKeyboard.h:6`),
   `TheRockCentral.mKinectShareCallback` (`net_ham/RockCentral.h:110`),
   `ThePassiveMessenger.mCallback` (`meta_ham/PassiveMessenger.h:55`),
   `gJoypadMsgSource` (`os/Joypad.cpp:29`).

### Tier 3 — plausible, unconfirmed

6. **Native GPU caches keyed by object pointer** — `sMeshGpuData`, `sTexGpuData`,
   `sCubeTexGpuData`, `sMeshGpu`, `sGeomSyncGen`, `sTexGpu`, and
   `GpuResourceRegistry`'s three maps, in both `native/src/platform/` and
   `../milo-native-engine/src/platform/`. Erase sites exist, but a repo-wide grep for
   `ReleaseGpuMesh` / `ReleaseGpuTex` / `ReleaseRB3Mesh` / `ReleaseRB3Tex` returned
   **zero call sites**, and neither `~RndMesh` nor `~RndTex` references them.
   **Worth confirming** — if the hooks really are unwired, these accumulate dangling keys
   across every dir unload. (Keys are hashed, not dereferenced, so the failure mode is a
   stale-value hit rather than an immediate fault.)

7. `sImpostorCache` (`world/Crowd.cpp:40`, native-only, keys *and* values dir-owned, no
   eviction hook found); `sShadowSpots` (`world/SpotlightDrawer.h:115`);
   `MidiParser::sParsers` (`midi/MidiParser.h:110`);
   `SynthPollable::sPollables` (`synth/Pollable.h:18`);
   `CharBoneDir::sResources` (`char/CharBoneDir.cpp:19` — a dir *inside* `Main()` that is
   explicitly `delete`d, so `Main()->DeleteObjects()` would leave it dangling and
   `Terminate()` would double-free); `Character::sCurrent`;
   `FreestyleMoveRecorder::sInstance` (dtor does not clear it).

8. `TaskTimeline::mPollingTask` (`obj/Task.h:138`) — the raw, unguarded sibling of the
   `unk84` entry that crashed. Narrow window (set and cleared within one `Poll`), but no
   liveness check on the path.

### Confirmed inert (do not spend time here)

- **All `ObjPtr` / `ObjPtrList` / `ObjPtrVec` / `ObjOwnerPtr` / `ObjDirPtr` holders.**
  Ring-protected, and after this fix that protection holds for cascade collateral too.
- Self-clearing statics: `RndEnviron::sCurrent`, `RndCam::sCurrent`,
  `RndPostProc::sCurrent`, `SpotlightDrawer::sCurrent`, `gDataThis`, `TheGame`,
  `TheGameMode`, `TheHamDirector`, `TheGamePanel`, the three panel `sInstance`s,
  `HamSongData::sInstance`.
- PropSync scratch globals (`gDir`, `gLine`, `gHair`, `gPropBones`, `gEditPreset`,
  `gColor`, `gMe`, `gCharMe`): they dangle permanently but are only read inside the same
  synchronous `SyncProperty` call that sets them.
- Native debug maps keyed by `const void*` that never dereference (`sSeen`, `sCnt`,
  `sHaPtr`, `sceneIds`, …).
- Dirless singletons: `sGlowMat`, `sGlobalDefaultSpline`, `gDefaultMat`, `sMainDir`.

### The tool for hunting these: `DC3_POISON_FREED_OBJECTS=1`

Added in `8f412036`. Cascade-freed blocks are filled with `0xDD` and **quarantined
instead of freed**, so the address is never reused. A vptr read back as
`0xDDDDDDDDDDDDDDDD` is non-canonical on x86-64, so the first virtual call through a
stale pointer faults **at the dereference**, with the offending holder on the stack —
converting "3 runs in 31, somewhere unrelated" into a deterministic stop.

Off by default (it never returns memory) and **self-announcing on first use**, because a
diagnostic you cannot confirm was active is worse than none.

Suite: 369 passed / 1 failed with the flag both off and on — it does not perturb tests.

**Negative result:** poisoning did **not** surface a raw-pointer use-after-free on the
boot→gameplay path (4 confirmed-active runs, all clean to `game_screen`). That path does
few repeated screen unloads, so this is weak evidence about the Tier-1/2 holders rather
than absolution. The follow-up wants a loop that re-enters and unloads the same screens
many times, which is where the resolve-once `Find<>` caches would actually be exercised.

---

## 4. The two merge-lifecycle tests: **both pass**

Run on this branch:

```
[  PASSED  ] 9 tests.   MergeLifecycleTest.*
```

Including `SubdirsSurviveSourceDirDeletion` and `MergedObjectsSurviveParentDirReload`.
They do not skip, and they do not fail.

The project memory recording them as "2 failing tests define the target" is **stale**.
The original cascade bug they were written for — `~ObjectDir`'s `NullifyAllRefs` killing
objects reparented by `MergeDirs` — is closed, and the tests were subsequently corrected
for engine behaviour they had mismodelled (`AppendSubDir` → `SetSubDir(true)` →
`SetName(nullptr, nullptr)` clears a subdir's name, so they check `HasSubDir` and pointer
identity rather than `FindObject` by name). That memory note should be updated.

---

## 5. Adversarial review of the `TaskMgr::Poll` guard

`src/system/obj/Task.cpp:459-500`. Four findings.

### 5.1 `NullifyObj()` does what the comment claims — confirmed ✅

`ObjRefConcrete::NullifyObj` sets `mObject = nullptr` and self-loops `next`/`prev`
(`Object.h:226`, `Object.h:124`). It writes **only to `this`**, which is the live `ObjPtr`
inside `TheTaskMgr`'s vector. It never touches the freed Task's block. Correct.

Note it does not *unlink* — the freed ring's neighbours still point at it. Harmless: that
ring is never walked again.

### 5.2 The stated reason for preferring it over `unk84.clear()` is **wrong** ❌

> "Plain `unk84.clear()` would not be safe on its own: `~ObjRefConcrete` calls
> `mObject->Release(this)` whenever the cascade/`sRingsDirty` guards happen to be false."

**`sRingsDirty` is a latch that is never reset.** It is initialised `false`
(`Object.cpp:23`) and set `true` in `FlushDeferredFrees` (`Dir.h:489`). Grep for any other
assignment: there is none.

A dangling queued task can only exist *after* a cascade has run, which means
`FlushDeferredFrees` has run, which means `sRingsDirty` is permanently `true`. The
branch the comment worries about is **unreachable in the exact scenario it describes**.

The conclusion is still right, but for a different reason. The branch that *does* execute
is `SafeReleaseFromRing(this)` (`ObjPtr_p.h:36-39`), and that writes

```c++
ref->prev->next = ref->next;
ref->next->prev = ref->prev;
```

— i.e. **into the freed Task's ring nodes**. That is the real hazard, and `NullifyObj` is
right because it avoids writing to *neighbours*, not because of which guard is false.

This matters practically: the wrong reasoning points a future lane at adding
`InDeleteObjects()` checks, when the actual rule is "never write to ring neighbours you
do not own."

### 5.3 `Task::IsLive()` is ABA-unsound — **confirmed by test** ❌

`LiveTasks()` is an `unordered_set<Task*>` keyed on **address**. It answers "is a Task
alive at this address", not "is *this* task alive". Free a Task, allocate another of the
same size, and the allocator hands back the same block — at which point
`IsLive(stalePointer)` is `true` again and the guard waves the entry through to
`delete unk84[i].Ptr()`, **destroying a live, in-use Task through a stale pointer**.

`CascadeRefInvariantTest.IsLiveIsAddressKeyedAndThereforeABAUnsound` demonstrates this,
and on this machine the allocator **did** reuse the block. (It skips rather than fails if
reuse does not happen — the claim under test is "reuse defeats the predicate", not
"reuse is certain".)

Not currently reachable: with rings nullified during a cascade, nothing dangling survives
to reach `Poll`. But the predicate cannot carry the load on its own, and the test exists
so nobody copies it to a new site believing otherwise.

### 5.4 The guard's own test demanded that the bug persist ❌

`PollSkipsQueuedTaskDestroyedByDeleteObjectsCascade` asserted:

```c++
ASSERT_EQ(TheTaskMgr.QueuedDeleteRawForTest(0), task)
    << "the queued ObjPtr should still hold the dangling pointer";
```

A test that pins a **symptom** rather than an invariant. Because the fix lived inside
`Poll()`, the guard could only be exercised if something dangling arrived — so the test
had to require the dangling pointer to exist. Consequence: **fixing the root made this
the only failing test in the suite**, i.e. the correct fix read as a regression.

Its central assertion is now inverted — the entry must come back null, and the guard must
never fire — with the precondition assertion kept so it cannot go vacuous.

### Verdict on the guard

Keep it, as defence in depth and as instrumentation that should now never fire. Do **not**
generalise it (see §6). Its comment has been left in place but is now describing a
condition the code no longer reaches; §5.2 and §5.3 are the corrections.

---

## 6. Is `Task::IsLive()` generalisable? **No.**

There is no analogous registry for arbitrary `Hmx::Object`s. `LiveTasks()` is the only
liveness registry in the tree. `DirPtrRefCounts()` (`Dir.h:21`) is address-keyed too, but
it is a refcount for `ObjDirPtr`, not a liveness oracle.

Copying the pattern would mean a new registry per type, each paying a hash insert/erase
on every construction and destruction, and each **inheriting the ABA unsoundness of
§5.3**. That is exactly the per-callsite-hack failure mode this project has a standing
rule against.

The general mechanism already existed: **the referent's own `mRefs` ring**. It is
maintained by every `ObjPtr`-family holder for free, it is exact rather than
address-approximate, and it has no ABA problem because the ring node is removed when the
holder goes away. The fix in §1 is precisely "use the mechanism that was already there."

---

## 8. Identified but NOT resolved

Recorded so the follow-up does not have to rediscover them.

### 8.1 `AddRef`'s ring amnesia — real defect, fix landed but unvalidated in isolation

`Hmx::Object::AddRef` responded to a dead tail node by calling `mRefs.Clear()`, dropping
the **entire** ring and orphaning every live `ObjRef` in it. Orphaned holders are then
unreachable by any later ring walk — i.e. it silently reintroduces the exact
dangling-holder bug §1 fixed.

The hazard it was written for is genuine: inserting at the tail writes
`mRefs.prev->next`, so a dead tail means writing through a freed pointer. The response
was disproportionate. Replaced with `PruneDeadRefs()`, which rebuilds the ring from its
live nodes using the same read-only traversal `SnapshotRing` and `NullifyAllRefs` already
use, writing only to live nodes and the sentinel.

**Landed, with a caveat stated plainly:** it did **not** change the runtime symptom it
was written for (4 of the first 5 runs still fired), and there is **no isolated unit
test** for it — manufacturing a dead-but-linked ring node from a test requires reaching
past the `ObjPtr` API. It is kept because the loss of live refs is readable directly from
the source and the prune is strictly weaker than the clear, and because the configuration
it is part of measured clean. A follow-up should write the isolated test or revert it.

### 8.2 Why is `AnimTask::mBlendTask` stale in the first place?

> **ANSWERED 2026-08-21, and the premise was wrong: it is not.** See the
> follow-up section at the end of this file. Everything in the rest of this
> subsection is the 2026-08-20 hypothesis, kept as written.

§2.3 stops the dangling *queue entry* from being created. It does not explain why
`AnimTask::~AnimTask` has a stale `ObjPtr<AnimTask>` to hand over. `mBlendTask` is a
normal `ObjPtr`, so the blend task's own `~Object` should have nullified it. That it did
not is a third defect in the same area, currently unexplained, and it may have other
victims that are not Tasks and therefore have no `IsLive()` registry to catch them.

**This is the highest-value open thread in this lane.**

### 8.3 The `Poll` guard's ABA hole was probably masking events, not just theoretical

The refusal counter fires in **every** run, while the `Poll` guard fired in only ~24%.
The same dead-task queueing happens every time; it was only *detected* a quarter of the
time. The most likely explanation for the rest is §5.3: the freed block gets recycled by
a new `Task`, `Task::IsLive` returns true for the stale pointer, and `Poll` deletes the
**new, live** task through the old pointer — silently, with no log line.

Stated as a hypothesis, not a measurement: it is consistent with the counts and with the
confirmed ABA test, but it was not directly observed. Worth confirming, because it would
mean the original bug was more severe than the crash rate suggested.

---

## 7. How confident should we be?

**Moderate — and specifically: higher than before this lane, but not high.**

The honest one-line answer: **two defects found and fixed, one root cause still
unexplained (§8.2), and a large adjacent class untouched (§3).**

What genuinely improved:
- The `~Object` skip is closed **structurally**, not guarded. Coverage follows from the
  ring belonging to the referent, so it does not depend on enumerating holders correctly.
  It regresses as a **deterministic test failure at a named line**, not a crash rate.
- The reported crash path is closed at its actual origin, and the origin was *measured*,
  not guessed — after two hypotheses were refuted by measurement.
- The guard that used to fire on ~24% of runs now fires on none, with a separate counter
  proving the event is being caught at the door rather than having stopped occurring.
- PPC codegen byte-identical on every touched unit.

What should hold confidence down:
- **§8.2 is unexplained.** `AnimTask::mBlendTask` is a plain `ObjPtr` that should have
  been nullified and was not. Until that is understood, there is a ref-loss mechanism
  loose in the engine, and Tasks are merely the victim that happens to have an `IsLive()`
  registry to catch it. Other victims would fail silently.
- **The `Poll` guard was catching only a fraction of the events** (§8.3). If the rest were
  ABA false-positives, the pre-fix engine was deleting live Tasks silently.
- **Runtime evidence is one linear path.** boot→gameplay, 1200 frames. The suite *skips*
  the deep `DtaFlowTest` UI chain in this configure and
  `ManualReproTest.AutosaveWarningPanelUnload` skips outside `dc3-native`. Nothing here
  covers repeated screen unload/reload cycles.
- **The raw-pointer class in §3 is real, uninvestigated, and produces the same symptom.**
  A future crash that looks like this one may well be a Tier-1 raw holder and will not be
  fixed by anything in this lane.
- **§8.1 landed without an isolated test.**

Above all: the first fix in this lane was correct, unit-tested, suite-green and
PPC-clean — **and did not fix the reported crash.** One clean runtime run said it had.
That should calibrate how much any single result here is worth.

### Smallest safe next increment, in priority order

1. **Root-cause §8.2** — why is `mBlendTask` stale? This is the only open item that could
   still be an active engine-wide ref-loss bug.
2. Confirm or refute §8.3 by counting deletions of recycled-address tasks. If real, it
   changes the severity assessment of the original bug retroactively.
3. Run the poison flag under a **screen-thrash** loop (enter/unload the same screens tens
   of times), not the linear boot path — the cheapest way to learn whether the Tier-1/2
   raw holders are live bugs or merely ugly.
4. Write the isolated test for §8.1, or revert it.
5. Confirm or refute §3.7 — whether the GPU-cache release hooks are called at all. A
   single grep answered "no call sites"; it deserves a real check.
6. Convert Tier-1 raw holders to `ObjPtr` where the type allows. Each conversion moves a
   holder from "unreachable by any sweep" to "covered by §1". `TheHamUI`'s twelve members
   are the best value per unit of risk.
7. Do **not** add more `InDeleteObjects()` guards or more liveness registries.

---

## Changes landed on this branch

| Commit | Change |
|---|---|
| `2c4fe795` | Red test — the invariant, stated deterministically (`native/tests/test_cascade_ref_invariant.cpp`) |
| `0aab85c0` | **Fix 1 of 2** — `~Object` nullifies its ref ring during a cascade instead of skipping it |
| `c2b27631` | Correct an overclaim in the fix comment; pin `Task::IsLive`'s ABA unsoundness with a test |
| `8f412036` | `DC3_POISON_FREED_OBJECTS` — opt-in poison/quarantine for hunting the raw-pointer class |
| `20e43c2f` | **Fix 2 of 2** — `QueueTaskDelete` refuses an already-destroyed Task; `AddRef` prunes dead ring nodes instead of clearing the whole ring |

Five tests in `native/tests/test_cascade_ref_invariant.cpp`, plus the inverted assertion
in `test_object_lifetime.cpp`'s `PollSkipsQueuedTaskDestroyedByDeleteObjectsCascade`.

### If only one thing is remembered from this lane

A fix can be correct, unit-tested, suite-green and PPC-byte-identical, and still not fix
the bug it was written for. The only thing that caught it was running the real flow
dozens of times and counting. **One clean run is not a result.**


---

# Follow-up, 2026-08-21 — `fix/cascade-refloss-20260821`

Task #126 picked up the three open threads. Two are closed, one is enumerated
and partly closed. Nothing here was concluded from a single run.

## Thread 1 — the ref-loss mechanism is not ref loss

### The instrument

Arguing about ring loss from the source had already produced two refuted
hypotheses (§2.2), so this lane built an instrument first: `RefAudit`
(`DC3_REFRING_AUDIT=1`, off by default, self-announcing). It maintains a shadow
index of *which `ObjRef` node currently targets which object*, updated at every
`mObject` mutation in `ObjRefConcrete`, and lets `~Object` ask the two questions
the rings cannot answer about themselves:

| probe | question | a hit means |
|---|---|---|
| `PreWalk` | is every node that targets me reachable from my ring? | **ring loss** — a live node was unlinked |
| `PostWalk`| after the walk, does any node still target me? | **a `Replace` that declined** to retarget |

Those are the only two shapes the dangling-holder bug can take, and they want
opposite fixes.

**Result: `LOST-FROM-RING` 0 and `WALK-DECLINED` 0 across 12/12 runs** — while
`QueueTaskDelete`'s refusal fired in all 12. The ring machinery is sound. The
"global registry of live `ObjRef`s" that §0 argued was unnecessary would also
have found nothing.

`RefAudit` stays in the tree as a diagnostic. Nothing consults it and nothing
may: the referent's own ring is the mechanism.

### What is actually happening

The paired backtraces (audit mode journals where each Task died):

```
QueueTaskDelete refused a dead task
  #1 TaskMgr::QueueTaskDelete
  #2 AnimTask::Poll +0x550                 <- Anim.cpp:461, argument is `this`
  #3 TaskTimeline::Poll
...that task was destroyed here:
  #0 ~Task
  #1 ~AnimTask
  #2 FlowAnimate::OnAnimEvent              <- `delete mAnimTask`
  #3 FlowAnimate::Handle
  #4 AnimTask::Poll +0x24d                 <- the "looped" message it just sent
```

`FlowAnimate::OnAnimEvent`'s `sLooped` branch does
`mAnimTask->SetListener(nullptr); delete mAnimTask;` when a stop is deferred.
`AnimTask::Poll` then resumes on freed memory: `mListener = nullptr` and
`mPrevFrame = frame` are *writes* to it, the `mAnimTarget` reads are reads of
it, and `QueueTaskDelete(this)` hands the manager a corpse.

Tasks are merely the victim class that happens to have an `IsLive()` registry to
notice. The shape — *a DTA/message callback deleting the object whose method is
on the stack* — is invisible everywhere else.

### The fix: `Hmx::DeathWatch`

An `ObjPtr` protects the **referent's** holders. Nothing protected the `this` of
a frame already on the stack. `DeathWatch` links a stack-local flag into the
object; `~Object` trips every flag in the chain before doing anything else, and
unlinks as it goes so `~DeathWatch` never writes back into a freed block.
Checking it is a load and a branch.

Deliberately **not** `Task::IsLive(this)`: that is address-keyed, so a recycled
block answers "still alive" and sends the frame straight back into freed memory.
`DeathWatch` compares no addresses and has no ABA hole.

`HX_DEATH_WATCH` / `HX_RETURN_IF_DELETED` expand to `((void)0)` off `HX_NATIVE`,
so the PPC control flow is untouched.

### Rates, not booleans

Counting "`QueueTaskDelete` refused an already-destroyed task" over
boot→gameplay runs:

| build | audit off | audit on |
|---|---|---|
| before | **5 / 40** | **12 / 12** |
| after  | **0 / 40** | **0 / 12** |
| after, with the guards deliberately removed again | **40 / 40** | — |

`exit 0` and reached `main_screen` in every run of every arm. The audit build's
timing makes the fault deterministic, which is the sharper of the two rulers;
the sabotage arm is sharper still.

## Thread 3 — ABA: designed out where possible, and §8.3 refuted

Every `Task` now carries a monotonic serial. `LiveTasks()` maps address →
serial, and `IsLive(ptr, serial)` answers "is *this* task alive". The two gates
that can capture a serial while the pointer is known good now do:
`TaskTimeline::Poll` (recorded in `TaskInfo` at `AddTask`) and `TaskMgr::Poll`'s
drain (a native-only vector parallel to `unk84`, recorded at queue time).

**`QueueTaskDelete`'s door check cannot be made sound**, and that is a finding
rather than an omission: its signature offers nothing but an address, so there
is no serial to compare. No registry fixes that — only the frame that still owns
a valid `this` can, which is precisely why Thread 1's fix lives in
`AnimTask::Poll`.

**§8.3 is refuted for this workload.** That hypothesis — the `Poll` guard's
~24%-vs-always discrepancy was ABA silently deleting *live* tasks — predicts a
nonzero count of "address live, serial moved on". Over 80 runs (40 fixed, 40
sabotaged) that count is **0**. The duller explanation fits the data:
`QueueTaskDelete`'s door check catches the event first, so `Poll`'s guard never
sees it (`poll_dropped` 0/40 in both arms while `refused` was 40/40 in the
sabotaged arm). The ABA hole is real — the unit test still demonstrates it on
this allocator — it just was not what was happening.

## Thread 2 — the raw-holder enumeration

Re-derived from scratch (620 `Hmx::Object` subclasses, matched against raw
pointer declarations), not taken from §3's counts. Totals: **472** raw pointer
members, **116** globals/statics (**69** with no clearing site found), **27**
function-local resolve-once caches, **96** containers of raw object pointers
plus **12** containers of by-value structs that embed them.

Two of §3's specific claims were wrong in opposite directions:

* **`TheHamUI` has eleven raw panel members, not twelve.**
* **§3.7's "zero call sites" grep was wrong.** `~RndMesh` has always called
  `CleanupGpuMesh`. `~RndTex` called nothing — see below.

### The triage rubric

A raw holder is dangerous iff **the holder outlives the referent** *and* **it is
dereferenced afterwards without a validity check**. That collapses the 472
members almost entirely: the overwhelming majority are held by an object in the
same `ObjectDir` as their referent, so the cascade destroys holder and referent
together, and the owner back-pointers (`ObjPtr::mOwner`, `ObjPtrVec::mOwner`,
`TypeProps::mOwner`) are structurally safe because the holder is embedded in the
owner. The population that matters is the one whose lifetime is *longer* than
the referent's: process-lifetime globals, class statics, function-local caches,
and address-keyed side tables.

Applying it to the loudest candidates:

| holder | verdict |
|---|---|
| `RndTex` / `RndCubeTex` GPU side tables | **DANGEROUS — confirmed live bug.** Fixed for `RndTex`; see below |
| `gDataVars` (+ `gReadFiles`, `FlowManager::mEventTimes`) | **Dangerous, and deliberately not fixed.** `DataNode` stores `Hmx::Object *` raw in an 8-byte union whose layout is PPC-critical, so it cannot carry a ring node. The only alternative is a global object→node index maintained on every object-valued `DataNode` assignment — i.e. on the hottest path in script evaluation. Not worth paying blind; let poisoning decide |
| `Character::sCurrent` | **Fine, but §3 named the wrong reason.** It is not "self-clearing": it is *scope-restored* by the `AutoSetCurrentCharacter` RAII guard, and every `Current()` read is inside such a scope. Same class as the PropSync scratch globals |
| `GpuResourceRegistry`'s three maps | **Inert — dead code.** No caller anywhere outside its own `.cpp`; a parallel implementation nothing uses |
| ~16 resolve-once `Find<>` caches (`RhythmDetector`, `RhythmBattle*`, `App.cpp`, `BinkMovieImpl`) | **Dangerous in principle, unexercised in evidence.** `RhythmDetector.cpp:310` dereferences `panel->TypeDef()` with no null and no liveness check. Not reached on any workload run here |
| `TheHamUI`'s 11 panel members, `UIManager::mCurrentScreen`/`mTransitionScreen`, `UIScreen::PanelRef::mPanel` | **Not converted, on purpose.** Conversion is mechanical but touches PPC-visible layout in `src/`, and nothing observed here fires. §3's "best value per unit of risk" was asserted, never measured |

### The one that was a real bug

`sTexGpuData` is `unordered_map<RndTex*, GpuTexData>`, keyed on the object's
**address**, holding no `ObjPtr`. `~RndTex` carried this instead of a hook:

```c++
// Note: RndTex destructor doesn't call us directly yet.
// For Tier 1, leaked GPU textures are acceptable (cleaned up at shutdown).
// TODO: Hook into RndTex destructor or add ref-counting.
```

The leak is the advertised cost and the lesser half. The other half: the next
`RndTex` the allocator places at that address inherits the dead entry with
`uploaded=true` and **renders the previous texture's image** — silently, with no
assert and no log. Fixed by calling `CleanupGpuTex(this)` from `~RndTex`, under
`HX_NATIVE`, exactly mirroring `~RndMesh`.

The regression test asserts the *outcome* (a fresh `RndTex` at a recycled
address must have no GPU view), uses the renderer's real `PresyncBitmap` upload
path rather than a fabricated entry, and uses address reuse as its own
non-vacuity control — it skips rather than passes if the allocator declines to
reuse. On this machine it reuses on the first attempt.

`RndCubeTex` has the identical hazard and is **not** fixed: the only
`Tex_Wgpu.cpp` compiled into `dc3-native`/`milo-tests` is the shared engine's,
which exports `CleanupGpuTex` but no `CleanupGpuCubeTex`, and adding it means
editing a repo three decomps build against concurrently plus a pin bump.

Two incidental findings from that attempt, both worth acting on separately:

* **`dc3-native` links cleanly with an undefined symbol.** With
  `CleanupGpuCubeTex(RndCubeTex*)` undefined the link *succeeded* and the
  program died at runtime with `symbol lookup error` the first time it was
  reached. A typo'd or unported `extern` is a runtime landmine here, not a build
  error.
* **`native/src/platform/Tex_Wgpu.cpp` produces no object for the built
  targets.** It is listed in CMake source sets that the native targets do not
  use (the engine's copy wins). Editing it looks like a fix and compiles
  nothing.

## Process note

Two changes in this lane were destroyed by `git checkout -- <file>` used to undo
a *sabotage* edit on a file whose fix had not been committed yet — checkout took
the fix along with the sabotage. One of them (`~RndTex`) shipped as a
test-without-fix and the suite caught it; the accompanying "PPC: 0 of 48,344
functions differ" claim was vacuous, because there was no change to be neutral
about. **Commit the fix before sabotaging it.**
