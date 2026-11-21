// The pose server's launch path: NativeSpawner (platform/ProcessSpawner_Native.h)
// and NativeSkeletonProvider's restart-on-death.
//
// The bug this guards (measured on a GPD Pocket, Arch + PipeWire, no rtkit):
// opening audio lowers the GAME's RLIMIT_RTTIME to 0:0 (PipeWire's module-rt),
// a pose_server.py fork+exec'd from the game afterwards inherits the hard 0,
// and MediaPipe's realtime thread gets it SIGKILLed at model load.  These tests
// reproduce that on any Linux box by lowering the limit themselves, after the
// spawner has been forked, exactly where audio init would.
//
// Every stub "python3" here is a script put first on PATH, as
// PoseServerStartTest does; the provider launches it as it would launch
// pose_server.py.  Every wait is bounded; only pids this test launched are
// signalled.

#include <gtest/gtest.h>

#include "platform/NativeNotice.h"
#include "platform/PoseCamera_Native.h"
#include "platform/ProcessSpawner_Native.h"
#include "platform/Skeleton_Native.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__linux__)

namespace {

class ScopedEnv {
public:
    ScopedEnv(const char *name, const char *value) : mName(name) {
        const char *old = getenv(name);
        mHad = old != nullptr;
        if (old)
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
    bool mHad = false;
};

std::string ReadFile(const std::string &path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool WaitUntil(const std::function<bool()> &cond, int timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (cond())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return cond();
}

// Gone, or a zombie nobody has reaped yet (it is not running either way).
bool ProcessGone(pid_t pid) {
    if (kill(pid, 0) != 0 && errno == ESRCH)
        return true;
    std::string stat = ReadFile("/proc/" + std::to_string(pid) + "/stat");
    size_t paren = stat.rfind(')');
    return stat.empty() || (paren != std::string::npos && paren + 2 < stat.size()
                            && stat[paren + 2] == 'Z');
}

// "Max realtime timeout  <soft>  <hard>  us" from a /proc/<pid>/limits dump.
std::string RtTimeLine(const std::string &limits) {
    std::istringstream ss(limits);
    std::string line;
    while (std::getline(ss, line))
        if (line.compare(0, 20, "Max realtime timeout") == 0)
            return line;
    return std::string();
}

bool RtTimeUnlimited(const std::string &line) {
    std::istringstream ss(line.substr(20));
    std::string soft, hard;
    ss >> soft >> hard;
    return soft == "unlimited" && hard == "unlimited";
}

// The first python3 on PATH that is not one of our stubs: the stub servers
// below are Python, run by it.
std::string RealPython() {
    const char *path = getenv("PATH");
    std::istringstream ss(path ? path : "/usr/bin:/bin");
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        std::string cand = dir + "/python3";
        if (dir.find("dc3_spawn_stub_") == std::string::npos && access(cand.c_str(), X_OK) == 0)
            return cand;
    }
    return std::string();
}

// A temp dir with an executable `python3` in it, first on PATH and named by
// DC3_POSE_PYTHON while alive.
class StubDir {
public:
    explicit StubDir(const std::string &python3Body) {
        const char *tmp = getenv("TMPDIR");
        std::string t = std::string((tmp && *tmp) ? tmp : "/tmp") + "/dc3_spawn_stub_XXXXXX";
        std::vector<char> buf(t.begin(), t.end());
        buf.push_back('\0');
        if (!mkdtemp(buf.data()))
            return;
        mDir = buf.data();
        std::string stub = mDir + "/python3";
        FILE *f = fopen(stub.c_str(), "w");
        if (!f)
            return;
        fputs(python3Body.c_str(), f);
        fclose(f);
        chmod(stub.c_str(), 0755);
        const char *oldPath = getenv("PATH");
        mPath.reset(new ScopedEnv("PATH", (mDir + ":" + (oldPath ? oldPath : "/usr/bin:/bin")).c_str()));
        mStubDirEnv.reset(new ScopedEnv("DC3_STUB_DIR", mDir.c_str()));
        // The game prefers a venv (ChoosePosePython) to python3 from PATH.
        mPython.reset(new ScopedEnv("DC3_POSE_PYTHON", stub.c_str()));
        mOk = true;
    }
    ~StubDir() {
        // Only the files this test made, by name.
        for (const char *name : { "python3", "stub.py", "runs", "limits.1", "limits.2", "limits.3",
                                  "limits.4", "server.sock", "stderr.log", "cameras",
                                  "pose_camera.txt", "pose_camera.txt.tmp" })
            unlink((mDir + "/" + name).c_str());
        rmdir(mDir.c_str());
    }
    bool ok() const { return mOk; }
    const std::string &dir() const { return mDir; }
    std::string sock() const { return mDir + "/server.sock"; }
    int Runs() const {
        std::istringstream ss(ReadFile(mDir + "/runs"));
        std::string line;
        int n = 0;
        while (std::getline(ss, line))
            n++;
        return n;
    }
    // The --camera each run was launched with, one per run.
    std::vector<std::string> Cameras() const {
        std::istringstream ss(ReadFile(mDir + "/cameras"));
        std::string line;
        std::vector<std::string> out;
        while (std::getline(ss, line))
            out.push_back(line);
        return out;
    }

private:
    std::string mDir;
    bool mOk = false;
    std::unique_ptr<ScopedEnv> mPath, mStubDirEnv, mPython;
};

// A pose server stand-in: records each run's pid and RLIMIT_RTTIME, binds the
// socket, and on accept either SIGKILLs itself (DC3_STUB_MODE=die always,
// die-once on its first run only) or streams empty v1 frames whose ids carry
// the run number (run * 1000 + n); stall-once SIGSTOPs itself after five
// frames of its first run, staying connected; slow-start is silent for 1.5 s
// after it is connected (a model loading), then streams.
const char *kStubPy = R"PY(
import os, signal, socket, struct, sys, time
d = os.environ["DC3_STUB_DIR"]
mode = os.environ.get("DC3_STUB_MODE", "serve")
with open(os.path.join(d, "runs"), "a") as f:
    f.write("%d\n" % os.getpid())
run = sum(1 for _ in open(os.path.join(d, "runs")))
if "--camera" in sys.argv:
    with open(os.path.join(d, "cameras"), "a") as f:
        f.write(sys.argv[sys.argv.index("--camera") + 1] + "\n")
with open("/proc/self/limits") as src, open(os.path.join(d, "limits.%d" % run), "w") as dst:
    dst.write(src.read())
path = sys.argv[sys.argv.index("--socket") + 1]
if os.path.exists(path):
    os.unlink(path)
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(path)
srv.listen(1)
c, _ = srv.accept()
if mode == "die" or (mode == "die-once" and run == 1):
    time.sleep(0.2)
    os.kill(os.getpid(), signal.SIGKILL)
if mode == "slow-start":
    time.sleep(1.5)
n = 0
while True:
    n += 1
    body = struct.pack("<IId", run * 1000 + n, 0, time.time())
    c.sendall(struct.pack("<I", len(body)) + body)
    if mode == "stall-once" and run == 1 and n == 5:
        os.kill(os.getpid(), signal.SIGSTOP)  # hung: connected, silent
    time.sleep(0.03)
)PY";

std::string PythonStub(const std::string &realPython) {
    return "#!/bin/sh\nexec " + realPython + " \"$DC3_STUB_DIR/stub.py\" \"$@\"\n";
}

bool WriteStubPy(const StubDir &dir) {
    FILE *f = fopen((dir.dir() + "/stub.py").c_str(), "w");
    if (!f)
        return false;
    fputs(kStubPy, f);
    fclose(f);
    return true;
}

// Each test starts from a process with no spawner, as a fresh main() would.
class PoseSpawnerTest : public ::testing::Test {
protected:
    void SetUp() override {
        NativeSpawner::ShutdownForTest();
        NativeNotice::Clear("pose");
    }
    void TearDown() override {
        NativeSpawner::ShutdownForTest();
        NativeNotice::Clear("pose");
    }
};

} // namespace

