# Session: Web Flashcard Diagnosis

**Date**: 2026-03-25
**Status**: In progress — web now has a hack/workaround that renders flashcards, but the underlying gameplay/world readiness issue is still not fixed.

## Problem

Flashcards did not render on the web build during gameplay. Score HUD worked fine.

## Current State

There are two separate facts to keep straight:

1. A workaround currently makes flashcards visible on web/native.
2. The underlying engine path that should drive them on Xbox parity is still broken in at least some gameplay paths.

So this is **not** “fixed” in the architectural sense. The current codebase contains a hack that papers over the symptom.

## Current Workarounds

### 1. Always-on flashcard refresh hack

`GamePanel::Poll()` now keeps forcing `update_flashcards` from C++ for the duration of gameplay when `merge_moves=1`.

That is a workaround for the fact that the normal scripted path:

`select_camera` → `OnSelectCamera` → `songAnim->SetFrame()` → `move_interp`

is not reliably driving flashcards on native/web.

### 2. Flashcard container repositioning

The `hud_left` / `hud_right` scrolling containers are also repositioned in `HamDirector::OnFileMerged` so they appear inside the native HUD camera frustum.

This is a separate visual/layout adjustment, not a fix for the deeper gameplay animation/update issue.

## What The Workaround Proves

The workaround proves that:

- the HUD objects exist
- the flashcard DTA update path can render correct cards
- Better Off Alone move lookup / remixer data is capable of producing real dance moves

It does **not** prove that the native/web engine is following the correct Xbox gameplay update path.

### Diagnostic chain

1. `all.grp showing=1` on card[1] with `cur_move=Roxbury_betteroffalone.move` — DTA data correct
2. `MoveNameFromBeat` returns real dance moves — move graph works
3. `SelectMove` selects dance moves for non-intro measures — remixer works
4. `SaveOriginalMoveParents`: null=0 rest=15 dance=255 — layout lookup works
5. The normal `move_interp` path is not reliable enough to drive flashcards without the C++ workaround

## Previous Misdiagnosis

The earlier conclusion that "all 92 move keys are Rest.move" was caused by a **stale `.o` file** that wasn't recompiled. Once properly rebuilt, the move graph loads correctly on both native and web with 36 parents, 92 variants, 255 dance entries.

## Incomplete Explanation We Should Not Treat As Final Root Cause

`WorldDir::Poll()` only fires `select_camera` when `TheWorld` is null (line 560-574 of `world/Dir.cpp`). During gameplay on native/web, something sets `TheWorld` before the game world polls, causing it to take the `RndDir::Poll()` shortcut (line 562) which skips `select_camera`. This doesn't affect character animation (clips are played via `HamDirector::Poll()` which runs from `RndDir::Poll()`), but it prevents the song anim's `move` prop key interp handler from firing.

That nested-`TheWorld` explanation is plausible, but it is **not sufficient** as the final diagnosis.

Fresh telemetry shows an even more basic problem in at least one current gameplay path: the game can reach `state=playing` without a valid merged world / character / song-anim pipeline at all.

## Files Modified

- `src/lazer/game/GamePanel.cpp` — `NativeHudFlashcardHackRequired()` always-on for merge_moves
- `src/system/hamobj/HamDirector.cpp` — Flashcard container repositioning in `OnFileMerged`

## Deeper Issue Still Open

The strongest current suspicion is that `GamePanel::PollForLoading()` can advance gameplay before the actual gameplay screen has a valid world pipeline:

- `songAnimFrame` can remain stuck at `0`
- `mergerDir=0`
- `clipDir=0`
- player characters can be absent

In that state, the flashcard hack only hides one symptom. The correct fix is likely to make gameplay readiness require a valid loaded world on the gameplay screen, then re-test whether any narrower `select_camera` / nested-`TheWorld` bug still remains.

## Remaining Work

- Fix the deeper gameplay/world readiness issue instead of relying on the flashcard refresh hack
- Re-test `songAnimFrame`, `mMerger`, `typeDef`, characters, and `move_interp` in a fully loaded gameplay world
- Only after that, decide whether a separate `WorldDir::Poll()` / `select_camera` fix is still needed
- Fine-tune flashcard X/Z positions for proper visual layout
- Clean up remaining diagnostic logs

## Follow-up Investigation: Correct Fix vs Current Hack

### The current note's `TheWorld` explanation is not sufficient

Re-reading `WorldDir::Poll()`:

- `select_camera` fires only in the outer-world branch (`TheWorld == nullptr`)
- if `TheWorld` is already set, the world takes the nested shortcut and skips `HandleType(select_camera)`

