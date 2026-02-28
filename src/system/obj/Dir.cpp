#include "obj/Dir.h"
#include "Dir.h"
#include "Msg.h"
#include "Object.h"
#include "Utl.h"
#include "math/Mtx.h"
#include "math/Rot.h"
#include "obj/Data.h"
#include "obj/DataFunc.h"
#include "obj/DataUtl.h"
#include "obj/DirLoader.h"
#include "obj/DirUnloader.h"
#include "obj/Object.h"
#include "obj/PropSync.h"
#include "os/Debug.h"
#include "os/File.h"
#include "os/System.h"
#include "utl/FilePath.h"
#include "utl/Loader.h"
#include "utl/MemMgr.h"
#include "utl/Option.h"
#include "utl/Std.h"
#include "utl/Symbol.h"
#include "utl/BinStream.h"
#include <utility>

const char *kNotObjectMsg = "Could not find %s in dir \"%s\"";
ObjectDir *ObjectDir::sMainDir;
ObjectDir *gDir;
std::map<std::pair<Symbol, Symbol>, bool> ObjectDir::sSuperClassMap;

#pragma region Virtual Methods

ObjectDir::ObjectDir()
    : mHashTable(0, Entry(), Entry(), 0), mStringTable(0), mProxyOverride(false),
      mInlineProxyType(kInlineCached), mLoader(nullptr), mIsSubDir(false),
      mInlineSubDirType(kInlineNever), mPathName(gNullStr), mViewports(7),
      mCurViewportID((ViewportId)0), unk8c(nullptr), mCurCam(nullptr), mAlwaysInlined(0),
      mAlwaysInlineHash(gNullStr) {
    ResetViewports();
}

ObjectDir::~ObjectDir() {
    mSubDirs.clear();
    delete mLoader;
    if (TheLoadMgr.AsyncUnload()) {
        new DirUnloader(this);
    } else {
        DeleteObjects();
        DeleteSubDirs();
    }
    if (!IsProxy()) {
        SetName(nullptr, nullptr);
    }
    if (mPathName != gNullStr) {
        MemOrPoolFree(strlen(mPathName) + 1, (void *)mPathName);
    }
    if (mAlwaysInlineHash != gNullStr) {
        MemOrPoolFree(strlen(mAlwaysInlineHash) + 1, (void *)mAlwaysInlineHash);
    }
}

BEGIN_HANDLERS(ObjectDir)
    HANDLE_ACTION(iterate, Iterate(_msg, true))
    HANDLE_ACTION(iterate_self, Iterate(_msg, false))
    HANDLE_ACTION(save_objects, DirLoader::SaveObjects(_msg->Str(2), this, false))
    HANDLE(find, OnFind)
    HANDLE_EXPR(exists, FindObject(_msg->Str(2), false, true) != nullptr)
    HANDLE_ACTION(sync_objects, SyncObjects())
    HANDLE_EXPR(is_proxy, IsProxy())
    HANDLE_EXPR(proxy_dir, ProxyDir())
    HANDLE_EXPR(proxy_name, ProxyName())
    HANDLE_ACTION(
        add_names,
        Reserve(
            mHashTable.Size() + _msg->Int(2) * 2,
            mStringTable.Size() + _msg->Int(2) * 0x14
        )
    )
    HANDLE_ACTION(override_proxy, SetProxyFile(_msg->Str(2), true))
    HANDLE_ACTION(delete_loader, RELEASE(mLoader))
    HANDLE_ACTION(reset_editor_state, ResetEditorState())
    HANDLE_EXPR(get_path_name, mPathName)
    HANDLE_EXPR(get_file_name, FileGetName(mPathName))
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

ObjectDir *ObjectDir::ProxyDir() const {
    return Loader() ? Loader()->ProxyDir() : nullptr;
}

const char *ObjectDir::ProxyName() const {
    return Loader() ? (Loader()->ProxyName() ? Loader()->ProxyName() : "") : "";
}

ObjectDir *SyncSubDir(const FilePath &fp, ObjectDir *dir) {
    Loader *loader = TheLoadMgr.GetLoader(fp);
    DirLoader *dirLoader = dir->IsProxy()
        ? dynamic_cast<DirLoader *>(loader)
        : dynamic_cast<DirLoader *>(TheLoadMgr.ForceGetLoader(fp));
    if (!dirLoader)
        return nullptr;
    ObjectDir *retDir = dirLoader->GetDir();
    if (retDir) {
        for (ObjDirItr<Hmx::Object> it(dir, false); it != nullptr; ++it) {
            Hmx::Object *found = retDir->FindObject(it->Name(), false, true);
            if (found && found != retDir && &*it != dir) {
                MILO_NOTIFY(
                    "%s exists in dir and subdir, so replacing %s with %s",
                    it->Name(),
                    PathName(it),
                    PathName(found)
                );
                it->ReplaceRefs(found);
                delete it;
            }
        }
    }
    return retDir;
}

