#include "meta_ham/MetagameRank.h"
#include "flow/PropertyEventProvider.h"
#include "game/GameMode.h"
#include "game/GamePanel.h"
#include "hamobj/Difficulty.h"
#include "hamobj/HamDirector.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamPlayerData.h"
#include "hamobj/PoseFatalities.h"
#include "math/Rand.h"
#include "meta/FixedSizeSaveableStream.h"
#include "meta_ham/AccomplishmentManager.h"
#include "meta_ham/Campaign.h"
#include "meta_ham/CampaignEra.h"
#include "meta_ham/CampaignPerformer.h"
#include "meta_ham/Challenges.h"
#include "meta_ham/HamProfile.h"
#include "meta_ham/HamSongMetadata.h"
#include "meta_ham/HamSongMgr.h"
#include "meta_ham/MetaPerformer.h"
#include "meta_ham/SongStatusMgr.h"
#include "net_ham/RockCentral.h"
#include "obj/Data.h"
#include "obj/DataFunc.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/DateTime.h"
#include "os/Debug.h"
#include "os/System.h"
#include "utl/Symbol.h"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace {
    DataArray *gRanksArray;
    DataArray *gRepeatableTasks;
    DataArray *gOneTimeTasks;

    // size 0x14
    struct DeferredAward {
        String unk0;
        Symbol unk8;
        Symbol unkc;
        Symbol unk10;
    };

    // size 0x20
    struct Unlockable {
        int unk0;
        Symbol unk4;
        Symbol unk8;
        Symbol unkc;
        Symbol unk10;
        std::vector<Symbol> unk14;
    };
    std::vector<Unlockable> gUnlockables;
    std::vector<std::vector<Unlockable *>> gTiers;
    std::list<DeferredAward> gDeferredAwardQueue;
}

MetagameRank::MetagameRank(HamProfile *p) : mProfile(p) {
    Clear();
    mSaveSizeMethod = SaveSize;
}

BEGIN_HANDLERS(MetagameRank)
    HANDLE_EXPR(get_score, mScore)
    HANDLE_EXPR(get_rank_number, mRankNumber)
    HANDLE_EXPR(get_rank_in_tier, GetRankInTier())
    HANDLE_EXPR(get_tier, GetTier())
    HANDLE_EXPR(get_xp_of_rank, GetXPOfRank(_msg->Int(2)))
    HANDLE_EXPR(has_new_rank, HasNewRank())
    HANDLE_EXPR(at_max_rank, mAtMaxRank)
    HANDLE_EXPR(get_percent_to_next_rank, mPctToNextRank)
    HANDLE_ACTION(award_points, AwardPointsForTask(_msg->Sym(2)))
    HANDLE_EXPR(have_deferred_points, mDeferredPoints.size() > 0)
    HANDLE(get_next_deferred_points, GetNextDeferredPoints)
END_HANDLERS

bool MetagameRank::HasNewRank() const {
    if (!mAtMaxRank) {
        return mPctToNextRank == 1;
    } else {
        return mHasNewRank;
    }
}

void MetagameRank::SaveFixed(FixedSizeSaveableStream &fs) const {
    fs << mScore;
    bool b1 = mFirstTimePlayed;
    if (!b1) {
        static Symbol play_first_time_disp("play_first_time_disp");
        FOREACH (it, mDeferredPoints) {
            if (it->mSource == play_first_time_disp) {
                b1 = true;
                break;
            }
        }
    }
    fs << b1;
    fs << mAtMaxRank;
    fs.Write(mOneTimeTaskFlags, 0x40);
    fs.Write(unk79, 0x40);
    static Symbol combined_xp_disp("combined_xp_disp");
    int sum;
    if (mDeferredPoints.size() != 0) {
        sum = 0;
        FOREACH (it, mDeferredPoints) {
            sum += it->mPoints;
        }
    } else {
        sum = 0;
    }
    SaveSymbolID(fs, combined_xp_disp);
    fs << sum;
    const_cast<MetagameRank *>(this)->mXpAwarded = false;
}

