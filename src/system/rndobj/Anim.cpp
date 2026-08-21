// MSVC expanded GetEaseFunction() inline here in the shipped build; see the
// opt-in note in math/Easing.h. Must precede the first include.
#define EASING_FORCE_INLINE_GET_EASE_FUNCTION 1
#include "rndobj\Anim.h"
#include "math/Easing.h"
#include "math\Utl.h"
#include "obj\Data.h"
#include "obj\DataUtl.h"

#include "obj\Msg.h"
#include "obj/Object.h"
#include "os\File.h"
#include "obj/Task.h"
#include "os\Debug.h"
#include "rndobj\AnimFilter.h"
#include "rndobj\Group.h"
#include "utl/BinStream.h"

static TaskUnits gRateUnits[6] = { kTaskSeconds, kTaskBeats,           kTaskUISeconds,
                                   kTaskBeats,   kTaskTutorialSeconds, kTaskBeats };
static float gRateFpu[6] = { 30.0f, 480.0f, 30.0f, 1.0f, 30.0f, 15.0f };

#pragma region Hmx::Object

RndAnimatable::RndAnimatable() : mFrame(0.0f), mRate(k30_fps) {}

BEGIN_HANDLERS(RndAnimatable)
    HANDLE_ACTION(set_frame, SetFrame(_msg->Float(2), 1.0f))
    HANDLE_EXPR(frame, mFrame)
    HANDLE_ACTION(set_key, SetKey(_msg->Float(2)))
    HANDLE_EXPR(end_frame, EndFrame())
    HANDLE_EXPR(start_frame, StartFrame())
    HANDLE(animate, OnAnimate)
    HANDLE_ACTION(stop_animation, StopAnimation())
    HANDLE_EXPR(is_animating, IsAnimating())
    HANDLE(convert_frames, OnConvertFrames)
    HANDLE(list_flow_labels, OnListFlowLabels)
END_HANDLERS

BEGIN_PROPSYNCS(RndAnimatable)
    SYNC_PROP(rate, (int &)mRate);
    SYNC_PROP_SET(frame, mFrame, SetFrame(_val.Float(), 1.0f))
    SYNC_PROP_SET(start_frame, StartFrame(), )
    SYNC_PROP_SET(end_frame, EndFrame(), )
END_PROPSYNCS

BEGIN_SAVES(RndAnimatable)
    SAVE_REVS(4, 0)
    bs << mFrame << mRate;
END_SAVES

BEGIN_COPYS(RndAnimatable)
    CREATE_COPY(RndAnimatable)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mFrame)
        COPY_MEMBER(mRate)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(4, 0)

BEGIN_LOADS(RndAnimatable)
    LOAD_REVS(bs)
    ASSERT_REVS(4, 0)
    if (d.rev > 1)
        d >> mFrame;
    if (d.rev > 3) {
        d >> (int &)mRate;
    } else if (d.rev > 2) {
        bool rate;
        d >> rate;
        mRate = (Rate)(!rate);
    }
    if (d.rev < 1) {
        int count;
        d >> count;
        float theScale = 1.0f;
        float theOffset = 0.0f;
        float theMin = 0.0f;
        float theMax = 0.0f;
        bool theLoop = false;
        int read;
        int unused1, unused2, unused3, unused4, unused5, unused6, unused7;
        while (count-- != 0) {
            d >> read;
            switch (read) {
            case 0:
                d >> theScale >> theOffset;
                break;
            case 1:
                d >> theMin >> theMax;
                d >> theLoop;
                break;
            case 2:
                d >> unused1 >> unused2;
                break;
            case 3:
                d >> unused3 >> unused4;
                break;
            case 4:
                d >> unused5 >> unused6 >> unused7;
                break;
            default:
                break;
            }
        }
        if (theScale != 1.0f || theOffset != 0.0f || (theMin != theMax)) {
            const char *filt = MakeString("%s.filt", FileGetBase(Name()));
            RndAnimFilter *filtObj = Dir()->New<RndAnimFilter>(filt);
            filtObj->SetProperty("anim", this);
            filtObj->SetProperty("scale", theScale);
            filtObj->SetProperty("offset", theOffset);
            filtObj->SetProperty("min", theMin);
            filtObj->SetProperty("max", theMax);
            filtObj->SetProperty("loop", theLoop);
        }
        ObjPtrList<RndAnimatable> animList(this);
        d >> animList;
        RndGroup *theGroup = dynamic_cast<RndGroup *>(this);
        FOREACH (it, animList) {
            if (theGroup)
                theGroup->AddObject(*it);
            else
                MILO_NOTIFY("%s not in group", (*it)->Name());
        }
    }
