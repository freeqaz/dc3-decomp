// LP64 literal sizes in the memory subsystem (ported from rb3-xenon's W16-UB
// audit, 63407036c / d3ffc7e97).  Each test drives the real decomp body: on the
// 360 every one of these literals is the right size, natively a pointer is 8
// bytes and the literal is short.
//
// None of these paths runs in the shipped native boot (no driver calls MemInit,
// so gNumHeaps == 0 and MemAlloc falls through to malloc), which is why each
// bug was latent.  The crash-prone ones run in a re-exec'd child
// (death_test_style "threadsafe", see test_object_lifetime.cpp) so a corrupted
// heap or a truncated .bss address fails one test instead of the binary.

#include "test_helpers.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <malloc.h>
#include <unistd.h>

#include "utl/TextStream.h"
#include "utl/KeylessHash.h"
#include "utl/AllocInfo.h"

#include "utl/MemHeap.h" // reached through the PCH, so `private` stays private
#define private public
#define protected public
#include "utl/MemTracker.h"
#include "utl/MemTrack.h"
#undef private
#undef protected

extern MemTracker *gMemTracker; // utl/MemTrack.cpp; not declared in a header
void BeginMemTrackFileName(const char *);
void EndMemTrackFileName();

// MemHeap.h comes in through milo-tests' PCH, so the `#define private public`
// idiom cannot reach it. An explicit instantiation may name a private member
// ([temp.explicit]), which is the standard way to read MemHeap::mFreeBlockChain.
template <typename Tag, typename Tag::type M>
struct PrivateMember {
    friend typename Tag::type Get(Tag) { return M; }
};
struct HeapChainTag {
    typedef FreeBlock *MemHeap::*type;
    friend type Get(HeapChainTag);
};
template struct PrivateMember<HeapChainTag, &MemHeap::mFreeBlockChain>;

