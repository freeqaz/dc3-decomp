#include "utl\MultiTempoTempoMap.h"
#include "os\Debug.h"
#include "utl\MemMgr.h"
#include "utl\Std.h"
#include <cmath>
#include <algorithm>

bool MultiTempoTempoMap::CompareTick(
    float tick, const MultiTempoTempoMap::TempoInfoPoint &pt
) {
    return tick < pt.mTick;
}

bool MultiTempoTempoMap::CompareTime(
    float time, const MultiTempoTempoMap::TempoInfoPoint &pt
) {
    return time < pt.mMs;
}

float MultiTempoTempoMap::GetTempoBPM(int tick) const {
    return 60000.0f / GetTempo(tick);
}

void MultiTempoTempoMap::ClearLoopPoints() {
    mStartLoopTick = -1.0f;
    mEndLoopTick = -1.0f;
    mStartLoopTime = -1.0f;
    mEndLoopTime = -1.0f;
}

void MultiTempoTempoMap::SetLoopPoints(int start, int end) {
    mStartLoopTick = start;
    mEndLoopTick = end;
    mStartLoopTime = TickToTime(mStartLoopTick);
    mEndLoopTime = TickToTime(mEndLoopTick);
}

int MultiTempoTempoMap::GetLoopTick(int tick, int &loopOffset) const {
    if (mStartLoopTick < 0.0f) {
        return tick;
    }

    loopOffset = 0;

    int startTick = static_cast<int>(mStartLoopTick);
    int endTick = static_cast<int>(mEndLoopTick);

    if (tick >= mEndLoopTick && mStartLoopTick != mEndLoopTick) {
        int loopTick = tick - startTick;
        int loopLength = endTick - startTick;
        int newTick = (loopTick % loopLength) + startTick;
        loopOffset = tick - newTick;
        return newTick;
    }
    return tick;
}

int MultiTempoTempoMap::GetLoopTick(int tick) const {
    int unused;
    return GetLoopTick(tick, unused);
}

float MultiTempoTempoMap::GetTimeInLoop(float time) {
    if (mStartLoopTick == -1.0f) {
        return time;
    }

    float startTime = TickToTime(mStartLoopTick);
    if (time < startTime) {
        return time;
    }

    float endTime = TickToTime(mEndLoopTick);

    float loopLength = endTime - startTime;
    float timeFromStart = time - startTime;
    MILO_ASSERT(timeFromStart >= 0.0f, 0xE3);

    float a = std::floor(timeFromStart / loopLength);
    return startTime + (timeFromStart - loopLength * a);
}

int MultiTempoTempoMap::GetNumTempoChangePoints() const { return mTempoPoints.size(); }

int MultiTempoTempoMap::GetTempoChangePoint(int index) const {
    MILO_ASSERT(index < mTempoPoints.size(), 0xF7);
    return mTempoPoints[index].mTick;
}

const MultiTempoTempoMap::TempoInfoPoint *MultiTempoTempoMap::PointForTick(float tick
) const {
    if (mTempoPoints.size() < 1) {
        MILO_NOTIFY("Tempo map is empty; at least one tempo map entry is required");
        return mTempoPoints.end();
    }

    // Find first point with tick > search tick, then step back to get point at or before
    const TempoInfoPoint *pt2 =
        std::upper_bound(mTempoPoints.begin(), mTempoPoints.end(), tick, CompareTick);
    if (pt2 != mTempoPoints.begin()) {
        pt2--;
    }

    return pt2;
}

const MultiTempoTempoMap::TempoInfoPoint *MultiTempoTempoMap::PointForTime(float time
) const {
    MILO_ASSERT(mTempoPoints.size() >= 1, 0x121);

    // upper_bound takes the parameter by reference: the image spills `time` to
    // its own home slot (0x8c(r1)) and passes that; there is no local copy.
    const TempoInfoPoint *pt2 =
        std::upper_bound(mTempoPoints.begin(), mTempoPoints.end(), time, CompareTime);
    if (pt2 != mTempoPoints.begin()) {
        pt2--;
    }

    return pt2;
}

float MultiTempoTempoMap::GetTempo(int tick) const {
    const TempoInfoPoint *pt = PointForTick(tick);
    if (pt != mTempoPoints.end())
        return (float)pt->mTempo / 1000.0f;
    else
        return 800.0f;
}

