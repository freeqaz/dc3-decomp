// Native frame cap -- see FramePacer_Native.h.
#include "FramePacer_Native.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <algorithm>
#include <thread>

namespace {

// The image's ceiling: sync 1 = one frame per refresh of a 60 Hz TV.
constexpr double kImageMaxFps = 60.0;

const char *Fmt(const char *fmt, double a, double b = 0.0, double c = 0.0) {
    static char buf[256];
    snprintf(buf, sizeof(buf), fmt, a, b, c);
    return buf;
}

} // namespace

Dc3FrameCap Dc3FrameCapDecide(const char *env, bool windowed, double refreshHz) {
    double requested;
    bool explicitCap = false;
    if (!env || !*env) {
        if (!windowed)
            return {0.0, "off (headless; DC3_FPS_CAP=N to cap)"};
        requested = kImageMaxFps;
    } else if (!strcmp(env, "0") || !strcasecmp(env, "off") || !strcasecmp(env, "no") ||
               !strcasecmp(env, "false")) {
        return {0.0, "off (DC3_FPS_CAP=0)"};
    } else {
        char *end = nullptr;
        requested = strtod(env, &end);
        if (end == env || *end || !(requested > 0.0) || !std::isfinite(requested)) {
            if (!windowed)
                return {0.0, "off (headless; DC3_FPS_CAP is not a number)"};
            requested = kImageMaxFps;
        } else {
            explicitCap = true;
        }
    }

    if (!windowed)
        return {requested, Fmt("%.1f fps (headless, DC3_FPS_CAP)", requested)};
    if (!(refreshHz > 0.0))
        return {requested, Fmt("%.1f fps (display rate unknown)", requested)};

    // Hold every frame for the same whole number of refreshes, choosing the
    // largest rate that is still >= the cap (never slower than asked): 72 on a
    // 144 Hz panel, 82.5 on 165, 60 on 120 and 240. A display at or below the
    // cap (60 Hz, 59.94 Hz, 75 Hz, ...) is left to vsync alone -- a second
    // clock racing the panel's would only add jitter.
    const int interval = (int)std::floor(refreshHz / requested + 0.05);
    if (interval <= 1)
        return {0.0, Fmt("off (the %.2f Hz display is at or under the %.0f fps cap; vsync paces)",
                         refreshHz, requested)};
    const double fps = refreshHz / interval;
    return {fps, Fmt(explicitCap ? "%.2f fps (DC3_FPS_CAP=%.0f on a %.2f Hz display)"
                                 : "%.2f fps (the image's %.0f fps ceiling on a %.2f Hz display; "
                                   "DC3_FPS_CAP=0 for display rate)",
                     fps, requested, refreshHz)};
}

void Dc3FramePacer::SetTarget(double fps) {
    mPeriodS = fps > 0.0 ? 1.0 / fps : 0.0;
    mStarted = false;
}

void Dc3FramePacer::Pace() {
    if (mPeriodS <= 0.0)
        return;
    const auto period =
        std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(mPeriodS));
    const auto now = Clock::now();
    if (!mStarted) {
        mStarted = true;
        mNext = now;
    }
    mNext += period;
    if (mNext > now) {
        std::this_thread::sleep_until(mNext);
        mSleptS += std::chrono::duration<double>(Clock::now() - now).count();
        mPaced++;
    } else if (now - mNext > period * 4) {
        // A long frame (a load, a hitch) does not buy a burst of unpaced
        // frames afterwards.
        mNext = now;
    }
}

void Dc3RafGate::Decide() {
    double sorted[kWindow];
    std::copy(mGaps, mGaps + mGapCount, sorted);
    // The one-refresh cluster, not the median of all gaps: see the header.
    // Its edge is the 25th percentile; its centre is the median of the gaps
    // within 1.5x of that edge (one refresh, jitter included; a gap holding a
    // frame's cost is two refreshes or more and falls outside).
    const int k = mGapCount / 4;
    std::nth_element(sorted, sorted + k, sorted + mGapCount);
    const double edgeMs = sorted[k];
    int n = 0;
    for (int i = 0; i < mGapCount; i++)
        if (sorted[i] <= edgeMs * 1.5)
            sorted[n++] = sorted[i];
    std::nth_element(sorted, sorted + n / 2, sorted + n);
    const double refreshMs = sorted[n / 2];
    const double hz = refreshMs > 0.0 ? 1000.0 / refreshMs : 0.0;
    const Dc3FrameCap cap = Dc3FrameCapDecide(mEnv.empty() ? nullptr : mEnv.c_str(), true, hz);
    // A re-estimate within 5% of the cap in force is not a new decision: the
    // estimate carries timestamp noise (72.0000 vs 71.9999 fps), and counting
    // those as changes logged the same "frame cap 72.00 fps" line again and
    // again and hid the one real flip among them.
    const bool changed = mDecisions == 0 || (cap.fps > 0.0) != (mCap.fps > 0.0) ||
                         std::fabs(cap.fps - mCap.fps) > 0.05 * mCap.fps;
    mRefreshHz = hz;
    if (changed) {
        snprintf(mWhy, sizeof(mWhy), "%s", cap.why); // cap.why is a static buffer
        mCap = {cap.fps, mWhy};
        mDecisions++;
    }
}

bool Dc3RafGate::OnRaf(double nowMs) {
    if (mLastRafMs >= 0.0) {
        const double gap = nowMs - mLastRafMs;
        if (gap > 0.0 && gap < 1000.0) { // a hidden tab's pause is not a refresh
            mGaps[mGapNext] = gap;
            mGapNext = (mGapNext + 1) % kWindow;
            if (mGapCount < kWindow)
                mGapCount++;
            if (++mSinceDecide >= (mDecisions == 0 ? 16 : kWindow)) {
                mSinceDecide = 0;
                Decide();
            }
        }
    }
    mLastRafMs = nowMs;

    bool run = true;
    const bool capped = mCap.fps > 0.0 && mRefreshHz > 0.0;
    const double halfRefreshMs = capped ? 500.0 / mRefreshHz : 0.0;
    if (capped && mDueMs >= 0.0)
        run = nowMs >= mDueMs - halfRefreshMs;
    if (run) {
        // On time (within half a refresh): the next frame is due one period
        // after this rAF, so the gate follows the panel and an estimate a
        // fraction off (143.6 vs 144 Hz) never accumulates into a drop. Late
        // (a frame ran past its refresh): one period after it was DUE, so
        // the lost refresh is made up on the next one -- at most one period
        // of debt, so a long stall buys one quick frame, not a burst.
        if (capped) {
            const double periodMs = 1000.0 / mCap.fps;
            if (mDueMs < 0.0 || nowMs - mDueMs <= halfRefreshMs)
                mDueMs = nowMs + periodMs;
            else
                mDueMs = std::max(mDueMs + periodMs, nowMs);
        } else {
            mDueMs = -1.0;
        }
        mLastRunMs = nowMs;
        mRan++;
    } else {
        mSkipped++;
    }
    return run;
}
