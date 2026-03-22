# Session: UIScreen::UnloadPanels Hack Removal (2026-03-21)

## Goal

Remove the `#ifdef HX_NATIVE return;` hack in `UIScreen::UnloadPanels()` that skipped all panel unloading during screen transitions, leaking all PanelDir memory.

## Root Causes Found

The original plan identified 4 root causes. Investigation revealed **additional** crash paths:

### 1. NullifyAllRefs ring walk crash (Object.cpp)
**Already in worktree.** The ring walk broke on dead entries (freed ObjPtrVec buffers) and missed live entries after them. Fixed to skip dead entries and continue walking, using raw pointer arithmetic with kAliveSentinel validation.

### 2. FlowNode::ActivateChild null deref (FlowNode.cpp)
Nullified ObjPtrVec entries passed to ActivateChild caused SIGSEGV on `child->Activate()`. Added `if (!child) return;` guard.

### 3. FlowQueueable::Deactivate/ChildFinished cascade (FlowQueueable.cpp)
`ObjPtrList<Hmx::Object> temp(mListeners)` copy-constructs from a list with nullified entries, calling `push_back(null)` on a `kObjListNoNull` list. Added `InDeleteObjects()` early return in both `Deactivate()` and `ChildFinished()` stop-requested paths.

### 4. ObjPtrVec::ReplaceNode duplicate (ObjPtr_p.h)
MergeObject redirects refs from o1->o2. If a vec had refs to both, both Nodes point to o2 (duplicate). Later destruction orphans a Node. Added duplicate detection and erase.

### 5. ObjPtrList::insert(null) during normal operation (ObjPtr_p.h)
**Discovered during debugging.** 120+ null inserts happened with `InDeleteObjects=0` — NOT during cascade. NullifyAllRefs nullifies ObjPtrList entries but doesn't update `mSize`. Subsequent operations (copy constructor, operator=) iterate and push_back the nullified entries. Fixed by silently skipping null inserts for `kObjListNoNull` lists.