int MultiTempoTempoMap::GetTempoInMicroseconds(int tick) const {
    const TempoInfoPoint *pt = PointForTick(tick);
    if (pt != mTempoPoints.end())
        return pt->mTempo;
    else
        return 800000;
}

float MultiTempoTempoMap::TickToTime(float tick) const {
    if (tick == 0.0f)
        return 0.0f;

    if (mStartLoopTick < 0.0f || tick <= mEndLoopTick) {
        const TempoInfoPoint *pt = PointForTick(tick);
        if (pt == mTempoPoints.end())
            return 0.0f;
        else
            return pt->mMs
                + (pt->mTempo * (tick - pt->mTick) / 480.0f / 1000.0f);
    } else {
        float loopTickLength = mEndLoopTick - mStartLoopTick;
        float loopTimeLength = mEndLoopTime - mStartLoopTime;
        float loopTick = tick - mEndLoopTick;
        float loopPercent = std::floor(loopTick / loopTickLength);

        float loopTime = loopTimeLength * loopPercent + mEndLoopTime;
        loopTime += TickToTime(loopTick - loopTickLength * loopPercent + mStartLoopTick)
            - mStartLoopTime;
        return loopTime;
    }
}

float MultiTempoTempoMap::TimeToTick(float time) const {
    if (time == 0.0f)
        return 0.0f;

    // need to load up-front to prevent re-loads in the `else` block
    float endTime; // = mEndLoopTime;

    if (mStartLoopTick < 0.0f || mEndLoopTick < 0.0f
        || time <= (endTime = mEndLoopTime)) {
        const TempoInfoPoint *pt = PointForTime(time);
        return pt->mTick + ((time - pt->mMs) * 1000.0f / (float)pt->mTempo) * 480.0f;
    } else {
        float loopTickLength = mEndLoopTick - mStartLoopTick;
        float loopTime = time - endTime;
        float loopTimeLength = endTime - mStartLoopTime;
        float loopPercent = std::floor(loopTime / loopTimeLength);

        float recurseTime = loopTime - loopTimeLength * loopPercent + mStartLoopTime;
        float loopTick = loopTickLength * loopPercent + mEndLoopTick;
        // The recursive argument is startTime PLUS the remainder, not minus it:
        // 0x827EE660 is `fnmsubs f11, f0, f29, f31` (= loopTime - loopTimeLength *
        // loopPercent) and 0x827EE66C is `fadds f1, f11, f13`, where f13 is
        // `lfs f13, 0x18(r31)` = mStartLoopTime.  We used to spell it
        // `-(loopTime - loopTimeLength * loopPercent) + mStartLoopTime`, which
        // MSVC lowered to the same fnmsubs followed by `fsubs f1, f12, f11` --
        // the remainder subtracted from startTime instead of added.  That is a
        // real sign inversion in the loop-wrap recursion, not a spelling.
        loopTick += TimeToTick(recurseTime) - mStartLoopTick;
        // RESIDUAL (w7-az, 97.3, 14 rows): the image computes loopTickLength
        // straight out of the two comparison registers -- 0x827EE630 is
        // `fsubs f30, f0, f13`, reusing the f13/f0 that 0x827EE60C and
        // 0x827EE618 loaded for the compares -- and only then loads
        // mStartLoopTime (`lfs f0, 0x18(r3)` at 0x827EE634).  We hoist that
        // load ahead of the block and compute loopTickLength last, which
        // permutes f0/f12/f13 across the compares and f29/f30 across the
        // callee-saved pair.  NEGATIVE: naming startTick/endTick as locals
        // assigned inside the condition (the RB3 spelling) does pin the compare
        // registers, but it makes them live across the recursive call and buys a
        // third callee-saved FPR (`bl __savefpr_26`) -- 83.5.  Swapping the
        // loopTime / loopTimeLength declaration order is byte-inert.
        return loopTick;
    }
}

MultiTempoTempoMap::MultiTempoTempoMap() : mStartLoopTick(-1.0f), mEndLoopTick(-1.0f) {}

void MultiTempoTempoMap::Finalize() { TrimExcess(mTempoPoints); }

bool MultiTempoTempoMap::AddTempoInfoPoint(int tick, int tempo) {
    if (mTempoPoints.size() == 0) {
        if (tick != 0) {
            return false;
        }
    } else if (tick < mTempoPoints.back().mTick) {
        return false;
    }

    MemDoTempAllocations tmp;
    mTempoPoints.push_back(TempoInfoPoint(TickToTime(tick), tick, tempo));
    return true;
}
