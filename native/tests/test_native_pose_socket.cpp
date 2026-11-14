// Desktop pose input, socket end (docs/native/POSE_INPUT.md, PLAN P-2 / P-5a).
//
// Everything a real webcam session depends on between pose_server.py and the
// image's Skeleton::Poll, exercised with no camera, no GPU and no Python:
//
//   PoseSocketReaderTest   NativeSkeletonProvider::ReaderThread over a real
//                          Unix socket served by this test: protocol v2 with
//                          DC3-20 (layout 1) and COCO-17 (layout 0), legacy v1,
//                          a truncated packet, an unknown layout, a disconnect.
//   PoseCocoMappingTest    MapCOCOToDC3 / NormalizedToMeters through the
//                          socket, against an independent reference.
//   PoseSensorSlotTest     GestureMgr_Native.cpp's AssignSlots contract, read
//                          where the image reads it: the SkeletonFrame the
//                          native sensor hands SkeletonUpdate (PollNewFrame).
//   PoseSensorLevelTest    the camera-tilt step on that frame: a packet
//                          flagged "camera does not know its gravity" is
//                          levelled by the game (mJointPositions), the raw
//                          joints kept (mRawPositions); an unflagged packet,
//                          or DC3_POSE_LEVEL=0, is untouched.
//   PoseServerStartTest    Start()'s no-camera path: the spawned server's exit
//                          is noticed at once and becomes a player notice.
//   PoseSessionTest        DC3_POSE_RECORD / session: replay (PLAN L-1): what a
//                          live sensor accepted, recorded and replayed, reaches
//                          the scorer as it did live -- levelled by the tilt
//                          estimate the live session had built up.
//
// Each case was sabotage-checked against the code it pins (see the commit).

#include <gtest/gtest.h>

#include "test_helpers.h" // EnsureSymbolInit
#include "platform/NativeNotice.h"
#include "platform/Skeleton_Native.h"
#include "platform/PoseLevel_Native.h"
#include "platform/PoseSession_Native.h"
#include "gesture/CameraInput.h"
#include "gesture/GestureMgr.h" // NUM_SKELETONS
#include "gesture/SkeletonUpdate.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

void GestureMgr_NativePreInit(); // GestureMgr_Native.cpp

namespace {

const uint32_t kMagic = 0x44503302u; // Skeleton_Native.cpp kProtocolMagic

// ---------------------------------------------------------------------------
// Wire builders (the layouts pose_server.py and synthetic_kinect.py emit)
// ---------------------------------------------------------------------------
struct Bytes {
    std::vector<uint8_t> b;
    template <typename T> void put(T v) {
        const uint8_t *p = (const uint8_t *)&v;
        b.insert(b.end(), p, p + sizeof(T));
    }
};

struct WirePerson {
    int32_t trackId;
    std::vector<float> values; // landmarks x comps, flat
};

std::vector<uint8_t> PacketV2(uint32_t frameId, double ts, uint16_t w, uint16_t h,
                              uint8_t landmarks, uint8_t layout,
                              const std::vector<WirePerson> &persons,
                              int declaredPersons = -1, uint16_t flags = 0) {
    Bytes body;
    body.put<uint32_t>(kMagic);
    body.put<uint32_t>(frameId);
    body.put<uint32_t>(declaredPersons >= 0 ? declaredPersons : (uint32_t)persons.size());
    body.put<double>(ts);
    body.put<uint16_t>(w);
    body.put<uint16_t>(h);
    body.put<uint8_t>(landmarks);
    body.put<uint8_t>(layout);
    body.put<uint16_t>(flags); // bit 0: a camera that does not know its gravity
    for (const WirePerson &p : persons) {
        body.put<int32_t>(p.trackId);
        for (float v : p.values)
            body.put<float>(v);
    }
    Bytes msg;
    msg.put<uint32_t>((uint32_t)body.b.size());
    msg.b.insert(msg.b.end(), body.b.begin(), body.b.end());
    return msg.b;
}

std::vector<uint8_t> PacketV1(uint32_t frameId, double ts, const std::vector<WirePerson> &persons) {
    Bytes body;
    body.put<uint32_t>(frameId);
    body.put<uint32_t>((uint32_t)persons.size());
    body.put<double>(ts);
    for (const WirePerson &p : persons) {
        body.put<int32_t>(p.trackId);
        for (float v : p.values)
            body.put<float>(v);
    }
    Bytes msg;
    msg.put<uint32_t>((uint32_t)body.b.size());
    msg.b.insert(msg.b.end(), body.b.begin(), body.b.end());
    return msg.b;
}

// DC3-20 person in camera-space metres: a standing pose shifted by `dx`, every
// joint at confidence `conf` (4 comps per joint).
WirePerson Dc3Person(int trackId, float dx, float conf = 0.9f) {
    static const float kPose[kNumJoints][3] = {
        { 0.00f, 0.90f, 2.0f },  { 0.00f, 1.10f, 2.0f },  { 0.00f, 1.40f, 2.0f },
        { 0.00f, 1.60f, 2.0f },  { -0.20f, 1.40f, 2.0f }, { -0.30f, 1.60f, 1.9f },
        { -0.32f, 1.85f, 1.9f }, { -0.33f, 1.92f, 1.9f }, { 0.20f, 1.40f, 2.0f },
        { 0.25f, 1.15f, 2.0f },  { 0.22f, 0.90f, 2.0f },  { 0.22f, 0.85f, 2.0f },
        { -0.12f, 0.85f, 2.0f }, { -0.12f, 0.45f, 2.1f }, { -0.12f, 0.05f, 2.0f },
        { 0.12f, 0.85f, 2.0f },  { 0.12f, 0.45f, 2.0f },  { 0.12f, 0.05f, 2.0f },
        { -0.12f, 0.00f, 1.9f }, { 0.12f, 0.00f, 1.9f },
    };
    WirePerson p;
    p.trackId = trackId;
    for (int j = 0; j < kNumJoints; j++) {
        p.values.push_back(kPose[j][0] + dx);
        p.values.push_back(kPose[j][1]);
        p.values.push_back(kPose[j][2]);
        p.values.push_back(conf);
    }
    return p;
}

// COCO-17 keypoints in normalised image coords (x right, y down), a person
// facing the camera: their LEFT side is at larger image x.
const float kCoco[17][3] = {
    { 0.50f, 0.20f, 0.9f }, // nose
    { 0.52f, 0.18f, 0.9f }, // left eye
    { 0.48f, 0.18f, 0.9f }, // right eye
    { 0.55f, 0.19f, 0.8f }, // left ear
    { 0.45f, 0.19f, 0.7f }, // right ear
    { 0.60f, 0.30f, 0.9f }, // left shoulder
    { 0.40f, 0.30f, 0.9f }, // right shoulder
    { 0.66f, 0.42f, 0.9f }, // left elbow
    { 0.34f, 0.42f, 0.5f }, // right elbow (inferred)
    { 0.70f, 0.52f, 0.9f }, // left wrist
    { 0.30f, 0.52f, 0.9f }, // right wrist
    { 0.56f, 0.58f, 0.9f }, // left hip
    { 0.44f, 0.58f, 0.9f }, // right hip
    { 0.57f, 0.75f, 0.9f }, // left knee
    { 0.43f, 0.75f, 0.9f }, // right knee
    { 0.58f, 0.92f, 0.2f }, // left ankle (not tracked)
    { 0.42f, 0.92f, 0.9f }, // right ankle
};

WirePerson CocoPerson(int trackId, bool v2) {
    WirePerson p;
    p.trackId = trackId;
    for (int k = 0; k < 17; k++) {
        p.values.push_back(kCoco[k][0]);
        p.values.push_back(kCoco[k][1]);
        if (v2)
            p.values.push_back(0.0f); // z, unused by layout 0
        p.values.push_back(kCoco[k][2]);
    }
    return p;
}

// ---------------------------------------------------------------------------
// A pose server played by the test: listens, accepts the provider, sends.
// ---------------------------------------------------------------------------
class FakePoseServer {
public:
    FakePoseServer() {
        const char *tmp = getenv("TMPDIR");
        std::string dir = (tmp && *tmp) ? tmp : "/tmp";
        mkdir(dir.c_str(), 0755);
        char name[64];
        snprintf(name, sizeof(name), "/dc3_pose_test_%d_%d.sock", (int)getpid(), sCounter++);
        mPath = dir + name;
        unlink(mPath.c_str());
        mListen = socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, mPath.c_str(), sizeof(addr.sun_path) - 1);
        mOk = mListen >= 0 && bind(mListen, (sockaddr *)&addr, sizeof(addr)) == 0
            && listen(mListen, 1) == 0;
    }
    ~FakePoseServer() {
        CloseClient();
        if (mListen >= 0)
            close(mListen);
        unlink(mPath.c_str());
    }
    bool ok() const { return mOk; }
    const std::string &path() const { return mPath; }
    bool Accept(int timeoutMs = 3000) {
        pollfd pfd = { mListen, POLLIN, 0 };
        if (poll(&pfd, 1, timeoutMs) <= 0)
            return false;
        mClient = accept(mListen, nullptr, nullptr);
        return mClient >= 0;
    }
    bool Send(const std::vector<uint8_t> &msg) {
        size_t off = 0;
        while (off < msg.size()) {
            ssize_t n = write(mClient, msg.data() + off, msg.size() - off);
            if (n <= 0)
                return false;
            off += (size_t)n;
        }
        return true;
    }
    void CloseClient() {
        if (mClient >= 0)
            close(mClient);
        mClient = -1;
    }

private:
    static int sCounter;
    std::string mPath;
    int mListen = -1;
    int mClient = -1;
    bool mOk = false;
};
int FakePoseServer::sCounter = 0;

