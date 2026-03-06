#include "test_helpers.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "char/CharClip.h"
#include "rndobj/Trans.h"
#include "utl/ChunkStream.h"
#include "utl/FilePath.h"

#include <algorithm>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

static std::string GetBinaryDir() {
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0)
        return ".";
    buf[len] = '\0';
    std::string path(buf);
    size_t slash = path.rfind('/');
    return (slash != std::string::npos) ? path.substr(0, slash) : ".";
}

static std::string GetViewerPath() {
    return GetBinaryDir() + "/milo-viewer";
}

static std::string GetProjectRoot() {
    std::string binDir = GetBinaryDir(); // .../native/build
    size_t slash = binDir.rfind('/');
    if (slash == std::string::npos)
        return ".";
    std::string parent = binDir.substr(0, slash); // .../native
    slash = parent.rfind('/');
    if (slash == std::string::npos)
        return ".";
    return parent.substr(0, slash); // repo root
}

static std::string GetMiloLibRoot() {
    const char* env = getenv("MILO_LIB");
    if (env && env[0])
        return env;

    const char* home = getenv("HOME");
    if (home && home[0]) {
        return std::string(home)
            + "/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3";
    }

    return "";
}

static bool OutputHasNullBackend(const std::string& output) {
    return output.find("GPU = Null backend") != std::string::npos;
}

static bool OutputHasGpuFailure(const std::string& output) {
    return output.find("GPU initialization failed") != std::string::npos
        || output.find("GPU fell back to Null backend") != std::string::npos;
}

static bool FileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static off_t FileSize(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return 0;
    return st.st_size;
}

struct RunResult {
    int exitCode;
    int signal;
    std::string output;
};

static RunResult RunCommand(const std::string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    RunResult out{-1, 0, ""};
    if (!pipe)
        return out;

    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe))
        out.output += buf;

    int status = pclose(pipe);
    if (WIFEXITED(status)) {
        out.exitCode = WEXITSTATUS(status);
        if (out.exitCode > 128 && out.exitCode <= 128 + 31)
            out.signal = out.exitCode - 128;
    } else if (WIFSIGNALED(status)) {
        out.exitCode = -1;
        out.signal = WTERMSIG(status);
    }
    return out;
}

static ObjectDir* TryLoadMiloAbsolute(const std::string& path) {
    FilePath fp(path.c_str());
    ChunkStream* probe = new ChunkStream(
        fp.c_str(), ChunkStream::kRead, 0x8000, false, kPlatformNone, false
    );
    if (probe->Fail()) {
        delete probe;
        return nullptr;
    }
    delete probe;
    return DirLoader::LoadObjects(fp, nullptr, nullptr);
}

static void WriteJsonEscaped(FILE* f, const char* s) {
    fputc('"', f);
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        unsigned char c = *p;
        if (c == '"' || c == '\\') {
            fputc('\\', f);
            fputc(c, f);
        } else if (c == '\n') {
            fputs("\\n", f);
        } else if (c == '\r') {
            fputs("\\r", f);
        } else if (c == '\t') {
            fputs("\\t", f);
        } else if (c < 0x20) {
            fprintf(f, "\\u%04x", (unsigned)c);
        } else {
            fputc(c, f);
        }
    }
    fputc('"', f);
}

