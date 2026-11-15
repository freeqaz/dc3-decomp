#pragma once
#ifdef HX_NATIVE

// Who is who in front of the camera (branch np-player-identity).
//
// The game's skeleton tracking id used to be the pose tracker's own id, and a
// player is bound to a tracking id.  MediaPipe gives no identity at all; the
// tracker on top of it (native/scripts/pose_mediapipe.py CentroidTracker, and
// its port native/web/pose/pose_geometry.js) pairs hip centres in the picture
// from frame to frame.  With two dancers MediaPipe often reports only one of
// them on a frame, and its own landmark tracking drifts from one body onto the
// other over a few frames.  Measured on a recorded two-player session (three
// songs, 5,833 camera frames, 8-10 Hz): 13 tracker ids for three bodies in one
// song; one id carried from the centre dancer onto the left one in three
// frames while the left dancer's id was dropped and the centre dancer came
// back three seconds later as somebody new.  For the game that is player 1
// dancing in player 2's body, player 2 gone for the rest of the song, and
// "New player detected!  Raise your hand to join." over a dancer who never
// left.  A Kinect does not do that: it tracks bodies in depth and its tracking
// id stays with the body.
//
// Two pieces, both pure (no engine state; native/tests/
// test_native_pose_identity.cpp drives them directly):
//
//   People   camera frame in (tracker id + hip position per person), a stable
//            id per person out.  It decides by WHERE a person is, in metres:
//            a detection belongs to the remembered person it is nearest to,
//            if it is within that person's reach; the tracker's id only breaks
//            ties.  A person who is not seen is remembered for the reacquire
//            window, and a body that turns up within reach of where she stood
//            is her again -- same id, so same slot, same player, same score.
//
//   Players  which skeleton each of the two players is bound to.  The image
//            unbinds a player whose skeleton is gone for longer than the
//            dropout hold plus its own recovery second.  This remembers where
//            the player stood for the reacquire window and binds her again, to
//            her own id when it returns, or to a body that appeared after she
//            was lost close to where she stood -- in every mode, not only the
//            two-player ones.  A body that was already in view, or is too far
//            away, or comes after the window, is not her: that one joins the
//            way the image has people join (raise a hand).
//
// DC3_POSE_REACQUIRE_MS is the window (default kDefaultReacquireMs).  0 is the
// strict behaviour from before this branch: tracker ids are the tracking ids,
// nothing is remembered, player 1 is auto-assigned the first free skeleton and
// player 2 only in a two-player mode.  NATIVE POLICY.

#include <cmath>
#include <cstdlib>