// The regression test for the GPD SIGKILL.  The spawner is forked first (the
// top of main), THEN this process's RLIMIT_RTTIME drops to 0:0 the way
// PipeWire's module-rt drops the game's at audio init, THEN the provider
// launches the server.  The server must see the limits the process started
// with, not the lowered ones.
TEST_F(PoseSpawnerTest, TheServerDoesNotInheritTheGamesLoweredRealtimeLimit) {
    struct rlimit start;
    ASSERT_EQ(getrlimit(RLIMIT_RTTIME, &start), 0);
    if (start.rlim_max != RLIM_INFINITY)
        GTEST_SKIP() << "this process was started with RLIMIT_RTTIME hard " << start.rlim_max
                     << ": nothing left to lose, so the test cannot tell a fix from no fix";

    ASSERT_TRUE(NativeSpawner::Init()); // main()'s first line

    struct rlimit zero = { 0, 0 };
    ASSERT_EQ(setrlimit(RLIMIT_RTTIME, &zero), 0);
    ASSERT_FALSE(RtTimeUnlimited(RtTimeLine(ReadFile("/proc/self/limits"))))
        << "control: the limit did not drop";

    // A stub that records its own limits and exits as a no-camera server
    // would, so Start() returns at once.
    StubDir stub("#!/bin/sh\ncat /proc/$$/limits > \"$DC3_STUB_DIR/limits.1\"\nexit 3\n");
    ASSERT_TRUE(stub.ok());
    ScopedEnv noSpawn("DC3_POSE_NO_SPAWN", nullptr);

    // Control: a child forked from this process now DOES inherit 0:0 -- the
    // bug the spawner exists for is real in this process.
    {
        pid_t pid = fork();
        if (pid == 0) {
            execlp("python3", "python3", (char *)nullptr);
            _exit(127);
        }
        ASSERT_GT(pid, 0);
        int st = 0;
        waitpid(pid, &st, 0);
        std::string line = RtTimeLine(ReadFile(stub.dir() + "/limits.1"));
        ASSERT_FALSE(line.empty());
        EXPECT_FALSE(RtTimeUnlimited(line)) << "control: a direct child escaped the limit: " << line;
        unlink((stub.dir() + "/limits.1").c_str());
    }

    NativeSkeletonProvider provider;
    EXPECT_FALSE(provider.Start(stub.sock(), "unused.task", 0));
    EXPECT_EQ(provider.LastProblem().brief, "No camera: pose tracking off")
        << "the exit code did not come back through the spawner";
    std::string line = RtTimeLine(ReadFile(stub.dir() + "/limits.1"));
    ASSERT_FALSE(line.empty()) << "the stub server never ran";
    EXPECT_TRUE(RtTimeUnlimited(line))
        << "the pose server inherited the lowered RLIMIT_RTTIME: " << line;
}

