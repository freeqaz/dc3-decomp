#include "test_helpers.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <ctime>
#include <dirent.h>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

// Engine headers
#include "os/Debug.h"
#include "os/System.h"
#include "rndobj/Rnd.h"
#include "world/World.h"
#include "char/Char.h"
#include "hamobj/Ham.h"
#include "flow/Flow.h"
#include "ui/PanelDir.h"
#include "ui/UIPanel.h"
#include "ui/UIScreen.h"
#include "ui/UIColor.h"
#include "ui/UIGuide.h"
#include "ui/UIPicture.h"
#include "ui/UITrigger.h"
#include "ui/UILabel.h"
#include "ui/UIComponent.h"
#include "ui/UIButton.h"
#include "ui/UISlider.h"
#include "ui/LabelNumberTicker.h"
#include "ui/LabelShrinkWrapper.h"
#include "ui/UILabelDir.h"
#include "ui/UIList.h"
#include "ui/InlineHelp.h"
#include "movie/TexMovie.h"
#include "synth/SynthSample.h"
#include "synth/Sound.h"
#include "synth/Sfx.h"
#include "synth/FxSendReverb.h"
#include "synth/FxSendEQ.h"
#include "synth/FxSendCompress.h"
#include "synth/FxSendDistortion.h"
#include "synth/FxSendDelay.h"
#include "synth/FxSendBitCrush.h"
#include "synth/FxSendPitchShift.h"
#include "synth/FxSendFlanger.h"
#include "synth/FxSendChorus.h"
#include "synth/FxSendSynapse.h"
#include "synth/FxSendWah.h"
#include "synth/FxSendMeterEffect.h"
#include "synth/Sequence.h"
#include "synth/Emitter.h"
#include "synth/Faders.h"
#include "synth/MidiInstrument.h"
#include "synth/MoggClip.h"
#include "synth/MeterEffectMonitor.h"
#include "synth/ADSR.h"
#include "synth/ThreeDSound.h"
#include "synth/AudioDucker.h"
#include "synth/Synth.h"
#include "synth/StreamReceiver.h"
#include "platform/StreamReceiver_Native.h"
#include "utl/Symbol.h"
#include "utl/MakeString.h"

// Forward declarations from engine
extern Rnd &TheRnd;
extern void NativeDetectDataDir();
extern const char *NativeFindDataDir();
void SetFileChecksumData();

static bool sSymbolInitialized = false;
static bool sEngineInitialized = false;

void EnsureSymbolInit() {
    if (sSymbolInitialized)
        return;
    sSymbolInitialized = true;
    Symbol::PreInit(0x80000, 0x4000);
    InitMakeString();
}

void EngineTeardown() {
    // Rnd::Terminate must run BEFORE SystemTerminate because it uses Symbols,
    // DataArrays, and the object system (e.g. SetName, RELEASE of ObjectDirs).
    // SystemTerminate destroys those subsystems (Symbol::Terminate, DataTerminate),
    // so calling TheRnd.Terminate() after would operate on freed memory.
    TheRnd.Terminate();
    SystemTerminate();

    // Use _exit() to skip C++ static destructors. The WgpuRnd global contains
    // a GpuDevice member with Dawn/WebGPU handles. During static destruction,
    // the order between our WgpuRnd destructor and Dawn's internal singletons
    // is undefined across translation units. Under parallel ctest runs (~50
    // processes), this causes intermittent "double free or corruption" ~4% of
    // the time as heap metadata gets corrupted by the misordered teardown.
    //
    // Since all meaningful cleanup is already done above, and test results
    // have already been reported to stdout, _exit() is safe here. We query
    // GTest for the correct exit code to preserve pass/fail signaling.
    int exitCode = testing::UnitTest::GetInstance()->Failed() ? 1 : 0;
    FinishTestTmp(); // _exit skips the atexit that would
    _exit(exitCode);
}

// The explicit "game data absent" gate. GTEST_SKIP() returns from THIS
// function only, so it lives in a void helper and EnsureEngineInit() reports
// the outcome; the skip itself is recorded on the current test (or, from
// SetUpTestSuite, on the suite, which gtest then skips whole).
static void SkipNoGameData() {
    char cwd[4096];
    const char *env = getenv("DC3_DATA");
    GTEST_SKIP() << kNoGameDataSkipTag << " gen/main_xbox.hdr not under $DC3_DATA ("
                 << (env ? env : "unset")
                 << ") or orig-assets, ../orig-assets, ../../orig-assets, . from cwd "
                 << (getcwd(cwd, sizeof(cwd)) ? cwd : "?")
                 << " -- the engine cannot boot without the DC3 archive";
}

