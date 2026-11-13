#include "Skeleton_Native.h"
#include "gesture/GestureMgr.h" // NUM_SKELETONS
#include "platform/NativeNotice.h"
#include "platform/CameraStream_Native.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <sys/wait.h> // WIFEXITED & co. for DescribeServerExit (also on web)
#ifndef __EMSCRIPTEN__
#include "PosePerformer_Native.h"
#include "platform/ExecutablePath.h"
#include "platform/ProcessSpawner_Native.h"
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <poll.h>
#ifdef __linux__
#include <linux/limits.h>
#else
#include <limits.h>
#endif
#endif

NativeSkeletonProvider *TheSkeletonProvider = nullptr;

// ---------------------------------------------------------------------------
// Sensor noise floor -- a contract every skeleton the native sensor hands the
// image must meet, live provider and dummy alike.
//
// A physical depth camera never reports a joint at the same position bit for
// bit on two frames: every Kinect joint, tracked or inferred, is re-estimated
// from a fresh, noisy depth image each frame.  The image's code relies on that.
// RhythmDetector's AnalyzeData (src/system/hamobj/RhythmDetector.cpp) z-scores
// each weighted joint axis's speed over 10-sample windows and divides by the
// window's VARIANCE with no zero guard; a joint axis that does not move at all
// for ten samples gives 0 * (1/0) = NaN (Xenon's FPU, exceptions masked, gives
// the same IEEE result as x86 -- nothing traps on either).  That NaN becomes
// the RhythmDetector record's score, RhythmBattlePlayer's mRhythmSuccessFraction
// and its phrase meter's rating frac; HamPhraseMeter::SetRatingFrac hands
// SetFrame(NaN) to phrase_meter.anim, and SymbolKeys::SetFrame's AtFrame falls
// through every NaN comparison, takes `next` one key PAST THE END of the key
// vector and hands DataGetMacro that garbage symbol: null DataArray, SIGSEGV
// (measured: RhythmBattlePlayer::Poll -> RndPropAnim::SetFrame ->
// SymbolKeys::SetFrame -> DataArray::Node, ~130 s into Better Off Alone with a
// bit-for-bit still skeleton on the pose socket).
//
// The native sensors break the contract in three ways the Kinect cannot: the
// low-confidence joint hold in FillSkeletonData keeps a joint at its last good
// value for as long as it stays under 0.3 confidence (a webcam with the legs
// out of frame: both legs constant all song); a socket feeder or a paused video
// sends the same frame again; and the dummy is a static table.  So every joint
// axis of every delivered frame carries a deterministic +/-1 micrometre offset
// (a few float ulps at 2 m).  That is far below anything gesture or move
// scoring resolves -- the pose gate's dummy DetectFrac samples matched the
// static dummy's to 4 decimals, where 10 um already moved them by 1e-4 -- and a
// still joint's z-scored speed is then noise multiplied by a ~1e-5 m/s raw
// speed, i.e. the ~0 rhythm score a still player should get, not NaN.  The
// offset is a pure function of (per-slot frame counter, joint, axis), so it
// adds no run-to-run variation of its own.
// ---------------------------------------------------------------------------
static float SensorNoiseFloor(uint32_t frame, int joint, int axis) {
    const float kJitterMetres = 1.0e-6f;
    uint32_t h = frame * 0x9E3779B1u ^ (uint32_t)(joint * 3 + axis) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0xC2B2AE3Du;
    h ^= h >> 13;
    // [-1, 1)
    float unit = (float)(h & 0xFFFFu) / 32768.0f - 1.0f;
    return unit * kJitterMetres;
}

static Vector3 WithSensorNoiseFloor(const Vector3 &pos, uint32_t frame, int joint) {
    return Vector3(
        pos.x + SensorNoiseFloor(frame, joint, 0),
        pos.y + SensorNoiseFloor(frame, joint, 1),
        pos.z + SensorNoiseFloor(frame, joint, 2)
    );
}

// ---- Player-facing failure reporting ---------------------------------------
// Before 2026-11-13 a missing camera cost a silent 5 s stall at boot and one
// stdout line ("pose tracking unavailable"); the game then scored a stand-in
// skeleton and the player was never told why nothing they did counted.  The
// server now exits with a code that says what failed (pose_server.py EXIT_*),
// Start() notices the exit as soon as it happens, and the problem goes on
// screen through NativeNotice.
static const char *kPoseNoticeKey = "pose";
static const char *kStandIn =
    " The game is using a stand-in skeleton, so your dancing is not scored.";

NativeSkeletonProvider::Problem NativeSkeletonProvider::DescribeServerExit(
    int waitStatus, const std::string &camera, const std::string &python
) {
    Problem p;
    char buf[512];
    if (WIFSIGNALED(waitStatus)) {
        snprintf(buf, sizeof(buf),
            "The pose server crashed (signal %d) - pose tracking is off.%s",
            WTERMSIG(waitStatus), kStandIn);
        p.text = buf;
        p.brief = "Pose server crashed: pose tracking off";
        return p;
    }
    int code = WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : -1;
    switch (code) {
    case kExitNoCamera:
        snprintf(buf, sizeof(buf),
            "No camera found (camera %s) - pose tracking is off.%s Connect a webcam"
            " and restart, or pick another camera with DC3_POSE_CAMERA.",
            camera.c_str(), kStandIn);
        p.brief = "No camera: pose tracking off";
        break;
    case kExitCameraLost:
        snprintf(buf, sizeof(buf),
            "The camera stopped sending pictures and did not come back - pose"
            " tracking is off.%s",
            kStandIn);
        p.brief = "Camera lost: pose tracking off";
        break;
    case kExitNoVideo:
        snprintf(buf, sizeof(buf),
            "The pose server could not open its video file - pose tracking is off.%s",
            kStandIn);
        p.brief = "Pose video unreadable: pose tracking off";
        break;
    case kExitNoModel:
        snprintf(buf, sizeof(buf),
            "The pose model file is missing (see DC3_POSE_MODEL, default"
            " %s) - pose tracking is off.%s",
            DefaultModelPath().c_str(), kStandIn);
        p.brief = "Pose model missing: pose tracking off";
        break;
    case kExitNoDeps:
        snprintf(buf, sizeof(buf),
            "Python pose packages (mediapipe, opencv-python, numpy) are not"
            " installed for %s - pose tracking is off.%s Install them with"
            " scripts/playtest/bootstrap.sh --fix (it makes the venv the game"
            " looks for), or point DC3_POSE_PYTHON at a Python that has them.",
            python.c_str(), kStandIn);
        p.brief = "Pose packages missing: pose tracking off";
        break;
    case kExitExecFailed:
        snprintf(buf, sizeof(buf),
            "%s could not be started, so there is no pose server - pose"
            " tracking is off.%s Run scripts/playtest/bootstrap.sh --fix.",
            python.c_str(), kStandIn);
        p.brief = "No python3: pose tracking off";
        break;
    default:
        snprintf(buf, sizeof(buf),
            "The pose server exited (code %d) - pose tracking is off.%s"
            " Its messages are in the console log.", code, kStandIn);
        p.brief = "Pose server exited: pose tracking off";
        break;
    }
    p.text = buf;
    return p;
}

