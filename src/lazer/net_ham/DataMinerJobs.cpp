#include "net_ham\DataMinerJobs.h"
#include "game\GamePanel.h"
#include "hamobj\HamDirector.h"
#include "hamobj\HamGameData.h"
#include "hamobj\HamMaster.h"
#include "hamobj\HamMove.h"
#include "hamobj\ScoreUtl.h"
#include "meta_ham\MetaPerformer.h"
#include "meta_ham\Playlist.h"
#include "meta_ham\ProfileMgr.h"
#include "net_ham\RCJobDingo.h"
#include "obj\Data.h"
#include "obj\Dir.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "os\PlatformMgr.h"
#include "utl\DataPointMgr.h"
#include "utl\Str.h"
#include "utl\Symbol.h"
#include "hamobj\HamGameData.h"
#include "hamobj\PracticeSection.h"
#include "game\GamePanel.h"
#include "meta_ham\ProfileMgr.h"
#include "os\PlatformMgr.h"
#include "meta_ham\HamProfile.h"
#include "hamobj\HamDirector.h"
#include "obj\Dir.h"
#include "os\Debug.h"
#include "utl\MakeString.h"
#include "xdk\xapilibi\xbox.h"

const char *GameEndedDataPointJob::GetXUIDStrFromProfile(HamProfile *profile) {
    int padNum = profile->GetPadNum();
    XUID xuid = 0;
    DWORD result = XUserGetXUID(padNum, &xuid);
    if (result != 0) {
        TheDebug.Notify(MakeString("XUserGetXUID returned %u", result));
    }
    return MakeString("%016I64X", xuid);
}

