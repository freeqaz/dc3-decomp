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

    _exit(128 + sig);
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL); // Disable buffering so we see output before crashes
    setbuf(stderr, NULL);

    // Use sigaction for reliable signal handling
    struct sigaction sa;
    sa.sa_sigaction = SignalHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);

    printf("DC3 Native Port - Starting...\n");

    App app(argc, argv);

    printf("DC3 Native Port - App constructed, calling Run()...\n");
    app.Run();

    printf("DC3 Native Port - Run() returned, exiting cleanly.\n");
    // Use _exit() to skip global destructors — static destruction order issues
    // cause crashes when ObjDirPtr destructors run after Loader list is destroyed
    _exit(0);
}
