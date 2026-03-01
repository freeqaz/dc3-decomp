#pragma once
#include "hamobj/Difficulty.h"
#include "meta_ham/Accomplishment.h"
#include "obj/Data.h"
#include "utl/Symbol.h"
#include <vector>

struct AccomplishmentCondition {
    Symbol mConditionType;
    int mCount;
    Symbol mSymbolArg; // 0x8
    Difficulty mDifficulty; // 0xc
    Symbol mCharacter; // 0x10
    Symbol mMode; // 0x14
    bool mNoFlashcards; // 0x18
};

class AccomplishmentConditional : public Accomplishment {
public:
    virtual ~AccomplishmentConditional();
    virtual Difficulty GetRequiredDifficulty() const;
    virtual bool CanBeLaunched() const { return true; }

    AccomplishmentConditional(DataArray *, int);

protected:
    void Configure(DataArray *);
    void UpdateConditionOptionalData(AccomplishmentCondition &, DataArray *);

    std::list<AccomplishmentCondition> m_lConditions; // 0x68
};
