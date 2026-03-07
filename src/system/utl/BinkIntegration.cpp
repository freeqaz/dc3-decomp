#include "os/Endian.h"
#include "os/CritSec.h"
#include "os/Debug.h"

CriticalSection gCrit;

struct BINKIO;
extern unsigned int BinkFileIdle(BINKIO *);

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
