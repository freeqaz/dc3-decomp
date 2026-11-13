# Lane notes: `src/App.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/App.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

File-level (was lines 48-58; nearest following definition: `NativeSettingsObject::NativeSettingsObject`):

```text
// w8-e 2026-09-15: ??_H@YAXPAXIHP6APAX0@Z@Z (64 B, 0%) is MSVC's CRT "vector
// constructor iterator" helper -- the thunk `new T[n]` emits to run T's ctor
// over n elements.  ham_xbox_r.map contributes it from `App.obj` at 823313a8
// (`f i`, a COMDAT of this TU), so the image really does `new T[n]` for some
// class-type T somewhere in this file.  Our App.cpp never array-news a class
// type (every array here is either POD or a std:: container), so the helper is
// never emitted and the row reads 0.0%.  This is NOT a missing body -- ??_H is
// CRT code we cannot write; the lever is the `new T[n]` site itself.  Note the
// sibling ??_M / ??_L helpers are absent from App.obj in the map, which says the
// array is constructed but never destroyed through a ??_M path (a leaked or
// never-freed table), a useful constraint when hunting the site.
```

File-level (was lines 1501-1508; nearest following definition: `Dc3W8jArrayProbeElem::Dc3W8jArrayProbeElem`):

```text
// w8-j: orphan-instantiation probe for ??_H@YAXPAXIHP6APAX0@Z@Z (0x823313A8,
// 64 B) -- MSVC's CRT "vector constructor iterator", the helper `new T[n]` emits
// to run T's constructor over n elements.  ham_xbox_r.map contributes it from
// App.obj and nothing in App.s calls it, so the array-new that emitted it was
// dropped by /OPT:REF.  ??_H, not ??_L: the EH form is chosen when the element
// type needs unwinding, so the real T had a constructor and NO destructor -- and
// consistently, the map attributes no ??_M / ??_L to App.obj either, i.e. the
// array is constructed and never destroyed through a ??_M path.
```


## `??0App@@QAA@HPAPAD@Z`

`App::App` -- moved verbatim from `src/App.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 443-447):

```text
    // Image order from here: SetPathEvalCallback right after GameInit, then
    // ContextCheckerInit, (Kinect sXShowCallback), AccomplishmentManager::Init,
    // MetagameRank::Init. MetagameRank::Preinit already ran in MetaPanel::Init
    // (image position); native used to call it a second time here.
    // FixedSizeSaveable/HamUserMgr run earlier, after SystemInit (w23-g12).
```

Inside the body (was lines 474-491):

```text
    // Register smart stub objects for DTA scripts that reference Xbox managers.
    // These return sensible defaults so DTA handlers execute correctly instead
    // of silently failing. See DTA_FLOW_V2_PLAN.md Phase 1.
    // Registered BEFORE the UI init: in the image the real managers are named
    // long before it (SaveLoadManager right after the common bank, SpeechMgr
    // in the Kinect init), and the UI init's DTA already runs.
    // content_mgr/challenges already exist by now (ContentMgr/Challenges
    // Init), so those stubs are deleted again, as before. There is no
    // profile_mgr stub any more: ProfileMgr::InitNative (MetaPanel::Init)
    // names the real object, so DTA reaches the image's ProfileMgr handlers
    // (w23-misc). There is no platform_mgr stub any more: native
    // SystemInit runs ThePlatformMgr.Init (w23-g14), which names the real
    // object "platform_mgr" (PlatformMgr_Native.cpp, w23-si), so DTA reaches
    // the image's PlatformMgr handlers.  There is no saveload_mgr stub either
    // any more: SaveLoadManager::Init ran above (after the common bank, where
    // the image runs it), so DTA reaches the real manager's handlers
    // (activate / is_idle / is_initial_load_done / is_autosave_enabled /
    // manual_save / enable_autosave / disable_autosave).
```


## `Dc3NativeFramePoll()`

`Dc3NativeFramePoll` -- moved verbatim from `src/App.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Attribution: no function named `Dc3NativeFramePoll` in report.json (unit); filed under the source name

Above the definition (was lines 777-829):

