#pragma once

#include "gesture/BaseSkeleton.h"
#include "gesture/Skeleton.h"
#include <cstdint>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
// <atomic> not usable with clang + GCC 15 headers; use volatile bool instead
// #include <atomic>

// GestureMgr_Native.cpp: how many stand-in dummies the sensor shows when no
// pose provider runs (1, or 2 in a two-player mode; DC3_DUMMY_PLAYERS=1|2
// overrides).  Pure, so the policy is testable without an engine.
int NativeDummyPlayerCount(bool modeNeedsTwo, const char *policy);
// GestureMgr_Native.cpp's stalled-source watchdog (DC3_POSE_STALL_MS): how
// many times a running pose source went silent long enough for the sensor to
// report nobody, and whether it is silent now.
unsigned NativePoseStalls();
bool NativePoseStalled();

// Receives skeleton data from pose_server.py (MediaPipe BlazePose) over a
// Unix socket. Protocol layout 1 carries DC3's own 20 joints in camera-space
// metres; the layout-0 path maps COCO-17 2D keypoints for any external
// COCO source (the in-tree YOLO backend that used it is retired).
class NativeSkeletonProvider {
public:
    static const int kCOCOKeypoints = 17;
    static const int kMaxPersons = 6;

    struct PersonData {
        int trackId = -1;
        Vector3 joints[kNumJoints];           // DC3 20-joint positions (meters)
        JointConfidence confidence[kNumJoints];
        bool valid = false;
    };

    NativeSkeletonProvider();
    ~NativeSkeletonProvider();

    // Exit codes native/scripts/pose_server.py uses for a failure the player
    // should hear about (keep in sync with its EXIT_* constants), plus the
    // spawned child's own exec failure.
    enum ServerExit {
        kExitNoCamera = 3,    // camera would not open, or opened and sent no frame
        kExitNoVideo = 4,     // --video file unreadable
        kExitNoModel = 5,     // the .task model file is missing
        kExitNoDeps = 6,      // mediapipe / opencv / numpy not importable
        kExitCameraLost = 7,  // the camera stopped delivering frames mid-run
        kExitExecFailed = 127 // python3 itself could not be run
    };

    // What went wrong, in the player's words: `text` for a banner, `brief` for
    // a one-line reminder.  Empty while the provider is healthy.
    struct Problem {
        std::string text;
        std::string brief;
        bool empty() const { return text.empty(); }
    };

    // The problem the server's exit says it had.  `waitStatus` is waitpid's
    // status word; `camera` (pose_server.py --camera: auto, an index, a path or
    // a /dev/v4l/by-id name) names the camera in the message.
    // `python` names the interpreter in the notices that are about it (no
    // packages, cannot start).
    static Problem DescribeServerExit(int waitStatus, const std::string &camera,
                                      const std::string &python = "python3");
    static Problem DescribeServerExit(int waitStatus, int cameraIndex) {
        return DescribeServerExit(waitStatus, std::to_string(cameraIndex));
    }

    // While the server has no camera (pose_server.py FLAG_NO_CAMERA: the
    // camera vanished mid-session -- a USB camera re-enumerating -- and the
    // server is looking for it again), these notices are up: one when it goes,
    // one when it is back.  The camera-lost one stays (corner line) until then.
    static Problem CameraLostProblem();
    static Problem CameraBackNotice();
    bool CameraMissing() const { return mNoCamera; }

    // Why the last Start() failed, or why a running server stopped (the reader
    // posts it when the socket closes).  Thread-safe copy.
    Problem LastProblem();

    // The MediaPipe model the spawned server loads when DC3_POSE_MODEL is
    // unset: native/models/pose_landmarker_full.task of the tree the
    // executable was built in, as an absolute path -- the working directory
    // is native/build for a windowed boot and orig-assets under ctest, so a
    // cwd-relative default only ever worked from the repo root (PLAN P-10).
    static std::string DefaultModelPath();

    // `camera` is passed to pose_server.py --camera as is: "auto" (the first
    // V4L2 video-capture device, re-resolved whenever the server reopens it),
    // an index, a /dev/videoN path or a /dev/v4l/by-id name.
    //
    // The pose server's command line, argv[0] first: the interpreter
    // (ChoosePosePython), the script, --socket, --model, then --video FILE
    // --loop when DC3_POSE_VIDEO is set (footage instead of the camera; the
    // playtest kit's --video) or --camera CAMERA, --hfov (DC3_POSE_HFOV),
    // --frames (DC3_CAMERA_FRAMES).  LaunchServer() runs it; DC3_POSE_SERVER
    // replaces the script with a drop-in that takes the same arguments.
    static std::vector<std::string> ServerCommand(const std::string &scriptPath,
                                                  const std::string &socketPath,
                                                  const std::string &modelPath,
                                                  const std::string &camera);
    static std::vector<std::string> ServerCommand(const std::string &scriptPath,
                                                  const std::string &socketPath,
                                                  const std::string &modelPath, int cameraIndex) {
        return ServerCommand(scriptPath, socketPath, modelPath, std::to_string(cameraIndex));
    }

