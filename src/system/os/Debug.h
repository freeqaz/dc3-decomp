#pragma once
#include "utl\TextStream.h"
#include "utl\TextFileStream.h"
#include <list>
#include <string.h>

class DataPoint;
typedef void ExitCallbackFunc(void);
typedef void FixedStringFunc(FixedString &);

// size 0x134
class Debug : public TextStream {
public:
    enum ModalType {
        kModalWarn = 0,
        kModalNotify = 1,
        kModalFail = 2
    };

    typedef void ModalCallbackFunc(ModalType &, FixedString &, bool);

private:
    void Modal(ModalType &, const char *, void *);

    bool mNoDebug; // 0x4
    bool mFailing; // 0x5
    bool mExiting; // 0x6
    bool mNoTry; // 0x7
    bool mNoModal; // 0x8
    int mTry; // 0xc
    TextFileStream *mLog; // 0x10
    bool mAlwaysFlush; // 0x14
    TextStream *mReflect; // 0x18
    ModalCallbackFunc *mModalCallback; // 0x1c
    std::list<ExitCallbackFunc *> mFailCallbacks; // 0x20
    std::list<ExitCallbackFunc *> mExitCallbacks; // 0x28
    std::list<FixedStringFunc *> mFailAppendCallbacks; // 0x30
    void (*mCrucibleCallback)(ModalType, DataPoint &); // 0x38
    // 0x3c is a struct, StackData
    unsigned int mFailThreadStack[50]; // starts at 0x3c
    const char *mFailThreadMsg; // 0x104
    const char *mNotifyThreadMsg; // 0x108
    const char *mCrucibleHostname; // 0x10c
    const char *mCrucibleApp; // 0x110
    String mCrucibleProject; // 0x114
    String mKernelVersion; // 0x11c
    String unk124; // 0x124
    String mHostName; // 0x12c

public:
    Debug();
    virtual ~Debug();
    virtual void Print(const char *);

    void Poll();
    void SetDisabled(bool);
    void SetTry(bool);
    void AddExitCallback(ExitCallbackFunc *func) { mExitCallbacks.push_front(func); }
    void AddFailAppendCallback(FixedStringFunc *func) { mFailAppendCallbacks.push_front(func); }
    void RemoveExitCallback(ExitCallbackFunc *);
    void AddFixedStrCallback(FixedStringFunc *func) { mFailAppendCallbacks.push_front(func); }
    bool CheckModalCallback(ModalCallbackFunc *func) { return mModalCallback == func; }
    ModalCallbackFunc *ModalCallback() const { return mModalCallback; }
    bool NoModal() const { return mNoModal; }
    void SetNoModal(bool nomodal) { mNoModal = nomodal; }

    void StartLog(const char *, bool);
    void StopLog();
    void Init();
    ModalCallbackFunc *SetModalCallback(ModalCallbackFunc *);
    void Exit(int, bool);
    void Warn(const char *msg);
    void Notify(const char *msg);
    void Fail(const char *msg, void *);
    void DoCrucible(ModalType, const char *, void *);
    TextStream *Reflect() const { return mReflect; }
    TextStream *SetReflect(TextStream *ts) {
        TextStream *ret = mReflect;
        mReflect = ts;
        return ret;
    }
};

typedef void ModalCallbackFunc(Debug::ModalType &, FixedString &, bool);

#include "utl\Str.h"
#include "utl\MakeString.h"
#include <list>

extern Debug TheDebug;
extern const char *kAssertStr;

#define MILO_ASSERT(cond, line)                                                          \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            TheDebugFailer << MakeString(kAssertStr, __FILE__, line, #cond);             \
        }                                                                                \
    } while (0)

