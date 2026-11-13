// Camera tilt (PLAN P-7, np-pose-tilt): NuiTransformMatrixLevel, the gravity
// estimator and Create's level step (native/src/platform/PoseLevel_Native.h).
//
//   NuiLevelMatrixTest      the XDK level matrix, ported from the image: it
//                           rotates the sensor's "up" onto +y, is a pure pitch
//                           for a pitch-only normal (fixture_transform.py
//                           --pitch's formula), and is the identity for an
//                           upright or zero normal.
//   GravityEstimatorTest    a synthetic dancer seen by a tilted camera comes
//                           out upright; nothing before 5 s of upright frames;
//                           held while the posture fails the gates; updated
//                           once a second and slowly, never per frame.
//   PoseLevelPolicyTest     DC3_POSE_LEVEL parsing.
//   PoseLevelFrameTest      NativeLevelSensorFrame on a SkeletonFrame:
//                           mJointPositions and mHipCenter levelled,
//                           mRawPositions raw, mFloorNormal the smoothed normal.
//
// The end-to-end case through the socket and the sensor frame is
// PoseSensorLevelTest in test_native_pose_socket.cpp.  Each case was
// sabotage-checked against the code it pins (see the commit).

#include <gtest/gtest.h>

#include "platform/PoseLevel_Native.h"
#include "gesture/Skeleton.h"
#include "xdk/nui/nuiskeleton.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace {

const float kPi = 3.14159265358979f;
const float kDeg = kPi / 180.0f;

Vector3 Xform(const Vector3 &v, const XMMATRIX &m) {
    return Vector3(v.x * m.r[0].x + v.y * m.r[1].x + v.z * m.r[2].x + m.r[3].x,
                   v.x * m.r[0].y + v.y * m.r[1].y + v.z * m.r[2].y + m.r[3].y,
                   v.x * m.r[0].z + v.y * m.r[1].z + v.z * m.r[2].z + m.r[3].z);
}

XMMATRIX Level(const Vector3 &n) {
    XMVECTOR g;
    g.x = n.x;
    g.y = n.y;
    g.z = n.z;
    g.w = 0.0f;
    return NuiTransformMatrixLevel(g);
}

// Rotate v by the rotation taking +y onto unit `up` (what a camera whose
// up is `up` sees of a levelled scene): Rodrigues about cross(y, up).
Vector3 FromLevel(const Vector3 &v, const Vector3 &up) {
    Vector3 ax(up.z, 0.0f, -up.x); // cross((0,1,0), up)
    float s = std::sqrt(ax.x * ax.x + ax.z * ax.z);
    if (s < 1e-9f)
        return v;
    ax = Vector3(ax.x / s, 0.0f, ax.z / s);
    float ang = std::acos(std::max(-1.0f, std::min(1.0f, up.y)));
    float c = std::cos(ang), sn = std::sin(ang);
    float d = ax.x * v.x + ax.y * v.y + ax.z * v.z;
    Vector3 cr(ax.y * v.z - ax.z * v.y, ax.z * v.x - ax.x * v.z, ax.x * v.y - ax.y * v.x);
    return Vector3(v.x * c + cr.x * sn + ax.x * d * (1 - c), v.y * c + cr.y * sn + ax.y * d * (1 - c),
                   v.z * c + cr.z * sn + ax.z * d * (1 - c));
}

float PitchOf(const Vector3 &a) { return std::atan2(-a.z, a.y) / kDeg; }
float RollOf(const Vector3 &a) { return std::atan2(a.x, a.y) / kDeg; }

// A dancer in LEVELLED camera space, facing the camera 2.4 m away: legs
// straight, trunk in line, the ankles -> HipCenter axis leaning
// kPriorPitchDeg toward the camera (the reference's own lean -- "upright" as
// a Kinect sees it); arms swinging with time `t`.
struct Body {
    Vector3 j[kNumJoints];
    JointConfidence c[kNumJoints];
};

