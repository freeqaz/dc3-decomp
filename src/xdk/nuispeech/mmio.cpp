#include "xdk\nui\mmio.h"
#include "xdk\XBOXKRNL.h"
#include "xdk\xapilibi\rtlheap.h"
#include <cstring>

// The winmm-style handle table this TU owns: every HANDLE it hands out points
// 0x2c bytes past a tagHNDL header, and the headers are chained through pNext
// off the pHandleList singly-linked list.  Sizes come from FreeHandle itself --
// it does `subi r31, r3, 0x2c` and zeroes +0x0/+0x4/+0x8 before RtlFreeHeap.
struct tagHNDL {
    tagHNDL *pNext; // 0x0
    void *pOwner; // 0x4
    unsigned long dwFlags; // 0x8
    unsigned char reserved[0x20]; // 0xc, payload starts at 0x2c
};

extern "C" {
RTL_CRITICAL_SECTION HandleListCritSec;
void *hHeap;
}
tagHNDL *pHandleList;

void FreeHandle(HANDLE h) {
    if (h == nullptr) {
        return;
    }

    tagHNDL *hndl = (tagHNDL *)((unsigned char *)h - 0x2c);

    RtlEnterCriticalSection(&HandleListCritSec);

    tagHNDL **ppLink = &pHandleList;
    while (*ppLink != nullptr) {
        tagHNDL *cur = *ppLink;
        if (cur == hndl) {
            *ppLink = hndl->pNext;
            RtlLeaveCriticalSection(&HandleListCritSec);
            hndl->pOwner = nullptr;
            hndl->dwFlags = 0;
            hndl->pNext = nullptr;
            RtlFreeHeap(hHeap, 0, hndl);
            return;
        }
        ppLink = &cur->pNext;
    }

    RtlLeaveCriticalSection(&HandleListCritSec);
}

MMRESULT mmioGetInfo(HMMIO hmmio, LPMMIOINFO pmmioinfo, UINT fuInfo) {
    if (hmmio == nullptr) {
        return 5;
    }
    if (pmmioinfo == nullptr) {
        return 11;
    }
    memcpy(pmmioinfo, hmmio, 0x48); // should be sizeof(MMIOINFO) but fsr mine is 4 bytes
                                    // larger
    return 0;
}

MMRESULT mmioSetInfo(HMMIO hmmio, LPCMMIOINFO pmmioinfo, UINT fuInfo) {
    if (hmmio == nullptr) {
        return 5;
    }
    if (pmmioinfo == nullptr) {
        return 11;
    }
    memcpy(hmmio, pmmioinfo, 0x48); // should be sizeof(MMIOINFO) but fsr mine is 4 bytes
                                    // larger
    LPMMIOINFO new_info = (LPMMIOINFO)hmmio;
    if (new_info->pchEndRead < new_info->pchNext) {
        new_info->pchEndRead = new_info->pchNext;
    }
    return 0;
}

FOURCC mmioStringToFOURCCW(LPCSTR sz, UINT uFlags) { return 0; }

__declspec(noinline) MMRESULT mmioFlush(HMMIO hmmio, UINT fuFlush) { return 0; }

LONG mmioSeek(HMMIO hmmio, LONG lOffset, int iOrigin) { return 0; }

__declspec(noinline) MMRESULT mmioSetBuffer(HMMIO hmmio, LPSTR pchBuffer, LONG cchBuffer, UINT fuBuffer) {
    return 0;
}

HMMIO mmioOpenW(LPWSTR pszFileName, LPMMIOINFO pmmioinfo, DWORD fdwOpen) {
    return nullptr;
}

// mmioClose's residual (79.3%) is NOT a source bug in mmioClose.  The census
// files it as WRONG_CALLEE (`target mmioSetBuffer vs base FreeHandle@@`), but
// the source below already calls mmioSetBuffer -- MSVC DELETES the call,
// because mmioSetBuffer is an 8-byte `return 0;` stub in this same TU and the
// result is discarded, so it is provably side-effect-free.  `noinline` stops
// inlining, not elimination.  The same same-TU knowledge lets MSVC keep
// fuClose in the VOLATILE r5 across `bl mmioFlush` (our stub clobbers nothing
// but r3) where the image must park it in r30 -- which is the whole prologue
// and frame-size delta as well.
//
// The image's mmioFlush is 0xB8 bytes and its mmioSetBuffer is 0x16C; ours are
// 8 each.  This row cannot close until those two are reconstructed.
//
// NEGATIVE, measured 2026-08-23: moving both stub DEFINITIONS below mmioClose,
// on the theory that MSVC only optimises against a body it has already seen,
// produced a BYTE-IDENTICAL object (79.3%, same 8 deletes, same diff_args).
// The elimination is a whole-TU pass, not a top-down one.  Do not retry it.
MMRESULT mmioClose(HMMIO hmmio, UINT fuClose) {
    if (hmmio == nullptr) {
        return 5;
    }
    uint flush_ret = mmioFlush(hmmio, 0);
    if (flush_ret != 0)
        return flush_ret;
    LPMMIOINFO info = (LPMMIOINFO)hmmio;
    uint proc_ret = info->pIOProc(info, 4, fuClose, 0);
    if (proc_ret != 0)
        return proc_ret;

    mmioSetBuffer(hmmio, 0, 0, 0);
    FreeHandle(hmmio);

    return 0;
}

MMRESULT mmioAdvance(HMMIO hmmio, LPMMIOINFO pmmioinfo, UINT fuAdvance) { return 0; }

LONG mmioRead(HMMIO hmmio, HPSTR pch, LONG cch) { return 0; }

LONG mmioWrite(HMMIO hmmio, const char *pch, LONG cch) { return 0; }
