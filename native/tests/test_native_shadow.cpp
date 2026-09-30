// Native-shadow regression tests.
//
// Every test here pins a place where the native build compiled a DIFFERENT
// body from the one the decomp tools measure -- an `#ifdef HX_NATIVE` branch
// (or a native-only definition) whose semantics had drifted from the Xbox
// image. No objdiff ruler can see these by construction: objdiff measures
// only the non-native branch. Inventory: scripts/analysis/native_shadow_audit.py;
// write-up: docs/decomp/patterns/native-shadow-bodies-are-unmeasured.md.
//
// Each test was watched FAILING against the pre-fix native body before the
// fix landed (see the commit that introduced it).

#include "test_helpers.h"

#include "flow/FlowNode.h"
#include "gesture/DrawUtl.h"
#include "gesture/SkeletonViz.h"
#include "flow/FlowQueueable.h"
#include "obj/Object.h"
#include "utl/LocaleChunkSort.h"
#include "math/Rand.h"

#include <cstdlib>
#include <vector>

#include <cstdint>

namespace {

class NativeShadowTest : public EngineTestFixture {};

// ---------------------------------------------------------------------------
// FlowQueueable::mListeners is the queue of pending re-triggers. The image
// keeps it as std::list<Hmx::Object *> (FlowQueueable.s, Activate: the
// obj==null arm stores 0 and calls list::insert), so a trigger with no
// listener is ONE queued entry. The native build had swapped it for an
// ObjPtrList in kObjListNoNull mode, whose insert() silently drops null --
// so every listener-less trigger (Flow::Activate(), FlowRun) vanished from
// the queue and a kQueue flow re-triggered while running never re-ran.
// ---------------------------------------------------------------------------
class ProbeQueueable : public FlowQueueable {
public:
    ProbeQueueable() : mTriggers(0), mChild(nullptr) {}
    // Stand-in for "activating our children leaves one running": record the
    // trigger and keep mChild in the running set, as a long-running child
    // (an animation, a wait) would.
    virtual bool ActivateTrigger() {
        mTriggers++;
        mRunningNodes.push_back(mChild);
        return true;
    }
    void SetInterrupt(QueueState q) { mInterrupt = q; }
    int NumListeners() const { return mListeners.size(); }

    int mTriggers;
    FlowNode *mChild;
};

class ProbeChild : public FlowNode {
public:
    ProbeChild() {}
};

TEST_F(NativeShadowTest, FlowQueueableQueuesListenerlessTriggers) {
    ProbeQueueable *q = new ProbeQueueable();
    ProbeChild *child = new ProbeChild();
    q->mChild = child;
    q->SetInterrupt(FlowNode::kQueue);

    // First trigger starts the flow; second arrives while it is running and
    // must be queued -- both with no listener, as Flow::Activate() does.
    EXPECT_TRUE(q->Activate(nullptr));
    EXPECT_EQ(q->mTriggers, 1);
    EXPECT_EQ(q->NumListeners(), 1) << "a null listener is one queued entry on Xbox";
    EXPECT_TRUE(q->Activate(nullptr));
    EXPECT_EQ(q->NumListeners(), 2);

    // The running child finishes: the queued trigger must re-run the flow.
    q->ChildFinished(child);
    EXPECT_EQ(q->mTriggers, 2) << "the queued listener-less trigger was lost";
    EXPECT_EQ(q->NumListeners(), 1);

    // And when that run finishes, the queue drains with no further run.
    q->ChildFinished(child);
    EXPECT_EQ(q->mTriggers, 2);
    EXPECT_EQ(q->NumListeners(), 0);

    delete q;
    delete child;
}

// ---------------------------------------------------------------------------
// RndText::FitTextEllipsis keeps shrinking while the character before the
// "..." is in a trim set. The image's set is the literal L" .," --
// Text.s 8269A640: lis/addi r23, ??_C@_17BKMGDHOL@?5?$AA?4?$AA?0?$AA?$AA?$AA@
// (.string16 " .,"), then `lhz r4,-0x2(r27); bl wcschr` at 8269A670..78.
// The native u16 port of that loop used {' ', '\t', '\n'}: "Hello, Wor..."
// stopped at "Hello,..." where the Xbox trims to "Hello...", and tabs and
// newlines were trimmed where the Xbox keeps them. wcschr also matches the
// terminator, so a NUL before the ellipsis counts as trimmable too.
// ---------------------------------------------------------------------------
} // namespace

