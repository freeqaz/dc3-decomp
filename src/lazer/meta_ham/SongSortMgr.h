#pragma once
#include "NavListSortMgr.h"
#include "SongRecord.h"
#include "SongSort.h"

class VoiceControlPanel;

class SongSortMgr : public NavListSortMgr {
    friend class SongSort;
    friend class VoiceControlPanel;
    friend class VoiceInputPanel;
public:
    virtual DataNode Handle(DataArray *, bool);
    virtual bool HeadersSelectable(); // 0x6c
    virtual bool SelectionIs(Symbol);
    virtual bool DataIs(int, Symbol);
    virtual DataNode OnCancel();
    virtual Symbol MoveOn();
    virtual void OnEnter();
    virtual int GetListIndexFromHeaderIndex(int);

    std::map<Symbol, SongRecord> &GetUnk78() { return mSongRecordMap; }
    unsigned int MapSize() { return mSongRecordMap.size(); }

    void MarkElementsProvided(UIListProvider *);
    void MarkElementInPlaylist(Symbol, bool);
    void OnHighlightChanged();
    void OnSetlistModeChanged();
    void OnSetlistChanged();
    void SetQuasiRandomSong();
    void RebuildSongRecordMap();
    void SetupQuasiRandomSongs();
    void SetSetlistMode(bool);
    bool GetSetlistMode();
    Symbol DetermineHeaderSymbolForSong(Symbol);
    int FirstArtistSongIndex(Symbol);

    static void Init(SongPreview &);
    static void Terminate();

#ifdef HX_NATIVE
    // Native song select: Recently Played, the artist sort, Left/Right and
    // the remembered sort.  Defined in native/src/platform/SongSelect_Native.cpp;
    // see docs/native/SONG_SELECT.md.
    /** The messages song_select.dta and HamNavList send that the image does
     * not have (nav_jump_row, sort_labels, saved_sort_index, save_sort,
     * save_focus, restore_focus). */
    DataNode NativeHandle(DataArray *);
    /** Where a section jump (Left/Right) or a page (LB/RB) takes the
     * highlight from row `selected`; -1 to stay where it is. */
    int NavJumpRow(int action, int selected, int pageRows);
    /** The sort names from the current sort on, for the help bar. */
    DataArray *SortLabels();
    /** The index of the sort remembered from the last launch, else 0. */
    int SavedSortIndex();
    void SaveSort();
    /** Remember the list's selected row by name, and find it again on the
     * way back: the list index the image's script keeps is another row once
     * Recently Played has changed. */
    void SaveFocus(int row);
    void RestoreFocus(Hmx::Object *panel);
#endif

private:
    SongSortMgr(SongPreview &);
    virtual ~SongSortMgr();

protected:
    std::map<Symbol, SongRecord> mSongRecordMap; // 0x78
    int mRankedSongCount; // 0x90
    std::vector<int> mQuasiRandomIndices; // 0x94
};

extern SongSortMgr *TheSongSortMgr;
