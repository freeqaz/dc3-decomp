template <class T, int InitVal, int DestroyVal>
class ScopedState {
public:
    ScopedState(T *ptr) : mPtr(ptr) { *mPtr = (T)InitVal; }
    ~ScopedState();
    T *mPtr;
};

template <class T, int InitVal, int DestroyVal>
ScopedState<T, InitVal, DestroyVal>::~ScopedState() {
    *mPtr = (T)DestroyVal;
}

// Force instantiation of the destructor COMDAT

// w8-e 2026-09-15: both 0% `fn_` rows in this unit are symbols.txt name
// collisions, not missing code.
//   * fn_825CCF20 (160 B) is ?DebugModal@@YAXAAW4ModalType@Debug@@AAVFixedString
//     @@_N@Z.  ham_xbox_r.map lists that mangled name TWICE -- 82331FA8 from
//     App.obj (size 0x1D0) and 825CCF20 from os:Debug.obj (this one) -- both
//     bare `f`, which only internal linkage produces.  dtk's apply_symbols_file
//     (jeff src/util/config.rs) can bind a real name exactly once and PARKS the
//     loser as fn_<addr>; config/373307D9/symbols.txt:117577 binds the App copy,
//     so the Debug copy is permanently fn_825CCF20 and can never pair.
//   * fn_825CE40C: see the w17-d note below -- it was a real funclet.
//
// w17-d: fn_825CE40C is Debug::Fail's MemHeapTracker cleanup (now 100).
template ScopedState<bool, 1, 0>::~ScopedState();

#include "os\Debug.h"
#include "HolmesClient.h"
#include "obj\Data.h"
#include "os\AppChild.h"
#include "os\CritSec.h"
#include "os\File.h"
#include "os\OSFuncs.h"
#include "os\SynchronizationEvent.h"
#include "os\System.h"
#include "os\Timer.h"
#include "os\NetworkSocket.h"
#include "utl\Cheats.h"
#include "utl\DataPointMgr.h"
#include "utl/Loader.h"
#include "utl\MemMgr.h"
#include "utl\Option.h"
#include "utl\TextFileStream.h"
#include "utl\MakeString.h"
#include "world\CameraShot.h"
#include <vector>
#include "xdk\XAPILIB.h"
#include "xdk\xbdm\xbdm.h"
#include "utl\Std.h"

const char *GetExpCode(int code);

#if defined(HX_NATIVE) && !defined(HX_WEB)
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <execinfo.h>
#include <set>
#include <string>
// Native-only harvest tap, OFF unless DC3_TAG_MODALS=1.
//
// On native MILO_ASSERT/MILO_FAIL are non-fatal and MILO_WARN/MILO_NOTIFY reach
// stderr (if at all) through App.cpp's DebugModal as an UNPREFIXED MILO_LOG line
// -- and a notify at notify_level 1 is routed to cheat_display and never printed.
// So a log cannot tell a warning from ordinary chatter.  With the tap on, every
// Warn/Notify/Fail is re-emitted as one `MILO_<KIND>: <msg>` line (newlines
// escaped), and the first occurrence of each distinct message also dumps a raw
// backtrace (`MILO_BT: ...` lines, resolvable with addr2line) so the site can be
// found even when the message carries no file:line.  scripts/native_assert_harvest.py
// consumes this.  Emits nothing and changes no control flow when disabled.
static void NativeModalTap(const char *kind, const char *msg) {
    static int sEnabled = -1;
    if (sEnabled == -1) {
        const char *env = getenv("DC3_TAG_MODALS");
        sEnabled = (env && atoi(env) != 0) ? 1 : 0;
    }
    if (!sEnabled)
        return;
    std::string line(msg ? msg : "(null)");
    std::string esc;
    esc.reserve(line.size());
    for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == '\n')
            esc += "\\n";
        else
            esc += line[i];
    }
    fprintf(stderr, "MILO_%s: %s\n", kind, esc.c_str());
    static std::set<std::string> sSeen;
    static int sDumps = 0;
    if (sDumps < 2000 && sSeen.insert(std::string(kind) + esc).second) {
        if (sDumps++ == 0) {
            // Load base of this executable, so a harvester can turn the raw
            // return addresses below into addr2line offsets.
            Dl_info info;
            if (dladdr((void *)&NativeModalTap, &info) && info.dli_fbase) {
                fprintf(stderr, "MILO_BT_BASE: %p %s\n", info.dli_fbase,
                        info.dli_fname ? info.dli_fname : "?");
            }
        }
        void *frames[32];
        int n = backtrace(frames, 32);
        for (int i = 1; i < n; i++) {
            fprintf(stderr, "MILO_BT: %p\n", frames[i]);
        }
        fprintf(stderr, "MILO_BT_END\n");
    }
    fflush(stderr);
}
#define NATIVE_MODAL_TAP(kind, msg) NativeModalTap(kind, msg)
#else
#define NATIVE_MODAL_TAP(kind, msg)
#endif

