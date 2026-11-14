// Paths that only worked when the working directory was the repo root
// (PLAN P-10, G-15).
//
// The native binaries run from three different working directories: the repo
// root (an old launcher habit), native/build (a windowed boot, the dogfood
// driver) and orig-assets (ctest's WORKING_DIRECTORY).  Two defaults resolved
// against the cwd and so only worked from the first:
//
//   P-10  the pose model, "native/models/pose_landmarker_full.task": a boot
//         from native/build spawned pose_server.py, which exited 5 ("model
//         file not found") and left "Pose model missing" on every screen.
//   G-15  every movie: FFmpegMovieImpl handed "videos/intro.bik" (and every
//         songs/*/*_prev.bik) straight to avformat_open_input, which opened it
//         relative to the cwd -- no intro, no song-select previews, no era or
//         credits movies.  Movies are game data and come out of the archive
//         like every other asset.
//
// Every test here runs its subject from a FRESH EMPTY working directory, so it
// cannot pass by accident of where milo-tests was launched.
//
// Sabotage record (each revert re-run, test watched FAIL, fix restored):
//   * GestureMgr_NativePoseModelPath() back to the literal
//     "native/models/pose_landmarker_full.task"
//       -> PoseModelDefaultIsTheTreesModel, PoseServerFindsTheDefaultModel FAIL
//   * FFmpegMovieImpl::OpenVideo back to avformat_open_input(path)
//       -> IntroMovieOpensFromAnyCwd, SongPreviewOpensFromAnyCwd FAIL
//   * NativeDetectOverlayDir without the tree-first probe
//       -> OverlayIsThisTreesNativeDta FAIL

#include <gtest/gtest.h>

#include "test_helpers.h"
#include "platform/ExecutablePath.h"
#include "platform/FFmpegMovieImpl.h"
#include "platform/Skeleton_Native.h"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

const char *NativeGetOverlayDir();

namespace {

bool IsFile(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string RealPath(const std::string &path) {
    char buf[PATH_MAX];
    return realpath(path.c_str(), buf) ? std::string(buf) : std::string();
}

std::string MakeTempDir(const char *tag) {
    const char *tmp = getenv("TMPDIR");
    std::string dir = std::string((tmp && *tmp) ? tmp : "/tmp") + "/" + tag + "_XXXXXX";
    std::vector<char> buf(dir.begin(), dir.end());
    buf.push_back('\0');
    if (!mkdtemp(buf.data()))
        return std::string();
    return std::string(buf.data());
}

// chdir into a fresh empty directory for the scope; restore the old cwd after.
class ScopedEmptyCwd {
public:
    explicit ScopedEmptyCwd(const char *tag) {
        char old[PATH_MAX];
        if (getcwd(old, sizeof(old)))
            mOld = old;
        mDir = MakeTempDir(tag);
        mOk = !mOld.empty() && !mDir.empty() && chdir(mDir.c_str()) == 0;
    }
    ~ScopedEmptyCwd() {
        if (!mOld.empty() && chdir(mOld.c_str()) != 0)
            perror("ScopedEmptyCwd: restoring cwd");
        if (!mDir.empty())
            rmdir(mDir.c_str());
    }
    bool ok() const { return mOk; }
    const std::string &dir() const { return mDir; }

private:
    std::string mOld, mDir;
    bool mOk = false;
};

class ScopedEnvVar {
public:
    ScopedEnvVar(const char *name, const char *value) : mName(name) {
        const char *old = getenv(name);
        if (old) {
            mHad = true;
            mOld = old;
        }
        if (value)
            setenv(name, value, 1);
        else
            unsetenv(name);
    }
    ~ScopedEnvVar() {
        if (mHad)
            setenv(mName.c_str(), mOld.c_str(), 1);
        else
            unsetenv(mName.c_str());
    }

private:
    std::string mName, mOld;
    bool mHad = false;
};

// The tree the test binary was built in, located independently of
// NativeTreeDir(): CMake's own idea of the source tree. Not __FILE__: under
// ccache (docs/native/BUILD_CACHE.md) the compiler is handed a path relative
// to the build dir, and this test deliberately runs from an unrelated cwd.
std::string ExpectedNativeDir() {
    return RealPath(std::string(DC3_DECOMP_SRC_DIR) + "/../native");
}

} // namespace

// ---------------------------------------------------------------------------
// The helper itself
// ---------------------------------------------------------------------------

TEST(NativeTreePaths, TreeDirIsTheBuildDirsParentWhateverTheCwd) {
    ScopedEmptyCwd cwd("dc3_tree_cwd");
    ASSERT_TRUE(cwd.ok());
    std::string tree = NativeTreeDir();
    ASSERT_FALSE(tree.empty()) << "executable path unknown on a desktop build";
    EXPECT_EQ(tree[0], '/');
    // It is a native/ dir: it ships the pose server script.
    EXPECT_TRUE(IsFile(tree + "/scripts/pose_server.py")) << tree;
    std::string expected = ExpectedNativeDir();
    if (!expected.empty())
        EXPECT_EQ(RealPath(tree), expected);
}

// ---------------------------------------------------------------------------
// P-10: the pose model
// ---------------------------------------------------------------------------

TEST(NativeTreePaths, PoseModelDefaultIsTheTreesModel) {
    ScopedEnvVar model("DC3_POSE_MODEL", nullptr);
    ScopedEmptyCwd cwd("dc3_model_cwd");
    ASSERT_TRUE(cwd.ok());

    std::string path = GestureMgr_NativePoseModelPath();
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path[0], '/') << "default model path is cwd-relative: " << path;
    EXPECT_EQ(path, NativeTreeDir() + "/models/pose_landmarker_full.task");

