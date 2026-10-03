#include "utl\PoolAlloc.h"
#include "MemMgr.h"
#include "math\Utl.h"
#include <cstdio>
#include <cstdlib>
#include "os\CritSec.h"
#include "os\Debug.h"
#include "obj\Data.h"
#include "utl\TextStream.h"
#include "utl\Std.h"

// w14-l: FILE STATICS.  Neither is in the linker map (which names every public
// global), and RawAlloc reaches gSmallHunk as +0x4 off the one base it
// materializes for gBigHunk (827CF164 `addi r31, r11, gBigHunk@l`, then
// 0x0(r31) / 0x4(r31)) -- MSVC only co-addresses two globals when both have
// internal linkage.  Their .data is the image's 0x82F189D0 / 0x82F189D4, which
// splits.txt used to file under MemMgr's range.
static int gBigHunk = 0xC800;
static int gSmallHunk = 0xC800;
int gPoolCapacity = 0;
bool gPoolAllocInitted = 0;
ChunkAllocator *gChunkAlloc = nullptr;
// NOT file statics: the image hoists a SEPARATE `lis` for each of these and
// keeps both @ha bases live across the calls in RawAlloc (r30 for sPoolBuf, r29
// for sPoolEnd).  MSVC co-addresses internal-linkage statics it has laid out
// itself -- which is what produced our `addi r31, r11, sPoolBuf@l` + 0x4(r31)
// pair -- but gives each EXTERNAL symbol its own relocation.  The mirror image
// of the gBigHunk/gSmallHunk case above, and between them the two halves of
// RawAlloc's residual.  .bss order (image: gPoolCapacity 0x830E5728,
// gPoolAllocInitted 0x572C, gChunkAlloc 0x5730, sPoolEnd 0x5734, sPoolBuf
// 0x5738) is declaration order, so keep sPoolEnd first.
int *sPoolEnd;
int *sPoolBuf;

void PoolAllocInit(DataArray *a) {
    a->FindData("big_hunk", gBigHunk);
    gPoolAllocInitted = true;
}

void *
PoolAlloc(int classSize, int reqSize, const char *file, int line, const char *name) {
    MILO_ASSERT_FMT(classSize >= 0, "PoolAlloc class size is < 0: %d", classSize);
#ifdef HX_NATIVE
    // On 64-bit native, the pool allocator's free list uses int-sized slots
    // which can't hold 64-bit pointers. Just use malloc instead.
    return malloc(reqSize);
#else
    CritSecTracker tracker(gMemLock);
    if (!gChunkAlloc) {
        gChunkAlloc = new ChunkAllocator();
    }
    MILO_ASSERT(reqSize == classSize, 0x15F);
    void *alloced = gChunkAlloc->Alloc(classSize);
    MemTrackAlloc(classSize, classSize, name, alloced, true, 0, file, line);
    return alloced;
#endif
}

void PoolFree(int idx, void *mem, const char *file, int line, const char *name) {
#ifdef HX_NATIVE
    free(mem);
#else
    CritSecTracker tracker(gMemLock);
    MemTrackFree(mem);
    MILO_ASSERT(gChunkAlloc, 0x16F);
    gChunkAlloc->Free(mem, idx);
#endif
}

void PoolReport(TextStream &ts) {
    CritSecTracker tracker(gMemLock);
    MILO_ASSERT(gChunkAlloc, 0x179);
    gChunkAlloc->Print(ts);
}

#pragma region FixedSizeAlloc

FixedSizeAlloc::FixedSizeAlloc(int allocSizeWords, int nodesPerChunk)
    : mAllocSizeWords(allocSizeWords), mNumAllocs(0), mMaxAllocs(0), mNumChunks(0),
      mFreeList(nullptr), mNodesPerChunk(nodesPerChunk) {
    MILO_ASSERT(mAllocSizeWords != 0, 0x9D);
}

void *FixedSizeAlloc::Alloc() {
    if (!mFreeList) {
        Refill();
    }
    int *ret = mFreeList;
    int numAllocs = mNumAllocs + 1;
    int *next = (int *)*ret;
    mNumAllocs = numAllocs;
    mFreeList = next;
    if (numAllocs > mMaxAllocs) {
        mMaxAllocs = numAllocs;
    }
    return ret;
}

void FixedSizeAlloc::Free(void *v) {
    *(int **)v = mFreeList;
    mFreeList = (int *)v;
    MILO_ASSERT_FMT(mNumAllocs > 0, "mNumAllocs is %d", mNumAllocs);
    mNumAllocs--;
}

