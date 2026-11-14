// PosePerformer_Native.cpp -- see PosePerformer_Native.h.
#include "PosePerformer_Native.h"
#include "Skeleton_Native.h"
#include "PoseSession_Native.h"
#include "gesture/StubCameraInput.h"
#include "hamobj/MoveDir.h"
#include "rndobj/Rnd.h"
#include "utl/TimeConversion.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

const char *Dc3PoseChoreoTarget(int p, float refSecs, Vector3 *out, const char **moveName);
float Dc3ScoringDtaLatencySeconds(float scorerSeconds);

namespace Dc3PosePerformer {

FrameTick FrameDue(double songSecs, double rateHz, double &nextDue) {
    double period = 1.0 / rateHz;
    // First frame, a new song (the clock went back) or a jump forward: resync.
    if (nextDue < -1e29 || songSecs < nextDue - 2 * period || songSecs > nextDue + 1.0) {
        nextDue = songSecs + period;
        return kResync;
    }
    if (songSecs + 1e-9 < nextDue)
        return kNotDue;
    nextDue += period;
    return kDue;
}

float HalfTempoBeat(float beat) {
    float start = floorf(beat / 4.0f) * 4.0f;
    return start + (beat - start) * 0.5f;
}

double Rng::Uniform() {
    // splitmix64
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return ((z >> 11) + 0.5) * (1.0 / 9007199254740992.0);
}

double Rng::Gauss() {
    double u1 = Uniform(), u2 = Uniform();
    return sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2);
}

} // namespace Dc3PosePerformer

namespace {

using namespace Dc3PosePerformer;

enum Mode { kNone, kChoreo, kWrong, kHalfTempo, kStill, kStand, kReplay, kSession };

struct State {
    bool init = false;
    Mode mode = kNone;
    double rateHz = 30.0;
    float leadSecs = 0.0f;
    float shiftBeats = 8.0f;
    bool noise = false;
    double sxy = 0, sz = 0, rho = 0;
    uint64_t seed = 1;
    Rng rng{1};
    double err[kNumJoints][3] = {};
    double nextDue = -1e30;
    uint32_t frameId = 0;
    bool haveLast = false;
    Vector3 last[kNumJoints];
    bool replaySync = false;
    bool clockLog = false;
    std::vector<ReplayFrame> replay;
    // DC3_POSE_PERFORM_GLITCH: a tracker's other faults, per published frame
    bool glitch = false;
    double pInfer = 0, pUntracked = 0, pFlip = 0, pDrop = 0;
    uint64_t glitchSeed = 7;
    Rng glitchRng{7};
    // per-run summary
    int published = 0, fromTarget = 0, held = 0, dropped = 0, flipped = 0;
};

State gState;

// Same thresholds as Skeleton_Native.cpp's ConfidenceFromScore (the socket
// reader's), so a replayed stream is graded exactly as when it was live.
JointConfidence ConfidenceFromScore(float score) {
    if (score < 0.3f)
        return kConfidenceNotTracked;
    if (score < 0.6f)
        return kConfidenceInferred;
    return kConfidenceTracked;
}

} // namespace

bool Dc3PosePerformer::LoadReplay(const char *path, std::vector<ReplayFrame> &out) {
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#')
            continue;
        // "<t> <tracked 0|1> [20 x (x y z conf)]" (dc3-pose-fixture v1,
        // scripts/pose_fixture_record.py / pose_replay_record.py); an untracked
        // frame carries no joints.
        double v[2 + kNumJoints * 4];
        int n = 0;
        char *p = line;
        char *end;
        while (n < (int)(sizeof(v) / sizeof(v[0]))) {
            double x = strtod(p, &end);
            if (end == p)
                break;
            v[n++] = x;
            p = end;
        }
        ReplayFrame fr;
        fr.t = v[0];
        if (n == 2 && v[1] == 0.0) {
            for (int j = 0; j < kNumJoints; j++) {
                fr.joints[j].Set(0.0f, 0.0f, 0.0f);
                fr.conf[j] = 0.0f; // nobody in the frame: published as 0 persons
            }
        } else if (n == 2 + kNumJoints * 4) {
            for (int j = 0; j < kNumJoints; j++) {
                const double *q = &v[2 + j * 4];
                fr.joints[j].Set((float)q[0], (float)q[1], (float)q[2]);
                fr.conf[j] = (float)q[3];
            }
        } else {
            continue;
        }
        out.push_back(fr);
    }
    fclose(f);
    return true;
}

