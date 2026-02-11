#pragma once
#include "NavListNode.h"
#include "Playlist.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "stl/_vector.h"
#include "ui/UILabel.h"
#include "ui/UIListCustom.h"
#include "ui/UIListLabel.h"
#include "utl/Symbol.h"

class PlaylistSortNode : public NavListItemNode {
public:
    virtual Symbol Select();
    virtual void Text(UIListLabel *, UILabel *) const;
    virtual void Custom(UIListCustom *, Hmx::Object *) const;
    virtual Symbol OnSelect();
    virtual void OnContentMounted(char const *, char const *);

    PlaylistSortNode(NavListItemSortCmp *, Playlist *);

protected:
    int unk48; // Force Playlist* to 0x4c
    Playlist *unk4c;
};

class PlaylistHeaderNode : public NavListHeaderNode {
public:
    // Hmx::Object
    virtual DataNode Handle(DataArray *, bool);

    // NavListSortNode
    virtual Symbol OnSelect();
    virtual Symbol OnSelectDone();
    virtual void OnHighlight();
    NavListSortNode *GetFirstActive();
    virtual void Text(UIListLabel *, UILabel *) const;
    virtual bool IsActive() const;
    char const *GetAlbumArtPath();
    virtual void Renumber(std::vector<NavListSortNode *> &);

    PlaylistHeaderNode(NavListItemSortCmp *, Symbol, bool);

protected:
    int mChallengeCount;
};
