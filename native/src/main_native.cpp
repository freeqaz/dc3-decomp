// DC3 Native Port - Entry Point
// Boots the real DC3 engine on x86_64 Linux

#include "App.h"
#include "os/Debug.h"
#include <cstdio>
#include <csignal>
#include <execinfo.h>
#include <unistd.h>

static void SignalHandler(int sig) {
    fprintf(stderr, "\nDC3 Native: Caught signal %d\n", sig);
    void *bt[32];
    int n = backtrace(bt, 32);
    char **syms = backtrace_symbols(bt, n);
    for (int i = 0; i < n; i++) {
        fprintf(stderr, "  [%d] %s\n", i, syms ? syms[i] : "??");
    }
    fflush(stderr);
    _exit(1);
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL); // Disable buffering so we see output before crashes
    signal(SIGSEGV, SignalHandler);
    signal(SIGABRT, SignalHandler);

    printf("DC3 Native Port - Starting...\n");

    App app(argc, argv);

    printf("DC3 Native Port - App constructed, calling Run()...\n");
    app.Run();

    printf("DC3 Native Port - Run() returned, exiting cleanly.\n");
    return 0;
}
