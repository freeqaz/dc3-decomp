// PLAN H-107: an open that finds no file is silent, as in the image.
//
// The image's AsyncFileWin::_OpenAsync (src/system/os/AsyncFile_Win.cpp) fails
// the open without a word when CreateFileA reports ERROR_FILE_NOT_FOUND (2),
// ERROR_PATH_NOT_FOUND (3) or ERROR_NOT_READY (0x15); only other errors reach
// ReadError. Callers lean on that: FileExists probes through it, Locale::Init
// asks for devkit:\locale\<lang>\locale_keep.dta, HamPlayerData reads
// devkit:\dancers.dta with DataReadFile(file, false) -- optional files, whose
// absence the caller handles (or reports itself when it matters).
//
// The engine's AsyncFileNative logged every failed fopen, so the web build's
// console carried "AsyncFile: failed to open 'devkit:/locale/eng/
// locale_keep.dta'" and two of devkit:/dancers.dta as errors on every boot
// (archive/np-gfx-2026-11-14/lanes/sharp/item10), for probes the original
// makes in silence. Not-found (ENOENT, ENOTDIR) is now silent; a file that is
// there and still cannot be opened keeps its log line, which the control below
// holds.

#include "test_helpers.h"
#include "test_tmp.h"

#include "obj/DataFile.h"
#include "os/File.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

void NativeSetOverlayDir(const char *dir);
const char *NativeGetOverlayDir();

namespace {

const char kOpenLog[] = "AsyncFile: failed to open";

class AsyncFileNotFoundTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!EnsureEngineInit()) return;
        mDir = MakeTestTempDir("dc3-asyncfile-notfound");
        ASSERT_FALSE(mDir.empty());
        // mkdtemp's suffix has capitals and FileNormalizePath lower-cases a
        // path that is not under a host root (H-17/H-23), so make the fixture
        // one: its files then open by their real names, and "not found"
        // below means not found, not mis-spelled.
        mSavedOverlay = NativeGetOverlayDir();
        mSaved = true;
        NativeSetOverlayDir(mDir.c_str());
    }

    void TearDown() override {
        if (mSaved) NativeSetOverlayDir(mSavedOverlay.c_str());
        for (auto it = mMade.rbegin(); it != mMade.rend(); ++it) {
            chmod(it->c_str(), 0644);
            unlink(it->c_str());
        }
    }

    std::string MakeFile(const char *name, const char *text) {
        std::string path = mDir + "/" + name;
        std::ofstream(path) << text;
        mMade.push_back(path);
        return path;
    }

    // FileExists(path) with stderr captured: what the probe printed.
    static std::string ProbeOutput(const std::string &path, bool *exists) {
        ::testing::internal::CaptureStderr();
        *exists = FileExists(path.c_str(), 0, nullptr);
        return ::testing::internal::GetCapturedStderr();
    }

    std::string mDir, mSavedOverlay;
    std::vector<std::string> mMade;
    bool mSaved = false;
};

TEST_F(AsyncFileNotFoundTest, MissingFileProbeIsSilent) {
    bool exists = true;
    std::string out = ProbeOutput(mDir + "/absent.dta", &exists);
    EXPECT_FALSE(exists);
    EXPECT_EQ(out.find(kOpenLog), std::string::npos) << out;
}

TEST_F(AsyncFileNotFoundTest, MissingDirectoryProbeIsSilent) {
    bool exists = true;
    std::string out = ProbeOutput(mDir + "/nodir/absent.dta", &exists);  // ENOENT
    EXPECT_FALSE(exists);
    EXPECT_EQ(out.find(kOpenLog), std::string::npos) << out;

    MakeFile("plain.dta", "(plain)\n");
    out = ProbeOutput(mDir + "/plain.dta/absent.dta", &exists);  // ENOTDIR
    EXPECT_FALSE(exists);
    EXPECT_EQ(out.find(kOpenLog), std::string::npos) << out;
}

// The two probes the web console showed, by the names the game uses. On
// desktop these are not local files (File_Native.cpp FileIsLocal), so they go
// to the archive and never reached AsyncFileNative; the web build calls every
// file local (File_Web.cpp), which is why only its console had them. Kept as
// the by-name record of what must stay silent on both.
TEST_F(AsyncFileNotFoundTest, DevkitDriveProbesAreSilent) {
    bool exists = true;
    std::string out = ProbeOutput("devkit:\\locale\\eng\\locale_keep.dta", &exists);
    EXPECT_FALSE(exists);
    EXPECT_EQ(out.find(kOpenLog), std::string::npos) << out;

    ::testing::internal::CaptureStderr();
    DataArray *dancers = DataReadFile("devkit:\\dancers.dta", false);
    out = ::testing::internal::GetCapturedStderr();
    EXPECT_EQ(dancers, nullptr);
    if (dancers) dancers->Release();
    EXPECT_EQ(out.find(kOpenLog), std::string::npos) << out;
}

// Controls: a file that is there opens, and one that is there but cannot be
// opened is still reported (the image's ReadError path), with its path.
TEST_F(AsyncFileNotFoundTest, ExistingFileOpens) {
    bool exists = false;
    std::string out = ProbeOutput(MakeFile("present.dta", "(present)\n"), &exists);
    EXPECT_TRUE(exists);
    EXPECT_EQ(out.find(kOpenLog), std::string::npos) << out;
}

TEST_F(AsyncFileNotFoundTest, UnreadableFileIsStillLogged) {
    if (geteuid() == 0) GTEST_SKIP() << "root opens a mode-000 file";
    std::string path = MakeFile("locked.dta", "(locked)\n");
    ASSERT_EQ(chmod(path.c_str(), 0), 0);
    bool exists = true;
    std::string out = ProbeOutput(path, &exists);
    EXPECT_FALSE(exists);
    EXPECT_NE(out.find(kOpenLog), std::string::npos)
        << "a permission failure must still be logged; got: " << out;
    EXPECT_NE(out.find("locked.dta"), std::string::npos) << out;
}

} // namespace
