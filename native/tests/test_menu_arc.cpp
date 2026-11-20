// Menu gestures on a webcam (np-menu-gestures): the arc detector's first node,
// with no game, GPU or socket.
//
//   MenuArcStartTest   ArcDetector::TryToStartSwipe puts the first node of a
//                      path down only on a frame whose wrist AND elbow are
//                      Tracked.  DC3_MENU_ARC_START=inferred lets a frame whose
//                      wrist or elbow the CAMERA scored Inferred start one too
//                      -- never one the camera lost, although the pose estimate
//                      hands the detectors that joint as Inferred as well.
//                      `tracked` is the image's rule, unchanged.  All but
//                      TheRule drive the image's own ArcDetector::Update with a
//                      Skeleton built by the image's Skeleton::Poll, so they
//                      fail when the hook in TryToStartSwipe is gone.
//                      Sabotaged, one defect at a time (each failed the
//                      tests named; PoseSensorArcStartTest is the same switch
//                      at the sensor, test_native_pose_socket.cpp):
//                        - ArcStartAllowedFor ignores `onInferred`
//                              -> TheRule, AnInferredWristStartsAPath...,
//                                 PoseSensorArcStartTest
//                        - ArcStartAllowedFor accepts NotTracked
//                              -> TheRule, ANotTrackedWrist...,
//                                 AJointTheEstimatePlaced..., PoseSensorArcStartTest
//                        - the hook's answer dropped in TryToStartSwipe
//                              -> AnInferredWristStartsAPath...
//                        - ArcStartAllowed says yes to whatever the image refused
//                              -> AnInferredWrist..., ANotTrackedWrist...,
//                                 AJointTheEstimatePlaced..., PoseSensorArcStartTest
//                        - a slot the camera lost keeps its last scores
//                              -> AJointTheEstimatePlaced...
//                        - ArcStartAllowed reads the next slot's scores
//                              -> AnInferredWrist..., AJointTheEstimatePlaced...,
//                                 PoseSensorArcStartTest
//                        - GestureMgr_Native notes the scores AFTER the pose
//                          estimate relabelled them
//                              -> PoseSensorArcStartTest only
//                        - Dc3SetMenuArcStart takes any word for `tracked`
//                              -> TheSwitchTakesItsTwoWordsAndNoOther

#include <gtest/gtest.h>

#include "platform/MenuGesture_Native.h"
#include "platform/NativeSettings.h" // Dc3SetMenuArcStart

#include "gesture/ArcDetector.h"
#include "gesture/BaseSkeleton.h"
#include "gesture/Skeleton.h"

#include <cstring>

