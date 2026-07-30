#include "Skeleton_Native.h"
#include "gesture/GestureMgr.h" // NUM_SKELETONS
#include <cstring>
#include <cstdio>
#include <cstdlib>
#ifndef __EMSCRIPTEN__
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <poll.h>
#ifdef __linux__
#include <linux/limits.h>
#else
#include <limits.h>
#endif
#endif

NativeSkeletonProvider *TheSkeletonProvider = nullptr;

#ifdef __EMSCRIPTEN__
// Stubs — no Kinect skeleton tracking on web
NativeSkeletonProvider::NativeSkeletonProvider() { memset(mFront, 0, sizeof(mFront)); memset(mBack, 0, sizeof(mBack)); memset(mPersons, 0, sizeof(mPersons)); }
NativeSkeletonProvider::~NativeSkeletonProvider() {}
bool NativeSkeletonProvider::Start(const std::string&, const std::string&, int, const std::string&) { return false; }
void NativeSkeletonProvider::Stop() {}
void NativeSkeletonProvider::Poll() {}
int NativeSkeletonProvider::FindByTrackId(int) const { return -1; }
Vector3 NativeSkeletonProvider::NormalizedToMeters(float, float) const { return Vector3(0,0,0); }
void NativeSkeletonProvider::MapCOCOToDC3(const float[][3], PersonData&) {}
void NativeSkeletonProvider::ResetJointHold(int) {}
void NativeSkeletonProvider::FillSkeleton(Skeleton&, int) const {}
#else

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

bool NativeSkeletonProvider::Start(
    const std::string &socketPath, const std::string &modelPath, int cameraIndex,
    const std::string &backend
) {
    if (mRunning) return true;

    mSocketPath = socketPath;

    // DC3_POSE_NO_SPAWN=1: connect-only mode — attach to an already-running
    // pose server (e.g. a synthetic one in CI) instead of forking pose_server.py.
    if (getenv("DC3_POSE_NO_SPAWN")) {
        printf("DC3_POSE_NO_SPAWN: connecting to existing pose server at %s\n",
            socketPath.c_str());
    } else {
        // Launch pose_server.py as child process
        // Resolve script path relative to the executable location
        std::string scriptPath;
        {
            char exePath[PATH_MAX] = {};
            ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
            if (len > 0) {
                exePath[len] = '\0';
                // Walk up from executable (native/build/milo-viewer) to project root
                std::string dir(exePath);
                // Strip executable name
                size_t slash = dir.rfind('/');
                if (slash != std::string::npos) dir = dir.substr(0, slash);
                // Strip "build" directory
                slash = dir.rfind('/');
                if (slash != std::string::npos) dir = dir.substr(0, slash);
                scriptPath = dir + "/scripts/pose_server.py";
            } else {
                scriptPath = "native/scripts/pose_server.py";
            }
        }

        mServerPid = fork();
        if (mServerPid == 0) {
            // Child process
            execlp("python3", "python3",
                   scriptPath.c_str(),
                   "--socket", socketPath.c_str(),
                   "--model", modelPath.c_str(),
                   "--backend", backend.c_str(),
                   "--camera", std::to_string(cameraIndex).c_str(),
                   nullptr);
            // If exec fails
            perror("Failed to launch pose_server.py");
            _exit(1);
        } else if (mServerPid < 0) {
            perror("fork failed");
            return false;
        }

        printf("Launched pose_server.py (pid %d)\n", mServerPid);
    }

    // Wait for socket to appear, then connect
    for (int attempt = 0; attempt < 50; attempt++) {
        usleep(100000); // 100ms

        mSocketFd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (mSocketFd < 0) continue;

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(mSocketFd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            printf("Connected to pose server\n");
            mRunning = true;
            mReaderThread = std::thread(&NativeSkeletonProvider::ReaderThread, this);
            return true;
        }

        close(mSocketFd);
        mSocketFd = -1;
    }

    fprintf(stderr, "Failed to connect to pose server after 5s\n");
    Stop();
    return false;
}

void NativeSkeletonProvider::Stop() {
    mRunning = false;

    if (mReaderThread.joinable()) {
        mReaderThread.join();
    }

    if (mSocketFd >= 0) {
        close(mSocketFd);
        mSocketFd = -1;
    }

    if (mServerPid > 0) {
        kill(mServerPid, SIGTERM);
        int status;
        waitpid(mServerPid, &status, WNOHANG);
        mServerPid = -1;
    }
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
    while (mRunning) {
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
            mRunning = false;
            break;
        }

        // Read packet
        std::vector<uint8_t> packet(packetLen);
        if (!readExact(mSocketFd, packet.data(), packetLen)) {
            fprintf(stderr, "Pose server read error\n");
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
        static const uint32_t kProtocolMagic = 0x44503302u;
        if (packetLen < 16) continue;
        const uint8_t *p = packet.data();
        const uint8_t *end = packet.data() + packetLen;

        uint32_t magic = 0;
        memcpy(&magic, p, 4);
        const bool v2 = (magic == kProtocolMagic);

        uint32_t frameId, numPersons;
        double timestamp;
        uint16_t frameW = 0, frameH = 0;
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
            p += 2; // pad
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
        (void)frameW;
        (void)frameH;

        // Swap to back buffer
        {
            std::lock_guard<std::mutex> lock(mSwapMutex);
            memcpy(mBack, newBack, sizeof(mBack));
            mNumPersonsBack = numPersons;
            mFrameIdBack = frameId;
        }
    }
}

