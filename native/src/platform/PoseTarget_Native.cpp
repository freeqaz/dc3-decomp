// PoseTarget_Native.cpp -- what the game is asking each player to do RIGHT NOW,
// as a skeleton, for the native synthetic sensor (scripts/synthetic_kinect.py).
//
// WHY
//   A human player imitates what is on screen: the dancer's choreography during
//   a song, the character's pose during a fatality / Strike a Pose.  The
//   synthetic sensor plays that human.  It reads the target here (GET
//   /api/pose/target, native/src/platform/HttpServer.cpp) and streams it back
//   through the external pose socket as ordinary camera-space skeleton frames,
//   so every scoring decision -- the FilterQueue error nodes, the async move
//   detectors, PoseFatalities::UpdateMatchingPose -- is taken by the decompiled
//   code on a LIVE Skeleton, with its own history, bone lengths and quality
//   filter.  Nothing here scores, rates, or sends a game message; it only reads.
//
//   (Contrast DC3_POSE_SELFTEST, FilterQueue.cpp: it substitutes the reference
//   DancerSkeleton for the player INSIDE the scorer, so the live-skeleton half
//   of the pipeline never runs.  This path exercises all of it.)
//
// SOURCES, per player
//   "fatality"  PoseFatalities::InFatality(p): the skeleton UpdateMatchingPose
//               compares the player against (mPlayerSkeletons[p], polled from
//               the character through CharCameraInput).
//   "choreo"    otherwise, the player's scheduled DetectFrames
//               (MoveDir::ResetDetectFrames: perform, battle and practice all
//               build them, practice from its skills sequence), interpolated at
//               t = SongSeconds() - latency + lead.  The scorer compares the
//               skeleton it receives at song time S with the frame scheduled at
//               S - latency (MoveDir::PostUpdateFilters), so a player who is
//               exactly on time shows frame S - latency at S.
//   "move"      no scheduled frames (Beginner builds none -- ResetDetectFrames
//               skips it -- yet Beginner is still rated, by the async
//               detectors): the frames of the move the player is dancing
//               (MoveDir::CurrentMove), from its MoveDetector, placed on the
//               beat MoveDetector::Poll schedules them.
//   "none"      no frame within kMaxGapSeconds of t (rests, the other battle
//               player's gaps, menus).  The sensor keeps its person standing.
#include "gesture/BaseSkeleton.h"
#include "gesture/Skeleton.h"
#include "hamobj/HamDirector.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamPlayerData.h"
#include "hamobj/MoveDetector.h"
#include "hamobj/MoveDir.h"
#include "hamobj/PoseFatalities.h"
#include "obj/Task.h"
#include "utl/TimeConversion.h"

#include "platform/ChoreoBlend.h"

#include <cmath>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Two scheduled frames further apart than this are not one continuous motion
// (a rest or another player's measure lies between them): hold, do not blend.
const float kMaxGapSeconds = 0.6f;

// DC3_POSE_PERFORM_KNOT=1: blend through each frame's displacement knot
// (ChoreoBlend.h) instead of the straight chord from frame to frame.  Off by
// default, for measurement only (PLAN H-50).  MEASURED, DC1 Easy + Expert, same
// binary: on Expert at a move's first occurrence, where the scheduled frames
// ARE the async detector's reference, the chord is already Perfect on 1,642 of
// 1,642 ratings (the knot: 1,641).  Every rating the knot changes is where the
// scheduled frames are not the reference (P-11); it fixed cantgetyou Easy
// (14 -> 1 not Perfect) but made the DC1 total 332 -> 356.
const bool sDisplacementKnot = [] {
    const char *v = getenv("DC3_POSE_PERFORM_KNOT");
    return v && v[0] == '1' && v[1] == '\0';
}();

void AppendJoints(std::string &out, const Vector3 *pos) {
    out += "[";
    char buf[96];
    for (int j = 0; j < kNumJoints; j++) {
        snprintf(buf, sizeof(buf), "%s[%.5f,%.5f,%.5f]", j ? "," : "", pos[j].x, pos[j].y,
                 pos[j].z);
        out += buf;
    }
    out += "]";
}

