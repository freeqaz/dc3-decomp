#include "os/Endian.h"
#include "os/CritSec.h"
#include "os/Debug.h"
#include "os/File.h"
#include "utl/MemMgr.h"
#include <cstring>

CriticalSection gCrit;

struct BINKIO;
#ifdef HX_NATIVE
// Bink SDK not available on native — stub all proprietary functions
void ReadFunc(BINKIO *, bool) {}
void BinkFree(void *) {}
unsigned int BinkFileReadHeader(BINKIO *, int, void *, unsigned int) { return 0; }
unsigned int BinkFileReadFrame(BINKIO *, unsigned int, int, void *, unsigned int) { return 0; }
void BinkSetMemory(void *(*)(unsigned int), void (*)(void *)) {}
void BinkSetIO(int (*)(BINKIO *, const char *, unsigned int)) {}
#else
extern void ReadFunc(BINKIO *, bool);
extern void BinkFree(void *);
extern unsigned int BinkFileReadHeader(BINKIO *, int, void *, unsigned int);
extern unsigned int BinkFileReadFrame(BINKIO *, unsigned int, int, void *, unsigned int);
extern void BinkSetMemory(void *(*)(unsigned int), void (*)(void *));
extern void BinkSetIO(int (*)(BINKIO *, const char *, unsigned int));
#endif
unsigned int BinkFileIdle(BINKIO *);

template<typename T>
void EndianSwapBlock(T *block, int count) {
    MILO_ASSERT(block != NULL, 0x53);
    MILO_ASSERT(count >= 0, 0x54);

    T *cur = block;
    T *end = block + count;
    if (cur != end) {
        do {
            *cur = EndianSwap(*cur);
            cur++;
        } while (cur != end);
    }
}

// Explicit instantiation for unsigned int
template void EndianSwapBlock<unsigned int>(unsigned int *, int);

int BinkFileBGControl(BINKIO *file, unsigned int flags) {
    char *pByte = (char *)file;
    volatile unsigned int *pControl = (unsigned int *)(pByte + 0x70);
    volatile unsigned int *pStatus = (unsigned int *)(pByte + 0x44);

    if (flags & 1) {
        if (*pControl == 0) {
            *pControl = 1;
        }
        if (flags & 0x80000000) {
            while (*pStatus != 0) {
                // spin
            }
        }
    } else if (flags & 2) {
        if (*pControl == 1) {
            *pControl = 0;
        }
        if (flags & 0x80000000) {
            BinkFileIdle(file);
        }
    }
    return *pControl;
}

void *BinkAlloc(unsigned int size) {
    return MemAlloc(size, "BinkIntegration.cpp", 0x44, "Bink Internal", 0);
}

unsigned int BinkFileGetBufferSize(BINKIO *, unsigned int size) {
    unsigned int aligned = (size + 0x7FFF) & 0xFFFF8000;
    if (aligned <= 0xFFFF) {
        aligned = 0x10000;
    }
    return aligned;
}

void BinkFileSetInfo(BINKIO *file, void *buf, unsigned int size, unsigned int, unsigned int fileFlags) {
    char *p = (char *)file;
    unsigned int aligned = size & 0xFFFF8000;
    *(void **)(p + 0x88) = buf;
    *(unsigned int *)(p + 0x8c) = (int)buf + aligned;
    *(void **)(p + 0x90) = buf;
    *(void **)(p + 0x94) = buf;
    *(unsigned int *)(p + 0x98) = aligned;
    *(unsigned int *)(p + 0x60) = aligned;
    *(unsigned int *)(p + 0x6c) = 0;
    *(unsigned int *)(p + 0xa0) = fileFlags;
}

void BinkFileClose(BINKIO *bink) {
    char *p = (char *)bink;
    if (*(unsigned int *)(p + 0x84) != 0) {
        File *file = *(File **)(p + 0x80);
        if (file != nullptr) {
            delete file;
        }
        *(File **)(p + 0x80) = nullptr;
    }
    if (*(unsigned int *)(p + 0xb4) == 2) {
        operator delete(*(void **)(p + 0xe8));
    }
}

unsigned int BinkFileIdle(BINKIO *bink) {
    char *p = (char *)bink;
    if (*(unsigned int *)(p + 0x40) != 0)
        return 0;
    if (*(unsigned int *)(p + 0x70) != 0)
        return 0;
    if (*(unsigned int *)(p + 0x44) != 0) {
        gCrit.Enter();
        ReadFunc(bink, false);
        gCrit.Exit();
    }
    return *(unsigned int *)(p + 0x44);
}

int BinkFileOpen(BINKIO *bink, const char *name, unsigned int flags) {
    char *p = (char *)bink;
    memset(bink, 0, 0x120);
    if (flags & 0x800000) {
        *(const char **)(p + 0x80) = name;
    } else {
        File *file = NewFile(name, 2);
        *(File **)(p + 0x80) = file;
        *(int *)(p + 0x84) = 1;
        if (file == nullptr)
            return 0;
    }
    *(void **)(p + 0x00) = (void *)BinkFileReadHeader;
    *(void **)(p + 0x04) = (void *)BinkFileReadFrame;
    *(void **)(p + 0x08) = (void *)BinkFileGetBufferSize;
    *(void **)(p + 0x0c) = (void *)BinkFileSetInfo;
    *(void **)(p + 0x10) = (void *)BinkFileIdle;
    *(void **)(p + 0x14) = (void *)BinkFileClose;
    *(void **)(p + 0x18) = (void *)BinkFileBGControl;
    return 1;
}

void BinkInit() {
    BinkSetMemory(BinkAlloc, operator delete);
    BinkSetIO(BinkFileOpen);
}
