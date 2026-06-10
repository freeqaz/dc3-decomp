// StubTrace unit tests (roadmap N.2).
//
// Exercises the native stub-hit counter directly (no renderer boot needed),
// proving HX_STUB_TRACE records hits when enabled, is silent when off, and that
// StubTraceDump::ToJson emits ranked {"name","count"} the /api/stubs endpoint
// serves.

#include "StubTrace.h"

#include <gtest/gtest.h>
#include <cstdlib>
#include <string>

// One of the real instrumented stubs (engine_stubs_generated.cpp). extern "C"
// so we can call it from this TU and drive the trace through the production
// macro, not a test double.
extern "C" int BinkGetError();
extern "C" int DmGetSystemInfo();

namespace {

// Helper: substring count.
size_t CountOccurrences(const std::string& hay, const std::string& needle) {
    size_t n = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
        ++n;
        pos += needle.size();
    }
    return n;
}

}  // namespace

// When tracing is OFF, calling a stub must not record anything.
TEST(StubTrace, SilentWhenDisabled) {
    dc3::gStubTraceEnabled = false;
    // A fresh dump baseline; calling the stub adds nothing.
    uint64_t total0 = 0, distinct0 = 0;
    dc3::StubTraceDump::ToJson(&total0, &distinct0);
    (void)BinkGetError();
    uint64_t total1 = 0, distinct1 = 0;
    dc3::StubTraceDump::ToJson(&total1, &distinct1);
    EXPECT_EQ(total1, total0);
}

// When tracing is ON, stub hits are counted and surface in the ranked JSON.
TEST(StubTrace, CountsHitsWhenEnabled) {
    dc3::gStubTraceEnabled = true;

    uint64_t baseTotal = 0;
    dc3::StubTraceDump::ToJson(&baseTotal, nullptr);

    (void)BinkGetError();
    (void)BinkGetError();
    (void)BinkGetError();
    (void)DmGetSystemInfo();

    uint64_t total = 0, distinct = 0;
    std::string json = dc3::StubTraceDump::ToJson(&total, &distinct);

    EXPECT_EQ(total - baseTotal, 4u);
    EXPECT_GE(distinct, 2u);

    // The names appear with their counts; BinkGetError (3) must rank ahead of
    // DmGetSystemInfo (1) in the descending-count ordering.
    EXPECT_EQ(CountOccurrences(json, "\"BinkGetError\""), 1u);
    EXPECT_EQ(CountOccurrences(json, "\"DmGetSystemInfo\""), 1u);
    size_t bink = json.find("\"BinkGetError\"");
    size_t dm = json.find("\"DmGetSystemInfo\"");
    ASSERT_NE(bink, std::string::npos);
    ASSERT_NE(dm, std::string::npos);
    EXPECT_LT(bink, dm) << "ranked dump should list the more-hit stub first";

    dc3::gStubTraceEnabled = false;
}

// The env-driven initializer flips the flag.
TEST(StubTrace, EnvEnablesTracing) {
    setenv("DC3_STUB_TRACE", "1", 1);
    EXPECT_TRUE(dc3::StubTraceInit());
    setenv("DC3_STUB_TRACE", "0", 1);
    EXPECT_FALSE(dc3::StubTraceInit());
    unsetenv("DC3_STUB_TRACE");
    EXPECT_FALSE(dc3::StubTraceInit());
}

// Empty/initial dump is valid JSON.
TEST(StubTrace, DumpIsAlwaysValidJson) {
    std::string json = dc3::StubTraceDump::ToJson();
    ASSERT_FALSE(json.empty());
    EXPECT_EQ(json.front(), '[');
    EXPECT_EQ(json.back(), ']');
}
