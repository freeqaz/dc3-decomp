#include "App.h"
#ifdef HX_NATIVE
#include <algorithm>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "ui/UIPanel.h"
#include "ui/PanelDir.h"
#include "rndobj/Dir.h"
extern GLFWwindow *gNativeWindow;
#endif
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

    // Character system
    CharInit();

    // World system
    WorldInit();

    // Ham (game-specific) system
    HamInit();

    // Song manager
    TheHamSongMgr.Init();

    // Game subsystem inits (from original init sequence)
    UIEventMgr::Init();
    MetaPanel::Init();
    GameInit();

    // UI system
    TheUI = new UIManager();
    TheUI->Init();

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
            allocatedFileCache = new (allocatedFileCache)
                FileCache(persistentCacheConfig->Node(1).Int(persistentCacheConfig), kLoadFront, false, true);
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

    const char *symbolText = *(const char **)&symbol;
    const char *scanPtr = symbolText;
    unsigned char scanCh;
    do {
        scanCh = (unsigned char)*scanPtr;
        ++scanPtr;
    } while (scanCh != '\0');

    int symbolLen = scanPtr - symbolText;
    symbolLen -= 1;
    if (symbolLen < 1) {
        TheDebug.Fail(MakeString(kAssertStr, "App.cpp", 0x2AB, "len > 0"), nullptr);
    }

#ifdef HX_NATIVE
    int copyLen = std::find_if(
                      symbolText,
                      symbolText + symbolLen,
                      static_cast<int (*)(int)>(isdigit)
    ) - symbolText;
#else
    stlpmtx_std::random_access_iterator_tag findTag;
    int copyLen = stlpmtx_std::__find_if(
                      symbolText,
                      symbolText + symbolLen,
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
        printf("DC3 Native: Windowed mode — close window or press ESC to exit\n");
    else
        printf("DC3 Native: Headless mode — running %d frames\n", maxFrames);

    while (true) {
        SystemPoll(false);

        if (TheUI)
            TheUI->Poll();

        TheTaskMgr.Poll();

        if (TheFlowMgr)
            TheFlowMgr->Poll();

        TheRnd.BeginDrawing();
        if (TheUI)
            TheUI->Draw();

        // Optional: force-draw a specific panel (for render debugging)
        // Set MILO_FORCE_DRAW_PANEL=cursor_panel to enable
        {
            static bool sForceDrawChecked = false;
            static PanelDir *sForceDrawDir = nullptr;
            if (!sForceDrawChecked && frameCount >= 50) {
                sForceDrawChecked = true;
                const char *forcePanelName = getenv("MILO_FORCE_DRAW_PANEL");
                if (forcePanelName && forcePanelName[0]) {
                    UIPanel *p = ObjectDir::Main()->Find<UIPanel>(forcePanelName, false);
                    if (p && p->LoadedDir()) {
                        sForceDrawDir = p->LoadedDir();
                        printf("DC3 Render: Force-drawing panel '%s'\n", forcePanelName);
                    } else {
                        printf("DC3 Render: Panel '%s' not found or not loaded\n", forcePanelName);
                    }
                }
            }
            if (sForceDrawDir) {
                sForceDrawDir->DrawShowing();
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
            waiverMs = Min(slowMs, waiverMs);
            float frameMs = loopMs - waiverMs;

            if (frameMs > 83.3333f) {
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