namespace {

const int kSlot = 2;

// A person standing 2 m from the sensor with the right forearm held out, as
// SkeletonUpdate hands one to a list's filters: the image's Skeleton::Poll on
// a sensor frame.  `wristConf` / `elbowConf` are what the detectors see.
void Stand(Skeleton &skel, int wristConf, int elbowConf, float wristX = 0.45f) {
    static const float kPose[kNumJoints][3] = {
        { 0.00f, 0.90f, 2.0f },  { 0.00f, 1.10f, 2.0f },  { 0.00f, 1.40f, 2.0f },
        { 0.00f, 1.60f, 2.0f },  { -0.20f, 1.40f, 2.0f }, { -0.25f, 1.15f, 2.0f },
        { -0.22f, 0.90f, 2.0f }, { -0.22f, 0.85f, 2.0f }, { 0.20f, 1.40f, 2.0f },
        { 0.30f, 1.15f, 1.9f },  { 0.45f, 1.20f, 1.7f },  { 0.47f, 1.22f, 1.65f },
        { -0.12f, 0.85f, 2.0f }, { -0.12f, 0.45f, 2.0f }, { -0.12f, 0.05f, 2.0f },
        { 0.12f, 0.85f, 2.0f },  { 0.12f, 0.45f, 2.0f },  { 0.12f, 0.05f, 2.0f },
        { -0.12f, 0.00f, 1.9f }, { 0.12f, 0.00f, 1.9f },
    };
    static SkeletonFrame frame; // 0x11c8 bytes
    memset(&frame, 0, sizeof(frame));
    frame.mElapsedMs = 33;
    frame.mFloorNormal.Set(0, 1, 0);
    SkeletonData &d = frame.mSkeletonDatas[kSlot];
    d.mTracking = kSkeletonTracked;
    d.mTrackingID = 7;
    for (int j = 0; j < kNumJoints; j++) {
        d.mJointPositions[j].Set(kPose[j][0], kPose[j][1], kPose[j][2]);
        d.mJointTrackingState[j] = kConfidenceTracked;
    }
    d.mJointPositions[kJointWristRight].x = wristX;
    for (int j = 0; j < kNumJoints; j++)
        d.mRawPositions[j] = d.mJointPositions[j];
    d.mJointTrackingState[kJointWristRight] = wristConf;
    d.mJointTrackingState[kJointElbowRight] = elbowConf;
    d.mHipCenter = d.mJointPositions[kJointHipCenter];
    skel.Poll(kSlot, frame);
}

// What the camera itself said of the right arm in slot `slot` (everything
// else Tracked), as GestureMgr_Native notes it before the pose estimate.
void CameraSaw(int slot, int wrist, int elbow) {
    int seen[kNumJoints];
    for (int j = 0; j < kNumJoints; j++)
        seen[j] = kConfidenceTracked;
    seen[kJointWristRight] = wrist;
    seen[kJointElbowRight] = elbow;
    Dc3MenuGesture::NoteSensorConfidence(slot, seen);
}

// The right hand's list: DirectionGestureFilterSingleUser(kSkeletonRight,
// kSkeletonLeft, 0.1, ...) -> mArcDetector.Initialize(kSkeletonLeft, wrist, elbow, 0.1).
struct RightArm {
    ArcDetector arc;
    bool was;
    RightArm() : was(Dc3MenuGesture::ArcStartsOnInferred()) {
        arc.Initialize(kSkeletonLeft, kJointWristRight, kJointElbowRight, 0.1f);
    }
    ~RightArm() {
        Dc3MenuGesture::SetArcStartsOnInferred(was);
        for (int s = 0; s < 6; s++)
            Dc3MenuGesture::NoteSensorConfidence(s, nullptr);
    }
    // One sensor frame: the camera's score of the wrist and elbow, and what
    // the detectors are handed for them (the same, unless the pose estimate
    // placed a joint the camera lost: then Inferred).
    int NodesAfter(int wristSeen, int elbowSeen, int wristConf, int elbowConf,
                   float wristX = 0.45f) {
        CameraSaw(kSlot, wristSeen, elbowSeen);
        Skeleton skel;
        Stand(skel, wristConf, elbowConf, wristX);
        EXPECT_TRUE(skel.IsTracked());
        EXPECT_EQ(skel.SkeletonIndex(), kSlot);
        arc.Update(skel, 33);
        return arc.NumJointsInPath();
    }
    int NodesAfter(int wrist, int elbow) { return NodesAfter(wrist, elbow, wrist, elbow); }
};

const int N = kConfidenceNotTracked, I = kConfidenceInferred, T = kConfidenceTracked;

} // namespace

TEST(MenuArcStartTest, TheRule) {
    using Dc3MenuGesture::ArcStartAllowedFor;
    // the image's answer stands in both modes
    EXPECT_TRUE(ArcStartAllowedFor(false, true, T, T));
    EXPECT_TRUE(ArcStartAllowedFor(true, true, T, T));
    // tracked (the image's rule): what the image refused stays refused
    EXPECT_FALSE(ArcStartAllowedFor(false, false, I, T));
    EXPECT_FALSE(ArcStartAllowedFor(false, false, T, I));
    EXPECT_FALSE(ArcStartAllowedFor(false, false, I, I));
    EXPECT_FALSE(ArcStartAllowedFor(false, false, N, T));
    // inferred: Inferred joints start a path, a NotTracked one never does
    EXPECT_TRUE(ArcStartAllowedFor(true, false, I, T));
    EXPECT_TRUE(ArcStartAllowedFor(true, false, T, I));
    EXPECT_TRUE(ArcStartAllowedFor(true, false, I, I));
    EXPECT_FALSE(ArcStartAllowedFor(true, false, N, T));
    EXPECT_FALSE(ArcStartAllowedFor(true, false, T, N));
    EXPECT_FALSE(ArcStartAllowedFor(true, false, N, N));
}

