#pragma once
#include "obj\Msg.h"
#include "utl\MemMgr.h"
#include "utl\Str.h"
#include "xdk\xapilibi\xbase.h"

// size 0x20
class Friend {
public:
    Friend();
    void SetName(String name) { mName = name; }
    const char *GetName() const { return mName.c_str(); }

    MEM_OVERLOAD(Friend, 0x1b)

    // Friend::Friend calls String::String() exactly twice, on this+0x0 and
    // this+0xC, and PlatformMgr::Poll stores the XUID at +0x18 into a 0x20-byte
    // allocation.  String is 8 bytes, so two 4-byte members sit between them at
    // +0x8 and +0x14; the default ctor leaves both uninitialised, and nothing
    // recovered so far reads them or the second string.
    String mName; // 0x0
    int unk8; // 0x8
    String unkc; // 0xc
    int unk14; // 0x14
    XUID mXUID; // 0x18
};

DECLARE_MESSAGE(FriendsListChangedMsg, "friends_list_changed")
FriendsListChangedMsg(int i) : Message(Type(), i) {}
END_MESSAGE
