#include "game/PartyModeMgr.h"
#include "flow/PropertyEventProvider.h"
#include "gesture/BaseSkeleton.h"
#include "hamobj/Difficulty.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamPlayerData.h"
#include "meta_ham/HamProfile.h"
#include "meta_ham/HamSongMetadata.h"
#include "meta_ham/HamSongMgr.h"
#include "meta_ham/MetaPerformer.h"
#include "meta_ham/ProfileMgr.h"
#include "meta_ham/SongRecord.h"
#include "meta_ham/Utl.h"
#include "net_ham/PartyModeJobs.h"
#include "net_ham/RCJobDingo.h"
#include "net_ham/RockCentral.h"
#include "obj/Data.h"
#include "obj/DataUtl.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os/ContentMgr.h"
#include "os/DateTime.h"
#include "os/Debug.h"
#include "os/PlatformMgr.h"
#include "os/System.h"
#include "ui/UI.h"
#include "utl/DataPointMgr.h"
#include "utl/JobMgr.h"
#include "utl/Locale.h"
#include "utl/Std.h"
#include "utl/Symbol.h"
#include <cstdio>
#include <cstdlib>

PartyModeMgr *ThePartyModeMgr;
int gRematchCount;

namespace {
    int GetEnumFromModeName(Symbol mode) {
        static Symbol bustamove("bustamove");
        static Symbol perform("perform");
        static Symbol dance_battle("dance_battle");
        static Symbol rhythm_battle("rhythm_battle");
        static Symbol strike_a_pose("strike_a_pose");
        if (mode == bustamove) {
            return 3;
        } else if (mode == perform) {
            return 0;
        } else if (mode == dance_battle) {
            return 1;
        } else if (mode == rhythm_battle) {
            return 2;
        } else if (mode == strike_a_pose) {
            return 4;
        } else {
            return 0x20;
        }
    }

    Symbol GetModeNameFromEnum(int enumType) {
        static Symbol bustamove("bustamove");
        static Symbol perform("perform");
        static Symbol dance_battle("dance_battle");
        static Symbol rhythm_battle("rhythm_battle");
        static Symbol strike_a_pose("strike_a_pose");
        switch (enumType) {
        case 0:
            return perform;
        case 1:
            return dance_battle;
        case 2:
            return rhythm_battle;
        case 3:
            return bustamove;
        case 4:
            return strike_a_pose;
        default:
            MILO_ASSERT(0, 0x49);
            return gNullStr;
        }
    }
}

#pragma region PartyModeARObject

const char *PartyModeARObject::GetTexPath() {
    const char *texPath = gNullStr;
    static Symbol image_path("image_path");
    DataArray *pathArr = unk8->FindArray(image_path);
    if (pathArr) {
        texPath = pathArr->Str(1);
    }
    return texPath;
}

#pragma endregion
#pragma region PartyModePlayer

PartyModePlayer::PartyModePlayer(PartyModeARObject *obj) : mARObject(obj), mScore(0) {
    mTitleArray = new DataArray(3);
}

PartyModePlayer::~PartyModePlayer() {
    PartyModeARObject *obj = mARObject;
    if (obj) {
        delete obj;
    }
    mARObject = 0;
    mTitleArray->Release();
}

void PartyModePlayer::PushTitle(Symbol s) {
    mTitleHistory.push_back(s);
    if (mTitleHistory.size() > 3) {
        mTitleHistory.pop_front();
    }
    int idx = 0;
    for (auto it = mTitleHistory.begin(); it != mTitleHistory.end(); ++it, ++idx) {
        mTitleArray->Node(idx) = *it;
    }
}

#pragma endregion
#pragma region PartyModeMgr

PartyModeMgr::PartyModeMgr() : mFrameSmoothers() {
    unk40 = false;
    mEventBucketSequences = 0;
    static Symbol party_mode("party_mode");
    mPartyModeCfg = SystemConfig(party_mode);
    static Symbol ar_objects("ar_objects");
    mARObjects = mPartyModeCfg->FindArray(ar_objects);
    static Symbol good_titles("good_titles");
    mGoodTitles = mPartyModeCfg->FindArray(good_titles);
    static Symbol bad_titles("bad_titles");
    mBadTitles = mPartyModeCfg->FindArray(bad_titles);
    static Symbol event_scoring("event_scoring");
    mEventScoring = mPartyModeCfg->FindArray(event_scoring);
    mUsePlaytestData = false;
    mPartyModePlaytestEvents = nullptr;
    static Symbol party_mode_playtest_data("party_mode_playtest_data");
    mPartyModePlaytestData = mPartyModeCfg->FindArray(party_mode_playtest_data);
    if (mPartyModePlaytestData) {
        static Symbol use_playtest_data("use_playtest_data");
        DataArray *useData = mPartyModePlaytestData->FindArray(use_playtest_data);
        if (useData && useData->Int(1) != 0) {
            mUsePlaytestData = true;
        }
    }
    if (mUsePlaytestData) {
        static Symbol party_mode_playtest_events("party_mode_playtest_events");
        mPartyModePlaytestEvents =
            mPartyModePlaytestData->FindArray(party_mode_playtest_events);
    }
    std::vector<Symbol> vec;
    int numGoodTitles = mGoodTitles->Size();
    vec.resize(numGoodTitles);
    for (int i = 1; i < numGoodTitles; i++) {
        vec[i - 1] = mGoodTitles->Sym(i);
    }
    mGoodTitlePicker.AddItems(vec);
    mGoodTitlePicker.SetMode(0);
    int numBadTitles = mBadTitles->Size();
    vec.resize(numBadTitles);
    for (int i = 1; i < numBadTitles; i++) {
        vec[i - 1] = mBadTitles->Sym(i);
    }
    mBadTitlePicker.AddItems(vec);
    mBadTitlePicker.SetMode(0);
    int numARObjects = mARObjects->Size() - 1;
    for (int i = 1; i <= numARObjects; i++) {
        mARObjectIndices.push_back(i);
    }
    for (int i = 0; i < numARObjects; i++) {
        int randIdx = rand() % numARObjects;
        int old = mARObjectIndices[i];
        mARObjectIndices[i] = mARObjectIndices[randIdx];
        mARObjectIndices[randIdx] = old;
    }
    mCurrEvent = nullptr;
    InitCharacters();
    for (int i = 0; i < 6; i++) {
        mFrameSmoothers[i].SetSmoothParameters(10, 1);
        mFrameSmoothers[i].ForceValue(Vector2(0.5, 0.5));
    }
    mDifficulty = DefaultDifficulty();
    mPlaylist = nullptr;
    mIsPlaylistShuffled = false;
    mIncludedModesMask = -1;
    mUseFullLengthSongs = false;
    static DataNode &n = DataVariable("force_song_shortening_off");
    if (n.Int()) {
        mUseFullLengthSongs = true;
    }
    mPerSongDifficulty = false;
    mCustomParty = false;
    mUsingPerSongOptions = false;
    mSetPartyOptionsJob = nullptr;
    mGetPartyOptionsJob = nullptr;
    mGetPartySongQueueJob = nullptr;
    mAddSongToPartySongQueueJob = nullptr;
    mDeleteSongFromPartySongQueueJob = nullptr;
    mQueueStateValid = false;
    mPlaytestEventSequences = nullptr;
}

PartyModeMgr::~PartyModeMgr() { ResetPlayers(); }

