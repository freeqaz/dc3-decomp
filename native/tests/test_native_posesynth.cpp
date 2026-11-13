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

// (NoSkeletonIdentifierMeansNoEnrolledPad, which pinned GetPadNum's native
// `if (!TheSkeletonIdentifier) return -1;` arm, was retired by w25-n1: since
// w23-sk ShellInput::Init builds the SkeletonIdentifier natively as the image
// does, so the arm was dead and GetPadNum is the image body on both builds.
// The "nobody enrolled -> -1" answer now comes from the identifier itself:
// the typed NuiIdentityGetEnrollmentInformation stand-in in
// xbox_link_stubs.cpp reports every slot empty, UpdateEnrolledPlayers sets
// mPadNum = -1, and the >= 4 arm below is pinned by the control.)

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
// CharCameraInput, had real ones.  The sensor now hands SkeletonUpdate a
// SkeletonFrame (FillSkeletonData) and the image's Poll does the rest; this
// pins that the slot data it writes is the NUI-shaped data Poll expects.
// ---------------------------------------------------------------------------
#include "platform/Skeleton_Native.h"
#include "math/Mtx.h"
#include <cmath>
#include <vector>

namespace {

// FillDummySkeletonData's standing pose, one arm raised so the limbs are not
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

    // native: the sensor's slot data (GestureMgr_Native.cpp BuildSensorFrame),
    // then the image's Skeleton::Poll, as SkeletonUpdate::UpdateCallbacks runs it
    static SkeletonFrame nativeFrame; // 0x11c8 bytes
    memset(&nativeFrame, 0, sizeof(nativeFrame));
    nativeFrame.mElapsedMs = 33;
    nativeFrame.mFloorNormal.Set(0, 1, 0);
    NativeSkeletonProvider::FillSkeletonData(nativeFrame.mSkeletonDatas[2], person, 2);
    Skeleton native;
    native.Poll(2, nativeFrame);

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

// ---------------------------------------------------------------------------
// NativeSensorNoiseFloorTest: a bit-for-bit still skeleton on the pose socket
// crashed gameplay (SIGSEGV ~130 s into Better Off Alone: RhythmBattlePlayer::
// Poll -> RndPropAnim::SetFrame -> SymbolKeys::SetFrame -> DataArray::Node).
// RhythmDetector's AnalyzeData z-scores each joint axis's speed over a
// 10-sample window and divides by the window's variance unguarded; a joint axis
// that never moves gives 0 * (1/0) = NaN, which reaches phrase_meter.anim's
// SymbolKeys::SetFrame as the frame, and AtFrame(NaN) reads a key one past the
// end.  The image never sees it because a Kinect never repeats a joint bit for
// bit; the native sensor did (a still feeder, and -- the webcam case -- the
// low-confidence hold freezing out-of-frame legs at their last good value).
// This pins the sensor contract the image relies on, measured where the image
// reads it: Skeleton::Poll's kCoordCamera JointPos, as RhythmDetector::AddFrame
// does, through the same mean/variance AnalyzeData computes.
// ---------------------------------------------------------------------------

// RhythmDetector.cpp's anonymous-namespace Mean / Variance, same clamping.
float WindowMean(const std::vector<float> &v, int start, int end) {
    end = (int)v.size() < end ? (int)v.size() : end;
    start = start > 0 ? start : 0;
    float sum = 0.0f;
    for (int i = start; i < end; i++)
        sum += v[i];
    return end - start ? sum / (end - start) : 0.0f;
}
float WindowVariance(const std::vector<float> &v, float mean, int start, int end) {
    end = (int)v.size() < end ? (int)v.size() : end;
    start = start > 0 ? start : 0;
    float sum = 0.0f;
    for (int i = start; i < end; i++)
        sum += (v[i] - mean) * (v[i] - mean);
    return end - start ? sum / (end - start) : 0.0f;
}

// Feed `frames` sensor frames through FillSkeletonData (or the dummy) and the
// image's Skeleton::Poll; record every joint's camera-space position.
template <typename MakePerson>
std::vector<std::vector<Vector3> > PollFrames(int slot, int frames, bool dummy, MakePerson make) {
    NativeSkeletonProvider::ResetJointHold(slot);
    static SkeletonFrame sensorFrame; // 0x11c8 bytes
    Skeleton skel;
    std::vector<std::vector<Vector3> > out;
    for (int f = 0; f < frames; f++) {
        memset(&sensorFrame, 0, sizeof(sensorFrame));
        sensorFrame.mElapsedMs = 33 * (f + 1);
        sensorFrame.mFloorNormal.Set(0, 1, 0);
        if (dummy) {
            NativeSkeletonProvider::FillDummySkeletonData(sensorFrame.mSkeletonDatas[slot]);
        } else {
            NativeSkeletonProvider::PersonData person;
            make(f, person);
            NativeSkeletonProvider::FillSkeletonData(sensorFrame.mSkeletonDatas[slot], person, slot);
        }
        skel.Poll(slot, sensorFrame);
        std::vector<Vector3> joints(kNumJoints);
        for (int j = 0; j < kNumJoints; j++)
            skel.JointPos(kCoordCamera, (SkeletonJoint)j, joints[j]);
        out.push_back(joints);
    }
    return out;
}

// AnalyzeData's first z-score window over every joint axis: |speed| samples
// ~0.1 s apart (the ring-buffer lag AddFrame picks, 3 frames at 30 Hz), then
// (raw[i] - mean) * (1 / var).  Returns the number of joint axes that would
// have gone NaN.
int CountZeroVarianceAxes(const std::vector<std::vector<Vector3> > &pos, const char *what) {
    const int kLag = 3, kWindow = 10;
    int bad = 0;
    for (int j = 0; j < kNumJoints; j++) {
        for (int c = 0; c < 3; c++) {
            std::vector<float> raw;
            for (int f = kLag; f < (int)pos.size() && (int)raw.size() < kWindow; f++)
                raw.push_back(fabsf((pos[f][j][c] - pos[f - kLag][j][c]) / 0.1f));
            float mean = WindowMean(raw, 0, kWindow);
            float var = WindowVariance(raw, mean, 0, kWindow);
            float z = (raw[0] - mean) * (1.0f / var);
            if (!(var > 0.0f) || !std::isfinite(z)) {
                bad++;
                ADD_FAILURE() << what << ": joint " << j << " axis " << c
                              << " has zero speed variance (var=" << var << ", z=" << z
                              << ") -- AnalyzeData would divide 0 by 0";
            }
        }
    }
    return bad;
}

TEST(NativeSensorNoiseFloorTest, AStillSkeletonNeverHasZeroSpeedVariance) {
    // The socket repro: the same pose, every joint tracked, every frame.
    std::vector<std::vector<Vector3> > pos = PollFrames(
        1, 16, false,
        [](int, NativeSkeletonProvider::PersonData &p) { StandingPose(p); }
    );
    EXPECT_EQ(CountZeroVarianceAxes(pos, "still tracked skeleton"), 0);
}

TEST(NativeSensorNoiseFloorTest, HeldLowConfidenceJointsNeverHaveZeroSpeedVariance) {
    // The webcam case: the upper body moves, the legs drop under 0.3
    // confidence after the first frame (out of frame) and their reported
    // positions are garbage -- the hold replaces them with the last good one.
    std::vector<std::vector<Vector3> > pos = PollFrames(
        3, 16, false,
        [](int f, NativeSkeletonProvider::PersonData &p) {
            StandingPose(p);
            for (int j = kJointHipCenter; j <= kJointHandRight; j++) {
                p.joints[j].x += 0.05f * sinf(0.7f * f + j);
                p.joints[j].y += 0.03f * sinf(1.1f * f + j);
                p.joints[j].z += 0.04f * sinf(0.9f * f + j);
            }
            if (f > 0) {
                for (int j = kJointHipLeft; j < kNumJoints; j++) {
                    p.confidence[j] = kConfidenceNotTracked;
                    p.joints[j] = Vector3(9.0f + f, -9.0f, 0.0f);
                }
            }
        }
    );
    // the hold must still be holding (the garbage never reaches the image)...
    EXPECT_NEAR(pos.back()[kJointKneeLeft].y, 0.45f, 1e-3f);
    // ...without freezing the joint bit for bit.
    EXPECT_EQ(CountZeroVarianceAxes(pos, "held legs"), 0);
}

TEST(NativeSensorNoiseFloorTest, TheDummySkeletonNeverHasZeroSpeedVariance) {
    std::vector<std::vector<Vector3> > pos = PollFrames(
        0, 16, true, [](int, NativeSkeletonProvider::PersonData &) {}
    );
    EXPECT_EQ(CountZeroVarianceAxes(pos, "dummy skeleton"), 0);
}

// ---------------------------------------------------------------------------
// NativeSkeletonProviderPublishTest: PublishFrame is the provider's one entry
// point for every transport (the desktop socket reader, and on web
// dc3_pose_push in PoseInput_Web.cpp). BuildSensorFrame integrates a frame only
// when FrameId changes, and takes the frame interval from Timestamp, so both
// must arrive exactly as published, latch only on Poll, and a later frame with
// fewer people must not leave the departed person's data behind.
// ---------------------------------------------------------------------------
TEST(NativeSkeletonProviderPublishTest, PollLatchesExactlyWhatWasPublished) {
    NativeSkeletonProvider provider;
    NativeSkeletonProvider::PersonData persons[2];
    StandingPose(persons[0]);
    StandingPose(persons[1]);
    persons[0].trackId = 4;
    persons[1].trackId = 9;
    persons[1].joints[kJointHandRight] = Vector3(0.5f, 1.25f, 2.5f);
    persons[0].valid = persons[1].valid = true;

    provider.PublishFrame(persons, 2, 7, 1.5);
    EXPECT_EQ(provider.NumPersons(), 0) << "nothing is visible before Poll";
    EXPECT_EQ(provider.FrameId(), 0u);

    provider.Poll();
    ASSERT_EQ(provider.NumPersons(), 2);
    EXPECT_EQ(provider.FrameId(), 7u);
    EXPECT_DOUBLE_EQ(provider.Timestamp(), 1.5);
    EXPECT_EQ(provider.GetPerson(1).trackId, 9);
    EXPECT_FLOAT_EQ(provider.GetPerson(1).joints[kJointHandRight].y, 1.25f);
    EXPECT_EQ(provider.FindByTrackId(4), 0);

    // An empty frame (nobody in view) is a real frame: it advances the id and
    // clears the slots rather than repeating the last people.
    provider.PublishFrame(persons, 0, 8, 1.533);
    provider.Poll();
    EXPECT_EQ(provider.NumPersons(), 0);
    EXPECT_EQ(provider.FrameId(), 8u);
    EXPECT_FALSE(provider.GetPerson(0).valid);
    EXPECT_FALSE(provider.GetPerson(1).valid);
    EXPECT_EQ(provider.FindByTrackId(4), -1);
}

TEST(NativeSkeletonProviderPublishTest, CountIsClampedToCapacity) {
    NativeSkeletonProvider provider;
    const int kMax = NativeSkeletonProvider::kMaxPersons; // EXPECT_EQ takes a reference
    std::vector<NativeSkeletonProvider::PersonData> many(kMax + 3);
    for (auto &p : many) {
        StandingPose(p);
        p.valid = true;
    }
    provider.PublishFrame(many.data(), (int)many.size(), 1, 0.0);
    provider.Poll();
    EXPECT_EQ(provider.NumPersons(), kMax);
    provider.PublishFrame(many.data(), -5, 2, 0.0);
    provider.Poll();
    EXPECT_EQ(provider.NumPersons(), 0);
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

// ---------------------------------------------------------------------------
// NativeDummyPlayersTest (np-twoplayer, PLAN G-1): with no pose provider the
// sensor shows standing stand-ins.  One stand-in makes every two-player mode a
// dead end without a camera: Game::CheckForSkeletonLoss pauses as soon as
// player 2 is not playing and the pause screen's Continue stays disabled until
// two players are bound.  In exactly the modes CheckForSkeletonLoss counts two
// for (requires_2_players / is_in_party_mode) a second stand-in appears; never
// otherwise, because a phantom second skeleton gets bound as player 2 and
// plays every solo song (the HamPlayerData::IsPlaying pin's old failure).
// ---------------------------------------------------------------------------
TEST(NativeDummyPlayersTest, SecondStandInOnlyInTwoPlayerModes) {
    EXPECT_EQ(NativeDummyPlayerCount(false, nullptr), 1) << "solo mode, default policy";
    EXPECT_EQ(NativeDummyPlayerCount(true, nullptr), 2) << "two-player mode, default policy";
    EXPECT_EQ(NativeDummyPlayerCount(false, ""), 1);
    EXPECT_EQ(NativeDummyPlayerCount(true, "auto"), 2);
    // DC3_DUMMY_PLAYERS=1: the 360's one-person pause, reproducible without a camera.
    EXPECT_EQ(NativeDummyPlayerCount(true, "1"), 1);
    EXPECT_EQ(NativeDummyPlayerCount(false, "2"), 2);
}

TEST(NativeDummyPlayersTest, TheTwoStandInsAreDistinctPeopleOnOppositeSides) {
    SkeletonData a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    NativeSkeletonProvider::FillDummySkeletonData(a, 0);
    NativeSkeletonProvider::FillDummySkeletonData(b, 1);
    EXPECT_EQ(a.mTracking, kSkeletonTracked);
    EXPECT_EQ(b.mTracking, kSkeletonTracked);
    // Dummy 0 is the solo stand-in, unchanged: id 1, centred.
    EXPECT_EQ(a.mTrackingID, 1);
    EXPECT_NEAR(a.mHipCenter.x, 0.0f, 1e-3f);
    // Dummy 1 is another person (SkeletonChooser binds by tracking id) on the
    // other side of SkeletonChooser's +-0.15 m side threshold.
    EXPECT_EQ(b.mTrackingID, 2);
    EXPECT_GT(b.mHipCenter.x, 0.15f);
    EXPECT_NEAR(b.mHipCenter.y, a.mHipCenter.y, 1e-3f);
}

} // namespace
