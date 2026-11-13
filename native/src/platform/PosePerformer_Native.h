#pragma once
// In-process synthetic performer: a deterministic stand-in for the Kinect, for
// scoring measurements (docs/native/POSE_INPUT.md, "Scoring believability").
//
// DC3_POSE=external DC3_POSE_PERFORM=<mode> makes the external provider read
// its frames from here instead of the pose socket.  Frames are produced on the
// main thread from the game's own song clock, at the Kinect's 30 Hz of SONG
// time, and published through NativeSkeletonProvider::PublishFrame -- the seam
// the socket reader and the web push use -- so everything after it is the
// shipped path.  Under DC3_FAST_TIME a run is then a pure function of its
// environment: no socket, no wall clock, no second process.
//
// Modes (player 0, one person):
//   choreo      the reference the scorer compares against (Dc3PoseChoreoTarget)
//   wrong       choreo shifted by DC3_POSE_PERFORM_SHIFT_BEATS (default 8)
//   halftempo   each measure's choreo played at half speed from the downbeat
//   still       the real Kinect skeleton frame baked into the image
//               (StubCameraInput::StubSkeletonData), held
//   stand       NativeSkeletonProvider's stick-figure standing pose, held
//   replay:FILE a recorded DC3-20 stream, one line per frame:
//               "t x0 y0 z0 c0 ... x19 y19 z19 c19" (t seconds, ascending);
//               DC3_POSE_PERFORM_REPLAY_SYNC=song plays the frame for the
//               reference time (as choreo), otherwise it loops on song time.
// Perturbations, any mode:
//   DC3_POSE_PERFORM_LEAD_MS  shift of the reference time (negative = late)
//   DC3_POSE_PERFORM_NOISE    "sxy,sz,rho,seed": AR(1) Gaussian error per joint
//                             per axis, metres, rho per 30 Hz frame
//   DC3_POSE_PERFORM_HZ       frame rate in song time (default 30)
#include "math/Vec.h"
#include <cstdint>

class NativeSkeletonProvider;

bool Dc3PosePerformerEnabled();
// Called by NativeSkeletonProvider::Poll on the main thread, before it latches.
void Dc3PosePerformerPump(NativeSkeletonProvider *provider);

namespace Dc3PosePerformer {
// The sensor clock: given the song time of this game frame, whether a new
// frame is due at `rateHz`.  `nextDue` is the clock's state (start at -1e30).
bool FrameDue(double songSecs, double rateHz, double &nextDue);
// Half tempo: beat `b` of a measure shows beat measureStart + (b - start) / 2.
float HalfTempoBeat(float beat);
// Deterministic standard normal stream.
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    double Uniform();
    double Gauss();
};
} // namespace Dc3PosePerformer