bool EnsureEngineInit() {
    if (sEngineInitialized)
        return true;
    // Checked BEFORE booting, with the boot path's own probe
    // (NativeFindDataDir, System_Native.cpp). Without the archive the boot
    // cannot succeed -- it now exits with "FATAL - no game data", and used to
    // die of SIGFPE -- so on a machine with no game data (the GitHub runner)
    // every engine-backed test skips here instead of taking the process down.
    if (!NativeFindDataDir()) {
        SkipNoGameData();
        return false;
    }
    sEngineInitialized = true;
    sSymbolInitialized = true; // engine init includes symbol init

    printf("=== Test Engine Init ===\n");

    // Force headless mode
    setenv("MILO_HEADLESS", "1", 1);

    // Minimal engine init (same as milo-viewer)
    char *fakeArgv[] = {(char *)"milo-tests", nullptr};
    int fakeArgc = 1;

    SetFileChecksumData();
    SystemPreInit(fakeArgc, fakeArgv, "config/ham_preinit_keep.dta");
    TheRnd.PreInit();
    SystemInit("config/ham_keep.dta");
    TheRnd.Init();

    std::atexit(EngineTeardown);

    // Register subsystem types
    FlowInit();
    CharInit();
    WorldInit();
    HamInit();

    // UI object factories — UIManager::Init() is too heavy for tests
    // (needs SystemConfig, Automator, cameras, etc.), so register types manually.
    REGISTER_OBJ_FACTORY(PanelDir)
    REGISTER_OBJ_FACTORY(UIPanel)
    REGISTER_OBJ_FACTORY(UIScreen)
    REGISTER_OBJ_FACTORY(UIColor)
    REGISTER_OBJ_FACTORY(UIGuide)
    REGISTER_OBJ_FACTORY(UIPicture)
    REGISTER_OBJ_FACTORY(UITrigger)
    REGISTER_OBJ_FACTORY(UILabel)
    REGISTER_OBJ_FACTORY(UIComponent)
    REGISTER_OBJ_FACTORY(UIButton)
    REGISTER_OBJ_FACTORY(UISlider)
    REGISTER_OBJ_FACTORY(LabelNumberTicker)
    REGISTER_OBJ_FACTORY(LabelShrinkWrapper)
    REGISTER_OBJ_FACTORY(UILabelDir)

    // UIList family — factories only (no UIList::Register() which needs a dir context)
    UIList::Init();

    // InlineHelp — PreLoad now implemented
    REGISTER_OBJ_FACTORY(InlineHelp)

    // TexMovie — Load now fixed
    REGISTER_OBJ_FACTORY(TexMovie)

    // Synth subsystem — SynthPreInit creates TheSynth singleton (needed by Sound ctor)
    SynthPreInit();
    // NativeSynth::Init() registers the receiver factory (as the image's
    // StreamReceiver360::Init does); the harness skips Init() because it also
    // opens the audio device, so register the factory here: MoggClip's
    // PostLoad starts a stream to count its channels.
    StreamReceiver::sFactory = StreamReceiverNative::Create;
    SynthSample::Disable();
    SynthSample::Init();
    REGISTER_OBJ_FACTORY(Sound)
    REGISTER_OBJ_FACTORY(Sfx)
    REGISTER_OBJ_FACTORY(SynthEmitter)
    REGISTER_OBJ_FACTORY(FxSendReverb)
    REGISTER_OBJ_FACTORY(FxSendEQ)
    REGISTER_OBJ_FACTORY(FxSendCompress)
    REGISTER_OBJ_FACTORY(FxSendDistortion)
    REGISTER_OBJ_FACTORY(FxSendDelay)
    REGISTER_OBJ_FACTORY(FxSendBitCrush)
    REGISTER_OBJ_FACTORY(FxSendPitchShift)
    REGISTER_OBJ_FACTORY(FxSendFlanger)
    REGISTER_OBJ_FACTORY(FxSendChorus)
    REGISTER_OBJ_FACTORY(FxSendSynapse)
    REGISTER_OBJ_FACTORY(FxSendWah)
    REGISTER_OBJ_FACTORY(FxSendMeterEffect)
    REGISTER_OBJ_FACTORY(WaitSeq)
    REGISTER_OBJ_FACTORY(RandomGroupSeq)
    REGISTER_OBJ_FACTORY(RandomIntervalGroupSeq)
    REGISTER_OBJ_FACTORY(SerialGroupSeq)
    REGISTER_OBJ_FACTORY(ParallelGroupSeq)
    REGISTER_OBJ_FACTORY(SfxSeq)
    REGISTER_OBJ_FACTORY(Fader)
    REGISTER_OBJ_FACTORY(MidiInstrument)
    REGISTER_OBJ_FACTORY(MoggClip)
    REGISTER_OBJ_FACTORY(MeterEffectMonitor)
    REGISTER_OBJ_FACTORY(ADSR)
    REGISTER_OBJ_FACTORY(ThreeDSound)
    REGISTER_OBJ_FACTORY(AudioDuckerTrigger)

    printf("=== Test Engine Init Complete ===\n");
    return true;
}

