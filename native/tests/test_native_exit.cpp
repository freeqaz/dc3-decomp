// H-9: every way dc3-native is asked to stop ends cleanly.
//
// Before (np-dogfood2, then measured again in np-exit):
//   * destroying the window (`xdotool windowclose` = XDestroyWindow from another
//     client) -> BadWindow in WgpuRnd::BeginDrawing -> Xlib's default handler
//     exit(1) -> static destructors -> SIGSEGV in ObjDirPtr<ObjectDir>::operator=
//     (rc 139);
//   * DTA `{exit}` (Debug::Exit -> XLaunchNewImage -> exit(0)) -> the same
//     static-destructor SIGSEGV (rc 139);
//   * SIGTERM -> _exit(143) from wherever the main thread was, a synchronous
//     save write included.
//
// Each test drives a real dc3-native and reads its exit status and DC3_EXIT
// record. The window tests need no window manager: they run their own Xvfb and
// act on the window from a second X client, as `xdotool windowclose` does
// (XDestroyWindow) and as a window manager's close button does (a
// WM_PROTOCOLS / WM_DELETE_WINDOW ClientMessage).
//
// This TU includes Xlib and no decomp header (Xlib's macros collide with the
// decomp PCH); CMake compiles it without the PCH.

#include <gtest/gtest.h>

// The process-exit tests run on every POSIX desktop host. Only the window
// tests further down are X11-only (they drive a private Xvfb with Xlib); on
// macOS the exit hook is the atexit + Dc3Exit path (main_native.cpp), which
// these tests are what check.
#if defined(__linux__) || defined(__APPLE__)

#if defined(__linux__)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif
#include "platform/ExecutablePath.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern const char *NativeFindDataDir();