BEGIN_HANDLERS(PartyModeMgr)
    HANDLE_ACTION(add_player_to_team, AddPlayerToTeam(_msg->Int(2)))
    HANDLE_ACTION(finalize_team, FinalizeTeam(_msg->Int(2)))
    HANDLE_ACTION(clear_team, ClearTeam(_msg->Int(2)))
    HANDLE_ACTION(finalize_party, FinalizeParty())
    HANDLE_ACTION(
        store_player_frame_pos,
        StorePlayerFramePos(_msg->Int(2), _msg->Float(3), _msg->Float(4))
    )
    HANDLE_ACTION(
        store_player_frame_scale, StorePlayerFrameScale(_msg->Int(2), _msg->Float(3))
    )
    HANDLE_EXPR(get_tex_path, GetPlayerARTexPath(_msg->Int(2)))
    HANDLE_EXPR(num_enrolled, (int)mPlayers.size())
    HANDLE_EXPR(num_enrolled_team_1, (int)mTeam1Players.size())
    HANDLE_EXPR(num_enrolled_team_2, (int)mTeam2Players.size())
    HANDLE_EXPR(get_curr_event_name, GetCurrEventName())
    HANDLE_EXPR(get_curr_event_display_name, GetCurrEventDisplayName())
    HANDLE_EXPR(get_curr_event_microgame_name, GetCurrEventMicrogameName())
    HANDLE_EXPR(get_curr_event_song_name, GetCurrEventSongName())
    HANDLE_EXPR(get_curr_event_song_shortname, GetCurrEventSongShortName())
    HANDLE_EXPR(get_curr_event_player_flags, GetCurrEventPlayerFlags())
    HANDLE_EXPR(get_curr_event_num_players, GetCurrEventNumPlayers())
    HANDLE_EXPR(get_curr_event_artist_name, GetCurrEventSongArtistName())
    HANDLE_EXPR(get_curr_event_players, GetCurrEventPlayers())
    HANDLE_ACTION(update_curr_event, SetCurrEvent())
    HANDLE_ACTION(update_rounds_played, UpdateRoundsPlayed())
    HANDLE_EXPR(get_max_participants, 8)
    HANDLE_ACTION(set_random_characters, SetRandomCharacters())
    HANDLE_ACTION(setup_character_data, SetupCharacterData())
    HANDLE_ACTION(reset_party, ResetParty())
    HANDLE_ACTION(crew_showdown_rematch, CrewShowdownRematch())
    HANDLE_EXPR(get_left_player_index, GetLeftPlayerIndex())
    HANDLE_EXPR(get_right_player_index, GetRightPlayerIndex())
    HANDLE_ACTION(inc_left_player_score, IncLeftPlayerScore(_msg->Int(2)))
    HANDLE_ACTION(inc_right_player_score, IncRightPlayerScore(_msg->Int(2)))
    HANDLE_EXPR(get_player_photo_index, GetPlayerPhotoIndex(_msg->Int(2)))
    HANDLE_ACTION(push_left_player_title, PushLeftPlayerTitle(_msg->Sym(2)))
    HANDLE_ACTION(push_right_player_title, PushRightPlayerTitle(_msg->Sym(2)))
    HANDLE_EXPR(is_showdown, mIsShowdown)
    HANDLE_EXPR(is_team_signed_in, IsTeamSignedIn(_msg->Int(2)))
    HANDLE_ACTION(set_left_team_score, SetLeftTeamScore(_msg->Float(2)))
    HANDLE_ACTION(set_right_team_score, SetRightTeamScore(_msg->Float(2)))
    HANDLE_ACTION(inc_left_team_score, IncLeftTeamScore(_msg->Float(2)))
    HANDLE_ACTION(inc_right_team_score, IncRightTeamScore(_msg->Float(2)))
    HANDLE_EXPR(get_left_team_score, mLeftTeamScore)
    HANDLE_EXPR(get_right_team_score, mRightTeamScore)
    HANDLE_EXPR(get_left_team_prev_score, mLeftTeamPrevScore)
    HANDLE_EXPR(get_right_team_prev_score, mRightTeamPrevScore)
    HANDLE_ACTION(start_new_round, StartNewRound())
    HANDLE_ACTION(
        smooth_frame_motion,
        SmoothFrameMotion(_msg->Int(2), _msg->Float(3), _msg->Float(4))
    )
    HANDLE_ACTION(
        force_frame_smoother_pos,
        ForceFrameSmootherPos(_msg->Int(2), _msg->Float(3), _msg->Float(4))
    )
    HANDLE(get_smoothed_frame_pos, OnGetSmoothedFramePos)
    HANDLE_ACTION(set_difficulty, mDifficulty = (Difficulty)_msg->Int(2))
    HANDLE_EXPR(get_difficulty, mDifficulty)
    HANDLE_ACTION(set_left_team_crew, mLeftTeamCrew = _msg->Sym(2))
    HANDLE_ACTION(set_right_team_crew, mRightTeamCrew = _msg->Sym(2))
    HANDLE_EXPR(get_left_team_crew, mLeftTeamCrew)
    HANDLE_EXPR(get_right_team_crew, mRightTeamCrew)
    HANDLE_EXPR(get_points_for_win, GetPointsForWin())
    HANDLE_EXPR(get_points_for_loss, GetPointsForLoss())
    HANDLE_ACTION(update_scores, UpdateScores())
    HANDLE_ACTION(use_selected_playlist, UseSelectedPlaylist(_msg->Int(2)))
    HANDLE_EXPR(is_using_playlist, IsUsingPlaylist())
    HANDLE_ACTION(shuffle_playlist, ShufflePlaylist(_msg->Int(2)))
    HANDLE_EXPR(is_playlist_shuffled, mIsPlaylistShuffled)
    HANDLE_ACTION(use_full_length_songs, mUseFullLengthSongs = _msg->Int(2))
    HANDLE_EXPR(is_using_full_length_songs, mUseFullLengthSongs)
    HANDLE_ACTION(toggle_included_mode, ToggleIncludedMode(_msg->Sym(2)))
    HANDLE_ACTION(
        toggle_included_mode_on, ToggleIncludedModeOn(_msg->Sym(2), _msg->Int(3))
    )
    HANDLE_ACTION(set_modes, SetModes())
    HANDLE_EXPR(is_mode_included, IsModeIncluded(_msg->Sym(2)))
    HANDLE_ACTION(setup_infinite_party_mode, SetupInfinitePartyMode())
    HANDLE(set_song_and_defaults, OnSetSongAndDefaults)
    HANDLE_EXPR(get_playlist_string, GetPlaylistString())
    HANDLE_ACTION(set_per_song_difficulty, mPerSongDifficulty = _msg->Int(2))
    HANDLE_EXPR(use_per_song_difficulty, mPerSongDifficulty)
    HANDLE_ACTION(set_custom_party, mCustomParty = _msg->Int(2))
    HANDLE_EXPR(is_custom_party, mCustomParty)
    HANDLE_EXPR(get_left_crew_color_1, GetLeftCrewColor1AsArray())
    HANDLE_EXPR(get_left_crew_color_2, GetLeftCrewColor2AsArray())
    HANDLE_EXPR(get_right_crew_color_1, GetRightCrewColor1AsArray())
    HANDLE_EXPR(get_right_crew_color_2, GetRightCrewColor2AsArray())
    HANDLE_EXPR(get_crew_color, GetCrewColor(_msg->Int(2), _msg->Int(3)))
    HANDLE_EXPR(
        get_left_crew_char_outfit, GetLeftCrewCharOutfit(_msg->Int(2), _msg->Int(3))
    )
    HANDLE_EXPR(
        get_right_crew_char_outfit, GetRightCrewCharOutfit(_msg->Int(2), _msg->Int(3))
    )
    HANDLE_EXPR(get_left_team_prev_pct_of_max_points, mLeftTeamPrevScorePercent)
    HANDLE_EXPR(get_right_team_prev_pct_of_max_points, mRightTeamPrevScorePercent)
    HANDLE_EXPR(get_left_team_curr_pct_of_max_points, mLeftTeamPrevScorePercent = mLeftTeamScore / mMaxPointsPerEvent)
    HANDLE_EXPR(get_right_team_curr_pct_of_max_points, mRightTeamPrevScorePercent = mRightTeamScore / mMaxPointsPerEvent)
    HANDLE_EXPR(get_winning_side, mWinningSide)
    HANDLE_EXPR(get_just_won_side, mJustWonSide)
    HANDLE_EXPR(left_team_max_wins, LeftTeamMaxWins())
    HANDLE_EXPR(right_team_max_wins, RightTeamMaxWins())
    HANDLE_ACTION(send_party_options_to_rc, SendPartyOptionsToRC())
    HANDLE_ACTION(get_party_options_from_rc, GetPartyOptionsFromRC())
    HANDLE_ACTION(get_party_song_queue_from_rc, GetPartySongQueueFromRC())
    HANDLE_EXPR(get_next_song, GetNextSongName())
    HANDLE_ACTION(change_to_another_game_mode, ChangeToAnotherGameMode())
    HANDLE_EXPR(get_rounds_played, mRoundsPlayed)
    HANDLE_EXPR(get_rounds_total, mRoundsTotal)
    HANDLE_ACTION(start_party_stats, GetDateAndTime(mPartyStatsStartTime))
    HANDLE_ACTION(end_party_stats, EndPartyStats())
    HANDLE_ACTION(smart_glass_listen, OnSmartGlassListen(_msg->Int(2)))
    HANDLE_ACTION(prune_history, PruneHistory())
    HANDLE_EXPR(stable_song, OnStableSong())
    HANDLE_EXPR(stable_mode, OnStableMode())
    HANDLE_MESSAGE(RCJobCompleteMsg)
    HANDLE_MESSAGE(SmartGlassMsg)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(PartyModeMgr)
    SYNC_PROP(is_playlist_shuffled, mIsPlaylistShuffled)
    SYNC_PROP(is_using_per_song_options, mUsingPerSongOptions)
    SYNC_PROP(curr_synced_song_id, mCurrSyncedSongID)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

void PartyModeMgr::ContentMounted(const char *contentName, const char *) {
    MILO_ASSERT(contentName, 0x154);
    if (!TheContentMgr.RefreshInProgress() && mCurrEvent) {
        if (TheHamSongMgr.IsContentUsedForSong(contentName, mCurrEvent->mSongID)) {
            static Symbol song_data_mounted("song_data_mounted");
            static Message msg(song_data_mounted, gNullStr);
            msg[0] = GetCurrEventSongShortName();
            TheUI->Export(msg, false);
        }
    }
}

void PartyModeMgr::Init() {
    MILO_ASSERT(ThePartyModeMgr == nullptr, 0x142);
    ThePartyModeMgr = new PartyModeMgr();
    if (ObjectDir::Main()) {
        ThePartyModeMgr->SetName("partymode_mgr", ObjectDir::Main());
    }
    TheContentMgr.RegisterCallback(ThePartyModeMgr, false);
}

int PartyModeMgr::GetLeftPlayerIndex() const {
    int idx = -1;
    if (mLeftPlayer) {
        idx = mLeftPlayer->Index();
    }
    return idx;
}

int PartyModeMgr::GetRightPlayerIndex() const {
    int idx = -1;
    if (mRightPlayer) {
        idx = mRightPlayer->Index();
    }
    return idx;
}

void PartyModeMgr::IncLeftPlayerScore(int score) {
    if (mLeftPlayer) {
        mLeftPlayer->IncScore(score);
    }
}

void PartyModeMgr::IncRightPlayerScore(int score) {
    if (mRightPlayer) {
        mRightPlayer->IncScore(score);
    }
}

