// PoseSession_Native.cpp -- see PoseSession_Native.h.
#include "PoseSession_Native.h"
#include "PoseLevel_Native.h"
#include "gesture/Skeleton.h"
#include "hamobj/MoveDir.h"
#include "rndobj/Rnd.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>

namespace {

const char kHeader[] = "# dc3-pose-session v1";

double SteadySecs() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

namespace Dc3PoseSession {

std::string Format(const Frame &f) {
    std::string s;
    char buf[512];
    snprintf(buf, sizeof(buf), "F %llu %u %.6f %.6f %u %.17g %d %d %.9g %.9g %.9g %d\n",
             (unsigned long long)f.seq, f.rnd, f.wall, f.song, f.frameId, f.timestamp,
             f.unlevelled ? 1 : 0, f.level ? 1 : 0, f.floorNormal.x, f.floorNormal.y,
             f.floorNormal.z, (int)f.persons.size());
    s += buf;
    for (const NativeSkeletonProvider::PersonData &p : f.persons) {
        snprintf(buf, sizeof(buf), "P %d %d", p.trackId, p.valid ? 1 : 0);
        s += buf;
        for (int j = 0; j < kNumJoints; j++) {
            snprintf(buf, sizeof(buf), " %.9g %.9g %.9g %d", p.joints[j].x, p.joints[j].y,
                     p.joints[j].z, (int)p.confidence[j]);
            s += buf;
        }
        s += '\n';
    }
    return s;
}

bool Load(const char *path, std::vector<Frame> &out, std::string *error) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        if (error)
            *error = std::string("cannot open ") + path;
        return false;
    }
    std::vector<char> line(16384);
    bool header = false;
    int lineNo = 0;
    int personsLeft = 0;
    auto fail = [&](const char *why) {
        if (error) {
            char b[256];
            snprintf(b, sizeof(b), "%s line %d: %s", path, lineNo, why);
            *error = b;
        }
        fclose(fp);
        return false;
    };
    while (fgets(line.data(), (int)line.size(), fp)) {
        lineNo++;
        const char *l = line.data();
        if (l[0] == '#') {
            if (!strncmp(l, kHeader, sizeof(kHeader) - 1))
                header = true;
            continue;
        }
        if (l[0] == '\n' || l[0] == '\0')
            continue;
        if (!header)
            return fail("no '# dc3-pose-session v1' header before the first record");
        if (l[0] == 'F') {
            if (personsLeft)
                return fail("F record before the previous frame's P records");
            Frame f;
            unsigned long long seq;
            unsigned rnd, fid;
            int unlev, level, n;
            float nx, ny, nz;
            if (sscanf(l, "F %llu %u %lf %lf %u %lf %d %d %f %f %f %d", &seq, &rnd, &f.wall,
                       &f.song, &fid, &f.timestamp, &unlev, &level, &nx, &ny, &nz, &n)
                    != 12
                || n < 0 || n > NativeSkeletonProvider::kMaxPersons)
                return fail("malformed F record");
            f.seq = seq;
            f.rnd = rnd;
            f.frameId = fid;
            f.unlevelled = unlev != 0;
            f.level = level != 0;
            f.floorNormal.Set(nx, ny, nz);
            out.push_back(f);
            personsLeft = n;
        } else if (l[0] == 'P') {
            if (!personsLeft || out.empty())
                return fail("P record without a frame");
            NativeSkeletonProvider::PersonData p;
            char *end;
            const char *q = l + 1;
            p.trackId = (int)strtol(q, &end, 10);
            if (end == q)
                return fail("malformed P record");
            q = end;
            p.valid = strtol(q, &end, 10) != 0;
            if (end == q)
                return fail("malformed P record");
            q = end;
            for (int j = 0; j < kNumJoints; j++) {
                float v[3];
                for (int a = 0; a < 3; a++) {
                    v[a] = strtof(q, &end);
                    if (end == q)
                        return fail("P record has fewer than 20 joints");
                    q = end;
                }
                long c = strtol(q, &end, 10);
                if (end == q || c < 0 || c > 2)
                    return fail("P record joint confidence is not 0, 1 or 2");
                q = end;
                p.joints[j].Set(v[0], v[1], v[2]);
                p.confidence[j] = (JointConfidence)c;
            }
            out.back().persons.push_back(p);
            personsLeft--;
        } else {
            return fail("unknown record");
        }
    }
    fclose(fp);
    if (!header) {
        if (error)
            *error = std::string(path) + ": no '# dc3-pose-session v1' header";
        return false;
    }
    if (personsLeft && error)
        *error = std::string(path) + ": last frame truncated (kept)";
    return true;
}

