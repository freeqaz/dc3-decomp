#include "utl\MemHeap.h"
#include "math\Utl.h"
#include "os\Debug.h"
#include "os\OSFuncs.h"
#include "os\CritSec.h"
#include "utl\MakeString.h"
#include "utl\MemMgr.h"
#include "utl\MemTracker.h"
#include "utl\TextStream.h"
#include "utl\AllocInfo.h"
#include "utl\MemTrack.h"
#include <cstdio>

namespace {
    int gTimeStamp;

    void PrintAlloc(TextStream &ts, int *ptr, int size, int count, const AllocInfo *info) {
        if (count > 0) {
            const char *str;
            if (count == 1) {
                str = MakeString("(%p ALLOC (size %6i)", ptr, size);
            } else {
                str = MakeString("(%p ALLOC (size %6i %i)", ptr, size, count);
            }
            ts << str;
            if (info != nullptr) {
                for (int i = 0; i < 0x10 && info->mStackTrace[i] != 0; i++) {
                    ts << *info;
                }
            }
            ts << MakeString(")\n");
        }
    }
}

int MemHeap::GetSizeWords(int size) {
    unsigned int words = ((size + 3) >> 2) + 1;
    if (words >= 3)
        return words;
    return 3;
}

void MemHeap::FreeBlockStats(int &lFrags, int &rFrags, int &freeBytes, int &i4, int &i5) {
    int i = 0;
    int ivar5 = 0;
    int ivar3 = 0;
    int ivar6 = -1;
    for (FreeBlock *it = mFreeBlockChain; it != nullptr; it = it->mNextBlock, i++) {
        int size = it->mSizeWords * 4;
        if (ivar5 < size) {
            ivar5 = size;
            ivar6 = i;
        }
        ivar3 += size;
    }
    freeBytes = ivar3;
    i5 = ivar5;
    lFrags = ivar6;
    rFrags = (i - ivar6) - 1;
    mMinFreeBytes = Min<unsigned int>(ivar3, mMinFreeBytes);
    i4 = mMinFreeBytes;
}

