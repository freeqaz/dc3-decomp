# MetaMusic DTA Convergence Plan

**Date**: 2026-03-24
**Status**: Investigation complete, implementation pending
**Problem**: Main menu audio continues playing during gameplay on native/web

## Xbox Architecture (Ground Truth)

The Xbox build uses a clean 3-part DTA architecture to stop menu music:

### 1. `loading_panel` enter trigger (loading.dta:20)

When the loading screen enters, its DTA handler explicitly stops menu music:

```dta
{new LoadingPanel loading_panel
   (enter
      {meta music_stop}    ;; <-- THE TRIGGER
      {meta init_songpreview}
      ...)}
```

### 2. MetaPanel `music_stop` handler (meta.dta:79-90)

MetaPanel defines a `music_stop` DTA handler:

```dta
{new MetaPanel meta
   (exit
      {set [is_crowd_playing] FALSE}
      {$this music_stop})       ;; Also called on panel exit
   (music_stop
      {do
         ($music {$this meta_music})
         {hamprovider set shellmusic_on FALSE}
         {if $music {$music stop}}     ;; -> MetaMusic::Stop()
         {set [is_crowd_playing] FALSE}
         {platform_mgr disable_xmp}})
   (music_start
      {do
         ($music {$this meta_music})
         {if {! $mute_shell_music}
            {if $music {$music start}}
            {hamprovider set shellmusic_on TRUE}
            ...
            {platform_mgr enable_xmp}}})}
```

### 3. `meta_game` panel — fade poller (meta.dta:1-20)

A separate UIPanel that polls MetaMusic and gates screen transitions:

```dta
{new UIPanel meta_game
   (poll
      {do
         ($music {meta meta_music})
         {if_else $music {$music poll} 0}})   ;; Calls MetaMusic::Poll()
   (exiting
      {do
         ($music {meta meta_music})
         {if_else $music {$music is_active} 0}})}  ;; Gates transition
```

### Screen Panel Assignments

| Screen | Has `meta`? | Has `meta_game`? |
|--------|------------|-----------------|
| main_screen, choose_mode, song_select, etc. | YES | no |
| meta_loading_* screens | YES | no |
| loading_screen, real_loading_screen | no | no |
| game_screen | no | no |
| endgame screens | no | YES |

### Xbox Transition Flow

```
menu_screen (meta) → meta_loading_screen (meta) → loading_screen → game_screen
                                                     ↑
                                        loading_panel enters:
                                        {meta music_stop} fires
```

1. `meta_loading_screen` exits to `loading_screen`
2. `meta` panel is NOT on `loading_screen` → `MetaPanel::Exit()` is called
3. `UIPanel::Exit()` → `HandleType("exit")` → DTA `(exit {$this music_stop})`
4. `MetaMusic::Stop()` starts a 1-second fade
5. Additionally, `loading_panel` enter fires `{meta music_stop}` as a safety net
6. `meta_game` panel (on endgame screens) polls MetaMusic to complete the fade

## Why It Doesn't Work on Native

### DTA Infrastructure IS Functional

The investigation confirmed:
- DTA files ARE loaded on native (`ham_keep.dta` → `ui.dta` → `init.dta` → `meta.dta`)
- `HandleType()` has NO native guards — it works unconditionally
- `UIPanel::Enter/Exit/Poll()` all call `HandleType()` unconditionally
- Panel objects ARE created from DTA `{new ...}` declarations with TypeDefs set

### The C++ Handlers ARE Wired

MetaPanel's C++ handlers support the DTA chain:
```cpp
BEGIN_HANDLERS(MetaPanel)
    HANDLE_EXPR(meta_music, TheMetaMusic)    // {$this meta_music} → returns TheMetaMusic
    ...
END_HANDLERS

BEGIN_HANDLERS(MetaMusic)
    HANDLE_ACTION(stop, Stop())              // {$music stop} → MetaMusic::Stop()
    HANDLE_ACTION(start, Start())            // {$music start} → MetaMusic::Start()
    HANDLE_ACTION(poll, Poll())              // {$music poll} → MetaMusic::Poll()
    HANDLE_EXPR(is_active, IsActive())       // {$music is_active} → bool
    ...
END_HANDLERS
```

### Likely Root Causes

1. **Native screen flow bypasses loading_screen**: The native port may skip the
   `meta_loading → loading_screen` flow entirely, going straight to `game_screen`.
   If so, `loading_panel` never enters and `{meta music_stop}` never fires.

