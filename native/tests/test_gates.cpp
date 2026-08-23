// Coverage-gate visibility check.
//
// Why this file exists: `ctest` counts SKIPPED as PASSED. The 2026-08-19
// toolchain audit found it printing "100% tests passed out of 441" while 79
// tests skipped — and the skipped tier was where two live native bugs were
// sitting (an intermittent SIGSEGV in TaskMgr::Poll and a 130-unit ankle jump).
// A green run therefore said nothing about coverage.
//
// Four things are checked here, and none can be satisfied by silence:
//
//  1. The gates CMake decided to enable actually REACH the test process. This
//     is an instrument check: the ENVIRONMENT test property is easy to get
//     wrong (a stray semicolon in the list, a property applied to the wrong
//     target) and the only symptom would be tests quietly skipping again.
//     MILO_TEST_GATES_ON_STR is baked in at compile time from the same CMake
//     variable that populates ENVIRONMENT, so the two must agree.
//
//  2. The gate table is printed on every run, pass or fail, so the operator can
//     see what the run did NOT cover without reading CMake.
//
//  3. The gate DECISION is still the one the source's own rule would make.
//     (1) proves delivery; it says nothing about whether the decision is
//     current. Every gate here is decided by an EXISTS probe at configure time
//     and then frozen — CMake only re-runs when a CMakeLists changes, so an
//     asset that appears on disk afterwards leaves the gate baked OFF forever
//     and (1) stays green, because a gate that is OFF is not in the ON list.
//     BuildGatesMatchTheSourceRule re-runs the recorded probes at RUN time.
//
//  4. The BINARY is this source tree's. Measured 2026-08-23: the main
//     checkout's native/build was three days stale — scripts/native_test.sh
//     ran ctest and never built — so an Aug-20 milo-tests was tested against an
//     Aug-23 tree. 449 registered where a fresh configure registers 504,
//     380/380 green, exit 0, and this suite PASSED: the gates it checks really
//     were delivered, just to a binary that was not this tree's. Checks (1)-(3)
//     are all satisfiable by a stale build, so none of them could see it.
//     BuildMatchesSources asks ninja's real dependency graph instead of
//     guessing from mtimes.
//
// This test never gates on assets, so it can always run and always report.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifndef MILO_TEST_GATES_ON_STR
#define MILO_TEST_GATES_ON_STR ""
#endif
#ifndef MILO_TEST_GATES_OFF_STR
#define MILO_TEST_GATES_OFF_STR ""
#endif
#ifndef MILO_TEST_GATE_PROBES_STR
#define MILO_TEST_GATE_PROBES_STR ""
#endif
#ifndef MILO_TEST_BINARY_DIR_STR
#define MILO_TEST_BINARY_DIR_STR ""
#endif
#ifndef MILO_TEST_GENERATOR_STR
#define MILO_TEST_GENERATOR_STR ""
#endif
#ifndef MILO_TEST_NINJA_STR
#define MILO_TEST_NINJA_STR ""
#endif
#ifndef MILO_TEST_REQUIRED_TARGETS_STR
#define MILO_TEST_REQUIRED_TARGETS_STR ""
#endif

