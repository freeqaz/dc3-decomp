// PLAN H-128: the thermal governor (engine gfx/ThermalGovernor.h carries the
// rule and its constants), the row cap it sets on top of the frame-size rules
// (ResolveFrameSizesThermal, TighterRowCap) and the level's knobs
// (platform/ThermalState.h). All pure: time is the test's.
//
// MEASURED 2026-11-19, fanless M3 MacBook Air at 2560x1440: thermal state 0 ->
// 2 after ~12 minutes; song select 62 -> 32-42 fps, a song 64-69 -> 45-51 fps.
#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "gfx/GpuDevice.h"
#include "gfx/ThermalGovernor.h"
#include "platform/ThermalState.h"

using milo::kThermalCritical;
using milo::kThermalFair;
using milo::kThermalNominal;
using milo::kThermalSerious;
using milo::kThermalUnknown;
using milo::ThermalGovernor;
using milo::ThermalGovernorConfig;
using milo::ThermalGovernorInput;

namespace {

// Drives a governor frame by frame and records every change of its cap.
struct Sim {
    ThermalGovernor gov;
    double now = 100.0;
    int cap = 0;
    int level = kThermalNominal;
    int baseRows = 1080;
    double target = 60.0;
    bool meaningful = true;
    double gpuMs = -1.0;  // the GPU's time for a frame (the rate rule); < 0 = none
    bool discrete = false;
    std::vector<std::pair<double, int>> changes;  // (time, new cap)

    Sim() = default;
    explicit Sim(const ThermalGovernorConfig& cfg) : gov(cfg) {}

    void Frame(double dt) {
        now += dt;
        ThermalGovernorInput in;
        in.nowSeconds = now;
        in.level = level;
        in.targetFps = target;
        in.baseRows = baseRows;
        in.paceMeaningful = meaningful;
        in.gpuFrameMs = gpuMs;
        in.discreteGpu = discrete;
        const int c = gov.Frame(in);
        if (c != cap) {
            changes.emplace_back(now, c);
            cap = c;
        }
    }
    // `seconds` of frames at a steady `fps`.
    void Run(double seconds, double fps) {
        const double dt = 1.0 / fps, end = now + seconds;
        while (now + dt <= end + 1e-9) Frame(dt);
    }
    // `seconds` of a `hz` vsync'd game averaging `fps`: one-refresh frames
    // with two-refresh frames spread evenly among them.
    void RunVsync(double seconds, double fps, double hz = 60.0) {
        const double refresh = 1.0 / hz, end = now + seconds;
        const double lateShare = hz / fps - 1.0;  // share of frames taking two refreshes
        double debt = 0.0;
        while (now < end) {
            debt += lateShare;
            if (debt >= 1.0) {
                debt -= 1.0;
                Frame(2 * refresh);
            } else {
                Frame(refresh);
            }
        }
    }
    std::vector<int> Caps() const {
        std::vector<int> v;
        for (auto& c : changes) v.push_back(c.second);
        return v;
    }
};

GpuDeviceDesc FollowingDesc() {
    GpuDeviceDesc d;
    d.outputFollowsTarget = true;
    d.logicalWidth = 1280;
    d.logicalHeight = 720;
    d.renderScale = 2.0f;            // the enhanced look's
    d.renderScaleIntegrated = 1.0f;
    d.outputMaxRowsIntegrated = 1080;
    return d;
}

bool Same(const FrameSizes& a, const FrameSizes& b) {
    return a.outputW == b.outputW && a.outputH == b.outputH && a.scale == b.scale &&
           a.drawnW == b.drawnW && a.drawnH == b.drawnH;
}

}  // namespace

// --- permission: the level ------------------------------------------------------

TEST(ThermalGovernor, ACoolMachineGivesUpNothingHoweverLateTheFrames) {
    for (int level : {kThermalNominal, kThermalUnknown}) {
        Sim s;
        s.level = level;
        s.Run(120.0, 30.0);
        EXPECT_EQ(s.cap, 0) << "level " << level;
        EXPECT_TRUE(s.changes.empty()) << "level " << level;
    }
}

TEST(ThermalGovernor, PressureWithoutLateFramesGivesUpNothing) {
    for (int level : {kThermalFair, kThermalSerious, kThermalCritical}) {
        Sim s;
        s.level = level;
        s.Run(300.0, 60.0);
        EXPECT_TRUE(s.changes.empty()) << "level " << level << ": the game held 60 fps";
    }
}

