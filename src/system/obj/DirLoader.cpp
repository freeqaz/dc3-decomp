#include "obj/DirLoader.h"
#include "Utl.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "obj/Dir.h"
#include "os/Archive.h"
#include "os/Debug.h"
#include "os/File.h"
#include "os/Platform.h"
#include "os/System.h"
#include "os/Timer.h"
#include "utl/BinStream.h"
#include "utl/ChunkStream.h"
#include "utl/FilePath.h"
#include "utl/Loader.h"
#include "utl/MemPoint.h"
#include "utl/MemTrack.h"
#include "utl/TextFileStream.h"
#include "utl/TextStream.h"
#include <map>

bool DirLoader::sPrintTimes;
bool DirLoader::sCacheMode;
ObjectDir *DirLoader::sTopSaveDir;
TextFileStream *DirLoader::sObjectMemDumpFile;
TextFileStream *DirLoader::sTypeMemDumpFile;
std::map<String, MemPointDelta> DirLoader::sMemPointMap;

DirLoader::DirLoader(
    const FilePath &fp,
    LoaderPos pos,
    Loader::Callback *cb,
    BinStream *stream,
    ObjectDir *dir,
    bool bbb,
    ObjectDir *dir2
)
    : Loader(fp, pos), mOwnStream(false), mStream(stream), mRev(0), mCounter(0),
      mObjects(nullptr, kObjListAllowNull), mCallback(cb), mDir(dir), mPostLoad(false),
      mLoadDir(true), mDeleteSelf(false), mProxyName(nullptr), mAccessed(0), mForceFailCallback(0),
      unk9a(0), mSubDir(bbb), unk9c(dir2), mProxyDir(this) {
    if (dir) {
        mDeleteSelf = true;
        mProxyName = dir->Name();
        mProxyDir = dir->Dir();
        mDir->SetLoader(this);
    }
    if (!stream && !dir && !bbb) {
        DataArray *arr = SystemConfig()->FindArray("force_milo_inline", false);
        if (arr) {
            for (int i = 1; i < arr->Size(); i++) {
                const char *str = arr->Str(i);
                if (FileMatch(fp.c_str(), str)) {
                    MILO_FAIL("Can't dynamically load milo files matching %s", str);
                }
            }
        }
    }
    if (fp.empty()) {
        mRoot = FilePath::Root();
    } else {
        const char *filePath = FileGetPath(mFile.c_str());
        char buf[256];
        strcpy(buf, filePath);
        int bufLen = strlen(buf);
        if (bufLen > 4 && streq("/gen", buf + bufLen - 4)) {
            buf[bufLen - 4] = '\0';
        }
        mRoot = FileMakePath(FileRoot(), buf);
    }
    mState = &DirLoader::OpenFile;
}

DirLoader::~DirLoader() {
    mDeleteSelf = false;
    if (!IsLoaded()) {
        Cleanup(nullptr);
    } else if (mDir) {
        mDir->SetLoader(nullptr);
        if (!mAccessed && !mProxyName) {
            RELEASE(mDir);
        }
    }
    mProxyDir = nullptr;
    if (mCallback && mForceFailCallback) {
        mCallback->FailedLoading(this);
        mCallback = 0;
    }
}

bool DirLoader::Replace(ObjRef *from, Hmx::Object *to) {
    if (from == &mProxyDir) {
        mProxyDir = nullptr;
        mProxyName = nullptr;
        delete this; // uhhh.
        return true;
    } else
        return false;
}

const char *DirLoader::DebugText() { return MakeString("DL: %s", mFile.c_str()); }
bool DirLoader::IsLoaded() const { return mState == &DirLoader::DoneLoading; }

const char *DirLoader::StateName() const {
    if (mState == &DirLoader::OpenFile)
        return "OpenFile";
    else if (mState == &DirLoader::LoadHeader)
        return "LoadHeader";
    else if (mState == &DirLoader::LoadDir)
        return "LoadDir";
    else if (mState == &DirLoader::LoadResources)
        return "LoadResources";
    else if (mState == &DirLoader::CreateObjects)
        return "CreateObjects";
    else if (mState == &DirLoader::LoadObjs)
        return "LoadObjs";
    else if (mState == &DirLoader::DoneLoading)
        return "DoneLoading";
    else
        return "INVALID";
}

void DirLoader::SetCacheMode(bool mode) { sCacheMode = mode; }

ObjectDir *DirLoader::GetDir() {
    MILO_ASSERT(IsLoaded(), 0x82);
    mAccessed = true;
    return mDir;
}

