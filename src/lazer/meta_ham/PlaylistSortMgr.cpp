#include "meta_ham/PlaylistSortMgr.h"

#include "hamobj/HamGameData.h"
#include "HamProfile.h"
#include "NavListSort.h"
#include "NavListSortMgr.h"
#include "PassiveMessenger.h"
#include "PlaylistSort.h"
#include "ProfileMgr.h"
#include "SaveLoadManager.h"
#include "game/PartyModeMgr.h"
#include "macros.h"
#include "meta/SongPreview.h"
#include "meta_ham/FitnessGoalMgr.h"
#include "meta_ham/Playlist.h"
#include "net_ham/PlaylistJobs.h"
#include "net_ham/RCJobDingo.h"
#include "net_ham/RockCentral.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "os/ContentMgr.h"
#include "os/Debug.h"
#include "os/PlatformMgr.h"
#include "ui/UI.h"
#include "utl/DataPointMgr.h"
#include "utl/Std.h"
#include "utl/Symbol.h"
#include <list>

bool CompareType(const Playlist *p1, const Playlist *p2) {
    int p1type = p1->GetType();
    int p2type = p2->GetType();
    if (p1type == p2type) {
        // Manual strcmp on playlist name (Symbol mName at offset 0x4)
        unsigned char *b2 = (unsigned char *)p2 + 4;
        unsigned char *b1 = (unsigned char *)p1 + 4;
        int diff;
        do {
            unsigned char c1 = *b1;
            unsigned char c2 = *b2;
            diff = c1 - c2;
            if (c1 == 0)
                break;
            b1++;
            b2++;
        } while (diff == 0);
        // Extract sign bit: returns true if diff < 0 (p1 < p2)
        return (unsigned int)diff >> 31;
    }
    return p2type > p1type;
}

// TODO: Remove once HandleCmdGetPlaylistsFromRC is implemented
// (mCustomPlaylists vector ops will trigger Playlist::operator= naturally)
void _force_playlist_assign(Playlist &a, const Playlist &b) { a = b; }

PlaylistSortMgr *ThePlaylistSortMgr;

PlaylistSortMgr::PlaylistSortMgr(SongPreview &sp) : NavListSortMgr(sp) {
    SetName("playlist_sort_mgr", ObjectDir::Main());
    mSorts.push_back(new PlaylistSortByType());
    static Symbol never_use("never_use");
    mCustomPlaylist.SetName(never_use);
    mProfileName = gNullStr;
    mOnlineID = gNullStr;
    mProcessingCommand = false;
}

PlaylistSort::~PlaylistSort() {}
PlaylistSortMgr::~PlaylistSortMgr() {}

void PlaylistSortMgr::Init(SongPreview &sp) {
    MILO_ASSERT(!ThePlaylistSortMgr, 0x1e);
    ThePlaylistSortMgr = new PlaylistSortMgr(sp);
    TheContentMgr.RegisterCallback(ThePlaylistSortMgr, false);
}

bool PlaylistSortMgr::IsProfileChanged() {
    HamProfile *pProfile = TheProfileMgr.GetActiveProfile(true);
    const char *name;
    if (pProfile) {
        name = pProfile->GetName();
    } else {
        name = gNullStr;
    }
    return mProfileName != name;
}

void PlaylistSortMgr::OnSmartGlassListen(int i) {
    if (i != 0) {
        ThePlatformMgr.AddSink(this, "smart_glass_msg");
    } else {
        ThePlatformMgr.RemoveSink(this, "smart_glass_msg");
    }
}

bool PlaylistSortMgr::HasValidProfile() {
    HamProfile *pProfile = TheProfileMgr.GetActiveProfile(true);
    if (pProfile) {
        pProfile->UpdateOnlineID();
        if (pProfile->IsSignedIn()) {
            int padNum = pProfile->GetPadNum();
            if (ThePlatformMgr.IsSignedIntoLive(padNum) && TheRockCentral.IsOnline()) {
                mProfileName = pProfile->GetName();
                QueueCmdChangeProfileOnlineID(pProfile->GetOnlineID()->ToString());
                return true;
            }
        }
    }
    mProfileName = gNullStr;
    QueueCmdChangeProfileOnlineID(gNullStr);
    return false;
}

void PlaylistSortMgr::StartCmdGetPlaylistsFromRC() {
    HamProfile *pProfile = TheProfileMgr.GetActiveProfile(true);
    if (pProfile && pProfile->IsSignedIn()) {
        int padNum = pProfile->GetPadNum();
        if (ThePlatformMgr.IsSignedIntoLive(padNum) && TheRockCentral.IsOnline()) {
            MILO_LOG("MY PORFILE ID: %s\n", mOnlineID.c_str());
            MILO_LOG("ACTUAL PORFILE ID: %s\n", pProfile->GetOnlineID()->ToString());
        }
    }
    mCurrentJob = new GetPlaylistsJob(this, mOnlineID.c_str());
    TheRockCentral.ManageJob(mCurrentJob);
}