bool PropSyncSubDirs(
    std::vector<ObjDirPtr<ObjectDir> > &subdirs,
    DataNode &val,
    DataArray *prop,
    int i,
    PropOp op
) {
    ObjectDir *theGDir = gDir;
    if (op == kPropSize) {
        MILO_ASSERT(i == prop->Size(), 0x947);
        val = (int)subdirs.size();
        return true;
    } else {
        MILO_ASSERT(i == prop->Size() - 1, 0x94D);
        std::vector<ObjDirPtr<ObjectDir> >::iterator subdirIt =
            subdirs.begin() + prop->Int(i);
        ObjDirPtr<ObjectDir> &ptr = *subdirIt;
        if (op == kPropSet || op == kPropInsert) {
            FilePath valPath = val.Str();
            FilePath relative =
                FileRelativePath(FilePath::Root().c_str(), valPath.c_str());
            FOREACH (it, subdirs) {
                if (it != subdirIt) {
                    const char *curRelative =
                        FileRelativePath(FilePath::Root().c_str(), it->GetFile().c_str());
                    if (streq(relative.c_str(), curRelative)) {
                        MILO_NOTIFY(
                            "Subdir '%s' can't be added to '%s' more than once!",
                            relative,
                            PathName(theGDir)
                        );
                        return true;
                    }
                }
            }
        }
        switch (op) {
        case kPropGet:
            val = FileRelativePath(FilePath::Root().c_str(), ptr.GetFile().c_str());
            break;
        case kPropSet:
            theGDir->RemovingSubDir(ptr);
            ptr = SyncSubDir(val.Str(), theGDir);
            theGDir->AddedSubDir(ptr);
            break;
        case kPropRemove:
            theGDir->RemovingSubDir(ptr);
            subdirs.erase(subdirIt);
            break;
        case kPropInsert:
            subdirIt = subdirs.insert(subdirIt, SyncSubDir(val.Str(), theGDir));
            theGDir->AddedSubDir(*subdirIt);
            break;
        default:
            return false;
        }
        return true;
    }
}

BEGIN_PROPSYNCS(ObjectDir)
    gDir = this;
    {
        static Symbol _s("subdirs");
        if (sym == _s) {
            PropSyncSubDirs(mSubDirs, _val, _prop, _i + 1, _op);
            return true;
        }
    }
    SYNC_PROP_SET(
        proxy_file,
        FileRelativePath(FilePath::Root().c_str(), ProxyFile().c_str()),
        SetProxyFile(_val.Str(), false)
    )
    SYNC_PROP(inline_proxy, (int &)mInlineProxyType)
    SYNC_PROP_SET(path_name, mPathName, )
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

inline BinStream &operator<<(BinStream &bs, const ObjectDir::Viewport &v) {
    bs << v.mXfm;
    return bs;
}

void ObjectDir::Save(BinStream &bs) {
    SAVE_REVS(0x1C, 0)
    SaveType(bs);
    bs << mAlwaysInlined;
    if (mAlwaysInlineHash && !bs.Cached()) {
        int len = strlen(mAlwaysInlineHash);
        bs << len;
        bs.Write(mAlwaysInlineHash, len);
    } else {
        bs << 0;
    }
    bs << mViewports;
    bs << mCurViewportID;
    bs << (unsigned char)mInlineProxyType;
    bs << mProxyFile;
    std::vector<ObjDirPtr<ObjectDir> > inlinedSubDirs;
    std::vector<ObjDirPtr<ObjectDir> > notInlinedSubDirs;
    if (SaveSubdirs()) {
        for (int i = 0; i < mSubDirs.size(); i++) {
            if (mSubDirs[i]) {
                ObjDirPtr<ObjectDir> &curSubDir = mSubDirs[i];
                if (curSubDir->InlineSubDirType() != kInlineNever) {
                    inlinedSubDirs.push_back(curSubDir);
                } else {
                    notInlinedSubDirs.push_back(curSubDir);
                }
            }
        }
    }
    bs << notInlinedSubDirs;
    bs << (unsigned char)mInlineSubDirType;
    bs << inlinedSubDirs;

    for (int i = 0; i < inlinedSubDirs.size(); i++) {
        InlineDirType iType = ((ObjectDir *)inlinedSubDirs[i])->InlineSubDirType();
        bs << (unsigned char)iType;
        SaveInlined(inlinedSubDirs[i].GetFile(), false, iType);
    }

    std::vector<bool> boolVec;
    boolVec.resize(mInlinedDirs.size(), false);
    for (int i = 0; i < mInlinedDirs.size(); i++) {
        InlinedDir &id = mInlinedDirs[i];
        switch (id.mType) {
        case kInlineCachedShared:
            id.shared = true;
        case kInlineCached: {
            bool old = gLoadingProxyFromDisk;
            if (!bs.Cached()) {
                id.dir = nullptr;
            } else {
                gLoadingProxyFromDisk = false;
                DirLoader::SetCacheMode(false);
                id.dir.LoadFile(id.file, false, false, kLoadFront, true);
                DirLoader::SetCacheMode(true);
                gLoadingProxyFromDisk = old;
            }
            break;
        }
        default: {
            MILO_ASSERT(id.mType == kInlineAlways, 0x211);
            int gg = 0;
            for (; gg != mSubDirs.size(); gg++) {
                if (mSubDirs[gg].GetFile() == id.file)
                    break;
            }
            MILO_ASSERT(gg < mSubDirs.size(), 0x21A);
            id.dir = (ObjectDir *)mSubDirs[gg];
            if (id.shared) {
                id.shared = false;
                MILO_NOTIFY("Can't share kInlineAlways dirs");
            }
            break;
        }
        }
        // what's happening here?
        if (id.dir) {
        } else {
        }
        bs << boolVec[i];
    }

    bool oldProxy = gLoadingProxyFromDisk;
    gLoadingProxyFromDisk = false;
    for (int i = mInlinedDirs.size() - 1; i >= 0; i--) {
        InlinedDir &id = mInlinedDirs[i];
        if (!boolVec[i]) {
            if (id.dir->IsSubDir()) {
                RemovingSubDir(id.dir);
            }
            String dirName = id.dir->Name();
            ObjectDir *dirDir = id.dir->Dir();
            if (!id.shared) {
                ObjectDir *dirToSet = id.dir;
                if (dirToSet->Dir()) {
                    int uniqIdx = 0;
                    const char *uniqStr;
                    while (true) {
                        uniqStr = MakeString("uniq%x", uniqIdx);
                        if (!dirToSet->FindContainingDir(uniqStr)
                            && !FindContainingDir(uniqStr))
                            break;
                        uniqIdx++;
                    }
                    dirToSet->SetName(uniqStr, dirToSet);
                }
            }
            FilePathTracker tracker(FileGetPath(id.file.c_str()));
            DirLoader::SaveObjects(bs, id.dir);
            if (!id.shared) {
                id.dir->SetName(dirName.c_str(), dirDir);
            }
            if (id.dir->IsSubDir()) {
                AddedSubDir(id.dir);
            }
        }
    }
    std::vector<InlinedDir> unused;
    mCurViewportID = (ViewportId)0;
    const char *nextname = unk8c ? unk8c->Name() : "";
    gLoadingProxyFromDisk = oldProxy;
    bs << nextname;
    const char *camName = mCurCam ? mCurCam->Name() : "";
    bs << camName;
    SaveRest(bs);
    gLoadingProxyFromDisk = false;
}

