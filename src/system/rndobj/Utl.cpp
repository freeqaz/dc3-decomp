#include "Rnd.h"
#include "math/Color.h"
#include "math/Mtx.h"
#include "math/Utl.h"
#include "math/Vec.h"
#include "obj/DataFunc.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/Endian.h"
#include "os/FileCache.h"
#include "os/Platform.h"
#include "os/System.h"
#include "rndobj/AmbientOcclusion.h"
#include "rndobj/Bitmap.h"
#include "rndobj/Cam.h"
#include "rndobj/Dir.h"
#include "rndobj/Draw.h"
#include "rndobj/Env.h"
#include "rndobj/Flare.h"
#include "rndobj/Group.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "rndobj/MetaMaterial.h"
#include "rndobj/Part.h"
#include "rndobj/Tex.h"
#include "utl/Cache.h"
#include "utl/Std.h"
#include "rndobj/Utl.h"
#include "math/Key.h"
#include "os/File.h"
#include "obj/Data.h"
#include "obj/Utl.h"

#include "math/Rand.h"

typedef void (*SplashFunc)(void);

class ResourceFileCacheHelper : public FileCacheHelper {
public:
    virtual const char *CacheFile(const char *);
};

ResourceFileCacheHelper gResourceFileCacheHelper;
float gLimitUVRange;
int gDxtCacher;
static ObjectDir *sSphereDir;
static RndMesh *sSphereMesh;
static ObjectDir *sCylinderDir;
static RndMesh *sCylinderMesh;
// std::list<BuildPoly> gChildPolys;
// std::list<BuildPoly> gParentPolys;
SplashFunc gSplashPoll;
SplashFunc gSplashSuspend;
SplashFunc gSplashResume;
Vector3 gUtlXfms;

RndGroup *GroupOwner(Hmx::Object *o) {
    if (o) {
        FOREACH (it, o->Refs()) {
            RndGroup *grp = dynamic_cast<RndGroup *>(it->RefOwner());
            if (grp) {
                if (grp->HasObject(o)) {
                    return grp;
                }
            }
        }
    }
    return nullptr;
}

DataNode OnGroupOwner(DataArray *da) { return GroupOwner(da->Obj<Hmx::Object>(1)); }

RndEnviron *FindEnviron(RndDrawable *d) {
    RndGroup *owner = GroupOwner(d);
    if (owner) {
        int i = owner->Draws().size();
        while (--i > 0) {
            if (owner->Draws()[i] == d && i >= 0) {
                for (; i >= 0; i--) {
                    RndEnviron *env = dynamic_cast<RndEnviron *>(owner->Draws()[i]);
                    if (env) {
                        return env;
                    }
                }
            }
        }
        return FindEnviron(owner);
    } else {
        RndDir *rdir = dynamic_cast<RndDir *>(d->Dir());
        if (rdir) {
            std::list<RndDrawable *> children;
            rdir->ListDrawChildren(children);
            if (ListFind(children, d)) {
                return rdir->GetEnv();
            }
        }
        MILO_NOTIFY("Need to find environment of draw parent");
    }
    return nullptr;
}

DataNode DataFindEnviron(DataArray *da) { return FindEnviron(da->Obj<RndDrawable>(1)); }

bool GroupedUnder(RndGroup *grp, Hmx::Object *o) {
    FOREACH (it, grp->Objects()) {
        if (*it == o)
            return true;
        RndGroup *casted = dynamic_cast<RndGroup *>(*it);
        if (casted && GroupedUnder(casted, o))
            return true;
    }
    return false;
}

void SetRndSplasherCallback(SplashFunc func1, SplashFunc func2, SplashFunc func3) {
    gSplashPoll = func1;
    gSplashSuspend = func2;
    gSplashResume = func3;
}

void RndSplasherPoll() {
    if (gSplashPoll)
        gSplashPoll();
}

void RndSplasherSuspend() {
    if (gSplashSuspend)
        gSplashSuspend();
}

void RndSplasherResume() {
    if (gSplashResume)
        gSplashResume();
}

const char *CacheResource(const char *, CacheResourceResult &);

Loader *ResourceFactory(const FilePath &f, LoaderPos p) {
    return new FileLoader(
        f, CacheResource(f.c_str(), nullptr), p, 0, false, true, nullptr, nullptr
    );
}

void RndUtlPreInit() {
    SystemConfig("rnd")->FindData("limit_uv_range", gLimitUVRange, true);
    TheLoadMgr.RegisterFactory("bmp", ResourceFactory);
    TheLoadMgr.RegisterFactory("png", ResourceFactory);
    TheLoadMgr.RegisterFactory("xbv", ResourceFactory);
    TheLoadMgr.RegisterFactory("jpg", ResourceFactory);
    TheLoadMgr.RegisterFactory("tif", ResourceFactory);
    TheLoadMgr.RegisterFactory("tiff", ResourceFactory);
    TheLoadMgr.RegisterFactory("psd", ResourceFactory);
    TheLoadMgr.RegisterFactory("gif", ResourceFactory);
    TheLoadMgr.RegisterFactory("tga", ResourceFactory);
    DataRegisterFunc("find_environ", DataFindEnviron);
    DataRegisterFunc("group_owner", OnGroupOwner);
}

void RndUtlInit() {
    FileCache::RegisterResourceCacheHelper(&gResourceFileCacheHelper);
    if (!UsingCD()) {
        sCylinderDir = DirLoader::LoadObjects(
            FilePath(FileSystemRoot(), "rndobj/cylinder.milo"), 0, 0
        );
    }
    sSphereDir =
        DirLoader::LoadObjects(FilePath(FileSystemRoot(), "rndobj/sphere.milo"), 0, 0);
    if (sSphereDir) {
        sSphereMesh = sSphereDir->Find<RndMesh>("sphere.mesh", true);
    }
    if (sCylinderDir) {
        // Note: Searches in sSphereDir, not sCylinderDir - matches original binary
        sCylinderMesh = sSphereDir->Find<RndMesh>("Cylinder.mesh", true);
    }
}

