#include "utl\MemTracker.h"
#include "AllocInfo.h"
#include "MemMgr.h"
#include "MemTrack.h"
#include "Memory.h"
#include "obj\Data.h"
#include "obj\DataFunc.h"
#include "os\Debug.h"
#include "os\System.h"
#include "utl/KeylessHash.h"
#include "math\Sort.h"
#include "utl\MakeString.h"
#include "utl\MemMgr.h"
#include "utl\MemStats.h"
#include "utl\Symbol.h"
#include "utl\TextFileStream.h"
#include "utl\TextStream.h"

extern bool gMemTrackerTracking;
String gMemLogType;

struct MemDiffEntry {
    char mName[59]; // 0x00
    char _pad;      // 0x3B
    int mNumDiff;   // 0x3C
    int mSizeDiff;  // 0x40
    int mHeap; // 0x44
    // total: 0x48 = 72 bytes

    // Two-key ordering: primary heap ascending, secondary size-diff descending.
    // Reducing this to the single `mHeap` key made every MemDiffEntry sort
    // helper compare on the wrong predicate (8 of them were stuck at 16-82%).
    // The shipped binary's secondary key is a *non-strict* `>=` — see the
    // `subfc`/`adde`/`clrlwi.` sequence reached by the `beq` at 0x58 in the
    // target's inlined __linear_insert. That makes comp(a, a) true, which
    // STLport's unguarded insertion sort tolerates only because the table is
    // tiny; libstdc++ does not, so the native build uses the strict form.
    bool operator<(const MemDiffEntry &other) const {
        if (mHeap != other.mHeap)
            return mHeap < other.mHeap;
#ifdef HX_NATIVE
        return mSizeDiff > other.mSizeDiff;
#else
        return mSizeDiff >= other.mSizeDiff;
#endif
    }
};

extern MemTracker *gMemTracker;

bool StackLess(AllocInfo *const &a1, AllocInfo *const &a2) {
    return a1->StackCompare(*a2) < 0;
}

int HashKey(void *ptr, int size) {
    MILO_ASSERT((uint(ptr) & 7) == 0, 0x25);
    return (uint(ptr) / 8) % size;
}

void DiffTblReport(const char *name, BlockStatTable &curTable, BlockStatTable &prevTable, TextStream &ts) {
    curTable.SortByName();
    prevTable.SortByName();

    int curIdx = 0;
    int prevIdx = 0;
    std::vector<MemDiffEntry> diffs;
    int curNum = curTable.GetNumStats();
    int prevNum = prevTable.GetNumStats();
    // Sole residual row (98.83%): the image emits `add r4, r20, r22` BEFORE
    // `addi r3, r31, 0x58`, we emit the object address first.  Both operand
    // orders (`prevNum + curNum`) and hoisting the sum into its own unsigned
    // local are exactly INERT -- 98.8 / same two rows / same registers -- so
    // the ordering is the scheduler's, not the source's.
    diffs.reserve(curNum + prevNum);

    while (curIdx < curNum) {
        if (prevIdx >= prevNum)
            break;

        BlockStat &curStat = curTable.GetBlockStat(curIdx);
        BlockStat &prevStat = prevTable.GetBlockStat(prevIdx);

        int cmp = strcmp(curStat.mName, prevStat.mName);

        int numAllocs1, numAllocs2;
        int size1, size2;
        int heap;
        const char *entryName;

        if (cmp < 0) {
            entryName = curStat.mName;
            numAllocs1 = curStat.mNumAllocs;
            size1 = curStat.mSizeReq;
            numAllocs2 = 0;
            heap = curStat.mHeap;
            size2 = 0;
            curIdx++;
        } else if (cmp > 0) {
            entryName = prevStat.mName;
            numAllocs1 = 0;
            size1 = 0;
            numAllocs2 = prevStat.mNumAllocs;
            size2 = prevStat.mSizeReq;
            heap = prevStat.mHeap;
            prevIdx++;
        } else {
            entryName = curStat.mName;
            numAllocs1 = curStat.mNumAllocs;
            size1 = curStat.mSizeReq;
            numAllocs2 = prevStat.mNumAllocs;
            size2 = prevStat.mSizeReq;
            heap = prevStat.mHeap;
            curIdx++;
            prevIdx++;
        }

        int numDiff = numAllocs1 - numAllocs2;
        int sizeDiff = size1 - size2;

        if (numDiff != 0 || sizeDiff != 0) {
            MemDiffEntry entry;
            strncpy(entry.mName, entryName, 0x3a);
            entry.mName[0x3a] = '\0';
            entry.mNumDiff = numDiff;
            entry.mSizeDiff = sizeDiff;
            entry.mHeap = heap;
            diffs.push_back(entry);
        }
    }

    int totalBytes = 0;
    int totalNum = 0;

    std::sort(diffs.begin(), diffs.end());

    ts << MakeString("%-62s %8s %8s\n", name, "Num", "Bytes");

    int lastHeap = -2;
    for (std::vector<MemDiffEntry>::iterator it = diffs.begin(); it != diffs.end(); ++it) {
        if (it->mHeap != lastHeap) {
            ts << MakeString(" HEAP %d ------------------\n", it->mHeap);
            lastHeap = it->mHeap;
        }
        totalBytes += it->mSizeDiff;
        totalNum += it->mNumDiff;
        ts << MakeString("  %-60s %8d %8d\n", it->mName, it->mNumDiff, it->mSizeDiff);
    }

    ts << MakeString(" %-61s %8d %8d\n\n", "TOTAL ------", totalNum, totalBytes);
}

