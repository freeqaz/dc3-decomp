#include "math/Geo.h"
#include "Vec.h"
#include "math/Mtx.h"
#include "math/Sphere.h"
#include "math/Utl.h"
#include "math/Vec.h"
#include "obj/DataFunc.h"
#include "os/System.h"
#include "utl/BinStream.h"
#include <cfloat>
#include <cmath>

void Triangle::Set(const Vector3 &v0, const Vector3 &v1, const Vector3 &v2) {
    origin = v0;
    // edge vectors
    frame.x.Set(v1.x - v0.x, v1.y - v0.y, v1.z - v0.z);
    frame.z.Set(v2.x - v0.x, v2.y - v0.y, v2.z - v0.z);
    // normal = cross(edge1, edge2)
    frame.y.Set(
        frame.x.y * frame.z.z - frame.x.z * frame.z.y,
        frame.x.z * frame.z.x - frame.x.x * frame.z.z,
        frame.x.x * frame.z.y - frame.x.y * frame.z.x
    );
}

float gUnitsPerMeter = 39.370079f;
float gBSPPosTol = 0.01f;
float gBSPDirTol = 0.985f;
int gBSPMaxDepth = 20;
int gBSPMaxCandidates = 40;
float gBSPCheckScale = 1.1f;

void NumNodes(const BSPNode *node, int &num, int &maxDepth) {
    static int depth = 0;
    if (node) {
        depth++;
        if (depth == 1) {
            num = 0;
            maxDepth = 1;
        } else if (depth > maxDepth) {
            maxDepth = depth;
        }
        NumNodes(node->left, num, maxDepth);
        NumNodes(node->right, num, maxDepth);
        num++;
        depth--;
    }
}

BinStream &operator<<(BinStream &bs, const BSPNode *node) {
    if (node) {
        bs << true;
        bs << node->plane << node->left << node->right;
    } else {
        bs << false;
    }
    return bs;
}

BinStream &operator>>(BinStream &bs, BSPNode *&node) {
    unsigned char nodeExists;
    bs >> nodeExists;
    if (nodeExists) {
        node = new BSPNode();
        bs >> node->plane >> node->left >> node->right;
    } else {
        node = nullptr;
    }
    return bs;
}

void Box::Extend(float scale) {
    mMin.x -= scale;
    mMin.y -= scale;
    mMin.z -= scale;
    mMax.x += scale;
    mMax.y += scale;
    mMax.z += scale;
}

bool Box::Contains(const Vector3 &v) const {
    return mMin.x <= v.x && mMin.y <= v.y && mMin.z <= v.z && mMax.x >= v.x
        && mMax.y >= v.y && mMax.z >= v.z;
}

bool Box::Contains(const Sphere &s) const {
    return mMin.x <= s.center.x - s.radius && mMin.y <= s.center.y - s.radius
        && mMin.z <= s.center.z - s.radius && mMax.x >= s.center.x + s.radius
        && mMax.y >= s.center.y + s.radius && mMax.z >= s.center.z + s.radius;
}

bool Box::Contains(const Triangle &t) const {
    Vector3 v1 = t.origin;
    Vector3 v2(
        t.frame.x.x + t.origin.x, t.frame.x.y + t.origin.y, t.frame.x.z + t.origin.z
    );
    Vector3 v3(
        t.frame.y.x + t.origin.x, t.frame.y.y + t.origin.y, t.frame.y.z + t.origin.z
    );
    return Contains(v1) && Contains(v2) && Contains(v3);
}

float Box::SurfaceArea() const {
    float x = mMax.x - mMin.x;
    float y = mMax.y - mMin.y;
    float z = mMax.z - mMin.z;
    float xy = x * y * 2;
    float xz = x * z * 2;
    float yz = y * z * 2;
    return xy + xz + yz;
}

float Box::Volume() const {
    float x = mMax.x - mMin.x;
    float y = mMax.y - mMin.y;
    float z = mMax.z - mMin.z;
    return x * y * z;
}

void Box::GrowToContain(const Vector3 &vec, bool b) {
    if (b) {
        mMin = mMax = vec;
    } else
        for (int i = 0; i < 3; i++) {
            MinEq(mMin[i], vec[i]);
            MaxEq(mMax[i], vec[i]);
        }
}

bool Box::Clamp(Vector3 &v) {
    return ClampEq(v.x, mMin.x, mMax.x) | ClampEq(v.y, mMin.y, mMax.y) | ClampEq(v.z, mMin.z, mMax.z);
}

void Normalize(const Plane &in, Plane &out) {
    float mult = 0;
    float len = std::sqrt(in.a * in.a + in.b * in.b + in.c * in.c);
    if (len != 0) {
        mult = 1 / len;
    }
    out.Set(in.a * mult, in.b * mult, in.c * mult, in.d * mult);
}

void ClosestPoint(const Vector3 &v1, const Vector3 &v2, const Vector3 &v3, Vector3 *vout) {
    Vector3 diff31, diff21;
    Subtract(v2, v1, diff21);
    Subtract(v3, v1, diff31);
    float f5 = Dot(diff31, diff21);
    if (!(f5 > 0)) {
        *vout = v1;
        return;
    }
    float dot21 = Dot(diff21, diff21);
    if (f5 > dot21) {
        *vout = v2;
        return;
    }
    Scale(diff21, f5 / dot21, diff21);
    Add(v1, diff21, *vout);
}

void Plane::Set(const Vector3 &v1, const Vector3 &v2, const Vector3 &v3) {
    Vector3 diff21, diff31, cross;
    Subtract(v2, v1, diff21);
    Subtract(v3, v1, diff31);
    Cross(diff31, diff21, cross);
    Normalize(cross, cross);
    a = cross.x;
    b = cross.y;
    c = cross.z;
    d = -::Dot(cross, v1);
}