// Choreography reference at song time t, or false.
bool ChoreoAt(const std::vector<DetectFrame> &frames, float t, Vector3 *out) {
    if (frames.empty())
        return false;
    // Frames are scheduled in move-key order, so Seconds() is non-decreasing.
    std::vector<DetectFrame>::const_iterator hi = std::lower_bound(
        frames.begin(), frames.end(), t,
        [](const DetectFrame &f, float s) { return f.Seconds() < s; });
    const DetectFrame *a = nullptr, *b = nullptr;
    if (hi != frames.end())
        b = &*hi;
    if (hi != frames.begin())
        a = &*(hi - 1);
    if (a && b && b->Seconds() - a->Seconds() > kMaxGapSeconds) {
        // not one motion: take whichever is close enough
        if (t - a->Seconds() <= kMaxGapSeconds * 0.5f)
            b = nullptr;
        else if (b->Seconds() - t <= kMaxGapSeconds * 0.5f)
            a = nullptr;
        else
            return false;
    }
    if (a && !b && t - a->Seconds() > kMaxGapSeconds * 0.5f)
        return false;
    if (b && !a && b->Seconds() - t > kMaxGapSeconds * 0.5f)
        return false;
    if (!a && !b)
        return false;
    if (!b) {
        // past the last frame of this motion: hold it
        const DancerSkeleton &sa = a->GetDancerFrame()->mSkeleton;
        for (int j = 0; j < kNumJoints; j++)
            out[j] = sa.CamJointPos((SkeletonJoint)j);
        return true;
    }
    // Blend toward b: the straight chord, or with DC3_POSE_PERFORM_KNOT=1
    // through its displacement knot (ChoreoBlend.h, PLAN H-50), so the player's
    // last ElapsedMs of motion is the dancer's authored motion.
    Vector3 pa[kNumJoints], pb[kNumJoints], db[kNumJoints];
    const DancerSkeleton &sb = b->GetDancerFrame()->mSkeleton;
    for (int j = 0; j < kNumJoints; j++) {
        pb[j] = sb.CamJointPos((SkeletonJoint)j);
        db[j] = sb.CamJointDisplacement((SkeletonJoint)j);
    }
    if (a) {
        const DancerSkeleton &sa = a->GetDancerFrame()->mSkeleton;
        for (int j = 0; j < kNumJoints; j++)
            pa[j] = sa.CamJointPos((SkeletonJoint)j);
    }
    ChoreoBlend::Blend(a ? a->Seconds() : 0.0f, a ? pa : nullptr, b->Seconds(), pb,
                       sDisplacementKnot ? db : nullptr, sb.ElapsedMs(), t, kNumJoints, out);
    return true;
}

// Which move each player is dancing, and from which measure: the game only
// exposes the CURRENT move (MoveDir::CurrentMove, set by Game::SetHamMove at
// the measure boundary), and a player who is `latency` behind is still
// finishing the previous one for the first moments of a measure.
struct MoveTrack {
    const HamMove *cur = nullptr;
    const HamMove *prev = nullptr;
    int curStart = -1;
};
MoveTrack gTrack[2];

void TrackMove(int p, const HamMove *cm, int measure) {
    MoveTrack &t = gTrack[p];
    if (measure < t.curStart) // a new song, or a loop back
        t = MoveTrack();
    if (cm != t.cur || t.curStart < 0) {
        t.prev = t.cur;
        t.cur = cm;
        t.curStart = measure;
    }
}

