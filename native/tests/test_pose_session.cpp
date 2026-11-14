// Pose session file and pose server command line (PLAN L-1, playtest kit).
//
//   PoseSessionFileTest    dc3-pose-session v1: a recorded frame reads back
//                          bit for bit (floats, ids, the double timestamp,
//                          confidences, several people, nobody); a file
//                          without the header, or with a damaged record, is
//                          refused with the line named; DueIndex is the
//                          latest frame at or before a song time.
//   PoseServerCommandTest  what Start() execs: the camera by default; a video
//                          file instead (DC3_POSE_VIDEO, the kit's --video);
//                          the interpreter (DC3_POSE_PYTHON, the kit's venv);
//                          --hfov and --frames as before; --camera as given
//                          (auto, an index, a by-id name).
//   PosePythonChoiceTest   the interpreter the game picks: DC3_POSE_PYTHON,
//                          then the playtest venv (DC3_POSE_VENV or
//                          ~/.local/share/dc3/pose-venv), then the checkout's
//                          .venv (a worktree's main checkout), then python3;
//                          fake interpreters print which one ran.  The
//                          missing-packages notice names it and the fix.
// The end-to-end record -> replay case is PoseSessionTest in
// test_native_pose_socket.cpp (it needs that file's fake socket server).

#include <gtest/gtest.h>

#include "platform/PoseSession_Native.h"
#include "platform/Skeleton_Native.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>

namespace {

class Env {
public:
    Env(const char *name, const char *value) : mName(name) {
        const char *old = getenv(name);
        mHad = old != nullptr;
        if (mHad)
            mOld = old;
        if (value)
            setenv(name, value, 1);
        else
            unsetenv(name);
    }
    ~Env() {
        if (mHad)
            setenv(mName.c_str(), mOld.c_str(), 1);
        else
            unsetenv(mName.c_str());
    }

private:
    std::string mName, mOld;
    bool mHad;
};

std::string WriteTemp(const std::string &text) {
    const char *tmp = getenv("TMPDIR");
    std::string path = std::string((tmp && *tmp) ? tmp : "/tmp") + "/dc3_session_unit_XXXXXX";
    std::vector<char> buf(path.begin(), path.end());
    buf.push_back('\0');
    int fd = mkstemp(buf.data());
    EXPECT_GE(fd, 0);
    if (fd >= 0) {
        EXPECT_EQ(write(fd, text.data(), text.size()), (ssize_t)text.size());
        close(fd);
    }
    return buf.data();
}

NativeSkeletonProvider::PersonData Person(int id, float seed) {
    NativeSkeletonProvider::PersonData p;
    p.trackId = id;
    p.valid = true;
    for (int j = 0; j < kNumJoints; j++) {
        // values with no short decimal form, so a lossy print would show
        p.joints[j].Set(seed + j / 3.0f, -seed * 0.7f + j * 1e-7f, 2.0f + j / 7.0f);
        p.confidence[j] = (JointConfidence)(j % 3);
    }
    return p;
}

} // namespace