That part is true. But it is **not enough** to explain the current native failure by itself.

### Telemetry caveat: the harness itself needed correction

One of the telemetry conclusions from earlier in the day was too strong.

The original `state` field in `native/src/telemetry/GameplayTelemetry.cpp` was
using `GamePanel::Unkf8()`, which is **not** the same as `mState ==
kGamePlaying`. That made the old note overconfident about interpreting every
sample as real gameplay. This has now been corrected to use the public
`is_playing` / `in_intro` / `is_game_over` handlers instead.

There is a second telemetry caveat too: the printed `pollEnabled` field is
currently derived from `songAnimFrame > 0`, not from the actual
`HamDirector::mPollEnabled` flag. So it should be treated as “song anim frame
went positive”, not as authoritative proof that director polling was disabled.

### Re-run after fixing the telemetry classifier still fails

Running the existing gameplay telemetry test on the current tree:

```bash
cd native/build && DC3_GAMEPLAY_TESTS=1 ./milo-tests --gtest_filter='GameplayTelemetryTest.SongAnimAdvances'
```

Current result:

- `songAnimFrame` never increases across 905 telemetry samples
- `typeDef` stays empty for the full run
- `mergerDir=0`
- `clipDir=0`
- `p0=0 p1=0`
- `p0SongAnim=-99`
- `charClipLayers=-1`

So the direct `HamDirector::Poll()` `SetFrame()` experiment is **not**
confirmed as a fix on the current tree.

What we can safely say now is narrower:

- the current automated native path still never shows a live merged gameplay
  world in telemetry
- the direct `SetFrame()` block cannot help in that state, because there is no
  valid song anim / merger / clip pipeline to drive
- the old “telemetry proves premature readiness” claim needs to be softened
  because the harness had its own state-label bug

In other words: the deeper issue is still open, but the exact proof chain has
to be more careful than the previous note claimed.

### Strongest concrete suspect now: `GamePanel::PollForLoading()` gate is too weak

Current code only blocks on `TheHamDirector->IsWorldLoaded()` while the **transition screen** still owns `world_panel`:

```cpp
if (TheUI->TransitionScreen()
    && TheUI->TransitionScreen()->HasPanel(worldPanel)) {
    if (!TheHamDirector->IsWorldLoaded()) return;
}
```

Once that condition becomes false, `GamePanel::PollForLoading()` can proceed to later gates and eventually report ready even if the world pipeline never became valid for the actual gameplay screen.

That is still the strongest concrete engine-level suspect, but it is no longer
proven by telemetry alone.

### Updated fix direction

The current C++ flashcard refresh hack is a symptom-level workaround. The more likely **correct fix** is:

1. Make gameplay readiness require a valid world pipeline on the actual gameplay screen, not only while a transition screen still references `world_panel`.
2. Re-test once `mMerger`, `typeDef`, characters, and `songAnimFrame` are all live during gameplay.
3. Only if flashcards still need the hack after that should we continue chasing a narrower `WorldDir::Poll()` / nested-`TheWorld` / `select_camera` issue.

So the likely ordering is:

- first fix premature game-start / world-readiness gating
- then re-evaluate whether `select_camera` is still missing in a **fully loaded** world

At the moment, the always-on `NativeHudFlashcardHackRequired()` change is
useful as a temporary visual workaround, but it is not the architectural fix.

## `HamDirector::Poll()` `SetFrame()` — Verified Working

### Key discovery: the frame-skip guard was the problem

The initial `SetFrame` experiment had `if (frame > 0.0f && frame != songAnim->GetFrame())`.
Diagnostics showed `calcFrame == curFrame` on every poll — the song anim was already being
advanced by `RndPropAnim::Poll()` (via `StartAnim()` at time 0). So `SetFrame` never fired.

Removing the `frame != curFrame` guard makes `SetFrame` fire every frame unconditionally.
This is correct because `SetFrame` does more than set the frame value — it **evaluates
all prop keys** and **fires interp handlers**. Even if the frame hasn't changed, the
prop key evaluation is what dispatches `move_interp`.

### Web verification (2026-03-25 late)

With the guard removed:
- Flashcard cards render on web (visible in gameplay screenshot)
- `cur_move` errors drop to only the initial 24 (early setup, before remixer)
- `set_campaign` count = 0 — the `GamePanel` flashcard hack's DTA messages are no longer needed
- Characters show correct dance poses (clip player working via SetFrame)

