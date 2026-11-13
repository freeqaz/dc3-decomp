#include "flow\FlowPickOne.h"
#include "flow\FlowNode.h"
#include "flow\DrivenPropertyEntry.h"
#include "math/Rand.h"
#include "obj\Data.h"
#include "obj/Object.h"
#include "os\Debug.h"
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

// Lane history (76 lines moved): docs/decomp/notes/system/flow/FlowPickOne.md
// > w31-e 2026-11-03 (98.555, same 9 rows): nothing new to add on the r26 placement, but
// > the residual's SHAPE is worth stating precisely, because it is not pure placement --
// > the instruction multiset differs by exactly two rows.
bool FlowPickOne::Activate() {
    FLOW_LOG("Activate\n");
    mStopRequested = false;
    PushDrivenProperties();

    if (mChance != (int)1.0f) {
        unsigned int r = rand() % 100;
        if ((float)(int)r > mChance * 100.0f) {
            return false;
        }
    }

    if (mChildNodes.empty())
        return false;

    switch (mChoiceType) {
    case kChoiceOrdered: {
        // The target materialises this as a bool (li 1 / blt / li 0 / clrlwi. /
        // bne), not as two direct branches, so the original held it in a local.
        int index = mIndex;
        bool inRange = index >= 0 && index < mChildNodes.size();
        if (!inRange)
            mIndex = 0;
        ActivateChild(mChildNodes[mIndex]);
        mIndex++;
        break;
    }
    case kChoiceRandom: {
        int numChildren = (int)mChildNodes.size();
        mIndex = RandomInt(0, numChildren);
        ActivateChild(mChildNodes[mIndex]);
        break;
    }
    case kChoiceRandomNoRepeat: {
        int numChildren = (int)mChildNodes.size();
        if (numChildren > 1) {
            int newIndex;
            do {
                newIndex = RandomInt(0, (int)mChildNodes.size());
            } while (newIndex == mIndex);
            mIndex = newIndex;
        } else {
            mIndex = 0;
        }
        ActivateChild(mChildNodes[mIndex]);
        break;
    }
    case kChoiceRandomJukeBox: {
        int numChildren = (int)mChildNodes.size();
        // The >1 arm is the fall-through one in the target (`ble` to an
        // out-of-line block), so it is the `if` body, not the `else`.
        if (numChildren > 1) {
            // Same bool-in-a-local shape as kChoiceOrdered.
            FlowNode *lastChosen = nullptr;
            bool haveHistory = mIndex >= 0 && mIndex < (int)mChoiceHistory.size();
            if (!haveHistory) {
                if (!mChoiceHistory.empty()) {
                    lastChosen = mChoiceHistory.back();
                }
                mChoiceHistory.clear();
                std::vector<FlowNode *> items;
                FOREACH (it, mChildNodes) {
                    items.push_back(it->Obj());
                }
                RandomShuffle(items.begin(), items.end());
                // Lane history (6 lines moved): docs/decomp/notes/system/flow/FlowPickOne.md
                while (int remaining = items.size()) {
                    mChoiceHistory.push_back(items.back());
                    items.pop_back();
                }
                // mIndex is cleared AFTER the refill, in the middle of the
                // comparison below: `stw r24, 0x7c(r30)` at 0x82405FDC sits
                // between the `lwz r11, 0x0(r26)` that loads mChoiceHistory[0]
                // and the `lwz r11, 0xc(r11)` that derefs it.
                mIndex = 0;
                // No null guard on lastChosen: the image compares
                // unconditionally at 0x82405FE4 (`cmplw cr6, r25, r11` / `bne`)
                // with lastChosen still 0 when the history was empty.  The
                // `if (lastChosen)` we used to wrap this in was
                // decompilation-introduced.
                if (lastChosen == mChoiceHistory[0]) {
                    mIndex = 1;
                }
            }
            ActivateChild(mChoiceHistory[mIndex]);
            mIndex++;
        } else if (numChildren == 1) {
            ActivateChild(mChildNodes[0]);
        }
        break;
    }
    case kChoiceUseIndex: {
        int numChildren = (int)mChildNodes.size();
        int adjustedIndex = mIndex % numChildren;
        mIndex = adjustedIndex;
        // Unlike the other cases (which index mNodes directly), this one walks
        // from begin(): the target calls ObjPtrVec::begin() -- the
        // empty()?nullptr: ternary is right there -- and then steps the
        // iterator 0x14 at a time, re-reading mIndex as the loop bound.
        ObjPtrVec<FlowNode>::iterator it = mChildNodes.begin();
        for (int i = 0; i < mIndex; i++) {
            ++it;
        }
        // No mIndex++ here: the image branches this arm to the *non*-
        // incrementing tail at 0x824060A0 (`bl ActivateChild` then straight to
        // the `!mRunningNodes.empty()` epilogue at 0x82406108).  The
        // incrementing tail is 0x824060E4, and only kChoiceOrdered and the
        // jukebox history path reach it.  Semantically right too: "use index"
        // means the driven property owns mIndex.
        ActivateChild(it->Obj());
        break;
    }
    default:
        MILO_NOTIFY_ONCE("FlowPickOne: bad picking type");
        break;
    }

    return !mRunningNodes.empty();
}

void FlowPickOne::OnChoiceTypeChanged() {
    if (mChoiceType != kChoiceUseIndex) {
        FOREACH (it, mDrivenPropEntries) {
            if (it->Node().Array()->Sym(0) == "index") {
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
