#pragma once
#include "obj\Data.h"
#include "utl\MemMgr.h"

namespace LocaleChunkSort {
    struct OrderedLocaleChunk {
        OrderedLocaleChunk() : node1(0), node2(0), node3(0) {}
        DataNode node1;
        DataNode node2;
        DataNode node3;

        MEM_ARRAY_OVERLOAD(OrderedLocaleChunk, 0x1d)
    };

    void Sort(OrderedLocaleChunk *, int);

    template <int N>
    int FastSort(const void *a, const void *b);
}
