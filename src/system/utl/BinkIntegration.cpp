#include "os/Endian.h"
#include "os\CritSec.h"
#include "os\Debug.h"
#include "os\File.h"
#include "os\Timer.h"
#include "obj\DataFile.h"
#include "synth\Synth.h"
#include "utl\EncryptXTEA.h"
#include "utl\MemMgr.h"
#include "KeyChain.h"
#include <cstring>
#include <cstdio>

CriticalSection gCrit;

struct BINKENCRYPTIONHEADER {
    unsigned int mSignature;       // 0x00
    unsigned int mVersion;         // 0x04
    unsigned int mKeyIndex;        // 0x08
    unsigned int mMagicA;          // 0x0c
    unsigned int mMagicB;          // 0x10
    unsigned int mPad;             // 0x14 (align mNonce to 8-byte)
    unsigned long long mNonce[2];  // 0x18
    unsigned char mKeyMask[0x10];  // 0x28
};

// The Bink SDK's BINKIO ends with `volatile U8 iodata[128+32]` at 0x80 -- the
// scratch area an IO implementation lays its own state over (binkxenon/bink.h).
// DC3's file IO overlays THIS struct on it, and that split is visible in the
// image: every function here that touches both halves materialises a SECOND
// base pointer at bink+0x80 and addresses the file state off that.  ReadFunc
// does it at 0x82E5D2AC (`addi r30, r3, 0x80`) and then reads e.g.
// `lwz r11, 0x34(r30)` for mEncHeader.mVersion, never `0xb4(bink)`.
struct BINKIOFILE {
    File *pFile;                   // 0x00 (0x80)
    unsigned int iCloseFile;       // 0x04
    unsigned char *pBuffer;        // 0x08
    unsigned char *pBufEnd;        // 0x0c
    unsigned char *pBufPos;        // 0x10
    unsigned char *pBufBack;       // 0x14
    unsigned int iBufEmpty;        // 0x18
    unsigned int iFileBufPos;      // 0x1c
    unsigned int fileFlags;        // 0x20
    unsigned int lastTimerRead;    // 0x24
    unsigned int iHeaderSize;      // 0x28
    unsigned int unk2c;            // 0x2c
    BINKENCRYPTIONHEADER mEncHeader; // 0x30 (size 0x38)
    XTEABlockEncrypter *pXTEADecrypter; // 0x68
};

// DC3 BINKIO struct — all IO state (file ptr, buffer, encryption) is in this flat struct.
// Offsets match what BinkFileOpen/BinkFileSetInfo/BinkFileClose/BinkFileBGControl use.
struct BINKIO {
    unsigned int (*ReadHeader)(struct BINKIO *, int, void *, unsigned int); // 0x00
    unsigned int (*ReadFrame)(struct BINKIO *, unsigned int, int, void *, unsigned int); // 0x04
    unsigned int (*GetBufferSize)(struct BINKIO *, unsigned int); // 0x08
    void (*SetInfo)(struct BINKIO *, void *, unsigned int, unsigned int, unsigned int); // 0x0c
    unsigned int (*Idle)(struct BINKIO *); // 0x10
    void (*Close)(struct BINKIO *); // 0x14
    int (*BGControl)(struct BINKIO *, unsigned int); // 0x18
    struct BINK *bink;             // 0x1c
    unsigned int unk20;            // 0x20
    unsigned int unk24;            // 0x24
    unsigned int unk28;            // 0x28
    unsigned int unk2c;            // 0x2c
    unsigned int unk30;            // 0x30
    unsigned int unk34;            // 0x34
    unsigned int unk38;            // 0x38
    unsigned int unk3c;            // 0x3c
    // Every counter the SDK shares with its background IO thread is
    // `volatile U32` in binkxenon/bink.h, and the image proves it: ReadFunc
    // RE-LOADS bytesAvail at 0x82E5D4EC after storing it at 0x82E5D4DC rather
    // than keeping the value in a register (0x82E5D4D4/DC/EC/FC are four
    // separate accesses to 0x6c), and loads Suspended at 0x82E5D520 at the
    // point of use instead of hoisting it above the ForegroundTime update.
    volatile unsigned int ReadError;        // 0x40
    volatile unsigned int DoingARead;       // 0x44
    volatile unsigned int BytesRead;        // 0x48
    volatile unsigned int unk4c;            // 0x4c
    volatile unsigned int ForegroundTime;   // 0x50
    volatile unsigned int TotalTime;        // 0x54
    volatile unsigned int unk58;            // 0x58
    volatile unsigned int unk5c;            // 0x5c
    volatile unsigned int BufSize;          // 0x60
    volatile unsigned int BufHighUsed;      // 0x64
    volatile unsigned int CurBufSize;       // 0x68
    volatile unsigned int bytesAvail;       // 0x6c
    volatile unsigned int Suspended;        // 0x70
    unsigned int unk74;            // 0x74
    unsigned int unk78;            // 0x78
    unsigned int unk7c;            // 0x7c
    BINKIOFILE io;                 // 0x80 -- the SDK's `volatile U8 iodata[]`
};

