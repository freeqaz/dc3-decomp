#include "char/Character.h"
#include "math/Mtx.h"
#include "math/Rand.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/System.h"
#include "os/Timer.h"
#include "rndobj/Cam.h"
#include "rndobj/Draw.h"
#include "rndobj/Env.h"
#include "rndobj/Mat.h"
#include "rndobj/MultiMesh.h"
#include "rndobj/Poll.h"
#include "rndobj/Rnd.h"
#include "rndobj/Tex.h"
#include "rndobj/Trans.h"
#include "rndobj/Utl.h"
#include "utl/BinStream.h"
#include "utl/Loader.h"
#include "world/ColorPalette.h"
#include "world/Crowd.h"
#include "world/Crowd3DCharHandle.h"

RndTex *gImpostorTex[kNumLods];
RndCam *gImpostorCamera;
RndMat *gImpostorMat;
int gNumCrowd;
WorldCrowd *gParent;

const Hmx::Color &ColorPalette::GetColor(int idx) const {
    MILO_ASSERT(mColors.size(), 0x18);
    int colorIdx = idx % mColors.size();
    return mColors[colorIdx];
}

#pragma region CharDef

void WorldCrowd::CharDef::Save(BinStream &bs) const {
    bs << mChar;
    bs << mHeight;
    bs << mDensity;
    bs << mRadius;
    bs << mUseRandomColor;
}

void WorldCrowd::CharDef::Load(BinStreamRev &d) {
    d >> mChar;
    d >> mHeight;
    d >> mDensity;
    if (d.rev > 1) {
        d >> mRadius;
    }
    if (d.rev > 8) {
        d >> mUseRandomColor;
    }
}

#pragma endregion
#pragma region CharData

void WorldCrowd::CharData::Save(BinStream &bs) const { mDef.Save(bs); }

BinStream &operator<<(BinStream &bs, const WorldCrowd::CharData &cd) {
    cd.Save(bs);
    return bs;
}

BinStreamRev &operator>>(BinStreamRev &d, WorldCrowd::CharData &cd) {
    cd.mDef.Load(d);
    return d;
}

#pragma endregion
#pragma region WorldCrowd

WorldCrowd::WorldCrowd()
    : mPlacementMesh(this), mCharacters(this), mNum(0), mCrowdRotate((CrowdRotate)0), mForce3DCrowd(0),
      mShow3DOnly(0), mCharFullness(1), mFlatFullness(1), mLod(0), mEnviron(this),
      mEnviron3D(this), mFocus(this), mCharForceLod(kLODPerFrame), unkd0(0),
      mModifyStamp(0) {
    if (gNumCrowd++ == 0) {
        int w, h, bpp;
        if (GetGfxMode() == kNewGfx) {
            w = 256;
            h = 512;
            bpp = 32;
        } else {
            w = 128;
            h = 256;
            bpp = 16;
        }
        for (int i = 0; i < kNumLods; i++) {
            gImpostorTex[i] = Hmx::Object::New<RndTex>();
            gImpostorTex[i]->SetBitmap(w, h, bpp, RndTex::kRendered, true, nullptr);
        }
        RELEASE(gImpostorMat);
        RndMat *mat = Hmx::Object::New<RndMat>();
        gImpostorMat = mat;
        mat->SetUseEnv(true);
        mat->SetPreLit(false);
        mat->SetBlend(RndMat::kBlendSrc);
        mat->SetZMode(kZModeNormal);
        mat->SetAlphaCut(true);
        mat->SetAlphaThreshold(0x80);
        mat->SetTexWrap(kTexWrapClamp);
        mat->SetPerPixelLit(false);
        mat->SetPointLights(true);
        CreateAndSetMetaMat(mat);
        gImpostorCamera = Hmx::Object::New<RndCam>();
        SetMatAndCameraLod();
    }
}

WorldCrowd::~WorldCrowd() {
    Delete3DCrowdHandles();
    for (ObjList<CharData>::iterator it = mCharacters.begin(); it != mCharacters.end();
         ++it) {
        if (it->mMMesh) {
            delete it->mMMesh->Mesh();
            RELEASE(it->mMMesh);
        }
    }
    gNumCrowd--;
    if (gNumCrowd == 0) {
        for (int i = 0; i < kNumLods; i++) {
            RELEASE(gImpostorTex[i]);
        }
        RELEASE(gImpostorCamera);
        RELEASE(gImpostorMat);
    }
}

DataNode WorldCrowd::OnRebuild(DataArray *) { return 0; }

