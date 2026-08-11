#pragma once

#include "beatmatch\HxMaster.h"
#include "hamobj\HamSongData.h"
#include "midi\DataEventList.h"
#include "obj/Object.h"
#include "utl\MemMgr.h"
#include "utl\SongPos.h"

class SongDB {
public:
    SongDB();
    ~SongDB();
    SongPos CalcSongPos(HxMaster *, float);
    void PostLoad(DataEventList *);

    float GetSongDurationMs() const;

    HamSongData *SongData() const { return mSongData; }
    MEM_OVERLOAD(SongDB, 0x1c);

private:
    void ParseEvents(DataEventList *);

    HamSongData *mSongData; // 0x0
    float mSongDurationMs; // 0x4
};

extern SongDB *TheSongDB;