namespace Dc3PoseIdentity {

static const int kDefaultReacquireMs = 5000;
static const int kMaxReacquireMs = 30000;

// DC3_POSE_REACQUIRE_MS, read per call (cheap) so a test can change it.
inline int ReacquireMs() {
    const char *v = getenv("DC3_POSE_REACQUIRE_MS");
    if (!v || !*v)
        return kDefaultReacquireMs;
    char *end = nullptr;
    long ms = strtol(v, &end, 10);
    if (end == v || *end != '\0' || ms < 0)
        return kDefaultReacquireMs;
    return ms > kMaxReacquireMs ? kMaxReacquireMs : (int)ms;
}

// ---------------------------------------------------------------------------
// People
// ---------------------------------------------------------------------------

// How far a person can be from where she was last seen and still be her.
// Measured on the recording, same tracker id, frames under 0.3 s apart: the
// hip moves under 0.08 m on 95% of frames and under 0.27 m on 99%; a tracker
// id that jumps to the neighbour moves 0.3-0.6 m in one 0.1 s frame.  Two
// dancers side by side are 0.43 m apart at the 5th percentile, 0.81 m at the
// median.  So: kReachBase at once, growing by kReachSpeed for every second
// she has not been seen, up to kReach (under the 5th-percentile neighbour) --
// or up to kReachSameId when the tracker still calls her by the same id.
// kReachBase 0.30 took a bystander 0.41 m away for the dancer once in the
// recording; 0.25 does not.
static const float kReachBase = 0.25f;
static const float kReachSpeed = 0.5f; // metres per second unseen
static const float kReach = 0.45f;
static const float kReachSameId = 1.5f;
// A person is also looked for where she was a moment ago (the anchor: her
// position at least kAnchorSec before the last one), with the reach that much
// time gives.  MediaPipe puts a dancer's detection on the bystander 0.4 m
// away for one frame, or walks it onto her neighbour over three; the last
// position is then somewhere she never was, and without the anchor she came
// back from it as somebody new (once in 50 s of the recording).
static const float kAnchorSec = 0.5f;
// What the tracker's opinion is worth: the same id makes a person this much
// nearer.  Small on purpose -- the tracker's id is what slides.
static const float kSameIdBonus = 0.05f;
// Depth from one camera is noisy and a squat moves the hip down: both count
// half.
static const float kDepthWeight = 0.5f;
static const float kHeightWeight = 0.5f;

static const int kMaxDetections = 6; // NativeSkeletonProvider::kMaxPersons
static const int kMaxPeople = 16;
// Ids this layer makes up when the tracker's id is somebody else's already.
static const int kPrivateIdBase = 1 << 20;

struct Detection {
    int rawId; // the tracker's id; < 0 = an untracked detection, passed through
    float x, y, z; // hip centre, camera space, metres
};

inline float Distance(float ax, float ay, float az, float bx, float by, float bz) {
    float dx = ax - bx, dy = kHeightWeight * (ay - by), dz = kDepthWeight * (az - bz);
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

class People {
public:
    void Reset() {
        mCount = 0;
        mNextPrivate = kPrivateIdBase;
    }

    // One camera frame.  `dtSec` is the sensor time since the previous one;
    // `windowMs` the reacquire window (0: off, ids pass through and nothing is
    // remembered).  stableIds[i] is the tracking id for dets[i].
    void Update(const Detection *dets, int count, float dtSec, int windowMs, int *stableIds) {
        if (count > kMaxDetections)
            count = kMaxDetections;
        if (windowMs <= 0) {
            Reset();
            for (int i = 0; i < count; i++)
                stableIds[i] = dets[i].rawId;
            return;
        }
        if (dtSec < 0.0f)
            dtSec = 0.0f;
        const float windowSec = windowMs * 0.001f;
        int kept = 0;
        for (int p = 0; p < mCount; p++) {
            mPeople[p].unseenSec += dtSec;
            mPeople[p].anchorSec += dtSec;
            if (mPeople[p].unseenSec <= windowSec)
                mPeople[kept++] = mPeople[p];
        }
        mCount = kept;

        // Every remembered person each detection could be, with what it costs.
        Search search;
        search.count = count;
        for (int i = 0; i < count; i++) {
            search.numCand[i] = 0;
            if (dets[i].rawId < 0)
                continue;
            for (int p = 0; p < mCount; p++) {
                const Person &who = mPeople[p];
                bool sameId = who.rawId == dets[i].rawId;
                float cap = sameId ? kReachSameId : kReach;
                float reach = kReachBase + kReachSpeed * who.unseenSec;
                float d = Distance(dets[i].x, dets[i].y, dets[i].z, who.x, who.y, who.z);
                bool within = d <= (reach < cap ? reach : cap);
                if (who.hasAnchor) {
                    float anchorReach = kReachBase + kReachSpeed * who.anchorSec;
                    float da = Distance(dets[i].x, dets[i].y, dets[i].z, who.ax, who.ay, who.az);
                    if (da <= (anchorReach < cap ? anchorReach : cap) && (!within || da < d)) {
                        within = true;
                        d = da;
                    }
                }
                if (!within)
                    continue;
                int n = search.numCand[i]++;
                search.cand[i][n] = p;
                search.cost[i][n] = d - (sameId ? kSameIdBonus : 0.0f);
            }
        }
        // The pairing that leaves the fewest detections new, then the least
        // total distance (at most six detections with a handful of candidates
        // each: exhaustive).
        search.best = 1e30f;
        for (int i = 0; i < count; i++)
            search.pick[i] = search.bestPick[i] = -1;
        for (int p = 0; p < mCount; p++)
            search.used[p] = false;
        Solve(search, 0, 0.0f);

        bool seen[kMaxPeople];
        for (int p = 0; p < kMaxPeople; p++)
            seen[p] = false;
        for (int i = 0; i < count; i++) {
            if (dets[i].rawId < 0) {
                stableIds[i] = dets[i].rawId;
                continue;
            }
            int p = search.bestPick[i];
            if (p < 0) {
                p = NewPerson(dets[i].rawId, seen);
            } else {
                Person &known = mPeople[p];
                if (known.rawId != dets[i].rawId)
                    mReacquired++;
                // Where she was before this sighting becomes the anchor once
                // the anchor she has is old enough.
                if (!known.hasAnchor || known.anchorSec >= kAnchorSec) {
                    known.hasAnchor = true;
                    known.ax = known.x;
                    known.ay = known.y;
                    known.az = known.z;
                    known.anchorSec = known.unseenSec;
                }
                // A sighting is worth the time since the last one, at most a
                // slow camera's frame.
                known.seenSec += known.unseenSec < kSightingSec ? known.unseenSec : kSightingSec;
            }
            Person &who = mPeople[p];
            who.rawId = dets[i].rawId;
            who.x = dets[i].x;
            who.y = dets[i].y;
            who.z = dets[i].z;
            who.unseenSec = 0.0f;
            seen[p] = true;
            stableIds[i] = who.id;
        }
        // A tracker id is one person's: whoever it was that it used to mean,
        // it means the person it is on now.
        for (int p = 0; p < mCount; p++) {
            if (seen[p])
                continue;
            for (int q = 0; q < mCount; q++) {
                if (seen[q] && mPeople[q].rawId == mPeople[p].rawId)
                    mPeople[p].rawId = -1;
            }
        }
    }

    // The stable id the tracker's id means now (the camera stream's person
    // masks are labelled with tracker ids); the id itself if nobody has it.
    int StableForRaw(int rawId) const {
        for (int p = 0; p < mCount; p++) {
            if (mPeople[p].rawId == rawId)
                return mPeople[p].id;
        }
        return rawId;
    }

    // How long the person with this tracking id has actually been seen, in
    // seconds (0: unknown or just arrived).  The dropout hold asks: someone
    // seen for half a second is a person, not a one-frame false detection,
    // whichever slot she is in now.
    float SeenSec(int id) const {
        for (int p = 0; p < mCount; p++) {
            if (mPeople[p].id == id)
                return mPeople[p].seenSec;
        }
        return 0.0f;
    }

    int Remembered() const { return mCount; }
    // Times a person was recognised under a different tracker id.
    int Reacquired() const { return mReacquired; }

private:
    struct Person {
        int id;    // the tracking id the game sees
        int rawId; // the tracker id she was last seen under, -1 = none now
        float x, y, z;   // where she was last seen
        float unseenSec; // how long ago
        bool hasAnchor;
        float ax, ay, az; // where she was a moment before that (kAnchorSec)
        float anchorSec;  // how long ago
        float seenSec;    // how long she has been seen in all
    };
    static constexpr float kSightingSec = 0.2f;
    struct Search {
        int count;
        int numCand[kMaxDetections];
        int cand[kMaxDetections][kMaxPeople];
        float cost[kMaxDetections][kMaxPeople];
        bool used[kMaxPeople];
        int pick[kMaxDetections], bestPick[kMaxDetections];
        float best;
    };
    static constexpr float kNewCost = 100.0f;

    static void Solve(Search &s, int i, float cost) {
        if (cost >= s.best)
            return;
        if (i == s.count) {
            s.best = cost;
            for (int k = 0; k < s.count; k++)
                s.bestPick[k] = s.pick[k];
            return;
        }
        for (int n = 0; n < s.numCand[i]; n++) {
            int p = s.cand[i][n];
            if (s.used[p])
                continue;
            s.used[p] = true;
            s.pick[i] = p;
            Solve(s, i + 1, cost + s.cost[i][n]);
            s.used[p] = false;
        }
        s.pick[i] = -1;
        Solve(s, i + 1, cost + kNewCost);
    }

    int NewPerson(int rawId, bool *seen) {
        int p = mCount;
        if (mCount < kMaxPeople) {
            mCount++;
        } else {
            // Full: forget whoever has been gone longest.
            p = -1;
            for (int q = 0; q < mCount; q++) {
                if (!seen[q] && (p < 0 || mPeople[q].unseenSec > mPeople[p].unseenSec))
                    p = q;
            }
            if (p < 0)
                p = 0;
        }
        // Her tracking id is the tracker's id unless somebody remembered
        // already goes by that number.
        int id = rawId;
        for (int q = 0; q < mCount; q++) {
            if (q != p && mPeople[q].id == rawId)
                id = -1;
        }
        if (id < 0)
            id = mNextPrivate++;
        mPeople[p].id = id;
        mPeople[p].rawId = rawId;
        mPeople[p].unseenSec = 0.0f;
        mPeople[p].hasAnchor = false;
        mPeople[p].anchorSec = 0.0f;
        mPeople[p].seenSec = 0.0f;
        return p;
    }

    Person mPeople[kMaxPeople];
    int mCount = 0;
    int mNextPrivate = kPrivateIdBase;
    int mReacquired = 0;
};

// ---------------------------------------------------------------------------
// Players
// ---------------------------------------------------------------------------

static const int kSlots = 6;   // NUM_SKELETONS
static const int kPlayers = 2;

// A lost player is bound to a body that APPEARED AFTER she was lost and stands
// within this of where she stood: kRebindBase at once, growing by kRebindSpeed
// a second, up to kRebindMax.  Wider than People's reach, because by now the
// dropout hold and the image's recovery second have both run out and she has
// had that long to move -- and narrower in what it accepts, because a body
// that was in view all along is never a candidate.
static const float kRebindBase = 0.3f;
static const float kRebindSpeed = 0.5f;
static const float kRebindMax = 0.75f;
// "Appeared after she was lost": after her skeleton was last really seen.  The
// slot goes on reporting her for the dropout hold after that, so the caller
// passes the hold; this is a few frames of slack on top, for the order things
// arrive in.
static const float kFreshSlackMs = 250.0f;
// A player the old rule auto-assigns (player 1; player 2 in a two-player
// mode) waits this long for her own body before she takes the nearest one
// whoever it is.  Under the image's recovery second on purpose: past that the
// image unbinds her and Game::CheckForSkeletonLoss pauses the song, which for
// one dancer whom the camera lost and found somewhere else is worse than
// being generous.
static const float kAutoWaitMs = 700.0f;

struct SlotView {
    bool tracked = false; // a skeleton is in this slot
    bool valid = false;   // and it passes the image's quality filter
    int id = -1;          // its tracking id
    float x = 0.0f, z = 0.0f; // hip centre, camera space, metres
};

enum Rule {
    kRuleNone = 0,
    kRuleSameId, // her own tracking id came back
    kRuleNear,   // a body that appeared where she stood after she was lost
    kRuleAuto,   // the auto-assign: first join, or nobody better
};

inline const char *RuleName(int rule) {
    switch (rule) {
    case kRuleSameId: return "same-id";
    case kRuleNear: return "near";
    case kRuleAuto: return "auto";
    default: return "none";
    }
}

class Players {
public:
    void Reset() {
        for (int p = 0; p < kPlayers; p++)
            mMem[p] = Memory();
        for (int s = 0; s < kSlots; s++) {
            mSlotId[s] = -1;
            mSlotAgeMs[s] = 0.0f;
        }
    }

    struct Result {
        int slot[kPlayers];   // the slot each player is on after this, -1 = none
        int assign[kPlayers]; // the tracking id to bind her to now, -1 = no change
        int rule[kPlayers];   // why (Rule), for an assign
        float distance[kPlayers]; // how far from where she stood (kRuleNear / kRuleAuto with memory)
        float lostMs[kPlayers];   // how long she had been lost
    };

    // One sensor frame.  boundId[p] is the tracking id player p holds (-1 =
    // none), `autoPlayers` how many players the auto-assign may bind without
    // being asked (1, or 2 in a two-player mode), `windowMs` the reacquire
    // window (0 = strict: no memory, first free slot), `holdMs` the dropout
    // hold (how long a slot kept reporting a skeleton nobody saw).
    Result Update(const SlotView *slots, const int *boundId, int autoPlayers, float elapsedMs,
                  int windowMs, int holdMs = 0) {
        Result r;
        for (int p = 0; p < kPlayers; p++) {
            r.slot[p] = -1;
            r.assign[p] = -1;
            r.rule[p] = kRuleNone;
            r.distance[p] = -1.0f;
            r.lostMs[p] = 0.0f;
        }
        // How long each slot's skeleton has been there.
        for (int s = 0; s < kSlots; s++) {
            if (slots[s].tracked && slots[s].id == mSlotId[s]) {
                mSlotAgeMs[s] += elapsedMs;
            } else {
                mSlotId[s] = slots[s].tracked ? slots[s].id : -1;
                mSlotAgeMs[s] = 0.0f;
            }
        }
        for (int p = 0; p < kPlayers; p++) {
            if (boundId[p] >= 0)
                r.slot[p] = Find(slots, boundId[p]);
        }
        if (windowMs <= 0) {
            for (int p = 0; p < kPlayers; p++)
                mMem[p] = Memory();
        } else {
            for (int p = 0; p < kPlayers; p++) {
                Memory &m = mMem[p];
                if (r.slot[p] >= 0) {
                    m.known = true;
                    m.lost = false;
                    m.id = boundId[p];
                    m.x = slots[r.slot[p]].x;
                    m.z = slots[r.slot[p]].z;
                    m.lostMs = 0.0f;
                } else if (m.known) {
                    bool inView = Find(slots, m.id) >= 0;
                    if (inView && !m.lost) {
                        // Unbound while her skeleton was in view: the image's
                        // own decision (the chooser, a one-player mode).  Not
                        // a loss, and not ours to undo.
                        m.known = false;
                    } else {
                        if (!inView)
                            m.lost = true;
                        m.lostMs += elapsedMs;
                        if (m.lostMs > (float)windowMs)
                            m.known = false;
                    }
                }
            }
        }

        bool claimed[kSlots];
        for (int s = 0; s < kSlots; s++)
            claimed[s] = (r.slot[0] == s || r.slot[1] == s);

        // 1. A lost player whose own tracking id is back.
        for (int p = 0; p < kPlayers; p++) {
            const Memory &m = mMem[p];
            if (r.slot[p] >= 0 || !m.known || !m.lost)
                continue;
            int s = Find(slots, m.id);
            if (s >= 0 && !claimed[s])
                Bind(r, claimed, slots, boundId, p, s, kRuleSameId, 0.0f);
        }
        // 2. A lost player and a body that appeared, after she was lost, near
        //    where she stood.  Nearest pair first.
        for (int round = 0; round < kPlayers; round++) {
            int bestP = -1, bestS = -1;
            float bestD = 0.0f;
            for (int p = 0; p < kPlayers; p++) {
                const Memory &m = mMem[p];
                if (r.slot[p] >= 0 || !m.known || !m.lost)
                    continue;
                float reach = kRebindBase + kRebindSpeed * m.lostMs * 0.001f;
                if (reach > kRebindMax)
                    reach = kRebindMax;
                for (int s = 0; s < kSlots; s++) {
                    if (!slots[s].valid || claimed[s] || mSlotAgeMs[s] > m.lostMs + (float)holdMs + kFreshSlackMs)
                        continue;
                    float d = Flat(m, slots[s]);
                    if (d <= reach && (bestP < 0 || d < bestD)) {
                        bestP = p;
                        bestS = s;
                        bestD = d;
                    }
                }
            }
            if (bestP < 0)
                break;
            Bind(r, claimed, slots, boundId, bestP, bestS, kRuleNear, bestD);
        }
        // 3. The auto-assign: a player nobody has to ask for takes a free
        //    skeleton, a quality-filter-valid one first -- the one nearest to
        //    where she stood if that is known, else the first.
        for (int pass = 0; pass < 2; pass++) {
            bool requireValid = (pass == 0);
            for (int p = 0; p < autoPlayers && p < kPlayers; p++) {
                if (r.slot[p] >= 0)
                    continue;
                const Memory &m = mMem[p];
                if (m.known && m.lost && m.lostMs < kAutoWaitMs)
                    continue; // still waiting for her own body
                int best = -1;
                float bestD = -1.0f;
                for (int s = 0; s < kSlots; s++) {
                    if (requireValid ? !slots[s].valid : !slots[s].tracked)
                        continue;
                    if (claimed[s])
                        continue;
                    if (!m.known) {
                        best = s;
                        break;
                    }
                    float d = Flat(m, slots[s]);
                    if (best < 0 || d < bestD) {
                        best = s;
                        bestD = d;
                    }
                }
                if (best >= 0)
                    Bind(r, claimed, slots, boundId, p, best, kRuleAuto, bestD);
            }
        }
        return r;
    }

    bool Remembered(int p) const { return mMem[p].known; }
    bool Lost(int p) const { return mMem[p].known && mMem[p].lost; }

private:
    struct Memory {
        bool known = false; // she was bound, to `id`, at (x, z)
        bool lost = false;  // and her skeleton has been out of view since
        int id = -1;
        float x = 0.0f, z = 0.0f;
        float lostMs = 0.0f;
    };

    static int Find(const SlotView *slots, int id) {
        for (int s = 0; s < kSlots; s++) {
            if (slots[s].tracked && slots[s].id == id)
                return s;
        }
        return -1;
    }
    static float Flat(const Memory &m, const SlotView &slot) {
        float dx = slot.x - m.x, dz = kDepthWeight * (slot.z - m.z);
        return sqrtf(dx * dx + dz * dz);
    }
    void Bind(Result &r, bool *claimed, const SlotView *slots, const int *boundId, int p, int s,
              int rule, float distance) {
        r.slot[p] = s;
        claimed[s] = true;
        Memory &m = mMem[p];
        if (boundId[p] != slots[s].id) {
            r.assign[p] = slots[s].id;
            r.rule[p] = rule;
            r.distance[p] = distance;
            r.lostMs[p] = m.known ? m.lostMs : 0.0f;
        }
        // She counts as bound from here.  If the image unbinds her again with
        // the skeleton in view, that is its decision (see Update) -- one
        // re-bind per loss, never one per frame.
        if (rule != kRuleAuto && m.known) {
            m.id = slots[s].id;
            m.lost = false;
            m.lostMs = 0.0f;
            m.x = slots[s].x;
            m.z = slots[s].z;
        }
    }

    Memory mMem[kPlayers];
    int mSlotId[kSlots] = { -1, -1, -1, -1, -1, -1 };
    float mSlotAgeMs[kSlots] = { 0, 0, 0, 0, 0, 0 };
};

} // namespace Dc3PoseIdentity

#endif // HX_NATIVE
