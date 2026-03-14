#pragma once
#include "NavListSortMgr.h"
#include "SongRecord.h"
#include "SongSort.h"

class VoiceControlPanel;

class SongSortMgr : public NavListSortMgr {
    friend class SongSort;
    friend class VoiceControlPanel;
public:
    virtual DataNode Handle(DataArray *, bool);
    virtual bool HeadersSelectable(); // 0x6c
    virtual bool SelectionIs(Symbol);
    virtual bool DataIs(int, Symbol);
    virtual DataNode OnCancel();
    virtual Symbol MoveOn();
    virtual void OnEnter();
    virtual int GetListIndexFromHeaderIndex(int);

    void MarkElementsProvided(UIListProvider *);
    void MarkElementInPlaylist(Symbol, bool);
    void OnHighlightChanged();
    void OnSetlistModeChanged();
    void OnSetlistChanged();
    void SetQuasiRandomSong();
    void RebuildSongRecordMap();
    void SetupQuasiRandomSongs();
    void SetSetlistMode(bool);
    Symbol DetermineHeaderSymbolForSong(Symbol);
    int FirstArtistSongIndex(Symbol);

    static void Init(SongPreview &);
    static void Terminate();

private:
    SongSortMgr(SongPreview &);
    virtual ~SongSortMgr();

protected:
    std::map<Symbol, SongRecord> mSongRecordMap; // 0x78
    int mRankedSongCount; // 0x90
    std::vector<int> mQuasiRandomIndices; // 0x94
};

extern SongSortMgr *TheSongSortMgr;
