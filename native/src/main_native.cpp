// DC3 Native Port - Entry Point
// Boots the real DC3 engine on x86_64 Linux

#include "App.h"
#include "os/Debug.h"
#include "StubTrace.h"
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <csetjmp>
#include <execinfo.h>
#include <unistd.h>
#include <cstring>
#include <ctime>
#include <atomic>
#include <thread>
#include <chrono>
#include <fcntl.h>

// ---------------------------------------------------------------------------
// Exit diagnostics — make every way this process can die self-identifying.
//
// Why this exists: long unattended headless runs were "just disappearing" with
// no crash text, and the exit code carried no information to tell the cases
// apart. Every intentional shutdown path in the native port funnels into
// _exit(0)/exit(0):
//
//   * headless frame cap reached      -> break -> _exit(0)              rc 0
//   * DTA `{exit}` (DataFunc.cpp)     -> TheDebug.Exit(0,true)          rc 0
//   * Debug::Exit(1,true) from a FAIL -> XLaunchNewImage() -> exit(0)   rc 0 (!)
//   * App::~App()                     -> TheDebug.Exit(0,true)          rc 0
//
// so "rc 0" meant anything from "ran to completion" to "a MILO_ASSERT killed
// it". Worse, a run that hit the frame cap printed a *success*-shaped line
// ("engine stable!") buried under megabytes of DC3_TEL telemetry, so a
// truncated capture was indistinguishable from a complete one.
//
// Everything below writes ONE greppable line, `DC3_EXIT: ...`, to stderr on
// every exit path, plus a periodic `DC3_HEARTBEAT:` line carrying uptime/RSS so
// a leak or a stall is visible in the artifact itself. Grep the tail of any run
// log for `DC3_EXIT:` — if it is absent, the process was killed by an
// uncatchable signal (SIGKILL, i.e. the OOM killer or `kill -9`) and the last
// `DC3_HEARTBEAT:` line dates the death and reports the RSS at the time.
// ---------------------------------------------------------------------------

// Set by App::RunWithoutDebugging just before the native main loop returns, so
// the exit record can name *why* the loop ended rather than guessing. Defined in
// src/App.cpp (under HX_NATIVE) so test binaries that link App.cpp without this
// translation unit still resolve it.
extern const char *gDc3ExitReason;

static time_t sStartTime = 0;
static std::atomic<bool> sExitReported{false};

// Current RSS in kB, or -1. Reads /proc/self/statm (a single small read) rather
// than parsing /proc/self/status, so it is cheap enough to call every heartbeat.
static long Dc3RssKb() {
    int fd = open("/proc/self/statm", O_RDONLY);
    if (fd < 0) return -1;
    char buf[128];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    long totalPages = 0, rssPages = 0;
    if (sscanf(buf, "%ld %ld", &totalPages, &rssPages) != 2) return -1;
    return rssPages * (sysconf(_SC_PAGESIZE) / 1024);
}

// Write the single terminal record. `how` names the mechanism, `detail` the
// specific cause. Safe to call more than once — only the first call reports, so
// an atexit hook firing after an explicit call does not double-log.
// asyncSafe=true restricts this to write(2) only, for use from a signal handler.
static void Dc3ReportExit(const char *how, const char *detail, int code, bool asyncSafe) {
    bool expected = false;
    if (!sExitReported.compare_exchange_strong(expected, true)) return;

    long uptime = sStartTime ? (long)(time(nullptr) - sStartTime) : -1;
    char buf[512];
    int len = snprintf(buf, sizeof(buf),
        "\nDC3_EXIT: how=%s detail=%s code=%d uptime_s=%ld rss_kb=%ld pid=%d\n",
        how, detail ? detail : "-", code, uptime,
        asyncSafe ? -1L : Dc3RssKb(), (int)getpid());
    if (len > 0) write(STDERR_FILENO, buf, (size_t)len);
}

// Catches exit()/quick paths we do not control — notably XLaunchNewImage(),
// which the Xbox code calls from Debug::Exit and which the native shim
// implements as a bare exit(0). Without this hook that route is invisible.
static void Dc3AtExitHook() {
    Dc3ReportExit("exit_runtime",
                  gDc3ExitReason ? gDc3ExitReason : "exit()_called_no_reason_recorded",
                  0, false);
}

// Termination signals. These are exactly what an OOM-killed parent shell or a
// closing tmux session delivers, and previously they killed the process with no
// record at all — the single most likely explanation for a run that "vanished".
// SIGKILL cannot be caught; its signature is a log with heartbeats and no
// DC3_EXIT line.
static void Dc3TermSignalHandler(int sig) {
    const char *name = (sig == SIGTERM) ? "SIGTERM" :
                       (sig == SIGINT)  ? "SIGINT"  :
                       (sig == SIGHUP)  ? "SIGHUP"  :
                       (sig == SIGQUIT) ? "SIGQUIT" :
                       (sig == SIGPIPE) ? "SIGPIPE" : "SIGNAL";
    Dc3ReportExit("terminating_signal", name, 128 + sig, true);
    _exit(128 + sig);
}