long HmxGlobalHandler(_EXCEPTION_POINTERS *ep) {
    if (DmIsDebuggerPresent()) {
        return 1;
    }
    void *addr = ep->ContextRecord;
    const char *code = GetExpCode(ep->ExceptionRecord->ExceptionCode);
    TheDebug.Fail(code, addr);
    return 0;
}

const char *kAssertStr = "File: %s Line: %d Error: %s\n";
extern bool gMemoryUsageTest;
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
    if (gNotifies.size() > 0) {
        for (int i = 0; i < gNotifies.size(); i++) {
            MILO_LOG("%s\n", gNotifies[i].c_str());
        }
        gNotifies.clear();
    }
    return oldFunc;
}

// UNMEASURED BY CONSTRUCTION.  ham_xbox_r.map lists
// ?DebugModal@@YAXAAW4ModalType@Debug@@AAVFixedString@@_N@Z at two addresses --
// App.obj 0x82331FA8 (App.cpp's own static, 0x1D0 bytes, which report.json
// scores) and os:Debug.obj 0x825CCF20 (this one, 0xA0 bytes).  Two genuinely
// different functions sharing a mangled name, one per TU; symbols.txt can only
// bind the name once, so dtk carves this address as `fn_825CCF20` and nothing
// ever pairs it with the definition below.  Adjudicate it by hand against
// build/373307D9/asm/system/os/Debug.s.
// Audited 2026-09-13: 40/40 instructions equal, relocations included.
// Tool: scripts/analysis/map_multiplicity_census.py
void DebugModal(Debug::ModalType &ty, FixedString &str, bool b3) {
    if (ty == Debug::kModalFail) {
        str += "\n\n-- Program ended --\n";
    } else {
        gNotifies.push_back(str.c_str());
    }
    MILO_LOG("%s\n", str.c_str());
}

Debug::Debug()
    : mNoDebug(0), mFailing(0), mExiting(0), mNoTry(0), mNoModal(0), mTry(0), mLog(0),
      mAlwaysFlush(0), mReflect(0), mModalCallback(DebugModal), mCrucibleCallback(0),
      mFailThreadMsg(0), mNotifyThreadMsg(0), mCrucibleHostname(0), mCrucibleApp(0) {}

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
    NATIVE_MODAL_TAP("WARN", msg);
    // Declared here, assigned in the else: the slot (0x50) is reserved for the
    // whole function, so the MILO_LOG temporary in the other branch takes 0x54.
    ModalType type;
    if (!mNoDebug) {
        if (!MainThread()) {
            MILO_LOG("THREAD-NOTIFY: %s\n", msg);
            if (mModalCallback) {
                CritSecTracker tracker(&gNotifyThreadSec);
                mNotifyThreadMsg = msg;
                gNotifyThreadSync.Wait(200);
            }
        } else {
            type = kModalWarn;
            Modal(type, msg, nullptr);
        }
    }
}

void Debug::Notify(const char *msg) {
    NATIVE_MODAL_TAP("NOTIFY", msg);
    // Declared here, assigned in the else: the slot (0x50) is reserved for the
    // whole function, so the MILO_LOG temporary in the other branch takes 0x54.
    ModalType type;
    if (!mNoDebug) {
        if (!MainThread()) {
            MILO_LOG("THREAD-NOTIFY: %s\n", msg);
            if (mModalCallback) {
                CritSecTracker tracker(&gNotifyThreadSec);
                mNotifyThreadMsg = msg;
                gNotifyThreadSync.Wait(200);
            }
        } else {
            type = kModalNotify;
            Modal(type, msg, nullptr);
        }
    }
}

