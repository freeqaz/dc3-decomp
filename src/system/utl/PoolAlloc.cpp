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

int gBigHunk = 0xC800;
int gSmallHunk = 0xC800;
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

// 98.214% (normalized, full ninja) -- 55 of 56 instructions.  The single charged
// row is an extra `mr r3, r11`: the image loads sPoolBuf straight into r3 and
// keeps `buf` there through the phi, so both paths return without a copy
// (`lwz r3, lbl_830E5738@l(r30)` at 0x827CF13C and `addi r3, r11, 0x40` at
// 0x827CF1D4); MSVC gives us r11 for the phi and copies at the end.  Every other
// difference is register naming, which the canonical ruler forgives.
//
// REFUTED at 98.214 (each a full ninja): `int *next = buf + words` temp; a named
// bool for the bounds condition; a separate `ret` copy before the bump; and all
// six orderings of the three head statements (buf / words / gPoolCapacity).  The
// byte-wise spelling of the two adds gets the phi into r3 but then emits the
// adds with the operands the other way round (`add r7, r11, r28` where the image
// has `add r8, r28, r3`) -- same one charged instruction, worse fuzzy (95.714 ->
// 96.786 is the only thing that moves).  Coalescing the phi onto r3 is an
// allocator decision we have not found a source lever for.
int *FixedSizeAlloc::RawAlloc(int size) {
    int *buf = sPoolBuf;
    // The pool is walked in INT UNITS, not bytes.  The image computes
    // `srawi r11, r4, 2` then `slwi r28, r11, 2` -- two separate instructions --
    // and reuses r28 for both the bounds check (`add r8, r28, r3`) and the bump
    // (`add r11, r28, r3`).  MSVC folds the byte-wise spelling `(size >> 2) << 2`
    // into a single `clrrwi`, so the image cannot have written that: a srawi/slwi
    // pair that does NOT fold is the signature of `int *` pointer arithmetic --
    // the `>> 2` is the source's, the `slwi 2` the compiler's sizeof(int)
    // scaling, emitted by different passes so they never combine.  Writing it as
    // pointer arithmetic also fixes the commutative operand order on both adds
    // (the scaled index first, the base second) and lets `buf` live in r3 from
    // the load, which is what removed our extra `mr r3, r11`.
    int words = size >> 2;
    gPoolCapacity += size;

    if (buf + words > sPoolEnd) {
        // The image reaches gSmallHunk as a +4 displacement off ONE materialized
        // base -- `addi r31, r11, ?gBigHunk@@3HA@l`, then 0x0(r31) / 0x4(r31),
        // and `?gBigHunk@@3HA` is the only relocation it names.  Two independent
        // external globals each get their own relocation, so MSVC will not
        // co-address them on its own; taking the base once and indexing is what
        // reproduces it.  Making them one struct also works but renames the
        // relocation, which costs PoolAllocInit's `FindData("big_hunk", ...)`
        // row -- measured: RawAlloc 98.036 / PoolAllocInit 99.583 for the
        // aggregate vs RawAlloc 98.214 / PoolAllocInit 100.0 for this form.
        int *hunkSizes = &gBigHunk; // [0] is gBigHunk, [1] is gSmallHunk
        if (MemNumHeaps() > 0) {
            if (hunkSizes[0] == hunkSizes[1]) {
                printf("PoolAlloc warning: allocating small pool chunk\n");
            }
            MemPushHeap(0);
        }

        sPoolBuf = (int *)_MemAllocTemp(hunkSizes[0], __FILE__, 0x71, "PoolChunk", 0);

        if (MemNumHeaps() > 0) {
            MemPopHeap();
        }

        // gBigHunk is re-read from memory here rather than cached across the
        // calls: the image loads it twice, once as the _MemAllocTemp argument and
        // once again after MemPopHeap, which is what a plain global read either
        // side of an opaque call produces.
        buf = sPoolBuf + 0x10;
        sPoolEnd = sPoolBuf + (hunkSizes[0] >> 2);
        hunkSizes[0] = hunkSizes[1];
    }

    sPoolBuf = buf + words;
    return buf;
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
