
#pragma once
#include "SongSortNode.h"
#include "meta_ham\NavListNode.h"
#include "meta_ham\NavListSort.h"
#include "ui\UILabel.h"
#include "ui\UIListLabel.h"

class SongSort : public NavListSort {
public:
    SongSort();
    virtual ~SongSort() {};
    virtual DataNode Handle(DataArray *, bool);
    virtual void BuildTree();
    virtual void DeleteItemList();
    virtual void BuildItemList();
    virtual void SetHighlightedIx(int);
    virtual void SetHighlightItem(const NavListSortNode *);
    virtual void UpdateHighlight();
    virtual void OnSelectShortcut(int);
    virtual void Text(int, int, UIListLabel *, UILabel *) const;

    Symbol DetermineHeaderSymbolFromSong(Symbol);

#ifdef HX_NATIVE
    /** Puts the Recently Played section in front of the sort's own sections
     * (native/src/platform/SongSelect_Native.cpp); nothing when there is no
     * history, under DC3_CLASSIC_SONG_SELECT, or outside song select proper. */
    void AddRecentlyPlayedSection();
#endif
};
