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
#include "meta_ham/MetaPerformer.h"
#include "macros.h"
#include "math/Utl.h"
#include "meta/SongPreview.h"
#include "meta_ham/FitnessGoalMgr.h"
#include "meta_ham/HamSongMgr.h"
#include "meta_ham/MetaPerformer.h"
#include "meta_ham/Playlist.h"
#include "meta_ham/PlaylistSortByTypeCmp.h"
#include "net_ham/PlaylistJobs.h"
#include "net_ham/RCJobDingo.h"
#include "net_ham/RockCentral.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "os/ContentMgr.h"
#include "os/Debug.h"
#include "os/PlatformMgr.h"
#include "stl/_algo.h"
#include "ui/UI.h"
#include "utl/DataPointMgr.h"
#include "utl/MakeString.h"
#include "utl/Std.h"
#include "utl/Symbol.h"
#include "stl/_map.h"
#include <list>

bool CompareType(const Playlist *p1, const Playlist *p2) {
    if (p1->GetType() == p2->GetType()) {
        return strcmp(p1->GetName().Str(), p2->GetName().Str()) < 0;
    }
    return p2->GetType() > p1->GetType();
}

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

PlaylistSortByType::PlaylistSortByType() {
    static Symbol by_type("by_type");
    mSortName = by_type;
}

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

Playlist *PlaylistSortMgr::GetPlaylist(int idx) {
    if (!IsHeader(idx)) {
        int playlistIdx = ConvertListIndexToPlaylistIndex(idx);
        if (playlistIdx >= 0) {
            return mPlaylists[playlistIdx];
        }
    }
    return nullptr;
}

void PlaylistSortMgr::QueueCmdGetPlaylistsFromRC() {
    CmdGetPlaylistsFromRC *cmd = new CmdGetPlaylistsFromRC();
    mCommandQueue.push_back(cmd);
    if (!mProcessingCommand) {
        ProcessNextCommand();
    }
}

void PlaylistSortMgr::QueueCmdResolvePlaylists() {
    CmdResolvePlaylists *cmd = new CmdResolvePlaylists();
    mCommandQueue.push_back(cmd);
    if (!mProcessingCommand) {
        ProcessNextCommand();
    }
}

void PlaylistSortMgr::QueueCmdGetPlaylistFromRC(int i) {
    CmdGetPlaylistFromRC *cmd = new CmdGetPlaylistFromRC(i);
    mCommandQueue.push_back(cmd);
    if (!mProcessingCommand) {
        ProcessNextCommand();
    }
}

void PlaylistSortMgr::QueueCmdChangeProfileOnlineID(String s) {
    CmdChangeProfileOnlineID *cmd = new CmdChangeProfileOnlineID(s);
    mCommandQueue.push_back(cmd);
    if (!mProcessingCommand) {
        ProcessNextCommand();
    }
}

void PlaylistSortMgr::HandleCmdGetPlaylistFromRC() {
    MILO_LOG("===== HandleCmdGetPlaylistFromRC\n");
    GetPlaylistJob *job = (GetPlaylistJob *)mCurrentJob;
    CmdGetPlaylistFromRC *cmd = (CmdGetPlaylistFromRC *)mCommandQueue.front();
    for (unsigned int i = 0; i < mCustomPlaylists.size(); i++) {
        if (mCustomPlaylists[i].GetOnlineID() == cmd->mData.i) {
            job->GetPlaylist(&mCustomPlaylists[i]);
            break;
        }
    }
    mCurrentJob = nullptr;
    RELEASE(mCommandQueue.front());
    mCommandQueue.pop_front();
    ProcessNextCommand();
}

void PlaylistSortMgr::HandleCmdGetPlaylistsFromRC() {
    MILO_LOG("===== HandleCmdGetPlaylistsFromRC\n");
    GetPlaylistsJob *job = (GetPlaylistsJob *)mCurrentJob;
    mCustomPlaylists.clear();
    job->GetPlaylists(&mCustomPlaylists);
    mCurrentJob = nullptr;
    MILO_LOG(">>>>>>>>>> there are %i of playlists on RC.\n", mCustomPlaylists.size());
    for (unsigned int i = 0; i < mCustomPlaylists.size(); i++) {
        QueueCmdGetPlaylistFromRC(mCustomPlaylists[i].GetOnlineID());
    }
    QueueCmdResolvePlaylists();
    RELEASE(mCommandQueue.front());
    mCommandQueue.pop_front();
    ProcessNextCommand();
}

void PlaylistSortMgr::HandleCmdChangeProfileOnlineID() {
    MILO_LOG("===== HandleCmdChangeProfileOnlineID\n");
    CmdChangeProfileOnlineID *cmd = (CmdChangeProfileOnlineID *)mCommandQueue.front();
    mOnlineID = cmd->mOnlineID;
    RELEASE(mCommandQueue.front());
    mCommandQueue.pop_front();
    ProcessNextCommand();
}

void PlaylistSortMgr::OnDeletePlaylistFromRC(Playlist *playlist) {
    if (playlist->GetOnlineID() != -1) {
        QueueCmdDeletePlaylistFromRC(playlist->GetOnlineID());
        playlist->SetOnlineID(-1);
    }
}