NativeSkeletonProvider::Problem NativeSkeletonProvider::CameraLostProblem() {
    Problem p;
    p.text = "The camera disconnected - looking for it again. Pose tracking is"
             " paused until it is back.";
    p.brief = "No camera: looking for it";
    return p;
}

NativeSkeletonProvider::Problem NativeSkeletonProvider::CameraBackNotice() {
    Problem p;
    p.text = "The camera is back - pose tracking resumed.";
    // No short form: a banner for a few seconds, then nothing in the corner.
    return p;
}

NativeSkeletonProvider::Problem NativeSkeletonProvider::LastProblem() {
    std::lock_guard<std::mutex> lock(mSwapMutex);
    return mProblem;
}

void NativeSkeletonProvider::ReportProblem(const Problem &p) {
    {
        std::lock_guard<std::mutex> lock(mSwapMutex);
        mProblem = p;
    }
    NativeNotice::Post(kPoseNoticeKey, p.text, p.brief, 20.0f);
}

// ---------------------------------------------------------------------------
// Sensor latency.  MoveDir::LoadScoring reads `latency_offset` (100 ms,
// config/scoring.dta) and compares the skeleton that arrives at song time S
// with the choreography scheduled at S - latency_offset
// (MoveDir::PostUpdateFilters).  That constant describes the KINECT's
// pipeline -- capture to NuiSkeletonGetNextFrame -- plus the display/audio lag
// it was tuned with.  A webcam + MediaPipe pipeline has its own latency, so
// DC3_POSE_LATENCY_MS lets the player's sensor say what its latency is; it
// replaces latency_offset (same units, same meaning).  Unset, the image's
// value stands.  A value outside [0, 1000] ms is ignored with a warning.
// Measured, docs/native/POSE_INPUT.md (Better Off Alone, Easy): a correct
// performance 300 ms later than latency_offset assumes rates 29 perfect / 28
// awesome / 20 bad of 77; the same with DC3_POSE_LATENCY_MS=400 rates 77/77
// perfect, score identical to an on-time sensor.
// ---------------------------------------------------------------------------
static float sScoringDtaLatency = -1.0f;

// latency_offset as config/scoring.dta gave it, before DC3_POSE_LATENCY_MS:
// the latency of the sensor the game was tuned for.  The in-process performer
// (PosePerformer_Native.cpp) is a sensor of exactly that latency, so the knob
// has something to correct when it is told to be late.  Before LoadScoring has
// run, the scorer's own value.
float Dc3ScoringDtaLatencySeconds(float scorerSeconds) {
    return sScoringDtaLatency >= 0.0f ? sScoringDtaLatency : scorerSeconds;
}

float Dc3SensorLatencySeconds(float scoringDtaSeconds) {
    sScoringDtaLatency = scoringDtaSeconds;
    const char *v = getenv("DC3_POSE_LATENCY_MS");
    if (!v || !*v)
        return scoringDtaSeconds;
    char *end = nullptr;
    double ms = strtod(v, &end);
    if (end == v || ms < 0.0 || ms > 1000.0) {
        fprintf(stderr, "DC3_POSE_LATENCY_MS='%s' ignored (want 0..1000 ms)\n", v);
        return scoringDtaSeconds;
    }
    fprintf(stderr, "DC3_POSE_LATENCY_MS: scoring latency %.0f ms (latency_offset %.0f ms)\n", ms,
            scoringDtaSeconds * 1000.0f);
    return (float)(ms * 0.001);
}

#ifdef __EMSCRIPTEN__
// Web: only the TRANSPORT is absent -- no pose_server.py to spawn, no Unix
// socket to read.  Frames arrive from the page's camera + MediaPipe pipeline
// (native/web/pose/) through dc3_pose_push (PoseInput_Web.cpp) into
// PublishFrame; the buffering, slot fill and joint hold below are shared.
bool NativeSkeletonProvider::Start(const std::string&, const std::string&, const std::string&) { return false; }
std::string NativeSkeletonProvider::DefaultModelPath() { return std::string(); }
void NativeSkeletonProvider::Stop() {
    mRunning = false;
    NativeCamera::Stop(); // the camera picture goes with the pose stream
}
#endif

// COCO keypoint indices
enum COCOKeypoint {
    COCO_NOSE = 0,
    COCO_LEFT_EYE = 1,
    COCO_RIGHT_EYE = 2,
    COCO_LEFT_EAR = 3,
    COCO_RIGHT_EAR = 4,
    COCO_LEFT_SHOULDER = 5,
    COCO_RIGHT_SHOULDER = 6,
    COCO_LEFT_ELBOW = 7,
    COCO_RIGHT_ELBOW = 8,
    COCO_LEFT_WRIST = 9,
    COCO_RIGHT_WRIST = 10,
    COCO_LEFT_HIP = 11,
    COCO_RIGHT_HIP = 12,
    COCO_LEFT_KNEE = 13,
    COCO_RIGHT_KNEE = 14,
    COCO_LEFT_ANKLE = 15,
    COCO_RIGHT_ANKLE = 16,
};

// v2/v3 header flag bits (native/scripts/pose_server.py FLAG_*).
static const uint16_t kFlagUnlevelled = 1u << 0;
static const uint16_t kFlagNoCamera = 1u << 1;  // camera gone; the server is reopening it

// Wire layout ids (see native/scripts/pose_server.py protocol v2).
enum PoseLayout {
    kLayoutCOCO17 = 0,  // normalised [0,1] image coords, no depth
    kLayoutDC3_20 = 1,  // DC3's own 20 joints, camera-space metres
};

// Same thresholds the COCO path uses, so the two backends agree on what counts
// as a trustworthy joint.
static JointConfidence ConfidenceFromScore(float score) {
    if (score < 0.3f) return kConfidenceNotTracked;
    if (score < 0.6f) return kConfidenceInferred;
    return kConfidenceTracked;
}

NativeSkeletonProvider::NativeSkeletonProvider() {
    memset(mFront, 0, sizeof(mFront));
    memset(mBack, 0, sizeof(mBack));
    memset(mPersons, 0, sizeof(mPersons));
}

NativeSkeletonProvider::~NativeSkeletonProvider() {
    Stop();
}

#ifndef __EMSCRIPTEN__
// ---- Desktop transport: pose_server.py child + Unix socket reader ----------