// Explicit template instantiation for MakeString with 4 array reference arguments
template const char *MakeString<const char(&)[9], const char(&)[3], const char(&)[9], const char(&)[8]>(
    const char *,
    const char(&)[9],
    const char(&)[3],
    const char(&)[9],
    const char(&)[8]
);

// RESIDUAL (w7-ai, 93.1%): every remaining row lives in target rows 41-65, the
// inlined AllocInfoVec(y) member init, and all of them are downstream of ONE
// missing instruction pair:
//
//     [57] clrlwi r5, r27, 2      ; r5 = y & 0x3fffffff, i.e. (unsigned)(y*4)/4
//     [65] stw    r5, 0x54(r31)   ; spilled before the first String ctor,
//                                 ; then overwritten with 0 at row 100 -- DEAD
//
// 0x54 is the slot that later holds the `(AllocInfo *)0` const-ref temp for the
// KeylessHash ctor, so MSVC coloured a dead scalar onto it. With that extra
// post-call work absent from our stream, MSVC has nothing to fill the pre-call
// slots with and hoists the `this + 0x18180` anchor ABOVE the DebugHeapAlloc
// call into callee-saved r26 (rows 41/43), where the image recomputes it into
// volatile r8 afterwards (rows 46/51) -- that scheduling difference is the
// other 8 rows. One root cause, not two.
//
// MEASURED NEGATIVE: spelling the division into AllocInfoVec's ctor as
// `mEndOfStorage(mStart + size * sizeof(AllocInfo *) / sizeof(AllocInfo *))`
// is BYTE-IDENTICAL -- MSVC folds the round trip, and it would in any case have
// had to show up in MemTracker::DiffDump's inlined copy of the same ctor, which
// has no clrlwi on either side (checked). So the division is NOT in the shared
// ctor; it is something in this function's own source that we are missing, and
// it must be live across the three String member ctors to get spilled at all.
//
// Also adjudicated and NOT a bug: the MakeString instantiation pair
// (`<const char(&)[19], int, const char(&)[5]>` target vs `<[15], int, [9]>`
// ours) is an ICF fold -- rows 82-85 reference the SAME
// ??_C@_0P@KDMJFNCI@MemTracker?4cpp and ??_C@_08LAKGMMIJ@mHashMem string
// symbols on both sides, so the array-size triple is just the fold
// representative's name.
MemTracker::MemTracker(int x, int y)
    : mHashMem(nullptr), mHashTable(nullptr), mTimeSlice(0), mCurStatTable(0),
      mFreedInfos(y), mLog(0), mReport(0), mHeap(x) {
    int hashSize = y * 2;
    mHashMem = DebugHeapAlloc(y * 8);
    MILO_ASSERT(mHashMem, 0x4E);
    mHashTable = new KeylessHash<void *, AllocInfo *>(
        hashSize, (AllocInfo *)0, (AllocInfo *)-1, (AllocInfo **)mHashMem
    );
    mFreeSysMem = _GetFreeSystemMemory();
    mFreePhysMem = _GetFreePhysicalMemory();
    DataRegisterFunc("spit_alloc_info", SpitAllocInfo);
    DataRegisterFunc("sai", SpitAllocInfo);
}