// w17-d: 98.019 -> 100 (and funclets fn_825CE39C/fn_825CE3C4 99.9 -> 100,
// fn_825CE40C 0 -> 100). BEHAVIOUR FIX. The image's unwind funclets name two
// RAII locals we were missing: __unwind$110261 runs
// ??1?$ScopedState@_N$00$0A@@@ on r31+0x54 (mFailing set true on entry, false
// on exit), and fn_825CE40C runs ??1MemHeapTracker on r31+0x2194. With the bare
// `mFailing = true ... MemPopHeap(); mFailing = false;` a `throw msg` out of a
// MILO_TRY left mFailing stuck at true (every later Fail silently ignored) and
// the heap pushed; the image clears both on unwind. `t` is block-scoped so its
// slot pools with the throw temp at 0x50.
void Debug::Fail(const char *msg, void *v) {
#ifdef HX_NATIVE
    fprintf(stderr, "FAIL: %s\n", msg);
    NATIVE_MODAL_TAP("FAIL", msg);
#ifdef HX_WEB
    // Web port: never fatal — matches Xbox "Continue" dialog behavior.
    // Many init paths trigger benign FAILs (missing assets, stubs).
    return;
#endif
    // Default: non-fatal (match Xbox 360 "Continue" dialog behavior).
    // DTA scripts trigger many benign FAILs during gameplay (missing assets,
    // songs not in lookup tables, etc.). Set MILO_FATAL_FAILS=1 to abort.
    static int sFatalFails = -1;
    if (sFatalFails == -1) {
        const char *env = getenv("MILO_FATAL_FAILS");
        sFatalFails = (env && atoi(env) != 0) ? 1 : 0;
    }
    if (sFatalFails)
        abort();
    return;
#endif
    if (!mNoDebug && !mFailing) {
        ScopedState<bool, true, false> failingState(&mFailing);
        StackString<256> msgStr(msg);
        StackString<4096> stackTrace;
        DataAppendStackTrace(stackTrace);
        MILO_LOG(stackTrace.c_str());
        static int heap = MemFindHeap("main");
        MemHeapTracker tracker(heap);
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
        {
            ModalType t = kModalFail;
            Modal(t, msgStr.c_str(), v);
            if (t != kModalFail) {
                mFailing = false;
            }
        }
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
        // The image formats sysInfo+0x18/+0x1a -- XDKVersion.Build/Qfe -- into
        // mKernelVersion, not KernelVersion.Major/Minor (+0xc/+0xe).
        mKernelVersion = MakeString("%d.%d", sysInfo.XDKVersion.Build, sysInfo.XDKVersion.Qfe);
    }
    mHostName = NetworkSocket::GetHostName();
}

// BEHAVIOUR FIX (w13-d, 96.75 -> 100): this used to be a hand-unrolled if/switch
// tree whose first half (codes <= 0xC000008D) ended in `default: break;` and then
// FELL OFF THE END of the function -- an unknown code such as 0x80000005
// returned whatever was in r3.  The image sends every unmatched code to the
// "Unhandled Exception %d" MakeString (0x825CC524: reached from the
// `bne cr6` at 0x825CC38C, 0x825CC404 and the `bgt cr6` at 0x825CC470).  It is
// one plain switch; MSVC builds the whole compare tree from it.
const char *GetExpCode(int code) {
    switch (code) {
    case (int)0xC0000005:
        return "EXCEPTION_ACCESS_VIOLATION";
    case (int)0x80000004:
        return "EXCEPTION_SINGLE_STEP";
    case (int)0x80000003:
        return "EXCEPTION_BREAKPOINT";
    case (int)0x80000002:
        return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case (int)0x80000001:
        return "EXCEPTION_GUARD_PAGE";
    case (int)0xC0000006:
        return "EXCEPTION_IN_PAGE_ERROR";
    case (int)0xC000008C:
        return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case (int)0xC0000026:
        return "EXCEPTION_INVALID_DISPOSITION";
    case (int)0xC0000025:
        return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
    case (int)0xC000001D:
        return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case (int)0xC0000008:
        return "EXCEPTION_INVALID_HANDLE";
    case (int)0xC000008D:
        return "EXCEPTION_FLT_DENORMAL_OPERAND";
    case (int)0xC000008E:
        return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case (int)0xC000008F:
        return "EXCEPTION_FLT_INEXACT_RESULT";
    case (int)0xC0000090:
        return "EXCEPTION_FLT_INVALID_OPERATION";
    case (int)0xC0000091:
        return "EXCEPTION_FLT_OVERFLOW";
    case (int)0xC0000092:
        return "EXCEPTION_FLT_STACK_CHECK";
    case (int)0xC0000093:
        return "EXCEPTION_FLT_UNDERFLOW";
    case (int)0xC0000094:
        return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case (int)0xC0000095:
        return "EXCEPTION_INT_OVERFLOW";
    case (int)0xC0000096:
        return "EXCEPTION_PRIV_INSTRUCTION";
    case (int)0xC00000FD:
        return "EXCEPTION_STACK_OVERFLOW";
    case (int)0xC000013A:
        return "CONTROL_C_EXIT";
    default:
        return MakeString("Unhandled Exception %d", (const CamShotFrame::BlendEaseMode &)code);
    }
}

