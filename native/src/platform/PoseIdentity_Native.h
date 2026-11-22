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
// THE PICTURE MOVED.  Nobody moves further than her reach between two frames;
// a camera that is nudged, or re-aimed, moves everybody in it at once.  When
// every person seen in the last frame is in this one under the same tracker
// id, nobody else is, no detection is where somebody else was, and they have
// all moved the same way (to within kMovedAgree) further than reach allows,
// the people are who the tracker says and it is the picture that moved: they
// keep their ids, and where everybody was is moved along with them.  Only
// between consecutive frames (kMovedFrameSec); a detection within somebody
// else's reach is decided by position as always (that is the tracker id
// sliding onto the neighbour, which is what this layer is for).
// Two people or more: one person alone has nobody to agree with (below).
// The synthetic sensor moves the picture several times a song: it stands
// every body up at (x, 0.9, 2.5) whenever the game has no move to show and
// puts them back at the move's position (y ~0.05, z ~2.0) when it has, all in
// one frame under the same ids.  Without this rule each of those made
// everybody somebody new, and with two people in view player 1 changed
// tracking id mid-song.
//
// ONE BODY ALONE.  The only person seen in the last frame, and the only
// detection in this one, under the same tracker id, out of her reach and out
// of everybody else's: she is still her -- one body in view is one person,
// one slot, one player.  The tracker's id by itself is not proof, so the jump
// is on trial for kAnchorSec: where she was before it becomes her anchor, and
// a body that turns up there in that time, and is nobody else, is her -- with
// the body that jumped still in view too, which is then somebody new.
// In the recorded session, over the whole evening (5,964 camera frames), a
// lone body jumps 4 times.  Three are one person whose depth estimate jumps
// as she walks in (0.6 to 3 m in a frame at 5-7 m from the camera): without
// this rule she was three people.  The fourth (song 3, 132.7 s) is the
// dancer's tracker id on somebody walking past 0.5 m away for one frame, with
// the dancer back under a new id in the next: the passer-by is the dancer for
// that one frame, as the tracker said, and the dancer is found at the anchor
// and is herself again.  (Moving the anchor along with her, as the picture
// rule does, left the passer-by with the dancer's identity, player 1 and her
// score, for the rest of the song: measured.)
// Without this rule a lone body that jumped was somebody new unless her
// anchor happened to be old enough to reach her.  BoxymanTest's one body
// jumps 0.48 m when the first move starts: a second person, in a second slot,
// under "New player detected!", in most runs.
// What is left: a dancer who goes undetected for longer than kAnchorSec while
// the tracker's id for her sits on somebody else out of her reach has given
// that body her identity.
static const float kMovedAgree = 0.1f;
static const float kMovedFrameSec = 0.5f;

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
            mPeople[p].ageSec += dtSec;
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
        for (int i = 0; i < count; i++)
            search.pick[i] = search.bestPick[i] = -1;
        if (PictureMoved(dets, count, dtSec, search)) {
            // bestPick is each detection's own person, moved with the picture.
            mMoved++;
        } else if (OneBodyAlone(dets, count, dtSec, search)) {
            // bestPick is the one person there is; her anchor is where she was.
            mJumped++;
        } else {
            // The pairing that leaves the fewest detections new, then the
            // least total distance (at most six detections with a handful of
            // candidates each: exhaustive).
            search.best = 1e30f;
            for (int p = 0; p < mCount; p++)
                search.used[p] = false;
            Solve(search, 0, 0.0f);
            JumpOnTrial(dets, count, search);
        }

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
                // the anchor she has is old enough (and a jump's trial is over).
                if (!known.hasAnchor || known.anchorSec >= kAnchorSec) {
                    known.hasAnchor = true;
                    known.ax = known.x;
                    known.ay = known.y;
                    known.az = known.z;
                    known.anchorSec = known.unseenSec;
                    known.jumped = false;
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
        for (int p = 0; p < mCount; p++)
            mPeople[p].seenLast = seen[p];
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

    // The tracker id the person with this tracking id was last seen under
    // (-1: nobody, or none now) -- the twin pass (PoseTwin_Native.h) works
    // in tracker ids and asks which ones the players are bound to.
    int RawForStable(int id) const {
        if (id < 0)
            return -1;
        for (int p = 0; p < mCount; p++) {
            if (mPeople[p].id == id)
                return mPeople[p].rawId;
        }
        return -1;
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
    // How long ago the person with this tracking id was first seen, in
    // seconds (-1: nobody known).  Players asks: a body that was there before
    // a player was lost is not that player, whatever slot it is in now.
    float AgeSec(int id) const {
        for (int p = 0; p < mCount; p++) {
            if (mPeople[p].id == id)
                return mPeople[p].ageSec;
        }
        return -1.0f;
    }

    int Remembered() const { return mCount; }
    // Times a person was recognised under a different tracker id.
    int Reacquired() const { return mReacquired; }
    // Times the picture moved under the people in it (kMovedAgree).
    int Moved() const { return mMoved; }
    // Times one body alone jumped out of reach and stayed who she was.
    int Jumped() const { return mJumped; }

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
        float ageSec;     // how long ago she was first seen
        bool seenLast;    // she was in the last frame
        bool jumped;      // ONE BODY ALONE: the anchor is where she jumped from
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

    // See kMovedAgree.  True: s.bestPick[i] is the person each detection is,
    // and everybody remembered has been moved the way the picture went.
    bool PictureMoved(const Detection *dets, int count, float dtSec, Search &s) {
        if (count < 2 || dtSec > kMovedFrameSec)
            return false;
        int inLast = 0;
        for (int p = 0; p < mCount; p++) {
            if (mPeople[p].seenLast)
                inLast++;
        }
        if (inLast != count)
            return false;
        int own[kMaxDetections];
        bool beyondReach = false;
        for (int i = 0; i < count; i++) {
            own[i] = -1;
            if (dets[i].rawId < 0)
                return false;
            for (int p = 0; p < mCount; p++) {
                if (mPeople[p].seenLast && mPeople[p].rawId == dets[i].rawId)
                    own[i] = p;
            }
            if (own[i] < 0)
                return false; // somebody new, so somebody is missing
            for (int k = 0; k < i; k++) {
                if (own[k] == own[i])
                    return false;
            }
            // Where somebody else was: position decides.
            for (int n = 0; n < s.numCand[i]; n++) {
                if (s.cand[i][n] != own[i])
                    return false;
            }
            if (s.numCand[i] == 0)
                beyondReach = true;
        }
        if (!beyondReach)
            return false; // everybody is within her own reach: nothing moved
        float sx = 0.0f, sy = 0.0f, sz = 0.0f;
        for (int i = 0; i < count; i++) {
            const Person &who = mPeople[own[i]];
            float dx = dets[i].x - who.x, dy = dets[i].y - who.y, dz = dets[i].z - who.z;
            if (i == 0) {
                sx = dx;
                sy = dy;
                sz = dz;
            } else if (Distance(dx, dy, dz, sx, sy, sz) > kMovedAgree) {
                return false; // they went different ways: people, not the picture
            }
        }
        // Where everybody was -- the people in this frame, and the ones
        // remembered and not seen -- in the picture as it is now.
        for (int p = 0; p < mCount; p++) {
            Person &who = mPeople[p];
            float dx = sx, dy = sy, dz = sz;
            for (int i = 0; i < count; i++) {
                if (own[i] == p) {
                    dx = dets[i].x - who.x;
                    dy = dets[i].y - who.y;
                    dz = dets[i].z - who.z;
                }
            }
            who.x += dx;
            who.y += dy;
            who.z += dz;
            if (who.hasAnchor) {
                who.ax += dx;
                who.ay += dy;
                who.az += dz;
            }
        }
        for (int i = 0; i < count; i++)
            s.bestPick[i] = own[i];
        return true;
    }

    // See ONE BODY ALONE.  True: s.bestPick[0] is her, her anchor is where she
    // was before this frame, and the jump is on trial.
    bool OneBodyAlone(const Detection *dets, int count, float dtSec, Search &s) {
        if (count != 1 || dets[0].rawId < 0 || dtSec > kMovedFrameSec)
            return false;
        if (s.numCand[0] != 0)
            return false; // within somebody's reach: position decides
        int her = -1;
        for (int p = 0; p < mCount; p++) {
            if (!mPeople[p].seenLast)
                continue;
            if (her >= 0)
                return false; // two were in view a frame ago: one is missing
            her = p;
        }
        if (her < 0 || mPeople[her].rawId != dets[0].rawId)
            return false;
        Person &who = mPeople[her];
        who.hasAnchor = true;
        who.ax = who.x;
        who.ay = who.y;
        who.az = who.z;
        who.anchorSec = who.unseenSec;
        who.jumped = true;
        s.bestPick[0] = her;
        return true;
    }

    // See ONE BODY ALONE.  The pairing has a person whose jump is on trial on
    // the body that jumped (it is the nearer one, and has her tracker id) and
    // a body where she jumped from that is nobody: that one is her.
    void JumpOnTrial(const Detection *dets, int count, Search &s) {
        for (int i = 0; i < count; i++) {
            int p = s.bestPick[i];
            if (p < 0 || !mPeople[p].jumped)
                continue;
            const Person &who = mPeople[p];
            float reach = kReachBase + kReachSpeed * who.anchorSec;
            if (reach > kReach)
                reach = kReach;
            float here = Distance(dets[i].x, dets[i].y, dets[i].z, who.ax, who.ay, who.az);
            for (int k = 0; k < count; k++) {
                if (k == i || s.bestPick[k] >= 0 || dets[k].rawId < 0)
                    continue;
                float da = Distance(dets[k].x, dets[k].y, dets[k].z, who.ax, who.ay, who.az);
                if (da <= reach && da < here) {
                    s.bestPick[k] = p;
                    s.bestPick[i] = -1;
                    break;
                }
            }
        }
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
        mPeople[p].ageSec = 0.0f;
        mPeople[p].seenLast = false;
        mPeople[p].jumped = false;
        return p;
    }

    Person mPeople[kMaxPeople];
    int mCount = 0;
    int mNextPrivate = kPrivateIdBase;
    int mReacquired = 0;
    int mMoved = 0;
    int mJumped = 0;
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
// being generous.  If the image unbinds her before that anyway, the wait is
// over there and then.
// Whoever she takes then is a STAND-IN: the song goes on, and the player
// herself is still looked for until the window ends -- her own tracking id,
// or a new body where she stood, takes the binding back.  (Song 3 of the
// recorded session: the camera lost the left dancer, who was player 1, for
// 1.7 s four measures in; the right dancer, who never joined, was bound in
// her place and kept the binding and the score for 41 measures while the
// player danced on unbound.)  Only past the window is the stand-in the player.
static const float kAutoWaitMs = 700.0f;

// LET GO (branch gpd-menubind, PLAN P-24).  In song select the image unbinds
// player 1 -- MEASURED on the GPD session of 2026-11-21, on about half of all
// sensor frames, mostly with her skeleton in view -- and the auto-assign used
// to bind her again at once to whatever free skeleton the frame held: the one
// that passes the quality filter first, and her own, held through a dropout,
// often does not.  MediaPipe at num_poses 2 often reports one of two people a
// frame, so that was the other person as often as not: replay menu24,
// tracking ids at z 3.4 and 1.5 m took the binding (and the menu's hand
// cursor) in turn.  And the image let go of her in view, she is not
// remembered (see Update), so on the next frame her skeleton misses -- the
// next frame, often -- the binding the image still holds counts for nothing
// and the other person was bound at once too.  A stand-in hops the same way.
// So, outside a song: for kReleaseWaitMs after the skeleton she was bound to
// was last in the frame, the auto-assign binds her only to a skeleton within
// kRebindMax of where it was (her own, or the same body under a new id); a
// farther one waits for that time.  Rules 1 and 2 (her own id, a fresh body
// where she stood) come first as before.  Not in a song: there Game::CheckForSkeletonLoss
// pauses on the first frame nobody is bound, and a wrong body for a few frames
// is the lesser harm.  DC3_POSE_RELEASE_WAIT_MS sets the wait; 0 is the
// behaviour before this.
static const int kDefaultReleaseWaitMs = 700;

inline int ReleaseWaitMs() {
    const char *v = getenv("DC3_POSE_RELEASE_WAIT_MS");
    if (!v || !*v)
        return kDefaultReleaseWaitMs;
    char *end = nullptr;
    long ms = strtol(v, &end, 10);
    if (end == v || *end != '\0' || ms < 0)
        return kDefaultReleaseWaitMs;
    return ms > kMaxReacquireMs ? kMaxReacquireMs : (int)ms;
}

struct SlotView {
    bool tracked = false; // a skeleton is in this slot
    bool valid = false;   // and it passes the image's quality filter
    int id = -1;          // its tracking id
    float x = 0.0f, z = 0.0f; // hip centre, camera space, metres
    // How long ago this person was first seen (People::AgeSec, ms); -1 = not
    // known, and Players uses the time the id has been in this slot -- which
    // starts again whenever a slot is given up and taken back.
    float ageMs = -1.0f;
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
        for (int p = 0; p < kPlayers; p++)
            mRelease[p] = Release();
    }

    struct Result {
        int slot[kPlayers];   // the slot each player is on after this, -1 = none
        int assign[kPlayers]; // the tracking id to bind her to now, -1 = no change
        int rule[kPlayers];   // why (Rule), for an assign
        float distance[kPlayers]; // how far from where she stood (kRuleNear / kRuleAuto with memory)
        float lostMs[kPlayers];   // how long she had been lost
        // The stand-in's tracking id when this assign takes the player off a
        // stand-in and back to herself, else -1.  The stand-in was never the
        // player: whoever keeps player ids beside the binding (the image's
        // GestureMgr / SkeletonChooser) has to forget this one, or she is
        // left looking like somebody who joined.
        int released[kPlayers];
        // Not bound this frame because every free skeleton is too far from
        // where her bound skeleton was last seen (kReleaseWaitMs).
        bool waiting[kPlayers];
        // For an assign: how far the body is from where her bound skeleton
        // was last seen, and how long ago that was (-1 = never seen).
        float fromBound[kPlayers];
        float sinceBoundMs[kPlayers];
    };

    // One sensor frame.  boundId[p] is the tracking id player p holds (-1 =
    // none), `autoPlayers` how many players the auto-assign may bind without
    // being asked (1, or 2 in a two-player mode), `windowMs` the reacquire
    // window (0 = strict: no memory, first free slot), `holdMs` the dropout
    // hold (how long a slot kept reporting a skeleton nobody saw),
    // `releaseWaitMs` the let-go-in-view wait (0 = none: in a song, or off).
    Result Update(const SlotView *slots, const int *boundId, int autoPlayers, float elapsedMs,
                  int windowMs, int holdMs = 0, int releaseWaitMs = 0) {
        Result r;
        for (int p = 0; p < kPlayers; p++) {
            r.slot[p] = -1;
            r.assign[p] = -1;
            r.rule[p] = kRuleNone;
            r.distance[p] = -1.0f;
            r.lostMs[p] = 0.0f;
            r.released[p] = -1;
            r.waiting[p] = false;
            r.fromBound[p] = -1.0f;
            r.sinceBoundMs[p] = -1.0f;
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
        // Where the skeleton she is bound to was last in the frame, and how
        // long ago (kReleaseWaitMs).
        for (int p = 0; p < kPlayers; p++) {
            Release &rel = mRelease[p];
            if (r.slot[p] >= 0) {
                rel.seen = true;
                rel.x = slots[r.slot[p]].x;
                rel.z = slots[r.slot[p]].z;
                rel.ms = 0.0f;
            } else {
                rel.ms += elapsedMs;
            }
        }
        if (windowMs <= 0) {
            for (int p = 0; p < kPlayers; p++)
                mMem[p] = Memory();
        } else {
            for (int p = 0; p < kPlayers; p++) {
                Memory &m = mMem[p];
                if (r.slot[p] >= 0) {
                    const SlotView &on = slots[r.slot[p]];
                    m.placed = true;
                    m.px = on.x;
                    m.pz = on.z;
                    if (OnStandIn(p, boundId)) {
                        // The song goes on with a stand-in; she is still lost.
                        m.lostMs += elapsedMs;
                        if (m.lostMs <= (float)windowMs)
                            continue;
                    }
                    m.known = true;
                    m.lost = false;
                    m.standIn = false;
                    m.id = boundId[p];
                    m.x = on.x;
                    m.z = on.z;
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

        // 1. A lost player whose own tracking id is back (she may be on a
        //    stand-in meanwhile: Looking).
        for (int p = 0; p < kPlayers; p++) {
            const Memory &m = mMem[p];
            if (!Looking(p, r, boundId))
                continue;
            int s = Find(slots, m.id);
            if (s >= 0 && !claimed[s])
                Bind(r, claimed, slots, boundId, p, s, kRuleSameId, 0.0f);
        }
        // 2. A lost player and a body that appeared, after she was lost, near
        //    where she stood.  Nearest pair first; skeletons that pass the
        //    quality filter before ones that are merely tracked, as the
        //    auto-assign has it (a dancer 1.4 m from the camera with her feet
        //    out of the picture is tracked and not valid: she is still the
        //    player, and left to rule 3 she is bound later, as a stand-in).
        for (int round = 0; round < 2 * kPlayers; round++) {
            bool requireValid = round < kPlayers;
            int bestP = -1, bestS = -1;
            float bestD = 0.0f;
            for (int p = 0; p < kPlayers; p++) {
                const Memory &m = mMem[p];
                if (!Looking(p, r, boundId))
                    continue;
                float reach = kRebindBase + kRebindSpeed * m.lostMs * 0.001f;
                if (reach > kRebindMax)
                    reach = kRebindMax;
                for (int s = 0; s < kSlots; s++) {
                    float age = slots[s].ageMs >= 0.0f ? slots[s].ageMs : mSlotAgeMs[s];
                    if (requireValid ? !slots[s].valid : !slots[s].tracked)
                        continue;
                    if (claimed[s] || age > m.lostMs + (float)holdMs + kFreshSlackMs)
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
                continue;
            Bind(r, claimed, slots, boundId, bestP, bestS, kRuleNear, bestD);
        }
        // 3. The auto-assign: a player nobody has to ask for takes a free
        //    skeleton, a quality-filter-valid one first -- the one nearest to
        //    where she stood if that is known, else the first.  While she is
        //    remembered and lost it is a stand-in (kAutoWaitMs).
        for (int pass = 0; pass < 2; pass++) {
            bool requireValid = (pass == 0);
            for (int p = 0; p < autoPlayers && p < kPlayers; p++) {
                if (r.slot[p] >= 0)
                    continue;
                const Memory &m = mMem[p];
                // She waits for her own body only while the image still holds
                // her binding, which is its recovery second.  Once the image
                // has given her up there is nothing left to wait for: the
                // next thing to happen is the song pausing.  (The wait is in
                // sensor time and the image's second is on the UI clock; they
                // are the same clock in a real session and not under
                // DC3_FAST_TIME, where the UI clock runs four times as fast
                // and the image unbinds her 250 sensor-ms in.)
                if (m.known && m.lost && m.lostMs < kAutoWaitMs && boundId[p] > 0)
                    continue; // still waiting for her own body
                const Release &rel = mRelease[p];
                const bool nearOnly = rel.seen && rel.ms < (float)releaseWaitMs;
                int best = -1;
                float bestD = -1.0f;
                for (int s = 0; s < kSlots; s++) {
                    if (requireValid ? !slots[s].valid : !slots[s].tracked)
                        continue;
                    if (claimed[s])
                        continue;
                    if (nearOnly) {
                        // Let go in view: only where she was, for now.
                        float d = Flat(rel.x, rel.z, slots[s]);
                        if (d > kRebindMax) {
                            r.waiting[p] = true;
                            continue;
                        }
                        if (best < 0 || d < bestD) {
                            best = s;
                            bestD = d;
                        }
                        continue;
                    }
                    if (!m.known && !m.placed) {
                        best = s;
                        break;
                    }
                    float d = m.known ? Flat(m, slots[s]) : Flat(m.px, m.pz, slots[s]);
                    if (best < 0 || d < bestD) {
                        best = s;
                        bestD = d;
                    }
                }
                if (best >= 0) {
                    r.waiting[p] = false;
                    Bind(r, claimed, slots, boundId, p, best, kRuleAuto, bestD);
                }
            }
        }
        return r;
    }

    bool Remembered(int p) const { return mMem[p].known; }
    bool Lost(int p) const { return mMem[p].known && mMem[p].lost; }
    bool StandIn(int p) const { return Lost(p) && mMem[p].standIn; }
    // The tracking id she was bound to and is waited for while lost (-1 =
    // none): her own body, whoever stands in for her meanwhile.
    int LostId(int p) const { return Lost(p) ? mMem[p].id : -1; }

private:
    struct Memory {
        bool known = false; // she was bound, to `id`, at (x, z)
        bool lost = false;  // and her skeleton has been out of view since
        int id = -1;
        float x = 0.0f, z = 0.0f;
        float lostMs = 0.0f;
        bool standIn = false; // lost, and bound to `standInId` meanwhile
        int standInId = -1;
        bool placed = false;  // where the skeleton she was last bound to stood,
        float px = 0.0f, pz = 0.0f; // kept when `known` is not (the auto-assign)
    };

    // Lost, and bound meanwhile to the stand-in the auto-assign gave her (a
    // binding to anybody else is the image's, and that one is the player).
    bool OnStandIn(int p, const int *boundId) const {
        const Memory &m = mMem[p];
        return m.known && m.lost && m.standIn && boundId[p] == m.standInId;
    }
    // Lost and to be found: with no skeleton, or on a stand-in.
    bool Looking(int p, const Result &r, const int *boundId) const {
        const Memory &m = mMem[p];
        return m.known && m.lost && (r.slot[p] < 0 || OnStandIn(p, boundId));
    }

    static int Find(const SlotView *slots, int id) {
        for (int s = 0; s < kSlots; s++) {
            if (slots[s].tracked && slots[s].id == id)
                return s;
        }
        return -1;
    }
    static float Flat(float x, float z, const SlotView &slot) {
        float dx = slot.x - x, dz = kDepthWeight * (slot.z - z);
        return sqrtf(dx * dx + dz * dz);
    }
    static float Flat(const Memory &m, const SlotView &slot) { return Flat(m.x, m.z, slot); }
    void Bind(Result &r, bool *claimed, const SlotView *slots, const int *boundId, int p, int s,
              int rule, float distance) {
        if (r.slot[p] >= 0)
            claimed[r.slot[p]] = false; // the stand-in is free again
        r.slot[p] = s;
        claimed[s] = true;
        Memory &m = mMem[p];
        Release &rel = mRelease[p];
        if (rel.seen) {
            r.fromBound[p] = Flat(rel.x, rel.z, slots[s]);
            r.sinceBoundMs[p] = rel.ms;
        }
        // Bound here counts as seen bound: where the image does not keep a
        // binding (song select) this is the only place she is.
        rel.seen = true;
        rel.x = slots[s].x;
        rel.z = slots[s].z;
        rel.ms = 0.0f;
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
            if (m.standIn && m.standInId != slots[s].id)
                r.released[p] = m.standInId;
            m.id = slots[s].id;
            m.lost = false;
            m.standIn = false;
            m.lostMs = 0.0f;
            m.x = slots[s].x;
            m.z = slots[s].z;
        } else if (rule == kRuleAuto && m.known && m.lost) {
            m.standIn = true;
            m.standInId = slots[s].id;
        }
    }

    // Where the skeleton each player was bound to was last in the frame
    // (kReleaseWaitMs).  Kept apart from Memory: it is about the binding, not
    // the person, and holds with no reacquire window.
    struct Release {
        bool seen = false; // (x, z) is known
        float x = 0.0f, z = 0.0f; // where her bound skeleton was last in the frame
        float ms = 0.0f;   // since then
    };

    Memory mMem[kPlayers];
    Release mRelease[kPlayers];
    int mSlotId[kSlots] = { -1, -1, -1, -1, -1, -1 };
    float mSlotAgeMs[kSlots] = { 0, 0, 0, 0, 0, 0 };
};

} // namespace Dc3PoseIdentity

#endif // HX_NATIVE