// --- trigger: late frames -------------------------------------------------------

TEST(ThermalGovernor, FairStepsDownAfterFiveLateSeconds) {
    Sim s;
    s.level = kThermalFair;
    const double t0 = s.now;
    s.Run(4.5, 40.0);
    EXPECT_TRUE(s.changes.empty()) << "stepped after " << (s.changes[0].first - t0) << " s";
    s.Run(2.0, 40.0);
    ASSERT_EQ(s.changes.size(), 1u);
    EXPECT_EQ(s.cap, 900);  // from the default cap's 1080 rows
    EXPECT_GE(s.changes[0].first - t0, 5.0);
    EXPECT_LE(s.changes[0].first - t0, 6.2);
}

TEST(ThermalGovernor, SeriousAndCriticalStepDownAfterTwoLateSeconds) {
    for (int level : {kThermalSerious, kThermalCritical}) {
        Sim s;
        s.level = level;
        const double t0 = s.now;
        s.Run(1.9, 40.0);
        EXPECT_TRUE(s.changes.empty()) << "level " << level;
        s.Run(1.3, 40.0);
        ASSERT_EQ(s.changes.size(), 1u) << "level " << level;
        EXPECT_EQ(s.cap, 900);
        EXPECT_LE(s.changes[0].first - t0, 3.2);
    }
}

TEST(ThermalGovernor, LateMeansUnderNinetyPercentOfTheTarget) {
    // 56 fps against 60 is inside the margin; 52 is not.
    Sim ok;
    ok.level = kThermalSerious;
    ok.Run(60.0, 56.0);
    EXPECT_TRUE(ok.changes.empty());
    Sim late;
    late.level = kThermalSerious;
    late.Run(5.0, 52.0);  // one step's worth of time
    EXPECT_EQ(late.cap, 900);
    // The measured throttle, as a 60 Hz display shows it: 51 fps is 18 % of
    // the frames a refresh late, 45 fps a third of them.
    for (double fps : {51.0, 45.0, 42.0, 32.0}) {
        Sim v;
        v.level = kThermalSerious;
        v.RunVsync(5.0, fps);
        EXPECT_EQ(v.cap, 900) << fps << " fps under vsync";
    }
    // A 30 fps target (a 30 Hz display) is not late at 30.
    Sim slowDisplay;
    slowDisplay.level = kThermalSerious;
    slowDisplay.target = 30.0;
    slowDisplay.Run(60.0, 30.0);
    EXPECT_TRUE(slowDisplay.changes.empty());
}

TEST(ThermalGovernor, AHitchIsNotLateFrames) {
    // A load under serious pressure: a 3 s frame, then two half-second frames
    // a second for a while, among frames on pace. No window has three late
    // frames, however low its rate.
    Sim s;
    s.level = kThermalSerious;
    s.Run(2.0, 60.0);
    s.Frame(3.0);
    s.Run(1.0, 60.0);
    for (int i = 0; i < 12; i++) {
        s.Frame(0.5);
        s.Run(0.25, 60.0);
        s.Frame(0.5);
        s.Run(0.25, 60.0);
    }
    s.Run(5.0, 60.0);
    EXPECT_TRUE(s.changes.empty()) << "a hitch stepped the output down to " << s.cap;
    // Control: the same wall time of steadily late frames does step.
    Sim c;
    c.level = kThermalSerious;
    c.Run(20.0, 40.0);
    EXPECT_FALSE(c.changes.empty());
}

TEST(ThermalGovernor, AnUnfocusedWindowsFramesAreNotJudged) {
    Sim s;
    s.level = kThermalSerious;
    s.meaningful = false;  // the compositor paces a hidden window (~1 fps)
    s.Run(60.0, 1.0);
    s.Run(60.0, 30.0);
    EXPECT_TRUE(s.changes.empty());
    s.meaningful = true;
    s.Run(4.0, 30.0);
    EXPECT_EQ(s.cap, 900);
}

TEST(ThermalGovernor, WithoutATargetRateNoFrameIsLate) {
    Sim s;
    s.level = kThermalCritical;
    s.target = 0.0;  // a consumer that never gave one (rb3, rb3-xenon)
    s.Run(120.0, 10.0);
    EXPECT_TRUE(s.changes.empty());
}

