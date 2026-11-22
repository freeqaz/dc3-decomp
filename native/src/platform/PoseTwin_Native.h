#pragma once
#ifdef HX_NATIVE

// One body reported twice at two depths (branch gpd-menubind, PLAN P-23).
//
// MediaPipe at num_poses 2 sometimes reports one body as two poses whose head
// and shoulders lie on top of each other in the picture while the hips (and so
// the torso length, and so the depth the tracker reads from it) differ: one
// with her legs, one with them guessed.  P-17's twin rule in CentroidTracker
// compares hip centres, and a twin that has once been matched beside her is
// "two people" to it for good.  MEASURED, GPD session 2026-11-21, the results
// screens after Better Off Alone: tracker ids 67 and 68 on one dancer at
// z 1.10 and 1.48 m, head 0.02 and shoulders 0.03 apart in the picture where
// her torso is 0.32 long; on frames MediaPipe reported only one of the two,
// the menus' auto-assign bound player 1 to whichever it was, and the binding
// flipped between them every few frames (207 re-binds outside songs that
// evening, 134 of them with nobody lost).  Two dancers in that session's
// songs were never closer than 0.5 of a torso by this measure (p5 1.3-2.0).
//
// So, before People sees the frame:
//  1. two detections in one frame whose head, shoulder centre and both
//     shoulders all lie within kTwinRatio torso lengths of each other in the
//     picture are one body: the one a player is bound to is kept (the
//     caller's `bound` list, player 1's first), else the one with more joints
//     the camera saw (a tie keeps last frame's); the other is not passed on.
//     MEASURED, replay of that evening's results screen: kept by joints
//     alone, the twin player 1 was NOT bound to won for ~6 s, her own body
//     stopped reaching the game, player 2 was bound to the twin and player 1
//     sat unbound until a new tracker id came (frames with player 1 bound
//     1098 on main, ~926 with joints alone);
//  2. the dropped report's tracker id is remembered as that body's for
//     kTwinMemorySec after they were last seen together; when it is the only
//     report of her (the kept one absent) it is her -- her tracker id, and
//     scaled about the camera to the depth her kept report was last read at,
//     which leaves every joint where it is in the picture;
//  3. the two seen apart (kApartRatio) are two bodies, and the memory goes.
//
// DC3_POSE_TWIN=0 turns it off (the behaviour before).  Pure: no engine state;
// native/tests/test_native_pose_identity.cpp drives it.  NATIVE POLICY.

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace Dc3PoseTwin {

static const float kTwinRatio = 0.25f;
static const float kApartRatio = 0.5f;
static const float kTwinMemorySec = 2.0f;
static const int kMaxDetections = 6;
static const int kMaxAliases = 6;

inline bool Enabled() {
    const char *v = getenv("DC3_POSE_TWIN");
    return !(v && !strcmp(v, "0"));
}

// One detection.  Camera space, metres, joints in the game's order (the
// Kinect's: 0 hip centre, 2 shoulder centre, 3 head, 4 left shoulder,
// 8 right shoulder); `seen` = how many joints the camera itself saw
// (confidence 2).
struct View {
    int rawId = -1;
    float hip[3], shoulder[3], head[3], shoulderL[3], shoulderR[3];
    int seen = 0;
};

struct Result {
    bool drop[kMaxDetections];      // not passed on
    int rawId[kMaxDetections];      // the tracker id to pass on
    float scale[kMaxDetections];    // multiply every joint by this (1 = as is)
};

inline void Project(const float *p, float &u, float &v) {
    if (p[2] > 0.05f) {
        u = p[0] / p[2];
        v = p[1] / p[2];
    } else {
        u = p[0];
        v = p[1];
    }
}

inline float PictureDistance(const float *a, const float *b) {
    float au, av, bu, bv;
    Project(a, au, av);
    Project(b, bu, bv);
    return sqrtf((au - bu) * (au - bu) + (av - bv) * (av - bv));
}

// The largest picture distance between their head and shoulders, in torso
// lengths (shoulder centre to hip centre in the picture, the longer one's).
inline float UpperBodyRatio(const View &a, const View &b) {
    float ta = PictureDistance(a.shoulder, a.hip), tb = PictureDistance(b.shoulder, b.hip);
    float torso = ta > tb ? ta : tb;
    if (torso < 1e-3f)
        torso = 1e-3f;
    float d = PictureDistance(a.head, b.head);
    float e = PictureDistance(a.shoulder, b.shoulder);
    float f = PictureDistance(a.shoulderL, b.shoulderL);
    float g = PictureDistance(a.shoulderR, b.shoulderR);
    float m = d;
    if (e > m) m = e;
    if (f > m) m = f;
    if (g > m) m = g;
    return m / torso;
}

class Twins {
public:
    void Reset() {
        mCount = 0;
        mLastKept = -1;
    }

    // One camera frame; `dtSec` the time since the previous one.  `bound`:
    // the tracker ids (the views' rawId) of the bodies players are bound to
    // or wait for, the first the most wanted; a twin pair holding one of
    // them keeps it.
    Result Update(const View *views, int count, float dtSec, const int *bound = nullptr,
                  int boundCount = 0) {
        Result r;
        for (int i = 0; i < kMaxDetections; i++) {
            r.drop[i] = false;
            r.rawId[i] = i < count ? views[i].rawId : -1;
            r.scale[i] = 1.0f;
        }
        for (int a = 0; a < mCount; a++)
            mAlias[a].age += dtSec;
        if (!Enabled()) {
            mCount = 0;
            return r;
        }
        if (count > kMaxDetections)
            count = kMaxDetections;
        // 1 and 3: pairs in this frame.
        for (int i = 0; i < count; i++) {
            for (int j = i + 1; j < count; j++) {
                if (r.drop[i] || r.drop[j])
                    continue;
                float ratio = UpperBodyRatio(views[i], views[j]);
                if (ratio >= kTwinRatio) {
                    if (ratio >= kApartRatio) {
                        Forget(views[i].rawId, views[j].rawId);
                        Forget(views[j].rawId, views[i].rawId);
                    }
                    continue;
                }
                int keep = i, drop = j;
                int wanted = Wanted(views[i].rawId, views[j].rawId, bound, boundCount);
                if (wanted == 1)
                    keep = j, drop = i;
                else if (wanted < 0
                         && (views[j].seen > views[i].seen
                             || (views[j].seen == views[i].seen && views[j].rawId == mLastKept)))
                    keep = j, drop = i;
                r.drop[drop] = true;
                mLastKept = views[keep].rawId;
                Remember(views[drop].rawId, views[keep].rawId);
                // The kept report is a body's own, whatever it was before.
                ForgetTwin(views[keep].rawId);
            }
        }
        // Where each kept body is now (the depth a lone twin is put at).
        for (int i = 0; i < count; i++) {
            if (r.drop[i])
                continue;
            for (int a = 0; a < mCount; a++) {
                if (mAlias[a].keptId == views[i].rawId)
                    mAlias[a].keptZ = views[i].shoulder[2];
            }
        }
        for (int a = 0; a < mCount;) {
            if (mAlias[a].age > kTwinMemorySec)
                mAlias[a] = mAlias[--mCount];
            else
                a++;
        }
        // 2: a lone report of a body under the twin's tracker id.
        for (int i = 0; i < count; i++) {
            if (r.drop[i])
                continue;
            int a = Find(views[i].rawId);
            if (a < 0)
                continue;
            bool keptHere = false;
            for (int k = 0; k < count; k++) {
                if (k != i && !r.drop[k] && views[k].rawId == mAlias[a].keptId)
                    keptHere = true;
            }
            if (keptHere)
                continue;
            r.rawId[i] = mAlias[a].keptId;
            float z = views[i].shoulder[2];
            r.scale[i] = (z > 0.05f && mAlias[a].keptZ > 0.05f) ? mAlias[a].keptZ / z : 1.0f;
            mStoodIn++;
        }
        return r;
    }

    int Aliases() const { return mCount; }
    int StoodIn() const { return mStoodIn; }

private:
    // 0: `a` is the more wanted, 1: `b`, -1: neither is bound.
    static int Wanted(int a, int b, const int *bound, int boundCount) {
        for (int k = 0; bound && k < boundCount; k++) {
            if (bound[k] < 0)
                continue;
            if (bound[k] == a)
                return 0;
            if (bound[k] == b)
                return 1;
        }
        return -1;
    }
    struct Alias {
        int twinId, keptId;
        float keptZ, age;
    };
    int Find(int twinId) const {
        for (int a = 0; a < mCount; a++) {
            if (mAlias[a].twinId == twinId)
                return a;
        }
        return -1;
    }
    void Remember(int twinId, int keptId) {
        if (twinId < 0 || keptId < 0)
            return;
        int a = Find(twinId);
        if (a < 0) {
            if (mCount == kMaxAliases) {
                int oldest = 0;
                for (int b = 1; b < mCount; b++) {
                    if (mAlias[b].age > mAlias[oldest].age)
                        oldest = b;
                }
                a = oldest;
            } else {
                a = mCount++;
            }
        }
        mAlias[a].twinId = twinId;
        mAlias[a].keptId = keptId;
        mAlias[a].keptZ = 0.0f;
        mAlias[a].age = 0.0f;
    }
    void Forget(int twinId, int keptId) {
        int a = Find(twinId);
        if (a >= 0 && mAlias[a].keptId == keptId)
            mAlias[a] = mAlias[--mCount];
    }
    void ForgetTwin(int twinId) {
        int a = Find(twinId);
        if (a >= 0)
            mAlias[a] = mAlias[--mCount];
    }

    Alias mAlias[kMaxAliases];
    int mCount = 0;
    int mLastKept = -1;
    int mStoodIn = 0;
};

} // namespace Dc3PoseTwin

#endif // HX_NATIVE