int DueIndex(const std::vector<Frame> &frames, double song) {
    if (frames.empty() || song < frames.front().song)
        return -1;
    size_t lo = 0, hi = frames.size();
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (frames[mid].song <= song)
            lo = mid;
        else
            hi = mid;
    }
    return (int)lo;
}

} // namespace Dc3PoseSession

// ---------------------------------------------------------------------------
// Recorder
// ---------------------------------------------------------------------------
namespace {

struct Recorder {
    bool init = false;
    FILE *fp = nullptr;
    std::string path;
    double t0 = 0.0;
    uint64_t seq = 0;
};
Recorder gRec;

void RecorderOpen(const char *p);

void RecorderInit() {
    if (gRec.init)
        return;
    gRec.init = true;
    const char *p = getenv("DC3_POSE_RECORD");
    if (!p || !*p)
        return;
    RecorderOpen(p);
}

void RecorderOpen(const char *p) {
    gRec.fp = fopen(p, "w");
    if (!gRec.fp) {
        fprintf(stderr, "DC3 POSE_RECORD: cannot write %s - this session is NOT recorded\n", p);
        return;
    }
    gRec.path = p;
    gRec.t0 = SteadySecs();
    time_t now = time(nullptr);
    char stamp[64];
    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S%z", localtime(&now));
    fprintf(gRec.fp, "%s\n# started %s pid %d\n", kHeader, stamp, (int)getpid());
    fprintf(gRec.fp, "# F seq rnd wall song frame_id ts unlevelled level nx ny nz num_persons\n");
    fprintf(gRec.fp, "# P track_id valid 20 x (x y z confidence 0/1/2)\n");
    fflush(gRec.fp);
    fprintf(stderr, "DC3 POSE_RECORD: recording accepted sensor frames to %s\n", p);
}

} // namespace

void Dc3PoseSessionRecordTo(const char *path) {
    gRec.init = true;
    if (gRec.fp)
        fclose(gRec.fp);
    gRec.fp = nullptr;
    gRec.path.clear();
    gRec.seq = 0;
    if (path)
        RecorderOpen(path);
}

const std::string &Dc3PoseSessionRecordPath() {
    RecorderInit();
    return gRec.path;
}

void Dc3PoseSessionRecord(const NativeSkeletonProvider::PersonData *persons, int numPersons,
                          uint32_t frameId, double timestamp, bool unlevelled,
                          const SkeletonFrame &levelled) {
    RecorderInit();
    if (!gRec.fp)
        return;
    Dc3PoseSession::Frame f;
    f.seq = ++gRec.seq;
    f.rnd = TheRnd.GetFrameID();
    f.wall = SteadySecs() - gRec.t0;
    f.song = MoveDir::SongSeconds();
    f.frameId = frameId;
    f.timestamp = timestamp;
    f.unlevelled = unlevelled;
    f.level = NativeSensorLevelApplies();
    f.floorNormal = levelled.mFloorNormal;
    if (numPersons < 0)
        numPersons = 0;
    if (numPersons > NativeSkeletonProvider::kMaxPersons)
        numPersons = NativeSkeletonProvider::kMaxPersons;
    f.persons.assign(persons, persons + numPersons);
    std::string s = Dc3PoseSession::Format(f);
    fwrite(s.data(), 1, s.size(), gRec.fp);
    fflush(gRec.fp);
}

