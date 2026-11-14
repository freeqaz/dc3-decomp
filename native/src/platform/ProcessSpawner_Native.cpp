// ProcessSpawner_Native.cpp -- see ProcessSpawner_Native.h for why.
#include "ProcessSpawner_Native.h"

#ifdef __EMSCRIPTEN__

namespace NativeSpawner {
bool Init() { return false; }
bool Active() { return false; }
pid_t SpawnerPid() { return -1; }
pid_t Spawn(const std::vector<std::string> &) { return -1; }
bool TryReap(pid_t, int *) { return false; }
void Kill(pid_t, int) {}
void ShutdownForTest() {}
} // namespace NativeSpawner

#else

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <poll.h>
#include <set>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

extern char **environ;

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 // macOS: SO_NOSIGPIPE is set on the socket instead
#endif

namespace {

// Wire: every message is [u32 length of what follows][u32 type][payload],
// native byte order (both ends are the same binary).
enum MsgType : uint32_t {
    kMsgSpawn = 1,   // game -> spawner: u32 argc, u32 envc, then cwd, argv..., env... as NUL-terminated strings
    kMsgKill = 2,    // game -> spawner: i32 pid, i32 signal
    kMsgSpawned = 101, // spawner -> game: i32 pid (or -errno)
    kMsgExited = 102,  // spawner -> game: i32 pid, i32 waitpid status
};

const uint32_t kMaxMessage = 1u << 20;

bool WriteAll(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

bool ReadAll(int fd, void *buf, size_t len) {
    char *p = (char *)buf;
    while (len > 0) {
        ssize_t n = read(fd, p, len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

bool SendMsg(int fd, uint32_t type, const void *payload, size_t len) {
    std::string buf;
    uint32_t total = (uint32_t)(len + 4);
    buf.append((const char *)&total, 4);
    buf.append((const char *)&type, 4);
    buf.append((const char *)payload, len);
    return WriteAll(fd, buf.data(), buf.size());
}

bool RecvMsg(int fd, uint32_t &type, std::string &payload) {
    uint32_t total = 0;
    if (!ReadAll(fd, &total, 4) || total < 4 || total > kMaxMessage)
        return false;
    if (!ReadAll(fd, &type, 4))
        return false;
    payload.resize(total - 4);
    return payload.empty() || ReadAll(fd, &payload[0], payload.size());
}

void SetCloexec(int fd) {
    int flags = fcntl(fd, F_GETFD);
    if (flags >= 0)
        fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}

// ---- The spawner process ---------------------------------------------------

volatile sig_atomic_t sSpawnerTerm = 0;
void SpawnerTermHandler(int) { sSpawnerTerm = 1; }

// In the helper, between fork and exec: undo everything the spawner changed
// about itself, so the helper starts as a child of the game always did.
[[noreturn]] void ExecHelper(int sock, pid_t spawner, const std::string &cwd,
                             std::vector<char *> &argv, std::vector<char *> &env) {
#ifdef __linux__
    // If the spawner dies -- even by SIGKILL -- the helper is told to go.
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (getppid() != spawner)
        _exit(1);
#else
    (void)spawner;
#endif
    close(sock);
    for (int sig = 1; sig < NSIG; sig++)
        signal(sig, SIG_DFL);
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);
    if (!cwd.empty() && chdir(cwd.c_str()) != 0)
        perror("spawner: chdir");
    // execvp searches the PATH of `environ`, so install the game's environment
    // first: a caller that changed PATH since Init() gets its own PATH.
    environ = env.data();
    execvp(argv[0], argv.data());
    fprintf(stderr, "spawner: could not run %s: %s\n", argv[0], strerror(errno));
    _exit(127);
}

void HandleSpawn(int sock, pid_t self, const std::string &payload, std::set<pid_t> &live) {
    int32_t result = -EINVAL;
    uint32_t argc = 0, envc = 0;
    if (payload.size() >= 8) {
        memcpy(&argc, payload.data(), 4);
        memcpy(&envc, payload.data() + 4, 4);
    }
    // Split the NUL-terminated strings after the two counts.
    std::vector<std::string> strs;
    size_t at = 8;
    while (at < payload.size()) {
        size_t nul = payload.find('\0', at);
        if (nul == std::string::npos)
            break;
        strs.push_back(payload.substr(at, nul - at));
        at = nul + 1;
    }
    if (argc > 0 && strs.size() == 1 + (size_t)argc + envc) {
        std::vector<char *> argv, env;
        for (uint32_t i = 0; i < argc; i++)
            argv.push_back(&strs[1 + i][0]);
        argv.push_back(nullptr);
        for (uint32_t i = 0; i < envc; i++)
            env.push_back(&strs[1 + argc + i][0]);
        env.push_back(nullptr);
        pid_t pid = fork();
        if (pid == 0)
            ExecHelper(sock, self, strs[0], argv, env);
        if (pid > 0) {
            live.insert(pid);
            result = pid;
        } else {
            result = -errno;
        }
    }
    SendMsg(sock, kMsgSpawned, &result, sizeof(result));
}

// Reap every helper that has exited; tell the game unless it has disowned it.
void ReapHelpers(int sock, std::set<pid_t> &live, std::set<pid_t> &disowned) {
    for (;;) {
        int status = 0;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0)
            return;
        live.erase(pid);
        if (disowned.erase(pid))
            continue;
        int32_t msg[2] = { pid, status };
        SendMsg(sock, kMsgExited, msg, sizeof(msg));
    }
}

[[noreturn]] void ShutDown(std::set<pid_t> &live) {
    for (pid_t pid : live)
        kill(pid, SIGTERM);
    // Up to 2 s for them to go on their own (pose_server.py unlinks its
    // socket on SIGTERM), then SIGKILL.
    for (int i = 0; i < 40 && !live.empty(); i++) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            live.erase(pid);
            continue;
        }
        if (pid < 0 && errno == ECHILD)
            break;
        usleep(50 * 1000);
    }
    for (pid_t pid : live) {
        kill(pid, SIGKILL);
        int status;
        waitpid(pid, &status, 0);
    }
    _exit(0);
}

[[noreturn]] void SpawnerMain(int sock, pid_t game) {
#ifdef __linux__
    prctl(PR_SET_NAME, "dc3-spawner");
    prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
    // Orderly exit on SIGTERM (and on PR_SET_PDEATHSIG's SIGTERM).  A terminal
    // Ctrl+C reaches the whole process group: the game quits on it, the helper
    // gets its own copy; the spawner itself waits for the game's EOF so it can
    // still reap the helper.
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SpawnerTermHandler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr);
    signal(SIGINT, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_DFL);
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);

    // Hold nothing of the game's but stdio and the socket.
    long maxFd = sysconf(_SC_OPEN_MAX);
    if (maxFd < 0 || maxFd > 65536)
        maxFd = 65536;
    for (int fd = 3; fd < maxFd; fd++)
        if (fd != sock)
            close(fd);

    pid_t self = getpid();
    std::set<pid_t> live, disowned;
    for (;;) {
        if (sSpawnerTerm || getppid() != game)
            ShutDown(live);
        ReapHelpers(sock, live, disowned);

        struct pollfd pfd = { sock, POLLIN, 0 };
        int r = poll(&pfd, 1, 100);
        if (r < 0 && errno != EINTR)
            ShutDown(live);
        if (r <= 0)
            continue;
        uint32_t type = 0;
        std::string payload;
        if (!RecvMsg(sock, type, payload))
            ShutDown(live); // EOF: the game has exited
        if (type == kMsgSpawn) {
            HandleSpawn(sock, self, payload, live);
        } else if (type == kMsgKill && payload.size() == 8) {
            int32_t msg[2];
            memcpy(msg, payload.data(), 8);
            if (live.count(msg[0])) { // never signal a pid we did not launch
                disowned.insert(msg[0]);
                kill(msg[0], msg[1]);
            }
        }
    }
}

