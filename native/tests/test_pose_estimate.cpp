// DC3_POSE_ESTIMATE (native/src/platform/PoseEstimate_Native.h, np-cutoff):
// completing the joints a webcam cannot see.  The math only -- no game, no
// GPU, no socket.  The end-to-end measurement (cut-off fixtures and the
// owner's GPD session through the real scorer) is scripts/pose_cutoff/ and
// POSE_INPUT.md "Cut-off joints".
//
//   PoseEstimateMathTest.TwoBone*      the knee solve keeps both bone lengths,
//                                      bends towards the bend direction, and
//                                      straightens out of reach.
//   PoseEstimateTest.LearnsBoneLengths a cut leg is rebuilt with the thigh
//                                      the player showed, not the default
//                                      proportion.  Sabotaged: learning off
//                                      (Learn() a no-op) -> this test and
//                                      FloorHoldsAnkles fail.
//   PoseEstimateTest.AWildTrackedJointTeachesNothing
//                                      300 frames of legs "tracked" 3 m below
//                                      the hips leave the learned bones
//                                      alone.  Sabotaged: the plausibility
//                                      gate off -> fails.
//   PoseEstimateTest.FloorHoldsAnkles  hips drop 20 cm with the legs cut:
//                                      the ankles stay on the floor and the
//                                      knees bend forward.  Sabotaged: floor
//                                      taken hip-relative (floorUp = hip -
//                                      stand each frame) -> fails.
//   PoseEstimateTest.HoldDecays        a joint that was just seen is held
//                                      where it was (relative to the body);
//                                      2 s later it is the estimate.
//                                      Sabotaged: no decay (w = 1) -> this
//                                      test and FloorHoldsAnkles fail.
//   PoseEstimateTest.APlausibleGuessIsKept
//                                      a knee guess within 0.5-1.6x the
//                                      learned thigh is kept as it is; a
//                                      guess 3 m off is ignored for the
//                                      solve.  Sabotaged: Plausible()
//                                      always false -> fails.
//   PoseEstimateTest.MarksInferred     every written joint reads Inferred,
//                                      seen joints are not touched, `off`
//                                      writes nothing, `tracker` only
//                                      relabels.  Sabotaged: estimated joints
//                                      left NotTracked -> fails.

#include <gtest/gtest.h>

#include "platform/PoseEstimate_Native.h"

#include <cmath>

using namespace Dc3PoseEstimate;

