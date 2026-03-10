# DTA Execution — Native Port Status

## Update (Session 39, late): DTA IS Working!

**Critical discovery**: DTA TypeDefs ARE loaded and handlers DO execute on native. The system config DTAs load from the ark, screen TypeDefs are applied via `SetType()`, and `HandleType()` fires for enter/exit/load/sync_objects messages. For example, `main_panel.enter` executes 23 DTA commands, `autosave_warning_screen.enter` executes 17 commands.

The issue is NOT that DTA doesn't load — it's that specific commands within DTA scripts fail silently (likely due to missing Xbox-specific object references like `$profile_mgr`, `$content_mgr`). Commands after the failure may be skipped if they're inside a conditional that depends on the failed result.

## What is DTA?

DTA (Data Array) is Harmonix's custom scripting/configuration format used throughout the Milo engine. On Xbox, DTA files are compiled into binary `.dtb` format and stored inside `.ark` archives. They control nearly all game behavior above the engine level.

## Remaining issues

Some DTA-driven behaviors don't work because:

1. **Route button messages** — `UIManager::mSink` is set exclusively by a `set_sink` DTA handler. Without it, we need native-only fallback dispatch code.

2. **Complete animation lifecycles** — DTA event handlers call `StopAnimation()` after enter animations finish. Without this, `AnimTask` objects persist forever (since `mAnimTarget` is non-null, `AnimTask::Poll` never self-deletes). We bypass `IsAnimating()` on native, but this means timing-dependent UI logic is broken.

3. **Populate content** — List items, song lists, mode definitions, and provider configurations come from DTA. The main menu shows 5 items on native only because the `HamNavProvider` has a default count.

4. **Drive screen transitions** — Screen flow (`next_screen`, `back_screen`, transition triggers) is defined in DTA. We use a timer-based auto-advance as a workaround.

5. **Initialize UI state** — Focus management, component wiring, panel enter/exit behavior, help bar configuration.

## Where DTA gets set in the codebase

### mSink (button routing)
```
src/system/ui/UI.cpp:806
    HANDLE_ACTION(set_sink, mSink = _msg->Obj<Hmx::Object>(2))
```
Only set via DTA message. No code path assigns mSink otherwise.

### Animation lifecycle
```
src/system/flow/FlowAnimate.cpp:280
    mAnimTask->mAnimTarget = NULL;  // Allows AnimTask::Poll to self-delete
```
FlowAnimate (DTA-driven) explicitly nulls `mAnimTarget` when done. Other code paths (like `HamNavList::PlayEnterAnim()`) never do this.

### Screen flow
```
src/system/ui/UI.cpp:772  (Init)
    static Message init("init");
    Hmx::Object::Handle(init, false);
```
The `"init"` broadcast triggers DTA handlers that set up screens, transitions, and state.

## Architecture of DTA loading on Xbox

1. **Boot**: `App::Init()` → `SystemInit()` → loads `system.dta` from ark
2. **UI Init**: `UIManager::Init()` → broadcasts `"init"` message → DTA handlers fire
3. **Screen Load**: `UIScreen::Load()` → loads screen's `.milo` + associated `.dta`
4. **ObjectDir Load**: `ObjectDir::Load()` → loads `.milo` which may embed inline DTA
5. **Runtime**: DTA handlers respond to messages (`button_down`, `screen_enter`, `anim_done`, etc.)

## What we have (more than expected!)

- **DataArray parser**: Fully implemented, can parse binary `.dtb` format
- **BlockMgr/ArkFile**: Can read `.ark` archives and extract files
- **Archive::Enumerate()**: Lists files in ark, auto-translates `.dta` → `.dtb` in `/gen/` subdirs
- **System DTA IS loaded**: `SystemPreInit("config/ham_preinit_keep.dta")` + `SystemInit("config/ham_keep.dta")` both load from ark on native. `SystemConfig("ui")` returns valid config. `SetTypeDef(SystemConfig("ui"))` applies the UI TypeDef.
- **Handle/Export**: Message dispatch infrastructure works
- **TypeDef system**: TypeDefs from DTA are applied to objects and their handlers fire

### Key DTB files in the ark
- `config/gen/ham_preinit_keep.dtb` — Pre-init config (loaded)
- `config/gen/ham_keep.dtb` — Main system config (loaded)
- `config/gen/campaign.dtb` — Campaign data
- `config/gen/macros.dtb` — Macro definitions (CHARACTERS, CREWS)
- Various screen-level DTAs inside `.milo` archives

## What we're missing

The system config IS loaded, but:

- **TypeDef init handler may reference missing objects**: The `(ui (...))` TypeDef's `(init ...)` handler may reference objects that don't exist on native (e.g., game-specific managers). If `set_sink` is inside an init handler that references a missing object, it fails silently.
- **Screen-level DTAs**: Each `.milo` screen may embed DTA with enter/exit handlers. These DTAs define screen transitions, animation lifecycle callbacks, and `set_sink` targets.
- **Per-object DTA loading**: When an ObjectDir loads a `.milo`, it should also load the embedded DTA. This may or may not be happening.
- **DTA execution context**: Some DTAs reference game-specific globals/objects that don't exist on native
- **Content system DTAs**: Song database, DLC, profile data — all DTA-driven

### Diagnostic results (Session 39)
- `SystemConfig("ui")` TypeDef has 23 entries, including a massive `(init ...)` handler with 634 commands
- **`set_sink` is NOT in the system config DTA** — scanned all 634 init commands, none match
- **`set_sink` never fires during native runtime** — traced the handler, zero calls
- **Conclusion**: `set_sink` is sent from a screen-level or `.milo`-embedded DTA, not the system config
- **Current fix**: Native sets `mSink = screen` directly in transition code (UIManager.cpp), removing the need for the DTA `set_sink` action entirely

## Proposed approach

### Option A: Full DTA loading (ideal)
Load `.dtb` files from ark alongside `.milo` files. Execute DTA init handlers. This would make mSink, animation lifecycle, and content population work naturally.

**Pros**: Most correct, removes all native workarounds
**Cons**: May require stubbing game-specific DTA commands, significant effort

### Option B: Targeted DTA extraction (pragmatic)
Extract key DTA configs (screen definitions, UI init) and hardcode their effects in native-only code. Keep the fallback dispatch for mSink.

**Pros**: Faster to implement, fewer unknowns
**Cons**: Fragile, won't scale to full game screens

### Option C: Hybrid (recommended)
1. Implement DTA autoload for ObjectDir (loads `.dtb` from ark when `.milo` loads)
2. Add native-safe execution (skip commands that reference missing Xbox subsystems)
3. Keep native fallbacks as safety net

## Files to investigate

- `src/system/obj/Dir.cpp` — ObjectDir::Load, where DTAs should be autoloaded
- `src/system/obj/Data.cpp` — DataArray loading/parsing
- `src/system/os/ArkFile.cpp` — Ark archive reading
- `src/system/os/BlockMgr.cpp` — File manager that routes reads through ark
- `config/` — If DTA source files exist here
- `orig-assets/gen/main_xbox.hdr` — Ark header listing all files
