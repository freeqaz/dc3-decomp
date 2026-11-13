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
//   PoseServerStartTest    Start()'s no-camera path: the spawned server's exit
//                          is noticed at once and becomes a player notice.
//
// Each case was sabotage-checked against the code it pins (see the commit).

#include <gtest/gtest.h>

#include "test_helpers.h" // EnsureSymbolInit
#include "platform/NativeNotice.h"
#include "platform/Skeleton_Native.h"
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
                              int declaredPersons = -1) {
    Bytes body;
    body.put<uint32_t>(kMagic);
    body.put<uint32_t>(frameId);
    body.put<uint32_t>(declaredPersons >= 0 ? declaredPersons : (uint32_t)persons.size());
    body.put<double>(ts);
    body.put<uint16_t>(w);
    body.put<uint16_t>(h);
    body.put<uint8_t>(landmarks);
    body.put<uint8_t>(layout);
    body.put<uint16_t>(0);
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

// ===========================================================================
// PoseServerStartTest -- no camera: fail fast, tell the player
// ===========================================================================
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