// Clean up sphere and cylinder resource directories
void RndUtlTerminate() {
    if (sSphereDir) {
        delete sSphereDir;
    }
    sSphereDir = 0;
    sSphereMesh = 0;
    if (sCylinderDir) {
        delete sCylinderDir;
    }
    sCylinderDir = 0;
    sCylinderMesh = 0;
}

MatShaderOptions GetDefaultMatShaderOpts(const Hmx::Object *obj, RndMat *mat) {
    MatShaderOptions opts;
    const RndMesh *mesh = dynamic_cast<const RndMesh *>(obj);
    if (mesh) {
        if (mesh->Mat() == mat) {
            opts.SetLast5(0x12);
            opts.SetHasBones(mesh->NumBones() != 0);
            opts.SetHasAOCalc(mesh->HasAOCalc());
        }
    } else {
        const RndMultiMesh *multimesh = dynamic_cast<const RndMultiMesh *>(obj);
        if (multimesh) {
            const RndMesh *mesh = multimesh->Mesh();
            if (mesh && mesh->Mat()) {
                if (mesh->Mat() == mat) {
                    int mask = mesh->TransConstraint()
                            == RndTransformable::kConstraintFastBillboardXYZ
                        ? 0xD
                        : 0xC;
                    opts.SetLast5(mask);
                    opts.SetHasBones(false);
                    opts.SetHasAOCalc(mesh->HasAOCalc());
                }
            }
        } else {
            const RndParticleSys *partSys = dynamic_cast<const RndParticleSys *>(obj);
            if (partSys) {
                if (partSys->GetMat() == mat) {
                    opts.SetLast5(0xE);
                }
            } else {
                const RndFlare *flare = dynamic_cast<const RndFlare *>(obj);
                if (flare) {
                    if (flare->GetMat() == mat) {
                        opts.SetLast5(6);
                    }
                }
            }
        }
    }
    return opts;
}

const char *MovieExtension(const char *name, Platform p) {
    const char *ext;
    if (stricmp(name, "xbv") == 0) {
        // xbox, pc, ps3, or wii only
        if (p >= kPlatformXBox && p <= kPlatformWii) {
            return "xbv";
        }
        return name;
    } else
        return nullptr;
}

float ConvertFov(float a, float b) {
    float x = tanf(0.5f * a);
    return atanf(b * x) * 2;
}

void PreMultiplyAlpha(Hmx::Color &c) {
    c.red *= c.alpha;
    c.green *= c.alpha;
    c.blue *= c.alpha;
}

int GenerationCount(RndTransformable *t1, RndTransformable *t2) {
    if (t1 && t2) {
        int count = 0;
        for (; t2 != nullptr; t2 = t2->TransParent()) {
            if (t2 == t1)
                return count;
            count++;
        }
    }
    return 0;
}

void CreateAndSetMetaMat(RndMat *mat) {
    MILO_ASSERT(mat, 0x124A);
    if (!mat->GetMetaMaterial()) {
        MetaMaterial *metaMat = mat->CreateMetaMaterial(false);
        mat->SetMetaMat(metaMat, true);
    }
}

bool ShouldStrip(RndTransformable *trans) {
    if (!trans)
        return false;
    const char *name = trans->Name();
    if (!name)
        return false;
    return strnicmp("bone_", name, 5) == 0 || strnicmp("exo_", name, 4) == 0
        || strncmp("spot_", name, 5) == 0;
}

bool AnimContains(const RndAnimatable *anim1, const RndAnimatable *anim2) {
    if (anim1 == anim2)
        return true;
    else {
        std::list<RndAnimatable *> children;
        anim1->ListAnimChildren(children);
        FOREACH (it, children) {
            if (AnimContains(*it, anim2))
                return true;
        }
        return false;
    }
}

RndMat *GetMat(RndDrawable *draw) {
    std::list<RndMat *> mats;
    draw->Mats(mats, false);
    RndMat *ret;
    if (mats.empty())
        ret = 0;
    else
        ret = mats.front();
    return ret;
}

bool SortDraws(RndDrawable *draw1, RndDrawable *draw2) {
    if (draw1->GetOrder() != draw2->GetOrder())
        return draw1->GetOrder() < draw2->GetOrder();
    else {
        RndMat *mat1 = GetMat(draw1);
        RndMat *mat2 = GetMat(draw2);
        if (mat1 != mat2) {
            return mat1 < mat2;
        } else
            return strcmp(draw1->Name(), draw2->Name()) < 0;
    }
}

bool SortPolls(const RndPollable *p1, const RndPollable *p2) {
    if (p1->PollEnabled() != p2->PollEnabled()) {
        return p1->PollEnabled();
    } else {
        return strcmp(p1->Name(), p2->Name()) < 0;
    }
}

bool LeftHanded(const Hmx::Matrix3 &m) {
    Vector3 cross;
    Cross(m.x, m.y, cross);
    float det = Dot(m.z, cross);
    return det < 0;
}

float AngleBetween(const Hmx::Quat &q1, const Hmx::Quat &q2) {
    Hmx::Quat qtmp;
    Negate(q1, qtmp);
    Multiply(q2, qtmp, qtmp);
    if (qtmp.w > 1.0f) {
        return 0;
    } else {
        return acosf(qtmp.w) * 2.0f;
    }
}

// Check if UV coordinates are invalid (NaN or out of range), clamping near-zero values.
// Returns true if the UV is bad (NaN or extreme), false if valid (after clamping).
bool BadUV(Vector2 &v) {
    // NaN check: IEEE 754 property that NaN != NaN
    bool xIsNaN = v.x != v.x;
    if (xIsNaN) return true;
    bool yIsNaN = v.y != v.y;
    if (yIsNaN) return true;

    // Range check: reject extreme values beyond reasonable UV range
    if (fabsf(v.x) > 1000.0f || fabsf(v.y) > 1000.0f) {
        return true;
    }

    // Clamp near-zero values to exactly zero to avoid floating-point precision issues
    bool xIsSmall = fabsf(v.x) < 0.0001f;
    if (xIsSmall) {
        v.x = 0;
    }
    bool yIsSmall = fabsf(v.y) < 0.0001f;
    if (yIsSmall) {
        v.y = 0;
    }

    return false;
}