// --- the steps -------------------------------------------------------------------

TEST(ThermalGovernor, StepsAre1080Then900Then720AndNeverBelow) {
    // An uncapped 2560x1440 frame (the Mac launcher's MILO_OUTPUT_MAX_ROWS=0).
    Sim s;
    s.level = kThermalSerious;
    s.baseRows = 1440;
    s.Run(120.0, 30.0);
    EXPECT_EQ(s.Caps(), (std::vector<int>{1080, 900, 720}));
    EXPECT_EQ(s.cap, 720);
    // The default 1080 cap in force: the first step is 900.
    Sim d;
    d.level = kThermalSerious;
    d.baseRows = 1080;
    d.Run(120.0, 30.0);
    EXPECT_EQ(d.Caps(), (std::vector<int>{900, 720}));
    // An explicit 800-row cap: one step, to the floor.
    Sim e;
    e.level = kThermalSerious;
    e.baseRows = 800;
    e.Run(120.0, 30.0);
    EXPECT_EQ(e.Caps(), (std::vector<int>{720}));
    // 720 rows or fewer: nothing to give.
    for (int rows : {720, 600, 360}) {
        Sim f;
        f.level = kThermalCritical;
        f.baseRows = rows;
        f.Run(120.0, 20.0);
        EXPECT_TRUE(f.changes.empty()) << rows << " rows";
    }
    // A frame that does not follow its target has no row cap at all.
    Sim n;
    n.level = kThermalCritical;
    n.baseRows = 0;
    n.Run(120.0, 20.0);
    EXPECT_TRUE(n.changes.empty());

    EXPECT_EQ(ThermalGovernor::StepDown(0, 1440, 720), 1080);
    EXPECT_EQ(ThermalGovernor::StepDown(1080, 1440, 720), 900);
    EXPECT_EQ(ThermalGovernor::StepDown(900, 1440, 720), 720);
    EXPECT_EQ(ThermalGovernor::StepDown(720, 1440, 720), 720);
    EXPECT_EQ(ThermalGovernor::StepDown(0, 1080, 720), 900);
    EXPECT_EQ(ThermalGovernor::StepDown(0, 720, 720), 0);
    EXPECT_EQ(ThermalGovernor::StepDown(0, 1000, 900), 900);  // the floor is the config's
    EXPECT_EQ(ThermalGovernor::StepDown(900, 1000, 900), 900);
    EXPECT_EQ(ThermalGovernor::StepUp(720, 1440), 900);
    EXPECT_EQ(ThermalGovernor::StepUp(900, 1440), 1080);
    EXPECT_EQ(ThermalGovernor::StepUp(1080, 1440), 0);
    EXPECT_EQ(ThermalGovernor::StepUp(900, 1080), 0);  // 1080 would not bind: released
    EXPECT_EQ(ThermalGovernor::StepUp(0, 1440), 0);
}

TEST(ThermalGovernor, AChangeSettlesBeforeTheNextStepDown) {
    Sim s;
    s.level = kThermalSerious;
    s.baseRows = 1440;
    s.Run(60.0, 30.0);
    ASSERT_EQ(s.changes.size(), 3u);
    // 5 s to settle and two late windows: 7 s apart at least.
    EXPECT_GE(s.changes[1].first - s.changes[0].first, 7.0);
    EXPECT_GE(s.changes[2].first - s.changes[1].first, 7.0);
    EXPECT_LE(s.changes[1].first - s.changes[0].first, 8.2);
}

// --- back up ---------------------------------------------------------------------

