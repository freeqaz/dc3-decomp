#include "App.h"
#ifdef HX_NATIVE
#include <algorithm>
#include <csetjmp>
#include "telemetry/GameplayTelemetry.h"
#if !defined(__EMSCRIPTEN__)
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
extern sigjmp_buf gDrawJmpBuf;
extern bool gDrawJmpBufSet;
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
#include "ui/UIPanel.h"
#include "ui/PanelDir.h"
#include "rndobj/Dir.h"
#include "rndobj/Text.h"
#include "world/Dir.h"
#include "char/Character.h"
#include "char/FileMerger.h"
#include "world/LightPreset.h"
#include "world/LightPresetManager.h"
#include "world/CameraManager.h"
#include "world/CameraShot.h"
#include "rndobj/Cam.h"
#include "hamobj/HamDirector.h"
#include "rndobj/Lit.h"
#include "meta_ham/AppLabel.h"
#include "meta_ham/HamUI.h"
#include "synth/StandardStream.h"
#include "synth/VorbisReader.h"
#include "os/BufFile.h"
#include "meta_ham/HamSongMgr.h"
#include "meta_ham/HamSongMetadata.h"
#include "platform/TransparentQueue.h"
#include "rndobj/BaseMaterial.h"
#include "rndobj/Mat.h"
#include "rndobj/TexRenderer.h"
#include "hamobj/HamGameData.h"
#include "obj/DirLoader.h"
#include "ui/UILabel.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/NativeSettings.h"
#include "utl/Locale.h"
#ifdef HX_IMGUI
#include "gfx/ImGuiBackend.h"
#include "platform/DebugPanel.h"
#include <imgui.h>
#endif
#ifdef __EMSCRIPTEN__
#include "audio/AudioDevice.h"
#endif
#if !defined(__EMSCRIPTEN__)
extern GLFWwindow *gNativeWindow;
#endif
#ifdef DC3_HTTP_SERVER
#include "platform/HttpServer.h"
extern void HttpServerInit();
extern void HttpServerShutdown();
#endif
// gNativeHudDir removed — HUD loaded by GameModeMerger.fm via FileMerger pipeline.
// DTA enter handler (hud_objects.dta:162) sets $hud_panel automatically.

// ---------------------------------------------------------------------------
// Native-only "smart stubs" for Xbox manager objects that DTA scripts reference.
// These replace bare Hmx::Object stubs with classes that return sensible defaults
// so DTA handlers (set_sink, has_active_profile, is_idle, etc.) execute correctly
// instead of silently failing.
// ---------------------------------------------------------------------------

// SaveLoadManager stub — DTA polls {saveload_mgr is_idle} after {saveload_mgr activate}
// to fire saveload_complete. We're always idle (no save system on native).
class NativeSaveLoadStub : public Hmx::Object {
public:
    NativeSaveLoadStub() {}
    virtual DataNode Handle(DataArray *msg, bool rev) {
        Symbol sym = msg->Sym(1);
        // DTA: {saveload_mgr activate} — start save/load process
        if (sym == "activate") return 0;
        // DTA: {saveload_mgr is_idle} — always idle, no save in progress
        if (sym == "is_idle") return DataNode(1);
        // DTA: {saveload_mgr is_initial_load_done} — always done
        if (sym == "is_initial_load_done") return DataNode(1);
        // DTA: {saveload_mgr is_autosave_enabled ...}
        if (sym == "is_autosave_enabled") return DataNode(0);
        if (sym == "autosave") return DataNode(0);
        return Hmx::Object::Handle(msg, rev);
    }
};

// ProfileMgr stub — DTA queries profile state for tutorial gating, content unlocks,
// and save routing. Returns "seen all tutorials, everything unlocked, no voice."
class NativeProfileMgrStub : public Hmx::Object {
public:
    NativeProfileMgrStub() {}
    virtual DataNode Handle(DataArray *msg, bool rev) {
        Symbol sym = msg->Sym(1);
        // Profile existence — no real profiles on native
        if (sym == "has_active_profile") return DataNode(0);
        if (sym == "has_active_profile_no_override") return DataNode(0);
        if (sym == "get_active_profile") return DataNode(0);
        if (sym == "get_non_active_profile") return DataNode(0);
        if (sym == "get_num_valid_profiles") return DataNode(0);
        // Tutorials — pretend all seen (skip tutorial flows)
        if (sym == "has_seen_tutorial") return DataNode(1);
        if (sym == "mark_tutorial_seen") return DataNode(0);
        // Content — unlock everything
        if (sym == "is_content_unlocked") return DataNode(1);
        if (sym == "is_difficulty_unlocked") return DataNode(1);
        // Voice — disabled (no Kinect microphone)
        if (sym == "get_disable_voice") return DataNode(1);
        if (sym == "get_disable_voice_commander") return DataNode(1);
        if (sym == "get_disable_voice_pause") return DataNode(1);
        if (sym == "get_disable_voice_practice") return DataNode(1);
        if (sym == "get_show_voice_tip") return DataNode(0);
        if (sym == "is_voice_commander_suboptimal") return DataNode(1);
        // Profile management no-ops
        if (sym == "clear_critical_profile") return DataNode(0);
        if (sym == "set_critical_profile") return DataNode(0);
        if (sym == "pose_found") return DataNode(0);
        if (sym == "on_player_name_change") return DataNode(0);
        // Audio defaults
        if (sym == "get_music_volume") return DataNode(8);
        if (sym == "get_fx_volume") return DataNode(8);
        if (sym == "get_crowd_volume") return DataNode(8);
        if (sym == "get_venue_preference") return DataNode(Symbol("default"));
        // Settings
        if (sym == "get_overscan") return DataNode(0);
        if (sym == "get_mono") return DataNode(0);
        if (sym == "get_disable_photos") return DataNode(0);
        if (sym == "get_disable_freestyle") return DataNode(0);
        if (sym == "get_no_flashcards") return DataNode(0);
        // Native-only settings
        if (sym == "get_camera_blend") return DataNode(NativeSettings::Get().cameraBlend ? 1 : 0);
        if (sym == "toggle_camera_blend") {
            NativeSettings::Get().cameraBlend = !NativeSettings::Get().cameraBlend;
            return DataNode(0);
        }
#ifdef HX_IMGUI
        if (sym == "get_debug_panel") return DataNode(DebugPanel::IsVisible() ? 1 : 0);
        if (sym == "toggle_debug_panel") {
            DebugPanel::Toggle();
            return DataNode(0);
        }
#endif
        if (sym == "has_finished_campaign") return DataNode(0);
        if (sym == "get_all_unlocked") return DataNode(0);
        if (sym == "needs_upload") return DataNode(0);
        if (sym == "global_options_needs_save") return DataNode(0);
        if (sym == "is_any_profile_signed_into_live") return DataNode(0);
        // Player count/outfit/crew — DTA multiuser handlers may query these
        if (sym == "get_num_players") return DataNode(2);
        if (sym == "get_player_outfit") return DataNode(Symbol(""));
        if (sym == "get_player_crew") return DataNode(Symbol(""));
        return Hmx::Object::Handle(msg, rev);
    }
};