void NativeSkeletonProvider::Poll() {
    std::lock_guard<std::mutex> lock(mSwapMutex);
    memcpy(mPersons, mBack, sizeof(mPersons));
    mNumPersons = mNumPersonsBack;
    mFrameIdFront = mFrameIdBack;
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
    float y = (0.5f - ny) * mViewHeight; // flip Y (image Y is down)
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

void NativeSkeletonProvider::FillSkeleton(Skeleton &skel, int personIdx) const {
    if (personIdx < 0 || personIdx >= mNumPersons || !mPersons[personIdx].valid)
        return;
    FillSkeleton(skel, mPersons[personIdx]);
}

// Last-good camera-space position per skeleton slot, for the confidence hold in
// FillSkeleton. Nothing under src/system/hamobj/ reads JointConf -- the scorer
// consumes mJointPos unconditionally -- so a keypoint the detector has no
// confidence in is otherwise graded as ground truth. A real Kinect never presents
// a garbage joint: it fills occluded joints from its own skeletal model and flags
// them inferred. Holding the last good position is the closest equivalent, and it
// also keeps displacement scoring sane (a held joint reads as stationary rather
// than as a large spurious velocity).
static Vector3 sLastGoodJointPos[NUM_SKELETONS][kNumJoints];
static bool sHaveLastGoodJoint[NUM_SKELETONS][kNumJoints];

void NativeSkeletonProvider::ResetJointHold(int skelIdx) {
    if (skelIdx < 0 || skelIdx >= NUM_SKELETONS)
        return;
    for (int j = 0; j < kNumJoints; j++)
        sHaveLastGoodJoint[skelIdx][j] = false;
}

void NativeSkeletonProvider::FillSkeleton(Skeleton &skel, const PersonData &person) const {
    // mSkeletonIdx is assigned by FinalizeSkeletonFrame, which runs AFTER this, so
    // on the very first fill of a slot it may still be -1; hold is simply disabled
    // until the slot is known.
    int slot = skel.mSkeletonIdx;
    bool canHold = (slot >= 0 && slot < NUM_SKELETONS);

    // Access protected members directly via friend declaration (LP64-safe)
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

        skel.mTrackedJoints[j].mJointPos[kCoordCamera] = pos;
        skel.mTrackedJoints[j].mSmoothedPos = pos;
        skel.mTrackedJoints[j].mJointConf = conf;
    }

    skel.mTracking = kSkeletonTracked;
    skel.mTrackingID = person.trackId;
}

#endif // !__EMSCRIPTEN__

void NativeSkeletonProvider::FillDummySkeleton(Skeleton &skel) {
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

    for (const auto &j : kPose) {
        Vector3 pos(j.x, j.y, j.z);
        skel.mTrackedJoints[j.joint].mJointPos[kCoordCamera] = pos;
        skel.mTrackedJoints[j.joint].mSmoothedPos = pos;
        skel.mTrackedJoints[j.joint].mJointConf = kConfidenceTracked;
    }

    skel.mTracking = kSkeletonTracked;
    skel.mTrackingID = 1;
}

void NativeSkeletonProvider::FinalizeSkeletonFrame(Skeleton &skel, int skelIdx, int elapsedMs) {
    skel.mSkeletonIdx = skelIdx;
    skel.mElapsedMs = elapsedMs;

    // Xbox Skeleton::Poll caches every bone length here, and Skeleton::BoneLength
    // returns that cache directly rather than recomputing. Leaving it zeroed makes
    // ErrorNode's norm_bones divisor zero, so PositionNode/DisplacementNode take
    // their "no base bone length" path and emit MAXIMUM error (1,1,1) for every
    // node — DetectFrac then pins at exactly 0 no matter what the player does.
    // (DC3_POSE_SELFTEST hid this: it substitutes a DancerSkeleton, which computes
    // its bone lengths lazily on demand.)
    for (int i = 0; i < kNumBones; i++) {
        skel.mCamBoneLengths[i] = skel.BaseSkeleton::BoneLength((SkeletonBone)i, kCoordCamera);
    }

    // Xbox sets this from the NUI body position; SkeletonQualityFilter treats a
    // zero root as "no data" and forces mValid/mSitting/mSideways all false, which
    // makes Skeleton::IsValid() permanently false (breaking ShellInput::HasSkeleton
    // and HamGameData::AutoAssignSkeletons player binding).
    skel.unkab0 = skel.mTrackedJoints[kJointHipCenter].mJointPos[kCoordCamera];

    skel.mCamDisplacements.clear();
}

void NativeSkeletonProvider::MarkUntracked(Skeleton &skel) {
    skel.Init();
    skel.mTrackingID = -1;
}
