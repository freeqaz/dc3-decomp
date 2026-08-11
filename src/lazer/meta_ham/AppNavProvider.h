#pragma once
#include "hamobj\HamNavProvider.h"
#include "ui\UIListLabel.h"
#include "obj/Object.h"

class AppNavProvider : public HamNavProvider {
public:
    AppNavProvider() {}
    ~AppNavProvider();
    // registers as HamNavProvider — transparent factory replacement
    OBJ_CLASSNAME(HamNavProvider);
    OBJ_SET_TYPE(AppNavProvider);
    virtual void Text(int, int, UIListLabel *, UILabel *) const;
    virtual void Custom(int, int, UIListCustom *, Hmx::Object *) const;
    virtual DataNode Handle(DataArray *, bool);

    NEW_OBJ(AppNavProvider)
};