BEGIN_HANDLERS(WorldCrowd)
    HANDLE(rebuild, OnRebuild)
    HANDLE_ACTION(assign_random_colors, AssignRandomColors(true))
    HANDLE(iterate_frac, OnIterateFrac)
    HANDLE_ACTION(set_fullness, SetFullness(_msg->Float(2), _msg->Float(3)))
    HANDLE_SUPERCLASS(RndDrawable)
    HANDLE_SUPERCLASS(RndPollable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_CUSTOM_PROPSYNC(WorldCrowd::CharData)
    SYNC_PROP(character, o.mDef.mChar)
    SYNC_PROP(height, o.mDef.mHeight)
    SYNC_PROP(density, o.mDef.mDensity)
    SYNC_PROP(radius, o.mDef.mRadius)
END_CUSTOM_PROPSYNC

BEGIN_PROPSYNCS(WorldCrowd)
    gParent = this;
    SYNC_PROP(num, mNum)
    SYNC_PROP(placement_mesh, mPlacementMesh)
    SYNC_PROP(characters, mCharacters)
    SYNC_PROP(show_3d_only, mShow3DOnly)
    SYNC_PROP(environ, mEnviron)
    SYNC_PROP(environ_3d, mEnviron3D)
    SYNC_PROP_SET(lod, mLod, SetLod(_val.Int()))
    SYNC_PROP_SET(force_3D_crowd, mForce3DCrowd, Force3DCrowd(_val.Int()))
    SYNC_PROP(focus, mFocus)
    SYNC_PROP(char_force_lod, (int &)mCharForceLod)
    SYNC_SUPERCLASS(RndDrawable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

void WorldCrowd::SetLod(int lod) { mLod = Clamp(0, 2, lod); }

BEGIN_SAVES(WorldCrowd)
    SAVE_REVS(0x10, 0)
    SAVE_SUPERCLASS(RndDrawable)
    bool force = mForce3DCrowd;
    Force3DCrowd(false);
    bs << mPlacementMesh << mNum << mCharacters << mEnviron;
    bs << mEnviron3D;
    FOREACH (it, mCharacters) {
        std::list<Transform> transforms;
        RndMultiMesh *mesh = it->mMMesh;
        if (mesh) {
            FOREACH (t, mesh->Instances()) {
                transforms.push_back(t->mXfm);
            }
        }
        bs << transforms;
    }
    bs << mModifyStamp;
    bs << force;
    bs << mShow3DOnly;
    bs << mFocus;
    bs << mCharForceLod;
    bs << unkd0;
    Force3DCrowd(force);
    SAVE_SUPERCLASS(RndPollable)
END_SAVES

BEGIN_COPYS(WorldCrowd)
    COPY_SUPERCLASS(RndDrawable)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(WorldCrowd)
    BEGIN_COPYING_MEMBERS
        Delete3DCrowdHandles();
        COPY_MEMBER(mPlacementMesh)
        COPY_MEMBER(mNum)
        COPY_MEMBER(mCenter)
        COPY_MEMBER(mCharFullness)
        COPY_MEMBER(mFlatFullness)
        COPY_MEMBER(mLod)
        COPY_MEMBER(mEnviron)
        COPY_MEMBER(mEnviron3D)
        COPY_MEMBER(mForce3DCrowd)
        COPY_MEMBER(mShow3DOnly)
        COPY_MEMBER(mFocus)
        COPY_MEMBER(mCharForceLod)
        COPY_MEMBER(unkd0)

        mCharacters.clear();
        mCharacters.resize(c->mCharacters.size());
        ObjList<CharData>::const_iterator j = c->mCharacters.begin();
        ObjList<CharData>::iterator i = mCharacters.begin();
        for (; i != mCharacters.end(); ++i, ++j) {
            i->mDef = j->mDef;
            i->mBackup = j->mBackup;
            i->m3DChars = j->m3DChars;
            i->m3DCharsCreated = j->m3DCharsCreated;
        }
        CreateMeshes();
        j = c->mCharacters.begin();
        for (ObjList<CharData>::iterator i = mCharacters.begin(); i != mCharacters.end();
             ++i, ++j) {
            if (i->mMMesh) {
                MILO_ASSERT(j->mMMesh, 0x1DD);
                i->mMMesh->Instances() = j->mMMesh->Instances();
            }
        }
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(0x10, 0)

BEGIN_LOADS(WorldCrowd)
    LOAD_REVS(bs)
    ASSERT_REVS(0x10, 0)
    LOAD_SUPERCLASS(RndDrawable)
    Reset3DCrowd();
    d >> mPlacementMesh;
    if (d.rev < 3) {
        int x;
        d >> x;
    }
    d >> mNum;
    if (d.rev < 8) {
        bool b;
        d >> b;
    }
    d >> mCharacters;
    if (d.rev > 6) {
        d >> mEnviron;
    }
    if (d.rev > 9) {
        d >> mEnviron3D;
    } else {
        mEnviron3D = mEnviron;
    }
    if (d.rev > 1) {
        CreateMeshes();
        FOREACH (it, mCharacters) {
            if (d.rev < 0xE) {
                std::list<Transform> xfmList;
                std::list<RndMultiMesh::Instance> instancesList;
                std::list<OldMMInst> oldmmiList;
                if (it->mMMesh) {
                    if (d.rev < 9) {
                        d >> xfmList;
                        it->mMMesh->Instances().clear();
                        FOREACH (transIt, xfmList) {
                            it->mMMesh->Instances().push_back(
                                RndMultiMesh::Instance(*transIt)
                            );
                        }
                    } else if (d.rev < 0xB) {
                        d >> oldmmiList;
                        FOREACH (mmiIt, oldmmiList) {
                            OldMMInst &old = *mmiIt;
                            it->mMMesh->Instances().push_back(
                                RndMultiMesh::Instance(old.mOldXfm)
                            );
                        }
                    } else {
                        InstanceList &instances = it->mMMesh->Instances();
                        unsigned int count;
                        d >> count;
                        instances.resize(count);
                        FOREACH (instIt, instances) {
                            instIt->LoadRev(d.stream, 3);
                        }
                    }
                } else if (d.rev > 3) {
                    if (d.rev < 9)
                        d >> xfmList;
                    else if (d.rev < 0xB)
                        d >> oldmmiList;
                    else
                        d >> instancesList;
                }
            } else {
                std::list<Transform> xfms;
                d >> xfms;
                if (it->mMMesh) {
                    it->mMMesh->Instances().clear();
                    FOREACH (xfmIt, xfms) {
                        it->mMMesh->Instances().push_back(RndMultiMesh::Instance(*xfmIt));
                    }
                }
            }
            AssignRandomColors(false);
        }
    } else {
        OnRebuild(nullptr);
    }
    if (d.rev > 4) {
        d >> mModifyStamp;
    }
    if (d.rev > 0xC) {
        bool force = false;
        d >> force;
        Force3DCrowd(force);
    }
    if (d.rev > 5) {
        d >> mShow3DOnly;
    }
    if (d.rev > 0xB) {
        d >> mFocus;
    }
    if (d.rev > 0xE) {
        d >> (int &)mCharForceLod;
    }
    if (d.rev > 0xF) {
        d >> unkd0;
    }
    if (d.rev > 0) {
        LOAD_SUPERCLASS(RndPollable);
    }
END_LOADS

void WorldCrowd::UpdateSphere() {
    Sphere s;
    MakeWorldSphere(s, true);
    SetSphere(s);
}

float WorldCrowd::GetDistanceToPlane(const Plane &p, Vector3 &vout) {
    if (mCharacters.empty())
        return 0;
    else {
        float dist = 0;
        bool b1 = true;
        FOREACH (it, mCharacters) {
            RndMultiMesh *multimesh = it->mMMesh;
            if (multimesh) {
                Vector3 v4c;
                float f5 = multimesh->GetDistanceToPlane(p, v4c);
                if (b1 || (std::fabs(f5) < std::fabs(dist))) {
                    b1 = false;
                    vout = v4c;
                    dist = f5;
                }
            }
        }
        return dist;
    }
}

bool WorldCrowd::MakeWorldSphere(Sphere &s, bool b) {
    if (b) {
        s.Zero();
        FOREACH (it, mCharacters) {
            RndMultiMesh *multimesh = it->mMMesh;
            if (multimesh) {
                Sphere local;
                multimesh->MakeWorldSphere(local, true);
                s.GrowToContain(local);
            }
        }
        return true;
    } else if (mSphere.GetRadius()) {
        s = mSphere;
        return true;
    } else
        return false;
}

void WorldCrowd::ListDrawChildren(std::list<RndDrawable *> &draws) {
    FOREACH (it, mCharacters) {
        Character *curChar = it->mDef.mChar;
        if (curChar)
            draws.push_back(curChar);
    }
}

void WorldCrowd::CollideList(const Segment &seg, std::list<Collision> &colls) {
    if (TheLoadMgr.EditMode() && CollideSphere(seg)) {
        ObjList<CharData>::iterator it = mCharacters.begin();
        ObjList<CharData>::iterator end = mCharacters.end();
        while (it != end) {
            RndMultiMesh *curMM = it->mMMesh;
            if (curMM) {
                curMM->CollideList(seg, colls);
            }
            ++it;
        }
    }
}

void WorldCrowd::Poll() {
    if (Showing()) {
        FOREACH (it, mCharacters) {
            Character *curChar = it->mDef.mChar;
            if (curChar && curChar->GetPollState() != 3) {
                curChar->Poll();
            }
        }
    }
}

void WorldCrowd::Enter() {
    RndPollable::Enter();
    FOREACH (it, mCharacters) {
        it->mDef.mMats.clear();
        Character *curChar = it->mDef.mChar;
        if (curChar) {
            if (curChar->GetPollState() != 2)
                curChar->Enter();
            ColorPalette *randPal = curChar->Find<ColorPalette>("random1.pal", false);
            if (randPal && randPal->NumColors() != 0) {
                for (ObjDirItr<RndMat> objIt(curChar, true); objIt; ++objIt) {
                    it->mDef.mMats.push_back(objIt);
                }
            }
        }
    }
}

void WorldCrowd::Exit() {
    RndPollable::Exit();
    FOREACH (it, mCharacters) {
        Character *curChar = it->mDef.mChar;
        if (curChar)
            curChar->Exit();
    }
}

void WorldCrowd::ListPollChildren(std::list<RndPollable *> &polls) const {
    FOREACH (it, mCharacters) {
        Character *curChar = it->mDef.mChar;
        if (curChar)
            polls.push_back(curChar);
    }
}

void WorldCrowd::Delete3DCrowdHandles() {
    if (TheLoadMgr.EditMode()) {
        FOREACH (it, mCharacters) {
            for (int i = 0; i != it->m3DChars.size(); i++) {
                RELEASE(it->m3DChars[i].mHandle);
            }
        }
    }
}

bool WorldCrowd::Crowd3DExists() {
    FOREACH (it, mCharacters) {
        if (it->mDef.mChar && it->mMMesh && !it->m3DChars.empty()) {
            return true;
        }
    }
    return false;
}

void WorldCrowd::SetMatAndCameraLod() {
    RndTex *tex = gImpostorTex[mLod];
    gImpostorCamera->SetTargetTex(tex);
    gImpostorMat->SetDiffuseTex(tex);
}

void WorldCrowd::CreateMeshes() {
    mCharFullness = 1.0f;
    mFlatFullness = 1.0f;
    mLod = 0;
    FOREACH (it, mCharacters) {
        if (it->mMMesh) {
            delete it->mMMesh->Mesh();
            RELEASE(it->mMMesh);
        }
        it->mBackup.clear();
        if (it->mDef.mChar) {
            RndMesh *built = BuildBillboard(it->mDef.mChar, it->mDef.mHeight);
            it->mMMesh = Hmx::Object::New<RndMultiMesh>();
            it->mMMesh->SetMesh(built);
        }
    }
}

struct Sort3DChars {
    bool operator()(
        const WorldCrowd::CharData::Char3D &char1,
        const WorldCrowd::CharData::Char3D &char2
    ) const {
        return char1.mIdx < char2.mIdx;
    }
};

void WorldCrowd::Sort3DCharList() {
    FOREACH (it, mCharacters) {
        std::sort(it->m3DChars.begin(), it->m3DChars.end(), Sort3DChars());
        it->m3DCharsCreated = it->m3DChars;
    }
}

void WorldCrowd::Set3DCharAll() {
    START_AUTO_TIMER("crowd_set3d");
    float fvar1 = mFlatFullness;
    Reset3DCrowd();
    FOREACH (it, mCharacters) {
        RndMultiMesh *multiMesh = it->mMMesh;
        if (multiMesh) {
            std::list<RndMultiMesh::Instance>::iterator instIt = multiMesh->Instances().begin();
            int idx = 0;
            for (; instIt != multiMesh->Instances().end(); ++instIt, ++idx) {
                CharData::Char3D char3D(instIt->mXfm, idx);
                it->m3DChars.push_back(char3D);
            }
            multiMesh->Instances().clear();
            multiMesh->InvalidateProxies();
        }
    }
    Sort3DCharList();
    SetFullness(fvar1, mCharFullness);
    AssignRandomColors(false);
}

void WorldCrowd::Force3DCrowd(bool force) {
    mForce3DCrowd = force;
    if (mForce3DCrowd) {
        Set3DCharAll();
    } else {
        SetFullness(1, 1);
        std::vector<std::pair<int, int> > v;
        Set3DCharList(v, this);
    }
}

RndMesh *WorldCrowd::BuildBillboard(Character *c, float height) {
    float halfHeight = height * 0.5f;
    c->GetSphere().GetRadius();
    RndMesh *mesh = Hmx::Object::New<RndMesh>();
    RndMesh::VertVector &verts = mesh->Verts();
    std::vector<RndMesh::Face> &faces = mesh->Faces();
    float halfWidth = halfHeight * 0.5f;
    verts.resize(4);
    float negHalfWidth = -halfWidth;
    verts[0].pos.Set(negHalfWidth, 0, halfHeight);
    float negHalfHeight = -halfHeight;
    verts[1].pos.Set(negHalfWidth, 0, negHalfHeight);
    verts[2].pos.Set(halfWidth, 0, halfHeight);
    verts[3].pos.Set(halfWidth, 0, negHalfHeight);

    verts[0].tex.Set(0, 0);
    verts[1].tex.Set(0, 1);
    verts[2].tex.Set(1, 0);
    verts[3].tex.Set(1, 1);

    faces.resize(2);
    faces[0].Set(0, 1, 2);
    faces[1].Set(1, 3, 2);
    mesh->Sync(0x3F);
    mesh->SetMat(gImpostorMat);
    mesh->SetTransConstraint(RndTransformable::kConstraintFastBillboardXYZ, gImpostorCamera, false);
    return mesh;
}

#ifndef HX_NATIVE
void SetMatColorFlags(ObjPtrList<RndMat, ObjectDir> &, int, stlpmtx_std::vector<Hmx::Color> *);
#endif

void WorldCrowd::Draw3DChars() {
    if (!Crowd3DExists()) return;
    // Use mEnviron3D if it has a pointer, else mEnviron
    RndEnviron *env;
    if (mEnviron3D) {
        env = mEnviron3D;
    } else {
        env = mEnviron;
    }
    // Save and clear the environ's use-approx-global flag
    bool savedApprox = true;
    if (env) {
        savedApprox = env->UsesApproxGlobal();
        env->SetUseApproxGlobal(false);
    }
    RndEnvironTracker tracker(env, nullptr);
    ObjList<CharData>::iterator charIt = mCharacters.begin();
    for (; charIt != mCharacters.end(); ++charIt) {
        Character *curChar = charIt->mDef.mChar;
        if (!curChar || !charIt->mMMesh) continue;
        int numChars = (int)charIt->m3DChars.size();
        for (int i = 0; i < numChars; i++) {
            Apply3DCharXfm(charIt, i, RndCam::Current());
#ifndef HX_NATIVE
            if (charIt->mDef.mUseRandomColor) {
                SetMatColorFlags(charIt->mDef.mMats, 3, &charIt->m3DChars[i].mColors);
            }
            bool savedSelfShadow = curChar->SelfShadow();
            bool savedUnk252 = *(bool *)((char *)curChar + 0x252);
            bool savedUnk251 = *(bool *)((char *)curChar + 0x251);
            bool isInGame = *(bool *)((char *)&TheRnd + 0x143);
            if (isInGame) {
                curChar->SetSelfShadow(false);
                *(bool *)((char *)curChar + 0x252) = false;
                *(bool *)((char *)curChar + 0x251) = false;
            }
            if (mCharForceLod != kLODPerFrame) {
                curChar->SetLodType(mCharForceLod);
            }
            curChar->DrawShowing();
            if (mCharForceLod != kLODPerFrame) {
                curChar->SetLodType(kLODPerFrame);
            }
            curChar->SetSelfShadow(savedSelfShadow);
            *(bool *)((char *)curChar + 0x252) = savedUnk252;
            *(bool *)((char *)curChar + 0x251) = savedUnk251;
#endif
        }
    }
    if (env) {
        env->SetUseApproxGlobal(savedApprox);
    }
}

void WorldCrowd::AssignRandomColors(bool incrementStamp) {
    if (incrementStamp) {
        mModifyStamp++;
    }
    FOREACH (it, mCharacters) {
        if (it->mDef.mChar && it->mMMesh && !it->m3DChars.empty()) {
            std::vector<ColorPalette *> colorPaletteList;
            it->mDef.mUseRandomColor = false;
            for (int i = 0; i < 3; i++) {
                ColorPalette *randPal = it->mDef.mChar->Find<ColorPalette>(
                    MakeString("random%d.pal", i + 1), false
                );
                if (randPal) {
                    colorPaletteList.push_back(randPal);
                }
            }
            if (!colorPaletteList.empty()) {
                for (int i = 0; i < (int)it->m3DChars.size(); i++) {
                    CharData::Char3D &char3D = it->m3DChars[i];
                    char3D.mColors.clear();
                    it->mDef.mUseRandomColor = true;
                    while ((int)char3D.mColors.size() < 3) {
                        ColorPalette *randPal =
                            colorPaletteList[RandomInt(0, colorPaletteList.size())];
                        Hmx::Color randColor =
                            randPal->GetColor(RandomInt(0, randPal->NumColors()));
                        char3D.mColors.push_back(randColor);
                    }
                }
            }
        }
    }
}

void WorldCrowd::Reset3DCrowd() {
    SetFullness(1.0f, mCharFullness);
    FOREACH (it, mCharacters) {
        RndMultiMesh *multiMesh = it->mMMesh;
        if (multiMesh) {
            InstanceList &instances = multiMesh->Instances();
            InstanceList::iterator instIt = instances.begin();
            int curInstIdx = 0;
            for (int i = 0; i < (int)it->m3DCharsCreated.size(); i++) {
                int targetInstIdx = (int)(intptr_t)it->m3DCharsCreated[i].mHandle;
                while (curInstIdx < targetInstIdx) {
                    ++instIt;
                    curInstIdx++;
                }
                RndMultiMesh::Instance inst(it->m3DCharsCreated[i].mXfm);
                instIt = instances.insert(instIt, inst);
                ++instIt;
                curInstIdx++;
            }
        }
        it->m3DCharsCreated.clear();
        it->m3DChars.clear();
    }
}

void WorldCrowd::SetFullness(float flatFullness, float charFullness) {
    START_AUTO_TIMER("crowd_set");
    mFlatFullness = flatFullness;
    mCharFullness = charFullness;
    Delete3DCrowdHandles();
    FOREACH (it, mCharacters) {
        RndMultiMesh *multiMesh = it->mMMesh;
        if (multiMesh) {
            InstanceList &instances = multiMesh->Instances();
            InstanceList &backup = it->mBackup;
            int instanceCount = (int)instances.size();
            int backupCount = (int)backup.size();
            int totalCount = instanceCount + backupCount;
            int targetInstances = (int)((float)totalCount * charFullness);
            if (instanceCount < targetInstances) {
                // move from backup to instances
                int toMove = targetInstances - instanceCount;
                InstanceList::iterator backIt = backup.begin();
                for (int i = 0; i < toMove; i++) {
                    ++backIt;
                }
                instances.splice(instances.end(), backup, backup.begin(), backIt);
                // invalidate proxies handled below
            } else if (targetInstances < instanceCount) {
                // move from instances to backup
                int toRemove = instanceCount - targetInstances;
                InstanceList::iterator instIt = instances.begin();
                for (int i = 0; i < toRemove; i++) {
                    ++instIt;
                }
                backup.splice(backup.end(), instances, instances.begin(), instIt);
                multiMesh->InvalidateProxies();
            }
            // handle m3DChars (visible 3D chars)
            int totalChars3D = (int)it->m3DChars.size() + (int)it->m3DCharsCreated.size();
            int targetChars3D = (int)((float)totalChars3D * flatFullness);
            if (targetChars3D > (int)instances.size())
                targetChars3D = (int)instances.size();
            int currentChars3D = (int)it->m3DChars.size();
            if (currentChars3D < targetChars3D) {
                int toAdd = targetChars3D - currentChars3D;
                int startIdx = currentChars3D;
                InstanceList::iterator instIt = instances.begin();
                // advance to startIdx in m3DCharsCreated
                for (int i = 0; i < startIdx; i++) ++instIt;
                for (int i = 0; i < toAdd; i++) {
                    it->m3DChars.push_back(it->m3DCharsCreated[startIdx + i]);
                    ++instIt;
                }
            } else if (targetChars3D < currentChars3D) {
                int toRemove = currentChars3D - targetChars3D;
                for (int i = 0; i < toRemove; i++) {
                    it->m3DChars.pop_back();
                }
            }
        }
    }
    AssignRandomColors(false);
}

void WorldCrowd::Set3DCharXfm(
    const std::list<CharData>::iterator &charItr, int charIdx, const Transform &xfm
) {
    MILO_ASSERT(charIdx >= 0 && charIdx < (int)charItr->m3DChars.size(), 0x289);
    CharData::Char3D &char3D = charItr->m3DChars[charIdx];
    char3D.mXfm = xfm;
    // Also update the matching entry in m3DCharsCreated (matched by handle)
    WorldCrowd3DCharHandle *handle = char3D.mHandle;
    for (int i = 0; i < (int)charItr->m3DCharsCreated.size(); i++) {
        if (charItr->m3DCharsCreated[i].mHandle == handle) {
            charItr->m3DCharsCreated[i].mXfm = xfm;
            break;
        }
    }
    MILO_ASSERT(true, 0x297); // always passes - just for line number
}

void WorldCrowd::Apply3DCharXfm(
    const std::list<CharData>::iterator &charItr, int charIdx, RndCam *cam
) {
    MILO_ASSERT(charIdx >= 0 && charIdx < (int)charItr->m3DChars.size(), 0x29d);
    WorldCrowd3DCharHandle *handle = charItr->m3DChars[charIdx].mHandle;
    if (!handle) return;
    RndTransformable *environ = mEnviron;
    if (!environ) return;
    const CharData::Char3D &char3D = charItr->m3DChars[charIdx];
    const Transform &charXfm = char3D.mXfm;
    float charHeight = charItr->mDef.mRadius * 0.5f;
    float halfRadius = charItr->mDef.mRadius * 0.5f * 0.5f;
    Transform newXfm;
    bool useFocus = (mCrowdRotate != kCrowdRotateNone) && cam;
    if (!useFocus && !mFocus) {
        // Use environ world xfm
        newXfm = environ->WorldXfm();
        handle->SetWorldXfm(newXfm);
        return;
    }
    // Get the environ's world y-axis (up direction from world xfm)
    const Transform &environXfm = environ->WorldXfm();
    Vector3 envY = environXfm.m.y;
    Vector3 forwardDir;
    if (mCrowdRotate == kCrowdRotateFace) {
        // Face toward camera
        const Transform &camXfm = cam->WorldXfm();
        forwardDir.x = camXfm.m.z.x * envY.y - camXfm.m.z.y * envY.x;
        forwardDir.y = camXfm.m.z.z * envY.x - camXfm.m.z.x * envY.z;
        forwardDir.z = camXfm.m.z.y * envY.z - camXfm.m.z.z * envY.y;
    } else if (mCrowdRotate == kCrowdRotateAway) {
        // Face away from camera
        const Transform &camXfm = cam->WorldXfm();
        forwardDir.x = camXfm.m.z.y * envY.x - camXfm.m.z.x * envY.y;
        forwardDir.y = camXfm.m.z.x * envY.z - camXfm.m.z.z * envY.x;
        forwardDir.z = camXfm.m.z.z * envY.y - camXfm.m.z.y * envY.z;
    } else if (mFocus) {
        // Face toward focus point
        const Vector3 &focusPos = mFocus->WorldXfm().v;
        forwardDir.x = focusPos.x - charXfm.v.x;
        forwardDir.y = focusPos.y - charXfm.v.y;
        forwardDir.z = envY.y * forwardDir.z - envY.z * 0.0f;
        // simple cross with up
        float fx = forwardDir.x, fy = forwardDir.y, fz = forwardDir.z;
        forwardDir.x = fy * envY.z - envY.y * 0.0f;
        forwardDir.y = envY.x * 0.0f - envY.z * fx;
        forwardDir.z = envY.y * fx - fy * envY.x;
    }
    Normalize(forwardDir, forwardDir);
    // Build right and up from forward
    Vector3 rightDir;
    rightDir.x = forwardDir.z * envY.y - forwardDir.y * envY.z;
    rightDir.y = forwardDir.x * envY.z - forwardDir.z * envY.x;
    rightDir.z = forwardDir.y * envY.x - forwardDir.x * envY.y;
    newXfm.m.x = forwardDir;
    newXfm.m.y = envY;
    newXfm.m.z = rightDir;
    newXfm.v = charXfm.v;
    handle->SetWorldXfm(newXfm);
}

void WorldCrowd::Set3DCharList(
    const std::vector<std::pair<int, int> > &pairVec, Hmx::Object *obj
) {
    START_AUTO_TIMER("crowd_set3d");
    if (!mForce3DCrowd) {
        float oldFullness = mFlatFullness;
        Reset3DCrowd();
        std::vector<std::pair<RndMultiMesh *, InstanceList::iterator> > grosserPairs;
        grosserPairs.reserve(pairVec.size());
        for (int i = 0; i < (int)pairVec.size(); i++) {
            int meshIdx = pairVec[i].first;
            if (meshIdx >= (int)mCharacters.size()) {
                MILO_WARN(
                    "%s setting bad mesh %d, only has %d",
                    PathName(obj),
                    meshIdx,
                    mCharacters.size()
                );
            } else {
                ObjList<CharData>::iterator charIt = mCharacters.begin();
                for (int n = 0; n < meshIdx; ++n, ++charIt)
                    ;
                RndMultiMesh *curMMesh = charIt->mMMesh;
                if (curMMesh) {
                    int charInstIdx = pairVec[i].second;
                    if (charInstIdx >= (int)curMMesh->Instances().size()) {
                        MILO_WARN(
                            "%s setting bad 3d char %d on mmesh %s, only has %d chars",
                            PathName(this),
                            charInstIdx,
                            curMMesh->Name(),
                            curMMesh->Instances().size()
                        );
                    } else {
                        InstanceList::iterator instIt = curMMesh->Instances().begin();
                        for (int n = 0; n < charInstIdx; ++instIt, ++n)
                            ;
                        CharData::Char3D char3D(instIt->mXfm, charInstIdx);
                        charIt->m3DChars.push_back(char3D);
                        grosserPairs.push_back(std::make_pair(charIt->mMMesh, instIt));
                    }
                }
            }
        }
        for (int i = 0; i < (int)grosserPairs.size(); i++) {
            grosserPairs[i].first->Instances().erase(grosserPairs[i].second);
            grosserPairs[i].first->InvalidateProxies();
        }
        Sort3DCharList();
        SetFullness(oldFullness, mCharFullness);
        AssignRandomColors(false);
        // Create handles for edit mode
        if (TheLoadMgr.EditMode()) {
            ObjList<CharData>::iterator charIt = mCharacters.begin();
            for (; charIt != mCharacters.end(); ++charIt) {
                for (int i = 0; i < (int)charIt->m3DChars.size(); i++) {
                    if (!charIt->m3DChars[i].mHandle) {
                        WorldCrowd3DCharHandle *handle =
                            Hmx::Object::New<WorldCrowd3DCharHandle>();
                        handle->Set3DChar(this, charIt, i, charIt->m3DChars[i].mXfm);
                        charIt->m3DChars[i].mHandle = handle;
                        // also update m3DCharsCreated
                        for (int j = 0; j < (int)charIt->m3DCharsCreated.size(); j++) {
                            if (charIt->m3DCharsCreated[j].mIdx == charIt->m3DChars[i].mIdx) {
                                charIt->m3DCharsCreated[j].mHandle = handle;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

void WorldCrowd::Mats(std::list<RndMat *> &mats, bool additive) {
    FOREACH (it, mCharacters) {
        if (it->mDef.mChar && it->mMMesh && !it->m3DChars.empty()) {
            FOREACH (matIt, it->mDef.mMats) {
                RndMat *mat = *matIt;
                if (mat) {
                    mats.push_back(mat);
                }
            }
        }
    }
}

DataNode WorldCrowd::OnIterateFrac(DataArray *da) {
    START_AUTO_TIMER("crowd_iter");
    return DataNode(0);
}

void WorldCrowd::DrawShowing() {
    START_AUTO_TIMER("crowd_draw");
    MILO_ASSERT(!gImpostorMat->NextPass(), 0x34A);
    if (mEnviron3D) {
        Draw3DChars();
        if (TheRnd.GetDrawMode() == Rnd::kDrawNormal) {
            // Render billboard crowd
            FOREACH (it, mCharacters) {
                RndMultiMesh *multiMesh = it->mMMesh;
                if (it->mDef.mChar && multiMesh && !mShow3DOnly) {
                    multiMesh->DrawShowing();
                }
            }
        }
    }
}

#ifndef HX_NATIVE
void SetMatColorFlags(ObjPtrList<RndMat, ObjectDir> &matList, int flags,
                      stlpmtx_std::vector<Hmx::Color> *colors) {
    RndMat **head = (RndMat **)((char *)&matList + 0x8);
    if (*head == NULL) {
        return;
    }

    RndMat *current = *head;
    do {
        current->SetColorMod(*(Hmx::Color *)flags, 0);
        u32 *modNum = (u32 *)((char *)current + 0x228);
        *modNum = *modNum | 2;

        if (colors != NULL) {
            int *pBegin = (int *)colors;
            int *pEnd = (int *)colors + 1;
            int size = *pEnd - *pBegin;
            int alignedSize = size & 0xFFFFFFF0;

            if (alignedSize != 0x30) {
                TheDebug.Fail(MakeString(kAssertStr, "Crowd.cpp", 0x33b, "RndMat::kColorModNum != modulate"), nullptr);
            }

            int colorCount = size >> 4;
            if (colorCount > 0) {
                for (u32 i = 0; i < (u32)colorCount; i++) {
                    current->SetColorMod(colors->at(i), i);
                }
            } else {
                stlpmtx_std::__stl_throw_out_of_range("vector");
            }
        }

        current = *(RndMat **)((char *)current + 0x14);
    } while (current != NULL);
}
#endif // HX_NATIVE

