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
#include <set>
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
std::list<BuildPoly> gChildPolys;
std::list<BuildPoly> gParentPolys;
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

void UtilDrawCircle2D(const Vector2 &center, float radius, const Hmx::Color &color, int segments) {
    std::vector<Vector2> pts(segments + 1);
    float aspect = TheRnd.YRatio();
    for (int i = 0; i <= segments; i++) {
        float angle = (float)i * 6.2831854820251465f / (float)segments;
        float cosVal = FastSin(angle + 1.5707963705062866f);
        float sinVal = FastSin(angle);
        pts[i].x = cosVal * aspect * radius + center.x;
        pts[i].y = sinVal * radius + center.y;
    }
    for (int i = 0; i < segments; i++) {
        UtilDrawLine(pts[i], pts[i + 1], color);
    }
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
    const auto& _ref1 = mesh2;
    return (mesh1.mXfm.v.z - gUtlXfms.z) * (mesh1.mXfm.v.z - gUtlXfms.z)
        + (mesh1.mXfm.v.y - gUtlXfms.y) * (mesh1.mXfm.v.y - gUtlXfms.y)
        + (mesh1.mXfm.v.x - gUtlXfms.x) * (mesh1.mXfm.v.x - gUtlXfms.x)
        < ((_ref1.mXfm.v.y - gUtlXfms.y) * (_ref1.mXfm.v.y - gUtlXfms.y) + ((_ref1.mXfm.v.x - gUtlXfms.x) * (_ref1.mXfm.v.x - gUtlXfms.x) + (_ref1.mXfm.v.z - gUtlXfms.z) * (_ref1.mXfm.v.z - gUtlXfms.z)));
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
        Vector3 pos1, pos2, pos3;
        Vector3 norm1, norm2, norm3;
        if (m->NumBones() > 0) {
            pos1 = m->SkinVertex(m->Verts()[face.v1], &norm1);
            pos2 = m->SkinVertex(m->Verts()[face.v2], &norm2);
            pos3 = m->SkinVertex(m->Verts()[face.v3], &norm3);
        } else {
            pos1 = m->Verts()[face.v1].pos;
            pos2 = m->Verts()[face.v2].pos;
            pos3 = m->Verts()[face.v3].pos;
            norm1 = m->Verts()[face.v1].norm;
            norm2 = m->Verts()[face.v2].norm;
            norm3 = m->Verts()[face.v3].norm;
        }
        float baryU = RandomFloat();
        float baryV = RandomFloat();
        if (baryU + baryV > 1.0f) {
            baryU = 1.0f - baryU;
            baryV = 1.0f - baryV;
        }
        float baryW = (1.0f - baryU) - baryV;
        pos1 *= baryU;
        pos2 *= baryV;
        pos3 *= baryW;
        Add(pos1, pos2, v1);
        Add(v1, pos3, v1);
        norm1 *= baryU;
        norm2 *= baryV;
        norm3 *= baryW;
        Add(norm1, norm2, v2);
        Add(v2, norm3, v2);
        Normalize(v2, v2);
    }
}

