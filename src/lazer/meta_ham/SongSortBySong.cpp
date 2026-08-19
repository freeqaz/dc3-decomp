#include "SongSortBySong.h"

#include "meta\Sorting.h"
#include "meta_ham\NavListNode.h"
#include "meta_ham\SongSortNode.h"
#include "ui\UIListWidget.h"
#include <cstdio>


SongCmp::~SongCmp() {}

int SongCmp::Compare(const NavListItemSortCmp *cmp, NavListNodeType type) const {
    switch (type) {
    case kNodeShortcut:
        return 0;

    case kNodeHeader: {
        const SongCmp *songCmp = cmp->GetSongCmp();
        int iVar3 = (signed char)*mSortKey - (signed char)*songCmp->mSortKey;
        if (mSortKeyEnd == 0)
            return iVar3;
        if (0 < iVar3)
            return iVar3;
        int iVar4 = (signed char)*mSortKeyEnd - (signed char)*songCmp->mSortKey;
        return iVar4 >> 31 & iVar4;
    }
    case kNodeItem: {
        const SongCmp *songCmp = cmp->GetSongCmp();
        return AlphaKeyStrCmp(mSortKey, songCmp->mSortKey, false);
    }
    default:
        MILO_FAIL("invalid type of node comparison.\n");
    }
    return 0;
}

NavListHeaderNode *SongSortBySong::NewHeaderNode(NavListItemNode *p1) const {
    SongSortNode *node = dynamic_cast<SongSortNode *>(p1);
    const char *title = node->Record()->Metadata()->Title();
    SongCmp *newCmp = new SongCmp(title, 0);
    SongCmp *cmp;
    if (newCmp != 0) {
        cmp = newCmp;
    } else {
        cmp = 0;
    }
    char sortLetter[2];
    sortLetter[0] = 0;
    sortLetter[1] = 0;
    sortLetter[0] = title[0];
    Symbol sortSym(sortLetter);

    return new SongHeaderNode(cmp, sortSym, true);
}

NavListHeaderNode *
SongSortBySong::NewHeaderNode(NavListItemNode *n1, NavListItemNode *n2) const {
    SongSortNode *node1 = dynamic_cast<SongSortNode *>(n1);
    SongSortNode *node2 = dynamic_cast<SongSortNode *>(n2);

    const char *title1 = node1->Record()->Metadata()->Title();
    const char *title2 = node2->Record()->Metadata()->Title();

    SongCmp *cmp = new SongCmp(title1, title2);

    char letter1[2];
    letter1[0] = 0;
    letter1[1] = 0;
    letter1[0] = title1[0];
    char letter2[2];
    letter2[0] = 0;
    letter2[1] = 0;
    letter2[0] = title2[0];

    char sortLabel[8];
    sortLabel[0] = 0;
    sortLabel[1] = 0;
    sortLabel[2] = 0;
    sortLabel[3] = 0;
    sortLabel[4] = 0;
    sortLabel[5] = 0;
    sortLabel[6] = 0;
    sortLabel[7] = 0;
    if (*title1 == *title2) {
        sprintf(sortLabel, "%s", letter1);
    } else {
        sprintf(sortLabel, "%s - %s", letter1, letter2);
    }
    Symbol sortSym(sortLabel);

    return new SongHeaderNode(cmp, sortSym, true);
}

NavListShortcutNode *SongSortBySong::NewShortcutNode(NavListItemNode *p1) const {
    SongSortNode *node = dynamic_cast<SongSortNode *>(p1);
    const char *title = node->Record()->Metadata()->Title();
    SongCmp *newCmp = new SongCmp(title, 0);
    SongCmp *cmp;
    if (newCmp != 0) {
        cmp = newCmp;
    } else {
        cmp = 0;
    }
    char sortLetter[2];
    sortLetter[0] = 0;
    sortLetter[1] = 0;
    sortLetter[0] = title[0];
    Symbol sortSym(sortLetter);

    return new NavListShortcutNode(cmp, sortSym, true);
}

NavListItemNode *SongSortBySong::NewItemNode(void *p1) const {
    SongRecord *record = static_cast<SongRecord *>(p1);
    const char *title = record->Metadata()->Title();
    SongCmp *cmp = new SongCmp(title, nullptr);

    return new SongSortNode(cmp, record);
}

