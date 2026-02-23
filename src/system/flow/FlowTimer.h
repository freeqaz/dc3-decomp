#pragma once
#include "flow/FlowNode.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "obj/PropSync.h"
#include "obj/Task.h"
#include "utl/BinStream.h"
#include "utl/PoolAlloc.h"

class FlowTimer;

class EventTask : public Task {
public:
    EventTask(FlowTimer *, ObjPtrVec<FlowNode> *, TaskUnits, float);
    virtual ~EventTask();
    OBJ_CLASSNAME(EventTask)
    virtual void Poll(float);

    POOL_OVERLOAD(EventTask, 0x12)

protected:
    ObjPtr<FlowTimer> mOwner; // 0x2C
    ObjPtrVec<FlowNode> *mChildNodes; // 0x40
    float mDuration; // 0x44
    float mElapsed; // 0x48
};

class FlowTimer : public FlowNode {
public:
    // Hmx::Object
    virtual ~FlowTimer();
    OBJ_CLASSNAME(FlowTimer)
    OBJ_SET_TYPE(FlowTimer)
    DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);
    virtual void Save(BinStream &);
    virtual void Copy(const Hmx::Object *, Hmx::Object::CopyType);
    virtual void Load(BinStream &);

    // FlowNode
    virtual bool Activate();
    virtual void Deactivate(bool);
    virtual void ChildFinished(FlowNode *);
    virtual void RequestStop();
    virtual void RequestStopCancel();
    virtual void Execute(FlowNode::QueueState);
    virtual bool IsRunning();

    void OnKeyframe(FlowNode *);
    void OnTimerEnd();
    OBJ_MEM_OVERLOAD(0x17)
    NEW_OBJ(FlowTimer)

    int mStopMode;
    ObjPtr<Task> mTask;
    int mRate; // 0x74
    float mTotalTime; // 0x78

protected:
    FlowTimer();
};