#ifdef HX_NATIVE
void *MemTracker::operator new(size_t size) { return DebugHeapAlloc(size); }
#else
void *MemTracker::operator new(unsigned int size) { return DebugHeapAlloc(size); }
#endif
void MemTracker::operator delete(void *mem) { DebugHeapFree(mem); }

const AllocInfo *MemTracker::GetInfo(void *info) const {
    AllocInfo **found = mHashTable->Find(info);
    if (found) {
        return *found;
    } else
        return nullptr;
}

void MemTracker::Alloc(
    int requestedSize,
    int actualSize,
    const char *type,
    void *memory,
    signed char heap,
    bool pooled,
    unsigned char strat,
    const char *file,
    int line
) {
    if (!gMemTrackerTracking)
        return;
    MILO_ASSERT(type, 0x6D);
    if (mHeap != -1 && heap != mHeap) {
        return;
    }
    gMemTrackerTracking = false;
    AllocInfo::bPrintCsv = true;
    if (!mHeapOnly) {
        AllocInfo *info = new AllocInfo(
            requestedSize,
            actualSize,
            type,
            memory,
            heap,
            pooled,
            strat,
            file,
            line,
            unk181b4,
            unk181ac
        );
        mHashTable->Insert(info);
        if (!pooled && gMemLogType != gNullStr && gMemLogType == type && mLog) {
            *mLog << " new, ";
            info->PrintCsv(*mLog);
            *mLog << "\n";
        } else if (!pooled && (mHeap == -1 || heap == mHeap) && strat == 0) {
            if (mLog) {
                *mLog << " ((com new) " << "(mem " << memory << ") " << *info << ")\n";
            }
            if (mSpew) {
                TheDebug << "::Alloc::" << info->mType << " Allocated "
                         << info->mActSize << " Requested " << info->mReqSize
                         << " Address " << (unsigned int)info->mMem << " Heap " << info->mHeap
                         << file << ":" << line << "\n";
            }
        }
    }
    if (!pooled) {
        mHeapStats[heap].Alloc(actualSize, requestedSize);
    }
    gMemTrackerTracking = true;
}

void MemTracker::Free(void *mem) {
    AllocInfo **found = mHashTable->Find(mem);
    if (found) {
        AllocInfo *info = *found;
        info->Validate();
        if (mLog && !info->mPooled && (mHeap == -1 || info->mHeap == mHeap)
            && info->mStrat == 0) {
            *mLog << " ((com free) " << "(" << mem << ") " << *info << ")\n";
        }
        if (!info->mPooled) {
            mHeapStats[info->mHeap].Free(info->mActSize, info->mReqSize);
        }
        mHashTable->Remove(found);
        if (info->mTimeSlice == mTimeSlice) {
            delete info;
        } else {
            mFreedInfos.push_back(info);
        }
    }
}

void MemTracker::ColatedPrint(TextStream &ts, AllocInfo *info, const char *com) {
    ts << "  ((com " << com << ") (rep " << 1 << " ) " << *info << ")\n";
}

