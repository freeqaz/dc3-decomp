#include "App.h"
#ifdef HX_NATIVE
#include <algorithm>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "ui/UIPanel.h"
#include "ui/PanelDir.h"
#include "rndobj/Dir.h"
#include "rndobj/Text.h"
#include "world/Dir.h"
#include "char/Character.h"
#include "char/CharFaceServo.h"
#include "char/CharLipSyncDriver.h"
#include "char/CharEyes.h"
#include "char/CharInterest.h"
#include "hamobj/HamCharacter.h"
#include "world/LightPreset.h"
#include "world/LightPresetManager.h"
#include "world/CameraManager.h"
#include "world/CameraShot.h"
#include "rndobj/Cam.h"
#include "hamobj/HamDirector.h"
#include "rndobj/Lit.h"
#include "meta_ham/HamUI.h"
#include "synth/StandardStream.h"
#include "synth/VorbisReader.h"
#include "os/BufFile.h"
#include "meta_ham/HamSongMgr.h"
#include "meta_ham/HamSongMetadata.h"
#include "platform/TransparentQueue.h"
#include "rndobj/BaseMaterial.h"
#include "rndobj/Mat.h"
#include "hamobj/HamGameData.h"
#include "obj/DirLoader.h"
#include "ui/UILabel.h"
extern GLFWwindow *gNativeWindow;
static ObjectDir *gNativeHudDir = nullptr;
static bool gFaceAnimInitDone = false;

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
        if (sym == "has_finished_campaign") return DataNode(0);
        if (sym == "get_all_unlocked") return DataNode(0);
        if (sym == "needs_upload") return DataNode(0);
        if (sym == "global_options_needs_save") return DataNode(0);
        if (sym == "is_any_profile_signed_into_live") return DataNode(0);
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
    // Native boot: init renderer, subsystems, but skip Kinect/splash/threading
    TheRnd.PreInit();

    static DataNode &notifyLevel = DataVariable("notify_level");
    {
        DataNode notifyLevelValue(1);
        notifyLevel = notifyLevelValue;
    }
    gRealCallback = TheDebug.SetModalCallback(DebugModal);
    SystemInit("config/ham_keep.dta");

    // Audio system (Fader/MoggClip factories need to be registered)
    SynthInit();

    // Movie system (needs to be initialized before DTA scripts create MoviePanel)
    Movie::Init();

    // Initialize renderer
    TheRnd.Init();

    // Register script functions
    MagnuInit();

    // Flow system - manages game state machine
    FlowInit();

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

    // World system
    WorldInit();

    // Ham (game-specific) system
    HamInit();

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
                // Mark player 0 as present (controller-based play).
                // Many providers (Character/Crew/Outfit/Venue/Difficulty) gate their
                // lists on player_present — if it's 0 for both, lists may swap or empty.
                provider->SetProperty(player_present, i == 0 ? 1 : 0);
                fprintf(stderr, "DC3 Native: Created player provider '%s' (side=%d)\n",
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

    // Game subsystem inits (from original init sequence)
    MetaPanel::Init();
    GameInit();

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
    fprintf(stderr, "DC3 Native: About to call ContentMgr::RefreshSynchronously\n");
    TheContentMgr.RefreshSynchronously();
    fprintf(stderr, "DC3 Native: ContentMgr::RefreshSynchronously returned\n");

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
#ifdef HX_NATIVE
    printf("DC3 Native: Entering main loop\n");
    int frameCount = 0;
    bool windowed = (gNativeWindow != nullptr);

    int maxFrames = 10000;
    const char *maxFramesEnv = getenv("MILO_MAX_FRAMES");
    if (maxFramesEnv) maxFrames = atoi(maxFramesEnv);
    if (maxFrames <= 0) maxFrames = 10000;

    if (windowed)
        printf("DC3 Native: Windowed mode - close window or press ESC to exit\n");
    else
        printf("DC3 Native: Headless mode — running %d frames\n", maxFrames);

    while (true) {
        SystemPoll(false);

        if (TheUI)
            TheUI->Poll();

        TheTaskMgr.Poll();

        if (TheFlowMgr)
            TheFlowMgr->Poll();

        if (TheSynth)
            TheSynth->Poll();

        // Native port: poll the venue WorldDir for animation/lighting.
        // The venue renders through world_panel as part of TheUI->Draw() —
        // no separate DrawShowing call needed. NaN camera protection is in
        // CameraManager::CalcFrame() and CamShot::SetFrame().
        {
            WorldDir* venueWorld = nullptr;
            if (TheHamDirector) {
                venueWorld = TheHamDirector->GetVenueWorld();
            }
#ifdef HX_NATIVE
            // Native: The DTA merger pipeline doesn't load venues automatically.
            // When entering game_screen with no venue world, load it explicitly.
            if (!venueWorld && TheUI && TheUI->CurrentScreen()) {
                static bool sVenueLoadAttempted = false;
                const char *curScreenName = TheUI->CurrentScreen()->Name();
                if (!sVenueLoadAttempted && strcmp(curScreenName, "game_screen") == 0) {
                    sVenueLoadAttempted = true;
                    const char *venueName = TheGameData ? TheGameData->Venue().Str() : nullptr;
                    // GameData may have been cleared — fall back to env var
                    if (!venueName || !*venueName) venueName = getenv("DC3_VENUE");
                    if (!venueName || !*venueName) venueName = "glitterati";
                    if (venueName && *venueName) {
                        const char *miloPath = MakeString("world/%s/%s.milo", venueName, venueName);
                        FilePath fp;
                        fp.Set(FilePath::Root().c_str(), miloPath);
                        printf("DC3 Native: Loading gameplay venue '%s' from '%s'\n", venueName, fp.c_str());
                        ObjectDir *venueDir = DirLoader::LoadObjects(fp, nullptr, nullptr);
                        if (venueDir) {
                            WorldDir *wdir = dynamic_cast<WorldDir*>(venueDir);
                            if (wdir) {
                                if (TheHamDirector) {
                                    TheHamDirector->SetNativeVenueWorld(wdir);
                                    printf("DC3 Native: Venue '%s' set on HamDirector\n", wdir->Name());
                                } else {
                                    // No HamDirector — register as fallback venue
                                    gNativeVenueDir = wdir;
                                    printf("DC3 Native: Venue '%s' set as fallback (no HamDirector)\n", wdir->Name());
                                }
                                venueWorld = wdir;
                                // Register video_recorder.srec stub (DTA scripts expect it)
                                if (!wdir->FindObject("video_recorder.srec", false, false)) {
                                    Hmx::Object *stub = Hmx::Object::NewObject("Object");
                                    stub->SetName("video_recorder.srec", wdir);
                                }
                            } else {
                                printf("DC3 Native: Venue '%s' loaded but is NOT a WorldDir\n", venueName);
                            }
                        } else {
                            printf("DC3 Native: Failed to load venue '%s' from '%s'\n", venueName, fp.c_str());
                        }
                    }
                }
            }
#endif
            if (!venueWorld && gNativeVenueDir) {
                venueWorld = dynamic_cast<WorldDir*>(gNativeVenueDir);
            }
            if (venueWorld) {
                // One-shot venue setup: load components, force lighting
                {
                    static WorldDir* sLastPresetVenue = nullptr;
                    if (venueWorld != sLastPresetVenue) {
                        sLastPresetVenue = venueWorld;

                        // Load venue component .milo files not handled by DTA flow
                        {
                            const char* venueName = TheGameData ? TheGameData->Venue().Str() : nullptr;
#ifdef HX_NATIVE
                            // GameData may have been cleared by HamDirector::~HamDirector
                            // during screen transitions. Fall back to DC3_VENUE env var.
                            if (!venueName || !*venueName) {
                                venueName = getenv("DC3_VENUE");
                            }
#endif
                            if (!venueName || !*venueName) venueName = "glitterati";
                            static const char* componentSuffixes[] = {
                                "_buildings", "_sky", "_set", "_chairs", "_table_glasses", nullptr
                            };
                            int totalMerged = 0;
                            for (const char** suffix = componentSuffixes; *suffix; suffix++) {
                                const char* miloPath = MakeString(
                                    "world/%s/%s%s.milo", venueName, venueName, *suffix);
                                FilePath fp;
                                fp.Set(FilePath::Root().c_str(), miloPath);
                                ObjectDir* componentDir = DirLoader::LoadObjects(fp, nullptr, nullptr);
                                if (componentDir) {
                                    MergeFilter filt(
                                        (MergeFilter::Action)0,
                                        MergeFilter::kMergeInlinedMoveSharedSubdirs);
                                    MergeDirs(componentDir, venueWorld, filt);
                                    totalMerged++;
                                }
                            }
                            if (totalMerged > 0) {
                                printf("DC3 Native: loaded %d venue components for '%s'\n",
                                       totalMerged, venueName);
                                venueWorld->SyncObjects();
                            }
                        }

                        // DC3 doesn't use the LightPreset system — lighting is driven
                        // by PropAnims that directly animate RndLight properties.
                        // Lights have artist-authored initial on/off states; respect them.
                        {
                            int lightCount = 0;
                            int showingCount = 0;
                            for (ObjDirItr<RndLight> lit(venueWorld, true); lit != nullptr; ++lit) {
                                lightCount++;
                                if (lit->Showing()) showingCount++;
                            }
                            printf("DC3 Native: venue '%s' — %d lights (%d showing)\n",
                                   venueWorld->Name(), lightCount, showingCount);
                        }
                    }
                }

                venueWorld->Poll();

                // Reset character root positions to prevent drift from root motion.
                // Only for menu venues — gameplay venues need characters at stage positions.
                bool isMenuVenue = (venueWorld == dynamic_cast<WorldDir*>(gNativeVenueDir))
                                && !(TheHamDirector && TheHamDirector->GetVenueWorld());
                if (isMenuVenue) {
                    for (ObjDirItr<Character> it(venueWorld, true); it != nullptr; ++it) {
                        Transform& xfm = it->DirtyLocalXfm();
                        xfm.v.Set(0, 0, 0);
                        xfm.m.Identity();
                    }
                }
                // One-time face animation init: load viseme clips, wire CharEyes,
                // enable procedural blinking. On Xbox, FileMerger loads visemes
                // via OnConfigureFileMerger. On native we load them directly.
                if (!gFaceAnimInitDone) {
                    gFaceAnimInitDone = true;
                    for (ObjDirItr<HamCharacter> it(venueWorld, true); it != nullptr; ++it) {
                        CharFaceServo *servo = it->Find<CharFaceServo>("face.faceservo", false);
                        // Load viseme clips if face servo exists but has no base clip
                        if (servo && !servo->BaseClip() && !it->Outfit().Null()) {
                            Symbol charSym = GetOutfitCharacter(it->Outfit(), false);
                            if (!charSym.Null()) {
                                const char *visemePath = GetCharacterViseme(charSym, false);
                                if (visemePath && visemePath[0]) {
                                    FilePath fp;
                                    fp.Set(FilePath::Root().c_str(), visemePath);
                                    ObjectDir *visemeDir = DirLoader::LoadObjects(fp, nullptr, nullptr);
                                    if (visemeDir) {
                                        servo->SetClips(visemeDir);
                                        CharLipSyncDriver *lipDrv = it->Find<CharLipSyncDriver>("face.lipdrv", false);
                                        if (lipDrv) lipDrv->SetClips(visemeDir);
                                        fprintf(stderr, "DC3 Native: Loaded visemes for '%s' from '%s' — base=%p\n",
                                            it->Name(), visemePath, servo->BaseClip());
                                    }
                                }
                            }
                        }
                        // Ensure CharEyes has reference to face servo for procedural blinking
                        CharEyes *eyes = it->GetEyes();
                        if (eyes && servo) {
                            eyes->SetFaceServo(servo);
                        }
                        // Enable blinking if we have viseme clips now
                        if (servo && servo->BaseClip()) {
                            it->SetBlinking(true);
                            fprintf(stderr, "DC3 Native: Enabled blinking for '%s'\n", it->Name());
                        }
                        // Create interest objects so characters have something to look at.
                        // On Xbox these come from the character .milo files.
                        // Create one "audience" interest at the front of the stage.
                        if (eyes && eyes->NumInterests() == 0) {
                            CharInterest *camInterest = Hmx::Object::New<CharInterest>();
                            if (camInterest) {
                                // Position the interest 120 inches in front of the character
                                // at roughly audience/camera height
                                const Vector3 &charPos = it->WorldXfm().v;
                                Transform interestXfm;
                                interestXfm.m.Identity();
                                interestXfm.v.Set(charPos.x, charPos.y + 120.0f, charPos.z + 24.0f);
                                camInterest->SetLocalXfm(interestXfm);
                                eyes->AddInterestObject(camInterest);
                                fprintf(stderr, "DC3 Native: Added audience interest for '%s' at (%.1f,%.1f,%.1f)\n",
                                    it->Name(), interestXfm.v.x, interestXfm.v.y, interestXfm.v.z);
                            }
                        }
                    }
                }
            }
        }

        // Load game HUD milo on game_screen — the Xbox flow uses FileMerger's
        // load_game_hud handler (GameMode::SetGameplayMode → char_objects.dta),
        // which doesn't fire on native. Load directly instead.
        // HACK: This bypasses the FileMerger pipeline. The full flow
        // needs GameMode::SetGameplayMode() wired via FileMerger.
        if (!gNativeHudDir && TheUI && TheUI->CurrentScreen()
            && (strcmp(TheUI->CurrentScreen()->Name(), "game_screen") == 0
                || strcmp(TheUI->CurrentScreen()->Name(), "main_screen") == 0)) {
            const char *hudMilo = "ui/hud/_default_hud.milo";
            if (TheGameMode) {
                const DataNode *modeProp = TheGameMode->Property("gameplay_mode");
                if (modeProp && modeProp->Type() != kDataUnhandled) {
                    Symbol mode = modeProp->ForceSym(nullptr);
                    if (mode == "practice" || mode == "campaign_practice")
                        hudMilo = "ui/hud/_practice_hud.milo";
                    else if (mode == "bustamove")
                        hudMilo = "ui/hud/_bustamove_hud.milo";
                    else if (mode == "cascade")
                        hudMilo = "ui/hud/_cascade_hud.milo";
                }
            }
            FilePath hudFp;
            hudFp.Set(FilePath::Root().c_str(), hudMilo);
            fprintf(stderr, "DC3 Native: Loading HUD from '%s'\n", hudFp.c_str());
            ObjectDir *hudDir = DirLoader::LoadObjects(hudFp, nullptr, nullptr);
            if (hudDir) {
                gNativeHudDir = hudDir;
                DataVariable("hud_panel") = DataNode(hudDir);
                fprintf(stderr, "DC3 Native: HUD loaded — '%s' (%s)\n",
                       hudDir->Name(), hudDir->ClassName());

                RndDir *rdir = dynamic_cast<RndDir *>(hudDir);
                if (rdir) {
                    rdir->SyncObjects();
                    rdir->Enter();
                    fprintf(stderr, "DC3 Native: HUD Enter() — %d draws\n",
                           rdir->NumDraws());
                }

                // Sync sub-RndDir draw lists (score_left, score_right, etc.)
                for (ObjDirItr<RndDir> dit(hudDir, true); dit != nullptr; ++dit) {
                    if (&*dit == rdir) continue;
                    dit->SyncObjects();
                }

                // Load score.milo into score_left and score_right subdirs.
                // On Xbox, FileMerger handles this via load_game_hud.
                // HACK: Direct load bypasses FileMerger pipeline.
                {
                    FilePath scoreFp;
                    scoreFp.Set(FilePath::Root().c_str(), "ui/hud/score.milo");
                    const char *scoreSlots[] = {"score_left", "score_right", nullptr};
                    for (const char **sp = scoreSlots; *sp; sp++) {
                        RndDir *slot = hudDir->Find<RndDir>(*sp, true);
                        if (!slot) continue;
                        ObjectDir *scoreDir = DirLoader::LoadObjects(scoreFp, nullptr, nullptr);
                        if (scoreDir) {
                            ObjDirPtr<ObjectDir> scoreDirPtr(scoreDir);
                            slot->AppendSubDir(scoreDirPtr);
                            RndDir *rScore = dynamic_cast<RndDir*>(scoreDir);
                            if (rScore) {
                                rScore->SyncObjects();
                                rScore->Enter();
                            }
                            slot->SyncObjects();
                            fprintf(stderr, "DC3 Native: Loaded score.milo into '%s'\n", *sp);
                        }
                    }
                    // Re-sync parent draw list to include score subdirs
                    if (rdir) rdir->SyncObjects();
                }

                // Show everything, then selectively hide noisy elements.
                // ObjDirItr only recurses SubDirs(), so iterate sub-RndDirs too.
                // HACK: Full HUD needs MoveMgr::Init() on native.
                auto showAllInDir = [](ObjectDir *dir) {
                    for (ObjDirItr<RndDrawable> it(dir, true); it != nullptr; ++it) {
                        const char *n = it->Name();
                        // Always hide these fullscreen overlays
                        if (strcmp(n, "blacken.mesh") == 0
                            || strcmp(n, "PostProcer") == 0
                            || strcmp(n, "camera.mesh") == 0)
                            continue;
                        it->SetShowing(true);
                    }
                };
                showAllInDir(hudDir);
                for (ObjDirItr<RndDir> dit(hudDir, true); dit != nullptr; ++dit) {
                    if (&*dit == hudDir) continue;
                    showAllInDir(&*dit);
                }

                // Hide noisy containers that need game data we don't have.
                // Must also hide children since they're in separate namespaces.
                const char *hideDirs[] = {
                    "flashcard_dock", "photo_display", "photo_award_counter",
                    "text_recap", "challenge_target_left", "challenge_target_right",
                    "challenge_mission_info", nullptr
                };
                for (const char **hp = hideDirs; *hp; hp++) {
                    RndDir *sub = hudDir->Find<RndDir>(*hp, true);
                    if (sub) {
                        sub->SetShowing(false);
                        for (ObjDirItr<RndDrawable> it(sub, true); it != nullptr; ++it)
                            it->SetShowing(false);
                    }
                }
                // Also hide specific parent-level noisy elements
                // Iterate all drawables and hide by name pattern
                for (ObjDirItr<RndDrawable> it(hudDir, true); it != nullptr; ++it) {
                    const char *n = it->Name();
                    if (strstr(n, "photo") || strstr(n, "freestyle")
                        || strstr(n, "miss_streak")
                        || strstr(n, "instructional")) {
                        it->SetShowing(false);
                    }
                }
                // Same for sub-RndDirs
                for (ObjDirItr<RndDir> dit(hudDir, true); dit != nullptr; ++dit) {
                    if (&*dit == hudDir) continue;
                    const char *dn = dit->Name();
                    if (strstr(dn, "photo")) {
                        for (ObjDirItr<RndDrawable> it(&*dit, true); it != nullptr; ++it)
                            it->SetShowing(false);
                    }
                }
                // Hide flashcard subdirs inside hud_left/hud_right
                for (ObjDirItr<RndDir> dit(hudDir, true); dit != nullptr; ++dit) {
                    const char *n = dit->Name();
                    if (strstr(n, "flashcard_") || strstr(n, "freestyle_card")) {
                        dit->SetShowing(false);
                        for (ObjDirItr<RndDrawable> it(&*dit, true); it != nullptr; ++it)
                            it->SetShowing(false);
                    }
                }

                // Set text on labels — normally done by DTA handlers
                {
                    const char *songName = TheGameData ? TheGameData->GetSong().Str() : "BOYFRIEND";
                    RndText *songLbl = hudDir->Find<RndText>("song_name.lbl", true);
                    if (songLbl) songLbl->SetText(songName);

                    RndText *artistLbl = hudDir->Find<RndText>("song_artist.lbl", true);
                    if (artistLbl) {
                        const char *artist = "Unknown Artist";
                        if (TheGameData) {
                            int songID = TheHamSongMgr.GetSongIDFromShortName(
                                TheGameData->GetSong(), false);
                            if (songID >= 0) {
                                const HamSongMetadata *meta = TheHamSongMgr.Data(songID);
                                if (meta) artist = meta->Artist();
                            }
                        }
                        artistLbl->SetText(artist);
                        artistLbl->SetShowing(true);
                    }

                    // Set score labels — search recursively since score.milo
                    // is appended as a subdir of score_left.
                    // Also add them to the HUD's draw list since AppendSubDir
                    // sets IsSubDir=true which prevents SyncDrawables from
                    // including them automatically.
                    const char *scoreLabels[] = {"score_left", "score_right", nullptr};
                    for (const char **sp = scoreLabels; *sp; sp++) {
                        RndDir *sub = hudDir->Find<RndDir>(*sp, true);
                        if (!sub) continue;
                        // Use UILabel API instead of RndText — UILabel::SetInt
                        // properly triggers LabelUpdate + UpdateText mesh generation.
                        UILabel *scoreLbl = sub->Find<UILabel>("score2.lbl", true);
                        if (scoreLbl) {
                            scoreLbl->SetInt(0, true); // localized format
                            scoreLbl->SetShowing(true);
                            // HACK: score2.lbl has width=0 from the milo file.
                            // Width is normally set by DTA flow. Set directly.
                            if (scoreLbl->Width() < 1.0f)
                                scoreLbl->SetWidth(200.0f);
                            // Fix font alpha (same issue as song labels)
                            for (int si = 0; si < scoreLbl->NumStyles(); si++)
                                scoreLbl->Styles()[si].SetAlpha(1.0f);
                            // Add to parent draw list so rdir->DrawShowing() renders it
                            if (rdir) rdir->NativeAddDraw(scoreLbl);
                            fprintf(stderr, "DC3 Native: Score label '%s/%s' added to draw list\n",
                                *sp, scoreLbl->Name());
                        }
                        // Also show all drawables in the score subdir
                        for (ObjDirItr<RndDrawable> dit(sub, true); dit != nullptr; ++dit)
                            dit->SetShowing(true);
                    }
                    fprintf(stderr, "DC3 Native: HUD labels set — song='%s'\n", songName);
                }

                fprintf(stderr, "DC3 Native: HUD initialized\n");
            } else {
                fprintf(stderr, "DC3 Native: Failed to load HUD from '%s'\n", hudFp.c_str());
            }
        }

        // Select the venue's camera before drawing so the 3D scene uses
        // the correct camera position from the CameraManager's current shot.
        // WorldDir::DrawShowing() only runs camera management for the ROOT world
        // (TheWorld==nullptr). The venue is drawn as a child, so its camera
        // setup is skipped. We must Select() the venue's camera manually.
        {
            WorldDir *drawVenue = TheHamDirector ? TheHamDirector->GetVenueWorld() : nullptr;
            if (!drawVenue && gNativeVenueDir)
                drawVenue = dynamic_cast<WorldDir *>(gNativeVenueDir);
            if (drawVenue) {
                CameraManager *camMgr = drawVenue->GetCameraManager();
                if (camMgr) {
                    CamShot *curShot = camMgr->CurrentShot();
                    if (curShot) {
                        curShot = curShot->CurrentShot();
                        RndCam *cam = curShot ? curShot->GetCam() : nullptr;
                        if (cam)
                            cam->Select();
                    }
                }
            }
        }

        // Draw: matches Xbox flow — BeginDrawing → TheUI->Draw() → EndDrawing.
        // The venue renders through world_panel (loads ../world/world.milo).
        // HUD panels (game_panel etc.) render over the 3D scene.
        TheRnd.BeginDrawing();
        // TEMP: When DC3_HUD_ONLY is set, skip venue and draw only HUD
        if (TheUI && !getenv("DC3_HUD_ONLY"))
            TheUI->Draw();
        // Draw HUD overlay — on Xbox this is drawn as part of the game_screen
        // panel hierarchy via FileMerger. On native we draw it explicitly.
        // Replicates PanelDir::DrawShowing() setup: EndWorld() transitions to
        // 2D overlay mode, then select the HUD's own camera for 2D projection.
        if (gNativeHudDir && !getenv("DC3_NO_HUD_DRAW")) {
            RndDir *rdir = dynamic_cast<RndDir *>(gNativeHudDir);
            if (rdir) {
                // Switch to HUD rendering: clear depth (preserve venue color),
                // select the HUD's own 3D perspective camera (Cam.cam at y=-768).
                TheRnd.EndWorld();
                TheRnd.ClearDepthForOverlay();

                RndCam *prevCam = RndCam::Current();
                RndCam *hudCam = gNativeHudDir->Find<RndCam>("Cam.cam", false);
                if (!hudCam && TheUI) hudCam = TheUI->GetCam();
                if (hudCam && hudCam != prevCam) {
                    FlushTransparentDraws();
                    hudCam->Select();
                }

                // Select HUD environment for correct ambient lighting
                RndEnviron *hudEnv = gNativeHudDir->Find<RndEnviron>("static_hud.env", true);
                if (hudEnv) hudEnv->Select(nullptr);

                // Force-show ALL drawables every frame — the Flow/DTA
                // animation system normally controls visibility, but it
                // doesn't run on native. Without this, everything stays
                // at showing=0 and DrawShowing() draws nothing.
                // HACK: Skip elements that shouldn't show without gameplay.
                for (ObjDirItr<RndDrawable> drawIt(gNativeHudDir, true); drawIt != nullptr; ++drawIt) {
                    const char *n = drawIt->Name();
                    // Explicitly hide elements that shouldn't show without gameplay
                    if (strcmp(n, "blacken.mesh") == 0 ||
                        strcmp(n, "freestyle_bloom") == 0 ||
                        strcmp(n, "skeleton.lbl") == 0 ||
                        strcmp(n, "camera.mesh") == 0 ||
                        strstr(n, "photo")) {
                        drawIt->SetShowing(false);
                        continue;
                    }
                    drawIt->SetShowing(true);
                }

                // Force HUD material alpha to 1.0 — DTA flow animations that
                // normally control alpha never run on native, leaving materials
                // at alpha=0 (invisible).
                for (ObjDirItr<RndMat> matIt(gNativeHudDir, true); matIt != nullptr; ++matIt) {
                    if (matIt->Alpha() < 0.01f)
                        matIt->SetAlpha(1.0f);
                }

                // Force text font color alpha to 1.0 — DTA flows normally
                // animate mFontColor.alpha for text labels, but without flows
                // running, labels like song_name.lbl stay at fontAlpha=0.
                for (ObjDirItr<RndText> tit(gNativeHudDir, true); tit != nullptr; ++tit) {
                    for (int si = 0; si < tit->NumStyles(); si++) {
                        if (tit->Styles()[si].GetAlpha() < 0.01f)
                            tit->Styles()[si].SetAlpha(1.0f);
                    }
                }

                // Update HUD text labels every frame — song data may
                // not be available when HUD first loads.
                {
                    RndText *songLbl = gNativeHudDir->Find<RndText>("song_name.lbl", true);
                    if (songLbl && songLbl->GetText().length() == 0) {
                        const char *song = TheGameData ? TheGameData->GetSong().Str() : "";
                        if (song[0]) songLbl->SetText(song);
                        else songLbl->SetText("BOYFRIEND");
                    }
                }

                // Fix score labels every frame — Flow system inside
                // score.milo resets text/width/alpha continuously.
                {
                    const char *slots[] = {"score_left", "score_right", nullptr};
                    for (const char **sp = slots; *sp; sp++) {
                        RndDir *slot = gNativeHudDir->Find<RndDir>(*sp, true);
                        if (!slot) continue;
                        UILabel *scoreLbl = slot->Find<UILabel>("score2.lbl", true);
                        if (scoreLbl) {
                            scoreLbl->SetShowing(true);
                            if (scoreLbl->GetText().length() == 0)
                                scoreLbl->SetInt(0, false);
                            if (scoreLbl->Width() < 1.0f)
                                scoreLbl->SetWidth(200.0f);
                            for (int sti = 0; sti < scoreLbl->NumStyles(); sti++)
                                scoreLbl->Styles()[sti].SetAlpha(1.0f);
                        }
                    }
                }

                rdir->DrawShowing();

                // Score labels are now in rdir's draw list via NativeAddDraw
                // (added during HUD init). No explicit DrawShowing needed.

                // Restore previous camera and environment
                if (prevCam && prevCam != RndCam::Current()) {
                    FlushTransparentDraws();
                    prevCam->Select();
                }
            }
        }
        TheRnd.EndDrawing();

        frameCount++;
        if (frameCount % 1000 == 0) {
            printf("DC3 Native: Frame %d\n", frameCount);
        }

        // Periodic UI state dump
        if (frameCount % 500 == 0 && TheUI) {
            const char *curScreen = TheUI->CurrentScreen() ? TheUI->CurrentScreen()->Name() : "<none>";
            const char *transScreen = TheUI->TransitionScreen() ? TheUI->TransitionScreen()->Name() : "<none>";
            printf("DC3 UI State [frame %d]: current='%s' transition='%s' inTransition=%d\n",
                   frameCount, curScreen, transScreen, (int)TheUI->InTransition());
        }

        // Auto-navigate: DC3_SCREEN=<target> to skip menus
        // For game_screen: navigate step-by-step through the screen chain
        {
            static bool sAutoNavDone = false;
            static bool sGameSetupDone = false;
            if (!sAutoNavDone && TheUI && TheUI->CurrentScreen() && !TheUI->InTransition()) {
                const char *targetScreen = getenv("DC3_SCREEN");
                if (targetScreen && targetScreen[0]) {
                    const char *curName = TheUI->CurrentScreen()->Name();

                    // If we've reached the target, stop navigating
                    if (strcmp(curName, targetScreen) == 0) {
                        sAutoNavDone = true;
                    }
                    // Set up game data once when on main_screen and targeting game_screen
                    else if (strcmp(curName, "main_screen") == 0 && !sGameSetupDone) {
                        sGameSetupDone = true;
                        fprintf(stderr, "DC3 Native: Auto-nav at main_screen — target='%s' GameData=%p GameMode=%p\n",
                            targetScreen, (void*)TheGameData, (void*)TheGameMode);
                        if (strcmp(targetScreen, "game_screen") == 0 && TheGameData && TheGameMode) {
                            const char *songName = getenv("DC3_SONG");
                            if (!songName || !songName[0]) songName = "boyfriend";
                            TheGameData->SetSong(Symbol(songName));

                            // Venue: DC3_VENUE override > song metadata > fallback
                            const char *venueEnv = getenv("DC3_VENUE");
                            const char *venueName = nullptr;
                            if (venueEnv && venueEnv[0]) {
                                venueName = venueEnv;
                            } else {
                                int songID = TheHamSongMgr.GetSongIDFromShortName(Symbol(songName), false);
                                if (songID >= 0) {
                                    const HamSongMetadata *meta = TheHamSongMgr.Data(songID);
                                    if (meta) {
                                        Symbol v = meta->Venue();
                                        if (v != Symbol() && v.Str()[0])
                                            venueName = v.Str();
                                    }
                                }
                            }
                            if (!venueName || !venueName[0]) venueName = "glitterati";
                            TheGameData->SetVenue(Symbol(venueName));
                            TheGameMode->SetMode(Symbol("perform"), Symbol("none"));
                            if (TheHamProvider) {
                                TheHamProvider->SetProperty("merge_moves", 0);
                                TheHamProvider->SetProperty("use_movegraph", 0);
                            }
                            HamPlayerData *p0 = TheGameData->Player(0);
                            HamPlayerData *p1 = TheGameData->Player(1);
                            if (p0) p0->SetDifficulty(kDifficultyEasy);
                            if (p1) p1->SetDifficulty(kDifficultyEasy);
                            fprintf(stderr, "DC3 Native: Game setup — song='%s' venue='%s' mode=perform\n",
                                   songName, venueName);
                        }
                        // Navigate to choose_mode_screen (next in chain)
                        UIScreen *next = ObjectDir::Main()->Find<UIScreen>("choose_mode_screen", false);
                        if (next) {
                            fprintf(stderr, "DC3 Native: Auto-nav chain: main_screen → choose_mode_screen\n");
                            TheUI->GotoScreen(next, false, false);
                        }
                    }
                    // Chain: choose_mode → song_select
                    else if (strcmp(curName, "choose_mode_screen") == 0) {
                        UIScreen *next = ObjectDir::Main()->Find<UIScreen>("song_select_screen", false);
                        if (next) {
                            fprintf(stderr, "DC3 Native: Auto-nav chain: choose_mode → song_select\n");
                            TheUI->GotoScreen(next, false, false);
                        }
                    }
                    // Chain: song_select → multiuser_screen
                    else if (strcmp(curName, "song_select_screen") == 0) {
                        UIScreen *next = ObjectDir::Main()->Find<UIScreen>("multiuser_screen", false);
                        if (next) {
                            fprintf(stderr, "DC3 Native: Auto-nav chain: song_select → multiuser\n");
                            TheUI->GotoScreen(next, false, false);
                        }
                    }
                    // Chain: multiuser → loading_screen
                    else if (strcmp(curName, "multiuser_screen") == 0) {
                        UIScreen *next = ObjectDir::Main()->Find<UIScreen>("loading_screen", false);
                        if (next) {
                            fprintf(stderr, "DC3 Native: Auto-nav chain: multiuser → loading\n");
                            TheUI->GotoScreen(next, false, false);
                        }
                    }
                    // Non-game targets: direct jump from main_screen
                    else if (strcmp(curName, "main_screen") == 0) {
                        sAutoNavDone = true;
                        UIScreen *target = ObjectDir::Main()->Find<UIScreen>(targetScreen, false);
                        if (target) {
                            fprintf(stderr, "DC3 Native: Auto-navigating to '%s'\n", targetScreen);
                            TheUI->GotoScreen(target, false, false);
                        }
                    }
                }
            }
        }

        if (windowed) {
            if (glfwWindowShouldClose(gNativeWindow))
                break;
        } else {
            if (frameCount >= maxFrames) {
                printf("DC3 Native: %d frames completed, engine stable!\n", frameCount);
                break;
            }
        }
    }
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
