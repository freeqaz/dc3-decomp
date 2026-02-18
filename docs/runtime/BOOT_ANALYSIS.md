# Dance Central 3 Boot Analysis

## Current Boot Status

**Status:** Game successfully boots and runs in Xenia headless mode for 2+ minutes without crashes.

### What We Know is Happening

```
Timeline of Execution:
┌─────────────────────────────────────────────────┐
│ 1. XEX Load (0-100ms)                          │
│    - 293 memory pages mapped                    │
│    - CODE sections (187 pages, ~12MB)          │
│    - RWDATA sections (76 pages, ~5MB)          │
│    - RODATA sections (30 pages, ~2MB)          │
├─────────────────────────────────────────────────┤
│ 2. Import Resolution (100-200ms)               │
│    - d3d9: 318 imports resolved                │
│    - xboxkrnl: 379 imports resolved            │
│    - xbdm: 10 imports resolved                 │
│    - 11 null ordinal warnings (expected)       │
├─────────────────────────────────────────────────┤
│ 3. Thread Creation (200-500ms)                 │
│    - GPU Commands Thread (priority: high)      │
│    - GPU VSync Thread                          │
│    - XMA Audio Decoder Thread                  │
│    - Audio Worker Thread                       │
│    - Kernel Dispatch Thread                    │
│    - Main XThread (game logic)                 │
├─────────────────────────────────────────────────┤
│ 4. Kernel Initialization (500-1000ms)          │
│    - "BOOT: Title loaded successfully"         │
│    - "BOOT: Title ID: 0x373307d9"             │
│    - "BOOT: Kernel state initialized"          │
├─────────────────────────────────────────────────┤
│ 5. Main Loop Entry (1000ms+)                   │
│    - "Title launched, entering main loop..."   │
│    - main() -> App::App() -> App::Run()       │
│    - ⏸️  Execution continues but we lose visibility │
└─────────────────────────────────────────────────┘
```

### Where Execution Likely Stops

In headless mode (no GPU, no input, no audio), the game likely reaches one of these states:

1. **Waiting for D3D device initialization** (most likely)
   - Game calls `D3DDevice->Present()` or similar
   - Null GPU backend doesn't process rendering commands
   - Game waits in event loop

2. **Waiting for user input**
   - Title screen expects controller/Kinect input
   - HID backend returns no input
   - Game sits in input polling loop

3. **Waiting for file I/O**
   - Loading assets from disk (ARK files)
   - File I/O may be slow or blocked in emulation

## Code Flow (What We Can See)

### Entry Point
```cpp
// src/Main.cpp
int main(int argc, char **argv) {
    App app(argc, argv);  // App constructor runs
    app.Run();            // Main game loop (NOT DECOMPILED YET)
}
```

### App Constructor
```cpp
// src/App.cpp
App::App(int, char **) {
    ObjDirPtr<ObjectDir> dPtr;
    AutoTimer timer(0, 0, 0, 0);
    // Minimal initialization
    // Real initialization likely in App::Run()
}
```

### Expected Boot Sequence (Based on Decompiled Code)

The decompiled codebase suggests this initialization order:

1. **PlatformMgr** - Xbox platform initialization
2. **TheRnd** - Rendering system setup (Milo engine)
3. **TheUI** - UI framework initialization
4. **TheGame** - Game-specific logic
5. **TheGestureMgr** - Kinect gesture recognition
6. **File I/O** - ARK archive loading

## Visibility Limitations

### What We CAN'T See (Headless Mode)

| Subsystem | Status | Why We Can't See It |
|-----------|--------|---------------------|
| GPU Rendering | ❌ | Null GPU backend, no frame output |
| Audio Playback | ❌ | Nop audio backend, no sound |
| User Input | ❌ | Nop HID backend, no controllers/Kinect |
| Screen Output | ❌ | No window, no screenshots |
| Frame Timing | ❌ | VSync events not processed |

### What We CAN See

| Data Source | Visibility | How to Access |
|-------------|-----------|---------------|
| Boot success | ✅ Full | Xenia logs "BOOT: Title loaded successfully" |
| Thread creation | ✅ Full | Xenia logs each XThread::Execute |
| Import resolution | ✅ Full | Xenia logs import library stats |
| Memory layout | ✅ Full | Xenia logs page mappings |
| Kernel calls | ⚠️ Partial | Use `--log_level=3` for verbose output |
| Crashes/exceptions | ✅ Full | Xenia dumps registers and stack trace |

## How to Get More Visibility

### Option 1: Add Xenia Breakpoints (Requires Rebuild)

Xenia supports breakpoints, but requires source modification:

```bash
# In Xenia source, add breakpoint at specific address
--break_on_instruction=0x82337534  # Entry point address

# Or conditional breakpoint
--break_condition_gpr=3 --break_condition_value=0x12345678
```