namespace {

std::vector<std::string> Split(const char *s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (const char *p = s; *p; ++p) {
        if (*p == sep) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += *p;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::vector<std::string> SplitCsv(const char *s) { return Split(s, ','); }

bool PathExists(const std::string &p) {
    struct stat st;
    return !p.empty() && ::stat(p.c_str(), &st) == 0;
}

// A gate entry in MILO_TEST_GATES_OFF_STR is "NAME(reason)"; strip the reason.
std::string GateName(const std::string &entry) {
    const size_t paren = entry.find('(');
    return paren == std::string::npos ? entry : entry.substr(0, paren);
}

struct CmdResult {
    bool ran = false;
    int status = -1;
    std::string output;
};

CmdResult RunCapture(const std::string &cmd) {
    CmdResult r;
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) return r;
    r.ran = true;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) r.output += buf;
    r.status = pclose(pipe);
    return r;
}

std::string ShellQuote(const std::string &s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

}  // namespace

// The gates CMake reported as ON must be visible in this process's environment.
// If this fails, the suite is silently narrower than the build believes.
TEST(TestGates, EnabledGatesReachTheTestProcess) {
    const std::vector<std::string> on = SplitCsv(MILO_TEST_GATES_ON_STR);

    printf("  Coverage gates ENABLED by CMake : %s\n",
           on.empty() ? "(none)" : MILO_TEST_GATES_ON_STR);
    printf("  Coverage gates left OFF         : %s\n",
           std::strlen(MILO_TEST_GATES_OFF_STR) ? MILO_TEST_GATES_OFF_STR : "(none)");

    for (const std::string &name : on) {
        const char *v = getenv(name.c_str());
        EXPECT_TRUE(v && v[0])
            << "CMake enabled the '" << name << "' coverage gate, but it is not "
            << "set in the test process. The ctest ENVIRONMENT property is not "
            << "reaching milo-tests, so every suite behind that gate is skipping "
            << "while the build reports it as covered.";
    }
}

// A gate that CMake left off is a real coverage hole. This does not fail — the
// holes are deliberate (slow suites, audio-device contention) — but it makes the
// hole impossible to miss in the ctest log, which is the whole point.
TEST(TestGates, ReportDisabledSuites) {
    const std::vector<std::string> off = SplitCsv(MILO_TEST_GATES_OFF_STR);
    if (off.empty()) {
        printf("  All optional suites are enabled in this configuration.\n");
        SUCCEED();
        return;
    }
    printf("  %zu coverage gate(s) are OFF in this run:\n", off.size());
    for (const std::string &name : off)
        printf("    - %s\n", name.c_str());
    printf("  Tests behind these gates report SKIPPED, and ctest counts SKIPPED\n"
           "  as PASSED. `scripts/native_test.sh` prints the skip count and\n"
           "  enforces a budget; plain `ctest` does not.\n");
    SUCCEED();
}

// ---------------------------------------------------------------------------
// (3) The gate DECISION must still be the one the source's rule would make.
// ---------------------------------------------------------------------------
//
// EnabledGatesReachTheTestProcess only iterates the gates that are ON, so a
// gate frozen OFF by a stale configure is invisible to it by construction:
// nothing is checked, and nothing checked is nothing failed.
//
// Each probed gate is recorded as "NAME=path". If the path exists now but the
// gate is OFF, the configure that decided it is out of date with the disk and
// the suite is running narrower than the source asks for.
//
// This deliberately does NOT fail the reverse direction (gate ON, probe path
// gone): that means the assets were deleted under a good build, which the
// suites behind the gate will report themselves, loudly, as failures.
TEST(TestGates, GateDecisionsMatchTheSourceRule) {
    const std::vector<std::string> probes = Split(MILO_TEST_GATE_PROBES_STR, '|');
    ASSERT_FALSE(probes.empty())
        << "No gate probes were baked in. MILO_TEST_GATE_PROBES_STR is empty, so "
           "this check cannot fail and is therefore not a check. Either CMake "
           "stopped recording probes or this binary predates them -- see "
           "native/CMakeLists.txt (MILO_TEST_GATE_PROBES).";

    const std::vector<std::string> on = SplitCsv(MILO_TEST_GATES_ON_STR);
    const std::vector<std::string> off_raw = SplitCsv(MILO_TEST_GATES_OFF_STR);
    std::vector<std::string> off;
    for (const std::string &e : off_raw) off.push_back(GateName(e));

    for (const std::string &probe : probes) {
        const size_t eq = probe.find('=');
        ASSERT_NE(eq, std::string::npos) << "malformed gate probe: " << probe;
        const std::string name = probe.substr(0, eq);
        const std::string path = probe.substr(eq + 1);

        const bool is_on =
            std::find(on.begin(), on.end(), name) != on.end();
        const bool is_off =
            std::find(off.begin(), off.end(), name) != off.end();
        ASSERT_TRUE(is_on || is_off)
            << "gate '" << name << "' has a probe but appears in neither the ON "
            << "nor the OFF list -- the gate table and the probe list have "
            << "drifted apart in native/CMakeLists.txt.";

        const bool exists = PathExists(path);
        printf("  probe %-20s %s  ->  %s (build says %s)\n", name.c_str(),
               exists ? "PRESENT" : "absent ", exists ? "should be ON" : "may be OFF",
               is_on ? "ON" : "OFF");

        if (exists && !is_on) {
            ADD_FAILURE()
                << "Coverage gate '" << name << "' is OFF in this build, but its "
                << "prerequisite exists on disk now:\n    " << path << "\n"
                << "CMake decided this gate at configure time and never re-asked "
                << "-- it only re-runs when a CMakeLists changes, not when assets "
                << "appear. Every suite behind '" << name << "' is skipping while "
                << "the box can actually run it. Reconfigure the build directory:\n"
                << "    cmake " << MILO_TEST_BINARY_DIR_STR << "\n"
                << "Do NOT raise the skip budget to absorb this.";
        }
    }
}

// ---------------------------------------------------------------------------
// (4) The binary under test must be built from THIS source tree.
// ---------------------------------------------------------------------------
//
// This is the check whose absence let a three-day-stale build report green.
// No mtime heuristic: ninja is asked, with the real dependency graph, whether
// the two targets the suite exercises are up to date. `-n` is a dry run and
// writes nothing.
//
// It is deliberately hard to disarm. A missing ninja, a missing build.ninja, a
// generator that is not Ninja, an empty baked-in path -- every one of those is
// a FAILURE, not a skip, because in each case the answer to "is this binary
// current?" is "unknown", and an unknown that reports green is exactly the
// defect being closed. MILO_TEST_ALLOW_STALE_BUILD=1 is the escape hatch, and
// it announces itself.
TEST(TestGates, BuildMatchesSources) {
    if (const char *allow = getenv("MILO_TEST_ALLOW_STALE_BUILD");
        allow && allow[0] && strcmp(allow, "0") != 0) {
        printf("  MILO_TEST_ALLOW_STALE_BUILD is set: build-currency NOT verified.\n"
               "  This run says nothing about whether the binary matches the tree.\n");
        SUCCEED();
        return;
    }

    const std::string build_dir = MILO_TEST_BINARY_DIR_STR;
    const std::string generator = MILO_TEST_GENERATOR_STR;
    const std::string ninja = MILO_TEST_NINJA_STR;

    ASSERT_FALSE(build_dir.empty())
        << "MILO_TEST_BINARY_DIR_STR was not baked in; build currency is "
           "unverifiable and this binary predates the check.";
    ASSERT_NE(generator.find("Ninja"), std::string::npos)
        << "Build-currency verification is implemented for the Ninja generator "
           "only; this build used '" << generator << "'. Rather than pass "
           "silently on an unverifiable configuration, this fails. Configure "
           "with -G Ninja, or set MILO_TEST_ALLOW_STALE_BUILD=1 and accept that "
           "a green run then proves nothing about staleness.";
    ASSERT_FALSE(ninja.empty())
        << "CMAKE_MAKE_PROGRAM was empty for a Ninja generator.";
    ASSERT_TRUE(PathExists(ninja)) << "ninja is gone: " << ninja;
    ASSERT_TRUE(PathExists(build_dir + "/build.ninja"))
        << "no build.ninja in " << build_dir;

    const std::string targets = MILO_TEST_REQUIRED_TARGETS_STR;
    ASSERT_FALSE(targets.empty())
        << "MILO_TEST_REQUIRED_TARGETS_STR was not baked in; there is nothing to "
           "check currency OF, so this test could not fail and would not be a "
           "test. See native/CMakeLists.txt (MILO_TEST_REQUIRED_TARGETS).";
    printf("  Build-currency targets: %s\n", targets.c_str());

    const std::string cmd = ShellQuote(ninja) + " -C " + ShellQuote(build_dir) +
                            " -n " + targets + " 2>&1";
    const CmdResult r = RunCapture(cmd);
    ASSERT_TRUE(r.ran) << "could not run: " << cmd;
    ASSERT_EQ(r.status, 0) << "ninja dry run failed:\n" << r.output;

    const bool up_to_date = r.output.find("no work to do") != std::string::npos;
    if (up_to_date) {
        printf("  Build currency: milo-tests and dc3-native are up to date.\n");
        return;
    }

    // Trim the dry-run plan; it can be thousands of lines.
    std::string head = r.output;
    if (head.size() > 4000) head = head.substr(0, 4000) + "\n  ...(truncated)";

    ADD_FAILURE()
        << "The build directory is STALE: ninja still has work to do for "
           "" << targets << ".\n"
        << "This test binary is therefore not built from the current source "
           "tree, and every result in this run -- including the registered test "
           "COUNT -- describes an older tree. On 2026-08-23 exactly this "
           "condition produced '449 registered, 380/380 passed, exit 0' from a "
           "three-day-old build while a fresh one registered 504 and the suite "
           "was materially different.\n"
        << "Fix it by building, not by re-running ctest:\n"
        << "    cmake --build " << build_dir << "\n"
        << "or use scripts/native_test.sh, which now builds first.\n\n"
        << "ninja -n output:\n" << head;
}