namespace {

void LoadReplay(const char *path) {
    if (!Dc3PosePerformer::LoadReplay(path, gState.replay))
        fprintf(stderr, "DC3 PERFORM: cannot open replay %s\n", path);
    else
        fprintf(stderr, "DC3 PERFORM: replay %s, %d frames\n", path, (int)gState.replay.size());
}

void Init() {
    if (gState.init)
        return;
    gState.init = true;
    const char *m = getenv("DC3_POSE_PERFORM");
    if (!m || !*m)
        return;
    if (!strcmp(m, "choreo"))
        gState.mode = kChoreo;
    else if (!strcmp(m, "wrong"))
        gState.mode = kWrong;
    else if (!strcmp(m, "halftempo"))
        gState.mode = kHalfTempo;
    else if (!strcmp(m, "still"))
        gState.mode = kStill;
    else if (!strcmp(m, "stand"))
        gState.mode = kStand;
    else if (!strncmp(m, "session:", 8)) {
        // A recorded session (PoseSession_Native.h): it publishes the frames
        // itself, persons, ids and timestamps as recorded.
        gState.mode = kSession;
        Dc3PoseSessionReplayLoad(m + 8);
        fprintf(stderr, "DC3 PERFORM: mode=%s\n", m);
        return;
    } else if (!strncmp(m, "replay:", 7)) {
        gState.mode = kReplay;
        LoadReplay(m + 7);
        const char *sync = getenv("DC3_POSE_PERFORM_REPLAY_SYNC");
        gState.replaySync = sync && !strcmp(sync, "song");
    } else {
        fprintf(stderr, "DC3 PERFORM: unknown mode '%s'\n", m);
        return;
    }
    gState.clockLog = getenv("DC3_POSE_PERFORM_CLOCKLOG") != nullptr;
    if (const char *v = getenv("DC3_POSE_PERFORM_HZ"))
        gState.rateHz = atof(v) > 0 ? atof(v) : 30.0;
    if (const char *v = getenv("DC3_POSE_PERFORM_LEAD_MS"))
        gState.leadSecs = (float)(atof(v) * 0.001);
    if (const char *v = getenv("DC3_POSE_PERFORM_SHIFT_BEATS"))
        gState.shiftBeats = (float)atof(v);
    if (const char *v = getenv("DC3_POSE_PERFORM_NOISE")) {
        double seed = 1;
        int n = sscanf(v, "%lf,%lf,%lf,%lf", &gState.sxy, &gState.sz, &gState.rho, &seed);
        if (n >= 3) {
            gState.noise = true;
            gState.seed = (uint64_t)seed;
            gState.rng = Rng(gState.seed);
        }
    }
    // DC3_POSE_PERFORM_GLITCH=infer,untracked,flip,drop[,seed]: per published
    // frame, each joint is INFERRED (score 0.45, 5 cm off) with probability
    // `infer` or NOT TRACKED (score 0.1, 30 cm off: a joint the tracker lost
    // is a guess) with probability `untracked`; the whole body's left and
    // right swap with probability `flip`; and the frame is not published at
    // all (a dropped camera frame) with probability `drop`.  The rates are per
    // camera frame, as a tracker's are, so an 8 Hz and a 30 Hz performer have
    // the same faults per frame (np-pose-fill's noise matrix).
    if (const char *v = getenv("DC3_POSE_PERFORM_GLITCH")) {
        double seed = 7;
        int n = sscanf(v, "%lf,%lf,%lf,%lf,%lf", &gState.pInfer, &gState.pUntracked, &gState.pFlip,
                       &gState.pDrop, &seed);
        if (n >= 4) {
            gState.glitch = true;
            gState.glitchSeed = (uint64_t)seed;
            gState.glitchRng = Rng(gState.glitchSeed);
        }
    }
    if (gState.glitch)
        fprintf(stderr, "DC3 PERFORM: glitch infer=%.3f untracked=%.3f flip=%.3f drop=%.3f\n",
                gState.pInfer, gState.pUntracked, gState.pFlip, gState.pDrop);
    fprintf(stderr,
            "DC3 PERFORM: mode=%s rate=%.1fHz lead=%.0fms shift=%.1fbeats noise=%s(%.3f,%.3f,%.2f)\n",
            m, gState.rateHz, gState.leadSecs * 1000.0f, gState.shiftBeats,
            gState.noise ? "on" : "off", gState.sxy, gState.sz, gState.rho);
}

void StillPose(Vector3 *out) {
    SkeletonData data;
    memset(&data, 0, sizeof(data));
    StubCameraInput::StubSkeletonData(data, Vector3(0.0f, 0.0f, 0.0f));
    for (int j = 0; j < kNumJoints; j++)
        out[j].Set(data.mJointPositions[j].x, data.mJointPositions[j].y, data.mJointPositions[j].z);
}

void StandPose(Vector3 *out) {
    SkeletonData data;
    memset(&data, 0, sizeof(data));
    NativeSkeletonProvider::FillDummySkeletonData(data);
    for (int j = 0; j < kNumJoints; j++)
        out[j].Set(data.mJointPositions[j].x, data.mJointPositions[j].y, data.mJointPositions[j].z);
}

bool ReplayAt(double t, bool loop, Vector3 *out, float *conf) {
    const std::vector<ReplayFrame> &r = gState.replay;
    if (r.empty())
        return false;
    double t0 = r.front().t, t1 = r.back().t;
    if (loop && t1 > t0) {
        t = t0 + fmod(t - t0, t1 - t0);
        if (t < t0)
            t += t1 - t0;
    } else if (t < t0 - 0.1 || t > t1 + 0.1) {
        return false;
    }
    // latest frame at or before t: a camera shows what it last captured
    size_t lo = 0, hi = r.size();
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (r[mid].t <= t)
            lo = mid;
        else
            hi = mid;
    }
    for (int j = 0; j < kNumJoints; j++) {
        out[j] = r[lo].joints[j];
        conf[j] = r[lo].conf[j];
    }
    return true;
}

} // namespace