2. **MetaPanel::Exit() game_screen guard**: Even if the panel lifecycle fires Exit(),
   MetaPanel has a guard that returns early if `BottomScreen() == "game_screen"`.
   During normal transition this shouldn't trigger (BottomScreen is still the old screen),
   but if native does something non-standard it could.

3. **`meta_game` panel may not be on active screens**: On Xbox, `meta_game` is only
   on endgame/campaign screens — it's NOT on the menu→game transition path. The fade
   completion during menu→game relies on `Fader::SynthPoll()` (automatic via SynthPollable)
   and `StandardStream::UpdateVolumes()`. If those work, the audio fades to silence
   even without `meta_game` polling. The issue is that `MetaMusic::Poll()` never
   finalizes the stream (calls `stream->Stop()`), leaving it registered with AudioDevice.

## Proposed Fix: Wire Up DTA Properly

### Step 1: Verify DTA handler execution

Add printf tracing to confirm whether DTA handlers fire on native:

```cpp
// In LoadingPanel::Enter(), after UIPanel::Enter():
printf("LoadingPanel::Enter() — checking if DTA (enter) handler fired music_stop\n");
```

Also check: does the native port go through `loading_screen` at all? Print screen
transitions in `UIManager::GotoScreenImpl()` (already has native debug prints).

### Step 2: If DTA handlers DO fire

The fix is simple — remove the `Kill()` band-aid from `GamePanel::Enter()` and let
the DTA flow work naturally. The remaining issue is `MetaMusic::Poll()` not being
called to finalize the stream. Fix by either:

a. Making `meta_game` panel active during the transition (add it to loading screens), or
b. Having `MetaPanel::Poll()` still call `MetaMusic::Poll()` during game_screen
   (remove the early return, but skip everything else), or
c. Making `MetaMusic::Stop()` immediately stop the stream (like `Kill()`) instead of
   fading — acceptable since loading screen has its own music anyway.

### Step 3: If DTA handlers DON'T fire

Investigate why. Likely candidates:
- `ObjectDir::FindObject("meta")` returns null (panel not in scope)
- `ExecuteScript` fails silently on some DTA construct
- TypeDef not set due to native-specific `SetTypeDef` override

If DTA is fundamentally broken, replicate the behavior in C++ at the right points:
- `LoadingPanel::Enter()`: add `TheMetaMusic->Kill()` (mirrors `{meta music_stop}`)
- `MetaPanel::Enter()`: already has `TheMetaMusic->Start()` for native (existing code)

### Step 4: Convergence with `music_start` / `music_stop`

Once music stop works, also converge music START:
- Currently: `MetaPanel::Enter()` has `#ifdef HX_NATIVE` calling `TheMetaMusic->Start()`
- Xbox DTA: `(music_start)` handler on MetaPanel, called from various screen enters
- If DTA works: remove the `#ifdef` and let DTA handle it
- If DTA doesn't work: keep the C++ fallback but add equivalent logic for all
  screens that call `{meta music_start}` on Xbox

## Key Files

| File | Role |
|------|------|
| `orig-assets/extracted/ui/meta.dta` | MetaPanel + meta_game DTA definitions |
| `orig-assets/extracted/ui/loading/loading.dta` | loading_panel DTA with `{meta music_stop}` |
| `orig-assets/extracted/ui/game.dta` | game_screen definition (no meta/meta_game) |
| `orig-assets/extracted/ui/meta_loading.dta` | meta_loading screen definitions |
| `src/lazer/meta_ham/MetaPanel.cpp` | MetaPanel C++ with game_screen guards |
| `src/lazer/meta_ham/LoadingPanel.cpp` | LoadingPanel C++ |
| `src/lazer/game/GamePanel.cpp` | GamePanel with current Kill() band-aid |
| `src/system/synth/MetaMusic.cpp` | MetaMusic with Stop/Kill/Poll |
| `src/system/ui/UIPanel.cpp` | UIPanel lifecycle (HandleType calls) |
| `src/system/ui/UIScreen.cpp` | Screen transitions (Exit → panel Exit) |
| `src/system/obj/Object.cpp:737` | HandleType() — executes DTA handlers |

## Quick Debug Checklist

- [ ] Does `loading_screen` appear in native screen transition logs?
- [ ] Does `LoadingPanel::Enter()` fire on native?
- [ ] Does `HandleType("enter")` in `UIPanel::Enter()` execute DTA handlers?
- [ ] Does `{meta music_stop}` resolve `meta` to the MetaPanel object?
- [ ] Does `MetaMusic::Stop()` actually get called?
- [ ] After Stop(), does `Fader::SynthPoll()` advance the fade?
- [ ] After fade completes, is the stream still registered with AudioDevice?
