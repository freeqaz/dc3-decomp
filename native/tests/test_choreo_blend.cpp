// The choreo performer's pose between two scheduled detect frames
// (native/src/platform/ChoreoBlend.h, PoseTarget_Native.cpp ChoreoAt; PLAN H-50).
//
// The straight chord from one detect frame to the next moves the player along
// the chord over a frame's displacement window (DancerSkeleton::ElapsedMs),
// while the scorer's displacement nodes compare that motion with the dancer's
// authored CamJointDisplacement (measured on cantgetyou Easy: 2-3 rad on the
// failing nodes).  The knot reproduces the authored motion; it is opt-in
// (DC3_POSE_PERFORM_KNOT=1), since over all of DC1 it does not lower the
// not-Perfect count (ChoreoBlend.h has the numbers).
//
// These tests pin the blend: over the last ElapsedMs before a frame the
// performer moves by exactly that frame's authored displacement, it still hits
// both frames' poses, and the knot steps aside when it cannot apply.

#include <gtest/gtest.h>

#include "platform/ChoreoBlend.h"

namespace {

struct V {
    float x, y, z;
    void Set(float a, float b, float c) {
        x = a;
        y = b;
        z = c;
    }
};

const int kN = 2; // two joints is enough: the blend is per joint

void ExpectNear(const V &a, const V &b, const char *what) {
    EXPECT_NEAR(a.x, b.x, 1e-5f) << what;
    EXPECT_NEAR(a.y, b.y, 1e-5f) << what;
    EXPECT_NEAR(a.z, b.z, 1e-5f) << what;
}

V Minus(const V &a, const V &b) { return V{a.x - b.x, a.y - b.y, a.z - b.z}; }

// The measured shape: from a to b the joint goes up and over, but its motion
// just before b is DOWN -- the authored displacement points against the chord.
const float kTa = 10.0f, kTb = 10.5f;
const int kDispMs = 99;
const V kPa[kN] = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
const V kPb[kN] = {{0.2f, 0.4f, 0.0f}, {1.0f, 1.2f, 1.0f}};
const V kDb[kN] = {{0.05f, -0.08f, 0.01f}, {0.0f, -0.03f, 0.0f}};

void At(float t, const V *pa, const V *db, int dispMs, V *out, float ta = kTa) {
    ChoreoBlend::Blend(ta, pa, kTb, kPb, db, dispMs, t, kN, out);
}

TEST(ChoreoBlendTest, MotionOverTheWindowIsTheAuthoredDisplacement) {
    V atB[kN], atKnot[kN];
    At(kTb, kPa, kDb, kDispMs, atB);
    At(kTb - kDispMs * 0.001f, kPa, kDb, kDispMs, atKnot);
    for (int j = 0; j < kN; j++) {
        ExpectNear(atB[j], kPb[j], "pose at the frame");
        // what Skeleton::Displacements measures: now minus ElapsedMs ago
        ExpectNear(Minus(atB[j], atKnot[j]), kDb[j], "motion over the window");
    }
    // and inside the window it moves along db, not along the chord
    V mid[kN];
    At(kTb - 0.5f * kDispMs * 0.001f, kPa, kDb, kDispMs, mid);
    V half{kPb[0].x - 0.5f * kDb[0].x, kPb[0].y - 0.5f * kDb[0].y, kPb[0].z - 0.5f * kDb[0].z};
    ExpectNear(mid[0], half, "halfway through the window");
}

TEST(ChoreoBlendTest, StillStartsFromThePreviousFrame) {
    V out[kN];
    At(kTa, kPa, kDb, kDispMs, out);
    for (int j = 0; j < kN; j++)
        ExpectNear(out[j], kPa[j], "pose at the previous frame");
}

// Control: without the displacement the old straight chord comes back, and its
// motion over the window points the other way from the authored one -- the
// failure the knot removes.
TEST(ChoreoBlendTest, WithoutADisplacementItIsTheChord) {
    V atB[kN], before[kN];
    float t0 = kTb - kDispMs * 0.001f;
    At(kTb, kPa, nullptr, kDispMs, atB);
    At(t0, kPa, nullptr, kDispMs, before);
    float frac = (kTb - t0) / (kTb - kTa);
    V chord = Minus(kPb[0], kPa[0]);
    V moved = Minus(atB[0], before[0]);
    ExpectNear(moved, V{chord.x * frac, chord.y * frac, chord.z * frac}, "chord motion");
    EXPECT_LT(moved.y * kDb[0].y, 0.0f) << "the chord moves up where the dancer moves down";
}

TEST(ChoreoBlendTest, FramesCloserThanTheWindowKeepTheChord) {
    // previous frame 50 ms before b: a knot 99 ms before b would sit before it
    V out[kN];
    float ta = kTb - 0.05f;
    At(kTb - 0.025f, kPa, kDb, kDispMs, out, ta);
    ExpectNear(out[0], V{(kPa[0].x + kPb[0].x) * 0.5f, (kPa[0].y + kPb[0].y) * 0.5f,
                         (kPa[0].z + kPb[0].z) * 0.5f},
               "plain chord midpoint");
}

TEST(ChoreoBlendTest, NoDisplacementWindowNoKnot) {
    for (int ms : {-1, 0, ChoreoBlend::kMaxDisplacementMs + 1}) {
        V a[kN], b[kN];
        At(kTb - 0.05f, kPa, kDb, ms, a);
        At(kTb - 0.05f, kPa, nullptr, ms, b);
        ExpectNear(a[0], b[0], "dispMs out of range blends as the chord");
    }
}

TEST(ChoreoBlendTest, FirstFrameHoldsAtTheKnotThenMovesByTheDisplacement) {
    V early[kN], atKnot[kN], atB[kN];
    At(kTb - 0.3f, nullptr, kDb, kDispMs, early);
    At(kTb - kDispMs * 0.001f, nullptr, kDb, kDispMs, atKnot);
    At(kTb, nullptr, kDb, kDispMs, atB);
    for (int j = 0; j < kN; j++) {
        ExpectNear(early[j], Minus(kPb[j], kDb[j]), "held at the knot");
        ExpectNear(atKnot[j], Minus(kPb[j], kDb[j]), "the knot");
        ExpectNear(atB[j], kPb[j], "the frame");
    }
    // without a displacement the first frame is simply held
    V held[kN];
    At(kTb - 0.3f, nullptr, nullptr, kDispMs, held);
    ExpectNear(held[0], kPb[0], "held at the frame");
}

} // namespace
