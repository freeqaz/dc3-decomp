# Stub & Function Implementation Roadmap

## Functions With Explicit TODO Comments

### SynthSample360::NewInst()
- **File**: `src/system/synth_xbox/SynthSample.cpp:37`
- **Current**: `return nullptr;` with `// TODO: needs SampleInst360 implementation`
- **Returns**: `SampleInst*`
- **Priority**: High — explicit TODO, audio subsystem critical

## Empty Function Bodies

### Game::StartIntro()
- **File**: `src/lazer/game/Game.cpp:312`
- **Size**: ~4 bytes (empty body)
- **Returns**: void
- **Context**: Should likely initialize intro sequence

### HamProfile::CheckForNinjaUnlock()
- **File**: `src/lazer/meta_ham/HamProfile.cpp:358`
- **Size**: ~4 bytes (empty body)
- **Returns**: void
- **Context**: Game achievement unlock

### HamProfile::CheckForIconManUnlock()
- **File**: `src/lazer/meta_ham/HamProfile.cpp:359`
- **Size**: ~4 bytes (empty body)
- **Returns**: void
- **Context**: Game achievement unlock

### SaveMemcardAction::PostAction()
- **File**: `src/lazer/meta_ham/HamMemcardAction.cpp:28`
- **Size**: ~4 bytes (empty body)
- **Returns**: void
- **Context**: Save completion hook

### LoadMemcardAction::PreAction()
- **File**: `src/lazer/meta_ham/HamMemcardAction.cpp:30`
- **Size**: ~4 bytes (empty body)
- **Returns**: void
- **Context**: Load preparation hook

### Character::Terminate()
- **File**: `src/system/char/Character.cpp:620`
- **Size**: ~4 bytes (empty body)
- **Returns**: void
- **Context**: Character cleanup/shutdown

## Assessment

Most promising candidates by potential impact:

1. **SynthSample360::NewInst()** — Explicit TODO, audio subsystem
2. **Game::StartIntro()** — Intro sequence logic
3. **HamProfile unlock checks** — Achievement unlock hooks
4. **Character::Terminate()** — Character cleanup

## Notes

- This list was generated from a scan of `src/system/` and `src/lazer/` directories
- Many "stub" functions may actually be correctly empty in the original binary
- Always verify against objdiff before implementing — a 100% match on an empty body means the original was also empty
- Use `query_functions` with the unit pattern to check current match % before working on any function