namespace {

std::string BinaryDir() {
    char buf[PATH_MAX];
    ssize_t n = ReadExecutablePath(buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = 0;
    std::string p(buf);
    size_t slash = p.rfind('/');
    return slash == std::string::npos ? "." : p.substr(0, slash);
}

std::string Dc3NativePath() { return BinaryDir() + "/dc3-native"; }

std::string ReadAll(const std::string &path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool Contains(const std::string &s, const char *needle) {
    return s.find(needle) != std::string::npos;
}

std::string Tail(const std::string &s, size_t n = 3000) {
    return s.size() > n ? "...\n" + s.substr(s.size() - n) : s;
}

// An empty persistence root, as the other engine tests use, so a run neither
// reads nor writes the developer's ~/.local/share/dc3.
std::string MakeRunDir() {
    char tmpl[] = "/tmp/dc3-native-exit-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) return std::string();
    std::string root(dir);
    mkdir((root + "/cache").c_str(), 0755);
    mkdir((root + "/saves").c_str(), 0755);
    return root;
}

struct Engine {
    pid_t pid = -1;
    std::string log;
    bool exited = false;
    int status = 0;

    std::string Output() const { return ReadAll(log); }

    // Reaps the child if it has exited. Returns true once it has.
    bool Poll() {
        if (exited) return true;
        int st = 0;
        if (waitpid(pid, &st, WNOHANG) == pid) {
            exited = true;
            status = st;
        }
        return exited;
    }

    // Waits for `needle` in the log; false if the engine exits or time runs out.
    bool WaitFor(const char *needle, int seconds) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        while (std::chrono::steady_clock::now() < deadline) {
            if (Contains(Output(), needle)) return true;
            if (Poll()) return Contains(Output(), needle);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }

    // Waits for exit; on timeout kills the engine and returns false.
    bool WaitExit(int seconds) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        while (std::chrono::steady_clock::now() < deadline) {
            if (Poll()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        exited = true;
        return false;
    }

    ~Engine() {
        if (pid > 0 && !Poll()) {
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
        }
    }
};

// Starts dc3-native with the test's environment plus `env`, output to a log in
// `runDir`. The cwd stays the test's, as RunHeadless does, so the engine finds
// its data the way NativeFindDataDir did.
void Start(Engine &e, const std::string &runDir, const std::vector<std::string> &env) {
    e.log = runDir + "/engine.log";
    pid_t pid = fork();
    if (pid == 0) {
        int fd = open(e.log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        setenv("MILO_FATAL_FAILS", "0", 1);
        setenv("DC3_SHOW_SPLASH", "0", 1);
        setenv("DC3_FAST_BOOT", "1", 1);
        setenv("MILO_MAX_FRAMES", "0", 1);
        setenv("DC3_POSE", "off", 1);
        setenv("DC3_HEARTBEAT_S", "0", 1);
        setenv("DC3_CACHE_DIR", (runDir + "/cache").c_str(), 1);
        setenv("DC3_SAVE_DIR", (runDir + "/saves").c_str(), 1);
        for (const std::string &kv : env) {
            size_t eq = kv.find('=');
            if (eq == std::string::npos) {
                unsetenv(kv.c_str());
            } else {
                setenv(kv.substr(0, eq).c_str(), kv.substr(eq + 1).c_str(), 1);
            }
        }
        std::string bin = Dc3NativePath();
        execl(bin.c_str(), bin.c_str(), (char *)nullptr);
        _exit(127);
    }
    e.pid = pid;
}

bool GpuUnavailable(const std::string &out) {
    return Contains(out.c_str(), "GPU initialization failed") ||
           Contains(out.c_str(), "Null backend");
}

// The clean-exit invariants every test shares.
void ExpectNoCrash(const std::string &out) {
    EXPECT_FALSE(Contains(out, "DC3 Native: Caught ")) << Tail(out);
    EXPECT_FALSE(Contains(out, "X Error of failed request")) << Tail(out);
}

class NativeExitTest : public ::testing::Test {
protected:
    std::string mRunDir;

    void SetUp() override {
        if (access(Dc3NativePath().c_str(), X_OK) != 0)
            GTEST_SKIP() << "dc3-native binary not found at " << Dc3NativePath();
        if (!NativeFindDataDir())
            GTEST_SKIP() << "[no-game-data] no gen/main_xbox.hdr for dc3-native to boot";
        mRunDir = MakeRunDir();
        ASSERT_FALSE(mRunDir.empty());
    }
};

// Sends `body` as an HTTP POST to /api/dta/eval and does not wait for a reply
// (an `{exit}` never sends one).
bool PostEval(int port, const char *body) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return false;
    }
    std::ostringstream req;
    req << "POST /api/dta/eval HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\n"
        << "Content-Length: " << strlen(body) << "\r\nConnection: close\r\n\r\n" << body;
    std::string s = req.str();
    bool ok = write(fd, s.data(), s.size()) == (ssize_t)s.size();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    close(fd);
    return ok;
}

// Runs `curl -s <args>` and returns its stdout ("" when curl is missing).
std::string Curl(const std::string &args) {
    std::string cmd = "curl -s -m 30 " + args + " 2>/dev/null";
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) return std::string();
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    pclose(p);
    return out;
}

bool HaveCurl() { return system("command -v curl >/dev/null 2>&1") == 0; }

} // namespace

// SIGTERM is a request to quit: the loop leaves at a frame boundary and the
// process exits through main's return, keeping 128+15 as its status.
TEST_F(NativeExitTest, SigtermQuitsAtAFrameBoundary) {
    Engine e;
    Start(e, mRunDir, {"MILO_HEADLESS=1"});
    ASSERT_TRUE(e.WaitFor("DC3 Native: Entering main loop", 120)) << Tail(e.Output());
    std::this_thread::sleep_for(std::chrono::seconds(2));
    ASSERT_FALSE(e.Poll()) << Tail(e.Output());
    kill(e.pid, SIGTERM);
    ASSERT_TRUE(e.WaitExit(30)) << "still running 30 s after SIGTERM\n" << Tail(e.Output());
    std::string out = e.Output();
    ExpectNoCrash(out);
    ASSERT_TRUE(WIFEXITED(e.status)) << Tail(out);
    EXPECT_EQ(WEXITSTATUS(e.status), 143);
    EXPECT_TRUE(Contains(out, "DC3_EXIT: how=main_loop_returned detail=SIGTERM code=143"))
        << "SIGTERM did not leave through the main loop\n" << Tail(out);
}

// DTA `{exit}` is the game's own exit (Debug::Exit -> XLaunchNewImage ->
// exit(0)). It must not run the static destructors that crash in
// ObjDirPtr<ObjectDir>::operator=.
TEST_F(NativeExitTest, DtaExitSkipsTheStaticDestructors) {
    Engine e;
    Start(e, mRunDir, {"MILO_HEADLESS=1", "DC3_HTTP=1", "DC3_HTTP_PORT=0"});
    ASSERT_TRUE(e.WaitFor("DC3 Native: Entering main loop", 120)) << Tail(e.Output());
    std::string out = e.Output();
    size_t at = out.find("DC3_HTTP_PORT=");
    ASSERT_NE(at, std::string::npos) << Tail(out);
    int port = atoi(out.c_str() + at + strlen("DC3_HTTP_PORT="));
    ASSERT_GT(port, 0);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    ASSERT_TRUE(PostEval(port, "{exit}"));
    ASSERT_TRUE(e.WaitExit(30)) << "still running 30 s after {exit}\n" << Tail(e.Output());
    out = e.Output();
    EXPECT_TRUE(Contains(out, "APP EXITING")) << "Debug::Exit never ran\n" << Tail(out);
    ExpectNoCrash(out);
    ASSERT_TRUE(WIFEXITED(e.status)) << Tail(out);
    EXPECT_EQ(WEXITSTATUS(e.status), 0) << Tail(out);
    EXPECT_TRUE(Contains(out, "DC3_EXIT: how=exit_runtime")) << Tail(out);
}

// np-harness: an HTTP port the engine cannot bind is a named exit (98, a FATAL
// line naming the port, DC3_EXIT detail=http_port_in_use), not an abort() that
// reads like any other crash.  The port is held by a plain listener, as by an
// unrelated process.
TEST_F(NativeExitTest, HttpPortInUseIsANamedExit) {
    int holder = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(holder, 0);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = 0;
    ASSERT_EQ(bind(holder, (sockaddr *)&addr, sizeof(addr)), 0);
    ASSERT_EQ(listen(holder, 1), 0);
    socklen_t len = sizeof(addr);
    ASSERT_EQ(getsockname(holder, (sockaddr *)&addr, &len), 0);
    int port = ntohs(addr.sin_port);

    Engine e;
    Start(e, mRunDir, {"MILO_HEADLESS=1", "DC3_HTTP=1", "DC3_HTTP_PORT=" + std::to_string(port)});
    bool exited = e.WaitExit(120);
    close(holder);
    std::string out = e.Output();
    ASSERT_TRUE(exited) << "still running with its port taken\n" << Tail(out);
    ExpectNoCrash(out);
    ASSERT_TRUE(WIFEXITED(e.status)) << "not a named exit (signal?)\n" << Tail(out);
    EXPECT_EQ(WEXITSTATUS(e.status), 98) << Tail(out);
    std::string named = "FATAL - HTTP debug server: port " + std::to_string(port) +
                        " already in use";
    EXPECT_TRUE(Contains(out, named.c_str()))
        << Tail(out);
    EXPECT_TRUE(Contains(out, "DC3_EXIT: how=exit_runtime detail=http_port_in_use code=98"))
        << Tail(out);
}

// np-harness: a second engine started on the first one's port exits 98 instead
// of sharing it.  Before, cpp-httplib's default SO_REUSEPORT let both listen and
// the kernel split the requests between them (measured: /api/health answered
// alternately by two engines on 9290).  Also checks DC3_HTTP_PORT_FILE.
TEST_F(NativeExitTest, SecondEngineOnAPortExitsInsteadOfSharing) {
    std::string otherDir = MakeRunDir();
    ASSERT_FALSE(otherDir.empty());
    std::string portFile = mRunDir + "/http_port";
    Engine first;
    Start(first, mRunDir,
          {"MILO_HEADLESS=1", "DC3_HTTP=1", "DC3_HTTP_PORT=0", "DC3_HTTP_PORT_FILE=" + portFile});
    ASSERT_TRUE(first.WaitFor("DC3_HTTP_PORT=", 120)) << Tail(first.Output());
    // Read at once, with no wait: HttpServer::Start writes the port file before
    // it prints the line, so the line implies the file.  Until 2026-11-13 it
    // printed first, and under load (~40) this read found no file.
    int port = atoi(ReadAll(portFile).c_str());
    ASSERT_GT(port, 0) << "DC3_HTTP_PORT_FILE not written\n" << Tail(first.Output());
    std::string line = "DC3_HTTP_PORT=" + std::to_string(port) + "\n";
    EXPECT_TRUE(Contains(first.Output(), line.c_str()))
        << "the port file disagrees with the engine's own line\n" << Tail(first.Output());

    Engine second;
    Start(second, otherDir,
          {"MILO_HEADLESS=1", "DC3_HTTP=1", "DC3_HTTP_PORT=" + std::to_string(port)});
    bool exited = second.WaitExit(120);
    std::string out = second.Output();
    ASSERT_TRUE(exited) << "the second engine is still running on port " << port
                        << " -- the two share it\n" << Tail(out);
    ASSERT_TRUE(WIFEXITED(second.status)) << Tail(out);
    EXPECT_EQ(WEXITSTATUS(second.status), 98) << Tail(out);
    EXPECT_TRUE(Contains(out, "detail=http_port_in_use")) << Tail(out);
    EXPECT_FALSE(first.Poll()) << "the first engine died\n" << Tail(first.Output());
}

// H-13: GET /api/health's uptime_s is wall-clock time.  It was
// TaskMgr::Seconds(kRealTime), the kTaskSeconds timeline that the game sets
// to song time during play (kRealTime only skips the A/V offset), so in a song
// it reported the song position.  The test moves that timeline the way a song
// does ({taskmgr set_seconds}) and reads it back as the control.
TEST_F(NativeExitTest, HealthUptimeIsWallClockNotSongTime) {
    if (!HaveCurl()) GTEST_SKIP() << "curl not installed";
    std::string portFile = mRunDir + "/http_port";
    Engine e;
    auto started = std::chrono::steady_clock::now();
    Start(e, mRunDir, {"MILO_HEADLESS=1", "MILO_NORENDER=1", "DC3_HTTP=1", "DC3_HTTP_PORT=0",
                       "DC3_HTTP_PORT_FILE=" + portFile});
    ASSERT_TRUE(e.WaitFor("DC3 Native: Entering main loop", 120)) << Tail(e.Output());
    int port = atoi(ReadAll(portFile).c_str());
    ASSERT_GT(port, 0) << Tail(e.Output());
    std::string api = "http://127.0.0.1:" + std::to_string(port) + "/api";
    Curl("-X POST --data '{taskmgr set_seconds 5000 FALSE}' " + api + "/dta/eval");
    std::string clock = Curl("-X POST --data '{taskmgr seconds}' " + api + "/dta/eval");
    std::string health = Curl(api + "/health");
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    size_t at = health.find("\"uptime_s\":");
    ASSERT_NE(at, std::string::npos) << "health: " << health;
    double uptime = atof(health.c_str() + at + strlen("\"uptime_s\":"));
    // Control: the song clock really did move (else this test proves nothing).
    EXPECT_NE(clock.find("5000"), std::string::npos)
        << "{taskmgr seconds} did not read back 5000: " << clock;
    EXPECT_GE(uptime, 0.0) << health;
    EXPECT_LE(uptime, wall + 1.0)
        << "uptime_s=" << uptime << " after " << wall << " s of wall clock: it is "
           "reading the song timeline (set to 5000), not the clock\n" << health;
    kill(e.pid, SIGTERM);
    e.WaitExit(30);
}

#if defined(__linux__)
namespace {

// The next Xvfb's screen (WxHxDepth) and X resources (the RESOURCE_MANAGER
// property, as xrdb sets it). A test that changes them restores them
// (ScopedXvfbSetup).
std::string sXvfbScreen = "1280x720x24";
std::string sXResources;

struct ScopedXvfbSetup {
    ScopedXvfbSetup(const char *screen, const char *resources = "") {
        sXvfbScreen = screen;
        sXResources = resources;
    }
    ~ScopedXvfbSetup() {
        sXvfbScreen = "1280x720x24";
        sXResources.clear();
    }
};

// A private X server. Skips (via `available`) when Xvfb is not installed.
struct Xvfb {
    pid_t pid = -1;
    std::string display;
    bool available = false;
    Display *resourceClient = nullptr; // holds sXResources on the server