// ============================================================================
// WriteSyntheticMilo — create a valid uncompressed .milo_xbox file
// ============================================================================
bool WriteSyntheticMilo(const char *path,
                        const std::vector<std::vector<uint8_t>> &chunks) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;

    int numChunks = (int)chunks.size();
    int maxChunkSize = 0;
    for (auto &c : chunks) {
        if ((int)c.size() > maxChunkSize)
            maxChunkSize = (int)c.size();
    }

    // ChunkInfo header: 0x810 bytes, all little-endian on disk
    // (ChunkStream reads it raw on native x86, no endian swap needed)
    uint8_t header[0x810];
    memset(header, 0, sizeof(header));

    // mID = 0xCABEDEAF (uncompressed)
    uint32_t id = 0xCABEDEAF;
    memcpy(&header[0x0], &id, 4);

    // mChunkInfoSize = 0x810
    uint32_t infoSize = 0x810;
    memcpy(&header[0x4], &infoSize, 4);

    // mNumChunks
    uint32_t nc = (uint32_t)numChunks;
    memcpy(&header[0x8], &nc, 4);

    // mMaxChunkSize
    uint32_t mcs = (uint32_t)maxChunkSize;
    memcpy(&header[0xC], &mcs, 4);

    // mChunks[512] — each is the chunk size (uncompressed, no flags)
    for (int i = 0; i < numChunks && i < 512; i++) {
        uint32_t sz = (uint32_t)chunks[i].size();
        memcpy(&header[0x10 + i * 4], &sz, 4);
    }

    fwrite(header, 1, 0x810, f);

    // Write chunk data
    for (auto &c : chunks) {
        if (!c.empty())
            fwrite(c.data(), 1, c.size(), f);
    }

    fclose(f);
    return true;
}

// ============================================================================
// GetTestBikPath — .bik test asset auto-discovery
// ============================================================================

static const char *kBikFixturePaths[] = {
    "/tmp/claude-1000/bik_fixtures/satisfaction_prev.bik",
    "/tmp/claude-1000/bik_fixtures/fire.bik",
    "/tmp/claude-1000/bik_fixtures/campaign_intro.bik",
    "/tmp/claude-1000/bik_fixtures/peak_heliblades.bik",
};

const char *GetTestBikPath() {
    // 1. Check env var
    const char *env = getenv("MILO_TEST_BIK");
    if (env && env[0]) return env;

    // 2. Check pre-extracted fixture files (from ExtractBik.ExtractSmallest)
    for (const char *path : kBikFixturePaths) {
        FILE *f = fopen(path, "rb");
        if (f) {
            fclose(f);
            return path;
        }
    }

    return nullptr;
}

// ============================================================================
// Test temp dirs and logs (np-tmpleak) -- the policy is in test_helpers.h
// ============================================================================

