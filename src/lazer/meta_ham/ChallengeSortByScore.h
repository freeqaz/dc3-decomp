#pragma once
#include "ChallengeSort.h"
#include "NavListNode.h"
#include "meta_ham/NavListNode.h"

class ChallengeScoreCmp : public NavListItemSortCmp {
public:
    ChallengeScoreCmp(int type, int score, Symbol songTitle)
        : mType(type), mScore(score), mSongTitle(songTitle) {}
    int Compare(const NavListItemSortCmp *, NavListNodeType) const;
    virtual const ChallengeScoreCmp *GetChallengeScoreCmp() const { return this; }

    int mType; // 0x04
    int mScore; // 0x08
    Symbol mSongTitle; // 0x0c
};

class ChallengeSortByScore : public ChallengeSort {
public:
    ChallengeSortByScore() {
        static Symbol by_score("by_score");
        mSortName = by_score;
    }
    virtual NavListItemNode *NewItemNode(void *) const;
    virtual NavListShortcutNode *NewShortcutNode(NavListItemNode *) const;
    virtual NavListHeaderNode *NewHeaderNode(NavListItemNode *) const;
    virtual NavListHeaderNode *NewHeaderNode(NavListItemNode *, NavListItemNode *) const;
};
