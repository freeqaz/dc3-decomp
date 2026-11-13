// shell_timeout.h -- bound a child command with GNU `timeout`, on Linux and macOS.
//
// Tests that shell out wrap the engine in `timeout N ...`. macOS has no
// `timeout`; Homebrew's coreutils installs it as `gtimeout`. Start the shell
// string with kShellTimeoutLookup and write `${t:+$t N}` where `timeout N`
// was (`exec ${t:+$t N} cmd` works too). With neither tool the child runs
// unbounded and ctest's per-test TIMEOUT is the bound; exit 124 then never
// appears. On Linux the expanded command is exactly `/usr/bin/timeout N ...`.
#pragma once

static const char *const kShellTimeoutLookup =
    "t=$(command -v timeout || command -v gtimeout); ";
