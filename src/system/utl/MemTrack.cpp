#include "utl\MemTrack.h"

#include "obj\DataFunc.h"
#include "utl\Str.h"
#include "obj\Data.h"
#include "os\CritSec.h"
#include "os\Debug.h"
#include "os\File.h"
#include "os\System.h"
#include "utl\AllocInfo.h"
#include "utl\MemMgr.h"
#include "utl\MemTracker.h"
#include "utl\PoolAlloc.h"
#include "utl\TextFileStream.h"

// Struct packing: array of 65 pointer-sized entries (260 = 0x104 bytes) followed immediately
// by an int stack position counter. This ensures counter is at array_base + 0x104 so the
// compiler can access it as lwz r11, 0x104(r_array_base).
struct MemTrackStack {
    char ptrs[260]; // (STACK_SIZE+1) * sizeof(void*) = 65 * 4 = 260 bytes
    int pos;        // stack position, at offset 0x104 from ptrs base
};

class HeapTracker; // only ever referenced through the pointer below

// This TU's zero-initialised globals are emitted into .bss in *reverse*
// declaration order, so the block below is the reverse of the target's data
// layout (config/373307D9/symbols.txt, 0x830E57D8 upward):
//
//   gAllocInfoHeap 0x830E57D8   gMemTracker    0x830E57DC
//   gMemTrackerTracking  ..E0   gMemoryUsageTest     ..E1
//   gHeapTracker         ..E4   gNumDiffs            ..E8
//   gLog                 ..EC   s_MemTrackObjectNameStack 0x830E57F0
//   s_MemTrackFileNameStack 0x830E58F8
//
// gHeapTracker is unreferenced in this TU but is a real global in the target
// (?gHeapTracker@@3PAVHeapTracker@@A); without it gNumDiffs slides down four
// bytes and both MemTrackStacks land at the wrong displacement.
static MemTrackStack s_MemTrackFileNameStack;   // CharArrayArray + s_MemTrackFileNameStackPos
static MemTrackStack s_MemTrackObjectNameStack; // MemTrackObjectName + s_MemTrackObjectNameStackPos
TextFileStream *gLog;
// gNumDiffs and gAllocInfoHeap are `static`: the linker map named every other
// global in this block but left 0x830E57E8 and 0x830E57D8 as bare lbl_
// addresses, i.e. they were never public. Internal linkage is also what lets
// MSVC fold &gAllocInfoHeap into the base register it then reaches
// s_MemTrackObjectNameStack / s_MemTrackFileNameStack from.
static int gNumDiffs;
HeapTracker *gHeapTracker;
bool gMemoryUsageTest;
bool gMemTrackerTracking;
MemTracker *gMemTracker;
static AllocInfo *gAllocInfoHeap;

// Dynamically initialised, so these are not part of the reversed block: they
// follow s_MemTrackFileNameStack in declaration order (0x830E5A00, 0x830E5A08).
String gMemTrackSourceFile;
String gMemTrackSourceObject;

#define MemTrackObjectName s_MemTrackObjectNameStack.ptrs
#define CharArrayArray s_MemTrackFileNameStack.ptrs
#define s_MemTrackObjectNameStackPos s_MemTrackObjectNameStack.pos
#define s_MemTrackFileNameStackPos s_MemTrackFileNameStack.pos

void StopLog() {
    if (gLog) {
        RELEASE(gLog);
    }
}

bool MemTrackEnable(bool enable) {
    bool old = gMemTrackerTracking;
    gMemTrackerTracking = enable;
    return old;
}

void MemTrackSpew(bool spew) {
    if (gMemTracker) {
        gMemTracker->SetSpew(spew);
    }
}

void MemTrackSetReportName(const char *name) {
    if (gMemTracker) {
        gMemTracker->SetReport(new TextFileStream(name, false));
        gMemTracker->SetAllocInfoName(name);
    }
}

void MemTrackReportMemoryAlloc(const char *name) {
    if (gMemTracker) {
        gMemTracker->ReportMemoryAlloc(name);
    }
}

