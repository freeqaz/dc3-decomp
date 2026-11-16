// test_frame_times.cpp -- the frame-times log's line shape (MILO_FRAME_TIMES,
// WgpuRnd::WriteFrameTimesHeader / WriteFrameTimes; np-gfx spotperf round 6,
// branch np-texwarm, PLAN H-116).
//
// Round 6 read the log off the web build's console: MILO_FRAME_TIMES=- puts
// the rows on stdout, each line "[FT] " prefixed so a driver can pick them out
// of the engine's other output. This pins:
//  - the header and every row carry the prefix on stdout, and neither does in
//    a file (a file is plain CSV);
//  - a row has as many columns as the header names (a column added to one and
//    not the other shifts every later column, silently);
//  - the first call only starts the clock: there is no previous frame to
//    measure, so no row;
//  - the last column, end_s (round 7, PLAN H-118), is the row's EndDrawing on
//    the steady clock in seconds: between steady_clock readings taken around
//    the calls, and rising row to row (perf samples taken with
//    -k CLOCK_MONOTONIC are put in their frame by it).
//
// The test points the log at its own file (test_tmp.h) and sets
// mFrameTimesStdout by hand: what stdout gets is the same bytes.
//
// Sabotage (dc3-decomp archive/np-gfx-2026-11-14/lanes/spotperf/round6):
//  - the row's prefix dropped -> red;
//  - the prefix written in file mode too -> red;
//  - a column added to the row and not the header -> red;
//  - a row written on the first call -> red;
//  - end_s written as the frame's begin, or as seconds since the log opened
//    (round 7) -> red.

#include "test_helpers.h"
#include "test_tmp.h"

#include "platform/Rnd_Wgpu.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> ReadLines(const std::string &path) {
    std::vector<std::string> out;
    std::ifstream in(path);
    for (std::string l; std::getline(in, l);) out.push_back(l);
    return out;
}

int Columns(const std::string &line) {
    int n = 1;
    for (char c : line) n += c == ',';
    return n;
}

class FrameTimesTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd) GTEST_SKIP() << "no WgpuRnd";
        if (gWgpuRnd->mFrameTimes) GTEST_SKIP() << "MILO_FRAME_TIMES is set for this run";
    }
    void TearDown() override {
        if (gWgpuRnd && mFile) {
            fclose(mFile);
            gWgpuRnd->mFrameTimes = nullptr;
            gWgpuRnd->mFrameTimesStdout = false;
            gWgpuRnd->mFrameTimesLastEnd = 0.0;
        }
    }
    // Header, then WriteFrameTimes `calls` times; the file's lines.
    std::vector<std::string> Log(bool onStdout, int calls) {
        const std::string path = TestTempFile(onStdout ? "frame-times-ft" : "frame-times-csv", ".csv");
        mFile = fopen(path.c_str(), "w");
        EXPECT_NE(mFile, nullptr) << path;
        if (!mFile) return {};
        gWgpuRnd->mFrameTimes = mFile;
        gWgpuRnd->mFrameTimesStdout = onStdout;
        gWgpuRnd->mFrameTimesLastEnd = 0.0;
        gWgpuRnd->WriteFrameTimesHeader();
        for (int i = 0; i < calls; i++) gWgpuRnd->WriteFrameTimes();
        fflush(mFile);
        return ReadLines(path);
    }
    FILE *mFile = nullptr;
};

} // namespace

TEST_F(FrameTimesTest, OnStdoutEveryLineIsPrefixedAndTheColumnsAgree) {
    const std::vector<std::string> lines = Log(true, 3);
    ASSERT_EQ(lines.size(), 3u) << "header + 2 rows (the first call only starts the clock)";
    for (const std::string &l : lines) EXPECT_EQ(l.rfind("[FT] ", 0), 0u) << l;
    EXPECT_EQ(lines[0].rfind("[FT] frame,period_ms,", 0), 0u) << lines[0];
    const int header = Columns(lines[0]);
    for (size_t i = 1; i < lines.size(); i++) EXPECT_EQ(Columns(lines[i]), header) << lines[i];
}

TEST_F(FrameTimesTest, InAFileTheLinesArePlainCsv) {
    const std::vector<std::string> lines = Log(false, 2);
    ASSERT_EQ(lines.size(), 2u) << "header + 1 row";
    EXPECT_EQ(lines[0].rfind("frame,period_ms,", 0), 0u) << lines[0];
    EXPECT_EQ(lines[1].find("[FT]"), std::string::npos) << lines[1];
    EXPECT_EQ(Columns(lines[1]), Columns(lines[0])) << lines[1];
}

TEST_F(FrameTimesTest, EndSIsTheSteadyClockAtEachRowsEnd) {
    using namespace std::chrono;
    const double before = duration<double>(steady_clock::now().time_since_epoch()).count();
    const std::vector<std::string> lines = Log(false, 4);
    const double after = duration<double>(steady_clock::now().time_since_epoch()).count();
    ASSERT_EQ(lines.size(), 4u) << "header + 3 rows";
    const std::string &header = lines[0];
    EXPECT_EQ(header.substr(header.rfind(',') + 1), "end_s") << header;
    double last = before;
    for (size_t i = 1; i < lines.size(); i++) {
        const double end = std::strtod(lines[i].c_str() + lines[i].rfind(',') + 1, nullptr);
        EXPECT_GT(end, last) << lines[i];
        EXPECT_LE(end, after) << lines[i];
        last = end;
    }
}
