#pragma once
// How much of the time the camera really saw each body (DC3_MOVE_RATING_LOG's
// seen=, np-gpd-track 2026-11-22).
//
// A rating says how a measure was danced only if the camera saw the dancer
// during it.  MediaPipe loses one of two dancers for whole stretches on a
// slow webcam: in the 2026-11-21 GPD session player 2 (betteroffalone) was
// detected in 55% of the camera's frames, and 29 of her 36 move_bad came from
// measures she was detected in under 80% of (MEASURED offline from the
// session's pose.dc3session against the song's tempo map).  While she is
// missing the slot layer holds her last pose (P-12) or she has no skeleton at
// all, so those ratings are the tracker's, not her dancing.  The log said
// nothing of it.  This counts, per tracking id, the camera frames (not fill
// frames) in which that body was in a slot as a detection of its own, and
// answers "what fraction of the camera's frames since you last asked".
//
// Logging only: nothing the game decides reads it.
#include <unordered_map>

namespace Dc3PoseSeen {

class Counter {
public:
    // One camera frame; `ids` are the tracking ids detected in it (held slots
    // and fill frames are not detections).
    void Frame(const int *ids, int n) {
        mFrames++;
        for (int i = 0; i < n; i++) {
            if (ids[i] < 0)
                continue;
            Body &b = mBodies[ids[i]];
            if (!b.known) {
                // First sighting: her measure starts here, not at boot.
                b.known = true;
                b.mark = mFrames - 1;
                b.seenAtMark = b.seen;
            }
            b.seen++;
        }
    }
    // Fraction of the camera frames since the previous call for `id` (or
    // since her first sighting) in which `id` was detected; then starts a new
    // span.  -1 for no id, a body never seen, or no camera frame since.
    float Since(int id) {
        if (id < 0)
            return -1.0f;
        auto it = mBodies.find(id);
        if (it == mBodies.end())
            return -1.0f;
        Body &b = it->second;
        unsigned frames = mFrames - b.mark;
        unsigned seen = b.seen - b.seenAtMark;
        b.mark = mFrames;
        b.seenAtMark = b.seen;
        if (frames == 0)
            return -1.0f;
        return (float)seen / (float)frames;
    }
    void Reset() {
        mFrames = 0;
        mBodies.clear();
    }

private:
    struct Body {
        bool known = false;
        unsigned seen = 0, mark = 0, seenAtMark = 0;
    };
    unsigned mFrames = 0;
    std::unordered_map<int, Body> mBodies;
};

} // namespace Dc3PoseSeen