// The exit status of a helper is relayed exactly: an exit code, and a signal.
TEST_F(PoseSpawnerTest, ExitStatusIsRelayed) {
    ASSERT_TRUE(NativeSpawner::Init());
    pid_t a = NativeSpawner::Spawn({ "sh", "-c", "exit 7" });
    pid_t b = NativeSpawner::Spawn({ "sh", "-c", "kill -9 $$" });
    pid_t c = NativeSpawner::Spawn({ "dc3-no-such-program-anywhere" });
    ASSERT_GT(a, 0);
    ASSERT_GT(b, 0);
    ASSERT_GT(c, 0);
    int sa = -1, sb = -1, sc = -1;
    EXPECT_TRUE(WaitUntil([&] { return NativeSpawner::TryReap(a, &sa); }, 3000));
    EXPECT_TRUE(WaitUntil([&] { return NativeSpawner::TryReap(b, &sb); }, 3000));
    EXPECT_TRUE(WaitUntil([&] { return NativeSpawner::TryReap(c, &sc); }, 3000));
    EXPECT_TRUE(WIFEXITED(sa) && WEXITSTATUS(sa) == 7) << "status 0x" << std::hex << sa;
    EXPECT_TRUE(WIFSIGNALED(sb) && WTERMSIG(sb) == SIGKILL) << "status 0x" << std::hex << sb;
    EXPECT_TRUE(WIFEXITED(sc) && WEXITSTATUS(sc) == 127) << "status 0x" << std::hex << sc;
    EXPECT_FALSE(NativeSpawner::TryReap(a, &sa)) << "an exit is reported once";
}

// A server killed by signal 9 before it binds: Start() says so.
TEST_F(PoseSpawnerTest, AServerKilledBeforeItConnectsIsReportedAsSignal9) {
    ASSERT_TRUE(NativeSpawner::Init());
    StubDir stub("#!/bin/sh\nkill -9 $$\n");
    ASSERT_TRUE(stub.ok());
    ScopedEnv noSpawn("DC3_POSE_NO_SPAWN", nullptr);
    NativeSkeletonProvider provider;
    EXPECT_FALSE(provider.Start(stub.sock(), "unused.task", 0));
    EXPECT_EQ(provider.LastProblem().brief, "Pose server crashed: pose tracking off");
    EXPECT_NE(provider.LastProblem().text.find("crashed (signal 9)"), std::string::npos)
        << provider.LastProblem().text;
    EXPECT_NE(NativeNotice::Text("pose").find("The pose server crashed (signal 9)"), std::string::npos)
        << NativeNotice::Text("pose");
}

// The GPD shape: the server connects, then is SIGKILLed (at model load).  With
// restarts off the player is told it crashed with signal 9.
TEST_F(PoseSpawnerTest, AConnectedServerKilledBySignal9IsReportedAsACrash) {
    std::string py = RealPython();
    if (py.empty())
        GTEST_SKIP() << "no python3 to run the stub server";
    ASSERT_TRUE(NativeSpawner::Init());
    StubDir stub(PythonStub(py));
    ASSERT_TRUE(stub.ok() && WriteStubPy(stub));
    ScopedEnv mode("DC3_STUB_MODE", "die");
    ScopedEnv noSpawn("DC3_POSE_NO_SPAWN", nullptr);

    NativeSkeletonProvider provider;
    provider.SetRestartPolicy(0, 10);
    ASSERT_TRUE(provider.Start(stub.sock(), "unused.task", 0));
    pid_t server = provider.ServerPid();
    ASSERT_GT(server, 0);
    EXPECT_TRUE(WaitUntil([&] { return !provider.IsRunning(); }, 5000));
    EXPECT_EQ(provider.LastProblem().brief, "Pose server crashed: pose tracking off");
    EXPECT_NE(provider.LastProblem().text.find("crashed (signal 9)"), std::string::npos)
        << provider.LastProblem().text;
    EXPECT_EQ(provider.Restarts(), 0);
    EXPECT_EQ(stub.Runs(), 1);
    EXPECT_TRUE(ProcessGone(server));
    provider.Stop();
}