Body Dancer(float t, float leanDeg = NativeGravityEstimator::kPriorPitchDeg) {
    Body b;
    // Built upright on the floor plane (y up from the ankles), then leaned.
    static const float kUpright[kNumJoints][3] = {
        { 0.00f, 0.95f, 0 },  { 0.00f, 1.10f, 0.03f }, { 0.00f, 1.42f, 0 }, { 0.00f, 1.62f, 0 },
        { -0.18f, 1.38f, 0 }, { -0.24f, 1.10f, 0 },    { -0.26f, 0.86f, 0 }, { -0.27f, 0.79f, 0 },
        { 0.18f, 1.38f, 0 },  { 0.24f, 1.10f, 0 },     { 0.26f, 0.86f, 0 },  { 0.27f, 0.79f, 0 },
        { -0.10f, 0.88f, 0 }, { -0.10f, 0.47f, 0 },    { -0.10f, 0.08f, 0 }, { 0.10f, 0.88f, 0 },
        { 0.10f, 0.47f, 0 },  { 0.10f, 0.08f, 0 },     { -0.10f, 0.02f, -0.06f }, { 0.10f, 0.02f, -0.06f },
    };
    float sw = std::sin(2.0f * kPi * 1.3f * t); // arms: 1.3 Hz swing
    float cl = std::cos(leanDeg * kDeg), sl = std::sin(leanDeg * kDeg);
    for (int i = 0; i < kNumJoints; i++) {
        float x = kUpright[i][0], y = kUpright[i][1] - 0.08f, z = kUpright[i][2];
        if (i == kJointElbowLeft || i == kJointWristLeft || i == kJointHandLeft
            || i == kJointElbowRight || i == kJointWristRight || i == kJointHandRight) {
            float r = (i == kJointElbowLeft || i == kJointElbowRight) ? 0.3f : 0.55f;
            z += -r * 0.8f * (sw > 0 ? sw : -sw);
            y += r * 0.5f * sw;
        }
        // lean: top toward the camera (-z) about the ankles' height (y = 0)
        float y2 = y * cl + z * sl, z2 = -y * sl + z * cl;
        b.j[i] = Vector3(x, y2 - 0.95f, z2 + 2.4f); // sensor ~0.95 m up, 2.4 m away
        b.c[i] = kConfidenceTracked;
    }
    return b;
}

Body SeenBy(const Body &level, float camPitchDeg, float camRollDeg) {
    Vector3 up = NativeGravityEstimator::NormalFromPitchRoll(camPitchDeg, camRollDeg);
    Body b = level;
    for (int i = 0; i < kNumJoints; i++)
        b.j[i] = FromLevel(level.j[i], up);
    return b;
}

Vector3 Axis(const Vector3 *j) {
    Vector3 f((j[kJointAnkleLeft].x + j[kJointAnkleRight].x) * 0.5f,
              (j[kJointAnkleLeft].y + j[kJointAnkleRight].y) * 0.5f,
              (j[kJointAnkleLeft].z + j[kJointAnkleRight].z) * 0.5f);
    return Vector3(j[kJointHipCenter].x - f.x, j[kJointHipCenter].y - f.y, j[kJointHipCenter].z - f.z);
}

// Feed `secs` of the dancer seen by the camera at 30 Hz.
void Feed(NativeGravityEstimator &e, float &t, float secs, float pitch, float roll) {
    for (int k = 0; k < (int)(secs * 30.0f + 0.5f); k++) {
        Body b = SeenBy(Dancer(t), pitch, roll);
        e.AddPerson(b.j, b.c);
        e.Tick(1.0f / 30.0f);
        t += 1.0f / 30.0f;
    }
}

// ===========================================================================
TEST(NuiLevelMatrixTest, LevelsTheSensorsUpOntoPlusY) {
    const Vector3 ups[] = {
        NativeGravityEstimator::NormalFromPitchRoll(8.0f, -3.0f),
        NativeGravityEstimator::NormalFromPitchRoll(-20.0f, 12.0f),
        Vector3(0.3f, 0.8f, -0.5f), // not unit: the image normalises it
    };
    for (const Vector3 &n : ups) {
        XMMATRIX m = Level(n);
        float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        Vector3 l = Xform(Vector3(n.x / len, n.y / len, n.z / len), m);
        EXPECT_NEAR(l.x, 0.0f, 1e-5f);
        EXPECT_NEAR(l.y, 1.0f, 1e-5f);
        EXPECT_NEAR(l.z, 0.0f, 1e-5f);
        // A rotation: orthonormal rows, determinant +1, no translation.
        for (int a = 0; a < 3; a++) {
            for (int b = 0; b < 3; b++) {
                float d = m.r[a].x * m.r[b].x + m.r[a].y * m.r[b].y + m.r[a].z * m.r[b].z;
                EXPECT_NEAR(d, a == b ? 1.0f : 0.0f, 1e-5f);
            }
        }
        float det = m.r[0].x * (m.r[1].y * m.r[2].z - m.r[1].z * m.r[2].y)
            - m.r[0].y * (m.r[1].x * m.r[2].z - m.r[1].z * m.r[2].x)
            + m.r[0].z * (m.r[1].x * m.r[2].y - m.r[1].y * m.r[2].x);
        EXPECT_NEAR(det, 1.0f, 1e-5f);
        EXPECT_EQ(m.r[3].x, 0.0f);
        EXPECT_EQ(m.r[3].y, 0.0f);
        EXPECT_EQ(m.r[3].z, 0.0f);
        EXPECT_EQ(m.r[3].w, 1.0f);
        // The minimal rotation: the axis perpendicular to up and +y is fixed
        // (no yaw is introduced).
        Vector3 ax(-n.z, 0.0f, n.x);
        Vector3 ax2 = Xform(ax, m);
        EXPECT_NEAR(ax2.x, ax.x, 1e-5f);
        EXPECT_NEAR(ax2.z, ax.z, 1e-5f);
    }
}

