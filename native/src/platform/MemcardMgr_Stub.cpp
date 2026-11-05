// DC3 Native Port - MemcardMgr Native Implementation
// Replaces MemcardMgr_Xbox.cpp — filesystem-based save/load
//
// The byte round-trip here was always correct; the *message contract* around
// it was not, and the contract is the only thing the image's SaveLoadManager
// state machine listens to (SaveLoadManager::OnMsg(const MCResultMsg &) and
// its Poll arms for states 4 / 0x46 / 0x47).  When this file exported nothing
// — and, on the search path, exported kMCNoError, which state 4's poll maps
// to its kS_SaveLoadError default arm — mWaiting was set and never cleared,
// so a save or a load could not finish.  Every operation below is now the
// image's own shape:
//
//     On* -> mState = kS_... -> ThreadStart() -> ThreadCall_*() -> ThreadDone()
//
// with the work still done synchronously on the caller's thread (native has
// no ThreadCall pump to poll) and exactly one MCResultMsg exported from
// ThreadDone:
//   * search / check-container: kMCFileExists (7) when this pad has a save,
//     kMCFileNotFound (8) otherwise.  These are the only two values state 4
//     advances on (7 -> load, 8 -> no-save path); 0 is a hard error there.
//   * save / load: kMCNoError (0), or the MemcardAction's own result when the
//     action's PostAction (the deserialise half of a load) failed.
//   * delete: kMCNoError, or kMCFileNotFound when there was nothing to remove.

#include "meta/MemcardMgr.h"
#include "obj/Dir.h"
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

// Does this pad have a save?  The image's answer to the same question is
// "does the container exist on the device" (FindValidUnit /
// CheckForSaveContainer); locally the container is the save file.
static bool SaveFileExists(int padNum) {
    char path[512];
    GetSavePath(path, sizeof(path), padNum);
    struct stat st;
    return stat(path, &st) == 0;
}

MemcardMgr::MemcardMgr()
    : mState(kS_None), mSaveDataBuffer(0), mSaveDataLength(0), mAction(0),
      mSaveCreateType(0), mPendingDeviceSelectorIndex(-1), mSelectDeviceWaiting(0),
      mSelectDeviceCallBackObj(0), mPadNum(-1), mProfile(0) {
    // The image's Init() zeroes these; native's Init may not run before the
    // manager is used (the boot wiring is a separate change), so the ctor
    // owns the initial state.
    for (int i = 0; i < 4; i++) {
        // ContainerId::Set lives in the Xbox TU (Memcard_Xbox.cpp), which
        // native does not compile, so the fields are set directly.
        mContainerIDs[i].mUserIndex = i;
        mContainerIDs[i].mDeviceId = XCONTENTDEVICE_ANY;
        mContainerIDs[i].unk8 = 0;
        mContainers[i] = nullptr;
        mValidDevices[i] = false;
    }
}

MemcardMgr::~MemcardMgr() {}

DataNode MemcardMgr::Handle(DataArray *da, bool ret) {
    return Hmx::Object::Handle(da, ret);
}

void MemcardMgr::Init() {
    // DTA reaches the manager by name (`{memcardmgr add_sink ...}`), so the
    // object must be named in the main dir exactly as the image names it.
    SetName("memcardmgr", ObjectDir::Main());
    for (int i = 0; i < 4; i++) {
        mValidDevices[i] = false;
        mContainers[i] = nullptr;
        mContainerIDs[i].mUserIndex = i;
        mContainerIDs[i].mDeviceId = XCONTENTDEVICE_ANY;
        mContainerIDs[i].unk8 = 0;
    }
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
    MILO_ASSERT(pProfile, 0xB5);
    mProfile = pProfile;
    mPadNum = mProfile->GetPadNum();
    MILO_ASSERT(mState == kS_None, 0xBA);
    mState = kS_Search;
    ThreadDone(ThreadStart());
}

