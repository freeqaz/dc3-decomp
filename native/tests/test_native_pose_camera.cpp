// The camera setting's pure half (native/src/platform/PoseCamera_Native.h):
// reading pose_server.py's camera list, naming a camera, what "next camera"
// means, the saved choice's file, and which camera a launch uses.
//
// Nothing here asks this box for its cameras or starts a pose server.  The
// lists are the text `pose_server.py --list-cameras tsv` prints for the
// playtest Mac's two cameras (tests/test_pose_server.py asserts the server
// prints exactly these lines from that Mac's system_profiler output).  The
// run-time switch and the whole setting against a stub server are in
// test_native_pose_spawner.cpp.

#include <gtest/gtest.h>

#include "platform/PoseCamera_Native.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace Dc3PoseCamera;

namespace {

const char *const kBrio = "id:0x220000046d0944";
const char *const kFaceTime = "id:3642F2CD-E322-42E7-9360-19815B003AA6";

const char *const kMacTwo =
    "dc3-pose-cameras 1\n"
    "camera\tid:0x220000046d0944\t0x220000046d0944\tMX Brio\n"
    "camera\tid:3642F2CD-E322-42E7-9360-19815B003AA6\t3642F2CD-E322-42E7-9360-19815B003AA6\t"
    "FaceTime HD Camera\n";
const char *const kMacAlone =
    "dc3-pose-cameras 1\n"
    "camera\tid:3642F2CD-E322-42E7-9360-19815B003AA6\t3642F2CD-E322-42E7-9360-19815B003AA6\t"
    "FaceTime HD Camera\n";

std::vector<Camera> Cameras(const char *tsv) {
    std::vector<Camera> cameras;
    EXPECT_TRUE(ParseList(tsv, &cameras));
    return cameras;
}

std::string ScratchFile() {
    const char *tmp = getenv("TMPDIR");
    std::string path = std::string((tmp && *tmp) ? tmp : "/tmp") + "/dc3_pose_camera_XXXXXX";
    std::vector<char> buf(path.begin(), path.end());
    buf.push_back('\0');
    int fd = mkstemp(buf.data());
    if (fd >= 0)
        close(fd);
    return buf.data();
}

} // namespace

TEST(PoseCameraListTest, TheServersListIsReadInItsOrder) {
    std::vector<Camera> cameras = Cameras(kMacTwo);
    ASSERT_EQ(cameras.size(), 2u);
    EXPECT_EQ(cameras[0].spec, kBrio);
    EXPECT_EQ(cameras[0].id, "0x220000046d0944");
    EXPECT_EQ(cameras[0].name, "MX Brio");
    EXPECT_EQ(cameras[1].spec, kFaceTime);
    EXPECT_EQ(cameras[1].name, "FaceTime HD Camera");
}

TEST(PoseCameraListTest, TextThatIsNotTheListIsNotAnEmptyList) {
    std::vector<Camera> cameras;
    EXPECT_FALSE(ParseList("", &cameras));
    EXPECT_FALSE(ParseList("Traceback (most recent call last):\n  File ...\n", &cameras));
    // A camera line with no header before it is somebody else's output.
    EXPECT_FALSE(ParseList("camera\tid:x\tx\tX\n", &cameras));
    EXPECT_TRUE(cameras.empty());
    // No cameras at all IS a list.
    EXPECT_TRUE(ParseList("dc3-pose-cameras 1\n", &cameras));
    EXPECT_TRUE(cameras.empty());
}

TEST(PoseCameraListTest, AnErrorLineAndLinesFromANewerServerAreNotCameras) {
    std::vector<Camera> cameras;
    std::string error;
    EXPECT_TRUE(ParseList("python noise first\n"
                          "dc3-pose-cameras 2\r\n"
                          "error\tcould not list the cameras: nope\n"
                          "colour\tblue\n"
                          "camera\tonly-two-fields\n"
                          "camera\t/dev/video0\t/dev/video0\tIntegrated Camera\textra\n",
                          &cameras, &error));
    EXPECT_EQ(error, "could not list the cameras: nope");
    ASSERT_EQ(cameras.size(), 1u);
    EXPECT_EQ(cameras[0].spec, "/dev/video0");
    EXPECT_EQ(cameras[0].name, "Integrated Camera");
}