bool RndTextEllipsisTrimsChar(unsigned short c);

namespace {

TEST(NativeShadowUnit, TextEllipsisTrimSetIsTheImagesLiteral) {
    EXPECT_TRUE(RndTextEllipsisTrimsChar(' '));
    EXPECT_TRUE(RndTextEllipsisTrimsChar('.')) << "image trims '.' before the ellipsis";
    EXPECT_TRUE(RndTextEllipsisTrimsChar(',')) << "image trims ',' before the ellipsis";
    EXPECT_FALSE(RndTextEllipsisTrimsChar('\t')) << "image does not trim tab";
    EXPECT_FALSE(RndTextEllipsisTrimsChar('\n')) << "image does not trim newline";
    EXPECT_FALSE(RndTextEllipsisTrimsChar('a'));
    EXPECT_FALSE(RndTextEllipsisTrimsChar(0x3002)); // ideographic full stop: not in L" .,"
    EXPECT_TRUE(RndTextEllipsisTrimsChar(0)) << "wcschr matches the terminator";
}

// ---------------------------------------------------------------------------
// ToggleDrawSkeletons (the "s" key cheat, DTA {ui toggle_draw_skeletons}).
// The image defines it once, in DrawUtl.obj (DrawUtl.s ?ToggleDrawSkeletons:
// 82434B90 lbz r11,0x8(r3) = mShowing; cntlzw/extrwi = !mShowing; bl
// SetShowing; 82434BA4 lbz r3,0x8(r11) returns the new mShowing). HamUI.s
// only CALLS it (82892F18). The DrawUtl body sat in #ifndef HX_NATIVE, so
// native linked a stray copy in HamUI.cpp that set Showing to the static
// RndDrawable::sForceSubpartSelection and returned that: it never turned
// skeleton drawing on. The first toggle from "showing" passes by accident
// (false either way); the second exposes it.
// ---------------------------------------------------------------------------
TEST_F(NativeShadowTest, ToggleDrawSkeletonsFlipsShowing) {
    SkeletonViz *saved = TheSkeletonViz;
    SkeletonViz *viz = Hmx::Object::New<SkeletonViz>();
    TheSkeletonViz = viz;
    bool savedForce = RndDrawable::GetForceSubpartSelection();
    RndDrawable::SetForceSubpartSelection(false);

    viz->SetShowing(true);
    EXPECT_FALSE(ToggleDrawSkeletons());
    EXPECT_FALSE(viz->Showing());
    EXPECT_TRUE(ToggleDrawSkeletons()) << "toggle must turn skeleton drawing back ON";
    EXPECT_TRUE(viz->Showing());

    RndDrawable::SetForceSubpartSelection(savedForce);
    TheSkeletonViz = saved;
    delete viz;
}

// ---------------------------------------------------------------------------
// Locale token lookup. Locale::Init sorts the (symbol, index, string) chunks
// with LocaleChunkSort::Sort and Locale::FindDataIndex binary-searches the
// result, so the two must agree on the key. On the Xbox they do: FastSort<3>
// compares the 32-bit symbol pointers as signed ints (Locale.s 827E9474
// cmpw) and the search compares (int)Symbol signed too (827E93E0 cmpw).
// Natively DataNode is 16 bytes, so FastSort<3>'s 8-byte stride reads the
// signed LOW 32 bits of the symbol pointer, then node1.mType, then the low 32
// bits of node2 -- while the HX_NATIVE search compares full 64-bit pointers,
// unsigned. Whenever the interned strings straddle a low-32 value of
// 0x80000000 (or a 4 GB line) the table is out of order for the search and
// tokens go missing -- ASLR-dependent missing UI text.
// ---------------------------------------------------------------------------
TEST(NativeShadowUnit, LocaleChunkSortOrdersByTheSearchKey) {
    // Three fake interned-string addresses. Sort never dereferences them.
    const uintptr_t base = (uintptr_t)0x7f1200000000ull;
    const char *lowA = (const char *)(base + 0x10);        // low32 0x00000010
    const char *lowB = (const char *)(base + 0x20);        // low32 0x00000020
    const char *high = (const char *)(base + 0x80000010);  // low32 0x80000010 (<0 signed)
    LocaleChunkSort::OrderedLocaleChunk chunks[3];
    const char *in[3] = { high, lowB, lowA };
    for (int i = 0; i < 3; i++) {
        chunks[i].node1 = DataNode(kDataSymbol, in[i]);
        chunks[i].node2 = DataNode(i);
    }
    LocaleChunkSort::Sort(chunks, 3);
    // FindDataIndex's native key is the full pointer, unsigned: lowA < lowB
    // < high. node2 identifies each chunk without dereferencing the fake
    // pointers (LiteralSym() would intern them).
    EXPECT_EQ(chunks[0].node2.Int(), 2); // lowA
    EXPECT_EQ(chunks[1].node2.Int(), 1); // lowB
    EXPECT_EQ(chunks[2].node2.Int(), 0)  // high
        << "sort put the pointer whose low 32 bits are negative FIRST; the "
           "binary search expects it LAST";
    for (int i = 0; i < 3; i++)
        chunks[i].node1 = DataNode(0); // don't let a dtor see the fake symbols
}

TEST(NativeShadowUnit, LocaleChunkSortKeepsFileOrderAmongDuplicates) {
    // A token defined twice: the image's second key is node2 (the running
    // chunk index), so the lower index sorts first and wins the dedupe.
    const char *sym = (const char *)(uintptr_t)0x7f1200001000ull;
    LocaleChunkSort::OrderedLocaleChunk chunks[2];
    chunks[0].node1 = DataNode(kDataSymbol, sym);
    chunks[0].node2 = DataNode(7);

    chunks[1].node1 = DataNode(kDataSymbol, sym);
    chunks[1].node2 = DataNode(3);

    LocaleChunkSort::Sort(chunks, 2);
    EXPECT_EQ(chunks[0].node2.Int(), 3);
    EXPECT_EQ(chunks[1].node2.Int(), 7);
    chunks[0].node1 = DataNode(0);
    chunks[1].node1 = DataNode(0);
}

// ---------------------------------------------------------------------------
// RandomShuffle. The image's std::random_shuffle is STLport's: for i in
// [first+1, last) iter_swap(i, first + __random_number(i-first+1)), and
// __random_number is CRT rand() % n (FlowPickOne.s 824051C8: bl rand; divw;
// mullw; subf). It never touches the game's RNG (sRand). The native wrapper
// was std::shuffle(first, last, default_random_engine(RandomInt())), which
// CONSUMED one sRand draw per shuffle -- shifting every later RandomInt /
// RandomFloat in MoveMgr, MiniGameMgr, FlowPickOne, MetagameRank, Jukebox --
// and ignored srand() entirely.
// ---------------------------------------------------------------------------
TEST(NativeShadowUnit, RandomShuffleLeavesTheGameRngAlone) {
    std::vector<int> v;
    for (int i = 0; i < 16; i++)
        v.push_back(i);
    SeedRand(1234);
    RandomShuffle(v.begin(), v.end());
    int afterShuffle = RandomInt();
    SeedRand(1234);
    int withoutShuffle = RandomInt();
    EXPECT_EQ(afterShuffle, withoutShuffle)
        << "RandomShuffle advanced the game RNG; the image's shuffle uses CRT rand()";
}

TEST(NativeShadowUnit, RandomShuffleIsDrivenByCrtRand) {
    std::vector<int> a, b;
    for (int i = 0; i < 16; i++) {
        a.push_back(i);
        b.push_back(i);
    }
    srand(99);
    RandomShuffle(a.begin(), a.end());
    RandomInt(); // disturb the game RNG between the two runs
    srand(99);
    RandomShuffle(b.begin(), b.end());
    EXPECT_EQ(a, b) << "same srand() seed must give the same permutation";
    // The STLport algorithm, spelled out: the permutation it must produce.
    std::vector<int> c;
    for (int i = 0; i < 16; i++)
        c.push_back(i);
    srand(99);
    for (int i = 1; i < 16; i++)
        std::swap(c[i], c[rand() % (i + 1)]);
    EXPECT_EQ(a, c);
}

} // namespace