TEST(ThermalGovernor, CoolingGivesBackOneStepPerNinetySecondDwell) {
    Sim s;
    s.level = kThermalSerious;
    s.baseRows = 1440;
    s.Run(60.0, 30.0);
    ASSERT_EQ(s.cap, 720);
    s.changes.clear();
    s.level = kThermalNominal;
    const double t0 = s.now;
    s.Run(89.0, 60.0);
    EXPECT_TRUE(s.changes.empty()) << "gave a step back after " << (s.changes[0].first - t0);
    s.Run(2.0, 60.0);
    ASSERT_EQ(s.changes.size(), 1u);
    EXPECT_EQ(s.cap, 900);
    s.Run(88.0, 60.0);
    EXPECT_EQ(s.changes.size(), 1u) << "the second step needs a whole dwell of its own";
    s.Run(2.5, 60.0);
    EXPECT_EQ(s.cap, 1080);
    s.Run(91.0, 60.0);
    EXPECT_EQ(s.Caps(), (std::vector<int>{900, 1080, 0}));
    EXPECT_EQ(s.gov.StepsUp(), 3);
    // Given back because the machine cooled, late frames or not.
    Sim l;
    l.level = kThermalSerious;
    l.Run(6.0, 30.0);
    ASSERT_EQ(l.cap, 900);
    l.level = kThermalNominal;
    l.Run(92.0, 30.0);
    EXPECT_EQ(l.cap, 0);
}

TEST(ThermalGovernor, ALevelThatWobblesHoldsTheSize) {
    Sim s;
    s.level = kThermalSerious;
    s.Run(6.0, 30.0);
    ASSERT_EQ(s.cap, 900);
    s.changes.clear();
    // Ten minutes of nominal for 80 s, fair for 5 s, on pace throughout.
    for (int i = 0; i < 7; i++) {
        s.level = kThermalNominal;
        s.Run(80.0, 60.0);
        s.level = kThermalFair;
        s.Run(5.0, 60.0);
    }
    EXPECT_TRUE(s.changes.empty()) << s.changes.size() << " changes while the level wobbled";
    EXPECT_EQ(s.cap, 900);
}

// --- the switch ------------------------------------------------------------------

TEST(ThermalGovernor, SwitchedOffItNeverActs) {
    ThermalGovernorConfig off;
    off.enabled = false;
    Sim s(off);
    s.level = kThermalCritical;
    s.baseRows = 1440;
    s.Run(120.0, 20.0);
    EXPECT_TRUE(s.changes.empty());
    EXPECT_EQ(s.gov.RowCap(), 0);
    EXPECT_EQ(s.gov.StepsDown(), 0);

    EXPECT_TRUE(milo::ParseThermalGovernorKnob(nullptr));
    EXPECT_TRUE(milo::ParseThermalGovernorKnob(""));
    EXPECT_TRUE(milo::ParseThermalGovernorKnob("1"));
    EXPECT_FALSE(milo::ParseThermalGovernorKnob("0"));
    EXPECT_FALSE(milo::ParseThermalGovernorKnob("off"));
}

TEST(ThermalGovernor, TheLevelKnobTakesZeroToThreeOnly) {
    EXPECT_EQ(milo::ParseThermalStateKnob("0"), kThermalNominal);
    EXPECT_EQ(milo::ParseThermalStateKnob("1"), kThermalFair);
    EXPECT_EQ(milo::ParseThermalStateKnob("2"), kThermalSerious);
    EXPECT_EQ(milo::ParseThermalStateKnob("3"), kThermalCritical);
    for (const char* bad : {"", "4", "-1", "12", "os", "serious"})
        EXPECT_EQ(milo::ParseThermalStateKnob(bad), kThermalUnknown) << bad;
    EXPECT_EQ(milo::ParseThermalStateKnob(nullptr), kThermalUnknown);
    EXPECT_STREQ(milo::ThermalLevelName(kThermalSerious), "serious");
    EXPECT_STREQ(milo::ThermalLevelName(kThermalUnknown), "unknown");
    // No level injected in this process: every platform but macOS has none.
#ifndef __APPLE__
    EXPECT_EQ(milo::ThermalOsLevel(), kThermalUnknown);
#endif
}

// --- the cap on top of the frame-size rules ---------------------------------------

TEST(ThermalRowCap, TheTighterCapWins) {
    EXPECT_EQ(TighterRowCap(0, 0), 0);
    EXPECT_EQ(TighterRowCap(1080, 0), 1080);
    EXPECT_EQ(TighterRowCap(0, 900), 900);
    EXPECT_EQ(TighterRowCap(1080, 900), 900);
    EXPECT_EQ(TighterRowCap(800, 900), 800);
    EXPECT_EQ(TighterRowCap(-1, 900), 900);
}