    // The Python the pose server runs under, and why it was chosen.  In order:
    // DC3_POSE_PYTHON if set; the playtest venv
    // ${DC3_POSE_VENV:-~/.local/share/dc3/pose-venv}/bin/python3 if it is
    // there (scripts/playtest/bootstrap.sh --fix makes it); the checkout's
    // .venv/bin/python3 (in a git worktree, the main checkout's) if it is
    // there; else python3 from PATH.  `treeRoot` is the checkout (the
    // directory holding native/); `home` is $HOME.  Before 2026-11-14 the game
    // always ran bare python3, so a game started outside play.sh on a box
    // whose packages live in the venv had no tracking (exit 6, cv2 missing:
    // the GPD, main 9a53ff273).
    struct PythonChoice {
        std::string path;
        std::string why;
    };
    static PythonChoice ChoosePosePython(const std::string &treeRoot, const char *home);
    static PythonChoice ChoosePosePython(); // this process's tree and $HOME

    bool Start(const std::string &socketPath = "/tmp/dc3_pose.sock",
               const std::string &modelPath = DefaultModelPath(),
               const std::string &camera = "auto");
    bool Start(const std::string &socketPath, const std::string &modelPath,
               int cameraIndex) {
        return Start(socketPath, modelPath, std::to_string(cameraIndex));
    }
    void Stop();
    bool IsRunning() const { return mRunning; }

    // Restart-on-death.  When a server this provider launched dies after it
    // had connected, the reader relaunches it -- at most `maxRestarts` times
    // per Start(), waiting baseBackoffMs, then twice that, and so on -- with a
    // one-line notice each time.  An exit that a restart cannot fix (the model
    // or the Python packages missing, python3 not runnable, a clean exit) is
    // not retried.  Defaults: 3 restarts, 1000 ms.  Call before Start().
    void SetRestartPolicy(int maxRestarts, int baseBackoffMs) {
        mMaxRestarts = maxRestarts;
        mRestartBackoffMs = baseBackoffMs;
    }
    int Restarts() const { return mRestarts; }
    // A launched server that stays connected but sends nothing for this long
    // (hung in inference, SIGSTOPped, a camera read that never returns) is
    // killed and relaunched under the same policy.  0 = never.  Default
    // DC3_POSE_SERVER_STALL_MS, else kDefaultServerStallMs.  Before Start().
    static const int kDefaultServerStallMs = 5000;
    void SetServerStallMs(int ms) { mServerStallMs = ms; }
    int ServerStalls() const { return mServerStalls; }
    // pid of the pose_server.py this provider launched, or -1.
    int ServerPid() const { return (int)mServerPid; }
    // Whether DescribeServerExit's exit is one a relaunch could fix.
    static bool ServerExitIsRestartable(int waitStatus);

    // Publish one frame of persons into the back buffer; the next Poll() latches
    // it.  This is the provider's whole contract with its transports: the
    // desktop socket reader calls it once per packet, and on web the page's
    // camera pipeline calls it through dc3_pose_push (PoseInput_Web.cpp).
    // Everything downstream -- Poll, FrameId/Timestamp, FillSkeletonData -- is
    // shared, so BuildSensorFrame reads both transports through one branch.
    // `unlevelled`: the frame comes from a camera that does not know its own
    // gravity (pose_server.py's header flag, every web push), so the sensor
    // estimates the camera's tilt and levels it (platform/PoseLevel_Native.h).
    // `declaredLatencyMs`: the source's own scoring latency (pose_server.py
    // declares 0 for a recorded file), or -1 for none -- see
    // Dc3SensorLatencySeconds.
    void PublishFrame(const PersonData *persons, int numPersons, uint32_t frameId,
                      double timestamp, bool unlevelled = false, int declaredLatencyMs = -1);

#ifdef __EMSCRIPTEN__
    // Web has no server to spawn or socket to read: the provider is "running"
    // while the page is pushing frames (StartPushed, on its first push) and
    // stops when the page loses its camera (Stop), which returns the sensor to
    // the dummy skeleton.
    void StartPushed() { mRunning = true; }
#endif