BEGIN_COPYS(ObjectDir)
    COPY_SUPERCLASS(Hmx::Object)
    if (ty != kCopyFromMax) {
        CREATE_COPY(ObjectDir)
        BEGIN_COPYING_MEMBERS
            if (!IsProxy()) {
                COPY_MEMBER(mViewports)
                COPY_MEMBER(mCurViewportID)
                for (int i = 0; i < mSubDirs.size(); i++) {
                    RemovingSubDir(mSubDirs[i]);
                }
                COPY_MEMBER(mSubDirs)
                for (int i = 0; i < mSubDirs.size(); i++) {
                    AddedSubDir(mSubDirs[i]);
                }
            }
            COPY_MEMBER(mInlineProxyType)
            COPY_MEMBER(mInlineSubDirType)
        END_COPYING_MEMBERS
    }
END_COPYS

void ObjectDir::Load(BinStream &bs) {
    PreLoad(bs);
    PostLoad(bs);
    if (IsProxy() && mLoader && !mLoader->IsLoaded()) {
        TheLoadMgr.PollUntilLoaded(mLoader, nullptr);
    }
}

void ObjectDir::PostSave(BinStream &) { SyncObjects(); }

void ObjectDir::SetProxyFile(const FilePath &file, bool override) {
    if (!IsProxy()) {
        MILO_NOTIFY("Can't set proxy file if own dir");
    } else {
        mProxyFile = file;
        mProxyOverride = override;
        if (!override) {
            DeleteObjects();
            DeleteSubDirs();
            if (!mProxyFile.empty()) {
                DirLoader *dl = new DirLoader(
                    mProxyFile, kLoadFront, nullptr, nullptr, this, false, nullptr
                );
                TheLoadMgr.PollUntilLoaded(dl, nullptr);
            }
        }
    }
}

void ObjectDir::SetSubDir(bool isSubdir) {
    if (isSubdir) {
        mIsSubDir = true;
        SetName(nullptr, nullptr);
        SetTypeDef(nullptr);
    }
}

void ObjectDir::SyncObjects() {
    static Message sync_objects("sync_objects");
    HandleType(sync_objects);
}

void ObjectDir::ResetEditorState() {
    mViewports.resize(kNumViewports);
    mCurViewportID = (ViewportId)0;
    unk8c = nullptr;
    mCurCam = nullptr;
    ResetViewports();
}

void ObjectDir::AddedObject(Hmx::Object *) {}

void ObjectDir::RemovingObject(Hmx::Object *obj) {
    if (obj == unk8c) {
        unk8c = nullptr;
    }
    if (obj == mCurCam) {
        mCurCam = nullptr;
        if (mCurViewportID == 7) {
            mCurViewportID = (ViewportId)0;
        }
    }
}

void ObjectDir::OldLoadProxies(BinStream &bs, int i) {
    int x;
    bs >> x;
    if (x != 0)
        MILO_FAIL("Proxies not allowed here");
}

#pragma endregion

BinStream &operator>>(BinStream &bs, InlineDirType &ty) {
    unsigned char uc;
    bs >> uc;
    ty = (InlineDirType)uc;
    return bs;
}

bool ObjectDir::SaveSubdirs() {
    return !IsProxy() || mProxyFile.empty() || gLoadingProxyFromDisk;
}

bool ObjectDir::ShouldSaveProxy(BinStream &bs) {
    return IsProxy() && (!mProxyFile.empty() || InlineProxy(bs));
}

void ObjectDir::SetInlineProxyType(InlineDirType t) {
    MILO_ASSERT(t != kInlineCachedShared, 0x198);
    mInlineProxyType = t;
}

BinStreamRev &operator>>(BinStreamRev &bs, ObjectDir::Viewport &v) {
    bs >> v.mXfm;
    if (bs.rev < 0x12) {
        int x;
        bs >> x;
    }
    return bs;
}

BinStream &operator>>(BinStream &bs, ObjectDir::Viewport &v) {
    bs >> v.mXfm;
    return bs;
}

void ObjectDir::TransferLoaderState(ObjectDir *dir) {
    mProxyFile = dir->mProxyFile;
    mProxyOverride = dir->mProxyOverride;
    mLoader = dir->mLoader;
    dir->mLoader = nullptr;
}

bool ObjectDir::HasDirPtrs() const {
    if (sDeleting == this) {
        return true;
    } else {
        FOREACH (it, mRefs) {
            if (it->IsDirPtr())
                return true;
        }
        return false;
    }
}

namespace {
    int gPreloadIdx = 0;
    ObjDirPtr<ObjectDir> gPreloaded[128];

    void DeleteShared() {
        for (; gPreloadIdx > 0; gPreloadIdx--) {
            gPreloaded[gPreloadIdx - 1] = 0;
        }
    }
}

ObjectDir::Viewport &ObjectDir::CurViewport() {
    if (mCurViewportID >= kNumViewports) {
        MILO_FAIL("%s mCurView = %d, >= kNumViewports", PathName(this), mCurViewportID);
    }
    return mViewports[mCurViewportID];
}

