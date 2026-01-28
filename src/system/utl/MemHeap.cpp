#include "utl/MemHeap.h"
#include "MemHeap.h"
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
    if (mDebugLevel > 0) {
    }
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

// void __thiscall
// MemHeap::Init(MemHeap *this,char *param_1,int param_2,int *param_3,int param_4,bool
// param_5,
//              Strategy param_6,int param_7,bool param_8)

// {

//   piVar7 = (param_3 + -1 & 0xfffffff0) + 0x10;
//   this->mIsHandleHeap = param_5;
//   this->mStrategy = param_6;
//   this->mStart = piVar7;
//   this->mAllowTemp = in_stack_00000057;
//   this->field11_0x24 = -1;
//   this->mDebugLevel = param_7;
//   this->mSizeWords = param_4 - (piVar7 - param_3 >> 2);
//   iVar1 = _anon_AD41C9DD::gTimeStamp;
//   _anon_AD41C9DD::gTimeStamp = _anon_AD41C9DD::gTimeStamp + 1;
//   InsertFreeBlock(this,this->mStart,this->mSizeWords,0x0,0x0,iVar1);
//   if (this->mDebugLevel > 0) {
//     uVar4 = ZEXT48(this->mFreeBlockChain);
//     uVar5 = (this->mFreeBlockChain->mSizeWords & 0x3fffffff) * 4 + uVar4;
//     if ((uVar4 + 0xc & 0xffffffff) < (uVar5 & 0xffffffff)) {
//       lVar3 = uVar4 + 8;
//       for (lVar6 = (((uVar5 - (uVar4 + 0xc)) + -1 << 0x20) >> 0x22) + 1; lVar6 != 0;
//           lVar6 = lVar6 + -1) {
//         lVar3 = lVar3 + 4;
//         *lVar3 = 0xdeaddead;
//       }
//     }
//   }
//   return;
// }