// ---- The game side ---------------------------------------------------------

std::mutex sMutex;
int sSock = -1;
pid_t sSpawner = -1;
bool sWarnedFallback = false;
std::set<pid_t> sOwned;             // helpers launched through the spawner, not yet reported
std::set<pid_t> sDirect;            // helpers launched by the fallback fork
std::map<pid_t, int> sExited;       // exit statuses relayed but not yet collected

// The spawner went away: close up, and report its helpers as PDEATHSIG'd.
void LostSpawnerLocked() {
    if (sSock >= 0) {
        fprintf(stderr, "NativeSpawner: lost the spawner process (pid %d)\n", (int)sSpawner);
        close(sSock);
        sSock = -1;
    }
    if (sSpawner > 0) {
        int status;
        waitpid(sSpawner, &status, WNOHANG);
        sSpawner = -1;
    }
    for (pid_t pid : sOwned)
        sExited[pid] = SIGTERM; // WIFSIGNALED, WTERMSIG == SIGTERM
    sOwned.clear();
}

// Handle one message from the spawner; `spawned` receives a kMsgSpawned reply.
bool ReadOneLocked(int32_t *spawned) {
    uint32_t type = 0;
    std::string payload;
    if (!RecvMsg(sSock, type, payload)) {
        LostSpawnerLocked();
        return false;
    }
    if (type == kMsgExited && payload.size() == 8) {
        int32_t msg[2];
        memcpy(msg, payload.data(), 8);
        if (sOwned.erase(msg[0]))
            sExited[msg[0]] = msg[1];
    } else if (type == kMsgSpawned && payload.size() == 4 && spawned) {
        memcpy(spawned, payload.data(), 4);
    }
    return true;
}