void SetLocalScale(RndTransformable *t, const Vector3 &vec) {
    Hmx::Matrix3 m;
    Normalize(t->LocalXfm().m, m);
    Scale(vec, m, m);
    t->SetLocalRot(m);
}

void CalcBox(RndMesh *m, Box &b) {
    FOREACH (it, m->Verts()) {
        Vector3 vec;
        Multiply(it->pos, m->WorldXfm(), vec);
        b.GrowToContain(vec, it == m->Verts().begin());
    }
}

void ClearAO(RndMesh *m) {
    if (m->HasAOCalc()) {
        for (uint i = 0; i < m->Verts().size(); i++) {
            m->Verts(i).color.Set(1, 1, 1, 1);
        }
        m->SetHasAOCalc(false);
        m->Sync(0x1F);
    }
}

void ListDrawGroups(RndDrawable *draw, ObjectDir *dir, std::list<RndGroup *> &gList) {
    for (ObjDirItr<RndGroup> it(dir, true); it != 0; ++it) {
        if (VectorFind(it->Draws(), draw)) {
            gList.push_back(it);
        }
    }
}

void ResetColors(std::vector<Hmx::Color> &colors, int newNumColors) {
    Hmx::Color reset(1, 1, 1, 1);
    colors.resize(newNumColors);
    for (int i = 0; i < newNumColors; i++) {
        colors[i] = reset;
    }
}

void UtilDrawString(const char *c, const Vector3 &v, const Hmx::Color &col) {
    Vector2 v2;
    if (RndCam::Current()->WorldToScreen(v, v2) > 0) {
        v2.x *= TheRnd.Width();
        v2.y *= TheRnd.Height();
        TheRnd.DrawString(c, v2, col, true);
    }
}

void UtilDrawBox(const Transform &tf, const Box &box, const Hmx::Color &col, bool b4) {
    Vector3 vecs[8] = { Vector3(box.mMin.x, box.mMin.y, box.mMin.z),
                        Vector3(box.mMin.x, box.mMax.y, box.mMin.z),
                        Vector3(box.mMax.x, box.mMax.y, box.mMin.z),
                        Vector3(box.mMax.x, box.mMin.y, box.mMin.z),
                        Vector3(box.mMin.x, box.mMin.y, box.mMax.z),
                        Vector3(box.mMin.x, box.mMax.y, box.mMax.z),
                        Vector3(box.mMax.x, box.mMax.y, box.mMax.z),
                        Vector3(box.mMax.x, box.mMin.y, box.mMax.z) };
    for (int i = 0; i < 8; i++) {
        Multiply(vecs[i], tf, vecs[i]);
    }
    TheRnd.DrawLine(vecs[0], vecs[1], col, b4);
    TheRnd.DrawLine(vecs[1], vecs[2], col, b4);
    TheRnd.DrawLine(vecs[2], vecs[3], col, b4);
    TheRnd.DrawLine(vecs[3], vecs[0], col, b4);

    TheRnd.DrawLine(vecs[0], vecs[4], col, b4);
    TheRnd.DrawLine(vecs[1], vecs[5], col, b4);
    TheRnd.DrawLine(vecs[2], vecs[6], col, b4);
    TheRnd.DrawLine(vecs[3], vecs[7], col, b4);

    TheRnd.DrawLine(vecs[4], vecs[5], col, b4);
    TheRnd.DrawLine(vecs[5], vecs[6], col, b4);
    TheRnd.DrawLine(vecs[6], vecs[7], col, b4);
    TheRnd.DrawLine(vecs[7], vecs[4], col, b4);
}

void UtilDrawAxes(const Transform &tf, float f, const Hmx::Color &c) {
    Vector3 vec38;
    Hmx::Color c48;
    ScaleAdd(tf.v, tf.m.x, f, vec38);
    Interp(c, Hmx::Color(1, 0, 0), 0.8f, c48);
    TheRnd.DrawLine(tf.v, vec38, c48, false);

    ScaleAdd(tf.v, tf.m.y, f, vec38);
    Interp(c, Hmx::Color(0, 1, 0), 0.8f, c48);
    TheRnd.DrawLine(tf.v, vec38, c48, false);

    ScaleAdd(tf.v, tf.m.z, f, vec38);
    Interp(c, Hmx::Color(0, 0, 1), 0.8f, c48);
    TheRnd.DrawLine(tf.v, vec38, c48, false);
}

void UtilDrawLine(const Vector2 &v1, const Vector2 &v2, const Hmx::Color &color) {
    RndCam *cam = RndCam::Current();
    float planeRatio = (cam->FarPlane() - cam->NearPlane()) / 10.0f + cam->NearPlane();
    Vector3 v3_1, v3_2;
    cam->ScreenToWorld(v1, planeRatio, v3_1);
    cam->ScreenToWorld(v2, planeRatio, v3_2);
    TheRnd.DrawLine(v3_1, v3_2, color, false);
}

void UtilDrawRect2D(const Vector2 &v1, const Vector2 &v2, const Hmx::Color &color) {
    Vector2 cross1(v2.x, v1.y);
    Vector2 cross2(v1.x, v2.y);
    UtilDrawLine(v1, cross1, color);
    UtilDrawLine(cross1, v2, color);
    UtilDrawLine(v2, cross2, color);
    UtilDrawLine(cross2, v1, color);
}

void CalcSphere(RndTransAnim *a, Sphere &s) {
    s.Zero();
    if (!a->TransKeys().empty()) {
        RndTransformable *trans = a->Trans() ? a->Trans()->TransParent() : nullptr;
        Box box;
        Vector3 vec;
        FOREACH (it, a->TransKeys()) {
            if (trans) {
                Multiply(it->value, trans->WorldXfm(), vec);
            } else
                vec = it->value;
            box.GrowToContain(vec, it == a->TransKeys().begin());
        }
        Vector3 vres;
        CalcBoxCenter(vres, box);
        Subtract(box.mMax, vres, vec);
        Vector3 vsphere;
        float fmax = Max(vec.x, vec.y, vec.z);
        CalcBoxCenter(vsphere, box);
        s.Set(vsphere, fmax);
    }
}