TEST(PoseCameraFindTest, ACameraIsNamedBySpecIdentityOrNameNeverByPosition) {
    std::vector<Camera> cameras = Cameras(kMacTwo);
    EXPECT_EQ(Find(cameras, kFaceTime), 1);
    EXPECT_EQ(Find(cameras, "3642F2CD-E322-42E7-9360-19815B003AA6"), 1);
    EXPECT_EQ(Find(cameras, "facetime hd camera"), 1);
    EXPECT_EQ(Find(cameras, "  MX Brio "), 0);
    // "1" is an index for the server to open, not the second entry here: on
    // Linux the list's positions are not /dev/video numbers.
    EXPECT_EQ(Find(cameras, "1"), -1);
    EXPECT_EQ(Find(cameras, "id:MX Brio"), -1) << "id: means the identity, not the name";
    EXPECT_EQ(Find(cameras, ""), -1);
    EXPECT_EQ(Find(cameras, "auto"), -1);
}

TEST(PoseCameraCycleTest, AutoIsTheFirstCameraAndNextWraps) {
    std::vector<Camera> two = Cameras(kMacTwo);
    EXPECT_EQ(InUse(two, "auto"), 0);
    EXPECT_EQ(InUse(two, ""), 0);
    EXPECT_EQ(Next(two, "auto"), 1);
    EXPECT_EQ(Next(two, kBrio), 1);
    EXPECT_EQ(Next(two, kFaceTime), 0);
    // A camera that is not in the list (an index, an unplugged one): the first.
    EXPECT_EQ(InUse(two, "/dev/video7"), -1);
    EXPECT_EQ(Next(two, "/dev/video7"), 0);
    // One camera: next is the one in use, so selecting the row changes nothing.
    std::vector<Camera> one = Cameras(kMacAlone);
    EXPECT_EQ(Next(one, "auto"), InUse(one, "auto"));
    EXPECT_EQ(Next(one, kFaceTime), InUse(one, kFaceTime));
    std::vector<Camera> none;
    EXPECT_EQ(InUse(none, "auto"), -1);
    EXPECT_EQ(Next(none, "auto"), -1);
}

TEST(PoseCameraSavedTest, TheChoiceRoundTripsThroughItsFile) {
    std::string path = ScratchFile();
    unlink(path.c_str());
    EXPECT_TRUE(LoadSaved(path).empty()) << "a missing file is no choice";

    Saved saved;
    saved.spec = kFaceTime;
    saved.name = "FaceTime HD Camera";
    ASSERT_TRUE(StoreSaved(path, saved));
    Saved back = LoadSaved(path);
    EXPECT_EQ(back.spec, kFaceTime);
    EXPECT_EQ(back.name, "FaceTime HD Camera");
    EXPECT_EQ(access((path + ".tmp").c_str(), F_OK), -1) << "the temporary is left behind";

    // auto is "no choice", whatever name was beside it.
    Saved autoChoice;
    autoChoice.spec = "auto";
    ASSERT_TRUE(StoreSaved(path, autoChoice));
    EXPECT_TRUE(LoadSaved(path).empty());
    unlink(path.c_str());

    EXPECT_FALSE(StoreSaved("/nonexistent-dir-for-dc3-test/pose_camera.txt", saved));
}

