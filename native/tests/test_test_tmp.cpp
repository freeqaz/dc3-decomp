// np-tmpleak: the test temp policy in test_helpers.h, end to end.
//
// /tmp filled on 2026-11-15 (EDQUOT on every gate): mkdtemp dirs nobody
// removed, per-test logs written on every run, and 5,363 milo-tests-<pid>-<n>
// scratch dirs whose atexit cleanup read an already-destroyed std::string.
// These tests drive the real mechanism -- a child milo-tests process, a real
// gtest pass or failure, a real process exit -- rather than a seam, because
// the scratch-dir leak lived exactly in the part a seam would skip (what is
// still alive when the exit hooks run).
//
// Sabotages, each red here (np-tmpleak, 2026-11-15): RemoveEntry doing nothing
// (PassingRun); the scratch dir not registered (PassingRun); KeepReason
// ignoring a failure (FailingRun); TestTmpKeepAll() always false (KeepSwitch);
// the sweep's kill(pid, 0) check removed (Sweep).

#include "test_helpers.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

bool Exists(const std::string &p) {
    struct stat st;
    return lstat(p.c_str(), &st) == 0;
}

bool Touch(const std::string &p) {
    std::ofstream f(p);
    f << "x\n";
    return f.good();
}

std::string SelfExe() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return std::string();
    buf[n] = '\0';
    return buf;
}

std::vector<std::string> ReadLines(const std::string &path) {
    std::vector<std::string> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line))
        if (!line.empty())
            out.push_back(line);
    return out;
}

struct ChildRun {
    int exitCode = -1;
    std::string output;
    std::vector<std::string> paths; // what the child made: dir, log, scratch, scoped dir
};

// Runs this binary's TmpdirIsTheBase as the child body, with $TMPDIR = `base`.
ChildRun RunChild(const std::string &base, const std::string &report, const char *mode,
                  const char *extraEnv = "") {
    ChildRun r;
    unlink(report.c_str());
    std::ostringstream cmd;
    cmd << "env -u DC3_KEEP_TEST_TMP -u MILO_TEST_KEEP_SCRATCH TMPDIR=" << base
        << " DC3_TEST_TMP_CHILD=" << mode << " DC3_TEST_TMP_REPORT=" << report << " "
        << extraEnv << " timeout 60 " << SelfExe()
        << " --gtest_filter=TestTmpHelper.TmpdirIsTheBase 2>&1";
    FILE *p = popen(cmd.str().c_str(), "r");
    if (!p)
        return r;
    char buf[4096];
    while (fgets(buf, sizeof(buf), p))
        r.output += buf;
    int status = pclose(p);
    r.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    r.paths = ReadLines(report);
    return r;
}

// The child body: make one of everything, say where, then pass or fail.
void ChildBody(const std::string &mode) {
    const char *report = getenv("DC3_TEST_TMP_REPORT");
    ASSERT_NE(report, nullptr);
    std::ofstream out(report);

    std::string dir = MakeTestTempDir("dc3-tt");
    ASSERT_FALSE(dir.empty());
    ASSERT_TRUE(Touch(dir + "/engine.log"));
    out << dir << "\n";

    std::string log = TestLogPath("tt");
    ASSERT_TRUE(Touch(log));
    out << log << "\n";

    const std::string &scratch = TestScratchDir();
    ASSERT_FALSE(scratch.empty());
    ASSERT_TRUE(Touch(scratch + "/fixture.milo_xbox"));
    out << scratch << "\n";

    {
        ScopedTestDir scoped("dc3-ts");
        ASSERT_TRUE(scoped.ok());
        ASSERT_TRUE(Touch(scoped.path() + "/shot.png"));
        out << scoped.path() << "\n";
        out.flush();
        if (mode == "fail")
            ADD_FAILURE() << "deliberate failure (DC3_TEST_TMP_CHILD=fail)";
    }
}

} // namespace

