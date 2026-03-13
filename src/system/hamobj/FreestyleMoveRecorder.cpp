#include "hamobj/FreestyleMoveRecorder.h"
#include "gesture/BaseSkeleton.h"
#include "gesture/GestureMgr.h"
#include "gesture/SkeletonUpdate.h"
#include "gesture/SkeletonViz.h"
#include "hamobj/DancerSkeleton.h"
#include "hamobj/FreestyleMove.h"
#include "math/Color.h"
#include "math/Geo.h"
#include "math/Vec.h"
#include "obj/Data.h"
#include "obj/DataFunc.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os/DateTime.h"
#include "rndobj/Rnd.h"
#include "rndobj/Tex.h"
#include "utl/FileStream.h"
#include "utl/Symbol.h"
#include "xdk/LIBCMT/ppcintrinsics.h"

DancerSkeleton sLastComparedDancerSkel;
static int sLastBeatMod;
static SkeletonViz *sVizRecorded = nullptr;
static SkeletonViz *sVizLive = nullptr;
static float sDebugRectX = 0.1f;
static float sDebugRectY = 0.3f;
static float sDebugRectW = 0.3f;

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

void FreestyleMoveRecorder::UpdateFakeSkeleton() {
    mPlaybackSpeed += TheTaskMgr.DeltaUISeconds();
    int beatMod = (int)TheTaskMgr.Beat() % 4;
    if (beatMod == 0 && sLastBeatMod != 0) {
        mPlaybackSpeed = 0;
    }
    sLastBeatMod = beatMod;
}

void FreestyleMoveRecorder::Poll() {
    int recordFrame;
    if (mRecordPos >= 0.0f) {
        recordFrame = (int)(mDefaultTimeout * mRecordPos) - 2;
    } else {
        recordFrame = -1;
    }

    int playbackFrame;
    if (mPlaybackPos >= 0.0f) {
        playbackFrame = (int)(mDefaultTimeout * mPlaybackPos);
    } else {
        playbackFrame = -1;
    }

    int maxFrame = mMaxFrames - 1;
    if (maxFrame < recordFrame) {
        recordFrame = maxFrame;
    }

    if (recordFrame >= 0 && mLastFrameIndex != mCurrentTakeIndex) {
        LiveCameraInput *camInput = TheGestureMgr->GetLiveCameraInput();
        if (!camInput->mDepthPolled) {
            camInput->PollNewStream(LiveCameraInput::kBufferDepth);
        }
        RndTex *streamTex = camInput->GetStreamTex(LiveCameraInput::kBufferDepth);
        if (streamTex) {
            void *texels = nullptr;
            char *depthDst =
                (char *)mTakes[mCurrentTakeIndex].mDepthFrames + recordFrame * 0x12c0;
            streamTex->TexelsLock(texels);
            if (texels) {
                int playerIdx = mSkeletonIndex;
                mTakes[mCurrentTakeIndex].unkc = playerIdx;
                unsigned short *src = (unsigned short *)texels;
                char *dst = depthDst - 0x50;
                int col = 0;
                do {
                    for (int row = 0x3c; row != 0; row--) {
                        int pixelPlayer = (*src & 7) - 1;
                        unsigned char depth;
                        if (pixelPlayer != playerIdx) {
                            if (playerIdx >= 0) {
                                depth = (unsigned char)((*src >> 7) & 0xFF);
                            } else {
                                depth = 0;
                            }
                        } else {
                            depth = (unsigned char)((*src >> 7) & 0xFF);
                        }
                        src += 0x600;
                        *(unsigned char *)(dst += 0x50) = depth;
                    }
                    col++;
                    texels = (char *)texels + 8;
                    src = (unsigned short *)texels;
                } while (col < 0x50);
            }
            streamTex->TexelsUnlock();
        }

        if (recordFrame == 0) {
            mTakes[mCurrentTakeIndex].CalcCentering(0);
        }

        int nextFrame = recordFrame + 1;
        if (nextFrame < mPlaybackIndex || mPlaybackIndex == -1) {
            if (!mRecording) {
                mTakes[mCurrentTakeIndex].mNumFrames = nextFrame;
                float beat = mRecordPos * 1000.0f;
                BaseSkeleton *skel = GetLiveSkeleton();
                mTakes[mCurrentTakeIndex].RecordSkeletonFrame(skel, recordFrame, beat);
            } else {
                BaseSkeleton *skel = GetLiveSkeleton();
                DancerSkeleton tempSkel;
                tempSkel.Init();
                float beat = mRecordPos * 1000.0f;
                if (skel && skel->IsTracked()) {
                    tempSkel.Set(*skel);
                }
                mFrameBuffer[recordFrame].skeleton = tempSkel;
                mFrameBuffer[recordFrame].mBeat = beat;
                mDancerTakeFrameCount = nextFrame;
            }

            int frameIdx = mFrameIndex;
            if (mFrameIndex < nextFrame) {
                frameIdx = nextFrame;
            }
            mFrameIndex = frameIdx;
        } else {
            mRecording = false;
            mPlaybackIndex = -1;
            mRecordPos = -1.0f;
        }
    }

    if (playbackFrame >= 0
        && (playbackFrame < mTakes[mCurrentTakeIndex].mNumFrames || mPlaybackActive)) {
        void *texels;
        mPlayerPalette->TexelsLock(texels);

        int prevFrame = playbackFrame - 1;
        int lastFrame = mFrameIndex - 1;
        if (prevFrame <= lastFrame) {
            lastFrame = prevFrame;
            if (prevFrame < 0) {
                lastFrame = 0;
            }
        }

        int takeIdx = mCurrentTakeIndex;
        char *depthBase = (char *)mTakes[takeIdx].mDepthFrames;
        int centerX = mTakes[takeIdx].unkc << 2;
        int unkVal = mTakes[takeIdx].unk14;
        int minDepth = unkVal - 0x7a;

        if (mPlaybackActive) {
            centerX = 0;
            minDepth = 0;
        }

        int pixelX = 0;
        unsigned short *rowPtr = (unsigned short *)((char *)texels - 0x300);
        int unkColor = mTakes[takeIdx].unkc;
        for (int col = 0; col < 0x140; col++) {
            int x = pixelX + centerX;
            unsigned int row = 0;
            unsigned short *ptr = rowPtr;
            for (int r = 0xf0; r != 0; r--) {
                unsigned int depthVal = 0;
                if ((int)x >= 0 && (int)x < 0x140) {
                    int depthY = ((int)row >> 2) + ((int)row < 0 && (row & 3) != 0 ? 1 : 0);
                    int depthX = ((int)x >> 2) + ((int)x < 0 && (x & 3) != 0 ? 1 : 0);
                    depthVal = (unsigned int)*(unsigned char *)(
                        depthBase + lastFrame * 0x12c0 + depthY * 0x50 + depthX
                    );
                }
                row++;
                ptr += 0x180;
                *ptr = (unsigned short)(((depthVal - minDepth) & 0xffffffff) << 7)
                    | (unsigned short)(-(unsigned short)(depthVal != 0)
                                       & ((short)unkColor + 1));
            }
            pixelX++;
            rowPtr = (unsigned short *)((char *)rowPtr + 2);
        }

        mPlayerPalette->TexelsUnlock();
    }

    if (0.0f <= mRecordPos) {
        mRecordPos += TheTaskMgr.DeltaUISeconds();
    }
    if (0.0f <= mPlaybackPos) {
        mPlaybackPos += TheTaskMgr.DeltaUISeconds();
    }
    UpdateFakeSkeleton();
}