    Xvfb() {
        if (system("command -v Xvfb >/dev/null 2>&1") != 0) return;
        int fds[2];
        if (pipe(fds) != 0) return;
        pid = fork();
        if (pid == 0) {
            close(fds[0]);
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
            }
            std::string fd = std::to_string(fds[1]);
            execlp("Xvfb", "Xvfb", "-displayfd", fd.c_str(), "-screen", "0", sXvfbScreen.c_str(),
                   "-nolisten", "tcp", (char *)nullptr);
            _exit(127);
        }
        close(fds[1]);
        char buf[32] = {};
        ssize_t n = 0, total = 0;
        // Xvfb writes the display number and a newline once it is ready.
        while (total < (ssize_t)sizeof(buf) - 1 &&
               (n = read(fds[0], buf + total, sizeof(buf) - 1 - total)) > 0) {
            total += n;
            if (strchr(buf, '\n')) break;
        }
        close(fds[0]);
        if (total > 0) {
            display = ":" + std::string(buf, strcspn(buf, "\n"));
            available = true;
            if (!sXResources.empty()) {
                // The property xrdb writes; GLFW reads Xft.dpi from it for the
                // content scale. The connection stays open: when its last
                // client goes, an X server resets, and the reset drops the
                // root window's properties (as xrdb's own docs warn).
                if ((resourceClient = XOpenDisplay(display.c_str()))) {
                    Atom rm = XInternAtom(resourceClient, "RESOURCE_MANAGER", False);
                    XChangeProperty(resourceClient, DefaultRootWindow(resourceClient), rm,
                                    XA_STRING, 8, PropModeReplace,
                                    (const unsigned char *)sXResources.data(),
                                    (int)sXResources.size());
                    XSync(resourceClient, False);
                }
            }
        }
    }
    ~Xvfb() {
        if (resourceClient) XCloseDisplay(resourceClient);
        if (pid > 0) {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
        }
    }
};

// The engine's top-level window: a direct child of the root (no window manager
// reparents it) whose name starts with "DC3".
Window FindEngineWindow(Display *dpy) {
    Window root = DefaultRootWindow(dpy), parent, *children = nullptr;
    unsigned int count = 0;
    Window found = 0;
    if (!XQueryTree(dpy, root, &root, &parent, &children, &count)) return 0;
    // GLFW names the window through _NET_WM_NAME (UTF-8: "DC3 Native — WebGPU");
    // its WM_NAME is COMPOUND_TEXT, which XFetchName does not return.
    Atom netWmName = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    for (unsigned int i = 0; i < count && !found; i++) {
        Atom type;
        int format;
        unsigned long items, after;
        unsigned char *name = nullptr;
        if (XGetWindowProperty(dpy, children[i], netWmName, 0, 64, False, utf8, &type,
                               &format, &items, &after, &name) == Success && name) {
            if (items >= 3 && strncmp((const char *)name, "DC3", 3) == 0) found = children[i];
            XFree(name);
        }
    }
    if (children) XFree(children);
    return found;
}

// Boots dc3-native in a window on `x`, lets it draw, then hands the window to
// `act` from a second X client. Returns the engine's output.
// The display RunWindowed's engine is on, for an `act` that runs X tools.
std::string sWindowDisplay;

template <typename Act>
void RunWindowed(const std::string &runDir, Act act, Engine &e,
                 std::vector<std::string> env = {}) {
    Xvfb x;
    if (!x.available) GTEST_SKIP() << "Xvfb not installed; the window tests need a private X server";
    sWindowDisplay = x.display;
    env.push_back("MILO_HEADLESS");
    env.push_back("DISPLAY=" + x.display);
    Start(e, runDir, env);
    ASSERT_TRUE(e.WaitFor("DC3 Native: Entering main loop", 120)) << Tail(e.Output());
    if (GpuUnavailable(e.Output()))
        GTEST_SKIP() << "GPU unavailable; no window to close";
    ASSERT_TRUE(e.WaitFor("Windowed mode", 30)) << "not a windowed run\n" << Tail(e.Output());
    std::this_thread::sleep_for(std::chrono::seconds(2));
    ASSERT_FALSE(e.Poll()) << Tail(e.Output());

    Display *dpy = XOpenDisplay(x.display.c_str());
    ASSERT_NE(dpy, nullptr) << "cannot open " << x.display;
    Window w = FindEngineWindow(dpy);
    ASSERT_NE(w, 0u) << "no DC3 window on " << x.display;
    act(dpy, w);
    XSync(dpy, False);
    XCloseDisplay(dpy);
    ASSERT_TRUE(e.WaitExit(30)) << "still running 30 s after the window went\n" << Tail(e.Output());
}

// Runs `xdotool <args>` against the engine's display; false if it failed.
bool Xdotool(const std::string &args) {
    std::string cmd = "DISPLAY=" + sWindowDisplay + " xdotool " + args + " >/dev/null 2>&1";
    return system(cmd.c_str()) == 0;
}

bool HaveXdotool() { return system("command -v xdotool >/dev/null 2>&1") == 0; }

void SendWmDeleteWindow(Display *dpy, Window w) {
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = (long)XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, w, False, NoEventMask, &ev);
}

} // namespace

