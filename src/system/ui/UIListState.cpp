#include "ui/UIListState.h"
#include "os/Debug.h"
#include "rndobj/Dir.h"
#include "ui/UIListProvider.h"
#include "utl/Loader.h"
#include "utl/Std.h"

UIListState::UIListState(UIListProvider *provider, UIListStateCallback *callback)
    : mCircular(0), mNumDisplay(5), mGridSpan(1), mSpeed(0.25f), mMinDisplay(0),
      mScrollPastMinDisplay(0), mMaxDisplay(-1), mScrollPastMaxDisplay(1),
      mProvider(provider), mFirstShowing(0), mTargetShowing(0), mSelectedDisplay(0),
      mStepPercent(0.0f), mStepTime(-1.0f), mCallback(callback) {}

int UIListState::SelectedDisplay() const {
    if (mCircular) {
        return mMinDisplay;
    } else {
        return mSelectedDisplay;
    }
}

void UIListState::SetNumDisplay(int num, bool b) {
    MILO_ASSERT(num > 0, 0x139);
    mNumDisplay = num;
    if (b) {
        SetSelected(0, -1, true);
    }
}

void UIListState::SetGridSpan(int span, bool b) {
    MILO_ASSERT(span > 0, 0x141);
    mGridSpan = span;
    if (b) {
        SetSelected(0, -1, true);
    }
}

void UIListState::SetSelected(int i1, int i2, bool b) {
    int i7 = WrapShowing(i1);
    if (b) {
        int i;
        for (i = i7; !mProvider->IsActive(Showing2Data(i));) {
            i++;
            if (Showing2Data(i) == Showing2Data(i7))
                break;
        }
        i7 = WrapShowing(i);
    }
    if (mCircular) {
        mFirstShowing = WrapShowing(i7 - mMinDisplay);
    } else {
        if (i2 != -1) {
            mFirstShowing = i2;
        } else {
            if (!mScrollPastMinDisplay) {
                i7 -= mMinDisplay;
            }
            mFirstShowing = Max(0, i7);
        }
        mFirstShowing = Max(mFirstShowing, MaxFirstShowing());
        mSelectedDisplay = i7 - mFirstShowing;
        if (mScrollPastMinDisplay) {
            mSelectedDisplay = mMinDisplay + i7;
        }
    }
    mTargetShowing = mFirstShowing;
    mStepTime = -1;
    mStepPercent = 0;
}

void UIListState::SetSpeed(float speed) {
    MILO_ASSERT(speed >= 0, 0x15f);
    mSpeed = speed;
}

float UIListState::Speed() const { return mSpeed; }

void UIListState::SetMinDisplay(int min) {
    MILO_ASSERT(min >= 0, 0x149);
    mMinDisplay = min;
    mSelectedDisplay = Max(min, mSelectedDisplay);
}

void UIListState::SetMaxDisplay(int max) {
    MILO_ASSERT(max >= -1, 0x150);
    if (TheLoadMgr.EditMode()) {
        max = Clamp(-1, mNumDisplay - 1, max);
    }
    mMaxDisplay = max;
}

void UIListState::SetScrollPastMinDisplay(bool scroll) {
    mScrollPastMinDisplay = scroll;
    if (mScrollPastMinDisplay) {
        mSelectedDisplay = Max(mSelectedDisplay, mMinDisplay);
    }
}

void UIListState::SetScrollPastMaxDisplay(bool) {}

int UIListState::Selected() const { return Display2Showing(SelectedDisplay()); }

int UIListState::SelectedData() const { return Display2Data(SelectedDisplay()); }

int UIListState::CurrentScroll() const { return ScrollToTarget(mTargetShowing); }

int UIListState::WrapShowing(int i) const {
    if (NumShowing() == 0)
        return 0;
    return Mod(i, NumShowing());
}

int UIListState::Display2Showing(int i) const {
    int offset = mFirstShowing + i;
    if (mScrollPastMinDisplay && !mCircular) {
        offset = offset - mMinDisplay;
        if (offset < 0 || offset >= NumShowing())
            return -1;
    }
    return WrapShowing(offset);
}

int UIListState::Showing2Data(int i) const {
    int count = WrapShowing(i);
    FOREACH (it, mHiddenData) {
        if (*it <= count)
            count++;
    }
    return count;
}

int UIListState::NumDisplayWithData() const {
    int ret = mNumDisplay;
    if (!mCircular) {
        int num = mProvider->NumData();
        if (mScrollPastMinDisplay)
            num += mMinDisplay;
        ret = Min(ret, num);
    }
    return ret;
}

