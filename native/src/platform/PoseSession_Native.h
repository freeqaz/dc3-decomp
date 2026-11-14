#pragma once
// Pose session record and replay (PLAN L-1, docs/native/PLAYTEST.md).
//
// A bug seen in a live session has to replay offline.  The recorder writes,
// for every sensor frame the game ACCEPTED (BuildSensorFrame in
// GestureMgr_Native.cpp, once per new provider frame), exactly what it
// received from the provider -- every person with its tracking id, 20 joints
// and per-joint confidence, the packet's frame id and capture timestamp, the
// unlevelled flag -- plus the floor normal the sensor levelled that frame by
// (the tilt estimate is sensor STATE built up over the whole session, so a
// replay of one song could not rebuild it) and when it happened: render
// frame, wall seconds since the recorder opened, and the song clock.
//
//   DC3_POSE_RECORD=FILE                  record (any provider: socket, web
//                                         push, the performer itself)
//   DC3_POSE=external DC3_POSE_PERFORM=session:FILE
//                                         replay through the performer seam
//                                         (NativeSkeletonProvider::PublishFrame)
//   DC3_POSE_SESSION_SYNC=song (default)  a frame is published when the song
//                                         clock reaches the song time it was
//                                         accepted at; before the first and
//                                         after the last frame the nearest one
//                                         is held, so the player stays tracked
//                         wall            by wall seconds since the replay
//                                         started (menus, gesture navigation)
//
// A replayed frame carries its recorded frame id, timestamp and persons, and
// BuildSensorFrame levels it by the recorded floor normal instead of running
// the estimator (Dc3PoseSessionReplayLevel).  So from FillPersonSlots on the
// replay feeds the scorer what the live session fed it, at the same song time.
// What it does not reproduce: the camera picture (not recorded: the player
// view only), the joint-hold cache and noise-floor counter state at the first
// frame, and -- in a real-time replay -- which render frame a song time falls
// on (DC3_FAST_TIME removes that).
//
// File format, text, one record per line (scripts/playtest/pose_session.py
// reads and splits it):
//   # dc3-pose-session v1
//   F seq rnd wall song frame_id ts unlevelled level nx ny nz num_persons
//   P track_id valid  x0 y0 z0 c0 ... x19 y19 z19 c19     (num_persons lines)
// c is the JointConfidence enum (0 not tracked, 1 inferred, 2 tracked);
// floats are %.9g (round-trip exact), ts %.17g; level is 1 when the frame was
// levelled at all (DC3_POSE_LEVEL not off).
#include "math/Vec.h"
#include "platform/Skeleton_Native.h"
#include <cstdint>
#include <string>
#include <vector>

struct SkeletonFrame;

namespace Dc3PoseSession {

struct Frame {
    uint64_t seq = 0;
    uint32_t rnd = 0;
    double wall = 0.0;
    double song = 0.0;
    uint32_t frameId = 0;
    double timestamp = 0.0;
    bool unlevelled = false;
    bool level = false;
    Vector3 floorNormal;
    std::vector<NativeSkeletonProvider::PersonData> persons;
};

// One F line + its P lines, as the recorder writes them.
std::string Format(const Frame &f);
// Parse a whole file.  False if it cannot be opened or has no v1 header;
// `error` says why (and names the first malformed line).
bool Load(const char *path, std::vector<Frame> &out, std::string *error = nullptr);

// Song-sync replay: the index of the frame due at song time `song` (the latest
// one accepted at or before it), or -1 before the first frame.
int DueIndex(const std::vector<Frame> &frames, double song);

} // namespace Dc3PoseSession

// Recorder (DC3_POSE_RECORD).  Called by BuildSensorFrame after the frame is
// levelled; a no-op when the variable is unset.  Each record is flushed, so a
// crash loses at most the frame it happened on.
void Dc3PoseSessionRecord(const NativeSkeletonProvider::PersonData *persons, int numPersons,
                          uint32_t frameId, double timestamp, bool unlevelled,
                          const SkeletonFrame &levelled);
// The recorder's path, empty when not recording.
const std::string &Dc3PoseSessionRecordPath();
// Tests: record to `path` from now on instead of $DC3_POSE_RECORD (nullptr
// closes the file).
void Dc3PoseSessionRecordTo(const char *path);

// Replay (DC3_POSE_PERFORM=session:FILE).  Dc3PosePerformerPump calls Pump.
bool Dc3PoseSessionReplayLoad(const char *path);
void Dc3PoseSessionReplayPump(NativeSkeletonProvider *provider);
// The pump at an explicit clock (song seconds, or wall seconds since the
// replay started under DC3_POSE_SESSION_SYNC=wall); Pump reads the clock.
void Dc3PoseSessionReplayPumpAt(NativeSkeletonProvider *provider, double clock);
// BuildSensorFrame: when the latched frame `frameId` is a replayed one, level
// `frame` by its recorded floor normal (or not at all, as recorded) and return
// true; false otherwise (the sensor's own NativeSensorLevel runs).
bool Dc3PoseSessionReplayLevel(SkeletonFrame &frame, uint32_t frameId);
