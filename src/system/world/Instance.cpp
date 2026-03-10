#include "world/Instance.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "obj/DirLoader.h"
#include "obj/PropSync_p.h"
#include "rndobj/Dir.h"
#include "rndobj/Group.h"
#include "utl/MemMgr.h"

template <>
bool PropSync(
    ObjDirPtr<WorldInstance> &ptr, DataNode &node, DataArray *prop, int i, PropOp op
) {
    if (op == kPropGet) {
        DataNode tmp(ptr.GetFile());
        node = tmp;
    } else {
        const char *str = node.Str(NULL);
        FilePath fp(str);
        ptr.LoadFile(fp, false, true, kLoadFront, false);
    }
    return true;
}

#pragma region WorldInstance

WorldInstance::WorldInstance() : mSharedGroup(0), mSharedGroup2(0) {}

WorldInstance::~WorldInstance() {
    if (mSharedGroup2)
        mSharedGroup2->ClearPollMaster();
    delete mSharedGroup2;
}

BEGIN_HANDLERS(WorldInstance)
    HANDLE_SUPERCLASS(RndDir)
END_HANDLERS

BEGIN_PROPSYNCS(WorldInstance)
    SYNC_PROP_MODIFY(instance_file, mDir, SyncDir())
    SYNC_PROP_SET(shared_group, mSharedGroup ? mSharedGroup->Group() : NULL, )
    SYNC_PROP_SET(poll_master, mSharedGroup ? (mSharedGroup->PollMaster() == this) : 0, )
    SYNC_SUPERCLASS(RndDir)
END_PROPSYNCS

BEGIN_SAVES(WorldInstance)
    SAVE_REVS(3, 0)
    bs << mDir.GetFile();
    SaveInlined(mDir.GetFile(), true, kInlineCachedShared);
    SAVE_SUPERCLASS(RndDir)
    SavePersistentObjects(bs);
END_SAVES

BEGIN_COPYS(WorldInstance)
    COPY_SUPERCLASS(RndDir)
END_COPYS

void WorldInstance::PostSave(BinStream &bs) { SyncDir(); }

void WorldInstance::PreSave(BinStream &) {}

void WorldInstance::DrawShowing() {
    RndDir::DrawShowing();
    if (mSharedGroup) {
        mSharedGroup->Draw(WorldXfm());
    }
}

RndDrawable *WorldInstance::CollideShowing(const Segment &s, float &f, Plane &pl) {
    if (RndDir::CollideShowing(s, f, pl))
        return this;
    if (mSharedGroup) {
        if (mSharedGroup->Collide(WorldXfm(), s, f, pl)) {
            return this;
        }
    }
    return 0;
}

void WorldInstance::Poll() {
    if (mSharedGroup)
        mSharedGroup->TryPoll(this);
    RndDir::Poll();
}

void WorldInstance::Enter() {
    if (mSharedGroup)
        mSharedGroup->TryEnter(this);
    RndDir::Enter();
}

float WorldInstance::GetDistanceToPlane(const Plane &pl, Vector3 &v) {
    float dist = RndDir::GetDistanceToPlane(pl, v);
    if (mSharedGroup) {
        Vector3 v28;
        float grpdist = mSharedGroup->DistanceToPlane(WorldXfm(), pl, v28);
        if (dist > grpdist) {
            v = v28;
            dist = grpdist;
        }
    }
    return dist;
}

bool WorldInstance::MakeWorldSphere(Sphere &s, bool b) {
    if (b) {
        RndDir::MakeWorldSphere(s, true);
        if (mSharedGroup) {
            Sphere s28;
            mSharedGroup->MakeWorldSphere(WorldXfm(), s28);
            s.GrowToContain(s28);
        }
        return true;
    } else {
        if (mSphere.GetRadius()) {
            Multiply(mSphere, WorldXfm(), s);
            return true;
        } else
            return false;
    }
}

INIT_REVS(3, 0)

void WorldInstance::PreLoad(BinStream &bs) {
    if (IsProxy())
        DeleteObjects();
    LOAD_REVS(bs);
    ASSERT_REVS(3, 0);
    if (d.rev > 0) {
        FilePath fp;
        bs >> fp;
        PreLoadInlined(fp, true, kInlineCachedShared);
    } else
        bs >> mDir;

    RndDir::PreLoad(bs);
    if (mProxyFile.length() != 0) {
        MILO_NOTIFY(
            "WorldInstance %s was created as RndDir. Object needs to be deleted and recreated.",
            Name()
        );
    }
    bs.PushRev(packRevs(d.altRev, d.rev), this);
}