// Scoped environment variable.
class ScopedEnv {
public:
    ScopedEnv(const char *name, const char *value) : mName(name) {
        const char *old = getenv(name);
        mHad = old != nullptr;
        if (mHad)
            mOld = old;
        if (value)
            setenv(name, value, 1);
        else
            unsetenv(name);
    }
    ~ScopedEnv() {
        if (mHad)
            setenv(mName.c_str(), mOld.c_str(), 1);
        else
            unsetenv(mName.c_str());
    }

private:
    std::string mName, mOld;
    bool mHad;
};

// Start `provider` against `server` (connect-only, as DC3_POSE_NO_SPAWN does).
bool Connect(NativeSkeletonProvider &provider, FakePoseServer &server) {
    ScopedEnv noSpawn("DC3_POSE_NO_SPAWN", "1");
    if (!provider.Start(server.path()))
        return false;
    return server.Accept();
}

// Latch until the reader has published `frameId` (bounded).
bool WaitForFrame(NativeSkeletonProvider &provider, uint32_t frameId, int timeoutMs = 3000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        provider.Poll();
        if (provider.FrameId() == frameId)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

void ExpectVec(const Vector3 &got, float x, float y, float z, const char *what) {
    EXPECT_NEAR(got.x, x, 1e-5f) << what;
    EXPECT_NEAR(got.y, y, 1e-5f) << what;
    EXPECT_NEAR(got.z, z, 1e-5f) << what;
}

// ===========================================================================
// PoseSocketReaderTest
// ===========================================================================
TEST(PoseSocketReaderTest, Dc3LayoutArrivesInMetresWithConfidenceBands) {
    FakePoseServer server;
    ASSERT_TRUE(server.ok());
    NativeSkeletonProvider provider;
    ASSERT_TRUE(Connect(provider, server));

    WirePerson a = Dc3Person(11, 0.0f, 0.9f);
    WirePerson b = Dc3Person(12, 0.5f, 0.9f);
    a.values[kJointHandRight * 4 + 3] = 0.45f; // inferred band [0.3, 0.6)
    a.values[kJointFootLeft * 4 + 3] = 0.29f;  // not tracked (< 0.3)
    a.values[kJointHead * 4 + 3] = 0.6f;       // tracked (>= 0.6)
    ASSERT_TRUE(server.Send(PacketV2(41, 12.25, 1280, 720, kNumJoints, 1, { a, b })));
    ASSERT_TRUE(WaitForFrame(provider, 41));

    ASSERT_EQ(provider.NumPersons(), 2);
    EXPECT_DOUBLE_EQ(provider.Timestamp(), 12.25);
    const NativeSkeletonProvider::PersonData &pa = provider.GetPerson(0);
    const NativeSkeletonProvider::PersonData &pb = provider.GetPerson(1);
    EXPECT_TRUE(pa.valid);
    EXPECT_TRUE(pb.valid);
    EXPECT_EQ(pa.trackId, 11);
    EXPECT_EQ(pb.trackId, 12);
    // Metres pass through untouched (no NormalizedToMeters, no 4:3 box).
    ExpectVec(pa.joints[kJointElbowLeft], -0.30f, 1.60f, 1.9f, "a elbowL");
    ExpectVec(pb.joints[kJointElbowLeft], 0.20f, 1.60f, 1.9f, "b elbowL");
    EXPECT_EQ(pa.confidence[kJointHandRight], kConfidenceInferred);
    EXPECT_EQ(pa.confidence[kJointFootLeft], kConfidenceNotTracked);
    EXPECT_EQ(pa.confidence[kJointHead], kConfidenceTracked);
    EXPECT_EQ(pa.confidence[kJointSpine], kConfidenceTracked);
    EXPECT_EQ(provider.FindByTrackId(12), 1);
    provider.Stop();
}

TEST(PoseSocketReaderTest, TruncatedAndUnknownPacketsDoNotDesyncTheStream) {
    FakePoseServer server;
    ASSERT_TRUE(server.ok());
    NativeSkeletonProvider provider;
    ASSERT_TRUE(Connect(provider, server));

    // Declares two people, carries one: the second must be dropped, not read
    // from past the end of the packet.
    ASSERT_TRUE(server.Send(
        PacketV2(1, 1.0, 640, 480, kNumJoints, 1, { Dc3Person(3, 0.0f) }, /*declared*/ 2)
    ));
    ASSERT_TRUE(WaitForFrame(provider, 1));
    EXPECT_TRUE(provider.GetPerson(0).valid);
    EXPECT_FALSE(provider.GetPerson(1).valid);

    // An unknown landmark count: the person is skipped, the cursor stays in
    // step, and the NEXT packet parses normally.
    WirePerson odd;
    odd.trackId = 9;
    odd.values.assign(5 * 4, 0.25f);
    ASSERT_TRUE(server.Send(PacketV2(2, 1.1, 640, 480, 5, 7, { odd })));
    ASSERT_TRUE(WaitForFrame(provider, 2));
    EXPECT_FALSE(provider.GetPerson(0).valid);

    ASSERT_TRUE(server.Send(PacketV2(3, 1.2, 640, 480, kNumJoints, 1, { Dc3Person(4, 0.1f) })));
    ASSERT_TRUE(WaitForFrame(provider, 3));
    EXPECT_TRUE(provider.GetPerson(0).valid);
    EXPECT_EQ(provider.GetPerson(0).trackId, 4);
    ExpectVec(provider.GetPerson(0).joints[kJointHipCenter], 0.10f, 0.90f, 2.0f, "hip");
    provider.Stop();
}

TEST(PoseSocketReaderTest, ADisconnectStopsTheProviderAndTellsThePlayer) {
    FakePoseServer server;
    ASSERT_TRUE(server.ok());
    NativeSkeletonProvider provider;
    ASSERT_TRUE(Connect(provider, server));
    NativeNotice::Clear("pose");
    ASSERT_TRUE(server.Send(PacketV2(5, 2.0, 640, 480, kNumJoints, 1, { Dc3Person(1, 0.0f) })));
    ASSERT_TRUE(WaitForFrame(provider, 5));
    EXPECT_TRUE(provider.IsRunning());
    EXPECT_TRUE(provider.LastProblem().empty());

    server.CloseClient();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (provider.IsRunning() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_FALSE(provider.IsRunning());
    EXPECT_FALSE(provider.LastProblem().empty());
    EXPECT_NE(NativeNotice::Text("pose").find("pose tracking is off"), std::string::npos)
        << "notice: " << NativeNotice::Text("pose");
    provider.Stop();
    NativeNotice::Clear("pose");
}

// pose_server.py FLAG_NO_CAMERA (bit 1): the camera vanished mid-session (the
// GPD's USB camera re-enumerates) and the server is reopening it.  The game
// keeps the connection, shows one notice while the camera is gone and another
// when it is back -- once per edge, not once per 0.1 s heartbeat.
TEST(PoseSocketReaderTest, ACameraThatGoesAndComesBackPostsOneNoticeEach) {
    const uint16_t kUnlevelled = 1u << 0, kNoCamera = 1u << 1;
    FakePoseServer server;
    ASSERT_TRUE(server.ok());
    NativeSkeletonProvider provider;
    ASSERT_TRUE(Connect(provider, server));
    NativeNotice::Clear("pose");
    ASSERT_TRUE(server.Send(PacketV2(1, 1.0, 640, 480, kNumJoints, 1, { Dc3Person(1, 0.0f) },
                                     -1, kUnlevelled)));
    ASSERT_TRUE(WaitForFrame(provider, 1));
    EXPECT_FALSE(provider.CameraMissing());
    EXPECT_EQ(NativeNotice::Text("pose"), "");

    // The camera goes: zero-person heartbeats carrying the flag.
    ASSERT_TRUE(server.Send(PacketV2(2, 1.1, 640, 480, kNumJoints, 1, {}, -1,
                                     kUnlevelled | kNoCamera)));
    ASSERT_TRUE(WaitForFrame(provider, 2));
    EXPECT_TRUE(provider.CameraMissing());
    EXPECT_TRUE(provider.IsRunning()) << "a missing camera must not drop the server";
    EXPECT_EQ(provider.NumPersons(), 0);
    EXPECT_EQ(NativeNotice::Text("pose"), NativeSkeletonProvider::CameraLostProblem().text);
    NativeNotice::View v;
    ASSERT_TRUE(NativeNotice::Current(v));
    EXPECT_TRUE(v.banner);
    EXPECT_TRUE(provider.LastProblem().empty()) << "paused, not failed";

    // More heartbeats do not post it again.
    NativeNotice::Clear("pose");
    for (uint32_t id = 3; id <= 6; id++)
        ASSERT_TRUE(server.Send(PacketV2(id, 1.0 + id * 0.1, 640, 480, kNumJoints, 1, {}, -1,
                                         kUnlevelled | kNoCamera)));
    ASSERT_TRUE(WaitForFrame(provider, 6));
    EXPECT_EQ(NativeNotice::Text("pose"), "") << "re-posted on every heartbeat";

    // The camera is back.
    ASSERT_TRUE(server.Send(PacketV2(7, 2.0, 640, 480, kNumJoints, 1, { Dc3Person(1, 0.0f) },
                                     -1, kUnlevelled)));
    ASSERT_TRUE(WaitForFrame(provider, 7));
    EXPECT_FALSE(provider.CameraMissing());
    EXPECT_EQ(provider.NumPersons(), 1);
    EXPECT_EQ(NativeNotice::Text("pose"), NativeSkeletonProvider::CameraBackNotice().text);
    NativeNotice::Clear("pose");
    ASSERT_TRUE(server.Send(PacketV2(8, 2.1, 640, 480, kNumJoints, 1, { Dc3Person(1, 0.0f) },
                                     -1, kUnlevelled)));
    ASSERT_TRUE(WaitForFrame(provider, 8));
    EXPECT_EQ(NativeNotice::Text("pose"), "");
    provider.Stop();
}

// Header flags bit 2 + bits 8..15 (pose_server.py header_flags): the source
// declares the scoring latency, in 5 ms units.  pose_server.py --video
// declares 0 (PLAN L-4).  The declaration lands on the main thread's Poll, and
// a frame that declares nothing (a webcam) takes it back.
TEST(PoseSocketReaderTest, ASourcesDeclaredLatencyReachesTheScorerAndAWebcamsDoesNot) {
    FakePoseServer server;
    ASSERT_TRUE(server.ok());
    NativeSkeletonProvider provider;
    ASSERT_TRUE(Connect(provider, server));
    Dc3DeclareSourceLatencyMs(-1);
    const uint16_t kUnlevelled = 1u << 0, kNoCamera = 1u << 1, kDeclared = 1u << 2;
    auto send = [&](uint32_t id, uint16_t flags) {
        return server.Send(
            PacketV2(id, id / 30.0, 640, 480, kNumJoints, 1, { Dc3Person(1, 0.0f) }, -1, flags)
        );
    };
    ASSERT_TRUE(send(61, kUnlevelled | kDeclared)); // a recorded file: 0 ms
    ASSERT_TRUE(WaitForFrame(provider, 61));
    EXPECT_EQ(Dc3DeclaredSourceLatencyMs(), 0);
    ASSERT_TRUE(send(62, kUnlevelled | kDeclared | (30u << 8))); // 30 x 5 = 150 ms
    ASSERT_TRUE(WaitForFrame(provider, 62));
    EXPECT_EQ(Dc3DeclaredSourceLatencyMs(), 150);
    // No bit 2: the high byte means nothing, and bit 1 (FLAG_NO_CAMERA) is
    // not a declaration.
    ASSERT_TRUE(send(63, kUnlevelled | kNoCamera | (30u << 8)));
    ASSERT_TRUE(WaitForFrame(provider, 63));
    EXPECT_EQ(Dc3DeclaredSourceLatencyMs(), -1);
    provider.Stop();
    Dc3DeclareSourceLatencyMs(-1);
    NativeNotice::Clear("pose"); // frame 63's FLAG_NO_CAMERA posted the lost notice
}

// ===========================================================================
// PoseCocoMappingTest -- the layout-0 path (any external COCO-17 source)
// ===========================================================================

// Independent reference for NormalizedToMeters: the Kinect view width at the
// fixed 3.0 m depth (2 * 3.0 * 160 / 285.63, JointUtl.s), x flipped so the
// subject's left is -X, y flipped, height from the frame's own aspect.
Vector3 RefMeters(float nx, float ny, float aspect) {
    const float viewW = 2.0f * 3.0f * 160.0f / 285.63f;
    return Vector3((0.5f - nx) * viewW, (0.5f - ny) * viewW * aspect, 3.0f);
}

void CheckCoco(const NativeSkeletonProvider::PersonData &p, float aspect) {
    auto ref = [&](int k) { return RefMeters(kCoco[k][0], kCoco[k][1], aspect); };
    auto mid = [](const Vector3 &a, const Vector3 &b) {
        return Vector3((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f);
    };
    Vector3 v = ref(5);
    ExpectVec(p.joints[kJointShoulderLeft], v.x, v.y, v.z, "shoulderL = COCO left shoulder");
    EXPECT_LT(p.joints[kJointShoulderLeft].x, p.joints[kJointShoulderRight].x)
        << "the subject's left must land at -X (not mirrored)";
    // Head: ear midpoint while both ears are confident.
    v = mid(ref(3), ref(4));
    ExpectVec(p.joints[kJointHead], v.x, v.y, v.z, "head = ear midpoint");
    // Hand: past the wrist along the forearm by 0.42 of it.
    Vector3 e = ref(7), w = ref(9);
    ExpectVec(p.joints[kJointHandLeft], w.x + (w.x - e.x) * 0.42f, w.y + (w.y - e.y) * 0.42f,
              3.0f, "handL extrapolated");
    // Spine: midway between the hip and shoulder midpoints.
    Vector3 hc = mid(ref(11), ref(12)), sc = mid(ref(5), ref(6));
    v = mid(hc, sc);
    ExpectVec(p.joints[kJointSpine], v.x, v.y, v.z, "spine");
    ExpectVec(p.joints[kJointHipCenter], hc.x, hc.y, hc.z, "hip centre");
    // Confidence: per keypoint, and the weaker parent for synthesised joints.
    EXPECT_EQ(p.confidence[kJointElbowRight], kConfidenceInferred);
    EXPECT_EQ(p.confidence[kJointHandRight], kConfidenceInferred); // min(wrist, elbow)
    EXPECT_EQ(p.confidence[kJointAnkleLeft], kConfidenceNotTracked);
    EXPECT_EQ(p.confidence[kJointFootLeft], kConfidenceNotTracked);
    EXPECT_EQ(p.confidence[kJointHead], kConfidenceTracked);
}

TEST(PoseCocoMappingTest, V2CocoUsesTheFramesOwnAspect) {
    FakePoseServer server;
    ASSERT_TRUE(server.ok());
    NativeSkeletonProvider provider;
    ASSERT_TRUE(Connect(provider, server));
    // 16:9: the vertical extent must follow 720/1280, not the Kinect 4:3.
    ASSERT_TRUE(server.Send(PacketV2(7, 3.0, 1280, 720, 17, 0, { CocoPerson(2, true) })));
    ASSERT_TRUE(WaitForFrame(provider, 7));
    ASSERT_TRUE(provider.GetPerson(0).valid);
    CheckCoco(provider.GetPerson(0), 720.0f / 1280.0f);
    provider.Stop();
}

TEST(PoseCocoMappingTest, V1PacketsKeepTheKinectAspect) {
    FakePoseServer server;
    ASSERT_TRUE(server.ok());
    NativeSkeletonProvider provider;
    ASSERT_TRUE(Connect(provider, server));
    ASSERT_TRUE(server.Send(PacketV1(9, 4.0, { CocoPerson(6, false) })));
    ASSERT_TRUE(WaitForFrame(provider, 9));
    ASSERT_TRUE(provider.GetPerson(0).valid);
    EXPECT_EQ(provider.GetPerson(0).trackId, 6);
    CheckCoco(provider.GetPerson(0), 0.75f);
    provider.Stop();
}

TEST(PoseCocoMappingTest, HeadFallsBackToTheNoseWithoutBothEars) {
    FakePoseServer server;
    ASSERT_TRUE(server.ok());
    NativeSkeletonProvider provider;
    ASSERT_TRUE(Connect(provider, server));
    WirePerson p = CocoPerson(2, true);
    p.values[4 * 4 + 3] = 0.1f; // right ear not tracked (profile view)
    ASSERT_TRUE(server.Send(PacketV2(8, 3.0, 640, 480, 17, 0, { p })));
    ASSERT_TRUE(WaitForFrame(provider, 8));
    Vector3 nose = RefMeters(kCoco[0][0], kCoco[0][1], 0.75f);
    ExpectVec(provider.GetPerson(0).joints[kJointHead], nose.x, nose.y, nose.z, "head = nose");
    provider.Stop();
}

// ===========================================================================
// PoseSensorSlotTest -- AssignSlots, as the image sees it
// ===========================================================================
class PoseSensorSlotTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!SkeletonUpdate::HasInstance()) {
            EnsureSymbolInit(); // SkeletonUpdate::Init registers DTA funcs
            GestureMgr_NativePreInit();
        }
        mSensor = SkeletonUpdate::InstanceHandle().GetCameraInput();
        ASSERT_NE(mSensor, nullptr);
        // No provider: the sensor resets its slot map and feeds the dummy.
        TheSkeletonProvider = nullptr;
        mSensor->PollNewFrame();
        ASSERT_TRUE(mServer.ok());
        ASSERT_TRUE(Connect(mProvider, mServer));
        TheSkeletonProvider = &mProvider;
    }
    void TearDown() override {
        TheSkeletonProvider = nullptr;
        mProvider.Stop();
        if (mSensor)
            mSensor->PollNewFrame(); // back to the dummy, slot map reset
    }

    // Send one frame of people (track id, x offset) and return the sensor's
    // SkeletonFrame for it.
    const SkeletonFrame *Frame(const std::vector<std::pair<int, float> > &people) {
        std::vector<WirePerson> wire;
        for (auto &p : people)
            wire.push_back(Dc3Person(p.first, p.second));
        // Ids keep rising across tests: the sensor remembers the last one it
        // integrated, and a repeat is (correctly) not a new frame.
        uint32_t id = ++sFrameId;
        if (!mServer.Send(PacketV2(id, id / 30.0, 640, 480, kNumJoints, 1, wire)))
            return nullptr;
        if (!WaitForFrame(mProvider, id))
            return nullptr;
        return mSensor->PollNewFrame();
    }

    // slot -> track id for every tracked slot (-1 = untracked).
    static std::vector<int> Slots(const SkeletonFrame *f) {
        std::vector<int> out(NUM_SKELETONS, -1);
        for (int s = 0; s < NUM_SKELETONS; s++) {
            if (f->mSkeletonDatas[s].mTracking == kSkeletonTracked)
                out[s] = f->mSkeletonDatas[s].mTrackingID;
        }
        return out;
    }
    static int SlotOf(const SkeletonFrame *f, int trackId) {
        std::vector<int> s = Slots(f);
        for (int i = 0; i < NUM_SKELETONS; i++) {
            if (s[i] == trackId)
                return i;
        }
        return -1;
    }

    CameraInput *mSensor = nullptr;
    FakePoseServer mServer;
    NativeSkeletonProvider mProvider;
    static uint32_t sFrameId;
};
uint32_t PoseSensorSlotTest::sFrameId = 1000;