// w21-bc: 98.214 -> 100 with RB3's ChunkAllocator::RawPoolAlloc shape: no
// `buf` local held across the refill -- the refill bumps the GLOBAL
// (`sPoolBuf += 0x10`) and the tail reads it back (`ret = sPoolBuf; sPoolBuf +=
// words`). MSVC then coalesces the phi onto r3 (image 0x827CF13C `lwz r3,
// sPoolBuf` / 0x827CF1D4 `addi r3, r11, 0x40`) and the extra `mr r3, r11` is
// gone. Same values stored and returned as the old `buf` spelling.
//
// The pool is walked in INT UNITS: the image's `srawi r11, r4, 2` / `slwi r28,
// r11, 2` pair (which MSVC would fold to one clrrwi for the byte-wise
// `(size >> 2) << 2`) is the source's `>> 2` plus the compiler's sizeof(int)
// scaling of `int *` arithmetic. Same at sPoolEnd. gBigHunk is read again after
// the calls (the image loads it twice).
int *FixedSizeAlloc::RawAlloc(int size) {
    int words = size >> 2;
    gPoolCapacity += size;

    if (sPoolBuf + words > sPoolEnd) {
        if (MemNumHeaps() > 0) {
            if (gBigHunk == gSmallHunk) {
                printf("PoolAlloc warning: allocating small pool chunk\n");
            }
            MemPushHeap(0);
        }

        sPoolBuf = (int *)_MemAllocTemp(gBigHunk, __FILE__, 0x71, "PoolChunk", 0);

        if (MemNumHeaps() > 0) {
            MemPopHeap();
        }

        sPoolEnd = sPoolBuf + (gBigHunk >> 2);
        sPoolBuf += 0x10;
        gBigHunk = gSmallHunk;
    }

    int *ret = sPoolBuf;
    sPoolBuf += words;
    return ret;
}

void FixedSizeAlloc::Refill() {
    MILO_ASSERT(mFreeList == 0, 0xCA);
    int allocSize = mAllocSizeWords * mNodesPerChunk;
    mFreeList = RawAlloc(allocSize * 4);
    mNumChunks++;

    int *cur = mFreeList;
    int *end = mFreeList + (allocSize - mAllocSizeWords);
    while (cur < end) {
        int *next = cur + mAllocSizeWords;
        *cur = (int)next;
        cur = next;
    }
    *cur = 0;
}

#pragma endregion
#pragma region ChunkAllocator

ChunkAllocator::ChunkAllocator() {
    for (int i = 0; i < MAX_FIXED_ALLOCS; i++) {
        mAllocs[i] = new FixedSizeAlloc((i + 1) * 4, 20);
    }
}

void *ChunkAllocator::Alloc(int idx) {
    int fixedSizeIndex = (idx - 1) >> 4;
    MILO_ASSERT(fixedSizeIndex < MAX_FIXED_ALLOCS, 0x116);
    return mAllocs[fixedSizeIndex]->Alloc();
}

void ChunkAllocator::Free(void *v, int idx) {
    int fixedSizeIndex = (idx - 1) >> 4;
    MILO_ASSERT(fixedSizeIndex < MAX_FIXED_ALLOCS, 0x122);
    MILO_ASSERT(mAllocs[fixedSizeIndex], 0x123);
    mAllocs[fixedSizeIndex]->Free(v);
}

void ChunkAllocator::Print(TextStream &ts) {
    ts << MakeString("\n*** POOL REPORT (Total Capacity: %d)***\n", gPoolCapacity);
    ts << MakeString("   NodeSize   NumAllocs  MaxAllocs  Capacity  Wasted\n");
    int wasted = 0;
    for (int i = 0; i < 64; i++) {
        if (mAllocs[i]) {
            FixedSizeAlloc *cur = mAllocs[i];
            int numAllocs = cur->mNumAllocs;
            int capacity = cur->mNodesPerChunk * cur->mNumChunks;
            int maxAllocs = cur->mMaxAllocs;
            int nodeSize = cur->mAllocSizeWords * 4;
            int curWasted = (capacity - cur->mNumAllocs) * cur->mAllocSizeWords * 4;
            ts << MakeString(
                "   %8d  %8d  %8d  %8d  %8d\n",
                nodeSize,
                numAllocs,
                maxAllocs,
                capacity,
                curWasted
            );
            wasted += curWasted;
        }
    }
    ts << MakeString("                             Total Waste = %8d\n", wasted);
}

#pragma endregion
#pragma region ReclaimableAlloc

ReclaimableAlloc::ReclaimableAlloc(int x, const char *name)
    : FixedSizeAlloc(((x + 15) >> 2) & ~3, 0x2800 / x), mName(name) {}

int *ReclaimableAlloc::RawAlloc(int num) {
    void *alloced = MemAlloc(num, __FILE__, 0x196, mName);
    mChunks.push_back(alloced);
    return (int *)alloced;
}

void *ReclaimableAlloc::CustAlloc(int bytes) {
    MILO_ASSERT(bytes <= mAllocSizeWords * 4, 0x188);
    return Alloc();
}

void ReclaimableAlloc::CustFree(void *mem) {
    Free(mem);
    if (mNumAllocs == 0) {
        DeallocAll();
    }
}

void ReclaimableAlloc::DeallocAll() {
    MILO_ASSERT(mNumAllocs == 0, 0x19D);
    FOREACH (it, mChunks) {
        MemFree(*it);
    }
    mChunks.clear();
    mNumChunks = 0;
    mFreeList = nullptr;
}