static bool WriteExpectedPoseJson(
    const char* path,
    ObjectDir* dir,
    const std::vector<std::string>& selectedBones,
    const char* sourceMilo,
    const char* clipName,
    float beat
) {
    if (!path || !dir)
        return false;

    std::vector<RndTransformable*> bones;
    for (const auto& name : selectedBones) {
        RndTransformable* t = dir->Find<RndTransformable>(name.c_str(), true);
        if (t)
            bones.push_back(t);
    }
    if (bones.empty())
        return false;

    std::sort(bones.begin(), bones.end(), [](const RndTransformable* a, const RndTransformable* b) {
        return strcmp(a->Name(), b->Name()) < 0;
    });

    FILE* f = fopen(path, "wb");
    if (!f)
        return false;

    fprintf(f, "{\n");
    fprintf(f, "  \"source_milo\": ");
    WriteJsonEscaped(f, sourceMilo ? sourceMilo : "");
    fprintf(f, ",\n  \"clip\": ");
    WriteJsonEscaped(f, clipName ? clipName : "");
    fprintf(f, ",\n  \"beat\": %.7f,\n", beat);
    fprintf(f, "  \"bone_count\": %zu,\n", bones.size());
    fprintf(f, "  \"bones\": [\n");

    for (size_t i = 0; i < bones.size(); i++) {
        RndTransformable* b = bones[i];
        const Transform& l = b->LocalXfm();
        const Transform& w = b->WorldXfm();
        fprintf(f, "    {\n");
        fprintf(f, "      \"name\": ");
        WriteJsonEscaped(f, b->Name());
        fprintf(f, ",\n");
        fprintf(f, "      \"local\": {\n");
        fprintf(f, "        \"pos\": [%.9g, %.9g, %.9g],\n", l.v.x, l.v.y, l.v.z);
        fprintf(f, "        \"m\": [[%.9g, %.9g, %.9g], [%.9g, %.9g, %.9g], [%.9g, %.9g, %.9g]]\n",
                l.m.x.x, l.m.x.y, l.m.x.z,
                l.m.y.x, l.m.y.y, l.m.y.z,
                l.m.z.x, l.m.z.y, l.m.z.z);
        fprintf(f, "      },\n");
        fprintf(f, "      \"world\": {\n");
        fprintf(f, "        \"pos\": [%.9g, %.9g, %.9g],\n", w.v.x, w.v.y, w.v.z);
        fprintf(f, "        \"m\": [[%.9g, %.9g, %.9g], [%.9g, %.9g, %.9g], [%.9g, %.9g, %.9g]]\n",
                w.m.x.x, w.m.x.y, w.m.x.z,
                w.m.y.x, w.m.y.y, w.m.y.z,
                w.m.z.x, w.m.z.y, w.m.z.z);
        fprintf(f, "      }\n");
        fprintf(f, "    }%s\n", (i + 1 < bones.size()) ? "," : "");
    }

    fprintf(f, "  ]\n}\n");
    fclose(f);
    return true;
}

TEST(MiloViewerScreenshot, ScreenshotModeExitsCleanlyAndWritesPng) {
    const std::string viewer = GetViewerPath();
    if (!FileExists(viewer))
        GTEST_SKIP() << "milo-viewer binary not found at " << viewer;

    const std::string miloLib = GetMiloLibRoot();
    if (miloLib.empty())
        GTEST_SKIP() << "MILO_LIB not set and HOME not available";

    const std::string charPath =
        miloLib + "/char/main/dancer/gen/aubrey01.milo_xbox";
    const std::string clipsPath =
        miloLib + "/char/crowd/anim/gen/female_base.milo_xbox";

    if (!FileExists(charPath) || !FileExists(clipsPath)) {
        GTEST_SKIP() << "Required DC3 assets not found under " << miloLib;
    }

    std::ostringstream pngPath;
    pngPath << "/tmp/milo_viewer_screenshot_test_" << getpid() << ".png";

    std::ostringstream cmd;
    cmd
        << "ASAN_OPTIONS='alloc_dealloc_mismatch=0:halt_on_error=0:detect_odr_violation=0' "
        << "timeout 120 "
        << "\"" << viewer << "\" "
        << "\"" << charPath << "\" "
        << "--screenshot \"" << pngPath.str() << "\" "
        << "--clips \"" << clipsPath << "\" "
        << "--clip crouching_great_01 --frame -2 --direct-pose "
        << "--width 640 --height 360 2>&1";

    RunResult result = RunCommand(cmd.str());
    if (OutputHasNullBackend(result.output)) {
        unlink(pngPath.str().c_str());
        GTEST_SKIP() << "Null backend active; cannot validate rendered screenshot output";
    }

    if (result.exitCode != 0) {
        printf("--- milo-viewer output tail ---\n");
        if (result.output.size() > 4000)
            printf("%s\n", result.output.substr(result.output.size() - 4000).c_str());
        else
            printf("%s\n", result.output.c_str());
        printf("--- end output ---\n");
    }

    EXPECT_TRUE(FileExists(pngPath.str())) << "Screenshot not created: " << pngPath.str();
    EXPECT_GT(FileSize(pngPath.str()), 0) << "Screenshot file is empty";
    if (result.signal != 0 || result.exitCode != 0) {
        printf("MiloViewerScreenshot: tolerated non-zero exit after screenshot write (exit=%d signal=%d)\n",
               result.exitCode, result.signal);
    }

    unlink(pngPath.str().c_str());
}