void MemcardMgr::OnCheckForSaveContainer(Profile *pProfile) {
    MILO_ASSERT(pProfile, 0x1BF);
    mProfile = pProfile;
    mPadNum = mProfile->GetPadNum();
    MILO_ASSERT(mState == kS_None, 0x1C3);
    mState = kS_CheckForSaveContainer;
    // Existence only.  The image mounts, opens the save and unmounts — it
    // never reads the container's contents here, and neither do we.
    ThreadDone(ThreadStart());
}

void MemcardMgr::OnSaveGame(Profile *pProfile, MemcardAction *pAction, int createType) {
    MILO_ASSERT(pProfile, 0x26D);
    mSaveCreateType = createType;
    mProfile = pProfile;
    mPadNum = mProfile->GetPadNum();
    mAction = pAction;
    MILO_ASSERT(mAction, 0x274);
    mAction->PreAction();
    MCResult res = mAction->Result();
    if (res != kMCNoError) {
        // The serialize half failed — report it now; the caller is waiting on
        // exactly one MCResultMsg and no thread work is warranted.
        MCResultMsg msg(res);
        Export(msg, true);
    } else {
        MILO_ASSERT(mState == kS_None, 0x281);
        mState = kS_SaveGame;
        ThreadDone(ThreadStart());
    }
}

void MemcardMgr::OnLoadGame(Profile *pProfile, MemcardAction *pAction) {
    MILO_ASSERT(pProfile, 0x2D9);
    mProfile = pProfile;
    mPadNum = mProfile->GetPadNum();
    mAction = pAction;
    MILO_ASSERT(mAction, 0x2DF);
    mAction->PreAction();
    MILO_ASSERT(mState == kS_None, 0x2E2);
    mState = kS_LoadGame;
    ThreadDone(ThreadStart());
}

void MemcardMgr::OnDeleteSaves(Profile *pProfile) {
    MILO_ASSERT(pProfile, 0x2EF);
    mProfile = pProfile;
    mPadNum = mProfile->GetPadNum();
    MILO_ASSERT(mState == kS_None, 0x2F4);
    mState = kS_DeleteSaves;
    ThreadDone(ThreadStart());
}

MCResult MemcardMgr::ThreadCall_SearchForDevice() {
    MILO_ASSERT(mSelectDeviceWaiting == false, 0x9D);
    MILO_ASSERT(mProfile, 0x9E);
    MILO_ASSERT(mPadNum != -1, 0x9F);
    // The image asks the platform for a valid unit on this pad and answers
    // kMCFileExists when one is found.  Locally the device always exists, so
    // the question that remains — and the one SaveLoadManager actually acts
    // on — is whether this pad already has a save on it.
    if (SaveFileExists(mPadNum)) {
        mValidDevices[mPadNum] = true; // image: IsDeviceValid on the found unit
        return kMCFileExists;
    }
    return kMCFileNotFound;
}

MCResult MemcardMgr::ThreadCall_CheckForSaveContainer() {
    return SaveFileExists(mPadNum) ? kMCFileExists : kMCFileNotFound;
}

MCResult MemcardMgr::ThreadCall_DeleteSaves() {
    char path[512];
    GetSavePath(path, sizeof(path), mPadNum);
    return (remove(path) == 0) ? kMCNoError : kMCFileNotFound;
}

