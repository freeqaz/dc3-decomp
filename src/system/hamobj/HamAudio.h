#pragma once
#include "beatmatch\HxAudio.h"
#include "obj\Data.h"
#include "obj/Object.h"
#include "synth\Faders.h"
#include "utl/Loader.h"
#include "utl\SongInfoCopy.h"
#include "utl\Symbol.h"

/** One crossfade timeline: the two loop endpoints, the fade length, and a
 *  state/pending flag.  Two of these live back to back in HamAudio at 0x5c
 *  and 0x6c, and PollCrossfade copies one onto the other field for field. */
struct HamCrossfade {
    float mStart; // 0x0
    float mEnd; // 0x4
    float mDuration; // 0x8
    /** Pending flag on the request, state on the active one. */
    int mFlag; // 0xc

    HamCrossfade() : mFlag(0) {}
};

class HamAudio : public Hmx::Object, public HxAudio {
public:
    HamAudio();
    // Hmx::Object
    virtual ~HamAudio();
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);
    // HxAudio
    virtual bool IsReady();
    virtual bool Paused() const;
    virtual void SetPaused(bool);
    virtual void Poll();
    virtual float GetTime() const;
    virtual Stream *GetSongStream() { return mSongStream; }
    virtual void SetMasterVolume(float);

    void SetMuteMaster(bool mute);
    void SetChannelVolume(int, float);
    void SetLoop(float, float);
    void ClearLoop();
    void Jump(float);
    void FinishLoad();
    bool Fail();
    bool IsFinished() const;
    void Load(SongInfo *, bool);
    void Play();
    bool GetCurrLoopMarkers(float &, float &) const;
    bool GetCurrLoopBeats(int &, int &) const;
    void SetCrossfadeJump(float, float, float);
    // Whether a crossfade is queued. Exposed for the crossfade-boundary
    // regression test (SetCrossfadeJump clears this when the crossfade is
    // judged invalid).
    bool CrossfadePending() const { return mCrossfade.mFlag != 0; }
#ifdef HX_NATIVE
    // Test-only seams (milo-tests crossfade regression); never compiled on Xbox.
    void SetStreamsForTest(Stream *a, Stream *b) {
        mStreams[0] = a;
        mStreams[1] = b;
    }
    void SetCrossfadeStateForTest(int s) { mActiveCrossfade.mFlag = s; }
#endif

    void SetBackgroundVolume(float);
    void SetForegroundVolume(float);
    void SetStereo(bool);
    void SetPracticeMode(bool) {}

    DataNode OnGetCurrentLoopBeats(DataArray *);
    DataNode OnSetCrossfadeJump(DataArray *);

private:
    void UpdateMasterFader();
    void Clear();
    void ToggleMuteMaster();
    void PrintFaders();
    void PollCrossfade();
    void DeleteFaders();
    void SetLoop(float, float, Stream *);

    FileLoader *mFileLoader; // 0x30
    char *mRawBuffer; // 0x34
    int mRawBufferSize; // 0x38
    SongInfo *mSongInfo; // 0x3c
    Stream *mSongStream; // 0x40
    Stream *mStreams[2]; // 0x44
    bool mReady; // 0x4c
    Fader *mMasterFader; // 0x50
    float mMasterVolume; // 0x54
    bool mMuteMaster; // 0x58
    bool mFXSendApplied; // 0x59
    /** The pending crossfade request. */
    HamCrossfade mCrossfade; // 0x5c
    /** The crossfade currently running. */
    HamCrossfade mActiveCrossfade; // 0x6c
    Fader *mCrossFaders[2]; // 0x7c
    std::vector<Fader *> mChannelFaders; // 0x84
    std::map<Symbol, Fader *> mTrackFaders; // 0x90
};
