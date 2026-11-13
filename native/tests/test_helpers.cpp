#include "test_helpers.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <sys/stat.h>
#include <filesystem>
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

static void RemoveTestScratchDir();

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
    RemoveTestScratchDir(); // _exit skips the atexit that would
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

static void RemoveTestScratchDir();
static bool sScratchUsed = false;
static int sScratchOwner = 0; // a forked death-test child inherits the atexit; it must not delete

const std::string &TestScratchDir() {
    // Lower-case only: the engine's file layer case-folds paths (an mkdtemp
    // suffix with capitals is not found again).  mkdir is the atomic claim; a
    // leftover dir of a dead process with this pid moves on to the next n.
    // Under /tmp/claude-1000 as the fixed paths it replaces were, so anything
    // a killed run leaves behind is on the box's scratch expiry.
    static std::string dir = [] {
        std::string base = "/tmp/claude-1000";
        mkdir(base.c_str(), 0755);
        for (int n = 0; n < 1000; n++) {
            std::string d = base + "/milo-tests-" + std::to_string((int)getpid()) + "-" +
                std::to_string(n);
            if (mkdir(d.c_str(), 0700) == 0) {
                sScratchUsed = true;
                sScratchOwner = (int)getpid();
                atexit(RemoveTestScratchDir);
                return d;
            }
        }
        return std::string();
    }();
    return dir;
}

// The dir is this process's alone (it created it), so it goes when the process
// does: at exit, and in EngineTeardown, which _exit()s past atexit.
// MILO_TEST_KEEP_SCRATCH=1 keeps it.
static void RemoveTestScratchDir() {
    if (!sScratchUsed || sScratchOwner != (int)getpid() || getenv("MILO_TEST_KEEP_SCRATCH"))
        return;
    std::error_code ec;
    std::filesystem::remove_all(TestScratchDir(), ec);
}