void PartyModeMgr::PushLeftPlayerTitle(Symbol title) {
    if (mLeftPlayer) {
        mLeftPlayer->PushTitle(title);
    }
}

void PartyModeMgr::PushRightPlayerTitle(Symbol title) {
    if (mRightPlayer) {
        mRightPlayer->PushTitle(title);
    }
}

void PartyModeMgr::SetLeftTeamScore(float score) {
    mLeftTeamPrevScore = mLeftTeamScore;
    mLeftTeamScore = score;
}

void PartyModeMgr::SetRightTeamScore(float score) {
    mRightTeamPrevScore = mRightTeamScore;
    mRightTeamScore = score;
}

void PartyModeMgr::IncLeftTeamScore(float score) {
    mLeftTeamPrevScore = mLeftTeamScore;
    mLeftTeamScore += score;
}

void PartyModeMgr::IncRightTeamScore(float score) {
    mRightTeamPrevScore = mRightTeamScore;
    mRightTeamScore += score;
}

void PartyModeMgr::StartNewRound() {
    mLeftTeamPrevScore = mLeftTeamScore;
    mRightTeamPrevScore = mRightTeamScore;
    mLeftTeamPrevScorePercent = 0;
    mRightTeamPrevScorePercent = 0;
    mLeftTeamScore = 0;
    mRightTeamScore = 0;
}

bool PartyModeMgr::LeftTeamMaxWins() const {
    return 0.001f >= mMaxPointsPerEvent - mLeftTeamScore && mWinningSide == 0;
}

bool PartyModeMgr::RightTeamMaxWins() const {
    return 0.001f >= mMaxPointsPerEvent - mRightTeamScore && mWinningSide == 1;
}

bool PartyModeMgr::IsModeIncluded(Symbol mode) {
    return (1 << GetEnumFromModeName(mode)) & mIncludedModesMask;
}

Symbol PartyModeMgr::GetNextSongName() {
    if (mCurrSyncedSongID == 0) {
        return gNullStr;
    } else {
        return TheHamSongMgr.GetShortNameFromSongID(mCurrSyncedSongID, false);
    }
}

HamProfile *PartyModeMgr::GetValidProfile() {
    HamProfile *profile = TheProfileMgr.GetActiveProfile(true);
    if (profile) {
        profile->UpdateOnlineID();
        if (profile->IsSignedIn() && ThePlatformMgr.IsSignedIntoLive(profile->GetPadNum())
            && TheRockCentral.IsOnline()) {
            return profile;
        }
    }
    return 0;
}

void PartyModeMgr::SetLeftTeamStarBonus() {
    mLeftTeamStarBonus = 0;
    if (mIsShowdown) {
        HamPlayerData *playerData0 = TheGameData->Player(0);
        Hmx::Object *provider0 = playerData0->Provider();
        HamPlayerData *playerData1 = TheGameData->Player(1);
        Hmx::Object *provider1 = playerData1->Provider();
        float score0 = provider0->Property("score")->Float();
        float score1 = provider1->Property("score")->Float();
        if (score1 > score0) {
            int numStars = TheHamProvider->Property("stars_earned", false)->Int();
            if (numStars == 5) {
                mLeftTeamStarBonus = mEventScoring->FindFloat("five_star_bonus");
            } else if (numStars == 6) {
                mLeftTeamStarBonus = mEventScoring->FindFloat("six_star_bonus");
            }
        }
    }
}

void PartyModeMgr::SetRightTeamStarBonus() {
    mRightTeamStarBonus = 0;
    if (mIsShowdown) {
        HamPlayerData *playerData0 = TheGameData->Player(0);
        Hmx::Object *provider0 = playerData0->Provider();
        HamPlayerData *playerData1 = TheGameData->Player(1);
        Hmx::Object *provider1 = playerData1->Provider();
        float score0 = provider0->Property("score")->Float();
        float score1 = provider1->Property("score")->Float();
        if (score0 > score1) {
            int numStars = TheHamProvider->Property("stars_earned", false)->Int();
            if (numStars == 5) {
                mRightTeamStarBonus = mEventScoring->FindFloat("five_star_bonus");
            } else if (numStars == 6) {
                mRightTeamStarBonus = mEventScoring->FindFloat("six_star_bonus");
            }
        }
    }
}

float PartyModeMgr::GetPointsForWin() {
    static Symbol win("win");
    DataArray *winPoints = mEventScoring->FindArray(win);
    MILO_ASSERT(winPoints, 0x427);
    DataArray *winData = winPoints->FindArray(mCurrEvent->mModeName, false);
    if (winData) {
        return winData->Float(1);
    } else {
        MILO_NOTIFY(
            "Party mode event %s does not have win scoring data",
            mCurrEvent->mModeName.Str()
        );
        return 0;
    }
}

float PartyModeMgr::GetPointsForLoss() {
    static Symbol lose("lose");
    DataArray *losePoints = mEventScoring->FindArray(lose);
    MILO_ASSERT(losePoints, 0x43D);
    DataArray *loseData = losePoints->FindArray(mCurrEvent->mModeName, false);
    if (loseData) {
        return loseData->Float(1);
    } else {
        MILO_NOTIFY(
            "Party mode event %s does not have lose scoring data",
            mCurrEvent->mModeName.Str()
        );
        return 0;
    }
}

void PartyModeMgr::UpdateRoundsPlayed() {
    mRoundsPlayed++;
    if (mRoundsUntilShowdown == 0) {
        mRoundsUntilShowdown = mRoundsTotal;
    } else {
        mRoundsUntilShowdown--;
    }
    MILO_LOG(
        "----- updating rounds played - rounds played: %d; rounds until showdown: %d\n",
        mRoundsPlayed,
        mRoundsUntilShowdown
    );
}

Symbol PartyModeMgr::GetCurrEventName() {
    MILO_ASSERT(mCurrEvent, 0x4BC);
    Symbol ret(gNullStr);
    ret = mCurrEvent->mModeName;
    return ret;
}

Symbol PartyModeMgr::GetCurrEventMicrogameName() {
    MILO_ASSERT(mCurrEvent, 0x4CB);
    Symbol ret(gNullStr);
    ret = mCurrEvent->mSubModeName;
    return ret;
}

Symbol PartyModeMgr::GetCurrEventSongName() {
    MILO_ASSERT(mCurrEvent, 0x4D4);
    Symbol ret(gNullStr);
    const HamSongMetadata *data = TheHamSongMgr.Data(mCurrEvent->mSongID);
    MILO_ASSERT(data, 0x4DB);
    ret = Symbol(data->Title());
    return ret;
}

Symbol PartyModeMgr::GetCurrEventSongShortName() {
    MILO_ASSERT(mCurrEvent, 0x4E3);
    Symbol ret(gNullStr);
    const HamSongMetadata *data = TheHamSongMgr.Data(mCurrEvent->mSongID);
    MILO_ASSERT(data, 0x4EA);
    SongRecord record(data);
    ret = record.ShortName();
    return ret;
}

Symbol PartyModeMgr::GetCurrEventSongArtistName() {
    MILO_ASSERT(mCurrEvent, 0x4F3);
    Symbol ret(gNullStr);
    static Symbol partymode_intermission("partymode_intermission");
    if (mCurrEvent->mModeName == partymode_intermission) {
        return ret;
    } else {
        const HamSongMetadata *data = TheHamSongMgr.Data(mCurrEvent->mSongID);
        MILO_ASSERT(data, 0x500);
        ret = Symbol(data->Artist());
    }
    return ret;
}

int PartyModeMgr::GetCurrEventPlayerFlags() {
    MILO_ASSERT(mCurrEvent, 0x508);
    return mCurrEvent->mPlayerFlags;
}

int PartyModeMgr::GetCurrEventNumPlayers() {
    MILO_ASSERT(mCurrEvent, 0x511);
    return mCurrEvent->mNumPlayers;
}

DataArray *PartyModeMgr::GetCurrEventPlayers() {
    MILO_ASSERT(mCurrEvent, 0x51A);
    return mCurrEvent->mPlayers;
}

void PartyModeMgr::SetupCharacterData() {
    MILO_ASSERT(TheHamProvider->Property("is_in_party_mode")->Int(), 0x5DB);
    for (int i = 0; i < 2; i++) {
        HamPlayerData *hpd = TheGameData->Player(i);
        Symbol crew;
        if (hpd->Side() == kSkeletonRight) {
            crew = mRightTeamCrew;
        } else {
            crew = mLeftTeamCrew;
        }
        hpd->SetCrew(crew);
        Symbol crewChar = GetCrewCharacter(crew, rand() % GetNumCrewCharacters(crew));
        hpd->SetCharacter(crewChar);
        Symbol outfit = GetCrewLookOutfit(crewChar);
        hpd->SetCharacterOutfit(outfit);
    }
    const HamSongMetadata *pData = TheHamSongMgr.Data(
        TheHamSongMgr.GetSongIDFromShortName(TheGameData->GetSong(), false)
    );
    MILO_ASSERT(pData, 0x5F8);
    TheGameData->SetVenue(pData->Venue());
}

void PartyModeMgr::SmoothFrameMotion(int frame_idx, float f2, float f3) {
    MILO_ASSERT_RANGE(frame_idx, 0, 6, 0x64E);
    mFrameSmoothers[frame_idx].Smooth(Vector2(f2, f3), TheTaskMgr.DeltaUISeconds(), false);
}

void PartyModeMgr::ForceFrameSmootherPos(int frame_idx, float f2, float f3) {
    MILO_ASSERT_RANGE(frame_idx, 0, 6, 0x656);
    mFrameSmoothers[frame_idx].ForceValue(Vector2(f2, f3));
}