void SpliceKeys(
    RndTransAnim *anim1, RndTransAnim *anim2, float firstFrame, float lastFrame
) {
    float start = anim1->StartFrame();
    float end = anim1->EndFrame();
    if (start < 0.0f || end > lastFrame)
        MILO_NOTIFY("%s has keyframes outside (0, %f)", anim1->Name(), lastFrame);
    else {
        RndTransformable *trans = anim1->Trans();
        if (!anim1->TransKeys().empty()) {
            if (anim1->TransKeys().front().frame != 0.0f) {
                anim1->TransKeys().Add(anim1->TransKeys().front().value, 0.0f, false);
            }
            if (anim1->TransKeys().back().frame != lastFrame) {
                anim1->TransKeys().Add(anim1->TransKeys().back().value, lastFrame, false);
            }
        } else if (trans) {
            anim1->TransKeys().Add(trans->LocalXfm().v, 0.0f, false);
            anim1->TransKeys().Add(trans->LocalXfm().v, lastFrame, false);
        } else {
            anim1->TransKeys().Add(Vector3(0.0f, 0.0f, 0.0f), 0.0f, false);
            anim1->TransKeys().Add(Vector3(0.0f, 0.0f, 0.0f), lastFrame, false);
        }

        if (!anim1->RotKeys().empty()) {
            if (anim1->RotKeys().front().frame != 0.0f) {
                anim1->RotKeys().Add(anim1->RotKeys().front().value, 0.0f, false);
            }
            if (anim1->RotKeys().back().frame != lastFrame) {
                anim1->RotKeys().Add(anim1->RotKeys().back().value, lastFrame, false);
            }
        } else if (trans) {
            Hmx::Quat q(trans->LocalXfm().m);
            anim1->RotKeys().Add(q, 0.0f, false);
            anim1->RotKeys().Add(q, lastFrame, false);
        } else {
            anim1->RotKeys().Add(Hmx::Quat(0.0f, 0.0f, 0.0f, 1.0f), 0.0f, false);
            anim1->RotKeys().Add(Hmx::Quat(0.0f, 0.0f, 0.0f, 1.0f), lastFrame, false);
        }

        if (!anim1->ScaleKeys().empty()) {
            if (anim1->ScaleKeys().front().frame != 0.0f) {
                anim1->ScaleKeys().Add(anim1->ScaleKeys().front().value, 0.0f, false);
            }
            if (anim1->ScaleKeys().back().frame != lastFrame) {
                anim1->ScaleKeys().Add(anim1->ScaleKeys().back().value, lastFrame, false);
            }
        } else if (trans) {
            Vector3 v;
            MakeScale(trans->LocalXfm().m, v);
            anim1->ScaleKeys().Add(v, 0.0f, false);
            anim1->ScaleKeys().Add(v, lastFrame, false);
        } else {
            anim1->ScaleKeys().Add(Vector3(1.0f, 1.0f, 1.0f), 0.0f, false);
            anim1->ScaleKeys().Add(Vector3(1.0f, 1.0f, 1.0f), lastFrame, false);
        }

        for (Keys<Vector3, Vector3>::iterator it = anim1->TransKeys().begin();
             it != anim1->TransKeys().end();
             it++) {
            (*it).frame += firstFrame;
        }
        for (Keys<Hmx::Quat, Hmx::Quat>::iterator it = anim1->RotKeys().begin();
             it != anim1->RotKeys().end();
             it++) {
            (*it).frame += firstFrame;
        }
        for (Keys<Vector3, Vector3>::iterator it = anim1->ScaleKeys().begin();
             it != anim1->ScaleKeys().end();
             it++) {
            (*it).frame += firstFrame;
        }

        float fsum = firstFrame + lastFrame;
        int transRemoved = anim2->TransKeys().Remove(firstFrame, fsum);
        int rotRemoved = anim2->RotKeys().Remove(firstFrame, fsum);
        int scaleRemoved = anim2->ScaleKeys().Remove(firstFrame, fsum);

        anim2->TransKeys().insert(
            anim2->TransKeys().begin() + transRemoved,
            anim1->TransKeys().begin(),
            anim1->TransKeys().end()
        );
        anim2->RotKeys().insert(
            anim2->RotKeys().begin() + rotRemoved,
            anim1->RotKeys().begin(),
            anim1->RotKeys().end()
        );
        anim2->ScaleKeys().insert(
            anim2->ScaleKeys().begin() + scaleRemoved,
            anim1->ScaleKeys().begin(),
            anim1->ScaleKeys().end()
        );
    }
}

void LinearizeKeys(
    RndTransAnim *anim, float f2, float f3, float f4, float firstFrame, float lastFrame
) {
    int firstFrameIdx, lastFrameIdx;
    if (f2) {
        if (anim->TransKeys().size() > 2) {
            Keys<Vector3, Vector3> vecKeys;
            anim->TransKeys().FindBounds(
                firstFrame, lastFrame, firstFrameIdx, lastFrameIdx
            );
            for (int i = firstFrameIdx + 1; i < lastFrameIdx - vecKeys.size();) {
                vecKeys.push_back(anim->TransKeys()[i]);
                anim->TransKeys().Remove(i);
                for (int j = 0; j < vecKeys.size(); j++) {
                    Vector3 vec;
                    InterpVector(
                        anim->TransKeys(), anim->TransSpline(), vecKeys[j].frame, vec, 0
                    );
                    Subtract(vec, vecKeys[j].value, vec);
                    if (Length(vec) > f2) {
                        anim->TransKeys().insert(
                            anim->TransKeys().begin() + i, vecKeys.back()
                        );
                        vecKeys.pop_back();
                        i++;
                        break;
                    }
                }
            }
        }
    }
    if (f3) {
        if (anim->RotKeys().size() > 2) {
            Keys<Hmx::Quat, Hmx::Quat> quatKeys;
            anim->RotKeys().FindBounds(firstFrame, lastFrame, firstFrameIdx, lastFrameIdx);
            for (int i = firstFrameIdx + 1; i < lastFrameIdx - quatKeys.size();) {
                quatKeys.push_back(anim->RotKeys()[i]);
                anim->RotKeys().Remove(i);
                for (int j = 0; j < quatKeys.size(); j++) {
                    Hmx::Quat q;
                    anim->RotKeys().AtFrame(quatKeys[j].frame, q);
                    if (AngleBetween(q, quatKeys[j].value) > f3) {
                        anim->RotKeys().insert(
                            anim->RotKeys().begin() + i, quatKeys.back()
                        );
                        quatKeys.pop_back();
                        i++;
                        break;
                    }
                }
            }
        }
    }
    if (f4) {
        if (anim->ScaleKeys().size() > 2) {
            Keys<Vector3, Vector3> vecKeys;
            anim->ScaleKeys().FindBounds(
                firstFrame, lastFrame, firstFrameIdx, lastFrameIdx
            );
            for (int i = firstFrameIdx + 1; i < lastFrameIdx - vecKeys.size();) {
                vecKeys.push_back(anim->ScaleKeys()[i]);
                anim->ScaleKeys().Remove(i);
                for (int j = 0; j < vecKeys.size(); j++) {
                    Vector3 vec;
                    InterpVector(
                        anim->ScaleKeys(), anim->ScaleSpline(), vecKeys[j].frame, vec, 0
                    );
                    Subtract(vec, vecKeys[j].value, vec);
                    if (Length(vec) > f4) {
                        anim->ScaleKeys().insert(
                            anim->ScaleKeys().begin() + i, vecKeys.back()
                        );
                        vecKeys.pop_back();
                        i++;
                        break;
                    }
                }
            }
        }
    }
}