void MemTrackReportMemoryUsage(const char *name) {
    if (gMemTracker) {
        gMemTracker->ReportMemoryUsage(name);
    }
}

void MemTrackReportClose(const char *name) {
    if (gMemTracker) {
        gMemTracker->ReportMemoryUsageOverview(name);
        gMemTracker->CloseReport();
    }
}

void MemTrackAlloc(
    int req,
    int act,
    const char *type,
    void *mem,
    bool pooled,
    unsigned char strat,
    const char *file,
    int line
) {
    if (gMemTracker && gMemTrackerTracking) {
        CritSecTracker tracker(gMemLock);
        int heap = GetCurrentHeapNum();
#ifndef HX_NATIVE
        if (mem >= (void *)0xA0000000) {
            heap = MemNumHeaps();
        }
#endif
        gMemTracker->Alloc(req, act, type, mem, heap, pooled, strat, file, line);
    }
}

void MemTrackFree(void *mem) {
    if (gMemTracker) {
        CritSecTracker tracker(gMemLock);
        gMemTracker->Free(mem);
    }
}

void MemTrackRealloc(void *key, int req, int act, void *mem) {
    if (gMemTracker) {
        CritSecTracker tracker(gMemLock);
        gMemTracker->Realloc(key, req, act, mem);
    }
}

const AllocInfo *MemTrackGetInfo(void *key) {
    if (gMemTracker) {
        CritSecTracker tracker(gMemLock);
        return gMemTracker->GetInfo(key);
    } else {
        return nullptr;
    }
}

void *DebugHeapAlloc(int size) { return malloc(size); }
void DebugHeapFree(void *mem) { free(mem); }

void MemDeltaFullReport() {
    for (int i = 0; i < MemNumHeaps(); i++) {
        MemDelta("", i);
    }
    PhysDelta("");
}

void StartLog(const char *base) {
    char buffer[64];
    if (gLog) {
        StopLog();
    }
    MILO_ASSERT(!gLog, 0x5B);
    int num = gNumDiffs;
    bool _cond = strstr(base, "diff");
    if (_cond) {
        gNumDiffs++;
    }
    while (true) {
        MILO_ASSERT(strlen( base ) < 55, 0x68);
        auto _tmp0 = MakeString("%s_%03i.txt", base, num);
        strcpy(buffer, _tmp0);
        num++;
        File *file = NewFile(buffer, 0x10001);
        if (!file)
            break;
        delete file;
    }
    MILO_LOG("writing file %s\n", buffer);
    gLog = new TextFileStream(buffer, false);
}

void MemTrackReport(int i1, bool b2) {
    if (gMemTracker) {
        CritSecTracker tracker(gMemLock);
        if (b2) {
            StartLog("mem_report");
            gMemTracker->Report(i1, *gLog);
            PoolReport(*gLog);
            StopLog();
            StartLog("mem_diff");
            gMemTracker->DiffDump(*gLog);
            StopLog();
        } else {
            gMemTracker->DiffDump(TheDebug);
        }
    }
}

void MemTrackHeapDump(bool freeOnly) {
    CritSecTracker tracker(gMemLock);
    StartLog("mem_dump");
    *gLog << "(executable " << TheSystemArgs.front() << ")\n";
    *gLog << "(data\n";
    for (int i = 0; i < MemNumHeaps(); i++) {
        if (gMemTracker) {
            if (gMemTracker->Heap() == -1 || gMemTracker->Heap() == i) {
                MemPrint(i, *gLog, freeOnly);
            }
        }
    }
    *gLog << ")\n";
    StopLog();
}

DataNode MemTrackReportDF(DataArray *) {
    MemTrackReport(1000, true);
    return 0;
}

DataNode MemTrackHeapDumpDF(DataArray *) {
    MemTrackHeapDump(false);
    return 0;
}

DataNode MemTrackLogDF(DataArray *a) {
    if (a->Int(1) == 1) {
        StartLog("mem_log");
        gMemTracker->StartLog(*gLog);
    } else {
        gMemTracker->StopLog();
        StopLog();
    }
    return 0;
}

static const int STACK_SIZE = 64;

