#include "meta_ham/LoadingPanel.h"
#include "game/SongDB.h"
#include "hamobj/HamAudio.h"
#include "hamobj/HamMaster.h"
#include "macros.h"
#include "meta/DataArraySongInfo.h"
#include "meta_ham/ContextChecker.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os/ContentMgr.h"
#include "os/Debug.h"
#include "os/File.h"
#include "os/FileCache.h"
#include "os/System.h"
#include "synth/Stream.h"
#include "ui/UI.h"
#include "ui/UIPanel.h"
#include "utl/BeatMap.h"
#include "utl/MakeString.h"
#include "utl/MemMgr.h"
#include "utl/Symbol.h"
#include "utl/TimeConversion.h"
#include "utl/TempoMap.h"

HamMaster *LoadingPanel::sLoadingMaster = nullptr;
SongDB *LoadingPanel::sSongDB = nullptr;

#ifdef HX_NATIVE
static bool sSkipLoadingMusicReadyGate = false;
#endif

LoadingPanel::LoadingPanel() : mSongInfo(0), mTempoMap(), mBeatMap(0) { sSongDB = new SongDB(); }

LoadingPanel::~LoadingPanel() {
    RELEASE(sSongDB);
    RELEASE(sLoadingMaster);
}

BEGIN_PROPSYNCS(LoadingPanel)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

char const *LoadingPanel::GetLoadingScreen(Symbol s) {
    DataArray *screenArray = SystemConfig("loading_screens");
    for (int i = 1; i < screenArray->Size(); i++) {
        DataArray *entry = screenArray->Array(i);
        Symbol entrySym = entry->Sym(0);
        if (entrySym == s) {
            return entry->Str(1);
        }
    }
    MILO_FAIL("can\'t find loadingScreen %s", s);
    return "";
}

void LoadingPanel::Unload() {
    if (mTempoMap) {
        SetTheTempoMap(mTempoMap);
    }

    if (mBeatMap) {
        SetTheBeatMap(mBeatMap);
    }

    delete sLoadingMaster;
    UIPanel::Unload();
}

void LoadingPanel::Load() {
    UIPanel::Load();
    sLoadingMaster = new HamMaster(sSongDB->SongData(), nullptr);
#ifdef HX_NATIVE
    sSkipLoadingMusicReadyGate = false;
#endif
    PlayLoadingMusic();
    sLoadingMaster->SetMaps();
}

bool LoadingPanel::IsLoaded() const {
    HamAudio *pAudio = sLoadingMaster->GetAudio();
    if (!pAudio) {
        MILO_NOTIFY("missing audio object!\n");
    }

#ifdef HX_NATIVE
    bool audioReady = !pAudio || pAudio->Fail() || pAudio->IsReady();
    if (!audioReady && sSkipLoadingMusicReadyGate) {
        audioReady = true;
    }
    return TheContentMgr.RefreshDone() && UIPanel::IsLoaded() && audioReady;
#else
    return TheContentMgr.RefreshDone() && UIPanel::IsLoaded()
        && (!pAudio || pAudio->Fail() || pAudio->IsReady());
#endif
}

bool LoadingPanel::Exiting() {
    if (mState == kDown && !TheUI->TransitionScreen()->IsLoaded()) {
        return true;
    }
    return UIPanel::Exiting();
}

void LoadingPanel::Enter() {
    UIPanel::Enter();
    TheTaskMgr.SetSecondsAndBeat(0, 0, true);
#ifdef HX_NATIVE
    // Loading music stream may not be ready on native (DTA variable not set)
    if (sLoadingMaster->GetHxAudio()->IsReady()) {
        Stream *stream = sLoadingMaster->GetHxAudio()->GetSongStream();
        if (stream) {
            stream->SetJump(Stream::kStreamEndMs, 0.0f, nullptr);
            stream->Play();
        }
    }
#else
    Stream *stream = sLoadingMaster->GetHxAudio()->GetSongStream();
    MILO_ASSERT(sLoadingMaster->GetHxAudio()->IsReady(), 0x6a);
    stream->SetJump(Stream::kStreamEndMs, 0.0f, nullptr);
    stream->Play();
#endif
}

void LoadingPanel::Poll() {
    UIPanel::Poll();
#ifdef HX_NATIVE
    if (sSkipLoadingMusicReadyGate) {
        return;
    }
#endif

    Stream *pStream = sLoadingMaster->GetHxAudio()->GetSongStream();
    MILO_ASSERT(pStream && pStream->IsPlaying(), 0x46);

    float streamMs = pStream->GetTime();
    TempoMap *tempoMap = sLoadingMaster->SongData()->GetTempoMap();
    if (TheTempoMap != tempoMap) {
        mTempoMap = TheTempoMap;
        SetTheTempoMap(tempoMap);
    }

    BeatMap *beatMap = sLoadingMaster->SongData()->GetBeatMap();
    if (TheBeatMap != beatMap) {
        mBeatMap = TheBeatMap;
        SetTheBeatMap(beatMap);
    }

    sLoadingMaster->Poll(streamMs);
    float beat = MsToBeat(sLoadingMaster->StreamMs());
    if (beat > 0.0f) {
        TheTaskMgr.SetSecondsAndBeat(sLoadingMaster->StreamMs() * 0.001f, beat, false);
    }
}

Symbol LoadingPanel::ChooseLoadingScreen() {
    Symbol randomItem =
        RandomContextSensitiveItem(SystemConfig("loading_screen_context"));
    return GetLoadingScreen(randomItem);
}

void LoadingPanel::PlayLoadingMusic() {
    static DataNode &n = DataVariable("loading_music_mogg");
    if (n.Equal(gNullStr, nullptr, true)) {
        ResetLoadingMusic();
    }

    const char *fileBase = FileGetBase(n.Str());

    // Verify MIDI file exists (scoped to ensure String destructor runs before next operations)
    {
        String filePath = MakeString("sfx/samples/shell/%s.mid", fileBase);
        File *f = FileCache::GetFileAll(filePath.c_str());
#ifdef HX_NATIVE
        if (!f) {
            MILO_WARN("LoadingPanel: loading music MIDI not found: %s", filePath.c_str());
            sSkipLoadingMusicReadyGate = true;
            return;
        }
#else
        MILO_ASSERT(f != NULL, 0xb7);
#endif
        delete f;
    }

    if (mSongInfo) {
        RELEASE(mSongInfo);
    }

    DataArray *sysConfig = SystemConfig("synth", fileBase);
    static Symbol song("song");
    DataArray *songArray = sysConfig->FindArray(song, false);
    mSongInfo = new DataArraySongInfo(songArray, nullptr, "loadmusic");
    sLoadingMaster->Load(mSongInfo, false, 0, false, (HamSongDataValidate)0, nullptr);
}

BEGIN_HANDLERS(LoadingPanel)
    HANDLE_EXPR(choose_loading_screen, ChooseLoadingScreen())
    HANDLE_SUPERCLASS(UIPanel)
END_HANDLERS

void ResetLoadingMusic() {
    static Symbol reset_loading_music_mogg("reset_loading_music_mogg");
    static DataArrayPtr func(new DataArray(1));
    func.Node(0) = reset_loading_music_mogg;
    func->Execute(false);
}
