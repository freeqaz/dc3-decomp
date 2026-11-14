#pragma once
// ChoreoBlend.h -- the choreo performer's pose between two scheduled detect
// frames (PoseTarget_Native.cpp ChoreoAt, PLAN H-50).
//
// WHY THE SECOND KNOT
//   A detect frame scores the player on two things: the pose at the frame's
//   time, and -- the displacement error nodes -- how each joint MOVED over the
//   frame's displacement window (DancerSkeleton::ElapsedMs(), 99 ms on the
//   Dance Central 1 moves measured).  The scorer takes the player's motion as
//   the live skeleton now minus the live skeleton ElapsedMs ago
//   (ErrorFrameInput -> Skeleton::Displacements), and the dancer's as the
//   authored CamJointDisplacement of that frame.
//
//   Interpolating straight from the previous frame to this one gives the right
//   pose but the wrong motion: the last 99 ms of a straight chord points from
//   the previous frame to this one, while the authored displacement is the
//   dancer's real motion just before this frame.  Wherever the real motion
//   curves -- an arm that swings up and comes back, a knee that bends and
//   straightens -- the two point different ways.  MEASURED on cantgetyou Easy
//   (Headwrush_cantgetyou, rated Awesome at exactly 0.8000 on every
//   occurrence): the failing displacement nodes read angles of 2-3 rad between
//   dancer and player motion.  Dance Central 1 moves have about five frames a
//   measure, so the chord is long and this is where it shows (H-50: 4.8% of
//   DC1 moves not Perfect against 0.1% of DC2/DC3 moves).
//
//   So the performer puts a second knot ElapsedMs before each frame, at the
//   frame's pose minus its authored displacement, and blends through it.  A
//   player who copies the dancer exactly is what the performer plays; this is
//   only the performer being a more faithful copy.  The scorer is unchanged.
//
// The knot is used only when it falls after the previous frame (frames closer
// together than ElapsedMs keep the plain chord) and when the frame carries a
// displacement window (ElapsedMs > 0; pre-rev-4 data reads -1).
//
// Header-only and free of game types (any V with x/y/z and Set) so it is
// tested on its own (native/tests/test_choreo_blend.cpp).

namespace ChoreoBlend {

// A displacement window longer than this is not trusted (no knot).  Nothing in
// the scorer uses this bound; it only keeps a garbage ElapsedMs harmless.
const int kMaxDisplacementMs = 1000;

// Seconds before `tb` at which the displacement knot sits, or a negative
// number if frame b has no usable displacement window.
inline float KnotLead(int dispMs) {
    if (dispMs <= 0 || dispMs > kMaxDisplacementMs)
        return -1.0f;
    return dispMs * 0.001f;
}

template <class V> inline void Lerp(const V &a, const V &b, float w, V &out) {
    out.Set(a.x + (b.x - a.x) * w, a.y + (b.y - a.y) * w, a.z + (b.z - a.z) * w);
}

inline float Clamp01(float w) { return w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w); }

// Pose of `n` joints at song time t, between frame a (time ta, pose pa; null
// when there is no earlier frame in this motion) and frame b (time tb, pose
// pb, authored displacement db over dispMs; db null to blend without a knot).
// t is expected in [ta, tb] (or <= tb when pa is null).
template <class V>
void Blend(float ta, const V *pa, float tb, const V *pb, const V *db, int dispMs, float t, int n,
           V *out) {
    float lead = db ? KnotLead(dispMs) : -1.0f;
    float tk = tb - lead;
    bool knot = lead > 0.0f && (!pa || tk > ta);
    if (knot && t >= tk) {
        // inside the displacement window: knot -> b, moving by exactly db
        float w = Clamp01((t - tk) / lead);
        for (int j = 0; j < n; j++) {
            V k;
            k.Set(pb[j].x - db[j].x, pb[j].y - db[j].y, pb[j].z - db[j].z);
            Lerp(k, pb[j], w, out[j]);
        }
        return;
    }
    if (!pa) {
        // no earlier frame: hold, at the knot if there is one
        for (int j = 0; j < n; j++) {
            if (knot)
                out[j].Set(pb[j].x - db[j].x, pb[j].y - db[j].y, pb[j].z - db[j].z);
            else
                out[j] = pb[j];
        }
        return;
    }
    float end = knot ? tk : tb;
    float w = end > ta ? Clamp01((t - ta) / (end - ta)) : 0.0f;
    for (int j = 0; j < n; j++) {
        if (knot) {
            V k;
            k.Set(pb[j].x - db[j].x, pb[j].y - db[j].y, pb[j].z - db[j].z);
            Lerp(pa[j], k, w, out[j]);
        } else {
            Lerp(pa[j], pb[j], w, out[j]);
        }
    }
}

} // namespace ChoreoBlend
