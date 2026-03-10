# Session 40 Plan — DTA Execution & Interactive Navigation

## Key Discovery (Session 39)

**DTA is NOT broken — it's already working!** The research revealed:

1. **System config DTAs load from ark** — `ham_keep.dtb` and `ham_preinit_keep.dtb` both load successfully
2. **Screen TypeDefs are applied** — Every screen has its TypeDef with `enter`, `exit`, `back`, etc.
3. **DTA handlers execute** — `HandleType` fires for screen enter/exit, panel enter/exit, load, sync_objects
4. **`main_panel.enter` has 23 DTA commands** that execute during main_screen transition
5. **`choose_mode_panel.enter` has 11 DTA commands** that execute

The DTA system works end-to-end. The remaining issues are:
- **`set_sink` is never called** — it's not a top-level handler in any screen TypeDef. It must be called from _within_ a DTA script (e.g., inside `main_panel.enter`'s 23 commands) but the command that does it fails silently because of a missing object reference or unsupported DTA function.
- **DTA script execution errors are silent** — When a DTA command references an object that doesn't exist on native (`$profile_mgr`, `$content_mgr`, etc.), it returns `kDataUnhandled` and continues, but commands after the error may be skipped.

## Plan

### Step 1: Trace DTA Script Execution Failures (30 min)

**Goal**: Find exactly which DTA commands fail and why.

**Approach**: Add error tracing to `DataArray::ExecuteScript` to log when DTA commands fail or reference null objects.

```
File: src/system/obj/Data.cpp
Target: DataArray::ExecuteScript, DataArray::Execute, DataArray::Evaluate
```

Specifically:
- Hook `Hmx::Object::Handle` to trace when `set_sink` is the symbol being handled (we already know it never fires)
- Add native-only trace to `DataArray::Command::Execute` to print commands that throw/fail
- Run and capture the full DTA execution log for `main_panel.enter` (23 commands) and `autosave_warning_screen.enter` (17 commands)

**Expected finding**: DTA commands that reference Xbox-specific managers (`$profile_mgr`, `$speech_mgr`, `$content_mgr`, `$gesture_mgr`) fail, and later commands (including `set_sink`) are skipped because they're inside a conditional that depends on an earlier result.

### Step 2: Fix DTA Execution Blockers (1-2 hours)

**Goal**: Get the DTA `enter` scripts to execute fully, including `set_sink`.

Based on Step 1 findings, likely fixes:
- **Stub missing DTA globals**: Add DataVariable stubs for `$profile_mgr`, `$content_mgr`, etc. that return safe defaults instead of failing
- **Guard DTA conditionals**: If commands fail inside `if` blocks, the issue might be that the condition references a missing object. Fix by ensuring the object exists (even as a stub) or by catching the error gracefully
- **Direct `set_sink` injection**: If `set_sink` is deeply nested in a conditional we can't easily fix, keep the native `mSink = screen` assignment as a permanent solution

### Step 3: Test Interactive Navigation with GPU (30 min)

**Goal**: Verify keyboard input navigates menus visually.

With the IsAnimating() bypass and mSink fix already in place:
- Run with `MILO_RENDER=1` and a GPU window
- Press arrow keys → verify HamNavList highlight changes
- Press Enter/A → verify selection triggers screen transition
- Take screenshots showing navigation state changes

**If keyboard input doesn't work**: Trace the full ButtonDownMsg dispatch chain:
1. Keyboard → Joypad emulation → JoypadClient → UIManager → mSink (screen) → FocusPanel → FocusComponent (HamNavList)
2. Verify each step with printf

### Step 4: Content Population Investigation (1 hour)

**Goal**: Understand why lists show 5 items but no real content.

The 5 items come from `HamNavProvider` defaults. To get real content:
- Trace what `main_panel.enter` DTA script does — does it call `set_provider` or similar?
- Check if `ChooseModeProvider` or similar gets populated via DTA
- Look at what data the `main_panel` HamNavList's provider returns
- If content is DTA-driven, fix the DTA execution path (Step 2) may solve this automatically

### Step 5: Remove `set_sink` Workaround If Possible (15 min)

If Step 2 succeeds and `set_sink` fires naturally:
- Remove the `mSink = trans` assignment in UIManager transition code
- Verify button dispatch still works
- If not needed, remove the workaround

If `set_sink` still doesn't fire naturally:
- Keep the workaround (it's clean and correct)
- Document why it's needed permanently

### Step 6: Screenshot + Document (15 min)

- Capture screenshots showing navigation working (or showing current state)
- Update `NATIVE_PORT_STATUS.md` with session 40 findings
- Update `MEMORY.md` with DTA execution findings
- Update `DTA_LOADING_BLOCKER.md` to reflect that DTA is actually working

## Architecture Notes

### DTA Handler Dispatch Chain
```
Message arrives at object
  → BEGIN_HANDLERS checks C++ HANDLE macros first
  → HANDLE_ARRAY(mTypeDef) checks DTA TypeDef handlers
  → Export() sends to sinks
```

### Screen Enter Flow
```
UIManager::GotoScreenImpl → kTransitionTo
  → Screen loads panels (DirLoader → .milo from ark)
  → Panels enter: UIPanel::Enter → HandleType("enter")
  → Screen enters: UIScreen::Enter → HandleType("enter")
  → DTA scripts execute (23 commands for main_panel.enter)
```

### TypeDef Resolution
```
.milo binary contains type name symbol (e.g., "main_screen")
  → SetType("main_screen") during object load
  → SystemConfig("objects", "UIScreen", "types") lookup
  → FindArray("main_screen") returns TypeDef DataArray
  → SetTypeDef(found) applies it
  → All handlers (enter, exit, button_down, etc.) available
```

## Files to Modify

| File | Change | Purpose |
|------|--------|---------|
| `src/system/obj/Data.cpp` | Add native execution trace | Find failing DTA commands |
| `src/system/obj/DataFunc.cpp` | Stub missing DTA globals | Prevent silent failures |
| `src/system/obj/Object.cpp` | Trace HandleType failures | Debug DTA dispatch |
| `src/system/ui/UI.cpp` | May remove mSink workaround | Cleanup if DTA works |

## Success Criteria

1. **Minimum**: Know exactly which DTA commands fail and why
2. **Good**: `set_sink` fires naturally via DTA, mSink workaround removable
3. **Great**: Keyboard navigation works visually in GPU mode
4. **Excellent**: List items show real content from DTA-driven providers
