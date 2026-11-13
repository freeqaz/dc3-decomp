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

#ifdef HX_NATIVE
// The heap is managed in 4-byte words and every free block starts with a
// FreeBlock header {mSizeWords, mTimeStamp, mNextBlock}. On the 360 that
// header is exactly 3 words, and the image writes the 3 as a literal: the
// smallest block (GetSizeWords) and the first word the debug fills may
// overwrite. On a 64-bit host mNextBlock is 8 bytes and the header is 4 words,
// so a freed 3-word block spilled mNextBlock into the next block's header, and
// the fills overwrote the high half of mNextBlock with 0xDEADDEAD.
// (rb3-xenon W16-UB, 63407036c.)
static const int kFreeBlockWords = (sizeof(FreeBlock) + 3) / 4;
#define FREE_BLOCK_WORDS kFreeBlockWords
#else
#define FREE_BLOCK_WORDS 3
#endif

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
                for (int i = 0; i < 0x10 && (unsigned int)info->mStackTrace[i] != 0; i++) {
                    ts << *info;
                }
            }
            ts << MakeString(")\n");
        }
    }
}

int MemHeap::GetSizeWords(int size) {
    unsigned int words = ((size + 3) >> 2) + 1;
    if (words >= FREE_BLOCK_WORDS)
        return words;
    return FREE_BLOCK_WORDS;
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
    // Lane history (11 lines moved): docs/decomp/notes/system/utl/MemHeap.md
    ts << MakeString("\n");
    int *curPtr = mStart;
    // Declared ptr, size, count: the image zeroes them in that order (`li
    // r24` / `li r29` / `li r28` around the endPtr add).
    int *curAllocPtr = nullptr;
    int curAllocSize = 0;
    int curAllocCount = 0;
    int *endPtr = mSizeWords + mStart;
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
            // blockSizeWords is read straight from the free block (no
            // sizeWords copy) and freeStr is an if/else: both only move c2's
            // colour priorities (the emitted code is the same). With the copy,
            // blockSizeWords (pri 24) outranked newPtr/newSize (pri 20); with
            // the big-block string as a default store, its hoisted address
            // was pri -16 and popped before verbose / the FREE format / "".
            // The image's order is newPtr r31, newSize r27, blockSizeWords
            // r26 and verbose r19, format r18, "" r17, big r16.
            const char *freeStr;
            curAllocCount = 0;
            blockSizeWords = *curFreeBlock;
            int blockSize = blockSizeWords << 2;
            if (blockSize >= 100000) {
                freeStr = " ; **** big free block!";
            } else {
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
#ifdef HX_NATIVE
            // Word 2 is only the low half of mNextBlock on a 64-bit host.
            curFreeBlock = (unsigned int *)((FreeBlock *)curFreeBlock)->mNextBlock;
#else
            curFreeBlock = (unsigned int *)curFreeBlock[2];
#endif
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
    // Lane history (55 lines moved): docs/decomp/notes/system/utl/MemHeap.md
    // > RESOLVED (w7-bn, 2026-09-15): 83.0 -> 100.0 canonical, 78 rows all equal, 312 B
    // > both sides.
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
        for (int *ptr = blockStartInt + FREE_BLOCK_WORDS; ptr < blockEnd; ptr++) {
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

// Lane history (24 lines moved): docs/decomp/notes/system/utl/MemHeap.md
// > RESIDUAL (w7-bu, 93.3 canonical, 45 rows): one register plan.
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
        // Lane history (40 lines moved): docs/decomp/notes/system/utl/MemHeap.md
        FreeBlock saved;
        saved.mSizeWords = blockSize;
        saved.mNextBlock = block->mNextBlock;
        saved.mTimeStamp = block->mTimeStamp;
        FreeBlock *oldBlock = block;
        block = (FreeBlock *)((int *)block + padWords);
        *block = saved;
        InsertFreeBlock(oldBlock, padWords, prevBlock, block, block->mTimeStamp);
        padWords = 0;
        prevBlock = oldBlock;
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

// Lane history (6 lines moved): docs/decomp/notes/system/utl/MemHeap.md
bool FreeBlock::AttemptMerge(FreeBlock *next, int debugLevel) {
    int thisSize = mSizeWords;
    if ((int *)this + thisSize == (int *)next) {
        unsigned int ts = Max(mTimeStamp, next->mTimeStamp);
        int nextSize = next->mSizeWords;
        FreeBlock *nextNext = next->mNextBlock;
        mTimeStamp = ts;
        mSizeWords = thisSize + nextSize;
        mNextBlock = nextNext;
        if (1 <= debugLevel) {
            int *ptr = (int *)next;
            int *end = ptr + FREE_BLOCK_WORDS;
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
            for (int *cur = (int *)newFree + FREE_BLOCK_WORDS; cur < end; cur++) {
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
        for (int *cur = (int *)newFree + FREE_BLOCK_WORDS; cur < end; cur++) {
            *cur = 0xDEADDEAD;
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

