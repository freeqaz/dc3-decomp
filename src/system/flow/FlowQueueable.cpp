#include "flow\FlowQueueable.h"
#include "flow\FlowNode.h"
#include "obj\Dir.h"
#include "obj\Msg.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include <list>

FlowQueueable::FlowQueueable()
    : mInterrupt(kImmediate)
#ifdef HX_NATIVE
      // kObjListAllowNull, NOT the default kObjListNoNull: mListeners is the
      // queue of pending triggers, and a trigger with no listener (every
      // Flow::Activate(), FlowRun) is a NULL entry that must be queued. The
      // image's std::list stores it (FlowQueueable.s, Activate: `stw r27,0x54`
      // with r27=0, then list::insert at this+0x60). A NoNull list silently
      // dropped it, so a kQueue/kQueueOne/kWhenAble flow re-triggered while
      // running lost the queued run. A listener destroyed while queued now
      // becomes a null entry (ReleaseListener(null) is a no-op) where the
      // Xbox list would hold a dangling pointer.
      , mListeners(this, kObjListAllowNull)
#endif
{}
FlowQueueable::~FlowQueueable() {}

BEGIN_HANDLERS(FlowQueueable)
    HANDLE_SUPERCLASS(FlowNode)
END_HANDLERS

BEGIN_PROPSYNCS(FlowQueueable)
    SYNC_PROP(interrupt, (int &)mInterrupt)
    SYNC_SUPERCLASS(FlowNode)
END_PROPSYNCS

BEGIN_SAVES(FlowQueueable)
    SAVE_REVS(0, 0)
    SAVE_SUPERCLASS(FlowNode)
    bs << mInterrupt;
END_SAVES

BEGIN_COPYS(FlowQueueable)
    COPY_SUPERCLASS(FlowNode)
    CREATE_COPY(FlowQueueable)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mInterrupt)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(0, 0)

BEGIN_LOADS(FlowQueueable)
    LOAD_REVS(bs)
    ASSERT_REVS(0, 0)
    LOAD_SUPERCLASS(FlowNode)
    d >> (int &)mInterrupt;
END_LOADS

void FlowQueueable::Deactivate(bool b) {
#ifdef HX_NATIVE
    if (ObjectDir::InDeleteObjects()) {
        mListeners.clear();
        FlowNode::Deactivate(b);
        return;
    }
    // ObjPtrList: pop before release to avoid ring-modified iteration.
    // If ReleaseListener triggers destruction of another listener still
    // in temp, the ring nulls its entry (kObjListAllowNull, see the ctor)
    // and ReleaseListener(nullptr) is a no-op.
    ObjPtrList<Hmx::Object> temp(mListeners);
    mListeners.clear();
    while (temp.size() > 0) {
        Hmx::Object *obj = temp.back();
        temp.pop_back();
        ReleaseListener(obj);
    }
#else
    std::list<Hmx::Object *> temp(mListeners);
    mListeners.clear();
    while (temp.size() > 0) {
        ReleaseListener(temp.back());
        temp.erase(--temp.end());
    }
#endif
    FlowNode::Deactivate(b);
}

