#include "BinkMovieImpl.h"
#include <cstring>
#include <stl/_vector.h>
#include <stl/_algobase.h>
#include <algorithm>

// Explicit template instantiation for vector<BINK*, StlNodeAlloc<BINK*>>
namespace stlpmtx_std {

template class vector<BINK*, StlNodeAlloc<BINK*>>;

} // namespace stlpmtx_std

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

BinkMovieImpl::BinkMovieImpl() {
    // Constructor body to be filled in during decompilation
}

BinkMovieImpl::~BinkMovieImpl() {
    // Destructor body to be filled in during decompilation
}