    // An explicit DC3_POSE_MODEL still wins, verbatim.
    ScopedEnvVar explicitModel("DC3_POSE_MODEL", "/somewhere/else.task");
    EXPECT_EQ(GestureMgr_NativePoseModelPath(), "/somewhere/else.task");
}

// End to end through the spawn: a stub python3 on PATH does what
// pose_server.py does first -- exit 5 (EXIT_NO_MODEL) unless --model names a
// file, from ITS working directory, which it inherits from the engine -- and
// then exits 3 (no camera) the way the real server does on a camera-less box.
// So "No camera" means the server got past the model; "Pose model missing" is
// the P-10 bug.  The stub also requires the script argument to exist.
TEST(NativeTreePaths, PoseServerFindsTheDefaultModel) {
    std::string tree = NativeTreeDir();
    ASSERT_FALSE(tree.empty());
    if (!IsFile(tree + "/models/pose_landmarker_full.task"))
        GTEST_SKIP() << "no " << tree << "/models/pose_landmarker_full.task in this tree";

    std::string stubDir = MakeTempDir("dc3_pose_path_stub");
    ASSERT_FALSE(stubDir.empty());
    std::string stub = stubDir + "/python3";
    std::string argLog = stubDir + "/args";
    FILE *f = fopen(stub.c_str(), "w");
    ASSERT_NE(f, nullptr);
    fprintf(f,
            "#!/bin/sh\n"
            "script=\"$1\"; shift\n"
            "model=\n"
            "while [ $# -gt 0 ]; do\n"
            "  case \"$1\" in --model) model=\"$2\"; shift;; esac\n"
            "  shift\n"
            "done\n"
            "printf '%%s\\n%%s\\n' \"$script\" \"$model\" > '%s'\n"
            "[ -f \"$script\" ] || exit 1\n"
            "[ -f \"$model\" ] || exit %d\n"
            "exit %d\n",
            argLog.c_str(), (int)NativeSkeletonProvider::kExitNoModel,
            (int)NativeSkeletonProvider::kExitNoCamera);
    fclose(f);
    chmod(stub.c_str(), 0755);

    const char *oldPath = getenv("PATH");
    std::string path = stubDir + ":" + (oldPath ? oldPath : "/usr/bin:/bin");
    ScopedEnvVar envPath("PATH", path.c_str());
    ScopedEnvVar noSpawn("DC3_POSE_NO_SPAWN", nullptr);
    ScopedEnvVar perform("DC3_POSE_PERFORM", nullptr);
    ScopedEnvVar model("DC3_POSE_MODEL", nullptr);

    std::string problem, seenScript, seenModel;
    {
        ScopedEmptyCwd cwd("dc3_pose_path_cwd");
        ASSERT_TRUE(cwd.ok());
        NativeSkeletonProvider provider;
        bool started = provider.Start(stubDir + "/never.sock",
                                      GestureMgr_NativePoseModelPath(), 0);
        EXPECT_FALSE(started); // the stub never binds the socket
        problem = provider.LastProblem().brief;
    }
    FILE *log = fopen(argLog.c_str(), "r");
    if (log) {
        char line[PATH_MAX + 2];
        if (fgets(line, sizeof(line), log)) {
            seenScript = line;
            if (!seenScript.empty() && seenScript.back() == '\n') seenScript.pop_back();
        }
        if (fgets(line, sizeof(line), log)) {
            seenModel = line;
            if (!seenModel.empty() && seenModel.back() == '\n') seenModel.pop_back();
        }
        fclose(log);
    }

    EXPECT_EQ(problem, "No camera: pose tracking off")
        << "the server did not get past its model check; model='" << seenModel
        << "' script='" << seenScript << "'";
    EXPECT_FALSE(seenModel.empty());
    if (!seenModel.empty()) EXPECT_EQ(seenModel[0], '/') << seenModel;
    EXPECT_TRUE(IsFile(seenScript)) << seenScript;

    unlink(argLog.c_str());
    unlink(stub.c_str());
    rmdir(stubDir.c_str());
}