void FreestyleMoveRecorder::DrawDebug() {
    if (DataVariable("bam_debug").Int()) {
        if (sVizRecorded == nullptr) {
            sVizRecorded = Hmx::Object::New<SkeletonViz>();
            sVizRecorded->Init();
            sVizLive = Hmx::Object::New<SkeletonViz>();
            sVizLive->Init();
        }

        SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();

        std::vector<SkeletonCallback *> callbacks;
        callbacks.push_back(this);

        float screenScale = sDebugRectW / TheRnd.YRatio();

        Hmx::Rect rect1(sDebugRectX, sDebugRectY, sDebugRectW, screenScale);
        Hmx::Color bgColor1(0, 0, 0, 0.4f);
        TheRnd.DrawRectScreen(rect1, bgColor1, nullptr, nullptr, nullptr);

        sVizRecorded->SetUsePhysicalCam(true);
        sVizRecorded->SetPhysicalCamScreenRect(rect1);
        sVizRecorded->Visualize(
            *handle.GetCameraInput(), sLastComparedDancerSkel, &callbacks, false
        );

        float screenScale2 = sDebugRectW / TheRnd.YRatio();
        Hmx::Rect rect2(sDebugRectX + sDebugRectW + 0.1f, sDebugRectY, sDebugRectW, screenScale2);
        Hmx::Color bgColor2(0, 0, 0, 0.4f);
        TheRnd.DrawRectScreen(rect2, bgColor2, nullptr, nullptr, nullptr);

        sVizLive->SetUsePhysicalCam(true);
        sVizLive->SetPhysicalCamScreenRect(rect2);
        BaseSkeleton *liveSkel = GetLiveSkeleton();
        if (liveSkel) {
            sVizLive->Visualize(
                *handle.GetCameraInput(), *liveSkel, &callbacks, false
            );
        }
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

BaseSkeleton *FreestyleMoveRecorder::GetLiveSkeleton() {
    int numFrames = mClipFrameCount;
    if (numFrames > 0) {
        int count = 0;
        int idx = 0;
        int byteOff = 0;
        do {
            if (count == unk40)
                break;
            float *base = (float *)((char *)mClipFrames + byteOff);
            if (base[0x2d8 / 4] > base[0x5b4 / 4]) {
                count++;
            }
            idx++;
            byteOff += 0x2dc;
        } while (idx < numFrames);

        if (idx < numFrames) {
            int off = idx * 0x2dc;
            do {
                if (*(float *)((char *)mClipFrames + off + 0x2d8) > mPlaybackSpeed * 1000.0f)
                    break;
                idx++;
                off += 0x2dc;
            } while (idx < numFrames);
        }

        return (BaseSkeleton *)((char *)mClipFrames + idx * 0x2dc);
    }
    if (mSkeletonIndex >= 0) {
        return &TheGestureMgr->GetSkeleton(mSkeletonIndex);
    }
    return NULL;
}

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

void FreestyleMoveRecorder::CompareDisplacementVectors(
    const Vector3 &v1, int count1, const Vector3 &v2, int count2, float &outSimilarity, float &outMaxDisp
) const {
    float zero = 0.0f;
    float len1 = Length(v1);
    float len2 = Length(v2);
    float avgDisp1 = zero;
    if (count1 != 0) {
        avgDisp1 = len1 / (float)count1;
    }
    float avgDisp2 = zero;
    if (count2 != 0) {
        avgDisp2 = len2 / (float)count2;
    }
    float maxDisp = avgDisp1;
    if (avgDisp1 - avgDisp2 < 0.0f) {
        maxDisp = avgDisp2;
    }
    outMaxDisp = maxDisp + 1e-5f;
    float invLen1 = zero;
    if (0.0f < len1) {
        invLen1 = 1.0f / len1;
    }
    float invLen2 = zero;
    if (0.0f < len2) {
        invLen2 = 1.0f / len2;
    }
    float dot = (v2.y * invLen2 * v1.y * invLen1
                 + v2.x * invLen2 * v1.x * invLen1
                 + invLen2 * v2.z * invLen1 * v1.z)
        * 0.87f;
    float angleDiff = -(dot - 1.0f);
    float clamped = (float)__fsel(-angleDiff, angleDiff, zero);
    float clamped1 = (float)__fsel(clamped - 1.0f, 1.0f, clamped);
    float score = clamped1 * clamped1 * 20.0f;
    float finalScore = (float)__fsel(-score, score, zero);
    float finalClamped = (float)__fsel(finalScore - 1.0f, 1.0f, finalScore);
    outSimilarity = 1.0f - finalClamped;
}

float FreestyleMoveRecorder::CompareSkeletonPositions(
    const BaseSkeleton *skel1, const BaseSkeleton *skel2, float scale
) const {
    if (skel1 && skel2) {
        if (skel1->IsTracked()) {
            if (skel2->IsTracked()) {
                unsigned int count = 0;
                float totalDist = 0.0f;
                float zero = 0.0f;
                if (mPositions.size() != 0) {
                    int idx = 0;
                    do {
                        Vector3 pos1, pos2;
                        skel1->NormPos(
                            (SkeletonCoordSys)mPositions[count].unk4,
                            (SkeletonJoint)mPositions[count].mJoint, pos1
                        );
                        skel2->NormPos(
                            (SkeletonCoordSys)mPositions[count].unk4,
                            (SkeletonJoint)mPositions[count].mJoint, pos2
                        );
                        count++;
                        idx += 8;
                        totalDist = (pos1.y - pos2.y) * (pos1.y - pos2.y)
                            + (pos1.z - pos2.z) * (pos1.z - pos2.z)
                            + (pos1.x - pos2.x) * (pos1.x - pos2.x) + totalDist;
                    } while (count < (unsigned int)mPositions.size());
                }
                float avg = totalDist / (float)(unsigned int)mAngleLimits.size();
                float result = avg * scale;
                float clamped = (float)__fsel(-result, result, zero);
                float clamped1 = (float)__fsel(clamped - 1.0f, 1.0f, clamped);
                return 1.0f - clamped1;
            }
        }
    }
    return 0.0f;
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