// TestTempBase is $TMPDIR when set and non-empty, else the fallback; and a
// dir made with $TMPDIR set lands under it, named with this pid.  (With
// DC3_TEST_TMP_CHILD set, this is the child body of the tests below instead.)
TEST(TestTmpHelper, TmpdirIsTheBase) {
    if (const char *mode = getenv("DC3_TEST_TMP_CHILD")) {
        ChildBody(mode);
        return;
    }
    const char *saved = getenv("TMPDIR");
    const std::string old = saved ? saved : "";
    ScopedTestDir base("dc3-tb");
    ASSERT_TRUE(base.ok());

    setenv("TMPDIR", (base.path() + "//").c_str(), 1);
    EXPECT_EQ(TestTempBase("/fb"), base.path());
    std::string d = MakeTestTempDir("dc3-tb-in");
    EXPECT_EQ(d.rfind(base.path() + "/dc3-tb-in-p" + std::to_string((int)getpid()) + "-", 0), 0u)
        << d;
    std::string f = TestTempFile("tb", ".txt");
    EXPECT_EQ(f, base.path() + "/tb-p" + std::to_string((int)getpid()) + ".txt");
    setenv("TMPDIR", "", 1);
    EXPECT_EQ(TestTempBase("/fb/"), "/fb");
    unsetenv("TMPDIR");
    EXPECT_EQ(TestTempBase("/fb"), "/fb");
    if (saved)
        setenv("TMPDIR", old.c_str(), 1);
}

class TestTmpChildTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(mHome.ok());
        mBase = mHome.path(); // the child's $TMPDIR
        mReport = mHome.path() + "/report.txt";
        ASSERT_FALSE(SelfExe().empty());
    }
    // What a keeping child left outside mHome -- a $TMPDIR too long for
    // kTestTempDirMaxLen falls back to /tmp -- goes when this test passes.
    void TearDown() override {
        if (HasFailure())
            return;
        for (const std::string &p : ReadLines(mReport)) {
            const std::string name = p.substr(p.rfind('/') + 1);
            bool ours = name.rfind("dc3-tt-p", 0) == 0 || name.rfind("dc3-ts-p", 0) == 0 ||
                        name.rfind("milo-tests-", 0) == 0 || name.rfind("tt_", 0) == 0;
            if (ours && p.rfind(mHome.path() + "/", 0) != 0 && p.find("/..") == std::string::npos) {
                std::error_code ec;
                std::filesystem::remove_all(p, ec);
            }
        }
    }
    ScopedTestDir mHome{"dc3-tc"};
    std::string mBase, mReport;
};

// A passing run removes its dir, its log, its scratch dir and its scoped dir.
// The scratch dir is the one that used to survive every normal exit.
TEST_F(TestTmpChildTest, PassingRunRemovesEverything) {
    ChildRun r = RunChild(mBase, mReport, "pass");
    ASSERT_EQ(r.exitCode, 0) << r.output;
    ASSERT_EQ(r.paths.size(), 4u) << r.output;
    for (const std::string &p : r.paths)
        EXPECT_FALSE(Exists(p)) << "a passing run left " << p << "\n" << r.output;
    EXPECT_EQ(r.output.find("[test-tmp] kept"), std::string::npos) << r.output;
}

// A failing run keeps all four and prints each path.
TEST_F(TestTmpChildTest, FailingRunKeepsEverythingAndSaysWhere) {
    ChildRun r = RunChild(mBase, mReport, "fail");
    ASSERT_NE(r.exitCode, 0) << r.output;
    ASSERT_EQ(r.paths.size(), 4u) << r.output;
    for (const std::string &p : r.paths) {
        EXPECT_TRUE(Exists(p)) << "a failing run removed " << p << "\n" << r.output;
        EXPECT_NE(r.output.find("[test-tmp] kept (this "), std::string::npos) << r.output;
        EXPECT_NE(r.output.find(p), std::string::npos) << "not printed: " << p << "\n" << r.output;
    }
}