GameEndedDataPointJob::GameEndedDataPointJob(
    Hmx::Object *callback, EndGameResult const &result
)
    : RCJob("dataminer/game_ended/", callback) {
    static Symbol mode("mode");
    static Symbol song("song");
    static Symbol reason("reason");
    static Symbol song_position("song_position");
    static Symbol playlist_perform("playlist_perform");
    static Symbol perform("perform");
    static Symbol dance_battle("dance_battle");
    static Symbol perform_legacy("perform_legacy");
    static Symbol practice("practice");
    static Symbol showdown("showdown");
    static Symbol score("score");
    static Symbol num_stars("num_stars");
    static Symbol perf_no_flashcard("perf_no_flashcard");
    static Symbol perf_current_stars("perf_current_stars");
    static Symbol photos_disabled("photos_disabled");
    static Symbol freestyle_disabled("freestyle_disabled");
    static Symbol num_bid_vcmds("num_bid_vcmds");
    static Symbol num_shell_vcmds("num_shell_vcmds");
    static Symbol custom_session("custom_session");
    static Symbol challenge("challenge");

    Symbol lastMode = MetaPerformer::Current()->LastPlayedMode();
    float song_pos = 0.0f;
    Symbol songSym = TheGameData->GetSong();

    if (TheMaster != nullptr && TheMaster->IsLoaded()) {
        float streamMs = TheMaster->StreamMs() / TheMaster->SongDurationMs();
        song_pos = -streamMs >= 0.0f ? 0.0f : streamMs;
        song_pos = song_pos - 1.0f >= 0.0f ? 1.0f : song_pos;
    }

    DataPoint dataP;

    dataP.AddPair(mode, DataNode(lastMode));
    dataP.AddPair(song, DataNode(songSym));
    dataP.AddPair(reason, (int)result);
    dataP.AddPair(song_position, song_pos);

    if (lastMode != showdown && lastMode != playlist_perform && lastMode != perform &&
        lastMode != dance_battle && lastMode != perform_legacy && lastMode != challenge) {
        MetaPerformer *perf = MetaPerformer::Current();
        if (perf->GetMoveScores().size() != 0) {
            MoveDir *moves = TheHamDirector->GetWorld()->Find<MoveDir>("moves", true);
            MILO_ASSERT(moves, 0x5a);
            PracticeSection *section = nullptr;
            for (ObjDirItr<PracticeSection> itr(moves, true); itr != nullptr; ++itr) {
                if (itr->GetDifficulty() == TheGameData->Player(0)->GetDifficulty()) {
                    section = itr;
                    break;
                }
            }
            MILO_ASSERT(section, 0x64);
            int num_steps = section->Steps().size();
            unsigned long num_scores = perf->GetMoveScores().size();
            if (num_scores > num_steps) {
                String str(MakeString("(%d/%d)", num_scores, num_steps));
                dataP.AddPair(custom_session, DataNode(str));
            }
        }
    } else {
        int num_stars_int = 0;
        const DataNode *pNode = TheGamePanel->Property(num_stars, false);
        if (pNode != nullptr) {
            num_stars_int = (int)pNode->Float();
        }
        dataP.AddPair(perf_current_stars, num_stars_int);
        dataP.AddPair(perf_no_flashcard, (int)MetaPerformer::Current()->CompletedSongWithNoFlashcards());
    }

    Symbol crew(gNullStr);
    Symbol character(gNullStr);
    const char *perf_move_ratings_str = "perf_move_ratings";
    const char *perf_calories_str = "perf_calories";
    const char *new_rank_str_base = "new_rank";
    const char *move_ratings_prefix = "move_ratings=";
    const char *perf_current_score_str = "perf_current_score";
    const char *diff_str_base = "diff";
    const char *new_content_str_base = "new_content";
    const char *comma_str = ",";
    const char *pract_move_ratings_str = "pract_move_ratings";
    const char *character_str_base = "character";
    const char *crew_str_base = "crew";
    const char *num_playlists_str_base = "num_playlists";
    const char *perf_fitness_mode_str = "perf_fitness_mode";
    const char *player_name_str_base = "player_name";
    const char *xuid_str_base = "xuid";

    for (int i = 0; i < 2; i++) {
        HamPlayerData *pData = TheGameData->Player(i);
        crew = pData->Crew();
        character = pData->Char();
        
        char buf[32];
        itoa(i, buf, 10);

        String crew_str(crew_str_base); crew_str += buf;
        String char_str(character_str_base); char_str += buf;
        String diff_str(diff_str_base); diff_str += buf;
        String score_str(perf_current_score_str); score_str += buf;
        String ratings_str;
        String move_ratings_str(move_ratings_prefix); move_ratings_str += buf;

        bool hasRatings = true;
        if (lastMode == perform || lastMode == dance_battle || lastMode == perform_legacy) {
            ratings_str = perf_move_ratings_str;
            ratings_str += buf;
        } else if (lastMode == practice) {
            ratings_str = pract_move_ratings_str;
            ratings_str += buf;
        } else {
            hasRatings = false;
        }

        if (hasRatings && CompileMoveRatings(move_ratings_str, i, lastMode == practice)) {
            dataP.AddPair(ratings_str.c_str(), DataNode(move_ratings_str));
        }

        static Symbol score("score");

        dataP.AddPair(crew_str.c_str(), DataNode(crew));
        dataP.AddPair(char_str.c_str(), DataNode(character));
        dataP.AddPair(diff_str.c_str(), (int)TheGameData->Player(i)->GetDifficulty());
        
        const DataNode *scoreNode = pData->Provider()->Property(score, true);
        dataP.AddPair(score_str.c_str(), scoreNode->Int());

        int padNum = pData->PadNum();
        HamProfile *prof = TheProfileMgr.GetProfileFromPad(padNum);
        if (prof != nullptr && prof->HasValidSaveData()) {
            if (prof->IsSignedIn()) {
                const char *xuid = GetXUIDStrFromProfile(prof);
                String xuid_str(xuid_str_base); xuid_str += buf;
                dataP.AddPair(xuid_str.c_str(), DataNode(xuid));

                String name_str(player_name_str_base); name_str += buf;
                dataP.AddPair(name_str.c_str(), DataNode(ThePlatformMgr.GetName(padNum)));
            }

            String acc_str;
            const AccomplishmentProgress &accProg = prof->GetAccomplishmentProgress();
            const std::list<std::pair<Symbol, Symbol> > &newAwards = accProg.GetNewAwards();
            for (std::list<std::pair<Symbol, Symbol> >::const_iterator it = newAwards.begin(); it != newAwards.end(); ++it) {
                if (it != newAwards.begin()) {
                    acc_str += comma_str;
                }
                acc_str += it->first.Str();
            }

            if (!acc_str.empty()) {
                String new_content_str(new_content_str_base); new_content_str += buf;
                dataP.AddPair(new_content_str.c_str(), DataNode(acc_str));
            }

            const char *rank_title = gNullStr;
            if (prof->GetMetagameRank()->HasNewRank()) {
                rank_title = prof->GetMetagameRank()->GetRankTitle().Str();
            }
            if (rank_title != gNullStr) {
                String new_rank_str(new_rank_str_base); new_rank_str += buf;
                dataP.AddPair(new_rank_str.c_str(), DataNode(rank_title));
            }

            if (lastMode == perform || lastMode == dance_battle || lastMode == perform_legacy) {
                bool inFit = prof->InFitnessMode();
                float tmp1, tmp2, cals;
                prof->GetFitnessStats(tmp1, tmp2, cals);
                if (inFit) {
                    String cals_str(perf_calories_str); cals_str += buf;
                    dataP.AddPair(cals_str.c_str(), cals);
                }

                String fitness_mode_str(perf_fitness_mode_str); fitness_mode_str += buf;
                dataP.AddPair(fitness_mode_str.c_str(), (int)inFit);
            }

            for (int p_idx = 0; p_idx < 5; p_idx++) {
                Playlist p(prof->GetPlaylist(p_idx));
                p.GetNumSongs();
            }

            String num_playlists_str(num_playlists_str_base); num_playlists_str += buf;
            dataP.AddPair(num_playlists_str.c_str(), 0);
        }
    }

    if (TheMaster != nullptr) {
        int duration = (int)TheMaster->SongDurationMs();
        static Symbol song_duration_ms("song_duration_ms");
        dataP.AddPair(song_duration_ms, duration);
    }

    dataP.AddPair(photos_disabled, (int)TheProfileMgr.DisablePhotos());
    dataP.AddPair(freestyle_disabled, (int)TheProfileMgr.DisableFreestyle());

    SetDataPoint(dataP);
}