END_LOADS

#pragma endregion
#pragma region RndAnimatable

void RndAnimatable::SetFrame(float frame, float blend) {
    if (mFrame != frame) {
        static Symbol frameSym("frame");
        mFrame = frame;
        BroadcastPropertyChange(frameSym);
    }
}

TaskUnits RndAnimatable::RateToTaskUnits(Rate myRate) { return gRateUnits[myRate]; }
TaskUnits RndAnimatable::Units() const { return gRateUnits[mRate]; }
float RndAnimatable::FramesPerUnit() { return gRateFpu[mRate]; }

bool RndAnimatable::ConvertFrames(float &f) {
    f /= FramesPerUnit();
    return (Units() != kTaskBeats);
}

bool RndAnimatable::IsAnimating() {
    FOREACH (it, Refs()) {
        if (dynamic_cast<AnimTask *>(it->RefOwner()))
            return true;
    }
    return false;
}

void RndAnimatable::StopAnimation() {
    for (ObjRef::iterator it = mRefs.begin(); it != mRefs.end();) {
        AnimTask *task = dynamic_cast<AnimTask *>(it->RefOwner());
        if (task) {
            delete task;
            it = mRefs.begin();
        } else
            ++it;
    }
}

void RndAnimatable::FireFlowLabel(Symbol s) {
    if (s.Null()) return;
    FOREACH (it, Refs()) {
        Hmx::Object *owner = it->RefOwner();
        if (owner && owner->ClassName() == "AnimTask") {
            // The event goes to the AnimTask's listener, not to the task
            // itself -- target reads mListener.mObject (0x4c) and uses that
            // as the Handle receiver.  Matches AnimTask::Poll's "looped" and
            // "ended" sends, which both target mListener.
            Hmx::Object *listener = static_cast<AnimTask *>(owner)->Listener();
            if (listener) {
                listener->Handle(Message("on_anim_event", s), false);
                break;
            }
        }
    }
    static Symbol flow_label_fired("flow_label_fired");
    Message msg(flow_label_fired, s.Str());
    Export(msg, true);
}

Task *RndAnimatable::Animate(
    float blend, bool wait, float delay, Hmx::Object *o, EaseType e, float f4, bool b5
) {
    AnimTask *task = new AnimTask(
        this, StartFrame(), EndFrame(), FramesPerUnit(), Loop(), blend, o, e, f4, b5
    );
    ObjPtr<AnimTask> taskPtr(nullptr, task);
    if (wait && taskPtr->BlendTask()) {
        delay += taskPtr->BlendTask()->TimeUntilEnd();
    }
    if (delay == 0) {
        SetFrame(StartFrame(), 1);
    }
    TheTaskMgr.Start(taskPtr, Units(), delay);
    return taskPtr;
}

Task *RndAnimatable::Animate(
    float start,
    float end,
    TaskUnits units,
    float period,
    float blend,
    Hmx::Object *listener,
    EaseType easeType,
    float f9,
    bool b10
) {
    float fpu;
    if (period) {
        fpu = std::fabs(end - start);
        fpu = fpu / period;
    } else {
        const float fpus[3] = { 30.0f, 480.0f, 30.0f };
        fpu = fpus[units];
    }
    AnimTask *task =
        new AnimTask(this, start, end, fpu, false, blend, listener, easeType, f9, b10);
    ObjPtr<AnimTask> taskPtr(nullptr, task);
    SetFrame(start, 1);
    TheTaskMgr.Start(taskPtr, units, 0);
    return taskPtr;
}

