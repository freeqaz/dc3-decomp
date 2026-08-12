#pragma once
#include "math\Utl.h"
#include "os\Timer.h"
#include "os\Debug.h"
#include "utl\MemMgr.h"
#include "utl\Symbol.h"
#include <vector>
#include <algorithm>

struct ObjEntry {
    ObjEntry(Symbol s, float ms, int inum) : name(s), maxMs(ms), totalMs(ms), num(inum) {}
    Symbol name; // 0x0
    float maxMs; // 0x4
    float totalMs; // 0x8
    int num; // 0xc

    void Dump() {
        MILO_LOG(
            "  %g %s num %d total %g av %g\n",
            maxMs,
            name.Str(),
            num,
            totalMs,
            totalMs / num
        );
    }

    MEM_OVERLOAD(ObjEntry, 0x16);
};

struct ObjSort {
    bool operator()(ObjEntry *e1, ObjEntry *e2) {
        return e1->maxMs > e2->maxMs ? true : false;
    }
};

class MessageTimer {
protected:
    static bool sActive;
    static void AddTime(Hmx::Object *o, Symbol msg, float ms);

public:
    Timer mTimer;
    class Hmx::Object *mObject;
    Symbol mMessage;

    MessageTimer(class Hmx::Object *o, Symbol message)
        : mTimer(), mObject(o), mMessage(message) {
        mTimer.Restart();
    }
    ~MessageTimer() { AddTime(mObject, mMessage, mTimer.SplitMs()); }
    static bool Active() { return sActive; }
    static void Init();
    static void Start();
    static void Stop();
    static void Dump();
};
