#pragma once
#include "NavListNode.h"
#include "SongSort.h"
#include "SongSortNode.h"
#include "meta_ham\NavListNode.h"

class DifficultyCmp : public NavListItemSortCmp {
public:
    DifficultyCmp(int tier, float rank, const char *name) {
        mTier = tier;
        mRank = rank;
        mName = name;
    };
    virtual ~DifficultyCmp();

    virtual int Compare(const NavListItemSortCmp *, NavListNodeType) const;
    virtual const DifficultyCmp *GetDifficultyCmp() const { return this; }

    int mTier; // 0x4
    float mRank; // 0x8
    const char *mName; // 0xC
};

class SongSortByDiff : public SongSort {
public:
    SongSortByDiff() {
        static Symbol by_difficulty("by_difficulty");
        SetSortName(by_difficulty);
    }
    virtual ~SongSortByDiff() {};

    virtual NavListItemNode *NewItemNode(void *) const;
    virtual NavListHeaderNode *NewHeaderNode(NavListItemNode *) const;
    virtual NavListHeaderNode *NewHeaderNode(NavListItemNode *, NavListItemNode *) const;
    virtual NavListShortcutNode *NewShortcutNode(NavListItemNode *) const;
};
