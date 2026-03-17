#include "hamobj/HamScrollBehavior.h"
#include "HamListRibbon.h"
#include "HamNavProvider.h"
#include "hamobj/HamNavList.h"
#include "gesture/GestureMgr.h"
#include "obj/Data.h"
#include "obj/Msg.h"
#include "obj/Task.h"
#include "os/System.h"
#include "rndobj/Anim.h"
#include "ui/UI.h"
#include "ui/UIListProvider.h"
#include "ui/UIListState.h"

float HamScrollBehavior::sScrollSettleTime = 0.1;

#ifdef HX_NATIVE
float HamScrollBehavior::mNeutralToSlowDownDelay;
float HamScrollBehavior::mSlowDownFirstTickDelay;
float HamScrollBehavior::mSlowDownTickDelay;
float HamScrollBehavior::mFastDownTickDelay;
float HamScrollBehavior::mNeutralToSlowUpDelay;
float HamScrollBehavior::mSlowUpFirstTickDelay;
float HamScrollBehavior::mSlowUpTickDelay;
float HamScrollBehavior::mFastUpTickDelay;
float HamScrollBehavior::mSlowScrollSpeed;
float HamScrollBehavior::mNormalScrollSpeed;
float HamScrollBehavior::mFastScrollSpeedBase;
float HamScrollBehavior::mFastScrollSpeedScalar;
float HamScrollBehavior::mScrollUpCap;
float HamScrollBehavior::mScrollDownCap;
float HamScrollBehavior::mSlowFastThreshold;
#endif

HamScrollBehavior::HamScrollBehavior(HamNavList *nav, UIListState *state)
    : mSettleTimer(0), mInputUp(0), mInputDown(0), mScrollStep(1), mScrollTimeAccum(0), mScrollSpeed(0.3), mTickDelay(0), mScrollCooldown(0),
      mAutoScrollActive(0), mFirstTick(0), mScrollProgress(0), mPendingScrollDir(0), mContinuationDir(0), mLastScrollDir(0), mScrollDir(0),
      mSmoother(0, 10, 0), mSpeedState(2), mListState(state), mNavList(nav) {}

void HamScrollBehavior::Init() {
    static Symbol ui("ui");
    static Symbol scroll_config("scroll_config");
    DataArray *uiCfg = SystemConfig(ui);
    if (uiCfg) {
        DataArray *cfg = uiCfg->FindArray(scroll_config, false);
        if (cfg) {
            static Symbol neutral_to_slow_down_delay("neutral_to_slow_down_delay");
            mNeutralToSlowDownDelay = cfg->FindFloat(neutral_to_slow_down_delay);
            static Symbol slow_down_first_tick_delay("slow_down_first_tick_delay");
            mSlowDownFirstTickDelay = cfg->FindFloat(slow_down_first_tick_delay);
            static Symbol slow_down_tick_delay("slow_down_tick_delay");
            mSlowDownTickDelay = cfg->FindFloat(slow_down_tick_delay);
            static Symbol fast_down_tick_delay("fast_down_tick_delay");
            mFastDownTickDelay = cfg->FindFloat(fast_down_tick_delay);
            static Symbol neutral_to_slow_up_delay("neutral_to_slow_up_delay");
            mNeutralToSlowUpDelay = cfg->FindFloat(neutral_to_slow_up_delay);
            static Symbol slow_up_first_tick_delay("slow_up_first_tick_delay");
            mSlowUpFirstTickDelay = cfg->FindFloat(slow_up_first_tick_delay);
            static Symbol slow_up_tick_delay("slow_up_tick_delay");
            mSlowUpTickDelay = cfg->FindFloat(slow_up_tick_delay);
            static Symbol fast_up_tick_delay("fast_up_tick_delay");
            mFastUpTickDelay = cfg->FindFloat(fast_up_tick_delay);
            static Symbol slow_scroll_speed("slow_scroll_speed");
            mSlowScrollSpeed = cfg->FindFloat(slow_scroll_speed);
            static Symbol normal_scroll_speed("normal_scroll_speed");
            mNormalScrollSpeed = cfg->FindFloat(normal_scroll_speed);
            static Symbol fast_scroll_speed_base("fast_scroll_speed_base");
            mFastScrollSpeedBase = cfg->FindFloat(fast_scroll_speed_base);
            static Symbol fast_scroll_speed_scalar("fast_scroll_speed_scalar");
            mFastScrollSpeedScalar = cfg->FindFloat(fast_scroll_speed_scalar);
            static Symbol scroll_up_cap("scroll_up_cap");
            mScrollUpCap = cfg->FindFloat(scroll_up_cap);
            static Symbol scroll_down_cap("scroll_down_cap");
            mScrollDownCap = cfg->FindFloat(scroll_down_cap);
            static Symbol slow_fast_threshold("slow_fast_threshold");
            mSlowFastThreshold = cfg->FindFloat(slow_fast_threshold);
        }
    }
}