void BeginMemTrackObjectName(const char *name) {
    if (gMemTracker) {
        s_MemTrackObjectNameStackPos++;
        MILO_ASSERT(s_MemTrackObjectNameStackPos <= STACK_SIZE, 0xBE);
        strncpy(((char **)MemTrackObjectName)[s_MemTrackObjectNameStackPos],
                gMemTracker->unk181b4.c_str(), 0x80);
        ((char **)MemTrackObjectName)[s_MemTrackObjectNameStackPos][0x7f] = '\0';
        static bool sCampfireToggle = false;
        if (strcmp(name, "flow/nav_player.milo") == 0) {
            sCampfireToggle = !sCampfireToggle;
        }
        gMemTracker->unk181b4 = name;
    }
}

void EndMemTrackObjectName() {
    if (gMemTracker) {
        int pos = s_MemTrackObjectNameStackPos - 1;
        s_MemTrackObjectNameStackPos = pos;
        MILO_ASSERT(0<=s_MemTrackObjectNameStackPos && s_MemTrackObjectNameStackPos<STACK_SIZE, 0xCF);
        if (s_MemTrackObjectNameStackPos >= 0) {
            gMemTracker->unk181b4 =
                ((char **)MemTrackObjectName)[s_MemTrackObjectNameStackPos];
        }
    }
}

void BeginMemTrackFileName(const char *name) {
    if (gMemTracker) {
        s_MemTrackFileNameStackPos++;
        MILO_ASSERT(s_MemTrackFileNameStackPos <= STACK_SIZE, 0xDA);
        strncpy(((char **)CharArrayArray)[s_MemTrackFileNameStackPos], name, 0x80);
        ((char **)CharArrayArray)[s_MemTrackFileNameStackPos][0x7f] = '\0';
        String *prev = &gMemTracker->unk181ac;
        gMemTracker->unk181a4 = *prev;
        *prev = name;
    }
}

void EndMemTrackFileName() {
    if (gMemTracker) {
        int pos = s_MemTrackFileNameStackPos - 1;
        s_MemTrackFileNameStackPos = pos;
        MILO_ASSERT(0<=s_MemTrackFileNameStackPos && s_MemTrackFileNameStackPos<STACK_SIZE, 0xE6);
        if (s_MemTrackFileNameStackPos >= 0) {
            char *name = ((char **)CharArrayArray)[s_MemTrackFileNameStackPos];
            String *prev = &gMemTracker->unk181ac;
            gMemTracker->unk181a4 = *prev;
            *prev = name;
        }
    }
}

void MemTrackInit(int heap, int numAllocs, bool heapOnly) {
    CritSecTracker tracker(gMemLock);
    MILO_ASSERT(!gMemTracker, 0x82);
    if (heapOnly) {
        numAllocs = 1;
    }
    gMemTracker = new MemTracker(heap, numAllocs);
    gMemTracker->SetHeapOnly(heapOnly);
    gAllocInfoHeap = (AllocInfo *)malloc(numAllocs * sizeof(AllocInfo));
    MILO_ASSERT(gAllocInfoHeap, 0x89);
    AllocInfo::SetPoolMemory(gAllocInfoHeap, numAllocs * sizeof(AllocInfo));
    DataRegisterFunc("heap_report", MemTrackReportDF);
    DataRegisterFunc("heap_dump", MemTrackHeapDumpDF);
    DataRegisterFunc("mem_log", MemTrackLogDF);
    MemTrackReport(0, false);
    AllocInfoInit();
    int i = 0;
    do {
        void *fileNameMem = MemAlloc(0x80, __FILE__, 0x9a, "MemTrackStack", 0);
        *(void **)((int)CharArrayArray + i) = fileNameMem;
        memset(fileNameMem, 0, 0x80);
        void *objectNameMem = MemAlloc(0x80, __FILE__, 0x9c, "MemTrackStack", 0);
        *(void **)((int)MemTrackObjectName + i) = objectNameMem;
        memset(objectNameMem, 0, 0x80);
        i += 4;
    } while (i <= 0x100);
}