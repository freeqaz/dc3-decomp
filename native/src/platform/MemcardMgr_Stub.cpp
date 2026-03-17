// DC3 Native Port - MemcardMgr Native Implementation
// Replaces MemcardMgr_Xbox.cpp — filesystem-based save/load

#include "meta/MemcardMgr.h"
#include "os/Debug.h"
#include "os/PlatformMgr.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

static const char *kSaveFilename = "save.dat";

// Get the save directory path (~/.local/share/dc3/saves/)
static const char *GetSaveDir() {
    static char sDir[512] = {};
    if (sDir[0]) return sDir;

    const char *envDir = getenv("DC3_SAVE_DIR");
    if (envDir && envDir[0]) {
        snprintf(sDir, sizeof(sDir), "%s", envDir);
    } else {
        const char *home = getenv("HOME");
        if (!home) home = "/tmp";
        snprintf(sDir, sizeof(sDir), "%s/.local/share/dc3/saves", home);
    }
    return sDir;
}

// Ensure save directory exists (create if needed)
static bool EnsureSaveDir() {
    const char *dir = GetSaveDir();
    // Create parent directories recursively
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);

    struct stat st;
    return (stat(dir, &st) == 0 && S_ISDIR(st.st_mode));
}

// Get full path for a save file
static void GetSavePath(char *buf, size_t bufSize, int padNum) {
    snprintf(buf, bufSize, "%s/profile_%d_%s", GetSaveDir(), padNum, kSaveFilename);
}

MemcardMgr::MemcardMgr()
    : mState(kS_None), mAction(0), mSaveCreateType(0),
      mPendingDeviceSelectorIndex(-1), mSelectDeviceWaiting(0),
      mSelectDeviceCallBackObj(0), mPadNum(-1), mProfile(0) {}

MemcardMgr::~MemcardMgr() {}

DataNode MemcardMgr::Handle(DataArray *da, bool ret) {
    return Hmx::Object::Handle(da, ret);
}

void MemcardMgr::Init() {
    if (EnsureSaveDir()) {
        printf("DC3 Native: save directory: %s\n", GetSaveDir());
    } else {
        printf("DC3 Native: WARNING — could not create save directory: %s\n", GetSaveDir());
    }
    ThePlatformMgr.AddSink(this);
}

bool MemcardMgr::IsStorageDeviceValid(Profile *) {
    // Always valid on native — we use the local filesystem
    return true;
}

void MemcardMgr::OnSearchForDevice(Profile *pProfile) {
    // No device enumeration needed — filesystem always available
    mProfile = pProfile;
    mPadNum = mProfile->GetPadNum();
    mState = kS_Search;
    // Immediately report success
    mState = kS_None;
    MCResultMsg msg(kMCNoError);
    Export(msg, true);
}

void MemcardMgr::OnCheckForSaveContainer(Profile *pProfile) {
    mProfile = pProfile;
    mPadNum = mProfile->GetPadNum();
    mState = kS_CheckForSaveContainer;

    // Check if save file exists
    char path[512];
    GetSavePath(path, sizeof(path), mPadNum);

    struct stat st;
    MCResult res;
    if (stat(path, &st) == 0 && st.st_size > 0) {
        // Save file exists — try to load it
        mState = kS_LoadGame;
        // We need to read into mSaveDataBuffer
        if (mAction) {
            mAction->PreAction();
        }
        res = (MCResult)ThreadStart();
        ThreadDone(res);
    } else {
        // No save file — report file not found (SaveLoadManager handles this)
        mState = kS_None;
        MCResultMsg msg(kMCFileNotFound);
        Export(msg, true);
    }
}

void MemcardMgr::OnSaveGame(Profile *pProfile, MemcardAction *pAction, int createType) {
    MILO_ASSERT(pProfile, 0x100);
    mSaveCreateType = createType;
    mProfile = pProfile;
    mPadNum = mProfile->GetPadNum();
    mAction = pAction;
    MILO_ASSERT(mAction, 0x106);

    mAction->PreAction();
    MCResult preResult = mAction->Result();
    if (preResult != kMCNoError) {
        MCResultMsg msg(preResult);
        Export(msg, true);
        return;
    }

    mState = kS_SaveGame;
    int res = ThreadStart();
    ThreadDone(res);
}

void MemcardMgr::OnLoadGame(Profile *pProfile, MemcardAction *pAction) {
    MILO_ASSERT(pProfile, 0x120);
    mProfile = pProfile;
    mPadNum = mProfile->GetPadNum();
    mAction = pAction;
    MILO_ASSERT(mAction, 0x126);

    mAction->PreAction();
    mState = kS_LoadGame;
    int res = ThreadStart();
    ThreadDone(res);
}

