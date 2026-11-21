// DC3 Native Port - which camera the pose server reads, as a setting.
// See PoseCamera_Native.h and docs/native/POSE_INPUT.md.

#include "PoseCamera_Native.h"
#include "Skeleton_Native.h"
#include "platform/NativeNotice.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <strings.h>

#ifndef __EMSCRIPTEN__
#include "platform/ExecutablePath.h"
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

// MemcardMgr_Stub.cpp: the directory the profile saves live in, created.
extern const char *NativeSaveDir();

namespace Dc3PoseCamera {

namespace {

const char *const kListHeader = "dc3-pose-cameras";  // pose_server.py CAMERA_LIST_HEADER
const char *const kSavedMagic = "dc3-pose-camera";
const int kSavedVersion = 1;
const char *const kIdPrefix = "id:";

bool StartsWith(const std::string &s, const char *prefix) {
    return s.compare(0, strlen(prefix), prefix) == 0;
}

std::string Trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') a++;
    while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
    return s.substr(a, b - a);
}

std::vector<std::string> SplitTabs(const std::string &line) {
    std::vector<std::string> out;
    size_t at = 0;
    for (;;) {
        size_t tab = line.find('\t', at);
        if (tab == std::string::npos) {
            out.push_back(line.substr(at));
            return out;
        }
        out.push_back(line.substr(at, tab - at));
        at = tab + 1;
    }
}

bool IsAuto(const std::string &spec) { return spec.empty() || strcasecmp(spec.c_str(), "auto") == 0; }

} // namespace

// ---- pure ------------------------------------------------------------------

bool ParseList(const std::string &tsv, std::vector<Camera> *cameras, std::string *error) {
    cameras->clear();
    if (error)
        error->clear();
    std::istringstream ss(tsv);
    std::string line;
    bool header = false;
    while (std::getline(ss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (!header) {
            // "dc3-pose-cameras <version>": anything before it is not the list.
            if (line.compare(0, strlen(kListHeader), kListHeader) == 0)
                header = true;
            continue;
        }
        std::vector<std::string> f = SplitTabs(line);
        if (f[0] == "error" && f.size() >= 2) {
            if (error)
                *error = f[1];
        } else if (f[0] == "camera" && f.size() >= 4 && !f[1].empty()) {
            Camera cam;
            cam.spec = f[1];
            cam.id = f[2];
            cam.name = f[3].empty() ? f[2] : f[3];
            cameras->push_back(cam);
        } // anything else: a newer server's line
    }
    return header;
}

int Find(const std::vector<Camera> &cameras, const std::string &what) {
    std::string wanted = Trim(what);
    if (wanted.empty())
        return -1;
    bool byId = StartsWith(wanted, kIdPrefix);
    std::string bare = byId ? wanted.substr(strlen(kIdPrefix)) : wanted;
    for (size_t i = 0; i < cameras.size(); i++) {
        if (cameras[i].spec == wanted || cameras[i].id == bare)
            return (int)i;
    }
    if (byId)
        return -1;
    for (size_t i = 0; i < cameras.size(); i++) {
        if (strcasecmp(cameras[i].name.c_str(), wanted.c_str()) == 0)
            return (int)i;
    }
    return -1;
}

int InUse(const std::vector<Camera> &cameras, const std::string &spec) {
    if (cameras.empty())
        return -1;
    return IsAuto(spec) ? 0 : Find(cameras, spec);
}

int Next(const std::vector<Camera> &cameras, const std::string &spec) {
    if (cameras.empty())
        return -1;
    return (InUse(cameras, spec) + 1) % (int)cameras.size(); // -1 (not listed) -> the first
}

Saved ParseSaved(const std::string &text) {
    Saved saved;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        line = Trim(line);
        size_t space = line.find(' ');
        if (space == std::string::npos)
            continue;
        std::string key = line.substr(0, space), value = Trim(line.substr(space + 1));
        if (key == "camera")
            saved.spec = value;
        else if (key == "name")
            saved.name = value;
        // the magic line and anything a newer build wrote: skipped
    }
    if (saved.empty())
        return Saved();
    return saved;
}

std::string FormatSaved(const Saved &saved) {
    std::string out = std::string(kSavedMagic) + " " + std::to_string(kSavedVersion) + "\n";
    out += "camera " + (saved.spec.empty() ? std::string("auto") : saved.spec) + "\n";
    if (!saved.name.empty())
        out += "name " + saved.name + "\n";
    return out;
}

Saved LoadSaved(const std::string &path) {
    std::ifstream f(path);
    if (!f)
        return Saved();
    std::stringstream ss;
    ss << f.rdbuf();
    return ParseSaved(ss.str());
}