void PlaylistSortMgr::FakeAddPlaylistsToRC() {
    HamProfile *pProfile = TheProfileMgr.GetActiveProfile(true);
    if (pProfile) {
        pProfile->UpdateOnlineID();
        if (pProfile->IsSignedIn()) {
            int padNum = pProfile->GetPadNum();
            if (ThePlatformMgr.IsSignedIntoLive(padNum) && TheRockCentral.IsOnline()) {
                TheRockCentral.ManageJob(new AddPlaylistJob(
                    nullptr,
                    pProfile->GetOnlineID()->ToString(),
                    "test_babygotback",
                    "6025"
                ));
                TheRockCentral.ManageJob(new AddPlaylistJob(
                    nullptr,
                    pProfile->GetOnlineID()->ToString(),
                    "test_badromance",
                    "6020,6025"
                ));
                TheRockCentral.ManageJob(new AddPlaylistJob(
                    nullptr,
                    pProfile->GetOnlineID()->ToString(),
                    "test_ymca",
                    "7011,6020,6025"
                ));
                return;
            }
        }
    }
    MILO_LOG(
        "[PlaylistSortMgr::FakeAddPlaylistsToRC] No Valid Profile Available. Skipping This Cheat.\n"
    );
}

int PlaylistSortMgr::ConvertListIndexToPlaylistIndex(int listIndex) {
    int playlistIndex = 0;
    for (int i = 0; i < mHeadersB.size(); i++) {
        if (listIndex >= mHeadersB[i])
            playlistIndex--;
    }

    return playlistIndex + listIndex;
}

void PlaylistSortMgr::BroadcastSyncMsg(Symbol s) {
    Symbol sym = s;
    MILO_LOG("[PlaylistSortMgr::BroadcastSyncMsg] Broadcasting msg (%s).\n", sym);
    Message msg(sym);
    HandleType(msg);
    TheUI->Handle(msg, false);
}

void PlaylistSortMgr::OnEnter() {
    UpdateList();
    FOREACH (it, mSorts) {
        (*it)->BuildTree();
    }
    NavListSort *sort = mSorts[mCurrentSortIdx];
    sort->BuildItemList();
    if (mHighlightSaved) {
        sort->SetHighlightID(mSavedHighlightID);
        mHighlightSaved = false;
    }
    sort->UpdateHighlight();
}

void PlaylistSortMgr::StartCmdGetPlaylistFromRC() {
    CmdGetPlaylistFromRC *cmd = (CmdGetPlaylistFromRC *)mCommandQueue.front();
    mCurrentJob = new GetPlaylistJob(this, mOnlineID.c_str(), cmd->mData.i);
    TheRockCentral.ManageJob(mCurrentJob);
}

void PlaylistSortMgr::StartCmdAddPlaylistToRC() {
    CmdAddPlaylistToRC *cmd = (CmdAddPlaylistToRC *)mCommandQueue.front();
    mCurrentJob = new AddPlaylistJob(this, mOnlineID.c_str(), cmd->mData.playlist);
    TheRockCentral.ManageJob(mCurrentJob);
}

void PlaylistSortMgr::StartCmdDeletePlaylistFromRC() {
    CmdDeletePlaylistFromRC *cmd = (CmdDeletePlaylistFromRC *)mCommandQueue.front();
    mCurrentJob = new DeletePlaylistJob(this, mOnlineID.c_str(), cmd->mData.i);
    TheRockCentral.ManageJob(mCurrentJob);
}

void PlaylistSortMgr::StartCmdEditPlaylist() {
    CmdEditPlaylist *cmd = (CmdEditPlaylist *)mCommandQueue.front();
    mCurrentJob = new EditPlaylistJob(this, mOnlineID.c_str(), cmd->mData.playlist);
    TheRockCentral.ManageJob(mCurrentJob);
}

DataNode PlaylistSortMgr::OnMsg(SmartGlassMsg const &) {
    MILO_LOG("SmartGlass: I should update playlist options/song from RC\n");
    SendDataPoint("smartglass/playlist");
    QueueCmdGetPlaylistsFromRC();
    return 1;
}

void PlaylistSortMgr::QueueCmdAddPlaylistToRC(Playlist *pl) {
    CmdAddPlaylistToRC *cmd = new CmdAddPlaylistToRC(pl);
    mCommandQueue.push_back(cmd);
    if (!mProcessingCommand) {
        ProcessNextCommand();
    }
}

void PlaylistSortMgr::QueueCmdDeletePlaylistFromRC(int i) {
    CmdDeletePlaylistFromRC *cmd = new CmdDeletePlaylistFromRC(i);
    mCommandQueue.push_back(cmd);
    if (!mProcessingCommand) {
        ProcessNextCommand();
    }
}

