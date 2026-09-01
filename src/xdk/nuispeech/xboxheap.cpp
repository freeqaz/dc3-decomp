#include "xboxheap.h"
#include "xdk\xapilibi\xbox.h"

NUISPEECH::CXboxHeap::CXboxHeap(unsigned int initSize, unsigned int size) {
    mSize = size;
    mFreeHead = this;
    mCount = 0;
    mUsedHead = this;
    auto& listHead = mListHead;
    listHead.mNext = &listHead;
    listHead.mPrev = &listHead;
    AllocatePageBlock(initSize);
}

// The free list is a circular doubly-linked list ordered by ascending mSize
// whose sentinel is the CXboxHeap itself: mFreeHead/mUsedHead at 0x0/0x4 are
// the sentinel's mNext/mPrev, which is why the constructor points both at
// `this`.  Insert `block` in front of the first entry that is not smaller.
void NUISPEECH::CXboxHeap::InsertFreeBlockList(_BLOCK_ENTRY *block) {
    _BLOCK_ENTRY *head = (_BLOCK_ENTRY *)this;
    _BLOCK_ENTRY *cur = (_BLOCK_ENTRY *)mFreeHead;

    if (cur == head) {
        _BLOCK_ENTRY *prev = head->mPrev;
        block->mNext = head;
        block->mPrev = prev;
        prev->mNext = block;
        head->mPrev = block;
        return;
    }

    do {
        if (block->mSize <= cur->mSize) {
            break;
        }
        cur = cur->mNext;
    } while (cur != head);

    _BLOCK_ENTRY *prev = cur->mPrev;
    block->mNext = cur;
    block->mPrev = cur->mPrev;
    prev->mNext = block;
    cur->mPrev = block;
}

// Rounds the request up to whole pages (plus the 0x18 bytes of page + block
// header), takes it from XMemAlloc, links the page onto mListHead and hands the
// one free block it contains to InsertFreeBlockList.
NUISPEECH::CXboxHeap::_BLOCK_ENTRY *NUISPEECH::CXboxHeap::AllocatePageBlock(unsigned int size) {
    unsigned int pageBytes = (size + 0x1017) & ~0xfff;

    if (mSize != 0 && mCount + pageBytes > mSize) {
        return nullptr;
    }

    _PAGE_ENTRY *page = (_PAGE_ENTRY *)XMemAlloc(pageBytes, 0x249b0000);
    if (page == nullptr) {
        return nullptr;
    }

    mCount += pageBytes;

    _BLOCK_ENTRY *block = (_BLOCK_ENTRY *)(page + 1);
    _PAGE_ENTRY *tail = mListHead.mPrev;
    page->mNext = &mListHead;
    page->mPrev = tail;
    tail->mNext = page;
    mListHead.mPrev = page;
    block->mSize = pageBytes - 0x18;
    block->mFlags = 3;
    InsertFreeBlockList(block);

    return block;
}
