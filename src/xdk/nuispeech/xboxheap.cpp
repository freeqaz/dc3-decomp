#include "xboxheap.h"

NUISPEECH::CXboxHeap::CXboxHeap(unsigned int initSize, unsigned int size) {
    mSize = size;
    mFreeHead = this;
    mCount = 0;
    mUsedHead = this;
    mListHead.mNext = &mListHead;
    mListHead.mPrev = &mListHead;
    AllocatePageBlock(initSize);
}