Symbol DirLoader::GetDirClass(const char *cc) {
    ChunkStream cs(cc, ChunkStream::kRead, 0x10000, true, kPlatformNone, false);
    if (cs.Fail()) {
        return Symbol("");
    } else {
        EofType t;
        while (t = cs.Eof(), t != NotEof) {
            MILO_ASSERT(t == TempEof, 0x199);
        }
        int i;
        cs >> i;
        Symbol s;
        cs >> s;
        return s;
    }
}

int DirLoader::ClassAndNameSort::ClassIndex(Hmx::Object *obj) {
    static DataArray *cfg = SystemConfig("system", "dir_sort");
    Symbol name = obj->ClassName();
    for (int i = cfg->Size() - 1; i != 0; i--) {
        DataNode &n = cfg->Node(i);
        if ((unsigned int)n.UncheckedInt() == name) {
            return i;
        }
    }
    return cfg->Size();
}

void WriteDeadAndMark(BinStream &bs) {
    bs << (unsigned char)0xAD << (unsigned char)0xDE << (unsigned char)0xAD
       << (unsigned char)0xDE;
    MarkChunk(bs);
}

const char *DirLoader::CachedPath(const char *cc, bool b) {
    const char *ext = FileGetExt(cc);
    if ((sCacheMode || b) && ext) {
        if (streq(ext, "milo")) {
            return MakeString(
                "%s/gen/%s.milo_%s",
                FileGetPath(cc),
                FileGetBase(cc),
                PlatformSymbol(TheLoadMgr.GetPlatform())
            );
        }
    }
    return cc;
}

bool DirLoader::ShouldBlockSubdirLoad(const FilePath &fp) {
    if (!fp.c_str())
        return false;
    if (!sPathEval)
        return false;
    return sPathEval(fp.c_str());
}

Symbol DirLoader::FixClassName(Symbol orig) {
    if (mRev >= 0x1C)
        goto ret;
    static Symbol CharClipSamples("CharClipSamples");
    static Symbol CharClip("CharClip");
    if (orig == CharClipSamples)
        orig = CharClip;

    if (mRev >= 0x1B)
        goto ret;
    static Symbol BandMeshLauncher("BandMeshLauncher");
    static Symbol PartLauncher("PartLauncher");
    if (orig == BandMeshLauncher)
        orig = PartLauncher;

    if (mRev >= 0x1A)
        goto ret;
    static Symbol P9TransDraw("P9TransDraw");
    static Symbol CharTransDraw("CharTransDraw");
    if (orig == P9TransDraw)
        orig = CharTransDraw;

    if (mRev >= 0x19)
        goto ret;
    static Symbol RenderedTex("RenderedTex");
    static Symbol TexRenderer("TexRenderer");
    static Symbol CompositeTexture("CompositeTexture");
    static Symbol LayerDir("LayerDir");
    if (orig == RenderedTex)
        orig = TexRenderer;
    else if (orig == CompositeTexture)
        orig = LayerDir;

    if (mRev >= 0x18)
        goto ret;
    static Symbol BandFx("BandFx");
    static Symbol WorldFx("WorldFx");
    if (orig == BandFx)
        return WorldFx;

    if (mRev >= 0x16)
        goto ret;
    static Symbol Slider("Slider");
    static Symbol BandSlider("BandSlider");
    if (orig == Slider)
        return BandSlider;

    if (mRev >= 0x15)
        goto ret;
    static Symbol TextEntry("TextEntry");
    static Symbol BandTextEntry("BandTextEntry");
    if (orig == TextEntry)
        return BandTextEntry;

    if (mRev >= 0x14)
        goto ret;
    static Symbol Placer("Placer");
    static Symbol BandPlacer("BandPlacer");
    if (orig == Placer)
        return BandPlacer;

    if (mRev >= 0x13)
        goto ret;
    static Symbol ButtonEx("ButtonEx");
    static Symbol BandButton("BandButton");
    if (orig == ButtonEx)
        return BandButton;

    static Symbol LabelEx("LabelEx");
    static Symbol BandLabel("BandLabel");
    if (orig == LabelEx)
        return BandLabel;

    static Symbol PictureEx("PictureEx");
    static Symbol BandPicture("BandPicture");
    if (orig == PictureEx)
        return BandPicture;

    if (mRev >= 0x12)
        goto ret;
    static Symbol UIPanel("UIPanel");
    static Symbol PanelDir("PanelDir");
    if (orig == UIPanel)
        return PanelDir;

    if (mRev >= 0x10)
        goto ret;
    static Symbol WorldInstance("WorldInstance");
    static Symbol WorldObject("WorldObject");
    if (orig == WorldInstance)
        return WorldObject;

    if (mRev >= 0xF)
        goto ret;
    static Symbol View("View");
    static Symbol Group("Group");
    if (orig == View)
        return Group;

    if (mRev >= 7)
        goto ret;
    static Symbol String("String");
    static Symbol Line("Line");
    if (orig == String)
        return Line;

    if (mRev >= 6)
        goto ret;
    static Symbol MeshGenerator("MeshGenerator");
    static Symbol Generator("Generator");
    if (orig == MeshGenerator)
        return Generator;

    if (mRev >= 5)
        goto ret;
    static Symbol TexMovie("TexMovie");
    static Symbol Movie("Movie");
    if (orig == TexMovie)
        return Movie;

ret:
    return orig;
}