TEST(ThermalRowCap, NoRuntimeCapIsExactlyTheRulesOwnSizes) {
    // Every (adapter, target, cap knob) the frame-size tests walk, with no
    // runtime cap: field for field what ResolveFrameSizes gives.
    const std::pair<int, int> sizes[] = {{1280, 720},  {1920, 1080}, {2560, 1440},
                                         {2560, 1600}, {3840, 2160}, {640, 300}};
    for (bool discrete : {false, true})
        for (bool presents : {false, true})
            for (int explicitRows : {-1, 0, 800, 1440})
                for (const std::pair<int, int>& wh : sizes) {
                    GpuDeviceDesc d = FollowingDesc();
                    d.outputMaxRowsExplicit = explicitRows;
                    const FrameSizes want =
                        ResolveFrameSizes(d, discrete, presents, wh.first, wh.second);
                    const FrameSizes got = ResolveFrameSizesThermal(
                        d, discrete, presents, wh.first, wh.second,
                        OutputRowCap(d, discrete, presents), 0);
                    EXPECT_TRUE(Same(want, got))
                        << discrete << presents << " rows " << explicitRows << " " << wh.first
                        << "x" << wh.second;
                }
}

TEST(ThermalRowCap, StepsOnAnIntegratedAdaptersWindow) {
    const GpuDeviceDesc d = FollowingDesc();
    const int base = OutputRowCap(d, false, true);
    ASSERT_EQ(base, 1080);
    FrameSizes s = ResolveFrameSizesThermal(d, false, true, 2560, 1440, base, 900);
    EXPECT_EQ(s.outputW, 1600);
    EXPECT_EQ(s.outputH, 900);
    EXPECT_EQ(s.drawnW, 1600);
    EXPECT_EQ(s.drawnH, 900);
    s = ResolveFrameSizesThermal(d, false, true, 2560, 1440, base, 720);
    EXPECT_EQ(s.outputW, 1280);
    EXPECT_EQ(s.outputH, 720);
    // The Mac launcher's uncapped frame: 1080 is the first step.
    GpuDeviceDesc uncapped = d;
    uncapped.outputMaxRowsExplicit = 0;
    s = ResolveFrameSizesThermal(uncapped, false, true, 2560, 1440, 0, 0);
    EXPECT_EQ(s.outputH, 1440);
    s = ResolveFrameSizesThermal(uncapped, false, true, 2560, 1440, 0, 1080);
    EXPECT_EQ(s.outputW, 1920);
    EXPECT_EQ(s.outputH, 1080);
    // A headless frame (an injected level): 1920x1080 to 1600x900.
    s = ResolveFrameSizesThermal(d, true, false, 1920, 1080, OutputRowCap(d, true, false), 900);
    EXPECT_EQ(s.outputW, 1600);
    EXPECT_EQ(s.outputH, 900);
}

TEST(ThermalRowCap, ACapThatDoesNotBindChangesNothing) {
    GpuDeviceDesc d = FollowingDesc();
    d.outputMaxRowsExplicit = 800;  // asked for, tighter than 1080 and 900
    const int base = OutputRowCap(d, false, true);
    const FrameSizes own = ResolveFrameSizes(d, false, true, 2560, 1440);
    EXPECT_EQ(own.outputH, 800);
    EXPECT_TRUE(Same(own, ResolveFrameSizesThermal(d, false, true, 2560, 1440, base, 1080)));
    EXPECT_TRUE(Same(own, ResolveFrameSizesThermal(d, false, true, 2560, 1440, base, 900)));
    EXPECT_EQ(ResolveFrameSizesThermal(d, false, true, 2560, 1440, base, 720).outputH, 720);
    // A 1280x720 window is already under every step.
    const GpuDeviceDesc plain = FollowingDesc();
    const FrameSizes small = ResolveFrameSizes(plain, false, true, 1280, 720);
    EXPECT_TRUE(Same(small, ResolveFrameSizesThermal(plain, false, true, 1280, 720, 1080, 900)));
}