bool HamScrollBehavior::ScrollUp(bool b) {
    if (mScrollCooldown > 0.0f && !b)
        return false;
    int i = mListState->FirstShowing() - mScrollStep;
    if (i < 0)
        return false;
    mListState->Scroll(-1, false);
    mListState->Poll(0.0f);
    mNavList->HandleHighlightChanged(i);
    mPendingScrollDir = 1;
    mSettleTimer = sScrollSettleTime;
    return true;
}

bool HamScrollBehavior::ScrollDown(bool b1) {
    if (mScrollCooldown > 0.0f && !b1)
        return false;
    int i2 = mListState->FirstShowing() + mScrollStep + HamListRibbon::sNumListSelectable - 1;
    if (i2 - mScrollStep >= mListState->NumShowing())
        return false;
    mNavList->HandleHighlightChanged(i2);
    mPendingScrollDir = 2;
    mSettleTimer = sScrollSettleTime;
    return true;
}

bool HamScrollBehavior::IsScrolling() const {
    return mPendingScrollDir != 0 || (mScrollDir == 1 && mListState->FirstShowing() != 0)
        || (mScrollDir == 2 && !AtBottom());
}

bool HamScrollBehavior::AtTop() const { return mListState->FirstShowing() == 0; }

bool HamScrollBehavior::AtBottom() const {
    return mListState->FirstShowing()
        == mListState->NumShowing() - HamListRibbon::sNumListSelectable;
}

void HamScrollBehavior::Enter() {
    mNavList->SetScrollSoundFrame(0);
    mNavList->PlayScrollSound();
}

void HamScrollBehavior::Reset() {
    mScrollDir = 0;
    mLastScrollDir = 0;
    mContinuationDir = 0;
    mSettleTimer = 0.0f;
    mPendingScrollDir = 0;
    mSmoother.Reset();
    mNavList->SetScrollSoundFrame(mSmoother.Level());
    mScrollTimeAccum = 0.0f;
    mInputUp = false;
    mScrollProgress = 0.0f;
    mInputDown = false;
    mScrollCooldown = 0.0f;
    mSpeedState = 2;
}

void HamScrollBehavior::Exit() {
    Reset();
    mNavList->StopScrollSound();
}