bool ObjectDir::HasSubDir(ObjectDir *dir) {
    if (this == dir)
        return true;
    else {
        for (int i = 0; i < mSubDirs.size(); i++) {
            if (mSubDirs[i] && mSubDirs[i]->HasSubDir(dir)) {
                return true;
            }
        }
    }
    return false;
}

void ObjectDir::SaveProxy(BinStream &bs) {
    if (ShouldSaveProxy(bs) && InlineProxy(bs)) {
        gLoadingProxyFromDisk = true;
        const char *path = mProxyFile.empty() ? FilePath::Root().c_str()
                                              : FileGetPath(mProxyFile.c_str());
        FilePathTracker tracker(path);
        DirLoader::SaveObjects(bs, this);
    }
}

void ObjectDir::ResetViewports() {
    Viewport *vp = &mViewports[0];
    vp[1].mXfm.m.Set(0, -1, 0, 1, 0, 0, 0, 0, 1);
    vp[1].mXfm.v.Set(-768, 0, 0);
    vp[2].mXfm.m.Set(0, 1, 0, -1, 0, 0, 0, 0, 1);
    vp[2].mXfm.v.Set(768, 0, 0);
    vp[3].mXfm.m.Set(1, 0, 0, 0, 0, 1, 0, 1, 0);
    vp[3].mXfm.v.Set(0, 0, 768);
    vp[4].mXfm.m.Set(1, 0, 0, 0, 0, 1, 0, -1, 0);
    vp[4].mXfm.v.Set(0, 0, -768);
    vp[5].mXfm.m.Set(1, 0, 0, 0, 1, 0, 0, 0, 1);
    vp[5].mXfm.v.Set(0, -768, 0);
    vp[6].mXfm.m.Set(-1, 0, 0, 0, -1, 0, 0, 0, 1);
    vp[6].mXfm.v.Set(0, 768, 0);
    MakeRotMatrix(Vector3(1, 1, -1), Vector3(0, 0, 1), vp[0].mXfm.m);
    Vector3 v(0, -768.0f, 0);
    Hmx::Matrix3 &m = vp[0].mXfm.m;
    vp[0].mXfm.v.Set(
        m.x.x * v.x + (m.z.x * v.z + m.y.x * v.y),
        m.x.y * v.x + (m.z.y * v.z + m.y.y * v.y),
        m.x.z * v.x + (m.z.z * v.z + m.y.z * v.y)
    );
}

DataNode OnLoadObjects(DataArray *a) {
    return DirLoader::LoadObjects(a->Str(1), nullptr, nullptr);
}

DataNode OnPathName(DataArray *a) { return PathName(a->Obj<Hmx::Object>(1)); }

DataNode OnReserveToFit(DataArray *a) {
    ReserveToFit(a->Obj<ObjectDir>(1), a->Obj<ObjectDir>(2), a->Int(3));
    return 0;
}

DataNode OnInitObject(DataArray *a) {
    a->Obj<Hmx::Object>(1)->InitObject();
    return 0;
}

void ObjectDir::Reserve(int hashSize, int stringSize) {
    MemTemp tmp;
    if (mHashTable.Size() < hashSize) {
        mHashTable.Resize(hashSize, 0);
    }
    mStringTable.Reserve(stringSize);
}

ObjectDir::InlinedDir::InlinedDir() : dir(), file() {}
ObjectDir::InlinedDir::~InlinedDir() {}

void ObjectDir::LoadSubDir(int i, const FilePath &fp, BinStream &bs, bool b) {
    if (IsProxy() && !mProxyFile.empty()) {
        mSubDirs[i] = 0;
    } else {
        FilePath subdirpath = GetSubDirPath(fp, bs);
        if (streq(mPathName, subdirpath.c_str())) {
            MILO_NOTIFY(
                "%s trying to subdir self in slot %d, setting NULL", PathName(this), i
            );
            mSubDirs[i] = 0;
        } else
            mSubDirs[i].LoadFile(subdirpath, true, b, kLoadFront, true);
    }
}

void PreloadArray(DataArray *arr, int idx) {
    for (int i = idx; i < arr->Size(); i++) {
        DataArray *curArr = arr->Array(i);
        if (curArr->Size() != 0) {
            if (curArr->Type(0) == kDataArray) {
                PreloadArray(curArr, 0);
            } else {
                const char *str = curArr->Str(0);
                bool shouldPop = false;
                if (curArr->Size() > 1) {
                    MemPushHeap(MemFindHeap(curArr->Sym(1).Str()));
                    shouldPop = true;
                }
                MILO_ASSERT(gPreloadIdx < DIM(gPreloaded), 0xA35);
                gPreloaded[gPreloadIdx++].LoadFile(str, false, true, kLoadFront, false);
                if (shouldPop) {
                    MemPopHeap();
                }
            }
        }
    }
}

void PreloadSharedSubdirs(Symbol s) {
    DataArray *arr = SystemConfig("preload_subdirs")->FindArray(s, false);
    if (arr) {
        PreloadArray(arr, 1);
    }
}

void ObjectDir::Terminate() {
    DeleteShared();
    sSuperClassMap.clear();
}

void ObjectDir::AddedSubDir(ObjDirPtr<ObjectDir> &subdir) {
    ObjectDir *dir = subdir;
    if (dir) {
        dir->InlineSubDirType();
        dir->SetSubDir(true);
        for (ObjDirItr<Hmx::Object> it(dir, true); it != nullptr; ++it) {
            AddedObject(it);
        }
    }
}

void ObjectDir::RemovingSubDir(ObjDirPtr<ObjectDir> &subdir) {
    ObjectDir *dir = subdir;
    if (dir) {
        dir->SetSubDir(false);
        for (ObjDirItr<Hmx::Object> it(dir, true); it != nullptr; ++it) {
            RemovingObject(it);
        }
    }
}

void ObjectDir::DeleteObjects() {
    for (ObjDirItr<Hmx::Object> it(this, false); it != nullptr; ++it) {
        if (it != this) {
            delete it;
        }
    }
}

