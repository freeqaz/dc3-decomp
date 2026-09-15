#include "rndobj\Line.h"
#include "obj/Object.h"
#include "math\Rot.h"
#include "rndobj/Cam.h"
#include "rndobj\Draw.h"
#include "rndobj\Mat.h"
#include "rndobj\Trans.h"
#include "utl/BinStream.h"
#include "world\Spotlight.h"

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
    SYNC_PROP_SET(fold_angle, mFoldAngle * RAD2DEG, SetFoldAngle(_val.Float() * DEG2RAD))
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
        if (mHasCaps) {
            if (mLinePairs) {
                num = (num & 0x7ffffffeU) * 2;
            } else {
                num = num + 2;
            }
        }
        mMesh->Verts().resize(num * 2);
        for (int i = 0; i < mPoints.size(); i++) {
            VertsMap vmap;
            MapVerts(i, vmap);
            if (vmap.t == 1) {
                vmap.v->tex.Set(0, 1);
                vmap.v++->color = mPoints[i].color;
                vmap.v->tex.Set(0, 0);
                vmap.v++->color = mPoints[i].color;
            }
            vmap.v->tex.Set(0.5f, 1.0f);
            vmap.v++->color = mPoints[i].color;
            vmap.v->tex.Set(0.5f, 0.0f);
            vmap.v++->color = mPoints[i].color;
            if (vmap.t == 2) {
                vmap.v->tex.Set(1, 1);
                vmap.v++->color = mPoints[i].color;
                vmap.v->tex.Set(1, 0);
                vmap.v++->color = mPoints[i].color;
            }
        }

        if (mLinePairs) {
            if (mHasCaps)
                num = num * 3 >> 1;
        } else
            num = (num - 1) * 2;
        mMesh->Faces().resize(num);
        num -= 2;
        while (num >= 0) {
            int i7 = num;
            if (mLinePairs) {
                if (mHasCaps) {
                    i7 = num % 6 + (num / 6) * 8;
                } else
                    i7 = num * 2;
            }
            mMesh->Faces(num).Set(i7, i7 + 2, i7 + 1);
            mMesh->Faces(num + 1).Set(i7 + 1, i7 + 2, i7 + 3);
            num -= 2;
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
                if ((unsigned int)(idx + 1) == mPoints.size()) {
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

/** Offsets a view-space position by a screen-space (x, z) pair. The y component is
 *  copied through unchanged, which is why the target re-stores `dst.y` when one of
 *  these immediately follows the other on the same destination. */
inline void Add(const Vector3 &v, const Vector2 &d, Vector3 &dst) {
    dst.Set(v.x + d.x, v.y, v.z + d.y);
}

inline void Subtract(const Vector3 &v, const Vector2 &d, Vector3 &dst) {
    dst.Set(v.x - d.x, v.y, v.z - d.y);
}

// 80.6% canonical (w7-ay: floor held). Tried, each measured against the
// image at 82678AF8-82678BE8: `if (len != 0) invLen = 1/len; else invLen = 0;`
// matches the image's `b`/`fmr f0, f11` join (82678B34/82678B38) but shifts
// the FPR assignment of the eight cap/body vertex writes so that the cap
// helpers re-read pos.y/pos.z instead of forwarding them (79.8); spelling
// the start-cap perp as `perp.x = -side1.y; perp.y = side1.x` reorders the
// mHasCaps test the way the image has it (82678B94-82678BA8) but the same
// cascade lands the caps at 77.9. The dir/side stage in the image forwards
// dir1.y and proj2.x but reloads dir1.x and proj2.y (82678B18, 82678AFC) --
// the mirror of what our spelling forwards -- and the residual is that
// register-allocation cascade, not a missing statement.
void RndLine::UpdateLinePair(RndLine::Point *pt1, RndLine::Point *pt2) {
    VertsMap vmap;
    MapVerts((pt1 - &mPoints[0]), vmap);

    if (pt1 == pt2) {
        if (mHasCaps) {
            *(Vector3 *)&vmap.v->pos = *(Vector3 *)&pt1->unk[0];
            vmap.v++;
            *(Vector3 *)&vmap.v->pos = *(Vector3 *)&pt1->unk[0];
            vmap.v++;
        }
        *(Vector3 *)&vmap.v->pos = *(Vector3 *)&pt1->unk[0];
        vmap.v++;
        *(Vector3 *)&vmap.v->pos = *(Vector3 *)&pt1->unk[0];
        vmap.v++;
        *(Vector3 *)&vmap.v->pos = *(Vector3 *)&pt2->unk[0];
        vmap.v++;
        *(Vector3 *)&vmap.v->pos = *(Vector3 *)&pt2->unk[0];
        vmap.v++;
        if (mHasCaps) {
            *(Vector3 *)&vmap.v->pos = *(Vector3 *)&pt2->unk[0];
            vmap.v++;
            *(Vector3 *)&vmap.v->pos = *(Vector3 *)&pt2->unk[0];
        }
    } else {
        Vector3 &viewPos1 = *(Vector3 *)&pt1->unk[0];
        Vector3 &viewPos2 = *(Vector3 *)&pt2->unk[0];
        Vector2 &proj1 = *(Vector2 *)&pt1->unk[4];
        Vector2 &dir1 = *(Vector2 *)&pt1->unk[6];
        Vector2 &side1 = *(Vector2 *)&pt1->unk[8];
        Vector2 &proj2 = *(Vector2 *)&pt2->unk[4];
        Vector2 &side2 = *(Vector2 *)&pt2->unk[8];
        Vector2 perp;

        // All three components are read before either projected value is
        // stored: retail has the x load up with the y/z pair rather than
        // after the store of proj.y, which only happens if the source read
        // them into locals first.
        float vy1 = viewPos1.y, vz1 = viewPos1.z, vx1 = viewPos1.x;
        float invY1 = 1.0f / vy1;
        proj1.y = vz1 * invY1;
        proj1.x = vx1 * invY1;
        float vy2 = viewPos2.y, vz2 = viewPos2.z, vx2 = viewPos2.x;
        float invY2 = 1.0f / vy2;
        proj2.x = vx2 * invY2;
        proj2.y = vz2 * invY2;

        float dirZ = proj2.y - proj1.y;
        dir1.y = dirZ;
        float dirX = proj2.x - proj1.x;
        dir1.x = dirX;
        dirZ = dir1.y;
        dirX = dir1.x;
        float len = std::sqrt(dirX * dirX + dirZ * dirZ);
        float invLen = 0.0f;
        if (len != 0.0f) {
            invLen = 1.0f / len;
        }
        dir1.y = invLen * dir1.y;
        dir1.x = invLen * dirX;

        side1.y = dir1.x;
        side1.x = -dir1.y;
        float width = mWidth;
        side1.y = width * side1.y;
        side1.x = side1.x * width;
        ((int *)&side2)[0] = ((int *)&side1)[0];
        ((int *)&side2)[1] = ((int *)&side1)[1];

        // Cap vertices are pushed one half-width *along* the line as well as across
        // it, so the offset is the side vector rotated a further quarter turn.
        float sideY = side1.x;
        float sideX = side1.y;
        perp.y = sideY;
        perp.x = -sideX;

        if (mHasCaps) {
            Subtract(viewPos1, side1, *(Vector3 *)&vmap.v->pos);
            Add(*(Vector3 *)&vmap.v->pos, perp, *(Vector3 *)&vmap.v->pos);
            vmap.v++;
            Add(viewPos1, side1, *(Vector3 *)&vmap.v->pos);
            Add(*(Vector3 *)&vmap.v->pos, perp, *(Vector3 *)&vmap.v->pos);
            vmap.v++;
        }

        Subtract(viewPos1, side1, *(Vector3 *)&(vmap.v++)->pos);
        Add(viewPos1, side1, *(Vector3 *)&(vmap.v++)->pos);
        Subtract(viewPos2, side2, *(Vector3 *)&(vmap.v++)->pos);
        Add(viewPos2, side2, *(Vector3 *)&(vmap.v++)->pos);

        if (mHasCaps) {
            float endY = side2.x;
            float endX = side2.y;
            perp.x = endX;
            perp.y = -endY;
            Subtract(viewPos2, side2, *(Vector3 *)&vmap.v->pos);
            Add(*(Vector3 *)&vmap.v->pos, perp, *(Vector3 *)&vmap.v->pos);
            vmap.v++;
            Add(viewPos2, side2, *(Vector3 *)&vmap.v->pos);
            Add(*(Vector3 *)&vmap.v->pos, perp, *(Vector3 *)&vmap.v->pos);
        }
    }
}

// 85.3% canonical (w7-ay, from 76.9). Residuals, all tried and consistent
// with the phase notes below: the phase-1 IV is derived from &start->ViewPos()
// and then biased (82678404 `addi r11, r4, 0x20`, then `subi r9, r11, 0x34`)
// where we bias start directly; phase 2 reloads dir.y for the normalisation
// multiply and side.x for the width scale but forwards dirX and side.y
// (partial store-forwarding -- spelling the re-reads as `dir.y * invLen` /
// `side.x * mWidth` forwarded MORE, not less, 81.8); the last-point copy
// batches its four loads into r7/r6/r9/r8 before storing; the two phase-4
// copy loops count with `subic.`/`bne` (82678694, 826786F0) and `stw`+`addi`
// where every counted spelling here (for, do/while, hoisted bound) gives
// `mtctr`/`bdnz` with `stwu`; the end cap re-reads pos.x after the Subtract
// where the image keeps it in f0.
//
// w7-bn (85.3, no net gain, full ninja each): the phase-4 `subic.`/`bne` loops
// ARE reproducible -- a STRUCT copy in the loop body is what stops MSVC from
// converting the counter to ctr.  `*(Vector2 *)&p->unk[8] = *(Vector2 *)&end
// ->unk[8]; p->ViewPos() = endView;` (endView a `Vector3 &`) gives both loops
// row for row: `subic.` at the top, `stw`+`addi r11, r11, 0x48`, the IV biased
// to p+0x20 (82678680 `addi r11, r11, 0x20`), the side words read off the
// end/start base (`lwz r8, 0x40(r29)`) and the view words off a hoisted
// end+0x20 (rows 186-207 and 214-229 all equal).  The Vector2 copy alone
// keeps `subic.` but leaves the IV `stwu`-biased (83.9); int side + Vector3
// view keeps `subic.` with the IV at p (84.2).  What it costs is phase 5: with
// any struct copy in phase 4 the start cap stops store-forwarding the
// Subtract result into the Add (`lfs f11, 0x8(r11)` / `lfs f10, 0x0(r11)`
// reloads instead of the image's forwarded `fadds f12, f0, f12` at
// 826787A4, 6 inserts / 3 deletes where this spelling has 2 / 1), so the net
// is 84.9.  The two have to be solved together; the int-copy spelling below
// keeps the better total.
void RndLine::UpdateLine(RndLine::Point *start, RndLine::Point *end) {
    // Phase 1: project every point (x, z over y in view space). The three
    // view-space components are read before either projected value is
    // stored (826783F8-82678438), as in UpdateLinePair.
    for (Point *pt = start; pt <= end; pt++) {
        Vector3 &viewPos = pt->ViewPos();
        Vector2 &proj = *(Vector2 *)(&viewPos + 1);
        float vy = viewPos.y, vz = viewPos.z, vx = viewPos.x;
        float invY = 1.0f / vy;
        proj.x = vx * invY;
        proj.y = vz * invY;
    }

    // Phase 2: direction and side vectors between adjacent points. Same
    // spelling as UpdateLinePair: dir is re-read after it is stored
    // (82678494 reloads dir.y), side.x is re-read for the width scale.
    Point *pt = start;
    for (; pt != end; pt++) {
        Vector2 &proj = *(Vector2 *)&pt->unk[4];
        Vector2 &dir = *(Vector2 *)&pt->unk[6];
        Vector2 &side = *(Vector2 *)&pt->unk[8];
        Vector2 &nextProj = *(Vector2 *)&(pt + 1)->unk[4];

        float dirZ = nextProj.y - proj.y;
        dir.y = dirZ;
        float dirX = nextProj.x - proj.x;
        dir.x = dirX;
        dirZ = dir.y;
        dirX = dir.x;
        float len = std::sqrt(dirX * dirX + dirZ * dirZ);
        float invLen;
        if (len != 0.0f) {
            invLen = 1.0f / len;
        } else {
            invLen = 0.0f;
        }
        dir.y = invLen * dir.y;
        dir.x = invLen * dirX;

        side.y = dir.x;
        side.x = -dir.y;
        float width = mWidth;
        side.y = width * side.y;
        side.x = side.x * width;
    }

    // The last point takes the direction and side of the one before it.
    {
        Point *prev = pt - 1;
        pt->unk[7] = prev->unk[7];
        pt->unk[8] = prev->unk[8];
        pt->unk[9] = prev->unk[9];
        pt->unk[6] = prev->unk[6];
    }

    // Phase 3: fold the side vector at sharp interior corners.
    bool flipped = false;
    Hmx::Ray prevRay;
    {
        Vector2 &startProj = *(Vector2 *)&start->unk[4];
        Vector2 &startDir = *(Vector2 *)&start->unk[6];
        Vector2 &startSide = *(Vector2 *)&start->unk[8];
        prevRay.dir = startDir;
        prevRay.base.Set(startSide.x + startProj.x, startSide.y + startProj.y);
    }

    for (Point *cur = start + 1; cur != end; cur++) {
        Vector2 &proj = *(Vector2 *)&cur->unk[4];
        Vector2 &dir = *(Vector2 *)&cur->unk[6];
        Vector2 &side = *(Vector2 *)&cur->unk[8];
        Vector2 &prevDir = *(Vector2 *)&(cur - 1)->unk[6];

        float dot = prevDir.x * dir.x + prevDir.y * dir.y;
        if (dot < mFoldCos) {
            flipped = !flipped;
        }
        if (flipped) {
            side.y = -side.y;
            side.x = -side.x;
        }

        Hmx::Ray oldPrevRay = prevRay;
        prevRay.base.Set(proj.x + side.x, proj.y + side.y);
        prevRay.dir = dir;

        if (dot < 0.9998499751091003f) {
            Intersect(prevRay, oldPrevRay, side);
            side.Set(side.x - proj.x, side.y - proj.y);
        }
    }

    if (flipped) {
        Vector2 &endSide = *(Vector2 *)&end->unk[8];
        endSide.y = -endSide.y;
        endSide.x = -endSide.x;
    }

    // Phase 4: points outside [start, end] copy the side vector and view
    // position of the nearest visible point. `mPoints.back()` is evaluated
    // once per block (the image hoists the bound out of the first loop and
    // counts with divwu, then re-evaluates it before MapVerts).
    if (&mPoints[0] == start) {
        Point *last = &mPoints.back();
        if (end + 1 <= last) {
            int *endSide = &end->unk[8];
            int *endView = &end->unk[0];
            for (Point *p = end + 1; p <= last; p++) {
                int *ptData = &p->unk[0];
                ptData[8] = endSide[0];
                ptData[9] = endSide[1];
                ptData[0] = endView[0];
                ptData[1] = endView[1];
                ptData[2] = endView[2];
                ptData[3] = endView[3];
            }
        }
    } else if (&mPoints[0] < start) {
        int *startSide = &start->unk[8];
        int *startView = &start->unk[0];
        for (Point *p = &mPoints[0]; p < start; p++) {
            int *ptData = &p->unk[0];
            ptData[8] = startSide[0];
            ptData[9] = startSide[1];
            ptData[0] = startView[0];
            ptData[1] = startView[1];
            ptData[2] = startView[2];
            ptData[3] = startView[3];
        }
    }

    // Phase 5: vertex positions. Each cap is TWO vertices, each written as
    // the body vertex and then offset by the quarter-turned side vector on
    // the same destination (the doubled pos.y store at 8267878C/82678790),
    // exactly as UpdateLinePair does. The previous body wrote four cap
    // vertices with the unoffset pair as their own vertices and offset the
    // start cap by (-side.y, -side.x); the image offsets it by
    // (-side.y, +side.x) (82678794-826787A0) and the unflipped end cap by
    // (+side.y, -side.x) (8267888C). The first point, the last point and
    // the start-cap offset are all formed BEFORE MapVerts and survive the
    // call in registers (82678744-8267874C `lfs f13, 0x44(r7); lfs f0,
    // 0x40(r7); fneg f13, f13`, then `bl MapVerts`), unconditionally.
    Point *first = &mPoints[0];
    Point *last = &mPoints.back();
    Vector2 &firstSide = *(Vector2 *)&first->unk[8];
    Vector2 startPerp;
    startPerp.x = -firstSide.y;
    startPerp.y = firstSide.x;
    VertsMap vmap;
    MapVerts(0, vmap);

    if (mHasCaps) {
        Vector3 &viewPos = first->ViewPos();
        Subtract(viewPos, firstSide, *(Vector3 *)&vmap.v->pos);
        Add(*(Vector3 *)&vmap.v->pos, startPerp, *(Vector3 *)&vmap.v->pos);
        vmap.v++;
        Add(viewPos, firstSide, *(Vector3 *)&vmap.v->pos);
        Add(*(Vector3 *)&vmap.v->pos, startPerp, *(Vector3 *)&vmap.v->pos);
        vmap.v++;
    }

    for (Point *p = first; p <= last; p++) {
        Vector3 &viewPos = p->ViewPos();
        Vector2 &side = *(Vector2 *)&p->unk[8];
        Subtract(viewPos, side, *(Vector3 *)&(vmap.v++)->pos);
        Add(viewPos, side, *(Vector3 *)&(vmap.v++)->pos);
    }

    if (mHasCaps) {
        Vector3 &viewPos = last->ViewPos();
        Vector2 &side = *(Vector2 *)&last->unk[8];
        Vector2 perp;
        if (flipped) {
            perp.y = side.x;
            perp.x = -side.y;
        } else {
            perp.x = side.y;
            perp.y = -side.x;
        }
        Subtract(viewPos, side, *(Vector3 *)&vmap.v->pos);
        Add(*(Vector3 *)&vmap.v->pos, perp, *(Vector3 *)&vmap.v->pos);
        vmap.v++;
        Add(viewPos, side, *(Vector3 *)&vmap.v->pos);
        Add(*(Vector3 *)&vmap.v->pos, perp, *(Vector3 *)&vmap.v->pos);
    }
}

void RndLine::UpdateLine(const Transform &camXfm, float nearPlane) {
    int numPts = (int)mPoints.size();
    if ((unsigned int)numPts < 2)
        return;

    // Transpose camera transform, then multiply with world transform
    Transform viewXfm;
    Transpose(camXfm, viewXfm);
    Multiply(WorldXfm(), viewXfm, viewXfm);

    // Transform points and track near-plane clipping
    int firstClipped = -1;
    int lastClipped = -1;
    int i = 0;
    float clipDist = nearPlane + 0.01f;

    numPts = (int)mPoints.size();
    for (i = 0; i < numPts; i++) {
        Point *pt = &mPoints[i];
        Multiply(pt->point, viewXfm, pt->ViewPos());
        if (pt->ViewPos().y < clipDist) {
            lastClipped = i;
            if (firstClipped == -1) {
                firstClipped = i;
            }
        }
    }
    if (firstClipped == 0 && lastClipped == numPts - 1)
        return;

    if (!mLinePairs) {
        int startIdx;
        int endIdx;
        if (lastClipped != -1) {
            if (firstClipped > (numPts - 1) - lastClipped) {
                Point *prevPt = &mPoints[firstClipped - 1];
                Point *pt = &mPoints[firstClipped];
                // Each `->ViewPos()` is re-formed at its use: the image reads
                // the near-plane component off the POINT base (0x82679068
                // `lfs f0, -0x24(r11)`) and only builds the +0x20 argument
                // pointers at the call (0x8267906C `addi r3, r10, 0x20`).
                Interp(prevPt->ViewPos(), pt->ViewPos(),
                       (clipDist - prevPt->ViewPos().y)
                           / (pt->ViewPos().y - prevPt->ViewPos().y),
                       pt->ViewPos());
                endIdx = firstClipped;
                startIdx = 0;
            } else {
                Point *pt = &mPoints[lastClipped];
                Point *nextPt = &mPoints[lastClipped + 1];
                Interp(pt->ViewPos(), nextPt->ViewPos(),
                       (clipDist - pt->ViewPos().y)
                           / (nextPt->ViewPos().y - pt->ViewPos().y),
                       pt->ViewPos());
                // startIdx before endIdx: 0x826790BC `mr r11, r29` precedes
                // 0x826790C0 `subi r10, r31, 0x1`.
                startIdx = lastClipped;
                endIdx = numPts - 1;
            }
        } else {
            startIdx = 0;
            endIdx = numPts - 1;
        }
        UpdateLine(&mPoints[startIdx], &mPoints[endIdx]);
    } else {
        i = 0;
        while (i < numPts - 1) {
            Point *pt1 = &mPoints[i];
            Point *pt2 = &mPoints[i + 1];
            // No `dist1`/`dist2` locals: the image re-reads the near-plane
            // component at every use (0x8267912C reloads pt1's from memory
            // after 0x82679118 has overwritten f0 with pt2's), and it
            // tail-merges the two Interp calls into one shared block at
            // 0x82679150.
            if (pt1->ViewPos().y < clipDist) {
                if (pt2->ViewPos().y < clipDist) {
                    pt2 = pt1;
                } else {
                    Interp(pt1->ViewPos(), pt2->ViewPos(),
                           (clipDist - pt1->ViewPos().y)
                               / (pt2->ViewPos().y - pt1->ViewPos().y),
                           pt1->ViewPos());
                }
            } else if (pt2->ViewPos().y < clipDist) {
                Interp(pt2->ViewPos(), pt1->ViewPos(),
                       (clipDist - pt2->ViewPos().y)
                           / (pt1->ViewPos().y - pt2->ViewPos().y),
                       pt2->ViewPos());
            }
            UpdateLinePair(pt1, pt2);
            i += 2;
        }
    }

    mMesh->Sync(0x1F);
}
