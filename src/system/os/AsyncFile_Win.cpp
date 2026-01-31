#include "os/AsyncFile_Win.h"
#include "os/ContentMgr.h"
#include "os/File.h"
#include "os/PlatformMgr.h"
#include "os/System.h"
#include "xdk/XAPILIB.h"
#include <io.h>

void ReadError(const char *cc) {
    DWORD err = GetLastError();
    String str;
    if (FileIsLocal(cc) && TheContentMgr.Contains(cc, str)) {
        MILO_LOG("ReadError in package '%s', err = 0x%08x\n", str, err);
        int isCorruptError = (err == ERROR_FILE_CORRUPT) || (err == ERROR_DISK_CORRUPT);
        TheContentMgr.OnReadFailure(isCorruptError, str.c_str());
    } else {
        if (!UsingCD())
            return;
        ThePlatformMgr.SetDiskError(kDiskError);
    }
}

AsyncFileWin::AsyncFileWin(const char *filename, int mode)
    : AsyncFile(filename, mode), mFile(INVALID_HANDLE_VALUE), unk3c(-1),
      mReadInProgress(0), mWriteInProgress(0) {}

AsyncFileWin::~AsyncFileWin() { Terminate(); }

bool AsyncFileWin::Truncate(int distanceToMove) {
    SetFilePointer(mFile, distanceToMove, nullptr, 0);
    return SetEndOfFile(mFile);
}

void AsyncFileWin::_OpenAsync() {
    unsigned int mode;
    int modeCheck;
    unsigned int openError;
    int fd;
    DWORD dwDesiredAccess;
    DWORD dwCreationDisposition;
    DWORD err;

    mSize = 0;
    if (gFakeFileErrors) {
        SetLastError(0x20000002);
    } else {
        unk34 = 0x800;
        mode = mMode;
        modeCheck = (mode & 0x7fffe) & (mode & 0x40002);
        if (modeCheck == 0) {
            fd =
                _open(mFilename.c_str(), (mode & 0xfffffffd) | 0x8000, 0x180);
            unk3c = fd;
            openError = ((unsigned int)fd) >> 31;
            mFail = openError;
            if (openError != 0)
                return;
            mSize = _lseeki64(fd, 0, 2);
            if (!(mode & 8)) {
                _lseek((int)unk3c, 0, 0);
            }
            return;
        }
        if (mMode & 2) {
            dwDesiredAccess = 0x80000000;
            dwCreationDisposition = 3;
        } else {
            if (mMode & 0x200) {
                dwDesiredAccess = 0x40000000;
                dwCreationDisposition = 2;
            } else {
                dwDesiredAccess = 0x40000000;
                dwCreationDisposition = (((mMode & 0x100) == 0) ? 1 : 0) + 3;
            }
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
        } else {
            mFail = false;
            mSize = GetFileSize(mFile, nullptr);
            return;
        }
    }
    mFail = true;
}

bool AsyncFileWin::_WriteDone() {
    if (!mWriteInProgress) {
        return true;
    } else {
        if (mOverlapped.Internal != 0x103) {
            mWriteInProgress = false;
            DWORD bytesWritten;
            if (GetOverlappedResult(mFile, &mOverlapped, &bytesWritten, false)) {
                return true;
            }
            mFail = true;
        }
        return false;
    }
}

void AsyncFileWin::_SeekToTell() {
    if (!(mMode & FILE_OPEN_READ)) {
        if (unk3c >= 0) {
            if (_lseek(unk3c, mTell, 0) < 0) {
                mFail = true;
            }
        } else {
            // Wait for pending write operation to complete
            while (!_WriteDone())
                ;
        }
    } else {
        // Wait for pending read operation to complete
        while (!_ReadDone())
            ;
    }
}

void AsyncFileWin::_Close() {
    if (mMode & FILE_OPEN_READ) {
        if (mFile == INVALID_HANDLE_VALUE)
            return;
        // Wait for pending read operation to complete
        while (!_ReadDone())
            ;
    } else {
        if (unk3c >= 0) {
            _close(unk3c);
        }
        if (mFile == INVALID_HANDLE_VALUE)
            return;
        // Wait for pending write operation to complete
        while (!_WriteDone())
            ;
    }
    CloseHandle(mFile);
    mFile = INVALID_HANDLE_VALUE;
}
