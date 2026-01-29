#include "BinkMovieImpl.h"
#include <cstring>

MovieInternalBuffers::MovieInternalBuffers() {
    // Zero out padding region (0x44-0xBC)
    memset(reinterpret_cast<char*>(this) + 0x44, 0, 0x78);

    // Zero out all pointer fields
    for (int i = 0; i < 17; i++) {
        mBinks[i] = nullptr;
    }
    mUnknown = nullptr;
}

MovieInternalBuffers::~MovieInternalBuffers() {
}