bool StoreSaved(const std::string &path, const Saved &saved) {
    std::string tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "w");
    if (!f)
        return false;
    std::string text = FormatSaved(saved);
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = (fclose(f) == 0) && ok;
    if (ok)
        ok = rename(tmp.c_str(), path.c_str()) == 0;
    if (!ok)
        remove(tmp.c_str());
    return ok;
}

const char *SourceName(Source source) {
    switch (source) {
    case kFromEnv: return "env";
    case kFromSaved: return "saved";
    case kFromSavedMissing: return "saved-missing";
    case kFromSetting: return "setting";
    case kFromDefault: break;
    }
    return "default";
}

Launch ResolveLaunch(const char *env, const Saved &saved, const std::vector<Camera> *cameras) {
    Launch launch;
    if (env && *env) {
        launch.spec = env;
        launch.source = kFromEnv;
        if (cameras) {
            int at = Find(*cameras, launch.spec);
            if (at >= 0)
                launch.name = (*cameras)[at].name;
        }
        return launch;
    }
    if (saved.empty())
        return launch;
    int at = cameras ? Find(*cameras, saved.spec) : -1;
    // Only an identity can be "not plugged in": an index or a path that was
    // saved by hand goes to the server as it is, as DC3_POSE_CAMERA's does.
    if (cameras && at < 0 && StartsWith(saved.spec, kIdPrefix)) {
        launch.source = kFromSavedMissing;
        launch.notice = "Camera \"" + (saved.name.empty() ? saved.spec : saved.name)
            + "\" is not connected - using the default camera. It is still your saved choice.";
        return launch;
    }
    launch.spec = saved.spec;
    launch.name = at >= 0 ? (*cameras)[at].name : saved.name;
    launch.source = kFromSaved;
    return launch;
}

// ---- this process ----------------------------------------------------------

namespace {

const char *const kNoticeKey = "pose-camera";
const float kNoticeSeconds = 5.0f;

std::string (*sListerForTest)() = nullptr;
std::string sSavedPathForTest;
std::vector<Camera> sCameras;
bool sListed = false;     // sCameras is an answer (possibly an empty one)
bool sListOk = false;     // ... and the box could be asked
Launch sLaunch;
bool sBooted = false;
std::string sSpec = "auto";
std::string sName;
Source sSource = kFromDefault;
bool sNoticeUp = false;
std::chrono::steady_clock::time_point sNoticeDown;

void PostNotice(const std::string &text, float seconds = kNoticeSeconds) {
#ifdef __EMSCRIPTEN__
    // No setting on web, and no FramePoll to take a notice down again.
    fprintf(stderr, "%s\n", text.c_str());
    (void)seconds;
    return;
#endif
    NativeNotice::Post(kNoticeKey, text, "", seconds);
    // Taken down again by FramePoll: a notice left posted hides older ones.
    sNoticeUp = true;
    sNoticeDown = std::chrono::steady_clock::now()
        + std::chrono::milliseconds((long long)(seconds * 1000.0f) + 500);
}

#ifndef __EMSCRIPTEN__
// Run argv and collect its stdout.  False when it could not be run or did not
// finish in time (it is killed).  Not through NativeSpawner: that hands the
// child the game's stdio, and this short-lived child starts no realtime
// thread, which is what the spawner exists for.
bool RunAndCapture(const std::vector<std::string> &argv, int timeoutMs, std::string *out) {
    int fds[2];
    if (pipe(fds) != 0)
        return false;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    std::vector<char *> args;
    for (const std::string &a : argv)
        args.push_back(const_cast<char *>(a.c_str()));
    args.push_back(nullptr);
    pid_t pid = -1;
    int rc = posix_spawnp(&pid, args[0], &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (rc != 0) {
        close(fds[0]);
        return false;
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    bool timedOut = false;
    char buf[4096];
    for (;;) {
        long long left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
            timedOut = true;
            break;
        }
        struct pollfd pfd = { fds[0], POLLIN, 0 };
        int r = poll(&pfd, 1, (int)left);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            timedOut = r == 0;
            break;
        }
        ssize_t n = read(fds[0], buf, sizeof(buf));
        if (n > 0)
            out->append(buf, (size_t)n);
        else if (n == 0 || errno != EINTR)
            break;
    }
    close(fds[0]);
    if (timedOut)
        kill(pid, SIGKILL);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return !timedOut;
}

// The lister's output, or "" when the box could not be asked.
// DC3_POSE_CAMERA_LIST names a file to read instead (the flow tests' cameras).
std::string RunLister() {
    if (sListerForTest)
        return sListerForTest();
    if (const char *file = getenv("DC3_POSE_CAMERA_LIST"); file && *file) {
        std::ifstream f(file);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
    // The real script even under DC3_POSE_SERVER: a drop-in server takes the
    // server's arguments, not this one.
    std::vector<std::string> argv = { NativeSkeletonProvider::ChoosePosePython().path,
                                      NativeTreePath("scripts/pose_server.py"),
                                      "--list-cameras", "tsv" };
    std::string out;
    auto started = std::chrono::steady_clock::now();
    bool ran = RunAndCapture(argv, 8000, &out);
    double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    printf("Pose camera: listed the cameras in %.0f ms (%s --list-cameras tsv)%s\n", ms,
           argv[0].c_str(), ran ? "" : ": FAILED or timed out");
    return ran ? out : std::string();
}
#else
std::string RunLister() { return sListerForTest ? sListerForTest() : std::string(); }
#endif

void Refresh() {
    std::string error;
    sListOk = ParseList(RunLister(), &sCameras, &error);
    sListed = true;
    if (!sListOk)
        fprintf(stderr, "Pose camera: the cameras could not be listed\n");
    else if (!error.empty())
        fprintf(stderr, "Pose camera: %s\n", error.c_str());
}

std::string JsonEscape(const std::string &s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += (char)c;
        } else if (c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out += (char)c;
        }
    }
    return out;
}