TEST(PoseSessionFileTest, ARecordedFrameReadsBackExactly) {
    Dc3PoseSession::Frame a;
    a.seq = 7;
    a.rnd = 12345;
    a.wall = 61.25;
    a.song = 33.333333;
    a.frameId = 4000000001u;
    a.timestamp = 1234567.0123456789; // a monotonic clock reading
    a.unlevelled = true;
    a.level = true;
    a.floorNormal.Set(0.01f, 0.99f, -0.1404f);
    a.persons = { Person(5, 0.3f), Person(-1, -1.1f) };
    a.persons[1].valid = false;
    Dc3PoseSession::Frame empty; // nobody in view
    empty.seq = 8;
    empty.frameId = 4000000002u;
    empty.song = 33.4;
    empty.floorNormal.Set(0, 1, 0);

    std::string path =
        WriteTemp("# dc3-pose-session v1\n# a comment\n" + Dc3PoseSession::Format(a)
                  + Dc3PoseSession::Format(empty));
    std::vector<Dc3PoseSession::Frame> got;
    std::string err;
    ASSERT_TRUE(Dc3PoseSession::Load(path.c_str(), got, &err)) << err;
    ASSERT_EQ(got.size(), 2u);
    const Dc3PoseSession::Frame &g = got[0];
    EXPECT_EQ(g.seq, 7u);
    EXPECT_EQ(g.rnd, 12345u);
    EXPECT_EQ(g.frameId, 4000000001u);
    EXPECT_EQ(g.timestamp, a.timestamp); // exactly
    EXPECT_TRUE(g.unlevelled);
    EXPECT_TRUE(g.level);
    EXPECT_EQ(g.floorNormal.x, a.floorNormal.x);
    EXPECT_EQ(g.floorNormal.y, a.floorNormal.y);
    EXPECT_EQ(g.floorNormal.z, a.floorNormal.z);
    ASSERT_EQ(g.persons.size(), 2u);
    for (int p = 0; p < 2; p++) {
        EXPECT_EQ(g.persons[p].trackId, a.persons[p].trackId);
        EXPECT_EQ(g.persons[p].valid, a.persons[p].valid);
        for (int j = 0; j < kNumJoints; j++) {
            EXPECT_EQ(g.persons[p].joints[j].x, a.persons[p].joints[j].x) << p << " " << j;
            EXPECT_EQ(g.persons[p].joints[j].y, a.persons[p].joints[j].y) << p << " " << j;
            EXPECT_EQ(g.persons[p].joints[j].z, a.persons[p].joints[j].z) << p << " " << j;
            EXPECT_EQ(g.persons[p].confidence[j], a.persons[p].confidence[j]) << p << " " << j;
        }
    }
    EXPECT_TRUE(got[1].persons.empty());
    EXPECT_FALSE(got[1].unlevelled);
    unlink(path.c_str());
}

TEST(PoseSessionFileTest, AFileItCannotTrustIsRefusedAndSaysWhere) {
    std::vector<Dc3PoseSession::Frame> got;
    std::string err;
    std::string frame = Dc3PoseSession::Format(Dc3PoseSession::Frame());

    std::string noHeader = WriteTemp(frame);
    EXPECT_FALSE(Dc3PoseSession::Load(noHeader.c_str(), got, &err));
    EXPECT_NE(err.find("header"), std::string::npos) << err;

    Dc3PoseSession::Frame one;
    one.persons = { Person(3, 0.0f) };
    std::string good = Dc3PoseSession::Format(one);
    std::string shortP = good.substr(0, good.rfind(' ')); // last confidence cut off
    shortP = shortP.substr(0, shortP.rfind(' ')) + "\n";  // and the last z
    std::string damaged = WriteTemp("# dc3-pose-session v1\n" + shortP);
    err.clear();
    EXPECT_FALSE(Dc3PoseSession::Load(damaged.c_str(), got, &err));
    EXPECT_NE(err.find("line 3"), std::string::npos) << err;

    std::string orphan = WriteTemp("# dc3-pose-session v1\nP 1 1 0 0 0 2\n");
    err.clear();
    EXPECT_FALSE(Dc3PoseSession::Load(orphan.c_str(), got, &err));
    EXPECT_NE(err.find("without a frame"), std::string::npos) << err;

    EXPECT_FALSE(Dc3PoseSession::Load("/nonexistent/x.dc3session", got, &err));
    unlink(noHeader.c_str());
    unlink(damaged.c_str());
    unlink(orphan.c_str());
}

TEST(PoseSessionFileTest, DueIndexIsTheLatestFrameAtOrBeforeTheSongTime) {
    std::vector<Dc3PoseSession::Frame> f(4);
    f[0].song = 1.0;
    f[1].song = 1.033;
    f[2].song = 1.067;
    f[3].song = 2.0;
    EXPECT_EQ(Dc3PoseSession::DueIndex(f, 0.5), -1);
    EXPECT_EQ(Dc3PoseSession::DueIndex(f, 1.0), 0);
    EXPECT_EQ(Dc3PoseSession::DueIndex(f, 1.05), 1);
    EXPECT_EQ(Dc3PoseSession::DueIndex(f, 1.067), 2);
    EXPECT_EQ(Dc3PoseSession::DueIndex(f, 1.9), 2);
    EXPECT_EQ(Dc3PoseSession::DueIndex(f, 50.0), 3);
    EXPECT_EQ(Dc3PoseSession::DueIndex({}, 1.0), -1);
}