void ObjectDir::RemoveSubDir(const ObjDirPtr<ObjectDir> &dPtr) {
    std::vector<ObjDirPtr<ObjectDir> >::iterator it = mSubDirs.begin();
    while (it != mSubDirs.end()) {
        if (*(u32 *)((u8 *)&(*it) + 0xc) == *(u32 *)((u8 *)&dPtr + 0xc)) {
            RemovingSubDir(*it);
            it = mSubDirs.erase(it);
            if (it == mSubDirs.end())
                break;
        }
        ++it;
    }
}

void ObjectDir::DeleteSubDirs() {
    for (int i = 0; i < mSubDirs.size(); i++) {
        RemovingSubDir(mSubDirs[i]);
    }
    mSubDirs.clear();
}

void CheckForDuplicates() {
    DataArray *cfg = SystemConfig("objects");
    std::list<Symbol> syms;
    for (int i = 1; i < cfg->Size(); i++) {
        syms.push_back(cfg->Array(i)->Sym(0));
    }
    syms.sort();
    Symbol previous;
    bool fail = false;
    for (std::list<Symbol>::iterator it = syms.begin(); it != syms.end();
         previous = *it, ++it) {
        Symbol cur = *it;
        if (cur == previous) {
            MILO_NOTIFY("Duplicate object %s in config", cur);
            fail = true;
        }
    }
    if (fail)
        MILO_FAIL("duplicate objects found in configs, bailing");
    syms.unique();
}

void ObjectDir::Init() {
    MessageTimer::Init();
    TheLoadMgr.RegisterFactory("milo", DirLoader::New);
    TheLoadMgr.RegisterFactory("milo_xbox", DirLoader::New);
    TheLoadMgr.RegisterFactory("milo_ps3", DirLoader::New);
    TheLoadMgr.RegisterFactory("milo_pc", DirLoader::New);
    TheLoadMgr.RegisterFactory("milo_ps2", DirLoader::New);
    TheLoadMgr.RegisterFactory("milo_wii", DirLoader::New);
    TheLoadMgr.RegisterFactory("rnd", DirLoader::New);
    TheLoadMgr.RegisterFactory("m2", DirLoader::New);
    TheLoadMgr.RegisterFactory("gh", DirLoader::New);
    TheLoadMgr.RegisterFactory("kr", DirLoader::New);
    CheckForDuplicates();
    DataRegisterFunc("load_objects", OnLoadObjects);
    DataRegisterFunc("init_object", OnInitObject);
    DataRegisterFunc("path_name", OnPathName);
    DataRegisterFunc("reserve_to_fit", OnReserveToFit);
    DirLoader::sPrintTimes = OptionBool("loader_times", false);
}

void ObjectDir::Iterate(DataArray *arr, bool b) {
    const DataNode &n = arr->Evaluate(2);
    Symbol s2;
    Symbol s8;
    if (n.Type() == kDataSymbol) {
        const char *str = n.UncheckedStr();
        s2 = STR_TO_SYM(str);
    } else {
        DataArray *a2 = n.UncheckedArray();
        s2 = a2->Sym(0);
        s8 = a2->Sym(1);
    }
    static DataArray *objects = SystemConfig("objects");
    objects->FindArray(s2);
    DataNode *var = arr->Var(3);
    DataNode varNode(*var);
    for (ObjDirItr<Hmx::Object> it(this, b); it != nullptr; ++it) {
        bool bbb;
        Symbol first = it->ClassName();
        std::pair<Symbol, Symbol> key = std::make_pair(first, s2);
        std::map<std::pair<Symbol, Symbol>, bool>::iterator superclassIt =
            sSuperClassMap.find(key);
        if (superclassIt == sSuperClassMap.end()) {
            bbb = IsASubclass(first, s2);
            sSuperClassMap[key] = bbb;
        } else
            bbb = superclassIt->second;
        if (bbb && (s2.Null() || it->Type() == s2)) {
            *var = &*it;
            for (int i = 4; i < arr->Size(); i++) {
                arr->Command(i)->Execute(true);
            }
        }
    }
    *var = varNode;
}

ObjDirPtr<ObjectDir> ObjectDir::PostLoadInlined() {
    MILO_ASSERT(mInlinedDirs.size() > 0, 0x296);
    InlinedDir iDir = mInlinedDirs.back();
    mInlinedDirs.pop_back();
    if (mInlinedDirs.size() == 0) {
        ClearAndShrink(mInlinedDirs);
    }
    if (iDir.shared && iDir.file.length() != 0 && !iDir.dir) {
        MILO_NOTIFY("Couldn't load shared inlined file %s\n", iDir.file);
    }
    return iDir.dir;
}

ObjectDir::Entry *ObjectDir::FindEntry(const char *name, bool add) {
    if (name == nullptr || *name == '\0')
        return nullptr;
    else {
        Entry *entry = mHashTable.Find(name);
        if (!entry && add) {
            Entry newEntry;
            newEntry.name = mStringTable.Add(name);
            entry = mHashTable.Insert(newEntry);
        }
        return entry;
    }
}

Hmx::Object *ObjectDir::FindObject(const char *name, bool parentDirs, bool subDirs) {
    Entry *entry = FindEntry(name, false);
    if (entry && entry->obj)
        return entry->obj;
    if (subDirs) {
        for (int i = 0; i < mSubDirs.size(); i++) {
            if (mSubDirs[i]) {
                Hmx::Object *found = mSubDirs[i]->FindObject(name, false, true);
                if (found)
                    return found;
            }
        }
    }
    if (strlen(name) != 0) {
        if (strcmp(name, Name()) == 0) {
            return this;
        }
    }
    if (parentDirs) {
        if (Dir() && Dir() != this) {
            return Dir()->FindObject(name, parentDirs, true);
        }
        if (this != sMainDir) {
            return sMainDir->FindObject(name, false, true);
        }
    }
    return nullptr;
}

