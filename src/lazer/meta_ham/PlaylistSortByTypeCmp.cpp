#include "meta_ham/PlaylistSort.h"
#include "meta_ham/PlaylistSortNode.h"
#include "os/Debug.h"
#include "utl/MakeString.h"
#include "utl/Symbol.h"

class PlaylistTypeCmp : public NavListItemSortCmp {
public:
    virtual ~PlaylistTypeCmp() {}
    virtual int Compare(const NavListItemSortCmp *, NavListNodeType) const;
    virtual const PlaylistTypeCmp *GetPlaylistTypeCmp() const { return this; }

    int mType;         // 0x4
    const char *mName; // 0x8
};

int PlaylistTypeCmp::Compare(
    const NavListItemSortCmp *other, NavListNodeType nodeType
) const {
    if (nodeType == kNodeShortcut) {
        return 0;
    } else if (nodeType == kNodeHeader) {
        const PlaylistTypeCmp *otherCmp = other->GetPlaylistTypeCmp();
        return mType - otherCmp->mType;
    } else if (nodeType == kNodeItem) {
        other->GetPlaylistTypeCmp();
        return -1;
    } else {
        TheDebug.Fail(FormatString("invalid type of node comparison.").Str(), 0);
    }
    return 0;
}

NavListShortcutNode *
PlaylistSortByType::NewShortcutNode(NavListItemNode *item) const {
    Playlist *playlist = static_cast<PlaylistSortNode *>(item)->GetPlaylist();
    int type;
    Symbol sym;
    if (playlist->IsCustom()) {
        type = 1;
        static Symbol playlist_custom("playlist_custom");
        sym = playlist_custom;
    } else if (playlist->GetIsBattlePlaylist()) {
        type = 4;
        static Symbol playlist_fitness("playlist_fitness");
        sym = playlist_fitness;
    } else if (playlist->GetIsFriendPlaylist()) {
        type = 2;
        static Symbol playlist_era("playlist_era");
        sym = playlist_era;
    } else {
        type = 3;
        static Symbol playlist_crew("playlist_crew");
        sym = playlist_crew;
    }

    PlaylistTypeCmp *cmp = new PlaylistTypeCmp();
    cmp->mType = type;
    cmp->mName = "";
    return new NavListShortcutNode(cmp, sym, true);
}

NavListHeaderNode *
PlaylistSortByType::NewHeaderNode(NavListItemNode *item) const {
    Playlist *playlist = static_cast<PlaylistSortNode *>(item)->GetPlaylist();
    int type;
    Symbol sym;
    if (playlist->IsCustom()) {
        type = 1;
        static Symbol playlist_custom("playlist_custom");
        sym = playlist_custom;
    } else if (playlist->GetIsBattlePlaylist()) {
        type = 4;
        static Symbol playlist_fitness("playlist_fitness");
        sym = playlist_fitness;
    } else if (playlist->GetIsFriendPlaylist()) {
        type = 2;
        static Symbol playlist_era("playlist_era");
        sym = playlist_era;
    } else {
        type = 3;
        static Symbol playlist_crew("playlist_crew");
        sym = playlist_crew;
    }

    PlaylistTypeCmp *cmp = new PlaylistTypeCmp();
    cmp->mType = type;
    cmp->mName = "";
    return new PlaylistHeaderNode(cmp, sym, true);
}

PlaylistSortNode::PlaylistSortNode(NavListItemSortCmp *cmp, Playlist *playlist)
    : NavListItemNode(cmp) {
    mPlaylist = playlist;
}

NavListItemNode *PlaylistSortByType::NewItemNode(void *data) const {
    Playlist *playlist = (Playlist *)data;
    int type;
    if (playlist->IsCustom()) {
        type = 1;
    } else if (playlist->GetIsBattlePlaylist()) {
        type = 4;
    } else {
        bool isFriend = playlist->GetIsFriendPlaylist();
        type = isFriend ? 2 : 3;
    }
    const char *name = playlist->GetName().Str();

    PlaylistTypeCmp *cmp = new PlaylistTypeCmp();
    cmp->mType = type;
    cmp->mName = name;
    return new PlaylistSortNode(cmp, playlist);
}
