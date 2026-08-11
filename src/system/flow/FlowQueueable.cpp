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
      , mListeners(this)
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
    // in temp, the ring auto-removes it (kObjListNoNull).
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