void MetagameRank::LoadFixed(FixedSizeSaveableStream &fs, int saveVersion) {
    fs >> mScore;
    // Version 0x46+: Added first-time play tracking flag
    if (saveVersion > 0x45) {
        fs >> mFirstTimePlayed;
    }
    // Version 0x4F+: Added max rank flag
    if (saveVersion > 0x4E) {
        fs >> mAtMaxRank;
    }
    fs.Read(mOneTimeTaskFlags, 0x40);
    // Clear play_first_time task if first-time flag was set
    if (mFirstTimePlayed) {
        int idx = -1;
        static Symbol play_first_time("play_first_time");
        GetOneTimeTask(play_first_time, nullptr, &idx);
        if (idx >= 0) {
            mOneTimeTaskFlags[idx] = 0;
        }
    }
    fs.Read(unk79, 0x40);
    // Version 0x3E-0x5A: Read and discard obsolete data
    if (saveVersion > 0x3D) {
        if (saveVersion <= 0x5A) {
            int x;
            fs >> x;
        }
    }
    // Version 0x5B+: Load combined XP from deferred points
    if (saveVersion > 0x5A) {
        DeferredPoints pt;
        LoadSymbolFromID(fs, pt.mSource);
        fs >> pt.mPoints;
        if (pt.mPoints > 0) {
            // Insert at front to restore exactly what was saved
            mDeferredPoints.insert(mDeferredPoints.begin(), pt);
        }
    }
    ComputeRankNumber(true);
    mXpAwarded = false;
}

void MetagameRank::Preinit() {
    DataArray *rankCfg = SystemConfig("rank");
    gRanksArray = rankCfg->FindArray("ranks");
}

DataNode HaveDeferredAward(DataArray *) { return !gDeferredAwardQueue.empty(); }
DataNode HandleDeferredAward(DataArray *) {
    if (gDeferredAwardQueue.empty()) {
        return 0;
    } else {
        DeferredAward award = gDeferredAwardQueue.front();
        gDeferredAwardQueue.pop_front();
        DataArrayPtr ptr(Symbol(award.unk0.c_str()), award.unk8, award.unkc, award.unk10);
        return ptr;
    }
}