int UIListState::MaxFirstShowing() const {
    MILO_ASSERT(!mCircular, 0xE8);
    int curshowing = NumShowing();
    int maxshowing = Max(0, curshowing - mNumDisplay);
    if (mMaxDisplay != -1 && mScrollPastMaxDisplay) {
        maxshowing +=
            (Min(curshowing, mNumDisplay) - Clamp(0, mNumDisplay, mMaxDisplay)) - 1;
    }
    if (mScrollPastMinDisplay) {
        maxshowing += mMinDisplay;
    }
    return Max(0, maxshowing);
}

int UIListState::ScrollMaxDisplay() const {
    MILO_ASSERT(!mCircular, 0xF7);
    int max = Max(0, Min(NumShowing() - 1, mNumDisplay - 1));
    if (mMaxDisplay != -1) {
        max = Clamp(0, max, mMaxDisplay);
    }
    return max;
}

int UIListState::SelectedNoWrap() const {
    int i1 = mFirstShowing + SelectedDisplay();
    if (mScrollPastMinDisplay && !mCircular) {
        i1 -= mMinDisplay;
        if (i1 < 0 || i1 >= NumShowing())
            return -1;
    }
    return i1;
}

int UIListState::Display2Data(int i) const {
    int disp = Display2Showing(i);
    if (disp == -1)
        return -1;
    else
        return Showing2Data(disp);
}

int UIListState::SnappedDataForDisplay(int i2) const {
    bool b1 = (!IsScrolling() && i2 == 0) || (mTargetShowing > mFirstShowing && i2 == 0)
        || (mTargetShowing < mFirstShowing && i2 == -1);
    if (b1) {
        int data = Display2Data(i2);
        return Provider()->SnappableAtOrBeforeData(data);
    } else
        return -1;
}

void UIListState::SetCircular(bool c) { mCircular = c; }

void UIListState::SetCircular(bool c, bool b) {
    mCircular = c;
    if (b) {
        SetSelected(0, -1, true);
    }
}

void UIListState::Poll(float f1) {
    if (IsScrolling()) {
        if (mStepTime == -1) {
            mStepTime = f1;
            mCallback->StartScroll(*this, CurrentScroll() > 0 ? 1 : -1, true);
        }
        if (f1 >= mSpeed + mStepTime) {
            int add = CurrentScroll() > 0 ? 1 : -1;
            mFirstShowing = WrapShowing(mFirstShowing + add);
            mCallback->CompleteScroll(*this);
            if (IsScrolling()) {
                mStepTime = (f1 - (f1 - (mStepTime + mSpeed)));
                mCallback->StartScroll(*this, CurrentScroll() > 0 ? 1 : -1, true);
            } else {
                mStepTime = -1;
            }
        }
        if (mFirstShowing == mTargetShowing || mSpeed == 0) {
            mStepPercent = 0;
            if (mSpeed == 0) {
                while (IsScrolling()) {
                    Poll(f1);
                }
            }
        } else {
            mStepPercent = (f1 - mStepTime) / mSpeed;
        }
    } else {
        mStepTime = -1;
        mStepPercent = 0;
    }
}

bool UIListState::CanScrollBack(bool b) const {
    if (mCircular)
        return true;
    int count = b ? Display2Data(mSelectedDisplay) : Showing2Data(mFirstShowing);
    for (count = count - 1; count >= 0; count--) {
        if (mProvider->IsActive(count))
            return true;
    }
    return false;
}

bool UIListState::CanScrollNext(bool b) const {
    if (mCircular)
        return true;
    else if (b) {
        for (int data = Display2Data(mSelectedDisplay) + 1; data < mProvider->NumData();
             data++) {
            if (mProvider->IsActive(data))
                return true;
        }
    } else {
        return MaxFirstShowing() > mFirstShowing;
    }
    return false;
}