// Restart-on-death: the first server is SIGKILLed after connecting, the
// relaunched one streams; tracking resumes on the new socket, the notice says
// what happened while it restarted, and is cleared once it is back.
TEST_F(PoseSpawnerTest, ADeadServerIsRestartedAndTrackingResumes) {
    std::string py = RealPython();
    if (py.empty())
        GTEST_SKIP() << "no python3 to run the stub server";
    ASSERT_TRUE(NativeSpawner::Init());
    StubDir stub(PythonStub(py));
    ASSERT_TRUE(stub.ok() && WriteStubPy(stub));
    ScopedEnv mode("DC3_STUB_MODE", "die-once");
    ScopedEnv noSpawn("DC3_POSE_NO_SPAWN", nullptr);

    NativeSkeletonProvider provider;
    provider.SetRestartPolicy(3, 300);
    ASSERT_TRUE(provider.Start(stub.sock(), "unused.task", 0));
    pid_t first = provider.ServerPid();
    ASSERT_GT(first, 0);

    // During the 300 ms backoff the player sees a restart notice.
    std::string seen;
    EXPECT_TRUE(WaitUntil([&] {
        std::string t = NativeNotice::Text("pose");
        if (!t.empty())
            seen = t;
        return !seen.empty();
    }, 5000));
    EXPECT_NE(seen.find("The pose server crashed (signal 9) - restarting pose tracking (1 of 3)"),
              std::string::npos) << seen;

    // Frames from the second run (ids 2001, 2002, ...) arrive.
    EXPECT_TRUE(WaitUntil([&] {
        provider.Poll();
        return provider.FrameId() > 2000;
    }, 8000)) << "frame id " << provider.FrameId();
    EXPECT_TRUE(provider.IsRunning());
    EXPECT_EQ(provider.Restarts(), 1);
    EXPECT_EQ(stub.Runs(), 2);
    EXPECT_NE(provider.ServerPid(), first);
    EXPECT_TRUE(ProcessGone(first));
    EXPECT_TRUE(provider.LastProblem().empty()) << provider.LastProblem().text;
    EXPECT_EQ(NativeNotice::Text("pose"), "");

    pid_t second = provider.ServerPid();
    provider.Stop();
    EXPECT_TRUE(WaitUntil([&] { return ProcessGone(second); }, 3000)) << "Stop() left the server";
}

// The camera setting at run time (PoseCamera_Native.h): a switch replaces the
// running server with one launched on the new --camera -- no backoff, no
// restart spent, no "pose" problem left up -- and tracking resumes.
// Sabotaged: drop the mSwitchRequested check from ReaderThread -> the server
// is never replaced, Runs() stays 1 and the frame-id wait times out.
TEST_F(PoseSpawnerTest, ACameraSwitchReplacesTheServerOnTheNewCamera) {
    std::string py = RealPython();
    if (py.empty())
        GTEST_SKIP() << "no python3 to run the stub server";
    ASSERT_TRUE(NativeSpawner::Init());
    StubDir stub(PythonStub(py));
    ASSERT_TRUE(stub.ok() && WriteStubPy(stub));
    ScopedEnv mode("DC3_STUB_MODE", "serve");
    ScopedEnv noSpawn("DC3_POSE_NO_SPAWN", nullptr);
    ScopedEnv noVideo("DC3_POSE_VIDEO", nullptr);

    NativeSkeletonProvider provider;
    provider.SetRestartPolicy(3, 300);
    ASSERT_TRUE(provider.Start(stub.sock(), "unused.task", "auto"));
    pid_t first = provider.ServerPid();
    ASSERT_TRUE(WaitUntil([&] {
        provider.Poll();
        return provider.FrameId() > 1000;
    }, 5000));
    EXPECT_TRUE(provider.LaunchesServer());
    EXPECT_EQ(provider.Camera(), "auto");

    auto started = std::chrono::steady_clock::now();
    ASSERT_TRUE(provider.SwitchCamera("id:fake-camera-b"));
    EXPECT_EQ(provider.Camera(), "id:fake-camera-b");
    EXPECT_TRUE(WaitUntil([&] {
        provider.Poll();
        return provider.FrameId() > 2000;
    }, 8000)) << "frame id " << provider.FrameId();
    double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    printf("camera switch: frames from the new server after %.0f ms\n", ms);
    EXPECT_TRUE(provider.IsRunning());
    EXPECT_EQ(provider.CameraSwitches(), 1);
    EXPECT_EQ(provider.Restarts(), 0) << "a switch is not a restart";
    EXPECT_EQ(stub.Runs(), 2);
    EXPECT_EQ(stub.Cameras(), (std::vector<std::string>{ "auto", "id:fake-camera-b" }));
    EXPECT_NE(provider.ServerPid(), first);
    EXPECT_TRUE(WaitUntil([&] { return ProcessGone(first); }, 3000)) << "the old server lives on";
    EXPECT_TRUE(provider.LastProblem().empty()) << provider.LastProblem().text;
    EXPECT_EQ(NativeNotice::Text("pose"), "");

    pid_t second = provider.ServerPid();
    provider.Stop();
    EXPECT_TRUE(WaitUntil([&] { return ProcessGone(second); }, 3000)) << "Stop() left the server";
}

