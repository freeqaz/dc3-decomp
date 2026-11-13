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
//
// Each case was sabotage-checked against the code it pins (see the commit).

#include <gtest/gtest.h>

#include "test_helpers.h" // EnsureSymbolInit
#include "platform/NativeNotice.h"
#include "platform/Skeleton_Native.h"
#include "platform/PoseLevel_Native.h"
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