void MemHeap::Print(TextStream &ts, bool verbose) {
    ts << MakeString(";---------------------------------------\n");
    const char *heapInfo = MakeString("; HEAP: %i (%s), starts %p, %d bytes\n", mNum, mName, mStart, mSizeWords * 4);
    ts << heapInfo;
    // REFUTED (w7-aq): swapping the lFrags/rFrags declaration order does not
    // move the 0x54/0x58 slot pair the image uses (`addi r4, r1, 0x58` /
    // `addi r5, r1, 0x54` at both FreeBlockStats call sites); the 4 offset rows
    // are unchanged to five decimals either way.
    int rFrags, lFrags, freeBytes, maxFreeIdx, minFreeBytes;
    FreeBlockStats(lFrags, rFrags, freeBytes, maxFreeIdx, minFreeBytes);
    ts << MakeString("\n");
    ts << MakeString(
        ";   lFrags =  %8d\n;   rFrags =  %8d\n;   Total Free Bytes=  %8d\n",
        lFrags,
        rFrags,
        freeBytes
    );
    // RESIDUAL (w7-aq, 96.601 canonical): the only structural rows left are
    // this read's placement -- we hoist `lwz mStart` and its `stw ..., 0x50(r1)`
    // spill above the FormatString block (idx 57/58) where the image does both
    // after it (idx 66/73), which shifts the four `li 0` initialisers by two
    // slots.  Everything else is register permutation.
    // NEGATIVE RESULT (w7-aq, 2026-09-14): the image loads mSizeWords (0xc)
    // first and mStart (0x4) second, both AFTER this MakeString("\n") write
    // (`lwz r10, 0xc(r31)` / `lwz r11, 0x4(r31)` / `slwi` / `add r20, r10,
    // r11`), while we hoist the 0x4 load above the call into r28.  Sinking
    // `curPtr = mStart` below the write costs 2pp (91.796 -> 89.8), with or
    // without an `int sizeWords = mSizeWords;` temp to force the load order --
    // it converts one insert/delete pair into two and re-splits the r10/r11
    // pair across the whole loop.  Same result w7-z measured independently.
    // curPtr is `int *`, not `unsigned int *`: MakeString takes every argument
    // by const reference, so the image passes `&curPtr` itself (`addi r4, r1,
    // 0x50`, idx 111) and therefore keeps the variable live in slot 0x50 --
    // written on loop entry (idx 72) and after every increment (idx 152).  A
    // `(int *)curPtr` cast at the call site materialises a *temporary* instead,
    // which is why our build spilled once, just before the call.
    int *curPtr = mStart;

    ts << MakeString("\n");
    int curAllocCount = 0;
    int *curAllocPtr = nullptr;
    int curAllocSize = 0;
    int *endPtr = curPtr + mSizeWords;
    const AllocInfo *curAllocInfo = nullptr;
    unsigned int blockSizeWords = 0;

    unsigned int *curFreeBlock = (unsigned int *)mFreeBlockChain;
    // `curPtr` itself is the pointer handed to the free-block MakeString, whose
    // args are all `const&` -- so the image ADDRESS-TAKES curPtr and pins it to
    // stack slot 0x50, writing it back on entry (827F8... `stw r11, 0x50(r1)`,
    // idx 72) and after every increment (`stw r21, 0x50(r1)`, idx 152), while
    // still caching it in r21.  A separate `savedCurPtr` copy would take the
    // slot instead and leave curPtr purely in a register.
    for (; curPtr < endPtr; curPtr += blockSizeWords) {
        if (curFreeBlock == nullptr || curPtr != (int *)curFreeBlock) {
            // Alloc block
            unsigned int *headerPtr = (unsigned int *)curPtr;
            // The image re-loads *headerPtr after the scan loop (idx 126
            // `lwz r10, 0x0(r11)`) instead of reusing the value the loop's
            // `lwzu` left in a register, so the shift reads the dereference
            // directly rather than a `hdr` local carried out of the loop.
            while ((int)*headerPtr == 0) {
                headerPtr++;
            }
            blockSizeWords = *headerPtr >> 8;

            if (!verbose) {
                int *newPtr = (int *)(headerPtr + 1);
                const AllocInfo *newInfo = MemTrackGetInfo(newPtr);
                int newSize = blockSizeWords << 2;
                if (newSize == curAllocSize) {
                    curAllocCount++;
                } else {
                    PrintAlloc(ts, curAllocPtr, curAllocSize, curAllocCount, curAllocInfo);
                    curAllocPtr = newPtr;
                    curAllocSize = newSize;
                    curAllocCount = 1;
                    curAllocInfo = newInfo;
                }
            }
        } else {
            // Free block
            PrintAlloc(ts, curAllocPtr, curAllocSize, curAllocCount, curAllocInfo);
            // The image clears curAllocSize HERE, before freeStr is set up and
            // before curAllocCount (idx 95 `li r29, 0x0`, 96 `stw r16, 0x54`,
            // 98 `li r28, 0x0`) -- not at the bottom of the branch next to the
            // blockSizeWords update.
            curAllocSize = 0;
            const char *freeStr = " ; **** big free block!";
            curAllocCount = 0;
            unsigned int sizeWords = *curFreeBlock;
            int blockSize = sizeWords << 2;
            if (blockSize < 100000) {
                freeStr = "";
            }
            unsigned int timeStamp = curFreeBlock[1];
            ts << MakeString(
                "(%p FREE  (size %6d) (time %5d))%s\n",
                curPtr,
                blockSize,
                timeStamp,
                freeStr
            );
            curFreeBlock = (unsigned int *)curFreeBlock[2];
            blockSizeWords = sizeWords;
        }
    }

    PrintAlloc(ts, curAllocPtr, curAllocSize, curAllocCount, curAllocInfo);
    ts << MakeString("\n\n");
}

void MemHeap::InsertFreeBlock(
    FreeBlock *iBlock, int size, FreeBlock *iPrevBlock, FreeBlock *iNextBlock, int time
) {
    MILO_ASSERT((iBlock != iPrevBlock) && (iBlock != iNextBlock), 0x68);
    iBlock->mSizeWords = size;
    iBlock->mNextBlock = iNextBlock;
    iBlock->mTimeStamp = time;
    if (iPrevBlock) {
        iPrevBlock->mNextBlock = iBlock;
    } else {
        mFreeBlockChain = iBlock;
    }
}

