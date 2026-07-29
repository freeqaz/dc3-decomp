#include "obj/Dir.h"
#include "obj/DirUnloader.h"
#include "os/Debug.h"
#include "utl/Loader.h"

const char *DirUnloader::DebugText() { return MakeString("UnLoader: %s", mFile.c_str()); }

void DirUnloader::PollLoading() {
    TheLoadMgr.StartAsyncUnload();
    if (mObjects.empty()) {
        delete this;
    } else {
        Hmx::Object *obj = mObjects.back();
        if (obj) {
#ifdef HX_NATIVE
            // Drop our own ref while obj is still alive. Inside the cascade
            // scope below ~Object skips ReplaceRefs(nullptr), and this vector
            // slot holds the only ref left in obj's ring (it was created after
            // ~ObjectDir ran NullifyAllRefs). Leaving it set would make the
            // pop_back() below unlink through a freed object.
            mObjects.back() = nullptr;
            // ~ObjectDir already nullified every ref into this dir, then left
            // the deletes to us — so by now each RndTransformable child's
            // ObjOwnerPtr mParent is null while its parent's raw mChildren
            // list still points at it. Without re-entering cascade mode the
            // parent's ~RndTransformable walks that list and dereferences
            // freed children.
            ObjectDir::CascadeDeleteScope cascade;
#endif
            delete obj;
        }
        mObjects.pop_back();
    }
    TheLoadMgr.FinishAsyncUnload();
}

DirUnloader::~DirUnloader() { MILO_ASSERT(mObjects.empty(), 0x20); }

DirUnloader::DirUnloader(ObjectDir *dir)
    : Loader(dir->GetPathName(), kLoadFront), mObjects() {
    mObjects.reserve(dir->HashTableSize() / 2);
    for (ObjDirItr<Hmx::Object> it(dir, false); it != 0; ++it) {
        Hmx::Object *cur = it;
        if (cur != dir) {
            cur->SetName(nullptr, nullptr);
            mObjects.push_back(ObjPtr<Hmx::Object>(this, cur));
        }
    }
}
