// test_gpu_timers.cpp -- the engine's per-render-pass GPU timers
// (milo-native-engine gfx/GpuTimers.h, MILO_GPU_TIMERS; dc3 PLAN H-96).
//
// One headless dc3-native boot with the timers on and one with them off, each
// ~900 frames into the menus at a 240 fps cap (>= 3.75 s wall, so at least two
// of the timers' 1 s windows close). On:
//   - the device came up with timestamp-query and the timers announced
//     themselves ("GPU timers ON"),
//   - a window was published with timed frames and a pass sum > 0,
//   - every window's pass sum fits inside its first-begin-to-last-end span
//     (the passes are serial on one queue),
//   - MainPass, the venue/menu scene pass, reports > 0 ms,
//   - no pass is unnamed (a begin site without a label prints unnamed@file:line),
//   - timestamp_quantization is off as the DEVICE reports its toggles. (The
//     values themselves cannot show it on the RTX 3090: with the toggle forced
//     on, every timestamp still had only its low 5 bits always zero, the same
//     as off -- Dawn applies the mask in its tick-to-ns conversion, and that
//     changed nothing here. ts_zero_low_bits on the window line stays as a
//     diagnostic for adapters where it does.)
// Off: no "GPU timers" line and no [GPUTIME] line at all.
//
// Skips (does not fail) only where the hardware cannot answer: no GPU / the
// Null backend, or an adapter without timestamp-query.
//
// Sabotage tried: InstrumentPass leaving desc.timestampWrites null -> every
// window has 0 timed frames: OnPublishesPassTimes and MainPassReportsTime
// fail; DisplayRamp's rp.label removed ->
// EveryPassIsNamed fails on "pass=unnamed@DisplayRamp.cpp:135"; GpuDevice's
// disabledToggles left empty -> the device reports timestamp_quantization
// enabled and TimestampsAreNotQuantized fails.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "platform/ExecutablePath.h"
#include "shell_timeout.h"

namespace {

std::string BinaryDir() {
    char buf[4096];
    ssize_t len = ReadExecutablePath(buf, sizeof(buf) - 1);
    if (len <= 0) return ".";
    buf[len] = '\0';
    std::string path(buf);
    size_t slash = path.rfind('/');
    return slash != std::string::npos ? path.substr(0, slash) : ".";
}

struct EngineRun {
    int exitCode = -1;
    std::string output;
    std::string setupError;
};

EngineRun Boot(bool timersOn) {
    EngineRun r;
    std::string bin = BinaryDir() + "/dc3-native";
    struct stat st;
    if (stat(bin.c_str(), &st) != 0) {
        r.setupError = "dc3-native missing at " + bin +
                       " (scripts/native_test.sh builds it with milo-tests)";
        return r;
    }
    char tmpl[] = "/tmp/dc3-gpu-timers-XXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) {
        r.setupError = "mkdtemp failed";
        return r;
    }
    std::string root(dir);
    mkdir((root + "/cache").c_str(), 0755);
    mkdir((root + "/saves").c_str(), 0755);
    std::ostringstream cmd;
    cmd << kShellTimeoutLookup << (timersOn ? "MILO_GPU_TIMERS=1 " : "env -u MILO_GPU_TIMERS ")
        << "MILO_HEADLESS=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1 "
        << "DC3_FPS_CAP=240 DC3_AUDIO_GAIN=0 DC3_POSE=off DC3_HEARTBEAT_S=0 "
        << "DC3_CACHE_DIR=" << root << "/cache DC3_SAVE_DIR=" << root << "/saves "
        << "MILO_MAX_FRAMES=900 ${t:+$t 180} " << bin << " 2>&1";
    FILE* p = popen(cmd.str().c_str(), "r");
    if (!p) {
        r.setupError = "popen failed";
        return r;
    }
    char buf[4096];
    while (fgets(buf, sizeof(buf), p)) r.output += buf;
    int status = pclose(p);
    r.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return r;
}

bool Has(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }

std::vector<std::string> Lines(const std::string& s, const char* prefix) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string line;
    while (std::getline(in, line))
        if (line.compare(0, strlen(prefix), prefix) == 0) out.push_back(line);
    return out;
}

double Field(const std::string& line, const char* key) {
    std::string k = std::string(" ") + key + "=";
    size_t at = line.find(k);
    if (at == std::string::npos) return -1.0;
    return atof(line.c_str() + at + k.size());
}