void Debug::Modal(ModalType &type, const char *msg, void *addr) {
    String msgCopy(msg);
    DoCrucible(type, msgCopy.c_str(), nullptr);
    StackString<4096> modalMsg(msgCopy.c_str());
    StackString<256> shortMsg;
    StackString<512> dataCallstack;
    StackString<2048> callstack;
    if (type == kModalFail) {
        MILO_LOG("FAIL-MSG: %s\n", msg);
        if (mModalCallback) {
            mModalCallback(type, modalMsg, false);
        }
        if (mFailThreadMsg) {
            AppendThreadStackTrace(modalMsg, (StackData *)mFailThreadStack);
        } else {
            String config;
            String version;
            if (SystemConfig()) {
                config = SystemConfig()->File();
                SystemConfig()->FindData("version", version, false);
            } else {
                config = "<unknown>";
            }
            modalMsg += MakeString(
                "\n\nConsoleName: %s   %s   Plat: %s   ",
                NetworkSocket::GetHostName(),
                version,
                PlatformSymbol(TheLoadMgr.GetPlatform())
            );
            modalMsg += MakeString("\nLang: %s   SystemConfig: %s", SystemLanguage(), config);
            modalMsg += MakeString(
                "\nUptime: %.2f hrs   UsingCD: %s   SDK: %s",
                SystemMs() * (1.0 / 3600000.0),
                UsingCD() ? "true" : "false",
                mKernelVersion
            );
            FOREACH (it, mFailAppendCallbacks) {
                (*it)(modalMsg);
            }
            AppendCheatsLog(shortMsg);
            modalMsg += shortMsg.c_str();
            DataAppendStackTrace(dataCallstack);
            modalMsg += dataCallstack.c_str();
            AppendStackTrace(callstack, addr);
            modalMsg += "\n";
            modalMsg += callstack.c_str();
        }
        if (type == kModalFail && TheAppChild) {
            TheAppChild->Sync(2);
        }
    }
    if (mModalCallback) {
        mModalCallback(type, modalMsg, true);
    } else {
        const char *typeNames[] = { "WARN", "NOTIFY", "FAIL" };
        const char *typeName = typeNames[type];
        MILO_LOG("%s: %s\n", typeName, modalMsg);
    }
    if (type == kModalFail) {
        if (mModalCallback) {
            PlatformDebugBreak();
        }
        Exit(1, true);
    }
}