void ReadDead(BinStream &bs) {
    unsigned char val;
#ifdef HX_NATIVE
    int startTell = bs.Tell();
#endif
    bs >> val;
    while (true) {
        if (val == 0xAD) {
            bs >> val;
            if (val == 0xDE) {
                bs >> val;
                if (val == 0xAD) {
                    bs >> val;
                    if (val == 0xDE) {
#ifdef HX_NATIVE
                        printf("ReadDead: found ADDEADDE at tell=%d->%d\n", startTell, bs.Tell());
                        fflush(stdout);
#endif
                        return;
                    }
                }
            }
        } else {
            bs >> val;
        }
    }
}

void ReadEditorDirDead(BinStream &bs) {
    unsigned char buf;
    for (unsigned int i = 0; i < 20; i++) {
        while (true) {
            EofType t;
            while ((t = bs.Eof()) != NotEof) {
                MILO_ASSERT(t == TempEof, 0x470);
            }
            bs >> buf;
            if (((const unsigned char *)"%#@EndOfEditorDir@#%")[i] == buf)
                break;
        }
    }
}

void DirLoader::DumpObjectMemDelta(
    const Hmx::Object *object, const MemPointDelta &memDelta
) const {
    MILO_ASSERT(object, 0x56A);
    MILO_ASSERT(sObjectMemDumpFile, 0x56B);
    if (memDelta.AnyGreaterThan(0)) {
        const char *name = object->Name();
        if (!name || !*name)
            name = "Unknown";
        const char *objPtrStr = MakeString("0x%X", (void *)object);
        *sObjectMemDumpFile << objPtrStr << "," << object->ClassName() << "," << name
                            << "," << mFile.c_str() << "," << memDelta.ToString(1)
                            << "\n";
    }
}

void SyncObjectsGlitchCB(float ms, void *v) {
    ObjectDir *dir = (ObjectDir *)v;
    const char *path = PathName(dir);
    MILO_LOG("%s %s SyncObjects took %.2f ms\n", dir->ClassName(), path, ms);
}

DirLoader *DirLoader::Find(const FilePath &fp) {
    if (!fp.empty()) {
        const std::list<Loader *> &ldrs = TheLoadMgr.Loaders();
        for (std::list<Loader *>::const_iterator it = ldrs.begin(); it != ldrs.end();
             ++it) {
            if ((*it)->LoaderFile() == fp) {
                DirLoader *dl = dynamic_cast<DirLoader *>(*it);
                if (dl)
                    return dl;
            }
        }
    }
    return nullptr;
}

bool DirLoader::ClassAndNameSort::operator()(Hmx::Object *o1, Hmx::Object *o2) {
    static Symbol ObjectDir("ObjectDir");
    bool o1sub = IsASubclass(o1->ClassName(), ObjectDir);
    if (o1sub != IsASubclass(o2->ClassName(), ObjectDir)) {
        return o1sub;
    } else {
        int idx1 = ClassIndex(o1);
        int idx2 = ClassIndex(o2);
        if (idx1 != idx2) {
            return idx1 < idx2;
        } else
            return strcmp(o1->Name(), o2->Name()) < 0;
    }
    return false;
}

DirLoader *DirLoader::FindLast(const FilePath &fp) {
    if (!fp.empty()) {
        const std::list<Loader *> &ldrs = TheLoadMgr.Loaders();
        for (std::list<Loader *>::const_reverse_iterator it = ldrs.rbegin();
             it != ldrs.rend();
             ++it) {
            if ((*it)->LoaderFile() == fp) {
                DirLoader *dl = dynamic_cast<DirLoader *>(*it);
                if (dl)
                    return dl;
            }
        }
    }
    return nullptr;
}