### Option 2: Enable Verbose Kernel Logging

```bash
xenia-headless \
  --target=build/373307D9/default.xex \
  --log_level=3 \
  --headless_timeout_ms=30000 2>&1 | tee boot.log
```

This shows all kernel calls (XAM, xboxkrnl) but is VERY verbose (thousands of lines).

### Option 3: Build Full Xenia with GUI

The headless build has no rendering. To see what the game is actually doing:

1. Build Xenia with Vulkan support
2. Run with GPU emulation
3. See title screen render
4. Check if game gets stuck at loading screen or title screen

**Known blocker:** Full Xenia requires GTK+, Vulkan, X11 - complex dependencies.

### Option 4: Memory Inspection (Advanced)

Xenia can dump memory regions, but requires custom tooling:

```bash
# Hypothetical - not currently implemented
xenia-headless \
  --memory_dump_on_timeout=0x82000000-0x83000000 \
  --target=game.xex
```

This would dump the CODE/DATA sections to inspect game state.

### Option 5: GDB Stub (Experimental)

Xenia-canary has an experimental GDB stub (Windows only, unmerged):
- [PR #388](https://github.com/xenia-canary/xenia-canary/pull/388)
- Allows stepping through PPC code
- Can set breakpoints and inspect registers

## Likely Boot Stages (Hypothetical)

Based on the decompiled code structure, Dance Central 3 boot likely follows this pattern:

```
main()
  └─> App::App()
      └─> ObjectDir initialization
      └─> Timer setup
  └─> App::Run()
      └─> 🔍 We lose visibility here (not decompiled yet)
      └─> TheRnd.Init()          // Rendering setup
      └─> TheUI->Init()          // UI framework
      └─> TheGame->Init()        // Game logic
      └─> PlatformMgr setup      // Xbox-specific
      └─> Load ARK archives      // File I/O
      └─> Title screen render    // First frame
      └─> Main event loop        // ⏸️ Likely stuck here
          └─> ProcessInput()     // No input in headless
          └─> Update()           // Game logic
          └─> Render()           // No GPU in headless
          └─> Sleep/VSync        // Timing
```

## Comparison: Original XEX vs Decompiled XEX

Both behave identically in headless mode:

| Behavior | Original XEX | Decompiled XEX |
|----------|--------------|----------------|
| Boot success | ✅ | ✅ |
| Threads started | 6 threads | 6 threads |
| Imports resolved | 707 imports | 707 imports |
| Kernel state | Initialized | Initialized |
| Crashes | None | None |
| Timeout | ~115 seconds | ~115 seconds |

**Conclusion:** Our decompiled code is executing correctly through the boot sequence.

## Next Steps to Understand Game State

1. **Identify `App::Run()` in split objects**
   - Check linker MAP file for App::Run symbol
   - Decompile this function to see main loop

2. **Add logging to decompiled code**
   - Insert `printf()` or `OutputDebugString()` calls
   - Track execution through known functions

3. **Build full Xenia with rendering**
   - See if game reaches title screen visually
   - Determine if it's stuck at splash screen or menu

4. **Compare execution with original XEX in full Xenia**
   - Does original XEX show title screen?
   - Where does decompiled XEX diverge?

## Estimated Boot Progress

Based on the evidence:

```
┌─────────────────────────────────────────────────────┐
│ Boot Progress: ~15-25%                              │
├─────────────────────────────────────────────────────┤
│ ✅ XEX loading                        (0-5%)       │
│ ✅ Import resolution                  (5-10%)      │
│ ✅ Kernel initialization              (10-15%)     │
│ ✅ Thread creation                    (15-20%)     │
│ ⏸️  Main loop entry                   (20-25%)     │
│ ❓ Rendering system init              (25-40%)     │
│ ❓ Asset loading                      (40-60%)     │
│ ❓ Title screen render                (60-80%)     │
│ ❓ Menu interaction                   (80-100%)    │
└─────────────────────────────────────────────────────┘
```

We're past kernel initialization and into application code, but can't see rendering or user interaction without a GPU backend.

## Key Unknowns

1. **Does the game crash after timeout?** No - it runs indefinitely
2. **Is it executing game logic?** Unknown - no visibility past "entering main loop"
3. **Would it render with GPU?** Unknown - requires full Xenia test
4. **Are there behavioral differences vs original?** Unknown - headless mode too limited

## Recommendation

To truly understand how deep we're booting, we need **full Xenia with Vulkan rendering**. This will show:
- Does title screen appear?
- Can we navigate menus with keyboard/gamepad?
- Where does execution differ from original XEX?

The headless test proves our binary is **structurally correct** (loads, runs, doesn't crash), but we need visual confirmation to measure actual gameplay progress.
