#include "hamobj/FreestyleMoveRecorder.h"
#include "gesture/BaseSkeleton.h"
#include "gesture/GestureMgr.h"
#include "hamobj/DancerSkeleton.h"
#include "hamobj/FreestyleMove.h"
#include "obj/Data.h"
#include "obj/DataFunc.h"
#include "obj/Object.h"
#include "os/DateTime.h"
#include "rndobj/Tex.h"
#include "utl/FileStream.h"
#include "utl/Symbol.h"

DancerSkeleton sLastComparedDancerSkel;

FreestyleMoveRecorder::FreestyleMoveRecorder()
    : mPlaybackSpeed(0), mClipFrames(0), mClipFrameCount(0), mRecordingFrames(0), mLastFrameIndex(-1), mMaxFrames(60), mRecordPos(-1), mPlaybackPos(-1),
      mDefaultTimeout(15), mPlaybackIndex(-1), mRecording(0), mPlaybackActive(0), mSkeletonIndex(-1), mCurrentTakeIndex(0) {
    mPlayerPalette = Hmx::Object::New<RndTex>();
    mPlayerPalette->SetBitmap(320, 240, 16, RndTex::kRegularLinear, false, nullptr);

    JointAngle angle;
    angle.mJoint = kJointHandRight;
    mAngleLimits.push_back(angle);
    angle.mJoint = kJointHandLeft;
    mAngleLimits.push_back(angle);
    angle.mJoint = kJointAnkleRight;
    mAngleLimits.push_back(angle);
    angle.mJoint = kJointAnkleLeft;
    mAngleLimits.push_back(angle);
    angle.mJoint = kJointKneeRight;
    mAngleLimits.push_back(angle);
    angle.mJoint = kJointKneeLeft;
    mAngleLimits.push_back(angle);
    mTrackedJoints.push_back(kJointHandRight); // 11
    mTrackedJoints.push_back(kJointHandLeft); // 7
    mTrackedJoints.push_back(kJointAnkleRight); // 17
    mTrackedJoints.push_back(kJointAnkleLeft); // 14
    mTrackedJoints.push_back(kJointHead); // 3
    mTrackedJoints.push_back(kJointHipCenter); // 0
    JointPos pos;
    pos.mJoint = 11;
    pos.unk4 = 2;
    mPositions.push_back(pos);
    pos.mJoint = 7;
    pos.unk4 = 1;
    mPositions.push_back(pos);
    pos.mJoint = 9;
    pos.unk4 = 2;
    mPositions.push_back(pos);
    pos.mJoint = 5;
    pos.unk4 = 1;
    mPositions.push_back(pos);
    pos.mJoint = 17;
    pos.unk4 = 4;
    mPositions.push_back(pos);
    pos.mJoint = 14;
    pos.unk4 = 3;
    mPositions.push_back(pos);
    pos.mJoint = 16;
    pos.unk4 = 4;
    mPositions.push_back(pos);
    pos.mJoint = 13;
    pos.unk4 = 3;
    mPositions.push_back(pos);
    mFrameBuffer = new FreestyleMoveFrame[mMaxFrames];
    DataRegisterFunc("bam_record_attempt", OnRecordAttempt);
    DataRegisterFunc("bam_write_created", OnWriteCreated);
    DataRegisterFunc("bam_read_created", OnReadCreated);
    DataRegisterFunc("bam_read_attempt", OnReadAttempt);
    DataRegisterFunc("bam_clear", OnClearAttempt);
}

FreestyleMoveRecorder::~FreestyleMoveRecorder() {
    delete mPlayerPalette;
    delete[] mFrameBuffer;
    delete[] mRecordingFrames;
    delete[] mClipFrames;
}

void FreestyleMoveRecorder::Free() {
    mRecordPos = -1;
    mPlaybackPos = -1;
    for (int i = 4; i != 0; i--) {
        mTakes[mCurrentTakeIndex].Free();
    }
}

void FreestyleMoveRecorder::StartRecording() {
    mPlaybackIndex = 0xffffffff;
    mRecording = false;
    mRecordPos = 0;
    mPlaybackPos = -1;
    if (mLastFrameIndex != mCurrentTakeIndex) {
        mTakes[mCurrentTakeIndex].Init(mMaxFrames);
    }
}

void FreestyleMoveRecorder::ClearRecording() {
    if (mLastFrameIndex != mCurrentTakeIndex) {
        mTakes[mCurrentTakeIndex].Clear();
    }
    mFrameIndex = 0;
}

void FreestyleMoveRecorder::StartRecordingDancerTake() {
    StartRecording();
    mRecording = true;
}

void FreestyleMoveRecorder::StopRecording() {
    mPlaybackIndex = mTakes[mCurrentTakeIndex].mNumFrames + 2;
}

void FreestyleMoveRecorder::StartPlayback(bool param_1) {
    mPlaybackActive = param_1;
    mPlaybackPos = 0;
}

void FreestyleMoveRecorder::StopPlayback() { mPlaybackPos = -1; }

void FreestyleMoveRecorder::ClearDancerTake() { mDancerTakeFrameCount = 0; }

void FreestyleMoveRecorder::AssignStaticInstance() { sInstance = this; }

void FreestyleMoveRecorder::UpdateRecordingAttempt(
    const BaseSkeleton *skeleton, float f2
) {
    if (mClipName != gNullStr) {
        mRecordingFrames[mRecordingFrameCount].skeleton.Set(*skeleton);
        mRecordingFrames[mRecordingFrameCount].mBeat = f2;
        mRecordingFrameCount++;
    }
}