TEST(NuiLevelMatrixTest, APitchOnlyUpIsARotationAboutX) {
    // fixture_transform.py --pitch D: y' = y cos D - z sin D, z' = y sin D + z cos D
    const float d = 10.0f;
    XMMATRIX m = Level(NativeGravityEstimator::NormalFromPitchRoll(d, 0.0f));
    Vector3 p(0.3f, -0.7f, 2.2f);
    Vector3 q = Xform(p, m);
    float c = std::cos(d * kDeg), s = std::sin(d * kDeg);
    EXPECT_NEAR(q.x, p.x, 1e-6f);
    EXPECT_NEAR(q.y, p.y * c - p.z * s, 1e-5f);
    EXPECT_NEAR(q.z, p.y * s + p.z * c, 1e-5f);
}

TEST(NuiLevelMatrixTest, AnUprightOrZeroUpIsTheIdentity) {
    for (const Vector3 &n : { Vector3(0, 1, 0), Vector3(0, 0, 0), Vector3(0, 2.5f, 0) }) {
        XMMATRIX m = Level(n);
        for (int a = 0; a < 4; a++) {
            EXPECT_EQ(m.r[a].x, a == 0 ? 1.0f : 0.0f);
            EXPECT_EQ(m.r[a].y, a == 1 ? 1.0f : 0.0f);
            EXPECT_EQ(m.r[a].z, a == 2 ? 1.0f : 0.0f);
            EXPECT_EQ(m.r[a].w, a == 3 ? 1.0f : 0.0f);
        }
    }
}

// ===========================================================================
TEST(GravityEstimatorTest, ATiltedDancerComesOutUpright) {
    const float camPitch = 8.0f, camRoll = -3.0f;
    NativeGravityEstimator e;
    float t = 0.0f;
    Feed(e, t, 12.0f, camPitch, camRoll);
    ASSERT_TRUE(e.HasEstimate());
    EXPECT_NEAR(e.PitchDeg(), camPitch, 0.3f);
    EXPECT_NEAR(e.RollDeg(), camRoll, 0.3f);

    // Level a new frame by the estimate: the axis is back to the Kinect's
    // upright, and every joint back where the level scene had it.
    XMMATRIX m = Level(e.NormalToGravity());
    Body level = Dancer(t);
    Body seen = SeenBy(level, camPitch, camRoll);
    Vector3 out[kNumJoints];
    for (int i = 0; i < kNumJoints; i++)
        out[i] = Xform(seen.j[i], m);
    Vector3 a = Axis(out);
    EXPECT_NEAR(PitchOf(a), NativeGravityEstimator::kPriorPitchDeg, 0.3f);
    EXPECT_NEAR(RollOf(a), 0.0f, 0.3f);
    // Unlevelled, the same frame leans by the camera's tilt.
    Vector3 raw = Axis(seen.j);
    EXPECT_GT(PitchOf(raw), NativeGravityEstimator::kPriorPitchDeg + camPitch - 0.5f);
    for (int i = 0; i < kNumJoints; i++) {
        EXPECT_NEAR(out[i].x, level.j[i].x, 0.015f) << i;
        EXPECT_NEAR(out[i].y, level.j[i].y, 0.015f) << i;
        EXPECT_NEAR(out[i].z, level.j[i].z, 0.015f) << i;
    }
}

TEST(GravityEstimatorTest, NothingBeforeFiveSecondsOfUprightFrames) {
    NativeGravityEstimator e;
    float t = 0.0f;
    Feed(e, t, 4.5f, 8.0f, 0.0f);
    EXPECT_FALSE(e.HasEstimate());
    Vector3 n = e.NormalToGravity();
    EXPECT_EQ(n.x, 0.0f);
    EXPECT_EQ(n.y, 1.0f);
    EXPECT_EQ(n.z, 0.0f);
    Feed(e, t, 1.5f, 8.0f, 0.0f);
    EXPECT_TRUE(e.HasEstimate());
}