struct BINK {
    unsigned int Width;   // 0x00
    unsigned int Height;  // 0x04
    unsigned int Frames;  // 0x08
    unsigned int FrameNum; // 0x0c
    char padding[0x28];
    int NumTracks;        // 0x38
};

#ifdef HX_NATIVE
// Bink SDK not available on native — stub all proprietary functions
void ReadFunc(BINKIO *, bool) {}
void BinkFree(void *) {}
unsigned int BinkFileReadHeader(BINKIO *, int, void *, unsigned int) { return 0; }
unsigned int BinkFileReadFrame(BINKIO *, unsigned int, int, void *, unsigned int) { return 0; }
void BinkSetMemory(void *(*)(unsigned int), void (*)(void *)) {}
void BinkSetIO(int (*)(BINKIO *, const char *, unsigned int)) {}
#else
extern void BinkFree(void *);
// The Bink SDK is C: binkxenon/bink.h declares these RADEXPFUNC/RADEXPLINK and
// the target's linker map carries them UNMANGLED (`BinkSetIO` @82ee7c38,
// `BinkSetMemory` @82ee9e20, both from binkxenon:binkread.obj). Declaring them
// as C++ here made BinkInit reference `?BinkSetIO@@YAXP6AHPAUBINKIO@@PBDI@Z@Z`
// instead -- the same shape as the createFilter bug, and equally invisible:
// relocation names are folded into arg_diff_score, so BinkInit read 100% with
// zero mismatches while calling symbols that do not exist in the image.
// (src/link_glue.cpp carries /ALTERNATENAME entries that were papering over
// exactly this; they become inert rather than load-bearing.)
extern "C" void BinkSetMemory(void *(*)(unsigned int), void (*)(void *));
extern "C" void BinkSetIO(int (*)(BINKIO *, const char *, unsigned int));
extern "C" unsigned int RADTimerRead();
#endif

// Forward declarations for all IO callbacks (referenced by BinkFileBGControl and BinkFileIdle below)
unsigned int BinkFileReadHeader(BINKIO *bink, int, void *header, unsigned int length);
void ReadFunc(BINKIO *bink, bool startRead);
unsigned int BinkFileReadFrame(BINKIO *bink, unsigned int frameOffset, int hasHeader, void *dest, unsigned int length);
unsigned int BinkFileIdle(BINKIO *bink);

// Explicit instantiation for unsigned int
template void EndianSwapBlock<unsigned int>(unsigned int *, int);


int BinkFileBGControl(BINKIO *file, unsigned int flags) {
    char *pByte = (char *)file;
    volatile unsigned int *pControl = (unsigned int *)(pByte + 0x70);
    volatile unsigned int *pStatus = (unsigned int *)(pByte + 0x44);

    if (flags & 1) {
        if (*pControl == 0) {
            *pControl = 1;
        }
        if (flags & 0x80000000) {
            while (*pStatus != 0) {
                // spin
            }
        }
    } else if (flags & 2) {
        if (*pControl == 1) {
            *pControl = 0;
        }
        if (flags & 0x80000000) {
            BinkFileIdle(file);
        }
    }
    return *pControl;
}

void *BinkAlloc(unsigned int size) {
    return MemAlloc(size, "BinkIntegration.cpp", 0x44, "Bink Internal", 0);
}

