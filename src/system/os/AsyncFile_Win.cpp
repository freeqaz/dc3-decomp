#include "os\AsyncFile_Win.h"
#include "os\ContentMgr.h"
#include "os\File.h"
#include "os\PlatformMgr.h"
#include "os\System.h"
#include "xdk\XAPILIB.h"
#include <errno.h>
#include <io.h>

void ReadError(const char *cc) {
    DWORD err = GetLastError();
    String str;
    if (FileIsLocal(cc)) {
        if (TheContentMgr.Contains(cc, str)) {
            MILO_LOG("ReadError in package '%s', err = 0x%08x\n", str, err);
            int b3 = (err == ERROR_FILE_CORRUPT) || (err == ERROR_DISK_CORRUPT);
            TheContentMgr.OnReadFailure(b3, str.c_str());
        }
        return;
    } else {
        if (!UsingCD())
            return;
        ThePlatformMgr.SetDiskError(kDiskError);
    }
}

AsyncFileWin::AsyncFileWin(const char *filename, int mode)
    : AsyncFile(filename, mode), mFile(INVALID_HANDLE_VALUE), mFd(-1),
      mReadInProgress(0), mWriteInProgress(0) {}

AsyncFileWin::~AsyncFileWin() { Terminate(); }

bool AsyncFileWin::Truncate(int distanceToMove) {
    SetFilePointer(mFile, distanceToMove, nullptr, 0);
    return SetEndOfFile(mFile);
}

void AsyncFileWin::_OpenAsync() {
    int fd;
    DWORD dwDesiredAccess;
    DWORD dwCreationDisposition;
    DWORD err;

    mSize = 0;
    if (gFakeFileErrors) {
        SetLastError(0x20000002);
        ReadError(mFilename.c_str());
        mFail = true;
        return;
    }
    mSectorBytes = 0x800;
    if (!(mMode & 0x40002)) {
        fd = _open(mFilename.c_str(), (mMode & ~2) | 0x8000, 0x180);
        mFd = fd;
        mFail = ((unsigned int)fd) >> 31;
        if (mFail)
            return;
        mSize = _lseeki64(fd, 0, 2);
        if (!(mMode & 8)) {
            _lseek((int)mFd, 0, 0);
        }
        return;
    }
    if (mMode & 2) {
        dwDesiredAccess = 0x80000000;
        dwCreationDisposition = 3;
    } else if (mMode & 0x200) {
        dwDesiredAccess = 0x40000000;
        dwCreationDisposition = 2;
    } else {
        dwDesiredAccess = 0x40000000;
        dwCreationDisposition = (mMode & 0x100) ? 4 : 3;
    }
    mFile = CreateFileA(
        mFilename.c_str(),
        dwDesiredAccess,
        3,
        nullptr,
        dwCreationDisposition,
        0x60000000,
        nullptr
    );
    if (mFile == (HANDLE)-1) {
        err = GetLastError();
        if ((err != 2) && (err != 3) && (err != 0x15)) {
            ReadError(mFilename.c_str());
        }
        mFail = true;
        return;
    }
    mFail = false;
    mSize = GetFileSize(mFile, nullptr);
}

bool AsyncFileWin::_WriteDone() {
    if (!mWriteInProgress) {
        return true;
    }
    if (mOverlapped.Internal != 0x103) { // STATUS_PENDING
        mWriteInProgress = false;
        DWORD bytesWritten; // required out param, value unused
        if (GetOverlappedResult(mFile, &mOverlapped, &bytesWritten, false)) {
            return true;
        }
        mFail = true;
    }
    return false;
}

void AsyncFileWin::_SeekToTell() {
    if (!(mMode & FILE_OPEN_READ)) { // write mode
        if (mFd >= 0) {
            if (_lseek(mFd, mTell, 0) < 0) {
                mFail = true;
            }
        } else {
            while (!_WriteDone())
                ;
        }
    } else { // read mode
        while (!_ReadDone())
            ;
    }
}

void AsyncFileWin::_Close() {
    if (mMode & FILE_OPEN_READ) { // read mode
        if (mFile == INVALID_HANDLE_VALUE)
            return;
        while (!_ReadDone())
            ;
    } else { // write mode
        if (mFd >= 0) {
            _close(mFd);
        }
        if (mFile == INVALID_HANDLE_VALUE)
            return;
        while (!_WriteDone())
            ;
    }
    CloseHandle(mFile);
    mFile = INVALID_HANDLE_VALUE;
}