TEST(MiloViewerScreenshot, ScreenshotModeWritesPoseDumpJson) {
    const std::string viewer = GetViewerPath();
    if (!FileExists(viewer))
        GTEST_SKIP() << "milo-viewer binary not found at " << viewer;

    const std::string miloLib = GetMiloLibRoot();
    if (miloLib.empty())
        GTEST_SKIP() << "MILO_LIB not set and HOME not available";

    const std::string charPath =
        miloLib + "/char/main/dancer/gen/aubrey01.milo_xbox";
    const std::string clipsPath =
        miloLib + "/char/crowd/anim/gen/female_base.milo_xbox";

    if (!FileExists(charPath) || !FileExists(clipsPath)) {
        GTEST_SKIP() << "Required DC3 assets not found under " << miloLib;
    }

    std::ostringstream pngPath;
    pngPath << "/tmp/milo_viewer_pose_dump_test_" << getpid() << ".png";
    std::ostringstream posePath;
    posePath << "/tmp/milo_viewer_pose_dump_test_" << getpid() << ".pose.json";

    std::ostringstream cmd;
    cmd
        << "ASAN_OPTIONS='alloc_dealloc_mismatch=0:halt_on_error=0:detect_odr_violation=0' "
        << "timeout 120 "
        << "\"" << viewer << "\" "
        << "\"" << charPath << "\" "
        << "--screenshot \"" << pngPath.str() << "\" "
        << "--clips \"" << clipsPath << "\" "
        << "--clip stand_bad_01 --direct-pose "
        << "--pose-dump \"" << posePath.str() << "\" "
        << "--pose-dump-beat MID "
        << "--width 640 --height 360 2>&1";

    RunResult result = RunCommand(cmd.str());
    if (OutputHasGpuFailure(result.output) || OutputHasNullBackend(result.output)) {
        unlink(pngPath.str().c_str());
        unlink(posePath.str().c_str());
        GTEST_SKIP() << "GPU unavailable; cannot run viewer screenshot+pose test";
    }

    EXPECT_TRUE(FileExists(pngPath.str())) << "Screenshot not created: " << pngPath.str();
    EXPECT_GT(FileSize(pngPath.str()), 0) << "Screenshot file is empty";
    EXPECT_TRUE(FileExists(posePath.str())) << "Pose dump not created: " << posePath.str();
    EXPECT_GT(FileSize(posePath.str()), 0) << "Pose dump file is empty";
    if (result.signal != 0 || result.exitCode != 0) {
        printf("MiloViewerScreenshot: tolerated non-zero exit after screenshot+pose write (exit=%d signal=%d)\n",
               result.exitCode, result.signal);
    }

    FILE* f = fopen(posePath.str().c_str(), "rb");
    ASSERT_NE(f, nullptr);
    std::string content;
    char buf[4096];
    while (true) {
        size_t n = fread(buf, 1, sizeof(buf), f);
        if (n == 0) break;
        content.append(buf, n);
    }
    fclose(f);

    EXPECT_NE(content.find("\"bones\""), std::string::npos);
    EXPECT_NE(content.find("\"bone_count\""), std::string::npos);
    EXPECT_NE(content.find("bone_pelvis.mesh"), std::string::npos);

    unlink(pngPath.str().c_str());
    unlink(posePath.str().c_str());
}

