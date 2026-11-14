// ProcessSpawner_Native.h -- launch helper processes from a pristine copy of
// the game process.
//
// Why this exists (measured on a GPD Pocket, Arch + PipeWire, no rtkit): the
// engine opens audio through miniaudio's ALSA backend; PipeWire's ALSA plugin
// loads libpipewire-module-rt, which -- with no rtkit to ask -- lowers
// RLIMIT_RTTIME to soft 0 / hard 0 for the WHOLE game process
// (/proc/<pid>/limits: "Max realtime timeout 0 0 us").  A child fork+exec'd
// after that inherits the hard limit and, being unprivileged, can never raise
// it again.  pose_server.py's MediaPipe graph runs a realtime thread, so the
// kernel SIGKILLs it the moment that thread is scheduled (sig 9, SI_KERNEL) --
// at model load -- and pose tracking was off for the session.
//
// The fix: the very first thing main() does is fork a tiny spawner process.  It
// holds the process-wide state the game was STARTED with (rlimits, signal
// dispositions as exec resets them, scheduling) and nothing else, and sleeps on
// a socket.  When the game wants a helper it sends argv, the CURRENT
// environment and working directory; the spawner fork+execs it and reports the
// pid, then relays its exit status when it dies.  Whatever audio, graphics or
// any library does to the game process afterwards cannot reach the helper.
//
// Lifetime: the spawner ends when the game ends -- socket EOF (the kernel closes
// the game's end however the game exits, SIGKILL included) and, as a second
// line, PR_SET_PDEATHSIG.  On the way out it SIGTERMs every helper it
// launched, waits up to 2 s, SIGKILLs the rest and reaps them.  Each helper
// also carries PR_SET_PDEATHSIG(SIGTERM) against the spawner, so not even a
// SIGKILLed spawner leaks one.
//
// A process that never called Init() (milo-viewer, a unit test that does not
// opt in) still gets a working Spawn(): it falls back to a direct fork+exec
// from the caller, which inherits whatever limits the caller has by then.
#pragma once

#include <string>
#include <sys/types.h>
#include <vector>

namespace NativeSpawner {

// Fork the spawner.  Call before ANYTHING that can change process-wide state
// (audio, graphics, threads) -- the top of main().  Idempotent; returns whether
// a spawner is running.  Desktop only; a no-op returning false on web.
bool Init();

// Whether Spawn() goes through the spawner (false: direct fork fallback).
bool Active();

// pid of the spawner process, or -1.
pid_t SpawnerPid();

// Launch argv[0] (searched on the caller's current PATH) with argv, the
// caller's current environment and working directory.  stdio is the game's
// stdio as it was when Init() ran.  Returns the helper's pid, or -1 with errno
// set.  An exec failure shows up as an exit with status 127.
pid_t Spawn(const std::vector<std::string> &argv);

// Non-blocking.  True once `pid` (from Spawn) has exited, with its waitpid
// status word in *status.  Each exit is reported once.  If the spawner itself
// has gone (it never should while the game lives), every helper it owned is
// reported as killed by SIGTERM -- what PR_SET_PDEATHSIG sent it.
bool TryReap(pid_t pid, int *status);

// Send `sig` to a helper launched by Spawn, and stop tracking it: its exit is
// collected by the spawner and not reported to TryReap.
void Kill(pid_t pid, int sig);

// Test hook: drop the connection as the game's exit would (the spawner then
// terminates its helpers and exits).  The next Spawn() falls back to a direct
// fork; Init() may be called again.
void ShutdownForTest();

} // namespace NativeSpawner
