#pragma once
#include "utl/MemMgr.h"
#include "utl/Str.h"
#include "xdk/xapilibi/xbase.h"

class Friend {
public:
    Friend();
    void SetName(String name) { mName = name; }

    MEM_OVERLOAD(Friend, 0x1b)

    String mName; // 0x0
    String unkc; // 0x8
    String unk10; // 0x10
    XUID mXUID; // 0x18
};