ObjectDir *ObjectDir::FindContainingDir(const char *name) {
    if (FindEntry(name, false))
        return this;
    for (int i = 0; i < mSubDirs.size(); i++) {
        if (mSubDirs[i]) {
            ObjectDir *subdir = mSubDirs[i]->FindContainingDir(name);
            if (subdir)
                return subdir;
        }
    }
    return nullptr;
}

void ObjectDir::AppendSubDir(const ObjDirPtr<ObjectDir> &subdir) {
    mSubDirs.push_back(subdir);
    AddedSubDir(mSubDirs.back());
}

DataNode ObjectDir::OnFind(DataArray *da) {
    Hmx::Object *found = FindObject(da->Str(2), false, true);
    if (da->Size() > 3) {
        if (da->Int(3) != 0 && !found) {
            MILO_FAIL("Couldn't find %s in %s", da->Str(2), Name());
        }
    }
    return found;
}

void ObjectDir::PreInit(int hashSize, int stringSize) {
    REGISTER_OBJ_FACTORY(Hmx::Object);
    REGISTER_OBJ_FACTORY(ObjectDir);
    sMainDir = new ObjectDir();
    sMainDir->Reserve(hashSize, stringSize);
    sMainDir->SetName("main", sMainDir);
    DataSetThis(sMainDir);
    sSuperClassMap.clear();
    if (UsingCD()) {
        DirLoader::SetCacheMode(true);
    }
}

void ObjectDir::SaveInlined(const FilePath &fp, bool share, InlineDirType type) {
    MILO_ASSERT(type != kInlineNever, 0x26A);
    if (type == kInlineAlways && share) {
        MILO_NOTIFY("Can't share kInlineAlways Dirs");
        share = false;
    }
    InlinedDir dir;
    dir.file = fp;
    dir.shared = share;
    dir.mType = type;
    mInlinedDirs.push_back(dir);
}

void ObjectDir::PreLoadInlined(const FilePath &fp, bool share, InlineDirType type) {
    MILO_ASSERT(type != kInlineNever, 0x27C);
    if (type == kInlineAlways && share) {
        MILO_NOTIFY("Can't share kInlineAlways Dirs");
        share = false;
    }
    InlinedDir dir;
    dir.file = fp;
    dir.shared = share;
    dir.mType = type;
    mInlinedDirs.push_back(dir);
}

void ObjectDir::SetCurViewport(ViewportId id, Hmx::Object *o) {
    mCurViewportID = id;
    mCurCam = o;
}

void ObjectDir::SetSubDirFlag(bool flag) { mIsSubDir = flag; }

bool ObjectDir::InlineProxy(BinStream &bs) {
    return (mInlineProxyType == kInlineCached && bs.Cached())
        || mInlineProxyType == kInlineAlways;
}

void ObjectDir::SetPathName(const char *path) {
    if (mPathName != gNullStr) {
        MemOrPoolFree(strlen(mPathName) + 1, (void *)mPathName);
    }
    if (path != 0 && *path != '\0') {
        mPathName =
            (char *)MemOrPoolAlloc(strlen(path) + 1, __FILE__, 0x996, "path name");
        strcpy((char *)mPathName, path);
        mStoredFile.Set(FilePath::Root().c_str(), mPathName);
    } else
        mPathName = gNullStr;
}

FilePath ObjectDir::GetSubDirPath(const FilePath &fp, const BinStream &bs) {
    static Message msg("change_subdir", gNullStr);
    msg[0] = fp.c_str();
    DataNode handled = HandleType(msg);
    FilePath ret;
    if (handled.Type() == kDataUnhandled) {
        ret = fp;
    } else if (streq(handled.Str(), "stream_cache")) {
        bool cached = bs.Cached();
        ret = FilePath(".", DirLoader::CachedPath(fp.c_str(), cached));
    } else {
        ret = FilePath(FileRoot(), handled.Str());
    }
    return ret;
}

INIT_REVS(0x1C, 0)