// DC3_KEEP_TEST_TMP=1 keeps a passing run's files.
TEST_F(TestTmpChildTest, KeepSwitchKeepsAPassingRun) {
    ChildRun r = RunChild(mBase, mReport, "pass", "DC3_KEEP_TEST_TMP=1");
    ASSERT_EQ(r.exitCode, 0) << r.output;
    ASSERT_EQ(r.paths.size(), 4u) << r.output;
    for (const std::string &p : r.paths) {
        EXPECT_TRUE(Exists(p)) << "DC3_KEEP_TEST_TMP=1 removed " << p << "\n" << r.output;
        EXPECT_NE(r.output.find(p), std::string::npos) << "not printed: " << p;
    }
}

// The sweep removes a dead owner's old dirs and nothing else.
TEST(TestTmpHelper, SweepRemovesOnlyDeadOwnersOldDirs) {
    ScopedTestDir base("dc3-sw");
    ASSERT_TRUE(base.ok());
    const std::string B = base.path();

    pid_t dead = fork();
    ASSERT_GE(dead, 0);
    if (dead == 0)
        _exit(0);
    ASSERT_EQ(waitpid(dead, nullptr, 0), dead);
    ASSERT_TRUE(kill(dead, 0) != 0 && errno == ESRCH) << "pid " << dead << " was reused at once";

    // A live owner that is neither this process nor its parent: a child
    // blocked reading a pipe until we close it.
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    pid_t live = fork();
    ASSERT_GE(live, 0);
    if (live == 0) {
        close(fds[1]);
        char c;
        while (read(fds[0], &c, 1) > 0) {
        }
        _exit(0);
    }
    close(fds[0]);

    const std::string D = std::to_string((int)dead), L = std::to_string((int)live);
    struct Plant {
        std::string name;
        bool dir;
        bool old;
        bool swept;
    };
    const std::vector<Plant> plants = {
        {"milo-tests-" + D + "-0", true, true, true},
        {"dc3-sw-x-p" + D + "-AbC123", true, true, true},
        {"milo-tests-" + L + "-0", true, true, false},      // live owner
        {"dc3-sw-x-p" + L + "-AbC123", true, true, false},  // live owner
        {"milo-tests-" + D + "-1", true, false, false},     // dead, but touched recently
        {"dc3-legacy-AbC123", true, true, false},           // no pid in the name
        {"dc3-sw-x-p" + D + "-AbC12", true, true, false},   // not a mkdtemp suffix
        {"milo-tests-" + D + "-2", false, true, false},     // a file, not a dir
        {"dc3-sw-x-p" + D + ".log", false, true, false},    // a log file
    };
    struct timeval old[2];
    gettimeofday(&old[0], nullptr);
    old[0].tv_sec -= 2 * 3600;
    old[1] = old[0];
    for (const Plant &p : plants) {
        std::string path = B + "/" + p.name;
        if (p.dir) {
            ASSERT_EQ(mkdir(path.c_str(), 0700), 0) << path;
            ASSERT_TRUE(Touch(path + "/inside"));
        } else {
            ASSERT_TRUE(Touch(path));
        }
        if (p.old)
            ASSERT_EQ(utimes(path.c_str(), old), 0) << path;
    }
    // A symlink with a dead owner's name, to a dir that must survive.
    const std::string target = B + "/target";
    ASSERT_EQ(mkdir(target.c_str(), 0700), 0);
    ASSERT_TRUE(Touch(target + "/keep"));
    const std::string link = B + "/dc3-sw-x-p" + D + "-ZzZ999";
    ASSERT_EQ(symlink(target.c_str(), link.c_str()), 0);
    lutimes(link.c_str(), old);

    int removed = SweepDeadTestTmp(B, 3600);

    close(fds[1]);
    waitpid(live, nullptr, 0);

    EXPECT_EQ(removed, 2);
    for (const Plant &p : plants)
        EXPECT_EQ(Exists(B + "/" + p.name), !p.swept)
            << p.name << (p.swept ? " should have been swept" : " should have been left alone");
    EXPECT_TRUE(Exists(link));
    EXPECT_TRUE(Exists(target + "/keep"));
}