// pose_server.py's --frames argument (PLAN P-4): "" = do not ask for camera
// frames (protocol v2).  DC3_CAMERA_FRAMES=off|0 disables, WxH picks the size.
static std::string CameraFramesArg() {
    const char *v = getenv("DC3_CAMERA_FRAMES");
    if (!v || !*v)
        return "320x240";
    if (!strcmp(v, "off") || !strcmp(v, "0"))
        return "";
    return v;
}


// ---- Which Python runs the pose server ------------------------------------
static bool IsExecutableFile(const std::string &path) {
    struct stat st;
    return !path.empty() && stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)
        && access(path.c_str(), X_OK) == 0;
}

// The main checkout of a git worktree: <root>/.git is then a file
// "gitdir: <main>/.git/worktrees/<name>".  "" for a main checkout or no git.
static std::string MainCheckoutOf(const std::string &root) {
    FILE *f = fopen((root + "/.git").c_str(), "r");
    if (!f)
        return std::string();
    char line[4096] = {};
    bool ok = fgets(line, sizeof(line), f) != nullptr;
    fclose(f);
    if (!ok || strncmp(line, "gitdir: ", 8) != 0)
        return std::string(); // a directory (fopen succeeds, fgets fails) or junk
    std::string gitdir(line + 8);
    while (!gitdir.empty() && (gitdir.back() == '\n' || gitdir.back() == '\r'))
        gitdir.pop_back();
    size_t at = gitdir.find("/.git/worktrees/");
    return at == std::string::npos ? std::string() : gitdir.substr(0, at);
}

NativeSkeletonProvider::PythonChoice NativeSkeletonProvider::ChoosePosePython(
    const std::string &treeRoot, const char *home
) {
    if (const char *py = getenv("DC3_POSE_PYTHON"); py && *py)
        return { py, "DC3_POSE_PYTHON" };
    std::string venv;
    const char *venvEnv = getenv("DC3_POSE_VENV");
    if (venvEnv && *venvEnv)
        venv = venvEnv;
    else if (home && *home)
        venv = std::string(home) + "/.local/share/dc3/pose-venv";
    if (!venv.empty() && IsExecutableFile(venv + "/bin/python3"))
        return { venv + "/bin/python3",
                 std::string("the playtest venv") + (venvEnv && *venvEnv ? " (DC3_POSE_VENV)" : "")
                     + "; scripts/playtest/bootstrap.sh --fix makes it" };
    if (!treeRoot.empty()) {
        if (IsExecutableFile(treeRoot + "/.venv/bin/python3"))
            return { treeRoot + "/.venv/bin/python3", "the checkout's .venv" };
        std::string main = MainCheckoutOf(treeRoot);
        if (!main.empty() && IsExecutableFile(main + "/.venv/bin/python3"))
            return { main + "/.venv/bin/python3", "the main checkout's .venv (this is a worktree)" };
    }
    return { "python3", "no venv found (" + (venv.empty() ? std::string("no $HOME") : venv)
                            + ", " + (treeRoot.empty() ? std::string("?") : treeRoot)
                            + "/.venv): python3 from PATH" };
}

NativeSkeletonProvider::PythonChoice NativeSkeletonProvider::ChoosePosePython() {
    std::string native = NativeTreeDir();
    std::string root;
    if (size_t slash = native.rfind('/'); slash != std::string::npos && slash > 0)
        root = native.substr(0, slash);
    return ChoosePosePython(root, getenv("HOME"));
}

std::vector<std::string> NativeSkeletonProvider::ServerCommand(
    const std::string &scriptPath, const std::string &socketPath, const std::string &modelPath,
    const std::string &camera
) {
    std::vector<std::string> argv = {
        ChoosePosePython().path, scriptPath, "--socket", socketPath, "--model", modelPath,
    };
    // DC3_POSE_VIDEO: a video file stands in for the camera (looped), so the
    // whole live path -- spawn, exit codes, notices, the socket -- runs
    // without one.  Otherwise the camera.
    const char *video = getenv("DC3_POSE_VIDEO");
    if (video && *video) {
        argv.insert(argv.end(), { "--video", video, "--loop" });
    } else {
        argv.insert(argv.end(), { "--camera", camera.empty() ? std::string("auto") : camera });
    }
    // DC3_POSE_HFOV (degrees) calibrates the server to the real camera's
    // horizontal FOV -- absolute depth scales with the assumed focal length,
    // and the 58.51-degree Kinect default measured +0.76..1.73 m of depth bias
    // on real 65-72.5 degree cameras (bench_model_z.py). Pose SHAPE is
    // FOV-invariant, so the default stays safe for scoring; calibrate for
    // better absolutes.
    const char *hfov = getenv("DC3_POSE_HFOV");
    if (hfov && *hfov)
        argv.insert(argv.end(), { "--hfov", hfov });
    // PLAN P-4: ask for the camera picture + person mask (protocol v3) the
    // player view is drawn from.  DC3_CAMERA_FRAMES=off keeps the pose-only v2
    // stream; WxH picks the size (default 320x240, the Kinect depth
    // resolution).
    std::string framesArg = CameraFramesArg();
    if (!framesArg.empty())
        argv.insert(argv.end(), { "--frames", framesArg });
    return argv;
}

std::string NativeSkeletonProvider::DefaultModelPath() {
    return NativeTreePath("models/pose_landmarker_full.task");
}

// How long Start() (and each restart) waits for the server's socket.
static const int kServerTimeoutMs = 5000;

static NativeSkeletonProvider::Problem StartTimeoutProblem() {
    NativeSkeletonProvider::Problem p;
    p.text = std::string("The pose server did not start within 5 seconds - pose tracking is off.")
        + kStandIn + " Its messages are in the console log.";
    p.brief = "Pose server did not start: pose tracking off";
    return p;
}

bool NativeSkeletonProvider::Start(
    const std::string &socketPath, const std::string &modelPath, const std::string &camera
) {
    if (mRunning) return true;

    // DC3_POSE_PERFORM: frames come from the in-process performer (Poll pumps
    // it), not from a socket.  See PosePerformer_Native.h.
    if (Dc3PosePerformerEnabled()) {
        printf("DC3_POSE_PERFORM: in-process synthetic performer\n");
        mRunning = true;
        return true;
    }

    mSocketPath = socketPath;
    mModelPath = modelPath;
    mCamera = camera.empty() ? std::string("auto") : camera;
    mNoCamera = false;
    mStopRequested = false;
    mRestarts = 0;
    {
        std::lock_guard<std::mutex> lock(mSwapMutex);
        mProblem = Problem();
    }

    // DC3_POSE_NO_SPAWN=1: connect-only mode — attach to an already-running
    // pose server (e.g. a synthetic one in CI) instead of forking pose_server.py.
    mSpawned = !getenv("DC3_POSE_NO_SPAWN");
    if (!mSpawned) {
        printf("DC3_POSE_NO_SPAWN: connecting to existing pose server at %s\n",
            socketPath.c_str());
    } else if (!LaunchServer()) {
        return false;
    }

    // Wait for the socket to appear, then connect.  pose_server.py binds it
    // once the camera has delivered a frame (before loading the model), and
    // exits with an EXIT_* code if the camera, model or packages are missing;
    // that exit is noticed on the next 50 ms tick instead of after the whole
    // timeout.
    int status = 0;
    switch (WaitForServer(kServerTimeoutMs, &status)) {
    case kConnected:
        printf("Connected to pose server\n");
        NativeNotice::Clear(kPoseNoticeKey);
        mRunning = true;
        mReaderThread = std::thread(&NativeSkeletonProvider::ReaderThread, this);
        return true;
    case kServerExited:
        ReportProblem(DescribeServerExit(status, mCamera, mPython));
        Stop();
        return false;
    case kTimedOut:
    case kStopped:
        break;
    }

    fprintf(stderr, "Failed to connect to pose server after %d ms\n", kServerTimeoutMs);
    ReportProblem(StartTimeoutProblem());
    Stop();
    return false;
}