void SetBSPParams(float f1, float f2, int r3, int r4, float f3) {
    gBSPPosTol = f1;
    gBSPCheckScale = f3;
    gBSPMaxCandidates = r4;
    gBSPDirTol = f2;
    gBSPMaxDepth = r3;
}

DataNode SetBSPParams(DataArray *da) {
    SetBSPParams(da->Float(1), da->Float(2), da->Int(3), da->Int(4), da->Float(5));
    return 0;
}

void GeoInit() {
    DataArray *cfg = SystemConfig("math");
    SetBSPParams(cfg->FindArray("bsp_pos_tol")->Float(1), cfg->FindArray("bsp_dir_tol")->Float(1), cfg->FindArray("bsp_max_depth")->Int(1), cfg->FindArray("bsp_max_candidates")->Int(1), cfg->FindArray("bsp_check_scale")->Float(1));
    DataRegisterFunc("set_bsp_params", SetBSPParams);
}

bool CheckBSPTree(const BSPNode *node, const Box &box) {
    if (!gBSPCheckScale)
        return true;
    Box box68;
    Multiply(box, gBSPCheckScale, box68);
    Hmx::Polygon polygon70;
    polygon70.points.resize(4);
    Transform tf50;
    polygon70.points[0] = Vector2(box68.mMin.x, box68.mMin.y);
    polygon70.points[1] = Vector2(box68.mMax.x, box68.mMin.y);
    polygon70.points[2] = Vector2(box68.mMax.x, box68.mMax.y);
    polygon70.points[3] = Vector2(box68.mMin.x, box68.mMax.y);
    tf50.m.Identity();
    tf50.v.Set(0, 0, box68.mMin.z);
    if (Intersect(tf50, polygon70, node))
        return false;
    // first intersect check

    polygon70.points.clear();
    polygon70.points.resize(4);
    polygon70.points[0] = Vector2(box68.mMin.x, -box68.mMax.y);
    polygon70.points[1] = Vector2(box68.mMax.x, -box68.mMax.y);
    polygon70.points[2] = Vector2(box68.mMax.x, -box68.mMin.y);
    polygon70.points[3] = Vector2(box68.mMin.x, -box68.mMin.y);
    float negone = -1.0f;
    tf50.m.Set(1.0f, 0.0f, 0.0f, 0.0f, negone, 0.0f, 0.0f, 0.0f, 0.0f);
    tf50.v.Set(0, 0, box68.mMax.z);
    if (Intersect(tf50, polygon70, node))
        return false;
    // second intersect check

    polygon70.points.clear();
    polygon70.points.resize(4);
    polygon70.points[0] = Vector2(box68.mMin.y, box68.mMin.z);
    polygon70.points[1] = Vector2(box68.mMax.y, box68.mMin.z);
    polygon70.points[2] = Vector2(box68.mMax.y, box68.mMax.z);
    polygon70.points[3] = Vector2(box68.mMin.y, box68.mMax.z);
    tf50.m.Set(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    tf50.v.Set(box68.mMin.x, 0, 0);
    if (Intersect(tf50, polygon70, node))
        return false;
    // third intersect check

    polygon70.points.clear();
    polygon70.points.resize(4);
    polygon70.points[0] = Vector2(-box68.mMax.y, box68.mMin.z);
    polygon70.points[1] = Vector2(-box68.mMin.y, box68.mMin.z);
    polygon70.points[2] = Vector2(-box68.mMin.y, box68.mMax.z);
    polygon70.points[3] = Vector2(-box68.mMax.y, box68.mMax.z);
    tf50.m.Set(1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    tf50.v.Set(box68.mMax.x, 0, 0);
    if (Intersect(tf50, polygon70, node))
        return false;
    // fourth intersect check

    polygon70.points.clear();
    polygon70.points.resize(4);
    polygon70.points[0] = Vector2(box68.mMin.x, box68.mMin.z);
    polygon70.points[1] = Vector2(box68.mMax.x, box68.mMin.z);
    polygon70.points[2] = Vector2(box68.mMax.x, box68.mMax.z);
    polygon70.points[3] = Vector2(box68.mMin.x, box68.mMax.z);
    tf50.m.Set(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    tf50.v.Set(0, box68.mMax.y, 0);
    if (Intersect(tf50, polygon70, node))
        return false;
    // fifth intersect check

    polygon70.points.clear();
    polygon70.points.resize(4);
    polygon70.points[0] = Vector2(-box68.mMax.x, box68.mMin.z);
    polygon70.points[1] = Vector2(-box68.mMin.x, box68.mMin.z);
    polygon70.points[2] = Vector2(-box68.mMin.x, box68.mMax.z);
    polygon70.points[3] = Vector2(-box68.mMax.x, box68.mMax.z);
    tf50.m.Set(-1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    tf50.v.Set(0, box68.mMin.y, 0);
    if (Intersect(tf50, polygon70, node))
        return false;
    return true;
    // sixth and final intersect check
}

void MultiplyEq(BSPNode *n, const Transform &t) {
    for (; n != nullptr; n = n->right) {
        Multiply(n->plane, t, n->plane);
        Normalize(n->plane, n->plane);
        MultiplyEq(n->left, t);
    }
}

void Intersect(const Hmx::Ray &ray1, const Hmx::Ray &ray2, Vector2 &vec) {
    // Cache ray components for cleaner computation
    float r1dx = ray1.dir.x;
    float r2dx = ray2.dir.x;
    float r1dy = ray1.dir.y;
    float r2dy = ray2.dir.y;
    float r1bx = ray1.base.x;
    float r1by = ray1.base.y;
    float r2bx = ray2.base.x;
    float r2by = ray2.base.y;

    // Compute 2D cross product determinant
    float dot = r1dy * r2dx - r1dx * r2dy;

    if (dot != 0.0f) {
        // Solve for intersection parameter s
        float s = ((r2by - r1by) * r1dx + (r1bx - r2bx) * r1dy) / dot;
        // Compute intersection point
        vec.Set(s * r2dx + r2bx, s * r2dy + r2by);
    } else {
        // Rays are parallel, use ray1's base as fallback
        vec = ray1.base;
    }
}

void Intersect(const Transform &trans, const Plane &plane, Hmx::Ray &ray) {
    Vector3 on = plane.On();
    Vector3 point;
    MultiplyTranspose(on, trans, point);
    float dotX = trans.m.x.x * plane.a + trans.m.x.y * plane.b + trans.m.x.z * plane.c;
    float dotY = trans.m.y.x * plane.a + trans.m.y.y * plane.b + trans.m.y.z * plane.c;
    float dotZ = trans.m.z.x * plane.a + trans.m.z.y * plane.b + trans.m.z.z * plane.c;
    ray.dir.Set(dotX, dotY);
    if (fabsf(dotX) > fabsf(dotY)) {
        ray.base.Set(point.y, point.x + (dotZ / dotX) * point.z);
    }
    else {
        ray.base.Set(point.y + (dotZ / dotY) * point.z, point.x);
    }
}

void BSPFace::OnSide(const Plane &plane, bool &front, bool &back) {
    front = false;
    back = false;
    const Vector2 *it = p.points.begin();
    if (it != p.points.end()) {
        float posTol = gBSPPosTol;
        float negTol = -gBSPPosTol;
        do {
            Vector3 pt(it->x, it->y, 0.0f);
            Multiply(pt, t, pt);
            float dot = plane.a * pt.x + plane.b * pt.y + plane.c * pt.z + plane.d;
            if (dot > posTol) {
                front = true;
            }
            if (dot < negTol) {
                back = true;
            }
            it++;
        } while (it != p.points.end());
    }
}

bool Intersect(const Vector3 &origin, const Vector3 &dir, const Box &box, float &tmin, float &tmax) {
    tmin = 1.1920929e-07f;
    tmax = FLT_MAX;
    for (unsigned int i = 0; i < 3; i++) {
        float invDir = 1.0f / dir[i];
        float t1 = (box.mMin[i] - origin[i]) * invDir;
        float t2 = (box.mMax[i] - origin[i]) * invDir;
        if (t2 < t1) {
            float tmp = t1;
            t1 = t2;
            t2 = tmp;
        }
        tmin = t1 - tmin >= 0.0f ? t1 : tmin;
        tmax = t2 - tmax < 0.0f ? t2 : tmax;
        if (tmax < tmin) {
            return false;
        }
    }
    return true;
}

bool Intersect(const Plane &plane, const Box &box) {
    float hx = (box.mMax.x - box.mMin.x) * 0.5f;
    float hy = (box.mMax.y - box.mMin.y) * 0.5f;
    float hz = (box.mMax.z - box.mMin.z) * 0.5f;
    Vector3 halfExtent;
    halfExtent.x = box.mMax.x - (box.mMin.x + hx);
    halfExtent.y = box.mMax.y - (box.mMin.y + hy);
    halfExtent.z = box.mMax.z - (box.mMin.z + hz);

    Vector3 pMin, pMax;
    for (unsigned int i = 0; i < 3; i++) {
        const Vector3 &normal = *(const Vector3 *)&plane.a;
        if (normal[i] > 0.0f) {
            pMin[i] = -halfExtent[i];
            pMax[i] = halfExtent[i];
        } else {
            pMin[i] = halfExtent[i];
            pMax[i] = -halfExtent[i];
        }
    }

    const Vector3 &normal = *(const Vector3 *)&plane.a;
    if (0.0f < normal.x * pMin.x + normal.y * pMin.y + normal.z * pMin.z + plane.d
        || normal.x * pMax.x + normal.y * pMax.y + normal.z * pMax.z + plane.d < 0.0f) {
        return false;
    }
    return true;
}

bool Intersect(const Triangle &tri, const Box &box) {
    Vector3 v0 = tri.origin;

    float halfX = (box.mMax.x - box.mMin.x) * 0.5f;
    float halfY = (box.mMax.y - box.mMin.y) * 0.5f;
    float halfZ = (box.mMax.z - box.mMin.z) * 0.5f;

    float cx = box.mMin.x + halfX;
    float cy = box.mMin.y + halfY;
    float cz = box.mMin.z + halfZ;

    // Translate triangle to box center
    float v0x = v0.x - cx;
    float v1x = (tri.frame.x.x + tri.origin.x) - cx;
    float v2x = (tri.frame.y.x + tri.origin.x) - cx;

    float v0y = v0.y - cy;
    float v1y = (tri.frame.x.y + tri.origin.y) - cy;
    float v2y = (tri.frame.y.y + tri.origin.y) - cy;

    float v0z = v0.z - cz;
    float v1z = (tri.frame.x.z + tri.origin.z) - cz;
    float v2z = (tri.frame.y.z + tri.origin.z) - cz;

    // X axis separation test (fsel ternary for min/max)
    {
        float diff = v0x - v1x;
        float mn = diff >= 0.0f ? v1x : v0x;
        float mx = diff >= 0.0f ? v0x : v1x;
        float sub_mn = mn - v2x;
        float sub_mx = mx - v2x;
        mn = sub_mn >= 0.0f ? v2x : mn;
        mx = sub_mx >= 0.0f ? mx : v2x;
        if (mn > halfX || mx < -halfX) return false;
    }

    // Y axis separation test
    {
        float diff = v0y - v1y;
        float mn = diff >= 0.0f ? v1y : v0y;
        float mx = diff >= 0.0f ? v0y : v1y;
        float sub_mn = mn - v2y;
        float sub_mx = mx - v2y;
        mn = sub_mn >= 0.0f ? v2y : mn;
        mx = sub_mx >= 0.0f ? mx : v2y;
        if (mn > halfY || mx < -halfY) return false;
    }

    // Z axis separation test
    {
        float diff = v0z - v1z;
        float mn = diff >= 0.0f ? v1z : v0z;
        float mx = diff >= 0.0f ? v0z : v1z;
        float sub_mn = mn - v2z;
        float sub_mx = mx - v2z;
        mn = sub_mn >= 0.0f ? v2z : mn;
        mx = sub_mx >= 0.0f ? mx : v2z;
        if (mn > halfZ || mx < -halfZ) return false;
    }

    // Face normal plane test — reuse v0 stack for plane
    float ny = tri.frame.z.y;
    float nz = tri.frame.z.z;
    float nx = tri.frame.z.x;
    Plane facePlane;
    facePlane.a = nx;
    facePlane.b = ny;
    facePlane.c = nz;
    facePlane.d = -(nx * v0x + (nz * v0z + ny * v0y));
    if (!Intersect(facePlane, box)) return false;

    // Edge cross product axes (9 tests)
    float e0x = v1x - v0x, e0y = v1y - v0y, e0z = v1z - v0z;
    float e1x = v2x - v1x, e1y = v2y - v1y, e1z = v2z - v1z;
    float e2x = v0x - v2x, e2y = v0y - v2y, e2z = v0z - v2z;

    // Cross products with box axes — 4-float stride (ax, ay, az, pad)
    float axes[9][4] = {
        { 0, -e0z, e0y, 0 },
        { e0z, 0, -e0x, 0 },
        { -e0y, e0x, 0, 0 },
        { 0, -e1z, e1y, 0 },
        { e1z, 0, -e1x, 0 },
        { -e1y, e1x, 0, 0 },
        { 0, -e2z, e2y, 0 },
        { e2z, 0, -e2x, 0 },
        { -e2y, e2x, 0, 0 },
    };

    float radii[9];
    float *pfAxis = &axes[0][1];
    float *pfR = radii;
    unsigned int i = 0;
    do {
        float ax = pfAxis[-1], ay = pfAxis[0], az = pfAxis[1];
        float absx = ax; if (absx <= 0.0f) absx = -absx;
        float absy = ay; if (absy <= 0.0f) absy = -absy;
        float absz = az; if (absz <= 0.0f) absz = -absz;
        float r = absx * halfX + absy * halfY + absz * halfZ;
        *pfR = r;

        float p0 = ax * v0x + ay * v0y + az * v0z;
        float p1 = ax * v1x + ay * v1y + az * v1z;
        float p2 = ax * v2x + ay * v2y + az * v2z;

        float diff = p1 - p2;
        float mx = diff >= 0.0f ? p1 : p2;
        float mn = diff >= 0.0f ? p2 : p1;
        mx = p0 - mx >= 0.0f ? p0 : mx;
        if (mx < -r) return false;
        mn = p0 - mn >= 0.0f ? mn : p0;
        if (r < mn) return false;

        i++;
        pfAxis += 4;
        pfR++;
    } while (i < 9);

    return true;
}

bool Intersect(const Segment &seg, const Triangle &tri, bool b, float &out) {
    float segDirX = seg.end.x - seg.start.x;
    float segDirY = seg.end.y - seg.start.y;
    float segDirZ = seg.end.z - seg.start.z;

    const Vector3 &triFrameZ = tri.frame.z;
    float segDirDot = triFrameZ.x * segDirX + triFrameZ.y * segDirY + triFrameZ.z * segDirZ;

    if (fabs(segDirDot) < 0.0001f || (b && segDirDot > 0.0f)) {
        return false;
    }

    float vec3AX = seg.start.x - tri.origin.x;
    float vec3AY = seg.start.y - tri.origin.y;
    float vec3AZ = seg.start.z - tri.origin.z;

    float tempDot = triFrameZ.x * vec3AX + triFrameZ.y * vec3AY + triFrameZ.z * vec3AZ;
    float t = -(tempDot / segDirDot);
    out = t;

    if (t < 0.0f || t > 1.0f) {
        return false;
    }

    float vec3BX = (seg.start.x + segDirX * t) - tri.origin.x;
    float vec3BY = (seg.start.y + segDirY * t) - tri.origin.y;
    float vec3BZ = (seg.start.z + segDirZ * t) - tri.origin.z;

    const Vector3 &triFrameX = tri.frame.x;
    const Vector3 &triFrameY = tri.frame.y;

    float dotXX = triFrameX.x * triFrameX.x + triFrameX.y * triFrameX.y + triFrameX.z * triFrameX.z;
    float dotYY = triFrameY.x * triFrameY.x + triFrameY.y * triFrameY.y + triFrameY.z * triFrameY.z;
    float dotXY = triFrameX.x * triFrameY.x + triFrameX.y * triFrameY.y + triFrameX.z * triFrameY.z;
    float dotX3B = triFrameX.x * vec3BX + triFrameX.y * vec3BY + triFrameX.z * vec3BZ;
    float dotY3B = triFrameY.x * vec3BX + triFrameY.y * vec3BY + triFrameY.z * vec3BZ;

    float inv = 1.0f / (dotXY * dotXY - dotYY * dotXX);
    float k = (dotY3B * dotXY - dotX3B * dotYY) * inv;
    if (k < 0.0f || k > 1.0f) {
        return false;
    }
    float j = (dotX3B * dotXY - dotY3B * dotXX) * inv;
    if (j < 0.0f || k + j > 1.0f) {
        return false;
    }
    return true;
}

#ifndef HX_NATIVE
// Comparator and list operations for BSPFace
namespace stlpmtx_std {
    // Compare BSPFace by area field - used for sorting in descending order
    template<>
    struct less<BSPFace> {
        bool operator()(const BSPFace& a, const BSPFace& b) const {
            return a.area > b.area; // Note: greater for descending sort
        }
    };

    template<>
    void _S_sort<BSPFace, StlNodeAlloc<BSPFace>, less<BSPFace>>(
        std::list<BSPFace, StlNodeAlloc<BSPFace>>& __that,
        less<BSPFace> __comp) {
        std::list<BSPFace, StlNodeAlloc<BSPFace>>::iterator __it = __that.begin();
        std::list<BSPFace, StlNodeAlloc<BSPFace>>::iterator __end = __that.end();

        // Do nothing if the list has length 0 or 1.
        if (__it != __end) {
            ++__it;
            if (__it != __end) {
                std::list<BSPFace, StlNodeAlloc<BSPFace>> __carry(__that.get_allocator());
                std::list<BSPFace, StlNodeAlloc<BSPFace>> __counter[64];
                int __fill = 0;
                while (!__that.empty()) {
                    __carry.splice(__carry.begin(), __that, __that.begin());
                    int __i = 0;
                    while(__i < __fill && !__counter[__i].empty()) {
                        _S_merge(__counter[__i], __carry, __comp);
                        __carry.swap(__counter[__i++]);
                    }
                    __carry.swap(__counter[__i]);
                    if (__i == __fill) ++__fill;
                }

                for (int __i = 1; __i < __fill; ++__i)
                    _S_merge(__counter[__i], __counter[__i-1], __comp);
                __that.swap(__counter[__fill-1]);
            }
        }
    }

    template<>
    std::list<BSPFace, StlNodeAlloc<BSPFace>>::iterator
    list<BSPFace, StlNodeAlloc<BSPFace>>::insert(
        std::list<BSPFace, StlNodeAlloc<BSPFace>>::iterator __pos,
        const BSPFace& __x) {
        _List_node_base* __tmp = _M_create_node(__x);
        _List_node_base* __pos_node = __pos._M_node;
        _List_node_base* __prev_node = __pos_node->_M_prev;

        __tmp->_M_next = __pos_node;
        __tmp->_M_prev = __prev_node;
        __prev_node->_M_next = __tmp;
        __pos_node->_M_prev = __tmp;

        return iterator(__tmp);
    }
}
#endif // HX_NATIVE

void Multiply(const Box &box, float f, Box &out) {
    Vector3 center;
    Interp(box.mMin, box.mMax, 0.5f, center);
    float hsy = box.mMax.y - center.y;
    float hsx = box.mMax.x - center.x;
    out.mMin.y = center.y - hsy * f;
    float hsz = box.mMax.z - center.z;
    out.mMin.z = center.z - hsz * f;
    out.mMin.x = center.x - hsx * f;
    out.mMax.z = hsz * f + center.z;
    out.mMax.x = hsx * f + center.x;
    out.mMax.y = hsy * f + center.y;
}

void Multiply(const Plane &p, const Transform &t, Plane &out) {
    Hmx::Matrix3 invM;
    FastInvert(t.m, invM);
    float b = p.b;
    float a = p.a;
    float c = p.c;
    float nx = invM.x.y * b + invM.x.x * a + invM.x.z * c;
    float ny = invM.y.y * b + invM.y.x * a + invM.y.z * c;
    float nz = invM.z.y * b + invM.z.x * a + invM.z.z * c;
    float scalar = -(p.d / (b * b + a * a + c * c));
    Vector3 on(a * scalar, b * scalar, c * scalar);
    Vector3 pOut;
    Multiply(on, t, pOut);
    out.Set(nx, ny, nz, -(pOut.y * ny + (pOut.z * nz + pOut.x * nx)));
}

void Sphere::GrowToContain(const Sphere &s) {
    if (s.radius == 0.0f)
        return;
    if (radius == 0.0f) {
        center = s.center;
        radius = s.radius;
        return;
    }
    float dx = s.center.x - center.x;
    float dy = s.center.y - center.y;
    float dz = s.center.z - center.z;
    float dist = std::sqrt((dy * dy + (dz * dz + dx * dx)));
    if (s.radius + dist > radius) {
        if (radius + dist < s.radius) {
            center = s.center;
            radius = s.radius;
            return;
        }
        if (dist == 0.0f)
            return;
        float invDist = 1.0f / dist;
        Vector3 a, b;
        a.x = center.x - (radius * (invDist * dx));
        a.z = center.z - dz * invDist * radius;
        b.x = s.center.x + s.radius * (dx * invDist);
        b.y = s.center.y + s.radius * (invDist * dy);
        a.y = center.y - radius * (invDist * dy);
        b.z = s.center.z + dz * invDist * s.radius;
        Interp(a, b, 0.5f, center);
        radius = (dist + s.radius + radius) * 0.5f;
        return;
    }
}

void Frustum::Set(float near, float far, float fovY, float ratio) {
    front.Set(0, 1, 0, -near);
    back.Set(0, -1, 0, far);
    float halfY = fovY * 0.5f;
    float sy = std::sin(halfY);
    float cy = std::cos(halfY);
    float sx = sy / ratio;
    top.Set(0, sy, -cy, 0);
    bottom.Set(0, sy, cy, 0);
    float len = std::sqrt(sx * sx + cy * cy);
    if (len != 0.0f) {
        len = 1.0f / len;
    }
    float la = len * cy;
    float lb = len * sx;
    left.Set(la, lb, 0, 0);
    right.Set(-la, lb, 0, 0);
    if (fovY == 0.0f) {
        right.d = 1.0f;
        left.d = 1.0f;
        top.d = ratio;
        bottom.d = ratio;
    }
}

bool operator>(const Sphere &s, const Frustum &f) {
    if (s < f.front || s < f.back || s < f.left || s < f.right || s < f.top || s < f.bottom)
        return false;
    return true;
}

bool Intersect(const Segment &seg, const Sphere &sphere) {
    float dir_z = seg.end.z - seg.start.z;
    float dir_x = seg.end.x - seg.start.x;
    float dir_y = seg.end.y - seg.start.y;
    float center_z = sphere.center.z;
    float center_x = sphere.center.x;
    float center_y = sphere.center.y;
    float a = dir_z * dir_z + dir_x * dir_x + dir_y * dir_y;
    if (a == 0.0f)
        return false;
    float t = ((center_y - seg.start.y) * dir_y
        + (center_x - seg.start.x) * dir_x
        + (center_z - seg.start.z) * dir_z) / a;
    float zero = 0.0f;
    float neg_t = -t;
    t = (neg_t >= 0.0f) ? zero : t;
    float one = 1.0f;
    float t_minus_one = t - one;
    t = (t_minus_one >= 0.0f) ? one : t;
    Vector3 closest;
    Interp(seg.start, seg.end, t, closest);
    float dy = closest.y - center_y;
    float dx = closest.x - center_x;
    float dz = closest.z - center_z;
    float r = sphere.radius;
    float r2 = r * r;
    float dist2 = dy * dy + dx * dx + dz * dz;
    if (dist2 > r2)
        return false;
    return true;
}

bool Intersect(const Vector3 &v, const BSPNode *n) {
    if (!n)
        return true;
    MILO_ASSERT(n, 0x4ca);
    if (n->plane.Dot(v) >= 0)
        return Intersect(v, n->left);
    else
        return Intersect(v, n->right);
}

bool Intersect(const Segment &seg, const BSPNode *n, float &t, Plane &p) {
    MILO_ASSERT(n, 0x4e6);

    float startDot = n->plane.Dot(seg.start);
    float endDot = n->plane.Dot(seg.end);

    if (startDot >= 0.0f && endDot >= 0.0f) {
        if (!n->left)
            return false;
        return Intersect(seg, n->left, t, p);
    }

    if (!(startDot > 0.0f) && !(endDot > 0.0f)) {
        if (!n->right) {
            t = 0.0f;
            return true;
        }
        return Intersect(seg, n->right, t, p);
    }

    float t2 = 0.0f;
    float denom = startDot - endDot;
    if (denom == 0.0f)
        return false;

    float frac = startDot / denom;
    Vector3 mid;
    Interp(seg.start, seg.end, frac, mid);

    Segment seg1;
    seg1.start = seg.start;
    seg1.end = seg.end;
    Segment seg2;
    seg2.start = mid;
    seg2.end = seg.end;

    if (startDot <= endDot) {
        if (!n->right) {
            t = frac;
            goto done_neg;
        }
        if (Intersect(seg1, n->right, t2, p)) {
            t = frac * t2;
        } else {
            if (!n->left || !Intersect(seg2, n->left, t2, p))
                return false;
            t = (1.0f - frac) * t2 + frac;
        }
        t = frac;
    done_neg:
        if (t2 == 0.0f && t != 0.0f) {
            p.a = -n->plane.a;
            p.b = -n->plane.b;
            p.c = -n->plane.c;
            p.d = -n->plane.d;
        }
    } else {
        if (!n->left) {
            if (!n->right || !Intersect(seg2, n->right, t2, p))
                return false;
            t = (1.0f - frac) * t2 + frac;
            t = frac;
            if (t2 == 0.0f && t != 0.0f) {
                p.a = n->plane.a;
                p.b = n->plane.b;
                p.c = n->plane.c;
                p.d = n->plane.d;
            }
        } else {
            if (Intersect(seg1, n->left, t2, p)) {
                t = frac * t2;
            } else {
                if (!n->right || !Intersect(seg2, n->right, t2, p))
                    return false;
                t = (1.0f - frac) * t2 + frac;
            }
            t = frac;
            if (t2 == 0.0f && t != 0.0f) {
                p.a = n->plane.a;
                p.b = n->plane.b;
                p.c = n->plane.c;
                p.d = n->plane.d;
            }
        }
    }
    return true;
}

bool Intersect(
    const Vector3 &v1, const Vector3 &v2, const Triangle &tri, float &out
) {
    float fy_z = tri.frame.y.z;
    float v2_x = v2.x;
    float h_x = v2.y * tri.frame.y.z - v2.z * tri.frame.y.y;
    float s_z = v1.z - tri.origin.z;
    float s_x = v1.x - tri.origin.x;
    float h_z = v2_x * tri.frame.y.y - tri.frame.y.x * v2.y;
    float s_y = v1.y - tri.origin.y;
    float h_y = tri.frame.y.x * v2.z - v2_x * fy_z;

    float fx_x = tri.frame.x.x;
    float fx_y = tri.frame.x.y;
    float fx_z = tri.frame.x.z;

    float u_num = s_x * h_x + s_z * h_z;
    float a = fx_x * h_x + fx_z * h_z;
    u_num = s_y * h_y + u_num;
    a = fx_y * h_y + a;

    if (0.0f <= u_num && u_num <= a) {
        float q_z = fx_y * s_x - fx_x * s_y;
        float q_y = fx_x * s_z - fx_z * s_x;
        float q_x = fx_z * s_y - fx_y * s_z;

        float v_num = v2.y * q_y + v2.z * q_z + v2_x * q_x;

        if (0.0f <= v_num &&
            v_num + u_num <= a) {
            float t = (tri.frame.y.y * q_y + tri.frame.y.z * q_z + tri.frame.y.x * q_x) / a;
            out = t;
            if (1.1920929e-07f <= t)
                return true;
        }
    }
    return false;
}

void BSPFace::Set(const Vector3 &p1, const Vector3 &p2, const Vector3 &p3) {
    Subtract(p2, p1, t.m.x);
    Normalize(t.m.x, t.m.x);

    Subtract(p3, p1, t.m.y);
    Cross(t.m.x, t.m.y, t.m.z);
    Normalize(t.m.z, t.m.z);
    Cross(t.m.z, t.m.x, t.m.y);

    t.v = p1;

    p.points.clear();
    Vector3 v;
    Vector2 pt;

    MultiplyTranspose(p1, t, v);
    pt.Set(v.x, v.y);
    p.points.push_back(pt);

    MultiplyTranspose(p2, t, v);
    pt.Set(v.x, v.y);
    p.points.push_back(pt);

    MultiplyTranspose(p3, t, v);
    pt.Set(v.x, v.y);
    p.points.push_back(pt);

    Update();
}

void BSPFace::Update() {
    MILO_ASSERT(p.points.size() > 2, 0x6c2);

    const Vector2 *anchor = p.points.begin();
    const Vector2 *v1 = anchor + 1;
    const Vector2 *v2 = anchor + 2;
    area = 0.0f;
    if (v2 != p.points.end()) {
        const Vector2 *nextPt;
        do {
            nextPt = v2 + 1;
            area += (v1->y * anchor->x - v1->x * anchor->y +
                     v2->x * anchor->y - v2->y * anchor->x +
                     v2->y * v1->x - v2->x * v1->y) * 0.5f;
            v1 = v2;
            v2 = nextPt;
        } while (nextPt != p.points.end());
    }

    planes.clear();

    Plane facePlane;
    facePlane.a = t.m.z.x;
    facePlane.b = t.m.z.y;
    facePlane.c = t.m.z.z;
    facePlane.d = -(t.m.z.x * t.v.x + t.m.z.y * t.v.y + t.m.z.z * t.v.z);
    planes.insert(planes.end(), facePlane);

    Vector3 prevPt(p.points.back().x, p.points.back().y, 0.0f);
    Multiply(prevPt, t, prevPt);

    for (const Vector2 *it = p.points.begin(); it != p.points.end(); it++) {
        Vector3 curPt(it->x, it->y, 0.0f);
        Multiply(curPt, t, curPt);

        float dx = curPt.x - prevPt.x;
        float dy = curPt.y - prevPt.y;
        float dz = curPt.z - prevPt.z;

        if (dx != 0.0f || dy != 0.0f || dz != 0.0f) {
            Vector3 normal;
            normal.x = t.m.z.z * dy - t.m.z.y * dz;
            normal.y = t.m.z.x * dz - t.m.z.z * dx;
            normal.z = t.m.z.y * dx - t.m.z.x * dy;
            Normalize(normal, normal);

            Plane edgePlane;
            edgePlane.a = normal.x;
            edgePlane.b = normal.y;
            edgePlane.c = normal.z;
            edgePlane.d = -(normal.x * curPt.x + (normal.y * curPt.y + normal.z * curPt.z));
            planes.insert(planes.end(), edgePlane);

            prevPt = curPt;
        }
    }
}

#ifndef HX_NATIVE
bool MakeBSPTree(BSPNode *&node, std::list<BSPFace> &faces, int depth) {
    if (faces.empty()) {
        node = nullptr;
        return true;
    }
    int nextDepth = depth + 1;
    if (nextDepth > gBSPMaxDepth) {
        TheDebug.Notify(MakeString("Bsp too deep"));
        return false;
    }
    node = new BSPNode();
    stlpmtx_std::_S_sort<BSPFace, stlpmtx_std::StlNodeAlloc<BSPFace>, stlpmtx_std::less<BSPFace>>(faces, stlpmtx_std::less<BSPFace>());

    int totalFaces = 0;
    for (std::list<BSPFace>::iterator it = faces.begin(); it != faces.end(); ++it)
        totalFaces++;

    int candidateIdx = 0;
    float bestScore = -1.0f;
    float zero = 0.0f;
    double powExp = (double)0.6f;
    for (std::list<BSPFace>::iterator faceIt = faces.begin(); faceIt != faces.end(); ++faceIt) {
        if (candidateIdx >= gBSPMaxCandidates) break;
        for (std::list<Plane>::iterator planeIt = faceIt->planes.begin(); planeIt != faceIt->planes.end(); ++planeIt) {
            if (totalFaces == 1) {
                node->plane = *planeIt;
                bestScore = zero;
                break;
            }
            int frontCount = 0, backCount = 0, spanCount = 0;
            float frontArea = zero, backArea = zero;
            std::list<BSPFace>::iterator jt;
            for (jt = faces.begin(); jt != faces.end(); ++jt) {
                bool front, back;
                jt->OnSide(*planeIt, front, back);
                if (!front && !back) {
                    if (fabs(planeIt->a * jt->t.m.z.x + planeIt->b * jt->t.m.z.y + planeIt->c * jt->t.m.z.z) < gBSPDirTol)
                        break;
                } else {
                    if (back) {
                        backArea += jt->area;
                        backCount++;
                        if (!front) continue;
                        spanCount++;
                    }
                    frontArea += jt->area;
                    frontCount++;
                }
            }
            if (jt != faces.end()) {
                candidateIdx--;
                continue;
            }
            float powBack = (float)pow((double)(spanCount + backCount), powExp);
            float score = (float)pow((double)(spanCount + frontCount), powExp) * frontArea
                        + powBack * backArea;
            if (frontCount < totalFaces && backCount < totalFaces && (bestScore < zero || score < bestScore)) {
                node->plane = *planeIt;
                bestScore = score;
            }
        }
        candidateIdx++;
    }

    if (bestScore < zero) {
        TheDebug.Notify(MakeString("Couldn't find candidate plane"));
        return false;
    }

    std::list<BSPFace> backFaces, frontFaces;
    std::list<BSPFace>::iterator it = faces.begin();
    while (it != faces.end()) {
        std::list<BSPFace>::iterator cur = it++;
        bool front, back;
        cur->OnSide(node->plane, front, back);
        if (!front && !back) {
            faces.erase(cur);
        } else if (!back) {
            frontFaces.splice(frontFaces.begin(), faces, cur);
        } else if (!front) {
            backFaces.splice(backFaces.begin(), faces, cur);
        } else {
            Hmx::Ray ray;
            Intersect(cur->t, node->plane, ray);
            BSPFace frontFace;
            frontFace.t = cur->t;
            Clip(cur->p, ray, frontFace.p);
            if (frontFace.p.points.size() > 2) {
                frontFace.Update();
                frontFaces.insert(frontFaces.begin(), frontFace);
            }
            ray.dir.Set(-ray.dir.x, -ray.dir.y);
            Clip(cur->p, ray, cur->p);
            if (cur->p.points.size() > 2) {
                cur->Update();
                backFaces.splice(backFaces.begin(), faces, cur);
            }
        }
    }

    bool ok = MakeBSPTree(node->left, frontFaces, nextDepth);
    if (!ok) {
        backFaces.clear();
        frontFaces.clear();
        return false;
    }
    ok = MakeBSPTree(node->right, backFaces, nextDepth);
    backFaces.clear();
    frontFaces.clear();
    return ok;
}
#else
bool MakeBSPTree(BSPNode *&, std::list<BSPFace> &, int) { return false; }
#endif

bool Intersect(const Transform &tf, const Hmx::Polygon &poly, const BSPNode *node) {
    if (!node)
        return true;

    // Classify all polygon points against the BSP split plane
    bool front = false;
    bool back = false;
    for (const Vector2 *i = poly.points.begin(); i != poly.points.end(); i++) {
        Vector3 v(i->x, i->y, 0.0f);
        Multiply(v, tf, v);
        float dot = node->plane.Dot(v);
        if (dot >= 0.0f)
            front = true;
        if (dot < 0.0f)
            back = true;
    }

    if (front && !back) {
        // Entirely in front
        return Intersect(tf, poly, node->left);
    }
    if (!front && back) {
        // Entirely behind
        return Intersect(tf, poly, node->right);
    }

    // Polygon straddles the plane - clip and test both sides
    Hmx::Ray r;
    Intersect(tf, node->plane, r);

    Hmx::Polygon splitPoly;
    Clip(poly, r, splitPoly);
    if (!splitPoly.points.empty() && !Intersect(tf, splitPoly, node->left))
        return false;

    // Clip against the other side (negate the ray direction)
    Hmx::Ray negRay;
    negRay.base = r.base;
    negRay.dir.Set(-r.dir.x, -r.dir.y);
    Hmx::Polygon splitPoly2;
    Clip(poly, negRay, splitPoly2);
    if (!splitPoly2.points.empty() && !Intersect(tf, splitPoly2, node->right))
        return false;

    return true;
}

void Clip(const Hmx::Polygon &poly, const Hmx::Ray &ray, Hmx::Polygon &out) {
    // Clip a 2D polygon against a half-plane defined by the ray
    // The ray defines a line: points on the left side (positive dot) are kept
    // Ray normal direction: (-dir.y, dir.x)
    std::vector<Vector2> tempPoints;
    std::vector<Vector2> *newPoints = &out.points;

    float lastDot = ray.dir.x * (poly.points.back().y - ray.base.y)
                  - ray.dir.y * (poly.points.back().x - ray.base.x);
    const Vector2 *lastPoint = &poly.points.back();

    for (const Vector2 *i = poly.points.begin(); i != poly.points.end(); i++) {
        float dot = ray.dir.x * (i->y - ray.base.y) - ray.dir.y * (i->x - ray.base.x);

        if (dot >= 0.0f) {
            if (lastDot < 0.0f) {
                // Entering: compute intersection and add it
                float t = lastDot / (lastDot - dot);
                Vector2 v;
                v.Set(lastPoint->x + t * (i->x - lastPoint->x),
                      lastPoint->y + t * (i->y - lastPoint->y));
                newPoints->push_back(v);
            }
            newPoints->push_back(*i);
        } else {
            if (lastDot >= 0.0f) {
                // Leaving: compute intersection and add it
                float t = lastDot / (lastDot - dot);
                Vector2 v;
                v.Set(lastPoint->x + t * (i->x - lastPoint->x),
                      lastPoint->y + t * (i->y - lastPoint->y));
                newPoints->push_back(v);
            }
        }

        lastDot = dot;
        lastPoint = i;
    }
}