// Reference for the move being danced at song beat `beat`: the frames the
// move's async detector scores against (MoveDetector::mDancerFrames, one per
// MoveFrame, each at its MoveFrame::GetBeat() into the measure --
// MoveDetector::Poll schedules frame i at BeatToSeconds(beat_i + 4 * measure)),
// or the move's own DancerSequence if no detector was built for it.
bool MoveAt(int p, float beat, Vector3 *out, const HamMove *&move) {
    MoveTrack &t = gTrack[p];
    int measure = (int)floorf(beat / 4.0f);
    move = nullptr;
    if (t.cur && measure >= t.curStart)
        move = t.cur;
    else if (t.prev && measure == t.curStart - 1)
        move = t.prev;
    if (!move)
        return false;
    const std::vector<MoveFrame> &mfs = move->GetMoveFrames();
    const DancerFrame *dfs = nullptr;
    int n = 0;
    MoveDir *dir = TheHamDirector->GetMoveDir();
    const MoveDetector *det =
        dir && dir->GetAsyncDetector() ? dir->GetAsyncDetector()->NativeFindDetector(move) : nullptr;
    if (det && !det->NativeDancerFrames().empty()) {
        dfs = &det->NativeDancerFrames()[0];
        n = (int)det->NativeDancerFrames().size();
    } else if (move->GetDancerSequence() && !move->GetDancerSequence()->GetDancerFrames().empty()) {
        dfs = &move->GetDancerSequence()->GetDancerFrames()[0];
        n = (int)move->GetDancerSequence()->GetDancerFrames().size();
    }
    if (n > (int)mfs.size())
        n = (int)mfs.size();
    if (n <= 0)
        return false;
    float local = beat - measure * 4.0f;
    int hi = 0;
    while (hi < n && mfs[hi].GetBeat() < local)
        hi++;
    int lo = hi > 0 ? hi - 1 : 0;
    if (hi >= n)
        hi = n - 1;
    float w = 0.0f;
    if (hi != lo && mfs[hi].GetBeat() > mfs[lo].GetBeat())
        w = (local - mfs[lo].GetBeat()) / (mfs[hi].GetBeat() - mfs[lo].GetBeat());
    if (w < 0.0f)
        w = 0.0f;
    if (w > 1.0f)
        w = 1.0f;
    const DancerSkeleton &sa = dfs[lo].mSkeleton;
    const DancerSkeleton &sb = dfs[hi].mSkeleton;
    for (int j = 0; j < kNumJoints; j++) {
        const Vector3 &pa = sa.CamJointPos((SkeletonJoint)j);
        const Vector3 &pb = sb.CamJointPos((SkeletonJoint)j);
        out[j].Set(pa.x + (pb.x - pa.x) * w, pa.y + (pb.y - pa.y) * w, pa.z + (pb.z - pa.z) * w);
    }
    return true;
}

} // namespace

// The choreography reference player `p` should be showing when the scorer
// compares it with the frame scheduled at `refSecs` (see SOURCES above): fills
// `out` and returns "choreo" or "move", or returns null ("none").  Also used by
// the in-process performer (PosePerformer_Native.cpp).  Main thread only;
// call once per game frame per player, it advances the move tracker.
const char *Dc3PoseChoreoTarget(int p, float refSecs, Vector3 *out, const char **moveName) {
    MoveDir *dir = TheHamDirector ? TheHamDirector->GetMoveDir() : nullptr;
    if (moveName)
        *moveName = "";
    if (!dir || p < 0 || p > 1)
        return nullptr;
    TrackMove(p, dir->CurrentMove(p), TheTaskMgr.CurrentMeasure());
    if (ChoreoAt(dir->NativePlayerDetectFrames(p), refSecs, out)) {
        HamMove *cur = dir->CurrentMove(p);
        if (cur && moveName)
            *moveName = cur->Name();
        return "choreo";
    }
    const HamMove *move = nullptr;
    if (MoveAt(p, SecondsToBeat(refSecs), out, move)) {
        if (moveName)
            *moveName = move->Name();
        return "move";
    }
    return nullptr;
}