unsigned int BinkFileGetBufferSize(BINKIO *, unsigned int size) {
    unsigned int aligned = (size + 0x7FFF) & 0xFFFF8000;
    if (aligned < 0x10000) {
        aligned = 0x10000;
    }
    return aligned;
}

void BinkFileSetInfo(BINKIO *file, void *buf, unsigned int size, unsigned int, unsigned int fileFlags) {
    size &= 0xFFFF8000;
    // Address the file half through its own pointer, as BinkFileReadHeader
    // does. With the named `io`, the pre-RA schedule puts the end-pointer add
    // ahead of the bytesAvail `li 0`, so regasg's first fit hands the add r10
    // and the zero r9, as in the image. Spelled inline off `file`, the li is
    // scheduled first and the two registers swap.
    // `size &=` (a parameter, sid 3) rather than a new local keeps `buf`
    // (sid 4) first in the add: higher sid first (c2rs 8.7).
    BINKIOFILE *io = &file->io;
    io->pBuffer = (unsigned char *)buf;
    io->pBufEnd = (unsigned char *)buf + size;
    io->pBufPos = (unsigned char *)buf;
    io->pBufBack = (unsigned char *)buf;
    io->iBufEmpty = size;
#ifdef HX_NATIVE
    file->BufSize = size;
    file->bytesAvail = 0;
#else
    // Plain stores: through the volatile members these two would be pinned in
    // program order, and the image schedules them as ordinary stores.
    char *p = (char *)file;
    *(unsigned int *)(p + 0x60) = size;
    *(unsigned int *)(p + 0x6c) = 0;
#endif
    io->fileFlags = fileFlags;
}

void BinkFileClose(BINKIO *bink) {
    char *p = (char *)bink;
    if (*(unsigned int *)(p + 0x84) != 0) {
        File *file = *(File **)(p + 0x80);
        if (file != nullptr) {
            delete file;
        }
        *(File **)(p + 0x80) = nullptr;
    }
    if (*(unsigned int *)(p + 0xb4) == 2) {
        operator delete(*(void **)(p + 0xe8));
    }
}

unsigned int BinkFileIdle(BINKIO *bink) {
    char *p = (char *)bink;
    if (*(unsigned int *)(p + 0x40) != 0)
        return 0;
    if (*(unsigned int *)(p + 0x70) != 0)
        return 0;
    if (*(unsigned int *)(p + 0x44) != 0) {
        // A scoped CritSecTracker, not a bare Enter/Exit pair: the image spills
        // &gCrit into a stack slot (`stw r29, 0x50(r31)`, 0x82E5DA50) and carries a
        // frame pointer, both of which come from the guard object's destructor.
        // Its two null tests fold away because &gCrit is provably non-null.
        CritSecTracker lock(&gCrit);
        ReadFunc(bink, false);
    }
    return *(unsigned int *)(p + 0x44);
}

int BinkFileOpen(BINKIO *bink, const char *name, unsigned int flags) {
    char *p = (char *)bink;
    memset(bink, 0, 0x120);
    if (flags & 0x800000) {
        *(const char **)(p + 0x80) = name;
    } else {
        File *file = NewFile(name, 2);
        *(File **)(p + 0x80) = file;
        *(int *)(p + 0x84) = 1;
        if (file == nullptr)
            return 0;
    }
    *(void **)(p + 0x00) = (void *)BinkFileReadHeader;
    *(void **)(p + 0x04) = (void *)BinkFileReadFrame;
    *(void **)(p + 0x08) = (void *)BinkFileGetBufferSize;
    *(void **)(p + 0x0c) = (void *)BinkFileSetInfo;
    *(void **)(p + 0x10) = (void *)BinkFileIdle;
    *(void **)(p + 0x14) = (void *)BinkFileClose;
    *(void **)(p + 0x18) = (void *)BinkFileBGControl;
    return 1;
}

void BinkInit() {
    BinkSetMemory(BinkAlloc, operator delete);
    BinkSetIO(BinkFileOpen);
}

