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
        bool b6;
        if (d2 == kDifficultyBeginner) {
            b6 = true;
        } else if (d == kDifficultyBeginner) {
            b6 = false;
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
            } else if (sbc == flawless_a || sbc == flawless_b) {
                i5 = progress.GetFlawlessMoveCount();
            } else if (sbc == nices_a || sbc == nices_b) {
                i5 = progress.GetNiceMoveCount();
            } else if (sbc == days) {
                i5 = progress.NumDays();
            } else if (sbc == weekends) {
                i5 = progress.NumWeekends();
            } else if (sbc == hardest_stars) {
                static Symbol omg("omg");
                if (s == omg) {
                    static Symbol stars_earned("stars_earned");
                    const DataNode *pStarsNode =
                        TheHamProvider->Property(stars_earned, false);
                    MILO_ASSERT(pStarsNode, 0x14C);
                    i5 = pStarsNode->Int();
                } else
                    continue;
            } else {
                MILO_NOTIFY("Condition is not currently supported: %s ", sbc);
                return false;
            }
            if (i5 >= i3) {
                return true;
            }
        }
    }
    return false;
}

void AccomplishmentOneShot::Configure(DataArray *i_pConfig) {
    MILO_ASSERT(i_pConfig, 0x23);
}
