#include "meta_ham\AccomplishmentOneShot.h"
#include "AccomplishmentOneShot.h"
#include "flow\PropertyEventProvider.h"
#include "hamobj\Difficulty.h"
#include "hamobj\HamPlayerData.h"
#include "meta_ham\AccomplishmentConditional.h"
#include "meta_ham\Accomplishment.h"
#include "meta_ham\AccomplishmentProgress.h"
#include "meta_ham\HamProfile.h"
#include "obj\Data.h"
#include "os\Debug.h"
#include "utl\Symbol.h"

AccomplishmentOneShot::AccomplishmentOneShot(DataArray *d, int i)
    : AccomplishmentConditional(d, i) {
    Configure(d);
}

AccomplishmentOneShot::~AccomplishmentOneShot() {}

// w20-m (normalized 100, 0 mismatched instructions, fuzzy 99.93): behaviour
// verified against the image -- every arm's branch target (Beginner/difficulty
// gate, cross-jumped flawless/nices calls, NumDays 0x114 / NumWeekends 0x11c,
// s == omg gate, shared Int() >= val tail, notify-and-return-false default).
// The charged relocations are names only: NumVerts@RndMesh vs
// GetFlawlessMoveCount and the two MakeString instantiations are ICF folds
// (same address in icf_aliases.map), and the second stars_earned / omg statics
// carry scope ordinal ?CP@/?CM@ (47/44) in the image vs ours ?EF@/?EC@ (69/66):
// the original opened 22 fewer scopes between the stars and hardest_stars
// arms. A shared `int count` + `||`-merged arms reproduced the ordinals exactly
// (44/47) but broke the cross-jumped block layout (85.0), so the separate-arm
// spelling stays; the original arm spelling is still unknown.
// w25-gs: not a register decision -- the ordinal is c1's per-function scope
// counter, so the c2 tap has nothing to say about it. Measured costs: each
// `else if (c) { if (x >= val) return true; }` arm opens 6 scopes, the inner
// if 2, MILO_ASSERT 5. Writing the arms as independent `if (c) { ...;
// continue; }` blocks is code-identical (245/245 rows) and gives ?DL@/?DO@
// (59/62); the image needs 44/47, 15 fewer. Open question: which arm shape
// opens about 2.5 scopes per arm and still cross-jumps like the image.
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
        const AccomplishmentCondition &cur = *it;
        Symbol condition = cur.mCondition;
        Difficulty d2 = cur.mDifficulty;
        int val = cur.mValue;
        unsigned char b6;
        if (d2 == kDifficultyBeginner) {
            b6 = 1;
        } else if (d == kDifficultyBeginner) {
            b6 = 0;
        } else {
            b6 = d2 <= d;
        }
        if (b6 != 0) {
            if (condition == stars) {
                static Symbol stars_earned("stars_earned");
                const DataNode *pStarsNode =
                    TheHamProvider->Property(stars_earned, false);
                MILO_ASSERT(pStarsNode, 0x112);
                if (pStarsNode->Int() >= val)
                    return true;
            } else if (condition == flawless_a) {
                if (progress.GetFlawlessMoveCount() >= val)
                    return true;
            } else if (condition == flawless_b) {
                if (progress.GetFlawlessMoveCount() >= val)
                    return true;
            } else if (condition == nices_a) {
                if (progress.GetNiceMoveCount() >= val)
                    return true;
            } else if (condition == nices_b) {
                if (progress.GetNiceMoveCount() >= val)
                    return true;
            } else if (condition == days) {
                if (progress.NumDays() >= val)
                    return true;
            } else if (condition == weekends) {
                if (progress.NumWeekends() >= val)
                    return true;
            } else if (condition == hardest_stars) {
                static Symbol omg("omg");
                if (s == omg) {
                    static Symbol stars_earned("stars_earned");
                    const DataNode *pStarsNode =
                        TheHamProvider->Property(stars_earned, false);
                    MILO_ASSERT(pStarsNode, 0x14C);
                    if (pStarsNode->Int() >= val)
                        return true;
                }
            } else {
                MILO_NOTIFY("Condition is not currently supported: %s ", condition);
                return false;
            }
        }
    }
    return false;
}

void AccomplishmentOneShot::Configure(DataArray *i_pConfig) {
    MILO_ASSERT(i_pConfig, 0x23);
}
