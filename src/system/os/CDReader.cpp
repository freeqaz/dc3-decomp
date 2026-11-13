#include "os\CDReader.h"
#include "os\Archive.h"
#include "os\File.h"
#include "os\PlatformMgr.h"
#include "os\System.h"
#include "xdk\xapilibi\errhandlingapi.h"
#include "xdk\xapilibi\fileapi.h"
#include <vector>
#include "xdk\XAPILIB.h"

namespace {
    OVERLAPPED gOverlapped;
    int gPendingFile;
    int gErrorCode;
    std::vector<void *> gArkFiles;
    std::vector<void *> gExternalArkFiles;

    void DiskErrorLoop() {
        MILO_NOTIFY("Disc error: %d", gErrorCode);
        ThePlatformMgr.SetDiskError(kDiskError);
    }

    int ArkFilesInit() {
        if (gArkFiles.empty()) {
            gArkFiles.resize(TheArchive->NumArkFiles());
            gExternalArkFiles.resize(TheArchive->NumArkFiles());
            memset(&gOverlapped, 0, sizeof(gOverlapped));
            for (int i = 0; i < gArkFiles.size(); i++) {
                const char *arkFileName = TheArchive->GetArkfileName(i);
                String fullPath;
                FileQualifiedFilename(fullPath, arkFileName);
                gArkFiles[i] = CreateFileA(
                    fullPath.c_str(),
                    GENERIC_READ,
                    FILE_SHARE_READ,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING,
                    nullptr
                );
                if (!gArkFiles[i]) {
                    gErrorCode = GetLastError();
                    DiskErrorLoop();
                    return 1;
                }
                gExternalArkFiles[i] = CreateFileA(
                    fullPath.c_str(),
                    GENERIC_READ,
                    FILE_SHARE_READ,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_FLAG_SEQUENTIAL_SCAN | FILE_ATTRIBUTE_NORMAL,
                    nullptr
                );
                if (!gExternalArkFiles[i]) {
                    gErrorCode = GetLastError();
                    DiskErrorLoop();
                    return 1;
                }
            }
        }
        return 0;
    }
}

bool CDReadDone() {
    if (gFakeFileErrors || !UsingCD()) {
        gErrorCode = 0x45D;
    } else {
        DWORD bytes;
        if (GetOverlappedResult(gArkFiles[gPendingFile], &gOverlapped, &bytes, false)) {
            return true;
        }
        gErrorCode = GetLastError();
        if (gErrorCode == ERROR_IO_PENDING) {
            return false;
        }
        if (gErrorCode == ERROR_IO_INCOMPLETE) {
            return false;
        }
    }
    DiskErrorLoop();
    return true;
}

int CDRead(int arkFile, int offset, int size, void *buffer) {
    if (gFakeFileErrors || !UsingCD()) {
        gErrorCode = 0x45D;
        DiskErrorLoop();
        return 1;
    } else {
        // The image returns ArkFilesInit()'s own result (0x82602B8C `bne` lands on
        // the epilogue with r3 untouched); it only ever returns 0 or 1. (w19-x)
        int initErr = ArkFilesInit();
        if (initErr) {
            return initErr;
        }
        u64 pos = (u64)offset << 0xB;
        gOverlapped.OffsetHigh = pos >> 0x20;
        gOverlapped.Offset = pos;
        if (!ReadFile(gArkFiles[arkFile], buffer, size << 0xB, nullptr, &gOverlapped)) {
            DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING) {
                if (err == ERROR_IO_INCOMPLETE) {
                    MILO_NOTIFY("Disc error: ERROR_IO_INCOMPLETE, ignoring");
                } else {
                    gErrorCode = err;
                    DiskErrorLoop();
                    return 1;
                }
            }
        }
        gPendingFile = arkFile;
        return 0;
    }
}

bool CDReadExternal(void *&v, int i, u64 u) {
    if (ArkFilesInit() != 0) {
        return false;
    } else {
        v = gExternalArkFiles[i];
        // Lane history (17 lines moved): docs/decomp/notes/system/os/CDReader.md
        // > The local the high-dword pointer points at is 64 bits wide, not a truncated
        // > LONG: 0x82602C9C is `std r29, 0x50(r1)`, a full 8-byte store of the offset,
        // > and r5 (= r1+0x50) is what SetFilePointer gets as lpDistanceToMoveHigh.
        u64 offset = u;
#ifdef HX_NATIVE
        LONG low = (LONG)u;
#else
        LONG low = ((const LONG *)&u)[1];
#endif
        SetFilePointer(v, low, (LONG *)&offset, 0);
        return true;
    }
}

int CDGetError() { return 0; }