TEST_F(PoseSensorSlotTest, PeopleKeepTheirSlotsWhateverOrderTheyArriveIn) {
    const SkeletonFrame *f = Frame({ { 21, -0.4f }, { 22, 0.4f } });
    ASSERT_NE(f, nullptr);
    int slotA = SlotOf(f, 21), slotB = SlotOf(f, 22);
    ASSERT_GE(slotA, 0);
    ASSERT_GE(slotB, 0);
    EXPECT_NE(slotA, slotB);
    // The slot carries that person's skeleton, not just the id.
    EXPECT_NEAR(f->mSkeletonDatas[slotA].mJointPositions[kJointHipCenter].x, -0.4f, 1e-4f);
    EXPECT_NEAR(f->mSkeletonDatas[slotB].mJointPositions[kJointHipCenter].x, 0.4f, 1e-4f);

    // The server lists them the other way round: slots must not follow.
    f = Frame({ { 22, 0.4f }, { 21, -0.4f } });
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(SlotOf(f, 21), slotA);
    EXPECT_EQ(SlotOf(f, 22), slotB);
    EXPECT_NEAR(f->mSkeletonDatas[slotA].mJointPositions[kJointHipCenter].x, -0.4f, 1e-4f);
}

TEST_F(PoseSensorSlotTest, ADepartedSlotIsNotHandedToANewcomerInTheSameFrame) {
    const SkeletonFrame *f = Frame({ { 31, 0.0f } });
    ASSERT_NE(f, nullptr);
    int slotA = SlotOf(f, 31);
    ASSERT_GE(slotA, 0);

    // 31 leaves as 32 arrives: 32 must not inherit 31's slot this frame, or
    // SkeletonUpdate archives 31's last pose as 32's history.
    f = Frame({ { 32, 0.3f } });
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(SlotOf(f, 31), -1);
    int slotB = SlotOf(f, 32);
    ASSERT_GE(slotB, 0);
    EXPECT_NE(slotB, slotA);
    EXPECT_NE(f->mSkeletonDatas[slotA].mTracking, kSkeletonTracked)
        << "the departed slot reads untracked for a frame";
}