void MemTracker::CloseReport() {
    if (mReport) {
        MemNumHeaps();
        TextStream &ts = *mReport;
        ts << "\n";
        ts << "\n";
        ts << "Category,CategoryName,Column,Budget,BudgetType,AlwaysShow,Tooltip\n";
        ts << "column_info,overview,Mode,0,0,1,notes\n";
        ts << "column_info,overview,MainPeak,0,0,1,notes\n";
        ts << "column_info,overview,MainAlloc,0,0,1,notes\n";
        ts << "column_info,overview,MainLargest,0,1,0,notes\n";
        ts << "column_info,overview,CharPeak,0,0,1,notes\n";
        ts << "column_info,overview,CharAlloc,0,0,1,notes\n";
        ts << "column_info,overview,CharLargest,0,1,0,notes\n";
        ts << "column_info,overview,PhysPeak,0,0,1,notes\n";
        ts << "column_info,overview,PhysAlloc,0,0,1,notes\n";
        ts << "column_info,overview,PhysLargest,0,0,1,notes\n";
        ts << "column_info,base,heap,-1.0,-1,1,heap name\n";
        ts << "column_info,base,free,0,0,0,bytes free in heap\n";
        ts << "column_info,base,biggest,0,0,1,size of largest free block\n";
        ts << "column_info,base,lfrags,0,0,0,fragmentation count at low end of memory\n";
        ts << "column_info,base,requested,0,0,0,amount of memory actually requested\n";
        ts << "column_info,base,allocated,0,0,1,amount of memory actually allocated\n";
        ts << "column_info,base,peak,0,0,1,memory high water mark\n";
        ts << "column_info,game,heap,-1.0,-1,1,heap name\n";
        ts << "column_info,game,free,0,0,0,bytes free in heap\n";
        ts << "column_info,game,biggest,0,0,1,size of largest free block\n";
        ts << "column_info,game,lfrags,0,0,0,fragmentation count at low end of memory\n";
        ts << "column_info,game,requested,0,0,0,amount of memory actually requested\n";
        ts << "column_info,game,allocated,0,0,1,amount of memory actually allocated\n";
        ts << "column_info,game,peak,0,0,1,memory high water mark\n";
        ts << "\n";
        ts << "Category,CategoryName\n";
        ts << "category_info,game\n";
        ts << "category_info,base\n";
        ts << "\nDone\n";
        mReport->File().Flush();
        RELEASE(mReport);
    }
}

void MemTracker::SetAllocInfoName(const char *name) {
    Hx_snprintf(mAllocInfoName, 64, "%s", name);
}

void MemTracker::StartLog(TextStream &ts) {
    if (mLog) {
        StopLog();
    }
    MILO_ASSERT(!mLog, 0x113);
    *mLog = ts;
    *mLog << "(elf " << TheSystemArgs.front() << ")\n";
    *mLog << "(data\n";
}

void MemTracker::StopLog() {
    if (mLog) {
        *mLog << ")";
        mLog = nullptr;
    }
}

void MemTracker::Realloc(void *key, int reqSize, int actualSize, void *mem) {
    AllocInfo **found = mHashTable->Find(key);
    if (found) {
        AllocInfo *info = *found;
        info->Validate();
        bool validHeap = mHeap == -1 || info->mHeap == mHeap;
        MILO_ASSERT(validHeap, 0xF6);
        if (reqSize == -1) {
            reqSize = info->mReqSize;
        }
        if (actualSize == -1) {
            actualSize = info->mActSize;
        }
        signed char heap = info->mHeap;
        unsigned char strat = info->mStrat;
        const char *type = info->mType;
        MILO_ASSERT(info->mPooled == 0, 0x100);
        Free(key);
        Alloc(reqSize, actualSize, type, mem, heap, false, strat, __FILE__, 0x102);
    }
}

