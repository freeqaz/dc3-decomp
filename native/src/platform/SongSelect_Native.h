#pragma once
// DC3 Native Port - song select additions (docs/native/SONG_SELECT.md).
//
// Three things the image's song select does not have, all native-only:
//
//   * a "Recently Played" section at the top of the list, in every sort;
//   * a fourth sort, by artist (the image carries the name, the locale string
//     and half the script for one, but no SongSortByArtist and no ArtistCmp);
//   * Left/Right = previous/next section, LB/RB = a screenful up/down.
//
// plus the sort being remembered across launches.  DC3_CLASSIC_SONG_SELECT=1
// turns every one of them off: the list, the sorts and the buttons are then
// the image's.
//
// The classes here are what the Xbox build must not see, so they are in a
// native-only file.  The few native members of image classes
// (SongSort::AddRecentlyPlayedSection, SongSortMgr::NativeHandle and friends,
// HamNavList::NativeNavJump) are declared under HX_NATIVE in their own
// headers and defined in SongSelect_Native.cpp.

#include "meta_ham/NavListNode.h"
#include "meta_ham/SongSort.h"
#include "meta_ham/SongSortNode.h"

// DC3_CLASSIC_SONG_SELECT (Dc3EnvFlag, default off), read once.  Also
// declared in NativeSettings.h, with the other switches.
bool Dc3ClassicSongSelect();

namespace NativeSongSelect {

// The most rows the Recently Played section shows.
const int kRecentRows = 6;

// `song` reached gameplay (GamePanel::Enter): the newest entry of the play
// history.  Any mode counts.  The history is kept under the classic switch
// too -- the switch only hides it -- so the list is right when it comes back.
void NotePlayed(Symbol song);

// Whether this visit's list has a Recently Played section at all: not under
// the classic switch, and only where the full song list is shown to pick one
// song from (song_select_mode song_select_quickplay: Perform, Rehearse,
// Dance Battle).  Playlist building keeps the image's list.
bool ShowsRecentlyPlayed();

// A song row of the Recently Played section: the second row of that song in
// the list.  Looking a song up by name never answers with one of these.
bool IsRecentRow(const NavListNode *node);

// The fourth sort, for SongSortMgr's ctor.
NavListSort *NewSongSortByArtist();

// A song row's label under the artist sort: the artist goes in the row's
// header_text slot, the one line of text a row has above its title (the
// image never fills it: NavListItemNode::mHeader is always empty).  True
// when the label was written here; false leaves it to SongSortNode::Text,
// which is every slot in every other sort and under the classic switch.
bool ArtistRowText(const SongSortNode *row, UIListLabel *slot, UILabel *label);

} // namespace NativeSongSelect

// Recently Played is its own shortcut -> header -> rows branch of the sort's
// tree, so numbering, collapsing and the saved highlight path treat it like
// any other section.  Its nodes are ordered by recency, not by the sort, and
// their comparator says so without ever looking inside the other side (which
// belongs to the sort and is of another type).
class RecentlyPlayedCmp : public NavListItemSortCmp {
public:
    virtual int Compare(const NavListItemSortCmp *other, NavListNodeType) const {
        return other == this ? 0 : -1;
    }
};

class RecentlyPlayedHeaderNode : public SongHeaderNode {
public:
    RecentlyPlayedHeaderNode();
    virtual void Text(UIListLabel *, UILabel *) const;

    static Symbol Token();
};

// Artist, then title.  A header's comparator carries the range of sort
// characters ("A" .. "C") its section covers; an item's carries one.
class ArtistCmp : public NavListItemSortCmp {
public:
    ArtistCmp(const char *artist, const char *title);
    ArtistCmp(const char *firstArtist, const char *lastArtist, const char *title);

    virtual int Compare(const NavListItemSortCmp *, NavListNodeType) const;
    virtual const ArtistCmp *GetArtistCmp() const { return this; }

    // 0 for an artist that does not start with a letter ("2NE1", "50 Cent"),
    // 1..26 for A..Z, after AlphaKeySkip ("The Trammps" is under T either way,
    // "The Black Eyed Peas" would be under B).
    static int SortCharRank(const char *artist);
    // "123" for rank 0, else the letter.
    static const char *SortCharLabel(int rank);

    const char *mArtist;
    const char *mTitle;
    int mFirstRank;
    int mLastRank;
};

class SongSortByArtist : public SongSort {
public:
    SongSortByArtist();
    virtual ~SongSortByArtist() {}

    // The image's BuildTree groups by_artist one way collapsed and another
    // way expanded, which its script then has to translate between.  Here a
    // section is the same either way.
    virtual void BuildTree();

    virtual NavListItemNode *NewItemNode(void *) const;
    virtual NavListShortcutNode *NewShortcutNode(NavListItemNode *) const;
    virtual NavListHeaderNode *NewHeaderNode(NavListItemNode *) const;
    virtual NavListHeaderNode *NewHeaderNode(NavListItemNode *, NavListItemNode *) const;

    // Sections smaller than this take the next letter in with them.
    static const int kMinSectionSongs = 4;
};
