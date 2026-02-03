#include "utl/MemHeap.h"
#include "math/Utl.h"
#include "os/Debug.h"
#include "utl/MakeString.h"
#include "utl/TextStream.h"
#include "utl/AllocInfo.h"
#include "utl/MemTrack.h"

namespace {
    int gTimeStamp;
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
    unk24 = Min<unsigned int>(ivar3, unk24);
    i4 = unk24;
}

void MemHeap::Print(TextStream &ts, bool verbose) {
    ts << MakeString(";---------------------------------------\n");
    int sizeBytes = mSizeWords * 4;
    ts << MakeString(
        "; HEAP: %i (%s), starts %p, %d bytes\n", mNum, mName, mStart, sizeBytes
    );
    int lFrags, rFrags, freeBytes, largestFree;
    FreeBlockStats(lFrags, rFrags, freeBytes, largestFree, largestFree);
    ts << MakeString("\n");
    ts << MakeString(
        ";   lFrags =  %8d\n;   rFrags =  %8d\n",
        lFrags,
        rFrags
    );
    ts << MakeString("\n");
    ts << MakeString("\n");

    unsigned int startPtr = (unsigned int)mStart;
    unsigned int endPtr = startPtr + (mSizeWords * 4);

    int curAllocCount = 0;
    int curAllocSize = 0;
    int curAllocPtr = 0;
    const AllocInfo *curAllocInfo = 0;
    FreeBlock *curFreeBlock = mFreeBlockChain;
    unsigned int curPtr = startPtr;

    while (curPtr < endPtr) {
        if (curFreeBlock != 0 && curPtr == (unsigned int)curFreeBlock) {
            // Process and flush current allocation
            if (curAllocCount > 0) {
                ts << *curAllocInfo;
            }

            // Process free block
            unsigned int blockSize = curFreeBlock->mSizeWords * 4;
            int blockTime = curFreeBlock->mTimeStamp;
            const char *freeStr = "";

            if (blockSize >= 0x186A0) {
                freeStr = " >>> BIG FREE BLOCK <<<";
            }

            ts << MakeString(
                "(%p FREE   (size %6d) (time %5d)) %s\n",
                curFreeBlock, blockSize, blockTime, freeStr
            );

            // Reset allocation tracking
            curAllocPtr = 0;
            curAllocSize = 0;
            curAllocCount = 0;
            curAllocInfo = 0;

            // Move to next free block
            curFreeBlock = curFreeBlock->mNextBlock;
            curPtr = (unsigned int)curFreeBlock;
        } else {
            // Process allocated block
            unsigned int blockHeader = *(unsigned int *)curPtr;
            unsigned int blockSize = (blockHeader >> 8);

            if (verbose == 0) {
                int allocSizeWords = blockSize * 4;

                if (allocSizeWords == curAllocSize) {
                    curAllocCount++;
                } else {
                    if (curAllocCount > 0) {
                        ts << *curAllocInfo;
                    }
                    curAllocPtr = curPtr + 4;
                    curAllocSize = allocSizeWords;
                    curAllocCount = 1;
                    curAllocInfo = 0;
                }
            }

            curPtr += blockSize * 4;
        }
    }

    // Print final allocation
    if (curAllocCount > 0) {
        ts << *curAllocInfo;
    }

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
    int *i7 = (start - 1) + 0x10;
    mIsHandleHeap = handle;
    mStrategy = strat;
    mStart = i7;
    mAllowTemp = allowTemp;
    unk24 = -1;
    mDebugLevel = debugLevel;
    mSizeWords = size - (i7 - start >> 2);
    gTimeStamp++;
    InsertFreeBlock((FreeBlock *)mStart, mSizeWords, nullptr, nullptr, gTimeStamp);
    if (mDebugLevel >= 1) {
        FreeBlock *blockStart = mFreeBlockChain;
        int *blockStartInt = (int *)blockStart;
        int *blockEnd = blockStartInt + (blockStart->mSizeWords << 2);
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
    for (auto block = mFreeBlockChain; block != nullptr; block = block->mNextBlock) {
        int start = ((int)block >> 2) + 1;
        int alignment = start + (1 << align) - 1 >> (1 << align);
        int pad = alignment - start;
        if ((int)block->mSizeWords >= pad + size) {
            blockinfo.mSizeWords = block->mSizeWords;
            blockinfo.mPadWords = pad;
            blockinfo.mBlock = block;
            blockinfo.mPrevBlock = prev;
            return;
        }
    }
}

void MemHeap::LastFit(int size, int align, FreeBlockInfo &blockinfo) {
    FreeBlock *block = mFreeBlockChain;
    if (block == nullptr) {
        return;
    }
    FreeBlock *prev = nullptr;
    do {
        // Calculate aligned position where allocation would end
        int alignShift = align + 2;
        int blockAddr = (int)block;
        int blockSize = block->mSizeWords;
        int allocEnd = blockAddr + (blockSize - size);
        int alignedEnd = (allocEnd >> alignShift) << alignShift;
        int pad = ((alignedEnd - blockAddr) - 4) >> 2;

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
        int start = ((int)block >> 2) + 1;
        // Calculate padding needed to align data to (1 << align) bytes
        int pad = ((((unsigned int)(1 << align) + start) - 1) >> align) << align;
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