const char *PartyModeMgr::GetPlaylistString() {
    if (!mPlaylist) {
        return gNullStr;
    } else {
        String str;
        if (mPlaylist->IsCustom()) {
            str = mPlaylist->GetName();
        } else {
            str = MakeString("%s_title", mPlaylist->GetName());
        }
        const char *fmt = FormatTimeMS(mPlaylist->GetDuration());
        static Symbol songname_duration("songname_duration");
        str = MakeString(
            Localize(songname_duration, nullptr, TheLocale),
            Localize(str.c_str(), nullptr, TheLocale),
            fmt
        );
        return str.c_str();
    }
}

DataArray *PartyModeMgr::GetLeftCrewColor1AsArray() {
    static Symbol TEAM_COLORS("TEAM_COLORS");
    DataArray *pTeamArray = DataGetMacro(TEAM_COLORS);
    MILO_ASSERT(pTeamArray, 0x803);
    static Symbol left("left");
    DataArray *pTeamData = pTeamArray->FindArray(left);
    MILO_ASSERT(pTeamData, 0x807);
    static Symbol colors("colors");
    DataArray *pTeamColors = pTeamData->FindArray(colors);
    MILO_ASSERT(pTeamColors, 0x80B);
    DataArray *pTeamColor = pTeamColors->Array(1);
    MILO_ASSERT(pTeamColor, 0x80E);
    return pTeamColor;
}

DataArray *PartyModeMgr::GetLeftCrewColor2AsArray() {
    static Symbol TEAM_COLORS("TEAM_COLORS");
    DataArray *pTeamArray = DataGetMacro(TEAM_COLORS);
    MILO_ASSERT(pTeamArray, 0x817);
    static Symbol left("left");
    DataArray *pTeamData = pTeamArray->FindArray(left);
    MILO_ASSERT(pTeamData, 0x81B);
    static Symbol colors("colors");
    DataArray *pTeamColors = pTeamData->FindArray(colors);
    MILO_ASSERT(pTeamColors, 0x81F);
    DataArray *pTeamColor = pTeamColors->Array(2);
    MILO_ASSERT(pTeamColor, 0x822);
    return pTeamColor;
}

DataArray *PartyModeMgr::GetRightCrewColor1AsArray() {
    static Symbol TEAM_COLORS("TEAM_COLORS");
    DataArray *pTeamArray = DataGetMacro(TEAM_COLORS);
    MILO_ASSERT(pTeamArray, 0x82B);
    static Symbol right("right");
    DataArray *pTeamData = pTeamArray->FindArray(right);
    MILO_ASSERT(pTeamData, 0x82F);
    static Symbol colors("colors");
    DataArray *pTeamColors = pTeamData->FindArray(colors);
    MILO_ASSERT(pTeamColors, 0x833);
    DataArray *pTeamColor = pTeamColors->Array(1);
    MILO_ASSERT(pTeamColor, 0x836);
    return pTeamColor;
}

DataArray *PartyModeMgr::GetRightCrewColor2AsArray() {
    static Symbol TEAM_COLORS("TEAM_COLORS");
    DataArray *pTeamArray = DataGetMacro(TEAM_COLORS);
    MILO_ASSERT(pTeamArray, 0x83F);
    static Symbol right("right");
    DataArray *pTeamData = pTeamArray->FindArray(right);
    MILO_ASSERT(pTeamData, 0x843);
    static Symbol colors("colors");
    DataArray *pTeamColors = pTeamData->FindArray(colors);
    MILO_ASSERT(pTeamColors, 0x847);
    DataArray *pTeamColor = pTeamColors->Array(2);
    MILO_ASSERT(pTeamColor, 0x84A);
    return pTeamColor;
}

Symbol PartyModeMgr::GetLeftCrewCharOutfit(int char_idx, int outfit_idx) {
    int numCrewChars = GetNumCrewCharacters(mLeftTeamCrew);
    MILO_ASSERT(char_idx < numCrewChars, 0x877);
    if (char_idx < 0) {
        char_idx = rand() % numCrewChars;
    }
    Symbol charSym = GetCrewCharacter(mLeftTeamCrew, char_idx);
    int numCharOutfits = GetNumCharacterOutfits(charSym);
    MILO_ASSERT(outfit_idx < numCharOutfits, 0x880);
    if (outfit_idx < 0) {
        outfit_idx = rand() % numCharOutfits;
    }
    return GetCharacterOutfit(charSym, outfit_idx);
}

Symbol PartyModeMgr::GetRightCrewCharOutfit(int char_idx, int outfit_idx) {
    int numCrewChars = GetNumCrewCharacters(mRightTeamCrew);
    MILO_ASSERT(char_idx < numCrewChars, 0x88E);
    if (char_idx < 0) {
        char_idx = rand() % numCrewChars;
    }
    Symbol charSym = GetCrewCharacter(mRightTeamCrew, char_idx);
    int numCharOutfits = GetNumCharacterOutfits(charSym);
    MILO_ASSERT(outfit_idx < numCharOutfits, 0x897);
    if (outfit_idx < 0) {
        outfit_idx = rand() % numCharOutfits;
    }
    return GetCharacterOutfit(charSym, outfit_idx);
}

void PartyModeMgr::ChangeToAnotherGameMode() {
    int i1 = (GetEnumFromModeName(mCurrEvent->mModeName) + 1) % 5;
    Symbol name = GetModeNameFromEnum(i1);
    while (!IsModeIncluded(name)) {
        i1 = (i1 + 1) % 5;
        name = GetModeNameFromEnum(i1);
    }
    mCurrEvent->mModeName = name;
}

void PartyModeMgr::EndPartyStats() {
    DateTime dt;
    GetDateAndTime(dt);
    unsigned int diff = dt.DiffSeconds(mPartyStatsStartTime);
    for (int i = 0; i < 2; i++) {
        HamPlayerData *playerData = TheGameData->Player(i);
        MILO_ASSERT(playerData, 0x9DA);
        HamProfile *profile = TheProfileMgr.GetProfileFromPad(playerData->PadNum());
        if (profile) {
            MetagameStats *stats = profile->GetMetagameStats();
            if (stats) {
                stats->UpdatePartyStats(diff);
            }
        }
    }
}

void PartyModeMgr::StorePlayerFramePos(int player, float f2, float f3) {
    MILO_ASSERT(player >= 0 && player < mPlayers.size(), 0x369);
    mPlayers[player]->StoreFramePos(f2, f3);
}

void PartyModeMgr::StorePlayerFrameScale(int player, float scale) {
    MILO_ASSERT(player >= 0 && player < mPlayers.size(), 0x370);
    mPlayers[player]->StoreFrameScale(scale);
}

const char *PartyModeMgr::GetPlayerARTexPath(int playerIndex) {
    MILO_ASSERT_RANGE(playerIndex, 0, mPlayers.size(), 0x377);
    return mPlayers[playerIndex]->GetTexPath();
}

void PartyModeMgr::SetRandomCharacters() {
    for (int i = 0; i < 2; i++) {
        HamPlayerData *pPlayerData = TheGameData->Player(i);
        MILO_ASSERT(pPlayerData, 0x5CA);
        Symbol symRandomCharacter = mCharacters[rand() % mCharacters.size()];
        MILO_ASSERT(symRandomCharacter != gNullStr, 0x5CE);
        Symbol crew = GetCrewForCharacter(symRandomCharacter);
        pPlayerData->SetCharacter(symRandomCharacter);
        pPlayerData->SetCrew(crew);
        pPlayerData->SetOutfit(GetCharacterOutfit(symRandomCharacter, 0));
    }
}

int PartyModeMgr::GetPlayerPhotoIndex(int player) {
    MILO_ASSERT_RANGE(player, 0, mPlayers.size(), 0x5FF);
    return mPlayers[player]->GetPhotoIndex();
}

void PartyModeMgr::BroadcastSyncMsg(Symbol msgType) {
    MILO_LOG("[PartyModeMgr::BroadcastSyncMsg] Broadcasting msg (%s).\n", msgType.Str());
    Message msg(msgType);
    HandleType(msg);
    TheUI->Handle(msg, false);
}

void PartyModeMgr::SendPartyOptionsToRC() {
    HamProfile *profile = GetValidProfile();
    if (!profile) {
        BroadcastSyncMsg("skipped_sync");
    } else {
        mSetPartyOptionsJob =
            new SetPartyOptionsJob(this, profile->GetOnlineID()->ToString());
        TheRockCentral.ManageJob(mSetPartyOptionsJob);
    }
}

void PartyModeMgr::GetPartyOptionsFromRC() {
    HamProfile *profile = GetValidProfile();
    if (!profile) {
        BroadcastSyncMsg("skipped_sync");
    } else {
        mGetPartyOptionsJob =
            new GetPartyOptionsJob(this, profile->GetOnlineID()->ToString());
        TheRockCentral.ManageJob(mGetPartyOptionsJob);
    }
}

void PartyModeMgr::ReadPartyOptions() {
    mGetPartyOptionsJob->GetOptions();
    mGetPartyOptionsJob = 0;
    BroadcastSyncMsg("options_updated");
}

void PartyModeMgr::GetPartySongQueueFromRC() {
    HamProfile *profile = GetValidProfile();
    if (!profile) {
        BroadcastSyncMsg("skipped_sync");
    } else {
        mGetPartySongQueueJob =
            new GetPartySongQueueJob(this, profile->GetOnlineID()->ToString());
        TheRockCentral.ManageJob(mGetPartySongQueueJob);
    }
}