void TransformKeys(RndTransAnim *tanim, const Transform &tf) {
    Vector3 v48;
    Hmx::Quat q58;
    Hmx::Matrix3 m3c;
    MakeScale(tf.m, v48);
    Scale(tf.m.x, 1.0f / v48.x, m3c.x);
    Scale(tf.m.y, 1.0f / v48.y, m3c.y);
    Scale(tf.m.z, 1.0f / v48.z, m3c.z);
    q58.Set(m3c);
    for (Keys<Vector3, Vector3>::iterator it = tanim->TransKeys().begin();
         it != tanim->TransKeys().end();
         ++it) {
        Multiply(it->value, tf, it->value);
    }
    for (Keys<Vector3, Vector3>::iterator it = tanim->ScaleKeys().begin();
         it != tanim->ScaleKeys().end();
         ++it) {
        Scale(it->value, v48.x, it->value);
    }
    for (Keys<Hmx::Quat, Hmx::Quat>::iterator it = tanim->RotKeys().begin();
         it != tanim->RotKeys().end();
         ++it) {
        Multiply(q58, it->value, it->value);
    }
}

// Swap RGBA byte order in-place for endianness conversion
// Uses pointer arithmetic pattern: base-4 with pixel[1] access for optimal codegen
void EndianSwapBitmap(RndBitmap &bmap) {
    int row = 0;
    int col = 0;
    if (bmap.Height() != 0) {
        do {
            col = 0;
            if (bmap.Width() > 0) {
                // Pointer positioned 4 bytes before row start for pre-increment access
                // This pattern (pixel[1] then pixel++) matches original binary codegen
                u32 *pixel = (u32 *)(bmap.Pixels() + bmap.RowBytes() * row - 4);
                do {
                    u32 val = pixel[1];
                    col++;
                    // Endian swap: exchange bytes 0↔2, preserve bytes 1 and 3
                    // RGBA (0x RRGB BBAA) becomes BGRA (0x BBGG RRAA)
                    pixel[1] = (val >> 16 | val & 0xFFFF0000) >> 8 & 0xFFFF
                        | ((val << 16 | val & 0xFFFF) & 0xFFFF00) << 8;
                    pixel++;
                } while (col < bmap.Width());
            }
            row++;
        } while (row < bmap.Height());
    }
}

void Clip(BuildPoly &bp, const Plane &plane, bool b) {
    Hmx::Ray ray;
    if (fabs(
            bp.mTransform.m.z.x * plane.a + bp.mTransform.m.z.z * plane.c
            + bp.mTransform.m.z.y * plane.b
        )
        <= 0.9999f) {
        Intersect(bp.mTransform, plane, ray);
        if (b) {
            ray.dir.x = -ray.dir.x;
            ray.dir.y = -ray.dir.y;
        }
        Clip(bp.mPoly, ray, bp.mPoly);
    }
}

void ScrambleXfms(RndMultiMesh *mesh) {
    double scrambleMax = 6.2829999923706055;
    double scrambleMin = 0.0;
    double max = 1.0;
    double min = -1.0;
    FOREACH (it, mesh->Instances()) {
        float randZ = RandomFloat(min, max);
        float randY = RandomFloat(min, max);
        float randX = RandomFloat(min, max);
        Vector3 vec(randX, randY, randZ);
        Normalize(vec, vec);
        float scrambler = RandomFloat(scrambleMin, scrambleMax);
        Hmx::Quat q;
        q.Set(vec, scrambler);
        MakeRotMatrix(q, it->mXfm.m);
    }
}

void SortXfms(RndMultiMesh *mesh, const Vector3 &vec) {
    gUtlXfms = vec;
    mesh->Instances().sort(XfmSort);
    mesh->InvalidateProxies();
}

bool XfmSort(RndMultiMesh::Instance &mesh1, RndMultiMesh::Instance &mesh2) {
    return (mesh1.mXfm.v.z - gUtlXfms.z) * (mesh1.mXfm.v.z - gUtlXfms.z)
        + (mesh1.mXfm.v.y - gUtlXfms.y) * (mesh1.mXfm.v.y - gUtlXfms.y)
        + (mesh1.mXfm.v.x - gUtlXfms.x) * (mesh1.mXfm.v.x - gUtlXfms.x)
        < (mesh2.mXfm.v.z - gUtlXfms.z) * (mesh2.mXfm.v.z - gUtlXfms.z)
        + (mesh2.mXfm.v.y - gUtlXfms.y) * (mesh2.mXfm.v.y - gUtlXfms.y)
        + (mesh2.mXfm.v.x - gUtlXfms.x) * (mesh2.mXfm.v.x - gUtlXfms.x);
}

