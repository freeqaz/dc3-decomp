// DC3 Native Port - KinectShare Stub
// Replaces lazer/net_ham/KinectShare.cpp, KinectShareJobs.cpp, KinectSharePanel.cpp
// No Kinect/XSocial on native — provides minimal implementation for object system

#include "meta_ham/KinectSharePanel.h"
#include "obj/Data.h"
#include "os/Debug.h"

KinectSharePanel::KinectSharePanel()
    : mTex(this), mUploadState(0), mBuf(0), mPreviewBuf(0), unk58(0) {
    memset(&mOverlapped, 0, sizeof(XOVERLAPPED));
}

BEGIN_HANDLERS(KinectSharePanel)
    HANDLE_SUPERCLASS(UIPanel)
END_HANDLERS

BEGIN_PROPSYNCS(KinectSharePanel)
    SYNC_PROP(texture, mTex)
END_PROPSYNCS

void KinectSharePanel::Poll() {
    UIPanel::Poll();
}

void KinectSharePanel::ConvertImages() {}
void KinectSharePanel::ConvertImagesForLinkPost() {}
DataNode KinectSharePanel::OnUpload(DataArray *) { return 0; }
DataNode KinectSharePanel::OnPostLink(DataArray *) { return 0; }
DataNode KinectSharePanel::OnCleanup(DataArray *) { return 0; }
DataNode KinectSharePanel::OnMsg(const RockCentralOpCompleteMsg &) { return 0; }