void PartyModeMgr::DeleteSongFromRCPartySongQueue(int songID) {
    HamProfile *profile = GetValidProfile();
    if (!profile) {
        BroadcastSyncMsg("skipped_sync");
    } else {
        mDeleteSongFromPartySongQueueJob = new DeleteSongFromPartySongQueueJob(
            this, profile->GetOnlineID()->ToString(), songID
        );
        TheRockCentral.ManageJob(mDeleteSongFromPartySongQueueJob);
    }
}

void PartyModeMgr::AddNextSongToRCPartySongQueue() {
    HamProfile *profile = GetValidProfile();
    if (!profile) {
        BroadcastSyncMsg("skipped_sync");
    } else {
        mAddSongToPartySongQueueJob = new AddSongToPartySongQueueJob(
            this, profile->GetOnlineID()->ToString(), mPartySongQueue.front().mSongID
        );
        TheRockCentral.ManageJob(mAddSongToPartySongQueueJob);
    }
}

Symbol PartyModeMgr::GetNextMode() {
    MILO_ASSERT(mModePicker.Size() > 0, 0x17B);
    return mModePicker.GetNext();
}

void PartyModeMgr::DetermineSubMode(Symbol *pMode, Symbol *pSubMode) {
    if (mUsePlaytestData) {
        *pMode = mModePicker.GetNext();
        *pSubMode = mSubModePicker.GetNext();
    } else if (TheHamProvider->Property("is_in_party_mode")->Int()
               && !mRoundsUntilShowdown) {
        static Symbol showdown("showdown");
        static Symbol ffa("ffa");
        *pMode = showdown;
        *pSubMode = ffa;
    } else {
        *pMode = mModePicker.GetNext();
        if (mEventBucketSequences) {
            Symbol sym = mEventBucketSequences->Sym(mRoundsPlayed + 1);
            static Symbol event_buckets("event_buckets");
            DataArray *arr = mPartyModeCfg->FindArray(event_buckets);
            arr = arr->FindArray(sym);
            int i12 = 0;
            for (int i = 1; i < arr->Size(); i++) {
                DataArray *curArr = arr->Array(i);
                if (IsModeIncluded(curArr->Sym(0))) {
                    i12 += curArr->Int(1);
                }
            }
            i12 = rand() % i12;
            int i = 1;
            int i4 = 0;
            for (; i < arr->Size(); i++) {
                DataArray *curArr = arr->Array(i);
                if (IsModeIncluded(curArr->Sym(0))) {
                    i4 += curArr->Int(1);
                    if (i12 < i4) {
                        *pMode = curArr->Sym(0);
                        break;
                    }
                }
            }
        }
        static Symbol dance_battle("dance_battle");
        if (*pMode == dance_battle) {
            static Symbol ffa("ffa");
            *pSubMode = ffa;
        }
    }
}

void PartyModeMgr::DetermineSubModeSong(Symbol *pShortName, int *pSongID) {
    if (mPlaytestEventSequences && !mPlaylist) {
        DataArray *arr = mPlaytestEventSequences->Array(mRoundsPlayed + 1);
        if (arr) {
            int rank = arr->Int(rand() % arr->Size());
            MILO_ASSERT_FMT(
                rank >= 1 && rank <= 4, "0x%08X is an invalid DJ logic intensity rank\n", (int)rank
            );
            *pShortName = mSubModeSongPickers[rank].GetNext();
            *pSongID = TheHamSongMgr.GetSongIDFromShortName(*pShortName);
            return;
        } else {
            MILO_NOTIFY(
                "DJ logic data doesn't contain enough information for 0x%08X rounds, picking random song instead",
                (int)mRoundsPlayed
            );
        }
    }
    *pShortName = mSubModeSongPicker.GetNext();
    *pSongID = TheHamSongMgr.GetSongIDFromShortName(*pShortName);
}

bool PartyModeMgr::IsTeamSignedIn(int i1) {
    if (i1 == 1) {
        return mTeam1PlayerPicker.Size() > 0;
    } else if (i1 == 2) {
        return mTeam2PlayerPicker.Size() > 0;
    } else {
        return false;
    }
}

PartyModePlayer *PartyModeMgr::CreatePartyModePlayer() {
    int objIdx = mPlayers.size() % mARObjectIndices.size() + 1;
    DataArray *objArr = mARObjects->Array(objIdx);
    PartyModeARObject *arObj = new PartyModeARObject(objArr);
    PartyModePlayer *player = new PartyModePlayer(arObj);
    player->SetCharacter(mCharacters[rand() % mCharacters.size()]);
    player->SetIndex(mPlayers.size());
    if (mTeam1PlayerPicker.Size() <= 0) {
        player->SetPhotoIndex(mTeam1Players.size());
    } else {
        player->SetPhotoIndex(mTeam2Players.size() + 4);
    }
    return player;
}

void PartyModeMgr::AddPlayerToTeam(int team) {
    PartyModePlayer *player = CreatePartyModePlayer();
    mPlayers.push_back(player);
    if (team == 1) {
        mTeam1Players.push_back(player);
    } else if (team == 2) {
        mTeam2Players.push_back(player);
    }
}

void PartyModeMgr::ClearTeam(int team) {
    switch (team) {
    case 1:
        for (int i = 0; i != mTeam1Players.size(); i++) {
            delete mTeam1Players[i];
        }
        mTeam1Players.clear();
        break;
    case 2:
        for (int i = 0; i != mTeam2Players.size(); i++) {
            delete mTeam2Players[i];
        }
        mTeam2Players.clear();
        break;
    default:
        MILO_ASSERT(team == 1 || team == 2, 0x20F);
        break;
    }
}

void PartyModeMgr::ResetPlayers() {
    for (int i = 0; i < mPlayers.size(); i++) {
        RELEASE(mPlayers[i]);
    }
    mPlayers.clear();
    mTeam1Players.clear();
    mTeam2Players.clear();
    mTeam1PlayerPicker.Clear();
    mTeam2PlayerPicker.Clear();
    mLeftPlayer = 0;
    mRightPlayer = 0;
}

void PartyModeMgr::ResetMicrogames() {
    mSubModePicker.Clear();
    DataArray *gamesArr = mPartyModeCfg->FindArray("party_mode_microgames");
    for (int i = 1; i < gamesArr->Size(); i++) {
        mSubModePicker.AddItem(gamesArr->Sym(i));
    }
    mSubModePicker.Randomize();
}

int PartyModeMgr::PickNextPlayer() {
    int ret = -1;
    if (mCurrentTeamSelector == 2) {
        ret = mTeam1PlayerPicker.GetNext();
        if (mUsePlaytestData) {
            ret = ret % mTeam1Players.size();
        }
        mCurrentTeamSelector = 1;
        if (mPlayerSequences) {
            DataArray *arr = mPlayerSequences->Array(mRoundsPlayed + 1);
            int idx = 0;
            if (mTeam1Players.size() > mTeam2Players.size())
                idx = 1;
            ret = arr->Int(idx);
        }
    } else if (mCurrentTeamSelector == 1) {
        ret = mTeam2PlayerPicker.GetNext();
        if (mUsePlaytestData) {
            ret = ret % mTeam2Players.size() + mTeam1Players.size();
        }
        mCurrentTeamSelector = 2;
        if (mPlayerSequences) {
            DataArray *arr = mPlayerSequences->Array(mRoundsPlayed + 1);
            int idx = 1;
            if (mTeam2Players.size() < mTeam1Players.size())
                idx = 0;
            auto teamCount = arr->Int(idx);
            ret = mTeam1Players.size() + teamCount;
        }
    }
    return ret;
}

void PartyModeMgr::ShufflePlaylist(bool b1) {
    MILO_ASSERT(IsUsingPlaylist(), 0x731);
    if (b1) {
        mSubModeSongPicker.mMode = 2;
        mSubModeSongPicker.mNumGets = 0;
    } else if (mIsPlaylistShuffled) {
        mSubModeSongPicker.mMode = 0;
        SetSongsFromPlaylist();
    }
    mIsPlaylistShuffled = b1;
}

void PartyModeMgr::ResetParty() {
    mRoundsPlayed = 0;
    mIsShowdown = false;
    mCurrentTeamSelector = 2;
    ResetPlayers();
    mDifficulty = DefaultDifficulty();
    if (mRandomSongPool.empty()) {
        TheHamSongMgr.GetRandomlySelectableRankedSongs(mRandomSongPool);
    }
    mLeftTeamPrevScore = mLeftTeamScore;
    mRightTeamPrevScore = mRightTeamScore;
    mLeftTeamScore = 0;
    mRightTeamScore = 0;
    mLeftTeamPrevScorePercent = 0;
    mRightTeamPrevScorePercent = 0;
    Symbol crew(gNullStr);
    HamPlayerData *pPlayerData = TheGameData->Player(0);
    MILO_ASSERT(pPlayerData, 0x17F);
    pPlayerData->SetCrew(crew);
    pPlayerData = TheGameData->Player(1);
    MILO_ASSERT(pPlayerData, 0x182);
    pPlayerData->SetCrew(crew);
    mWinningSide = 2;
    mJustWonSide = 2;
    mPlaytestEventSequences = nullptr;
}