Task *RndAnimatable::Animate(
    float blend,
    bool wait,
    float delay,
    Rate rate,
    float start,
    float end,
    float period,
    float scale,
    Symbol type,
    Hmx::Object *listener,
    EaseType easeType,
    float easePower,
    bool b10
) {
    static Symbol dest("dest");
    static Symbol loop("loop");
    float fpu;
    if (type == dest)
        start = mFrame;
    if (period) {
        fpu = std::fabs(end - start);
        fpu = fpu / period;
    } else
        fpu = scale * gRateFpu[rate];

    AnimTask *task = new AnimTask(
        this, start, end, fpu, type == loop, blend, listener, easeType, easePower, b10
    );
    ObjPtr<AnimTask> taskPtr(nullptr, task);
    if (wait) {
        if (taskPtr->BlendTask()) {
            delay += taskPtr->BlendTask()->TimeUntilEnd();
        }
    }
    if (delay == 0) {
        SetFrame(start, 1);
    }
    TheTaskMgr.Start(taskPtr, gRateUnits[rate], delay);
    return taskPtr;
}

#pragma endregion
#pragma region AnimTask

AnimTask::AnimTask(
    RndAnimatable *anim,
    float start,
    float end,
    float fpu,
    bool loop,
    float blend,
    Hmx::Object *listener,
    EaseType easeType,
    float easePower,
    bool wait
)
    : mAnim(this), mListener(this), mAnimTarget(this), mBlendTask(this),
      mBlendPeriod(blend), mLoop(loop), mEasePower(easePower) {
    mBlending = false;
    mBlendTime = 0;
    mWait = wait;
    mActive = true;
    mEaseFunc = GetEaseFunction(easeType);
    mListener = listener;
    MILO_ASSERT(anim, 0x213);
    mMin = Min(start, end);
    mMax = Max(start, end);
    if (NearlyZero(fpu)) {
        fpu = 1;
    }
    mFrameSpan = (mMax - mMin) / fpu;
    if (start < end) {
        mScale = fpu;
        mOffset = mMin;
    } else {
        mScale = -fpu;
        mOffset = mMax;
    }
    Hmx::Object *target = anim->AnimTarget();
    if (target) {
        FOREACH (it, target->Refs()) {
            Hmx::Object *owner = it->RefOwner();
            if (owner && owner->ClassName() == StaticClassName()) {
                mBlendTask = static_cast<AnimTask *>(owner);
                MILO_ASSERT(mBlendTask != this, 0x231);
                break;
            }
        }
    }
    if (mBlendPeriod && mBlendTask) {
        mBlendTask->mBlending = true;
    }
    mAnim = anim;
    mAnimTarget = anim->AnimTarget();
}

AnimTask::~AnimTask() { TheTaskMgr.QueueTaskDelete(mBlendTask); }

bool AnimTask::Replace(ObjRef *from, Hmx::Object *to) {
    if (from == &mAnim) {
        RndAnimatable *myAnim = Anim();
        if (!mAnim.SetObj(to)) {
            if (mBlendTask && mBlendTask->Anim() == myAnim) {
                mBlendTask = nullptr;
            }
            Hmx::Object::Replace(from, to);
            TheTaskMgr.QueueTaskDelete(this);
        }
        return true;
    } else
        return Hmx::Object::Replace(from, to);
}

float AnimTask::TimeUntilEnd() {
    float time;
    if (mScale > 0.0f) {
        float fpu = mAnim->FramesPerUnit();
        time = (mMax - mAnim->GetFrame()) / fpu;
    } else {
        float fpu = mAnim->FramesPerUnit();
        time = (mAnim->GetFrame() - mMin) / fpu;
    }
    return time;
}