void MemcardMgr::OnDeleteSaves(Profile *pProfile) {
    mProfile = pProfile;
    mPadNum = mProfile->GetPadNum();

    char path[512];
    GetSavePath(path, sizeof(path), mPadNum);
    int ret = remove(path);

    mState = kS_None;
    MCResultMsg msg(ret == 0 ? kMCNoError : kMCFileNotFound);
    Export(msg, true);
}

int MemcardMgr::ThreadStart() {
    int ret = 0;
    switch (mState) {
    case kS_None:
        MILO_WARN("MemcardMgr::ThreadStart with no mode set.\n");
        break;
    case kS_Search:
        ret = kMCNoError; // filesystem always available
        break;
    case kS_CheckForSaveContainer: {
        char path[512];
        GetSavePath(path, sizeof(path), mPadNum);
        struct stat st;
        ret = (stat(path, &st) == 0) ? kMCFileExists : kMCFileNotFound;
        break;
    }
    case kS_SaveGame: {
        if (!mSaveDataBuffer || mSaveDataLength <= 0) {
            ret = kMCReadWriteFailed;
            break;
        }
        EnsureSaveDir();
        char path[512];
        GetSavePath(path, sizeof(path), mPadNum);
        FILE *f = fopen(path, "wb");
        if (!f) {
            MILO_WARN("MemcardMgr: failed to open '%s' for writing\n", path);
            ret = kMCReadWriteFailed;
            break;
        }
        size_t written = fwrite(mSaveDataBuffer, 1, mSaveDataLength, f);
        fclose(f);
        if ((int)written != mSaveDataLength) {
            MILO_WARN("MemcardMgr: wrote %zu of %d bytes to '%s'\n", written, mSaveDataLength, path);
            ret = kMCReadWriteFailed;
        } else {
            printf("DC3 Native: saved %d bytes to %s\n", mSaveDataLength, path);
            ret = kMCNoError;
        }
        break;
    }
    case kS_LoadGame: {
        if (!mSaveDataBuffer || mSaveDataLength <= 0) {
            ret = kMCReadWriteFailed;
            break;
        }
        char path[512];
        GetSavePath(path, sizeof(path), mPadNum);
        FILE *f = fopen(path, "rb");
        if (!f) {
            ret = kMCFileNotFound;
            break;
        }
        // Get file size
        fseek(f, 0, SEEK_END);
        long fileSize = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (fileSize <= 0) {
            fclose(f);
            ret = kMCCorrupt;
            break;
        }
        int readSize = (fileSize < mSaveDataLength) ? (int)fileSize : mSaveDataLength;
        size_t bytesRead = fread(mSaveDataBuffer, 1, readSize, f);
        fclose(f);
        if ((int)bytesRead != readSize) {
            MILO_WARN("MemcardMgr: read %zu of %d bytes from '%s'\n", bytesRead, readSize, path);
            ret = kMCReadWriteFailed;
        } else {
            printf("DC3 Native: loaded %d bytes from %s\n", readSize, path);
            ret = kMCNoError;
        }
        break;
    }
    case kS_DeleteSaves:
        ret = kMCNoError;
        break;
    }
    return ret;
}

void MemcardMgr::ThreadDone(int mcResult) {
    State oldState = mState;
    mState = kS_None;
    switch (oldState) {
    case kS_None:
        break;
    case kS_Search:
    case kS_CheckForSaveContainer: {
        MCResultMsg msg((MCResult)mcResult);
        Export(msg, true);
        break;
    }
    case kS_SaveGame:
    case kS_LoadGame: {
        if (mAction) {
            mAction->SetResult((MCResult)mcResult);
            mAction->PostAction();
        }
        SaveLoadProfileComplete(mProfile, mcResult);
        break;
    }
    case kS_DeleteSaves: {
        MCResultMsg msg((MCResult)mcResult);
        Export(msg, true);
        break;
    }
    }
}

void MemcardMgr::SetDevice(unsigned int) {
    // No device selection needed on native
}

void MemcardMgr::SelectDevice(Profile *pProfile, Hmx::Object *callback, int padNum, bool) {
    // Auto-select — filesystem is always available
    mProfile = pProfile;
    mPadNum = padNum;
    mSelectDeviceCallBackObj = callback;
    mValidDevices[padNum] = true;

    // Immediately report device chosen
    if (callback) {
        DeviceChosenMsg msg(0);
        callback->Handle(msg, true);
    }
}
