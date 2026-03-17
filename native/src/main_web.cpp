// DC3 Web Port — Entry Point (Phase 5: Engine Rendering)
// Bootstraps the engine in the browser via Emscripten.
//
// Boot sequence (state machine, driven by emscripten_set_main_loop):
//   BOOT_INIT         → create MEMFS dirs, start bundle download
//   BOOT_FETCHING     → poll until bundle download complete
//   BOOT_ENGINE_INIT  → SystemPreInit + SystemInit + subsystem inits
//   BOOT_GPU_WAIT     → wait for async WebGPU adapter/device
//   BOOT_GPU_READY    → initialize GPU resources (pipelines, buffers)
//   BOOT_RUNNING      → per-frame engine render loop (poll + draw)

#ifdef __EMSCRIPTEN__

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/em_asm.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/WebAssets.h"

// Engine headers
#include "os/Debug.h"
#include "os/System.h"
#include "os/Timer.h"
#include "rndobj/Rnd_NG.h"
#include "utl/MakeString.h"
#include "utl/Cheats.h"
#include "utl/Magnu.h"

// Subsystem headers
#include "char/Char.h"
#include "flow/FlowManager.h"
#include "flow/Flow.h"
#include "game/Game.h"
#include "game/HamUserMgr.h"
#include "hamobj/Ham.h"
#include "hamobj/HamGameData.h"
#include "hamobj/MoveMgr.h"
#include "hamobj/MiniGameMgr.h"
#include "meta_ham/HamSongMgr.h"
#include "meta_ham/MetaPanel.h"
#include "meta_ham/HamUI.h"
#include "movie/Movie.h"
#include "synth/Synth.h"
#include "audio/AudioDevice.h"
#include "ui/UI.h"
#include "world/World.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "obj/Data.h"
#include "obj/Object.h"

// WgpuRnd access
#include "platform/Rnd_Wgpu.h"
extern WgpuRnd *gWgpuRnd;

// ============================================================================
// Web stub manager classes — respond to DTA script messages
// ============================================================================

// SaveLoadMgr stub: {saveload_mgr is_idle} → TRUE, {saveload_mgr activate} → no-op
class WebSaveLoadMgr : public Hmx::Object {
public:
    WebSaveLoadMgr() {}
    virtual DataNode Handle(DataArray *msg, bool b) {
        Symbol type = msg->Sym(1);
        if (type == "is_idle") return DataNode(1);
        if (type == "activate") return DataNode(0);
        if (type == "autosave") return DataNode(0);
        if (type == "get_dialog_msg") return DataNode("");
        if (type == "get_dialog_opt1") return DataNode("");
        if (type == "get_dialog_opt2") return DataNode("");
        if (type == "get_dialog_focus_option") return DataNode(0);
        return Hmx::Object::Handle(msg, b);
    }
};

// ProfileMgr stub: responds to profile queries with safe defaults
class WebProfileMgr : public Hmx::Object {
public:
    WebProfileMgr() {}
    virtual DataNode Handle(DataArray *msg, bool b) {
        Symbol type = msg->Sym(1);
        if (type == "has_active_profile") return DataNode(0);
        if (type == "get_active_profile") return DataNode(0);
        if (type == "get_disable_voice") return DataNode(1);
        if (type == "get_disable_freestyle") return DataNode(0);
        if (type == "has_seen_tutorial") return DataNode(1);
        if (type == "get_num_valid_profiles") return DataNode(0);
        if (type == "get_active_profile_name") return DataNode("");
        return Hmx::Object::Handle(msg, b);
    }
};

// Generic stub that returns 0/false for any message
class WebGenericStub : public Hmx::Object {
public:
    WebGenericStub() {}
    virtual DataNode Handle(DataArray *msg, bool b) {
        return DataNode(0);
    }
};

// Forward declarations from other TUs
extern void NativeSetDataDir(const char *);
extern void InitMakeString();
void SetFileChecksumData();
void SystemPreInit(const char *cmdLine, const char *cfg);
void SystemInit(const char *cfg);
void SystemPoll(bool);

