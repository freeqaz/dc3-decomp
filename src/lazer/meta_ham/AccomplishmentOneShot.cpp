#include "meta_ham/AccomplishmentOneShot.h"
#include "AccomplishmentOneShot.h"
#include "flow/PropertyEventProvider.h"
#include "hamobj/Difficulty.h"
#include "hamobj/HamPlayerData.h"
#include "meta_ham/AccomplishmentConditional.h"
#include "meta_ham/Accomplishment.h"
#include "meta_ham/AccomplishmentProgress.h"
#include "meta_ham/HamProfile.h"
#include "obj/Data.h"
#include "os/Debug.h"
#include "utl/Symbol.h"

AccomplishmentOneShot::AccomplishmentOneShot(DataArray *d, int i)
    : AccomplishmentConditional(d, i) {
    Configure(d);
}

AccomplishmentOneShot::~AccomplishmentOneShot() {}

bool AccomplishmentOneShot::AreOneShotConditionsMet(
    HamPlayerData *hpd, HamProfile *profile, Symbol s, Difficulty d
) {
    static Symbol stars("stars");
    static Symbol flawless_a("flawless_a");
    static Symbol flawless_b("flawless_b");
    static Symbol nices_a("nices_a");
    static Symbol nices_b("nices_b");
    static Symbol days("days");
    static Symbol weekends("weekends");
    static Symbol hardest_stars("hardest_stars");
    const AccomplishmentProgress &progress = profile->GetAccomplishmentProgress();
    FOREACH (it, m_lConditions) {
        Symbol sbc = it->mConditionType;
        Difficulty d2 = it->mDifficulty;
        int i3 = it->mCount;
        unsigned char b6;
        if (d2 == kDifficultyBeginner) {
            b6 = 1;
        } else if (d == kDifficultyBeginner) {
            b6 = 0;
        } else {
            b6 = d2 <= d;
        }
        if (b6) {
            int i5;
            if (sbc == stars) {
                static Symbol stars_earned("stars_earned");
                const DataNode *pStarsNode =
                    TheHamProvider->Property(stars_earned, false);
                MILO_ASSERT(pStarsNode, 0x112);
                i5 = pStarsNode->Int();
                if (i5 >= i3) return true;
            } else if (sbc == flawless_a) {
                if (progress.GetFlawlessMoveCount() >= i3) return true;
            } else if (sbc == flawless_b) {
                if (progress.GetFlawlessMoveCount() >= i3) return true;
            } else if (sbc == nices_a) {
                if (progress.GetNiceMoveCount() >= i3) return true;
            } else if (sbc == nices_b) {
                if (progress.GetNiceMoveCount() >= i3) return true;
            } else if (sbc == days) {
                if (progress.NumDays() >= i3) return true;
            } else if (sbc == weekends) {
                if (progress.NumWeekends() >= i3) return true;
            } else if (sbc == hardest_stars) {
                static Symbol omg("omg");
                if (s == omg) {
                    static Symbol stars_earned("stars_earned");
                    const DataNode *pStarsNode =
                        TheHamProvider->Property(stars_earned, false);
                    MILO_ASSERT(pStarsNode, 0x14C);
                    i5 = pStarsNode->Int();
                    if (i5 >= i3) return true;
                } else
                    continue;
            } else {
                MILO_NOTIFY("Condition is not currently supported: %s ", sbc);
                return false;
            }
        }
    }
    return false;
}

void AccomplishmentOneShot::Configure(DataArray *i_pConfig) {
    MILO_ASSERT(i_pConfig, 0x23);
}