// PlatformMgr stub — DTA calls add_sink/remove_sink for Xbox Live events
// and queries guide/signin state. Base Hmx::Object handles add_sink/remove_sink.
class NativePlatformMgrStub : public Hmx::Object {
public:
    NativePlatformMgrStub() {}
    virtual DataNode Handle(DataArray *msg, bool rev) {
        Symbol sym = msg->Sym(1);
        if (sym == "is_guide_showing") return DataNode(0);
        if (sym == "is_pad_signed_into_live") return DataNode(0);
        if (sym == "show_controller_required") return DataNode(0);
        if (sym == "enable_xmp") return DataNode(0);
        if (sym == "disable_xmp") return DataNode(0);
        if (sym == "guide_showing") return DataNode(0);
        // Kinect hardware — not present on native
        if (sym == "has_kinect") return DataNode(0);
        if (sym == "is_kinect_connected") return DataNode(0);
        return Hmx::Object::Handle(msg, rev);
    }
};

// SpeechMgr stub — Kinect voice recognition. No microphone on native.
class NativeSpeechMgrStub : public Hmx::Object {
public:
    NativeSpeechMgrStub() {}
    virtual DataNode Handle(DataArray *msg, bool rev) {
        Symbol sym = msg->Sym(1);
        if (sym == "set_rule") return DataNode(0);
        if (sym == "begin_recognition") return DataNode(0);
        if (sym == "set_recognizing") return DataNode(0);
        if (sym == "end_recognition") return DataNode(0);
        if (sym == "is_recognizing") return DataNode(0);
        if (sym == "is_speech_supportable") return DataNode(0);
        if (sym == "get_result") return DataNode(Symbol(""));
        return Hmx::Object::Handle(msg, rev);
    }
};

#endif // HX_NATIVE
#include "ChecksumData_xbox.h"
#include "char/Char.h"
#include "flow/FlowManager.h"
#include "flow/Flow.h"
#include "flow/PropertyEventProvider.h"
#include "game/Game.h"
#include "game/GameMode.h"
#include "game/HamUserMgr.h"
#include "game/PartyModeMgr.h"
#include "game/PresenceMgr.h"
#include "math/Utl.h"
#include "gesture/GestureMgr.h"
#include "gesture/LiveCameraInput.h"
#include "gesture/SkeletonUpdate.h"
#include "hamobj/HamNavList.h"
#include "hamobj/Ham.h"
#include "hamobj/HamGameData.h"
#include "hamobj/MiniGameMgr.h"
#include "hamobj/MoveMgr.h"
#include "hamobj/HamWardrobe.h"
#include "meta/Achievements.h"
#include "meta/FixedSizeSaveable.h"
#include "meta_ham/AccomplishmentManager.h"
#include "meta_ham/Challenges.h"
#include "meta_ham/ContextChecker.h"
#include "meta_ham/HamSongMgr.h"
#include "meta_ham/MetaPanel.h"
#include "meta_ham/ProfileMgr.h"
#include "meta_ham/UIEventMgr.h"
#include "meta_ham/MetagameRank.h"
#include "meta_ham/Leaderboards.h"
#include "meta_ham/SaveLoadManager.h"
#include "midi/MidiParser.h"
#include "movie/Movie.h"
#include "movie/Splash.h"
#include "net_ham/RockCentral.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "obj/Msg.h"
#include "os/Archive.h"
#include "os/Debug.h"
#include "os/File.h"
#include "os/FileCache.h"
#include "os/PlatformMgr.h"
#include "os/System.h"
#include "os/Timer.h"
#include "rndobj/HiResScreen.h"
#include "rndobj/Rnd.h"
#include "synth/Synth.h"
#include "ui/PanelDir.h"
#include "ui/UI.h"
#include "utl/Cheats.h"
#include "utl/Magnu.h"
#include "utl/MemTracker.h"
#include "utl/Option.h"
#include "utl/MakeString.h"
#include "world/World.h"
#include <algorithm>
#include <cstring>
#include <cctype>
#include <new>

namespace {
    bool gListenForKinectGuide;
    FileCache *gPersistentCache;
    ModalCallbackFunc *gRealCallback;
    unsigned int gIsUselessLoadSymbolInitBits;
    Symbol gIsUselessLoadDanceBattleMode;
    Symbol gIsUselessLoadPracticeMode;
    Symbol gIsUselessLoadCampaignPracticeMode;
    Symbol gIsUselessLoadInCampaignModeProp;
    Symbol gIsUselessLoadInCampaignStingerProp;
    Symbol gIsUselessLoadJustIntroMode;
    Symbol gIsUselessLoadMindControlMode;
    Symbol gIsUselessLoadBustAMoveMode;
    Symbol gIsUselessLoadChallengeMode;
    Symbol gIsUselessLoadStrikeAPoseMode;
    Symbol gIsUselessLoadRhythmBattleMode;
    Symbol gIsUselessLoadGameplayModeProp;
    Symbol gIsUselessLoadCurrentCampaignEraProp;
    Symbol gIsUselessLoadEraTanBattleMode;
}

Symbol RemoveDigitSuffix(const Symbol &);
bool IsUselessLoad(const char *);
void DebugModal(Debug::ModalType &, FixedString &, bool);
bool XShowNuiCallback(u32 &);
DWORD KinectGuideThread(void *);