void PartyModeMgr::InitCharacters() {
    mCharacters.clear();
    DataArray *crewsArr = SystemConfig()->FindArray("selectable_crews", false);
    if (crewsArr) {
        for (int i = 1; i < crewsArr->Size(); i++) {
            Symbol crew = crewsArr->Sym(i);
            int numChars = GetNumCrewCharacters(crew);
            for (int j = 0; j < numChars; j++) {
                Symbol charSym = GetCrewCharacter(crew, j);
                mCharacters.push_back(charSym);
            }
        }
    }
}

void PartyModeMgr::CrewShowdownRematch() {
    mLeftTeamPrevScore = mLeftTeamScore;
    mRightTeamPrevScore = mRightTeamScore;
    mRoundsPlayed = 0;
    mIsShowdown = false;
    mCurrentTeamSelector = 2;
    mLeftTeamScore = 0;
    mRightTeamScore = 0;
    mLeftTeamPrevScorePercent = 0;
    mRightTeamPrevScorePercent = 0;
    SetCurrEvent();
    mWinningSide = 2;
    mJustWonSide = 2;
    static Symbol rematches_this_boot("rematches_this_boot");
    gRematchCount++;
    SendDataPoint("crew_throwdown/rematch", rematches_this_boot, gRematchCount);
}

void PartyModeMgr::SetupInfinitePartyMode() {
    TheHamSongMgr.GetRandomlySelectableRankedSongs(mRandomSongPool);
    if (!(mPlaylist)) {
        ResetSongs();
        mSubModeSongPicker.mNumGets = 0;
        mSubModeSongPicker.mMode = 2;
    } else {
        mSubModeSongPicker.SetMode(0);
    }
    ResetModes(true);
    ResetMicrogames();
    RELEASE(mCurrEvent);
    mCurrEvent = new SubMode();
    GetDateAndTime(mRoundStartTime);
}

void PartyModeMgr::SetModes() {
    ResetModes(false);
    if (mCurrEvent && !IsModeIncluded(mCurrEvent->mModeName)) {
        Symbol mode, submode;
        DetermineSubMode(&mode, &submode);
        mCurrEvent->mModeName = mode;
        mCurrEvent->mSubModeName = submode;
    }
}

void PartyModeMgr::SetSongAndDefaults(Symbol song, Symbol mode, bool force_crew_outfit) {
    static Symbol dance_battle("dance_battle");
    static Symbol strike_a_pose("strike_a_pose");
    if (mCurrEvent) {
        RELEASE(mCurrEvent);
    }
    mCurrEvent = new SubMode();
    if (song.Null()) {
        song = mSubModeSongPicker.GetNext();
    }
    mCurrEvent->mSongName = song;
    if (mode.Null()) {
        mode = GetNextMode();
    }
    mCurrEvent->mModeName = mode;
    int songID = TheHamSongMgr.GetSongIDFromShortName(song);
    mCurrEvent->mSongID = songID;
    const HamSongMetadata *data = TheHamSongMgr.Data(songID);
    HamPlayerData *songPlayerData;
    Symbol songCrew;
    Symbol songChar;
    Symbol songOutfit;
    HamPlayerData *altPlayerData;
    Symbol altCrew;
    Symbol altChar;
    Symbol altOutfit;
    bool isSpecialMode = mode == dance_battle || mode == strike_a_pose;
    MetaPerformer::Current()->CalcCharacters(
        data,
        isSpecialMode,
        (PlayerFlag)2,
        songPlayerData,
        songCrew,
        songChar,
        songOutfit,
        altPlayerData,
        altCrew,
        altChar,
        altOutfit
    );
    if (force_crew_outfit) {
        songOutfit = GetCrewLookOutfit(songChar);
        altOutfit = GetCrewLookOutfit(altChar);
    }
    songPlayerData->SetCharacter(songChar);
    songPlayerData->SetOutfit(songOutfit);
    songPlayerData->SetCrew(songCrew);
    altPlayerData->SetCharacter(altChar);
    altPlayerData->SetOutfit(altOutfit);
    altPlayerData->SetCrew(altCrew);
    MILO_LOG(
        "PartyModeMgr::SetSongAndDefaults(Symbol song = '%s', Symbol mode = '%s', bool force_crew_outfit = %d)\n",
        song,
        mode,
        force_crew_outfit
    );
    MILO_LOG(
        "   %s: songChar = '%s' songCrew = '%s' songOutfit = %s\n",
        songPlayerData->Side() == kSkeletonLeft ? "left" : "right",
        songChar,
        songCrew,
        songOutfit
    );
    MILO_LOG(
        "   %s: altChar  = '%s' altCrew  = '%s' altOutfit  = %s\n",
        altPlayerData->Side() == kSkeletonLeft ? "left" : "right",
        altChar,
        altCrew,
        altOutfit
    );
    TheGameData->SetSong(song);
    MetaPerformer::Current()->SetVenuePref("default");
    MetaPerformer::Current()->Handle(Message("setup_venue"), true);
    ConfigHistory ch;
    ch.mForceCrewOutfit = force_crew_outfit;
    ch.mMode = mode;
    ch.mSong = song;
    ch.mTimeStamp = SystemMs();
    mCfgHistories.push_back(ch);
    PruneHistory();
}

PartyModeMgr::SubMode *PartyModeMgr::CreateEventA() {
    Symbol mode;
    Symbol submode;
    DetermineSubMode(&mode, &submode);
    int flags = 0;
    std::vector<int> vec;
    int numPlayers = 0;
    DetermineSubModePlayers(mode, &flags, &numPlayers, &vec);
    int songID = 0;
    Symbol shortname;
    DetermineSubModeSong(&shortname, &songID);
    SubMode *event = new SubMode();
    event->mNumPlayers = numPlayers;
    event->mPlayerFlags = flags;
    event->mSongID = songID;
    event->mSongName = shortname;
    event->mSubModeName = submode;
    event->mModeName = mode;
    event->mPlayerIndices.insert(event->mPlayerIndices.begin(), vec.begin(), vec.end());
    DataArray *a = new DataArray(numPlayers);
    for (int i = 0; i < numPlayers; i++) {
        auto playerNode = a->Node(i);
        playerNode = event->mPlayerIndices[i];
    }
    event->mPlayers = a;
    return event;
}

void PartyModeMgr::ToggleIncludedMode(Symbol mode) {
    mIncludedModesMask ^= 1 << GetEnumFromModeName(mode);
    bool high = (1 << GetEnumFromModeName(mode)) & mIncludedModesMask;
    MILO_LOG("----- TOGGLING %s to %s\n", mode.Str(), high ? "true" : "false");
    static Symbol is_in_infinite_party_mode("is_in_infinite_party_mode");
    if (!unk40) {
        if (TheHamProvider->Property(is_in_infinite_party_mode)->Int() != 0) {
            SendDataPoint("partymode/mode_toggle", mode, high);
        } else {
            SendDataPoint("crew_throwdown/mode_toggle", mode, high);
        }
    }
}

void PartyModeMgr::UseSelectedPlaylist(bool b1) {
    if (b1) {
        MetaPerformer *pPerformer = MetaPerformer::Current();
        MILO_ASSERT(pPerformer, 0x6F1);
        mPlaylist = pPerformer->GetPlaylist();
        MILO_ASSERT(mPlaylist, 0x6F4);
        SetSongsFromPlaylist();
    } else {
        if (mPlaylist) {
            ResetSongs();
        }
        mPlaylist = nullptr;
    }
}

void PartyModeMgr::SetPlaylist(Playlist *playlist) {
    MILO_ASSERT(playlist, 0x706);
    mPlaylist = playlist;
    SetSongsFromPlaylist();
}

void PartyModeMgr::SetCurrEvent() {
    if (mCurrEvent) {
        RELEASE(mCurrEvent);
    }
    mCurrEvent = CreateEventA();
    static Symbol showdown("showdown");
    mIsShowdown = mCurrEvent->mModeName == showdown;
    mLeftPlayer = mCurrEvent->mNumPlayers > 0 ? mPlayers[mCurrEvent->mPlayerIndices[0]] : nullptr;
    mRightPlayer = mCurrEvent->mNumPlayers > 1 ? mPlayers[mCurrEvent->mPlayerIndices[1]] : nullptr;
}

DataNode PartyModeMgr::OnGetSmoothedFramePos(const DataArray *a) {
    MILO_ASSERT(a->Size() == 5, 0x65E);
    int idx = a->Int(2);
    Vector2 v = mFrameSmoothers[idx].Value();
    *a->Var(3) = v.x;
    *a->Var(4) = v.y;
    return 0;
}

DataNode PartyModeMgr::OnStableSong() {
    return mCfgHistories.empty() ? Symbol("") : mCfgHistories[0].mSong;
}

DataNode PartyModeMgr::OnStableMode() {
    return mCfgHistories.empty() ? Symbol("") : mCfgHistories[0].mMode;
}

DataNode PartyModeMgr::OnMsg(const SmartGlassMsg &msg) {
    MILO_LOG("SmartGlass: I should update Party Mode options/song from RC\n");
    SendDataPoint("smartglass/party");
    GetPartyOptionsFromRC();
    GetPartySongQueueFromRC();
    BroadcastSyncMsg("update_party_from_rc");
    return 1;
}

