#pragma once

#include "meta_ham/Award.h"
#include "obj/Data.h"
#include "utl/Symbol.h"
class AccomplishmentCategory {
public:
    virtual ~AccomplishmentCategory();

    AccomplishmentCategory(DataArray const *, int);
    Symbol GetName() const;
    Symbol GetAward() const;
    Symbol GetGroup() const;
    bool HasAward() const;

    int mIndex;
    Symbol mName; // 0x8
    Symbol mGroup; // 0xc
    Symbol mAward; // 0x10

protected:
    void Configure(DataArray const *);
};