// Builds the JSON body for GET /api/pose/target.  Main thread only.
std::string Dc3PoseTargetJson(float leadMs) {
    std::string out = "{";
    char buf[256];
    MoveDir *dir = TheHamDirector ? TheHamDirector->GetMoveDir() : nullptr;
    PoseFatalities *fatal = TheHamDirector ? TheHamDirector->GetPoseFatalities() : nullptr;
    float songSecs = MoveDir::SongSeconds();
    float latency = MoveDir::NativeLatencySeconds();
    float refSecs = songSecs - latency + leadMs * 0.001f;
    snprintf(buf, sizeof(buf),
             "\"songSeconds\":%.4f,\"latency\":%.4f,\"refSeconds\":%.4f,\"beat\":%.3f,"
             "\"measure\":%d,\"moveDir\":%s,",
             songSecs, latency, refSecs, TheTaskMgr.Beat(), TheTaskMgr.CurrentMeasure(),
             dir ? "true" : "false");
    out += buf;
    out += "\"players\":[";
    for (int p = 0; p < 2; p++) {
        if (p)
            out += ",";
        int trackingId = -1;
        int side = -1;
        int score = 0;
        const char *rating = "";
        HamPlayerData *pd = TheGameData ? TheGameData->Player(p) : nullptr;
        if (pd) {
            trackingId = pd->GetSkeletonTrackingID();
            side = pd->Side();
            static Symbol scoreSym("score");
            static Symbol ratingSym("rating");
            const DataNode *n = pd->Provider() ? pd->Provider()->Property(scoreSym, false) : nullptr;
            if (n && n->Type() == kDataInt)
                score = n->Int();
            n = pd->Provider() ? pd->Provider()->Property(ratingSym, false) : nullptr;
            if (n && n->Type() == kDataSymbol)
                rating = n->Sym().Str();
        }
        snprintf(buf, sizeof(buf),
                 "{\"player\":%d,\"trackingId\":%d,\"side\":%d,\"score\":%d,"
                 "\"rating\":\"%s\",\"inFatality\":%s,",
                 p, trackingId, side, score, rating,
                 (fatal && fatal->InFatality(p)) ? "true" : "false");
        out += buf;
        Vector3 joints[kNumJoints];
        const char *source = "none";
        const char *moveName = "";
        if (fatal && fatal->InFatality(p)) {
            float match, clipOffset, progress, hold;
            int poseIndex;
            fatal->NativeMatchState(p, match, clipOffset, progress, hold, poseIndex);
            snprintf(buf, sizeof(buf),
                     "\"fatalMatch\":%.4f,\"fatalClipOffset\":%.4f,\"fatalProgress\":%.4f,"
                     "\"fatalHold\":%.4f,\"fatalPose\":%d,",
                     match, clipOffset, progress, hold, poseIndex);
            out += buf;
            // the player as UpdateMatchingPose sees them, for diagnosis
            const Skeleton *live = pd ? pd->GetSkeleton() : nullptr;
            if (live) {
                Vector3 lj[kNumJoints];
                for (int j = 0; j < kNumJoints; j++)
                    live->JointPos(kCoordCamera, (SkeletonJoint)j, lj[j]);
                snprintf(buf, sizeof(buf), "\"liveTracked\":%s,\"liveJoints\":",
                         live->IsTracked() ? "true" : "false");
                out += buf;
                AppendJoints(out, lj);
                out += ",";
            } else {
                out += "\"liveTracked\":null,";
            }
            const Skeleton &s = fatal->NativeTargetSkeleton(p);
            if (s.IsTracked()) {
                for (int j = 0; j < kNumJoints; j++)
                    s.JointPos(kCoordCamera, (SkeletonJoint)j, joints[j]);
                source = "fatality";
            }
        }
        if (dir)
            TrackMove(p, dir->CurrentMove(p), TheTaskMgr.CurrentMeasure());
        if (!strcmp(source, "none") && dir) {
            const char *choreo = Dc3PoseChoreoTarget(p, refSecs, joints, &moveName);
            if (choreo)
                source = choreo;
        }
        snprintf(buf, sizeof(buf), "\"source\":\"%s\",\"move\":\"%s\",\"frames\":%d", source,
                 moveName, dir ? (int)dir->NativePlayerDetectFrames(p).size() : 0);
        out += buf;
        if (strcmp(source, "none")) {
            out += ",\"joints\":";
            AppendJoints(out, joints);
        }
        out += "}";
    }
    out += "]}";
    return out;
}
