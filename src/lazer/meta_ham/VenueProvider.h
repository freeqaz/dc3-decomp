#pragma once
#include "obj/Object.h"
#include "stl\_vector.h"
#include "ui\UIListProvider.h"
#include "utl\Symbol.h"

class VenueProvider : public UIListProvider, public Hmx::Object {
public:
    VenueProvider();
    virtual void Text(int, int, UIListLabel *, UILabel *) const;
    virtual Symbol DataSymbol(int) const;
    virtual int NumData() const;
    virtual int DataIndex(Symbol s) const {
        for (unsigned int i = 0; i < mVenues.size(); i++) {
            if (mVenues[i] == s)
                return i;
        }
        return -1;
    }

    void UpdateList();
    void SetPlayer(int player) { mPlayer = player; }

private:
    int mPlayer; // 0x30
    std::vector<Symbol> mVenues; // 0x34
};