void DrainLocked() {
    while (sSock >= 0) {
        struct pollfd pfd = { sSock, POLLIN, 0 };
        if (poll(&pfd, 1, 0) <= 0)
            return;
        if (!ReadOneLocked(nullptr))
            return;
    }
}

pid_t DirectSpawn(const std::vector<std::string> &args) {
    if (!sWarnedFallback) {
        sWarnedFallback = true;
        fprintf(stderr, "NativeSpawner: no spawner process (Init() not called); launching %s "
                        "directly, so it inherits this process's current limits\n",
                args[0].c_str());
    }
    std::vector<char *> argv;
    for (const std::string &a : args)
        argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid = fork();
    if (pid == 0) {
        execvp(argv[0], argv.data());
        fprintf(stderr, "could not run %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    if (pid > 0)
        sDirect.insert(pid);
    return pid;
}

} // namespace

namespace NativeSpawner {

bool Init() {
    std::lock_guard<std::mutex> lock(sMutex);
    if (sSock >= 0)
        return true;
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        perror("NativeSpawner: socketpair");
        return false;
    }
    SetCloexec(sv[0]);
    SetCloexec(sv[1]);
    pid_t game = getpid();
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) {
        perror("NativeSpawner: fork");
        close(sv[0]);
        close(sv[1]);
        return false;
    }
    if (pid == 0) {
        close(sv[0]);
        SpawnerMain(sv[1], game);
    }
    close(sv[1]);
    sSock = sv[0];
    sSpawner = pid;
    return true;
}

bool Active() {
    std::lock_guard<std::mutex> lock(sMutex);
    return sSock >= 0;
}

pid_t SpawnerPid() {
    std::lock_guard<std::mutex> lock(sMutex);
    return sSock >= 0 ? sSpawner : -1;
}

pid_t Spawn(const std::vector<std::string> &argv) {
    if (argv.empty()) {
        errno = EINVAL;
        return -1;
    }
    std::lock_guard<std::mutex> lock(sMutex);
    if (sSock < 0)
        return DirectSpawn(argv);

    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd)))
        cwd[0] = '\0';
    uint32_t envc = 0;
    for (char **e = environ; e && *e; e++)
        envc++;
    uint32_t argc = (uint32_t)argv.size();
    std::string payload;
    payload.append((const char *)&argc, 4);
    payload.append((const char *)&envc, 4);
    payload.append(cwd).push_back('\0');
    for (const std::string &a : argv)
        payload.append(a).push_back('\0');
    for (char **e = environ; e && *e; e++)
        payload.append(*e).push_back('\0');
    if (payload.size() + 4 > kMaxMessage || !SendMsg(sSock, kMsgSpawn, payload.data(), payload.size())) {
        LostSpawnerLocked();
        return DirectSpawn(argv);
    }
    int32_t spawned = 0;
    while (spawned == 0) {
        if (!ReadOneLocked(&spawned))
            return DirectSpawn(argv);
    }
    if (spawned < 0) {
        errno = -spawned;
        return -1;
    }
    sOwned.insert(spawned);
    return spawned;
}

bool TryReap(pid_t pid, int *status) {
    std::lock_guard<std::mutex> lock(sMutex);
    if (sDirect.count(pid)) {
        int st = 0;
        if (waitpid(pid, &st, WNOHANG) == pid) {
            sDirect.erase(pid);
            if (status)
                *status = st;
            return true;
        }
        return false;
    }
    DrainLocked();
    auto it = sExited.find(pid);
    if (it == sExited.end())
        return false;
    if (status)
        *status = it->second;
    sExited.erase(it);
    return true;
}

void Kill(pid_t pid, int sig) {
    std::lock_guard<std::mutex> lock(sMutex);
    if (sDirect.count(pid)) {
        kill(pid, sig);
        int st;
        if (waitpid(pid, &st, WNOHANG) == pid)
            sDirect.erase(pid);
        return;
    }
    sExited.erase(pid);
    if (!sOwned.erase(pid) || sSock < 0)
        return;
    int32_t msg[2] = { pid, sig };
    if (!SendMsg(sSock, kMsgKill, msg, sizeof(msg)))
        LostSpawnerLocked();
}

void ShutdownForTest() {
    std::lock_guard<std::mutex> lock(sMutex);
    if (sSock < 0)
        return;
    close(sSock);
    sSock = -1;
    if (sSpawner > 0) {
        int status;
        waitpid(sSpawner, &status, 0);
        sSpawner = -1;
    }
    sOwned.clear();
    sExited.clear();
}

} // namespace NativeSpawner

#endif // __EMSCRIPTEN__
