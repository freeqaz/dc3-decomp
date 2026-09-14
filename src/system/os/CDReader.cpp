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
            gOverlapped.Internal = 0;
            gOverlapped.InternalHigh = 0;
            gOverlapped.Offset = 0;
            gOverlapped.OffsetHigh = 0;
            gOverlapped.hEvent = 0;
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
        if (ArkFilesInit()) {
            return 1;
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
        // The local the high-dword pointer points at is 64 bits wide, not a
        // truncated LONG: 0x82602C9C is `std r29, 0x50(r1)`, a full 8-byte store
        // of the offset, and r5 (= r1+0x50) is what SetFilePointer gets as
        // lpDistanceToMoveHigh.  On big-endian that pointer lands on the HIGH
        // dword of the offset, which is what the API wants.  Written as
        // `LONG l = u;` we stored the truncated LOW 32 bits there instead and
        // handed the API the low half twice.  The `u64` parameter is also homed
        // (`std r5, 0xa0(r1)`) and the LONG argument reloaded out of it
        // (`lwz r4, 0xa4(r1)`) -- RESIDUAL (w7-az, 88.46, 3 rows): we keep the
        // offset in a callee-saved register and truncate with `clrrwi` instead,
        // so the two home instructions are missing.  An explicit `(LONG)` cast on
        // the second argument is byte-inert.
        u64 offset = u;
        SetFilePointer(v, (LONG)u, (LONG *)&offset, 0);
        return true;
    }
}

int CDGetError() { return 0; }