void DirLoader::WriteTypeMemDump(TextFileStream *file) {
    MILO_ASSERT(file, 0x5B1);
    *file << "Class," << MemPointDelta::HeaderString("") << "\n";
    for (std::map<String, MemPointDelta>::iterator it = sMemPointMap.begin();
         it != sMemPointMap.end();
         ++it) {
        MemPointDelta pt = it->second;
        file->Print(it->first.c_str());
        *file << "," << pt.ToString(1) << "\n";
    }
    file->File().Flush();
}

void DirLoader::Cleanup(const char *str) {
    if (str) {
        MILO_NOTIFY(str);
    }
    mObjects.clear();
    if (mOwnStream)
        RELEASE(mStream);
    if (mDir) {
        if (!IsLoaded()) {
            mDir->SetLoader(nullptr);
            if (!mProxyName) {
                RELEASE(mDir);
            }
        }
        if (mProxyName) {
            if (mDir->Dir() == mDir) {
                mDir->SetName(mProxyName, mProxyDir);
            }
        }
        if (IsLoaded() && mDir) {
            AutoGlitchReport report(50.0f, SyncObjectsGlitchCB, mDir);
            mDir->SetSubDirFlag(mSubDir);
            mDir->SyncObjects();
            mDir->SetSubDirFlag(false);
        }
    }
    mState = &DirLoader::DoneLoading;
    mTimer.Stop();
    if (sPrintTimes) {
        MILO_LOG("%s: %f ms\n", mFile, mTimer.Ms());
    }
    if (mCallback && (str || mForceFailCallback)) {
        mCallback->FailedLoading(this);
        mCallback = nullptr;
    }
    if (mDeleteSelf) {
        delete this;
    }
}

void DirLoader::AddTypeObjectMemDelta(
    const Hmx::Object *object, const MemPointDelta &memDelta
) const {
    MILO_ASSERT(object, 0x584);
    MILO_ASSERT(sTypeMemDumpFile, 0x585);
    if (memDelta.AnyGreaterThan(0)) {
        const char *name = object->ClassName().Str();
        if (!name || !*name)
            name = "Unknown";
        std::map<String, MemPointDelta>::iterator it = sMemPointMap.find(name);
        MemPointDelta *target;
        if (it != sMemPointMap.end()) {
            target = &it->second;
        } else {
            std::pair<std::map<String, MemPointDelta>::iterator, bool> result =
                sMemPointMap.insert(std::pair<String, MemPointDelta>(name, MemPointDelta()));
            target = &result.first->second;
        }
        *target += memDelta;
    }
}

void DirLoader::SaveObjects(BinStream &bs, ObjectDir *dir) {
    char name[256];
    MILO_ASSERT(sTopSaveDir != dir, 0x10C);
    if (!sTopSaveDir)
        sTopSaveDir = dir;
    ObjectDir *parentDir = dir->Dir();
    strcpy(name, dir->Name());
    if (parentDir != dir) {
        dir->SetName(NextName(dir->Name(), dir), dir);
    }
    int hashSize = dir->HashTableUsedSize();
    int strSize = dir->StrTableUsedSize();
    for (ObjDirItr<Hmx::Object> it(dir, false); it != nullptr; ++it) {
        if (it != dir) {
            it->PreSave(bs);
        }
    }
    dir->PreSave(bs);
    bs << 0x20;
    bs << dir->ClassName() << dir->Name();
    bs << hashSize * 2;
    bs << strSize;
    bs << false;
    std::list<Hmx::Object *> objects;
    for (ObjDirItr<Hmx::Object> it(dir, false); it != nullptr; ++it) {
        if (it != dir) {
            objects.push_back(it);
        }
    }
    auto _tmp2 = ClassAndNameSort();
    objects.sort(_tmp2);
    bs << objects.size();
    for (std::list<Hmx::Object *>::const_iterator it = objects.begin();
         it != objects.end();
         it++) {
        bs << (*it)->ClassName() << (*it)->Name();
    }
    SetActiveChunkObject(dir);
    dir->Save(bs);
    WriteDeadAndMark(bs);
    for (std::list<Hmx::Object *>::const_iterator it = objects.begin();
         it != objects.end();
         it++) {
        SetActiveChunkObject(*it);
        (*it)->Save(bs);
        WriteDeadAndMark(bs);
        ObjectDir *proxy = dynamic_cast<ObjectDir *>(*it);
        if (proxy)
            proxy->SaveProxy(bs);
    }
    if (!bs.Cached()) {
        dir->PostSave(bs);
        for (std::list<Hmx::Object *>::const_iterator it = objects.begin();
             it != objects.end();
             it++) {
            (*it)->PostSave(bs);
        }
    }
    if (parentDir != dir) {
        dir->SetName(name, dir);
    }
    if (sTopSaveDir == dir) {
        sTopSaveDir = nullptr;
    }
}

