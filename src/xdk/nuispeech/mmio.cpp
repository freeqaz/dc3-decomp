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

// xapilibi's LOCAL heap allocators.  This TU is not linked (the target object
// is), but these are real exports: ham_xbox_r.map has LocalAlloc at 0x823363e8
// (xapilibi:localalloc.obj), LocalFree at 0x82336440 and LocalReAlloc at
// 0x82de1d68.
void *LocalAlloc(UINT uFlags, SIZE_T uBytes);
void *LocalFree(void *hMem);
void *LocalReAlloc(void *hMem, SIZE_T uBytes, UINT uFlags);
}
tagHNDL *pHandleList;

// Win32 multimedia constants, as spelled by mmsystem.h.
enum {
    MMSYSERR_INVALHANDLE = 5,
    MMSYSERR_INVALPARAM = 11,
    MMIOERR_OUTOFMEMORY = 0x102,
    MMIOERR_CANNOTWRITE = 0x106,

    MMIOM_SEEK = 2,
    MMIOM_CLOSE = 4,
    MMIOM_WRITEFLUSH = 5,

    MMIO_EMPTYBUF = 0x00000010,
    MMIO_ALLOCBUF = 0x00010000,
    MMIO_DIRTY = 0x10000000,

    LMEM_FIXED = 0x0000,
    LMEM_MOVEABLE = 0x0002,

    // mmioFOURCC('M','E','M',' ') -- the in-memory I/O procedure never buffers.
    FOURCC_MEM = 0x204D454D
};

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
        return MMSYSERR_INVALHANDLE;
    }
    if (pmmioinfo == nullptr) {
        return MMSYSERR_INVALPARAM;
    }
    memcpy(pmmioinfo, hmmio, 0x48); // should be sizeof(MMIOINFO) but fsr mine is 4 bytes
                                    // larger
    return 0;
}

