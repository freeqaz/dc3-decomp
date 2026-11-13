// DC3 Native Port - quit requests (H-9)
//
// One flag the native main loop (App::RunWithoutDebugging) checks once per
// frame. Anything that wants the process to end -- a terminating signal, the
// loss of the X window -- sets it instead of exiting on the spot, so the exit
// happens at a frame boundary, through the same loop-return -> main ->
// _exit path a window close already takes:
//
//   * never in the middle of a frame. Saves are written synchronously inside
//     a frame (MemcardMgr::ThreadCall_SaveGame does fopen/fwrite/fclose on the
//     main thread), so a frame boundary is never half-way through a save.
//     The old SIGTERM handler _exit()ed from wherever the main thread was.
//   * never through static destructors (see main_native.cpp).
//
// Async-signal-safe: Dc3RequestQuit is one lock-free compare-exchange.
#pragma once

#include <atomic>
#include <cstdlib>

// Why the loop should stop ("SIGTERM", "window_lost", ...), or nullptr.
// The FIRST request wins; later ones are refused (Dc3RequestQuit returns
// false), which is how a second SIGTERM knows to stop immediately.
extern std::atomic<const char *> gDc3QuitRequest;
// The process exit code that goes with the winning request (128+sig for a
// signal, 0 for a lost window).
extern std::atomic<int> gDc3QuitCode;

static_assert(std::atomic<const char *>::is_always_lock_free,
              "Dc3RequestQuit is called from signal handlers");

inline bool Dc3RequestQuit(const char *reason, int code) {
    const char *expected = nullptr;
    if (!gDc3QuitRequest.compare_exchange_strong(expected, reason))
        return false;
    gDc3QuitCode.store(code);
    return true;
}

// exit() with the status recorded first. glibc's on_exit hands the exit hook
// (main_native.cpp) the status; macOS has no on_exit and atexit cannot see it,
// so every native exit() site calls this instead and the hook reads the
// recorded value. -1 = exit() was called by someone else (status unknown).
inline std::atomic<int> gDc3ExitStatus{-1};
[[noreturn]] inline void Dc3Exit(int status) {
    gDc3ExitStatus.store(status);
    std::exit(status);
}

// Per-frame window check, installed by dc3-native when it has an X11 window
// (native/src/platform/WindowLoss_X11.cpp). Returns true once the window is
// gone, after requesting the quit itself. nullptr everywhere else.
extern bool (*gDc3WindowLostPoll)();