namespace {

// A player facing the camera (camera space: +y up, +z away, her left = -x),
// hip centre at (x, hipY, 2.5), legs straight down to ankles at y = 0.1 when
// hipY = 0.9.  thigh = |hip -> knee|.
Person MakeBody(float hipY = 0.9f, float thigh = 0.40f, float x = 0.0f) {
    Person p;
    p.trackId = 1;
    p.valid = true;
    const float z = 2.5f;
    auto set = [&](int j, float jx, float jy, float jz) { p.joints[j].Set(x + jx, jy, jz); };
    set(kJointHipCenter, 0, hipY, z);
    set(kJointSpine, 0, hipY + 0.07f, z);
    set(kJointShoulderCenter, 0, hipY + 0.38f, z);
    set(kJointHead, 0, hipY + 0.55f, z);
    set(kJointShoulderLeft, -0.16f, hipY + 0.38f, z);
    set(kJointShoulderRight, 0.16f, hipY + 0.38f, z);
    set(kJointElbowLeft, -0.20f, hipY + 0.14f, z);
    set(kJointElbowRight, 0.20f, hipY + 0.14f, z);
    set(kJointWristLeft, -0.22f, hipY - 0.09f, z);
    set(kJointWristRight, 0.22f, hipY - 0.09f, z);
    set(kJointHandLeft, -0.22f, hipY - 0.16f, z);
    set(kJointHandRight, 0.22f, hipY - 0.16f, z);
    set(kJointHipLeft, -0.10f, hipY, z);
    set(kJointHipRight, 0.10f, hipY, z);
    set(kJointKneeLeft, -0.10f, hipY - thigh, z);
    set(kJointKneeRight, 0.10f, hipY - thigh, z);
    set(kJointAnkleLeft, -0.10f, 0.1f, z);
    set(kJointAnkleRight, 0.10f, 0.1f, z);
    set(kJointFootLeft, -0.11f, 0.04f, z - 0.05f);
    set(kJointFootRight, 0.11f, 0.04f, z - 0.05f);
    for (int j = 0; j < kNumJoints; j++)
        p.confidence[j] = kConfidenceTracked;
    return p;
}

// The legs below the frame: knees, ankles and feet NotTracked, at a guess
// the tracker made (far below the hips, so a test cannot pass by keeping it).
void CutLegs(Person &p) {
    const int legs[] = {kJointKneeLeft, kJointKneeRight, kJointAnkleLeft,
                        kJointAnkleRight, kJointFootLeft, kJointFootRight};
    for (int j : legs) {
        p.confidence[j] = kConfidenceNotTracked;
        p.joints[j].Set(p.joints[j].x, -3.0f, 2.5f);
    }
}

float Dist(const Vector3 &a, const Vector3 &b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void Teach(Estimator &e, const Person &p, int frames) {
    for (int i = 0; i < frames; i++) {
        Person q = p;
        e.Apply(0, q, 1.0f / 30.0f, true);
    }
}

} // namespace

TEST(PoseEstimateMathTest, TwoBoneKeepsLengthsAndBendsForward) {
    Vector3 root(0, 1, 2), target(0, 0.3f, 2), bend(0, 0, -1), mid, end;
    EXPECT_TRUE(TwoBone(root, target, 0.4f, 0.4f, bend, mid, end));
    EXPECT_NEAR(Dist(root, mid), 0.4f, 1e-4f);
    EXPECT_NEAR(Dist(mid, end), 0.4f, 1e-4f);
    EXPECT_NEAR(Dist(end, target), 0.0f, 1e-4f);
    EXPECT_LT(mid.z, 2.0f - 0.1f) << "the knee bends towards the bend direction (-z)";
}

TEST(PoseEstimateMathTest, TwoBoneStraightensOutOfReach) {
    Vector3 root(0, 1, 2), target(0, -1, 2), bend(0, 0, -1), mid, end;
    EXPECT_FALSE(TwoBone(root, target, 0.4f, 0.4f, bend, mid, end));
    EXPECT_NEAR(Dist(root, end), 0.8f, 1e-3f);
    EXPECT_NEAR(mid.z, 2.0f, 1e-2f) << "straight: no bend";
    EXPECT_LT(end.y, root.y);
}

TEST(PoseEstimateTest, LearnsBoneLengths) {
    Estimator e;
    e.SetMode(kKinematic);
    Teach(e, MakeBody(1.0f, 0.50f), 120);
    EXPECT_NEAR(e.Thigh(0, 0), 0.50f, 0.01f);
    EXPECT_NEAR(e.Thigh(0, 1), 0.50f, 0.01f);
    // long after the last tracked frame, so the hold has decayed
    Person q;
    for (int i = 0; i < 90; i++) {
        q = MakeBody(1.0f, 0.50f);
        CutLegs(q);
        e.Apply(0, q, 1.0f / 30.0f, true);
    }
    EXPECT_NEAR(Dist(q.joints[kJointHipLeft], q.joints[kJointKneeLeft]), 0.50f, 0.01f);
    EXPECT_NEAR(Dist(q.joints[kJointHipRight], q.joints[kJointKneeRight]), 0.50f, 0.01f);
}

TEST(PoseEstimateTest, AWildTrackedJointTeachesNothing) {
    Estimator e;
    e.SetMode(kKinematic);
    Teach(e, MakeBody(1.0f, 0.50f), 120);
    // a run of frames with the legs "tracked" 3 m below the hips (what a
    // replay's held frame used to publish)
    Person wild = MakeBody(1.0f, 0.50f);
    CutLegs(wild);
    for (int j : {kJointKneeLeft, kJointKneeRight, kJointAnkleLeft, kJointAnkleRight, kJointFootLeft,
                  kJointFootRight})
        wild.confidence[j] = kConfidenceTracked;
    Teach(e, wild, 300);
    EXPECT_NEAR(e.Thigh(0, 0), 0.50f, 0.01f);
    EXPECT_NEAR(e.Thigh(0, 1), 0.50f, 0.01f);
    EXPECT_GT(e.Shin(0, 0), 0.3f);
    EXPECT_LT(e.Shin(0, 0), 0.7f);
}

TEST(PoseEstimateTest, FloorHoldsAnkles) {
    Estimator e;
    e.SetMode(kKinematic);
    Teach(e, MakeBody(0.9f), 150);
    ASSERT_TRUE(e.HaveFloor(0));
    EXPECT_NEAR(e.FloorUp(0), 0.1f, 0.02f);
    // legs cut for 3 s, the last second with the hips 20 cm lower
    Person q;
    for (int i = 0; i < 90; i++) {
        q = MakeBody(i < 60 ? 0.9f : 0.7f);
        CutLegs(q);
        e.Apply(0, q, 1.0f / 30.0f, true);
    }
    for (int j : {kJointAnkleLeft, kJointAnkleRight})
        EXPECT_NEAR(q.joints[j].y, 0.1f, 0.035f) << "ankle " << j << " stays on the floor";
    for (int j : {kJointKneeLeft, kJointKneeRight}) {
        EXPECT_LT(q.joints[j].z, 2.5f - 0.1f) << "knee " << j << " bends forward (towards the camera)";
        EXPECT_GT(q.joints[j].y, 0.1f + 0.1f);
        EXPECT_LT(q.joints[j].y, 0.7f - 0.1f);
    }
    EXPECT_NEAR(Dist(q.joints[kJointHipLeft], q.joints[kJointKneeLeft]), 0.40f, 0.01f);
    EXPECT_NEAR(Dist(q.joints[kJointKneeLeft], q.joints[kJointAnkleLeft]), e.Shin(0, 0), 0.01f);
}

TEST(PoseEstimateTest, HoldDecays) {
    Estimator e;
    e.SetMode(kKinematic);
    // seen with the left thigh raised 60 degrees forward, shin hanging
    Person seen = MakeBody();
    seen.joints[kJointKneeLeft].Set(-0.10f, 0.70f, 2.5f - 0.3464f);
    seen.joints[kJointAnkleLeft].Set(-0.10f, 0.30f, 2.5f - 0.3464f);
    seen.joints[kJointFootLeft].Set(-0.11f, 0.24f, 2.5f - 0.40f);
    Teach(e, seen, 60);
    // the body steps 0.5 m to her left with the legs cut
    Person q = MakeBody(0.9f, 0.40f, -0.5f);
    CutLegs(q);
    e.Apply(0, q, 1.0f / 30.0f, true);
    Vector3 heldAt(-0.10f - 0.5f, 0.70f, 2.5f - 0.3464f);
    EXPECT_LT(Dist(q.joints[kJointKneeLeft], heldAt), 0.05f) << "just lost: where it was, relative to the hip";
    for (int i = 0; i < 60; i++) {
        q = MakeBody(0.9f, 0.40f, -0.5f);
        CutLegs(q);
        e.Apply(0, q, 1.0f / 30.0f, true);
    }
    // 2 s on: the standing solution, the knee about under the hip again
    EXPECT_GT(Dist(q.joints[kJointKneeLeft], heldAt), 0.2f);
    EXPECT_NEAR(q.joints[kJointKneeLeft].y, 0.9f - 0.40f, 0.06f);
}

TEST(PoseEstimateTest, MarksInferred) {
    for (Mode m : {kOff, kTracker, kKinematic}) {
        Estimator e;
        e.SetMode(m);
        Teach(e, MakeBody(), 30);
        Person in = MakeBody();
        CutLegs(in);
        in.confidence[kJointHead] = kConfidenceNotTracked;
        Person out = in;
        int wrote = e.Apply(0, out, 1.0f / 30.0f, true);
        EXPECT_EQ(wrote, m == kOff ? 0 : 7) << ModeName(m);
        for (int j = 0; j < kNumJoints; j++) {
            if (in.confidence[j] == kConfidenceNotTracked) {
                EXPECT_EQ(out.confidence[j], m == kOff ? kConfidenceNotTracked : kConfidenceInferred)
                    << ModeName(m) << " joint " << j;
                if (m != kKinematic)
                    EXPECT_TRUE(out.joints[j] == in.joints[j]) << ModeName(m) << " moves only in kinematic";
                else
                    EXPECT_GT(out.joints[j].y, -2.0f) << "joint " << j << " is not the tracker's guess";
            } else {
                EXPECT_EQ(out.confidence[j], in.confidence[j]);
                EXPECT_TRUE(out.joints[j] == in.joints[j]) << "seen joint " << j << " untouched";
            }
        }
    }
}

TEST(PoseEstimateTest, APlausibleGuessIsKept) {
    Estimator e;
    e.SetMode(kKinematic);
    Teach(e, MakeBody(1.0f, 0.50f), 120);
    // long enough cut that the hold has decayed; the tracker guesses the
    // left knee raised forward, its bone 20 % short
    Person q;
    Vector3 guess(-0.10f, 1.0f - 0.40f * 0.5f, 2.5f - 0.40f * 0.866f);
    for (int i = 0; i < 90; i++) {
        q = MakeBody(1.0f, 0.50f);
        CutLegs(q);
        q.joints[kJointKneeLeft] = guess;
        e.Apply(0, q, 1.0f / 30.0f, true);
    }
    const Vector3 &knee = q.joints[kJointKneeLeft];
    // a 0.40 m thigh guess against a learned 0.50 is plausible: kept as it is
    // (rescaling it to the learned length scored lower in the game)
    EXPECT_NEAR(Dist(knee, guess), 0.0f, 0.01f) << "the guess itself";
    EXPECT_EQ(q.confidence[kJointKneeLeft], kConfidenceInferred);
    // the right knee's guess (-3 m) is not plausible: the solve, under the hip
    EXPECT_NEAR(q.joints[kJointKneeRight].y, 1.0f - 0.50f, 0.06f);
}

TEST(PoseEstimateTest, GainBlendsFromTheTrackersGuess) {
    Estimator full, none, half;
    full.SetMode(kKinematic);
    none.SetMode(kKinematic);
    half.SetMode(kKinematic);
    none.SetGain(0.0f);
    half.SetGain(0.5f);
    for (Estimator *e : {&full, &none, &half})
        Teach(*e, MakeBody(), 30);
    Person in = MakeBody();
    CutLegs(in);
    Person a = in, b = in, c = in;
    full.Apply(0, a, 1.0f / 30.0f, true);
    none.Apply(0, b, 1.0f / 30.0f, true);
    half.Apply(0, c, 1.0f / 30.0f, true);
    EXPECT_TRUE(b.joints[kJointKneeLeft] == in.joints[kJointKneeLeft]);
    EXPECT_NEAR(c.joints[kJointKneeLeft].y,
                0.5f * (a.joints[kJointKneeLeft].y + in.joints[kJointKneeLeft].y), 1e-4f);
}

TEST(PoseEstimateTest, ParsesItsKnobs) {
    bool ok = false;
    EXPECT_EQ(ParseMode(nullptr, &ok), kKinematic);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ParseMode("off", &ok), kOff);
    EXPECT_EQ(ParseMode("0", &ok), kOff);
    EXPECT_EQ(ParseMode("tracker", &ok), kTracker);
    EXPECT_EQ(ParseMode("kinematic", &ok), kKinematic);
    EXPECT_EQ(ParseMode("prior", &ok), kKinematic);
    EXPECT_FALSE(ok);
    EXPECT_FLOAT_EQ(ParseGain("0.25", &ok), 0.25f);
    EXPECT_TRUE(ok);
    EXPECT_FLOAT_EQ(ParseGain("2", &ok), kDefaultGain);
    EXPECT_FALSE(ok);
}
