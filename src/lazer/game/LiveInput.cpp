#include "game/LiveInput.h"
#include "game/GamePanel.h"
#include "hamobj/HamAudio.h"
#include "meta_ham/ProfileMgr.h"
#include "obj/Task.h"
#ifdef HX_NATIVE
#include <cstdlib>
#endif

LiveInput::LiveInput(HamAudio &audio) : mAudio(audio), mTimeOffset(0)
#ifdef HX_NATIVE
    , mFastTimeMs(0.0f)
#endif
{
    mTimer.Restart();
}

#ifdef HX_NATIVE
static bool sFastTime = (getenv("DC3_FAST_TIME") != nullptr);
#endif

float LiveInput::CurrentMs(bool b1) const {
    const_cast<LiveInput *>(this)->mTimer.Split();
    float toAdd;
#ifdef HX_NATIVE
    // DC3_FAST_TIME: advance song time by a fixed step per frame instead
    // of wall-clock or audio time.  Makes headless gameplay finish in seconds.
    // Must override both b1=true (wall-clock) and b1=false (audio) paths,
    // because mRealTime=false during normal gameplay when audio succeeds.
    if (sFastTime) {
        mFastTimeMs += 1000.0f / 120.0f;
        toAdd = mFastTimeMs + mTimeOffset;
    } else
#endif
    if (b1) {
        toAdd = const_cast<LiveInput *>(this)->mTimer.Ms() + mTimeOffset;
    } else {
        toAdd = mAudio.GetTime();
    }
    return TheGamePanel->DeJitter(GetSongToTaskMgrMs() + toAdd);
}

float LiveInput::GetSongToTaskMgrMs() const {
    return TheProfileMgr.GetSongToTaskMgrMs(kGame);
}

void LiveInput::SetPaused(bool b1) {
    if (b1) {
        mAudio.SetPaused(true);
    } else if (!TheGamePanel->IsGameOver() && mAudio.IsReady()) {
        mAudio.SetPaused(false);
    }
}

void LiveInput::SetTimeOffset() {
    float f1 = TheTaskMgr.Seconds(TaskMgr::kRealTime) * 1000.0f;
    f1 = f1 - mTimer.SplitMs();
    mTimeOffset = f1 - TheProfileMgr.GetSongToTaskMgrMs(kGame);
#ifdef HX_NATIVE
    if (sFastTime) mFastTimeMs = 0.0f;
#endif
}

void LiveInput::SetPostWaitJumpOffset(float f1) {
    mTimer.Restart();
    mTimeOffset = f1 - mTimer.Ms();
#ifdef HX_NATIVE
    if (sFastTime) mFastTimeMs = 0.0f;
#endif
}