namespace {

struct TmpEntry {
    std::string path;
    bool isDir;
    int owner;              // getpid() of the process that created it
    bool announced = false; // its "kept" line has been printed
};

struct TmpRegistry {
    std::mutex mu;
    std::vector<TmpEntry> entries;
    bool hooked = false;
};

// Leaked on purpose.  It is read by an atexit handler and by EngineTeardown,
// and a static object can already be destroyed by then -- which is how
// TestScratchDir()'s cleanup came to remove nothing at all (test_helpers.h).
TmpRegistry &Registry() {
    static TmpRegistry *r = new TmpRegistry;
    return *r;
}

bool EnvOn(const char *name) {
    const char *v = getenv(name);
    return v && *v && strcmp(v, "0") != 0;
}

// Whether `name` carries `pid`'s token in the shape the helpers make it.
bool NameIsOwnedBy(const std::string &name, int pid, bool isDir) {
    const std::string p = std::to_string(pid);
    if (isDir) {
        if (name.rfind("milo-tests-" + p + "-", 0) == 0)
            return true;
        return name.rfind("dc3-", 0) == 0 && name.find("-p" + p + "-") != std::string::npos;
    }
    return name.find("-p" + p + ".") != std::string::npos;
}

// The guard in front of every removal: an absolute path with no "..", not a
// top-level entry, created by THIS process (a forked child inherits the
// registry and must not delete its parent's dirs), and named with this
// process's pid token.
bool SafeToRemove(const TmpEntry &e) {
    const std::string &p = e.path;
    if (p.empty() || p[0] != '/' || p.find("/..") != std::string::npos)
        return false;
    size_t slash = p.rfind('/');
    if (slash == 0 || slash + 1 >= p.size())
        return false;
    if (e.owner != (int)getpid())
        return false;
    return NameIsOwnedBy(p.substr(slash + 1), e.owner, e.isDir);
}

void RemoveEntry(const TmpEntry &e) {
    if (!SafeToRemove(e))
        return;
    struct stat st;
    if (lstat(e.path.c_str(), &st) != 0)
        return;
    if (e.isDir) {
        if (!S_ISDIR(st.st_mode))
            return;
        std::error_code ec;
        std::filesystem::remove_all(e.path, ec);
    } else if (S_ISREG(st.st_mode)) {
        unlink(e.path.c_str());
    }
}

void PrintKept(const std::string &path, const char *why) {
    if (access(path.c_str(), F_OK) != 0)
        return; // a log that was never written
    fprintf(stderr, "[test-tmp] kept (%s): %s\n", why, path.c_str());
}

void FinishAtExit() { FinishTestTmp(); }

void Register(const std::string &path, bool isDir) {
    TmpRegistry &r = Registry();
    std::lock_guard<std::mutex> lock(r.mu);
    for (const TmpEntry &e : r.entries)
        if (e.path == path)
            return;
    r.entries.push_back({path, isDir, (int)getpid()});
    if (!r.hooked) {
        r.hooked = true;
        atexit(FinishAtExit);
    }
}

// Why everything is being kept, or nullptr to remove it.
const char *KeepReason() {
    if (TestTmpKeepAll())
        return "DC3_KEEP_TEST_TMP=1";
    auto *ut = ::testing::UnitTest::GetInstance();
    if (ut->current_test_info())
        return "the process exited inside a test";
    if (ut->Failed())
        return "this run failed";
    return nullptr;
}

std::string NormalizedBase(const char *dir) {
    std::string b = (dir && *dir) ? dir : "/tmp";
    while (b.size() > 1 && b.back() == '/')
        b.pop_back();
    return b;
}

std::string TryMkdtemp(const std::string &base, const std::string &name) {
    mkdir(base.c_str(), 0755); // /tmp/claude-1000, <build>/tmp: may not exist yet
    std::string tmpl = base + "/" + name + "XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    if (!mkdtemp(buf.data()))
        return std::string();
    return std::string(buf.data());
}

bool HasUpper(const std::string &s) {
    for (char c : s)
        if (c >= 'A' && c <= 'Z')
            return true;
    return false;
}

// The owner pid in a helper-made dir name, or 0 if it is not one:
//   milo-tests-<pid>-<n>          (TestScratchDir)
//   dc3-<prefix>-p<pid>-XXXXXX    (MakeTestTempDir)
long OwnerPidOf(const char *name) {
    static const char kScratch[] = "milo-tests-";
    if (strncmp(name, kScratch, sizeof(kScratch) - 1) == 0) {
        const char *p = name + sizeof(kScratch) - 1;
        char *end = nullptr;
        long pid = strtol(p, &end, 10);
        if (end == p || *p == '-' || *p == '+' || *end != '-' || !end[1])
            return 0;
        for (const char *q = end + 1; *q; q++)
            if (*q < '0' || *q > '9')
                return 0;
        return pid;
    }
    if (strncmp(name, "dc3-", 4) != 0)
        return 0;
    const char *last = strrchr(name, '-');
    if (!last || strlen(last + 1) != 6)
        return 0;
    for (const char *q = last + 1; *q; q++)
        if (!isalnum((unsigned char)*q))
            return 0;
    const char *d = last;
    while (d > name && d[-1] >= '0' && d[-1] <= '9')
        d--;
    // at least "dc3-x" before "-p<digits>"
    if (d == last || d - name < 7 || d[-1] != 'p' || d[-2] != '-')
        return 0;
    return strtol(d, nullptr, 10);
}

// The scratch dir `pid` most recently claimed under one of `bases`: the
// highest n of an existing milo-tests-<pid>-<n> dir that is ours.  A process
// claims the lowest free n, so a higher n is always the newer claim.
std::string NewestScratchDirOf(int pid, const std::vector<std::string> &bases) {
    const uid_t uid = getuid();
    for (const std::string &b : bases) {
        for (int n = 999; n >= 0; n--) {
            std::string d = b + "/milo-tests-" + std::to_string(pid) + "-" + std::to_string(n);
            struct stat st;
            if (lstat(d.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == uid)
                return d;
        }
    }
    return std::string();
}

// Each milo-tests process sweeps once, before its first test.
class TestTmpSweepEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        if (TestTmpKeepAll())
            return;
        std::vector<std::string> bases = {TestTempBase("/tmp"), TestTempBase("/tmp/claude-1000"),
                                          "/tmp", "/tmp/claude-1000"};
        std::sort(bases.begin(), bases.end());
        bases.erase(std::unique(bases.begin(), bases.end()), bases.end());
        for (const std::string &b : bases) {
            int n = SweepDeadTestTmp(b, kTestTmpSweepMinAgeSeconds);
            if (n > 0)
                fprintf(stderr, "[test-tmp] swept %d dir(s) of dead test processes from %s\n", n,
                        b.c_str());
        }
    }
};