TEST_F(PoseSensorSlotTest, AnUntrackedDetectionNeverClaimsASlot) {
    const SkeletonFrame *f = Frame({ { -1, 0.0f }, { 41, 0.5f } });
    ASSERT_NE(f, nullptr);
    // Count tracked slots by their tracking state, not their id: a slot taken
    // by the -1 detection would carry tracking id -1.
    int tracked = 0;
    for (int s = 0; s < NUM_SKELETONS; s++) {
        if (f->mSkeletonDatas[s].mTracking == kSkeletonTracked) {
            tracked++;
            EXPECT_EQ(f->mSkeletonDatas[s].mTrackingID, 41) << "slot " << s;
        }
    }
    EXPECT_EQ(tracked, 1);
}

// ---------------------------------------------------------------------------
// The dropout hold (PLAN P-12): a person MediaPipe loses while she is still in
// view keeps her slot and tracking id, every joint inferred at its last good
// position, for DC3_POSE_HOLD_MS (default 1000) of sensor time.  Frames here
// are 1/30 s apart, which the sensor integrates as 33 ms.
// ---------------------------------------------------------------------------
class PoseSensorHoldTest : public PoseSensorSlotTest {
protected:
    void SetUp() override {
        unsetenv("DC3_POSE_HOLD_MS"); // the default under test
        PoseSensorSlotTest::SetUp();
    }
    void TearDown() override {
        unsetenv("DC3_POSE_HOLD_MS");
        PoseSensorSlotTest::TearDown();
    }
    // `frames` frames of one person; returns the slot she holds.
    int Present(int trackId, float x, int frames) {
        const SkeletonFrame *f = nullptr;
        for (int i = 0; i < frames; i++)
            f = Frame({ { trackId, x } });
        return f ? SlotOf(f, trackId) : -1;
    }
};