App::App(int argc, char **argv) {
    Timer startupTimer;
    startupTimer.Start();

    EnableKeyCheats(false);
    SetFileChecksumData();
    SystemPreInit(argc, argv, "config/ham_preinit_keep.dta");

    if (TheArchive) {
        static const int kAppArchivePermissions[11] = {
            0x000100CF,
            0x000100C6,
            0x000100EE,
            0x000100C4,
            0x000100BD,
            0x000100C5,
            0x000100BA,
            0x00010059,
            0x00010097,
            0x00010081,
            0x0001001B,
        };
        TheArchive->SetArchivePermission(11, kAppArchivePermissions);
    }

#ifdef HX_NATIVE
    // Native boot: init renderer, subsystems, skip Kinect/threading
    TheRnd.PreInit();

    static DataNode &notifyLevel = DataVariable("notify_level");
    {
        DataNode notifyLevelValue(1);
        notifyLevel = notifyLevelValue;
    }
    gRealCallback = TheDebug.SetModalCallback(DebugModal);

#ifdef __EMSCRIPTEN__
    // Must set cache mode BEFORE SystemInit — metamaterials.milo loads during
    // SystemInit → RndMat::LoadMetaMaterials(), and CachedPath needs sCacheMode
    // to transform "metamaterials.milo" → "gen/metamaterials.milo_xbox".
    DirLoader::SetCacheMode(true);
#endif

    SystemInit("config/ham_keep.dta");

    // Audio system (Fader/MoggClip factories need to be registered)
    SynthInit();

    // Movie system (needs to be initialized before DTA scripts create MoviePanel)
    Movie::Init();

    // Initialize renderer
    TheRnd.Init();

#ifdef __EMSCRIPTEN__
    // Yield to browser so WebGPU adapter/device async callbacks fire.
    // After resume, mDevice is valid and we can create GPU resources.
    emscripten_sleep(0);
    {
        extern WgpuRnd *gWgpuRnd;
        if (gWgpuRnd && gWgpuRnd->Gpu().IsReady()) {
            gWgpuRnd->InitGpuResources();
        }
    }
#endif

    // Splash screens (ESRB + Harmonix logos) — same as Xbox boot.
    // Disable with DC3_SHOW_SPLASH=0.
    // On web, the HTML overlay plays the splash video while WASM boots —
    // the engine's synchronous Splash system can't render on Emscripten
    // (no rAF yield), so skip it to avoid wasting boot time.
    const char *splashEnv = getenv("DC3_SHOW_SPLASH");
#ifdef __EMSCRIPTEN__
    bool showSplash = false;
#else
    bool showSplash = !splashEnv || strcmp(splashEnv, "0") != 0;
#endif
    Splash splash;
    if (showSplash) {
        splash.AddScreen("ui/splash/eng/esrb_keep.milo", 0x12C0);
        splash.AddScreen("ui/splash/harmonix_keep.milo", 3000);
        splash.PrepareRemaining();
        splash.BeginSplasher();
    }
#ifdef __EMSCRIPTEN__
    // Yield immediately after splash Draw() so the browser can present the
    // surface texture. emdawnwebgpu auto-presents at rAF boundary — if we
    // don't yield here, the splash frame expires before the browser sees it.
    if (showSplash) emscripten_sleep(0);
#endif

    // Register script functions
    MagnuInit();
    if (showSplash && TheSplasher) TheSplasher->Poll();
#ifdef __EMSCRIPTEN__
    if (showSplash) emscripten_sleep(0);
#endif

    // Flow system - manages game state machine
    FlowInit();
    if (showSplash && TheSplasher) TheSplasher->Poll();
#ifdef __EMSCRIPTEN__
    if (showSplash) emscripten_sleep(0);
#endif

    // Load common sound bank (Faders, FxSend, Sound objects used by gameplay)
    {
        ObjDirPtr<ObjectDir> commonBankDir;
        DataArray *soundBanksConfig = SystemConfig("sound", "banks", "common");
        const char *soundBankPath = soundBanksConfig->Node(1).Str(soundBanksConfig);
        commonBankDir.LoadFile(soundBankPath, false, true, kLoadFront, false);
        TheSynth->SetDir(commonBankDir);
    }

    // Character system
    CharInit();
    if (showSplash && TheSplasher) TheSplasher->Poll();
#ifdef __EMSCRIPTEN__
    if (showSplash) emscripten_sleep(0);
#endif

    // World system
    WorldInit();
    if (showSplash && TheSplasher) TheSplasher->Poll();
#ifdef __EMSCRIPTEN__
    if (showSplash) emscripten_sleep(0);
#endif

    // Ham (game-specific) system
    HamInit();
    if (showSplash && TheSplasher) TheSplasher->Poll();
#ifdef __EMSCRIPTEN__
    if (showSplash) emscripten_sleep(0);
#endif

    // Override HamLabel factory → AppLabel (DC3-specific subclass).
    // HamInit() registers HamLabel::NewObject for "HamLabel"; we replace it
    // with AppLabel::NewObject so .milo deserialization creates AppLabel
    // instances, which MainMenuProvider::Text dynamic_casts to.
    REGISTER_OBJ_FACTORY(AppLabel)

    // Ensure player providers exist — ham_init.dta normally creates these via DTA,
    // but if the config chain fails on native, players have null mProvider which
    // breaks SkeletonChooser::GetPlayerSide(), HamPlayerData::Side(), etc.
    if (TheGameData) {
        for (int i = 0; i < 2; i++) {
            HamPlayerData *pd = TheGameData->Player(i);
            if (pd && !pd->Provider()) {
                char providerName[32];
                snprintf(providerName, sizeof(providerName), "player_provider_%d", i + 1);
                // Check if DTA already created it but didn't wire it up
                PropertyEventProvider *provider =
                    ObjectDir::Main()->Find<PropertyEventProvider>(providerName, false);
                if (!provider) {
                    provider = Hmx::Object::New<PropertyEventProvider>();
                    provider->SetName(providerName, ObjectDir::Main());
                }
                // Wire provider to player data via property sync
                DataNode provNode(provider);
                pd->SetProperty(Symbol("provider"), provNode);
                // Set side: player 0 = right, player 1 = left (matches ham_init.dta)
                static Symbol side("side");
                static Symbol player_present("player_present");
                provider->SetProperty(side, i == 0 ? 1 : 0); // kSkeletonRight=1, kSkeletonLeft=0
                // Mark both players as present so the full HUD renders
                // (hud_left for player 1, hud_right for player 0).
                // On Xbox, both sides show in crew/party mode.
                provider->SetProperty(player_present, 1);
                MILO_LOG("DC3 Native: Created player provider '%s' (side=%d)\n",
                        providerName, i == 0 ? 1 : 0);
            }
        }
    }

    // MoveMgr — creates SuperEasyRemixer, SongLayout, loads category.dta.
    // Must be after HamInit() which registers the SongLayout factory.
    MoveMgr::Init(0);
    MiniGameMgr::Init();

    // Song manager
    TheHamSongMgr.Init();
    if (showSplash && TheSplasher) TheSplasher->Poll();
#ifdef __EMSCRIPTEN__
    if (showSplash) emscripten_sleep(0);
#endif

    // Game subsystem inits (from original init sequence)
    MetaPanel::Init();
    GameInit();
    if (showSplash && TheSplasher) TheSplasher->Poll();
#ifdef __EMSCRIPTEN__
    if (showSplash) emscripten_sleep(0);
#endif

    // Subsystem inits that other code dereferences without null checks.
    // Order matches Xbox init sequence (FixedSizeSaveable/HamUserMgr early,
    // AccomplishmentManager before MetagameRank since Init() uses TheAccomplishmentMgr).
    FixedSizeSaveable::Init(0x5C, 0x1662);
    HamUserMgrInit(false);
    AccomplishmentManager::Init(SystemConfig("accomplishment_info"));
    MetagameRank::Preinit(); // sets gRanksArray, needed by MetagameRank methods
    MetagameRank::Init();
    PartyModeMgr::Init();

    // Register MidiParser factory so .milo files can deserialize MidiParser objects.
    // Missing this caused silent null returns from NewObject("MidiParser").
    MidiParser::Init();

    // Set path eval callback to skip loading unnecessary assets based on game mode.
    // Same callback used in PPC path — filters out mode-specific loads.
    DirLoader::SetPathEvalCallback(IsUselessLoad);

    // Register DTA script functions (random_context, etc.) so DTA handlers that
    // reference them don't silently fail. This is critical for DTA handler execution.
    ContextCheckerInit();

    // Trigger content refresh to load base game songs from ark.
    // This must happen after HamSongMgr.Init() (registers callback) and
    // MetaPanel::Init() (registers SongSortMgr etc.) so all callbacks fire.
    MILO_LOG("DC3 Native: About to call ContentMgr::RefreshSynchronously\n");
    TheContentMgr.RefreshSynchronously();
    MILO_LOG("DC3 Native: ContentMgr::RefreshSynchronously returned\n");

    // UI system — use the global TheHamUI (game-specific UIManager subclass)
    // for proper two-pass draw pipeline (letterbox, blacklight, helpbar, shell input)
    // HamUI::Init() calls UIEventMgr::Init() + UIManager::Init() internally
    TheUI = &TheHamUI;
    TheHamUI.Init();
    // Register smart stub objects for DTA scripts that reference Xbox managers.
    // These return sensible defaults so DTA handlers execute correctly instead
    // of silently failing. See DTA_FLOW_V2_PLAN.md Phase 1.
    {
        auto registerStub = [](const char *name, Hmx::Object *obj) {
            if (!ObjectDir::Main()->FindObject(name, false, false)) {
                obj->SetName(name, ObjectDir::Main());
            } else {
                delete obj;
            }
        };
        registerStub("saveload_mgr", new NativeSaveLoadStub());
        registerStub("profile_mgr", new NativeProfileMgrStub());
        registerStub("platform_mgr", new NativePlatformMgrStub());
        // These don't need smart handlers — bare stubs are sufficient
        registerStub("content_mgr", new Hmx::Object());
        registerStub("challenges", new Hmx::Object());
        registerStub("speech_mgr", new NativeSpeechMgrStub());
    }

    // Inject native-only locale strings via MagnuStrings (checked first by
    // Locale::Localize, English-only, normally unused on native).
    {
        DataArray *nativeLocale = new DataArray(4);

        DataArray *label = new DataArray(2);
        label->Node(0) = DataNode(Symbol("option_camera_blend"));
        label->Node(1) = DataNode("<altb>Camera Blend</alt> Enabled");
        nativeLocale->Node(0) = DataNode(label, kDataArray);
        label->Release();

        DataArray *desc = new DataArray(2);
        desc->Node(0) = DataNode(Symbol("option_camera_blend_desc"));
        desc->Node(1) = DataNode("Smooth camera blending between shots. Turn off for Xbox-faithful instant cuts.");
        nativeLocale->Node(1) = DataNode(desc, kDataArray);
        desc->Release();

        DataArray *dbgLabel = new DataArray(2);
        dbgLabel->Node(0) = DataNode(Symbol("option_debug_panel"));
        dbgLabel->Node(1) = DataNode("<altb>Debug Overlay</alt>");
        nativeLocale->Node(2) = DataNode(dbgLabel, kDataArray);
        dbgLabel->Release();

        DataArray *dbgDesc = new DataArray(2);
        dbgDesc->Node(0) = DataNode(Symbol("option_debug_panel_desc"));
        dbgDesc->Node(1) = DataNode("Show camera debug overlay with real-time sliders. Toggle with ~ key.");
        nativeLocale->Node(3) = DataNode(dbgDesc, kDataArray);
        dbgDesc->Release();

        TheLocale.SetMagnuStrings(nativeLocale);
    }

#ifdef HX_IMGUI
    // Initialize ImGui debug overlay
    {
#if !defined(__EMSCRIPTEN__)
        GLFWwindow *win = gNativeWindow;
#else
        GLFWwindow *win = nullptr; // Web backend ignores window param
#endif
        auto &gpu = static_cast<WgpuRnd&>(TheRnd).Gpu();
        if (gpu.Device()) {
            ImGuiBackend::Init(win, gpu.Device(), gpu.SurfaceFormat());
            DebugPanel::Init();
            MILO_LOG("DC3 Native: ImGui debug panel initialized (~ to toggle)\n");
        }
    }
#endif

    if (showSplash) {
        splash.EndSplasher();
    }
#ifdef __EMSCRIPTEN__
    if (showSplash) emscripten_sleep(0);
#endif

    // Go to first screen (title screen)
    TheUI->GotoFirstScreen();
#else
    TheRnd.PreInit();

    static DataNode &notifyLevel = DataVariable("notify_level");
    if (UsingCD()) {
        DataNode notifyLevelValue(1);
        notifyLevel = notifyLevelValue;
    } else {
        DataNode notifyLevelValue(1);
        notifyLevel = notifyLevelValue;
    }

    gRealCallback = TheDebug.SetModalCallback(DebugModal);

    SynthPreInit();
    Movie::Init();
    TheRnd.SetClearColor(Hmx::Color(0.0f, 0.0f, 0.0f, 1.0f));

    Splash splash;
    bool fastBoot = OptionBool("fast", false);
    if (fastBoot || !UsingCD()) {
        splash.SetWaitForSplash(false);
    }
    if (fastBoot) {
        SynthSample::Disable();
    }

    PlatformRegion region = ThePlatformMgr.GetRegion();
    unsigned long systemLocale = ULSystemLocale();
    if (systemLocale == 0x14) {
        splash.AddScreen("ui/splash/jpn/esrb_keep.milo", 0x12C0);
    } else if (region == kRegionNA) {
        splash.AddScreen("ui/splash/eng/esrb_keep.milo", 0x12C0);
    }
    splash.AddScreen("ui/splash/harmonix_keep.milo", 3000);
    splash.PrepareNext();
    splash.BeginSplasher();

    float splashStartMs = startupTimer.SplitMs();
    LiveCameraInput::PreInit();
    LiveCameraInput::Init();

    gListenForKinectGuide = true;
    HANDLE kinectGuideThread = CreateThread(0, 0, KinectGuideThread, 0, 4, 0);
    XSetThreadProcessor(kinectGuideThread, 1);
    ResumeThread(kinectGuideThread);

    splash.PrepareRemaining();
    SystemInit("config/ham_keep.dta");
#endif

#ifndef HX_NATIVE
    MagnuInit();
    if (TheSplasher)
        TheSplasher->Poll();

    splash.Suspend();
    TheRnd.Init();
    TheServer.Init();
    TheRockCentral.Init();
    splash.Resume();
    if (TheSplasher)
        TheSplasher->Poll();

    FormatString redBuildBanner("HMX Red Build!\n");
    TheDebug << redBuildBanner.Str();

    FixedSizeSaveable::Init(0x5C, 0x1662);
    HamUserMgrInit(false);
    if (TheSplasher)
        TheSplasher->Poll();

    SynthInit();
    if (TheSplasher)
        TheSplasher->Poll();

    FlowInit();
    if (TheSplasher)
        TheSplasher->Poll();

    {
        ObjDirPtr<ObjectDir> audioMixerDir;
        audioMixerDir.LoadFile("sfx/audio_mixer.milo", false, true, kLoadFront, false);

        ObjDirPtr<ObjectDir> commonBankDir;
        DataArray *soundBanksConfig = SystemConfig("sound", "banks", "common");
        const char *soundBankPath = soundBanksConfig->Node(1).Str(soundBanksConfig);
        commonBankDir.LoadFile(soundBankPath, false, true, kLoadFront, false);
        TheSynth->SetDir(commonBankDir);

        if (TheSplasher)
            TheSplasher->Poll();
    }

    SaveLoadManager::Init();
    if (TheSplasher)
        TheSplasher->Poll();

    CharInit();
    if (TheSplasher)
        TheSplasher->Poll();

    MidiParser::Init();
    if (TheSplasher)
        TheSplasher->Poll();

    WorldInit();
    if (TheSplasher)
        TheSplasher->Poll();

    HamInit();
    if (TheSplasher)
        TheSplasher->Poll();

    TheHamSongMgr.Init();
    if (TheSplasher)
        TheSplasher->Poll();

    MetaPanel::Init();
    if (TheSplasher)
        TheSplasher->Poll();

    GameInit();
    DirLoader::SetPathEvalCallback(IsUselessLoad);
    if (TheSplasher)
        TheSplasher->Poll();

    ContextCheckerInit();
    if (TheSplasher)
        TheSplasher->Poll();

    PlatformMgr::sXShowCallback = XShowNuiCallback;
    if (TheSplasher)
        TheSplasher->Poll();

    AccomplishmentManager::Init(SystemConfig("accomplishment_info"));
    if (TheSplasher)
        TheSplasher->Poll();

    MetagameRank::Init();

    DataArray *persistentCacheConfig = SystemConfig("persistent_filecache");
    if (persistentCacheConfig) {
        FileCache *allocatedFileCache = (FileCache *)MemAlloc(
            0x1C,
            "e:\\lazer_build_gmc1\\system\\src\\os/FileCache.h",
            0x21,
            "FileCache",
            0
        );
        if (allocatedFileCache) {
            auto cacheSize = persistentCacheConfig->Node(1).Int(persistentCacheConfig);
            allocatedFileCache = new (allocatedFileCache)
                FileCache(cacheSize, kLoadFront, false, true);
        }
        gPersistentCache = allocatedFileCache;
        gPersistentCache->StartSet(0);
        for (int cachePathIdx = 2; cachePathIdx < persistentCacheConfig->Size(); ++cachePathIdx) {
            FilePath emptyPath("");
            FilePath cachePath(persistentCacheConfig->Node(cachePathIdx).Str(persistentCacheConfig));
            gPersistentCache->Add(cachePath, 1, emptyPath);
        }
        gPersistentCache->EndSet();
        gPersistentCache->PollUntilLoaded();
    }

    static DataNode &extraSongs = DataVariable("extra_songs");
    if (UsingCD()) {
        DataNode extraSongsValue(0);
        extraSongs = extraSongsValue;
    } else {
        DataNode extraSongsValue(1);
        extraSongs = extraSongsValue;
    }

    TheUI->Init();
    if (TheSplasher)
        TheSplasher->Poll();

    GestureMgr::DebugInit();
    ThePresenceMgr.Init();
    if (TheSplasher)
        TheSplasher->Poll();

    MoveMgr::Init(0);
    MiniGameMgr::Init();
    if (TheSplasher)
        TheSplasher->Poll();

    PartyModeMgr::Init();
    TheUI->GotoFirstScreen();

    float startupEndMs = startupTimer.SplitMs();
    if (TheArchive && Archive::DebugArkOrder()) {
        float startupRemainderMs = startupEndMs - splashStartMs;
        TheDebug << MakeString("Startup Time: %f %f\n", splashStartMs, startupRemainderMs);
    }

    splash.EndSplasher();
    EnableKeyCheats(true);
    AutoGlitchReport::EnableCallback();
    ThePlatformMgr.SetBackgroundDownloadPriority(true);

    gListenForKinectGuide = false;
    WaitForSingleObject(kinectGuideThread, 0xFFFFFFFF);
    CloseHandle(kinectGuideThread);

    MemTrackEnable(true);
#endif // !HX_NATIVE
}