// "2 of 3" for the camera in use, "" when it is not in the list.
std::string Position() {
    int at = InUse(sCameras, sSpec);
    if (at < 0)
        return std::string();
    return std::to_string(at + 1) + " of " + std::to_string(sCameras.size());
}

// Make `spec` the camera: restart the server on it, remember it, say so.
bool Apply(const std::string &spec, const std::string &name) {
    bool changed = spec != sSpec;
    sSpec = spec;
    sName = name;
    sSource = kFromSetting;
    Saved saved;
    saved.spec = spec;
    saved.name = name;
    bool stored = StoreSaved(SavedPath(), saved);
    bool running = TheSkeletonProvider && TheSkeletonProvider->IsRunning();
    bool switched = true;
    if (TheSkeletonProvider && (changed || !running))
        switched = TheSkeletonProvider->SwitchCamera(spec);
    std::string shown = CurrentName();
    std::string text = "Camera: " + (shown.empty() ? spec : shown);
    std::string position = Position();
    if (!position.empty() && sCameras.size() > 1)
        text += " (" + position + ")";
    text += switched ? "." : " - it could not be started.";
    if (!stored)
        text += " The choice could not be saved.";
    else if (const char *env = getenv("DC3_POSE_CAMERA"); env && *env)
        text += " DC3_POSE_CAMERA is set and decides again at the next launch.";
    fprintf(stderr, "DC3 POSE CAMERA: now %s (%s)%s\n", spec.c_str(), shown.c_str(),
            stored ? "" : " [not saved]");
    PostNotice(text);
    return switched;
}

} // namespace

std::string SavedPath() {
    if (!sSavedPathForTest.empty())
        return sSavedPathForTest;
    return std::string(NativeSaveDir()) + "/pose_camera.txt";
}

const Launch &Boot() {
    if (sBooted)
        return sLaunch;
    sBooted = true;
    const char *env = getenv("DC3_POSE_CAMERA");
    Saved saved = LoadSaved(SavedPath());
    // The lister runs only when there is a saved choice to check: a launch
    // that never touched the setting costs nothing more than before.
    bool needList = !(env && *env) && !saved.empty();
    if (needList)
        Refresh();
    sLaunch = ResolveLaunch(env, saved, (needList && sListOk) ? &sCameras : nullptr);
    sSpec = sLaunch.spec;
    sName = sLaunch.name;
    sSource = sLaunch.source;
    printf("DC3 POSE CAMERA: %s (%s%s%s; Options > Gameplay Settings > Camera, "
           "{native_settings set_pose_camera ...} or /api/pose_camera changes it)\n",
           sSpec.c_str(), SourceName(sSource), sName.empty() ? "" : ", ", sName.c_str());
    if (!sLaunch.notice.empty())
        PostNotice(sLaunch.notice, 8.0f);
    return sLaunch;
}

bool Available() {
#ifdef __EMSCRIPTEN__
    return false;
#else
    if (!TheSkeletonProvider || !TheSkeletonProvider->LaunchesServer())
        return false;
    const char *video = getenv("DC3_POSE_VIDEO"); // footage stands in for the camera
    return !(video && *video);
#endif
}

const std::vector<Camera> &List(bool refresh) {
    if (refresh || !sListed)
        Refresh();
    return sCameras;
}

std::string Current() { return sSpec; }