// ---------------------------------------------------------------------------
// DTA overlay
// ---------------------------------------------------------------------------

class NativeTreeEnginePaths : public EngineTestFixture {};

// The overlay DTAs are source and belong to the tree the binary was built
// from.  "<dataDir>/../native/dta" is not that in a worktree: orig-assets is a
// symlink to the main checkout's, so "orig-assets/.." resolves to MAIN.
TEST_F(NativeTreeEnginePaths, OverlayIsThisTreesNativeDta) {
    std::string tree = NativeTreeDir();
    ASSERT_FALSE(tree.empty());
    std::string overlay = NativeGetOverlayDir();
    ASSERT_FALSE(overlay.empty());
    EXPECT_EQ(RealPath(overlay), RealPath(tree + "/dta"))
        << "overlay '" << overlay << "' is not this tree's native/dta";
}

// ---------------------------------------------------------------------------
// G-15: movies
// ---------------------------------------------------------------------------

namespace {

// Open `path` the way MoviePanel/TexMovie do (a game path), from an empty
// cwd, and decode one frame.
void ExpectMoviePlaysFromEmptyCwd(const char *path, int wantW, int wantH,
                                  float wantSeconds) {
    ScopedEmptyCwd cwd("dc3_movie_cwd");
    ASSERT_TRUE(cwd.ok());
    ASSERT_NE(access(path, F_OK), 0) << "vacuous: " << path << " exists under the cwd";

    FFmpegMovieImpl movie;
    bool ok = movie.BeginFromFile(path, 0.0f, false, false, true, false, 1, nullptr,
                                  kLoadFront);
    ASSERT_TRUE(ok) << path << " did not open from cwd " << cwd.dir();
    EXPECT_TRUE(movie.IsOpen());
    movie.SetVirtualTime(100.0f);
    EXPECT_TRUE(movie.Poll());
    EXPECT_TRUE(movie.HasDecodedFrame()) << path;
    EXPECT_EQ(movie.GetDecodedWidth(), wantW);
    EXPECT_EQ(movie.GetDecodedHeight(), wantH);
    // Splash::Poll times a movie as MsPerFrame() * NumFrames().
    EXPECT_NEAR(movie.NumFrames() * movie.MsPerFrame() / 1000.0f, wantSeconds, 0.5f)
        << movie.NumFrames() << " frames at " << movie.MsPerFrame() << " ms";
    movie.End();
    EXPECT_FALSE(movie.IsOpen());
}

} // namespace

TEST_F(NativeTreeEnginePaths, IntroMovieOpensFromAnyCwd) {
    ExpectMoviePlaysFromEmptyCwd("videos/intro.bik", 1280, 720, 82.9f); // ffprobe: 82.88 s
}

TEST_F(NativeTreeEnginePaths, SongPreviewOpensFromAnyCwd) {
    ExpectMoviePlaysFromEmptyCwd("songs/ymca/ymca_prev.bik", 256, 256, 30.0f); // ffprobe: 30.00 s
}

TEST_F(NativeTreeEnginePaths, MissingMovieFailsCleanly) {
    FFmpegMovieImpl movie;
    EXPECT_FALSE(movie.BeginFromFile("videos/no_such_movie.bik", 0.0f, false, false,
                                     true, false, 1, nullptr, kLoadFront));
    EXPECT_FALSE(movie.IsOpen());
    EXPECT_FALSE(movie.Poll()); // not open = done, as the image's Bink impl
}
