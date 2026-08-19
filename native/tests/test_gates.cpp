// Coverage-gate visibility check.
//
// Why this file exists: `ctest` counts SKIPPED as PASSED. The 2026-08-19
// toolchain audit found it printing "100% tests passed out of 441" while 79
// tests skipped — and the skipped tier was where two live native bugs were
// sitting (an intermittent SIGSEGV in TaskMgr::Poll and a 130-unit ankle jump).
// A green run therefore said nothing about coverage.
//
// Two things are checked here, and neither can be satisfied by silence:
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
// This test never gates on assets, so it can always run and always report.

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef MILO_TEST_GATES_ON_STR
#define MILO_TEST_GATES_ON_STR ""
#endif
#ifndef MILO_TEST_GATES_OFF_STR
#define MILO_TEST_GATES_OFF_STR ""
#endif

namespace {

std::vector<std::string> SplitCsv(const char *s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char *p = s; *p; ++p) {
        if (*p == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += *p;
        }
    }
    if (!cur.empty()) out.push_back(cur);
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