std::string TwoFakeCameras() {
    return "dc3-pose-cameras 1\n"
           "camera\tid:0x220000046d0944\t0x220000046d0944\tMX Brio\n"
           "camera\tid:3642F2CD-E322-42E7-9360-19815B003AA6\t3642F2CD-E322-42E7-9360-19815B003AA6\t"
           "FaceTime HD Camera\n";
}
std::string OneFakeCamera() {
    return "dc3-pose-cameras 1\n"
           "camera\tid:3642F2CD-E322-42E7-9360-19815B003AA6\t3642F2CD-E322-42E7-9360-19815B003AA6\t"
           "FaceTime HD Camera\n";
}

// The setting end to end, as the Options row drives it: the launch is auto,
// "next camera" restarts the server on the second camera's IDENTITY, names it
// in a notice and saves it; with one camera -- the one in use -- selecting
// the row restarts nothing; and the next launch reads the saved choice.  The
// cameras are a fixed list (this box's devices are never asked).
TEST_F(PoseSpawnerTest, TheCameraSettingSwitchesAtRunTimeAndIsRememberedAtTheNextLaunch) {
    std::string py = RealPython();
    if (py.empty())
        GTEST_SKIP() << "no python3 to run the stub server";
    ASSERT_TRUE(NativeSpawner::Init());
    StubDir stub(PythonStub(py));
    ASSERT_TRUE(stub.ok() && WriteStubPy(stub));
    ScopedEnv mode("DC3_STUB_MODE", "serve");
    ScopedEnv noSpawn("DC3_POSE_NO_SPAWN", nullptr);
    ScopedEnv noVideo("DC3_POSE_VIDEO", nullptr);
    ScopedEnv noCamera("DC3_POSE_CAMERA", nullptr);
    const std::string faceTime = "id:3642F2CD-E322-42E7-9360-19815B003AA6";
    const std::string savedPath = stub.dir() + "/pose_camera.txt";

    struct Restore {
        NativeSkeletonProvider *old = TheSkeletonProvider;
        ~Restore() {
            TheSkeletonProvider = old;
            Dc3PoseCamera::SetListerForTest(nullptr);
            Dc3PoseCamera::SetSavedPathForTest("");
            Dc3PoseCamera::ResetForTest();
        }
    } restore;
    Dc3PoseCamera::SetListerForTest(TwoFakeCameras);
    Dc3PoseCamera::SetSavedPathForTest(savedPath);
    Dc3PoseCamera::ResetForTest();

    // No provider: there is nothing to switch, and the row is not shown.
    TheSkeletonProvider = nullptr;
    EXPECT_FALSE(Dc3PoseCamera::Available());
    EXPECT_FALSE(Dc3PoseCamera::Cycle());

    const Dc3PoseCamera::Launch &launch = Dc3PoseCamera::Boot();
    EXPECT_EQ(launch.spec, "auto");
    EXPECT_EQ(launch.source, Dc3PoseCamera::kFromDefault);

    NativeSkeletonProvider provider;
    provider.SetRestartPolicy(3, 300);
    ASSERT_TRUE(provider.Start(stub.sock(), "unused.task", launch.spec));
    TheSkeletonProvider = &provider;
    ASSERT_TRUE(WaitUntil([&] {
        provider.Poll();
        return provider.FrameId() > 1000;
    }, 5000));
    ASSERT_TRUE(Dc3PoseCamera::Available());
    EXPECT_EQ(Dc3PoseCamera::Description(),
              "Camera in use: MX Brio (automatic). Select to switch to the next of 2 cameras.");

    // Next camera: auto is the first (MX Brio), so this is the FaceTime one.
    EXPECT_TRUE(Dc3PoseCamera::Cycle());
    EXPECT_EQ(Dc3PoseCamera::Current(), faceTime);
    EXPECT_EQ(Dc3PoseCamera::CurrentName(), "FaceTime HD Camera");
    EXPECT_EQ(NativeNotice::Text("pose-camera"), "Camera: FaceTime HD Camera (2 of 2).");
    EXPECT_TRUE(WaitUntil([&] {
        provider.Poll();
        return provider.FrameId() > 2000;
    }, 8000)) << "frame id " << provider.FrameId();
    EXPECT_EQ(stub.Cameras(), (std::vector<std::string>{ "auto", faceTime }));
    Dc3PoseCamera::Saved saved = Dc3PoseCamera::LoadSaved(savedPath);
    EXPECT_EQ(saved.spec, faceTime);
    EXPECT_EQ(saved.name, "FaceTime HD Camera");

    // The Brio is unplugged: one camera, the one in use.  Nothing restarts.
    Dc3PoseCamera::SetListerForTest(OneFakeCamera);
    EXPECT_TRUE(Dc3PoseCamera::Cycle());
    EXPECT_EQ(NativeNotice::Text("pose-camera"), "Camera: FaceTime HD Camera (the only camera found).");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(stub.Runs(), 2);
    EXPECT_EQ(provider.CameraSwitches(), 1);

    // By name, any case; a name that is nobody's changes nothing.
    Dc3PoseCamera::SetListerForTest(TwoFakeCameras);
    EXPECT_FALSE(Dc3PoseCamera::Set("no such camera"));
    EXPECT_EQ(Dc3PoseCamera::Current(), faceTime);
    EXPECT_TRUE(Dc3PoseCamera::Set("mx brio"));
    EXPECT_EQ(Dc3PoseCamera::Current(), "id:0x220000046d0944");
    EXPECT_TRUE(WaitUntil([&] {
        provider.Poll();
        return provider.FrameId() > 3000;
    }, 8000)) << "frame id " << provider.FrameId();
    EXPECT_EQ(stub.Cameras().back(), "id:0x220000046d0944");
    EXPECT_EQ(provider.Restarts(), 0);
    provider.Stop();
    TheSkeletonProvider = nullptr;

    // The next launch: the saved choice, by identity.  With that camera
    // unplugged: auto for this run, a notice, and the choice still saved.
    Dc3PoseCamera::ResetForTest();
    const Dc3PoseCamera::Launch &again = Dc3PoseCamera::Boot();
    EXPECT_EQ(again.spec, "id:0x220000046d0944");
    EXPECT_EQ(again.source, Dc3PoseCamera::kFromSaved);
    EXPECT_EQ(again.name, "MX Brio");

    Dc3PoseCamera::ResetForTest();
    Dc3PoseCamera::SetListerForTest(OneFakeCamera);
    const Dc3PoseCamera::Launch &unplugged = Dc3PoseCamera::Boot();
    EXPECT_EQ(unplugged.spec, "auto");
    EXPECT_EQ(unplugged.source, Dc3PoseCamera::kFromSavedMissing);
    EXPECT_NE(NativeNotice::Text("pose-camera").find("\"MX Brio\" is not connected"),
              std::string::npos) << NativeNotice::Text("pose-camera");
    EXPECT_EQ(Dc3PoseCamera::LoadSaved(savedPath).spec, "id:0x220000046d0944");

    // DC3_POSE_CAMERA wins over the file and does not rewrite it.
    Dc3PoseCamera::ResetForTest();
    {
        ScopedEnv env("DC3_POSE_CAMERA", "1");
        const Dc3PoseCamera::Launch &byEnv = Dc3PoseCamera::Boot();
        EXPECT_EQ(byEnv.spec, "1");
        EXPECT_EQ(byEnv.source, Dc3PoseCamera::kFromEnv);
        EXPECT_EQ(Dc3PoseCamera::LoadSaved(savedPath).spec, "id:0x220000046d0944");
    }
}