void AnimTask::Poll(float time) {
    // A listener callback made from this function can `delete` this very task:
    // FlowAnimate::OnAnimEvent("looped") does `delete mAnimTask` when a stop is
    // deferred, and the AnimTask it deletes is the one whose Poll sent the
    // event. Everything after that -- `mListener = nullptr`, `mPrevFrame =
    // frame`, the mAnimTarget reads, and finally QueueTaskDelete(this) -- was a
    // use-after-free, and it is where TaskMgr's "already-destroyed task"
    // refusal came from (measured: caller is AnimTask::Poll, not ~AnimTask, and
    // the pointer is `this`, not mBlendTask).
    //
    // The watch is a stack flag ~Object trips; checking it is a load and a
    // branch. Deliberately NOT Task::IsLive(this): that is address-keyed and
    // ABA-unsound, so a recycled block would answer "still alive" and send us
    // straight back into the freed frame.
    //
    // Compiles to nothing on PPC, which keeps the original control flow.
    HX_DEATH_WATCH(this);
    if (!mAnim)
        return;
    if (mActive) {
        mAnim->StartAnim();
        HX_RETURN_IF_DELETED();
        mPrevFrame = mAnim->GetFrame();
        mActive = false;
    }
    float blend = 1.0f;
    if (mBlendPeriod) {
        blend = time / mBlendPeriod;
        if (blend >= 1.0f) {
            blend = 1.0f;
            TheTaskMgr.QueueTaskDelete(mBlendTask);
            mBlendPeriod = 0.0f;
        } else if (!mBlendTask) {
            float oldtime = mBlendTime;
            mBlendTime = time;
            blend = (time - oldtime) / (mBlendPeriod - oldtime);
        }
    } else {
        if (mBlendTask)
            TheTaskMgr.QueueTaskDelete(mBlendTask);
    }

    // The raw frame in animation space. Note this is never overwritten below: the
    // looped / waited / clamped value is a separate value handed to SetFrame, and
    // mPrevFrame always records this raw one.
    float frame;
    if (!mLoop && time <= mFrameSpan && mFrameSpan != 0.0f) {
        frame = mEaseFunc(time / mFrameSpan, mEasePower, 1.0f) * mScale * mFrameSpan
            + mOffset;
    } else {
        frame = mScale * time + mOffset;
    }

    if (mLoop) {
        float wrappedFrame = ModRange(mMin, mMax, frame);
        mAnim->SetFrame(wrappedFrame, blend);
        HX_RETURN_IF_DELETED();
        if (mListener) {
            // Which lap of [mMin, mMax] we are on. Both quotients must stay
            // separate divisions -- sharing a `range` local lets MSVC's reciprocal
            // transform turn them into one fdivs plus two fmuls.
            if ((int)(mPrevFrame / (mMax - mMin)) != (int)(frame / (mMax - mMin))) {
                static Message msg("on_anim_event", DataNode(Symbol("looped")));
                mListener->Handle(msg, false);
                HX_RETURN_IF_DELETED();
            }
        }
    } else {
        float setFrame;
        if (mWait) {
            // A waiting task wraps into the animatable's *own* frame range rather
            // than into [mMin, mMax], and snaps to whichever end of [mMin, mMax] it
            // is travelling from / to at the two boundaries.
            float startFrame = mAnim->StartFrame();
            float endFrame = mAnim->EndFrame();
            float animMin = Min(startFrame, endFrame);
            float animMax = Max(startFrame, endFrame);
            float animRange = animMax - animMin;
            float endpoint;
            // The two snap cases share one fmod. It has to live inside the second
            // case's block with the first jumping into it: that is what puts the
            // shared tail on the second arm's fall-through path, the way the target
            // lays it out. A goto to a statement *after* the if/else chain compiles
            // to the other placement (measured).
            if (time == 0.0f) {
                endpoint = mScale > 0.0f ? mMin : mMax;
                goto snap;
            }
            if (time >= mFrameSpan) {
                endpoint = mScale > 0.0f ? mMax : mMin;
            snap:
                setFrame = fmod(endpoint, animRange);
            } else {
                float wrapped = fmod(frame, animRange);
                setFrame = wrapped + animMin;
            }
        } else {
            setFrame = Clamp(mMin, mMax, frame);
        }
        mAnim->SetFrame(setFrame, blend);
        HX_RETURN_IF_DELETED();
    }

    mPrevFrame = frame;

#ifdef HX_NATIVE
    // On native, DTA callbacks that would call StopAnimation() or null mAnimTarget
    // never fire. Auto-null when a non-looping animation has completed.
    if (mAnimTarget && !mLoop && !mBlending && !mBlendPeriod) {
        if (time > mFrameSpan && mFrameSpan > 0.0f) {
            mAnimTarget = NULL;
        }
    }
#endif
    if (!mAnimTarget
        || (!mLoop && !mBlending && !mBlendPeriod
            && (time > mFrameSpan || mScale == 0.0f))) {
        if (mListener) {
            static Message msg("on_anim_event", DataNode(Symbol("ended")));
            mListener->Handle(msg, false);
            HX_RETURN_IF_DELETED();
            mListener = nullptr;
        }
        TheTaskMgr.QueueTaskDelete(this);
    }
}