bool DirLoader::SaveObjects(const char *file, ObjectDir *dir, bool) {
    if (sCacheMode && dir->InlineSubDirType() != kInlineNever) {
        MILO_LOG("Not caching %s because it is an inlined subdir.\n", file);
        return false;
    } else {
        FilePathTracker tracker(FileGetPath(file));
        file = CachedPath(file, false);
        if (sCacheMode) {
            FileMkDir(FileGetPath(file));
        }
        Platform p = sCacheMode ? TheLoadMgr.GetPlatform() : kPlatformPC;
        MILO_ASSERT(p != kPlatformNone, 0x1B3);
        bool noNulls = !gNullFiles;
        ChunkStream cs(
            file,
            ChunkStream::kWrite,
            p == kPlatformWii ? 0x10000 : 0x20000,
            noNulls,
            p,
            sCacheMode
        );
        if (cs.Fail()) {
            MILO_NOTIFY("Could not open file: %s", file);
            return false;
        } else {
            SaveObjects(cs, dir);
            return true;
        }
    }
}

bool DirLoader::SetupDir(Symbol sym) {
    MemPoint begin(MemPoint::kInitType0);
    if (sObjectMemDumpFile || sTypeMemDumpFile) {
        begin = MemPoint(MemPoint::kInitType1);
    }
    if (mDir) {
        if (mDir->ClassName() != sym) {
            if (mDir != mDir->Dir()) {
                MILO_NOTIFY(
                    "%s: Proxy %s class %s not %s, converting",
                    PathName(mDir->Dir()),
                    mFile.c_str(),
                    mDir->ClassName(),
                    sym
                );
            } else {
                MILO_NOTIFY(
                    "%s: Proxy class %s not %s, converting",
                    mFile.c_str(),
                    mDir->ClassName(),
                    sym
                );
            }
            ObjectDir *newDir =
                dynamic_cast<ObjectDir *>(Hmx::Object::NewObject(sym));
            if (!newDir) {
                Cleanup(MakeString(
                    "%s: Trying to make non ObjectDir proxy class %s %s",
                    mFile.c_str(),
                    mDir->ClassName(),
                    sym
                ));
                return false;
            }
            newDir->TransferLoaderState(mDir);
            ReplaceObject(mDir, newDir, true, true, false);
            mDir = newDir;
        }
    } else {
        mDir = dynamic_cast<ObjectDir *>(Hmx::Object::NewObject(sym));
    }
    mDir->SetPathName(mFile.c_str());
    if (sObjectMemDumpFile) {
        MemPoint end(MemPoint::kInitType1);
        DumpObjectMemDelta(mDir, end - begin);
    }
    if (sTypeMemDumpFile) {
        MemPoint end(MemPoint::kInitType1);
        AddTypeObjectMemDelta(mDir, end - begin);
    }
    return true;
}