TEST_F(PoseSensorHoldTest, ADropoutKeepsTheSlotTrackedAndInferredThroughTheHold) {
    int slot = Present(61, 0.1f, 31); // ~1 s in view
    ASSERT_GE(slot, 0);
    // 29 empty frames = 957 ms, inside the 1000 ms hold.
    for (int i = 0; i < 29; i++) {
        const SkeletonFrame *f = Frame({});
        ASSERT_NE(f, nullptr);
        ASSERT_EQ(SlotOf(f, 61), slot) << "released after " << i + 1 << " empty frames";
        const SkeletonData &d = f->mSkeletonDatas[slot];
        EXPECT_NEAR(d.mJointPositions[kJointHipCenter].x, 0.1f, 1e-4f) << "held at the last pose";
        EXPECT_NEAR(d.mJointPositions[kJointHandLeft].y, 1.92f, 1e-4f);
        for (int j = 0; j < kNumJoints; j++)
            ASSERT_EQ(d.mJointTrackingState[j], kConfidenceInferred) << "joint " << j;
    }
    // Back: same slot, tracked joints again.
    const SkeletonFrame *f = Frame({ { 61, 0.15f } });
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(SlotOf(f, 61), slot);
    EXPECT_EQ(f->mSkeletonDatas[slot].mJointTrackingState[kJointHipCenter], kConfidenceTracked);
    EXPECT_NEAR(f->mSkeletonDatas[slot].mJointPositions[kJointHipCenter].x, 0.15f, 1e-4f);
}

TEST_F(PoseSensorHoldTest, TheHoldIsBoundedSoSomeoneWhoLeftIsLost) {
    int slot = Present(62, 0.0f, 31);
    ASSERT_GE(slot, 0);
    // 30 x 33 ms = 990 ms: still held.  The 31st (1023 ms) releases the slot.
    for (int i = 0; i < 30; i++) {
        const SkeletonFrame *f = Frame({});
        ASSERT_NE(f, nullptr);
        ASSERT_EQ(SlotOf(f, 62), slot) << "released after " << i + 1 << " empty frames";
    }
    const SkeletonFrame *f = Frame({});
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(SlotOf(f, 62), -1) << "held past DC3_POSE_HOLD_MS";
    EXPECT_NE(f->mSkeletonDatas[slot].mTracking, kSkeletonTracked);
}

TEST_F(PoseSensorHoldTest, ZeroTurnsTheHoldOff) {
    setenv("DC3_POSE_HOLD_MS", "0", 1);
    int slot = Present(63, 0.0f, 31);
    ASSERT_GE(slot, 0);
    const SkeletonFrame *f = Frame({});
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(SlotOf(f, 63), -1);
}

TEST_F(PoseSensorHoldTest, ABriefFalseDetectionIsNotHeld) {
    // 3 frames (100 ms) is under the 500 ms a person must have been seen.
    ASSERT_GE(Present(64, 0.0f, 3), 0);
    const SkeletonFrame *f = Frame({});
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(SlotOf(f, 64), -1);
}

TEST_F(PoseSensorHoldTest, ANewIdWhereTheHeldPersonStoodEndsTheHold) {
    int slot = Present(65, 0.0f, 31);
    ASSERT_GE(slot, 0);
    ASSERT_EQ(SlotOf(Frame({}), 65), slot); // held
    // The tracker calls the person at the same spot someone new: one body,
    // so one skeleton -- the held one goes.
    const SkeletonFrame *f = Frame({ { 66, 0.1f } });
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(SlotOf(f, 65), -1);
    EXPECT_GE(SlotOf(f, 66), 0);
    int tracked = 0;
    for (int s = 0; s < NUM_SKELETONS; s++)
        tracked += f->mSkeletonDatas[s].mTracking == kSkeletonTracked;
    EXPECT_EQ(tracked, 1);
}

TEST_F(PoseSensorHoldTest, ANewcomerElsewhereDoesNotEndTheHold) {
    int slot = Present(67, -0.5f, 31);
    ASSERT_GE(slot, 0);
    const SkeletonFrame *f = Frame({ { 68, 0.6f } }); // 1.1 m away
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(SlotOf(f, 67), slot);
    int other = SlotOf(f, 68);
    EXPECT_GE(other, 0);
    EXPECT_NE(other, slot);
}

TEST_F(PoseSensorSlotTest, ARepeatedFrameIdIsNotANewSensorFrame) {
    ASSERT_NE(Frame({ { 51, 0.0f } }), nullptr);
    // Same generation again: the camera has not produced a new picture.
    EXPECT_EQ(mSensor->PollNewFrame(), nullptr);
}

// ===========================================================================
// PoseServerStartTest -- no camera: fail fast, tell the player
// ===========================================================================
// ===========================================================================
// PoseSensorLevelTest -- SkeletonFrame::Create's gravity step on the sensor
// frame (PoseLevel_Native.h), read where the image reads it
// ===========================================================================
class PoseSensorLevelTest : public PoseSensorSlotTest {
protected:
    void SetUp() override {
        PoseSensorSlotTest::SetUp();
        // config/default.dta kinect/up_vector_smoothing (SkeletonFrame::Init
        // reads it in a booted game).
        SkeletonFrame::sUpVectorSmoother.SetSmoothParameters(10.0f, 0.0f);
        SkeletonFrame::sUpVectorSmoother.ForceValue(Vector3(0, 1, 0));
        NativeSensorLevelSetPolicy(NativePoseLevelPolicy()); // auto, fresh estimate
    }
    void TearDown() override {
        NativeSensorLevelSetPolicy(NativePoseLevelPolicy());
        SkeletonFrame::sUpVectorSmoother.ForceValue(Vector3(0, 1, 0));
        PoseSensorSlotTest::TearDown();
    }

    // Dc3Person seen by a camera pitched `deg` down: the scene rotated about
    // the camera's x axis, +y onto the camera's up (0, cos, -sin).
    static WirePerson Tilted(int trackId, float deg) {
        WirePerson p = Dc3Person(trackId, 0.0f, 0.9f);
        float c = std::cos(deg * 3.14159265f / 180.0f), s = std::sin(deg * 3.14159265f / 180.0f);
        for (int j = 0; j < kNumJoints; j++) {
            float y = p.values[j * 4 + 1], z = p.values[j * 4 + 2];
            p.values[j * 4 + 1] = y * c + z * s;
            p.values[j * 4 + 2] = -y * s + z * c;
        }
        return p;
    }

    const SkeletonFrame *Send(const WirePerson &person, uint16_t flags) {
        uint32_t id = ++sFrameId;
        if (!mServer.Send(PacketV2(id, id / 30.0, 640, 480, kNumJoints, 1, { person }, -1, flags)))
            return nullptr;
        if (!WaitForFrame(mProvider, id))
            return nullptr;
        return mSensor->PollNewFrame();
    }