std::string Tail(const std::string& s, size_t n = 4000) {
    return s.size() > n ? s.substr(s.size() - n) : s;
}

// ctest runs each TEST in its own process, so each run is made lazily, by the
// tests that read it: four tests share one "on" boot only within a process.
const EngineRun& OnRun() {
    static EngineRun r = Boot(true);
    return r;
}
const EngineRun& OffRun() {
    static EngineRun r = Boot(false);
    return r;
}

class GpuTimersTest : public ::testing::Test {
protected:
    const std::string& On() { return OnRun().output; }
    void RequireTimedOnRun() {
        ASSERT_TRUE(OnRun().setupError.empty()) << OnRun().setupError;
        if (Has(On(), "Null backend") || Has(On(), "GPU initialization failed"))
            GTEST_SKIP() << "no GPU (Null backend / GPU init failed): the timers have nothing to time";
        if (Has(On(), "GPU timers unavailable: the adapter does not offer"))
            GTEST_SKIP() << "this adapter has no timestamp-query";
    }
};

TEST_F(GpuTimersTest, OnPublishesPassTimes) {
    RequireTimedOnRun();
    if (IsSkipped() || HasFatalFailure()) return;
    ASSERT_TRUE(Has(On(), "GPU timers ON")) << Tail(On());
    std::vector<std::string> windows = Lines(On(), "[GPUTIME] window=");
    ASSERT_FALSE(windows.empty()) << "no [GPUTIME] window line in ~4 s of frames\n" << Tail(On());
    int timed = 0;
    for (const std::string& w : windows) {
        double frames = Field(w, "frames");
        if (frames <= 0) continue;
        timed++;
        double sum = Field(w, "gpu_sum_ms"), span = Field(w, "gpu_span_ms");
        EXPECT_GT(sum, 0.0) << w;
        EXPECT_LE(sum, span * 1.001 + 1e-3) << "pass sum beyond the frame's span: " << w;
    }
    EXPECT_GT(timed, 0) << "every window had 0 timed frames: the passes carry no timestamps\n"
                        << Tail(On());
}

TEST_F(GpuTimersTest, MainPassReportsTime) {
    RequireTimedOnRun();
    if (IsSkipped() || HasFatalFailure()) return;
    double best = -1.0;
    for (const std::string& l : Lines(On(), "[GPUTIME] pass=MainPass "))
        best = std::max(best, Field(l, "ms"));
    EXPECT_GT(best, 0.0) << "no MainPass row with ms > 0\n" << Tail(On());
}

TEST_F(GpuTimersTest, EveryPassIsNamed) {
    RequireTimedOnRun();
    if (IsSkipped() || HasFatalFailure()) return;
    std::vector<std::string> unnamed;
    for (const std::string& l : Lines(On(), "[GPUTIME] pass=unnamed@")) unnamed.push_back(l);
    EXPECT_TRUE(unnamed.empty())
        << "a BeginRenderPass site has no descriptor label (set rp.label at that site):\n"
        << (unnamed.empty() ? "" : unnamed.front());
}

TEST_F(GpuTimersTest, TimestampsAreNotQuantized) {
    RequireTimedOnRun();
    if (IsSkipped() || HasFatalFailure()) return;
    // Dawn enables timestamp_quantization by default natively; the engine
    // disables it and reads the device's toggles back (dawn::native::
    // GetTogglesUsed). "cannot be read" (a Dawn without dawn/native headers) is
    // not a failure of the timers, but it is not this box's Dawn either.
    EXPECT_TRUE(Has(On(), "timestamp_quantization off (device confirms)"))
        << "the device did not confirm timestamp_quantization is off:\n"
        << (Lines(On(), "GPU timers").empty() ? Tail(On()) : Lines(On(), "GPU timers").front());
}

TEST_F(GpuTimersTest, OffCreatesNothingAndPrintsNothing) {
    ASSERT_TRUE(OffRun().setupError.empty()) << OffRun().setupError;
    EXPECT_TRUE(Has(OffRun().output, "DC3 Native")) << "the off run did not boot\n" << Tail(OffRun().output);
    EXPECT_FALSE(Has(OffRun().output, "GPU timers")) << Tail(OffRun().output);
    EXPECT_FALSE(Has(OffRun().output, "[GPUTIME]")) << Tail(OffRun().output);
}

}  // namespace