TEST(PoseServerCommandTest, CameraByDefaultVideoAndInterpreterWhenAsked) {
    Env v("DC3_POSE_VIDEO", nullptr), py("DC3_POSE_PYTHON", nullptr), h("DC3_POSE_HFOV", nullptr),
        fr("DC3_CAMERA_FRAMES", nullptr);
    using Cmd = std::vector<std::string>;
    // argv[0] is ChoosePosePython's answer (PosePythonChoiceTest covers it).
    const std::string py0 = NativeSkeletonProvider::ChoosePosePython().path;
    Cmd cam = NativeSkeletonProvider::ServerCommand("/t/pose_server.py", "/s.sock", "/m.task", 2);
    EXPECT_EQ(cam, (Cmd{ py0, "/t/pose_server.py", "--socket", "/s.sock", "--model", "/m.task",
                         "--camera", "2", "--frames", "320x240" }));
    Cmd autoCam = NativeSkeletonProvider::ServerCommand("/t/pose_server.py", "/s.sock", "/m.task",
                                                        std::string("auto"));
    EXPECT_EQ(autoCam[7], "auto");
    Cmd byId = NativeSkeletonProvider::ServerCommand("/t/pose_server.py", "/s.sock", "/m.task",
                                                     std::string("usb-Cam-video-index0"));
    EXPECT_EQ(byId[7], "usb-Cam-video-index0");
    {
        Env v2("DC3_POSE_VIDEO", "/clips/dance.mp4"), py2("DC3_POSE_PYTHON", "/venv/bin/python3"),
            h2("DC3_POSE_HFOV", "70"), fr2("DC3_CAMERA_FRAMES", "off");
        Cmd vid = NativeSkeletonProvider::ServerCommand("/t/pose_server.py", "/s.sock", "/m.task", 2);
        EXPECT_EQ(vid, (Cmd{ "/venv/bin/python3", "/t/pose_server.py", "--socket", "/s.sock",
                             "--model", "/m.task", "--video", "/clips/dance.mp4", "--loop",
                             "--hfov", "70" }));
    }
}

// ---- Which Python the game runs the pose server under ----------------------
// A directory of fake interpreters, each a shell script printing its name, so
// the test can run the chosen one and see which it was.
class FakeBox {
public:
    FakeBox() {
        const char *tmp = getenv("TMPDIR");
        std::string t = std::string((tmp && *tmp) ? tmp : "/tmp") + "/dc3_posepy_XXXXXX";
        std::vector<char> buf(t.begin(), t.end());
        buf.push_back('\0');
        root = mkdtemp(buf.data());
        mMade.push_back(root);
        home = root + "/home";
        tree = root + "/tree";
        main = root + "/main";
        Python(home + "/.local/share/dc3/pose-venv", "home-venv");
        Python(root + "/custom-venv", "custom-venv");
        Python(tree + "/.venv", "tree-venv");
        Python(main + "/.venv", "main-venv");
        Write(tree + "/.git", "gitdir: " + main + "/.git/worktrees/tree\n");
    }
    ~FakeBox() {
        // Exact paths only, newest first: no recursive delete.
        for (auto it = mMade.rbegin(); it != mMade.rend(); ++it)
            if (unlink(it->c_str()) != 0)
                rmdir(it->c_str());
    }

    static std::string Ran(const std::string &python) {
        FILE *p = popen((python + " 2>/dev/null").c_str(), "r");
        if (!p)
            return "?";
        char out[64] = {};
        if (!fgets(out, sizeof(out), p))
            out[0] = '\0';
        pclose(p);
        std::string s(out);
        while (!s.empty() && s.back() == '\n')
            s.pop_back();
        return s;
    }
    void Remove(const std::string &venv) { unlink((venv + "/bin/python3").c_str()); }

