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

#if defined(__linux__) // Xlib, /proc/self/exe, glibc on_exit in the engine

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

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
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
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

namespace {

// A private X server. Skips (via `available`) when Xvfb is not installed.
struct Xvfb {
    pid_t pid = -1;
    std::string display;
    bool available = false;

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
            execlp("Xvfb", "Xvfb", "-displayfd", fd.c_str(), "-screen", "0", "1280x720x24",
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
        }
    }
    ~Xvfb() {
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
template <typename Act>
void RunWindowed(const std::string &runDir, Act act, Engine &e) {
    Xvfb x;
    if (!x.available) GTEST_SKIP() << "Xvfb not installed; the window tests need a private X server";
    Start(e, runDir, {"MILO_HEADLESS", "DISPLAY=" + x.display});
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
    RunWindowed(mRunDir, [](Display *dpy, Window w) {
        XEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.xclient.type = ClientMessage;
        ev.xclient.window = w;
        ev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", False);
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = (long)XInternAtom(dpy, "WM_DELETE_WINDOW", False);
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(dpy, w, False, NoEventMask, &ev);
    }, e);
    if (IsSkipped() || HasFatalFailure()) return;
    std::string out = e.Output();
    ExpectNoCrash(out);
    ASSERT_TRUE(WIFEXITED(e.status)) << Tail(out);
    EXPECT_EQ(WEXITSTATUS(e.status), 0) << Tail(out);
    EXPECT_TRUE(Contains(out, "DC3_EXIT: how=main_loop_returned detail=window_closed code=0"))
        << Tail(out);
}

#endif // __linux__
