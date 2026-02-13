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
        int b3 = (err == ERROR_FILE_CORRUPT) || (err == ERROR_DISK_CORRUPT);
        TheContentMgr.OnReadFailure(b3, str.c_str());
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
        mFail = true;
        return;
    }
    unk34 = 0x800;
        modeCheck = (mode & 0x7fffe) & (mode = mMode & 0x40002);
    if (modeCheck == 0) {
        fd = _open(mFilename.c_str(), (mode & 0xfffffffd) | 0x8000, 0x180);
        mFd = fd;
        openError = ((unsigned int)fd) >> 31;
        mFail = openError;
        if (openError != 0)
            return;
        mSize = _lseeki64(fd, 0, 2);
        if (!(mode & 8)) {
            _lseek((int)mFd, 0, 0);
        }
        return;
    }
    if (mode & 2) {
        dwDesiredAccess = 0x80000000;
        dwCreationDisposition = 3;
    } else if (mode & 0x200) {
        dwDesiredAccess = 0x40000000;
        dwCreationDisposition = 2;
    } else {
        dwCreationDisposition = 3 + (((mode & 0x100) == 0) ? 1 : 0);
        dwDesiredAccess = 0x40000000;
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
