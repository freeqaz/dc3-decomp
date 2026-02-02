#include "VorbisMem.h"
#include "utl/MemMgr.h"
#include <cstring>

static const char *kOggInternalName = "Ogg_Internal";

void *OggMalloc(int i) { return _MemAllocTemp(i, __FILE__, 0x1C, kOggInternalName, 0); }

void *OggCalloc(int i1, int i2) {
    void *tmp = _MemAllocTemp(i1 * i2, __FILE__, 0x1C, kOggInternalName, 0);
    memset(tmp, 0, i1 * i2);
    return tmp;
}

void *OggRealloc(void *v, int i) {
    MemPushTemp();
    void *result = MemRealloc(v, i, __FILE__, 0x2B, kOggInternalName, 0);
    MemPopTemp();
    return result;
}

void OggFree(void *v) { MemFree(v); }