MMRESULT mmioSetInfo(HMMIO hmmio, LPCMMIOINFO pmmioinfo, UINT fuInfo) {
    if (hmmio == nullptr) {
        return MMSYSERR_INVALHANDLE;
    }
    if (pmmioinfo == nullptr) {
        return MMSYSERR_INVALPARAM;
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

// Every buffered transfer goes through here so the I/O procedure's file
// pointer is re-seeked to the offset the buffer actually represents before the
// read/write is issued.
LONG mmioDiskIO(LPMMIOINFO pmmioinfo, UINT uMsg, HPSTR pch, LONG cch) {
    if (pmmioinfo->lDiskOffset != pmmioinfo->lBufOffset) {
        if (pmmioinfo->pIOProc(pmmioinfo, MMIOM_SEEK, pmmioinfo->lBufOffset, 0) == -1) {
            return -1;
        }
    }
    return pmmioinfo->pIOProc(pmmioinfo, uMsg, (LONG)pch, cch);
}

MMRESULT mmioFlush(HMMIO hmmio, UINT fuFlush) {
    if (hmmio == nullptr) {
        return MMSYSERR_INVALHANDLE;
    }
    LPMMIOINFO info = (LPMMIOINFO)hmmio;
    if (info->fccIOProc != FOURCC_MEM && info->pchBuffer != nullptr) {
        if (info->dwFlags & MMIO_DIRTY) {
            LONG cch = info->pchEndRead - info->pchBuffer;
            if (mmioDiskIO(info, MMIOM_WRITEFLUSH, info->pchBuffer, cch) != cch) {
                return MMIOERR_CANNOTWRITE;
            }
            info->dwFlags &= ~MMIO_DIRTY;
        }
        if (fuFlush & MMIO_EMPTYBUF) {
            LONG cchUsed = info->pchNext - info->pchBuffer;
            info->pchEndRead = info->pchBuffer;
            info->pchNext = info->pchBuffer;
            info->lBufOffset += cchUsed;
        }
    }
    return 0;
}

LONG mmioSeek(HMMIO hmmio, LONG lOffset, int iOrigin) { return 0; }

MMRESULT mmioSetBuffer(HMMIO hmmio, LPSTR pchBuffer, LONG cchBuffer, UINT fuBuffer) {
    if (hmmio == nullptr) {
        return MMSYSERR_INVALHANDLE;
    }
    LPMMIOINFO info = (LPMMIOINFO)hmmio;
    if ((info->dwFlags & MMIO_ALLOCBUF) && pchBuffer == nullptr && cchBuffer > 0) {
        // Resize the buffer we already own, keeping its contents and both
        // cursors.  Anything past the requested size has to be flushed out
        // first, so retry with MMIO_EMPTYBUF until the used span fits.
        UINT fuFlush = 0;
        LONG cchNext;
        LONG cchEndRead;
        HPSTR pchOld;
        for (;; fuFlush = MMIO_EMPTYBUF) {
            MMRESULT flushRet = mmioFlush(hmmio, fuFlush);
            if (flushRet != 0) {
                return flushRet;
            }
            pchOld = info->pchBuffer;
            cchNext = info->pchNext - pchOld;
            cchEndRead = info->pchEndRead - pchOld;
            if (cchBuffer >= cchNext) {
                break;
            }
        }
        HPSTR pchNew = (HPSTR)LocalReAlloc(pchOld, cchBuffer, LMEM_MOVEABLE);
        if (pchNew == nullptr) {
            return MMIOERR_OUTOFMEMORY;
        }
        info->cchBuffer = cchBuffer;
        info->pchBuffer = pchNew;
        info->pchNext = pchNew + cchNext;
        info->pchEndRead = pchNew + cchEndRead;
        info->pchEndWrite = pchNew + cchBuffer;
        if (cchEndRead > cchBuffer) {
            info->pchEndRead = pchNew + cchBuffer;
        }
        return 0;
    }

    MMRESULT flushRet = mmioFlush(hmmio, MMIO_EMPTYBUF);
    if (flushRet != 0) {
        return flushRet;
    }
    if (info->dwFlags & MMIO_ALLOCBUF) {
        LocalFree(info->pchBuffer);
        info->dwFlags &= ~MMIO_ALLOCBUF;
    }
    MMRESULT ret = 0;
    if (pchBuffer == nullptr && cchBuffer > 0) {
        HPSTR pchNew = (HPSTR)LocalAlloc(LMEM_FIXED, cchBuffer);
        if (pchNew != nullptr) {
            info->dwFlags |= MMIO_ALLOCBUF;
            pchBuffer = pchNew;
        } else {
            ret = MMIOERR_OUTOFMEMORY;
            cchBuffer = 0;
        }
    }
    info->pchBuffer = pchBuffer;
    info->cchBuffer = cchBuffer;
    info->pchEndRead = pchBuffer;
    info->pchNext = pchBuffer;
    info->pchEndWrite = pchBuffer + cchBuffer;
    return ret;
}

HMMIO mmioOpenW(LPWSTR pszFileName, LPMMIOINFO pmmioinfo, DWORD fdwOpen) {
    return nullptr;
}

// mmioClose read 79.3% for as long as mmioFlush and mmioSetBuffer were 8-byte
// `return 0;` stubs in this same TU: the source below already called
// mmioSetBuffer, but MSVC could see the whole stub body, prove the call
// side-effect-free with its result discarded, and DELETE it -- taking the
// `li r4/r5/r6, 0` argument set-up with it.  (`__declspec(noinline)` stops
// inlining, not elimination; and moving the stub definitions below mmioClose
// produced a byte-identical object, so the pass is whole-TU, not top-down.)
// The same knowledge let MSVC keep fuClose in the VOLATILE r5 across
// `bl mmioFlush`, where the image has to park it in r30 -- which was also the
// whole prologue and frame-size delta.  Reconstructing the two real bodies
// closes all of it.
MMRESULT mmioClose(HMMIO hmmio, UINT fuClose) {
    if (hmmio == nullptr) {
        return MMSYSERR_INVALHANDLE;
    }
    uint flush_ret = mmioFlush(hmmio, 0);
    if (flush_ret != 0)
        return flush_ret;
    LPMMIOINFO info = (LPMMIOINFO)hmmio;
    uint proc_ret = info->pIOProc(info, MMIOM_CLOSE, fuClose, 0);
    if (proc_ret != 0)
        return proc_ret;

    mmioSetBuffer(hmmio, 0, 0, 0);
    FreeHandle(hmmio);

    return 0;
}

MMRESULT mmioAdvance(HMMIO hmmio, LPMMIOINFO pmmioinfo, UINT fuAdvance) { return 0; }

LONG mmioRead(HMMIO hmmio, HPSTR pch, LONG cch) { return 0; }

LONG mmioWrite(HMMIO hmmio, const char *pch, LONG cch) { return 0; }