void MemTracker::HeapReport(TextStream &ts) {
    int max = MemNumHeaps() + 1;
    for (int i = 0; i < max; i++) {
        HeapStats &curStats = mHeapStats[i];
        ts << MakeString("\n*** FREE LIST for heap #%d ***\n", i);
        if (i == MemNumHeaps()) {
            ts << MakeString("  Heap name          = %14s\n", "physical");
            ts << MakeString("  Heap size          = %14d\n", mFreePhysMem);
            ts << MakeString(
                "  Num Free Bytes     = %14d\n", mFreePhysMem - PhysicalUsage()
            );
            ts << MakeString("  Biggest Free Block = %14d\n", _GetFreePhysicalMemory());
            ts << MakeString("  Num Free Blocks    = %14s\n", "N/A");
        } else {
            int i1, i2, i3, i4, i5;
            MemFreeBlockStats(i, i1, i2, i3, i4, i5);
            ts << MakeString("  Heap name          = %14s\n", MemHeapName(i));
            ts << MakeString("  Heap size          = %14d\n", MemHeapSize(i));
            ts << MakeString("  Num Free Bytes     = %14d\n", i3);
            ts << MakeString("  Biggest Free Block = %14d\n", i5);
            ts << MakeString("  lFrags             = %14d\n", i1);
        }
        ts << MakeString("  Num Allocs         = %14d\n", curStats.mTotalNumAllocs);
        ts << MakeString("  Bytes Requested    = %14d\n", curStats.mTotalReqSize);
        ts << MakeString("  Bytes Allocated    = %14d\n", curStats.mTotalActSize);
        ts << MakeString("  Peak Num Allocs    = %14d\n", curStats.mMaxNumAllocs);
        ts << MakeString("  Peak Bytes Alloc'd = %14d\n", curStats.mMaxActSize);
    }
}

void MemTracker::UpdateStats() {
    mPoolTable[mCurStatTable].Clear();
    mMemTable[mCurStatTable].Clear();
    for (auto it = mHashTable->Begin(); it != nullptr; it = mHashTable->Next(it)) {
        AllocInfo *info = *it;
        if (info->mPooled) {
            mPoolTable[mCurStatTable].Update(
                info->mType, info->mHeap, info->mReqSize, info->mActSize
            );
        } else {
            mMemTable[mCurStatTable].Update(
                info->mType, info->mHeap, info->mReqSize, info->mActSize
            );
        }
    }
}

DataNode MemTracker::SpitAllocInfo(DataArray *a) {
    int ret = 1;
    if (a && a->Size() > 1) {
        TextFileStream stream(a->Str(1), false);
        ret = SpitAllocInfo(&stream);
    }
    return ret;
}

void MemTracker::Report(int threshold, TextStream &ts) {
    int numMemBlocks, numPoolBlocks;

    HeapReport(ts);
    UpdateStats();

    mMemTable[mCurStatTable].SortBySize();
    numMemBlocks = mMemTable[mCurStatTable].GetNumStats();
    ts << MakeString("\n  %-30s %2s %5s %10s %10s\n", "TYPE", "Hp", "Num", "SzRequest", "SzActual");

    for (int i = 0; i < numMemBlocks; i++) {
        BlockStat &stat = mMemTable[mCurStatTable].GetBlockStat(i);
        if (stat.mSizeAct >= threshold) {
            ts << MakeString(
                "  %-30s %2d %5d %10d %10d\n",
                stat.mName, stat.mHeap, stat.mNumAllocs, stat.mSizeReq, stat.mSizeAct
            );
        }
    }

    mPoolTable[mCurStatTable].SortBySize();
    numPoolBlocks = mPoolTable[mCurStatTable].GetNumStats();
    ts << MakeString("\n  %-30s %5s %10s %10s\n", "POOL TYPE", "Num", "SzRequest", "SzActual");

    for (int i = 0; i < numPoolBlocks; i++) {
        BlockStat &stat = mPoolTable[mCurStatTable].GetBlockStat(i);
        if (stat.mSizeAct >= threshold) {
            ts << MakeString("  %-30s %5d %10d %10d\n", stat.mName, stat.mNumAllocs, stat.mSizeReq, stat.mSizeAct);
        }
    }

    ts << "Diff from last report:\n";
    DiffTblReport("MALLOC DIFF TYPES", mMemTable[mCurStatTable], mMemTable[1 - mCurStatTable], ts);

    DiffTblReport("POOL DIFF TYPES", mPoolTable[mCurStatTable], mPoolTable[1 - mCurStatTable], ts);

    mCurStatTable = 1 - mCurStatTable;
}