#ifdef HX_NATIVE
void App::RunOneFrame() {
    SystemPoll(false);

    if (TheUI)
        TheUI->Poll();

    TheTaskMgr.Poll();

    if (TheFlowMgr)
        TheFlowMgr->Poll();

    if (TheSynth)
        TheSynth->Poll();

#ifdef __EMSCRIPTEN__
    AudioDevice::GetInstance().PumpAudio();
#endif

    TheRnd.BeginDrawing();
    if (TheUI)
        TheUI->Draw();

#ifdef HX_IMGUI
    // ImGui debug overlay — deferred init on web (GPU device is async)
    {
        static bool sImGuiReady = false;
        if (!sImGuiReady) {
            auto &gpu = static_cast<WgpuRnd&>(TheRnd).Gpu();
            if (gpu.Device()) {
                ImGuiBackend::Init(nullptr, gpu.Device(), gpu.SurfaceFormat());
                DebugPanel::Init();
                sImGuiReady = true;
            }
        }
        if (sImGuiReady && ImGui::GetCurrentContext()) {
            ImGuiBackend::NewFrame();
            if (DebugPanel::IsVisible())
                DebugPanel::Draw();
            ImGui::Render();
        }
    }
#endif

    TheRnd.EndDrawing();
}
#endif // HX_NATIVE