bool NativeSkeletonProvider::LaunchServer() {
    // pose_server.py is found next to the executable's tree (native/build/<exe>
    // -> native/scripts), never the working directory; DC3_POSE_SERVER names
    // a drop-in that takes the same arguments (the playtest kit's footage
    // feeder).  The command line, interpreter included, is ServerCommand's.
    std::string scriptPath = NativeTreePath("scripts/pose_server.py");
    if (const char *srv = getenv("DC3_POSE_SERVER"); srv && *srv)
        scriptPath = srv;
    PythonChoice py = ChoosePosePython();
    mPython = py.path;
    printf("Pose server python: %s (%s)\n", py.path.c_str(), py.why.c_str());
    std::vector<std::string> argv = ServerCommand(scriptPath, mSocketPath, mModelPath, mCamera);
    printf("Pose server command:");
    for (const std::string &a : argv)
        printf(" %s", a.c_str());
    printf("\n");
    // Through the spawner forked at the top of main (ProcessSpawner_Native.h):
    // a child forked from this process now would inherit the RLIMIT_RTTIME of
    // 0 that PipeWire's ALSA plugin sets on it, and MediaPipe's realtime
    // thread gets the server SIGKILLed at model load.
    mServerPid = NativeSpawner::Spawn(argv);
    if (mServerPid < 0) {
        perror("Failed to launch pose_server.py");
        mServerPid = -1;
        return false;
    }
    printf("Launched pose_server.py (pid %d%s)\n", (int)mServerPid,
        NativeSpawner::Active() ? ", via the spawner" : "");
    return true;
}

NativeSkeletonProvider::WaitResult NativeSkeletonProvider::WaitForServer(
    int timeoutMs, int *exitStatus
) {
    static const int kPollMs = 50;
    for (int waited = 0; waited < timeoutMs; waited += kPollMs) {
        usleep(kPollMs * 1000);
        if (mStopRequested)
            return kStopped;

        if (mServerPid > 0) {
            int status = 0;
            if (NativeSpawner::TryReap(mServerPid, &status)) {
                mServerPid = -1;
                fprintf(stderr, "pose_server.py exited after %d ms (status 0x%x)\n",
                    waited + kPollMs, status);
                *exitStatus = status;
                return kServerExited;
            }
        }

        mSocketFd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (mSocketFd < 0) continue;

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, mSocketPath.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(mSocketFd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            return kConnected;

        close(mSocketFd);
        mSocketFd = -1;
    }
    return kTimedOut;
}

void NativeSkeletonProvider::Stop() {
    mStopRequested = true;
    mRunning = false;
    NativeCamera::Stop(); // the camera picture goes with the pose stream

    if (mReaderThread.joinable()) {
        mReaderThread.join();
    }

    if (mSocketFd >= 0) {
        close(mSocketFd);
        mSocketFd = -1;
    }

    if (mServerPid > 0) {
        // The spawner reaps it (and SIGKILLs it if it ignores this).
        NativeSpawner::Kill(mServerPid, SIGTERM);
        mServerPid = -1;
    }
}

bool NativeSkeletonProvider::ServerExitIsRestartable(int waitStatus) {
    if (WIFSIGNALED(waitStatus))
        return true; // a crash, e.g. the kernel's SIGKILL
    if (!WIFEXITED(waitStatus))
        return false;
    switch (WEXITSTATUS(waitStatus)) {
    case 0:               // asked to stop, or its video ended
    case kExitNoVideo:
    case kExitNoModel:
    case kExitNoDeps:
    case kExitExecFailed: // nothing a second try changes
        return false;
    default:              // a lost camera, an unexpected error
        return true;
    }
}

// The socket closed under the reader.  If we spawned the server, give it a
// moment to finish exiting so its code can say why (a lost camera exits
// EXIT_CAMERA_LOST); otherwise -- a connect-only server, or one still running
// -- say that the stream stopped.  A server we launched is then relaunched,
// within the restart policy.
bool NativeSkeletonProvider::ServerGone() {
    NativeCamera::Stop(); // no more camera frames either (PLAN P-4)
    if (mSocketFd >= 0) {
        close(mSocketFd);
        mSocketFd = -1;
    }
    mNoCamera = false; // a new server (or none) starts over; its notice replaces this one

    Problem streamStopped;
    streamStopped.text = std::string("The pose stream stopped - pose tracking is off.") + kStandIn;
    streamStopped.brief = "Pose stream stopped: pose tracking off";

    bool exited = false;
    int status = 0;
    if (mServerPid > 0) {
        for (int i = 0; i < 20 && !mStopRequested; i++) {
            if (NativeSpawner::TryReap(mServerPid, &status)) {
                mServerPid = -1;
                exited = true;
                break;
            }
            usleep(25000);
        }
    }
    if (!mSpawned) {
        ReportProblem(streamStopped);
        return false;
    }
    Problem problem = exited ? DescribeServerExit(status, mCamera, mPython) : streamStopped;
    if (!exited && mServerPid > 0) {
        // It closed the socket but did not exit: replace it.
        NativeSpawner::Kill(mServerPid, SIGTERM);
        mServerPid = -1;
    }

    for (;;) {
        if (mStopRequested)
            return false;
        if (exited && !ServerExitIsRestartable(status))
            break;
        if (mRestarts >= mMaxRestarts) {
            if (mMaxRestarts > 0)
                fprintf(stderr, "Pose server: not restarting it again (%d restarts already)\n",
                    (int)mRestarts);
            break;
        }
        mRestarts = mRestarts + 1;
        int delayMs = mRestartBackoffMs;
        for (int i = 1; i < mRestarts && delayMs < 30000; i++)
            delayMs *= 2;

        // One line per restart, on the console and on screen.
        std::string why;
        if (!exited) {
            why = "The pose stream stopped";
        } else if (WIFSIGNALED(status)) {
            why = "The pose server crashed (signal " + std::to_string(WTERMSIG(status)) + ")";
        } else {
            why = "The pose server exited (code " + std::to_string(WEXITSTATUS(status)) + ")";
        }
        fprintf(stderr, "%s - restarting it in %d ms (restart %d of %d)\n", why.c_str(), delayMs,
            (int)mRestarts, mMaxRestarts);
        char brief[96];
        snprintf(brief, sizeof(brief), "Pose server restarting (%d of %d)", (int)mRestarts,
            mMaxRestarts);
        Problem restarting;
        restarting.text = why + " - restarting pose tracking (" + std::to_string(mRestarts) + " of "
            + std::to_string(mMaxRestarts) + ").";
        restarting.brief = brief;
        ReportProblem(restarting);

        for (int waited = 0; waited < delayMs && !mStopRequested; waited += 50)
            usleep(50 * 1000);
        if (mStopRequested || !LaunchServer())
            continue; // a failed fork counts as a restart

        switch (WaitForServer(kServerTimeoutMs, &status)) {
        case kConnected:
            printf("Pose server restarted (pid %d): pose tracking resumed\n", (int)mServerPid);
            {
                std::lock_guard<std::mutex> lock(mSwapMutex);
                mProblem = Problem();
            }
            NativeNotice::Clear(kPoseNoticeKey);
            return true;
        case kServerExited:
            exited = true;
            problem = DescribeServerExit(status, mCamera, mPython);
            break;
        case kTimedOut:
            exited = false;
            problem = StartTimeoutProblem();
            if (mServerPid > 0) {
                NativeSpawner::Kill(mServerPid, SIGTERM);
                mServerPid = -1;
            }
            break;
        case kStopped:
            return false;
        }
    }
    ReportProblem(problem);
    return false;
}

// The v3 camera block that follows the persons: hand it to the camera stream
// with the persons' tracking ids and root depths, so the mask's ordinals can
// become NUI player indices when the frame is latched.
static void ParseCameraBlock(const uint8_t *p, const uint8_t *end,
                             const NativeSkeletonProvider::PersonData *persons,
                             int numPersons, double timestamp) {
    int trackIds[NativeCamera::kMaxPersons];
    float depthM[NativeCamera::kMaxPersons];
    int n = numPersons < NativeCamera::kMaxPersons ? numPersons : NativeCamera::kMaxPersons;
    for (int i = 0; i < n; i++) {
        trackIds[i] = persons[i].valid ? persons[i].trackId : -1;
        depthM[i] = persons[i].valid ? persons[i].joints[kJointHipCenter].z : 0.0f;
    }
    NativeCamera::Image image;
    const char *err = NativeCamera::ParseWireBlock(p, end, trackIds, depthM, n, timestamp, image);
    if (err) {
        static bool sWarned = false;
        if (!sWarned) {
            sWarned = true;
            fprintf(stderr, "Pose packet camera block rejected: %s\n", err);
        }
        return;
    }
    if (image.width > 0)
        NativeCamera::Publish(std::move(image));
}

static bool readExact(int fd, void *buf, size_t len) {
    char *p = (char *)buf;
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = read(fd, p, remaining);
        if (n <= 0) return false;
        p += n;
        remaining -= n;
    }
    return true;
}