### 6. Inner DeleteObjects Phase 0 crash (Dir.cpp)
**Key discovery via GDB.** The crash was in `NullifyAllRefs()` during the INNER `DeleteObjects` (Flow's ObjectDir base). The outer Phase 1 destroyed objects and freed their ObjPtrVec vector buffers. Ring entries in those freed buffers contained garbage. The inner Phase 0 walked those corrupted rings -> SIGSEGV.

**Fix:** Two-part:
- **Pre-cascade recursive NullifyAllRefs** in `~ObjectDir()` BEFORE `mSubDirs.clear()`, while all memory is still valid. Uses dynamic_cast discovery to find inline sub-ObjectDirs not registered in mSubDirs.
- **Skip Phase 0** in inner `DeleteObjects` (`sDeleteObjectsDepth > 1`) since the outer level already handled it.

### 7. AnimTask use-after-free (UIScreen.cpp)
**Discovered after fixing #6.** AnimTasks in TheTaskMgr reference RndAnimatable objects in panels. On Xbox, `~Object::ReplaceRefs(nullptr)` triggers `AnimTask::Replace -> QueueTaskDelete`. On native, cascade destruction skips `ReplaceRefs` to avoid ring corruption, leaving AnimTasks with stale pointers.

The recursive NullifyAllRefs reached 24 objects (5 dirs) in autosave_warning_panel but STILL missed the AnimTask's target — likely in an unregistered inline sub-ObjectDir or merged dir.

**Fix:** `ClearTimelineTasks(kTaskSeconds)` + `ClearTimelineTasks(kTaskUISeconds)` before panel unload. See deep dive below.

## ClearTimelineTasks Deep Dive

### Why not ClearTasks() (the nuclear option)?

`ClearTasks()` deletes ALL tasks from ALL 4 timelines (seconds, beats, UI seconds, tutorial seconds). This is too broad:

- **Shared panels**: If screens A and B share a panel, that panel's animations survive unload. ClearTasks kills them anyway.
- **Beat-synced tasks**: Music/audio-driven visual effects on `kTaskBeats` timeline would be killed.
- **Global tasks**: ScriptTasks/ThreadTasks not scoped to any panel dir get nuked.

### Why ClearTimelineTasks(seconds + UI) is better

UI animations (panel enter/exit transitions, PropAnim effects) run on `kTaskSeconds` and `kTaskUISeconds`. Beat-synced and tutorial tasks are independent of panel lifecycle. Clearing only seconds + UI timelines:
- Kills the stale AnimTasks that cause the crash
- Preserves beat-synced tasks (audio, music effects)
- Preserves tutorial tasks
- Still kills shared-panel animations, but these are re-created by the new screen's Enter() DTA scripts anyway

### Why a fully surgical approach is hard

The ideal fix would call `ReplaceRefs(nullptr)` on each dying object (like Xbox does via `~Object`), triggering per-task cleanup callbacks. But:
1. Native skips `ReplaceRefs` during cascade due to ring corruption from freed ObjPtrVec buffers
2. Pre-cascade `ReplaceRefs` via recursive dir crawling doesn't reach ALL objects — some live in unregistered inline sub-ObjectDirs that hash-table iteration misses
3. Adding a `TaskMgr::RemoveTasksInDir(dir)` requires accessing private Task members (mAnim, mObj, mThis) to find each task's target, which changes the PPC-facing API

### Task type inventory

| Task Subclass | Target Member | Replace Behavior (Xbox) | Affected by ClearTimelineTasks? |
|---|---|---|---|
| AnimTask | `mAnim` (ObjOwnerPtr) | QueueTaskDelete | Yes — runs on seconds |
| MessageTask | `mObj` (ObjOwnerPtr) | `delete this` | Yes — usually seconds |
| PropertyTask | `mTarget` (ObjPtr) | `delete this` | Yes — seconds/UI |
| EventTask | `mOwner` (ObjPtr) | Lazy: checks in Poll | Yes — seconds |
| ScriptTask | `mThis` (ObjOwnerPtr) | `delete this` | Varies by creator |
| ThreadTask | (inherits ScriptTask) | `delete this` | Varies by creator |

### Timing safety

`ClearTimelineTasks` runs in `UIScreen::Enter()` at the start of `UnloadPanels()`, BEFORE the new screen's panels call `Enter()`. So tasks created by the incoming screen are not yet created and cannot be affected.

## Files Changed

| File | Change |
|------|--------|
| `src/system/obj/Object.cpp` | NullifyAllRefs: skip dead ring entries, continue walking |
| `src/system/flow/FlowNode.cpp` | ActivateChild: null guard |
| `src/system/flow/FlowQueueable.cpp` | Deactivate + ChildFinished: InDeleteObjects() early return; ObjPtrList for native |
| `src/system/obj/ObjPtr_p.h` | ReplaceNode duplicate detection; ObjPtrList::insert null skip |
| `src/system/obj/Dir.cpp` | Recursive NullifyAllRefs before mSubDirs.clear(); skip inner Phase 0; Phase 1 null safety |
| `src/system/ui/UIScreen.cpp` | ClearTimelineTasks(seconds + UI) before panel unload; hack removed |

## Verification

- `ninja` — PPC build, zero regressions (same match percentages)
- `HeadlessBootTest.BootReachesChooseModeOnDefaultUnloadPath` — PASSED
- `HeadlessBootTest.BootAndRun100Frames` — PASSED
- Full test suite: 3 pre-existing failures (MoggDecode, SurvivesMainLoop, PoseDump), no new failures

## Key Debugging Insights

1. **File-based logging** was essential. The test spawns `dc3-native` as a subprocess — `fprintf(stderr)` output was captured but `fprintf` to a file (`/tmp/claude-1000/...`) revealed that all 120+ null ObjPtrList inserts had `InDeleteObjects=0`.

2. **GDB** (`gdb -batch -ex run -ex bt`) revealed the crash was in `NullifyAllRefs` during inner DeleteObjects, not in the Phase 1 destructor calls as initially suspected.

3. The **mSubDirs** list was empty by the time `DeleteObjects` ran, because `~ObjectDir` clears it first. This broke the recursive ObjDirItr approach — had to move recursive nullification before `mSubDirs.clear()`.

4. Even with recursive NullifyAllRefs covering 164 objects across 5 dirs, **AnimTask's target was still not found**. The final fix (`ClearTimelineTasks`) is a higher-level approach that sidesteps the ring traversal entirely. Scoped to seconds + UI timelines to minimize blast radius.
