#include "rndobj/Line.h"
#include "obj/Object.h"
#include "math/Rot.h"
#include "rndobj/Cam.h"
#include "rndobj/Draw.h"
#include "rndobj/Mat.h"
#include "rndobj/Trans.h"
#include "utl/BinStream.h"
#include "world/Spotlight.h"

void Spotlight::UpdateSphere() {
    Sphere s;
    MakeWorldSphere(s, true);
    Transform xfm;
    FastInvert(WorldXfm(), xfm);
    Multiply(s, xfm, s);
    SetSphere(s);
}

RndLine *gLine;

#pragma region Hmx::Object

RndLine::RndLine()
    : mWidth(1), mHasCaps(true), mLinePairs(false), mFoldAngle(PI / 2), mMat(this),
      mLineUpdate(true) {
    mMesh = Hmx::Object::New<RndMesh>();
    mMesh->SetMutable(0x1F);
    mMesh->SetTransParent(this, false);
    UpdateInternal();
}

BEGIN_HANDLERS(RndLine)
    HANDLE_EXPR(num_points, NumPoints())
    HANDLE_ACTION(
        set_point_pos,
        SetPointPos(_msg->Int(2), Vector3(_msg->Float(3), _msg->Float(4), _msg->Float(5)))
    )
    HANDLE_EXPR(point_color, mPoints[_msg->Int(2)].color.PackAlpha())
    HANDLE_ACTION(
        set_point_color,
        SetPointColor(
            _msg->Int(2),
            Hmx::Color(_msg->Float(3), _msg->Float(4), _msg->Float(5), _msg->Float(6)),
            true
        )
    )
    HANDLE_ACTION(
        set_points_color,
        SetPointsColor(
            _msg->Int(2),
            _msg->Int(3),
            Hmx::Color(_msg->Float(4), _msg->Float(5), _msg->Float(6), _msg->Float(7))
        )
    )
    HANDLE_ACTION(set_update, SetUpdate(_msg->Int(2)))
    HANDLE(set_mat, OnSetMat)
    HANDLE_SUPERCLASS(RndDrawable)
    HANDLE_SUPERCLASS(RndTransformable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_CUSTOM_PROPSYNC(RndLine::Point)
    SYNC_PROP(point, o.point)
    SYNC_PROP_MODIFY(color, o.color, gLine->UpdatePointColor(_prop->Int(_i - 1), true))
    SYNC_PROP_MODIFY(
        alpha, o.color.alpha, gLine->UpdatePointColor(_prop->Int(_i - 1), true)
    )
END_CUSTOM_PROPSYNC

BEGIN_PROPSYNCS(RndLine)
    gLine = this;
    SYNC_PROP_SET(mat, mMat.Ptr(), SetMat(_val.Obj<RndMat>()))
    SYNC_PROP(width, mWidth)
    SYNC_PROP_SET(
        fold_angle, mFoldAngle * RAD2DEG, mFoldAngle = _val.Float() * DEG2RAD;
        mFoldCos = cos(mFoldAngle)
    )
    SYNC_PROP_MODIFY(has_caps, mHasCaps, SetNumPoints(NumPoints()))
    SYNC_PROP_MODIFY(line_pairs, mLinePairs, SetNumPoints(NumPoints()))
    SYNC_PROP_SET(num_points, NumPoints(), SetNumPoints(_val.Int()))
    SYNC_PROP_MODIFY(points, mPoints, SetNumPoints(NumPoints()))
    SYNC_SUPERCLASS(RndDrawable)
    SYNC_SUPERCLASS(RndTransformable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BinStream &operator<<(BinStream &bs, const RndLine::Point &pt) {
    bs << pt.point << pt.color;
    return bs;
}

BEGIN_SAVES(RndLine)
    SAVE_REVS(4, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndDrawable)
    SAVE_SUPERCLASS(RndTransformable)
    bs << mMat << mPoints << mWidth << mFoldAngle << mHasCaps;
    bs << mLinePairs;
END_SAVES

BEGIN_COPYS(RndLine)
    CREATE_COPY_AS(RndLine, d);
    MILO_ASSERT(d, 0x2D2);
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndDrawable)
    COPY_SUPERCLASS(RndTransformable)
    COPY_MEMBER_FROM(d, mMat)
    COPY_MEMBER_FROM(d, mPoints)
    COPY_MEMBER_FROM(d, mWidth)
    COPY_MEMBER_FROM(d, mFoldAngle)
    COPY_MEMBER_FROM(d, mHasCaps)
    COPY_MEMBER_FROM(d, mLinePairs)
    UpdateInternal();
END_COPYS

BinStreamRev &operator>>(BinStreamRev &d, RndLine::Point &pt) {
    d >> pt.point >> pt.color;
    return d;
}

INIT_REVS(4, 0)

BEGIN_LOADS(RndLine)
    LOAD_REVS(bs)
    ASSERT_REVS(4, 0)
    if (d.rev > 3) {
        Hmx::Object::Load(bs);
    }
    RndDrawable::Load(bs);
    if (d.rev < 3) {
        ObjPtrList<Hmx::Object> objList(this);
        int x;
        bs >> x >> objList;
    }
    RndTransformable::Load(bs);
    bs >> mMat;
    d >> mPoints;
    bs >> mWidth;
    if (d.rev > 0) {
        bs >> mFoldAngle;
        d >> mHasCaps;
    }
    if (d.rev > 1) {
        d >> mLinePairs;
    }
    UpdateInternal();
END_LOADS

inline TextStream &operator<<(TextStream &ts, const RndLine::Point &pt) {
    ts << "\n\tv:" << pt.point << "\n\tc:" << pt.color;
    return ts;
}

void RndLine::Print() {
    TheDebug << "   points: " << mPoints << "\n";
    TheDebug << "   width: " << mWidth << "\n";
    TheDebug << "   foldAngle: " << mFoldAngle << "\n";
    TheDebug << "   hasCaps: " << mHasCaps << "\n";
    TheDebug << "   linePairs:" << mLinePairs << "\n";
}

#pragma endregion
#pragma region RndDrawable

void RndLine::UpdateSphere() {
    Sphere s;
    MakeWorldSphere(s, true);
    Transform xfm;
    FastInvert(WorldXfm(), xfm);
    Multiply(s, xfm, s);
    SetSphere(s);
}

float RndLine::GetDistanceToPlane(const Plane &p, Vector3 &v3) {
    if (mPoints.empty())
        return 0;
    WorldXfm();
    float ret = 0.0f;
    bool first = true;
    FOREACH (it, mPoints) {
        float t1 = p.a * it->point.x;
        float t2 = p.b * it->point.y;
        float t3 = p.c * it->point.z;
        float dot = t1 + t2 + t3 + p.d;
        if (first || std::fabs(dot) < std::fabs(ret)) {
            first = false;
            ret = dot;
            v3 = it->point;
        }
    }
    return ret;
}

bool RndLine::MakeWorldSphere(Sphere &s, bool b2) {
    if (b2) {
        s.Zero();
        FOREACH (it, mPoints) {
            s.GrowToContain(Sphere(it->point, mWidth));
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

void RndLine::Mats(std::list<class RndMat *> &mats, bool) {
    if (mMat) {
        mats.push_back(mMat);
    }
}

void RndLine::DrawShowing() {
    if (mPoints.size() >= 2) {
        if (mLineUpdate) {
            RndCam *cam = RndCam::Current();
            UpdateLine(cam->WorldXfm(), cam->NearPlane());
            mMesh->SetWorldXfm(cam->WorldXfm());
        }
        mMesh->DrawShowing();
    }
}

RndDrawable *RndLine::CollideShowing(const Segment &s, float &f, Plane &p) {
    RndDrawable *d = mMesh->Collide(s, f, p);
    return d ? this : d;
}

int RndLine::CollidePlane(const Plane &p) { return mMesh->CollidePlane(p); }

#pragma endregion
#pragma region RndLine

void RndLine::SetMat(RndMat *mat) {
    mMat = mat;
    mMesh->SetMat(mat);
}

void RndLine::SetUpdate(bool b1) {
    mLineUpdate = b1;
    if (!mLineUpdate) {
        Transform xfm(WorldXfm());
        static Vector3 offset(0, -1, 0);
        Multiply(offset, xfm, xfm.v);
        UpdateLine(xfm, 0);
        mMesh->SetLocalPos(offset);
    }
}

void RndLine::SetPointPos(int i, const Vector3 &pos) {
    MILO_ASSERT((i >= 0) && (i < mPoints.size()), 0x1CE);
    mPoints[i].point = pos;
}

void RndLine::SetPointColor(int i, const Hmx::Color &color, bool sync) {
    MILO_ASSERT((i >= 0) && (i < mPoints.size()), 0x1D5);
    mPoints[i].color = color;
    UpdatePointColor(i, sync);
}

void RndLine::UpdatePointColor(int i, bool sync) {
    Point *pt = &mPoints[i];
    VertsMap vmap;
    MapVerts(i, vmap);
    vmap.v++->color = pt->color;
    vmap.v++->color = pt->color;
    if (vmap.t) {
        vmap.v++->color = pt->color;
        vmap.v++->color = pt->color;
    }
    if (sync)
        mMesh->Sync(0x1F);
}

void RndLine::UpdateInternal() {
    mFoldCos = cos(mFoldAngle);
    mMesh->SetMat(mMat);
    SetNumPoints(mPoints.size());
}

void RndLine::SetNumPoints(int num) {
    mPoints.resize(num);
    if ((int)num >= 1) {
        int i1 = num;
        if (mHasCaps) {
            i1 = num + 2;
            if (mLinePairs) {
                i1 = (num & 0x7ffffffeU) * 2;
            }
        }
        mMesh->Verts().resize(i1 * 2);
        int numPoints = mPoints.size();
        for (int i = 0; (unsigned int)i < numPoints; i++) {
            VertsMap vmap;
            Hmx::Color &ptColor = mPoints[i].color;
            MapVerts(i, vmap);
            if (vmap.t == 1) {
                vmap.v->tex.Set(0, 1);
                vmap.v++->color = ptColor;
                vmap.v->tex.Set(0, 0);
                vmap.v++->color = ptColor;
            }
            vmap.v->tex.Set(1, 1);
            vmap.v++->color = ptColor;
            vmap.v->tex.Set(0, 1);
            vmap.v++->color = ptColor;
            if (vmap.t == 2) {
                vmap.v->tex.Set(1, 1);
                vmap.v++->color = ptColor;
                vmap.v->tex.Set(1, 0);
                vmap.v++->color = ptColor;
            }
        }

        if (mLinePairs) {
            if (mHasCaps)
                i1 = i1 * 3 >> 1;
        } else
            i1 = (i1 - 1) * 2;
        mMesh->Faces().resize(i1);
        for (int i5 = i1 - 2; i5 >= 0; i5 -= 2) {
            int i7 = i5;
            if (mLinePairs) {
                if (mHasCaps) {
                    i7 = i5 % 6 + (i5 / 6) * 8;
                } else
                    i7 = i5 * 2;
            }
            mMesh->Faces(i5).Set(i7, i7 + 2, i7 + 1);
            mMesh->Faces(i1 - 1).Set(i7 + 1, i7 + 2, i7 + 3);
            i1 = i5;
        }
        mMesh->Sync(0x13F);
    }
}

DataNode RndLine::OnSetMat(const DataArray *array) {
    RndMat *mat = array->Obj<RndMat>(2);
    SetMat(mat);
    SetShowing(mat);
    return 0;
}

void RndLine::MapVerts(int idx, VertsMap &vmap) {
    if (mHasCaps) {
        if (mLinePairs) {
            vmap.t = (idx & 1) + 1;
            vmap.v = &mMesh->Verts()[idx * 4];
        } else {
            if (0 == idx) {
                vmap.t = 1;
                vmap.v = &mMesh->Verts()[0];
            } else {
                int lastIdx = (int)mPoints.size() - 1;
                if ((unsigned int)idx == lastIdx) {
                    vmap.t = 2;
                    vmap.v = &mMesh->Verts()[(int)mMesh->Verts().size() - 4];
                } else {
                    vmap.t = 0;
                    vmap.v = &mMesh->Verts()[(idx + 1) * 2];
                }
            }
        }
    } else {
        vmap.t = 0;
        vmap.v = &mMesh->Verts()[idx * 2];
    }
}

void RndLine::SetPointsColor(int start, int end, const Hmx::Color &color) {
    MILO_ASSERT((start >= 0) && (start < mPoints.size()) && (end >= 0) && (end < mPoints.size()), 0x1F2);
    if (end < start) {
        int tmp = start;
        start = end;
        end = tmp;
    }
    for (int i = start; i <= end; i++) {
        mPoints[i].color = color;
        VertsMap vmap;
        MapVerts(i, vmap);
        vmap.v++->color = color;
        vmap.v++->color = color;
        if (vmap.t != 0) {
            vmap.v++->color = color;
            vmap.v++->color = color;
        }
    }
    mMesh->Sync(0x1F);
}

void RndLine::UpdateLine(RndLine::Point *, RndLine::Point *) {}
void RndLine::UpdateLinePair(RndLine::Point *, RndLine::Point *) {}

#ifdef HX_NATIVE

void RndLine::UpdateLine(const Transform &camXfm, float nearPlane) {
    int numPts = (int)mPoints.size();
    if (numPts < 2) return;

    // Camera position in world space
    Vector3 camPos = camXfm.v;

    // Process each point — generate billboard vertices facing the camera
    for (int i = 0; i < numPts; i++) {
        Vector3 &p = mPoints[i].point;

        // Compute segment direction
        Vector3 dir;
        if (i == 0) {
            Subtract(mPoints[1].point, p, dir);
        } else if (i == numPts - 1) {
            Subtract(p, mPoints[i - 1].point, dir);
        } else {
            // Average of adjacent segments for smooth corners
            Vector3 d1, d2;
            Subtract(p, mPoints[i - 1].point, d1);
            Subtract(mPoints[i + 1].point, p, d2);

            // Check fold angle — if angle too sharp, use incoming direction
            float dot = d1.x * d2.x + d1.y * d2.y + d1.z * d2.z;
            float len1 = Length(d1);
            float len2 = Length(d2);
            if (len1 > 0.0001f && len2 > 0.0001f) {
                float cosAngle = dot / (len1 * len2);
                if (cosAngle < mFoldCos) {
                    // Sharp fold — use incoming segment
                    dir = d1;
                } else {
                    Add(d1, d2, dir);
                }
            } else {
                Add(d1, d2, dir);
            }
        }

        Normalize(dir, dir);

        // Camera-facing perpendicular
        Vector3 toCamera;
        Subtract(camPos, p, toCamera);
        Vector3 side;
        Cross(toCamera, dir, side);
        float sideLen = Length(side);
        if (sideLen > 0.0001f) {
            Scale(side, mWidth * 0.5f / sideLen, side);
        }

        // Set vertex positions
        VertsMap vmap;
        MapVerts(i, vmap);

        if (vmap.t == 1) {
            // Start cap: cap verts at point position (degenerate)
            vmap.v->pos.Set(p.x + side.x, p.y + side.y, p.z + side.z);
            vmap.v++;
            vmap.v->pos.Set(p.x - side.x, p.y - side.y, p.z - side.z);
            vmap.v++;
        }

        // Main verts
        vmap.v->pos.Set(p.x + side.x, p.y + side.y, p.z + side.z);
        vmap.v++;
        vmap.v->pos.Set(p.x - side.x, p.y - side.y, p.z - side.z);
        vmap.v++;

        if (vmap.t == 2) {
            // End cap: cap verts at point position (degenerate)
            vmap.v->pos.Set(p.x + side.x, p.y + side.y, p.z + side.z);
            vmap.v++;
            vmap.v->pos.Set(p.x - side.x, p.y - side.y, p.z - side.z);
        }
    }

    mMesh->Sync(0x1F);
}

#else

void RndLine::UpdateLine(const Transform &, float) {}

#endif
