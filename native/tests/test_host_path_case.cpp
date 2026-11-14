// PLAN H-17 / H-23: a HOST directory with upper-case letters in it.
//
// The decomp's FileNormalizePath (src/system/os/File.cpp) lower-cases every
// path FileMakePath builds, which the Xbox's case-insensitive file system
// allowed. The native file layer roots paths in host directories -- the data
// dir (DC3_DATA), the native/dta overlay, the system root -- so before the
// HX_NATIVE branch there, one capital anywhere above them (/home/Alice,
// ~/tmp/np-bi-cacheB, DC3_DATA=/tmp/mindata.jeHQ) made every file under them
// unopenable: "AsyncFile: failed to open '.../mindata.jehq/...'".
//
// These tests build such a directory (a temp dir with capitals, not a
// worktree rename) and check that the host prefix keeps its spelling while the
// game-relative part keeps the original's case folding.

#include "test_helpers.h"

#include "os/File.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

void NativeSetOverlayDir(const char *dir);
const char *NativeGetOverlayDir();
void NativeSetDataDir(const char *dir);
const char *NativeGetDataDir();

namespace {

std::string Lower(std::string s) {
    for (char &c : s)
        c = (char)tolower((unsigned char)c);
    return s;
}

bool IsFile(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

class HostPathCaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        char tmpl[] = "/tmp/dc3-HostCase-XXXXXX";
        char *dir = mkdtemp(tmpl);
        ASSERT_NE(dir, nullptr);
        mTmp = dir;
        mOverlay = mTmp + "/Over.Lay";
        ASSERT_EQ(mkdir(mOverlay.c_str(), 0755), 0);
        ASSERT_EQ(mkdir((mOverlay + "/ui").c_str(), 0755), 0);
        mProbe = mOverlay + "/ui/probe.dta";
        std::ofstream(mProbe) << "(probe 1)\n";
        ASSERT_TRUE(IsFile(mProbe));
        ASSERT_NE(Lower(mOverlay), mOverlay) << "the fixture needs capitals";
        UseOverlay();
    }

    // Point the overlay at the fixture, remembering what it was. Called again
    // after an engine boot (which detects its own overlay).
    void UseOverlay() {
        if (!mSaved) {
            mSavedOverlay = NativeGetOverlayDir();
            mSavedData = NativeGetDataDir();
            mSaved = true;
        }
        NativeSetOverlayDir(mOverlay.c_str());
    }

    void TearDown() override {
        if (mSaved) {
            NativeSetOverlayDir(mSavedOverlay.c_str());
            NativeSetDataDir(mSavedData.c_str());
        }
        if (!mProbe.empty()) unlink(mProbe.c_str());
        if (!mOverlay.empty()) {
            rmdir((mOverlay + "/ui").c_str());
            rmdir(mOverlay.c_str());
        }
        if (!mTmp.empty()) rmdir(mTmp.c_str());
    }

    std::string MakePath(const char *root, const char *file) {
        char buf[256];
        return FileMakePathBuf(root, file, buf);
    }

    std::string mTmp, mOverlay, mProbe, mSavedOverlay, mSavedData;
    bool mSaved = false;
};

// The H-23 path: FileQualifiedFilename roots an overlay file in the overlay
// directory, and the result must name the file that is there.
TEST_F(HostPathCaseTest, OverlayFileQualifiesToItsHostSpelling) {
    char out[256];
    FileQualifiedFilename(out, sizeof(out), "ui/probe.dta");
    EXPECT_EQ(std::string(out), mProbe);
    EXPECT_TRUE(IsFile(out)) << "qualified to a path that does not exist: " << out;
}

// ... and the file opens and reads through the game's own file layer.
TEST_F(HostPathCaseTest, OverlayFileOpensThroughNewFile) {
    // The boot sets its own overlay and data dirs: restore ours afterwards,
    // and put the engine's back at TearDown.
    NativeSetOverlayDir(mSavedOverlay.c_str());
    mSaved = false;
    if (!EnsureEngineInit()) return;
    UseOverlay();
    File *f = NewFile("ui/probe.dta", 2);
    ASSERT_NE(f, nullptr) << "NewFile could not open the overlay file under " << mOverlay;
    char buf[16] = {};
    int n = f->Read(buf, 9);
    delete f;
    EXPECT_EQ(n, 9);
    EXPECT_STREQ(buf, "(probe 1)");
}

// The game-relative part still folds case, as on the Xbox: data and overlay
// files are named in lower case on disk.
TEST_F(HostPathCaseTest, GameRelativePartIsStillLowerCased) {
    EXPECT_EQ(MakePath(mOverlay.c_str(), "UI\\Probe.DTA"), mProbe);
}

// A host prefix that an earlier FileMakePath already lower-cased gets its
// spelling back (FileMakePath(FileSystemRoot(), ...) output fed into another
// FileMakePath is the common shape).
TEST_F(HostPathCaseTest, AnAlreadyLoweredHostPrefixIsRestored) {
    std::string lowered = Lower(mProbe);
    ASSERT_FALSE(IsFile(lowered));
    EXPECT_EQ(MakePath(".", lowered.c_str()), mProbe);
}

// The data directory is a host root too (H-17: DC3_DATA=/tmp/mindata.jeHQ).
TEST_F(HostPathCaseTest, DataDirIsAHostRoot) {
    NativeSetOverlayDir("");
    NativeSetDataDir(mOverlay.c_str());
    char out[256];
    FileQualifiedFilename(out, sizeof(out), "UI/probe.dta");
    EXPECT_EQ(std::string(out), mProbe);
}

// Only whole components of a host root are kept: a sibling directory whose
// name merely starts with the root's last component is game/other data and is
// folded as before; so is any absolute path outside every host root.
TEST_F(HostPathCaseTest, OnlyWholeComponentsOfAHostRootAreKept) {
    const std::string sibling = mOverlay + "er/UI/X.dta";
    EXPECT_EQ(MakePath(".", sibling.c_str()), Lower(sibling));
    EXPECT_EQ(MakePath(".", "/Not/A/Host/Root.DTA"), "/not/a/host/root.dta");
}

} // namespace