TEST(ThermalRowCap, ACappedFrameIsNeverDrawnLargerThanTheUncappedOne) {
    // A discrete adapter's 2560x1440 window draws 1x (2x would pass
    // 3840x2160). Capped to 1080 rows the default scale rule would supersample
    // 2x -- 3840x2160 drawn, 2.25x the pixels of the frame it replaces.
    const GpuDeviceDesc d = FollowingDesc();
    const FrameSizes base = ResolveFrameSizes(d, true, true, 2560, 1440);
    ASSERT_EQ(base.drawnW, 2560);
    ASSERT_EQ(base.scale, 1.0f);
    ASSERT_EQ(ResolveFrameSizesWithCap(d, true, true, 2560, 1440, 1080).drawnW, 3840)
        << "the premise: the cap alone would raise the render scale";
    for (int cap : {1080, 900, 720}) {
        const FrameSizes s = ResolveFrameSizesThermal(d, true, true, 2560, 1440, 0, cap);
        EXPECT_EQ(s.outputH, cap);
        EXPECT_EQ(s.scale, 1.0f) << cap;
        EXPECT_LT((long)s.drawnW * s.drawnH, (long)base.drawnW * base.drawnH) << cap;
    }
}

// --- the rate rule: outputs drawn above 1080 rows (gfx/ThermalGovernor.h) --------
//
// MEASURED 2026-11-23, the M3 Air on a 4K TV at 30 Hz with the cap lifted
// (MILO_OUTPUT_MAX_ROWS=0): 3840x2160, songs 12.4-23.8 fps against 30 for half
// an hour at thermal level nominal; the thermal rule never acted.

TEST(ThermalRateRule, ACoolMachineDrawing2160RowsLateStepsTo1440Then1080) {
    for (int level : {kThermalNominal, kThermalUnknown}) {
        Sim s;
        s.level = level;
        s.baseRows = 2160;
        s.target = 30.0;  // the 30 Hz TV
        s.RunVsync(7.5, 20.0, 30.0);
        EXPECT_TRUE(s.changes.empty()) << "level " << level << ": 8 late seconds, not 7";
        s.RunVsync(120.0, 20.0, 30.0);
        EXPECT_EQ(s.Caps(), (std::vector<int>{1440, 1080})) << "level " << level;
        EXPECT_EQ(s.gov.RateStepsDown(), 2);
        EXPECT_EQ(s.gov.StepsDown(), 0) << "the thermal rule's count (its notice) is untouched";
        ASSERT_EQ(s.changes.size(), 2u);
        EXPECT_GE(s.changes[1].first - s.changes[0].first, 13.0) << "5 s settle + 8 late windows";
    }
}

TEST(ThermalRateRule, NeverTakesAFrameTo1080RowsOrUnder) {
    for (int rows : {1080, 1000, 720}) {
        Sim s;
        s.baseRows = rows;
        s.Run(300.0, 12.0);
        EXPECT_TRUE(s.changes.empty()) << rows << " rows";
    }
    Sim s;  // a 1200-row window on a discrete GPU: one step, to 1080
    s.baseRows = 1200;
    s.Run(300.0, 12.0);
    EXPECT_EQ(s.Caps(), (std::vector<int>{1080}));
    EXPECT_EQ(ThermalGovernor::RateStepDown(0, 2160, 1080), 1440);
    EXPECT_EQ(ThermalGovernor::RateStepDown(1440, 2160, 1080), 1080);
    EXPECT_EQ(ThermalGovernor::RateStepDown(1080, 2160, 1080), 1080);
    EXPECT_EQ(ThermalGovernor::RateStepDown(0, 1440, 1080), 1080);
    EXPECT_EQ(ThermalGovernor::RateStepDown(0, 1080, 1080), 0);
    EXPECT_EQ(ThermalGovernor::RateStepUp(1080, 2160), 1440);
    EXPECT_EQ(ThermalGovernor::RateStepUp(1440, 2160), 0);
    EXPECT_EQ(ThermalGovernor::RateStepUp(1080, 1440), 0);  // 1440 would not bind
}