::testing::Environment *const sTestTmpSweepEnv =
    ::testing::AddGlobalTestEnvironment(new TestTmpSweepEnvironment);

} // namespace

std::string TestTempBase(const char *fallback) {
    const char *t = getenv("TMPDIR");
    return NormalizedBase((t && *t) ? t : fallback);
}

bool TestTmpKeepAll() { return EnvOn("DC3_KEEP_TEST_TMP") || EnvOn("MILO_TEST_KEEP_SCRATCH"); }

std::string MakeTestTempDir(const char *prefix, const char *fallbackBase) {
    std::string pre = prefix ? prefix : "dc3-tmp";
    if (pre.rfind("dc3-", 0) != 0)
        pre = "dc3-" + pre; // the sweep recognises only dc3-...-p<pid>-XXXXXX
    const std::string name = pre + "-p" + std::to_string((int)getpid()) + "-";
    const std::string fallback = NormalizedBase(fallbackBase);
    const std::string base = TestTempBase(fallback.c_str());
    std::string dir;
    if (base.size() + 1 + name.size() + 6 <= kTestTempDirMaxLen)
        dir = TryMkdtemp(base, name);
    if (dir.empty() && base != fallback)
        dir = TryMkdtemp(fallback, name);
    if (!dir.empty())
        Register(dir, true);
    return dir;
}

std::string TestTempFile(const std::string &stem, const char *ext, const char *fallbackBase) {
    std::string e = (ext && *ext) ? ext : ".log";
    if (e[0] != '.')
        e = "." + e;
    const std::string base = TestTempBase(fallbackBase);
    mkdir(base.c_str(), 0755);
    std::string path = base + "/" + stem + "-p" + std::to_string((int)getpid()) + e;
    Register(path, false);
    return path;
}

std::string TestLogPath(const std::string &stem) {
    auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
    return TestTempFile(info ? stem + "_" + info->name() : stem);
}

