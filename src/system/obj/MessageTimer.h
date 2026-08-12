#pragma once
#include "math\Utl.h"
#include "os\Timer.h"
#include "os\Debug.h"
#include "utl\MemMgr.h"
#include "utl\Symbol.h"
#include <vector>
#include <algorithm>

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
