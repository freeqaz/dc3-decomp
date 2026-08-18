// Regression test: the ThreadCall worker must have enough stack to run the DTA
// parser (task #105 — SIGSEGV on the DTA loader thread).
//
// THE BUG
// -------
// ThreadCall_Native.cpp created its worker with
//     pthread_attr_setstacksize(&attr, 0x10000);   // 64 KB
// copied verbatim from the shipped Xbox call in src/system/os/ThreadCall_Win.cpp:
//     _beginthreadex(nullptr, 0x10000, MyThreadFunc, nullptr, 0, nullptr);
//
// A stack budget expressed in *bytes* does not survive a change of ABI and code
// generator. DataLoaderThreadObj::ThreadStart -> DataReadStream ->
// ParseArray/ParseNode is the deepest recursion that runs on this thread, and
// its frames are ~17x fatter under clang/x86_64 than under MSVC/PPC:
//
//                  MSVC/PPC (target .s)   clang/x86_64 (gdb frame sizes)
//     ParseNode          0x160  =  352 B              4192 B
//     ParseArray         0x080  =  128 B              4240 B
//     per nesting level           480 B               8432 B
//
// The gap is not pointer width — it is inlining. MakeString<>/FormatString owns
// a `char mFmtBuf[0x1000]` (src/system/utl/MakeString.h). MSVC keeps the
// template instantiation out of line in its own COMDAT; clang inlines it into
// every caller that mentions MILO_ASSERT / MILO_NOTIFY / MakeString, which is
// every function on this path. So each native frame carries a cold 4 KB buffer.
//
// 64 KB therefore bought the Xbox ~130 levels of DTA array nesting and bought
// native about four. HeadlessBootTest.SurvivesMainLoop died with SIGSEGV at
// si_addr == rsp-8 (the `call` instruction's return-address push) on the
// worker's PROT_NONE guard page, four ParseArray levels into a parse during
// ContentMgr::RefreshSynchronously.
//
// WHAT THIS TEST PINS
// -------------------
//  1. The worker thread's stack is at least kMinWorkerStack. This is checked
//     FIRST and with ASSERT_, so a regression fails cleanly instead of taking
//     the whole milo-tests binary down with a hard SIGSEGV.
//  2. The worker can actually parse a DTA nested deeper than shipped content,
//     via the exact BufStream -> DataReadStream path DataLoaderThreadObj uses.
//
// Nothing here touches decomp source, so PPC codegen is unaffected: the Xbox
// build uses ThreadCall_Win.cpp and keeps its 0x10000.

#include "test_helpers.h"

#include "obj/Data.h"
#include "obj/DataFile.h"
#include "os/ThreadCall.h"
#include "utl/BufStream.h"

#include <pthread.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {

// Deepest array nesting in shipped DC3 content is 20
// (world/world_objects.dta, ui/hud/hud_objects.dta). Go past it.
constexpr int kNestDepth = 32;

// At the measured 8432 B per nesting level, 32 levels costs ~270 KB, plus the
// fixed ~13 KB of ThreadStart/DataReadStream/lexer frames. 1 MB is the floor a
// regression would have to stay above; the engine actually asks for 8 MB (the
// same default the main thread gets), so this has ~8x slack and will not fire
// on ordinary tuning — only on a return to an Xbox-sized budget.
constexpr size_t kMinWorkerStack = 1024u * 1024u;

// "(((( 1 ))))" — kNestDepth open parens, an int, kNestDepth close parens.
std::string MakeNestedDta(int depth) {
    std::string s;
    s.reserve(2 * depth + 8);
    for (int i = 0; i < depth; i++)
        s += '(';
    s += " 1 ";
    for (int i = 0; i < depth; i++)
        s += ')';
    s += '\n';
    return s;
}

// Depth of the leftmost spine of a parsed DataArray.
int SpineDepth(const DataArray *arr) {
    int depth = 0;
    while (arr && arr->Size() > 0 && arr->Type(0) == kDataArray) {
        arr = arr->Array(0);
        depth++;
    }
    return depth;
}

// Runs on the ThreadCall worker — the same thread DataLoaderThreadObj uses.
struct LoaderStackProbe : public ThreadCallback {
    std::string mText;
    size_t mStackSize = 0;
    int mParsedDepth = -1;
    bool mParsedOk = false;
    volatile bool mDone = false;

    int ThreadStart() override {
        pthread_attr_t attr;
        if (pthread_getattr_np(pthread_self(), &attr) == 0) {
            void *base = nullptr;
            size_t size = 0;
            if (pthread_attr_getstack(&attr, &base, &size) == 0)
                mStackSize = size;
            pthread_attr_destroy(&attr);
        }

        // Only recurse if the stack can take it — otherwise this would SIGSEGV
        // and kill the whole test binary before gtest could report anything.
        if (mStackSize >= kMinWorkerStack) {
            BufStream bs(mText.data(), (int)mText.size(), true);
            bs.SetName("test_loader_thread_stack.dta");
            DataArray *arr = DataReadStream(&bs);
            if (arr) {
                mParsedOk = true;
                // DataReadStream returns the file's top-level array, whose sole
                // element is the outermost '(' — so the spine length from there
                // is exactly the paren count.
                mParsedDepth = SpineDepth(arr);
                arr->Release();
            }
        }
        return 0;
    }

    void ThreadDone(int) override { mDone = true; }
};

class LoaderThreadStackTest : public EngineTestFixture {};

TEST_F(LoaderThreadStackTest, WorkerHasStackForNestedDtaParse) {
    LoaderStackProbe probe;
    probe.mText = MakeNestedDta(kNestDepth);

    ThreadCall(&probe);
    for (int i = 0; i < 5000 && !probe.mDone; i++) {
        ThreadCallPoll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(probe.mDone) << "ThreadCall worker never completed the job";

    ASSERT_NE(probe.mStackSize, 0u) << "pthread_getattr_np failed on the worker";
    ASSERT_GE(probe.mStackSize, kMinWorkerStack)
        << "ThreadCall worker stack is " << probe.mStackSize
        << " bytes. The DTA parser costs ~8432 bytes per array-nesting level "
           "under clang/x86_64 and shipped content nests to 20, so this will "
           "SIGSEGV on the worker's guard page during "
           "ContentMgr::RefreshSynchronously. See ThreadCall_Native.cpp.";

    EXPECT_TRUE(probe.mParsedOk) << "DataReadStream returned null on the worker";
    EXPECT_EQ(probe.mParsedDepth, kNestDepth)
        << "parsed nesting depth disagrees with the source DTA";
}

} // namespace