TEST(GravityEstimatorTest, HeldWhileThePostureFailsTheGates) {
    NativeGravityEstimator e;
    float t = 0.0f;
    Feed(e, t, 10.0f, 8.0f, 0.0f);
    ASSERT_TRUE(e.HasEstimate());
    const float pitch = e.PitchDeg(), roll = e.RollDeg();
    const int samples = e.NumSamples();
    // 30 s the estimator must not learn from, each seen by a camera tilted
    // the other way: legs out of frame (MediaPipe still guesses them),
    // bent over, side-on, and knees bent.
    for (int k = 0; k < 900; k++, t += 1.0f / 30.0f) {
        Body b = SeenBy(Dancer(t, 20.0f), -10.0f, 6.0f);
        switch (k % 4) {
        case 0:
            b.c[kJointKneeLeft] = b.c[kJointAnkleLeft] = kConfidenceInferred;
            break;
        case 1: // trunk folded 40 deg forward of the legs
            for (int i : { kJointSpine, kJointShoulderCenter, kJointHead, kJointShoulderLeft,
                           kJointShoulderRight }) {
                b.j[i].z -= 0.8f * (b.j[i].y - b.j[kJointHipCenter].y);
            }
            break;
        case 2: // side-on: the shoulder line points at the camera
            b.j[kJointShoulderLeft].x = b.j[kJointShoulderRight].x = 0.0f;
            b.j[kJointShoulderLeft].z -= 0.2f;
            break;
        case 3: // knees bent forward in the picture
            b.j[kJointKneeLeft].x -= 0.25f;
            b.j[kJointKneeRight].x += 0.25f;
            break;
        }
        EXPECT_FALSE(e.AddPerson(b.j, b.c)) << k;
        e.Tick(1.0f / 30.0f);
    }
    EXPECT_EQ(e.PitchDeg(), pitch);
    EXPECT_EQ(e.RollDeg(), roll);
    EXPECT_EQ(e.NumSamples(), samples);
}

TEST(GravityEstimatorTest, UpdatedOnceASecondAndSlowly) {
    NativeGravityEstimator e;
    float t = 0.0f;
    Feed(e, t, 25.0f, 8.0f, 0.0f);
    ASSERT_NEAR(e.PitchDeg(), 8.0f, 0.3f);
    // The camera is knocked level.  Within one update period the estimate
    // does not move at all, however many frames arrive.
    int changes = 0;
    float prev = e.PitchDeg();
    std::vector<float> history;
    for (int k = 0; k < 90; k++) {
        Body b = SeenBy(Dancer(t), 0.0f, 0.0f);
        e.AddPerson(b.j, b.c);
        bool changed = e.Tick(1.0f / 30.0f);
        t += 1.0f / 30.0f;
        if (e.PitchDeg() != prev) {
            changes++;
            EXPECT_TRUE(changed);
            // A step is at most kBlend of the way to the window's median
            // (which still holds the old camera's samples).
            EXPECT_LE(prev - e.PitchDeg(), NativeGravityEstimator::kBlend * 8.5f) << k;
            prev = e.PitchDeg();
        } else {
            EXPECT_FALSE(changed);
        }
    }
    EXPECT_LE(changes, 3); // 3 s: at most one update a second
    EXPECT_GT(e.PitchDeg(), 6.0f); // and still near the old camera: slow
    Feed(e, t, 40.0f, 0.0f, 0.0f);
    EXPECT_NEAR(e.PitchDeg(), 0.0f, 0.5f);
}

// ===========================================================================
TEST(PoseLevelPolicyTest, ParsesTheEnvironment) {
    EXPECT_EQ(NativePoseLevelParse(nullptr).mode, NativePoseLevelPolicy::kAuto);
    EXPECT_EQ(NativePoseLevelParse("").mode, NativePoseLevelPolicy::kAuto);
    EXPECT_EQ(NativePoseLevelParse("auto").mode, NativePoseLevelPolicy::kAuto);
    EXPECT_EQ(NativePoseLevelParse("1").mode, NativePoseLevelPolicy::kAuto);
    EXPECT_EQ(NativePoseLevelParse("0").mode, NativePoseLevelPolicy::kOff);
    EXPECT_EQ(NativePoseLevelParse("off").mode, NativePoseLevelPolicy::kOff);
    NativePoseLevelPolicy f = NativePoseLevelParse("10");
    EXPECT_EQ(f.mode, NativePoseLevelPolicy::kFixed);
    EXPECT_FLOAT_EQ(f.fixedPitchDeg, 10.0f);
    EXPECT_EQ(NativePoseLevelParse("-7.5").mode, NativePoseLevelPolicy::kFixed);
    EXPECT_EQ(NativePoseLevelParse("45").mode, NativePoseLevelPolicy::kAuto);   // out of range
    EXPECT_EQ(NativePoseLevelParse("10deg").mode, NativePoseLevelPolicy::kAuto); // not a number
}