bool GameEndedDataPointJob::CompileMoveRatings(
    String &str, int playerIndex, bool b3
) const {
    MILO_ASSERT(playerIndex >= 0 && playerIndex < MAX_NUM_PLAYERS, 0x120);
    bool ret = false;
    const std::vector<HamMoveScore> &moveScores =
        MetaPerformer::Current()->GetMoveScores(playerIndex);
    if (moveScores.size() != 0) {
        char buffer[56];
        bool b4 = true;
        for (int i = 0; i < moveScores.size(); i++) {
            if (!moveScores[i].mMove->IsRest()) {
                ret = true;
                String moveName = moveScores[i].mMove->Name();
                if (SearchReplace(moveName.c_str(), "&", "%26", buffer)) {
                    moveName = buffer;
                }
                if (!b4) {
                    str += "|";
                }
                if (!b3) {
                    str += MakeString(
                        "%s:%.2f%%20(%s)",
                        moveName,
                        moveScores[i].mDetectFrac,
                        RatingState(moveScores[i].mRatingStateIndex).Str() + 5
                    );
                } else if (moveScores[i].mRatingStateIndex < 0) {
                    const char *speed;
                    if (moveScores[i].mRatingStateIndex == -4) {
                        speed = "fast";
                    } else if (moveScores[i].mRatingStateIndex == -3) {
                        speed = "pass";
                    } else {
                        speed = "fail";
                    }
                    str += MakeString(
                        "%s:%s%%20(slowmo:%d)",
                        moveName,
                        speed,
                        moveScores[i].mSlowMo ? 1 : 0
                    );
                } else {
                    const char *rating;
                    if (moveScores[i].mRatingStateIndex == 0) {
                        rating = "perfect";
                    } else if (moveScores[i].mRatingStateIndex == 1) {
                        rating = "awesome";
                    } else if (moveScores[i].mRatingStateIndex == 3) {
                        rating = "ok";
                    } else {
                        rating = "bad";
                    }
                    str += MakeString(
                        "%s:%.2f%%20(%s)%%20(slowmo:%d)",
                        moveName,
                        moveScores[i].mDetectFrac,
                        rating,
                        moveScores[i].mSlowMo ? 1 : 0
                    );
                }
                b4 = false;
            }
        }
    }
    return ret;
}

