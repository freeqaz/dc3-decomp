#include "meta_ham/AccomplishmentCampaignConditional.h"
#include "hamobj/Difficulty.h"
#include "meta_ham/AccomplishmentConditional.h"
#include "meta_ham/HamProfile.h"
#include "obj/Data.h"
#include "os/Debug.h"
#include "utl/Symbol.h"

AccomplishmentCampaignConditional::AccomplishmentCampaignConditional(DataArray *d, int i)
    : AccomplishmentConditional(d, i) {
    Configure(d);
}

AccomplishmentCampaignConditional::~AccomplishmentCampaignConditional() {}

bool AccomplishmentCampaignConditional::IsFulfilled(HamProfile *profile) const {
    static Symbol crewsong("crewsong");
    static Symbol crewcomplete("crewcomplete");
    static Symbol crewcomplete_count("crewcomplete_count");
    FOREACH (it, m_lConditions) {
        const AccomplishmentCondition &curCond = *it;
        Difficulty curDiff = curCond.mDifficulty;
        Symbol cur8 = curCond.mSymbolArg;
        int cur4 = curCond.mCount;
        Symbol curSym = curCond.mConditionType;
        const CampaignProgress &progress = profile->GetCampaignProgress(curDiff);
        if (curSym == crewsong) {
            if (progress.IsDanceCrazeSongAvailable(cur8))
                return true;
        } else if (curSym == crewcomplete) {
            if (progress.IsEraComplete(cur8))
                return true;
        } else if (curSym == crewcomplete_count) {
            if (progress.GetNumCompletedEras() >= cur4) {
                return true;
            }
        } else {
            MILO_NOTIFY("Condition is not currently supported: %s ", curSym);
            return false;
        }
    }
    return false;
}

void AccomplishmentCampaignConditional::Configure(DataArray *i_pConfig) {
    MILO_ASSERT(i_pConfig, 0x20);
}
