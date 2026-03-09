#include "flow/FlowPickOne.h"
#include "flow/FlowNode.h"
#include "flow/DrivenPropertyEntry.h"
#include "math/Rand.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include <algorithm>
#include <vector>

FlowPickOne::FlowPickOne()
    : mChoiceHistory(this), mChoiceType(kChoiceRandom), mIndex(0), mChance(1) {}
FlowPickOne::~FlowPickOne() {}

BEGIN_HANDLERS(FlowPickOne)
    HANDLE_SUPERCLASS(FlowNode)
END_HANDLERS

BEGIN_PROPSYNCS(FlowPickOne)
    SYNC_PROP_MODIFY(choice_type, (int &)mChoiceType, OnChoiceTypeChanged())
    SYNC_PROP(index, mIndex)
    SYNC_PROP(chance, mChance)
    SYNC_SUPERCLASS(FlowNode)
END_PROPSYNCS

BEGIN_SAVES(FlowPickOne)
    SAVE_REVS(1, 0)
    SAVE_SUPERCLASS(FlowNode)
    bs << mChoiceType;
    bs << mChance;
END_SAVES

BEGIN_COPYS(FlowPickOne)
    COPY_SUPERCLASS(FlowNode)
    CREATE_COPY(FlowPickOne)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mChoiceType)
        COPY_MEMBER(mChance)
    END_COPYING_MEMBERS
END_COPYS

bool FlowPickOne::Activate() {
    FLOW_LOG("Activate\n");
    mStopRequested = false;
    PushDrivenProperties();

    if (mChance != 1.0f) {
        unsigned int r = rand() % 100;
        if (mChance * 100.0f < (float)(int)r) {
            return false;
        }
    }

    int numChildren = (int)mChildNodes.size();
    if (mChildNodes.empty())
        return false;

    FlowNode *chosen = nullptr;

    switch (mChoiceType) {
    case kChoiceOrdered:
        if (mIndex < 0 || numChildren <= mIndex)
            mIndex = 0;
        chosen = (mChildNodes.begin() + mIndex)->Obj();
        ActivateChild(chosen);
        mIndex++;
        return !mRunningNodes.empty();
    case kChoiceRandom:
        mIndex = RandomInt(0, numChildren);
        chosen = (mChildNodes.begin() + mIndex)->Obj();
        break;
    case kChoiceRandomNoRepeat:
        if (numChildren < 2) {
            mIndex = 0;
        } else {
            int newIndex;
            do {
                newIndex = RandomInt(0, numChildren);
            } while (newIndex == mIndex);
            mIndex = newIndex;
        }
        chosen = (mChildNodes.begin() + mIndex)->Obj();
        break;
    case kChoiceRandomJukeBox: {
        if (numChildren <= 1) {
            if (numChildren == 1)
                chosen = mChildNodes.begin()->Obj();
            break;
        }
        int historySize = (int)mChoiceHistory.size();
        if (mIndex < 0 || historySize <= mIndex) {
            FlowNode *lastChosen = nullptr;
            if (!mChoiceHistory.empty()) {
                lastChosen = (mChoiceHistory.begin() + (historySize - 1))->Obj();
            }
            mChoiceHistory.clear();
            std::vector<FlowNode *> items;
            FOREACH (it, mChildNodes) {
                items.push_back(it->Obj());
            }
            std::random_shuffle(items.begin(), items.end());
            for (auto rit = items.end(); rit != items.begin();) {
                --rit;
                mChoiceHistory.push_back(*rit);
            }
            mIndex = 0;
            if (lastChosen && mChoiceHistory.begin()->Obj() == lastChosen) {
                mIndex = 1;
            }
            historySize = (int)mChoiceHistory.size();
        }
        if (mIndex < historySize) {
            chosen = (mChoiceHistory.begin() + mIndex)->Obj();
            mIndex++;
        }
        break;
    }
    case kChoiceUseIndex: {
        int adjustedIndex = mIndex % numChildren;
        mIndex = adjustedIndex;
        chosen = (mChildNodes.begin() + adjustedIndex)->Obj();
        ActivateChild(chosen);
        mIndex++;
        return !mRunningNodes.empty();
    }
    default:
        MILO_NOTIFY_ONCE("Bad ChoiceType in FlowPickOne!");
        break;
    }

    if (chosen) {
        ActivateChild(chosen);
    }
    return !mRunningNodes.empty();
}

void FlowPickOne::OnChoiceTypeChanged() {
    if (mChoiceType != kChoiceUseIndex) {
        FOREACH (it, mDrivenPropEntries) {
            DataArray *arr = it->Node().Array();
            if (arr->Sym(0) == "index") {
                mDrivenPropEntries.erase(it);
                return;
            }
        }
    }
}

INIT_REVS(1, 0)

BEGIN_LOADS(FlowPickOne)
    LOAD_REVS(bs)
    ASSERT_REVS(1, 0)
    LOAD_SUPERCLASS(FlowNode)
    d >> (int &)mChoiceType;
    if (d.rev > 0) {
        d >> mChance;
    }
END_LOADS
