#include "hamobj/HamScrollBehavior.h"
#include "HamListRibbon.h"
#include "HamNavProvider.h"
#include "hamobj/HamNavList.h"
#include "obj/Data.h"
#include "os/System.h"
#include "rndobj/Anim.h"
#include "ui/UIListProvider.h"
#include "ui/UIListState.h"

float HamScrollBehavior::sScrollSettleTime = 0.1;

HamScrollBehavior::HamScrollBehavior(HamNavList *nav, UIListState *state)
    : mSettleTimer(0), unk4(0), unk5(0), mScrollStep(1), unkc(0), mScrollSpeed(0.3), unk14(0), mScrollCooldown(0),
      unk1c(0), unk1d(0), unk20(0), mPendingScrollDir(0), unk28(0), unk2c(0), mScrollDir(0),
      mSmoother(0, 10, 0), unk48(2), mListState(state), mNavList(nav) {}

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
    unk2c = 0;
    unk28 = 0;
    mSettleTimer = 0.0f;
    mPendingScrollDir = 0;
    mSmoother.Reset();
    mNavList->SetScrollSoundFrame(mSmoother.Level());
    unkc = 0.0f;
    unk4 = false;
    unk20 = 0.0f;
    unk5 = false;
    mScrollCooldown = 0.0f;
    unk48 = 2;
}

void HamScrollBehavior::Exit() {
    Reset();
    mNavList->StopScrollSound();
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