#ifndef HX_NATIVE
unsigned int BinkFileReadHeader(BINKIO *bink, int, void *header, unsigned int length) {
    BINKIOFILE *bf = &bink->io;
    File *file = bf->pFile;
    BINKENCRYPTIONHEADER *encHeader = &bf->mEncHeader;
    // If we haven't read the encryption header yet (mSignature == 0), read it now
    if (encHeader->mSignature == 0) {
        int encRead = file->Read(encHeader, sizeof(BINKENCRYPTIONHEADER));
        // Byteswap mSignature through mMagicB (5 uints = 0x14 bytes)
        EndianSwapBlock<unsigned int>(&encHeader->mSignature, 5);
        // Byteswap the nonce fields
        unsigned long long n0 = encHeader->mNonce[0];
        unsigned long long n1 = encHeader->mNonce[1];
        encHeader->mNonce[0] = EndianSwap(n0);
        encHeader->mNonce[1] = EndianSwap(n1);
        // Check if this is an encrypted BIK ("BIKE" = 0x4542494b)
        if (encHeader->mSignature == 0x4542494b) {
            XTEABlockEncrypter *decrypter = new XTEABlockEncrypter;
            bf->pXTEADecrypter = decrypter;

            // Key derivation — same DTA obfuscation pattern as VorbisReader::setupCypher
            DataArray *arr = DataReadString("{Na 42 'O32'}");
            unsigned int iEval = arr->Evaluate(0).Int();
            arr->Release();

            char i6 = (iEval % 13) + 'A';
            char script[256];
            unsigned char masterKey[256];
            sprintf(script, "{%c %d %c}", i6, (int)masterKey ^ iEval, i6);
            DataArray *buf118Arr = DataReadString(script);
            buf118Arr->Evaluate(0);
            buf118Arr->Release();

            unsigned char key[0x10];
            KeyChain::getKey(encHeader->mKeyIndex, key, masterKey);
            TheSynth->Grinder().GrindArray(
                encHeader->mMagicA, encHeader->mMagicB, key, 0x10, 0xc
            );
            for (int i = 0; i < 16; i++) {
                key[i] ^= encHeader->mKeyMask[i];
            }

            EndianSwapBlock<unsigned int>((unsigned int *)key, 4);
            bf->pXTEADecrypter->SetKey(key);
            bf->pXTEADecrypter->SetNonce(bf->mEncHeader.mNonce, 0);
            bf->iFileBufPos += encRead;
        } else {
            // Not an encrypted BIK — seek back and pretend we never read the header
            memset(encHeader, 0, encRead);
            file->Seek(-encRead, FILE_SEEK_CUR);
            BINK *curBink = bink->bink;
            if (curBink != NULL && curBink->NumTracks > 2 && curBink->Width < 8) {
                MILO_LOG("Attempting read of unsecure Bink song file!\n");
            }
        }
    }
    // Read the actual Bink file header
    unsigned int bytesRead = (unsigned int)file->Read(header, length);
    if (bytesRead != length) {
        bink->ReadError = 1;
    }
    bf->iHeaderSize += bytesRead;
    bf->iFileBufPos += bytesRead;
    int remaining = file->Size() - (int)bf->iFileBufPos;
    if ((unsigned int)remaining >= bink->BufSize) {
        remaining = (int)bink->BufSize;
    }
    bink->CurBufSize = (unsigned int)remaining;
    EndianSwapBlock<unsigned int>((unsigned int *)header, bytesRead >> 2);
    return bytesRead;
}