void HamScrollBehavior::Update(float input) {
    int scrollDir = mScrollDir;

    // Settle timer - counts down when no pending scroll
    if (mSettleTimer > 0.0f && mPendingScrollDir == 0) {
        if (mInputDown || mInputUp) {
            mSettleTimer = 0.0f;
        }
        float dt = TheTaskMgr.DeltaUISeconds();
        mSettleTimer -= dt;
        if (mSettleTimer <= 0.0f) {
            static Message scrollingSettledMsg("scrolling_settled");
            UIScreen *screen = TheUI->CurrentScreen();
            if (screen) {
                screen->Handle(scrollingSettledMsg, false);
            }
            if (mNavList) {
                int data = mListState->SelectedData();
                mNavList->SendHighlightSettledMsg(data);
            }
        }
    }

    // Direction handling
    if (!(scrollDir == 0)) {
        mLastScrollDir = scrollDir;
        float delay = mNeutralToSlowDownDelay;
        if (scrollDir == 1) {
            delay = mNeutralToSlowUpDelay;
        }
        float dt = TheTaskMgr.DeltaUISeconds();
        mScrollTimeAccum += dt;
        if ((!mAutoScrollActive && mScrollTimeAccum >= delay) || (mAutoScrollActive && mTickDelay <= mScrollTimeAccum)) {
            if (!mAutoScrollActive) {
                mFirstTick = true;
            } else {
                mFirstTick = false;
            }
            mScrollTimeAccum = 0.0f;
            mAutoScrollActive = true;
            if (scrollDir == 1) {
                ScrollUp(false);
            } else if (scrollDir == 2) {
                ScrollDown(false);
            }
        }
    } else {
        mScrollTimeAccum = 0.0f;
        mAutoScrollActive = false;
        mFirstTick = false;
    }

    // Input normalization
    float intensity;
    if (!(input <= 0.5f)) {
        float ratio = (input - 1.0f) / mScrollDownCap;
        float clamped = (-ratio < 0.0f) ? ratio : 0.0f;
        intensity = (clamped - 1.0f < 0.0f) ? clamped : 1.0f;
    } else {
        float ratio = input / mScrollUpCap;
        float clamped = (-1.0f - ratio < 0.0f) ? ratio : -1.0f;
        intensity = (clamped < 0.0f) ? clamped : 0.0f;
    }

    float absIntensity = fabsf(intensity);
    float soundLevel = 0.0f;
    float speed;

    // Speed state machine
    if (TheGestureMgr == NULL || TheGestureMgr->InControllerMode() || !mAutoScrollActive) {
        speed = mNormalScrollSpeed;
        mSpeedState = 2;
        mTickDelay = 0.0f;
    } else if (mScrollTimeAccum > 0.001f) {
        int state = mSpeedState;
        if (state == 0) {
        fast_scroll:
            speed = mFastScrollSpeedScalar * absIntensity + mFastScrollSpeedBase;
            soundLevel = (absIntensity - mSlowFastThreshold) / (1.0f - mSlowFastThreshold);
        } else if (state == 1 || state == 3) {
            speed = mSlowScrollSpeed;
        } else {
            speed = 0.0f;
            if (state == 4) goto fast_scroll;
        }
    } else if (absIntensity < mSlowFastThreshold || mSpeedState == 2) {
        speed = mSlowScrollSpeed;
        if (!mFirstTick) {
            float delay = mSlowDownTickDelay;
            if (scrollDir == 1) {
                delay = mSlowUpTickDelay;
            }
            mTickDelay = delay;
        } else {
            float delay = mSlowDownFirstTickDelay;
            if (scrollDir == 1) {
                delay = mSlowUpFirstTickDelay;
            }
            mTickDelay = delay;
        }
        int state = 1;
        if (scrollDir != 1) {
            state = 3;
        }
        mSpeedState = state;
    } else {
        float delay = mFastDownTickDelay;
        if (scrollDir == 1) {
            delay = mFastUpTickDelay;
        }
        mTickDelay = delay;
        float threshold = mSlowFastThreshold;
        mSpeedState = -(unsigned int)(scrollDir != 1) & 4;
        speed = mFastScrollSpeedScalar * absIntensity + mFastScrollSpeedBase;
        soundLevel = (absIntensity - threshold) / (1.0f - threshold);
    }

    // Sound smoother
    mSmoother.Smooth(soundLevel, TheTaskMgr.DeltaUISeconds());
    mNavList->SetScrollSoundFrame(mSmoother.Level());

    // Scroll speed anim
    RndAnimatable *scrollAnim = mNavList->mScrollSpeedAnim;
    if (scrollAnim) {
        float shift;
        switch (mSpeedState) {
        case 0:
            shift = -1.0f - mSmoother.Level();
            break;
        case 1:
            shift = -1.0f;
            break;
        case 2:
            if (input <= 0.5f) {
                shift = 0.0f;
                auto _tmp4 = mListState->FirstShowing();
                if (_tmp4 != 0) {
                    shift = -mNavList->CalculateSwell(0);
                }
            } else {
                shift = 0.0f;
                if (!AtBottom() && mNavList->mRibbonMode != HamListRibbon::kRibbonDisengaged) {
                    shift = mNavList->CalculateSwell(5);
                }
            }
            break;
        case 3:
            shift = 1.0f;
            break;
        case 4:
            shift = mSmoother.Level() + 1.0f;
            break;
        default:
            goto skip_anim;
        }
        scrollAnim->SetFrame(shift, 1.0f);
    }
skip_anim:

    // Scroll progress
    if (mPendingScrollDir != 0) {
        float dt4 = TheTaskMgr.DeltaUISeconds();
        float progress = mScrollCooldown + dt4 * speed;
        int dir = mPendingScrollDir;
        mScrollCooldown = progress;

        if (progress > 1.0f) {
            mScrollProgress = 0.0f;
            if (dir == 2) {
                int first = mListState->FirstShowing();
                mListState->SetSelected(first + HamListRibbon::sNumListSelectable - 1, first, true);
                mListState->Scroll(1, false);
                mListState->Poll(0.0f);
            }

            bool scrolled = false;
            if (mContinuationDir == mPendingScrollDir) {
                mScrollCooldown -= 1.0f;
                if (mPendingScrollDir == 2) {
                    scrolled = ScrollDown(true);
                } else {
                    scrolled = ScrollUp(true);
                }
            }

            if (!scrolled) {
                mScrollCooldown = 0.0f;
                mPendingScrollDir = 0;
                goto done;
            }
            dir = mPendingScrollDir;
            progress = mScrollCooldown;
        }

        if (dir == 1) {
            progress = 1.0f - progress;
        }
        mScrollProgress = progress;
    }

done:
    mContinuationDir = 0;
}

void HamNavList::PlayScrollSound() {
    if (mListRibbonResource) {
        Sound *scrollSound = mListRibbonResource->ScrollSound();
        if ((int)scrollSound) {
            scrollSound->Play(0, 0, 0, 0, 0);
        }
    }
}

void HamNavList::StopScrollSound() {
    if (mListRibbonResource) {
        Sound *scrollSound = mListRibbonResource->ScrollSound();
        if ((int)scrollSound) {
            scrollSound->Stop(0, false);
        }
    }
}

void HamNavList::SetScrollSoundFrame(float f) {
    if (mListRibbonResource) {
        RndAnimatable *scrollSoundAnim = mListRibbonResource->ScrollSoundAnim();
        if ((int)scrollSoundAnim) {
            scrollSoundAnim->SetFrame(f, 1.0f);
        }
    }
}
