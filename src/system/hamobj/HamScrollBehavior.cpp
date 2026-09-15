#include "hamobj\HamScrollBehavior.h"
#include "HamListRibbon.h"
#include "HamNavProvider.h"
#include "hamobj\HamNavList.h"
#include "gesture\GestureMgr.h"
#include "obj\Data.h"
#include "obj\Msg.h"
#include "obj/Task.h"
#include "os\System.h"
#include "rndobj\Anim.h"
#include "ui\UI.h"
#include "ui\UIListProvider.h"
#include "ui\UIListState.h"

float HamScrollBehavior::sScrollSettleTime = 0.1;

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
        bool settled = mSettleTimer <= 0.0f;
        if (settled) {
            static Message scrollingSettledMsg("scrolling_settled");
            // Not a named local: 8248B3F4 tests the loaded pointer with the SIGNED
            // `cmpwi cr6, r4, 0x0`, in the argument register it was loaded into.
            // A `UIScreen *screen = ...; if (screen)` local gives `cmplwi` instead.
            if (TheUI->CurrentScreen()) {
                TheUI->CurrentScreen()->Handle(scrollingSettledMsg, false);
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
        float delay;
        if (scrollDir == 1) {
            delay = mNeutralToSlowUpDelay;
        } else {
            delay = mNeutralToSlowDownDelay;
        }
        mScrollTimeAccum = TheTaskMgr.DeltaUISeconds() + mScrollTimeAccum;
        if ((!mAutoScrollActive && mScrollTimeAccum >= delay) || (mAutoScrollActive && mScrollTimeAccum >= mTickDelay)) {
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
        // 8248B500 stores 0x1c before 0x1d.  The two bool stores come out 0x1d
        // first whenever the float store precedes them (`= 0.0f` first, and
        // both chained spellings); the bools first and the float LAST is the
        // order that reproduces 8248B500..8248B508 (w7-by).
        mAutoScrollActive = false;
        mFirstTick = false;
        mScrollTimeAccum = 0.0f;
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
    float speed;
    // NOT pre-initialised: the image assigns soundLevel in every arm.  8248B704
    // `fmr f31, f28` is a zero shared by the normal arm, switch case 1/3 and the
    // switch default, and 8248B674 is the slow arm's own copy -- a single
    // `= 0.0f` initialiser here hoists one `fmr` above the whole state machine.
    float soundLevel;

    // Speed state machine
    if (TheGestureMgr == NULL || TheGestureMgr->InControllerMode() || !mAutoScrollActive) {
        speed = mNormalScrollSpeed;
        mSpeedState = 2;
        mTickDelay = 0.0f;
        soundLevel = 0.0f;
    } else if (!(mScrollTimeAccum > 0.001f)) {
        if (absIntensity < mSlowFastThreshold || mSpeedState == 2) {
            speed = mSlowScrollSpeed;
            if (mFirstTick) {
                float delay;
                if (scrollDir == 1) {
                    delay = mSlowUpFirstTickDelay;
                } else {
                    delay = mSlowDownFirstTickDelay;
                }
                mTickDelay = delay;
            } else {
                float delay;
                if (scrollDir == 1) {
                    delay = mSlowUpTickDelay;
                } else {
                    delay = mSlowDownTickDelay;
                }
                mTickDelay = delay;
            }
            soundLevel = 0.0f;
            // Ternary, not `int state = 1; if (..) state = 3;`: 8248B67C puts
            // the `mr r11, r27` (state = 1) AFTER the cmpwi (w7-by).
            mSpeedState = (scrollDir == 1) ? 1 : 3;
        } else {
            speed = mFastScrollSpeedScalar * absIntensity + mFastScrollSpeedBase;
            float delay;
            if (scrollDir == 1) {
                delay = mFastUpTickDelay;
            } else {
                delay = mFastDownTickDelay;
            }
            mTickDelay = delay;
            float threshold = mSlowFastThreshold;
            mSpeedState = (scrollDir == 1) ? 0 : 4;
            soundLevel = (absIntensity - threshold) / (1.0f - threshold);
        }
    } else {
        switch (mSpeedState) {
        case 0:
        fast_scroll:
            speed = mFastScrollSpeedScalar * absIntensity + mFastScrollSpeedBase;
            soundLevel = (absIntensity - mSlowFastThreshold) / (1.0f - mSlowFastThreshold);
            break;
        case 1:
        case 3:
            speed = mSlowScrollSpeed;
            soundLevel = 0.0f;
            break;
        case 4:
            goto fast_scroll;
        default:
            speed = 0.0f;
            soundLevel = 0.0f;
            break;
        }
    }

    // Sound smoother
    mSmoother.Smooth(soundLevel, TheTaskMgr.DeltaUISeconds());
    mNavList->SetScrollSoundFrame(mSmoother.Level());

    // Scroll speed anim
    // w7-by (96.291664 -> 100.0): the w7-ai note below was right about the
    // shape and stopped one step short.  The named `anim` local gives retail's
    // unsigned test on the callee-saved r29 (cmplwi cr6, r29, 0x0 at
    // 0x8248B738; the w7-ai note cited 0x8248B928, a unit-relative offset), and
    // the r28<->r29 swap it seemed to cost only existed while
    // a `float shift` local carried the result to ONE SetFrame call after the
    // switch: with `anim->SetFrame(expr, 1.0f)` written in every arm, MSVC
    // materialises f1/f2 per arm and cross-jumps into the merged tail at
    // three different points (0x8248B7F8 / 0x8248B7FC / 0x8248B804), the
    // swell arms load anim's vtable BEFORE their CalculateSwell call
    // (`lwz r28, 0x0(r29)` at 0x8248B7A8 and 0x8248B7D8), and both registers land where
    // retail has them.
    // NOTE (w7-ai, superseded): Retail loads mScrollSpeedAnim into the
    // callee-saved r29 and tests it UNSIGNED where the ObjPtr expression gave
    // a volatile r11 and a SIGNED cmpwi.  A named `RndAnimatable *anim` alone
    // produced the cmplwi but swapped r28<->r29 against a single call after a
    // `float shift` local: 95.8% with the local vs 96.3% without.
    RndAnimatable *anim = mNavList->mScrollSpeedAnim;
    if (anim) {
        switch (mSpeedState) {
        case 0:
            anim->SetFrame(-1.0f - mSmoother.Level(), 1.0f);
            break;
        case 1:
            anim->SetFrame(-1.0f, 1.0f);
            break;
        case 2:
            if (input > 0.5f) {
                if (!AtBottom() && mNavList->mRibbonMode != HamListRibbon::kRibbonDisengaged) {
                    anim->SetFrame(mNavList->CalculateSwell(5), 1.0f);
                } else {
                    anim->SetFrame(0.0f, 1.0f);
                }
            } else {
                if (mListState->FirstShowing() != 0) {
                    anim->SetFrame(-mNavList->CalculateSwell(0), 1.0f);
                } else {
                    anim->SetFrame(0.0f, 1.0f);
                }
            }
            break;
        case 3:
            anim->SetFrame(1.0f, 1.0f);
            break;
        case 4:
            anim->SetFrame(mSmoother.Level() + 1.0f, 1.0f);
            break;
        }
    }

    // Scroll progress
    if (mPendingScrollDir != 0) {
        float dt4 = TheTaskMgr.DeltaUISeconds();
        float progress = mScrollCooldown + dt4 * speed;
        int dir = mPendingScrollDir;
        mScrollCooldown = progress;

        if (progress > 1.0f) {
            mScrollProgress = 0.0f;
            if (dir == 2) {
                // FirstShowing() read twice, not through an `int first` local:
                // 8248B85C `add r11, r5, r11` adds the value in the r5 argument
                // register; a local puts it right (w7-by).
                mListState->SetSelected(
                    mListState->FirstShowing() + HamListRibbon::sNumListSelectable - 1,
                    mListState->FirstShowing(),
                    true
                );
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

            if (scrolled) {
                dir = mPendingScrollDir;
                progress = mScrollCooldown;
            } else {
                mScrollCooldown = 0.0f;
                mPendingScrollDir = 0;
                goto done;
            }
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
