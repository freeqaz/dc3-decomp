#pragma once
#include "math/DoubleExponentialSmoother.h"
#include "ui/UIListState.h"

class HamNavList;

class HamScrollBehavior {
public:
    HamScrollBehavior(HamNavList *, UIListState *);
    bool ScrollUp(bool);
    bool ScrollDown(bool);
    bool IsScrolling() const;
    bool AtTop() const;
    bool AtBottom() const;
    void Enter();
    void Reset();
    void Exit();
    void Update(float);
    void PlayScrollSound();

    float GetFirstVal() { return mSettleTimer; }

    static void Init();
    static float mNeutralToSlowDownDelay;
    static float mSlowDownFirstTickDelay;
    static float mSlowDownTickDelay;
    static float mFastDownTickDelay;
    static float mNeutralToSlowUpDelay;
    static float mSlowUpFirstTickDelay;
    static float mSlowUpTickDelay;
    static float mFastUpTickDelay;
    static float mSlowScrollSpeed;
    static float mNormalScrollSpeed;
    static float mFastScrollSpeedBase;
    static float mFastScrollSpeedScalar;
    static float mScrollUpCap;
    static float mScrollDownCap;
    static float mSlowFastThreshold;

    friend class HamNavList;

private:
    static float sScrollSettleTime;

    float mSettleTimer;
    bool unk4;
    bool unk5;
    int mScrollStep;
    float unkc;
    float mScrollSpeed;
    float unk14;
    float mScrollCooldown;
    bool unk1c;
    bool unk1d;
    float unk20;
    int mPendingScrollDir;
    int unk28;
    int unk2c;
    int mScrollDir;
    DoubleExponentialSmoother mSmoother;
    int unk48;
    UIListState *mListState;
    HamNavList *mNavList;
};