void App::CaptureHiRes() {
    bool paused = AllPaused();

    if (paused)
        TheGame->SetTimePaused(true);

    DrawRegular();

    int tiles = TheHiResScreen.GetTiling() * TheHiResScreen.GetTiling();

    for (int i = 0; i <= tiles; i++) {
        DrawRegular();
        TheHiResScreen.Accumulate();
    }

    TheHiResScreen.Finish();

    if (paused)
        TheGame->SetTimePaused(false);
}

void App::DrawRegular() {
    TheRnd.BeginDrawing();
    TheUI->Draw();
    TheRnd.EndDrawing();
}

App::~App() { TheDebug.Exit(0, true); }

bool XShowNuiCallback(u32 &p1) {
    bool ret;

    MILO_ASSERT(TheGestureMgr, 0x87);

    Skeleton *skel = TheGestureMgr->GetActiveSkeleton();

    if (!HamNavList::sLastSelectInControllerMode && skel && skel->IsTracked()) {
        ret = true;
        p1 = skel->TrackingID();
    } else {
        ret = false;
    }

    return ret;

}

bool EndsWith(const char *str, const char *suffix) {
    int strLen = strlen(str);
    int suffixLen = strlen(suffix);
    return strstr(str, suffix) == str + strLen - suffixLen;
}

