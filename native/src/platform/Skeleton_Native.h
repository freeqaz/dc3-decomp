#pragma once

#include "gesture/BaseSkeleton.h"
#include "gesture/Skeleton.h"
#include <cstdint>
#include <string>
#include <thread>
#include <mutex>
// <atomic> not usable with clang + GCC 15 headers; use volatile bool instead
// #include <atomic>

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

    bool Start(const std::string &socketPath = "/tmp/dc3_pose.sock",
               const std::string &modelPath = "native/models/pose_landmarker_full.task",
               int cameraIndex = 0);
    void Stop();
    bool IsRunning() const { return mRunning; }

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

    // The same for a neutral standing pose (hands at sides), tracking id 1.
    // Used as the native sensor's frame when no pose provider is running.
    static void FillDummySkeletonData(SkeletonData &data);

    // Drop the low-confidence joint-hold cache for a slot. Must be called when a
    // slot changes occupants, or the incoming person inherits the previous
    // person's held joint positions.
    static void ResetJointHold(int skelIdx);

private:
    void ReaderThread();
    void MapCOCOToDC3(const float cocoKpts[][3], PersonData &out);
    Vector3 NormalizedToMeters(float nx, float ny) const;

    std::string mSocketPath;
    int mSocketFd = -1;
    pid_t mServerPid = -1;

    std::thread mReaderThread;
    volatile bool mRunning = false;

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
