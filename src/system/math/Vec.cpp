#include "math/Mtx.h"
#include "math/Vec.h"

void ScaleAddEq(Hmx::Matrix3 &m1, const Hmx::Matrix3 &m2, float f) {
    ScaleAdd(m1.x, m2.x, f, m1.x);
    ScaleAdd(m1.y, m2.y, f, m1.y);
    ScaleAdd(m1.z, m2.z, f, m1.z);
}

void ScaleAddEq(Transform &tf1, const Transform &tf2, float f) {
    ScaleAddEq(tf1.m, tf2.m, f);
    ScaleAdd(tf1.v, tf2.v, f, tf1.v);
}