void DirLoader::LoadObjs() {
    FilePathTracker tracker(mRoot.c_str());
    EofType t;
#ifdef HX_NATIVE
    printf("DirLoader::LoadObjs '%s': mObjects.size()=%d tell=%d\n",
           mFile.c_str(), (int)mObjects.size(), mStream->Tell());
    fflush(stdout);
#endif
    while (!mObjects.empty()) {
        t = mStream->Eof();
        if (t != NotEof) {
            MILO_ASSERT(t == TempEof, 0x4C0);
        } else {
            Hmx::Object *obj = mObjects.front();
            if (obj) {
                if (!mPostLoad) {
                    MemPoint begin(MemPoint::kInitType0);
                    if (sObjectMemDumpFile || sTypeMemDumpFile) {
                        begin = MemPoint(MemPoint::kInitType1);
                    }
                    const char *name = obj->Name();
                    if (!strcmp(name, ""))
                        name = mProxyName;
                    BeginMemTrackObjectName(name);
                    if (mDir) {
                        BeginMemTrackFileName(mDir->GetPathName());
                    }
                    obj->PreLoad(*mStream);
                    mPostLoad = true;
                    if (sObjectMemDumpFile) {
                        MemPoint end(MemPoint::kInitType1);
                        DumpObjectMemDelta(obj, end - begin);
                    }
                    if (sTypeMemDumpFile) {
                        MemPoint end(MemPoint::kInitType1);
                        AddTypeObjectMemDelta(obj, end - begin);
                    }
                    EndMemTrackFileName();
                    EndMemTrackObjectName();
                }
                std::list<Loader *> &loaders = TheLoadMgr.Loading();
                Loader *firstLoader = loaders.empty() ? nullptr : loaders.front();
                if (firstLoader != this) {
                    return;
                }
                MemPoint begin(MemPoint::kInitType0);
                if (sObjectMemDumpFile || sTypeMemDumpFile) {
                    begin = MemPoint(MemPoint::kInitType1);
                }
                const char *name = obj->Name();
                if (!strcmp(name, ""))
                    name = mProxyName;
                BeginMemTrackObjectName(name);
                if (mDir) {
                    BeginMemTrackFileName(mDir->GetPathName());
                }
                obj->PostLoad(*mStream);
                mPostLoad = false;
                if (sObjectMemDumpFile) {
                    MemPoint end(MemPoint::kInitType1);
                    DumpObjectMemDelta(obj, end - begin);
                }
                if (sTypeMemDumpFile) {
                    MemPoint end(MemPoint::kInitType1);
                    AddTypeObjectMemDelta(obj, end - begin);
                }
                EndMemTrackFileName();
                EndMemTrackObjectName();
                if (mRev > 1) {
#ifdef HX_NATIVE
                    printf("DirLoader::LoadObjs '%s': ReadDead before tell=%d (obj='%s')\n",
                           mFile.c_str(), mStream->Tell(), obj->Name());
                    fflush(stdout);
#endif
                    ReadDead(*mStream);
#ifdef HX_NATIVE
                    printf("DirLoader::LoadObjs '%s': ReadDead after tell=%d\n",
                           mFile.c_str(), mStream->Tell());
                    fflush(stdout);
#endif
                }
            } else {
#ifdef HX_NATIVE
                printf("DirLoader::LoadObjs '%s': null obj, ReadDead tell=%d\n",
                       mFile.c_str(), mStream->Tell());
                fflush(stdout);
#endif
                MILO_ASSERT(mRev > 1, 0x507);
                ReadDead(*mStream);
            }
            mObjects.pop_front();
        }
        if (TheLoadMgr.CheckSplit()) {
            return;
        }
        std::list<Loader *> &loaders = TheLoadMgr.Loading();
        Loader *firstLoader = loaders.empty() ? nullptr : loaders.front();
        if (firstLoader != this)
            return;
    }
#ifdef HX_NATIVE
    printf("DirLoader::LoadObjs '%s': all objects done, transitioning to DoneLoading tell=%d\n",
           mFile.c_str(), mStream->Tell());
    fflush(stdout);
#endif
    mState = &DirLoader::DoneLoading;
    if (mRev > 0x1d) {
        if (mRev == 0x1e) {
            t = mStream->Eof();
            MILO_ASSERT(t == TempEof, 0x524);
            if (mStream->Eof() == NotEof) {
                ReadDead(*mStream);
            }
        } else if (mRev == 0x1f) {
            ReadEditorDirDead(*mStream);
        }
        if (unk9a && mRev > 0x1f) {
            ReadEditorDirDead(*mStream);
        }
    }
    Cleanup(nullptr);
    std::list<Loader *> &loaders = TheLoadMgr.Loading();
    Loader *firstLoader = loaders.empty() ? nullptr : loaders.front();
    if (firstLoader != this)
        return;
    if (mCallback)
        mCallback->FinishLoading(this);
}