```text
// w22-ml: the per-frame poll sequence of the image's App::RunWithoutDebugging
// (?RunWithoutDebugging@App@@QAAXXZ, the do/while body), shared by BOTH native
// loops -- the desktop/headless loop in RunWithoutDebugging below and the web
// loop's RunOneFrame. Native cannot run the image's loop body itself (it draws
// through DrawRegular with no crash guard / ImGui / HTTP hooks, and derefs
// managers native never creates), so this mirrors its POLL part call for call,
// in the image's order. What the image does, per frame, from the target listing:
//
//   SystemPoll(false)                                   bl ?SystemPoll@@YAX_N@Z
//   "misc_poll": TheAchievements->Poll()                bl ?Poll@Achievements@@QAAXXZ
//                TheAccomplishmentMgr->Poll()           bl ?Poll@AccomplishmentManager@@QAAXXZ
//                if (TheLeaderboards) ->Poll()          beq cr6 / bl ?Poll@Leaderboards@@QAAXXZ
//                if (TheChallenges) ->Poll()            beq cr6 / bl ?Poll@Challenges@@QAAXXZ
//                TheSaveLoadMgr->Poll()                 bl ?Poll@SaveLoadManager@@QAAXXZ
//   "synth_poll": TheSynth->Poll()                      bctrl
//   "rock_central_poll": TheRockCentral.Poll()          bl ?Poll@RockCentral@@QAAXXZ
//   "gesture_poll": TheGestureMgr->Poll()               bl ?Poll@GestureMgr@@QAAXXZ
//   TheUI->Poll()                                       bctrl
//   $hud_panel <- "update_all_flashcard_dance_pct"      DataVariable/CompatibleType/
//                                                       __RTDynamicCast PanelDir/Handle
//   TheTaskMgr.Poll(); TheFlowMgr->Poll()               bl ?Poll@TaskMgr / ?Poll@FlowManager
//   "skeleton_post_update": SkeletonUpdate PostUpdate   bl ?PostUpdate@SkeletonUpdateHandle
//   FileDiscSpinUp()                                    bl FileDiscSpinUp
//   then CaptureHiRes()/DrawRegular() and the GLITCH report (callers' business).
//
// Before w22-ml the native loops ran only SystemPoll, Gesture, UI, Task, Flow,
// Synth (Synth LAST, where the image polls it before the UI), and skipped the
// misc/RockCentral/skeleton polls and the hud_panel message entirely -- nothing
// else sends update_all_flashcard_dance_pct (ui/hud/hud_objects.dta:1011), so a
// mystery flashcard's dance_pct.anim never moved on native. None of the skips
// had a recorded reason (git log -S: the native loop was written from scratch in
// 3e7fed8b5 and Synth appended in c09c65e61 with no ordering rationale).
//
// Native-only differences that remain, each because native lacks the subsystem:
//  - TheSaveLoadMgr is the real manager (SaveLoadManager::Init runs in the
//    native ctor arm); the null check stays because Poll is also reached from
//    milo-tests, which never constructs App.
//    TheLeaderboards is null on native (MetaPanel::Init, #ifndef HX_NATIVE); the
//    image already guards it.
//  - TheRockCentral is polled un-Init'd: ThePlatformMgr.IsConnected() is false on
//    native, so it never logs in; its only other work goes to DingoServerNative,
//    whose ManageJob/Poll are no-ops (native/src/platform/DingoSvr_Native.cpp).
//  - SkeletonUpdate is created natively by GestureMgr::Init (behind the native
//    sensor CameraInput, GestureMgr_Native.cpp), so skeleton_post_update runs
//    the image's whole skeleton step: sensor frame -> Update -> every
//    callback's Update, then PostUpdate in registration order.
//  - The gesture poll keeps its DC3_NATIVE_SCORING / DC3_POSE_SELFTEST gate.
//  - The callers keep their own draw (crash guard, ImGui, HTTP, PumpAudio) in
//    place of DrawRegular, and do not reproduce TheHiResScreen/CaptureHiRes or
//    the image's per-frame "GLITCH: %g ms" report (a frame over 83.33 ms minus
//    the slow-frame waiver shows text on cheat_display when notify_level != 0,
//    which native sets to 1): native frame time is not Xbox frame time, and
//    headless/uncapped runs would put dev-kit overlay text on screen.
```


## `?RunWithoutDebugging@App@@QAAXXZ`

`App::RunWithoutDebugging` -- moved verbatim from `src/App.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 1195-1202):

```text
// w19-e (99.25, 24 rows, all in the loop preheader 18-68): the image and we
// hoist the SAME loop-invariant addresses into the SAME callee-saved
// registers and frame slots (0x60 "none", 0x70 Message vtable, 0x78 PanelDir
// RTTI, 0x7c "ACTIVE %s", 0x90 sSlowFrameTimer, 0xa0 Object RTTI); only the
// ORDER of the lis/addi/stw pairs differs (image: sSlowFrameTimer, Object
// RTTI, PanelDir RTTI, ... ; ours: Message vtable, PanelDir RTTI, ...).
// Inert: the inner do/while rewritten as a block + `if (glitch <= ...)
// continue;`.
```
