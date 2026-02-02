#include "meta/DeJitterPanel.h"
#include "ui/UIPanel.h"
#include "utl/DeJitter.h"

DeJitterPanel::DeJitterPanel() : unkf8(true) {}

DeJitterPanel::~DeJitterPanel() {}

void DeJitterPanel::Enter() {
    unk68.Reset();
    unkf8 = true;
    DeJitterSetter setter(unk68, 0);
    UIPanel::Enter();
}

void DeJitterPanel::Poll() {
    // First frame only: prime the jitter state
    if (unkf8) {
        unk38.Restart();
        float f;
        unk68.NewMs(0, f);
    }
    {
        // Use scoped time correction: pass timer on subsequent frames, nullptr on first
        DeJitterSetter setter(unk68, unkf8 ? nullptr : &unk38);
        UIPanel::Poll();
    }
    unkf8 = false;
}

DeJitterSetter::DeJitterSetter(DeJitter &dj, Timer *t) {
    // Save current time state for restoration in destructor
    secs = TheTaskMgr.Seconds(TaskMgr::kRealTime);
    delta_secs = TheTaskMgr.DeltaSeconds();
    float f1 = 0.0f;
    float f18 = 0.0f;
    if (t) {
        // Apply jitter correction via DeJitter, convert ms to seconds
        f1 = dj.NewMs(t->SplitMs(), f18) * 0.001f;
        f18 *= 0.001f;
    }
    // Set corrected time for the duration of this scope
    TheTaskMgr.SetTimeAndDelta(kTaskSeconds, f1, f18);
}

DeJitterSetter::~DeJitterSetter() {
    TheTaskMgr.SetTimeAndDelta(kTaskSeconds, secs, delta_secs);
}