CustomPlaylist &CustomPlaylist::operator=(const CustomPlaylist &other) {
    Playlist::operator=(other);
    FixedSizeSaveable::operator=(other);
    mProfile = other.mProfile;
    unk24 = other.unk24;
    mOnlineID = other.mOnlineID;
    return *this;
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

void PlaylistSortMgr::SendPassiveMsg(Symbol sym) {
    static Symbol p1("p1");
    static Symbol p2("p2");
    static Symbol none("none");

    Symbol playerSym = none;
    for (int i = 0; i < 2; i++) {
        HamPlayerData *playerData = TheGameData->Player(i);
        MILO_ASSERT(playerData, 0xf6);
        if (playerData->GetPlayerName() == mProfileName) {
            playerSym = (i == 0) ? p1 : p2;
            break;
        }
    }
    ThePassiveMessenger->TriggerGenericMsg(
        sym, playerSym, kPassiveMessageGeneral, Symbol(gNullStr), -1
    );
}

DataNode PlaylistSortMgr::OnMsg(const RCJobCompleteMsg &msg) {
    if (!msg.Success()) {
        MILO_LOG("[PlaylistSortMgr::OnMsg] Playlist net API failed.\n");
        mCurrentJob = nullptr;
        BroadcastSyncMsg(Symbol("sync_failed"));
        mProcessingCommand = false;
        while (!mCommandQueue.empty()) {
            RELEASE(mCommandQueue.front());
            mCommandQueue.pop_front();
        }
    } else {
        bool updated = false;
        if (msg.Job() == mCurrentJob) {
            switch (mCommandQueue.front()->GetType()) {
            case 1:
                HandleCmdGetPlaylistsFromRC();
                break;
            case 3:
                HandleCmdGetPlaylistFromRC();
                break;
            case 4:
                HandleCmdAddPlaylistToRC();
                updated = true;
                break;
            case 5:
                HandleCmdEditPlaylist();
                updated = true;
                break;
            case 6:
                HandleCmdDeletePlaylistFromRC();
                updated = true;
                break;
            }
        }
        if (updated) {
            DataNode playlist("playlist");
            DataNode updatedNode("updated");
            ThePlatformMgr.SmartGlassSend(0, DataArrayPtr(updatedNode, playlist));
        }
    }
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

void PlaylistSortMgr::UpdateList() {
    mPlaylists.clear();
    HamProfile *profile = TheProfileMgr.GetActiveProfile(true);
    if (profile) {
        for (int i = 0; i < 5; i++) {
            Playlist *playlist = &profile->GetPlaylist(i);
            if (playlist->GetNumSongs() == 0) {
                static Symbol playlist_create("playlist_create");
                playlist->SetName(playlist_create);
                ThePlaylistSortMgr->mPlaylists.push_back(playlist);
                break;
            }
        }
        for (int i = 0; i < 5; i++) {
            Playlist *playlist = &profile->GetPlaylist(i);
            if (playlist->GetNumSongs() != 0) {
                ThePlaylistSortMgr->mPlaylists.push_back(playlist);
            }
        }
    }
    for (int i = 0; i < TheHamSongMgr.GetNumPlaylists(); i++) {
        ThePlaylistSortMgr->mPlaylists.push_back(TheHamSongMgr.GetPlaylist(i));
    }
    std::sort(mPlaylists.begin(), mPlaylists.end(), CompareType);
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
    if (!activeProfile || mProfileName != activeProfile->GetName()) {
        BroadcastSyncMsg("sync_failed");
    } else {
        int something = Max(5 - (int)mCustomPlaylists.size(), 0);
        int size = (int)mCustomPlaylists.size();
        for (int i = 0; i < size; i++) {
            CustomPlaylist &cusPlaylist =
                dynamic_cast<CustomPlaylist &>(activeProfile->GetPlaylist(i));
            cusPlaylist.Copy(&mCustomPlaylists[i]);
            cusPlaylist.SetParentProfile(activeProfile);
        }
        for (int i = 5 - something; i < 5; i++) {
            Playlist *playlist = &activeProfile->GetPlaylist(i);
            int numSongs = playlist->GetNumSongs();
            while (numSongs != 0) {
                numSongs--;
                playlist->RemoveSong();
            }
            playlist->SetOnlineID(-1);
        }

        if (TheSaveLoadMgr)
            TheSaveLoadMgr->AutoSave();

        BroadcastSyncMsg("playlists_synced");
        if (mCustomPlaylists.size() > 0) {
            SendPassiveMsg("playlist_syned_with_rc");
        }
    }
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

void PlaylistSortMgr::UpdateCurrPlaylistWithRC() {
    MetaPerformer *performer = MetaPerformer::Current();
    MILO_ASSERT(performer, 0x296);

    Playlist *playlist = performer->GetPlaylist();
    if (playlist && playlist->IsCustom() && playlist->IsDirty()) {
        if (playlist->GetNumSongs() != 0) {
            static Symbol playlist_create("playlist_create");
            if (playlist->GetName() == playlist_create) {
                std::map<Symbol, bool> usedNames;
                for (int i = 1; i <= 5; i++) {
                    Symbol name(MakeString("playlist_custom_%02i", i));
                    usedNames[name] = false;
                }
                HamProfile *profile = TheProfileMgr.GetActiveProfile(true);
                for (int i = 0; i < 5; i++) {
                    Playlist &pl = profile->GetPlaylist(i);
                    Symbol name = pl.GetName();
                    std::map<Symbol, bool>::iterator it = usedNames.find(name);
                    if (it != usedNames.end()) {
                        it->second = true;
                    }
                }
                for (std::map<Symbol, bool>::iterator it = usedNames.begin();
                     it != usedNames.end();
                     ++it) {
                    if (!it->second) {
                        playlist->SetName(it->first);
                        break;
                    }
                }
            }
            int onlineID = playlist->GetOnlineID();
            if (onlineID != -1) {
                QueueCmdEditPlaylist(playlist);
            } else {
                QueueCmdAddPlaylistToRC(playlist);
            }
        } else {
            int onlineID = playlist->GetOnlineID();
            if (onlineID != -1) {
                QueueCmdDeletePlaylistFromRC(playlist->GetOnlineID());
            }
        }
    }
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