void MemHeap::Init(
    const char *name,
    int num,
    int *start,
    int size,
    bool handle,
    Strategy strat,
    int debugLevel,
    bool allowTemp
) {
    MILO_ASSERT_FMT(start, "Could not allocate %d bytes for heap %s\n", size * 4, name);
    // RESOLVED (w7-bn, 2026-09-15): 83.0 -> 100.0 canonical, 78 rows all
    // equal, 312 B both sides.  The lever is the OPACITY of the first
    // mStart write, not a read-back.  `int **pStart = &mStart; *pStart =
    // start;` is a store the early optimizer will not equate with
    // this->mStart, so (a) the later direct `mStart = alignedStart` cannot
    // dead-store-eliminate it (827F87BC survives), (b) the adjacent direct
    // read `int *rawStart = mStart;` stays a real load through the early
    // passes and is forwarded LATE by the machine-level stw->lwz peephole,
    // which keeps lwz's zero-extension -- that is the `clrrwi r10, r30, 0`
    // at 827F87C0 -- and (c) the stack-argument load `lbz r8, 0xf7(r1)`
    // (827F87C4) cannot hoist above an opaque store, which is what pins the
    // whole store window (827F87BC-827F8810) into the image's order.
    // Measured on the way (each with run_objdiff in this worktree):
    //   direct store + read through *pStart, adjacent          89.7 (clrrwi
    //     present, both sides 312 B, but lbz hoisted above the store);
    //   *pStart store + *pStart read, adjacent                  89.0 (read
    //     forwarded early, no clrrwi, dead `addi r11, r31, 0x4`);
    //   *pStart for both stores and the read, 3 stores between 66.4 (read
    //     stays a real lwz);
    //   direct store + direct read + aligned store via *pStart  74.1 (read
    //     forwarded early as bare r30);
    //   mAllowTemp moved between the stores; `*(unsigned int *)&mStart`
    //     read; (unsigned long long) casts on the subtraction; reading
    //     (mStart - rawStart) after the aligned store: all byte-identical
    //     to their parent spelling.
    // The paragraphs below are the history that led here; they are kept
    // because every negative in them is still true of a DIRECT read-back.
    // RESIDUAL (w7-aq, 83.013 canonical): the image writes mStart TWICE --
    // 827F87BC stores the raw `start`, 827F87E8 overwrites it with the
    // 16-byte-aligned pointer -- and also carries a `clrrwi r10, r30, 0` copy
    // of `start` (827F87C0).  Our build dead-store-eliminates the first write,
    // and the two missing instructions drag the whole store-scheduling window
    // (idx 27-56) out of alignment; the rest of the function is exact.
    // REFUTED (w7-aq): `auto &ref = mStart` around both writes (the spelling
    // that was here before), computing the aligned pointer from `mStart`
    // rather than from `start`, and routing it through an `int *rawStart =
    // mStart;` local -- all three still DSE the first store, all three read
    // 83.013 to five decimals.
    // NEGATIVE RESULT (w7-bi, 2026-09-14): a fourth spelling, aimed at the
    // OTHER missing instruction.  `clrrwi r10, r30, 0` (827F87C0) is a
    // zero-extended COPY of `start`, and its only consumer is the mSizeWords
    // subtraction at 827F87EC -- so it is the SIZE, not the alignment, that
    // the image computes from a value which went through a 32-bit conversion.
    // Routing only that subtraction through a read-back (`int *rawStart =
    // mStart;` placed after `mStrategy` and before the aligned store, with the
    // alignment still taken from `start` so 827F87B8 `subi r11, r30, 0x4`
    // keeps pairing) is ALSO dead-store-eliminated: identical 83.0 canonical,
    // identical 83-row table, same 2-instruction deficit (312 B target vs
    // 304 B base).  MSVC forwards the store to the read and then removes it,
    // so no read-back spelling can keep it alive.  The entire residual is
    // those 2 absent instructions plus the member-store reshuffle they cause
    // (idx 27-56); idx 0-26 and 57-82 are exact on both sides.
    // Opaque first write + direct read-back: see the RESOLVED note above.
    // Do NOT "simplify" to `mStart = start;` -- that spelling is DSE'd and
    // reads 83.0.
    int **pStart = &mStart;
    *pStart = start;
    int *rawStart = mStart;
    mName = name;
    mNum = num;
    mIsHandleHeap = handle;
    int *alignedStart = (int *)(((uintptr_t)start - 4 & ~(uintptr_t)0xFU) + 0x10);
    mStrategy = strat;
    mStart = alignedStart;
    mAllowTemp = allowTemp;
    mMinFreeBytes = -1;
    mDebugLevel = debugLevel;
    mSizeWords = size - (alignedStart - rawStart);
    // POST-increment: 827F8814 reads gTimeStamp into r8, 827F8818/1C store
    // r8+1 back, and r8 -- the OLD value -- is what reaches InsertFreeBlock.
    InsertFreeBlock((FreeBlock *)mStart, mSizeWords, nullptr, nullptr, gTimeStamp++);
    if (1 <= mDebugLevel) {
        FreeBlock *blockStart = mFreeBlockChain;
        int *blockStartInt = (int *)blockStart;
        int *blockEnd = blockStartInt + blockStart->mSizeWords;
        for (int *ptr = blockStartInt + 3; ptr < blockEnd; ptr++) {
            *ptr = 0xDEADDEAD;
        }
    }
}