void NativeSkeletonProvider::ReaderThread() {
    while (mRunning && !mStopRequested) {
        // Poll for data with timeout
        struct pollfd pfd;
        pfd.fd = mSocketFd;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 100); // 100ms timeout
        if (ret <= 0) continue;

        // Read packet length prefix
        uint32_t packetLen;
        if (!readExact(mSocketFd, &packetLen, 4)) {
            fprintf(stderr, "Pose server disconnected\n");
            if (ServerGone())
                continue; // a relaunched server is connected again
            mRunning = false;
            break;
        }

        // Read packet
        std::vector<uint8_t> packet(packetLen);
        if (!readExact(mSocketFd, packet.data(), packetLen)) {
            fprintf(stderr, "Pose server read error\n");
            if (ServerGone())
                continue;
            mRunning = false;
            break;
        }

        // Two wire formats. v1 (legacy, still emitted by pose_server_synthetic.py):
        //   [u32 frame_id][u32 num_persons][f64 ts], then per person
        //   [i32 track_id] + 17 x (f32 x, y, conf), normalised image coords.
        // v2 (pose_server.py, both backends):
        //   [u32 magic][u32 frame_id][u32 num_persons][f64 ts]
        //   [u16 w][u16 h][u8 num_landmarks][u8 layout][u16 pad], then per person
        //   [i32 track_id] + num_landmarks x (f32 x, y, z, conf).
        // They are distinguished by the leading magic: v1's first field is a small
        // incrementing frame_id, so a large sentinel cannot collide.
        // v3 (pose_server.py --frames): v2 with a new magic and a camera image
        // block after the persons (ParseCameraBlock).
        // v2/v3 header flags (the u16 after `layout`, 0 from older servers):
        // bit 0 = the camera does not know its gravity (pose_server.py: a
        // webcam), so the sensor estimates its tilt (PoseLevel_Native.h).
        // synthetic_kinect.py sends 0: it plays a Kinect, which is level.
        static const uint32_t kProtocolMagic = 0x44503302u;
        static const uint32_t kProtocolMagicV3 = 0x44503303u;
        if (packetLen < 16) continue;
        const uint8_t *p = packet.data();
        const uint8_t *end = packet.data() + packetLen;

        uint32_t magic = 0;
        memcpy(&magic, p, 4);
        const bool v3 = (magic == kProtocolMagicV3);
        const bool v2 = (magic == kProtocolMagic) || v3;

        uint32_t frameId, numPersons;
        double timestamp;
        uint16_t frameW = 0, frameH = 0, flags = 0;
        uint8_t numLandmarks = 17, layout = kLayoutCOCO17;

        if (v2) {
            if (packetLen < 28) continue;
            p += 4; // magic
            memcpy(&frameId, p, 4); p += 4;
            memcpy(&numPersons, p, 4); p += 4;
            memcpy(&timestamp, p, 8); p += 8;
            memcpy(&frameW, p, 2); p += 2;
            memcpy(&frameH, p, 2); p += 2;
            numLandmarks = *p++;
            layout = *p++;
            memcpy(&flags, p, 2); p += 2;
            // Must be set before the person loop below: MapCOCOToDC3 ->
            // NormalizedToMeters consumes it for THIS packet's landmarks.
            if (frameW > 0 && frameH > 0)
                mFrameAspect = (float)frameH / (float)frameW;
        } else {
            memcpy(&frameId, p, 4); p += 4;
            memcpy(&numPersons, p, 4); p += 4;
            memcpy(&timestamp, p, 8); p += 8;
        }

        if (numPersons > (uint32_t)kMaxPersons)
            numPersons = kMaxPersons;

        // Parse per-person data
        PersonData newBack[kMaxPersons];
        memset(newBack, 0, sizeof(newBack));

        const size_t compsPerLandmark = v2 ? 4 : 3;
        const size_t personBytes = 4 + (size_t)numLandmarks * compsPerLandmark * sizeof(float);
        bool truncated = false;

        for (uint32_t i = 0; i < numPersons; i++) {
            if ((size_t)(end - p) < personBytes) { truncated = true; break; }

            int32_t trackId;
            memcpy(&trackId, p, 4); p += 4;
            newBack[i].trackId = trackId;

            if (layout == kLayoutDC3_20 && numLandmarks == kNumJoints) {
                // Already DC3's own 20 joints in camera-space METRES. No remap and
                // no NormalizedToMeters, which is exactly why this path is immune
                // to the 4:3 view-box assumption that skews a 16:9 camera.
                for (int j = 0; j < kNumJoints; j++) {
                    float v[4];
                    memcpy(v, p, 4 * sizeof(float));
                    p += 4 * sizeof(float);
                    newBack[i].joints[j] = Vector3(v[0], v[1], v[2]);
                    newBack[i].confidence[j] = ConfidenceFromScore(v[3]);
                }
            } else if (numLandmarks == 17) {
                float cocoKpts[17][3]; // x, y, conf
                for (int j = 0; j < 17; j++) {
                    float v[4] = { 0, 0, 0, 0 };
                    memcpy(v, p, compsPerLandmark * sizeof(float));
                    p += compsPerLandmark * sizeof(float);
                    cocoKpts[j][0] = v[0];
                    cocoKpts[j][1] = v[1];
                    // v1 packs conf third; v2 packs z third and conf fourth.
                    cocoKpts[j][2] = v2 ? v[3] : v[2];
                }
                MapCOCOToDC3(cocoKpts, newBack[i]);
            } else {
                // Unknown layout/landmark count: skip this person rather than
                // walking the cursor off the end of the packet.
                p += personBytes - 4;
                continue;
            }
            newBack[i].valid = true;
        }

        if (truncated) {
            static bool sWarned = false;
            if (!sWarned) {
                sWarned = true;
                fprintf(stderr,
                    "Pose packet truncated (len=%u persons=%u landmarks=%u layout=%u);"
                    " dropping trailing persons\n",
                    packetLen, numPersons, numLandmarks, layout);
            }
        }

        if (v3 && !truncated)
            ParseCameraBlock(p, end, newBack, (int)numPersons, timestamp);

        // FLAG_NO_CAMERA: one notice when the camera goes, one when it is
        // back (edges only -- the server repeats the flag every 0.1 s).
        const bool noCamera = v2 && (flags & kFlagNoCamera) != 0;
        if (noCamera != mNoCamera) {
            mNoCamera = noCamera;
            if (noCamera) {
                fprintf(stderr, "Pose server: camera lost, it is looking for it again\n");
                NativeNotice::Post(kPoseNoticeKey, CameraLostProblem().text,
                    CameraLostProblem().brief, 20.0f);
            } else {
                fprintf(stderr, "Pose server: camera is back\n");
                NativeNotice::Post(kPoseNoticeKey, CameraBackNotice().text, "", 5.0f);
            }
        }

        PublishFrame(newBack, (int)numPersons, frameId, timestamp, (flags & kFlagUnlevelled) != 0);
    }
}