void ReadFunc(BINKIO *bink, bool startRead) {
    // The SDK half of BINKIO stays addressed off `bink` (r28: 0x44 DoingARead,
    // 0x6c bytesAvail, 0x48 BytesRead, 0x64 BufHighUsed, 0x50 ForegroundTime,
    // 0x70 Suspended); the file half gets its OWN base pointer, materialised
    // once at 0x82E5D2AC `addi r30, r3, 0x80` and held in a callee-saved
    // register for the whole function.  Every access below it is then
    // `X(r30)` with X the offset INSIDE BINKIOFILE -- e.g. 0x82E5D2E4
    // `lwz r11, 0x34(r30)` for mEncHeader.mVersion, never `0xb4(bink)`.
    BINKIOFILE *bf = &bink->io;
    // If an async read was in progress, check if it's done
    if (bink->DoingARead != 0) {
        int bytesRead = 0;
        if (!bf->pFile->ReadDone(bytesRead))
            return;
        bink->DoingARead = 0;
        if (bf->mEncHeader.mVersion == 2) {
            static Timer *_t = AutoTimer::GetTimer(Symbol("XTEA"));
            XTEABlock temp;
            AutoTimer _at(_t, 50.0f, nullptr, nullptr);
            // Lane history (90 lines moved): docs/decomp/notes/system/utl/BinkIntegration.md
            XTEABlock *block = (XTEABlock *)bf->pBufBack;
            while (block < (XTEABlock *)((unsigned char *)bf->pBufBack + bytesRead)) {
                block->mData[1] = EndianSwap(block->mData[1]);
                block->mData[0] = EndianSwap(block->mData[0]);
                bf->pXTEADecrypter->Encrypt(block, &temp);
                unsigned int *dst = (unsigned int *)block;
                const unsigned int *src = (const unsigned int *)&temp;
                dst[0] = src[1]; dst[1] = src[0];
                dst[2] = src[3]; dst[3] = src[2];
                block++;
            }
        } else {
            EndianSwapBlock<unsigned int>((unsigned int *)bf->pBufBack, (unsigned int)bytesRead >> 2);
        }
        // Advance pBufBack by bytesRead, wrapping at pBufEnd back to pBuffer
        unsigned int uBytesRead = (unsigned int)bytesRead;
        bf->pBufBack += uBytesRead;
        if (bf->pBufBack >= bf->pBufEnd) {
            bf->pBufBack = bf->pBuffer;
        }
        bf->iBufEmpty -= uBytesRead;
        bink->bytesAvail += uBytesRead;
        bink->BytesRead += uBytesRead;
        // Lane history (8 lines moved): docs/decomp/notes/system/utl/BinkIntegration.md
        // > The image loads bytesAvail (0x82E5D4EC `lwz r11, 0x6c(r28)`) before
        // > BufHighUsed (0x82E5D4F0), compares `cmplw r11, r10` and then RELOADS
        // > bytesAvail for the store (0x82E5D4FC).
        unsigned int avail = bink->bytesAvail;
        if (avail > bink->BufHighUsed) {
            bink->BufHighUsed = bink->bytesAvail;
        }
        int now = RADTimerRead();
        int elapsed = now - (int)bf->lastTimerRead;
        bf->lastTimerRead = elapsed;
        bink->ForegroundTime += (unsigned int)elapsed;
        if (bink->Suspended != 0) {
            return;
        }
    }
    if (startRead) {
        // ONE statement: the image loads pFile once into a callee-saved
        // register (0x82E5D534 `lwz r29, 0x0(r30)`) and reuses it for both
        // virtual calls, reloading only the vtable at 0x82E5D54C.  Written as
        // two statements with two locals MSVC will not CSE across the first
        // call and re-reads pFile from bf.  Eof() and ReadAsync are separate
        // statements and DO reload it (0x82E5D574, 0x82E5D5A8).
        unsigned int remaining = (unsigned int)(bf->pFile->Size() - bf->pFile->Tell());
        if (bf->iBufEmpty < 0x8000 || bf->pFile->Eof()) {
            bink->CurBufSize = bink->bytesAvail;
        } else {
            bink->DoingARead = 1;
            if (remaining > 0x8000)
                remaining = 0x8000;
            bf->pFile->ReadAsync(bf->pBufBack, (int)remaining);
        }
    }
}