void DirLoader::LoadDir() {
    if (mLoadDir) {
        FilePathTracker tracker(mRoot.c_str());
        bool oldproxy = gLoadingProxyFromDisk;
        gLoadingProxyFromDisk = (bool)mProxyName;
        if (!mPostLoad) {
#ifdef HX_NATIVE
            printf("DirLoader::LoadDir: calling PreLoad tell=%d\n", mStream->Tell());
#endif
            MemPoint begin(MemPoint::kInitType0);
            if (sObjectMemDumpFile || sTypeMemDumpFile) {
                begin = MemPoint(MemPoint::kInitType1);
            }
            mDir->PreLoad(*mStream);
            mPostLoad = true;
            if (sObjectMemDumpFile) {
                MemPoint end(MemPoint::kInitType1);
                DumpObjectMemDelta(mDir, end - begin);
            }
            if (sTypeMemDumpFile) {
                MemPoint end(MemPoint::kInitType1);
                AddTypeObjectMemDelta(mDir, end - begin);
            }
        }
        EofType t = TempEof;
        std::list<Loader *> &loaders = TheLoadMgr.Loading();
        Loader *firstLoader = loaders.empty() ? nullptr : loaders.front();
        if (firstLoader != this || (t = mStream->Eof(), t != NotEof)) {
            MILO_ASSERT(t == TempEof, 0x49F);
            gLoadingProxyFromDisk = oldproxy;
            return;
        }
        MemPoint begin(MemPoint::kInitType0);
        if (sObjectMemDumpFile || sTypeMemDumpFile) {
            begin = MemPoint(MemPoint::kInitType1);
        }
#ifdef HX_NATIVE
        printf("DirLoader::LoadDir: calling PostLoad tell=%d\n", mStream->Tell());
#endif
        mDir->PostLoad(*mStream);
#ifdef HX_NATIVE
        printf("DirLoader::LoadDir: PostLoad done tell=%d\n", mStream->Tell());
#endif
        gLoadingProxyFromDisk = oldproxy;
        mPostLoad = false;
        if (sObjectMemDumpFile) {
            MemPoint end(MemPoint::kInitType1);
            DumpObjectMemDelta(mDir, end - begin);
        }
        if (sTypeMemDumpFile) {
            MemPoint end(MemPoint::kInitType1);
            AddTypeObjectMemDelta(mDir, end - begin);
        }
    }
    ReadDead(*mStream);
    mState = &DirLoader::LoadObjs;
}

void DirLoader::LoadResources() {
    if (mCounter-- != 0) {
        FilePathTracker fpt(mRoot.c_str());
        FilePath fp2;
        *mStream >> fp2;
        if (!fp2.empty()) {
            TheLoadMgr.AddLoader(fp2, kLoadFront);
        }
    } else {
        if (mRev > 0xD)
            mState = &DirLoader::LoadDir;
        else
            mState = &DirLoader::LoadObjs;
    }
}

void DirLoader::CreateObjects() {
    while (mCounter-- != 0) {
        Hmx::Object *obj = nullptr;
        Symbol classSym;
        *mStream >> classSym;
        classSym = FixClassName(classSym);
        char buf[0x80];
        mStream->ReadString(buf, 0x80);
        bool b8;
        if (mRev > 0 && mRev < 8) {
            *mStream >> b8;
        }
#ifdef HX_NATIVE
        printf("DirLoader::CreateObjects: class='%s' name='%s'\n", classSym.Str(), buf);
        fflush(stdout);
#endif
        if (!Hmx::Object::RegisteredFactory(classSym)) {
            MILO_NOTIFY("%s: Can't make %s", mFile.c_str(), classSym);
            goto release_obj;
        } else {
            MemPoint begin(MemPoint::kInitType0);
            if (sObjectMemDumpFile || sTypeMemDumpFile) {
                begin = MemPoint(MemPoint::kInitType1);
            }
            BeginMemTrackObjectName(buf);
            obj = Hmx::Object::NewObject(classSym);
            EndMemTrackObjectName();
            if (mRev == 0x16 && dynamic_cast<ObjectDir *>(obj)) {
            release_obj:
                RELEASE(obj);
            } else {
                obj->SetName(buf, mDir);
            }
            if (sObjectMemDumpFile) {
                MemPoint end(MemPoint::kInitType1);
                DumpObjectMemDelta(obj, end - begin);
            }
            if (sTypeMemDumpFile) {
                MemPoint end(MemPoint::kInitType1);
                AddTypeObjectMemDelta(obj, end - begin);
            }
        }
        mObjects.push_back(obj);
        if (TheLoadMgr.CheckSplit())
            return;
    }
    if (mRev > 16) {
        mState = &DirLoader::LoadDir;
    } else {
        *mStream >> mCounter;
        mState = &DirLoader::LoadResources;
    }
}