// What `xdotool windowclose` did in np-dogfood2: another client destroys the
// window. The loop must notice and end; no Xlib exit, no destructor crash.
TEST_F(NativeExitTest, DestroyedWindowEndsTheLoop) {
    Engine e;
    RunWindowed(mRunDir, [](Display *dpy, Window w) { XDestroyWindow(dpy, w); }, e);
    if (IsSkipped() || HasFatalFailure()) return;
    std::string out = e.Output();
    ExpectNoCrash(out);
    ASSERT_TRUE(WIFEXITED(e.status)) << Tail(out);
    EXPECT_EQ(WEXITSTATUS(e.status), 0) << Tail(out);
    EXPECT_TRUE(Contains(out, "DC3_EXIT: how=main_loop_returned detail=window_lost code=0"))
        << Tail(out);
}

// A window manager's close button: WM_PROTOCOLS / WM_DELETE_WINDOW. GLFW turns
// it into glfwWindowShouldClose, and the loop ends as `window_closed`. This is
// the graceful close np-dogfood2 could not test (`xdotool windowquit` sends
// _NET_CLOSE_WINDOW to the root window, which only a window manager acts on).
TEST_F(NativeExitTest, WmDeleteWindowEndsTheLoop) {
    Engine e;
    RunWindowed(mRunDir, [](Display *dpy, Window w) { SendWmDeleteWindow(dpy, w); }, e);
    if (IsSkipped() || HasFatalFailure()) return;
    std::string out = e.Output();
    ExpectNoCrash(out);
    ASSERT_TRUE(WIFEXITED(e.status)) << Tail(out);
    EXPECT_EQ(WEXITSTATUS(e.status), 0) << Tail(out);
    EXPECT_TRUE(Contains(out, "DC3_EXIT: how=main_loop_returned detail=window_closed code=0"))
        << Tail(out);
}