#endif // !__EMSCRIPTEN__ (desktop transport)

void NativeSkeletonProvider::PublishFrame(
    const PersonData *persons, int numPersons, uint32_t frameId, double timestamp,
    bool unlevelled
) {
    if (numPersons < 0)
        numPersons = 0;
    if (numPersons > kMaxPersons)
        numPersons = kMaxPersons;
    std::lock_guard<std::mutex> lock(mSwapMutex);
    memcpy(mBack, persons, numPersons * sizeof(PersonData));
    memset(mBack + numPersons, 0, (kMaxPersons - numPersons) * sizeof(PersonData));
    mNumPersonsBack = numPersons;
    mFrameIdBack = frameId;
    mTimestampBack = timestamp;
    mUnlevelledBack = unlevelled;
}

void NativeSkeletonProvider::Poll() {
#ifndef __EMSCRIPTEN__
    if (mRunning && mSocketFd < 0 && Dc3PosePerformerEnabled())
        Dc3PosePerformerPump(this); // publishes; latched below
#endif
    std::lock_guard<std::mutex> lock(mSwapMutex);
    memcpy(mPersons, mBack, sizeof(mPersons));
    mNumPersons = mNumPersonsBack;
    mFrameIdFront = mFrameIdBack;
    mTimestampFront = mTimestampBack;
    mUnlevelledFront = mUnlevelledBack;
}

int NativeSkeletonProvider::FindByTrackId(int trackId) const {
    for (int i = 0; i < mNumPersons; i++) {
        if (mPersons[i].valid && mPersons[i].trackId == trackId)
            return i;
    }
    return -1;
}

Vector3 NativeSkeletonProvider::NormalizedToMeters(float nx, float ny) const {
    // Map normalized [0,1] camera coords to approximate meter-space
    // Origin at hip center, X = player's right, Y up, Z toward camera.
    // X is flipped like Y: a subject facing the camera has their anatomical
    // LEFT at large image x, but DC3 camera space puts player-left at -X.
    // Ground truth: the baked Kinect capture in StubCameraInput has
    // ShoulderLeft.x=-0.047 / ShoulderRight.x=+0.322, and FillDummySkeleton
    // below uses left=-0.20 / right=+0.20. Without this flip the live pose
    // path contradicted the dummy path and every pose was mirrored.
    float x = (0.5f - nx) * mViewWidth;
    // Vertical extent derives from the ACTUAL frame aspect (square pixels:
    // viewH = viewW * H/W), fed from the v2 packet header. Fixes the 16:9
    // webcam case, which the old hardcoded 4:3 view box anisotropically skewed.
    float y = (0.5f - ny) * (mViewWidth * mFrameAspect); // flip Y (image Y is down)
    float z = mViewDepth;
    return Vector3(x, y, z);
}

