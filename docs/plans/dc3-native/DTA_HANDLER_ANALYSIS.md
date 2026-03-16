# DTA Handler Execution — Root Cause Analysis

**Last Updated**: 2026-03-16
**Status**: Research complete — animation issue is NOT a DTA problem

## Revised Finding (2026-03-16)

**The animation completion issue is NOT caused by missing DTA handlers.**

### Evidence

1. Added `ContextCheckerInit()` and `MidiParser::Init()` to native init path — no change in behavior
2. Added diagnostic logging to `AnimTask::Poll()` dispatch:
   - `mTypeDef` is **null for all animated objects** — confirmed via `fprintf(stderr, "typeDef=%p")`
   - `on_anim_event` is **not defined in ANY DTA config file** (objects.dta, ham_objects.dta, etc.)
   - The `on_anim_event` message returns `kDataUnhandled` (type=0) for every listener
3. This means `on_anim_event` is unhandled on **both Xbox and native** — it's not a DTA regression

### Real Root Cause: mAnimTarget Lifecycle

The `AnimTask::Poll()` dispatch block (Anim.cpp:435-446) only executes when `mAnimTarget` is null:

```cpp
if (!mAnimTarget) {  // Only runs when mAnimTarget is null
    if (!mLoop && !mBlending && !mBlendPeriod) {
        if (time > mFrameSpan || mScale == 0.0f) {
            if (mListener) {
                mListener->Handle(msg, false);  // on_anim_event
            }
            mListener = nullptr;
            TheTaskMgr.QueueTaskDelete(this);  // Task removes itself
        }
    }
}
```

On **Xbox**: `mAnimTarget` becomes null through some mechanism (target object destruction, parent cleanup, or a message handler that nulls the reference). This allows the dispatch block to run and the task to self-delete.

On **native**: `mAnimTarget` stays non-null (the ObjPtr reference persists because object destruction timing differs). The dispatch block never runs, so `IsAnimating()` stays true forever.

### The Hack is Correct

The auto-null hack at Anim.cpp:426-434 is the **correct fix**:

```cpp
#ifdef HX_NATIVE
if (mAnimTarget && !mLoop && !mBlending && !mBlendPeriod) {
    if (time > mFrameSpan && mFrameSpan > 0.0f) {
        mAnimTarget = NULL;  // Force completion
    }
}
#endif
```

This forces `mAnimTarget` to null when a non-looping animation exceeds its frame span, allowing the natural task self-deletion path to execute. This is safe because:
- The animation has already played past its end
- Non-looping animations should complete and clean up
- The target is no longer needed

### What This Changes

**DTA handler execution is still incomplete on native** (mTypeDef is null for most objects), but this is **not the cause of the animation hang**. The Init calls added to App.cpp (ContextCheckerInit, MidiParser::Init, DirLoader::SetPathEvalCallback) are still valuable for:
- MidiParser object deserialization from .milo files
- DTA script function availability
- Content path resolution filtering

But they don't fix the animation issue, and removing the Anim.cpp hack is **not recommended**.

---

## mTypeDef Status

Diagnostic confirmed: `mTypeDef` is null for animated objects loaded from .milo files. Possible reasons:
1. Objects are loaded with a null type Symbol (empty type name → `SetType(Symbol(""))` → `SetTypeDef(nullptr)`)
2. Type names exist but the corresponding config entries are missing from objects.dta
3. Object types are not configured in the system config hierarchy

This is a lower-priority issue — most game functionality works without DTA type definitions. The main impact is:
- No DTA-defined message handlers on objects
- No custom property type behaviors
- No script-driven object configuration overrides

## Init Calls Added (2026-03-16)

Added to `App.cpp` HX_NATIVE init block (after `GameInit()`):

| Init Call | Purpose | Status |
|---|---|---|
| `MidiParser::Init()` | Register MidiParser factory + 14 script variable caches | ADDED, builds clean |
| `DirLoader::SetPathEvalCallback(IsUselessLoad)` | Filter unnecessary asset loads by game mode | ADDED, builds clean |
| `ContextCheckerInit()` | Register 5 DTA script functions (random_context, etc.) | ADDED, builds clean |

All three Init calls are header-included and compile without issues on native. 500-frame smoke test passes.

## Related Hacks

| Hack | File | Lines | Removable? |
|------|------|-------|:---:|
| AnimTarget auto-null | Anim.cpp | 426-434 | **NO** — correct fix for native timing |
| kRibbonSelect IsAnimating skip | HamNavList.cpp | 505-509 | NO — depends on animation completion |
| Audio timeout bypass | Game.cpp | 805-825 | NO — async loading difference |
