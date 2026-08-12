#include "obj\MessageTimer.h"
#include "obj\DataFunc.h"
#include "utl\Std.h"
#include <vector>

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

struct EventEntry {
    EventEntry(Symbol s, Hmx::Object *o, float ms) {
        msgs = s;
        Add(o, ms);
    }

    Symbol msgs; // 0x0
    std::vector<ObjEntry *> objs; // 0x4

    ~EventEntry() {
        for (int i = 0; i < objs.size(); i++) {
            delete objs[i];
        }
    }

    float MaxMs() {
        float total = 0.0f;
        for (int i = 0; i < objs.size(); i++) {
            MaxEq(total, objs[i]->maxMs);
        }
        return total;
    }

    void Dump() {
        std::sort(objs.begin(), objs.end(), ObjSort());
        MILO_LOG("%g %s\n", MaxMs(), msgs.Str());
        for (int i = 0; i < objs.size(); i++) {
            objs[i]->Dump();
        }
    }

    void Add(Hmx::Object *o, float ms);

    MEM_OVERLOAD(EventEntry, 0x3D);
};

struct MaxSort {
    bool operator()(EventEntry *e1, EventEntry *e2) const {
        return e1->MaxMs() > e2->MaxMs();
    }
};




std::vector<EventEntry *> gEntries;
bool MessageTimer::sActive;

DataNode MessageTimerStop(DataArray *) {
    MessageTimer::Stop();
    return 0;
}

DataNode MessageTimerOn(DataArray *) { return MessageTimer::Active(); }

void EventEntry::Add(Hmx::Object *o, float ms) {
    Symbol sym =
        o ? MakeString("%s 0x%x", o->Name(), (int)o) : MakeString("0x%x", (int)o);
    for (int i = 0; i < objs.size(); i++) {
        if (objs[i]->name == sym) {
            ObjEntry *cur = objs[i];
            MaxEq(cur->maxMs, ms);
            cur->totalMs += ms;
            cur->num++;
            return;
        }
    }
    objs.push_back(new ObjEntry(sym, ms, 1));
}

void MessageTimer::AddTime(Hmx::Object *o, Symbol msg, float ms) {
    if (sActive) {
        for (int i = 0; i < gEntries.size(); i++) {
            if (gEntries[i]->msgs == msg) {
                gEntries[i]->Add(o, ms);
                return;
            }
        }
        gEntries.push_back(new EventEntry(msg, o, ms));
    }
}

DataNode MessageTimerStart(DataArray *) {
    MessageTimer::Start();
    return 0;
}

DataNode MessageTimerDump(DataArray *) {
    MessageTimer::Dump();
    return 0;
}

void MessageTimer::Start() {
    sActive = true;
    DeleteAll(gEntries);
}

void MessageTimer::Stop() { sActive = false; }

void MessageTimer::Dump() {
    MILO_LOG("Message Tracker Dump!\n");
    std::sort(gEntries.begin(), gEntries.end(), MaxSort());
    for (int i = 0; i < gEntries.size(); i++) {
        gEntries[i]->Dump();
    }
}

void MessageTimer::Init() {
    DataRegisterFunc("message_timer_start", MessageTimerStart);
    DataRegisterFunc("message_timer_stop", MessageTimerStop);
    DataRegisterFunc("message_timer_dump", MessageTimerDump);
    DataRegisterFunc("message_timer_on", MessageTimerOn);
}
