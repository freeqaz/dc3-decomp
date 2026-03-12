// DC3 Native Port - MemcardMgr Stub
// Replaces MemcardMgr_Xbox.cpp — platform-specific methods only

#include "meta/MemcardMgr.h"

MemcardMgr::MemcardMgr()
    : mState(kS_None), mAction(0), mSaveCreateType(0),
      mPendingDeviceSelectorIndex(-1), mSelectDeviceWaiting(0),
      mSelectDeviceCallBackObj(0), mPadNum(-1), mProfile(0) {}

MemcardMgr::~MemcardMgr() {}

DataNode MemcardMgr::Handle(DataArray *da, bool ret) {
    return Hmx::Object::Handle(da, ret);
}

int MemcardMgr::ThreadStart() { return 0; }
void MemcardMgr::ThreadDone(int) {}

void MemcardMgr::Init() {}
bool MemcardMgr::IsStorageDeviceValid(Profile *) { return false; }
void MemcardMgr::OnCheckForSaveContainer(Profile *) {}
void MemcardMgr::OnDeleteSaves(Profile *) {}
void MemcardMgr::OnSaveGame(Profile *, MemcardAction *, int) {}
void MemcardMgr::OnLoadGame(Profile *, MemcardAction *) {}
void MemcardMgr::OnSearchForDevice(Profile *) {}
void MemcardMgr::SetDevice(unsigned int) {}
void MemcardMgr::SelectDevice(Profile *, Hmx::Object *, int, bool) {}
