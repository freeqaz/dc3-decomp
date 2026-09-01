#pragma once

namespace NUISPEECH {
    class CXboxHeap {
        // Link header stamped on the front of every page block taken from
        // XMemAlloc.  Eight bytes: mListHead sits at 0x8 and mSize starts at
        // 0x10, so this cannot be the (0x10-byte) _BLOCK_ENTRY.
        struct _PAGE_ENTRY {
            _PAGE_ENTRY *mNext; // 0x0
            _PAGE_ENTRY *mPrev; // 0x4
        };

        struct _BLOCK_ENTRY {
            _BLOCK_ENTRY *mNext; // 0x0
            _BLOCK_ENTRY *mPrev; // 0x4
            unsigned int mSize; // 0x8
            unsigned int mFlags; // 0xc
        };

    public:
        CXboxHeap(unsigned int, unsigned int);
        ~CXboxHeap();

        void *Alloc(unsigned int, bool);
        bool Free(void *);
        void *Realloc(void *, unsigned int, bool);

    private:
        _BLOCK_ENTRY *AllocatePageBlock(unsigned int);
        void InsertFreeBlockList(_BLOCK_ENTRY *);

    protected:
        CXboxHeap *mFreeHead; // 0x0
        CXboxHeap *mUsedHead; // 0x4
        _PAGE_ENTRY mListHead; // 0x8 (mNext at 0x8, mPrev at 0xC)
        unsigned int mSize; // 0x10
        unsigned int mCount; // 0x14
    };
}