// ---------------------------------------------------------------------------
// Replay
// ---------------------------------------------------------------------------
namespace {

struct Replay {
    std::vector<Dc3PoseSession::Frame> frames;
    bool wallSync = false;
    bool started = false;
    double wallT0 = 0.0;
    int lastIndex = -2;        // frame index last published (-1 = holding the first)
    uint32_t holdId = 0;       // ids of held frames: high bit set, never a recorded id
    uint32_t publishedId = 0;  // frame id of the last publish
    int publishedIndex = -1;   // and the recorded frame it carried
    int published = 0, held = 0;
};
Replay gReplay;

void Publish(NativeSkeletonProvider *provider, int index, bool hold) {
    const Dc3PoseSession::Frame &f = gReplay.frames[index];
    uint32_t id = f.frameId;
    double ts = f.timestamp;
    if (hold) {
        // A camera pointed at someone who is standing there: a new frame every
        // call, with an id no recorded frame has, and no sensor timestamp, so
        // the elapsed time comes from the clock (AcceptedFrameElapsed).
        id = 0x80000000u | (++gReplay.holdId & 0x7fffffffu);
        ts = -1.0;
        gReplay.held++;
    } else {
        gReplay.published++;
    }
    provider->PublishFrame(f.persons.data(), (int)f.persons.size(), id, ts, f.unlevelled);
    gReplay.publishedId = id;
    gReplay.publishedIndex = index;
}

} // namespace

bool Dc3PoseSessionReplayLoad(const char *path) {
    gReplay = Replay();
    std::string err;
    bool ok = Dc3PoseSession::Load(path, gReplay.frames, &err);
    const char *sync = getenv("DC3_POSE_SESSION_SYNC");
    gReplay.wallSync = sync && !strcmp(sync, "wall");
    if (!ok) {
        fprintf(stderr, "DC3 PERFORM: cannot replay session: %s\n", err.c_str());
        return false;
    }
    if (!err.empty())
        fprintf(stderr, "DC3 PERFORM: session %s\n", err.c_str());
    fprintf(stderr, "DC3 PERFORM: session %s, %d frames, song %.3f..%.3f s, sync=%s\n", path,
            (int)gReplay.frames.size(),
            gReplay.frames.empty() ? 0.0 : gReplay.frames.front().song,
            gReplay.frames.empty() ? 0.0 : gReplay.frames.back().song,
            gReplay.wallSync ? "wall" : "song");
    return !gReplay.frames.empty();
}

void Dc3PoseSessionReplayPump(NativeSkeletonProvider *provider) {
    if (gReplay.frames.empty())
        return;
    double clock;
    if (gReplay.wallSync) {
        double now = SteadySecs();
        if (!gReplay.started) {
            gReplay.started = true;
            gReplay.wallT0 = now - gReplay.frames.front().wall;
        }
        clock = now - gReplay.wallT0;
    } else {
        clock = MoveDir::SongSeconds();
    }
    Dc3PoseSessionReplayPumpAt(provider, clock);
}

void Dc3PoseSessionReplayPumpAt(NativeSkeletonProvider *provider, double clock) {
    const std::vector<Dc3PoseSession::Frame> &fr = gReplay.frames;
    if (fr.empty())
        return;
    int due;
    if (gReplay.wallSync) {
        double w = clock;
        // latest frame accepted at or before wall time w
        size_t lo = 0, hi = fr.size();
        while (hi - lo > 1) {
            size_t mid = (lo + hi) / 2;
            if (fr[mid].wall <= w)
                lo = mid;
            else
                hi = mid;
        }
        due = (int)lo;
    } else {
        due = Dc3PoseSession::DueIndex(fr, clock);
    }
    if (due < 0) {
        Publish(provider, 0, true); // before the first frame: hold it
        gReplay.lastIndex = -1;
        return;
    }
    if (due == gReplay.lastIndex) {
        // After the last frame (a camera frame period past it): hold it.
        if (due == (int)fr.size() - 1 && !gReplay.wallSync && clock > fr[due].song + 0.05)
            Publish(provider, due, true);
        return;
    }
    gReplay.lastIndex = due;
    Publish(provider, due, false);
    if ((gReplay.published % 1800) == 0)
        fprintf(stderr, "DC3 PERFORM: session frames=%d held=%d clock=%.2f\n", gReplay.published,
                gReplay.held, clock);
}

bool Dc3PoseSessionReplayLevel(SkeletonFrame &frame, uint32_t frameId) {
    if (gReplay.frames.empty() || gReplay.publishedIndex < 0 || frameId != gReplay.publishedId)
        return false;
    const Dc3PoseSession::Frame &f = gReplay.frames[gReplay.publishedIndex];
    if (f.level)
        NativeLevelSensorFrameAt(frame, f.floorNormal);
    return true;
}