void FinishTestTmp() {
    TmpRegistry &r = Registry();
    std::vector<TmpEntry> mine;
    {
        std::lock_guard<std::mutex> lock(r.mu);
        for (const TmpEntry &e : r.entries)
            if (e.owner == (int)getpid())
                mine.push_back(e);
        r.entries.clear();
    }
    const char *why = KeepReason();
    // Newest first: a dir made inside another registered dir goes before it.
    for (auto it = mine.rbegin(); it != mine.rend(); ++it) {
        if (why) {
            if (!it->announced)
                PrintKept(it->path, why);
        } else {
            RemoveEntry(*it);
        }
    }
}

ScopedTestDir::ScopedTestDir(const char *prefix, const char *fallbackBase)
    : mPath(MakeTestTempDir(prefix, fallbackBase)) {}

ScopedTestDir::~ScopedTestDir() {
    if (mPath.empty())
        return;
    TmpRegistry &r = Registry();
    std::lock_guard<std::mutex> lock(r.mu);
    for (size_t i = 0; i < r.entries.size(); i++) {
        TmpEntry &e = r.entries[i];
        if (e.path != mPath)
            continue;
        if (e.owner != (int)getpid())
            return; // a forked child leaving the scope: the dir is its parent's
        if (TestTmpKeepAll() || ::testing::Test::HasFailure()) {
            // Left registered: the run has failed, so the exit policy keeps it.
            PrintKept(e.path, TestTmpKeepAll() ? "DC3_KEEP_TEST_TMP=1" : "this test failed");
            e.announced = true;
            return;
        }
        TmpEntry gone = e;
        r.entries.erase(r.entries.begin() + i);
        RemoveEntry(gone);
        return;
    }
}

int SweepDeadTestTmp(const std::string &base, long minAgeSeconds) {
    DIR *dir = opendir(base.c_str());
    if (!dir)
        return 0;
    const time_t now = time(nullptr);
    const uid_t uid = getuid();
    int removed = 0;
    while (struct dirent *de = readdir(dir)) {
        long pid = OwnerPidOf(de->d_name);
        if (pid <= 1 || pid > INT_MAX)
            continue;
        std::string path = base + "/" + de->d_name;
        struct stat st;
        if (lstat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != uid)
            continue;
        if (now - st.st_mtime < minAgeSeconds)
            continue;
        // Alive -- or EPERM, a live process that is not ours -- keeps its dir.
        if (kill((pid_t)pid, 0) == 0 || errno != ESRCH)
            continue;
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        if (!ec)
            removed++;
    }
    closedir(dir);
    return removed;
}

const std::string &TestScratchDir() {
    // Lower-case only: the engine's file layer case-folds paths (an mkdtemp
    // suffix with capitals is not found again), so a $TMPDIR with capitals
    // falls back to /tmp/claude-1000.  mkdir is the atomic claim; a leftover
    // dir of a dead process with this pid moves on to the next n.  The string
    // is leaked so that nothing that runs at exit can outlive it.
    static const std::string *dir = [] {
        std::vector<std::string> bases;
        std::string base = TestTempBase("/tmp/claude-1000");
        if (!HasUpper(base))
            bases.push_back(base);
        if (base != "/tmp/claude-1000")
            bases.push_back("/tmp/claude-1000");
        // A "threadsafe" death test re-executes this binary: the child has a
        // new pid, runs the test body again (so it wants a scratch dir again)
        // and then _exits inside ASSERT_EXIT, so nothing of its own cleanup
        // runs.  Its dir goes INSIDE the parent's, which the parent removes
        // with its own.  (The 2026-11-15 after-gate left exactly one dir, from
        // ChunkStreamTest.ReadDeadReturnsOnFailedStream's child.)
        if (!std::string(GTEST_FLAG_GET(internal_run_death_test)).empty()) {
            std::string parent = NewestScratchDirOf((int)getppid(), bases);
            if (!parent.empty()) {
                std::string d = parent + "/milo-tests-" + std::to_string((int)getpid()) + "-0";
                if (mkdir(d.c_str(), 0700) == 0) {
                    Register(d, true);
                    return new std::string(d);
                }
            }
        }
        for (const std::string &b : bases) {
            mkdir(b.c_str(), 0755);
            for (int n = 0; n < 1000; n++) {
                std::string d = b + "/milo-tests-" + std::to_string((int)getpid()) + "-" +
                    std::to_string(n);
                if (mkdir(d.c_str(), 0700) == 0) {
                    Register(d, true);
                    return new std::string(d);
                }
                if (errno != EEXIST)
                    break;
            }
        }
        return new std::string();
    }();
    return *dir;
}