unsigned int BinkFileReadFrame(BINKIO *bink, unsigned int frameNum, int origOffset, void *dest, unsigned int length) {
    // The offset read from is the SDK's THIRD parameter, `origofs` -- r5 in the
    // image (0x82E5D644 `mr r29, r5`, then `add r29, r11, r29` for the
    // encryption-header skew, `cmpwi cr6, r29, -0x1`, `cmplw r29, iFileBufPos`).
    // The second parameter, `Framenum`, is never read.  Signature in
    // binkxenon/bink.h: BINKIOREADFRAME(BINKIO*, U32 Framenum, S32 origofs,
    // void *dest, U32 size).
    //
    // A scoped guard, not a bare Enter/Exit pair: the image sets up an EH frame
    // pointer (0x82E5D630 `subi r31, r1, 0xb0`) and stores &gCrit into the
    // guard object at 0x82E5D64C `stw r22, 0x50(r31)` BEFORE calling Enter, so
    // the Exit runs from a destructor on every path.  CritSecTracker's null
    // check folds away because &gCrit is provably non-null.
    CritSecTracker _cs(&gCrit);
    // 0x82E5D660 `addi r26, r30, 0x80`, emitted BETWEEN the ReadError load and
    // its branch -- so it is declared before the early return, not after it.
    // The image reaches pFile, iHeaderSize, mEncHeader and pXTEADecrypter
    // through THIS base (0x0/0x28/0x30/0x34/0x48/0x68 off r26) but the five
    // buffer fields off `bink` itself (0x88 pBuffer, 0x8c pBufEnd,
    // 0x90 pBufPos, 0x98 iBufEmpty, 0x9c iFileBufPos), so the two halves are
    // spelled differently below.
    BINKIOFILE *bf = &bink->io;
    if (bink->ReadError != 0) {
        return 0;
    }
    // If encrypted, skip the 0x38-byte encryption header when computing offset.
    // BRANCHLESS in the image: 0x82E5D68C `subfic r10, r10, 0x0` /
    // `subfe r10, r10, r10` builds the 0/-1 mask from mSignature,
    // `and r11, r10, r11` selects 0x38, and `add r29, r11, r29` adds it with
    // the 0x38 term on the LEFT.  An `if (...) adjOffset += 0x38;` emits a
    // cmplwi/beq pair instead.
    unsigned int adjOffset
        = (bf->mEncHeader.mSignature != 0 ? 0x38u : 0u) + (unsigned int)origOffset;
    // Check if the file has enough data
    unsigned int fileSize = (unsigned int)bf->pFile->Size();
    if (adjOffset + length > fileSize) {
        bink->ReadError = 1;
        return 0;
    }
    // Declared HERE, after both early returns: the image has no zero register
    // live across Enter, it builds one lazily (0x82E5D6C0 `li r30, 0x0` for the
    // size-check return).  Initialised at the top of the function all the
    // zeros CSE into one hoisted `li` before Enter.
    //
    // Lane history (21 lines moved): docs/decomp/notes/system/utl/BinkIntegration.md
    unsigned int bytesReturned;
    {
        int startTimer = RADTimerRead();
        bytesReturned = 0;
        unsigned int seekPos = adjOffset;
        // If frame is not at the current file position, seek/skip
        if ((int)seekPos != -1 && seekPos != bink->io.iFileBufPos) {
            unsigned int blockOff = 0;
            // ONE `&&`, not a nested `if` with an empty else.  The image tests
            // seekPos against iFileBufPos at 0x82E5D6EC and takes BOTH the
            // `beq` (skip everything, 0x82E5D6F0) and the `ble` (0x82E5D6F8)
            // out of that one compare: `ble` -- seekPos BELOW iFileBufPos --
            // lands on .L_82E5D76C, the flush-and-seek block, exactly where
            // the `bgt cr6` at 0x82E5D714 (seekPos above Tell()) lands.  We
            // used to spell this as `if (seekPos > iFileBufPos) { if (<=Tell)
            // ... else <full seek> }` with no else on the outer test, so a
            // BACKWARDS seek fell straight through to the iFileBufPos update
            // below without ever draining DoingARead, resetting pBufPos /
            // pBufBack / iBufEmpty / bytesAvail, or calling File::Seek --
            // it advertised the new position over a stale buffer.
            //
            // The image also never materialises a `fileTell` local: the
            // `cmpw cr6, r29, r3` at 0x82E5D710 consumes Tell()'s result in
            // r3 directly.
            if (seekPos > bink->io.iFileBufPos
                && (int)seekPos <= bf->pFile->Tell()) {
                // Advance buffer read position to skip data
                unsigned int advance = seekPos - bink->io.iFileBufPos;
                bink->io.pBufPos += advance;
                if (bink->io.pBufPos > bink->io.pBufEnd) {
                    bink->io.pBufPos -= bink->BufSize;
                }
                bink->io.iBufEmpty += advance;
                bink->bytesAvail -= advance;
            } else {
                // Need full seek — flush buffer state
                while (bink->DoingARead != 0) {
                    ReadFunc(bink, false);
                }
                // Lane history (13 lines moved): docs/decomp/notes/system/utl/BinkIntegration.md
                unsigned char *pBuf = bink->io.pBuffer;
                bink->io.pBufPos = pBuf;
                bink->io.pBufBack = pBuf;
                bink->io.iBufEmpty = bink->BufSize;
                bink->bytesAvail = 0;
                if (bf->mEncHeader.mVersion == 2) {
                    // Lane history (16 lines moved): docs/decomp/notes/system/utl/BinkIntegration.md
                    unsigned int hdrSize = bf->iHeaderSize;
                    unsigned int rawOff = seekPos - hdrSize - 0x38;
                    bink->io.pBufPos = pBuf + (rawOff & 0xf);
                    blockOff = rawOff & 0xf;
                    seekPos = (rawOff & 0xfffffff0) + hdrSize + 0x38;
                    bf->pXTEADecrypter->SetNonce(bf->mEncHeader.mNonce, rawOff >> 4);
                }
                bf->pFile->Seek((int)seekPos, FILE_SEEK_SET);
                bink->DoingARead = 0;
            }
            bink->io.iFileBufPos = blockOff + seekPos;
        }
        if (bink->io.pBuffer == nullptr) {
            // No buffer — direct synchronous read
            int readStart = RADTimerRead();
            unsigned int nr = (unsigned int)bf->pFile->Read(dest, (int)length);
            if (nr < length) {
                bink->ReadError = 1;
            }
            // Lane history (5 lines moved): docs/decomp/notes/system/utl/BinkIntegration.md
            bytesReturned = nr;
            bink->BytesRead += nr;
            bink->io.iFileBufPos += nr;
            int readEnd = RADTimerRead();
            *(volatile unsigned int *)&bink->ForegroundTime += (unsigned int)(readEnd - readStart);
            *(volatile unsigned int *)&bink->ForegroundTime += (unsigned int)(readEnd - startTimer);
            EndianSwapBlock<unsigned int>((unsigned int *)dest, nr >> 2);
        } else {
            // Buffered async read loop
            unsigned char *destPtr = (unsigned char *)dest;
            unsigned int remaining = length;
            while (remaining > 0 && bink->ReadError == 0) {
                ReadFunc(bink, true);
                unsigned int avail = bink->bytesAvail;
                if (avail > remaining)
                    avail = remaining;
                if (avail > 0) {
                    bink->io.iFileBufPos += avail;
                    remaining -= avail;
                    bytesReturned += avail;
                    // Handle circular buffer wrap
                    unsigned int toEnd = (unsigned int)(bink->io.pBufEnd - bink->io.pBufPos);
                    if (toEnd <= avail) {
                        memcpy(destPtr, bink->io.pBufPos, toEnd);
                        destPtr += toEnd;
                        avail -= toEnd;
                        bink->io.iBufEmpty += toEnd;
                        bink->io.pBufPos = bink->io.pBuffer;
                        bink->bytesAvail -= toEnd;
                    }
                    if (avail > 0) {
                        memcpy(destPtr, bink->io.pBufPos, avail);
                        destPtr += avail;
                        // Lane history (7 lines moved): docs/decomp/notes/system/utl/BinkIntegration.md
                        bink->io.pBufPos += avail;
                        bink->io.iBufEmpty += avail;
                        bink->bytesAvail -= avail;
                    }
                }
            }
            int threadEnd = RADTimerRead();
            bink->TotalTime += (unsigned int)(threadEnd - startTimer);
        }
        // Update CurBufSize for Bink SDK flow control
        unsigned int newAvail = (unsigned int)(bf->pFile->Size() - (int)bink->io.iFileBufPos);
        if (newAvail >= bink->BufSize) {
            newAvail = bink->BufSize;
        }
        bink->CurBufSize = newAvail;
        if (bink->bytesAvail + 0x8000 > bink->CurBufSize) {
            bink->CurBufSize = bink->bytesAvail;
        }
    }
    return bytesReturned;
}
#endif