bool Dc3PosePerformerEnabled() {
    Init();
    return gState.mode != kNone;
}

void Dc3PosePerformerPump(NativeSkeletonProvider *provider) {
    Init();
    if (gState.mode == kNone)
        return;
    if (gState.mode == kSession) {
        Dc3PoseSessionReplayPump(provider);
        return;
    }
    double song = MoveDir::SongSeconds();
    FrameTick tick = FrameDue(song, gState.rateHz, gState.nextDue);
    if (tick == kNotDue)
        return;
    if (tick == kResync) {
        // The clock resynced (first frame, a new song, a seek): restart the
        // noise stream, so the error a song sees depends only on the seed and
        // the song's own frames -- not on how many frames the menus and the
        // load before it happened to take (measured: two runs of one noisy
        // cell scored 2,391,589 and 2,412,351 before this reset).
        gState.rng = Rng(gState.seed);
        gState.glitchRng = Rng(gState.glitchSeed);
        memset(gState.err, 0, sizeof(gState.err));
    }

    // A sensor of the Kinect's latency (scoring.dta latency_offset), made late
    // or early by LEAD_MS.  Deliberately NOT the scorer's latency: with
    // DC3_POSE_LATENCY_MS set the two differ, and the knob must be measurable.
    float scorerLatency = MoveDir::NativeLatencySeconds();
    float refSecs = (float)song - Dc3ScoringDtaLatencySeconds(scorerLatency) + gState.leadSecs;
    Vector3 joints[kNumJoints];
    float conf[kNumJoints];
    for (int j = 0; j < kNumJoints; j++)
        conf[j] = 1.0f;
    bool got = false;
    const char *moveName = "";
    switch (gState.mode) {
    case kChoreo:
        got = Dc3PoseChoreoTarget(0, refSecs, joints, &moveName) != nullptr;
        break;
    case kWrong:
        // Keep the move tracker on the real song position, then read elsewhere.
        Dc3PoseChoreoTarget(0, refSecs, joints, &moveName);
        got = Dc3PoseChoreoTarget(
                  0, BeatToSeconds(SecondsToBeat(refSecs) + gState.shiftBeats), joints, &moveName
              )
            != nullptr;
        break;
    case kHalfTempo:
        Dc3PoseChoreoTarget(0, refSecs, joints, &moveName);
        got = Dc3PoseChoreoTarget(
                  0, BeatToSeconds(HalfTempoBeat(SecondsToBeat(refSecs))), joints, &moveName
              )
            != nullptr;
        break;
    case kStill:
        StillPose(joints);
        got = true;
        break;
    case kStand:
        StandPose(joints);
        got = true;
        break;
    case kReplay:
        got = ReplayAt(gState.replaySync ? refSecs : song, !gState.replaySync, joints, conf);
        break;
    default:
        break;
    }
    if (got) {
        gState.fromTarget++;
        for (int j = 0; j < kNumJoints; j++)
            gState.last[j] = joints[j];
        gState.haveLast = true;
    } else if (gState.haveLast) {
        // between targets (rests, intro): a person holds where they were
        gState.held++;
        for (int j = 0; j < kNumJoints; j++)
            joints[j] = gState.last[j];
    } else {
        gState.held++;
        StandPose(joints);
    }
    if (gState.noise) {
        double k = sqrt(1.0 - gState.rho * gState.rho);
        for (int j = 0; j < kNumJoints; j++) {
            double s[3] = { gState.sxy, gState.sxy, gState.sz };
            for (int a = 0; a < 3; a++)
                gState.err[j][a] = gState.rho * gState.err[j][a] + k * s[a] * gState.rng.Gauss();
            joints[j].Set(
                joints[j].x + (float)gState.err[j][0],
                joints[j].y + (float)gState.err[j][1],
                joints[j].z + (float)gState.err[j][2]
            );
        }
    }

    if (gState.glitch) {
        Rng &g = gState.glitchRng;
        if (g.Uniform() < gState.pDrop) {
            gState.dropped++;
            return; // the camera frame never arrives
        }
        if (g.Uniform() < gState.pFlip) {
            static const int kPairs[][2] = {
                { kJointShoulderLeft, kJointShoulderRight }, { kJointElbowLeft, kJointElbowRight },
                { kJointWristLeft, kJointWristRight },       { kJointHandLeft, kJointHandRight },
                { kJointHipLeft, kJointHipRight },           { kJointKneeLeft, kJointKneeRight },
                { kJointAnkleLeft, kJointAnkleRight },       { kJointFootLeft, kJointFootRight },
            };
            for (const auto &pr : kPairs) {
                std::swap(joints[pr[0]], joints[pr[1]]);
                std::swap(conf[pr[0]], conf[pr[1]]);
            }
            gState.flipped++;
        }
        for (int j = 0; j < kNumJoints; j++) {
            double u = g.Uniform();
            double off;
            if (u < gState.pUntracked) {
                conf[j] = conf[j] < 0.1f ? conf[j] : 0.1f;
                off = 0.30;
            } else if (u < gState.pUntracked + gState.pInfer) {
                conf[j] = conf[j] < 0.45f ? conf[j] : 0.45f;
                off = 0.05;
            } else {
                continue;
            }
            joints[j].Set(joints[j].x + (float)(off * g.Gauss()), joints[j].y + (float)(off * g.Gauss()),
                          joints[j].z + (float)(off * g.Gauss()));
        }
    }

    if (gState.clockLog) // render frame -> song time, to key screenshots to the song
        fprintf(stderr, "DC3 PERFORM CLOCK rnd=%u song=%.4f ref=%.4f\n", TheRnd.GetFrameID(), song,
                refSecs);

    // A replayed frame with no confident joint is a frame with nobody in it.
    int numPersons = 1;
    if (gState.mode == kReplay) {
        bool any = false;
        for (int j = 0; j < kNumJoints; j++)
            any = any || conf[j] >= 0.3f;
        if (!any)
            numPersons = 0;
    }

    NativeSkeletonProvider::PersonData person;
    person.trackId = 5; // Kinect tracking ids are > 0; synthetic_kinect's person 0
    person.valid = true;
    for (int j = 0; j < kNumJoints; j++) {
        person.joints[j] = joints[j];
        person.confidence[j] = ConfidenceFromScore(conf[j]);
    }
    provider->PublishFrame(&person, numPersons, ++gState.frameId, song);
    gState.published++;
    if ((gState.published % 1800) == 0)
        fprintf(stderr, "DC3 PERFORM: frames=%d target=%d held=%d dropped=%d flipped=%d song=%.2f\n",
                gState.published, gState.fromTarget, gState.held, gState.dropped, gState.flipped,
                song);
}