void UtilDrawSphere(const Vector3 &v, float f, const Hmx::Color &col, RndMat *) {
    if (!sSphereMesh) {
        MILO_NOTIFY_ONCE("Sphere mesh is not loaded");
    } else {
        Transform tf58;
        tf58.Reset();
        Scale(Vector3(f, f, f), tf58.m, tf58.m);
        tf58.v = v;
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

void UtilDrawCigar(
    const Transform &tf, const float *const radii, const float *const lengths,
    const Hmx::Color &col, int segments
) {
    if (!(!sCylinderMesh)) {
        Vector3 scaledLengths;
        float len = Length(*(const Vector3 *)lengths);
        for (int i = 0; i < 3; i++) {
            (&scaledLengths.x)[i] = radii[i] * len;
        }
        Hmx::Matrix3 basis;
        memcpy(&basis, lengths, sizeof(Hmx::Matrix3));
        Normalize(*(Vector3 *)&basis, *(Vector3 *)&basis);

        Vector3 bottom, top;
        bottom.Set(0, scaledLengths.x - radii[0], 0);
        top.Set(0, scaledLengths.y + radii[1], 0);
        Multiply(bottom, (const Transform &)basis, bottom);
        Multiply(top, (const Transform &)basis, top);

        int i = 0;
        if (segments > 0) {
            float angleStep = 6.2831854820251465 / (float)segments;
            float angle = 1.5707963705062866f;
            do {
                float sinVal = FastSin(angle + angleStep);
                float cosVal = FastSin(angle);
                Vector3 p1, p2, p3, p4;

                p1.Set(cosVal * radii[0], sinVal * radii[0], 0);
                Multiply(p1, (const Transform &)basis, p1);
                Add(p1, bottom, p1);

                p2.Set(cosVal * radii[1], sinVal * radii[1], 0);
                Multiply(p2, (const Transform &)basis, p2);
                Add(p2, top, p2);

                float nextSin = FastSin(angle + angleStep + angleStep);
                float nextCos = FastSin(angle + angleStep);

                p3.Set(nextCos * radii[0], nextSin * radii[0], 0);
                Multiply(p3, (const Transform &)basis, p3);
                Add(p3, bottom, p3);

                p4.Set(nextCos * radii[1], nextSin * radii[1], 0);
                Multiply(p4, (const Transform &)basis, p4);
                Add(p4, top, p4);

                TheRnd.DrawLine(p1, p2, col, false);
                TheRnd.DrawLine(p1, p3, col, false);
                TheRnd.DrawLine(p2, p4, col, false);

                angle += angleStep;
                i++;
            } while (i < segments);
        }
    } else {
        MILO_NOTIFY("Cylinder mesh is not loaded");
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
    int minIdx = 0;
    int idx = 0;
    float minDotProduct = 10000.0f;
    for (; idx < 3; idx++) {
        if (MinEq(minDotProduct, Dot(mb0[idx], tf88.m.y))) {
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
    int idx = 0;
    DataArrayPtr ptr(new DataArray(0x100));
    ptr->Node(idx++) = NULL_OBJ;
    for (ObjDirItr<RndTex> it(dir, true); it; ++it) {
        bool isNormalMapOrRenderTarget = false;
        FilePath fp(it->File());
        if (strstr(FileGetBase(fp.c_str()), "_norm")) {
            isNormalMapOrRenderTarget = true;
        } else {
            if (fp.empty()) {
                if (it->IsRenderTarget())
                    isNormalMapOrRenderTarget = true;
            }
        }
        if (isNormalMapOrRenderTarget) {
            auto texNode = DataNode(it);
            ptr->Node(idx++) = texNode;
        }
    }
    ptr->Resize(idx);
    return ptr;
}

DataNode GetTexturesOfType(ObjectDir *dir, RndTex::Type texType) {
    int num = 0;
    for (ObjDirItr<RndTex> it(dir, true); it != 0; ++it) {
        if ((texType & it->GetType()) == texType) {
            num++;
        }
    }
    DataArrayPtr ptr(new DataArray(num + 1));
    num = 0;
    for (ObjDirItr<RndTex> it(dir, true); it != 0; ++it) {
        if ((texType & it->GetType()) == texType) {
            DataNode texNode = DataNode(it);
            ptr->Node(num++) = texNode;
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
                bool shouldErase = false;
                for (int i = 0; i < arr->Size(); i++) {
                    if (streq((*gListIt)->Name(), arr->Str(i))) {
                        shouldErase = true;
                        break;
                    }
                }
                if (shouldErase)
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
    bool shouldCheckBpp = false;
    if (GetGfxMode() == 0 || rendered)
        shouldCheckBpp = true;
    int scaleFactor = 1;
    if (shouldCheckBpp)
        scaleFactor = i5;
    for (ObjDirItr<RndTex> it(dir, true); it != 0; ++it) {
        if (iType == it->GetType()) {
            int local_bpp = shouldCheckBpp ? it->Bpp() : 1;
            if (rendered && GetGfxMode() == 1 && local_bpp == 0x10)
                local_bpp = 0x20;
            int product = it->Width() * it->Height() * local_bpp;
            if (product > i3 * i4 * scaleFactor) {
                MILO_WARN(
                    "%s is too big w:%d h:%d bpp:%d",
                    PathName(it),
                    it->Width(),
                    it->Height(),
                    local_bpp
                );
            }
            if (product != 0 && shouldCheckBpp && local_bpp > maxBpp) {
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

void TestMaterialTextures(ObjectDir *dir) {
    for (ObjDirItr<RndMat> it(dir, false); it != 0; ++it) {
        RndTex *normMap = it->NormalMap();
        if (normMap) {
            FilePath fp(normMap->File());
            if (!normMap->IsRenderTarget() && !strstr(fp.c_str(), "_norm")) {
                MILO_NOTIFY(
                    "normal map %s used by %s must have _norm in the filename",
                    PathName(normMap),
                    PathName(it)
                );
            }
        }
    }
}

void ComputeFaceTangentBasis(RndMesh *m, int faceIdx, Hmx::Matrix3 &outBasis);

void MakeTangentsLate(RndMesh *m) {
    if (!m) return;
    RndMesh *geom = m->GetGeomOwner();
    if (geom != m || geom->Verts().size() == 0) return;
    if (GetGfxMode() == kOldGfx) return;

    // Build per-face tangent array
    Vector4 zeroTangent(0, 0, 0, 0);
    std::vector<Vector4> faceTangents(m->Faces().size(), zeroTangent);
    double posW = 1.0;
    double negW = -1.0;
    for (unsigned int i = 0; i < m->Faces().size(); i++) {
        Hmx::Matrix3 basis;
        ComputeFaceTangentBasis(m, i, basis);
        double w = posW;
        if ((basis.x.z * basis.z.y - basis.z.z * basis.x.y) * basis.y.x +
            basis.y.y * (basis.z.z * basis.x.x - basis.x.z * basis.z.x) +
            basis.y.z * (basis.x.y * basis.z.x - basis.z.y * basis.x.x) < 0.0) {
            w = negW;
        }
        faceTangents[i].w = (float)w;
        Normalize(basis.x, *(Vector3*)&faceTangents[i]);
    }

    // Accumulate tangents per vertex
    double zeroThresh = 0.0;
    for (int i = 0; i < (int)m->Verts().size(); i++) {
        RndMesh::Vert& v = m->Verts()[i];
        bool first = true;
        for (unsigned int f = 0; f < m->Faces().size(); f++) {
            RndMesh::Face& face = m->Faces()[f];
            int k;
            for (k = 0; k < 3; k++) {
                if (face[k] == i) break;
            }
            if (3 != k) {
                if (first) {
                    first = false;
                    v.tangent = faceTangents[f];
                } else {
                    if ((double)(faceTangents[f].w * v.tangent.w) < zeroThresh) {
                        auto notifyMsg = MakeString("NOTIFY: %s has previously welded vertex tangents with opposite handedness; re-export from Max for more accurate normal mapping.\n", (char*)PathName(m));
                        TheDebug << notifyMsg;
                    } else {
                        v.tangent.x += faceTangents[f].x;
                        v.tangent.y += faceTangents[f].y;
                        v.tangent.z += faceTangents[f].z;
                    }
                }
            }
        }
        Normalize(*(Vector3*)&v.tangent, *(Vector3*)&v.tangent);

        // Gram-Schmidt orthogonalize tangent against normal
        float tx = v.tangent.x, ty = v.tangent.y, tz = v.tangent.z;
        float tDotN = v.norm.x * tx + v.norm.z * tz + v.norm.y * ty;
        float scaleX = v.norm.x * tDotN;
        float scaleY = v.norm.y * tDotN;
        float scaleZ = v.norm.z * tDotN;
        float ox = tx - scaleX;
        float oy = ty - scaleY;
        float oz = tz - scaleZ;
        Normalize(*(Vector3*)&ox, *(Vector3*)&v.tangent);
    }
    TheDebug << MakeString("NOTIFY: %s MakingTangentsLate, resave this file!", (char*)PathName(m));
}

void ComputeFaceTangentBasis(RndMesh *m, int faceIdx, Hmx::Matrix3 &outBasis) {
    if (!m) {
        MILO_ASSERT(m, 0x250);
    }
    outBasis.x.x = 1.0f; outBasis.x.y = 0.0f; outBasis.x.z = 0.0f;
    outBasis.y.x = 0.0f; outBasis.y.y = 1.0f; outBasis.y.z = 0.0f;
    outBasis.z.x = 0.0f; outBasis.z.y = 0.0f; outBasis.z.z = 1.0f;

    RndMesh::Face &face = m->Faces()[faceIdx];
    if (face.v1 != face.v2 && face.v2 != face.v3) {
        if (face.v3 != face.v1) {
        RndMesh::Vert &vert1 = m->Verts()[face.v1];
        RndMesh::Vert &vert2 = m->Verts()[face.v2];
        RndMesh::Vert &vert3 = m->Verts()[face.v3];

        if (!BadUV(vert1.tex) && !BadUV(vert2.tex) && !BadUV(vert3.tex)) {
            float dx21 = vert2.pos.x - vert1.pos.x;
            float dz21 = vert2.pos.z - vert1.pos.z;
            float dy21 = vert2.pos.y - vert1.pos.y;
            float dx31 = vert3.pos.x - vert1.pos.x;
            float dy31 = vert3.pos.y - vert1.pos.y;
            float dz31 = vert3.pos.z - vert1.pos.z;

            float du21 = vert2.tex.x - vert1.tex.x;
            float dv21 = vert2.tex.y - vert1.tex.y;
            float du31 = vert3.tex.x - vert1.tex.x;
            float dv31 = vert3.tex.y - vert1.tex.y;

            if (dx21 == 0.0f && dy21 == 0.0f && dz21 == 0.0f) return;
            if (dx31 == 0.0f && dy31 == 0.0f & dz31 == 0.0f) return;
            if (du21 == 0.0f && dv21 == 0.0f) return;
            if (du31 == 0.0f && dv31 == 0.0f) return;

            float crossX = dz31 * dy21 - dy31 * dz21;
            float crossY = dz31 * dx21 - dx31 * dz21;
            float crossZ = dy31 * dx21 - dx31 * dy21;
            Hmx::Matrix3 edgeMat(Vector3(dx21, dy21, dz21),
                                  Vector3(dx31, dy31, dz31),
                                  Vector3(crossX, crossY, crossZ));

            Invert(edgeMat, edgeMat);

            // Transpose the inverted edge matrix
            float swapXY = edgeMat.x.y; edgeMat.x.y = edgeMat.y.x; edgeMat.y.x = swapXY;
            float swapXZ = edgeMat.x.z; edgeMat.x.z = edgeMat.z.x; edgeMat.z.x = swapXZ;
            float swapYZ = edgeMat.y.z; edgeMat.y.z = edgeMat.z.y; edgeMat.z.y = swapYZ;

            Hmx::Matrix3 texMat;
            texMat.x.Set(du21, du31, 0.0f);
            texMat.y.Set(dv21, dv31, 0.0f);
            texMat.z.Set(0.0f, 0.0f, 1.0f);

            Multiply(texMat, edgeMat, outBasis);
            return;
        }
        TheDebug << MakeString("NOTIFY: %s has bad UVs, should reexport from Max\n", (char*)PathName(m));
    }
    }
}

void MakeNormals(RndMesh *m) {
    if (!m || m->GetGeomOwner() != m || m->Verts().size() == 0) return;

    bool leftHanded = LeftHanded(m->WorldXfm().m);

    int numVerts = m->Verts().size();
    std::vector<int> repVerts(numVerts);
    for (int i = 0; i < m->Verts().size(); i++) {
        const Vector3& pos = m->Verts()[i].pos;
        int rep = i;
        for (int j = 0; (unsigned int)j < i; j++) {
            const Vector3& otherPos = m->Verts()[j].pos;
            if (fabsf(pos.x - otherPos.x) <= 0.001f &&
                fabs(pos.y - otherPos.y) <= 0.001f &&
                fabs(pos.z - otherPos.z) <= 0.001f) {
                rep = j;
                break;
            }
        }
        repVerts[i] = rep;
    }

    for (int i = 0; i < m->Verts().size(); i++) {
        m->Verts()[i].norm.Zero();

        int rep = repVerts[i];
        for (int f = 0; f < m->Faces().size(); f++) {
            RndMesh::Face& face = m->Faces()[f];
            int k;
            for (k = 0; k < 3; k++) {
                if ((unsigned int)repVerts[face[k]] == rep)
                    break;
            }
            if (k != 3) {
                const RndMesh::Vert& v0 = m->Verts()[face[k]];
                const RndMesh::Vert& v1 = m->Verts()[face[(k+1)%3]];
                const RndMesh::Vert& v2 = m->Verts()[face[(k+2)%3]];

                Vector3 e1(v1.pos.x - v0.pos.x, v1.pos.y - v0.pos.y, v1.pos.z - v0.pos.z);
                Vector3 e2(v2.pos.x - v0.pos.x, v2.pos.y - v0.pos.y, v2.pos.z - v0.pos.z);

                if (e1.x != 0 || e1.y != 0 || e1.z != 0) {
                    if (e2.x != 0 || e2.y != 0 || e2.z != 0) {
                        if (e1.x != e2.x || e1.y != e2.y || e1.z != e2.z) {
                            Vector3 crossProd(e2.z * e1.y - e2.y * e1.z, e2.x * e1.z - e2.z * e1.x, e2.y * e1.x - e2.x * e1.y);
                            Normalize(crossProd, crossProd);
                            Normalize(e1, e1);
                            Normalize(e2, e2);
                            float angle = (float)acos((double)(e2.x * e1.x + e2.y * e1.y + e2.z * e1.z));

                            m->Verts()[i].norm.x += crossProd.x * angle;
                            m->Verts()[i].norm.y += crossProd.y * angle;
                            m->Verts()[i].norm.z += crossProd.z * angle;
                        }
                    }
                }
            }
        }
        Normalize(m->Verts()[i].norm, m->Verts()[i].norm);

        if (leftHanded) {
            m->Verts()[i].norm.x = -m->Verts()[i].norm.x;
            m->Verts()[i].norm.y = -m->Verts()[i].norm.y;
            m->Verts()[i].norm.z = -m->Verts()[i].norm.z;
        }
    }
    m->Sync(0x1F);
}

void ResetNormals(RndMesh *m) {
    auto zeroVec = Vector4(0, 0, 0, 0);
    if (!m || m->GetGeomOwner() != m || m->Verts().size() == 0) return;


    bool leftHanded = LeftHanded(m->WorldXfm().m);
    std::vector<Vector4> faceTangents(m->Faces().size(), zeroVec);

    for (int i = 0; i < m->Faces().size(); i++) {
        Hmx::Matrix3 basis;
        ComputeFaceTangentBasis(m, i, basis);

        float crossX = basis.x.y * basis.z.x - basis.z.y * basis.x.x;
        float crossY = basis.z.z * basis.x.x - basis.x.z * basis.z.x;
        float crossZ = basis.x.z * basis.z.y - basis.z.z * basis.x.y;
        Normalize(basis.x, *(Vector3*)&faceTangents[i]);
        float w = ((crossZ * basis.y.x + (basis.y.y * crossY + basis.y.z * crossX)) < 0.0f) ? -1.0f : 1.0f;
        faceTangents[i].w = w;
    }

    int numVerts = m->Verts().size();
    std::vector<int> repVerts(numVerts);
    for (int i = 0; i < m->Verts().size(); i++) {
        const Vector3& pos = m->Verts()[i].pos;
        int rep = i;
        for (int j = 0; j < i; j++) {
            const Vector3& otherPos = m->Verts()[j].pos;
            if (fabs(pos.x - otherPos.x) <= 0.001f &&
                fabs(pos.y - otherPos.y) <= 0.001f &&
                fabs(pos.z - otherPos.z) <= 0.001f) {
                rep = j;
                break;
            }
        }
        repVerts[i] = rep;
    }

    for (int i = 0; i < m->Verts().size(); i++) {
        RndMesh::Vert& v = m->Verts()[i];
        v.norm.Zero();
        v.tangent.Set(0, 0, 0, 0);

        int rep = repVerts[i];
        for (int f = 0; f < m->Faces().size(); f++) {
            RndMesh::Face& face = m->Faces()[f];
            for (int k = 0; k <= 2; k++) {
                if ((unsigned int)repVerts[face[k]] == rep) {
                    const RndMesh::Vert& v0 = m->Verts()[face[k]];
                    const RndMesh::Vert& v1 = m->Verts()[face[(k+1)%3]];
                    const RndMesh::Vert& v2 = m->Verts()[face[(k+2)%3]];

                    float dx1 = v1.pos.x - v0.pos.x;
                    float dy1 = v1.pos.y - v0.pos.y;
                    float dz1 = v1.pos.z - v0.pos.z;
                    float dx2 = v2.pos.x - v0.pos.x;
                    float dy2 = v2.pos.y - v0.pos.y;
                    float dz2 = v2.pos.z - v0.pos.z;

                    if (dx1 != 0 || dy1 != 0 || dz1 != 0) {
                        if (dx2 != 0 || dy2 != 0 || dz2 != 0) {
                            if (dx1 != dx2 || dy1 != dy2 || dz1 != dz2) {
                                Vector3 crossProd(dz2 * dy1 - dy2 * dz1, dx2 * dz1 - dz2 * dx1, -(dx2 * dy1 - dy2 * dx1));
                                Normalize(crossProd, crossProd);
                                Vector3 e1(dx1, dy1, dz1), e2(dx2, dy2, dz2);
                                Normalize(e1, e1);
                                Normalize(e2, e2);
                                float angle = (float)acos((double)(e2.x * e1.x + e2.y * e1.y + e2.z * e1.z));

                                v.norm.x += crossProd.x * angle;
                                v.norm.y += crossProd.y * angle;
                                v.norm.z += crossProd.z * angle;

                                Vector4& ft = faceTangents[f];
                                v.tangent.x += ft.x * angle;
                                v.tangent.y += ft.y * angle;
                                v.tangent.z += ft.z * angle;
                            }
                        }
                    }
                }
            }
        }
        Normalize(v.norm, v.norm);
        Normalize(*(Vector3*)&v.tangent, *(Vector3*)&v.tangent);

        if (leftHanded) {
            v.norm.x = -v.norm.x; v.norm.y = -v.norm.y; v.norm.z = -v.norm.z;
            v.tangent.x = -v.tangent.x; v.tangent.y = -v.tangent.y; v.tangent.z = -v.tangent.z;
        }

        float tx = v.tangent.x, ty = v.tangent.y, tz = v.tangent.z;
        float tDotN = v.norm.x * tx + v.norm.z * tz + v.norm.y * ty;
        Vector3 ortho(tx - v.norm.x * tDotN, ty - v.norm.y * tDotN, tz - v.norm.z * tDotN);
        Normalize(ortho, *(Vector3*)&v.tangent);
    }
    m->Sync(0x1F);
}

void ConvertBonesToTranses(ObjectDir *dir, bool b) {
    std::list<RndMesh *> meshes;
    for (ObjDirItr<RndMesh> it(dir, true); it != 0; ++it) {
        RndTransformable *itTrans = it;
        if (ShouldStrip(itTrans)) {
            meshes.push_back(it);
        } else {
            if (b) {
                bool foundBoneRef = false;
                auto refs = it->Refs();
                FOREACH (rit, refs) {
                    RndMesh *curRefOwner = dynamic_cast<RndMesh *>(rit->RefOwner());
                    if (curRefOwner) {
                        for (int i = 0; i < curRefOwner->NumBones(); i++) {
                            if (curRefOwner->BoneTransAt(i) == itTrans) {
                                meshes.push_back(it);
                                foundBoneRef = true;
                                break;
                            }
                        }
                    }
                    if (foundBoneRef)
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

#ifndef HX_NATIVE
bool RndAmbientOcclusion::Edge::operator<(const Edge &e) const {
    unsigned short aMax = v1, aMin = v0;
    unsigned int a;
    if (aMin < aMax) {
        a = ((unsigned int)aMin << 16) | aMax;
    } else {
        a = ((unsigned int)aMax << 16) | aMin;
    }
    unsigned short bMax = e.v1, bMin = e.v0;
    unsigned int b;
    if (bMin < bMax) {
        b = ((unsigned int)bMin << 16) | bMax;
    } else {
        b = ((unsigned int)bMax << 16) | bMin;
    }
    return a < b;
}
#endif

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

void FixVertOrder(const RndMesh *src, RndMesh *dst) {
    int mismatchCount = 0;
    RndMesh::VertVector &srcVerts = const_cast<RndMesh *>(src)->Verts();
    RndMesh::VertVector &dstVerts = dst->Verts();
    int srcCount = srcVerts.mNumVerts;
    float tolerance = 1e-5f;
    if (srcCount > 0) {
        unsigned int i = 0;
        do {
            unsigned int j = 0;
            float stx = srcVerts.mVerts[i].tex.x;
            float sty = srcVerts.mVerts[i].tex.y;
            if (dstVerts.mNumVerts > 0) {
                do {
                    if (fabsf(stx - dstVerts.mVerts[j].tex.x) < tolerance &&
                        fabsf(sty - dstVerts.mVerts[j].tex.y) < tolerance)
                        goto found;
                    j++;
                } while ((int)j < dstVerts.mNumVerts);
            }
            j = (unsigned int)-1;
        found:
            if (!((int)j == -1)) {
                unsigned short ii = (unsigned short)i;
                unsigned short js = (unsigned short)j;
                if (js != ii) {
                    RndMesh::Vert tmp;
                    memcpy(&tmp, &dstVerts.mVerts[js], sizeof(RndMesh::Vert));
                    memcpy(&dstVerts.mVerts[js], &dstVerts.mVerts[ii], sizeof(RndMesh::Vert));
                    memcpy(&dstVerts.mVerts[ii], &tmp, sizeof(RndMesh::Vert));
                    int numFaces = (int)dst->Faces().size();
                    if (numFaces > 0) {
                        unsigned short *faceData = &dst->Faces()[0].v1;
                        int n = numFaces;
                        do {
                            if (faceData[0] == js) faceData[0] = ii;
                            else if (faceData[0] == ii) faceData[0] = js;
                            if (faceData[1] == js) faceData[1] = ii;
                            else if (faceData[1] == ii) faceData[1] = js;
                            if (faceData[2] == js) faceData[2] = ii;
                            else if (faceData[2] == ii) faceData[2] = js;
                            faceData += 3;
                            n--;
                        } while (n != 0);
                    }
                }
            } else {
                mismatchCount++;
            }
            i++;
        } while ((int)i < srcCount);
        if (mismatchCount != 0) {
            TheDebug << MakeString("%s has %d mismatched verts\n", PathName(src), mismatchCount);
        }
    }
}

void BurnXfm(RndMesh *mesh, bool keepTranslation) {
    Transform xfm = mesh->LocalXfm();
    if (keepTranslation) {
        xfm.v.Zero();
    }
    Hmx::Matrix3 normalMat;
    Invert(xfm.m, normalMat);

    float xy = normalMat.x.y;
    normalMat.x.y = normalMat.y.x;
    normalMat.y.x = xy;

    float xz = normalMat.x.z;
    normalMat.x.z = normalMat.z.x;
    normalMat.z.x = xz;

    float yz = normalMat.y.z;
    normalMat.y.z = normalMat.z.y;
    normalMat.z.y = yz;

    for (RndMesh::Vert *it = mesh->Verts().begin(); it != mesh->Verts().end(); it++) {
        Multiply(it->pos, xfm, it->pos);
        Multiply(it->norm, normalMat, it->norm);
        Normalize(it->norm, it->norm);
        Vector3 tangent(it->tangent.x, it->tangent.y, it->tangent.z);
        Multiply(tangent, normalMat, tangent);
        Normalize(tangent, tangent);
        it->tangent.x = tangent.x;
        it->tangent.y = tangent.y;
        it->tangent.z = tangent.z;
    }
    mesh->Sync(0x1F);
    if (mesh->GetBSPTree()) {
        MultiplyEq(mesh->GetBSPTree(), xfm);
    }
    Sphere s;
    Multiply(mesh->GetSphere(), xfm, s);
    mesh->SetSphere(s);

    Transform ident;
    ident.Reset();
    if (keepTranslation) {
        ident.v = mesh->LocalXfm().v;
    }
    mesh->SetLocalXfm(ident);
}

void TessellateMesh(RndMesh *mesh) {
    typedef RndAmbientOcclusion::Edge Edge;
    std::set<Edge> edges;
    RndMesh *geomOwner = mesh->GetGeomOwner();
    std::vector<RndMesh::Vert> newVerts;

    std::vector<RndMesh::Face> newFaces;

    auto _tmp0 = geomOwner->Faces().size();
    newFaces.reserve(_tmp0 * 4);
    auto vertCount = geomOwner->Verts().size();
    newVerts.reserve(vertCount * 3);

    unsigned short nextVert = (unsigned short)geomOwner->Verts().size();

    for (unsigned int i = 0; i < (unsigned int)geomOwner->Faces().size(); i++) {
        RndMesh::Face &face = geomOwner->Faces()[i];
        unsigned short v2 = face.v2;
        unsigned short v1 = face.v1;
        unsigned short v3 = face.v3;

        int vertsBase = (int)(unsigned int)geomOwner->Verts().mVerts;

        RndMesh::Vert *pv1 = (RndMesh::Vert *)((unsigned int)v1 * 0x60 + vertsBase);
        RndMesh::Vert *pv2 = (RndMesh::Vert *)((unsigned int)v2 * 0x60 + vertsBase);
        RndMesh::Vert *pv3 = (RndMesh::Vert *)((unsigned int)v3 * 0x60 + vertsBase);

        RndMesh::Vert blend12, blend23, blend31;
        RndAmbientOcclusion::BlendVert(*pv1, *pv2, blend12);
        RndAmbientOcclusion::BlendVert(*pv2, *pv3, blend23);
        RndAmbientOcclusion::BlendVert(*pv3, *pv1, blend31);

        unsigned short mid12, mid23, mid31;

        // Edge v1-v2
        Edge e12;
        e12.v0 = v1;
        e12.v1 = v2;
        e12.midpoint = -1;
        std::set<Edge>::iterator it12 = edges.find(e12);
        if (it12 == edges.end()) {
            mid12 = nextVert++;
            e12.midpoint = mid12;
            edges.insert(e12);
            newVerts.push_back(blend12);
        } else {
            mid12 = it12->midpoint;
        }

        // Edge v2-v3
        Edge e23;
        e23.v0 = v2;
        e23.v1 = v3;
        e23.midpoint = -1;
        std::set<Edge>::iterator it23 = edges.find(e23);
        if (it23 == edges.end()) {
            mid23 = nextVert++;
            e23.midpoint = mid23;
            edges.insert(e23);
            newVerts.push_back(blend23);
        } else {
            mid23 = it23->midpoint;
        }

        // Edge v3-v1
        Edge e31;
        e31.v0 = v3;
        e31.v1 = v1;
        e31.midpoint = -1;
        std::set<Edge>::iterator it31 = edges.find(e31);
        if (it31 == edges.end()) {
            mid31 = nextVert++;
            e31.midpoint = mid31;
            edges.insert(e31);
            newVerts.push_back(blend31);
        } else {
            mid31 = it31->midpoint;
        }

        // 4 sub-faces
        RndMesh::Face f1, f2, f3, f4;
        f1.Set(v1, mid12, mid31);
        f2.Set(mid31, mid12, mid23);
        f3.Set(mid12, v2, mid23);
        f4.Set(mid23, v3, mid31);
        newFaces.push_back(f1);
        newFaces.push_back(f2);
        newFaces.push_back(f3);
        newFaces.push_back(f4);
    }

    // Replace faces
    geomOwner->Faces().assign(newFaces.begin(), newFaces.end());

    // Expand verts and copy new midpoint verts
    int origNumVerts = geomOwner->Verts().size();
    geomOwner->Verts().resize(origNumVerts + (int)newVerts.size());

    if ((unsigned int)origNumVerts < (unsigned int)(nextVert & 0xFFFF)) {
        int offset = origNumVerts * 0x60;
        int count = (nextVert & 0xFFFF) - origNumVerts;
        RndMesh::Vert *src = &newVerts[0];
        do {
            memcpy(
                (void *)((int)(unsigned int)geomOwner->Verts().mVerts + offset),
                src,
                sizeof(RndMesh::Vert)
            );
            count--;
            offset += 0x60;
            src++;
        } while (count != 0);
    }

    mesh->Sync(0x3f);
}

void BuildVisit(BSPNode *node) {
    if (node == NULL)
        return;

    BuildPoly newPoly;
    newPoly.mPoly.points.clear();
    gParentPolys.push_back(newPoly);

    std::list<BuildPoly>::iterator lastIt = gParentPolys.end();
    --lastIt;
    BuildPoly &poly = *lastIt;

    Plane &plane = node->plane;
    float lenSq = plane.a * plane.a + plane.b * plane.b + plane.c * plane.c;
    float invDist = -(plane.d / lenSq);

    poly.mTransform.v.y = plane.b * invDist;
    poly.mTransform.v.x = plane.a * invDist;
    poly.mTransform.v.z = plane.c * invDist;

    poly.mTransform.m.z.y = plane.b;
    poly.mTransform.m.z.x = plane.a;
    poly.mTransform.m.z.z = plane.c;

    poly.mTransform.m.y.Set(0, 1, 0);

    if (fabsf(poly.mTransform.m.z.x * 0.0f + poly.mTransform.m.z.z * 0.0f
              + poly.mTransform.m.z.y * 1.0f)
        > 0.9f) {
        poly.mTransform.m.y.Set(1, 0, 0);
    }

    // x = y cross z
    poly.mTransform.m.x.x =
        poly.mTransform.m.y.y * poly.mTransform.m.z.z
        - poly.mTransform.m.y.z * poly.mTransform.m.z.y;
    poly.mTransform.m.x.y =
        poly.mTransform.m.z.x * poly.mTransform.m.y.z
        - poly.mTransform.m.z.z * poly.mTransform.m.y.x;
    poly.mTransform.m.x.z =
        poly.mTransform.m.y.x * poly.mTransform.m.z.y
        - poly.mTransform.m.y.y * poly.mTransform.m.z.x;

    Normalize(poly.mTransform.m.x, poly.mTransform.m.x);

    // y = z cross x
    poly.mTransform.m.y.x =
        poly.mTransform.m.z.y * poly.mTransform.m.x.z
        - poly.mTransform.m.z.z * poly.mTransform.m.x.y;
    poly.mTransform.m.y.y =
        poly.mTransform.m.x.x * poly.mTransform.m.z.z
        - poly.mTransform.m.x.z * poly.mTransform.m.z.x;
    poly.mTransform.m.y.z =
        poly.mTransform.m.z.x * poly.mTransform.m.x.y
        - poly.mTransform.m.z.y * poly.mTransform.m.x.x;

    // Add large quad
    Vector2 p0(-10000.0f, 10000.0f);
    Vector2 p1(-10000.0f, -10000.0f);
    Vector2 p2(10000.0f, -10000.0f);
    Vector2 p3(10000.0f, 10000.0f);
    poly.mPoly.points.push_back(p0);
    poly.mPoly.points.push_back(p1);
    poly.mPoly.points.push_back(p2);
    poly.mPoly.points.push_back(p3);

    if (node->left == NULL) {
        // Leaf: clip parents against plane (front), recurse right
        for (std::list<BuildPoly>::iterator it = gParentPolys.begin();
             it != gParentPolys.end(); ++it) {
            Clip(*it, node->plane, true);
        }

        BuildVisit(node->right);

        for (std::list<BuildPoly>::iterator it = gChildPolys.begin();
             it != gChildPolys.end(); ++it) {
            Clip(*it, node->plane, true);
        }
    } else {
        // Save parents
        std::list<BuildPoly> savedParents(gParentPolys);

        // Clip parents (back side), recurse left
        for (std::list<BuildPoly>::iterator it = gParentPolys.begin();
             it != gParentPolys.end(); ++it) {
            Clip(*it, node->plane, false);
        }

        BuildVisit(node->left);

        // Clip children (back side)
        for (std::list<BuildPoly>::iterator it = gChildPolys.begin();
             it != gChildPolys.end(); ++it) {
            Clip(*it, node->plane, false);
        }

        // Swap children and parents with saved state
        std::list<BuildPoly> tempChildren;
        tempChildren.swap(gChildPolys);
        gParentPolys.swap(savedParents);

        // Clip parents (front side), recurse right
        for (std::list<BuildPoly>::iterator it = gParentPolys.begin();
             it != gParentPolys.end(); ++it) {
            Clip(*it, node->plane, true);
        }

        BuildVisit(node->right);

        // Clip children (front side)
        for (std::list<BuildPoly>::iterator it = gChildPolys.begin();
             it != gChildPolys.end(); ++it) {
            Clip(*it, node->plane, true);
        }

        // Splice saved lists back
        gParentPolys.splice(gParentPolys.end(), savedParents);
        gChildPolys.splice(gChildPolys.end(), tempChildren);

        tempChildren.clear();
        savedParents.clear();
    }

    // Move polys whose normal matches this node's plane from parents to children
    std::list<BuildPoly>::iterator it = gParentPolys.begin();
    while (it != gParentPolys.end()) {
        bool match = (node->plane.a == it->mTransform.m.z.x
                      && it->mTransform.m.z.y == node->plane.b
                      && it->mTransform.m.z.z == node->plane.c);
        if (match) {
            std::list<BuildPoly>::iterator next = it;
            ++next;
            gChildPolys.splice(gChildPolys.begin(), gParentPolys, it);
            it = next;
        } else {
            ++it;
        }
    }
}

void BuildFromBSP(RndMesh *mesh) {
    RndMesh *geomOwner = mesh->GetGeomOwner();
    BuildVisit(geomOwner->GetBSPTree());

    int totalVerts = 0;
    unsigned int totalFaces = 0;

    std::list<BuildPoly>::iterator it = gChildPolys.begin();
    while (it != gChildPolys.end()) {
        int numPoints = (int)it->mPoly.points.size();
        if ((unsigned int)numPoints < 3) {
            it = gChildPolys.erase(it);
        } else {
            totalVerts += numPoints;
            totalFaces += numPoints - 2;
            ++it;
        }
    }

    geomOwner->Verts().resize(totalVerts);

    int currentFaces = (int)geomOwner->Faces().size();
    if (totalFaces < (unsigned int)currentFaces) {
        geomOwner->Faces().erase(
            geomOwner->Faces().begin() + totalFaces,
            geomOwner->Faces().end()
        );
    } else {
        RndMesh::Face emptyFace;
        geomOwner->Faces().insert(
            geomOwner->Faces().end(),
            totalFaces - currentFaces,
            emptyFace
        );
    }

    int vertIdx = 0;
    int faceIdx = 0;

    for (std::list<BuildPoly>::iterator pit = gChildPolys.begin();
         pit != gChildPolys.end(); ++pit) {
        Vector2 *points = &pit->mPoly.points[0];
        Vector2 *pointsEnd = &pit->mPoly.points[0] + pit->mPoly.points.size();

        if (points != pointsEnd) {
            int vertOffset = vertIdx * 0x60;
            do {
                Vector3 pt(points->x, points->y, 0.0f);
                Multiply(
                    pt,
                    pit->mTransform,
                    *(Vector3 *)((char *)geomOwner->Verts().mVerts + vertOffset)
                );
                points++;
                vertIdx++;
                vertOffset += 0x60;
            } while (points != pointsEnd);
        }

        int firstVert = vertIdx - (int)pit->mPoly.points.size();
        int j = firstVert + 2;
        if (j < vertIdx) {
            int numTris = vertIdx - j;
            int faceOffset = faceIdx * 6;
            int v2 = firstVert + 1;
            faceIdx += numTris;
            do {
                unsigned short *facePtr =
                    (unsigned short *)((char *)&geomOwner->Faces()[0] + faceOffset);
                facePtr[0] = (unsigned short)firstVert;
                facePtr[1] = (unsigned short)v2;
                facePtr[2] = (unsigned short)j;
                j++;
                v2++;
                faceOffset += 6;
                numTris--;
            } while (numTris != 0);
        }
    }

    gParentPolys.clear();
    gChildPolys.clear();

    MakeNormals(mesh);
}

template int Keys<Vector3, Vector3>::AtFrame(
    float, const Key<Vector3> *&, const Key<Vector3> *&, float &
) const;