void PartyModeMgr::FinalizePlaytestParty() {
    int numEvents = mPartyModePlaytestEvents->Size() - 1;
    std::vector<Symbol> modeVec(numEvents, gNullStr);
    std::vector<Symbol> songVec(numEvents, gNullStr);
    std::vector<Symbol> subModeVec(numEvents, gNullStr);
    std::vector<int> team1Players;
    std::vector<int> team2Players;

    for (int i = 1; i <= numEvents; i++) {
        DataArray *eventArr = mPartyModePlaytestEvents->Array(i);
        Symbol mode = eventArr->Sym(0);
        Symbol subMode = eventArr->Sym(1);
        Symbol song = eventArr->Sym(2);
        if (eventArr->Size() > 4) {
            int team = eventArr->Int(3);
            auto playerOffset = eventArr->Int(4);
            int playerIdx = playerOffset + mPlayers.size();
            team1Players.push_back(team);
            team2Players.push_back(playerIdx);
        }
        modeVec[i - 1] = mode;
        subModeVec[i - 1] = subMode;
        songVec[i - 1] = song;
    }

    mModePicker.Clear();
    mSubModeSongPicker.Clear();
    mModePicker.AddItems(modeVec);
    mSubModePicker.Clear();
    mSubModePicker.AddItems(subModeVec);
    mSubModeSongPicker.Clear();
    mSubModeSongPicker.AddItems(songVec);
    mTeam1PlayerPicker.Clear();
    mTeam1PlayerPicker.AddItems(team1Players);
    mTeam2PlayerPicker.Clear();
    mTeam2PlayerPicker.AddItems(team2Players);

    mRoundsTotal = numEvents - 1;
    mRoundsUntilShowdown = numEvents - 1;
    mMaxPointsPerEvent = (float)(numEvents - 1) + 1.0f;
    mSixStarBonus = mEventScoring->FindArray("six_star_bonus")->Float(1);
    SetCurrEvent();
}

DataNode PartyModeMgr::OnMsg(const RCJobCompleteMsg &msg) {
    // Handle job failure - cancel all pending party mode jobs
    if (!msg.Success()) {
        MILO_LOG("[PartyModeMgr::OnMsg] Party net API failed.\n");
        // Iterate through all 5 job pointers and cancel the failed one
        SetPartyOptionsJob **pJob = &mSetPartyOptionsJob;
        int count = 5;
        do {
            if (*pJob == msg.Job()) {
                (*pJob)->Cancel(false);
                *pJob = 0;
            }
            count--;
            pJob++;
        } while (count != 0);
        BroadcastSyncMsg("skipped_sync");
        return 1;
    }

    // Track whether we need to notify SmartGlass of updates
    bool b = false;

    // Handle successful job completion - dispatch based on job type
    if (msg.Job() == mSetPartyOptionsJob) {
        BroadcastSyncMsg("options_sent");
        mSetPartyOptionsJob = 0;
        b = true;
    } else {
        if (msg.Job() == mGetPartyOptionsJob) {
            ReadPartyOptions();
        } else {
            if (msg.Job() == mGetPartySongQueueJob) {
                ReadPartySongQueue();
            } else {
                if (msg.Job() == mDeleteSongFromPartySongQueueJob) {
                    mDeleteSongFromPartySongQueueJob = 0;
                } else {
                    if (msg.Job() != mAddSongToPartySongQueueJob) {
                        goto leave;
                    }
                    mAddSongToPartySongQueueJob = 0;
                    mPartySongQueue.pop_front();
                    if (!mPartySongQueue.empty()) {
                        AddNextSongToRCPartySongQueue();
                        b = true;
                    } else {
                        mAddSongToPartySongQueueJob = 0;
                        mQueueStateValid = false;
                    }
                }
                BroadcastSyncMsg("song_queue_updated");
                b = true;
            }
        }
    }
leave:
    // Notify SmartGlass app if party data changed
    if (b) {
        DataNode party("party");
        DataNode updated("updated");
        ThePlatformMgr.SmartGlassSend(0, DataArrayPtr(updated, party));
    }
    return 1;
}

void PartyModeMgr::FinalizeTeam(int team) {
    std::vector<PartyModePlayer *> *teamPlayers;
    PseudoRandomPicker<int> *teamPicker;
    switch (team) {
    case 1:
        teamPlayers = &mTeam1Players;
        teamPicker = &mTeam1PlayerPicker;
        break;
    case 2:
        teamPlayers = &mTeam2Players;
        teamPicker = &mTeam2PlayerPicker;
        break;
    default:
        MILO_ASSERT(team == 1 || team == 2, 0x1EE);
        break;
    }
    int numTeamPlayers = teamPlayers->size();
    int totalPlayers = mPlayers.size();
    std::vector<int> indices;
    indices.resize(numTeamPlayers);
    for (int i = 0; i < numTeamPlayers; i++) {
        indices[i] = i + (totalPlayers - numTeamPlayers);
    }
    teamPicker->AddItems(indices);
    teamPicker->mNumGets = 0;
    teamPicker->mMode = 2;
}

void PartyModeMgr::FinalizeParty() {
    if (mUsePlaytestData) {
        FinalizePlaytestParty();
        return;
    }
    if (mSubModeSongPicker.mItems.empty()) {
        ResetSongs();
    }
    if (mModePicker.mItems.empty()) {
        ResetModes(true);
    }
    static Symbol crew_showdown_num_events("crew_showdown_num_events");
    static Symbol use_events_per_player("use_events_per_player");
    static Symbol events_per_player("events_per_player");
    static Symbol total_events("total_events");
    DataArray *numEventsArr = mPartyModeCfg->FindArray(crew_showdown_num_events, true);
    int team1Size = mTeam1Players.size();
    int team2Size = mTeam2Players.size();
    int maxTeamSize = team2Size;
    if (team2Size <= team1Size) {
        maxTeamSize = team1Size;
    }
    DataArray *usePerPlayerArr = numEventsArr->FindArray(use_events_per_player, true);
    int usePerPlayer = usePerPlayerArr->Node(1).Int(usePerPlayerArr);
    if (usePerPlayer == 0) {
        DataArray *totalArr = numEventsArr->FindArray(total_events, true);
        mRoundsTotal = totalArr->Node(maxTeamSize).Int(totalArr);
    } else {
        DataArray *perPlayerArr = numEventsArr->FindArray(events_per_player, true);
        int perPlayer = perPlayerArr->Node(1).Int(perPlayerArr);
        mRoundsTotal = perPlayer * maxTeamSize;
    }
    mRoundsUntilShowdown = mRoundsTotal;
    mMaxPointsPerEvent = (float)mRoundsTotal + 1.0f;
    {
        static Symbol six_star_bonus("six_star_bonus");
        DataArray *sixStarArr = mEventScoring->FindArray(six_star_bonus, true);
        mSixStarBonus = sixStarArr->Node(1).Float(sixStarArr);
    }
    static Symbol player_sequences("player_sequences");
    DataArray *playerSeqArr = mPartyModeCfg->FindArray(player_sequences, true);
    char buf[8];
    int minTeam = team2Size;
    int maxTeam = team1Size;
    if (team1Size < team2Size) {
        minTeam = team1Size;
        maxTeam = team2Size;
    }
    sprintf(buf, "%dv%d", minTeam, maxTeam);
    mPlayerSequences = playerSeqArr->FindArray(Symbol(buf), true);
    if (mPlayerSequences == nullptr) {
        FormatString fmt("Not enough player sequence. There will be problems.");
        TheDebug.Notify(fmt.Str());
    } else {
        TheDebug << FormatString("There is a player sequence. There will be no problems.\n").Str();
    }
    static Symbol dj_logic("dj_logic");
    static Symbol number_of_songs("number_of_songs");
    static Symbol intensity_sequence("intensity_sequence");
    static Symbol bucket_sequence("bucket_sequence");
    DataArray *djLogicArr = mPartyModeCfg->FindArray(dj_logic, true);
    DataArray *numSongsArr = djLogicArr->FindArray(number_of_songs, true);
    DataArray *roundsArr = numSongsArr->FindArray(mRoundsTotal, false);
    if (roundsArr == nullptr) {
        mPlaytestEventSequences = nullptr;
        mEventBucketSequences = nullptr;
    } else {
        mPlaytestEventSequences = roundsArr->FindArray(intensity_sequence, true);
        if (mPlaytestEventSequences == nullptr) {
            FormatString fmt("Not enough DJ logic. There will be problems.");
            TheDebug.Notify(fmt.Str());
        } else {
            TheDebug << FormatString("There is enough DJ logic. There will be no problems.\n").Str();
        }
        mEventBucketSequences = roundsArr->FindArray(bucket_sequence, true);
        if (mEventBucketSequences == nullptr) {
            FormatString fmt("Not enough mode bucket. There will be problems.");
            TheDebug.Notify(fmt.Str());
        } else {
            TheDebug << FormatString("There is mode bucket. There will be no problems.\n").Str();
        }
    }
    static Symbol team_1_size("team_1_size");
    static Symbol team_2_size("team_2_size");
    static Symbol team_1_crew("team_1_crew");
    static Symbol team_2_crew("team_2_crew");
    static Symbol difficulty("difficulty");
    SendDataPoint(
        "crew_throwdown/finalize",
        team_1_size, team1Size,
        team_2_size, team2Size,
        team_1_crew, mLeftTeamCrew,
        team_2_crew, mRightTeamCrew,
        difficulty, (int)mDifficulty
    );
    SetCurrEvent();
}

int PartyModeMgr::GetCrewColor(int team, int colorIdx) {
    float r = 0.0f, g = 0.0f, b = 0.0f;
    DataArray *colorArr = nullptr;
    if (team == 0) {
        if (colorIdx == 1) {
            colorArr = GetRightCrewColor1AsArray();
        } else if (colorIdx == 2) {
            colorArr = GetRightCrewColor2AsArray();
        }
    } else if (team == 1) {
        if (colorIdx == 1) {
            colorArr = GetLeftCrewColor1AsArray();
        } else if (colorIdx == 2) {
            colorArr = GetLeftCrewColor2AsArray();
        }
    }
    if (colorArr != nullptr) {
        r = colorArr->Node(0).Float(colorArr);
        g = colorArr->Node(1).Float(colorArr);
        b = colorArr->Node(2).Float(colorArr);
    }
    return (((int)(b * 255.0f) & 0xFF) << 8 | (int)(g * 255.0f) & 0xFF) << 8 |
           (int)(r * 255.0f) & 0xFF;
}

