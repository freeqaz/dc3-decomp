#include "char/CharClipSet.h"
#include "char/CharClipGroup.h"
#include "char/Character.h"
#include "char/CharBonesMeshes.h"
#include "char/CharUtl.h"
#include "char/CharForeTwist.h"
#include "char/CharUpperTwist.h"
#include "char/CharNeckTwist.h"
#include "char/CharPollable.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "utl/BinStream.h"

CharClipSet::CharClipSet()
    : mCharFilePath(), mPreviewChar(this), mPreviewClip(this), mStillClip(this) {
    ResetPreviewState();
    SetRate(k1_fpb);
}

CharClipSet::~CharClipSet() {}

BEGIN_PROPSYNCS(CharClipSet)
    SYNC_PROP(char_file_path, mCharFilePath)
    SYNC_PROP(preview_clip, mPreviewClip)
    SYNC_PROP(still_clip, mStillClip)
    SYNC_PROP(filter_flags, mFilterFlags)
    SYNC_PROP_SET(bpm, mBpm, SetBpm(_val.Int()))
    SYNC_PROP(preview_walk, mPreviewWalk)
    SYNC_SUPERCLASS(ObjectDir)
END_PROPSYNCS

BEGIN_SAVES(CharClipSet)
    SAVE_REVS(24, 0)
    SAVE_SUPERCLASS(ObjectDir)
    bs << mCharFilePath;
    bs << mPreviewClip;
    bs << mFilterFlags;
    bs << mBpm;
    bs << mPreviewWalk;
    bs << mStillClip;
END_SAVES

BEGIN_COPYS(CharClipSet)
    COPY_SUPERCLASS(ObjectDir)
    CREATE_COPY(CharClipSet)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mCharFilePath)
        COPY_MEMBER(mPreviewClip)
        COPY_MEMBER(mFilterFlags)
        COPY_MEMBER(mBpm)
        COPY_MEMBER(mPreviewWalk)
        COPY_MEMBER(mStillClip)
    END_COPYING_MEMBERS
END_COPYS

BEGIN_LOADS(CharClipSet)
    ObjectDir::Load(bs);
END_LOADS

void CharClipSet::PreSave(BinStream &bs) {
    if (mPreviewChar)
        mPreviewChar->SetName("", 0);
    if (bs.Cached()) {
        ResetPreviewState();
        ResetEditorState();
    }
}

void CharClipSet::PostSave(BinStream &bs) {
    ObjectDir::PostSave(bs);
    if (mPreviewChar) {
        mPreviewChar->SetName("preview_character", this);
        mPreviewChar->Enter();
    }
}

void CharClipSet::PreLoad(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(0x18, 0);
    MILO_ASSERT(d.rev > 3, 0x4E);
    ObjectDir::PreLoad(bs);
    bs.PushRev(packRevs(d.altRev, d.rev), this);
}

void CharClipSet::PostLoad(BinStream &) {}

void CharClipSet::SetFrame(float frame, float blend) {
    if (mPreviewClip && mPreviewChar) {
        RndAnimatable::SetFrame(frame, 1);
        CharBonesMeshes mesh1;
        CharBonesMeshes mesh2;
        mesh1.SetName("preview_anim", mPreviewChar);
        mPreviewClip->StuffBones(mesh1);
        mesh2.SetName("preview", this);
        mPreviewClip->StuffBones(mesh2);
        mesh2.Zero();
        mesh1.Zero();
        CharClip *relative = mPreviewClip->Relative();
        if (relative) {
            CharClip *theClip = mStillClip ? mStillClip : mPreviewClip;
            theClip->ScaleAdd(mesh1, 1, frame, 0);
            mPreviewClip->RotateTo(mesh1, 1, frame);
            theClip->ScaleAdd(mesh2, 1, frame, 0);
            mPreviewClip->RotateTo(mesh2, 1, frame);
        } else {
            mPreviewClip->ScaleAdd(mesh1, 1, frame, 0);
            mPreviewClip->ScaleAdd(mesh2, 1, frame, 0);
        }
        mesh1.PoseMeshes();
        mesh2.PoseMeshes();
        if (mPreviewWalk) {
            RndTransformable *pelvisTrans =
                CharUtlFindBoneTrans("bone_pelvis", mesh1.Dir());
            float *rotZPtr = (float *)mesh1.FindPtr("bone_facing.rotz");
            Vector3 *posPtr = (Vector3 *)mesh1.FindPtr("bone_facing.pos");
            if (pelvisTrans && posPtr && rotZPtr) {
                Transform &pelvisXfm = pelvisTrans->DirtyLocalXfm();
                if (rotZPtr) {
                    RotateAboutZ(pelvisXfm.m, *rotZPtr, pelvisXfm.m);
                    RotateAboutZ(pelvisXfm.v, *rotZPtr, pelvisXfm.v);
                    Normalize(pelvisXfm.m, pelvisXfm.m);
                }
                pelvisXfm.v += *posPtr;
            }
            for (ObjDirItr<CharBone> it(mPreviewClip->GetResource(), false);
                 it != nullptr;
                 ++it) {
                if (it->BakeOutAsTopLevel()) {
                    String str(it->Name());
                    if (str.find(".cb") != String::npos) {
                        str = str.substr(0, str.length() - 3);
                    }
                    RndTransformable *t = CharUtlFindBoneTrans(str.c_str(), mesh1.Dir());
                    if (t && posPtr && rotZPtr) {
                        Transform &xfm = t->DirtyLocalXfm();
                        if (rotZPtr) {
                            RotateAboutZ(xfm.m, *rotZPtr, xfm.m);
                            RotateAboutZ(xfm.v, *rotZPtr, xfm.v);
                            Normalize(xfm.m, xfm.m);
                        }
                        xfm.v += *posPtr;
                    }
                }
            }
        }

        for (ObjDirItr<CharPollable> it(mPreviewChar, true); it != nullptr; ++it) {
            if (dynamic_cast<CharForeTwist *>(&*it)
                || dynamic_cast<CharUpperTwist *>(&*it)
                || dynamic_cast<CharNeckTwist *>(&*it)) {
                it->Poll();
            }
        }
    }
}

