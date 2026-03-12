#pragma once
#include "NavListSortMgr.h"
#include "game/PartyModeMgr.h"
#include "meta/SongPreview.h"
#include "meta_ham/FitnessGoalMgr.h"
#include "meta_ham/Playlist.h"
#include "net_ham/PlaylistJobs.h"
#include "net_ham/RCJobDingo.h"
#include "obj/Data.h"
#include "stl/_vector.h"
#include "utl/Str.h"
#include "utl/Symbol.h"
#include <list>

struct CmdGetPlaylistsFromRC : public QueueableCommand {
    virtual int GetType() { return 1; } // fix
    CmdGetPlaylistsFromRC() {}
};

struct CmdGetPlaylistFromRC : public QueueableCommand {
    virtual int GetType() { return 1; } // fix
    CmdGetPlaylistFromRC(int i) { mData.i = i; }

    union {
        int i;
        HamProfile *profile;
        Playlist *playlist;
        CustomPlaylist *customPlaylist;
        const char *onlineID;
    } mData;
};

struct CmdResolvePlaylists : public QueueableCommand {
    virtual int GetType() { return 1; } // fix
    CmdResolvePlaylists() {}
};

struct CmdAddPlaylistToRC : public QueueableCommand {
    virtual int GetType() { return 4; }
    CmdAddPlaylistToRC(Playlist *pl) { mData.playlist = pl; }

    union {
        int i;
        HamProfile *profile;
        Playlist *playlist;
        CustomPlaylist *customPlaylist;
        const char *onlineID;
    } mData;
};

struct CmdDeletePlaylistFromRC : public QueueableCommand {
    virtual int GetType() { return 6; }
    CmdDeletePlaylistFromRC(int i) { mData.i = i; }

    union {
        int i;
        HamProfile *profile;
        Playlist *playlist;
        CustomPlaylist *customPlaylist;
        const char *onlineID;
    } mData;
};

struct CmdEditPlaylist : public QueueableCommand {
    virtual int GetType() { return 5; }
    CmdEditPlaylist(Playlist *pl) { mData.playlist = pl; }

    union {
        int i;
        HamProfile *profile;
        Playlist *playlist;
        CustomPlaylist *customPlaylist;
        const char *onlineID;
    } mData;
};

class PlaylistSortMgr : public NavListSortMgr {
public:
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SelectionIs(Symbol) { return 0; }
    virtual Symbol MoveOn() { return 0; }
    virtual void OnEnter();

    int ConvertListIndexToPlaylistIndex(int);
    Playlist *GetPlaylist(int);
    void OnDeletePlaylistFromRC(Playlist *);
    void UpdateList();

    static void Init(SongPreview &);

    std::vector<Playlist*> mPlaylists;
    CustomPlaylist mCustomPlaylist;
    String mProfileName;
    String mOnlineID;
    std::list<QueueableCommand *> mCommandQueue;
    bool mProcessingCommand;
    RCJob *mCurrentJob;
    std::vector<CustomPlaylist> mCustomPlaylists;

private:
    virtual ~PlaylistSortMgr();

    PlaylistSortMgr(SongPreview &);
    bool IsProfileChanged();
    void OnSmartGlassListen(int);
    void SendPassiveMsg(Symbol);
    void StartCmdGetPlaylistsFromRC();
    void FakeAddPlaylistsToRC();
    void BroadcastSyncMsg(Symbol);
    void ResolvePlaylists();
    void StartCmdGetPlaylistFromRC();
    void StartCmdAddPlaylistToRC();
    void StartCmdDeletePlaylistFromRC();
    void StartCmdEditPlaylist();
    void HandleCmdChangeProfileOnlineID();
    void HandleCmdResolvePlaylists();
    void ProcessNextCommand();
    void QueueCmdChangeProfileOnlineID(String);
    void QueueCmdGetPlaylistsFromRC();
    void QueueCmdResolvePlaylists();
    void QueueCmdGetPlaylistFromRC(int);
    void HandleCmdGetPlaylistFromRC();
    void QueueCmdAddPlaylistToRC(Playlist *);
    void HandleCmdAddPlaylistToRC();
    void QueueCmdDeletePlaylistFromRC(int);
    void HandleCmdDeletePlaylistFromRC();
    void QueueCmdEditPlaylist(Playlist *);
    void HandleCmdEditPlaylist();
    bool HasValidProfile();
    void UpdateCurrPlaylistWithRC();
    void HandleCmdGetPlaylistsFromRC();
    DataNode OnMsg(SmartGlassMsg const &);
    DataNode OnMsg(RCJobCompleteMsg const &);
};

extern PlaylistSortMgr *ThePlaylistSortMgr;