void FreestyleMoveRecorder::RecordMoveAttempt(String str) {
    mClipName = str;
    delete[] mRecordingFrames;
    mRecordingFrames = new FreestyleMoveFrame[480];
    mRecordingFrameCount = 0;
}

void FreestyleMoveRecorder::WriteRecordedMoveAttempt() {
    WriteFreestyleMoveClip(mClipName, mRecordingFrameCount, mRecordingFrames);
    mClipName = gNullStr;
    delete[] mRecordingFrames;
    mRecordingFrames = nullptr;
    mRecordingFrameCount = 0;
}

void FreestyleMoveRecorder::ClearFreestyleMoveClip() {
    delete[] mClipFrames;
    mClipFrames = nullptr;
    mClipFrameCount = 0;
}

void FreestyleMoveRecorder::PlaybackComplete() {
    if (mClipName != gNullStr) {
        WriteRecordedMoveAttempt();
    }
}

void FreestyleMoveRecorder::ClearFrameScores() {
    for (int i = 0; i < 2; i++) {
        unke4[i].Clear();
    }
}

void FreestyleMoveRecorder::WriteFreestyleMoveClip(
    String str, int framecount, FreestyleMoveFrame *frames
) {
    if (str.length() > 0x26) {
        str.resize(0x26);
    }
    str += ".bamclp";
    const char *path = MakeString("devkit:\\%s", str);
    FileStream stream(path, FileStream::kWrite, true);
    stream << mRecordingTarget;
    stream << framecount;
    for (int i = 0; i < framecount; i++) {
        frames[i].skeleton.Write(stream);
        stream << frames[i].mBeat;
    }
    MILO_LOG("Saved clip to %s, framecount: %d\n", path, framecount);
}

void FreestyleMoveRecorder::ReadFreestyleMoveClip(
    String str, int &framecount, FreestyleMoveFrame *frames
) {
    if (str.length() > 0x26) {
        str.resize(0x26);
    }
    str += ".bamclp";
    const char *path = MakeString("devkit:\\%s", str);
    FileStream stream(path, FileStream::kRead, true);
    Symbol s;
    stream >> s;
    stream >> framecount;
    for (int i = 0; i < framecount; i++) {
        frames[i].skeleton.Read(stream);
        stream >> frames[i].mBeat;
    }
    MILO_LOG("Loaded clip that was recorded with %s, framecount: %d\n", s, framecount);
}

DataNode FreestyleMoveRecorder::OnRecordAttempt(DataArray *a) {
    String str;
    if (a->Size() >= 2) {
        str = a->Str(1);
    } else {
        str = sInstance->mRecordingTarget.Str();
        str += "_attempt_";
        DateTime dt;
        GetDateAndTime(dt);
        str += MakeString("%02d%02d_%02d%02d", dt.Month(), dt.mDay, dt.mHour, dt.mMin);
    }
    sInstance->RecordMoveAttempt(str);
    return 0;
}

DataNode FreestyleMoveRecorder::OnWriteCreated(DataArray *a) {
    String str;
    if (a->Size() >= 2) {
        str = a->Str(1);
    } else {
        str = sInstance->mRecordingTarget.Str();
        str += "_created_";
        DateTime dt;
        GetDateAndTime(dt);
        str += MakeString("%02d%02d_%02d%02d", dt.Month(), dt.mDay, dt.mHour, dt.mMin);
    }
    sInstance->WriteFreestyleMoveClip(
        str,
        sInstance->mTakes[sInstance->mCurrentTakeIndex].mNumFrames,
        sInstance->mTakes[sInstance->mCurrentTakeIndex].mFrames
    );
    return 0;
}

DataNode FreestyleMoveRecorder::OnReadCreated(DataArray *a) {
    int framecount;
    sInstance->ReadFreestyleMoveClip(
        a->Str(1), framecount, sInstance->mTakes[sInstance->mCurrentTakeIndex].mFrames
    );
    sInstance->mTakes[sInstance->mCurrentTakeIndex].Init(sInstance->mMaxFrames);
    sInstance->mTakes[sInstance->mCurrentTakeIndex].mNumFrames = framecount;
    sInstance->mLastFrameIndex = sInstance->mCurrentTakeIndex;
    return 0;
}

DataNode FreestyleMoveRecorder::OnReadAttempt(DataArray *a) {
    delete[] sInstance->mClipFrames;
    sInstance->mClipFrames = new FreestyleMoveFrame[480];
    sInstance->ReadFreestyleMoveClip(a->Str(1), sInstance->mClipFrameCount, sInstance->mClipFrames);
    return 0;
}

DataNode FreestyleMoveRecorder::OnClearAttempt(DataArray *a) {
    sInstance->ClearFreestyleMoveClip();
    return 0;
}

float FreestyleMoveRecorder::GetScore(int i1, int i2, float f, bool b) {
    // Default: use skeleton from gesture manager if player index is valid
    BaseSkeleton *skeletonToScore = nullptr;
    if (i1 >= 0) {
        skeletonToScore = &TheGestureMgr->GetSkeleton(i1);
    }

    // Check if there's a live skeleton that should override the default
    BaseSkeleton *liveSkeleton = GetLiveSkeleton();
    if (liveSkeleton) {
        // Get the reference skeleton (if mSkeletonIndex is set)
        BaseSkeleton *referenceSkeleton;
        if (mSkeletonIndex >= 0) {
            referenceSkeleton = &TheGestureMgr->GetSkeleton(mSkeletonIndex);
        } else {
            referenceSkeleton = nullptr;
        }

        // Use live skeleton only if it differs from reference
        if (liveSkeleton != referenceSkeleton) {
            skeletonToScore = liveSkeleton;
        }
    }

    return GetScore(skeletonToScore, i2, f, b);
}
