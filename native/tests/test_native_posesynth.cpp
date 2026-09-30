// Native pose-synthesis lane (branch native-posesynth, 2026-09-30).
//
// scripts/synthetic_kinect.py now PERFORMS: it replays the choreography /
// fatality pose the game asks for as live skeleton frames, so the decompiled
// scoring rates moves natively for the first time.  Each test here pins a
// divergence that only became reachable once a native player could score.
//
// HamUserPadNumTest: a perform-route run that scored 1.66M points crashed on
// perform_endgame_screen: MetagameRank::GetNextDeferredPoints ->
// ComputeRankNumber -> AwardForRankUp -> HamProfile::GetHamUser ->
// HamUserMgr::GetUserFromPad -> HamUser::GetPadNum, SIGSEGV at 0x88 --
// TheSkeletonIdentifier is null on native (ShellInput::Init's HX_NATIVE arm
// never builds one; the image always has one from boot).  A rank-up needs
// points, so no native run had reached it before.

#include <gtest/gtest.h>

#include "game/HamUser.h"
#define private public
#include "meta_ham/SkeletonIdentifier.h"
#undef private

namespace {

TEST(HamUserPadNumTest, NoSkeletonIdentifierMeansNoEnrolledPad) {
    // The image's HamUser::GetPadNum reads the enrolled player's pad from
    // TheSkeletonIdentifier (SkeletonIdentifier::UpdateEnrolledPlayers fills
    // it from NuiIdentityGetEnrollmentInformation; a player with no NUI
    // enrollment gets mPadNum = -1, and GetPadNum returns -1).  Native has no
    // identifier at all -- the same "nobody enrolled" state -- so the answer
    // must be -1, not a null dereference.
    SkeletonIdentifier *saved = TheSkeletonIdentifier;
    TheSkeletonIdentifier = nullptr;
    HamUser *user = HamUser::NewHamUser(0);
    ASSERT_NE(user, nullptr);
    EXPECT_EQ(user->GetPadNum(), -1);
    EXPECT_FALSE(user->CanSaveData());
    delete user;
    TheSkeletonIdentifier = saved;
}

TEST(HamUserPadNumTest, EnrolledPadStillComesFromTheIdentifier) {
    // Control: with an identifier present the image's path is untouched --
    // an enrolled pad below 4 is returned, 4 and up reads as none.
    SkeletonIdentifier *saved = TheSkeletonIdentifier;
    {
        SkeletonIdentifier ident; // ctor sets TheSkeletonIdentifier = this
        ASSERT_EQ(TheSkeletonIdentifier, &ident);
        HamUser *user = HamUser::NewHamUser(1);
        ASSERT_NE(user, nullptr);
        ident.mEnrolledPlayers[1].mPadNum = 2;
        EXPECT_EQ(user->GetPadNum(), 2);
        ident.mEnrolledPlayers[1].mPadNum = 5;
        EXPECT_EQ(user->GetPadNum(), -1);
        delete user;
    }
    TheSkeletonIdentifier = saved;
}

} // namespace

// ---------------------------------------------------------------------------
// NativeSkeletonPollTest: a live native skeleton must carry every coordinate
// system Skeleton::Poll derives, not only camera space.
//
// Found by the performing sensor in a dance-battle fatality: the sensor fed
// the fatality's own target skeleton (PoseFatalities::mPlayerSkeletons, a
// translated copy) and PoseFatalities::UpdateMatchingPose still scored the
// match 0.0.  FreestyleMoveRecorder::CompareSkeletonPositions compares
// NormPos in the four LIMB coordinate systems (kCoordLeftArm..kCoordRightLeg),
// i.e. Skeleton::JointPos(cs) = mTrackedJoints[j].mJointPos[cs], which the
// image's Skeleton::Poll fills by MultiplyTranspose through mPlayerXfms.  The
// native provider hand-filled mJointPos[kCoordCamera] only (FillSkeleton +
// FinalizeSkeletonFrame), so every limb coordinate of a live native skeleton
// was a stale zero -- the target, polled by the image's Poll from
// CharCameraInput, had real ones.
// ---------------------------------------------------------------------------
#include "platform/Skeleton_Native.h"
#include "math/Mtx.h"

