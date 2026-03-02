#include "os/Debug.h"
#include "HolmesClient.h"
#include "obj/Data.h"
#include "os/CritSec.h"
#include "os/OSFuncs.h"
#include "os/SynchronizationEvent.h"
#include "os/System.h"
#include "os/Timer.h"
#include "os/NetworkSocket.h"
#include "utl/MemMgr.h"
#include "utl/Option.h"
#include "utl/TextFileStream.h"
#include "utl/MakeString.h"
#include <vector>
#include "xdk/XAPILIB.h"
#include "xdk/xbdm/xbdm.h"
#include "utl/Std.h"

extern long HmxGlobalHandler(_EXCEPTION_POINTERS *);

const char *kAssertStr = "File: %s Line: %d Error: %s\n";
bool gMemoryUsageTest;
DebugWarner TheDebugWarner;
DebugNotifier TheDebugNotifier;
DebugFailer TheDebugFailer;
SynchronizationEvent gNotifyThreadSync;
CriticalSection gNotifyThreadSec;
Debug TheDebug;
std::vector<String> gNotifies;

typedef void ModalCallbackFunc(Debug::ModalType &, FixedString &, bool);

void Debug::SetDisabled(bool d) { mNoDebug = d; }

void Debug::StopLog() { RELEASE(mLog); }

const char *DevHostname(Symbol s) {
    static Symbol hostnames = "hostnames";
    return SystemConfig() ? SystemConfig(hostnames, s)->Str(1) : nullptr;
}

ModalCallbackFunc *Debug::SetModalCallback(ModalCallbackFunc *func) {
    if (mNoModal)
        return nullptr;
    ModalCallbackFunc *oldFunc = mModalCallback;
    mModalCallback = func;
    if (!gNotifies.empty()) {
        for (int i = 0; i < gNotifies.size(); i++) {
            MILO_NOTIFY("%s\n", gNotifies[i].c_str());
        }
        gNotifies.clear();
    }
    return oldFunc;
}

void DebugModal(enum Debug::ModalType &ty, class FixedString &str, bool b3) {
    if (ty == Debug::kModalFail) {
        str += "\n\n-- Program ended --\n";
    } else {
        gNotifies.push_back(str.c_str());
    }
    MILO_LOG("%s\n", str.c_str());
}

Debug::Debug()
    : mNoDebug(0), mFailing(0), mExiting(0), mNoTry(0), mNoModal(0), mTry(0), mLog(0),
      mAlwaysFlush(0), mReflect(0), mModalCallback(DebugModal), unk38(0),
      mFailThreadMsg(0), mNotifyThreadMsg(0), unk10c(0), unk110(0) {}

void Debug::RemoveExitCallback(ExitCallbackFunc *func) {
    if (!mExiting) {
        mExitCallbacks.remove(func);
    }
}

Debug::~Debug() { StopLog(); }

void Debug::Print(const char *msg) {
    if (mLog) {
        mLog->Print(msg);
        if (mAlwaysFlush) {
            mLog->File().Flush();
        }
    }
    if (MainThread() && mReflect) {
        mReflect->Print(msg);
    }
    if (!UsingCD()) {
        HolmesClientPrint(msg);
    }
    OutputDebugStringA(msg);
}

void Debug::Exit(int exitCode, bool call_exit) {
    if (!mExiting) {
        mExiting = true;
        MILO_LOG("APP EXITING\n");
        MILO_LOG("EXIT CODE %d call_exit %d\n", exitCode, call_exit);
        if (!gMemoryUsageTest) {
            FOREACH (it, mExitCallbacks) {
                (*it)();
            }
        }
        mExitCallbacks.clear();
        if (call_exit) {
            XLaunchNewImage("", 0);
        }
    }
}

void Debug::Warn(const char *msg) {
    if (!mNoDebug) {
        if (!MainThread()) {
            MILO_LOG("THREAD-NOTIFY: %s\n", msg);
            if (mModalCallback) {
                CritSecTracker tracker(&gNotifyThreadSec);
                mNotifyThreadMsg = msg;
                gNotifyThreadSync.Wait(200);
            }
        } else {
            ModalType type = kModalWarn;
            Modal(type, msg, nullptr);
        }
    }
}

void Debug::Notify(const char *msg) {
    if (!mNoDebug) {
        if (!MainThread()) {
            MILO_LOG("THREAD-NOTIFY: %s\n", msg);
            if (mModalCallback) {
                CritSecTracker tracker(&gNotifyThreadSec);
                mNotifyThreadMsg = msg;
                gNotifyThreadSync.Wait(200);
            }
        } else {
            ModalType type = kModalNotify;
            Modal(type, msg, nullptr);
        }
    }
}