Symbol RemoveDigitSuffix(const Symbol &symbol) {
    char trimmedText[64];
    trimmedText[0] = '\0';
    memset(trimmedText + 1, 0, 63);

    const char *symbolText = symbol.Str();
    int len = strlen(symbolText);
    MILO_ASSERT(len > 0, 0x2AB);

#ifdef HX_NATIVE
    int copyLen = std::find_if(
                      symbolText,
                      symbolText + len,
                      static_cast<int (*)(int)>(isdigit)
    ) - symbolText;
#else
    stlpmtx_std::random_access_iterator_tag findTag;
    int copyLen = stlpmtx_std::__find_if(
                      symbolText,
                      symbolText + len,
                      static_cast<int (*)(int)>(isdigit),
                      findTag
    ) - symbolText;
#endif
    if (copyLen != 0) {
        memmove(trimmedText, symbolText, copyLen);
    }

    return Symbol(trimmedText);
}

static bool InLoaderModeSafe(Symbol modeSymbol) {
    return g_LoaderModeCallback ? g_LoaderModeCallback(modeSymbol) : false;
}

bool IsUselessLoad(const char *loadPath) {
    bool shouldSkipLoad = false;

    if (gMiloTool || loadPath == 0 || TheGameData == 0) {
        return false;
    }

    HamPlayerData *player0 = TheGameData->Player(0);
    HamPlayerData *player1 = TheGameData->Player(1);
    if (player0 == 0 || player1 == 0) {
        return false;
    }

    bool isLocalizedVoiceBankPath;
    if (strstr(loadPath, "sfx/loc/") != loadPath
        || (isLocalizedVoiceBankPath = true, strstr(loadPath, "/vo_bank_") == 0)) {
        isLocalizedVoiceBankPath = false;
    }

    const char *baseName = FileGetBase(loadPath);
    Symbol baseNameSymbol(baseName);

    bool isCharacterCamshotPath;
    if (strstr(loadPath, "world/shared/camshots/") != loadPath
        || (isCharacterCamshotPath = true, GetCharacterEntry(baseNameSymbol, false) == 0)) {
        isCharacterCamshotPath = false;
    }

    bool isCrewCamshotPath = strstr(loadPath, "world/shared/camshots/crew_") == loadPath;
    if ((gIsUselessLoadSymbolInitBits & 0x1) == 0) {
        gIsUselessLoadSymbolInitBits |= 0x1;
        new (&gIsUselessLoadDanceBattleMode) Symbol("dance_battle");
    }
    if ((isLocalizedVoiceBankPath || isCharacterCamshotPath)
        && strstr(loadPath, player0->Char().Str()) == 0
        && strstr(loadPath, player1->Char().Str()) == 0) {
        if (g_LoaderModeCallback(gIsUselessLoadDanceBattleMode)) {
            Symbol player0Crew = GetCrewForCharacter(player0->Char(), true);
            Symbol player1Crew = GetCrewForCharacter(player1->Char(), true);
            bool matchesCrewCharacter;
            const char *crewCharacterMatch
                = strstr(baseNameSymbol.Str(), GetCrewCharacter(player0Crew, 0).Str());
            if (crewCharacterMatch == 0) {
                crewCharacterMatch = strstr(baseNameSymbol.Str(), GetCrewCharacter(player0Crew, 1).Str());
                if (crewCharacterMatch != 0) {
                    goto crew_character_matched;
                }
                crewCharacterMatch = strstr(baseNameSymbol.Str(), GetCrewCharacter(player1Crew, 0).Str());
                if (crewCharacterMatch != 0) {
                    goto crew_character_matched;
                }
                crewCharacterMatch = strstr(baseNameSymbol.Str(), GetCrewCharacter(player1Crew, 1).Str());
                matchesCrewCharacter = false;
                if (crewCharacterMatch != 0) {
                    goto crew_character_matched;
                }
            } else {
crew_character_matched:
                matchesCrewCharacter = true;
            }

            if (!matchesCrewCharacter) {
                shouldSkipLoad = true;
            }
        } else {
            shouldSkipLoad = true;
        }
    }

    if (!g_LoaderModeCallback(gIsUselessLoadDanceBattleMode)
        && (isCrewCamshotPath || EndsWith(loadPath, "/vo_bank.milo"))) {
        shouldSkipLoad = true;
    }

    if ((gIsUselessLoadSymbolInitBits & 0x2) == 0) {
        gIsUselessLoadSymbolInitBits |= 0x2;
        new (&gIsUselessLoadPracticeMode) Symbol("practice");
    }
    if ((gIsUselessLoadSymbolInitBits & 0x4) == 0) {
        gIsUselessLoadSymbolInitBits |= 0x4;
        new (&gIsUselessLoadCampaignPracticeMode) Symbol("campaign_practice");
    }
    if (!g_LoaderModeCallback(gIsUselessLoadPracticeMode)
        && !g_LoaderModeCallback(gIsUselessLoadCampaignPracticeMode)
        && EndsWith(loadPath, "/barks.milo")) {
        shouldSkipLoad = true;
    }

    if ((gIsUselessLoadSymbolInitBits & 0x8) == 0) {
        gIsUselessLoadSymbolInitBits |= 0x8;
        new (&gIsUselessLoadInCampaignModeProp) Symbol("is_in_campaign_mode");
    }
    if ((gIsUselessLoadSymbolInitBits & 0x10) == 0) {
        gIsUselessLoadSymbolInitBits |= 0x10;
        new (&gIsUselessLoadInCampaignStingerProp) Symbol("is_in_campaign_stinger");
    }
    bool inCampaignMode;
    bool inCampaignStinger;
    if (TheHamProvider == 0) {
not_in_campaign_mode:
        inCampaignMode = false;
    } else {
        int campaignModeValue = TheHamProvider->Property(gIsUselessLoadInCampaignModeProp, true)->Int();
        inCampaignMode = true;
        if (campaignModeValue == 0) {
            goto not_in_campaign_mode;
        }
    }
    if (TheHamProvider == 0) {
not_in_campaign_stinger:
        inCampaignStinger = false;
    } else {
        int campaignStingerValue = TheHamProvider->Property(gIsUselessLoadInCampaignStingerProp, true)->Int();
        inCampaignStinger = true;
        if (campaignStingerValue == 0) {
            goto not_in_campaign_stinger;
        }
    }

    if (!inCampaignMode) {
        if (!inCampaignStinger) {
            if (strstr(loadPath, "/campaign/camp_scene_") != 0) {
                shouldSkipLoad = true;
            }
        }
    }

    if (strstr(loadPath, "/vo_bank_camp_") != 0) {
        shouldSkipLoad = !inCampaignMode;
    }

    if ((gIsUselessLoadSymbolInitBits & 0x20) == 0) {
        gIsUselessLoadSymbolInitBits |= 0x20;
        new (&gIsUselessLoadJustIntroMode) Symbol("just_intro");
    }
    if ((gIsUselessLoadSymbolInitBits & 0x40) == 0) {
        gIsUselessLoadSymbolInitBits |= 0x40;
        new (&gIsUselessLoadMindControlMode) Symbol("mind_control");
    }
    bool inMindControlOrIntro
        = g_LoaderModeCallback(gIsUselessLoadMindControlMode)
        || g_LoaderModeCallback(gIsUselessLoadJustIntroMode);
    if (TheHamWardrobe && (inMindControlOrIntro || g_LoaderModeCallback(gIsUselessLoadDanceBattleMode))
        && (isLocalizedVoiceBankPath || isCharacterCamshotPath)) {
        Symbol override0;
        Symbol override1;

        if (inMindControlOrIntro) {
            override0 = TheHamWardrobe->GetBackupOutfitOverride(0);
            override1 = TheHamWardrobe->GetBackupOutfitOverride(1);
        } else if (g_LoaderModeCallback(gIsUselessLoadDanceBattleMode)) {
            override0 = GetAlternateCharacter(player0->Char());
            override1 = GetAlternateCharacter(player1->Char());
        }

        if (!override0.Null() && !override1.Null()) {
            Symbol trimmedOverride0 = RemoveDigitSuffix(override0);
            Symbol trimmedOverride1 = RemoveDigitSuffix(override1);
            if (strstr(loadPath, trimmedOverride0.Str()) != 0 || strstr(loadPath, trimmedOverride1.Str()) != 0) {
                shouldSkipLoad = false;
            }
        }
    }

    if (EndsWith(loadPath, "/vo_bank.milo")) {
        shouldSkipLoad = false;
    }

    if ((gIsUselessLoadSymbolInitBits & 0x80) == 0) {
        gIsUselessLoadSymbolInitBits |= 0x80;
        new (&gIsUselessLoadBustAMoveMode) Symbol("bustamove");
    }
    if (g_LoaderModeCallback(gIsUselessLoadBustAMoveMode)
        && EndsWith(loadPath, "/vo_bank_bustamove.milo")) {
        shouldSkipLoad = false;
    }
    if ((gIsUselessLoadSymbolInitBits & 0x100) == 0) {
        gIsUselessLoadSymbolInitBits |= 0x100;
        new (&gIsUselessLoadChallengeMode) Symbol("challenge");
    }
    if (g_LoaderModeCallback(gIsUselessLoadChallengeMode)
        && EndsWith(loadPath, "/vo_bank_challenge.milo")) {
        shouldSkipLoad = false;
    }
    if ((gIsUselessLoadSymbolInitBits & 0x200) == 0) {
        gIsUselessLoadSymbolInitBits |= 0x200;
        new (&gIsUselessLoadStrikeAPoseMode) Symbol("strike_a_pose");
    }
    if (g_LoaderModeCallback(gIsUselessLoadStrikeAPoseMode)
        && EndsWith(loadPath, "/vo_bank_strikeapose.milo")) {
        shouldSkipLoad = false;
    }

    if ((gIsUselessLoadSymbolInitBits & 0x400) == 0) {
        gIsUselessLoadSymbolInitBits |= 0x400;
        new (&gIsUselessLoadRhythmBattleMode) Symbol("rhythm_battle");
    }
    if ((gIsUselessLoadSymbolInitBits & 0x800) == 0) {
        gIsUselessLoadSymbolInitBits |= 0x800;
        new (&gIsUselessLoadGameplayModeProp) Symbol("gameplay_mode");
    }
    if ((gIsUselessLoadSymbolInitBits & 0x1000) == 0) {
        gIsUselessLoadSymbolInitBits |= 0x1000;
        new (&gIsUselessLoadCurrentCampaignEraProp) Symbol("current_campaign_era");
    }
    if ((gIsUselessLoadSymbolInitBits & 0x2000) == 0) {
        gIsUselessLoadSymbolInitBits |= 0x2000;
        new (&gIsUselessLoadEraTanBattleMode) Symbol("era_tan_battle");
    }
    bool isRhythmBattleContext = false;
    if (TheHamProvider) {
        isRhythmBattleContext
            = TheHamProvider->Property(gIsUselessLoadGameplayModeProp, true)->Sym() == gIsUselessLoadRhythmBattleMode;
        if (inCampaignMode
            && TheHamProvider->Property(gIsUselessLoadCurrentCampaignEraProp, true)->Sym()
                == gIsUselessLoadEraTanBattleMode) {
            isRhythmBattleContext = true;
        }
    }

    if (isRhythmBattleContext) {
        if (EndsWith(loadPath, "/vo_bank_rhythmbattle.milo")) {
            shouldSkipLoad = false;
        }
        if (inCampaignMode) {
            if (EndsWith(loadPath, "/vo_bank_rhythmbattle_finale.milo")) {
                shouldSkipLoad = false;
            }
        }
    }

    if (strstr(baseNameSymbol.Str(), "vo_bank_tutorial_") != 0) {
        shouldSkipLoad = false;
    }

    if ((g_LoaderModeCallback(gIsUselessLoadPracticeMode)
         || g_LoaderModeCallback(gIsUselessLoadCampaignPracticeMode))
        && EndsWith(loadPath, "/vo_bank_rehearse.milo")) {
        shouldSkipLoad = false;
    }

    if (shouldSkipLoad) {
        const char *loggedPath = loadPath;
        if (loggedPath == 0) {
            loggedPath = "NULL";
        }
        TheDebug << MakeString("'%s' is a useless load\n", loggedPath);
    }

    return shouldSkipLoad;
}

