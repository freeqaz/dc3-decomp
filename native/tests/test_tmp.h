#pragma once

// Test temp dirs and logs.  Declared here, with no engine header, so the
// subprocess-driving suites (test_native_exit.cpp builds without the PCH or
// MSVC compat) can use it; defined in test_helpers.cpp.

#include <gtest/gtest.h>
#include <cstddef>
#include <string>

// ============================================================================
// Test temp dirs and logs (np-tmpleak)
// ============================================================================
//
// Every temp dir and log a test makes goes through these, so one policy holds:
//
//   * Base: $TMPDIR when set and non-empty, else the fallback the call names
//     (what the site used to hardcode: /tmp for dirs, /tmp/claude-1000 for
//     logs).  ctest sets TMPDIR=<build>/tmp for every test (MILO_TEST_ENV), so
//     a gate run writes inside its own build dir, not into /tmp.
//   * The owning pid is in every name: dirs are <prefix>-p<pid>-XXXXXX
//     (mkdtemp), logs <stem>-p<pid>.<ext>, the scratch dir milo-tests-<pid>-<n>.
//   * A run that PASSES removes them when the process exits.  A run that FAILS
//     keeps them and prints "[test-tmp] kept ...: <path>" for each, because
//     people diagnose from them.  DC3_KEEP_TEST_TMP=1 (or the older
//     MILO_TEST_KEEP_SCRATCH=1) keeps everything.  "The run" is the process:
//     ctest runs one test per process, so that is the test; a direct milo-tests
//     run of many tests keeps everything if any of them failed.
//   * Only paths these functions created are ever removed, and only by the
//     process that created them (a forked child inherits the registry and must
//     not delete its parent's dirs).
//   * A process killed from outside (ctest timeout, ^C) runs no exit hooks.
//     Each milo-tests process starts with SweepDeadTestTmp over the bases,
//     which removes helper-named DIRS whose owner pid is dead (see below).
//
// Process lifetime is the default because most of these dirs are handed to a
// child dc3-native as its cache/save/data root and are reused across two
// engine runs (save/restart/restore, DlcSongFlowTest's warm boot), so they
// must outlive any one scope.  ScopedTestDir is for a dir that belongs to one
// test body.

// $TMPDIR if set and non-empty, else `fallback`.  No trailing slash.
std::string TestTempBase(const char *fallback = "/tmp");

// DC3_KEEP_TEST_TMP=1 / MILO_TEST_KEEP_SCRATCH=1: keep everything.
bool TestTmpKeepAll();

// mkdtemp <base>/<prefix>-p<pid>-XXXXXX, removed at process exit if the run
// passed.  `prefix` must be lower-case [a-z0-9-] and start with "dc3-" (the
// sweep only recognises that shape).  If the path would be longer than
// kTestTempDirMaxLen -- these roots hold AF_UNIX sockets, and sun_path is 108
// bytes with its NUL: 92 + "/stub/pose.sock" (test_native_exit.cpp, the longest
// in use) is 107 -- or mkdtemp fails under $TMPDIR, it falls back to
// `fallbackBase`.  Empty string on failure.
constexpr size_t kTestTempDirMaxLen = 92;
std::string MakeTestTempDir(const char *prefix, const char *fallbackBase = "/tmp");

// <base>/<stem>-p<pid><ext>, with base TestTempBase(fallbackBase).  Not
// created -- the caller writes it -- but registered, so it is removed at
// process exit if the run passed (whether it was written or not).  The same
// stem gives the same path within a process.
std::string TestTempFile(const std::string &stem, const char *ext = ".log",
                         const char *fallbackBase = "/tmp/claude-1000");

// TestTempFile("<stem>_<current test name>").
std::string TestLogPath(const std::string &stem);

// Removes every registered path this process created, or keeps and prints
// them if the run failed or DC3_KEEP_TEST_TMP=1.  Runs at exit; EngineTeardown
// calls it before its _exit.  Idempotent.
void FinishTestTmp();

// A dir for one scope: created like MakeTestTempDir, and removed when the
// object is destroyed unless the current test has failed by then
// (::testing::Test::HasFailure()) or DC3_KEEP_TEST_TMP=1 -- then it is kept,
// printed, and left to the exit-time policy above.
class ScopedTestDir {
public:
    explicit ScopedTestDir(const char *prefix, const char *fallbackBase = "/tmp");
    ~ScopedTestDir();
    ScopedTestDir(const ScopedTestDir &) = delete;
    ScopedTestDir &operator=(const ScopedTestDir &) = delete;
    const std::string &path() const { return mPath; }
    const char *c_str() const { return mPath.c_str(); }
    bool ok() const { return !mPath.empty(); }

private:
    std::string mPath;
};

// Removes, directly under `base`, every DIRECTORY whose name is
// milo-tests-<pid>-<n> or dc3-...-p<pid>-XXXXXX (what the helpers above make)
// when ALL of these hold: it is a real directory (not a symlink) owned by this
// uid; kill(pid, 0) fails with ESRCH, so no process with that pid exists; and
// it has not been modified for `minAgeSeconds`.  The age floor is what makes a
// pid check safe for a run in ANOTHER pid namespace (whose pids mean nothing
// here): no test outlives ctest's timeout, so a dir untouched for hours has no
// live owner anywhere.  A live pid -- including a recycled one -- keeps its
// dir.  Files (failure logs) are never swept.  Returns the number removed.
int SweepDeadTestTmp(const std::string &base, long minAgeSeconds);

// What each milo-tests process sweeps at start (a gtest global Environment):
// the helper bases and the legacy /tmp, /tmp/claude-1000.
constexpr long kTestTmpSweepMinAgeSeconds = 6 * 3600;