void DirLoader::LoadHeader() {
    EofType t;
    while (t = mStream->Eof(), t != NotEof) {
        if (t != TempEof) {
            Cleanup(MakeString(
                "%s: Unexpected end of file. Proceeding as if file were empty.",
                mStream->Name()
            ));
            return;
        }
        if (TheLoadMgr.CheckSplit())
            return;
    }
    *mStream >> mRev;
#ifdef HX_NATIVE
    printf("DirLoader::LoadHeader '%s': mRev=%d (0x%x) tell=%d\n", mFile.c_str(), mRev, mRev, mStream->Tell());
#endif
    if (mRev < 7) {
        Cleanup(MakeString("Can't load old ObjectDir %s", mFile));
        return;
    }
    Symbol dirSym("RndDir");
    if (!Hmx::Object::RegisteredFactory(dirSym)) {
        dirSym = "ObjectDir";
    }
    if (mRev > 0xD) {
        Symbol symRead;
        *mStream >> symRead;
        symRead = FixClassName(symRead);
        char buf[0x80];
        mStream->ReadString(buf, 0x80);
#ifdef HX_NATIVE
        printf("DirLoader::LoadHeader: dirClass='%s' dirName='%s' tell=%d\n", symRead.Str(), buf, mStream->Tell());
#endif
        if (!Hmx::Object::RegisteredFactory(symRead)) {
            MILO_NOTIFY(
                "%s: %s not registered, defaulting to %s",
                mFile.c_str(),
                symRead,
                dirSym
            );
            symRead = dirSym;
            mLoadDir = false;
        }
        if (!SetupDir(symRead))
            return;
        int size1, size2;
        *mStream >> size1 >> size2;
#ifdef HX_NATIVE
        printf("DirLoader::LoadHeader: size1=%d size2=%d tell=%d\n", size1, size2, mStream->Tell());
#endif
        size1 += mDir->HashTableUsedSize() + 0x10;
        size2 += mDir->StrTableUsedSize() + 0x98;
        mDir->Reserve(size1, size2);
        bool &unk9aRef = unk9a;
        unk9aRef = false;
        if (mRev > 0x1c) {
            *mStream >> unk9aRef;
        }
        mDir->SetName(buf, mDir);
    } else if (mRev > 0xC) {
        Symbol sa8;
        *mStream >> sa8;
        if (!SetupDir("ObjectDir"))
            return;
        mDir->SetName(FileGetBase(mFile.c_str()), mDir);
        mDir->ObjectDir::Load(*mStream);
    } else {
        if (!SetupDir(dirSym))
            return;
        mDir->SetName(FileGetBase(mFile.c_str()), mDir);
    }
    mDir->SetLoader(this);
    *mStream >> mCounter;
#ifdef HX_NATIVE
    printf("DirLoader::LoadHeader: counter=%d tell=%d\n", mCounter, mStream->Tell());
#endif
    if (mRev < 0xE) {
        mDir->Reserve(mCounter * 2, mCounter * 25);
    }
    mState = &DirLoader::CreateObjects;
}

void DirLoader::OpenFile() {
    mTimer.Start();
    if (mStream == nullptr) {
        Archive *theArchive = TheArchive;
        bool using_cd = UsingCD();
        bool cache_mode = sCacheMode;
        const char *fileStr = mFile.c_str();
        bool matches = gHostFile && FileMatch(fileStr, gHostFile);
        if (matches) {
            SetCacheMode(gHostCached);
            SetUsingCD(false);
            TheArchive = nullptr;
        }
        const char *path = CachedPath(fileStr, false);
        mStream =
            new ChunkStream(path, ChunkStream::kRead, 0x10000, true, kPlatformNone, false);
        mOwnStream = true;
        if (matches) {
            SetCacheMode(cache_mode);
            SetUsingCD(using_cd);
            TheArchive = theArchive;
        }
        if (mStream->Fail()) {
            if (mProxyDir) {
                Cleanup(
                    MakeString("%s: could not load: %s", PathName(mProxyDir.Ptr()), path)
                );
            } else {
                Cleanup(MakeString("Could not load: %s", path));
            }
            return;
        }
    }
    mState = &DirLoader::LoadHeader;
}

ObjectDir *DirLoader::LoadObjects(const FilePath &fp, Callback *cb, BinStream *bs) {
    if (sTypeMemDumpFile) {
        sMemPointMap.clear();
    }
    DirLoader dirLoader(fp, kLoadFront, cb, bs, nullptr, false, nullptr);
    TheLoadMgr.PollUntilLoaded(&dirLoader, nullptr);
    if (sTypeMemDumpFile) {
        WriteTypeMemDump(sTypeMemDumpFile);
    }
    return dirLoader.GetDir();
}
