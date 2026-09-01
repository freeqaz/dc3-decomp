#pragma once
#include "meta/FixedSizeSaveable.h"
#include "meta\FixedSizeSaveableStream.h"
#include "utl\Symbol.h"

enum PlaylistType { // Taken from RB3 and "Setlist" replaced with "Playlist"
    kPlaylistLocal = 0,
    kPlaylistInternal = 1,
    kPlaylistFriend = 2,
    kPlaylistHarmonix = 3,
    kBattleHarmonix = 4,
    kBattleFriend = 5,
    kBattleHarmonixArchived = 6,
    kBattleFriendArchived = 7
};

class Playlist {
public:
    Playlist();
    virtual ~Playlist(); // 0x0
    virtual bool IsCustom() const { return false; } // 0x4
    virtual void SetOnlineID(int) {} // 0x8
    virtual int GetOnlineID() { return -1; } // 0xc
    virtual bool IsDirty() { return false; } // 0x10
    virtual PlaylistType GetType() const {
        if (mIsFriendPlaylist) {
            return (PlaylistType)2;
        } else if (mIsBattlePlaylist) {
            return (PlaylistType)4;
        } else {
            return IsCustom() ? (PlaylistType)1 : (PlaylistType)3;
        }
    } // 0x14

    void SwapSongs(int, int);
    void MoveSong(int, int);
    void ShuffleSongs();
    bool IsValidSong(int) const;
    int GetSong(int) const;
    int GetDuration() const;
    int GetSongDuration(int) const;
    void RemoveSong();
    int GetLastValidSongIndex() const;
    void RemoveSongAtIndex(int);
    void AddSong(int);
    void Clear();
    void InsertSong(int, int);
    int GetNumSongs() const;
    bool IsEmpty() const { return m_vSongs.empty(); }
    bool IsFull() const { return m_vSongs.size() >= 20; }
    Symbol GetName() const { return mName; }
    void SetName(Symbol name) { mName = name; }
    void SetIsBattlePlaylist(bool b) { mIsBattlePlaylist = b; }
    bool GetIsBattlePlaylist() const { return mIsBattlePlaylist; }
    void SetIsFriendPlaylist(bool b) { mIsFriendPlaylist = b; }
    bool GetIsFriendPlaylist() const { return mIsFriendPlaylist; }
    // In-class (implicitly inline), not out-of-line in Playlist.cpp: ham_xbox_r.map
    // flags the target's only copy of ??4Playlist@@QAAAAV0@ABV0@@Z as `f i` and
    // parks it in meta_ham:PlaylistSortMgr.obj -- CustomPlaylist::operator=
    // (PlaylistSortMgr.cpp:255) chains to it, and that is the TU the linker kept
    // the folded COMDAT from. Body unchanged.
    Playlist &operator=(const Playlist &other) {
        mName = other.mName;
        mIsBattlePlaylist = other.mIsBattlePlaylist;
        mIsFriendPlaylist = other.mIsFriendPlaylist;
        m_vSongs = other.m_vSongs;
        return *this;
    }

protected:
    virtual void HandleChange() {}

    Symbol mName; // 0x4
    bool mIsBattlePlaylist; // 0x8
    bool mIsFriendPlaylist; // 0x9
    std::vector<int> m_vSongs; // 0xc
};

class CustomPlaylist : public Playlist, public FixedSizeSaveable {
public:
    CustomPlaylist();
    virtual ~CustomPlaylist();
    virtual bool IsCustom() const { return true; } // 0x4
    virtual void SetOnlineID(int id) { mOnlineID = id; } // 0x8
    virtual int GetOnlineID() { return mOnlineID; } // 0x28
    /** ?IsDirty@CustomPlaylist@@UAA_NXZ is a REAL body at 0x825FA5C8 --
     * `lbz r3, 0x24(r3); blr` -- not the `li r3,0` fold at 0x82AEAE70 that
     * ?IsDirty@Playlist@@ (the base) sits on.  Offset 0x24 is mDirty, which
     * HandleChange() sets and SaveFixed() clears after posting
     * PlaylistChangedJob.  With this hardcoded false,
     * PlaylistSortMgr.cpp's `IsCustom() && IsDirty()` branch was dead. */
    virtual bool IsDirty() { return mDirty; } // 0x24
    // FixedSizeSaveable
    virtual void SaveFixed(FixedSizeSaveableStream &) const;
    virtual void LoadFixed(FixedSizeSaveableStream &, int);

    void SetParentProfile(class HamProfile *);
    void Copy(CustomPlaylist *);
    CustomPlaylist &operator=(const CustomPlaylist &);

    static int SaveSize(int);

    HamProfile *mProfile; // 0x20
    bool mDirty; // 0x24
    int mOnlineID; // 0x28

protected:
    virtual void HandleChange(); // 0x18
};

int GetDynamicPlaylistID(Symbol);