// Same emitted code as MILO_ASSERT, but opens NO lexical scope.
//
// The shipping DC3 binary contains more than one assert spelling.  MSVC stamps
// a per-function "scopes opened so far" counter into every function-local
// static's mangled name, so the binary tells us how many scopes each assert
// opened.  The do/while form above opens five; 16 functions' statics can only
// be reconciled at five, and swapping the whole build to this expression form
// costs 10,556 B of matched code (measured 2026-08-19).  But four functions --
// Hmx::Object::ExportPropertyChange, `anonymous namespace'::MonthToken,
// ShellInput::Init and KinectSharePanel::OnPostLink -- require an assert that
// opens NONE, and this is the spelling that satisfies them.  It is also the
// spelling the sibling RB3 decomp settled on (../rb3/src/system/os/Debug.h).
// Both forms compile to byte-identical instructions; only the numbering of the
// statics that follow them differs.
//
// DO NOT use this at a new call site unless the target's scope index for a
// local static in that same function demands it.  See
// docs/decomp/patterns/fixable-scope-index.md.
#define MILO_ASSERT_EXPR(cond, line)                                                     \
    ((cond) || (TheDebugFailer << MakeString(kAssertStr, __FILE__, line, #cond), 0))

// Same emitted code as MILO_ASSERT, but opens THREE lexical scopes instead of
// five -- it is the do/while form with the do/while peeled off, so the cost is
// `if` (2) + its braced body (1).  A third spelling, in other words, sitting
// between MILO_ASSERT (5) and MILO_ASSERT_EXPR (0).
//
// Five functions in the shipping image can only be reconciled at three:
// Automator::FillButtonMsg, CampaignSongProvider::Text,
// CampaignMqCrewProvider::Text, CampaignMqCrewProvider::UpdateList and
// SongSelectPlaylistProvider::Text.  Each of them holds a function-local
// static whose scope ordinal is exactly 2 lower per preceding assert than the
// do/while form produces, and on each the correction ALSO flips the static's
// guard variable from MSVC's per-TU `?$S<n>@...@4IA` naming to the target's
// bit-packed `??_B<scope>@<fn>@5<scope>@` -- scripts/obj_guard_patcher.py
// already performs that rename, but it keys on the scope, so it silently
// declines to fire while the ordinals disagree.  (The `??_B` vs `$S` split was
// previously filed as a compiler-mode floor.  It is not: `??_B` is what MSVC
// emits for a static inside a COMDAT function, `$S<n>` for one inside an
// ordinary function, and the patcher bridges the two -- when, and only when,
// the scopes line up.)
//
// HAZARD: unlike MILO_ASSERT this is not a single statement, so
// `if (x) MILO_ASSERT_IF(c, n); else y();` binds the `else` to the macro's own
// `if`.  Use it only in plain statement position.
//
// DO NOT use this at a new call site unless the target's scope index for a
// local static in that same function demands it.  See
// docs/decomp/patterns/fixable-scope-index.md.
#define MILO_ASSERT_IF(cond, line)                                                       \
    if (!(cond)) {                                                                       \
        TheDebugFailer << MakeString(kAssertStr, __FILE__, line, #cond);                 \
    }

#define MILO_ASSERT_FMT(cond, ...)                                                       \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            TheDebugFailer << MakeString(__VA_ARGS__);                                   \
        }                                                                                \
    } while (0)

#define MILO_FAIL(...) TheDebugFailer << MakeString(__VA_ARGS__)
#define MILO_WARN(...) TheDebugWarner << MakeString(__VA_ARGS__)
// DTA runtime errors (DataNode "Data %s is not Int" etc.). The image routes
// them through TheDebugFailer: inside MILO_TRY that throws the message to the
// MILO_CATCH (FlowMathOp::Apply keeps result = val, Cheats/Console/Watcher
// report the bad script); outside it is the fatal modal.
// Native: the same failer. Debug::Fail throws under the image's conditions
// inside MILO_TRY, and outside it native fails are non-fatal (platform choice,
// see Debug::Fail) and print `FAIL:`. Native used to downgrade the outside-TRY
// case to a warning because boot hit platform absences there (net_cache_mgr,
// skeleton_identifier); both objects exist natively now, and a probe of every
// MILO_FAIL_DTA reached outside MILO_TRY over the --all-gates suite and a
// 1500-frame headless boot found none but DtaTypeErrorTest's own (w25-pch2).
#define MILO_FAIL_DTA(...) TheDebugFailer << MakeString(__VA_ARGS__)
#define MILO_NOTIFY(...) TheDebugNotifier << MakeString(__VA_ARGS__)
#define MILO_NOTIFY_BETA(...) DebugBeta() << MakeString(__VA_ARGS__)
#ifdef HX_NATIVE
#define MILO_LOG(...) do { const char *milo_log_str_ = MakeString(__VA_ARGS__); fprintf(stderr, "%s", milo_log_str_); TheDebug << milo_log_str_; } while(0) // args evaluated ONCE (was twice)
#else
#define MILO_LOG(...) TheDebug << MakeString(__VA_ARGS__)
#endif

// Usage:
// MILO_TRY {
//     // The code to try
// } MILO_CATCH(errMsg) {
//     // Use errMsg here, e.g.:
//     MILO_NOTIFY("An unexpected thing happened: %s", errMsg);
// }
#define MILO_TRY                                                                         \
    TheDebug.SetTry(true);                                                               \
    try {                                                                                \
        do

#define MILO_CATCH(name)                                                                 \
    while (false)                                                                        \
        ;                                                                                \
    TheDebug.SetTry(false);                                                              \
    }                                                                                    \
    catch (const char *name)

// (min) <= (value) && (value) < (max)
#define MILO_ASSERT_RANGE(value, min, max, line)                                         \
    MILO_ASSERT((min) <= (value) && (value) < (max), line)

// (min) <= (value) && (value) <= (max)
#define MILO_ASSERT_RANGE_EQ(value, min, max, line)                                      \
    MILO_ASSERT((min) <= (value) && (value) <= (max), line)

class DebugWarner {
public:
    void operator<<(const char *c) { TheDebug.Warn(c); }
};

extern DebugWarner TheDebugWarner;

class DebugNotifier {
public:
    void operator<<(const char *c) { TheDebug.Notify(c); }
};

extern DebugNotifier TheDebugNotifier;

class DebugFailer {
public:
    void operator<<(const char *cc) { TheDebug.Fail(cc, nullptr); }
};

extern DebugFailer TheDebugFailer;

class DebugNotifyOncePrinter {
    char msg[0x100];

public:
    void operator<<(const char *cc) {
        if (strcmp(msg, cc)) {
            strncpy(msg, cc, 0xFF);
            TheDebug.Print(cc);
        }
    }
};

extern DebugNotifyOncePrinter TheDebugNotifyOncePrinter;

#define MILO_PRINT_ONCE(...) TheDebugNotifyOncePrinter << MakeString(__VA_ARGS__)

namespace {
    // RESIDUAL (w9-c 2026-09-30): this function has exactly ONE instantiation in
    // the whole binary (`default/system/char/CharFaceServo`, via
    // DebugNotifyOncer::operator<<), and it is that unit's only sub-100 row:
    // 96.552 canonical / 96.552 fuzzy, 2 of 59 instructions, 232 B both sides.
    // The two rows are the SAME store moved: the image writes `it`'s home slot
    // with begin() BEFORE the emptiness branch (`stw r11, 0x50(r31)` between the
    // `cmplw` and the `beq`), MSVC SINKS it past the `count > 0x10` early return
    // and emits it immediately before the second loop instead.  Both count loops,
    // both compare loops, the String ctor, the insert and the dtor are all equal.
    // Refuted, one full ninja each (this is a PCH-reached header, so each probe
    // rebuilds 574 TUs):
    //   96.552 (inert)  declaring `it` ahead of `count`
    //   93.103 (WORSE)  giving the counting loop its own iterator and declaring
    //                   `it` once at the top
    //   94.828 (WORSE)  replacing the counting loop with `strings.size() > 0x10`
    //                   -- stlport's size() is an O(n) distance loop, but it does
    //                   not inline to the image's four instructions
    // Declaration order does not move the store, which is consistent with
    // docs/decomp/patterns/lexical-scope-controls-msvc-stack-slots: the slot SET
    // is already right (objdiff reports 1 PERMUTED slot, not a missing one) and
    // only the initialising store's placement differs.
    // CLOSED (w17-c): 96.552 -> 100.  The count is `strings.size()` AND the
    // compare loop owns its own for-scoped iterator.  The 0x50 store the image
    // makes before the emptiness branch appears to be the begin() temporary of size()'s
    // inlined distance(); the 94.828 probe above still shared one `it` between
    // the size() test and the loop, which rotated the loop's pre-test away.
    inline bool AddToStrings(const char *name, std::list<String> &strings) {
        if (strings.size() > 0x10)
            return false;
        for (std::list<String>::iterator it = strings.begin(); it != strings.end(); ++it) {
            if (strcmp(it->c_str(), name) == 0)
                return false;
        }
        String s(name);
        strings.push_back(s);
        return true;
    }
}

class DebugNotifyOncer {
private:
    std::list<String> mStrings;

public:
    DebugNotifyOncer() {}
    ~DebugNotifyOncer() {}

    void operator<<(const char *cc) {
        if (AddToStrings(cc, mStrings)) {
            TheDebugNotifier << cc;
        }
    }
};

#define MILO_NOTIFY_ONCE(...)                                                            \
    {                                                                                    \
        static DebugNotifyOncer _dw;                                                     \
        _dw << MakeString(__VA_ARGS__);                                                  \
    }

class DebugWarnOncer {
private:
    std::list<String> mStrings;

public:
    DebugWarnOncer() {}
    ~DebugWarnOncer() {}

    void operator<<(const char *cc) {
        if (AddToStrings(cc, mStrings)) {
            TheDebugWarner << cc;
        }
    }
};

#define MILO_WARN_ONCE(...)                                                              \
    {                                                                                    \
        static DebugWarnOncer _dw;                                                       \
        _dw << MakeString(__VA_ARGS__);                                                  \
    }