void Debug::DoCrucible(ModalType type, const char *msg, void *addr) {
    if (!mCrucibleHostname) {
        if (SystemConfig()) {
            DataArray *cfg = SystemConfig()->FindArray("crucible", false);
            if (cfg) {
                mCrucibleHostname = cfg->FindArray("hostname", true)->Str(1);
                mCrucibleApp = cfg->FindArray("app", true)->Str(1);
                mCrucibleProject = cfg->FindArray("project", true)->Str(1);
            }
        }
        if (!mCrucibleHostname) {
            mCrucibleHostname = DevHostname("crucible");
        }
    }
    // MEASURED, 2026-09-14 (lane w7-aa).  The residual 22 rows here are a single
    // r27<->r28 rotation (16 of 21 swap rows) between these two DataPoints, plus
    // one insert/delete pair at idx 278/282 (`addi r3, r31, 0x50` scheduled four
    // rows apart) and 3 PERMUTED stack slots.  Swapping THESE TWO DECLARATIONS
    // does NOT move the registers -- it keeps the whole r27<->r28 rotation and
    // ADDS 9 offset swaps of (0x70,0x90), i.e. it moves the frame slots instead:
    // 22 rows -> 40 rows, 99.53052 -> 99.5 (worse).  Do not re-try the swap; the
    // lever is liveness across the Directory()/DataPointGetter calls, not order.
    DataPoint mainPoint;
    DataPoint detailPoint;
    mainPoint.AddPair("message", DataNode(msg));
    const char *typeStr;
    if (type == kModalFail) {
        typeStr = "crash";
    } else if (type == kModalNotify) {
        typeStr = "notify";
    } else {
        typeStr = "warn";
    }
    mainPoint.AddPair("severity", DataNode(typeStr));
    mainPoint.AddPair("project", DataNode(mCrucibleProject.c_str()));
    mainPoint.AddPair("platform", DataNode(PlatformSymbol(TheLoadMgr.GetPlatform())));
    mainPoint.AddPair("source", DataNode(mHostName));
    {
        String config;
        String version;
        if (SystemConfig()) {
            config = SystemConfig()->File();
            SystemConfig()->FindData("version", version, false);
        } else {
            config = "<unknown>";
        }
        detailPoint.AddPair("config_name", DataNode(config));
        mainPoint.AddPair("version", DataNode(version));
    }
    detailPoint.AddPair("uptime", DataNode(SystemMs()));
    // w16-d: the ternary (with THIS polarity) gives the image's register
    // assignment -- &TheSystemArgs in r27, the TextStream vtable in r28; the
    // `exeName = ""; if (!empty()) exeName = front();` statement form swapped
    // them (20 rows). `!empty() ? front() : ""` fixes the registers but emits
    // an extra branch.
    const char *exeName = TheSystemArgs.empty() ? "" : TheSystemArgs.front();
    {
        StackString<256> exePath(exeName);
        StackString<256> exeBase(exePath.c_str());
        {
            StackString<256> baseName(FileGetBase(exeBase.c_str()));
            exeBase = baseName;
        }
        if (strlen(exeBase.c_str()) > 3) {
            if (exeBase[strlen(exeBase.c_str()) - 2] == '_') {
                exeBase[strlen(exeBase.c_str()) - 2] = '\0';
            }
        }
        exePath.ReplaceAll('\\', '/');
        detailPoint.AddPair("path", DataNode(exePath.c_str()));
        // INERT (lane w7-bb, 2026-09-14): folding this if/else into the ternary
        // `DataNode(mCrucibleApp ? mCrucibleApp : exeBase.c_str())` -- the shape
        // suggested by the image hoisting the DataNode temp's address
        // (`addi r3, r31, 0x50`) ABOVE the `cmplwi`/`bne` at idx 278 where we
        // emit it after -- is byte-identical.  DoCrucible stays at 99.53052 with
        // the identical 22 rows, so the two-slot hoist is scheduler-owned, not
        // a conditional-expression-vs-statement difference.
        // w16-d: also inert -- `DataNode(!mCrucibleApp ? exeBase.c_str() :
        // mCrucibleApp)`; and `mCrucibleApp ? DataNode(..) : DataNode(..)`
        // is far worse (92.6, frame +0x20).
        const char *appName = mCrucibleApp;
        if (!mCrucibleApp) {
            appName = exeBase.c_str();
        }
        mainPoint.AddPair("application", DataNode(appName));
    }
    {
        StackString<256> argsStr;
        for (unsigned int i = 0; i < TheSystemArgs.size(); i++) {
            StackString<256> arg(TheSystemArgs[i]);
            arg.ReplaceAll('\\', '/');
            argsStr += arg.c_str();
            argsStr += "\r\n";
        }
        detailPoint.AddPair("args", DataNode(argsStr.c_str()));
    }
    detailPoint.AddPair("opsys", DataNode(mKernelVersion));
    mainPoint.AddPair("extra", DataNode(""));
    if (type == kModalFail) {
        StackString<512> dataCallstack;
        DataAppendStackTrace(dataCallstack);
        StackString<2048> callstack;
        AppendStackTrace(callstack, addr);
        StackString<3096> stackTrace;
        stackTrace += "\r\n";
        stackTrace += callstack.c_str();
        stackTrace += dataCallstack.c_str();
        detailPoint.AddPair("stack", DataNode(stackTrace.c_str()));
    }
    {
        StackString<256> cheatsMsg;
        AppendCheatsLog(cheatsMsg);
        if (*cheatsMsg.c_str() != '\0') {
            detailPoint.AddPair("history", DataNode(cheatsMsg.c_str()));
        }
    }
    if (mCrucibleCallback) {
        mCrucibleCallback(type, detailPoint);
    }
    String jsonStr;
    detailPoint.ToJSON(jsonStr);
    mainPoint.AddPair("data", DataNode(jsonStr));
}