    // Call each frame to read latest data
    void Poll();

    // Access tracked persons (thread-safe snapshot from last Poll)
    int NumPersons() const { return mNumPersons; }
    const PersonData &GetPerson(int idx) const { return mPersons[idx]; }

    // frame_id of the pose packet snapshotted by the last Poll(). The pose
    // server runs at camera rate (slower than the game loop); scoring gates the
    // archive/fill on this changing so a single camera frame is not integrated
    // multiple times.
    uint32_t FrameId() const { return mFrameIdFront; }

    // Sensor capture timestamp (seconds) of that packet, as the server stamped
    // it -- the v1/v2 header's f64 ts.  Xbox takes a skeleton frame's elapsed
    // time from NUI_SKELETON_FRAME's own timestamp, not from when the game got
    // round to reading it; GestureMgr_NativePoll does the same with this.
    double Timestamp() const { return mTimestampFront; }

    // Whether that packet came from a camera that does not know its gravity
    // (PublishFrame's `unlevelled`).
    bool FrameUnlevelled() const { return mUnlevelledFront; }

    // Find person by BOTSORT track ID, returns -1 if not found
    int FindByTrackId(int trackId) const;

    // Write one TRACKED slot of a SkeletonFrame from person data, in the shape
    // SkeletonFrame::Create gives a NUI-tracked slot: camera-space joints in
    // mJointPositions and mRawPositions, per-joint confidence, tracking id, hip
    // centre.  SkeletonUpdate::UpdateCallbacks then hands the frame to the
    // image's own Skeleton::Poll, which derives everything past camera space.
    // `slot` keys the low-confidence joint hold.  mClippedFlags (the NUI
    // enrollment index) is left to the caller.
    static void FillSkeletonData(SkeletonData &data, const PersonData &person, int slot);

    // The same for a neutral standing pose (hands at sides).  Used as the
    // native sensor's frame when no pose provider is running.  `dummy` 0 is
    // the solo stand-in: tracking id 1, centred.  `dummy` 1 is the second
    // stand-in BuildSensorFrame adds in two-player modes: tracking id 2, at
    // x = +0.4 m, where scripts/synthetic_kinect.py's second person stands,
    // so SkeletonChooser keeps the two on opposite sides.  Each
    // has its own noise-floor counter, so dummy 0's signal does not change
    // with the number of dummies.
    static void FillDummySkeletonData(SkeletonData &data, int dummy = 0);
    static const int kDummyTrackingId[2];

    // Drop the low-confidence joint-hold cache for a slot. Must be called when a
    // slot changes occupants, or the incoming person inherits the previous
    // person's held joint positions.
    static void ResetJointHold(int skelIdx);
    // Restart the sensor noise floor's per-slot frame counters (Skeleton_Native.cpp,
    // SensorNoiseFloor), so the offsets that follow are those of a fresh
    // process's first frames.  A session replay calls it when its song starts:
    // the counters otherwise carry how many frames the menus happened to take.
    static void ResetSensorNoiseFloor();

private:
    void ReaderThread();
    // Record a problem and show it to the player (NativeNotice key "pose").
    void ReportProblem(const Problem &p);
    // Reader thread: the socket closed.  Reports why and, for a server this
    // provider launched, relaunches it per the restart policy.  True when a
    // relaunched server is connected again (mSocketFd is the new socket).
    bool ServerGone();
    // Launch pose_server.py through NativeSpawner; sets mServerPid.
    bool LaunchServer();
    enum WaitResult { kConnected, kServerExited, kTimedOut, kStopped };
    // Wait for the server's socket and connect (mSocketFd).  kServerExited
    // fills *exitStatus.
    WaitResult WaitForServer(int timeoutMs, int *exitStatus);
    void MapCOCOToDC3(const float cocoKpts[][3], PersonData &out);
    Vector3 NormalizedToMeters(float nx, float ny) const;

    std::string mSocketPath;
    std::string mModelPath;
    int mSocketFd = -1;
    pid_t mServerPid = -1;
    bool mSpawned = false; // this provider launched the server (not DC3_POSE_NO_SPAWN)
    std::string mCamera = "auto";
    std::string mPython = "python3"; // the interpreter LaunchServer chose (for notices)
    volatile bool mNoCamera = false; // the last packet carried FLAG_NO_CAMERA
    int mMaxRestarts = 3;
    int mRestartBackoffMs = 1000;
    volatile int mRestarts = 0;
    int mServerStallMs = -1;            // -1 = DC3_POSE_SERVER_STALL_MS / the default
    volatile int mServerStalls = 0;
    volatile bool mServerStalled = false; // ServerGone: this server was killed for stalling
    Problem mProblem; // guarded by mSwapMutex

