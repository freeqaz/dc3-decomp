// DC3 Native Port - Entry Point
// Boots the real DC3 engine on x86_64 Linux

#include "App.h"
#include "os/Debug.h"
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <execinfo.h>
#include <unistd.h>

static void SignalHandler(int sig, siginfo_t *info, void *) {
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

    _exit(128 + sig);
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL); // Disable buffering so we see output before crashes
    setbuf(stderr, NULL);

    // Use sigaction for reliable signal handling
    struct sigaction sa;
    sa.sa_sigaction = SignalHandler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
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
