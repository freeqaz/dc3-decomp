# Web Audio Investigation: Title Ambience Persists Into Gameplay

Date: 2026-03-25

## Summary

The audio that continues into gameplay on web is not shell `MetaMusic`.
It is `shell_dciambience`, a separate `Sound` / `FlowSound` path loaded from the shared synth common bank.

The existing DTA stop hooks only stop `MetaMusic`, so they do not affect this title ambience sound.

## Main Findings

### 1. The normal DTA stop hooks are present

The song-start flow already calls:

- `{meta music_stop}` in `enter_gameplay`
- `{meta music_stop}` again on loading-screen enter

Relevant files:

- `orig-assets/extracted/ui/global.dta`
- `orig-assets/extracted/ui/loading/loading.dta`

These hooks are not missing.

### 2. Shell `MetaMusic` is not the source that persists

Web audio probe logs showed:

- `shellmusic_loop_01.mogg` starts on title
- it is paused on the way to gameplay
- it remains resident in the web mixer, but silent

Meanwhile, 15 seconds into gameplay, the active audible non-song stream is:

- `MoggClip[sfx/samples/shell/shell_dciambience.mogg]`

The actual gameplay song is a separate stream:

- `HamAudio[songs/betteroffalone/betteroffalone] primary`

Primary artifact:

- `/tmp/dc3-web/audio-probe-run4/console.jsonl`

Key lines from that run:

- shell music silent in gameplay:
  - line 3233
- persistent non-song audio:
  - lines 3235-3236
- gameplay song:
  - lines 3237-3238

### 3. The persistent audio is title ambience from the synth common bank

Binary asset search in `orig-assets/extracted/sfx/gen/common_bank.milo_xbox` showed:

- `MoggClip` asset `shell_dciambience.mogg`
- `Sound` asset `shell_dciambience.snd`
- `Flow` asset `titlescreen_amb1.flow`
- a `FlowSound` child that points at `shell_dciambience.snd`

This is a separate sound path from shell `MetaMusic`.

### 4. Why it starts

The most likely startup path is asset-driven flow auto-start:

- `Flow::PostLoad()` reads `mStartMode` from the asset
- `Flow::Enter()` auto-activates flows with nonzero start mode
- `FlowSound::Activate()` immediately calls `mSound->Play(...)`

Relevant code:

- `src/system/flow/Flow.cpp`
- `src/system/flow/Flow.h`
- `src/system/flow/FlowSound.cpp`

So this ambience starts because the title ambience flow in the common bank starts it, not because DTA explicitly issues `meta music_start`.

### 5. Why it never stops

There is no evidence of a DTA stop hook for this sound.

Important distinction:

- `meta music_stop` only stops shell `MetaMusic`
- `shell_dciambience` is a `Sound` / `FlowSound` object in `common_bank`

Also important:

- `FlowSound` defaults to `mImmediateRelease = true`
- in that mode, `Activate()` just fires `mSound->Play(...)` and returns
- that means playback is effectively fire-and-forget unless some separate stop path exists

I did not find any DTA references to:

- `shell_dciambience`
- `shell_dciambience.snd`
- `titlescreen_amb1.flow`

So the current best explanation is:

- title ambience starts from common-bank flow startup
- there is no explicit stop path for it on the menu-to-game transition
- `meta music_stop` is unrelated to it

## Native / Web Debugging Notes

### Native

Using the HTTP debug workflow and `DC3_HTTP_PORT=9877`, native showed the expected shell music stop / restart / stop / kill chain. That confirmed the normal DTA `MetaMusic` path is working.

Conclusion from native:

- no missing `MetaMusic` stop hook
- if audio persists, it is outside the `MetaMusic` system

### Web

Useful command path:

- `docs/debugging/web.md`
- `native/web/server.py`
- `scripts/web/audio-probe.mjs`

The web probe was useful for confirming:

- shell music is silent in gameplay
- `shell_dciambience` is the persistent extra source

## Temporary Regression During Investigation

Two web-only instrumentation attempts destabilized title-screen startup:

1. `Sound.cpp` trace that logged `shell_dciambience` play/stop
2. `MoggClip.cpp` trace that called `PathName(this)` / `PathName(mEventReceiver)` during startup

The second one was the confirmed cause of the title-screen hang after autosave.

Current status:

- the risky path-based `MoggClip` debug tag was removed
- the web build returned to normal title-screen startup

## Current Conclusion

The user-facing symptom is real, but the root cause is not shell `MetaMusic`.

It is title ambience:

- `titlescreen_amb1.flow`
- `shell_dciambience.snd`
- `shell_dciambience.mogg`

The missing behavior is an explicit stop for that path when leaving title / shell for gameplay.

## Recommended Next Step

Add an explicit stop/deactivate for the title ambience sound or flow on the menu-to-game transition.

That fix should target the `Sound` / `FlowSound` path, not `meta music_stop`.