void Debug::Fail(const char *msg, void *v) {
#ifdef HX_NATIVE
    fprintf(stderr, "FAIL: %s\n", msg);
    // Default: fatal, to catch bugs early. Set MILO_FATAL_FAILS=0 to continue
    // past MILO_FAIL (like Xbox 360 debug build "Continue" dialog).
    static int sFatalFails = -1;
    if (sFatalFails == -1) {
        const char *env = getenv("MILO_FATAL_FAILS");
        sFatalFails = (!env || atoi(env) != 0) ? 1 : 0;
    }
    if (sFatalFails)
        abort();
    return;
#endif
    if (!mNoDebug && !mFailing) {
        mFailing = true;
        StackString<256> msgStr(msg);
        StackString<4096> stackTrace;
        DataAppendStackTrace(stackTrace);
        MILO_LOG(stackTrace.c_str());
        static int heap = MemFindHeap("main");
        MemPushHeap(heap);
        if (!MainThread()) {
            CaptureStackTrace(0x32, (StackData *)mFailThreadStack, v);
            mFailThreadMsg = msg;
            MILO_LOG("THREAD-FAIL: %s\n", msgStr);
            while (true) {
                Timer::Sleep(200);
                PlatformDebugBreak();
            }
        }
        if (mTry) {
            mTry--;
            throw msg;
        }
        FOREACH (it, mFailCallbacks) {
            (*it)();
        }
        mFailCallbacks.clear();
        ModalType t = kModalFail;
        Modal(t, msgStr.c_str(), v);
        if (t != kModalFail) {
            mFailing = false;
        }
        MemPopHeap();
        mFailing = false;
    }
}

void Debug::Poll() {
    MILO_ASSERT(MainThread(), 0x1D4);
    if (mTry) {
        int oldTry = mTry;
        mTry = 0;
        MILO_FAIL("TRY conditional not exited %d", oldTry);
    }
    if (mFailThreadMsg) {
        Fail(mFailThreadMsg, nullptr);
    }
    if (mNotifyThreadMsg) {
        String notifyStr(mNotifyThreadMsg);
        mNotifyThreadMsg = nullptr;
        gNotifyThreadSync.Set();
        Notify(notifyStr.c_str());
    }
}

void Debug::SetTry(bool tryBool) {
    MILO_ASSERT(MainThread(), 0x1F5);
    if (!mNoTry) {
        if (tryBool) {
            mTry++;
        } else
            mTry--;
    }
}

void Debug::StartLog(const char *log, bool flush) {
    RELEASE(mLog);
    mLog = new TextFileStream(log, false);
    mAlwaysFlush = flush;
    if (mLog->File().Fail()) {
        MILO_NOTIFY("Couldn't open log %s", log);
        RELEASE(mLog);
    }
}

void Debug::Init() {
    mNoTry = OptionBool("no_try", false);
    const char *log = OptionStr("log", nullptr);
    if (log) {
        StartLog(log, true);
    }
    if (OptionBool("no_modal", false)) {
        SetModalCallback(nullptr);
        mNoModal = true;
    } else {
        SetModalCallback(DebugModal);
    }
    log = OptionStr("log", nullptr);
    if (log) {
        StartLog(log, true);
    }
#ifndef HX_NATIVE
    SetUnhandledExceptionFilter(&HmxGlobalHandler);
#endif
    mFailing = false;
    DM_SYSTEM_INFO sysInfo;
    unsigned char pad[12];
    (void)pad;
    sysInfo.SizeOfStruct = 0x20;
    if (DmGetSystemInfo(&sysInfo) >= 0) {
        mKernelVersion = MakeString("%d.%d", sysInfo.KernelVersion.Major, sysInfo.KernelVersion.Minor);
    }
    mHostName = NetworkSocket::GetHostName();
}

const char *GetExpCode(int code) {
    volatile int arg = code;

    if (code <= 0xC000008D) {
        if (code != 0xC000008D) {
            if (code <= 0xC0000006) {
                if (code != 0xC0000006) {
                    int temp = code - 0x80000001;
                    if (temp != 0) {
                        switch ((unsigned int)temp) {
                        case 0x40000004:
                            return "EXCEPTION_ACCESS_VIOLATION";
                        case 0x3:
                            return "EXCEPTION_SINGLE_STEP";
                        case 0x2:
                            return "EXCEPTION_BREAKPOINT";
                        case 0x1:
                            return "EXCEPTION_DATATYPE_MISALIGNMENT";
                        default:
                            break;
                        }
                    } else {
                        return "EXCEPTION_GUARD_PAGE";
                    }
                } else {
                    return "EXCEPTION_IN_PAGE_ERROR";
                }
            } else {
                int temp = code - 0xC0000008;
                if (temp != 0) {
                    switch ((unsigned int)temp) {
                    case 0x84:
                        return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
                    case 0x1E:
                        return "EXCEPTION_INVALID_DISPOSITION";
                    case 0x1D:
                        return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
                    case 0x15:
                        return "EXCEPTION_ILLEGAL_INSTRUCTION";
                    default:
                        break;
                    }
                } else {
                    return "EXCEPTION_INVALID_HANDLE";
                }
            }
        } else {
            return "EXCEPTION_FLT_DENORMAL_OPERAND";
        }
    } else {
        switch (code) {
        default: {
            int temp = code + 0x3FFFFF72;
            if ((unsigned int)temp <= 8U) {
                if (temp == 0) {
                    return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
                }
                return "EXCEPTION_PRIV_INSTRUCTION";
            }
            extern const char *merged_82610090(const char *, volatile int *);
            return merged_82610090("Unhandled Exception", &arg);
        }
        case (int)0xC00000FD:
            return "EXCEPTION_STACK_OVERFLOW";
        case (int)0xC000013A:
            return "CONTROL_C_EXIT";
        }
    }
    return "Unhandled Exception";
}