void PlaylistSortMgr::QueueCmdEditPlaylist(Playlist *pl) {
    CmdEditPlaylist *cmd = new CmdEditPlaylist(pl);
    mCommandQueue.push_back(cmd);
    if (!mProcessingCommand) {
        ProcessNextCommand();
    }
}

void PlaylistSortMgr::ProcessNextCommand() {
    if (mCommandQueue.size() == 0) {
        mProcessingCommand = false;
    } else {
        mProcessingCommand = true;
        QueueableCommand *cmd = mCommandQueue.front();
        switch (cmd->GetType()) {
        case 0:
            HandleCmdChangeProfileOnlineID();
            break;
        case 1:
            StartCmdGetPlaylistsFromRC();
            break;
        case 2:
            HandleCmdResolvePlaylists();
            break;
        case 3:
            StartCmdGetPlaylistFromRC();
            break;
        case 4:
            StartCmdAddPlaylistToRC();
            break;
        case 5:
            StartCmdEditPlaylist();
            break;
        case 6:
            StartCmdDeletePlaylistFromRC();
            break;
        }
    }
}

void PlaylistSortMgr::ResolvePlaylists() {
    HamProfile *activeProfile = TheProfileMgr.GetActiveProfile(true);
    if (activeProfile) {
        const char *profileName = activeProfile->GetName();
        bool flag = mProfileName != profileName;
        if (!flag) {
            int count = (int)mCustomPlaylists.size();
            for (int i = 0; i < count; i++) {
                Playlist *playlist = &activeProfile->GetPlaylist(i);
                CustomPlaylist *cusPlaylist = dynamic_cast<CustomPlaylist *>(playlist);
                cusPlaylist->Copy(&mCustomPlaylists[i]);
                cusPlaylist->SetParentProfile(activeProfile);
            }
        }
        for (int i = 0; i < 5; i++) {
            Playlist *playlist = &activeProfile->GetPlaylist(i);
            int numSongs = playlist->GetNumSongs();
            while (numSongs-- != 0) {
                playlist->RemoveSong();
            }
            playlist->SetOnlineID(-1);
        }
        if (TheSaveLoadMgr) {
            TheSaveLoadMgr->AutoSave();
        }
        BroadcastSyncMsg("playlists_synced");
        if (mCustomPlaylists.size() > 0) {
            SendPassiveMsg("playlist_syned_with_rc");
        }
        return;
    }
    BroadcastSyncMsg("sync_failed");
}

void PlaylistSortMgr::HandleCmdDeletePlaylistFromRC() {
    MILO_LOG("===== HandleCmdDeletePlaylistFromRC\n");
    mCurrentJob = nullptr;
    RELEASE(mCommandQueue.front());
    mCommandQueue.pop_front();
    ProcessNextCommand();
}

void PlaylistSortMgr::HandleCmdAddPlaylistToRC() {
    MILO_LOG("===== HandleCmdAddPlaylistToRC\n");
    ((AddPlaylistJob *)mCurrentJob)->GetPlaylistID(((CmdAddPlaylistToRC *)mCommandQueue.front())->mData.customPlaylist);
    mCurrentJob = nullptr;
    RELEASE(mCommandQueue.front());
    mCommandQueue.pop_front();
    ProcessNextCommand();
}

void PlaylistSortMgr::HandleCmdResolvePlaylists() {
    MILO_LOG("===== HandleCmdResolvePlaylists\n");
    ResolvePlaylists();
    RELEASE(mCommandQueue.front());
    mCommandQueue.pop_front();
    ProcessNextCommand();
}

void PlaylistSortMgr::HandleCmdEditPlaylist() {
    mCurrentJob = nullptr;
    RELEASE(mCommandQueue.front());
    mCommandQueue.pop_front();
    ProcessNextCommand();
}

BEGIN_HANDLERS(PlaylistSortMgr)
    HANDLE_EXPR(has_valid_profile, HasValidProfile())
    HANDLE_EXPR(is_profile_changed, IsProfileChanged())
    HANDLE_ACTION(get_playlists_from_rc, QueueCmdGetPlaylistsFromRC())
    HANDLE_ACTION(update_curr_playlist_with_rc, UpdateCurrPlaylistWithRC())
    HANDLE_ACTION(fake_add_playlists_to_rc, FakeAddPlaylistsToRC())
    HANDLE_ACTION(smart_glass_listen, OnSmartGlassListen(_msg->Int(2)))
    HANDLE_MESSAGE(RCJobCompleteMsg)
    HANDLE_MESSAGE(SmartGlassMsg) HANDLE_SUPERCLASS(NavListSortMgr)
END_HANDLERS