float CharClipSet::StartFrame() {
    if (mPreviewClip)
        return mPreviewClip->StartBeat();
    else
        return 0;
}

float CharClipSet::EndFrame() {
    if (mPreviewClip)
        return mPreviewClip->EndBeat();
    else
        return 0;
}

void CharClipSet::ListDrawChildren(std::list<RndDrawable *> &draws) {
    RndDir *char_obj = mPreviewChar;
    if (char_obj != 0) {
        draws.insert(draws.end(), char_obj);
    }
}

void CharClipSet::ResetEditorState() {
    ResetPreviewState();
    ObjectDir::ResetEditorState();
}

void CharClipSet::SetBpm(int bpm) {
    static Symbol sBpm("bpm");
    mBpm = bpm;
}

void CharClipSet::ResetPreviewState() {
    delete mPreviewChar;
    mPreviewClip = 0;
    mStillClip = 0;
    mCharFilePath.SetRoot("");
    mFilterFlags = 0;
    mBpm = 90;
    mPreviewWalk = false;
}

void CharClipSet::SortGroups() {
    for (ObjDirItr<CharClipGroup> it(this, false); it != 0; ++it) {
        it->Sort();
    }
}

void CharClipSet::LoadCharacter() {
    MILO_ASSERT(TheLoadMgr.EditMode(), 0x14b);
    delete mPreviewChar;
    ObjectDir *loadedobj = DirLoader::LoadObjects(mCharFilePath, 0, 0);
    ObjectDir *dummy = dynamic_cast<RndDir *>(loadedobj);
    mPreviewChar = dynamic_cast<RndDir *>(dummy);
    Character *theChar = dynamic_cast<Character *>(dummy);
    if (mPreviewChar && !theChar) {
        for (ObjDirItr<Character> it(mPreviewChar, true); it != nullptr; ++it) {
            mPreviewChar = it;
            break;
        }
    }
    if (mPreviewChar) {
        mPreviewChar->Enter();
        mPreviewChar->SetName("preview_character", this);
    } else
        MILO_NOTIFY(
            "Preview character can only be loaded if the CharClipSet is the top-level directory."
        );
}

void CharClipSet::RecenterAll() { MILO_NOTIFY("You can only recenter clips from PC"); }

DataNode CharClipSet::OnListClips(DataArray *) {
    std::list<CharClip *> clips;
    for (ObjDirItr<CharClip> it(this, true); it != nullptr; ++it) {
        if ((mFilterFlags & it->Flags()) == mFilterFlags) {
            clips.push_back(it);
        }
    }
    clips.sort(ObjNameSort());
    DataArray *arr = new DataArray(clips.size() + 1);
    arr->Node(0) = NULL_OBJ;
    int idx = 1;
    for (std::list<CharClip *>::iterator it = clips.begin(); it != clips.end(); ++it) {
        arr->Node(idx++) = *it;
    }
    DataNode ret(arr, kDataArray);
    arr->Release();
    return ret;
}

BEGIN_HANDLERS(CharClipSet)
    HANDLE_ACTION(sort_groups, SortGroups())
    HANDLE_ACTION(recenter_all, RecenterAll())
    HANDLE_ACTION(load_character, LoadCharacter())
    HANDLE(list_clips, OnListClips)
    HANDLE_SUPERCLASS(ObjectDir)
END_HANDLERS