### Answer to the three possibilities

The answer is **#3**: gameplay world IS valid (merger non-null, songAnim non-null,
mPollEnabled=1), but `select_camera` / song-anim evaluation was not firing because
the `world_panel` is a PanelDir not a WorldDir. The `SetFrame` in `HamDirector::Poll()`
is the correct architectural fix.

## `update_all_flashcard_campaign_status` Cascade Bug (2026-03-26)

### Discovery

Crash log analysis (`tmp/web-crash-endofsong1.txt`) revealed the flashcard hack was
causing massive per-frame log spam via two unhandled messages:

1. **`set_campaign` (8x/frame)** — from `hud_objects.dta:1062`
2. **`get_mastery_moves` (1x/frame)** — from `campaign_vo.dta:240`

Both stem from **one root cause**: the hack was sending `update_all_flashcard_campaign_status`
to the HUD every beat. On Xbox this message only fires during campaign mode when a move is
mastered (from `perform.dta:217`, gated by `metamode == campaign_perform` AND
`meta_performer was_last_move_mastered`). The hack was blasting it unconditionally in quickplay.

### The DTA cascade

```
RefreshNativeHudFlashcards (C++ hack, every beat)
  → sends update_all_flashcard_campaign_status to $hud_panel
    → hud_objects.dta:1062 handler checks metamode
      → non-campaign path (quickplay always takes this):
        foreach flashcard: {$this set_campaign FALSE FALSE FALSE}
        ^^^^^^^^ BUG: $this = HUD PanelDir, not $flash_card
        → "unhandled msg: set_campaign" x8 (one per flashcard)
      → line 1112: unconditionally calls {trigger_camp_vo_power_move_executed}
        → campaign_vo.dta:240: {meta_performer get_mastery_moves {meta_performer get_era}}
          → QuickplayPerformer has no handler
          → "unhandled msg: get_mastery_moves"
```

### Note on the DTA typo

The `{$this set_campaign FALSE FALSE FALSE}` at `hud_objects.dta:1111` is a **harmless
dead-code typo** in the original shipped Xbox DTA (confirmed authentic — auto-generated
from binary DTB extracted from the ARK archive, never hand-edited). `$this` stays bound
to the HUD PanelDir throughout the handler; the PanelDir has no `set_campaign` handler,
so `END_HANDLERS` silently returns `DATA_UNHANDLED`. The same typo exists in
`set_all_flashcards_mastered` at line 1061. Correct code exists nearby for contrast
(`clear_all_flashcard_campaign_status` at line 1113 correctly uses `$flash_card`).

On Xbox this was a no-op because `update_all_flashcard_campaign_status` only fires during
campaign mode, where the `if_else` always takes the campaign branch (which also has the
typo but is equally harmless). The native hack exposed it by calling the handler in
quickplay mode. **The DTA is authentic and should not be modified** — the fix belongs
entirely in our C++ code.

### Resolution

The `update_all_flashcard_campaign_status` send was removed from the hack (documented at
`GamePanel.cpp:124-131`). Both `set_campaign` and `get_mastery_moves` spam are fixed.

With the `SetFrame` architectural fix in `HamDirector::Poll()` (see above), the entire
flashcard hack is no longer needed and should be removed, which also eliminates any risk
of this cascade recurring.

### `get_mastery_moves` is not a missing subsystem

Mastery moves are a fully implemented campaign feature in `CampaignPerformer::GetMasteryMoves()`
(`CampaignPerformer.cpp:657`). The handler is only on `CampaignPerformer`, not the base
`MetaPerformer`, by design — it should never be called outside campaign mode. The DTA
callers in `flashcard_dock.dta:80-83` already handle `kDataUnhandled` gracefully.

## Related: Stream-Finished Hang (separate P0)

The same crash log revealed a **separate P0 issue**: the song never ends. After audio
streams are destroyed, `songMs` freezes because `StreamReceiverNative::mPlayCursor` stops
advancing when the AudioDevice removes the finished source. The "end" MIDI event beat is
never reached, so the DTA script that sends `{$game_panel win}` never fires.

Full diagnosis in `docs/sessions/2026-03-25-native-stream-finished-bug.md`.

## Remaining Work

- Remove the `GamePanel.cpp` flashcard hack (no longer needed with `SetFrame` driving interp handlers)
- Fine-tune flashcard X/Z positions for proper visual layout
- Clean up diagnostic fprintf blocks
- Fix `scripts/web/lib/core.mjs` — the nav fix agent changed Space to Enter for title/attract screens (already landed)