// G-5: the keys PLAN G-5 added -- X, Y, Z (LT), C (RT) -- reach pad 0 as
// button messages, typed into the window from a second X client the way a
// person would (XTEST, the window under the pointer has the focus). Each is a
// `xdotool key` tap, press and release inside one frame, which only reads as a
// press because the poll turns on GLFW sticky keys. X is typed first: the
// session's first press is answered by the poll's own action table, which used
// to give X ViewModify (5) on the press and ShellOption (14) on the release.
// MILO_INPUT_LOG=1 prints one "Joypad: down ..." line per button message.
TEST_F(NativeExitTest, KeysForXYAndTriggersReachThePad) {
    if (!HaveXdotool()) GTEST_SKIP() << "xdotool not installed";
    Engine e;
    std::string missing;
    RunWindowed(mRunDir, [&](Display *dpy, Window w) {
        const char *keys[] = {"x", "y", "z", "c"};
        for (const char *k : keys) {
            ASSERT_TRUE(Xdotool(std::string("key ") + k)) << "xdotool key " << k;
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
        }
        const char *want[] = {
            "Joypad: down pad=0 button=7 (X) action=14",
            "Joypad: down pad=0 button=4 (Y) action=3",
            "Joypad: down pad=0 button=0 (LT)",
            "Joypad: down pad=0 button=1 (RT)",
        };
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        for (;;) {
            std::string out = e.Output();
            missing.clear();
            for (const char *line : want)
                if (!Contains(out, line)) missing += std::string("\n  ") + line;
            if (missing.empty() || std::chrono::steady_clock::now() > deadline) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        SendWmDeleteWindow(dpy, w);
    }, e, {"MILO_INPUT_LOG=1"});
    if (IsSkipped() || HasFatalFailure()) return;
    std::string out = e.Output();
    EXPECT_TRUE(missing.empty()) << "no button message for:" << missing << "\n" << Tail(out);
    EXPECT_TRUE(Contains(out, "X = X, Y = Y")) << "startup key map not printed\n" << Tail(out);
    EXPECT_TRUE(Contains(out, "Z = LT, C = RT, Ctrl+Q = quit")) << Tail(out);
    EXPECT_FALSE(Contains(out, "press ESC to exit")) << "Esc is B; the log must not call it quit";
    ExpectNoCrash(out);
}

// G-5: Ctrl+Q quits like a window close -- at a frame boundary, rc 0 -- and is
// not also an LB press (Q alone is LB). Ctrl is held across the Q tap, as a
// person presses it. (`xdotool key ctrl+q` releases Ctrl before Q within
// microseconds; the ImGui backend's modifier reads in its key callback use up
// Ctrl's sticky state, so that synthetic chord is not seen. Human-speed
// presses are.)
TEST_F(NativeExitTest, CtrlQEndsTheLoop) {
    if (!HaveXdotool()) GTEST_SKIP() << "xdotool not installed";
    Engine e;
    RunWindowed(mRunDir, [&](Display *, Window) {
        ASSERT_TRUE(Xdotool("keydown ctrl sleep 0.2 key q sleep 0.2 keyup ctrl"));
    }, e, {"MILO_INPUT_LOG=1"});
    if (IsSkipped() || HasFatalFailure()) return;
    std::string out = e.Output();
    ExpectNoCrash(out);
    ASSERT_TRUE(WIFEXITED(e.status)) << Tail(out);
    EXPECT_EQ(WEXITSTATUS(e.status), 0) << Tail(out);
    EXPECT_TRUE(Contains(out, "DC3_EXIT: how=main_loop_returned detail=quit_key code=0"))
        << Tail(out);
    EXPECT_FALSE(Contains(out, "button=2 (LB)")) << "Ctrl+Q also pressed LB\n" << Tail(out);
}

// H-13: /api/screenshot on a windowed engine.  It read only the headless
// target, so a windowed run answered 500 "headless mode required" and drivers
// fell back to `import -window`.  It now reads the swapchain texture between
// the frame's submit and present (WgpuRnd's frame-ready callback).
TEST_F(NativeExitTest, ScreenshotWorksWindowed) {
    if (!HaveCurl()) GTEST_SKIP() << "curl not installed";
    std::string portFile = mRunDir + "/http_port";
    std::string png = mRunDir + "/shot.png";
    Engine e;
    std::string code;
    RunWindowed(mRunDir, [&](Display *dpy, Window w) {
        int port = atoi(ReadAll(portFile).c_str());
        ASSERT_GT(port, 0) << "no DC3_HTTP_PORT_FILE";
        code = Curl("-o " + png + " -w '%{http_code}' http://127.0.0.1:" +
                    std::to_string(port) + "/api/screenshot");
        SendWmDeleteWindow(dpy, w);
    }, e, {"DC3_HTTP=1", "DC3_HTTP_PORT=0", "DC3_HTTP_PORT_FILE=" + portFile});
    if (IsSkipped() || HasFatalFailure()) return;
    std::string out = e.Output();
    std::string bytes = ReadAll(png);
    ASSERT_EQ(code, "200") << "body: " << bytes.substr(0, 200) << "\n" << Tail(out);
    ASSERT_GE(bytes.size(), (size_t)24);
    EXPECT_EQ(bytes.compare(0, 8, "\x89PNG\r\n\x1a\n"), 0) << "not a PNG";
    auto be32 = [&](size_t o) {
        return ((unsigned)(unsigned char)bytes[o] << 24) | ((unsigned)(unsigned char)bytes[o + 1] << 16) |
               ((unsigned)(unsigned char)bytes[o + 2] << 8) | (unsigned)(unsigned char)bytes[o + 3];
    };
    EXPECT_EQ(be32(16), 1280u) << "PNG width";
    EXPECT_EQ(be32(20), 720u) << "PNG height";
    ExpectNoCrash(out);
}

// np-defaults: with DC3_HTTP_PORT unset, a window prefers 9090 and falls back
// to a kernel-assigned port when 9090 is taken (the owner's game, a second
// engine), instead of exiting 98 -- the debug server is on by default in the
// dev profile, so a second windowed run must still start.  The first cut
// exited 98 "cannot bind port 0": a failed bind decommissions a cpp-httplib
// Server and its next bind returns -1 at once.
TEST_F(NativeExitTest, WindowedDefaultPortFallsBackWhen9090IsTaken) {
    int holder = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(holder, 0);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(9090);
    // Taken by this test, or already by someone else: 9090 is busy either way.
    if (bind(holder, (sockaddr *)&addr, sizeof(addr)) == 0) {
        ASSERT_EQ(listen(holder, 1), 0);
    } else if (errno != EADDRINUSE) {
        int err = errno;
        close(holder);
        GTEST_SKIP() << "cannot bind 127.0.0.1:9090: " << strerror(err);
    }
    std::string portFile = mRunDir + "/http_port";
    Engine e;
    int port = 0;
    RunWindowed(mRunDir, [&](Display *dpy, Window w) {
        port = atoi(ReadAll(portFile).c_str());
        SendWmDeleteWindow(dpy, w);
    }, e, {"DC3_HTTP=1", "DC3_HTTP_PORT=", "DC3_HTTP_PORT_FILE=" + portFile});
    close(holder);
    if (IsSkipped() || HasFatalFailure()) return;
    std::string out = e.Output();
    EXPECT_FALSE(Contains(out, "detail=http_port")) << "the busy default port was fatal\n"
                                                    << Tail(out);
    EXPECT_GT(port, 0) << "no DC3_HTTP_PORT_FILE\n" << Tail(out);
    EXPECT_NE(port, 9090) << Tail(out);
    EXPECT_TRUE(Contains(out, "port 9090 is taken")) << "the fallback was not announced\n"
                                                     << Tail(out);
    EXPECT_TRUE(Contains(out, "(loopback only)")) << Tail(out);
    ASSERT_TRUE(WIFEXITED(e.status)) << Tail(out);
    EXPECT_EQ(WEXITSTATUS(e.status), 0) << Tail(out);
    ExpectNoCrash(out);
}

// ===========================================================================
// H-18: the window shows the whole frame at any size; H-33: a window that
// cannot be drawn into stops the game instead of spinning.
//
// The frame is the Rnd virtual resolution (1280x720), scaled into the window
// with its aspect kept and black bars where the window's shape differs
// (engine gfx/PresentScale). These read the window's pixels back from the X
// server (XGetImage): bars must be exactly black, and the picture must reach
// the window's right and bottom edges -- the old corner picture left
// everything right of x=1280 and below y=720 black.
// ===========================================================================
namespace {

struct WindowShot {
    int w = 0, h = 0;
    std::vector<uint32_t> px; // 0xRRGGBB

    bool Grab(Display *dpy, Window win) {
        XWindowAttributes a;
        if (!XGetWindowAttributes(dpy, win, &a)) return false;
        XImage *img = XGetImage(dpy, win, 0, 0, a.width, a.height, AllPlanes, ZPixmap);
        if (!img) return false;
        w = a.width;
        h = a.height;
        px.resize((size_t)w * h);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) px[(size_t)y * w + x] = (uint32_t)XGetPixel(img, x, y) & 0xFFFFFF;
        XDestroyImage(img);
        return true;
    }
    // Fraction of pixels in [x0,x1) x [y0,y1) brighter than near-black.
    double Lit(int x0, int y0, int x1, int y1) const {
        long lit = 0, n = 0;
        for (int y = std::max(0, y0); y < std::min(h, y1); y++)
            for (int x = std::max(0, x0); x < std::min(w, x1); x++, n++) {
                uint32_t p = px[(size_t)y * w + x];
                if (((p >> 16) & 0xFF) > 8 || ((p >> 8) & 0xFF) > 8 || (p & 0xFF) > 8) lit++;
            }
        return n ? (double)lit / n : 0.0;
    }
    // Every pixel in the region is pure black (a bar).
    bool Black(int x0, int y0, int x1, int y1) const {
        for (int y = std::max(0, y0); y < std::min(h, y1); y++)
            for (int x = std::max(0, x0); x < std::min(w, x1); x++)
                if (px[(size_t)y * w + x] != 0) return false;
        return true;
    }
};

// The fitted picture for a 1280x720 frame (gfx/PresentScale's FitPresentRect).
struct Fit {
    int x, y, w, h;
};
Fit FitFrame(int winW, int winH) {
    Fit f;
    if ((long long)winW * 720 > (long long)winH * 1280) {
        f.h = winH;
        f.w = (int)(((long long)winH * 1280 + 360) / 720);
    } else {
        f.w = winW;
        f.h = (int)(((long long)winW * 720 + 640) / 1280);
    }
    f.x = (winW - f.w) / 2;
    f.y = (winH - f.h) / 2;
    return f;
}

// Waits (up to 20 s) for the window to be winW x winH with the frame fitted in
// it: bars black, picture lit along its right and bottom edges. "" on
// success, else what was wrong with the last look.
std::string WaitForFittedPicture(Display *dpy, Window win, int winW, int winH) {
    std::string why = "never grabbed";
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    do {
        XSync(dpy, False);
        WindowShot s;
        if (!s.Grab(dpy, win)) {
            why = "XGetImage failed";
        } else if (s.w != winW || s.h != winH) {
            why = "window is " + std::to_string(s.w) + "x" + std::to_string(s.h) + ", want " +
                  std::to_string(winW) + "x" + std::to_string(winH);
        } else {
            Fit f = FitFrame(winW, winH);
            const int strip = std::max(16, f.w / 12);
            double right = s.Lit(f.x + f.w - strip, f.y, f.x + f.w, f.y + f.h);
            double bottom = s.Lit(f.x, f.y + f.h - f.h / 8, f.x + f.w, f.y + f.h);
            bool bars = s.Black(0, 0, winW, f.y) && s.Black(0, f.y + f.h, winW, winH) &&
                        s.Black(0, 0, f.x, winH) && s.Black(f.x + f.w, 0, winW, winH);
            char buf[256];
            snprintf(buf, sizeof buf,
                     "picture %dx%d+%d+%d: right-edge strip %.1f%% lit, bottom strip %.1f%% lit, "
                     "bars %s",
                     f.w, f.h, f.x, f.y, right * 100, bottom * 100, bars ? "black" : "NOT black");
            why = buf;
            if (bars && right > 0.01 && bottom > 0.01) return "";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    } while (std::chrono::steady_clock::now() < deadline);
    return why;
}

class NativeWindowTest : public NativeExitTest {};

// Waits for the engine to exit on its own (no window); false on timeout.
bool WaitOwnExit(Engine &e, int seconds) { return e.WaitExit(seconds); }

} // namespace

// A window larger than the frame, and of another shape: the frame fills its
// width with bars above and below. Then resized wider than 16:9 (bars left and
// right) and taller (bars above and below again), as a person or a tiling
// desktop does.
TEST_F(NativeWindowTest, TheFrameIsScaledToFitTheWindow) {
    if (!HaveXdotool()) GTEST_SKIP() << "xdotool not installed";
    ScopedXvfbSetup setup("1920x1200x24");
    Engine e;
    std::string atStart, wide, tall;
    RunWindowed(mRunDir, [&](Display *dpy, Window w) {
        atStart = WaitForFittedPicture(dpy, w, 1600, 1000);
        ASSERT_TRUE(Xdotool("windowsize " + std::to_string(w) + " 1800 700"));
        wide = WaitForFittedPicture(dpy, w, 1800, 700);
        ASSERT_TRUE(Xdotool("windowsize " + std::to_string(w) + " 1000 900"));
        tall = WaitForFittedPicture(dpy, w, 1000, 900);
        SendWmDeleteWindow(dpy, w);
    }, e, {"MILO_WIDTH=1600", "MILO_HEIGHT=1000"});
    if (IsSkipped() || HasFatalFailure()) return;
    std::string out = e.Output();
    EXPECT_EQ(atStart, "") << "1600x1000 window\n" << Tail(out);
    EXPECT_EQ(wide, "") << "resized to 1800x700\n" << Tail(out);
    EXPECT_EQ(tall, "") << "resized to 1000x900\n" << Tail(out);
    EXPECT_TRUE(Contains(out, "frame scaled to fit")) << Tail(out);
    ExpectNoCrash(out);
}

// F11 and Alt+Enter toggle fullscreen (the window takes the whole screen, the
// frame letterboxed in it) and back to the window's size. Alt+Enter is not
// also an A press: the keyboard pad reports nothing while Alt is held.
TEST_F(NativeWindowTest, F11AndAltEnterToggleFullscreen) {
    if (!HaveXdotool()) GTEST_SKIP() << "xdotool not installed";
    ScopedXvfbSetup setup("1920x1200x24");
    Engine e;
    std::string full1, back1, full2, back2;
    RunWindowed(mRunDir, [&](Display *dpy, Window w) {
        ASSERT_TRUE(Xdotool("key F11"));
        full1 = WaitForFittedPicture(dpy, w, 1920, 1200);
        ASSERT_TRUE(Xdotool("key F11"));
        back1 = WaitForFittedPicture(dpy, w, 1280, 720);
        // Alt held across the Enter, as a person presses it.
        ASSERT_TRUE(Xdotool("keydown alt sleep 0.2 key Return sleep 0.2 keyup alt"));
        full2 = WaitForFittedPicture(dpy, w, 1920, 1200);
        ASSERT_TRUE(Xdotool("keydown alt sleep 0.2 key Return sleep 0.2 keyup alt"));
        back2 = WaitForFittedPicture(dpy, w, 1280, 720);
        SendWmDeleteWindow(dpy, w);
    }, e, {"MILO_INPUT_LOG=1"});
    if (IsSkipped() || HasFatalFailure()) return;
    std::string out = e.Output();
    EXPECT_EQ(full1, "") << "F11\n" << Tail(out);
    EXPECT_EQ(back1, "") << "F11 again\n" << Tail(out);
    EXPECT_EQ(full2, "") << "Alt+Enter\n" << Tail(out);
    EXPECT_EQ(back2, "") << "Alt+Enter again\n" << Tail(out);
    EXPECT_FALSE(Contains(out, "(A) action")) << "Alt+Enter also pressed A\n" << Tail(out);
    ExpectNoCrash(out);
}

// MILO_FULLSCREEN=1 starts fullscreen (scripts/play.sh --fullscreen).
TEST_F(NativeWindowTest, MiloFullscreenStartsFullscreen) {
    ScopedXvfbSetup setup("1920x1080x24");
    Engine e;
    std::string full;
    RunWindowed(mRunDir, [&](Display *dpy, Window w) {
        full = WaitForFittedPicture(dpy, w, 1920, 1080);
        SendWmDeleteWindow(dpy, w);
    }, e, {"MILO_FULLSCREEN=1"});
    if (IsSkipped() || HasFatalFailure()) return;
    std::string out = e.Output();
    EXPECT_EQ(full, "") << Tail(out);
    EXPECT_TRUE(Contains(out, "GpuDevice: fullscreen 1920x1080")) << Tail(out);
    ExpectNoCrash(out);
}

// HiDPI on X11: at a content scale of 2 (Xft.dpi 192) the window opens at
// twice the size in pixels, and the frame fills it at exactly 2x.
TEST_F(NativeWindowTest, AnX11ContentScaleSizesTheWindow) {
    ScopedXvfbSetup setup("2560x1600x24", "Xft.dpi:\t192\n");
    Engine e;
    std::string hidpi;
    RunWindowed(mRunDir, [&](Display *dpy, Window w) {
        hidpi = WaitForFittedPicture(dpy, w, 2560, 1440);
        SendWmDeleteWindow(dpy, w);
    }, e);
    if (IsSkipped() || HasFatalFailure()) return;
    std::string out = e.Output();
    EXPECT_EQ(hidpi, "") << Tail(out);
    EXPECT_TRUE(Contains(out, "surface 2560x1440")) << Tail(out);
    ExpectNoCrash(out);
}

// H-33: a window whose surface cannot be presented to on any window system is
// a named exit (97), not a silent fall back to an invisible headless run.
// MILO_SURFACE_FAULT=init makes every surface probe fail.
TEST_F(NativeWindowTest, AnUnusableSurfaceIsANamedExit) {
    Xvfb x;
    if (!x.available) GTEST_SKIP() << "Xvfb not installed";
    Engine e;
    Start(e, mRunDir, {"MILO_HEADLESS", "DISPLAY=" + x.display, "MILO_SURFACE_FAULT=init"});
    ASSERT_TRUE(WaitOwnExit(e, 180)) << "still running\n" << Tail(e.Output());
    std::string out = e.Output();
    if (GpuUnavailable(out)) GTEST_SKIP() << "GPU unavailable";
    ASSERT_TRUE(WIFEXITED(e.status)) << Tail(out);
    EXPECT_EQ(WEXITSTATUS(e.status), 97) << Tail(out);
    EXPECT_TRUE(Contains(out, "DC3 Native: FATAL - the window cannot be presented to")) << Tail(out);
    EXPECT_TRUE(Contains(out, "detail=window_surface_unusable code=97")) << Tail(out);
    EXPECT_FALSE(Contains(out, "falling back to headless")) << Tail(out);
    EXPECT_FALSE(Contains(out, "Entering main loop")) << Tail(out);
    ExpectNoCrash(out);
}

// H-33: the fallback re-opens the window (on X11: XWayland). Exercised here by
// MILO_SURFACE_FAULT=init-once, which fails the first window's probe only and
// lets the fallback run once even on X11: the second window must come up,
// draw and close normally.
TEST_F(NativeWindowTest, AnUnusableFirstSurfaceReopensTheWindow) {
    Engine e;
    std::string pic;
    RunWindowed(mRunDir, [&](Display *dpy, Window w) {
        pic = WaitForFittedPicture(dpy, w, 1280, 720);
        SendWmDeleteWindow(dpy, w);
    }, e, {"MILO_SURFACE_FAULT=init-once"});
    if (IsSkipped() || HasFatalFailure()) return;
    std::string out = e.Output();
    EXPECT_TRUE(Contains(out, "cannot be presented to: MILO_SURFACE_FAULT says so (test hook); "
                              "opening it again through X11"))
        << Tail(out);
    EXPECT_EQ(pic, "") << Tail(out);
    ASSERT_TRUE(WIFEXITED(e.status)) << Tail(out);
    EXPECT_EQ(WEXITSTATUS(e.status), 0) << Tail(out);
    ExpectNoCrash(out);
}

// H-33: a surface that never hands out a frame. The loop must not spin (on the
// GPD it ran 6,000-7,000 iterations a second drawing nothing): each failed
// acquire waits a frame period, and after 5 s without a single frame the game
// stops with exit 97. The engine's own count of failed acquires over that time
// is the loop's rate.
TEST_F(NativeWindowTest, ASurfaceThatGivesNoFramesStopsWithoutSpinning) {
    Xvfb x;
    if (!x.available) GTEST_SKIP() << "Xvfb not installed";
    Engine e;
    // The frame cap off (DC3_FPS_CAP=0, and rb3 has none): the cap alone
    // would also hold this loop near 60/s and hide a spin in the acquire.
    Start(e, mRunDir, {"MILO_HEADLESS", "DISPLAY=" + x.display, "MILO_SURFACE_FAULT=acquire",
                       "DC3_FPS_CAP=0"});
    ASSERT_TRUE(WaitOwnExit(e, 300)) << "still running\n" << Tail(e.Output());
    std::string out = e.Output();
    if (GpuUnavailable(out)) GTEST_SKIP() << "GPU unavailable";
    ASSERT_TRUE(WIFEXITED(e.status)) << Tail(out);
    EXPECT_EQ(WEXITSTATUS(e.status), 97) << Tail(out);
    EXPECT_TRUE(Contains(out, "detail=window_surface_unusable code=97")) << Tail(out);
    // "... has not given a single frame in 5 s (312 attempts, last status Error)"
    size_t at = out.find("has not given a single frame in ");
    ASSERT_NE(at, std::string::npos) << Tail(out);
    double secs = 0;
    long long attempts = 0;
    ASSERT_EQ(sscanf(out.c_str() + at, "has not given a single frame in %lf s (%lld attempts",
                     &secs, &attempts), 2)
        << out.substr(at, 120);
    ASSERT_GE(secs, 5.0);
    const double rate = attempts / secs;
    // A frame period of sleep per attempt caps it near 60/s (fewer on a loaded
    // box); a spinning loop is in the thousands.
    EXPECT_LT(rate, 120.0) << attempts << " acquires in " << secs << " s\n" << Tail(out);
    EXPECT_GT(attempts, 0);
    ExpectNoCrash(out);
}

#endif // __linux__ (window tests)

#if defined(__linux__)
// ===========================================================================
// The pose server dc3-native launches (platform/ProcessSpawner_Native.h).
//
// On a PipeWire box without rtkit, opening audio lowers the game's
// RLIMIT_RTTIME to 0:0, and a pose_server.py fork+exec'd from the game
// afterwards inherited it and was SIGKILLed at MediaPipe model load.  These
// tests make that happen on any Linux box: as soon as dc3-native's spawner
// exists, the test lowers the GAME's limit with prlimit(2) -- after the
// spawner fork, before the pose server launch, which is where audio init sits
// -- and a stub "python3" first on PATH records the limits it was given.
// ===========================================================================
#include <sys/resource.h>

namespace {

std::string SpawnRealPython() {
    const char *path = getenv("PATH");
    std::istringstream ss(path ? path : "/usr/bin:/bin");
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        std::string cand = dir + "/python3";
        if (!dir.empty() && access(cand.c_str(), X_OK) == 0)
            return cand;
    }
    return std::string();
}

std::vector<pid_t> ChildrenOf(pid_t pid) {
    std::string s = ReadAll("/proc/" + std::to_string(pid) + "/task/" + std::to_string(pid) + "/children");
    std::istringstream ss(s);
    std::vector<pid_t> out;
    int c;
    while (ss >> c)
        out.push_back(c);
    return out;
}

bool PidGone(pid_t pid) {
    if (kill(pid, 0) != 0 && errno == ESRCH)
        return true;
    std::string stat = ReadAll("/proc/" + std::to_string(pid) + "/stat");
    size_t paren = stat.rfind(')');
    return stat.empty() || (paren != std::string::npos && paren + 2 < stat.size() && stat[paren + 2] == 'Z');
}

bool WaitGone(pid_t pid, int ms) {
    for (int i = 0; i < ms / 50; i++) {
        if (PidGone(pid))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return PidGone(pid);
}

std::string RtTime(const std::string &limits) {
    size_t at = limits.find("Max realtime timeout");
    if (at == std::string::npos)
        return std::string();
    size_t eol = limits.find('\n', at);
    return limits.substr(at, eol == std::string::npos ? std::string::npos : eol - at);
}

bool RtTimeUnlimited(const std::string &line) {
    std::istringstream ss(line.size() > 20 ? line.substr(20) : std::string());
    std::string soft, hard;
    ss >> soft >> hard;
    return soft == "unlimited" && hard == "unlimited";
}

// The pid in "Launched pose_server.py (pid N, via the spawner)", or -1 (also
// when it was launched without the spawner).
pid_t SpawnedServerPid(const std::string &out, size_t from = 0) {
    size_t at = out.find("Launched pose_server.py (pid ", from);
    if (at == std::string::npos)
        return -1;
    size_t eol = out.find('\n', at);
    if (out.substr(at, eol - at).find("via the spawner") == std::string::npos)
        return -1;
    return atoi(out.c_str() + at + strlen("Launched pose_server.py (pid "));
}

// Records each run's limits, binds the socket and streams empty v1 frames;
// DC3_STUB_MODE=die-once SIGKILLs the first run 0.5 s after the game connects.
const char *kEnginePoseStub = R"PY(
import os, signal, socket, struct, sys, time
d = os.environ["DC3_STUB_DIR"]
with open(os.path.join(d, "runs"), "a") as f:
    f.write("%d\n" % os.getpid())
run = sum(1 for _ in open(os.path.join(d, "runs")))
with open("/proc/self/limits") as src, open(os.path.join(d, "limits.%d" % run), "w") as dst:
    dst.write(src.read())
path = sys.argv[sys.argv.index("--socket") + 1]
if os.path.exists(path):
    os.unlink(path)
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(path)
srv.listen(1)
c, _ = srv.accept()
if os.environ.get("DC3_STUB_MODE") == "die-once" and run == 1:
    time.sleep(0.5)
    os.kill(os.getpid(), signal.SIGKILL)
n = 0
while True:
    n += 1
    body = struct.pack("<IId", run * 1000 + n, 0, time.time())
    c.sendall(struct.pack("<I", len(body)) + body)
    time.sleep(0.05)
)PY";

class NativePoseSpawnTest : public NativeExitTest {
protected:
    std::string mStub;
    std::vector<std::string> mEnv;

    void SetUp() override {
        NativeExitTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
            return;
        std::string py = SpawnRealPython();
        if (py.empty())
            GTEST_SKIP() << "no python3 to run the stub pose server";
        mStub = mRunDir + "/stub";
        ASSERT_EQ(mkdir(mStub.c_str(), 0755), 0);
        {
            std::ofstream f(mStub + "/stub.py");
            f << kEnginePoseStub;
        }
        {
            std::ofstream f(mStub + "/python3");
            f << "#!/bin/sh\nexec " << py << " \"$DC3_STUB_DIR/stub.py\" \"$@\"\n";
        }
        chmod((mStub + "/python3").c_str(), 0755);
        const char *path = getenv("PATH");
        mEnv = {
            "MILO_HEADLESS=1",
            "DC3_POSE=external", // headless skips the pose server unless DC3_POSE is set
            "DC3_POSE_NO_SPAWN",
            "DC3_POSE_SOCKET=" + mStub + "/pose.sock",
            "DC3_CAMERA_FRAMES=off",
            "DC3_STUB_DIR=" + mStub,
            "PATH=" + mStub + ":" + (path ? path : "/usr/bin:/bin"),
            "DC3_POSE_PYTHON=" + mStub + "/python3", // else the game prefers a venv
        };
    }

    // The engine's spawner (its first child), within `ms`.
    pid_t WaitForSpawner(const Engine &e, int ms) {
        for (int i = 0; i < ms; i++) {
            std::vector<pid_t> kids = ChildrenOf(e.pid);
            if (!kids.empty())
                return kids[0];
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return -1;
    }
};

} // namespace

// The GPD bug, reproduced on any box: the game's RLIMIT_RTTIME is lowered to
// 0:0 after startup, and the pose server it launches must not inherit that.
// Then the game quits, and neither the spawner nor the server outlives it.
TEST_F(NativePoseSpawnTest, ThePoseServerKeepsTheLimitsTheGameStartedWith) {
    struct rlimit own;
    ASSERT_EQ(getrlimit(RLIMIT_RTTIME, &own), 0);
    if (own.rlim_max != RLIM_INFINITY)
        GTEST_SKIP() << "the test runs under RLIMIT_RTTIME hard " << own.rlim_max
                     << ", so dc3-native starts with no limit left to lose";
    Engine e;
    Start(e, mRunDir, mEnv);
    // main() has started (its first line forks the spawner); audio has not.
    ASSERT_TRUE(e.WaitFor("DC3 Native Port - Starting", 20)) << Tail(e.Output());
    std::vector<pid_t> kids = ChildrenOf(e.pid);
    pid_t spawner = kids.empty() ? -1 : kids[0];

    // What PipeWire's module-rt does to the game at audio init.
    struct rlimit zero = { 0, 0 };
    ASSERT_EQ(prlimit(e.pid, RLIMIT_RTTIME, &zero, nullptr), 0) << strerror(errno);
    std::string gameLine = RtTime(ReadAll("/proc/" + std::to_string(e.pid) + "/limits"));
    ASSERT_FALSE(RtTimeUnlimited(gameLine)) << "control: the game's limit did not drop: " << gameLine;
    ASSERT_NE(access((mStub + "/limits.1").c_str(), F_OK), 0)
        << "the pose server ran before the limit dropped; the test proves nothing";

    ASSERT_TRUE(e.WaitFor("Connected to pose server", 120)) << Tail(e.Output());
    std::string out = e.Output();
    pid_t server = SpawnedServerPid(out);
    EXPECT_GT(server, 0) << "the server was not launched through the spawner\n" << Tail(out);
    std::string serverLine = RtTime(ReadAll(mStub + "/limits.1"));
    ASSERT_FALSE(serverLine.empty());
    EXPECT_TRUE(RtTimeUnlimited(serverLine))
        << "the pose server inherited the game's lowered RLIMIT_RTTIME: " << serverLine;
    ASSERT_GT(spawner, 0) << "dc3-native forked no spawner\n" << Tail(out);

    ASSERT_TRUE(e.WaitFor("DC3 Native: Entering main loop", 120)) << Tail(e.Output());
    kill(e.pid, SIGTERM);
    ASSERT_TRUE(e.WaitExit(30)) << Tail(e.Output());
    EXPECT_TRUE(WaitGone(spawner, 5000)) << "the spawner outlived the game";
    if (server > 0)
        EXPECT_TRUE(WaitGone(server, 5000)) << "the pose server outlived the game";
    if (!PidGone(spawner)) kill(spawner, SIGKILL);
    if (server > 0 && !PidGone(server)) kill(server, SIGKILL);
}

// A pose server SIGKILLed mid-session: the game says it crashed with signal 9,
// relaunches it, and tracking resumes.  A SIGKILLed game takes both the
// spawner and the relaunched server with it.
TEST_F(NativePoseSpawnTest, APoseServerKilledMidSessionIsReportedAndRelaunched) {
    mEnv.push_back("DC3_STUB_MODE=die-once");
    Engine e;
    Start(e, mRunDir, mEnv);
    pid_t spawner = WaitForSpawner(e, 20000);
    ASSERT_GT(spawner, 0) << Tail(e.Output());
    ASSERT_TRUE(e.WaitFor("Pose server restarted (pid", 150)) << Tail(e.Output());
    std::string out = e.Output();
    EXPECT_TRUE(Contains(out, "The pose server crashed (signal 9) - restarting it in 1000 ms (restart 1 of 3)"))
        << Tail(out);
    pid_t first = SpawnedServerPid(out);
    size_t second_at = out.find("Launched pose_server.py (pid ", out.find("restart 1 of 3"));
    pid_t second = SpawnedServerPid(out, second_at);
    EXPECT_GT(first, 0);
    ASSERT_GT(second, 0) << Tail(out);
    EXPECT_NE(first, second);
    EXPECT_FALSE(PidGone(second)) << "the relaunched server is not running";

    kill(e.pid, SIGKILL);
    e.WaitExit(10);
    EXPECT_TRUE(WaitGone(spawner, 5000)) << "the spawner outlived a SIGKILLed game";
    EXPECT_TRUE(WaitGone(second, 5000)) << "the pose server outlived a SIGKILLed game";
    if (!PidGone(spawner)) kill(spawner, SIGKILL);
    if (!PidGone(second)) kill(second, SIGKILL);
}
#endif // __linux__ (pose spawner)

#endif // __linux__ || __APPLE__
