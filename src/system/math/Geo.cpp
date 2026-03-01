#include "math/Geo.h"
#include "Vec.h"
#include "math/Mtx.h"
#include "math/Sphere.h"
#include "math/Utl.h"
#include "math/Vec.h"
#include "obj/DataFunc.h"
#include "os/System.h"
#include "utl/BinStream.h"
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
    gBSPDirTol = f2;
    gBSPMaxDepth = r3;
    gBSPMaxCandidates = r4;
    gBSPCheckScale = f3;
}

DataNode SetBSPParams(DataArray *da) {
    SetBSPParams(da->Float(1), da->Float(2), da->Int(3), da->Int(4), da->Float(5));
    return 0;
}

void GeoInit() {
    DataArray *cfg = SystemConfig("math");
    float scale = cfg->FindArray("bsp_check_scale")->Float(1);
    int candidates = cfg->FindArray("bsp_max_candidates")->Int(1);
    int depth = cfg->FindArray("bsp_max_depth")->Int(1);
    float dirtol = cfg->FindArray("bsp_dir_tol")->Float(1);
    float postol = cfg->FindArray("bsp_pos_tol")->Float(1);
    SetBSPParams(postol, dirtol, depth, candidates, scale);
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
    Vector3 halfSize;
    Subtract(box.mMax, center, halfSize);
    Scale(halfSize, f, halfSize);
    Subtract(center, halfSize, out.mMin);
    Add(center, halfSize, out.mMax);
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
    if (radius != 0.0f) {
        float dx = s.center.x - center.x;
        float dz = s.center.z - center.z;
        float dy = s.center.y - center.y;
        float dist = std::sqrt(dy * dy + dz * dz + dx * dx);
        if (s.radius + dist > radius) {
            if (!(radius + dist < s.radius)) {
                if (dist == 0.0f)
                    return;
                float invDist = 1.0f / dist;
                Vector3 a, b;
                a.x = center.x - dx * invDist * radius;
                a.z = center.z - dz * invDist * radius;
                b.x = s.center.x + s.radius * (dx * invDist);
                b.y = s.center.y + s.radius * (invDist * dy);
                a.y = center.y - radius * (invDist * dy);
                b.z = s.center.z + dz * invDist * s.radius;
                Interp(a, b, 0.5f, center);
                radius = (dist + s.radius + radius) * 0.5f;
                return;
            }
        } else {
            return;
        }
    }
    center = s.center;
    radius = s.radius;
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
        left.d = 1.0f;
        right.d = 1.0f;
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
    if (!n)
        return false;
    MILO_ASSERT(n, 0x4e6);
    float zero = 0.0f;
    float startDot = n->plane.Dot(seg.start);
    float endDot = n->plane.Dot(seg.end);
    if (startDot >= 0 && endDot >= 0) {
        if (!n->left)
            return false;
        return Intersect(seg, n->left, t, p);
    }
    if (!(startDot > 0) && !(endDot > 0)) {
        if (!n->right) {
            t = zero;
            return true;
        }
        return Intersect(seg, n->right, t, p);
    }
    float denom = startDot - endDot;
    if (denom == 0.0f)
        return false;
    float frac = startDot / denom;
    Vector3 mid;
    Interp(seg.start, seg.end, frac, mid);
    if (startDot >= 0) {
        Segment seg1;
        seg1.start = seg.start;
        seg1.end = mid;
        if (Intersect(seg1, n->left, t, p))
            return true;
        t = frac;
        p = n->plane;
        Segment seg2;
        seg2.start = mid;
        seg2.end = seg.end;
        float t2;
        if (Intersect(seg2, n->right, t2, p)) {
            t = frac + t2 * (1.0f - frac);
            return true;
        }
        return true;
    } else {
        Segment seg1;
        seg1.start = seg.start;
        seg1.end = mid;
        if (Intersect(seg1, n->right, t, p))
            return true;
        t = frac;
        p = n->plane;
        Segment seg2;
        seg2.start = mid;
        seg2.end = seg.end;
        float t2;
        if (Intersect(seg2, n->left, t2, p)) {
            t = frac + t2 * (1.0f - frac);
            return true;
        }
        return true;
    }
}

bool Intersect(
    const Vector3 &v1, const Vector3 &v2, const Triangle &tri, float &out
) {
    // h = cross(v2, tri.frame.y)
    // Compute components using fmsubs pattern: a*b - c
    float fy_x = tri.frame.y.x;
    float fy_y = tri.frame.y.y;
    float fy_z = tri.frame.y.z;
    float v2_y = v2.y;
    float v2_z = v2.z;
    float v2_x = v2.x;
    float eps = 1e-4f;

    // h.z = v2.x * fy.y - fy.x * v2.y
    float hz_partial = fy_x * v2_y;   // fy.x * v2.y
    // h.x = v2.y * fy.z - v2.z * fy.y
    float hx_partial = v2_z * fy_y;   // v2.z * fy.y
    // h.y = fy.x * v2.z - v2.x * fy.z
    float hy_partial = v2_x * fy_z;   // v2.x * fy.z

    float h_z = v2_x * fy_y - hz_partial;   // h.z = v2.x*fy.y - fy.x*v2.y
    float h_x = v2_y * fy_z - hx_partial;   // h.x = v2.y*fy.z - v2.z*fy.y
    float h_y = fy_x * v2_z - hy_partial;   // h.y = fy.x*v2.z - v2.x*fy.z

    // s = v1 - tri.origin
    float s_z = v1.z - tri.origin.z;
    float s_x = v1.x - tri.origin.x;
    float s_y = v1.y - tri.origin.y;

    float fx_z = tri.frame.x.z;
    float fx_x = tri.frame.x.x;
    float fx_y = tri.frame.x.y;

    // Compute a = dot(frame.x, h) and u_num = dot(s, h) simultaneously
    float u_z_part = s_z * h_z;
    float a_z_part = fx_z * h_z;
    float u_num = s_x * h_x + u_z_part;
    float a = fx_x * h_x + a_z_part;
    u_num = s_y * h_y + u_num;
    a = fx_y * h_y + a;

    if (u_num < eps)
        return false;
    if (u_num > a)
        return false;

    // q = cross(s, frame.x)
    // q.z = s.x * fx.y - fx.x * s.y
    // q.y = fx.x * s.z - fx.z * s.x
    // q.x = fx.z * s.y - fx.y * s.z
    float q_z = fx_y * s_x - fx_x * s_y;
    float q_y = fx_x * s_z - fx_z * s_x;
    float q_x = fx_z * s_y - fx_y * s_z;

    // v_num = dot(v2, q)
    float v_num = v2_y * q_y + v2_z * q_z + v2_x * q_x;

    if (v_num < eps)
        return false;
    if (v_num + u_num > a)
        return false;

    // t = dot(frame.y, q) / a
    float t_num = fy_y * q_y + fy_z * q_z + fy_x * q_x;
    float t = t_num / a;
    out = t;

    float min_t = 1.192093e-7f;   // FLT_EPSILON (0x34000000)
    if (t < min_t)
        return false;
    return true;
}
