#include "utl/MemHeap.h"
#include "math/Utl.h"
#include "os/Debug.h"
#include "utl/MakeString.h"
#include "utl/TextStream.h"
#include "utl/AllocInfo.h"
#include "utl/MemTrack.h"

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
    ts << MakeString("; HEAP: %i (%s), starts %p, %d bytes\n", mNum, mName, mStart, mSizeWords * 4);
    int rFrags, lFrags, freeBytes, maxFreeIdx, minFreeBytes;
    FreeBlockStats(lFrags, rFrags, freeBytes, maxFreeIdx, minFreeBytes);
    ts << MakeString("\n");
    ts << MakeString(
        ";   lFrags =  %8d\n;   rFrags =  %8d\n;   Total Free Bytes=  %8d\n",
        lFrags,
        rFrags,
        freeBytes
    );
    ts << MakeString("\n");

    unsigned int *curPtr = (unsigned int *)mStart;
    unsigned int *endPtr = curPtr + mSizeWords;
    unsigned int *curFreeBlock = (unsigned int *)mFreeBlockChain;
    int curAllocCount = 0;
    int curAllocSize = 0;
    int *curAllocPtr = nullptr;
    const AllocInfo *curAllocInfo = nullptr;

    unsigned int blockSizeWords = 0;
    for (; curPtr < endPtr; curPtr += blockSizeWords) {
        unsigned int *savedCurPtr = curPtr;

        if (curFreeBlock == nullptr || curPtr != curFreeBlock) {
            // Alloc block
            unsigned int hdr = *curPtr;
            unsigned int *headerPtr = curPtr;
            while (hdr == 0) {
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
    mStart = start;
    mName = name;
    mNum = num;
    int *i7 = (int *)(((uintptr_t)start - 4 & ~(uintptr_t)0xFU) + 0x10);
    mIsHandleHeap = handle;
    mStrategy = strat;
    mStart = i7;
    mAllowTemp = allowTemp;
    mMinFreeBytes = -1;
    mDebugLevel = debugLevel;
    mSizeWords = size - (i7 - start);
    int time = gTimeStamp;
    gTimeStamp++;
    InsertFreeBlock((FreeBlock *)mStart, mSizeWords, nullptr, nullptr, time);
    if (mDebugLevel >= 1) {
        FreeBlock *blockStart = mFreeBlockChain;
        int *blockStartInt = (int *)blockStart;
        int *blockEnd = blockStartInt + blockStart->mSizeWords;
        if (blockStartInt + 3 < blockEnd) {
            int *ptr = blockStartInt + 2;
            for (int count = (((blockEnd - (blockStartInt + 3)) - 1) >> 2) + 1; count != 0; count--) {
                ptr++;
                *ptr = 0xDEADDEAD;
            }
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