std::string CurrentName() {
    if (IsAuto(sSpec)) {
        int at = InUse(sCameras, sSpec);
        return at >= 0 ? sCameras[at].name + " (automatic)" : std::string();
    }
    int at = Find(sCameras, sSpec);
    return at >= 0 ? sCameras[at].name : sName;
}

Source CurrentSource() { return sSource; }

bool Set(const std::string &what) {
    std::string wanted = Trim(what);
    if (!Available()) {
        PostNotice("Camera: there is no camera pose server to switch in this run.");
        return false;
    }
    if (wanted.empty())
        return false;
    List(true);
    int at = Find(sCameras, wanted);
    if (at >= 0)
        return Apply(sCameras[at].spec, sCameras[at].name);
    if (IsAuto(wanted))
        return Apply("auto", "");
    // An index, a /dev/videoN path, an identity that is not plugged in now:
    // pose_server.py --camera's own business, as with DC3_POSE_CAMERA.
    bool raw = wanted[0] == '/' || StartsWith(wanted, kIdPrefix)
        || wanted.find_first_not_of("0123456789") == std::string::npos;
    if (raw)
        return Apply(wanted, "");
    std::string names;
    for (const Camera &cam : sCameras)
        names += (names.empty() ? "" : ", ") + cam.name;
    PostNotice("Camera: nothing called \"" + wanted + "\" (found: "
               + (names.empty() ? std::string("no cameras") : names) + ").");
    return false;
}

bool Cycle() {
    if (!Available()) {
        PostNotice("Camera: there is no camera pose server to switch in this run.");
        return false;
    }
    List(true);
    if (sCameras.empty()) {
        PostNotice(sListOk ? "Camera: no cameras found."
                           : "Camera: the cameras could not be listed (see the console log).");
        return false;
    }
    int now = InUse(sCameras, sSpec);
    int next = Next(sCameras, sSpec);
    bool running = TheSkeletonProvider && TheSkeletonProvider->IsRunning();
    if (next == now && running) {
        // One camera, and it is the one in use: nothing to restart or save.
        PostNotice("Camera: " + sCameras[now].name + " (the only camera found).");
        return true;
    }
    return Apply(sCameras[next].spec, sCameras[next].name);
}

std::string Description() {
    if (!Available())
        return "No camera in use.";
    List(false);
    std::string name = CurrentName();
    std::string text = "Camera in use: " + (name.empty() ? sSpec : name) + ".";
    if (sCameras.size() > 1)
        text += " Select to switch to the next of " + std::to_string(sCameras.size()) + " cameras.";
    else
        text += " Plug in another camera, then select to switch to it.";
    return text;
}

std::string StateJson(bool refresh) {
    List(refresh);
    Saved saved = LoadSaved(SavedPath());
    int inUse = InUse(sCameras, sSpec);
    std::string json = "{\"available\":";
    json += Available() ? "true" : "false";
    json += ",\"current\":\"" + JsonEscape(sSpec) + "\"";
    json += ",\"name\":\"" + JsonEscape(CurrentName()) + "\"";
    json += std::string(",\"source\":\"") + SourceName(sSource) + "\"";
    json += ",\"saved\":\"" + JsonEscape(saved.spec) + "\"";
    json += ",\"savedName\":\"" + JsonEscape(saved.name) + "\"";
    json += std::string(",\"listed\":") + (sListOk ? "true" : "false");
    json += ",\"cameras\":[";
    for (size_t i = 0; i < sCameras.size(); i++) {
        if (i)
            json += ",";
        json += "{\"spec\":\"" + JsonEscape(sCameras[i].spec) + "\",\"id\":\""
            + JsonEscape(sCameras[i].id) + "\",\"name\":\"" + JsonEscape(sCameras[i].name)
            + "\",\"inUse\":" + ((int)i == inUse ? "true" : "false") + "}";
    }
    json += "]}";
    return json;
}

void FramePoll() {
    if (sNoticeUp && std::chrono::steady_clock::now() >= sNoticeDown) {
        NativeNotice::Clear(kNoticeKey);
        sNoticeUp = false;
    }
}

void SetListerForTest(std::string (*lister)()) { sListerForTest = lister; }
void SetSavedPathForTest(const std::string &path) { sSavedPathForTest = path; }

void ResetForTest() {
    sCameras.clear();
    sListed = sListOk = false;
    sLaunch = Launch();
    sBooted = false;
    sSpec = "auto";
    sName.clear();
    sSource = kFromDefault;
    if (sNoticeUp)
        NativeNotice::Clear(kNoticeKey);
    sNoticeUp = false;
}

} // namespace Dc3PoseCamera
