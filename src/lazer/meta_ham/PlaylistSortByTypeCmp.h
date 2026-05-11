#pragma once

#include "meta_ham/NavListNode.h"
#include "meta_ham/PlaylistSort.h"
#include "utl/Symbol.h"

class PlaylistTypeCmp : public NavListItemSortCmp {
public:
    virtual ~PlaylistTypeCmp() {}
    virtual int Compare(NavListItemSortCmp const *, NavListNodeType) const;
    virtual const PlaylistTypeCmp *GetPlaylistTypeCmp() const { return this; }

    PlaylistTypeCmp(int type, const char *name) : mType(type), mName(name) {}

    int mType;
    const char *mName;
};

