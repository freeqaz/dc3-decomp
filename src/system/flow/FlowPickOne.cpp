#include "flow/FlowPickOne.h"
#include "flow/FlowNode.h"
#include "flow/DrivenPropertyEntry.h"
#include "math/Rand.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/Debug.h"

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

// Helper: get Nth element from ObjPtrVec by index
static FlowNode *GetNthChild(ObjPtrVec<FlowNode> &vec, int n) {
    auto it = vec.begin();
    for (int i = 0; i < n && it != vec.end(); i++)
        ++it;
    return (it != vec.end()) ? it->Obj() : nullptr;
}

bool FlowPickOne::Activate() {
    FLOW_LOG("Activate\n");
    mStopRequested = false;
    PushDrivenProperties();

    // Chance check
    if (mChance != 1.0f) {
        if (mChance * 100.0f < (float)(rand() % 100)) {
            return false;
        }
    }

    int numChildren = mChildNodes.size();
    if (numChildren == 0)
        return false;

    FlowNode *chosen = nullptr;

    switch (mChoiceType) {
    case kChoiceOrdered:
        if (mIndex < 0 || mIndex >= numChildren)
            mIndex = 0;
        chosen = GetNthChild(mChildNodes, mIndex);
        mIndex++;
        break;
    case kChoiceRandom:
        mIndex = RandomInt(0, numChildren);
        chosen = GetNthChild(mChildNodes, mIndex);
        break;
    case kChoiceRandomNoRepeat:
        if (numChildren <= 1) {
            mIndex = 0;
        } else {
            int newIndex;
            do {
                newIndex = RandomInt(0, numChildren);
            } while (newIndex == mIndex);
            mIndex = newIndex;
        }
        chosen = GetNthChild(mChildNodes, mIndex);
        break;
    case kChoiceRandomJukeBox:
        if (numChildren <= 1) {
            auto firstChild = mChildNodes.begin()->Obj();
            if (numChildren == 1)
                chosen = firstChild;
            break;
        }
        {
            int historySize = mChoiceHistory.size();
            if (mIndex < 0 || mIndex >= historySize) {
                // Save last chosen before clearing
                FlowNode *lastChosen =
                    (historySize > 0) ? GetNthChild(mChoiceHistory, historySize - 1)
                                      : nullptr;
                mChoiceHistory.clear();
                // Add all children to history
                FOREACH (it, mChildNodes) {
                    mChoiceHistory.push_back(it->Obj());
                }
                // Shuffle via swap
                int newSize = mChoiceHistory.size();
                for (int i = newSize - 1; i > 0; i--) {
                    int j = RandomInt(0, i + 1);
                    mChoiceHistory.swap(i, j);
                }
                mIndex = 0;
                // If first element is same as lastChosen, start at 1
                auto firstHistory = mChoiceHistory.begin()->Obj();
                if (lastChosen && newSize > 0 &&
                    firstHistory == lastChosen) {
                    mIndex = 1;
                }
                historySize = newSize;
            }
            if (mIndex < historySize) {
                chosen = GetNthChild(mChoiceHistory, mIndex);
                mIndex++;
            }
        }
        break;
    case kChoiceUseIndex:
        {
            int adjustedIndex = mIndex % numChildren;
            mIndex = adjustedIndex;
            chosen = GetNthChild(mChildNodes, adjustedIndex);
        }
        mIndex++;
        break;
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