OmgScoresJob::OmgScoresJob(Hmx::Object *callback, int p1Score, int p2Score)
    : RCJob("dataminer/omg_scores/", callback) {
    static Symbol player1_score("player1_score");
    static Symbol player2_score("player2_score");
    DataPoint dataP;
    dataP.AddPair(player1_score, p1Score);
    dataP.AddPair(player2_score, p2Score);
    SetDataPoint(dataP);
}

PlayerDroppedInJob::PlayerDroppedInJob(Hmx::Object *callback, int playerIdx)
    : RCJob("dataminer/player_dropped_in/", callback) {
    static Symbol player_idx("player_idx");
    DataPoint dataP;
    dataP.AddPair(player_idx, playerIdx);
    SetDataPoint(dataP);
}

PlayerDroppedOutJob::PlayerDroppedOutJob(Hmx::Object *callback, int playerIdx)
    : RCJob("dataminer/player_dropped_out/", callback) {
    static Symbol player_idx("player_idx");
    DataPoint dataP;
    dataP.AddPair(player_idx, playerIdx);
    SetDataPoint(dataP);
}

ControllerModeJob::ControllerModeJob(Hmx::Object *callback, int enterCount, int exitCount)
    : RCJob("dataminer/controller_mode/", callback) {
    static Symbol enter_count("enter_count");
    static Symbol exit_count("exit_count");
    DataPoint dataP;
    dataP.AddPair(enter_count, enterCount);
    dataP.AddPair(exit_count, exitCount);
    SetDataPoint(dataP);
}

PlaylistChangedJob::PlaylistChangedJob(Hmx::Object *callback, Symbol name, int numSongs)
    : RCJob("dataminer/playlist_changed/", callback) {
    static Symbol playlist_name("playlist_name");
    static Symbol num_songs("num_songs");
    DataPoint dataP;
    dataP.AddPair(playlist_name, name);
    dataP.AddPair(num_songs, numSongs);
    SetDataPoint(dataP);
}

ScreenResJob::ScreenResJob(Hmx::Object *callback, _XVIDEO_MODE *videoMode)
    : RCJob("dataminer/screen_resolution/", callback) {
    DataPoint dataP;
    static Symbol dwDisplayWidth("dwDisplayWidth");
    dataP.AddPair(dwDisplayWidth, videoMode->dwDisplayWidth);
    static Symbol dwDisplayHeight("dwDisplayHeight");
    dataP.AddPair(dwDisplayHeight, videoMode->dwDisplayHeight);
    static Symbol fIsInterlaced("fIsInterlaced");
    dataP.AddPair(fIsInterlaced, videoMode->fIsInterlaced);
    static Symbol fIsWideScreen("fIsWideScreen");
    dataP.AddPair(fIsWideScreen, videoMode->fIsWideScreen);
    static Symbol fIsHiDef("fIsHiDef");
    dataP.AddPair(fIsHiDef, videoMode->fIsHiDef);
    static Symbol RefreshRate("RefreshRate");
    dataP.AddPair(RefreshRate, videoMode->RefreshRate);
    static Symbol VideoStandard("VideoStandard");
    dataP.AddPair(VideoStandard, videoMode->VideoStandard);
    SetDataPoint(dataP);
}
