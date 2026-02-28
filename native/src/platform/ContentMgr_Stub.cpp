// DC3 Native Port - ContentMgr Stub
// Replaces ContentMgr_Xbox.cpp - no DLC content

#include "os/ContentMgr.h"

// Native ContentMgr: no DLC, no content refresh, everything always "done"
class NativeContentMgr : public ContentMgr {
public:
    NativeContentMgr() { mState = kDone; }
    virtual void Init() {
        mState = kDone;
        ContentMgr::Init();
    }
};

static NativeContentMgr gContentMgr;
ContentMgr &TheContentMgr = gContentMgr;
