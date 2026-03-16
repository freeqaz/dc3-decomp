# DTA Handler Execution — Root Cause Analysis

**Last Updated**: 2026-03-16
**Status**: Research complete — actionable fix path identified

## Problem Statement

Multiple `#ifdef HX_NATIVE` hacks exist because DTA (DataArray) script handlers don't fire on the native port. The most visible symptom: `transition_complete` and `on_anim_event` callbacks never execute, causing `RndAnimatable::IsAnimating()` to return true forever.

## Expected Execution Path (Xbox)

```
1. AnimTask::Poll() (Anim.cpp:437-444)
   → Animation completes (time > mFrameSpan)
   → static Message msg("on_anim_event", DataNode(Symbol("ended")))
   → mListener->Handle(msg, false)

2. Object::Handle() (Object.h:695-702)
   → HANDLE_ARRAY(mTypeDef) macro expands to:
   → if (mTypeDef && (found = mTypeDef->FindArray(sym, false)))
   →   found->ExecuteScript(1, this, _msg, 2)

3. DTA Script Execution
   → The typedef's DTA handler calls StopAnimation()
   → StopAnimation() removes all AnimTask references
   → IsAnimating() returns false

4. HamNavList::Poll() checks IsAnimating()
   → Returns false → kRibbonSelect completes → transition to kRibbonSwell
```

## Where It Breaks on Native

### Primary Cause: mTypeDef is Null

Objects loaded from `.milo_xbox` files have their `mTypeDef` populated from DTA type definitions. The type definition is a `DataArray*` containing message handler scripts.

On Xbox, type definitions are loaded from:
1. `config/objects.dta` — base object type definitions
2. Per-unit `.dta` files — specialized handler overrides
3. `.milo_xbox` embedded type data

On native, `mTypeDef` is likely null because:
- Type definition loading may require DTA functions that aren't registered (see missing Init calls)
- `objects.dta` parsing may silently fail on unregistered function references
- The `SetTypeDef()` call during object construction may not find the matching typedef

### Secondary Cause: Missing DTA Function Registrations

`ContextCheckerInit()` registers 5+ DTA script functions via `DataRegisterFunc()`:
- `random_context`
- `random_context_allow_failure`
- `seed_random_context`
- `handle_context_used`
- `random_context_count`

If a DTA handler script references any unregistered function, `ExecuteScript()` may:
1. Call `MILO_FAIL_DTA()` (non-fatal on native) and return early
2. Skip the handler entirely
3. Execute partially, missing the `StopAnimation()` call

### Tertiary Cause: ExecuteScript Silent Failure

Under `MILO_FAIL_DTA` (non-fatal mode, the default on native), script execution errors don't crash — they log a warning and continue. This means:
- Handler references to missing objects → warning + skip
- Handler calls to unregistered functions → warning + skip
- Net result: the handler "fires" but doesn't actually do anything

## Verification Steps for Future Agents

### Step 1: Check mTypeDef Population

Add diagnostic logging:
```cpp
// In Object.cpp or Object.h, HANDLE_ARRAY macro
if (!mTypeDef) {
    fprintf(stderr, "HANDLE_ARRAY: mTypeDef is null for %s (class %s)\n",
            Name(), ClassName());
}
```

Run the engine and check if objects that should have DTA handlers have null `mTypeDef`.

### Step 2: Add ContextCheckerInit() to Native Init

In `App.cpp`, inside the `#ifdef HX_NATIVE` block, add:
```cpp
ContextCheckerInit();  // Register DTA script functions
```

This may require stubbing dependencies. Check what `ContextCheckerInit()` needs.

### Step 3: Add MidiParser::Init() to Native Init

```cpp
MidiParser::Init();  // Register MidiParser object factory
```

### Step 4: Test AnimTask Event Dispatch

After adding Init calls, check if `on_anim_event("ended")` actually dispatches through to the DTA handler:
```cpp
// In Anim.cpp:440, after mListener->Handle(msg, false)
DataNode result = mListener->Handle(msg, false);
fprintf(stderr, "AnimTask: on_anim_event dispatch result: %d (DATA_HANDLED=%d)\n",
        result.Int(), kDataHandled);
```

If the result is `kDataUnhandled`, the handler isn't being found/executed.

### Step 5: Check objects.dta Loading

Verify that `config/objects.dta` parses successfully on native:
- Run with `MILO_FATAL_FAILS=0` and grep for DTA errors
- Check if type definitions are populated after SystemInit

## Fix Path

**Optimistic path** (if mTypeDef population is the issue):
1. Add missing Init calls → DTA functions register → objects.dta parses fully → mTypeDef populates → handlers fire → remove hacks

**Pessimistic path** (if deeper issues exist):
1. The Anim.cpp auto-null hack is safe and correct — AnimTask auto-completes when animation finishes
2. The HamNavList IsAnimating skip is safe — transitions complete without waiting for DTA confirmation
3. Keep both hacks as permanent platform differences

## Related Hacks

| Hack | File | Lines | Removable If DTA Fixed? |
|------|------|-------|:---:|
| AnimTarget auto-null | Anim.cpp | 426-434 | YES |
| kRibbonSelect IsAnimating skip | HamNavList.cpp | 505-509 | YES |
| kRibbonSelect gesture skip | HamNavList.cpp | 1430-1440 | YES |
| Audio timeout bypass | Game.cpp | 805-825 | MAYBE (depends on load_new_song script timing) |

## Key Files

| File | What to check |
|------|--------------|
| `src/system/rndobj/Anim.cpp:426-444` | AnimTask::Poll — event dispatch + native hack |
| `src/system/obj/Object.h:695-702` | HANDLE_ARRAY macro — typedef dispatch |
| `src/system/obj/Object.cpp:SetTypeDef` | Where mTypeDef gets populated |
| `src/App.cpp:233-346` (HX_NATIVE block) | Missing Init calls |
| `src/system/hamobj/HamNavList.cpp:505,1522` | kRibbonSelect hacks |
| `src/lazer/game/Game.cpp:805-825` | Audio timeout hack |
