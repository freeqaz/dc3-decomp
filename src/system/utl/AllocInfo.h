#pragma once
#include "MemTrack.h"
#include "os\Debug.h"
#include "utl\Str.h"
#include "utl\trie.h"
#include "utl\TextStream.h"

// size 0x65
#pragma pack(push, 1)
class AllocInfo {
public:
    AllocInfo(
        int requestedSize,
        int actualSize,
        const char *type,
        void *mem,
        signed char heap,
        bool pooled,
        unsigned char strat,
        const char *file,
        int line,
        String &,
        String &
    );
    ~AllocInfo();

    int Compare(const AllocInfo &) const;
    void FillStackTrace();
    void Validate() const;

    void PrintCsv(TextStream &) const;
    void PrintForReport(TextStream &) const;
    void PrintForReport(struct _iobuf *) const;
    void Print(TextStream &) const;
    int StackCompare(const AllocInfo &) const;

    static bool bPrintCsv;
    static void SetPoolMemory(void *, int);
#ifdef HX_NATIVE
    static void *operator new(size_t);
#else
    static void *operator new(unsigned int);
#endif
    static void operator delete(void *);

    int mReqSize; // 0x0
    int mActSize; // 0x4
    const char *mType; // 0x8
    void *mMem; // 0xc
    signed char mHeap; // 0x10
    bool mPooled; // 0x11
    short mTimeSlice; // 0x12
    unsigned char mStrat; // 0x14
    const char *mFile; // 0x15
    int mLine; // 0x19
    unsigned int unk1d; // 0x1d
    unsigned int unk21; // 0x21
    int mStackTrace[0x10]; // 0x25
};
#pragma pack(pop)

TextStream &operator<<(TextStream &, const AllocInfo &);

class AllocInfoVec {
public:
    AllocInfoVec() : mStart(0), mEnd(0), mEndOfStorage(0) {}
    // STLport-shaped: `allocate(size, size)` hands the byte count back as an
    // element count through the reference (see Allocate). In the shipped
    // MemTracker::MemTracker that write-back is visible as `clrlwi r5, r27, 2`
    // (y & 0x3fffffff = (y*4)/4 unsigned, 827DB768) spilled to a frame temp
    // (`stw r5, 0x54(r31)`, 827DB788) that nothing reads; the plain
    // `DebugHeapAlloc(size * sizeof(AllocInfo *))` init could not produce
    // either. Must stay __forceinline: without it MSVC emits an out-of-line
    // ??0AllocInfoVec call (measured, w7-bu).
    __forceinline AllocInfoVec(unsigned int size)
        : mStart(Allocate(size, size)), mEnd(mStart), mEndOfStorage(mStart + size) {}
    // NO destructor.  The shipped MemTracker::DiffDump carries pdata flag
    // 0x40008603 -- the exception-handler bit CLEAR -- while DiffTblReport in
    // the same TU is 0xC000A404, so DiffDump has no unwind region at all.  It
    // nevertheless holds an AllocInfoVec at 0x50(r1) (its address is passed to
    // the out-of-line AllocInfoVec::push_back) and frees that buffer with a
    // single `bl DebugHeapFree` at the one normal exit.  A destructor-bearing
    // local would have forced an unwind region; an explicit Free() call does
    // not.  MemTracker itself is a never-destroyed global, so mFreedInfos does
    // not need one either.
    void Free() { DebugHeapFree(mStart); }

    AllocInfo **begin() { return mStart; }
    AllocInfo **end() { return mEnd; }

    void push_back(AllocInfo *info) {
        MILO_ASSERT(mEnd < mEndOfStorage, 0x61);
        *mEnd++ = info;
    }

    void delete_and_clear() {
        for (auto it = mStart; it != mEnd; ++it) {
            AllocInfo *info = *it;
            if (info) {
                delete info;
            }
        }
        mEnd = mStart;
    }

private:
    // STLport-shaped allocate(n, allocated_n): the byte count is handed back
    // as an element count through the reference.
    static __forceinline AllocInfo **Allocate(unsigned int n, unsigned int &allocatedN) {
        unsigned int bytes = n * sizeof(AllocInfo *);
        AllocInfo **p = (AllocInfo **)DebugHeapAlloc(bytes);
        allocatedN = bytes / sizeof(AllocInfo *);
        return p;
    }

    AllocInfo **mStart; // 0x0
    AllocInfo **mEnd; // 0x4
    AllocInfo **mEndOfStorage; // 0x8
};

void AllocInfoInit();