    static float Pitch(const PaddedJointPos *j) {
        float y = j[kJointHipCenter].y - 0.5f * (j[kJointAnkleLeft].y + j[kJointAnkleRight].y);
        float z = j[kJointHipCenter].z - 0.5f * (j[kJointAnkleLeft].z + j[kJointAnkleRight].z);
        return std::atan2(-z, y) * 180.0f / 3.14159265f;
    }
};

TEST_F(PoseSensorLevelTest, AFlaggedCameraIsLevelledByTheGame) {
    const float tilt = 8.0f;
    const WirePerson person = Tilted(51, tilt);
    const SkeletonFrame *f = nullptr;
    for (int k = 0; k < 240; k++) { // 8 s at 30 Hz
        f = Send(person, 1);
        ASSERT_NE(f, nullptr) << k;
    }
    const NativeGravityEstimator &e = NativeSensorGravityEstimator();
    ASSERT_TRUE(e.HasEstimate());
    // Dc3Person stands dead straight (lean 0), so the estimate is the camera's
    // tilt less the reference's own lean.
    EXPECT_NEAR(e.PitchDeg(), tilt - NativeGravityEstimator::kPriorPitchDeg, 0.1f);
    int slot = SlotOf(f, 51);
    ASSERT_GE(slot, 0);
    const SkeletonData &d = f->mSkeletonDatas[slot];
    // What the scorer reads (mJointPositions, camera space) stands as the
    // Kinect reference does; mRawPositions is what the camera saw.
    EXPECT_NEAR(Pitch(d.mJointPositions), NativeGravityEstimator::kPriorPitchDeg, 0.1f);
    EXPECT_NEAR(Pitch(d.mRawPositions), tilt, 0.1f);
    for (int j = 0; j < kNumJoints; j++) {
        EXPECT_NEAR(d.mRawPositions[j].y, person.values[j * 4 + 1], 1e-5f) << j;
        EXPECT_NEAR(d.mRawPositions[j].z, person.values[j * 4 + 2], 1e-5f) << j;
    }
    Vector3 n = e.NormalToGravity();
    EXPECT_NEAR(f->mFloorNormal.y, n.y, 1e-4f);
    EXPECT_NEAR(f->mFloorNormal.z, n.z, 1e-4f);
    EXPECT_NEAR(d.mHipCenter.z, d.mJointPositions[kJointHipCenter].z, 1e-6f);
}

TEST_F(PoseSensorLevelTest, AnUnflaggedPacketIsUntouched) {
    // synthetic_kinect.py's packets (flags 0) play a Kinect: already level.
    const WirePerson person = Tilted(52, 8.0f);
    const SkeletonFrame *f = nullptr;
    for (int k = 0; k < 240; k++) {
        f = Send(person, 0);
        ASSERT_NE(f, nullptr) << k;
    }
    EXPECT_FALSE(NativeSensorGravityEstimator().HasEstimate());
    EXPECT_EQ(NativeSensorGravityEstimator().NumSamples(), 0);
    int slot = SlotOf(f, 52);
    ASSERT_GE(slot, 0);
    for (int j = 0; j < kNumJoints; j++) {
        EXPECT_NEAR(f->mSkeletonDatas[slot].mJointPositions[j].y, person.values[j * 4 + 1], 1e-5f) << j;
        EXPECT_NEAR(f->mSkeletonDatas[slot].mJointPositions[j].z, person.values[j * 4 + 2], 1e-5f) << j;
    }
    EXPECT_EQ(f->mFloorNormal.y, 1.0f);
}

TEST_F(PoseSensorLevelTest, LevelOffLeavesAFlaggedCameraAsItWas) {
    NativePoseLevelPolicy off;
    off.mode = NativePoseLevelPolicy::kOff;
    NativeSensorLevelSetPolicy(off);
    const WirePerson person = Tilted(53, 8.0f);
    const SkeletonFrame *f = nullptr;
    for (int k = 0; k < 240; k++) {
        f = Send(person, 1);
        ASSERT_NE(f, nullptr) << k;
    }
    EXPECT_FALSE(NativeSensorGravityEstimator().HasEstimate());
    int slot = SlotOf(f, 53);
    ASSERT_GE(slot, 0);
    EXPECT_NEAR(Pitch(f->mSkeletonDatas[slot].mJointPositions), 8.0f, 0.1f);
}

TEST_F(PoseSensorLevelTest, AFixedPitchLevelsByExactlyThat) {
    NativePoseLevelPolicy fixed;
    fixed.mode = NativePoseLevelPolicy::kFixed;
    fixed.fixedPitchDeg = 8.0f;
    NativeSensorLevelSetPolicy(fixed);
    const WirePerson person = Tilted(54, 8.0f);
    const SkeletonFrame *f = nullptr;
    for (int k = 0; k < 60; k++) { // the smoother converges in ~1 s
        f = Send(person, 1);
        ASSERT_NE(f, nullptr) << k;
    }
    int slot = SlotOf(f, 54);
    ASSERT_GE(slot, 0);
    EXPECT_NEAR(Pitch(f->mSkeletonDatas[slot].mJointPositions), 0.0f, 0.05f);
    EXPECT_FALSE(NativeSensorGravityEstimator().HasEstimate());
}

// ===========================================================================
// PoseSessionTest -- record a live sensor session, replay it (PLAN L-1)
// ===========================================================================
class PoseSessionTest : public PoseSensorLevelTest {
protected:
    struct Seen {
        Vector3 joints[2][kNumJoints];
        int track[2];
        Vector3 floor;
    };
    // Both tracked slots of `f`, in tracking-id order (a, b).
    static bool Capture(const SkeletonFrame *f, int a, int b, Seen &out) {
        int ids[2] = { a, b };
        for (int k = 0; k < 2; k++) {
            int slot = SlotOf(f, ids[k]);
            if (slot < 0)
                return false;
            out.track[k] = ids[k];
            for (int j = 0; j < kNumJoints; j++) {
                const PaddedJointPos &p = f->mSkeletonDatas[slot].mJointPositions[j];
                out.joints[k][j].Set(p.x, p.y, p.z);
            }
        }
        out.floor = f->mFloorNormal;
        return true;
    }
    static std::string TempPath(const char *name) {
        const char *tmp = getenv("TMPDIR");
        std::string dir = std::string((tmp && *tmp) ? tmp : "/tmp") + "/dc3_session_XXXXXX";
        std::vector<char> buf(dir.begin(), dir.end());
        buf.push_back('\0');
        EXPECT_NE(mkdtemp(buf.data()), nullptr);
        return std::string(buf.data()) + "/" + name;
    }
};