TEST(ThermalRateRule, LateFramesTheGpuIsNotSlowForKeepTheSize) {
    // MEASURED 2026-11-24, RTX 3090 headless at 3840x2160 under load: boot ran
    // 28.7 fps against 60 with the GPU's span 0.6-5.7 ms. Fewer pixels would
    // not have made it faster.
    Sim cpu;
    cpu.baseRows = 2160;
    cpu.gpuMs = 5.0;  // under 0.75 * 16.7 ms
    cpu.Run(120.0, 28.7);
    EXPECT_TRUE(cpu.changes.empty());
    Sim gpu;  // the same late frames with the GPU at 14 ms: the pixels are why
    gpu.baseRows = 2160;
    gpu.gpuMs = 14.0;
    gpu.Run(120.0, 28.7);
    EXPECT_EQ(gpu.Caps(), (std::vector<int>{1440, 1080}));
    // Not measured: an adapter that is not discrete is blamed (the Mac's
    // case), a discrete one never is.
    Sim d;
    d.baseRows = 2160;
    d.discrete = true;
    d.Run(120.0, 20.0);
    EXPECT_TRUE(d.changes.empty());
    d.gpuMs = 30.0;
    d.Run(15.0, 20.0);  // one step (the second needs 5 s settle + 8 more)
    EXPECT_EQ(d.Caps(), (std::vector<int>{1440}));
    // One window the GPU is not blamed for breaks the run of eight.
    Sim b;
    b.baseRows = 2160;
    b.gpuMs = 14.0;
    for (int i = 0; i < 10; i++) {
        b.Run(6.0, 28.7);
        b.gpuMs = 5.0;
        b.Run(1.5, 28.7);
        b.gpuMs = 14.0;
    }
    EXPECT_TRUE(b.changes.empty());
}

TEST(ThermalRateRule, ASteady30OnA30HzDisplayIsNotLate) {
    // The target is the rate the display can show (dc3's Dc3ThermalTargetFps):
    // 30 frames a second on a 30 Hz TV is every frame on time.
    Sim s;
    s.baseRows = 2160;
    s.target = 30.0;
    s.RunVsync(600.0, 30.0, 30.0);
    EXPECT_TRUE(s.changes.empty());
    // 29 of them, a frame in thirty a refresh late, is not late either: 90 %.
    s.RunVsync(600.0, 29.0, 30.0);
    EXPECT_TRUE(s.changes.empty());
    // The same 30 fps judged against 60 WOULD be late: the target is what
    // decides, which is why it must be the display's.
    Sim w;
    w.baseRows = 2160;
    w.target = 60.0;
    w.RunVsync(60.0, 30.0, 60.0);
    EXPECT_FALSE(w.changes.empty());
}

TEST(ThermalRateRule, AFewSlowSecondsDoNotCostTheSize) {
    Sim s;
    s.baseRows = 2160;
    for (int i = 0; i < 20; i++) {  // 6 slow seconds, then 4 on time, again and again
        s.Run(6.0, 30.0);
        s.Run(4.0, 60.0);
    }
    EXPECT_TRUE(s.changes.empty());
    // A load's hitch (one 3 s frame) inside a steady run is not a late window.
    s.Run(5.0, 60.0);
    s.Frame(3.0);
    s.Run(30.0, 60.0);
    EXPECT_TRUE(s.changes.empty());
}

// Stepped down at nominal on the Mac's numbers, then held at a steady 30.
static Sim SteppedTo(int cap) {
    Sim s;
    s.baseRows = 2160;
    s.target = 30.0;
    while (s.cap != cap && s.now < 400.0) s.RunVsync(1.0, 20.0, 30.0);
    s.RunVsync(6.0, 30.0, 30.0);  // on time, through the change's settle
    return s;
}

TEST(ThermalRateRule, OnTimeFramesAloneNeverGiveTheSizeBack) {
    Sim s = SteppedTo(1440);
    ASSERT_EQ(s.cap, 1440);
    s.changes.clear();
    s.RunVsync(900.0, 30.0, 30.0);  // every frame on time, no GPU time known
    EXPECT_TRUE(s.changes.empty()) << "under vsync on time says nothing about the next size";
    s.gpuMs = 15.5;  // the Mac at 1440 rows: 15.5 * 2.25 = 34.9 ms > 0.7 * 33.3
    s.RunVsync(900.0, 30.0, 30.0);
    EXPECT_TRUE(s.changes.empty());
    EXPECT_GT(s.gov.LastPredictedMs(), s.gov.LastBudgetMs());
}