namespace {

// FillDummySkeleton's standing pose, one arm raised so the limbs are not
// degenerate.
void StandingPose(NativeSkeletonProvider::PersonData &p) {
    static const float kPose[kNumJoints][3] = {
        { 0.00f, 0.90f, 2.0f },  { 0.00f, 1.10f, 2.0f },  { 0.00f, 1.40f, 2.0f },
        { 0.00f, 1.60f, 2.0f },  { -0.20f, 1.40f, 2.0f }, { -0.30f, 1.60f, 1.9f },
        { -0.32f, 1.85f, 1.9f }, { -0.33f, 1.92f, 1.9f }, { 0.20f, 1.40f, 2.0f },
        { 0.25f, 1.15f, 2.0f },  { 0.22f, 0.90f, 2.0f },  { 0.22f, 0.85f, 2.0f },
        { -0.12f, 0.85f, 2.0f }, { -0.12f, 0.45f, 2.1f }, { -0.12f, 0.05f, 2.0f },
        { 0.12f, 0.85f, 2.0f },  { 0.12f, 0.45f, 2.0f },  { 0.12f, 0.05f, 2.0f },
        { -0.12f, 0.00f, 1.9f }, { 0.12f, 0.00f, 1.9f },
    };
    p.trackId = 5;
    p.valid = true;
    for (int j = 0; j < kNumJoints; j++) {
        p.joints[j] = Vector3(kPose[j][0], kPose[j][1], kPose[j][2]);
        p.confidence[j] = kConfidenceTracked;
    }
}

TEST(NativeSkeletonPollTest, LiveSkeletonCarriesTheLimbCoordinateSystems) {
    NativeSkeletonProvider::PersonData person;
    StandingPose(person);

    // native: the provider's fill + finalize, as GestureMgr_NativePoll does it
    static NativeSkeletonProvider helper;
    Skeleton native;
    helper.FillSkeleton(native, person);
    NativeSkeletonProvider::FinalizeSkeletonFrame(native, 2, 33);

    // image: Skeleton::Poll on the equivalent SkeletonFrame
    static SkeletonFrame frame; // 0x11c8 bytes
    memset(&frame, 0, sizeof(frame));
    frame.mElapsedMs = 33;
    frame.mFloorNormal.Set(0, 1, 0);
    SkeletonData &d = frame.mSkeletonDatas[2];
    d.mTracking = kSkeletonTracked;
    d.mTrackingID = person.trackId;
    for (int j = 0; j < kNumJoints; j++) {
        d.mJointPositions[j].Set(person.joints[j].x, person.joints[j].y, person.joints[j].z);
        d.mRawPositions[j] = d.mJointPositions[j];
        d.mJointTrackingState[j] = kConfidenceTracked;
    }
    d.mHipCenter = d.mJointPositions[kJointHipCenter];
    Skeleton image;
    image.Poll(2, frame);

    ASSERT_TRUE(native.IsTracked());
    for (int cs = 0; cs < kNumCoordSys; cs++) {
        for (int j = 0; j < kNumJoints; j++) {
            Vector3 a, b;
            native.JointPos((SkeletonCoordSys)cs, (SkeletonJoint)j, a);
            image.JointPos((SkeletonCoordSys)cs, (SkeletonJoint)j, b);
            EXPECT_NEAR(a.x, b.x, 1e-5f) << "cs=" << cs << " joint=" << j;
            EXPECT_NEAR(a.y, b.y, 1e-5f) << "cs=" << cs << " joint=" << j;
            EXPECT_NEAR(a.z, b.z, 1e-5f) << "cs=" << cs << " joint=" << j;
        }
    }
    // and the arm's limb frame is not degenerate
    Vector3 hand;
    native.NormPos(kCoordLeftArm, kJointHandLeft, hand);
    EXPECT_GT(fabsf(hand.x) + fabsf(hand.y) + fabsf(hand.z), 0.1f);
    EXPECT_EQ(native.TrackingID(), 5);
    EXPECT_EQ(native.SkeletonIndex(), 2);
    EXPECT_EQ(native.ElapsedMs(), 33);
}

} // namespace

// ---------------------------------------------------------------------------
// AsyncDetectorRatingTest: perform and dance battle rate every move from
// MoveAsyncDetector::MoveRatingFrac (`last_detector_result`).  Native returned
// 0 there unless SkeletonUpdate::HasInstance() -- never true natively -- so
// every move rated at the bottom whatever the player did, and no battle
// fatality could start.  The image has no such guard: an active detector is
// polled and its fraction reported.
// ---------------------------------------------------------------------------
#define private public
#define protected public
#include "hamobj/MoveDetector.h"
#include "hamobj/HamMove.h"
#undef private
#undef protected
#include "obj/Task.h"

#include <new>
#include <set>

namespace {

TEST(AsyncDetectorRatingTest, AnActiveDetectorsLastFractionIsReported) {
    // A HamMove only as far as MoveRatingFrac reads it: Scored(), and its
    // address as the detector key.
    void *moveMem = ::operator new(sizeof(HamMove));
    memset(moveMem, 0, sizeof(HamMove));
    HamMove *move = static_cast<HamMove *>(moveMem);
    move->mScored = true;

    void *detMem = ::operator new(sizeof(MoveDetector));
    memset(detMem, 0, sizeof(MoveDetector));
    MoveDetector *det = static_cast<MoveDetector *>(detMem);
    det->mMove = move;
    det->mActive = true;
    // Already polled for this measure and beat, so Poll() changes nothing and
    // the detector's own last fraction is what gets reported.
    det->mDetectFrameOffset = TheTaskMgr.CurrentBeat();
    det->mLastDetectFrameIdx = TheTaskMgr.CurrentMeasure();
    det->mLastDetectFracs[0] = 0.8f;
    det->mLastDetectFracs[1] = 0.3f;

    void *asyncMem = ::operator new(sizeof(MoveAsyncDetector));
    memset(asyncMem, 0, sizeof(MoveAsyncDetector));
    MoveAsyncDetector *async = static_cast<MoveAsyncDetector *>(asyncMem);
    // MoveDir::MoveIdx / MoveBeat only read TheTaskMgr; no MoveDir state is
    // touched on this path.
    async->mDir = reinterpret_cast<MoveDir *>(0x1000);
    new (&async->mDetectors) std::vector<MoveDetector *>();
    new (&async->mActiveDetectors) std::set<MoveDetector *>();
    async->mDetectors.push_back(det);

    EXPECT_FLOAT_EQ(async->MoveRatingFrac(0, MoveAsyncDetector::kRatingLast, move), 0.8f);
    EXPECT_FLOAT_EQ(async->MoveRatingFrac(1, MoveAsyncDetector::kRatingLast, move), 0.3f);

    async->mDetectors.~vector();
    async->mActiveDetectors.~set();
    ::operator delete(asyncMem);
    ::operator delete(detMem);
    ::operator delete(moveMem);
}

} // namespace
