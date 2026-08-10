#pragma once

#include "obj\Data.h"
#include "utl\Symbol.h"
class AccomplishmentGroup {
public:
    virtual ~AccomplishmentGroup();

    AccomplishmentGroup(DataArray *, int);
    bool HasAward() const;
    Symbol GetName() const;
    Symbol GetAward() const;

    Symbol mName;
    int mIndex;
    Symbol mAward;

protected:
    virtual void Configure(DataArray *);
};
