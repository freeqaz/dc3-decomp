#include "os/Endian.h"
#include "os/Debug.h"

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