int MemTracker::SpitAllocInfo(TextStream *ts) {
    int ret = 1;
    if (gMemTracker != nullptr && gMemTracker->mHashTable != nullptr) {
        // Identical in shape to the _iobuf overload below, and it has to be:
        // the image sends both banners to TheDebug (r29 holds &TheDebug across
        // the loop), NOT to `ts`, with a single operator<< each and no trailing
        // "\n" -- the format string already ends in one. The two FormatStrings
        // also share the stack slot at r1+0x50 (frame 0x1090, not 0x20b0), so
        // each lives in its own scope.
        {
            FormatString fmt("----------------BEGIN MemTracker::SpitAllocInfo\n");
            TheDebug << fmt.Str();
        }
        for (auto it = gMemTracker->mHashTable->Begin(); it != nullptr; it = gMemTracker->mHashTable->Next(it)) {
            AllocInfo *info = *it;
            info->PrintForReport(*ts);
        }
        {
            FormatString fmt("----------------END MemTracker::SpitAllocInfo\n");
            TheDebug << fmt.Str();
        }
        ret = 0;
    }
    return ret;
}

int MemTracker::SpitAllocInfo(struct _iobuf *file) {
    int ret = 1;
    if (gMemTracker != nullptr && gMemTracker->mHashTable != nullptr) {
        {
            FormatString fmt("----------------BEGIN MemTracker::SpitAllocInfo\n");
            TheDebug << fmt.Str();
        }
        for (auto it = gMemTracker->mHashTable->Begin(); it != nullptr; it = gMemTracker->mHashTable->Next(it)) {
            AllocInfo *info = *it;
            info->PrintForReport(file);
        }
        {
            FormatString fmt("----------------END MemTracker::SpitAllocInfo\n");
            TheDebug << fmt.Str();
        }
        ret = 0;
    }
    return ret;
}

