// PoseEstimate_Native.cpp -- see PoseEstimate_Native.h.
#include "PoseEstimate_Native.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace Dc3PoseEstimate {

namespace {

struct V {
    float x, y, z;
    V() : x(0), y(0), z(0) {}
    V(float a, float b, float c) : x(a), y(b), z(c) {}
    explicit V(const Vector3 &v) : x(v.x), y(v.y), z(v.z) {}
    V operator+(const V &o) const { return V(x + o.x, y + o.y, z + o.z); }
    V operator-(const V &o) const { return V(x - o.x, y - o.y, z - o.z); }
    V operator*(float s) const { return V(x * s, y * s, z * s); }
    void To(Vector3 &v) const { v.Set(x, y, z); }
};
float Dot(const V &a, const V &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V Cross(const V &a, const V &b) {
    return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
float Len(const V &a) { return std::sqrt(Dot(a, a)); }
V Unit(const V &a, const V &fallback) {
    float l = Len(a);
    return l > 1e-6f ? a * (1.0f / l) : fallback;
}

const int kHip[2] = {kJointHipLeft, kJointHipRight};
const int kKnee[2] = {kJointKneeLeft, kJointKneeRight};
const int kAnkle[2] = {kJointAnkleLeft, kJointAnkleRight};
const int kFoot[2] = {kJointFootLeft, kJointFootRight};
const int kElbow[2] = {kJointElbowLeft, kJointElbowRight};
const int kWrist[2] = {kJointWristLeft, kJointWristRight};
const int kHand[2] = {kJointHandLeft, kJointHandRight};

// The joint each one's hold is kept relative to.
int Anchor(int j) {
    switch (j) {
    case kJointKneeLeft:
    case kJointAnkleLeft:
        return kJointHipLeft;
    case kJointKneeRight:
    case kJointAnkleRight:
        return kJointHipRight;
    case kJointFootLeft:
        return kJointAnkleLeft;
    case kJointFootRight:
        return kJointAnkleRight;
    case kJointHead:
        return kJointShoulderCenter;
    case kJointElbowLeft:
        return kJointShoulderLeft;
    case kJointElbowRight:
        return kJointShoulderRight;
    case kJointWristLeft:
        return kJointElbowLeft;
    case kJointWristRight:
        return kJointElbowRight;
    case kJointHandLeft:
        return kJointWristLeft;
    case kJointHandRight:
        return kJointWristRight;
    }
    return -1;
}

// The neutral leg in the body frame (r = player's right, u = up, f = towards
// where she faces), torso units, MEASURED medians over the footage fixtures:
// ankle 0.13 m outside the hip joint and 0.04 m behind it; the foot
// (heel/toe midpoint) below and ahead of the ankle.  `side` 0 = left.
const float kAnkleOut = 0.34f, kAnkleBack = 0.10f;
const float kFootDirOut = 0.26f, kFootDirUp = -0.80f, kFootDirFwd = 0.51f;
const float kKneeBendOut = 0.3f; // the knee bends forward and a little outward

void Learn(float &v, float sample, float alpha) { v = v > 0.0f ? v + (sample - v) * alpha : sample; }

// A tracker's guess for a bone is used for its direction when the bone it
// implies is within this range of the bone's length: a guess 2 m from its
// parent (a stream with no real guesses) is not.
const float kGuessMin = 0.5f, kGuessMax = 1.6f;
bool Plausible(const V &bone, float length) {
    float l = Len(bone);
    return length > 0.0f && l >= kGuessMin * length && l <= kGuessMax * length;
}
} // namespace

Mode ParseMode(const char *env, bool *ok) {
    if (ok)
        *ok = true;
    if (!env || !*env || !strcmp(env, "kinematic"))
        return kKinematic;
    if (!strcmp(env, "off") || !strcmp(env, "0"))
        return kOff;
    if (!strcmp(env, "tracker"))
        return kTracker;
    if (ok)
        *ok = false;
    return kKinematic;
}

float ParseGain(const char *env, bool *ok) {
    if (ok)
        *ok = true;
    if (!env || !*env)
        return kDefaultGain;
    char *end = nullptr;
    double g = strtod(env, &end);
    if (end == env || *end != '\0' || !(g >= 0.0 && g <= 1.0)) {
        if (ok)
            *ok = false;
        return kDefaultGain;
    }
    return (float)g;
}

const char *ModeName(Mode m) {
    switch (m) {
    case kOff:
        return "off";
    case kTracker:
        return "tracker";
    case kKinematic:
        return "kinematic";
    }
    return "?";
}

bool TwoBone(const Vector3 &rootIn, const Vector3 &targetIn, float a, float b, const Vector3 &bendIn,
             Vector3 &midOut, Vector3 &endOut) {
    V root(rootIn), target(targetIn), bend(bendIn);
    V d = target - root;
    float dist = Len(d);
    V axis = Unit(d, V(0, -1, 0));
    // the bend direction, made perpendicular to the limb
    V perp = Unit(bend - axis * Dot(bend, axis), Unit(Cross(axis, V(1, 0, 0)), V(0, 0, -1)));
    bool reach = true;
    float reachMax = (a + b) * 0.9999f, reachMin = std::fabs(a - b) + 1e-4f;
    if (dist > reachMax) {
        dist = reachMax;
        reach = false;
    } else if (dist < reachMin) {
        dist = reachMin;
        reach = false;
    }
    // law of cosines: distance along the axis to the middle joint's foot
    float along = (a * a - b * b + dist * dist) / (2.0f * dist);
    float h2 = a * a - along * along;
    float h = h2 > 0.0f ? std::sqrt(h2) : 0.0f;
    V mid = root + axis * along + perp * h;
    V end = root + axis * dist;
    mid.To(midOut);
    end.To(endOut);
    return reach;
}

void Estimator::Reset() {
    for (int s = 0; s < kSlots; s++)
        mBody[s] = Body();
    mEstimated = 0;
}

void Estimator::ResetSlot(int slot) {
    if (slot < 0 || slot >= kSlots)
        return;
    // the floor (hipStand, up) is the camera's, not the person's
    Body &b = mBody[slot];
    Body fresh;
    fresh.up = b.up;
    fresh.haveUp = b.haveUp;
    b = fresh;
}

float Estimator::FloorUp(int slot) const {
    const Body &b = mBody[slot];
    float stand = b.standHip > 0.0f ? b.standHip : kStandHipPerTorso * b.torso;
    return b.hipStand - stand;
}

int Estimator::Apply(int slot, Person &p, float seconds, bool learn) {
    if (mMode == kOff || !p.valid || slot < 0 || slot >= kSlots)
        return 0;
    bool missing[kNumJoints];
    int numMissing = 0;
    for (int j = 0; j < kNumJoints; j++) {
        missing[j] = p.confidence[j] == kConfidenceNotTracked;
        numMissing += missing[j];
    }
    Body &b = mBody[slot];
    const JointConfidence kT = kConfidenceTracked;
    if (seconds < 0.0f)
        seconds = 0.0f;
    if (seconds > 0.5f)
        seconds = 0.5f;

    V joint[kNumJoints];
    for (int j = 0; j < kNumJoints; j++)
        joint[j] = V(p.joints[j]);
    V hipC = joint[kJointHipCenter], shC = joint[kJointShoulderCenter];
    bool torsoTracked = p.confidence[kJointHipLeft] == kT && p.confidence[kJointHipRight] == kT
        && p.confidence[kJointShoulderLeft] == kT && p.confidence[kJointShoulderRight] == kT;
    bool hipsSeen = !missing[kJointHipLeft] && !missing[kJointHipRight];

    // --- learn: torso, up, bones, standing height --------------------------
    if (learn && torsoTracked) {
        V t = shC - hipC;
        float tl = Len(t);
        if (tl > 0.15f && tl < 0.8f) {
            Learn(b.torso, tl, b.torsoSamples < 10 ? 0.3f : 0.05f);
            b.torsoSamples++;
            V ui = t * (1.0f / tl);
            if (!b.haveUp) {
                b.up = Vector3(ui.x, ui.y, ui.z);
                b.haveUp = true;
            } else {
                V u(b.up);
                float k = seconds / 2.0f; // ~2 s time constant
                if (k > 1.0f)
                    k = 1.0f;
                Unit(u + (ui - u) * k, u).To(b.up);
            }
        }
    }
    if (!b.haveUp) {
        // nothing to stand the body on yet: the tracker's guesses only
        b.up.Set(0.0f, 1.0f, 0.0f);
    }
    V u(b.up);
    V r = Unit((joint[kJointHipRight] - joint[kJointHipLeft])
                   - u * Dot(joint[kJointHipRight] - joint[kJointHipLeft], u),
               V(1, 0, 0));
    V f = Cross(u, r);
    float T = b.torso;

    if (learn) {
        // A bone sample is learned only when it is plausible for this torso:
        // a tracker can call a joint tracked at a wild place, and one such
        // run teaches a leg the solve then cannot use.
        auto learnBone = [&](float &v, const V &bone, float perTorso) {
            if (Plausible(bone, perTorso * T))
                Learn(v, Len(bone), 0.05f);
        };
        for (int s = 0; s < 2; s++) {
            if (p.confidence[kHip[s]] == kT && p.confidence[kKnee[s]] == kT)
                learnBone(b.thigh[s], joint[kKnee[s]] - joint[kHip[s]], kThighPerTorso);
            if (p.confidence[kKnee[s]] == kT && p.confidence[kAnkle[s]] == kT)
                learnBone(b.shin[s], joint[kAnkle[s]] - joint[kKnee[s]], kShinPerTorso);
            if (p.confidence[kAnkle[s]] == kT && p.confidence[kFoot[s]] == kT)
                learnBone(b.foot[s], joint[kFoot[s]] - joint[kAnkle[s]], kFootPerTorso);
        }
        if (b.haveUp && hipsSeen && torsoTracked) {
            float h = Dot(hipC, u);
            if (!b.haveStand) {
                b.hipStand = h;
                b.haveStand = true;
            } else if (h > b.hipStand) {
                float rise = 0.25f * seconds; // a jump must not lift the floor
                b.hipStand += (h - b.hipStand) < rise ? (h - b.hipStand) : rise;
            } else {
                float fall = 0.02f * seconds; // a squat must not lower it either
                b.hipStand -= (b.hipStand - h) < fall ? (b.hipStand - h) : fall;
            }
            if (p.confidence[kJointAnkleLeft] == kT && p.confidence[kJointAnkleRight] == kT
                && h >= b.hipStand - 0.05f) {
                float lo = Dot(joint[kJointAnkleLeft], u);
                float lo2 = Dot(joint[kJointAnkleRight], u);
                float stand = h - (lo < lo2 ? lo : lo2);
                if (stand >= kGuessMin * kStandHipPerTorso * T && stand <= kGuessMax * kStandHipPerTorso * T)
                    Learn(b.standHip, stand, 0.05f);
            }
        }
        // holds: each tracked joint's pose relative to its anchor
        if (hipsSeen) {
            for (int j = 0; j < kNumJoints; j++) {
                int a = Anchor(j);
                if (a < 0 || p.confidence[j] != kT || missing[a])
                    continue;
                V d = joint[j] - joint[a];
                b.held[j].Set(Dot(d, r), Dot(d, u), Dot(d, f));
                b.haveHeld[j] = true;
                b.age[j] = 0.0f;
            }
        }
    }
    for (int j = 0; j < kNumJoints; j++) {
        if (p.confidence[j] != kT)
            b.age[j] += seconds;
    }
    if (numMissing == 0)
        return 0;

    V est[kNumJoints];
    for (int j = 0; j < kNumJoints; j++)
        est[j] = joint[j];
    bool kin = mMode == kKinematic && hipsSeen && b.haveUp;
    auto holdBlend = [&](int j, const V &base) -> V {
        int a = Anchor(j);
        if (a < 0 || !b.haveHeld[j] || !hipsSeen)
            return base;
        float w = std::exp(-b.age[j] / kHoldSeconds);
        const Vector3 &o = b.held[j];
        V held = est[a] + r * o.x + u * o.y + f * o.z;
        return base + (held - base) * w;
    };

    if (kin) {
        float floorUp = b.haveStand ? FloorUp(slot) : Dot(hipC, u) - kStandHipPerTorso * T;
        for (int s = 0; s < 2; s++) {
            float side = s == 0 ? -1.0f : 1.0f;
            float a = b.thigh[s] > 0.0f ? b.thigh[s] : kThighPerTorso * T;
            float sh = b.shin[s] > 0.0f ? b.shin[s] : kShinPerTorso * T;
            float ft = b.foot[s] > 0.0f ? b.foot[s] : kFootPerTorso * T;
            int H = kHip[s], K = kKnee[s], A = kAnkle[s], F = kFoot[s];
            V hip = joint[H];
            V target = hip + r * (side * kAnkleOut * T) - f * (kAnkleBack * T);
            target = target + u * (floorUp - Dot(target, u));
            V bend = Unit(f + r * (side * kKneeBendOut), f);
            Vector3 mid, end;
            // The tracker's guess for a joint out of the picture is usually
            // close (MEASURED on cropped footage: knees 0.12 m median, half
            // the solve's error), so where it is plausible -- a bone within
            // 0.5-1.6x the length learned for it -- the guess is kept as it
            // is, under the hold.  Rescaling it to the learned length was
            // closer in metres (ankles 0.177 vs 0.189 m on the knees crop)
            // but scored lower in the game (owner's YMCA 38,853 vs 40,947;
            // knees crop 4,447,405 vs 4,486,820; POSE_INPUT.md "Cut-off
            // joints").  A guess that is not plausible (or a stream with no
            // guesses) gets the solve.
            V knee = joint[K];
            bool kneeGuess = missing[K] && Plausible(knee - hip, a);
            V kneeFrom = missing[K] ? knee : joint[K];
            bool ankleGuess = missing[A] && (kneeGuess || !missing[K])
                && Plausible(joint[A] - kneeFrom, sh);
            if (kneeGuess || ankleGuess) {
                V ankle = missing[A] ? (ankleGuess ? joint[A]
                                                   : kneeFrom + Unit(target - kneeFrom, u * -1.0f) * sh)
                                     : joint[A];
                // No floor here: standing a guessed ankle on the estimated
                // floor made it worse (MEASURED, knees cut, knee error 0.12 ->
                // 0.18-0.21 m with a 5 cm slack, 0.13 with 30 cm).
                if (missing[K])
                    est[K] = holdBlend(K, knee);
                if (missing[A])
                    est[A] = holdBlend(A, ankle);
            } else if (missing[K] && missing[A]) {
                Vector3 tgt;
                target.To(tgt);
                Vector3 bendV;
                bend.To(bendV);
                Vector3 hipV;
                hip.To(hipV);
                TwoBone(hipV, tgt, a, sh, bendV, mid, end);
                est[K] = holdBlend(K, V(mid));
                // the ankle hangs from the knee as estimated (held or solved)
                V ankle = holdBlend(A, V(end));
                est[A] = est[K] + Unit(ankle - est[K], u * -1.0f) * sh;
            } else if (missing[K]) {
                Vector3 hipV, ankV, bendV;
                hip.To(hipV);
                joint[A].To(ankV);
                bend.To(bendV);
                TwoBone(hipV, ankV, a, sh, bendV, mid, end);
                est[K] = holdBlend(K, V(mid));
            } else if (missing[A]) {
                // the knee is seen: the shin points from it to the floor target
                V ankle = joint[K] + Unit(target - joint[K], u * -1.0f) * sh;
                est[A] = holdBlend(A, ankle);
            }
            if (missing[F]) {
                V dir = Unit(r * (side * kFootDirOut) + u * kFootDirUp + f * kFootDirFwd, u * -1.0f);
                V rawFoot = joint[F] - joint[A];
                // a plausible guess keeps its own offset from the ankle as
                // estimated (as the knee and ankle above keep theirs)
                if (Plausible(rawFoot, ft))
                    est[F] = est[A] + rawFoot;
                else
                    est[F] = holdBlend(F, est[A] + dir * ft);
            }
        }
        if (missing[kJointHead] && !missing[kJointShoulderCenter]) {
            float hl = kHeadPerTorso * T;
            V rawHead = joint[kJointHead] - shC;
            V dir = Plausible(rawHead, hl) ? Unit(rawHead, u) : u;
            est[kJointHead] = holdBlend(kJointHead, shC + dir * hl);
        }
    }
    // the arms (and the legs and head when the body cannot be stood up): the
    // tracker's guess, under the hold
    if (mMode == kKinematic && hipsSeen) {
        for (int s = 0; s < 2; s++) {
            const int chain[3] = {kElbow[s], kWrist[s], kHand[s]};
            for (int c = 0; c < 3; c++) {
                if (missing[chain[c]])
                    est[chain[c]] = holdBlend(chain[c], joint[chain[c]]);
            }
        }
    }

    int wrote = 0;
    for (int j = 0; j < kNumJoints; j++) {
        if (!missing[j])
            continue;
        V out = joint[j] + (est[j] - joint[j]) * (mMode == kKinematic ? mGain : 0.0f);
        out.To(p.joints[j]);
        p.confidence[j] = kConfidenceInferred;
        wrote++;
    }
    mEstimated += wrote;
    return wrote;
}

} // namespace Dc3PoseEstimate
