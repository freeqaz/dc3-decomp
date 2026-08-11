#pragma once
#include "obj\Data.h"
#include "obj/Object.h"
#include "os/FileCache.h"
#include "rndobj\Poll.h"
#include "stl\_vector.h"
#include "utl\Symbol.h"

class SongSequence : public RndPollable {
public:
    // size 0x3c
    struct Entry {
        Symbol mSongShortName; // 0x00
        Symbol mSongLongName;  // 0x04
        Symbol mGameplayMode;  // 0x08 - gameplay mode (perform/holla_back/mind_control)
        float mIntroLoopMeasure;  // 0x0c - intro loop measure position
        float mOutroLoopMeasure;  // 0x10 - outro loop measure position
        Symbol mModeConfig;       // 0x14 - holla_back_config mode symbol
        float mEventStartMeasure; // 0x18 - event loop start (measures)
        float mEventEndMeasure;   // 0x1c - event loop end (measures)
        bool mIsIntro;         // 0x20
        bool mIsOutro;         // 0x21
        Symbol mIntroCamShot;  // 0x24
        Symbol mOutroCamShot;  // 0x28
        Symbol mCrew1Symbol;   // 0x2c
        Symbol mCrew2Symbol;   // 0x30
        int mTotalScore;       // 0x34 - total score
        int mStarCount;        // 0x38 - star count
    };

    SongSequence();
    virtual ~SongSequence();
    virtual DataNode Handle(DataArray *, bool);

    bool Done() const;
    void LoadNextSongAudio();
    Symbol GetIntroCamShot() const;
    Symbol GetOutroCamShot() const;
    void OnSongLoaded();
    void Clear();
    bool DoNext(bool, bool);
    void Init();
    void Add(const DataArray *);
    int CurrentIndex() const { return mCurrentIndex; }
    bool GetVenueEntered() const { return mVenueEntered; }
    void SetVenueEntered(bool val) { mVenueEntered = val; }

protected:
    std::vector<Entry> mEntries;       // 0x8
    int mCurrentIndex;                 // 0x14
    float mPrevSongPosition;           // 0x18 - timestamp for DoNext rate limiting
    float mNextSongLoadPosition;       // 0x1c
    u32 unk20;                         // 0x20
    float mCurrentPlaybackPosition;    // 0x24 - UISeconds at song load start
    bool mVenueEntered;                // 0x28
    FileCache *mFileCache; // 0x2c
};

extern SongSequence TheSongSequence;