#pragma endregion
#pragma region Handlers

DataNode RndAnimatable::OnConvertFrames(DataArray *arr) {
    float f = arr->Float(2);
    bool conv = ConvertFrames(f);
    *arr->Var(2) = f;
    return conv;
}

DataNode RndAnimatable::OnAnimate(DataArray *arr) {
    float local_blend; // 0x88
    float local_ease_power; // 0x84
    EaseType local_ease; // 0x80
    TaskUnits local_units; // 0x7c
    const char *local_name; // 0x78
    float local_delay; // 0x74
    bool local_wait; // 0x72
    bool local_wrap; // 0x71
    bool animTaskLoop; // 0x70

    local_blend = 0.0f;
    float animTaskStart = StartFrame();
    float animTaskEnd = EndFrame();
    animTaskLoop = Loop();
    float p = FramesPerUnit();
    local_units = Units();
    local_delay = 0.0f;
    local_name = nullptr;
    local_wait = false;
    local_wrap = false;
    local_ease_power = 2;
    local_ease = kEaseLinear;
    Hmx::Object *local_listener = nullptr;

    static Symbol blend("blend");
    static Symbol range("range");
    static Symbol loop("loop");
    static Symbol dest("dest");
    static Symbol period("period");
    static Symbol delay("delay");
    static Symbol units("units");
    static Symbol name("name");
    static Symbol wait("wait");
    static Symbol wrap("wrap");
    static Symbol ease_power("ease_power");
    static Symbol ease("ease");
    static Symbol listener("listener");

    arr->FindData(blend, local_blend, false);
    arr->FindData(delay, local_delay, false);
    arr->FindData(units, (int &)local_units, false);
    arr->FindData(name, local_name, false);
    arr->FindData(wait, local_wait, false);
    arr->FindData(wrap, local_wrap, false);
    arr->FindData(ease_power, local_ease_power, false);
    arr->FindData(ease, (int &)local_ease, false);

    if (arr->FindArray(listener, false)) {
        local_listener = arr->FindArray(listener, true)->GetObj(1);
    }
    DataArray *rangeArr = arr->FindArray(range, false);
    if (rangeArr) {
        animTaskStart = rangeArr->Float(1);
        animTaskEnd = rangeArr->Float(2);
        animTaskLoop = false;
    }
    DataArray *loopArr = arr->FindArray(loop, false);
    if (loopArr) {
        if (loopArr->Size() > 1)
            animTaskStart = loopArr->Float(1);
        else
            animTaskStart = StartFrame();
        if (loopArr->Size() > 2)
            animTaskEnd = loopArr->Float(2);
        else
            animTaskEnd = EndFrame();
        animTaskLoop = true;
    }
    DataArray *destArr = arr->FindArray(dest, false);
    if (destArr) {
        animTaskStart = GetFrame();
        animTaskEnd = destArr->Float(1);
        animTaskLoop = false;
    }
    DataArray *periodArr = arr->FindArray(period, false);
    if (periodArr) {
        p = periodArr->Float(1);
        MILO_ASSERT(p, 0x1C5);
        float fabs = std::fabs(animTaskEnd - animTaskStart);
        p = fabs / p;
    }
    AnimTask *task = new AnimTask(
        this,
        animTaskStart,
        animTaskEnd,
        p,
        animTaskLoop,
        local_blend,
        local_listener,
        local_ease,
        local_ease_power,
        local_wait
    );
    ObjPtr<AnimTask> taskPtr(nullptr, task);
    if (local_name && taskPtr) {
        MILO_ASSERT(DataThis(), 0x1CD);
        taskPtr->SetName(local_name, DataThis()->DataDir());
    }
    if (local_wait && taskPtr->BlendTask()) {
        if (taskPtr->BlendTask()->Anim()->GetRate() != GetRate()) {
            MILO_NOTIFY("%s: need same rate to wait", Name());
        } else
            local_delay = taskPtr->BlendTask()->TimeUntilEnd();
    }
    static Symbol trigger_anim_task("trigger_anim_task");
    if (!Property(trigger_anim_task, false) || Property(trigger_anim_task)->Int() != 0) {
        TheTaskMgr.Start(taskPtr, local_units, local_delay);
    }

    return DataNode(taskPtr.Ptr());
}
