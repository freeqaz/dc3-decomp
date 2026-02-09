#pragma once
#include "MQSongSort.h"
#include "NavListNode.h"
class MQSongCharCmp : public NavListItemSortCmp {
public:
    MQSongCharCmp(const char *c, const char *c2) : mSongName(c), mCharacterName(c2){}
    virtual int Compare(const NavListItemSortCmp *, NavListNodeType) const;

    const char *mSongName;
    const char *mCharacterName;
};

class MQSongSortByCharacter : public MQSongSort {
public:
    MQSongSortByCharacter() {
        static Symbol by_character("by_character");
        mSortName = by_character;
    }
    virtual ~MQSongSortByCharacter();

    virtual NavListItemNode *NewItemNode(void *) const;
    virtual NavListHeaderNode *NewHeaderNode(NavListItemNode *) const;
    virtual NavListHeaderNode *NewHeaderNode(NavListItemNode *, NavListItemNode *) const;
    virtual NavListShortcutNode *NewShortcutNode(NavListItemNode *) const;
};