TEST_F(PoseSessionTest, AReplayedSessionReachesTheScorerAsItDidLive) {
    const std::string path = TempPath("live.dc3session");
    Dc3PoseSessionRecordTo(path.c_str());
    ASSERT_EQ(Dc3PoseSessionRecordPath(), path);

    // Live: two people, a camera pitched 8 degrees, flagged unlevelled, 10 s
    // at 30 Hz -- long enough that the estimator has an estimate and moves.
    std::vector<Seen> live;
    std::vector<uint32_t> ids;
    for (int k = 0; k < 300; k++) {
        WirePerson a = Tilted(61, 8.0f), b = Tilted(62, 8.0f);
        for (int j = 0; j < kNumJoints; j++) {
            a.values[j * 4 + 0] -= 0.4f;
            b.values[j * 4 + 0] += 0.4f + 0.01f * std::sin(k * 0.2f); // b sways
        }
        uint32_t id = ++sFrameId;
        ASSERT_TRUE(mServer.Send(PacketV2(id, 100.0 + id / 30.0, 640, 480, kNumJoints, 1, { a, b }, -1, 1)));
        ASSERT_TRUE(WaitForFrame(mProvider, id));
        const SkeletonFrame *f = mSensor->PollNewFrame();
        ASSERT_NE(f, nullptr) << k;
        Seen s;
        ASSERT_TRUE(Capture(f, 61, 62, s)) << k;
        live.push_back(s);
        ids.push_back(id);
    }
    Dc3PoseSessionRecordTo(nullptr);
    ASSERT_TRUE(NativeSensorGravityEstimator().HasEstimate());

    // The file holds what the sensor accepted, exactly.
    std::vector<Dc3PoseSession::Frame> frames;
    std::string err;
    ASSERT_TRUE(Dc3PoseSession::Load(path.c_str(), frames, &err)) << err;
    ASSERT_EQ(frames.size(), live.size());
    for (size_t k = 0; k < frames.size(); k++) {
        ASSERT_EQ(frames[k].frameId, ids[k]);
        EXPECT_EQ(frames[k].timestamp, 100.0 + ids[k] / 30.0);
        EXPECT_TRUE(frames[k].unlevelled);
        EXPECT_TRUE(frames[k].level);
        ASSERT_EQ(frames[k].persons.size(), 2u);
        EXPECT_EQ(frames[k].persons[0].trackId, 61);
        EXPECT_EQ(frames[k].persons[1].trackId, 62);
        EXPECT_EQ(frames[k].floorNormal.y, live[k].floor.y) << k;
        EXPECT_EQ(frames[k].floorNormal.z, live[k].floor.z) << k;
    }
    // The estimate MOVED during the session, so a replay that re-estimated
    // from scratch could not match (the point of recording the floor normal).
    EXPECT_GT(std::fabs(live.back().floor.z - live.front().floor.z), 0.05f);

    // Give each frame its own song time and replay it, the estimator reset as
    // in a fresh process that is replaying only this song.
    for (size_t k = 0; k < frames.size(); k++)
        frames[k].song = 10.0 + k / 30.0;
    const std::string replayPath = TempPath("song.dc3session");
    {
        FILE *fp = fopen(replayPath.c_str(), "w");
        ASSERT_NE(fp, nullptr);
        fputs("# dc3-pose-session v1\n", fp);
        for (const Dc3PoseSession::Frame &f : frames)
            fputs(Dc3PoseSession::Format(f).c_str(), fp);
        fclose(fp);
    }
    NativeSensorLevelSetPolicy(NativePoseLevelPolicy());
    SkeletonFrame::sUpVectorSmoother.ForceValue(Vector3(0, 1, 0));
    ASSERT_FALSE(NativeSensorGravityEstimator().HasEstimate());
    ASSERT_TRUE(Dc3PoseSessionReplayLoad(replayPath.c_str()));

    // Before the first frame: the first frame is held (the player stays
    // tracked through the menus), as a new frame each call.
    Dc3PoseSessionReplayPumpAt(&mProvider, 5.0);
    const SkeletonFrame *f = mSensor->PollNewFrame();
    ASSERT_NE(f, nullptr);
    EXPECT_GE(SlotOf(f, 61), 0);
    EXPECT_GE(SlotOf(f, 62), 0);

    for (size_t k = 0; k < frames.size(); k++) {
        Dc3PoseSessionReplayPumpAt(&mProvider, frames[k].song + 0.001);
        f = mSensor->PollNewFrame();
        ASSERT_NE(f, nullptr) << k;
        EXPECT_EQ(mProvider.FrameId(), ids[k]);
        EXPECT_EQ(mProvider.Timestamp(), 100.0 + ids[k] / 30.0);
        Seen s;
        ASSERT_TRUE(Capture(f, 61, 62, s)) << k;
        for (int p = 0; p < 2; p++) {
            for (int j = 0; j < kNumJoints; j++) {
                // Equal up to the sensor's 1 um noise floor (its counter is
                // per process, not per frame).
                ASSERT_NEAR(s.joints[p][j].x, live[k].joints[p][j].x, 1e-5f) << k << " " << p << " " << j;
                ASSERT_NEAR(s.joints[p][j].y, live[k].joints[p][j].y, 1e-5f) << k << " " << p << " " << j;
                ASSERT_NEAR(s.joints[p][j].z, live[k].joints[p][j].z, 1e-5f) << k << " " << p << " " << j;
            }
        }
    }
    // The same song time again is not a new frame; past the end, the last
    // frame is held.
    Dc3PoseSessionReplayPumpAt(&mProvider, frames.back().song + 0.001);
    EXPECT_EQ(mProvider.FrameId(), ids.back());
    Dc3PoseSessionReplayPumpAt(&mProvider, frames.back().song + 5.0);
    f = mSensor->PollNewFrame();
    ASSERT_NE(f, nullptr);
    EXPECT_NE(mProvider.FrameId(), ids.back());
    EXPECT_GE(SlotOf(f, 62), 0);
    Dc3PoseSessionReplayLoad("/nonexistent/none.dc3session"); // clears the replay
}

// A song cut starts in the count-in (song clock below zero).  The menus' clock
// runs up from zero, through the song's own times: the replay must hold the
// first frame there, not play the dance into the menus (where a moving player
// stole the pad's presses and stranded replay_proof.py on choose_mode_screen),
// and start when the song clock comes down to the count-in -- every time.
// Every frame goes out, one per render frame, even when several come due at
// once (a hitch), as the live session accepted them.
TEST_F(PoseSessionTest, ASongReplayWaitsForTheCountInNotTheMenuClock) {
    std::vector<Dc3PoseSession::Frame> frames(300);
    for (size_t k = 0; k < frames.size(); k++) {
        Dc3PoseSession::Frame &f = frames[k];
        f.seq = f.frameId = 7000 + (uint32_t)k;
        f.wall = k / 30.0;
        f.song = -5.0 + k / 30.0; // -5 .. +5
        f.timestamp = 200.0 + k / 30.0;
        WirePerson w = Tilted(71, 0.0f);
        NativeSkeletonProvider::PersonData p;
        p.trackId = 71;
        p.valid = true;
        for (int j = 0; j < kNumJoints; j++) {
            p.joints[j].Set(w.values[j * 4 + 0], w.values[j * 4 + 1], w.values[j * 4 + 2]);
            p.confidence[j] = kConfidenceTracked;
        }
        f.persons.push_back(p);
    }
    const std::string path = TempPath("countin.dc3session");
    {
        FILE *fp = fopen(path.c_str(), "w");
        ASSERT_NE(fp, nullptr);
        fputs("# dc3-pose-session v1\n", fp);
        for (const Dc3PoseSession::Frame &f : frames)
            fputs(Dc3PoseSession::Format(f).c_str(), fp);
        fclose(fp);
    }
    ASSERT_TRUE(Dc3PoseSessionReplayLoad(path.c_str()));
    // Menus: 0 .. 4 s, inside the session's song times, held every call.
    for (double t = 0.0; t < 4.0; t += 1.0 / 30) {
        Dc3PoseSessionReplayPumpAt(&mProvider, t);
        ASSERT_NE(mSensor->PollNewFrame(), nullptr);
        EXPECT_GE(mProvider.FrameId(), 0x80000000u) << "menu clock " << t << " played a frame";
    }
    // The song: count-in, then each frame on time.
    for (size_t k = 0; k < frames.size(); k++) {
        Dc3PoseSessionReplayPumpAt(&mProvider, frames[k].song + 0.001);
        ASSERT_NE(mSensor->PollNewFrame(), nullptr) << k;
        ASSERT_EQ(mProvider.FrameId(), frames[k].frameId) << k;
    }
    // Back to the menus (the clock jumps back to zero): held again, and the
    // next song's count-in starts the replay again.
    Dc3PoseSessionReplayPumpAt(&mProvider, 0.5);
    mSensor->PollNewFrame();
    EXPECT_GE(mProvider.FrameId(), 0x80000000u);
    Dc3PoseSessionReplayPumpAt(&mProvider, 1.0);
    mSensor->PollNewFrame();
    EXPECT_GE(mProvider.FrameId(), 0x80000000u);
    // Re-armed by a clock that is already 0.1 s in: it starts from the first
    // frame and catches up, one frame per call.
    for (int k = 0; k <= 3; k++) {
        Dc3PoseSessionReplayPumpAt(&mProvider, frames[3].song + 0.001);
        mSensor->PollNewFrame();
        EXPECT_EQ(mProvider.FrameId(), frames[k].frameId) << k;
    }
    // A hitch: three frames come due on one render frame.  None is dropped;
    // they go out one per call, in order (the game accepts one per frame).
    for (int k = 4; k <= 6; k++) {
        Dc3PoseSessionReplayPumpAt(&mProvider, frames[6].song + 0.001);
        mSensor->PollNewFrame();
        EXPECT_EQ(mProvider.FrameId(), frames[k].frameId) << k;
    }
    // Two seconds behind is a seek, not a hitch: it jumps.
    Dc3PoseSessionReplayPumpAt(&mProvider, frames[66].song + 0.001);
    mSensor->PollNewFrame();
    EXPECT_EQ(mProvider.FrameId(), frames[66].frameId);
    Dc3PoseSessionReplayLoad("/nonexistent/none.dc3session");
}