// ============================================================================
// Boot state machine
// ============================================================================

enum BootState {
    BOOT_INIT,
    BOOT_FETCHING,
    BOOT_ENGINE_INIT,
    BOOT_GPU_WAIT,
    BOOT_GPU_READY,
    BOOT_RUNNING,
    BOOT_ERROR,
};

static BootState sBootState = BOOT_INIT;
static int sFrameCount = 0;
static int sGpuWaitFrames = 0;
static bool sGpuReady = false;
static const int kGpuWaitTimeout = 300; // ~5 seconds at 60fps

// ============================================================================
// Main loop — drives the boot state machine
// ============================================================================

static void mainLoop() {
    switch (sBootState) {

    case BOOT_INIT: {
        printf("DC3 Web: downloading assets (bundle)...\n");
        WebAssetsInit();
        WebAssetsFetchBundle();
        sBootState = BOOT_FETCHING;
        break;
    }

    case BOOT_FETCHING: {
        if (!WebAssetsAllDone()) break;

        int ok = WebAssetsCompletedCount();
        int fail = WebAssetsFailedCount();
        printf("DC3 Web: assets ready (%d files, %d errors)\n", ok, fail);
        sBootState = BOOT_ENGINE_INIT;
        break;
    }

    case BOOT_ENGINE_INIT: {
        printf("DC3 Web: initializing engine...\n");

        // Initialize string utilities (must be first)
        InitMakeString();
        SetFileChecksumData();

        // Engine pre-init — loads ham_preinit_keep.dta from MEMFS
        printf("DC3 Web: SystemPreInit...\n");
        SystemPreInit("dc3-web", "config/ham_preinit_keep.dta");

        // Full engine init — loads ham_keep.dta and all subsystems
        printf("DC3 Web: SystemInit...\n");
        SystemInit("config/ham_keep.dta");

        // Enable cache mode so DirLoader::CachedPath() transforms
        // "ui/foo.milo" → "ui/gen/foo.milo_xbox" (matching ark extraction layout)
        DirLoader::SetCacheMode(true);

        // Movie system
        Movie::Init();

        // Register script functions (before Rnd, matching Xbox boot order)
        MagnuInit();

        // Initialize renderer — PreInit registers NG factories (NgEnviron, NgMat, etc.)
        // Must be before SynthInit (overlays are created here)
        printf("DC3 Web: TheRnd.PreInit()...\n");
        TheRnd.PreInit();
        printf("DC3 Web: TheRnd.Init()...\n");
        TheRnd.Init();

        // Audio system (Fader/MoggClip factories — needs overlays from Rnd::Init)
        printf("DC3 Web: SynthInit...\n");
        SynthInit();

        // Flow system - manages game state machine
        printf("DC3 Web: FlowInit...\n");
        FlowInit();

        // Character system
        CharInit();

        // World system
        WorldInit();

        // Ham (game-specific) system
        printf("DC3 Web: HamInit...\n");
        HamInit();

        // Song manager
        printf("DC3 Web: TheHamSongMgr.Init()...\n");
        TheHamSongMgr.Init();

        // Game subsystem inits
        printf("DC3 Web: MetaPanel::Init()...\n");
        MetaPanel::Init();
        printf("DC3 Web: GameInit()...\n");
        GameInit();

        // MoveMgr — real init (creates SuperEasyRemixer, SongLayout, loads category.dta).
        // Must be after HamInit() which registers the SongLayout factory.
        printf("DC3 Web: MoveMgr::Init()...\n");
        MoveMgr::Init(0);
        MiniGameMgr::Init();

        // UI system — use the global TheHamUI (game-specific UIManager subclass)
        printf("DC3 Web: TheHamUI.Init()...\n");
        TheUI = &TheHamUI;
        TheHamUI.Init();

        // Register stub objects for DTA scripts that reference Xbox managers.
        // Objects already registered by engine init (content_mgr, player_provider_*,
        // ui_event_mgr, voice_input_panel) are skipped — the real ones handle those.
        {
            auto registerStub = [](const char *name, Hmx::Object *obj) {
                obj->SetName(name, ObjectDir::Main());
            };
            registerStub("saveload_mgr", new WebSaveLoadMgr());
            registerStub("profile_mgr", new WebProfileMgr());
            registerStub("platform_mgr", new WebGenericStub());
            registerStub("challenges", new WebGenericStub());
            registerStub("speech_mgr", new WebGenericStub());
        }

        // Go to first screen (title screen)
        printf("DC3 Web: GotoFirstScreen...\n");
        TheUI->GotoFirstScreen();

        sBootState = BOOT_GPU_WAIT;
        printf("DC3 Web: waiting for GPU...\n");
        break;
    }

    case BOOT_GPU_WAIT: {
        sGpuWaitFrames++;
        // Poll WebGPU instance to process async callbacks
        if (gWgpuRnd) {
            gWgpuRnd->Gpu().PollEvents();
            if (gWgpuRnd->Gpu().IsReady()) {
                sBootState = BOOT_GPU_READY;
                break;
            }
        }
        // Timeout — proceed without GPU (headless/no-WebGPU mode)
        if (sGpuWaitFrames >= kGpuWaitTimeout) {
            printf("DC3 Web: GPU not ready after %d frames — proceeding without rendering\n", sGpuWaitFrames);
            sGpuReady = false;
            sBootState = BOOT_RUNNING;
        }
        break;
    }

    case BOOT_GPU_READY: {
        printf("DC3 Web: GPU ready, initializing resources...\n");
        gWgpuRnd->InitGpuResources();
        printf("DC3 Web: entering render loop\n");
        sGpuReady = true;
        sBootState = BOOT_RUNNING;
        break;
    }

    case BOOT_RUNNING: {
        sFrameCount++;
        // Export frame count to JS for Playwright test harness
        EM_ASM({ window.dc3FrameCount = $0; }, sFrameCount);
        if (sFrameCount <= 3) {
            printf("DC3 Web: BOOT_RUNNING frame %d start\n", sFrameCount);
            fflush(stdout);
        }

        // Full engine frame: poll systems + draw
        SystemPoll(false);
        if (sFrameCount <= 3) { printf("DC3 Web: after SystemPoll\n"); fflush(stdout); }

        if (TheUI)
            TheUI->Poll();
        if (sFrameCount <= 3) { printf("DC3 Web: after UI Poll\n"); fflush(stdout); }

        TheTaskMgr.Poll();

        if (TheFlowMgr)
            TheFlowMgr->Poll();
        if (sFrameCount <= 3) { printf("DC3 Web: after FlowMgr Poll\n"); fflush(stdout); }

        // Poll synth subsystem — drives SynthPollable (StandardStream/VorbisReader)
        if (TheSynth)
            TheSynth->Poll();

        // Pump audio: mix all sources and push to AudioWorklet ring buffer
        AudioDevice::GetInstance().PumpAudio();

        // Draw (skip if GPU not available)
        if (sGpuReady) {
            TheRnd.BeginDrawing();
            if (sFrameCount <= 3) { printf("DC3 Web: after BeginDrawing\n"); fflush(stdout); }
            if (TheUI)
                TheUI->Draw();
            if (sFrameCount <= 3) { printf("DC3 Web: after UI Draw\n"); fflush(stdout); }
            TheRnd.EndDrawing();
            if (sFrameCount <= 3) { printf("DC3 Web: after EndDrawing\n"); fflush(stdout); }
        }

        if (sFrameCount == 1 || sFrameCount % 300 == 0) {
            printf("DC3 Web: frame %d\n", sFrameCount);
            fflush(stdout);
        }
        break;
    }

    case BOOT_ERROR:
        break;
    }
}

// ============================================================================
// Entry point
// ============================================================================

int main(int argc, char **argv) {
    printf("DC3 Web Port — Initializing\n");

    // Set MEMFS data directory for File_Web.cpp
    NativeSetDataDir("/data");

    emscripten_set_main_loop(mainLoop, 0, true);
    return EXIT_SUCCESS;
}

#endif // __EMSCRIPTEN__