int MemHeap::AllocSize(int *ptr) {
    if ((ptr >= mStart) && (ptr < mStart + mSizeWords)) {
        unsigned int header = *(unsigned int *)(ptr - 1);
        unsigned int blockSizeWords = header >> 8;
        unsigned int blockSizeControl = (header >> 4) & 0xF;
        return (blockSizeWords - blockSizeControl - 1) * 4;
    }
    return 0;
}

void MemHeap::FirstFit(int size, int align, FreeBlockInfo &blockinfo) {
    FreeBlock *prev = nullptr;
    for (FreeBlock *block = mFreeBlockChain; block != nullptr; block = block->mNextBlock) {
        // Calculate the data start position (after FreeBlock header)
        intptr_t start = ((intptr_t)block >> 2) + 1;
        // Calculate padding needed to align data to (1 << align) bytes
        intptr_t pad = ((((uintptr_t)(1 << align) + start) - 1) >> align) << align;
        pad = pad - start;
        if ((int)block->mSizeWords >= pad + size) {
            blockinfo.mSizeWords = block->mSizeWords;
            blockinfo.mPadWords = pad;
            blockinfo.mBlock = block;
            blockinfo.mPrevBlock = prev;
            return;
        }
        prev = block;
    }
}

void MemHeap::LastFit(int size, int align, FreeBlockInfo &blockinfo) {
    FreeBlock *block = mFreeBlockChain;
    FreeBlock *prev = nullptr;
    if (block == nullptr) {
        return;
    }
    int alignShift = align + 2;
    do {
        intptr_t blockAddr = (intptr_t)block;
        int blockSize = block->mSizeWords;
        intptr_t allocEnd = blockAddr + (blockSize - size) * 4;
        intptr_t alignedEnd = (allocEnd >> alignShift) << alignShift;
        int pad = (int)(((alignedEnd - blockAddr) - 4) >> 2);

        if (pad >= 0) {
            blockinfo.mSizeWords = blockSize;
            blockinfo.mPadWords = pad;
            blockinfo.mBlock = block;
            blockinfo.mPrevBlock = prev;
        }
        prev = block;
        block = block->mNextBlock;
    } while (block != nullptr);
}

void MemHeap::BestFit(int size, int align, FreeBlockInfo &blockinfo) {
    FreeBlock *block = mFreeBlockChain;
    FreeBlock *prev = nullptr;
    if (block == nullptr) {
        return;
    }
    do {
        int blockSize = (int)block->mSizeWords;
        // Calculate the data start position (after FreeBlock header)
        intptr_t start = ((intptr_t)block >> 2) + 1;
        // Calculate padding needed to align data to (1 << align) bytes
        intptr_t pad = ((((uintptr_t)(1 << align) + start) - 1) >> align) << align;
        pad = pad - start;
        // Track the best fit: smallest block that satisfies size requirement
        if ((blockSize >= pad + size) && (blockSize < blockinfo.mSizeWords)) {
            blockinfo.mSizeWords = blockSize;
            blockinfo.mPadWords = pad;
            blockinfo.mBlock = block;
            blockinfo.mPrevBlock = prev;
        }
        prev = block;
        block = block->mNextBlock;
    } while (block != nullptr);
}

void MemHeap::LRUFit(int size, int align, FreeBlockInfo &blockinfo) {
    int bestTime = 0x7FFFFFFF;
    FreeBlock *prev = nullptr;
    for (FreeBlock *block = mFreeBlockChain; block != nullptr; ) {
        int ts = block->mTimeStamp;
        intptr_t start = ((intptr_t)block >> 2) + 1;
        intptr_t pad = ((((uintptr_t)(1 << align) + start) - 1) >> align) << align;
        pad = pad - start;
        if ((int)block->mSizeWords >= pad + size && ts < bestTime) {
            blockinfo.mSizeWords = block->mSizeWords;
            blockinfo.mPadWords = pad;
            blockinfo.mBlock = block;
            blockinfo.mPrevBlock = prev;
            bestTime = ts;
        }
        prev = block;
        block = block->mNextBlock;
    }
}