void PartyModeMgr::PruneHistory() {
    int now = SystemMs();
    int count = (int)mCfgHistories.size();
    for (int i = count - 1; i >= 0; i--) {
        if (now - mCfgHistories[i].mTimeStamp > 0x2ee) {
            if (i > 0) {
                mCfgHistories.erase(mCfgHistories.begin(), mCfgHistories.begin() + i);
            }
            return;
        }
    }
}

DataNode PartyModeMgr::OnSetSongAndDefaults(DataArray *_msg) {
    Symbol song;
    Symbol mode;
    bool force = false;
    int sz = _msg->Size();
    if (sz == 3) {
        mode = Symbol(gNullStr);
        song = _msg->Sym(2);
    } else if (sz == 4) {
        mode = _msg->Sym(2);
        song = _msg->Sym(3);
    } else if (sz == 5) {
        song = _msg->Sym(2);
        mode = _msg->Sym(3);
        force = _msg->Int(4) != 0;
    } else {
        song = Symbol(gNullStr);
        mode = Symbol(gNullStr);
    }
    SetSongAndDefaults(song, mode, force);
    return DataNode(0);
}

void PartyModeMgr::ResetSongs() {
    int count = (int)mRandomSongPool.size();
    Symbol shortname;
    std::vector<Symbol> songNames(count, Symbol());
    mSubModeSongPicker.mItems.resize(0, Symbol());
    for (int i = 0; i < count; i++) {
        songNames[i] = TheHamSongMgr.GetShortNameFromSongID(mRandomSongPool[i]);
    }
    mSubModeSongPicker.AddItems(songNames);
    mSubModeSongPicker.Randomize();
    for (int i = 0; i < 4; i++) {
        mSubModeSongPickers[i].mItems.resize(0, Symbol());
    }
    for (int i = 0; i < count; i++) {
        const HamSongMetadata *data = TheHamSongMgr.Data(mRandomSongPool[i]);
        int rank = data->DJIntensityRank();
        shortname = TheHamSongMgr.GetShortNameFromSongID(mRandomSongPool[i]);
        mSubModeSongPickers[rank - 1].mItems.push_back(shortname);
    }
    for (int i = 0; i < 4; i++) {
        if (mSubModeSongPickers[i].Size() > 0) {
            mSubModeSongPickers[i].Randomize();
        }
    }
}

void PartyModeMgr::ReadPartySongQueue() {
    GetPartySongQueueJob *job = mGetPartySongQueueJob;
    mPartySongQueue.clear();
    job->GetSongQueue(&mPartySongQueue);
    mGetPartySongQueueJob = nullptr;
    if (mPartySongQueue.size() != 0) {
        mCurrSyncedSongID = 0;
        while (mPartySongQueue.size() != 0) {
            Symbol shortname = TheHamSongMgr.GetShortNameFromSongID(mPartySongQueue.front().mSongID, false);
            if (!shortname.Null()) {
                break;
            }
            DeleteSongFromRCPartySongQueue(mPartySongQueue.front().mQueueIndex);
            mPartySongQueue.pop_front();
        }
        if (mPartySongQueue.size() != 0) {
            mCurrSyncedSongID = mPartySongQueue.front().mSongID;
            DeleteSongFromRCPartySongQueue(mPartySongQueue.front().mQueueIndex);
            mPartySongQueue.pop_front();
        }
    } else {
        mCurrSyncedSongID = 0;
        Symbol updated("song_queue_updated");
        BroadcastSyncMsg(updated);
    }
}

void PartyModeMgr::ToggleIncludedModeOn(Symbol mode, bool on) {
    if (on) {
        if (!IsModeIncluded(mode))
            goto toggle;
    }
    if (!on) {
        if (IsModeIncluded(mode))
            goto toggle;
    }
    return;
toggle:
    ToggleIncludedMode(mode);
}

void PartyModeMgr::ResetModes(bool resetAll) {
    unk40 = true;
    mModePicker.mItems.resize(0, Symbol());
    Symbol is_in_party_mode("is_in_party_mode");
    int isPartyMode = TheHamProvider->Property(is_in_party_mode)->Int();
    DataArray *cfgArr;
    if (isPartyMode) {
        Symbol crew_showdown_weighted_event_types("crew_showdown_weighted_event_types");
        cfgArr = mPartyModeCfg->FindArray(crew_showdown_weighted_event_types);
    } else {
        Symbol party_mode_weighted_event_types("party_mode_weighted_event_types");
        cfgArr = mPartyModeCfg->FindArray(party_mode_weighted_event_types);
    }
    if (resetAll) {
        for (int i = 0; i < 5; i++) {
            ToggleIncludedModeOn(GetModeNameFromEnum(i), false);
        }
    }
    for (int i = 1; i < cfgArr->Size(); i++) {
        DataArray *subArr = cfgArr->Node(i).Array(cfgArr);
        if (subArr) {
            Symbol sym = subArr->Sym(0);
            int weight = subArr->Node(2).Int(subArr);
            if ((resetAll && weight != 0) || IsModeIncluded(sym)) {
                int count = subArr->Node(1).Int(subArr);
                for (int j = 0; j < count; j++) {
                    mModePicker.mItems.insert(mModePicker.mItems.end(), sym);
                }
                ToggleIncludedModeOn(sym, true);
            }
        }
    }
    mModePicker.mNumGets = 0;
    mModePicker.mMode = 2;
    unk40 = false;
}

void PartyModeMgr::UpdateScores() {
    HamPlayerData *pPlayer1Data = TheGameData->Player(0);
    MILO_ASSERT(pPlayer1Data, 0x66c);
    PropertyEventProvider *pPlayer1Provider = pPlayer1Data->Provider();
    MILO_ASSERT(pPlayer1Provider, 0x66f);
    HamPlayerData *pPlayer2Data = TheGameData->Player(1);
    MILO_ASSERT(pPlayer2Data, 0x672);
    PropertyEventProvider *pPlayer2Provider = pPlayer2Data->Provider();
    MILO_ASSERT(pPlayer2Provider, 0x675);
    static Symbol score("score");
    int score1 = pPlayer1Provider->Property(score, true)->Int();
    int score2 = pPlayer2Provider->Property(score, true)->Int();
    static Symbol side("side");
    int side1 = pPlayer1Provider->Property(side, true)->Int();
    int side2 = pPlayer2Provider->Property(side, true)->Int();
    if (score2 < score1) {
        mJustWonSide = side1;
    } else if (score1 < score2) {
        mJustWonSide = side2;
    } else {
        mJustWonSide = 2;
    }
    if (mJustWonSide == 0) {
        mLeftTeamPrevScore = mLeftTeamScore;
        mLeftTeamScore += GetPointsForWin();
        mRightTeamPrevScore = mRightTeamScore;
        mRightTeamScore += GetPointsForLoss();
    } else if (mJustWonSide == 1) {
        mLeftTeamPrevScore = mLeftTeamScore;
        mLeftTeamScore += GetPointsForLoss();
        mRightTeamPrevScore = mRightTeamScore;
        mRightTeamScore += GetPointsForWin();
    } else if (mJustWonSide < 3) {
        mLeftTeamPrevScore = mLeftTeamScore;
        mLeftTeamScore += GetPointsForWin();
        mRightTeamPrevScore = mRightTeamScore;
        mRightTeamScore += GetPointsForWin();
    }
    SetLeftTeamStarBonus();
    SetRightTeamStarBonus();
    float diff = (mLeftTeamStarBonus + mLeftTeamScore) - (mRightTeamStarBonus + mRightTeamScore);
    if (diff < -0.001f) {
        mWinningSide = 1;
        return;
    }
    if (diff > 0.001f) {
        mWinningSide = 0;
        return;
    }
    if (!mIsShowdown) {
        mWinningSide = 2;
        return;
    }
    mWinningSide = mJustWonSide;
    static Symbol left("left");
    static Symbol right("right");
    static Symbol random("random");
    if (mWinningSide == 0) {
        mLeftTeamPrevScore = mLeftTeamScore;
        mLeftTeamScore += GetPointsForWin();
        SendDataPoint("crew_throwdown/tiebreaker", side, left, random, 0);
    } else if (mWinningSide == 1) {
        mRightTeamPrevScore = mRightTeamScore;
        mRightTeamScore += GetPointsForWin();
        SendDataPoint("crew_throwdown/tiebreaker", side, right, random, 0);
    } else if (mWinningSide == 2) {
        if (rand() % 2) {
            mWinningSide = 0;
            mLeftTeamPrevScore = mLeftTeamScore;
            mLeftTeamScore += GetPointsForWin();
            SendDataPoint("crew_throwdown/tiebreaker", side, left, random, 1);
        } else {
            mWinningSide = 1;
            mRightTeamPrevScore = mRightTeamScore;
            mRightTeamScore += GetPointsForWin();
            SendDataPoint("crew_throwdown/tiebreaker", side, right, random, 1);
        }
    }
}

// TODO: implement SetSongsFromPlaylist
void PartyModeMgr::SetSongsFromPlaylist() {}