void DistributeXfms(RndMultiMesh *mm, int i, float f) {
    int idx = 0;
    FOREACH (it, mm->Instances()) {
        Vector3 v5c((float)(idx % i) * f, (float)(idx / i) * f, 0);
        Add(it->mXfm.v, v5c, it->mXfm.v);
        ++idx;
    }
}

void MoveXfms(RndMultiMesh *mm, const Vector3 &v) {
    FOREACH (it, mm->Instances()) {
        Add(it->mXfm.v, v, it->mXfm.v);
    }
}

void ScaleXfms(RndMultiMesh *mm, const Vector3 &v) {
    FOREACH (it, mm->Instances()) {
        Scale(v, it->mXfm.m, it->mXfm.m);
    }
}

void RandomXfms(RndMultiMesh *) { MILO_ASSERT(0, 3173); }

void RandomPointOnMesh(RndMesh *m, Vector3 &v1, Vector3 &v2) {
    RndMesh::Face &face = m->Faces()[RandomInt(0, m->Faces().size())];
    int numverts = m->Verts().size();
    if (face.v1 >= numverts || face.v2 >= numverts || face.v3 >= numverts) {
        MILO_NOTIFY_ONCE(
            "%s: %s random face contains unknown vert indices!", PathName(m), m->Name()
        );
        v1.Zero();
        v2.Zero();
    } else {
        Vector3 v58, v64, v70;
        Vector3 v7c, v88, v94;
        if (m->NumBones() > 0) {
            v58 = m->SkinVertex(m->Verts()[face.v1], &v7c);
            v64 = m->SkinVertex(m->Verts()[face.v2], &v88);
            v70 = m->SkinVertex(m->Verts()[face.v3], &v94);
        } else {
            v58 = m->Verts()[face.v1].pos;
            v64 = m->Verts()[face.v2].pos;
            v70 = m->Verts()[face.v3].pos;
            v7c = m->Verts()[face.v1].norm;
            v88 = m->Verts()[face.v2].norm;
            v94 = m->Verts()[face.v3].norm;
        }
        float f8 = RandomFloat();
        float f9 = RandomFloat();
        if (f8 + f9 > 1.0f) {
            f8 = 1.0f - f8;
            f9 = 1.0f - f9;
        }
        float f1 = (1.0f - f8) - f9;
        v58 *= f8;
        v64 *= f9;
        v70 *= f1;
        Add(v58, v64, v1);
        Add(v1, v70, v1);
        v7c *= f8;
        v88 *= f9;
        v94 *= f1;
        Add(v7c, v88, v2);
        Add(v2, v94, v2);
        Normalize(v2, v2);
    }
}

void UtilDrawSphere(const Vector3 &v, float f, const Hmx::Color &col, RndMat *) {
    if (!sSphereMesh) {
        MILO_NOTIFY_ONCE("Sphere mesh is not loaded");
    } else {
        Transform tf58;
        tf58.Reset();
        tf58.v = v;
        Scale(Vector3(f, f, f), tf58.m, tf58.m);
        sSphereMesh->Mat()->SetColor(col.red, col.green, col.blue);
        sSphereMesh->Mat()->SetAlpha(0.2f);
        sSphereMesh->Mat()->SetCull(kCullNone);
        sSphereMesh->SetLocalXfm(tf58);
        sSphereMesh->SetSphere(Sphere(Vector3(0, 0, 0), f));
        sSphereMesh->Draw();
    }
}

void UtilDrawCylinder(
    const Transform &tf, float radius, float height, const Hmx::Color &col, int
) {
    if (!sCylinderMesh) {
        MILO_NOTIFY_ONCE("Cylinder mesh is not loaded");
    } else {
        Transform tf58;
        tf58 = tf;
        Scale(Vector3(radius, height, radius), tf58.m, tf58.m);
        sCylinderMesh->Mat()->SetColor(col.red, col.green, col.blue);
        sCylinderMesh->Mat()->SetAlpha(0.2f);
        sCylinderMesh->Mat()->SetCull(kCullNone);
        sCylinderMesh->SetLocalXfm(tf58);
        sCylinderMesh->Draw();
    }
}

void UtilDrawPlane(
    const Plane &p, const Vector3 &v, const Hmx::Color &c, int i4, float f, bool
) {
    Transform tf88;
    ScaleAdd(v, *(const Vector3 *)&p, -p.Dot(v), tf88.v);
    tf88.m.y = *(const Vector3 *)&p;
    Hmx::Matrix3 mb0;
    mb0.Identity();
    int idx = 0;
    int minIdx = 0;
    float ref = 10000.0f;
    for (; idx < 3; idx++) {
        if (MinEq(ref, Dot(mb0[idx], tf88.m.y))) {
            minIdx = idx;
        }
    }
    Cross(tf88.m.y, mb0[minIdx], tf88.m.z);
    Normalize(tf88.m.z, tf88.m.z);
    Cross(tf88.m.y, tf88.m.z, tf88.m.x);
    for (int i = 0; i < i4; i++) {
        Vector3 vecbc, vecc8, vecd4, vece0;
        float scalar = (float)(i + 1) * f;
        ScaleAdd(tf88.v, tf88.m.x, scalar, vece0);
        ScaleAdd(tf88.v, tf88.m.z, scalar, vecd4);
        float negscalar = -scalar;
        ScaleAdd(tf88.v, tf88.m.x, negscalar, vecc8);
        ScaleAdd(tf88.v, tf88.m.z, negscalar, vecbc);
        TheRnd.DrawLine(vece0, vecd4, c, false);
        TheRnd.DrawLine(vecd4, vecc8, c, false);
        TheRnd.DrawLine(vecc8, vecbc, c, false);
        TheRnd.DrawLine(vecbc, vece0, c, false);
    }
}