void FlowQueueable::ChildFinished(FlowNode *node) {
    FLOW_LOG("Child Finished of class:%s\n", node->ClassName());
    if (mInterrupt == kPassThrough) {
        FlowNode::ChildFinished(node);
        return;
    }
    mRunningNodes.remove(node);
    if (!mRunningNodes.empty())
        return;

    if (mStopRequested) {
#ifdef HX_NATIVE
        if (ObjectDir::InDeleteObjects()) {
            mListeners.clear();
            return;
        }
        ObjPtrList<Hmx::Object> temp(mListeners);
        mListeners.clear();
        while (temp.size() > 0) {
            Hmx::Object *obj = temp.back();
            temp.pop_back();
            ReleaseListener(obj);
        }
#else
        std::list<Hmx::Object *> temp(mListeners);
        mListeners.clear();
        while (temp.size() > 0) {
            ReleaseListener(temp.back());
            temp.erase(--temp.end());
        }
#endif
        if (mFlowParent && mRunningNodes.empty()
            && mFlowParent->HasRunningNode(this)) {
            FLOW_LOG("Releasing\n");
            mFlowParent->ChildFinished(this);
        }
    } else {
        if (mListeners.size() > 1) {
            // w16-d (99.53, 9 rows at 0x82xxxD50..D90): the image keeps the
            // begin NODE in r10 and re-reads its value for ReleaseListener
            // (`lwz r4, 0x8(r10)`), i.e. front is reached through the node, not
            // copied. `Hmx::Object *&front = mListeners.front();` (or an
            // iterator `first` with `*first`) reproduces every register, but
            // MSVC then rotates the loop as a guarded do-while (cmplw/beq at
            // entry) where the image enters with `b <cond>` -- 98.6, 3 rows.
            // `for (++it; ...)` and `while (++it != end)` give the same
            // rotation; comparing against `mListeners.front()` inside the loop
            // is 99.1. Kept the higher-scoring value copy.
            // w21-ad (still 99.53; behaviour re-read vs 823F82C4..823F8368:
            // agrees, incl. the size()<=1 fallthrough to the !empty arm):
            // the rotation is LICM's -- whenever the value load sits INSIDE the
            // loop (reference `front`, `*first`-in-loop) MSVC hoists it into a
            // guarded preheader (98.6, 3 rows), and whenever it is a copy
            // before the loop the release reuses the copy. Measured: iterator
            // `first` + `front = *first` + ReleaseListener(*first) = CSE'd, same
            // 9 rows; ReleaseListener(mListeners.front()) re-reads begin AND
            // value (99.5, 8 rows); ref + value copy 98.1; `for (it++; ..; it++)`
            // with ref 98.6; `!found &&` in the condition 96.6; std::find 96.6.
            Hmx::Object *front = mListeners.front();
            bool found = false;
#ifdef HX_NATIVE
            auto it = mListeners.begin();
#else
            std::list<Hmx::Object *>::iterator it = mListeners.begin();
#endif
            ++it;
            for (; it != mListeners.end(); ++it) {
                if (*it == front) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                ReleaseListener(front);
            }
            mListeners.erase(mListeners.begin());
            ActivateTrigger();
        } else if (!mListeners.empty()) {
            ReleaseListener(mListeners.front());
            mListeners.erase(mListeners.begin());
        }
    }
}

bool FlowQueueable::Activate(Hmx::Object *obj) {
    FLOW_LOG("Activate\n");
    mStopRequested = false;
    if (mRunningNodes.empty()) {
        if (obj) {
            mListeners.push_back(obj);
        } else {
            mListeners.push_back(nullptr);
        }
        if (ActivateTrigger()) {
            return true;
        } else {
            mListeners.clear();
            return false;
        }
    } else {
        switch (mInterrupt) {
        case kIgnore:
            FLOW_LOG("Ignoring trigger\n");
            ReleaseListener(obj);
            return false;
        case kQueue:
            FLOW_LOG("Queueing trigger\n");
            if (obj) {
                mListeners.push_back(obj);
            } else {
                mListeners.push_back(nullptr);
            }
            return true;
        case kQueueOne:
            FLOW_LOG("Queue One\n");
            while (mListeners.size() > 1) {
                ReleaseListener(mListeners.back());
                mListeners.pop_back();
            }
            if (obj) {
                mListeners.push_back(obj);
            } else {
                mListeners.push_back(nullptr);
            }
            return true;
        case kImmediate:
            FLOW_LOG("Immediate Interrupt\n");
            FlowQueueable::Deactivate(false);
            if (obj) {
                mListeners.push_back(obj);
            } else {
                mListeners.push_back(nullptr);
            }
            ActivateTrigger();
            return !mRunningNodes.empty();
        case kWhenAble:
            FLOW_LOG("When Able Interruption\n");
            while (mListeners.size() > 1) {
                ReleaseListener(mListeners.back());
                mListeners.pop_back();
            }
            if (obj) {
                mListeners.push_back(obj);
            } else {
                mListeners.push_back(nullptr);
            }
            RequestStop();
            return true;
        default:
            MILO_NOTIFY_ONCE("FlowQueueable: bad interupt value");
            return false;
        }
    }
}

void FlowQueueable::RequestStopCancel() {
    if (!mStopRequested)
        return;
    FlowNode::RequestStopCancel();
}

void FlowQueueable::RequestStop() { FlowNode::RequestStop(); }

void FlowQueueable::ReleaseListener(Hmx::Object *obj) {
    if (obj) {
        obj->Handle(Message("on_flow_finished", this), true);
    }
}