// Periodic liveness + memory record. Interval in seconds via DC3_HEARTBEAT_S
// (default 60; set 0 to disable). Runs detached so it cannot hold up shutdown.
static void Dc3StartHeartbeat() {
    int interval = 60;
    if (const char *env = getenv("DC3_HEARTBEAT_S")) interval = atoi(env);
    if (interval <= 0) return;

    std::thread([interval]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(interval));
            fprintf(stderr, "DC3_HEARTBEAT: uptime_s=%ld rss_kb=%ld\n",
                    (long)(time(nullptr) - sStartTime), Dc3RssKb());
        }
    }).detach();
}

// Recovery jump buffer for crash-resilient draw calls.
// When gDrawJmpBufSet is true and a SIGSEGV occurs, we longjmp back
// to the draw call site instead of terminating. This lets the engine
// survive renderer crashes during partially-loaded scenes.
sigjmp_buf gDrawJmpBuf;
bool gDrawJmpBufSet = false;

static void SignalHandler(int sig, siginfo_t *info, void *) {
    // If we're inside a draw call, recover instead of crashing
    if (sig == SIGSEGV && gDrawJmpBufSet) {
        gDrawJmpBufSet = false;
        siglongjmp(gDrawJmpBuf, 1);
    }

    // Use write() and backtrace_symbols_fd — async-signal-safe
    const char *signame = (sig == SIGSEGV) ? "SIGSEGV" :
                          (sig == SIGABRT) ? "SIGABRT" :
                          (sig == SIGBUS)  ? "SIGBUS"  : "SIGNAL";
    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "\nDC3 Native: Caught %s (signal %d) at address %p\n",
        signame, sig, info ? info->si_addr : nullptr);
    write(STDERR_FILENO, buf, len);

    void *bt[64];
    int n = backtrace(bt, 64);
    backtrace_symbols_fd(bt, n, STDERR_FILENO);

    // If stub tracing is on and DC3_STUB_TRACE_DUMP names a file, persist the
    // ranked stub-hit worklist accumulated up to this crash. dc3-native currently
    // crashes in a downstream Flow/ObjRef cascade on the first UI poll (a separate
    // pre-existing native bug), so /api/stubs cannot be polled from a live boot;
    // this captures the real boot-path stub hits anyway. Not strictly
    // async-signal-safe (it allocates), but this is a terminal crash-dump path —
    // losing the data is the only alternative.
    const char* dumpPath = getenv("DC3_STUB_TRACE_DUMP");
    if (dumpPath && dumpPath[0] && ::dc3::gStubTraceEnabled) {
        long ndistinct = ::dc3::StubTraceDump::DumpToFile(dumpPath);
        char dbuf[256];
        int dlen = snprintf(dbuf, sizeof(dbuf),
            "DC3 Native: wrote %ld distinct stub hits to %s\n", ndistinct, dumpPath);
        write(STDERR_FILENO, dbuf, dlen);
    }

    Dc3ReportExit("fatal_signal", signame, 128 + sig, true);
    _exit(128 + sig);
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL); // Disable buffering so we see output before crashes
    setbuf(stderr, NULL);

    sStartTime = time(nullptr);
    atexit(Dc3AtExitHook);

    // Use sigaction for reliable signal handling
    struct sigaction sa;
    sa.sa_sigaction = SignalHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);

    // Termination signals get a record too. Previously an external kill (a
    // dying parent shell, a tmux teardown, `kill` from a wrapper script) ended
    // the process with nothing in the log at all, which is precisely the
    // "process is simply gone, no message" signature we could not explain.
    struct sigaction st;
    memset(&st, 0, sizeof(st));
    st.sa_handler = Dc3TermSignalHandler;
    sigemptyset(&st.sa_mask);
    st.sa_flags = 0;
    sigaction(SIGTERM, &st, nullptr);
    sigaction(SIGINT, &st, nullptr);
    sigaction(SIGHUP, &st, nullptr);
    sigaction(SIGQUIT, &st, nullptr);

    Dc3StartHeartbeat();

    printf("DC3 Native Port - Starting...\n");

    App app(argc, argv);

    printf("DC3 Native Port - App constructed, calling Run()...\n");
    app.Run();

    printf("DC3 Native Port - Run() returned, exiting cleanly.\n");
    Dc3ReportExit("main_loop_returned",
                  gDc3ExitReason ? gDc3ExitReason : "unknown", 0, false);
    // Use _exit() to skip global destructors — static destruction order issues
    // cause crashes when ObjDirPtr destructors run after Loader list is destroyed
    _exit(0);
}