    std::thread mReaderThread;
    volatile bool mRunning = false;
    volatile bool mStopRequested = false;

    // Double-buffered: reader writes to mBack, Poll() swaps to mFront
    std::mutex mSwapMutex;
    PersonData mFront[kMaxPersons];
    PersonData mBack[kMaxPersons];
    int mNumPersonsFront = 0;
    int mNumPersonsBack = 0;
    int mNumPersons = 0;
    PersonData mPersons[kMaxPersons]; // Snapshot for game thread

    // Latest packet frame_id (reader writes mFrameIdBack, Poll() latches Front)
    uint32_t mFrameIdBack = 0;
    uint32_t mFrameIdFront = 0;
    double mTimestampBack = 0.0;
    double mTimestampFront = 0.0;
    bool mUnlevelledBack = false;
    bool mUnlevelledFront = false;
    int mDeclaredLatencyMsBack = -1;

    // Coordinate mapping: camera view extent in metres at mViewDepth.
    //
    // These are NOT guesses. NuiTransformSkeletonToDepthImage is declared in
    // src/xdk/nui/nuiskeleton.h but never defined in-tree (it links from the XDK),
    // so the projection was recovered from the target disassembly at
    // build/373307D9/asm/system/gesture/JointUtl.s:580-627 (0x824435E0):
    //
    //     u = 160 + 285.63 * x/z        v = 120 - 285.63 * y/z
    //
    // over a 320x240 depth image -- hFOV 58.51 deg, vFOV 45.58 deg. Corroborated
    // in-tree by JointUtl.cpp:89,103 normalising by 1/320 and 1/240, and by
    // LiveCameraInput.cpp:494 opening depth at NUI_IMAGE_RESOLUTION_320x240.
    //
    // Solving for the full-frame extent at z: width = 2*z*160/285.63. At z = 3.0
    // that is 3.361 x 2.521 m, NOT the 2.4 x 1.8 previously hardcoded -- which
    // corresponded to z = 2.14 m and so under-scaled x and y by ~1.40x relative
    // to this same struct's fixed z = 3.0. x/y and z were mutually inconsistent,
    // which would silently corrupt any depth estimate scored against them.
    //
    // The vertical extent is derived from the width and the ACTUAL frame aspect
    // (square pixels: viewH = viewW * H/W). Protocol v2 carries the camera frame
    // dimensions, so a 16:9 webcam no longer gets its normalised coords squeezed
    // through a 4:3 box (which anisotropically skewed every limb angle on the
    // COCO/YOLO path). v1 packets carry no dimensions and keep the Kinect 4:3.
    float mViewWidth = 3.361f;  // 2 * 3.0 * 160 / 285.63
    float mViewDepth = 3.0f;    // meters from camera to subject (fixed Z)
    // H/W of the incoming camera frame. Written by the reader thread from the
    // v2 header; only read by NormalizedToMeters, which also runs on the reader
    // thread (via MapCOCOToDC3), so no synchronisation is needed.
    float mFrameAspect = 0.75f; // Kinect 320x240 default until a v2 header says otherwise
};

extern NativeSkeletonProvider *TheSkeletonProvider;

// The model GestureMgr_NativeInit passes to Start(): $DC3_POSE_MODEL, else
// NativeSkeletonProvider::DefaultModelPath() (GestureMgr_Native.cpp).
std::string GestureMgr_NativePoseModelPath();

// Scoring latency (Skeleton_Native.cpp, "Sensor latency"): explicit
// DC3_POSE_LATENCY_MS beats the source's declaration beats scoring.dta's
// latency_offset.
enum Dc3LatencySource { kLatencyDefault = 0, kLatencyDeclared = 1, kLatencyExplicit = 2 };
// MoveDir::LoadScoring's hook: the latency the scorer should use.
float Dc3SensorLatencySeconds(float scoringDtaSeconds);
// The latency in force for `scoringDtaSeconds`, and why (no logging).
Dc3LatencySource Dc3ResolveSensorLatency(float scoringDtaSeconds, float *seconds);
const char *Dc3LatencySourceName(Dc3LatencySource source);
// The pose source declared `ms` (-1 = nothing).  Main thread; applied to the
// scorer at once if LoadScoring has run.  NativeSkeletonProvider::Poll calls it.
void Dc3DeclareSourceLatencyMs(int ms);
int Dc3DeclaredSourceLatencyMs();