TEST(PoseServerExitTest, EachServerExitCodeHasItsOwnMessage) {
    using P = NativeSkeletonProvider;
    auto brief = [](int code) { return P::DescribeServerExit(code << 8, 2).brief; };
    EXPECT_EQ(brief(P::kExitNoCamera), "No camera: pose tracking off");
    EXPECT_EQ(brief(P::kExitCameraLost), "Camera lost: pose tracking off");
    EXPECT_EQ(brief(P::kExitNoModel), "Pose model missing: pose tracking off");
    EXPECT_EQ(brief(P::kExitNoDeps), "Pose packages missing: pose tracking off");
    EXPECT_EQ(brief(P::kExitNoVideo), "Pose video unreadable: pose tracking off");
    EXPECT_EQ(brief(P::kExitExecFailed), "No python3: pose tracking off");
    EXPECT_EQ(brief(1), "Pose server exited: pose tracking off");
    EXPECT_NE(P::DescribeServerExit(P::kExitNoCamera << 8, 2).text.find("camera 2"),
              std::string::npos);
    EXPECT_EQ(P::DescribeServerExit(11 /* SIGSEGV */, 0).brief,
              "Pose server crashed: pose tracking off");
    const std::string byId = "usb-SunplusIT_Inc_Integrated_Camera_JX20250923V2-video-index0";
    EXPECT_NE(P::DescribeServerExit(P::kExitNoCamera << 8, byId).text.find("camera " + byId),
              std::string::npos);
    EXPECT_NE(P::DescribeServerExit(P::kExitNoCamera << 8, "auto").text.find("camera auto"),
              std::string::npos);
}

// DC3_POSE_CAMERA reaches pose_server.py --camera as given, and unset is
// "auto" -- never a fixed index, which named a /dev/video node that no longer
// existed after the GPD's camera re-enumerated.
TEST(PoseServerStartTest, TheCameraSpecReachesTheServerAsGivenAndDefaultsToAuto) {
    const char *tmp = getenv("TMPDIR");
    std::string dir = std::string((tmp && *tmp) ? tmp : "/tmp") + "/dc3_pose_stub_XXXXXX";
    std::vector<char> buf(dir.begin(), dir.end());
    buf.push_back('\0');
    ASSERT_NE(mkdtemp(buf.data()), nullptr);
    std::string stubDir(buf.data());
    std::string stub = stubDir + "/python3";
    std::string argsFile = stubDir + "/args";
    FILE *f = fopen(stub.c_str(), "w");
    ASSERT_NE(f, nullptr);
    fprintf(f, "#!/bin/sh\nprintf '%%s\\n' \"$@\" > '%s'\nexit %d\n", argsFile.c_str(),
            (int)NativeSkeletonProvider::kExitNoCamera);
    fclose(f);
    chmod(stub.c_str(), 0755);
    const char *oldPath = getenv("PATH");
    std::string path = stubDir + ":" + (oldPath ? oldPath : "/usr/bin:/bin");
    ScopedEnv envPath("PATH", path.c_str());
    ScopedEnv envPy("DC3_POSE_PYTHON", stub.c_str()); // else the game prefers a venv
    ScopedEnv noSpawn("DC3_POSE_NO_SPAWN", nullptr);

    auto cameraArg = [&]() {
        std::string out;
        FILE *a = fopen(argsFile.c_str(), "r");
        if (!a) return std::string("<no args file>");
        char line[512];
        bool next = false;
        while (fgets(line, sizeof(line), a)) {
            std::string l(line);
            if (!l.empty() && l.back() == '\n') l.pop_back();
            if (next) { out = l; break; }
            next = (l == "--camera");
        }
        fclose(a);
        unlink(argsFile.c_str());
        return out;
    };
    std::string sock = stubDir + "/never.sock";
    {
        NativeSkeletonProvider provider;
        EXPECT_FALSE(provider.Start(sock, "unused.task"));
        EXPECT_EQ(cameraArg(), "auto");
        EXPECT_NE(provider.LastProblem().text.find("camera auto"), std::string::npos);
    }
    {
        NativeSkeletonProvider provider;
        const std::string byId = "/dev/v4l/by-id/usb-Fake_Camera-video-index0";
        EXPECT_FALSE(provider.Start(sock, "unused.task", byId));
        EXPECT_EQ(cameraArg(), byId);
        EXPECT_NE(provider.LastProblem().text.find(byId), std::string::npos);
    }
    {
        NativeSkeletonProvider provider;
        EXPECT_FALSE(provider.Start(sock, "unused.task", 2));
        EXPECT_EQ(cameraArg(), "2");
    }
    NativeNotice::Clear("pose");
    unlink(stub.c_str());
    rmdir(stubDir.c_str());
}

// A python3 that behaves like pose_server.py on a box with no camera: it exits
// EXIT_NO_CAMERA without ever binding the socket.
TEST(PoseServerStartTest, NoCameraFailsInUnderASecondAndPostsANotice) {
    const char *tmp = getenv("TMPDIR");
    std::string dir = std::string((tmp && *tmp) ? tmp : "/tmp") + "/dc3_pose_stub_XXXXXX";
    std::vector<char> buf(dir.begin(), dir.end());
    buf.push_back('\0');
    ASSERT_NE(mkdtemp(buf.data()), nullptr);
    std::string stubDir(buf.data());
    std::string stub = stubDir + "/python3";
    FILE *f = fopen(stub.c_str(), "w");
    ASSERT_NE(f, nullptr);
    fprintf(f, "#!/bin/sh\nsleep 0.2\nexit %d\n", (int)NativeSkeletonProvider::kExitNoCamera);
    fclose(f);
    chmod(stub.c_str(), 0755);

    const char *oldPath = getenv("PATH");
    std::string path = stubDir + ":" + (oldPath ? oldPath : "/usr/bin:/bin");
    ScopedEnv envPath("PATH", path.c_str());
    ScopedEnv envPy("DC3_POSE_PYTHON", stub.c_str()); // else the game prefers a venv
    ScopedEnv noSpawn("DC3_POSE_NO_SPAWN", nullptr);
    NativeNotice::Clear("pose");

    NativeSkeletonProvider provider;
    std::string sock = stubDir + "/never.sock";
    auto t0 = std::chrono::steady_clock::now();
    bool started = provider.Start(sock, "unused.task", 4);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    EXPECT_FALSE(started);
    EXPECT_FALSE(provider.IsRunning());
    EXPECT_LT(secs, 1.5) << "Start() waited out its whole timeout instead of noticing the exit";
    EXPECT_EQ(provider.LastProblem().brief, "No camera: pose tracking off");
    EXPECT_NE(provider.LastProblem().text.find("camera 4"), std::string::npos);
    EXPECT_NE(NativeNotice::Text("pose").find("No camera found"), std::string::npos)
        << "notice: '" << NativeNotice::Text("pose") << "'";
    NativeNotice::View v;
    EXPECT_TRUE(NativeNotice::Current(v));
    EXPECT_TRUE(v.banner);
    NativeNotice::Clear("pose");
    EXPECT_EQ(NativeNotice::Text("pose"), "");

    unlink(stub.c_str());
    rmdir(stubDir.c_str());
}

} // namespace