    std::string root, home, tree, main;

private:
    std::vector<std::string> mMade;
    void Mkdirs(const std::string &dir) {
        if (dir.size() <= root.size())
            return;
        Mkdirs(dir.substr(0, dir.rfind('/')));
        if (mkdir(dir.c_str(), 0755) == 0)
            mMade.push_back(dir);
    }
    void Write(const std::string &path, const std::string &text) {
        Mkdirs(path.substr(0, path.rfind('/')));
        mMade.push_back(path);
        FILE *f = fopen(path.c_str(), "w");
        ASSERT_NE(f, nullptr) << path;
        fputs(text.c_str(), f);
        fclose(f);
    }
    void Python(const std::string &venv, const char *name) {
        Write(venv + "/bin/python3", std::string("#!/bin/sh\necho ") + name + "\n");
        chmod((venv + "/bin/python3").c_str(), 0755);
    }
};

TEST(PosePythonChoiceTest, EnvThenPlaytestVenvThenCheckoutVenvThenPath) {
    using P = NativeSkeletonProvider;
    FakeBox b;
    Env py("DC3_POSE_PYTHON", nullptr), venv("DC3_POSE_VENV", nullptr);
    auto ran = [&](const char *home = nullptr) {
        return FakeBox::Ran(P::ChoosePosePython(b.tree, home ? home : b.home.c_str()).path);
    };
    {
        Env e("DC3_POSE_PYTHON", (b.root + "/custom-venv/bin/python3").c_str());
        EXPECT_EQ(ran(), "custom-venv");
        EXPECT_EQ(P::ChoosePosePython(b.tree, b.home.c_str()).why, "DC3_POSE_PYTHON");
    }
    EXPECT_EQ(ran(), "home-venv");
    EXPECT_NE(P::ChoosePosePython(b.tree, b.home.c_str()).why.find("playtest venv"),
              std::string::npos);
    {
        Env e("DC3_POSE_VENV", (b.root + "/custom-venv").c_str());
        EXPECT_EQ(ran(), "custom-venv");
    }
    {
        Env e("DC3_POSE_VENV", (b.root + "/no-such-venv").c_str());
        EXPECT_EQ(ran(), "tree-venv"); // DC3_POSE_VENV replaces the default, as in play.sh
    }
    b.Remove(b.home + "/.local/share/dc3/pose-venv");
    EXPECT_EQ(ran(), "tree-venv");
    b.Remove(b.tree + "/.venv");
    EXPECT_EQ(ran(), "main-venv");
    EXPECT_NE(P::ChoosePosePython(b.tree, b.home.c_str()).why.find("worktree"), std::string::npos);
    b.Remove(b.main + "/.venv");
    P::PythonChoice none = P::ChoosePosePython(b.tree, b.home.c_str());
    EXPECT_EQ(none.path, "python3");
    EXPECT_NE(none.why.find("PATH"), std::string::npos);
    EXPECT_EQ(P::ChoosePosePython(b.tree, nullptr).path, "python3"); // no $HOME at all
}

TEST(PosePythonChoiceTest, AFileThatCannotRunIsSkipped) {
    using P = NativeSkeletonProvider;
    FakeBox b;
    Env py("DC3_POSE_PYTHON", nullptr), venv("DC3_POSE_VENV", nullptr);
    chmod((b.home + "/.local/share/dc3/pose-venv/bin/python3").c_str(), 0644);
    EXPECT_EQ(FakeBox::Ran(P::ChoosePosePython(b.tree, b.home.c_str()).path), "tree-venv");
}

TEST(PosePythonChoiceTest, TheMissingPackagesNoticeNamesTheInterpreterAndTheFix) {
    using P = NativeSkeletonProvider;
    std::string t = P::DescribeServerExit(P::kExitNoDeps << 8, "auto", "/v/bin/python3").text;
    EXPECT_NE(t.find("/v/bin/python3"), std::string::npos) << t;
    EXPECT_NE(t.find("scripts/playtest/bootstrap.sh --fix"), std::string::npos) << t;
    t = P::DescribeServerExit(P::kExitExecFailed << 8, "auto", "/v/bin/python3").text;
    EXPECT_NE(t.find("/v/bin/python3"), std::string::npos) << t;
}