bool UIListState::ShouldHoldDisplayInPlace(int i2) const {
    bool shouldCheck = (mTargetShowing > mFirstShowing && i2 == 0)
        || (mTargetShowing < mFirstShowing && i2 == -1);
    if (shouldCheck) {
        if (SnappedDataForDisplay(i2) >= 0) {
            int nextDisp = i2 + 1;
            if (nextDisp != mNumDisplay && Display2Data(nextDisp) != -1) {
                if (!Provider()->IsSnappableAtData(Display2Data(nextDisp))) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool UIListState::BuildScroll(int direction, int firstShowing, int selectedDisplay, ScrollState &state) const {
    state.mFirstShowing = firstShowing;
    state.mSelectedDisplay = selectedDisplay;

    if (mFirstShowing != firstShowing) {
        int dirSign = direction > 0 ? 1 : -1;
        int scrollSign = ScrollToTarget(firstShowing) > 0 ? 1 : -1;
        if (dirSign != scrollSign)
            return false;
    }

    if (mCircular) {
        int newFirst = WrapShowing(state.mFirstShowing + direction);
        int curScroll = ScrollToTarget(state.mFirstShowing);
        if (curScroll != 0) {
            int curSign = curScroll > 0 ? 1 : -1;
            int newSign = ScrollToTarget(newFirst) > 0 ? 1 : -1;
            if (curSign != newSign)
                return false;
        }
        state.mFirstShowing = newFirst;
    } else {
        state.mSelectedDisplay += direction;
        int scrollMax = ScrollMaxDisplay();
        if (mScrollPastMinDisplay && mMinDisplay >= scrollMax) {
            scrollMax = mMinDisplay;
        }

        if (state.mSelectedDisplay < 0) {
            state.mFirstShowing += state.mSelectedDisplay;
            int sel = mMinDisplay;
            if (mMinDisplay >= firstShowing) {
                sel = firstShowing;
            }
            state.mSelectedDisplay = sel;
        } else if (state.mSelectedDisplay > scrollMax) {
            state.mFirstShowing += (state.mSelectedDisplay - scrollMax);
            state.mSelectedDisplay = scrollMax;
        } else if (!mScrollPastMinDisplay || state.mSelectedDisplay >= mMinDisplay) {
            int origFirst = state.mFirstShowing;
            if (state.mSelectedDisplay < mMinDisplay) {
                state.mFirstShowing = Max(0, origFirst - 1);
            }
            state.mSelectedDisplay = (state.mSelectedDisplay - state.mFirstShowing) + origFirst;
            return origFirst != state.mFirstShowing;
        } else {
            state.mFirstShowing += (state.mSelectedDisplay - mMinDisplay);
            state.mSelectedDisplay = mMinDisplay;
        }

        int result;
        if (state.mSelectedDisplay > scrollMax) {
            result = scrollMax;
        } else {
            if (state.mSelectedDisplay < 0) {
                result = 0;
            } else {
                result = state.mSelectedDisplay;
            }
        }
        state.mSelectedDisplay = result;

        int maxFirst = MaxFirstShowing();
        if (state.mFirstShowing <= maxFirst) {
            if (state.mFirstShowing < 0) {
                maxFirst = 0;
            } else {
                maxFirst = state.mFirstShowing;
            }
        }
        state.mFirstShowing = maxFirst;
    }

    return (state.mSelectedDisplay == selectedDisplay) || (state.mFirstShowing != firstShowing);
}

void UIListState::Scroll(int direction, bool skipActive) {
    if (mFirstShowing != mTargetShowing)
        return;

    ScrollState state;
    bool changed = BuildScroll(direction, mTargetShowing, mSelectedDisplay, state);

    if (mCircular) {
        int curFirst = state.mFirstShowing;
        int curSel = state.mSelectedDisplay;
        if (!skipActive) {
            do {
                curSel = state.mSelectedDisplay;
                curFirst = state.mFirstShowing;
                int sel = curSel;
                if (mCircular)
                    sel = mMinDisplay;
                if (mScrollPastMinDisplay)
                    sel -= mMinDisplay;
                int data = Showing2Data(sel + curFirst);
                if (mProvider->IsActive(data))
                    goto accept_circ;
                if (mTargetShowing == curFirst)
                    return;
                int step = direction > 0 ? 1 : -1;
                BuildScroll(step, curFirst, curSel, state);
            } while (curFirst != state.mFirstShowing);
        }
        accept_circ:
        mTargetShowing = curFirst;
        MILO_ASSERT(curSel == mSelectedDisplay, 0x1d6);
    } else {
        bool hitBoundary = false;
        int curFirst = state.mFirstShowing;
        int curSel = state.mSelectedDisplay;
        if (!skipActive) {
            while (true) {
                int sel = curSel;
                if (mCircular)
                    sel = mMinDisplay;
                if (mScrollPastMinDisplay)
                    sel -= mMinDisplay;
                int data = Showing2Data(sel + curFirst);
                if (mProvider->IsActive(data))
                    break;
                if (hitBoundary)
                    return;

                int step = 1;
                if (direction <= 0)
                    step = -1;
                changed = BuildScroll(step, curFirst, curSel, state);

                if (step == 1) {
                    if (state.mFirstShowing == MaxFirstShowing()) {
                        hitBoundary = (state.mSelectedDisplay == ScrollMaxDisplay());
                        if (hitBoundary)
                            goto retry;
                    }
                } else {
                    bool atZero = state.mFirstShowing == 0;
                    curFirst = state.mFirstShowing;
                    curSel = state.mSelectedDisplay;
                    if (mScrollPastMinDisplay) {
                        if (atZero) {
                            hitBoundary = (curSel == mMinDisplay);
                            if (hitBoundary)
                                continue;
                        }
                    } else {
                        if (atZero && curSel == 0) {
                            hitBoundary = true;
                            continue;
                        }
                    }
                }
                hitBoundary = false;
                retry:
                curFirst = state.mFirstShowing;
                curSel = state.mSelectedDisplay;
            }
        }
        mTargetShowing = curFirst;
        mSelectedDisplay = curSel;
        if (!skipActive && !changed) {
            int dir = 1;
            if (direction <= 0)
                dir = -1;
            mCallback->StartScroll(*this, dir, false);
            mCallback->CompleteScroll(*this);
        }
    }
}

void UIListState::PageScroll(int amount) {
    int direction;
    if (amount > 0) {
        direction = 1;
    } else {
        direction = -1;
    }

    if (mCircular) {
        direction *= mNumDisplay;
    } else if (direction > 0) {
        int numDisplay = mNumDisplay;
        int selectedDisplay = mSelectedDisplay;
        if ((selectedDisplay == numDisplay - 1) || (selectedDisplay == mMaxDisplay)) {
            direction = numDisplay - mMinDisplay;
        } else {
            direction = ((numDisplay - mMinDisplay) - selectedDisplay) - 1;
        }
    } else if (direction < 0) {
        int selectedDisplay = mSelectedDisplay;
        int minDisplay = mMinDisplay;
        if (selectedDisplay == minDisplay) {
            direction = minDisplay - mNumDisplay;
        } else {
            int diff = minDisplay - selectedDisplay;
            direction = (diff >> 0x1F) & diff;
        }
    }

    Scroll(direction, false);
}

void UIListState::SetSelectedSimulateScroll(int i) {
    int showing = WrapShowing(i);
    mFirstShowing = mTargetShowing;
    mStepTime = -1.0f;
    mStepPercent = 0.0f;
    int diff = showing - SelectedNoWrap();
    if (diff != 0) {
        if ((diff > 0 ? diff : -diff) > mNumDisplay * 2) {
            int dir = diff > 0 ? 1 : -1;
            SetSelected(showing - mNumDisplay * dir * 2, -1, true);
        }
        while (SelectedNoWrap() != showing) {
            int nowrap = SelectedNoWrap();
            int dir = nowrap - showing > 0 ? 1 : -1;
            Scroll(dir, true);
            mStepTime = -1.0f;
            mStepPercent = 0.0f;
            mFirstShowing = mTargetShowing;
        }
        MILO_ASSERT(showing == SelectedNoWrap(), 0x1BC);
        mCallback->CompleteScroll(*this);
    }
}

int UIListState::MinDisplay() const { return 1; }

int UIListState::MaxDisplay() const { return 1; }

bool UIListState::ScrollPastMinDisplay() const { return mScrollPastMinDisplay; }

bool UIListState::ScrollPastMaxDisplay() const { return mScrollPastMaxDisplay; }

bool UIListState::IsScrolling() const { return mFirstShowing != mTargetShowing; }

int UIListState::NumShowing() const { return mProvider->NumData() - mHiddenData.size(); }

UIListProvider *UIListState::Provider() { return mProvider; }

UIListProvider *UIListState::Provider() const { return mProvider; }

void UIListState::SetProvider(UIListProvider *provider, RndDir *rdir) {
    MILO_ASSERT(provider, 0x126);
    provider->InitData(rdir);
    mProvider = provider;
    mHiddenData.clear();
    for (int i = 0; i < mProvider->NumData(); i++) {
        if (mProvider->IsHidden(i)) {
            mHiddenData.push_back(i);
        }
    }
    SetSelected(0, -1, true);
}

int UIListState::ScrollToTarget(int i1) const {
    int showing = i1 - mFirstShowing;
    if (mCircular) {
        int u2;
        if (showing > 0) {
            u2 = showing - NumShowing();
        } else {
            u2 = showing + NumShowing();
        }
        int i3 = abs(u2);
        int ivar1 = abs(showing);
        if (i3 < ivar1) {
            return u2;
        }
        if (i3 == ivar1) {
            return 1;
        }
    }
    return showing;
}