void WorldInstance::LoadPersistentObjects(BinStreamRev &bs) {
    if (IsProxy()) {
        if (bs.rev > 2) {
            // allocate more hashtable and stringtable space
            int hashSize, stringSize;
            bs >> hashSize;
            bs >> stringSize;
            hashSize *= 2;
            Reserve(hashSize, stringSize);
        }
        // create the persistent objects using their ClassName and Name
        // then push them into our persistent object list
        std::list<Hmx::Object *> objlist;
        int count;
        bs >> count;
        while (count-- != 0) {
            Symbol objClassName;
            bs >> objClassName;
            char objName[0x80];
            bs.stream.ReadString(objName, 0x80);

            if (!Hmx::Object::RegisteredFactory(objClassName)) {
                MILO_NOTIFY("%s: Can't make %s", mStoredFile.c_str(), objClassName);
                DeleteObjects();
                return;
            }

            Hmx::Object *obj = Hmx::Object::NewObject(objClassName);
            obj->SetName(objName, this);
            objlist.push_back(obj);
        }

        String dirNameStr;
        ObjectDir *dirDir = nullptr;
        DataArray *dirTypeDef = nullptr;
        ObjDirPtr<ObjectDir> subDir;
        if (mDir) {
            dirNameStr = mDir->Name();
            dirDir = mDir->Dir();
            dirTypeDef = (DataArray *)mDir->TypeDef();
            subDir = mDir;
            AppendSubDir(subDir);
        }
        while (!objlist.empty()) {
            Hmx::Object *cur = objlist.front();
            cur->PreLoad(bs.stream);
            cur->PostLoad(bs.stream);
            objlist.pop_front();
        }
        if (mDir) {
            RemoveSubDir(subDir);
            mDir->SetName(dirNameStr.c_str(), dirDir);
            mDir->SetTypeDef(dirTypeDef);
        }
    }
}

void WorldInstance::DeleteTransientObjects() {
    if (!Dir() || Dir() == DirLoader::TopSaveDir()
        || Dir()->InlineSubDirType() != kInlineAlways) {
        DeleteObjects();
    } else {
        for (ObjDirItr<Hmx::Object> obj(this, false); obj != nullptr; ++obj) {
            if (obj != this) {
                const ObjRef &refs = obj->Refs();
                ObjectDir *dir_ref = Dir();
                Hmx::Object *to = mDir->Find<Hmx::Object>(obj->Name(), true);
                MILO_ASSERT(obj->ClassName() == to->ClassName(), 0x1CB);
                {
                    MemDoTempAllocations m(true, false);
                    for (ObjRef::iterator it = refs.begin(); it != refs.end(); ++it) {
                        if ((*it).RefOwner() && (*it).RefOwner()->Dir() == this) {
                            (*it).Replace(to);
                        }
                    }
                }
                delete obj;
            }
        }
    }
}

void WorldInstance::SetProxyFile(const FilePath &fp, bool override) {
    MILO_ASSERT(!override, 0x246);
    DeleteObjects();
    mDir.LoadFile(fp, false, true, kLoadFront, false);
    SyncDir();
    if (mDir) {
        Hmx::Object::Copy(mDir, kCopyShallow);
    }
}

#pragma endregion WorldInstance
#pragma region SharedGroup

SharedGroup::SharedGroup(RndGroup *group) : mGroup(group), mPollMaster(this) {
    AddPolls(group);
}

void SharedGroup::ClearPollMaster() { mPollMaster = nullptr; }

void SharedGroup::TryPoll(WorldInstance *inst) {
    if (!mPollMaster)
        mPollMaster = inst;
    else if (mPollMaster != inst)
        return;
    FOREACH (it, mPolls) {
        (*it)->Poll();
    }
}

void SharedGroup::TryEnter(WorldInstance *inst) {
    if (!mPollMaster)
        mPollMaster = inst;
    else if (mPollMaster != inst)
        return;
    FOREACH (it, mPolls) {
        (*it)->Enter();
    }

    Hmx::Object *src = dynamic_cast<Hmx::Object *>(mPollMaster->Dir());
    if (src) {
        Hmx::Object *src2 = dynamic_cast<Hmx::Object *>(mGroup->Dir());
        if (src2)
            src2->ChainSource(src, 0);
    }
}

float SharedGroup::DistanceToPlane(const Transform &tf, const Plane &pl, Vector3 &v) {
    mGroup->SetWorldXfm(tf);
    return mGroup->GetDistanceToPlane(pl, v);
}

void SharedGroup::MakeWorldSphere(const Transform &tf, Sphere &s) {
    mGroup->SetWorldXfm(tf);
    mGroup->MakeWorldSphere(s, true);
}

bool SharedGroup::Collide(const Transform &tf, const Segment &s, float &f, Plane &pl) {
    mGroup->SetWorldXfm(tf);
    return mGroup->Collide(s, f, pl);
}

void SharedGroup::Draw(const Transform &tf) {
    mGroup->SetWorldXfm(tf);
    mGroup->Draw();
}

#pragma endregion SharedGroup