// RESIDUAL at 95.37 canonical (w7-at, 2026-09-14). 110 of 137 instructions are
// equal and the structure is right; what is left is 16 callee-saved register
// renames (r23<->r27, r24<->r28) plus ONE three-instruction reordering at target
// rows 49-59, where the image materialises the DebugHeapAlloc result into a
// callee-saved register and stores THAT to both 0x50 and 0x54 --
//   mr r27,r3 / add r10,r30,r3 / mr r24,r3 / stw r27,0x50 / mr r29,r3 /
//   stw r27,0x54 / mr r3,r11 / lwz r4,0(r11) / stw r10,0x58
// -- so its AllocInfoVec words land in declaration order 0x50, 0x54, 0x58 and
// the image already holds allocIt (r29) and the Free() pointer (r24) before the
// push_back loop even starts. We store r3 straight to 0x50 and emit 0x58 in the
// middle. Same instruction multiset, different schedule.
//
// TWO NEGATIVE RESULTS, both byte-identical output:
//  - hoisting `allocIt`/`allocEnd` above the sorts and passing them AS the sort
//    arguments (so the image's reuse of the sort's argument registers as the
//    loop variables has a source spelling): MSVC already CSEs this.
//  - rewriting AllocInfoVec(int) from a member-init list to three body
//    assignments in declaration order, to force the 0x50/0x54/0x58 store order:
//    MSVC canonicalises the two forms. (AllocInfo.h is PCH-reached, so this was
//    measured with a full ninja; since the objects did not change there is no
//    binary-wide delta to report.)
// Adjudicated and NOT a bug: the "alloc"/"free" literals look swapped at rows
// 89-93 but the uses at rows 119/128 swap back -- both sides pass "alloc" on the
// allocIt arm and "free" on the freedIt arm. It is only which of r25/r26 holds
// which literal.
void MemTracker::DiffDump(TextStream &ts) {
    if (mTimeSlice) {
        ts << "(executable " << TheSystemArgs.front() << ")\n";
        ts << "(data\n";
        {
            int count = 0;
            for (AllocInfo **it = mHashTable->Begin(); it; it = mHashTable->Next(it)) {
                if (mTimeSlice == (*it)->mTimeSlice) {
                    count++;
                }
            }
            AllocInfoVec allocVec(count);
            for (AllocInfo **it = mHashTable->Begin(); it; it = mHashTable->Next(it)) {
                if (mTimeSlice == (*it)->mTimeSlice) {
                    allocVec.push_back(*it);
                }
            }
            std::sort(allocVec.begin(), allocVec.end(), StackLess);
            std::sort(mFreedInfos.begin(), mFreedInfos.end(), StackLess);

            AllocInfo **freedIt = mFreedInfos.begin();
            AllocInfo **allocIt = allocVec.begin();
            AllocInfo **allocEnd = allocVec.end();

            for (; allocIt != allocEnd || freedIt != mFreedInfos.end();) {
                if (allocIt == allocEnd) {
                    ColatedPrint(ts, *freedIt, "free");
                    freedIt++;
                } else if (freedIt == mFreedInfos.end()) {
                    ColatedPrint(ts, *allocIt, "alloc");
                    allocIt++;
                } else {
                    int cmp = (*allocIt)->StackCompare(**freedIt);
                    if (cmp < 0) {
                        ColatedPrint(ts, *allocIt, "alloc");
                        allocIt++;
                    } else if (cmp > 0) {
                        ColatedPrint(ts, *freedIt, "free");
                        freedIt++;
                    } else {
                        allocIt++;
                        freedIt++;
                    }
                }
            }
            allocVec.Free();
        }
        ts << ")\n";
    }
    mFreedInfos.delete_and_clear();
    mTimeSlice++;
}

#include "hamobj\HamGameData.h"
#include "hamobj\HamPlayerData.h"

void MemTracker::ReportMemoryAlloc(const char *name) {
    const char *venueStr = TheGameData->Venue().Str();
    const char *char0 = 0;
    const char *char1 = 0;
    Symbol song = TheGameData->GetSong();
    HamPlayerData *p0 = TheGameData->Player(0);
    if (p0) {
        char0 = p0->Char().Str();
    }
    HamPlayerData *p1 = TheGameData->Player(1);
    if (p1) {
        char1 = p1->Char().Str();
    }
    char buf[128];
    Hx_snprintf(buf, 0x80, "%s_%s_%s_%s_%s_%s_alloc_info.csv",
                mAllocInfoName, name, venueStr, char0, char1, song.Str());
    TextFileStream stream(buf, false);
    SpitAllocInfo(&stream);
    stream.File().Flush();
}

static bool sReportHeaderWritten = false;

void MemTracker::ReportMemoryUsage(const char *name) {
    TextStream *ts = &TheDebug;
    if (mReport) {
        ts = mReport;
    }
    if (!sReportHeaderWritten) {
        FormatString hdr("Category,heap,free,biggest,lfrags,requested,allocated,peak\n");
        *ts << hdr.Str();
        sReportHeaderWritten = true;
    }
    int numHeaps = MemNumHeaps();
    for (int i = 0; i < numHeaps + 1; i++) {
        {
            FormatString nameStr(name);
            *ts << nameStr.Str();
        }
        if (i == MemNumHeaps()) {
            int freeMem = _GetFreePhysicalMemory();
            int used = mFreePhysMem - PhysicalUsage();
            if (used < freeMem) {
                used = freeMem;
            }
            {
                FormatString physHeap(",physicalHeap");
                *ts << physHeap.Str();
            }
            *ts << MakeString(",%d", used);
            *ts << MakeString(",%d", freeMem);
            {
                FormatString zero(",0");
                *ts << zero.Str();
            }
        } else {
            int lfrags, i2, free, i4, biggest;
            MemFreeBlockStats(i, lfrags, i2, free, i4, biggest);
            const char *heapName = MemHeapName(i);
            *ts << MakeString(",%sHeap", heapName);
            *ts << MakeString(",%d", free);
            *ts << MakeString(",%d", biggest);
            *ts << MakeString(",%d", lfrags);
        }
        HeapStats &stats = mHeapStats[i];
        *ts << MakeString(",%d", stats.mTotalReqSize);
        *ts << MakeString(",%d", stats.mTotalActSize);
        *ts << MakeString(",%d\n", stats.mMaxActSize);
    }
}