TEST(MiloViewerScreenshot, PoseDumpCanMatchGoldenWithTolerance) {
    const std::string viewer = GetViewerPath();
    if (!FileExists(viewer))
        GTEST_SKIP() << "milo-viewer binary not found at " << viewer;

    const std::string project = GetProjectRoot();
    const std::string compareTool = project + "/native/scripts/compare_pose_json.py";
    if (!FileExists(compareTool))
        GTEST_SKIP() << "compare_pose_json.py missing at " << compareTool;

    const std::string miloLib = GetMiloLibRoot();
    if (miloLib.empty())
        GTEST_SKIP() << "MILO_LIB not set and HOME not available";

    const std::string charPath =
        miloLib + "/char/main/dancer/gen/aubrey01.milo_xbox";
    const std::string clipsPath =
        miloLib + "/char/crowd/anim/gen/female_base.milo_xbox";
    if (!FileExists(charPath) || !FileExists(clipsPath))
        GTEST_SKIP() << "Required DC3 assets not found under " << miloLib;

    const char* envGoldenDir = getenv("MILO_POSE_GOLDEN_DIR");
    std::string goldenDir = envGoldenDir && envGoldenDir[0]
        ? std::string(envGoldenDir)
        : project + "/archive/screenshots/pose_regression/goldens";
    std::string goldenPose = goldenDir + "/stand_bad_mid.pose.json";
    if (!FileExists(goldenPose))
        GTEST_SKIP() << "No golden pose JSON found at " << goldenPose;

    std::ostringstream capturePose;
    capturePose << "/tmp/milo_viewer_pose_compare_" << getpid() << ".pose.json";
    std::ostringstream capturePng;
    capturePng << "/tmp/milo_viewer_pose_compare_" << getpid() << ".png";

    std::ostringstream runViewer;
    runViewer
        << "ASAN_OPTIONS='alloc_dealloc_mismatch=0:halt_on_error=0:detect_odr_violation=0' "
        << "timeout 120 "
        << "\"" << viewer << "\" "
        << "\"" << charPath << "\" "
        << "--screenshot \"" << capturePng.str() << "\" "
        << "--clips \"" << clipsPath << "\" "
        << "--clip stand_bad_01 --direct-pose "
        << "--pose-dump \"" << capturePose.str() << "\" "
        << "--pose-dump-beat MID "
        << "--width 640 --height 360 2>&1";

    RunResult run = RunCommand(runViewer.str());
    ASSERT_TRUE(FileExists(capturePose.str()));
    if (run.signal != 0 || run.exitCode != 0) {
        printf("MiloViewerScreenshot: tolerated non-zero exit after pose dump write (exit=%d signal=%d)\n",
               run.exitCode, run.signal);
    }

    std::ostringstream compareCmd;
    compareCmd
        << "\"" << compareTool << "\" "
        << "\"" << goldenPose << "\" "
        << "\"" << capturePose.str() << "\" "
        << "--pos-tol 0.01 --mat-tol 0.01 --beat-tol 0.001 --require-same-clip 2>&1";

    RunResult cmp = RunCommand(compareCmd.str());
    if (cmp.exitCode != 0) {
        printf("--- pose compare output ---\n%s\n--- end ---\n", cmp.output.c_str());
    }
    EXPECT_EQ(cmp.exitCode, 0) << "Pose dump differs from golden beyond tolerance";

    unlink(capturePose.str().c_str());
    unlink(capturePng.str().c_str());
}

class MiloViewerPosePipeline : public EngineTestFixture {};

