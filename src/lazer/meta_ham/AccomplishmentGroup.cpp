#include "meta_ham/AccomplishmentGroup.h"
#include "AccomplishmentGroup.h"
#include "obj/Data.h"
#include "os/Debug.h"
#include "utl/Symbol.h"

AccomplishmentGroup::AccomplishmentGroup(DataArray *d, int i)
    : mName(""), mIndex(i), mAward("") {
    Configure(d);
}

AccomplishmentGroup::~AccomplishmentGroup() {}

bool AccomplishmentGroup::HasAward() const { return mAward != ""; }

Symbol AccomplishmentGroup::GetName() const { return mName; }
Symbol AccomplishmentGroup::GetAward() const { return mAward; }

void AccomplishmentGroup::Configure(DataArray *i_pConfig) {
    MILO_ASSERT(i_pConfig, 0x1b);
    mName = i_pConfig->Sym(0);

    static Symbol award("award");
    i_pConfig->FindData(award, mAward, false);
}
