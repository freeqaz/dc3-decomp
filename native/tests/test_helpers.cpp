#include "test_helpers.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>

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
#include "utl/Symbol.h"

// Forward declarations from engine
extern Rnd &TheRnd;
extern void NativeDetectDataDir();
void SetFileChecksumData();

static bool sSymbolInitialized = false;
static bool sEngineInitialized = false;

void EnsureSymbolInit() {
    if (sSymbolInitialized)
        return;
    sSymbolInitialized = true;
    Symbol::PreInit(0x80000, 0x4000);
}

void EnsureEngineInit() {
    if (sEngineInitialized)
        return;
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

    // Register subsystem types
    FlowInit();
    CharInit();
    WorldInit();
    HamInit();

    // UI object factories — UIManager::Init() is too heavy for tests
    // (needs SystemConfig, Automator, cameras, etc.), so register key types manually.
    REGISTER_OBJ_FACTORY(PanelDir)
    REGISTER_OBJ_FACTORY(UIPanel)
    REGISTER_OBJ_FACTORY(UIScreen)

    printf("=== Test Engine Init Complete ===\n");
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