TEST(ThermalRateRule, MeasuredHeadroomGivesOneStepBackPerThirtySeconds) {
    Sim s = SteppedTo(1080);
    ASSERT_EQ(s.cap, 1080);
    s.changes.clear();
    s.gpuMs = 5.8;  // 1080 rows on the Mac, cool: 5.8 * (1440/1080)^2 = 10.3 ms <= 23.3
    const double t0 = s.now;
    s.RunVsync(29.0, 30.0, 30.0);
    EXPECT_TRUE(s.changes.empty()) << "gave a step back after " << s.changes[0].first - t0;
    s.RunVsync(3.5, 30.0, 30.0);
    EXPECT_EQ(s.Caps(), (std::vector<int>{1440}));
    // At 1440 rows the measured time grows; 9 ms predicts 20.3 at 2160: under 23.3.
    s.gpuMs = 9.0;
    s.RunVsync(40.0, 30.0, 30.0);  // 5 s settle, then the dwell
    EXPECT_EQ(s.Caps(), (std::vector<int>{1440, 0})) << "released: the size asked for";
    EXPECT_EQ(s.gov.RateStepsUp(), 2);
    // One window without headroom restarts the dwell.
    Sim b = SteppedTo(1080);
    b.changes.clear();
    b.gpuMs = 5.8;
    b.RunVsync(20.0, 30.0, 30.0);
    b.gpuMs = 20.0;
    b.RunVsync(1.5, 30.0, 30.0);
    b.gpuMs = 5.8;
    b.RunVsync(20.0, 30.0, 30.0);
    EXPECT_TRUE(b.changes.empty());
}

TEST(ThermalRateRule, AStepUpThatDoesNotHoldWaitsTwiceAsLongNextTime) {
    Sim s = SteppedTo(1440);
    s.changes.clear();
    s.gpuMs = 4.0;
    s.RunVsync(33.0, 30.0, 30.0);
    ASSERT_EQ(s.Caps(), (std::vector<int>{0}));
    s.gpuMs = 40.0;  // the full size is late again, and the GPU is why
    s.RunVsync(20.0, 20.0, 30.0);
    ASSERT_EQ(s.Caps(), (std::vector<int>{0, 1440}));
    EXPECT_EQ(s.gov.RateUpDwellSeconds(), 60.0);
    const double t0 = s.now;
    s.gpuMs = 4.0;
    s.RunVsync(50.0, 30.0, 30.0);
    EXPECT_EQ(s.changes.size(), 2u) << "back up after " << s.now - t0 << " s; the dwell is 60";
    s.RunVsync(20.0, 30.0, 30.0);
    EXPECT_EQ(s.Caps(), (std::vector<int>{0, 1440, 0}));
}

TEST(ThermalRateRule, ThermalPressureStillOwnsEverythingBelow1080) {
    Sim s = SteppedTo(1440);
    s.changes.clear();
    s.level = kThermalSerious;
    s.RunVsync(60.0, 20.0, 30.0);
    EXPECT_EQ(s.Caps(), (std::vector<int>{1080, 900, 720}));
    EXPECT_EQ(s.gov.StepsDown(), 3);
    EXPECT_EQ(s.gov.RateCap(), 1440);
    s.changes.clear();
    s.level = kThermalNominal;
    s.gpuMs = 2.0;  // plenty, but the thermal rule gives its own steps back first
    s.RunVsync(275.0, 30.0, 30.0);
    EXPECT_EQ(s.Caps(), (std::vector<int>{900, 1080, 1440}));
    EXPECT_EQ(s.gov.ThermalCap(), 0);
}

TEST(ThermalRateRule, SwitchedOffAnOversizedFrameKeepsItsSize) {
    ThermalGovernorConfig off;
    off.rateRule = false;
    Sim s(off);
    s.baseRows = 2160;
    s.target = 30.0;
    s.RunVsync(300.0, 15.0, 30.0);
    EXPECT_TRUE(s.changes.empty());
    // The thermal rule alone, as before.
    s.level = kThermalSerious;
    s.RunVsync(6.0, 15.0, 30.0);  // two late windows: one step, then the settle
    EXPECT_EQ(s.Caps(), (std::vector<int>{1080}));
}

TEST(ThermalRateRule, AWindowMadeSmallerThanTheCapStartsAfresh) {
    Sim s = SteppedTo(1440);
    ASSERT_EQ(s.cap, 1440);
    s.baseRows = 1200;  // the player shrank the window: 1440 does not bind
    s.RunVsync(2.0, 30.0, 30.0);
    EXPECT_EQ(s.gov.RateCap(), 0);
    s.baseRows = 2160;  // and made it big again: the size asked for, judged afresh
    s.RunVsync(2.0, 30.0, 30.0);
    EXPECT_EQ(s.gov.RowCap(), 0);
}
