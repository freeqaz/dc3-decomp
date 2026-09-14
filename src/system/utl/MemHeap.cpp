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
    int rFrags, lFrags, freeBytes, maxFreeIdx, minFreeBytes;
    FreeBlockStats(lFrags, rFrags, freeBytes, maxFreeIdx, minFreeBytes);
    ts << MakeString("\n");
    ts << MakeString(
        ";   lFrags =  %8d\n;   rFrags =  %8d\n;   Total Free Bytes=  %8d\n",
        lFrags,
        rFrags,
        freeBytes
    );
    unsigned int *curPtr = (unsigned int *)mStart;

    ts << MakeString("\n");
    int curAllocCount = 0;
    int *curAllocPtr = nullptr;
    int curAllocSize = 0;
    unsigned int *endPtr = curPtr + mSizeWords;
    const AllocInfo *curAllocInfo = nullptr;
    unsigned int blockSizeWords = 0;

    unsigned int *curFreeBlock = (unsigned int *)mFreeBlockChain;
    for (; curPtr < endPtr; curPtr += blockSizeWords) {
        unsigned int *savedCurPtr = curPtr;

        if (curFreeBlock == nullptr || curPtr != curFreeBlock) {
            // Alloc block
            unsigned int hdr = *curPtr;
            unsigned int *headerPtr = curPtr;
            while ((int)hdr == 0) {
                headerPtr++;
                hdr = *headerPtr;
            }
            blockSizeWords = hdr >> 8;

            if (!verbose) {
                int *newPtr = (int *)(headerPtr + 1);
                const AllocInfo *newInfo = MemTrackGetInfo(newPtr);
                int newSize = blockSizeWords << 2;
                if (newSize == curAllocSize) {
                    curAllocCount++;
                } else {
                    PrintAlloc(ts, curAllocPtr, curAllocSize, curAllocCount, curAllocInfo);
                    curAllocCount = 1;
                    curAllocPtr = newPtr;
                    curAllocInfo = newInfo;
                    curAllocSize = newSize;
                }
            }
        } else {
            // Free block
            PrintAlloc(ts, curAllocPtr, curAllocSize, curAllocCount, curAllocInfo);
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
                (int *)savedCurPtr,
                blockSize,
                timeStamp,
                freeStr
            );
            curFreeBlock = (unsigned int *)curFreeBlock[2];
            curAllocSize = 0;
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
    mStart = start;
    mName = name;
    mNum = num;
    mIsHandleHeap = handle;
    int *alignedStart = (int *)(((uintptr_t)start - 4 & ~(uintptr_t)0xFU) + 0x10);
    mStrategy = strat;
    mStart = alignedStart;
    mAllowTemp = allowTemp;
    mMinFreeBytes = -1;
    mDebugLevel = debugLevel;
    mSizeWords = size - (alignedStart - start);
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

// RESIDUAL (w7-aq, 90.007 canonical): the remaining rows are one register
// assignment, not a missing statement.  The image puts `this` in r26 and
// sizeWords in r27 (827F88A4/827F88AC); we get the pair the other way round,
// which charges 15 rows across the four Fit calls and the three later `this`
// uses.  Consequences of the same choice: the image loads info.mBlock straight
// into r31 and updates it in place with `stwux` (827F89A8), while we load into
// r30, copy to r31 and use `add`+`stwx`; and MSVC tail-merges the two
// `return nullptr` sites the other way (the image's default arm branches
// FORWARD into the null check's `li r3, 0`, ours branches back).  Declaration
// order is not a lever here -- both are parameters.
int *MemHeap::TryAlloc(int sizeWords, int align, int &allocSize) {
    FreeBlockInfo info;
    info.mBlock = nullptr;
    info.mPrevBlock = nullptr;
    info.mSizeWords = 0x7FFFFFFF;
    info.mPadWords = 0x7FFFFFFF;

    switch (mStrategy) {
    case kFirstFit: FirstFit(sizeWords, align, info); break;
    case kBestFit:  BestFit(sizeWords, align, info); break;
    case kLRUFit:   LRUFit(sizeWords, align, info); break;
    case kLastFit:  LastFit(sizeWords, align, info); break;
    default:
        MILO_ASSERT(false, 0x151);
        return nullptr;
    }

    FreeBlock *block = info.mBlock;
    if (block == nullptr) return nullptr;

    FreeBlock *prevBlock = info.mPrevBlock;
    int padWords = info.mPadWords;
    // blockSize is assigned in BOTH arms, never before the branch: the image
    // loads info.mSizeWords twice (827F8988 inside the split arm, 827F89D0 in
    // the else arm). Hoisting it to a single initialiser above the `if` costs
    // the second load and forces an extra live copy of padWords.
    int blockSize;

    if (padWords > 8) {
        // `block` itself is advanced -- there is no separate newBlock local.
        // That is what lets the image fuse the advance and the mSizeWords
        // store into a single `stwux r28, r31, r10` (827F89A8).
        blockSize = info.mSizeWords - padWords;
        FreeBlock *oldBlock = block;
        FreeBlock *nextBlock = oldBlock->mNextBlock;
        unsigned int timeStamp = oldBlock->mTimeStamp;
        block = (FreeBlock *)((int *)block + padWords);
        block->mSizeWords = blockSize;
        block->mNextBlock = nextBlock;
        block->mTimeStamp = timeStamp;
        InsertFreeBlock(oldBlock, padWords, prevBlock, block, timeStamp);
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
        if (prevBlock == nullptr) {
            mFreeBlockChain = block->mNextBlock;
        } else {
            prevBlock->mNextBlock = block->mNextBlock;
        }
        totalUsed = blockSize;
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