void MemTracker::ReportMemoryUsageOverview(const char *name) {
    TextStream *ts = &TheDebug;
    if (mReport) {
        ts = mReport;
    }
    FormatString hdr(
        "\nCategory,Mode,MainPeak,MainAlloc,MainLargest,CharPeak,CharAlloc,CharLargest,"
        "PhysPeak,PhysAlloc,PhysLargest\n"
    );
    *ts << hdr.Str();
    // +1 folded into the call's result (image: `addi r26, r3, 0x1` immediately
    // after `bl MemNumHeaps`, before the two stream writes), so there is no
    // separate numHeaps local.
    int loopMax = MemNumHeaps() + 1;
    // Chained, not two statements: the image feeds operator<<'s returned
    // TextStream& straight into the second call (`mr r4, r30; bl` with no
    // intervening `mr r3, r31`).
    *ts << "overview," << name;
    for (int i = 0; i < loopMax; i++) {
        // NOTE (w7-ai): the five locals share one declaration with the
        // MemFreeBlockStats call precisely because the image's physical-heap
        // arm writes into TWO OF THEM rather than into fresh locals. Slot map
        // from the target listing -- the call passes r4=0x58, r5=0x60, r6=0x50,
        // r7=0x5c, r8=0x54 -- so `free` is 0x50 and `lfrags` is 0x58, and those
        // are exactly the two slots the physical arm stores to. Declaring
        // `used` as a private local instead let MSVC dead-code the whole
        // `mFreePhysMem - PhysicalUsage()` computation away (9 deleted
        // instructions); it only survives because these locals have had their
        // address taken in the sibling arm.
        //
        // BEHAVIOUR, faithful to the image and deliberately preserved: the
        // physical arm never assigns `biggest`, so PhysLargest prints an
        // uninitialised slot. The image reads r1+0x54 (the call's LAST
        // out-param) having only written r1+0x50 and r1+0x58.
        int lfrags, i2, free, i4, biggest;
        if (i == MemNumHeaps()) {
            int freeMem = _GetFreePhysicalMemory();
            free = mFreePhysMem - PhysicalUsage();
            if (free < freeMem) {
                free = freeMem;
            }
            lfrags = 0;
        } else {
            MemFreeBlockStats(i, lfrags, i2, free, i4, biggest);
        }
        HeapStats &stats = mHeapStats[i];
        *ts << MakeString(",%d", stats.mMaxActSize);
        *ts << MakeString(",%d", stats.mTotalActSize);
        *ts << MakeString(",%d", biggest);
    }
}

#ifndef HX_NATIVE
// Forward declaration for __pop_heap_aux template specialization
namespace stlpmtx_std {
    extern void __pop_heap_aux(MemDiffEntry*, MemDiffEntry*, int, less<MemDiffEntry>);
}

// Template specialization for sort_heap<MemDiffEntry*, less<MemDiffEntry>>
namespace stlpmtx_std {
    template <>
    void sort_heap<MemDiffEntry* __restrict, less<MemDiffEntry>>(MemDiffEntry* __restrict __first, MemDiffEntry* __restrict __last, less<MemDiffEntry> __comp) {
        while ((__last - __first) / 72 > 1) {
            __pop_heap_aux(__first, __last, 0, __comp);
            __last -= 72;
        }
    }
}
#endif