void App::Run() { RunWithoutDebugging(); }

void App::RunWithoutDebugging() {
#if defined(HX_NATIVE) && !defined(__EMSCRIPTEN__)
    MILO_LOG("DC3 Native: Entering main loop\n");
    int frameCount = 0;
    bool windowed = (gNativeWindow != nullptr);

    int maxFrames = 10000;
    const char *maxFramesEnv = getenv("MILO_MAX_FRAMES");
    if (maxFramesEnv) maxFrames = atoi(maxFramesEnv);
    if (maxFrames <= 0) maxFrames = 10000;

    GameplayTelemetry::Init();
#ifdef DC3_HTTP_SERVER
    HttpServerInit();
#endif

    if (windowed)
        MILO_LOG("DC3 Native: Windowed mode - close window or press ESC to exit\n");
    else
        MILO_LOG("DC3 Native: Headless mode — running %d frames\n", maxFrames);

    while (true) {
        SystemPoll(false);

        if (TheUI)
            TheUI->Poll();

        TheTaskMgr.Poll();

        if (TheFlowMgr)
            TheFlowMgr->Poll();

        if (TheSynth)
            TheSynth->Poll();

        GameplayTelemetry::Sample(frameCount);
#ifdef DC3_HTTP_SERVER
        if (TheHttpServer) {
            TheHttpServer->ProcessCommands();
            const char* screenName = (TheUI && TheUI->CurrentScreen())
                ? TheUI->CurrentScreen()->Name() : "";
            TheHttpServer->NotifyFrame(screenName, frameCount);
        }
#endif

        // Draw: BeginDrawing → UI panels (venue + HUD + menus) → EndDrawing.
        // During gameplay, the venue renders through world_panel → HamDirector.
        TheRnd.BeginDrawing();
        // Draw UI panels (menus, transitions, flashcards, HUD overlays).
        // With FileMerger convergence, game_screen panels are loaded via
        // the engine pipeline and DTA flow controls visibility.
        if (TheUI && !getenv("DC3_HUD_ONLY") && !getenv("DC3_NO_UI")) {
            if (sigsetjmp(gDrawJmpBuf, 1) == 0) {
                gDrawJmpBufSet = true;
                TheUI->Draw();
                gDrawJmpBufSet = false;
            } else {
                gDrawJmpBufSet = false;
                static int sDrawCrashCount = 0;
                if (++sDrawCrashCount <= 3)
                    fprintf(stderr, "DC3 Native: caught SIGSEGV in Draw(), skipping frame %d (crash #%d)\n", frameCount, sDrawCrashCount);
            }
        }
        // HUD drawn by TheUI->Draw() via game_screen panel hierarchy.
        // FileMerger-loaded HUD flows control visibility/alpha/positioning.

#ifdef HX_IMGUI
        // ImGui debug overlay — rendered after game UI, before EndDrawing
        // EndDrawing() calls RenderImGuiOverlay() which consumes the draw data
        if (ImGui::GetCurrentContext()) {
            ImGuiBackend::NewFrame();
            if (DebugPanel::IsVisible())
                DebugPanel::Draw();
            ImGui::Render();
        }
#endif

        TheRnd.EndDrawing();
#ifdef DC3_HTTP_SERVER
        if (TheHttpServer) TheHttpServer->ProcessScreenshots();
#endif

        frameCount++;

        if (windowed) {
            if (glfwWindowShouldClose(gNativeWindow))
                break;
        } else {
            if (frameCount >= maxFrames) {
                MILO_LOG("DC3 Native: %d frames completed, engine stable!\n", frameCount);
                break;
            }
        }
    }
#ifdef DC3_HTTP_SERVER
    HttpServerShutdown();
#endif
    return;
#endif
    while (true) {
            Timer loop_timer;
            loop_timer.Restart();
            SystemPoll(false);

            TIMER_ACTION("misc_poll", {
                TheAchievements->Poll();
                TheAccomplishmentMgr->Poll();
                if (TheLeaderboards)
                    TheLeaderboards->Poll();
                if (TheChallenges)
                    TheChallenges->Poll();
                TheSaveLoadMgr->Poll();
            })

            TIMER_ACTION("synth_poll", TheSynth->Poll())

            TIMER_ACTION("rock_central_poll", TheRockCentral.Poll())

            TIMER_ACTION("gesture_poll", TheGestureMgr->Poll())

            TheUI->Poll();

            {
                DataNode &hud_panel = DataVariable("hud_panel");
                if (hud_panel.CompatibleType(kDataObject)) {
                    PanelDir *panel = dynamic_cast<PanelDir *>(hud_panel.GetObj(0));
                    if (panel) {
                        Message msg("update_all_flashcard_dance_pct");
                        panel->Handle(msg, true);
                    }
                }
            }

            TheTaskMgr.Poll();
            TheFlowMgr->Poll();

            TIMER_ACTION("skeleton_post_update", {
                SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
                handle.PostUpdate();
            })

            FileDiscSpinUp();

            if (TheHiResScreen.IsActive()) {
                CaptureHiRes();
            } else {
                DrawRegular();
            }

            float loopMs = loop_timer.SplitMs();
            float waiverMs = Timer::SlowFrameWaiver();
            float slowMs = Timer::SlowFrameTimer().SplitMs();
            if (waiverMs > slowMs) waiverMs = slowMs;
            float frameMs = loopMs - waiverMs;

            if (frameMs > 83.3333) {
                const char *msg = 0;
                const char *activeScreen = "none";
                UIScreen *currentScreen = TheUI->CurrentScreen();
                if (currentScreen) {
                    activeScreen = currentScreen->Name();
                }

                const char *transName = "none";
                UIScreen *transScreen = TheUI->TransitionScreen();
                if (transScreen) {
                    transName = transScreen->Name();
                }
                UIManager::TransitionState state = TheUI->GetTransitionState();
                switch (state) {
                case UIManager::kTransitionNone:
                    msg = MakeString("GLITCH: %g ms, ACTIVE %s", frameMs, activeScreen);
                    break;
                case UIManager::kTransitionTo:
                    msg = MakeString(
                        "GLITCH: %g ms, %s TRANS TO %s", frameMs, activeScreen, transName
                    );
                    break;
                case UIManager::kTransitionFrom:
                    msg = MakeString(
                        "GLITCH: %g ms, %s TRANS FROM %s", frameMs, activeScreen, transName
                    );
                    break;
                case UIManager::kTransitionPop:
                    msg = MakeString("GLITCH: %g ms, POPPING %s", frameMs, transName);
                    break;
                }

                static DataNode &notify_level = DataVariable("notify_level");
                if (notify_level.Int() != 0) {
                    static Hmx::Object *cheat_display =
                        ObjectDir::Main()->Find<Hmx::Object>("cheat_display", true);
                    static Message show("show", DataNode(0));
                    show[0] = DataNode(msg);
                    cheat_display->Handle(show, false);
                }
            }
    }
}

DWORD KinectGuideThread(void *) {
    HRESULT hr;
    hr = NuiSkeletonTrackingDisable();
    if (hr < 0) {
        MILO_FAIL("NuiSkeletonTrackingDisable failed");
    }
    hr = NuiSkeletonTrackingEnable(0, 0);
    if (hr < 0) {
        MILO_FAIL("NuiSkeletonTrackingEnable failed");
    }
    HANDLE kinect_listener = XNotifyCreateListener(1);
    MILO_ASSERT(kinect_listener, 0xa2);
    while (gListenForKinectGuide) {
        DWORD dwId;
        ULONG_PTR param;
        while (XNotifyGetNext(kinect_listener, 0, &dwId, &param)) {
            if (dwId == 0x6001a) {
                XShowNuiGuideUI(param);
            }
        }
    }
    CloseHandle(kinect_listener);
    hr = NuiSkeletonTrackingDisable();
    if (hr < 0) {
        MILO_FAIL("NuiSkeletonTrackingDisable failed");
    }
    hr = NuiSkeletonTrackingEnable(SkeletonUpdate::NewSkeletonEvent(), 2);
    if (hr < 0) {
        MILO_FAIL("NuiSkeletonTrackingEnable failed");
    }
    return 0;
}