MCResult MemcardMgr::ThreadCall_SaveGame() {
    if (!mSaveDataBuffer || mSaveDataLength <= 0) {
        return kMCReadWriteFailed;
    }
    // createType 0 ("open existing only", SaveLoadManager kS_SaveNoOverwrite)
    // reports the already-present save rather than overwriting it, exactly as
    // the image's mount of an existing container does.
    if (mSaveCreateType == 0 && SaveFileExists(mPadNum)) {
        return kMCFileExists;
    }
    EnsureSaveDir();
    char path[512];
    GetSavePath(path, sizeof(path), mPadNum);
    FILE *f = fopen(path, "wb");
    if (!f) {
        MILO_WARN("MemcardMgr: failed to open '%s' for writing\n", path);
        return kMCReadWriteFailed;
    }
    size_t written = fwrite(mSaveDataBuffer, 1, mSaveDataLength, f);
    fclose(f);
    if ((int)written != mSaveDataLength) {
        MILO_WARN("MemcardMgr: wrote %zu of %d bytes to '%s'\n", written, mSaveDataLength, path);
        return kMCReadWriteFailed;
    }
    printf("DC3 Native: saved %d bytes to %s\n", mSaveDataLength, path);
    return kMCNoError;
}

MCResult MemcardMgr::ThreadCall_LoadGame() {
    if (!mSaveDataBuffer || mSaveDataLength <= 0) {
        return kMCReadWriteFailed;
    }
    char path[512];
    GetSavePath(path, sizeof(path), mPadNum);
    FILE *f = fopen(path, "rb");
    if (!f) {
        return kMCFileNotFound;
    }
    // Get file size
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fileSize <= 0) {
        fclose(f);
        return kMCCorrupt;
    }
    int readSize = (fileSize < mSaveDataLength) ? (int)fileSize : mSaveDataLength;
    size_t bytesRead = fread(mSaveDataBuffer, 1, readSize, f);
    fclose(f);
    if ((int)bytesRead != readSize) {
        MILO_WARN("MemcardMgr: read %zu of %d bytes from '%s'\n", bytesRead, readSize, path);
        return kMCReadWriteFailed;
    }
    printf("DC3 Native: loaded %d bytes from %s\n", readSize, path);
    return kMCNoError;
}

int MemcardMgr::ThreadStart() {
    int ret = 0;
    switch (mState) {
    case kS_None:
        // Image: MILO_FAIL.  Native keeps the warning (a fail here would be
        // an assert flood for a recoverable misuse).
        MILO_WARN("ThreadCall triggered MemcardMgr::ThreadStart with no mode set.\n");
        break;
    case kS_Search:
        ret = ThreadCall_SearchForDevice();
        break;
    case kS_CheckForSaveContainer:
        ret = ThreadCall_CheckForSaveContainer();
        break;
    case kS_SaveGame:
        ret = ThreadCall_SaveGame();
        break;
    case kS_LoadGame:
        ret = ThreadCall_LoadGame();
        break;
    case kS_DeleteSaves:
        ret = ThreadCall_DeleteSaves();
        break;
    }
    return ret;
}

void MemcardMgr::ThreadDone(int mcResult) {
    State oldState = mState;
    mState = kS_None;
    switch (oldState) {
    case kS_None:
        MILO_WARN("ThreadCall triggered MemcardMgr::ThreadDone with no mode set.\n");
        break;
    case kS_Search:
    case kS_CheckForSaveContainer: {
        MCResultMsg msg((MCResult)mcResult);
        Export(msg, true);
        break;
    }
    case kS_SaveGame:
    case kS_LoadGame: {
        // mAction is non-null here by construction: OnSaveGame and OnLoadGame
        // both assert it before entering these states, and nothing else sets
        // them.  The image dereferences it without a check; so do we.
        mAction->SetResult((MCResult)mcResult);
        mAction->PostAction();
        // A load's PostAction is where the deserialise happens, so its
        // verdict (corrupt / obsolete / newer version) outranks a clean read.
        if (mcResult == 0 && mAction->Result() != kMCNoError) {
            mcResult = mAction->Result();
        }
        mAction = nullptr;
        mPadNum = -1;
        MCResultMsg msg((MCResult)mcResult);
        Export(msg, true);
        break;
    }
    case kS_DeleteSaves: {
        mPadNum = -1;
        MCResultMsg msg((MCResult)mcResult);
        Export(msg, true);
        break;
    }
    default:
        break;
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