namespace {

FreeBlock *&Chain(MemHeap &h) { return h.*Get(HeapChainTag()); }

bool TmpWritable() {
    FILE *f = fopen("/tmp/.gtest_write_check", "w");
    if (!f)
        return false;
    fclose(f);
    remove("/tmp/.gtest_write_check");
    return true;
}

class CollectStream : public TextStream {
public:
    std::string mText;
    virtual void Print(const char *s) { mText += s; }
};

int CountOf(const std::string &text, const char *needle) {
    int n = 0;
    for (size_t pos = text.find(needle); pos != std::string::npos;
         pos = text.find(needle, pos + 1))
        n++;
    return n;
}

bool ChainInBounds(MemHeap &h, int limit, int &nodes) {
    nodes = 0;
    for (FreeBlock *f = Chain(h); f; f = f->mNextBlock) {
        if ((int *)f < h.Start() || (int *)f >= h.End() || ++nodes > limit)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// MemHeap: FreeBlock {u32 mSizeWords, u32 mTimeStamp, FreeBlock *mNextBlock}
// is 3 words on the 360 and 4 natively.  GetSizeWords' minimum block, the
// debug fills in Init / Free / Truncate / AttemptMerge, and Print's read of
// the chain pointer all assumed 3.
// ---------------------------------------------------------------------------
void MemHeapMinBlockChild() {
    alarm(30); // a mis-read chain pointer can loop forever (Print); fail, don't hang
    const int kBytes = 64 * 1024;
    int *buf = (int *)aligned_alloc(16, kBytes);
    memset(buf, 0, kBytes);
    MemHeap h;
    // debugLevel 1: every fill site is live.
    h.Init("lp64", 0, buf, kBytes / 4, false, MemHeap::kFirstFit, 1, false);
    if (!Chain(h) || Chain(h)->mNextBlock != nullptr) {
        fprintf(stderr, "Init's fill reached the initial block's mNextBlock\n");
        _exit(2);
    }
    const int initialFree = Chain(h)->mSizeWords;
    const int sizeWords = MemHeap::GetSizeWords(4); // the minimum block
    const int alignWords = MemHeap::GetAlignWords(4);
    if (sizeWords * 4 < (int)sizeof(FreeBlock)) {
        fprintf(stderr, "minimum block %d words cannot hold a %zu-byte FreeBlock\n",
                sizeWords, sizeof(FreeBlock));
        _exit(3);
    }

    const int kBlocks = 256;
    std::vector<int *> blocks;
    std::vector<int> sizes;
    for (int i = 0; i < kBlocks; i++) {
        int got = 0;
        int *b = h.Alloc(sizeWords, alignWords, got);
        if (!b)
            _exit(4);
        *b = 0x55000000 | i;
        blocks.push_back(b);
        sizes.push_back(h.AllocSize(b));
    }
    // Free every other block: each sits between two live ones, so it cannot
    // merge and stays exactly the minimum size.
    for (int i = 0; i < kBlocks; i += 2)
        h.Free(blocks[i]);
    int badHeader = 0, badWord = 0;
    for (int i = 1; i < kBlocks; i += 2) {
        badHeader += h.AllocSize(blocks[i]) != sizes[i];
        badWord += *blocks[i] != (0x55000000 | i);
    }
    if (badHeader || badWord) {
        fprintf(stderr, "freeing minimum blocks changed %d live headers, %d live words\n",
                badHeader, badWord);
        _exit(5);
    }
    int chain = 0;
    if (!ChainInBounds(h, kBlocks + 4, chain) || chain != kBlocks / 2 + 1) {
        fprintf(stderr, "free chain left the heap or has %d nodes\n", chain);
        _exit(6);
    }
    // Print walks the same chain through its own read of mNextBlock.
    CollectStream dump;
    h.Print(dump, true);
    const int printed = CountOf(dump.mText, " FREE ");
    if (printed != chain) {
        fprintf(stderr, "Print listed %d free blocks, chain has %d\n", printed, chain);
        _exit(7);
    }

    // Truncate: the tail it frees is followed by a live block, so it cannot
    // merge, and its mNextBlock must survive Truncate's own fill. The guard is
    // too big for the minimum-size holes, so it lands right after `big`.
    int got = 0;
    int *big = h.Alloc(64, alignWords, got);
    int *guard = h.Alloc(16, alignWords, got);
    if (!big || guard != big + ((unsigned int)big[-1] >> 8)) {
        fprintf(stderr, "guard block is not adjacent to the truncated block\n");
        _exit(11);
    }
    *guard = 0x66666666;
    h.Truncate(big, 4, got);
    if (!ChainInBounds(h, kBlocks + 8, chain) || *guard != 0x66666666) {
        fprintf(stderr, "Truncate's fill corrupted the free chain (%d nodes)\n", chain);
        _exit(9);
    }
    h.Free(guard);
    h.Free(big);

    // Free the rest: every free merges (AttemptMerge's fill) back to one block.
    for (int i = 1; i < kBlocks; i += 2)
        h.Free(blocks[i]);
    if (!Chain(h) || Chain(h)->mNextBlock != nullptr
        || (int)Chain(h)->mSizeWords != initialFree) {
        fprintf(stderr, "after freeing everything the heap is not one %d-word block\n",
                initialFree);
        _exit(10);
    }
    // debugLevel 1 fills every word of a free block past its header. Each
    // merge (AttemptMerge) must also fill the absorbed block's whole header.
    int *whole = (int *)Chain(h);
    const int headerWords = (int)((sizeof(FreeBlock) + 3) / 4);
    int unfilled = 0;
    for (int *w = whole + headerWords; w < whole + initialFree; w++)
        unfilled += *w != (int)0xDEADDEAD;
    if (unfilled) {
        fprintf(stderr, "%d words of the merged free block escaped the debug fill\n",
                unfilled);
        _exit(12);
    }
    free(buf);
    _exit(0);
}

// Symbol + MakeString init: Print formats through MakeString.
class NativeMemoryLP64 : public SymbolTestFixture {};

TEST_F(NativeMemoryLP64, MemHeapMinimumFreeBlockHoldsAWholeFreeBlock) {
    if (!TmpWritable())
        GTEST_SKIP() << "/tmp not writable (sandbox) - ASSERT_EXIT needs /tmp access";
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    ASSERT_EXIT(MemHeapMinBlockChild(), ::testing::ExitedWithCode(0), "")
        << "a 3-word FreeBlock assumption corrupted the heap (sizeof(FreeBlock) = "
        << sizeof(FreeBlock) << ")";
}

// ---------------------------------------------------------------------------
// MemTracker: the hash table is 2 * numAllocs AllocInfo* entries, all cleared
// by the KeylessHash ctor, in a block of numAllocs * 8 bytes -- half the table
// natively.
// ---------------------------------------------------------------------------
void MemTrackerHashChild() {
    alarm(30);
    MemTracker *t = new MemTracker(0, 64);
    const size_t need = (size_t)t->mHashTable->Size() * sizeof(AllocInfo *);
    const size_t have = malloc_usable_size(t->mHashMem);
    if (have < need) {
        fprintf(stderr, "hash table needs %zu bytes, mHashMem holds %zu\n", need, have);
        _exit(2);
    }
    _exit(0);
}

TEST_F(NativeMemoryLP64, MemTrackerHashBlockHoldsTheWholeTable) {
    if (!TmpWritable())
        GTEST_SKIP() << "/tmp not writable (sandbox) - ASSERT_EXIT needs /tmp access";
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    ASSERT_EXIT(MemTrackerHashChild(), ::testing::ExitedWithCode(0), "")
        << "MemTracker's hash block is smaller than its KeylessHash table";
}

// ---------------------------------------------------------------------------
// MemTrack name stacks: 65 pointer slots each (STACK_SIZE 64, slot 0 unused by
// Begin).  char[260] holds 32 eight-byte pointers, and MemTrackInit stored the
// slot buffers through `(int)CharArrayArray + i`, a truncated .bss address,
// at a 4-byte stride.
// ---------------------------------------------------------------------------
void MemTrackStacksChild() {
    alarm(30);
    MemTrackInit(0, 64, false);
    if (!gMemTracker)
        _exit(2);
    static char names[65][16];
    for (int i = 1; i <= 64; i++) {
        snprintf(names[i], sizeof(names[i]), "lp64_f%02d", i);
        BeginMemTrackFileName(names[i]);
    }
    // Each Begin pushed the previous name; each End pops back to it.
    int wrong = 0;
    for (int i = 64; i >= 1; i--) {
        EndMemTrackFileName();
        const char *want = i > 1 ? names[i - 1] : "";
        wrong += strcmp(gMemTracker->unk181ac.c_str(), want) != 0;
    }
    if (wrong) {
        fprintf(stderr, "%d of 64 pops restored the wrong file name\n", wrong);
        _exit(3);
    }
    _exit(0);
}

TEST_F(NativeMemoryLP64, MemTrackNameStacksHold65Pointers) {
    if (!TmpWritable())
        GTEST_SKIP() << "/tmp not writable (sandbox) - ASSERT_EXIT needs /tmp access";
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    ASSERT_EXIT(MemTrackStacksChild(), ::testing::ExitedWithCode(0), "")
        << "MemTrackInit / the name stacks are sized for 4-byte pointers";
}

} // namespace