void AttachMesh(RndMesh *main, RndMesh *attach) {
    MILO_ASSERT(main && attach, 0x536);
    int nummainfaces = main->Faces().size();
    int numattachfaces = attach->Faces().size();
    main->Faces().resize(nummainfaces + numattachfaces);
    int numverts = main->Verts().size();
    for (int i = 0; i < numattachfaces; i++) {
        RndMesh::Face &curattachface = attach->Faces(i);
        RndMesh::Face &mainface = main->Faces(i + nummainfaces);
        mainface.Set(
            curattachface.v1 + numverts,
            curattachface.v2 + numverts,
            curattachface.v3 + numverts
        );
    }
    Transform tf50;
    FastInvert(main->WorldXfm(), tf50);
    Multiply(attach->WorldXfm(), tf50, tf50);
    int numattachverts = attach->Verts().size();
    main->Verts().resize(numverts + numattachverts);
    for (int i = 0; i < numattachverts; i++) {
        RndMesh::Vert &mainvert = main->Verts(i + numverts);
        RndMesh::Vert &attachvert = attach->Verts(i);
        Multiply(attachvert.pos, tf50, mainvert.pos);
        mainvert.color = attachvert.color;
        mainvert.boneWeights = attachvert.boneWeights;
        mainvert.norm = attachvert.norm;
        mainvert.tex = attachvert.tex;
    }
    main->Sync(0x3F);
}

const char *CacheResource(const char *cc, const Hmx::Object *o) {
    if (!cc || (*cc == '\0'))
        return 0;
    else {
        CacheResourceResult res;
        const char *ret = CacheResource(cc, res);
        if (res > kCacheUnnecessary) {
            switch (res) {
            case kCacheUnknownExtension:
                if (o)
                    MILO_WARN(
                        "%s: \"%s\" has unrecognized extension \"%s\"",
                        PathName(o),
                        cc,
                        FileGetExt(cc)
                    );
                else
                    MILO_WARN(
                        "Unrecognized extension \"%s\" to \"%s\"", FileGetExt(cc), cc
                    );
                break;
            case kCacheMissingFile:
                if (o)
                    MILO_WARN("%s: couldn't find %s", PathName(o), cc);
                else
                    MILO_WARN("Couldn't find %s", cc);
                break;
            default:
                if (o)
                    MILO_WARN("%s: unknown CacheResource error %s", PathName(o), cc);
                else
                    MILO_WARN("Unknown CacheResource error %s", cc);
                break;
            }
        }
        return ret;
    }
}

const char *CacheResource(const char *cc, CacheResourceResult &res) {
    Platform thisPlatform = TheLoadMgr.GetPlatform();
    res = kCacheUnnecessary;
    char buf[256];
    const char *localized = FileLocalize(cc, buf);
    const char *ext = FileGetExt(localized);
    if (stricmp(ext, "bmp") != 0 && stricmp(ext, "png") != 0) {
        const char *movieExt = MovieExtension(ext, thisPlatform);
        if (movieExt) {
            return MakeString(
                "%s/%s.%s", FileGetPath(localized), FileGetBase(localized), movieExt
            );
        } else {
            res = kCacheUnknownExtension;
            return nullptr;
        }
    } else {
        if (TheLoadMgr.GetPlatform() == kPlatformPS3) {
            const char *xboxStr = strstr(localized, "_xbox");
            if (xboxStr) {
                static char *ps3File;
                strcpy(ps3File, localized);
                int ps3Idx = xboxStr - localized;
                strcpy(ps3File + ps3Idx, "_ps3");
                strcpy(ps3File + ps3Idx + 4, xboxStr + 5);
            }
        }
        static char *cacheFile;
        strcpy(
            cacheFile,
            MakeString(
                "%s/gen/%s.%s_%s",
                FileGetPath(localized),
                FileGetBase(localized),
                FileGetExt(localized),
                PlatformSymbol(thisPlatform)
            )
        );
        return cacheFile;
    }
}

DataNode GetNormalMapTextures(ObjectDir *dir) {
    DataArrayPtr ptr(new DataArray(0x100));
    int idx = 0;
    ptr->Node(idx++) = NULL_OBJ;
    for (ObjDirItr<RndTex> it(dir, true); it; ++it) {
        bool b1 = false;
        FilePath fp(it->File());
        if (strstr(FileGetBase(fp.c_str()), "_norm")) {
            b1 = true;
        } else {
            if (fp.empty()) {
                if (it->IsRenderTarget())
                    b1 = true;
            }
        }
        if (b1) {
            ptr->Node(idx++) = DataNode(it);
        }
    }
    ptr->Resize(idx);
    return ptr;
}

DataNode GetTexturesOfType(ObjectDir *dir, RndTex::Type texType) {
    int num = 0;
    for (ObjDirItr<RndTex> it(dir, true); it != 0; ++it) {
        if (texType == (texType & it->GetType())) {
            num++;
        }
    }
    DataArrayPtr ptr(new DataArray(num + 1));
    num = 0;
    for (ObjDirItr<RndTex> it(dir, true); it != 0; ++it) {
        if (texType == (texType & it->GetType())) {
            ptr->Node(num++) = DataNode(it);
        }
    }
    ptr->Node(num) = NULL_OBJ;
    return ptr;
}

DataNode GetRenderTextures(ObjectDir *dir) {
    return GetTexturesOfType(dir, RndTex::kRendered);
}

DataNode GetRenderTexturesNoZ(ObjectDir *dir) {
    return GetTexturesOfType(dir, RndTex::kRenderedNoZ);
}

DataNode OnTestDrawGroups(DataArray *da) {
    DataArray *arr = 0;
    ObjectDir *dir = da->Obj<ObjectDir>(2);
    if (da->Size() > 3)
        arr = da->Array(3);
    for (ObjDirItr<RndDrawable> it(dir, true); it; ++it) {
        std::list<RndGroup *> gList;
        ListDrawGroups(it, dir, gList);
        if (arr) {
            for (std::list<RndGroup *>::iterator gListIt = gList.begin();
                 gListIt != gList.end();) {
                bool canerase = false;
                for (int i = 0; i < arr->Size(); i++) {
                    if (streq((*gListIt)->Name(), arr->Str(i))) {
                        canerase = true;
                        break;
                    }
                }
                if (canerase)
                    gListIt = gList.erase(gListIt);
                else
                    ++gListIt;
            }
        }
        if (gList.size() > 1) {
            String str(MakeString("%s is in %d groups:", PathName(it), (long)gList.size()));
            for (std::list<RndGroup *>::iterator gListIt = gList.begin();
                 gListIt != gList.end();
                 ++gListIt) {
                str << " " << PathName(*gListIt);
            }
            MILO_NOTIFY(str.c_str());
        }
    }
    return 0;
}

