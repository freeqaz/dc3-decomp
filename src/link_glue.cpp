// Link glue: provides ICF-merged function definitions that are missing from
// split objects. The original linker folded these with identical functions via
// Identical COMDAT Folding (ICF), so they don't exist as separate symbols in
// any split .obj file. Our decomp source defines them (MemMgr.cpp, DataArray.cpp)
// but those units are NonMatching, so the split objects are used instead.
//
// This file gets compiled and linked alongside the other objects to provide
// the missing symbols.

#include "obj/Data.h"
#include "os/Debug.h"
#include "utl/MemMgr.h"
#include "utl/PoolAlloc.h"

void operator delete(void *v) { MemFree(v, "unknown", 0, "unknown"); }
void operator delete[](void *v) { MemFree(v, "unknown", 0, "unknown"); }

DataNode &DataArray::Node(int i) const {
    MILO_ASSERT_FMT(
        i >= 0 && i < mSize,
        "Array doesn't have node %d, only has %d (file %s, line %d)",
        i, mSize, File(), Line()
    );
    return mNodes[i];
}

DataNode &DataArray::Node(int i) {
    MILO_ASSERT_FMT(
        i >= 0 && i < mSize,
        "Array doesn't have node %d, only has %d (file %s, line %d)",
        i, mSize, File(), Line()
    );
    return mNodes[i];
}

void MemOrPoolFreeSTL(
    int poolIdx, void *mem, const char *file, int line, const char *name
) {
    if (mem) {
        if (poolIdx > 0x80) {
            MemFree(mem, file, line, name);
        } else {
            PoolFree(poolIdx, mem, file, line, name);
        }
    }
}