// COLLATERAL (w7-bg): 99.29% -> 97.84% canonical when _ReadAsync below was fixed
// (unk58 bool -> unsigned char, its `aligned` local unsigned char -> int).  The
// code SHAPE here improved -- the `mr r28, r11` insert that copied the memset
// zero into a callee-saved register is gone -- but MSVC then allocates one
// fewer callee-saved register for this function (r23..r30 vs the image's
// r22..r30), which renumbers every row, and objdiff's section-relative branch
// rendering charges one `bne` displacement (82605xxx `bne 0x754` vs `bne 0xa00`)
// that had previously coincided.  Every other row here is register permutation,
// which the canonical ruler forgives.  Moving `int aligned` above the memset was
// measured and is INERT.  Do not undo the _ReadAsync fix for this: it is worth
// +1.35pp on a 768 B row against -1.45pp on a 564 B one.
void AsyncFileWin::_WriteAsync(const void *buf, int count) {
    if (mFd >= 0) {
        int written = _write(mFd, buf, count);
        if (written < count) {
            if (written == -1 && errno == ENOSPC) {
                MILO_NOTIFY("AsyncFileWin::_Write: out of disk space");
            }
            mFail = true;
        }
    } else {
        MILO_ASSERT(!mWriteInProgress && !mReadInProgress, 0xe5);
        MILO_ASSERT(count >= 0, 0xe6);
        if (count == 0)
            return;
        mWriteInProgress = true;
        memset(&mOverlapped, 0, sizeof(OVERLAPPED));
        int aligned = 0;
        if (((int)buf & 3) == 0) {
            if (Tell() % mSectorBytes == 0) {
                if (count % mSectorBytes == 0) {
                    aligned = 1;
                }
            }
        }
        MILO_ASSERT(aligned, 0xf5);
        mOverlapped.Offset = Tell();
        if (!WriteFile(mFile, buf, count, 0, &mOverlapped)) {
            if (GetLastError() != 0x3e5) {
                mFail = true;
            }
        }
    }
}

void AsyncFileWin::_ReadAsync(void *buf, int count) {
    MILO_ASSERT(!mReadInProgress && !mWriteInProgress, 0x139);
    MILO_ASSERT(count >= 0, 0x13a);
    if (gFakeFileErrors) {
        SetLastError(0x20000002);
        ReadError(mFilename.c_str());
        mFail = true;
        return;
    }
    if (count == 0)
        return;
    mReadInProgress = true;
    memset(&mOverlapped, 0, sizeof(OVERLAPPED));
    unk5c = buf;
    unk64 = count;
    int aligned = 0;
    if (((int)buf & 3) == 0) {
        if (Tell() % mSectorBytes == 0) {
            if (unk64 % mSectorBytes == 0) {
                aligned = 1;
            }
        }
    }
    // `aligned` is an int and `unk58` an unsigned char: the image narrows once and
    // reuses the narrowed value for both the store and the test --
    // 82605440 `clrlwi. r11, r30, 24` / 82605444 `stb r11, 0x58(r31)`.  A `bool`
    // unk58 forces a subic/subfe normalisation there; an `unsigned char` local
    // makes the store use the raw register and sink past the `mr r3, r31`.
    unk58 = aligned;
    int bytesToRead;
    if (unk58) {
        mOverlapped.Offset = Tell();
        bytesToRead = unk64;
        unk60 = unk5c;
    } else {
        mOverlapped.Offset = (Tell() / mSectorBytes) * mSectorBytes;
        int alignedEnd = ((Tell() + unk64 + mSectorBytes - 1) / mSectorBytes) * mSectorBytes;
        bytesToRead = alignedEnd - mOverlapped.Offset;
        MILO_ASSERT(bytesToRead%mSectorBytes == 0, 0x16a);
        unk60 = _MemAllocTemp(bytesToRead, "AsyncFile_Win.cpp", 0x16d, "AsyncFileTempBuf", 0);
        unk68 = Tell() - mOverlapped.Offset;
    }
    if (!ReadFile(mFile, unk60, bytesToRead, 0, &mOverlapped)) {
        if (GetLastError() != 0x3e5) {
            ReadError(mFilename.c_str());
            mFail = true;
        }
    }
}

bool AsyncFileWin::_ReadDone() {
    if (gFakeFileErrors) {
        SetLastError(0x20000002);
        ReadError(mFilename.c_str());
        mReadInProgress = false;
        mFail = 1;
        return false;
    }
    if (!mReadInProgress) {
        return true;
    }
    if (mOverlapped.Internal == 0x103) {
        return false;
    }
    DWORD bytesTransferred;
    if (GetOverlappedResult(mFile, &mOverlapped, &bytesTransferred, false)) {
        if (unk58 == 0) {
            memcpy(unk5c, (char *)unk60 + unk68, unk64);
            MemFree(unk60, "unknown", 0, "unknown");
        }
        mReadInProgress = false;
        return true;
    }
    ReadError(mFilename.c_str());
    mReadInProgress = false;
    mFail = 1;
    return false;
}