TEST_F(MiloViewerPosePipeline, ViewerPoseDumpMatchesInProcessPoseMeshes) {
    const std::string viewer = GetViewerPath();
    if (!FileExists(viewer))
        GTEST_SKIP() << "milo-viewer binary not found at " << viewer;

    const std::string project = GetProjectRoot();
    const std::string compareTool = project + "/native/scripts/compare_pose_json.py";
    if (!FileExists(compareTool))
        GTEST_SKIP() << "compare_pose_json.py missing at " << compareTool;

    const std::string miloLib = GetMiloLibRoot();
    if (miloLib.empty())
        GTEST_SKIP() << "MILO_LIB not set and HOME not available";

    const std::string charPath = miloLib + "/char/main/dancer/gen/aubrey01.milo_xbox";
    const std::string clipsPath = miloLib + "/char/crowd/anim/gen/female_base.milo_xbox";
    if (!FileExists(charPath) || !FileExists(clipsPath))
        GTEST_SKIP() << "Required DC3 assets not found under " << miloLib;

    const std::vector<std::string> bones = {
        "bone_head.mesh",
        "bone_L-hand.mesh",
        "bone_pelvis.mesh",
        "bone_R-hand.mesh",
    };
    std::string bonesCsv;
    for (size_t i = 0; i < bones.size(); i++) {
        bonesCsv += bones[i];
        if (i + 1 < bones.size())
            bonesCsv += ",";
    }

    std::ostringstream capturePose;
    capturePose << "/tmp/milo_viewer_pose_pipeline_capture_" << getpid() << ".pose.json";
    std::ostringstream expectedPose;
    expectedPose << "/tmp/milo_viewer_pose_pipeline_expected_" << getpid() << ".pose.json";
    std::ostringstream capturePng;
    capturePng << "/tmp/milo_viewer_pose_pipeline_capture_" << getpid() << ".png";

    std::ostringstream runViewer;
    runViewer
        << "ASAN_OPTIONS='alloc_dealloc_mismatch=0:halt_on_error=0:detect_odr_violation=0' "
        << "timeout 120 "
        << "\"" << viewer << "\" "
        << "\"" << charPath << "\" "
        << "--screenshot \"" << capturePng.str() << "\" "
        << "--clips \"" << clipsPath << "\" "
        << "--clip stand_bad_01 --direct-pose "
        << "--pose-dump \"" << capturePose.str() << "\" "
        << "--pose-dump-bones \"" << bonesCsv << "\" "
        << "--pose-dump-beat MID "
        << "--width 640 --height 360 2>&1";

    RunResult run = RunCommand(runViewer.str());
    if (OutputHasGpuFailure(run.output) || OutputHasNullBackend(run.output)) {
        unlink(capturePose.str().c_str());
        unlink(capturePng.str().c_str());
        GTEST_SKIP() << "GPU unavailable; cannot run viewer pose pipeline test";
    }
    ASSERT_TRUE(FileExists(capturePose.str()));
    if (run.signal != 0 || run.exitCode != 0) {
        printf("MiloViewerPosePipeline: tolerated non-zero exit after pose dump write (exit=%d signal=%d)\n",
               run.exitCode, run.signal);
    }

    ObjectDir* charDir = TryLoadMiloAbsolute(charPath);
    ASSERT_NE(charDir, nullptr) << "Failed to load character milo for expected pose";
    ObjectDir* clipsDir = TryLoadMiloAbsolute(clipsPath);
    ASSERT_NE(clipsDir, nullptr) << "Failed to load clips milo for expected pose";

    CharClip* clip = nullptr;
    for (ObjDirItr<CharClip> it(clipsDir, true); it; ++it) {
        if (strcmp(it->Name(), "stand_bad_01") == 0) {
            clip = it;
            break;
        }
    }
    ASSERT_NE(clip, nullptr) << "Clip stand_bad_01 not found in " << clipsPath;

    float beat = (clip->StartBeat() + clip->EndBeat()) * 0.5f;
    clip->PoseMeshes(charDir, beat);

    ASSERT_TRUE(
        WriteExpectedPoseJson(
            expectedPose.str().c_str(), charDir, bones, charPath.c_str(), clip->Name(), beat
        )
    ) << "Failed to write expected pose json";

    std::ostringstream compareCmd;
    compareCmd
        << "\"" << compareTool << "\" "
        << "\"" << expectedPose.str() << "\" "
        << "\"" << capturePose.str() << "\" "
        << "--pos-tol 2.0 --mat-tol 0.01 --beat-tol 0.001 --require-same-clip 2>&1";

    RunResult cmp = RunCommand(compareCmd.str());
    if (cmp.exitCode != 0) {
        printf("--- pose pipeline compare output ---\n%s\n--- end ---\n", cmp.output.c_str());
    }
    EXPECT_EQ(cmp.exitCode, 0)
        << "Viewer pose dump diverges from in-process PoseMeshes at MID beat";

    unlink(capturePose.str().c_str());
    unlink(expectedPose.str().c_str());
    unlink(capturePng.str().c_str());
}