// A server that hangs -- connected, alive, silent (np-cutoff, PLAN H-94) --
// is replaced: after the stall time the provider SIGKILLs it (a stopped
// process ignores SIGTERM) and relaunches it under the restart policy.
// Before, the reader polled the silent socket forever and the game scored a
// frozen player.  Sabotaged: SetServerStallMs(0) here -> no restart, the
// frame-id wait times out, caught.
TEST_F(PoseSpawnerTest, AStalledServerIsReplacedAndTrackingResumes) {
    std::string py = RealPython();
    if (py.empty())
        GTEST_SKIP() << "no python3 to run the stub server";
    ASSERT_TRUE(NativeSpawner::Init());
    StubDir stub(PythonStub(py));
    ASSERT_TRUE(stub.ok() && WriteStubPy(stub));
    ScopedEnv mode("DC3_STUB_MODE", "stall-once");
    ScopedEnv noSpawn("DC3_POSE_NO_SPAWN", nullptr);

    NativeSkeletonProvider provider;
    provider.SetRestartPolicy(3, 200);
    provider.SetServerStallMs(600);
    ASSERT_TRUE(provider.Start(stub.sock(), "unused.task", 0));
    pid_t first = provider.ServerPid();
    ASSERT_GT(first, 0);

    std::string seen;
    EXPECT_TRUE(WaitUntil([&] {
        std::string t = NativeNotice::Text("pose");
        if (t.find("stalled") != std::string::npos)
            seen = t;
        return !seen.empty();
    }, 5000)) << NativeNotice::Text("pose");
    EXPECT_NE(seen.find("The pose server stalled - restarting pose tracking (1 of 3)"),
              std::string::npos) << seen;
    EXPECT_TRUE(WaitUntil([&] {
        provider.Poll();
        return provider.FrameId() > 2000;
    }, 8000)) << "frame id " << provider.FrameId();
    EXPECT_TRUE(provider.IsRunning());
    EXPECT_EQ(provider.ServerStalls(), 1);
    EXPECT_EQ(provider.Restarts(), 1);
    EXPECT_EQ(stub.Runs(), 2);
    EXPECT_TRUE(ProcessGone(first));
    EXPECT_EQ(NativeNotice::Text("pose"), "");

    pid_t second = provider.ServerPid();
    provider.Stop();
    EXPECT_TRUE(WaitUntil([&] { return ProcessGone(second); }, 3000)) << "Stop() left the server";
}