// ===========================================================================
class PoseLevelFrameTest : public ::testing::Test {
protected:
    void SetUp() override {
        // config/default.dta kinect/up_vector_smoothing: smoothing 10, trend 0
        // (SkeletonFrame::Init reads it in a booted game).
        SkeletonFrame::sUpVectorSmoother.SetSmoothParameters(10.0f, 0.0f);
        SkeletonFrame::sUpVectorSmoother.ForceValue(Vector3(0, 1, 0));
    }
    void TearDown() override { SkeletonFrame::sUpVectorSmoother.ForceValue(Vector3(0, 1, 0)); }

    static void Fill(SkeletonFrame &f, const Body &b) {
        memset(&f, 0, sizeof(f));
        f.mElapsedMs = 33;
        SkeletonData &d = f.mSkeletonDatas[2];
        d.mTracking = kSkeletonTracked;
        for (int i = 0; i < kNumJoints; i++) {
            d.mJointPositions[i].Set(b.j[i].x, b.j[i].y, b.j[i].z);
            d.mRawPositions[i].Set(b.j[i].x, b.j[i].y, b.j[i].z);
        }
        d.mHipCenter = b.j[kJointHipCenter];
        // An untracked slot with stale data must be left alone.
        f.mSkeletonDatas[4].mJointPositions[0].Set(1, 2, 3);
    }
};

TEST_F(PoseLevelFrameTest, CreatesGravityStepLevelsTheJointsAndKeepsTheRaw) {
    const Vector3 up = NativeGravityEstimator::NormalFromPitchRoll(9.0f, 2.0f);
    Body level = Dancer(0.4f);
    Body seen = SeenBy(level, 9.0f, 2.0f);
    SkeletonFrame f;
    for (int k = 0; k < 60; k++) { // 2 s: the image's smoother (10/s) converges
        Fill(f, seen);
        NativeLevelSensorFrame(f, up);
    }
    EXPECT_NEAR(f.mFloorNormal.x, up.x, 1e-4f);
    EXPECT_NEAR(f.mFloorNormal.y, up.y, 1e-4f);
    EXPECT_NEAR(f.mFloorNormal.z, up.z, 1e-4f);
    const SkeletonData &d = f.mSkeletonDatas[2];
    for (int i = 0; i < kNumJoints; i++) {
        EXPECT_NEAR(d.mJointPositions[i].x, level.j[i].x, 2e-3f) << i;
        EXPECT_NEAR(d.mJointPositions[i].y, level.j[i].y, 2e-3f) << i;
        EXPECT_NEAR(d.mJointPositions[i].z, level.j[i].z, 2e-3f) << i;
        EXPECT_EQ(d.mRawPositions[i].x, seen.j[i].x) << i;
        EXPECT_EQ(d.mRawPositions[i].y, seen.j[i].y) << i;
        EXPECT_EQ(d.mRawPositions[i].z, seen.j[i].z) << i;
    }
    EXPECT_NEAR(d.mHipCenter.y, level.j[kJointHipCenter].y, 2e-3f);
    EXPECT_NEAR(d.mHipCenter.z, level.j[kJointHipCenter].z, 2e-3f);
    EXPECT_EQ(f.mSkeletonDatas[4].mJointPositions[0].x, 1.0f);
    EXPECT_EQ(f.mSkeletonDatas[4].mJointPositions[0].z, 3.0f);
}

TEST_F(PoseLevelFrameTest, AnUprightNormalLeavesTheFrameExactlyAsItWas) {
    Body seen = SeenBy(Dancer(0.4f), 9.0f, 2.0f);
    SkeletonFrame f;
    Fill(f, seen);
    NativeLevelSensorFrame(f, Vector3(0, 1, 0));
    const SkeletonData &d = f.mSkeletonDatas[2];
    for (int i = 0; i < kNumJoints; i++) {
        EXPECT_EQ(d.mJointPositions[i].x, seen.j[i].x) << i;
        EXPECT_EQ(d.mJointPositions[i].y, seen.j[i].y) << i;
        EXPECT_EQ(d.mJointPositions[i].z, seen.j[i].z) << i;
    }
    EXPECT_EQ(f.mFloorNormal.y, 1.0f);
}

} // namespace