void NativeSkeletonProvider::MapCOCOToDC3(const float cocoKpts[][3], PersonData &out) {
    // Helper: convert a single COCO keypoint
    auto kpt = [&](int idx) -> Vector3 {
        return NormalizedToMeters(cocoKpts[idx][0], cocoKpts[idx][1]);
    };
    auto kptConf = [&](int idx) -> JointConfidence {
        float c = cocoKpts[idx][2];
        if (c < 0.3f) return kConfidenceNotTracked;
        if (c < 0.6f) return kConfidenceInferred;
        return kConfidenceTracked;
    };
    auto midpoint = [](const Vector3 &a, const Vector3 &b) -> Vector3 {
        return Vector3((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f);
    };
    auto minConf = [](JointConfidence a, JointConfidence b) -> JointConfidence {
        return (a < b) ? a : b;
    };
    // Extrapolate a missing tip joint past `tip` along the parent->tip direction,
    // by `frac` of the parent bone's length. Used for hand and foot, which COCO-17
    // simply does not have. The fraction scales with the measured parent bone, so
    // it tracks the subject's apparent size and distance automatically.
    auto extrapolate = [](const Vector3 &parent, const Vector3 &tip,
                           float frac) -> Vector3 {
        Vector3 dir;
        Subtract(tip, parent, dir);
        return Vector3(tip.x + dir.x * frac, tip.y + dir.y * frac, tip.z + dir.z * frac);
    };

    // Direct mappings.
    // Kinect's kJointHead sits at roughly the centre of the skull; the COCO nose is
    // anterior and inferior to it, which biases every head-weighted error node by a
    // constant offset. The ear midpoint is much closer to skull centre, and the ear
    // keypoints were previously decoded and then discarded. Fall back to the nose
    // when the ears are unreliable (profile views occlude one ear).
    JointConfidence earConf =
        minConf(kptConf(COCO_LEFT_EAR), kptConf(COCO_RIGHT_EAR));
    if (earConf > kConfidenceNotTracked) {
        out.joints[kJointHead] = midpoint(kpt(COCO_LEFT_EAR), kpt(COCO_RIGHT_EAR));
        out.confidence[kJointHead] = earConf;
    } else {
        out.joints[kJointHead] = kpt(COCO_NOSE);
        out.confidence[kJointHead] = kptConf(COCO_NOSE);
    }

    out.joints[kJointShoulderLeft] = kpt(COCO_LEFT_SHOULDER);
    out.confidence[kJointShoulderLeft] = kptConf(COCO_LEFT_SHOULDER);

    out.joints[kJointElbowLeft] = kpt(COCO_LEFT_ELBOW);
    out.confidence[kJointElbowLeft] = kptConf(COCO_LEFT_ELBOW);

    out.joints[kJointWristLeft] = kpt(COCO_LEFT_WRIST);
    out.confidence[kJointWristLeft] = kptConf(COCO_LEFT_WRIST);

    // hand == wrist made kBoneHandLeft/Right length 0. ErrorNode::NormBoneLengths
    // sums the bones in a node's norm_bones set, and PositionNode returns a flat
    // max error (1,1,1) whenever that base sum is <= 0 (ErrorNode.cpp:415), while a
    // partial sum that omits the hand inflates the desired/base ratio. Extrapolate
    // the palm past the wrist along the forearm: wrist-to-palm is ~28% of forearm.
    out.joints[kJointHandLeft] =
        extrapolate(kpt(COCO_LEFT_ELBOW), kpt(COCO_LEFT_WRIST), 0.42f);
    out.confidence[kJointHandLeft] =
        minConf(kptConf(COCO_LEFT_WRIST), kptConf(COCO_LEFT_ELBOW));

    out.joints[kJointShoulderRight] = kpt(COCO_RIGHT_SHOULDER);
    out.confidence[kJointShoulderRight] = kptConf(COCO_RIGHT_SHOULDER);

    out.joints[kJointElbowRight] = kpt(COCO_RIGHT_ELBOW);
    out.confidence[kJointElbowRight] = kptConf(COCO_RIGHT_ELBOW);

    out.joints[kJointWristRight] = kpt(COCO_RIGHT_WRIST);
    out.confidence[kJointWristRight] = kptConf(COCO_RIGHT_WRIST);

    out.joints[kJointHandRight] =
        extrapolate(kpt(COCO_RIGHT_ELBOW), kpt(COCO_RIGHT_WRIST), 0.42f);
    out.confidence[kJointHandRight] =
        minConf(kptConf(COCO_RIGHT_WRIST), kptConf(COCO_RIGHT_ELBOW));

    out.joints[kJointHipLeft] = kpt(COCO_LEFT_HIP);
    out.confidence[kJointHipLeft] = kptConf(COCO_LEFT_HIP);

    out.joints[kJointKneeLeft] = kpt(COCO_LEFT_KNEE);
    out.confidence[kJointKneeLeft] = kptConf(COCO_LEFT_KNEE);

    out.joints[kJointAnkleLeft] = kpt(COCO_LEFT_ANKLE);
    out.confidence[kJointAnkleLeft] = kptConf(COCO_LEFT_ANKLE);

    out.joints[kJointHipRight] = kpt(COCO_RIGHT_HIP);
    out.confidence[kJointHipRight] = kptConf(COCO_RIGHT_HIP);

    out.joints[kJointKneeRight] = kpt(COCO_RIGHT_KNEE);
    out.confidence[kJointKneeRight] = kptConf(COCO_RIGHT_KNEE);

    out.joints[kJointAnkleRight] = kpt(COCO_RIGHT_ANKLE);
    out.confidence[kJointAnkleRight] = kptConf(COCO_RIGHT_ANKLE);

    // Same for foot == ankle (kBoneFootLeft/Right were length 0). Kinect's foot
    // joint is forward of and below the ankle; "forward" is +Z, which a 2D
    // keypoint set cannot see, so extend along the shin instead. That lands the
    // foot below the ankle -- wrong in Z, but a plausible non-degenerate bone
    // length, which is what the normalizer actually consumes. Revisit once the
    // provider supplies real depth and real foot landmarks.
    out.joints[kJointFootLeft] =
        extrapolate(kpt(COCO_LEFT_KNEE), kpt(COCO_LEFT_ANKLE), 0.11f);
    out.confidence[kJointFootLeft] =
        minConf(kptConf(COCO_LEFT_ANKLE), kptConf(COCO_LEFT_KNEE));

    out.joints[kJointFootRight] =
        extrapolate(kpt(COCO_RIGHT_KNEE), kpt(COCO_RIGHT_ANKLE), 0.11f);
    out.confidence[kJointFootRight] =
        minConf(kptConf(COCO_RIGHT_ANKLE), kptConf(COCO_RIGHT_KNEE));

    // Synthesized joints
    Vector3 hipCenter = midpoint(kpt(COCO_LEFT_HIP), kpt(COCO_RIGHT_HIP));
    Vector3 shoulderCenter = midpoint(kpt(COCO_LEFT_SHOULDER), kpt(COCO_RIGHT_SHOULDER));

    out.joints[kJointHipCenter] = hipCenter;
    out.confidence[kJointHipCenter] = minConf(kptConf(COCO_LEFT_HIP), kptConf(COCO_RIGHT_HIP));

    out.joints[kJointShoulderCenter] = shoulderCenter;
    out.confidence[kJointShoulderCenter] = minConf(kptConf(COCO_LEFT_SHOULDER), kptConf(COCO_RIGHT_SHOULDER));

    out.joints[kJointSpine] = midpoint(hipCenter, shoulderCenter);
    out.confidence[kJointSpine] = minConf(out.confidence[kJointHipCenter], out.confidence[kJointShoulderCenter]);
}

// Last-good camera-space position per skeleton slot, for the confidence hold in
// FillSkeletonData. Nothing under src/system/hamobj/ reads JointConf -- the scorer
// consumes mJointPos unconditionally -- so a keypoint the detector has no
// confidence in is otherwise graded as ground truth. A real Kinect never presents
// a garbage joint: it fills occluded joints from its own skeletal model and flags
// them inferred. Holding the last good position is the closest equivalent, and it
// also keeps displacement scoring sane (a held joint reads as stationary rather
// than as a large spurious velocity).
static Vector3 sLastGoodJointPos[NUM_SKELETONS][kNumJoints];
static bool sHaveLastGoodJoint[NUM_SKELETONS][kNumJoints];

static uint32_t sNoiseFrame[NUM_SKELETONS + 1];

void NativeSkeletonProvider::ResetSensorNoiseFloor() {
    for (uint32_t &n : sNoiseFrame)
        n = 0;
}

void NativeSkeletonProvider::ResetJointHold(int skelIdx) {
    if (skelIdx < 0 || skelIdx >= NUM_SKELETONS)
        return;
    for (int j = 0; j < kNumJoints; j++)
        sHaveLastGoodJoint[skelIdx][j] = false;
}

void NativeSkeletonProvider::FillSkeletonData(
    SkeletonData &data, const PersonData &person, int slot
) {
    bool canHold = (slot >= 0 && slot < NUM_SKELETONS);
    // One noise-floor frame per delivered frame per slot (see SensorNoiseFloor
    // at the top of this file).  A held joint is exactly the case that needs it.
    uint32_t noiseFrame = ++sNoiseFrame[canHold ? slot : NUM_SKELETONS];
    for (int j = 0; j < kNumJoints; j++) {
        Vector3 pos = person.joints[j];
        JointConfidence conf = person.confidence[j];

        if (canHold) {
            if (conf > kConfidenceNotTracked) {
                sLastGoodJointPos[slot][j] = pos;
                sHaveLastGoodJoint[slot][j] = true;
            } else if (sHaveLastGoodJoint[slot][j]) {
                pos = sLastGoodJointPos[slot][j];
            }
        }

        // The hold above caches the CLEAN position; the floor is added on the
        // way out, so a held joint keeps moving by sensor noise and does not
        // random-walk.
        pos = WithSensorNoiseFloor(pos, noiseFrame, j);
        data.mJointPositions[j].Set(pos.x, pos.y, pos.z);
        data.mRawPositions[j].Set(pos.x, pos.y, pos.z);
        data.mJointTrackingState[j] = conf;
    }
    data.mTracking = kSkeletonTracked;
    data.mTrackingID = person.trackId;
    data.mQualityFlags = 0;
    // Xbox sets this from the NUI body position; SkeletonQualityFilter treats a
    // zero root as "no data" and forces mValid/mSitting/mSideways all false,
    // which makes Skeleton::IsValid() permanently false (breaking
    // ShellInput::HasSkeleton and the player binding).
    data.mHipCenter = data.mJointPositions[kJointHipCenter];
}

const int NativeSkeletonProvider::kDummyTrackingId[2] = { 1, 2 };

void NativeSkeletonProvider::FillDummySkeletonData(SkeletonData &data, int dummy) {
    if (dummy < 0 || dummy > 1)
        dummy = 0;
    // Neutral standing pose — hands at sides, below hip height.
    // Passes quality filter (20 confident joints, not sitting/sideways)
    // but gesture filters see disengaged player (hands below hips).
    static const struct { SkeletonJoint joint; float x, y, z; } kPose[] = {
        { kJointHipCenter,       0.00f, 0.90f, 2.0f },
        { kJointSpine,           0.00f, 1.10f, 2.0f },
        { kJointShoulderCenter,  0.00f, 1.40f, 2.0f },
        { kJointHead,            0.00f, 1.60f, 2.0f },
        { kJointShoulderLeft,   -0.20f, 1.40f, 2.0f },
        { kJointElbowLeft,      -0.25f, 1.15f, 2.0f },
        { kJointWristLeft,      -0.22f, 0.90f, 2.0f },
        { kJointHandLeft,       -0.22f, 0.85f, 2.0f },
        { kJointShoulderRight,   0.20f, 1.40f, 2.0f },
        { kJointElbowRight,      0.25f, 1.15f, 2.0f },
        { kJointWristRight,      0.22f, 0.90f, 2.0f },
        { kJointHandRight,       0.22f, 0.85f, 2.0f },
        { kJointHipLeft,        -0.12f, 0.85f, 2.0f },
        { kJointKneeLeft,       -0.12f, 0.45f, 2.0f },
        { kJointAnkleLeft,      -0.12f, 0.05f, 2.0f },
        { kJointHipRight,        0.12f, 0.85f, 2.0f },
        { kJointKneeRight,       0.12f, 0.45f, 2.0f },
        { kJointAnkleRight,      0.12f, 0.05f, 2.0f },
        { kJointFootLeft,       -0.12f, 0.00f, 2.0f },
        { kJointFootRight,       0.12f, 0.00f, 2.0f },
    };

    // The static table would be bit-for-bit still; the sensor noise floor
    // (SensorNoiseFloor at the top of this file) keeps it a camera's signal.
    // Same counter and hash the dummy has always used, so its output is
    // unchanged by the floor moving to a shared helper.
    // Dummy 1 counts separately and from a distant start, so the two
    // stand-ins never carry the same noise sample.
    static uint32_t sDummyFrame[2] = { 0, 0x40000000u };
    uint32_t noiseFrame = ++sDummyFrame[dummy];
    // Dummy 1 stands where scripts/synthetic_kinect.py's second person does
    // (+0.4 m): measured with that sensor, the image's SkeletonChooser then
    // keeps player 0 on side 0 and player 1 on side 1 through a whole battle.
    const float xOffset = dummy == 0 ? 0.0f : 0.40f;

    for (const auto &j : kPose) {
        Vector3 pos = WithSensorNoiseFloor(Vector3(j.x + xOffset, j.y, j.z), noiseFrame, j.joint);
        data.mJointPositions[j.joint].Set(pos.x, pos.y, pos.z);
        data.mRawPositions[j.joint].Set(pos.x, pos.y, pos.z);
        data.mJointTrackingState[j.joint] = kConfidenceTracked;
    }

    data.mTracking = kSkeletonTracked;
    data.mTrackingID = kDummyTrackingId[dummy];
    data.mQualityFlags = 0;
    data.mHipCenter = data.mJointPositions[kJointHipCenter];
}