TEST(MenuArcStartTest, AnInferredWristStartsAPathOnlyWhenAllowed) {
    {
        RightArm r;
        Dc3MenuGesture::SetArcStartsOnInferred(false);
        EXPECT_EQ(r.NodesAfter(I, T), 0);
        EXPECT_EQ(r.NodesAfter(T, I), 0);
        // the image's own start, to show the rig can start one at all
        EXPECT_EQ(r.NodesAfter(T, T), 1);
    }
    {
        RightArm r;
        Dc3MenuGesture::SetArcStartsOnInferred(true);
        EXPECT_EQ(r.NodesAfter(I, T), 1);
    }
    {
        RightArm r;
        Dc3MenuGesture::SetArcStartsOnInferred(true);
        EXPECT_EQ(r.NodesAfter(T, I), 1);
    }
}

TEST(MenuArcStartTest, ANotTrackedWristNeverStartsAPath) {
    for (int mode = 0; mode < 2; mode++) {
        RightArm r;
        Dc3MenuGesture::SetArcStartsOnInferred(mode == 1);
        EXPECT_EQ(r.NodesAfter(N, T), 0) << "mode " << mode;
        EXPECT_EQ(r.NodesAfter(T, N), 0) << "mode " << mode;
    }
}

// The pose estimate (DC3_POSE_ESTIMATE, on by default) puts a joint the
// camera lost where it guesses and hands it to the detectors as Inferred.  On
// a recording that started a path on an arm the camera never saw; the switch
// reads the camera's own score.
TEST(MenuArcStartTest, AJointTheEstimatePlacedNeverStartsAPath) {
    for (int mode = 0; mode < 2; mode++) {
        RightArm r;
        Dc3MenuGesture::SetArcStartsOnInferred(mode == 1);
        EXPECT_EQ(r.NodesAfter(N, T, I, T), 0) << "mode " << mode;
        EXPECT_EQ(r.NodesAfter(T, N, T, I), 0) << "mode " << mode;
        EXPECT_EQ(r.NodesAfter(N, N, I, I), 0) << "mode " << mode;
    }
    // ... and nor does a body the camera does not see this frame (a slot held
    // through a dropout: every joint Inferred, nothing noted for the slot)
    RightArm r;
    Dc3MenuGesture::SetArcStartsOnInferred(true);
    Skeleton skel;
    Stand(skel, I, I);
    CameraSaw(kSlot, I, I); // the frame before, it did
    Dc3MenuGesture::NoteSensorConfidence(kSlot, nullptr);
    r.arc.Update(skel, 33);
    EXPECT_EQ(r.arc.NumJointsInPath(), 0);
    // another slot's camera scores are not this one's
    CameraSaw(kSlot + 1, I, I);
    r.arc.Update(skel, 33);
    EXPECT_EQ(r.arc.NumJointsInPath(), 0);
}

// Once a path has its first node the image takes every later frame whatever
// its confidence: the switch is about the first node only.
TEST(MenuArcStartTest, LaterNodesNeverAskedForConfidence) {
    RightArm r;
    Dc3MenuGesture::SetArcStartsOnInferred(false);
    ASSERT_EQ(r.NodesAfter(T, T, T, T, 0.45f), 1);
    // the forearm 3 cm across the body, the wrist Inferred: a second node
    EXPECT_EQ(r.NodesAfter(I, T, I, T, 0.42f), 2);
}

// {native_settings set_menu_arc_start WORD} and {native_settings
// menu_arc_start} (src/App.cpp) are these two functions.
TEST(MenuArcStartTest, TheSwitchTakesItsTwoWordsAndNoOther) {
    bool was = Dc3MenuGesture::ArcStartsOnInferred();
    EXPECT_TRUE(Dc3SetMenuArcStart("inferred"));
    EXPECT_TRUE(Dc3MenuGesture::ArcStartsOnInferred());
    EXPECT_STREQ(Dc3MenuArcStartName(), "inferred");
    EXPECT_FALSE(Dc3SetMenuArcStart("both"));
    EXPECT_FALSE(Dc3SetMenuArcStart(""));
    EXPECT_FALSE(Dc3SetMenuArcStart(nullptr));
    EXPECT_TRUE(Dc3MenuGesture::ArcStartsOnInferred()) << "a wrong word changes nothing";
    EXPECT_TRUE(Dc3SetMenuArcStart("tracked"));
    EXPECT_FALSE(Dc3MenuGesture::ArcStartsOnInferred());
    EXPECT_STREQ(Dc3MenuArcStartName(), "tracked");
    Dc3MenuGesture::SetArcStartsOnInferred(was);
}