TEST(PoseCameraSavedTest, AHandEditedOrNewerFileStillLoads) {
    Saved saved = ParseSaved("dc3-pose-camera 7\n"
                             "  camera   id:0x220000046d0944  \r\n"
                             "resolution 1920x1080\n"
                             "\n"
                             "name MX Brio\n");
    EXPECT_EQ(saved.spec, kBrio);
    EXPECT_EQ(saved.name, "MX Brio");
    EXPECT_TRUE(ParseSaved("").empty());
    EXPECT_TRUE(ParseSaved("garbage\n\x01\x02\n").empty());
    EXPECT_TRUE(ParseSaved("name Only A Name\n").empty());
    EXPECT_EQ(FormatSaved(saved), "dc3-pose-camera 1\ncamera id:0x220000046d0944\nname MX Brio\n");
}

TEST(PoseCameraLaunchTest, TheEnvironmentWinsThenTheSavedChoiceThenAuto) {
    std::vector<Camera> two = Cameras(kMacTwo);
    Saved none, saved;
    saved.spec = kFaceTime;
    saved.name = "FaceTime HD Camera";

    Launch byDefault = ResolveLaunch(nullptr, none, &two);
    EXPECT_EQ(byDefault.spec, "auto");
    EXPECT_EQ(byDefault.source, kFromDefault);
    EXPECT_TRUE(byDefault.notice.empty());
    EXPECT_EQ(ResolveLaunch("", none, nullptr).spec, "auto") << "an empty variable is unset";

    Launch bySaved = ResolveLaunch(nullptr, saved, &two);
    EXPECT_EQ(bySaved.spec, kFaceTime);
    EXPECT_EQ(bySaved.source, kFromSaved);
    EXPECT_EQ(bySaved.name, "FaceTime HD Camera");
    EXPECT_TRUE(bySaved.notice.empty());

    Launch byEnv = ResolveLaunch("0", saved, &two);
    EXPECT_EQ(byEnv.spec, "0");
    EXPECT_EQ(byEnv.source, kFromEnv);
    EXPECT_TRUE(byEnv.notice.empty());
    EXPECT_EQ(ResolveLaunch("MX Brio", saved, &two).name, "MX Brio");
}

TEST(PoseCameraLaunchTest, ASavedCameraThatIsNotPluggedInIsAutoForThisRunWithANotice) {
    std::vector<Camera> alone = Cameras(kMacAlone);
    Saved saved;
    saved.spec = kBrio;
    saved.name = "MX Brio";
    Launch launch = ResolveLaunch(nullptr, saved, &alone);
    EXPECT_EQ(launch.spec, "auto");
    EXPECT_EQ(launch.source, kFromSavedMissing);
    EXPECT_NE(launch.notice.find("\"MX Brio\" is not connected"), std::string::npos) << launch.notice;
    // Plugged back in, the same saved choice is used again.
    std::vector<Camera> two = Cameras(kMacTwo);
    EXPECT_EQ(ResolveLaunch(nullptr, saved, &two).spec, kBrio);
    // No cameras at all is still an answer: the saved one is not there.
    std::vector<Camera> none;
    EXPECT_EQ(ResolveLaunch(nullptr, saved, &none).source, kFromSavedMissing);
}

TEST(PoseCameraLaunchTest, WhenTheBoxCannotBeAskedTheSavedChoiceGoesToTheServerAsItIs) {
    Saved saved;
    saved.spec = kBrio;
    saved.name = "MX Brio";
    Launch launch = ResolveLaunch(nullptr, saved, nullptr);
    EXPECT_EQ(launch.spec, kBrio);
    EXPECT_EQ(launch.source, kFromSaved);
    EXPECT_EQ(launch.name, "MX Brio");
    // So does a saved index or path, list or no list: only an identity can be
    // "not plugged in".
    std::vector<Camera> alone = Cameras(kMacAlone);
    Saved byPath;
    byPath.spec = "/dev/video2";
    EXPECT_EQ(ResolveLaunch(nullptr, byPath, &alone).spec, "/dev/video2");
    EXPECT_EQ(ResolveLaunch(nullptr, byPath, &alone).source, kFromSaved);
}