// A server that is connected and has not yet sent its first frame is loading
// its model, not hung (np-pose-steady; MEASURED on an M3 MacBook Air: the 5 s
// watchdog killed the server four times in a row while MediaPipe loaded, so
// there was no tracking at all).  Here: silent for 1.5 s under a 0.6 s stall
// time.  Until the first frame the limit is kServerFirstFrameMs.  Sabotaged:
// the reader using the stall time before the first frame too -> killed at
// 0.6 s, caught; and the first packet never counting as the first frame ->
// AStalledServerIsReplacedAndTrackingResumes waits 30 s for its stall, caught.
TEST_F(PoseSpawnerTest, AServerSilentWhileItLoadsIsNotReplaced) {
    std::string py = RealPython();
    if (py.empty())
        GTEST_SKIP() << "no python3 to run the stub server";
    ASSERT_TRUE(NativeSpawner::Init());
    StubDir stub(PythonStub(py));
    ASSERT_TRUE(stub.ok() && WriteStubPy(stub));
    ScopedEnv mode("DC3_STUB_MODE", "slow-start");
    ScopedEnv noSpawn("DC3_POSE_NO_SPAWN", nullptr);

    NativeSkeletonProvider provider;
    provider.SetRestartPolicy(3, 200);
    provider.SetServerStallMs(600);
    ASSERT_TRUE(provider.Start(stub.sock(), "unused.task", 0));
    pid_t first = provider.ServerPid();
    ASSERT_GT(first, 0);
    EXPECT_TRUE(WaitUntil([&] {
        provider.Poll();
        return provider.FrameId() > 1000;
    }, 8000)) << "frame id " << provider.FrameId();
    EXPECT_EQ(provider.FrameId() / 1000, 1u) << "the frames are a relaunched server's";
    EXPECT_EQ(provider.ServerStalls(), 0) << "killed while it was starting";
    EXPECT_EQ(provider.Restarts(), 0);
    EXPECT_EQ(stub.Runs(), 1);
    EXPECT_EQ(provider.ServerPid(), first);
    provider.Stop();
    EXPECT_TRUE(WaitUntil([&] { return ProcessGone(first); }, 3000)) << "Stop() left the server";
}

// A server that keeps dying is relaunched exactly maxRestarts times, then the
// player is told tracking is off.  One "restarting" line per restart.
TEST_F(PoseSpawnerTest, RestartsAreBounded) {
    std::string py = RealPython();
    if (py.empty())
        GTEST_SKIP() << "no python3 to run the stub server";
    ASSERT_TRUE(NativeSpawner::Init());
    StubDir stub(PythonStub(py));
    ASSERT_TRUE(stub.ok() && WriteStubPy(stub));
    ScopedEnv mode("DC3_STUB_MODE", "die");
    ScopedEnv noSpawn("DC3_POSE_NO_SPAWN", nullptr);

    // Capture this process's stderr (the provider's console lines).
    std::string errPath = stub.dir() + "/stderr.log";
    fflush(stderr);
    int saved = dup(STDERR_FILENO);
    int fd = open(errPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);
    dup2(fd, STDERR_FILENO);
    close(fd);

    NativeSkeletonProvider provider;
    provider.SetRestartPolicy(3, 20);
    bool started = provider.Start(stub.sock(), "unused.task", 0);
    bool stopped = WaitUntil([&] { return !provider.IsRunning(); }, 20000);

    fflush(stderr);
    dup2(saved, STDERR_FILENO);
    close(saved);
    std::string err = ReadFile(errPath);

    ASSERT_TRUE(started);
    EXPECT_TRUE(stopped) << err;
    EXPECT_EQ(provider.Restarts(), 3);
    EXPECT_EQ(stub.Runs(), 4) << "the first launch plus three restarts";
    int lines = 0;
    for (size_t at = err.find("restarting it in"); at != std::string::npos;
         at = err.find("restarting it in", at + 1))
        lines++;
    EXPECT_EQ(lines, 3) << err;
    EXPECT_NE(err.find("crashed (signal 9) - restarting it in 20 ms (restart 1 of 3)"), std::string::npos) << err;
    EXPECT_NE(err.find("restarting it in 80 ms (restart 3 of 3)"), std::string::npos) << err;
    EXPECT_EQ(provider.LastProblem().brief, "Pose server crashed: pose tracking off");
    provider.Stop();
}

// A failure no relaunch can fix is not retried.
TEST_F(PoseSpawnerTest, AnUnfixableExitIsNotRestarted) {
    using P = NativeSkeletonProvider;
    EXPECT_TRUE(P::ServerExitIsRestartable(SIGKILL));
    EXPECT_TRUE(P::ServerExitIsRestartable(SIGSEGV));
    EXPECT_TRUE(P::ServerExitIsRestartable(P::kExitCameraLost << 8));
    EXPECT_TRUE(P::ServerExitIsRestartable(1 << 8));
    EXPECT_FALSE(P::ServerExitIsRestartable(0));
    EXPECT_FALSE(P::ServerExitIsRestartable(P::kExitNoModel << 8));
    EXPECT_FALSE(P::ServerExitIsRestartable(P::kExitNoDeps << 8));
    EXPECT_FALSE(P::ServerExitIsRestartable(P::kExitNoVideo << 8));
    EXPECT_FALSE(P::ServerExitIsRestartable(P::kExitExecFailed << 8));
}