void ObjectDir::PreLoad(BinStream &bs) {
    LOAD_REVS(bs)
    ASSERT_REVS(0x1C, 0)
#ifdef HX_NATIVE
    printf("ObjectDir::PreLoad '%s': rev=%d altRev=%d tell=%d\n", Name(), d.rev, d.altRev, bs.Tell());
#endif

    if (d.rev > 0x15) {
        LoadType(bs);
#ifdef HX_NATIVE
        printf("ObjectDir::PreLoad: after LoadType tell=%d\n", bs.Tell());
#endif
    } else if (d.rev >= 2 && d.rev <= 0x10) {
        Hmx::Object::Load(bs);
    }

    if (d.rev < 3) {
        int hashSize, strSize;
        bs >> hashSize >> strSize;
        Reserve(hashSize, strSize);
    }

    if (d.rev > 0x19) {
        if (d.rev < 0x1B) {
            bool b;
            bs >> b;
            mAlwaysInlined = b;
        } else {
            bs >> mAlwaysInlined;
        }
        int hashLen;
        bs >> hashLen;
#ifdef HX_NATIVE
        printf("ObjectDir::PreLoad: mAlwaysInlined=%d hashLen=%d tell=%d\n", mAlwaysInlined, hashLen, bs.Tell());
#endif
        if (hashLen) {
            char *hash = (char *)MemOrPoolAlloc(hashLen + 1, __FILE__, 0x9B0, nullptr);
            mAlwaysInlineHash = hash;
            bs.Read(hash, hashLen);
            hash[hashLen] = '\0';
        }
    }

    if (d.rev > 1) {
        bs >> mViewports;
        bs >> (int &)mCurViewportID;
#ifdef HX_NATIVE
        printf("ObjectDir::PreLoad: mViewports.size=%d mCurViewportID=%d tell=%d\n", (int)mViewports.size(), (int)mCurViewportID, bs.Tell());
#endif
    }

    if (d.rev > 0xC) {
        if (d.rev > 0x13) {
            if (!gLoadingProxyFromDisk) {
                unsigned char proxyType;
                bs >> proxyType;
                mInlineProxyType = (InlineDirType)proxyType;
            } else {
                unsigned char dummy;
                bs >> dummy;
            }
        }
        if (gLoadingProxyFromDisk || mProxyOverride) {
            bool fail = false;
            if (mProxyOverride && (mInlineProxyType == kInlineCached || mInlineProxyType == kInlineAlways)) {
                fail = true;
            }
            if (fail) {
                MILO_FAIL("You cannot override an inlined proxy!");
            }
            FilePath fp;
            bs >> fp;
            mProxyOverride = false;
        } else {
            FilePath fp;
            bs >> fp;
            if (!fp.empty() && fp == mProxyFile) {
                mProxyOverride = true;
            } else {
                mProxyFile = fp;
                mProxyOverride = false;
            }
        }
    }

    if (d.rev >= 2 && d.rev <= 10) {
        char buf[0x80];
        bs.ReadString(buf, 0x80);
    }
    if (d.rev >= 4 && d.rev <= 10) {
        char buf[0x80];
        bs.ReadString(buf, 0x80);
        mCurCam = FindObject(buf, false, true);
    }
    if (d.rev == 5) {
        char buf[0x80];
        bs.ReadString(buf, 0x80);
    }

    static std::vector<FilePath> inlinedSubDirs;
    static std::vector<FilePath> notInlinedSubDirs;

#ifdef HX_NATIVE
    printf("ObjectDir::PreLoad: before subdirs, proxyFile='%s' tell=%d\n", mProxyFile.c_str(), bs.Tell());
#endif
    if (d.rev > 2) {
        bs >> notInlinedSubDirs;
        std::vector<int> intVec;
        if (d.rev == 0x17) {
            bs >> intVec;
        }
        if (d.rev > 0x14) {
            bs >> mInlineSubDirType;
            bs >> inlinedSubDirs;
        } else {
            inlinedSubDirs.clear();
        }
#ifdef HX_NATIVE
        printf("ObjectDir::PreLoad: notInlinedSubDirs=%d inlinedSubDirs=%d inlineSubDirType=%d tell=%d\n",
               (int)notInlinedSubDirs.size(), (int)inlinedSubDirs.size(), mInlineSubDirType, bs.Tell());
#endif

        int i20 = 0;
        if (SaveSubdirs() || inlinedSubDirs.size() != 0 || notInlinedSubDirs.size() != 0) {
            for (int i = 0; i < mSubDirs.size(); i++) {
                RemovingSubDir(mSubDirs[i]);
            }
            if (!bs.Cached()
                && mSubDirs.size() == notInlinedSubDirs.size() + inlinedSubDirs.size()) {
                i20 = 1;
            } else {
                mSubDirs.reserve(notInlinedSubDirs.size() + inlinedSubDirs.size());
                mSubDirs.resize(notInlinedSubDirs.size() + inlinedSubDirs.size());
            }
        } else {
            i20 = 2;
        }

        for (int i = 0; i != notInlinedSubDirs.size(); i++) {
            bool filesneq = mSubDirs[i].GetFile() != notInlinedSubDirs[i];
            if (i20 == 0 || filesneq) {
                bool b17 = false;
                if (intVec.size() != 0) {
                    b17 = intVec[i] != 0;
                }
                LoadSubDir(i, notInlinedSubDirs[i], bs, !b17);
            }
        }

        if (d.rev > 0x17) {
            int numNotInlined = notInlinedSubDirs.size();
            for (int i = 0; i < inlinedSubDirs.size(); i++) {
                bool getfileres = mSubDirs[i + numNotInlined].GetFile() != inlinedSubDirs[i];
                InlineDirType dType;
                if (d.rev > 0x18) {
                    unsigned char b;
                    bs >> b;
                    MILO_ASSERT_RANGE_EQ(b, kInlineCached, kInlineCachedShared, 0x3C3);
                    dType = (InlineDirType)b;
                } else {
                    dType = kInlineCached;
                }
                inlinedSubDirs[i] = GetSubDirPath(inlinedSubDirs[i], bs);
                PreLoadInlined(inlinedSubDirs[i], false, dType);
                if (i20 == 1) {
                    bs.PushRev(getfileres, this);
                }
            }
            bs.PushRev(numNotInlined, this);
            if (!bs.Cached()) {
                bs.PushRev(i20, this);
            }
        }
    }

    if (d.rev == 12 || d.rev == 13) {
        OldLoadProxies(bs, d.rev);
    }

    if (d.rev < 0x13) {
        if (d.rev > 0xF) {
            int inlineProxy;
            bs >> inlineProxy;
            MILO_ASSERT(inlineProxy != 1, 0x3E1);
        } else if (d.rev > 0xE) {
            bool inlineProxy;
            bs >> inlineProxy;
            MILO_ASSERT(!inlineProxy, 0x3E6);
        }
    }

    std::vector<bool> boolVec;
    boolVec.resize(mInlinedDirs.size());
    for (int i = 0; i < mInlinedDirs.size(); i++) {
        if (d.rev < 0x19 && !bs.Cached()) {
            boolVec[i] = true;
        } else {
            bool b;
            bs >> b;
            boolVec[i] = b;
        }
    }

    for (int i = 0; i < mInlinedDirs.size(); i++) {
        InlinedDir &curIDir = mInlinedDirs[i];
        FilePath fpath(curIDir.file);
        if (!bs.Cached() || !boolVec[i]) {
            if (!boolVec[i] && (curIDir.mType == kInlineAlways || bs.Cached())) {
                curIDir.dir.LoadInlinedFile(fpath, bs);
            } else if (IsProxy() && !mProxyFile.empty()) {
                curIDir.dir = nullptr;
            } else {
                curIDir.dir.LoadFile(fpath, true, curIDir.shared, kLoadFront, true);
            }
        }
    }

    if (d.rev >= 21 && d.rev <= 23) {
        int offset = notInlinedSubDirs.size();
        MILO_ASSERT(mSubDirs.capacity() >= offset + inlinedSubDirs.size(), 0x41A);
        for (int i = 0; i < inlinedSubDirs.size(); i++) {
            mSubDirs[i + offset].LoadInlinedFile(inlinedSubDirs[i], bs);
        }
    }

    mIsSubDir = false;
#ifdef HX_NATIVE
    printf("ObjectDir::PreLoad: done, tell=%d\n", bs.Tell());
#endif
    bs.PushRev(packRevs(d.altRev, d.rev), this);
}