void MetagameRank::Init() {
    static DataNode &xp_force_award_small = DataVariable("xp_force_award_small");
    static DataNode &xp_force_award_one_time = DataVariable("xp_force_award_one_time");
    static DataNode &xp_force_award_medium = DataVariable("xp_force_award_medium");
    static DataNode &xp_force_award_large = DataVariable("xp_force_award_large");
    static DataNode &xp_force_award_all = DataVariable("xp_force_award_all");
    static DataNode &xp_force_one_rank_up = DataVariable("xp_force_one_rank_up");
    xp_force_award_small = 0;
    xp_force_award_medium = 0;
    xp_force_award_large = 0;
    xp_force_award_one_time = 0;
    xp_force_award_all = 0;
    xp_force_one_rank_up = 0;
    DataRegisterFunc("xp_have_deferred_award", HaveDeferredAward);
    DataRegisterFunc("xp_deferred_award", HandleDeferredAward);
    int unlockablesSize = 0;
    DataArray *rankCfg = SystemConfig("rank");
    DataArray *unlockArr = rankCfg->FindArray("unlockables");
    if (unlockArr) {
        auto unlockablesArrSize = unlockArr->Size();
        unlockablesSize = unlockablesArrSize - 1;
        gUnlockables.resize(unlockablesSize);
        for (int i = 0; i < unlockablesSize; i++) {
            DataArray *curUnlockArray = unlockArr->Array(i + 1);
            Unlockable &cur = gUnlockables[i];
            cur.unk0 = i + 1;
            cur.unk4 = curUnlockArray->Sym(0);
            cur.unk8 = curUnlockArray->FindSym("name");
            cur.unkc = curUnlockArray->FindSym("desc");
            auto imageSym = curUnlockArray->FindSym("image");
            cur.unk10 = imageSym;
            DataArray *unlocksToPopulate = curUnlockArray->FindArray("unlock");
            cur.unk14.resize(unlocksToPopulate->Size() - 1);
            for (int j = 1; j < unlocksToPopulate->Size(); j++) {
                cur.unk14[j - 1] = unlocksToPopulate->Sym(j);
                TheAccomplishmentMgr->AddAssetAward(cur.unk14[j - 1], cur.unk4);
            }
        }
    }
    DataArray *tierArr = rankCfg->FindArray("tiers");
    if (tierArr) {
        auto tierArrSize = tierArr->Size();
        int tiersSize = tierArrSize - 1;
        gTiers.resize(tiersSize);
        for (int i = 0; i < tiersSize; i++) {
            DataArray *innerTierArr = tierArr->Array(i + 1);
            int innerSize = innerTierArr->Size();
            gTiers[i].reserve(innerSize);
            for (int j = 0; j < innerSize; j++) {
                bool found = false;
                Symbol unlockSym = innerTierArr->Sym(j);
                int k;
                for (k = 0; k < unlockablesSize; k++) {
                    if (unlockSym == gUnlockables[k].unk4) {
                        gTiers[i].push_back(&gUnlockables[k]);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    TheDebug.Fail(MakeString("Unlock named %s not found in unlock list", unlockSym), 0);
                }
            }
        }
    }
    DataArray *taskArr = rankCfg->FindArray("tasks");
    gOneTimeTasks = rankCfg->FindArray("one_time");
    gRepeatableTasks = taskArr->FindArray("repeatable");
}

void MetagameRank::Clear() {
    mXpAwarded = false;
    mScore = 0;
    mFirstTimePlayed = true;
    memset(mOneTimeTaskFlags, 0, 0x40);
    memset(unk79, 0, 0x40);
    mDeferredPoints.clear();
    mRankNumber = 0;
    mAtMaxRank = false;
    mHasNewRank = false;
    mPctToNextRank = 0;
    ComputeRankNumber(true);
}

Symbol MetagameRank::GetRankTitle() const {
    char buf[32];
    sprintf(buf, "rank_%d", mRankNumber);
    return buf;
}

// Search gOneTimeTasks for a task with the given symbol name.
// If found, optionally return the DataArray* and/or the task index.
// Returns true if found, false otherwise.
bool MetagameRank::GetOneTimeTask(Symbol s, DataArray **aptr, int *iptr) {
    // Early return if no output parameters requested
    if (!aptr && !iptr) {
        return false;
    }

    // Search through one-time tasks array
    short size = gOneTimeTasks->Size();
    int i = 1; // Start at 1 to skip the array name
    while (i < size) {
        DataNode &n = gOneTimeTasks->Node(i);
        if (n.Type() == kDataArray && n.Array()->Sym(0) == s) {
            // Found matching task
            if (aptr) {
                *aptr = n.Array();
            }
            if (iptr) {
                *iptr = i - 1; // Return 0-based index
            }
            return true;
        }
        i++;
    }

    // Not found - set output parameters to default values
    if (aptr) {
        *aptr = nullptr;
    }
    if (iptr) {
        *iptr = -1;
    }
    return false;
}

int MetagameRank::GetXPOfRank(int i) const {
    int ret = 0;
    if (i > 0 && i < gRanksArray->Size() - 1) {
        ret = gRanksArray->Array(i + 1)->Int(0);
        if (i >= 1) {
            ret -= gRanksArray->Array(i)->Int(0);
        }
    }
    return ret;
}

DataNode MetagameRank::GetNextDeferredPoints(DataArray *a) {
    if (mDeferredPoints.empty()) {
        static Symbol xp_previous_points_msg("xp_previous_points_msg");
        return xp_previous_points_msg;
    } else {
        DeferredPoints pt = mDeferredPoints.front();
        mDeferredPoints.pop_front();
        mScore += pt.mPoints;
        ComputeRankNumber(false);
        mXpAwarded = true;
        DataArrayPtr ptr(pt.mSource, pt.mPoints);
        return ptr;
    }
}

extern PropertyEventProvider *TheHamProvider;

bool compare_deferred_points(DeferredPoints a, DeferredPoints b);

void MetagameRank::UpdateScore(
    int songID,
    const HamPlayerData *playerData,
    const SongStatusMgr *statusMgr,
    int stars,
    int unk
) {
    // Check if in party mode - early return (NOT static - constructed each call)
    auto isPartyMode = TheHamProvider->Property(Symbol("is_in_party_mode"), true)->Int(0);
    if (isPartyMode) {
        return;
    }

    if (TheHamProvider->Property(Symbol("is_in_infinite_party_mode"), true)->Int(0)) {
        return;
    }

    // Static symbols - order matches guard counter allocation (Ghidra verified)
    // Guard word 1 (bits 0-31)
    static Symbol new_song_completed_on_hard("new_song_completed_on_hard");
    static Symbol completed_song_moderate("completed_song_moderate");
    static Symbol completed_song_on_beginner("completed_song_on_beginner");
    static Symbol new_song_completed_on_beginner("new_song_completed_on_beginner");
    static Symbol new_song_completed_on_medium("new_song_completed_on_medium");
    static Symbol completed_song_hardcore("completed_song_hardcore");
    static Symbol bodie_birthday("bodie_birthday");
    static Symbol nail_fatality("nail_fatality");
    static Symbol challenge_met("challenge_met");
    static Symbol completed_song_on_hard("completed_song_on_hard");
    static Symbol random_bonus_occurs_1pct_of_the_time(
        "random_bonus_occurs_1pct_of_the_time"
    );
    static Symbol fitness_bonus("fitness_bonus");
    static Symbol completed_song_on_easy("completed_song_on_easy");
    static Symbol completed_song_on_medium("completed_song_on_medium");
    static Symbol double_xp_weekend("double_xp_weekend");
    static Symbol completed_song_warmup("completed_song_warmup");
    static Symbol completed_song_with_4_stars("completed_song_with_4_stars");
    static Symbol completed_song_tough("completed_song_tough");
    static Symbol completed_song_simple("completed_song_simple");
    static Symbol completed_song_with_1_star("completed_song_with_1_star");
    static Symbol completed_song_legit("completed_song_legit");
    static Symbol completed_song_with_2_stars("completed_song_with_2_stars");
    static Symbol perfect_performance_no_misses("perfect_performance_no_misses");
    static Symbol playlist_bonus("playlist_bonus");
    static Symbol new_song_completed_on_easy("new_song_completed_on_easy");
    static Symbol challenge_attempt("challenge_attempt");
    static Symbol dlc_bonus("dlc_bonus");
    static Symbol completed_song_with_5_stars("completed_song_with_5_stars");
    static Symbol completed_song_with_3_stars("completed_song_with_3_stars");
    static Symbol golden_performance("golden_performance");
    static Symbol completed_song_off_the_hook("completed_song_off_the_hook");
    static Symbol emilia_birthday("emilia_birthday");
    // Guard word 2 (bits 0-30)
    static Symbol taye_birthday("taye_birthday");
    static Symbol lilt_birthday("lilt_birthday");
    static Symbol angel_birthday("angel_birthday");
    static Symbol aubrey_birthday("aubrey_birthday");
    static Symbol mo_birthday("mo_birthday");
    static Symbol glitch_birthday("glitch_birthday");
    static Symbol dare_birthday("dare_birthday");
    static Symbol maccoy_birthday("maccoy_birthday");
    static Symbol oblio_birthday("oblio_birthday");
    static Symbol kerith_birthday("kerith_birthday");
    static Symbol jaryn_birthday("jaryn_birthday");
    static Symbol rasa_birthday("rasa_birthday");
    static Symbol lima_birthday("lima_birthday");
    static Symbol robota_birthday("robota_birthday");
    static Symbol robotb_birthday("robotb_birthday");
    static Symbol tan_birthday("tan_birthday");
    static Symbol tanrobot_birthday("tanrobot_birthday");
    static Symbol ninjaman_birthday("ninjaman_birthday");
    static Symbol ninjawoman_birthday("ninjawoman_birthday");
    static Symbol iconmanblue_birthday("iconmanblue_birthday");
    static Symbol iconmanpink_birthday("iconmanpink_birthday");
    static Symbol play_first_time("play_first_time");
    static Symbol new_era_completed_campaign_70s("new_era_completed_campaign_70s");
    static Symbol new_era_completed_campaign_80s("new_era_completed_campaign_80s");
    static Symbol new_era_completed_campaign_90s("new_era_completed_campaign_90s");
    static Symbol new_era_completed_campaign_00s("new_era_completed_campaign_00s");
    static Symbol new_era_completed_campaign_10s("new_era_completed_campaign_10s");
    static Symbol campaign_completed_on_easy_3("campaign_completed_on_easy_3");
    static Symbol campaign_completed_on_medium("campaign_completed_on_medium");
    static Symbol campaign_completed_on_hard("campaign_completed_on_hard");
    static Symbol five_star_a_characters_songlist("five_star_a_characters_songlist");

    // Handle first time play bonus
    if (mFirstTimePlayed) {
        mFirstTimePlayed = false;
        AwardPointsForTask(play_first_time);
    }

    // Debug force award variables
    static DataNode &xp_force_award_small = DataVariable("xp_force_award_small");
    static DataNode &xp_force_award_medium = DataVariable("xp_force_award_medium");
    static DataNode &xp_force_award_large = DataVariable("xp_force_award_large");
    static DataNode &xp_force_award_one_time = DataVariable("xp_force_award_one_time");
    static DataNode &xp_force_award_all = DataVariable("xp_force_award_all");
    static DataNode &xp_force_one_rank_up = DataVariable("xp_force_one_rank_up");

    // Handle force award small
    if (xp_force_award_small.Int(0)) {
        static Symbol smallTasks[] = { new_song_completed_on_beginner,
                                       new_song_completed_on_easy,
                                       new_song_completed_on_medium,
                                       new_song_completed_on_hard,
                                       maccoy_birthday,
                                       dare_birthday,
                                       glitch_birthday,
                                       completed_song_moderate,
                                       completed_song_tough,
                                       completed_song_legit,
                                       completed_song_hardcore,
                                       completed_song_off_the_hook,
                                       random_bonus_occurs_1pct_of_the_time,
                                       challenge_attempt,
                                       nail_fatality,
                                       perfect_performance_no_misses,
                                       emilia_birthday,
                                       completed_song_with_1_star,
                                       completed_song_with_2_stars,
                                       completed_song_with_3_stars,
                                       completed_song_with_4_stars,
                                       completed_song_with_5_stars,
                                       completed_song_on_beginner,
                                       completed_song_on_easy,
                                       completed_song_on_medium,
                                       completed_song_on_hard,
                                       golden_performance,
                                       completed_song_warmup,
                                       completed_song_simple,
                                       bodie_birthday,
                                       taye_birthday,
                                       lilt_birthday,
                                       angel_birthday,
                                       aubrey_birthday,
                                       mo_birthday,
                                       dlc_bonus,
                                       challenge_met,
                                       fitness_bonus,
                                       playlist_bonus,
                                       oblio_birthday,
                                       kerith_birthday };
        int idx = RandomInt(0, 0x29);
        Symbol task = smallTasks[idx];
        TheDebug << MakeString("XP Forcing Small Task: %s\n", task);
        AwardPointsForTask(task);
    }

    // Handle force award medium
    if (xp_force_award_medium.Int(0)) {
        static Symbol mediumTasks[] = {
            completed_song_with_2_stars,    completed_song_on_hard,
            completed_song_off_the_hook,    completed_song_with_3_stars,
            completed_song_with_4_stars,    new_era_completed_campaign_70s,
            new_era_completed_campaign_80s, new_era_completed_campaign_90s,
            new_era_completed_campaign_00s, new_era_completed_campaign_10s,
            campaign_completed_on_easy_3
        };
        int idx = RandomInt(0, 0xb);
        Symbol task = mediumTasks[idx];
        AwardPointsForTask(task);
        TheDebug << MakeString("XP Forcing Medium Task: %s\n", task);
    }

    // Handle force award large
    if (xp_force_award_large.Int(0)) {
        static Symbol largeTasks[] = { completed_song_with_5_stars,
                                       campaign_completed_on_medium,
                                       campaign_completed_on_hard,
                                       five_star_a_characters_songlist };
        int idx = RandomInt(0, 0x4);
        Symbol task = largeTasks[idx];
        TheDebug << MakeString("XP Forcing Large Task: %s\n", task);
        AwardPointsForTask(task);
    }

    // Handle force award one time
    if (xp_force_award_one_time.Int(0)) {
        static Symbol oneTimeTasks[] = { jaryn_birthday,
                                         new_era_completed_campaign_70s,
                                         new_era_completed_campaign_80s,
                                         new_era_completed_campaign_90s,
                                         new_era_completed_campaign_00s,
                                         new_era_completed_campaign_10s,
                                         campaign_completed_on_easy_3,
                                         campaign_completed_on_medium,
                                         campaign_completed_on_hard,
                                         five_star_a_characters_songlist };
        bool awarded = false;
        for (int i = 0; (unsigned int)i < 10; i++) {
            Symbol task = oneTimeTasks[i];
            int task_index = -1;
            if (GetOneTimeTask(task, nullptr, &task_index)) {
                MILO_ASSERT(task_index >= 0 && task_index < 64, 0x36F);
                if (!mOneTimeTaskFlags[task_index]) {
                    TheDebug << MakeString("XP Forcing One-Time Task: %s\n", task);
                    AwardPointsForTask(task);
                    awarded = true;
                    break;
                }
            }
        }
        if (!awarded) {
            TheDebug
                << MakeString("XP Forcing One-Time Task: ALL ONE-TIME TASKS HAVE BEEN COMPLETED\n");
        }
    }

    // Skip normal scoring if any force award is active
    if (xp_force_award_small.Int(0) || xp_force_award_medium.Int(0)
        || xp_force_award_large.Int(0) || xp_force_award_one_time.Int(0)) {
        return;
    }

    // Force one rank up
    if (xp_force_one_rank_up.Int(0)) {
        TheDebug << MakeString("XP Forcing One Rank Up\n");
        static Symbol played_1000_songs_disp("played_1000_songs_disp");
        AwardPoints(GetXPOfRank(mRankNumber), played_1000_songs_disp);
        return;
    }

    if (xp_force_award_all.Int(0)) {
        // ======== Force award all ranks ========
        TheDebug << MakeString("XP Forcing Awarding All Ranks\n");
        float mult = 1.0f;
        if (TheRockCentral.GetMotdXPFlag()) {
            mult = 0.5f;
        }
        double dmult = (double)mult;
        static Symbol played_1000_songs_disp2("played_1000_songs_disp");
        for (int i = mRankNumber; i < 0x41; i++) {
            int xp = GetXPOfRank(i);
            AwardPoints((int)((double)(long long)xp * dmult), played_1000_songs_disp2);
        }
        xp_force_award_all = DataNode(0);
    } else {
        // ======== Normal scoring ========

        // Random 1% bonus
        if (RandomInt(0, 100) == 0x2a) {
            AwardPointsForTask(random_bonus_occurs_1pct_of_the_time);
        }

        // Double XP weekend
        if (TheRockCentral.GetMotdXPFlag()) {
            AwardPointsForTask(double_xp_weekend);
        }

        // Star-based awards
        if (unk > 5) {
            AwardPointsForTask(completed_song_with_5_stars);
            AwardPointsForTask(golden_performance);
        }
        switch (unk) {
        case 1: AwardPointsForTask(completed_song_with_1_star); break;
        case 2: AwardPointsForTask(completed_song_with_2_stars); break;
        case 3: AwardPointsForTask(completed_song_with_3_stars); break;
        case 4: AwardPointsForTask(completed_song_with_4_stars); break;
        case 5: AwardPointsForTask(completed_song_with_5_stars); break;
        }

        // Difficulty-based awards
        HamPlayerData *player = TheGameData->Player(playerData->PlayerIndex());
        Difficulty diff = player->GetDifficulty();
        switch ((unsigned int)diff) {
        case kDifficultyEasy: AwardPointsForTask(completed_song_on_easy); break;
        case kDifficultyMedium: AwardPointsForTask(completed_song_on_medium); break;
        case kDifficultyExpert: AwardPointsForTask(completed_song_on_hard); break;
        case kDifficultyBeginner: AwardPointsForTask(completed_song_on_beginner); break;
        }

        // Song metadata-based awards
        const HamSongMetadata *songData = TheHamSongMgr.Data(songID);
        if (songData) {
            // Rank tier awards
            float rank = songData->Rank();
            int tier = TheHamSongMgr.RankTier((int)rank);
            switch ((unsigned int)tier) {
            case 0: AwardPointsForTask(completed_song_warmup); break;
            case 1: AwardPointsForTask(completed_song_simple); break;
            case 2: AwardPointsForTask(completed_song_moderate); break;
            case 3: AwardPointsForTask(completed_song_tough); break;
            case 4: AwardPointsForTask(completed_song_legit); break;
            case 5: AwardPointsForTask(completed_song_hardcore); break;
            case 6: AwardPointsForTask(completed_song_off_the_hook); break;
            }

            // Validate both players have data and providers
            for (int i = 0; i < 2; i++) {
                HamPlayerData *player_data = TheGameData->Player(i);
                MILO_ASSERT(player_data, 0x3E0);
                PropertyEventProvider *player_provider = player_data->Provider();
                MILO_ASSERT(player_provider, 0x3E3);
            }

            // Character star tracking
            int starsEarned =
                TheHamProvider->Property(Symbol("stars_earned"), false)->Int(0);
            if (starsEarned > 4) {
                Symbol character = songData->Character();
                SongStatusMgr *ssm = mProfile->GetSongStatusMgr();
                bool placeholder = false;
                int existingStars = ssm->GetStars(songID, placeholder);
                if (existingStars > 5) {
                    existingStars = 5;
                } else {
                    existingStars = existingStars >= 0 ? existingStars : 0;
                }
                if (starsEarned > 5) {
                    starsEarned = 5;
                } else {
                    starsEarned = starsEarned >= 0 ? starsEarned : 0;
                }
                int charStarsEarned = 0;
                int charStarsRequired = 0;
                TheHamSongMgr.GetCharacterStars(
                    mProfile, character, charStarsEarned, charStarsRequired
                );
                charStarsEarned += (starsEarned - existingStars);
                if (charStarsRequired >= 0 && charStarsEarned >= charStarsRequired) {
                    AwardPointsForTask(five_star_a_characters_songlist);
                }
            }

            // DLC bonus
            if (songData->IsDownload()) {
                AwardPointsForTask(dlc_bonus);
            }

            // Campaign mode
            static Symbol is_in_campaign_mode("is_in_campaign_mode");
            if (TheHamProvider->Property(is_in_campaign_mode, true)->Int(0)) {
                MetaPerformer *perf = MetaPerformer::Current();
                CampaignPerformer *campaignPerf =
                    dynamic_cast<CampaignPerformer *>(perf);
                if (campaignPerf) {
                    CampaignEra *era =
                        TheCampaign->GetCampaignEra(campaignPerf->Era());
                    Symbol songShortName =
                        TheHamSongMgr.GetShortNameFromSongID(songID);
                    Symbol danceCrazeSong = era->GetDanceCrazeSong();
                    Symbol eraName = era->GetName();

                    static Symbol era01("era01");
                    static Symbol era02("era02");
                    static Symbol era03("era03");
                    static Symbol era04("era04");
                    static Symbol era05("era05");
                    static Symbol era_tan_battle("era_tan_battle");

                    if (songShortName == danceCrazeSong) {
                        if (eraName == era01) {
                            AwardPointsForTask(new_era_completed_campaign_70s);
                        } else if (eraName == era02) {
                            AwardPointsForTask(new_era_completed_campaign_80s);
                        } else if (eraName == era03) {
                            AwardPointsForTask(new_era_completed_campaign_90s);
                        } else if (eraName == era04) {
                            AwardPointsForTask(new_era_completed_campaign_00s);
                        } else if (eraName == era05) {
                            AwardPointsForTask(new_era_completed_campaign_10s);
                        }
                    } else {
                        if (eraName == era_tan_battle) {
                            HamPlayerData *p =
                                TheGameData->Player(playerData->PlayerIndex());
                            Difficulty d = p->GetDifficulty();
                            switch ((unsigned int)d) {
                            case kDifficultyEasy: AwardPointsForTask(campaign_completed_on_easy_3); break;
                            case kDifficultyMedium: AwardPointsForTask(campaign_completed_on_medium); break;
                            case kDifficultyExpert: AwardPointsForTask(campaign_completed_on_hard); break;
                            }
                        }
                    }
                }
            }
        }

        // New song / harder difficulty awards
        HamPlayerData *curPlayer = TheGameData->Player(playerData->PlayerIndex());
        Difficulty curDiff = curPlayer->GetDifficulty();
        bool songPlayed = statusMgr->IsSongPlayed(songID);
        if (songPlayed) {
            Difficulty prevDiff = statusMgr->GetDifficulty(songID);
            if (IsHarderDifficulty(curDiff, prevDiff)) {
                goto awardNewSong;
            }
        } else {
        awardNewSong:
            switch ((unsigned int)curDiff) {
            case kDifficultyEasy: AwardPointsForTask(new_song_completed_on_easy); break;
            case kDifficultyMedium: AwardPointsForTask(new_song_completed_on_medium); break;
            case kDifficultyExpert: AwardPointsForTask(new_song_completed_on_hard); break;
            case kDifficultyBeginner: AwardPointsForTask(new_song_completed_on_beginner); break;
            }
        }

        // Fitness bonus
        if (mProfile && mProfile->InFitnessMode()) {
            AwardPointsForTask(fitness_bonus);
        }

        // Playlist bonus
        if (TheGameMode->InMode(Symbol("playlist_perform"), true)) {
            AwardPointsForTask(playlist_bonus);
        }

        // Num rated measures / perfect performance check
        {
            Message numRatedMsg("num_rated_measures");
            DataNode result = TheGamePanel->Handle(numRatedMsg, false);

            if (result.Type() != kDataUnhandled) {
                PropertyEventProvider *provider = playerData->Provider();
                int numPerfect =
                    provider->Property(Symbol("num_perfect"), false)->Int(0);
                int numRated = result.Int(0);
                if (numPerfect == numRated && numPerfect > 0) {
                    AwardPointsForTask(perfect_performance_no_misses);
                }
            }
        }

        // Birthday checks
        {
            Symbol characters[] = {
                Symbol("emilia"),      Symbol("bodie"),       Symbol("taye"),
                Symbol("lilt"),        Symbol("angel"),       Symbol("aubrey"),
                Symbol("mo"),          Symbol("glitch"),      Symbol("dare"),
                Symbol("maccoy"),      Symbol("oblio"),       Symbol("kerith"),
                Symbol("jaryn"),       Symbol("rasa"),        Symbol("lima"),
                Symbol("robota"),      Symbol("robotb"),      Symbol("tan"),
                Symbol("tanrobot"),    Symbol("ninjaman"),    Symbol("ninjawoman"),
                Symbol("iconmanblue"), Symbol("iconmanpink")
            };

            DateTime dt;
            GetDateAndTime(dt);

            static Symbol birthdaySym("birthday");
            DataArray *bdayConfig = SystemConfig(birthdaySym);
            for (int i = 0; i < 23; i++) {
                if (playerData->Char() == characters[i]) {
                    DataArray *charBday =
                        bdayConfig->FindArray(birthdaySym, true)
                            ->FindArray(characters[i], true);
                    int bdayMonth = charBday->Int(1);
                    if (bdayMonth == dt.mMonth + 1) {
                        int bdayDay = charBday->Int(2);
                        if (bdayDay == dt.mDay) {
                            char buf[256];
                            sprintf(buf, "%s_birthday", characters[i].Str());
                            AwardPointsForTask(Symbol(buf));
                        }
                    }
                }
            }
        }

        // Challenge mode
        {
            static Symbol challenge("challenge");
            static Symbol challenge_met_disp("challenge_met_disp");
            if (TheGameMode->InMode(challenge, true)) {
                std::vector<int> xps;
                if (TheChallenges->GetBeatenChallengeXPs(playerData, stars, xps)) {
                    int numXPs = xps.size();
                    if (numXPs == 0) {
                        AwardPointsForTask(challenge_attempt);
                    } else {
                        for (unsigned int j = 0; j < xps.size(); j++) {
                            AwardPoints(xps[j], challenge_met_disp);
                            if (mProfile) {
                                mProfile->IncrementChallengesMet();
                            }
                        }
                    }
                }
            }
        }

        // Nail fatality / full combo
        if (!TheGameMode->InMode(Symbol("strike_a_pose"), true)) {
            PoseFatalities *poseFat = TheHamDirector->GetPoseFatalities();
            if (poseFat) {
                if (poseFat->GotFullCombo(playerData->PlayerIndex())) {
                    AwardPointsForTask(nail_fatality);
                }
            }
        }

        mDeferredPoints.sort(compare_deferred_points);
    }
}

void MetagameRank::AwardPoints(int i, Symbol s) {
    if (TheRockCentral.GetMotdXPFlag()) {
        i = i << 1;
    }
    DeferredPoints df;
    df.mPoints = i;
    df.mSource = s;
    mDeferredPoints.push_back(df);
    mXpAwarded = true;
}

void MetagameRank::AwardPointsForTask(Symbol task) {
    static Symbol score("score");
    static Symbol display("display");
    DataArray *taskArray = gRepeatableTasks->FindArray(task, false);
    if (!taskArray) {
        int task_index = -1;
        bool oneTimeTask = GetOneTimeTask(task, &taskArray, &task_index);
        if (!oneTimeTask) {
            MILO_FAIL("Task %s not found in metagame_rank.dta", task_index);
        }

        MILO_ASSERT(task_index, 0x19b); // change later
        if (!oneTimeTask) {
            return;
        }
        if (mOneTimeTaskFlags[0] != 0) {
            return;
        }
        mOneTimeTaskFlags[0] = 1;
    }

    int scoreNum = taskArray->FindArray(score)->Int(1);
    Symbol disp = taskArray->FindArray(display)->Sym(1);
    if (0 <= scoreNum) {
        AwardPoints(scoreNum, disp);
    }
}