namespace {

// Fork a stand-in game that starts the spawner, launches `helperScript`
// through it, reports both pids, then either exits (`how` = "exit"), waits to
// be SIGKILLed ("killed"), or has its spawner SIGKILLed ("spawner-killed").
// Returns false on setup failure.
bool RunStandInGame(const char *how, const char *helperScript, pid_t &spawner, pid_t &helper,
                    std::string &why) {
    int fds[2];
    if (pipe(fds) != 0) {
        why = "pipe";
        return false;
    }
    pid_t game = fork();
    if (game == 0) {
        close(fds[0]);
        int32_t pids[2] = { -1, -1 };
        if (NativeSpawner::Init()) {
            pids[0] = NativeSpawner::SpawnerPid();
            pids[1] = NativeSpawner::Spawn({ "sh", "-c", helperScript });
        }
        (void)!write(fds[1], pids, sizeof(pids));
        close(fds[1]);
        if (!strcmp(how, "exit"))
            _exit(0);
        for (int i = 0; i < 600; i++)
            usleep(100 * 1000);
        _exit(0);
    }
    close(fds[1]);
    int32_t pids[2] = { -1, -1 };
    bool got = read(fds[0], pids, sizeof(pids)) == (ssize_t)sizeof(pids);
    close(fds[0]);
    spawner = pids[0];
    helper = pids[1];
    if (!got || spawner <= 0 || helper <= 0) {
        why = "the stand-in game did not start a spawner and a helper";
        kill(game, SIGKILL);
        waitpid(game, nullptr, 0);
        return false;
    }
    // Let the helper get going (and install its trap).
    usleep(300 * 1000);
    if (!strcmp(how, "killed"))
        kill(game, SIGKILL);
    else if (!strcmp(how, "spawner-killed"))
        kill(spawner, SIGKILL);
    if (!strcmp(how, "spawner-killed")) {
        // The game lives on; the helper must not outlive its spawner.
        bool gone = WaitUntil([&] { return ProcessGone(helper); }, 5000);
        kill(game, SIGKILL);
        waitpid(game, nullptr, 0);
        if (!gone) {
            why = "the helper outlived its SIGKILLed spawner";
            kill(helper, SIGKILL);
            return false;
        }
        return true;
    }
    int st;
    waitpid(game, &st, 0);
    return true;
}

} // namespace

// However the game ends, neither the spawner nor the server outlives it.
TEST_F(PoseSpawnerTest, NothingOutlivesAGameThatExits) {
    pid_t spawner = -1, helper = -1;
    std::string why;
    ASSERT_TRUE(RunStandInGame("exit", "sleep 60", spawner, helper, why)) << why;
    EXPECT_TRUE(WaitUntil([&] { return ProcessGone(helper); }, 5000)) << "server " << helper;
    EXPECT_TRUE(WaitUntil([&] { return ProcessGone(spawner); }, 5000)) << "spawner " << spawner;
    if (!ProcessGone(helper)) kill(helper, SIGKILL);
    if (!ProcessGone(spawner)) kill(spawner, SIGKILL);
}

TEST_F(PoseSpawnerTest, NothingOutlivesAGameThatIsKilled) {
    pid_t spawner = -1, helper = -1;
    std::string why;
    ASSERT_TRUE(RunStandInGame("killed", "sleep 60", spawner, helper, why)) << why;
    EXPECT_TRUE(WaitUntil([&] { return ProcessGone(helper); }, 5000)) << "server " << helper;
    EXPECT_TRUE(WaitUntil([&] { return ProcessGone(spawner); }, 5000)) << "spawner " << spawner;
    if (!ProcessGone(helper)) kill(helper, SIGKILL);
    if (!ProcessGone(spawner)) kill(spawner, SIGKILL);
}

// A server that ignores SIGTERM is SIGKILLed when the game goes.
TEST_F(PoseSpawnerTest, AServerThatIgnoresSigtermIsStillReaped) {
    pid_t spawner = -1, helper = -1;
    std::string why;
    ASSERT_TRUE(RunStandInGame("killed", "trap '' TERM; while :; do sleep 1; done", spawner,
                               helper, why)) << why;
    EXPECT_TRUE(WaitUntil([&] { return ProcessGone(helper); }, 6000)) << "server " << helper;
    EXPECT_TRUE(WaitUntil([&] { return ProcessGone(spawner); }, 6000)) << "spawner " << spawner;
    if (!ProcessGone(helper)) kill(helper, SIGKILL);
    if (!ProcessGone(spawner)) kill(spawner, SIGKILL);
}

// Not even a SIGKILLed spawner leaks the server (PR_SET_PDEATHSIG).
TEST_F(PoseSpawnerTest, ASpawnerKilledOutrightTakesTheServerWithIt) {
    pid_t spawner = -1, helper = -1;
    std::string why;
    EXPECT_TRUE(RunStandInGame("spawner-killed", "sleep 60", spawner, helper, why)) << why;
}

#endif // __linux__