int MemHeap::GetAlignWords(int bytes) {
    if (bytes == 0)
        return 1;
    else {
        int num;
        int isOdd;
        int tmp = bytes;

        for (num = 0, isOdd = 0; tmp > 1; tmp >>= 1) {
            if (tmp & 1) {
                isOdd = 1;
            }
            num++;
        }
        return Max(0, num + isOdd - 2);
    }
}

// w7-bu (2026-09-15): 90.007 -> 93.3 canonical.  Three of w7-aq's four
// residual rows were spellings, not allocation: the default-arm tail merge
// (block starts null, default arm `break`s), the split header built as a
// register FreeBlock temp and stored whole (loads-before-stores + the
// forwarded-reload `clrrwi r8, r11, 0` at 827F89B0), and `totalUsed =
// blockSize` at the top of its arm (`mr r27, r28` at 827F8A08).  See the
// in-body comments for the measurements.
// RESIDUAL (w7-bu, 93.3 canonical, 45 rows): one register plan.  The image
// puts `this` in r26 and sizeWords in r27 (827F88A4/827F88AC), we get the
// pair the other way round (15 rows); it loads info.mBlock straight into r31
// and copies the OLD block to r29 inside the split arm just before the
// in-place `stwux r28, r31, r10` (827F89A0/827F89A8), we keep the loaded
// value as the survivor and copy the advanced pointer at the top (`mr r31,
// r30`, +`add`/`stwx` instead of `stwux`); it keeps padWords in r30 and
// copies it into r5 for the call (827F89A4), we load into r5 and copy the
// survivor out.  Measured inert: `sizeWords += padWords` in place of the
// totalUsed local (byte-identical), swapping the two header-load
// declarations (byte-identical), a `newBlock` local with `block = newBlock`
// after the call (identical rows), reassigning prevBlock before the call
// through an oldPrev temp (88.8).  The remaining 0x4/0x8 load+store order
// (image next@0x8 first, ours ascending) rides on the same stwux plan: with
// the base register updated in place the scheduler orders the two stores
// after it differently.  Declaration order is not a lever -- both swapped
// values are parameters.
int *MemHeap::TryAlloc(int sizeWords, int align, int &allocSize) {
    FreeBlockInfo info;
    info.mBlock = nullptr;
    info.mPrevBlock = nullptr;
    info.mSizeWords = 0x7FFFFFFF;
    info.mPadWords = 0x7FFFFFFF;

    // block starts null and the default arm falls out of the switch: MSVC
    // then jump-threads the known-null path straight into the null check's
    // `li r3, 0` (827F8914 `b .L_827F8970`), which is the image's shape.  A
    // `return nullptr` in the default arm tail-merges the other way round
    // (the null check branches BACK into the default arm's copy).
    FreeBlock *block = nullptr;
    switch (mStrategy) {
    case kFirstFit: FirstFit(sizeWords, align, info); block = info.mBlock; break;
    case kBestFit:  BestFit(sizeWords, align, info); block = info.mBlock; break;
    case kLRUFit:   LRUFit(sizeWords, align, info); block = info.mBlock; break;
    case kLastFit:  LastFit(sizeWords, align, info); block = info.mBlock; break;
    default:
        MILO_ASSERT(false, 0x151);
        break;
    }

    if (block == nullptr) return nullptr;

    FreeBlock *prevBlock = info.mPrevBlock;
    int padWords = info.mPadWords;
    // blockSize is assigned in BOTH arms, never before the branch: the image
    // loads info.mSizeWords twice (827F8988 inside the split arm, 827F89D0 in
    // the else arm). Hoisting it to a single initialiser above the `if` costs
    // the second load and forces an extra live copy of padWords.
    int blockSize;

    if (padWords > 8) {
        blockSize = info.mSizeWords - padWords;
        // The split block's header is built in a register-held FreeBlock
        // temp and stored as a WHOLE STRUCT, not field by field: that is the
        // only spelling that (a) hoists both header loads above every store
        // (827F8990/827F899C precede the `stwux` at 827F89A8) and (b) makes
        // the timestamp argument a store-forwarded reload -- the
        // `clrrwi r8, r11, 0` at 827F89B0 is MSVC zero-extending a value it
        // forwarded out of the struct store, which a plain `timeStamp` local
        // never needs (w7-bu, measured: locals 90.0, temp+struct store 93.3).
        // A memory-copied temp (`saved = *block`) keeps a dead load of the
        // old mSizeWords and spills it (91.4); assigning the three fields is
        // what keeps the temp in registers.
        FreeBlock saved;
        saved.mSizeWords = blockSize;
        saved.mTimeStamp = block->mTimeStamp;
        saved.mNextBlock = block->mNextBlock;
        FreeBlock *oldBlock = block;
        block = (FreeBlock *)((int *)block + padWords);
        *block = saved;
        InsertFreeBlock(oldBlock, padWords, prevBlock, block, block->mTimeStamp);
        prevBlock = oldBlock;
        padWords = 0;
    } else {
        blockSize = info.mSizeWords;
    }

    int totalUsed = padWords + sizeWords;
    int remainder = blockSize - totalUsed;

    if (remainder > 8) {
        InsertFreeBlock(
            (FreeBlock *)((int *)block + totalUsed), remainder,
            prevBlock, block->mNextBlock, block->mTimeStamp
        );
    } else {
        // Assigned at the TOP of the arm: the image's `mr r27, r28` (827F8A08)
        // sits between the shared `lwz mNextBlock` and the prevBlock test.
        totalUsed = blockSize;
        if (prevBlock == nullptr) {
            mFreeBlockChain = block->mNextBlock;
        } else {
            prevBlock->mNextBlock = block->mNextBlock;
        }
    }

    unsigned int *header = (unsigned int *)block + padWords;
    // The `& 0xF` is what makes MSVC fold the pad nibble in with `rlwimi`
    // (827F8A20) instead of a shift-and-or: without it the compiler has to
    // assume padWords can overflow the field.
    *header = (totalUsed << 8) | ((padWords & 0xF) << 4) | (*header & 0xF);

    // The image re-derives the start of the pad run from the nibble it just
    // wrote (827F8A30 `rlwinm r9, r10, 30, 26, 29`, then `subf r10, r9, r11`)
    // rather than reusing the block pointer it still has in r31.
    unsigned int *ptr = header - ((*header >> 4) & 0xF);
    for (; ptr != header; ptr++) {
        *ptr = 0;
    }

    if (1 <= mDebugLevel) {
        unsigned int hdr = *header;
        unsigned int dataWords = (hdr >> 8) - ((hdr >> 4) & 0xF);
        unsigned int *end = header + dataWords;
        // The image's fill covers [header+1, end) one word at a time
        // (827F8AA4 `stwu r9, 0x4(r8)` under `mtctr`). The previous spelling
        // here computed `((end - cur - 1) >> 2) + 1` on an already
        // word-scaled pointer difference -- a second divide by 4 -- and
        // pre-incremented before storing, so it filled a quarter of the
        // block starting one word late.
        for (unsigned int *cur = header + 1; cur < end; cur++) {
            *cur = 0xABCDABCD;
        }
    }

    allocSize = *header >> 8;
    return (int *)(header + 1);
}

