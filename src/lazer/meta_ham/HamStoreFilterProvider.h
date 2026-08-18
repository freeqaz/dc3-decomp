#pragma once
#include "obj\Data.h"
#include "obj/Object.h"
#include "stl\_vector.h"
#include "ui\UIListProvider.h"
#include "utl\Symbol.h"

class HamStoreFilter {
public:
    HamStoreFilter(Symbol s);
    HamStoreFilter(DataArray const *);

    Symbol mFilterSym;
    String mDisplayName;
    std::vector<Symbol> mSortTypes;
};

class HamStoreFilterProvider : public UIListProvider, public Hmx::Object {
public:
    virtual void Text(int, int, UIListLabel *, UILabel *) const;
    virtual Symbol DataSymbol(int) const;
    virtual int NumData() const;
    virtual RndMat *Mat(int, int, UIListMesh *) const { return nullptr; }

    HamStoreFilterProvider(std::vector<HamStoreFilter *> *);

    std::vector<HamStoreFilter *> *mFilters; // 0x30
};