void TestTextureSize(ObjectDir *dir, int iType, int i3, int i4, int i5, int maxBpp) {
    bool rendered = false;
    if (iType == RndTex::kRendered || iType == RndTex::kRenderedNoZ)
        rendered = true;
    bool b2 = false;
    if (GetGfxMode() == 0 || rendered)
        b2 = true;
    int ivar4 = 1;
    if (b2)
        ivar4 = i5;
    for (ObjDirItr<RndTex> it(dir, true); it != 0; ++it) {
        if (iType == it->GetType()) {
            int local_bpp = b2 ? it->Bpp() : 1;
            if (rendered && GetGfxMode() == 1 && local_bpp == 0x10)
                local_bpp = 0x20;
            int product = it->Width() * it->Height() * local_bpp;
            if (product > i3 * i4 * ivar4) {
                MILO_WARN(
                    "%s is too big w:%d h:%d bpp:%d",
                    PathName(it),
                    it->Width(),
                    it->Height(),
                    local_bpp
                );
            }
            if (product != 0 && b2 && local_bpp > maxBpp) {
                MILO_WARN("%s is %d bpp > %d, too big", PathName(it), local_bpp, maxBpp);
            }
        }
    }
}

void TestTexturePaths(ObjectDir *dir) {
    String str(FileRoot());
    FileNormalizePath(str.c_str());
    for (ObjDirItr<RndTex> it(dir, true); it != 0; ++it) {
        FilePath fp(it->File());
        if (fp.empty())
            continue;
        String relative(FileRelativePath(FileRoot(), fp.c_str()));
        FileNormalizePath(str.c_str());
        const char *normalized = relative.c_str();
        if (strstr(relative.c_str(), "..") == relative.c_str()) {
            if (strstr(relative.c_str(), "../../system/run") != normalized) {
                MILO_WARN("%s: %s is outside project path", PathName(it), relative);
            }
        }
        const char *normalized2 = relative.c_str();
        if (strlen(normalized2) > 2 && normalized2[1] == ':') {
            MILO_WARN("%s: %s is outside project path", PathName(it), relative);
        }
    }
    if (dir->Loader()) {
        const char *fpstr = dir->Loader()->LoaderFile().c_str();
        const char *ng = strstr(fpstr, "/ng/");
        for (ObjDirItr<RndTex> it(dir, true); it != 0; ++it) {
            const char *texStr = it->File().c_str();
            if (ng == 0 && strstr(texStr, "/ng/") != 0) {
                MILO_WARN("og %s has ng texture %s", fpstr, texStr);
            } else if (ng && strstr(texStr, "/og/") != 0) {
                MILO_WARN("ng %s has og texture %s", fpstr, texStr);
            }
        }
    }
}

void TestMaterialTextures(ObjectDir *) {}

void ConvertBonesToTranses(ObjectDir *dir, bool b) {
    std::list<RndMesh *> meshes;
    for (ObjDirItr<RndMesh> it(dir, true); it != 0; ++it) {
        RndTransformable *itTrans = it;
        if (ShouldStrip(itTrans)) {
            meshes.push_back(it);
        } else {
            if (b) {
                bool b1 = false;
                FOREACH (rit, it->Refs()) {
                    RndMesh *curRefOwner = dynamic_cast<RndMesh *>(rit->RefOwner());
                    if (curRefOwner) {
                        for (int i = 0; i < curRefOwner->NumBones(); i++) {
                            if (curRefOwner->BoneTransAt(i) == itTrans) {
                                meshes.push_back(it);
                                b1 = true;
                                break;
                            }
                        }
                    }
                    if (b1)
                        break;
                }
            }
        }
    }
    while (!meshes.empty()) {
        ReplaceObject(
            meshes.front(), Hmx::Object::New<RndTransformable>(), true, true, true
        );
        meshes.pop_front();
    }
    for (ObjDirItr<RndTransformable> it(dir, true); it != 0; ++it) {
        if (strncmp("spot_", it->Name(), 5) == 0) {
            Normalize(it->LocalXfm().m, it->DirtyLocalXfm().m);
        }
    }
}

void SetBloomBlurWeights(bool, float, float) {}

void SetBloomBlurWeightsStreak(bool, float, float, float, int, float) {}

const char *ResourceFileCacheHelper::CacheFile(const char *cc) {
    return CacheResource(cc, (const Hmx::Object *)0);
}

bool RndAmbientOcclusion::Edge::operator<(const Edge &e) const {
    unsigned short a1 = v1, a0 = v0;
    unsigned int a;
    if (a0 < a1) {
        a = ((unsigned int)a0 << 16) | a1;
    } else {
        a = ((unsigned int)a1 << 16) | a0;
    }
    unsigned short b1 = e.v1, b0 = e.v0;
    unsigned int b;
    if (b0 < b1) {
        b = ((unsigned int)b0 << 16) | b1;
    } else {
        b = ((unsigned int)b1 << 16) | b0;
    }
    return a < b;
}

#include "rndobj/CamAnim.h"

void RndScaleObject(Hmx::Object *obj, float scale, float fovScale) {
    RndDrawable *draw = dynamic_cast<RndDrawable *>(obj);
    if (draw) {
        Sphere s = draw->GetSphere();
        s.center *= scale;
        s.radius *= scale;
        draw->SetSphere(s);
    }
    RndTransformable *trans = dynamic_cast<RndTransformable *>(obj);
    if (trans) {
        Vector3 pos;
        Scale(trans->LocalXfm().v, scale, pos);
        trans->SetLocalPos(pos);
    }
    RndCam *cam = dynamic_cast<RndCam *>(obj);
    if (cam) {
        cam->SetFrustum(cam->NearPlane() * scale, cam->FarPlane() * scale, cam->YFov(), 1.0f);
        return;
    }
    RndCamAnim *camanim = dynamic_cast<RndCamAnim *>(obj);
    if (camanim) {
        if (camanim->KeysOwner() == camanim) {
            ScaleFrame(camanim->FovKeys(), fovScale);
        }
        return;
    }
}