void ObjectDir::PostLoad(BinStream &bs) {
    BinStreamRev d(bs, bs.PopRev(this));
#ifdef HX_NATIVE
    printf("ObjectDir::PostLoad '%s': rev=%d altRev=%d tell=%d inlinedDirs=%d\n",
           Name(), d.rev, d.altRev, bs.Tell(), (int)mInlinedDirs.size());
#endif

    for (int i = mInlinedDirs.size() - 1; i >= 0; i--) {
        InlinedDir &iDir = mInlinedDirs[i];
        int tempRev = d.rev;
        iDir.dir.PostLoad(mLoader);
        d.rev = tempRev;
        if (iDir.mType == kInlineCachedShared) {
            iDir.shared = true;
        }
        if (iDir.shared) {
            FilePath &fp = iDir.file;
            DirLoader *last = DirLoader::FindLast(fp);
            if (last) {
                if (last->IsLoaded()) {
                    iDir.dir = last->GetDir();
                } else {
                    MILO_NOTIFY("Can't share unloaded dir %s", fp);
                }
            }
        } else {
            if (iDir.dir.IsLoaded()) {
                delete iDir.dir->mLoader;
                iDir.dir->mLoader = nullptr;
            }
        }
    }

#ifdef HX_NATIVE
    printf("ObjectDir::PostLoad: after inlinedDirs, tell=%d\n", bs.Tell());
#endif
    if (d.rev > 0x17) {
        int revs2 = bs.Cached() ? 0 : bs.PopRev(this);
        int offset = bs.PopRev(this);
#ifdef HX_NATIVE
        printf("ObjectDir::PostLoad: revs2=%d offset=%d mSubDirs.size=%d tell=%d\n",
               revs2, offset, (int)mSubDirs.size(), bs.Tell());
#endif
        MILO_ASSERT_RANGE_EQ(offset, 0, mSubDirs.size(), 0x466);
        if (revs2 != 2) {
            for (int i = mSubDirs.size() - offset - 1; i >= 0; i--) {
                bool bbb = false;
                if (revs2 == 1) {
                    bbb = bs.PopRev(this) != 0;
                }
                ObjDirPtr<ObjectDir> inlinedDirPtr = PostLoadInlined();
                ObjDirPtr<ObjectDir> &curDirPtr = mSubDirs[i + offset];
                if (revs2 == 0 || bbb) {
                    curDirPtr = inlinedDirPtr;
                }
                AddedSubDir(curDirPtr);
            }
            for (offset = offset - 1; offset >= 0; offset--) {
                ObjDirPtr<ObjectDir> &offsetPtr = mSubDirs[offset];
                offsetPtr.PostLoad(mLoader);
                AddedSubDir(offsetPtr);
            }
        }
    } else {
        for (int i = 0; i < mSubDirs.size(); i++) {
            ObjDirPtr<ObjectDir> &curDirPtr = mSubDirs[i];
            curDirPtr.PostLoad(mLoader);
            AddedSubDir(curDirPtr);
            if (curDirPtr.IsLoaded()) {
                if (curDirPtr->InlineSubDirType() != kInlineNever) {
                    delete curDirPtr->mLoader;
                    curDirPtr->mLoader = nullptr;
                }
            }
        }
    }

    if (d.rev > 10) {
        char buf[0x80];
        bs.ReadString(buf, 0x80);
#ifdef HX_NATIVE
        printf("ObjectDir::PostLoad: unk8c='%s' tell=%d\n", buf, bs.Tell());
#endif
        unk8c = FindObject(buf, false, true);
        bs.ReadString(buf, 0x80);
#ifdef HX_NATIVE
        printf("ObjectDir::PostLoad: mCurCam='%s' tell=%d\n", buf, bs.Tell());
#endif
        mCurCam = FindObject(buf, false, true);
    }

    if (d.rev > 0x15) {
#ifdef HX_NATIVE
        printf("ObjectDir::PostLoad: calling LoadRest tell=%d\n", bs.Tell());
#endif
        LoadRest(bs);
#ifdef HX_NATIVE
        printf("ObjectDir::PostLoad: LoadRest done tell=%d\n", bs.Tell());
#endif
    } else if (d.rev > 0x10) {
        Hmx::Object::Load(bs);
    }

    static Message change_proxies("change_proxies");
    HandleType(change_proxies);

    if (mProxyOverride) {
        mProxyOverride = false;
        if (!gLoadingProxyFromDisk
            && (!IsProxy()
                || (mInlineProxyType == kInlineCached || mInlineProxyType == kInlineAlways))) {
            MILO_FAIL("You cannot override an inlined proxy!");
        }
    } else if (IsProxy() && !mProxyFile.empty()) {
        DeleteObjects();
        DeleteSubDirs();
        DirLoader *dl = new DirLoader(
            mProxyFile,
            kLoadFront,
            nullptr,
            InlineProxy(bs) ? &bs : nullptr,
            this,
            false,
            nullptr
        );
    }
}