int *MemHeap::Alloc(int sizeWords, int align, int &allocSize) {
    int *result = TryAlloc(sizeWords, align, allocSize);
    if (result == nullptr) {
        int lFrags, rFrags, freeBytes, minFreeBytes, maxFreeBlock;
        FreeBlockStats(lFrags, rFrags, freeBytes, minFreeBytes, maxFreeBlock);
        bool isMain = MainThread();
        if (!isMain) {
            extern bool gInsideMemFunc;
            extern CriticalSection *gMemLock;
            gInsideMemFunc = false;
            gMemLock->Abandon();
        }
        extern MemTracker *gMemTracker;
        if (gMemTracker != nullptr && !gMemTracker->GetHeapOnly()) {
            FILE *f = fopen("devkit:\\out_of_mem_alloc_info.csv", "w");
            if (f) {
                MemTracker::SpitAllocInfo((struct _iobuf *)f);
                fclose(f);
            }
        }
        int wantBytes = sizeWords * 4;
        char buf[2048];
        const char *msg = MakeString(
            "Allocation failure, heap \"%s\", want %d bytes\n"
            "   lFrags=  %8d\n"
            "   rFrags=  %8d\n"
            "   Biggest Block=%8d\n"
            "   Free Bytes=   %8d\n",
            mName, wantBytes, lFrags, rFrags, maxFreeBlock, freeBytes
        );
        strcpy(buf, msg);
        int len = strlen(buf);
        MemPrintOverview(-3, buf + len);
        MILO_FAIL(buf);
    }
    return result;
}

