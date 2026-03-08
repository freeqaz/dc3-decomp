#include "flow/FlowSound.h"
#include "FlowNode.h"
#include "flow/FlowManager.h"
#include "flow/FlowNode.h"
#include "flow/FlowPtr.h"
#include "math/Decibels.h"
#include "obj/Object.h"
#include "synth/Sound.h"

FlowSound::FlowSound()
    : mImmediateRelease(true), mStopMode(kStopLastFrame), mHasMarkerFired(false),
      mStopMarkerType(0), mStopRequested(false), mSound(this), mVolume(0), mTranspose(0),
      mPan(0), mIsPlaying(false), mForceStop(false), mUseIntensity(true),
      mCurrentIntensity(1) {}

FlowSound::~FlowSound() { TheFlowMgr->CancelCommand(this); }

BEGIN_HANDLERS(FlowSound)
    HANDLE_ACTION(on_flow_finished, ChildFinished(_msg->Obj<FlowNode>(2)))
    HANDLE_ACTION(on_marker_event, OnMarkerEvent(_msg->Sym(2)))
    HANDLE_SUPERCLASS(FlowNode)
END_HANDLERS

BEGIN_PROPSYNCS(FlowSound)
    SYNC_PROP(immediate_release, mImmediateRelease)
    SYNC_PROP(volume, mVolume)
    SYNC_PROP(pan, mPan)
    SYNC_PROP(transpose, mTranspose)
    SYNC_PROP_MODIFY(sound, mSound, OnSoundSelected())
    SYNC_PROP(stop_mode, (int &)mStopMode)
    SYNC_PROP(force_stop, mForceStop)
    SYNC_PROP(use_intensity, mUseIntensity)
    SYNC_SUPERCLASS(FlowNode)
END_PROPSYNCS

BEGIN_SAVES(FlowSound)
    SAVE_REVS(3, 0)
    SAVE_SUPERCLASS(FlowNode)
    bs << mImmediateRelease;
    bs << mSound;
    bs << mVolume;
    bs << mPan;
    bs << mTranspose;
    bs << mStopMode;
    bs << mForceStop;
    bs << mUseIntensity;
END_SAVES

INIT_REVS(3, 0)

BEGIN_LOADS(FlowSound)
    LOAD_REVS(bs)
    ASSERT_REVS(3, 0)
    LOAD_SUPERCLASS(FlowNode)
    d >> mImmediateRelease;
    if (d.rev < 2) {
        mSound = mSound.LoadFromMainOrDir(bs);
    } else {
        mSound.LoadFromMainOrDir(bs);
    }
    bs >> mVolume >> mPan >> mTranspose;
    if (d.rev > 0)
        d >> mForceStop;
    if (2 < d.rev)
        d >> mUseIntensity;
END_LOADS

BEGIN_COPYS(FlowSound)
    COPY_SUPERCLASS(FlowNode)
    CREATE_COPY_AS(FlowSound, c)
    BEGIN_COPYING_MEMBERS_FROM(c)
        COPY_MEMBER(mImmediateRelease)
        COPY_MEMBER(mVolume)
        COPY_MEMBER(mPan)
        COPY_MEMBER(mTranspose)
        COPY_MEMBER(mStopMode)
        COPY_MEMBER(mForceStop)
        COPY_MEMBER(mUseIntensity)
    END_COPYING_MEMBERS
END_COPYS

bool FlowSound::Activate() {
    FLOW_LOG("Activate\n");
    if (!mSound)
        return false;
    else {
        PushDrivenProperties();
        mCurrentIntensity = FlowNode::sIntensity;
        if (mImmediateRelease && !mForceStop) {
            mIsPlaying = false;
            float db = RatioToDb(mCurrentIntensity) + mVolume;
            mSound->Play(db, mPan, mTranspose, nullptr, 0);
            return false;
        } else if (mForceStop) {
            mIsPlaying = false;
            mSound->Stop(nullptr, false);
            return false;
        } else {
            TheFlowMgr->QueueCommand(this, kQueue);
            return true;
        }
    }
}

void FlowSound::Deactivate(bool b1) {
    FLOW_LOG("Deactivated\n");
    mIsPlaying = false;
    if (mSound) {
        mSound->Stop(this, false);
    }
    FlowNode::Deactivate(b1);
}

void FlowSound::ChildFinished(FlowNode *child) {
    FLOW_LOG("Child Finished of class:%s\n", child->ClassName());
    mRunningNodes.remove(child);
    if (!mIsPlaying) {
        FLOW_LOG("Timed Release From Parent \n");
        Timer timer;
        timer.Reset();
        timer.Start();
        mFlowParent->ChildFinished(this);
        timer.Stop();
        TheFlowMgr->AddMs(timer.Ms());
    }
}

void FlowSound::RequestStop() {
    FLOW_LOG("RequestStop\n");
    switch (mStopMode) {
    case FlowNode::kStopImmediate:
    case FlowNode::kReleaseAndContinue:
        mStopRequested = true;
        TheFlowMgr->QueueCommand(this, kIgnore);
        break;
    case FlowNode::kStopLastFrame:
        mStopRequested = true;
        break;
    case FlowNode::kStopOnMarker:
        mStopMarkerType = 2;
        mStopRequested = true;
        break;
    case FlowNode::kStopBetweenMarkers:
        if (mHasMarkerFired) {
            TheFlowMgr->QueueCommand(this, kIgnore);
        } else {
            mStopMarkerType = 3;
            mStopRequested = true;
        }
        break;
    default:
        break;
    }
    FlowNode::RequestStop();
}

void FlowSound::RequestStopCancel() {
    FLOW_LOG("RequestStopCancel\n");
    FlowNode::RequestStopCancel();
    if (mStopRequested) {
        mStopRequested = false;
        TheFlowMgr->QueueCommand(this, kQueue);
    }
}

void FlowSound::Execute(QueueState qs) {
    FLOW_LOG("Execute: state = %i\n", qs);
    if (IsRunning()) {
        if (qs == kIgnore) {
            mIsPlaying = false;
            if (mStopMode == kReleaseAndContinue) {
                mSound->EndLoop(this);
            } else {
                mSound->Stop(this, true);
            }
            if (!mImmediateRelease) {
                FLOW_LOG("Timed Release From Parent \n");
                Timer timer;
                timer.Reset();
                timer.Start();
                mFlowParent->ChildFinished(this);
                timer.Stop();
                TheFlowMgr->AddMs(timer.Ms());
            }
            FlowNode::Deactivate(false);
        }
    } else {
        if (qs == kQueue) {
            mStopRequested = false;
            mStopMarkerType = 0;
            if (!mIsPlaying) {
                mIsPlaying = true;
                float db = RatioToDb(mCurrentIntensity) + mVolume;
                mSound->Play(db, mPan, mTranspose, this, 0);
            }
        } else if (qs == kIgnore) {
            mFlowParent->ChildFinished(this);
        }
    }
}

bool FlowSound::IsRunning() { return mIsPlaying || FlowNode::IsRunning(); }

void FlowSound::UpdateIntensity() {
    FLOW_LOG("Updating Intensity: %0.2f\n", FlowNode::sIntensity);
    if (mUseIntensity) {
        float db = RatioToDb(FlowNode::sIntensity);
        mCurrentIntensity = FlowNode::sIntensity;
        db += mVolume;
        mSound->SetVolume(db, this);
    }
    FlowNode::UpdateIntensity();
}

void FlowSound::OnSoundSelected() {
    if (mSound) {
        if (mSound->Property("loop", true)->Int() == 1) {
            mImmediateRelease = false;
        }
    }
}
