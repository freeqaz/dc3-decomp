// The loader's per-poll budget during gameplay -- see LoaderBudget_Native.h.
#include "LoaderBudget_Native.h"

#include "utl/Loader.h"
#include "ui/UI.h"
#include "ui/UIScreen.h"
#include "os/Debug.h"

#include <cmath>
#include <cstring>

namespace {

// The image's frame: one refresh of a 60 Hz TV.
constexpr double kImageFrameMs = 1000.0 / 60.0;

double sFrameTargetFps = 0.0;

Dc3LoaderBudget &Budget() {
    static Dc3LoaderBudget sBudget;
    return sBudget;
}

} // namespace

double Dc3LoaderBudgetMs(double imageMs, double targetFps) {
    // <= 0 is the image's "no slicing" (LoadMgr::Poll does nothing): kept.
    if (!(targetFps > 0.0) || !std::isfinite(targetFps) || !(imageMs > 0.0))
        return imageMs;
    return imageMs * (1000.0 / targetFps) / kImageFrameMs;
}

float Dc3LoaderBudget::Step(float current, double targetFps, bool gameplayOnScreen) {
    if (!mHave || current != mAppliedMs) {
        mImageMs = current;
        mHave = true;
    }
    float next = gameplayOnScreen ? (float)Dc3LoaderBudgetMs(mImageMs, targetFps) : mImageMs;
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

void Dc3LoaderBudgetSetFrameTarget(double fps) { sFrameTargetFps = fps > 0.0 ? fps : 0.0; }
double Dc3LoaderBudgetFrameTarget() { return sFrameTargetFps; }

bool Dc3GameplayOnScreen() {
    if (!TheUI)
        return false;
    UIScreen *bottom = TheUI->BottomScreen();
    return bottom && bottom->Name() && strcmp(bottom->Name(), "game_screen") == 0;
}

void Dc3LoaderBudgetApply() {
    Dc3LoaderBudget &budget = Budget();
    static bool sLogged = false;
    static float sLoggedApplied = 0.0f, sLoggedImageMs = 0.0f;
    static double sLoggedFps = -1.0;
    // LoadMgr has a setter and no getter; the setter returns what it held.
    const float current = TheLoadMgr.SetLoaderPeriod(0.0f);
    TheLoadMgr.SetLoaderPeriod(current);
    const double fps = sFrameTargetFps;
    const bool gameplay = Dc3GameplayOnScreen();
    const float next = budget.Step(current, fps, gameplay);
    if (next != current)
        TheLoadMgr.SetLoaderPeriod(next);
    if (!sLogged || next != sLoggedApplied || budget.ImageMs() != sLoggedImageMs || fps != sLoggedFps) {
        // What the loader holds now, read back from LoadMgr (not `next`): the
        // line reports the budget LoadMgr::Poll will see.
        const float held = TheLoadMgr.SetLoaderPeriod(next);
        sLogged = true;
        sLoggedApplied = next;
        sLoggedImageMs = budget.ImageMs();
        sLoggedFps = fps;
        if (!(fps > 0.0))
            MILO_LOG("DC3 Native: loader budget %.2f ms a poll (the image's; no frame target)\n", held);
        else if (gameplay)
            MILO_LOG("DC3 Native: loader budget %.2f ms a poll (the image's %.2f ms at a %.2f fps "
                     "frame target; gameplay is on screen)\n",
                     held, budget.ImageMs(), fps);
        else
            MILO_LOG("DC3 Native: loader budget %.2f ms a poll (the image's; %.2f fps frame target, "
                     "gameplay is not on screen)\n",
                     held, fps);
    }
}