bool FreeBlock::AttemptMerge(FreeBlock *next, int debugLevel) {
    if ((int *)this + mSizeWords == (int *)next) {
        unsigned int ts = mTimeStamp;
        if (mTimeStamp < next->mTimeStamp) {
            ts = next->mTimeStamp;
        }
        int nextSize = next->mSizeWords;
        FreeBlock *nextNext = next->mNextBlock;
        mTimeStamp = ts;
        mSizeWords += nextSize;
        mNextBlock = nextNext;
        if (1 <= debugLevel) {
            int *ptr = (int *)next;
            int *end = ptr + 3;
            if (ptr < end) {
                do {
                    *ptr = 0xDEADDEAD;
                    ptr++;
                } while (ptr < end);
            }
        }
        return true;
    }
    return false;
}

int *MemHeap::Truncate(int *ptr, int newSizeWords, int &allocSize) {
    if (ptr < mStart || ptr >= mStart + mSizeWords) {
        return nullptr;
    }

    unsigned int header = *(unsigned int *)(ptr - 1);
    unsigned int blockSizeWords = header >> 8;
    unsigned int padWords = (header >> 4) & 0xF;
    int truncWords = blockSizeWords - padWords - newSizeWords - 1;
    MILO_ASSERT(truncWords >= 0, 0x1A8);

    unsigned int *headerPtr = (unsigned int *)(ptr - 1);

    if (truncWords > 8) {
        FreeBlock *prev = nullptr;
        FreeBlock *next;
        for (next = mFreeBlockChain; next != nullptr && (int *)next < (int *)headerPtr; next = next->mNextBlock) {
            prev = next;
        }
        int ts = gTimeStamp;
        FreeBlock *newFree = (FreeBlock *)((int *)ptr + newSizeWords);
        gTimeStamp++;
        InsertFreeBlock(newFree, truncWords, prev, next, ts);
        if (1 <= mDebugLevel) {
            int *end = (int *)newFree + newFree->mSizeWords;
            for (int *cur = (int *)newFree + 3; cur < end; cur++) {
                *cur = 0xDEADDEAD;
            }
        }
        if (next != nullptr) {
            newFree->AttemptMerge(next, mDebugLevel);
        }
        *headerPtr = (*headerPtr & 0xFF) | ((*headerPtr - (truncWords << 8)) & 0xFFFFFF00);
    }

    allocSize = *headerPtr >> 8;
    return ptr;
}

int MemHeap::Free(int *ptr) {
    if (ptr < mStart || ptr >= mStart + mSizeWords) {
        return 0;
    }

    unsigned int *headerAddr = (unsigned int *)(ptr - 1);
    unsigned int header = *headerAddr;
    int blockSizeBytes = (header >> 6) & 0x3FFFFFC;

    FreeBlock *prev = nullptr;
    FreeBlock *next;
    for (next = mFreeBlockChain; next != nullptr && (int *)next < (int *)headerAddr; next = next->mNextBlock) {
        prev = next;
    }

    unsigned int padBytes = (header >> 2) & 0x3C;
    int *blockStart = (int *)((char *)headerAddr - padBytes);

    int ts = gTimeStamp++;
    FreeBlock *newFree = (FreeBlock *)blockStart;
    InsertFreeBlock(newFree, *headerAddr >> 8, prev, next, ts);

    if (1 <= mDebugLevel) {
        int *end = (int *)newFree + newFree->mSizeWords;
        int *end3 = (int *)newFree + 3;
        if (end3 < end) {
            int *cur = end3 - 1;
            for (unsigned int count = (((unsigned int)end - (unsigned int)end3) - 1) / 4 + 1; count != 0; count--) {
                cur++;
                *cur = 0xDEADDEAD;
            }
        }
    }

    if (next != nullptr) {
        newFree->AttemptMerge(next, mDebugLevel);
    }
    if (prev != nullptr) {
        prev->AttemptMerge(newFree, mDebugLevel);
    }

    return blockSizeBytes;
}

