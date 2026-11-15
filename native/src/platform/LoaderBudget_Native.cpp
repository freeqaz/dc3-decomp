// The loader's per-poll budget follows the frame period -- see LoaderBudget_Native.h.
#include "LoaderBudget_Native.h"

#include "utl/Loader.h"
#include "os/Debug.h"

#include <chrono>
#include <cmath>

namespace {

// The image's frame: one refresh of a 60 Hz TV.
constexpr double kImageFrameMs = 1000.0 / 60.0;

double sFrameTargetFps = 0.0;

Dc3LoaderBudget &Budget() {
    static Dc3LoaderBudget sBudget;
    return sBudget;
}

using Clock = std::chrono::steady_clock;
Clock::time_point sFrameStart{};
bool sFrameStarted = false;

} // namespace

double Dc3LoaderBudgetMs(double imageMs, double targetFps) {
    // <= 0 is the image's "no slicing" (LoadMgr::Poll does nothing): kept.
    if (!(targetFps > 0.0) || !std::isfinite(targetFps) || !(imageMs > 0.0))
        return imageMs;
    return imageMs * (1000.0 / targetFps) / kImageFrameMs;
}

float Dc3LoaderBudget::Step(float current, double targetFps) {
    if (!mHave || current != mAppliedMs) {
        mImageMs = current;
        mHave = true;
        mSpareMs = 0.0f; // a new image value starts from its floor
    }
    mFloorMs = (float)Dc3LoaderBudgetMs(mImageMs, targetFps);
    float next = mFloorMs;
    if (mSpareMs > 0.0f && mFloorMs < mImageMs)
        next = std::fmin(mFloorMs + mSpareMs, mImageMs);
    // The image only ever sets whole milliseconds (10, 14). A value we apply
    // that is itself whole (5, 7 at 120 fps) is moved by one float step so
    // that the image setting that same number later still reads as the
    // image's: without it, at 84 fps 14 ms scales to exactly 10, and the
    // image's next 10 would look like ours and stay unscaled.
    if (next != mImageMs && next == std::floor(next))
        next = std::nextafter(next, INFINITY);
    mAppliedMs = next;
    return next;
}

void Dc3LoaderBudget::EndFrame(double busyMs, double targetFps, bool loaderBusy) {
    if (!(targetFps > 0.0) || !std::isfinite(targetFps) || !loaderBusy || !(mImageMs > mFloorMs)) {
        // Nothing waiting to load (or nothing to add): the next load starts
        // from the floor, so it can never begin with a frame over the period.
        mSpareMs = 0.0f;
        return;
    }
    const double slack = 1000.0 / targetFps - kSpareMarginMs - busyMs;
    double spare = mSpareMs + slack;
    if (spare < 0.0)
        spare = 0.0;
    if (spare > mImageMs - mFloorMs)
        spare = mImageMs - mFloorMs;
    mSpareMs = (float)spare;
}

void Dc3LoaderBudgetSetFrameTarget(double fps) { sFrameTargetFps = fps > 0.0 ? fps : 0.0; }
double Dc3LoaderBudgetFrameTarget() { return sFrameTargetFps; }

void Dc3LoaderBudgetApply() {
    Dc3LoaderBudget &budget = Budget();
    static bool sLogged = false;
    static float sLoggedFloor = 0.0f, sLoggedImageMs = 0.0f;
    static double sLoggedFps = -1.0;
    sFrameStart = Clock::now();
    sFrameStarted = true;
    // LoadMgr has a setter and no getter; the setter returns what it held.
    const float current = TheLoadMgr.SetLoaderPeriod(0.0f);
    TheLoadMgr.SetLoaderPeriod(current);
    const double fps = sFrameTargetFps;
    const float next = budget.Step(current, fps);
    if (next != current)
        TheLoadMgr.SetLoaderPeriod(next);
    // One line whenever the image's value, the frame target or the floor
    // changes (not for the spare time, which moves every loading frame).
    if (!sLogged || budget.FloorMs() != sLoggedFloor || budget.ImageMs() != sLoggedImageMs
        || fps != sLoggedFps) {
        // What the loader holds now, read back from LoadMgr (not `next`): the
        // line reports the budget LoadMgr::Poll will see.
        const float held = TheLoadMgr.SetLoaderPeriod(next);
        sLogged = true;
        sLoggedFloor = budget.FloorMs();
        sLoggedImageMs = budget.ImageMs();
        sLoggedFps = fps;
        if (fps > 0.0)
            MILO_LOG("DC3 Native: loader budget %.2f ms a poll (the image's %.2f ms, at a %.2f fps "
                     "frame target; up to %.2f ms from the frame's spare time)\n",
                     held, budget.ImageMs(), fps, budget.ImageMs());
        else
            MILO_LOG("DC3 Native: loader budget %.2f ms a poll (the image's; no frame target)\n",
                     held);
    }
}

void Dc3LoaderBudgetEndFrame() {
    if (!sFrameStarted)
        return;
    const double busyMs =
        std::chrono::duration<double, std::milli>(Clock::now() - sFrameStart).count();
    Budget().EndFrame(busyMs, sFrameTargetFps, !TheLoadMgr.Loading().empty());
}
