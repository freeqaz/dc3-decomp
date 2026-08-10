#include "meta_ham\PlaylistSort.h"
#include "meta_ham\PlaylistSortByTypeCmp.h"
#include "meta_ham\PlaylistSortNode.h"
#include "os\Debug.h"
#include "utl\MakeString.h"
#include "utl\Symbol.h"

int PlaylistTypeCmp::Compare(NavListItemSortCmp const *cmp, NavListNodeType type) const {
    switch (type) {
    case kNodeShortcut:
        return 0;
        break;
    case kNodeHeader:
        return mType - cmp->GetPlaylistTypeCmp()->mType;
        break;
    case kNodeItem:
        cmp->GetPlaylistTypeCmp();
        return -1;
        break;
    default:
        MILO_FAIL("invalid type of node comparison.\n");
        break;
    }
    return 0;
}

NavListShortcutNode *PlaylistSortByType::NewShortcutNode(NavListItemNode *node) const {
    Playlist *p = ((PlaylistSortNode *)node)->GetPlaylist();
    int type;
    Symbol name;
    if (p->IsCustom()) {
        type = 1;
        static Symbol playlist_custom("playlist_custom");
        name = playlist_custom;
    } else if (p->GetIsBattlePlaylist()) {
        type = 4;
        static Symbol playlist_fitness("playlist_fitness");
        name = playlist_fitness;
    } else if (p->GetIsFriendPlaylist()) {
        type = 2;
        static Symbol playlist_era("playlist_era");
        name = playlist_era;
    } else {
        type = 3;
        static Symbol playlist_crew("playlist_crew");
        name = playlist_crew;
    }

    PlaylistTypeCmp *cmp = new PlaylistTypeCmp(type, "");
    return new NavListShortcutNode(cmp, name, true);
}

NavListHeaderNode *PlaylistSortByType::NewHeaderNode(NavListItemNode *node) const {
    Playlist *p = ((PlaylistSortNode *)node)->GetPlaylist();
    int type;
    Symbol name;
    if (p->IsCustom()) {
        type = 1;
        static Symbol playlist_custom("playlist_custom");
        name = playlist_custom;
    } else if (p->GetIsBattlePlaylist()) {
        type = 4;
        static Symbol playlist_fitness("playlist_fitness");
        name = playlist_fitness;
    } else if (p->GetIsFriendPlaylist()) {
        type = 2;
        static Symbol playlist_era("playlist_era");
        name = playlist_era;
    } else {
        type = 3;
        static Symbol playlist_crew("playlist_crew");
        name = playlist_crew;
    }

    PlaylistTypeCmp *cmp = new PlaylistTypeCmp(type, "");
    return new PlaylistHeaderNode(cmp, name, true);
}

NavListHeaderNode *
PlaylistSortByType::NewHeaderNode(NavListItemNode *n1, NavListItemNode *n2) const {
    return NewHeaderNode(n1);
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

    PlaylistTypeCmp *cmp = new PlaylistTypeCmp(type, name);
    return new PlaylistSortNode(cmp, playlist);
}
